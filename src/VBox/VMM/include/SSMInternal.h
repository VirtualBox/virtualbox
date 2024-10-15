/* $Id: SSMInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * SSM - Internal header file.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef VMM_INCLUDED_SRC_include_SSMInternal_h
#define VMM_INCLUDED_SRC_include_SSMInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <VBox/types.h>
#include <VBox/vmm/ssm.h>
#include <iprt/critsect.h>

RT_C_DECLS_BEGIN

/** @defgroup grp_ssm_int       Internals
 * @ingroup grp_ssm
 * @internal
 * @{
 */


/**
 * Data unit callback type.
 */
typedef enum SSMUNITTYPE
{
    /** PDM Device . */
    SSMUNITTYPE_DEV = 1,
    /** PDM Driver. */
    SSMUNITTYPE_DRV,
    /** PDM USB device. */
    SSMUNITTYPE_USB,
    /** VM Internal. */
    SSMUNITTYPE_INTERNAL,
    /** External Wrapper. */
    SSMUNITTYPE_EXTERNAL
} SSMUNITTYPE;

/** Pointer to a data unit descriptor. */
typedef struct SSMUNIT *PSSMUNIT;

/**
 * Data unit descriptor.
 */
typedef struct SSMUNIT
{
    /** Pointer ot the next one in the list. */
    PSSMUNIT                pNext;

    /** Called in this save/load operation.
     * The flag is used to determine whether there is need for a call to
     * done or not. */
    bool                    fCalled;
    /** Finished its live part.
     * This is used to handle VERR_SSM_VOTE_FOR_GIVING_UP.  */
    bool                    fDoneLive;
    /** Callback interface type. */
    SSMUNITTYPE             enmType;
    /** Type specific data. */
    union
    {
        /** SSMUNITTYPE_DEV. */
        struct
        {
            /** Prepare live save. */
            PFNSSMDEVLIVEPREP   pfnLivePrep;
            /** Execute live save. */
            PFNSSMDEVLIVEEXEC   pfnLiveExec;
            /** Vote live save complete. */
            PFNSSMDEVLIVEVOTE   pfnLiveVote;
            /** Prepare save. */
            PFNSSMDEVSAVEPREP   pfnSavePrep;
            /** Execute save. */
            PFNSSMDEVSAVEEXEC   pfnSaveExec;
            /** Done save. */
            PFNSSMDEVSAVEDONE   pfnSaveDone;
            /** Prepare load. */
            PFNSSMDEVLOADPREP   pfnLoadPrep;
            /** Execute load. */
            PFNSSMDEVLOADEXEC   pfnLoadExec;
            /** Done load. */
            PFNSSMDEVLOADDONE   pfnLoadDone;
            /** Device instance. */
            PPDMDEVINS          pDevIns;
        } Dev;

        /** SSMUNITTYPE_DRV. */
        struct
        {
            /** Prepare live save. */
            PFNSSMDRVLIVEPREP   pfnLivePrep;
            /** Execute live save. */
            PFNSSMDRVLIVEEXEC   pfnLiveExec;
            /** Vote live save complete. */
            PFNSSMDRVLIVEVOTE   pfnLiveVote;
            /** Prepare save. */
            PFNSSMDRVSAVEPREP   pfnSavePrep;
            /** Execute save. */
            PFNSSMDRVSAVEEXEC   pfnSaveExec;
            /** Done save. */
            PFNSSMDRVSAVEDONE   pfnSaveDone;
            /** Prepare load. */
            PFNSSMDRVLOADPREP   pfnLoadPrep;
            /** Execute load. */
            PFNSSMDRVLOADEXEC   pfnLoadExec;
            /** Done load. */
            PFNSSMDRVLOADDONE   pfnLoadDone;
            /** Driver instance. */
            PPDMDRVINS          pDrvIns;
        } Drv;

        /** SSMUNITTYPE_USB. */
        struct
        {
            /** Prepare live save. */
            PFNSSMUSBLIVEPREP   pfnLivePrep;
            /** Execute live save. */
            PFNSSMUSBLIVEEXEC   pfnLiveExec;
            /** Vote live save complete. */
            PFNSSMUSBLIVEVOTE   pfnLiveVote;
            /** Prepare save. */
            PFNSSMUSBSAVEPREP   pfnSavePrep;
            /** Execute save. */
            PFNSSMUSBSAVEEXEC   pfnSaveExec;
            /** Done save. */
            PFNSSMUSBSAVEDONE   pfnSaveDone;
            /** Prepare load. */
            PFNSSMUSBLOADPREP   pfnLoadPrep;
            /** Execute load. */
            PFNSSMUSBLOADEXEC   pfnLoadExec;
            /** Done load. */
            PFNSSMUSBLOADDONE   pfnLoadDone;
            /** USB instance. */
            PPDMUSBINS          pUsbIns;
        } Usb;

        /** SSMUNITTYPE_INTERNAL. */
        struct
        {
            /** Prepare live save. */
            PFNSSMINTLIVEPREP   pfnLivePrep;
            /** Execute live save. */
            PFNSSMINTLIVEEXEC   pfnLiveExec;
            /** Vote live save complete. */
            PFNSSMINTLIVEVOTE   pfnLiveVote;
            /** Prepare save. */
            PFNSSMINTSAVEPREP   pfnSavePrep;
            /** Execute save. */
            PFNSSMINTSAVEEXEC   pfnSaveExec;
            /** Done save. */
            PFNSSMINTSAVEDONE   pfnSaveDone;
            /** Prepare load. */
            PFNSSMINTLOADPREP   pfnLoadPrep;
            /** Execute load. */
            PFNSSMINTLOADEXEC   pfnLoadExec;
            /** Done load. */
            PFNSSMINTLOADDONE   pfnLoadDone;
        } Internal;

        /** SSMUNITTYPE_EXTERNAL. */
        struct
        {
            /** Prepare live save. */
            PFNSSMEXTLIVEPREP   pfnLivePrep;
            /** Execute live save. */
            PFNSSMEXTLIVEEXEC   pfnLiveExec;
            /** Vote live save complete. */
            PFNSSMEXTLIVEVOTE   pfnLiveVote;
            /** Prepare save. */
            PFNSSMEXTSAVEPREP   pfnSavePrep;
            /** Execute save. */
            PFNSSMEXTSAVEEXEC   pfnSaveExec;
            /** Done save. */
            PFNSSMEXTSAVEDONE   pfnSaveDone;
            /** Prepare load. */
            PFNSSMEXTLOADPREP   pfnLoadPrep;
            /** Execute load. */
            PFNSSMEXTLOADEXEC   pfnLoadExec;
            /** Done load. */
            PFNSSMEXTLOADDONE   pfnLoadDone;
            /** User data. */
            void               *pvUser;
        } External;

        struct
        {
            /** Prepare live save. */
            PFNRT               pfnLivePrep;
            /** Execute live save. */
            PFNRT               pfnLiveExec;
            /** Vote live save complete. */
            PFNRT               pfnLiveVote;
            /** Prepare save. */
            PFNRT               pfnSavePrep;
            /** Execute save. */
            PFNRT               pfnSaveExec;
            /** Done save. */
            PFNRT               pfnSaveDone;
            /** Prepare load. */
            PFNRT               pfnLoadPrep;
            /** Execute load. */
            PFNRT               pfnLoadExec;
            /** Done load. */
            PFNRT               pfnLoadDone;
            /** User data. */
            void               *pvKey;
        } Common;
    } u;
    /** Data layout version. */
    uint32_t                u32Version;
    /** Instance number. */
    uint32_t                u32Instance;
    /** The offset of the final data unit.
     * This is used for constructing the directory. */
    RTFOFF                  offStream;
    /** Critical section to be taken before working any of the callbacks. */
    PPDMCRITSECT            pCritSect;
    /** The guessed size of the data unit - used only for progress indication. */
    size_t                  cbGuess;
    /** Name size. (bytes) */
    size_t                  cchName;
    /** Name of this unit. (extends beyond the defined size) */
    char                    szName[1];
} SSMUNIT;

AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLivePrep, u.Dev.pfnLivePrep);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLiveExec, u.Dev.pfnLiveExec);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLiveVote, u.Dev.pfnLiveVote);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnSavePrep, u.Dev.pfnSavePrep);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnSaveExec, u.Dev.pfnSaveExec);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnSaveDone, u.Dev.pfnSaveDone);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLoadPrep, u.Dev.pfnLoadPrep);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLoadExec, u.Dev.pfnLoadExec);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLoadDone, u.Dev.pfnLoadDone);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pvKey,       u.Dev.pDevIns);

AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLivePrep, u.Drv.pfnLivePrep);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLiveExec, u.Drv.pfnLiveExec);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLiveVote, u.Drv.pfnLiveVote);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnSavePrep, u.Drv.pfnSavePrep);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnSaveExec, u.Drv.pfnSaveExec);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnSaveDone, u.Drv.pfnSaveDone);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLoadPrep, u.Drv.pfnLoadPrep);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLoadExec, u.Drv.pfnLoadExec);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLoadDone, u.Drv.pfnLoadDone);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pvKey,       u.Drv.pDrvIns);

AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLivePrep, u.Usb.pfnLivePrep);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLiveExec, u.Usb.pfnLiveExec);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLiveVote, u.Usb.pfnLiveVote);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnSavePrep, u.Usb.pfnSavePrep);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnSaveExec, u.Usb.pfnSaveExec);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnSaveDone, u.Usb.pfnSaveDone);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLoadPrep, u.Usb.pfnLoadPrep);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLoadExec, u.Usb.pfnLoadExec);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLoadDone, u.Usb.pfnLoadDone);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pvKey,       u.Usb.pUsbIns);

AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLivePrep, u.Internal.pfnLivePrep);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLiveExec, u.Internal.pfnLiveExec);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLiveVote, u.Internal.pfnLiveVote);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnSavePrep, u.Internal.pfnSavePrep);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnSaveExec, u.Internal.pfnSaveExec);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnSaveDone, u.Internal.pfnSaveDone);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLoadPrep, u.Internal.pfnLoadPrep);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLoadExec, u.Internal.pfnLoadExec);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLoadDone, u.Internal.pfnLoadDone);

AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLivePrep, u.External.pfnLivePrep);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLiveExec, u.External.pfnLiveExec);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLiveVote, u.External.pfnLiveVote);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnSavePrep, u.External.pfnSavePrep);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnSaveExec, u.External.pfnSaveExec);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnSaveDone, u.External.pfnSaveDone);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLoadPrep, u.External.pfnLoadPrep);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLoadExec, u.External.pfnLoadExec);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pfnLoadDone, u.External.pfnLoadDone);
AssertCompile2MemberOffsets(SSMUNIT, u.Common.pvKey,       u.External.pvUser);


/**
 * SSM VM Instance data.
 * Changes to this must checked against the padding of the cfgm union in VM!
 *
 * @todo Move this to UVM.
 */
typedef struct SSM
{
    /** Critical section for serializing cancellation (pSSM). */
    RTCRITSECT              CancelCritSect;
    /** The handle of the current save or load operation.
     * This is used by SSMR3Cancel.  */
    PSSMHANDLE volatile     pSSM;

    /** FIFO of data entity descriptors. */
    R3PTRTYPE(PSSMUNIT)     pHead;
    /** The number of register units. */
    uint32_t                cUnits;
    /** For lazy init. */
    bool                    fInitialized;
    /** Current pass (for STAM). */
    uint32_t                uPass;
    uint32_t                u32Alignment;
} SSM;
/** Pointer to SSM VM instance data. */
typedef SSM *PSSM;



/** @} */

RT_C_DECLS_END

#endif /* !VMM_INCLUDED_SRC_include_SSMInternal_h */

