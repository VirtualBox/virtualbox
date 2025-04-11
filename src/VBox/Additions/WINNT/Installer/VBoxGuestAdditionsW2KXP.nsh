; $Id: VBoxGuestAdditionsW2KXP.nsh 109190 2025-04-11 10:15:40Z andreas.loeffler@oracle.com $
;; @file
; VBoxGuestAdditionsW2KXP.nsh - Guest Additions installation for Windows 2000/XP.
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

Function W2K_SetVideoResolution

  ; NSIS only supports global vars, even in functions -- great
  Var /GLOBAL i
  Var /GLOBAL tmp
  Var /GLOBAL tmppath
  Var /GLOBAL dev_id
  Var /GLOBAL dev_desc

  ; Check for all required parameters
  StrCmp $g_iScreenX "0" exit
  StrCmp $g_iScreenY "0" exit
  StrCmp $g_iScreenBpp "0" exit

  ${LogVerbose} "Setting display parameters ($g_iScreenXx$g_iScreenY, $g_iScreenBpp BPP) ..."

  ; Enumerate all video devices (up to 32 at the moment, use key "MaxObjectNumber" key later)
  ${For} $i 0 32

    ReadRegStr $tmp HKLM "HARDWARE\DEVICEMAP\VIDEO" "\Device\Video$i"
    StrCmp $tmp "" dev_not_found

    ; Extract path to video settings
    ; Ex: \Registry\Machine\System\CurrentControlSet\Control\Video\{28B74D2B-F0A9-48E0-8028-D76F6BB1AE65}\0000
    ; Or: \Registry\Machine\System\CurrentControlSet\Control\Video\vboxvideo\Device0
    ; Result: Machine\System\CurrentControlSet\Control\Video\{28B74D2B-F0A9-48E0-8028-D76F6BB1AE65}\0000
    Push "$tmp" ; String
    Push "\" ; SubString
    Push ">" ; SearchDirection
    Push ">" ; StrInclusionDirection
    Push "0" ; IncludeSubString
    Push "2" ; Loops
    Push "0" ; CaseSensitive
    Call StrStrAdv
    Pop $tmppath ; $1 only contains the full path
    StrCmp $tmppath "" dev_not_found

    ; Get device description
    ReadRegStr $dev_desc HKLM "$tmppath" "Device Description"
!ifdef _DEBUG
    ${LogVerbose} "Registry path: $tmppath"
    ${LogVerbose} "Registry path to device name: $temp"
!endif
    ${LogVerbose} "Detected video device: $dev_desc"

    ${If} $dev_desc == "VirtualBox Graphics Adapter"
      ${LogVerbose} "VirtualBox video device found!"
      Goto dev_found
    ${EndIf}
  ${Next}
  Goto dev_not_found

dev_found:

  ; If we're on Windows 2000, skip the ID detection ...
  ${If} $g_strWinVersion == "2000"
    Goto change_res
  ${EndIf}
  Goto dev_found_detect_id

dev_found_detect_id:

  StrCpy $i 0 ; Start at index 0
  ${LogVerbose} "Detecting device ID ..."

dev_found_detect_id_loop:

  ; Resolve real path to hardware instance "{GUID}"
  EnumRegKey $dev_id HKLM "SYSTEM\CurrentControlSet\Control\Video" $i
  StrCmp $dev_id "" dev_not_found ; No more entries? Jump out
!ifdef _DEBUG
  ${LogVerbose} "Got device ID: $dev_id"
!endif
  ReadRegStr $dev_desc HKLM "SYSTEM\CurrentControlSet\Control\Video\$dev_id\0000" "Device Description" ; Try to read device name
  ${If} $dev_desc == "VirtualBox Graphics Adapter"
    ${LogVerbose} "Device ID of $dev_desc: $dev_id"
    Goto change_res
  ${EndIf}

  IntOp $i $i + 1 ; Increment index
  goto dev_found_detect_id_loop

dev_not_found:

  ${LogVerbose} "No VirtualBox video device (yet) detected! No custom mode set."
  Goto exit

change_res:

!ifdef _DEBUG
  ${LogVerbose} "Device description: $dev_desc"
  ${LogVerbose} "Device ID: $dev_id"
!endif

  Var /GLOBAL reg_path_device
  Var /GLOBAL reg_path_monitor

  ${LogVerbose} "Custom mode set: Platform is Windows $g_strWinVersion"
  ${If} $g_strWinVersion == "2000"
  ${OrIf} $g_strWinVersion == "Vista"
    StrCpy $reg_path_device "SYSTEM\CurrentControlSet\SERVICES\VBoxVideo\Device0"
    StrCpy $reg_path_monitor "SYSTEM\CurrentControlSet\SERVICES\VBoxVideo\Device0\Mon00000001"
  ${ElseIf} $g_strWinVersion == "XP"
  ${OrIf} $g_strWinVersion == "7"
  ${OrIf} $g_strWinVersion == "8"
  ${OrIf} $g_strWinVersion == "8_1"
  ${OrIf} $g_strWinVersion == "10"
    StrCpy $reg_path_device "SYSTEM\CurrentControlSet\Control\Video\$dev_id\0000"
    StrCpy $reg_path_monitor "SYSTEM\CurrentControlSet\Control\VIDEO\$dev_id\0000\Mon00000001"
  ${Else}
    ${LogVerbose} "Custom mode set: Windows $g_strWinVersion not supported yet"
    Goto exit
  ${EndIf}

  ; Write the new value in the adapter config (VBoxVideo.sys) using hex values in binary format
  ${CmdExecute} "$\"$INSTDIR\VBoxGuestInstallHelper.exe$\" registry write HKLM $reg_path_device CustomXRes REG_BIN $g_iScreenX DWORD"  'non-zero-exitcode=abort'
  ${CmdExecute} "$\"$INSTDIR\VBoxGuestInstallHelper.exe$\" registry write HKLM $reg_path_device CustomYRes REG_BIN $g_iScreenY DWORD"  'non-zero-exitcode=abort'
  ${CmdExecute} "$\"$INSTDIR\VBoxGuestInstallHelper.exe$\" registry write HKLM $reg_path_device CustomBPP REG_BIN $g_iScreenBpp DWORD" 'non-zero-exitcode=abort'

  ; ... and tell Windows to use that mode on next start!
  WriteRegDWORD HKCC $reg_path_device "DefaultSettings.XResolution" "$g_iScreenX"
  WriteRegDWORD HKCC $reg_path_device "DefaultSettings.YResolution" "$g_iScreenY"
  WriteRegDWORD HKCC $reg_path_device "DefaultSettings.BitsPerPixel" "$g_iScreenBpp"

  WriteRegDWORD HKCC $reg_path_monitor "DefaultSettings.XResolution" "$g_iScreenX"
  WriteRegDWORD HKCC $reg_path_monitor "DefaultSettings.YResolution" "$g_iScreenY"
  WriteRegDWORD HKCC $reg_path_monitor "DefaultSettings.BitsPerPixel" "$g_iScreenBpp"

  ${LogVerbose} "Custom mode set to $g_iScreenXx$g_iScreenY, $g_iScreenBpp BPP on next restart."

exit:

FunctionEnd

!macro W2K_CleanupCerts un
;;
; Removes certificates and the certificate utility.
;
; Since the certificate collection depends on the build config and have
; changed over time, we always clean it up before installation.
;
Function ${un}W2K_CleanupCerts
  Delete /REBOOTOK "$INSTDIR\cert\vbox.cer"
  Delete /REBOOTOK "$INSTDIR\cert\vbox-root.cer"
  Delete /REBOOTOK "$INSTDIR\cert\vbox-timestamp-root.cer"
  Delete /REBOOTOK "$INSTDIR\cert\vbox-sha1.cer"
  Delete /REBOOTOK "$INSTDIR\cert\vbox-sha1-root.cer"
  Delete /REBOOTOK "$INSTDIR\cert\vbox-sha1-timestamp-root.cer"
  Delete /REBOOTOK "$INSTDIR\cert\vbox-sha256.cer"
  Delete /REBOOTOK "$INSTDIR\cert\vbox-sha256-root.cer"
  Delete /REBOOTOK "$INSTDIR\cert\vbox-sha256-timestamp-root.cer"
  Delete /REBOOTOK "$INSTDIR\cert\vbox-sha256-r3.cer"
  Delete /REBOOTOK "$INSTDIR\cert\vbox-sha256-r3-root.cer"
  Delete /REBOOTOK "$INSTDIR\cert\vbox-sha256-r3-timestamp-root.cer"
  Delete /REBOOTOK "$INSTDIR\cert\vbox-legacy-timestamp-ca.cer"
  Delete /REBOOTOK "$INSTDIR\cert\root-versign-pca3-g5.cer"             ; only for a while during 7.0 beta phase
  Delete /REBOOTOK "$INSTDIR\cert\root-digicert-assured-id.cer"         ; ditto
  Delete /REBOOTOK "$INSTDIR\cert\root-digicert-high-assurance-ev.cer"  ; ditto
  Delete /REBOOTOK "$INSTDIR\cert\VBoxCertUtil.exe"
  RMDir  /REBOOTOK "$INSTDIR\cert"
FunctionEnd
!macroend
!insertmacro W2K_CleanupCerts ""
!insertmacro W2K_CleanupCerts "un."


!macro W2K_CleanupObsoleteFiles un
;;
; Removes files we're no longer shipping.
;
; During installation this step should be taken before copy files over in case
; the list gets out of sync and we start shipping files on it.  That way it
; doesn't much matter as the file will be restore afterwards.
;
Function ${un}W2K_CleanupObsoleteFiles
  Delete /REBOOTOK "$INSTDIR\VBoxWHQLFake.exe"  ; Removed in r152293 (runup to 7.0).
  Delete /REBOOTOK "$INSTDIR\VBoxICD.dll"       ; Removed in r151892 (runup to 7.0).
FunctionEnd
!macroend
!insertmacro W2K_CleanupObsoleteFiles ""
!insertmacro W2K_CleanupObsoleteFiles "un."


!ifdef VBOX_SIGN_ADDITIONS
;;
; Run VBoxCertUtil to install the given certificate if absent on the system.
;
; @param    pop1    The certificate file.
; @param    pop2    Short description.
;
Function   W2K_InstallRootCert
  ; Prolog: Save $0 & $1 and move the parameters into them.
  Push    $0
  Exch    2
  Push    $1
  Exch    2
  Pop     $0                                ; Filename
  Pop     $1                                ; Description.

  ; Do the work.
  ${LogVerbose} "Installing $1 ('$0') if missing ..."
  ${CmdExecute} "$\"$INSTDIR\cert\VBoxCertUtil.exe$\" add-root --add-if-new $\"$INSTDIR\cert\$0$\"" 'non-zero-exitcode=abort'

  ; Epilog: Restore $0 & $1 (we return nothing).
  Pop     $2
  Pop     $1
  Pop     $0
FunctionEnd
!endif

Function W2K_Prepare
  ; Save registers
  Push  $R0
  Push  $R1
  Push  $R2
  Push  $R3
  Push  $R4

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

  ; Ditch old certificates and stuff to avoid confusion if we now ship fewer / different files.
  Call W2K_CleanupCerts

!ifdef VBOX_SIGN_ADDITIONS
  ;
  ; When installing signed GAs, we need to make sure that the associated root
  ; certs are present, we use VBoxCertUtil for this task.
  ;
  ${LogVerbose} "Installing VBoxCertUtil.exe ..."
  SetOutPath "$INSTDIR\cert"
  FILE "$%PATH_OUT%\bin\additions\VBoxCertUtil.exe"
  !if  "$%VBOX_GA_CERT_ROOT_SHA1%" != "none"
  FILE "$%PATH_OUT%\bin\additions\$%VBOX_GA_CERT_ROOT_SHA1%"
  !endif
  !if  "$%VBOX_GA_CERT_ROOT_SHA1_TS%" != "none"
  FILE "$%PATH_OUT%\bin\additions\$%VBOX_GA_CERT_ROOT_SHA1_TS%"
  !endif
  !if  "$%VBOX_GA_CERT_ROOT_SHA2%" != "none"
  FILE "$%PATH_OUT%\bin\additions\$%VBOX_GA_CERT_ROOT_SHA2%"
  !endif
  !if  "$%VBOX_GA_CERT_ROOT_SHA2_TS%" != "none"
  FILE "$%PATH_OUT%\bin\additions\$%VBOX_GA_CERT_ROOT_SHA2_TS%"
  !endif
  !if  "$%VBOX_GA_CERT_ROOT_SHA2_R3%" != "none"
  FILE "$%PATH_OUT%\bin\additions\$%VBOX_GA_CERT_ROOT_SHA2_R3%"
  !endif
  !if  "$%VBOX_GA_CERT_ROOT_SHA2_R3_TS%" != "none"
  FILE "$%PATH_OUT%\bin\additions\$%VBOX_GA_CERT_ROOT_SHA2_R3_TS%"
  !endif

  ;
  ; Install the certificates if missing.
  ;
  !if  "$%VBOX_GA_CERT_ROOT_SHA1%" != "none"
  Push "SHA-1 root"
  Push "$%VBOX_GA_CERT_ROOT_SHA1%"
  Call W2K_InstallRootCert
  !endif
  !if  "$%VBOX_GA_CERT_ROOT_SHA1_TS%" != "none"
    !ifdef VBOX_WITH_VBOX_LEGACY_TS_CA
  ; If not explicitly specified, let the detected Windows version decide what
  ; to do. On guest OSes < Windows 10 we always go for the PreW10 security
  ; catalog files (.cat) and there we install our legacy timestamp CA by default.
  ${If}    $g_bInstallTimestampCA == "unset"
  ${AndIf} $g_strWinVersion != "10"
      StrCpy $g_bInstallTimestampCA "true"
  ${EndIf}
  ${If} $g_bInstallTimestampCA == "true"
    Push "SHA-1 timestamp root"
    Push "$%VBOX_GA_CERT_ROOT_SHA1_TS%"
    Call W2K_InstallRootCert
  ${EndIf}
    !else
  Push "SHA-1 timestamp root"
  Push "$%VBOX_GA_CERT_ROOT_SHA1_TS%"
  Call W2K_InstallRootCert
    !endif ; VBOX_WITH_VBOX_LEGACY_TS_CA
  !endif

  ; XP sp3 and later can make use of SHA-2 certs. Windows 2000 cannot.
  ; Note that VBOX_GA_CERT_ROOT_SHA1 may be a SHA-2 cert, the hash algorithm
  ; refers to the windows signature structures not the certificate.
  ${If} $g_strWinVersion != "2000"
  !if  "$%VBOX_GA_CERT_ROOT_SHA2%" != "none"
    Push "SHA-2 root"
    Push "$%VBOX_GA_CERT_ROOT_SHA2%"
    Call W2K_InstallRootCert
  !endif
  !if  "$%VBOX_GA_CERT_ROOT_SHA2_TS%" != "none"
    Push "SHA-2 timestamp root"
    Push "$%VBOX_GA_CERT_ROOT_SHA2_TS%"
    Call W2K_InstallRootCert
  !endif
  !if  "$%VBOX_GA_CERT_ROOT_SHA2_R3%" != "none"
    Push "SHA-2 ring-3 root"
    Push "$%VBOX_GA_CERT_ROOT_SHA2_R3%"
    Call W2K_InstallRootCert
  !endif
  !if  "$%VBOX_GA_CERT_ROOT_SHA2_R3_TS%" != "none"
    Push "SHA-2 ring-3 timestamp root"
    Push "$%VBOX_GA_CERT_ROOT_SHA2_R3_TS%"
    Call W2K_InstallRootCert
  !endif
  ${EndIf}
!endif ; VBOX_SIGN_ADDITIONS

  ; Restore registers
  Pop $R4
  Pop $R3
  Pop $R2
  Pop $R1
  Pop $R0
FunctionEnd

Function W2K_CopyFiles

  Push $0
  SetOutPath "$INSTDIR"

  ; Video driver
  FILE "$%PATH_OUT%\bin\additions\VBoxVideo.sys"
  FILE "$%PATH_OUT%\bin\additions\VBoxDisp.dll"

  ; Mouse driver
  FILE "$%PATH_OUT%\bin\additions\VBoxMouse.sys"
  FILE "$%PATH_OUT%\bin\additions\VBoxMouse.inf"
!ifdef VBOX_SIGN_ADDITIONS
  ${If} $g_strWinVersion == "10"
    FILE "$%PATH_OUT%\bin\additions\VBoxMouse.cat"
  ${Else}
    FILE "/oname=VBoxMouse.cat" "$%PATH_OUT%\bin\additions\VBoxMouse-PreW10.cat"
  ${EndIf}
!endif

  ; Guest driver
  FILE "$%PATH_OUT%\bin\additions\VBoxGuest.sys"
  FILE "$%PATH_OUT%\bin\additions\VBoxGuest.inf"
!if $%KBUILD_TARGET_ARCH% == "x86"
  ${If} $g_strEarlyNTDrvInfix != ""
    FILE "$%PATH_OUT%\bin\additions\VBoxGuestEarlyNT.inf"
  !ifdef VBOX_SIGN_ADDITIONS
    FILE "$%PATH_OUT%\bin\additions\VBoxGuestEarlyNT.cat"
  !endif
  ${EndIf}
!endif
!ifdef VBOX_SIGN_ADDITIONS
  ${If} $g_strWinVersion == "10"
    FILE "$%PATH_OUT%\bin\additions\VBoxGuest.cat"
  ${Else}
    FILE "/oname=VBoxGuest.cat" "$%PATH_OUT%\bin\additions\VBoxGuest-PreW10.cat"
  ${EndIf}
!endif

  ; Guest driver files
  FILE "$%PATH_OUT%\bin\additions\VBoxTray.exe"
  FILE "$%PATH_OUT%\bin\additions\VBoxControl.exe"

  ; Tools
  SetOutPath "$INSTDIR\Tools"
!ifdef VBOX_WITH_ADDITIONS_SHIPPING_AUDIO_TEST
  FILE "$%PATH_OUT%\bin\additions\VBoxAudioTest.exe"
!endif

  SetOutPath $g_strSystemDir

  ; VBoxService
  ${If} $g_bNoVBoxServiceExit == "false"
    ; VBoxService has been terminated before, so just install the file
    ; in the regular way
    FILE "$%PATH_OUT%\bin\additions\VBoxService.exe"
  ${Else}
    ; VBoxService is in use and wasn't terminated intentionally. So extract the
    ; new version into a temporary location and install it on next reboot
    Push $0
    ClearErrors
    GetTempFileName $0
    IfErrors 0 +3
      ${LogVerbose} "Error getting temp file for VBoxService.exe"
      StrCpy "$0" "$INSTDIR\VBoxServiceTemp.exe"
    ${LogVerbose} "VBoxService is in use, will be installed on next reboot (from '$0')"
    File "/oname=$0" "$%PATH_OUT%\bin\additions\VBoxService.exe"
    IfErrors 0 +2
      ${LogVerbose} "Error copying VBoxService.exe to '$0'"
    Rename /REBOOTOK "$0" "$g_strSystemDir\VBoxService.exe"
    IfErrors 0 +2
      ${LogVerbose} "Error renaming '$0' to '$g_strSystemDir\VBoxService.exe'"
    Pop $0
  ${EndIf}

!if $%VBOX_WITH_WDDM% == "1"
  ${If} $g_bWithWDDM == "true"
    ; WDDM Video driver
    SetOutPath "$INSTDIR"

    !ifdef VBOX_SIGN_ADDITIONS
      ${If} $g_strWinVersion == "10"
        FILE "$%PATH_OUT%\bin\additions\VBoxWddm.cat"
      ${Else}
        FILE "/oname=VBoxWddm.cat" "$%PATH_OUT%\bin\additions\VBoxWddm-PreW10.cat"
      ${EndIf}
    !endif
    FILE "$%PATH_OUT%\bin\additions\VBoxWddm.sys"
    FILE "$%PATH_OUT%\bin\additions\VBoxWddm.inf"

    FILE "$%PATH_OUT%\bin\additions\VBoxDispD3D.dll"
    !if $%VBOX_WITH_WDDM_DX% == "1"
      FILE "$%PATH_OUT%\bin\additions\VBoxDX.dll"
    !endif
    !if $%VBOX_WITH_MESA3D% == "1"
      FILE "$%PATH_OUT%\bin\additions\VBoxNine.dll"
      FILE "$%PATH_OUT%\bin\additions\VBoxSVGA.dll"
      FILE "$%PATH_OUT%\bin\additions\VBoxGL.dll"
    !endif

    !if $%KBUILD_TARGET_ARCH% == "amd64"
      FILE "$%PATH_OUT%\bin\additions\VBoxDispD3D-x86.dll"
      !if $%VBOX_WITH_WDDM_DX% == "1"
        FILE "$%PATH_OUT%\bin\additions\VBoxDX-x86.dll"
      !endif
      !if $%VBOX_WITH_MESA3D% == "1"
        FILE "$%PATH_OUT%\bin\additions\VBoxNine-x86.dll"
        FILE "$%PATH_OUT%\bin\additions\VBoxSVGA-x86.dll"
        FILE "$%PATH_OUT%\bin\additions\VBoxGL-x86.dll"
      !endif
    !endif ; $%KBUILD_TARGET_ARCH% == "amd64"

  ${EndIf}
!endif ; $%VBOX_WITH_WDDM% == "1"

  Pop $0

FunctionEnd


Function W2K_InstallFiles

  ; The Shared Folder IFS goes to the system directory
  !if $%KBUILD_TARGET_ARCH% == "x86"
    ; On x86 we have to use a different shared folder driver linked against an older RDBSS for Windows 7 and older.
    ${If} $g_strWinVersion == "2000"
    ${OrIf} $g_strWinVersion == "XP"
    ${OrIf} $g_strWinVersion == "2003"
    ${OrIf} $g_strWinVersion == "Vista"
    ${OrIf} $g_strWinVersion == "7"
      !insertmacro ReplaceDLL "$%PATH_OUT%\bin\additions\VBoxSFW2K.sys" "$g_strSystemDir\drivers\VBoxSF.sys" "$INSTDIR"
    ${Else}
      !insertmacro ReplaceDLL "$%PATH_OUT%\bin\additions\VBoxSF.sys" "$g_strSystemDir\drivers\VBoxSF.sys" "$INSTDIR"
    ${EndIf}
  !else
    !insertmacro ReplaceDLL "$%PATH_OUT%\bin\additions\VBoxSF.sys" "$g_strSystemDir\drivers\VBoxSF.sys" "$INSTDIR"
  !endif

  !insertmacro ReplaceDLL "$%PATH_OUT%\bin\additions\VBoxMRXNP.dll" "$g_strSystemDir\VBoxMRXNP.dll" "$INSTDIR"
  AccessControl::GrantOnFile "$g_strSystemDir\VBoxMRXNP.dll" "(BU)" "GenericRead"
  !if $%KBUILD_TARGET_ARCH% == "amd64"
    ; Only 64-bit installer: Copy the 32-bit DLL for 32 bit applications.
    !insertmacro ReplaceDLL "$%PATH_OUT%\bin\additions\VBoxMRXNP-x86.dll" "$g_strSysWow64\VBoxMRXNP.dll" "$INSTDIR"
    AccessControl::GrantOnFile "$g_strSysWow64\VBoxMRXNP.dll" "(BU)" "GenericRead"
  !endif

  ; The VBoxTray hook DLL also goes to the system directory; it might be locked
  !insertmacro ReplaceDLL "$%PATH_OUT%\bin\additions\VBoxHook.dll" "$g_strSystemDir\VBoxHook.dll" "$INSTDIR"
  AccessControl::GrantOnFile "$g_strSystemDir\VBoxHook.dll" "(BU)" "GenericRead"

  ${LogVerbose} "Installing drivers ..."

  Push $0 ; For fetching results

  SetOutPath "$INSTDIR"

  ${If} $g_bNoGuestDrv == "false"
    ${LogVerbose} "Installing guest driver ..."
    ${CmdExecute} "$\"$INSTDIR\VBoxDrvInst.exe$\" --logfile $\"$INSTDIR\install_drivers.log$\" install --inf-file $\"$INSTDIR\VBoxGuest$g_strEarlyNTDrvInfix.inf$\" --ignore-reboot" 'non-zero-exitcode=abort'
  ${Else}
    ${LogVerbose} "Guest driver installation skipped!"
  ${EndIf}

  ${If} $g_bNoVideoDrv == "false"
    ${If} $g_bWithWDDM == "true"
      ${LogVerbose} "Installing WDDM video driver..."
      ${CmdExecute} "$\"$INSTDIR\VBoxDrvInst.exe$\" --logfile $\"$INSTDIR\install_drivers.log$\" install --inf-file $\"$INSTDIR\VBoxWddm.inf$\" --ignore-reboot" 'non-zero-exitcode=abort'
    ${Else}
      ${LogVerbose} "Installing video driver ..."
      ${CmdExecute} "$\"$INSTDIR\VBoxDrvInst.exe$\" --logfile $\"$INSTDIR\install_drivers.log$\" install --inf-file $\"$INSTDIR\VBoxVideo$g_strEarlyNTDrvInfix.inf$\" --ignore-reboot" 'non-zero-exitcode=abort'
    ${EndIf}
  ${Else}
    ${LogVerbose} "Video driver installation skipped!"
  ${EndIf}

  ;
  ; Mouse driver.
  ;
  ${If} $g_bNoMouseDrv == "false"
    ${LogVerbose} "Installing mouse driver ..."
    ${CmdExecute} "$\"$INSTDIR\VBoxDrvInst.exe$\" --logfile $\"$INSTDIR\install_drivers.log$\" install --inf-file $\"$INSTDIR\VBoxMouse.inf$\" --ignore-reboot" 'non-zero-exitcode=abort'
  ${Else}
    ${LogVerbose} "Mouse driver installation skipped!"
  ${EndIf}

  ;
  ; Create the VBoxService service
  ; No need to stop/remove the service here! Do this only on uninstallation!
  ;
  ${LogVerbose} "Installing VirtualBox service ..."
  ${CmdExecute} "$\"$INSTDIR\VBoxGuestInstallHelper.exe$\" service create $\"VBoxService$\" $\"VirtualBox Guest Additions Service$\" 16 2 $\"%SystemRoot%\System32\VBoxService.exe$\" $\"Base$\"" 'non-zero-exitcode=abort'

  ; Set service description
  WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\VBoxService" "Description" "Manages VM runtime information, time synchronization, remote sysprep execution and miscellaneous utilities for guest operating systems."


  ;
  ; Shared folders.
  ;
  ${LogVerbose} "Installing Shared Folders service ..."

  ; Create the Shared Folders service ...
  ; No need to stop/remove the service here! Do this only on uninstallation!
  ${CmdExecute} "$\"$INSTDIR\VBoxGuestInstallHelper.exe$\" service create $\"VBoxSF$\" $\"VirtualBox Shared Folders$\" 2 1 $\"\SystemRoot\System32\drivers\VBoxSF.sys$\" $\"NetworkProvider$\"" 'non-zero-exitcode=abort'

  ; ... and the link to the network provider
  WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\VBoxSF\NetworkProvider" "DeviceName" "\Device\VBoxMiniRdr"
  WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\VBoxSF\NetworkProvider" "Name" "VirtualBox Shared Folders"
  WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\VBoxSF\NetworkProvider" "ProviderPath" "$SYSDIR\VBoxMRXNP.dll"

  ; Add default network providers (if not present or corrupted)
  ${CmdExecute} "$\"$INSTDIR\VBoxGuestInstallHelper.exe$\" netprovider add WebClient"         'non-zero-exitcode=abort'
  ${CmdExecute} "$\"$INSTDIR\VBoxGuestInstallHelper.exe$\" netprovider add LanmanWorkstation" 'non-zero-exitcode=abort'
  ${CmdExecute} "$\"$INSTDIR\VBoxGuestInstallHelper.exe$\" netprovider add RDPNP"             'non-zero-exitcode=abort'

  ; Add the shared folders network provider
  ${LogVerbose} "Adding network provider (Order = $g_iSfOrder) ..."
  ${CmdExecute} "$\"$INSTDIR\VBoxGuestInstallHelper.exe$\" netprovider add VBoxSF $g_iSfOrder" 'non-zero-exitcode=abort'


  Pop $0

FunctionEnd

Function W2K_Main

  SetOutPath "$INSTDIR"
  SetOverwrite on

  Call W2K_Prepare
  Call W2K_CleanupObsoleteFiles
  Call W2K_CopyFiles
  Call W2K_InstallFiles
  Call W2K_SetVideoResolution

FunctionEnd

!macro W2K_UninstallInstDir un
Function ${un}W2K_UninstallInstDir

  Delete /REBOOTOK "$INSTDIR\VBoxVideo.sys"
  Delete /REBOOTOK "$INSTDIR\VBoxVideo.inf"
  Delete /REBOOTOK "$INSTDIR\VBoxVideo.cat"
  Delete /REBOOTOK "$INSTDIR\VBoxVideoEarlyNT.inf"
  Delete /REBOOTOK "$INSTDIR\VBoxVideoEarlyNT.cat"
  Delete /REBOOTOK "$INSTDIR\VBoxDisp.dll"

  Delete /REBOOTOK "$INSTDIR\VBoxMouse.sys"
  Delete /REBOOTOK "$INSTDIR\VBoxMouse.inf"
  Delete /REBOOTOK "$INSTDIR\VBoxMouse.cat"

  Delete /REBOOTOK "$INSTDIR\VBoxTray.exe"

  Delete /REBOOTOK "$INSTDIR\VBoxGuest.sys"
  Delete /REBOOTOK "$INSTDIR\VBoxGuest.inf"
  Delete /REBOOTOK "$INSTDIR\VBoxGuest.cat"
  Delete /REBOOTOK "$INSTDIR\VBoxGuestEarlyNT.inf"
  Delete /REBOOTOK "$INSTDIR\VBoxGuestEarlyNT.cat"

  Delete /REBOOTOK "$INSTDIR\VBCoInst.dll" ; Deprecated, does not get installed anymore
  Delete /REBOOTOK "$INSTDIR\VBoxControl.exe"
  Delete /REBOOTOK "$INSTDIR\VBoxService.exe" ; Deprecated, does not get installed anymore

!if $%VBOX_WITH_WDDM% == "1"
  Delete /REBOOTOK "$INSTDIR\VBoxWddm.cat"
  Delete /REBOOTOK "$INSTDIR\VBoxWddm.sys"
  Delete /REBOOTOK "$INSTDIR\VBoxWddm.inf"
  ; Obsolete files begin
  Delete /REBOOTOK "$INSTDIR\VBoxVideoWddm.cat"
  Delete /REBOOTOK "$INSTDIR\VBoxVideoWddm.sys"
  Delete /REBOOTOK "$INSTDIR\VBoxVideoWddm.inf"
  Delete /REBOOTOK "$INSTDIR\VBoxVideoW8.cat"
  Delete /REBOOTOK "$INSTDIR\VBoxVideoW8.sys"
  Delete /REBOOTOK "$INSTDIR\VBoxVideoW8.inf"
  ; Obsolete files end
  Delete /REBOOTOK "$INSTDIR\VBoxDispD3D.dll"
  !if $%VBOX_WITH_WDDM_DX% == "1"
    Delete /REBOOTOK "$INSTDIR\VBoxDX.dll"
  !endif
  !if $%VBOX_WITH_MESA3D% == "1"
    Delete /REBOOTOK "$INSTDIR\VBoxNine.dll"
    Delete /REBOOTOK "$INSTDIR\VBoxSVGA.dll"
    Delete /REBOOTOK "$INSTDIR\VBoxICD.dll"
    Delete /REBOOTOK "$INSTDIR\VBoxGL.dll"
  !endif

    Delete /REBOOTOK "$INSTDIR\VBoxD3D9wddm.dll"
    Delete /REBOOTOK "$INSTDIR\wined3dwddm.dll"
    ; Try to delete libWine in case it is there from old installation
    Delete /REBOOTOK "$INSTDIR\libWine.dll"

  !if $%KBUILD_TARGET_ARCH% == "amd64"
    Delete /REBOOTOK "$INSTDIR\VBoxDispD3D-x86.dll"
    !if $%VBOX_WITH_WDDM_DX% == "1"
      Delete /REBOOTOK "$INSTDIR\VBoxDX-x86.dll"
    !endif
    !if $%VBOX_WITH_MESA3D% == "1"
      Delete /REBOOTOK "$INSTDIR\VBoxNine-x86.dll"
      Delete /REBOOTOK "$INSTDIR\VBoxSVGA-x86.dll"
      Delete /REBOOTOK "$INSTDIR\VBoxICD-x86.dll"
      Delete /REBOOTOK "$INSTDIR\VBoxGL-x86.dll"
    !endif

      Delete /REBOOTOK "$INSTDIR\VBoxD3D9wddm-x86.dll"
      Delete /REBOOTOK "$INSTDIR\wined3dwddm-x86.dll"
  !endif ; $%KBUILD_TARGET_ARCH% == "amd64"
!endif ; $%VBOX_WITH_WDDM% == "1"

  ; Certificates, utility and directory.
  Call ${un}W2K_CleanupCerts

  ; Log files
  Delete /REBOOTOK "$INSTDIR\install.log"
  Delete /REBOOTOK "$INSTDIR\install_ui.log"
  Delete /REBOOTOK "$INSTDIR\install_drivers.log"

  ; Obsolete stuff.
  Call ${un}W2K_CleanupObsoleteFiles

FunctionEnd
!macroend
;!insertmacro W2K_UninstallInstDir "" - only .un version used
!insertmacro W2K_UninstallInstDir "un."

!macro W2K_Uninstall un
Function ${un}W2K_Uninstall

  Push $0

  ; Remove VirtualBox video driver
  ${LogVerbose} "Uninstalling video driver ..."
  ${CmdExecute} "$\"$INSTDIR\VBoxDrvInst.exe$\" --logfile $\"$TEMP\${PRODUCT_NAME}\uninstall_drivers.log$\" uninstall --model $\"VBoxVideo*$\" --ignore-reboot" 'non-zero-exitcode=log'
  ${CmdExecute} "$\"$INSTDIR\VBoxGuestInstallHelper.exe$\" service delete VBoxVideo" 'non-zero-exitcode=log'
  Delete /REBOOTOK "$g_strSystemDir\drivers\VBoxVideo.sys"
  Delete /REBOOTOK "$g_strSystemDir\VBoxDisp.dll"

  ; Remove video driver
!if $%VBOX_WITH_WDDM% == "1"

  ${LogVerbose} "Uninstalling WDDM video driver..."
  ${CmdExecute} "$\"$INSTDIR\VBoxDrvInst.exe$\" --logfile $\"$TEMP\${PRODUCT_NAME}\uninstall_drivers.log$\" uninstall --model $\"VBoxWddm*$\" --ignore-reboot" 'non-zero-exitcode=log'
  ${CmdExecute} "$\"$INSTDIR\VBoxGuestInstallHelper.exe$\" service delete VBoxWddm" 'non-zero-exitcode=log'
  ;misha> @todo driver file removal (as well as service removal) should be done as driver package uninstall
  ;       could be done with "VBoxGuestInstallHelper.exe /u", e.g. by passing additional arg to it denoting that driver package is to be uninstalled
  Delete /REBOOTOK "$g_strSystemDir\drivers\VBoxWddm.sys"

  ; Obsolete files begin
  ${LogVerbose} "Uninstalling WDDM video driver for Windows 8..."
  ${CmdExecute} "$\"$INSTDIR\VBoxDrvInst.exe$\" --logfile $\"$TEMP\${PRODUCT_NAME}\uninstall_drivers.log$\" uninstall --model $\"VBoxVideoW8*$\" --ignore-reboot" 'non-zero-exitcode=log'
  ${CmdExecute} "$\"$INSTDIR\VBoxGuestInstallHelper.exe$\" service delete VBoxVideoW8" 'non-zero-exitcode=log'
  ;misha> @todo driver file removal (as well as service removal) should be done as driver package uninstall
  ;       could be done with "VBoxGuestInstallHelper.exe /u", e.g. by passing additional arg to it denoting that driver package is to be uninstalled
  Delete /REBOOTOK "$g_strSystemDir\drivers\VBoxVideoW8.sys"

  ${LogVerbose} "Uninstalling WDDM video driver for Windows Vista and 7..."
  ${CmdExecute} "$\"$INSTDIR\VBoxDrvInst.exe$\" --logfile $\"$TEMP\${PRODUCT_NAME}\uninstall_drivers.log$\" uninstall --model $\"VBoxVideoWddm*$\" --ignore-reboot" 'non-zero-exitcode=log'
  ; Always try to remove both VBoxVideoWddm & VBoxVideo services no matter what is installed currently
  ${CmdExecute} "$\"$INSTDIR\VBoxGuestInstallHelper.exe$\" service delete VBoxVideoWddm" 'non-zero-exitcode=log'
  ;misha> @todo driver file removal (as well as service removal) should be done as driver package uninstall
  ;       could be done with "VBoxGuestInstallHelper.exe /u", e.g. by passing additional arg to it denoting that driver package is to be uninstalled
  Delete /REBOOTOK "$g_strSystemDir\drivers\VBoxVideoWddm.sys"
  ; Obsolete files end

  Delete /REBOOTOK "$g_strSystemDir\VBoxDispD3D.dll"
  !if $%KBUILD_TARGET_ARCH% == "amd64"
    Delete /REBOOTOK "$g_strSysWow64\VBoxDispD3D-x86.dll"
  !endif

  !if $%VBOX_WITH_WDDM_DX% == "1"
    Delete /REBOOTOK "$g_strSystemDir\VBoxDX.dll"
    !if $%KBUILD_TARGET_ARCH% == "amd64"
      Delete /REBOOTOK "$g_strSysWow64\VBoxDX-x86.dll"
    !endif
  !endif

  !if $%VBOX_WITH_MESA3D% == "1"
    Delete /REBOOTOK "$g_strSystemDir\VBoxNine.dll"
    Delete /REBOOTOK "$g_strSystemDir\VBoxSVGA.dll"
    Delete /REBOOTOK "$g_strSystemDir\VBoxICD.dll"
    Delete /REBOOTOK "$g_strSystemDir\VBoxGL.dll"

    !if $%KBUILD_TARGET_ARCH% == "amd64"
      Delete /REBOOTOK "$g_strSysWow64\VBoxNine-x86.dll"
      Delete /REBOOTOK "$g_strSysWow64\VBoxSVGA-x86.dll"
      Delete /REBOOTOK "$g_strSysWow64\VBoxICD-x86.dll"
      Delete /REBOOTOK "$g_strSysWow64\VBoxGL-x86.dll"
    !endif
  !endif
!endif ; $%VBOX_WITH_WDDM% == "1"

  ; Remove mouse driver
  ${LogVerbose} "Removing mouse driver ..."
  ${CmdExecute} "$\"$INSTDIR\VBoxDrvInst.exe$\" --logfile $\"$TEMP\${PRODUCT_NAME}\uninstall_drivers.log$\" uninstall --model $\"VBoxMouse*$\" --ignore-reboot" 'non-zero-exitcode=log'
  ; @todo Fix VBoxMouse.inf to also take care of the next line!
  ${CmdExecute} "$\"$INSTDIR\VBoxGuestInstallHelper.exe$\" registry delmultisz $\"SYSTEM\CurrentControlSet\Control\Class\{4D36E96F-E325-11CE-BFC1-08002BE10318}$\" $\"UpperFilters$\" $\"VBoxMouse$\"" 'non-zero-exitcode=log'

  ; Delete the VBoxService service
  Call ${un}StopVBoxService
  ${CmdExecute} "$\"$INSTDIR\VBoxGuestInstallHelper.exe$\" service delete VBoxService" 'non-zero-exitcode=log'
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "VBoxService"
  Delete /REBOOTOK "$g_strSystemDir\VBoxService.exe"

  ; VBoxGINA
  Delete /REBOOTOK "$g_strSystemDir\VBoxGINA.dll"
  ReadRegStr $0 HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion\WinLogon" "GinaDLL"
  ${If} $0 == "VBoxGINA.dll"
    ${LogVerbose} "Removing auto-logon support ..."
    DeleteRegValue HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion\WinLogon" "GinaDLL"
  ${EndIf}
  DeleteRegKey HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion\WinLogon\Notify\VBoxGINA"

  ; Delete VBoxTray
  Call ${un}StopVBoxTray
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "VBoxTray"

  ; Remove guest driver. Needs to be done last.
  ${LogVerbose} "Removing guest driver ..."
  ${CmdExecute} "$\"$INSTDIR\VBoxDrvInst.exe$\" --logfile $\"$TEMP\${PRODUCT_NAME}\uninstall_drivers.log$\" uninstall --model $\"VBoxGuest*$\" --ignore-reboot" 'non-zero-exitcode=log'

  ${CmdExecute} "$\"$INSTDIR\VBoxGuestInstallHelper.exe$\" service delete VBoxGuest" 'non-zero-exitcode=log'
  Delete /REBOOTOK "$g_strSystemDir\drivers\VBoxGuest.sys"
  Delete /REBOOTOK "$g_strSystemDir\VBCoInst.dll" ; Deprecated, does not get installed anymore
  Delete /REBOOTOK "$g_strSystemDir\VBoxTray.exe"
  Delete /REBOOTOK "$g_strSystemDir\VBoxHook.dll"
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "VBoxTray" ; Remove VBoxTray autorun
  Delete /REBOOTOK "$g_strSystemDir\VBoxControl.exe"

  ; Remove shared folders driver
  ${LogVerbose} "Removing shared folders driver ..."
  ${CmdExecute} "$\"$INSTDIR\VBoxGuestInstallHelper.exe$\" netprovider remove VBoxSF" 'non-zero-exitcode=log'
  ${CmdExecute} "$\"$INSTDIR\VBoxGuestInstallHelper.exe$\" service delete VBoxSF" 'non-zero-exitcode=log'
  Delete /REBOOTOK "$g_strSystemDir\VBoxMRXNP.dll" ; The network provider DLL will be locked
  !if $%KBUILD_TARGET_ARCH% == "amd64"
    ; Only 64-bit installer: Also remove 32-bit DLLs on 64-bit target arch in Wow64 node
    Delete /REBOOTOK "$g_strSysWow64\VBoxMRXNP.dll"
  !endif ; amd64
  Delete /REBOOTOK "$g_strSystemDir\drivers\VBoxSF.sys"

  Pop $0

FunctionEnd
!macroend
!ifndef UNINSTALLER_ONLY
  !insertmacro W2K_Uninstall ""
!endif
!insertmacro W2K_Uninstall "un."
