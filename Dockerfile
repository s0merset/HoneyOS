# ============================================================
# HoneyOS Build Environment - Dockerfile
# CMSC 125 — Operating Systems, UP Cebu
#
# Creates a reproducible Linux build environment with all the
# cross-compilation tools needed to build HoneyOS.
#
# Why Docker?
#   macOS and Windows host linkers do not support the flags needed
#   to produce a 32-bit freestanding ELF kernel. Running the build
#   inside an Ubuntu container avoids host toolchain incompatibilities.
#
# Tools installed:
#   nasm          — Assembler for boot/boot.asm
#   gcc           — C compiler for kernel source files
#   gcc-multilib  — 32-bit headers/libraries needed for -m32 compilation
#   make          — Runs the Makefile build rules
#   grub-pc-bin   — GRUB i386-pc binaries required by grub-mkrescue
#   grub-common   — Shared GRUB utilities (grub-mkrescue depends on this)
#   xorriso       — ISO 9660 image writer used internally by grub-mkrescue
#   mtools        — FAT filesystem tools (mformat, mdir) for honeyfs.img setup
# ============================================================

# Use Ubuntu 22.04 LTS as the base — stable and well-supported toolchain
FROM ubuntu:22.04

# Suppress interactive prompts during apt-get (e.g., timezone selection)
ENV DEBIAN_FRONTEND=noninteractive

# Install all build dependencies in a single RUN layer to minimize image size
RUN apt-get update && apt-get install -y \
    nasm \
    gcc \
    gcc-multilib \
    make \
    grub-pc-bin \
    grub-common \
    xorriso \
    mtools \
    && rm -rf /var/lib/apt/lists/*    # Remove package cache to keep the image lean

# Set the working directory — all project files will be relative to this path
WORKDIR /honeyos

# Copy the entire project into the container image
COPY . .

# Build the ISO during image creation so the artifact is ready immediately
RUN make honeyos.iso

# Default command when running the container interactively
CMD ["bash"]
