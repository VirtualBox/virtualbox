; $Id: VBoxGuestAdditionsUninstallOld.nsh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; VBoxGuestAdditionsUninstallOld.nsh - Guest Additions uninstallation handling for legacy packages.
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

!ifdef UNUSED_CODE
!macro Uninstall_RunExtUnInstaller un
Function ${un}Uninstall_RunExtUnInstaller

  Pop  $0
  Push $1
  Push $2

  ; Try to run the current uninstaller
  StrCpy $1 "$0\uninst.exe"
  IfFileExists "$1" run 0
    MessageBox MB_OK "VirtualBox Guest Additions uninstaller not found! Path = $1" /SD IDOK
    StrCpy $0 1 ; Tell the caller that we were not able to start the uninstaller
    Goto exit

run:

  ; Always try to run in silent mode
  Goto run_uninst_silent

run_uninst_silent:

  ExecWait '"$1" /S _?=$0' $2 ; Silently run uninst.exe in it's dir and don't copy it to a temp. location
  Goto handle_result

run_uninst:

  ExecWait '"$1" _?=$0' $2 ; Run uninst.exe in it's dir and don't copy it to a temp. location
  Goto handle_result

handle_result:

  ; Note that here a race might going on after the user clicked on
  ; "Reboot now" in the installer ran above and this installer cleaning
  ; up afterwards

  ; ... so try to abort the current reboot / shutdown caused by the installer ran before
  Call ${un}AbortShutdown

;!ifdef _DEBUG
;      MessageBox MB_OK 'Debug Message: Uninstaller was called, result is: $2' /SD IDOK
;!endif

  ${Switch} $2 ; Check exit codes
    ${Case} 1  ; Aborted by user
      StrCpy $0 1 ; Tell the caller that we were aborted by the user
      ${Break}
    ${Case} 2  ; Aborted by script (that might be okay)
      StrCpy $0 0 ; All went well
      ${Break}
    ${Default} ; Normal exixt
      StrCpy $0 0 ; All went well
      ${Break}
  ${EndSwitch}
  Goto exit

exit:

  Pop $2
  Pop $1
  Push $0

FunctionEnd
!macroend
!insertmacro Uninstall_RunExtUnInstaller ""
!insertmacro Uninstall_RunExtUnInstaller "un."
!endif ; UNUSED_CODE

!macro Uninstall_WipeInstallationDirectory un
Function ${un}Uninstall_WipeInstallationDirectory

  Pop  $0
  Push $1
  Push $2

  ; Do some basic sanity checks for not screwing up too fatal ...
  ${LogVerbose} "Removing old installation directory ($0) ..."
  ${If} $0    != $PROGRAMFILES
  ${AndIf} $0 != $PROGRAMFILES32
  ${AndIf} $0 != $PROGRAMFILES64
  ${AndIf} $0 != $COMMONFILES32
  ${AndIf} $0 != $COMMONFILES64
  ${AndIf} $0 != $WINDIR
  ${AndIf} $0 != $SYSDIR
    ${LogVerbose} "Wiping ($0) ..."
    Goto wipe
  ${EndIf}
  Goto wipe_abort

wipe:

  RMDir /r /REBOOTOK "$0"
  StrCpy $0 0 ; All went well
  Goto exit

wipe_abort:

  ${LogVerbose} "Won't remove directory ($0)!"
  StrCpy $0 1 ; Signal some failure
  Goto exit

exit:

  Pop $2
  Pop $1
  Push $0

FunctionEnd
!macroend
!insertmacro Uninstall_WipeInstallationDirectory ""

; This function cleans up an old Sun installation
!macro Uninstall_Sun un
Function ${un}Uninstall_Sun

  Push $0
  Push $1
  Push $2

  ; Get current installation path
  ReadRegStr $0 HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Sun VirtualBox Guest Additions" "UninstallString"
  StrCmp $0 "" exit

  ; Extract path
  Push "$0"       ; String
  Push "\"        ; SubString
  Push "<"        ; SearchDirection
  Push "<"        ; StrInclusionDirection
  Push "0"        ; IncludeSubString
  Push "0"        ; Loops
  Push "0"        ; CaseSensitive
  Call ${un}StrStrAdv
  Pop $1          ; $1 only contains the full path

  StrCmp $1 "" exit

  ; Save current i8042prt info to new uninstall registry path
  ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Sun VirtualBox Guest Additions" ${ORG_MOUSE_PATH}
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" ${ORG_MOUSE_PATH} $0

  ; Try to wipe current installation directory
  Push $1 ; Push uninstaller path to stack
  Call ${un}Uninstall_WipeInstallationDirectory
  Pop $2  ; Get uninstaller exit code from stack
  StrCmp $2 0 common exit ; Only process common part if exit code is 0, otherwise exit

common:

  ; Make sure everything is cleaned up in case the old uninstaller did forget something
  DeleteRegKey HKLM "SOFTWARE\Sun\VirtualBox Guest Additions"
  DeleteRegKey /ifempty HKLM "SOFTWARE\Sun"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Sun VirtualBox Guest Additions"
  Delete /REBOOTOK "$1\netamd.inf"
  Delete /REBOOTOK "$1\pcntpci5.cat"
  Delete /REBOOTOK "$1\PCNTPCI5.sys"

  ; Try to remove old installation directory if empty
  RMDir /r /REBOOTOK "$SMPROGRAMS\Sun VirtualBox Guest Additions"
  RMDir /REBOOTOK "$1"

  ; Get original mouse driver info and restore it
  ;ReadRegStr $0 ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" ${ORG_MOUSE_PATH}
  ;WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\i8042prt" "ImagePath" $0
  ;Delete "$SYSDIR\drivers\VBoxMouseNT.sys"

  ; Delete vendor installation directory (only if completely empty)
!if $%KBUILD_TARGET_ARCH% == "x86"       ; 32-bit
  RMDir /REBOOTOK "$PROGRAMFILES32\Sun"
!else   ; 64-bit
  RMDir /REBOOTOK "$PROGRAMFILES64\Sun"
!endif

exit:

  Pop $2
  Pop $1
  Pop $0

FunctionEnd
!macroend
!insertmacro Uninstall_Sun ""

; This function cleans up an old xVM Sun installation
!macro Uninstall_SunXVM un
Function ${un}Uninstall_SunXVM

  Push $0
  Push $1
  Push $2

  ; Get current installation path
  ReadRegStr $0 HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Sun xVM VirtualBox Guest Additions" "UninstallString"
  StrCmp $0 "" exit

  ; Extract path
  Push "$0"       ; String
  Push "\"        ; SubString
  Push "<"        ; SearchDirection
  Push "<"        ; StrInclusionDirection
  Push "0"        ; IncludeSubString
  Push "0"        ; Loops
  Push "0"        ; CaseSensitive
  Call ${un}StrStrAdv
  Pop $1          ; $1 only contains the full path

  StrCmp $1 "" exit

  ; Save current i8042prt info to new uninstall registry path
  ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Sun xVM VirtualBox Guest Additions" ${ORG_MOUSE_PATH}
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" ${ORG_MOUSE_PATH} $0

  ; Try to wipe current installation directory
  Push $1 ; Push uninstaller path to stack
  Call ${un}Uninstall_WipeInstallationDirectory
  Pop $2  ; Get uninstaller exit code from stack
  StrCmp $2 0 common exit ; Only process common part if exit code is 0, otherwise exit

common:

  ; Make sure everything is cleaned up in case the old uninstaller did forget something
  DeleteRegKey HKLM "SOFTWARE\Sun\xVM VirtualBox Guest Additions"
  DeleteRegKey /ifempty HKLM "SOFTWARE\Sun"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Sun xVM VirtualBox Guest Additions"
  Delete /REBOOTOK "$1\netamd.inf"
  Delete /REBOOTOK "$1\pcntpci5.cat"
  Delete /REBOOTOK "$1\PCNTPCI5.sys"

  ; Try to remove old installation directory if empty
  RMDir /r /REBOOTOK "$SMPROGRAMS\Sun xVM VirtualBox Guest Additions"
  RMDir /REBOOTOK "$1"

  ; Delete vendor installation directory (only if completely empty)
!if $%KBUILD_TARGET_ARCH% == "x86"       ; 32-bit
  RMDir /REBOOTOK "$PROGRAMFILES32\Sun"
!else   ; 64-bit
  RMDir /REBOOTOK "$PROGRAMFILES64\Sun"
!endif

  ; Get original mouse driver info and restore it
  ;ReadRegStr $0 ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" ${ORG_MOUSE_PATH}
  ;WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\i8042prt" "ImagePath" $0
  ;Delete "$SYSDIR\drivers\VBoxMouseNT.sys"

exit:

  Pop $2
  Pop $1
  Pop $0

FunctionEnd
!macroend
!insertmacro Uninstall_SunXVM ""

; This function cleans up an old innotek installation
!macro Uninstall_Innotek un
Function ${un}Uninstall_Innotek

  Push $0
  Push $1
  Push $2

  ReadRegStr $0 HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\innotek VirtualBox Guest Additions" "UninstallString"
  StrCmp $0 "" exit

  ; Extract path
  Push "$0"       ; String
  Push "\"        ; SubString
  Push "<"        ; SearchDirection
  Push "<"        ; StrInclusionDirection
  Push "0"        ; IncludeSubString
  Push "0"        ; Loops
  Push "0"        ; CaseSensitive
  Call ${un}StrStrAdv
  Pop $1          ; $1 only contains the full path

  StrCmp $1 "" exit

  ; Save current i8042prt info to new uninstall registry path
  ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\innotek VirtualBox Guest Additions" ${ORG_MOUSE_PATH}
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" ${ORG_MOUSE_PATH} $0

  ; Try to wipe current installation directory
  Push $1 ; Push uninstaller path to stack
  Call ${un}Uninstall_WipeInstallationDirectory
  Pop $2  ; Get uninstaller exit code from stack
  StrCmp $2 0 common exit ; Only process common part if exit code is 0, otherwise exit

common:

  ; Remove left over files which were not entirely cached by the formerly running
  ; uninstaller
  DeleteRegKey HKLM "SOFTWARE\innotek\VirtualBox Guest Additions"
  DeleteRegKey HKLM "SOFTWARE\innotek"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\innotek VirtualBox Guest Additions"
  Delete /REBOOTOK "$1\VBoxGuestDrvInst.exe"
  Delete /REBOOTOK "$1\VBoxMouseInst.exe"
  Delete /REBOOTOK "$1\VBoxSFDrvInst.exe"
  Delete /REBOOTOK "$1\RegCleanup.exe"
  Delete /REBOOTOK "$1\VBoxService.exe"
  Delete /REBOOTOK "$1\VBoxMouseInst.exe"
  Delete /REBOOTOK "$1\innotek VirtualBox Guest Additions.url"
  Delete /REBOOTOK "$1\uninst.exe"
  Delete /REBOOTOK "$1\iexplore.ico"
  Delete /REBOOTOK "$1\install.log"
  Delete /REBOOTOK "$1\VBCoInst.dll"
  Delete /REBOOTOK "$1\VBoxControl.exe"
  Delete /REBOOTOK "$1\VBoxDisp.dll"
  Delete /REBOOTOK "$1\VBoxGINA.dll"
  Delete /REBOOTOK "$1\VBoxGuest.cat"
  Delete /REBOOTOK "$1\VBoxGuest.inf"
  Delete /REBOOTOK "$1\VBoxGuest.sys"
  Delete /REBOOTOK "$1\VBoxMouse.inf"
  Delete /REBOOTOK "$1\VBoxMouse.sys"
  Delete /REBOOTOK "$1\VBoxVideo.cat"
  Delete /REBOOTOK "$1\VBoxVideo.inf"
  Delete /REBOOTOK "$1\VBoxVideo.sys"

  ; Try to remove old installation directory if empty
  RMDir /r /REBOOTOK "$SMPROGRAMS\innotek VirtualBox Guest Additions"
  RMDir /REBOOTOK "$1"

  ; Delete vendor installation directory (only if completely empty)
!if $%KBUILD_TARGET_ARCH% == "x86"       ; 32-bit
  RMDir /REBOOTOK "$PROGRAMFILES32\innotek"
!else   ; 64-bit
  RMDir /REBOOTOK "$PROGRAMFILES64\innotek"
!endif

  ; Get original mouse driver info and restore it
  ;ReadRegStr $0 ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" ${ORG_MOUSE_PATH}
  ;WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\i8042prt" "ImagePath" $0
  ;Delete "$SYSDIR\drivers\VBoxMouseNT.sys"

exit:

  Pop $2
  Pop $1
  Pop $0

FunctionEnd
!macroend
!insertmacro Uninstall_Innotek ""
