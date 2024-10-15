@echo off
rem $Id: PackDriversForSubmission.cmd 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
rem rem @file
rem Windows NT batch script for preparing for signing submission.
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
rem Parse arguments.
rem
set _MY_OPT_BINDIR=..\bin
set _MY_OPT_PDBDIR=
set _MY_OPT_GADIR=
set _MY_OPT_WITH_MAIN=1
set _MY_OPT_WITH_PDB=1
set _MY_OPT_EXTPACK=
set _MY_OPT_NO_EXTRACT=0
set _MY_OPT_WITH_EXTPACK=1
set _MY_OPT_WITH_GA=0
set _MY_OPT_OUTPUT=
set _MY_OPT_DDF_FILE=
set _MY_OPT_ARCH=amd64

:argument_loop
if ".%1" == "."             goto no_more_arguments

if ".%1" == ".-h"           goto opt_h
if ".%1" == ".-?"           goto opt_h
if ".%1" == "./h"           goto opt_h
if ".%1" == "./H"           goto opt_h
if ".%1" == "./?"           goto opt_h
if ".%1" == ".-help"        goto opt_h
if ".%1" == ".--help"       goto opt_h

if ".%1" == ".-a"           goto opt_a
if ".%1" == ".--arch"       goto opt_a
if ".%1" == ".-b"           goto opt_b
if ".%1" == ".--bindir"     goto opt_b
if ".%1" == ".-d"           goto opt_d
if ".%1" == ".--ddf"        goto opt_d
if ".%1" == ".-e"           goto opt_e
if ".%1" == ".--extpack"    goto opt_e
if ".%1" == ".--no-main"    goto opt_m
if ".%1" == ".-n"           goto opt_n
if ".%1" == ".--no-pdb"     goto opt_n
if ".%1" == ".-o"           goto opt_o
if ".%1" == ".--output"     goto opt_o
if ".%1" == ".-p"           goto opt_p
if ".%1" == ".--pdb"        goto opt_p
if ".%1" == ".-t"           goto opt_p
if ".%1" == ".--no-extract" goto opt_t
if ".%1" == ".-x"           goto opt_x
if ".%1" == ".--no-extpack" goto opt_x
if ".%1" == ".-g"           goto opt_g
if ".%1" == ".--ga"         goto opt_g
if ".%1" == ".--additions"  goto opt_g
echo syntax error: Unknown option: %1
echo               Try --help to list valid options.
goto end_failed

:argument_loop_next_with_value
shift
shift
goto argument_loop

:opt_a
if ".%~2" == "."            goto syntax_error_missing_value
if not "%2" == "x86" if not "%2" == "amd64" goto syntax_error_unknown_arch
set _MY_OPT_ARCH=%~2
goto argument_loop_next_with_value

:opt_b
if ".%~2" == "."            goto syntax_error_missing_value
set _MY_OPT_BINDIR=%~2
goto argument_loop_next_with_value

:opt_d
if ".%~2" == "."            goto syntax_error_missing_value
set _MY_OPT_DDF_FILE=%~2
goto argument_loop_next_with_value

:opt_e
if ".%~2" == "."            goto syntax_error_missing_value
set _MY_OPT_EXTPACK=%~2
goto argument_loop_next_with_value

:opt_h
echo This script creates a .cab file containing all drivers needing blessing from
echo Microsoft to run on recent Windows 10 installations.
echo .
echo Usage: PackDriversForSubmission.cmd [-b bindir] [-p pdbdir] [--no-main] [-n/--no-pdb] [-e expack]
echo           [-x/--no-extpack] [-g/--ga/--additions] [-o output.cab] [-p output.ddf] [-a x86/amd64]
echo .
echo Warning! This script should normally be invoked from the repack directory w/o any parameters.
goto end_failed

:opt_m
set _MY_OPT_WITH_MAIN=0
shift
goto argument_loop

:opt_n
set _MY_OPT_WITH_PDB=0
shift
goto argument_loop

:opt_p
if ".%~2" == "."            goto syntax_error_missing_value
set _MY_OPT_PDBDIR=%~2
goto argument_loop_next_with_value

:opt_o
if ".%~2" == "."            goto syntax_error_missing_value
set _MY_OPT_OUTPUT=%~2
goto argument_loop_next_with_value

:opt_t
set _MY_OPT_NO_EXTRACT=1
shift
goto argument_loop

:opt_x
set _MY_OPT_WITH_EXTPACK=0
shift
goto argument_loop

:opt_g
set _MY_OPT_WITH_GA=1
shift
goto argument_loop

:syntax_error_missing_value
echo syntax error: missing or empty option value after %1
goto end_failed

:syntax_error_unknown_arch
echo syntax error: Unknown architecture: %2
goto end_failed

:error_bindir_does_not_exist
echo syntax error: Specified BIN directory does not exist: "%_MY_OPT_BINDIR%"
goto end_failed

:error_pdbdir_does_not_exist
echo syntax error: Specified PDB directory does not exist: "%_MY_OPT_PDBDIR%"
goto end_failed

:error_extpack_does_not_exist
echo syntax error: Specified extension pack does not exist: "%_MY_OPT_EXTPACK%"
goto end_failed

:error_additions_does_not_exist
echo syntax error: Specified guest additions does not exist: "%_MY_OPT_GADIR%"
goto end_failed


:error_output_exists
echo error: The output file already exist: "%_MY_OPT_OUTPUT%"
goto end_failed

:error_ddf_exists
echo error: The DDF file already exist: "%_MY_OPT_DDF_FILE%"
goto end_failed

:no_more_arguments
rem validate specified options
if not exist "%_MY_OPT_BINDIR%"     goto error_bindir_does_not_exist

if "%_MY_OPT_WITH_PDB%" == "0"      goto no_pdbdir_validation
if ".%_MY_OPT_PDBDIR%" == "."       set _MY_OPT_PDBDIR=%_MY_OPT_BINDIR%\..\stage\debug\bin
if not exist "%_MY_OPT_PDBDIR%"     goto error_pdbdir_does_not_exist
:no_pdbdir_validation

if "%_MY_OPT_WITH_EXTPACK%" == "0"  goto no_extpack_validation
if "%_MY_OPT_NO_EXTRACT%" == "1"    goto no_extpack_validation
if ".%_MY_OPT_EXTPACK%" == "."      set _MY_OPT_EXTPACK=%_MY_OPT_BINDIR%\Oracle_VirtualBox_Extension_Pack.vbox-extpack
if not exist "%_MY_OPT_EXTPACK%"    goto error_extpack_does_not_exist
:no_extpack_validation

if "%_MY_OPT_WITH_GA%" == "0"       goto no_additions_validation
if ".%_MY_OPT_GADIR%" == "."        set _MY_OPT_GADIR=%_MY_OPT_BINDIR%\additions
if not exist "%_MY_OPT_GADIR%"      goto error_additions_does_not_exist
:no_additions_validation

if ".%_MY_OPT_OUTPUT%" == "."       set _MY_OPT_OUTPUT=VBoxDrivers-@VBOX_VERSION_STRING@r@VBOX_SVN_REV@-%_MY_OPT_ARCH%.cab
if exist "%_MY_OPT_OUTPUT%"         goto error_output_exists

if ".%_MY_OPT_DDF_FILE%" == "."     set _MY_OPT_DDF_FILE=%_MY_OPT_OUTPUT%.ddf
if exist "%_MY_OPT_DDF_FILE%"       goto error_ddf_exists


rem
rem Unpack the extension pack.
rem We unpack it into the bin directory in the usual location.
rem
rem Note! Modify the path a little to ensure windows utilities are used before
rem       cygwin ones, and that we can use stuff from bin\tools if we like.
rem
set PATH=%SystemRoot%\System32;%PATH%;%_MY_OPT_BINDIR%
if "%_MY_OPT_WITH_EXTPACK%" == "0" goto no_extpack_unpack
set _MY_EXTPACK_DIR=%_MY_OPT_BINDIR%\ExtensionPacks\Oracle_VirtualBox_Extension_Pack
if not exist "%_MY_OPT_BINDIR%\ExtensionPacks"  ( mkdir "%_MY_OPT_BINDIR%\ExtensionPacks" || goto end_failed )
if not exist "%_MY_EXTPACK_DIR%"                ( mkdir "%_MY_EXTPACK_DIR%" || goto end_failed )
if "%_MY_OPT_NO_EXTRACT%" == "1" goto no_extpack_unpack
"%_MY_OPT_BINDIR%\tools\RTTar.exe" -xzf "%_MY_OPT_EXTPACK%"  -C "%_MY_EXTPACK_DIR%" || goto end_failed
:no_extpack_unpack

rem
rem Create the DDF file for makecab.
rem
echo .OPTION EXPLICIT>                                                                  "%_MY_OPT_DDF_FILE%" || goto end_failed
echo .Set CabinetFileCountThreshold=0 >>                                                "%_MY_OPT_DDF_FILE%"
echo .Set FolderFileCountThreshold=0 >>                                                 "%_MY_OPT_DDF_FILE%"
echo .Set FolderSizeThreshold=0 >>                                                      "%_MY_OPT_DDF_FILE%"
echo .Set MaxCabinetSize=0 >>                                                           "%_MY_OPT_DDF_FILE%"
echo .Set MaxDiskFileCount=0 >>                                                         "%_MY_OPT_DDF_FILE%"
echo .Set MaxDiskSize=0 >>                                                              "%_MY_OPT_DDF_FILE%"
echo .Set Cabinet=on>>                                                                  "%_MY_OPT_DDF_FILE%"
echo .Set CompressionType=MSZIP>>                                                       "%_MY_OPT_DDF_FILE%"
echo .Set Compress=on>>                                                                 "%_MY_OPT_DDF_FILE%"
echo .Set DiskDirectoryTemplate= >>                                                     "%_MY_OPT_DDF_FILE%"
echo .Set CabinetNameTemplate=%_MY_OPT_OUTPUT%>>                                        "%_MY_OPT_DDF_FILE%"
echo .Set InfFileName=%_MY_OPT_OUTPUT%.inf>>                                            "%_MY_OPT_DDF_FILE%"
echo .Set RptFileName=%_MY_OPT_OUTPUT%.rpt>>                                            "%_MY_OPT_DDF_FILE%"

if %_MY_OPT_WITH_MAIN% == 0 goto skip_main_package
echo .Set DestinationDir=VBoxSup>>                                                      "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_BINDIR%\VBoxSup.inf VBoxSup.inf>>                                         "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_BINDIR%\VBoxSup.sys VBoxSup.sys>>                                         "%_MY_OPT_DDF_FILE%"
if "%_MY_OPT_WITH_PDB%" == "1" echo %_MY_OPT_PDBDIR%\VBoxSup.pdb VBoxSup.pdb>>          "%_MY_OPT_DDF_FILE%"

echo .Set DestinationDir=VBoxNetAdp6>>                                                  "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_BINDIR%\VBoxNetAdp6.inf VBoxNetAdp6.inf>>                                 "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_BINDIR%\VBoxNetAdp6.sys VBoxNetAdp6.sys>>                                 "%_MY_OPT_DDF_FILE%"
if "%_MY_OPT_WITH_PDB%" == "1" echo %_MY_OPT_PDBDIR%\VBoxNetAdp6.pdb VBoxNetAdp6.pdb>>  "%_MY_OPT_DDF_FILE%"

echo .Set DestinationDir=VBoxNetLwf>>                                                   "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_BINDIR%\VBoxNetLwf.inf VBoxNetLwf.inf>>                                   "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_BINDIR%\VBoxNetLwf.sys VBoxNetLwf.sys>>                                   "%_MY_OPT_DDF_FILE%"
if "%_MY_OPT_WITH_PDB%" == "1" echo %_MY_OPT_PDBDIR%\VBoxNetLwf.pdb VBoxNetLwf.pdb>>    "%_MY_OPT_DDF_FILE%"

echo .Set DestinationDir=VBoxUSB>>                                                      "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_BINDIR%\VBoxUSB.inf VBoxUSB.inf>>                                         "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_BINDIR%\VBoxUSB.sys VBoxUSB.sys>>                                         "%_MY_OPT_DDF_FILE%"
if "%_MY_OPT_WITH_PDB%" == "1" echo %_MY_OPT_PDBDIR%\VBoxUSB.pdb VBoxUSB.pdb>>          "%_MY_OPT_DDF_FILE%"

echo .Set DestinationDir=VBoxUSBMon>>                                                   "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_BINDIR%\VBoxUSBMon.inf VBoxUSBMon.inf>>                                   "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_BINDIR%\VBoxUSBMon.sys VBoxUSBMon.sys>>                                   "%_MY_OPT_DDF_FILE%"
if "%_MY_OPT_WITH_PDB%" == "1" echo %_MY_OPT_PDBDIR%\VBoxUSBMon.pdb VBoxUSBMon.pdb>>    "%_MY_OPT_DDF_FILE%"

echo .Set DestinationDir=VMMR0>>                                                        "%_MY_OPT_DDF_FILE%"
echo .\VMMR0.inf VMMR0.inf>>                                                            "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_BINDIR%\VMMR0.r0 VMMR0.r0>>                                               "%_MY_OPT_DDF_FILE%"
if "%_MY_OPT_WITH_PDB%" == "1" echo %_MY_OPT_PDBDIR%\VMMR0.pdb VMMR0.pdb>>              "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_BINDIR%\VBoxDDR0.r0 VBoxDDR0.r0>>                                         "%_MY_OPT_DDF_FILE%"
if "%_MY_OPT_WITH_PDB%" == "1" echo %_MY_OPT_PDBDIR%\VBoxDDR0.pdb VBoxDDR0.pdb>>        "%_MY_OPT_DDF_FILE%"
:skip_main_package

if "%_MY_OPT_WITH_EXTPACK%" == "0" goto no_extpack_ddf
echo .Set DestinationDir=VBoxExtPackPuel>>                                              "%_MY_OPT_DDF_FILE%"
echo .\VBoxExtPackPuel.inf VBoxExtPackPuel.inf>>                                        "%_MY_OPT_DDF_FILE%"
rem echo %_MY_EXTPACK_DIR%\win.%_MY_OPT_ARCH%\VBoxEhciR0.r0 VBoxEhciR0.r0>>                 "%_MY_OPT_DDF_FILE%"
echo %_MY_EXTPACK_DIR%\win.%_MY_OPT_ARCH%\VBoxNvmeR0.r0 VBoxNvmeR0.r0>>                 "%_MY_OPT_DDF_FILE%"
rem echo %_MY_EXTPACK_DIR%\win.%_MY_OPT_ARCH%\VBoxPciRawR0.r0 VBoxPciRawR0.r0>>             "%_MY_OPT_DDF_FILE%"
:no_extpack_ddf

if "%_MY_OPT_WITH_GA%" == "0"   goto no_additions_ddf
echo .Set DestinationDir=VBoxGuestAdditions>>                                           "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_GADIR%\VBoxGuest.inf>>                                                    "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_GADIR%\VBoxGuest.sys>>                                                    "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_GADIR%\VBoxTray.exe>>                                                     "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_GADIR%\VBoxControl.exe>>                                                  "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_GADIR%\VBoxMouse.inf>>                                                    "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_GADIR%\VBoxMouse.sys>>                                                    "%_MY_OPT_DDF_FILE%"
rem VBoxVideo files are excluded from attestation signing.
rem echo %_MY_OPT_GADIR%\VBoxVideo.inf>>                                                    "%_MY_OPT_DDF_FILE%"
rem echo %_MY_OPT_GADIR%\VBoxVideo.sys>>                                                    "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_GADIR%\VBoxWddm.inf>>                                                     "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_GADIR%\VBoxWddm.sys>>                                                     "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_GADIR%\VBoxDX.dll>>                                                       "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_GADIR%\VBoxDisp.dll>>                                                     "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_GADIR%\VBoxDispD3D.dll>>                                                  "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_GADIR%\VBoxNine.dll>>                                                     "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_GADIR%\VBoxSVGA.dll>>                                                     "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_GADIR%\VBoxGL.dll>>                                                       "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_GADIR%\VBoxMRXNP.dll>>                                                    "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_GADIR%\VBoxSF.sys>>                                                       "%_MY_OPT_DDF_FILE%"
if ".%_MY_OPT_ARCH%" == ".x86" goto skip_amd64_files
echo %_MY_OPT_GADIR%\VBoxDX-x86.dll>>                                                   "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_GADIR%\VBoxDispD3D-x86.dll>>                                              "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_GADIR%\VBoxNine-x86.dll>>                                                 "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_GADIR%\VBoxSVGA-x86.dll>>                                                 "%_MY_OPT_DDF_FILE%"
echo %_MY_OPT_GADIR%\VBoxGL-x86.dll>>                                                   "%_MY_OPT_DDF_FILE%"
:skip_amd64_files

:no_additions_ddf

rem
rem Create the cabient file.
rem Note! MakeCab is shipped on W10, so we ASSUME it's in the PATH.
rem
MakeCab.exe /v2 /F "%_MY_OPT_DDF_FILE%" || goto end_failed

rem
rem EV sign the cabient file.
rem
if not exist "sign-ev.cmd" goto end
echo info: Now signing the created CAB file. Can take a few minutes...
call sign-ev.cmd "%_MY_OPT_OUTPUT%" || goto end_failed

goto end

:end_failed
@endlocal
@endlocal
@exit /b 1

:end
@endlocal
@endlocal
