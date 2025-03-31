@echo off

set TLC_PATH="../../build/tlc.exe"

%TLC_PATH% main.tlisp
mkdir build
move main.ll build/
cd build

llc main.ll
clang main.s -o main.exe