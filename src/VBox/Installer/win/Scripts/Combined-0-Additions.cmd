@echo off
rem $Id: Combined-0-Additions.cmd 110270 2025-07-16 17:58:34Z klaus.espenlaub@oracle.com $
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

call D:/repack-env.cmd

cd win.amd64/release/repackadd

call Combined-1-Prepare.cmd -t release -g --no-extpack 2>&1 | tee Combined-1-Prepare.log

call Combined-2-SignAdditions.cmd -t release -g --no-extpack 2>&1 | tee Combined-2-SignAdditions.log

call Combined-3-RepackAdditions.cmd --vboxall-untar-dir "../../../" --outdir "../../../output" --build-type "release" 2>&1 | tee Combined-3-RepackAdditions.log
