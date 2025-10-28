extern crate alloc;
use alloc::vec::Vec;
use alloc::vec;
use core::iter::Iterator;
use core::option::Option;
use core::option::Option::{Some, None};

extern "C" {
    fn vga_clear();
    fn vga_print(s: *const u8);
    fn vga_set_color(color: u8);
    fn rust_keyboard_clear_buffer();
    fn serial_write(s: *const u8);
    fn rust_keyboard_get_char() -> i32;
    fn rust_vfs_read(path_ptr: *const u8, buf_ptr: *mut u8, max_len: i32) -> i32;
    fn rust_vfs_write(path_ptr: *const u8, buf_ptr: *const u8, len: u64) -> u64;
    fn rust_vfs_ls(path_ptr: *const u8) -> i32;
    fn rust_vfs_mkdir(path_ptr: *const u8) -> i32;
    fn rust_vfs_create_file(path_ptr: *const u8) -> i32;
    fn rust_vfs_unlink(path_ptr: *const u8) -> i32;
    fn rust_kmalloc(size: usize) -> *mut u8;
    fn rust_kfree(ptr: *mut u8);
    fn serial_write_hex(s: *const u8, value: u64);
    fn serial_write_dec(s: *const u8, value: u64);
    fn rust_get_block_header_size() -> usize;
    fn pause();
    fn rust_vga_enable_auto_clear();
    fn rust_vga_disable_auto_clear();
    fn rust_vga_is_auto_clear_enabled() -> bool;
    fn timer_get_ticks() -> u64;
    fn rust_process_list();
    fn pmm_total_memory() -> u64;
    fn pmm_free_memory() -> u64;
    fn pci_test_devices();

    // socket-level FFI
    fn sock_socket() -> i32;
    fn sock_bind(s: i32, ip: *const u8, port: u16) -> i32;
    fn sock_listen(s: i32, backlog: i32) -> i32;
    fn sock_accept(s: i32, out_ip: *mut u8, out_port: *mut u16) -> i32;
    fn sock_connect(s: i32, ip: *const u8, port: u16) -> i32;
    fn sock_send(s: i32, buf: *const u8, len: usize) -> isize;
    fn sock_recv(s: i32, buf: *mut u8, len: usize) -> isize;
    fn sock_close(s: i32) -> i32;
    fn sock_set_nonblock(s: i32, nonblock: i32) -> i32;
    fn sock_poll(fds: *mut i32, nfds: i32, events_out: *mut i32, timeout_ms: i32) -> i32;
    fn rtc_get_date(year: *mut i32, month: *mut i32, day: *mut i32, hour: *mut i32, minute: *mut i32, second: *mut i32);
}

// Utility functions
fn print_str(s: &[u8]) {
    let mut buf = [0u8; 1024];
    let len = core::cmp::min(s.len(), buf.len() - 1);
    buf[..len].copy_from_slice(&s[..len]);
    buf[len] = 0;
    unsafe {
        serial_write(buf.as_ptr());
        vga_print(buf.as_ptr());
    }
}

fn print_char(c: u8) {
    unsafe {
        let s = [c, 0];
        vga_print(s.as_ptr());
    }
}

fn print_int(n: usize) {
    let mut buf = [0u8; 32];
    let mut i = 0;
    let mut num = n;
    if num == 0 {
        buf[0] = b'0';
        i = 1;
    } else {
        while num > 0 {
            buf[i] = b'0' + (num % 10) as u8;
            num /= 10;
            i += 1;
        }
    }
    // Reverse the digits
    for j in 0..i/2 {
        let temp = buf[j];
        buf[j] = buf[i-1-j];
        buf[i-1-j] = temp;
    }
    buf[i] = 0;
    print_str(&buf[..i+1]);
}

fn get_str(bytes: &[u8]) -> &[u8] {
    if let Some(null_pos) = bytes.iter().position(|&b| b == 0) {
        &bytes[..null_pos]
    } else {
        bytes
    }
}

fn str_eq(a: &[u8], b: &[u8]) -> bool {
    let a_str = get_str(a);
    let b_str = get_str(b);
    a_str == b_str
}

fn copy_str(dest: &mut [u8], src: &[u8]) {
    if dest.len() == 0 { return; }
    let len = core::cmp::min(dest.len() - 1, src.len());
    dest[..len].copy_from_slice(&src[..len]);
    dest[len] = 0;
}

fn str_append(dest: &mut [u8], src: &[u8]) {
    let dest_len = get_str(dest).len();
    let remaining = dest.len().saturating_sub(dest_len + 1);
    let copy_len = core::cmp::min(remaining, src.len());
        
    if dest_len + copy_len < dest.len() {
        dest[dest_len..dest_len + copy_len].copy_from_slice(&src[..copy_len]);
        dest[dest_len + copy_len] = 0;
    }    
}

fn find_byte(haystack: &[u8], needle: u8) -> Option<usize> {
    haystack.iter().position(|&b| b == needle)
}

fn parse_int(s: &[u8]) -> Option<i32> {
    let mut result = 0i32;
    let mut negative = false;
    let mut i = 0;
    if s.len() == 0 { return None; }
    if s[0] == b'-' {
        negative = true;
        i = 1;
    }
    while i < s.len() && s[i] >= b'0' && s[i] <= b'9' {
        result = result * 10 + (s[i] - b'0') as i32;
        i += 1;
    }
    if i == 0 || (i == 1 && negative) { return None; }
    Some(if negative { -result } else { result })
}

const MAX_CMD_LEN: usize = 1024; // Reduce from 1024 to prevent overflow
const MAX_PATH_LEN: usize = 256;
const MAX_ENV_VARS: usize = 50; // Reduce from 50 to save memory
const MAX_ALIASES: usize = 20; // Reduce from 20 to save memory
const MAX_HISTORY: usize = 10; // Safe value instead of 1
const MAX_ARGS: usize = 32; // Reduce from 32 to save memory

#[derive(Clone, Copy)]
pub struct EnvVar {
    pub name: [u8; 64],
    pub value: [u8; 256],
}

#[derive(Clone, Copy)]
pub struct Alias {
    pub name: [u8; 32],
    pub command: [u8; 256],
}

pub struct BashShell {
    pub cwd: [u8; MAX_PATH_LEN],
    pub env_vars: *mut EnvVar,
    pub env_count: usize,
    pub aliases: *mut Alias,
    pub alias_count: usize,
    pub history: *mut u8,
    pub history_count: usize,
    pub history_pos: usize,
    pub last_exit_code: i32,
    pub prompt: [u8; 64],
}

// Global bash shell instance
static mut BASH_SHELL: Option<BashShell> = None;

#[no_mangle]
pub extern "C" fn rust_bash_init() {
    unsafe {
        while rust_keyboard_get_char() != -1 {}
        let shell = BashShell::new();
        
        // Store in global
        BASH_SHELL = Some(shell);
    }
}

impl BashShell {
    pub fn new() -> BashShell {        
        // Calculate allocation sizes
        let env_size = core::mem::size_of::<EnvVar>() * MAX_ENV_VARS;
        let alias_size = core::mem::size_of::<Alias>() * MAX_ALIASES;
        let history_size = MAX_CMD_LEN * MAX_HISTORY;
        
        let env_vars_ptr = unsafe { rust_kmalloc(env_size) as *mut EnvVar };
        let aliases_ptr = unsafe { rust_kmalloc(alias_size) as *mut Alias };
        let history_ptr = unsafe { rust_kmalloc(history_size) as *mut u8 };
        
        if env_vars_ptr.is_null() || aliases_ptr.is_null() || history_ptr.is_null() {
            panic!("Failed to allocate memory for shell");
        }

        unsafe {
            core::ptr::write_bytes(env_vars_ptr as *mut u8, 0, env_size);
            core::ptr::write_bytes(aliases_ptr as *mut u8, 0, alias_size);
            core::ptr::write_bytes(history_ptr, 0, history_size);
        }

        let mut shell = Self {
            cwd: [0; MAX_PATH_LEN],
            env_vars: env_vars_ptr,
            env_count: 0,
            aliases: aliases_ptr,
            alias_count: 0,
            history: history_ptr,
            history_count: 0,
            history_pos: 0,
            last_exit_code: 0,
            prompt: [0; 64],
        };
        
        // Set initial working directory to root
        shell.cwd[0] = b'/';
        shell.cwd[1] = 0;
        
        shell
    }
    
    pub fn set_env(&mut self, name: &[u8], value: &[u8]) {
        unsafe { serial_write(b"[BASH-DEBUG] set_env: START\n\0".as_ptr()); }
        
        // Skip if env_vars is null (not allocated)
        if self.env_vars.is_null() {
            unsafe { serial_write(b"[BASH-DEBUG] set_env: env_vars is null, return\n\0".as_ptr()); }
            return;
        }
        
        unsafe { serial_write(b"[BASH-DEBUG] set_env: before search loop\n\0".as_ptr()); }
        
        let mut index = self.env_count;
        let mut found = false;
        
        for i in 0..self.env_count {
            unsafe { serial_write(b"[BASH-DEBUG] set_env: before deref env_vars.add(i)\n\0".as_ptr()); }
            let env_var = unsafe { &mut *self.env_vars.add(i) };
            unsafe { serial_write(b"[BASH-DEBUG] set_env: after deref env_vars.add(i)\n\0".as_ptr()); }
            
            // Compare names
            let mut match_name = true;
            for j in 0..64 {
                if env_var.name[j] != name.get(j).copied().unwrap_or(0) {
                    match_name = false;
                    break;
                }
                if name.get(j).copied().unwrap_or(0) == 0 { break; }
            }
            if match_name {
                index = i;
                found = true;
                break;
            }
        }
        
        unsafe { serial_write(b"[BASH-DEBUG] set_env: after search loop\n\0".as_ptr()); }
        
        if !found && self.env_count < MAX_ENV_VARS {
            unsafe { serial_write(b"[BASH-DEBUG] set_env: incrementing env_count\n\0".as_ptr()); }
            self.env_count += 1;
        }
        
        if index < MAX_ENV_VARS {
            unsafe { serial_write(b"[BASH-DEBUG] set_env: before deref env_vars.add(index)\n\0".as_ptr()); }
            let env_var = unsafe { &mut *self.env_vars.add(index) };
            unsafe { serial_write(b"[BASH-DEBUG] set_env: after deref env_vars.add(index)\n\0".as_ptr()); }
            
            // Copy name
            unsafe { serial_write(b"[BASH-DEBUG] set_env: before copy name\n\0".as_ptr()); }
            for j in 0..64 {
                env_var.name[j] = name.get(j).copied().unwrap_or(0);
                if name.get(j).copied().unwrap_or(0) == 0 { break; }
            }
            unsafe { serial_write(b"[BASH-DEBUG] set_env: after copy name\n\0".as_ptr()); }
            
            // Copy value
            unsafe { serial_write(b"[BASH-DEBUG] set_env: before copy value\n\0".as_ptr()); }
            for j in 0..256 {
                env_var.value[j] = value.get(j).copied().unwrap_or(0);
                if value.get(j).copied().unwrap_or(0) == 0 { break; }
            }
            unsafe { serial_write(b"[BASH-DEBUG] set_env: after copy value\n\0".as_ptr()); }
        }
        
        unsafe { serial_write(b"[BASH-DEBUG] set_env: END\n\0".as_ptr()); }
    }
    
    pub fn get_env(&self, name: &[u8]) -> Option<&[u8]> {
        // Skip if env_vars is null (not allocated)
        if self.env_vars.is_null() {
            return None;
        }
        
        unsafe {
            for i in 0..self.env_count {
                if str_eq(&(*self.env_vars.add(i)).name, name) {
                    return Some(get_str(&(*self.env_vars.add(i)).value));
                }
            }
        }
        None
    }
    
    pub fn set_alias(&mut self, name: &[u8], command: &[u8]) {
        // Skip if aliases is null (not allocated)
        if self.aliases.is_null() {
            return;
        }
        
        if self.alias_count < MAX_ALIASES {
            unsafe {
                // Manual assignment for name
                let name_len = core::cmp::min(name.len(), 31);
                (&mut (*self.aliases.add(self.alias_count)).name)[..name_len].copy_from_slice(&name[..name_len]);
                (&mut (*self.aliases.add(self.alias_count)).name)[name_len] = 0;
                // Manual assignment for command
                let cmd_len = core::cmp::min(command.len(), 255);
                (&mut (*self.aliases.add(self.alias_count)).command)[..cmd_len].copy_from_slice(&command[..cmd_len]);
                (&mut (*self.aliases.add(self.alias_count)).command)[cmd_len] = 0;
            }
            self.alias_count += 1;
        }
    }
    
    pub fn get_alias(&self, name: &[u8]) -> Option<&[u8]> {
        // Skip if aliases is null (not allocated)
        if self.aliases.is_null() {
            return None;
        }
        
        unsafe {
            for i in 0..self.alias_count {
                if str_eq(&(*self.aliases.add(i)).name, name) {
                    return Some(get_str(&(*self.aliases.add(i)).command));
                }
            }
        }
        None
    }
    
    pub fn add_to_history(&mut self, command: &[u8]) {
        // Skip if history is null (not allocated)
        if self.history.is_null() {
            return;
        }
        
        unsafe {
            if self.history_count < MAX_HISTORY {
                let entry_ptr = self.history.add(self.history_count * MAX_CMD_LEN) as *mut [u8; MAX_CMD_LEN];
                // Manual assignment for history entry
                let cmd_len = core::cmp::min(command.len(), MAX_CMD_LEN - 1);
                (&mut (*entry_ptr))[..cmd_len].copy_from_slice(&command[..cmd_len]);
                (&mut (*entry_ptr))[cmd_len] = 0;
                self.history_count += 1;
            } else {
                // Shift history
                for i in 0..MAX_HISTORY-1 {
                    let dst = self.history.add(i * MAX_CMD_LEN) as *mut [u8; MAX_CMD_LEN];
                    let src = self.history.add((i + 1) * MAX_CMD_LEN) as *mut [u8; MAX_CMD_LEN];
                    *dst = *src;
                }
                let entry_ptr = self.history.add((MAX_HISTORY - 1) * MAX_CMD_LEN) as *mut [u8; MAX_CMD_LEN];
                // Manual assignment for last history entry
                let cmd_len = core::cmp::min(command.len(), MAX_CMD_LEN - 1);
                (&mut (*entry_ptr))[..cmd_len].copy_from_slice(&command[..cmd_len]);
                (&mut (*entry_ptr))[cmd_len] = 0;
            }
        }
        self.history_pos = self.history_count;
    }
    
    pub fn update_prompt(&mut self) {
        // Simple prompt: user@host:pwd$
        let mut prompt = [0u8; 64];
        let mut pos = 0;
        let prefix = b"root@shadeos:";
        let cwd_str = get_str(&self.cwd);
        let suffix = b"$ ";
        let mut copy = |src: &[u8]| {
            for &b in src {
                if pos < 63 {
                    prompt[pos] = b;
                    pos += 1;
                }
            }
        };
        copy(prefix);
        copy(cwd_str);
        copy(suffix);
        prompt[pos] = 0;
        self.prompt.copy_from_slice(&prompt);
    }
    
    pub fn run(&mut self) {
        unsafe { vga_clear(); }
        unsafe { rust_keyboard_clear_buffer(); }
        unsafe { vga_set_color(0x0E); }        
        print_str(b"ShadeOS boot complete. Welcome!\n");
        unsafe { vga_set_color(0x0F); }
        unsafe {
            extern "C" { fn sys_sti(); }
            sys_sti();
        }
        
        loop {
            self.update_prompt();
            print_str(get_str(&self.prompt));
            let mut input = [0u8; MAX_CMD_LEN];
            if self.read_line(&mut input) {
                // Debug: print raw input buffer
                unsafe {
                    for i in 0..MAX_CMD_LEN {
                        if input[i] == 0 { break; }
                        let mut hex = [b'0'; 3];
                        let v = input[i];
                        hex[0] = b'0' + ((v >> 4) & 0xF);
                        hex[1] = b'0' + (v & 0xF);
                        hex[2] = b' ';
                        serial_write(hex.as_ptr());
                    }
                    serial_write(b"\n\0".as_ptr());
                }
                let input_str = get_str(&input);
                // unsafe {
                //     serial_write(b"[BASH-DEBUG] input_str len: \0".as_ptr());
                //     serial_write_dec(b"\0".as_ptr(), input_str.len() as u64);
                //     serial_write(b"\n\0".as_ptr());
                // }
                // Only process non-empty, non-whitespace commands
                let trimmed = input_str.iter().position(|&c| c != b' ' && c != b'\t').map(|start| {
                    let end = input_str.iter().rposition(|&c| c != b' ' && c != b'\t').unwrap_or(start);
                    &input_str[start..=end]
                }).unwrap_or(b"");
                
                if !trimmed.is_empty() {
                    self.add_to_history(trimmed);
                    self.execute_command(trimmed);
                } else {
                    unsafe { serial_write(b"[BASH-DEBUG] Empty command, continuing to next prompt\n\0".as_ptr()); }
                }
            } else {
                unsafe { serial_write(b"[BASH-DEBUG] After read_line (no input)\n\0".as_ptr()); }
            }
        }
    }
    
    fn read_line(&self, buffer: &mut [u8]) -> bool {
        let mut pos = 0;
        
        // Initialize buffer to all zeros to prevent garbage data
        for i in 0..buffer.len() {
            buffer[i] = 0;
        }
        
        loop {
            unsafe {
                let c = rust_keyboard_get_char();
                if c == -1 || c == 0 { 
                    // Use HLT to wait for interrupts instead of busy waiting
                    extern "C" { fn pause(); }
                    pause(); // This will halt until the next interrupt
                    continue; 
                }
                let ch = c as u8;
                
                
                match ch {
                    b'\n' | b'\r' => {
                        // Always accept Enter, even for empty commands
                        print_str(b"\n");
                        buffer[pos] = 0;
                        return true;
                    },
                    8 | 127 => { // Backspace
                        if pos > 0 {
                            pos -= 1;
                            buffer[pos] = 0; // Clear the character
                            print_char(8); print_char(b' '); print_char(8);
                        }
                    },
                    _ => {
                        if ch >= 32 && ch < 127 && pos < buffer.len() - 1 {
                            buffer[pos] = ch;
                            pos += 1;
                            print_char(ch);
                        }
                    }
                }
            }
        }
    }
    
    fn tab_complete(&self, _partial: &mut [u8]) {
        // Basic tab completion - just beep for now
        print_str(b"\x07"); // Bell character
    }
    
    pub fn execute_command(&mut self, command_line: &[u8]) {
        // Trim leading/trailing whitespace and check if empty
        let start = command_line.iter().position(|&c| c != b' ' && c != b'\t').unwrap_or(0);
        let end = command_line.iter().rposition(|&c| c != b' ' && c != b'\t').unwrap_or(0);
        
        if start > end {
            return;
        }
        let trimmed_command = &command_line[start..=end];

        let args_ptr = unsafe { rust_kmalloc(64 * MAX_ARGS) as *mut u8 };
        if args_ptr.is_null() {
            unsafe { serial_write(b"[BASH-ERROR] Failed to allocate args buffer\n\0".as_ptr()); }
            return;
        }
        
        // Create a slice view of the allocated memory
        let args_slice = unsafe { core::slice::from_raw_parts_mut(args_ptr, 64 * MAX_ARGS) };
        
        // Parse command into heap-allocated buffer
        let argc = self.parse_command_heap(trimmed_command, args_slice);
        
        unsafe {
            serial_write(b"[BASH-DEBUG] parse_command_heap returned argc=\0".as_ptr());
            serial_write_dec(b"\0".as_ptr(), argc as u64);
        }
        
        if argc == 0 { 
            unsafe { rust_kfree(args_ptr); }
            return; 
        }
        
        // Get command from first argument using proper helper function
        let cmd = self.get_arg_heap(args_slice, 0);
        
        // Check for aliases first - allocate alias buffer on heap
        let alias_buf_ptr = unsafe { rust_kmalloc(256) as *mut u8 };
        let mut is_alias = false;
        
        if !alias_buf_ptr.is_null() {
            if let Some(alias_cmd) = self.get_alias(cmd) {
                let alias_len = core::cmp::min(alias_cmd.len(), 255);
                unsafe {
                    core::ptr::copy_nonoverlapping(alias_cmd.as_ptr(), alias_buf_ptr, alias_len);
                    *alias_buf_ptr.add(alias_len) = 0;
                }
                is_alias = true;
            }
        }
        
        if is_alias {
            let alias_slice = unsafe { core::slice::from_raw_parts(alias_buf_ptr, 256) };
            self.execute_command(get_str(alias_slice));
            unsafe { rust_kfree(alias_buf_ptr); }
            unsafe { rust_kfree(args_ptr); }
            return;
        }
        
        if !alias_buf_ptr.is_null() {
            unsafe { rust_kfree(alias_buf_ptr); }
        }
        
        // Built-in commands
        match cmd {
            b"help" => self.cmd_help(), //work
            b"exit" => self.cmd_exit_heap(args_slice, argc), //work but needs to actually exit the shell
            b"cd" => self.cmd_cd_heap(args_slice, argc), //works
            b"pwd" => self.cmd_pwd_heap(args_slice, argc),//works
            b"ls" => self.cmd_ls_heap(args_slice, argc), //works but needs to list (. and  ..) in the list (minor upgrade)
            b"cat" => self.cmd_cat_heap(args_slice, argc), //cant test due to keyboard inout limitations doesnt supports alternate chars(shift + char)
            b"echo" => self.cmd_echo_heap(args_slice, argc),//works but needs enhacement
            b"mkdir" => self.cmd_mkdir_heap(args_slice, argc),//works
            b"touch" => self.cmd_touch_heap(args_slice, argc),//works
            b"rm" => self.cmd_rm_heap(args_slice, argc),//works
            b"env" => self.cmd_env(),//works
            b"export" => self.cmd_export_heap(args_slice, argc), //works
            b"alias" => self.cmd_alias_heap(args_slice, argc), //works
            b"history" => self.cmd_history(), // works
            b"clear" => self.cmd_clear(), // works
            b"date" => self.cmd_date(), // works but date is incorrect
            b"uptime" => self.cmd_uptime(), // work
            b"ps" => self.cmd_ps(), // work
            b"kill" => self.cmd_kill_heap(args_slice, argc), // work
            b"which" => self.cmd_which_heap(args_slice, argc), // work
            b"whoami" => self.cmd_whoami(), // work
            b"uname" => self.cmd_uname(), // work
            b"free" => self.cmd_free(),//works
            b"df" => self.cmd_df(), //works
            b"mv" => self.cmd_mv_heap(args_slice, argc), //not implemented
            b"ifconfig" => self.cmd_ifconfig(),//works with default will be able to test soon after tcp implementation
            b"route" => self.cmd_route(),//works
            b"htop" => self.cmd_htop(),//works
            b"lspci" => self.cmd_device(),//works
            b"mount" => self.cmd_mount(),
            b"ssh" => self.cmd_ssh_heap(args_slice, argc),
            b"scp" => self.cmd_scp_heap(args_slice, argc),
            b"rsync" => self.cmd_rsync_heap(args_slice, argc),
            b"top" => self.cmd_top(),
            b"iotop" => self.cmd_iotop(),
            b"lsof" => self.cmd_lsof(),
            b"chmod" => self.cmd_chmod_heap(args_slice, argc),
            b"chown" => self.cmd_chown_heap(args_slice, argc),
            b"ln" => self.cmd_ln_heap(args_slice, argc),
            b"du" => self.cmd_du_heap(args_slice, argc),
            b"sort" => self.cmd_sort_heap(args_slice, argc),
            b"uniq" => self.cmd_uniq_heap(args_slice, argc),
            b"cut" => self.cmd_cut_heap(args_slice, argc),
            b"tr" => self.cmd_tr_heap(args_slice, argc),
            b"sed" => self.cmd_sed_heap(args_slice, argc),
            b"awk" => self.cmd_awk_heap(args_slice, argc),
            b"tar" => self.cmd_tar_heap(args_slice, argc),
            b"gzip" => self.cmd_gzip_heap(args_slice, argc),
            b"gunzip" => self.cmd_gunzip_heap(args_slice, argc),
            b"wget" => self.cmd_wget_heap(args_slice, argc),
            b"curl" => self.cmd_curl_heap(args_slice, argc),
            b"ping" => self.cmd_ping_heap(args_slice, argc), //has invalid opcode error
            b"httpget" => self.cmd_httpget_heap(args_slice, argc),
            b"netstat" => self.cmd_netstat(),
            b"strace" => self.cmd_strace_heap(args_slice, argc),
            b"gdb" => self.cmd_gdb_heap(args_slice, argc),
            b"valgrind" => self.cmd_valgrind_heap(args_slice, argc),
            b"make" => self.cmd_make_heap(args_slice, argc),
            b"gcc" => self.cmd_gcc_heap(args_slice, argc),
            b"g++" => self.cmd_gpp_heap(args_slice, argc),
            b"ld" => self.cmd_ld_heap(args_slice, argc),
            b"ar" => self.cmd_ar_heap(args_slice, argc),
            b"nm" => self.cmd_nm_heap(args_slice, argc),
            b"objdump" => self.cmd_objdump_heap(args_slice, argc),
            b"readelf" => self.cmd_readelf_heap(args_slice, argc),
            b"strings" => self.cmd_strings_heap(args_slice, argc),
            b"hexdump" => self.cmd_hexdump_heap(args_slice, argc),
            b"od" => self.cmd_od_heap(args_slice, argc),
            b"xxd" => self.cmd_xxd_heap(args_slice, argc),
            b"base64" => self.cmd_base64_heap(args_slice, argc),
            b"md5sum" => self.cmd_md5sum_heap(args_slice, argc),
            b"sha1sum" => self.cmd_sha1sum_heap(args_slice, argc),
            b"sha256sum" => self.cmd_sha256sum_heap(args_slice, argc),
            b"openssl" => self.cmd_openssl_heap(args_slice, argc),
            b"gpg" => self.cmd_gpg_heap(args_slice, argc),
            b"vim" => self.cmd_vim_heap(args_slice, argc),
            b"nano" => self.cmd_nano_heap(args_slice, argc),
            b"emacs" => self.cmd_emacs_heap(args_slice, argc),
            b"less" => self.cmd_less_heap(args_slice, argc),
            b"more" => self.cmd_more_heap(args_slice, argc),
            b"man" => self.cmd_man_heap(args_slice, argc),
            b"info" => self.cmd_info_heap(args_slice, argc),
            b"apropos" => self.cmd_apropos_heap(args_slice, argc),
            b"whatis" => self.cmd_whatis_heap(args_slice, argc),
            b"locate" => self.cmd_locate_heap(args_slice, argc),
            b"updatedb" => self.cmd_updatedb(),
            b"crontab" => self.cmd_crontab_heap(args_slice, argc),
            b"at" => self.cmd_at_heap(args_slice, argc),
            b"batch" => self.cmd_batch_heap(args_slice, argc),
            b"nohup" => self.cmd_nohup_heap(args_slice, argc),
            b"screen" => self.cmd_screen_heap(args_slice, argc),
            b"tmux" => self.cmd_tmux_heap(args_slice, argc),
            b"jobs" => self.cmd_jobs(),
            b"bg" => self.cmd_bg_heap(args_slice, argc),
            b"fg" => self.cmd_fg_heap(args_slice, argc),
            b"disown" => self.cmd_disown_heap(args_slice, argc),
            b"su" => self.cmd_su_heap(args_slice, argc),
            b"sudo" => self.cmd_sudo_heap(args_slice, argc),
            b"passwd" => self.cmd_passwd_heap(args_slice, argc),
            b"useradd" => self.cmd_useradd_heap(args_slice, argc),
            b"userdel" => self.cmd_userdel_heap(args_slice, argc),
            b"usermod" => self.cmd_usermod_heap(args_slice, argc),
            b"groupadd" => self.cmd_groupadd_heap(args_slice, argc),
            b"groupdel" => self.cmd_groupdel_heap(args_slice, argc),
            b"groupmod" => self.cmd_groupmod_heap(args_slice, argc),
            b"id" => self.cmd_id_heap(args_slice, argc),
            b"groups" => self.cmd_groups_heap(args_slice, argc),
            b"newgrp" => self.cmd_newgrp_heap(args_slice, argc),
            b"chgrp" => self.cmd_chgrp_heap(args_slice, argc),
            b"umask" => self.cmd_umask_heap(args_slice, argc),
            b"test" => self.cmd_test_http(),
            b"[" => self.cmd_test_heap(args_slice, argc),
            b"true" => self.cmd_true(),
            b"false" => self.cmd_false(),
            b"yes" => self.cmd_yes_heap(args_slice, argc),
            b"seq" => self.cmd_seq_heap(args_slice, argc),
            b"expr" => self.cmd_expr_heap(args_slice, argc),
            b"bc" => self.cmd_bc_heap(args_slice, argc),
            b"dc" => self.cmd_dc_heap(args_slice, argc),
            b"factor" => self.cmd_factor_heap(args_slice, argc),
            b"sleep" => self.cmd_sleep_heap(args_slice, argc),
            b"timeout" => self.cmd_timeout_heap(args_slice, argc),
            b"watch" => self.cmd_watch_heap(args_slice, argc),
            b"time" => self.cmd_time_heap(args_slice, argc),
            b"tee" => self.cmd_tee_heap(args_slice, argc),
            b"xargs" => self.cmd_xargs_heap(args_slice, argc),
            b"parallel" => self.cmd_parallel_heap(args_slice, argc),
            b"test_args" => self.cmd_test_args_heap(args_slice, argc),
            b"vga" => self.cmd_vga_heap(args_slice, argc),
            _ => {
                print_str(b"bash: ");
                print_str(cmd);
                print_str(b": command not found\n");
                self.last_exit_code = 127;
            }
        }
        
        unsafe { rust_kfree(args_ptr); }
    }
    
    fn parse_command_heap(&self, command_line: &[u8], args_buffer: &mut [u8]) -> usize {
        let mut argc = 0;
        let mut i = 0;
        let mut arg_start = 0;
        let mut in_quotes = false;
        let mut quote_char = 0u8;
        let mut buffer_pos = 0;
        
        // Skip leading whitespace
        while i < command_line.len() && (command_line[i] == b' ' || command_line[i] == b'\t') {
            i += 1;
        }
        
        while i < command_line.len() && argc < MAX_ARGS && buffer_pos < args_buffer.len() - 64 {
            let ch = command_line[i];
            
            if !in_quotes && (ch == b' ' || ch == b'\t') {
                // End of argument
                if buffer_pos > arg_start {
                    args_buffer[buffer_pos] = 0;
                    buffer_pos += 1;
                    argc += 1;
                    arg_start = buffer_pos;
                }
                // Skip whitespace
                while i < command_line.len() && (command_line[i] == b' ' || command_line[i] == b'\t') {
                    i += 1;
                }
                continue;
            } else if ch == b'"' || ch == b'\'' {
                if !in_quotes {
                    in_quotes = true;
                    quote_char = ch;
                } else if ch == quote_char {
                    in_quotes = false;
                }
            } else {
                args_buffer[buffer_pos] = ch;
                buffer_pos += 1;
            }
            
            i += 1;
        }
        
        // Add final argument
        if buffer_pos > arg_start && argc < MAX_ARGS {
            args_buffer[buffer_pos] = 0;
            argc += 1;
        }
        
        argc
    }
    
    // Helper to get nth argument from heap buffer
    fn get_arg_heap<'a>(&self, args_buffer: &'a [u8], n: usize) -> &'a [u8] {
        let mut current_arg = 0;
        let mut pos = 0;
        
        unsafe { 
            serial_write(b"[BASH-DEBUG] get_arg_heap called with n=\0".as_ptr());
            serial_write_dec(b"\0".as_ptr(), n as u64);
        }
        
        // Find the start of the nth argument
        while pos < args_buffer.len() && current_arg < n {
            // Skip to next null terminator
            while pos < args_buffer.len() && args_buffer[pos] != 0 {
                pos += 1;
            }
            if pos < args_buffer.len() {
                pos += 1; // Skip the null terminator
            }
            current_arg += 1;
        }
        
        if pos >= args_buffer.len() || current_arg < n {
            unsafe { serial_write(b"[BASH-DEBUG] get_arg_heap: argument not found\n\0".as_ptr()); }
            return b"";
        }
        
        let start = pos;
        // Find the end of this argument
        while pos < args_buffer.len() && args_buffer[pos] != 0 {
            pos += 1;
        }
        
        let arg = &args_buffer[start..pos];
        unsafe {
            serial_write(b"[BASH-DEBUG] get_arg_heap: found arg '\0".as_ptr());
            for i in 0..core::cmp::min(arg.len(), 32) {
                let ch = [arg[i], 0];
                serial_write(ch.as_ptr());
            }
            serial_write(b"'\n\0".as_ptr());
        }
        arg
    }
    
    // Built-in command implementations using heap args
    fn cmd_exit_heap(&mut self, args_buffer: &[u8], argc: usize) {
        let exit_code = if argc > 1 {
            parse_int(self.get_arg_heap(args_buffer, 1)).unwrap_or(0)
        } else {
            self.last_exit_code
        };
        print_str(b"Goodbye!\n");
        self.last_exit_code = exit_code;
    }
    fn cmd_test_http(&mut self) {
        let url = "http://www.google.com\0";
        // allocate buffer in Rust and pass pointer to C http_get helper
        // let mut out = vec![0u8; 8192];
        // unsafe {
        //     let ret = http_get(url.as_ptr(), out.as_mut_ptr(), out.len() as i32);
        //     if ret > 0 {
        //         print_str(b"HTTP GET successful, response:\n");
        //         for i in 0..ret as usize {
        //             print_char(out[i]);
        //         }
        //         print_str(b"\n");
        //     } else {
        //         print_str(b"HTTP GET failed\n");
        //     }
        // }
        self.last_exit_code = 0;
    }
    fn cmd_cd_heap(&mut self, args_buffer: &[u8], argc: usize) {
        let path = if argc > 1 {
            self.get_arg_heap(args_buffer, 1)
        } else {
            self.get_env(b"HOME").unwrap_or(b"/")
         };
        if path.is_empty() {
            self.last_exit_code = 0;
            return;
        }

        let mut new_path_abs = [0u8; MAX_PATH_LEN];

        if path.starts_with(b"/") {
            copy_str(&mut new_path_abs, path);
        } else {
             let cwd_str = get_str(&self.cwd);
            if path == b".." {
                 if cwd_str.len() > 1 {
                     if let Some(last_slash) = cwd_str.iter().rposition(|&b| b == b'/') {
                        if last_slash > 0 {
                            copy_str(&mut new_path_abs, &cwd_str[..last_slash]);
                         } else {
                            copy_str(&mut new_path_abs, b"/");
                         }
                     }
                } else {
                    copy_str(&mut new_path_abs, b"/");
                 }
            } else if path == b"." {
                copy_str(&mut new_path_abs, cwd_str);
             } else {
                copy_str(&mut new_path_abs, cwd_str);
                 if cwd_str != b"/" {
                    str_append(&mut new_path_abs, b"/");
                 }
            str_append(&mut new_path_abs, path);
             }
        }

        // To check if the directory exists, we'll try to create a temporary file in it.
        let mut temp_file_path = [0u8; MAX_PATH_LEN];
        copy_str(&mut temp_file_path, get_str(&new_path_abs));
        if get_str(&new_path_abs) != b"/" {
            str_append(&mut temp_file_path, b"/");
        }
        // A unique name to avoid collisions.
        str_append(&mut temp_file_path, b".__gemini_cd_check");

        let result = unsafe { rust_vfs_create_file(temp_file_path.as_ptr()) };

        if result == 0 {
            // Success, the directory exists.
            // Clean up the temporary file.
            unsafe { rust_vfs_unlink(temp_file_path.as_ptr()); }

            // Update cwd.
            copy_str(&mut self.cwd, get_str(&new_path_abs));
             let cwd_val = get_str(&self.cwd).to_vec();
             self.set_env(b"PWD", &cwd_val);
             self.last_exit_code = 0;
        } else {
            // If creating a file fails, it could be because it's a file, not a directory,
            // or it doesn't exist. In either case, we can't cd into it.
            print_str(b"cd: ");
            print_str(path);
            print_str(b": No such file or directory\n");
            self.last_exit_code = 1;
         }
    }
 

    fn cmd_pwd_heap(&mut self, _args_buffer: &[u8], _argc: usize) {
        let cwd_str = get_str(&self.cwd);
        if cwd_str.is_empty() {
            // If cwd is empty, set it to root and print it
            self.cwd[0] = b'/';
            self.cwd[1] = 0;
            print_str(b"/\n");
        } else {
            print_str(cwd_str);
            print_str(b"\n");
        }
        self.last_exit_code = 0;
    }
    
    fn cmd_ls_heap(&mut self, args_buffer: &[u8], argc: usize) {
        let path = if argc > 1 {
            self.get_arg_heap(args_buffer, 1)
        } else {
            b"."
        };
        
        // Convert relative path to absolute
        let mut abs_path = [0u8; MAX_PATH_LEN];
        if path.starts_with(b"/") {
            copy_str(&mut abs_path, path);
        } else if path == b"." {
            copy_str(&mut abs_path, get_str(&self.cwd));
        } else {
            copy_str(&mut abs_path, get_str(&self.cwd));
            if get_str(&self.cwd) != b"/" {
                str_append(&mut abs_path, b"/");
            }
            str_append(&mut abs_path, path);
        }
        
        unsafe {
            let result = rust_vfs_ls(abs_path.as_ptr());
            self.last_exit_code = if result == 0 { 0 } else { 1 };
        }
    }
    
    fn cmd_cat_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: cat <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let filename = self.get_arg_heap(args_buffer, 1);
        let mut abs_path = [0u8; MAX_PATH_LEN];
        
        if filename.starts_with(b"/") {
            copy_str(&mut abs_path, filename);
        } else {
            copy_str(&mut abs_path, get_str(&self.cwd));
            if get_str(&self.cwd) != b"/" {
                str_append(&mut abs_path, b"/");
            }
            str_append(&mut abs_path, filename);
        }
        
        let mut buffer = [0u8; 4096];
        unsafe {
            let bytes_read = rust_vfs_read(abs_path.as_ptr(), buffer.as_mut_ptr(), 4095);
            if bytes_read > 0 {
                buffer[bytes_read as usize] = 0;
                print_str(&buffer[..bytes_read as usize + 1]);
                self.last_exit_code = 0;
            } else {
                print_str(b"cat: ");
                print_str(filename);
                print_str(b": No such file or directory\n");
                self.last_exit_code = 1;
            }
        }
    }
    
    fn cmd_echo_heap(&mut self, args_buffer: &[u8], argc: usize) {
        for i in 1..argc {
            if i > 1 {
                print_str(b" ");
            }
            print_str(self.get_arg_heap(args_buffer, i));
        }
        print_str(b"\n");
        self.last_exit_code = 0;
    }
    
    fn cmd_mkdir_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: mkdir <directory>\n");
            self.last_exit_code = 1;
            return;
        }
        
    let dirname = self.get_arg_heap(args_buffer, 1);

    unsafe {
        serial_write(b"[BASH-DEBUG] In cmd_mkdir_heap, dirname is: '\0".as_ptr());
        for &byte in dirname {
            let ch = [byte, 0];
            serial_write(ch.as_ptr());
        }
        serial_write(b"'\n\0".as_ptr());
    }
        let mut abs_path = [0u8; MAX_PATH_LEN];
        
        if dirname.starts_with(b"/") {
            copy_str(&mut abs_path, dirname);
        } else {
            copy_str(&mut abs_path, get_str(&self.cwd));
            if get_str(&self.cwd) != b"/" {
                str_append(&mut abs_path, b"/");
            }
            str_append(&mut abs_path, dirname);
        }
        
        unsafe {
            let result = rust_vfs_mkdir(abs_path.as_ptr());
            if result == 0 {
                self.last_exit_code = 0;
            } else {
                print_str(b"mkdir: cannot create directory '");
                print_str(dirname);
                print_str(b"'\n");
                self.last_exit_code = 1;
            }
        }
    }
    
    fn cmd_touch_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: touch <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let filename = self.get_arg_heap(args_buffer, 1);
        let mut abs_path = [0u8; MAX_PATH_LEN];
        
        if filename.starts_with(b"/") {
            copy_str(&mut abs_path, filename);
        } else {
            copy_str(&mut abs_path, get_str(&self.cwd));
            if get_str(&self.cwd) != b"/" {
                str_append(&mut abs_path, b"/");
            }
            str_append(&mut abs_path, filename);
        }
        
        unsafe {
            let result = rust_vfs_create_file(abs_path.as_ptr());
            if result == 0 {
                self.last_exit_code = 0;
            } else {
                print_str(b"touch: cannot touch '");
                print_str(filename);
                print_str(b"'\n");
                self.last_exit_code = 1;
            }
        }
    }
    
    fn cmd_rm_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: rm <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let filename = self.get_arg_heap(args_buffer, 1);
        let mut abs_path = [0u8; MAX_PATH_LEN];
        
        if filename.starts_with(b"/") {
            copy_str(&mut abs_path, filename);
        } else {
            copy_str(&mut abs_path, get_str(&self.cwd));
            if get_str(&self.cwd) != b"/" {
                str_append(&mut abs_path, b"/");
            }
            str_append(&mut abs_path, filename);
        }
        
        unsafe {
            let result = rust_vfs_unlink(abs_path.as_ptr());
            if result == 0 {
                self.last_exit_code = 0;
            } else {
                print_str(b"rm: cannot remove '");
                print_str(filename);
                print_str(b"': No such file or directory\n");
                self.last_exit_code = 1;
            }
        }
    }
    
    fn cmd_export_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: export VAR=value\n");
            self.last_exit_code = 1;
            return;
        }
        let assignment_buf = self.get_arg_heap(args_buffer, 1).to_vec();
        let assignment = &assignment_buf[..];
        if let Some(eq_pos) = find_byte(assignment, b'=') {
            let name = assignment[..eq_pos].to_vec();
            let value = assignment[eq_pos + 1..].to_vec();
            self.set_env(&name, &value);
            self.last_exit_code = 0;
        } else {
            print_str(b"export: invalid assignment\n");
            self.last_exit_code = 1;
        }
    }
    
    fn cmd_alias_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            // Show all aliases
            if !self.aliases.is_null() {
                unsafe {
                    for i in 0..self.alias_count {
                        let alias = &*self.aliases.add(i);
                        print_str(b"alias ");
                        print_str(get_str(&alias.name));
                        print_str(b"='");
                        print_str(get_str(&alias.command));
                        print_str(b"'\n");
                    }
                }
            }
            self.last_exit_code = 0;
            return;
        }
        let assignment_buf = self.get_arg_heap(args_buffer, 1).to_vec();
        let assignment = &assignment_buf[..];
        if let Some(eq_pos) = find_byte(assignment, b'=') {
            let name = assignment[..eq_pos].to_vec();
            let command = assignment[eq_pos + 1..].to_vec();
            self.set_alias(&name, &command);
            self.last_exit_code = 0;
        } else {
            print_str(b"alias: invalid assignment\n");
            self.last_exit_code = 1;
        }
    }
    
    fn cmd_kill_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: kill <pid>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let pid_str = self.get_arg_heap(args_buffer, 1);
        if let Some(_pid) = parse_int(pid_str) {
            print_str(b"kill: process termination not implemented\n");
            self.last_exit_code = 1;
        } else {
            print_str(b"kill: invalid PID\n");
            self.last_exit_code = 1;
        }
    }
    
    fn cmd_which_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: which <command>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let command = self.get_arg_heap(args_buffer, 1);
        
        // Check if it's a built-in command
        let builtins: &[&[u8]] = &[
            b"help", b"exit", b"cd", b"pwd", b"ls", b"cat", b"echo", b"mkdir", b"touch", b"rm",
            b"env", b"export", b"alias", b"history", b"clear", b"date", b"uptime", b"ps", b"kill",
            b"which", b"whoami", b"uname", b"free", b"df", b"mount", b"umount"
        ];
        
        for &builtin in builtins {
            if str_eq(command, builtin) {
                print_str(command);
                print_str(b": shell builtin\n");
                self.last_exit_code = 0;
                return;
            }
        }
        
        print_str(command);
        print_str(b": not found\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_umount_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: umount <mountpoint>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _mountpoint = self.get_arg_heap(args_buffer, 1);
        print_str(b"umount: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_grep_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 3 {
            print_str(b"Usage: grep <pattern> <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _pattern = self.get_arg_heap(args_buffer, 1);
        let _file = self.get_arg_heap(args_buffer, 2);
        print_str(b"grep: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_find_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: find <path> [options]\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _path = self.get_arg_heap(args_buffer, 1);
        print_str(b"find: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_wc_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: wc <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _file = self.get_arg_heap(args_buffer, 1);
        print_str(b"wc: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_head_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: head <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _file = self.get_arg_heap(args_buffer, 1);
        print_str(b"head: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_tail_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: tail <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _file = self.get_arg_heap(args_buffer, 1);
        print_str(b"tail: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_cp_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 3 {
            print_str(b"Usage: cp <source> <destination>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _source = self.get_arg_heap(args_buffer, 1);
        let _dest = self.get_arg_heap(args_buffer, 2);
        print_str(b"cp: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_mv_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 3 {
            print_str(b"Usage: mv <source> <destination>\n");
            self.last_exit_code = 1;
            return;
        }

        let source = self.get_arg_heap(args_buffer, 1);
        let dest = self.get_arg_heap(args_buffer, 2);

        let mut source_path = [0u8; MAX_PATH_LEN];
        if source.starts_with(b"/") {
            copy_str(&mut source_path, source);
        } else {
            copy_str(&mut source_path, get_str(&self.cwd));
            if get_str(&self.cwd) != b"/" {
                str_append(&mut source_path, b"/");
            }
            str_append(&mut source_path, source);
        }

        let mut dest_path = [0u8; MAX_PATH_LEN];
        if dest.starts_with(b"/") {
            copy_str(&mut dest_path, dest);
        } else {
            copy_str(&mut dest_path, get_str(&self.cwd));
            if get_str(&self.cwd) != b"/" {
                str_append(&mut dest_path, b"/");
            }
            str_append(&mut dest_path, dest);
        }

        let mut buffer = [0u8; 4096];
        let bytes_read = unsafe { rust_vfs_read(source_path.as_ptr(), buffer.as_mut_ptr(), 4095) };

        if bytes_read <= 0 {
            print_str(b"mv: cannot stat '" );
            print_str(source);
            print_str(b"': No such file or directory\n");
            self.last_exit_code = 1;
            return;
        }

        let write_result = unsafe { rust_vfs_write(dest_path.as_ptr(), buffer.as_ptr(), bytes_read as u64) };

        if write_result != bytes_read as u64 {
            // Attempt to create the file first if it doesn't exist
            if unsafe { rust_vfs_create_file(dest_path.as_ptr()) } == 0 {
                let write_result2 = unsafe { rust_vfs_write(dest_path.as_ptr(), buffer.as_ptr(), bytes_read as u64) };
                if write_result2 != bytes_read as u64 {
                    print_str(b"mv: cannot move '" );
                    print_str(source);
                    print_str(b"' to '" );
                    print_str(dest);
                    print_str(b"': Write failed\n");
                    self.last_exit_code = 1;
                    return;
                }
            } else {
                print_str(b"mv: cannot move '" );
                print_str(source);
                print_str(b"' to '" );
                print_str(dest);
                print_str(b"': Cannot create file\n");
                self.last_exit_code = 1;
                return;
            }
        }

        if unsafe { rust_vfs_unlink(source_path.as_ptr()) } != 0 {
            print_str(b"mv: failed to remove original file '" );
            print_str(source);
            print_str(b"'\n");
            self.last_exit_code = 1;
            return;
        }

        self.last_exit_code = 0;
    }
    
    fn cmd_chmod_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 3 {
            print_str(b"Usage: chmod <mode> <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _mode = self.get_arg_heap(args_buffer, 1);
        let _file = self.get_arg_heap(args_buffer, 2);
        print_str(b"chmod: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_chown_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 3 {
            print_str(b"Usage: chown <owner> <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _owner = self.get_arg_heap(args_buffer, 1);
        let _file = self.get_arg_heap(args_buffer, 2);
        print_str(b"chown: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_ln_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 3 {
            print_str(b"Usage: ln <target> <link>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _target = self.get_arg_heap(args_buffer, 1);
        let _link = self.get_arg_heap(args_buffer, 2);
        print_str(b"ln: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_du_heap(&mut self, args_buffer: &[u8], argc: usize) {
        let _path = if argc > 1 {
            self.get_arg_heap(args_buffer, 1)
        } else {
            b"."
        };
        print_str(b"du: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_sort_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: sort <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _file = self.get_arg_heap(args_buffer, 1);
        print_str(b"sort: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_uniq_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: uniq <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _file = self.get_arg_heap(args_buffer, 1);
        print_str(b"uniq: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_cut_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: cut [options] <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        print_str(b"cut: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_tr_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 3 {
            print_str(b"Usage: tr <set1> <set2>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _set1 = self.get_arg_heap(args_buffer, 1);
        let _set2 = self.get_arg_heap(args_buffer, 2);
        print_str(b"tr: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_sed_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: sed <script> [file]\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _script = self.get_arg_heap(args_buffer, 1);
        print_str(b"sed: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_awk_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: awk <program> [file]\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _program = self.get_arg_heap(args_buffer, 1);
        print_str(b"awk: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_tar_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: tar [options] <archive> [files...]\n");
            self.last_exit_code = 1;
            return;
        }
        
        print_str(b"tar: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_gzip_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: gzip <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _file = self.get_arg_heap(args_buffer, 1);
        print_str(b"gzip: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_gunzip_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: gunzip <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _file = self.get_arg_heap(args_buffer, 1);
        print_str(b"gunzip: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_wget_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: wget <url>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _url = self.get_arg_heap(args_buffer, 1);
        print_str(b"wget: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_curl_heap(&mut self, _args_buffer: &[u8], _argc: usize) {
        print_str(b"curl: not implemented, use httpget <ip> [path]\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_ping_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: ping <host>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _host = self.get_arg_heap(args_buffer, 1);
        print_str(b"ping: not implemented\n");
        self.last_exit_code = 1;
    }

    fn cmd_httpget_heap(&mut self, args_buffer: &[u8], argc: usize) {
        // if argc < 2 { print_str(b"usage: httpget <url>\n"); self.last_exit_code = 1; return; }
        // let url = self.get_arg_heap(args_buffer, 1);
        // // allocate buffer in Rust and pass pointer to C http_get helper
        // let mut out = vec![0u8; 8192];
        // unsafe {
        //     let ret = http_get(url.as_ptr(), out.as_mut_ptr(), out.len() as i32);
        //     if ret > 0 {
        //         let s = core::str::from_utf8(&out[..ret as usize]).unwrap_or("<binary>");
        //         print_str(b"http_get response:\n");
        //         print_str(s.as_bytes());
        //         print_str(b"\n");
        //     } else {
        //         print_str(b"http_get failed\n");
        //     }
        //     self.last_exit_code = if ret > 0 { 0 } else { 1 };
        // }
    }
    
    fn cmd_ssh_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: ssh <host>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _host = self.get_arg_heap(args_buffer, 1);
        print_str(b"ssh: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_scp_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 3 {
            print_str(b"Usage: scp <source> <destination>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _source = self.get_arg_heap(args_buffer, 1);
        let _dest = self.get_arg_heap(args_buffer, 2);
        print_str(b"scp: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_rsync_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 3 {
            print_str(b"Usage: rsync <source> <destination>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _source = self.get_arg_heap(args_buffer, 1);
        let _dest = self.get_arg_heap(args_buffer, 2);
        print_str(b"rsync: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_strace_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: strace <command>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _command = self.get_arg_heap(args_buffer, 1);
        print_str(b"strace: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_gdb_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: gdb <program>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _program = self.get_arg_heap(args_buffer, 1);
        print_str(b"gdb: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_valgrind_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: valgrind <program>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _program = self.get_arg_heap(args_buffer, 1);
        print_str(b"valgrind: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_make_heap(&mut self, args_buffer: &[u8], argc: usize) {
        let _target = if argc > 1 {
            self.get_arg_heap(args_buffer, 1)
        } else {
            b"all"
        };
        print_str(b"make: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_gcc_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: gcc <source>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _source = self.get_arg_heap(args_buffer, 1);
        print_str(b"gcc: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_gpp_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: g++ <source>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _source = self.get_arg_heap(args_buffer, 1);
        print_str(b"g++: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_ld_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: ld <object>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _object = self.get_arg_heap(args_buffer, 1);
        print_str(b"ld: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_ar_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 3 {
            print_str(b"Usage: ar <options> <archive> <files...>\n");
            self.last_exit_code = 1;
            return;
        }
        
        print_str(b"ar: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_nm_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: nm <object>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _object = self.get_arg_heap(args_buffer, 1);
        print_str(b"nm: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_objdump_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: objdump <object>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _object = self.get_arg_heap(args_buffer, 1);
        print_str(b"objdump: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_readelf_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: readelf <elf>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _elf = self.get_arg_heap(args_buffer, 1);
        print_str(b"readelf: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_strings_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: strings <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _file = self.get_arg_heap(args_buffer, 1);
        print_str(b"strings: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_hexdump_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: hexdump <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _file = self.get_arg_heap(args_buffer, 1);
        print_str(b"hexdump: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_od_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: od <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _file = self.get_arg_heap(args_buffer, 1);
        print_str(b"od: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_xxd_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: xxd <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _file = self.get_arg_heap(args_buffer, 1);
        print_str(b"xxd: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_base64_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: base64 <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _file = self.get_arg_heap(args_buffer, 1);
        print_str(b"base64: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_md5sum_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: md5sum <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _file = self.get_arg_heap(args_buffer, 1);
        print_str(b"md5sum: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_sha1sum_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: sha1sum <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _file = self.get_arg_heap(args_buffer, 1);
        print_str(b"sha1sum: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_sha256sum_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: sha256sum <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _file = self.get_arg_heap(args_buffer, 1);
        print_str(b"sha256sum: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_openssl_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: openssl <command> [args...]\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _command = self.get_arg_heap(args_buffer, 1);
        print_str(b"openssl: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_gpg_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: gpg [options] <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        print_str(b"gpg: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_vim_heap(&mut self, args_buffer: &[u8], argc: usize) {
        let _file = if argc > 1 {
            self.get_arg_heap(args_buffer, 1)
        } else {
            b""
        };
        print_str(b"vim: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_nano_heap(&mut self, args_buffer: &[u8], argc: usize) {
        let _file = if argc > 1 {
            self.get_arg_heap(args_buffer, 1)
        } else {
            b""
        };
        print_str(b"nano: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_emacs_heap(&mut self, args_buffer: &[u8], argc: usize) {
        let _file = if argc > 1 {
            self.get_arg_heap(args_buffer, 1)
        } else {
            b""
        };
        print_str(b"emacs: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_less_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: less <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _file = self.get_arg_heap(args_buffer, 1);
        print_str(b"less: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_more_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: more <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _file = self.get_arg_heap(args_buffer, 1);
        print_str(b"more: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_man_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: man <command>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _command = self.get_arg_heap(args_buffer, 1);
        print_str(b"man: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_info_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: info <topic>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _topic = self.get_arg_heap(args_buffer, 1);
        print_str(b"info: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_apropos_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: apropos <keyword>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _keyword = self.get_arg_heap(args_buffer, 1);
        print_str(b"apropos: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_whatis_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: whatis <command>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _command = self.get_arg_heap(args_buffer, 1);
        print_str(b"whatis: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_locate_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: locate <pattern>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _pattern = self.get_arg_heap(args_buffer, 1);
        print_str(b"locate: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_crontab_heap(&mut self, args_buffer: &[u8], argc: usize) {
        let _option = if argc > 1 {
            self.get_arg_heap(args_buffer, 1)
        } else {
            b"-l"
        };
        print_str(b"crontab: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_at_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: at <time>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _time = self.get_arg_heap(args_buffer, 1);
        print_str(b"at: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_batch_heap(&mut self, args_buffer: &[u8], argc: usize) {
        let _file = if argc > 1 {
            self.get_arg_heap(args_buffer, 1)
        } else {
            b""
        };
        print_str(b"batch: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_nohup_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: nohup <command>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _command = self.get_arg_heap(args_buffer, 1);
        print_str(b"nohup: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_screen_heap(&mut self, args_buffer: &[u8], argc: usize) {
        let _option = if argc > 1 {
            self.get_arg_heap(args_buffer, 1)
        } else {
            b""
        };
        print_str(b"screen: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_tmux_heap(&mut self, args_buffer: &[u8], argc: usize) {
        let _command = if argc > 1 {
            self.get_arg_heap(args_buffer, 1)
        } else {
            b"new-session"
        };
        print_str(b"tmux: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_bg_heap(&mut self, args_buffer: &[u8], argc: usize) {
        let _job = if argc > 1 {
            self.get_arg_heap(args_buffer, 1)
        } else {
            b"%1"
        };
        print_str(b"bg: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_fg_heap(&mut self, args_buffer: &[u8], argc: usize) {
        let _job = if argc > 1 {
            self.get_arg_heap(args_buffer, 1)
        } else {
            b"%1"
        };
        print_str(b"fg: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_disown_heap(&mut self, args_buffer: &[u8], argc: usize) {
        let _job = if argc > 1 {
            self.get_arg_heap(args_buffer, 1)
        } else {
            b"%1"
        };
        print_str(b"disown: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_su_heap(&mut self, args_buffer: &[u8], argc: usize) {
        let _user = if argc > 1 {
            self.get_arg_heap(args_buffer, 1)
        } else {
            b"root"
        };
        print_str(b"su: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_sudo_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: sudo <command>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _command = self.get_arg_heap(args_buffer, 1);
        print_str(b"sudo: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_passwd_heap(&mut self, args_buffer: &[u8], argc: usize) {
        let _user = if argc > 1 {
            self.get_arg_heap(args_buffer, 1)
        } else {
            b"root"
        };
        print_str(b"passwd: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_useradd_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: useradd <username>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _username = self.get_arg_heap(args_buffer, 1);
        print_str(b"useradd: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_userdel_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: userdel <username>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _username = self.get_arg_heap(args_buffer, 1);
        print_str(b"userdel: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_usermod_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: usermod [options] <username>\n");
            self.last_exit_code = 1;
            return;
        }
        
        print_str(b"usermod: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_groupadd_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: groupadd <groupname>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _groupname = self.get_arg_heap(args_buffer, 1);
        print_str(b"groupadd: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_groupdel_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: groupdel <groupname>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _groupname = self.get_arg_heap(args_buffer, 1);
        print_str(b"groupdel: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_groupmod_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: groupmod [options] <groupname>\n");
            self.last_exit_code = 1;
            return;
        }
        
        print_str(b"groupmod: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_id_heap(&mut self, args_buffer: &[u8], argc: usize) {
        let _user = if argc > 1 {
            self.get_arg_heap(args_buffer, 1)
        } else {
            b"root"
        };
        print_str(b"uid=0(root) gid=0(root) groups=0(root)\n");
        self.last_exit_code = 0;
    }
    
    fn cmd_groups_heap(&mut self, args_buffer: &[u8], argc: usize) {
        let _user = if argc > 1 {
            self.get_arg_heap(args_buffer, 1)
        } else {
            b"root"
        };
        print_str(b"root\n");
        self.last_exit_code = 0;
    }
    
    fn cmd_newgrp_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: newgrp <group>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _group = self.get_arg_heap(args_buffer, 1);
        print_str(b"newgrp: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_chgrp_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 3 {
            print_str(b"Usage: chgrp <group> <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _group = self.get_arg_heap(args_buffer, 1);
        let _file = self.get_arg_heap(args_buffer, 2);
        print_str(b"chgrp: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_umask_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc > 1 {
            let _mask = self.get_arg_heap(args_buffer, 1);
            print_str(b"umask: not implemented\n");
            self.last_exit_code = 1;
        } else {
            print_str(b"0022\n");
            self.last_exit_code = 0;
        }
    }
    
    fn cmd_test_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            self.last_exit_code = 1;
            return;
        }
        
        // Simple test implementation
        let _expr = self.get_arg_heap(args_buffer, 1);
        print_str(b"test: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_yes_heap(&mut self, args_buffer: &[u8], argc: usize) {
        let text = if argc > 1 {
            self.get_arg_heap(args_buffer, 1)
        } else {
            b"y"
        };
        
        // Print a few times then stop (to avoid infinite loop)
        for _ in 0..10 {
            print_str(text);
            print_str(b"\n");
        }
        print_str(b"... (stopped after 10 iterations)\n");
        self.last_exit_code = 0;
    }
    
    fn cmd_seq_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: seq <last> or seq <first> <last>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let (start, end) = if argc == 2 {
            (1, parse_int(self.get_arg_heap(args_buffer, 1)).unwrap_or(1))
        } else {
            (
                parse_int(self.get_arg_heap(args_buffer, 1)).unwrap_or(1),
                parse_int(self.get_arg_heap(args_buffer, 2)).unwrap_or(1)
            )
        };
        
        for i in start..=end {
            print_int(i as usize);
            print_str(b"\n");
        }
        self.last_exit_code = 0;
    }
    
    fn cmd_expr_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: expr <expression>\n");
            self.last_exit_code = 1;
            return;
        }
        
        // Simple expression evaluation
        let _expr = self.get_arg_heap(args_buffer, 1);
        print_str(b"expr: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_bc_heap(&mut self, args_buffer: &[u8], argc: usize) {
        let _file = if argc > 1 {
            self.get_arg_heap(args_buffer, 1)
        } else {
            b""
        };
        print_str(b"bc: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_dc_heap(&mut self, args_buffer: &[u8], argc: usize) {
        let _file = if argc > 1 {
            self.get_arg_heap(args_buffer, 1)
        } else {
            b""
        };
        print_str(b"dc: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_factor_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: factor <number>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _number = self.get_arg_heap(args_buffer, 1);
        print_str(b"factor: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_sleep_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: sleep <seconds>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _seconds = self.get_arg_heap(args_buffer, 1);
        print_str(b"sleep: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_timeout_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 3 {
            print_str(b"Usage: timeout <duration> <command>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _duration = self.get_arg_heap(args_buffer, 1);
        let _command = self.get_arg_heap(args_buffer, 2);
        print_str(b"timeout: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_watch_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: watch <command>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _command = self.get_arg_heap(args_buffer, 1);
        print_str(b"watch: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_time_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: time <command>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _command = self.get_arg_heap(args_buffer, 1);
        print_str(b"time: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_tee_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: tee <file>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _file = self.get_arg_heap(args_buffer, 1);
        print_str(b"tee: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_xargs_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: xargs <command>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _command = self.get_arg_heap(args_buffer, 1);
        print_str(b"xargs: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_parallel_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: parallel <command>\n");
            self.last_exit_code = 1;
            return;
        }
        
        let _command = self.get_arg_heap(args_buffer, 1);
        print_str(b"parallel: not implemented\n");
        self.last_exit_code = 1;
    }
    
    // Simple built-in commands (no heap args needed)
    fn cmd_help(&mut self) {
        print_str(b"ShadeOS Bash-compatible Shell - Available Commands:\n\n");
        print_str(b"File Operations:\n");
        print_str(b"  ls [path]          - List directory contents\n");
        print_str(b"  cat <file>         - Display file contents\n");
        print_str(b"  mkdir <dir>        - Create directory\n");
        print_str(b"  touch <file>       - Create empty file\n");
        print_str(b"  rm <file>          - Remove file\n");
        print_str(b"  cp <src> <dst>     - Copy file (not implemented)\n");
        print_str(b"  mv <src> <dst>     - Move file (not implemented)\n");
        print_str(b"\nNavigation:\n");
        print_str(b"  cd [path]          - Change directory\n");
        print_str(b"  pwd                - Print working directory\n");
        print_str(b"...\n");
        print_str(b"\nSystem:\n");
        print_str(b"  ps                 - List processes\n");
        print_str(b"  kill <pid>         - Terminate process\n");
        print_str(b"  free               - Show memory usage\n");
        print_str(b"  df                 - Show disk usage\n");
        print_str(b"  mount              - Show mounted filesystems\n");
        print_str(b"  uname              - System information\n");
        print_str(b"\nEnvironment:\n");
        print_str(b"  env                - Show environment variables\n");
        print_str(b"  export VAR=val     - Set environment variable\n");
        print_str(b"  alias name=cmd     - Create command alias\n");
        print_str(b"  history            - Show command history\n");
        print_str(b"\nUtilities:\n");
        print_str(b"  echo <text>        - Print text\n");
        print_str(b"  clear              - Clear screen\n");
        print_str(b"  date               - Show current date/time\n");
        print_str(b"  uptime             - Show system uptime\n");
        print_str(b"  which <cmd>        - Locate command\n");
        print_str(b"  whoami             - Show current user\n");
        print_str(b"  help               - Show this help\n");
        print_str(b"  exit [code]        - Exit shell\n");
        print_str(b"\nNote: Many advanced commands are recognized but not yet implemented.\n");
        self.last_exit_code = 0;
    }
    
    
    fn cmd_env(&mut self) {
        if !self.env_vars.is_null() {
            unsafe {
                for i in 0..self.env_count {
                    let env_var = &*self.env_vars.add(i);
                    print_str(get_str(&env_var.name));
                    print_str(b"=");
                    print_str(get_str(&env_var.value));
                    print_str(b"\n");
                }
            }
        }
        self.last_exit_code = 0;
    }
    
    fn cmd_history(&mut self) {
        if !self.history.is_null() {
            unsafe {
                for i in 0..self.history_count {
                    print_int(i + 1);
                    print_str(b"  ");
                    let entry_ptr = self.history.add(i * MAX_CMD_LEN);
                    let entry_slice = core::slice::from_raw_parts(entry_ptr, MAX_CMD_LEN);
                    print_str(get_str(entry_slice));
                    print_str(b"\n");
                }
            }
        }
        self.last_exit_code = 0;
    }
    
    fn cmd_clear(&mut self) {
        print_str(b"\x1b[2J\x1b[H"); // ANSI escape codes to clear screen
        unsafe { vga_clear(); };
        self.last_exit_code = 0;
    }
    
    fn cmd_vga_heap(&mut self, args_buffer: &[u8], argc: usize) {
        if argc < 2 {
            print_str(b"Usage: vga <command> [args]\n");
            print_str(b"Commands:\n");
            print_str(b"  auto-clear on|off    - Enable/disable auto-clear on screen overflow\n");
            print_str(b"  status               - Show current VGA settings\n");
            self.last_exit_code = 1;
            return;
        }
        
        let command = self.get_arg_heap(args_buffer, 1);
        
        if str_eq(command, b"auto-clear") {
            if argc < 3 {
                print_str(b"Usage: vga auto-clear on|off\n");
                self.last_exit_code = 1;
                return;
            }
            
            let arg = self.get_arg_heap(args_buffer, 2);
            if str_eq(arg, b"on") {
                unsafe { rust_vga_enable_auto_clear(); }
                print_str(b"Auto-clear enabled\n");
                self.last_exit_code = 0;
            } else if str_eq(arg, b"off") {
                unsafe { rust_vga_disable_auto_clear(); }
                print_str(b"Auto-clear disabled\n");
                self.last_exit_code = 0;
            } else {
                print_str(b"Invalid argument. Use 'on' or 'off'\n");
                self.last_exit_code = 1;
            }
        } else if str_eq(command, b"status") {
            let enabled = unsafe { rust_vga_is_auto_clear_enabled() };
            print_str(b"VGA Status:\n");
            print_str(b"  Auto-clear: ");
            if enabled {
                print_str(b"enabled\n");
            } else {
                print_str(b"disabled\n");
            }
            self.last_exit_code = 0;
        } else {
            print_str(b"Unknown command: ");
            print_str(command);
            print_str(b"\n");
            self.last_exit_code = 1;
        }
    }
    
    fn cmd_date(&mut self) {
        unsafe {
                let mut year: i32 = 0;
                let mut month: i32 = 0;
                let mut day: i32 = 0;
                let mut hour: i32 = 0;
                let mut minute: i32 = 0;
                let mut second: i32 = 0;

                rtc_get_date(
                    &mut year, &mut month, &mut day,
                    &mut hour, &mut minute, &mut second,
                );

                let msg = alloc::format!("{}-{}-{} {:02}:{:02}:{:02}\n",
                    day, month, year, hour, minute, second);
                print_str(msg.as_bytes());
            }
            self.last_exit_code = 0;
        }
    
    fn cmd_uptime(&mut self) {
    unsafe {
        extern "C" { 
            fn timer_get_seconds() -> u64;
            fn get_load1() -> f64;
            fn get_load5() -> f64;
            fn get_load15() -> f64;
        }

        let secs = timer_get_seconds();
        let days = secs / 86400;
        let hours = (secs % 86400) / 3600;
        let minutes = (secs % 3600) / 60;
        let seconds = secs % 60;

        let uptime_str = if days > 0 {
            alloc::format!("{} day{}, {:02}:{:02}", 
                days, if days > 1 { "s" } else { "" }, hours, minutes)
        } else if hours > 0 {
            alloc::format!("{:02}:{:02}", hours, minutes)
        } else if minutes > 0 {
            alloc::format!("{} min", minutes)
        } else {
            alloc::format!("{} sec", seconds)
        };

        let msg = alloc::format!(
            " up {},  1 user,  load average: {:.2}, {:.2}, {:.2}\n",
            uptime_str, get_load1(), get_load5(), get_load15()
        );

        print_str(msg.as_bytes());
    }
    }
    
    fn cmd_ps(&mut self) {
        print_str(b"  PID TTY          TIME CMD\n");
        unsafe {
            rust_process_list();
        }
        self.last_exit_code = 0;
    }
    
    fn cmd_whoami(&mut self) {
        print_str(b"root\n");
        self.last_exit_code = 0;
    }
    
    fn cmd_uname(&mut self) {
        print_str(b"ShadeOS\n");
        self.last_exit_code = 0;
    }
    
    fn cmd_free(&mut self) {
        unsafe {
            let total_mem = pmm_total_memory();
            let free_mem = pmm_free_memory();
            let used_mem = total_mem - free_mem;
            
            print_str(b"              total        used        free      shared  buff/cache   available\n");
            
            // Format total memory
            let total_kb = total_mem / 1024;
            let used_kb = used_mem / 1024;
            let free_kb = free_mem / 1024;
            
            print_str(b"Mem:        ");
            print_int(total_kb as usize);
            print_str(b"      ");
            print_int(used_kb as usize);
            print_str(b"      ");
            print_int(free_kb as usize);
            print_str(b"           0           0      ");
            print_int(free_kb as usize);
            print_str(b"\n");
        }
        self.last_exit_code = 0;
    }
    
    fn cmd_df(&mut self) {
        print_str(b"Filesystem     1K-blocks  Used Available Use% Mounted on\n");
        print_str(b"ramfs             16384     0     16384   0% /\n");
        self.last_exit_code = 0;
    }

    fn cmd_device(&mut self) {
        unsafe{
            print_str(b"will tell next time\n");
        };
        self.last_exit_code = 0;
    }
    fn cmd_mount(&mut self) {
        print_str(b"ramfs on / type ramfs (rw,relatime)\n");
        self.last_exit_code = 0;
    }
    
    fn cmd_netstat(&mut self) {
        print_str(b"Active Internet connections (w/o servers)\n");
        self.last_exit_code = 0;
    }
    
    fn cmd_ifconfig(&mut self) {
        print_str(b"lo: flags=73<UP,LOOPBACK,RUNNING>  mtu 65536\n");
        print_str(b"        inet 127.0.0.1  netmask 255.0.0.0\n");
        print_str(b"        loop  txqueuelen 1000  (Local Loopback)\n");
        self.last_exit_code = 0;
    }
    
    fn cmd_route(&mut self) {
        print_str(b"Kernel IP routing table\n");
        print_str(b"Destination     Gateway         Genmask         Flags Metric Ref    Use Iface\n");
        print_str(b"default         *               0.0.0.0         U     0      0        0 lo\n");
        self.last_exit_code = 0;
    }
    
    fn cmd_top(&mut self) {
        unsafe {
            let ticks = timer_get_ticks();
            print_str(b"top - ");
            format_uptime(ticks);
            print_str(b",  1 user,  load average: 0.00, 0.00, 0.00\n");
        }
        self.last_exit_code = 0;
    }
    
    fn cmd_htop(&mut self) {
        print_str(b"htop: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_iotop(&mut self) {
        print_str(b"iotop: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_lsof(&mut self) {
        print_str(b"lsof: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_updatedb(&mut self) {
        print_str(b"updatedb: not implemented\n");
        self.last_exit_code = 1;
    }
    
    fn cmd_jobs(&mut self) {
        print_str(b"No active jobs\n");
        self.last_exit_code = 0;
    }
    
    fn cmd_true(&mut self) {
        self.last_exit_code = 0;
    }
    
    fn cmd_false(&mut self) {
        self.last_exit_code = 1;
    }
    
    fn cmd_test_args_heap(&mut self, args_buffer: &[u8], argc: usize) {
        print_str(b"test_args: argc=");
        print_int(argc);
        print_str(b"\n");
        
        for i in 0..argc {
            print_str(b"  arg[");
            print_int(i);
            print_str(b"]=");
            let arg = self.get_arg_heap(args_buffer, i);
            print_str(b"'");
            print_str(arg);
            print_str(b"' (len=");
            print_int(arg.len());
            print_str(b")\n");
        }
        
        self.last_exit_code = 0;
    }
}

impl Drop for BashShell {
    fn drop(&mut self) {
        unsafe {
            if !self.env_vars.is_null() {
                rust_kfree(self.env_vars as *mut u8);
            }
            if !self.aliases.is_null() {
                rust_kfree(self.aliases as *mut u8);
            }
            if !self.history.is_null() {
                rust_kfree(self.history);
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn rust_bash_run() {
    unsafe {
        if let Some(ref mut shell) = BASH_SHELL {
            shell.run();
        } else {
            serial_write(b"[BASH-ERROR] Shell not initialized\n\0".as_ptr());
        }
    }
}

#[no_mangle]
pub extern "C" fn rust_bash_execute(command: *const u8) {
    if command.is_null() {
        return;
    }
    
    unsafe {
        if let Some(ref mut shell) = BASH_SHELL {
            // Convert C string to Rust slice
            let mut len = 0;
            while *command.add(len) != 0 {
                len += 1;
            }
            let cmd_slice = core::slice::from_raw_parts(command, len);
            shell.execute_command(cmd_slice);
        }
    }
}

// Add these helper functions after the existing utility functions

fn format_uptime(ticks: u64) {
    // Assuming 100 ticks per second (timer frequency)
    let seconds = ticks / 100;
    let days = seconds / 86400;
    let hours = (seconds % 86400) / 3600;
    let minutes = (seconds % 3600) / 60;
    let secs = seconds % 60;
    
    if days > 0 {
        print_str(b"up ");
        print_int(days as usize);
        print_str(b" days, ");
        print_int(hours as usize);
        print_str(b":");
        if minutes < 10 { print_str(b"0"); }
        print_int(minutes as usize);
        print_str(b":");
        if secs < 10 { print_str(b"0"); }
        print_int(secs as usize);
    } else if hours > 0 {
        print_str(b"up ");
        print_int(hours as usize);
        print_str(b":");
        if minutes < 10 { print_str(b"0"); }
        print_int(minutes as usize);
        print_str(b":");
        if secs < 10 { print_str(b"0"); }
        print_int(secs as usize);
    } else if minutes > 0 {
        print_str(b"up ");
        print_int(minutes as usize);
        print_str(b":");
        if secs < 10 { print_str(b"0"); }
        print_int(secs as usize);
    } else {
        print_str(b"up ");
        print_int(secs as usize);
        print_str(b" seconds");
    }
}
