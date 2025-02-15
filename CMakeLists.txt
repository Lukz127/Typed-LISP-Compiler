cmake_minimum_required(VERSION 3.15)

project(Compiler C)

set(LLVM_DIR "D:/Programs/llvm-project/build/Release/lib" CACHE PATH "Path to LLVM CMake configuration")

# Find LLVM
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Include LLVM directories
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# Create the executable
add_executable(compiler src/main.c)
include_directories(BEFORE compiler src/)
include_directories(BEFORE compiler src/include)

# Link against LLVM
target_link_libraries(compiler PRIVATE LLVM-C)
