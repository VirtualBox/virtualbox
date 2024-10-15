@echo off
REM $Id: comregister.cmd 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
REM
REM Script to register the VirtualBox COM classes
REM (both inproc and out-of-process)
REM

REM
REM Copyright (C) 2006-2024 Oracle and/or its affiliates.
REM
REM This file is part of VirtualBox base platform packages, as
REM available from https://www.virtualbox.org.
REM
REM This program is free software; you can redistribute it and/or
REM modify it under the terms of the GNU General Public License
REM as published by the Free Software Foundation, in version 3 of the
REM License.
REM
REM This program is distributed in the hope that it will be useful, but
REM WITHOUT ANY WARRANTY; without even the implied warranty of
REM MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
REM General Public License for more details.
REM
REM You should have received a copy of the GNU General Public License
REM along with this program; if not, see <https://www.gnu.org/licenses>.
REM
REM SPDX-License-Identifier: GPL-3.0-only
REM

setlocal

REM Check if the current user is an administrator. Otherwise
REM all the COM registration will fail silently.
NET FILE 1>NUL 2>NUL & IF ERRORLEVEL 1 (ECHO Must be run as Administrator. Exiting.) & GOTO end

REM
REM Figure out where the script lives first, so that we can invoke the
REM correct VBoxSVC and register the right VBoxC.dll.
REM

REM Determine the current directory.
set _SCRIPT_CURDIR=%CD%
for /f "tokens=*" %%d in ('cd') do set _SCRIPT_CURDIR=%%d

REM Determine a correct self - by %0.
set _SCRIPT_SELF=%0
if exist "%_SCRIPT_SELF%" goto found_self
set _SCRIPT_SELF=%_SCRIPT_SELF%.cmd
if exist "%_SCRIPT_SELF%" goto found_self

REM Determine a correct self - by current working directory.
set _SCRIPT_SELF=%_SCRIPT_CURDIR%\comregister.cmd
if exist "%_SCRIPT_SELF%" goto found_self

REM Determine a correct self - by the PATH
REM This is very verbose because nested for loops didn't work out.
for /f "tokens=1  delims=;" %%d in ("%PATH%") do set _SCRIPT_SELF=%%d\comregister.cmd
if exist "%_SCRIPT_SELF%" goto found_self
for /f "tokens=2  delims=;" %%d in ("%PATH%") do set _SCRIPT_SELF=%%d\comregister.cmd
if exist "%_SCRIPT_SELF%" goto found_self
for /f "tokens=3  delims=;" %%d in ("%PATH%") do set _SCRIPT_SELF=%%d\comregister.cmd
if exist "%_SCRIPT_SELF%" goto found_self
for /f "tokens=4  delims=;" %%d in ("%PATH%") do set _SCRIPT_SELF=%%d\comregister.cmd
if exist "%_SCRIPT_SELF%" goto found_self
for /f "tokens=5  delims=;" %%d in ("%PATH%") do set _SCRIPT_SELF=%%d\comregister.cmd
if exist "%_SCRIPT_SELF%" goto found_self
for /f "tokens=6  delims=;" %%d in ("%PATH%") do set _SCRIPT_SELF=%%d\comregister.cmd
if exist "%_SCRIPT_SELF%" goto found_self
for /f "tokens=7  delims=;" %%d in ("%PATH%") do set _SCRIPT_SELF=%%d\comregister.cmd
if exist "%_SCRIPT_SELF%" goto found_self
for /f "tokens=8  delims=;" %%d in ("%PATH%") do set _SCRIPT_SELF=%%d\comregister.cmd
if exist "%_SCRIPT_SELF%" goto found_self
for /f "tokens=9  delims=;" %%d in ("%PATH%") do set _SCRIPT_SELF=%%d\comregister.cmd
if exist "%_SCRIPT_SELF%" goto found_self
for /f "tokens=10 delims=;" %%d in ("%PATH%") do set _SCRIPT_SELF=%%d\comregister.cmd
if exist "%_SCRIPT_SELF%" goto found_self
for /f "tokens=11 delims=;" %%d in ("%PATH%") do set _SCRIPT_SELF=%%d\comregister.cmd
if exist "%_SCRIPT_SELF%" goto found_self
for /f "tokens=12 delims=;" %%d in ("%PATH%") do set _SCRIPT_SELF=%%d\comregister.cmd
if exist "%_SCRIPT_SELF%" goto found_self
for /f "tokens=13 delims=;" %%d in ("%PATH%") do set _SCRIPT_SELF=%%d\comregister.cmd
if exist "%_SCRIPT_SELF%" goto found_self
for /f "tokens=14 delims=;" %%d in ("%PATH%") do set _SCRIPT_SELF=%%d\comregister.cmd
if exist "%_SCRIPT_SELF%" goto found_self
for /f "tokens=15 delims=;" %%d in ("%PATH%") do set _SCRIPT_SELF=%%d\comregister.cmd
if exist "%_SCRIPT_SELF%" goto found_self
for /f "tokens=16 delims=;" %%d in ("%PATH%") do set _SCRIPT_SELF=%%d\comregister.cmd
if exist "%_SCRIPT_SELF%" goto found_self
for /f "tokens=17 delims=;" %%d in ("%PATH%") do set _SCRIPT_SELF=%%d\comregister.cmd
if exist "%_SCRIPT_SELF%" goto found_self
for /f "tokens=18 delims=;" %%d in ("%PATH%") do set _SCRIPT_SELF=%%d\comregister.cmd
if exist "%_SCRIPT_SELF%" goto found_self
for /f "tokens=19 delims=;" %%d in ("%PATH%") do set _SCRIPT_SELF=%%d\comregister.cmd
if exist "%_SCRIPT_SELF%" goto found_self
for /f "tokens=20 delims=;" %%d in ("%PATH%") do set _SCRIPT_SELF=%%d\comregister.cmd
if exist "%_SCRIPT_SELF%" goto found_self
echo Warning: Not able to determin the comregister.cmd location.
set _VBOX_DIR=
goto register

:found_self
set _VBOX_DIR=
cd "%_SCRIPT_SELF%\.."
for /f "tokens=*" %%d in ('cd') do set _VBOX_DIR=%%d\
cd "%_SCRIPT_CURDIR%"

REM
REM Check for 64-bitness.
REM
set fIs64BitWindows=0
if not "%ProgramW6432%x" == "x" set fIs64BitWindows=1
if exist "%windir\syswow64\kernel32.dll" set fIs64BitWindows=1

REM
REM Figure out the Windows version as the proxy stub requires 6.0 or later (at least for 64-bit).
REM
set WinVer=Version 4.0.1381
set WinVerMajor=4
set WinVerMinor=0
set WinVerBuild=1381
for /f "tokens=2 delims=[]" %%a in ('ver') do set WinVer=%%a
for /f "tokens=2,3,4 delims=. " %%a in ("%WinVer%") do (
    set WinVerMajor=%%a
    set WinVerMinor=%%b
    set WinVerBuild=%%c
)
REM echo WinVerMajor=%WinVerMajor% WinVerMinor=%WinVerMinor% WinVerBuild=%WinVerBuild%  WinVer=%WinVer%

REM
REM Parse arguments.
REM
set fNoProxy=0
set fUninstallOnly=0

:arg_loop
if "%1x" == "x"             goto arg_done

if "%1" == "-u"             goto arg_uninstall
if "%1" == "--uninstall"    goto arg_uninstall
if "%1" == "--proxy"        goto arg_proxy
if "%1" == "--no-proxy"     goto arg_no_proxy
echo syntax error: Unknown option %1
echo usage: comregister.cmd [-u,--uninstall] [--no-proxy] [--proxy]
goto end

:arg_uninstall
set fUninstallOnly=1
goto arg_next

:arg_proxy
set fNoProxy=0
goto arg_next

:arg_no_proxy
set fNoProxy=1
goto arg_next

:arg_next
shift
goto arg_loop
:arg_done

REM
REM Do the registrations.
REM
@if %fIs64BitWindows% == 1 goto register_amd64

:register_x86
@echo on
"%_VBOX_DIR%VBoxSVC.exe" /UnregServer
regsvr32 /s /u "%_VBOX_DIR%VBoxC.dll"
%windir%\system32\regsvr32 /s /u "%_VBOX_DIR%VBoxProxyStub.dll"
@if %fUninstallOnly% == 1 goto end
"%_VBOX_DIR%VBoxSVC.exe" /RegServer
"%_VBOX_DIR%VBoxSDS.exe" /RegService
regsvr32 /s    "%_VBOX_DIR%VBoxC.dll"
@if %fNoProxy% == 1 goto end
if exist "%_VBOX_DIR%VBoxProxyStub.dll"     %windir%\system32\regsvr32 /s "%_VBOX_DIR%VBoxProxyStub.dll"
@echo off
goto end

REM Unregister all first, then register them. The order matters here.
:register_amd64
if "%WinVerMajor%" == "5" goto register_amd64_legacy
if not "%WinVerMajor%" == "6" goto register_amd64_not_legacy
if not "%WinVerMinor%" == "0" goto register_amd64_not_legacy
:register_amd64_legacy
set s64BitProxyStub=VBoxProxyStubLegacy.dll
goto register_amd64_begin
:register_amd64_not_legacy
set s64BitProxyStub=VBoxProxyStub.dll
:register_amd64_begin
echo s64BitProxyStub=%s64BitProxyStub%
@echo on
"%_VBOX_DIR%VBoxSVC.exe" /UnregServer
"%_VBOX_DIR%VBoxSDS.exe" /UnregService
%windir%\system32\regsvr32 /s /u "%_VBOX_DIR%VBoxC.dll"
%windir%\syswow64\regsvr32 /s /u "%_VBOX_DIR%x86\VBoxClient-x86.dll"
%windir%\system32\regsvr32 /s /u "%_VBOX_DIR%%s64BitProxyStub%"
%windir%\syswow64\regsvr32 /s /u "%_VBOX_DIR%x86\VBoxProxyStub-x86.dll"
if %fUninstallOnly% == 1 goto end
"%_VBOX_DIR%VBoxSVC.exe" /RegServer
"%_VBOX_DIR%VBoxSDS.exe" /RegService
%windir%\system32\regsvr32 /s    "%_VBOX_DIR%VBoxC.dll"
%windir%\syswow64\regsvr32 /s    "%_VBOX_DIR%x86\VBoxClient-x86.dll"
if %fNoProxy% == 1 goto end
if exist "%_VBOX_DIR%%s64BitProxyStub%"         %windir%\system32\regsvr32 /s "%_VBOX_DIR%%s64BitProxyStub%"
if exist "%_VBOX_DIR%x86\VBoxProxyStub-x86.dll" %windir%\syswow64\regsvr32 /s "%_VBOX_DIR%x86\VBoxProxyStub-x86.dll"
@echo off

:end
@endlocal
