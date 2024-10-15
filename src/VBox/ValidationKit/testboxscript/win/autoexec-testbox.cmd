@echo off
REM $Id: autoexec-testbox.cmd 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
REM REM @file
REM VirtualBox Validation Kit - testbox script, automatic execution wrapper.
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
REM The contents of this file may alternatively be used under the terms
REM of the Common Development and Distribution License Version 1.0
REM (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
REM in the VirtualBox distribution, in which case the provisions of the
REM CDDL are applicable instead of those of the GPL.
REM
REM You may elect to license modified versions of this file under the
REM terms and conditions of either the GPL or the CDDL or both.
REM
REM SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
REM

@echo "$Id: autoexec-testbox.cmd 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $"
@echo on
setlocal EnableExtensions
set exe=python.exe
for /f %%x in ('tasklist /NH /FI "IMAGENAME eq %exe%"') do if %%x == %exe% goto end

if exist %SystemRoot%\System32\aim_ll.exe (
    set RAMEXE=aim
) else if exist %SystemRoot%\System32\imdisk.exe (
    set RAMEXE=imdisk
) else goto defaulttest

REM Take presence of imdisk.exe or aim_ll.exe as order to test in ramdisk.
set RAMDRIVE=D:
if exist %RAMDRIVE%\TEMP goto skip
if %RAMEXE% == aim (
    aim_ll -a -t vm -s 16G -m %RAMDRIVE% -p "/fs:ntfs /q /y"
) else if %RAMEXE% == imdisk (
    imdisk -a -s 16GB -m %RAMDRIVE% -p "/fs:ntfs /q /y" -o "awe"
) else goto defaulttest
:skip

set VBOX_INSTALL_PATH=%RAMDRIVE%\VBoxInstall
set TMP=%RAMDRIVE%\TEMP
set TEMP=%TMP%

mkdir %VBOX_INSTALL_PATH%
mkdir %TMP%

set TESTBOXSCRIPT_OPTS=--scratch-root=%RAMDRIVE%\testbox

:defaulttest
%SystemDrive%\Python27\python.exe %SystemDrive%\testboxscript\testboxscript\testboxscript.py --testrsrc-server-type=cifs --builds-server-type=cifs %TESTBOXSCRIPT_OPTS%
pause
:end
