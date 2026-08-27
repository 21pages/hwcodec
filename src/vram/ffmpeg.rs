#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(unused)]
include!(concat!(env!("OUT_DIR"), "/ffmpeg_vram_ffi.rs"));

use crate::{
    common::DataFormat::*,
    common::Driver::{AMF, MFX, NV},
    vram::{
        inner::{DecodeCalls, EncodeCalls, InnerDecodeContext, InnerEncodeContext},
        DecodeContext, DynamicContext, EncodeContext, FeatureContext, MAX_ADATERS,
    },
};

#[cfg(windows)]
use crate::common::Driver::FFMPEG;

pub(crate) unsafe fn new_encoder(ctx: &EncodeContext) -> *mut std::ffi::c_void {
    ffmpeg_vram_new_encoder_ex(
        ctx.d.device.unwrap_or(std::ptr::null_mut()),
        ctx.f.luid,
        ctx.f.data_format as i32,
        ctx.d.width,
        ctx.d.height,
        ctx.d.kbitrate,
        ctx.d.framerate,
        ctx.d.gop,
        ctx.f.bit_depth as i32,
        ctx.d.input_hdr as i32,
    )
}

#[cfg(windows)]
pub(crate) unsafe fn new_main10_decoder(ctx: &DecodeContext) -> *mut std::ffi::c_void {
    ffmpeg_vram_new_decoder_ex(
        ctx.device.unwrap_or(std::ptr::null_mut()),
        ctx.luid,
        ctx.data_format as i32,
        10,
    )
}

#[cfg(windows)]
pub(crate) fn available_main10_decoders() -> Vec<DecodeContext> {
    let mut luids = vec![0; MAX_ADATERS];
    let mut vendors = vec![0; MAX_ADATERS];
    let mut count = 0;
    let data = crate::common::DATA_H265_MAIN10_720P;
    let result = unsafe {
        ffmpeg_vram_test_decode_ex(
            luids.as_mut_ptr(),
            vendors.as_mut_ptr(),
            MAX_ADATERS as i32,
            &mut count,
            H265 as i32,
            10,
            data.as_ptr() as *mut u8,
            data.len() as i32,
            std::ptr::null(),
            std::ptr::null(),
            0,
        )
    };
    if result != 0 || count < 0 || count as usize > MAX_ADATERS {
        return Vec::new();
    }

    (0..count as usize)
        .filter_map(|index| {
            let vendor = match vendors[index] {
                0 => NV,
                1 => AMF,
                2 => MFX,
                _ => return None,
            };
            Some(DecodeContext {
                device: None,
                driver: FFMPEG,
                vendor,
                luid: luids[index],
                data_format: H265,
                bit_depth: 10,
            })
        })
        .collect()
}

#[cfg(windows)]
pub(crate) fn available_main10(mut d: DynamicContext) -> Vec<FeatureContext> {
    d.input_hdr = true;
    let mut luids = vec![0; MAX_ADATERS];
    let mut vendors = vec![0; MAX_ADATERS];
    let mut count = 0;
    let result = unsafe {
        ffmpeg_vram_test_encode_ex(
            luids.as_mut_ptr(),
            vendors.as_mut_ptr(),
            MAX_ADATERS as i32,
            &mut count,
            H265 as i32,
            d.width,
            d.height,
            d.kbitrate,
            d.framerate,
            d.gop,
            10,
            1,
            std::ptr::null(),
            std::ptr::null(),
            0,
        )
    };
    if result != 0 || count < 0 || count as usize > MAX_ADATERS {
        return Vec::new();
    }

    (0..count as usize)
        .filter_map(|index| {
            let vendor = match vendors[index] {
                0 => NV,
                1 => AMF,
                2 => MFX,
                _ => return None,
            };
            Some(FeatureContext {
                driver: FFMPEG,
                vendor,
                luid: luids[index],
                data_format: H265,
                bit_depth: 10,
            })
        })
        .collect()
}

pub fn encode_calls() -> EncodeCalls {
    EncodeCalls {
        new: ffmpeg_vram_new_encoder,
        encode: ffmpeg_vram_encode,
        destroy: ffmpeg_vram_destroy_encoder,
        test: ffmpeg_vram_test_encode,
        set_bitrate: ffmpeg_vram_set_bitrate,
        set_framerate: ffmpeg_vram_set_framerate,
    }
}

pub fn decode_calls() -> DecodeCalls {
    DecodeCalls {
        new: ffmpeg_vram_new_decoder,
        decode: ffmpeg_vram_decode,
        destroy: ffmpeg_vram_destroy_decoder,
        test: ffmpeg_vram_test_decode,
    }
}

pub fn possible_support_encoders() -> Vec<InnerEncodeContext> {
    let dataFormats = vec![H264, H265];
    let mut v = vec![];
    for dataFormat in dataFormats.iter() {
        v.push(InnerEncodeContext {
            format: dataFormat.clone(),
        });
    }
    v
}

pub fn possible_support_decoders() -> Vec<InnerDecodeContext> {
    let codecs = vec![H264, H265];
    let mut v = vec![];
    for codec in codecs.iter() {
        v.push(InnerDecodeContext {
            data_format: codec.clone(),
        });
    }
    v
}
