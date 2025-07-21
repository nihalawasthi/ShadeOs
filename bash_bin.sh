# create_bash_binary.sh - Create a simple bash binary for testing

echo "Creating a simple bash binary for ShadeOS..."

# Create a minimal bash-like binary
cat > bash_stub.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    printf("ShadeOS Bash v1.0\n");
    printf("This is a minimal bash implementation.\n");
    
    char input[256];
    while (1) {
        printf("bash-1.0$ ");
        fflush(stdout);
        
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }
        
        // Remove newline
        input[strcspn(input, "\n")] = 0;
        
        if (strcmp(input, "exit") == 0) {
            break;
        } else if (strcmp(input, "help") == 0) {
            printf("Available commands: help, exit, echo\n");
        } else if (strncmp(input, "echo ", 5) == 0) {
            printf("%s\n", input + 5);
        } else if (strlen(input) > 0) {
            printf("bash: %s: command not found\n", input);
        }
    }
    
    return 0;
}
EOF

# Compile it (if we have a cross-compiler)
if command -v x86_64-elf-gcc &> /dev/null; then
    echo "Compiling bash stub with cross-compiler..."
    x86_64-elf-gcc -static -o bash_binary bash_stub.c
    echo "Bash binary created: bash_binary"
else
    echo "Cross-compiler not available. Using the Rust implementation instead."
fi

# Clean up
rm -f bash_stub.c

echo "Done!"
