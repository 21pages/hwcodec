use log::{error, trace};

use crate::{av_log_get_level, free_scaler, new_scaler, scale, AVPixelFormat, AV_LOG_ERROR};
use std::{ffi::c_void, os::raw::c_int};

#[derive(Debug, Clone, PartialEq)]
pub struct ScaleContext {
    pub srcW: c_int,
    pub srcH: c_int,
    pub srcFormat: AVPixelFormat,
    pub srcFullRange: bool,
    pub srcSpace: c_int,
    pub dstW: c_int,
    pub dstH: c_int,
    pub dstFormat: AVPixelFormat,
    pub dstFullRange: bool,
    pub dstSpace: c_int,
}

pub struct Scaler {
    inner: Box<c_void>,
    pub ctx: ScaleContext,
}

unsafe impl Send for Scaler {}
unsafe impl Sync for Scaler {}

impl Scaler {
    pub fn new(ctx: ScaleContext) -> Result<Self, ()> {
        unsafe {
            let inner = new_scaler(
                ctx.srcW,
                ctx.srcH,
                ctx.srcFormat,
                ctx.srcFullRange,
                ctx.srcSpace,
                ctx.dstW,
                ctx.dstH,
                ctx.dstFormat,
                ctx.dstFullRange,
                ctx.dstSpace,
            );
            if inner.is_null() {
                return Err(());
            }

            Ok(Scaler {
                inner: Box::from_raw(inner as *mut c_void),
                ctx,
            })
        }
    }

    pub fn scale(
        &self,
        srcSlice: *const *const u8,
        srcStride: *const c_int,
        srcSliceY: c_int,
        srcSliceH: c_int,
        dst: *const *mut u8,
        dstStride: *const c_int,
    ) -> Result<(), i32> {
        unsafe {
            let result = scale(
                &*self.inner,
                srcSlice,
                srcStride,
                srcSliceY,
                srcSliceH,
                dst,
                dstStride,
            );
            if result < 0 {
                if av_log_get_level() >= AV_LOG_ERROR as _ {
                    error!("Error scale: {}", result);
                }
                return Err(result);
            }
            Ok(())
        }
    }
}

impl Drop for Scaler {
    fn drop(&mut self) {
        unsafe {
            free_scaler(self.inner.as_mut());
            trace!("Scaler dropped");
        }
    }
}
