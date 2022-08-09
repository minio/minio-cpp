#!/bin/bash

function clang_format() {
    echo "verifying 'clang-format --output-replacements-xml --style=Google $@'"
    if clang-format --output-replacements-xml --style=Google "$@" | grep -q '<replacement '; then
        echo "ERROR:" "$@" "not in Google C/C++ style"
        echo "To fix formatting run"
        echo "$ clang-format -i --style=Google" "$@"
        return 255
    fi
}

ec=0
source_files=$(find src include examples tests -type f \( -name '*.h' -o -name '*.cc' \))
for file in ${source_files}; do
    if ! clang_format "$file"; then
        ec=255
    fi
done

exit "$ec"
