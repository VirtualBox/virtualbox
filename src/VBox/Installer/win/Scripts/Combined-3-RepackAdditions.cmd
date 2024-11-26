@echo off
rem $Id: Combined-3-RepackAdditions.cmd 107166 2024-11-26 09:03:23Z andreas.loeffler@oracle.com $
rem rem @file
rem Windows NT batch script for repacking signed amd64 and x86 drivers.
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
set _MY_OPT_UNTAR_DIR=%_MY_SCRIPT_DIR%\..\..\..\
for %%i in (%_MY_OPT_UNTAR_DIR%) do set _MY_OPT_UNTAR_DIR=%%~fi
set _MY_OPT_BUILD_TYPE=@KBUILD_TYPE@
set _MY_OPT_OUTDIR=%_MY_OPT_UNTAR_DIR%\output
set _MY_OPT_SRC_DIR=%_MY_SCRIPT_DIR%\resources\

:argument_loop
if ".%1" == "."             goto no_more_arguments

if ".%1" == ".-h"           goto opt_h
if ".%1" == ".-?"           goto opt_h
if ".%1" == "./h"           goto opt_h
if ".%1" == "./H"           goto opt_h
if ".%1" == "./?"           goto opt_h
if ".%1" == ".-help"        goto opt_h
if ".%1" == ".--help"       goto opt_h

if ".%1" == ".-o"                   goto opt_o
if ".%1" == ".--outdir"             goto opt_o
if ".%1" == ".-s"                   goto opt_s
if ".%1" == ".--source"             goto opt_s
if ".%1" == ".--signed-amd64"       goto opt_signed_amd64
if ".%1" == ".--signed-x86"         goto opt_signed_x86
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

:opt_h
echo Toplevel combined package: Repack the guest additions.
echo .
echo Usage: Combined-3-RepackAdditions.cmd [-o output-dir]
echo            [-u/--vboxall-dir unpacked-vboxall-dir] [-t build-type]
echo            [--signed-amd64 signed-amd64.zip]
echo            [--signed-x86 signed-x86.zip]
echo
echo .
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
set _MY_OPT_SRC_DIR=%~f2
goto argument_loop_next_with_value

:opt_signed_amd64
if ".%~2" == "."            goto syntax_error_missing_value
set _MY_OPT_SIGNED_AMD64=%~f2
goto argument_loop_next_with_value

:opt_signed_x86
if ".%~2" == "."            goto syntax_error_missing_value
set _MY_OPT_SIGNED_X86=%~f2
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

:error_signed_amd64_not_found
echo syntax error: Zip with signed AMD64 drivers not found: "%_MY_OPT_SIGNED_AMD64%"
goto end_failed

:error_signed_x86_not_found
echo syntax error: Zip with signed X86 drivers not found: "%_MY_OPT_SIGNED_X86%"
goto end_failed

:error_src_dir_not_found
echo syntax error: src directory not found: "%_MY_OPT_SRC_DIR%"
goto end_failed


:no_more_arguments
rem
rem Validate and adjust specified options.
rem

if not exist "%_MY_OPT_UNTAR_DIR%"      goto error_vboxall_untar_dir_not_found

set _MY_BINDIR_AMD64=%_MY_OPT_UNTAR_DIR%\win.amd64\%_MY_OPT_BUILD_TYPE%\bin\additions
set _MY_BINDIR_X86=%_MY_OPT_UNTAR_DIR%\win.x86\%_MY_OPT_BUILD_TYPE%\bin\additions
if not exist "%_MY_BINDIR_AMD64%"       goto error_amd64_bindir_not_found
if not exist "%_MY_BINDIR_X86%"         goto error_x86_bindir_not_found

set _MY_REPACK_DIR_AMD64=%_MY_OPT_UNTAR_DIR%\win.amd64\%_MY_OPT_BUILD_TYPE%\repackadd
set _MY_REPACK_DIR_X86=%_MY_OPT_UNTAR_DIR%\win.x86\%_MY_OPT_BUILD_TYPE%\repackadd
if not exist "%_MY_REPACK_DIR_AMD64%"   goto error_amd64_repack_dir_not_found
if not exist "%_MY_REPACK_DIR_X86%"     goto error_x86_repack_dir_not_found

if not ".%_MY_OPT_SIGNED_AMD64%" == "." goto skip_set_default_amd64_signed
set _MY_OPT_SIGNED_AMD64="%_MY_OPT_OUTDIR%/VBoxDrivers-@VBOX_VERSION_STRING@r@VBOX_SVN_REV@-amd64.cab.Signed.zip"
:skip_set_default_amd64_signed

if not ".%_MY_OPT_SIGNED_X86%" == "." goto skip_set_default_x86_signed
set _MY_OPT_SIGNED_X86="%_MY_OPT_OUTDIR%/VBoxDrivers-@VBOX_VERSION_STRING@r@VBOX_SVN_REV@-x86.cab.Signed.zip"
:skip_set_default_x86_signed

if not exist "%_MY_OPT_SIGNED_AMD64%"   goto error_signed_amd64_not_found
if not exist "%_MY_OPT_SIGNED_X86%"     goto error_signed_x86_not_found

rem Make sure the output dir exists.
if not exist "%_MY_OPT_OUTDIR%"     (mkdir "%_MY_OPT_OUTDIR%" || goto end_failed)

if not exist "%_MY_OPT_SRC_DIR%"              goto error_src_dir_not_found

rem
rem Unpacking the two driver zips.
rem
echo **************************************************************************
echo * AMD64: Unpacking signed drivers...
echo **************************************************************************
cd /d "%_MY_REPACK_DIR_AMD64%" || goto end_failed
call "%_MY_REPACK_DIR_AMD64%\UnpackBlessedDrivers.cmd" -n -b "%_MY_BINDIR_AMD64%" -i "%_MY_OPT_SIGNED_AMD64%" --guest-additions || goto end_failed
echo .

echo **************************************************************************
echo * X86: Unpacking signed drivers...
echo **************************************************************************
cd /d "%_MY_REPACK_DIR_X86%" || goto end_failed
call "%_MY_REPACK_DIR_X86%\UnpackBlessedDrivers.cmd" -n -b "%_MY_BINDIR_X86%" -i "%_MY_OPT_SIGNED_X86%" --guest-additions || goto end_failed
echo .


rem
rem Building amd64 installer
rem
echo **************************************************************************
echo * Building amd64 installer
echo **************************************************************************

del %_MY_OPT_UNTAR_DIR%\win.amd64\release\bin\additions\VBoxWindowsAdditions-amd64.exe
cp %_MY_REPACK_DIR_AMD64%\..\obj\uninst.exe %_MY_REPACK_DIR_AMD64%

rem TBD: that has to be converted to invoke auto-generated .cmd

%KBUILD_BIN_PATH%\kmk_redirect.exe -C %_MY_OPT_SRC_DIR% ^
        -E "PATH_OUT=%_MY_REPACK_DIR_AMD64%\.." ^
        -E "PATH_TARGET=%_MY_REPACK_DIR_AMD64%" ^
        -E "PATH_TARGET_X86=%_MY_REPACK_DIR_X86%\resources" ^
        -E "VBOX_PATH_ADDITIONS_WIN_X86=%_MY_REPACK_DIR_AMD64%\..\bin\additions" ^
        -E "VBOX_VENDOR=Oracle Corporation" -E "VBOX_VENDOR_SHORT=Oracle" -E "VBOX_PRODUCT=Oracle VirtualBox" ^
        -E "VBOX_C_YEAR=@VBOX_C_YEAR@" -E "VBOX_VERSION_STRING=@VBOX_VERSION_STRING@" -E "VBOX_VERSION_STRING_RAW=@VBOX_VERSION_STRING_RAW@" ^
        -E "VBOX_VERSION_MAJOR=@VBOX_VERSION_MAJOR@" -E "VBOX_VERSION_MINOR=@VBOX_VERSION_MINOR@" -E "VBOX_VERSION_BUILD=@VBOX_VERSION_BUILD@" -E "VBOX_SVN_REV=@VBOX_SVN_REV@" ^
        -E "VBOX_WINDOWS_ADDITIONS_ICON_FILE=%_MY_OPT_SRC_DIR%\VirtualBoxGA-vista.ico" ^
        -E "VBOX_NSIS_ICON_FILE=%_MY_OPT_SRC_DIR%\VirtualBoxGA-nsis.ico" ^
        -E "VBOX_WITH_GUEST_INSTALL_HELPER=1" -E "VBOX_WITH_GUEST_INSTALLER_UNICODE=1" -E "VBOX_WITH_LICENSE_INSTALL_RTF=1" ^
        -E "VBOX_WITH_WDDM=1" -E "VBOX_WITH_MESA3D=1" -E "VBOX_BRAND_WIN_ADD_INST_DLGBMP=%_MY_OPT_SRC_DIR%\welcome.bmp" ^
        -E "VBOX_BRAND_LICENSE_RTF=%_MY_OPT_SRC_DIR%\License-gpl-3.0.rtf"  -E "KBUILD_TYPE=%_MY_OPT_BUILD_TYPE%" -E "KBUILD_TARGET_ARCH=amd64" ^
        --  %KBUILD_DEVTOOLS%/win.x86/nsis/v3.04-log/makensis.exe /NOCD /V2 ^
                "/DVBOX_SIGN_ADDITIONS=1" ^
                "/DEXTERNAL_UNINSTALLER=1" ^
                "%_MY_OPT_SRC_DIR%\VBoxGuestAdditions.nsi"

rem
rem Building amd64 installer
rem
echo **************************************************************************
echo * Building x86 installer
echo **************************************************************************

del %_MY_OPT_UNTAR_DIR%\win.x86\release\bin\additions\VBoxWindowsAdditions-x86.exe
cp %_MY_REPACK_DIR_X86%\..\obj\uninst.exe %_MY_REPACK_DIR_X86%\

rem TBD: that has to be converted to invoke auto-generated .cmd

%KBUILD_BIN_PATH%\kmk_redirect.exe -C %_MY_OPT_SRC_DIR% ^
        -E "PATH_OUT=%_MY_REPACK_DIR_X86%\.." ^
        -E "PATH_TARGET=%_MY_REPACK_DIR_X86%" ^
        -E "PATH_TARGET_X86=%_MY_REPACK_DIR_X86%\resources" ^
        -E "VBOX_PATH_ADDITIONS_WIN_X86=%_MY_REPACK_DIR_X86%\..\bin\additions" ^
        -E "VBOX_VENDOR=Oracle Corporation" -E "VBOX_VENDOR_SHORT=Oracle" -E "VBOX_PRODUCT=Oracle VirtualBox" ^
        -E "VBOX_C_YEAR=@VBOX_C_YEAR@" -E "VBOX_VERSION_STRING=@VBOX_VERSION_STRING@" -E "VBOX_VERSION_STRING_RAW=@VBOX_VERSION_STRING_RAW@" ^
        -E "VBOX_VERSION_MAJOR=@VBOX_VERSION_MAJOR@" -E "VBOX_VERSION_MINOR=@VBOX_VERSION_MINOR@" -E "VBOX_VERSION_BUILD=@VBOX_VERSION_BUILD@" -E "VBOX_SVN_REV=@VBOX_SVN_REV@" ^
        -E "VBOX_WINDOWS_ADDITIONS_ICON_FILE=%_MY_OPT_SRC_DIR%\VirtualBoxGA-vista.ico" ^
        -E "VBOX_NSIS_ICON_FILE=%_MY_OPT_SRC_DIR%\VirtualBoxGA-nsis.ico" ^
        -E "VBOX_WITH_GUEST_INSTALL_HELPER=1" -E "VBOX_WITH_GUEST_INSTALLER_UNICODE=1" -E "VBOX_WITH_LICENSE_INSTALL_RTF=1" ^
        -E "VBOX_WITH_WDDM=1" -E "VBOX_WITH_MESA3D=1" -E "VBOX_BRAND_WIN_ADD_INST_DLGBMP=%_MY_OPT_SRC_DIR%\welcome.bmp" ^
        -E "VBOX_BRAND_LICENSE_RTF=%_MY_OPT_SRC_DIR%\License-gpl-3.0.rtf"  -E "KBUILD_TYPE=%_MY_OPT_BUILD_TYPE%" -E "KBUILD_TARGET_ARCH=x86" ^
        --  %KBUILD_DEVTOOLS%/win.x86/nsis/v3.04-log/makensis.exe /NOCD /V2 ^
                "/DVBOX_SIGN_ADDITIONS=1" ^
                "/DEXTERNAL_UNINSTALLER=1" ^
                "%_MY_OPT_SRC_DIR%\VBoxGuestAdditions.nsi"

rem
rem Making .iso
rem
echo **************************************************************************
echo * Making VBoxGuestAdditions.iso
echo **************************************************************************

del %_MY_OPT_OUTDIR%/VBoxGuestAdditions.iso

rem TBD: that has to be converted to invoke auto-generated .cmd

%_MY_SCRIPT_DIR%/../bin/bldRTIsoMaker.exe ^
        --output %_MY_OPT_OUTDIR%/VBoxGuestAdditions.iso ^
        --iso-level 3 ^
        --rock-ridge ^
        --joliet ^
        --rational-attribs ^
        --random-order-verification 2048 ^
        /cert/vbox-sha1.cer=%_MY_SCRIPT_DIR%/../bin/additions/vbox-sha1.cer ^
        /cert/vbox-sha256.cer=%_MY_SCRIPT_DIR%/../bin/additions/vbox-sha256.cer ^
        /windows11-bypass.reg=%_MY_SCRIPT_DIR%/../bin/additions/windows11-bypass.reg ^
        /VBoxWindowsAdditions-x86.exe=%_MY_OPT_UNTAR_DIR%/win.x86/release/bin/additions/VBoxWindowsAdditions-x86.exe ^
        /VBoxWindowsAdditions.exe=%_MY_OPT_UNTAR_DIR%/win.x86/release/bin/additions/VBoxWindowsAdditions.exe ^
        /AUTORUN.INF=%_MY_OPT_SRC_DIR%/AUTORUN.INF ^
        /cert/VBoxCertUtil.exe=%_MY_SCRIPT_DIR%/../bin/additions/VBoxCertUtil.exe ^
        /NT3x/Readme.txt=%_MY_OPT_SRC_DIR%/NT3xReadme.txt ^
        /NT3x/VBoxGuest.sys=%_MY_OPT_UNTAR_DIR%/win.x86/release/bin/additions/VBoxGuest.sys ^
        /NT3x/VBoxGuest.cat=%_MY_OPT_UNTAR_DIR%/win.x86/release/bin/additions/VBoxGuest.cat ^
        /NT3x/VBoxGuest.inf=%_MY_OPT_UNTAR_DIR%/win.x86/release/bin/additions/VBoxGuest.inf ^
        /NT3x/VBoxMouseNT.sys=%_MY_OPT_UNTAR_DIR%/win.x86/release/bin/additions/VBoxMouseNT.sys ^
        /NT3x/VBoxMouse.inf=%_MY_OPT_UNTAR_DIR%/win.x86/release/bin/additions/VBoxMouse.inf ^
        /NT3x/VBoxMouse.cat=%_MY_OPT_UNTAR_DIR%/win.x86/release/bin/additions/VBoxMouse.cat ^
        /NT3x/VBoxMouse.sys=%_MY_OPT_UNTAR_DIR%/win.x86/release/bin/additions/VBoxMouse.sys ^
        /NT3x/VBoxControl.exe=%_MY_OPT_UNTAR_DIR%/win.x86/release/bin/additions/VBoxControl.exe ^
        /NT3x/VBoxService.exe=%_MY_OPT_UNTAR_DIR%/win.x86/release/bin/additions/VBoxService.exe ^
        /VBoxWindowsAdditions-amd64.exe=%_MY_OPT_UNTAR_DIR%/win.amd64/release/bin/additions/VBoxWindowsAdditions-amd64.exe ^
        /VBoxSolarisAdditions.pkg=%_MY_OPT_UNTAR_DIR%/solaris.x86/release/bin/additions/VBoxSolarisAdditions.pkg ^
        /OS2/VBoxGuest.sys=%_MY_OPT_UNTAR_DIR%/os2.x86/release/bin/additions/VBoxGuest.sys ^
        /OS2/VBoxSF.ifs=%_MY_OPT_UNTAR_DIR%/os2.x86/release/bin/additions/VBoxSF.ifs ^
        /OS2/VBoxService.exe=%_MY_OPT_UNTAR_DIR%/os2.x86/release/bin/additions/VBoxService.exe ^
        /OS2/VBoxControl.exe=%_MY_OPT_UNTAR_DIR%/os2.x86/release/bin/additions/VBoxControl.exe ^
        /OS2/VBoxReplaceDll.exe=%_MY_OPT_UNTAR_DIR%/os2.x86/release/bin/additions/VBoxReplaceDll.exe ^
        /OS2/libc06.dll=%_MY_OPT_UNTAR_DIR%/os2.x86/release/bin/additions/libc06.dll ^
        /OS2/libc061.dll=%_MY_OPT_UNTAR_DIR%/os2.x86/release/bin/additions/libc061.dll ^
        /OS2/libc062.dll=%_MY_OPT_UNTAR_DIR%/os2.x86/release/bin/additions/libc062.dll ^
        /OS2/libc063.dll=%_MY_OPT_UNTAR_DIR%/os2.x86/release/bin/additions/libc063.dll ^
        /OS2/libc064.dll=%_MY_OPT_UNTAR_DIR%/os2.x86/release/bin/additions/libc064.dll ^
        /OS2/libc065.dll=%_MY_OPT_UNTAR_DIR%/os2.x86/release/bin/additions/libc065.dll ^
        /OS2/libc066.dll=%_MY_OPT_UNTAR_DIR%/os2.x86/release/bin/additions/libc066.dll ^
        /OS2/readme.txt=%_MY_OPT_UNTAR_DIR%/os2.x86/release/bin/additions/readme.txt ^
        /OS2/gengradd.dll=%_MY_OPT_UNTAR_DIR%/os2.x86/release/bin/additions/gengradd.dll ^
        /OS2/vboxmouse.sys=%_MY_OPT_UNTAR_DIR%/os2.x86/release/bin/additions/vboxmouse.sys ^
        /VBoxLinuxAdditions.run=%_MY_OPT_UNTAR_DIR%/linux.x86/release/bin/additions/VBoxLinuxAdditions.run ^
        /runasroot.sh=%_MY_OPT_UNTAR_DIR%/linux.x86/release/bin/additions/runasroot.sh ^
        /autorun.sh=%_MY_OPT_UNTAR_DIR%/linux.x86/release/bin/additions/autorun.sh ^
        /VBoxDarwinAdditions.pkg=%_MY_OPT_UNTAR_DIR%/darwin.amd64/release/dist/additions/VBoxGuestAdditions.pkg ^
        /VBoxDarwinAdditionsUninstall.tool=%_MY_OPT_UNTAR_DIR%/darwin.amd64/release/dist/additions/VBoxDarwinAdditionsUninstall.tool ^
        --chmod a+x:/VBoxLinuxAdditions.run  --chmod a+x:/runasroot.sh  --chmod a+x:/autorun.sh  --chmod a+x:/VBoxDarwinAdditionsUninstall.tool ^
        --volume-id="VBOXADDITIONS_@VBOX_VERSION_STRING@_@VBOX_SVN_REV@" ^
        --name-setup=joliet ^
        --volume-id="VBox_GAs_@VBOX_VERSION_STRING@"

if not exist %_MY_OPT_OUTDIR%/VBoxGuestAdditions.iso goto end_failed
call set _MY_OUT_FILES=%%VBoxGuestAdditions.iso

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
