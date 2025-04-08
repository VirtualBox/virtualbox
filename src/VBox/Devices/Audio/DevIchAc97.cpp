/* $Id: DevIchAc97.cpp 109106 2025-04-08 14:04:25Z andreas.loeffler@oracle.com $ */
/** @file
 * DevIchAc97 - VBox ICH AC97 Audio Controller.
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
#define LOG_GROUP LOG_GROUP_DEV_AC97
#include <VBox/log.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pdmaudioifs.h>
#include <VBox/vmm/pdmaudioinline.h>
#include <VBox/AssertGuest.h>

#include <iprt/assert.h>
#ifdef IN_RING3
# ifdef DEBUG
#  include <iprt/file.h>
# endif
# include <iprt/mem.h>
# include <iprt/semaphore.h>
# include <iprt/string.h>
# include <iprt/uuid.h>
# include <iprt/zero.h>
#endif

#include "VBoxDD.h"

#include "AudioMixBuffer.h"
#include "AudioMixer.h"
#include "AudioHlp.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Current saved state version. */
#define AC97_SAVED_STATE_VERSION 1

/** Default timer frequency (in Hz). */
#define AC97_TIMER_HZ_DEFAULT   100

/** Maximum number of streams we support. */
#define AC97_MAX_STREAMS        3

/** Maximum FIFO size (in bytes) - unused. */
#define AC97_FIFO_MAX           256

/** @name AC97_SR_XXX - Status Register Bits (AC97_NABM_OFF_SR, PI_SR, PO_SR, MC_SR).
 * @{ */
#define AC97_SR_FIFOE           RT_BIT(4)           /**< rwc, FIFO error. */
#define AC97_SR_BCIS            RT_BIT(3)           /**< rwc, Buffer completion interrupt status. */
#define AC97_SR_LVBCI           RT_BIT(2)           /**< rwc, Last valid buffer completion interrupt. */
#define AC97_SR_CELV            RT_BIT(1)           /**< ro,  Current equals last valid. */
#define AC97_SR_DCH             RT_BIT(0)           /**< ro,  Controller halted. */
#define AC97_SR_VALID_MASK      (RT_BIT(5) - 1)
#define AC97_SR_WCLEAR_MASK     (AC97_SR_FIFOE | AC97_SR_BCIS | AC97_SR_LVBCI)
#define AC97_SR_RO_MASK         (AC97_SR_DCH | AC97_SR_CELV)
#define AC97_SR_INT_MASK        (AC97_SR_FIFOE | AC97_SR_BCIS | AC97_SR_LVBCI)
/** @} */

/** @name AC97_CR_XXX - Control Register Bits (AC97_NABM_OFF_CR, PI_CR, PO_CR, MC_CR).
 * @{ */
#define AC97_CR_IOCE            RT_BIT(4)           /**< rw,   Interrupt On Completion Enable. */
#define AC97_CR_FEIE            RT_BIT(3)           /**< rw    FIFO Error Interrupt Enable. */
#define AC97_CR_LVBIE           RT_BIT(2)           /**< rw    Last Valid Buffer Interrupt Enable. */
#define AC97_CR_RR              RT_BIT(1)           /**< rw    Reset Registers. */
#define AC97_CR_RPBM            RT_BIT(0)           /**< rw    Run/Pause Bus Master. */
#define AC97_CR_VALID_MASK      (RT_BIT(5) - 1)
#define AC97_CR_DONT_CLEAR_MASK (AC97_CR_IOCE | AC97_CR_FEIE | AC97_CR_LVBIE)
/** @} */

/** @name AC97_GC_XXX - Global Control Bits (see AC97_GLOB_CNT).
 * @{ */
#define AC97_GC_WR              4                   /**< rw    Warm reset. */
#define AC97_GC_CR              2                   /**< rw    Cold reset. */
#define AC97_GC_VALID_MASK      (RT_BIT(6) - 1)
/** @} */

/** @name AC97_GS_XXX - Global Status Bits (AC97_GLOB_STA).
 * @{ */
#define AC97_GS_MD3             RT_BIT(17)          /**< rw */
#define AC97_GS_AD3             RT_BIT(16)          /**< rw */
#define AC97_GS_RCS             RT_BIT(15)          /**< rwc */
#define AC97_GS_B3S12           RT_BIT(14)          /**< ro */
#define AC97_GS_B2S12           RT_BIT(13)          /**< ro */
#define AC97_GS_B1S12           RT_BIT(12)          /**< ro */
#define AC97_GS_S1R1            RT_BIT(11)          /**< rwc */
#define AC97_GS_S0R1            RT_BIT(10)          /**< rwc */
#define AC97_GS_S1CR            RT_BIT(9)           /**< ro */
#define AC97_GS_S0CR            RT_BIT(8)           /**< ro */
#define AC97_GS_MINT            RT_BIT(7)           /**< ro */
#define AC97_GS_POINT           RT_BIT(6)           /**< ro */
#define AC97_GS_PIINT           RT_BIT(5)           /**< ro */
#define AC97_GS_RSRVD           (RT_BIT(4) | RT_BIT(3))
#define AC97_GS_MOINT           RT_BIT(2)           /**< ro */
#define AC97_GS_MIINT           RT_BIT(1)           /**< ro */
#define AC97_GS_GSCI            RT_BIT(0)           /**< rwc */
#define AC97_GS_RO_MASK         (  AC97_GS_B3S12 \
                                 | AC97_GS_B2S12 \
                                 | AC97_GS_B1S12 \
                                 | AC97_GS_S1CR \
                                 | AC97_GS_S0CR \
                                 | AC97_GS_MINT \
                                 | AC97_GS_POINT \
                                 | AC97_GS_PIINT \
                                 | AC97_GS_RSRVD \
                                 | AC97_GS_MOINT \
                                 | AC97_GS_MIINT)
#define AC97_GS_VALID_MASK      (RT_BIT(18) - 1)
#define AC97_GS_WCLEAR_MASK     (AC97_GS_RCS | AC97_GS_S1R1 | AC97_GS_S0R1 | AC97_GS_GSCI)
/** @} */

/** @name Buffer Descriptor (BDLE, BDL).
 * @{ */
#define AC97_BD_IOC             RT_BIT(31)          /**< Interrupt on Completion. */
#define AC97_BD_BUP             RT_BIT(30)          /**< Buffer Underrun Policy. */

#define AC97_BD_LEN_MASK        0xFFFF              /**< Mask for the BDL buffer length. */

#define AC97_BD_LEN_CTL_MBZ     UINT32_C(0x3fff0000) /**< Must-be-zero mask for AC97BDLE.ctl_len. */

#define AC97_MAX_BDLE           32                  /**< Maximum number of BDLEs. */
/** @} */

/** @name Extended Audio ID Register (EAID).
 * @{ */
#define AC97_EAID_VRA           RT_BIT(0)           /**< Variable Rate Audio. */
#define AC97_EAID_VRM           RT_BIT(3)           /**< Variable Rate Mic Audio. */
#define AC97_EAID_REV0          RT_BIT(10)          /**< AC'97 revision compliance. */
#define AC97_EAID_REV1          RT_BIT(11)          /**< AC'97 revision compliance. */
/** @} */

/** @name Extended Audio Control and Status Register (EACS).
 * @{ */
#define AC97_EACS_VRA           RT_BIT(0)           /**< Variable Rate Audio (4.2.1.1). */
#define AC97_EACS_VRM           RT_BIT(3)           /**< Variable Rate Mic Audio (4.2.1.1). */
/** @} */

/** @name Baseline Audio Register Set (BARS).
 * @{ */
#define AC97_BARS_VOL_MASK      0x1f                /**< Volume mask for the Baseline Audio Register Set (5.7.2). */
#define AC97_BARS_GAIN_MASK     0x0f                /**< Gain mask for the Baseline Audio Register Set. */
#define AC97_BARS_VOL_MUTE_SHIFT 15                 /**< Mute bit shift for the Baseline Audio Register Set (5.7.2). */
/** @} */

/** AC'97 uses 1.5dB steps, we use 0.375dB steps: 1 AC'97 step equals 4 PDM steps. */
#define AC97_DB_FACTOR          4

/** @name Recording inputs?
 * @{ */
#define AC97_REC_MIC            UINT8_C(0)
#define AC97_REC_CD             UINT8_C(1)
#define AC97_REC_VIDEO          UINT8_C(2)
#define AC97_REC_AUX            UINT8_C(3)
#define AC97_REC_LINE_IN        UINT8_C(4)
#define AC97_REC_STEREO_MIX     UINT8_C(5)
#define AC97_REC_MONO_MIX       UINT8_C(6)
#define AC97_REC_PHONE          UINT8_C(7)
#define AC97_REC_MASK           UINT8_C(7)
/** @} */

/** @name Mixer registers / NAM BAR registers?
 * @{ */
#define AC97_Reset                      0x00
#define AC97_Master_Volume_Mute         0x02
#define AC97_Headphone_Volume_Mute      0x04        /**< Also known as AUX, see table 16, section 5.7. */
#define AC97_Master_Volume_Mono_Mute    0x06
#define AC97_Master_Tone_RL             0x08
#define AC97_PC_BEEP_Volume_Mute        0x0a
#define AC97_Phone_Volume_Mute          0x0c
#define AC97_Mic_Volume_Mute            0x0e
#define AC97_Line_In_Volume_Mute        0x10
#define AC97_CD_Volume_Mute             0x12
#define AC97_Video_Volume_Mute          0x14
#define AC97_Aux_Volume_Mute            0x16
#define AC97_PCM_Out_Volume_Mute        0x18
#define AC97_Record_Select              0x1a
#define AC97_Record_Gain_Mute           0x1c
#define AC97_Record_Gain_Mic_Mute       0x1e
#define AC97_General_Purpose            0x20
#define AC97_3D_Control                 0x22
#define AC97_AC_97_RESERVED             0x24
#define AC97_Powerdown_Ctrl_Stat        0x26
#define AC97_Extended_Audio_ID          0x28
#define AC97_Extended_Audio_Ctrl_Stat   0x2a
#define AC97_PCM_Front_DAC_Rate         0x2c
#define AC97_PCM_Surround_DAC_Rate      0x2e
#define AC97_PCM_LFE_DAC_Rate           0x30
#define AC97_PCM_LR_ADC_Rate            0x32
#define AC97_MIC_ADC_Rate               0x34
#define AC97_6Ch_Vol_C_LFE_Mute         0x36
#define AC97_6Ch_Vol_L_R_Surround_Mute  0x38
#define AC97_Vendor_Reserved            0x58
#define AC97_AD_Misc                    0x76
#define AC97_Vendor_ID1                 0x7c
#define AC97_Vendor_ID2                 0x7e
/** @} */

/** @name Analog Devices miscellaneous regiter bits used in AD1980.
 * @{  */
#define AC97_AD_MISC_LOSEL              RT_BIT(5)   /**< Surround (rear) goes to line out outputs. */
#define AC97_AD_MISC_HPSEL              RT_BIT(10)  /**< PCM (front) goes to headphone outputs. */
/** @} */


/** @name BUP flag values.
 * @{ */
#define BUP_SET                         RT_BIT_32(0)
#define BUP_LAST                        RT_BIT_32(1)
/** @} */

/** @name AC'97 source indices.
 * @note The order of these indices is fixed (also applies for saved states) for
 *       the moment.  So make sure you know what you're done when altering this!
 * @{
 */
#define AC97SOUNDSOURCE_PI_INDEX        0           /**< PCM in */
#define AC97SOUNDSOURCE_PO_INDEX        1           /**< PCM out */
#define AC97SOUNDSOURCE_MC_INDEX        2           /**< Mic in */
#define AC97SOUNDSOURCE_MAX             3           /**< Max sound sources. */
/** @} */

/** Port number (offset into NABM BAR) to stream index. */
#define AC97_PORT2IDX(a_idx)            ( ((a_idx) >> 4) & 3 )
/** Port number (offset into NABM BAR) to stream index, but no masking. */
#define AC97_PORT2IDX_UNMASKED(a_idx)   ( ((a_idx) >> 4) )

/** @name Stream offsets
 * @{ */
#define AC97_NABM_OFF_BDBAR             0x0         /**< Buffer Descriptor Base Address */
#define AC97_NABM_OFF_CIV               0x4         /**< Current Index Value */
#define AC97_NABM_OFF_LVI               0x5         /**< Last Valid Index */
#define AC97_NABM_OFF_SR                0x6         /**< Status Register */
#define AC97_NABM_OFF_PICB              0x8         /**< Position in Current Buffer */
#define AC97_NABM_OFF_PIV               0xa         /**< Prefetched Index Value */
#define AC97_NABM_OFF_CR                0xb         /**< Control Register */
#define AC97_NABM_OFF_MASK              0xf         /**< Mask for getting the the per-stream register. */
/** @} */


/** @name PCM in NABM BAR registers (0x00..0x0f).
 * @{ */
#define PI_BDBAR (AC97SOUNDSOURCE_PI_INDEX * 0x10 + 0x0) /**< PCM in: Buffer Descriptor Base Address */
#define PI_CIV   (AC97SOUNDSOURCE_PI_INDEX * 0x10 + 0x4) /**< PCM in: Current Index Value */
#define PI_LVI   (AC97SOUNDSOURCE_PI_INDEX * 0x10 + 0x5) /**< PCM in: Last Valid Index */
#define PI_SR    (AC97SOUNDSOURCE_PI_INDEX * 0x10 + 0x6) /**< PCM in: Status Register */
#define PI_PICB  (AC97SOUNDSOURCE_PI_INDEX * 0x10 + 0x8) /**< PCM in: Position in Current Buffer */
#define PI_PIV   (AC97SOUNDSOURCE_PI_INDEX * 0x10 + 0xa) /**< PCM in: Prefetched Index Value */
#define PI_CR    (AC97SOUNDSOURCE_PI_INDEX * 0x10 + 0xb) /**< PCM in: Control Register */
/** @} */

/** @name PCM out NABM BAR registers (0x10..0x1f).
 * @{ */
#define PO_BDBAR (AC97SOUNDSOURCE_PO_INDEX * 0x10 + 0x0) /**< PCM out: Buffer Descriptor Base Address */
#define PO_CIV   (AC97SOUNDSOURCE_PO_INDEX * 0x10 + 0x4) /**< PCM out: Current Index Value */
#define PO_LVI   (AC97SOUNDSOURCE_PO_INDEX * 0x10 + 0x5) /**< PCM out: Last Valid Index */
#define PO_SR    (AC97SOUNDSOURCE_PO_INDEX * 0x10 + 0x6) /**< PCM out: Status Register */
#define PO_PICB  (AC97SOUNDSOURCE_PO_INDEX * 0x10 + 0x8) /**< PCM out: Position in Current Buffer */
#define PO_PIV   (AC97SOUNDSOURCE_PO_INDEX * 0x10 + 0xa) /**< PCM out: Prefetched Index Value */
#define PO_CR    (AC97SOUNDSOURCE_PO_INDEX * 0x10 + 0xb) /**< PCM out: Control Register */
/** @} */

/** @name Mic in NABM BAR registers (0x20..0x2f).
 * @{ */
#define MC_BDBAR (AC97SOUNDSOURCE_MC_INDEX * 0x10 + 0x0) /**< PCM in: Buffer Descriptor Base Address */
#define MC_CIV   (AC97SOUNDSOURCE_MC_INDEX * 0x10 + 0x4) /**< PCM in: Current Index Value */
#define MC_LVI   (AC97SOUNDSOURCE_MC_INDEX * 0x10 + 0x5) /**< PCM in: Last Valid Index */
#define MC_SR    (AC97SOUNDSOURCE_MC_INDEX * 0x10 + 0x6) /**< PCM in: Status Register */
#define MC_PICB  (AC97SOUNDSOURCE_MC_INDEX * 0x10 + 0x8) /**< PCM in: Position in Current Buffer */
#define MC_PIV   (AC97SOUNDSOURCE_MC_INDEX * 0x10 + 0xa) /**< PCM in: Prefetched Index Value */
#define MC_CR    (AC97SOUNDSOURCE_MC_INDEX * 0x10 + 0xb) /**< PCM in: Control Register */
/** @} */

/** @name Misc NABM BAR registers.
 * @{  */
/** NABMBAR: Global Control Register.
 * @note This is kind of in the MIC IN area.  */
#define AC97_GLOB_CNT                   0x2c
/** NABMBAR: Global Status. */
#define AC97_GLOB_STA                   0x30
/** Codec Access Semaphore Register. */
#define AC97_CAS                        0x34
/** @} */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** The ICH AC'97 (Intel) controller (shared). */
typedef struct AC97STATE *PAC97STATE;
/** The ICH AC'97 (Intel) controller (ring-3). */
typedef struct AC97STATER3 *PAC97STATER3;

/**
 * Buffer Descriptor List Entry (BDLE).
 *
 * (See section 3.2.1 in Intel document number 252751-001, or section 1.2.2.1 in
 * Intel document number 302349-003.)
 */
typedef struct AC97BDLE
{
    /** Location of data buffer (bits 31:1). */
    uint32_t                addr;
    /** Flags (bits 31 + 30) and length (bits 15:0) of data buffer (in audio samples).
     * @todo split up into two 16-bit fields.  */
    uint32_t                ctl_len;
} AC97BDLE;
AssertCompileSize(AC97BDLE, 8);
/** Pointer to BDLE. */
typedef AC97BDLE *PAC97BDLE;

/**
 * Bus master register set for an audio stream.
 *
 * (See section 16.2 in Intel document 301473-002, or section 2.2 in Intel
 * document 302349-003.)
 */
typedef struct AC97BMREGS
{
    uint32_t                bdbar;      /**< rw 0, Buffer Descriptor List: BAR (Base Address Register). */
    uint8_t                 civ;        /**< ro 0, Current index value. */
    uint8_t                 lvi;        /**< rw 0, Last valid index. */
    uint16_t                sr;         /**< rw 1, Status register. */
    uint16_t                picb;       /**< ro 0, Position in current buffer (samples left to process). */
    uint8_t                 piv;        /**< ro 0, Prefetched index value. */
    uint8_t                 cr;         /**< rw 0, Control register. */
    int32_t                 bd_valid;   /**< Whether current BDLE is initialized or not. */
    AC97BDLE                bd;         /**< Current Buffer Descriptor List Entry (BDLE). */
} AC97BMREGS;
AssertCompileSizeAlignment(AC97BMREGS, 8);
/** Pointer to the BM registers of an audio stream. */
typedef AC97BMREGS *PAC97BMREGS;

/**
 * The internal state of an AC'97 stream.
 */
typedef struct AC97STREAMSTATE
{
    /** Critical section for this stream. */
    RTCRITSECT              CritSect;
    /** Circular buffer (FIFO) for holding DMA'ed data. */
    R3PTRTYPE(PRTCIRCBUF)   pCircBuf;
#if HC_ARCH_BITS == 32
    uint32_t                Padding;
#endif
    /** Current circular buffer read offset (for tracing & logging). */
    uint64_t                offRead;
    /** Current circular buffer write offset (for tracing & logging). */
    uint64_t                offWrite;
    /** The stream's current configuration. */
    PDMAUDIOSTREAMCFG       Cfg; //+108
    /** Timestamp of the last DMA data transfer. */
    uint64_t                tsTransferLast;
    /** Timestamp of the next DMA data transfer.
     *  Next for determining the next scheduling window.
     *  Can be 0 if no next transfer is scheduled. */
    uint64_t                tsTransferNext;
    /** The stream's timer Hz rate.
     *  This value can can be different from the device's default Hz rate,
     *  depending on the rate the stream expects (e.g. for 5.1 speaker setups).
     *  Set in R3StreamInit(). */
    uint16_t                uTimerHz;
    /** Set if we've registered the asynchronous update job. */
    bool                    fRegisteredAsyncUpdateJob;
    /** Input streams only: Set when we switch from feeding the guest silence and
     *  commits to proving actual audio input bytes. */
    bool                    fInputPreBuffered;
    /** This is ZERO if stream setup succeeded, otherwise it's the RTTimeNanoTS() at
     *  which to retry setting it up.  The latter applies only to same
     *  parameters. */
    uint64_t                nsRetrySetup;
    /** Timestamp (in ns) of last stream update. */
    uint64_t                tsLastUpdateNs;

    /** Size of the DMA buffer (pCircBuf) in bytes. */
    uint32_t                StatDmaBufSize;
    /** Number of used bytes in the DMA buffer (pCircBuf). */
    uint32_t                StatDmaBufUsed;
    /** Counter for all under/overflows problems. */
    STAMCOUNTER             StatDmaFlowProblems;
    /** Counter for unresovled under/overflows problems. */
    STAMCOUNTER             StatDmaFlowErrors;
    /** Number of bytes involved in unresolved flow errors. */
    STAMCOUNTER             StatDmaFlowErrorBytes;
    STAMCOUNTER             StatDmaSkippedDch;
    STAMCOUNTER             StatDmaSkippedPendingBcis;
    STAMPROFILE             StatStart;
    STAMPROFILE             StatReset;
    STAMPROFILE             StatStop;
    STAMPROFILE             StatReSetUpChanged;
    STAMPROFILE             StatReSetUpSame;
    STAMCOUNTER             StatWriteLviRecover;
    STAMCOUNTER             StatWriteCr;
} AC97STREAMSTATE;
AssertCompileSizeAlignment(AC97STREAMSTATE, 8);
/** Pointer to internal state of an AC'97 stream. */
typedef AC97STREAMSTATE *PAC97STREAMSTATE;

/**
 * Runtime configurable debug stuff for an AC'97 stream.
 */
typedef struct AC97STREAMDEBUGRT
{
    /** Whether debugging is enabled or not. */
    bool                        fEnabled;
    uint8_t                     Padding[7];
    /** File for dumping stream reads / writes.
     *  For input streams, this dumps data being written to the device FIFO,
     *  whereas for output streams this dumps data being read from the device FIFO. */
    R3PTRTYPE(PAUDIOHLPFILE)    pFileStream;
    /** File for dumping DMA reads / writes.
     *  For input streams, this dumps data being written to the device DMA,
     *  whereas for output streams this dumps data being read from the device DMA. */
    R3PTRTYPE(PAUDIOHLPFILE)    pFileDMA;
} AC97STREAMDEBUGRT;

/**
 * Debug stuff for an AC'97 stream.
 */
typedef struct AC97STREAMDEBUG
{
    /** Runtime debug stuff. */
    AC97STREAMDEBUGRT       Runtime;
} AC97STREAMDEBUG;

/**
 * The shared AC'97 stream state.
 */
typedef struct AC97STREAM
{
    /** Bus master registers of this stream. */
    AC97BMREGS              Regs;
    /** Stream number (SDn). */
    uint8_t                 u8SD;
    uint8_t                 abPadding0[7];

    /** The timer for pumping data thru the attached LUN drivers. */
    TMTIMERHANDLE           hTimer;
    /** When the timer was armed (timer clock). */
    uint64_t                uArmedTs;
    /** (Virtual) clock ticks per transfer. */
    uint64_t                cDmaPeriodTicks;
    /** Transfer chunk size (in bytes) of a transfer period. */
    uint32_t                cbDmaPeriod;
    /** DMA period counter (for logging). */
    uint32_t                uDmaPeriod;

    STAMCOUNTER             StatWriteLvi;
    STAMCOUNTER             StatWriteSr1;
    STAMCOUNTER             StatWriteSr2;
    STAMCOUNTER             StatWriteBdBar;
} AC97STREAM;
AssertCompileSizeAlignment(AC97STREAM, 8);
/** Pointer to a shared AC'97 stream state. */
typedef AC97STREAM *PAC97STREAM;


/**
 * The ring-3 AC'97 stream state.
 */
typedef struct AC97STREAMR3
{
    /** Stream number (SDn). */
    uint8_t                 u8SD;
    uint8_t                 abPadding0[7];
    /** Internal state of this stream. */
    AC97STREAMSTATE         State;
    /** Debug stuff. */
    AC97STREAMDEBUG         Dbg;
} AC97STREAMR3;
AssertCompileSizeAlignment(AC97STREAMR3, 8);
/** Pointer to an AC'97 stream state for ring-3. */
typedef AC97STREAMR3 *PAC97STREAMR3;


/**
 * A driver stream (host backend).
 *
 * Each driver has its own instances of audio mixer streams, which then
 * can go into the same (or even different) audio mixer sinks.
 */
typedef struct AC97DRIVERSTREAM
{
    /** Associated mixer stream handle. */
    R3PTRTYPE(PAUDMIXSTREAM)    pMixStrm;
} AC97DRIVERSTREAM;
/** Pointer to a driver stream. */
typedef AC97DRIVERSTREAM *PAC97DRIVERSTREAM;

/**
 * A host backend driver (LUN).
 */
typedef struct AC97DRIVER
{
    /** Node for storing this driver in our device driver list of AC97STATE. */
    RTLISTNODER3                    Node;
    /** LUN # to which this driver has been assigned. */
    uint8_t                         uLUN;
    /** Whether this driver is in an attached state or not. */
    bool                            fAttached;
    uint8_t                         abPadding[6];
    /** Pointer to attached driver base interface. */
    R3PTRTYPE(PPDMIBASE)            pDrvBase;
    /** Audio connector interface to the underlying host backend. */
    R3PTRTYPE(PPDMIAUDIOCONNECTOR)  pConnector;
    /** Driver stream for line input. */
    AC97DRIVERSTREAM                LineIn;
    /** Driver stream for mic input. */
    AC97DRIVERSTREAM                MicIn;
    /** Driver stream for output. */
    AC97DRIVERSTREAM                Out;
    /** The LUN description. */
    char                            szDesc[48 - 2];
} AC97DRIVER;
/** Pointer to a host backend driver (LUN). */
typedef AC97DRIVER *PAC97DRIVER;

/**
 * Debug settings.
 */
typedef struct AC97STATEDEBUG
{
    /** Whether debugging is enabled or not. */
    bool                    fEnabled;
    bool                    afAlignment[7];
    /** Path where to dump the debug output to.
     *  Can be NULL, in which the system's temporary directory will be used then. */
    R3PTRTYPE(char *)       pszOutPath;
} AC97STATEDEBUG;


/* Codec models. */
typedef enum AC97CODEC
{
    AC97CODEC_INVALID = 0,      /**< Customary illegal zero value. */
    AC97CODEC_STAC9700,         /**< SigmaTel STAC9700 */
    AC97CODEC_AD1980,           /**< Analog Devices AD1980 */
    AC97CODEC_AD1981B,          /**< Analog Devices AD1981B */
    AC97CODEC_32BIT_HACK = 0x7fffffff
} AC97CODEC;


/**
 * The shared AC'97 device state.
 */
typedef struct AC97STATE
{
    /** Critical section protecting the AC'97 state. */
    PDMCRITSECT             CritSect;
    /** Global Control (Bus Master Control Register). */
    uint32_t                glob_cnt;
    /** Global Status (Bus Master Control Register). */
    uint32_t                glob_sta;
    /** Codec Access Semaphore Register (Bus Master Control Register). */
    uint32_t                cas;
    uint32_t                last_samp;
    uint8_t                 mixer_data[256];
    /** Array of AC'97 streams (parallel to AC97STATER3::aStreams). */
    AC97STREAM              aStreams[AC97_MAX_STREAMS];
    /** The device timer Hz rate. Defaults to AC97_TIMER_HZ_DEFAULT_DEFAULT. */
    uint16_t                uTimerHz;
    /** Config: Internal input DMA buffer size override, specified in milliseconds.
     * Zero means default size according to buffer and stream config.
     * @sa BufSizeInMs config value.  */
    uint16_t                cMsCircBufIn;
    /** Config: Internal output DMA buffer size override, specified in milliseconds.
     * Zero means default size according to buffer and stream config.
     * @sa BufSizeOutMs config value.  */
    uint16_t                cMsCircBufOut;
    uint16_t                au16Padding1[1];
    uint8_t                 silence[128];
    uint32_t                bup_flag;
    /** Codec model. */
    AC97CODEC               enmCodecModel;

    /** PCI region \#0: NAM I/O ports. */
    IOMIOPORTHANDLE         hIoPortsNam;
    /** PCI region \#0: NANM I/O ports. */
    IOMIOPORTHANDLE         hIoPortsNabm;

    STAMCOUNTER             StatUnimplementedNabmReads;
    STAMCOUNTER             StatUnimplementedNabmWrites;
    STAMCOUNTER             StatUnimplementedNamReads;
    STAMCOUNTER             StatUnimplementedNamWrites;
#ifdef VBOX_WITH_STATISTICS
    STAMPROFILE             StatTimer;
#endif
} AC97STATE;
AssertCompileMemberAlignment(AC97STATE, aStreams, 8);
AssertCompileMemberAlignment(AC97STATE, StatUnimplementedNabmReads, 8);


/**
 * The ring-3 AC'97 device state.
 */
typedef struct AC97STATER3
{
    /** Array of AC'97 streams (parallel to AC97STATE:aStreams). */
    AC97STREAMR3            aStreams[AC97_MAX_STREAMS];
    /** R3 pointer to the device instance. */
    PPDMDEVINSR3            pDevIns;
    /** List of associated LUN drivers (AC97DRIVER). */
    RTLISTANCHORR3          lstDrv;
    /** The device's software mixer. */
    R3PTRTYPE(PAUDIOMIXER)  pMixer;
    /** Audio sink for PCM output. */
    R3PTRTYPE(PAUDMIXSINK)  pSinkOut;
    /** Audio sink for line input. */
    R3PTRTYPE(PAUDMIXSINK)  pSinkLineIn;
    /** Audio sink for microphone input. */
    R3PTRTYPE(PAUDMIXSINK)  pSinkMicIn;
    /** The base interface for LUN\#0. */
    PDMIBASE                IBase;
    /** Debug settings. */
    AC97STATEDEBUG          Dbg;
} AC97STATER3;
AssertCompileMemberAlignment(AC97STATER3, aStreams, 8);
/** Pointer to the ring-3 AC'97 device state. */
typedef AC97STATER3 *PAC97STATER3;


/**
 * Acquires the AC'97 lock.
 */
#define DEVAC97_LOCK(a_pDevIns, a_pThis) \
    do { \
        int const rcLock = PDMDevHlpCritSectEnter((a_pDevIns), &(a_pThis)->CritSect, VERR_IGNORED); \
        PDM_CRITSECT_RELEASE_ASSERT_RC_DEV((a_pDevIns), &(a_pThis)->CritSect, rcLock); \
    } while (0)

/**
 * Acquires the AC'97 lock or returns.
 */
# define DEVAC97_LOCK_RETURN(a_pDevIns, a_pThis, a_rcBusy) \
    do { \
        int rcLock = PDMDevHlpCritSectEnter((a_pDevIns), &(a_pThis)->CritSect, a_rcBusy); \
        if (rcLock == VINF_SUCCESS) \
        { /* likely */ } \
        else \
        { \
            AssertRC(rcLock); \
            return rcLock; \
        } \
    } while (0)

/**
 * Releases the AC'97 lock.
 */
#define DEVAC97_UNLOCK(a_pDevIns, a_pThis) \
    do { PDMDevHlpCritSectLeave((a_pDevIns), &(a_pThis)->CritSect); } while (0)


#ifndef VBOX_DEVICE_STRUCT_TESTCASE


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void                 ichac97StreamUpdateSR(PPDMDEVINS pDevIns, PAC97STATE pThis, PAC97STREAM pStream, uint32_t new_sr);
static uint16_t             ichac97MixerGet(PAC97STATE pThis, uint32_t uMixerIdx);
#ifdef IN_RING3
DECLINLINE(void)            ichac97R3StreamLock(PAC97STREAMR3 pStreamCC);
DECLINLINE(void)            ichac97R3StreamUnlock(PAC97STREAMR3 pStreamCC);
static void                 ichac97R3DbgPrintBdl(PPDMDEVINS pDevIns, PAC97STATE pThis, PAC97STREAM pStream,
                                                 PCDBGFINFOHLP pHlp, const char *pszPrefix);
static DECLCALLBACK(void)   ichac97R3Reset(PPDMDEVINS pDevIns);
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef IN_RING3
/** NABM I/O port descriptions. */
static const IOMIOPORTDESC g_aNabmPorts[] =
{
    { "PCM IN - BDBAR",     "PCM IN - BDBAR",   NULL, NULL },
    { "",                   NULL,               NULL, NULL },
    { "",                   NULL,               NULL, NULL },
    { "",                   NULL,               NULL, NULL },
    { "PCM IN - CIV",       "PCM IN - CIV",     NULL, NULL },
    { "PCM IN - LVI",       "PCM IN - LIV",     NULL, NULL },
    { "PCM IN - SR",        "PCM IN - SR",      NULL, NULL },
    { "",                   NULL,               NULL, NULL },
    { "PCM IN - PICB",      "PCM IN - PICB",    NULL, NULL },
    { "",                   NULL,               NULL, NULL },
    { "PCM IN - PIV",       "PCM IN - PIV",     NULL, NULL },
    { "PCM IN - CR",        "PCM IN - CR",      NULL, NULL },
    { "",                   NULL,               NULL, NULL },
    { "",                   NULL,               NULL, NULL },
    { "",                   NULL,               NULL, NULL },
    { "",                   NULL,               NULL, NULL },

    { "PCM OUT - BDBAR",    "PCM OUT - BDBAR",  NULL, NULL },
    { "",                   NULL,               NULL, NULL },
    { "",                   NULL,               NULL, NULL },
    { "",                   NULL,               NULL, NULL },
    { "PCM OUT - CIV",      "PCM OUT - CIV",    NULL, NULL },
    { "PCM OUT - LVI",      "PCM OUT - LIV",    NULL, NULL },
    { "PCM OUT - SR",       "PCM OUT - SR",     NULL, NULL },
    { "",                   NULL,               NULL, NULL },
    { "PCM OUT - PICB",     "PCM OUT - PICB",   NULL, NULL },
    { "",                   NULL,               NULL, NULL },
    { "PCM OUT - PIV",      "PCM OUT - PIV",    NULL, NULL },
    { "PCM OUT - CR",       "PCM IN - CR",      NULL, NULL },
    { "",                   NULL,               NULL, NULL },
    { "",                   NULL,               NULL, NULL },
    { "",                   NULL,               NULL, NULL },
    { "",                   NULL,               NULL, NULL },

    { "MIC IN - BDBAR",     "MIC IN - BDBAR",   NULL, NULL },
    { "",                   NULL,               NULL, NULL },
    { "",                   NULL,               NULL, NULL },
    { "",                   NULL,               NULL, NULL },
    { "MIC IN - CIV",       "MIC IN - CIV",     NULL, NULL },
    { "MIC IN - LVI",       "MIC IN - LIV",     NULL, NULL },
    { "MIC IN - SR",        "MIC IN - SR",      NULL, NULL },
    { "",                   NULL,               NULL, NULL },
    { "MIC IN - PICB",      "MIC IN - PICB",    NULL, NULL },
    { "",                   NULL,               NULL, NULL },
    { "MIC IN - PIV",       "MIC IN - PIV",     NULL, NULL },
    { "MIC IN - CR",        "MIC IN - CR",      NULL, NULL },
    { "GLOB CNT",           "GLOB CNT",         NULL, NULL },
    { "",                   NULL,               NULL, NULL },
    { "",                   NULL,               NULL, NULL },
    { "",                   NULL,               NULL, NULL },

    { "GLOB STA",           "GLOB STA",         NULL, NULL },
    { "",                   NULL,               NULL, NULL },
    { "",                   NULL,               NULL, NULL },
    { "",                   NULL,               NULL, NULL },
    { "CAS",                "CAS",              NULL, NULL },
    { NULL,                 NULL,               NULL, NULL },
};

/** @name Source indices
 * @{ */
# define AC97SOUNDSOURCE_PI_INDEX       0           /**< PCM in */
# define AC97SOUNDSOURCE_PO_INDEX       1           /**< PCM out */
# define AC97SOUNDSOURCE_MC_INDEX       2           /**< Mic in */
# define AC97SOUNDSOURCE_MAX            3           /**< Max sound sources. */
/** @} */

/** Port number (offset into NABM BAR) to stream index. */
# define AC97_PORT2IDX(a_idx)           ( ((a_idx) >> 4) & 3 )
/** Port number (offset into NABM BAR) to stream index, but no masking. */
# define AC97_PORT2IDX_UNMASKED(a_idx)  ( ((a_idx) >> 4) )

/** @name Stream offsets
 * @{ */
# define AC97_NABM_OFF_BDBAR            0x0         /**< Buffer Descriptor Base Address */
# define AC97_NABM_OFF_CIV              0x4         /**< Current Index Value */
# define AC97_NABM_OFF_LVI              0x5         /**< Last Valid Index */
# define AC97_NABM_OFF_SR               0x6         /**< Status Register */
# define AC97_NABM_OFF_PICB             0x8         /**< Position in Current Buffer */
# define AC97_NABM_OFF_PIV              0xa         /**< Prefetched Index Value */
# define AC97_NABM_OFF_CR               0xb         /**< Control Register */
# define AC97_NABM_OFF_MASK             0xf         /**< Mask for getting the the per-stream register. */
/** @} */

#endif /* IN_RING3 */



static void ichac97WarmReset(PAC97STATE pThis)
{
    NOREF(pThis);
}

static void ichac97ColdReset(PAC97STATE pThis)
{
    NOREF(pThis);
}


#ifdef IN_RING3

/**
 * Returns the audio direction of a specified stream descriptor.
 *
 * @return  Audio direction.
 */
DECLINLINE(PDMAUDIODIR) ichac97R3GetDirFromSD(uint8_t uSD)
{
    switch (uSD)
    {
        case AC97SOUNDSOURCE_PI_INDEX: return PDMAUDIODIR_IN;
        case AC97SOUNDSOURCE_PO_INDEX: return PDMAUDIODIR_OUT;
        case AC97SOUNDSOURCE_MC_INDEX: return PDMAUDIODIR_IN;
    }

    AssertFailed();
    return PDMAUDIODIR_UNKNOWN;
}


/**
 * Retrieves the audio mixer sink of a corresponding AC'97 stream index.
 *
 * @returns Pointer to audio mixer sink if found, or NULL if not found / invalid.
 * @param   pThisCC             The ring-3 AC'97 state.
 * @param   uIndex              Stream index to get audio mixer sink for.
 */
DECLINLINE(PAUDMIXSINK) ichac97R3IndexToSink(PAC97STATER3 pThisCC, uint8_t uIndex)
{
    switch (uIndex)
    {
        case AC97SOUNDSOURCE_PI_INDEX: return pThisCC->pSinkLineIn;
        case AC97SOUNDSOURCE_PO_INDEX: return pThisCC->pSinkOut;
        case AC97SOUNDSOURCE_MC_INDEX: return pThisCC->pSinkMicIn;
        default:
            AssertMsgFailedReturn(("Wrong index %RU8\n", uIndex), NULL);
    }
}


/*********************************************************************************************************************************
*   Stream DMA                                                                                                                   *
*********************************************************************************************************************************/

/**
 * Retrieves the available size of (buffered) audio data (in bytes) of a given AC'97 stream.
 *
 * @returns Available data (in bytes).
 * @param   pStreamCC           The AC'97 stream to retrieve size for (ring-3).
 */
DECLINLINE(uint32_t) ichac97R3StreamGetUsed(PAC97STREAMR3 pStreamCC)
{
    PRTCIRCBUF const pCircBuf = pStreamCC->State.pCircBuf;
    if (pCircBuf)
        return (uint32_t)RTCircBufUsed(pCircBuf);
    return 0;
}


/**
 * Retrieves the free size of audio data (in bytes) of a given AC'97 stream.
 *
 * @returns Free data (in bytes).
 * @param   pStreamCC           AC'97 stream to retrieve size for (ring-3).
 */
DECLINLINE(uint32_t) ichac97R3StreamGetFree(PAC97STREAMR3 pStreamCC)
{
    PRTCIRCBUF const pCircBuf = pStreamCC->State.pCircBuf;
    if (pCircBuf)
        return (uint32_t)RTCircBufFree(pCircBuf);
    return 0;
}


# if 0 /* Unused */
static void ichac97R3WriteBUP(PAC97STATE pThis, uint32_t cbElapsed)
{
    LogFlowFunc(("cbElapsed=%RU32\n", cbElapsed));

    if (!(pThis->bup_flag & BUP_SET))
    {
        if (pThis->bup_flag & BUP_LAST)
        {
            unsigned int i;
            uint32_t *p = (uint32_t*)pThis->silence;
            for (i = 0; i < sizeof(pThis->silence) / 4; i++) /** @todo r=andy Assumes 16-bit samples, stereo. */
                *p++ = pThis->last_samp;
        }
        else
            RT_ZERO(pThis->silence);

        pThis->bup_flag |= BUP_SET;
    }

    while (cbElapsed)
    {
        uint32_t cbToWrite = RT_MIN(cbElapsed, (uint32_t)sizeof(pThis->silence));
        uint32_t cbWrittenToStream;

        int rc2 = AudioMixerSinkWrite(pThisCC->pSinkOut, AUDMIXOP_COPY,
                                      pThis->silence, cbToWrite, &cbWrittenToStream);
        if (RT_SUCCESS(rc2))
        {
            if (cbWrittenToStream < cbToWrite) /* Lagging behind? */
                LogFlowFunc(("Warning: Only written %RU32 / %RU32 bytes, expect lags\n", cbWrittenToStream, cbToWrite));
        }

        /* Always report all data as being written;
         * backends who were not able to catch up have to deal with it themselves. */
        Assert(cbElapsed >= cbToWrite);
        cbElapsed -= cbToWrite;
    }
}
# endif /* Unused */


/**
 * Fetches the next buffer descriptor (BDLE) updating the stream registers.
 *
 * This will skip zero length descriptors.
 *
 * @returns Zero, or AC97_SR_BCIS if skipped zero length buffer with IOC set.
 * @param   pDevIns             The device instance.
 * @param   pStream             AC'97 stream to fetch BDLE for.
 * @param   pStreamCC           The AC'97 stream, ring-3 state.
 *
 * @remarks Updates CIV, PIV, BD and PICB.
 *
 * @note    Both PIV and CIV will be zero after a stream reset, so the first
 *          time we advance the buffer position afterwards, CIV will remain zero
 *          and PIV becomes 1.  Thus we will start processing from BDLE00 and
 *          not BDLE01 as CIV=0 may lead you to think.
 */
static uint32_t ichac97R3StreamFetchNextBdle(PPDMDEVINS pDevIns, PAC97STREAM pStream, PAC97STREAMR3 pStreamCC)
{
    RT_NOREF(pStreamCC);
    uint32_t fSrBcis = 0;
    uint32_t cbTotal = 0; /* Counts the total length (in bytes) of the buffer descriptor list (BDL). */

    /*
     * Loop for skipping zero length entries.
     */
    for (;;)
    {
        /* Advance the buffer. */
        pStream->Regs.civ = pStream->Regs.piv % AC97_MAX_BDLE /* (paranoia) */;
        pStream->Regs.piv = (pStream->Regs.piv + 1) % AC97_MAX_BDLE;

        /* Load it. */
        AC97BDLE Bdle = { 0, 0 };
        PDMDevHlpPCIPhysRead(pDevIns, pStream->Regs.bdbar + pStream->Regs.civ * sizeof(AC97BDLE), &Bdle, sizeof(AC97BDLE));
        pStream->Regs.bd_valid   = 1;
        pStream->Regs.bd.addr    = RT_H2LE_U32(Bdle.addr) & ~3;
        pStream->Regs.bd.ctl_len = RT_H2LE_U32(Bdle.ctl_len);
        pStream->Regs.picb       = pStream->Regs.bd.ctl_len & AC97_BD_LEN_MASK;

        cbTotal += pStream->Regs.bd.ctl_len & AC97_BD_LEN_MASK;

        LogFlowFunc(("BDLE%02u: %#RX32 L %#x / LB %#x, ctl=%#06x%s%s\n",
                     pStream->Regs.civ, pStream->Regs.bd.addr, pStream->Regs.bd.ctl_len & AC97_BD_LEN_MASK,
                     (pStream->Regs.bd.ctl_len & AC97_BD_LEN_MASK) * PDMAudioPropsSampleSize(&pStreamCC->State.Cfg.Props),
                     pStream->Regs.bd.ctl_len >> 16,
                     pStream->Regs.bd.ctl_len & AC97_BD_IOC ? " ioc" : "",
                     pStream->Regs.bd.ctl_len & AC97_BD_BUP ? " bup" : ""));

        /* Complain about any reserved bits set in CTL and ADDR: */
        ASSERT_GUEST_MSG(!(pStream->Regs.bd.ctl_len & AC97_BD_LEN_CTL_MBZ),
                         ("Reserved bits set: %#RX32\n", pStream->Regs.bd.ctl_len));
        ASSERT_GUEST_MSG(!(RT_H2LE_U32(Bdle.addr) & 3),
                         ("Reserved addr bits set: %#RX32\n", RT_H2LE_U32(Bdle.addr) ));

        /* If the length is non-zero or if we've reached LVI, we're done regardless
           of what's been loaded.  Otherwise, we skip zero length buffers. */
        if (pStream->Regs.picb)
            break;
        if (pStream->Regs.civ == (pStream->Regs.lvi % AC97_MAX_BDLE /* (paranoia) */))
        {
            LogFunc(("BDLE%02u is zero length! Can't skip (CIV=LVI). %#RX32 %#RX32\n", pStream->Regs.civ, Bdle.addr, Bdle.ctl_len));
            break;
        }
        LogFunc(("BDLE%02u is zero length! Skipping. %#RX32 %#RX32\n", pStream->Regs.civ, Bdle.addr, Bdle.ctl_len));

        /* If the buffer has IOC set, make sure it's triggered by the caller. */
        if (pStream->Regs.bd.ctl_len & AC97_BD_IOC)
            fSrBcis |= AC97_SR_BCIS;
    }

    /* 1.2.4.2 PCM Buffer Restrictions (in 302349-003) - #1  */
    ASSERT_GUEST_MSG(!(pStream->Regs.picb & 1),
                     ("Odd lengths buffers are not allowed: %#x (%d) samples\n",  pStream->Regs.picb, pStream->Regs.picb));

    /* 1.2.4.2 PCM Buffer Restrictions (in 302349-003) - #2
     *
     * Note: Some guests (like older NetBSDs) first seem to set up the BDL a tad later so that cbTotal is 0.
     *       This means that the BDL is not set up at all.
     *       In such cases pStream->Regs.picb also will be 0 here and (debug) asserts here, which is annoying for debug builds.
     *       So first check if we have *any* BDLE set up before checking if PICB is > 0.
     */
    ASSERT_GUEST_MSG(cbTotal == 0 || pStream->Regs.picb > 0, ("Zero length buffers not allowed to terminate list (LVI=%u CIV=%u, cbTotal=%zu)\n",
                                                              pStream->Regs.lvi, pStream->Regs.civ, cbTotal));

    return fSrBcis;
}


/**
 * Transfers data of an AC'97 stream according to its usage (input / output).
 *
 * For an SDO (output) stream this means reading DMA data from the device to
 * the AC'97 stream's internal FIFO buffer.
 *
 * For an SDI (input) stream this is reading audio data from the AC'97 stream's
 * internal FIFO buffer and writing it as DMA data to the device.
 *
 * @returns VBox status code.
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared AC'97 state.
 * @param   pStream             The AC'97 stream to update (shared).
 * @param   pStreamCC           The AC'97 stream to update (ring-3).
 * @param   cbToProcess         The max amount of data to process (i.e.
 *                              put into / remove from the circular buffer).
 *                              Unless something is going seriously wrong, this
 *                              will always be transfer size for the current
 *                              period.   The current period will never be
 *                              larger than what can be stored in the current
 *                              buffer (i.e. what PICB indicates).
 * @param   fWriteSilence       Whether to write silence if this is an input
 *                              stream (done while waiting for backend to get
 *                              going).
 * @param   fInput              Set if input, clear if output.
 */
static int ichac97R3StreamTransfer(PPDMDEVINS pDevIns, PAC97STATE pThis, PAC97STREAM pStream,
                                   PAC97STREAMR3 pStreamCC, uint32_t cbToProcess, bool fWriteSilence, bool fInput)
{
    if (RT_LIKELY(cbToProcess > 0))
        Assert(PDMAudioPropsIsSizeAligned(&pStreamCC->State.Cfg.Props, cbToProcess));
    else
        return VINF_SUCCESS;

    ichac97R3StreamLock(pStreamCC);

    /*
     * Check that the controller is not halted (DCH) and that the buffer
     * completion interrupt isn't pending.
     */
    /** @todo r=bird: Why do we not just barge ahead even when BCIS is set?  Can't
     *        find anything in spec indicating that we shouldn't.  Linux shouldn't
     *        care if be bundle IOCs, as it checks how many steps we've taken using
     *        CIV.  The Windows AC'97 sample driver doesn't care at all, since it
     *        just sets LIV to CIV-1  (thought that's probably not what the real
     *        windows driver does)...
     *
     *        This is not going to sound good if it happens often enough, because
     *        each time we'll lose one DMA period (exact length depends on the
     *        buffer here).
     *
     *        If we're going to keep this hack, there should be a
     *        PDMDevHlpTimerSetRelative call arm-ing the DMA timer to fire shortly
     *        after BCIS is cleared.  Otherwise, we might lag behind even more
     *        before we get stuff going again.
     *
     *        I just wish there was some clear reasoning in the source code for
     *        weird shit like this.  This is just random voodoo.  Sigh^3! */
    if (!(pStream->Regs.sr & (AC97_SR_DCH | AC97_SR_BCIS))) /* Controller halted? */
    { /* not halted nor does it have pending interrupt - likely */ }
    else
    {
        /** @todo Stop DMA timer when DCH is set. */
        if (pStream->Regs.sr & AC97_SR_DCH)
        {
            STAM_REL_COUNTER_INC(&pStreamCC->State.StatDmaSkippedDch);
            LogFunc(("[SD%RU8] DCH set\n", pStream->u8SD));
        }
        if (pStream->Regs.sr & AC97_SR_BCIS)
        {
            STAM_REL_COUNTER_INC(&pStreamCC->State.StatDmaSkippedPendingBcis);
            LogFunc(("[SD%RU8] BCIS set\n", pStream->u8SD));
        }
        if ((pStream->Regs.cr & AC97_CR_RPBM) /* Bus master operation started. */ && !fInput)
        {
            /*ichac97R3WriteBUP(pThis, cbToProcess);*/
        }

        ichac97R3StreamUnlock(pStreamCC);
        return VINF_SUCCESS;
    }

    /*                                                           0x1ba*2 = 0x374 (884) 0x3c0
     * Transfer loop.
     */
#ifdef LOG_ENABLED
    uint32_t   cbProcessedTotal = 0;
#endif
    int        rc               = VINF_SUCCESS;
    PRTCIRCBUF pCircBuf         = pStreamCC->State.pCircBuf;
    AssertReturnStmt(pCircBuf, ichac97R3StreamUnlock(pStreamCC), VINF_SUCCESS);
    Assert((uint32_t)pStream->Regs.picb * PDMAudioPropsSampleSize(&pStreamCC->State.Cfg.Props) >= cbToProcess);
    Log3Func(("[SD%RU8] cbToProcess=%#x PICB=%#x/%#x\n", pStream->u8SD, cbToProcess,
              pStream->Regs.picb, pStream->Regs.picb * PDMAudioPropsSampleSize(&pStreamCC->State.Cfg.Props)));

    while (cbToProcess > 0)
    {
        uint32_t cbChunk = cbToProcess;

        /*
         * Output.
         */
        if (!fInput)
        {
            void  *pvDst = NULL;
            size_t cbDst = 0;
            RTCircBufAcquireWriteBlock(pCircBuf, cbChunk, &pvDst, &cbDst);

            if (cbDst)
            {
                int rc2 = PDMDevHlpPCIPhysRead(pDevIns, pStream->Regs.bd.addr, pvDst, cbDst);
                AssertRC(rc2);

                if (RT_LIKELY(!pStreamCC->Dbg.Runtime.pFileDMA))
                { /* likely */ }
                else
                    AudioHlpFileWrite(pStreamCC->Dbg.Runtime.pFileDMA, pvDst, cbDst);
            }

            RTCircBufReleaseWriteBlock(pCircBuf, cbDst);

            cbChunk = (uint32_t)cbDst; /* Update the current chunk size to what really has been written. */
        }
        /*
         * Input.
         */
        else if (!fWriteSilence)
        {
            void  *pvSrc = NULL;
            size_t cbSrc = 0;
            RTCircBufAcquireReadBlock(pCircBuf, cbChunk, &pvSrc, &cbSrc);

            if (cbSrc)
            {
                int rc2 = PDMDevHlpPCIPhysWrite(pDevIns, pStream->Regs.bd.addr, pvSrc, cbSrc);
                AssertRC(rc2);

                if (RT_LIKELY(!pStreamCC->Dbg.Runtime.pFileDMA))
                { /* likely */ }
                else
                    AudioHlpFileWrite(pStreamCC->Dbg.Runtime.pFileDMA, pvSrc, cbSrc);
            }

            RTCircBufReleaseReadBlock(pCircBuf, cbSrc);

            cbChunk = (uint32_t)cbSrc; /* Update the current chunk size to what really has been read. */
        }
        else
        {
            /* Since the format is signed 16-bit or 32-bit integer samples, we can
               use g_abRTZero64K as source and avoid some unnecessary bzero() work. */
            cbChunk = RT_MIN(cbChunk, sizeof(g_abRTZero64K));
            cbChunk = PDMAudioPropsFloorBytesToFrame(&pStreamCC->State.Cfg.Props, cbChunk);

            int rc2 = PDMDevHlpPCIPhysWrite(pDevIns, pStream->Regs.bd.addr, g_abRTZero64K, cbChunk);
            AssertRC(rc2);
        }

        Assert(PDMAudioPropsIsSizeAligned(&pStreamCC->State.Cfg.Props, cbChunk));
        Assert(cbChunk <= cbToProcess);

        /*
         * Advance.
         */
        pStream->Regs.picb    -= cbChunk / PDMAudioPropsSampleSize(&pStreamCC->State.Cfg.Props);
        pStream->Regs.bd.addr += cbChunk;
        cbToProcess           -= cbChunk;
#ifdef LOG_ENABLED
        cbProcessedTotal      += cbChunk;
#endif
        LogFlowFunc(("[SD%RU8] cbChunk=%#x, cbToProcess=%#x, cbTotal=%#x picb=%#x\n",
                     pStream->u8SD, cbChunk, cbToProcess, cbProcessedTotal, pStream->Regs.picb));
    }

    /*
     * Fetch a new buffer descriptor if we've exhausted the current one.
     */
    if (!pStream->Regs.picb)
    {
        uint32_t fNewSr = pStream->Regs.sr & ~AC97_SR_CELV;

        if (pStream->Regs.bd.ctl_len & AC97_BD_IOC)
            fNewSr |= AC97_SR_BCIS;

        if (pStream->Regs.civ != pStream->Regs.lvi)
            fNewSr |= ichac97R3StreamFetchNextBdle(pDevIns, pStream, pStreamCC);
        else
        {
            LogFunc(("Underrun CIV (%RU8) == LVI (%RU8)\n", pStream->Regs.civ, pStream->Regs.lvi));
            fNewSr |= AC97_SR_LVBCI | AC97_SR_DCH | AC97_SR_CELV;
            pThis->bup_flag = (pStream->Regs.bd.ctl_len & AC97_BD_BUP) ? BUP_LAST : 0;
            /** @todo r=bird: The bup_flag isn't cleared anywhere else.  We should probably
             *        do what the spec says, and keep writing zeros (silence).
             *        Alternatively, we could hope the guest will pause the DMA engine
             *        immediately after seeing this condition, in which case we should
             *        stop the DMA timer from being re-armed. */
        }

        ichac97StreamUpdateSR(pDevIns, pThis, pStream, fNewSr);
    }

    ichac97R3StreamUnlock(pStreamCC);
    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Input streams: Pulls data from the mixer, putting it in the internal DMA
 * buffer.
 *
 * @param   pStreamR3       The AC'97 stream (ring-3 bits).
 * @param   pSink           The mixer sink to pull from.
 */
static void ichac97R3StreamPullFromMixer(PAC97STREAMR3 pStreamR3, PAUDMIXSINK pSink)
{
# ifdef LOG_ENABLED
    uint64_t const offWriteOld = pStreamR3->State.offWrite;
# endif
    pStreamR3->State.offWrite = AudioMixerSinkTransferToCircBuf(pSink,
                                                                pStreamR3->State.pCircBuf,
                                                                pStreamR3->State.offWrite,
                                                                pStreamR3->u8SD,
                                                                pStreamR3->Dbg.Runtime.fEnabled
                                                                ? pStreamR3->Dbg.Runtime.pFileStream : NULL);

    Log3Func(("[SD%RU8] transferred=%#RX64 bytes -> @%#RX64\n", pStreamR3->u8SD,
              pStreamR3->State.offWrite - offWriteOld, pStreamR3->State.offWrite));

    /* Update buffer stats. */
    pStreamR3->State.StatDmaBufUsed = (uint32_t)RTCircBufUsed(pStreamR3->State.pCircBuf);
}


/**
 * Output streams: Pushes data to the mixer.
 *
 * @param   pStreamR3       The AC'97 stream (ring-3 bits).
 * @param   pSink           The mixer sink to push to.
 */
static void ichac97R3StreamPushToMixer(PAC97STREAMR3 pStreamR3, PAUDMIXSINK pSink)
{
# ifdef LOG_ENABLED
    uint64_t const offReadOld = pStreamR3->State.offRead;
# endif
    pStreamR3->State.offRead = AudioMixerSinkTransferFromCircBuf(pSink,
                                                                 pStreamR3->State.pCircBuf,
                                                                 pStreamR3->State.offRead,
                                                                 pStreamR3->u8SD,
                                                                 pStreamR3->Dbg.Runtime.fEnabled
                                                                 ? pStreamR3->Dbg.Runtime.pFileStream : NULL);

    Log3Func(("[SD%RU8] transferred=%#RX64 bytes -> @%#RX64\n", pStreamR3->u8SD,
              pStreamR3->State.offRead - offReadOld, pStreamR3->State.offRead));

    /* Update buffer stats. */
    pStreamR3->State.StatDmaBufUsed = (uint32_t)RTCircBufUsed(pStreamR3->State.pCircBuf);
}


/**
 * Updates an AC'97 stream by doing its DMA transfers.
 *
 * The host sink(s) set the overall pace (bird: no it doesn't, the DMA timer
 * does - we just hope like heck it matches the speed at which the *backend*
 * host audio driver processes samples).
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared AC'97 state.
 * @param   pThisCC             The ring-3 AC'97 state.
 * @param   pStream             The AC'97 stream to update (shared).
 * @param   pStreamCC           The AC'97 stream to update (ring-3).
 * @param   pSink               The sink being updated.
 */
static void ichac97R3StreamUpdateDma(PPDMDEVINS pDevIns, PAC97STATE pThis, PAC97STATER3 pThisCC,
                                     PAC97STREAM pStream, PAC97STREAMR3 pStreamCC, PAUDMIXSINK pSink)
{
    RT_NOREF(pThisCC);
    int rc2;

    /* The amount we're supposed to be transfering in this DMA period. */
    uint32_t cbPeriod = pStream->cbDmaPeriod;

    /*
     * Output streams (SDO).
     */
    if (pStreamCC->State.Cfg.enmDir == PDMAUDIODIR_OUT)
    {
        /*
         * Check how much room we have in our DMA buffer.  There should be at
         * least one period worth of space there or we're in an overflow situation.
         */
        uint32_t cbStreamFree = ichac97R3StreamGetFree(pStreamCC);
        if (cbStreamFree >= cbPeriod)
        { /* likely */ }
        else
        {
            STAM_REL_COUNTER_INC(&pStreamCC->State.StatDmaFlowProblems);
            LogFunc(("Warning! Stream #%u has insufficient space free: %u bytes, need %u.  Will try move data out of the buffer...\n",
                 pStreamCC->u8SD, cbStreamFree, cbPeriod));
            int rc = AudioMixerSinkTryLock(pSink);
            if (RT_SUCCESS(rc))
            {
                ichac97R3StreamPushToMixer(pStreamCC, pSink);
                AudioMixerSinkUpdate(pSink, 0, 0);
                AudioMixerSinkUnlock(pSink);
            }
            else
                RTThreadYield();
            LogFunc(("Gained %u bytes.\n", ichac97R3StreamGetFree(pStreamCC) - cbStreamFree));

            cbStreamFree = ichac97R3StreamGetFree(pStreamCC);
            if (cbStreamFree < cbPeriod)
            {
                /* Unable to make sufficient space.  Drop the whole buffer content.
                 * This is needed in order to keep the device emulation running at a constant rate,
                 * at the cost of losing valid (but too much) data. */
                STAM_REL_COUNTER_INC(&pStreamCC->State.StatDmaFlowErrors);
                LogRel2(("AC97: Warning: Hit stream #%RU8 overflow, dropping %u bytes of audio data\n",
                         pStreamCC->u8SD, ichac97R3StreamGetUsed(pStreamCC)));
# ifdef AC97_STRICT
                AssertMsgFailed(("Hit stream #%RU8 overflow -- timing bug?\n", pStreamCC->u8SD));
# endif
                RTCircBufReset(pStreamCC->State.pCircBuf);
                pStreamCC->State.offWrite = 0;
                pStreamCC->State.offRead  = 0;
                cbStreamFree = ichac97R3StreamGetFree(pStreamCC);
                Assert(cbStreamFree >= cbPeriod);
            }
        }

        /*
         * Do the DMA transfer.
         */
        Log3Func(("[SD%RU8] PICB=%#x samples / %RU64 ms, cbFree=%#x / %RU64 ms, cbTransferChunk=%#x / %RU64 ms\n", pStream->u8SD,
                  pStream->Regs.picb, PDMAudioPropsBytesToMilli(&pStreamCC->State.Cfg.Props,
                                                                  PDMAudioPropsSampleSize(&pStreamCC->State.Cfg.Props)
                                                                * pStream->Regs.picb),
                  cbStreamFree, PDMAudioPropsBytesToMilli(&pStreamCC->State.Cfg.Props, cbStreamFree),
                  cbPeriod, PDMAudioPropsBytesToMilli(&pStreamCC->State.Cfg.Props, cbPeriod)));

        rc2 = ichac97R3StreamTransfer(pDevIns, pThis, pStream, pStreamCC, RT_MIN(cbStreamFree, cbPeriod),
                                      false /*fWriteSilence*/, false /*fInput*/);
        AssertRC(rc2);

        pStreamCC->State.tsLastUpdateNs = RTTimeNanoTS();


        /*
         * Notify the AIO thread.
         */
        rc2 = AudioMixerSinkSignalUpdateJob(pSink);
        AssertRC(rc2);
    }
    /*
     * Input stream (SDI).
     */
    else
    {
        /*
         * See how much data we've got buffered...
         */
        bool     fWriteSilence = false;
        uint32_t cbStreamUsed  = ichac97R3StreamGetUsed(pStreamCC);
        if (pStreamCC->State.fInputPreBuffered && cbStreamUsed >= cbPeriod)
        { /*likely*/ }
        /*
         * Because it may take a while for the input stream to get going (at least
         * with pulseaudio), we feed the guest silence till we've pre-buffer a
         * couple of timer Hz periods.  (This avoid lots of bogus buffer underruns
         * when starting an input stream and hogging the timer EMT.)
         */
        else if (!pStreamCC->State.fInputPreBuffered)
        {
            uint32_t const cbPreBuffer = PDMAudioPropsNanoToBytes(&pStreamCC->State.Cfg.Props,
                                                                  RT_NS_1SEC / pStreamCC->State.uTimerHz);
            if (cbStreamUsed < cbPreBuffer)
            {
                Log3Func(("Pre-buffering (got %#x out of %#x bytes)...\n", cbStreamUsed, cbPreBuffer));
                fWriteSilence = true;
                cbStreamUsed  = cbPeriod;
            }
            else
            {
                Log3Func(("Completed pre-buffering (got %#x, needed %#x bytes).\n", cbStreamUsed, cbPreBuffer));
                pStreamCC->State.fInputPreBuffered = true;
                fWriteSilence = ichac97R3StreamGetFree(pStreamCC) >= cbPreBuffer + cbPreBuffer / 2;
                if (fWriteSilence)
                    cbStreamUsed = cbPeriod;
            }
        }
        /*
         * When we're low on data, we must really try fetch some ourselves
         * as buffer underruns must not happen.
         */
        else
        {
            STAM_REL_COUNTER_INC(&pStreamCC->State.StatDmaFlowProblems);
            LogFunc(("Warning! Stream #%u has insufficient data available: %u bytes, need %u.  Will try move pull more data into the buffer...\n",
                 pStreamCC->u8SD, cbStreamUsed, cbPeriod));
            int rc = AudioMixerSinkTryLock(pSink);
            if (RT_SUCCESS(rc))
            {
                AudioMixerSinkUpdate(pSink, cbStreamUsed, cbPeriod);
                ichac97R3StreamPullFromMixer(pStreamCC, pSink);
                AudioMixerSinkUnlock(pSink);
            }
            else
                RTThreadYield();
            LogFunc(("Gained %u bytes.\n", ichac97R3StreamGetUsed(pStreamCC) - cbStreamUsed));
            cbStreamUsed = ichac97R3StreamGetUsed(pStreamCC);
            if (cbStreamUsed < cbPeriod)
            {
                /* Unable to find sufficient input data by simple prodding.
                   In order to keep a constant byte stream following thru the DMA
                   engine into the guest, we will try again and then fall back on
                   filling the gap with silence. */
                uint32_t cbSilence = 0;
                do
                {
                    AudioMixerSinkLock(pSink);

                    cbStreamUsed = ichac97R3StreamGetUsed(pStreamCC);
                    if (cbStreamUsed < cbPeriod)
                    {
                        ichac97R3StreamPullFromMixer(pStreamCC, pSink);
                        cbStreamUsed = ichac97R3StreamGetUsed(pStreamCC);
                        while (cbStreamUsed < cbPeriod)
                        {
                            void  *pvDstBuf;
                            size_t cbDstBuf;
                            RTCircBufAcquireWriteBlock(pStreamCC->State.pCircBuf, cbPeriod - cbStreamUsed,
                                                       &pvDstBuf, &cbDstBuf);
                            RT_BZERO(pvDstBuf, cbDstBuf);
                            RTCircBufReleaseWriteBlock(pStreamCC->State.pCircBuf, cbDstBuf);
                            cbSilence    += (uint32_t)cbDstBuf;
                            cbStreamUsed += (uint32_t)cbDstBuf;
                        }
                    }

                    AudioMixerSinkUnlock(pSink);
                } while (cbStreamUsed < cbPeriod);
                if (cbSilence > 0)
                {
                    STAM_REL_COUNTER_INC(&pStreamCC->State.StatDmaFlowErrors);
                    STAM_REL_COUNTER_ADD(&pStreamCC->State.StatDmaFlowErrorBytes, cbSilence);
                    LogRel2(("AC97: Warning: Stream #%RU8 underrun, added %u bytes of silence (%u us)\n", pStreamCC->u8SD,
                             cbSilence, PDMAudioPropsBytesToMicro(&pStreamCC->State.Cfg.Props, cbSilence)));
                }
            }
        }

        /*
         * Do the DMA'ing.
         */
        if (cbStreamUsed)
        {
            rc2 = ichac97R3StreamTransfer(pDevIns, pThis, pStream, pStreamCC, RT_MIN(cbPeriod, cbStreamUsed),
                                          fWriteSilence, true /*fInput*/);
            AssertRC(rc2);

            pStreamCC->State.tsLastUpdateNs = RTTimeNanoTS();
        }

        /*
         * We should always kick the AIO thread.
         */
        /** @todo This isn't entirely ideal.  If we get into an underrun situation,
         *        we ideally want the AIO thread to run right before the DMA timer
         *        rather than right after it ran. */
        Log5Func(("Notifying AIO thread\n"));
        rc2 = AudioMixerSinkSignalUpdateJob(pSink);
        AssertRC(rc2);
    }
}


/**
 * @callback_method_impl{FNAUDMIXSINKUPDATE}
 *
 * For output streams this moves data from the internal DMA buffer (in which
 * ichac97R3StreamUpdateDma put it), thru the mixer and to the various backend
 * audio devices.
 *
 * For input streams this pulls data from the backend audio device(s), thru the
 * mixer and puts it in the internal DMA buffer ready for
 * ichac97R3StreamUpdateDma to pump into guest memory.
 */
static DECLCALLBACK(void) ichac97R3StreamUpdateAsyncIoJob(PPDMDEVINS pDevIns, PAUDMIXSINK pSink, void *pvUser)
{
    PAC97STATER3 const  pThisCC   = PDMDEVINS_2_DATA_CC(pDevIns, PAC97STATER3);
    PAC97STREAMR3 const pStreamCC = (PAC97STREAMR3)pvUser;
    Assert(pStreamCC->u8SD == (uintptr_t)(pStreamCC - &pThisCC->aStreams[0]));
    Assert(pSink == ichac97R3IndexToSink(pThisCC, pStreamCC->u8SD));
    RT_NOREF(pThisCC);

    /*
     * Output (SDO).
     */
    if (pStreamCC->State.Cfg.enmDir == PDMAUDIODIR_OUT)
        ichac97R3StreamPushToMixer(pStreamCC, pSink);
    /*
     * Input (SDI).
     */
    else
        ichac97R3StreamPullFromMixer(pStreamCC, pSink);
}


/**
 * Updates the next transfer based on a specific amount of bytes.
 *
 * @param   pDevIns             The device instance.
 * @param   pStream             The AC'97 stream to update (shared).
 * @param   pStreamCC           The AC'97 stream to update (ring-3).
 */
static void ichac97R3StreamTransferUpdate(PPDMDEVINS pDevIns, PAC97STREAM pStream, PAC97STREAMR3 pStreamCC)
{
    /*
     * Get the number of bytes left in the current buffer.
     *
     * This isn't entirely optimal iff the current entry doesn't have IOC set, in
     * that case we should use the number of bytes to the next IOC.  Unfortuantely,
     * it seems the spec doesn't allow us to prefetch more than one BDLE, so we
     * probably cannot look ahead without violating that restriction.  This is
     * probably a purely theoretical problem at this point.
     */
    uint32_t const cbLeftInBdle = pStream->Regs.picb * PDMAudioPropsSampleSize(&pStreamCC->State.Cfg.Props);
    if (cbLeftInBdle > 0) /** @todo r=bird: see todo about this in ichac97R3StreamFetchBDLE. */
    {
        /*
         * Since the buffer can be up to 0xfffe samples long (frame aligning stereo
         * prevents 0xffff), which translates to 743ms at a 44.1kHz rate, we must
         * also take the nominal timer frequency into account here so we keep
         * moving data at a steady rate.  (In theory, I think the guest can even
         * set up just one buffer and anticipate where we are in the buffer
         * processing when it writes/reads from it.  Linux seems to be doing such
         * configs when not playing or something.)
         */
        uint32_t const cbMaxPerHz = PDMAudioPropsNanoToBytes(&pStreamCC->State.Cfg.Props, RT_NS_1SEC / pStreamCC->State.uTimerHz);

        if (cbLeftInBdle <= cbMaxPerHz)
            pStream->cbDmaPeriod = cbLeftInBdle;
        /* Try avoid leaving a very short period at the end of a buffer. */
        else if (cbLeftInBdle >= cbMaxPerHz + cbMaxPerHz / 2)
            pStream->cbDmaPeriod = cbMaxPerHz;
        else
            pStream->cbDmaPeriod = PDMAudioPropsFloorBytesToFrame(&pStreamCC->State.Cfg.Props, cbLeftInBdle / 2);

        /*
         * Translate the chunk size to timer ticks.
         */
        uint64_t const cNsXferChunk = PDMAudioPropsBytesToNano(&pStreamCC->State.Cfg.Props, pStream->cbDmaPeriod);
        pStream->cDmaPeriodTicks    = PDMDevHlpTimerFromNano(pDevIns, pStream->hTimer, cNsXferChunk);
        Assert(pStream->cDmaPeriodTicks > 0);

        Log3Func(("[SD%RU8] cbLeftInBdle=%#RX32 cbMaxPerHz=%#RX32 (%RU16Hz) -> cbDmaPeriod=%#RX32 cDmaPeriodTicks=%RX64\n",
                  pStream->u8SD, cbLeftInBdle, cbMaxPerHz, pStreamCC->State.uTimerHz, pStream->cbDmaPeriod, pStream->cDmaPeriodTicks));
    }
}


/**
 * Sets the virtual device timer to a new expiration time.
 *
 * @param   pDevIns             The device instance.
 * @param   pStream             AC'97 stream to set timer for.
 * @param   cTicksToDeadline    The number of ticks to the new deadline.
 *
 * @remarks This used to be more complicated a long time ago...
 */
DECLINLINE(void) ichac97R3TimerSet(PPDMDEVINS pDevIns, PAC97STREAM pStream, uint64_t cTicksToDeadline)
{
    int rc = PDMDevHlpTimerSetRelative(pDevIns, pStream->hTimer, cTicksToDeadline, &pStream->uArmedTs);
    AssertRC(rc);
}


/**
 * @callback_method_impl{FNTMTIMERDEV,
 * Timer callback which handles the audio data transfers on a periodic basis.}
 */
static DECLCALLBACK(void) ichac97R3Timer(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PAC97STATE      pThis     = PDMDEVINS_2_DATA(pDevIns, PAC97STATE);
    STAM_PROFILE_START(&pThis->StatTimer, a);
    PAC97STATER3    pThisCC   = PDMDEVINS_2_DATA_CC(pDevIns, PAC97STATER3);
    PAC97STREAM     pStream   = (PAC97STREAM)pvUser;
    PAC97STREAMR3   pStreamCC = &RT_SAFE_SUBSCRIPT8(pThisCC->aStreams, pStream->u8SD);
    Assert(hTimer == pStream->hTimer); RT_NOREF(hTimer);

    Assert(pStream - &pThis->aStreams[0] == pStream->u8SD);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
    Assert(PDMDevHlpTimerIsLockOwner(pDevIns, pStream->hTimer));

    PAUDMIXSINK pSink = ichac97R3IndexToSink(pThisCC, pStream->u8SD);
    if (pSink && AudioMixerSinkIsActive(pSink))
    {
        ichac97R3StreamUpdateDma(pDevIns, pThis, pThisCC, pStream, pStreamCC, pSink);

        pStream->uDmaPeriod++;
        ichac97R3StreamTransferUpdate(pDevIns, pStream, pStreamCC);
        ichac97R3TimerSet(pDevIns, pStream, pStream->cDmaPeriodTicks);
    }

    STAM_PROFILE_STOP(&pThis->StatTimer, a);
}

#endif /* IN_RING3 */


/*********************************************************************************************************************************
*   AC'97 Stream Management                                                                                                      *
*********************************************************************************************************************************/
#ifdef IN_RING3

/**
 * Locks an AC'97 stream for serialized access.
 *
 * @param   pStreamCC           The AC'97 stream to lock (ring-3).
 */
DECLINLINE(void) ichac97R3StreamLock(PAC97STREAMR3 pStreamCC)
{
    int rc2 = RTCritSectEnter(&pStreamCC->State.CritSect);
    AssertRC(rc2);
}

/**
 * Unlocks a formerly locked AC'97 stream.
 *
 * @param   pStreamCC           The AC'97 stream to unlock (ring-3).
 */
DECLINLINE(void) ichac97R3StreamUnlock(PAC97STREAMR3 pStreamCC)
{
    int rc2 = RTCritSectLeave(&pStreamCC->State.CritSect);
    AssertRC(rc2);
}

#endif /* IN_RING3 */

/**
 * Updates the status register (SR) of an AC'97 audio stream.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared AC'97 state.
 * @param   pStream             AC'97 stream to update SR for.
 * @param   new_sr              New value for status register (SR).
 */
static void ichac97StreamUpdateSR(PPDMDEVINS pDevIns, PAC97STATE pThis, PAC97STREAM pStream, uint32_t new_sr)
{
    bool fSignal = false;
    int  iIRQL = 0;

    uint32_t new_mask = new_sr & AC97_SR_INT_MASK;
    uint32_t old_mask = pStream->Regs.sr & AC97_SR_INT_MASK;

    if (new_mask ^ old_mask)
    {
        /** @todo Is IRQ deasserted when only one of status bits is cleared? */
        if (!new_mask)
        {
            fSignal = true;
            iIRQL   = 0;
        }
        else if ((new_mask & AC97_SR_LVBCI) && (pStream->Regs.cr & AC97_CR_LVBIE))
        {
            fSignal = true;
            iIRQL   = 1;
        }
        else if ((new_mask & AC97_SR_BCIS) && (pStream->Regs.cr & AC97_CR_IOCE))
        {
            fSignal = true;
            iIRQL   = 1;
        }
    }

    pStream->Regs.sr = new_sr;

    LogFlowFunc(("IOC%d, LVB%d, sr=%#x, fSignal=%RTbool, IRQL=%d\n",
                 pStream->Regs.sr & AC97_SR_BCIS, pStream->Regs.sr & AC97_SR_LVBCI, pStream->Regs.sr, fSignal, iIRQL));

    if (fSignal)
    {
        static uint32_t const s_aMasks[] = { AC97_GS_PIINT, AC97_GS_POINT, AC97_GS_MINT };
        Assert(pStream->u8SD < AC97_MAX_STREAMS);
        if (iIRQL)
            pThis->glob_sta |=  s_aMasks[pStream->u8SD];
        else
            pThis->glob_sta &= ~s_aMasks[pStream->u8SD];

        LogFlowFunc(("Setting IRQ level=%d\n", iIRQL));
        PDMDevHlpPCISetIrq(pDevIns, 0, iIRQL);
    }
}

/**
 * Writes a new value to a stream's status register (SR).
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared AC'97 device state.
 * @param   pStream             Stream to update SR for.
 * @param   u32Val              New value to set the stream's SR to.
 */
static void ichac97StreamWriteSR(PPDMDEVINS pDevIns, PAC97STATE pThis, PAC97STREAM pStream, uint32_t u32Val)
{
    Log3Func(("[SD%RU8] SR <- %#x (sr %#x)\n", pStream->u8SD, u32Val, pStream->Regs.sr));

    pStream->Regs.sr |= u32Val & ~(AC97_SR_RO_MASK | AC97_SR_WCLEAR_MASK);
    ichac97StreamUpdateSR(pDevIns, pThis, pStream, pStream->Regs.sr & ~(u32Val & AC97_SR_WCLEAR_MASK));
}

#ifdef IN_RING3

/**
 * Resets an AC'97 stream.
 *
 * @param   pThis               The shared AC'97 state.
 * @param   pStream             The AC'97 stream to reset (shared).
 * @param   pStreamCC           The AC'97 stream to reset (ring-3).
 */
static void ichac97R3StreamReset(PAC97STATE pThis, PAC97STREAM pStream, PAC97STREAMR3 pStreamCC)
{
    ichac97R3StreamLock(pStreamCC);

    LogFunc(("[SD%RU8]\n", pStream->u8SD));

    /* Note: Do *not* reset the stream's circular buffer here, as the audio mixer still relies on
     *       previously announced DMA data (via AudioMixerSinkDrainAndStop()) and processes it asynchronously.
     *       Resetting the buffer here will cause a race condition.  See @bugref{10354}. */

    pStream->Regs.bdbar    = 0;
    pStream->Regs.civ      = 0;
    pStream->Regs.lvi      = 0;

    pStream->Regs.picb     = 0;
    pStream->Regs.piv      = 0; /* Note! Because this is also zero, we will actually start transferring with BDLE00. */
    pStream->Regs.cr      &= AC97_CR_DONT_CLEAR_MASK;
    pStream->Regs.bd_valid = 0;

    RT_ZERO(pThis->silence);

    ichac97R3StreamUnlock(pStreamCC);
}

/**
 * Retrieves a specific driver stream of a AC'97 driver.
 *
 * @returns Pointer to driver stream if found, or NULL if not found.
 * @param   pDrv        Driver to retrieve driver stream for.
 * @param   enmDir      Stream direction to retrieve.
 * @param   enmPath     Stream destination / source to retrieve.
 */
static PAC97DRIVERSTREAM ichac97R3MixerGetDrvStream(PAC97DRIVER pDrv, PDMAUDIODIR enmDir, PDMAUDIOPATH enmPath)
{
    if (enmDir == PDMAUDIODIR_IN)
    {
        LogFunc(("enmRecSource=%d\n", enmPath));
        switch (enmPath)
        {
            case PDMAUDIOPATH_IN_LINE:
                return &pDrv->LineIn;
            case PDMAUDIOPATH_IN_MIC:
                return &pDrv->MicIn;
            default:
                AssertFailedBreak();
        }
    }
    else if (enmDir == PDMAUDIODIR_OUT)
    {
        LogFunc(("enmPlaybackDst=%d\n", enmPath));
        switch (enmPath)
        {
            case PDMAUDIOPATH_OUT_FRONT:
                return &pDrv->Out;
            default:
                AssertFailedBreak();
        }
    }
    else
        AssertFailed();

    return NULL;
}

/**
 * Adds a driver stream to a specific mixer sink.
 *
 * Called by ichac97R3MixerAddDrvStreams() and ichac97R3MixerAddDrv().
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pMixSink    Mixer sink to add driver stream to.
 * @param   pCfg        Stream configuration to use.
 * @param   pDrv        Driver stream to add.
 */
static int ichac97R3MixerAddDrvStream(PPDMDEVINS pDevIns, PAUDMIXSINK pMixSink, PCPDMAUDIOSTREAMCFG pCfg, PAC97DRIVER pDrv)
{
    AssertPtrReturn(pMixSink, VERR_INVALID_POINTER);
    LogFunc(("[LUN#%RU8] %s\n", pDrv->uLUN, pCfg->szName));

    int rc;
    PAC97DRIVERSTREAM pDrvStream = ichac97R3MixerGetDrvStream(pDrv, pCfg->enmDir, pCfg->enmPath);
    if (pDrvStream)
    {
        AssertMsg(pDrvStream->pMixStrm == NULL, ("[LUN#%RU8] Driver stream already present when it must not\n", pDrv->uLUN));

        PAUDMIXSTREAM pMixStrm;
        rc = AudioMixerSinkCreateStream(pMixSink, pDrv->pConnector, pCfg, pDevIns, &pMixStrm);
        LogFlowFunc(("LUN#%RU8: Created stream \"%s\" for sink, rc=%Rrc\n", pDrv->uLUN, pCfg->szName, rc));
        if (RT_SUCCESS(rc))
        {
            rc = AudioMixerSinkAddStream(pMixSink, pMixStrm);
            LogFlowFunc(("LUN#%RU8: Added stream \"%s\" to sink, rc=%Rrc\n", pDrv->uLUN, pCfg->szName, rc));
            if (RT_SUCCESS(rc))
                pDrvStream->pMixStrm = pMixStrm;
            else
                AudioMixerStreamDestroy(pMixStrm, pDevIns, true /*fImmediate*/);
        }
    }
    else
        rc = VERR_INVALID_PARAMETER;

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Adds all current driver streams to a specific mixer sink.
 *
 * Called by ichac97R3StreamSetUp().
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThisCC     The ring-3 AC'97 state.
 * @param   pMixSink    Mixer sink to add stream to.
 * @param   pCfg        Stream configuration to use.
 */
static int ichac97R3MixerAddDrvStreams(PPDMDEVINS pDevIns, PAC97STATER3 pThisCC, PAUDMIXSINK pMixSink, PCPDMAUDIOSTREAMCFG pCfg)
{
    AssertPtrReturn(pMixSink, VERR_INVALID_POINTER);

    int rc;
    if (AudioHlpStreamCfgIsValid(pCfg))
    {
        rc = AudioMixerSinkSetFormat(pMixSink, &pCfg->Props, pCfg->Device.cMsSchedulingHint);
        if (RT_SUCCESS(rc))
        {
            PAC97DRIVER pDrv;
            RTListForEach(&pThisCC->lstDrv, pDrv, AC97DRIVER, Node)
            {
                int rc2 = ichac97R3MixerAddDrvStream(pDevIns, pMixSink, pCfg, pDrv);
                if (RT_FAILURE(rc2))
                    LogFunc(("Attaching stream failed with %Rrc\n", rc2));

                /* Do not pass failure to rc here, as there might be drivers which aren't
                   configured / ready yet. */
            }
        }
    }
    else
        rc = VERR_INVALID_PARAMETER;

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Removes a driver stream from a specific mixer sink.
 *
 * Worker for ichac97R3MixerRemoveDrvStreams.
 *
 * @param   pDevIns     The device instance.
 * @param   pMixSink    Mixer sink to remove audio streams from.
 * @param   enmDir      Stream direction to remove.
 * @param   enmPath     Stream destination / source to remove.
 * @param   pDrv        Driver stream to remove.
 */
static void ichac97R3MixerRemoveDrvStream(PPDMDEVINS pDevIns, PAUDMIXSINK pMixSink, PDMAUDIODIR enmDir,
                                          PDMAUDIOPATH enmPath, PAC97DRIVER pDrv)
{
    PAC97DRIVERSTREAM pDrvStream = ichac97R3MixerGetDrvStream(pDrv, enmDir, enmPath);
    if (pDrvStream)
    {
        if (pDrvStream->pMixStrm)
        {
            AudioMixerSinkRemoveStream(pMixSink, pDrvStream->pMixStrm);

            AudioMixerStreamDestroy(pDrvStream->pMixStrm, pDevIns, false /*fImmediate*/);
            pDrvStream->pMixStrm = NULL;
        }
    }
}

/**
 * Removes all driver streams from a specific mixer sink.
 *
 * Called by ichac97R3StreamSetUp() and ichac97R3StreamsDestroy().
 *
 * @param   pDevIns     The device instance.
 * @param   pThisCC     The ring-3 AC'97 state.
 * @param   pMixSink    Mixer sink to remove audio streams from.
 * @param   enmDir      Stream direction to remove.
 * @param   enmPath     Stream destination / source to remove.
 */
static void ichac97R3MixerRemoveDrvStreams(PPDMDEVINS pDevIns, PAC97STATER3 pThisCC, PAUDMIXSINK pMixSink,
                                           PDMAUDIODIR enmDir, PDMAUDIOPATH enmPath)
{
    AssertPtrReturnVoid(pMixSink);

    PAC97DRIVER pDrv;
    RTListForEach(&pThisCC->lstDrv, pDrv, AC97DRIVER, Node)
    {
        ichac97R3MixerRemoveDrvStream(pDevIns, pMixSink, enmDir, enmPath, pDrv);
    }
}


/**
 * Gets the frequency of a given stream.
 *
 * @returns The frequency. Zero if invalid stream index.
 * @param   pThis       The shared AC'97 device state.
 * @param   idxStream   The stream.
 */
DECLINLINE(uint32_t) ichach97R3CalcStreamHz(PAC97STATE pThis, uint8_t idxStream)
{
    switch (idxStream)
    {
        case AC97SOUNDSOURCE_PI_INDEX:
            return ichac97MixerGet(pThis, AC97_PCM_LR_ADC_Rate);

        case AC97SOUNDSOURCE_MC_INDEX:
            return ichac97MixerGet(pThis, AC97_MIC_ADC_Rate);

        case AC97SOUNDSOURCE_PO_INDEX:
            return ichac97MixerGet(pThis, AC97_PCM_Front_DAC_Rate);

        default:
            AssertMsgFailedReturn(("%d\n", idxStream), 0);
    }
}


/**
 * Gets the PCM properties for a given stream.
 *
 * @returns pProps.
 * @param   pThis       The shared AC'97 device state.
 * @param   idxStream   Which stream
 * @param   pProps      Where to return the stream properties.
 */
DECLINLINE(PPDMAUDIOPCMPROPS) ichach97R3CalcStreamProps(PAC97STATE pThis, uint8_t idxStream, PPDMAUDIOPCMPROPS pProps)
{
    PDMAudioPropsInit(pProps, 2 /*16-bit*/, true /*signed*/, 2 /*stereo*/, ichach97R3CalcStreamHz(pThis, idxStream));
    return pProps;
}


/**
 * Sets up an AC'97 stream with its current mixer settings.
 *
 * This will set up an AC'97 stream with 2 (stereo) channels, 16-bit samples and
 * the last set sample rate in the AC'97 mixer for this stream.
 *
 * @returns VBox status code.
 * @retval  VINF_NO_CHANGE if the streams weren't re-created.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The shared AC'97 device state (shared).
 * @param   pThisCC     The shared AC'97 device state (ring-3).
 * @param   pStream     The AC'97 stream to open (shared).
 * @param   pStreamCC   The AC'97 stream to open (ring-3).
 * @param   fForce      Whether to force re-opening the stream or not.
 *                      Otherwise re-opening only will happen if the PCM properties have changed.
 *
 * @remarks This is called holding:
 *              -# The AC'97 device lock.
 *              -# The AC'97 stream lock.
 *              -# The mixer sink lock (to prevent racing AIO thread).
 */
static int ichac97R3StreamSetUp(PPDMDEVINS pDevIns, PAC97STATE pThis, PAC97STATER3 pThisCC, PAC97STREAM pStream,
                                PAC97STREAMR3 pStreamCC, bool fForce)
{
    /*
     * Assemble the stream config and get the associated mixer sink.
     */
    PDMAUDIOPCMPROPS    PropsTmp;
    PDMAUDIOSTREAMCFG   Cfg;
    PDMAudioStrmCfgInitWithProps(&Cfg, ichach97R3CalcStreamProps(pThis, pStream->u8SD, &PropsTmp));
    Assert(Cfg.enmDir != PDMAUDIODIR_UNKNOWN);

    PAUDMIXSINK         pMixSink;
    switch (pStream->u8SD)
    {
        case AC97SOUNDSOURCE_PI_INDEX:
            Cfg.enmDir      = PDMAUDIODIR_IN;
            Cfg.enmPath     = PDMAUDIOPATH_IN_LINE;
            RTStrCopy(Cfg.szName, sizeof(Cfg.szName), "Line-In");

            pMixSink        = pThisCC->pSinkLineIn;
            break;

        case AC97SOUNDSOURCE_MC_INDEX:
            Cfg.enmDir      = PDMAUDIODIR_IN;
            Cfg.enmPath     = PDMAUDIOPATH_IN_MIC;
            RTStrCopy(Cfg.szName, sizeof(Cfg.szName), "Mic-In");

            pMixSink        = pThisCC->pSinkMicIn;
            break;

        case AC97SOUNDSOURCE_PO_INDEX:
            Cfg.enmDir      = PDMAUDIODIR_OUT;
            Cfg.enmPath     = PDMAUDIOPATH_OUT_FRONT;
            RTStrCopy(Cfg.szName, sizeof(Cfg.szName), "Output");

            pMixSink        = pThisCC->pSinkOut;
            break;

        default:
            AssertMsgFailedReturn(("u8SD=%d\n", pStream->u8SD), VERR_INTERNAL_ERROR_3);
    }

    /* Validate locks -- see @bugref{10350}. */
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
    Assert(RTCritSectIsOwned(&pStreamCC->State.CritSect));
    Assert(AudioMixerSinkLockIsOwner(pMixSink));

    /*
     * Don't continue if the frequency is out of range (the rest of the
     * properties should be okay).
     * Note! Don't assert on this as we may easily end up here with Hz=0.
     */
    char szTmp[PDMAUDIOSTRMCFGTOSTRING_MAX];
    if (AudioHlpStreamCfgIsValid(&Cfg))
    { }
    else
    {
        LogFunc(("Invalid stream #%u rate: %s\n", pStreamCC->u8SD, PDMAudioStrmCfgToString(&Cfg, szTmp, sizeof(szTmp)) ));
        return VERR_OUT_OF_RANGE;
    }

    /*
     * Read the buffer descriptors and check what the max distance between
     * interrupts are, so we can more correctly size the internal DMA buffer.
     *
     * Note! The buffer list are not fixed once the stream starts running as
     *       with HDA, so this is just a general idea of what the guest is
     *       up to and we cannot really make much of a plan out of it.
     */
    uint8_t const  bLvi     = pStream->Regs.lvi % AC97_MAX_BDLE /* paranoia */;
    uint8_t const  bCiv     = pStream->Regs.civ % AC97_MAX_BDLE /* paranoia */;
    uint32_t const uAddrBdl = pStream->Regs.bdbar;

    /* Linux does this a number of times while probing/whatever the device. The
       IOMMU usually does allow us to read address zero, so let's skip and hope
       for a better config before the guest actually wants to play/record.
       (Note that bLvi and bCiv are also zero then, but I'm not entirely sure if
       that can be taken to mean anything as such, as it still indicates that
       BDLE00 is valid (LVI == last valid index).) */
    /** @todo Instead of refusing to read address zero, we should probably allow
     * reading address zero if explicitly programmed.  But, too much work now. */
    if (uAddrBdl != 0)
        LogFlowFunc(("bdbar=%#x bLvi=%#x bCiv=%#x\n", uAddrBdl, bLvi, bCiv));
    else
    {
        LogFunc(("Invalid stream #%u: bdbar=%#x bLvi=%#x bCiv=%#x (%s)\n", pStreamCC->u8SD, uAddrBdl, bLvi, bCiv,
                 PDMAudioStrmCfgToString(&Cfg, szTmp, sizeof(szTmp))));
        return VERR_OUT_OF_RANGE;
    }

    AC97BDLE aBdl[AC97_MAX_BDLE];
    RT_ZERO(aBdl);
    PDMDevHlpPCIPhysRead(pDevIns, uAddrBdl, aBdl, sizeof(aBdl));

    uint32_t      cSamplesMax   = 0;
    uint32_t      cSamplesMin   = UINT32_MAX;
    uint32_t      cSamplesCur   = 0;
    uint32_t      cSamplesTotal = 0;
    uint32_t      cBuffers      = 1;
    for (uintptr_t i = bCiv; ; cBuffers++)
    {
        Log2Func(("BDLE%02u: %#x LB %#x; %#x\n", i, aBdl[i].addr, aBdl[i].ctl_len & AC97_BD_LEN_MASK, aBdl[i].ctl_len >> 16));
        cSamplesTotal += aBdl[i].ctl_len & AC97_BD_LEN_MASK;
        cSamplesCur   += aBdl[i].ctl_len & AC97_BD_LEN_MASK;
        if (aBdl[i].ctl_len & AC97_BD_IOC)
        {
            if (cSamplesCur > cSamplesMax)
                cSamplesMax = cSamplesCur;
            if (cSamplesCur < cSamplesMin)
                cSamplesMin = cSamplesCur;
            cSamplesCur = 0;
        }

        /* Advance. */
        if (i != bLvi)
            i = (i + 1) % RT_ELEMENTS(aBdl);
        else
            break;
    }
    if (!cSamplesCur)
    { /* likely */ }
    else if (!cSamplesMax)
    {
        LogFlowFunc(("%u buffers without IOC set, assuming %#x samples as the IOC period.\n", cBuffers, cSamplesMax));
        cSamplesMin = cSamplesMax = cSamplesCur;
    }
    else if (cSamplesCur > cSamplesMax)
    {
        LogFlowFunc(("final buffer is without IOC, using open period as max (%#x vs current max %#x).\n", cSamplesCur, cSamplesMax));
        cSamplesMax = cSamplesCur;
    }
    else
        LogFlowFunc(("final buffer is without IOC, ignoring (%#x vs current max %#x).\n", cSamplesCur, cSamplesMax));

    uint32_t const cbDmaMinBuf  = cSamplesMax * PDMAudioPropsSampleSize(&Cfg.Props) * 3; /* see further down */
    uint32_t const cMsDmaMinBuf = PDMAudioPropsBytesToMilli(&Cfg.Props, cbDmaMinBuf);
    LogRel3(("AC97: [SD%RU8] buffer length stats: total=%#x in %u buffers, min=%#x, max=%#x => min DMA buffer %u ms / %#x bytes\n",
             pStream->u8SD, cSamplesTotal, cBuffers, cSamplesMin, cSamplesMax, cMsDmaMinBuf, cbDmaMinBuf));

    /*
     * Calculate the timer Hz / scheduling hint based on the stream frame rate.
     */
    uint32_t uTimerHz;
    if (pThis->uTimerHz == AC97_TIMER_HZ_DEFAULT) /* Make sure that we don't have any custom Hz rate set we want to enforce */
    {
        if (Cfg.Props.uHz > 44100) /* E.g. 48000 Hz. */
            uTimerHz = 200;
        else
            uTimerHz = AC97_TIMER_HZ_DEFAULT;
    }
    else
        uTimerHz = pThis->uTimerHz;

    if (   uTimerHz >= 10
        && uTimerHz <= 500)
    { /* likely */ }
    else
    {
        LogFunc(("[SD%RU8] Adjusting uTimerHz=%u to %u\n", pStream->u8SD, uTimerHz,
                 Cfg.Props.uHz > 44100 ? 200 : AC97_TIMER_HZ_DEFAULT));
        uTimerHz = Cfg.Props.uHz > 44100 ? 200 : AC97_TIMER_HZ_DEFAULT;
    }

    /* Translate it to a scheduling hint. */
    uint32_t const cMsSchedulingHint = RT_MS_1SEC / uTimerHz;

    /*
     * Calculate the circular buffer size so we can decide whether to recreate
     * the stream or not.
     *
     * As mentioned in the HDA code, this should be at least able to hold the
     * data transferred in three DMA periods and in three AIO period (whichever
     * is higher).  However, if we assume that the DMA code will engage the DMA
     * timer thread (currently EMT) if the AIO thread isn't getting schduled to
     * transfer data thru the stack, we don't need to go overboard and double
     * the minimums here.  The less buffer the less possible delay can build when
     * TM is doing catch up.
     */
    uint32_t cMsCircBuf = Cfg.enmDir == PDMAUDIODIR_IN ? pThis->cMsCircBufIn : pThis->cMsCircBufOut;
    cMsCircBuf = RT_MAX(cMsCircBuf, cMsDmaMinBuf);
    cMsCircBuf = RT_MAX(cMsCircBuf, cMsSchedulingHint * 3);
    cMsCircBuf = RT_MIN(cMsCircBuf, RT_MS_1SEC * 2);
    uint32_t const cbCircBuf = PDMAudioPropsMilliToBytes(&Cfg.Props, cMsCircBuf);

    LogFlowFunc(("Stream %u: uTimerHz: %u -> %u; cMsSchedulingHint: %u -> %u; cbCircBuf: %#zx -> %#x (%u ms, cMsDmaMinBuf=%u)%s\n",
                 pStreamCC->u8SD, pStreamCC->State.uTimerHz, uTimerHz,
                 pStreamCC->State.Cfg.Device.cMsSchedulingHint, cMsSchedulingHint,
                 pStreamCC->State.pCircBuf ? RTCircBufSize(pStreamCC->State.pCircBuf) : 0, cbCircBuf, cMsCircBuf, cMsDmaMinBuf,
                 !pStreamCC->State.pCircBuf || RTCircBufSize(pStreamCC->State.pCircBuf) != cbCircBuf  ? " - re-creating DMA buffer" : ""));

    /*
     * Update the stream's timer rate and scheduling hint, re-registering the AIO
     * update job if necessary.
     */
    if (   pStreamCC->State.Cfg.Device.cMsSchedulingHint != cMsSchedulingHint
        || !pStreamCC->State.fRegisteredAsyncUpdateJob)
    {
        if (pStreamCC->State.fRegisteredAsyncUpdateJob)
            AudioMixerSinkRemoveUpdateJob(pMixSink, ichac97R3StreamUpdateAsyncIoJob, pStreamCC);
        int rc2 = AudioMixerSinkAddUpdateJob(pMixSink, ichac97R3StreamUpdateAsyncIoJob, pStreamCC,
                                             pStreamCC->State.Cfg.Device.cMsSchedulingHint);
        AssertRC(rc2);
        pStreamCC->State.fRegisteredAsyncUpdateJob = RT_SUCCESS(rc2) || rc2 == VERR_ALREADY_EXISTS;
    }

    pStreamCC->State.uTimerHz    = uTimerHz;
    Cfg.Device.cMsSchedulingHint = cMsSchedulingHint;

    /*
     * Re-create the circular buffer if necessary, resetting if not.
     */
    if (   pStreamCC->State.pCircBuf
        && RTCircBufSize(pStreamCC->State.pCircBuf) == cbCircBuf)
    {
        /* Note: Do *not* reset the stream's circular buffer here, as the audio mixer still relies on
         *       previously announced DMA data (via AudioMixerSinkDrainAndStop()) and processes it asynchronously.
         *       Resetting the buffer here will cause a race condition.  See @bugref{10354}. */
    }
    else
    {
        if (pStreamCC->State.pCircBuf)
            RTCircBufDestroy(pStreamCC->State.pCircBuf);

        int rc = RTCircBufCreate(&pStreamCC->State.pCircBuf, cbCircBuf);
        AssertRCReturnStmt(rc, pStreamCC->State.pCircBuf = NULL, rc);

        pStreamCC->State.StatDmaBufSize = (uint32_t)RTCircBufSize(pStreamCC->State.pCircBuf);
    }
    Assert(pStreamCC->State.StatDmaBufSize == cbCircBuf);

    /*
     * Only (re-)create the stream (and driver chain) if we really have to.
     * Otherwise avoid this and just reuse it, as this costs performance.
     */
    int rc = VINF_SUCCESS;
    if (   fForce
        || !PDMAudioStrmCfgMatchesProps(&Cfg, &pStreamCC->State.Cfg.Props)
        || (pStreamCC->State.nsRetrySetup && RTTimeNanoTS() >= pStreamCC->State.nsRetrySetup))
    {
        LogRel2(("AC97: Setting up stream #%u: %s\n", pStreamCC->u8SD, PDMAudioStrmCfgToString(&Cfg, szTmp, sizeof(szTmp)) ));

        ichac97R3MixerRemoveDrvStreams(pDevIns, pThisCC, pMixSink, Cfg.enmDir, Cfg.enmPath);

        rc = ichac97R3MixerAddDrvStreams(pDevIns, pThisCC, pMixSink, &Cfg);
        if (RT_SUCCESS(rc))
        {
            PDMAudioStrmCfgCopy(&pStreamCC->State.Cfg, &Cfg);
            pStreamCC->State.nsRetrySetup = 0;
            LogFlowFunc(("[SD%RU8] success (uHz=%u)\n", pStreamCC->u8SD, PDMAudioPropsHz(&Cfg.Props)));
        }
        else
        {
            LogFunc(("[SD%RU8] ichac97R3MixerAddDrvStreams failed: %Rrc (uHz=%u)\n",
                     pStreamCC->u8SD, rc, PDMAudioPropsHz(&Cfg.Props)));
            pStreamCC->State.nsRetrySetup = RTTimeNanoTS() + 5*RT_NS_1SEC_64; /* retry in 5 seconds, unless config changes. */
        }
    }
    else
    {
        LogFlowFunc(("[SD%RU8] Skipping set-up (unchanged: %s)\n",
                     pStreamCC->u8SD, PDMAudioStrmCfgToString(&Cfg, szTmp, sizeof(szTmp))));
        rc = VINF_NO_CHANGE;
    }
    return rc;
}


/**
 * Tears down an AC'97 stream (counter part to ichac97R3StreamSetUp).
 *
 * Empty stub at present, nothing to do here as we reuse streams and only really
 * re-open them if parameters changed (seldom).
 *
 * @param   pStream             The AC'97 stream to close (shared).
 */
static void ichac97R3StreamTearDown(PAC97STREAM pStream)
{
    RT_NOREF(pStream);
    LogFlowFunc(("[SD%RU8]\n", pStream->u8SD));
}


/**
 * Tears down and sets up an AC'97 stream on the backend side with the current
 * AC'97 mixer settings for this stream.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The shared AC'97 device state.
 * @param   pThisCC     The ring-3 AC'97 device state.
 * @param   pStream     The AC'97 stream to re-open (shared).
 * @param   pStreamCC   The AC'97 stream to re-open (ring-3).
 * @param   fForce      Whether to force re-opening the stream or not.
 *                      Otherwise re-opening only will happen if the PCM properties have changed.
 *
 * @remarks This is called holding:
 *              -# The AC'97 device lock.
 *
 *          Will acquire the stream and mixer sink locks. See @bugref{10350}
 */
static int ichac97R3StreamReSetUp(PPDMDEVINS pDevIns, PAC97STATE pThis, PAC97STATER3 pThisCC,
                                  PAC97STREAM pStream, PAC97STREAMR3 pStreamCC, bool fForce)
{
    STAM_REL_PROFILE_START_NS(&pStreamCC->State.StatReSetUpChanged, r);
    LogFlowFunc(("[SD%RU8]\n", pStream->u8SD));
    Assert(pStream->u8SD == pStreamCC->u8SD);
    Assert(pStream   - &pThis->aStreams[0]   == pStream->u8SD);
    Assert(pStreamCC - &pThisCC->aStreams[0] == pStream->u8SD);

    ichac97R3StreamLock(pStreamCC);
    PAUDMIXSINK const pSink = ichac97R3IndexToSink(pThisCC, pStream->u8SD);
    if (pSink)
        AudioMixerSinkLock(pSink);

    ichac97R3StreamTearDown(pStream);
    int rc = ichac97R3StreamSetUp(pDevIns, pThis, pThisCC, pStream, pStreamCC, fForce);
    if (rc == VINF_NO_CHANGE)
        STAM_REL_PROFILE_STOP_NS(&pStreamCC->State.StatReSetUpSame, r);
    else
        STAM_REL_PROFILE_STOP_NS(&pStreamCC->State.StatReSetUpChanged, r);

    if (pSink)
        AudioMixerSinkUnlock(pSink);
    ichac97R3StreamUnlock(pStreamCC);

    return rc;
}


/**
 * Enables or disables an AC'97 audio stream.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The shared AC'97 state.
 * @param   pThisCC     The ring-3 AC'97 state.
 * @param   pStream     The AC'97 stream to enable or disable (shared state).
 * @param   pStreamCC   The ring-3 stream state (matching to @a pStream).
 * @param   fEnable     Whether to enable or disable the stream.
 *
 */
static int ichac97R3StreamEnable(PPDMDEVINS pDevIns, PAC97STATE pThis, PAC97STATER3 pThisCC,
                                 PAC97STREAM pStream, PAC97STREAMR3 pStreamCC, bool fEnable)
{
    ichac97R3StreamLock(pStreamCC);
    PAUDMIXSINK const pSink = ichac97R3IndexToSink(pThisCC, pStream->u8SD);
    if (pSink)
        AudioMixerSinkLock(pSink);

    int rc = VINF_SUCCESS;
    /*
     * Enable.
     */
    if (fEnable)
    {
        /* Reset the input pre-buffering state and DMA period counter. */
        pStreamCC->State.fInputPreBuffered = false;
        pStream->uDmaPeriod = 0;

        /* Set up (update) the AC'97 stream as needed. */
        rc = ichac97R3StreamSetUp(pDevIns, pThis, pThisCC, pStream, pStreamCC, false /* fForce */);
        if (RT_SUCCESS(rc))
        {
            /* Open debug files. */
            if (RT_LIKELY(!pStreamCC->Dbg.Runtime.fEnabled))
            { /* likely */ }
            else
            {
                if (!AudioHlpFileIsOpen(pStreamCC->Dbg.Runtime.pFileStream))
                    AudioHlpFileOpen(pStreamCC->Dbg.Runtime.pFileStream, AUDIOHLPFILE_DEFAULT_OPEN_FLAGS,
                                     &pStreamCC->State.Cfg.Props);
                if (!AudioHlpFileIsOpen(pStreamCC->Dbg.Runtime.pFileDMA))
                    AudioHlpFileOpen(pStreamCC->Dbg.Runtime.pFileDMA, AUDIOHLPFILE_DEFAULT_OPEN_FLAGS,
                                     &pStreamCC->State.Cfg.Props);
            }

            /* Do the actual enabling (won't fail as long as pSink is valid). */
            if (pSink)
                rc = AudioMixerSinkStart(pSink);
        }
    }
    /*
     * Disable
     */
    else
    {
        rc = AudioMixerSinkDrainAndStop(pSink, pStreamCC->State.pCircBuf ? (uint32_t)RTCircBufUsed(pStreamCC->State.pCircBuf) : 0);
        ichac97R3StreamTearDown(pStream);
    }

    /* Make sure to leave the lock before (eventually) starting the timer. */
    if (pSink)
        AudioMixerSinkUnlock(pSink);
    ichac97R3StreamUnlock(pStreamCC);
    LogFunc(("[SD%RU8] fEnable=%RTbool, rc=%Rrc\n", pStream->u8SD, fEnable, rc));
    return rc;
}


/**
 * Returns whether an AC'97 stream is enabled or not.
 *
 * Only used by ichac97R3SaveExec().
 *
 * @returns VBox status code.
 * @param   pThisCC             The ring-3 AC'97 device state.
 * @param   pStream             Stream to return status for.
 */
static bool ichac97R3StreamIsEnabled(PAC97STATER3 pThisCC, PAC97STREAM pStream)
{
    PAUDMIXSINK pSink = ichac97R3IndexToSink(pThisCC, pStream->u8SD);
    bool fIsEnabled = pSink && (AudioMixerSinkGetStatus(pSink) & AUDMIXSINK_STS_RUNNING);

    LogFunc(("[SD%RU8] fIsEnabled=%RTbool\n", pStream->u8SD, fIsEnabled));
    return fIsEnabled;
}


/**
 * Terminates an AC'97 audio stream (VM destroy).
 *
 * This is called by ichac97R3StreamsDestroy during VM poweroff & destruction.
 *
 * @param   pThisCC             The ring-3 AC'97 state.
 * @param   pStream             The AC'97 stream to destroy (shared).
 * @param   pStreamCC           The AC'97 stream to destroy (ring-3).
 * @sa      ichac97R3StreamConstruct
 */
static void ichac97R3StreamDestroy(PAC97STATER3 pThisCC, PAC97STREAM pStream, PAC97STREAMR3 pStreamCC)
{
    LogFlowFunc(("[SD%RU8]\n", pStream->u8SD));

    ichac97R3StreamTearDown(pStream);

    int rc2 = RTCritSectDelete(&pStreamCC->State.CritSect);
    AssertRC(rc2);

    if (pStreamCC->State.fRegisteredAsyncUpdateJob)
    {
        PAUDMIXSINK pSink = ichac97R3IndexToSink(pThisCC, pStream->u8SD);
        if (pSink)
            AudioMixerSinkRemoveUpdateJob(pSink, ichac97R3StreamUpdateAsyncIoJob, pStreamCC);
        pStreamCC->State.fRegisteredAsyncUpdateJob = false;
    }

    if (RT_LIKELY(!pStreamCC->Dbg.Runtime.fEnabled))
    { /* likely */ }
    else
    {
        AudioHlpFileDestroy(pStreamCC->Dbg.Runtime.pFileStream);
        pStreamCC->Dbg.Runtime.pFileStream = NULL;

        AudioHlpFileDestroy(pStreamCC->Dbg.Runtime.pFileDMA);
        pStreamCC->Dbg.Runtime.pFileDMA = NULL;
    }

    if (pStreamCC->State.pCircBuf)
    {
        RTCircBufDestroy(pStreamCC->State.pCircBuf);
        pStreamCC->State.pCircBuf = NULL;
    }

    LogFlowFuncLeave();
}


/**
 * Initializes an AC'97 audio stream (VM construct).
 *
 * This is only called by ichac97R3Construct.
 *
 * @returns VBox status code.
 * @param   pThisCC             The ring-3 AC'97 state.
 * @param   pStream             The AC'97 stream to create (shared).
 * @param   pStreamCC           The AC'97 stream to create (ring-3).
 * @param   u8SD                Stream descriptor number to assign.
 * @sa      ichac97R3StreamDestroy
 */
static int ichac97R3StreamConstruct(PAC97STATER3 pThisCC, PAC97STREAM pStream, PAC97STREAMR3 pStreamCC, uint8_t u8SD)
{
    LogFunc(("[SD%RU8] pStream=%p\n", u8SD, pStream));

    AssertReturn(u8SD < AC97_MAX_STREAMS, VERR_INVALID_PARAMETER);
    pStream->u8SD       = u8SD;
    pStreamCC->u8SD     = u8SD;

    int rc = RTCritSectInit(&pStreamCC->State.CritSect);
    AssertRCReturn(rc, rc);

    pStreamCC->Dbg.Runtime.fEnabled = pThisCC->Dbg.fEnabled;

    if (RT_LIKELY(!pStreamCC->Dbg.Runtime.fEnabled))
    { /* likely */ }
    else
    {
        int rc2 = AudioHlpFileCreateF(&pStreamCC->Dbg.Runtime.pFileStream, AUDIOHLPFILE_FLAGS_NONE, AUDIOHLPFILETYPE_WAV,
                                      pThisCC->Dbg.pszOutPath, AUDIOHLPFILENAME_FLAGS_NONE, 0 /*uInstance*/,
                                      ichac97R3GetDirFromSD(pStream->u8SD) == PDMAUDIODIR_IN
                                      ? "ac97StreamWriteSD%RU8" : "ac97StreamReadSD%RU8", pStream->u8SD);
        AssertRC(rc2);

        rc2 = AudioHlpFileCreateF(&pStreamCC->Dbg.Runtime.pFileDMA, AUDIOHLPFILE_FLAGS_NONE, AUDIOHLPFILETYPE_WAV,
                                  pThisCC->Dbg.pszOutPath, AUDIOHLPFILENAME_FLAGS_NONE, 0 /*uInstance*/,
                                  ichac97R3GetDirFromSD(pStream->u8SD) == PDMAUDIODIR_IN
                                  ? "ac97DMAWriteSD%RU8" : "ac97DMAReadSD%RU8", pStream->u8SD);
        AssertRC(rc2);

        /* Delete stale debugging files from a former run. */
        AudioHlpFileDelete(pStreamCC->Dbg.Runtime.pFileStream);
        AudioHlpFileDelete(pStreamCC->Dbg.Runtime.pFileDMA);
    }

    return rc;
}

#endif /* IN_RING3 */


/*********************************************************************************************************************************
*   NABM I/O Port Handlers (Global + Stream)                                                                                     *
*********************************************************************************************************************************/

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN}
 */
static DECLCALLBACK(VBOXSTRICTRC)
ichac97IoPortNabmRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PAC97STATE pThis = PDMDEVINS_2_DATA(pDevIns, PAC97STATE);
    RT_NOREF(pvUser);

    DEVAC97_LOCK_RETURN(pDevIns, pThis, VINF_IOM_R3_IOPORT_READ);

    /* Get the index of the NABMBAR port. */
    if (   AC97_PORT2IDX_UNMASKED(offPort) < AC97_MAX_STREAMS
        && offPort != AC97_GLOB_CNT)
    {
        PAC97STREAM pStream = &pThis->aStreams[AC97_PORT2IDX(offPort)];

        switch (cb)
        {
            case 1:
                switch (offPort & AC97_NABM_OFF_MASK)
                {
                    case AC97_NABM_OFF_CIV:
                        /* Current Index Value Register */
                        *pu32 = pStream->Regs.civ;
                        Log3Func(("CIV[%d] -> %#x\n", AC97_PORT2IDX(offPort), *pu32));
                        break;
                    case AC97_NABM_OFF_LVI:
                        /* Last Valid Index Register */
                        *pu32 = pStream->Regs.lvi;
                        Log3Func(("LVI[%d] -> %#x\n", AC97_PORT2IDX(offPort), *pu32));
                        break;
                    case AC97_NABM_OFF_PIV:
                        /* Prefetched Index Value Register */
                        *pu32 = pStream->Regs.piv;
                        Log3Func(("PIV[%d] -> %#x\n", AC97_PORT2IDX(offPort), *pu32));
                        break;
                    case AC97_NABM_OFF_CR:
                        /* Control Register */
                        *pu32 = pStream->Regs.cr;
                        Log3Func(("CR[%d] -> %#x\n", AC97_PORT2IDX(offPort), *pu32));
                        break;
                    case AC97_NABM_OFF_SR:
                        /* Status Register (lower part) */
                        *pu32 = RT_LO_U8(pStream->Regs.sr);
                        Log3Func(("SRb[%d] -> %#x\n", AC97_PORT2IDX(offPort), *pu32));
                        break;
                    default:
                        *pu32 = UINT32_MAX;
                        LogRel2(("AC97: Warning: Unimplemented NAMB read offPort=%#x LB 1 (line " RT_XSTR(__LINE__) ")\n", offPort));
                        STAM_REL_COUNTER_INC(&pThis->StatUnimplementedNabmReads);
                        break;
                }
                break;

            case 2:
                switch (offPort & AC97_NABM_OFF_MASK)
                {
                    case AC97_NABM_OFF_SR:
                        /* Status Register */
                        *pu32 = pStream->Regs.sr;
                        Log3Func(("SR[%d] -> %#x\n", AC97_PORT2IDX(offPort), *pu32));
                        break;
                    case AC97_NABM_OFF_PICB:
                        /* Position in Current Buffer
                         * ---
                         * We can do DMA work here if we want to give the guest a better impression of
                         * the DMA engine of a real device.  For ring-0 we'd have to add some buffering
                         * to AC97STREAM (4K or so), only going to ring-3 if full.  Ring-3 would commit
                         * that buffer and write directly to the internal DMA pCircBuf.
                         *
                         * Checking a Linux guest (knoppix 8.6.2), I see some PIC reads each DMA cycle,
                         * however most of these happen very very early, 1-10% into the buffer. So, I'm
                         * not sure if it's worth it, as it'll be a big complication... */
#if 1
                        *pu32 = pStream->Regs.picb;
# ifdef LOG_ENABLED
                        if (LogIs3Enabled())
                        {
                            uint64_t offPeriod = PDMDevHlpTimerGet(pDevIns, pStream->hTimer) - pStream->uArmedTs;
                            Log3Func(("PICB[%d] -> %#x (%RU64 of %RU64 ticks / %RU64%% into DMA period #%RU32)\n",
                                      AC97_PORT2IDX(offPort), *pu32, offPeriod, pStream->cDmaPeriodTicks,
                                      pStream->cDmaPeriodTicks ? offPeriod * 100 / pStream->cDmaPeriodTicks : 0,
                                      pStream->uDmaPeriod));
                        }
# endif
#else /* For trying out sub-buffer PICB.  Will cause distortions, but can be helpful to see if it help eliminate other issues.  */
                        if (   (pStream->Regs.cr & AC97_CR_RPBM)
                            && !(pStream->Regs.sr & AC97_SR_DCH)
                            && pStream->uArmedTs > 0
                            && pStream->cDmaPeriodTicks > 0)
                        {
                            uint64_t const offPeriod = PDMDevHlpTimerGet(pDevIns, pStream->hTimer) - pStream->uArmedTs;
                            uint32_t cSamples;
                            if (offPeriod < pStream->cDmaPeriodTicks)
                                cSamples = pStream->Regs.picb * offPeriod / pStream->cDmaPeriodTicks;
                            else
                                cSamples = pStream->Regs.picb;
                            if (cSamples + 8 < pStream->Regs.picb)
                            { /* likely */ }
                            else if (pStream->Regs.picb > 8)
                                cSamples = pStream->Regs.picb - 8;
                            else
                                cSamples = 0;
                            *pu32 = pStream->Regs.picb - cSamples;
                            Log3Func(("PICB[%d] -> %#x (PICB=%#x cSamples=%#x offPeriod=%RU64 of %RU64 / %RU64%%)\n",
                                      AC97_PORT2IDX(offPort), *pu32, pStream->Regs.picb, cSamples, offPeriod,
                                      pStream->cDmaPeriodTicks, offPeriod * 100 / pStream->cDmaPeriodTicks));
                        }
                        else
                        {
                            *pu32 = pStream->Regs.picb;
                            Log3Func(("PICB[%d] -> %#x\n", AC97_PORT2IDX(offPort), *pu32));
                        }
#endif
                        break;
                    default:
                        *pu32 = UINT32_MAX;
                        LogRel2(("AC97: Warning: Unimplemented NAMB read offPort=%#x LB 2 (line " RT_XSTR(__LINE__) ")\n", offPort));
                        STAM_REL_COUNTER_INC(&pThis->StatUnimplementedNabmReads);
                        break;
                }
                break;

            case 4:
                switch (offPort & AC97_NABM_OFF_MASK)
                {
                    case AC97_NABM_OFF_BDBAR:
                        /* Buffer Descriptor Base Address Register */
                        *pu32 = pStream->Regs.bdbar;
                        Log3Func(("BMADDR[%d] -> %#x\n", AC97_PORT2IDX(offPort), *pu32));
                        break;
                    case AC97_NABM_OFF_CIV:
                        /* 32-bit access: Current Index Value Register +
                         *                Last Valid Index Register +
                         *                Status Register */
                        *pu32 = pStream->Regs.civ | ((uint32_t)pStream->Regs.lvi << 8) | ((uint32_t)pStream->Regs.sr << 16);
                        Log3Func(("CIV LVI SR[%d] -> %#x, %#x, %#x\n",
                                  AC97_PORT2IDX(offPort), pStream->Regs.civ, pStream->Regs.lvi, pStream->Regs.sr));
                        break;
                    case AC97_NABM_OFF_PICB:
                        /* 32-bit access: Position in Current Buffer Register +
                         *                Prefetched Index Value Register +
                         *                Control Register */
                        *pu32 = pStream->Regs.picb | ((uint32_t)pStream->Regs.piv << 16) | ((uint32_t)pStream->Regs.cr << 24);
                        Log3Func(("PICB PIV CR[%d] -> %#x %#x %#x %#x\n",
                                  AC97_PORT2IDX(offPort), *pu32, pStream->Regs.picb, pStream->Regs.piv, pStream->Regs.cr));
                        break;

                    default:
                        *pu32 = UINT32_MAX;
                        LogRel2(("AC97: Warning: Unimplemented NAMB read offPort=%#x LB 4 (line " RT_XSTR(__LINE__) ")\n", offPort));
                        STAM_REL_COUNTER_INC(&pThis->StatUnimplementedNabmReads);
                        break;
                }
                break;

            default:
                DEVAC97_UNLOCK(pDevIns, pThis);
                AssertFailed();
                return VERR_IOM_IOPORT_UNUSED;
        }
    }
    else
    {
        switch (cb)
        {
            case 1:
                switch (offPort)
                {
                    case AC97_CAS:
                        /* Codec Access Semaphore Register */
                        Log3Func(("CAS %d\n", pThis->cas));
                        *pu32 = pThis->cas;
                        pThis->cas = 1;
                        break;
                    default:
                        *pu32 = UINT32_MAX;
                        LogRel2(("AC97: Warning: Unimplemented NAMB read offPort=%#x LB 1 (line " RT_XSTR(__LINE__) ")\n", offPort));
                        STAM_REL_COUNTER_INC(&pThis->StatUnimplementedNabmReads);
                        break;
                }
                break;

            case 2:
                *pu32 = UINT32_MAX;
                LogRel2(("AC97: Warning: Unimplemented NAMB read offPort=%#x LB 2 (line " RT_XSTR(__LINE__) ")\n", offPort));
                STAM_REL_COUNTER_INC(&pThis->StatUnimplementedNabmReads);
                break;

            case 4:
                switch (offPort)
                {
                    case AC97_GLOB_CNT:
                        /* Global Control */
                        *pu32 = pThis->glob_cnt;
                        Log3Func(("glob_cnt -> %#x\n", *pu32));
                        break;
                    case AC97_GLOB_STA:
                        /* Global Status */
                        *pu32 = pThis->glob_sta | AC97_GS_S0CR;
                        Log3Func(("glob_sta -> %#x\n", *pu32));
                        break;
                    default:
                        *pu32 = UINT32_MAX;
                        LogRel2(("AC97: Warning: Unimplemented NAMB read offPort=%#x LB 4 (line " RT_XSTR(__LINE__) ")\n", offPort));
                        STAM_REL_COUNTER_INC(&pThis->StatUnimplementedNabmReads);
                        break;
                }
                break;

            default:
                DEVAC97_UNLOCK(pDevIns, pThis);
                AssertFailed();
                return VERR_IOM_IOPORT_UNUSED;
        }
    }

    DEVAC97_UNLOCK(pDevIns, pThis);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT}
 */
static DECLCALLBACK(VBOXSTRICTRC)
ichac97IoPortNabmWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PAC97STATE      pThis   = PDMDEVINS_2_DATA(pDevIns, PAC97STATE);
#ifdef IN_RING3
    PAC97STATER3    pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PAC97STATER3);
#endif
    RT_NOREF(pvUser);

    VBOXSTRICTRC rc = VINF_SUCCESS;
    if (   AC97_PORT2IDX_UNMASKED(offPort) < AC97_MAX_STREAMS
        && offPort != AC97_GLOB_CNT)
    {
#ifdef IN_RING3
        PAC97STREAMR3   pStreamCC = &pThisCC->aStreams[AC97_PORT2IDX(offPort)];
#endif
        PAC97STREAM     pStream   = &pThis->aStreams[AC97_PORT2IDX(offPort)];

        switch (cb)
        {
            case 1:
                switch (offPort & AC97_NABM_OFF_MASK)
                {
                    /*
                     * Last Valid Index.
                     */
                    case AC97_NABM_OFF_LVI:
                        DEVAC97_LOCK_RETURN(pDevIns, pThis, VINF_IOM_R3_IOPORT_WRITE);

                        if (   !(pStream->Regs.sr & AC97_SR_DCH)
                            || !(pStream->Regs.cr & AC97_CR_RPBM))
                        {
                            pStream->Regs.lvi = u32 % AC97_MAX_BDLE;
                            STAM_REL_COUNTER_INC(&pStream->StatWriteLvi);
                            DEVAC97_UNLOCK(pDevIns, pThis);
                            Log3Func(("[SD%RU8] LVI <- %#x\n", pStream->u8SD, u32));
                        }
                        else
                        {
#ifdef IN_RING3
                            /* Recover from underflow situation where CIV caught up with LVI
                               and the DMA processing stopped.  We clear the status condition,
                               update LVI and then try to load the next BDLE.  Unfortunately,
                               we cannot do this from ring-0 as much of the BDLE state is
                               ring-3 only. */
                            pStream->Regs.sr &= ~(AC97_SR_DCH | AC97_SR_CELV);
                            pStream->Regs.lvi = u32 % AC97_MAX_BDLE;
                            if (ichac97R3StreamFetchNextBdle(pDevIns, pStream, pStreamCC))
                                ichac97StreamUpdateSR(pDevIns, pThis, pStream, pStream->Regs.sr | AC97_SR_BCIS);

                            /* We now have to re-arm the DMA timer according to the new BDLE length.
                               This means leaving the device lock to avoid virtual sync lock order issues. */
                            ichac97R3StreamTransferUpdate(pDevIns, pStream, pStreamCC);
                            uint64_t const cTicksToDeadline = pStream->cDmaPeriodTicks;

                            /** @todo Stop the DMA timer when we get into the AC97_SR_CELV situation to
                             *        avoid potential race here. */
                            STAM_REL_COUNTER_INC(&pStreamCC->State.StatWriteLviRecover);
                            DEVAC97_UNLOCK(pDevIns, pThis);

                            LogFunc(("[SD%RU8] LVI <- %#x; CIV=%#x PIV=%#x SR=%#x cTicksToDeadline=%#RX64 [recovering]\n",
                                     pStream->u8SD, u32, pStream->Regs.civ, pStream->Regs.piv, pStream->Regs.sr, cTicksToDeadline));

                            int rc2 = PDMDevHlpTimerSetRelative(pDevIns, pStream->hTimer, cTicksToDeadline, &pStream->uArmedTs);
                            AssertRC(rc2);
#else
                            DEVAC97_UNLOCK(pDevIns, pThis);
                            rc = VINF_IOM_R3_IOPORT_WRITE;
#endif
                        }
                        break;

                    /*
                     * Control Registers.
                     */
                    case AC97_NABM_OFF_CR:
                    {
#ifdef IN_RING3
                        DEVAC97_LOCK(pDevIns, pThis);
                        STAM_REL_COUNTER_INC(&pStreamCC->State.StatWriteCr);

                        uint32_t const fCrChanged = pStream->Regs.cr ^ u32;
                        Log3Func(("[SD%RU8] CR <- %#x (was %#x; changed %#x)\n", pStream->u8SD, u32, pStream->Regs.cr, fCrChanged));

                        /*
                         * Busmaster reset.
                         */
                        if (u32 & AC97_CR_RR)
                        {
                            STAM_REL_PROFILE_START_NS(&pStreamCC->State.StatReset, r);
                            LogFunc(("[SD%RU8] Reset\n", pStream->u8SD));

                            /* Make sure that Run/Pause Bus Master bit (RPBM) is cleared (0).
                               3.2.7 in 302349-003 says RPBM be must be clear when resetting
                               and that behavior is undefined if it's set. */
                            ASSERT_GUEST_STMT((pStream->Regs.cr & AC97_CR_RPBM) == 0,
                                              ichac97R3StreamEnable(pDevIns, pThis, pThisCC, pStream,
                                                                    pStreamCC, false /* fEnable */));

                            ichac97R3StreamReset(pThis, pStream, pStreamCC);

                            ichac97StreamUpdateSR(pDevIns, pThis, pStream, AC97_SR_DCH); /** @todo Do we need to do that? */

                            DEVAC97_UNLOCK(pDevIns, pThis);
                            STAM_REL_PROFILE_STOP_NS(&pStreamCC->State.StatReset, r);
                            break;
                        }

                        /*
                         * Write the new value to the register and if RPBM didn't change we're done.
                         */
                        pStream->Regs.cr = u32 & AC97_CR_VALID_MASK;

                        if (!(fCrChanged & AC97_CR_RPBM))
                            DEVAC97_UNLOCK(pDevIns, pThis); /* Probably not so likely, but avoid one extra intentation level. */
                        /*
                         * Pause busmaster.
                         */
                        else if (!(pStream->Regs.cr & AC97_CR_RPBM))
                        {
                            STAM_REL_PROFILE_START_NS(&pStreamCC->State.StatStop, p);
                            LogFunc(("[SD%RU8] Pause busmaster (disable stream) SR=%#x -> %#x\n",
                                     pStream->u8SD, pStream->Regs.sr, pStream->Regs.sr | AC97_SR_DCH));
                            ichac97R3StreamEnable(pDevIns, pThis, pThisCC, pStream, pStreamCC, false /* fEnable */);
                            pStream->Regs.sr |= AC97_SR_DCH;

                            DEVAC97_UNLOCK(pDevIns, pThis);
                            STAM_REL_PROFILE_STOP_NS(&pStreamCC->State.StatStop, p);
                        }
                        /*
                         * Run busmaster.
                         */
                        else
                        {
                            STAM_REL_PROFILE_START_NS(&pStreamCC->State.StatStart, r);
                            LogFunc(("[SD%RU8] Run busmaster (enable stream) SR=%#x -> %#x\n",
                                     pStream->u8SD, pStream->Regs.sr, pStream->Regs.sr & ~AC97_SR_DCH));
                            pStream->Regs.sr &= ~AC97_SR_DCH;

                            if (ichac97R3StreamFetchNextBdle(pDevIns, pStream, pStreamCC))
                                ichac97StreamUpdateSR(pDevIns, pThis, pStream, pStream->Regs.sr | AC97_SR_BCIS);
# ifdef LOG_ENABLED
                            if (LogIsFlowEnabled())
                                ichac97R3DbgPrintBdl(pDevIns, pThis, pStream, PDMDevHlpDBGFInfoLogHlp(pDevIns), "ichac97IoPortNabmWrite: ");
# endif
                            ichac97R3StreamEnable(pDevIns, pThis, pThisCC, pStream, pStreamCC, true /* fEnable */);

                            /*
                             * Arm the DMA timer.  Must drop the AC'97 device lock first as it would
                             * create a lock order violation with the virtual sync time lock otherwise.
                             */
                            ichac97R3StreamTransferUpdate(pDevIns, pStream, pStreamCC);
                            uint64_t const cTicksToDeadline = pStream->cDmaPeriodTicks;

                            DEVAC97_UNLOCK(pDevIns, pThis);

                            /** @todo for output streams we could probably service this a little bit
                             *        earlier if we push it, just to reduce the lag...  For HDA we do a
                             *        DMA run immediately after the stream is enabled. */
                            int rc2 = PDMDevHlpTimerSetRelative(pDevIns, pStream->hTimer, cTicksToDeadline, &pStream->uArmedTs);
                            AssertRC(rc2);

                            STAM_REL_PROFILE_STOP_NS(&pStreamCC->State.StatStart, r);
                        }
#else /* !IN_RING3 */
                        rc = VINF_IOM_R3_IOPORT_WRITE;
#endif
                        break;
                    }

                    /*
                     * Status Registers.
                     */
                    case AC97_NABM_OFF_SR:
                        DEVAC97_LOCK_RETURN(pDevIns, pThis, VINF_IOM_R3_IOPORT_WRITE);
                        ichac97StreamWriteSR(pDevIns, pThis, pStream, u32);
                        STAM_REL_COUNTER_INC(&pStream->StatWriteSr1);
                        DEVAC97_UNLOCK(pDevIns, pThis);
                        break;

                    default:
                        /* Linux tries to write CIV. */
                        LogRel2(("AC97: Warning: Unimplemented NAMB write offPort=%#x%s <- %#x LB 1 (line " RT_XSTR(__LINE__) ")\n",
                                 offPort, (offPort & AC97_NABM_OFF_MASK) == AC97_NABM_OFF_CIV ? " (CIV)" : "" , u32));
                        STAM_REL_COUNTER_INC(&pThis->StatUnimplementedNabmWrites);
                        break;
                }
                break;

            case 2:
                switch (offPort & AC97_NABM_OFF_MASK)
                {
                    case AC97_NABM_OFF_SR:
                        DEVAC97_LOCK_RETURN(pDevIns, pThis, VINF_IOM_R3_IOPORT_WRITE);
                        ichac97StreamWriteSR(pDevIns, pThis, pStream, u32);
                        STAM_REL_COUNTER_INC(&pStream->StatWriteSr2);
                        DEVAC97_UNLOCK(pDevIns, pThis);
                        break;
                    default:
                        LogRel2(("AC97: Warning: Unimplemented NAMB write offPort=%#x <- %#x LB 2 (line " RT_XSTR(__LINE__) ")\n", offPort, u32));
                        STAM_REL_COUNTER_INC(&pThis->StatUnimplementedNabmWrites);
                        break;
                }
                break;

            case 4:
                switch (offPort & AC97_NABM_OFF_MASK)
                {
                    case AC97_NABM_OFF_BDBAR:
                        DEVAC97_LOCK_RETURN(pDevIns, pThis, VINF_IOM_R3_IOPORT_WRITE);
                        /* Buffer Descriptor list Base Address Register */
                        pStream->Regs.bdbar = u32 & ~(uint32_t)3;
                        Log3Func(("[SD%RU8] BDBAR <- %#x (bdbar %#x)\n", AC97_PORT2IDX(offPort), u32, pStream->Regs.bdbar));
                        STAM_REL_COUNTER_INC(&pStream->StatWriteBdBar);
                        DEVAC97_UNLOCK(pDevIns, pThis);
                        break;
                    default:
                        LogRel2(("AC97: Warning: Unimplemented NAMB write offPort=%#x <- %#x LB 4 (line " RT_XSTR(__LINE__) ")\n", offPort, u32));
                        STAM_REL_COUNTER_INC(&pThis->StatUnimplementedNabmWrites);
                        break;
                }
                break;

            default:
                AssertMsgFailed(("offPort=%#x <- %#x LB %u\n", offPort, u32, cb));
                break;
        }
    }
    else
    {
        switch (cb)
        {
            case 1:
                LogRel2(("AC97: Warning: Unimplemented NAMB write offPort=%#x <- %#x LB 1 (line " RT_XSTR(__LINE__) ")\n", offPort, u32));
                STAM_REL_COUNTER_INC(&pThis->StatUnimplementedNabmWrites);
                break;

            case 2:
                LogRel2(("AC97: Warning: Unimplemented NAMB write offPort=%#x <- %#x LB 2 (line " RT_XSTR(__LINE__) ")\n", offPort, u32));
                STAM_REL_COUNTER_INC(&pThis->StatUnimplementedNabmWrites);
                break;

            case 4:
                switch (offPort)
                {
                    case AC97_GLOB_CNT:
                        /* Global Control */
                        DEVAC97_LOCK_RETURN(pDevIns, pThis, VINF_IOM_R3_IOPORT_WRITE);
                        if (u32 & AC97_GC_WR)
                            ichac97WarmReset(pThis);
                        if (u32 & AC97_GC_CR)
                            ichac97ColdReset(pThis);
                        if (!(u32 & (AC97_GC_WR | AC97_GC_CR)))
                            pThis->glob_cnt = u32 & AC97_GC_VALID_MASK;
                        Log3Func(("glob_cnt <- %#x (glob_cnt %#x)\n", u32, pThis->glob_cnt));
                        DEVAC97_UNLOCK(pDevIns, pThis);
                        break;
                    case AC97_GLOB_STA:
                        /* Global Status */
                        DEVAC97_LOCK_RETURN(pDevIns, pThis, VINF_IOM_R3_IOPORT_WRITE);
                        pThis->glob_sta &= ~(u32 & AC97_GS_WCLEAR_MASK);
                        pThis->glob_sta |= (u32 & ~(AC97_GS_WCLEAR_MASK | AC97_GS_RO_MASK)) & AC97_GS_VALID_MASK;
                        Log3Func(("glob_sta <- %#x (glob_sta %#x)\n", u32, pThis->glob_sta));
                        DEVAC97_UNLOCK(pDevIns, pThis);
                        break;
                    default:
                        LogRel2(("AC97: Warning: Unimplemented NAMB write offPort=%#x <- %#x LB 4 (line " RT_XSTR(__LINE__) ")\n", offPort, u32));
                        STAM_REL_COUNTER_INC(&pThis->StatUnimplementedNabmWrites);
                        break;
                }
                break;

            default:
                AssertMsgFailed(("offPort=%#x <- %#x LB %u\n", offPort, u32, cb));
                break;
        }
    }

    return rc;
}


/*********************************************************************************************************************************
*   Mixer & NAM I/O handlers                                                                                                     *
*********************************************************************************************************************************/

/**
 * Sets a AC'97 mixer control to a specific value.
 *
 * @param   pThis               The shared AC'97 state.
 * @param   uMixerIdx           Mixer control to set value for.
 * @param   uVal                Value to set.
 */
static void ichac97MixerSet(PAC97STATE pThis, uint8_t uMixerIdx, uint16_t uVal)
{
    AssertMsgReturnVoid(uMixerIdx + 2U <= sizeof(pThis->mixer_data),
                        ("Index %RU8 out of bounds (%zu)\n", uMixerIdx, sizeof(pThis->mixer_data)));

    LogRel2(("AC97: Setting mixer index #%RU8 to %RU16 (%RU8 %RU8)\n", uMixerIdx, uVal, RT_HI_U8(uVal), RT_LO_U8(uVal)));

    pThis->mixer_data[uMixerIdx + 0] = RT_LO_U8(uVal);
    pThis->mixer_data[uMixerIdx + 1] = RT_HI_U8(uVal);
}


/**
 * Gets a value from a specific AC'97 mixer control.
 *
 * @returns Retrieved mixer control value.
 * @param   pThis               The shared AC'97 state.
 * @param   uMixerIdx           Mixer control to get value for.
 */
static uint16_t ichac97MixerGet(PAC97STATE pThis, uint32_t uMixerIdx)
{
    AssertMsgReturn(uMixerIdx + 2U <= sizeof(pThis->mixer_data),
                    ("Index %RU8 out of bounds (%zu)\n", uMixerIdx, sizeof(pThis->mixer_data)),
                    UINT16_MAX);
    return RT_MAKE_U16(pThis->mixer_data[uMixerIdx + 0], pThis->mixer_data[uMixerIdx + 1]);
}

#ifdef IN_RING3

/**
 * Sets the volume of a specific AC'97 mixer control.
 *
 * This currently only supports attenuation -- gain support is currently not implemented.
 *
 * @returns VBox status code.
 * @param   pThis               The shared AC'97 state.
 * @param   pThisCC             The ring-3 AC'97 state.
 * @param   index               AC'97 mixer index to set volume for.
 * @param   enmMixerCtl         Corresponding audio mixer sink.
 * @param   uVal                Volume value to set.
 */
static int ichac97R3MixerSetVolume(PAC97STATE pThis, PAC97STATER3 pThisCC, int index, PDMAUDIOMIXERCTL enmMixerCtl, uint32_t uVal)
{
    /*
     * From AC'97 SoundMax Codec AD1981A/AD1981B:
     * "Because AC '97 defines 6-bit volume registers, to maintain compatibility whenever the
     *  D5 or D13 bits are set to 1, their respective lower five volume bits are automatically
     *  set to 1 by the Codec logic. On readback, all lower 5 bits will read ones whenever
     *  these bits are set to 1."
     *
     * Linux ALSA depends on this behavior to detect that only 5 bits are used for volume
     * control and the optional 6th bit is not used. Note that this logic only applies to the
     * master volume controls.
     */
    if (   index == AC97_Master_Volume_Mute
        || index == AC97_Headphone_Volume_Mute
        || index == AC97_Master_Volume_Mono_Mute)
    {
        if (uVal & RT_BIT(5))  /* D5 bit set? */
            uVal |= RT_BIT(4) | RT_BIT(3) | RT_BIT(2) | RT_BIT(1) | RT_BIT(0);
        if (uVal & RT_BIT(13)) /* D13 bit set? */
            uVal |= RT_BIT(12) | RT_BIT(11) | RT_BIT(10) | RT_BIT(9) | RT_BIT(8);
    }

    const bool  fCtlMuted    = (uVal >> AC97_BARS_VOL_MUTE_SHIFT) & 1;
    uint8_t     uCtlAttLeft  = (uVal >> 8) & AC97_BARS_VOL_MASK;
    uint8_t     uCtlAttRight = uVal & AC97_BARS_VOL_MASK;

    /* For the master and headphone volume, 0 corresponds to 0dB attenuation. For the other
     * volume controls, 0 means 12dB gain and 8 means unity gain.
     */
    if (index != AC97_Master_Volume_Mute && index != AC97_Headphone_Volume_Mute)
    {
# ifndef VBOX_WITH_AC97_GAIN_SUPPORT
        /* NB: Currently there is no gain support, only attenuation. */
        uCtlAttLeft  = uCtlAttLeft  < 8 ? 0 : uCtlAttLeft  - 8;
        uCtlAttRight = uCtlAttRight < 8 ? 0 : uCtlAttRight - 8;
# endif
    }
    Assert(uCtlAttLeft  <= 255 / AC97_DB_FACTOR);
    Assert(uCtlAttRight <= 255 / AC97_DB_FACTOR);

    LogFunc(("index=0x%x, uVal=%RU32, enmMixerCtl=%RU32\n", index, uVal, enmMixerCtl));
    LogFunc(("uCtlAttLeft=%RU8, uCtlAttRight=%RU8 ", uCtlAttLeft, uCtlAttRight));

    /*
     * For AC'97 volume controls, each additional step means -1.5dB attenuation with
     * zero being maximum. In contrast, we're internally using 255 (PDMAUDIO_VOLUME_MAX)
     * steps, each -0.375dB, where 0 corresponds to -96dB and 255 corresponds to 0dB.
     */
    uint8_t lVol = PDMAUDIO_VOLUME_MAX - uCtlAttLeft  * AC97_DB_FACTOR;
    uint8_t rVol = PDMAUDIO_VOLUME_MAX - uCtlAttRight * AC97_DB_FACTOR;

    Log(("-> fMuted=%RTbool, lVol=%RU8, rVol=%RU8\n", fCtlMuted, lVol, rVol));

    int rc = VINF_SUCCESS;

    if (pThisCC->pMixer) /* Device can be in reset state, so no mixer available. */
    {
        PDMAUDIOVOLUME Vol;
        PDMAudioVolumeInitFromStereo(&Vol, fCtlMuted, lVol, rVol);

        PAUDMIXSINK pSink = NULL;
        switch (enmMixerCtl)
        {
            case PDMAUDIOMIXERCTL_VOLUME_MASTER:
                rc = AudioMixerSetMasterVolume(pThisCC->pMixer, &Vol);
                break;

            case PDMAUDIOMIXERCTL_FRONT:
                pSink = pThisCC->pSinkOut;
                break;

            case PDMAUDIOMIXERCTL_MIC_IN:
            case PDMAUDIOMIXERCTL_LINE_IN:
                /* These are recognized but do nothing. */
                break;

            default:
                AssertFailed();
                rc = VERR_NOT_SUPPORTED;
                break;
        }

        if (pSink)
            rc = AudioMixerSinkSetVolume(pSink, &Vol);
    }

    ichac97MixerSet(pThis, index, uVal);

    if (RT_FAILURE(rc))
        LogFlowFunc(("Failed with %Rrc\n", rc));

    return rc;
}

/**
 * Sets the gain of a specific AC'97 recording control.
 *
 * @note    Gain support is currently not implemented in PDM audio.
 *
 * @returns VBox status code.
 * @param   pThis               The shared AC'97 state.
 * @param   pThisCC             The ring-3 AC'97 state.
 * @param   index               AC'97 mixer index to set volume for.
 * @param   enmMixerCtl         Corresponding audio mixer sink.
 * @param   uVal                Volume value to set.
 */
static int ichac97R3MixerSetGain(PAC97STATE pThis, PAC97STATER3 pThisCC, int index, PDMAUDIOMIXERCTL enmMixerCtl, uint32_t uVal)
{
    /*
     * For AC'97 recording controls, each additional step means +1.5dB gain with
     * zero being 0dB gain and 15 being +22.5dB gain.
     */
    bool const  fCtlMuted     = (uVal >> AC97_BARS_VOL_MUTE_SHIFT) & 1;
    uint8_t     uCtlGainLeft  = (uVal >> 8) & AC97_BARS_GAIN_MASK;
    uint8_t     uCtlGainRight = uVal & AC97_BARS_GAIN_MASK;

    Assert(uCtlGainLeft  <= 255 / AC97_DB_FACTOR);
    Assert(uCtlGainRight <= 255 / AC97_DB_FACTOR);

    LogFunc(("index=0x%x, uVal=%RU32, enmMixerCtl=%RU32\n", index, uVal, enmMixerCtl));
    LogFunc(("uCtlGainLeft=%RU8, uCtlGainRight=%RU8 ", uCtlGainLeft, uCtlGainRight));

    uint8_t lVol = PDMAUDIO_VOLUME_MAX + uCtlGainLeft  * AC97_DB_FACTOR;
    uint8_t rVol = PDMAUDIO_VOLUME_MAX + uCtlGainRight * AC97_DB_FACTOR;

    /* We do not currently support gain. Since AC'97 does not support attenuation
     * for the recording input, the best we can do is set the maximum volume.
     */
# ifndef VBOX_WITH_AC97_GAIN_SUPPORT
    /* NB: Currently there is no gain support, only attenuation. Since AC'97 does not
     * support attenuation for the recording inputs, the best we can do is set the
     * maximum volume.
     */
    lVol = rVol = PDMAUDIO_VOLUME_MAX;
# endif

    Log(("-> fMuted=%RTbool, lVol=%RU8, rVol=%RU8\n", fCtlMuted, lVol, rVol));

    int rc = VINF_SUCCESS;

    if (pThisCC->pMixer) /* Device can be in reset state, so no mixer available. */
    {
        PDMAUDIOVOLUME Vol;
        PDMAudioVolumeInitFromStereo(&Vol, fCtlMuted, lVol, rVol);

        PAUDMIXSINK    pSink = NULL;
        switch (enmMixerCtl)
        {
            case PDMAUDIOMIXERCTL_MIC_IN:
                pSink = pThisCC->pSinkMicIn;
                break;

            case PDMAUDIOMIXERCTL_LINE_IN:
                pSink = pThisCC->pSinkLineIn;
                break;

            default:
                AssertFailed();
                rc = VERR_NOT_SUPPORTED;
                break;
        }

        if (pSink)
        {
            rc = AudioMixerSinkSetVolume(pSink, &Vol);
            /* There is only one AC'97 recording gain control. If line in
             * is changed, also update the microphone. If the optional dedicated
             * microphone is changed, only change that.
             * NB: The codecs we support do not have the dedicated microphone control.
             */
            if (pSink == pThisCC->pSinkLineIn && pThisCC->pSinkMicIn)
                rc = AudioMixerSinkSetVolume(pSink, &Vol);
        }
    }

    ichac97MixerSet(pThis, index, uVal);

    if (RT_FAILURE(rc))
        LogFlowFunc(("Failed with %Rrc\n", rc));

    return rc;
}


/**
 * Converts an AC'97 recording source index to a PDM audio recording source.
 *
 * @returns PDM audio recording source.
 * @param   uIdx                AC'97 index to convert.
 */
static PDMAUDIOPATH ichac97R3IdxToRecSource(uint8_t uIdx)
{
    switch (uIdx)
    {
        case AC97_REC_MIC:     return PDMAUDIOPATH_IN_MIC;
        case AC97_REC_CD:      return PDMAUDIOPATH_IN_CD;
        case AC97_REC_VIDEO:   return PDMAUDIOPATH_IN_VIDEO;
        case AC97_REC_AUX:     return PDMAUDIOPATH_IN_AUX;
        case AC97_REC_LINE_IN: return PDMAUDIOPATH_IN_LINE;
        case AC97_REC_PHONE:   return PDMAUDIOPATH_IN_PHONE;
        default:
            break;
    }

    LogFlowFunc(("Unknown record source %d, using MIC\n", uIdx));
    return PDMAUDIOPATH_IN_MIC;
}


/**
 * Converts a PDM audio recording source to an AC'97 recording source index.
 *
 * @returns AC'97 recording source index.
 * @param   enmRecSrc           PDM audio recording source to convert.
 */
static uint8_t ichac97R3RecSourceToIdx(PDMAUDIOPATH enmRecSrc)
{
    switch (enmRecSrc)
    {
        case PDMAUDIOPATH_IN_MIC:    return AC97_REC_MIC;
        case PDMAUDIOPATH_IN_CD:     return AC97_REC_CD;
        case PDMAUDIOPATH_IN_VIDEO:  return AC97_REC_VIDEO;
        case PDMAUDIOPATH_IN_AUX:    return AC97_REC_AUX;
        case PDMAUDIOPATH_IN_LINE:   return AC97_REC_LINE_IN;
        case PDMAUDIOPATH_IN_PHONE:  return AC97_REC_PHONE;
        default:
            AssertMsgFailedBreak(("%d\n", enmRecSrc));
    }

    LogFlowFunc(("Unknown audio recording source %d using MIC\n", enmRecSrc));
    return AC97_REC_MIC;
}


/**
 * Performs an AC'97 mixer record select to switch to a different recording
 * source.
 *
 * @param   pThis               The shared AC'97 state.
 * @param   val                 AC'97 recording source index to set.
 */
static void ichac97R3MixerRecordSelect(PAC97STATE pThis, uint32_t val)
{
    uint8_t rs = val & AC97_REC_MASK;
    uint8_t ls = (val >> 8) & AC97_REC_MASK;

    PDMAUDIOPATH const ars = ichac97R3IdxToRecSource(rs);
    PDMAUDIOPATH const als = ichac97R3IdxToRecSource(ls);

    rs = ichac97R3RecSourceToIdx(ars);
    ls = ichac97R3RecSourceToIdx(als);

    LogRel(("AC97: Record select to left=%s, right=%s\n", PDMAudioPathGetName(ars), PDMAudioPathGetName(als)));

    ichac97MixerSet(pThis, AC97_Record_Select, rs | (ls << 8));
}

/**
 * Resets the AC'97 mixer.
 *
 * @returns VBox status code.
 * @param   pThis               The shared AC'97 state.
 * @param   pThisCC             The ring-3 AC'97 state.
 */
static int ichac97R3MixerReset(PAC97STATE pThis, PAC97STATER3 pThisCC)
{
    LogFlowFuncEnter();

    RT_ZERO(pThis->mixer_data);

    /* Note: Make sure to reset all registers first before bailing out on error. */

    ichac97MixerSet(pThis, AC97_Reset                   , 0x0000); /* 6940 */
    ichac97MixerSet(pThis, AC97_Master_Volume_Mono_Mute , 0x8000);
    ichac97MixerSet(pThis, AC97_PC_BEEP_Volume_Mute     , 0x0000);

    ichac97MixerSet(pThis, AC97_Phone_Volume_Mute       , 0x8008);
    ichac97MixerSet(pThis, AC97_Mic_Volume_Mute         , 0x8008);
    ichac97MixerSet(pThis, AC97_CD_Volume_Mute          , 0x8808);
    ichac97MixerSet(pThis, AC97_Aux_Volume_Mute         , 0x8808);
    ichac97MixerSet(pThis, AC97_Record_Gain_Mic_Mute    , 0x8000);
    ichac97MixerSet(pThis, AC97_General_Purpose         , 0x0000);
    ichac97MixerSet(pThis, AC97_3D_Control              , 0x0000);
    ichac97MixerSet(pThis, AC97_Powerdown_Ctrl_Stat     , 0x000f);

    /* Configure Extended Audio ID (EAID) + Control & Status (EACS) registers. */
    const uint16_t fEAID = AC97_EAID_REV1 | AC97_EACS_VRA | AC97_EACS_VRM; /* Our hardware is AC'97 rev2.3 compliant. */
    const uint16_t fEACS = AC97_EACS_VRA | AC97_EACS_VRM;                  /* Variable Rate PCM Audio (VRA) + Mic-In (VRM) capable. */

    LogRel(("AC97: Mixer reset (EAID=0x%x, EACS=0x%x)\n", fEAID, fEACS));

    ichac97MixerSet(pThis, AC97_Extended_Audio_ID,        fEAID);
    ichac97MixerSet(pThis, AC97_Extended_Audio_Ctrl_Stat, fEACS);
    ichac97MixerSet(pThis, AC97_PCM_Front_DAC_Rate      , 0xbb80 /* 48000 Hz by default */);
    ichac97MixerSet(pThis, AC97_PCM_Surround_DAC_Rate   , 0xbb80 /* 48000 Hz by default */);
    ichac97MixerSet(pThis, AC97_PCM_LFE_DAC_Rate        , 0xbb80 /* 48000 Hz by default */);
    ichac97MixerSet(pThis, AC97_PCM_LR_ADC_Rate         , 0xbb80 /* 48000 Hz by default */);
    ichac97MixerSet(pThis, AC97_MIC_ADC_Rate            , 0xbb80 /* 48000 Hz by default */);

    if (pThis->enmCodecModel == AC97CODEC_AD1980)
    {
        /* Analog Devices 1980 (AD1980) */
        ichac97MixerSet(pThis, AC97_Reset                   , 0x0010); /* Headphones. */
        ichac97MixerSet(pThis, AC97_Vendor_ID1              , 0x4144);
        ichac97MixerSet(pThis, AC97_Vendor_ID2              , 0x5370);
        ichac97MixerSet(pThis, AC97_Headphone_Volume_Mute   , 0x8000);
    }
    else if (pThis->enmCodecModel == AC97CODEC_AD1981B)
    {
        /* Analog Devices 1981B (AD1981B) */
        ichac97MixerSet(pThis, AC97_Vendor_ID1              , 0x4144);
        ichac97MixerSet(pThis, AC97_Vendor_ID2              , 0x5374);
    }
    else
    {
        /* Sigmatel 9700 (STAC9700) */
        ichac97MixerSet(pThis, AC97_Vendor_ID1              , 0x8384);
        ichac97MixerSet(pThis, AC97_Vendor_ID2              , 0x7600); /* 7608 */
    }
    ichac97R3MixerRecordSelect(pThis, 0);

    /* The default value is 8000h, which corresponds to 0 dB attenuation with mute on. */
    ichac97R3MixerSetVolume(pThis, pThisCC, AC97_Master_Volume_Mute,  PDMAUDIOMIXERCTL_VOLUME_MASTER, 0x8000);

    /* The default value for stereo registers is 8808h, which corresponds to 0 dB gain with mute on.*/
    ichac97R3MixerSetVolume(pThis, pThisCC, AC97_PCM_Out_Volume_Mute, PDMAUDIOMIXERCTL_FRONT,         0x8808);
    ichac97R3MixerSetVolume(pThis, pThisCC, AC97_Line_In_Volume_Mute, PDMAUDIOMIXERCTL_LINE_IN,       0x8808);
    ichac97R3MixerSetVolume(pThis, pThisCC, AC97_Mic_Volume_Mute,     PDMAUDIOMIXERCTL_MIC_IN,        0x8008);

    /* The default for record controls is 0 dB gain with mute on. */
    ichac97R3MixerSetGain(pThis, pThisCC, AC97_Record_Gain_Mute,      PDMAUDIOMIXERCTL_LINE_IN,       0x8000);
    ichac97R3MixerSetGain(pThis, pThisCC, AC97_Record_Gain_Mic_Mute,  PDMAUDIOMIXERCTL_MIC_IN,        0x8000);

    return VINF_SUCCESS;
}

#endif /* IN_RING3 */

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN}
 */
static DECLCALLBACK(VBOXSTRICTRC)
ichac97IoPortNamRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PAC97STATE pThis = PDMDEVINS_2_DATA(pDevIns, PAC97STATE);
    RT_NOREF(pvUser);
    Assert(offPort < 256);

    DEVAC97_LOCK_RETURN(pDevIns, pThis, VINF_IOM_R3_IOPORT_READ);

    VBOXSTRICTRC rc = VINF_SUCCESS;
    switch (cb)
    {
        case 1:
            LogRel2(("AC97: Warning: Unimplemented NAM read offPort=%#x LB 1 (line " RT_XSTR(__LINE__) ")\n", offPort));
            STAM_REL_COUNTER_INC(&pThis->StatUnimplementedNamReads);
            pThis->cas = 0;
            *pu32 = UINT32_MAX;
            break;

        case 2:
            pThis->cas = 0;
            *pu32 = ichac97MixerGet(pThis, offPort);
            break;

        case 4:
            LogRel2(("AC97: Warning: Unimplemented NAM read offPort=%#x LB 4 (line " RT_XSTR(__LINE__) ")\n", offPort));
            STAM_REL_COUNTER_INC(&pThis->StatUnimplementedNamReads);
            pThis->cas = 0;
            *pu32 = UINT32_MAX;
            break;

        default:
            AssertFailed();
            rc = VERR_IOM_IOPORT_UNUSED;
            break;
    }

    DEVAC97_UNLOCK(pDevIns, pThis);
    return rc;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT}
 */
static DECLCALLBACK(VBOXSTRICTRC)
ichac97IoPortNamWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PAC97STATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PAC97STATE);
#ifdef IN_RING3
    PAC97STATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PAC97STATER3);
#endif
    RT_NOREF(pvUser);

    DEVAC97_LOCK_RETURN(pDevIns, pThis, VINF_IOM_R3_IOPORT_WRITE);

    VBOXSTRICTRC rc = VINF_SUCCESS;
    switch (cb)
    {
        case 1:
            LogRel2(("AC97: Warning: Unimplemented NAM write offPort=%#x <- %#x LB 1 (line " RT_XSTR(__LINE__) ")\n", offPort, u32));
            STAM_REL_COUNTER_INC(&pThis->StatUnimplementedNamWrites);
            pThis->cas = 0;
            break;

        case 2:
        {
            pThis->cas = 0;
            switch (offPort)
            {
                case AC97_Reset:
#ifdef IN_RING3
                    ichac97R3Reset(pDevIns);
#else
                    rc = VINF_IOM_R3_IOPORT_WRITE;
#endif
                    break;
                case AC97_Powerdown_Ctrl_Stat:
                    u32 &= ~0xf;
                    u32 |= ichac97MixerGet(pThis, offPort) & 0xf;
                    ichac97MixerSet(pThis, offPort, u32);
                    break;
                case AC97_Master_Volume_Mute:
                    if (pThis->enmCodecModel == AC97CODEC_AD1980)
                    {
                        if (ichac97MixerGet(pThis, AC97_AD_Misc) & AC97_AD_MISC_LOSEL)
                            break; /* Register controls surround (rear), do nothing. */
                    }
#ifdef IN_RING3
                    ichac97R3MixerSetVolume(pThis, pThisCC, offPort, PDMAUDIOMIXERCTL_VOLUME_MASTER, u32);
#else
                    rc = VINF_IOM_R3_IOPORT_WRITE;
#endif
                    break;
                case AC97_Headphone_Volume_Mute:
                    if (pThis->enmCodecModel == AC97CODEC_AD1980)
                    {
                        if (ichac97MixerGet(pThis, AC97_AD_Misc) & AC97_AD_MISC_HPSEL)
                        {
                            /* Register controls PCM (front) outputs. */
#ifdef IN_RING3
                            ichac97R3MixerSetVolume(pThis, pThisCC, offPort, PDMAUDIOMIXERCTL_VOLUME_MASTER, u32);
#else
                            rc = VINF_IOM_R3_IOPORT_WRITE;
#endif
                        }
                    }
                    break;
                case AC97_PCM_Out_Volume_Mute:
#ifdef IN_RING3
                    ichac97R3MixerSetVolume(pThis, pThisCC, offPort, PDMAUDIOMIXERCTL_FRONT, u32);
#else
                    rc = VINF_IOM_R3_IOPORT_WRITE;
#endif
                    break;
                case AC97_Line_In_Volume_Mute:
#ifdef IN_RING3
                    ichac97R3MixerSetVolume(pThis, pThisCC, offPort, PDMAUDIOMIXERCTL_LINE_IN, u32);
#else
                    rc = VINF_IOM_R3_IOPORT_WRITE;
#endif
                    break;
                case AC97_Record_Select:
#ifdef IN_RING3
                    ichac97R3MixerRecordSelect(pThis, u32);
#else
                    rc = VINF_IOM_R3_IOPORT_WRITE;
#endif
                    break;
                case AC97_Record_Gain_Mute:
#ifdef IN_RING3
                    /* Newer Ubuntu guests rely on that when controlling gain and muting
                     * the recording (capturing) levels. */
                    ichac97R3MixerSetGain(pThis, pThisCC, offPort, PDMAUDIOMIXERCTL_LINE_IN, u32);
#else
                    rc = VINF_IOM_R3_IOPORT_WRITE;
#endif
                    break;
                case AC97_Record_Gain_Mic_Mute:
#ifdef IN_RING3
                    /* Ditto; see note above. */
                    ichac97R3MixerSetGain(pThis, pThisCC, offPort, PDMAUDIOMIXERCTL_MIC_IN,  u32);
#else
                    rc = VINF_IOM_R3_IOPORT_WRITE;
#endif
                    break;
                case AC97_Vendor_ID1:
                case AC97_Vendor_ID2:
                    LogFunc(("Attempt to write vendor ID to %#x\n", u32));
                    break;
                case AC97_Extended_Audio_ID:
                    LogFunc(("Attempt to write extended audio ID to %#x\n", u32));
                    break;
                case AC97_Extended_Audio_Ctrl_Stat:
#ifdef IN_RING3
                    /*
                     * Handle VRA bits.
                     */
                    if (!(u32 & AC97_EACS_VRA)) /* Check if VRA bit is not set. */
                    {
                        ichac97MixerSet(pThis, AC97_PCM_Front_DAC_Rate, 0xbb80); /* Set default (48000 Hz). */
                        /** @todo r=bird: Why reopen it now?  Can't we put that off till it's
                         *        actually used? */
                        ichac97R3StreamReSetUp(pDevIns, pThis, pThisCC, &pThis->aStreams[AC97SOUNDSOURCE_PO_INDEX],
                                              &pThisCC->aStreams[AC97SOUNDSOURCE_PO_INDEX], true /* fForce */);

                        ichac97MixerSet(pThis, AC97_PCM_LR_ADC_Rate, 0xbb80); /* Set default (48000 Hz). */
                        /** @todo r=bird: Why reopen it now?  Can't we put that off till it's
                         *        actually used? */
                        ichac97R3StreamReSetUp(pDevIns, pThis, pThisCC, &pThis->aStreams[AC97SOUNDSOURCE_PI_INDEX],
                                              &pThisCC->aStreams[AC97SOUNDSOURCE_PI_INDEX], true /* fForce */);
                    }
                    else
                        LogRel2(("AC97: Variable rate audio (VRA) is not supported\n"));

                    /*
                     * Handle VRM bits.
                     */
                    if (!(u32 & AC97_EACS_VRM)) /* Check if VRM bit is not set. */
                    {
                        ichac97MixerSet(pThis, AC97_MIC_ADC_Rate, 0xbb80); /* Set default (48000 Hz). */
                        /** @todo r=bird: Why reopen it now?  Can't we put that off till it's
                         *        actually used? */
                        ichac97R3StreamReSetUp(pDevIns, pThis, pThisCC, &pThis->aStreams[AC97SOUNDSOURCE_MC_INDEX],
                                              &pThisCC->aStreams[AC97SOUNDSOURCE_MC_INDEX], true /* fForce */);
                    }
                    else
                        LogRel2(("AC97: Variable rate microphone audio (VRM) is not supported\n"));

                    LogRel2(("AC97: Setting extended audio control to %#x\n", u32));
                    ichac97MixerSet(pThis, AC97_Extended_Audio_Ctrl_Stat, u32);
#else /* !IN_RING3 */
                    rc = VINF_IOM_R3_IOPORT_WRITE;
#endif
                    break;
                case AC97_PCM_Front_DAC_Rate: /* Output slots 3, 4, 6. */
#ifdef IN_RING3
                    if (ichac97MixerGet(pThis, AC97_Extended_Audio_Ctrl_Stat) & AC97_EACS_VRA)
                    {
                        LogRel2(("AC97: Setting front DAC rate to 0x%x\n", u32));
                        ichac97MixerSet(pThis, offPort, u32);
                        /** @todo r=bird: Why reopen it now?  Can't we put that off till it's
                         *        actually used? */
                        ichac97R3StreamReSetUp(pDevIns, pThis, pThisCC, &pThis->aStreams[AC97SOUNDSOURCE_PO_INDEX],
                                              &pThisCC->aStreams[AC97SOUNDSOURCE_PO_INDEX], true /* fForce */);
                    }
                    else
                        LogRel2(("AC97: Setting front DAC rate (0x%x) when VRA is not set is forbidden, ignoring\n", u32));
#else
                    rc = VINF_IOM_R3_IOPORT_WRITE;
#endif
                    break;
                case AC97_MIC_ADC_Rate: /* Input slot 6. */
#ifdef IN_RING3
                    if (ichac97MixerGet(pThis, AC97_Extended_Audio_Ctrl_Stat) & AC97_EACS_VRM)
                    {
                        LogRel2(("AC97: Setting microphone ADC rate to 0x%x\n", u32));
                        ichac97MixerSet(pThis, offPort, u32);
                        /** @todo r=bird: Why reopen it now?  Can't we put that off till it's
                         *        actually used? */
                        ichac97R3StreamReSetUp(pDevIns, pThis, pThisCC, &pThis->aStreams[AC97SOUNDSOURCE_MC_INDEX],
                                              &pThisCC->aStreams[AC97SOUNDSOURCE_MC_INDEX], true /* fForce */);
                    }
                    else
                        LogRel2(("AC97: Setting microphone ADC rate (0x%x) when VRM is not set is forbidden, ignoring\n", u32));
#else
                    rc = VINF_IOM_R3_IOPORT_WRITE;
#endif
                    break;
                case AC97_PCM_LR_ADC_Rate: /* Input slots 3, 4. */
#ifdef IN_RING3
                    if (ichac97MixerGet(pThis, AC97_Extended_Audio_Ctrl_Stat) & AC97_EACS_VRA)
                    {
                        LogRel2(("AC97: Setting line-in ADC rate to 0x%x\n", u32));
                        ichac97MixerSet(pThis, offPort, u32);
                        /** @todo r=bird: Why reopen it now?  Can't we put that off till it's
                         *        actually used? */
                        ichac97R3StreamReSetUp(pDevIns, pThis, pThisCC, &pThis->aStreams[AC97SOUNDSOURCE_PI_INDEX],
                                              &pThisCC->aStreams[AC97SOUNDSOURCE_PI_INDEX], true /* fForce */);
                    }
                    else
                        LogRel2(("AC97: Setting line-in ADC rate (0x%x) when VRA is not set is forbidden, ignoring\n", u32));
#else
                    rc = VINF_IOM_R3_IOPORT_WRITE;
#endif
                    break;
                default:
                    /* Most of these are to register we don't care about like AC97_CD_Volume_Mute
                       and AC97_Master_Volume_Mono_Mute or things we don't need to handle specially.
                       Thus this is not a 'warning' but an 'info log message. */
                    LogRel2(("AC97: Info: Unimplemented NAM write offPort=%#x <- %#x LB 2 (line " RT_XSTR(__LINE__) ")\n", offPort, u32));
                    STAM_REL_COUNTER_INC(&pThis->StatUnimplementedNamWrites);
                    ichac97MixerSet(pThis, offPort, u32);
                    break;
            }
            break;
        }

        case 4:
            LogRel2(("AC97: Warning: Unimplemented NAM write offPort=%#x <- %#x LB 4 (line " RT_XSTR(__LINE__) ")\n", offPort, u32));
            STAM_REL_COUNTER_INC(&pThis->StatUnimplementedNamWrites);
            pThis->cas = 0;
            break;

        default:
            AssertMsgFailed(("Unhandled NAM write offPort=%#x, cb=%u u32=%#x\n", offPort, cb, u32));
            break;
    }

    DEVAC97_UNLOCK(pDevIns, pThis);
    return rc;
}

#ifdef IN_RING3


/*********************************************************************************************************************************
*   State Saving & Loading                                                                                                       *
*********************************************************************************************************************************/

/**
 * Saves (serializes) an AC'97 stream using SSM.
 *
 * @param   pDevIns             Device instance.
 * @param   pSSM                Saved state manager (SSM) handle to use.
 * @param   pStream             AC'97 stream to save.
 */
static void ichac97R3SaveStream(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, PAC97STREAM pStream)
{
    PCPDMDEVHLPR3 pHlp  = pDevIns->pHlpR3;

    pHlp->pfnSSMPutU32(pSSM, pStream->Regs.bdbar);
    pHlp->pfnSSMPutU8( pSSM, pStream->Regs.civ);
    pHlp->pfnSSMPutU8( pSSM, pStream->Regs.lvi);
    pHlp->pfnSSMPutU16(pSSM, pStream->Regs.sr);
    pHlp->pfnSSMPutU16(pSSM, pStream->Regs.picb);
    pHlp->pfnSSMPutU8( pSSM, pStream->Regs.piv);
    pHlp->pfnSSMPutU8( pSSM, pStream->Regs.cr);
    pHlp->pfnSSMPutS32(pSSM, pStream->Regs.bd_valid);
    pHlp->pfnSSMPutU32(pSSM, pStream->Regs.bd.addr);
    pHlp->pfnSSMPutU32(pSSM, pStream->Regs.bd.ctl_len);
}


/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) ichac97R3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PAC97STATE      pThis   = PDMDEVINS_2_DATA(pDevIns, PAC97STATE);
    PAC97STATER3    pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PAC97STATER3);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    LogFlowFuncEnter();

    pHlp->pfnSSMPutU32(pSSM, pThis->glob_cnt);
    pHlp->pfnSSMPutU32(pSSM, pThis->glob_sta);
    pHlp->pfnSSMPutU32(pSSM, pThis->cas);

    /*
     * The order that the streams are saved here is fixed, so don't change.
     */
    /** @todo r=andy For the next saved state version, add unique stream identifiers and a stream count. */
    for (unsigned i = 0; i < AC97_MAX_STREAMS; i++)
        ichac97R3SaveStream(pDevIns, pSSM, &pThis->aStreams[i]);

    pHlp->pfnSSMPutMem(pSSM, pThis->mixer_data, sizeof(pThis->mixer_data));

    /* The stream order is against fixed and set in stone. */
    uint8_t afActiveStrms[AC97SOUNDSOURCE_MAX];
    afActiveStrms[AC97SOUNDSOURCE_PI_INDEX] = ichac97R3StreamIsEnabled(pThisCC, &pThis->aStreams[AC97SOUNDSOURCE_PI_INDEX]);
    afActiveStrms[AC97SOUNDSOURCE_PO_INDEX] = ichac97R3StreamIsEnabled(pThisCC, &pThis->aStreams[AC97SOUNDSOURCE_PO_INDEX]);
    afActiveStrms[AC97SOUNDSOURCE_MC_INDEX] = ichac97R3StreamIsEnabled(pThisCC, &pThis->aStreams[AC97SOUNDSOURCE_MC_INDEX]);
    AssertCompile(RT_ELEMENTS(afActiveStrms) == 3);
    pHlp->pfnSSMPutMem(pSSM, afActiveStrms, sizeof(afActiveStrms));

    LogFlowFuncLeaveRC(VINF_SUCCESS);
    return VINF_SUCCESS;
}


/**
 * Loads an AC'97 stream from SSM.
 *
 * @returns VBox status code.
 * @param   pDevIns             The device instance.
 * @param   pSSM                Saved state manager (SSM) handle to use.
 * @param   pStream             AC'97 stream to load.
 */
static int ichac97R3LoadStream(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, PAC97STREAM pStream)
{
    PCPDMDEVHLPR3 pHlp  = pDevIns->pHlpR3;

    pHlp->pfnSSMGetU32(pSSM, &pStream->Regs.bdbar);
    pHlp->pfnSSMGetU8( pSSM, &pStream->Regs.civ);
    pHlp->pfnSSMGetU8( pSSM, &pStream->Regs.lvi);
    pHlp->pfnSSMGetU16(pSSM, &pStream->Regs.sr);
    pHlp->pfnSSMGetU16(pSSM, &pStream->Regs.picb);
    pHlp->pfnSSMGetU8( pSSM, &pStream->Regs.piv);
    pHlp->pfnSSMGetU8( pSSM, &pStream->Regs.cr);
    pHlp->pfnSSMGetS32(pSSM, &pStream->Regs.bd_valid);
    pHlp->pfnSSMGetU32(pSSM, &pStream->Regs.bd.addr);
    return pHlp->pfnSSMGetU32(pSSM, &pStream->Regs.bd.ctl_len);
}


/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) ichac97R3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PAC97STATE    pThis   = PDMDEVINS_2_DATA(pDevIns, PAC97STATE);
    PAC97STATER3  pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PAC97STATER3);
    PCPDMDEVHLPR3 pHlp    = pDevIns->pHlpR3;

    LogRel2(("ichac97LoadExec: uVersion=%RU32, uPass=0x%x\n", uVersion, uPass));

    AssertMsgReturn (uVersion == AC97_SAVED_STATE_VERSION, ("%RU32\n", uVersion), VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION);
    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);

    pHlp->pfnSSMGetU32(pSSM, &pThis->glob_cnt);
    pHlp->pfnSSMGetU32(pSSM, &pThis->glob_sta);
    pHlp->pfnSSMGetU32(pSSM, &pThis->cas);

    /*
     * The order the streams are loaded here is critical (defined by
     * AC97SOUNDSOURCE_XX_INDEX), so don't touch!
     */
    for (unsigned i = 0; i < AC97_MAX_STREAMS; i++)
    {
        int rc = ichac97R3LoadStream(pDevIns, pSSM, &pThis->aStreams[i]);
        AssertRCReturn(rc, rc);
    }

    pHlp->pfnSSMGetMem(pSSM, pThis->mixer_data, sizeof(pThis->mixer_data));

    ichac97R3MixerRecordSelect(pThis, ichac97MixerGet(pThis, AC97_Record_Select));
    ichac97R3MixerSetVolume(pThis, pThisCC, AC97_Master_Volume_Mute,    PDMAUDIOMIXERCTL_VOLUME_MASTER,
                            ichac97MixerGet(pThis, AC97_Master_Volume_Mute));
    ichac97R3MixerSetVolume(pThis, pThisCC, AC97_PCM_Out_Volume_Mute,   PDMAUDIOMIXERCTL_FRONT,
                            ichac97MixerGet(pThis, AC97_PCM_Out_Volume_Mute));
    ichac97R3MixerSetVolume(pThis, pThisCC, AC97_Line_In_Volume_Mute,   PDMAUDIOMIXERCTL_LINE_IN,
                            ichac97MixerGet(pThis, AC97_Line_In_Volume_Mute));
    ichac97R3MixerSetVolume(pThis, pThisCC, AC97_Mic_Volume_Mute,       PDMAUDIOMIXERCTL_MIC_IN,
                            ichac97MixerGet(pThis, AC97_Mic_Volume_Mute));
    ichac97R3MixerSetGain(pThis, pThisCC, AC97_Record_Gain_Mic_Mute,    PDMAUDIOMIXERCTL_MIC_IN,
                          ichac97MixerGet(pThis, AC97_Record_Gain_Mic_Mute));
    ichac97R3MixerSetGain(pThis, pThisCC, AC97_Record_Gain_Mute,        PDMAUDIOMIXERCTL_LINE_IN,
                          ichac97MixerGet(pThis, AC97_Record_Gain_Mute));
    if (pThis->enmCodecModel == AC97CODEC_AD1980)
        if (ichac97MixerGet(pThis, AC97_AD_Misc) & AC97_AD_MISC_HPSEL)
            ichac97R3MixerSetVolume(pThis, pThisCC, AC97_Headphone_Volume_Mute, PDMAUDIOMIXERCTL_VOLUME_MASTER,
                                    ichac97MixerGet(pThis, AC97_Headphone_Volume_Mute));

    /*
     * Again the stream order is set is stone.
     */
    uint8_t afActiveStrms[AC97SOUNDSOURCE_MAX];
    int rc = pHlp->pfnSSMGetMem(pSSM, afActiveStrms, sizeof(afActiveStrms));
    AssertRCReturn(rc, rc);

    for (unsigned i = 0; i < AC97_MAX_STREAMS; i++)
    {
        const bool          fEnable   = RT_BOOL(afActiveStrms[i]);
        const PAC97STREAM   pStream   = &pThis->aStreams[i];
        const PAC97STREAMR3 pStreamCC = &pThisCC->aStreams[i];

        rc = ichac97R3StreamEnable(pDevIns, pThis, pThisCC, pStream, pStreamCC, fEnable);
        AssertRC(rc);
        if (   fEnable
            && RT_SUCCESS(rc))
        {
            /*
             * We need to make sure to update the stream's next transfer (if any) when
             * restoring from a saved state.
             *
             * Otherwise pStream->cDmaPeriodTicks always will be 0 and thus streams won't
             * resume when running while the saved state has been taken.
             *
             * Also see oem2ticketref:52.
             */
            ichac97R3StreamTransferUpdate(pDevIns, pStream, pStreamCC);

            /* Re-arm the timer for this stream. */
            /** @todo r=aeichner This causes a VM hang upon saved state resume when NetBSD is used as a guest
             * Stopping the timer if cDmaPeriodTicks is 0 is a workaround but needs further investigation,
             * see @bugref{9759} for more information. */
            if (pStream->cDmaPeriodTicks)
                ichac97R3TimerSet(pDevIns, pStream, pStream->cDmaPeriodTicks);
            else
                PDMDevHlpTimerStop(pDevIns, pStream->hTimer);
        }

        /* Keep going. */
    }

    pThis->bup_flag  = 0;
    pThis->last_samp = 0;

    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   Debug Info Items                                                                                                             *
*********************************************************************************************************************************/

/** Used by ichac97R3DbgInfoStream and ichac97R3DbgInfoBDL. */
static int ichac97R3DbgLookupStrmIdx(PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    if (pszArgs && *pszArgs)
    {
        int32_t idxStream;
        int rc = RTStrToInt32Full(pszArgs, 0, &idxStream);
        if (RT_SUCCESS(rc) && idxStream >= -1 && idxStream < AC97_MAX_STREAMS)
            return idxStream;
        pHlp->pfnPrintf(pHlp, "Argument '%s' is not a valid stream number!\n", pszArgs);
    }
    return -1;
}


/**
 * Generic buffer descriptor list dumper.
 */
static void ichac97R3DbgPrintBdl(PPDMDEVINS pDevIns, PAC97STATE pThis, PAC97STREAM pStream,
                                 PCDBGFINFOHLP pHlp, const char *pszPrefix)
{
    uint8_t const bLvi = pStream->Regs.lvi;
    uint8_t const bCiv = pStream->Regs.civ;
    pHlp->pfnPrintf(pHlp, "%sBDL for stream #%u: @ %#RX32 LB 0x100; CIV=%#04x LVI=%#04x:\n",
                    pszPrefix, pStream->u8SD, pStream->Regs.bdbar, bCiv, bLvi);
    if (pStream->Regs.bdbar != 0)
    {
        /* Read all in one go. */
        AC97BDLE aBdl[AC97_MAX_BDLE];
        RT_ZERO(aBdl);
        PDMDevHlpPCIPhysRead(pDevIns, pStream->Regs.bdbar, aBdl, sizeof(aBdl));

        /* Get the audio props for the stream so we can translate the sizes correctly. */
        PDMAUDIOPCMPROPS Props;
        ichach97R3CalcStreamProps(pThis, pStream->u8SD, &Props);

        /* Dump them. */
        uint64_t cbTotal = 0;
        uint64_t cbValid = 0;
        for (unsigned i = 0; i < RT_ELEMENTS(aBdl); i++)
        {
            aBdl[i].addr    = RT_LE2H_U32(aBdl[i].addr);
            aBdl[i].ctl_len = RT_LE2H_U32(aBdl[i].ctl_len);

            bool const fValid = bCiv <= bLvi
                              ? i >= bCiv && i <= bLvi
                              : i >= bCiv || i <= bLvi;

            uint32_t const cb = (aBdl[i].ctl_len & AC97_BD_LEN_MASK) * PDMAudioPropsSampleSize(&Props); /** @todo or frame size? OSDev says frame... */
            cbTotal += cb;
            if (fValid)
                cbValid += cb;

            char szFlags[64];
            szFlags[0] = '\0';
            if (aBdl[i].ctl_len & ~(AC97_BD_LEN_MASK | AC97_BD_IOC | AC97_BD_BUP))
                RTStrPrintf(szFlags, sizeof(szFlags), " !!fFlags=%#x!!\n", aBdl[i].ctl_len & ~AC97_BD_LEN_MASK);

            pHlp->pfnPrintf(pHlp, "%s %cBDLE%02u: %#010RX32 L %#06x / LB %#RX32 / %RU64ms%s%s%s%s\n",
                            pszPrefix, fValid ? ' ' : '?', i, aBdl[i].addr,
                            aBdl[i].ctl_len & AC97_BD_LEN_MASK, cb, PDMAudioPropsBytesToMilli(&Props, cb),
                            aBdl[i].ctl_len & AC97_BD_IOC ?  " ioc" : "",
                            aBdl[i].ctl_len & AC97_BD_BUP ?  " bup" : "",
                            szFlags, !(aBdl[i].addr & 3) ? "" : " !!Addr!!");
        }

        pHlp->pfnPrintf(pHlp, "%sTotal: %#RX64 bytes (%RU64), %RU64 ms;  Valid: %#RX64 bytes (%RU64), %RU64 ms\n", pszPrefix,
                        cbTotal, cbTotal, PDMAudioPropsBytesToMilli(&Props, cbTotal),
                        cbValid, cbValid, PDMAudioPropsBytesToMilli(&Props, cbValid) );
    }
}


/**
 * @callback_method_impl{FNDBGFHANDLERDEV, ac97bdl}
 */
static DECLCALLBACK(void) ichac97R3DbgInfoBDL(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PAC97STATE  pThis     = PDMDEVINS_2_DATA(pDevIns, PAC97STATE);
    int         idxStream = ichac97R3DbgLookupStrmIdx(pHlp, pszArgs);
    if (idxStream != -1)
        ichac97R3DbgPrintBdl(pDevIns, pThis, &pThis->aStreams[idxStream], pHlp, "");
    else
        for (idxStream = 0; idxStream < AC97_MAX_STREAMS; ++idxStream)
            ichac97R3DbgPrintBdl(pDevIns, pThis, &pThis->aStreams[idxStream], pHlp, "");
}


/** Worker for ichac97R3DbgInfoStream.    */
static void ichac97R3DbgPrintStream(PCDBGFINFOHLP pHlp, PAC97STREAM pStream, PAC97STREAMR3 pStreamR3)
{
    char szTmp[PDMAUDIOSTRMCFGTOSTRING_MAX];
    pHlp->pfnPrintf(pHlp, "Stream #%d: %s\n", pStream->u8SD,
                    PDMAudioStrmCfgToString(&pStreamR3->State.Cfg, szTmp, sizeof(szTmp)));
    pHlp->pfnPrintf(pHlp, "  BDBAR   %#010RX32\n", pStream->Regs.bdbar);
    pHlp->pfnPrintf(pHlp, "  CIV     %#04RX8\n", pStream->Regs.civ);
    pHlp->pfnPrintf(pHlp, "  LVI     %#04RX8\n", pStream->Regs.lvi);
    pHlp->pfnPrintf(pHlp, "  SR      %#06RX16\n", pStream->Regs.sr);
    pHlp->pfnPrintf(pHlp, "  PICB    %#06RX16\n", pStream->Regs.picb);
    pHlp->pfnPrintf(pHlp, "  PIV     %#04RX8\n", pStream->Regs.piv);
    pHlp->pfnPrintf(pHlp, "  CR      %#04RX8\n", pStream->Regs.cr);
    if (pStream->Regs.bd_valid)
    {
        pHlp->pfnPrintf(pHlp, "  BD.ADDR %#010RX32\n", pStream->Regs.bd.addr);
        pHlp->pfnPrintf(pHlp, "  BD.LEN  %#04RX16\n", (uint16_t)pStream->Regs.bd.ctl_len);
        pHlp->pfnPrintf(pHlp, "  BD.CTL  %#04RX16\n", (uint16_t)(pStream->Regs.bd.ctl_len >> 16));
    }

    pHlp->pfnPrintf(pHlp, "  offRead            %#RX64\n", pStreamR3->State.offRead);
    pHlp->pfnPrintf(pHlp, "  offWrite           %#RX64\n", pStreamR3->State.offWrite);
    pHlp->pfnPrintf(pHlp, "  uTimerHz           %RU16\n", pStreamR3->State.uTimerHz);
    pHlp->pfnPrintf(pHlp, "  cDmaPeriodTicks    %RU64\n", pStream->cDmaPeriodTicks);
    pHlp->pfnPrintf(pHlp, "  cbDmaPeriod        %#RX32\n", pStream->cbDmaPeriod);
}


/**
 * @callback_method_impl{FNDBGFHANDLERDEV, ac97stream}
 */
static DECLCALLBACK(void) ichac97R3DbgInfoStream(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PAC97STATE   pThis     = PDMDEVINS_2_DATA(pDevIns, PAC97STATE);
    PAC97STATER3 pThisCC   = PDMDEVINS_2_DATA_CC(pDevIns, PAC97STATER3);
    int          idxStream = ichac97R3DbgLookupStrmIdx(pHlp, pszArgs);
    if (idxStream != -1)
        ichac97R3DbgPrintStream(pHlp, &pThis->aStreams[idxStream], &pThisCC->aStreams[idxStream]);
    else
        for (idxStream = 0; idxStream < AC97_MAX_STREAMS; ++idxStream)
            ichac97R3DbgPrintStream(pHlp, &pThis->aStreams[idxStream], &pThisCC->aStreams[idxStream]);
}


/**
 * @callback_method_impl{FNDBGFHANDLERDEV, ac97mixer}
 */
static DECLCALLBACK(void) ichac97R3DbgInfoMixer(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PAC97STATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PAC97STATER3);
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
static DECLCALLBACK(void *) ichac97R3QueryInterface(struct PDMIBASE *pInterface, const char *pszIID)
{
    PAC97STATER3 pThisCC = RT_FROM_MEMBER(pInterface, AC97STATER3, IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThisCC->IBase);
    return NULL;
}


/*********************************************************************************************************************************
*   PDMDEVREG                                                                                                                    *
*********************************************************************************************************************************/

/**
 * Destroys all AC'97 audio streams of the device.
 *
 * @param   pDevIns     The device AC'97 instance.
 * @param   pThis       The shared AC'97 state.
 * @param   pThisCC     The ring-3 AC'97 state.
 */
static void ichac97R3StreamsDestroy(PPDMDEVINS pDevIns, PAC97STATE pThis, PAC97STATER3 pThisCC)
{
    LogFlowFuncEnter();

    /*
     * Destroy all AC'97 streams.
     */
    for (unsigned i = 0; i < AC97_MAX_STREAMS; i++)
        ichac97R3StreamDestroy(pThisCC, &pThis->aStreams[i], &pThisCC->aStreams[i]);

    /*
     * Destroy all sinks.
     */
    if (pThisCC->pSinkLineIn)
    {
        ichac97R3MixerRemoveDrvStreams(pDevIns, pThisCC, pThisCC->pSinkLineIn, PDMAUDIODIR_IN, PDMAUDIOPATH_IN_LINE);

        AudioMixerSinkDestroy(pThisCC->pSinkLineIn, pDevIns);
        pThisCC->pSinkLineIn = NULL;
    }

    if (pThisCC->pSinkMicIn)
    {
        ichac97R3MixerRemoveDrvStreams(pDevIns, pThisCC, pThisCC->pSinkMicIn, PDMAUDIODIR_IN, PDMAUDIOPATH_IN_MIC);

        AudioMixerSinkDestroy(pThisCC->pSinkMicIn, pDevIns);
        pThisCC->pSinkMicIn = NULL;
    }

    if (pThisCC->pSinkOut)
    {
        ichac97R3MixerRemoveDrvStreams(pDevIns, pThisCC, pThisCC->pSinkOut, PDMAUDIODIR_OUT, PDMAUDIOPATH_OUT_FRONT);

        AudioMixerSinkDestroy(pThisCC->pSinkOut, pDevIns);
        pThisCC->pSinkOut = NULL;
    }
}


/**
 * Powers off the device.
 *
 * @param   pDevIns             Device instance to power off.
 */
static DECLCALLBACK(void) ichac97R3PowerOff(PPDMDEVINS pDevIns)
{
    PAC97STATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PAC97STATE);
    PAC97STATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PAC97STATER3);

    LogRel2(("AC97: Powering off ...\n"));

    /* Note: Involves mixer stream / sink destruction, so also do this here
     *       instead of in ichac97R3Destruct(). */
    ichac97R3StreamsDestroy(pDevIns, pThis, pThisCC);

    /*
     * Note: Destroy the mixer while powering off and *not* in ichac97R3Destruct,
     *       giving the mixer the chance to release any references held to
     *       PDM audio streams it maintains.
     */
    if (pThisCC->pMixer)
    {
        AudioMixerDestroy(pThisCC->pMixer, pDevIns);
        pThisCC->pMixer = NULL;
    }
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 *
 * @remarks The original sources didn't install a reset handler, but it seems to
 *          make sense to me so we'll do it.
 */
static DECLCALLBACK(void) ichac97R3Reset(PPDMDEVINS pDevIns)
{
    PAC97STATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PAC97STATE);
    PAC97STATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PAC97STATER3);

    LogRel(("AC97: Reset\n"));

    /*
     * Reset the mixer too. The Windows XP driver seems to rely on
     * this. At least it wants to read the vendor id before it resets
     * the codec manually.
     */
    ichac97R3MixerReset(pThis, pThisCC);

    /*
     * Reset all streams.
     */
    for (unsigned i = 0; i < AC97_MAX_STREAMS; i++)
    {
        ichac97R3StreamEnable(pDevIns, pThis, pThisCC, &pThis->aStreams[i], &pThisCC->aStreams[i], false /* fEnable */);
        ichac97R3StreamReset(pThis, &pThis->aStreams[i], &pThisCC->aStreams[i]);
    }

    /*
     * Reset mixer sinks.
     *
     * Do the reset here instead of in ichac97R3StreamReset();
     * the mixer sink(s) might still have data to be processed when an audio stream gets reset.
     */
    AudioMixerSinkReset(pThisCC->pSinkLineIn);
    AudioMixerSinkReset(pThisCC->pSinkMicIn);
    AudioMixerSinkReset(pThisCC->pSinkOut);
}


/**
 * Adds a specific AC'97 driver to the driver chain.
 *
 * Only called from ichac97R3Attach().
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThisCC     The ring-3 AC'97 device state.
 * @param   pDrv        The AC'97 driver to add.
 */
static int ichac97R3MixerAddDrv(PPDMDEVINS pDevIns, PAC97STATER3 pThisCC, PAC97DRIVER pDrv)
{
    int rc = VINF_SUCCESS;

    if (AudioHlpStreamCfgIsValid(&pThisCC->aStreams[AC97SOUNDSOURCE_PI_INDEX].State.Cfg))
        rc = ichac97R3MixerAddDrvStream(pDevIns, pThisCC->pSinkLineIn,
                                        &pThisCC->aStreams[AC97SOUNDSOURCE_PI_INDEX].State.Cfg, pDrv);

    if (AudioHlpStreamCfgIsValid(&pThisCC->aStreams[AC97SOUNDSOURCE_PO_INDEX].State.Cfg))
    {
        int rc2 = ichac97R3MixerAddDrvStream(pDevIns, pThisCC->pSinkOut,
                                             &pThisCC->aStreams[AC97SOUNDSOURCE_PO_INDEX].State.Cfg, pDrv);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    if (AudioHlpStreamCfgIsValid(&pThisCC->aStreams[AC97SOUNDSOURCE_MC_INDEX].State.Cfg))
    {
        int rc2 = ichac97R3MixerAddDrvStream(pDevIns, pThisCC->pSinkMicIn,
                                             &pThisCC->aStreams[AC97SOUNDSOURCE_MC_INDEX].State.Cfg, pDrv);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    return rc;
}


/**
 * Worker for ichac97R3Construct() and ichac97R3Attach().
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThisCC     The ring-3 AC'97 device state.
 * @param   uLUN        The logical unit which is being attached.
 * @param   ppDrv       Attached driver instance on success. Optional.
 */
static int ichac97R3AttachInternal(PPDMDEVINS pDevIns, PAC97STATER3 pThisCC, unsigned uLUN, PAC97DRIVER *ppDrv)
{
    /*
     * Allocate a new driver structure and try attach the driver.
     */
    PAC97DRIVER pDrv = (PAC97DRIVER)RTMemAllocZ(sizeof(AC97DRIVER));
    AssertPtrReturn(pDrv, VERR_NO_MEMORY);
    RTStrPrintf(pDrv->szDesc, sizeof(pDrv->szDesc), "Audio driver port (AC'97) for LUN #%u", uLUN);

    PPDMIBASE pDrvBase;
    int rc = PDMDevHlpDriverAttach(pDevIns, uLUN, &pThisCC->IBase, &pDrvBase, pDrv->szDesc);
    if (RT_SUCCESS(rc))
    {
        pDrv->pConnector = PDMIBASE_QUERY_INTERFACE(pDrvBase, PDMIAUDIOCONNECTOR);
        AssertPtr(pDrv->pConnector);
        if (RT_VALID_PTR(pDrv->pConnector))
        {
            pDrv->pDrvBase   = pDrvBase;
            pDrv->uLUN       = uLUN;

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
             */
            if (   pDrv->pConnector
                && pDrv->pConnector->pfnStreamConfigHint)
            {
                /* 48kHz */
                PDMAUDIOSTREAMCFG Cfg;
                RT_ZERO(Cfg);
                Cfg.enmDir                        = PDMAUDIODIR_OUT;
                Cfg.enmPath                       = PDMAUDIOPATH_OUT_FRONT;
                Cfg.Device.cMsSchedulingHint      = 5;
                Cfg.Backend.cFramesPreBuffering   = UINT32_MAX;
                PDMAudioPropsInit(&Cfg.Props, 2, true /*fSigned*/, 2, 48000);
                RTStrPrintf(Cfg.szName, sizeof(Cfg.szName), "output 48kHz 2ch S16 (HDA config hint)");

                pDrv->pConnector->pfnStreamConfigHint(pDrv->pConnector, &Cfg); /* (may trash CfgReq) */
# if 0
                /* 44.1kHz */
                RT_ZERO(Cfg);
                Cfg.enmDir                        = PDMAUDIODIR_OUT;
                Cfg.enmPath                       = PDMAUDIOPATH_OUT_FRONT;
                Cfg.Device.cMsSchedulingHint      = 10;
                Cfg.Backend.cFramesPreBuffering   = UINT32_MAX;
                PDMAudioPropsInit(&Cfg.Props, 2, true /*fSigned*/, 2, 44100);
                RTStrPrintf(Cfg.szName, sizeof(Cfg.szName), "output 44.1kHz 2ch S16 (HDA config hint)");

                pDrv->pConnector->pfnStreamConfigHint(pDrv->pConnector, &Cfg); /* (may trash CfgReq) */
# endif
            }

            LogFunc(("LUN#%u: returns VINF_SUCCESS (pCon=%p)\n", uLUN, pDrv->pConnector));
            return VINF_SUCCESS;
        }

        rc = VERR_PDM_MISSING_INTERFACE_BELOW;
    }
    else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
        LogFunc(("No attached driver for LUN #%u\n", uLUN));
    else
        LogFunc(("Attached driver for LUN #%u failed: %Rrc\n", uLUN, rc));
    RTMemFree(pDrv);

    LogFunc(("LUN#%u: rc=%Rrc\n", uLUN, rc));
    return rc;
}


/**
 * @interface_method_impl{PDMDEVREGR3,pfnAttach}
 */
static DECLCALLBACK(int) ichac97R3Attach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PAC97STATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PAC97STATE);
    PAC97STATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PAC97STATER3);
    RT_NOREF(fFlags);
    LogFunc(("iLUN=%u, fFlags=%#x\n", iLUN, fFlags));

    DEVAC97_LOCK(pDevIns, pThis);

    PAC97DRIVER pDrv;
    int rc = ichac97R3AttachInternal(pDevIns, pThisCC, iLUN, &pDrv);
    if (RT_SUCCESS(rc))
    {
        int rc2 = ichac97R3MixerAddDrv(pDevIns, pThisCC, pDrv);
        if (RT_FAILURE(rc2))
            LogFunc(("ichac97R3MixerAddDrv failed with %Rrc (ignored)\n", rc2));
    }

    DEVAC97_UNLOCK(pDevIns, pThis);

    return rc;
}


/**
 * Removes a specific AC'97 driver from the driver chain and destroys its
 * associated streams.
 *
 * Only called from ichac97R3Detach().
 *
 * @param   pDevIns     The device instance.
 * @param   pThisCC     The ring-3 AC'97 device state.
 * @param   pDrv        AC'97 driver to remove.
 */
static void ichac97R3MixerRemoveDrv(PPDMDEVINS pDevIns, PAC97STATER3 pThisCC, PAC97DRIVER pDrv)
{
    if (pDrv->MicIn.pMixStrm)
    {
        AudioMixerSinkRemoveStream(pThisCC->pSinkMicIn,  pDrv->MicIn.pMixStrm);
        AudioMixerStreamDestroy(pDrv->MicIn.pMixStrm, pDevIns, true /*fImmediate*/);
        pDrv->MicIn.pMixStrm = NULL;
    }

    if (pDrv->LineIn.pMixStrm)
    {
        AudioMixerSinkRemoveStream(pThisCC->pSinkLineIn, pDrv->LineIn.pMixStrm);
        AudioMixerStreamDestroy(pDrv->LineIn.pMixStrm, pDevIns, true /*fImmediate*/);
        pDrv->LineIn.pMixStrm = NULL;
    }

    if (pDrv->Out.pMixStrm)
    {
        AudioMixerSinkRemoveStream(pThisCC->pSinkOut,    pDrv->Out.pMixStrm);
        AudioMixerStreamDestroy(pDrv->Out.pMixStrm, pDevIns, true /*fImmediate*/);
        pDrv->Out.pMixStrm = NULL;
    }

    RTListNodeRemove(&pDrv->Node);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDetach}
 */
static DECLCALLBACK(void) ichac97R3Detach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PAC97STATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PAC97STATE);
    PAC97STATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PAC97STATER3);
    RT_NOREF(fFlags);

    LogFunc(("iLUN=%u, fFlags=0x%x\n", iLUN, fFlags));

    DEVAC97_LOCK(pDevIns, pThis);

    PAC97DRIVER pDrv;
    RTListForEach(&pThisCC->lstDrv, pDrv, AC97DRIVER, Node)
    {
        if (pDrv->uLUN == iLUN)
        {
            /* Remove the driver from our list and destory it's associated streams.
               This also will un-set the driver as a recording source (if associated). */
            ichac97R3MixerRemoveDrv(pDevIns, pThisCC, pDrv);
            LogFunc(("Detached LUN#%u\n", pDrv->uLUN));

            DEVAC97_UNLOCK(pDevIns, pThis);

            RTMemFree(pDrv);
            return;
        }
    }

    DEVAC97_UNLOCK(pDevIns, pThis);
    LogFunc(("LUN#%u was not found\n", iLUN));
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) ichac97R3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns); /* this shall come first */
    PAC97STATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PAC97STATER3);

    LogFlowFuncEnter();

    PAC97DRIVER pDrv, pDrvNext;
    RTListForEachSafe(&pThisCC->lstDrv, pDrv, pDrvNext, AC97DRIVER, Node)
    {
        RTListNodeRemove(&pDrv->Node);
        RTMemFree(pDrv);
    }

    /* Sanity. */
    Assert(RTListIsEmpty(&pThisCC->lstDrv));

    /* We don't always go via PowerOff, so make sure the mixer is destroyed. */
    if (pThisCC->pMixer)
    {
        AudioMixerDestroy(pThisCC->pMixer, pDevIns);
        pThisCC->pMixer = NULL;
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) ichac97R3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns); /* this shall come first */
    PAC97STATE      pThis   = PDMDEVINS_2_DATA(pDevIns, PAC97STATE);
    PAC97STATER3    pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PAC97STATER3);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    Assert(iInstance == 0); RT_NOREF(iInstance);

    /*
     * Initialize data so we can run the destructor without scewing up.
     */
    pThisCC->pDevIns                  = pDevIns;
    pThisCC->IBase.pfnQueryInterface  = ichac97R3QueryInterface;
    RTListInit(&pThisCC->lstDrv);

    /*
     * Validate and read configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "BufSizeInMs|BufSizeOutMs|Codec|TimerHz|DebugEnabled|DebugPathOut", "");

    /** @devcfgm{ac97,BufSizeInMs,uint16_t,0,2000,0,ms}
     * The size of the DMA buffer for input streams expressed in milliseconds. */
    int rc = pHlp->pfnCFGMQueryU16Def(pCfg, "BufSizeInMs", &pThis->cMsCircBufIn, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("AC97 configuration error: failed to read 'BufSizeInMs' as 16-bit unsigned integer"));
    if (pThis->cMsCircBufIn > 2000)
        return PDMDEV_SET_ERROR(pDevIns, VERR_OUT_OF_RANGE,
                                N_("AC97 configuration error: 'BufSizeInMs' is out of bound, max 2000 ms"));

    /** @devcfgm{ac97,BufSizeOutMs,uint16_t,0,2000,0,ms}
     * The size of the DMA buffer for output streams expressed in milliseconds. */
    rc = pHlp->pfnCFGMQueryU16Def(pCfg, "BufSizeOutMs", &pThis->cMsCircBufOut, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("AC97 configuration error: failed to read 'BufSizeOutMs' as 16-bit unsigned integer"));
    if (pThis->cMsCircBufOut > 2000)
        return PDMDEV_SET_ERROR(pDevIns, VERR_OUT_OF_RANGE,
                                N_("AC97 configuration error: 'BufSizeOutMs' is out of bound, max 2000 ms"));

    /** @devcfgm{ac97,TimerHz,uint16_t,10,1000,100,ms}
     * Currently the approximate rate at which the asynchronous I/O threads move
     * data from/to the DMA buffer, thru the mixer and drivers stack, and
     * to/from the host device/whatever.  (It does NOT govern any DMA timer rate any
     * more as might be hinted at by the name.) */
    rc = pHlp->pfnCFGMQueryU16Def(pCfg, "TimerHz", &pThis->uTimerHz, AC97_TIMER_HZ_DEFAULT);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("AC'97 configuration error: failed to read 'TimerHz' as a 16-bit unsigned integer"));
    if (pThis->uTimerHz < 10 || pThis->uTimerHz > 1000)
        return PDMDEV_SET_ERROR(pDevIns, VERR_OUT_OF_RANGE,
                                N_("AC'97 configuration error: 'TimerHz' is out of range (10-1000 Hz)"));

    if (pThis->uTimerHz != AC97_TIMER_HZ_DEFAULT)
        LogRel(("AC97: Using custom device timer rate: %RU16 Hz\n", pThis->uTimerHz));

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "DebugEnabled", &pThisCC->Dbg.fEnabled, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("AC97 configuration error: failed to read debugging enabled flag as boolean"));

    rc = pHlp->pfnCFGMQueryStringAllocDef(pCfg, "DebugPathOut", &pThisCC->Dbg.pszOutPath, NULL);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("AC97 configuration error: failed to read debugging output path flag as string"));

    if (pThisCC->Dbg.fEnabled)
        LogRel2(("AC97: Debug output will be saved to '%s'\n", pThisCC->Dbg.pszOutPath));

    /*
     * The AD1980 codec (with corresponding PCI subsystem vendor ID) is whitelisted
     * in the Linux kernel; Linux makes no attempt to measure the data rate and assumes
     * 48 kHz rate, which is exactly what we need. Same goes for AD1981B.
     */
    char szCodec[20];
    rc = pHlp->pfnCFGMQueryStringDef(pCfg, "Codec", &szCodec[0], sizeof(szCodec), "STAC9700");
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, VERR_PDM_DEVINS_UNKNOWN_CFG_VALUES,
                                N_("AC'97 configuration error: Querying \"Codec\" as string failed"));
    if (!strcmp(szCodec, "STAC9700"))
        pThis->enmCodecModel = AC97CODEC_STAC9700;
    else if (!strcmp(szCodec, "AD1980"))
        pThis->enmCodecModel = AC97CODEC_AD1980;
    else if (!strcmp(szCodec, "AD1981B"))
        pThis->enmCodecModel = AC97CODEC_AD1981B;
    else
        return PDMDevHlpVMSetError(pDevIns, VERR_PDM_DEVINS_UNKNOWN_CFG_VALUES, RT_SRC_POS,
                                   N_("AC'97 configuration error: The \"Codec\" value \"%s\" is unsupported"), szCodec);

    LogRel(("AC97: Using codec '%s'\n", szCodec));

    /*
     * Use an own critical section for the device instead of the default
     * one provided by PDM. This allows fine-grained locking in combination
     * with TM when timer-specific stuff is being called in e.g. the MMIO handlers.
     */
    rc = PDMDevHlpCritSectInit(pDevIns, &pThis->CritSect, RT_SRC_POS, "AC'97");
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    /*
     * Initialize data (most of it anyway).
     */
    /* PCI Device */
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PCIDevSetVendorId(pPciDev,              0x8086); /* 00 ro - intel. */               Assert(pPciDev->abConfig[0x00] == 0x86); Assert(pPciDev->abConfig[0x01] == 0x80);
    PCIDevSetDeviceId(pPciDev,              0x2415); /* 02 ro - 82801 / 82801aa(?). */  Assert(pPciDev->abConfig[0x02] == 0x15); Assert(pPciDev->abConfig[0x03] == 0x24);
    PCIDevSetCommand(pPciDev,               0x0000); /* 04 rw,ro - pcicmd. */           Assert(pPciDev->abConfig[0x04] == 0x00); Assert(pPciDev->abConfig[0x05] == 0x00);
    PCIDevSetStatus(pPciDev,                VBOX_PCI_STATUS_DEVSEL_MEDIUM |  VBOX_PCI_STATUS_FAST_BACK); /* 06 rwc?,ro? - pcists. */  Assert(pPciDev->abConfig[0x06] == 0x80); Assert(pPciDev->abConfig[0x07] == 0x02);
    PCIDevSetRevisionId(pPciDev,            0x01);   /* 08 ro - rid. */                 Assert(pPciDev->abConfig[0x08] == 0x01);
    PCIDevSetClassProg(pPciDev,             0x00);   /* 09 ro - pi. */                  Assert(pPciDev->abConfig[0x09] == 0x00);
    PCIDevSetClassSub(pPciDev,              0x01);   /* 0a ro - scc; 01 == Audio. */    Assert(pPciDev->abConfig[0x0a] == 0x01);
    PCIDevSetClassBase(pPciDev,             0x04);   /* 0b ro - bcc; 04 == multimedia.*/Assert(pPciDev->abConfig[0x0b] == 0x04);
    PCIDevSetHeaderType(pPciDev,            0x00);   /* 0e ro - headtyp. */             Assert(pPciDev->abConfig[0x0e] == 0x00);
    PCIDevSetBaseAddress(pPciDev,           0,       /* 10 rw - nambar - native audio mixer base. */
                           true /* fIoSpace */, false /* fPrefetchable */, false /* f64Bit */, 0x00000000); Assert(pPciDev->abConfig[0x10] == 0x01); Assert(pPciDev->abConfig[0x11] == 0x00); Assert(pPciDev->abConfig[0x12] == 0x00); Assert(pPciDev->abConfig[0x13] == 0x00);
    PCIDevSetBaseAddress(pPciDev,           1,       /* 14 rw - nabmbar - native audio bus mastering. */
                         true /* fIoSpace */, false /* fPrefetchable */, false /* f64Bit */, 0x00000000); Assert(pPciDev->abConfig[0x14] == 0x01); Assert(pPciDev->abConfig[0x15] == 0x00); Assert(pPciDev->abConfig[0x16] == 0x00); Assert(pPciDev->abConfig[0x17] == 0x00);
    PCIDevSetInterruptLine(pPciDev,         0x00);   /* 3c rw. */                       Assert(pPciDev->abConfig[0x3c] == 0x00);
    PCIDevSetInterruptPin(pPciDev,          0x01);   /* 3d ro - INTA#. */               Assert(pPciDev->abConfig[0x3d] == 0x01);

    if (pThis->enmCodecModel == AC97CODEC_AD1980)
    {
        PCIDevSetSubSystemVendorId(pPciDev, 0x1028); /* 2c ro - Dell.) */
        PCIDevSetSubSystemId(pPciDev,       0x0177); /* 2e ro. */
    }
    else if (pThis->enmCodecModel == AC97CODEC_AD1981B)
    {
        PCIDevSetSubSystemVendorId(pPciDev, 0x1028); /* 2c ro - Dell.) */
        PCIDevSetSubSystemId(pPciDev,       0x01ad); /* 2e ro. */
    }
    else
    {
        PCIDevSetSubSystemVendorId(pPciDev, 0x8086); /* 2c ro - Intel.) */
        PCIDevSetSubSystemId(pPciDev,       0x0000); /* 2e ro. */
    }

    /*
     * Register the PCI device and associated I/O regions.
     */
    rc = PDMDevHlpPCIRegister(pDevIns, pPciDev);
    if (RT_FAILURE(rc))
        return rc;

    rc = PDMDevHlpPCIIORegionCreateIo(pDevIns, 0 /*iPciRegion*/, 256 /*cPorts*/,
                                      ichac97IoPortNamWrite, ichac97IoPortNamRead, NULL /*pvUser*/,
                                      "ICHAC97 NAM", NULL /*paExtDescs*/, &pThis->hIoPortsNam);
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpPCIIORegionCreateIo(pDevIns, 1 /*iPciRegion*/, 64 /*cPorts*/,
                                      ichac97IoPortNabmWrite, ichac97IoPortNabmRead, NULL /*pvUser*/,
                                      "ICHAC97 NABM", g_aNabmPorts, &pThis->hIoPortsNabm);
    AssertRCReturn(rc, rc);

    /*
     * Saved state.
     */
    rc = PDMDevHlpSSMRegister(pDevIns, AC97_SAVED_STATE_VERSION, sizeof(*pThis), ichac97R3SaveExec, ichac97R3LoadExec);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Attach drivers.  We ASSUME they are configured consecutively without any
     * gaps, so we stop when we hit the first LUN w/o a driver configured.
     */
    for (unsigned iLun = 0; ; iLun++)
    {
        AssertBreak(iLun < UINT8_MAX);
        LogFunc(("Trying to attach driver for LUN#%u ...\n", iLun));
        rc = ichac97R3AttachInternal(pDevIns, pThisCC, iLun, NULL /* ppDrv */);
        if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
        {
            LogFunc(("cLUNs=%u\n", iLun));
            break;
        }
        AssertLogRelMsgReturn(RT_SUCCESS(rc),  ("LUN#%u: rc=%Rrc\n", iLun, rc), rc);
    }

    uint32_t fMixer = AUDMIXER_FLAGS_NONE;
    if (pThisCC->Dbg.fEnabled)
        fMixer |= AUDMIXER_FLAGS_DEBUG;

    rc = AudioMixerCreate("AC'97 Mixer", 0 /* uFlags */, &pThisCC->pMixer);
    AssertRCReturn(rc, rc);

    rc = AudioMixerCreateSink(pThisCC->pMixer, "Line In",
                              PDMAUDIODIR_IN, pDevIns, &pThisCC->pSinkLineIn);
    AssertRCReturn(rc, rc);
    rc = AudioMixerCreateSink(pThisCC->pMixer, "Microphone In",
                              PDMAUDIODIR_IN, pDevIns, &pThisCC->pSinkMicIn);
    AssertRCReturn(rc, rc);
    rc = AudioMixerCreateSink(pThisCC->pMixer, "PCM Output",
                              PDMAUDIODIR_OUT, pDevIns, &pThisCC->pSinkOut);
    AssertRCReturn(rc, rc);

    /*
     * Create all hardware streams.
     */
    AssertCompile(RT_ELEMENTS(pThis->aStreams) == AC97_MAX_STREAMS);
    for (unsigned i = 0; i < AC97_MAX_STREAMS; i++)
    {
        rc = ichac97R3StreamConstruct(pThisCC, &pThis->aStreams[i], &pThisCC->aStreams[i], i /* SD# */);
        AssertRCReturn(rc, rc);
    }

    /*
     * Create the emulation timers (one per stream).
     *
     * We must the critical section for the timers as the device has a
     * noop section associated with it.
     *
     * Note:  Use TMCLOCK_VIRTUAL_SYNC here, as the guest's AC'97 driver
     *        relies on exact (virtual) DMA timing and uses DMA Position Buffers
     *        instead of the LPIB registers.
     */
    /** @todo r=bird: The need to use virtual sync is perhaps because TM
     *        doesn't schedule regular TMCLOCK_VIRTUAL timers as accurately as it
     *        should (VT-x preemption timer, etc).  Hope to address that before
     *        long. @bugref{9943}. */
    static const char * const s_apszNames[] = { "AC97 PI", "AC97 PO", "AC97 MC" };
    AssertCompile(RT_ELEMENTS(s_apszNames) == AC97_MAX_STREAMS);
    for (unsigned i = 0; i < AC97_MAX_STREAMS; i++)
    {
        rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL_SYNC, ichac97R3Timer, &pThis->aStreams[i],
                                  TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_RING0, s_apszNames[i], &pThis->aStreams[i].hTimer);
        AssertRCReturn(rc, rc);

        rc = PDMDevHlpTimerSetCritSect(pDevIns, pThis->aStreams[i].hTimer, &pThis->CritSect);
        AssertRCReturn(rc, rc);
    }

    ichac97R3Reset(pDevIns);

    /*
     * Info items.
     */
    //PDMDevHlpDBGFInfoRegister(pDevIns, "ac97",         "AC'97 registers. (ac97 [register case-insensitive])", ichac97R3DbgInfo);
    PDMDevHlpDBGFInfoRegister(pDevIns, "ac97bdl",      "AC'97 buffer descriptor list (BDL). (ac97bdl [stream number])",
                              ichac97R3DbgInfoBDL);
    PDMDevHlpDBGFInfoRegister(pDevIns, "ac97stream",   "AC'97 stream info. (ac97stream [stream number])", ichac97R3DbgInfoStream);
    PDMDevHlpDBGFInfoRegister(pDevIns, "ac97mixer",    "AC'97 mixer state.",                              ichac97R3DbgInfoMixer);

    /*
     * Register statistics.
     */
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatUnimplementedNabmReads,  STAMTYPE_COUNTER, "UnimplementedNabmReads", STAMUNIT_OCCURENCES, "Unimplemented NABM register reads.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatUnimplementedNabmWrites, STAMTYPE_COUNTER, "UnimplementedNabmWrites", STAMUNIT_OCCURENCES, "Unimplemented NABM register writes.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatUnimplementedNamReads,   STAMTYPE_COUNTER, "UnimplementedNamReads", STAMUNIT_OCCURENCES, "Unimplemented NAM register reads.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatUnimplementedNamWrites,  STAMTYPE_COUNTER, "UnimplementedNamWrites", STAMUNIT_OCCURENCES, "Unimplemented NAM register writes.");
# ifdef VBOX_WITH_STATISTICS
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTimer,        STAMTYPE_PROFILE, "Timer",        STAMUNIT_TICKS_PER_CALL, "Profiling ichac97Timer.");
# endif
    for (unsigned idxStream = 0; idxStream < RT_ELEMENTS(pThis->aStreams); idxStream++)
    {
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStreams[idxStream].cbDmaPeriod, STAMTYPE_U32, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                               "Bytes to transfer in the current DMA period.",  "Stream%u/cbTransferChunk", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStreams[idxStream].Regs.cr, STAMTYPE_X8, STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                               "Control register (CR), bit 0 is the run bit.",  "Stream%u/reg-CR", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStreams[idxStream].Regs.sr, STAMTYPE_X16, STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                               "Status register (SR).",                         "Stream%u/reg-SR", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.Cfg.Props.uHz, STAMTYPE_U32, STAMVISIBILITY_USED, STAMUNIT_HZ,
                               "The stream frequency.",                         "Stream%u/Hz", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.Cfg.Props.cbFrame, STAMTYPE_U8, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                               "The frame size.",                               "Stream%u/FrameSize", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.offRead, STAMTYPE_U64, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                               "Virtual internal buffer read position.",        "Stream%u/offRead", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.offWrite, STAMTYPE_U64, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                               "Virtual internal buffer write position.",       "Stream%u/offWrite", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.StatDmaBufSize, STAMTYPE_U32, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                               "Size of the internal DMA buffer.",              "Stream%u/DMABufSize", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.StatDmaBufUsed, STAMTYPE_U32, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                               "Number of bytes used in the internal DMA buffer.", "Stream%u/DMABufUsed", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.StatDmaFlowProblems, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                               "Number of internal DMA buffer problems.",       "Stream%u/DMABufferProblems", idxStream);
        if (ichac97R3GetDirFromSD(idxStream) == PDMAUDIODIR_OUT)
            PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.StatDmaFlowErrors, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                                   "Number of internal DMA buffer overflows.",  "Stream%u/DMABufferOverflows", idxStream);
        else
        {
            PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.StatDmaFlowErrors, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                                   "Number of internal DMA buffer underuns.", "Stream%u/DMABufferUnderruns", idxStream);
            PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.StatDmaFlowErrorBytes, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                                   "Number of bytes of silence added to cope with underruns.", "Stream%u/DMABufferSilence", idxStream);
        }
        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.StatDmaSkippedDch, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                               "DMA transfer period skipped, controller halted (DCH).", "Stream%u/DMASkippedDch", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.StatDmaSkippedPendingBcis, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                               "DMA transfer period skipped because of BCIS pending.", "Stream%u/DMASkippedPendingBCIS", idxStream);

        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.StatStart, STAMTYPE_PROFILE, STAMVISIBILITY_USED, STAMUNIT_NS_PER_CALL,
                               "Starting the stream.",  "Stream%u/Start", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.StatStop, STAMTYPE_PROFILE, STAMVISIBILITY_USED, STAMUNIT_NS_PER_CALL,
                               "Stopping the stream.",  "Stream%u/Stop", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.StatReset, STAMTYPE_PROFILE, STAMVISIBILITY_USED, STAMUNIT_NS_PER_CALL,
                               "Resetting the stream.",  "Stream%u/Reset", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.StatReSetUpChanged, STAMTYPE_PROFILE, STAMVISIBILITY_USED, STAMUNIT_NS_PER_CALL,
                               "ichac97R3StreamReSetUp when recreating the streams.", "Stream%u/ReSetUp-Change", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.StatReSetUpSame, STAMTYPE_PROFILE, STAMVISIBILITY_USED, STAMUNIT_NS_PER_CALL,
                               "ichac97R3StreamReSetUp when no change.",        "Stream%u/ReSetUp-NoChange", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.StatWriteCr, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                               "CR register writes.",                           "Stream%u/WriteCr", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aStreams[idxStream].State.StatWriteLviRecover, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                               "LVI register writes recovering from underflow.", "Stream%u/WriteLviRecover", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStreams[idxStream].StatWriteLvi, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                               "LVI register writes (non-recoving).",           "Stream%u/WriteLvi", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStreams[idxStream].StatWriteSr1, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                               "SR register 1-byte writes.",                    "Stream%u/WriteSr-1byte", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStreams[idxStream].StatWriteSr2, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                               "SR register 2-byte writes.",                    "Stream%u/WriteSr-2byte", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStreams[idxStream].StatWriteBdBar, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                               "BDBAR register writes.",                        "Stream%u/WriteBdBar", idxStream);
    }

    LogFlowFuncLeaveRC(VINF_SUCCESS);
    return VINF_SUCCESS;
}

#else /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) ichac97RZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PAC97STATE pThis = PDMDEVINS_2_DATA(pDevIns, PAC97STATE);

    int rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortsNam, ichac97IoPortNamWrite, ichac97IoPortNamRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortsNabm, ichac97IoPortNabmWrite, ichac97IoPortNabmRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */

/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceICHAC97 =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "ichac97",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE
                                    | PDM_DEVREG_FLAGS_FIRST_POWEROFF_NOTIFICATION /* stream clearnup with working drivers */,
    /* .fClass = */                 PDM_DEVREG_CLASS_AUDIO,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(AC97STATE),
    /* .cbInstanceCC = */           CTX_EXPR(sizeof(AC97STATER3), 0, 0),
    /* .cbInstanceRC = */           0,
    /* .cMaxPciDevices = */         1,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "ICH AC'97 Audio Controller",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           ichac97R3Construct,
    /* .pfnDestruct = */            ichac97R3Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               ichac97R3Reset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              ichac97R3Attach,
    /* .pfnDetach = */              ichac97R3Detach,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        NULL,
    /* .pfnPowerOff = */            ichac97R3PowerOff,
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
    /* .pfnConstruct = */           ichac97RZConstruct,
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
    /* .pfnConstruct = */           ichac97RZConstruct,
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

