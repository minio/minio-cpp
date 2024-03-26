#!/bin/bash

if [ -z "${CLANG_FORMAT}" ]; then
  CLANG_FORMAT="clang-format"
fi

function do_clang_format() {
    echo "verifying '${CLANG_FORMAT} --output-replacements-xml --style=Google $@'"
    if ${CLANG_FORMAT} --output-replacements-xml --style=Google "$@" | grep -q '<replacement '; then
        echo "ERROR:" "$@" "not in Google C/C++ style"
        echo "To fix formatting run"
        echo "$ ${CLANG_FORMAT} -i --style=Google" "$@"
        return 255
    fi
}

tmpfile="tmpfile.$RANDOM"
find src include examples tests -iname "*.cc" -o -iname "*.h" > "$tmpfile"
ec=0
while read -r file; do
    if ! do_clang_format "$file"; then
        ec=255
    fi
done < "$tmpfile"
rm -f "$tmpfile"

exit "$ec"
