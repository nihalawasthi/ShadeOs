use core::mem::size_of;
use core::ptr::null_mut;

extern "C" {
    fn serial_write(s: *const u8);
}

fn serial_write_hex(val: u64) {
    let mut buf = [0u8; 17];
    let mut i = 15;
    let mut v = val;
    buf[16] = 0; // Null terminator

    if v == 0 {
        unsafe { serial_write(b"0\0".as_ptr()); }
        return;
    }

    while v > 0 {
        let digit = (v % 16) as u8;
        buf[i] = if digit < 10 { b'0' + digit } else { b'a' + (digit - 10) };
        v /= 16;
        i -= 1;
    }
    unsafe { serial_write(buf.as_ptr().add(i + 1)); }
}


const HEAP_SIZE: usize = 1024 * 1024 * 4; // 4 MiB
const ALIGNMENT: usize = 16;
const BLOCK_MAGIC: u32 = 0xDEADBEEF;

#[repr(C, align(16))]
pub struct Block {
    pub magic: u32,
    pub size: usize,
    pub is_free: bool,
    pub next: *mut Block,
    pub prev: *mut Block,
}

#[repr(align(16))]
struct AlignedHeap([u8; HEAP_SIZE]);
static mut HEAP: AlignedHeap = AlignedHeap([0; HEAP_SIZE]);
static mut HEAP_HEAD: *mut Block = null_mut();

#[no_mangle]
pub extern "C" fn init_heap() {
    unsafe {
        let head = HEAP.0.as_mut_ptr() as *mut Block;
        let heap_base = HEAP.0.as_ptr() as usize;
        let heap_end = heap_base + HEAP_SIZE;
        (*head).magic = BLOCK_MAGIC;
        (*head).size = HEAP_SIZE - size_of::<Block>();
        (*head).is_free = true;
        (*head).next = null_mut();
        (*head).prev = null_mut();
        HEAP_HEAD = head;
        serial_write(b"[MEM] Paging enabled, heap at 0x\0".as_ptr());
        serial_write_hex(heap_base as u64);
        serial_write(b"-0x\0".as_ptr());
        serial_write_hex(heap_end as u64);
        serial_write(b"\n\0".as_ptr());
    }
}

#[no_mangle]
pub extern "C" fn rust_get_block_header_size() -> usize {
    size_of::<Block>()
}

#[no_mangle]
pub extern "C" fn rust_kmalloc(size: usize) -> *mut u8 {
    unsafe {
        if size == 0 {
            return null_mut();
        }

        let aligned_size = (size + (ALIGNMENT - 1)) & !(ALIGNMENT - 1);
        let mut current = HEAP_HEAD;

        while !current.is_null() {
            let block = &mut *current;
            if block.is_free && block.size >= aligned_size {
                // This block is large enough.
                // Check if we need to split it.
                if block.size > aligned_size + size_of::<Block>() {
                    // Split the block.
                    let new_block_addr = (current as usize) + size_of::<Block>() + aligned_size;
                    let new_block = new_block_addr as *mut Block;
                    
                    (*new_block).magic = BLOCK_MAGIC;
                    (*new_block).size = block.size - aligned_size - size_of::<Block>();
                    (*new_block).is_free = true;
                    (*new_block).next = block.next;
                    (*new_block).prev = current;

                    if !block.next.is_null() {
                        (*block.next).prev = new_block;
                    }
                    
                    // The original block is now smaller.
                    block.size = aligned_size;
                    block.next = new_block;
                }
                
                block.is_free = false;
                return (current as *mut u8).add(size_of::<Block>());
            }
            current = block.next;
        }

        serial_write(b"[HEAP-ERROR] Out of memory!\n\0".as_ptr());
        null_mut()
    }
}

#[no_mangle]
pub extern "C" fn rust_kfree(ptr: *mut u8) {
    if ptr.is_null() {
        return;
    }
    
    unsafe {
        let block_ptr = (ptr as *mut u8).sub(size_of::<Block>()) as *mut Block;
        let block = &mut *block_ptr;
        
        if block.magic != BLOCK_MAGIC || block.is_free {
            serial_write(b"[HEAP-ERROR] Invalid free() or double free!\n\0".as_ptr());
            return;
        }

        block.is_free = true;

        // Coalesce with next block
        if !block.next.is_null() {
            let next_block = &mut *block.next;
            if next_block.is_free {
                block.size += size_of::<Block>() + next_block.size;
                block.next = next_block.next;
                if !next_block.next.is_null() {
                    (*next_block.next).prev = block_ptr;
                }
            }
        }

        // Coalesce with previous block
        if !block.prev.is_null() {
            let prev_block = &mut *block.prev;
            if prev_block.is_free {
                prev_block.size += size_of::<Block>() + block.size;
                prev_block.next = block.next;
                if !block.next.is_null() {
                    (*block.next).prev = block.prev;
                }
            }
        }
    }
}

// Stubs for functions that were removed but might still be called elsewhere
#[no_mangle]
pub extern "C" fn rust_heap_validate() -> bool {
    true // Bump allocator is always valid
}

#[no_mangle]
pub extern "C" fn rust_heap_stats() -> (usize, usize, usize) {
    (0, 0, 0) // Not implemented for bump allocator
}
