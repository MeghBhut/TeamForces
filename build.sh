#!/usr/bin/env bash
# build.sh — build TeamForces on Linux (or Windows Git-Bash/MSYS2).
# Usage:  ./build.sh
set -e

CXX="${CXX:-g++}"
OUT="teamforces"
LIBS="-pthread"

# On Windows we need the Winsock library and a static link so the .exe is portable.
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    OUT="teamforces.exe"
    LIBS="-pthread -static -static-libgcc -static-libstdc++ -lws2_32"
    ;;
esac

echo "Compiling with $CXX -> $OUT"
$CXX -std=c++17 -O2 -I third_party src/*.cpp -o "$OUT" $LIBS
echo "OK -> $OUT"
