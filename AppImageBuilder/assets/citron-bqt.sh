#! /bin/sh

LD_LIBRARY_PATH=./qt5:/usr/lib/$(uname -m)-linux-gnu:/usr/lib:. QT_PLUGIN_PATH=./qt5 exec ./citron "$@"
