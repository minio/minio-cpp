#!/bin/bash

function clang_format() {
    if clang-format --output-replacements-xml --style=Google "$@" | grep -q '<replacement '; then
        echo "ERROR:" "$@" "not in Google C/C++ style"
        echo "To fix formatting run"
        echo "$ clang-format -i --style=Google" "$@"
        return 255
    fi
}

ec=0
mapfile -t files < <(find . -iname "*.cpp" -o -iname "*.h")
for file in "${files[@]}"; do
    if ! clang_format "$file"; then
        ec=255
    fi
done

exit "$ec"
