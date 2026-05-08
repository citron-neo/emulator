#!/bin/sh
# citron AppImage wrapper script

# Find the directory where the AppImage is located
if [ -n "$APPIMAGE" ]; then
    APPIMAGE_DIR="$(dirname "$APPIMAGE")"
else
    # Fallback for non-AppImage execution
    APPIMAGE_DIR="$(cd "$(dirname "$0")" && pwd)"
fi

# Set PGO profile output to be next to the AppImage
# This ensures that instrumented builds write their results where the user expects.
if [ -z "$LLVM_PROFILE_FILE" ]; then
    export LLVM_PROFILE_FILE="$APPIMAGE_DIR/default-%p.profraw"
fi

# We don't 'cd' to APPIMAGE_DIR to avoid breaking relative paths for games,
# but Citron will find the 'user' folder if it's in the current working directory
# from which the AppImage was launched.

# Run the real binary
# $APPDIR is the mount point set by the AppRun loader
exec "$APPDIR/usr/bin/citron.bin" "$@"
