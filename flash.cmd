@echo off
REM ---------------------------------------------------------------------------
REM flash.cmd
REM ---------------------------------------------------------------------------
REM Windows flashing script for STM32 using ST-LINK via STM32CubeProgrammer CLI.
REM
REM Function:
REM   - Checks if the HEX file exists
REM   - Programs the device over SWD
REM   - Verifies flash contents
REM   - Resets the target MCU
REM   - Fails fast on errors
REM ---------------------------------------------------------------------------

REM Path to the generated HEX file (from CMake/Ninja build)
set HEX=build\nucleo_f767_base.hex

REM Check if the HEX file exists before flashing
if not exist %HEX% (
  echo HEX not found: %HEX%
  echo Build first!
  exit /b 1
)

REM Informational output
echo Flashing %HEX% via ST-LINK (verify + reset)...

REM Invoke STM32CubeProgrammer CLI:
REM  -c port=SWD : connect via SWD using ST-LINK
REM  -w <file>   : write HEX file to flash
REM  -v          : verify after programming
REM  -rst        : reset target after flashing
"C:\ST\STM32CubeCLT_1.20.0\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" ^
  -c port=SWD ^
  -w %HEX% ^
  -v ^
  -rst

REM Check the exit code of the programmer
REM Non-zero means flashing failed
if %ERRORLEVEL% NEQ 0 (
  echo FLASH FAILED
  exit /b 1
)

REM Success message
echo Flash successful!
