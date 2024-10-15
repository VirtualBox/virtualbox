; $Id: VBoxGuestAdditionsExternal.nsh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; VBoxGuestAdditionExternal.nsh - Utility function for invoking external applications.
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

;;
; Macro for executing external applications.
;
; Uses the nsExec plugin in different styles, depending on whether this
; installer runs in silent mode or not. If the external program reports an exit
; code other than 0 the installer will be aborted.
;
; @param   ${cmdline}   Command line (full qualified and quoted).
; @param   ${options}   Either 'non-zero-exitcode=log' or 'non-zero-exitcode=abort'.
;                       Controls how to handle non-zero exit codes, in the latter
;                       case they will trigger an installation abort, while in the
;                       form only a warning in the log file.
;
!macro _cmdExecute cmdline options
  ; Save $0 & $1
  Push $0
  Push $1

  ; Check that the options are valid.
  ${IfNot}    ${options} != "exitcode=0"
  ${AndIfNot} ${options} != "ignore-exitcode"
    Abort "Internal error in _cmdExecute: options=${options} cmdline=${cmdline}"
  ${EndIf}

  ;
  ; Execute the command, putting the exit code in $0 when done.
  ;
  ${LogVerbose} "Executing: ${cmdline}"
  ${If} ${Silent}
    nsExec::ExecToStack "${cmdline}"
    Pop $0 ; Return value (exit code)
    Pop $1 ; Stdout/stderr output (up to ${NSIS_MAX_STRLEN})
    ${LogVerbose} "$1"
  ${Else}
    nsExec::ExecToLog "${cmdline}"
    Pop $0 ; Return value (exit code)
  ${EndIf}

  ${LogVerbose} "Execution returned exit code: $0"

  ;
  ; Check if it failed and take action according to the 2nd argument.
  ;
  ${If} $0 <> 0
    ${If} ${options} == "exitcode=0"
      ${LogVerbose} "Error excuting $\"${cmdline}$\" (exit code: $0) -- aborting installation"
      Abort "Error excuting $\"${cmdline}$\" (exit code: $0) -- aborting installation"
    ${Else}
      ${LogVerbose} "Warning: Executing $\"${cmdline}$\" returned with exit code $0"
    ${EndIf}
  ${EndIf}

  ; Restore $0 and $1.
  Pop $1
  Pop $0
!macroend
!define CmdExecute "!insertmacro _cmdExecute"
