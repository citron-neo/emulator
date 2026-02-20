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
    echo "Usage: $0 [STAGE] [OPTIONS]"
    echo ""
    echo "STAGE can be:"
    echo "  generate  - Build with PGO instrumentation (Stage 1)"
    echo "  use       - Build using PGO profile data (Stage 2)"
    echo "  clean     - Clean build directory but preserve profiles"
    echo "  merge     - Merge profile data (Clang: .profraw -> .profdata)"
    echo "  summary   - Show profile file statistics"
    echo ""
    echo "Example workflow:"
    echo "  $0 generate       # Build instrumented version"
    echo "  # Run citron, play 2-3 games for 5-10 min each, exit cleanly"
    echo "  $0 merge          # Merge collected profiles"
    echo "  $0 summary        # Check coverage"
    echo "  $0 use            # Build optimized version"
    echo ""
    echo "Options:"
    echo "  -j N      Number of parallel jobs (default: auto-detect)"
    echo "  -lto      Enable Link-Time Optimization"
    echo "  -h        Show this help message"
}

clean_stale_profiles() {
    if [ ! -d "$PGO_PROFILES_DIR" ]; then
        return
    fi

    local gcda_count
    gcda_count=$(find "$PGO_PROFILES_DIR" -name "*.gcda" 2>/dev/null | wc -l)
    if [ "$gcda_count" -gt 0 ]; then
        print_info "Removing $gcda_count stale .gcda file(s)..."
        find "$PGO_PROFILES_DIR" -name "*.gcda" -delete
    fi

    local profraw_count
    profraw_count=$(find "$PGO_PROFILES_DIR" -name "*.profraw" 2>/dev/null | wc -l)
    if [ "$profraw_count" -gt 0 ]; then
        print_info "Removing $profraw_count stale .profraw file(s)..."
        find "$PGO_PROFILES_DIR" -name "*.profraw" -delete
    fi
}

merge_clang_profiles() {
    if ! command -v llvm-profdata &>/dev/null; then
        print_warning "llvm-profdata not found. Cannot merge Clang profiles."
        return 1
    fi

    local profraw_files
    profraw_files=$(find "$PGO_PROFILES_DIR" -name "*.profraw" 2>/dev/null)
    if [ -z "$profraw_files" ]; then
        print_info "No .profraw files to merge."
        return 0
    fi

    local count
    count=$(echo "$profraw_files" | wc -l)
    print_info "Merging $count .profraw file(s)..."

    llvm-profdata merge \
        -output="${PGO_PROFILES_DIR}/default.profdata" \
        ${profraw_files}

    print_info "Merged into ${PGO_PROFILES_DIR}/default.profdata"

    # Clean raw files after merge
    print_info "Cleaning merged .profraw files..."
    find "$PGO_PROFILES_DIR" -name "*.profraw" -delete

    return 0
}

show_profile_summary() {
    if [ ! -d "$PGO_PROFILES_DIR" ]; then
        print_warning "Profile directory not found: $PGO_PROFILES_DIR"
        return
    fi

    print_header "Profile Summary"

    local profdata="${PGO_PROFILES_DIR}/default.profdata"
    if [ -f "$profdata" ]; then
        local size
        size=$(du -h "$profdata" | cut -f1)
        print_info "Clang profile: $profdata ($size)"
        if command -v llvm-profdata &>/dev/null; then
            llvm-profdata show --counts --all-functions "$profdata" 2>/dev/null | tail -5
        fi
    fi

    local gcda_count
    gcda_count=$(find "$PGO_PROFILES_DIR" -name "*.gcda" 2>/dev/null | wc -l)
    if [ "$gcda_count" -gt 0 ]; then
        print_info "GCC profile files: $gcda_count .gcda files"
        local total_size
        total_size=$(find "$PGO_PROFILES_DIR" -name "*.gcda" -exec du -ch {} + 2>/dev/null | tail -1 | cut -f1)
        print_info "Total .gcda size: $total_size"
    fi

    local profraw_count
    profraw_count=$(find "$PGO_PROFILES_DIR" -name "*.profraw" 2>/dev/null | wc -l)
    if [ "$profraw_count" -gt 0 ]; then
        print_warning "$profraw_count unmerged .profraw file(s) found. Run '$0 merge' first."
    fi

    if [ "$gcda_count" -eq 0 ] && [ ! -f "$profdata" ] && [ "$profraw_count" -eq 0 ]; then
        print_warning "No profile data found in $PGO_PROFILES_DIR"
    fi
}

# Parse arguments
STAGE=""
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo "4")
ENABLE_LTO="OFF"

while [[ $# -gt 0 ]]; do
    case $1 in
        generate|use|clean|merge|summary)
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

# --- Clean stage ---
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

# --- Merge stage ---
if [ "$STAGE" == "merge" ]; then
    print_header "Merging PGO Profiles"

    if [ ! -d "$PGO_PROFILES_DIR" ]; then
        print_error "Profile directory not found: $PGO_PROFILES_DIR"
        exit 1
    fi

    merge_clang_profiles
    print_info "Done! Run '$0 summary' to check coverage."
    exit 0
fi

# --- Summary stage ---
if [ "$STAGE" == "summary" ]; then
    show_profile_summary
    exit 0
fi

# --- Generate stage ---
if [ "$STAGE" == "generate" ]; then
    print_header "PGO Stage 1: Generate Profile Data"

    # Clean stale profile data from any previous run
    if [ -d "$PGO_PROFILES_DIR" ]; then
        clean_stale_profiles
    fi
    
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
    echo ""
    echo -e "${YELLOW}  Training guide for best PGO results:${NC}"
    echo ""
    echo "  1. Run:  ./bin/citron"
    echo "  2. Launch a game and play PAST initial loading"
    echo "     (first-time shader compilation is a critical hot path)"
    echo "  3. Play for at least 5-10 minutes per game"
    echo "  4. Test 2-3 different games for broader code coverage"
    echo "  5. Navigate menus, settings, and game list to profile the UI"
    echo "  6. Exit citron cleanly (File -> Exit or Ctrl+Q)"
    echo ""
    echo -e "${YELLOW}  After each session, you can run:${NC}"
    echo "    $0 merge     # Consolidate collected profiles"
    echo "    $0 summary   # Check profile coverage"
    echo ""
    echo -e "${YELLOW}  When satisfied with coverage, build the optimized binary:${NC}"
    echo "    $0 use"
    echo ""
fi

# --- Use stage ---
if [ "$STAGE" == "use" ]; then
    print_header "PGO Stage 2: Build Optimized Binary"
    
    # Check if profile data exists
    if [ ! -d "$PGO_PROFILES_DIR" ] || [ -z "$(ls -A $PGO_PROFILES_DIR 2>/dev/null)" ]; then
        print_error "No profile data found in $PGO_PROFILES_DIR"
        print_info "Please run the generate stage first and collect profile data"
        exit 1
    fi

    # Merge any outstanding raw profiles before building
    print_info "Merging any outstanding profile data..."
    merge_clang_profiles || true
    
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
fi
