; $Id: VBoxGuestAdditionsNT4.nsh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; VBoxGuestAdditionsNT4.nsh - Guest Additions installation for NT4.
;

;
; Copyright (C) 2006-2024 Oracle and/or its affiliates.
;
; This file is part of VirtualBox base platform packages, as
; available from https://www.virtualbox.org.
;
; This program is free software; you can redistribute it and/or
; modify it under the terms of the GNU General Public License
; as published by the Free Software Foundation, in version 3 of the
; License.
;
; This program is distributed in the hope that it will be useful, but
; WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
; General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program; if not, see <https://www.gnu.org/licenses>.
;
; SPDX-License-Identifier: GPL-3.0-only
;

Function NT4_SetVideoResolution

  ; Check for all required parameters
  StrCmp $g_iScreenX "0" missingParms
  StrCmp $g_iScreenY "0" missingParms
  StrCmp $g_iScreenBpp "0" missingParms
  Goto haveParms

missingParms:

  ${LogVerbose} "Missing display parameters for NT4, setting default (640x480, 8 BPP) ..."

  StrCpy $g_iScreenX '640'   ; Default value
  StrCpy $g_iScreenY '480'   ; Default value
  StrCpy $g_iScreenBpp '8'   ; Default value

  ; Write setting into registry to show the desktop applet on next boot
  WriteRegStr HKLM "SYSTEM\CurrentControlSet\Control\GraphicsDrivers\NewDisplay" "" ""

haveParms:

  ${LogVerbose} "Setting display parameters for NT4 ($g_iScreenXx$g_iScreenY, $g_iScreenBpp BPP) ..."

  WriteRegDWORD HKLM "SYSTEM\CurrentControlSet\Hardware Profiles\Current\System\CurrentControlSet\Services\vboxvideo\Device0" "DefaultSettings.BitsPerPel" $g_iScreenBpp
  WriteRegDWORD HKLM "SYSTEM\CurrentControlSet\Hardware Profiles\Current\System\CurrentControlSet\Services\vboxvideo\Device0" "DefaultSettings.Flags" 0x00000000
  WriteRegDWORD HKLM "SYSTEM\CurrentControlSet\Hardware Profiles\Current\System\CurrentControlSet\Services\vboxvideo\Device0" "DefaultSettings.VRefresh" 0x00000001
  WriteRegDWORD HKLM "SYSTEM\CurrentControlSet\Hardware Profiles\Current\System\CurrentControlSet\Services\vboxvideo\Device0" "DefaultSettings.XPanning" 0x00000000
  WriteRegDWORD HKLM "SYSTEM\CurrentControlSet\Hardware Profiles\Current\System\CurrentControlSet\Services\vboxvideo\Device0" "DefaultSettings.XResolution" $g_iScreenX
  WriteRegDWORD HKLM "SYSTEM\CurrentControlSet\Hardware Profiles\Current\System\CurrentControlSet\Services\vboxvideo\Device0" "DefaultSettings.YPanning" 0x00000000
  WriteRegDWORD HKLM "SYSTEM\CurrentControlSet\Hardware Profiles\Current\System\CurrentControlSet\Services\vboxvideo\Device0" "DefaultSettings.YResolution" $g_iScreenY

FunctionEnd

Function NT4_SaveMouseDriverInfo

  Push $0

  ; !!! NOTE !!!
  ; Due to some re-branding (see functions Uninstall_Sun, Uninstall_Innotek and
  ; Uninstall_SunXVM) the installer *has* to transport the very first saved i8042prt
  ; value to the current installer's "uninstall" directory in both mentioned
  ; functions above, otherwise NT4 will be screwed because it then would store
  ; "VBoxMouseNT.sys" as the original i8042prt driver which obviously isn't there
  ; after uninstallation anymore
  ; !!! NOTE !!!

  ; Save current mouse driver info so we may restore it on uninstallation
  ; But first check if we already installed the additions otherwise we will
  ; overwrite it with the VBoxMouseNT.sys
  ReadRegStr $0 HKLM "${PRODUCT_UNINST_KEY}" ${ORG_MOUSE_PATH}
  StrCmp $0 "" 0 exists

  ${LogVerbose} "Saving mouse driver info ..."
  ReadRegStr $0 HKLM "SYSTEM\CurrentControlSet\Services\i8042prt" "ImagePath"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" ${ORG_MOUSE_PATH} $0
  Goto exit

exists:

  ${LogVerbose} "Mouse driver info already saved."
  Goto exit

exit:

!ifdef _DEBUG
  ${LogVerbose} "Mouse driver info: $0"
!endif

  Pop $0

FunctionEnd

Function NT4_Prepare

  ${If} $g_bNoVBoxServiceExit == "false"
    ; Stop / kill VBoxService
    Call StopVBoxService
  ${EndIf}

  ${If} $g_bNoVBoxTrayExit == "false"
    ; Stop / kill VBoxTray
    Call StopVBoxTray
  ${EndIf}

  ; Delete VBoxService from registry
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "VBoxService"

  ; Delete old VBoxService.exe from install directory (replaced by VBoxTray.exe)
  Delete /REBOOTOK "$INSTDIR\VBoxService.exe"

FunctionEnd

Function NT4_CopyFiles

  ${LogVerbose} "Copying files for NT4 ..."

  SetOutPath "$INSTDIR"
  FILE "$%PATH_OUT%\bin\additions\RegCleanup.exe"
  AccessControl::SetOnFile "$INSTDIR\RegCleanup.exe" "(BU)" "GenericRead"
!ifdef VBOX_WITH_ADDITIONS_SHIPPING_AUDIO_TEST
  FILE "$%PATH_OUT%\bin\additions\VBoxAudioTest.exe"
!endif

  ; The files to install for NT 4, they go into the system directories
  SetOutPath "$SYSDIR"
  FILE "$%PATH_OUT%\bin\additions\VBoxDisp.dll"
  AccessControl::SetOnFile "$SYSDIR\VBoxDisp.dll" "(BU)" "GenericRead"
  FILE "$%PATH_OUT%\bin\additions\VBoxTray.exe"
  AccessControl::SetOnFile "$SYSDIR\VBoxTray.exe" "(BU)" "GenericRead"
  FILE "$%PATH_OUT%\bin\additions\VBoxHook.dll"
  AccessControl::SetOnFile "$SYSDIR\VBoxHook.dll" "(BU)" "GenericRead"
  FILE "$%PATH_OUT%\bin\additions\VBoxControl.exe"
  AccessControl::SetOnFile "$SYSDIR\VBoxControl.exe" "(BU)" "GenericRead"

  ; VBoxService
  FILE "$%PATH_OUT%\bin\additions\VBoxService.exe"
  AccessControl::SetOnFile "$SYSDIR\VBoxService.exe" "(BU)" "GenericRead"

  ; The drivers into the "drivers" directory
  SetOutPath "$SYSDIR\drivers"
  FILE "$%PATH_OUT%\bin\additions\VBoxVideo.sys"
  AccessControl::SetOnFile "$SYSDIR\drivers\VBoxVideo.sys" "(BU)" "GenericRead"
  FILE "$%PATH_OUT%\bin\additions\VBoxMouseNT.sys"
  AccessControl::SetOnFile "$SYSDIR\drivers\VBoxMouseNT.sys" "(BU)" "GenericRead"
  FILE "$%PATH_OUT%\bin\additions\VBoxGuest.sys"
  AccessControl::SetOnFile "$SYSDIR\drivers\VBoxGuest.sys" "(BU)" "GenericRead"
  ;FILE "$%PATH_OUT%\bin\additions\VBoxSFNT.sys" ; Shared Folders not available on NT4!
  ;AccessControl::SetOnFile "$SYSDIR\drivers\VBoxSFNT.sys" "(BU)" "GenericRead"

FunctionEnd

Function NT4_InstallFiles

  ${LogVerbose} "Installing drivers for NT4 ..."

  ; Install guest driver
  ${CmdExecute} "$\"$INSTDIR\VBoxDrvInst.exe$\" service create $\"VBoxGuest$\" $\"VBoxGuest Support Driver$\" 1 1 $\"$SYSDIR\drivers\VBoxGuest.sys$\" $\"Base$\"" 'non-zero-exitcode=abort'

  ; Bugfix: Set "Start" to 1, otherwise, VBoxGuest won't start on boot-up!
  ; Bugfix: Correct invalid "ImagePath" (\??\C:\WINNT\...)
  WriteRegDWORD HKLM "SYSTEM\CurrentControlSet\Services\VBoxGuest" "Start" 1
  WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\VBoxGuest" "ImagePath" "\SystemRoot\System32\DRIVERS\VBoxGuest.sys"

  ; Run VBoxTray when Windows NT starts
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "VBoxTray" '"$SYSDIR\VBoxTray.exe"'

  ; Video driver
  ${CmdExecute} "$\"$INSTDIR\VBoxDrvInst.exe$\" driver nt4-install-video" 'non-zero-exitcode=abort'

  ; Create the VBoxService service
  ; No need to stop/remove the service here! Do this only on uninstallation!
  ${LogVerbose} "Installing VirtualBox service ..."
  ${CmdExecute} "$\"$INSTDIR\VBoxDrvInst.exe$\" service create $\"VBoxService$\" $\"VirtualBox Guest Additions Service$\" 16 2 $\"%SystemRoot%\system32\VBoxService.exe$\" $\"Base$\"" 'non-zero-exitcode=abort'

   ; Create the Shared Folders service ...
  ;nsSCM::Install /NOUNLOAD "VBoxSF" "VirtualBox Shared Folders" 1 1 "$SYSDIR\drivers\VBoxSFNT.sys" "Network" "" "" ""
  ;Pop $0                      ; Ret value

!ifdef _DEBUG
  ;${LogVerbose} "SCM::Install VBoxSFNT.sys: $0"
!endif

  ;IntCmp $0 0 +1 error error  ; Check ret value (0=OK, 1=Error)

  ; ... and the link to the network provider
  ;WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\VBoxSF\NetworkProvider" "DeviceName" "\Device\VBoxMiniRdr"
  ;WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\VBoxSF\NetworkProvider" "Name" "VirtualBox Shared Folders"
  ;WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\VBoxSF\NetworkProvider" "ProviderPath" "$SYSDIR\VBoxMRXNP.dll"

  ; Add the shared folders network provider
  ;${CmdExecute} "$\"$INSTDIR\VBoxDrvInst.exe$\" netprovider add VBoxSF" 'non-zero-exitcode=abort'

FunctionEnd

Function NT4_Main

  SetOutPath "$INSTDIR"

  Call NT4_Prepare
  Call NT4_CopyFiles

  ; This removes the flag "new display driver installed on the next bootup
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\RunOnce" "VBoxGuestInst" '"$INSTDIR\RegCleanup.exe"'

  Call NT4_SaveMouseDriverInfo
  Call NT4_InstallFiles
  Call NT4_SetVideoResolution

  ; Write mouse driver name to registry overwriting the default name
  WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\i8042prt" "ImagePath" "\SystemRoot\System32\DRIVERS\VBoxMouseNT.sys"

FunctionEnd

!macro NT4_UninstallInstDir un
Function ${un}NT4_UninstallInstDir

  ; Delete remaining files
  Delete /REBOOTOK "$INSTDIR\VBoxGuestDrvInst.exe"
  Delete /REBOOTOK "$INSTDIR\RegCleanup.exe"

FunctionEnd
!macroend
;!insertmacro NT4_UninstallInstDir "" - only .un version used
!insertmacro NT4_UninstallInstDir "un."

!macro NT4_Uninstall un
Function ${un}NT4_Uninstall

  Push $0

  ; Remove the guest driver service
  ${CmdExecute} "$\"$INSTDIR\VBoxDrvInst.exe$\" service delete VBoxGuest" 'non-zero-exitcode=log'
  Delete /REBOOTOK "$SYSDIR\drivers\VBoxGuest.sys"

  ; Delete the VBoxService service
  Call ${un}StopVBoxService
  ${CmdExecute} "$\"$INSTDIR\VBoxDrvInst.exe$\" service delete VBoxService" 'non-zero-exitcode=log'
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "VBoxService"
  Delete /REBOOTOK "$SYSDIR\VBoxService.exe"

  ; Delete the VBoxTray app
  Call ${un}StopVBoxTray
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "VBoxTray"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\RunOnce" "VBoxTrayDel" "$SYSDIR\cmd.exe /c del /F /Q $SYSDIR\VBoxTray.exe"
  Delete /REBOOTOK "$SYSDIR\VBoxTray.exe" ; If it can't be removed cause it's running, try next boot with "RunOnce" key above!
  Delete /REBOOTOK "$SYSDIR\VBoxHook.dll"

  ; Delete the VBoxControl utility
  Delete /REBOOTOK "$SYSDIR\VBoxControl.exe"

  ; Delete the VBoxVideo service
  ${CmdExecute} "$\"$INSTDIR\VBoxDrvInst.exe$\" service delete VBoxVideo" 'non-zero-exitcode=log'

  ; Delete the VBox video driver files
  Delete /REBOOTOK "$SYSDIR\drivers\VBoxVideo.sys"
  Delete /REBOOTOK "$SYSDIR\VBoxDisp.dll"

  ; Get original mouse driver info and restore it
  ReadRegStr $0 ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" ${ORG_MOUSE_PATH}
  ; If we still got our driver stored in $0 then this will *never* work, so
  ; warn the user and set it to the default driver to not screw up NT4 here
  ${If} $0 == "System32\DRIVERS\VBoxMouseNT.sys"
    WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\i8042prt" "ImagePath" "\SystemRoot\System32\DRIVERS\i8042prt.sys"
    ${LogVerbose} "Old mouse driver is set to VBoxMouseNT.sys, defaulting to i8042prt.sys ..."
  ${Else}
    WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\i8042prt" "ImagePath" $0
  ${EndIf}
  Delete /REBOOTOK "$SYSDIR\drivers\VBoxMouseNT.sys"

  Pop $0

FunctionEnd
!macroend
!ifndef UNINSTALLER_ONLY
  !insertmacro NT4_Uninstall ""
!endif
!insertmacro NT4_Uninstall "un."
