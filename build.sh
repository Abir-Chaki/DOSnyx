#!/usr/bin/env bash
set -e

# ========= CONFIG =========
BINUTILS_VERSION=2.41
GCC_VERSION=13.2.0
TARGET=x86_64-elf
PREFIX="$HOME/cross"
SRC_DIR="$HOME/src"
CORES=2
# ==========================

echo "=========================================="
echo " DOSnyx Cross Compiler Build Script"
echo " Target: $TARGET"
echo " Prefix: $PREFIX"
echo "=========================================="
echo ""

# ==========================
# Install Dependencies
# ==========================

echo "==> Installing required packages..."
sudo apt update
sudo apt install -y \
    build-essential \
    bison \
    flex \
    libgmp-dev \
    libmpc-dev \
    libmpfr-dev \
    texinfo \
    wget

# ==========================
# Prepare Directories
# ==========================

mkdir -p "$SRC_DIR"
mkdir -p "$PREFIX"
cd "$SRC_DIR"

# ==========================
# Download Binutils
# ==========================

echo "==> Downloading Binutils..."

if [ ! -f "binutils-${BINUTILS_VERSION}.tar.gz" ]; then
    wget -4 -c https://mirrors.kernel.org/gnu/binutils/binutils-${BINUTILS_VERSION}.tar.gz
fi

if [ ! -d "binutils-${BINUTILS_VERSION}" ]; then
    tar -xf binutils-${BINUTILS_VERSION}.tar.gz
fi

# ==========================
# Build Binutils
# ==========================

if [ ! -f "$PREFIX/bin/${TARGET}-ld" ]; then
    echo "==> Building Binutils..."
    mkdir -p build-binutils
    cd build-binutils

    ../binutils-${BINUTILS_VERSION}/configure \
        --target=$TARGET \
        --prefix="$PREFIX" \
        --with-sysroot \
        --disable-nls \
        --disable-werror \
        --disable-multilib

    make -j$CORES
    make install

    cd "$SRC_DIR"
else
    echo "==> Binutils already built. Skipping."
fi

# ==========================
# Download GCC
# ==========================

echo "==> Downloading GCC..."

if [ ! -f "gcc-${GCC_VERSION}.tar.gz" ]; then
    wget -4 -c https://mirrors.kernel.org/gnu/gcc/gcc-${GCC_VERSION}/gcc-${GCC_VERSION}.tar.gz
fi

if [ ! -d "gcc-${GCC_VERSION}" ]; then
    tar -xf gcc-${GCC_VERSION}.tar.gz
fi

# Download GCC prerequisites (gmp, mpfr, mpc)
cd gcc-${GCC_VERSION}
./contrib/download_prerequisites
cd "$SRC_DIR"

# ==========================
# Build GCC
# ==========================

if [ ! -f "$PREFIX/bin/${TARGET}-gcc" ]; then
    echo "==> Building GCC (this will take a while)..."

    mkdir -p build-gcc
    cd build-gcc

    ../gcc-${GCC_VERSION}/configure \
        --target=$TARGET \
        --prefix="$PREFIX" \
        --disable-nls \
        --enable-languages=c,c++ \
        --without-headers \
        --disable-multilib

    make all-gcc -j$CORES
    make all-target-libgcc -j$CORES

    make install-gcc
    make install-target-libgcc

    cd "$SRC_DIR"
else
    echo "==> GCC already built. Skipping."
fi

# ==========================
# Add to PATH
# ==========================

if ! grep -q "$PREFIX/bin" ~/.bashrc; then
    echo "export PATH=\"$PREFIX/bin:\$PATH\"" >> ~/.bashrc
fi

export PATH="$PREFIX/bin:$PATH"

echo ""
echo "=========================================="
echo " Cross compiler successfully built!"
echo "=========================================="
echo ""
echo "Test it with:"
echo "    x86_64-elf-gcc -v"
echo ""