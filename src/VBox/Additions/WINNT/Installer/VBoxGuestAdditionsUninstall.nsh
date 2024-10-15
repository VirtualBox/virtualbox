; $Id: VBoxGuestAdditionsUninstall.nsh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; VBoxGuestAdditionsUninstall.nsh - Guest Additions uninstallation.
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

!macro UninstallCommon un
Function ${un}UninstallCommon

  Delete /REBOOTOK "$INSTDIR\install*.log"
  Delete /REBOOTOK "$INSTDIR\uninst.exe"
  Delete /REBOOTOK "$INSTDIR\${PRODUCT_NAME}.url"

  ; Remove common files
  Delete /REBOOTOK "$INSTDIR\VBoxDrvInst.exe"
  Delete /REBOOTOK "$INSTDIR\DIFxAPI.dll"

  Delete /REBOOTOK "$INSTDIR\VBoxVideo.inf"
!ifdef VBOX_SIGN_ADDITIONS
  Delete /REBOOTOK "$INSTDIR\VBoxVideo.cat"
!endif

!if $%VBOX_WITH_LICENSE_INSTALL_RTF% == "1"
  Delete /REBOOTOK "$INSTDIR\${LICENSE_FILE_RTF}"
!endif

  Delete /REBOOTOK "$INSTDIR\VBoxGINA.dll"

  ; Delete registry keys
  DeleteRegKey /ifempty HKLM "${PRODUCT_INSTALL_KEY}"
  DeleteRegKey /ifempty HKLM "${VENDOR_ROOT_KEY}"

  ; Delete desktop & start menu entries
  Delete "$DESKTOP\${PRODUCT_NAME}.lnk"            ; Obsolete. We don't install a desktop link any more.
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall.lnk"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\Website.url"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\Website.lnk" ; Old name. Changed to Website.url in r153663.
  RMDir  "$SMPROGRAMS\${PRODUCT_NAME}"

  ; Delete Guest Additions directory (only if completely empty)
  RMDir /REBOOTOK "$INSTDIR"

  ; Delete vendor installation directory (only if completely empty)
!if $%KBUILD_TARGET_ARCH% == "x86"       ; 32-bit
  RMDir /REBOOTOK "$PROGRAMFILES32\$%VBOX_VENDOR_SHORT%"
!else   ; 64-bit
  RMDir /REBOOTOK "$PROGRAMFILES64\$%VBOX_VENDOR_SHORT%"
!endif

  ; Remove registry entries
  DeleteRegKey ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}"

FunctionEnd
!macroend
;!insertmacro UninstallCommon "" - only .un version used
!insertmacro UninstallCommon "un."

!macro Uninstall un
Function ${un}Uninstall

  ${LogVerbose} "Uninstalling system files ..."
!ifdef _DEBUG
  ${LogVerbose} "Detected OS version: Windows $g_strWinVersion"
  ${LogVerbose} "System Directory: $g_strSystemDir"
!endif

  ; Which OS are we using?
!if $%KBUILD_TARGET_ARCH% == "x86"       ; 32-bit
  StrCmp $g_strWinVersion "NT4" nt4     ; Windows NT 4.0
!endif
  StrCmp $g_strWinVersion "2000" w2k    ; Windows 2000
  StrCmp $g_strWinVersion "XP" w2k      ; Windows XP
  StrCmp $g_strWinVersion "2003" w2k    ; Windows 2003 Server
  StrCmp $g_strWinVersion "Vista" vista ; Windows Vista
  StrCmp $g_strWinVersion "7" vista     ; Windows 7
  StrCmp $g_strWinVersion "8" vista     ; Windows 8
  StrCmp $g_strWinVersion "8_1" vista   ; Windows 8.1 / Windows Server 2012 R2
  StrCmp $g_strWinVersion "10" vista    ; Windows 10

  ${If} $g_bForceInstall == "true"
    Goto vista ; Assume newer OS than we know of ...
  ${EndIf}

  Goto notsupported

!if $%KBUILD_TARGET_ARCH% == "x86"       ; 32-bit
nt4:

  Call ${un}NT4_Uninstall
  goto common
!endif

w2k:

  Call ${un}W2K_Uninstall
  goto common

vista:

  Call ${un}W2K_Uninstall
  Call ${un}Vista_Uninstall
  goto common

notsupported:

  MessageBox MB_ICONSTOP $(VBOX_PLATFORM_UNSUPPORTED) /SD IDOK
  Goto exit

common:

exit:

FunctionEnd
!macroend
!ifndef UNINSTALLER_ONLY
  !insertmacro Uninstall ""
!endif
!insertmacro Uninstall "un."

;;
; The last step of the uninstallation where we remove all files from the
; install directory and such.
;
!macro UninstallInstDir un
Function ${un}UninstallInstDir

  ${LogVerbose} "Uninstalling directory ..."
!ifdef _DEBUG
  ${LogVerbose} "Detected OS version: Windows $g_strWinVersion"
  ${LogVerbose} "System Directory: $g_strSystemDir"
!endif

  ; Which OS are we using?
!if $%KBUILD_TARGET_ARCH% == "x86"       ; 32-bit
  StrCmp $g_strWinVersion "NT4" nt4     ; Windows NT 4.0
!endif
  StrCmp $g_strWinVersion "2000" w2k    ; Windows 2000
  StrCmp $g_strWinVersion "XP" w2k      ; Windows XP
  StrCmp $g_strWinVersion "2003" w2k    ; Windows 2003 Server
  StrCmp $g_strWinVersion "Vista" vista ; Windows Vista
  StrCmp $g_strWinVersion "7" vista     ; Windows 7
  StrCmp $g_strWinVersion "8" vista     ; Windows 8
  StrCmp $g_strWinVersion "8_1" vista   ; Windows 8.1 / Windows Server 2012 R2
  StrCmp $g_strWinVersion "10" vista    ; Windows 10

  ${If} $g_bForceInstall == "true"
    Goto vista ; Assume newer OS than we know of ...
  ${EndIf}

  MessageBox MB_ICONSTOP $(VBOX_PLATFORM_UNSUPPORTED) /SD IDOK
  Goto exit

!if $%KBUILD_TARGET_ARCH% == "x86"       ; 32-bit
nt4:

  Call ${un}NT4_UninstallInstDir
  goto common
!endif

w2k:

  Call ${un}W2K_UninstallInstDir
  goto common

vista:

  Call ${un}W2K_UninstallInstDir
  Call ${un}Vista_UninstallInstDir
  goto common

common:

  Call ${un}Common_CleanupObsoleteFiles

  ; This will attempt remove the install dir, so must be last.
  Call ${un}UninstallCommon

exit:

FunctionEnd
!macroend
;!insertmacro UninstallInstDir "" - only un. version is used.
!insertmacro UninstallInstDir "un."
