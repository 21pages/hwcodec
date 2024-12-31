extern "C" {
#include <libavutil/opt.h>
}

#include "util.h"
#include <limits>
#include <map>
#include <string.h>
#include <vector>

#include "common.h"

#include "common.h"

#define LOG_MODULE "UTIL"
#include "log.h"

namespace util_encode {

void set_av_codec_ctx(AVCodecContext *c, const std::string &name, int gop, int fps) {
  c->has_b_frames = 0;
  c->max_b_frames = 0;
  if (gop > 0 && gop < std::numeric_limits<int16_t>::max()) {
    c->gop_size = gop;
  } else if (name.find("vaapi") != std::string::npos) {
    c->gop_size = std::numeric_limits<int16_t>::max();
  } else if (name.find("qsv") != std::string::npos) {
    c->gop_size = std::numeric_limits<uint16_t>::max();
  } else if (name.find("nvenc") != std::string::npos)  {
    c->gop_size = 0xffffffff; // NVENC_INFINITE_GOPLENGTH
  } else {
    c->gop_size = std::numeric_limits<int>::max();
  }
  c->keyint_min = std::numeric_limits<int>::max();
  /* frames per second */
  c->time_base = av_make_q(1, 1000);
  c->framerate = av_make_q(fps, 1);
  c->flags |= AV_CODEC_FLAG2_LOCAL_HEADER;
  c->flags |= AV_CODEC_FLAG_LOW_DELAY;
  c->slices = 1;
  c->thread_type = FF_THREAD_SLICE;
  c->thread_count = c->slices;

  // https://github.com/obsproject/obs-studio/blob/3cc7dc0e7cf8b01081dc23e432115f7efd0c8877/plugins/obs-ffmpeg/obs-ffmpeg-mux.c#L160
  c->color_range = AVCOL_RANGE_MPEG;
  c->colorspace = AVCOL_SPC_SMPTE170M;
  c->color_primaries = AVCOL_PRI_SMPTE170M;
  c->color_trc = AVCOL_TRC_SMPTE170M;

  if (name.find("h264") != std::string::npos) {
    c->profile = FF_PROFILE_H264_HIGH;
  } else if (name.find("hevc") != std::string::npos) {
    c->profile = FF_PROFILE_HEVC_MAIN;
  }
}

bool set_lantency_free(void *priv_data, const std::string &name) {
  int ret;

  if (name.find("nvenc") != std::string::npos) {
    if ((ret = av_opt_set(priv_data, "delay", "0", 0)) < 0) {
      LOG_ERROR("nvenc set_lantency_free failed, ret = " + av_err2str(ret));
      return false;
    }
  }
  if (name.find("amf") != std::string::npos) {
    if ((ret = av_opt_set(priv_data, "query_timeout", "1000", 0)) < 0) {
      LOG_ERROR("amf set_lantency_free failed, ret = " + av_err2str(ret));
      return false;
    }
  }
  if (name.find("qsv") != std::string::npos) {
    if ((ret = av_opt_set(priv_data, "async_depth", "1", 0)) < 0) {
      LOG_ERROR("qsv set_lantency_free failed, ret = " + av_err2str(ret));
      return false;
    }
  }
  if (name.find("vaapi") != std::string::npos) {
    if ((ret = av_opt_set(priv_data, "async_depth", "1", 0)) < 0) {
      LOG_ERROR("vaapi set_lantency_free failed, ret = " + av_err2str(ret));
      return false;
    }
  }
  if (name.find("videotoolbox") != std::string::npos) {
    if ((ret = av_opt_set_int(priv_data, "realtime", 1, 0)) < 0) {
      LOG_ERROR("videotoolbox set realtime failed, ret = " + av_err2str(ret));
      return false;
    }
    if ((ret = av_opt_set_int(priv_data, "prio_speed", 1, 0)) < 0) {
      LOG_ERROR("videotoolbox set prio_speed failed, ret = " + av_err2str(ret));
      return false;
    }
  }
  return true;
}

bool set_rate_control(AVCodecContext *c, const std::string &name, int kbs) {
  change_bit_rate(c, name, kbs);

  if (name.find("qsv") != std::string::npos) {
    // https://github.com/LizardByte/Sunshine/blob/3e47cd3cc8fd37a7a88be82444ff4f3c0022856b/src/video.cpp#L1635
    c->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL;
  } else if (name.find("nvenc") != std::string::npos) {
    int ret = av_opt_set(c->priv_data, "rc", "vbr", 0);
    if (ret < 0) {
      LOG_ERROR("nvenc set opt rc failed, ret = " + av_err2str(ret));
      return false;
    }
    if (name.find("h264") != std::string::npos) {
      c->qmin = 31;
      c->qmax = 38;
    } else if (name.find("hevc") != std::string::npos) {
      c->qmin = 24;
      c->qmax = 38;
    }
  } else if (name.find("amf") != std::string::npos) {
    int ret = av_opt_set(c->priv_data, "rc", "cbr", 0);
    if (ret < 0) {
      LOG_ERROR("amf set opt rc failed, ret = " + av_err2str(ret));
      return false;
    }
  } else if (name.find("mediacodec") != std::string::npos) {
    int ret = av_opt_set(c->priv_data, "bitrate_mode", "vbr", 0);
    if (ret < 0) {
      LOG_ERROR("mediacodec set opt bitrate_mode failed, ret = " + av_err2str(ret));
      return false;
     }
   }
}

bool set_gpu(void *priv_data, const std::string &name, int gpu) {
  int ret;
  if (gpu < 0)
    return -1;
  if (name.find("nvenc") != std::string::npos) {
    if ((ret = av_opt_set_int(priv_data, "gpu", gpu, 0)) < 0) {
      LOG_ERROR("nvenc set gpu failed, ret = " + av_err2str(ret));
      return false;
    }
  }
  return true;
}

bool force_hw(void *priv_data, const std::string &name) {
  int ret;
  if (name.find("_mf") != std::string::npos) {
    if ((ret = av_opt_set_int(priv_data, "hw_encoding", 1, 0)) < 0) {
      LOG_ERROR("mediafoundation set hw_encoding failed, ret = " +
                av_err2str(ret));
      return false;
    }
  }
  if (name.find("videotoolbox") != std::string::npos) {
    if ((ret = av_opt_set_int(priv_data, "allow_sw", 0, 0)) < 0) {
      LOG_ERROR("mediafoundation set allow_sw failed, ret = " +
                av_err2str(ret));
      return false;
    }
  }
  return true;
}

bool set_others(void *priv_data, const std::string &name) {
  int ret;
  if (name.find("_mf") != std::string::npos) {
    // ff_eAVScenarioInfo_DisplayRemoting = 1
    if ((ret = av_opt_set_int(priv_data, "scenario", 1, 0)) < 0) {
      LOG_ERROR("mediafoundation set scenario failed, ret = " +
                av_err2str(ret));
      return false;
    }
  }
  if (name.find("vaapi") != std::string::npos) {
    if ((ret = av_opt_set_int(priv_data, "idr_interval",
                              std::numeric_limits<int>::max(), 0)) < 0) {
      LOG_ERROR("vaapi set idr_interval failed, ret = " + av_err2str(ret));
      return false;
    }
  }
  return true;
}

bool change_bit_rate(AVCodecContext *c, const std::string &name, int kbs) {
  /* put sample parameters */
  // https://github.com/FFmpeg/FFmpeg/blob/415f012359364a77e8394436f222b74a8641a3ee/libavcodec/encode.c#L581
  if (kbs <= 0) {
    kbs = 1000;
  }
  c->bit_rate = kbs * 1000;
  if (name.find("qsv") != std::string::npos) {
    c->rc_max_rate = c->bit_rate;
    c->bit_rate--; // cbr with vbr
  }
  if (name.find("nvenc") != std::string::npos) {
    c->rc_max_rate = c->bit_rate;
    c->rc_buffer_size = c->bit_rate;
  }
  return true;
}

void vram_encode_test_callback(const uint8_t *data, int32_t len, int32_t key, const void *obj, int64_t pts) {
  (void)data;
  (void)len;
  (void)pts;
  if (obj) {
    int32_t *pkey = (int32_t *)obj;
    *pkey = key;
  }
}

} // namespace util_encode

namespace util_decode {

static bool g_flag_could_not_find_ref_with_poc = false;

bool has_flag_could_not_find_ref_with_poc() {
  bool v = g_flag_could_not_find_ref_with_poc;
  g_flag_could_not_find_ref_with_poc = false;
  return v;
}

} // namespace util_decode

extern "C" void hwcodec_set_flag_could_not_find_ref_with_poc() {
  util_decode::g_flag_could_not_find_ref_with_poc = true;
}