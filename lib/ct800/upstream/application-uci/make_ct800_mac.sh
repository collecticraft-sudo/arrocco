#!/bin/bash
fw_ver="V1.46"

starting_dir=$(pwd)
# change the path to where this script is - useful in case this script
# is being called from somewhere else, like from within an IDE
cd "$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

echo "Generating CT800 64 bit for macOS."
clang -DNO_MONO_COND -m64 -pthread -Wall -Wextra -Wstrict-prototypes -Wshadow -Werror -O2 -flto -std=c99 -fno-strict-aliasing -fno-strict-overflow -fno-common -o output/CT800_${fw_ver} play.c kpk.c eval.c move_gen.c hashtables.c search.c util.c book.c kpk_table.c bookdata.c
if [ $? -ne 0 ]; then
    # go back to the starting directory
    cd "$starting_dir"
    exit 1
fi

# GNU 'strip' botches up executables without failure return code as of 2021.
# Try it on a temp copy and react to that - in case GNU 'strip' should get
# fixed later. Or maybe, Xcode's 'strip' is being used, that works.
cp output/CT800_${fw_ver}{,_tmp}
strip output/CT800_${fw_ver}_tmp
if $(echo "quit" | output/CT800_${fw_ver}_tmp &>/dev/null); then
    # 'strip' succeeded or is not installed
    mv output/CT800_${fw_ver}{_tmp,}
    # go back to the starting directory
    cd "$starting_dir"
    exit 0
fi
# 'strip' failed
rm -f output/CT800_${fw_ver}_tmp

# go back to the starting directory
cd "$starting_dir"
