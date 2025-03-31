# Raylib Flappy Bird

This is an example of using Raylib with Typed LISP. Currently, there are no menus, collisions or score etc, but it demonstrates what can be done with Typed LISP. Press space to jump.

Special thanks to Samuel Custodio for the [assets](https://github.com/samuelcust/flappy-bird-assets) licensed under the [MIT License](https://github.com/samuelcust/flappy-bird-assets/blob/master/LICENSE).

## Compiling

## Dependencies

- [Raylib](https://github.com/raysan5/raylib/releases) (the msvc binaries if you're using windows)
- The clang compiler on path (for Windows)
- The preferred linker / C compiler on path (for Linux)

### On Windows:

- Check `%TLC_PATH%` and `%RAYLIB_PATH%` in the `compile.bat` file. The `%TLC_PATH%` should be the path to the `tlc.exe` file, which you will get by building the `TLC` project. If you have not changed the build directory or the position of this directory relative to it, you should be able to just keep it relative. But make sure the `%RAYLIB_PATH%` is the directory that contains the `include` and `lib` directories containing raylib headers and binaries.
- Run the `compile.bat` file to compile it. If there is no error, the resulting executable will be in `/build/program.exe`.

### On Linux:

- Try the Windows method. If it fails do the following steps.
- Compile `main.tlisp` using `tlc`.
- Use `llvm-link` to link `main.ll` and `raylib.ll`, also specify the output.
- Use `llc` with the `-filetype=obj` parameter to compile the result from the previous step.
- Use clang or any other linker / C compiler to correctly link the object file with raylib and other necessary libraries
