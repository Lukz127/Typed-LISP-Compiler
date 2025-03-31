# Macro example

This is an example explains how to use macros. Look at `main.tlisp` to learn how to use them.

## Compiling

## Dependencies

- The clang compiler on path

### On Windows:

- Check `%TLC_PATH%` in the `compile.bat` file. The `%TLC_PATH%` should be the path to the `tlc.exe` file, which you will get by building the `TLC` project. If you have not changed the build directory or the position of this directory relative to it, you should be able to just keep it relative.
- Run the `compile.bat` file to compile it. If there is no error, the resulting executable will be at `/build/program.exe`.

### On Linux:

- Try the Windows method. If it fails do the following steps.
- Compile `main.tlisp` using `tlc`.
- Use `llc` to compile the result from the previous step.
- Use clang to compile the result from the previous step. This must be clang, because the resulting assembly from llc could be incompatible with gcc and others.
