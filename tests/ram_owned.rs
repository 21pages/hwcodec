use hwcodec::{
    common::{Quality, RateControl},
    ffmpeg::{AVHWDeviceType, AVPixelFormat},
    ffmpeg_ram::{
        decode::{DecodeContext, Decoder},
        encode::{EncodeContext, Encoder},
    },
};

#[test]
#[ignore = "requires Intel QSV and NVIDIA NVENC hardware"]
fn owned_frames_encode_and_repeat_on_hardware() {
    for name in ["h264_qsv", "hevc_qsv", "h264_nvenc", "hevc_nvenc"] {
        for (width, height) in [(1920, 1080), (3840, 2160)] {
            for align in [0, 256] {
                let mut encoder = Encoder::new(EncodeContext {
                    name: name.into(),
                    mc_name: None,
                    width,
                    height,
                    pixfmt: AVPixelFormat::AV_PIX_FMT_NV12,
                    align,
                    fps: 30,
                    gop: 60,
                    rc: RateControl::RC_CBR,
                    quality: Quality::Quality_Default,
                    kbs: if width == 1920 { 5000 } else { 20000 },
                    q: -1,
                    thread_count: 1,
                })
                .expect(name);
                let mut decoder = Decoder::new(DecodeContext {
                    name: if name.starts_with("h264") {
                        "h264"
                    } else {
                        "hevc"
                    }
                    .into(),
                    device_type: AVHWDeviceType::AV_HWDEVICE_TYPE_NONE,
                    thread_count: 1,
                })
                .expect("software decoder");
                assert!(encoder.reencode(0).is_err());
                // An abandoned writable view cannot be repeated as a complete image.
                drop(encoder.prepare_frame().unwrap());
                assert!(encoder.reencode(0).is_err());
                let mut decoded = 0;
                let mut marker = 0;
                for i in 0..38 {
                    let ms = i * 33;
                    let packets = if i < 30 {
                        marker = 40 + i as u8 * 3;
                        let mut input = encoder.prepare_frame().unwrap();
                        assert_eq!(
                            (input.width(), input.height()),
                            (width as usize, height as usize)
                        );
                        let [y, uv, unused] = input.planes_mut();
                        y.fill(marker);
                        uv.fill(128);
                        assert!(unused.is_empty());
                        std::mem::take(input.encode(ms).expect("encode owned frame"))
                    } else {
                        std::mem::take(encoder.reencode(ms).expect("repeat owned frame"))
                    };
                    assert_eq!(packets.len(), 1, "{name}: one packet per submission");
                    for packet in packets {
                        assert_eq!(packet.pts, ms);
                        for frame in decoder.decode(&packet.data).expect("decode packet") {
                            assert_eq!((frame.width, frame.height), (width, height));
                            for (x, y) in [(0, 0), (width / 2, height / 2), (width - 1, height - 1)]
                            {
                                let value = frame.data[0][(y * frame.linesize[0] + x) as usize];
                                assert!(value.abs_diff(marker) <= 4, "{name}: {value} != {marker}");
                            }
                            decoded += 1;
                        }
                    }
                }
                assert_eq!(decoded, 38);
                println!("PASS {name} {width}x{height} align={align}: {decoded} decoded frames");
            }
        }
    }
}
