@echo off
REM ---------------------------------------------------------------------------
REM build.cmd
REM ---------------------------------------------------------------------------
REM Windows build script for the STM32 project using:
REM   - CMake
REM   - Ninja generator
REM   - GNU Arm Embedded Toolchain (via arm-none-eabi.cmake toolchain file)
REM
REM This script performs a clean build:
REM   1. Deletes the existing build directory
REM   2. Configures CMake with the ARM toolchain
REM   3. Builds the project
REM ---------------------------------------------------------------------------

REM Disable command echoing for cleaner output
@echo off

REM Remove the entire build directory (if it exists)
REM  /s = remove all subdirectories and files
REM  /q = quiet mode (no confirmation)
rmdir /s /q build

REM Configure the project:
REM  -S .      : source directory (project root)
REM  -B build  : build directory
REM  -G Ninja  : use Ninja as the build system
REM  -DCMAKE_TOOLCHAIN_FILE : cross-compilation toolchain description
REM  -DCMAKE_BUILD_TYPE=Debug : Debug build (symbols, low optimization)
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake\arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Debug

REM Build the project using the previously generated Ninja files
cmake --build build
