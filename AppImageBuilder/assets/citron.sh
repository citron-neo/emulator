#! /bin/sh

LD_LIBRARY_PATH=/usr/lib/$(uname -m)-linux-gnu:/usr/lib:. exec ./citron "$@"
