#!/bin/sh
# Cross-compile ScoopStar 7.0 for 64-bit Windows with MinGW-w64.
set -e
cd "$(dirname "$0")"
# Hunspell (C++), built once
if [ ! -f hunspell/libhunspell.a ]; then
  mkdir -p hunspell/obj
  for f in affentry affixmgr csutil filemgr hashmgr hunspell hunzip phonet replist suggestmgr hunspelltrace; do
    x86_64-w64-mingw32-g++ -O2 -std=c++17 -DHUNSPELL_STATIC -DBUILDING_LIBHUNSPELL -c hunspell/$f.cxx -o hunspell/obj/$f.o
  done
  x86_64-w64-mingw32-ar rcs hunspell/libhunspell.a hunspell/obj/*.o
fi
if [ ! -f hyphen/libhyphen.a ]; then
  x86_64-w64-mingw32-gcc -O2 -c hyphen/hyphen.c -o hyphen/hyphen.o
  x86_64-w64-mingw32-gcc -O2 -c hyphen/hnjalloc.c -o hyphen/hnjalloc.o
  x86_64-w64-mingw32-ar rcs hyphen/libhyphen.a hyphen/hyphen.o hyphen/hnjalloc.o
fi
cd src
x86_64-w64-mingw32-windres scoopstar.rc -O coff -o scoopstar_res.o
mkdir -p obj
for f in screen doc edit menus ui fileio print spell gen; do
  x86_64-w64-mingw32-gcc -O2 -std=gnu11 -Wall -Wno-unused-function -municode -DHUNSPELL_STATIC -c $f.c -o obj/$f.o
done
x86_64-w64-mingw32-g++ -O2 -municode -mwindows -static -o ../ScoopStar.exe obj/*.o scoopstar_res.o \
  ../hunspell/libhunspell.a ../hyphen/libhyphen.a -lgdi32 -luser32 -lcomdlg32 -lshell32 -lole32 -luuid -lgdiplus
echo built ../ScoopStar.exe
