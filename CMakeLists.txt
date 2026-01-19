# -----------------------------------------------------------------------------
# CMakeLists.txt
# -----------------------------------------------------------------------------
# Build configuration for an STM32 NUCLEO-F767ZI bare-metal project using:
#  - GNU Arm Embedded Toolchain (arm-none-eabi-*)
#  - STM32 HAL + CMSIS
#  - Optional lwIP middleware
#  - USB Device (CDC)
#
# IMPORTANT: This file is written for cross-compiling (no host execution).
# -----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.20)

# Define the project and enabled languages
# - C, CXX: application + HAL + middleware
# - ASM: startup file(s)
project(nucleo_f767_base C CXX ASM)

# -----------------------------------------------------------------------------
# Language standards
# -----------------------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -----------------------------------------------------------------------------
# Default build type (if not provided by the user)
# Typical usage:
#   cmake -DCMAKE_BUILD_TYPE=Release ..
# -----------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Convenience alias for the final target name
set(TARGET ${PROJECT_NAME})

# =============================================================================
# Options
# =============================================================================

# Toggle lwIP middleware compilation and defines
option(ENABLE_LWIP "Enable lwIP middleware (Ethernet/UDP/TCP stack)" ON)

# -----------------------------------------------------------------------------
# MCU / CPU flags (NUCLEO-F767ZI / STM32F767ZI = Cortex-M7 + hard-float FPU)
# -----------------------------------------------------------------------------
# These flags must match your MCU core and FPU ABI.
set(MCPU_FLAGS
  -mcpu=cortex-m7
  -mthumb
  -mfpu=fpv5-d16
  -mfloat-abi=hard
)

# -----------------------------------------------------------------------------
# Global compile options applied to all targets in this directory
# -----------------------------------------------------------------------------
add_compile_options(
  ${MCPU_FLAGS}

  # Place each function/data item into its own section (enables --gc-sections)
  -ffunction-sections
  -fdata-sections

  # Avoid tentative definitions to reduce surprises with duplicate globals
  -fno-common

  # Warnings
  -Wall
  -Wextra
  -Wno-unused-parameter
)

# Build-type specific optimization/debug flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-Og -g3)
else()
  add_compile_options(-O2 -g0)
endif()

# -----------------------------------------------------------------------------
# Preprocessor defines used throughout the build
# -----------------------------------------------------------------------------
add_compile_definitions(
  USE_HAL_DRIVER
  STM32F767xx
)

# =============================================================================
# Sources
# =============================================================================

# Application sources:
# - Src/        (project sources)
# - Core/Src/   (CubeMX generated core code)
file(GLOB_RECURSE APP_SRC
  "${CMAKE_SOURCE_DIR}/Src/*.c"
  "${CMAKE_SOURCE_DIR}/Src/*.cpp"
  "${CMAKE_SOURCE_DIR}/Core/Src/*.c"
  "${CMAKE_SOURCE_DIR}/Core/Src/*.cpp"
)

# Example exclusion hook (currently commented out)
# list(FILTER APP_SRC EXCLUDE REGEX ".*/Src/eth\\.c$")

# STM32 HAL driver sources
file(GLOB_RECURSE HAL_SRC
  "${CMAKE_SOURCE_DIR}/Drivers/STM32F7xx_HAL_Driver/Src/*.c"
)

# CMSIS device templates (system_stm32f7xx.c etc. depending on CubeMX output)
file(GLOB_RECURSE CMSIS_TPL_SRC
  "${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32F7xx/Source/Templates/*.c"
)

# BSP / PHY drivers (LAN8742 etc.)
# Needed because ethernetif.c includes lan8742.h and uses lan8742.c
file(GLOB_RECURSE BSP_SRC
  "${CMAKE_SOURCE_DIR}/Drivers/BSP/**/*.c"
)

# USB Device middleware: Core + CDC class
file(GLOB_RECURSE USB_MW_SRC
  "${CMAKE_SOURCE_DIR}/Middlewares/ST/STM32_USB_Device_Library/Core/Src/*.c"
  "${CMAKE_SOURCE_DIR}/Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Src/*.c"
)

# CubeMX generated USB device glue code
file(GLOB_RECURSE USB_GEN_SRC
  "${CMAKE_SOURCE_DIR}/USB_DEVICE/App/*.c"
  "${CMAKE_SOURCE_DIR}/USB_DEVICE/Target/*.c"
)

# Fallback: some CubeMX projects place usbd_*.c under Src/ or Core/Src/
file(GLOB_RECURSE USB_SRC_FALLBACK
  "${CMAKE_SOURCE_DIR}/Src/usbd_*.c"
  "${CMAKE_SOURCE_DIR}/Core/Src/usbd_*.c"
)

# =============================================================================
# lwIP Middleware 
# =============================================================================

# These variables are populated only when ENABLE_LWIP=ON.
set(LWIP_SRC "")
set(LWIP_INC "")
set(LWIP_SYSTEM_INC "")
set(LWIPOPTS_INC "")

if(ENABLE_LWIP)
  # CubeMX typically places lwIP here
  set(LWIP_ROOT "${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/LwIP")
  set(LWIP_SRC_DIR "${LWIP_ROOT}/src")

  # opt.h is a good indicator that the lwIP include tree exists
  if(EXISTS "${LWIP_SRC_DIR}/include/lwip/opt.h")
    # Public includes for lwIP headers
    set(LWIP_INC "${LWIP_SRC_DIR}/include")

    # "system" contains sys_arch.c and related glue (CubeMX-style)
    set(LWIP_SYSTEM_INC "${LWIP_ROOT}/system")

    # lwIP core sources (IPv4 and IPv6 included)
    file(GLOB_RECURSE LWIP_CORE_SRC
      "${LWIP_SRC_DIR}/core/*.c"
      "${LWIP_SRC_DIR}/core/ipv4/*.c"
      "${LWIP_SRC_DIR}/core/ipv6/*.c"
    )

    # lwIP higher-level APIs (sockets/netconn depending on opts)
    file(GLOB_RECURSE LWIP_API_SRC
      "${LWIP_SRC_DIR}/api/*.c"
    )

    # lwIP network interface helpers
    file(GLOB_RECURSE LWIP_NETIF_SRC
      "${LWIP_SRC_DIR}/netif/*.c"
    )

    # Collect lwIP sources into one list
    set(LWIP_SRC
      ${LWIP_CORE_SRC}
      ${LWIP_API_SRC}
      ${LWIP_NETIF_SRC}
    )

    # Locate lwipopts.h (project-specific configuration header)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Core/Inc/lwipopts.h")
      set(LWIPOPTS_INC "${CMAKE_SOURCE_DIR}/Core/Inc")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/LWIP/App/lwipopts.h")
      set(LWIPOPTS_INC "${CMAKE_SOURCE_DIR}/LWIP/App")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/Inc/lwipopts.h")
      set(LWIPOPTS_INC "${CMAKE_SOURCE_DIR}/Inc")
    else()
      message(WARNING
        "lwIP enabled but lwipopts.h not found in Core/Inc, LWIP/App, or Inc. "
        "You may need to add it.")
    endif()

    # Optional compile define to allow conditional compilation in code
    add_compile_definitions(USE_LWIP)

    message(STATUS "lwIP: enabled (${LWIP_ROOT})")
  else()
    # Hard error if user asked for lwIP but the tree is missing
    message(FATAL_ERROR
      "ENABLE_LWIP=ON but lwIP not found. Expected: ${LWIP_SRC_DIR}/include/lwip/opt.h")
  endif()
else()
  message(STATUS "lwIP: disabled (ENABLE_LWIP=OFF)")
endif()

# =============================================================================
# Startup ASM
# =============================================================================

# Startup assembly file is required for vector table + reset handler.
# CubeMX usually generates something like startup_stm32f767xx.s
file(GLOB STARTUP_ASM
  "${CMAKE_SOURCE_DIR}/Startup/*.s"
  "${CMAKE_SOURCE_DIR}/Startup/*.S"
  "${CMAKE_SOURCE_DIR}/startup*.s"
  "${CMAKE_SOURCE_DIR}/startup*.S"
)

# Fail early if no startup file is found
if(STARTUP_ASM STREQUAL "")
  message(FATAL_ERROR
    "No startup *.s found. Put startup_stm32f767*.s into Startup/ or project root.")
endif()

# =============================================================================
# Linker script
# =============================================================================

# Try common CubeMX linker script file names and locations
set(LINKER_SCRIPT_DEFAULT "")
if(EXISTS "${CMAKE_SOURCE_DIR}/STM32F767ZITX_FLASH.ld")
  set(LINKER_SCRIPT_DEFAULT "${CMAKE_SOURCE_DIR}/STM32F767ZITX_FLASH.ld")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/STM32F767ZI_FLASH.ld")
  set(LINKER_SCRIPT_DEFAULT "${CMAKE_SOURCE_DIR}/STM32F767ZI_FLASH.ld")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/Core/STM32F767ZITX_FLASH.ld")
  set(LINKER_SCRIPT_DEFAULT "${CMAKE_SOURCE_DIR}/Core/STM32F767ZITX_FLASH.ld")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/STM32F767ZITx_FLASH.ld")
  set(LINKER_SCRIPT_DEFAULT "${CMAKE_SOURCE_DIR}/STM32F767ZITx_FLASH.ld")
endif()

# Fail early if the linker script cannot be located
if(LINKER_SCRIPT_DEFAULT STREQUAL "")
  message(FATAL_ERROR
    "No linker script found. Place your *.ld in project root or Core/.")
endif()

# Final linker script path used by the target
set(LINKER_SCRIPT ${LINKER_SCRIPT_DEFAULT})

# =============================================================================
# Target executable
# =============================================================================

# Build the ELF executable from all collected sources
add_executable(${TARGET}
  ${APP_SRC}
  ${HAL_SRC}
  ${CMSIS_TPL_SRC}
  ${BSP_SRC}
  ${USB_MW_SRC}
  ${USB_GEN_SRC}
  ${USB_SRC_FALLBACK}
  ${LWIP_SRC}
  ${STARTUP_ASM}

  # Explicitly added source (even if already part of APP_SRC)
  ${CMAKE_SOURCE_DIR}/Src/app_net.c
)

# =============================================================================
# Include directories
# =============================================================================

# Private includes: only this target sees them
target_include_directories(${TARGET} PRIVATE
  # Project headers
  "${CMAKE_SOURCE_DIR}/Inc"
  "${CMAKE_SOURCE_DIR}/Core/Inc"

  # HAL / CMSIS headers
  "${CMAKE_SOURCE_DIR}/Drivers/STM32F7xx_HAL_Driver/Inc"
  "${CMAKE_SOURCE_DIR}/Drivers/STM32F7xx_HAL_Driver/Inc/Legacy"
  "${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32F7xx/Include"
  "${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Include"

  # BSP / Board / PHY headers
  "${CMAKE_SOURCE_DIR}/Drivers/BSP"
  "${CMAKE_SOURCE_DIR}/Drivers/BSP/Components"
  "${CMAKE_SOURCE_DIR}/Drivers/BSP/Components/Common"
  "${CMAKE_SOURCE_DIR}/Drivers/BSP/Components/lan8742"
  "${CMAKE_SOURCE_DIR}/Drivers/BSP/NUCLEO_F767ZI"

  # USB Device middleware headers
  "${CMAKE_SOURCE_DIR}/Middlewares/ST/STM32_USB_Device_Library/Core/Inc"
  "${CMAKE_SOURCE_DIR}/Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc"

  # CubeMX generated USB headers
  "${CMAKE_SOURCE_DIR}/USB_DEVICE/App"
  "${CMAKE_SOURCE_DIR}/USB_DEVICE/Target"
)

# Add lwIP include paths only if lwIP is enabled
if(ENABLE_LWIP)
  target_include_directories(${TARGET} PRIVATE
    "${LWIP_INC}"
    "${LWIP_SYSTEM_INC}"
  )

  # lwipopts.h include path (if found)
  if(NOT LWIPOPTS_INC STREQUAL "")
    target_include_directories(${TARGET} PRIVATE "${LWIPOPTS_INC}")
  endif()
endif()

# =============================================================================
# Link options
# =============================================================================

# Linker flags:
#  - CPU flags must also be present at link stage
#  - -T selects the linker script
#  - --gc-sections removes unused sections (pairs with -ffunction/-fdata-sections)
#  - Map file output for debugging
#  - nosys/nano specs: small libc, stub syscalls
target_link_options(${TARGET} PRIVATE
  ${MCPU_FLAGS}
  "-T${LINKER_SCRIPT}"
  -Wl,--gc-sections
  -Wl,-Map=${TARGET}.map
  --specs=nosys.specs
  --specs=nano.specs
)

# Link against basic libraries:
# - m: math
# - c: libc (newlib-nano due to nano.specs)
# - gcc: libgcc (compiler runtime)
target_link_libraries(${TARGET} PRIVATE
  m
  c
  gcc
)

# =============================================================================
# Post-build artifacts: HEX/BIN + size
# =============================================================================

# Convert ELF to Intel HEX and raw BIN, then print the size summary.
# Requires CMAKE_OBJCOPY and CMAKE_SIZE from the toolchain file.
add_custom_command(TARGET ${TARGET} POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -Oihex $<TARGET_FILE:${TARGET}> ${TARGET}.hex
  COMMAND ${CMAKE_OBJCOPY} -Obinary $<TARGET_FILE:${TARGET}> ${TARGET}.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET}>
  COMMENT "Generating ${TARGET}.hex/.bin and printing size"
)

# -----------------------------------------------------------------------------
# Configuration summary
# -----------------------------------------------------------------------------
message(STATUS "Target: ${TARGET}")
message(STATUS "Linker script: ${LINKER_SCRIPT}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "ENABLE_LWIP: ${ENABLE_LWIP}")
