@echo off

set TLC_PATH="../../build/tlc.exe"
set RAYLIB_PATH="C:\raylib-5.5_win64_msvc16"

%TLC_PATH% main.tlisp
mkdir build
move main.ll build/
move raylib.ll build/
cd build

llvm-link main.ll raylib.ll -o program
llc program -filetype=obj -o program.o
clang -o program.exe program.o -I%RAYLIB_PATH%\include -L%RAYLIB_PATH%\lib -lmsvcrt -lraylib -lOpenGL32 -lGdi32 -lWinMM -lkernel32 -lshell32 -lUser32 -Xlinker /NODEFAULTLIB:libcmt
