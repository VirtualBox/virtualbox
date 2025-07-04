@echo off
rem $Id: gen-vscode-workspace.cmd 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $
rem rem @file
rem Windows NT batch script for launching gen-vscode-workspace.sh
rem

rem
rem Copyright (C) 2022-2024 Oracle and/or its affiliates.
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
rem gen-vscode-workspace.sh should be in the same directory as this script.
rem
set MY_SCRIPT=%~dp0gen-vscode-workspace.sh
if exist "%MY_SCRIPT%" goto found
echo gen-vscode-workspace.cmd: failed to find gen-vscode-workspace.sh in "%~dp0".
goto end

rem
rem Found it, convert slashes and tell kmk_ash to interpret it.
rem
:found
set MY_SCRIPT=%MY_SCRIPT:\=/%
set MY_ARGS=%*
if ".%MY_ARGS%." NEQ ".." set MY_ARGS=%MY_ARGS:\=/%
kmk_ash %MY_SCRIPT% --windows-host %MY_ARGS%

:end
endlocal
endlocal
