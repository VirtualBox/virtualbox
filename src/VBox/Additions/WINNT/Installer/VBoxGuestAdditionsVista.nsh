; $Id: VBoxGuestAdditionsVista.nsh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; VBoxGuestAdditionsVista.nsh - Guest Additions installation for Windows Vista/7.
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

Function Vista_CheckForRequirements

  Push $0

  ${LogVerbose} "Checking for installation requirements for Vista / Windows 7 / Windows 8 ..."

  ; Nothing to do here right now.

  Pop $0

FunctionEnd

Function Vista_Prepare

  ; Try to restore the original Direct3D files in case we're coming from an old(er) Guest Additions
  ; installation, which formerly replaced those system files with our own stubs.
  ; This no longer is needed and thus needs to be reverted in any case.
  Call RestoreFilesDirect3D
  ; Ignore the result in case we had trouble restoring. The system would be in an inconsistent state anyway.

  Call VBoxMMR_Uninstall

FunctionEnd

Function Vista_CopyFiles

  SetOutPath "$INSTDIR"
  SetOverwrite on

  ; The files are for Vista only, they go into the application directory

  ; VBoxNET drivers are not tested yet - commented out until officially supported and released
  ;FILE "$%PATH_OUT%\bin\additions\VBoxNET.inf"
  ;FILE "$%PATH_OUT%\bin\additions\VBoxNET.sys"

FunctionEnd

Function Vista_InstallFiles

  ${LogVerbose} "Installing drivers for Vista / Windows 7 / Windows 8 ..."

  SetOutPath "$INSTDIR"
  ; Nothing here yet

!ifdef UNUSED_CODE
  Goto done

error:

  Abort "ERROR: Could not install files! Installation aborted."

done:
!endif ; UNUSED_CODE

FunctionEnd

Function Vista_Main

  Call Vista_Prepare
  Call Vista_CopyFiles
  Call Vista_InstallFiles

FunctionEnd

!macro Vista_UninstallInstDir un
Function ${un}Vista_UninstallInstDir

!if $%KBUILD_TARGET_ARCH% == "x86"       ; 32-bit
  Delete /REBOOTOK "$INSTDIR\netamd.inf"
  Delete /REBOOTOK "$INSTDIR\pcntpci5.cat"
  Delete /REBOOTOK "$INSTDIR\PCNTPCI5.sys"
!endif

FunctionEnd
!macroend
;!insertmacro Vista_UninstallInstDir "" - only .un version used
!insertmacro Vista_UninstallInstDir "un."

!macro Vista_Uninstall un
Function ${un}Vista_Uninstall

   ; Remove credential provider
   ${LogVerbose} "Removing auto-logon support ..."
   DeleteRegKey HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\{275D3BCC-22BB-4948-A7F6-3A3054EBA92B}"
   DeleteRegKey HKCR "CLSID\{275D3BCC-22BB-4948-A7F6-3A3054EBA92B}"
   Delete /REBOOTOK "$g_strSystemDir\VBoxCredProv.dll"

   Call ${un}VBoxMMR_Uninstall

FunctionEnd
!macroend
!ifndef UNINSTALLER_ONLY
  !insertmacro Vista_Uninstall ""
!endif
!insertmacro Vista_Uninstall "un."

!macro VBoxMMR_Uninstall un
Function ${un}VBoxMMR_Uninstall

  ; Remove VBoxMMR always

  DetailPrint "Uninstalling VBoxMMR."
  Call ${un}StopVBoxMMR

  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "VBoxMMR"

  Delete /REBOOTOK "$g_strSystemDir\VBoxMMR.exe"

  !if $%KBUILD_TARGET_ARCH% == "amd64"
    Delete /REBOOTOK "$g_strSysWow64\VBoxMMRHook.dll"
    Delete /REBOOTOK "$INSTDIR\VBoxMMR-x86.exe"
    Delete /REBOOTOK "$INSTDIR\VBoxMMRHook-x86.dll"
  !else
    Delete /REBOOTOK "$g_strSystemDir\VBoxMMRHook.dll"
    Delete /REBOOTOK "$INSTDIR\VBoxMMR.exe"
    Delete /REBOOTOK "$INSTDIR\VBoxMMRHook.dll"
  !endif

FunctionEnd
!macroend
!insertmacro VBoxMMR_Uninstall ""
!insertmacro VBoxMMR_Uninstall "un."
