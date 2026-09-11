// No GPU required. Use the same RustDesk-patched FFmpeg headers as hwcodec.
// c++ -std=c++11 -Icpp/common -Icpp/ffmpeg_ram $(pkg-config --cflags libavcodec libavutil) \
//   tests/vaapi_rate_control.cpp $(pkg-config --libs libavutil) -o /tmp/vaapi-rc-test
#include "vaapi_encode.h"

#include <cassert>
#include <iostream>
#include <utility>

namespace gol {
void error(const std::string &) {}
void info(const std::string &) {}
} // namespace gol

namespace {
struct Options {
  const AVClass *av_class;
  int low_power;
  int rc_mode;
};

const AVOption options[] = {
    {"low_power", nullptr, offsetof(Options, low_power), AV_OPT_TYPE_BOOL,
     {.i64 = 0}, 0, 1},
    {"rc_mode", nullptr, offsetof(Options, rc_mode), AV_OPT_TYPE_INT,
     {.i64 = 0}, 0, 6, 0, "rc_mode"},
    {"CQP", nullptr, 0, AV_OPT_TYPE_CONST, {.i64 = 1}, 0, 0, 0, "rc_mode"},
    {"CBR", nullptr, 0, AV_OPT_TYPE_CONST, {.i64 = 2}, 0, 0, 0, "rc_mode"},
    {"VBR", nullptr, 0, AV_OPT_TYPE_CONST, {.i64 = 3}, 0, 0, 0, "rc_mode"},
    {"ICQ", nullptr, 0, AV_OPT_TYPE_CONST, {.i64 = 4}, 0, 0, 0, "rc_mode"},
    {"QVBR", nullptr, 0, AV_OPT_TYPE_CONST, {.i64 = 5}, 0, 0, 0, "rc_mode"},
    {"AVBR", nullptr, 0, AV_OPT_TYPE_CONST, {.i64 = 6}, 0, 0, 0, "rc_mode"},
    {nullptr},
};

const AVClass option_class = [] {
  AVClass c = {};
  c.class_name = "test-vaapi";
  c.item_name = av_default_item_name;
  c.option = options;
  c.version = LIBAVUTIL_VERSION_INT;
  return c;
}();

struct Driver {
  std::vector<std::pair<VAEntrypoint, unsigned int>> entries;
  VAProfile expected_profile = VAProfileH264High;
  VAEntrypoint queried_entrypoint = static_cast<VAEntrypoint>(0);
  VAStatus entrypoint_status = VA_STATUS_SUCCESS;
  VAStatus attribute_status = VA_STATUS_SUCCESS;
  int queries = 0;

  static int max_entrypoints(VADisplay) { return 8; }
  static VAStatus query_entrypoints(VADisplay display, VAProfile profile,
                                    VAEntrypoint *entries, int *count) {
    auto *d = static_cast<Driver *>(display);
    assert(profile == d->expected_profile);
    assert(d->entries.size() <= 8);
    *count = static_cast<int>(d->entries.size());
    for (int i = 0; i < *count; ++i)
      entries[i] = d->entries[i].first;
    return d->entrypoint_status;
  }
  static VAStatus query_attributes(VADisplay display, VAProfile profile,
                                    VAEntrypoint entrypoint, VAConfigAttrib *attr,
                                    int count) {
    auto *d = static_cast<Driver *>(display);
    assert(profile == d->expected_profile);
    assert(count == 1 && attr->type == VAConfigAttribRateControl);
    d->queried_entrypoint = entrypoint;
    ++d->queries;
    for (const auto &entry : d->entries) {
      if (entry.first == entrypoint) {
        attr->value = entry.second;
        return d->attribute_status;
      }
    }
    assert(false);
    return VA_STATUS_ERROR_OPERATION_FAILED;
  }
};

struct Encoder {
  Driver driver;
  Options opts = {};
  AVCodecContext c = {};
  VAAPIDynLoadFunctions funcs = {};
  AVVAAPIDeviceContext va = {};
  AVHWDeviceContext device = {};
  AVBufferRef device_ref = {};

  explicit Encoder(unsigned int supported) {
    driver.entries = {{VAEntrypointEncSlice, supported}};
    opts.av_class = &option_class;
    av_opt_set_defaults(&opts);
    c.priv_data = &opts;
    c.codec_id = AV_CODEC_ID_H264;
    c.profile = FF_PROFILE_H264_HIGH;
    c.bit_rate = 1000000;
    c.rc_min_rate = 500000;
    c.rc_max_rate = 2000000;
    c.rc_buffer_size = 4000000;
    c.rc_initial_buffer_occupancy = 3000000;
    funcs.vaMaxNumEntrypoints = Driver::max_entrypoints;
    funcs.vaQueryConfigEntrypoints = Driver::query_entrypoints;
    funcs.vaGetConfigAttributes = Driver::query_attributes;
    va.display = &driver;
    va.funcs = &funcs;
    device.type = AV_HWDEVICE_TYPE_VAAPI;
    device.hwctx = &va;
    device_ref.data = reinterpret_cast<uint8_t *>(&device);
  }
  bool configure(int rc = RC_CBR, int q = -1) {
    return vaapi_encode::set_rate_control(&c, &device_ref, rc, q);
  }
  void assert_no_bitrate() {
    assert(c.bit_rate == 0 && c.rc_min_rate == 0 && c.rc_max_rate == 0);
    assert(c.rc_buffer_size == 0 && c.rc_initial_buffer_occupancy == 0);
  }
  void assert_bitrate_preserved() {
    assert(c.bit_rate == 1000000 && c.rc_min_rate == 500000);
    assert(c.rc_max_rate == 2000000 && c.rc_buffer_size == 4000000);
    assert(c.rc_initial_buffer_occupancy == 3000000);
  }
};
} // namespace

int main() {
  {
    Encoder e(VA_RC_CQP | VA_RC_MB);
    e.driver.entries = {{VAEntrypointVLD, 0},
                        {VAEntrypointEncSliceLP, VA_RC_CQP | VA_RC_MB}};
    assert(e.configure() && e.opts.rc_mode == 1);
    assert(e.driver.queried_entrypoint == VAEntrypointEncSliceLP);
    assert(e.opts.low_power == 0 && e.c.global_quality == 0);
    e.assert_no_bitrate();
  }
  for (int rc : {RC_CBR, RC_VBR, RC_DEFAULT}) {
    Encoder e(VA_RC_CBR | VA_RC_VBR | VA_RC_CQP | VA_RC_ICQ);
    assert(e.configure(rc));
    assert(e.opts.rc_mode == (rc == RC_CBR ? 2 : 3));
    e.assert_bitrate_preserved();
  }
  {
    Encoder e(VA_RC_VBR);
    assert(e.configure(RC_CBR) && e.opts.rc_mode == 3);
    e.assert_bitrate_preserved();
  }
  {
    Encoder e(VA_RC_CBR);
    assert(e.configure(RC_VBR) && e.opts.rc_mode == 2);
    e.assert_bitrate_preserved();
  }
  {
    Encoder e(VA_RC_ICQ | VA_RC_CQP);
    assert(e.configure(RC_CBR, 28) && e.opts.rc_mode == 4);
    assert(e.c.global_quality == 28);
    e.assert_no_bitrate();
  }
  {
    Encoder e(VA_RC_CQP);
    assert(e.configure(RC_CBR, 22) && e.opts.rc_mode == 1);
    assert(e.c.global_quality == 22);
    e.assert_no_bitrate();
  }
  {
    Encoder e(VA_RC_CBR | VA_RC_CQP);
    assert(e.configure(RC_CQ) && e.opts.rc_mode == 1);
    e.assert_no_bitrate();
  }
  {
    Encoder e(VA_RC_CBR);
    assert(e.configure(RC_CQ) && e.opts.rc_mode == 2);
    e.assert_bitrate_preserved();
  }
  {
    Encoder e(VA_RC_CBR | VA_RC_CQP);
    e.c.bit_rate = 0;
    assert(e.configure() && e.opts.rc_mode == 1);
    e.assert_no_bitrate();
  }
  {
    Encoder e(VA_RC_CBR);
    e.c.bit_rate = 0;
    assert(!e.configure());
  }
  {
    Encoder e(VA_ATTRIB_NOT_SUPPORTED);
    assert(e.configure() && e.opts.rc_mode == 1);
    e.assert_no_bitrate();
  }
  {
    Encoder e(VA_RC_CBR);
    e.driver.entries.push_back({VAEntrypointEncSliceLP, VA_RC_CQP});
    assert(e.configure() && e.opts.rc_mode == 2);
    assert(e.driver.queried_entrypoint == VAEntrypointEncSlice);
    e.assert_bitrate_preserved();
    e.opts.low_power = 1;
    assert(e.configure() && e.opts.rc_mode == 1);
    assert(e.driver.queried_entrypoint == VAEntrypointEncSliceLP);
    e.assert_no_bitrate();
  }
  {
    Encoder e(VA_RC_CBR);
    e.driver.entries.insert(e.driver.entries.begin(), {VAEntrypointEncSliceLP, VA_RC_CQP});
    assert(e.configure() && e.opts.rc_mode == 1);
    assert(e.driver.queried_entrypoint == VAEntrypointEncSliceLP);
  }
  {
    Encoder e(VA_RC_CBR);
    e.c.codec_id = AV_CODEC_ID_HEVC;
    e.c.profile = FF_PROFILE_HEVC_MAIN;
    e.driver.expected_profile = VAProfileHEVCMain;
    assert(e.configure() && e.opts.rc_mode == 2);
  }
  {
    Encoder e(VA_RC_CBR);
    e.opts.low_power = 1;
    assert(!e.configure() && e.driver.queries == 0);
  }
  {
    Encoder e(VA_RC_CQP);
    e.driver.attribute_status = VA_STATUS_ERROR_OPERATION_FAILED;
    assert(!e.configure() && e.opts.rc_mode == 0);
    e.assert_bitrate_preserved();
  }
  {
    Encoder e(VA_RC_CQP);
    e.driver.entrypoint_status = VA_STATUS_ERROR_OPERATION_FAILED;
    assert(!e.configure() && e.driver.queries == 0);
  }
  for (unsigned int modes : {0u, static_cast<unsigned int>(VA_RC_MB)}) {
    Encoder e(modes);
    assert(!e.configure());
  }
  {
    Encoder e(VA_RC_CQP);
    assert(!e.configure(RC_CQ, 52));
  }
  {
    Encoder e(VA_RC_CBR);
    e.funcs.vaGetConfigAttributes = nullptr;
    assert(!e.configure() && e.driver.queries == 0);
  }
  {
    Encoder e(VA_RC_CBR);
    e.driver.entries.clear();
    assert(!e.configure() && e.driver.queries == 0);
  }
  {
    Encoder e(VA_RC_CBR);
    e.c.codec_id = AV_CODEC_ID_VP9;
    assert(e.configure() && e.driver.queries == 0);
    assert(e.opts.rc_mode == 0);
    e.assert_bitrate_preserved();
  }
  {
    Encoder e(VA_RC_AVBR);
    assert(e.configure() && e.opts.rc_mode == 6);
    e.assert_bitrate_preserved();
  }
  {
    Encoder e(VA_RC_QVBR);
    assert(e.configure(RC_CBR, 25) && e.opts.rc_mode == 5);
    assert(e.c.global_quality == 25);
    e.assert_bitrate_preserved();
  }
  std::cout << "VAAPI RC tests passed\n";
}
