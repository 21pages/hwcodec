#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

#define MAX_GOP 0x7FFFFFFF // i32 max

#define TEST_TIMEOUT_MS 1000
#define ENCODE_TIMEOUT_MS 1000
#define DECODE_TIMEOUT_MS 1000

enum AdapterVendor {
  ADAPTER_VENDOR_AMD = 0x1002,
  ADAPTER_VENDOR_INTEL = 0x8086,
  ADAPTER_VENDOR_NVIDIA = 0x10DE,
  ADAPTER_VENDOR_UNKNOWN = 0,
};

enum SurfaceFormat {
  SURFACE_FORMAT_BGRA,
  SURFACE_FORMAT_RGBA,
  SURFACE_FORMAT_NV12,
};

enum DataFormat {
  H264,
  H265,
  VP8,
  VP9,
  AV1,
};

// same as Driver
enum Vendor {
  VENDOR_NV = 0,
  VENDOR_AMD = 1,
  VENDOR_INTEL = 2,
  VENDOR_FFMPEG = 3
};

enum Quality { Quality_Default, Quality_High, Quality_Medium, Quality_Low };

// Rate control modes for video encoding
//
// How RustDesk uses this library (https://github.com/21pages/rustdesk):
// - RustDesk primarily uses RC_CBR for most platforms (libs/scrap/src/common/hwcodec.rs L248, L694)
// - Exception: Android MediaCodec uses RC_VBR (hwcodec.rs L246)
// - Rate control is set via EncodeContext.rc parameter passed to hwcodec encoder
// - Dynamic bitrate changes are handled via set_bitrate() method based on quality ratio
//
// Comparison with Sunshine (https://github.com/LizardByte/Sunshine):
// - Sunshine uses encoder-specific rate control constants directly in encoder configurations
//   (e.g., NV_ENC_PARAMS_RC_CBR for NVENC, VA_RC_CBR/VA_RC_VBR for VAAPI)
// - This implementation uses a unified enum that is mapped to encoder-specific values in set_rate_control()
// - Both RustDesk and Sunshine primarily use CBR for low-latency streaming
// - Sunshine additionally supports CQP (Constant QP) mode for VAAPI when other RC modes are unavailable
enum RateControl {
  RC_DEFAULT,  // Default rate control (encoder-specific)
  RC_CBR,      // Constant Bitrate - RustDesk's primary mode, same as Sunshine's approach
  RC_VBR,      // Variable Bitrate - used by RustDesk for Android MediaCodec
  RC_CQ,       // Constant Quality - similar to Sunshine's CQP mode
};

enum HwcodecErrno {
  HWCODEC_SUCCESS = 0,
  HWCODEC_ERR_COMMON = -1,
  HWCODEC_ERR_HEVC_COULD_NOT_FIND_POC = -2,
};

#endif // COMMON_H