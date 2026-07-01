#!/bin/sh
set -e

cd "$(dirname "$0")" || exit 1
mkdir -p build
"${CXX:-g++}" -std=c++17 -O2 -pipe \
    src/main.cpp \
    src/parser.cpp \
    src/machine_state.cpp \
    src/scheduler.cpp \
    src/output.cpp \
    -o build/execname
