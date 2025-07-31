/* $Id: DevHda.cpp 110494 2025-07-31 10:11:09Z andreas.loeffler@oracle.com $ */
/** @file
 * Intel HD Audio Controller Emulation.
 *
 * Implemented against the specifications found in "High Definition Audio
 * Specification", Revision 1.0a June 17, 2010, and  "Intel I/O Controller
 * HUB 6 (ICH6) Family, Datasheet", document number 301473-002.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_HDA
#include <VBox/log.h>

#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pdmaudioifs.h>
#include <VBox/vmm/pdmaudioinline.h>
#ifdef HDA_DEBUG_GUEST_RIP
# include <VBox/vmm/cpum.h>
#endif
#include <VBox/version.h>
#include <VBox/AssertGuest.h>

#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/asm-math.h>
#include <iprt/file.h>
#include <iprt/list.h>
# include <iprt/string.h>
#ifdef IN_RING3
# include <iprt/mem.h>
# include <iprt/semaphore.h>
# include <iprt/uuid.h>
#endif

#include "VBoxDD.h"

#include "AudioMixBuffer.h"
#include "AudioMixer.h"

#define VBOX_HDA_CAN_ACCESS_REG_MAP /* g_aHdaRegMap is accessible */
#include "DevHda.h"

#include "AudioHlp.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#if defined(VBOX_WITH_HP_HDA)
/* HP Pavilion dv4t-1300 */
# define HDA_PCI_VENDOR_ID 0x103c
# define HDA_PCI_DEVICE_ID 0x30f7
#elif defined(VBOX_WITH_INTEL_HDA)
/* Intel HDA controller */
# define HDA_PCI_VENDOR_ID 0x8086
# define HDA_PCI_DEVICE_ID 0x2668
#elif defined(VBOX_WITH_NVIDIA_HDA)
/* nVidia HDA controller */
# define HDA_PCI_VENDOR_ID 0x10de
# define HDA_PCI_DEVICE_ID 0x0ac0
#else
# error "Please specify your HDA device vendor/device IDs"
#endif

/**
 * Acquires the HDA lock.
 */
#define DEVHDA_LOCK(a_pDevIns, a_pThis) \
    do { \
        int const rcLock = PDMDevHlpCritSectEnter((a_pDevIns), &(a_pThis)->CritSect, VERR_IGNORED); \
        PDM_CRITSECT_RELEASE_ASSERT_RC_DEV((a_pDevIns), &(a_pThis)->CritSect, rcLock); \
    } while (0)

/**
 * Acquires the HDA lock or returns.
 */
#define DEVHDA_LOCK_RETURN(a_pDevIns, a_pThis, a_rcBusy) \
    do { \
        int const rcLock = PDMDevHlpCritSectEnter((a_pDevIns), &(a_pThis)->CritSect, a_rcBusy); \
        if (rcLock == VINF_SUCCESS) \
        { /* likely */ } \
        else \
        { \
            AssertRC(rcLock); \
            return rcLock; \
        } \
    } while (0)

/**
 * Acquires the HDA lock or returns.
 */
# define DEVHDA_LOCK_RETURN_VOID(a_pDevIns, a_pThis) \
    do { \
        int const rcLock = PDMDevHlpCritSectEnter((a_pDevIns), &(a_pThis)->CritSect, VERR_IGNORED); \
        if (rcLock == VINF_SUCCESS) \
        { /* likely */ } \
        else \
        { \
            PDM_CRITSECT_RELEASE_ASSERT_RC_DEV((a_pDevIns), &(a_pThis)->CritSect, rcLock); \
            return; \
        } \
    } while (0)

/**
 * Releases the HDA lock.
 */
#define DEVHDA_UNLOCK(a_pDevIns, a_pThis) \
    do { PDMDevHlpCritSectLeave((a_pDevIns), &(a_pThis)->CritSect); } while (0)

/**
 * Acquires the TM lock and HDA lock, returns on failure.
 */
#define DEVHDA_LOCK_BOTH_RETURN(a_pDevIns, a_pThis, a_pStream, a_rcBusy) \
    do { \
        VBOXSTRICTRC rcLock = PDMDevHlpTimerLockClock2(pDevIns, (a_pStream)->hTimer, &(a_pThis)->CritSect,  (a_rcBusy)); \
        if (RT_LIKELY(rcLock == VINF_SUCCESS)) \
        {  /* likely */ } \
        else \
            return VBOXSTRICTRC_TODO(rcLock); \
    } while (0)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Structure defining a (host backend) driver stream.
 * Each driver has its own instances of audio mixer streams, which then
 * can go into the same (or even different) audio mixer sinks.
 */
typedef struct HDADRIVERSTREAM
{
    /** Associated mixer handle. */
    R3PTRTYPE(PAUDMIXSTREAM)           pMixStrm;
} HDADRIVERSTREAM, *PHDADRIVERSTREAM;

/**
 * Struct for maintaining a host backend driver.
 * This driver must be associated to one, and only one,
 * HDA codec. The HDA controller does the actual multiplexing
 * of HDA codec data to various host backend drivers then.
 *
 * This HDA device uses a timer in order to synchronize all
 * read/write accesses across all attached LUNs / backends.
 */
typedef struct HDADRIVER
{
    /** Node for storing this driver in our device driver list of HDASTATE. */
    RTLISTNODER3                       Node;
    /** Pointer to shared HDA device state. */
    R3PTRTYPE(PHDASTATE)               pHDAStateShared;
    /** Pointer to the ring-3 HDA device state. */
    R3PTRTYPE(PHDASTATER3)             pHDAStateR3;
    /** LUN to which this driver has been assigned. */
    uint8_t                            uLUN;
    /** Whether this driver is in an attached state or not. */
    bool                               fAttached;
    uint8_t                            u32Padding0[6];
    /** Pointer to attached driver base interface. */
    R3PTRTYPE(PPDMIBASE)               pDrvBase;
    /** Audio connector interface to the underlying host backend. */
    R3PTRTYPE(PPDMIAUDIOCONNECTOR)     pConnector;
    /** Mixer stream for line input. */
    HDADRIVERSTREAM                    LineIn;
#ifdef VBOX_WITH_AUDIO_HDA_MIC_IN
    /** Mixer stream for mic input. */
    HDADRIVERSTREAM                    MicIn;
#endif
    /** Mixer stream for front output. */
    HDADRIVERSTREAM                    Front;
#ifdef VBOX_WITH_AUDIO_HDA_51_SURROUND
    /** Mixer stream for center/LFE output. */
    HDADRIVERSTREAM                    CenterLFE;
    /** Mixer stream for rear output. */
    HDADRIVERSTREAM                    Rear;
#endif
    /** The LUN description. */
    char                               szDesc[48 - 2];
} HDADRIVER;
/** The HDA host driver backend. */
typedef struct HDADRIVER *PHDADRIVER;


/** Internal state of this BDLE.
 *  Not part of the actual BDLE registers.
 * @note Only for saved state.  */
typedef struct HDABDLESTATELEGACY
{
    /** Own index within the BDL (Buffer Descriptor List). */
    uint32_t     u32BDLIndex;
    /** Number of bytes below the stream's FIFO watermark (SDFIFOW).
     *  Used to check if we need fill up the FIFO again. */
    uint32_t     cbBelowFIFOW;
    /** Current offset in DMA buffer (in bytes).*/
    uint32_t     u32BufOff;
    uint32_t     Padding;
} HDABDLESTATELEGACY;

/**
 * BDLE and state.
 * @note Only for saved state.
 */
typedef struct HDABDLELEGACY
{
    /** The actual BDL description. */
    HDABDLEDESC         Desc;
    HDABDLESTATELEGACY  State;
} HDABDLELEGACY;
AssertCompileSize(HDABDLELEGACY, 32);


/** Read callback. */
typedef VBOXSTRICTRC FNHDAREGREAD(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t *pu32Value);
/** Write callback. */
typedef VBOXSTRICTRC FNHDAREGWRITE(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value);

/**
 * HDA register descriptor.
 */
typedef struct HDAREGDESC
{
    /** Register offset in the register space. */
    uint32_t        off;
    /** Size in bytes. Registers of size > 4 are in fact tables. */
    uint8_t         cb;
    /** Register descriptor (RD) flags of type HDA_RD_F_XXX. These are used to
     *  specify the read/write handling policy of the register. */
    uint8_t         fFlags;
    /** Index into the register storage array (HDASTATE::au32Regs). */
    uint8_t         idxReg;
    uint8_t         bUnused;
    /** Readable bits. */
    uint32_t        fReadableMask;
    /** Writable bits. */
    uint32_t        fWritableMask;
    /** Read callback. */
    FNHDAREGREAD   *pfnRead;
    /** Write callback. */
    FNHDAREGWRITE  *pfnWrite;
#if defined(IN_RING3) || defined(LOG_ENABLED) /* Saves 0x2f23 - 0x1888 = 0x169B (5787) bytes in VBoxDDR0. */
    /** Abbreviated name. */
    const char     *pszName;
# ifdef IN_RING3
    /** Description (for stats). */
    const char     *pszDesc;
# endif
#endif
} HDAREGDESC;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#ifndef VBOX_DEVICE_STRUCT_TESTCASE
#ifdef IN_RING3
static void hdaR3GCTLReset(PPDMDEVINS pDevIns, PHDASTATE pThis, PHDASTATER3 pThisCC);
#endif

/** @name Register read/write stubs.
 * @{
 */
static FNHDAREGREAD  hdaRegReadUnimpl;
static FNHDAREGWRITE hdaRegWriteUnimpl;
/** @} */

/** @name Global register set read/write functions.
 * @{
 */
static FNHDAREGWRITE hdaRegWriteGCTL;
static FNHDAREGREAD  hdaRegReadLPIB;
static FNHDAREGREAD  hdaRegReadWALCLK;
static FNHDAREGWRITE hdaRegWriteSSYNC;
static FNHDAREGWRITE hdaRegWriteNewSSYNC;
static FNHDAREGWRITE hdaRegWriteCORBWP;
static FNHDAREGWRITE hdaRegWriteCORBRP;
static FNHDAREGWRITE hdaRegWriteCORBCTL;
static FNHDAREGWRITE hdaRegWriteCORBSIZE;
static FNHDAREGWRITE hdaRegWriteCORBSTS;
static FNHDAREGWRITE hdaRegWriteRINTCNT;
static FNHDAREGWRITE hdaRegWriteRIRBWP;
static FNHDAREGWRITE hdaRegWriteRIRBSTS;
static FNHDAREGWRITE hdaRegWriteSTATESTS;
static FNHDAREGWRITE hdaRegWriteIRS;
static FNHDAREGREAD  hdaRegReadIRS;
static FNHDAREGWRITE hdaRegWriteBase;
/** @} */

/** @name {IOB}SDn read/write functions.
 * @{
 */
static FNHDAREGWRITE hdaRegWriteSDCBL;
static FNHDAREGWRITE hdaRegWriteSDCTL;
static FNHDAREGWRITE hdaRegWriteSDSTS;
static FNHDAREGWRITE hdaRegWriteSDLVI;
static FNHDAREGWRITE hdaRegWriteSDFIFOW;
static FNHDAREGWRITE hdaRegWriteSDFIFOS;
static FNHDAREGWRITE hdaRegWriteSDFMT;
static FNHDAREGWRITE hdaRegWriteSDBDPL;
static FNHDAREGWRITE hdaRegWriteSDBDPU;
static FNHDAREGREAD  hdaRegReadSDnPIB;
static FNHDAREGREAD  hdaRegReadSDnEFIFOS;
/** @} */

/** @name Generic register read/write functions.
 * @{
 */
static FNHDAREGREAD  hdaRegReadU32;
static FNHDAREGWRITE hdaRegWriteU32;
static FNHDAREGREAD  hdaRegReadU24;
#ifdef IN_RING3
static FNHDAREGWRITE hdaRegWriteU24;
#endif
static FNHDAREGREAD  hdaRegReadU16;
static FNHDAREGWRITE hdaRegWriteU16;
static FNHDAREGREAD  hdaRegReadU8;
static FNHDAREGWRITE hdaRegWriteU8;
/** @} */

/** @name HDA device functions.
 * @{
 */
#ifdef IN_RING3
static int hdaR3AddStream(PHDASTATER3 pThisCC, PPDMAUDIOSTREAMCFG pCfg);
static int hdaR3RemoveStream(PHDASTATER3 pThisCC, PPDMAUDIOSTREAMCFG pCfg);
#endif /* IN_RING3 */
/** @} */

/** @name HDA mixer functions.
 * @{
 */
#ifdef IN_RING3
static int hdaR3MixerAddDrvStream(PPDMDEVINS pDevIns, PAUDMIXSINK pMixSink, PCPDMAUDIOSTREAMCFG pCfg, PHDADRIVER pDrv);
#endif
/** @} */

#ifdef IN_RING3
static FNSSMFIELDGETPUT hdaR3GetPutTrans_HDABDLEDESC_fFlags_6;
static FNSSMFIELDGETPUT hdaR3GetPutTrans_HDABDLE_Desc_fFlags_1thru4;
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** No register description (RD) flags defined. */
#define HDA_RD_F_NONE               0
/** Writes to SD are allowed while RUN bit is set. */
#define HDA_RD_F_SD_WRITE_RUN       RT_BIT(0)

/** @def HDA_REG_ENTRY_EX
 * Maps the entry values to the actual HDAREGDESC layout, which is differs
 * depending on context and build type. */
#if defined(IN_RING3) || defined(LOG_ENABLED)
# ifdef IN_RING3
#  define HDA_REG_ENTRY_EX(a_offBar, a_cbReg, a_fReadMask, a_fWriteMask, a_fFlags, a_pfnRead, a_pfnWrite, a_idxMap, a_szName, a_szDesc) \
    { a_offBar, a_cbReg, a_fFlags, a_idxMap, 0, a_fReadMask, a_fWriteMask, a_pfnRead, a_pfnWrite, a_szName, a_szDesc }
# else
#  define HDA_REG_ENTRY_EX(a_offBar, a_cbReg, a_fReadMask, a_fWriteMask, a_fFlags, a_pfnRead, a_pfnWrite, a_idxMap, a_szName, a_szDesc) \
    { a_offBar, a_cbReg, a_fFlags, a_idxMap, 0, a_fReadMask, a_fWriteMask, a_pfnRead, a_pfnWrite, a_szName }
# endif
#else
# define HDA_REG_ENTRY_EX(a_offBar, a_cbReg, a_fReadMask, a_fWriteMask, a_fFlags, a_pfnRead, a_pfnWrite, a_idxMap, a_szName, a_szDesc) \
    { a_offBar, a_cbReg, a_fFlags, a_idxMap, 0, a_fReadMask, a_fWriteMask, a_pfnRead, a_pfnWrite }
#endif

#define HDA_REG_ENTRY(a_offBar, a_cbReg, a_fReadMask, a_fWriteMask, a_fFlags, a_pfnRead, a_pfnWrite, a_ShortRegNm, a_szDesc) \
    HDA_REG_ENTRY_EX(a_offBar, a_cbReg, a_fReadMask, a_fWriteMask, a_fFlags, a_pfnRead, a_pfnWrite, HDA_MEM_IND_NAME(a_ShortRegNm), #a_ShortRegNm, a_szDesc)
#define HDA_REG_ENTRY_STR(a_offBar, a_cbReg, a_fReadMask, a_fWriteMask, a_fFlags, a_pfnRead, a_pfnWrite, a_StrPrefix, a_ShortRegNm, a_szDesc) \
    HDA_REG_ENTRY_EX(a_offBar, a_cbReg, a_fReadMask, a_fWriteMask, a_fFlags, a_pfnRead, a_pfnWrite, HDA_MEM_IND_NAME(a_StrPrefix ## a_ShortRegNm), #a_StrPrefix #a_ShortRegNm, #a_StrPrefix ": " a_szDesc)

/** Emits a single audio stream register set (e.g. OSD0) at a specified offset. */
#define HDA_REG_MAP_STRM(offset, name) \
                   /* offset         size     read mask   write mask  flags                  read callback   write callback     index, abbrev, description */ \
                   /* -------        -------  ----------  ----------  ---------------------- --------------  -----------------  -----------------------------  ----------- */ \
    /* Offset 0x80 (SD0) */ \
    HDA_REG_ENTRY_STR(offset,        0x00003, 0x00FF001F, 0x00F0001F, HDA_RD_F_SD_WRITE_RUN, hdaRegReadU24 , hdaRegWriteSDCTL  , name, CTL  , "Stream Descriptor Control"), \
    /* Offset 0x83 (SD0) */ \
    HDA_REG_ENTRY_STR(offset + 0x3,  0x00001, 0x0000003C, 0x0000001C, HDA_RD_F_SD_WRITE_RUN, hdaRegReadU8  , hdaRegWriteSDSTS  , name, STS  , "Status" ), \
    /* Offset 0x84 (SD0) */ \
    HDA_REG_ENTRY_STR(offset + 0x4,  0x00004, 0xFFFFFFFF, 0x00000000, HDA_RD_F_NONE,         hdaRegReadLPIB, hdaRegWriteU32    , name, LPIB , "Link Position In Buffer" ), \
    /* Offset 0x88 (SD0) */ \
    HDA_REG_ENTRY_STR(offset + 0x8,  0x00004, 0xFFFFFFFF, 0xFFFFFFFF, HDA_RD_F_NONE,         hdaRegReadU32 , hdaRegWriteSDCBL  , name, CBL  , "Cyclic Buffer Length" ), \
    /* Offset 0x8C (SD0) -- upper 8 bits are reserved */ \
    HDA_REG_ENTRY_STR(offset + 0xC,  0x00002, 0x0000FFFF, 0x000000FF, HDA_RD_F_NONE,         hdaRegReadU16 , hdaRegWriteSDLVI  , name, LVI  , "Last Valid Index" ), \
    /* Reserved: FIFO Watermark. ** @todo Document this! */ \
    HDA_REG_ENTRY_STR(offset + 0xE,  0x00002, 0x00000007, 0x00000007, HDA_RD_F_NONE,         hdaRegReadU16 , hdaRegWriteSDFIFOW, name, FIFOW, "FIFO Watermark" ), \
    /* Offset 0x90 (SD0) */ \
    HDA_REG_ENTRY_STR(offset + 0x10, 0x00002, 0x000000FF, 0x000000FF, HDA_RD_F_NONE,         hdaRegReadU16 , hdaRegWriteSDFIFOS, name, FIFOS, "FIFO Size" ), \
    /* Offset 0x92 (SD0) */ \
    HDA_REG_ENTRY_STR(offset + 0x12, 0x00002, 0x00007F7F, 0x00007F7F, HDA_RD_F_NONE,         hdaRegReadU16 , hdaRegWriteSDFMT  , name, FMT  , "Stream Format" ), \
    /* Reserved: 0x94 - 0x98. */ \
    /* Offset 0x98 (SD0) */ \
    HDA_REG_ENTRY_STR(offset + 0x18, 0x00004, 0xFFFFFF80, 0xFFFFFF80, HDA_RD_F_NONE,         hdaRegReadU32 , hdaRegWriteSDBDPL , name, BDPL , "Buffer Descriptor List Pointer-Lower Base Address" ), \
    /* Offset 0x9C (SD0) */ \
    HDA_REG_ENTRY_STR(offset + 0x1C, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, HDA_RD_F_NONE,         hdaRegReadU32 , hdaRegWriteSDBDPU , name, BDPU , "Buffer Descriptor List Pointer-Upper Base Address" )

/** Defines a single audio stream register set (e.g. OSD0). */
#define HDA_REG_MAP_DEF_STREAM(index, name) \
    HDA_REG_MAP_STRM(HDA_REG_DESC_SD0_BASE + (index * 32 /* 0x20 */), name)

/** Skylake stream registers. */
#define HDA_REG_MAP_SKYLAKE_STRM(a_off, a_StrPrefix) \
                   /* offset        size     read mask   write mask  flags          read callback        write callback     index, abbrev, description */ \
                   /* -------       -------  ----------  ----------  -------------- --------------       -----------------  -----------------------------  ----------- */ \
    /* 0x1084 */ \
    HDA_REG_ENTRY_STR(a_off + 0x04, 0x00004, 0xffffffff, 0x00000000, HDA_RD_F_NONE, hdaRegReadSDnPIB,    hdaRegWriteUnimpl, a_StrPrefix, DPIB,   "DMA Position In Buffer" ), \
    /* 0x1094 */ \
    HDA_REG_ENTRY_STR(a_off + 0x14, 0x00004, 0xffffffff, 0x00000000, HDA_RD_F_NONE, hdaRegReadSDnEFIFOS, hdaRegWriteUnimpl, a_StrPrefix, EFIFOS, "Extended FIFO Size" )


/** See 302349 p 6.2. */
static const HDAREGDESC g_aHdaRegMap[HDA_NUM_REGS] =
{
                /* offset  size     read mask   write mask  flags          read callback     write callback       index + abbrev               */
                /*-------  -------  ----------  ----------  -------------- ----------------  -------------------  ------------------------     */
    HDA_REG_ENTRY(0x00000, 0x00002, 0x0000FFFB, 0x00000000, HDA_RD_F_NONE, hdaRegReadU16   , hdaRegWriteUnimpl  , GCAP,        "Global Capabilities" ),
    HDA_REG_ENTRY(0x00002, 0x00001, 0x000000FF, 0x00000000, HDA_RD_F_NONE, hdaRegReadU8    , hdaRegWriteUnimpl  , VMIN,        "Minor Version" ),
    HDA_REG_ENTRY(0x00003, 0x00001, 0x000000FF, 0x00000000, HDA_RD_F_NONE, hdaRegReadU8    , hdaRegWriteUnimpl  , VMAJ,        "Major Version" ),
    HDA_REG_ENTRY(0x00004, 0x00002, 0x0000FFFF, 0x00000000, HDA_RD_F_NONE, hdaRegReadU16   , hdaRegWriteU16     , OUTPAY,      "Output Payload Capabilities" ),
    HDA_REG_ENTRY(0x00006, 0x00002, 0x0000FFFF, 0x00000000, HDA_RD_F_NONE, hdaRegReadU16   , hdaRegWriteUnimpl  , INPAY,       "Input Payload Capabilities" ),
    HDA_REG_ENTRY(0x00008, 0x00004, 0x00000103, 0x00000103, HDA_RD_F_NONE, hdaRegReadU32   , hdaRegWriteGCTL    , GCTL,        "Global Control" ),
    HDA_REG_ENTRY(0x0000c, 0x00002, 0x00007FFF, 0x00007FFF, HDA_RD_F_NONE, hdaRegReadU16   , hdaRegWriteU16     , WAKEEN,      "Wake Enable" ),
    HDA_REG_ENTRY(0x0000e, 0x00002, 0x00000007, 0x00000007, HDA_RD_F_NONE, hdaRegReadU8    , hdaRegWriteSTATESTS, STATESTS,    "State Change Status" ),
    HDA_REG_ENTRY(0x00010, 0x00002, 0xFFFFFFFF, 0x00000000, HDA_RD_F_NONE, hdaRegReadUnimpl, hdaRegWriteUnimpl  , GSTS,        "Global Status" ),
    HDA_REG_ENTRY(0x00014, 0x00002, 0xFFFFFFFF, 0x00000000, HDA_RD_F_NONE, hdaRegReadU16   , hdaRegWriteUnimpl  , LLCH,        "Linked List Capabilities Header" ),
    HDA_REG_ENTRY(0x00018, 0x00002, 0x0000FFFF, 0x00000000, HDA_RD_F_NONE, hdaRegReadU16   , hdaRegWriteU16     , OUTSTRMPAY,  "Output Stream Payload Capability" ),
    HDA_REG_ENTRY(0x0001A, 0x00002, 0x0000FFFF, 0x00000000, HDA_RD_F_NONE, hdaRegReadU16   , hdaRegWriteUnimpl  , INSTRMPAY,   "Input Stream Payload Capability" ),
    HDA_REG_ENTRY(0x00020, 0x00004, 0xC00000FF, 0xC00000FF, HDA_RD_F_NONE, hdaRegReadU32   , hdaRegWriteU32     , INTCTL,      "Interrupt Control" ),
    HDA_REG_ENTRY(0x00024, 0x00004, 0xC00000FF, 0x00000000, HDA_RD_F_NONE, hdaRegReadU32   , hdaRegWriteUnimpl  , INTSTS,      "Interrupt Status" ),
 HDA_REG_ENTRY_EX(0x00030, 0x00004, 0xFFFFFFFF, 0x00000000, HDA_RD_F_NONE, hdaRegReadWALCLK, hdaRegWriteUnimpl  , 0, "WALCLK", "Wall Clock Counter" ),
    HDA_REG_ENTRY(0x00034, 0x00004, 0x000000FF, 0x000000FF, HDA_RD_F_NONE, hdaRegReadU32   , hdaRegWriteSSYNC   , SSYNC,       "Stream Synchronization (old)" ),
    HDA_REG_ENTRY(0x00038, 0x00004, 0x000000FF, 0x000000FF, HDA_RD_F_NONE, hdaRegReadU32   , hdaRegWriteNewSSYNC, SSYNC,       "Stream Synchronization (new)" ),
    HDA_REG_ENTRY(0x00040, 0x00004, 0xFFFFFF80, 0xFFFFFF80, HDA_RD_F_NONE, hdaRegReadU32   , hdaRegWriteBase    , CORBLBASE,   "CORB Lower Base Address" ),
    HDA_REG_ENTRY(0x00044, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, HDA_RD_F_NONE, hdaRegReadU32   , hdaRegWriteBase    , CORBUBASE,   "CORB Upper Base Address" ),
    HDA_REG_ENTRY(0x00048, 0x00002, 0x000000FF, 0x000000FF, HDA_RD_F_NONE, hdaRegReadU16   , hdaRegWriteCORBWP  , CORBWP,      "CORB Write Pointer" ),
    HDA_REG_ENTRY(0x0004A, 0x00002, 0x000080FF, 0x00008000, HDA_RD_F_NONE, hdaRegReadU16   , hdaRegWriteCORBRP  , CORBRP,      "CORB Read Pointer" ),
    HDA_REG_ENTRY(0x0004C, 0x00001, 0x00000003, 0x00000003, HDA_RD_F_NONE, hdaRegReadU8    , hdaRegWriteCORBCTL , CORBCTL,     "CORB Control" ),
    HDA_REG_ENTRY(0x0004D, 0x00001, 0x00000001, 0x00000001, HDA_RD_F_NONE, hdaRegReadU8    , hdaRegWriteCORBSTS , CORBSTS,     "CORB Status" ),
    HDA_REG_ENTRY(0x0004E, 0x00001, 0x000000F3, 0x00000003, HDA_RD_F_NONE, hdaRegReadU8    , hdaRegWriteCORBSIZE, CORBSIZE,    "CORB Size" ),
    HDA_REG_ENTRY(0x00050, 0x00004, 0xFFFFFF80, 0xFFFFFF80, HDA_RD_F_NONE, hdaRegReadU32   , hdaRegWriteBase    , RIRBLBASE,   "RIRB Lower Base Address" ),
    HDA_REG_ENTRY(0x00054, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, HDA_RD_F_NONE, hdaRegReadU32   , hdaRegWriteBase    , RIRBUBASE,   "RIRB Upper Base Address" ),
    HDA_REG_ENTRY(0x00058, 0x00002, 0x000000FF, 0x00008000, HDA_RD_F_NONE, hdaRegReadU8    , hdaRegWriteRIRBWP  , RIRBWP,      "RIRB Write Pointer" ),
    HDA_REG_ENTRY(0x0005A, 0x00002, 0x000000FF, 0x000000FF, HDA_RD_F_NONE, hdaRegReadU16   , hdaRegWriteRINTCNT , RINTCNT,     "Response Interrupt Count" ),
    HDA_REG_ENTRY(0x0005C, 0x00001, 0x00000007, 0x00000007, HDA_RD_F_NONE, hdaRegReadU8    , hdaRegWriteU8      , RIRBCTL,     "RIRB Control" ),
    HDA_REG_ENTRY(0x0005D, 0x00001, 0x00000005, 0x00000005, HDA_RD_F_NONE, hdaRegReadU8    , hdaRegWriteRIRBSTS , RIRBSTS,     "RIRB Status" ),
    HDA_REG_ENTRY(0x0005E, 0x00001, 0x000000F3, 0x00000000, HDA_RD_F_NONE, hdaRegReadU8    , hdaRegWriteUnimpl  , RIRBSIZE,    "RIRB Size" ),
    HDA_REG_ENTRY(0x00060, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, HDA_RD_F_NONE, hdaRegReadU32   , hdaRegWriteU32     , IC,          "Immediate Command" ),
    HDA_REG_ENTRY(0x00064, 0x00004, 0x00000000, 0xFFFFFFFF, HDA_RD_F_NONE, hdaRegReadU32   , hdaRegWriteUnimpl  , IR,          "Immediate Response" ),
    HDA_REG_ENTRY(0x00068, 0x00002, 0x00000002, 0x00000002, HDA_RD_F_NONE, hdaRegReadIRS   , hdaRegWriteIRS     , IRS,         "Immediate Command Status" ),
    HDA_REG_ENTRY(0x00070, 0x00004, 0xFFFFFFFF, 0xFFFFFF81, HDA_RD_F_NONE, hdaRegReadU32   , hdaRegWriteBase    , DPLBASE,     "DMA Position Lower Base" ),
    HDA_REG_ENTRY(0x00074, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, HDA_RD_F_NONE, hdaRegReadU32   , hdaRegWriteBase    , DPUBASE,     "DMA Position Upper Base" ),
    /* 4 Serial Data In (SDI). */
    HDA_REG_MAP_DEF_STREAM(0, SD0),
    HDA_REG_MAP_DEF_STREAM(1, SD1),
    HDA_REG_MAP_DEF_STREAM(2, SD2),
    HDA_REG_MAP_DEF_STREAM(3, SD3),
    /* 4 Serial Data Out (SDO). */
    HDA_REG_MAP_DEF_STREAM(4, SD4),
    HDA_REG_MAP_DEF_STREAM(5, SD5),
    HDA_REG_MAP_DEF_STREAM(6, SD6),
    HDA_REG_MAP_DEF_STREAM(7, SD7),
    HDA_REG_ENTRY(0x00c00, 0x00004, 0xFFFFFFFF, 0x00000000, HDA_RD_F_NONE, hdaRegReadU32   , hdaRegWriteUnimpl  , MLCH,        "Multiple Links Capability Header" ),
    HDA_REG_ENTRY(0x00c04, 0x00004, 0xFFFFFFFF, 0x00000000, HDA_RD_F_NONE, hdaRegReadU32   , hdaRegWriteUnimpl  , MLCD,        "Multiple Links Capability Declaration" ),
    HDA_REG_MAP_SKYLAKE_STRM(0x01080, SD0),
    HDA_REG_MAP_SKYLAKE_STRM(0x010a0, SD1),
    HDA_REG_MAP_SKYLAKE_STRM(0x010c0, SD2),
    HDA_REG_MAP_SKYLAKE_STRM(0x010e0, SD3),
    HDA_REG_MAP_SKYLAKE_STRM(0x01100, SD4),
    HDA_REG_MAP_SKYLAKE_STRM(0x01120, SD5),
    HDA_REG_MAP_SKYLAKE_STRM(0x01140, SD6),
    HDA_REG_MAP_SKYLAKE_STRM(0x01160, SD7),
};

#undef HDA_REG_ENTRY_EX
#undef HDA_REG_ENTRY
#undef HDA_REG_ENTRY_STR
#undef HDA_REG_MAP_STRM
#undef HDA_REG_MAP_DEF_STREAM

/**
 * HDA register aliases (HDA spec 3.3.45).
 * @remarks Sorted by offReg.
 * @remarks Lookup code ASSUMES this starts somewhere after g_aHdaRegMap ends.
 */
static struct HDAREGALIAS
{
    /** The alias register offset. */
    uint32_t    offReg;
    /** The register index. */
    int         idxAlias;
} const g_aHdaRegAliases[] =
{
    { 0x2030, HDA_REG_WALCLK  },
    { 0x2084, HDA_REG_SD0LPIB },
    { 0x20a4, HDA_REG_SD1LPIB },
    { 0x20c4, HDA_REG_SD2LPIB },
    { 0x20e4, HDA_REG_SD3LPIB },
    { 0x2104, HDA_REG_SD4LPIB },
    { 0x2124, HDA_REG_SD5LPIB },
    { 0x2144, HDA_REG_SD6LPIB },
    { 0x2164, HDA_REG_SD7LPIB }
};

#ifdef IN_RING3

/** HDABDLEDESC field descriptors for the v7+ saved state. */
static SSMFIELD const g_aSSMBDLEDescFields7[] =
{
    SSMFIELD_ENTRY(HDABDLEDESC, u64BufAddr),
    SSMFIELD_ENTRY(HDABDLEDESC, u32BufSize),
    SSMFIELD_ENTRY(HDABDLEDESC, fFlags),
    SSMFIELD_ENTRY_TERM()
};

/** HDABDLEDESC field descriptors for the v6 saved states. */
static SSMFIELD const g_aSSMBDLEDescFields6[] =
{
    SSMFIELD_ENTRY(HDABDLEDESC, u64BufAddr),
    SSMFIELD_ENTRY(HDABDLEDESC, u32BufSize),
    SSMFIELD_ENTRY_CALLBACK(HDABDLEDESC, fFlags, hdaR3GetPutTrans_HDABDLEDESC_fFlags_6),
    SSMFIELD_ENTRY_TERM()
};

/** HDABDLESTATE field descriptors for the v6 saved state. */
static SSMFIELD const g_aSSMBDLEStateFields6[] =
{
    SSMFIELD_ENTRY(HDABDLESTATELEGACY, u32BDLIndex),
    SSMFIELD_ENTRY(HDABDLESTATELEGACY, cbBelowFIFOW),
    SSMFIELD_ENTRY_OLD(FIFO,           256),            /* Deprecated; now is handled in the stream's circular buffer. */
    SSMFIELD_ENTRY(HDABDLESTATELEGACY, u32BufOff),
    SSMFIELD_ENTRY_TERM()
};

/** HDABDLESTATE field descriptors for the v7+ saved state. */
static SSMFIELD const g_aSSMBDLEStateFields7[] =
{
    SSMFIELD_ENTRY(HDABDLESTATELEGACY, u32BDLIndex),
    SSMFIELD_ENTRY(HDABDLESTATELEGACY, cbBelowFIFOW),
    SSMFIELD_ENTRY(HDABDLESTATELEGACY, u32BufOff),
    SSMFIELD_ENTRY_TERM()
};

/** HDASTREAMSTATE field descriptors for the v6 saved state. */
static SSMFIELD const g_aSSMStreamStateFields6[] =
{
    SSMFIELD_ENTRY_OLD(cBDLE,      sizeof(uint16_t)), /* Deprecated. */
    SSMFIELD_ENTRY_OLD(uCurBDLE,   sizeof(uint16_t)), /* We figure it out from LPID */
    SSMFIELD_ENTRY_OLD(fStop,      1),                /* Deprecated; see SSMR3PutBool(). */
    SSMFIELD_ENTRY_OLD(fRunning,   1),                /* Deprecated; using the HDA_SDCTL_RUN bit is sufficient. */
    SSMFIELD_ENTRY(HDASTREAMSTATE, fInReset),
    SSMFIELD_ENTRY_TERM()
};

/** HDASTREAMSTATE field descriptors for the v7+ saved state. */
static SSMFIELD const g_aSSMStreamStateFields7[] =
{
    SSMFIELD_ENTRY(HDASTREAMSTATE, idxCurBdle),         /* For backward compatibility we save this. We use LPIB on restore. */
    SSMFIELD_ENTRY_OLD(uCurBDLEHi, sizeof(uint8_t)),    /* uCurBDLE was 16-bit for some reason, so store/ignore the zero top byte. */
    SSMFIELD_ENTRY(HDASTREAMSTATE, fInReset),
    SSMFIELD_ENTRY(HDASTREAMSTATE, tsTransferNext),
    SSMFIELD_ENTRY_TERM()
};

/** HDABDLE field descriptors for the v1 thru v4 saved states. */
static SSMFIELD const g_aSSMStreamBdleFields1234[] =
{
    SSMFIELD_ENTRY(HDABDLELEGACY,           Desc.u64BufAddr),       /* u64BdleCviAddr */
    SSMFIELD_ENTRY_OLD(u32BdleMaxCvi,       sizeof(uint32_t)),      /* u32BdleMaxCvi */
    SSMFIELD_ENTRY(HDABDLELEGACY,           State.u32BDLIndex),     /* u32BdleCvi */
    SSMFIELD_ENTRY(HDABDLELEGACY,           Desc.u32BufSize),       /* u32BdleCviLen */
    SSMFIELD_ENTRY(HDABDLELEGACY,           State.u32BufOff),       /* u32BdleCviPos */
    SSMFIELD_ENTRY_CALLBACK(HDABDLELEGACY,  Desc.fFlags, hdaR3GetPutTrans_HDABDLE_Desc_fFlags_1thru4), /* fBdleCviIoc */
    SSMFIELD_ENTRY(HDABDLELEGACY,           State.cbBelowFIFOW),    /* cbUnderFifoW */
    SSMFIELD_ENTRY_OLD(au8FIFO,             256),                   /* au8FIFO */
    SSMFIELD_ENTRY_TERM()
};

#endif /* IN_RING3 */

/**
 * 32-bit size indexed masks, i.e. g_afMasks[2 bytes] = 0xffff.
 */
static uint32_t const g_afMasks[5] =
{
    UINT32_C(0), UINT32_C(0x000000ff), UINT32_C(0x0000ffff), UINT32_C(0x00ffffff), UINT32_C(0xffffffff)
};


#ifdef VBOX_STRICT

/**
 * Strict register accessor verifing defines and mapping table.
 * @see HDA_REG
 */
DECLINLINE(uint32_t *) hdaStrictRegAccessor(PHDASTATE pThis, uint32_t idxMap, uint32_t idxReg)
{
    Assert(idxMap < RT_ELEMENTS(g_aHdaRegMap));
    AssertMsg(idxReg == g_aHdaRegMap[idxMap].idxReg, ("idxReg=%d\n", idxReg));
    return &pThis->au32Regs[idxReg];
}

/**
 * Strict stream register accessor verifing defines and mapping table.
 * @see HDA_STREAM_REG
 */
DECLINLINE(uint32_t *) hdaStrictStreamRegAccessor(PHDASTATE pThis, uint32_t idxMap0, uint32_t idxReg0, size_t idxStream)
{
    Assert(idxMap0 < RT_ELEMENTS(g_aHdaRegMap));
    AssertMsg(idxStream < RT_ELEMENTS(pThis->aStreams), ("%#zx\n", idxStream));
    AssertMsg(idxReg0 + idxStream * 10 == g_aHdaRegMap[idxMap0 + idxStream * 10].idxReg,
              ("idxReg0=%d idxStream=%zx\n", idxReg0, idxStream));
    return &pThis->au32Regs[idxReg0 + idxStream * 10];
}

#endif /* VBOX_STRICT */


/**
 * Returns a new INTSTS value based on the current device state.
 *
 * @returns Determined INTSTS register value.
 * @param   pThis               The shared HDA device state.
 *
 * @remarks This function does *not* set INTSTS!
 */
static uint32_t hdaGetINTSTS(PHDASTATE pThis)
{
    uint32_t intSts = 0;

    /* Check controller interrupts (RIRB, STATEST). */
    if (HDA_REG(pThis, RIRBSTS) & HDA_REG(pThis, RIRBCTL) & (HDA_RIRBCTL_ROIC | HDA_RIRBCTL_RINTCTL))
    {
        intSts |= HDA_INTSTS_CIS; /* Set the Controller Interrupt Status (CIS). */
    }

    /* Check SDIN State Change Status Flags. */
    if (HDA_REG(pThis, STATESTS) & HDA_REG(pThis, WAKEEN))
    {
        intSts |= HDA_INTSTS_CIS; /* Touch Controller Interrupt Status (CIS). */
    }

    /* For each stream, check if any interrupt status bit is set and enabled. */
    for (uint8_t iStrm = 0; iStrm < HDA_MAX_STREAMS; ++iStrm)
    {
        if (HDA_STREAM_REG(pThis, STS, iStrm) & HDA_STREAM_REG(pThis, CTL, iStrm) & (HDA_SDCTL_DEIE | HDA_SDCTL_FEIE  | HDA_SDCTL_IOCE))
        {
            Log3Func(("[SD%d] interrupt status set\n", iStrm));
            intSts |= RT_BIT(iStrm);
        }
    }

    if (intSts)
        intSts |= HDA_INTSTS_GIS; /* Set the Global Interrupt Status (GIS). */

    Log3Func(("-> 0x%x\n", intSts));

    return intSts;
}


/**
 * Processes (asserts/deasserts) the HDA interrupt according to the current state.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared HDA device state.
 * @param   pszSource           Caller information.
 */
#if defined(LOG_ENABLED) || defined(DOXYGEN_RUNNING)
void hdaProcessInterrupt(PPDMDEVINS pDevIns, PHDASTATE pThis, const char *pszSource)
#else
void hdaProcessInterrupt(PPDMDEVINS pDevIns, PHDASTATE pThis)
#endif
{
    uint32_t uIntSts = hdaGetINTSTS(pThis);

    HDA_REG(pThis, INTSTS) = uIntSts;

    /* NB: It is possible to have GIS set even when CIE/SIEn are all zero; the GIS bit does
     * not control the interrupt signal. See Figure 4 on page 54 of the HDA 1.0a spec.
     */
    /* Global Interrupt Enable (GIE) set? */
    if (   (HDA_REG(pThis, INTCTL) & HDA_INTCTL_GIE)
        && (HDA_REG(pThis, INTSTS) & HDA_REG(pThis, INTCTL) & (HDA_INTCTL_CIE | HDA_STRMINT_MASK)))
    {
        Log3Func(("Asserted (%s)\n", pszSource));

        PDMDevHlpPCISetIrq(pDevIns, 0, 1 /* Assert */);
        pThis->u8IRQL = 1;

#ifdef DEBUG
        pThis->Dbg.IRQ.tsAssertedNs = RTTimeNanoTS();
        pThis->Dbg.IRQ.tsProcessedLastNs = pThis->Dbg.IRQ.tsAssertedNs;
#endif
    }
    else
    {
        Log3Func(("Deasserted (%s)\n", pszSource));

        PDMDevHlpPCISetIrq(pDevIns, 0, 0 /* Deassert */);
        pThis->u8IRQL = 0;
    }
}


/**
 * Looks up a register at the exact offset given by @a offReg.
 *
 * @returns Register index on success, -1 if not found.
 * @param   offReg              The register offset.
 */
static int hdaRegLookup(uint32_t offReg)
{
    /*
     * Aliases.
     */
    if (offReg >= g_aHdaRegAliases[0].offReg)
    {
        for (unsigned i = 0; i < RT_ELEMENTS(g_aHdaRegAliases); i++)
            if (offReg == g_aHdaRegAliases[i].offReg)
                return g_aHdaRegAliases[i].idxAlias;
        Assert(g_aHdaRegMap[RT_ELEMENTS(g_aHdaRegMap) - 1].off < offReg);
        return -1;
    }

    /*
     * Binary search the
     */
    int idxEnd  = RT_ELEMENTS(g_aHdaRegMap);
    int idxLow  = 0;
    for (;;)
    {
        int idxMiddle = idxLow + (idxEnd - idxLow) / 2;
        if (offReg < g_aHdaRegMap[idxMiddle].off)
        {
            if (idxLow != idxMiddle)
                idxEnd = idxMiddle;
            else
                break;
        }
        else if (offReg > g_aHdaRegMap[idxMiddle].off)
        {
            idxLow = idxMiddle + 1;
            if (idxLow < idxEnd)
            { /* likely */ }
            else
                break;
        }
        else
            return idxMiddle;
    }

#ifdef RT_STRICT
    for (unsigned i = 0; i < RT_ELEMENTS(g_aHdaRegMap); i++)
        Assert(g_aHdaRegMap[i].off != offReg);
#endif
    return -1;
}

#ifdef IN_RING3
/**
 * Looks up a register covering the offset given by @a offReg.
 *
 * @returns Register index on success, -1 if not found.
 * @param   offReg      The register offset.
 * @param   pcbBefore   Where to return the number of bytes in the matching
 *                      register preceding @a offReg.
 */
static int hdaR3RegLookupWithin(uint32_t offReg, uint32_t *pcbBefore)
{
    /*
     * Aliases.
     *
     * We ASSUME the aliases are for whole registers and that they have the
     * same alignment (release-asserted in the constructor), so we don't need
     * to calculate the within-register-offset twice here.
     */
    if (offReg >= g_aHdaRegAliases[0].offReg)
    {
        for (unsigned i = 0; i < RT_ELEMENTS(g_aHdaRegAliases); i++)
        {
            uint32_t const off = offReg - g_aHdaRegAliases[i].offReg;
            if (off < 4)  /* No register is wider than 4 bytes (release-asserted in constructor). */
            {
                const uint32_t idxAlias = g_aHdaRegAliases[i].idxAlias;
                if (off < g_aHdaRegMap[idxAlias].cb)
                {
                    Assert(off > 0); /* ASSUMES the caller already did a hdaRegLookup which failed. */
                    Assert((g_aHdaRegAliases[i].offReg & 3) == (g_aHdaRegMap[idxAlias].off & 3));
                    *pcbBefore = off;
                    return idxAlias;
                }
            }
        }
        Assert(g_aHdaRegMap[RT_ELEMENTS(g_aHdaRegMap) - 1].off < offReg);
        *pcbBefore = 0;
        return -1;
    }

    /*
     * Binary search the register map.
     */
    int idxEnd  = RT_ELEMENTS(g_aHdaRegMap);
    int idxLow  = 0;
    for (;;)
    {
        int idxMiddle = idxLow + (idxEnd - idxLow) / 2;
        if (offReg < g_aHdaRegMap[idxMiddle].off)
        {
            if (idxLow == idxMiddle)
                break;
            idxEnd = idxMiddle;
        }
        else if (offReg >= g_aHdaRegMap[idxMiddle].off + g_aHdaRegMap[idxMiddle].cb)
        {
            idxLow = idxMiddle + 1;
            if (idxLow >= idxEnd)
                break;
        }
        else
        {
            offReg -= g_aHdaRegMap[idxMiddle].off;
            *pcbBefore = offReg;
            Assert(offReg > 0); /* ASSUMES the caller already did a hdaRegLookup which failed. */
            Assert(g_aHdaRegMap[idxMiddle].cb <= 4); /* This is release-asserted in the constructor. */
            return idxMiddle;
        }
    }

# ifdef RT_STRICT
    for (unsigned i = 0; i < RT_ELEMENTS(g_aHdaRegMap); i++)
        Assert(offReg - g_aHdaRegMap[i].off >= g_aHdaRegMap[i].cb);
# endif
    *pcbBefore = 0;
    return -1;
}
#endif /* IN_RING3 */

#ifdef IN_RING3 /* Codec is not yet kosher enough for ring-0.  @bugref{9890c64} */

/**
 * Synchronizes the CORB / RIRB buffers between internal <-> device state.
 *
 * @returns VBox status code.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared HDA device state.
 * @param   fLocal              Specify true to synchronize HDA state's CORB buffer with the device state,
 *                              or false to synchronize the device state's RIRB buffer with the HDA state.
 *
 * @todo r=andy Break this up into two functions?
 */
static int hdaR3CmdSync(PPDMDEVINS pDevIns, PHDASTATE pThis, bool fLocal)
{
    int rc = VINF_SUCCESS;
    if (fLocal)
    {
        if (pThis->u64CORBBase)
        {
            Assert(pThis->cbCorbBuf);
            rc = PDMDevHlpPCIPhysRead(pDevIns, pThis->u64CORBBase, pThis->au32CorbBuf,
                                      RT_MIN(pThis->cbCorbBuf, sizeof(pThis->au32CorbBuf)));
            Log3Func(("CORB: read %RGp LB %#x (%Rrc)\n", pThis->u64CORBBase, pThis->cbCorbBuf, rc));
            AssertRCReturn(rc, rc);
        }
    }
    else
    {
        if (pThis->u64RIRBBase)
        {
            Assert(pThis->cbRirbBuf);

            rc = PDMDevHlpPCIPhysWrite(pDevIns, pThis->u64RIRBBase, pThis->au64RirbBuf,
                                       RT_MIN(pThis->cbRirbBuf, sizeof(pThis->au64RirbBuf)));
            Log3Func(("RIRB: phys read %RGp LB %#x (%Rrc)\n", pThis->u64RIRBBase, pThis->cbRirbBuf, rc));
            AssertRCReturn(rc, rc);
        }
    }

# ifdef DEBUG_CMD_BUFFER
        LogFunc(("fLocal=%RTbool\n", fLocal));

        uint8_t i = 0;
        do
        {
            LogFunc(("CORB%02x: ", i));
            uint8_t j = 0;
            do
            {
                const char *pszPrefix;
                if ((i + j) == HDA_REG(pThis, CORBRP))
                    pszPrefix = "[R]";
                else if ((i + j) == HDA_REG(pThis, CORBWP))
                    pszPrefix = "[W]";
                else
                    pszPrefix = "   "; /* three spaces */
                Log((" %s%08x", pszPrefix, pThis->pu32CorbBuf[i + j]));
                j++;
            } while (j < 8);
            Log(("\n"));
            i += 8;
        } while (i != 0);

        do
        {
            LogFunc(("RIRB%02x: ", i));
            uint8_t j = 0;
            do
            {
                const char *prefix;
                if ((i + j) == HDA_REG(pThis, RIRBWP))
                    prefix = "[W]";
                else
                    prefix = "   ";
                Log((" %s%016lx", prefix, pThis->pu64RirbBuf[i + j]));
            } while (++j < 8);
            Log(("\n"));
            i += 8;
        } while (i != 0);
# endif
    return rc;
}


/**
 * Processes the next CORB buffer command in the queue.
 *
 * This will invoke the HDA codec ring-3 verb dispatcher.
 *
 * @returns VBox status code.
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared HDA device state.
 * @param   pThisCC             The ring-0 HDA device state.
 */
static int hdaR3CORBCmdProcess(PPDMDEVINS pDevIns, PHDASTATE pThis, PHDASTATECC pThisCC)
{
    Log3Func(("ENTER CORB(RP:%x, WP:%x) RIRBWP:%x\n", HDA_REG(pThis, CORBRP), HDA_REG(pThis, CORBWP), HDA_REG(pThis, RIRBWP)));

    if (!(HDA_REG(pThis, CORBCTL) & HDA_CORBCTL_DMA))
    {
        LogFunc(("CORB DMA not active, skipping\n"));
        return VINF_SUCCESS;
    }

    Assert(pThis->cbCorbBuf);

    int rc = hdaR3CmdSync(pDevIns, pThis, true /* Sync from guest */);
    AssertRCReturn(rc, rc);

    /*
     * Prepare local copies of relevant registers.
     */
    uint16_t cIntCnt = HDA_REG(pThis, RINTCNT) & 0xff;
    if (!cIntCnt) /* 0 means 256 interrupts. */
        cIntCnt = HDA_MAX_RINTCNT;

    uint32_t const cCorbEntries = RT_MIN(RT_MAX(pThis->cbCorbBuf, 1), sizeof(pThis->au32CorbBuf)) / HDA_CORB_ELEMENT_SIZE;
    uint8_t  const corbWp       = HDA_REG(pThis, CORBWP) % cCorbEntries;
    uint8_t        corbRp       = HDA_REG(pThis, CORBRP);
    uint8_t        rirbWp       = HDA_REG(pThis, RIRBWP);

    /*
     * The loop.
     */
    Log3Func(("START CORB(RP:%x, WP:%x) RIRBWP:%x, RINTCNT:%RU8/%RU8\n", corbRp, corbWp, rirbWp, pThis->u16RespIntCnt, cIntCnt));
    while (corbRp != corbWp)
    {
        /* Fetch the command from the CORB. */
        corbRp = (corbRp + 1) /* Advance +1 as the first command(s) are at CORBWP + 1. */ % cCorbEntries;
        uint32_t const uCmd = pThis->au32CorbBuf[corbRp];

        /*
         * Execute the command.
         */
        uint64_t uResp = 0;
        rc = hdaR3CodecLookup(&pThisCC->Codec, HDA_CODEC_CMD(uCmd, 0 /* Codec index */), &uResp);
        if (RT_SUCCESS(rc))
            AssertRCSuccess(rc); /* no informational statuses */
        else
            Log3Func(("Lookup for codec verb %08x failed: %Rrc\n", uCmd, rc));
        Log3Func(("Codec verb %08x -> response %016RX64\n", uCmd, uResp));

        if (   (uResp & CODEC_RESPONSE_UNSOLICITED)
            && !(HDA_REG(pThis, GCTL) & HDA_GCTL_UNSOL))
        {
            LogFunc(("Unexpected unsolicited response.\n"));
            HDA_REG(pThis, CORBRP) = corbRp;
            /** @todo r=andy No RIRB syncing to guest required in that case? */
            /** @todo r=bird: Why isn't RIRBWP updated here.  The response might come
             *        after already processing several commands, can't it?  (When you think
             *        about it, it is bascially the same question as Andy is asking.) */
            return VINF_SUCCESS;
        }

        /*
         * Store the response in the RIRB.
         */
        AssertCompile(HDA_RIRB_SIZE == RT_ELEMENTS(pThis->au64RirbBuf));
        rirbWp = (rirbWp + 1) % HDA_RIRB_SIZE;
        pThis->au64RirbBuf[rirbWp] = uResp;

        /*
         * Send interrupt if needed.
         */
        bool fSendInterrupt = false;
        pThis->u16RespIntCnt++;
        if (pThis->u16RespIntCnt >= cIntCnt) /* Response interrupt count reached? */
        {
            pThis->u16RespIntCnt = 0; /* Reset internal interrupt response counter. */

            Log3Func(("Response interrupt count reached (%RU16)\n", pThis->u16RespIntCnt));
            fSendInterrupt = true;
        }
        else if (corbRp == corbWp) /* Did we reach the end of the current command buffer? */
        {
            Log3Func(("Command buffer empty\n"));
            fSendInterrupt = true;
        }
        if (fSendInterrupt)
        {
            if (HDA_REG(pThis, RIRBCTL) & HDA_RIRBCTL_RINTCTL) /* Response Interrupt Control (RINTCTL) enabled? */
            {
                HDA_REG(pThis, RIRBSTS) |= HDA_RIRBSTS_RINTFL;
                HDA_PROCESS_INTERRUPT(pDevIns, pThis);
            }
        }
    }

    /*
     * Put register locals back.
     */
    Log3Func(("END CORB(RP:%x, WP:%x) RIRBWP:%x, RINTCNT:%RU8/%RU8\n", corbRp, corbWp, rirbWp, pThis->u16RespIntCnt, cIntCnt));
    HDA_REG(pThis, CORBRP) = corbRp;
    HDA_REG(pThis, RIRBWP) = rirbWp;

    /*
     * Write out the response.
     */
    rc = hdaR3CmdSync(pDevIns, pThis, false /* Sync to guest */);
    AssertRC(rc);

    return rc;
}

#endif /* IN_RING3 - @bugref{9890c64} */

#ifdef IN_RING3
/**
 * @callback_method_impl{FNPDMTASKDEV, Continue CORB DMA in ring-3}
 */
static DECLCALLBACK(void) hdaR3CorbDmaTaskWorker(PPDMDEVINS pDevIns, void *pvUser)
{
    PHDASTATE       pThis   = PDMDEVINS_2_DATA(pDevIns, PHDASTATE);
    PHDASTATER3     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PHDASTATER3);
    RT_NOREF(pvUser);
    LogFlowFunc(("\n"));

    DEVHDA_LOCK(pDevIns, pThis);
    hdaR3CORBCmdProcess(pDevIns, pThis, pThisCC);
    DEVHDA_UNLOCK(pDevIns, pThis);

}
#endif /* IN_RING3 */

/* Register access handlers. */

static VBOXSTRICTRC hdaRegReadUnimpl(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis, iReg);
    *pu32Value = 0;
    return VINF_SUCCESS;
}

static VBOXSTRICTRC hdaRegWriteUnimpl(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(pDevIns, pThis, iReg, u32Value);
    return VINF_SUCCESS;
}

/* U8 */
static VBOXSTRICTRC hdaRegReadU8(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t *pu32Value)
{
    Assert(((pThis->au32Regs[g_aHdaRegMap[iReg].idxReg] & g_aHdaRegMap[iReg].fReadableMask) & UINT32_C(0xffffff00)) == 0);
    return hdaRegReadU32(pDevIns, pThis, iReg, pu32Value);
}

static VBOXSTRICTRC hdaRegWriteU8(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
    Assert((u32Value & 0xffffff00) == 0);
    return hdaRegWriteU32(pDevIns, pThis, iReg, u32Value);
}

/* U16 */
static VBOXSTRICTRC hdaRegReadU16(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t *pu32Value)
{
    Assert(((pThis->au32Regs[g_aHdaRegMap[iReg].idxReg] & g_aHdaRegMap[iReg].fReadableMask) & UINT32_C(0xffff0000)) == 0);
    return hdaRegReadU32(pDevIns, pThis, iReg, pu32Value);
}

static VBOXSTRICTRC hdaRegWriteU16(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
    Assert((u32Value & 0xffff0000) == 0);
    return hdaRegWriteU32(pDevIns, pThis, iReg, u32Value);
}

/* U24 */
static VBOXSTRICTRC hdaRegReadU24(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t *pu32Value)
{
    Assert(((pThis->au32Regs[g_aHdaRegMap[iReg].idxReg] & g_aHdaRegMap[iReg].fReadableMask) & UINT32_C(0xff000000)) == 0);
    return hdaRegReadU32(pDevIns, pThis, iReg, pu32Value);
}

#ifdef IN_RING3
static VBOXSTRICTRC hdaRegWriteU24(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
    Assert((u32Value & 0xff000000) == 0);
    return hdaRegWriteU32(pDevIns, pThis, iReg, u32Value);
}
#endif

/* U32 */
static VBOXSTRICTRC hdaRegReadU32(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns);

    uint32_t const iRegMem = g_aHdaRegMap[iReg].idxReg;
    *pu32Value = pThis->au32Regs[iRegMem] & g_aHdaRegMap[iReg].fReadableMask;
    return VINF_SUCCESS;
}

static VBOXSTRICTRC hdaRegWriteU32(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(pDevIns);

    uint32_t const iRegMem = g_aHdaRegMap[iReg].idxReg;
    pThis->au32Regs[iRegMem]  = (u32Value & g_aHdaRegMap[iReg].fWritableMask)
                              | (pThis->au32Regs[iRegMem] & ~g_aHdaRegMap[iReg].fWritableMask);
    return VINF_SUCCESS;
}

static VBOXSTRICTRC hdaRegWriteGCTL(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(pDevIns, iReg);

    if (u32Value & HDA_GCTL_CRST)
    {
        /* Set the CRST bit to indicate that we're leaving reset mode. */
        HDA_REG(pThis, GCTL) |= HDA_GCTL_CRST;
        LogFunc(("Guest leaving HDA reset\n"));
    }
    else
    {
#ifdef IN_RING3
        /* Enter reset state. */
        LogFunc(("Guest entering HDA reset with DMA(RIRB:%s, CORB:%s)\n",
                 HDA_REG(pThis, CORBCTL) & HDA_CORBCTL_DMA ? "on" : "off",
                 HDA_REG(pThis, RIRBCTL) & HDA_RIRBCTL_RDMAEN ? "on" : "off"));

        /* Clear the CRST bit to indicate that we're in reset state. */
        HDA_REG(pThis, GCTL) &= ~HDA_GCTL_CRST;

        hdaR3GCTLReset(pDevIns, pThis, PDMDEVINS_2_DATA_CC(pDevIns,  PHDASTATER3));
#else
        return VINF_IOM_R3_MMIO_WRITE;
#endif
    }

    if (u32Value & HDA_GCTL_FCNTRL)
    {
        /* Flush: GSTS:1 set, see 6.2.6. */
        HDA_REG(pThis, GSTS) |= HDA_GSTS_FSTS;  /* Set the flush status. */
        /* DPLBASE and DPUBASE should be initialized with initial value (see 6.2.6). */
    }

    return VINF_SUCCESS;
}

static VBOXSTRICTRC hdaRegWriteSTATESTS(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(pDevIns);

    uint32_t v  = HDA_REG_IND(pThis, iReg);
    uint32_t nv = u32Value & HDA_STATESTS_SCSF_MASK;

    HDA_REG(pThis, STATESTS) &= ~(v & nv); /* Write of 1 clears corresponding bit. */

    return VINF_SUCCESS;
}

static VBOXSTRICTRC hdaRegReadLPIB(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns);
    uint8_t const  uSD   = HDA_SD_NUM_FROM_REG(pThis, LPIB, iReg);
    uint32_t const uLPIB = HDA_STREAM_REG(pThis, LPIB, uSD);

#ifdef VBOX_HDA_WITH_ON_REG_ACCESS_DMA
    /*
     * Should we consider doing DMA work while we're here?  That would require
     * the stream to have the DMA engine enabled and be an output stream.
     */
    if (   (HDA_STREAM_REG(pThis, CTL, uSD) & HDA_SDCTL_RUN)
        && hdaGetDirFromSD(uSD) == PDMAUDIODIR_OUT
        && uSD < RT_ELEMENTS(pThis->aStreams) /* paranoia */)
    {
        PHDASTREAM const pStreamShared = &pThis->aStreams[uSD];
        Assert(pStreamShared->u8SD == uSD);
        if (pStreamShared->State.fRunning /* should be same as HDA_SDCTL_RUN, but doesn't hurt to check twice */)
        {
            /*
             * Calculate where the DMA engine should be according to the clock, if we can.
             */
            uint32_t const cbFrame  = PDMAudioPropsFrameSize(&pStreamShared->State.Cfg.Props);
            uint32_t const cbPeriod = pStreamShared->State.cbCurDmaPeriod;
            if (cbPeriod > cbFrame)
            {
                AssertMsg(pStreamShared->State.cbDmaTotal < cbPeriod, ("%#x vs %#x\n", pStreamShared->State.cbDmaTotal, cbPeriod));
                uint64_t const tsTransferNext = pStreamShared->State.tsTransferNext;
                uint64_t const tsNow          = PDMDevHlpTimerGet(pDevIns, pThis->aStreams[0].hTimer); /* only #0 works in r0 */
                uint32_t       cbFuture;
                if (tsNow < tsTransferNext)
                {
                    /** @todo ASSUMES nanosecond clock ticks, need to make this
                     *        resolution independent. */
                    cbFuture = PDMAudioPropsNanoToBytes(&pStreamShared->State.Cfg.Props, tsTransferNext - tsNow);
                    cbFuture = RT_MIN(cbFuture, cbPeriod - cbFrame);
                }
                else
                {
                    /* We've hit/overshot the timer deadline.  Return to ring-3 if we're
                       not already there to increase the chance that we'll help expidite
                       the timer.  If we're already in ring-3, do all but the last frame. */
# ifndef IN_RING3
                    LogFunc(("[SD%RU8] DMA period expired: tsNow=%RU64 >= tsTransferNext=%RU64 -> VINF_IOM_R3_MMIO_READ\n",
                             tsNow, tsTransferNext));
                    return VINF_IOM_R3_MMIO_READ;
# else
                    cbFuture = cbPeriod - cbFrame;
                    LogFunc(("[SD%RU8] DMA period expired: tsNow=%RU64 >= tsTransferNext=%RU64 -> cbFuture=%#x (cbPeriod=%#x - cbFrame=%#x)\n",
                             tsNow, tsTransferNext, cbFuture, cbPeriod, cbFrame));
# endif
                }
                uint32_t const offNow = PDMAudioPropsFloorBytesToFrame(&pStreamShared->State.Cfg.Props, cbPeriod - cbFuture);

                /*
                 * Should we transfer a little?  Minimum is 64 bytes (semi-random,
                 * suspect real hardware might be doing some cache aligned stuff,
                 * which might soon get complicated if you take unaligned buffers
                 * into consideration and which cache line size (128 bytes is just
                 * as likely as 64 or 32 bytes)).
                 */
                uint32_t cbDmaTotal = pStreamShared->State.cbDmaTotal;
                if (cbDmaTotal + 64 <= offNow)
                {
                    VBOXSTRICTRC rcStrict = hdaStreamDoOnAccessDmaOutput(pDevIns, pThis, pStreamShared,
                                                                         tsNow, offNow - cbDmaTotal);

                    /* LPIB is updated by hdaStreamDoOnAccessDmaOutput, so get the new value. */
                    uint32_t const uNewLpib = HDA_STREAM_REG(pThis, LPIB, uSD);
                    *pu32Value = uNewLpib;

                    LogFlowFunc(("[SD%RU8] LPIB=%#RX32 (CBL=%#RX32 PrevLPIB=%#x offNow=%#x) rcStrict=%Rrc\n", uSD,
                                 uNewLpib, HDA_STREAM_REG(pThis, CBL, uSD), uLPIB, offNow, VBOXSTRICTRC_VAL(rcStrict) ));
                    return rcStrict;
                }

                /*
                 * Do nothing, just return LPIB as it is.
                 */
                LogFlowFunc(("[SD%RU8] Skipping DMA transfer: cbDmaTotal=%#x offNow=%#x\n", uSD, cbDmaTotal, offNow));
            }
            else
                LogFunc(("[SD%RU8] cbPeriod=%#x <= cbFrame=%#x!!\n", uSD, cbPeriod, cbFrame));
        }
        else
            LogFunc(("[SD%RU8] fRunning=0 SDnCTL=%#x!!\n", uSD, HDA_STREAM_REG(pThis, CTL, uSD) ));
    }
#endif /* VBOX_HDA_WITH_ON_REG_ACCESS_DMA */

    LogFlowFunc(("[SD%RU8] LPIB=%#RX32 (CBL=%#RX32 CTL=%#RX32)\n",
                 uSD, uLPIB, HDA_STREAM_REG(pThis, CBL, uSD), HDA_STREAM_REG(pThis, CTL, uSD) ));
    *pu32Value = uLPIB;
    return VINF_SUCCESS;
}

/**
 * Gets the wall clock.
 *
 * Used by hdaRegReadWALCLK() and 'info hda'.
 *
 * @returns Strict VBox status code if @a fDoDma is @c true, otherwise
 *          VINF_SUCCESS.
 * @param   pDevIns     The device instance.
 * @param   pThis       The shared HDA device state.
 * @param   fDoDma      Whether to consider doing DMA work or not.
 * @param   puWallNow   Where to return the current wall clock time.
 */
static VBOXSTRICTRC hdaQueryWallClock(PPDMDEVINS pDevIns, PHDASTATE pThis, bool fDoDma, uint64_t *puWallNow)
{
    /*
     * The wall clock is calculated from the virtual sync clock.  Since
     * the clock is supposed to reset to zero on controller reset, a
     * start offset is subtracted.
     *
     * In addition, we hold the clock back when there are active DMA engines
     * so that the guest won't conclude we've gotten further in the buffer
     * processing than what we really have. (We generally read a whole buffer
     * at once when the IOC is due, so we're a lot later than what real
     * hardware would be in reading/writing the buffers.)
     *
     * Here are some old notes from the DMA engine that might be useful even
     * if a little dated:
     *
     * Note 1) Only certain guests (like Linux' snd_hda_intel) rely on the WALCLK register
     *         in order to determine the correct timing of the sound device. Other guests
     *         like Windows 7 + 10 (or even more exotic ones like Haiku) will completely
     *         ignore this.
     *
     * Note 2) When updating the WALCLK register too often / early (or even in a non-monotonic
     *         fashion) this *will* upset guest device drivers and will completely fuck up the
     *         sound output. Running VLC on the guest will tell!
     */
    uint64_t const uFreq    = PDMDevHlpTimerGetFreq(pDevIns, pThis->aStreams[0].hTimer);
    Assert(uFreq <= UINT32_MAX);
    uint64_t const tsStart  = 0; /** @todo pThis->tsWallClkStart (as it is reset on controller reset) */
    uint64_t const tsNow    = PDMDevHlpTimerGet(pDevIns, pThis->aStreams[0].hTimer);

    /* Find the oldest DMA transfer timestamp from the active streams. */
    int            iDmaNow  = -1;
    uint64_t       tsDmaNow = tsNow;
    for (size_t i = 0; i < RT_ELEMENTS(pThis->aStreams); i++)
        if (pThis->aStreams[i].State.fRunning)
        {
#ifdef VBOX_HDA_WITH_ON_REG_ACCESS_DMA
            /* Linux is reading WALCLK before one of the DMA position reads and
               we've already got the current time from TM, so check if we should
               do a little bit of DMA'ing here to help WALCLK ahead. */
            if (fDoDma)
            {
                if (hdaGetDirFromSD((uint8_t)i) == PDMAUDIODIR_OUT)
                {
                    VBOXSTRICTRC rcStrict = hdaStreamMaybeDoOnAccessDmaOutput(pDevIns, pThis, &pThis->aStreams[i], tsNow);
                    if (rcStrict == VINF_SUCCESS)
                    { /* likely */ }
                    else
                        return rcStrict;
                }
            }
#endif

            if (   pThis->aStreams[i].State.tsTransferLast < tsDmaNow
                && pThis->aStreams[i].State.tsTransferLast > tsStart)
            {
                tsDmaNow = pThis->aStreams[i].State.tsTransferLast;
                iDmaNow  = (int)i;
            }
        }

    /* Convert it to wall clock ticks. */
    uint64_t const uWallClkNow = ASMMultU64ByU32DivByU32(tsDmaNow - tsStart,
                                                         24000000 /*Wall clock frequency */,
                                                         uFreq);
    Log3Func(("Returning %#RX64 - tsNow=%#RX64 tsDmaNow=%#RX64 (%d) -> %#RX64\n",
              uWallClkNow, tsNow, tsDmaNow, iDmaNow, tsNow - tsDmaNow));
    RT_NOREF(iDmaNow, fDoDma);
    *puWallNow = uWallClkNow;
    return VINF_SUCCESS;
}

static VBOXSTRICTRC hdaRegReadWALCLK(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t *pu32Value)
{
    uint64_t     uWallNow = 0;
    VBOXSTRICTRC rcStrict = hdaQueryWallClock(pDevIns, pThis, true /*fDoDma*/, &uWallNow);
    if (rcStrict == VINF_SUCCESS)
    {
        *pu32Value = (uint32_t)uWallNow;
        return VINF_SUCCESS;
    }
    RT_NOREF(iReg);
    return rcStrict;
}

static VBOXSTRICTRC hdaRegWriteSSYNCWorker(PHDASTATE pThis, uint32_t iReg, uint32_t u32Value, const char *pszCaller)
{
    RT_NOREF(pszCaller);

    /*
     * The SSYNC register is a DMA pause mask where each bit represents a stream.
     * There should be no DMA transfers going down the driver chains when the a
     * stream has its bit set here.  There are two scenarios described in the
     * specification, starting and stopping, though it can probably be used for
     * other purposes if the guest gets creative...
     *
     * Anyway, if we ever want to implement this, we'd be manipulating the DMA
     * timers of the affected streams here, I think.  At least in the start
     * scenario, we would run the first DMA transfers from here.
     */
    uint32_t const fOld     = HDA_REG(pThis, SSYNC);
    uint32_t const fNew     = (u32Value &  g_aHdaRegMap[iReg].fWritableMask)
                            | (fOld     & ~g_aHdaRegMap[iReg].fWritableMask);
    uint32_t const fChanged = (fNew ^ fOld) & (RT_BIT_32(HDA_MAX_STREAMS) - 1);
    if (fChanged)
    {
#if 0 /** @todo implement SSYNC: ndef IN_RING3 */
        Log3(("%s: Going to ring-3 to handle SSYNC change: %#x\n", pszCaller, fChanged));
        return VINF_IOM_R3_MMIO_WRITE;
#else
        for (uint32_t fMask = 1, i = 0; fMask < RT_BIT_32(HDA_MAX_STREAMS); i++, fMask <<= 1)
            if (!(fChanged & fMask))
            { /* nothing */ }
            else if (fNew & fMask)
            {
                Log3(("%Rfn: SSYNC bit %u set\n", pszCaller, i));
                /* See code in SDCTL around hdaR3StreamTimerMain call. */
            }
            else
            {
                Log3(("%Rfn: SSYNC bit %u cleared\n", pszCaller, i));
                /* The next DMA timer callout will not do anything. */
            }
#endif
    }

    HDA_REG(pThis, SSYNC) = fNew;
    return VINF_SUCCESS;
}

static VBOXSTRICTRC hdaRegWriteSSYNC(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(pDevIns);
    return hdaRegWriteSSYNCWorker(pThis, iReg, u32Value, __FUNCTION__);
}

static VBOXSTRICTRC hdaRegWriteNewSSYNC(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(pDevIns);
    return hdaRegWriteSSYNCWorker(pThis, iReg, u32Value, __FUNCTION__);
}

static VBOXSTRICTRC hdaRegWriteCORBRP(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(pDevIns, iReg);
    if (u32Value & HDA_CORBRP_RST)
    {
        /* Do a CORB reset. */
        if (pThis->cbCorbBuf)
            RT_ZERO(pThis->au32CorbBuf);

        LogRel2(("HDA: CORB reset\n"));
        HDA_REG(pThis, CORBRP) = HDA_CORBRP_RST;    /* Clears the pointer. */
    }
    else
        HDA_REG(pThis, CORBRP) &= ~HDA_CORBRP_RST;  /* Only CORBRP_RST bit is writable. */

    return VINF_SUCCESS;
}

static VBOXSTRICTRC hdaRegWriteCORBCTL(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
    VBOXSTRICTRC rc = hdaRegWriteU8(pDevIns, pThis, iReg, u32Value);
    AssertRCSuccess(VBOXSTRICTRC_VAL(rc));

    if (HDA_REG(pThis, CORBCTL) & HDA_CORBCTL_DMA) /* DMA engine started? */
    {
#ifdef IN_RING3 /** @todo do PDMDevHlpTaskTrigger everywhere? */
        rc = hdaR3CORBCmdProcess(pDevIns, pThis, PDMDEVINS_2_DATA_CC(pDevIns, PHDASTATECC));
#else
        rc = PDMDevHlpTaskTrigger(pDevIns, pThis->hCorbDmaTask);
        if (rc != VINF_SUCCESS && RT_SUCCESS(rc))
            rc = VINF_SUCCESS;
#endif
    }
    else
        LogFunc(("CORB DMA not running, skipping\n"));

    return rc;
}

static VBOXSTRICTRC hdaRegWriteCORBSIZE(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(pDevIns, iReg);

    if (!(HDA_REG(pThis, CORBCTL) & HDA_CORBCTL_DMA)) /* Ignore request if CORB DMA engine is (still) running. */
    {
        u32Value = (u32Value & HDA_CORBSIZE_SZ);

        uint16_t cEntries;
        switch (u32Value)
        {
            case 0: /* 8 byte; 2 entries. */
                cEntries = 2;
                break;
            case 1: /* 64 byte; 16 entries. */
                cEntries = 16;
                break;
            case 2: /* 1 KB; 256 entries. */
                cEntries = HDA_CORB_SIZE; /* default. */
                break;
            default:
                LogRel(("HDA: Guest tried to set an invalid CORB size (0x%x), keeping default\n", u32Value));
                u32Value = 2;
                cEntries = HDA_CORB_SIZE; /* Use default size. */
                break;
        }

        uint32_t cbCorbBuf = cEntries * HDA_CORB_ELEMENT_SIZE;
        Assert(cbCorbBuf <= sizeof(pThis->au32CorbBuf)); /* paranoia */

        if (cbCorbBuf != pThis->cbCorbBuf)
        {
            RT_ZERO(pThis->au32CorbBuf); /* Clear CORB when setting a new size. */
            pThis->cbCorbBuf = cbCorbBuf;
        }

        LogFunc(("CORB buffer size is now %RU32 bytes (%u entries)\n", pThis->cbCorbBuf, pThis->cbCorbBuf / HDA_CORB_ELEMENT_SIZE));

        HDA_REG(pThis, CORBSIZE) = u32Value;
    }
    else
        LogFunc(("CORB DMA is (still) running, skipping\n"));
    return VINF_SUCCESS;
}

static VBOXSTRICTRC hdaRegWriteCORBSTS(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(pDevIns, iReg);

    uint32_t v = HDA_REG(pThis, CORBSTS);
    HDA_REG(pThis, CORBSTS) &= ~(v & u32Value);

    return VINF_SUCCESS;
}

static VBOXSTRICTRC hdaRegWriteCORBWP(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
    VBOXSTRICTRC rc = hdaRegWriteU16(pDevIns, pThis, iReg, u32Value);
    AssertRCSuccess(VBOXSTRICTRC_VAL(rc));

#ifdef IN_RING3 /** @todo do PDMDevHlpTaskTrigger everywhere? */
    return hdaR3CORBCmdProcess(pDevIns, pThis, PDMDEVINS_2_DATA_CC(pDevIns, PHDASTATECC));
#else
    rc = PDMDevHlpTaskTrigger(pDevIns, pThis->hCorbDmaTask);
    return RT_SUCCESS(rc) ? VINF_SUCCESS : rc;
#endif
}

static VBOXSTRICTRC hdaRegWriteSDCBL(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
    return hdaRegWriteU32(pDevIns, pThis, iReg, u32Value);
}

static VBOXSTRICTRC hdaRegWriteSDCTL(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
#ifdef IN_RING3
    /* Get the stream descriptor number. */
    const uint8_t uSD = HDA_SD_NUM_FROM_REG(pThis, CTL, iReg);
    AssertReturn(uSD < RT_ELEMENTS(pThis->aStreams), VERR_INTERNAL_ERROR_3); /* paranoia^2: Bad g_aHdaRegMap. */

    /*
     * Extract the stream tag the guest wants to use for this specific
     * stream descriptor (SDn). This only can happen if the stream is in a non-running
     * state, so we're doing the lookup and assignment here.
     *
     * So depending on the guest OS, SD3 can use stream tag 4, for example.
     */
    PHDASTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PHDASTATER3);
    uint8_t     uTag    = (u32Value >> HDA_SDCTL_NUM_SHIFT) & HDA_SDCTL_NUM_MASK;
    ASSERT_GUEST_MSG_RETURN(uTag < RT_ELEMENTS(pThisCC->aTags),
                            ("SD%RU8: Invalid stream tag %RU8 (u32Value=%#x)!\n", uSD, uTag, u32Value),
                            VINF_SUCCESS /* Always return success to the MMIO handler. */);

    PHDASTREAM   const pStreamShared = &pThis->aStreams[uSD];
    PHDASTREAMR3 const pStreamR3     = &pThisCC->aStreams[uSD];

    const bool fRun      = RT_BOOL(u32Value & HDA_SDCTL_RUN);
    const bool fReset    = RT_BOOL(u32Value & HDA_SDCTL_SRST);

    /* If the run bit is set, we take the virtual-sync clock lock as well so we
       can safely update timers via hdaR3TimerSet if necessary.   We need to be
       very careful with the fInReset and fInRun indicators here, as they may
       change during the relocking if we need to acquire the clock lock. */
    const bool fNeedVirtualSyncClockLock = (u32Value & (HDA_SDCTL_RUN | HDA_SDCTL_SRST)) == HDA_SDCTL_RUN
                                        && (HDA_REG_IND(pThis, iReg) & HDA_SDCTL_RUN) == 0;
    if (fNeedVirtualSyncClockLock)
    {
        DEVHDA_UNLOCK(pDevIns, pThis);
        DEVHDA_LOCK_BOTH_RETURN(pDevIns, pThis, pStreamShared, VINF_IOM_R3_MMIO_WRITE);
    }

    const bool fInRun    = RT_BOOL(HDA_REG_IND(pThis, iReg) & HDA_SDCTL_RUN);
    const bool fInReset  = RT_BOOL(HDA_REG_IND(pThis, iReg) & HDA_SDCTL_SRST);

    /*LogFunc(("[SD%RU8] fRun=%RTbool, fInRun=%RTbool, fReset=%RTbool, fInReset=%RTbool, %R[sdctl]\n",
               uSD, fRun, fInRun, fReset, fInReset, u32Value));*/
    if (fInReset)
    {
        ASSERT_GUEST(!fReset);
        ASSERT_GUEST(!fInRun && !fRun);

        /* Exit reset state. */
        ASMAtomicXchgBool(&pStreamShared->State.fInReset, false);

        /* Report that we're done resetting this stream by clearing SRST. */
        HDA_STREAM_REG(pThis, CTL, uSD) &= ~HDA_SDCTL_SRST;

        LogFunc(("[SD%RU8] Reset exit\n", uSD));
    }
    else if (fReset)
    {
        /* ICH6 datasheet 18.2.33 says that RUN bit should be cleared before initiation of reset. */
        ASSERT_GUEST(!fInRun && !fRun);

        LogFunc(("[SD%RU8] Reset enter\n", uSD));

        STAM_REL_PROFILE_START_NS(&pStreamR3->State.StatReset, a);
        Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
        PAUDMIXSINK const pMixSink = pStreamR3->pMixSink ? pStreamR3->pMixSink->pMixSink : NULL;
        if (pMixSink)
            AudioMixerSinkLock(pMixSink);

        /* Deal with reset while running. */
        if (pStreamShared->State.fRunning)
        {
            int rc2 = hdaR3StreamEnable(pThis, pStreamShared, pStreamR3, false /* fEnable */);
            AssertRC(rc2); Assert(!pStreamShared->State.fRunning);
            pStreamShared->State.fRunning = false;
        }

        hdaR3StreamReset(pThis, pThisCC, pStreamShared, pStreamR3, uSD);

        if (pMixSink) /* (FYI. pMixSink might not be what pStreamR3->pMixSink->pMixSink points at any longer) */
            AudioMixerSinkUnlock(pMixSink);
        STAM_REL_PROFILE_STOP_NS(&pStreamR3->State.StatReset, a);
    }
    else
    {
        /*
         * We enter here to change DMA states only.
         */
        if (fInRun != fRun)
        {
            STAM_REL_PROFILE_START_NS((fRun ? &pStreamR3->State.StatStart : &pStreamR3->State.StatStop), r);
            Assert(!fReset && !fInReset); /* (code change paranoia, currently impossible ) */
            LogFunc(("[SD%RU8] State changed (fRun=%RTbool)\n", uSD, fRun));

            Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
            /** @todo bird: It's not clear to me when the pMixSink is actually
             *        assigned to the stream, so being paranoid till I find out... */
            PAUDMIXSINK const pMixSink = pStreamR3->pMixSink ? pStreamR3->pMixSink->pMixSink : NULL;
            if (pMixSink)
                AudioMixerSinkLock(pMixSink);

            int rc2 = VINF_SUCCESS;
            if (fRun)
            {
                if (hdaGetDirFromSD(uSD) == PDMAUDIODIR_OUT)
                {
                    const uint8_t uStripeCtl = ((u32Value >> HDA_SDCTL_STRIPE_SHIFT) & HDA_SDCTL_STRIPE_MASK) + 1;
                    LogFunc(("[SD%RU8] Using %RU8 SDOs (stripe control)\n", uSD, uStripeCtl));
                    if (uStripeCtl > 1)
                        LogRel2(("HDA: Warning: Striping output over more than one SDO for stream #%RU8 currently is not implemented " \
                                 "(%RU8 SDOs requested)\n", uSD, uStripeCtl));
                }

                /* Assign new values. */
                LogFunc(("[SD%RU8] Using stream tag=%RU8\n", uSD, uTag));
                PHDATAG pTag = &pThisCC->aTags[uTag];
                pTag->uTag      = uTag;
                pTag->pStreamR3 = &pThisCC->aStreams[uSD];

# ifdef LOG_ENABLED
                if (LogIsEnabled())
                {
                    PDMAUDIOPCMPROPS Props = { 0 };
                    rc2 = hdaR3SDFMTToPCMProps(HDA_STREAM_REG(pThis, FMT, uSD), &Props); AssertRC(rc2);
                    LogFunc(("[SD%RU8] %RU32Hz, %RU8bit, %RU8 channel(s)\n",
                             uSD, Props.uHz, PDMAudioPropsSampleBits(&Props), PDMAudioPropsChannels(&Props)));
                }
# endif
                /* (Re-)initialize the stream with current values. */
                rc2 = hdaR3StreamSetUp(pDevIns, pThis, pStreamShared, pStreamR3, uSD);
                if (   RT_SUCCESS(rc2)
                    /* Any vital stream change occurred so that we need to (re-)add the stream to our setup?
                     * Otherwise just skip this, as this costs a lot of performance. */
                    /** @todo r=bird: hdaR3StreamSetUp does not return VINF_NO_CHANGE since r142810. */
                    && rc2 != VINF_NO_CHANGE)
                {
                    /* Remove the old stream from the device setup. */
                    rc2 = hdaR3RemoveStream(pThisCC, &pStreamShared->State.Cfg);
                    AssertRC(rc2);

                    /* Add the stream to the device setup. */
                    rc2 = hdaR3AddStream(pThisCC, &pStreamShared->State.Cfg);
                    AssertRC(rc2);
                }
            }

            if (RT_SUCCESS(rc2))
            {
                /* Enable/disable the stream. */
                rc2 = hdaR3StreamEnable(pThis, pStreamShared, pStreamR3, fRun /* fEnable */);
                AssertRC(rc2);

                if (fRun)
                {
                    /** @todo move this into a HDAStream.cpp function. */
                    uint64_t tsNow;
                    if (hdaGetDirFromSD(uSD) == PDMAUDIODIR_OUT)
                    {
                        /* Output streams: Avoid going through the timer here by calling the stream's timer
                           function directly.  Should speed up starting the stream transfers. */
                        tsNow = hdaR3StreamTimerMain(pDevIns, pThis, pThisCC, pStreamShared, pStreamR3);
                    }
                    else
                    {
                        /* Input streams: Arm the timer and kick the AIO thread. */
                        tsNow = PDMDevHlpTimerGet(pDevIns, pStreamShared->hTimer);
                        pStreamShared->State.tsTransferLast = tsNow; /* for WALCLK */

                        uint64_t tsTransferNext = tsNow + pStreamShared->State.aSchedule[0].cPeriodTicks;
                        pStreamShared->State.tsTransferNext = tsTransferNext; /* legacy */
                        pStreamShared->State.cbCurDmaPeriod = pStreamShared->State.aSchedule[0].cbPeriod;
                        Log3Func(("[SD%RU8] tsTransferNext=%RU64 (in %RU64)\n",
                                  pStreamShared->u8SD, tsTransferNext, tsTransferNext - tsNow));

                        int rc = PDMDevHlpTimerSet(pDevIns, pStreamShared->hTimer, tsTransferNext);
                        AssertRC(rc);

                        /** @todo we should have a delayed AIO thread kick off, really... */
                        if (pStreamR3->pMixSink && pStreamR3->pMixSink->pMixSink)
                            AudioMixerSinkSignalUpdateJob(pStreamR3->pMixSink->pMixSink);
                        else
                            AssertFailed();
                    }
                    hdaR3StreamMarkStarted(pDevIns, pThis, pStreamShared, tsNow);
                }
                else
                    hdaR3StreamMarkStopped(pStreamShared);
            }

            /* Make sure to leave the lock before (eventually) starting the timer. */
            if (pMixSink)
                AudioMixerSinkUnlock(pMixSink);
            STAM_REL_PROFILE_STOP_NS((fRun ? &pStreamR3->State.StatStart : &pStreamR3->State.StatStop), r);
        }
    }

    if (fNeedVirtualSyncClockLock)
        PDMDevHlpTimerUnlockClock(pDevIns, pStreamShared->hTimer); /* Caller will unlock pThis->CritSect. */

    return hdaRegWriteU24(pDevIns, pThis, iReg, u32Value);
#else  /* !IN_RING3 */
    RT_NOREF(pDevIns, pThis, iReg, u32Value);
    return VINF_IOM_R3_MMIO_WRITE;
#endif /* !IN_RING3 */
}

static VBOXSTRICTRC hdaRegWriteSDSTS(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
    uint32_t v = HDA_REG_IND(pThis, iReg);

    /* Clear (zero) FIFOE, DESE and BCIS bits when writing 1 to it (6.2.33). */
    HDA_REG_IND(pThis, iReg) &= ~(u32Value & v);

    HDA_PROCESS_INTERRUPT(pDevIns, pThis);

    return VINF_SUCCESS;
}

static VBOXSTRICTRC hdaRegWriteSDLVI(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
    const size_t idxStream = HDA_SD_NUM_FROM_REG(pThis, LVI, iReg);
    AssertReturn(idxStream < RT_ELEMENTS(pThis->aStreams), VERR_INTERNAL_ERROR_3); /* paranoia^2: Bad g_aHdaRegMap. */

    ASSERT_GUEST_LOGREL_MSG(u32Value <= UINT8_MAX, /* Should be covered by the register write mask, but just to make sure. */
                            ("LVI for stream #%zu must not be bigger than %RU8\n", idxStream, UINT8_MAX - 1));
    return hdaRegWriteU16(pDevIns, pThis, iReg, u32Value);
}

/**
 * Calculates the number of bytes of a FIFOW register.
 *
 * @return Number of bytes of a given FIFOW register.
 * @param  u16RegFIFOW          FIFOW register to convert.
 */
uint8_t hdaSDFIFOWToBytes(uint16_t u16RegFIFOW)
{
    uint32_t cb;
    switch (u16RegFIFOW)
    {
        case HDA_SDFIFOW_8B:  cb = 8;  break;
        case HDA_SDFIFOW_16B: cb = 16; break;
        case HDA_SDFIFOW_32B: cb = 32; break;
        default:
            AssertFailedStmt(cb = 32); /* Paranoia. */
            break;
    }

    Assert(RT_IS_POWER_OF_TWO(cb));
    return cb;
}

static VBOXSTRICTRC hdaRegWriteSDFIFOW(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
    size_t const idxStream = HDA_SD_NUM_FROM_REG(pThis, FIFOW, iReg);
    AssertReturn(idxStream < RT_ELEMENTS(pThis->aStreams), VERR_INTERNAL_ERROR_3); /* paranoia^2: Bad g_aHdaRegMap. */

    if (RT_LIKELY(hdaGetDirFromSD((uint8_t)idxStream) == PDMAUDIODIR_IN)) /* FIFOW for input streams only. */
    { /* likely */ }
    else
    {
#ifndef IN_RING0
        LogRel(("HDA: Warning: Guest tried to write read-only FIFOW to output stream #%RU8, ignoring\n", idxStream));
        return VINF_SUCCESS;
#else
        return VINF_IOM_R3_MMIO_WRITE; /* (Go to ring-3 for release logging.) */
#endif
    }

    uint16_t u16FIFOW = 0;
    switch (u32Value)
    {
        case HDA_SDFIFOW_8B:
        case HDA_SDFIFOW_16B:
        case HDA_SDFIFOW_32B:
            u16FIFOW = RT_LO_U16(u32Value); /* Only bits 2:0 are used; see ICH-6, 18.2.38. */
            break;
        default:
            ASSERT_GUEST_LOGREL_MSG_FAILED(("Guest tried writing unsupported FIFOW (0x%zx) to stream #%RU8, defaulting to 32 bytes\n",
                                            u32Value, idxStream));
            u16FIFOW = HDA_SDFIFOW_32B;
            break;
    }

    pThis->aStreams[idxStream].u8FIFOW = hdaSDFIFOWToBytes(u16FIFOW);
    LogFunc(("[SD%zu] Updating FIFOW to %RU8 bytes\n", idxStream, pThis->aStreams[idxStream].u8FIFOW));
    return hdaRegWriteU16(pDevIns, pThis, iReg, u16FIFOW);
}

/**
 * @note This method could be called for changing value on Output Streams only (ICH6 datasheet 18.2.39).
 */
static VBOXSTRICTRC hdaRegWriteSDFIFOS(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
    uint8_t uSD = HDA_SD_NUM_FROM_REG(pThis, FIFOS, iReg);

    ASSERT_GUEST_LOGREL_MSG_RETURN(hdaGetDirFromSD(uSD) == PDMAUDIODIR_OUT, /* FIFOS for output streams only. */
                                   ("Guest tried writing read-only FIFOS to input stream #%RU8, ignoring\n", uSD),
                                   VINF_SUCCESS);

    uint32_t u32FIFOS;
    switch (u32Value)
    {
        case HDA_SDOFIFO_16B:
        case HDA_SDOFIFO_32B:
        case HDA_SDOFIFO_64B:
        case HDA_SDOFIFO_128B:
        case HDA_SDOFIFO_192B:
        case HDA_SDOFIFO_256B:
            u32FIFOS = u32Value;
            break;

        default:
            ASSERT_GUEST_LOGREL_MSG_FAILED(("Guest tried writing unsupported FIFOS (0x%x) to stream #%RU8, defaulting to 192 bytes\n",
                                            u32Value, uSD));
            u32FIFOS = HDA_SDOFIFO_192B;
            break;
    }

    return hdaRegWriteU16(pDevIns, pThis, iReg, u32FIFOS);
}

#ifdef IN_RING3

/**
 * Adds an audio output stream to the device setup using the given configuration.
 *
 * @returns VBox status code.
 * @param   pThisCC             The ring-3 HDA device state.
 * @param   pCfg                Stream configuration to use for adding a stream.
 */
static int hdaR3AddStreamOut(PHDASTATER3 pThisCC, PPDMAUDIOSTREAMCFG pCfg)
{
    AssertPtrReturn(pCfg,  VERR_INVALID_POINTER);

    AssertReturn(pCfg->enmDir == PDMAUDIODIR_OUT, VERR_INVALID_PARAMETER);

    LogFlowFunc(("Stream=%s\n", pCfg->szName));

    int rc = VINF_SUCCESS;

    bool fUseFront = true; /* Always use front out by default. */
# ifdef VBOX_WITH_AUDIO_HDA_51_SURROUND
    bool fUseRear;
    bool fUseCenter;
    bool fUseLFE;

    fUseRear = fUseCenter = fUseLFE = false;

    /*
     * Use commonly used setups for speaker configurations.
     */

    /** @todo Make the following configurable through mixer API and/or CFGM? */
    switch (PDMAudioPropsChannels(&pCfg->Props))
    {
        case 3:  /* 2.1: Front (Stereo) + LFE. */
        {
            fUseLFE   = true;
            break;
        }

        case 4:  /* Quadrophonic: Front (Stereo) + Rear (Stereo). */
        {
            fUseRear  = true;
            break;
        }

        case 5:  /* 4.1: Front (Stereo) + Rear (Stereo) + LFE. */
        {
            fUseRear  = true;
            fUseLFE   = true;
            break;
        }

        case 6:  /* 5.1: Front (Stereo) + Rear (Stereo) + Center/LFE. */
        {
            fUseRear   = true;
            fUseCenter = true;
            fUseLFE    = true;
            break;
        }

        default: /* Unknown; fall back to 2 front channels (stereo). */
        {
            rc = VERR_NOT_SUPPORTED;
            break;
        }
    }
# endif /* !VBOX_WITH_AUDIO_HDA_51_SURROUND */

    if (rc == VERR_NOT_SUPPORTED)
    {
        LogRel2(("HDA: Warning: Unsupported channel count (%RU8), falling back to stereo channels (2)\n",
                 PDMAudioPropsChannels(&pCfg->Props) ));

        /* Fall back to 2 channels (see below in fUseFront block). */
        rc = VINF_SUCCESS;
    }

    do
    {
        if (RT_FAILURE(rc))
            break;

        if (fUseFront)
        {
            RTStrPrintf(pCfg->szName, RT_ELEMENTS(pCfg->szName), "Front");

            pCfg->enmPath = PDMAUDIOPATH_OUT_FRONT;
            /// @todo PDMAudioPropsSetChannels(&pCfg->Props, 2); ?

            rc = hdaR3CodecAddStream(&pThisCC->Codec, PDMAUDIOMIXERCTL_FRONT, pCfg);
        }

# ifdef VBOX_WITH_AUDIO_HDA_51_SURROUND
        if (   RT_SUCCESS(rc)
            && (fUseCenter || fUseLFE))
        {
            RTStrPrintf(pCfg->szName, RT_ELEMENTS(pCfg->szName), "Center/LFE");

            pCfg->enmPath = PDMAUDIOPATH_OUT_CENTER_LFE;
            PDMAudioPropsSetChannels(&pCfg->Props, fUseCenter && fUseLFE ? 2 : 1);

            rc = hdaR3CodecAddStream(&pThisCC->Codec, PDMAUDIOMIXERCTL_CENTER_LFE, pCfg);
        }

        if (   RT_SUCCESS(rc)
            && fUseRear)
        {
            RTStrPrintf(pCfg->szName, RT_ELEMENTS(pCfg->szName), "Rear");

            pCfg->enmPath = PDMAUDIOPATH_OUT_REAR;
            PDMAudioPropsSetChannels(&pCfg->Props, 2);

            rc = hdaR3CodecAddStream(&pThisCC->Codec, PDMAUDIOMIXERCTL_REAR, pCfg);
        }
# endif /* VBOX_WITH_AUDIO_HDA_51_SURROUND */

    } while (0);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Adds an audio input stream to the device setup using the given configuration.
 *
 * @returns VBox status code.
 * @param   pThisCC             The ring-3 HDA device state.
 * @param   pCfg                Stream configuration to use for adding a stream.
 */
static int hdaR3AddStreamIn(PHDASTATER3 pThisCC, PPDMAUDIOSTREAMCFG pCfg)
{
    AssertPtrReturn(pCfg,  VERR_INVALID_POINTER);

    AssertReturn(pCfg->enmDir == PDMAUDIODIR_IN, VERR_INVALID_PARAMETER);

    LogFlowFunc(("Stream=%s enmPath=%ld\n", pCfg->szName, pCfg->enmPath));

    int rc;
    switch (pCfg->enmPath)
    {
        case PDMAUDIOPATH_IN_LINE:
            rc = hdaR3CodecAddStream(&pThisCC->Codec, PDMAUDIOMIXERCTL_LINE_IN, pCfg);
            break;
# ifdef VBOX_WITH_AUDIO_HDA_MIC_IN
        case PDMAUDIOPATH_IN_MIC:
            rc = hdaR3CodecAddStream(&pThisCC->Codec, PDMAUDIOMIXERCTL_MIC_IN, pCfg);
            break;
# endif
        default:
            rc = VERR_NOT_SUPPORTED;
            break;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Adds an audio stream to the device setup using the given configuration.
 *
 * @returns VBox status code.
 * @param   pThisCC             The ring-3 HDA device state.
 * @param   pCfg                Stream configuration to use for adding a stream.
 */
static int hdaR3AddStream(PHDASTATER3 pThisCC, PPDMAUDIOSTREAMCFG pCfg)
{
    AssertPtrReturn(pCfg,  VERR_INVALID_POINTER);

    LogFlowFuncEnter();

    int rc;
    switch (pCfg->enmDir)
    {
        case PDMAUDIODIR_OUT:
            rc = hdaR3AddStreamOut(pThisCC, pCfg);
            break;

        case PDMAUDIODIR_IN:
            rc = hdaR3AddStreamIn(pThisCC, pCfg);
            break;

        default:
            rc = VERR_NOT_SUPPORTED;
            AssertFailed();
            break;
    }

    LogFlowFunc(("Returning %Rrc\n", rc));

    return rc;
}

/**
 * Removes an audio stream from the device setup using the given configuration.
 *
 * Used by hdaRegWriteSDCTL().
 *
 * @returns VBox status code.
 * @param   pThisCC             The ring-3 HDA device state.
 * @param   pCfg                Stream configuration to use for removing a stream.
 */
static int hdaR3RemoveStream(PHDASTATER3 pThisCC, PPDMAUDIOSTREAMCFG pCfg)
{
    AssertPtrReturn(pCfg,  VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;

    PDMAUDIOMIXERCTL enmMixerCtl = PDMAUDIOMIXERCTL_UNKNOWN;
    switch (pCfg->enmDir)
    {
        case PDMAUDIODIR_IN:
        {
            LogFlowFunc(("Stream=%s enmPath=%d (src)\n", pCfg->szName, pCfg->enmPath));

            switch (pCfg->enmPath)
            {
                case PDMAUDIOPATH_UNKNOWN:  break;
                case PDMAUDIOPATH_IN_LINE:  enmMixerCtl = PDMAUDIOMIXERCTL_LINE_IN; break;
# ifdef VBOX_WITH_AUDIO_HDA_MIC_IN
                case PDMAUDIOPATH_IN_MIC:   enmMixerCtl = PDMAUDIOMIXERCTL_MIC_IN;  break;
# endif
                default:
                    rc = VERR_NOT_SUPPORTED;
                    break;
            }
            break;
        }

        case PDMAUDIODIR_OUT:
        {
            LogFlowFunc(("Stream=%s, enmPath=%d (dst)\n", pCfg->szName, pCfg->enmPath));

            switch (pCfg->enmPath)
            {
                case PDMAUDIOPATH_UNKNOWN:          break;
                case PDMAUDIOPATH_OUT_FRONT:        enmMixerCtl = PDMAUDIOMIXERCTL_FRONT;      break;
# ifdef VBOX_WITH_AUDIO_HDA_51_SURROUND
                case PDMAUDIOPATH_OUT_CENTER_LFE:   enmMixerCtl = PDMAUDIOMIXERCTL_CENTER_LFE; break;
                case PDMAUDIOPATH_OUT_REAR:         enmMixerCtl = PDMAUDIOMIXERCTL_REAR;       break;
# endif
                default:
                    rc = VERR_NOT_SUPPORTED;
                    break;
            }
            break;
        }

        default:
            rc = VERR_NOT_SUPPORTED;
            break;
    }

    if (   RT_SUCCESS(rc)
        && enmMixerCtl != PDMAUDIOMIXERCTL_UNKNOWN)
    {
        rc = hdaR3CodecRemoveStream(&pThisCC->Codec, enmMixerCtl, false /*fImmediate*/);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

#endif /* IN_RING3 */

static VBOXSTRICTRC hdaRegWriteSDFMT(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
#ifdef IN_RING3
    PDMAUDIOPCMPROPS Props;
    int rc2 = hdaR3SDFMTToPCMProps(RT_LO_U16(u32Value), &Props);
    AssertRC(rc2);
    LogFunc(("[SD%RU8] Set to %#x (%RU32Hz, %RU8bit, %RU8 channel(s))\n", HDA_SD_NUM_FROM_REG(pThis, FMT, iReg), u32Value,
             PDMAudioPropsHz(&Props), PDMAudioPropsSampleBits(&Props), PDMAudioPropsChannels(&Props)));

    /*
     * Write the wanted stream format into the register in any case.
     *
     * This is important for e.g. MacOS guests, as those try to initialize streams which are not reported
     * by the device emulation (wants 4 channels, only have 2 channels at the moment).
     *
     * When ignoring those (invalid) formats, this leads to MacOS thinking that the device is malfunctioning
     * and therefore disabling the device completely.
     */
    return hdaRegWriteU16(pDevIns, pThis, iReg, u32Value);
#else
    RT_NOREF(pDevIns, pThis, iReg, u32Value);
    return VINF_IOM_R3_MMIO_WRITE;
#endif
}

/**
 * Worker for writes to the BDPL and BDPU registers.
 */
DECLINLINE(VBOXSTRICTRC) hdaRegWriteSDBDPX(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value, uint8_t uSD)
{
    RT_NOREF(uSD);
    return hdaRegWriteU32(pDevIns, pThis, iReg, u32Value);
}

static VBOXSTRICTRC hdaRegWriteSDBDPL(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
    return hdaRegWriteSDBDPX(pDevIns, pThis, iReg, u32Value, HDA_SD_NUM_FROM_REG(pThis, BDPL, iReg));
}

static VBOXSTRICTRC hdaRegWriteSDBDPU(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
    return hdaRegWriteSDBDPX(pDevIns, pThis, iReg, u32Value, HDA_SD_NUM_FROM_REG(pThis, BDPU, iReg));
}

/** Skylake specific. */
static VBOXSTRICTRC hdaRegReadSDnPIB(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t *pu32Value)
{
    uint8_t const uSD = HDA_SD_NUM_FROM_SKYLAKE_REG(DPIB, iReg);
    LogFlowFunc(("uSD=%u -> SDnLPIB\n", uSD));
    return hdaRegReadLPIB(pDevIns, pThis, HDA_SD_TO_REG(LPIB, uSD), pu32Value);
}

/** Skylake specific. */
static VBOXSTRICTRC hdaRegReadSDnEFIFOS(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t *pu32Value)
{
    /** @todo This is not implemented as I have found no specs yet.  */
    RT_NOREF(pDevIns, pThis, iReg);
    LogFunc(("TODO - need register spec: uSD=%u\n", HDA_SD_NUM_FROM_SKYLAKE_REG(DPIB, iReg)));
    *pu32Value = 256;
    return VINF_SUCCESS;
}


static VBOXSTRICTRC hdaRegReadIRS(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t *pu32Value)
{
    /* regarding 3.4.3 we should mark IRS as busy in case CORB is active */
    if (   HDA_REG(pThis, CORBWP) != HDA_REG(pThis, CORBRP)
        || (HDA_REG(pThis, CORBCTL) & HDA_CORBCTL_DMA))
        HDA_REG(pThis, IRS) = HDA_IRS_ICB;  /* busy */

    return hdaRegReadU32(pDevIns, pThis, iReg, pu32Value);
}

static VBOXSTRICTRC hdaRegWriteIRS(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(pDevIns, iReg);

    /*
     * If the guest set the ICB bit of IRS register, HDA should process the verb in IC register,
     * write the response to IR register, and set the IRV (valid in case of success) bit of IRS register.
     */
    if (   (u32Value & HDA_IRS_ICB)
        && !(HDA_REG(pThis, IRS) & HDA_IRS_ICB))
    {
#ifdef IN_RING3
        uint32_t uCmd = HDA_REG(pThis, IC);

        if (HDA_REG(pThis, CORBWP) != HDA_REG(pThis, CORBRP))
        {
            /*
             * 3.4.3: Defines behavior of immediate Command status register.
             */
            LogRel(("HDA: Guest attempted process immediate verb (%x) with active CORB\n", uCmd));
            return VINF_SUCCESS;
        }

        HDA_REG(pThis, IRS) = HDA_IRS_ICB;  /* busy */

        PHDASTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PHDASTATER3);
        uint64_t    uResp   = 0;
        int rc2 = hdaR3CodecLookup(&pThisCC->Codec, HDA_CODEC_CMD(uCmd, 0 /* LUN */), &uResp);
        if (RT_FAILURE(rc2))
            LogFunc(("Codec lookup failed with rc2=%Rrc\n", rc2));

        HDA_REG(pThis, IR)   = (uint32_t)uResp; /** @todo r=andy Do we need a 64-bit response? */
        HDA_REG(pThis, IRS)  = HDA_IRS_IRV;     /* result is ready  */
        /** @todo r=michaln We just set the IRS value, why are we clearing unset bits? */
        HDA_REG(pThis, IRS) &= ~HDA_IRS_ICB;    /* busy is clear */

        return VINF_SUCCESS;
#else  /* !IN_RING3 */
        return VINF_IOM_R3_MMIO_WRITE;
#endif /* !IN_RING3 */
    }

    /*
     * Once the guest read the response, it should clear the IRV bit of the IRS register.
     */
    HDA_REG(pThis, IRS) &= ~(u32Value & HDA_IRS_IRV);
    return VINF_SUCCESS;
}

static VBOXSTRICTRC hdaRegWriteRIRBWP(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(pDevIns, iReg);

    if (HDA_REG(pThis, CORBCTL) & HDA_CORBCTL_DMA) /* Ignore request if CORB DMA engine is (still) running. */
        LogFunc(("CORB DMA (still) running, skipping\n"));
    else
    {
        if (u32Value & HDA_RIRBWP_RST)
        {
            /* Do a RIRB reset. */
            if (pThis->cbRirbBuf)
                RT_ZERO(pThis->au64RirbBuf);

            LogRel2(("HDA: RIRB reset\n"));

            HDA_REG(pThis, RIRBWP) = 0;
        }
        /* The remaining bits are O, see 6.2.22. */
    }
    return VINF_SUCCESS;
}

static VBOXSTRICTRC hdaRegWriteRINTCNT(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(pDevIns);
    if (HDA_REG(pThis, CORBCTL) & HDA_CORBCTL_DMA) /* Ignore request if CORB DMA engine is (still) running. */
    {
        LogFunc(("CORB DMA is (still) running, skipping\n"));
        return VINF_SUCCESS;
    }

    VBOXSTRICTRC rc = hdaRegWriteU16(pDevIns, pThis, iReg, u32Value);
    AssertRC(VBOXSTRICTRC_VAL(rc));

    /** @todo r=bird: Shouldn't we make sure the HDASTATE::u16RespIntCnt is below
     *        the new RINTCNT value?  Or alterantively, make the DMA look take
     *        this into account instead...  I'll do the later for now. */

    LogFunc(("Response interrupt count is now %RU8\n", HDA_REG(pThis, RINTCNT) & 0xFF));
    return rc;
}

static VBOXSTRICTRC hdaRegWriteBase(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(pDevIns);

    VBOXSTRICTRC rc = hdaRegWriteU32(pDevIns, pThis, iReg, u32Value);
    AssertRCSuccess(VBOXSTRICTRC_VAL(rc));

    uint32_t const iRegMem = g_aHdaRegMap[iReg].idxReg;
    switch (iReg)
    {
        case HDA_REG_CORBLBASE:
            pThis->u64CORBBase &= UINT64_C(0xFFFFFFFF00000000);
            pThis->u64CORBBase |= pThis->au32Regs[iRegMem];
            break;
        case HDA_REG_CORBUBASE:
            pThis->u64CORBBase &= UINT64_C(0x00000000FFFFFFFF);
            pThis->u64CORBBase |= (uint64_t)pThis->au32Regs[iRegMem] << 32;
            break;
        case HDA_REG_RIRBLBASE:
            pThis->u64RIRBBase &= UINT64_C(0xFFFFFFFF00000000);
            pThis->u64RIRBBase |= pThis->au32Regs[iRegMem];
            break;
        case HDA_REG_RIRBUBASE:
            pThis->u64RIRBBase &= UINT64_C(0x00000000FFFFFFFF);
            pThis->u64RIRBBase |= (uint64_t)pThis->au32Regs[iRegMem] << 32;
            break;
        case HDA_REG_DPLBASE:
            pThis->u64DPBase = pThis->au32Regs[iRegMem] & DPBASE_ADDR_MASK;
            Assert(pThis->u64DPBase % 128 == 0); /* Must be 128-byte aligned. */

            /* Also make sure to handle the DMA position enable bit. */
            pThis->fDMAPosition = pThis->au32Regs[iRegMem] & RT_BIT_32(0);

#ifndef IN_RING0
            LogRel(("HDA: DP base (lower) set: %#RGp\n", pThis->u64DPBase));
            LogRel(("HDA: DMA position buffer is %s\n", pThis->fDMAPosition ? "enabled" : "disabled"));
#else
            return VINF_IOM_R3_MMIO_WRITE; /* (Go to ring-3 for release logging.) */
#endif
            break;
        case HDA_REG_DPUBASE:
            pThis->u64DPBase = RT_MAKE_U64(RT_LO_U32(pThis->u64DPBase) & DPBASE_ADDR_MASK, pThis->au32Regs[iRegMem]);
#ifndef IN_RING0
            LogRel(("HDA: DP base (upper) set: %#RGp\n", pThis->u64DPBase));
#else
            return VINF_IOM_R3_MMIO_WRITE; /* (Go to ring-3 for release logging.) */
#endif
            break;
        default:
            AssertMsgFailed(("Invalid index\n"));
            break;
    }

    LogFunc(("CORB base:%llx RIRB base: %llx DP base: %llx\n",
             pThis->u64CORBBase, pThis->u64RIRBBase, pThis->u64DPBase));
    return rc;
}

static VBOXSTRICTRC hdaRegWriteRIRBSTS(PPDMDEVINS pDevIns, PHDASTATE pThis, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(pDevIns, iReg);

    uint8_t v = HDA_REG(pThis, RIRBSTS);
    HDA_REG(pThis, RIRBSTS) &= ~(v & u32Value);

    HDA_PROCESS_INTERRUPT(pDevIns, pThis);
    return VINF_SUCCESS;
}

#ifdef IN_RING3

/**
 * Retrieves a corresponding sink for a given mixer control.
 *
 * @return  Pointer to the sink, NULL if no sink is found.
 * @param   pThisCC             The ring-3 HDA device state.
 * @param   enmMixerCtl         Mixer control to get the corresponding sink for.
 */
static PHDAMIXERSINK hdaR3MixerControlToSink(PHDASTATER3 pThisCC, PDMAUDIOMIXERCTL enmMixerCtl)
{
    PHDAMIXERSINK pSink;

    switch (enmMixerCtl)
    {
        case PDMAUDIOMIXERCTL_VOLUME_MASTER:
            /* Fall through is intentional. */
        case PDMAUDIOMIXERCTL_FRONT:
            pSink = &pThisCC->SinkFront;
            break;
# ifdef VBOX_WITH_AUDIO_HDA_51_SURROUND
        case PDMAUDIOMIXERCTL_CENTER_LFE:
            pSink = &pThisCC->SinkCenterLFE;
            break;
        case PDMAUDIOMIXERCTL_REAR:
            pSink = &pThisCC->SinkRear;
            break;
# endif
        case PDMAUDIOMIXERCTL_LINE_IN:
            pSink = &pThisCC->SinkLineIn;
            break;
# ifdef VBOX_WITH_AUDIO_HDA_MIC_IN
        case PDMAUDIOMIXERCTL_MIC_IN:
            pSink = &pThisCC->SinkMicIn;
            break;
# endif
        default:
            AssertMsgFailed(("Unhandled mixer control\n"));
            pSink = NULL;
            break;
    }

    return pSink;
}

/**
 * Adds a specific HDA driver to the driver chain.
 *
 * @returns VBox status code.
 * @param   pDevIns     The HDA device instance.
 * @param   pThisCC     The ring-3 HDA device state.
 * @param   pDrv        HDA driver to add.
 */
static int hdaR3MixerAddDrv(PPDMDEVINS pDevIns, PHDASTATER3 pThisCC, PHDADRIVER pDrv)
{
    int rc = VINF_SUCCESS;

    PHDASTREAM pStream = pThisCC->SinkLineIn.pStreamShared;
    if (   pStream
        && AudioHlpStreamCfgIsValid(&pStream->State.Cfg))
    {
        int rc2 = hdaR3MixerAddDrvStream(pDevIns, pThisCC->SinkLineIn.pMixSink, &pStream->State.Cfg, pDrv);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

# ifdef VBOX_WITH_AUDIO_HDA_MIC_IN
    pStream = pThisCC->SinkMicIn.pStreamShared;
    if (   pStream
        && AudioHlpStreamCfgIsValid(&pStream->State.Cfg))
    {
        int rc2 = hdaR3MixerAddDrvStream(pDevIns, pThisCC->SinkMicIn.pMixSink, &pStream->State.Cfg, pDrv);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }
# endif

    pStream = pThisCC->SinkFront.pStreamShared;
    if (   pStream
        && AudioHlpStreamCfgIsValid(&pStream->State.Cfg))
    {
        int rc2 = hdaR3MixerAddDrvStream(pDevIns, pThisCC->SinkFront.pMixSink, &pStream->State.Cfg, pDrv);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

# ifdef VBOX_WITH_AUDIO_HDA_51_SURROUND
    pStream = pThisCC->SinkCenterLFE.pStreamShared;
    if (   pStream
        && AudioHlpStreamCfgIsValid(&pStream->State.Cfg))
    {
        int rc2 = hdaR3MixerAddDrvStream(pDevIns, pThisCC->SinkCenterLFE.pMixSink, &pStream->State.Cfg, pDrv);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    pStream = pThisCC->SinkRear.pStreamShared;
    if (   pStream
        && AudioHlpStreamCfgIsValid(&pStream->State.Cfg))
    {
        int rc2 = hdaR3MixerAddDrvStream(pDevIns, pThisCC->SinkRear.pMixSink, &pStream->State.Cfg, pDrv);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }
# endif

    return rc;
}

/**
 * Removes a specific HDA driver from the driver chain and destroys its
 * associated streams.
 *
 * @param   pDevIns     The device instance.
 * @param   pThisCC     The ring-3 HDA device state.
 * @param   pDrv        HDA driver to remove.
 */
static void hdaR3MixerRemoveDrv(PPDMDEVINS pDevIns, PHDASTATER3 pThisCC, PHDADRIVER pDrv)
{
    AssertPtrReturnVoid(pDrv);

    if (pDrv->LineIn.pMixStrm)
    {
        AudioMixerSinkRemoveStream(pThisCC->SinkLineIn.pMixSink, pDrv->LineIn.pMixStrm);
        AudioMixerStreamDestroy(pDrv->LineIn.pMixStrm, pDevIns, true /*fImmediate*/);
        pDrv->LineIn.pMixStrm = NULL;
    }

# ifdef VBOX_WITH_AUDIO_HDA_MIC_IN
    if (pDrv->MicIn.pMixStrm)
    {
        AudioMixerSinkRemoveStream(pThisCC->SinkMicIn.pMixSink, pDrv->MicIn.pMixStrm);
        AudioMixerStreamDestroy(pDrv->MicIn.pMixStrm, pDevIns, true /*fImmediate*/);
        pDrv->MicIn.pMixStrm = NULL;
    }
# endif

    if (pDrv->Front.pMixStrm)
    {
        AudioMixerSinkRemoveStream(pThisCC->SinkFront.pMixSink, pDrv->Front.pMixStrm);
        AudioMixerStreamDestroy(pDrv->Front.pMixStrm, pDevIns, true /*fImmediate*/);
        pDrv->Front.pMixStrm = NULL;
    }

# ifdef VBOX_WITH_AUDIO_HDA_51_SURROUND
    if (pDrv->CenterLFE.pMixStrm)
    {
        AudioMixerSinkRemoveStream(pThisCC->SinkCenterLFE.pMixSink, pDrv->CenterLFE.pMixStrm);
        AudioMixerStreamDestroy(pDrv->CenterLFE.pMixStrm, pDevIns, true /*fImmediate*/);
        pDrv->CenterLFE.pMixStrm = NULL;
    }

    if (pDrv->Rear.pMixStrm)
    {
        AudioMixerSinkRemoveStream(pThisCC->SinkRear.pMixSink, pDrv->Rear.pMixStrm);
        AudioMixerStreamDestroy(pDrv->Rear.pMixStrm, pDevIns, true /*fImmediate*/);
        pDrv->Rear.pMixStrm = NULL;
    }
# endif

    RTListNodeRemove(&pDrv->Node);
}

/**
 * Adds a driver stream to a specific mixer sink.
 *
 * @returns VBox status code (ignored by caller).
 * @param   pDevIns     The HDA device instance.
 * @param   pMixSink    Audio mixer sink to add audio streams to.
 * @param   pCfg        Audio stream configuration to use for the audio
 *                      streams to add.
 * @param   pDrv        Driver stream to add.
 */
static int hdaR3MixerAddDrvStream(PPDMDEVINS pDevIns, PAUDMIXSINK pMixSink, PCPDMAUDIOSTREAMCFG pCfg, PHDADRIVER pDrv)
{
    AssertPtrReturn(pMixSink, VERR_INVALID_POINTER);
    AssertPtrReturn(pCfg,     VERR_INVALID_POINTER);

    LogFunc(("szSink=%s, szStream=%s, cChannels=%RU8\n", pMixSink->pszName, pCfg->szName, PDMAudioPropsChannels(&pCfg->Props)));

    /*
     * Get the matching stream driver.
     */
    PHDADRIVERSTREAM pDrvStream = NULL;
    if (pCfg->enmDir == PDMAUDIODIR_IN)
    {
        LogFunc(("enmPath=%d (src)\n", pCfg->enmPath));
        switch (pCfg->enmPath)
        {
            case PDMAUDIOPATH_IN_LINE:
                pDrvStream = &pDrv->LineIn;
                break;
# ifdef VBOX_WITH_AUDIO_HDA_MIC_IN
            case PDMAUDIOPATH_IN_MIC:
                pDrvStream = &pDrv->MicIn;
                break;
# endif
            default:
                LogFunc(("returns VERR_NOT_SUPPORTED - enmPath=%d\n", pCfg->enmPath));
                return VERR_NOT_SUPPORTED;
        }
    }
    else if (pCfg->enmDir == PDMAUDIODIR_OUT)
    {
        LogFunc(("enmDst=%d %s (dst)\n", pCfg->enmPath, PDMAudioPathGetName(pCfg->enmPath)));
        switch (pCfg->enmPath)
        {
            case PDMAUDIOPATH_OUT_FRONT:
                pDrvStream = &pDrv->Front;
                break;
# ifdef VBOX_WITH_AUDIO_HDA_51_SURROUND
            case PDMAUDIOPATH_OUT_CENTER_LFE:
                pDrvStream = &pDrv->CenterLFE;
                break;
            case PDMAUDIOPATH_OUT_REAR:
                pDrvStream = &pDrv->Rear;
                break;
# endif
            default:
                LogFunc(("returns VERR_NOT_SUPPORTED - enmPath=%d %s\n", pCfg->enmPath, PDMAudioPathGetName(pCfg->enmPath)));
                return VERR_NOT_SUPPORTED;
        }
    }
    else
        AssertFailedReturn(VERR_NOT_SUPPORTED);

    LogFunc(("[LUN#%RU8] %s\n", pDrv->uLUN, pCfg->szName));

    AssertPtr(pDrvStream);
    AssertMsg(pDrvStream->pMixStrm == NULL, ("[LUN#%RU8] Driver stream already present when it must not\n", pDrv->uLUN));

    PAUDMIXSTREAM pMixStrm = NULL;
    int rc = AudioMixerSinkCreateStream(pMixSink, pDrv->pConnector, pCfg, pDevIns, &pMixStrm);
    LogFlowFunc(("LUN#%RU8: Created stream \"%s\" for sink, rc=%Rrc\n", pDrv->uLUN, pCfg->szName, rc));
    if (RT_SUCCESS(rc))
    {
        rc = AudioMixerSinkAddStream(pMixSink, pMixStrm);
        LogFlowFunc(("LUN#%RU8: Added stream \"%s\" to sink, rc=%Rrc\n", pDrv->uLUN, pCfg->szName, rc));
        if (RT_FAILURE(rc))
            AudioMixerStreamDestroy(pMixStrm, pDevIns, true /*fImmediate*/);
    }

    if (RT_SUCCESS(rc))
        pDrvStream->pMixStrm = pMixStrm;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Adds all current driver streams to a specific mixer sink.
 *
 * @returns VBox status code.
 * @param   pDevIns     The HDA device instance.
 * @param   pThisCC     The ring-3 HDA device state.
 * @param   pMixSink    Audio mixer sink to add stream to.
 * @param   pCfg        Audio stream configuration to use for the audio streams
 *                      to add.
 */
static int hdaR3MixerAddDrvStreams(PPDMDEVINS pDevIns, PHDASTATER3 pThisCC, PAUDMIXSINK pMixSink, PCPDMAUDIOSTREAMCFG pCfg)
{
    AssertPtrReturn(pMixSink, VERR_INVALID_POINTER);
    AssertPtrReturn(pCfg,     VERR_INVALID_POINTER);

    LogFunc(("Sink=%s, Stream=%s\n", pMixSink->pszName, pCfg->szName));

    int rc;
    if (AudioHlpStreamCfgIsValid(pCfg))
    {
        rc = AudioMixerSinkSetFormat(pMixSink, &pCfg->Props, pCfg->Device.cMsSchedulingHint);
        if (RT_SUCCESS(rc))
        {
            PHDADRIVER pDrv;
            RTListForEach(&pThisCC->lstDrv, pDrv, HDADRIVER, Node)
            {
                /* We ignore failures here because one non-working driver shouldn't
                   be allowed to spoil it for everyone else. */
                int rc2 = hdaR3MixerAddDrvStream(pDevIns, pMixSink, pCfg, pDrv);
                if (RT_FAILURE(rc2))
                    LogFunc(("Attaching stream failed with %Rrc (ignored)\n", rc2));
            }
        }
    }
    else
        rc = VERR_INVALID_PARAMETER;
    return rc;
}


/**
 * Adds a new audio stream to a specific mixer control.
 *
 * Depending on the mixer control the stream then gets assigned to one of the
 * internal mixer sinks, which in turn then handle the mixing of all connected
 * streams to that sink.
 *
 * @return  VBox status code.
 * @param   pCodec              The codec instance data.
 * @param   enmMixerCtl         Mixer control to assign new stream to.
 * @param   pCfg                Stream configuration for the new stream.
 */
DECLHIDDEN(int) hdaR3MixerAddStream(PHDACODECR3 pCodec, PDMAUDIOMIXERCTL enmMixerCtl, PCPDMAUDIOSTREAMCFG pCfg)
{
    PHDASTATER3 pThisCC = RT_FROM_MEMBER(pCodec, HDASTATER3, Codec);
    AssertPtrReturn(pCfg,  VERR_INVALID_POINTER);

    int rc;
    PHDAMIXERSINK pSink = hdaR3MixerControlToSink(pThisCC, enmMixerCtl);
    if (pSink)
    {
        rc = hdaR3MixerAddDrvStreams(pThisCC->pDevIns, pThisCC, pSink->pMixSink, pCfg);

        AssertPtr(pSink->pMixSink);
        LogFlowFunc(("Sink=%s, Mixer control=%s\n", pSink->pMixSink->pszName, PDMAudioMixerCtlGetName(enmMixerCtl)));
    }
    else
        rc = VERR_NOT_FOUND;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Removes a specified mixer control from the HDA's mixer.
 *
 * @return  VBox status code.
 * @param   pCodec              The codec instance data.
 * @param   enmMixerCtl         Mixer control to remove.
 * @param   fImmediate          Whether the backend should be allowed to
 *                              finished draining (@c false) or if it must be
 *                              destroyed immediately (@c true).
 */
DECLHIDDEN(int) hdaR3MixerRemoveStream(PHDACODECR3 pCodec, PDMAUDIOMIXERCTL enmMixerCtl, bool fImmediate)
{
    PHDASTATER3 pThisCC = RT_FROM_MEMBER(pCodec, HDASTATER3, Codec);
    int         rc;

    PHDAMIXERSINK pSink = hdaR3MixerControlToSink(pThisCC, enmMixerCtl);
    if (pSink)
    {
        PHDADRIVER pDrv;
        RTListForEach(&pThisCC->lstDrv, pDrv, HDADRIVER, Node)
        {
            PAUDMIXSTREAM pMixStream = NULL;
            switch (enmMixerCtl)
            {
                /*
                 * Input.
                 */
                case PDMAUDIOMIXERCTL_LINE_IN:
                    pMixStream = pDrv->LineIn.pMixStrm;
                    pDrv->LineIn.pMixStrm = NULL;
                    break;
# ifdef VBOX_WITH_AUDIO_HDA_MIC_IN
                case PDMAUDIOMIXERCTL_MIC_IN:
                    pMixStream = pDrv->MicIn.pMixStrm;
                    pDrv->MicIn.pMixStrm = NULL;
                    break;
# endif
                /*
                 * Output.
                 */
                case PDMAUDIOMIXERCTL_FRONT:
                    pMixStream = pDrv->Front.pMixStrm;
                    pDrv->Front.pMixStrm = NULL;
                    break;
# ifdef VBOX_WITH_AUDIO_HDA_51_SURROUND
                case PDMAUDIOMIXERCTL_CENTER_LFE:
                    pMixStream = pDrv->CenterLFE.pMixStrm;
                    pDrv->CenterLFE.pMixStrm = NULL;
                    break;
                case PDMAUDIOMIXERCTL_REAR:
                    pMixStream = pDrv->Rear.pMixStrm;
                    pDrv->Rear.pMixStrm = NULL;
                    break;
# endif
                default:
                    AssertMsgFailed(("Mixer control %d not implemented\n", enmMixerCtl));
                    break;
            }

            if (pMixStream)
            {
                AudioMixerSinkRemoveStream(pSink->pMixSink, pMixStream);
                AudioMixerStreamDestroy(pMixStream, pThisCC->pDevIns, fImmediate);

                pMixStream = NULL;
            }
        }

        AudioMixerSinkRemoveAllStreams(pSink->pMixSink);
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_NOT_FOUND;

    LogFunc(("Mixer control=%s, rc=%Rrc\n", PDMAudioMixerCtlGetName(enmMixerCtl), rc));
    return rc;
}

/**
 * Controls an input / output converter widget, that is, which converter is
 * connected to which stream (and channel).
 *
 * @return  VBox status code.
 * @param   pCodec              The codec instance data.
 * @param   enmMixerCtl         Mixer control to set SD stream number and channel for.
 * @param   uSD                 SD stream number (number + 1) to set. Set to 0 for unassign.
 * @param   uChannel            Channel to set. Only valid if a valid SD stream number is specified.
 *
 * @note Is also called directly by the DevHDA code.
 */
DECLHIDDEN(int) hdaR3MixerControl(PHDACODECR3 pCodec, PDMAUDIOMIXERCTL enmMixerCtl, uint8_t uSD, uint8_t uChannel)
{
    PHDASTATER3 pThisCC = RT_FROM_MEMBER(pCodec, HDASTATER3, Codec);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PHDASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PHDASTATE);
    LogFunc(("enmMixerCtl=%s, uSD=%RU8, uChannel=%RU8\n", PDMAudioMixerCtlGetName(enmMixerCtl), uSD, uChannel));

    if (uSD == 0) /* Stream number 0 is reserved. */
    {
        Log2Func(("Invalid SDn (%RU8) number for mixer control '%s', ignoring\n", uSD, PDMAudioMixerCtlGetName(enmMixerCtl)));
        return VINF_SUCCESS;
    }
    /* uChannel is optional. */

    /* SDn0 starts as 1. */
    Assert(uSD);
    uSD--;

# ifndef VBOX_WITH_AUDIO_HDA_MIC_IN
    /* Only SDI0 (Line-In) is supported. */
    if (   hdaGetDirFromSD(uSD) == PDMAUDIODIR_IN
        && uSD >= 1)
    {
        LogRel2(("HDA: Dedicated Mic-In support not imlpemented / built-in (stream #%RU8), using Line-In (stream #0) instead\n", uSD));
        uSD = 0;
    }
# endif

    int rc = VINF_SUCCESS;

    PHDAMIXERSINK pSink = hdaR3MixerControlToSink(pThisCC, enmMixerCtl);
    if (pSink)
    {
        AssertPtr(pSink->pMixSink);

        /* If this an output stream, determine the correct SD#. */
        if (   uSD < HDA_MAX_SDI
            && AudioMixerSinkGetDir(pSink->pMixSink) == PDMAUDIODIR_OUT)
            uSD += HDA_MAX_SDI;

        /* Make 100% sure we got a good stream number before continuing. */
        AssertLogRelReturn(uSD < RT_ELEMENTS(pThisCC->aStreams), VERR_NOT_IMPLEMENTED);

        /* Detach the existing stream from the sink. */
        PHDASTREAM const   pOldStreamShared = pSink->pStreamShared;
        PHDASTREAMR3 const pOldStreamR3     = pSink->pStreamR3;
        if (   pOldStreamShared
            && pOldStreamR3
            && (   pOldStreamShared->u8SD      != uSD
                || pOldStreamShared->u8Channel != uChannel)
           )
        {
            LogFunc(("Sink '%s' was assigned to stream #%RU8 (channel %RU8) before\n",
                     pSink->pMixSink->pszName, pOldStreamShared->u8SD, pOldStreamShared->u8Channel));
            Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));

            /* Only disable the stream if the stream descriptor # has changed. */
            if (pOldStreamShared->u8SD != uSD)
                hdaR3StreamEnable(pThis, pOldStreamShared, pOldStreamR3, false /*fEnable*/);

            if (pOldStreamR3->State.pAioRegSink)
            {
                AudioMixerSinkRemoveUpdateJob(pOldStreamR3->State.pAioRegSink, hdaR3StreamUpdateAsyncIoJob, pOldStreamR3);
                pOldStreamR3->State.pAioRegSink = NULL;
            }

            pOldStreamR3->pMixSink = NULL;


            pSink->pStreamShared = NULL;
            pSink->pStreamR3     = NULL;
        }

        /* Attach the new stream to the sink.
         * Enabling the stream will be done by the guest via a separate SDnCTL call then. */
        if (pSink->pStreamShared == NULL)
        {
            LogRel2(("HDA: Setting sink '%s' to stream #%RU8 (channel %RU8), mixer control=%s\n",
                     pSink->pMixSink->pszName, uSD, uChannel, PDMAudioMixerCtlGetName(enmMixerCtl)));

            PHDASTREAMR3 pStreamR3     = &pThisCC->aStreams[uSD];
            PHDASTREAM   pStreamShared = &pThis->aStreams[uSD];
            Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));

            pSink->pStreamR3     = pStreamR3;
            pSink->pStreamShared = pStreamShared;

            pStreamShared->u8Channel = uChannel;
            pStreamR3->pMixSink      = pSink;

            rc = VINF_SUCCESS;
        }
    }
    else
        rc = VERR_NOT_FOUND;

    if (RT_FAILURE(rc))
        LogRel(("HDA: Converter control for stream #%RU8 (channel %RU8) / mixer control '%s' failed with %Rrc, skipping\n",
                uSD, uChannel, PDMAudioMixerCtlGetName(enmMixerCtl), rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sets the volume of a specified mixer control.
 *
 * @return  IPRT status code.
 * @param   pCodec              The codec instance data.
 * @param   enmMixerCtl         Mixer control to set volume for.
 * @param   pVol                Pointer to volume data to set.
 */
DECLHIDDEN(int) hdaR3MixerSetVolume(PHDACODECR3 pCodec, PDMAUDIOMIXERCTL enmMixerCtl, PPDMAUDIOVOLUME pVol)
{
    PHDASTATER3 pThisCC = RT_FROM_MEMBER(pCodec, HDASTATER3, Codec);
    int         rc;

    PHDAMIXERSINK pSink = hdaR3MixerControlToSink(pThisCC, enmMixerCtl);
    if (   pSink
        && pSink->pMixSink)
    {
        LogRel2(("HDA: Setting volume for mixer sink '%s' to fMuted=%RTbool auChannels=%.*Rhxs\n",
                 pSink->pMixSink->pszName, pVol->fMuted, sizeof(pVol->auChannels), pVol->auChannels));

        /* Set the volume.
         * We assume that the codec already converted it to the correct range. */
        rc = AudioMixerSinkSetVolume(pSink->pMixSink, pVol);
    }
    else
        rc = VERR_NOT_FOUND;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * @callback_method_impl{FNTMTIMERDEV, Main routine for the stream's timer.}
 */
static DECLCALLBACK(void) hdaR3Timer(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PHDASTATE       pThis         = PDMDEVINS_2_DATA(pDevIns, PHDASTATE);
    PHDASTATER3     pThisCC       = PDMDEVINS_2_DATA_CC(pDevIns, PHDASTATER3);
    uintptr_t       idxStream     = (uintptr_t)pvUser;
    AssertReturnVoid(idxStream < RT_ELEMENTS(pThis->aStreams));
    PHDASTREAM      pStreamShared = &pThis->aStreams[idxStream];
    PHDASTREAMR3    pStreamR3     = &pThisCC->aStreams[idxStream];
    Assert(hTimer == pStreamShared->hTimer);

    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
    Assert(PDMDevHlpTimerIsLockOwner(pDevIns, hTimer));

    RT_NOREF(hTimer);

    hdaR3StreamTimerMain(pDevIns, pThis, pThisCC, pStreamShared, pStreamR3);
}

/**
 * Soft reset of the device triggered via GCTL.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared HDA device state.
 * @param   pThisCC             The ring-3 HDA device state.
 */
static void hdaR3GCTLReset(PPDMDEVINS pDevIns, PHDASTATE pThis, PHDASTATER3 pThisCC)
{
    LogFlowFuncEnter();
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));

    /*
     * Make sure all streams have stopped as these have both timers and
     * asynchronous worker threads that would race us if we delay this work.
     */
    for (size_t idxStream = 0; idxStream < RT_ELEMENTS(pThis->aStreams); idxStream++)
    {
        PHDASTREAM const   pStreamShared = &pThis->aStreams[idxStream];
        PHDASTREAMR3 const pStreamR3     = &pThisCC->aStreams[idxStream];
        PAUDMIXSINK const pMixSink = pStreamR3->pMixSink ? pStreamR3->pMixSink->pMixSink : NULL;
        if (pMixSink)
            AudioMixerSinkLock(pMixSink);

        /* We're doing this unconditionally, hope that's not problematic in any way... */
        int rc = hdaR3StreamEnable(pThis, pStreamShared, &pThisCC->aStreams[idxStream], false /* fEnable */);
        AssertLogRelMsg(RT_SUCCESS(rc) && !pStreamShared->State.fRunning,
                        ("Disabling stream #%u failed: %Rrc, fRunning=%d\n", idxStream, rc, pStreamShared->State.fRunning));
        pStreamShared->State.fRunning = false;

        hdaR3StreamReset(pThis, pThisCC, pStreamShared, &pThisCC->aStreams[idxStream], (uint8_t)idxStream);

        if (pMixSink) /* (FYI. pMixSink might not be what pStreamR3->pMixSink->pMixSink points at any longer) */
            AudioMixerSinkUnlock(pMixSink);
    }

    /*
     * Reset registers.
     */
    HDA_REG(pThis, GCAP)     = HDA_MAKE_GCAP(HDA_MAX_SDO, HDA_MAX_SDI, 0, 0, 1); /* see 6.2.1 */
    HDA_REG(pThis, VMIN)     = 0x00;                                             /* see 6.2.2 */
    HDA_REG(pThis, VMAJ)     = 0x01;                                             /* see 6.2.3 */
    HDA_REG(pThis, OUTPAY)   = 0x003C;                                           /* see 6.2.4 */
    HDA_REG(pThis, INPAY)    = 0x001D;                                           /* see 6.2.5 */
    HDA_REG(pThis, CORBSIZE) = 0x42; /* Up to 256 CORB entries                      see 6.2.1 */
    HDA_REG(pThis, RIRBSIZE) = 0x42; /* Up to 256 RIRB entries                      see 6.2.1 */
    HDA_REG(pThis, CORBRP)   = 0x0;
    HDA_REG(pThis, CORBWP)   = 0x0;
    HDA_REG(pThis, RIRBWP)   = 0x0;
    /* Some guests (like Haiku) don't set RINTCNT explicitly but expect an interrupt after each
     * RIRB response -- so initialize RINTCNT to 1 by default. */
    HDA_REG(pThis, RINTCNT)  = 0x1;
    /* For newer devices, there is a capability list offset word at 0x14, linux read it, does
       no checking and simply reads the dword it specifies.  The list terminates when the lower
       16 bits are zero.  See snd_hdac_bus_parse_capabilities.  Table 5-2 in intel 341081-002
       specifies this to be 0xc00 and chaining with 0x800, 0x500 and 0x1f00. We just terminate
       it at 0xc00 for now. */
    HDA_REG(pThis, LLCH)     = 0xc00;
    HDA_REG(pThis, MLCH)     = 0x0;
    HDA_REG(pThis, MLCD)     = 0x0;

    /*
     * Stop any audio currently playing and/or recording.
     */
    pThisCC->SinkFront.pStreamShared = NULL;
    pThisCC->SinkFront.pStreamR3     = NULL;
    if (pThisCC->SinkFront.pMixSink)
        AudioMixerSinkReset(pThisCC->SinkFront.pMixSink);
# ifdef VBOX_WITH_AUDIO_HDA_MIC_IN
    pThisCC->SinkMicIn.pStreamShared = NULL;
    pThisCC->SinkMicIn.pStreamR3     = NULL;
    if (pThisCC->SinkMicIn.pMixSink)
        AudioMixerSinkReset(pThisCC->SinkMicIn.pMixSink);
# endif
    pThisCC->SinkLineIn.pStreamShared = NULL;
    pThisCC->SinkLineIn.pStreamR3     = NULL;
    if (pThisCC->SinkLineIn.pMixSink)
        AudioMixerSinkReset(pThisCC->SinkLineIn.pMixSink);
# ifdef VBOX_WITH_AUDIO_HDA_51_SURROUND
    pThisCC->SinkCenterLFE = NULL;
    if (pThisCC->SinkCenterLFE.pMixSink)
        AudioMixerSinkReset(pThisCC->SinkCenterLFE.pMixSink);
    pThisCC->SinkRear.pStreamShared = NULL;
    pThisCC->SinkRear.pStreamR3     = NULL;
    if (pThisCC->SinkRear.pMixSink)
        AudioMixerSinkReset(pThisCC->SinkRear.pMixSink);
# endif

    /*
     * Reset the codec.
     */
    hdaCodecReset(&pThisCC->Codec);

    /*
     * Set some sensible defaults for which HDA sinks
     * are connected to which stream number.
     *
     * We use SD0 for input and SD4 for output by default.
     * These stream numbers can be changed by the guest dynamically lateron.
     */
    ASMCompilerBarrier(); /* paranoia */
# ifdef VBOX_WITH_AUDIO_HDA_MIC_IN
    hdaR3MixerControl(&pThisCC->Codec, PDMAUDIOMIXERCTL_MIC_IN    , 1 /* SD0 */, 0 /* Channel */);
# endif
    hdaR3MixerControl(&pThisCC->Codec, PDMAUDIOMIXERCTL_LINE_IN   , 1 /* SD0 */, 0 /* Channel */);

    hdaR3MixerControl(&pThisCC->Codec, PDMAUDIOMIXERCTL_FRONT     , 5 /* SD4 */, 0 /* Channel */);
# ifdef VBOX_WITH_AUDIO_HDA_51_SURROUND
    hdaR3MixerControl(&pThisCC->Codec, PDMAUDIOMIXERCTL_CENTER_LFE, 5 /* SD4 */, 0 /* Channel */);
    hdaR3MixerControl(&pThisCC->Codec, PDMAUDIOMIXERCTL_REAR      , 5 /* SD4 */, 0 /* Channel */);
# endif
    ASMCompilerBarrier(); /* paranoia */

    /* Reset CORB. */
    pThis->cbCorbBuf = HDA_CORB_SIZE * HDA_CORB_ELEMENT_SIZE;
    RT_ZERO(pThis->au32CorbBuf);

    /* Reset RIRB. */
    pThis->cbRirbBuf = HDA_RIRB_SIZE * HDA_RIRB_ELEMENT_SIZE;
    RT_ZERO(pThis->au64RirbBuf);

    /* Clear our internal response interrupt counter. */
    pThis->u16RespIntCnt = 0;

    /* Clear stream tags <-> objects mapping table. */
    RT_ZERO(pThisCC->aTags);

    /* Emulation of codec "wake up" (HDA spec 5.5.1 and 6.5). */
    HDA_REG(pThis, STATESTS) = 0x1;

    /* Reset the wall clock. */
    pThis->tsWalClkStart = PDMDevHlpTimerGet(pDevIns, pThis->aStreams[0].hTimer);

    LogFlowFuncLeave();
    LogRel(("HDA: Reset\n"));
}

#else   /* !IN_RING3 */

/**
 * Checks if a dword read starting with @a idxRegDsc is safe.
 *
 * We can guarentee it only standard reader callbacks are used.
 * @returns true if it will always succeed, false if it may return back to
 *          ring-3 or we're just not sure.
 * @param   idxRegDsc       The first register descriptor in the DWORD being read.
 */
DECLINLINE(bool) hdaIsMultiReadSafeInRZ(unsigned idxRegDsc)
{
    int32_t cbLeft = 4; /* signed on purpose */
    do
    {
        if (   g_aHdaRegMap[idxRegDsc].pfnRead == hdaRegReadU24
            || g_aHdaRegMap[idxRegDsc].pfnRead == hdaRegReadU16
            || g_aHdaRegMap[idxRegDsc].pfnRead == hdaRegReadU8
            || g_aHdaRegMap[idxRegDsc].pfnRead == hdaRegReadUnimpl)
        { /* okay */ }
        else
        {
            Log4(("hdaIsMultiReadSafeInRZ: idxRegDsc=%u %s\n", idxRegDsc, g_aHdaRegMap[idxRegDsc].pszName));
            return false;
        }

        idxRegDsc++;
        if (idxRegDsc < RT_ELEMENTS(g_aHdaRegMap))
            cbLeft -= g_aHdaRegMap[idxRegDsc].off - g_aHdaRegMap[idxRegDsc - 1].off;
        else
            break;
    } while (cbLeft > 0);
    return true;
}


#endif /* !IN_RING3 */


/* MMIO callbacks */

/**
 * @callback_method_impl{FNIOMMMIONEWREAD, Looks up and calls the appropriate handler.}
 *
 * @note During implementation, we discovered so-called "forgotten" or "hole"
 *       registers whose description is not listed in the RPM, datasheet, or
 *       spec.
 */
static DECLCALLBACK(VBOXSTRICTRC) hdaMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    PHDASTATE       pThis  = PDMDEVINS_2_DATA(pDevIns, PHDASTATE);
    VBOXSTRICTRC    rc;
    RT_NOREF_PV(pvUser);
    Assert(pThis->uAlignmentCheckMagic == HDASTATE_ALIGNMENT_CHECK_MAGIC);

    /*
     * Look up and log.
     */
    int             idxRegDsc = hdaRegLookup(off);    /* Register descriptor index. */
#ifdef LOG_ENABLED
    unsigned const  cbLog     = cb;
    uint32_t        offRegLog = (uint32_t)off;
# ifdef HDA_DEBUG_GUEST_RIP
    if (LogIs6Enabled())
    {
        PVMCPU pVCpu = (PVMCPU)PDMDevHlpGetVMCPU(pDevIns);
        Log6Func(("cs:rip=%04x:%016RX64 rflags=%08RX32\n", CPUMGetGuestCS(pVCpu), CPUMGetGuestRIP(pVCpu), CPUMGetGuestEFlags(pVCpu)));
    }
# endif
#endif

    Log3Func(("off=%#x cb=%#x\n", offRegLog, cb));
    Assert(cb == 4); Assert((off & 3) == 0);

    rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VINF_IOM_R3_MMIO_READ);
    if (rc == VINF_SUCCESS)
    {
        if (!(HDA_REG(pThis, GCTL) & HDA_GCTL_CRST) && idxRegDsc != HDA_REG_GCTL)
            LogFunc(("Access to registers except GCTL is blocked while resetting\n"));

        if (idxRegDsc >= 0)
        {
            /* ASSUMES gapless DWORD at end of map. */
            if (g_aHdaRegMap[idxRegDsc].cb == 4)
            {
                /*
                 * Straight forward DWORD access.
                 */
                rc = g_aHdaRegMap[idxRegDsc].pfnRead(pDevIns, pThis, idxRegDsc, (uint32_t *)pv);
                Log3Func(("  Read %s => %x (%Rrc)\n", g_aHdaRegMap[idxRegDsc].pszName, *(uint32_t *)pv, VBOXSTRICTRC_VAL(rc)));
                STAM_COUNTER_INC(&pThis->aStatRegReads[idxRegDsc]);
            }
#ifndef IN_RING3
            else if (!hdaIsMultiReadSafeInRZ(idxRegDsc))

            {
                STAM_COUNTER_INC(&pThis->aStatRegReadsToR3[idxRegDsc]);
                rc = VINF_IOM_R3_MMIO_READ;
            }
#endif
            else
            {
                /*
                 * Multi register read (unless there are trailing gaps).
                 * ASSUMES that only DWORD reads have sideeffects.
                 */
                STAM_COUNTER_INC(&pThis->CTX_SUFF_Z(StatRegMultiReads));
                Log4(("hdaMmioRead: multi read: %#x LB %#x %s\n", off, cb, g_aHdaRegMap[idxRegDsc].pszName));
                uint32_t u32Value = 0;
                unsigned cbLeft   = 4;
                do
                {
                    uint32_t const  cbReg        = g_aHdaRegMap[idxRegDsc].cb;
                    uint32_t        u32Tmp       = 0;

                    rc = g_aHdaRegMap[idxRegDsc].pfnRead(pDevIns, pThis, idxRegDsc, &u32Tmp);
                    Log4Func(("  Read %s[%db] => %x (%Rrc)*\n", g_aHdaRegMap[idxRegDsc].pszName, cbReg, u32Tmp, VBOXSTRICTRC_VAL(rc)));
                    STAM_COUNTER_INC(&pThis->aStatRegReads[idxRegDsc]);
#ifdef IN_RING3
                    if (rc != VINF_SUCCESS)
                        break;
#else
                    AssertMsgBreak(rc == VINF_SUCCESS, ("rc=%Rrc - impossible, we sanitized the readers!\n", VBOXSTRICTRC_VAL(rc)));
#endif
                    u32Value |= (u32Tmp & g_afMasks[cbReg]) << ((4 - cbLeft) * 8);

                    cbLeft -= cbReg;
                    off    += cbReg;
                    idxRegDsc++;
                } while (cbLeft > 0 && g_aHdaRegMap[idxRegDsc].off == off);

                if (rc == VINF_SUCCESS)
                    *(uint32_t *)pv = u32Value;
                else
                    Assert(!IOM_SUCCESS(rc));
            }
        }
        else
        {
            LogRel(("HDA: Invalid read access @0x%x (bytes=%u)\n", (uint32_t)off, cb));
            Log3Func(("  Hole at %x is accessed for read\n", offRegLog));
            STAM_COUNTER_INC(&pThis->StatRegUnknownReads);
            rc = VINF_IOM_MMIO_UNUSED_FF;
        }

        DEVHDA_UNLOCK(pDevIns, pThis);

        /*
         * Log the outcome.
         */
#ifdef LOG_ENABLED
        if (cbLog == 4)
            Log3Func(("  Returning @%#05x -> %#010x %Rrc\n", offRegLog, *(uint32_t *)pv, VBOXSTRICTRC_VAL(rc)));
        else if (cbLog == 2)
            Log3Func(("  Returning @%#05x -> %#06x %Rrc\n", offRegLog, *(uint16_t *)pv, VBOXSTRICTRC_VAL(rc)));
        else if (cbLog == 1)
            Log3Func(("  Returning @%#05x -> %#04x %Rrc\n", offRegLog, *(uint8_t *)pv, VBOXSTRICTRC_VAL(rc)));
#endif
    }
    else
    {
        if (idxRegDsc >= 0)
            STAM_COUNTER_INC(&pThis->aStatRegReadsToR3[idxRegDsc]);
    }
    return rc;
}


DECLINLINE(VBOXSTRICTRC) hdaWriteReg(PPDMDEVINS pDevIns, PHDASTATE pThis, int idxRegDsc, uint32_t u32Value, char const *pszLog)
{
    if (   (HDA_REG(pThis, GCTL) & HDA_GCTL_CRST)
        || idxRegDsc == HDA_REG_GCTL)
    { /* likely */ }
    else
    {
        Log(("hdaWriteReg: Warning: Access to %s is blocked while controller is in reset mode\n", g_aHdaRegMap[idxRegDsc].pszName));
#if defined(IN_RING3) || defined(LOG_ENABLED)
        LogRel2(("HDA: Warning: Access to register %s is blocked while controller is in reset mode\n",
                 g_aHdaRegMap[idxRegDsc].pszName));
#endif
        STAM_COUNTER_INC(&pThis->StatRegWritesBlockedByReset);
        return VINF_SUCCESS;
    }

    /*
     * Handle RD (register description) flags.
     */

    /* For SDI / SDO: Check if writes to those registers are allowed while SDCTL's RUN bit is set. */
    if (idxRegDsc >= HDA_NUM_GENERAL_REGS)
    {
        /*
         * Some OSes (like Win 10 AU) violate the spec by writing stuff to registers which are not supposed to be be touched
         * while SDCTL's RUN bit is set. So just ignore those values.
         */
        const uint32_t uSDCTL = HDA_STREAM_REG(pThis, CTL, HDA_SD_NUM_FROM_REG(pThis, CTL, idxRegDsc));
        if (   !(uSDCTL & HDA_SDCTL_RUN)
            || (g_aHdaRegMap[idxRegDsc].fFlags & HDA_RD_F_SD_WRITE_RUN))
        { /* likely */ }
        else
        {
            Log(("hdaWriteReg: Warning: Access to %s is blocked! %R[sdctl]\n", g_aHdaRegMap[idxRegDsc].pszName, uSDCTL));
#if defined(IN_RING3) || defined(LOG_ENABLED)
            LogRel2(("HDA: Warning: Access to register %s is blocked while the stream's RUN bit is set\n",
                     g_aHdaRegMap[idxRegDsc].pszName));
#endif
            STAM_COUNTER_INC(&pThis->StatRegWritesBlockedByRun);
            return VINF_SUCCESS;
        }
    }

#ifdef LOG_ENABLED
    uint32_t const idxRegMem   = g_aHdaRegMap[idxRegDsc].idxReg;
    uint32_t const u32OldValue = pThis->au32Regs[idxRegMem];
#endif
    VBOXSTRICTRC rc = g_aHdaRegMap[idxRegDsc].pfnWrite(pDevIns, pThis, idxRegDsc, u32Value);
    Log3Func(("Written value %#x to %s[%d byte]; %x => %x%s, rc=%d\n", u32Value, g_aHdaRegMap[idxRegDsc].pszName,
              g_aHdaRegMap[idxRegDsc].cb, u32OldValue, pThis->au32Regs[idxRegMem], pszLog, VBOXSTRICTRC_VAL(rc)));
#ifndef IN_RING3
    if (rc == VINF_IOM_R3_MMIO_WRITE)
        STAM_COUNTER_INC(&pThis->aStatRegWritesToR3[idxRegDsc]);
    else
#endif
        STAM_COUNTER_INC(&pThis->aStatRegWrites[idxRegDsc]);

    RT_NOREF(pszLog);
    return rc;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE,
 *      Looks up and calls the appropriate handler.}
 */
static DECLCALLBACK(VBOXSTRICTRC) hdaMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PHDASTATE pThis  = PDMDEVINS_2_DATA(pDevIns, PHDASTATE);
    RT_NOREF_PV(pvUser);
    Assert(pThis->uAlignmentCheckMagic == HDASTATE_ALIGNMENT_CHECK_MAGIC);

    /*
     * Look up and log the access.
     */
    int         idxRegDsc = hdaRegLookup(off);
#if defined(IN_RING3) || defined(LOG_ENABLED)
    uint32_t    idxRegMem = idxRegDsc != -1 ? g_aHdaRegMap[idxRegDsc].idxReg : UINT32_MAX;
#endif
    uint64_t    u64Value;
    if (cb == 4)        u64Value = *(uint32_t const *)pv;
    else if (cb == 2)   u64Value = *(uint16_t const *)pv;
    else if (cb == 1)   u64Value = *(uint8_t const *)pv;
    else if (cb == 8)   u64Value = *(uint64_t const *)pv;
    else
        ASSERT_GUEST_MSG_FAILED_RETURN(("cb=%u %.*Rhxs\n", cb, cb, pv),
                                       PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "odd write size: off=%RGp cb=%u\n", off, cb));

    /*
     * The behavior of accesses that aren't aligned on natural boundraries is
     * undefined. Just reject them outright.
     */
    ASSERT_GUEST_MSG_RETURN((off & (cb - 1)) == 0, ("off=%RGp cb=%u %.*Rhxs\n", off, cb, cb, pv),
                            PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "misaligned write access: off=%RGp cb=%u\n", off, cb));

#ifdef LOG_ENABLED
    uint32_t const u32LogOldValue = idxRegDsc >= 0 ? pThis->au32Regs[idxRegMem] : UINT32_MAX;
# ifdef HDA_DEBUG_GUEST_RIP
    if (LogIs6Enabled())
    {
        PVMCPU pVCpu = (PVMCPU)PDMDevHlpGetVMCPU(pDevIns);
        Log6Func(("cs:rip=%04x:%016RX64 rflags=%08RX32\n", CPUMGetGuestCS(pVCpu), CPUMGetGuestRIP(pVCpu), CPUMGetGuestEFlags(pVCpu)));
    }
# endif
#endif

    /*
     * Try for a direct hit first.
     */
    VBOXSTRICTRC rc;
    if (idxRegDsc >= 0 && g_aHdaRegMap[idxRegDsc].cb == cb)
    {
        DEVHDA_LOCK_RETURN(pDevIns, pThis, VINF_IOM_R3_MMIO_WRITE);

        Log3Func(("@%#05x u%u=%#0*RX64 %s\n", (uint32_t)off, cb * 8, 2 + cb * 2, u64Value, g_aHdaRegMap[idxRegDsc].pszName));
        rc = hdaWriteReg(pDevIns, pThis, idxRegDsc, u64Value, "");
        Log3Func(("  %#x -> %#x\n", u32LogOldValue, idxRegMem != UINT32_MAX ? pThis->au32Regs[idxRegMem] : UINT32_MAX));

        DEVHDA_UNLOCK(pDevIns, pThis);
    }
    /*
     * Sub-register access.  Supply missing bits as needed.
     */
    else if (   idxRegDsc >= 0
             && cb < g_aHdaRegMap[idxRegDsc].cb)
    {
        DEVHDA_LOCK_RETURN(pDevIns, pThis, VINF_IOM_R3_MMIO_WRITE);

        u64Value |=   pThis->au32Regs[g_aHdaRegMap[idxRegDsc].idxReg]
                    & g_afMasks[g_aHdaRegMap[idxRegDsc].cb]
                    & ~g_afMasks[cb];
        Log4Func(("@%#05x u%u=%#0*RX64 cb=%#x cbReg=%x %s\n"
                  "hdaMmioWrite: Supplying missing bits (%#x): %#llx -> %#llx ...\n",
                  (uint32_t)off, cb * 8, 2 + cb * 2, u64Value, cb, g_aHdaRegMap[idxRegDsc].cb, g_aHdaRegMap[idxRegDsc].pszName,
                  g_afMasks[g_aHdaRegMap[idxRegDsc].cb] & ~g_afMasks[cb], u64Value & g_afMasks[cb], u64Value));
        rc = hdaWriteReg(pDevIns, pThis, idxRegDsc, u64Value, "");
        Log4Func(("  %#x -> %#x\n", u32LogOldValue, idxRegMem != UINT32_MAX ? pThis->au32Regs[idxRegMem] : UINT32_MAX));
        STAM_COUNTER_INC(&pThis->CTX_SUFF_Z(StatRegSubWrite));

        DEVHDA_UNLOCK(pDevIns, pThis);
    }
    /*
     * Partial or multiple register access, loop thru the requested memory.
     */
    else
    {
#ifdef IN_RING3
        DEVHDA_LOCK_RETURN(pDevIns, pThis, VINF_IOM_R3_MMIO_WRITE);

        if (idxRegDsc == -1)
            Log4Func(("@%#05x u32=%#010x cb=%d\n", (uint32_t)off, *(uint32_t const *)pv, cb));
        else if (g_aHdaRegMap[idxRegDsc].cb == cb)
            Log4Func(("@%#05x u%u=%#0*RX64 %s\n", (uint32_t)off, cb * 8, 2 + cb * 2, u64Value, g_aHdaRegMap[idxRegDsc].pszName));
        else
            Log4Func(("@%#05x u%u=%#0*RX64 %s - mismatch cbReg=%u\n", (uint32_t)off, cb * 8, 2 + cb * 2, u64Value,
                      g_aHdaRegMap[idxRegDsc].pszName, g_aHdaRegMap[idxRegDsc].cb));

        /*
         * If it's an access beyond the start of the register, shift the input
         * value and fill in missing bits. Natural alignment rules means we
         * will only see 1 or 2 byte accesses of this kind, so no risk of
         * shifting out input values.
         */
        if (idxRegDsc < 0)
        {
            uint32_t cbBefore;
            idxRegDsc = hdaR3RegLookupWithin(off, &cbBefore);
            if (idxRegDsc != -1)
            {
                Assert(cbBefore > 0 && cbBefore < 4 /* no register is wider than 4 bytes, we check in the constructor */);
                off      -= cbBefore;
                idxRegMem = g_aHdaRegMap[idxRegDsc].idxReg;
                u64Value <<= cbBefore * 8;
                u64Value  |= pThis->au32Regs[idxRegMem] & g_afMasks[cbBefore];
                Log4Func(("  Within register, supplied %u leading bits: %#llx -> %#llx ...\n",
                          cbBefore * 8, ~(uint64_t)g_afMasks[cbBefore] & u64Value, u64Value));
                STAM_COUNTER_INC(&pThis->CTX_SUFF_Z(StatRegMultiWrites));
            }
            else
                STAM_COUNTER_INC(&pThis->StatRegUnknownWrites);
        }
        else
        {
            Log4(("hdaMmioWrite: multi write: %s\n", g_aHdaRegMap[idxRegDsc].pszName));
            STAM_COUNTER_INC(&pThis->CTX_SUFF_Z(StatRegMultiWrites));
        }

        /* Loop thru the write area, it may cover multiple registers. */
        rc = VINF_SUCCESS;
        for (;;)
        {
            uint32_t cbReg;
            if (idxRegDsc >= 0)
            {
                idxRegMem = g_aHdaRegMap[idxRegDsc].idxReg;
                cbReg     = g_aHdaRegMap[idxRegDsc].cb;
                if (cb < cbReg)
                {
                    u64Value |= pThis->au32Regs[idxRegMem] & g_afMasks[cbReg] & ~g_afMasks[cb];
                    Log4Func(("  Supplying missing bits (%#x): %#llx -> %#llx ...\n",
                              g_afMasks[cbReg] & ~g_afMasks[cb], u64Value & g_afMasks[cb], u64Value));
                }
# ifdef LOG_ENABLED
                uint32_t uLogOldVal = pThis->au32Regs[idxRegMem];
# endif
                rc = hdaWriteReg(pDevIns, pThis, idxRegDsc, u64Value & g_afMasks[cbReg], "*");
                Log4Func(("  %#x -> %#x\n", uLogOldVal, pThis->au32Regs[idxRegMem]));
            }
            else
            {
                LogRel(("HDA: Invalid write access @0x%x\n", (uint32_t)off));
                cbReg = 1;
            }
            if (rc != VINF_SUCCESS)
                break;
            if (cbReg >= cb)
                break;

            /* Advance. */
            off += cbReg;
            cb  -= cbReg;
            u64Value >>= cbReg * 8;
            if (idxRegDsc == -1)
                idxRegDsc = hdaRegLookup(off);
            else
            {
                /** @todo r=bird: This doesn't work for aliased registers, since the incremented
                 * offset won't match as it's still the aliased one.  Only scenario, though
                 * would be misaligned accesses (2, 4 or 8 bytes), and the result would be that
                 * only the first part will be written.  Given that the aliases we have are lone
                 * registers, that seems like they shouldn't have anything else around them,
                 * this is probably the correct behaviour, though real hw may of course
                 * disagree.  Only look into it if we have a sane guest running into this. */
                idxRegDsc++;
                if (   (unsigned)idxRegDsc >= RT_ELEMENTS(g_aHdaRegMap)
                    || g_aHdaRegMap[idxRegDsc].off != off)
                    idxRegDsc = -1;
            }
        }

        DEVHDA_UNLOCK(pDevIns, pThis);

#else  /* !IN_RING3 */
        /* Take the simple way out. */
        rc = VINF_IOM_R3_MMIO_WRITE;
#endif /* !IN_RING3 */
    }

    return rc;
}

#ifdef IN_RING3


/*********************************************************************************************************************************
*   Saved state                                                                                                                  *
*********************************************************************************************************************************/

/**
 * @callback_method_impl{FNSSMFIELDGETPUT,
 * Version 6 saves the IOC flag in HDABDLEDESC::fFlags as a bool}
 */
static DECLCALLBACK(int)
hdaR3GetPutTrans_HDABDLEDESC_fFlags_6(PSSMHANDLE pSSM, const struct SSMFIELD *pField, void *pvStruct,
                                      uint32_t fFlags, bool fGetOrPut, void *pvUser)
{
    PPDMDEVINS pDevIns = (PPDMDEVINS)pvUser;
    RT_NOREF(pSSM, pField, pvStruct, fFlags);
    AssertReturn(fGetOrPut, VERR_INTERNAL_ERROR_4);
    bool fIoc;
    int rc = pDevIns->pHlpR3->pfnSSMGetBool(pSSM, &fIoc);
    if (RT_SUCCESS(rc))
    {
        PHDABDLEDESC pDesc = (PHDABDLEDESC)pvStruct;
        pDesc->fFlags = fIoc ? HDA_BDLE_F_IOC : 0;
    }
    return rc;
}


/**
 * @callback_method_impl{FNSSMFIELDGETPUT,
 * Versions 1 thru 4 save the IOC flag in HDASTREAMSTATE::DescfFlags as a bool}
 */
static DECLCALLBACK(int)
hdaR3GetPutTrans_HDABDLE_Desc_fFlags_1thru4(PSSMHANDLE pSSM, const struct SSMFIELD *pField, void *pvStruct,
                                            uint32_t fFlags, bool fGetOrPut, void *pvUser)
{
    PPDMDEVINS pDevIns = (PPDMDEVINS)pvUser;
    RT_NOREF(pSSM, pField, pvStruct, fFlags);
    AssertReturn(fGetOrPut, VERR_INTERNAL_ERROR_4);
    bool fIoc;
    int rc = pDevIns->pHlpR3->pfnSSMGetBool(pSSM, &fIoc);
    if (RT_SUCCESS(rc))
    {
        HDABDLELEGACY *pState = (HDABDLELEGACY *)pvStruct;
        pState->Desc.fFlags = fIoc ? HDA_BDLE_F_IOC : 0;
    }
    return rc;
}


static int hdaR3SaveStream(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, PHDASTREAM pStreamShared, PHDASTREAMR3 pStreamR3)
{
    PCPDMDEVHLPR3 pHlp = pDevIns->pHlpR3;
# ifdef LOG_ENABLED
    PHDASTATE pThis = PDMDEVINS_2_DATA(pDevIns, PHDASTATE);
# endif

    Log2Func(("[SD%RU8]\n", pStreamShared->u8SD));

    /* Save stream ID. */
    Assert(pStreamShared->u8SD < HDA_MAX_STREAMS);
    int rc = pHlp->pfnSSMPutU8(pSSM, pStreamShared->u8SD);
    AssertRCReturn(rc, rc);

    rc = pHlp->pfnSSMPutStructEx(pSSM, &pStreamShared->State, sizeof(pStreamShared->State),
                                 0 /*fFlags*/, g_aSSMStreamStateFields7, NULL);
    AssertRCReturn(rc, rc);

    AssertCompile(sizeof(pStreamShared->State.idxCurBdle) == sizeof(uint8_t) && RT_ELEMENTS(pStreamShared->State.aBdl) == 256);
    HDABDLEDESC TmpDesc = *(HDABDLEDESC *)&pStreamShared->State.aBdl[pStreamShared->State.idxCurBdle];
    rc = pHlp->pfnSSMPutStructEx(pSSM, &TmpDesc, sizeof(TmpDesc), 0 /*fFlags*/, g_aSSMBDLEDescFields7, NULL);
    AssertRCReturn(rc, rc);

    HDABDLESTATELEGACY TmpState = { pStreamShared->State.idxCurBdle, 0, pStreamShared->State.offCurBdle, 0 };
    rc = pHlp->pfnSSMPutStructEx(pSSM, &TmpState, sizeof(TmpState), 0 /*fFlags*/,  g_aSSMBDLEStateFields7, NULL);
    AssertRCReturn(rc, rc);

    PAUDMIXSINK pSink         = NULL;
    uint32_t    cbCircBuf     = 0;
    uint32_t    cbCircBufUsed = 0;
    if (pStreamR3->State.pCircBuf)
    {
        cbCircBuf = (uint32_t)RTCircBufSize(pStreamR3->State.pCircBuf);

        /* We take the AIO lock here and releases it after saving the buffer,
           otherwise the AIO thread could race us reading out the buffer data. */
        pSink = pStreamR3->pMixSink ? pStreamR3->pMixSink->pMixSink : NULL;
        if (   !pSink
            || RT_SUCCESS(AudioMixerSinkTryLock(pSink)))
        {
            cbCircBufUsed = (uint32_t)RTCircBufUsed(pStreamR3->State.pCircBuf);
            if (cbCircBufUsed == 0 && pSink)
                AudioMixerSinkUnlock(pSink);
        }
    }

    pHlp->pfnSSMPutU32(pSSM, cbCircBuf);
    rc = pHlp->pfnSSMPutU32(pSSM, cbCircBufUsed);

    if (cbCircBufUsed > 0)
    {
        /* HACK ALERT! We cannot remove data from the buffer (live snapshot),
                       we use RTCircBufOffsetRead and RTCircBufAcquireReadBlock
                       creatively to get at the other buffer segment in case
                       of a wraparound. */
        size_t const offBuf = RTCircBufOffsetRead(pStreamR3->State.pCircBuf);
        void        *pvBuf  = NULL;
        size_t       cbBuf  = 0;
        RTCircBufAcquireReadBlock(pStreamR3->State.pCircBuf, cbCircBufUsed, &pvBuf, &cbBuf);
        Assert(cbBuf);
        rc = pHlp->pfnSSMPutMem(pSSM, pvBuf, cbBuf);
        if (cbBuf < cbCircBufUsed)
            rc = pHlp->pfnSSMPutMem(pSSM, (uint8_t *)pvBuf - offBuf, cbCircBufUsed - cbBuf);
        RTCircBufReleaseReadBlock(pStreamR3->State.pCircBuf, 0 /* Don't advance read pointer! */);

        if (pSink)
            AudioMixerSinkUnlock(pSink);
    }

    Log2Func(("[SD%RU8] LPIB=%RU32, CBL=%RU32, LVI=%RU32\n", pStreamR3->u8SD, HDA_STREAM_REG(pThis, LPIB, pStreamShared->u8SD),
              HDA_STREAM_REG(pThis, CBL, pStreamShared->u8SD), HDA_STREAM_REG(pThis, LVI, pStreamShared->u8SD)));

#ifdef LOG_ENABLED
    hdaR3BDLEDumpAll(pDevIns, pThis, pStreamShared->u64BDLBase, pStreamShared->u16LVI + 1);
#endif

    return rc;
}

/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) hdaR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PHDASTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PHDASTATE);
    PHDASTATER3   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PHDASTATER3);
    PCPDMDEVHLPR3 pHlp    = pDevIns->pHlpR3;

    /* Save Codec nodes states. */
    hdaCodecSaveState(pDevIns, &pThisCC->Codec, pSSM);

    /* Save MMIO registers. */
    pHlp->pfnSSMPutU32(pSSM, RT_ELEMENTS(pThis->au32Regs));
    pHlp->pfnSSMPutMem(pSSM, pThis->au32Regs, sizeof(pThis->au32Regs));

    /* Save controller-specifc internals. */
    pHlp->pfnSSMPutU64(pSSM, pThis->tsWalClkStart);
    pHlp->pfnSSMPutU8(pSSM, pThis->u8IRQL);

    /* Save number of streams. */
    pHlp->pfnSSMPutU32(pSSM, HDA_MAX_STREAMS);

    /* Save stream states. */
    for (uint8_t i = 0; i < HDA_MAX_STREAMS; i++)
    {
        int rc = hdaR3SaveStream(pDevIns, pSSM, &pThis->aStreams[i], &pThisCC->aStreams[i]);
        AssertRCReturn(rc, rc);
    }

    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNSSMDEVLOADDONE,
 * Finishes stream setup and resuming.}
 */
static DECLCALLBACK(int) hdaR3LoadDone(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PHDASTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PHDASTATE);
    PHDASTATER3   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PHDASTATER3);
    LogFlowFuncEnter();

    /*
     * Enable all previously active streams.
     */
    for (size_t i = 0; i < HDA_MAX_STREAMS; i++)
    {
        PHDASTREAM pStreamShared = &pThis->aStreams[i];

        bool fActive = RT_BOOL(HDA_STREAM_REG(pThis, CTL, i) & HDA_SDCTL_RUN);
        if (fActive)
        {
            PHDASTREAMR3 pStreamR3 = &pThisCC->aStreams[i];

            /* (Re-)enable the stream. */
            int rc2 = hdaR3StreamEnable(pThis, pStreamShared, pStreamR3, true /* fEnable */);
            AssertRC(rc2);

            /* Add the stream to the device setup. */
            rc2 = hdaR3AddStream(pThisCC, &pStreamShared->State.Cfg);
            AssertRC(rc2);

            /* Use the LPIB to find the current scheduling position.  If this isn't
               exactly on a scheduling item adjust LPIB down to the start of the
               current.  This isn't entirely ideal, but it avoid the IRQ counting
               issue if we round it upwards. (it is also a lot simpler) */
            uint32_t uLpib = HDA_STREAM_REG(pThis, LPIB, i);
            AssertLogRelMsgStmt(uLpib < pStreamShared->u32CBL, ("LPIB=%#RX32 CBL=%#RX32\n", uLpib, pStreamShared->u32CBL),
                                HDA_STREAM_REG(pThis, LPIB, i) = uLpib = 0);

            uint32_t off = 0;
            for (uint32_t j = 0; j < pStreamShared->State.cSchedule; j++)
            {
                AssertReturn(pStreamShared->State.aSchedule[j].cbPeriod >= 1 && pStreamShared->State.aSchedule[j].cLoops >= 1,
                             pDevIns->pHlpR3->pfnSSMSetLoadError(pSSM, VERR_INTERNAL_ERROR_2, RT_SRC_POS,
                                                                 "Stream #%u, sched #%u: cbPeriod=%u cLoops=%u\n",
                                                                 pStreamShared->u8SD, j,
                                                                 pStreamShared->State.aSchedule[j].cbPeriod,
                                                                 pStreamShared->State.aSchedule[j].cLoops));
                uint32_t cbCur = pStreamShared->State.aSchedule[j].cbPeriod
                               * pStreamShared->State.aSchedule[j].cLoops;
                if (uLpib >= off + cbCur)
                    off += cbCur;
                else
                {
                    uint32_t const offDelta = uLpib - off;
                    uint32_t idxLoop = offDelta / pStreamShared->State.aSchedule[j].cbPeriod;
                    uint32_t offLoop = offDelta % pStreamShared->State.aSchedule[j].cbPeriod;
                    if (offLoop)
                    {
                        /** @todo somehow bake this into the DMA timer logic.   */
                        LogFunc(("stream #%u: LPIB=%#RX32; adjusting due to scheduling clash: -%#x (j=%u idxLoop=%u cbPeriod=%#x)\n",
                                 pStreamShared->u8SD, uLpib, offLoop, j, idxLoop, pStreamShared->State.aSchedule[j].cbPeriod));
                        uLpib -= offLoop;
                        HDA_STREAM_REG(pThis, LPIB, i) = uLpib;
                    }
                    pStreamShared->State.idxSchedule     = (uint16_t)j;
                    pStreamShared->State.idxScheduleLoop = (uint16_t)idxLoop;
                    off = UINT32_MAX;
                    break;
                }
            }
            Assert(off == UINT32_MAX);

            /* Now figure out the current BDLE and the offset within it. */
            off = 0;
            for (uint32_t j = 0; j < pStreamShared->State.cBdles; j++)
                if (uLpib >= off + pStreamShared->State.aBdl[j].cb)
                    off += pStreamShared->State.aBdl[j].cb;
                else
                {
                    pStreamShared->State.idxCurBdle = j;
                    pStreamShared->State.offCurBdle = uLpib - off;
                    off = UINT32_MAX;
                    break;
                }
            AssertReturn(off == UINT32_MAX, pDevIns->pHlpR3->pfnSSMSetLoadError(pSSM, VERR_INTERNAL_ERROR_3, RT_SRC_POS,
                                                                                "Stream #%u: LPIB=%#RX32 not found in loaded BDL\n",
                                                                                pStreamShared->u8SD, uLpib));

            /* Avoid going through the timer here by calling the stream's timer function directly.
             * Should speed up starting the stream transfers. */
            PDMDevHlpTimerLockClock2(pDevIns, pStreamShared->hTimer, &pThis->CritSect, VERR_IGNORED);
            uint64_t tsNow = hdaR3StreamTimerMain(pDevIns, pThis, pThisCC, pStreamShared, pStreamR3);
            PDMDevHlpTimerUnlockClock2(pDevIns, pStreamShared->hTimer, &pThis->CritSect);

            hdaR3StreamMarkStarted(pDevIns, pThis, pStreamShared, tsNow);
        }
    }

    LogFlowFuncLeave();
    return VINF_SUCCESS;
}

/**
 * Handles loading of all saved state versions older than the current one.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       Pointer to the shared HDA state.
 * @param   pThisCC     Pointer to the ring-3 HDA state.
 * @param   pSSM        The saved state handle.
 * @param   uVersion    Saved state version to load.
 */
static int hdaR3LoadExecLegacy(PPDMDEVINS pDevIns, PHDASTATE pThis, PHDASTATER3 pThisCC, PSSMHANDLE pSSM, uint32_t uVersion)
{
    PCPDMDEVHLPR3 pHlp = pDevIns->pHlpR3;
    int           rc;

    /*
     * Load MMIO registers.
     */
    uint32_t cRegs;
    switch (uVersion)
    {
        case HDA_SAVED_STATE_VERSION_1:
            /* Starting with r71199, we would save 112 instead of 113
               registers due to some code cleanups.  This only affected trunk
               builds in the 4.1 development period. */
            cRegs = 113;
            if (pHlp->pfnSSMHandleRevision(pSSM) >= 71199)
            {
                uint32_t uVer = pHlp->pfnSSMHandleVersion(pSSM);
                if (   VBOX_FULL_VERSION_GET_MAJOR(uVer) == 4
                    && VBOX_FULL_VERSION_GET_MINOR(uVer) == 0
                    && VBOX_FULL_VERSION_GET_BUILD(uVer) >= 51)
                    cRegs = 112;
            }
            break;

        case HDA_SAVED_STATE_VERSION_2:
        case HDA_SAVED_STATE_VERSION_3:
            cRegs = 112;
            AssertCompile(RT_ELEMENTS(pThis->au32Regs) >= 112);
            break;

        /* Since version 4 we store the register count to stay flexible. */
        case HDA_SAVED_STATE_VERSION_4:
        case HDA_SAVED_STATE_VERSION_5:
        case HDA_SAVED_STATE_VERSION_6:
            rc = pHlp->pfnSSMGetU32(pSSM, &cRegs);
            AssertRCReturn(rc, rc);
            if (cRegs != RT_ELEMENTS(pThis->au32Regs))
                LogRel(("HDA: SSM version cRegs is %RU32, expected %RU32\n", cRegs, RT_ELEMENTS(pThis->au32Regs)));
            break;

        default:
            AssertLogRelMsgFailedReturn(("HDA: Internal Error! Didn't expect saved state version %RU32 ending up in hdaR3LoadExecLegacy!\n",
                                         uVersion), VERR_INTERNAL_ERROR_5);
    }

    if (cRegs >= RT_ELEMENTS(pThis->au32Regs))
    {
        pHlp->pfnSSMGetMem(pSSM, pThis->au32Regs, sizeof(pThis->au32Regs));
        pHlp->pfnSSMSkip(pSSM, sizeof(uint32_t) * (cRegs - RT_ELEMENTS(pThis->au32Regs)));
    }
    else
        pHlp->pfnSSMGetMem(pSSM, pThis->au32Regs, sizeof(uint32_t) * cRegs);

    /* Make sure to update the base addresses first before initializing any streams down below. */
    pThis->u64CORBBase  = RT_MAKE_U64(HDA_REG(pThis, CORBLBASE), HDA_REG(pThis, CORBUBASE));
    pThis->u64RIRBBase  = RT_MAKE_U64(HDA_REG(pThis, RIRBLBASE), HDA_REG(pThis, RIRBUBASE));
    pThis->u64DPBase    = RT_MAKE_U64(HDA_REG(pThis, DPLBASE) & DPBASE_ADDR_MASK, HDA_REG(pThis, DPUBASE));

    /* Also make sure to update the DMA position bit if this was enabled when saving the state. */
    pThis->fDMAPosition = RT_BOOL(HDA_REG(pThis, DPLBASE) & RT_BIT_32(0));

    /*
     * Load BDLEs (Buffer Descriptor List Entries) and DMA counters.
     *
     * Note: Saved states < v5 store LVI (u32BdleMaxCvi) for
     *       *every* BDLE state, whereas it only needs to be stored
     *       *once* for every stream. Most of the BDLE state we can
     *       get out of the registers anyway, so just ignore those values.
     *
     *       Also, only the current BDLE was saved, regardless whether
     *       there were more than one (and there are at least two entries,
     *       according to the spec).
     */
    switch (uVersion)
    {
        case HDA_SAVED_STATE_VERSION_1:
        case HDA_SAVED_STATE_VERSION_2:
        case HDA_SAVED_STATE_VERSION_3:
        case HDA_SAVED_STATE_VERSION_4:
        {
            /* Only load the internal states.
             * The rest will be initialized from the saved registers later. */

            /* Note 1: Only the *current* BDLE for a stream was saved! */
            /* Note 2: The stream's saving order is/was fixed, so don't touch! */

            HDABDLELEGACY BDLE;

            /* Output */
            PHDASTREAM pStreamShared = &pThis->aStreams[4];
            rc = hdaR3StreamSetUp(pDevIns, pThis, pStreamShared, &pThisCC->aStreams[4], 4 /* Stream descriptor, hardcoded */);
            AssertRCReturn(rc, rc);
            RT_ZERO(BDLE);
            rc = pHlp->pfnSSMGetStructEx(pSSM, &BDLE, sizeof(BDLE), 0 /* fFlags */, g_aSSMStreamBdleFields1234, pDevIns);
            AssertRCReturn(rc, rc);
            pStreamShared->State.idxCurBdle = (uint8_t)BDLE.State.u32BDLIndex; /* not necessary */

            /* Microphone-In */
            pStreamShared = &pThis->aStreams[2];
            rc = hdaR3StreamSetUp(pDevIns, pThis, pStreamShared, &pThisCC->aStreams[2], 2 /* Stream descriptor, hardcoded */);
            AssertRCReturn(rc, rc);
            rc = pHlp->pfnSSMGetStructEx(pSSM, &BDLE, sizeof(BDLE), 0 /* fFlags */, g_aSSMStreamBdleFields1234, pDevIns);
            AssertRCReturn(rc, rc);
            pStreamShared->State.idxCurBdle = (uint8_t)BDLE.State.u32BDLIndex; /* not necessary */

            /* Line-In */
            pStreamShared = &pThis->aStreams[0];
            rc = hdaR3StreamSetUp(pDevIns, pThis, pStreamShared, &pThisCC->aStreams[0], 0 /* Stream descriptor, hardcoded */);
            AssertRCReturn(rc, rc);
            rc = pHlp->pfnSSMGetStructEx(pSSM, &BDLE, sizeof(BDLE), 0 /* fFlags */, g_aSSMStreamBdleFields1234, pDevIns);
            AssertRCReturn(rc, rc);
            pStreamShared->State.idxCurBdle = (uint8_t)BDLE.State.u32BDLIndex; /* not necessary */
            break;
        }

        /*
         * v5 & v6 - Since v5 we support flexible stream and BDLE counts.
         */
        default:
        {
            /* Stream count. */
            uint32_t cStreams;
            rc = pHlp->pfnSSMGetU32(pSSM, &cStreams);
            AssertRCReturn(rc, rc);
            if (cStreams > HDA_MAX_STREAMS)
                return pHlp->pfnSSMSetLoadError(pSSM, VERR_SSM_DATA_UNIT_FORMAT_CHANGED, RT_SRC_POS,
                                                N_("State contains %u streams while %u is the maximum supported"),
                                                cStreams, HDA_MAX_STREAMS);

            /* Load stream states. */
            for (uint32_t i = 0; i < cStreams; i++)
            {
                uint8_t idStream;
                rc = pHlp->pfnSSMGetU8(pSSM, &idStream);
                AssertRCReturn(rc, rc);

                HDASTREAM    StreamDummyShared;
                HDASTREAMR3  StreamDummyR3;
                PHDASTREAM   pStreamShared = idStream < RT_ELEMENTS(pThis->aStreams) ? &pThis->aStreams[idStream] : &StreamDummyShared;
                PHDASTREAMR3 pStreamR3     = idStream < RT_ELEMENTS(pThisCC->aStreams) ? &pThisCC->aStreams[idStream] : &StreamDummyR3;
                AssertLogRelMsgStmt(idStream < RT_ELEMENTS(pThisCC->aStreams),
                                    ("HDA stream ID=%RU8 not supported, skipping loadingit ...\n", idStream),
                                    RT_ZERO(StreamDummyShared); RT_ZERO(StreamDummyR3));

                rc = hdaR3StreamSetUp(pDevIns, pThis, pStreamShared, pStreamR3, idStream);
                if (RT_FAILURE(rc))
                {
                    LogRel(("HDA: Stream #%RU32: Setting up of stream %RU8 failed, rc=%Rrc\n", i, idStream, rc));
                    break;
                }

                /*
                 * Load BDLEs (Buffer Descriptor List Entries) and DMA counters.
                 */
                if (uVersion == HDA_SAVED_STATE_VERSION_5)
                {
                    struct V5HDASTREAMSTATE /* HDASTREAMSTATE + HDABDLE */
                    {
                        uint16_t cBLDEs;
                        uint16_t uCurBDLE;
                        uint32_t u32BDLEIndex;
                        uint32_t cbBelowFIFOW;
                        uint32_t u32BufOff;
                    } Tmp;
                    static SSMFIELD const g_aV5State1Fields[] =
                    {
                        SSMFIELD_ENTRY(V5HDASTREAMSTATE, cBLDEs),
                        SSMFIELD_ENTRY(V5HDASTREAMSTATE, uCurBDLE),
                        SSMFIELD_ENTRY_TERM()
                    };
                    rc = pHlp->pfnSSMGetStructEx(pSSM, &Tmp, sizeof(Tmp), 0 /* fFlags */, g_aV5State1Fields, NULL);
                    AssertRCReturn(rc, rc);
                    pStreamShared->State.idxCurBdle = (uint8_t)Tmp.uCurBDLE; /* not necessary */

                    for (uint16_t a = 0; a < Tmp.cBLDEs; a++)
                    {
                        static SSMFIELD const g_aV5State2Fields[] =
                        {
                            SSMFIELD_ENTRY(V5HDASTREAMSTATE, u32BDLEIndex),
                            SSMFIELD_ENTRY_OLD(au8FIFO, 256),
                            SSMFIELD_ENTRY(V5HDASTREAMSTATE, cbBelowFIFOW),
                            SSMFIELD_ENTRY_TERM()
                        };
                        rc = pHlp->pfnSSMGetStructEx(pSSM, &Tmp, sizeof(Tmp), 0 /* fFlags */, g_aV5State2Fields, NULL);
                        AssertRCReturn(rc, rc);
                    }
                }
                else
                {
                    rc = pHlp->pfnSSMGetStructEx(pSSM, &pStreamShared->State, sizeof(HDASTREAMSTATE),
                                                 0 /* fFlags */, g_aSSMStreamStateFields6, NULL);
                    AssertRCReturn(rc, rc);

                    HDABDLEDESC IgnDesc;
                    rc = pHlp->pfnSSMGetStructEx(pSSM, &IgnDesc, sizeof(IgnDesc), 0 /* fFlags */, g_aSSMBDLEDescFields6, pDevIns);
                    AssertRCReturn(rc, rc);

                    HDABDLESTATELEGACY IgnState;
                    rc = pHlp->pfnSSMGetStructEx(pSSM, &IgnState, sizeof(IgnState), 0 /* fFlags */, g_aSSMBDLEStateFields6, NULL);
                    AssertRCReturn(rc, rc);

                    Log2Func(("[SD%RU8] LPIB=%RU32, CBL=%RU32, LVI=%RU32\n", idStream, HDA_STREAM_REG(pThis, LPIB, idStream),
                              HDA_STREAM_REG(pThis, CBL, idStream), HDA_STREAM_REG(pThis, LVI, idStream)));
#ifdef LOG_ENABLED
                    hdaR3BDLEDumpAll(pDevIns, pThis, pStreamShared->u64BDLBase, pStreamShared->u16LVI + 1);
#endif
                }

            } /* for cStreams */
            break;
        } /* default */
    }

    return rc;
}

/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) hdaR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PHDASTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PHDASTATE);
    PHDASTATER3   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PHDASTATER3);
    PCPDMDEVHLPR3 pHlp    = pDevIns->pHlpR3;

    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);

    LogRel2(("hdaR3LoadExec: uVersion=%RU32, uPass=0x%x\n", uVersion, uPass));

    /*
     * Load Codec nodes states.
     */
    int rc = hdaR3CodecLoadState(pDevIns, &pThisCC->Codec, pSSM, uVersion);
    if (RT_FAILURE(rc))
    {
        LogRel(("HDA: Failed loading codec state (version %RU32, pass 0x%x), rc=%Rrc\n", uVersion, uPass, rc));
        return rc;
    }

    if (uVersion <= HDA_SAVED_STATE_VERSION_6) /* Handle older saved states? */
        return hdaR3LoadExecLegacy(pDevIns, pThis, pThisCC, pSSM, uVersion);

    /*
     * Load MMIO registers.
     */
    uint32_t cRegs;
    rc = pHlp->pfnSSMGetU32(pSSM, &cRegs); AssertRCReturn(rc, rc);
    AssertRCReturn(rc, rc);
    if (cRegs != RT_ELEMENTS(pThis->au32Regs))
        LogRel(("HDA: SSM version cRegs is %RU32, expected %RU32\n", cRegs, RT_ELEMENTS(pThis->au32Regs)));

    if (cRegs >= RT_ELEMENTS(pThis->au32Regs))
    {
        pHlp->pfnSSMGetMem(pSSM, pThis->au32Regs, sizeof(pThis->au32Regs));
        rc = pHlp->pfnSSMSkip(pSSM, sizeof(uint32_t) * (cRegs - RT_ELEMENTS(pThis->au32Regs)));
        AssertRCReturn(rc, rc);
    }
    else
    {
        rc = pHlp->pfnSSMGetMem(pSSM, pThis->au32Regs, sizeof(uint32_t) * cRegs);
        AssertRCReturn(rc, rc);
    }

    /* Make sure to update the base addresses first before initializing any streams down below. */
    pThis->u64CORBBase  = RT_MAKE_U64(HDA_REG(pThis, CORBLBASE), HDA_REG(pThis, CORBUBASE));
    pThis->u64RIRBBase  = RT_MAKE_U64(HDA_REG(pThis, RIRBLBASE), HDA_REG(pThis, RIRBUBASE));
    pThis->u64DPBase    = RT_MAKE_U64(HDA_REG(pThis, DPLBASE) & DPBASE_ADDR_MASK, HDA_REG(pThis, DPUBASE));

    /* Also make sure to update the DMA position bit if this was enabled when saving the state. */
    pThis->fDMAPosition = RT_BOOL(HDA_REG(pThis, DPLBASE) & RT_BIT_32(0));

    /*
     * Load controller-specific internals.
     */
    if (   uVersion >= HDA_SAVED_STATE_WITHOUT_PERIOD
        /* Don't annoy other team mates (forgot this for state v7): */
        || pHlp->pfnSSMHandleRevision(pSSM) >= 116273
        || pHlp->pfnSSMHandleVersion(pSSM)  >= VBOX_FULL_VERSION_MAKE(5, 2, 0))
    {
        pHlp->pfnSSMGetU64(pSSM, &pThis->tsWalClkStart); /* Was current wall clock */
        rc = pHlp->pfnSSMGetU8(pSSM, &pThis->u8IRQL);
        AssertRCReturn(rc, rc);

        /* Convert the saved wall clock timestamp to a start timestamp. */
        if (uVersion < HDA_SAVED_STATE_WITHOUT_PERIOD && pThis->tsWalClkStart != 0)
        {
            uint64_t const cTimerTicksPerSec = PDMDevHlpTimerGetFreq(pDevIns, pThis->aStreams[0].hTimer);
            AssertLogRel(cTimerTicksPerSec <= UINT32_MAX);
            pThis->tsWalClkStart = ASMMultU64ByU32DivByU32(pThis->tsWalClkStart,
                                                           cTimerTicksPerSec,
                                                           24000000 /* wall clock freq */);
            pThis->tsWalClkStart = PDMDevHlpTimerGet(pDevIns, pThis->aStreams[0].hTimer) - pThis->tsWalClkStart;
        }
    }

    /*
     * Load streams.
     */
    uint32_t cStreams;
    rc = pHlp->pfnSSMGetU32(pSSM, &cStreams);
    AssertRCReturn(rc, rc);
    if (cStreams > HDA_MAX_STREAMS)
        return pHlp->pfnSSMSetLoadError(pSSM, VERR_SSM_DATA_UNIT_FORMAT_CHANGED, RT_SRC_POS,
                                        N_("State contains %u streams while %u is the maximum supported"),
                                        cStreams, HDA_MAX_STREAMS);
    Log2Func(("cStreams=%RU32\n", cStreams));

    /* Load stream states. */
    for (uint32_t i = 0; i < cStreams; i++)
    {
        uint8_t idStream;
        rc = pHlp->pfnSSMGetU8(pSSM, &idStream);
        AssertRCReturn(rc, rc);

        /* Paranoia. */
        AssertLogRelMsgReturn(idStream < HDA_MAX_STREAMS,
                              ("HDA: Saved state contains bogus stream ID %RU8 for stream #%RU8", idStream, i),
                              VERR_SSM_INVALID_STATE);

        HDASTREAM    StreamDummyShared;
        HDASTREAMR3  StreamDummyR3;
        PHDASTREAM   pStreamShared = idStream < RT_ELEMENTS(pThis->aStreams) ? &pThis->aStreams[idStream] : &StreamDummyShared;
        PHDASTREAMR3 pStreamR3     = idStream < RT_ELEMENTS(pThisCC->aStreams) ? &pThisCC->aStreams[idStream] : &StreamDummyR3;
        AssertLogRelMsgStmt(idStream < RT_ELEMENTS(pThisCC->aStreams),
                            ("HDA stream ID=%RU8 not supported, skipping loadingit ...\n", idStream),
                            RT_ZERO(StreamDummyShared); RT_ZERO(StreamDummyR3));

        rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED); /* timer code requires this */
        AssertRCReturn(rc, rc);
        rc = hdaR3StreamSetUp(pDevIns, pThis, pStreamShared, pStreamR3, idStream);
        PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
        if (RT_FAILURE(rc))
        {
            LogRel(("HDA: Stream #%RU8: Setting up failed, rc=%Rrc\n", idStream, rc));
            /* Continue. */
        }

        rc = pHlp->pfnSSMGetStructEx(pSSM, &pStreamShared->State, sizeof(HDASTREAMSTATE),
                                     0 /* fFlags */, g_aSSMStreamStateFields7, NULL);
        AssertRCReturn(rc, rc);

        /*
         * Load BDLEs (Buffer Descriptor List Entries) and DMA counters.
         * Obsolete. Derived from LPID now.
         */
        HDABDLEDESC IgnDesc;
        rc = pHlp->pfnSSMGetStructEx(pSSM, &IgnDesc, sizeof(IgnDesc), 0 /* fFlags */, g_aSSMBDLEDescFields7, NULL);
        AssertRCReturn(rc, rc);

        HDABDLESTATELEGACY IgnState;
        rc = pHlp->pfnSSMGetStructEx(pSSM, &IgnState, sizeof(IgnState), 0 /* fFlags */, g_aSSMBDLEStateFields7, NULL);
        AssertRCReturn(rc, rc);

        Log2Func(("[SD%RU8]\n", pStreamShared->u8SD));

        /*
         * Load period state if present.
         */
        if (uVersion < HDA_SAVED_STATE_WITHOUT_PERIOD)
        {
            static SSMFIELD const s_aSSMStreamPeriodFields7[] = /* For the removed HDASTREAMPERIOD structure. */
            {
                SSMFIELD_ENTRY_OLD(u64StartWalClk,     sizeof(uint64_t)),
                SSMFIELD_ENTRY_OLD(u64ElapsedWalClk,   sizeof(uint64_t)),
                SSMFIELD_ENTRY_OLD(cFramesTransferred, sizeof(uint32_t)),
                SSMFIELD_ENTRY_OLD(cIntPending, sizeof(uint8_t)),   /** @todo Not sure what we should for non-zero values on restore... ignoring it for now.  */
                SSMFIELD_ENTRY_TERM()
            };
            uint8_t bWhatever = 0;
            rc = pHlp->pfnSSMGetStructEx(pSSM, &bWhatever, sizeof(bWhatever), 0 /* fFlags */, s_aSSMStreamPeriodFields7, NULL);
            AssertRCReturn(rc, rc);
        }

        /*
         * Load internal DMA buffer.
         */
        uint32_t cbCircBuf = 0;
        pHlp->pfnSSMGetU32(pSSM, &cbCircBuf); /* cbCircBuf */
        uint32_t cbCircBufUsed = 0;
        rc = pHlp->pfnSSMGetU32(pSSM, &cbCircBufUsed); /* cbCircBuf */
        AssertRCReturn(rc, rc);

        if (cbCircBuf) /* If 0, skip the buffer. */
        {
            /* Paranoia. */
            AssertLogRelMsgReturn(cbCircBuf <= _32M,
                                  ("HDA: Saved state contains bogus DMA buffer size (%RU32) for stream #%RU8",
                                   cbCircBuf, idStream),
                                  VERR_SSM_DATA_UNIT_FORMAT_CHANGED);
            AssertLogRelMsgReturn(cbCircBufUsed <= cbCircBuf,
                                  ("HDA: Saved state contains invalid DMA buffer usage (%RU32/%RU32) for stream #%RU8",
                                   cbCircBufUsed, cbCircBuf, idStream),
                                  VERR_SSM_DATA_UNIT_FORMAT_CHANGED);

            /* Do we need to cre-create the circular buffer do fit the data size? */
            if (   pStreamR3->State.pCircBuf
                && cbCircBuf != (uint32_t)RTCircBufSize(pStreamR3->State.pCircBuf))
            {
                RTCircBufDestroy(pStreamR3->State.pCircBuf);
                pStreamR3->State.pCircBuf = NULL;
            }

            rc = RTCircBufCreate(&pStreamR3->State.pCircBuf, cbCircBuf);
            AssertRCReturn(rc, rc);
            pStreamR3->State.StatDmaBufSize = cbCircBuf;

            if (cbCircBufUsed)
            {
                void  *pvBuf = NULL;
                size_t cbBuf = 0;
                RTCircBufAcquireWriteBlock(pStreamR3->State.pCircBuf, cbCircBufUsed, &pvBuf, &cbBuf);

                AssertLogRelMsgReturn(cbBuf == cbCircBufUsed, ("cbBuf=%zu cbCircBufUsed=%zu\n", cbBuf, cbCircBufUsed),
                                      VERR_INTERNAL_ERROR_3);
                rc = pHlp->pfnSSMGetMem(pSSM, pvBuf, cbBuf);
                AssertRCReturn(rc, rc);
                pStreamShared->State.offWrite = cbCircBufUsed;

                RTCircBufReleaseWriteBlock(pStreamR3->State.pCircBuf, cbBuf);

                Assert(cbBuf == cbCircBufUsed);
            }
        }

        Log2Func(("[SD%RU8] LPIB=%RU32, CBL=%RU32, LVI=%RU32\n", idStream, HDA_STREAM_REG(pThis, LPIB, idStream),
                  HDA_STREAM_REG(pThis, CBL, idStream), HDA_STREAM_REG(pThis, LVI, idStream)));
#ifdef LOG_ENABLED
        hdaR3BDLEDumpAll(pDevIns, pThis, pStreamShared->u64BDLBase, pStreamShared->u16LVI + 1);
#endif
        /** @todo (Re-)initialize active periods? */

    } /* for cStreams */

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/*********************************************************************************************************************************
*   IPRT format type handlers                                                                                                    *
*********************************************************************************************************************************/

/**
 * @callback_method_impl{FNRTSTRFORMATTYPE}
 */
static DECLCALLBACK(size_t) hdaR3StrFmtSDCTL(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                                             const char *pszType, void const *pvValue,
                                             int cchWidth, int cchPrecision, unsigned fFlags,
                                             void *pvUser)
{
    RT_NOREF(pszType, cchWidth,  cchPrecision, fFlags, pvUser);
    uint32_t uSDCTL = (uint32_t)(uintptr_t)pvValue;
    return RTStrFormat(pfnOutput, pvArgOutput, NULL, 0,
                       "SDCTL(raw:%#x, DIR:%s, TP:%RTbool, STRIPE:%x, DEIE:%RTbool, FEIE:%RTbool, IOCE:%RTbool, RUN:%RTbool, RESET:%RTbool)",
                       uSDCTL,
                       uSDCTL & HDA_SDCTL_DIR ? "OUT" : "IN",
                       RT_BOOL(uSDCTL & HDA_SDCTL_TP),
                       (uSDCTL & HDA_SDCTL_STRIPE_MASK) >> HDA_SDCTL_STRIPE_SHIFT,
                       RT_BOOL(uSDCTL & HDA_SDCTL_DEIE),
                       RT_BOOL(uSDCTL & HDA_SDCTL_FEIE),
                       RT_BOOL(uSDCTL & HDA_SDCTL_IOCE),
                       RT_BOOL(uSDCTL & HDA_SDCTL_RUN),
                       RT_BOOL(uSDCTL & HDA_SDCTL_SRST));
}

/**
 * @callback_method_impl{FNRTSTRFORMATTYPE}
 */
static DECLCALLBACK(size_t) hdaR3StrFmtSDFIFOS(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                                               const char *pszType, void const *pvValue,
                                               int cchWidth, int cchPrecision, unsigned fFlags,
                                               void *pvUser)
{
    RT_NOREF(pszType, cchWidth,  cchPrecision, fFlags, pvUser);
    uint32_t uSDFIFOS = (uint32_t)(uintptr_t)pvValue;
    return RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, "SDFIFOS(raw:%#x, sdfifos:%RU8 B)", uSDFIFOS, uSDFIFOS ? uSDFIFOS + 1 : 0);
}

/**
 * @callback_method_impl{FNRTSTRFORMATTYPE}
 */
static DECLCALLBACK(size_t) hdaR3StrFmtSDFIFOW(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                                               const char *pszType, void const *pvValue,
                                               int cchWidth, int cchPrecision, unsigned fFlags,
                                               void *pvUser)
{
    RT_NOREF(pszType, cchWidth,  cchPrecision, fFlags, pvUser);
    uint32_t uSDFIFOW = (uint32_t)(uintptr_t)pvValue;
    return RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, "SDFIFOW(raw: %#0x, sdfifow:%d B)", uSDFIFOW, hdaSDFIFOWToBytes(uSDFIFOW));
}

/**
 * @callback_method_impl{FNRTSTRFORMATTYPE}
 */
static DECLCALLBACK(size_t) hdaR3StrFmtSDSTS(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                                             const char *pszType, void const *pvValue,
                                             int cchWidth, int cchPrecision, unsigned fFlags,
                                             void *pvUser)
{
    RT_NOREF(pszType, cchWidth,  cchPrecision, fFlags, pvUser);
    uint32_t uSdSts = (uint32_t)(uintptr_t)pvValue;
    return RTStrFormat(pfnOutput, pvArgOutput, NULL, 0,
                       "SDSTS(raw:%#0x, fifordy:%RTbool, dese:%RTbool, fifoe:%RTbool, bcis:%RTbool)",
                       uSdSts,
                       RT_BOOL(uSdSts & HDA_SDSTS_FIFORDY),
                       RT_BOOL(uSdSts & HDA_SDSTS_DESE),
                       RT_BOOL(uSdSts & HDA_SDSTS_FIFOE),
                       RT_BOOL(uSdSts & HDA_SDSTS_BCIS));
}


/*********************************************************************************************************************************
*   Debug Info Item Handlers                                                                                                     *
*********************************************************************************************************************************/

/** Worker for hdaR3DbgInfo. */
static int hdaR3DbgLookupRegByName(const char *pszArgs)
{
    if (pszArgs && *pszArgs != '\0')
        for (int iReg = 0; iReg < HDA_NUM_REGS; ++iReg)
            if (!RTStrICmp(g_aHdaRegMap[iReg].pszName, pszArgs))
                return iReg;
    return -1;
}

/** Worker for hdaR3DbgInfo.  */
static void hdaR3DbgPrintRegister(PPDMDEVINS pDevIns, PHDASTATE pThis, PCDBGFINFOHLP pHlp, int iHdaIndex)
{
    /** @todo HDA_REG_IDX_NOMEM & GCAP both uses idxReg zero, no flag or anything
     *        to tell them appart. */
    if (g_aHdaRegMap[iHdaIndex].idxReg != 0 || g_aHdaRegMap[iHdaIndex].pfnRead != hdaRegReadWALCLK)
        pHlp->pfnPrintf(pHlp, "%s: 0x%x\n", g_aHdaRegMap[iHdaIndex].pszName, pThis->au32Regs[g_aHdaRegMap[iHdaIndex].idxReg]);
    else
    {
        uint64_t uWallNow = 0;
        hdaQueryWallClock(pDevIns, pThis, false /*fDoDma*/, &uWallNow);
        pHlp->pfnPrintf(pHlp, "%s: 0x%RX64\n", g_aHdaRegMap[iHdaIndex].pszName, uWallNow);
    }
}

/**
 * @callback_method_impl{FNDBGFHANDLERDEV}
 */
static DECLCALLBACK(void) hdaR3DbgInfo(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PHDASTATE pThis = PDMDEVINS_2_DATA(pDevIns, PHDASTATE);
    int idxReg = hdaR3DbgLookupRegByName(pszArgs);
    if (idxReg != -1)
        hdaR3DbgPrintRegister(pDevIns, pThis, pHlp, idxReg);
    else
        for (idxReg = 0; idxReg < HDA_NUM_REGS; ++idxReg)
            hdaR3DbgPrintRegister(pDevIns, pThis, pHlp, idxReg);
}

/** Worker for hdaR3DbgInfoStream.    */
static void hdaR3DbgPrintStream(PHDASTATE pThis, PCDBGFINFOHLP pHlp, int idxStream)
{
    char szTmp[PDMAUDIOSTRMCFGTOSTRING_MAX];
    PHDASTREAM const pStream = &pThis->aStreams[idxStream];
    pHlp->pfnPrintf(pHlp, "Stream #%d: %s\n", idxStream, PDMAudioStrmCfgToString(&pStream->State.Cfg, szTmp, sizeof(szTmp)));
    pHlp->pfnPrintf(pHlp, "  SD%dCTL  : %R[sdctl]\n",   idxStream, HDA_STREAM_REG(pThis, CTL,   idxStream));
    pHlp->pfnPrintf(pHlp, "  SD%dCTS  : %R[sdsts]\n",   idxStream, HDA_STREAM_REG(pThis, STS,   idxStream));
    pHlp->pfnPrintf(pHlp, "  SD%dFIFOS: %R[sdfifos]\n", idxStream, HDA_STREAM_REG(pThis, FIFOS, idxStream));
    pHlp->pfnPrintf(pHlp, "  SD%dFIFOW: %R[sdfifow]\n", idxStream, HDA_STREAM_REG(pThis, FIFOW, idxStream));
    pHlp->pfnPrintf(pHlp, "  Current BDLE%02u: %s%#RX64 LB %#x%s - off=%#x\n", pStream->State.idxCurBdle, "%%" /*vboxdbg phys prefix*/,
                    pStream->State.aBdl[pStream->State.idxCurBdle].GCPhys, pStream->State.aBdl[pStream->State.idxCurBdle].cb,
                    pStream->State.aBdl[pStream->State.idxCurBdle].fFlags ? " IOC" : "", pStream->State.offCurBdle);
}

/** Worker for hdaR3DbgInfoBDL. */
static void hdaR3DbgPrintBDL(PPDMDEVINS pDevIns, PHDASTATE pThis, PCDBGFINFOHLP pHlp, int idxStream)
{
    const PHDASTREAM   pStream  = &pThis->aStreams[idxStream];
    PCPDMAUDIOPCMPROPS pProps   = &pStream->State.Cfg.Props;
    uint64_t const u64BaseDMA   = RT_MAKE_U64(HDA_STREAM_REG(pThis, BDPL, idxStream),
                                              HDA_STREAM_REG(pThis, BDPU, idxStream));
    uint16_t const u16LVI       = HDA_STREAM_REG(pThis, LVI, idxStream);
    uint32_t const u32CBL       = HDA_STREAM_REG(pThis, CBL, idxStream);
    uint8_t const  idxCurBdle   = pStream->State.idxCurBdle;
    pHlp->pfnPrintf(pHlp, "Stream #%d BDL: %s%#011RX64 LB %#x (LVI=%u)\n", idxStream, "%%" /*vboxdbg phys prefix*/,
                    u64BaseDMA, u16LVI * sizeof(HDABDLEDESC), u16LVI);
    if (u64BaseDMA || idxCurBdle != 0 || pStream->State.aBdl[idxCurBdle].GCPhys != 0 || pStream->State.aBdl[idxCurBdle].cb != 0)
        pHlp->pfnPrintf(pHlp, "  Current:     BDLE%03u: %s%#011RX64 LB %#x%s - off=%#x  LPIB=%#RX32\n",
                        pStream->State.idxCurBdle, "%%" /*vboxdbg phys prefix*/,
                        pStream->State.aBdl[idxCurBdle].GCPhys, pStream->State.aBdl[idxCurBdle].cb,
                        pStream->State.aBdl[idxCurBdle].fFlags ? " IOC" : "", pStream->State.offCurBdle,
                        HDA_STREAM_REG(pThis, LPIB, idxStream));
    if (!u64BaseDMA)
        return;

    /*
     * The BDL:
     */
    uint64_t cbTotal = 0;
    for (uint16_t i = 0; i < u16LVI + 1; i++)
    {
        HDABDLEDESC bd = {0, 0, 0};
        PDMDevHlpPCIPhysRead(pDevIns, u64BaseDMA + i * sizeof(HDABDLEDESC), &bd, sizeof(bd));

        char szFlags[64];
        szFlags[0] = '\0';
        if (bd.fFlags & ~HDA_BDLE_F_IOC)
            RTStrPrintf(szFlags, sizeof(szFlags), " !!fFlags=%#x!!\n", bd.fFlags);
        pHlp->pfnPrintf(pHlp, "    %sBDLE%03u: %s%#011RX64 LB %#06x (%RU64 us) %s%s\n", idxCurBdle == i ? "=>" : "  ", i, "%%",
                        bd.u64BufAddr, bd.u32BufSize, PDMAudioPropsBytesToMicro(pProps, bd.u32BufSize),
                        bd.fFlags & HDA_BDLE_F_IOC ? " IOC=1" : "", szFlags);

        if (memcmp(&bd, &pStream->State.aBdl[i], sizeof(bd)) != 0)
        {
            szFlags[0] = '\0';
            if (bd.fFlags & ~HDA_BDLE_F_IOC)
                RTStrPrintf(szFlags, sizeof(szFlags), " !!fFlags=%#x!!\n", bd.fFlags);
            pHlp->pfnPrintf(pHlp, "    !!!loaded: %s%#011RX64 LB %#06x %s%s\n", "%%", pStream->State.aBdl[i].GCPhys,
                            pStream->State.aBdl[i].cb, pStream->State.aBdl[i].fFlags & HDA_BDLE_F_IOC ? " IOC=1" : "", szFlags);
        }

        cbTotal += bd.u32BufSize;
    }
    pHlp->pfnPrintf(pHlp, "  Total: %#RX64 bytes (%RU64), %RU64 ms\n", cbTotal, cbTotal,
                    PDMAudioPropsBytesToMilli(pProps, (uint32_t)cbTotal));
    if (cbTotal != u32CBL)
        pHlp->pfnPrintf(pHlp, "  Warning: %#RX64 bytes does not match CBL (%#RX64)!\n", cbTotal, u32CBL);

    /*
     * The scheduling plan.
     */
    uint16_t const idxSchedule = pStream->State.idxSchedule;
    pHlp->pfnPrintf(pHlp, "  Scheduling: %u items, %u prologue.  Current: %u, loop %u.\n",  pStream->State.cSchedule,
                    pStream->State.cSchedulePrologue, idxSchedule, pStream->State.idxScheduleLoop);
    for (uint16_t i = 0; i < pStream->State.cSchedule; i++)
        pHlp->pfnPrintf(pHlp, "    %s#%02u: %#x bytes, %u loop%s, %RU32 ticks. BDLE%u thru BDLE%u\n",
                        i == idxSchedule ? "=>" : "  ", i,
                        pStream->State.aSchedule[i].cbPeriod, pStream->State.aSchedule[i].cLoops,
                        pStream->State.aSchedule[i].cLoops == 1 ? "" : "s",
                        pStream->State.aSchedule[i].cPeriodTicks, pStream->State.aSchedule[i].idxFirst,
                        pStream->State.aSchedule[i].idxFirst + pStream->State.aSchedule[i].cEntries - 1);
}

/** Used by hdaR3DbgInfoStream and hdaR3DbgInfoBDL. */
static int hdaR3DbgLookupStrmIdx(PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    if (pszArgs && *pszArgs)
    {
        int32_t idxStream;
        int rc = RTStrToInt32Full(pszArgs, 0, &idxStream);
        if (RT_SUCCESS(rc) && idxStream >= -1 && idxStream < HDA_MAX_STREAMS)
            return idxStream;
        pHlp->pfnPrintf(pHlp, "Argument '%s' is not a valid stream number!\n", pszArgs);
    }
    return -1;
}

/**
 * @callback_method_impl{FNDBGFHANDLERDEV, hdastream}
 */
static DECLCALLBACK(void) hdaR3DbgInfoStream(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PHDASTATE   pThis     = PDMDEVINS_2_DATA(pDevIns, PHDASTATE);
    int         idxStream = hdaR3DbgLookupStrmIdx(pHlp, pszArgs);
    if (idxStream != -1)
        hdaR3DbgPrintStream(pThis, pHlp, idxStream);
    else
        for (idxStream = 0; idxStream < HDA_MAX_STREAMS; ++idxStream)
            hdaR3DbgPrintStream(pThis, pHlp, idxStream);
}

/**
 * @callback_method_impl{FNDBGFHANDLERDEV, hdabdl}
 */
static DECLCALLBACK(void) hdaR3DbgInfoBDL(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PHDASTATE   pThis     = PDMDEVINS_2_DATA(pDevIns, PHDASTATE);
    int         idxStream = hdaR3DbgLookupStrmIdx(pHlp, pszArgs);
    if (idxStream != -1)
        hdaR3DbgPrintBDL(pDevIns, pThis, pHlp, idxStream);
    else
    {
        for (idxStream = 0; idxStream < HDA_MAX_STREAMS; ++idxStream)
            hdaR3DbgPrintBDL(pDevIns, pThis, pHlp, idxStream);
        idxStream = -1;
    }

    /*
     * DMA stream positions:
     */
    uint64_t const uDPBase = pThis->u64DPBase & DPBASE_ADDR_MASK;
    pHlp->pfnPrintf(pHlp, "DMA counters %#011RX64 LB %#x, %s:\n", uDPBase, HDA_MAX_STREAMS * 2 * sizeof(uint32_t),
                    pThis->fDMAPosition ? "enabled" : "disabled");
    if (uDPBase)
    {
        struct
        {
            uint32_t off, uReserved;
        } aPositions[HDA_MAX_STREAMS];
        RT_ZERO(aPositions);
        PDMDevHlpPCIPhysRead(pDevIns, uDPBase , &aPositions[0], sizeof(aPositions));

        for (unsigned i = 0; i < RT_ELEMENTS(aPositions); i++)
            if (idxStream == -1 || i == (unsigned)idxStream) /* lazy bird */
            {
                char szReserved[64];
                szReserved[0] = '\0';
                if (aPositions[i].uReserved != 0)
                    RTStrPrintf(szReserved, sizeof(szReserved), " reserved=%#x", aPositions[i].uReserved);
                pHlp->pfnPrintf(pHlp, "  Stream #%u DMA @ %#x%s\n", i, aPositions[i].off, szReserved);
            }
    }
}

/**
 * @callback_method_impl{FNDBGFHANDLERDEV, hdcnodes}
 */
static DECLCALLBACK(void) hdaR3DbgInfoCodecNodes(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PHDASTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PHDASTATER3);
    hdaR3CodecDbgListNodes(&pThisCC->Codec, pHlp, pszArgs);
}

/**
 * @callback_method_impl{FNDBGFHANDLERDEV, hdcselector}
 */
static DECLCALLBACK(void) hdaR3DbgInfoCodecSelector(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PHDASTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PHDASTATER3);
    hdaR3CodecDbgSelector(&pThisCC->Codec, pHlp, pszArgs);
}

/**
 * @callback_method_impl{FNDBGFHANDLERDEV, hdamixer}
 */
static DECLCALLBACK(void) hdaR3DbgInfoMixer(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PHDASTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PHDASTATER3);
    if (pThisCC->pMixer)
        AudioMixerDebug(pThisCC->pMixer, pHlp, pszArgs);
    else
        pHlp->pfnPrintf(pHlp, "Mixer not available\n");
}


/*********************************************************************************************************************************
*   PDMIBASE                                                                                                                     *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) hdaR3QueryInterface(struct PDMIBASE *pInterface, const char *pszIID)
{
    PHDASTATER3 pThisCC = RT_FROM_MEMBER(pInterface, HDASTATER3, IBase);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThisCC->IBase);
    return NULL;
}


/*********************************************************************************************************************************
*   PDMDEVREGR3                                                                                                                  *
*********************************************************************************************************************************/

/**
 * Worker for hdaR3Construct() and hdaR3Attach().
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The shared HDA device state.
 * @param   pThisCC     The ring-3 HDA device state.
 * @param   uLUN        The logical unit which is being detached.
 * @param   ppDrv       Attached driver instance on success. Optional.
 */
static int hdaR3AttachInternal(PPDMDEVINS pDevIns, PHDASTATE pThis, PHDASTATER3 pThisCC, unsigned uLUN, PHDADRIVER *ppDrv)
{
    PHDADRIVER pDrv = (PHDADRIVER)RTMemAllocZ(sizeof(HDADRIVER));
    AssertPtrReturn(pDrv, VERR_NO_MEMORY);
    RTStrPrintf(pDrv->szDesc, sizeof(pDrv->szDesc), "Audio driver port (HDA) for LUN #%u", uLUN);

    PPDMIBASE pDrvBase;
    int rc = PDMDevHlpDriverAttach(pDevIns, uLUN, &pThisCC->IBase, &pDrvBase, pDrv->szDesc);
    if (RT_SUCCESS(rc))
    {
        pDrv->pConnector = PDMIBASE_QUERY_INTERFACE(pDrvBase, PDMIAUDIOCONNECTOR);
        AssertPtr(pDrv->pConnector);
        if (RT_VALID_PTR(pDrv->pConnector))
        {
            pDrv->pDrvBase          = pDrvBase;
            pDrv->pHDAStateShared   = pThis;
            pDrv->pHDAStateR3       = pThisCC;
            pDrv->uLUN              = uLUN;

            /* Attach to driver list if not attached yet. */
            if (!pDrv->fAttached)
            {
                RTListAppend(&pThisCC->lstDrv, &pDrv->Node);
                pDrv->fAttached = true;
            }

            if (ppDrv)
                *ppDrv = pDrv;

            /*
             * While we're here, give the windows backends a hint about our typical playback
             * configuration.
             * Note! If 48000Hz is advertised to the guest, add it here.
             */
            if (   pDrv->pConnector
                && pDrv->pConnector->pfnStreamConfigHint)
            {
                PDMAUDIOSTREAMCFG Cfg;
                RT_ZERO(Cfg);
                Cfg.enmDir                        = PDMAUDIODIR_OUT;
                Cfg.enmPath                       = PDMAUDIOPATH_OUT_FRONT;
                Cfg.Device.cMsSchedulingHint      = 10;
                Cfg.Backend.cFramesPreBuffering   = UINT32_MAX;
                PDMAudioPropsInit(&Cfg.Props, 2, true /*fSigned*/, 2, 44100);
                RTStrPrintf(Cfg.szName, sizeof(Cfg.szName), "output 44.1kHz 2ch S16 (HDA config hint)");

                pDrv->pConnector->pfnStreamConfigHint(pDrv->pConnector, &Cfg); /* (may trash CfgReq) */
            }

            LogFunc(("LUN#%u: returns VINF_SUCCESS (pCon=%p)\n", uLUN, pDrv->pConnector));
            return VINF_SUCCESS;
        }

        rc = VERR_PDM_MISSING_INTERFACE_BELOW;
    }
    else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
        LogFunc(("No attached driver for LUN #%u\n", uLUN));
    else
        LogFunc(("Failed attaching driver for LUN #%u: %Rrc\n", uLUN, rc));
    RTMemFree(pDrv);

    LogFunc(("LUN#%u: rc=%Rrc\n", uLUN, rc));
    return rc;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnAttach}
 */
static DECLCALLBACK(int) hdaR3Attach(PPDMDEVINS pDevIns, unsigned uLUN, uint32_t fFlags)
{
    PHDASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PHDASTATE);
    PHDASTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PHDASTATER3);
    RT_NOREF(fFlags);
    LogFunc(("uLUN=%u, fFlags=0x%x\n", uLUN, fFlags));

    DEVHDA_LOCK_RETURN(pDevIns, pThis, VERR_IGNORED);

    PHDADRIVER pDrv;
    int rc = hdaR3AttachInternal(pDevIns, pThis, pThisCC, uLUN, &pDrv);
    if (RT_SUCCESS(rc))
    {
        int rc2 = hdaR3MixerAddDrv(pDevIns, pThisCC, pDrv);
        if (RT_FAILURE(rc2))
            LogFunc(("hdaR3MixerAddDrv failed with %Rrc (ignored)\n", rc2));
    }

    DEVHDA_UNLOCK(pDevIns, pThis);
    return rc;
}


/**
 * Worker for hdaR3Detach that does all but free pDrv.
 *
 * This is called to let the device detach from a driver for a specified LUN
 * at runtime.
 *
 * @param   pDevIns     The device instance.
 * @param   pThisCC     The ring-3 HDA device state.
 * @param   pDrv        Driver to detach from device.
 */
static void hdaR3DetachInternal(PPDMDEVINS pDevIns, PHDASTATER3 pThisCC, PHDADRIVER pDrv)
{
    /* Remove the driver from our list and destory it's associated streams.
       This also will un-set the driver as a recording source (if associated). */
    hdaR3MixerRemoveDrv(pDevIns, pThisCC, pDrv);
    LogFunc(("LUN#%u detached\n", pDrv->uLUN));
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDetach}
 */
static DECLCALLBACK(void) hdaR3Detach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PHDASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PHDASTATE);
    PHDASTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PHDASTATER3);
    RT_NOREF(fFlags);
    LogFunc(("iLUN=%u, fFlags=%#x\n", iLUN, fFlags));

    DEVHDA_LOCK(pDevIns, pThis);

    PHDADRIVER pDrv;
    RTListForEach(&pThisCC->lstDrv, pDrv, HDADRIVER, Node)
    {
        if (pDrv->uLUN == iLUN)
        {
            hdaR3DetachInternal(pDevIns, pThisCC, pDrv);
            RTMemFree(pDrv);
            DEVHDA_UNLOCK(pDevIns, pThis);
            return;
        }
    }

    DEVHDA_UNLOCK(pDevIns, pThis);
    LogFunc(("LUN#%u was not found\n", iLUN));
}


/**
 * Powers off the device.
 *
 * @param   pDevIns             Device instance to power off.
 */
static DECLCALLBACK(void) hdaR3PowerOff(PPDMDEVINS pDevIns)
{
    PHDASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PHDASTATE);
    PHDASTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PHDASTATER3);

    DEVHDA_LOCK_RETURN_VOID(pDevIns, pThis);

    LogRel2(("HDA: Powering off ...\n"));

/** @todo r=bird: What this "releasing references" and whatever here is
 *        referring to, is apparently that the device is destroyed after the
 *        drivers, so creating trouble as those structures have been torn down
 *        already...  Reverse order, like we do for power off?  Need a new
 *        PDMDEVREG flag. */

    /* Ditto goes for the codec, which in turn uses the mixer. */
    hdaR3CodecPowerOff(&pThisCC->Codec);

    /* This is to prevent us from calling into the mixer and mixer sink code
       after it has been destroyed below. */
    for (uint8_t i = 0; i < HDA_MAX_STREAMS; i++)
        pThisCC->aStreams[i].State.pAioRegSink = NULL; /* don't need to remove, we're destorying it. */

    /*
     * Note: Destroy the mixer while powering off and *not* in hdaR3Destruct,
     *       giving the mixer the chance to release any references held to
     *       PDM audio streams it maintains.
     */
    if (pThisCC->pMixer)
    {
        AudioMixerDestroy(pThisCC->pMixer, pDevIns);
        pThisCC->pMixer = NULL;
    }

    DEVHDA_UNLOCK(pDevIns, pThis);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) hdaR3Reset(PPDMDEVINS pDevIns)
{
    PHDASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PHDASTATE);
    PHDASTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PHDASTATER3);

    LogFlowFuncEnter();

    DEVHDA_LOCK_RETURN_VOID(pDevIns, pThis);

     /*
     * 18.2.6,7 defines that values of this registers might be cleared on power on/reset
     * hdaR3Reset shouldn't affects these registers.
     */
    HDA_REG(pThis, WAKEEN)  = 0x0;

    hdaR3GCTLReset(pDevIns, pThis, pThisCC);

    /* Indicate that HDA is not in reset. The firmware is supposed to (un)reset HDA,
     * but we can take a shortcut.
     */
    HDA_REG(pThis, GCTL)    = HDA_GCTL_CRST;

    DEVHDA_UNLOCK(pDevIns, pThis);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) hdaR3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns); /* this shall come first */
    PHDASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PHDASTATE);
    PHDASTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PHDASTATER3);

    if (PDMDevHlpCritSectIsInitialized(pDevIns, &pThis->CritSect))
    {
        int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
        AssertRC(rc);
    }

    PHDADRIVER pDrv;
    while (!RTListIsEmpty(&pThisCC->lstDrv))
    {
        pDrv = RTListGetFirst(&pThisCC->lstDrv, HDADRIVER, Node);

        RTListNodeRemove(&pDrv->Node);
        RTMemFree(pDrv);
    }

    hdaCodecDestruct(&pThisCC->Codec);

    for (uint8_t i = 0; i < HDA_MAX_STREAMS; i++)
        hdaR3StreamDestroy(&pThisCC->aStreams[i]);

    /* We don't always go via PowerOff, so make sure the mixer is destroyed. */
    if (pThisCC->pMixer)
    {
        AudioMixerDestroy(pThisCC->pMixer, pDevIns);
        pThisCC->pMixer = NULL;
    }

    if (PDMDevHlpCritSectIsInitialized(pDevIns, &pThis->CritSect))
    {
        PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
        PDMDevHlpCritSectDelete(pDevIns, &pThis->CritSect);
    }
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) hdaR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns); /* this shall come first */
    PHDASTATE       pThis   = PDMDEVINS_2_DATA(pDevIns, PHDASTATE);
    PHDASTATER3     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PHDASTATER3);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    Assert(iInstance == 0); RT_NOREF(iInstance);

    /*
     * Initialize the state sufficently to make the destructor work.
     */
    pThis->uAlignmentCheckMagic = HDASTATE_ALIGNMENT_CHECK_MAGIC;
    RTListInit(&pThisCC->lstDrv);
    pThis->cbCorbBuf            = HDA_CORB_SIZE * HDA_CORB_ELEMENT_SIZE;
    pThis->cbRirbBuf            = HDA_RIRB_SIZE * HDA_RIRB_ELEMENT_SIZE;
    pThis->hCorbDmaTask         = NIL_PDMTASKHANDLE;

    /** @todo r=bird: There are probably other things which should be
     *        initialized here before we start failing. */

    /*
     * Validate and read configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns,
                                  "BufSizeInMs"
                                  "|BufSizeOutMs"
                                  "|DebugEnabled"
                                  "|DebugPathOut"
                                  "|DeviceName",
                                  "");

    /** @devcfgm{hda,BufSizeInMs,uint16_t,0,2000,0,ms}
     * The size of the DMA buffer for input streams expressed in milliseconds. */
    int rc = pHlp->pfnCFGMQueryU16Def(pCfg, "BufSizeInMs", &pThis->cMsCircBufIn, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("HDA configuration error: failed to read 'BufSizeInMs' as 16-bit unsigned integer"));
    if (pThis->cMsCircBufIn > 2000)
        return PDMDEV_SET_ERROR(pDevIns, VERR_OUT_OF_RANGE,
                                N_("HDA configuration error: 'BufSizeInMs' is out of bound, max 2000 ms"));

    /** @devcfgm{hda,BufSizeOutMs,uint16_t,0,2000,0,ms}
     * The size of the DMA buffer for output streams expressed in milliseconds. */
    rc = pHlp->pfnCFGMQueryU16Def(pCfg, "BufSizeOutMs", &pThis->cMsCircBufOut, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("HDA configuration error: failed to read 'BufSizeOutMs' as 16-bit unsigned integer"));
    if (pThis->cMsCircBufOut > 2000)
        return PDMDEV_SET_ERROR(pDevIns, VERR_OUT_OF_RANGE,
                                N_("HDA configuration error: 'BufSizeOutMs' is out of bound, max 2000 ms"));

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "DebugEnabled", &pThisCC->Dbg.fEnabled, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("HDA configuration error: failed to read debugging enabled flag as boolean"));

    rc = pHlp->pfnCFGMQueryStringAllocDef(pCfg, "DebugPathOut", &pThisCC->Dbg.pszOutPath, NULL);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("HDA configuration error: failed to read debugging output path flag as string"));
    if (pThisCC->Dbg.fEnabled)
        LogRel2(("HDA: Debug output will be saved to '%s'\n", pThisCC->Dbg.pszOutPath));

    /** @devcfgm{hda,DeviceName,string}
     * Override the default device/vendor IDs for the emulated device:
     *      - "" - default
     *      - "Intel ICH6"
     *      - "Intel Sunrise Point" - great for macOS 10.15
     */
    char szDeviceName[32];
    rc = pHlp->pfnCFGMQueryStringDef(pCfg, "DeviceName", szDeviceName, sizeof(szDeviceName), "");
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("HDA configuration error: failed to read 'DeviceName' name string"));
    enum
    {
        kDevice_Default,
        kDevice_IntelIch6,
        kDevice_IntelSunrisePoint /*skylake timeframe*/
    } enmDevice;
    if (strcmp(szDeviceName, "") == 0)
        enmDevice = kDevice_Default;
    else if (strcmp(szDeviceName, "Intel ICH6") == 0)
        enmDevice = kDevice_IntelIch6;
    else if (strcmp(szDeviceName, "Intel Sunrise Point") == 0)
        enmDevice = kDevice_IntelSunrisePoint;
    else
        return  PDMDevHlpVMSetError(pDevIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                    N_("HDA configuration error: Unknown 'DeviceName' name '%s'"), szDeviceName);

    /*
     * Use our own critical section for the device instead of the default
     * one provided by PDM. This allows fine-grained locking in combination
     * with TM when timer-specific stuff is being called in e.g. the MMIO handlers.
     */
    rc = PDMDevHlpCritSectInit(pDevIns, &pThis->CritSect, RT_SRC_POS, "HDA");
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    /*
     * Initialize data (most of it anyway).
     */
    pThisCC->pDevIns                  = pDevIns;
    /* IBase */
    pThisCC->IBase.pfnQueryInterface  = hdaR3QueryInterface;

    /* PCI Device */
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);

    switch (enmDevice)
    {
        case kDevice_Default:
            PDMPciDevSetVendorId(pPciDev, HDA_PCI_VENDOR_ID);
            PDMPciDevSetDeviceId(pPciDev, HDA_PCI_DEVICE_ID);
            break;
        case kDevice_IntelIch6: /* Our default intel device. */
            PDMPciDevSetVendorId(pPciDev, 0x8086);
            PDMPciDevSetDeviceId(pPciDev, 0x2668);
            break;
        case kDevice_IntelSunrisePoint: /* this is supported by more recent macOS version, at least 10.15 */
            PDMPciDevSetVendorId(pPciDev, 0x8086);
            PDMPciDevSetDeviceId(pPciDev, 0x9d70);
            break;
    }

    PDMPciDevSetCommand(       pPciDev, 0x0000); /* 04 rw,ro - pcicmd. */
    PDMPciDevSetStatus(        pPciDev, VBOX_PCI_STATUS_CAP_LIST); /* 06 rwc?,ro? - pcists. */
    PDMPciDevSetRevisionId(    pPciDev, 0x01);   /* 08 ro - rid. */
    PDMPciDevSetClassProg(     pPciDev, 0x00);   /* 09 ro - pi. */
    PDMPciDevSetClassSub(      pPciDev, 0x03);   /* 0a ro - scc; 03 == HDA. */
    PDMPciDevSetClassBase(     pPciDev, 0x04);   /* 0b ro - bcc; 04 == multimedia. */
    PDMPciDevSetHeaderType(    pPciDev, 0x00);   /* 0e ro - headtyp. */
    PDMPciDevSetBaseAddress(   pPciDev, 0,       /* 10 rw - MMIO */
                               false /* fIoSpace */, false /* fPrefetchable */, true /* f64Bit */, 0x00000000);
    PDMPciDevSetInterruptLine( pPciDev, 0x00);   /* 3c rw. */
    PDMPciDevSetInterruptPin(  pPciDev, 0x01);   /* 3d ro - INTA#. */

# if defined(HDA_AS_PCI_EXPRESS)
    PDMPciDevSetCapabilityList(pPciDev, 0x80);
# elif defined(VBOX_WITH_MSI_DEVICES)
    PDMPciDevSetCapabilityList(pPciDev, 0x60);
# else
    PDMPciDevSetCapabilityList(pPciDev, 0x50);   /* ICH6 datasheet 18.1.16 */
# endif

    /// @todo r=michaln: If there are really no PDMPciDevSetXx for these, the
    /// meaning of these values needs to be properly documented!
    /* HDCTL off 0x40 bit 0 selects signaling mode (1-HDA, 0 - Ac97) 18.1.19 */
    PDMPciDevSetByte(          pPciDev, 0x40, 0x01);

    /* Power Management */
    PDMPciDevSetByte(          pPciDev, 0x50 + 0, VBOX_PCI_CAP_ID_PM);
    PDMPciDevSetByte(          pPciDev, 0x50 + 1, 0x0); /* next */
    PDMPciDevSetWord(          pPciDev, 0x50 + 2, VBOX_PCI_PM_CAP_DSI | 0x02 /* version, PM1.1 */ );

# ifdef HDA_AS_PCI_EXPRESS
    /* PCI Express */
    PDMPciDevSetByte(          pPciDev, 0x80 + 0, VBOX_PCI_CAP_ID_EXP); /* PCI_Express */
    PDMPciDevSetByte(          pPciDev, 0x80 + 1, 0x60); /* next */
    /* Device flags */
    PDMPciDevSetWord(          pPciDev, 0x80 + 2,
                                 1 /* version */
                               | (VBOX_PCI_EXP_TYPE_ROOT_INT_EP << 4) /* Root Complex Integrated Endpoint */
                               | (100 << 9) /* MSI */ );
    /* Device capabilities */
    PDMPciDevSetDWord(         pPciDev, 0x80 + 4, VBOX_PCI_EXP_DEVCAP_FLRESET);
    /* Device control */
    PDMPciDevSetWord(          pPciDev, 0x80 + 8, 0);
    /* Device status */
    PDMPciDevSetWord(          pPciDev, 0x80 + 10, 0);
    /* Link caps */
    PDMPciDevSetDWord(         pPciDev, 0x80 + 12, 0);
    /* Link control */
    PDMPciDevSetWord(          pPciDev, 0x80 + 16, 0);
    /* Link status */
    PDMPciDevSetWord(          pPciDev, 0x80 + 18, 0);
    /* Slot capabilities */
    PDMPciDevSetDWord(         pPciDev, 0x80 + 20, 0);
    /* Slot control */
    PDMPciDevSetWord(          pPciDev, 0x80 + 24, 0);
    /* Slot status */
    PDMPciDevSetWord(          pPciDev, 0x80 + 26, 0);
    /* Root control */
    PDMPciDevSetWord(          pPciDev, 0x80 + 28, 0);
    /* Root capabilities */
    PDMPciDevSetWord(          pPciDev, 0x80 + 30, 0);
    /* Root status */
    PDMPciDevSetDWord(         pPciDev, 0x80 + 32, 0);
    /* Device capabilities 2 */
    PDMPciDevSetDWord(         pPciDev, 0x80 + 36, 0);
    /* Device control 2 */
    PDMPciDevSetQWord(         pPciDev, 0x80 + 40, 0);
    /* Link control 2 */
    PDMPciDevSetQWord(         pPciDev, 0x80 + 48, 0);
    /* Slot control 2 */
    PDMPciDevSetWord(          pPciDev, 0x80 + 56, 0);
# endif /* HDA_AS_PCI_EXPRESS */

    /*
     * Register the PCI device.
     */
    rc = PDMDevHlpPCIRegister(pDevIns, pPciDev);
    AssertRCReturn(rc, rc);

    /** @todo r=bird: The IOMMMIO_FLAGS_READ_DWORD flag isn't entirely optimal,
     * as several frequently used registers aren't dword sized.  6.0 and earlier
     * will go to ring-3 to handle accesses to any such register, where-as 6.1 and
     * later will do trivial register reads in ring-0.   Real optimal code would use
     * IOMMMIO_FLAGS_READ_PASSTHRU and do the necessary extra work to deal with
     * anything the guest may throw at us. */
    rc = PDMDevHlpPCIIORegionCreateMmio(pDevIns, 0, 0x4000, PCI_ADDRESS_SPACE_MEM, hdaMmioWrite, hdaMmioRead, NULL /*pvUser*/,
                                        IOMMMIO_FLAGS_READ_DWORD | IOMMMIO_FLAGS_WRITE_PASSTHRU, "HDA", &pThis->hMmio);
    AssertRCReturn(rc, rc);

# ifdef VBOX_WITH_MSI_DEVICES
    PDMMSIREG MsiReg;
    RT_ZERO(MsiReg);
    MsiReg.cMsiVectors    = 1;
    MsiReg.iMsiCapOffset  = 0x60;
    MsiReg.iMsiNextOffset = 0x50;
    rc = PDMDevHlpPCIRegisterMsi(pDevIns, &MsiReg);
    if (RT_FAILURE(rc))
    {
        /* That's OK, we can work without MSI */
        PDMPciDevSetCapabilityList(pPciDev, 0x50);
    }
# endif

    /* Create task for continuing CORB DMA in ring-3. */
    rc = PDMDevHlpTaskCreate(pDevIns, PDMTASK_F_RZ, "HDA CORB DMA",
                             hdaR3CorbDmaTaskWorker, NULL /*pvUser*/, &pThis->hCorbDmaTask);
    AssertRCReturn(rc,rc);

    rc = PDMDevHlpSSMRegisterEx(pDevIns, HDA_SAVED_STATE_VERSION, sizeof(*pThis), NULL /*pszBefore*/,
                                NULL /*pfnLivePrep*/, NULL /*pfnLiveExec*/, NULL /*pfnLiveVote*/,
                                NULL /*pfnSavePrep*/, hdaR3SaveExec, NULL /*pfnSaveDone*/,
                                NULL /*pfnLoadPrep*/, hdaR3LoadExec, hdaR3LoadDone);
    AssertRCReturn(rc, rc);

    /*
     * Attach drivers.  We ASSUME they are configured consecutively without any
     * gaps, so we stop when we hit the first LUN w/o a driver configured.
     */
    for (unsigned iLun = 0; ; iLun++)
    {
        AssertBreak(iLun < UINT8_MAX);
        LogFunc(("Trying to attach driver for LUN#%u ...\n", iLun));
        rc = hdaR3AttachInternal(pDevIns, pThis, pThisCC, iLun, NULL /* ppDrv */);
        if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
        {
            LogFunc(("cLUNs=%u\n", iLun));
            break;
        }
        AssertLogRelMsgReturn(RT_SUCCESS(rc),  ("LUN#%u: rc=%Rrc\n", iLun, rc), rc);
    }

    /*
     * Create the mixer.
     */
    uint32_t fMixer = AUDMIXER_FLAGS_NONE;
    if (pThisCC->Dbg.fEnabled)
        fMixer |= AUDMIXER_FLAGS_DEBUG;
    rc = AudioMixerCreate("HDA Mixer", fMixer, &pThisCC->pMixer);
    AssertRCReturn(rc, rc);

    /*
     * Add mixer output sinks.
     */
# ifdef VBOX_WITH_AUDIO_HDA_51_SURROUND
    rc = AudioMixerCreateSink(pThisCC->pMixer, "Front",
                              PDMAUDIODIR_OUT, pDevIns, &pThisCC->SinkFront.pMixSink);
    AssertRCReturn(rc, rc);
    rc = AudioMixerCreateSink(pThisCC->pMixer, "Center+Subwoofer",
                              PDMAUDIODIR_OUT, pDevIns, &pThisCC->SinkCenterLFE.pMixSink);
    AssertRCReturn(rc, rc);
    rc = AudioMixerCreateSink(pThisCC->pMixer, "Rear",
                              PDMAUDIODIR_OUT, pDevIns, &pThisCC->SinkRear.pMixSink);
    AssertRCReturn(rc, rc);
# else
    rc = AudioMixerCreateSink(pThisCC->pMixer, "PCM Output",
                              PDMAUDIODIR_OUT, pDevIns, &pThisCC->SinkFront.pMixSink);
    AssertRCReturn(rc, rc);
# endif /* VBOX_WITH_AUDIO_HDA_51_SURROUND */

    /*
     * Add mixer input sinks.
     */
    rc = AudioMixerCreateSink(pThisCC->pMixer, "Line In",
                              PDMAUDIODIR_IN, pDevIns, &pThisCC->SinkLineIn.pMixSink);
    AssertRCReturn(rc, rc);
# ifdef VBOX_WITH_AUDIO_HDA_MIC_IN
    rc = AudioMixerCreateSink(pThisCC->pMixer, "Microphone In",
                              PDMAUDIODIR_IN, pDevIns, &pThisCC->SinkMicIn.pMixSink);
    AssertRCReturn(rc, rc);
# endif

    /* There is no master volume control. Set the master to max. */
    PDMAUDIOVOLUME Vol = PDMAUDIOVOLUME_INITIALIZER_MAX;
    rc = AudioMixerSetMasterVolume(pThisCC->pMixer, &Vol);
    AssertRCReturn(rc, rc);

    /*
     * Initialize the codec.
     */
    /* Construct the common + R3 codec part. */
    rc = hdaR3CodecConstruct(pDevIns, &pThisCC->Codec, 0 /* Codec index */, pCfg);
    AssertRCReturn(rc, rc);

    /* ICH6 datasheet defines 0 values for SVID and SID (18.1.14-15), which together with values returned for
       verb F20 should provide device/codec recognition. */
    Assert(pThisCC->Codec.Cfg.idVendor);
    Assert(pThisCC->Codec.Cfg.idDevice);
    PDMPciDevSetSubSystemVendorId(pPciDev, pThisCC->Codec.Cfg.idVendor); /* 2c ro - intel.) */
    PDMPciDevSetSubSystemId(      pPciDev, pThisCC->Codec.Cfg.idDevice); /* 2e ro. */

    /*
     * Create the per stream timers and the asso.
     *
     * We must the critical section for the timers as the device has a
     * noop section associated with it.
     *
     * Note:  Use TMCLOCK_VIRTUAL_SYNC here, as the guest's HDA driver relies
     *        on exact (virtual) DMA timing and uses DMA Position Buffers
     *        instead of the LPIB registers.
     */
    /** @todo r=bird: The need to use virtual sync is perhaps because TM
     *        doesn't schedule regular TMCLOCK_VIRTUAL timers as accurately as it
     *        should (VT-x preemption timer, etc).  Hope to address that before
     *        long. @bugref{9943}. */
    static const char * const s_apszNames[] =
    { "HDA SD0", "HDA SD1", "HDA SD2", "HDA SD3", "HDA SD4", "HDA SD5", "HDA SD6", "HDA SD7", };
    AssertCompile(RT_ELEMENTS(s_apszNames) == HDA_MAX_STREAMS);
    for (size_t i = 0; i < HDA_MAX_STREAMS; i++)
    {
        rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL_SYNC, hdaR3Timer, (void *)(uintptr_t)i,
                                  TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_RING0, s_apszNames[i], &pThis->aStreams[i].hTimer);
        AssertRCReturn(rc, rc);

        rc = PDMDevHlpTimerSetCritSect(pDevIns, pThis->aStreams[i].hTimer, &pThis->CritSect);
        AssertRCReturn(rc, rc);
    }

    /*
     * Create all hardware streams.
     */
    for (uint8_t i = 0; i < HDA_MAX_STREAMS; ++i)
    {
        rc = hdaR3StreamConstruct(&pThis->aStreams[i], &pThisCC->aStreams[i], pThis, pThisCC, i /* u8SD */);
        AssertRCReturn(rc, rc);
    }

    hdaR3Reset(pDevIns);

    /*
     * Info items and string formatter types.  The latter is non-optional as
     * the info handles use (at least some of) the custom types and we cannot
     * accept screwing formatting.
     */
    PDMDevHlpDBGFInfoRegister(pDevIns, "hda",         "HDA registers. (hda [register case-insensitive])", hdaR3DbgInfo);
    PDMDevHlpDBGFInfoRegister(pDevIns, "hdabdl",
                              "HDA buffer descriptor list (BDL) and DMA stream positions. (hdabdl [stream number])",
                              hdaR3DbgInfoBDL);
    PDMDevHlpDBGFInfoRegister(pDevIns, "hdastream",   "HDA stream info. (hdastream [stream number])",    hdaR3DbgInfoStream);
    PDMDevHlpDBGFInfoRegister(pDevIns, "hdcnodes",    "HDA codec nodes.",                                hdaR3DbgInfoCodecNodes);
    PDMDevHlpDBGFInfoRegister(pDevIns, "hdcselector", "HDA codec's selector states [node number].",      hdaR3DbgInfoCodecSelector);
    PDMDevHlpDBGFInfoRegister(pDevIns, "hdamixer",    "HDA mixer state.",                                hdaR3DbgInfoMixer);

    rc = RTStrFormatTypeRegister("sdctl",   hdaR3StrFmtSDCTL,   NULL);
    AssertMsgReturn(RT_SUCCESS(rc) || rc == VERR_ALREADY_EXISTS, ("%Rrc\n", rc), rc);
    rc = RTStrFormatTypeRegister("sdsts",   hdaR3StrFmtSDSTS,   NULL);
    AssertMsgReturn(RT_SUCCESS(rc) || rc == VERR_ALREADY_EXISTS, ("%Rrc\n", rc), rc);
    /** @todo the next two are rather pointless.   */
    rc = RTStrFormatTypeRegister("sdfifos", hdaR3StrFmtSDFIFOS, NULL);
    AssertMsgReturn(RT_SUCCESS(rc) || rc == VERR_ALREADY_EXISTS, ("%Rrc\n", rc), rc);
    rc = RTStrFormatTypeRegister("sdfifow", hdaR3StrFmtSDFIFOW, NULL);
    AssertMsgReturn(RT_SUCCESS(rc) || rc == VERR_ALREADY_EXISTS, ("%Rrc\n", rc), rc);

    /*
     * Asserting sanity.
     */
    AssertCompile(RT_ELEMENTS(pThis->au32Regs) < 256 /* assumption by HDAREGDESC::idxReg */);
    for (unsigned i = 0; i < RT_ELEMENTS(g_aHdaRegMap); i++)
    {
        struct HDAREGDESC const *pReg     = &g_aHdaRegMap[i];
        struct HDAREGDESC const *pNextReg = i + 1 < RT_ELEMENTS(g_aHdaRegMap) ? &g_aHdaRegMap[i + 1] : NULL;

        /* binary search order. */
        AssertReleaseMsg(!pNextReg || pReg->off + pReg->cb <= pNextReg->off,
                         ("[%#x] = {%#x LB %#x}  vs. [%#x] = {%#x LB %#x}\n",
                          i, pReg->off, pReg->cb, i + 1, pNextReg->off, pNextReg->cb));

        /* alignment. */
        AssertReleaseMsg(   pReg->cb == 1
                         || (pReg->cb == 2 && (pReg->off & 1) == 0)
                         || (pReg->cb == 3 && (pReg->off & 3) == 0)
                         || (pReg->cb == 4 && (pReg->off & 3) == 0),
                         ("[%#x] = {%#x LB %#x}\n", i, pReg->off, pReg->cb));

        /* registers are packed into dwords - with 3 exceptions with gaps at the end of the dword. */
        AssertRelease(((pReg->off + pReg->cb) & 3) == 0 || pNextReg);
        if (pReg->off & 3)
        {
            struct HDAREGDESC const *pPrevReg = i > 0 ?  &g_aHdaRegMap[i - 1] : NULL;
            AssertReleaseMsg(pPrevReg, ("[%#x] = {%#x LB %#x}\n", i, pReg->off, pReg->cb));
            if (pPrevReg)
                AssertReleaseMsg(pPrevReg->off + pPrevReg->cb == pReg->off,
                                 ("[%#x] = {%#x LB %#x}  vs. [%#x] = {%#x LB %#x}\n",
                                  i - 1, pPrevReg->off, pPrevReg->cb, i + 1, pReg->off, pReg->cb));
        }
#if 0
        if ((pReg->offset + pReg->size) & 3)
        {
            AssertReleaseMsg(pNextReg, ("[%#x] = {%#x LB %#x}\n", i, pReg->offset, pReg->size));
            if (pNextReg)
                AssertReleaseMsg(pReg->offset + pReg->size == pNextReg->offset,
                                 ("[%#x] = {%#x LB %#x}  vs. [%#x] = {%#x LB %#x}\n",
                                  i, pReg->offset, pReg->size, i + 1,  pNextReg->offset, pNextReg->size));
        }
#endif
        /* The final entry is a full DWORD, no gaps! Allows shortcuts. */
        AssertReleaseMsg(pNextReg || ((pReg->off + pReg->cb) & 3) == 0,
                         ("[%#x] = {%#x LB %#x}\n", i, pReg->off, pReg->cb));
    }
    for (unsigned i = 0; i < RT_ELEMENTS(g_aHdaRegAliases); i++)
    {
        /* Valid alias index. */
        uint32_t const idxAlias = g_aHdaRegAliases[i].idxAlias;
        AssertReleaseMsg(g_aHdaRegAliases[i].idxAlias < (int)RT_ELEMENTS(g_aHdaRegMap), ("[%#x] idxAlias=%#x\n", i, idxAlias));
        /* Same register alignment. */
        AssertReleaseMsg((g_aHdaRegAliases[i].offReg & 3) == (g_aHdaRegMap[idxAlias].off & 3),
                         ("[%#x] idxAlias=%#x offReg=%#x vs off=%#x\n",
                          i, idxAlias, g_aHdaRegAliases[i].offReg, g_aHdaRegMap[idxAlias].off));
        /* Register is four or fewer bytes wide (already checked above). */
        AssertReleaseMsg(g_aHdaRegMap[idxAlias].cb <= 4, ("[%#x] idxAlias=%#x cb=%d\n", i, idxAlias, g_aHdaRegMap[idxAlias].cb));
    }
    Assert(strcmp(g_aHdaRegMap[HDA_REG_SSYNC].pszName, "SSYNC") == 0);
    Assert(strcmp(g_aHdaRegMap[HDA_REG_DPUBASE].pszName, "DPUBASE") == 0);
    Assert(strcmp(g_aHdaRegMap[HDA_REG_MLCH].pszName, "MLCH") == 0);
    Assert(strcmp(g_aHdaRegMap[HDA_REG_SD3DPIB].pszName, "SD3DPIB") == 0);
    Assert(strcmp(g_aHdaRegMap[HDA_REG_SD7EFIFOS].pszName, "SD7EFIFOS") == 0);

    /*
     * Register statistics.
     */
# ifdef VBOX_WITH_STATISTICS
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatIn,               STAMTYPE_PROFILE, "Input",             STAMUNIT_TICKS_PER_CALL, "Profiling input.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatOut,              STAMTYPE_PROFILE, "Output",            STAMUNIT_TICKS_PER_CALL, "Profiling output.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatBytesRead,        STAMTYPE_COUNTER, "BytesRead"   ,      STAMUNIT_BYTES,          "Bytes read (DMA) from the guest.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatBytesWritten,     STAMTYPE_COUNTER, "BytesWritten",      STAMUNIT_BYTES,          "Bytes written (DMA) to the guest.");
#  ifdef VBOX_HDA_WITH_ON_REG_ACCESS_DMA
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatAccessDmaOutput,  STAMTYPE_COUNTER, "AccessDmaOutput",   STAMUNIT_COUNT,          "Number of on-register-access DMA sub-transfers we've made.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatAccessDmaOutputToR3,STAMTYPE_COUNTER, "AccessDmaOutputToR3", STAMUNIT_COUNT,      "Number of time the on-register-access DMA forced a ring-3 return.");
#  endif

    AssertCompile(RT_ELEMENTS(g_aHdaRegMap)              == HDA_NUM_REGS);
    AssertCompile(RT_ELEMENTS(pThis->aStatRegReads)      == HDA_NUM_REGS);
    AssertCompile(RT_ELEMENTS(pThis->aStatRegReadsToR3)  == HDA_NUM_REGS);
    AssertCompile(RT_ELEMENTS(pThis->aStatRegWrites)     == HDA_NUM_REGS);
    AssertCompile(RT_ELEMENTS(pThis->aStatRegWritesToR3) == HDA_NUM_REGS);
    for (size_t i = 0; i < RT_ELEMENTS(g_aHdaRegMap); i++)
    {
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStatRegReads[i], STAMTYPE_COUNTER,  STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                               g_aHdaRegMap[i].pszDesc, "Regs/%03x-%s-Reads",       g_aHdaRegMap[i].off, g_aHdaRegMap[i].pszName);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStatRegReadsToR3[i], STAMTYPE_COUNTER,  STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                               g_aHdaRegMap[i].pszDesc, "Regs/%03x-%s-Reads-ToR3",  g_aHdaRegMap[i].off, g_aHdaRegMap[i].pszName);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStatRegWrites[i], STAMTYPE_COUNTER,  STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                               g_aHdaRegMap[i].pszDesc, "Regs/%03x-%s-Writes",      g_aHdaRegMap[i].off, g_aHdaRegMap[i].pszName);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStatRegWritesToR3[i], STAMTYPE_COUNTER,  STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                               g_aHdaRegMap[i].pszDesc, "Regs/%03x-%s-Writes-ToR3", g_aHdaRegMap[i].off, g_aHdaRegMap[i].pszName);
    }
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRegMultiReadsR3,   STAMTYPE_COUNTER, "RegMultiReadsR3",    STAMUNIT_OCCURENCES, "Register read not targeting just one register, handled in ring-3");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRegMultiReadsRZ,   STAMTYPE_COUNTER, "RegMultiReadsRZ",    STAMUNIT_OCCURENCES, "Register read not targeting just one register, handled in ring-0");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRegMultiWritesR3,  STAMTYPE_COUNTER, "RegMultiWritesR3",   STAMUNIT_OCCURENCES, "Register writes not targeting just one register, handled in ring-3");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRegMultiWritesRZ,  STAMTYPE_COUNTER, "RegMultiWritesRZ",   STAMUNIT_OCCURENCES, "Register writes not targeting just one register, handled in ring-0");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRegSubWriteR3,     STAMTYPE_COUNTER, "RegSubWritesR3",     STAMUNIT_OCCURENCES, "Trucated register writes, handled in ring-3");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRegSubWriteRZ,     STAMTYPE_COUNTER, "RegSubWritesRZ",     STAMUNIT_OCCURENCES, "Trucated register writes, handled in ring-0");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRegUnknownReads,   STAMTYPE_COUNTER, "RegUnknownReads",    STAMUNIT_OCCURENCES, "Reads of unknown registers.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRegUnknownWrites,  STAMTYPE_COUNTER, "RegUnknownWrites",   STAMUNIT_OCCURENCES, "Writes to unknown registers.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRegWritesBlockedByReset, STAMTYPE_COUNTER, "RegWritesBlockedByReset", STAMUNIT_OCCURENCES, "Writes blocked by pending reset (GCTL/CRST)");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRegWritesBlockedByRun,   STAMTYPE_COUNTER, "RegWritesBlockedByRun",   STAMUNIT_OCCURENCES, "Writes blocked by byte RUN bit.");
# endif

    for (uint8_t idxStream = 0; idxStream < RT_ELEMENTS(pThisCC->aStreams); idxStream++)
    {
        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.StatDmaFlowProblems, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                               "Number of internal DMA buffer problems.",   "Stream%u/DMABufferProblems", idxStream);
        if (hdaGetDirFromSD(idxStream) == PDMAUDIODIR_OUT)
            PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.StatDmaFlowErrors, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                                   "Number of internal DMA buffer overflows.",  "Stream%u/DMABufferOverflows", idxStream);
        else
        {
            PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.StatDmaFlowErrors, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                                   "Number of internal DMA buffer underuns.", "Stream%u/DMABufferUnderruns", idxStream);
            PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.StatDmaFlowErrorBytes, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                                   "Number of bytes of silence added to cope with underruns.", "Stream%u/DMABufferSilence", idxStream);
        }
        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.StatDmaSkippedPendingBcis, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                               "DMA transfer period skipped because of BCIS pending.", "Stream%u/DMASkippedPendingBCIS", idxStream);

        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStreams[idxStream].State.offRead, STAMTYPE_U64, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                               "Virtual internal buffer read position.",    "Stream%u/offRead", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStreams[idxStream].State.offWrite, STAMTYPE_U64, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                               "Virtual internal buffer write position.",   "Stream%u/offWrite", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStreams[idxStream].State.cbCurDmaPeriod, STAMTYPE_U32, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                               "Bytes transfered per DMA timer callout.",   "Stream%u/cbCurDmaPeriod", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, (void*)&pThis->aStreams[idxStream].State.fRunning, STAMTYPE_BOOL, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                               "True if the stream is in RUN mode.",        "Stream%u/fRunning", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStreams[idxStream].State.Cfg.Props.uHz, STAMTYPE_U32, STAMVISIBILITY_USED, STAMUNIT_HZ,
                               "The stream frequency.",                     "Stream%u/Cfg/Hz", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStreams[idxStream].State.Cfg.Props.cbFrame, STAMTYPE_U8, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                               "The frame size.",                           "Stream%u/Cfg/FrameSize", idxStream);
#if 0 /** @todo this would require some callback or expansion. */
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStreams[idxStream].State.Cfg.Props.cChannelsX, STAMTYPE_U8, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                               "The number of channels.",                   "Stream%u/Cfg/Channels-Host", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.Mapping.GuestProps.cChannels, STAMTYPE_U8, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                               "The number of channels.",                   "Stream%u/Cfg/Channels-Guest", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStreams[idxStream].State.Cfg.Props.cbSample, STAMTYPE_U8, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                               "The size of a sample (per channel).",       "Stream%u/Cfg/cbSample", idxStream);
#endif

        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.StatDmaBufSize, STAMTYPE_U32, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                               "Size of the internal DMA buffer.",  "Stream%u/DMABufSize", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.StatDmaBufUsed, STAMTYPE_U32, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                               "Number of bytes used in the internal DMA buffer.",  "Stream%u/DMABufUsed", idxStream);

        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.StatStart, STAMTYPE_PROFILE, STAMVISIBILITY_USED, STAMUNIT_NS_PER_CALL,
                               "Starting the stream.",  "Stream%u/Start", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.StatStop, STAMTYPE_PROFILE, STAMVISIBILITY_USED, STAMUNIT_NS_PER_CALL,
                               "Stopping the stream.",  "Stream%u/Stop", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.StatReset, STAMTYPE_PROFILE, STAMVISIBILITY_USED, STAMUNIT_NS_PER_CALL,
                               "Resetting the stream.",  "Stream%u/Reset", idxStream);
    }

    return VINF_SUCCESS;
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) hdaRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns); /* this shall come first */
    PHDASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PHDASTATE);
    PHDASTATER0 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PHDASTATER0);

    int rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpMmioSetUpContext(pDevIns, pThis->hMmio, hdaMmioWrite, hdaMmioRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

# if 0 /* Codec is not yet kosher enough for ring-0.  @bugref{9890c64} */
    /* Construct the R0 codec part. */
    rc = hdaR0CodecConstruct(pDevIns, &pThis->Codec, &pThisCC->Codec);
    AssertRCReturn(rc, rc);
# else
    RT_NOREF(pThisCC);
# endif

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */

/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceHDA =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "hda",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE
                                    | PDM_DEVREG_FLAGS_FIRST_POWEROFF_NOTIFICATION /* stream clearnup with working drivers */,
    /* .fClass = */                 PDM_DEVREG_CLASS_AUDIO,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(HDASTATE),
    /* .cbInstanceCC = */           CTX_EXPR(sizeof(HDASTATER3), sizeof(HDASTATER0), 0),
    /* .cbInstanceRC = */           0,
    /* .cMaxPciDevices = */         1,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Intel HD Audio Controller",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           hdaR3Construct,
    /* .pfnDestruct = */            hdaR3Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               hdaR3Reset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              hdaR3Attach,
    /* .pfnDetach = */              hdaR3Detach,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        NULL,
    /* .pfnPowerOff = */            hdaR3PowerOff,
    /* .pfnSoftReset = */           NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RING0)
    /* .pfnEarlyConstruct = */      NULL,
    /* .pfnConstruct = */           hdaRZConstruct,
    /* .pfnDestruct = */            NULL,
    /* .pfnFinalDestruct = */       NULL,
    /* .pfnRequest = */             NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RC)
    /* .pfnConstruct = */           hdaRZConstruct,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#else
# error "Not in IN_RING3, IN_RING0 or IN_RC!"
#endif
    /* .u32VersionEnd = */          PDM_DEVREG_VERSION
};

#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */

