@echo off
rem $Id: Combined-2-SignAdditions.cmd 110270 2025-07-16 17:58:34Z klaus.espenlaub@oracle.com $
rem rem @file
rem Windows NT batch script for attestation signing both amd64 and x86.
rem

rem
rem Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
set _MY_PACK_EXTPACK=1
set _MY_PACK_ADDITIONS=0

rem
rem Parse arguments.
rem
set _MY_OPT_UNTAR_DIR=%_MY_SCRIPT_DIR%\..\..\..
for %%i in (%_MY_OPT_UNTAR_DIR%) do set _MY_OPT_UNTAR_DIR=%%~fi
set _MY_OPT_EXTPACK=%_MY_OPT_UNTAR_DIR%\Oracle_VirtualBox_Extension_Pack-%_MY_VER_REV%.vbox-extpack
set _MY_OPT_EXTPACK_ENTERPRISE=%_MY_OPT_UNTAR_DIR%\Oracle_VirtualBox_Extension_Pack-%_MY_VER_REV%-ENTERPRISE.vbox-extpack
set _MY_OPT_BUILD_TYPE=@KBUILD_TYPE@
set _MY_OPT_OUTDIR=%_MY_OPT_UNTAR_DIR%\output

:argument_loop
if ".%1" == "."             goto no_more_arguments

if ".%1" == ".-h"           goto opt_h
if ".%1" == ".-?"           goto opt_h
if ".%1" == "./h"           goto opt_h
if ".%1" == "./H"           goto opt_h
if ".%1" == "./?"           goto opt_h
if ".%1" == ".-help"        goto opt_h
if ".%1" == ".--help"       goto opt_h

if ".%1" == ".-g"                   goto opt_g
if ".%1" == ".--additions"          goto opt_g
if ".%1" == ".-e"                   goto opt_e
if ".%1" == ".--extpack"            goto opt_e
if ".%1" == ".--no-extpack"         goto opt_ne
if ".%1" == ".-o"                   goto opt_o
if ".%1" == ".--outdir"             goto opt_o
if ".%1" == ".-s"                   goto opt_s
if ".%1" == ".--extpack-enterprise" goto opt_s
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

:opt_g
set _MY_PACK_ADDITIONS=1
shift
goto argument_loop

:opt_e
if ".%~2" == "."            goto syntax_error_missing_value
set _MY_OPT_EXTPACK=%~f2
goto argument_loop_next_with_value

:opt_ne
set _MY_PACK_EXTPACK=0
shift
goto argument_loop

:opt_h
echo Toplevel combined package: Prepare both x86 and amd64 for submission.
echo .
echo Usage: Combined-2-SignAdditions.cmd [-o output-dir]
echo            --no-extpack -g/--additions
echo            [-u/--vboxall-dir unpacked-vboxall-dir] [-t build-type]
echo .
echo Default -e/--extpack value:            %_MY_OPT_EXTPACK%
echo Default -s/--extpack-enterprise value: %_MY_OPT_EXTPACK_ENTERPRISE%
echo Default -u/--vboxall-untar-dir value:  %_MY_OPT_UNTAR_DIR%
echo Default -o/--outdir value:             %_MY_OPT_OUTDIR%
echo Default -t/--build-type value:         %_MY_OPT_BUILD_TYPE%
echo .
goto end_failed

:opt_o
if ".%~2" == "."            goto syntax_error_missing_value
set _MY_OPT_OUTDIR=%~f2
goto argument_loop_next_with_value

:opt_s
if ".%~2" == "."            goto syntax_error_missing_value
set _MY_OPT_EXTPACK_ENTERPRISE=%~f2
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

:error_amd64_bindir_not_found
echo syntax error: The AMD64 bin directory was not found: "%_MY_BINDIR_AMD64%"
goto end_failed

:error_x86_bindir_not_found
echo syntax error: The X86 bin directory was not found: "%_MY_BINDIR_X86%"
goto end_failed

:error_amd64_repack_dir_not_found
echo syntax error: The AMD64 repack directory was not found: "%_MY_REPACK_DIR_AMD64%"
goto end_failed

:error_x86_repack_dir_not_found
echo syntax error: The X86 repack directory was not found: "%_MY_REPACK_DIR_X86%"
goto end_failed

:error_extpack_not_found
echo syntax error: Specified extension pack not found: "%_MY_OPT_EXTPACK%"
goto end_failed

:error_enterprise_extpack_not_found
echo syntax error: Specified enterprise extension pack not found: "%_MY_OPT_EXTPACK_ENTERPRISE%"
goto end_failed

:error_extpack_and_additions_together
echo usage error: You can't prepare extPack and GuestAdditions in one call
goto end_failed

:no_more_arguments
rem
rem Validate and adjust specified options.
rem

if not exist "%_MY_OPT_UNTAR_DIR%"      goto error_vboxall_untar_dir_not_found

set _MY_BINDIR_AMD64=%_MY_OPT_UNTAR_DIR%\win.amd64\%_MY_OPT_BUILD_TYPE%\bin
set _MY_BINDIR_X86=%_MY_OPT_UNTAR_DIR%\win.x86\%_MY_OPT_BUILD_TYPE%\bin
if not exist "%_MY_BINDIR_AMD64%"       goto error_amd64_bindir_not_found
if not exist "%_MY_BINDIR_X86%"         goto error_x86_bindir_not_found

if ".%_MY_PACK_EXTPACK%" == ".%_MY_PACK_ADDITIONS%" goto error_extpack_and_additions_together

set _MY_REPACK_DIR_AMD64=%_MY_OPT_UNTAR_DIR%\win.amd64\%_MY_OPT_BUILD_TYPE%\repack
set _MY_REPACK_DIR_X86=%_MY_OPT_UNTAR_DIR%\win.x86\%_MY_OPT_BUILD_TYPE%\repack
if ".%_MY_PACK_ADDITIONS%" == ".0" goto skip_additions_packing_options
set _MY_REPACK_DIR_AMD64=%_MY_OPT_UNTAR_DIR%\win.amd64\%_MY_OPT_BUILD_TYPE%\repackadd
set _MY_REPACK_DIR_X86=%_MY_OPT_UNTAR_DIR%\win.x86\%_MY_OPT_BUILD_TYPE%\repackadd
:skip_additions_packing_options
if not exist "%_MY_REPACK_DIR_AMD64%"   goto error_amd64_repack_dir_not_found
if not exist "%_MY_REPACK_DIR_X86%"     goto error_x86_repack_dir_not_found

rem Make sure the output dir exists.
if not exist "%_MY_OPT_OUTDIR%"     (mkdir "%_MY_OPT_OUTDIR%" || goto end_failed)

rem
rem ExtPack section
rem
if ".%_MY_PACK_EXTPACK%" == ".0" goto skip_extpack_packing

if not exist "%_MY_OPT_EXTPACK%"        goto error_extpack_not_found
if not ".%_MY_OPT_EXTPACK_ENTERPRISE%" == "." if not exist "%_MY_OPT_EXTPACK_ENTERPRISE%" goto error_enterprise_extpack_not_found

rem
rem Install the extpack in the bin directories.
rem Note! Not really necessary, but whatever.
rem
echo on
copy /y "%_MY_OPT_EXTPACK%" "%_MY_BINDIR_AMD64%\Oracle_VirtualBox_Extension_Pack.vbox-extpack" || goto end_failed
copy /y "%_MY_OPT_EXTPACK%"   "%_MY_BINDIR_X86%\Oracle_VirtualBox_Extension_Pack.vbox-extpack" || goto end_failed
@echo off

rem
rem Do the packing of ExtPack
rem
echo **************************************************************************
echo Packing AMD64 drivers
echo **************************************************************************

echo "ERROR: this script is for GuestAdditions only"
goto end_failed


echo .
echo **************************************************************************
echo Packing X86 drivers
echo **************************************************************************

echo "ERROR: this script is for GuestAdditions only"
goto end_failed

:skip_extpack_packing

rem
rem GuestAdditions section
rem
if ".%_MY_PACK_ADDITIONS%" == ".0" goto skip_additions_packing

rem
rem Do the packing of GuestAdditions
rem
echo **************************************************************************
echo Preparing signing AMD64 additions
echo **************************************************************************
cd /d "%_MY_REPACK_DIR_AMD64%" || goto end_failed
del /q VBoxDrivers-%_MY_VER_REV%-amd64.cab.*.zip 2>NUL > NUL
del /q %_MY_OPT_OUTDIR%\VBoxDrivers-%_MY_VER_REV%-amd64.cab.Signed.zip 2>NUL > NUL
SET wget=%KBUILD_DEVTOOLS%/win.amd64/bin/wget

echo **************************************************************************
echo Preparing signing X86 drivers
echo **************************************************************************
cd /d "%_MY_REPACK_DIR_X86%" || goto end_failed
del /q VBoxDrivers-%_MY_VER_REV%-x86.cab.*.zip 2>NUL > NUL
del /q %_MY_OPT_OUTDIR%\VBoxDrivers-%_MY_VER_REV%-x86.cab.Signed.zip 2>NUL > NUL
rem SET wget=%KBUILD_DEVTOOLS%/win.x86/bin/wget

cd /d "%_MY_SAVED_CD%"

echo **************************************************************************
echo Signing signing AMD64 and x86 additions
echo **************************************************************************
del /q VBoxDrivers-%_MY_VER_REV%-amd64.cab.*.zip 2>NUL > NUL
del /q %_MY_OPT_OUTDIR%\VBoxDrivers-%_MY_VER_REV%-amd64.cab.Signed.zip 2>NUL > NUL
del /q VBoxDrivers-%_MY_VER_REV%-x86.cab.*.zip 2>NUL > NUL
del /q %_MY_OPT_OUTDIR%\VBoxDrivers-%_MY_VER_REV%-x86.cab.Signed.zip 2>NUL > NUL

call C:\Utils\attestation\AttestationSigning.bat "VirtualBox GA drivers" "amd64" "%_MY_VER_REV%" "%_MY_OPT_OUTDIR%\VBoxDrivers-%_MY_VER_REV%-amd64.cab"
call C:\Utils\attestation\AttestationSigning.bat "VirtualBox GA drivers" "x86" "%_MY_VER_REV%" "%_MY_OPT_OUTDIR%\VBoxDrivers-%_MY_VER_REV%-x86.cab"

:skip_additions_packing

rem
rem Generate script for taking the next step.
rem
set _MY_NEXT_SCRIPT_SHORT_NAME=Combined-3-Repack.cmd
set _MY_NEXT_SCRIPT=%_MY_OPT_OUTDIR%\%_MY_NEXT_SCRIPT_SHORT_NAME%
if ".%_MY_PACK_ADDITIONS%" == ".0" goto generate_legacy_script
set _MY_NEXT_SCRIPT_SHORT_NAME=Combined-3-RepackAdditions.cmd
set _MY_NEXT_SCRIPT=%_MY_OPT_OUTDIR%\%_MY_NEXT_SCRIPT_SHORT_NAME%
echo cd /d "%cd%" > "%_MY_NEXT_SCRIPT%"
echo call "%_MY_SCRIPT_DIR%%_MY_NEXT_SCRIPT_SHORT_NAME%" ^
    --vboxall-untar-dir "%_MY_OPT_UNTAR_DIR%" ^
    --outdir "%_MY_OPT_OUTDIR%" ^
    --build-type "%_MY_OPT_BUILD_TYPE%" %%* >> "%_MY_NEXT_SCRIPT%"
goto show_next_steps
:generate_legacy_script
echo cd /d "%cd%" > "%_MY_NEXT_SCRIPT%"
echo call "%_MY_SCRIPT_DIR%%_MY_NEXT_SCRIPT_SHORT_NAME%" --extpack "%_MY_OPT_EXTPACK%" ^
    --extpack-enterprise "%_MY_OPT_EXTPACK_ENTERPRISE%" ^
    --vboxall-untar-dir "%_MY_OPT_UNTAR_DIR%" ^
    --outdir "%_MY_OPT_OUTDIR%" ^
    %_MY_OPT_SCRIPT_SKIPEXTPACK_PARAM% %_MY_OPT_SCRIPT_ADDITIONS_PARAM% ^
    --build-type "%_MY_OPT_BUILD_TYPE%" %%* >> "%_MY_NEXT_SCRIPT%"

:show_next_steps
rem
rem Instructions on what to do next.
rem
echo **************************************************************************
echo * Second step is done.
echo *
echo * Created:
echo *     %_MY_OPT_OUTDIR%\VBoxDrivers-%_MY_VER_REV%-amd64.cab.Signed.zip
echo *     %_MY_OPT_OUTDIR%\VBoxDrivers-%_MY_VER_REV%-x86.cab.Signed.zip
echo *
echo * Next steps:
echo *   1. "%_MY_NEXT_SCRIPT%" --signed-x86 {zip} --signed-amd64 {zip} %_MY_OPT_SCRIPT_SKIPEXTPACK_PARAM% %_MY_OPT_SCRIPT_ADDITIONAL_PARAMS%
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

