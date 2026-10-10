#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
: "${ZIG:=zig}"
mkdir -p bin
"$ZIG" c++ -target x86_64-windows-gnu -std=c++17 -O2 -g0 -fno-ident -Isrc \
  src/main.cpp -lwinmm -luser32 -lgdi32 -lcomctl32 -municode \
  -Wl,--subsystem,windows -Wl,--strip-all -o bin/OPMChord.exe
"$ZIG" c++ -target x86-windows-gnu -std=c++17 -O2 -g0 -fno-ident -Isrc \
  src/x68_helper.cpp -municode -Wl,--strip-all -o bin/OPMChord-X68Bridge32.exe
