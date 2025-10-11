#!/bin/bash
# SPDX-FileCopyrightText: 2025 citron Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

# PGO Build Script for Citron (Linux/macOS)
# This script automates the Profile-Guided Optimization build process

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
PGO_PROFILES_DIR="${BUILD_DIR}/pgo-profiles"
BACKUP_PROFILES_DIR="${SCRIPT_DIR}/pgo-profiles-backup"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_header() {
    echo -e "${BLUE}=================================================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}=================================================================${NC}"
}

print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

show_usage() {
    echo "Usage: $0 [STAGE]"
    echo ""
    echo "STAGE can be:"
    echo "  generate  - Build with PGO instrumentation (Stage 1)"
    echo "  use       - Build using PGO profile data (Stage 2)"
    echo "  clean     - Clean build directory but preserve profiles"
    echo ""
    echo "Example:"
    echo "  $0 generate    # Build instrumented version"
    echo "  # Run citron to collect profiles"
    echo "  $0 use         # Build optimized version"
    echo ""
    echo "Options:"
    echo "  -j N      Number of parallel jobs (default: auto-detect)"
    echo "  -lto      Enable Link-Time Optimization"
    echo "  -h        Show this help message"
}

# Parse arguments
STAGE=""
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo "4")
ENABLE_LTO="OFF"

while [[ $# -gt 0 ]]; do
    case $1 in
        generate|use|clean)
            STAGE="$1"
            shift
            ;;
        -j)
            JOBS="$2"
            shift 2
            ;;
        -lto)
            ENABLE_LTO="ON"
            shift
            ;;
        -h|--help)
            show_usage
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            show_usage
            exit 1
            ;;
    esac
done

if [ -z "$STAGE" ]; then
    print_error "No stage specified"
    show_usage
    exit 1
fi

# Clean stage
if [ "$STAGE" == "clean" ]; then
    print_header "Cleaning Build Directory"
    
    if [ -d "$PGO_PROFILES_DIR" ]; then
        print_info "Backing up PGO profiles..."
        mkdir -p "$BACKUP_PROFILES_DIR"
        cp -r "$PGO_PROFILES_DIR"/* "$BACKUP_PROFILES_DIR/" 2>/dev/null || true
    fi
    
    if [ -d "$BUILD_DIR" ]; then
        print_info "Removing build directory..."
        rm -rf "$BUILD_DIR"
    fi
    
    if [ -d "$BACKUP_PROFILES_DIR" ]; then
        print_info "Restoring PGO profiles..."
        mkdir -p "$PGO_PROFILES_DIR"
        mv "$BACKUP_PROFILES_DIR"/* "$PGO_PROFILES_DIR/" 2>/dev/null || true
        rm -rf "$BACKUP_PROFILES_DIR"
    fi
    
    print_info "Clean complete!"
    exit 0
fi

# Generate stage
if [ "$STAGE" == "generate" ]; then
    print_header "PGO Stage 1: Generate Profile Data"
    
    # Create build directory
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    # Configure
    print_info "Configuring CMake..."
    cmake .. \
        -DCITRON_ENABLE_PGO_GENERATE=ON \
        -DCITRON_ENABLE_LTO=$ENABLE_LTO \
        -DCMAKE_BUILD_TYPE=Release
    
    # Build
    print_info "Building instrumented Citron (this may take a while)..."
    cmake --build . -j"$JOBS"
    
    print_header "Build Complete!"
    print_info "Next steps:"
    echo "  1. Run: ./bin/citron"
    echo "  2. Play games for 15-30 minutes to collect profile data"
    echo "  3. Exit citron"
    
    # Clang-specific instructions
    if [ "$(cmake --system-information | grep CMAKE_CXX_COMPILER_ID | grep -i clang)" ]; then
        echo "  4. Merge profiles: llvm-profdata merge -output=$PGO_PROFILES_DIR/default.profdata $PGO_PROFILES_DIR/*.profraw"
        echo "  5. Run: $0 use"
    else
        echo "  4. Run: $0 use"
    fi
fi

# Use stage
if [ "$STAGE" == "use" ]; then
    print_header "PGO Stage 2: Build Optimized Binary"
    
    # Check if profile data exists
    if [ ! -d "$PGO_PROFILES_DIR" ] || [ -z "$(ls -A $PGO_PROFILES_DIR 2>/dev/null)" ]; then
        print_error "No profile data found in $PGO_PROFILES_DIR"
        print_info "Please run the generate stage first and collect profile data"
        exit 1
    fi
    
    # Backup profiles if build directory exists
    if [ -d "$BUILD_DIR" ]; then
        print_info "Backing up PGO profiles..."
        mkdir -p "$BACKUP_PROFILES_DIR"
        cp -r "$PGO_PROFILES_DIR"/* "$BACKUP_PROFILES_DIR/"
        rm -rf "$BUILD_DIR"
    fi
    
    # Create build directory and restore profiles
    mkdir -p "$BUILD_DIR"
    if [ -d "$BACKUP_PROFILES_DIR" ]; then
        mkdir -p "$PGO_PROFILES_DIR"
        mv "$BACKUP_PROFILES_DIR"/* "$PGO_PROFILES_DIR/"
        rm -rf "$BACKUP_PROFILES_DIR"
    fi
    
    cd "$BUILD_DIR"
    
    # Configure
    print_info "Configuring CMake..."
    cmake .. \
        -DCITRON_ENABLE_PGO_USE=ON \
        -DCITRON_ENABLE_LTO=$ENABLE_LTO \
        -DCMAKE_BUILD_TYPE=Release
    
    # Build
    print_info "Building optimized Citron (this may take a while)..."
    cmake --build . -j"$JOBS"
    
    print_header "Build Complete!"
    print_info "Your optimized Citron binary is ready!"
    print_info "Location: $BUILD_DIR/bin/citron"
    echo ""
    print_info "This build is optimized for your specific usage patterns."
    print_info "Enjoy improved performance! 🚀"
fi

