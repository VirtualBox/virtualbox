@echo off
rem $Id: Single-0-All.cmd 110270 2025-07-16 17:58:34Z klaus.espenlaub@oracle.com $
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

cd repack

del VBoxDrivers*

call Single-1-Prepare.cmd --outdir .

C:\Utils\attestation\AttestationSigning.bat "VirtualBox drivers @VBOX_VERSION_STRING@r@VBOX_SVN_REV@" "amd64" "r@VBOX_SVN_REV@" "VBoxDrivers-@VBOX_VERSION_STRING@r@VBOX_SVN_REV@-amd64.cab"

call Single-3-Repack.cmd --signed VBoxDrivers-@VBOX_VERSION_STRING@r@VBOX_SVN_REV@-amd64.cab.Signed.zip
