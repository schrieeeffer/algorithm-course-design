#!/bin/sh
set -e

cd "$(dirname "$0")" || exit 1

if [ ! -x ./build/execname ]; then
    sh ./build.sh
fi

if [ -x ./build/execname ]; then
    exec ./build/execname
fi

if [ -x ./build/execname.exe ]; then
    exec ./build/execname.exe
fi

echo "Executable build/execname was not created." >&2
exit 1
