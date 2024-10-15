; $Id: VBoxGuestAdditionsLog.nsh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; VBoxGuestAdditionLog.nsh - Logging functions.
;

;
; Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

;
; Macro for enable/disable logging
; @param   "true" to enable logging, "false" to disable.
;
!macro _logEnable enable

  ${If} ${enable} == "true"
    LogSet on
    ${LogVerbose} "Started logging into separate file"
  ${Else}
    ${LogVerbose} "Stopped logging into separate file"
    LogSet off
  ${EndIf}

!macroend
!define LogEnable "!insertmacro _logEnable"

;
; Macro for (verbose) logging
; @param   Text to log.
;
!macro _logVerbose text

  LogText "${text}"
  IfSilent +2
    DetailPrint "${text}"

!macroend
!define LogVerbose "!insertmacro _logVerbose"

;
; Sends a logging text to the running instance of VBoxTray
; which then presents to text via balloon popup in the system tray (if enabled).
;
; @param   Message type (0=Info, 1=Warning, 2=Error).
; @param   Message text.
;
; @todo Add message timeout as parameter.
;
!macro _logToVBoxTray type text

    ${LogVerbose} "To VBoxTray: ${text}"
!if $%VBOX_WITH_GUEST_INSTALL_HELPER% == "1"
    Push $0
    ; Parameters:
    ; - String: Description / Body
    ; - String: Title / Name of application
    ; - Integer: Type of message: 1 (Info), 2 (Warning), 3 (Error)
    ; - Integer: Time (in msec) to show the notification
    VBoxGuestInstallHelper::VBoxTrayShowBallonMsg "${text}" "VirtualBox Guest Additions Setup" ${type} 5000
    Pop $0 ; Get return value (ignored for now)
    Pop $0 ; Restore original $0 from stack
!endif

!macroend
!define LogToVBoxTray "!insertmacro _logToVBoxTray"
