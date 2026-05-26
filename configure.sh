#!/bin/sh

set -x

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_OPTIONS="-DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DBUILD_SHARED_LIBS=ON"

if [ -n "$VCPKG_ROOT" ]; then
	BUILD_OPTIONS="${BUILD_OPTIONS} -DVCPKG_ROOT=${VCPKG_ROOT}"
fi

echo "== [Configuring Build - Debug] =="
eval cmake . -B build/Debug -DCMAKE_BUILD_TYPE=Debug ${BUILD_OPTIONS} "$@"
echo ""

echo "== [Configuring Build - Release] =="
eval cmake . -B build/Release -DCMAKE_BUILD_TYPE=Release ${BUILD_OPTIONS} "$@"
echo ""
