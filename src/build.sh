#!/bin/sh
sources="buffer.c interpreter.c lps.c main.c master_slave_channel.c primitives.c reader.c readtable.c sexp.c symboltable.c arenapool.c"
ctt readtable.ct || exit
cc -o scripts/export_intrinsics scripts/export_intrinsics.c
./scripts/export_intrinsics
gcc -o ssi $sources -g -fshort-enums -fno-strict-aliasing -fsanitize=undefined
rm ./scripts/export_intrinsics
# rm readtable.c
