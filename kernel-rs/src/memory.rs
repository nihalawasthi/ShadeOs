#[no_mangle]
pub extern "C" fn rust_memset(dest: *mut u8, val: i32, len: usize) -> *mut u8 {
    unsafe {
        for i in 0..len {
            *dest.add(i) = val as u8;
        }
        dest
    }
}

#[no_mangle]
pub extern "C" fn rust_memcpy(dest: *mut u8, src: *const u8, len: usize) -> *mut u8 {
    unsafe {
        for i in 0..len {
            *dest.add(i) = *src.add(i);
        }
        dest
    }
}
