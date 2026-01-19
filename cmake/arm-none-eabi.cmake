# cmake/arm-none-eabi.cmake
#
# Generic ARM bare-metal toolchain file for STM32 projects
# using the GNU Arm Embedded Toolchain (arm-none-eabi-*).
#
# This file tells CMake:
#  - we are building for a non-host, bare-metal target
#  - which compilers and binutils to use
#  - how to find the arm-none-eabi tools
#  - how CMake should behave when searching for headers/libs
#

cmake_minimum_required(VERSION 3.20)

# -----------------------------------------------------------------------------
# Target system description
# -----------------------------------------------------------------------------
# "Generic" tells CMake that this is not a hosted OS (no Linux/Windows/macOS).
# This disables many platform assumptions.
set(CMAKE_SYSTEM_NAME Generic)

# Target CPU architecture
set(CMAKE_SYSTEM_PROCESSOR arm)

# -----------------------------------------------------------------------------
# Try-compile behavior
# -----------------------------------------------------------------------------
# CMake normally tries to compile and RUN small test executables.
# On bare-metal targets this is impossible, so we force static libraries
# instead of executables for try-compile tests.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# -----------------------------------------------------------------------------
# Toolchain path (optional)
# -----------------------------------------------------------------------------
# User can explicitly specify the toolchain installation directory:
#
#   cmake -DARM_NONE_EABI_TOOLCHAIN_PATH="C:/Program Files (x86)/GNU Arm Embedded Toolchain/12.3 rel1/bin" ..
#
# If left empty, CMake will search for the tools in PATH.
set(ARM_NONE_EABI_TOOLCHAIN_PATH "" CACHE PATH
    "Path to arm-none-eabi toolchain bin directory")

# -----------------------------------------------------------------------------
# Helper function to locate arm-none-eabi tools
# -----------------------------------------------------------------------------
# Tries the user-provided toolchain path first (if set),
# then falls back to the system PATH.
# Fails hard if the tool cannot be found.
function(_find_arm_tool outvar tool)
  if(ARM_NONE_EABI_TOOLCHAIN_PATH AND EXISTS "${ARM_NONE_EABI_TOOLCHAIN_PATH}")
    find_program(${outvar} NAMES ${tool}
      HINTS "${ARM_NONE_EABI_TOOLCHAIN_PATH}"
      NO_DEFAULT_PATH
    )
  endif()

  # Fallback: search in PATH
  if(NOT ${outvar})
    find_program(${outvar} NAMES ${tool})
  endif()

  # Stop configuration if the tool is missing
  if(NOT ${outvar})
    message(FATAL_ERROR
      "Could not find ${tool}. Put it in PATH or set "
      "-DARM_NONE_EABI_TOOLCHAIN_PATH=...</bin>")
  endif()
endfunction()

# -----------------------------------------------------------------------------
# Locate all required GNU Arm tools
# -----------------------------------------------------------------------------
_find_arm_tool(ARM_GCC      arm-none-eabi-gcc)
_find_arm_tool(ARM_GXX      arm-none-eabi-g++)
_find_arm_tool(ARM_AS       arm-none-eabi-gcc)     # gcc drives assembler
_find_arm_tool(ARM_AR       arm-none-eabi-ar)
_find_arm_tool(ARM_OBJCOPY  arm-none-eabi-objcopy)
_find_arm_tool(ARM_OBJDUMP  arm-none-eabi-objdump)
_find_arm_tool(ARM_SIZE     arm-none-eabi-size)
_find_arm_tool(ARM_RANLIB   arm-none-eabi-ranlib)
_find_arm_tool(ARM_GDB      arm-none-eabi-gdb)

# -----------------------------------------------------------------------------
# Tell CMake which compilers to use
# -----------------------------------------------------------------------------
# FORCE is used because toolchain files are processed before project()
# and we want to override any defaults.
set(CMAKE_C_COMPILER   "${ARM_GCC}" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${ARM_GXX}" CACHE FILEPATH "" FORCE)

# The assembler is also driven through gcc (supports preprocessing)
set(CMAKE_ASM_COMPILER "${ARM_AS}" CACHE FILEPATH "" FORCE)

# -----------------------------------------------------------------------------
# Binutils configuration
# -----------------------------------------------------------------------------
set(CMAKE_AR     "${ARM_AR}"     CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB "${ARM_RANLIB}" CACHE FILEPATH "" FORCE)

# Export additional tools so they can be used in CMakeLists.txt
# (e.g. for custom post-build steps)
set(CMAKE_OBJCOPY "${ARM_OBJCOPY}" CACHE FILEPATH "" FORCE)
set(CMAKE_OBJDUMP "${ARM_OBJDUMP}" CACHE FILEPATH "" FORCE)
set(CMAKE_SIZE    "${ARM_SIZE}"    CACHE FILEPATH "" FORCE)
set(CMAKE_GDB     "${ARM_GDB}"     CACHE FILEPATH "" FORCE)

# -----------------------------------------------------------------------------
# CMake find behavior (important for cross-compiling)
# -----------------------------------------------------------------------------
# Do NOT search the target root path for programs (executables).
# Programs must be taken from the host system.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# Libraries, headers and packages must come from the target environment
# (or toolchain/sysroot), not the host OS.
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
