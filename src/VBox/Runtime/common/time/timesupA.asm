; $Id: timesupA.asm 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; IPRT - Time using SUPLib, the Assembly Implementation.
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
; The contents of this file may alternatively be used under the terms
; of the Common Development and Distribution License Version 1.0
; (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
; in the VirtualBox distribution, in which case the provisions of the
; CDDL are applicable instead of those of the GPL.
;
; You may elect to license modified versions of this file under the
; terms and conditions of either the GPL or the CDDL or both.
;
; SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
;

%ifndef IN_GUEST

%include "iprt/asmdefs.mac"
%include "VBox/sup.mac"

;
; Use the C reference implementation for now.
;
%error "This is out of date, use C code.  Not worth it for a couple of ticks in some functions and equal or worse performance in others."
This is out of date
This is out of date
This is out of date


;; Keep this in sync with iprt/time.h.
struc RTTIMENANOTSDATA
    .pu64Prev           RTCCPTR_RES 1
    .pfnBad             RTCCPTR_RES 1
    .pfnRediscover      RTCCPTR_RES 1
    .pvDummy            RTCCPTR_RES 1
    .c1nsSteps          resd 1
    .cExpired           resd 1
    .cBadPrev           resd 1
    .cUpdateRaces       resd 1
endstruc


BEGINDATA
%undef IN_SUPLIB
%undef IMPORTED_SUPLIB
%ifdef IN_SUP_R0
 %define IN_SUPLIB
%endif
%ifdef IN_SUP_R3
 %define IN_SUPLIB
%endif
%ifdef IN_SUP_RC
 %define IN_SUPLIB
%endif
%ifdef IN_SUPLIB
 extern NAME(g_pSUPGlobalInfoPage)
%elifdef IN_RING0
 %ifdef RT_OS_WINDOWS
  %define IMPORTED_SUPLIB
  extern IMPNAME(g_SUPGlobalInfoPage)
 %else
  extern NAME(g_SUPGlobalInfoPage)
 %endif
%else
 %ifdef RT_OS_WINDOWS
  %define IMPORTED_SUPLIB
  extern IMPNAME(g_pSUPGlobalInfoPage)
 %else
  extern NAME(g_pSUPGlobalInfoPage)
 %endif
%endif

BEGINCODE

;
; The default stuff that works everywhere.
; Uses cpuid for serializing.
;
%undef  ASYNC_GIP
%undef  USE_LFENCE
%undef  WITH_TSC_DELTA
%undef  NEED_APIC_ID
%define NEED_TRANSACTION_ID
%define rtTimeNanoTSInternalAsm    RTTimeNanoTSLegacySyncNoDelta
%include "timesupA.mac"

%define rtTimeNanoTSInternalAsm    RTTimeNanoTSLegacyInvariantNoDelta
%include "timesupA.mac"

%define WITH_TSC_DELTA
%define NEED_APIC_ID
%define rtTimeNanoTSInternalAsm    RTTimeNanoTSLegacySyncWithDelta
%include "timesupA.mac"

%define rtTimeNanoTSInternalAsm    RTTimeNanoTSLegacyInvariantWithDelta
%include "timesupA.mac"

%define ASYNC_GIP
%undef  WITH_TSC_DELTA
%define NEED_APIC_ID
%ifdef IN_RC
 %undef NEED_TRANSACTION_ID
%endif
%define rtTimeNanoTSInternalAsm    RTTimeNanoTSLegacyAsync
%include "timesupA.mac"


;
; Alternative implementation that employs lfence instead of cpuid.
;
%undef  ASYNC_GIP
%define USE_LFENCE
%undef  WITH_TSC_DELTA
%undef  NEED_APIC_ID
%define NEED_TRANSACTION_ID
%define rtTimeNanoTSInternalAsm    RTTimeNanoTSLFenceSyncNoDelta
%include "timesupA.mac"

%define rtTimeNanoTSInternalAsm    RTTimeNanoTSLFenceInvariantNoDelta
%include "timesupA.mac"

%define WITH_TSC_DELTA
%define NEED_APIC_ID
%define rtTimeNanoTSInternalAsm    RTTimeNanoTSLFenceSyncWithDelta
%include "timesupA.mac"

%define rtTimeNanoTSInternalAsm    RTTimeNanoTSLFenceInvariantWithDelta
%include "timesupA.mac"

%define ASYNC_GIP
%undef  WITH_TSC_DELTA
%define NEED_APIC_ID
%ifdef IN_RC
 %undef NEED_TRANSACTION_ID
%endif
%define rtTimeNanoTSInternalAsm    RTTimeNanoTSLFenceAsync
%include "timesupA.mac"


%endif ; !IN_GUEST
