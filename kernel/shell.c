#include "icmp.h"
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

// Forward declarations for Rust bash functions
extern void rust_bash_init();
extern void rust_bash_run();
extern int rust_bash_execute(const char* command);

#define SHELL_INPUT_MAX 128
#define SHELL_HISTORY 8

static char input_buf[SHELL_INPUT_MAX];
static int input_len = 0;
static char history[SHELL_HISTORY][SHELL_INPUT_MAX];
static int hist_count = 0, hist_pos = 0;
static vfs_node_t* cwd = 0;

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

void shell_init() {
    vga_clear();
    input_len = 0;
    hist_count = 0;
    hist_pos = 0;
    
    vga_print("[SHELL] Initializing Rust VFS...\n");
    serial_write("[SHELL] Initializing Rust VFS...\n");
    rust_vfs_init();
    
    vga_print("[SHELL] Getting VFS root...\n");
    serial_write("[SHELL] Getting VFS root...\n");
    cwd = vfs_get_root();
    if (!cwd) cwd = &fallback_root;
    
    vga_print("[SHELL] Initializing Bash shell...\n");
    serial_write("[SHELL] Initializing Bash shell...\n");
    rust_bash_init();
    
    vga_print("[SHELL] Shell initialization complete\n");
    serial_write("[SHELL] Shell initialization complete\n");
}

void shell_run() {
    vga_print("Starting ShadeOS Bash-compatible Shell...\n\n");
    serial_write("Starting ShadeOS Bash-compatible Shell...\n\n");
    
    // Run the Rust bash implementation
    rust_bash_run();
}

// Legacy shell functions for compatibility
static void shell_prompt() {
    vga_set_color(0x0A);
    if (!cwd) {
        vga_print("[ERR: cwd NULL] ");
        vga_set_color(0x0F);
        return;
    }
    vga_print(cwd->name);
    vga_print(" > ");
    vga_set_color(0x0F);
}

static void shell_clear() {
    vga_clear();
}

static void shell_help() {
    vga_print("ShadeOS Shell - Now using Bash-compatible implementation!\n");
    vga_print("Type 'help' in the bash shell for available commands.\n");
}


void shell_readline(char* buf, int maxlen) {
    if (!buf) {
        vga_print("[ERR: shell_readline buf NULL]\n");
        serial_write("[ERR: shell_readline buf NULL]\n");
        return;
    }
    
    input_len = 0;
    while (1) {
        int c = get_ascii_char();
        if (c == -1) continue;
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
    buf[input_len] = 0;
}
