#include "shell.h"
#include "vga.h"
#include "keyboard.h"
#include "vfs.h"
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
static vfs_node_t* cwd = 0;

static void shell_prompt() {
    vga_set_color(0x0A);
    // Simple prompt when VFS is disabled
    vga_print("shadeos > ");
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
}

static void shell_ls(const char* arg) {
    vga_print("ls: VFS not available\n");
}

static void shell_cat(const char* arg) {
    vga_print("cat: VFS not available\n");
}

static void shell_echo(const char* arg) {
    vga_print(arg);
    vga_print("\n");
}

static void shell_mkdir(const char* arg) {
    vga_print("mkdir: VFS not available\n");
}

static void shell_cd(const char* arg) {
    vga_print("cd: VFS not available\n");
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
        // Simulate install with static string
        if (pkg_install(name, "This is a demo package.", 23) == 0)
            vga_print("pkg: installed\n");
        else
            vga_print("pkg: install failed\n");
    } else if (!strcmp(cmd, "remove")) {
        if (!name[0]) { vga_print("pkg: remove needs a name\n"); return; }
        if (pkg_remove(name) == 0)
            vga_print("pkg: removed\n");
        else
            vga_print("pkg: remove failed\n");
    } else if (!strcmp(cmd, "list")) {
        pkg_list();
    } else if (!strcmp(cmd, "info")) {
        if (!name[0]) { vga_print("pkg: info needs a name\n"); return; }
        pkg_info(name);
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
    vfs_node_t* node = vfs_create(fname, VFS_TYPE_MEM, cwd);
    if (!node) {
        vga_print("wget: failed to create file\n");
        return;
    }
    vfs_write(node, buf, n);
    vga_print("wget: file downloaded\n");
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
    input_len = 0;
    hist_count = 0;
    hist_pos = 0;
    // Temporarily disable VFS to avoid crashes
    cwd = NULL;
    // pkg_init(); // Temporarily disabled
    vga_print("[SHELL] Shell initialized (VFS disabled)\n");
    serial_write("[SHELL] Shell initialized (VFS disabled)\n");
}

void shell_run() {
    shell_clear();
    vga_print("ShadeOS Shell\nType 'help' for commands.\n\n");
    while (1) {
        shell_prompt();
        input_len = 0;
        memset(input_buf, 0, SHELL_INPUT_MAX);
        // Read line
        while (1) {
            int c = keyboard_getchar();
            if (c == -1) {
                // Add a small delay to prevent tight loop
                for (volatile int i = 0; i < 1000; i++);
                continue;
            }
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