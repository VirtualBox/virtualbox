@echo off
rem $Id: backport-merge-and-commit.cmd 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
rem rem @file
rem Windows NT batch script for launching backport-merge-and-commit.sh
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
rem SPDX-License-Identifier: GPL-3.0-only
rem

setlocal ENABLEEXTENSIONS
setlocal

rem
rem svn-ps.sh should be in the same directory as this script.
rem
set MY_SCRIPT=%~dp0backport-merge-and-commit.sh
if exist "%MY_SCRIPT%" goto found
echo backport-merge-and-commit.sh: failed to find backport-merge-and-commit.sh in "%~dp0".
goto end

rem
rem Found it, convert slashes and tell kmk_ash to interpret it.
rem
:found
set MY_SCRIPT=%MY_SCRIPT:\=/%
set MY_ARGS=%*
if ".%MY_ARGS%." NEQ ".." set MY_ARGS=%MY_ARGS:\=/%
kmk_ash %MY_SCRIPT% %MY_ARGS%

:end
endlocal
endlocal

