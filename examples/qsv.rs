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
    std::thread::sleep(std::time::Duration::from_secs(30));
}
