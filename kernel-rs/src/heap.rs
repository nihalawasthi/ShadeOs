use core::mem::size_of;
use core::ptr::{null_mut, NonNull};
use core::option::Option;
use core::option::Option::{Some, None};

const HEAP_SIZE: usize = 1024 * 1024; // 1 MiB
const ALIGNMENT: usize = 16;
const BLOCK_MAGIC: u32 = 0xDEADBEEF;
static mut HEAP: [u8; HEAP_SIZE] = [0; HEAP_SIZE];

#[repr(C)]
struct Block {
    magic: u32,
    size: usize,
    free: bool,
    next: Option<NonNull<Block>>,
}

static mut HEAP_HEAD: Option<NonNull<Block>> = None;

fn align_up(addr: usize, align: usize) -> usize {
    (addr + align - 1) & !(align - 1)
}

unsafe fn init_heap() {
    if HEAP_HEAD.is_none() {
        let head = HEAP.as_mut_ptr() as *mut Block;
        (*head).magic = BLOCK_MAGIC;
        (*head).size = HEAP_SIZE - size_of::<Block>();
        (*head).free = true;
        (*head).next = None;
        HEAP_HEAD = Some(NonNull::new_unchecked(head));
    }
}

#[no_mangle]
pub extern "C" fn rust_kmalloc(size: usize) -> *mut u8 {
    if size == 0 || size > HEAP_SIZE { return null_mut(); }
    unsafe {
        init_heap();
        let mut current = HEAP_HEAD;
        while let Some(mut block_ptr) = current {
            let block = block_ptr.as_mut();
            if block.magic != BLOCK_MAGIC {
                return null_mut(); // Corruption detected
            }
            if block.free && block.size >= size {
                // Calculate aligned user pointer
                let block_start = block as *mut Block as usize;
                let user_start = align_up(block_start + size_of::<Block>(), ALIGNMENT);
                let align_offset = user_start - (block_start + size_of::<Block>());
                let total_needed = size + align_offset;
                if block.size >= total_needed {
                    // Split if large enough
                    if block.size >= total_needed + size_of::<Block>() + ALIGNMENT {
                        let new_block_ptr = (block as *mut Block as *mut u8)
                            .add(size_of::<Block>() + total_needed) as *mut Block;
                        (*new_block_ptr).magic = BLOCK_MAGIC;
                        (*new_block_ptr).size = block.size - total_needed - size_of::<Block>();
                        (*new_block_ptr).free = true;
                        (*new_block_ptr).next = block.next;
                        block.size = total_needed;
                        block.next = Some(NonNull::new_unchecked(new_block_ptr));
                    }
                    block.free = false;
                    let user_ptr = (block as *mut Block as *mut u8).add(size_of::<Block>() + align_offset);
                    return user_ptr;
                }
            }
            current = block.next;
        }
        null_mut()
    }
}

#[no_mangle]
pub extern "C" fn rust_kfree(ptr: *mut u8) {
    if ptr.is_null() { return; }
    unsafe {
        // Find the block header
        let mut block_ptr = (ptr as usize - size_of::<Block>()) as *mut Block;
        // Scan backwards if not aligned (in case of alignment offset)
        while (*block_ptr).magic != BLOCK_MAGIC {
            block_ptr = (block_ptr as *mut u8).sub(1) as *mut Block;
        }
        if (*block_ptr).magic != BLOCK_MAGIC {
            return; // Corruption detected
        }
        (*block_ptr).free = true;
        // Merge adjacent free blocks
        let mut current = HEAP_HEAD;
        while let Some(mut block_ptr) = current {
            let block = block_ptr.as_mut();
            while let Some(mut next_ptr) = block.next {
                let next = next_ptr.as_mut();
                if block.free && next.free {
                    block.size += size_of::<Block>() + next.size;
                    block.next = next.next;
                } else {
                    break;
                }
            }
            current = block.next;
        }
    }
} 