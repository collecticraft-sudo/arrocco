#!/bin/bash
fw_ver="V1.46"

starting_dir=$(pwd)
# change the path to where this script is - useful in case this script
# is being called from somewhere else, like from within an IDE
cd "$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

echo "Generating CT800 32/64 bit for Linux."
gcc -pthread -Wall -Wextra -Wlogical-op -Wstrict-prototypes -Wshadow -Werror -O2 -march=native -mtune=native -flto=auto -std=c99 -faggressive-loop-optimizations -fno-unsafe-loop-optimizations -fgcse-sm -fgcse-las -fgcse-after-reload -fno-strict-aliasing -fno-strict-overflow -fno-common -o output/CT800_${fw_ver} play.c tuner.c kpk.c eval.c move_gen.c hashtables.c search.c util.c book.c kpk_table.c bookdata.c -lrt -Wl,-s
# gcc -DAUTOTUNE -pthread -Wall -Wextra -Wpedantic -Wlogical-op -Wstrict-prototypes -Wshadow -Werror -O2 -march=native -mtune=native -flto=auto -std=c99 -faggressive-loop-optimizations -fno-unsafe-loop-optimizations -fgcse-sm -fgcse-las -fgcse-after-reload -fno-strict-aliasing -fno-strict-overflow -fno-common -o output/CT800_${fw_ver} play.c tuner.c kpk.c eval.c move_gen.c hashtables.c search.c util.c book.c kpk_table.c bookdata.c -lrt -lm -Wl,-s

# go back to the starting directory
cd "$starting_dir"
