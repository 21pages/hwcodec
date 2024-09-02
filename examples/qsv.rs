use env_logger::{init_from_env, Env, DEFAULT_FILTER_ENV};
use hwcodec::common::MAX_GOP;
use hwcodec::vram::{decode, encode, DynamicContext};
use hwcodec::{
    common::{get_gpu_signature, Quality::*, RateControl::*},
    ffmpeg::AVPixelFormat,
    ffmpeg_ram::{
        decode::Decoder,
        encode::{EncodeContext, Encoder},
    },
};

fn main() {
    init_from_env(Env::default().filter_or(DEFAULT_FILTER_ENV, "info"));
    vram();
}

fn ram() {
    let encode_ctx = EncodeContext {
        name: String::from("hevc_qsv"),
        mc_name: None,
        width: 1920,
        height: 1080,
        pixfmt: AVPixelFormat::AV_PIX_FMT_NV12,
        align: 0,
        kbs: 0,
        fps: 30,
        gop: 60,
        quality: Quality_Default,
        rc: RC_DEFAULT,
        thread_count: 4,
        q: -1,
    };
    let mut video_encoder = Encoder::new(encode_ctx).unwrap();
}

fn vram() {
    log::info!("start");
    let dynamic = DynamicContext {
        width: 1920,
        height: 1080,
        kbitrate: 5000,
        framerate: 30,
        gop: MAX_GOP as _,
        device: None,
    };
    hwcodec::vram::encode::test_qsv(dynamic);
    log::info!("end");
    // std::thread::sleep(std::time::Duration::from_secs(30));
}
