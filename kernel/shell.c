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
    .name = "/",
    .type = VFS_TYPE_DIR,
    .size = 0,
    .pos = 0,
    .used = 1,
    .data = NULL,
    .parent = NULL,
    .child = NULL,
    .sibling = NULL,
    .fat_filename = ""
};

static void shell_prompt() {
    vga_set_color(0x0A);
    vga_print(cwd ? cwd->name : "/"); // Show current directory name
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
}

static void shell_ls(const char* arg) {
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
    vga_print("\n");
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

static void shell_cd(const char* arg) {
    if (!arg || !*arg) {
        cwd = vfs_get_root();
        vga_print("cd: changed to root directory\n");
        return;
    }
    
    // For 'cd', we need to find the *C-side* node to update `cwd`.
    // This means `vfs_find` needs to be able to locate nodes that exist in Rust VFS.
    // For simplicity, we'll only allow `cd` to directories that are already
    // represented in the C-side `nodes` array (e.g., the root, or those created
    // via `vfs_create` if it were to track all Rust-created dirs).
    // A more robust solution would involve `rust_vfs_stat` or similar to check if path is a directory.
    
    vfs_node_t* target_dir = vfs_find(arg, cwd); // This still uses the C-side lookup
    if (!target_dir || target_dir->type != VFS_TYPE_DIR) {
        vga_print("cd: No such directory or not a directory (C-side lookup only).\n");
        return;
    }
    cwd = target_dir;
    vga_print("cd: changed directory\n");
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
    if (!strcmp(cmd, "help")) shell_help();
    else if (!strcmp(cmd, "clear")) shell_clear();
    else if (!strcmp(cmd, "ls")) shell_ls(arg);
    else if (!strcmp(cmd, "cat")) shell_cat(arg);
    else if (!strcmp(cmd, "echo")) shell_echo(arg);
    else if (!strcmp(cmd, "mkdir")) shell_mkdir(arg);
    else if (!strcmp(cmd, "cd")) shell_cd(arg);
    else if (!strcmp(cmd, "pkg") || !strcmp(cmd, "pacman") || !strcmp(cmd, "apt-get")) shell_pkg(arg);
    else if (!strcmp(cmd, "wget")) shell_wget(arg);
    else vga_print("Unknown command. Type 'help'.\n");
}

void shell_init() {
    vga_clear();
    input_len = 0;
    hist_count = 0;
    hist_pos = 0;
    // vfs_init(); // Initialize the C-side VFS wrapper
    rust_vfs_init(); // Use Rust VFS instead
    cwd = vfs_get_root(); // Set CWD to root (C-side representation)
    if (!cwd) cwd = &fallback_root;
    // pkg_init(); // Package manager init is now handled by shell_pkg directly
    vga_print("[SHELL] Shell initialized\n");
    serial_write("[SHELL] Shell initialized\n");
}

void shell_run() {
    vga_print("shell_run: Starting shell loop\n");
    
    while (1) {
        shell_prompt();
        input_len = 0;
        memset(input_buf, 0, SHELL_INPUT_MAX);
        
        // Read line
        while (1) {
            poll_keyboard_input();
            int c = get_ascii_char();
            if (c == -1) continue;
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
        shell_exec(cmd, arg ? arg : "");
    }
}
