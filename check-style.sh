#!/bin/bash

if [ -z "${CLANG_FORMAT}" ]; then
  CLANG_FORMAT="clang-format"
fi

FIX_MODE=0
if [ "$1" = "--fix" ] || [ "$1" = "-f" ]; then
    FIX_MODE=1
fi

function do_clang_format() {
    if [ "$FIX_MODE" = "1" ]; then
        if ${CLANG_FORMAT} --output-replacements-xml --style=Google "$@" | grep -q '<replacement '; then
            echo "fixing '$@'"
            ${CLANG_FORMAT} -i --style=Google "$@"
            return 0
        fi
    else
        echo "verifying '${CLANG_FORMAT} --output-replacements-xml --style=Google $@'"
        if ${CLANG_FORMAT} --output-replacements-xml --style=Google "$@" | grep -q '<replacement '; then
            echo "ERROR:" "$@" "not in Google C/C++ style"
            echo "To fix formatting run"
            echo "$ ${CLANG_FORMAT} -i --style=Google" "$@"
            return 255
        fi
    fi
}

tmpfile="tmpfile.$RANDOM"
find src include examples tests -iname "*.cc" -o -iname "*.h" | grep -v 'cuda\.h$' | grep -v 'cufile\.h$' | grep -v 'cufile_info\.h$' > "$tmpfile"
ec=0
while read -r file; do
    if ! do_clang_format "$file"; then
        ec=255
    fi
done < "$tmpfile"
rm -f "$tmpfile"

if [ "$FIX_MODE" = "1" ]; then
    echo "All formatting issues fixed."
fi

exit "$ec"
