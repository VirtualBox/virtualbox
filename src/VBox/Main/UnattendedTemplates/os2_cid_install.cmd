@ECHO OFF
REM $Id: os2_cid_install.cmd 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
REM REM @fileREM
REM VirtualBox CID Installation - main driver script for boot CD/floppy.
REM

REM
REM Copyright (C) 2004-2024 Oracle and/or its affiliates.
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

REM Check the phase argument and jump to the right section of the file.
if "%1" == "PHASE1" goto phase1
if "%1" == "PHASE2" goto phase2
if "%1" == "PHASE3" goto phase3
@echo ** error: invalid or missing parameter. Expected PHASE1, PHASE2 or PHASE3 as the first parameter to the script.
pause
cmd.exe
exit /b 1

REM
REM Phase 1 - Base system installation.
REM
:phase1
SET CDROM=S:

@echo on
@echo .
@echo Step 1.1 - Partition the disk.
@echo .
cd %CDROM%\os2image\disk_6
%CDROM%

lvm.exe /NEWMBR:1 && goto lvm_newmbr_ok
@echo ** error: Writing a new MBR on disk 1 failed.
goto lvm_failed
:lvm_newmbr_ok

@REM Depends the default drive name being "[ D1 ]".  However it's cosmetical,
@REM so we don't complain if this fails.
lvm.exe "/SETNAME:DRIVE,[ D1 ],BootDrive"

lvm.exe /CREATE:PARTITION,OS2Boot,1,1024,PRIMARY,BOOTABLE && goto lvm_create_partition_ok
@echo ** error: Creating boot partition on disk 1 failed.
goto lvm_failed
:lvm_create_partition_ok

lvm.exe /CREATE:VOLUME,COMPATIBILITY,BOOTOS2,C:,OS2Boot,1,OS2Boot && goto lvm_create_volume_ok
@echo ** error: Creating boot volume on disk 1 failed.
goto lvm_failed
:lvm_create_volume_ok

lvm.exe /SETSTARTABLE:VOLUME,OS2Boot && goto lvm_set_startable_ok
@echo ** error: Setting boot volume on disk 1 startable failed.
goto lvm_failed
:lvm_set_startable_ok

@REM Depending on the freespace automatically getting the name "[ FS1 ]".
lvm.exe "/CREATE:PARTITION,Data,1,LOGICAL,NotBootable,[ FS1 ]" && goto lvm_create_data_partition_ok
@echo ** error: Creating data partition on disk 1 failed.
goto lvm_failed
:lvm_create_data_partition_ok

lvm.exe /CREATE:VOLUME,LVM,D:,Data,1,Data && goto lvm_create_data_volume_ok
@echo ** error: Creating data volume on disk 1 failed.
goto lvm_failed
:lvm_create_data_volume_ok

REM pause
lvm.exe /QUERY
REM CMD.EXE
goto done_step1_1

:lvm_failed
@echo .
@echo An LVM operation failed (see above).
@echo The process requires a blank disk with no partitions.  Starting LVM
@echo so you can manually correct this.
@echo .
pause
lvm.exe
%CDROM%\cid\exe\os2\setboot.exe /B
exit

:done_step1_1

:step1_2
@echo .
@echo Step 1.2 - Format the volumes.
@echo .
cd %CDROM%\os2image\disk_3
%CDROM%

FORMAT.COM C: /FS:HPFS /V:OS2Boot < %CDROM%\VBoxCID\YES.TXT && goto format_boot_ok
@echo ** error: Formatting C: failed.
pause
:format_boot_ok

FORMAT.COM D: /FS:JFS /V:Data < %CDROM%\VBoxCID\YES.TXT && goto format_data_ok
@echo ** error: Formatting D: failed.
pause
:format_data_ok

cd \

:step1_3
@echo .
@echo Step 1.3 - Putting response files and CID tools on C:
@echo .
mkdir C:\VBoxCID
mkdir C:\OS2
copy %CDROM%\cid\exe\os2\*.*            C:\VBoxCID
copy %CDROM%\cid\dll\os2\*.*            C:\VBoxCID
copy %CDROM%\os2image\disk_2\inst32.dll C:\VBoxCID
copy %CDROM%\VBoxCID\*.*                C:\VBoxCID && goto copy_1_ok
@echo ** error: Copying CID stuff from CDROM to C: failed (#1).
pause
:copy_1_ok
copy %CDROM%\VBoxCID.CMD                C:\VBoxCID && goto copy_2_ok
@echo ** error: Copying CID stuff from CDROM to C: failed (#2).
pause
:copy_2_ok

:step1_4
@echo .
@echo Step 1.4 - Start OS/2 CID installation.
@echo .
SET REMOTE_INSTALL_STATE=CAS_WARP4
cd C:\OS2
C:
@REM Treat 0xfe00 as a success status. It seems to mean that a reboot is required.
C:\VBoxCID\OS2_UTIL.EXE --as-zero 0xfe00 -- C:\VBoxCID\SEMAINT.EXE /S:%CDROM%\os2image /B:C: /L1:C:\VBoxCID\1.4.1-Maint.log /T:C:\OS2 && goto semaint_ok
C:\VBoxCID\OS2_UTIL.EXE --file-to-backdoor C:\VBoxCID\1.4.1-Maint.log
pause
:semaint_ok
REM CMD.EXE

cd C:\VBoxCID
C:
@REM Treat 0xff02 as a success status. It seems to mean that a reboot is required.
C:\VBoxCID\OS2_UTIL.EXE --as-zero 0xff02 -- C:\VBoxCID\SEINST.EXE /S:%CDROM%\os2image /B:C: /L1:C:\VBoxCID\1.4.2-CIDInst.log /R:C:\VBoxCID\OS2.RSP /T:A:\ && goto seinst_ok
C:\VBoxCID\OS2_UTIL.EXE --file-to-backdoor C:\VBoxCID\1.4.2-CIDInst.log
pause
:seinst_ok
REM CMD.EXE

:step1_5
@echo .
@echo Step 1.5 - Make C: bootable.
@echo .
C:
cd C:\OS2
SYSINSTX.COM C: && goto sysinstx_ok
pause
:sysinstx_ok

@echo Copying over patched OS2LDR from A:
attrib -R -H -S C:\OS2LDR
copy C:\OS2LDR C:\OS2LDR.Phase1
del  C:\OS2LDR
copy A:\OS2LDR C:\OS2LDR && goto copy_os2ldr_ok
pause
:copy_os2ldr_ok
attrib +R +H +S C:\OS2LDR

@REM This copy is for the end of phase 2 as someone replaces it.
copy A:\OS2LDR C:\VBoxCID && goto copy_os2ldr_2_ok
pause
:copy_os2ldr_2_ok
attrib +r C:\VBoxCID\OS2LDR

@echo Enabling Alt-F2 driver logging during boot.
@echo > "C:\ALTF2ON.$$$"

@echo Install startup.cmd for phase2.
@echo C:\VBoxCID\OS2_UTIL.EXE --tee-to-backdoor --tee-to-file C:\VBoxCID\Phase2.log --append -- C:\OS2\CMD.EXE /C C:\VBoxCID\VBoxCID.CMD PHASE2> C:\STARTUP.CMD && goto phase2_startup_ok
pause
:phase2_startup_ok

copy C:\CONFIG.SYS C:\VBoxCID\Phase1-end-config.sys

REM now reboot.
goto reboot


REM
REM Phase 2 - Install GRADD drivers (VGA is horribly slow).
REM
:phase2
SET CDROM=E:
IF EXIST "%CDROM%\VBoxCID.CMD" goto phase2_found_cdrom
SET CDROM=D:
IF EXIST "%CDROM%\VBoxCID.CMD" goto phase2_found_cdrom
SET CDROM=F:
IF EXIST "%CDROM%\VBoxCID.CMD" goto phase2_found_cdrom
SET CDROM=G:
IF EXIST "%CDROM%\VBoxCID.CMD" goto phase2_found_cdrom
SET CDROM=H:
IF EXIST "%CDROM%\VBoxCID.CMD" goto phase2_found_cdrom
SET CDROM=S:
IF EXIST "%CDROM%\VBoxCID.CMD" goto phase2_found_cdrom
@echo ** error: Unable to find the CDROM drive
pause
CMD
SET CDROM=E:
:phase2_found_cdrom
cd C:\VBoxCID
C:

@echo on

:step2_1
@echo .
@echo Step 2.1 - Install the video driver.
@echo .
@REM Treat 0xfe00 as a success status. It seems to mean that a reboot is required.
C:\VBoxCID\OS2_UTIL.EXE --as-zero 0xfe00 -- C:\OS2\INSTALL\DspInstl.EXE /PD:C:\OS2\INSTALL\GENGRADD.DSC /S:%CDROM%\OS2IMAGE /T:C: /RES:1024X768X16777216 /U && goto dspinstl_ok
C:\VBoxCID\OS2_UTIL.EXE --file-to-backdoor C:\OS2\INSTALL\DSPINSTL.LOG
C:\VBoxCID\OS2_UTIL.EXE --file-to-backdoor C:\OS2\INSTALL\GRADD.LOG
pause
:dspinstl_ok

@REM TODO: Error: 1 Error getting current desktop mode
@REM UPDATE: This is probably not working because SVGA.EXE doesn't want to play along with our graphics adapter,
@REM         so it looks like there is no simple way of changing the resolution or select a better monitor.
call VCfgCID.CMD /L1:C:\VBoxCID\2.1-Video.log /L2:C:\VBoxCID\2.1-Video-2.log /RES:1024X768X16777216 /MON:548
goto vcfgcid_ok
C:\VBoxCID\OS2_UTIL.EXE --file-to-backdoor C:\VBoxCID\2.1-Video.log
pause
:vcfgcid_ok
cd C:\VBoxCID
C:

:step2_2
@echo Install startup.cmd for phase3.
ren C:\STARTUP.CMD C:\VBoxCID\Phase2-end-startup.cmd
copy C:\CONFIG.SYS C:\VBoxCID\Phase2-end-config.sys
@echo C:\VBoxCID\OS2_UTIL.EXE --tee-to-backdoor --tee-to-file C:\VBoxCID\Phase3.log --append -- C:\OS2\CMD.EXE /C C:\VBoxCID\VBoxCID.CMD PHASE3> C:\STARTUP.CMD && goto phase3_startup_ok
pause
:phase3_startup_ok

REM now reboot.
goto reboot


REM
REM Phase 2 - The rest of the installation running of the base install with fast GRADD drivers.
REM
:phase3
SET CDROM=E:
IF EXIST "%CDROM%\VBoxCID.CMD" goto phase3_found_cdrom
SET CDROM=D:
IF EXIST "%CDROM%\VBoxCID.CMD" goto phase3_found_cdrom
SET CDROM=F:
IF EXIST "%CDROM%\VBoxCID.CMD" goto phase3_found_cdrom
SET CDROM=G:
IF EXIST "%CDROM%\VBoxCID.CMD" goto phase3_found_cdrom
SET CDROM=H:
IF EXIST "%CDROM%\VBoxCID.CMD" goto phase3_found_cdrom
SET CDROM=S:
IF EXIST "%CDROM%\VBoxCID.CMD" goto phase3_found_cdrom
@echo ** error: Unable to find the CDROM drive
pause
CMD
SET CDROM=E:
:phase3_found_cdrom
cd C:\VBoxCID
C:

@echo on

:step3_1
@echo .
@echo Step 3.1 - Install multimedia.
@echo .
cd C:\mmtemp
C:
@REM Does not have any /L, /L1, or /L2 options.  Fixed log file: C:\MINSTALL.LOG.
@REM Treat 0xfe00 as a success status. It seems to mean that a reboot is required.
C:\VBoxCID\OS2_UTIL.EXE --as-zero 0xfe00 -- MInstall.EXE /M /R:C:\VBoxCID\MMOS2.RSP && goto mmos2_ok
C:\VBoxCID\OS2_UTIL.EXE --file-to-backdoor C:\MINSTALL.LOG
pause
:mmos2_ok
cd C:\VBoxCID

:step3_2
@echo .
@echo Step 3.2 - Install features.
@echo .
@REM Treat 0xfe00 as a success status. It seems to mean that a reboot is required.
C:\VBoxCID\OS2_UTIL.EXE --as-zero 0xfe00 -- CLIFI.EXE /A:C /B:C: /S:%CDROM%\os2image\fi /R:C:\OS2\INSTALL\FIBASE.RSP /L1:C:\VBoxCID\3.2-FeatureInstaller.log /R2:C:\VBoxCID\OS2.RSP
@REM does not exit with status 0 on success.
goto features_ok
C:\VBoxCID\OS2_UTIL.EXE --file-to-backdoor C:\VBoxCID\3.2-FeatureInstaller.log
pause
:features_ok

:step3_3
@echo .
@echo Step 3.3 - Install MPTS.
@echo .
@REM If we want to use non-standard drivers like the intel ones, copy the .NIF- and
@REM .OS2-files to C:\IBMCOM\MACS before launching the installer (needs creating first).
@REM Note! Does not accept /L2:.
@REM Note! Omitting /TU:C in hope that it solves the lan install failure (no netbeui configured in mpts).
CD %CDROM%\CID\SERVER\MPTS
%CDROM%
@REM Treat 0xfe00 as a success status. It seems to mean that a reboot is required.
C:\VBoxCID\OS2_UTIL.EXE --as-zero 0xfe00 -- %CDROM%\CID\SERVER\MPTS\MPTS.EXE /R:C:\VBoxCID\MPTS.RSP /S:%CDROM%\CID\SERVER\MPTS /T:C: /L1:C:\VBoxCID\3.3-Mpts.log && goto mpts_ok
C:\VBoxCID\OS2_UTIL.EXE --file-to-backdoor C:\VBoxCID\3.3-Mpts.log
pause
:mpts_ok
CD %CDROM%\
C:

:step3_4
@echo .
@echo Step 3.4 - Install TCP/IP.
@echo .
CD %CDROM%\CID\SERVER\TCPAPPS
%CDROM%
@REM Treat 0xfe00 as a success status. It seems to mean that a reboot is required.
C:\VBoxCID\OS2_UTIL.EXE --as-zero 0xfe00 -- CLIFI.EXE /A:C /B:C: /S:%CDROM%\CID\SERVER\TCPAPPS\INSTALL /R:%CDROM%\CID\SERVER\TCPAPPS\INSTALL\TCPINST.RSP /L1:C:\VBoxCID\3.4-tcp.log /L2:C:\VBoxCID\3.4-tcp-2.log && goto tcp_ok
C:\VBoxCID\OS2_UTIL.EXE --file-to-backdoor C:\VBoxCID\3.4-tcp.log
pause
:tcp_ok
CD %CDROM%\
C:

CD %CDROM%\CID\SERVER\TCPAPPS\INSTALL
%CDROM%
C:\VBoxCID\OS2_UTIL.EXE -- %CDROM%\CID\SERVER\TCPAPPS\INSTALL\makecmd.exe C:\TCPIP en_US C:\MPTS && goto makecmd_ok
pause
:makecmd_ok
cd %CDROM%\

:step3_5
@echo .
@echo Step 3.5 - Install IBM LAN Requestor/Peer.
@echo .
SET REMOTE_INSTALL_STATE=CAS_OS/2 Peer
CD %CDROM%\CID\SERVER\IBMLS
%CDROM%
C:\VBoxCID\OS2_UTIL.EXE -- %CDROM%\CID\SERVER\IBMLS\LANINSTR.EXE /REQ /R:C:\VBoxCID\IBMLan.rsp /L1:C:\VBoxCID\3.5-IBMLan.log /L2:C:\VBoxCID\3.5-IBMLan-2.log && goto ibmlan_ok
C:\VBoxCID\OS2_UTIL.EXE --file-to-backdoor C:\VBoxCID\3.5-IBMLan.log
:ibmlan_ok
CD %CDROM%\
C:

:step3_6
@echo .
@echo Step 3.6 - Install Netscape.
@echo .
CD C:\VBoxCID
C:
%CDROM%
@REM Skipping as it hangs after a "Message file not found." error. (The DPATH amendment doesn't help.)  Logs give no clue.
@REM The install works fine after the phase3 reboot. Next log message then is "NS46EXIT QLTOBMCONVERT en_US, rc=0x0000",
@REM so maybe it is related to the LANG environment variable or Locale? Hmm. LANG seems to be set...
goto netscape_ok
SET DPATH=%DPATH%;C:\NETSCAPE\SIUTIL;C:\NETSCAPE\PROGRAM;
IF "x%LANG%x" == "xx" THEN SET LANG=en_US
C:\VBoxCID\OS2_UTIL.EXE -- %CDROM%\CID\SERVER\NETSCAPE\INSTALL.EXE /X /A:I /TU:C: /C:%CDROM%\CID\SERVER\NETSCAPE\NS46.ICF /S:%CDROM%\CID\SERVER\NETSCAPE /R:C:\VBoxCID\Netscape.RSP /L1:C:\VBoxCID\3.6-Netscape.log /L2:C:\VBoxCID\3.6-Netscape-2.log && goto netscape_ok
C:\VBoxCID\OS2_UTIL.EXE --file-to-backdoor C:\VBoxCID\3.6-Netscape.log
pause
:netscape_ok
CD %CDROM%\
C:

:step3_7
@echo .
@echo Step 3.7 - Install feature installer.
@echo .
@REM No /L2: support.
@REM The /NN option is to make it not fail if netscape is missing.
C:\VBoxCID\OS2_UTIL.EXE -- C:\OS2\INSTALL\WSFI\FiSetup.EXE /B:C: /S:C:\OS2\INSTALL\WSFI\FISETUP /NN /L1:C:\VBoxCID\3.7-FiSetup.log && goto fisetup_ok
C:\VBoxCID\OS2_UTIL.EXE --file-to-backdoor C:\VBoxCID\3.7-FiSetup.log
pause
:fisetup_ok

:step3_8
@echo .
@echo Step 3.8 - Install the test execution service (TXS).
@echo .
@@VBOX_COND_IS_INSTALLING_TEST_EXEC_SERVICE@@
mkdir C:\VBoxValKit
mkdir D:\TestArea
copy %CDROM%\VBoxValidationKit\*.* C:\VBoxValKit && goto valkit_copy_1_ok
pause
:valkit_copy_1_ok
copy %CDROM%\VBoxValidationKit\os2\x86\*.* C:\VBoxValKit && goto valkit_copy_2_ok
pause
:valkit_copy_2_ok
@@VBOX_COND_ELSE@@
@echo Not requested. Skipping.
@@VBOX_COND_END@@

:step3_9
@echo .
@echo Step 3.9 - Install final startup.cmd and copy over OS2LDR again.
@echo .
attrib -r -h -s C:\STARTUP.CMD
copy C:\VBoxCID\STARTUP.CMD C:\ && goto final_startup_ok
pause
:final_startup_ok

attrib -r -h -s C:\OS2LDR
if not exist C:\VBoxCID\OS2LDR pause
if not exist C:\VBoxCID\OS2LDR goto final_os2ldr_ok
copy C:\OS2LDR C:\OS2LDR.Phase2
del  C:\OS2LDR
copy C:\VBoxCID\OS2LDR C:\OS2LDR && goto final_os2ldr_ok
pause
:final_os2ldr_ok
attrib +r +h +s C:\OS2LDR

:step3_10
@REM Putting this after placing the final Startup.cmd so we can test the
@REM installer's ability to parse and modify it.
@echo .
@echo Step 3.10 - Install guest additions.
@echo .
@@VBOX_COND_IS_INSTALLING_ADDITIONS@@
%CDROM%\VBoxAdditions\OS2\VBoxOs2AdditionsInstall.exe --do-install && goto addition_install_ok
pause
:addition_install_ok
@@VBOX_COND_ELSE@@
@echo Not requested. Skipping.
@@VBOX_COND_END@@

:step3_11
@echo .
@echo Step 3.11 - Cleanup
@echo .
del /N C:\*.bio
del /N C:\*.i13
del /N C:\*.snp
del /N C:\CONFIG.ADD
mkdir C:\MMTEMP 2>nul
del /N C:\MMTEMP\*.*
@REM This is only needed if we don't install mmos2:
@REM for %%i in (acpadd2 azt16dd azt32dd csbsaud es1688dd es1788dd es1868dd es1888dd es688dd jazzdd mvprobdd mvprodd sb16d2 sbawed2 sbd2 sbp2d2 sbpd2) do del /N C:\MMTEMP\OS2\DRIVERS\%%i\*.*
@REM for %%i in (acpadd2 azt16dd azt32dd csbsaud es1688dd es1788dd es1868dd es1888dd es688dd jazzdd mvprobdd mvprodd sb16d2 sbawed2 sbd2 sbp2d2 sbpd2) do rmdir C:\MMTEMP\OS2\DRIVERS\%%i
@REM rmdir C:\MMTEMP\OS2\DRIVERS
@REM rmdir C:\MMTEMP\OS2
rmdir C:\MMTEMP
copy C:\CONFIG.SYS C:\VBoxCID || goto skip_sys_cleanup
del /N C:\*.SYS
copy C:\VBoxCID\CONFIG.SYS C:\
:skip_sys_cleanup

:step3_12
@@VBOX_COND_HAS_POST_INSTALL_COMMAND@@
@echo .
@echo Step 3.12 - Custom actions: "@@VBOX_INSERT_POST_INSTALL_COMMAND@@"
@echo .
cd C:\VBoxCID
C:
@@VBOX_INSERT_POST_INSTALL_COMMAND@@
@@VBOX_COND_END@@

copy C:\CONFIG.SYS C:\VBoxCID\Phase3-end-config.sys


REM
REM Reboot (common to both phases).
REM
:reboot
@echo .
@echo Reboot (%1)
@echo .
cd C:\OS2
C:

@REM @echo debug
@REM CMD.EXE

SETBOOT /IBD:C
pause
CMD.EXE

