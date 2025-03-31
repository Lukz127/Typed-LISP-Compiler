# Typed LISP Compiler

This is a compiler for my own programming language - Typed LISP written in C.

The `/examples/` directory contains multiple examples, that explain how the language works.

To get syntax highlighting in VSCode you can install the [Typed LISP VSCode support](https://github.com/Lukz127/TLISP-VSCode-support/releases) extension

Special thanks to Sean Barrett for the `stb_ds.h` library.
Special thanks to Samuel Custodio for the [assets](https://github.com/samuelcust/flappy-bird-assets) for the Raylib Flappy Bird example.

## Table of Contents

- [Typed LISP Compiler](#typed-lisp-compiler)
  - [Table of Contents](#table-of-contents)
  - [Compilation](#compilation)
    - [Dependencies](#dependencies)
    - [Instructions](#instructions)
  - [Usage](#usage)
    - [Full example of compiling](#full-example-of-compiling)
      - [Loops example](#loops-example)
      - [Raylib Flappy Bird example (without using `compile.bat`)](#raylib-flappy-bird-example-without-using-compilebat)
  - [This project also uses libraries / resources:](#this-project-also-uses-libraries--resources)

## Compilation

### Dependencies

- [CMake](https://cmake.org) with your preferred build system (for example `make` or `Visual Studio Build Tools`)
- [LLVM](https://github.com/llvm/llvm-project)

### Instructions

- `mkdir build`
- `cd build`
- Use `cmake .. -DCMAKE_BUILD_TYPE=Release -G <your-preferred-generator>` for example `cmake .. -DCMAKE_BUILD_TYPE=Release -G "MinGW Makefiles"` for me (on Windows).
- If it doesn't find LibXml2, install LibXml2 using your package manager (`vcpkg install LibXml2` on windows with vcpkg). Then run cmake again.
- If it doesn't find LLVM, make sure llvm is installed, then open `CMakeLists.txt`, uncomment line 5 `set(LLVM_DIR "D:/Programs/llvm-project/build/Release/lib" ...)` and set it to the directory where your LLVM binaries are. Then run cmake again.
- Run `make` if you used Makefile, otherwise compile as your generator requires.

## Usage

After compilation, `tlc.exe` will be in the build directory. For help run `tlc.exe --help` or `tlc.exe -h`

- To compile a Typed LISP file, go in the directory of the file (this is important, because Typed LISP uses imports, that can be relative).
- Use `tlc.exe <file_name>` to compile the main file. For example `tlc.exe ./main.tlisp`.
- If the compilation had no errors, each Typed LISP file will be compiled to LLVM IR (`<file_name>.ll` file) and will be placed next to the Typed LISP file (this means also in the same directory).
- If your project has multiple Typed LISP files:
  - Option 1:
    - Use `llvm-link` to link all LLVM IR files. You must also specify the output file.
    - For example `llvm-link ./main.ll ./someLibrary/someSourceFile1.ll ./someLibrary/someSourceFile2.ll -o program`
    - Then use `llc` to compile the resulting file. You can either output an object file directly `-filetype=obj -o <file_name>.o`, or you can compile it to an assembly file.
    - For example `llc program` (for assembly) or `llc program -filetype=obj -o program.o` (for object file)
    - Then link or compile the result and also link with any external dependencies the same way you would with C. If you are compiling assembly, you must use clang. To link I think you can use any C linker, but I use clang just in case.
    - For example `clang program.s -o program.exe` or `clang program.o -o program.exe`
  - Option 2:
    - Use `llc` to compile each LLVM IR separately. You can either output an object file directly `-filetype=obj -o <file_name>.o`, or you can compile it to an assembly file.
    - For example `llc ./main.ll`, `llc ./someLibrary/someSourceFile1.ll`, `llc ./someLibrary/someSourceFile2.ll`
    - Or `llc ./main.ll -filetype=obj -o main.o`, `llc ./someLibrary/someSourceFile1.ll -filetype=obj -o ./someLibrary/someSourceFile1.ll`, `llc ./someLibrary/someSourceFile2.ll -filetype=obj -o ./someLibrary/someSourceFile2.ll`
    - If you compiled it to assembly use clang (it must be clang) to compile all assembly files that you generated using llc.
    - Then use clang (any linker should work but I use clang just in case) to link all object files and any libraries necessary (like when you link C)
- If your project has a single Typed LISP file it's the same as with multiple, but you only do it with one file.

### Full example of compiling

#### Loops example

```batch
cd ./examples/loops
../../build/tlc.exe ./main.tlisp
llc ./main.ll
clang ./main.s -o main.exe
```

#### Raylib Flappy Bird example (without using `compile.bat`)

```batch
cd ./examples/raylib-flappy-bird
../../build/tlc.exe ./main.tlisp
llvm-link ./main.ll ./raylib.ll -o program
llc program -filetype=obj -o program.o

@rem clang -o program.exe program.o is for linking the object file and the rest is for linking it with libraries
clang -o program.exe program.o -I%RAYLIB_PATH%\include -L%RAYLIB_PATH%\lib -lmsvcrt -lraylib -lOpenGL32 -lGdi32 -lWinMM -lkernel32 -lshell32 -lUser32 -Xlinker /NODEFAULTLIB:libcmt
```

## This project also uses libraries / resources:

- **LLVM**, which is licensed under the [Apache License Version 2.0](https://github.com/llvm/llvm-project/blob/main/LICENSE.TXT)
- **stb_ds**, which is licensed under the [MIT License/public domain](https://github.com/Lukz127/Typed-LISP-Compiler/blob/master/src/include/stb_ds.h)
- **flappy-bird-assets**, which are licensed under the [MIT License](https://github.com/samuelcust/flappy-bird-assets/blob/master/LICENSE)
