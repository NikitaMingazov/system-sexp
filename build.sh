#!/bin/sh
# eventually this will be rewritten in system-sexp, with a nob.c bootstrap

# download source and build ctime-transpiler
mkdir -p build
cd build
if [ -d "ctime" ]; then
  cd ctime
  git pull
else
	git clone 'https://github.com/NikitaMingazov/ctime' --depth 1
fi
cd ctime
clang -o nob ./nob.c && ./nob
ctt="$(pwd)/build/ctt"

# now work on system-sexp
cd ../../src

# transpile ctime sources (X.ct defaults to X.c output)

# something broke with libtcc in-memory, so clang is used as fallback (extremely slow :'( )
# libtcc used to be able to find <stdbool.h>, now it doesn't? I don't get it
# $ctt readtable.ct || exit
# $ctt main.ct || exit

$ctt readtable.ct -cc clang -a -O0 -a -Wno-duplicate-decl-specifier || exit
$ctt main.ct -cc clang -a -O0 || exit

# export intrinsics into the primitive table
clang -o scripts/export_intrinsics scripts/export_intrinsics.c
./scripts/export_intrinsics

# expose compiler's preprocessor variables
clang -dM -E - < /dev/null > preprocessor.h

sources="buffer.c interpreter.c lps.c main.c master_slave_channel.c primitives.c reader.c readtable.c sexp.c symboltable.c arenapool.c allocators/std-alloc.c allocators/arena-alloc.c"
# -fsanitize=undefined
clang -o ../build/ssi $sources -g -fdefer-ts -Werror=vla # -O3

# cleanup
rm ./scripts/export_intrinsics
rm exported_intrinsics.h
rm preprocessor.h
rm main.c
rm readtable.c

rm -rf ../build/ctime
