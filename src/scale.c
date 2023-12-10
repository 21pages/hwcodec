#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// https://github.com/obsproject/obs-studio/blob/542cb876dce6077bb24fa5ee0439e7d21ac53f59/libobs/media-io/video-scaler-ffmpeg.c#L148

struct Scaler {
  struct SwsContext *swsContext;
};

#define FIXED_1_0 (1 << 16)
void *new_scaler(int srcW, int srcH, enum AVPixelFormat srcFormat,
                 bool srcFullRange, int srcSpace, int dstW, int dstH,
                 enum AVPixelFormat dstFormat, bool dstFullRange,
                 int dstSpace) {
  struct Scaler *scaler = NULL;
  struct SwsContext *swsContext = NULL;

  swsContext = sws_alloc_context();
  if (!swsContext) {
    fprintf(stderr, "Failed to alloc SwsContext\n");
    goto _exit;
  }
  av_opt_set_int(swsContext, "sws_flags", SWS_FAST_BILINEAR, 0);
  av_opt_set_int(swsContext, "srcw", srcW, 0);
  av_opt_set_int(swsContext, "srch", srcH, 0);
  av_opt_set_int(swsContext, "dstw", dstW, 0);
  av_opt_set_int(swsContext, "dsth", dstH, 0);
  av_opt_set_int(swsContext, "src_format", srcFormat, 0);
  av_opt_set_int(swsContext, "dst_format", dstFormat, 0);
  av_opt_set_int(swsContext, "src_range", srcFullRange ? 1 : 0, 0);
  av_opt_set_int(swsContext, "dst_range", dstFullRange ? 1 : 0, 0);

  if (sws_init_context(swsContext, NULL, NULL) < 0) {
    fprintf(stderr, "Failed to init SwsContext\n");
    goto _exit;
  }

  if (sws_setColorspaceDetails(
          swsContext, sws_getCoefficients(srcSpace), srcFullRange ? 1 : 0,
          sws_getCoefficients(srcSpace), dstFullRange ? 1 : 0, 0, FIXED_1_0,
          FIXED_1_0) < 0) {
    fprintf(stderr, "Failed to set colorspace details\n");
    goto _exit;
  }

  scaler = (struct Scaler *)malloc(sizeof(struct Scaler));
  if (!scaler) {
    fprintf(stderr, "Failed to alloc scaler\n");
    goto _exit;
  }
  scaler->swsContext = swsContext;
  return (void *)scaler;

_exit:
  if (swsContext) {
    sws_freeContext(swsContext);
  }
}

int scale(const void *const scaler, const uint8_t *const srcSlice[],
          const int srcStride[], int srcSliceY, int srcSliceH,
          uint8_t *const dst[], const int dstStride[]) {
  struct SwsContext *swsContext = ((struct Scaler *)scaler)->swsContext;
  return sws_scale(swsContext, srcSlice, srcStride, srcSliceY, srcSliceH, dst,
                   dstStride);
}

void free_scaler(void *scaler) {
  if (!scaler) {
    return;
  }
  struct SwsContext *swsContext = ((struct Scaler *)scaler)->swsContext;
  if (swsContext) {
    sws_freeContext(swsContext);
  }
}