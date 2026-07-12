#!/bin/sh
sources="buffer.c interpreter.c lps.c main.c master_slave_channel.c primitives.c reader.c readtable.c sexp.c symboltable.c arenapool.c"
ctt readtable.ct || exit
gcc -o ssi $sources -g -fshort-enums -fno-strict-aliasing -fsanitize=undefined
# rm readtable.c
