extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
}
#include <cassert>
#include <cstdio>
#include <vector>

#ifdef NDEBUG
#error These tests require assertions.
#endif

namespace {
std::vector<AVFrame *> retained;
bool packet_pending, fail_allocate, fail_send;
int allocations, writable_calls, uploads, hardware_allocations;
int width = 66, height = 34;
AVPixelFormat format;

int get_buffer(AVFrame *frame, int align) {
  ++allocations;
  if (fail_allocate)
    return AVERROR(ENOMEM);
  return av_frame_get_buffer(frame, align);
}
int make_writable(AVFrame *frame) {
  ++writable_calls;
  return av_frame_make_writable(frame);
}
int send_frame(AVCodecContext *, const AVFrame *frame) {
  if (fail_send)
    return AVERROR(EAGAIN);
  retained.push_back(av_frame_clone(frame));
  assert(retained.back());
  packet_pending = true;
  return 0;
}
int receive_packet(AVCodecContext *, AVPacket *packet) {
  if (!packet_pending)
    return AVERROR(EAGAIN);
  packet_pending = false;
  assert(av_new_packet(packet, 1) == 0);
  packet->data[0] = 42;
  return 0;
}
int hardware_buffer(AVBufferRef *, AVFrame *frame, int) {
  ++hardware_allocations;
  frame->width = width;
  frame->height = height;
  frame->format = format;
  return av_frame_get_buffer(frame, 0);
}
int upload(AVFrame *dst, const AVFrame *src, int) {
  ++uploads;
  assert(av_frame_is_writable(dst));
  return av_frame_copy(dst, src);
}
void output(const uint8_t *data, int len, int64_t, int, const void *) {
  assert(len == 1 && data[0] == 42);
}
void release_retained() {
  for (auto *frame : retained)
    av_frame_free(&frame);
  retained.clear();
}
void check_pixels(const AVFrame *frame, uint8_t expected) {
  int row_bytes[4] = {};
  assert(av_image_fill_linesizes(row_bytes, format, width) == 0);
  const int planes = format == AV_PIX_FMT_NV12 ? 2 : 3;
  for (int p = 0; p < planes; ++p)
    for (int y = 0; y < (p ? height / 2 : height); ++y)
      for (int x = 0; x < row_bytes[p]; ++x)
        assert(frame->data[p][y * frame->linesize[p] + x] == expected);
}
} // namespace

#define av_frame_get_buffer get_buffer
#define av_frame_make_writable make_writable
#define avcodec_send_frame send_frame
#define avcodec_receive_packet receive_packet
#define av_hwframe_get_buffer hardware_buffer
#define av_hwframe_transfer_data upload
#include "../../cpp/ffmpeg_ram/ffmpeg_ram_encode.cpp"
#undef av_frame_get_buffer
#undef av_frame_make_writable
#undef avcodec_send_frame
#undef avcodec_receive_packet
#undef av_hwframe_get_buffer
#undef av_hwframe_transfer_data

extern "C" void hwcodec_log(int, const char *) {}
extern "C" void hwcodec_av_log_callback(int, const char *) {}

int main() {
  int cases = 0;
  for (auto pixfmt : {AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P}) {
    format = pixfmt;
    for (int align : {0, 1, 256}) {
      for (bool gpu : {false, true}) {
        allocations = writable_calls = uploads = hardware_allocations = 0;
        fail_allocate = fail_send = packet_pending = false;
        FFmpegRamEncoder codec("test", nullptr, width, height, format, align,
                               30, 60, RC_CBR, Quality_Default, 1000, -1, 1,
                               -1, output);
        codec.c_ = avcodec_alloc_context3(nullptr);
        codec.pkt_ = av_packet_alloc();
        codec.frame_ = av_frame_alloc();
        codec.frame_->format = format;
        codec.frame_->width = width;
        codec.frame_->height = height;
        assert(av_frame_get_buffer(codec.frame_, align) == 0);
        if (gpu) {
          codec.hw_device_type_ = AV_HWDEVICE_TYPE_VAAPI;
          codec.hw_frame_ = av_frame_alloc();
          assert(hardware_buffer(nullptr, codec.hw_frame_, 0) == 0);
        }
        uint8_t *data[8] = {};
        int stride[8] = {};
        auto prepare = [&](uint8_t value) {
          assert(codec.prepare_frame(data, stride) == 0);
          const int planes = format == AV_PIX_FMT_NV12 ? 2 : 3;
          for (int p = 0; p < planes; ++p) {
            assert(data[p] == codec.frame_->data[p]);
            assert(stride[p] == codec.frame_->linesize[p]);
            memset(data[p], value, stride[p] * (p ? height / 2 : height));
          }
        };
        assert(codec.reencode(nullptr, 0) < 0);
        assert(codec.encode_prepared(nullptr, 0) < 0);
        assert(codec.prepare_frame(data, stride) == 0);
        for (auto *buffer : codec.frame_->buf)
          if (buffer)
            for (size_t i = 0; i < buffer->size; ++i)
              assert(buffer->data[i] == 0);
        prepare(85);
        auto *first_data = data[0];
        assert(codec.reencode(nullptr, 0) < 0);
        assert(codec.encode_prepared(nullptr, 1) == 0);
        assert(codec.encode_prepared(nullptr, 1) < 0);
        assert(gpu || retained.back()->data[0] == first_data);
        check_pixels(retained.back(), 85);
        auto *first_encoded = retained.back()->data[0];
        assert(codec.reencode(nullptr, 2) == 0);
        assert(retained.back()->data[0] == first_encoded);
        assert(retained.back()->pts == 2 && retained.front()->pts == 1);
        assert(uploads == (gpu ? 1 : 0));

        // Retain the software frame too: force replacement on both input paths.
        AVFrame *held_input = av_frame_clone(codec.frame_);
        fail_allocate = true;
        assert(codec.prepare_frame(data, stride) == AVERROR(ENOMEM));
        assert(codec.reencode(nullptr, 3) < 0);
        check_pixels(held_input, 85);
        fail_allocate = false;
        prepare(170);
        assert(data[0] != first_data);
        check_pixels(held_input, 85);
        assert(codec.encode_prepared(nullptr, 3) == 0);
        check_pixels(retained.back(), 170);
        check_pixels(retained.front(), 85);
        assert(writable_calls == 0);
        assert(uploads == (gpu ? 2 : 0));
        assert(!gpu || hardware_allocations == 2);
        av_frame_free(&held_input);
        release_retained();

        auto *reusable = codec.frame_->data[0];
        int previous_allocations = allocations;
        prepare(42);
        assert(data[0] == reusable && allocations == previous_allocations);
        // Dropping an unsubmitted view must not expose a partial image to repeat.
        assert(codec.reencode(nullptr, 4) < 0);
        int legacy_length = 0;
        assert(calculate_offset_length(format, height, codec.frame_->linesize,
                                       codec.offset_, &legacy_length) == 0);
        std::vector<uint8_t> legacy(legacy_length, 23);
        assert(codec.encode(legacy.data(), legacy_length, nullptr, 4) == 0);
        assert(codec.reencode(nullptr, 4) < 0);
        release_retained();
        prepare(44);
        assert(data[0] != legacy.data());
        legacy.clear();
        legacy.shrink_to_fit();
        assert(codec.encode_prepared(nullptr, 4) == 0);
        check_pixels(retained.back(), 44);
        release_retained();
        prepare(43);
        fail_send = true;
        assert(codec.encode_prepared(nullptr, 4) == AVERROR(EAGAIN));
        fail_send = false;
        assert(codec.reencode(nullptr, 4) == 0);
        check_pixels(retained.back(), 43);
        codec.free_encoder();
        check_pixels(retained.back(), 43);
        release_retained();
        ++cases;
      }
    }
  }
  assert(ffmpeg_ram_prepare_frame(nullptr, nullptr, nullptr) < 0);
  assert(ffmpeg_ram_encode_prepared(nullptr, nullptr, 0) < 0);
  assert(ffmpeg_ram_reencode(nullptr, nullptr, 0) < 0);
  std::printf("PASS %d ownership/reuse/repeat/failure cases\n", cases);
}
