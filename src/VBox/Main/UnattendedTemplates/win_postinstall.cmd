@echo off
rem $Id: win_postinstall.cmd 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
rem rem @file
rem Post installation script template for Windows.
rem
rem This runs after the target system has been booted, typically as
rem part of the first logon.
rem

rem
rem Copyright (C) 2017-2024 Oracle and/or its affiliates.
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

rem Globals.
set MY_LOG_FILE=C:\vboxpostinstall.log

rem Log header.
echo *** started >> %MY_LOG_FILE%
echo *** CD=%CD% >> %MY_LOG_FILE%
echo *** Environment BEGIN >> %MY_LOG_FILE%
set >> %MY_LOG_FILE%
echo *** Environment END >> %MY_LOG_FILE%

@@VBOX_COND_HAS_PROXY@@
set PROXY=@@VBOX_INSERT_PROXY@@
set HTTP_PROXY=%PROXY%
set HTTPS_PROXY=%PROXY%
echo HTTP proxy is %HTTP_PROXY% >> %MY_LOG_FILE%
echo HTTPS proxy is %HTTPS_PROXY% >> %MY_LOG_FILE%
@@VBOX_COND_END@@

@@VBOX_COND_IS_INSTALLING_ADDITIONS@@
rem
rem Install the guest additions.
rem

rem First find the CDROM with the GAs on them.
set MY_VBOX_ADDITIONS=E:\vboxadditions
if exist %MY_VBOX_ADDITIONS%\VBoxWindowsAdditions.exe goto found_vbox_additions
set MY_VBOX_ADDITIONS=D:\vboxadditions
if exist %MY_VBOX_ADDITIONS%\VBoxWindowsAdditions.exe goto found_vbox_additions
set MY_VBOX_ADDITIONS=F:\vboxadditions
if exist %MY_VBOX_ADDITIONS%\VBoxWindowsAdditions.exe goto found_vbox_additions
set MY_VBOX_ADDITIONS=G:\vboxadditions
if exist %MY_VBOX_ADDITIONS%\VBoxWindowsAdditions.exe goto found_vbox_additions
set MY_VBOX_ADDITIONS=E:
if exist %MY_VBOX_ADDITIONS%\VBoxWindowsAdditions.exe goto found_vbox_additions
set MY_VBOX_ADDITIONS=F:
if exist %MY_VBOX_ADDITIONS%\VBoxWindowsAdditions.exe goto found_vbox_additions
set MY_VBOX_ADDITIONS=G:
if exist %MY_VBOX_ADDITIONS%\VBoxWindowsAdditions.exe goto found_vbox_additions
set MY_VBOX_ADDITIONS=D:
if exist %MY_VBOX_ADDITIONS%\VBoxWindowsAdditions.exe goto found_vbox_additions
set MY_VBOX_ADDITIONS=E:\vboxadditions
:found_vbox_additions
echo *** MY_VBOX_ADDITIONS=%MY_VBOX_ADDITIONS%\ >> %MY_LOG_FILE%

rem Then add signing certificate to trusted publishers
echo *** Running: %MY_VBOX_ADDITIONS%\cert\VBoxCertUtil.exe ... >> %MY_LOG_FILE%
%MY_VBOX_ADDITIONS%\cert\VBoxCertUtil.exe add-trusted-publisher %MY_VBOX_ADDITIONS%\cert\vbox*.cer --root %MY_VBOX_ADDITIONS%\cert\vbox*.cer >> %MY_LOG_FILE% 2>&1
echo *** ERRORLEVEL: %ERRORLEVEL% >> %MY_LOG_FILE%

rem Then do the installation.
echo *** Running: %MY_VBOX_ADDITIONS%\VBoxWindowsAdditions.exe /S >> %MY_LOG_FILE%
%MY_VBOX_ADDITIONS%\VBoxWindowsAdditions.exe /S
echo *** ERRORLEVEL: %ERRORLEVEL% >> %MY_LOG_FILE%

@@VBOX_COND_END@@


@@VBOX_COND_IS_INSTALLING_TEST_EXEC_SERVICE@@
rem
rem Install the Test Execution service
rem

rem First find the CDROM with the validation kit on it.
set MY_VBOX_VALIDATION_KIT=E:\vboxvalidationkit
if exist %MY_VBOX_VALIDATION_KIT%\vboxtxs-readme.txt goto found_vbox_validation_kit
set MY_VBOX_VALIDATION_KIT=D:\vboxvalidationkit
if exist %MY_VBOX_VALIDATION_KIT%\vboxtxs-readme.txt goto found_vbox_validation_kit
set MY_VBOX_VALIDATION_KIT=F:\vboxvalidationkit
if exist %MY_VBOX_VALIDATION_KIT%\vboxtxs-readme.txt goto found_vbox_validation_kit
set MY_VBOX_VALIDATION_KIT=G:\vboxvalidationkit
if exist %MY_VBOX_VALIDATION_KIT%\vboxtxs-readme.txt goto found_vbox_validation_kit
set MY_VBOX_VALIDATION_KIT=E:
if exist %MY_VBOX_VALIDATION_KIT%\vboxtxs-readme.txt goto found_vbox_validation_kit
set MY_VBOX_VALIDATION_KIT=F:
if exist %MY_VBOX_VALIDATION_KIT%\vboxtxs-readme.txt goto found_vbox_validation_kit
set MY_VBOX_VALIDATION_KIT=G:
if exist %MY_VBOX_VALIDATION_KIT%\vboxtxs-readme.txt goto found_vbox_validation_kit
set MY_VBOX_VALIDATION_KIT=D:
if exist %MY_VBOX_VALIDATION_KIT%\vboxtxs-readme.txt goto found_vbox_validation_kit
set MY_VBOX_VALIDATION_KIT=E:\vboxvalidationkit
:found_vbox_validation_kit
echo *** MY_VBOX_VALIDATION_KIT=%MY_VBOX_VALIDATION_KIT%\ >> %MY_LOG_FILE%

rem Copy over the files.
echo *** Running: mkdir %SystemDrive%\Apps >> %MY_LOG_FILE%
mkdir %SystemDrive%\Apps >> %MY_LOG_FILE% 2>&1
echo *** ERRORLEVEL: %ERRORLEVEL% >> %MY_LOG_FILE%

echo *** Running: copy %MY_VBOX_VALIDATION_KIT%\win\* %SystemDrive%\Apps >> %MY_LOG_FILE%
copy %MY_VBOX_VALIDATION_KIT%\win\* %SystemDrive%\Apps >> %MY_LOG_FILE% 2>&1
echo *** ERRORLEVEL: %ERRORLEVEL% >> %MY_LOG_FILE%

echo *** Running: copy %MY_VBOX_VALIDATION_KIT%\win\%PROCESSOR_ARCHITECTURE%\* %SystemDrive%\Apps >> %MY_LOG_FILE%
copy %MY_VBOX_VALIDATION_KIT%\win\%PROCESSOR_ARCHITECTURE%\* %SystemDrive%\Apps >> %MY_LOG_FILE% 2>&1
echo *** ERRORLEVEL: %ERRORLEVEL% >> %MY_LOG_FILE%

rem Configure the firewall to allow TXS to listen.
echo *** Running: netsh firewall add portopening TCP 5048 "TestExecService 5048" >> %MY_LOG_FILE%
netsh firewall add portopening TCP 5048 "TestExecService 5048" >> %MY_LOG_FILE% 2>&1
echo *** ERRORLEVEL: %ERRORLEVEL% >> %MY_LOG_FILE%

echo *** Running: netsh firewall add portopening TCP 5042 "TestExecService 5042" >> %MY_LOG_FILE%
netsh firewall add portopening TCP 5042 "TestExecService 5042" >> %MY_LOG_FILE% 2>&1
echo *** ERRORLEVEL: %ERRORLEVEL% >> %MY_LOG_FILE%

rem Update the registry to autorun the service and make sure we've got autologon.
echo *** Running: reg.exe ADD HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run /v NTConfiguration /d %SystemDrive%\Apps\vboxtxs.cmd /f >> %MY_LOG_FILE%
reg.exe ADD HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run /v NTConfiguration /d %SystemDrive%\Apps\vboxtxs.cmd /f >> %MY_LOG_FILE% 2>&1
echo *** ERRORLEVEL: %ERRORLEVEL% >> %MY_LOG_FILE%

echo *** Running: reg.exe ADD "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon" /v PowerdownAfterShutdown /d 1 /f >> %MY_LOG_FILE%
reg.exe ADD "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon" /v PowerdownAfterShutdown /d 1 /f >> %MY_LOG_FILE% 2>&1
echo *** ERRORLEVEL: %ERRORLEVEL% >> %MY_LOG_FILE%

echo *** Running: reg.exe ADD "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon" /v ForceAutoLogon /d 1 /f >> %MY_LOG_FILE%
reg.exe ADD "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon" /v ForceAutoLogon /d 1 /f >> %MY_LOG_FILE% 2>&1
echo *** ERRORLEVEL: %ERRORLEVEL% >> %MY_LOG_FILE%
rem  AutoAdminLogon too if administrator?

@@VBOX_COND_END@@


@@VBOX_COND_HAS_POST_INSTALL_COMMAND@@
rem
rem Run user command.
rem
echo *** Running custom user command ... >> %MY_LOG_FILE%
echo *** Running: "@@VBOX_INSERT_POST_INSTALL_COMMAND@@" >> %MY_LOG_FILE%
@@VBOX_INSERT_POST_INSTALL_COMMAND@@
@@VBOX_COND_END@@

rem
rem Eject/rename no longer needed unattended install configuration and media.
rem
if exist a:\autounattend.xml ren a:\autounattend.xml autounattend-disabled.xml
rem rem @todo eject DVD install media

echo *** done >> %MY_LOG_FILE%

