# Build Instructions

This document provides detailed instructions for building P2P0 on different platforms.

## Table of Contents
- [Prerequisites](#prerequisites)
- [Linux](#linux)
- [macOS](#macos)
- [Windows](#windows)
- [Cross-compilation](#cross-compilation)
- [Troubleshooting](#troubleshooting)

---

## Prerequisites

### General Requirements
- C99-compliant compiler (GCC 4.0+, Clang 3.0+, MSVC 2015+)
- Make utility (GNU Make, BSD Make, or nmake)
- Git (for cloning the repository)

### No External Dependencies
P2P0 has **zero external dependencies**. It only requires:
- Standard C library
- System socket API (POSIX sockets on Unix, Winsock2 on Windows)

---

## Linux

### Install Build Tools

**Debian/Ubuntu:**
```bash
sudo apt-get update
sudo apt-get install build-essential
```

**Fedora/RHEL/CentOS:**
```bash
sudo yum groupinstall "Development Tools"
# or on newer versions:
sudo dnf groupinstall "Development Tools"
```

**Arch Linux:**
```bash
sudo pacman -S base-devel
```

### Build

```bash
cd p2p0
make
```

### Build Output
```
build/libp2p0.a              # Static library
bin/server_simple            # SIMPLE signaling server
bin/server_ice_relay         # ICE-RELAY signaling server
bin/client_simple            # SIMPLE client example
bin/client_ice_relay         # ICE-RELAY client example
bin/client_pubsub            # PUBSUB client example
```

### Install (Optional)

```bash
# Install library
sudo cp build/libp2p0.a /usr/local/lib/

# Install headers
sudo mkdir -p /usr/local/include/p2p0
sudo cp include/*.h /usr/local/include/p2p0/

# Install binaries
sudo cp bin/* /usr/local/bin/
```

---

## macOS

### Install Build Tools

Install Xcode Command Line Tools:
```bash
xcode-select --install
```

Or install via Homebrew:
```bash
brew install gcc make
```

### Build

```bash
cd p2p0
make
```

The build process is identical to Linux.

### macOS-specific Notes
- Default compiler is Clang (works perfectly)
- GCC can also be used if installed via Homebrew
- No additional dependencies needed

---

## Windows

### Option 1: MinGW/MSYS2 (Recommended)

#### Install MSYS2
1. Download from https://www.msys2.org/
2. Install and update:
```bash
pacman -Syu
pacman -S mingw-w64-x86_64-gcc make
```

#### Build
```bash
cd p2p0
make
```

### Option 2: Visual Studio (MSVC)

#### Prerequisites
- Visual Studio 2015 or later
- Windows SDK

#### Build with nmake
Create `Makefile.nmake`:
```makefile
CC = cl
CFLAGS = /W4 /std:c11 /I.\include
LDFLAGS = ws2_32.lib

all:
    $(CC) $(CFLAGS) /c src\p2p0.c /Fo:build\
    $(CC) $(CFLAGS) /c src\p2p0_simple.c /Fo:build\
    $(CC) $(CFLAGS) /c src\p2p0_ice_relay.c /Fo:build\
    $(CC) $(CFLAGS) /c src\p2p0_pubsub.c /Fo:build\
    lib /OUT:build\p2p0.lib build\*.obj
    $(CC) $(CFLAGS) examples\client_simple.c build\p2p0.lib $(LDFLAGS) /Fe:bin\client_simple.exe
```

Then build:
```cmd
nmake /f Makefile.nmake
```

### Option 3: Windows Subsystem for Linux (WSL)

```bash
# In WSL terminal
cd p2p0
make
```

### Windows-specific Notes
- Winsock2 library (ws2_32.lib) is automatically linked
- Executables have `.exe` extension
- Use `nmake` instead of `make` with MSVC

---

## Cross-compilation

### ARM Linux (e.g., Raspberry Pi)

```bash
# Install ARM toolchain
sudo apt-get install gcc-arm-linux-gnueabihf

# Build
make CC=arm-linux-gnueabihf-gcc
```

### Android (using NDK)

```bash
# Set NDK path
export NDK=/path/to/android-ndk

# Build for ARM64
make CC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android21-clang
```

---

## Build Configuration

### Debug Build

```bash
make CFLAGS="-g -O0 -DDEBUG"
```

### Release Build (Optimized)

```bash
make CFLAGS="-O3 -DNDEBUG"
```

### Static Analysis

```bash
# With GCC
make CFLAGS="-Wall -Wextra -Werror -pedantic"

# With Clang static analyzer
scan-build make
```

### Address Sanitizer (Debug Memory Issues)

```bash
make CFLAGS="-fsanitize=address -g" LDFLAGS="-fsanitize=address"
```

---

## Makefile Targets

| Target | Description |
|--------|-------------|
| `all` | Build library and all examples (default) |
| `clean` | Remove all build artifacts |
| `test` | Build and run basic tests |
| `help` | Show help message with usage examples |

### Examples

```bash
# Build everything
make

# Clean build directory
make clean

# Build with verbose output
make V=1

# Parallel build (faster)
make -j4
```

---

## Troubleshooting

### Issue: "make: command not found"

**Solution:**
- Linux: Install build-essential
- macOS: Install Xcode Command Line Tools
- Windows: Use MSYS2 or WSL

### Issue: "socket.h: No such file or directory"

**Solution:**
- Linux: Install libc development files
  ```bash
  sudo apt-get install libc6-dev
  ```
- Ensure you're using a proper C99 compiler

### Issue: "undefined reference to `socket`"

**Solution:**
- On some systems, you may need to link against additional libraries:
  ```bash
  make LDFLAGS="-lsocket -lnsl"
  ```

### Issue: Windows compilation errors with MSVC

**Solution:**
- Make sure you're using C11 mode: `/std:c11`
- Ensure Windows SDK is installed
- Use proper escape characters in nmake files

### Issue: Permission denied when running servers

**Solution:**
- Don't run on privileged ports (< 1024) without sudo
- Or use higher port numbers (e.g., 9000+)

### Issue: "Address already in use"

**Solution:**
- Another process is using the port
- Wait a moment for port to be released
- Or use a different port:
  ```bash
  ./bin/server_simple 9999
  ```

---

## Verification

After building, verify the installation:

```bash
# Check library
file build/libp2p0.a

# Check executables
ls -lh bin/

# Run simple test
./bin/server_simple &
./bin/client_simple listen peer1
```

---

## IDE Integration

### Visual Studio Code

Create `.vscode/tasks.json`:
```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "build",
            "type": "shell",
            "command": "make",
            "group": {
                "kind": "build",
                "isDefault": true
            }
        }
    ]
}
```

### CLion

CLion automatically detects Makefile projects. Just open the directory.

### Vim/Neovim

```vim
:make
```

---

## Performance Optimization

### Compiler Optimization Flags

```bash
# Level 2 optimization (balanced)
make CFLAGS="-O2"

# Level 3 optimization (aggressive)
make CFLAGS="-O3"

# Size optimization
make CFLAGS="-Os"

# Link-time optimization
make CFLAGS="-O3 -flto" LDFLAGS="-flto"
```

### Profile-Guided Optimization (PGO)

```bash
# Step 1: Build with profiling
make CFLAGS="-fprofile-generate"

# Step 2: Run representative workload
./bin/client_simple listen peer1

# Step 3: Rebuild with profile data
make clean
make CFLAGS="-fprofile-use"
```

---

## Next Steps

After building successfully:
1. Read the [API documentation](API.md)
2. Study the [protocol specifications](PROTOCOLS.md)
3. Run the example programs
4. Build your own P2P application!

---

## Getting Help

If you encounter issues not covered here:
1. Check existing GitHub Issues
2. Create a new issue with:
   - Your OS and version
   - Compiler version
   - Complete error messages
   - Build command used
