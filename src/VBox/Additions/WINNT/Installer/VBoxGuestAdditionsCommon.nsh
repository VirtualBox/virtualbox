; $Id: VBoxGuestAdditionsCommon.nsh 107166 2024-11-26 09:03:23Z andreas.loeffler@oracle.com $
;; @file
; VBoxGuestAdditionsCommon.nsh - Common / shared utility functions.
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


!macro Common_CleanupObsoleteFiles un
;;
; Removes common files we're no longer shipping.
;
; During installation this step should be taken before copy files over in case
; the list gets out of sync and we start shipping files on it.  That way it
; doesn't much matter as the file will be restore afterwards.
;
Function ${un}Common_CleanupObsoleteFiles
  Delete /REBOOTOK "$INSTDIR\iexplore.ico"      ; Removed in r153662.
FunctionEnd
!macroend
!insertmacro Common_CleanupObsoleteFiles ""
!ifdef UNINSTALLER_ONLY
  !insertmacro Common_CleanupObsoleteFiles "un."
!else ifndef VBOX_SIGN_ADDITIONS
  !insertmacro Common_CleanupObsoleteFiles "un."
!endif

Function Common_CopyFiles

  SetOutPath "$INSTDIR"
  SetOverwrite on

!ifdef VBOX_WITH_LICENSE_INSTALL_RTF
  ; Copy license file (if any) into the installation directory
  FILE "/oname=${LICENSE_FILE_RTF}" "$%VBOX_BRAND_LICENSE_RTF%"
!endif

  FILE "$%PATH_OUT%\bin\additions\VBoxDrvInst.exe"

  FILE "$%PATH_OUT%\bin\additions\VBoxVideo.inf"
!if $%KBUILD_TARGET_ARCH% == "x86"
  ${If} $g_strEarlyNTDrvInfix != ""
    FILE "$%PATH_OUT%\bin\additions\VBoxVideoEarlyNT.inf"
  !ifdef VBOX_SIGN_ADDITIONS
    FILE "$%PATH_OUT%\bin\additions\VBoxVideoEarlyNT.cat"
  !endif
  ${EndIf}
!endif
!ifdef VBOX_SIGN_ADDITIONS
  ${If} $g_strWinVersion == "10"
    FILE "$%PATH_OUT%\bin\additions\VBoxVideo.cat"
  ${Else}
    FILE "/oname=VBoxVideo.cat" "$%PATH_OUT%\bin\additions\VBoxVideo-PreW10.cat"
  ${EndIf}
!endif

FunctionEnd

!ifndef UNINSTALLER_ONLY
;;
; Extract files to the install dir + arch.
;
Function ExtractFiles

  ; @todo: Use a define for all the file specs to group the files per module
  ; and keep the redundancy low

  Push $0
  StrCpy "$0" "$INSTDIR\$%KBUILD_TARGET_ARCH%"

  ; Root files
  SetOutPath "$0"
!if $%VBOX_WITH_LICENSE_INSTALL_RTF% == "1"
  FILE "/oname=${LICENSE_FILE_RTF}" "$%VBOX_BRAND_LICENSE_RTF%"
!endif

  ; Video driver
  SetOutPath "$0\VBoxVideo"
  FILE "$%PATH_OUT%\bin\additions\VBoxVideo.sys"
  FILE "$%PATH_OUT%\bin\additions\VBoxVideo.inf"
!if $%KBUILD_TARGET_ARCH% == "x86"
  ${If} $g_strEarlyNTDrvInfix != ""
    FILE "$%PATH_OUT%\bin\additions\VBoxVideoEarlyNT.inf"
  !ifdef VBOX_SIGN_ADDITIONS
    FILE "$%PATH_OUT%\bin\additions\VBoxVideoEarlyNT.cat"
  !endif
  ${EndIf}
!endif
!ifdef VBOX_SIGN_ADDITIONS
  ${If} $g_strWinVersion == "10"
    FILE "$%PATH_OUT%\bin\additions\VBoxVideo.cat"
  ${Else}
    FILE "/oname=VBoxVideo.cat" "$%PATH_OUT%\bin\additions\VBoxVideo-PreW10.cat"
  ${EndIf}
!endif
  FILE "$%PATH_OUT%\bin\additions\VBoxDisp.dll"

!if $%VBOX_WITH_WDDM% == "1"
  ; WDDM Video driver
  SetOutPath "$0\VBoxWddm"

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
!endif ; $%VBOX_WITH_WDDM% == "1"

  ; Mouse driver
  SetOutPath "$0\VBoxMouse"
  FILE "$%PATH_OUT%\bin\additions\VBoxMouse.sys"
  FILE "$%PATH_OUT%\bin\additions\VBoxMouse.inf"
!ifdef VBOX_SIGN_ADDITIONS
  ${If} $g_strWinVersion == "10"
    FILE "$%PATH_OUT%\bin\additions\VBoxMouse.cat"
  ${Else}
    FILE "/oname=VBoxMouse.cat" "$%PATH_OUT%\bin\additions\VBoxMouse-PreW10.cat"
  ${EndIf}
!endif

!if $%KBUILD_TARGET_ARCH% == "x86"
  SetOutPath "$0\VBoxMouse\NT4"
  FILE "$%PATH_OUT%\bin\additions\VBoxMouseNT.sys"
!endif

  ; Guest driver
  SetOutPath "$0\VBoxGuest"
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
  FILE "$%PATH_OUT%\bin\additions\VBoxTray.exe"
  FILE "$%PATH_OUT%\bin\additions\VBoxHook.dll"
  FILE "$%PATH_OUT%\bin\additions\VBoxControl.exe"

  ; VBoxService
  SetOutPath "$0\Bin"
  FILE "$%PATH_OUT%\bin\additions\VBoxService.exe"

  ; Shared Folders
  SetOutPath "$0\VBoxSF"
  FILE "$%PATH_OUT%\bin\additions\VBoxSF.sys"
  FILE "$%PATH_OUT%\bin\additions\VBoxMRXNP.dll"
  !if $%KBUILD_TARGET_ARCH% == "amd64"
    ; Only 64-bit installer: Also copy 32-bit DLLs on 64-bit target
    FILE "$%PATH_OUT%\bin\additions\VBoxMRXNP-x86.dll"
  !endif

  ; Auto-Logon
  SetOutPath "$0\AutoLogon"
  FILE "$%PATH_OUT%\bin\additions\VBoxGINA.dll"
  FILE "$%PATH_OUT%\bin\additions\VBoxCredProv.dll"

  ; Misc tools
  SetOutPath "$0\Tools"
  FILE "$%PATH_OUT%\bin\additions\VBoxDrvInst.exe"
!ifdef VBOX_WITH_ADDITIONS_SHIPPING_AUDIO_TEST
  FILE "$%PATH_OUT%\bin\additions\VBoxAudioTest.exe"
!endif

!if $%KBUILD_TARGET_ARCH% == "x86"
  SetOutPath "$0\Tools\NT4"
  FILE "$%PATH_OUT%\bin\additions\RegCleanup.exe"
!endif

  Pop $0

FunctionEnd
!endif ; UNINSTALLER_ONLY

;;
; Checks that the installer target architecture matches the host,
; i.e. that we're not trying to install 32-bit binaries on a 64-bit
; host from WOW64 mode.
;
!macro CheckArchitecture un
Function ${un}CheckArchitecture

  Push $0   ;; @todo r=bird: Why ??

  System::Call "kernel32::GetCurrentProcess() i .s"
  System::Call "kernel32::IsWow64Process(i s, *i .r0)"
  ; R0 now contains 1 if we're a 64-bit process, or 0 if not

!if $%KBUILD_TARGET_ARCH% == "amd64"
  IntCmp $0 0 wrong_platform
!else ; 32-bit
  IntCmp $0 1 wrong_platform
!endif

  Push 0
  Goto exit

wrong_platform:

  Push 1
  Goto exit

exit:

FunctionEnd
!macroend
!ifndef UNINSTALLER_ONLY
  !insertmacro CheckArchitecture ""
  !insertmacro CheckArchitecture "un."
!endif

;
; Macro for retrieving the Windows version this installer is running on.
;
; @return  Stack: Windows version string. Empty on error /
;                 if not able to identify.
;
!macro GetWindowsVersionEx un
Function ${un}GetWindowsVersionEx

  Push $0
  Push $1

  ; Check if we are running on Windows 2000 or above
  ; For other windows versions (> XP) it may be necessary to change winver.nsh
  Call ${un}GetWindowsVersion
  Pop $0         ; Windows Version

  Push $0        ; The windows version string
  Push "NT"      ; String to search for. W2K+ returns no string containing "NT"
  Call ${un}StrStr
  Pop $1

  ${If} $1 == "" ; If empty -> not NT 3.XX or 4.XX
    ; $0 contains the original version string
  ${Else}
    ; Ok we know it is NT. Must be a string like NT X.XX
    Push $0        ; The windows version string
    Push "4."      ; String to search for
    Call ${un}StrStr
    Pop $1
    ${If} $1 == "" ; If empty -> not NT 4
      ;; @todo NT <= 3.x ?
      ; $0 contains the original version string
    ${Else}
      StrCpy $0 "NT4"
    ${EndIf}
  ${EndIf}

  Pop $1
  Exch $0

FunctionEnd
!macroend
!ifndef UNINSTALLER_ONLY
  !insertmacro GetWindowsVersionEx ""
!endif
!insertmacro GetWindowsVersionEx "un."

!ifndef UNINSTALLER_ONLY
!macro GetAdditionsVersion un
Function ${un}GetAdditionsVersion

  Push $0
  Push $1

  ; Get additions version
  ReadRegStr $0 HKLM "SOFTWARE\$%VBOX_VENDOR_SHORT%\VirtualBox Guest Additions" "Version"

  ; Get revision
  ReadRegStr $g_strAddVerRev HKLM "SOFTWARE\$%VBOX_VENDOR_SHORT%\VirtualBox Guest Additions" "Revision"

  ; Extract major version
  Push "$0"       ; String
  Push "."        ; SubString
  Push ">"        ; SearchDirection
  Push "<"        ; StrInclusionDirection
  Push "0"        ; IncludeSubString
  Push "0"        ; Loops
  Push "0"        ; CaseSensitive
  Call ${un}StrStrAdv
  Pop $g_strAddVerMaj

  ; Extract minor version
  Push "$0"       ; String
  Push "."        ; SubString
  Push ">"        ; SearchDirection
  Push ">"        ; StrInclusionDirection
  Push "0"        ; IncludeSubString
  Push "0"        ; Loops
  Push "0"        ; CaseSensitive
  Call ${un}StrStrAdv
  Pop $1          ; Got first part (e.g. "1.5")

  Push "$1"       ; String
  Push "."        ; SubString
  Push ">"        ; SearchDirection
  Push "<"        ; StrInclusionDirection
  Push "0"        ; IncludeSubString
  Push "0"        ; Loops
  Push "0"        ; CaseSensitive
  Call ${un}StrStrAdv
  Pop $g_strAddVerMin   ; Extracted second part (e.g. "5" from "1.5")

  ; Extract build number
  Push "$0"       ; String
  Push "."        ; SubString
  Push "<"        ; SearchDirection
  Push ">"        ; StrInclusionDirection
  Push "0"        ; IncludeSubString
  Push "0"        ; Loops
  Push "0"        ; CaseSensitive
  Call ${un}StrStrAdv
  Pop $g_strAddVerBuild

  Pop $1
  Pop $0

FunctionEnd
!macroend
!insertmacro GetAdditionsVersion ""
; !insertmacro GetAdditionsVersion "un." - not used.
!endif ; UNINSTALLER_ONLY

!macro StopVBoxService un
Function ${un}StopVBoxService

  Push $0   ; Temp results
  Push $1
  Push $2   ; Image name of VBoxService
  Push $3   ; Safety counter

  StrCpy $3 "0" ; Init counter
  ${LogVerbose} "Stopping VBoxService ..."

  ${LogVerbose} "Stopping VBoxService via SCM ..."
  ${If} $g_strWinVersion == "NT4"
    nsExec::Exec '"$SYSDIR\net.exe" stop VBoxService'
  ${Else}
    nsExec::Exec '"$SYSDIR\sc.exe" stop VBoxService'
  ${EndIf}
  Sleep "1000"           ; Wait a bit

!ifdef _DEBUG
  ${LogVerbose} "Stopping VBoxService (as exe) ..."
!endif

exe_stop_loop:

  IntCmp $3 10 exit      ; Only try this loop 10 times max
  IntOp  $3 $3 + 1       ; Increment

!ifdef _DEBUG
  ${LogVerbose} "Stopping attempt #$3"
!endif

  StrCpy $2 "VBoxService.exe"

  ${nsProcess::FindProcess} $2 $0
  StrCmp $0 0 0 exit

  ${nsProcess::KillProcess} $2 $0
  Sleep "1000" ; Wait a bit
  Goto exe_stop_loop

exit:

  ${LogVerbose} "Stopping VBoxService done"

  Pop $3
  Pop $2
  Pop $1
  Pop $0

FunctionEnd
!macroend
!insertmacro StopVBoxService ""
!insertmacro StopVBoxService "un."

!macro StopVBoxTray un
Function ${un}StopVBoxTray

  Push $0   ; Temp results
  Push $1   ; Safety counter

  StrCpy $1 "0" ; Init counter
  ${LogVerbose} "Stopping VBoxTray ..."

exe_stop:

  IntCmp $1 10 exit      ; Only try this loop 10 times max
  IntOp  $1 $1 + 1       ; Increment

  ${nsProcess::FindProcess} "VBoxTray.exe" $0
  StrCmp $0 0 0 exit

  ${nsProcess::KillProcess} "VBoxTray.exe" $0
  Sleep "1000"           ; Wait a bit
  Goto exe_stop

exit:

  ${LogVerbose} "Stopping VBoxTray done"

  Pop $1
  Pop $0

FunctionEnd
!macroend
!insertmacro StopVBoxTray ""
!insertmacro StopVBoxTray "un."

!macro StopVBoxMMR un
Function ${un}StopVBoxMMR

  Push $0   ; Temp results
  Push $1   ; Safety counter

  StrCpy $1 "0" ; Init counter
  DetailPrint "Stopping VBoxMMR ..."

exe_stop:

  IntCmp $1 10 exit      ; Only try this loop 10 times max
  IntOp  $1 $1 + 1       ; Increment

  ${nsProcess::FindProcess} "VBoxMMR.exe" $0
  StrCmp $0 0 0 exit

  ${nsProcess::KillProcess} "VBoxMMR.exe" $0
  Sleep "1000"           ; Wait a bit
  Goto exe_stop

exit:

  DetailPrint "Stopping VBoxMMR done."

  Pop $1
  Pop $0

FunctionEnd
!macroend
!insertmacro StopVBoxMMR ""
!insertmacro StopVBoxMMR "un."

!macro WriteRegBinR ROOT KEY NAME VALUE
  WriteRegBin "${ROOT}" "${KEY}" "${NAME}" "${VALUE}"
!macroend

!ifdef UNUSED_CODE ; Only used by unused Uninstall_RunExtUnInstaller function in VBoxguestAdditionsUninstallOld.nsh.
!macro AbortShutdown un
Function ${un}AbortShutdown

  ${If} ${FileExists} "$g_strSystemDir\shutdown.exe"
    ; Try to abort the shutdown
    ${CmdExecute} "$\"$g_strSystemDir\shutdown.exe$\" -a" 'non-zero-exitcode=log'
  ${Else}
    ${LogVerbose} "Shutting down not supported: Binary $\"$g_strSystemDir\shutdown.exe$\" not found"
  ${EndIf}

FunctionEnd
!macroend
!insertmacro AbortShutdown ""
!insertmacro AbortShutdown "un."
!endif ; UNUSED_CODE

;;
; Sets $g_bCapDllCache, $g_bCapXPDM, $g_bWithWDDM and $g_bCapWDDM.
;
!macro CheckForCapabilities un
Function ${un}CheckForCapabilities

  Push $0

  ; Retrieve system mode and store result in
  System::Call 'user32::GetSystemMetrics(i ${SM_CLEANBOOT}) i .r0'
  StrCpy $g_iSystemMode $0

  ; Does the guest have a DLL cache?
  ${If}   $g_strWinVersion == "NT4"     ; bird: NT4 doesn't have a DLL cache, WTP is 5.0 <= NtVersion < 6.0.
  ${OrIf} $g_strWinVersion == "2000"
  ${OrIf} $g_strWinVersion == "XP"
    StrCpy $g_bCapDllCache "true"
    ${LogVerbose}  "OS has a DLL cache"
  ${EndIf}

  ${If}   $g_strWinVersion == "2000"
  ${OrIf} $g_strWinVersion == "XP"
  ${OrIf} $g_strWinVersion == "2003"
  ${OrIf} $g_strWinVersion == "Vista"
  ${OrIf} $g_strWinVersion == "7"
    StrCpy $g_bCapXPDM "true"
    ${LogVerbose} "OS is XPDM driver capable"
  ${EndIf}

!if $%VBOX_WITH_WDDM% == "1"
  ; By default use the WDDM driver on Vista+
  ${If}   $g_strWinVersion == "Vista"
  ${OrIf} $g_strWinVersion == "7"
  ${OrIf} $g_strWinVersion == "8"
  ${OrIf} $g_strWinVersion == "8_1"
  ${OrIf} $g_strWinVersion == "10"
    StrCpy $g_bWithWDDM "true"
    StrCpy $g_bCapWDDM "true"
    ${LogVerbose} "OS is WDDM driver capable"
  ${EndIf}
!endif

  Pop $0

FunctionEnd
!macroend
!ifndef UNINSTALLER_ONLY
  !insertmacro CheckForCapabilities ""
!endif
!insertmacro CheckForCapabilities "un."

; Switches (back) the path + registry view to
; 32-bit mode (SysWOW64) on 64-bit guests
!macro SetAppMode32 un
Function ${un}SetAppMode32
  !if $%KBUILD_TARGET_ARCH% == "amd64"
    ${EnableX64FSRedirection}
    SetRegView 32
  !endif
FunctionEnd
!macroend
!ifndef UNINSTALLER_ONLY
  !insertmacro SetAppMode32 ""
  !insertmacro SetAppMode32 "un."
!endif

; Because this NSIS installer is always built in 32-bit mode, we have to
; do some tricks for the Windows paths + registry on 64-bit guests
!macro SetAppMode64 un
Function ${un}SetAppMode64
  !if $%KBUILD_TARGET_ARCH% == "amd64"
    ${DisableX64FSRedirection}
    SetRegView 64
  !endif
FunctionEnd
!macroend
!insertmacro SetAppMode64 ""
!insertmacro SetAppMode64 "un."

;
; Retrieves the vendor ("CompanyName" of FILEINFO structure)
; of a given file.
; @return  Stack: Company name, or "" on error/if not found.
; @param   Stack: File name to retrieve vendor for.
;
!macro GetFileVendor un
Function ${un}GetFileVendor

  ; Preserve values
  Exch $0 ; Stack: $0 <filename> (Get file name into $0)
  Push $1

  IfFileExists "$0" found
  Goto not_found

found:

  VBoxGuestInstallHelper::FileGetVendor "$0"
  ; Stack: <vendor> $1 $0
  Pop  $0 ; Get vendor
  Pop  $1 ; Restore $1
  Exch $0 ; Restore $0, push vendor on top of stack
  Goto end

not_found:

  Pop $1
  Pop $0
  Push "File not found"
  Goto end

end:

FunctionEnd
!macroend
!insertmacro GetFileVendor ""
!insertmacro GetFileVendor "un."

;
; Retrieves the architecture of a given file.
; @return  Stack: Architecture ("x86", "amd64") or error message.
; @param   Stack: File name to retrieve architecture for.
;
!macro GetFileArchitecture un
Function ${un}GetFileArchitecture

  ; Preserve values
  Exch $0 ; Stack: $0 <filename> (Get file name into $0)
  Push $1

  IfFileExists "$0" found
  Goto not_found

found:

  ${LogVerbose} "Getting architecture of file $\"$0$\" ..."

  VBoxGuestInstallHelper::FileGetArchitecture "$0"

  ; Stack: <architecture> $1 $0
  Pop  $0 ; Get architecture string

  ${LogVerbose} "Architecture is: $0"

  Pop  $1 ; Restore $1
  Exch $0 ; Restore $0, push vendor on top of stack
  Goto end

not_found:

  Pop $1
  Pop $0
  Push "File not found"
  Goto end

end:

FunctionEnd
!macroend
!insertmacro GetFileArchitecture ""
!insertmacro GetFileArchitecture "un."

;
; Verifies a given file by checking its file vendor and target
; architecture.
; @return  Stack: "0" if valid, "1" if not, "2" on error / not found.
; @param   Stack: Architecture ("x86" or "amd64").
; @param   Stack: Vendor.
; @param   Stack: File name to verify.
;
!macro VerifyFile un
Function ${un}VerifyFile

  ; Preserve values
  Exch $0 ; File;         S: old$0 vendor arch
  Exch    ;               S: vendor old$0 arch
  Exch $1 ; Vendor;       S: old$1 old$0 arch
  Exch    ;               S: old$0 old$1 arch
  Exch 2  ;               S: arch old$1 old$0
  Exch $2 ; Architecture; S: old$2 old$1 old$0
  Push $3 ;               S: old$3 old$2 old$1 old$0

  ${LogVerbose} "Verifying file $\"$0$\" (vendor: $1, arch: $2) ..."

  IfFileExists "$0" check_arch
  Goto not_found

check_arch:

  ${LogVerbose} "File $\"$0$\" found"

  Push $0
  Call ${un}GetFileArchitecture
  Pop $3

  ${LogVerbose} "Architecture is: $3"

  ${If} $3 == $2
    Goto check_vendor
  ${EndIf}
  Goto invalid

check_vendor:

  Push $0
  Call ${un}GetFileVendor
  Pop $3

  ${LogVerbose} "Vendor is: $3"

  ${If} $3 == $1
    Goto valid
  ${EndIf}

invalid:

  ${LogVerbose} "File $\"$0$\" is invalid"

  StrCpy $3 "1" ; Invalid
  Goto end

valid:

  ${LogVerbose} "File $\"$0$\" is valid"

  StrCpy $3 "0" ; Valid
  Goto end

not_found:

  ${LogVerbose} "File $\"$0$\" was not found"

  StrCpy $3 "2" ; Not found
  Goto end

end:

  ; S: old$3 old$2 old$1 old$0
  Exch $3 ; S: $3 old$2 old$1 old$0
  Exch    ; S: old$2 $3 old$1
  Pop $2  ; S: $3 old$1 old$0
  Exch    ; S: old$1 $3 old$0
  Pop $1  ; S: $3 old$0
  Exch    ; S: old$0 $3
  Pop $0  ; S: $3

FunctionEnd
!macroend
!insertmacro VerifyFile ""
!insertmacro VerifyFile "un."

;
; Macro for accessing VerifyFile in a more convenient way by using
; a parameter list.
; @return  Stack: "0" if valid, "1" if not, "2" on error / not found.
; @param   Un/Installer prefix; either "" or "un".
; @param   Name of file to verify.
; @param   Vendor to check for.
; @param   Architecture ("x86" or "amd64") to check for.
;
!macro VerifyFileEx un File Vendor Architecture
  Push $0
  Push "${Architecture}"
  Push "${Vendor}"
  Push "${File}"
  Call ${un}VerifyFile
  Pop $0
  ${If} $0 == "0"
    ${LogVerbose} "Verification of file $\"${File}$\" successful (Vendor: ${Vendor}, Architecture: ${Architecture})"
  ${ElseIf} $0 == "1"
    ${LogVerbose} "Verification of file $\"${File}$\" failed (not Vendor: ${Vendor}, and/or not Architecture: ${Architecture})"
  ${Else}
    ${LogVerbose} "Skipping to file $\"${File}$\"; not found"
  ${EndIf}
  ; Push result popped off the stack to stack again
  Push $0
!macroend
!define VerifyFileEx "!insertmacro VerifyFileEx"

;
; Macro for copying a file only if the source file is verified
; to be from a certain vendor and architecture.
; @return  Stack: "0" if copied, "1" if not, "2" on error / not found.
; @param   Un/Installer prefix; either "" or "un".
; @param   Name of file to verify and copy to destination.
; @param   Destination name to copy verified file to.
; @param   Vendor to check for.
; @param   Architecture ("x86" or "amd64") to check for.
;
!macro CopyFileEx un FileSrc FileDest Vendor Architecture
  Push $0
  Push "${Architecture}"
  Push "${Vendor}"
  Push "${FileSrc}"
  Call ${un}VerifyFile
  Pop $0

  Push "${Architecture}"
  Push "Oracle Corporation"
  Push "${FileDest}"
  Call ${un}VerifyFile
  Pop $0

  ${If} $0 == "0"
    ${LogVerbose} "Copying verified file $\"${FileSrc}$\" to $\"${FileDest}$\" ..."
    ClearErrors
    SetOverwrite on
    CopyFiles /SILENT "${FileSrc}" "${FileDest}"
    ${If} ${Errors}
      CreateDirectory "$TEMP\${PRODUCT_NAME}"
      ${GetFileName} "${FileSrc}" $0 ; Get the base name
      CopyFiles /SILENT "${FileSrc}" "$TEMP\${PRODUCT_NAME}\$0"
      ${LogVerbose} "Immediate installation failed, postponing to next reboot (temporary location is: $\"$TEMP\${PRODUCT_NAME}\$0$\") ..."
      ;${InstallFileEx} "${un}" "${FileSrc}" "${FileDest}" "$TEMP" ; Only works with compile time files!
      System::Call "kernel32::MoveFileEx(t '$TEMP\${PRODUCT_NAME}\$0', t '${FileDest}', i 5)"
    ${EndIf}
  ${Else}
    ${LogVerbose} "Skipping to copy file $\"${FileSrc}$\" to $\"${FileDest}$\" (not Vendor: ${Vendor}, Architecture: ${Architecture})"
  ${EndIf}
  ; Push result popped off the stack to stack again
  Push $0
!macroend
!define CopyFileEx "!insertmacro CopyFileEx"

;
; Macro for installing a library/DLL.
; @return  Stack: "0" if copied, "1" if not, "2" on error / not found.
; @param   Un/Installer prefix; either "" or "un".
; @param   Name of lib/DLL to copy to destination.
; @param   Destination name to copy the source file to.
; @param   Temporary folder used for exchanging the (locked) lib/DLL after a reboot.
;
!macro InstallFileEx un FileSrc FileDest DirTemp
  ${LogVerbose} "Installing library $\"${FileSrc}$\" to $\"${FileDest}$\" ..."
  ; Try the gentle way and replace the file instantly
  !insertmacro InstallLib DLL NOTSHARED NOREBOOT_NOTPROTECTED "${FileSrc}" "${FileDest}" "${DirTemp}"
  ; If the above call didn't help, use a (later) reboot to replace the file
  !insertmacro InstallLib DLL NOTSHARED REBOOT_NOTPROTECTED "${FileSrc}" "${FileDest}" "${DirTemp}"
!macroend
!define InstallFileEx "!insertmacro InstallFileEx"

;
; Macro for installing a library/DLL.
; @return  Stack: "0" if copied, "1" if not, "2" on error / not found.
; @param   Un/Installer prefix; either "" or "un".
; @param   Name of lib/DLL to verify and copy to destination.
; @param   Destination name to copy verified file to.
; @param   Temporary folder used for exchanging the (locked) lib/DLL after a reboot.
; @param   Vendor to check for.
; @param   Architecture ("x86" or "amd64") to check for.
;
!macro InstallFileVerify un FileSrc FileDest DirTemp Vendor Architecture
  Push $0
  Push "${Architecture}"
  Push "${Vendor}"
  Push "${FileSrc}"
  ${LogVerbose} "Verifying library $\"${FileSrc}$\" ..."
  Call ${un}VerifyFile
  Pop $0
  ${If} $0 == "0"
    ${InstallFileEx} ${un} ${FileSrc} ${FileDest} ${DirTemp}
  ${Else}
    ${LogVerbose} "File $\"${FileSrc}$\" did not pass verification (Vendor: ${Vendor}, Architecture: ${Architecture})"
  ${EndIf}
  ; Push result popped off the stack to stack again.
  Push $0
!macroend
!define InstallFileVerify "!insertmacro InstallFileVerify"


; Note: We don't ship modified Direct3D files anymore, but we need to (try to)
;       restore the original (backed up) DLLs when upgrading from an old(er)
;       installation.
;
; Restores formerly backed up Direct3D original files, which were replaced by
; a VBox XPDM driver installation before. This might be necessary for upgrading a
; XPDM installation to a WDDM one.
; @return  Stack: "0" if files were restored successfully; otherwise "1".
;
!macro RestoreFilesDirect3D un
Function ${un}RestoreFilesDirect3D
  ${If}  $g_bCapXPDM != "true"
      ${LogVerbose} "RestoreFilesDirect3D: XPDM is not supported"
      Return
  ${EndIf}

  Push $0

  ; We need to switch to 64-bit app mode to handle the "real" 64-bit files in
  ; "system32" on a 64-bit guest
  Call ${un}SetAppMode64

  ${LogVerbose} "Restoring original D3D files ..."
  ${CopyFileEx} "${un}" "$SYSDIR\msd3d8.dll" "$SYSDIR\d3d8.dll" "Microsoft Corporation" "$%KBUILD_TARGET_ARCH%"
  ${CopyFileEx} "${un}" "$SYSDIR\msd3d9.dll" "$SYSDIR\d3d9.dll" "Microsoft Corporation" "$%KBUILD_TARGET_ARCH%"

  ${If} $g_bCapDllCache == "true"
    ${CopyFileEx} "${un}" "$SYSDIR\dllcache\msd3d8.dll" "$SYSDIR\dllcache\d3d8.dll" "Microsoft Corporation" "$%KBUILD_TARGET_ARCH%"
    ${CopyFileEx} "${un}" "$SYSDIR\dllcache\msd3d9.dll" "$SYSDIR\dllcache\d3d9.dll" "Microsoft Corporation" "$%KBUILD_TARGET_ARCH%"
  ${EndIf}

!if $%KBUILD_TARGET_ARCH% == "amd64"
  ${CopyFileEx} "${un}" "$g_strSysWow64\msd3d8.dll" "$g_strSysWow64\d3d8.dll" "Microsoft Corporation" "x86"
  ${CopyFileEx} "${un}" "$g_strSysWow64\msd3d9.dll" "$g_strSysWow64\d3d9.dll" "Microsoft Corporation" "x86"

  ${If} $g_bCapDllCache == "true"
    ${CopyFileEx} "${un}" "$g_strSysWow64\dllcache\msd3d8.dll" "$g_strSysWow64\dllcache\d3d8.dll" "Microsoft Corporation" "x86"
    ${CopyFileEx} "${un}" "$g_strSysWow64\dllcache\msd3d9.dll" "$g_strSysWow64\dllcache\d3d9.dll" "Microsoft Corporation" "x86"
  ${EndIf}
!endif

  Pop $0

FunctionEnd
!macroend
!insertmacro RestoreFilesDirect3D ""
!insertmacro RestoreFilesDirect3D "un."
