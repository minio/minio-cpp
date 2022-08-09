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

tmpfile="tmpfile.$RANDOM"
find src include examples tests -iname "*.cc" -o -iname "*.h" > "$tmpfile"
ec=0
while read -r file; do
    if ! clang_format "$file"; then
        ec=255
    fi
done < "$tmpfile"
rm -f "$tmpfile"

exit "$ec"
