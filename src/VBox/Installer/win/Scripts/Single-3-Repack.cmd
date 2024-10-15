@echo off
rem $Id: Single-3-Repack.cmd 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
rem rem @file
rem Windows NT batch script for repacking signed amd64 or x86 drivers.
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
rem Globals and checks for required enviornment variables.
rem
if ".%KBUILD_DEVTOOLS%" == "." (echo KBUILD_DEVTOOLS is not set & goto end_failed)
if ".%KBUILD_BIN_PATH%" == "." (echo KBUILD_BIN_PATH is not set & goto end_failed)
set _MY_SCRIPT_DIR=%~dp0
set _MY_SAVED_CD=%CD%
set _MY_VER_REV=@VBOX_VERSION_STRING@r@VBOX_SVN_REV@

rem
rem Parse arguments.
rem
set _MY_OPT_UNTAR_DIR=%_MY_SCRIPT_DIR%\..\
for %%i in (%_MY_OPT_UNTAR_DIR%) do set _MY_OPT_UNTAR_DIR=%%~fi
set _MY_OPT_EXTPACK=%_MY_OPT_UNTAR_DIR%\Oracle_VirtualBox_Extension_Pack-%_MY_VER_REV%.vbox-extpack
set _MY_OPT_EXTPACK_ENTERPRISE=%_MY_OPT_UNTAR_DIR%\Oracle_VirtualBox_Extension_Pack-%_MY_VER_REV%-ENTERPRISE.vbox-extpack
set _MY_OPT_BUILD_TYPE=@KBUILD_TYPE@
set _MY_OPT_OUTDIR=%_MY_OPT_UNTAR_DIR%\output
set _MY_OPT_SIGNED=
set _MY_OPT_NOEXTPACK=

:argument_loop
if ".%1" == "."             goto no_more_arguments

if ".%1" == ".-h"           goto opt_h
if ".%1" == ".-?"           goto opt_h
if ".%1" == "./h"           goto opt_h
if ".%1" == "./H"           goto opt_h
if ".%1" == "./?"           goto opt_h
if ".%1" == ".-help"        goto opt_h
if ".%1" == ".--help"       goto opt_h

if ".%1" == ".-e"                   goto opt_e
if ".%1" == ".--extpack"            goto opt_e
if ".%1" == ".-n"                   goto opt_n
if ".%1" == ".--no-extpack"         goto opt_n
if ".%1" == ".-o"                   goto opt_o
if ".%1" == ".--outdir"             goto opt_o
if ".%1" == ".-s"                   goto opt_s
if ".%1" == ".--extpack-enterprise" goto opt_s
if ".%1" == ".--signed"             goto opt_signed
if ".%1" == ".-t"                   goto opt_t
if ".%1" == ".--build-type"         goto opt_t
if ".%1" == ".-u"                   goto opt_u
if ".%1" == ".--vboxall-untar-dir"  goto opt_u
echo syntax error: Unknown option: %1
echo               Try --help to list valid options.
goto end_failed

:argument_loop_next_with_value
shift
shift
goto argument_loop

:opt_e
if ".%~2" == "."             goto syntax_error_missing_value
set _MY_OPT_EXTPACK=%~f2
goto argument_loop_next_with_value

:opt_h
echo Toplevel combined package: Repack the installer and extpacks.
echo .
echo Usage: Combined-3-Repack.cmd [-o output-dir] [-e/--extpack puel.vbox-extpack]
echo            [-s/--extpack-enterprise puel-enterprise.vbox-extpack]
echo            [-u/--vboxall-dir unpacked-vboxall-dir] [-t build-type]
echo            [--signed signed.zip]
echo
echo .
echo Default -e/--extpack value:            %_MY_OPT_EXTPACK%
echo Default -s/--extpack-enterprise value: %_MY_OPT_EXTPACK_ENTERPRISE%
echo Default -u/--vboxall-untar-dir value:  %_MY_OPT_UNTAR_DIR%
echo Default -o/--outdir value:             %_MY_OPT_OUTDIR%
echo Default -t/--build-type value:         %_MY_OPT_BUILD_TYPE%
echo .
goto end_failed

:opt_n
set _MY_OPT_NOEXTPACK=1
shift
goto argument_loop

:opt_o
if ".%~2" == "."            goto syntax_error_missing_value
set _MY_OPT_OUTDIR=%~f2
goto argument_loop_next_with_value

:opt_s
if ".%~2" == "."            goto syntax_error_missing_value
set _MY_OPT_EXTPACK_ENTERPRISE=%~f2
goto argument_loop_next_with_value

:opt_signed
if ".%~2" == "."            goto syntax_error_missing_value
set _MY_OPT_SIGNED=%~f2
goto argument_loop_next_with_value

:opt_t
if ".%~2" == "."            goto syntax_error_missing_value
set _MY_OPT_BUILD_TYPE=%~2
goto argument_loop_next_with_value

:opt_u
if ".%~2" == "."            goto syntax_error_missing_value
set _MY_OPT_UNTAR_DIR=%~f2
goto argument_loop_next_with_value


:syntax_error_missing_value
echo syntax error: missing or empty option value after %1
goto end_failed


:error_vboxall_untar_dir_not_found
echo syntax error: The VBoxAll untar directory was not found: "%_MY_OPT_UNTAR_DIR%"
goto end_failed

:error_bindir_not_found
echo syntax error: The bin directory was not found: "%_MY_BINDIR%"
goto end_failed

:error_repack_dir_not_found
echo syntax error: The repack directory was not found: "%_MY_REPACK_DIR%"
goto end_failed

:error_extpack_not_found
echo syntax error: Specified extension pack not found: "%_MY_OPT_EXTPACK%"
goto end_failed

:error_enterprise_extpack_not_found
echo syntax error: Specified enterprise extension pack not found: "%_MY_OPT_EXTPACK_ENTERPRISE%"
goto end_failed

:error_signed_not_found
echo syntax error: Zip with signed drivers not found: "%_MY_OPT_SIGNED%"
goto end_failed


:no_more_arguments
rem
rem Validate and adjust specified options.
rem

if not exist "%_MY_OPT_UNTAR_DIR%"      goto error_vboxall_untar_dir_not_found

set _MY_BINDIR=%_MY_OPT_UNTAR_DIR%\bin
if not exist "%_MY_BINDIR%"             goto error_bindir_not_found

set _MY_REPACK_DIR=%_MY_OPT_UNTAR_DIR%\repack
if not exist "%_MY_REPACK_DIR%"         goto error_repack_dir_not_found

if ".%_MY_OPT_NOEXTPACK%" == ".1"       goto no_enterprise_check
if not exist "%_MY_OPT_EXTPACK%"        goto error_extpack_not_found
if not ".%_MY_OPT_EXTPACK_ENTERPRISE%" == "." if not exist "%_MY_OPT_EXTPACK_ENTERPRISE%" goto error_enterprise_extpack_not_found
:no_enterprise_check

if not exist "%_MY_OPT_SIGNED%"         goto error_signed_not_found

rem Make sure the output dir exists.
if not exist "%_MY_OPT_OUTDIR%"     (mkdir "%_MY_OPT_OUTDIR%" || goto end_failed)

rem
rem Unpacking the driver zip.
rem
echo **************************************************************************
echo * Unpacking signed drivers...
echo **************************************************************************
cd /d "%_MY_REPACK_DIR%" || goto end_failed
call "%_MY_REPACK_DIR%\UnpackBlessedDrivers.cmd" -n -b "%_MY_BINDIR%" -i "%_MY_OPT_SIGNED%" || goto end_failed
echo .


rem
rem Do the work.
rem
echo **************************************************************************
echo * Repackaging installers
echo **************************************************************************
echo * Compiling WIX...
cd /d "%_MY_REPACK_DIR%" || goto end_failed
for %%i in (1-*.cmd) do (call %%i || goto end_failed)
echo .

echo * Linking WIX...
for %%i in (2-*.cmd) do (call %%i || goto end_failed)
echo .

echo * Applying language patches to MSI...
for %%i in (3-*.cmd) do (call %%i || goto end_failed)
echo .

echo * Creating multi arch installer...
for %%i in (4-*.cmd) do (call %%i || goto end_failed)
echo .


set _MY_OUT_FILES=
cd /d "%_MY_REPACK_DIR%" || goto end_failed
for %%i in (VBoxMerge*msm) do (
    copy /y "%%i" "%_MY_OPT_OUTDIR%" || goto end_failed
    call set _MY_OUT_FILES=%%_MY_OUT_FILES%% %%~nxi
)
for %%i in (VirtualBox-*MultiArch*exe) do (
    copy /y "%%i" "%_MY_OPT_OUTDIR%" || goto end_failed
    call set _MY_OUT_FILES=%%_MY_OUT_FILES%% %%~nxi
)


if ".%_MY_OPT_NOEXTPACK%" == ".1" goto no_enterprise_repacking
rem
rem Repack the extension packs.
rem
echo **************************************************************************
echo * Repacking extension packs.
echo **************************************************************************
cd /d "%_MY_REPACK_DIR%" || goto end_failed

echo * Regular PUEL...
set _MY_TMP_OUT=%_MY_OPT_EXTPACK%
for %%i in (%_MY_TMP_OUT%) do (
    set _MY_TMP_OUT=%_MY_OPT_OUTDIR%\%%~nxi
    call set _MY_OUT_FILES=%%_MY_OUT_FILES%% %%~nxi
)
call "%_MY_REPACK_DIR%\RepackExtPack.cmd" --bindir-amd64 "%_MY_BINDIR%" --bindir-x86 "%_MY_BINDIR%" ^
    --input "%_MY_OPT_EXTPACK%" --output "%_MY_TMP_OUT%" || goto end_failed

if ".%_MY_OPT_EXTPACK_ENTERPRISE%" == "." goto no_enterprise_repacking
echo * Enterprise PUEL...
set _MY_TMP_OUT=%_MY_OPT_EXTPACK_ENTERPRISE%
for %%i in (%_MY_TMP_OUT%) do (
    set _MY_TMP_OUT=%_MY_OPT_OUTDIR%\%%~nxi
    call set _MY_OUT_FILES=%%_MY_OUT_FILES%% %%~nxi
)
call "%_MY_REPACK_DIR%\RepackExtPack.cmd" --bindir-amd64 "%_MY_BINDIR%" --bindir-x86 "%_MY_BINDIR%" ^
    --input "%_MY_OPT_EXTPACK_ENTERPRISE%" --output "%_MY_TMP_OUT%" || goto end_failed
:no_enterprise_repacking
@cd /d "%_MY_SAVED_CD%"

rem
rem That's that.
rem
echo **************************************************************************
echo * The third and final step is done.
echo *
echo * Successfully created:
for %%i in (%_MY_OUT_FILES%) do echo *    "%_MY_OPT_OUTDIR%\%%i"
goto end


:end_failed
@cd /d "%_MY_SAVED_CD%"
@endlocal
@endlocal
@echo * Failed!
@exit /b 1

:end
@cd /d "%_MY_SAVED_CD%"
@endlocal
@endlocal
