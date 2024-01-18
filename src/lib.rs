pub mod common;
pub mod ffmpeg;
pub mod ffram;
pub mod mux;
#[cfg(feature = "sdk")]
pub mod native;

#[no_mangle]
pub extern "C" fn hwcodec_log(level: i32, message: *const std::os::raw::c_char) {
    unsafe {
        let c_str = std::ffi::CStr::from_ptr(message);
        if let Ok(str_slice) = c_str.to_str() {
            let string = String::from(str_slice);
            match level {
                0 => log::error!("{}", string),
                1 => log::warn!("{}", string),
                2 => log::info!("{}", string),
                3 => log::debug!("{}", string),
                4 => log::trace!("{}", string),
                _ => {}
            }
        }
    }
}
