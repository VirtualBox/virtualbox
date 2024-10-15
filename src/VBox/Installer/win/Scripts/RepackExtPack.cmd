@echo off
rem $Id: RepackExtPack.cmd 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
rem rem @file
rem Windows NT batch script for repacking an extension pack with blessed .r0 files.
rem

rem
rem Copyright (C) 2018-2024 Oracle and/or its affiliates.
rem
rem This file is part of VirtualBox base platform packages, as
rem available from https://www.virtualbox.org.
rem
rem This program is free software; you can redistribute it and/or
rem modify it under the terms of the GNU General Public License
rem as published by the Free Software Foundation, in version 3 of the
rem License.
rem
rem This program is distributed in the hope that it will be useful, but
rem WITHOUT ANY WARRANTY; without even the implied warranty of
rem MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
rem General Public License for more details.
rem
rem You should have received a copy of the GNU General Public License
rem along with this program; if not, see <https://www.gnu.org/licenses>.
rem
rem SPDX-License-Identifier: GPL-3.0-only
rem


setlocal ENABLEEXTENSIONS
setlocal

rem
rem Check for environment variables we need.
rem
if ".%KBUILD_DEVTOOLS%" == "." (echo KBUILD_DEVTOOLS is not set & goto end_failed)
rem if ".%KBUILD_BIN_PATH%" == "." (echo KBUILD_BIN_PATH is not set & goto end_failed)

rem
rem Parse arguments.
rem
set _MY_OPT_BINDIR_X86=..\..\..\win.x86\@KBUILD_TYPE@\bin
set _MY_OPT_BINDIR_AMD64=..\..\..\win.amd64\@KBUILD_TYPE@\bin
set _MY_OPT_INPUT=
set _MY_OPT_OUTPUT=
set _MY_OPT_STAGE_DIR=.\repack-extpack-%RANDOM%
for %%i in (%_MY_OPT_STAGE_DIR%) do set _MY_OPT_STAGE_DIR=%%~fi
set _MY_OPT_SIGN_CAT=1

:argument_loop
if ".%1" == "."             goto no_more_arguments

if ".%1" == ".-h"           goto opt_h
if ".%1" == ".-?"           goto opt_h
if ".%1" == "./h"           goto opt_h
if ".%1" == "./H"           goto opt_h
if ".%1" == "./?"           goto opt_h
if ".%1" == ".-help"        goto opt_h
if ".%1" == ".--help"       goto opt_h

if ".%1" == ".-a"           goto opt_a
if ".%1" == ".--bindir-amd64" goto opt_a
if ".%1" == ".-b"           goto opt_b
if ".%1" == ".--bindir-x86" goto opt_b
if ".%1" == ".-i"           goto opt_i
if ".%1" == ".--input"      goto opt_i
if ".%1" == ".-o"           goto opt_o
if ".%1" == ".--output"     goto opt_o
if ".%1" == ".-s"           goto opt_s
if ".%1" == ".--stage-dir"  goto opt_s
echo syntax error: Unknown option: %1
echo               Try --help to list valid options.
goto end_failed

:argument_loop_next_with_value
shift
shift
goto argument_loop

:opt_a
if ".%~2" == "."            goto syntax_error_missing_value
set _MY_OPT_BINDIR_AMD64=%~f2
goto argument_loop_next_with_value

:opt_b
if ".%~2" == "."            goto syntax_error_missing_value
set _MY_OPT_BINDIR_X86=%~f2
goto argument_loop_next_with_value

:opt_h
echo This script repacks an extension pack replacing windows .r0 files with
echo blessed copies from the bin directory.  The ASSUMPTION here is that prior
echo to invoking this script, the UnpackBlessedDrivers.cmd script was executed
echo both for win.amd64 and win.x86.
echo .
echo Usage: RepackExtPack.cmd [-b bindir-x86] [-a bindir-amd64] [-s staging-dir]
echo            -i input.vbox-extpack -o output.vbox-extpack
echo .
echo Warning! This script should normally be invoked from the win.x86 repack directory.
goto end_failed

:opt_i
if ".%~2" == "."            goto syntax_error_missing_value
set _MY_OPT_INPUT=%~f2
goto argument_loop_next_with_value

:opt_o
if ".%~2" == "."            goto syntax_error_missing_value
set _MY_OPT_OUTPUT=%~f2
goto argument_loop_next_with_value

:opt_s
if ".%~2" == "."            goto syntax_error_missing_value
set _MY_OPT_STAGE_DIR=%~f2
goto argument_loop_next_with_value

:syntax_error_missing_value
echo syntax error: missing or empty option value after %1
goto end_failed

:error_bindir_amd64_does_not_exist
echo syntax error: Specified AMD64 BIN directory does not exist: "%_MY_OPT_BINDIR_AMD64%"
goto end_failed

:error_bindir_x86_does_not_exist
echo syntax error: Specified x86 BIN directory does not exist: "%_MY_OPT_BINDIR_X86%"
goto end_failed

:error_input_not_found
echo error: Input file does not exist: "%_MY_OPT_INPUT%"
goto end_failed

:error_stage_dir_exists
echo error: Temporary staging directory exists: "%_MY_OPT_STAGE_DIR%"
goto end_failed

:no_more_arguments
rem
rem Validate and adjust specified options.
rem
if not exist "%_MY_OPT_BINDIR_X86%"   goto error_bindir_x86_does_not_exist
if not exist "%_MY_OPT_BINDIR_AMD64%" goto error_bindir_amd64_does_not_exist

if ".%_MY_OPT_INPUT%" == "."        set _MY_OPT_INPUT=%_MY_OPT_BINDIR_X86%\Oracle_VirtualBox_Extension_Pack.vbox-extpack
if not exist "%_MY_OPT_INPUT%"      goto error_input_not_found

if ".%_MY_OPT_OUTPUT%" == "."       for %%i in ("%_MY_OPT_INPUT%") do set _MY_OPT_OUTPUT=.\%%~nxi

rem Make _MY_OPT_STAGE_DIR absolute.
if exist "%_MY_OPT_STAGE_DIR%"      goto error_stage_dir_exists

rem
rem Modify PATH to facilitate using our zip, gzip and manifest tools
rem
rem TODO: Use RTTar for creation too.
rem TODO: Not sure how well the bsdtar output actually work with 5.1...
rem TODO: Check whether we need stupid cygwin to get the right execute bits (x) on unix.
rem
set PATH=%PATH%;%_MY_OPT_BINDIR_AMD64%
set _MY_TOOL_TAR_EXPAND="%_MY_OPT_BINDIR_AMD64%\tools\RTTar.exe" -x
set _MY_TOOL_TAR_CREATE="%KBUILD_DEVTOOLS%\win.x86\gnuwin32\r1\bin\bsdtar.exe" -c --format ustar
set _MY_TOOL_GZIP=%_MY_OPT_BINDIR_AMD64%\tools\RTGzip.exe
set _MY_TOOL_MANIFEST=%_MY_OPT_BINDIR_AMD64%\tools\RTManifest.exe

rem
rem Unpack the extension pack.
rem
echo * Unpacking "%_MY_OPT_INPUT" to "%_MY_OPT_STAGE_DIR%"...
mkdir "%_MY_OPT_STAGE_DIR%" || goto end_failed
%_MY_TOOL_TAR_EXPAND% -vzf "%_MY_OPT_INPUT%" -C "%_MY_OPT_STAGE_DIR%" || goto end_failed_cleanup

rem
rem Copy over the blessed .r0 files.
rem
echo * Copying over blessed .r0 binaries...
if not exist "%_MY_OPT_STAGE_DIR%\win.x86" goto no_win_x86
for %%i in ("%_MY_OPT_STAGE_DIR%\win.x86\*.r0") do (
    echo -=- %%i
    copy /y "%_MY_OPT_BINDIR_X86%\%%~nxi" "%_MY_OPT_STAGE_DIR%\win.x86" || goto end_failed_cleanup
)
:no_win_x86

for %%i in ("%_MY_OPT_STAGE_DIR%\win.amd64\*.r0") do (
    echo -=- %%i
    copy /y "%_MY_OPT_BINDIR_AMD64%\%%~nxi" "%_MY_OPT_STAGE_DIR%\win.amd64" || goto end_failed_cleanup
)

rem
rem Recreate the manifest.
rem
echo * Collecting files for manifest...
set _MY_MANIFEST_FILES=
for /D %%d in ("%_MY_OPT_STAGE_DIR%\*") do (
    for %%f in ("%%d\*") do call set _MY_MANIFEST_FILES=%%_MY_MANIFEST_FILES%% %%~nxd/%%~nxf
)
for %%f in ("%_MY_OPT_STAGE_DIR%\*") do (
    if not "%%~nxf" == "ExtPack.manifest" if not "%%~nxf" == "ExtPack.signature" call set _MY_MANIFEST_FILES=%%_MY_MANIFEST_FILES%% %%~nxf
)
rem echo _MY_MANIFEST_FILES=%_MY_MANIFEST_FILES%

echo * Creating manifest...
echo on
"%_MY_TOOL_MANIFEST%" --manifest "%_MY_OPT_STAGE_DIR%\ExtPack.manifest" --chdir "%_MY_OPT_STAGE_DIR%" %_MY_MANIFEST_FILES% || goto end_failed_cleanup
@echo off

rem
rem Repackage the damn thing.
rem
@echo * Packing extension pack...

echo on
%_MY_TOOL_TAR_CREATE% -vf "%_MY_OPT_OUTPUT%.tmp" -C "%_MY_OPT_STAGE_DIR%" . || goto end_failed_cleanup
"%_MY_TOOL_GZIP%" -9 -n "%_MY_OPT_OUTPUT%.tmp"       || goto end_failed_cleanup
move /Y "%_MY_OPT_OUTPUT%.tmp.gz" "%_MY_OPT_OUTPUT%" || goto end_failed_cleanup
echo off

rem
rem Cleanup and we're good.
rem
echo * Cleaning up...
rmdir /s /q "%_MY_OPT_STAGE_DIR%"

echo * Successfully created: "%_MY_OPT_OUTPUT%
goto end

:end_failed_cleanup
@rmdir /s /q "%_MY_OPT_STAGE_DIR%"
@if exist "%_MY_OPT_OUTPUT%.tmp"    del "%_MY_OPT_OUTPUT%.tmp"
@if exist "%_MY_OPT_OUTPUT%.tmp.gz" del "%_MY_OPT_OUTPUT%.tmp.gz"

:end_failed
@endlocal
@endlocal
@echo * Failed!
@exit /b 1

:end
@endlocal
@endlocal

