#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <bundle-item>" >&2
    exit 2
fi

item="$1"
identity="${EXPANDED_CODE_SIGN_IDENTITY:-}"

if [ -z "$identity" ]; then
    identity="${CODE_SIGN_IDENTITY:-}"
fi

if [ -z "$identity" ]; then
    echo "No code signing identity was provided by Xcode; skipping embed signing for $item." >&2
    exit 0
fi

/usr/bin/codesign --force --sign "$identity" --timestamp=none "$item"
