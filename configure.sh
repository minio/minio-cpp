#!/bin/sh

set -x
BUILD_OPTIONS="-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"

if [ -n "$VCPKG_ROOT" ]; then
	BUILD_OPTIONS="${BUILD_OPTIONS} -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
fi

echo "== [Configuring Build - Debug] =="
eval cmake . -B build/Debug -DCMAKE_BUILD_TYPE=Debug ${BUILD_OPTIONS} "$@"
echo ""

echo "== [Configuring Build - Release] =="
eval cmake . -B build/Release -DCMAKE_BUILD_TYPE=Release ${BUILD_OPTIONS} "$@"
echo ""
