#include "shell.h"
#include "vga.h"
#include "keyboard.h"
#include "vfs.h" // Now uses the C wrapper which calls Rust
#include "heap.h"
#include "kernel.h"
#include "string.h"
#include "pkg.h"
#include "net.h"
#include "serial.h"

#define SHELL_INPUT_MAX 128
#define SHELL_HISTORY 8

static char input_buf[SHELL_INPUT_MAX];
static int input_len = 0;
static char history[SHELL_HISTORY][SHELL_INPUT_MAX];
static int hist_count = 0, hist_pos = 0;
static vfs_node_t* cwd = 0; // This will now be a C-side vfs_node_t representing the current directory

// Fallback root node for when vfs_get_root() returns NULL
static vfs_node_t fallback_root = {
    .used = 1,
    .node_type = VFS_TYPE_DIR,
    .name = "/",
    .size = 0,
    .parent = NULL,
    .child = NULL,
    .sibling = NULL
};

// Replace print_ptr with a simple hex print
static void print_hex_digit(unsigned char d) {
    if (d < 10) vga_putchar('0' + d), serial_write((char[]){'0' + d, 0});
    else vga_putchar('A' + (d - 10)), serial_write((char[]){'A' + (d - 10), 0});
}

static void print_hex_ptr(const char* label, void* ptr) {
    vga_print(label);
    serial_write(label);
    vga_print(": 0x");
    serial_write(": 0x");
    unsigned long val = (unsigned long)ptr;
    for (int i = (sizeof(unsigned long) * 2) - 1; i >= 0; i--) {
        unsigned char d = (val >> (i * 4)) & 0xF;
        char c = (d < 10) ? ('0' + d) : ('A' + (d - 10));
        vga_putchar(c);
        serial_write((char[]){c, 0});
    }
    vga_print("\n");
    serial_write("\n");
}

static void shell_prompt() {
    vga_set_color(0x0A);
    // REMOVED DEBUG PRINTS: print_hex_ptr("[DEBUG] shell_prompt: cwd", cwd);
    if (!cwd) {
        vga_print("[ERR: cwd NULL] ");
        vga_set_color(0x0F);
        return;
    }
    // REMOVED DEBUG PRINTS: vga_print("[DEBUG] cwd->name bytes: ");
    // REMOVED DEBUG PRINTS: serial_write("[DEBUG] cwd->name bytes: ");
    // REMOVED DEBUG PRINTS: for (int i = 0; i < 32; i++) {
    // REMOVED DEBUG PRINTS:     unsigned char c = ((unsigned char*)cwd->name)[i];
    // REMOVED DEBUG PRINTS:     char hex[3] = {"0123456789ABCDEF"[c >> 4], "0123456789ABCDEF"[c & 0xF], 0};
    // REMOVED DEBUG PRINTS:     vga_print(hex);
    // REMOVED DEBUG PRINTS:     serial_write(hex);
    // REMOVED DEBUG PRINTS:     vga_putchar(' ');
    // REMOVED DEBUG PRINTS:     serial_write(" ");
    // REMOVED DEBUG PRINTS: }
    // REMOVED DEBUG PRINTS: vga_print("\n");
    // REMOVED DEBUG PRINTS: serial_write("\n");
    vga_print(cwd->name); // Show current directory name
    vga_print(" > ");
    vga_set_color(0x0F);
}

static void shell_clear() {
    vga_clear();
}

static void shell_help() {
    vga_print("Built-in commands:\n");
    vga_print("  help    - Show this help\n");
    vga_print("  clear   - Clear the screen\n");
    vga_print("  ls      - List files\n");
    vga_print("  cat     - Print file contents\n");
    vga_print("  mkdir   - Create directory\n");
    vga_print("  cd      - Change directory\n");
    vga_print("  echo    - Print text\n");
    vga_print("  pkg     - Package manager (install, remove, list, info)\n");
    vga_print("  wget    - Download file via UDP\n");
    vga_print("  touch   - Create empty file\n");
    vga_print("  rm      - Remove file/directory\n");
    vga_print("  stat    - Get file/directory info\n");
}

static void shell_ls(const char* arg) {
    // REMOVED DEBUG PRINTS: print_hex_ptr("[DEBUG] shell_ls: cwd", cwd);
    if (!cwd) { vga_print("[ERR: cwd NULL]\n"); return; }
    char full_path[256];
    if (!arg || !*arg || strcmp(arg, ".") == 0) {
        // List current directory
        snprintf(full_path, sizeof(full_path), "%s", cwd->name);
    } else if (strcmp(arg, "..") == 0) {
        // List parent directory
        if (cwd->parent) {
            snprintf(full_path, sizeof(full_path), "%s", cwd->parent->name);
        } else {
            snprintf(full_path, sizeof(full_path), "/"); // Root's parent is root
        }
    } else {
        // List specified path (relative to cwd or absolute)
        if (arg[0] == '/') {
            snprintf(full_path, sizeof(full_path), "%s", arg);
        } else {
            snprintf(full_path, sizeof(full_path), "%s%s%s", cwd->name, (strcmp(cwd->name, "/") == 0) ? "" : "/", arg);
        }
    }
    
    // Call Rust VFS ls
    vga_print(full_path);
    vga_print(":\n");
    int result = rust_vfs_ls(full_path);
    if (result != 0) {
        vga_print("ls: Failed to list directory.\n");
    }
}

static void shell_cat(const char* arg) {
    if (!arg || !*arg) {
        vga_print("cat: missing operand\n");
        return;
    }
    
    char full_path[256];
    if (arg[0] == '/') {
        snprintf(full_path, sizeof(full_path), "%s", arg);
    } else {
        snprintf(full_path, sizeof(full_path), "%s%s%s", cwd->name, (strcmp(cwd->name, "/") == 0) ? "" : "/", arg);
    }

    char buf[256]; // Read in chunks
    int bytes_read;
    
    // Delegate reading to Rust VFS
    bytes_read = rust_vfs_read(full_path, buf, sizeof(buf) - 1);

    if (bytes_read > 0) {
        buf[bytes_read] = '\0';
        vga_print(buf);
    } else if (bytes_read == 0) {
        vga_print("cat: File is empty or not found.\n");
    } else {
        vga_print("cat: Failed to read file.\n");
    }
    vga_print("\n");
}

static void shell_echo(const char* arg) {
    vga_print(arg);
    vga_print("\n");
}

static void shell_mkdir(const char* arg) {
    if (!arg || !*arg) {
        vga_print("mkdir: missing operand\n");
        return;
    }
    char full_path[256];
    if (arg[0] == '/') {
        snprintf(full_path, sizeof(full_path), "%s", arg);
    } else {
        snprintf(full_path, sizeof(full_path), "%s%s%s", cwd->name, (strcmp(cwd->name, "/") == 0) ? "" : "/", arg);
    }

    if (rust_vfs_mkdir(full_path) == 0) {
        vga_print("mkdir: created directory\n");
        // For now, we don't dynamically update the C-side vfs_node_t tree for new directories
        // This means 'cd' to newly created directories won't work until reboot or a more complex C-side VFS cache.
        // For a full solution, vfs_find would need to query Rust VFS.
    } else {
        vga_print("mkdir: failed to create directory\n");
    }
}

static void shell_touch(const char* arg) {
    if (!arg || !*arg) {
        vga_print("touch: missing operand\n");
        return;
    }
    char full_path[256];
    if (arg[0] == '/') {
        snprintf(full_path, sizeof(full_path), "%s", arg);
    } else {
        snprintf(full_path, sizeof(full_path), "%s%s%s", cwd->name, (strcmp(cwd->name, "/") == 0) ? "" : "/", arg);
    }
    int res = rust_vfs_create_file(full_path);
    if (res == 0) vga_print("touch: file created\n");
    else if (res == -17) vga_print("touch: file exists\n");
    else if (res == -28) vga_print("touch: no space\n");
    else vga_print("touch: failed\n");
}

static void shell_rm(const char* arg) {
    if (!arg || !*arg) {
        vga_print("rm: missing operand\n");
        return;
    }
    char full_path[256];
    if (arg[0] == '/') {
        snprintf(full_path, sizeof(full_path), "%s", arg);
    } else {
        snprintf(full_path, sizeof(full_path), "%s%s%s", cwd->name, (strcmp(cwd->name, "/") == 0) ? "" : "/", arg);
    }
    int res = rust_vfs_unlink(full_path);
    if (res == 0) vga_print("rm: deleted\n");
    else if (res == -2) vga_print("rm: not found\n");
    else vga_print("rm: failed\n");
}

static void shell_stat(const char* arg) {
    // REMOVED DEBUG PRINTS: print_hex_ptr("[DEBUG] shell_stat: cwd", cwd);
    if (!cwd) { vga_print("[ERR: cwd NULL]\n"); return; }
    if (!arg || !*arg) {
        vga_print("stat: missing operand\n");
        return;
    }
    char full_path[256];
    if (arg[0] == '/') {
        snprintf(full_path, sizeof(full_path), "%s", arg);
    } else {
        snprintf(full_path, sizeof(full_path), "%s%s%s", cwd->name, (strcmp(cwd->name, "/") == 0) ? "" : "/", arg);
    }
    vfs_node_t statbuf;
    int res = rust_vfs_stat(full_path, &statbuf);
    if (res == 0) {
        vga_print("stat: name="); vga_print(statbuf.name); vga_print(", type=");
        if (statbuf.node_type == 1) vga_print("dir\n");
        else if (statbuf.node_type == 2) vga_print("file\n");
        else vga_print("unknown\n");
    } else if (res == -2) vga_print("stat: not found\n");
    else vga_print("stat: failed\n");
}

// Update shell_cd to use rust_vfs_stat for directory check
static void shell_cd(const char* arg) {
    // REMOVED DEBUG PRINTS: print_hex_ptr("[DEBUG] shell_cd: cwd", cwd);
    if (!cwd) { vga_print("[ERR: cwd NULL]\n"); return; }
    if (!arg || !*arg) {
        cwd = vfs_get_root();
        vga_print("cd: changed to root directory\n");
        return;
    }
    char full_path[256];
    if (arg[0] == '/') {
        snprintf(full_path, sizeof(full_path), "%s", arg);
    } else {
        snprintf(full_path, sizeof(full_path), "%s%s%s", cwd->name, (strcmp(cwd->name, "/") == 0) ? "" : "/", arg);
    }
    vfs_node_t statbuf;
    int res = rust_vfs_stat(full_path, &statbuf);
    if (res == 0 && statbuf.node_type == VFS_TYPE_DIR) {
        // For now, we can't dynamically get a C-side pointer to the Rust node
        // So, we'll just update the CWD name for display purposes.
        // A proper solution would involve Rust returning a stable ID or pointer.
        // For simplicity, we'll just update the name in the CWD struct.
        // This is a temporary workaround until a more robust C-Rust VFS integration.
        strncpy(cwd->name, full_path, sizeof(cwd->name) - 1);
        cwd->name[sizeof(cwd->name) - 1] = '\0';
        vga_print("cd: changed directory to ");
        vga_print(cwd->name);
        vga_print("\n");
    } else if (res == 0) {
        vga_print("cd: not a directory\n");
    } else {
        vga_print("cd: not found\n");
    }
}

static void shell_pkg(const char* arg) {
    if (!arg || !*arg) {
        vga_print("Usage: pkg <install|remove|list|info> ...\n");
        return;
    }
    char cmd[16] = {0};
    char name[32] = {0};
    int i = 0, j = 0;
    while (arg[i] && arg[i] != ' ') { cmd[j++] = arg[i++]; }
    cmd[j] = 0;
    while (arg[i] == ' ') i++;
    j = 0;
    while (arg[i] && j < 31) name[j++] = arg[i++];
    name[j] = 0;
    if (!strcmp(cmd, "install")) {
        if (!name[0]) { vga_print("pkg: install needs a name\n"); return; }
        // Simulate install with static string, writing to Rust VFS
        if (rust_vfs_write(name, "This is a demo package.", 23) == 23) // Assuming 23 bytes written
            vga_print("pkg: installed (via Rust VFS)\n");
        else
            vga_print("pkg: install failed\n");
    } else if (!strcmp(cmd, "remove")) {
        // pkg_remove is not yet implemented in Rust VFS
        vga_print("pkg: remove not yet implemented for Rust VFS.\n");
    } else if (!strcmp(cmd, "list")) {
        // pkg_list will now call shell_ls on the packages directory
        vga_print("pkg list: calling shell_ls with /pkgs\n");
        shell_ls("/pkgs"); // Assuming packages are in /pkgs
    } else if (!strcmp(cmd, "info")) {
        if (!name[0]) { vga_print("pkg: info needs a name\n"); return; }
        // pkg_info will now call shell_cat on the package file
        char pkg_path[64];
        snprintf(pkg_path, sizeof(pkg_path), "/pkgs/%s", name);
        shell_cat(pkg_path);
    } else {
        vga_print("pkg: unknown subcommand\n");
    }
}

static void shell_wget(const char* arg) {
    if (!arg || !*arg) {
        vga_print("Usage: wget <ip> <port> <filename>\n");
        return;
    }
    char ipstr[16] = {0}, portstr[8] = {0}, fname[32] = {0};
    int i = 0, j = 0;
    while (arg[i] && arg[i] != ' ') ipstr[j++] = arg[i++];
    ipstr[j] = 0;
    while (arg[i] == ' ') i++;
    j = 0;
    while (arg[i] && arg[i] != ' ') portstr[j++] = arg[i++];
    portstr[j] = 0;
    while (arg[i] == ' ') i++;
    j = 0;
    while (arg[i] && j < 31) fname[j++] = arg[i++];
    fname[j] = 0;
    if (!ipstr[0] || !portstr[0] || !fname[0]) {
        vga_print("wget: missing argument\n");
        return;
    }
    struct ip_addr ip = {0};
    int ip1, ip2, ip3, ip4;
    if (sscanf(ipstr, "%d.%d.%d.%d", &ip1, &ip2, &ip3, &ip4) != 4) {
        vga_print("wget: invalid IP\n");
        return;
    }
    ip.addr[0]=ip1; ip.addr[1]=ip2; ip.addr[2]=ip3; ip.addr[3]=ip4;
    uint16_t port = 0;
    for (int k = 0; portstr[k]; k++) port = port*10 + (portstr[k]-'0');
    char req[64];
    int reqlen = snprintf(req, sizeof(req), "GET %s", fname);
    udp_send(ip, port, req, reqlen);
    vga_print("wget: waiting for response...\n");
    char buf[1024] = {0};
    int n = 0;
    for (int tries = 0; tries < 100000; tries++) {
        n = udp_poll_recv(0, 0, buf, sizeof(buf)-1);
        if (n > 0) break;
    }
    if (n <= 0) {
        vga_print("wget: no response\n");
        return;
    }
    
    // Use Rust VFS to write the downloaded file
    char full_fname[256];
    snprintf(full_fname, sizeof(full_fname), "%s%s%s", cwd->name, (strcmp(cwd->name, "/") == 0) ? "" : "/", fname);

    if (rust_vfs_write(full_fname, buf, n) == n) {
        vga_print("wget: file downloaded and written via Rust VFS\n");
    } else {
        vga_print("wget: failed to write file via Rust VFS\n");
    }
}

static void shell_exec(const char* cmd, const char* arg) {
    serial_write("[SHELL] new command entered : ");
    serial_write(cmd);
    serial_write("\n");
    if (!strcmp(cmd, "help")) shell_help();
    else if (!strcmp(cmd, "clear")) shell_clear();
    else if (!strcmp(cmd, "ls")) shell_ls(arg);
    else if (!strcmp(cmd, "cat")) shell_cat(arg);
    else if (!strcmp(cmd, "echo")) shell_echo(arg);
    else if (!strcmp(cmd, "mkdir")) shell_mkdir(arg);
    else if (!strcmp(cmd, "cd")) shell_cd(arg);
    else if (!strcmp(cmd, "touch")) shell_touch(arg);
    else if (!strcmp(cmd, "rm")) shell_rm(arg);
    else if (!strcmp(cmd, "stat")) shell_stat(arg);
    else if (!strcmp(cmd, "pkg") || !strcmp(cmd, "pacman") || !strcmp(cmd, "apt-get")) shell_pkg(arg);
    else if (!strcmp(cmd, "wget")) shell_wget(arg);
    else vga_print("Unknown command. Type 'help'.\n");
}

void shell_init() {
    vga_clear();
    input_len = 0;
    hist_count = 0;
    hist_pos = 0;
    vga_print("[SHELL] About to call rust_vfs_init...\n");
    serial_write("[SHELL] About to call rust_vfs_init...\n");
    rust_vfs_init(); // Use Rust VFS instead
    vga_print("[SHELL] rust_vfs_init completed\n");
    serial_write("[SHELL] rust_vfs_init completed\n");
    vga_print("[SHELL] About to call vfs_get_root...\n");
    serial_write("[SHELL] About to call vfs_get_root...\n");
    cwd = vfs_get_root(); // Set CWD to root (C-side representation)
    vga_print("[SHELL] vfs_get_root completed\n");
    serial_write("[SHELL] vfs_get_root completed\n");
    if (!cwd) cwd = &fallback_root;
    // REMOVED DEBUG PRINTS: print_hex_ptr("[DEBUG] shell_init: cwd", cwd);
    vga_print("[SHELL] Shell initialized\n");
    serial_write("[SHELL] Shell initialized\n");
}

void shell_run() {    
    while (1) {
        shell_prompt();
        // REMOVED DEBUG PRINTS: vga_print("[DEBUG] after shell_prompt\n");
        // REMOVED DEBUG PRINTS: serial_write("[DEBUG] after shell_prompt\n");
        input_len = 0;
        memset(input_buf, 0, SHELL_INPUT_MAX);
        
        // Read line
        // REMOVED DEBUG PRINTS: vga_print("[DEBUG] before read input\n");
        // REMOVED DEBUG PRINTS: serial_write("[DEBUG] before read input\n");
        if (!input_buf) {
            vga_print("[ERR: input buffer NULL]\n");
            serial_write("[ERR: input buffer NULL]\n");
            continue;
        }
        while (1) {
            // poll_keyboard_input(); // This should NOT be here
            int c = get_ascii_char(); // This now calls Rust and handles waiting
            if (c == -1) continue; // Should not happen with blocking get_ascii_char
            if (c == '\n' || c == '\r') break;
            if (c == 8 && input_len > 0) { // Backspace
                input_len--;
                vga_print("\b \b");
            } else if (c >= 32 && input_len < SHELL_INPUT_MAX-1) {
                input_buf[input_len++] = c;
                char s[2] = {c, 0};
                vga_print(s);
            }
        }
        vga_print("\n");
        if (input_len == 0) continue;
        input_buf[input_len] = 0; // Ensure null-termination
        // Add to history
        if (hist_count < SHELL_HISTORY) hist_count++;
        for (int i = SHELL_HISTORY-1; i > 0; i--) memcpy(history[i], history[i-1], SHELL_INPUT_MAX);
        memcpy(history[0], input_buf, SHELL_INPUT_MAX);
        // Parse command
        char* cmd = input_buf;
        char* arg = NULL;
        for (int i = 0; input_buf[i]; i++) {
            if (input_buf[i] == ' ') {
                input_buf[i] = 0;
                arg = input_buf + i + 1;
                break;
            }
        }
        // REMOVED DEBUG PRINTS: vga_print("[DEBUG] before exec command\n");
        // REMOVED DEBUG PRINTS: serial_write("[DEBUG] before exec command\n");
        shell_exec(cmd, arg ? arg : "");
        // REMOVED DEBUG PRINTS: vga_print("[DEBUG] after exec command\n");
        // REMOVED DEBUG PRINTS: serial_write("[DEBUG] after exec command\n");
    }
}

// Add debug prints to shell_readline
void shell_readline(char* buf, int maxlen) {
    // REMOVED DEBUG PRINTS: vga_print("[DEBUG] entered shell_readline\n");
    // REMOVED DEBUG PRINTS: serial_write("[DEBUG] entered shell_readline\n");
    // REMOVED DEBUG PRINTS: vga_print("[DEBUG] shell_readline: buf ptr: ");
    // REMOVED DEBUG PRINTS: serial_write("[DEBUG] shell_readline: buf ptr: ");
    // REMOVED DEBUG PRINTS: print_hex_ptr("buf", buf);
    // REMOVED DEBUG PRINTS: vga_print("[DEBUG] shell_readline: maxlen: ");
    // REMOVED DEBUG PRINTS: serial_write("[DEBUG] shell_readline: maxlen: ");
    // REMOVED DEBUG PRINTS: // Print maxlen as hex
    // REMOVED DEBUG PRINTS: for (int i = (sizeof(int) * 2) - 1; i >= 0; i--) {
    // REMOVED DEBUG PRINTS:     unsigned char d = (maxlen >> (i * 4)) & 0xF;
    // REMOVED DEBUG PRINTS:     char c = (d < 10) ? ('0' + d) : ('A' + (d - 10));
    // REMOVED DEBUG PRINTS:     vga_putchar(c);
    // REMOVED DEBUG PRINTS:     serial_write((char[]){c, 0});
    // REMOVED DEBUG PRINTS: }
    // REMOVED DEBUG PRINTS: vga_print("\n");
    // REMOVED DEBUG PRINTS: serial_write("\n");
    if (!buf) {
        vga_print("[ERR: shell_readline buf NULL]\n");
        serial_write("[ERR: shell_readline buf NULL]\n");
        return;
    }
    input_len = 0; // Reset input_len for this specific readline call
    while (1) {
        // poll_keyboard_input(); // This should NOT be here
        int c = get_ascii_char(); // This now calls Rust and handles waiting
        if (c == -1) continue; // Should not happen with blocking get_ascii_char
        if (c == '\n' || c == '\r') break;
        if (c == 8 && input_len > 0) { // Backspace
            input_len--;
            vga_print("\b \b");
        } else if (c >= 32 && input_len < maxlen-1) {
            buf[input_len++] = c;
            char s[2] = {c, 0};
            vga_print(s);
        }
    }
    buf[input_len] = 0; // Ensure null-termination
}
