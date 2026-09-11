#ifndef HWCODEC_VAAPI_ENCODE_H
#define HWCODEC_VAAPI_ENCODE_H

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext_vaapi.h>
#include <libavutil/opt.h>
}
#include <vector>

#include "common.h"
#include "log.h"

namespace vaapi_encode {

struct RateControlMode {
  const char *name;
  unsigned int flag;
  bool bitrate;
  bool quality;
};

inline const RateControlMode *choose_rate_control(unsigned int supported,
                                                 int rc, bool has_bitrate) {
  static const RateControlMode modes[] = {
      {"CBR", VA_RC_CBR, true, false},
      {"VBR", VA_RC_VBR, true, false},
#if VA_CHECK_VERSION(1, 3, 0)
      {"AVBR", VA_RC_AVBR, true, false},
      {"QVBR", VA_RC_QVBR, true, true},
#endif
#if VA_CHECK_VERSION(1, 1, 0)
      {"ICQ", VA_RC_ICQ, false, true},
#endif
      {"CQP", VA_RC_CQP, false, true},
  };
  if (has_bitrate && rc != RC_CQ) {
    const auto &preferred = modes[rc == RC_CBR ? 0 : 1];
    if (supported & preferred.flag)
      return &preferred;
  }
  // Honor a quality preference first, but keep a bitrate mode available when
  // the driver has no quality mode and the caller supplied a bitrate.
  const bool prefer_bitrate = has_bitrate && rc != RC_CQ;
  for (int pass = 0; pass < 2; ++pass) {
    const bool want_bitrate = pass == 0 ? prefer_bitrate : !prefer_bitrate;
    for (const auto &mode : modes) {
      if (mode.bitrate == want_bitrate && (!mode.bitrate || has_bitrate) &&
          (supported & mode.flag))
        return &mode;
    }
  }
  return nullptr;
}

inline bool set_rate_control(AVCodecContext *c, AVBufferRef *device_ref,
                             int rc, int q) {
  // These are the profiles set by util_encode::set_av_codec_ctx for the RAM
  // encoders. Leave other profiles/codecs to FFmpeg's own selection.
  VAProfile profile;
  if (c->codec_id == AV_CODEC_ID_H264 && c->profile == FF_PROFILE_H264_HIGH)
    profile = VAProfileH264High;
  else if (c->codec_id == AV_CODEC_ID_HEVC && c->profile == FF_PROFILE_HEVC_MAIN)
    profile = VAProfileHEVCMain;
  else
    return true;

  if (!device_ref || !device_ref->data) {
    LOG_ERROR(std::string("VAAPI device context unavailable"));
    return false;
  }
  auto *device = reinterpret_cast<AVHWDeviceContext *>(device_ref->data);
  if (device->type != AV_HWDEVICE_TYPE_VAAPI || !device->hwctx) {
    LOG_ERROR(std::string("Invalid VAAPI device context"));
    return false;
  }
  auto *va = static_cast<AVVAAPIDeviceContext *>(device->hwctx);
  // RustDesk's 0006-dlopen-libva.patch exposes the already loaded functions.
  auto *f = va->funcs;
  if (!f || !f->vaMaxNumEntrypoints || !f->vaQueryConfigEntrypoints ||
      !f->vaGetConfigAttributes) {
    LOG_ERROR(std::string("VAAPI rate control query functions unavailable"));
    return false;
  }

  int64_t low_power = 0;
  int ret = av_opt_get_int(c->priv_data, "low_power", 0, &low_power);
  if (ret < 0) {
    LOG_ERROR(std::string("VAAPI get low_power failed, ret = ") + av_err2str(ret));
    return false;
  }
  int count = f->vaMaxNumEntrypoints(va->display);
  if (count <= 0) {
    LOG_ERROR(std::string("VAAPI invalid entrypoint count: ") + std::to_string(count));
    return false;
  }
  std::vector<VAEntrypoint> entrypoints(count);
  VAStatus status = f->vaQueryConfigEntrypoints(va->display, profile,
                                               entrypoints.data(), &count);
  if (status != VA_STATUS_SUCCESS || count < 0 ||
      static_cast<size_t>(count) > entrypoints.size()) {
    LOG_ERROR(std::string("VAAPI query entrypoints failed, status = ") +
              std::to_string(status));
    return false;
  }

  // Match FFmpeg's driver-order selection, including LP in the default list.
  // RC support belongs to a profile/entrypoint pair, not to the GPU as a whole.
  // https://github.com/FFmpeg/FFmpeg/blob/n7.1/libavcodec/vaapi_encode.c#L844-L883
  VAEntrypoint entrypoint = static_cast<VAEntrypoint>(0);
  for (int i = 0; i < count; ++i) {
    bool usable = !low_power && (entrypoints[i] == VAEntrypointEncSlice ||
                                 entrypoints[i] == VAEntrypointEncPicture);
#if VA_CHECK_VERSION(0, 39, 2)
    usable = usable || entrypoints[i] == VAEntrypointEncSliceLP;
#endif
    if (usable) {
      entrypoint = entrypoints[i];
      break;
    }
  }
  if (!entrypoint) {
    LOG_ERROR(std::string("VAAPI no usable encoding entrypoint"));
    return false;
  }

  VAConfigAttrib attr = {};
  attr.type = VAConfigAttribRateControl;
  status = f->vaGetConfigAttributes(va->display, profile, entrypoint, &attr, 1);
  if (status != VA_STATUS_SUCCESS) {
    LOG_ERROR(std::string("VAAPI query rate control failed, status = ") +
              std::to_string(status));
    return false;
  }
  // FFmpeg assumes CQP when the driver does not report this attribute.
  // https://github.com/FFmpeg/FFmpeg/blob/n7.1/libavcodec/vaapi_encode.c#L1088-L1097
  const unsigned int supported =
      attr.value == VA_ATTRIB_NOT_SUPPORTED ? VA_RC_CQP : attr.value;
  const auto *mode = choose_rate_control(supported, rc, c->bit_rate > 0);
  if (!mode) {
    LOG_ERROR(std::string("VAAPI no compatible rate control mode, supported = ") +
              std::to_string(supported));
    return false;
  }
  if ((ret = av_opt_set(c->priv_data, "rc_mode", mode->name, 0)) < 0) {
    LOG_ERROR(std::string("VAAPI set rc_mode failed, ret = ") + av_err2str(ret));
    return false;
  }
  if (!mode->bitrate) {
    // A positive bitrate otherwise excludes CQP during FFmpeg auto selection.
    // https://github.com/rustdesk/rustdesk/issues/16156
    c->bit_rate = 0;
    c->rc_min_rate = 0;
    c->rc_max_rate = 0;
    c->rc_buffer_size = 0;
    c->rc_initial_buffer_occupancy = 0;
  }
  if (mode->quality && q > 0) {
    if (q > 51) {
      LOG_ERROR(std::string("VAAPI quality must be in range 1..51"));
      return false;
    }
    c->global_quality = q;
  }
  LOG_INFO(std::string("VAAPI profile = ") + std::to_string(profile) +
           ", entrypoint = " + std::to_string(entrypoint) +
           ", supported RC = " + std::to_string(supported) +
           ", selected RC = " + mode->name);
  return true;
}

} // namespace vaapi_encode

#endif
