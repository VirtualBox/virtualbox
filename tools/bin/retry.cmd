@echo off
rem $Id: retry.cmd 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
rem rem @file
rem Windows NT batch script that retries a command 5 times.
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

rem
rem Note! We're using %ERRORLEVEL% here instead of the classic
rem IF ERRORLEVEL 0 GOTO blah because the latter cannot handle
rem the complete range or status codes while the former can.
rem
rem Note! SET changes ERRORLEVEL on XP+, so we have to ECHO
rem before incrementing the counter.
rem
set /a retry_count = 1
:retry
%*
if %ERRORLEVEL% == 0 goto success
if %retry_count% GEQ 5 goto give_up
echo retry.cmd: Attempt %retry_count% FAILED(%ERRORLEVEL%),  retrying: %*
set /a retry_count += 1
goto retry

:give_up
echo retry.cmd: Attempt %retry_count% FAILED(%ERRORLEVEL%), giving up: %*
set retry_count=
exit /b 1

:success
if %retry_count% NEQ 1 echo retry.cmd: Success after %retry_count% tries: %*!
set retry_count=
exit /b 0

