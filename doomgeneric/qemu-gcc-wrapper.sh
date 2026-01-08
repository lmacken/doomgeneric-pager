#!/bin/bash
# Wrapper to run OpenWrt SDK gcc through QEMU
# Requires: OPENWRT_SDK environment variable set to SDK path

if [ -z "$OPENWRT_SDK" ]; then
    echo "ERROR: OPENWRT_SDK environment variable not set" >&2
    exit 1
fi

if [ ! -d "$OPENWRT_SDK" ]; then
    echo "ERROR: OPENWRT_SDK path does not exist: $OPENWRT_SDK" >&2
    exit 1
fi
TOOLCHAIN_DIR="$OPENWRT_SDK/staging_dir/toolchain-mipsel_24kc_gcc-11.2.0_musl"
HOST_DIR="$OPENWRT_SDK/staging_dir/host"

export STAGING_DIR="$OPENWRT_SDK/staging_dir"
export PATH="$TOOLCHAIN_DIR/bin:$PATH"

# Use QEMU to run the x86-64 compiler binary directly with proper library paths
exec qemu-x86_64 -L "$HOST_DIR" -E LD_LIBRARY_PATH="$HOST_DIR/lib" "$TOOLCHAIN_DIR/bin/.mipsel-openwrt-linux-musl-gcc.bin" "$@"

