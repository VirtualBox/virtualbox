@echo off
rem $Id: loadall.cmd 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
rem rem @file
rem Windows NT batch script for loading the host drivers.
rem

rem
rem Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
rem The contents of this file may alternatively be used under the terms
rem of the Common Development and Distribution License Version 1.0
rem (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
rem in the VirtualBox distribution, in which case the provisions of the
rem CDDL are applicable instead of those of the GPL.
rem
rem You may elect to license modified versions of this file under the
rem terms and conditions of either the GPL or the CDDL or both.
rem
rem SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
rem


setlocal ENABLEEXTENSIONS
setlocal ENABLEDELAYEDEXPANSION
setlocal


rem
rem find the directory we're in.
rem
set MY_DIR=%~dp0
if exist "%MY_DIR%\load.cmd" goto dir_okay
echo load.cmd: failed to find load.sh in "%~dp0".
goto end

:dir_okay
rem
rem We don't use the driver files directly any more because of win10 keeping the open,
rem so create an alternative directory for the binaries.
rem
set MY_ALTDIR=%MY_DIR%\..\LoadedDrivers
if not exist "%MY_ALTDIR%" mkdir "%MY_ALTDIR%"

rem
rem Display device states.
rem
for %%i in (VBoxNetAdp VBoxNetAdp6 VBoxNetFlt VBoxNetLwf VBoxUSBMon VBoxUSB VBoxSup) do (
    set type=
    for /f "usebackq tokens=*" %%f in (`sc query %%i`) do (set xxx=%%f&&if "!xxx:~0,5!" =="STATE" set type=!xxx!)
    for /f "usebackq tokens=2 delims=:" %%f in ('!type!') do set type=%%f
    if "!type!x" == "x" set type= not configured, probably
    echo load.sh: %%i -!type!
)

rem
rem Copy uninstallers and installers and VBoxRT into the dir:
rem
echo **
echo ** Copying installers and uninstallers into %MY_ALTDIR%...
echo **
set MY_FAILED=no
for %%i in (NetAdpUninstall.exe NetAdp6Uninstall.exe USBUninstall.exe NetFltUninstall.exe NetLwfUninstall.exe SUPUninstall.exe ^
            NetAdpInstall.exe   NetAdp6Install.exe   USBInstall.exe   NetFltInstall.exe   NetLwfInstall.exe   SUPInstall.exe ^
            VBoxRT.dll) do if exist "%MY_DIR%\%%i" (copy "%MY_DIR%\%%i" "%MY_ALTDIR%\%%i" || set MY_FAILED=yes)
if "%MY_FAILED%" == "yes" goto end

rem
rem Unload the drivers.
rem
echo **
echo ** Unloading drivers...
echo **
for %%i in (NetAdpUninstall.exe NetAdp6Uninstall.exe USBUninstall.exe NetFltUninstall.exe NetLwfUninstall.exe SUPUninstall.exe) do (
    if exist "%MY_ALTDIR%\%%i" (echo  * Running %%i...&& "%MY_ALTDIR%\%%i")
)

rem
rem Copy the driver files into the directory now that they no longer should be in use and can be overwritten.
rem
echo **
echo ** Copying drivers into %MY_ALTDIR%...
echo **
set MY_FAILED=no
for %%i in (^
    VBoxSup.sys     VBoxSup.inf      VBoxSup.cat     VBoxSup-PreW10.cat ^
    VBoxNetAdp.sys  VBoxNetAdp.inf   VBoxNetAdp.cat ^
    VBoxNetAdp6.sys VBoxNetAdp6.inf  VBoxNetAdp6.cat VBoxNetAdp6-PreW10.cat ^
    VBoxNetFlt.sys  VBoxNetFlt.inf   VBoxNetFlt.cat                         VBoxNetFltNobj.dll ^
    VBoxNetFltM.inf  ^
    VBoxNetLwf.sys  VBoxNetLwf.inf   VBoxNetLwf.cat  VBoxNetLwf-PreW10.cat  ^
    VBoxUSB.sys     VBoxUSB.inf      VBoxUSB.cat     VBoxUSB-PreW10.cat     ^
    VBoxUSBMon.sys  VBoxUSBMon.inf   VBoxUSBMon.cat  VBoxUSBMon-PreW10.cat  ) ^
do if exist "%MY_DIR%\%%i" (copy "%MY_DIR%\%%i" "%MY_ALTDIR%\%%i" || set MY_FAILED=yes)
if "%MY_FAILED%" == "yes" goto end

rem
rem Invoke the installer if asked to do so.
rem
if "%1%" == "-u" goto end
if "%1%" == "--uninstall" goto end

set MY_VER=
for /f "usebackq delims=[] tokens=2" %%f in (`cmd /c ver`) do set MY_VER=%%f
for /f "usebackq tokens=2" %%f in ('%MY_VER%') do set MY_VER=%%f
for /f "usebackq delims=. tokens=1" %%f in ('%MY_VER%') do set MY_VER=%%f
set MY_VER=0000%MY_VER%
set MY_VER=%MY_VER:~-2%

echo **
echo ** Loading drivers (for windows version %MY_VER%)...
echo **

if "%MY_VER%" GEQ "06" (
    set MY_INSTALLERS=SUPInstall.exe USBInstall.exe NetLwfInstall.exe NetAdp6Install.exe
) else (
    set MY_INSTALLERS=SUPInstall.exe USBInstall.exe NetFltInstall.exe
    rem NetAdpInstall.exe; - busted
)
for %%i in (%MY_INSTALLERS%) do (echo  * Running %%i...&& "%MY_ALTDIR%\%%i" || (echo loadall.cmd: %%i failed&& goto end))

echo * loadall.cmd completed successfully.
:end
endlocal
endlocal
endlocal

