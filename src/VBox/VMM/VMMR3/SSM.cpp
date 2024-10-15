/* $Id: SSM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * SSM - Saved State Manager.
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


/** @page pg_ssm        SSM - The Saved State Manager
 *
 * The Saved State Manager (SSM) implements facilities for saving and loading a
 * VM state in a structural manner using callbacks for named data units.
 *
 * At init time each of the VMM components, Devices, Drivers and one or two
 * other things will register data units which they need to save and restore.
 * Each unit have a unique name (ascii), instance number, and a set of callbacks
 * associated with it.  The name will be used to identify the unit during
 * restore.  The callbacks are for the two operations, save and restore.  There
 * are three callbacks for each of the two - a prepare, a execute and a complete
 * - giving each component ample opportunity to perform actions both before and
 * afterwards.
 *
 * The SSM provides a number of APIs for encoding and decoding the data: @see
 * grp_ssm
 *
 *
 *
 * @section sec_ssm_live_snapshots  Live Snapshots
 *
 * The live snapshots feature (LS) is similar to teleportation (TP) and was a
 * natural first step when implementing TP.  The main differences between LS and
 * TP are that after a live snapshot we will have a saved state file, disk image
 * snapshots, and the VM will still be running.
 *
 * Compared to normal saved stated and snapshots, the difference is in that the
 * VM is running while we do most of the saving.  Prior to LS, there was only
 * one round of callbacks during saving and the VM was paused during it.  With
 * LS there are 1 or more passes while the VM is still running and a final one
 * after it has been paused.  The runtime passes are executed on a dedicated
 * thread running at at the same priority as the EMTs so that the saving doesn't
 * starve or lose in scheduling questions (note: not implemented yet). The final
 * pass is done on EMT(0).
 *
 * There are a couple of common reasons why LS and TP will fail:
 *   - Memory configuration changed (PCI memory mappings).
 *   - Takes too long (TP) / Too much output (LS).
 *
 *
 * The live saving sequence is something like this:
 *
 *      -# SSMR3LiveSave is called on EMT0.  It returns a saved state
 *         handle.
 *      -# SSMR3LiveDoStep1 is called on a non-EMT.  This will save the major
 *         parts of the state while the VM may still be running.
 *      -# The VM is suspended.
 *      -# SSMR3LiveDoStep2 is called on EMT0 to save the remainder of the state
 *         in the normal way.
 *      -# The client does any necessary reconfiguration of harddisks and
 *         similar.
 *      -# SSMR3LiveDone is called on EMT0 to close the handle.
 *      -# The VM is resumed or powered off and destroyed.
 *
 *
 * @section sec_ssm_teleportation   Teleportation
 *
 * As mentioned in the previous section, the main differences between this and
 * live snapshots are in where the saved state is written and what state the
 * local VM is in afterwards - at least from the VMM point of view.  The
 * necessary administrative work - establishing the connection to the remote
 * machine, cloning the VM config on it and doing lowlevel saved state data
 * transfer - is taken care of by layer above the VMM (i.e.  Main).
 *
 * The SSM data format was made streamable for the purpose of teleportation
 * (v1.2 was the last non-streamable version).
 *
 *
 * @section sec_ssm_format          Saved State Format
 *
 * The stream format starts with a header (SSMFILEHDR) that indicates the
 * version and such things, it is followed by zero or more saved state units
 * (name + instance + pass), and the stream concludes with a footer
 * (SSMFILEFTR) that contains unit counts and optionally a checksum for the
 * entire file.  (In version 1.2 and earlier, the checksum was in the header and
 * there was no footer.  This meant that the header was updated after the entire
 * file was written.)
 *
 * The saved state units each starts with a variable sized header
 * (SSMFILEUNITHDRV2) that contains the name, instance and pass.  The data
 * follows the header and is encoded as records with a 2-8 byte record header
 * indicating the type, flags and size.  The first byte in the record header
 * indicates the type and flags:
 *
 *   - bits 0..3: Record type:
 *       - type 0: Invalid.
 *       - type 1: Terminator with CRC-32 and unit size.
 *       - type 2: Raw data record.
 *       - type 3: Raw data compressed by LZF. The data is prefixed by a 8-bit
 *                 field containing the length of the uncompressed data given in
 *                 1KB units.
 *       - type 4: Zero data. The record header is followed by a 8-bit field
 *                 counting the length of the zero data given in 1KB units.
 *       - type 5: Named data - length prefixed name followed by the data. This
 *                 type is not implemented yet as we're missing the API part, so
 *                 the type assignment is tentative.
 *       - types 6 thru 15 are current undefined.
 *   - bit 4: Important (set), can be skipped (clear).
 *   - bit 5: Undefined flag, must be zero.
 *   - bit 6: Undefined flag, must be zero.
 *   - bit 7: "magic" bit, always set.
 *
 * Record header byte 2 (optionally thru 7) is the size of the following data
 * encoded in UTF-8 style.  To make buffering simpler and more efficient during
 * the save operation, the strict checks enforcing optimal encoding has been
 * relaxed for the 2 and 3 byte encodings.
 *
 * (In version 1.2 and earlier the unit data was compressed and not record
 * based. The unit header contained the compressed size of the data, i.e. it
 * needed updating after the data was written.)
 *
 *
 * @section sec_ssm_future          Future Changes
 *
 * There are plans to extend SSM to make it easier to be both backwards and
 * (somewhat) forwards compatible.  One of the new features will be being able
 * to classify units and data items as unimportant (added to the format in
 * v2.0).  Another suggested feature is naming data items (also added to the
 * format in v2.0), perhaps by extending the SSMR3PutStruct API.  Both features
 * will require API changes, the naming may possibly require both buffering of
 * the stream as well as some helper managing them.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_SSM
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/pdmcritsect.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/vmm.h>
#include "SSMInternal.h"
#include <VBox/vmm/vm.h>
#include <VBox/vmm/uvm.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/version.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/crc.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include <iprt/thread.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include <iprt/zip.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The max length of a unit name. */
#define SSM_MAX_NAME_SIZE                       48

/** Saved state file magic base string. */
#define SSMFILEHDR_MAGIC_BASE                   "\177VirtualBox SavedState "
/** Saved state file magic indicating version 1.x. */
#define SSMFILEHDR_MAGIC_V1_X                   "\177VirtualBox SavedState V1."
/** Saved state file v1.1 magic. */
#define SSMFILEHDR_MAGIC_V1_1                   "\177VirtualBox SavedState V1.1\n"
/** Saved state file v1.2 magic. */
#define SSMFILEHDR_MAGIC_V1_2                   "\177VirtualBox SavedState V1.2\n\0\0\0"
/** Saved state file v2.0 magic. */
#define SSMFILEHDR_MAGIC_V2_0                   "\177VirtualBox SavedState V2.0\n\0\0\0"

/** @name SSMFILEHDR::fFlags
 * @{ */
/** The stream is checksummed up to the footer using CRC-32. */
#define SSMFILEHDR_FLAGS_STREAM_CRC32           RT_BIT_32(0)
/** Indicates that the file was produced by a live save. */
#define SSMFILEHDR_FLAGS_STREAM_LIVE_SAVE       RT_BIT_32(1)
/** @} */

/** The directory magic. */
#define SSMFILEDIR_MAGIC                        "\nDir\n\0\0"

/** Saved state file v2.0 magic. */
#define SSMFILEFTR_MAGIC                        "\nFooter"

/** Data unit magic. */
#define SSMFILEUNITHDR_MAGIC                    "\nUnit\n\0"
/** Data end marker magic. */
#define SSMFILEUNITHDR_END                      "\nTheEnd"


/** @name Record Types (data unit)
 * @{ */
/** The record type mask. */
#define SSM_REC_TYPE_MASK                       UINT8_C(0x0f)
/** Invalid record. */
#define SSM_REC_TYPE_INVALID                    0
/** Normal termination record, see SSMRECTERM. */
#define SSM_REC_TYPE_TERM                       1
/** Raw data. The data follows the size field without further ado. */
#define SSM_REC_TYPE_RAW                        2
/** Raw data compressed by LZF.
 * The record header is followed by a 8-bit field containing the size of the
 * uncompressed data in 1KB units.  The compressed data is after it. */
#define SSM_REC_TYPE_RAW_LZF                    3
/** Raw zero data.
 * The record header is followed by a 8-bit field containing the size of the
 * zero data in 1KB units. */
#define SSM_REC_TYPE_RAW_ZERO                   4
/** Named data items.
 * A length prefix zero terminated string (i.e. max 255) followed by the data.  */
#define SSM_REC_TYPE_NAMED                      5
/** Macro for validating the record type.
 * This can be used with the flags+type byte, no need to mask out the type first. */
#define SSM_REC_TYPE_IS_VALID(u8Type)           (   ((u8Type) & SSM_REC_TYPE_MASK) >  SSM_REC_TYPE_INVALID \
                                                 && ((u8Type) & SSM_REC_TYPE_MASK) <= SSM_REC_TYPE_NAMED )
/** @} */

/** The flag mask. */
#define SSM_REC_FLAGS_MASK                      UINT8_C(0xf0)
/** The record is important if this flag is set, if clear it can be omitted. */
#define SSM_REC_FLAGS_IMPORTANT                 UINT8_C(0x10)
/** This flag is always set. */
#define SSM_REC_FLAGS_FIXED                     UINT8_C(0x80)
/** Macro for validating the flags.
 * No need to mask the flags out of the flags+type byte before invoking this macro.  */
#define SSM_REC_FLAGS_ARE_VALID(fFlags)         ( ((fFlags) & UINT8_C(0xe0)) == UINT8_C(0x80) )

/** Macro for validating the type and flags byte in a data record. */
#define SSM_REC_ARE_TYPE_AND_FLAGS_VALID(u8)    ( SSM_REC_FLAGS_ARE_VALID(u8) && SSM_REC_TYPE_IS_VALID(u8) )

/** @name SSMRECTERM::fFlags
 * @{ */
/** There is a CRC-32 value for the stream. */
#define SSMRECTERM_FLAGS_CRC32                  UINT16_C(0x0001)
/** @} */

/** Start structure magic. (Isaac Asimov) */
#define SSMR3STRUCT_BEGIN                       UINT32_C(0x19200102)
/** End structure magic. (Isaac Asimov) */
#define SSMR3STRUCT_END                         UINT32_C(0x19920406)


/** Number of bytes to log in Log2 and Log4 statements. */
#define SSM_LOG_BYTES                           16

/** SSMHANDLE::fCancelled value indicating that the operation has been
 *  cancelled. */
#define SSMHANDLE_CANCELLED                     UINT32_C(0xdeadbeef)
/** SSMHANDLE::fCancelled value indicating no cancellation. */
#define SSMHANDLE_OK                            UINT32_C(0x77777777)


/** Macro for checking the u32CRC field of a structure.
 * The Msg can assume there are  u32ActualCRC and u32CRC in the context. */
#define SSM_CHECK_CRC32_RET(p, cb, Msg) \
    do \
    { \
        uint32_t u32CRC = (p)->u32CRC; \
        (p)->u32CRC = 0; \
        uint32_t u32ActualCRC = RTCrc32((p), (cb)); \
        (p)->u32CRC = u32CRC; \
        AssertLogRelMsgReturn(u32ActualCRC == u32CRC, Msg, VERR_SSM_INTEGRITY_CRC); \
    } while (0)

/** The number of bytes to compress is one block.
 * Must be a multiple of 1KB.  */
#define SSM_ZIP_BLOCK_SIZE                      _4K
AssertCompile(SSM_ZIP_BLOCK_SIZE / _1K * _1K == SSM_ZIP_BLOCK_SIZE);


/**
 * Asserts that the handle is writable and returns with VERR_SSM_INVALID_STATE
 * if it isn't.
 */
#define SSM_ASSERT_WRITEABLE_RET(pSSM) \
    AssertMsgReturn(   pSSM->enmOp == SSMSTATE_SAVE_EXEC \
                    || pSSM->enmOp == SSMSTATE_LIVE_EXEC \
                    || pSSM->enmOp == SSMSTATE_OPEN_WRITE,\
                    ("Invalid state %d\n", pSSM->enmOp), VERR_SSM_INVALID_STATE);

/**
 * Asserts that the handle is readable and returns with VERR_SSM_INVALID_STATE
 * if it isn't.
 */
#define SSM_ASSERT_READABLE_RET(pSSM) \
    AssertMsgReturn(   pSSM->enmOp == SSMSTATE_LOAD_EXEC \
                    || pSSM->enmOp == SSMSTATE_OPEN_READ,\
                    ("Invalid state %d\n", pSSM->enmOp), VERR_SSM_INVALID_STATE);

/** Checks for cancellation and returns if pending.
 * Sets SSMHANDLE::rc to VERR_SSM_CANCELLED (if it still indicates success) and
 * then returns SSMHANDLE::rc. (Debug logging only.) */
#define SSM_CHECK_CANCELLED_RET(pSSM) \
    do \
    { \
        if (RT_UNLIKELY(ASMAtomicUoReadU32(&(pSSM)->fCancelled) == SSMHANDLE_CANCELLED)) \
        { \
            LogFlow(("%Rfn: Cancelled -> VERR_SSM_CANCELLED\n", __PRETTY_FUNCTION__)); \
            if (RT_SUCCESS((pSSM)->rc)) \
                (pSSM)->rc = VERR_SSM_CANCELLED; \
            return (pSSM)->rc; \
        } \
    } while (0)

/**
 * Asserts that the handle is somewhat valid.  No returns as this is just a
 * simple safeguard for catching bad API calls. */
#define SSM_ASSERT_VALID_HANDLE(pSSM) \
    do \
    { \
        AssertPtr(pSSM); \
        Assert(pSSM->enmOp > SSMSTATE_INVALID && pSSM->enmOp < SSMSTATE_END); \
    } while (0)


/** @def SSM_HOST_IS_MSC_32
 * Set to 1 if the host is 32-bit MSC, otherwise set to 0.
 *   */
#if defined(_MSC_VER) && HC_ARCH_BITS == 32
# define SSM_HOST_IS_MSC_32     1
#else
# define SSM_HOST_IS_MSC_32     0
#endif



/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** SSM state. */
typedef enum SSMSTATE
{
    SSMSTATE_INVALID = 0,
    SSMSTATE_LIVE_PREP,
    SSMSTATE_LIVE_STEP1,
    SSMSTATE_LIVE_EXEC,
    SSMSTATE_LIVE_VOTE,
    SSMSTATE_LIVE_STEP2,
    SSMSTATE_SAVE_PREP,
    SSMSTATE_SAVE_EXEC,
    SSMSTATE_SAVE_DONE,
    SSMSTATE_LOAD_PREP,
    SSMSTATE_LOAD_EXEC,
    SSMSTATE_LOAD_DONE,
    SSMSTATE_OPEN_READ,
    SSMSTATE_OPEN_WRITE,
    SSMSTATE_END
} SSMSTATE;


/** Pointer to a SSM stream buffer. */
typedef struct SSMSTRMBUF *PSSMSTRMBUF;
/**
 * A SSM stream buffer.
 */
typedef struct SSMSTRMBUF
{
    /** The buffer data. */
    uint8_t                 abData[_64K];

    /** The stream position of this buffer. */
    uint64_t                offStream;
    /** The amount of buffered data. */
    uint32_t                cb;
    /** End of stream indicator (for read streams only). */
    bool                    fEndOfStream;
    /** The nano timestamp set by ssmR3StrmGetFreeBuf. */
    uint64_t                NanoTS;
    /** Pointer to the next buffer in the chain. */
    PSSMSTRMBUF volatile    pNext;
} SSMSTRMBUF;

/**
 * SSM stream.
 *
 * This is a typical producer / consumer setup with a dedicated I/O thread and
 * fixed number of buffers for read ahead and write back.
 */
typedef struct SSMSTRM
{
    /** The stream method table. */
    PCSSMSTRMOPS            pOps;
    /** The user argument for the stream methods.
     * For file based streams, this is the file handle and not a pointer. */
    void                   *pvUser;

    /** Write (set) or read (clear) stream. */
    bool                    fWrite;
    /** Termination indicator. */
    bool volatile           fTerminating;
    /** Indicates whether it is necessary to seek before the next buffer is
     *  read from the stream.  This is used to avoid a seek in ssmR3StrmPeekAt. */
    bool                    fNeedSeek;
    /** Stream error status. */
    int32_t volatile        rc;
    /** The handle of the I/O thread. This is set to nil when not active. */
    RTTHREAD                hIoThread;
    /** Where to seek to. */
    uint64_t                offNeedSeekTo;

    /** The head of the consumer queue.
     * For save the consumer is the I/O thread.  For load the I/O thread is the
     * producer. */
    PSSMSTRMBUF volatile    pHead;
    /** Chain of free buffers.
     * The consumer/producer roles are the inverse of pHead.  */
    PSSMSTRMBUF volatile    pFree;
    /** Event that's signalled when pHead is updated. */
    RTSEMEVENT              hEvtHead;
    /** Event that's signalled when pFree is updated. */
    RTSEMEVENT              hEvtFree;

    /** List of pending buffers that has been dequeued from pHead and reversed. */
    PSSMSTRMBUF             pPending;
    /** Pointer to the current buffer. */
    PSSMSTRMBUF             pCur;
    /** The stream offset of the current buffer. */
    uint64_t                offCurStream;
    /** The current buffer offset. */
    uint32_t                off;
    /** Whether we're checksumming reads/writes. */
    bool                    fChecksummed;
    /** The stream CRC if fChecksummed is set. */
    uint32_t                u32StreamCRC;
    /** How far into the buffer u32StreamCRC is up-to-date.
     * This may lag behind off as it's desirable to checksum as large blocks as
     * possible.  */
    uint32_t                offStreamCRC;
} SSMSTRM;
/** Pointer to a SSM stream. */
typedef SSMSTRM *PSSMSTRM;


/**
 * Handle structure.
 */
typedef struct SSMHANDLE
{
    /** Stream/buffer manager. */
    SSMSTRM                 Strm;

    /** Pointer to the VM. */
    PVM                     pVM;
    /** The current operation. */
    SSMSTATE                enmOp;
    /** What to do after save completes. (move the enum) */
    SSMAFTER                enmAfter;
    /** Flag indicating that the operation has been cancelled. */
    uint32_t volatile       fCancelled;
    /** The current rc of the save operation. */
    int32_t                 rc;
    /** Number of compressed bytes left in the current data unit (V1). */
    uint64_t                cbUnitLeftV1;
    /** The current compressed? offset into the data unit. */
    uint64_t                offUnit;
    /** The current user data offset into the unit (debug purposes). */
    uint64_t                offUnitUser;
    /** Indicates that this is a live save or restore operation. */
    bool                    fLiveSave;

    /** Pointer to the progress callback function. */
    PFNVMPROGRESS           pfnProgress;
    /** User specified argument to the callback function. */
    void                   *pvUser;
    /** Next completion percentage. (corresponds to offEstProgress) */
    unsigned                uPercent;
    /** The position of the next progress callback in the estimated file. */
    uint64_t                offEstProgress;
    /** The estimated total byte count.
     * (Only valid after the prep.) */
    uint64_t                cbEstTotal;
    /** Current position in the estimated file. */
    uint64_t                offEst;
    /** End of current unit in the estimated file. */
    uint64_t                offEstUnitEnd;
    /** The amount of % we reserve for the 'live' stage */
    unsigned                uPercentLive;
    /** The amount of % we reserve for the 'prepare' phase */
    unsigned                uPercentPrepare;
    /** The amount of % we reserve for the 'done' stage */
    unsigned                uPercentDone;
    /** The lowest value reported via SSMR3HandleReportLivePercent during one
     * vote run. */
    unsigned                uReportedLivePercent;
    /** The filename, NULL if remote stream. */
    const char             *pszFilename;

    union
    {
        /** Write data. */
        struct
        {
            /** Offset into the databuffer. */
            uint32_t        offDataBuffer;
            /** Space for the record header.  */
            uint8_t         abRecHdr[1+7];
            /** Data buffer. */
            uint8_t         abDataBuffer[4096];
            /** The maximum downtime given as milliseconds. */
            uint32_t        cMsMaxDowntime;

            /** SSMSTATE_OPEN_WRITE: Number of allocated directory entries. */
            uint32_t        cDirEntriesAlloced;
            /** SSMSTATE_OPEN_WRITE: The directory. */
            struct SSMFILEDIR *pDir;
        } Write;

        /** Read data. */
        struct
        {
            /** V1: The decompressor of the current data unit. */
            PRTZIPDECOMP    pZipDecompV1;
            /** The major format version number. */
            uint32_t        uFmtVerMajor;
            /** The minor format version number. */
            uint32_t        uFmtVerMinor;

            /** V2: Unread bytes in the current record. */
            uint32_t        cbRecLeft;
            /** V2: Bytes in the data buffer. */
            uint32_t        cbDataBuffer;
            /** V2: Current buffer position. */
            uint32_t        offDataBuffer;
            /** V2: End of data indicator. */
            bool            fEndOfData;
            /** V2: The type and flags byte fo the current record. */
            uint8_t         u8TypeAndFlags;

            /** @name Context info for SSMR3SetLoadError.
             * @{  */
            /** Pointer to the header for the current unit. */
            PSSMUNIT        pCurUnit;
            /** The version of the current unit if in the load exec stage. */
            uint32_t        uCurUnitVer;
            /** The pass number of the current unit if in the load exec stage. */
            uint32_t        uCurUnitPass;
            /** Whether SSMR3SetLoadError[V] has been called.
             * @note Using ASMAtomicXchgBool because I'm very lazy. */
            bool volatile   fHaveSetError;
            /** @} */

            /** RTGCPHYS size in bytes. (Only applicable when loading/reading.) */
            unsigned        cbGCPhys;
            /** RTGCPTR size in bytes. (Only applicable when loading/reading.) */
            unsigned        cbGCPtr;
            /** Whether cbGCPtr is fixed or settable. */
            bool            fFixedGCPtrSize;

            /** 32-bit MSC saved this? */
            bool            fIsHostMsc32;
            /** "Host OS" dot "architecture", picked up from recent SSM data units. */
            char            szHostOSAndArch[32];

            /** @name Header info (set by ssmR3ValidateFile)
             * @{ */
            /** The size of the file header. */
            uint32_t        cbFileHdr;
            /** The major version number. */
            uint16_t        u16VerMajor;
            /** The minor version number. */
            uint16_t        u16VerMinor;
            /** The build number. */
            uint32_t        u32VerBuild;
            /** The SVN revision. */
            uint32_t        u32SvnRev;
            /** 32 or 64 depending on the host. */
            uint8_t         cHostBits;
            /** Whether the stream is checksummed (SSMFILEHDR_FLAGS_STREAM_CRC32). */
            bool            fStreamCrc32;
            /** The CRC of the loaded file. */
            uint32_t        u32LoadCRC;
            /** The size of the load file. */
            uint64_t        cbLoadFile;
            /** @} */

            /** V2: Data buffer.
             * @remarks Be extremely careful when changing the size of this buffer! */
            uint8_t         abDataBuffer[4096];

            /** V2: Decompression buffer for when we cannot use the stream buffer. */
            uint8_t         abComprBuffer[4096];
        } Read;
    } u;
} SSMHANDLE;


/**
 * Header of the saved state file.
 *
 * Added in r5xxxx on 2009-07-2?, VirtualBox v3.0.51.
 */
typedef struct SSMFILEHDR
{
    /** Magic string which identifies this file as a version of VBox saved state
     *  file format (SSMFILEHDR_MAGIC_V2_0). */
    char            szMagic[32];
    /** The major version number. */
    uint16_t        u16VerMajor;
    /** The minor version number. */
    uint16_t        u16VerMinor;
    /** The build number. */
    uint32_t        u32VerBuild;
    /** The SVN revision. */
    uint32_t        u32SvnRev;
    /** 32 or 64 depending on the host. */
    uint8_t         cHostBits;
    /** The size of RTGCPHYS. */
    uint8_t         cbGCPhys;
    /** The size of RTGCPTR. */
    uint8_t         cbGCPtr;
    /** Reserved header space - must be zero. */
    uint8_t         u8Reserved;
    /** The number of units that (may) have stored data in the file. */
    uint32_t        cUnits;
    /** Flags, see SSMFILEHDR_FLAGS_XXX.  */
    uint32_t        fFlags;
    /** The maximum size of decompressed data. */
    uint32_t        cbMaxDecompr;
    /** The checksum of this header.
     * This field is set to zero when calculating the checksum. */
    uint32_t        u32CRC;
} SSMFILEHDR;
AssertCompileSize(SSMFILEHDR, 64);
AssertCompileMemberOffset(SSMFILEHDR, u32CRC, 60);
AssertCompileMemberSize(SSMFILEHDR, szMagic, sizeof(SSMFILEHDR_MAGIC_V2_0));
/** Pointer to a saved state file header. */
typedef SSMFILEHDR *PSSMFILEHDR;
/** Pointer to a const saved state file header. */
typedef SSMFILEHDR const *PCSSMFILEHDR;


/**
 * Header of the saved state file.
 *
 * Added in r40980 on 2008-12-15, VirtualBox v2.0.51.
 *
 * @remarks This is a superset of SSMFILEHDRV11.
 */
typedef struct SSMFILEHDRV12
{
    /** Magic string which identifies this file as a version of VBox saved state
     *  file format (SSMFILEHDR_MAGIC_V1_2). */
    char            achMagic[32];
    /** The size of this file. Used to check
     * whether the save completed and that things are fine otherwise. */
    uint64_t        cbFile;
    /** File checksum. The actual calculation skips past the u32CRC field. */
    uint32_t        u32CRC;
    /** Padding. */
    uint32_t        u32Reserved;
    /** The machine UUID. (Ignored if NIL.) */
    RTUUID          MachineUuid;

    /** The major version number. */
    uint16_t        u16VerMajor;
    /** The minor version number. */
    uint16_t        u16VerMinor;
    /** The build number. */
    uint32_t        u32VerBuild;
    /** The SVN revision. */
    uint32_t        u32SvnRev;

    /** 32 or 64 depending on the host. */
    uint8_t         cHostBits;
    /** The size of RTGCPHYS. */
    uint8_t         cbGCPhys;
    /** The size of RTGCPTR. */
    uint8_t         cbGCPtr;
    /** Padding. */
    uint8_t         au8Reserved;
} SSMFILEHDRV12;
AssertCompileSize(SSMFILEHDRV12, 64+16);
AssertCompileMemberOffset(SSMFILEHDRV12, u32CRC, 40);
AssertCompileMemberSize(SSMFILEHDRV12, achMagic, sizeof(SSMFILEHDR_MAGIC_V1_2));
/** Pointer to a saved state file header. */
typedef SSMFILEHDRV12 *PSSMFILEHDRV12;


/**
 * Header of the saved state file, version 1.1.
 *
 * Added in r23677 on 2007-08-17, VirtualBox v1.4.1.
 */
typedef struct SSMFILEHDRV11
{
    /** Magic string which identifies this file as a version of VBox saved state
     *  file format (SSMFILEHDR_MAGIC_V1_1). */
    char            achMagic[32];
    /** The size of this file. Used to check
     * whether the save completed and that things are fine otherwise. */
    uint64_t        cbFile;
    /** File checksum. The actual calculation skips past the u32CRC field. */
    uint32_t        u32CRC;
    /** Padding. */
    uint32_t        u32Reserved;
    /** The machine UUID. (Ignored if NIL.) */
    RTUUID          MachineUuid;
} SSMFILEHDRV11;
AssertCompileSize(SSMFILEHDRV11, 64);
AssertCompileMemberOffset(SSMFILEHDRV11, u32CRC, 40);
/** Pointer to a saved state file header. */
typedef SSMFILEHDRV11 *PSSMFILEHDRV11;


/**
 * Data unit header.
 */
typedef struct SSMFILEUNITHDRV2
{
    /** Magic (SSMFILEUNITHDR_MAGIC or SSMFILEUNITHDR_END). */
    char            szMagic[8];
    /** The offset in the saved state stream of the start of this unit.
     * This is mainly intended for sanity checking. */
    uint64_t        offStream;
    /** The CRC-in-progress value this unit starts at. */
    uint32_t        u32CurStreamCRC;
    /** The checksum of this structure, including the whole name.
     * Calculated with this field set to zero.  */
    uint32_t        u32CRC;
    /** Data version. */
    uint32_t        u32Version;
    /** Instance number. */
    uint32_t        u32Instance;
    /** Data pass number. */
    uint32_t        u32Pass;
    /** Flags reserved for future extensions. Must be zero. */
    uint32_t        fFlags;
    /** Size of the data unit name including the terminator. (bytes) */
    uint32_t        cbName;
    /** Data unit name, variable size. */
    char            szName[SSM_MAX_NAME_SIZE];
} SSMFILEUNITHDRV2;
AssertCompileMemberOffset(SSMFILEUNITHDRV2, szName, 44);
AssertCompileMemberSize(SSMFILEUNITHDRV2, szMagic, sizeof(SSMFILEUNITHDR_MAGIC));
AssertCompileMemberSize(SSMFILEUNITHDRV2, szMagic, sizeof(SSMFILEUNITHDR_END));
/** Pointer to SSMFILEUNITHDRV2.  */
typedef SSMFILEUNITHDRV2 *PSSMFILEUNITHDRV2;


/**
 * Data unit header.
 *
 * This is used by v1.0, v1.1 and v1.2 of the format.
 */
typedef struct SSMFILEUNITHDRV1
{
    /** Magic (SSMFILEUNITHDR_MAGIC or SSMFILEUNITHDR_END). */
    char            achMagic[8];
    /** Number of bytes in this data unit including the header. */
    uint64_t        cbUnit;
    /** Data version. */
    uint32_t        u32Version;
    /** Instance number. */
    uint32_t        u32Instance;
    /** Size of the data unit name including the terminator. (bytes) */
    uint32_t        cchName;
    /** Data unit name. */
    char            szName[1];
} SSMFILEUNITHDRV1;
/** Pointer to SSMFILEUNITHDR.  */
typedef SSMFILEUNITHDRV1 *PSSMFILEUNITHDRV1;


/**
 * Termination data record.
 */
typedef struct SSMRECTERM
{
    uint8_t         u8TypeAndFlags;
    /** The record size (sizeof(SSMRECTERM) - 2). */
    uint8_t         cbRec;
    /** Flags, see SSMRECTERM_FLAGS_CRC32. */
    uint16_t        fFlags;
    /** The checksum of the stream up to fFlags (exclusive). */
    uint32_t        u32StreamCRC;
    /** The length of this data unit in bytes (including this record). */
    uint64_t        cbUnit;
} SSMRECTERM;
AssertCompileSize(SSMRECTERM, 16);
AssertCompileMemberAlignment(SSMRECTERM, cbUnit, 8);
/** Pointer to a termination record. */
typedef SSMRECTERM *PSSMRECTERM;
/** Pointer to a const termination record. */
typedef SSMRECTERM const *PCSSMRECTERM;


/**
 * Directory entry.
 */
typedef struct SSMFILEDIRENTRY
{
    /** The offset of the data unit. */
    uint64_t        off;
    /** The instance number. */
    uint32_t        u32Instance;
    /** The CRC-32 of the name excluding the terminator. (lazy bird) */
    uint32_t        u32NameCRC;
} SSMFILEDIRENTRY;
AssertCompileSize(SSMFILEDIRENTRY, 16);
/** Pointer to a directory entry. */
typedef SSMFILEDIRENTRY *PSSMFILEDIRENTRY;
/** Pointer to a const directory entry. */
typedef SSMFILEDIRENTRY const *PCSSMFILEDIRENTRY;

/**
 * Directory for the data units from the final pass.
 *
 * This is used to speed up SSMR3Seek (it would have to decompress and parse the
 * whole stream otherwise).
 */
typedef struct SSMFILEDIR
{
    /** Magic string (SSMFILEDIR_MAGIC). */
    char            szMagic[8];
    /** The CRC-32 for the whole directory.
     * Calculated with this field set to zero. */
    uint32_t        u32CRC;
    /** The number of directory entries. */
    uint32_t        cEntries;
    /** The directory entries (variable size). */
    SSMFILEDIRENTRY aEntries[1];
} SSMFILEDIR;
AssertCompileSize(SSMFILEDIR, 32);
/** Pointer to a directory. */
typedef SSMFILEDIR *PSSMFILEDIR;
/** Pointer to a const directory. */
typedef SSMFILEDIR *PSSMFILEDIR;


/**
 * Footer structure
 */
typedef struct SSMFILEFTR
{
    /** Magic string (SSMFILEFTR_MAGIC). */
    char            szMagic[8];
    /** The offset of this record in the stream. */
    uint64_t        offStream;
    /** The CRC for the stream.
     * This is set to zero if SSMFILEHDR_FLAGS_STREAM_CRC32 is clear. */
    uint32_t        u32StreamCRC;
    /** Number directory entries. */
    uint32_t        cDirEntries;
    /** Reserved footer space - must be zero. */
    uint32_t        u32Reserved;
    /** The CRC-32 for this structure.
     * Calculated with this field set to zero. */
    uint32_t        u32CRC;
} SSMFILEFTR;
AssertCompileSize(SSMFILEFTR, 32);
/** Pointer to a footer. */
typedef SSMFILEFTR *PSSMFILEFTR;
/** Pointer to a const footer. */
typedef SSMFILEFTR const *PCSSMFILEFTR;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifndef SSM_STANDALONE
/** Zeros used by the struct putter.
 * This must be at least 8 bytes or the code breaks. */
static uint8_t const    g_abZero[_1K] = {0};
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#ifndef SSM_STANDALONE
static int                  ssmR3LazyInit(PVM pVM);
static DECLCALLBACK(int)    ssmR3SelfLiveExec(PVM pVM, PSSMHANDLE pSSM, uint32_t uPass);
static DECLCALLBACK(int)    ssmR3SelfSaveExec(PVM pVM, PSSMHANDLE pSSM);
static DECLCALLBACK(int)    ssmR3SelfLoadExec(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass);
static DECLCALLBACK(int)    ssmR3LiveControlLoadExec(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass);
static int                  ssmR3Register(PVM pVM, const char *pszName, uint32_t uInstance, uint32_t uVersion, size_t cbGuess, const char *pszBefore, PSSMUNIT *ppUnit);
static int                  ssmR3LiveControlEmit(PSSMHANDLE pSSM, long double lrdPct, uint32_t uPass);
#endif

static int                  ssmR3StrmWriteBuffers(PSSMSTRM pStrm);
static int                  ssmR3StrmReadMore(PSSMSTRM pStrm);

#ifndef SSM_STANDALONE
static int                  ssmR3DataFlushBuffer(PSSMHANDLE pSSM);
#endif
static int                  ssmR3DataReadRecHdrV2(PSSMHANDLE pSSM);


#ifndef SSM_STANDALONE

/**
 * Cleans up resources allocated by SSM on VM termination.
 *
 * @param   pVM                 The cross context VM structure.
 * @note    Not using VMMR3_INT_DECL because of testcases.
 */
VMMR3DECL(void) SSMR3Term(PVM pVM)
{
    if (pVM->ssm.s.fInitialized)
    {
        pVM->ssm.s.fInitialized = false;
        RTCritSectDelete(&pVM->ssm.s.CancelCritSect);
    }
}


/**
 * Performs lazy initialization of the SSM.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
static int ssmR3LazyInit(PVM pVM)
{
    /*
     * Register a saved state unit which we use to put the VirtualBox version,
     * revision and similar stuff in.
     */
    pVM->ssm.s.fInitialized = true;
    int rc = SSMR3RegisterInternal(pVM, "SSM", 0 /*uInstance*/, 1 /*uVersion*/, 64 /*cbGuess*/,
                                   NULL /*pfnLivePrep*/, ssmR3SelfLiveExec, NULL /*pfnLiveVote*/,
                                   NULL /*pfnSavePrep*/, ssmR3SelfSaveExec, NULL /*pfnSaveDone*/,
                                   NULL /*pfnSavePrep*/, ssmR3SelfLoadExec, NULL /*pfnSaveDone*/);
    if (RT_SUCCESS(rc))
        rc = SSMR3RegisterInternal(pVM, "SSMLiveControl", 0 /*uInstance*/, 1 /*uVersion*/, 1 /*cbGuess*/,
                                   NULL /*pfnLivePrep*/, NULL /*pfnLiveExec*/,     NULL /*pfnLiveVote*/,
                                   NULL /*pfnSavePrep*/, NULL /*pfnSaveExec*/,     NULL /*pfnSaveDone*/,
                                   NULL /*pfnSavePrep*/, ssmR3LiveControlLoadExec, NULL /*pfnSaveDone*/);

    /*
     * Initialize the cancellation critsect now.
     */
    if (RT_SUCCESS(rc))
        rc = RTCritSectInitNamed(&pVM->ssm.s.CancelCritSect, "SSM-CancelCritSect");
    if (RT_SUCCESS(rc))
    {
        STAM_REL_REG_USED(pVM, &pVM->ssm.s.uPass, STAMTYPE_U32, "/SSM/uPass", STAMUNIT_COUNT, "Current pass");
    }

    pVM->ssm.s.fInitialized = RT_SUCCESS(rc);
    return rc;
}


/**
 * Do ssmR3SelfSaveExec in pass 0.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pSSM            The SSM handle.
 * @param   uPass           The data pass number.
 */
static DECLCALLBACK(int) ssmR3SelfLiveExec(PVM pVM, PSSMHANDLE pSSM, uint32_t uPass)
{
    if (uPass == 0)
    {
        int rc = ssmR3SelfSaveExec(pVM, pSSM);
        if (RT_SUCCESS(rc))
            rc = VINF_SSM_DONT_CALL_AGAIN;
        return rc;
    }
    AssertFailed();
    return VERR_SSM_UNEXPECTED_PASS;
}


/**
 * For saving usful things without having to go thru the tedious process of
 * adding it to the header.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pSSM            The SSM handle.
 */
static DECLCALLBACK(int) ssmR3SelfSaveExec(PVM pVM, PSSMHANDLE pSSM)
{
    NOREF(pVM);

    /*
     * String table containing pairs of variable and value string.
     * Terminated by two empty strings.
     */
    SSMR3PutStrZ(pSSM, "Build Type");
    SSMR3PutStrZ(pSSM, KBUILD_TYPE);
    SSMR3PutStrZ(pSSM, "Host OS");
    SSMR3PutStrZ(pSSM, KBUILD_TARGET "." KBUILD_TARGET_ARCH);
#ifdef VBOX_OSE
    SSMR3PutStrZ(pSSM, "OSE");
    SSMR3PutStrZ(pSSM, "true");
#endif

    /* terminator */
    SSMR3PutStrZ(pSSM, "");
    return SSMR3PutStrZ(pSSM, "");
}


/**
 * For load the version + revision and stuff.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pSSM            The SSM handle.
 * @param   uVersion        The version (1).
 * @param   uPass           The pass.
 */
static DECLCALLBACK(int) ssmR3SelfLoadExec(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    AssertLogRelMsgReturn(uVersion == 1, ("%d\n", uVersion), VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION);
    NOREF(pVM); NOREF(uPass);

    /*
     * The first and last passes contains a {name, value} string table that is
     * terminated by two emptry strings.  It contains useful informal build
     * info and can be very handy when something goes wrong after restore.
     */
    if (    uPass == 0
        ||  uPass == SSM_PASS_FINAL)
    {
        for (unsigned i = 0; ; i++)
        {
            char szVar[128];
            char szValue[1024];
            int rc = SSMR3GetStrZ(pSSM, szVar, sizeof(szVar));
            AssertRCReturn(rc, rc);
            rc = SSMR3GetStrZ(pSSM, szValue, sizeof(szValue));
            AssertRCReturn(rc, rc);
            if (!szVar[0] && !szValue[0])
                break;
            if (i == 0)
                LogRel(("SSM: Saved state info:\n"));
            LogRel(("SSM:   %s: %s\n", szVar, szValue));

            /*
             * Detect 32-bit MSC for handling SSMFIELD_ENTRY_PAD_MSC32_AUTO.
             * Save the Host OS for SSMR3HandleHostOSAndArch
             */
            if (!strcmp(szVar, "Host OS"))
            {
                bool fIsHostMsc32 = !strcmp(szValue, "win.x86");
                if (fIsHostMsc32 != pSSM->u.Read.fIsHostMsc32)
                {
                    LogRel(("SSM: (fIsHostMsc32 %RTbool => %RTbool)\n", pSSM->u.Read.fIsHostMsc32, fIsHostMsc32));
                    pSSM->u.Read.fIsHostMsc32 = fIsHostMsc32;
                }

                size_t cchValue = strlen(szValue);
                size_t cchCopy = RT_MIN(cchValue, sizeof(pSSM->u.Read.szHostOSAndArch) - 1);
                Assert(cchValue == cchCopy);
                memcpy(pSSM->u.Read.szHostOSAndArch, szValue, cchCopy);
                pSSM->u.Read.szHostOSAndArch[cchCopy] = '\0';
            }
        }
    }
    return VINF_SUCCESS;
}


/**
 * Load exec callback for the special live save state unit that tracks the
 * progress of a live save.
 *
 * This is saved by ssmR3LiveControlEmit().
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pSSM            The SSM handle.
 * @param   uVersion        The version (1).
 * @param   uPass           The pass.
 */
static DECLCALLBACK(int) ssmR3LiveControlLoadExec(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    AssertLogRelMsgReturn(uVersion == 1, ("%d\n", uVersion), VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION);
    NOREF(uPass);

    uint16_t uPartsPerTenThousand;
    int rc = SSMR3GetU16(pSSM, &uPartsPerTenThousand);
    if (RT_SUCCESS(rc))
    {
        /* Scale it down to fit in our exec range. */
        unsigned uPct = (unsigned)(  (long double)uPartsPerTenThousand / 100
                                   * (100 - pSSM->uPercentPrepare - pSSM->uPercentDone) / 100)
                      + pSSM->uPercentPrepare;
        if (uPct != pSSM->uPercent)
        {
            AssertMsg(uPct < 100, ("uPct=%d uPartsPerTenThousand=%d uPercentPrepare=%d uPercentDone=%d\n", uPct, uPartsPerTenThousand, pSSM->uPercentPrepare, pSSM->uPercentDone));
            pSSM->uPercent = uPct;
            if (pSSM->pfnProgress)
                pSSM->pfnProgress(pVM->pUVM, RT_MIN(uPct, 100 - pSSM->uPercentDone), pSSM->pvUser);
        }
    }
    return rc;
}


/**
 * Internal registration worker.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pszName         Data unit name.
 * @param   uInstance       The instance id.
 * @param   uVersion        The data unit version.
 * @param   cbGuess         The guessed data unit size.
 * @param   pszBefore       Name of data unit to be placed in front of.
 *                          Optional.
 * @param   ppUnit          Where to store the inserted unit node.
 *                          Caller must fill in the missing details.
 */
static int ssmR3Register(PVM pVM, const char *pszName, uint32_t uInstance,
                         uint32_t uVersion, size_t cbGuess, const char *pszBefore, PSSMUNIT *ppUnit)
{
    /*
     * Validate input.
     */
    AssertPtr(pszName);
    AssertReturn(*pszName, VERR_INVALID_PARAMETER);
    size_t cchName = strlen(pszName);
    AssertMsgReturn(cchName < SSM_MAX_NAME_SIZE, ("%zu >= %u: %s\n", cchName, SSM_MAX_NAME_SIZE, pszName), VERR_OUT_OF_RANGE);

    AssertReturn(!pszBefore || *pszBefore, VERR_INVALID_PARAMETER);
    size_t cchBefore = pszBefore ? strlen(pszBefore) : 0;
    AssertMsgReturn(cchBefore < SSM_MAX_NAME_SIZE, ("%zu >= %u: %s\n", cchBefore, SSM_MAX_NAME_SIZE, pszBefore), VERR_OUT_OF_RANGE);

    /*
     * Lazy init.
     */
    if (!pVM->ssm.s.fInitialized)
    {
        int rc = ssmR3LazyInit(pVM);
        AssertRCReturn(rc, rc);
    }

    /*
     * Walk to the end of the list checking for duplicates as we go.
     */
    PSSMUNIT    pUnitBeforePrev = NULL;
    PSSMUNIT    pUnitBefore     = NULL;
    PSSMUNIT    pUnitPrev       = NULL;
    PSSMUNIT    pUnit           = pVM->ssm.s.pHead;
    while (pUnit)
    {
        if (    pUnit->u32Instance == uInstance
            &&  pUnit->cchName == cchName
            &&  !memcmp(pUnit->szName, pszName, cchName))
        {
            AssertMsgFailed(("Duplicate registration %s\n", pszName));
            return VERR_SSM_UNIT_EXISTS;
        }
        if (    pUnit->cchName == cchBefore
            &&  !pUnitBefore
            &&  !memcmp(pUnit->szName, pszBefore, cchBefore))
        {
            pUnitBeforePrev = pUnitPrev;
            pUnitBefore     = pUnit;
        }

        /* next */
        pUnitPrev = pUnit;
        pUnit = pUnit->pNext;
    }

    /*
     * Allocate new node.
     */
    pUnit = (PSSMUNIT)MMR3HeapAllocZ(pVM, MM_TAG_SSM, RT_UOFFSETOF_DYN(SSMUNIT, szName[cchName + 1]));
    if (!pUnit)
        return VERR_NO_MEMORY;

    /*
     * Fill in (some) data. (Stuff is zero'd.)
     */
    pUnit->u32Version   = uVersion;
    pUnit->u32Instance  = uInstance;
    pUnit->cbGuess      = cbGuess;
    pUnit->cchName      = cchName;
    memcpy(pUnit->szName, pszName, cchName);

    /*
     * Insert
     */
    if (pUnitBefore)
    {
        pUnit->pNext = pUnitBefore;
        if (pUnitBeforePrev)
            pUnitBeforePrev->pNext = pUnit;
        else
            pVM->ssm.s.pHead       = pUnit;
    }
    else if (pUnitPrev)
        pUnitPrev->pNext = pUnit;
    else
        pVM->ssm.s.pHead = pUnit;
    pVM->ssm.s.cUnits++;

    *ppUnit = pUnit;
    return VINF_SUCCESS;
}


/**
 * Register a PDM Devices data unit.
 *
 * @returns VBox status code.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pDevIns         Device instance.
 * @param   pszName         Data unit name.
 * @param   uInstance       The instance identifier of the data unit.
 *                          This must together with the name be unique.
 * @param   uVersion        Data layout version number.
 * @param   cbGuess         The approximate amount of data in the unit.
 *                          Only for progress indicators.
 * @param   pszBefore       Name of data unit which we should be put in front
 *                          of. Optional (NULL).
 *
 * @param   pfnLivePrep     Prepare live save callback, optional.
 * @param   pfnLiveExec     Execute live save callback, optional.
 * @param   pfnLiveVote     Vote live save callback, optional.
 *
 * @param   pfnSavePrep     Prepare save callback, optional.
 * @param   pfnSaveExec     Execute save callback, optional.
 * @param   pfnSaveDone     Done save callback, optional.
 *
 * @param   pfnLoadPrep     Prepare load callback, optional.
 * @param   pfnLoadExec     Execute load callback, optional.
 * @param   pfnLoadDone     Done load callback, optional.
 */
VMMR3_INT_DECL(int)
SSMR3RegisterDevice(PVM pVM, PPDMDEVINS pDevIns, const char *pszName,
                    uint32_t uInstance, uint32_t uVersion, size_t cbGuess, const char *pszBefore,
                    PFNSSMDEVLIVEPREP pfnLivePrep, PFNSSMDEVLIVEEXEC pfnLiveExec, PFNSSMDEVLIVEVOTE pfnLiveVote,
                    PFNSSMDEVSAVEPREP pfnSavePrep, PFNSSMDEVSAVEEXEC pfnSaveExec, PFNSSMDEVSAVEDONE pfnSaveDone,
                    PFNSSMDEVLOADPREP pfnLoadPrep, PFNSSMDEVLOADEXEC pfnLoadExec, PFNSSMDEVLOADDONE pfnLoadDone)
{
    PSSMUNIT pUnit;
    int rc = ssmR3Register(pVM, pszName, uInstance, uVersion, cbGuess, pszBefore, &pUnit);
    if (RT_SUCCESS(rc))
    {
        pUnit->enmType = SSMUNITTYPE_DEV;
        pUnit->u.Dev.pfnLivePrep = pfnLivePrep;
        pUnit->u.Dev.pfnLiveExec = pfnLiveExec;
        pUnit->u.Dev.pfnLiveVote = pfnLiveVote;
        pUnit->u.Dev.pfnSavePrep = pfnSavePrep;
        pUnit->u.Dev.pfnSaveExec = pfnSaveExec;
        pUnit->u.Dev.pfnSaveDone = pfnSaveDone;
        pUnit->u.Dev.pfnLoadPrep = pfnLoadPrep;
        pUnit->u.Dev.pfnLoadExec = pfnLoadExec;
        pUnit->u.Dev.pfnLoadDone = pfnLoadDone;
        pUnit->u.Dev.pDevIns = pDevIns;
        pUnit->pCritSect = PDMR3DevGetCritSect(pVM, pDevIns);
    }
    return rc;
}


/**
 * Register a PDM driver data unit.
 *
 * @returns VBox status code.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pDrvIns         Driver instance.
 * @param   pszName         Data unit name.
 * @param   uInstance       The instance identifier of the data unit.
 *                          This must together with the name be unique.
 * @param   uVersion        Data layout version number.
 * @param   cbGuess         The approximate amount of data in the unit.
 *                          Only for progress indicators.
 *
 * @param   pfnLivePrep     Prepare live save callback, optional.
 * @param   pfnLiveExec     Execute live save callback, optional.
 * @param   pfnLiveVote     Vote live save callback, optional.
 *
 * @param   pfnSavePrep     Prepare save callback, optional.
 * @param   pfnSaveExec     Execute save callback, optional.
 * @param   pfnSaveDone     Done save callback, optional.
 *
 * @param   pfnLoadPrep     Prepare load callback, optional.
 * @param   pfnLoadExec     Execute load callback, optional.
 * @param   pfnLoadDone     Done load callback, optional.
 */
VMMR3_INT_DECL(int)
SSMR3RegisterDriver(PVM pVM, PPDMDRVINS pDrvIns, const char *pszName, uint32_t uInstance, uint32_t uVersion, size_t cbGuess,
                    PFNSSMDRVLIVEPREP pfnLivePrep, PFNSSMDRVLIVEEXEC pfnLiveExec, PFNSSMDRVLIVEVOTE pfnLiveVote,
                    PFNSSMDRVSAVEPREP pfnSavePrep, PFNSSMDRVSAVEEXEC pfnSaveExec, PFNSSMDRVSAVEDONE pfnSaveDone,
                    PFNSSMDRVLOADPREP pfnLoadPrep, PFNSSMDRVLOADEXEC pfnLoadExec, PFNSSMDRVLOADDONE pfnLoadDone)
{
    PSSMUNIT pUnit;
    int rc = ssmR3Register(pVM, pszName, uInstance, uVersion, cbGuess, NULL, &pUnit);
    if (RT_SUCCESS(rc))
    {
        pUnit->enmType = SSMUNITTYPE_DRV;
        pUnit->u.Drv.pfnLivePrep = pfnLivePrep;
        pUnit->u.Drv.pfnLiveExec = pfnLiveExec;
        pUnit->u.Drv.pfnLiveVote = pfnLiveVote;
        pUnit->u.Drv.pfnSavePrep = pfnSavePrep;
        pUnit->u.Drv.pfnSaveExec = pfnSaveExec;
        pUnit->u.Drv.pfnSaveDone = pfnSaveDone;
        pUnit->u.Drv.pfnLoadPrep = pfnLoadPrep;
        pUnit->u.Drv.pfnLoadExec = pfnLoadExec;
        pUnit->u.Drv.pfnLoadDone = pfnLoadDone;
        pUnit->u.Drv.pDrvIns = pDrvIns;
    }
    return rc;
}


/**
 * Register a PDM USB device data unit.
 *
 * @returns VBox status code.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pUsbIns         USB instance.
 * @param   pszName         Data unit name.
 * @param   uInstance       The instance identifier of the data unit.
 *                          This must together with the name be unique.
 * @param   uVersion        Data layout version number.
 * @param   cbGuess         The approximate amount of data in the unit.
 *                          Only for progress indicators.
 *
 * @param   pfnLivePrep     Prepare live save callback, optional.
 * @param   pfnLiveExec     Execute live save callback, optional.
 * @param   pfnLiveVote     Vote live save callback, optional.
 *
 * @param   pfnSavePrep     Prepare save callback, optional.
 * @param   pfnSaveExec     Execute save callback, optional.
 * @param   pfnSaveDone     Done save callback, optional.
 *
 * @param   pfnLoadPrep     Prepare load callback, optional.
 * @param   pfnLoadExec     Execute load callback, optional.
 * @param   pfnLoadDone     Done load callback, optional.
 */
VMMR3_INT_DECL(int)
SSMR3RegisterUsb(PVM pVM, PPDMUSBINS pUsbIns, const char *pszName, uint32_t uInstance, uint32_t uVersion, size_t cbGuess,
                 PFNSSMUSBLIVEPREP pfnLivePrep, PFNSSMUSBLIVEEXEC pfnLiveExec, PFNSSMUSBLIVEVOTE pfnLiveVote,
                 PFNSSMUSBSAVEPREP pfnSavePrep, PFNSSMUSBSAVEEXEC pfnSaveExec, PFNSSMUSBSAVEDONE pfnSaveDone,
                 PFNSSMUSBLOADPREP pfnLoadPrep, PFNSSMUSBLOADEXEC pfnLoadExec, PFNSSMUSBLOADDONE pfnLoadDone)
{
    PSSMUNIT pUnit;
    int rc = ssmR3Register(pVM, pszName, uInstance, uVersion, cbGuess, NULL, &pUnit);
    if (RT_SUCCESS(rc))
    {
        pUnit->enmType = SSMUNITTYPE_USB;
        pUnit->u.Usb.pfnLivePrep = pfnLivePrep;
        pUnit->u.Usb.pfnLiveExec = pfnLiveExec;
        pUnit->u.Usb.pfnLiveVote = pfnLiveVote;
        pUnit->u.Usb.pfnSavePrep = pfnSavePrep;
        pUnit->u.Usb.pfnSaveExec = pfnSaveExec;
        pUnit->u.Usb.pfnSaveDone = pfnSaveDone;
        pUnit->u.Usb.pfnLoadPrep = pfnLoadPrep;
        pUnit->u.Usb.pfnLoadExec = pfnLoadExec;
        pUnit->u.Usb.pfnLoadDone = pfnLoadDone;
        pUnit->u.Usb.pUsbIns = pUsbIns;
    }
    return rc;
}


/**
 * Register a internal data unit.
 *
 * @returns VBox status code.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pszName         Data unit name.
 * @param   uInstance       The instance identifier of the data unit.
 *                          This must together with the name be unique.
 * @param   uVersion        Data layout version number.
 * @param   cbGuess         The approximate amount of data in the unit.
 *                          Only for progress indicators.
 *
 * @param   pfnLivePrep     Prepare live save callback, optional.
 * @param   pfnLiveExec     Execute live save callback, optional.
 * @param   pfnLiveVote     Vote live save callback, optional.
 *
 * @param   pfnSavePrep     Prepare save callback, optional.
 * @param   pfnSaveExec     Execute save callback, optional.
 * @param   pfnSaveDone     Done save callback, optional.
 *
 * @param   pfnLoadPrep     Prepare load callback, optional.
 * @param   pfnLoadExec     Execute load callback, optional.
 * @param   pfnLoadDone     Done load callback, optional.
 */
VMMR3DECL(int) SSMR3RegisterInternal(PVM pVM, const char *pszName, uint32_t uInstance, uint32_t uVersion, size_t cbGuess,
    PFNSSMINTLIVEPREP pfnLivePrep, PFNSSMINTLIVEEXEC pfnLiveExec, PFNSSMINTLIVEVOTE pfnLiveVote,
    PFNSSMINTSAVEPREP pfnSavePrep, PFNSSMINTSAVEEXEC pfnSaveExec, PFNSSMINTSAVEDONE pfnSaveDone,
    PFNSSMINTLOADPREP pfnLoadPrep, PFNSSMINTLOADEXEC pfnLoadExec, PFNSSMINTLOADDONE pfnLoadDone)
{
    PSSMUNIT pUnit;
    int rc = ssmR3Register(pVM, pszName, uInstance, uVersion, cbGuess, NULL /* pszBefore */, &pUnit);
    if (RT_SUCCESS(rc))
    {
        pUnit->enmType = SSMUNITTYPE_INTERNAL;
        pUnit->u.Internal.pfnLivePrep = pfnLivePrep;
        pUnit->u.Internal.pfnLiveExec = pfnLiveExec;
        pUnit->u.Internal.pfnLiveVote = pfnLiveVote;
        pUnit->u.Internal.pfnSavePrep = pfnSavePrep;
        pUnit->u.Internal.pfnSaveExec = pfnSaveExec;
        pUnit->u.Internal.pfnSaveDone = pfnSaveDone;
        pUnit->u.Internal.pfnLoadPrep = pfnLoadPrep;
        pUnit->u.Internal.pfnLoadExec = pfnLoadExec;
        pUnit->u.Internal.pfnLoadDone = pfnLoadDone;
    }
    return rc;
}


/**
 * Register an external data unit.
 *
 * @returns VBox status code.
 *
 * @param   pUVM            The user mode VM handle.
 * @param   pszName         Data unit name.
 * @param   uInstance       The instance identifier of the data unit.
 *                          This must together with the name be unique.
 * @param   uVersion        Data layout version number.
 * @param   cbGuess         The approximate amount of data in the unit.
 *                          Only for progress indicators.
 *
 * @param   pfnLivePrep     Prepare live save callback, optional.
 * @param   pfnLiveExec     Execute live save callback, optional.
 * @param   pfnLiveVote     Vote live save callback, optional.
 *
 * @param   pfnSavePrep     Prepare save callback, optional.
 * @param   pfnSaveExec     Execute save callback, optional.
 * @param   pfnSaveDone     Done save callback, optional.
 *
 * @param   pfnLoadPrep     Prepare load callback, optional.
 * @param   pfnLoadExec     Execute load callback, optional.
 * @param   pfnLoadDone     Done load callback, optional.
 * @param   pvUser          User argument.
 */
VMMR3DECL(int) SSMR3RegisterExternal(PUVM pUVM, const char *pszName, uint32_t uInstance, uint32_t uVersion, size_t cbGuess,
    PFNSSMEXTLIVEPREP pfnLivePrep, PFNSSMEXTLIVEEXEC pfnLiveExec, PFNSSMEXTLIVEVOTE pfnLiveVote,
    PFNSSMEXTSAVEPREP pfnSavePrep, PFNSSMEXTSAVEEXEC pfnSaveExec, PFNSSMEXTSAVEDONE pfnSaveDone,
    PFNSSMEXTLOADPREP pfnLoadPrep, PFNSSMEXTLOADEXEC pfnLoadExec, PFNSSMEXTLOADDONE pfnLoadDone, void *pvUser)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    PSSMUNIT pUnit;
    int rc = ssmR3Register(pVM, pszName, uInstance, uVersion, cbGuess, NULL /* pszBefore */, &pUnit);
    if (RT_SUCCESS(rc))
    {
        pUnit->enmType = SSMUNITTYPE_EXTERNAL;
        pUnit->u.External.pfnLivePrep = pfnLivePrep;
        pUnit->u.External.pfnLiveExec = pfnLiveExec;
        pUnit->u.External.pfnLiveVote = pfnLiveVote;
        pUnit->u.External.pfnSavePrep = pfnSavePrep;
        pUnit->u.External.pfnSaveExec = pfnSaveExec;
        pUnit->u.External.pfnSaveDone = pfnSaveDone;
        pUnit->u.External.pfnLoadPrep = pfnLoadPrep;
        pUnit->u.External.pfnLoadExec = pfnLoadExec;
        pUnit->u.External.pfnLoadDone = pfnLoadDone;
        pUnit->u.External.pvUser      = pvUser;
    }
    return rc;
}


/**
 * @callback_method_impl{FNSSMINTLOADEXEC,
 * Stub that skips the whole unit (see SSMR3RegisterStub).}
 */
static DECLCALLBACK(int) ssmR3LoadExecStub(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    NOREF(pVM); NOREF(uVersion); NOREF(uPass);
    return SSMR3SkipToEndOfUnit(pSSM);
}


/**
 * Registers a stub state loader for working around legacy.
 *
 * This is used to deal with irelevant PATM and CSAM saved state units in HM
 * mode and when built without raw-mode.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pszName             Data unit name.
 * @param   uInstance           Instance number.
 */
VMMR3DECL(int) SSMR3RegisterStub(PVM pVM, const char *pszName, uint32_t uInstance)
{
    return SSMR3RegisterInternal(pVM, pszName, uInstance, UINT32_MAX, 0,
                                 NULL, NULL, NULL,
                                 NULL, NULL, NULL,
                                 NULL, ssmR3LoadExecStub, NULL);
}


/**
 * Deregister one or more PDM Device data units.
 *
 * @returns VBox status code.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pDevIns         Device instance.
 * @param   pszName         Data unit name.
 *                          Use NULL to deregister all data units for that device instance.
 * @param   uInstance       The instance identifier of the data unit.
 *                          This must together with the name be unique.
 * @remark  Only for dynamic data units and dynamic unloaded modules.
 */
VMMR3_INT_DECL(int) SSMR3DeregisterDevice(PVM pVM, PPDMDEVINS pDevIns, const char *pszName, uint32_t uInstance)
{
    /*
     * Validate input.
     */
    if (!pDevIns)
    {
        AssertMsgFailed(("pDevIns is NULL!\n"));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Search the list.
     */
    size_t      cchName = pszName ? strlen(pszName) : 0;
    int         rc = pszName ? VERR_SSM_UNIT_NOT_FOUND : VINF_SUCCESS;
    PSSMUNIT    pUnitPrev = NULL;
    PSSMUNIT    pUnit = pVM->ssm.s.pHead;
    while (pUnit)
    {
        if (    pUnit->enmType == SSMUNITTYPE_DEV
            &&  (   !pszName
                 || (   pUnit->cchName == cchName
                     && !memcmp(pUnit->szName, pszName, cchName)))
            &&  pUnit->u32Instance == uInstance
            )
        {
            if (pUnit->u.Dev.pDevIns == pDevIns)
            {
                /*
                 * Unlink it, advance pointer, and free the node.
                 */
                PSSMUNIT pFree = pUnit;
                pUnit = pUnit->pNext;
                if (pUnitPrev)
                    pUnitPrev->pNext = pUnit;
                else
                    pVM->ssm.s.pHead = pUnit;
                pVM->ssm.s.cUnits--;
                Log(("SSM: Removed data unit '%s' (pdm dev).\n", pFree->szName));
                MMR3HeapFree(pFree);

                if (pszName)
                    return VINF_SUCCESS;
                rc = VINF_SUCCESS;
                continue;
            }
            else if (pszName)
            {
                AssertMsgFailed(("Caller is not owner! Owner=%p Caller=%p %s\n",
                                 pUnit->u.Dev.pDevIns, pDevIns, pszName));
                return VERR_SSM_UNIT_NOT_OWNER;
            }
        }

        /* next */
        pUnitPrev = pUnit;
        pUnit = pUnit->pNext;
    }

    return rc;
}


/**
 * Deregister one ore more PDM Driver data units.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pDrvIns         Driver instance.
 * @param   pszName         Data unit name.
 *                          Use NULL to deregister all data units for that driver instance.
 * @param   uInstance       The instance identifier of the data unit.
 *                          This must together with the name be unique. Ignored if pszName is NULL.
 * @remark  Only for dynamic data units and dynamic unloaded modules.
 */
VMMR3_INT_DECL(int) SSMR3DeregisterDriver(PVM pVM, PPDMDRVINS pDrvIns, const char *pszName, uint32_t uInstance)
{
    /*
     * Validate input.
     */
    if (!pDrvIns)
    {
        AssertMsgFailed(("pDrvIns is NULL!\n"));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Search the list.
     */
    size_t      cchName = pszName ? strlen(pszName) : 0;
    int         rc = pszName ? VERR_SSM_UNIT_NOT_FOUND : VINF_SUCCESS;
    PSSMUNIT    pUnitPrev = NULL;
    PSSMUNIT    pUnit = pVM->ssm.s.pHead;
    while (pUnit)
    {
        if (    pUnit->enmType == SSMUNITTYPE_DRV
            &&  (   !pszName
                 || (   pUnit->cchName == cchName
                     && !memcmp(pUnit->szName, pszName, cchName)
                     && pUnit->u32Instance == uInstance))
            )
        {
            if (pUnit->u.Drv.pDrvIns == pDrvIns)
            {
                /*
                 * Unlink it, advance pointer, and free the node.
                 */
                PSSMUNIT pFree = pUnit;
                pUnit = pUnit->pNext;
                if (pUnitPrev)
                    pUnitPrev->pNext = pUnit;
                else
                    pVM->ssm.s.pHead = pUnit;
                pVM->ssm.s.cUnits--;
                Log(("SSM: Removed data unit '%s' (pdm drv).\n", pFree->szName));
                MMR3HeapFree(pFree);

                if (pszName)
                    return VINF_SUCCESS;
                rc = VINF_SUCCESS;
                continue;
            }

            AssertMsgReturn(!pszName,
                            ("Caller is not owner! Owner=%p Caller=%p %s\n", pUnit->u.Drv.pDrvIns, pDrvIns, pszName),
                            VERR_SSM_UNIT_NOT_OWNER);
        }

        /* next */
        pUnitPrev = pUnit;
        pUnit = pUnit->pNext;
    }

    return rc;
}


/**
 * Deregister one or more PDM USB device data units.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pUsbIns         USB device instance.
 * @param   pszName         Data unit name.
 *                          Use NULL to deregister all data units for that driver instance.
 * @param   uInstance       The instance identifier of the data unit.
 *                          This must together with the name be unique. Ignored if pszName is NULL.
 * @remark  Only for dynamic data units and dynamic unloaded modules.
 */
VMMR3_INT_DECL(int) SSMR3DeregisterUsb(PVM pVM, PPDMUSBINS pUsbIns, const char *pszName, uint32_t uInstance)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pUsbIns, VERR_INVALID_POINTER);

    /*
     * Search the list.
     */
    size_t      cchName = pszName ? strlen(pszName) : 0;
    int         rc = pszName ? VERR_SSM_UNIT_NOT_FOUND : VINF_SUCCESS;
    PSSMUNIT    pUnitPrev = NULL;
    PSSMUNIT    pUnit = pVM->ssm.s.pHead;
    while (pUnit)
    {
        if (    pUnit->enmType == SSMUNITTYPE_USB
            &&  (   !pszName
                 || (   pUnit->cchName == cchName
                     && !memcmp(pUnit->szName, pszName, cchName)
                     && pUnit->u32Instance == uInstance))
            )
        {
            if (pUnit->u.Usb.pUsbIns == pUsbIns)
            {
                /*
                 * Unlink it, advance pointer, and free the node.
                 */
                PSSMUNIT pFree = pUnit;
                pUnit = pUnit->pNext;
                if (pUnitPrev)
                    pUnitPrev->pNext = pUnit;
                else
                    pVM->ssm.s.pHead = pUnit;
                pVM->ssm.s.cUnits--;
                Log(("SSM: Removed data unit '%s' (pdm drv).\n", pFree->szName));
                MMR3HeapFree(pFree);

                if (pszName)
                    return VINF_SUCCESS;
                rc = VINF_SUCCESS;
                continue;
            }

            AssertMsgReturn(!pszName,
                            ("Caller is not owner! Owner=%p Caller=%p %s\n", pUnit->u.Usb.pUsbIns, pUsbIns, pszName),
                            VERR_SSM_UNIT_NOT_OWNER);
        }

        /* next */
        pUnitPrev = pUnit;
        pUnit = pUnit->pNext;
    }

    return rc;
}


/**
 * Deregister a data unit.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   enmType         Unit type
 * @param   pszName         Data unit name.
 * @remark  Only for dynamic data units.
 */
static int ssmR3DeregisterByNameAndType(PVM pVM, const char *pszName, SSMUNITTYPE enmType)
{
    /*
     * Validate input.
     */
    if (!pszName)
    {
        AssertMsgFailed(("pszName is NULL!\n"));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Search the list.
     */
    size_t      cchName = strlen(pszName);
    int         rc = VERR_SSM_UNIT_NOT_FOUND;
    PSSMUNIT    pUnitPrev = NULL;
    PSSMUNIT    pUnit = pVM->ssm.s.pHead;
    while (pUnit)
    {
        if (    pUnit->enmType == enmType
            &&  pUnit->cchName == cchName
            &&  !memcmp(pUnit->szName, pszName, cchName))
        {
            /*
             * Unlink it, advance pointer, and free the node.
             */
            PSSMUNIT pFree = pUnit;
            pUnit = pUnit->pNext;
            if (pUnitPrev)
                pUnitPrev->pNext = pUnit;
            else
                pVM->ssm.s.pHead = pUnit;
            pVM->ssm.s.cUnits--;
            Log(("SSM: Removed data unit '%s' (type=%d).\n", pFree->szName, enmType));
            MMR3HeapFree(pFree);
            return VINF_SUCCESS;
        }

        /* next */
        pUnitPrev = pUnit;
        pUnit = pUnit->pNext;
    }

    return rc;
}


/**
 * Deregister an internal data unit.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pszName         Data unit name.
 * @remark  Only for dynamic data units.
 */
VMMR3DECL(int) SSMR3DeregisterInternal(PVM pVM, const char *pszName)
{
    return ssmR3DeregisterByNameAndType(pVM, pszName, SSMUNITTYPE_INTERNAL);
}


/**
 * Deregister an external data unit.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM structure.
 * @param   pszName         Data unit name.
 * @remark  Only for dynamic data units.
 */
VMMR3DECL(int) SSMR3DeregisterExternal(PUVM pUVM, const char *pszName)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    return ssmR3DeregisterByNameAndType(pVM, pszName, SSMUNITTYPE_EXTERNAL);
}

#endif /* !SSM_STANDALONE */


/**
 * Initializes the stream after/before opening the file/whatever.
 *
 * @returns VINF_SUCCESS or VERR_NO_MEMORY.
 * @param   pStrm           The stream handle.
 * @param   fChecksummed    Whether the stream is to be checksummed while
 *                          written/read.
 * @param   cBuffers        The number of buffers.
 */
static int ssmR3StrmInitInternal(PSSMSTRM pStrm, bool fChecksummed, uint32_t cBuffers)
{
    Assert(cBuffers > 0);

    /*
     * Init the common data members.
     */
    pStrm->fTerminating = false;
    pStrm->fNeedSeek    = false;
    pStrm->rc           = VINF_SUCCESS;
    pStrm->hIoThread    = NIL_RTTHREAD;
    pStrm->offNeedSeekTo= UINT64_MAX;

    pStrm->pHead        = NULL;
    pStrm->pFree        = NULL;
    pStrm->hEvtHead     = NIL_RTSEMEVENT;
    pStrm->hEvtFree     = NIL_RTSEMEVENT;

    pStrm->pPending     = NULL;
    pStrm->pCur         = NULL;
    pStrm->offCurStream = 0;
    pStrm->off          = 0;
    pStrm->fChecksummed = fChecksummed;
    pStrm->u32StreamCRC = fChecksummed ? RTCrc32Start() : 0;
    pStrm->offStreamCRC = 0;

    /*
     * Allocate the buffers.  Page align them in case that makes the kernel
     * and/or cpu happier in some way.
     */
    int rc = VINF_SUCCESS;
    for (uint32_t i = 0; i < cBuffers; i++)
    {
        PSSMSTRMBUF pBuf = (PSSMSTRMBUF)RTMemPageAllocZ(sizeof(*pBuf));
        if (!pBuf)
        {
            if (i > 2)
            {
                LogRel(("ssmR3StrmAllocBuffer: WARNING: Could only get %d stream buffers.\n", i));
                break;
            }
            LogRel(("ssmR3StrmAllocBuffer: Failed to allocate stream buffers. (i=%d)\n", i));
            return VERR_NO_MEMORY;
        }

        /* link it */
        pBuf->pNext = pStrm->pFree;
        pStrm->pFree = pBuf;
    }

    /*
     * Create the event semaphores.
     */
    rc = RTSemEventCreate(&pStrm->hEvtHead);
    if (RT_FAILURE(rc))
        return rc;
    rc = RTSemEventCreate(&pStrm->hEvtFree);
    if (RT_FAILURE(rc))
        return rc;

    return VINF_SUCCESS;
}


/**
 * Destroys a list of buffers.
 *
 * @param   pHead            Pointer to the head.
 */
static void ssmR3StrmDestroyBufList(PSSMSTRMBUF pHead)
{
    while (pHead)
    {
        PSSMSTRMBUF pCur = pHead;
        pHead = pCur->pNext;
        pCur->pNext = NULL;
        RTMemPageFree(pCur, sizeof(*pCur));
    }
}


/**
 * Cleans up a stream after ssmR3StrmInitInternal has been called (regardless of
 * it succeeded or not).
 *
 * @param   pStrm           The stream handle.
 */
static void ssmR3StrmDelete(PSSMSTRM pStrm)
{
    RTMemPageFree(pStrm->pCur, sizeof(*pStrm->pCur));
    pStrm->pCur = NULL;
    ssmR3StrmDestroyBufList(pStrm->pHead);
    pStrm->pHead = NULL;
    ssmR3StrmDestroyBufList(pStrm->pPending);
    pStrm->pPending = NULL;
    ssmR3StrmDestroyBufList(pStrm->pFree);
    pStrm->pFree = NULL;

    RTSemEventDestroy(pStrm->hEvtHead);
    pStrm->hEvtHead = NIL_RTSEMEVENT;

    RTSemEventDestroy(pStrm->hEvtFree);
    pStrm->hEvtFree = NIL_RTSEMEVENT;
}


/**
 * Initializes a stream that uses a method table.
 *
 * @returns VBox status code.
 * @param   pStrm           The stream manager structure.
 * @param   pStreamOps      The stream method table.
 * @param   pvUser          The user argument for the stream methods.
 * @param   fWrite          Whether to open for writing or reading.
 * @param   fChecksummed    Whether the stream is to be checksummed while
 *                          written/read.
 * @param   cBuffers        The number of buffers.
 */
static int ssmR3StrmInit(PSSMSTRM pStrm, PCSSMSTRMOPS pStreamOps, void *pvUser, bool fWrite, bool fChecksummed, uint32_t cBuffers)
{
    int rc = ssmR3StrmInitInternal(pStrm, fChecksummed, cBuffers);
    if (RT_SUCCESS(rc))
    {
        pStrm->pOps   = pStreamOps;
        pStrm->pvUser = pvUser;
        pStrm->fWrite = fWrite;
        return VINF_SUCCESS;
    }

    ssmR3StrmDelete(pStrm);
    pStrm->rc = rc;
    return rc;
}


/**
 * @copydoc SSMSTRMOPS::pfnWrite
 */
static DECLCALLBACK(int) ssmR3FileWrite(void *pvUser, uint64_t offStream, const void *pvBuf, size_t cbToWrite)
{
    NOREF(offStream);
    return RTFileWriteAt((RTFILE)(uintptr_t)pvUser, offStream, pvBuf, cbToWrite, NULL); /** @todo use RTFileWrite */
}


/**
 * @copydoc SSMSTRMOPS::pfnRead
 */
static DECLCALLBACK(int) ssmR3FileRead(void *pvUser, uint64_t offStream, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    Assert(RTFileTell((RTFILE)(uintptr_t)pvUser) == offStream); NOREF(offStream);
    return RTFileRead((RTFILE)(uintptr_t)pvUser, pvBuf, cbToRead, pcbRead);
}


/**
 * @copydoc SSMSTRMOPS::pfnSeek
 */
static DECLCALLBACK(int) ssmR3FileSeek(void *pvUser, int64_t offSeek, unsigned uMethod, uint64_t *poffActual)
{
    return RTFileSeek((RTFILE)(uintptr_t)pvUser, offSeek, uMethod, poffActual);
}


/**
 * @copydoc SSMSTRMOPS::pfnTell
 */
static DECLCALLBACK(uint64_t) ssmR3FileTell(void *pvUser)
{
    return RTFileTell((RTFILE)(uintptr_t)pvUser);
}


/**
 * @copydoc SSMSTRMOPS::pfnSize
 */
static DECLCALLBACK(int) ssmR3FileSize(void *pvUser, uint64_t *pcb)
{
    return RTFileQuerySize((RTFILE)(uintptr_t)pvUser, pcb);
}


/**
 * @copydoc SSMSTRMOPS::pfnIsOk
 */
static DECLCALLBACK(int) ssmR3FileIsOk(void *pvUser)
{
    /*
     * Check that there is still some space left on the disk.
     */
    RTFOFF cbFree;
    int rc = RTFileQueryFsSizes((RTFILE)(uintptr_t)pvUser, NULL, &cbFree, NULL, NULL);
#define SSM_MIN_DISK_FREE    ((RTFOFF)( 10 * _1M ))
    if (RT_SUCCESS(rc))
    {
        if (cbFree < SSM_MIN_DISK_FREE)
        {
            LogRel(("SSM: Giving up: Low on disk space. (cbFree=%RTfoff, SSM_MIN_DISK_FREE=%RTfoff).\n",
                    cbFree, SSM_MIN_DISK_FREE));
            rc = VERR_SSM_LOW_ON_DISK_SPACE;
        }
    }
    else if (rc == VERR_NOT_SUPPORTED)
        rc = VINF_SUCCESS;
    else
        AssertLogRelRC(rc);
    return rc;
}


/**
 * @copydoc SSMSTRMOPS::pfnClose
 */
static DECLCALLBACK(int) ssmR3FileClose(void *pvUser, bool fCancelled)
{
    NOREF(fCancelled);
    return RTFileClose((RTFILE)(uintptr_t)pvUser);
}


/**
 * Method table for a file based stream.
 */
static SSMSTRMOPS const g_ssmR3FileOps =
{
    SSMSTRMOPS_VERSION,
    ssmR3FileWrite,
    ssmR3FileRead,
    ssmR3FileSeek,
    ssmR3FileTell,
    ssmR3FileSize,
    ssmR3FileIsOk,
    ssmR3FileClose,
    SSMSTRMOPS_VERSION
};


/**
 * Opens a file stream.
 *
 * @returns VBox status code.
 * @param   pStrm           The stream manager structure.
 * @param   pszFilename     The file to open or create.
 * @param   fWrite          Whether to open for writing or reading.
 * @param   fChecksummed    Whether the stream is to be checksummed while
 *                          written/read.
 * @param   cBuffers        The number of buffers.
 */
static int ssmR3StrmOpenFile(PSSMSTRM pStrm, const char *pszFilename, bool fWrite, bool fChecksummed, uint32_t cBuffers)
{
    int rc = ssmR3StrmInitInternal(pStrm, fChecksummed, cBuffers);
    if (RT_SUCCESS(rc))
    {
        uint32_t fFlags = fWrite
                        ? RTFILE_O_READWRITE | RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_WRITE
                        : RTFILE_O_READ      | RTFILE_O_OPEN           | RTFILE_O_DENY_WRITE;
        RTFILE hFile;
        rc = RTFileOpen(&hFile, pszFilename, fFlags);
        if (RT_SUCCESS(rc))
        {
            pStrm->pOps   = &g_ssmR3FileOps;
            pStrm->pvUser = (void *)(uintptr_t)hFile;
            pStrm->fWrite = fWrite;
            return VINF_SUCCESS;
        }
    }

    ssmR3StrmDelete(pStrm);
    pStrm->rc = rc;
    return rc;
}


/**
 * Raise an error condition on the stream.
 *
 * @returns true if we raised the error condition, false if the stream already
 *          had an error condition set.
 *
 * @param   pStrm           The stream handle.
 * @param   rc              The VBox error status code.
 *
 * @thread  Any.
 */
DECLINLINE(bool) ssmR3StrmSetError(PSSMSTRM pStrm, int rc)
{
    Assert(RT_FAILURE_NP(rc));
    return ASMAtomicCmpXchgS32(&pStrm->rc, rc, VINF_SUCCESS);
}


/**
 * Puts a buffer into the free list.
 *
 * @param   pStrm           The stream handle.
 * @param   pBuf            The buffer.
 *
 * @thread  The consumer.
 */
static void ssmR3StrmPutFreeBuf(PSSMSTRM pStrm, PSSMSTRMBUF pBuf)
{
    for (;;)
    {
        PSSMSTRMBUF pCurFreeHead = ASMAtomicUoReadPtrT(&pStrm->pFree, PSSMSTRMBUF);
        ASMAtomicUoWritePtr(&pBuf->pNext, pCurFreeHead);
        if (ASMAtomicCmpXchgPtr(&pStrm->pFree, pBuf, pCurFreeHead))
        {
            int rc = RTSemEventSignal(pStrm->hEvtFree);
            AssertRC(rc);
            return;
        }
    }
}


/**
 * Gets a free buffer, waits for one if necessary.
 *
 * @returns Pointer to the buffer on success. NULL if we're terminating.
 * @param   pStrm           The stream handle.
 *
 * @thread  The producer.
 */
static PSSMSTRMBUF ssmR3StrmGetFreeBuf(PSSMSTRM pStrm)
{
    for (;;)
    {
        PSSMSTRMBUF pMine = ASMAtomicUoReadPtrT(&pStrm->pFree, PSSMSTRMBUF);
        if (!pMine)
        {
            if (pStrm->fTerminating)
                return NULL;
            if (RT_FAILURE(pStrm->rc))
                return NULL;
            if (    pStrm->fWrite
                &&  pStrm->hIoThread == NIL_RTTHREAD)
            {
                int rc = ssmR3StrmWriteBuffers(pStrm);
                if (RT_FAILURE(rc))
                    return NULL;
            }
            int rc = RTSemEventWaitNoResume(pStrm->hEvtFree, 30000);
            if (    rc == VERR_SEM_DESTROYED
                ||  pStrm->fTerminating)
                return NULL;
            continue;
        }

        if (ASMAtomicCmpXchgPtr(&pStrm->pFree, pMine->pNext, pMine))
        {
            pMine->offStream    = UINT64_MAX;
            pMine->cb           = 0;
            pMine->pNext        = NULL;
            pMine->fEndOfStream = false;
            pMine->NanoTS       = RTTimeNanoTS();
            return pMine;
        }
    }
}


/**
 * Puts a buffer onto the queue.
 *
 * @param   pStrm           The stream handle.
 * @param   pBuf            The stream buffer to put.
 *
 * @thread  The producer.
 */
static void ssmR3StrmPutBuf(PSSMSTRM pStrm, PSSMSTRMBUF pBuf)
{
    for (;;)
    {
        PSSMSTRMBUF pCurHead = ASMAtomicUoReadPtrT(&pStrm->pHead, PSSMSTRMBUF);
        ASMAtomicUoWritePtr(&pBuf->pNext, pCurHead);
        if (ASMAtomicCmpXchgPtr(&pStrm->pHead, pBuf, pCurHead))
        {
            int rc = RTSemEventSignal(pStrm->hEvtHead);
            AssertRC(rc);
            return;
        }
    }
}


/**
 * Reverses the list.
 *
 * @returns The head of the reversed list.
 * @param   pHead           The head of the list to reverse.
 */
static PSSMSTRMBUF ssmR3StrmReverseList(PSSMSTRMBUF pHead)
{
    PSSMSTRMBUF pRevHead = NULL;
    while (pHead)
    {
        PSSMSTRMBUF pCur = pHead;
        pHead = pCur->pNext;
        pCur->pNext = pRevHead;
        pRevHead = pCur;
    }
    return pRevHead;
}


/**
 * Gets one buffer from the queue, will wait for one to become ready if
 * necessary.
 *
 * @returns Pointer to the buffer on success. NULL if we're terminating.
 * @param   pStrm           The stream handle.
 *
 * @thread  The consumer.
 */
static PSSMSTRMBUF ssmR3StrmGetBuf(PSSMSTRM pStrm)
{
    for (;;)
    {
        PSSMSTRMBUF pMine = pStrm->pPending;
        if (pMine)
        {
            pStrm->pPending = pMine->pNext;
            pMine->pNext = NULL;
            return pMine;
        }

        pMine = ASMAtomicXchgPtrT(&pStrm->pHead, NULL, PSSMSTRMBUF);
        if (pMine)
            pStrm->pPending = ssmR3StrmReverseList(pMine);
        else
        {
            if (pStrm->fTerminating)
                return NULL;
            if (RT_FAILURE(pStrm->rc))
                return NULL;
            if (    !pStrm->fWrite
                &&  pStrm->hIoThread == NIL_RTTHREAD)
            {
                int rc = ssmR3StrmReadMore(pStrm);
                if (RT_FAILURE(rc))
                    return NULL;
                continue;
            }

            int rc = RTSemEventWaitNoResume(pStrm->hEvtHead, 30000);
            if (    rc == VERR_SEM_DESTROYED
                ||  pStrm->fTerminating)
                return NULL;
        }
    }
}


/**
 * Flushes the current buffer (both write and read streams).
 *
 * @param   pStrm           The stream handle.
 */
static void ssmR3StrmFlushCurBuf(PSSMSTRM pStrm)
{
    if (pStrm->pCur)
    {
        PSSMSTRMBUF pBuf = pStrm->pCur;
        pStrm->pCur = NULL;

        if (pStrm->fWrite)
        {
            uint32_t cb     = pStrm->off;
            pBuf->cb        = cb;
            pBuf->offStream = pStrm->offCurStream;
            if (    pStrm->fChecksummed
                &&  pStrm->offStreamCRC < cb)
                pStrm->u32StreamCRC = RTCrc32Process(pStrm->u32StreamCRC,
                                                     &pBuf->abData[pStrm->offStreamCRC],
                                                     cb - pStrm->offStreamCRC);
            pStrm->offCurStream += cb;
            pStrm->off           = 0;
            pStrm->offStreamCRC  = 0;

            ssmR3StrmPutBuf(pStrm, pBuf);
        }
        else
        {
            uint32_t cb = pBuf->cb;
            if (    pStrm->fChecksummed
                &&  pStrm->offStreamCRC < cb)
                pStrm->u32StreamCRC = RTCrc32Process(pStrm->u32StreamCRC,
                                                     &pBuf->abData[pStrm->offStreamCRC],
                                                     cb - pStrm->offStreamCRC);
            pStrm->offCurStream += cb;
            pStrm->off           = 0;
            pStrm->offStreamCRC  = 0;

            ssmR3StrmPutFreeBuf(pStrm, pBuf);
        }
    }
}


/**
 * Flush buffered data.
 *
 * @returns VBox status code. Returns VINF_EOF if we encounter a buffer with the
 *          fEndOfStream indicator set.
 * @param   pStrm           The stream handle.
 *
 * @thread  The producer thread.
 */
static int ssmR3StrmWriteBuffers(PSSMSTRM pStrm)
{
    Assert(pStrm->fWrite);

    /*
     * Just return if the stream has a pending error condition.
     */
    int rc = pStrm->rc;
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Grab the pending list and write it out.
     */
    PSSMSTRMBUF pHead = ASMAtomicXchgPtrT(&pStrm->pHead, NULL, PSSMSTRMBUF);
    if (!pHead)
        return VINF_SUCCESS;
    pHead = ssmR3StrmReverseList(pHead);

    while (pHead)
    {
        /* pop */
        PSSMSTRMBUF pCur = pHead;
        pHead = pCur->pNext;

        /* flush */
        rc = pStrm->pOps->pfnIsOk(pStrm->pvUser);
        if (RT_SUCCESS(rc))
            rc = pStrm->pOps->pfnWrite(pStrm->pvUser, pCur->offStream, &pCur->abData[0], pCur->cb);
        if (    RT_FAILURE(rc)
            &&  ssmR3StrmSetError(pStrm, rc))
            LogRel(("ssmR3StrmWriteBuffers: Write failed with rc=%Rrc at offStream=%#llx\n", rc, pCur->offStream));

        /* free */
        bool fEndOfStream = pCur->fEndOfStream;
        ssmR3StrmPutFreeBuf(pStrm, pCur);
        if (fEndOfStream)
        {
            Assert(!pHead);
            return VINF_EOF;
        }
    }

    return pStrm->rc;
}


/**
 * Closes the stream after first flushing any pending write.
 *
 * @returns VBox status code.
 * @param   pStrm           The stream handle.
 * @param   fCancelled      Indicates whether the operation was cancelled or
 *                          not.
 */
static int ssmR3StrmClose(PSSMSTRM pStrm, bool fCancelled)
{
    /*
     * Flush, terminate the I/O thread, and close the stream.
     */
    if (pStrm->fWrite)
    {
        ssmR3StrmFlushCurBuf(pStrm);
        if (pStrm->hIoThread == NIL_RTTHREAD)
            ssmR3StrmWriteBuffers(pStrm);
    }

    if (pStrm->hIoThread != NIL_RTTHREAD)
        ASMAtomicWriteBool(&pStrm->fTerminating, true);

    int rc;
    if (pStrm->fWrite)
    {
        if (pStrm->hIoThread != NIL_RTTHREAD)
        {
            int rc2 = RTSemEventSignal(pStrm->hEvtHead);
            AssertLogRelRC(rc2);
            int rc3 = RTThreadWait(pStrm->hIoThread, RT_INDEFINITE_WAIT, NULL);
            AssertLogRelRC(rc3);
            pStrm->hIoThread = NIL_RTTHREAD;
        }

        rc = pStrm->pOps->pfnClose(pStrm->pvUser, fCancelled);
        if (RT_FAILURE(rc))
            ssmR3StrmSetError(pStrm, rc);
    }
    else
    {
        rc = pStrm->pOps->pfnClose(pStrm->pvUser, fCancelled);
        if (RT_FAILURE(rc))
            ssmR3StrmSetError(pStrm, rc);

        if (pStrm->hIoThread != NIL_RTTHREAD)
        {
            int rc2 = RTSemEventSignal(pStrm->hEvtFree);
            AssertLogRelRC(rc2);
            int rc3 = RTThreadWait(pStrm->hIoThread, RT_INDEFINITE_WAIT, NULL);
            AssertLogRelRC(rc3);
            pStrm->hIoThread = NIL_RTTHREAD;
        }
    }

    pStrm->pOps   = NULL;
    pStrm->pvUser = NULL;

    rc = pStrm->rc;
    ssmR3StrmDelete(pStrm);

    return rc;
}

#ifndef SSM_STANDALONE

/**
 * Stream output routine.
 *
 * @returns VBox status code.
 * @param   pStrm       The stream handle.
 * @param   pvBuf       What to write.
 * @param   cbToWrite   How much to write.
 *
 * @thread  The producer in a write stream (never the I/O thread).
 */
static int ssmR3StrmWrite(PSSMSTRM pStrm, const void *pvBuf, size_t cbToWrite)
{
    AssertReturn(cbToWrite > 0, VINF_SUCCESS);
    Assert(pStrm->fWrite);

    /*
     * Squeeze as much as possible into the current buffer.
     */
    PSSMSTRMBUF pBuf = pStrm->pCur;
    if (RT_LIKELY(pBuf))
    {
        uint32_t cbLeft = RT_SIZEOFMEMB(SSMSTRMBUF, abData) - pStrm->off;
        if (RT_LIKELY(cbLeft >= cbToWrite))
        {
            memcpy(&pBuf->abData[pStrm->off], pvBuf, cbToWrite);
            pStrm->off += (uint32_t)cbToWrite;
            return VINF_SUCCESS;
        }

        if (cbLeft > 0)
        {
            memcpy(&pBuf->abData[pStrm->off], pvBuf, cbLeft);
            pStrm->off += cbLeft;
            cbToWrite  -= cbLeft;
            pvBuf       = (uint8_t const *)pvBuf + cbLeft;
        }
        Assert(pStrm->off == RT_SIZEOFMEMB(SSMSTRMBUF, abData));
    }

    /*
     * Need one or more new buffers.
     */
    do
    {
        /*
         * Flush the current buffer and replace it with a new one.
         */
        ssmR3StrmFlushCurBuf(pStrm);
        pBuf = ssmR3StrmGetFreeBuf(pStrm);
        if (!pBuf)
            break;
        pStrm->pCur = pBuf;
        Assert(pStrm->off == 0);

        /*
         * Copy data to the buffer.
         */
        uint32_t cbCopy = RT_SIZEOFMEMB(SSMSTRMBUF, abData);
        if (cbCopy > cbToWrite)
            cbCopy = (uint32_t)cbToWrite;
        memcpy(&pBuf->abData[0], pvBuf, cbCopy);
        pStrm->off  = cbCopy;
        cbToWrite  -= cbCopy;
        pvBuf       = (uint8_t const *)pvBuf + cbCopy;
    } while (cbToWrite > 0);

    return pStrm->rc;
}


/**
 * Reserves space in the current buffer so the caller can write directly to the
 * buffer instead of doing double buffering.
 *
 * @returns VBox status code
 * @param   pStrm       The stream handle.
 * @param   cb          The amount of buffer space to reserve.
 * @param   ppb         Where to return the pointer.
 */
static int ssmR3StrmReserveWriteBufferSpace(PSSMSTRM pStrm, size_t cb, uint8_t **ppb)
{
    Assert(pStrm->fWrite);
    Assert(RT_SIZEOFMEMB(SSMSTRMBUF, abData) / 4 >= cb);

    /*
     * Check if there is room in the current buffer, it not flush it.
     */
    PSSMSTRMBUF pBuf = pStrm->pCur;
    if (pBuf)
    {
        uint32_t cbLeft = RT_SIZEOFMEMB(SSMSTRMBUF, abData) - pStrm->off;
        if (cbLeft >= cb)
        {
            *ppb = &pBuf->abData[pStrm->off];
            return VINF_SUCCESS;
        }

        ssmR3StrmFlushCurBuf(pStrm);
    }

    /*
     * Get a fresh buffer and return a pointer into it.
     */
    pBuf = ssmR3StrmGetFreeBuf(pStrm);
    if (pBuf)
    {
        pStrm->pCur = pBuf;
        Assert(pStrm->off == 0);
        *ppb = &pBuf->abData[0];
    }
    else
        *ppb = NULL; /* make gcc happy. */
    return pStrm->rc;
}


/**
 * Commits buffer space reserved by ssmR3StrmReserveWriteBufferSpace.
 *
 * @returns VBox status code.
 * @param   pStrm       The stream handle.
 * @param   cb          The amount of buffer space to commit.  This can be less
 *                      that what was reserved initially.
 */
static int ssmR3StrmCommitWriteBufferSpace(PSSMSTRM pStrm, size_t cb)
{
    Assert(pStrm->pCur);
    Assert(pStrm->off + cb <= RT_SIZEOFMEMB(SSMSTRMBUF, abData));
    pStrm->off += (uint32_t)cb;
    return VINF_SUCCESS;
}


/**
 * Marks the end of the stream.
 *
 * This will cause the I/O thread to quit waiting for more buffers.
 *
 * @returns VBox status code.
 * @param   pStrm       The stream handle.
 */
static int ssmR3StrmSetEnd(PSSMSTRM pStrm)
{
    Assert(pStrm->fWrite);
    PSSMSTRMBUF pBuf = pStrm->pCur;
    if (RT_UNLIKELY(!pStrm->pCur))
    {
        pBuf = ssmR3StrmGetFreeBuf(pStrm);
        if (!pBuf)
            return pStrm->rc;
        pStrm->pCur = pBuf;
        Assert(pStrm->off == 0);
    }
    pBuf->fEndOfStream = true;
    ssmR3StrmFlushCurBuf(pStrm);
    return VINF_SUCCESS;
}

#endif /* !SSM_STANDALONE */

/**
 * Read more from the stream.
 *
 * @returns VBox status code. VERR_EOF gets translated into VINF_EOF.
 * @param   pStrm       The stream handle.
 *
 * @thread  The I/O thread when we got one, otherwise the stream user.
 */
static int ssmR3StrmReadMore(PSSMSTRM pStrm)
{
    int rc;
    Log6(("ssmR3StrmReadMore:\n"));

    /*
     * Undo seek done by ssmR3StrmPeekAt.
     */
    if (pStrm->fNeedSeek)
    {
        rc = pStrm->pOps->pfnSeek(pStrm->pvUser, pStrm->offNeedSeekTo, RTFILE_SEEK_BEGIN, NULL);
        if (RT_FAILURE(rc))
        {
            if (ssmR3StrmSetError(pStrm, rc))
                LogRel(("ssmR3StrmReadMore: RTFileSeek(,%#llx,) failed with rc=%Rrc\n", pStrm->offNeedSeekTo, rc));
            return rc;
        }
        pStrm->fNeedSeek     = false;
        pStrm->offNeedSeekTo = UINT64_MAX;
    }

    /*
     * Get a free buffer and try fill it up.
     */
    PSSMSTRMBUF pBuf = ssmR3StrmGetFreeBuf(pStrm);
    if (!pBuf)
        return pStrm->rc;

    pBuf->offStream = pStrm->pOps->pfnTell(pStrm->pvUser);
    size_t cbRead   = sizeof(pBuf->abData);
    rc = pStrm->pOps->pfnRead(pStrm->pvUser, pBuf->offStream, &pBuf->abData[0], cbRead, &cbRead);
    if (    RT_SUCCESS(rc)
        &&  cbRead > 0)
    {
        pBuf->cb           = (uint32_t)cbRead;
        pBuf->fEndOfStream = false;
        Log6(("ssmR3StrmReadMore: %#010llx %#x\n", pBuf->offStream, pBuf->cb));
        ssmR3StrmPutBuf(pStrm, pBuf);
    }
    else if (   (   RT_SUCCESS_NP(rc)
                 && cbRead == 0)
             || rc == VERR_EOF)
    {
        pBuf->cb           = 0;
        pBuf->fEndOfStream = true;
        Log6(("ssmR3StrmReadMore: %#010llx 0 EOF!\n", pBuf->offStream));
        ssmR3StrmPutBuf(pStrm, pBuf);
        rc = VINF_EOF;
    }
    else
    {
        Log6(("ssmR3StrmReadMore: %#010llx rc=%Rrc!\n", pBuf->offStream, rc));
        if (ssmR3StrmSetError(pStrm, rc))
            LogRel(("ssmR3StrmReadMore: RTFileRead(,,%#x,) -> %Rrc at offset %#llx\n",
                    sizeof(pBuf->abData), rc, pBuf->offStream));
        ssmR3StrmPutFreeBuf(pStrm, pBuf);
    }
    return rc;
}


/**
 * Stream input routine.
 *
 * @returns VBox status code.
 * @param   pStrm       The stream handle.
 * @param   pvBuf       Where to put what we read.
 * @param   cbToRead    How much to read.
 */
static int ssmR3StrmRead(PSSMSTRM pStrm, void *pvBuf, size_t cbToRead)
{
    AssertReturn(cbToRead > 0, VINF_SUCCESS);
    Assert(!pStrm->fWrite);

    /*
     * Read from the current buffer if we got one.
     */
    PSSMSTRMBUF pBuf = pStrm->pCur;
    if (RT_LIKELY(pBuf))
    {
        Assert(pStrm->off <= pBuf->cb);
        uint32_t cbLeft = pBuf->cb - pStrm->off;
        if (cbLeft >= cbToRead)
        {
            memcpy(pvBuf, &pBuf->abData[pStrm->off], cbToRead);
            pStrm->off += (uint32_t)cbToRead;
            Assert(pStrm->off <= pBuf->cb);
            return VINF_SUCCESS;
        }
        if (cbLeft)
        {
            memcpy(pvBuf, &pBuf->abData[pStrm->off], cbLeft);
            pStrm->off += cbLeft;
            cbToRead   -= cbLeft;
            pvBuf       = (uint8_t *)pvBuf + cbLeft;
        }
        else if (pBuf->fEndOfStream)
            return VERR_EOF;
        Assert(pStrm->off == pBuf->cb);
    }

    /*
     * Get more buffers from the stream.
     */
    int rc = VINF_SUCCESS;
    do
    {
        /*
         * Check for EOF first - never flush the EOF buffer.
         */
        if (    pBuf
            &&  pBuf->fEndOfStream)
            return VERR_EOF;

        /*
         * Flush the current buffer and get the next one.
         */
        ssmR3StrmFlushCurBuf(pStrm);
        pBuf = ssmR3StrmGetBuf(pStrm);
        if (!pBuf)
        {
            rc = pStrm->rc;
            break;
        }
        pStrm->pCur = pBuf;
        Assert(pStrm->off == 0);
        Assert(pStrm->offCurStream == pBuf->offStream);
        if (!pBuf->cb)
        {
            Assert(pBuf->fEndOfStream);
            return VERR_EOF;
        }

        /*
         * Read data from the buffer.
         */
        uint32_t cbCopy = pBuf->cb;
        if (cbCopy > cbToRead)
            cbCopy = (uint32_t)cbToRead;
        memcpy(pvBuf, &pBuf->abData[0], cbCopy);
        pStrm->off  = cbCopy;
        cbToRead   -= cbCopy;
        pvBuf       = (uint8_t *)pvBuf + cbCopy;
        Assert(!pStrm->pCur || pStrm->off <= pStrm->pCur->cb);
    } while (cbToRead > 0);

    return rc;
}


/**
 * Reads data from the stream but instead of copying it to some output buffer
 * the caller gets a pointer to into the current stream buffer.
 *
 * The returned pointer becomes invalid after the next stream operation!
 *
 * @returns Pointer to the read data residing in the stream buffer.  NULL is
 *          returned if the request amount of data isn't available in the
 *          buffer.  The caller must fall back on ssmR3StrmRead when this
 *          happens.
 *
 * @param   pStrm       The stream handle.
 * @param   cbToRead    The number of bytes to tread.
 */
static uint8_t const *ssmR3StrmReadDirect(PSSMSTRM pStrm, size_t cbToRead)
{
    AssertReturn(cbToRead > 0, VINF_SUCCESS);
    Assert(!pStrm->fWrite);

    /*
     * Too lazy to fetch more data for the odd case that we're
     * exactly at the boundary between two buffers.
     */
    PSSMSTRMBUF pBuf = pStrm->pCur;
    if (RT_LIKELY(pBuf))
    {
        Assert(pStrm->off <= pBuf->cb);
        uint32_t cbLeft = pBuf->cb - pStrm->off;
        if (cbLeft >= cbToRead)
        {
            uint8_t const *pb = &pBuf->abData[pStrm->off];
            pStrm->off += (uint32_t)cbToRead;
            Assert(pStrm->off <= pBuf->cb);
            return pb;
        }
    }
    return NULL;
}


#ifndef SSM_STANDALONE
/**
 * Check that the stream is OK and flush data that is getting old
 *
 * The checking is mainly for testing for cancellation and out of space
 * conditions.
 *
 * @returns VBox status code.
 * @param   pStrm       The stream handle.
 */
static int ssmR3StrmCheckAndFlush(PSSMSTRM pStrm)
{
    int rc = pStrm->pOps->pfnIsOk(pStrm->pvUser);
    if (RT_FAILURE(rc))
        return rc;

    if (    pStrm->fWrite
        &&  pStrm->hIoThread != NIL_RTTHREAD
        &&  !pStrm->pHead /* the worker is probably idle */
        &&  pStrm->pCur
        &&  RTTimeNanoTS() - pStrm->pCur->NanoTS > 500*1000*1000 /* 0.5s */
       )
        ssmR3StrmFlushCurBuf(pStrm);
    return VINF_SUCCESS;
}
#endif /* !SSM_STANDALONE */


#if !defined(SSM_STANDALONE) || defined(LOG_ENABLED)
/**
 * Tell current stream position.
 *
 * @returns stream position.
 * @param   pStrm       The stream handle.
 */
static uint64_t ssmR3StrmTell(PSSMSTRM pStrm)
{
    return pStrm->offCurStream + pStrm->off;
}
#endif


/**
 * Gets the intermediate stream CRC up to the current position.
 *
 * @returns CRC.
 * @param   pStrm       The stream handle.
 */
static uint32_t ssmR3StrmCurCRC(PSSMSTRM pStrm)
{
    if (!pStrm->fChecksummed)
        return 0;
    if (pStrm->offStreamCRC < pStrm->off)
    {
        PSSMSTRMBUF pBuf = pStrm->pCur; Assert(pBuf);
        pStrm->u32StreamCRC = RTCrc32Process(pStrm->u32StreamCRC, &pBuf->abData[pStrm->offStreamCRC], pStrm->off - pStrm->offStreamCRC);
        pStrm->offStreamCRC = pStrm->off;
    }
    else
        Assert(pStrm->offStreamCRC == pStrm->off);
    return pStrm->u32StreamCRC;
}


/**
 * Gets the final stream CRC up to the current position.
 *
 * @returns CRC.
 * @param   pStrm       The stream handle.
 */
static uint32_t ssmR3StrmFinalCRC(PSSMSTRM pStrm)
{
    if (!pStrm->fChecksummed)
        return 0;
    return RTCrc32Finish(ssmR3StrmCurCRC(pStrm));
}


/**
 * Disables checksumming of the stream.
 *
 * @param   pStrm       The stream handle.
 */
static void ssmR3StrmDisableChecksumming(PSSMSTRM pStrm)
{
    pStrm->fChecksummed = false;
}


/**
 * Used by SSMR3Seek to position the stream at the new unit.
 *
 * @returns VBox status code.
 * @param   pStrm       The strem handle.
 * @param   off         The seek offset.
 * @param   uMethod     The seek method.
 * @param   u32CurCRC   The current CRC at the seek position.
 */
static int ssmR3StrmSeek(PSSMSTRM pStrm, int64_t off, uint32_t uMethod, uint32_t u32CurCRC)
{
    AssertReturn(!pStrm->fWrite, VERR_NOT_SUPPORTED);
    AssertReturn(pStrm->hIoThread == NIL_RTTHREAD, VERR_WRONG_ORDER);

    uint64_t offStream;
    int rc = pStrm->pOps->pfnSeek(pStrm->pvUser, off, uMethod, &offStream);
    if (RT_SUCCESS(rc))
    {
        pStrm->fNeedSeek    = false;
        pStrm->offNeedSeekTo= UINT64_MAX;
        pStrm->offCurStream = offStream;
        pStrm->off          = 0;
        pStrm->offStreamCRC = 0;
        if (pStrm->fChecksummed)
            pStrm->u32StreamCRC = u32CurCRC;
        if (pStrm->pCur)
        {
            ssmR3StrmPutFreeBuf(pStrm, pStrm->pCur);
            pStrm->pCur = NULL;
        }
        if (pStrm->pPending)
        {
            ssmR3StrmDestroyBufList(pStrm->pPending);
            pStrm->pPending = NULL;
        }
        if (pStrm->pHead)
        {
            ssmR3StrmDestroyBufList(pStrm->pHead);
            pStrm->pHead = NULL;
        }
    }
    return rc;
}


#ifndef SSM_STANDALONE
/**
 * Skip some bytes in the stream.
 *
 * This is only used if someone didn't read all of their data in the V1 format,
 * so don't bother making this very efficient yet.
 *
 * @returns VBox status code.
 * @param   pStrm       The stream handle.
 * @param   offDst      The destination offset.
 */
static int ssmR3StrmSkipTo(PSSMSTRM pStrm, uint64_t offDst)
{
    /* dead simple - lazy bird! */
    for (;;)
    {
        uint64_t offCur = ssmR3StrmTell(pStrm);
        AssertReturn(offCur <= offDst, VERR_SSM_SKIP_BACKWARDS);
        if (offCur == offDst)
            return VINF_SUCCESS;

        uint8_t abBuf[4096];
        size_t cbToRead = RT_MIN(sizeof(abBuf), offDst - offCur);
        int rc = ssmR3StrmRead(pStrm, abBuf, cbToRead);
        if (RT_FAILURE(rc))
            return rc;
    }
}
#endif /* !SSM_STANDALONE */


/**
 * Get the size of the file.
 *
 * This does not work for non-file streams!
 *
 * @returns The file size, or UINT64_MAX if not a file stream.
 * @param   pStrm       The stream handle.
 */
static uint64_t ssmR3StrmGetSize(PSSMSTRM pStrm)
{
    uint64_t cbFile;
    int rc = pStrm->pOps->pfnSize(pStrm->pvUser, &cbFile);
    AssertLogRelRCReturn(rc, UINT64_MAX);
    return cbFile;
}


/***
 * Tests if the stream is a file stream or not.
 *
 * @returns true / false.
 * @param   pStrm       The stream handle.
 */
static bool ssmR3StrmIsFile(PSSMSTRM pStrm)
{
    return pStrm->pOps == &g_ssmR3FileOps;
}


/**
 * Peeks at data in a file stream without buffering anything (or upsetting
 * the buffering for that matter).
 *
 * @returns VBox status code.
 * @param   pStrm       The stream handle
 * @param   off         The offset to start peeking at. Use a negative offset to
 *                      peek at something relative to the end of the file.
 * @param   pvBuf       Output buffer.
 * @param   cbToRead    How much to read.
 * @param   poff        Where to optionally store the position.  Useful when
 *                      using a negative off.
 *
 * @remarks Failures occurring while peeking will not be raised on the stream.
 */
static int ssmR3StrmPeekAt(PSSMSTRM pStrm, RTFOFF off, void *pvBuf, size_t cbToRead, uint64_t *poff)
{
    AssertReturn(!pStrm->fWrite, VERR_NOT_SUPPORTED);
    AssertReturn(pStrm->hIoThread == NIL_RTTHREAD, VERR_WRONG_ORDER);

    if (!pStrm->fNeedSeek)
    {
        pStrm->fNeedSeek     = true;
        pStrm->offNeedSeekTo = pStrm->offCurStream + (pStrm->pCur ? pStrm->pCur->cb : 0);
    }
    uint64_t offActual;
    int rc = pStrm->pOps->pfnSeek(pStrm->pvUser, off, off >= 0 ? RTFILE_SEEK_BEGIN : RTFILE_SEEK_END, &offActual);
    if (RT_SUCCESS(rc))
    {
        if (poff)
            *poff = offActual;
        rc = pStrm->pOps->pfnRead(pStrm->pvUser, offActual, pvBuf, cbToRead, NULL);
    }

    return rc;
}

#ifndef SSM_STANDALONE

/**
 * The I/O thread.
 *
 * @returns VINF_SUCCESS (ignored).
 * @param   hSelf       The thread handle.
 * @param   pvStrm      The stream handle.
 */
static DECLCALLBACK(int) ssmR3StrmIoThread(RTTHREAD hSelf, void *pvStrm)
{
    PSSMSTRM pStrm = (PSSMSTRM)pvStrm;
    ASMAtomicWriteHandle(&pStrm->hIoThread, hSelf); /* paranoia */

    Log(("ssmR3StrmIoThread: starts working\n"));
    if (pStrm->fWrite)
    {
        /*
         * Write until error or terminated.
         */
        for (;;)
        {
            int rc = ssmR3StrmWriteBuffers(pStrm);
            if (    RT_FAILURE(rc)
                ||  rc == VINF_EOF)
            {
                Log(("ssmR3StrmIoThread: quitting writing with rc=%Rrc.\n", rc));
                break;
            }
            if (RT_FAILURE(pStrm->rc))
            {
                Log(("ssmR3StrmIoThread: quitting writing with stream rc=%Rrc\n", pStrm->rc));
                break;
            }

            if (ASMAtomicReadBool(&pStrm->fTerminating))
            {
                if (!ASMAtomicReadPtrT(&pStrm->pHead, PSSMSTRMBUF))
                {
                    Log(("ssmR3StrmIoThread: quitting writing because of pending termination.\n"));
                    break;
                }
                Log(("ssmR3StrmIoThread: postponing termination because of pending buffers.\n"));
            }
            else if (!ASMAtomicReadPtrT(&pStrm->pHead, PSSMSTRMBUF))
            {
                rc = RTSemEventWait(pStrm->hEvtHead, RT_INDEFINITE_WAIT);
                AssertLogRelRC(rc);
            }
        }

        if (!ASMAtomicReadBool(&pStrm->fTerminating))
            RTSemEventSignal(pStrm->hEvtFree);
    }
    else
    {
        /*
         * Read until end of file, error or termination.
         */
        for (;;)
        {
            if (ASMAtomicReadBool(&pStrm->fTerminating))
            {
                Log(("ssmR3StrmIoThread: quitting reading because of pending termination.\n"));
                break;
            }

            int rc = ssmR3StrmReadMore(pStrm);
            if (    RT_FAILURE(rc)
                ||  rc == VINF_EOF)
            {
                Log(("ssmR3StrmIoThread: quitting reading with rc=%Rrc\n", rc));
                break;
            }
            if (RT_FAILURE(pStrm->rc))
            {
                Log(("ssmR3StrmIoThread: quitting reading with stream rc=%Rrc\n", pStrm->rc));
                break;
            }
        }

        if (!ASMAtomicReadBool(&pStrm->fTerminating))
            RTSemEventSignal(pStrm->hEvtHead);
    }

    return VINF_SUCCESS;
}


/**
 * Starts the I/O thread for the specified stream.
 *
 * @param   pStrm       The stream handle.
 */
static void ssmR3StrmStartIoThread(PSSMSTRM pStrm)
{
    Assert(pStrm->hIoThread == NIL_RTTHREAD);

    RTTHREAD hThread;
    int rc = RTThreadCreate(&hThread, ssmR3StrmIoThread, pStrm, 0, RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "SSM-IO");
    AssertRCReturnVoid(rc);
    ASMAtomicWriteHandle(&pStrm->hIoThread, hThread); /* paranoia */
}


/**
 * Stops the I/O thread.
 *
 * @param   pStrm       The stream handle.
 */
static void ssmR3StrmStopIoThread(PSSMSTRM pStrm)
{
    LogFlow(("ssmR3StrmStopIoThread: %p\n", pStrm->hIoThread));
    if (pStrm->hIoThread != NIL_RTTHREAD)
    {
        /*
         * Signal the I/O thread and wait for it to complete.
         */
        ASMAtomicWriteBool(&pStrm->fTerminating, true);
        if (pStrm->fWrite)
        {
            int rc1 = RTSemEventSignal(pStrm->hEvtHead);
            AssertLogRelRC(rc1);
        }
        else
        {
            int rc2 = RTSemEventSignal(pStrm->hEvtFree);
            AssertLogRelRC(rc2);
        }
        int rc3 = RTThreadWait(pStrm->hIoThread, RT_INDEFINITE_WAIT, NULL);
        AssertLogRelRC(rc3);
        pStrm->hIoThread = NIL_RTTHREAD;
        pStrm->fTerminating = false; /* Can't read stuff otherwise. */
    }
}

#endif /* !SSM_STANDALONE */

/**
 * Works the progress calculation for non-live saves and restores.
 *
 * @param   pSSM        The SSM handle.
 * @param   cbAdvance   Number of bytes to advance (with in the current unit).
 */
static void ssmR3ProgressByByte(PSSMHANDLE pSSM, uint64_t cbAdvance)
{
    if (!pSSM->fLiveSave)
    {
        /* Can't advance it beyond the estimated end of the unit. */
        uint64_t cbLeft = pSSM->offEstUnitEnd - pSSM->offEst;
        if (cbAdvance > cbLeft)
            cbAdvance = cbLeft;
        pSSM->offEst += cbAdvance;

        /* uPercentPrepare% prepare, xx% exec, uPercentDone% done+crc. This is not
           quite right for live save, but the non-live stage there is very short. */
        while (   pSSM->offEst >= pSSM->offEstProgress
               && pSSM->uPercent <= 100 - pSSM->uPercentDone)
        {
            if (pSSM->pfnProgress)
                pSSM->pfnProgress(pSSM->pVM->pUVM, pSSM->uPercent, pSSM->pvUser);
            pSSM->uPercent++;
            pSSM->offEstProgress = (pSSM->uPercent - pSSM->uPercentPrepare - pSSM->uPercentLive) * pSSM->cbEstTotal
                                 / (100 - pSSM->uPercentDone - pSSM->uPercentPrepare - pSSM->uPercentLive);
        }
    }
}


#ifndef SSM_STANDALONE
/**
 * Makes the SSM operation cancellable or not (via SSMR3Cancel).
 *
 * @param   pVM             The cross context VM structure.
 * @param   pSSM            The saved state handle. (SSMHANDLE::rc may be set.)
 * @param   fCancellable    The new state.
 */
static void ssmR3SetCancellable(PVM pVM, PSSMHANDLE pSSM, bool fCancellable)
{
    RTCritSectEnter(&pVM->ssm.s.CancelCritSect);
    if (fCancellable)
    {
        Assert(!pVM->ssm.s.pSSM);
        pVM->ssm.s.pSSM = pSSM;
    }
    else
    {
        if (pVM->ssm.s.pSSM == pSSM)
            pVM->ssm.s.pSSM = NULL;

        uint32_t fCancelled = ASMAtomicUoReadU32(&pSSM->fCancelled);
        if (    fCancelled == SSMHANDLE_CANCELLED
            &&  RT_SUCCESS(pSSM->rc))
            pSSM->rc = VERR_SSM_CANCELLED;
    }

    RTCritSectLeave(&pVM->ssm.s.CancelCritSect);
}
#endif /* !SSM_STANDALONE */


/**
 * Gets the host bit count of the saved state.
 *
 * Works for on both save and load handles.
 *
 * @returns 32 or 64.
 * @param   pSSM                The saved state handle.
 */
DECLINLINE(uint32_t) ssmR3GetHostBits(PSSMHANDLE pSSM)
{
    if (pSSM->enmOp >= SSMSTATE_LOAD_PREP)
    {
        uint32_t cBits = pSSM->u.Read.cHostBits;
        if (cBits)
            return cBits;
    }
    return HC_ARCH_BITS;
}


/**
 * Saved state origins on a host using 32-bit MSC?
 *
 * Works for on both save and load handles.
 *
 * @returns true/false.
 * @param   pSSM                The saved state handle.
 */
DECLINLINE(bool) ssmR3IsHostMsc32(PSSMHANDLE pSSM)
{
    if (pSSM->enmOp >= SSMSTATE_LOAD_PREP)
        return pSSM->u.Read.fIsHostMsc32;
    return SSM_HOST_IS_MSC_32;
}

#ifndef SSM_STANDALONE

/**
 * Finishes a data unit.
 * All buffers and compressor instances are flushed and destroyed.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 */
static int ssmR3DataWriteFinish(PSSMHANDLE pSSM)
{
    //Log2(("ssmR3DataWriteFinish: %#010llx start\n", ssmR3StrmTell(&pSSM->Strm)));
    int rc = ssmR3DataFlushBuffer(pSSM);
    if (RT_SUCCESS(rc))
    {
        pSSM->offUnit     = UINT64_MAX;
        pSSM->offUnitUser = UINT64_MAX;
        return VINF_SUCCESS;
    }

    if (RT_SUCCESS(pSSM->rc))
        pSSM->rc = rc;
    Log2(("ssmR3DataWriteFinish: failure rc=%Rrc\n", rc));
    return rc;
}


/**
 * Begins writing the data of a data unit.
 *
 * Errors are signalled via pSSM->rc.
 *
 * @param   pSSM            The saved state handle.
 */
static void ssmR3DataWriteBegin(PSSMHANDLE pSSM)
{
    pSSM->offUnit     = 0;
    pSSM->offUnitUser = 0;
}


/**
 * Writes a record to the current data item in the saved state file.
 *
 * @returns VBox status code. Sets pSSM->rc on failure.
 * @param   pSSM            The saved state handle.
 * @param   pvBuf           The bits to write.
 * @param   cbBuf           The number of bytes to write.
 */
static int ssmR3DataWriteRaw(PSSMHANDLE pSSM, const void *pvBuf, size_t cbBuf)
{
    Log2(("ssmR3DataWriteRaw: %08llx|%08llx: pvBuf=%p cbBuf=%#x %.*Rhxs%s\n",
          ssmR3StrmTell(&pSSM->Strm), pSSM->offUnit, pvBuf, cbBuf, RT_MIN(cbBuf, SSM_LOG_BYTES), pvBuf, cbBuf > SSM_LOG_BYTES ? "..." : ""));

    /*
     * Check that everything is fine.
     */
    if (RT_FAILURE(pSSM->rc))
        return pSSM->rc;

    /*
     * Write the data item in 1MB chunks for progress indicator reasons.
     */
    while (cbBuf > 0)
    {
        size_t cbChunk = RT_MIN(cbBuf, _1M);
        int rc = ssmR3StrmWrite(&pSSM->Strm, pvBuf, cbChunk);
        if (RT_FAILURE(rc))
            return rc;
        pSSM->offUnit += cbChunk;
        cbBuf -= cbChunk;
        pvBuf = (char *)pvBuf + cbChunk;
    }

    return VINF_SUCCESS;
}


/**
 * Writes a record header for the specified amount of data.
 *
 * @returns VBox status code. Sets pSSM->rc on failure.
 * @param   pSSM            The saved state handle
 * @param   cb              The amount of data.
 * @param   u8TypeAndFlags  The record type and flags.
 */
static int ssmR3DataWriteRecHdr(PSSMHANDLE pSSM, size_t cb, uint8_t u8TypeAndFlags)
{
    size_t  cbHdr;
    uint8_t abHdr[8];
    abHdr[0] = u8TypeAndFlags;
    if (cb < 0x80)
    {
        cbHdr = 2;
        abHdr[1] = (uint8_t)cb;
    }
    else if (cb < 0x00000800)
    {
        cbHdr = 3;
        abHdr[1] = (uint8_t)(0xc0 | (cb >> 6));
        abHdr[2] = (uint8_t)(0x80 | (cb & 0x3f));
    }
    else if (cb < 0x00010000)
    {
        cbHdr = 4;
        abHdr[1] = (uint8_t)(0xe0 | (cb >> 12));
        abHdr[2] = (uint8_t)(0x80 | ((cb >> 6) & 0x3f));
        abHdr[3] = (uint8_t)(0x80 | (cb & 0x3f));
    }
    else if (cb < 0x00200000)
    {
        cbHdr = 5;
        abHdr[1] = (uint8_t)(0xf0 |  (cb >> 18));
        abHdr[2] = (uint8_t)(0x80 | ((cb >> 12) & 0x3f));
        abHdr[3] = (uint8_t)(0x80 | ((cb >>  6) & 0x3f));
        abHdr[4] = (uint8_t)(0x80 |  (cb        & 0x3f));
    }
    else if (cb < 0x04000000)
    {
        cbHdr = 6;
        abHdr[1] = (uint8_t)(0xf8 |  (cb >> 24));
        abHdr[2] = (uint8_t)(0x80 | ((cb >> 18) & 0x3f));
        abHdr[3] = (uint8_t)(0x80 | ((cb >> 12) & 0x3f));
        abHdr[4] = (uint8_t)(0x80 | ((cb >>  6) & 0x3f));
        abHdr[5] = (uint8_t)(0x80 |  (cb        & 0x3f));
    }
    else if (cb <= 0x7fffffff)
    {
        cbHdr = 7;
        abHdr[1] = (uint8_t)(0xfc |  (cb >> 30));
        abHdr[2] = (uint8_t)(0x80 | ((cb >> 24) & 0x3f));
        abHdr[3] = (uint8_t)(0x80 | ((cb >> 18) & 0x3f));
        abHdr[4] = (uint8_t)(0x80 | ((cb >> 12) & 0x3f));
        abHdr[5] = (uint8_t)(0x80 | ((cb >>  6) & 0x3f));
        abHdr[6] = (uint8_t)(0x80 | (cb & 0x3f));
    }
    else
        AssertLogRelMsgFailedReturn(("cb=%#x\n", cb), pSSM->rc = VERR_SSM_MEM_TOO_BIG);

    Log3(("ssmR3DataWriteRecHdr: %08llx|%08llx/%08x: Type=%02x fImportant=%RTbool cbHdr=%u\n",
          ssmR3StrmTell(&pSSM->Strm) + cbHdr, pSSM->offUnit + cbHdr, cb, u8TypeAndFlags & SSM_REC_TYPE_MASK, !!(u8TypeAndFlags & SSM_REC_FLAGS_IMPORTANT), cbHdr));

    return ssmR3DataWriteRaw(pSSM, &abHdr[0], cbHdr);
}


/**
 * Worker that flushes the buffered data.
 *
 * @returns VBox status code. Will set pSSM->rc on error.
 * @param   pSSM            The saved state handle.
 */
static int ssmR3DataFlushBuffer(PSSMHANDLE pSSM)
{
    /*
     * Check how much there current is in the buffer.
     */
    uint32_t cb = pSSM->u.Write.offDataBuffer;
    if (!cb)
        return pSSM->rc;
    pSSM->u.Write.offDataBuffer = 0;

    /*
     * Write a record header and then the data.
     * (No need for fancy optimizations here any longer since the stream is
     * fully buffered.)
     */
    int rc = ssmR3DataWriteRecHdr(pSSM, cb, SSM_REC_FLAGS_FIXED | SSM_REC_FLAGS_IMPORTANT | SSM_REC_TYPE_RAW);
    if (RT_SUCCESS(rc))
        rc = ssmR3DataWriteRaw(pSSM, pSSM->u.Write.abDataBuffer, cb);
    ssmR3ProgressByByte(pSSM, cb);
    return rc;
}


/**
 * ssmR3DataWrite worker that writes big stuff.
 *
 * @returns VBox status code
 * @param   pSSM            The saved state handle.
 * @param   pvBuf           The bits to write.
 * @param   cbBuf           The number of bytes to write.
 */
static int ssmR3DataWriteBig(PSSMHANDLE pSSM, const void *pvBuf, size_t cbBuf)
{
    int rc = ssmR3DataFlushBuffer(pSSM);
    if (RT_SUCCESS(rc))
    {
        pSSM->offUnitUser += cbBuf;

        /*
         * Split it up into compression blocks.
         */
        for (;;)
        {
            if (    cbBuf >= SSM_ZIP_BLOCK_SIZE
                && (    ((uintptr_t)pvBuf & 0xf)
                    ||  !ASMMemIsZero(pvBuf, SSM_ZIP_BLOCK_SIZE))
               )
            {
                /*
                 * Compress it.
                 */
                AssertCompile(1 + 3 + 1 + SSM_ZIP_BLOCK_SIZE < 0x00010000);
                uint8_t *pb;
                rc = ssmR3StrmReserveWriteBufferSpace(&pSSM->Strm, 1 + 3 + 1 + SSM_ZIP_BLOCK_SIZE, &pb);
                if (RT_FAILURE(rc))
                    break;
                size_t cbRec = SSM_ZIP_BLOCK_SIZE - (SSM_ZIP_BLOCK_SIZE / 16);
                rc = RTZipBlockCompress(RTZIPTYPE_LZF, RTZIPLEVEL_FAST, 0 /*fFlags*/,
                                        pvBuf, SSM_ZIP_BLOCK_SIZE,
                                        pb + 1 + 3 + 1, cbRec, &cbRec);
                if (RT_SUCCESS(rc))
                {
                    pb[0] = SSM_REC_FLAGS_FIXED | SSM_REC_FLAGS_IMPORTANT | SSM_REC_TYPE_RAW_LZF;
                    pb[4] = SSM_ZIP_BLOCK_SIZE / _1K;
                    cbRec += 1;
                }
                else
                {
                    pb[0] = SSM_REC_FLAGS_FIXED | SSM_REC_FLAGS_IMPORTANT | SSM_REC_TYPE_RAW;
                    memcpy(&pb[4], pvBuf, SSM_ZIP_BLOCK_SIZE);
                    cbRec = SSM_ZIP_BLOCK_SIZE;
                }
                pb[1] = (uint8_t)(0xe0 | ( cbRec >> 12));
                pb[2] = (uint8_t)(0x80 | ((cbRec >>  6) & 0x3f));
                pb[3] = (uint8_t)(0x80 | ( cbRec        & 0x3f));
                cbRec += 1 + 3;
                rc = ssmR3StrmCommitWriteBufferSpace(&pSSM->Strm, cbRec);
                if (RT_FAILURE(rc))
                    break;

                pSSM->offUnit += cbRec;
                ssmR3ProgressByByte(pSSM, SSM_ZIP_BLOCK_SIZE);

                /* advance */
                if (cbBuf == SSM_ZIP_BLOCK_SIZE)
                    return VINF_SUCCESS;
                cbBuf -= SSM_ZIP_BLOCK_SIZE;
                pvBuf = (uint8_t const*)pvBuf + SSM_ZIP_BLOCK_SIZE;
            }
            else if (cbBuf >= SSM_ZIP_BLOCK_SIZE)
            {
                /*
                 * Zero block.
                 */
                uint8_t abRec[3];
                abRec[0] = SSM_REC_FLAGS_FIXED | SSM_REC_FLAGS_IMPORTANT | SSM_REC_TYPE_RAW_ZERO;
                abRec[1] = 1;
                abRec[2] = SSM_ZIP_BLOCK_SIZE / _1K;
                Log3(("ssmR3DataWriteBig: %08llx|%08llx/%08x: ZERO\n", ssmR3StrmTell(&pSSM->Strm) + 2, pSSM->offUnit + 2, 1));
                rc = ssmR3DataWriteRaw(pSSM, &abRec[0], sizeof(abRec));
                if (RT_FAILURE(rc))
                    break;

                /* advance */
                ssmR3ProgressByByte(pSSM, SSM_ZIP_BLOCK_SIZE);
                if (cbBuf == SSM_ZIP_BLOCK_SIZE)
                    return VINF_SUCCESS;
                cbBuf -= SSM_ZIP_BLOCK_SIZE;
                pvBuf = (uint8_t const*)pvBuf + SSM_ZIP_BLOCK_SIZE;
            }
            else
            {
                /*
                 * Less than one block left, store it the simple way.
                 */
                rc = ssmR3DataWriteRecHdr(pSSM, cbBuf, SSM_REC_FLAGS_FIXED | SSM_REC_FLAGS_IMPORTANT | SSM_REC_TYPE_RAW);
                if (RT_SUCCESS(rc))
                    rc = ssmR3DataWriteRaw(pSSM, pvBuf, cbBuf);
                ssmR3ProgressByByte(pSSM, cbBuf);
                break;
            }
        }
    }
    return rc;
}


/**
 * ssmR3DataWrite worker that is called when there isn't enough room in the
 * buffer for the current chunk of data.
 *
 * This will first flush the buffer and then add the new bits to it.
 *
 * @returns VBox status code
 * @param   pSSM            The saved state handle.
 * @param   pvBuf           The bits to write.
 * @param   cbBuf           The number of bytes to write.
 */
static int ssmR3DataWriteFlushAndBuffer(PSSMHANDLE pSSM, const void *pvBuf, size_t cbBuf)
{
    int rc = ssmR3DataFlushBuffer(pSSM);
    if (RT_SUCCESS(rc))
    {
        memcpy(&pSSM->u.Write.abDataBuffer[0], pvBuf, cbBuf);
        pSSM->u.Write.offDataBuffer = (uint32_t)cbBuf;
        pSSM->offUnitUser += cbBuf;
    }
    return rc;
}


/**
 * Writes data to the current data unit.
 *
 * This is an inlined wrapper that optimizes the small writes that so many of
 * the APIs make.
 *
 * @returns VBox status code
 * @param   pSSM            The saved state handle.
 * @param   pvBuf           The bits to write.
 * @param   cbBuf           The number of bytes to write.
 */
DECLINLINE(int) ssmR3DataWrite(PSSMHANDLE pSSM, const void *pvBuf, size_t cbBuf)
{
    if (cbBuf > sizeof(pSSM->u.Write.abDataBuffer) / 8)
        return ssmR3DataWriteBig(pSSM, pvBuf, cbBuf);
    if (!cbBuf)
        return VINF_SUCCESS;

    uint32_t off = pSSM->u.Write.offDataBuffer;
    if (RT_UNLIKELY(cbBuf + off > sizeof(pSSM->u.Write.abDataBuffer)))
        return ssmR3DataWriteFlushAndBuffer(pSSM, pvBuf, cbBuf);

    memcpy(&pSSM->u.Write.abDataBuffer[off], pvBuf, cbBuf);
    pSSM->u.Write.offDataBuffer = off + (uint32_t)cbBuf;
    pSSM->offUnitUser += cbBuf;
    return VINF_SUCCESS;
}


/**
 * Puts a structure.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pvStruct        The structure address.
 * @param   paFields        The array of structure fields descriptions.
 *                          The array must be terminated by a SSMFIELD_ENTRY_TERM().
 */
VMMR3DECL(int) SSMR3PutStruct(PSSMHANDLE pSSM, const void *pvStruct, PCSSMFIELD paFields)
{
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    AssertPtr(pvStruct);
    AssertPtr(paFields);

    /* begin marker. */
    int rc = SSMR3PutU32(pSSM, SSMR3STRUCT_BEGIN);
    if (RT_FAILURE(rc))
        return rc;

    /* put the fields */
    for (PCSSMFIELD pCur = paFields;
         pCur->cb != UINT32_MAX && pCur->off != UINT32_MAX;
         pCur++)
    {
        uint8_t const *pbField = (uint8_t const *)pvStruct + pCur->off;
        switch ((uintptr_t)pCur->pfnGetPutOrTransformer)
        {
            case SSMFIELDTRANS_NO_TRANSFORMATION:
                rc = ssmR3DataWrite(pSSM, pbField, pCur->cb);
                break;

            case SSMFIELDTRANS_GCPTR:
                AssertMsgBreakStmt(pCur->cb == sizeof(RTGCPTR), ("%#x (%s)\n", pCur->cb, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                rc = SSMR3PutGCPtr(pSSM, *(PRTGCPTR)pbField);
                break;

            case SSMFIELDTRANS_GCPHYS:
                AssertMsgBreakStmt(pCur->cb == sizeof(RTGCPHYS), ("%#x (%s)\n", pCur->cb, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                rc = SSMR3PutGCPhys(pSSM, *(PRTGCPHYS)pbField);
                break;

            case SSMFIELDTRANS_RCPTR:
                AssertMsgBreakStmt(pCur->cb == sizeof(RTRCPTR), ("%#x (%s)\n", pCur->cb, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                rc = SSMR3PutRCPtr(pSSM, *(PRTRCPTR)pbField);
                break;

            case SSMFIELDTRANS_RCPTR_ARRAY:
            {
                uint32_t const cEntries = pCur->cb / sizeof(RTRCPTR);
                AssertMsgBreakStmt(pCur->cb == cEntries * sizeof(RTRCPTR) && cEntries, ("%#x (%s)\n", pCur->cb, pCur->pszName),
                                   rc = VERR_SSM_FIELD_INVALID_SIZE);
                rc = VINF_SUCCESS;
                for (uint32_t i = 0; i < cEntries && RT_SUCCESS(rc); i++)
                    rc = SSMR3PutRCPtr(pSSM, ((PRTRCPTR)pbField)[i]);
                break;
            }

            default:
                AssertMsgFailedBreakStmt(("%#x\n", pCur->pfnGetPutOrTransformer), rc = VERR_SSM_FIELD_COMPLEX);
        }
        if (RT_FAILURE(rc))
        {
            if (RT_SUCCESS(pSSM->rc))
                pSSM->rc = rc;
            return rc;
        }
    }

    /* end marker */
    return SSMR3PutU32(pSSM, SSMR3STRUCT_END);
}


/**
 * SSMR3PutStructEx helper that puts a HCPTR that is used as a NULL indicator.
 *
 * @returns VBox status code.
 *
 * @param   pSSM            The saved state handle.
 * @param   pv              The value to put.
 * @param   fFlags          SSMSTRUCT_FLAGS_XXX.
 */
DECLINLINE(int) ssmR3PutHCPtrNI(PSSMHANDLE pSSM, void *pv, uint32_t fFlags)
{
    int rc;
    if (fFlags & SSMSTRUCT_FLAGS_DONT_IGNORE)
        rc = ssmR3DataWrite(pSSM, &pv, sizeof(void *));
    else
        rc = SSMR3PutBool(pSSM, pv != NULL);
    return rc;
}


/**
 * SSMR3PutStructEx helper that puts an arbitrary number of zeros.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   cbToFill        The number of zeros to stuff into the state.
 */
static int ssmR3PutZeros(PSSMHANDLE pSSM, uint32_t cbToFill)
{
    while (cbToFill > 0)
    {
        uint32_t cb = RT_MIN(sizeof(g_abZero), cbToFill);
        int rc = ssmR3DataWrite(pSSM, g_abZero, cb);
        if (RT_FAILURE(rc))
            return rc;
        cbToFill -= cb;
    }
    return VINF_SUCCESS;
}


/**
 * Puts a structure, extended API.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pvStruct        The structure address.
 * @param   cbStruct        The size of the struct (use for validation only).
 * @param   fFlags          Combination of SSMSTRUCT_FLAGS_XXX defines.
 * @param   paFields        The array of structure fields descriptions. The
 *                          array must be terminated by a SSMFIELD_ENTRY_TERM().
 * @param   pvUser          User argument for any callbacks that paFields might
 *                          contain.
 */
VMMR3DECL(int) SSMR3PutStructEx(PSSMHANDLE pSSM, const void *pvStruct, size_t cbStruct,
                                uint32_t fFlags, PCSSMFIELD paFields, void *pvUser)
{
    int rc;

    /*
     * Validation.
     */
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    AssertMsgReturn(!(fFlags & ~SSMSTRUCT_FLAGS_VALID_MASK), ("%#x\n", fFlags), pSSM->rc = VERR_INVALID_PARAMETER);
    AssertPtr(pvStruct);
    AssertPtr(paFields);


    /*
     * Begin marker.
     */
    if (!(fFlags & (SSMSTRUCT_FLAGS_NO_MARKERS | SSMSTRUCT_FLAGS_NO_LEAD_MARKER)))
    {
        rc = SSMR3PutU32(pSSM, SSMR3STRUCT_BEGIN);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Put the fields
     */
    rc = VINF_SUCCESS;
    uint32_t off = 0;
    for (PCSSMFIELD pCur = paFields;
         pCur->cb != UINT32_MAX && pCur->off != UINT32_MAX;
         pCur++)
    {
        uint32_t const offField =    (!SSMFIELDTRANS_IS_PADDING(pCur->pfnGetPutOrTransformer) || pCur->off != UINT32_MAX / 2)
                                  &&  !SSMFIELDTRANS_IS_OLD(pCur->pfnGetPutOrTransformer)
                                ? pCur->off
                                : off;
        uint32_t const cbField  = SSMFIELDTRANS_IS_OLD(pCur->pfnGetPutOrTransformer)
                                ? 0
                                : SSMFIELDTRANS_IS_PADDING(pCur->pfnGetPutOrTransformer)
                                ? RT_HIWORD(pCur->cb)
                                : pCur->cb;
        AssertMsgBreakStmt(   cbField            <= cbStruct
                           && offField + cbField <= cbStruct
                           && offField + cbField >= offField,
                           ("offField=%#x cbField=%#x cbStruct=%#x (%s)\n", offField, cbField, cbStruct, pCur->pszName),
                           rc = VERR_SSM_FIELD_OUT_OF_BOUNDS);
        AssertMsgBreakStmt(   !(fFlags & SSMSTRUCT_FLAGS_FULL_STRUCT)
                           || off == offField,
                           ("off=%#x offField=%#x (%s)\n", off, offField, pCur->pszName),
                           rc = VERR_SSM_FIELD_NOT_CONSECUTIVE);

        rc = VINF_SUCCESS;
        uint8_t const *pbField = (uint8_t const *)pvStruct + offField;
        switch ((uintptr_t)pCur->pfnGetPutOrTransformer)
        {
            case SSMFIELDTRANS_NO_TRANSFORMATION:
                rc = ssmR3DataWrite(pSSM, pbField, cbField);
                break;

            case SSMFIELDTRANS_GCPHYS:
                AssertMsgBreakStmt(cbField == sizeof(RTGCPHYS), ("%#x (%s)\n", cbField, pCur->pszName),
                                   rc = VERR_SSM_FIELD_INVALID_SIZE);
                rc = SSMR3PutGCPhys(pSSM, *(PRTGCPHYS)pbField);
                break;

            case SSMFIELDTRANS_GCPTR:
                AssertMsgBreakStmt(cbField == sizeof(RTGCPTR), ("%#x (%s)\n", cbField, pCur->pszName),
                                   rc = VERR_SSM_FIELD_INVALID_SIZE);
                rc = SSMR3PutGCPtr(pSSM, *(PRTGCPTR)pbField);
                break;

            case SSMFIELDTRANS_RCPTR:
                AssertMsgBreakStmt(cbField == sizeof(RTRCPTR), ("%#x (%s)\n", cbField, pCur->pszName),
                                   rc = VERR_SSM_FIELD_INVALID_SIZE);
                rc = SSMR3PutRCPtr(pSSM, *(PRTRCPTR)pbField);
                break;

            case SSMFIELDTRANS_RCPTR_ARRAY:
            {
                uint32_t const cEntries = cbField / sizeof(RTRCPTR);
                AssertMsgBreakStmt(cbField == cEntries * sizeof(RTRCPTR) && cEntries, ("%#x (%s)\n", cbField, pCur->pszName),
                                   rc = VERR_SSM_FIELD_INVALID_SIZE);
                for (uint32_t i = 0; i < cEntries && RT_SUCCESS(rc); i++)
                    rc = SSMR3PutRCPtr(pSSM, ((PRTRCPTR)pbField)[i]);
                break;
            }

            case SSMFIELDTRANS_HCPTR_NI:
                AssertMsgBreakStmt(cbField == sizeof(void *), ("%#x (%s)\n", cbField, pCur->pszName),
                                   rc = VERR_SSM_FIELD_INVALID_SIZE);
                rc = ssmR3PutHCPtrNI(pSSM, *(void * const *)pbField, fFlags);
                break;

            case SSMFIELDTRANS_HCPTR_NI_ARRAY:
            {
                uint32_t const cEntries = cbField / sizeof(void *);
                AssertMsgBreakStmt(cbField == cEntries * sizeof(void *) && cEntries, ("%#x (%s)\n", cbField, pCur->pszName),
                                   rc = VERR_SSM_FIELD_INVALID_SIZE);
                for (uint32_t i = 0; i < cEntries && RT_SUCCESS(rc); i++)
                    rc = ssmR3PutHCPtrNI(pSSM, ((void * const *)pbField)[i], fFlags);
                break;
            }

            case SSMFIELDTRANS_HCPTR_HACK_U32:
                AssertMsgBreakStmt(cbField == sizeof(void *), ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                AssertMsgBreakStmt(*(uintptr_t *)pbField <= UINT32_MAX, ("%p (%s)\n", *(uintptr_t *)pbField, pCur->pszName),
                                   rc = VERR_SSM_FIELD_INVALID_VALUE);
                rc = ssmR3DataWrite(pSSM, pbField, sizeof(uint32_t));
                if ((fFlags & SSMSTRUCT_FLAGS_DONT_IGNORE) && sizeof(void *) != sizeof(uint32_t) && RT_SUCCESS(rc))
                    rc = ssmR3DataWrite(pSSM, g_abZero, sizeof(uint32_t));
                break;

            case SSMFIELDTRANS_U32_ZX_U64:
                AssertFailedBreakStmt(rc = VERR_SSM_FIELD_LOAD_ONLY_TRANSFORMATION);
                break;

            case SSMFIELDTRANS_IGNORE:
                if (fFlags & SSMSTRUCT_FLAGS_DONT_IGNORE)
                    rc = ssmR3PutZeros(pSSM, cbField);
                break;

            case SSMFIELDTRANS_IGN_GCPHYS:
                AssertMsgBreakStmt(cbField == sizeof(RTGCPHYS), ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                if (fFlags & SSMSTRUCT_FLAGS_DONT_IGNORE)
                    rc = ssmR3DataWrite(pSSM, g_abZero, sizeof(RTGCPHYS));
                break;

            case SSMFIELDTRANS_IGN_GCPTR:
                AssertMsgBreakStmt(cbField == sizeof(RTGCPTR), ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                if (fFlags & SSMSTRUCT_FLAGS_DONT_IGNORE)
                    rc = ssmR3DataWrite(pSSM, g_abZero, sizeof(RTGCPTR));
                break;

            case SSMFIELDTRANS_IGN_RCPTR:
                AssertMsgBreakStmt(cbField == sizeof(RTRCPTR), ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                if (fFlags & SSMSTRUCT_FLAGS_DONT_IGNORE)
                    rc = ssmR3DataWrite(pSSM, g_abZero, sizeof(RTRCPTR));
                break;

            case SSMFIELDTRANS_IGN_HCPTR:
                AssertMsgBreakStmt(cbField == sizeof(void *), ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                if (fFlags & SSMSTRUCT_FLAGS_DONT_IGNORE)
                    rc = ssmR3DataWrite(pSSM, g_abZero, sizeof(void *));
                break;


            case SSMFIELDTRANS_OLD:
                AssertMsgBreakStmt(pCur->off == UINT32_MAX / 2, ("%#x %#x (%s)\n", pCur->cb, pCur->off, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                rc = ssmR3PutZeros(pSSM, pCur->cb);
                break;

            case SSMFIELDTRANS_OLD_GCPHYS:
                AssertMsgBreakStmt(pCur->cb == sizeof(RTGCPHYS) && pCur->off == UINT32_MAX / 2, ("%#x %#x (%s)\n", pCur->cb, pCur->off, pCur->pszName),
                                   rc = VERR_SSM_FIELD_INVALID_SIZE);
                rc = ssmR3DataWrite(pSSM, g_abZero, sizeof(RTGCPHYS));
                break;

            case SSMFIELDTRANS_OLD_GCPTR:
                AssertMsgBreakStmt(pCur->cb == sizeof(RTGCPTR) && pCur->off == UINT32_MAX / 2, ("%#x %#x (%s)\n", pCur->cb, pCur->off, pCur->pszName),
                                   rc = VERR_SSM_FIELD_INVALID_SIZE);
                rc = ssmR3DataWrite(pSSM, g_abZero, sizeof(RTGCPTR));
                break;

            case SSMFIELDTRANS_OLD_RCPTR:
                AssertMsgBreakStmt(pCur->cb == sizeof(RTRCPTR) && pCur->off == UINT32_MAX / 2, ("%#x %#x (%s)\n", pCur->cb, pCur->off, pCur->pszName),
                                   rc = VERR_SSM_FIELD_INVALID_SIZE);
                rc = ssmR3DataWrite(pSSM, g_abZero, sizeof(RTRCPTR));
                break;

            case SSMFIELDTRANS_OLD_HCPTR:
                AssertMsgBreakStmt(pCur->cb == sizeof(void *) && pCur->off == UINT32_MAX / 2, ("%#x %#x (%s)\n", pCur->cb, pCur->off, pCur->pszName),
                                   rc = VERR_SSM_FIELD_INVALID_SIZE);
                rc = ssmR3DataWrite(pSSM, g_abZero, sizeof(void *));
                break;

            case SSMFIELDTRANS_OLD_PAD_HC:
                AssertMsgBreakStmt(pCur->off == UINT32_MAX / 2, ("%#x %#x (%s)\n", pCur->cb, pCur->off, pCur->pszName),
                                   rc = VERR_SSM_FIELD_INVALID_SIZE);
                rc = ssmR3PutZeros(pSSM, HC_ARCH_BITS == 64 ? RT_HIWORD(pCur->cb) : RT_LOWORD(pCur->cb));
                break;

            case SSMFIELDTRANS_OLD_PAD_MSC32:
                AssertMsgBreakStmt(pCur->off == UINT32_MAX / 2, ("%#x %#x (%s)\n", pCur->cb, pCur->off, pCur->pszName),
                                   rc = VERR_SSM_FIELD_INVALID_SIZE);
                if (SSM_HOST_IS_MSC_32)
                    rc = ssmR3PutZeros(pSSM, pCur->cb);
                break;


            case SSMFIELDTRANS_PAD_HC:
            case SSMFIELDTRANS_PAD_HC32:
            case SSMFIELDTRANS_PAD_HC64:
            case SSMFIELDTRANS_PAD_HC_AUTO:
            case SSMFIELDTRANS_PAD_MSC32_AUTO:
            {
                uint32_t cb32    = RT_BYTE1(pCur->cb);
                uint32_t cb64    = RT_BYTE2(pCur->cb);
                uint32_t cbCtx   =    HC_ARCH_BITS == 64
                                   || (   (uintptr_t)pCur->pfnGetPutOrTransformer == SSMFIELDTRANS_PAD_MSC32_AUTO
                                       && !SSM_HOST_IS_MSC_32)
                                 ? cb64 : cb32;
                uint32_t cbSaved =    ssmR3GetHostBits(pSSM) == 64
                                   || (   (uintptr_t)pCur->pfnGetPutOrTransformer == SSMFIELDTRANS_PAD_MSC32_AUTO
                                       && !ssmR3IsHostMsc32(pSSM))
                                 ? cb64 : cb32;
                AssertMsgBreakStmt(    cbField == cbCtx
                                   &&  (   (   pCur->off == UINT32_MAX / 2
                                            && (   cbField == 0
                                                || (uintptr_t)pCur->pfnGetPutOrTransformer == SSMFIELDTRANS_PAD_HC_AUTO
                                                || (uintptr_t)pCur->pfnGetPutOrTransformer == SSMFIELDTRANS_PAD_MSC32_AUTO
                                               )
                                           )
                                        || (pCur->off != UINT32_MAX / 2 && cbField != 0)
                                        )
                                   , ("cbField=%#x cb32=%#x cb64=%#x HC_ARCH_BITS=%u cbCtx=%#x cbSaved=%#x off=%#x\n",
                                      cbField, cb32, cb64, HC_ARCH_BITS, cbCtx, cbSaved, pCur->off),
                                   rc = VERR_SSM_FIELD_INVALID_PADDING_SIZE);
                if (fFlags & SSMSTRUCT_FLAGS_DONT_IGNORE)
                    rc = ssmR3PutZeros(pSSM, cbSaved);
                break;
            }

            default:
                AssertPtrBreakStmt(pCur->pfnGetPutOrTransformer, rc = VERR_SSM_FIELD_INVALID_CALLBACK);
                rc = pCur->pfnGetPutOrTransformer(pSSM, pCur, (void *)pvStruct, fFlags, false /*fGetOrPut*/, pvUser);
                break;
        }
        if (RT_FAILURE(rc))
            break; /* Deal with failures in one place (see below). */

        off = offField + cbField;
    }

    if (RT_SUCCESS(rc))
        AssertMsgStmt(   !(fFlags & SSMSTRUCT_FLAGS_FULL_STRUCT)
                      || off == cbStruct,
                      ("off=%#x cbStruct=%#x\n", off, cbStruct),
                      rc = VERR_SSM_FIELD_NOT_CONSECUTIVE);

    if (RT_FAILURE(rc))
    {
        if (RT_SUCCESS(pSSM->rc))
            pSSM->rc = rc;
        return rc;
    }

    /*
     * End marker
     */
    if (!(fFlags & (SSMSTRUCT_FLAGS_NO_MARKERS | SSMSTRUCT_FLAGS_NO_TAIL_MARKER)))
    {
        rc = SSMR3PutU32(pSSM, SSMR3STRUCT_END);
        if (RT_FAILURE(rc))
            return rc;
    }

    return VINF_SUCCESS;
}


/**
 * Saves a boolean item to the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   fBool           Item to save.
 */
VMMR3DECL(int) SSMR3PutBool(PSSMHANDLE pSSM, bool fBool)
{
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    uint8_t u8 = fBool; /* enforce 1 byte size */
    return ssmR3DataWrite(pSSM, &u8, sizeof(u8));
}


/**
 * Saves a 8-bit unsigned integer item to the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   u8              Item to save.
 */
VMMR3DECL(int) SSMR3PutU8(PSSMHANDLE pSSM, uint8_t u8)
{
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataWrite(pSSM, &u8, sizeof(u8));
}


/**
 * Saves a 8-bit signed integer item to the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   i8              Item to save.
 */
VMMR3DECL(int) SSMR3PutS8(PSSMHANDLE pSSM, int8_t i8)
{
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataWrite(pSSM, &i8, sizeof(i8));
}


/**
 * Saves a 16-bit unsigned integer item to the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   u16             Item to save.
 */
VMMR3DECL(int) SSMR3PutU16(PSSMHANDLE pSSM, uint16_t u16)
{
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataWrite(pSSM, &u16, sizeof(u16));
}


/**
 * Saves a 16-bit signed integer item to the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   i16             Item to save.
 */
VMMR3DECL(int) SSMR3PutS16(PSSMHANDLE pSSM, int16_t i16)
{
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataWrite(pSSM, &i16, sizeof(i16));
}


/**
 * Saves a 32-bit unsigned integer item to the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   u32             Item to save.
 */
VMMR3DECL(int) SSMR3PutU32(PSSMHANDLE pSSM, uint32_t u32)
{
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataWrite(pSSM, &u32, sizeof(u32));
}


/**
 * Saves a 32-bit signed integer item to the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   i32             Item to save.
 */
VMMR3DECL(int) SSMR3PutS32(PSSMHANDLE pSSM, int32_t i32)
{
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataWrite(pSSM, &i32, sizeof(i32));
}


/**
 * Saves a 64-bit unsigned integer item to the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   u64             Item to save.
 */
VMMR3DECL(int) SSMR3PutU64(PSSMHANDLE pSSM, uint64_t u64)
{
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataWrite(pSSM, &u64, sizeof(u64));
}


/**
 * Saves a 64-bit signed integer item to the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   i64             Item to save.
 */
VMMR3DECL(int) SSMR3PutS64(PSSMHANDLE pSSM, int64_t i64)
{
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataWrite(pSSM, &i64, sizeof(i64));
}


/**
 * Saves a 128-bit unsigned integer item to the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   u128            Item to save.
 */
VMMR3DECL(int) SSMR3PutU128(PSSMHANDLE pSSM, uint128_t u128)
{
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataWrite(pSSM, &u128, sizeof(u128));
}


/**
 * Saves a 128-bit signed integer item to the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   i128            Item to save.
 */
VMMR3DECL(int) SSMR3PutS128(PSSMHANDLE pSSM, int128_t i128)
{
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataWrite(pSSM, &i128, sizeof(i128));
}


/**
 * Saves a VBox unsigned integer item to the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   u               Item to save.
 */
VMMR3DECL(int) SSMR3PutUInt(PSSMHANDLE pSSM, RTUINT u)
{
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataWrite(pSSM, &u, sizeof(u));
}


/**
 * Saves a VBox signed integer item to the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   i               Item to save.
 */
VMMR3DECL(int) SSMR3PutSInt(PSSMHANDLE pSSM, RTINT i)
{
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataWrite(pSSM, &i, sizeof(i));
}


/**
 * Saves a GC natural unsigned integer item to the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   u               Item to save.
 *
 * @deprecated Silly type, don't use it.
 */
VMMR3DECL(int) SSMR3PutGCUInt(PSSMHANDLE pSSM, RTGCUINT u)
{
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataWrite(pSSM, &u, sizeof(u));
}


/**
 * Saves a GC unsigned integer register item to the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   u               Item to save.
 */
VMMR3DECL(int) SSMR3PutGCUIntReg(PSSMHANDLE pSSM, RTGCUINTREG u)
{
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataWrite(pSSM, &u, sizeof(u));
}


/**
 * Saves a 32 bits GC physical address item to the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   GCPhys          The item to save
 */
VMMR3DECL(int) SSMR3PutGCPhys32(PSSMHANDLE pSSM, RTGCPHYS32 GCPhys)
{
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataWrite(pSSM, &GCPhys, sizeof(GCPhys));
}


/**
 * Saves a 64 bits GC physical address item to the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   GCPhys          The item to save
 */
VMMR3DECL(int) SSMR3PutGCPhys64(PSSMHANDLE pSSM, RTGCPHYS64 GCPhys)
{
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataWrite(pSSM, &GCPhys, sizeof(GCPhys));
}


/**
 * Saves a GC physical address item to the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   GCPhys          The item to save
 */
VMMR3DECL(int) SSMR3PutGCPhys(PSSMHANDLE pSSM, RTGCPHYS GCPhys)
{
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataWrite(pSSM, &GCPhys, sizeof(GCPhys));
}


/**
 * Saves a GC virtual address item to the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   GCPtr           The item to save.
 */
VMMR3DECL(int) SSMR3PutGCPtr(PSSMHANDLE pSSM, RTGCPTR GCPtr)
{
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataWrite(pSSM, &GCPtr, sizeof(GCPtr));
}


/**
 * Saves an RC virtual address item to the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   RCPtr           The item to save.
 */
VMMR3DECL(int) SSMR3PutRCPtr(PSSMHANDLE pSSM, RTRCPTR RCPtr)
{
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataWrite(pSSM, &RCPtr, sizeof(RCPtr));
}


/**
 * Saves a GC virtual address (represented as an unsigned integer) item to the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   GCPtr           The item to save.
 */
VMMR3DECL(int) SSMR3PutGCUIntPtr(PSSMHANDLE pSSM, RTGCUINTPTR GCPtr)
{
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataWrite(pSSM, &GCPtr, sizeof(GCPtr));
}


/**
 * Saves a I/O port address item to the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   IOPort          The item to save.
 */
VMMR3DECL(int) SSMR3PutIOPort(PSSMHANDLE pSSM, RTIOPORT IOPort)
{
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataWrite(pSSM, &IOPort, sizeof(IOPort));
}


/**
 * Saves a selector item to the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   Sel             The item to save.
 */
VMMR3DECL(int) SSMR3PutSel(PSSMHANDLE pSSM, RTSEL Sel)
{
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataWrite(pSSM, &Sel, sizeof(Sel));
}


/**
 * Saves a memory item to the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pv              Item to save.
 * @param   cb              Size of the item.
 */
VMMR3DECL(int) SSMR3PutMem(PSSMHANDLE pSSM, const void *pv, size_t cb)
{
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataWrite(pSSM, pv, cb);
}


/**
 * Saves a zero terminated string item to the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   psz             Item to save.
 */
VMMR3DECL(int) SSMR3PutStrZ(PSSMHANDLE pSSM, const char *psz)
{
    SSM_ASSERT_WRITEABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);

    size_t cch = strlen(psz);
    if (cch > _1M)
    {
        AssertMsgFailed(("a %zu byte long string, what's this!?!\n", cch));
        return VERR_TOO_MUCH_DATA;
    }
    uint32_t u32 = (uint32_t)cch;
    int rc = ssmR3DataWrite(pSSM, &u32, sizeof(u32));
    if (rc)
        return rc;
    return ssmR3DataWrite(pSSM, psz, cch);
}


/**
 * Emits a SSMLiveControl unit with a new progress report.
 *
 * @returns VBox status code.
 * @param   pSSM                    The saved state handle.
 * @param   lrdPct                  The progress of the live save.
 * @param   uPass                   The current pass.
 */
static int ssmR3LiveControlEmit(PSSMHANDLE pSSM, long double lrdPct, uint32_t uPass)
{
    AssertMsg(lrdPct <= 100.0, ("%u\n", lrdPct * 100));

    /*
     * Make sure we're in one of the two EXEC states or we may fail.
     */
    SSMSTATE enmSavedState = pSSM->enmOp;
    if (enmSavedState == SSMSTATE_LIVE_VOTE)
        pSSM->enmOp = SSMSTATE_LIVE_EXEC;
    else if (enmSavedState == SSMSTATE_SAVE_DONE)
        pSSM->enmOp = SSMSTATE_SAVE_EXEC;

    /*
     * Write the unit header.
     */
    SSMFILEUNITHDRV2 UnitHdr;
    memcpy(&UnitHdr.szMagic[0], SSMFILEUNITHDR_MAGIC, sizeof(UnitHdr.szMagic));
    UnitHdr.offStream       = ssmR3StrmTell(&pSSM->Strm);
    UnitHdr.u32CurStreamCRC = ssmR3StrmCurCRC(&pSSM->Strm);
    UnitHdr.u32CRC          = 0;
    UnitHdr.u32Version      = 1;
    UnitHdr.u32Instance     = 0;
    UnitHdr.u32Pass         = uPass;
    UnitHdr.fFlags          = 0;
    UnitHdr.cbName          = sizeof("SSMLiveControl");
    memcpy(&UnitHdr.szName[0], "SSMLiveControl", UnitHdr.cbName);
    UnitHdr.u32CRC          = RTCrc32(&UnitHdr, RT_UOFFSETOF_DYN(SSMFILEUNITHDRV2, szName[UnitHdr.cbName]));
    Log(("SSM: Unit at %#9llx: '%s', instance %u, pass %#x, version %u\n",
         UnitHdr.offStream, UnitHdr.szName, UnitHdr.u32Instance, UnitHdr.u32Pass, UnitHdr.u32Version));
    int rc = ssmR3StrmWrite(&pSSM->Strm, &UnitHdr, RT_UOFFSETOF_DYN(SSMFILEUNITHDRV2, szName[UnitHdr.cbName]));
    if (RT_SUCCESS(rc))
    {
        /*
         * Write the payload.
         */
        ssmR3DataWriteBegin(pSSM);

        uint16_t u16PartsPerTenThousand = (uint16_t)(lrdPct * (100 - pSSM->uPercentDone));
        AssertMsg(u16PartsPerTenThousand <= 10000, ("%u\n", u16PartsPerTenThousand));
        ssmR3DataWrite(pSSM, &u16PartsPerTenThousand, sizeof(u16PartsPerTenThousand));

        rc = ssmR3DataFlushBuffer(pSSM); /* will return SSMHANDLE::rc if it is set */
        if (RT_SUCCESS(rc))
        {
            /*
             * Write the termination record and flush the compression stream.
             */
            SSMRECTERM TermRec;
            TermRec.u8TypeAndFlags   = SSM_REC_FLAGS_FIXED | SSM_REC_FLAGS_IMPORTANT | SSM_REC_TYPE_TERM;
            TermRec.cbRec            = sizeof(TermRec) - 2;
            if (pSSM->Strm.fChecksummed)
            {
                TermRec.fFlags       = SSMRECTERM_FLAGS_CRC32;
                TermRec.u32StreamCRC = RTCrc32Finish(RTCrc32Process(ssmR3StrmCurCRC(&pSSM->Strm), &TermRec, 2));
            }
            else
            {
                TermRec.fFlags       = 0;
                TermRec.u32StreamCRC = 0;
            }
            TermRec.cbUnit           = pSSM->offUnit + sizeof(TermRec);
            rc = ssmR3DataWriteRaw(pSSM, &TermRec, sizeof(TermRec));
            if (RT_SUCCESS(rc))
                rc = ssmR3DataWriteFinish(pSSM);
            if (RT_SUCCESS(rc))
            {
                pSSM->enmOp = enmSavedState;
                return rc;
            }
        }
    }

    LogRel(("SSM: Failed to write live control unit. rc=%Rrc\n", rc));
    if (RT_SUCCESS_NP(pSSM->rc))
        pSSM->rc = rc;
    pSSM->enmOp = enmSavedState;
    return rc;
}



/**
 * Enters the critical session (optionally) associated with the unit.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   pUnit               The unit.
 */
DECLINLINE(void) ssmR3UnitCritSectEnter(PVM pVM, PSSMUNIT pUnit)
{
    PPDMCRITSECT pCritSect = pUnit->pCritSect;
    if (pCritSect)
    {
        int rc = PDMCritSectEnter(pVM, pCritSect, VERR_IGNORED);
        AssertRC(rc);
    }
}


/**
 * Leaves the critical session (optionally) associated with the unit.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   pUnit               The unit.
 */
DECLINLINE(void) ssmR3UnitCritSectLeave(PVM pVM, PSSMUNIT pUnit)
{
    PPDMCRITSECT pCritSect = pUnit->pCritSect;
    if (pCritSect)
    {
        int rc = PDMCritSectLeave(pVM, pCritSect);
        AssertRC(rc);
    }
}


/**
 * Do the pfnSaveDone run.
 *
 * @returns VBox status code (pSSM->rc).
 * @param   pVM                 The cross context VM structure.
 * @param   pSSM                The saved state handle.
 */
static int ssmR3SaveDoDoneRun(PVM pVM, PSSMHANDLE pSSM)
{
    VM_ASSERT_EMT0(pVM);

    /*
     * Do the done run.
     */
    pSSM->enmOp = SSMSTATE_SAVE_DONE;
    for (PSSMUNIT pUnit = pVM->ssm.s.pHead; pUnit; pUnit = pUnit->pNext)
    {
        if (    pUnit->u.Common.pfnSaveDone
            &&  (   pUnit->fCalled
                 || (!pUnit->u.Common.pfnSavePrep && !pUnit->u.Common.pfnSaveExec)))
        {
            int rcOld = pSSM->rc;
            int rc;
            ssmR3UnitCritSectEnter(pVM, pUnit);
            switch (pUnit->enmType)
            {
                case SSMUNITTYPE_DEV:
                    rc = pUnit->u.Dev.pfnSaveDone(pUnit->u.Dev.pDevIns, pSSM);
                    break;
                case SSMUNITTYPE_DRV:
                    rc = pUnit->u.Drv.pfnSaveDone(pUnit->u.Drv.pDrvIns, pSSM);
                    break;
                case SSMUNITTYPE_USB:
                    rc = pUnit->u.Usb.pfnSaveDone(pUnit->u.Usb.pUsbIns, pSSM);
                    break;
                case SSMUNITTYPE_INTERNAL:
                    rc = pUnit->u.Internal.pfnSaveDone(pVM, pSSM);
                    break;
                case SSMUNITTYPE_EXTERNAL:
                    rc = pUnit->u.External.pfnSaveDone(pSSM, VMMR3GetVTable(), pUnit->u.External.pvUser);
                    break;
                default:
                    rc = VERR_SSM_IPE_1;
                    break;
            }
            ssmR3UnitCritSectLeave(pVM, pUnit);
            if (RT_SUCCESS(rc) && pSSM->rc != rcOld)
                rc = pSSM->rc;
            if (RT_FAILURE(rc))
            {
                LogRel(("SSM: Done save failed with rc=%Rrc for data unit '%s.\n", rc, pUnit->szName));
                if (RT_SUCCESS_NP(pSSM->rc))
                    pSSM->rc = rc;
            }
        }
    }
    return pSSM->rc;
}


/**
 * Worker for SSMR3LiveDone and SSMR3Save that closes the handle and deletes the
 * saved state file on failure.
 *
 * @returns VBox status code (pSSM->rc).
 * @param   pVM                 The cross context VM structure.
 * @param   pSSM                The saved state handle.
 */
static int ssmR3SaveDoClose(PVM pVM, PSSMHANDLE pSSM)
{
    VM_ASSERT_EMT0(pVM);
    pVM->ssm.s.uPass = 0;

    /*
     * Make it non-cancellable, close the stream and delete the file on failure.
     */
    ssmR3SetCancellable(pVM, pSSM, false);
    int rc = ssmR3StrmClose(&pSSM->Strm, pSSM->rc == VERR_SSM_CANCELLED);
    if (RT_SUCCESS(rc))
        rc = pSSM->rc;
    if (RT_SUCCESS(rc))
    {
        Assert(pSSM->enmOp == SSMSTATE_SAVE_DONE);
        if (pSSM->pfnProgress)
            pSSM->pfnProgress(pVM->pUVM, 100, pSSM->pvUser);
        LogRel(("SSM: Successfully saved the VM state to '%s'\n",
                pSSM->pszFilename ? pSSM->pszFilename : "<remote-machine>"));
    }
    else
    {
        if (pSSM->pszFilename)
        {
            int rc2 = RTFileDelete(pSSM->pszFilename);
            AssertRC(rc2);
            if (RT_SUCCESS(rc2))
                LogRel(("SSM: Failed to save the VM state to '%s' (file deleted): %Rrc\n",
                        pSSM->pszFilename, rc));
            else
                LogRel(("SSM: Failed to save the VM state to '%s' (file deletion failed, rc2=%Rrc): %Rrc\n",
                        pSSM->pszFilename, rc2, rc));
        }
        else
            LogRel(("SSM: Failed to save the VM state.\n"));

        Assert(pSSM->enmOp <= SSMSTATE_SAVE_DONE);
        if (pSSM->enmOp != SSMSTATE_SAVE_DONE)
            ssmR3SaveDoDoneRun(pVM, pSSM);
    }

    /*
     * Trash the handle before freeing it.
     */
    ASMAtomicWriteU32(&pSSM->fCancelled, 0);
    pSSM->pVM = NULL;
    pSSM->enmAfter = SSMAFTER_INVALID;
    pSSM->enmOp = SSMSTATE_INVALID;
    RTMemFree(pSSM);

    return rc;
}


/**
 * Closes the SSM handle.
 *
 * This must always be called on a handled returned by SSMR3LiveSave.
 *
 * @returns VBox status code.
 *
 * @param   pSSM            The SSM handle returned by SSMR3LiveSave.
 *
 * @thread  EMT(0).
 */
VMMR3_INT_DECL(int) SSMR3LiveDone(PSSMHANDLE pSSM)
{
    LogFlow(("SSMR3LiveDone: pSSM=%p\n", pSSM));

    /*
     * Validate input.
     */
    AssertPtrReturn(pSSM, VERR_INVALID_POINTER);
    PVM pVM = pSSM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_EMT0(pVM);
    AssertMsgReturn(   pSSM->enmAfter == SSMAFTER_DESTROY
                    || pSSM->enmAfter == SSMAFTER_CONTINUE
                    || pSSM->enmAfter == SSMAFTER_TELEPORT,
                    ("%d\n", pSSM->enmAfter),
                    VERR_INVALID_PARAMETER);
    AssertMsgReturn(    pSSM->enmOp >= SSMSTATE_LIVE_PREP
                    &&  pSSM->enmOp <= SSMSTATE_SAVE_DONE,
                    ("%d\n", pSSM->enmOp), VERR_INVALID_STATE);

    /*
     * Join paths with SSMR3Save again.
     */
    return ssmR3SaveDoClose(pVM, pSSM);
}


/**
 * Writes the directory.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pSSM                The SSM handle.
 * @param   pcEntries           Where to return the number of directory entries.
 */
static int ssmR3WriteDirectory(PVM pVM, PSSMHANDLE pSSM, uint32_t *pcEntries)
{
    VM_ASSERT_EMT0(pVM);

    /*
     * Grab some temporary memory for the dictionary.
     */
    size_t      cbDir = RT_UOFFSETOF_DYN(SSMFILEDIR, aEntries[pVM->ssm.s.cUnits]);
    PSSMFILEDIR pDir  = (PSSMFILEDIR)RTMemTmpAlloc(cbDir);
    if (!pDir)
    {
        LogRel(("ssmR3WriteDirectory: failed to allocate %zu bytes!\n", cbDir));
        return VERR_NO_TMP_MEMORY;
    }

    /*
     * Initialize it.
     */
    memcpy(pDir->szMagic, SSMFILEDIR_MAGIC, sizeof(pDir->szMagic));
    pDir->u32CRC   = 0;
    pDir->cEntries = 0;

    for (PSSMUNIT pUnit = pVM->ssm.s.pHead; pUnit; pUnit = pUnit->pNext)
        if (pUnit->offStream != RTFOFF_MIN)
        {
            PSSMFILEDIRENTRY pEntry = &pDir->aEntries[pDir->cEntries++];
            Assert(pDir->cEntries <= pVM->ssm.s.cUnits);
            Assert(pUnit->offStream >= (RTFOFF)sizeof(SSMFILEHDR));
            pEntry->off         = pUnit->offStream;
            pEntry->u32Instance = pUnit->u32Instance;
            pEntry->u32NameCRC  = RTCrc32(pUnit->szName, pUnit->cchName);
        }

    /*
     * Calculate the actual size and CRC-32, then write the directory
     * out to the stream.
     */
    *pcEntries = pDir->cEntries;
    cbDir = RT_UOFFSETOF_DYN(SSMFILEDIR, aEntries[pDir->cEntries]);
    pDir->u32CRC = RTCrc32(pDir, cbDir);
    int rc = ssmR3StrmWrite(&pSSM->Strm, pDir, cbDir);
    RTMemTmpFree(pDir);
    return rc;
}


/**
 * Writes out the END unit.
 */
static int ssmR3WriteEndUnit(PSSMHANDLE pSSM)
{
    AssertRC(pSSM->rc);
    SSMFILEUNITHDRV2 UnitHdr;
    memcpy(&UnitHdr.szMagic[0], SSMFILEUNITHDR_END, sizeof(UnitHdr.szMagic));
    UnitHdr.offStream       = ssmR3StrmTell(&pSSM->Strm);
    UnitHdr.u32CurStreamCRC = ssmR3StrmCurCRC(&pSSM->Strm);
    UnitHdr.u32CRC          = 0;
    UnitHdr.u32Version      = 0;
    UnitHdr.u32Instance     = 0;
    UnitHdr.u32Pass         = SSM_PASS_FINAL;
    UnitHdr.fFlags          = 0;
    UnitHdr.cbName          = 0;
    UnitHdr.u32CRC          = RTCrc32(&UnitHdr, RT_UOFFSETOF(SSMFILEUNITHDRV2, szName[0]));
    Log(("SSM: Unit at %#9llx: END UNIT\n", UnitHdr.offStream));
    int rc = ssmR3StrmWrite(&pSSM->Strm, &UnitHdr, RT_UOFFSETOF(SSMFILEUNITHDRV2, szName[0]));
    if (RT_FAILURE(rc))
    {
        LogRel(("SSM: Failed writing the end unit: %Rrc\n", rc));
        pSSM->rc = rc;
    }
    return rc;
}


/**
 * Writes out the file footer and sets the end of the stream.
 */
static int ssmR3WriteFileFooter(PSSMHANDLE pSSM, uint32_t cDirEntries)
{
    SSMFILEFTR Footer;
    memcpy(Footer.szMagic, SSMFILEFTR_MAGIC, sizeof(Footer.szMagic));
    Footer.offStream    = ssmR3StrmTell(&pSSM->Strm);
    Footer.u32StreamCRC = ssmR3StrmFinalCRC(&pSSM->Strm);
    Footer.cDirEntries  = cDirEntries;
    Footer.u32Reserved  = 0;
    Footer.u32CRC       = 0;
    Footer.u32CRC       = RTCrc32(&Footer, sizeof(Footer));
    Log(("SSM: Footer at %#9llx: \n", Footer.offStream));
    int rc = ssmR3StrmWrite(&pSSM->Strm, &Footer, sizeof(Footer));
    if (RT_SUCCESS(rc))
        rc = ssmR3StrmSetEnd(&pSSM->Strm);
    if (RT_SUCCESS(rc))
        LogRel(("SSM: Footer at %#llx (%lld), %u directory entries.\n", Footer.offStream, Footer.offStream, Footer.cDirEntries));
    else
    {
        LogRel(("SSM: Failed writing the footer: %Rrc\n", rc));
        pSSM->rc = rc;
    }
    return rc;
}


/**
 * Finalize the saved state stream, i.e. add the end unit, directory
 * and footer.
 *
 * @returns VBox status code (pSSM->rc).
 * @param   pVM                 The cross context VM structure.
 * @param   pSSM                The saved state handle.
 */
static int ssmR3SaveDoFinalization(PVM pVM, PSSMHANDLE pSSM)
{
    VM_ASSERT_EMT0(pVM);
    Assert(RT_SUCCESS(pSSM->rc));

    /*
     * Write the end unit.
     */
    int rc = ssmR3WriteEndUnit(pSSM);
    if (RT_SUCCESS(rc))
    {
        /*
         * Write the directory for the final units and then the footer.
         */
        uint32_t cDirEntries = 0;
        rc = ssmR3WriteDirectory(pVM, pSSM, &cDirEntries);
        if (RT_SUCCESS(rc))
        {
            /*
             * Write the footer.
             */
            rc = ssmR3WriteFileFooter(pSSM, cDirEntries);
        }
        else
        {
            LogRel(("SSM: Failed writing the directory: %Rrc\n", rc));
            pSSM->rc = rc;
        }
    }
    return rc;
}


/**
 * Writes the file directory and footer.
 *
 * @returns VBox status code.
 * @param   pSSM                The SSM handle.
 */
VMMR3DECL(int) SSMR3WriteFileFooter(PSSMHANDLE pSSM)
{
    AssertPtrReturn(pSSM, VERR_INVALID_POINTER);
    AssertMsgReturn(pSSM->enmOp == SSMSTATE_OPEN_WRITE, ("%d\n", pSSM->enmOp), VERR_INVALID_PARAMETER);
    AssertMsgReturn(pSSM->enmAfter == SSMAFTER_OPENED, ("%d\n", pSSM->enmAfter),VERR_INVALID_PARAMETER);
    Assert(pSSM->fCancelled == SSMHANDLE_OK);

    /*
     * Write the directory.
     */
    PSSMFILEDIR pDir = pSSM->u.Write.pDir;
    if (!pDir)
    {
        /* Allocate an empty directory as a placeholder. (Don't know we can
           read this back, but whatever) */
        pSSM->u.Write.pDir = pDir = (PSSMFILEDIR)RTMemAllocZ(RT_UOFFSETOF(SSMFILEDIR, aEntries[1]));
        AssertReturn(pDir, VERR_NO_MEMORY);
        pSSM->u.Write.cDirEntriesAlloced = 1;
    }
    memcpy(pDir->szMagic, SSMFILEDIR_MAGIC, sizeof(pDir->szMagic));
    pDir->u32CRC = 0;
    uint32_t const cbDir = RT_UOFFSETOF_DYN(SSMFILEDIR, aEntries[pDir->cEntries]);
    pDir->u32CRC = RTCrc32(pDir, cbDir);
    int rc = ssmR3StrmWrite(&pSSM->Strm, pDir, cbDir);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Write the footer.
     */
    return ssmR3WriteFileFooter(pSSM, pDir->cEntries);
}


/**
 * Works the progress calculation during the exec part of a live save.
 *
 * @param   pSSM        The SSM handle.
 * @param   iUnit       The current unit number.
 */
static void ssmR3ProgressByUnit(PSSMHANDLE pSSM, uint32_t iUnit)
{
    if (pSSM->fLiveSave)
    {
        unsigned    uPctExec = iUnit * 100 / pSSM->pVM->ssm.s.cUnits;
        unsigned    cPctExec = 100 - pSSM->uPercentDone - pSSM->uPercentPrepare - pSSM->uPercentLive;
        long double lrdPct   = (long double)uPctExec * cPctExec / 100 + pSSM->uPercentPrepare + pSSM->uPercentLive;
        unsigned    uPct     = (unsigned)lrdPct;
        if (uPct != pSSM->uPercent)
        {
            ssmR3LiveControlEmit(pSSM, lrdPct, SSM_PASS_FINAL);
            pSSM->uPercent =  uPct;
            pSSM->pfnProgress(pSSM->pVM->pUVM, uPct, pSSM->pvUser);
        }
    }
}


/**
 * Head code for saving a unit shared by ssmR3SaveDoExecRun() and
 * SSMR3WriteUnitBegin().
 */
static int ssmR3SaveDoExecRunOneUnitBegin(PSSMHANDLE pSSM, PSSMUNIT pUnit, unsigned iUnit)
{
    /*
     * Not all unit have a callback. Skip those which don't and
     * make sure to keep the progress indicator up to date.
     */
    ssmR3ProgressByUnit(pSSM, iUnit);
    pSSM->offEstUnitEnd += pUnit->cbGuess;
    if (!pUnit->u.Common.pfnSaveExec)
    {
        pUnit->fCalled = true;
        if (pUnit->cbGuess)
            ssmR3ProgressByByte(pSSM, pSSM->offEstUnitEnd - pSSM->offEst);
        return VINF_NO_CHANGE;
    }
    pUnit->offStream = ssmR3StrmTell(&pSSM->Strm);

    /*
     * Check for cancellation.
     */
    if (RT_UNLIKELY(ASMAtomicUoReadU32(&(pSSM)->fCancelled) == SSMHANDLE_CANCELLED))
    {
        LogRel(("SSM: Cancelled!\n"));
        AssertRC(pSSM->rc);
        return pSSM->rc = VERR_SSM_CANCELLED;
    }

    /*
     * Write data unit header
     */
    SSMFILEUNITHDRV2 UnitHdr;
    memcpy(&UnitHdr.szMagic[0], SSMFILEUNITHDR_MAGIC, sizeof(UnitHdr.szMagic));
    UnitHdr.offStream       = pUnit->offStream;
    UnitHdr.u32CurStreamCRC = ssmR3StrmCurCRC(&pSSM->Strm);
    UnitHdr.u32CRC          = 0;
    UnitHdr.u32Version      = pUnit->u32Version;
    UnitHdr.u32Instance     = pUnit->u32Instance;
    UnitHdr.u32Pass         = SSM_PASS_FINAL;
    UnitHdr.fFlags          = 0;
    UnitHdr.cbName          = (uint32_t)pUnit->cchName + 1;
    memcpy(&UnitHdr.szName[0], &pUnit->szName[0], UnitHdr.cbName);
    UnitHdr.u32CRC          = RTCrc32(&UnitHdr, RT_UOFFSETOF_DYN(SSMFILEUNITHDRV2, szName[UnitHdr.cbName]));
    Log(("SSM: Unit at %#9llx: '%s', instance %u, pass %#x, version %u\n",
         UnitHdr.offStream, UnitHdr.szName, UnitHdr.u32Instance, UnitHdr.u32Pass, UnitHdr.u32Version));
    int rc = ssmR3StrmWrite(&pSSM->Strm, &UnitHdr, RT_UOFFSETOF_DYN(SSMFILEUNITHDRV2, szName[UnitHdr.cbName]));
    if (RT_FAILURE(rc))
    {
        LogRel(("SSM: Failed to write unit header. rc=%Rrc\n", rc));
        return pSSM->rc = rc;
    }

    ssmR3DataWriteBegin(pSSM);
    return VINF_SUCCESS;
}


/**
 * Tail code for saving a unit shared by ssmR3SaveDoExecRun() and
 * SSMR3WriteUnitComplete().
 */
static int ssmR3SaveDoExecRunOneUnitComplete(PSSMHANDLE pSSM)
{
    /*
     * Write the termination record and flush the compression stream.
     */
    SSMRECTERM TermRec;
    TermRec.u8TypeAndFlags   = SSM_REC_FLAGS_FIXED | SSM_REC_FLAGS_IMPORTANT | SSM_REC_TYPE_TERM;
    TermRec.cbRec            = sizeof(TermRec) - 2;
    if (pSSM->Strm.fChecksummed)
    {
        TermRec.fFlags       = SSMRECTERM_FLAGS_CRC32;
        TermRec.u32StreamCRC = RTCrc32Finish(RTCrc32Process(ssmR3StrmCurCRC(&pSSM->Strm), &TermRec, 2));
    }
    else
    {
        TermRec.fFlags       = 0;
        TermRec.u32StreamCRC = 0;
    }
    TermRec.cbUnit           = pSSM->offUnit + sizeof(TermRec);
    int rc = ssmR3DataWriteRaw(pSSM, &TermRec, sizeof(TermRec));
    if (RT_SUCCESS(rc))
        rc = ssmR3DataWriteFinish(pSSM);
    if (RT_FAILURE(rc))
    {
        LogRel(("SSM: Failed terminating unit: %Rrc\n", rc));
        return pSSM->rc = rc;
    }

    /*
     * Advance the progress indicator to the end of the current unit.
     */
    ssmR3ProgressByByte(pSSM, pSSM->offEstUnitEnd - pSSM->offEst);

    return VINF_SUCCESS;
}


/**
 * Do the pfnSaveExec run.
 *
 * @returns VBox status code (pSSM->rc).
 * @param   pVM                 The cross context VM structure.
 * @param   pSSM                The saved state handle.
 */
static int ssmR3SaveDoExecRun(PVM pVM, PSSMHANDLE pSSM)
{
    VM_ASSERT_EMT0(pVM);
    AssertRC(pSSM->rc);
    pSSM->rc = VINF_SUCCESS;
    pSSM->enmOp = SSMSTATE_SAVE_EXEC;
    unsigned iUnit = 0;
    for (PSSMUNIT pUnit = pVM->ssm.s.pHead; pUnit; pUnit = pUnit->pNext, iUnit++)
    {
        /*
         * Begin unit saving.
         */
        int rc = ssmR3SaveDoExecRunOneUnitBegin(pSSM, pUnit, iUnit);
        if (rc != VINF_SUCCESS)
        {
            Assert(RT_FAILURE_NP(rc) || (!pUnit->u.Common.pfnSaveExec && rc == VINF_NO_CHANGE));
            if (RT_SUCCESS(rc))
                continue;
            return rc;
        }

        /*
         * Call the execute handler.
         */
        ssmR3UnitCritSectEnter(pVM, pUnit);
        switch (pUnit->enmType)
        {
            case SSMUNITTYPE_DEV:
                rc = pUnit->u.Dev.pfnSaveExec(pUnit->u.Dev.pDevIns, pSSM);
                break;
            case SSMUNITTYPE_DRV:
                rc = pUnit->u.Drv.pfnSaveExec(pUnit->u.Drv.pDrvIns, pSSM);
                break;
            case SSMUNITTYPE_USB:
                rc = pUnit->u.Usb.pfnSaveExec(pUnit->u.Usb.pUsbIns, pSSM);
                break;
            case SSMUNITTYPE_INTERNAL:
                rc = pUnit->u.Internal.pfnSaveExec(pVM, pSSM);
                break;
            case SSMUNITTYPE_EXTERNAL:
                rc = pUnit->u.External.pfnSaveExec(pSSM, VMMR3GetVTable(), pUnit->u.External.pvUser);
                break;
            default:
                rc = VERR_SSM_IPE_1;
                break;
        }
        ssmR3UnitCritSectLeave(pVM, pUnit);
        pUnit->fCalled = true;
        if (RT_FAILURE(rc) && RT_SUCCESS_NP(pSSM->rc))
            pSSM->rc = rc;
        else
            rc = ssmR3DataFlushBuffer(pSSM); /* will return SSMHANDLE::rc if it is set */
        if (RT_FAILURE(rc))
        {
            LogRel(("SSM: Execute save failed with rc=%Rrc for data unit '%s'/#%u.\n", rc, pUnit->szName, pUnit->u32Instance));
            return rc;
        }

        /*
         * Finish the unit.
         */
        rc = ssmR3SaveDoExecRunOneUnitComplete(pSSM);
        if (RT_FAILURE(rc))
        {
            Assert(RT_FAILURE_NP(pSSM->rc));
            return rc;
        }
    } /* for each unit */
    ssmR3ProgressByUnit(pSSM, pVM->ssm.s.cUnits);

    /* (progress should be pending 99% now) */
    AssertMsg(   pSSM->uPercent == 101 - pSSM->uPercentDone
              || pSSM->uPercent == 100 - pSSM->uPercentDone,
              ("%d\n", pSSM->uPercent));
    return VINF_SUCCESS;
}


/**
 * Begins saving a unit to a SSM file/stream opened via SSMR3Open.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pszName         The unit name.
 * @param   uVersion        The unit version.
 * @param   uInstance       The unit instance number.
 */
VMMR3DECL(int) SSMR3WriteUnitBegin(PSSMHANDLE pSSM, const char *pszName, uint32_t uVersion, uint32_t uInstance)
{
    /*
     * Validate input and state.
     */
    AssertPtrReturn(pSSM, VERR_INVALID_POINTER);
    AssertMsgReturn(pSSM->enmOp == SSMSTATE_OPEN_WRITE, ("%d\n", pSSM->enmOp), VERR_INVALID_PARAMETER);
    AssertMsgReturn(pSSM->enmAfter == SSMAFTER_OPENED, ("%d\n", pSSM->enmAfter),VERR_INVALID_PARAMETER);

    AssertPtrReturn(pszName, VERR_INVALID_PARAMETER);
    size_t const cchName = strlen(pszName);
    AssertReturn(cchName > 0 && cchName < SSM_MAX_NAME_SIZE, VERR_INVALID_NAME);


    /*
     * We abuse u32CRC to indicate whether we've started a unit.
     */
    AssertRCReturn(pSSM->rc, pSSM->rc);
    AssertReturn(!pSSM->u.Write.pDir || pSSM->u.Write.pDir->u32CRC == 0, pSSM->rc = VERR_INVALID_STATE);

    /*
     * Add the unit to the directory.
     */
    PSSMFILEDIR pDir     = pSSM->u.Write.pDir;
    uint32_t    cAlloced = pSSM->u.Write.cDirEntriesAlloced;
    if (!pDir || pDir->cEntries + 1 > cAlloced)
    {
        pDir = (PSSMFILEDIR)RTMemReallocZ(pDir, cAlloced ? RT_UOFFSETOF_DYN(SSMFILEDIR, aEntries[cAlloced]) : 0,
                                          RT_UOFFSETOF_DYN(SSMFILEDIR, aEntries[cAlloced + 16]));
        AssertReturn(pDir, pSSM->rc = VERR_NO_MEMORY);
        pSSM->u.Write.cDirEntriesAlloced = cAlloced + 16;
        pSSM->u.Write.pDir               = pDir;
    }
    pDir->aEntries[pDir->cEntries].off         = ssmR3StrmTell(&pSSM->Strm);
    pDir->aEntries[pDir->cEntries].u32Instance = uInstance;
    pDir->aEntries[pDir->cEntries].u32NameCRC  = RTCrc32(pszName, cchName);
    pDir->cEntries++;

    /*
     * Create a unit structure and join paths with ssmR3SaveDoExecRun.
     */
    union
    {
        SSMUNIT Unit;
        uint8_t abPadding[RT_UOFFSETOF(SSMUNIT, szName) + SSM_MAX_NAME_SIZE];
    } u;
    RT_ZERO(u);
    u.Unit.enmType              = SSMUNITTYPE_EXTERNAL;
    u.Unit.u32Version           = uVersion;
    u.Unit.u32Instance          = uInstance;
    u.Unit.offStream            = 0;
    u.Unit.pCritSect            = NULL;
    u.Unit.u.Common.pfnSaveExec = (PFNRT)~(uintptr_t)1; /* must be non-zero */
    u.Unit.cbGuess              = 64; /* whatever */
    u.Unit.cchName              = cchName;
    memcpy(u.Unit.szName, pszName, cchName + 1);

    int rc = ssmR3SaveDoExecRunOneUnitBegin(pSSM, &u.Unit, pDir->cEntries - 1);
    if (rc == VINF_SUCCESS)
        pSSM->u.Write.pDir->u32CRC = 1; /* See above. */
    else
        Assert(RT_FAILURE_NP(rc));
    return rc;
}


/**
 * Completes a unit started by SSMR3WriteUnitBegin().
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 */
VMMR3DECL(int) SSMR3WriteUnitComplete(PSSMHANDLE pSSM)
{
    /*
     * Validate input and state.
     */
    AssertPtrReturn(pSSM, VERR_INVALID_POINTER);
    AssertMsgReturn(pSSM->enmOp == SSMSTATE_OPEN_WRITE, ("%d\n", pSSM->enmOp), VERR_INVALID_PARAMETER);
    AssertMsgReturn(pSSM->enmAfter == SSMAFTER_OPENED, ("%d\n", pSSM->enmAfter),VERR_INVALID_PARAMETER);

    AssertReturn(pSSM->u.Write.pDir, VERR_INVALID_STATE);
    AssertReturn(pSSM->u.Write.pDir->cEntries > 0, VERR_INVALID_STATE);
    AssertReturn(pSSM->u.Write.pDir->u32CRC == 1, VERR_INVALID_STATE);

    AssertRCReturn(pSSM->rc, pSSM->rc);
    pSSM->u.Write.pDir->u32CRC = 0;

    /*
     * Finish the unit.
     */
    int rc = ssmR3DataFlushBuffer(pSSM); /* will return SSMHANDLE::rc if it is set */
    if (RT_SUCCESS(rc))
        rc = ssmR3SaveDoExecRunOneUnitComplete(pSSM);
    return rc;
}


/**
 * Do the pfnSavePrep run.
 *
 * @returns VBox status code (pSSM->rc).
 * @param   pVM                 The cross context VM structure.
 * @param   pSSM                The saved state handle.
 */
static int ssmR3SaveDoPrepRun(PVM pVM, PSSMHANDLE pSSM)
{
    VM_ASSERT_EMT0(pVM);
    Assert(RT_SUCCESS(pSSM->rc));
    pSSM->enmOp = SSMSTATE_SAVE_PREP;
    for (PSSMUNIT pUnit = pVM->ssm.s.pHead; pUnit; pUnit = pUnit->pNext)
    {
        if (pUnit->u.Common.pfnSavePrep)
        {
            int rc;
            ssmR3UnitCritSectEnter(pVM, pUnit);
            switch (pUnit->enmType)
            {
                case SSMUNITTYPE_DEV:
                    rc = pUnit->u.Dev.pfnSavePrep(pUnit->u.Dev.pDevIns, pSSM);
                    break;
                case SSMUNITTYPE_DRV:
                    rc = pUnit->u.Drv.pfnSavePrep(pUnit->u.Drv.pDrvIns, pSSM);
                    break;
                case SSMUNITTYPE_USB:
                    rc = pUnit->u.Usb.pfnSavePrep(pUnit->u.Usb.pUsbIns, pSSM);
                    break;
                case SSMUNITTYPE_INTERNAL:
                    rc = pUnit->u.Internal.pfnSavePrep(pVM, pSSM);
                    break;
                case SSMUNITTYPE_EXTERNAL:
                    rc = pUnit->u.External.pfnSavePrep(pSSM, VMMR3GetVTable(), pUnit->u.External.pvUser);
                    break;
                default:
                    rc = VERR_SSM_IPE_1;
                    break;
            }
            ssmR3UnitCritSectLeave(pVM, pUnit);
            pUnit->fCalled = true;
            if (RT_FAILURE(rc) && RT_SUCCESS_NP(pSSM->rc))
                pSSM->rc = rc;
            else
                rc = pSSM->rc;
            if (RT_FAILURE(rc))
            {
                LogRel(("SSM: Prepare save failed with rc=%Rrc for data unit '%s.\n", rc, pUnit->szName));
                return rc;
            }
        }

        pSSM->cbEstTotal += pUnit->cbGuess;
    }

    /*
     * Work the progress indicator if we got one.
     */
    if (pSSM->pfnProgress)
        pSSM->pfnProgress(pVM->pUVM, pSSM->uPercentPrepare + pSSM->uPercentLive - 1, pSSM->pvUser);
    pSSM->uPercent = pSSM->uPercentPrepare + pSSM->uPercentLive;

    return VINF_SUCCESS;
}


/**
 * Common worker for SSMR3Save and SSMR3LiveSave.
 *
 * @returns VBox status code (no need to check pSSM->rc).
 * @param   pVM                 The cross context VM structure.
 * @param   pSSM                The state handle.
 *
 * @thread  EMT(0)
 */
static int ssmR3SaveDoCommon(PVM pVM, PSSMHANDLE pSSM)
{
    VM_ASSERT_EMT0(pVM);

    /*
     * Do the work.
     */
    int rc = ssmR3SaveDoPrepRun(pVM, pSSM);
    if (RT_SUCCESS(rc))
    {
        rc = ssmR3SaveDoExecRun(pVM, pSSM);
        if (RT_SUCCESS(rc))
            rc = ssmR3SaveDoFinalization(pVM, pSSM);
    }
    Assert(pSSM->rc == rc);
    int rc2 = ssmR3SaveDoDoneRun(pVM, pSSM);
    if (RT_SUCCESS(rc))
        rc = rc2;

    return rc;
}


/**
 * Saves the rest of the state on EMT0.
 *
 * @returns VBox status code.
 *
 * @param   pSSM            The SSM handle returned by SSMR3LiveSave.
 *
 * @thread  Non-EMT thread. Will involve the EMT at the end of the operation.
 */
VMMR3_INT_DECL(int) SSMR3LiveDoStep2(PSSMHANDLE pSSM)
{
    LogFlow(("SSMR3LiveDoStep2: pSSM=%p\n", pSSM));

    /*
     * Validate input.
     */
    AssertPtrReturn(pSSM, VERR_INVALID_POINTER);
    PVM pVM = pSSM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_EMT0(pVM);
    AssertMsgReturn(   pSSM->enmAfter == SSMAFTER_DESTROY
                    || pSSM->enmAfter == SSMAFTER_CONTINUE
                    || pSSM->enmAfter == SSMAFTER_TELEPORT,
                    ("%d\n", pSSM->enmAfter),
                    VERR_INVALID_PARAMETER);
    AssertMsgReturn(pSSM->enmOp == SSMSTATE_LIVE_STEP2, ("%d\n", pSSM->enmOp), VERR_INVALID_STATE);
    AssertRCReturn(pSSM->rc, pSSM->rc);

    /*
     * Join paths with VMMR3Save.
     */
    return ssmR3SaveDoCommon(pVM, pSSM);
}


/**
 * Writes the file header.
 *
 * @returns VBox status code.
 * @param   pSSM                The SSM handle.
 * @param   cUnits              Copy of pVM->ssm.s.cUnits, typically.
 */
static int ssmR3WriteFileHeader(PSSMHANDLE pSSM, uint32_t cUnits)
{
    SSMFILEHDR FileHdr;
    memcpy(&FileHdr.szMagic, SSMFILEHDR_MAGIC_V2_0, sizeof(FileHdr.szMagic));
    FileHdr.u16VerMajor  = VBOX_VERSION_MAJOR;
    FileHdr.u16VerMinor  = VBOX_VERSION_MINOR;
    FileHdr.u32VerBuild  = VBOX_VERSION_BUILD;
    FileHdr.u32SvnRev    = VMMGetSvnRev();
    FileHdr.cHostBits    = HC_ARCH_BITS;
    FileHdr.cbGCPhys     = sizeof(RTGCPHYS);
    FileHdr.cbGCPtr      = sizeof(RTGCPTR);
    FileHdr.u8Reserved   = 0;
    FileHdr.cUnits       = cUnits;
    FileHdr.fFlags       = SSMFILEHDR_FLAGS_STREAM_CRC32;
    if (pSSM->fLiveSave)
        FileHdr.fFlags  |= SSMFILEHDR_FLAGS_STREAM_LIVE_SAVE;
    FileHdr.cbMaxDecompr = RT_SIZEOFMEMB(SSMHANDLE, u.Read.abDataBuffer);
    FileHdr.u32CRC       = 0;
    FileHdr.u32CRC       = RTCrc32(&FileHdr, sizeof(FileHdr));
    return ssmR3StrmWrite(&pSSM->Strm, &FileHdr, sizeof(FileHdr));
}


/**
 * Writes the file header.
 *
 * @returns VBox status code.
 * @param   pSSM                The SSM handle.
 * @param   cUnits              Number of units to advertise in the header.
 */
VMMR3DECL(int) SSMR3WriteFileHeader(PSSMHANDLE pSSM, uint32_t cUnits)
{
    AssertPtrReturn(pSSM, VERR_INVALID_POINTER);
    AssertMsgReturn(pSSM->enmOp == SSMSTATE_OPEN_WRITE, ("%d\n", pSSM->enmOp), VERR_INVALID_PARAMETER);
    AssertMsgReturn(pSSM->enmAfter == SSMAFTER_OPENED, ("%d\n", pSSM->enmAfter),VERR_INVALID_PARAMETER);
    AssertMsgReturn(cUnits > 0 && cUnits < _4K, ("%d\n", cUnits),VERR_INVALID_PARAMETER);
    Assert(pSSM->fCancelled == SSMHANDLE_OK);

    return ssmR3WriteFileHeader(pSSM, cUnits);
}


/**
 * Writes the file header and clear the per-unit data.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pSSM                The SSM handle.
 */
static int ssmR3WriteHeaderAndClearPerUnitData(PVM pVM, PSSMHANDLE pSSM)
{
    /*
     * Write the header.
     */
    int rc = ssmR3WriteFileHeader(pSSM, pVM->ssm.s.cUnits);
    if (RT_SUCCESS(rc))
    {
        /*
         * Clear the per unit flags and offsets.
         */
        for (PSSMUNIT pUnit = pVM->ssm.s.pHead; pUnit; pUnit = pUnit->pNext)
        {
            pUnit->fCalled   = false;
            pUnit->offStream = RTFOFF_MIN;
        }
    }
    return rc;
}


/**
 * Creates a new saved state file.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pszFilename         The name of the file.  NULL if pStreamOps is
 *                              used.
 * @param   pStreamOps          The stream methods.  NULL if pszFilename is
 *                              used.
 * @param   pvStreamOpsUser     The user argument to the stream methods.
 * @param   enmAfter            What to do afterwards.
 * @param   pfnProgress         The progress callback.
 * @param   pvProgressUser      The progress callback user argument.
 * @param   ppSSM               Where to return the pointer to the saved state
 *                              handle upon successful return.  Free it using
 *                              RTMemFree after closing the stream.
 */
static int ssmR3SaveDoCreateFile(PVM pVM, const char *pszFilename, PCSSMSTRMOPS pStreamOps, void *pvStreamOpsUser,
                                 SSMAFTER enmAfter, PFNVMPROGRESS pfnProgress, void *pvProgressUser, PSSMHANDLE *ppSSM)
{
    PSSMHANDLE pSSM = (PSSMHANDLE)RTMemAllocZ(sizeof(*pSSM));
    if (!pSSM)
        return VERR_NO_MEMORY;

    pSSM->pVM                       = pVM;
    pSSM->enmOp                     = SSMSTATE_INVALID;
    pSSM->enmAfter                  = enmAfter;
    pSSM->fCancelled                = SSMHANDLE_OK;
    pSSM->rc                        = VINF_SUCCESS;
    pSSM->cbUnitLeftV1              = 0;
    pSSM->offUnit                   = UINT64_MAX;
    pSSM->offUnitUser               = UINT64_MAX;
    pSSM->fLiveSave                 = false;
    pSSM->pfnProgress               = pfnProgress;
    pSSM->pvUser                    = pvProgressUser;
    pSSM->uPercent                  = 0;
    pSSM->offEstProgress            = 0;
    pSSM->cbEstTotal                = 0;
    pSSM->offEst                    = 0;
    pSSM->offEstUnitEnd             = 0;
    pSSM->uPercentLive              = 0;
    pSSM->uPercentPrepare           = 0;
    pSSM->uPercentDone              = 0;
    pSSM->uReportedLivePercent      = 0;
    pSSM->pszFilename               = pszFilename;
    pSSM->u.Write.offDataBuffer     = 0;
    pSSM->u.Write.cMsMaxDowntime    = UINT32_MAX;

    int rc;
    if (pStreamOps)
        rc = ssmR3StrmInit(&pSSM->Strm, pStreamOps, pvStreamOpsUser, true /*fWrite*/, true /*fChecksummed*/, 8 /*cBuffers*/);
    else
        rc = ssmR3StrmOpenFile(&pSSM->Strm, pszFilename,             true /*fWrite*/, true /*fChecksummed*/, 8 /*cBuffers*/);
    if (RT_FAILURE(rc))
    {
        LogRel(("SSM: Failed to create save state file '%s', rc=%Rrc.\n",  pszFilename, rc));
        RTMemFree(pSSM);
        return rc;
    }

    *ppSSM = pSSM;
    return VINF_SUCCESS;
}


/**
 * Start VM save operation.
 *
 * @returns VBox status code.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pszFilename     Name of the file to save the state in. NULL if pStreamOps is used.
 * @param   pStreamOps      The stream method table. NULL if pszFilename is
 *                          used.
 * @param   pvStreamOpsUser The user argument to the stream methods.
 * @param   enmAfter        What is planned after a successful save operation.
 * @param   pfnProgress     Progress callback. Optional.
 * @param   pvUser          User argument for the progress callback.
 *
 * @thread  EMT
 */
VMMR3DECL(int) SSMR3Save(PVM pVM, const char *pszFilename, PCSSMSTRMOPS pStreamOps, void *pvStreamOpsUser,
                         SSMAFTER enmAfter, PFNVMPROGRESS pfnProgress, void *pvUser)
{
    LogFlow(("SSMR3Save: pszFilename=%p:{%s} enmAfter=%d pfnProgress=%p pvUser=%p\n", pszFilename, pszFilename, enmAfter, pfnProgress, pvUser));
    VM_ASSERT_EMT0(pVM);

    /*
     * Validate input.
     */
    AssertMsgReturn(   enmAfter == SSMAFTER_DESTROY
                    || enmAfter == SSMAFTER_CONTINUE,
                    ("%d\n", enmAfter),
                    VERR_INVALID_PARAMETER);

    AssertReturn(!pszFilename != !pStreamOps, VERR_INVALID_PARAMETER);
    if (pStreamOps)
    {
        AssertReturn(pStreamOps->u32Version == SSMSTRMOPS_VERSION, VERR_INVALID_MAGIC);
        AssertReturn(pStreamOps->u32EndVersion == SSMSTRMOPS_VERSION, VERR_INVALID_MAGIC);
        AssertReturn(pStreamOps->pfnWrite, VERR_INVALID_PARAMETER);
        AssertReturn(pStreamOps->pfnRead, VERR_INVALID_PARAMETER);
        AssertReturn(pStreamOps->pfnSeek, VERR_INVALID_PARAMETER);
        AssertReturn(pStreamOps->pfnTell, VERR_INVALID_PARAMETER);
        AssertReturn(pStreamOps->pfnSize, VERR_INVALID_PARAMETER);
        AssertReturn(pStreamOps->pfnClose, VERR_INVALID_PARAMETER);
    }

    /*
     * Create the saved state file and handle.
     *
     * Note that there might be quite some work to do after executing the saving,
     * so we reserve 20% for the 'Done' period.
     */
    PSSMHANDLE pSSM;
    int rc = ssmR3SaveDoCreateFile(pVM, pszFilename, pStreamOps, pvStreamOpsUser,
                                   enmAfter, pfnProgress, pvUser, &pSSM);
    if (RT_FAILURE(rc))
        return rc;
    pSSM->uPercentLive    = 0;
    pSSM->uPercentPrepare = 20;
    pSSM->uPercentDone    = 2;
    pSSM->fLiveSave       = false;

    /*
     * Write the saved state stream header and join paths with
     * the other save methods for the rest of the job.
     */
    Log(("SSM: Starting state save to file '%s'...\n", pszFilename));
    ssmR3StrmStartIoThread(&pSSM->Strm);
    rc = ssmR3WriteHeaderAndClearPerUnitData(pVM, pSSM);
    if (RT_SUCCESS(rc))
    {
        ssmR3SetCancellable(pVM, pSSM, true);
        ssmR3SaveDoCommon(pVM, pSSM);
    }

    return ssmR3SaveDoClose(pVM, pSSM);
}


/**
 * Used by PGM to report the completion percentage of the live stage during the
 * vote run.
 *
 * @param   pSSM                The saved state handle.
 * @param   uPercent            The completion percentage.
 */
VMMR3DECL(void) SSMR3HandleReportLivePercent(PSSMHANDLE pSSM, unsigned uPercent)
{
    AssertMsgReturnVoid(pSSM->enmOp == SSMSTATE_LIVE_VOTE, ("%d\n", pSSM->enmOp));
    AssertReturnVoid(uPercent <= 100);
    if (uPercent < pSSM->uReportedLivePercent)
        pSSM->uReportedLivePercent = uPercent;
}


/**
 * Calls pfnLiveVote for all units.
 *
 * @returns VBox status code (no need to check pSSM->rc).
 * @retval  VINF_SUCCESS if we can pass on to step 2.
 * @retval  VINF_SSM_VOTE_FOR_ANOTHER_PASS if we need another pass.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   pSSM                The saved state handle.
 * @param   uPass               The current pass.
 */
static int ssmR3LiveDoVoteRun(PVM pVM, PSSMHANDLE pSSM, uint32_t uPass)
{
    int rcRet = VINF_SUCCESS;
    AssertRC(pSSM->rc);
    pSSM->rc = VINF_SUCCESS;
    pSSM->enmOp = SSMSTATE_LIVE_VOTE;

    unsigned uPrevPrecent = pSSM->uReportedLivePercent;
    pSSM->uReportedLivePercent = 101;

    for (PSSMUNIT pUnit = pVM->ssm.s.pHead; pUnit; pUnit = pUnit->pNext)
    {
        if (    pUnit->u.Common.pfnLiveVote
            &&  !pUnit->fDoneLive)
        {
            int rc;
            ssmR3UnitCritSectEnter(pVM, pUnit);
            switch (pUnit->enmType)
            {
                case SSMUNITTYPE_DEV:
                    rc = pUnit->u.Dev.pfnLiveVote(pUnit->u.Dev.pDevIns, pSSM, uPass);
                    break;
                case SSMUNITTYPE_DRV:
                    rc = pUnit->u.Drv.pfnLiveVote(pUnit->u.Drv.pDrvIns, pSSM, uPass);
                    break;
                case SSMUNITTYPE_USB:
                    rc = pUnit->u.Usb.pfnLiveVote(pUnit->u.Usb.pUsbIns, pSSM, uPass);
                    break;
                case SSMUNITTYPE_INTERNAL:
                    rc = pUnit->u.Internal.pfnLiveVote(pVM, pSSM, uPass);
                    break;
                case SSMUNITTYPE_EXTERNAL:
                    rc = pUnit->u.External.pfnLiveVote(pSSM, VMMR3GetVTable(), pUnit->u.External.pvUser, uPass);
                    break;
                default:
                    rc = VERR_SSM_IPE_1;
                    break;
            }
            ssmR3UnitCritSectLeave(pVM, pUnit);
            pUnit->fCalled = true;
            Assert(pSSM->rc == VINF_SUCCESS);
            if (rc != VINF_SUCCESS)
            {
                if (rc == VINF_SSM_VOTE_FOR_ANOTHER_PASS)
                {
                    Log(("ssmR3DoLiveVoteRun: '%s'/#%u -> VINF_SSM_VOTE_FOR_ANOTHER_PASS (pass=%u)\n", pUnit->szName, pUnit->u32Instance, uPass));
                    rcRet = VINF_SSM_VOTE_FOR_ANOTHER_PASS;
                }
                else if (rc == VINF_SSM_VOTE_DONE_DONT_CALL_AGAIN)
                {
                    pUnit->fDoneLive = true;
                    Log(("ssmR3DoLiveVoteRun: '%s'/#%u -> VINF_SSM_VOTE_DONE_DONT_CALL_AGAIN (pass=%u)\n", pUnit->szName, pUnit->u32Instance, uPass));
                }
                else
                {
                    /*
                     * rc is usually VERR_SSM_VOTE_FOR_GIVING_UP here, but we allow
                     * other status codes for better user feed back.  However, no
                     * other non-error status is allowed.
                     */
                    LogRel(("SSM: Error - '%s'/#%u voted %Rrc! (pass=%u)\n", pUnit->szName, pUnit->u32Instance, rc, uPass));
                    AssertMsgReturn(RT_FAILURE(rc), ("%Rrc; '%s'\n", rc, pUnit->szName), pSSM->rc = VERR_IPE_UNEXPECTED_INFO_STATUS);
                    return pSSM->rc = rc;
                }
            }
        }
    }
    if (rcRet == VINF_SUCCESS)
    {
        LogRel(("SSM: Step 1 completed after pass %u.\n", uPass));
        pSSM->uReportedLivePercent = 100;
    }
    else
    {
        /*
         * Work the progress callback.
         */
        if (pSSM->uReportedLivePercent > 100)
            pSSM->uReportedLivePercent = 0;
        if (   pSSM->uReportedLivePercent != uPrevPrecent
            && pSSM->pfnProgress
            && pSSM->uPercentLive)
        {
            long double lrdPct = (long double)pSSM->uReportedLivePercent * pSSM->uPercentLive / 100;
            unsigned    uPct   = (unsigned)lrdPct;
            if (uPct != pSSM->uPercent)
            {
                ssmR3LiveControlEmit(pSSM, lrdPct, uPass);
                pSSM->uPercent = uPct;
                pSSM->pfnProgress(pVM->pUVM, uPct, pSSM->pvUser);
            }
        }
    }
    return rcRet;
}


/**
 * Calls pfnLiveExec for all units.
 *
 * @returns VBox status code (no need to check pSSM->rc).
 *
 * @param   pVM                 The cross context VM structure.
 * @param   pSSM                The saved state handle.
 * @param   uPass               The current pass.
 */
static int ssmR3LiveDoExecRun(PVM pVM, PSSMHANDLE pSSM, uint32_t uPass)
{
    AssertRC(pSSM->rc);
    pSSM->rc = VINF_SUCCESS;
    pSSM->enmOp = SSMSTATE_LIVE_EXEC;
    for (PSSMUNIT pUnit = pVM->ssm.s.pHead; pUnit; pUnit = pUnit->pNext)
    {
        /*
         * Skip units without a callback (this is most).
         */
        if (   !pUnit->u.Common.pfnLiveExec
            || pUnit->fDoneLive)
            continue;
        pUnit->offStream = ssmR3StrmTell(&pSSM->Strm);

        /*
         * Check for cancellation.
         */
        if (RT_UNLIKELY(ASMAtomicUoReadU32(&(pSSM)->fCancelled) == SSMHANDLE_CANCELLED))
        {
            LogRel(("SSM: Cancelled!\n"));
            AssertRC(pSSM->rc);
            return pSSM->rc = VERR_SSM_CANCELLED;
        }

        /*
         * Write data unit header.
         */
        SSMFILEUNITHDRV2 UnitHdr;
        memcpy(&UnitHdr.szMagic[0], SSMFILEUNITHDR_MAGIC, sizeof(UnitHdr.szMagic));
        UnitHdr.offStream       = pUnit->offStream;
        UnitHdr.u32CurStreamCRC = ssmR3StrmCurCRC(&pSSM->Strm);
        UnitHdr.u32CRC          = 0;
        UnitHdr.u32Version      = pUnit->u32Version;
        UnitHdr.u32Instance     = pUnit->u32Instance;
        UnitHdr.u32Pass         = uPass;
        UnitHdr.fFlags          = 0;
        UnitHdr.cbName          = (uint32_t)pUnit->cchName + 1;
        memcpy(&UnitHdr.szName[0], &pUnit->szName[0], UnitHdr.cbName);
        UnitHdr.u32CRC          = RTCrc32(&UnitHdr, RT_UOFFSETOF_DYN(SSMFILEUNITHDRV2, szName[UnitHdr.cbName]));
        Log(("SSM: Unit at %#9llx: '%s', instance %u, pass %#x, version %u\n",
             UnitHdr.offStream, UnitHdr.szName, UnitHdr.u32Instance, UnitHdr.u32Pass, UnitHdr.u32Version));
        int rc = ssmR3StrmWrite(&pSSM->Strm, &UnitHdr, RT_UOFFSETOF_DYN(SSMFILEUNITHDRV2, szName[UnitHdr.cbName]));
        if (RT_FAILURE(rc))
        {
            LogRel(("SSM: Failed to write unit header. rc=%Rrc\n", rc));
            return pSSM->rc = rc;
        }

        /*
         * Call the execute handler.
         */
        ssmR3DataWriteBegin(pSSM);
        ssmR3UnitCritSectEnter(pVM, pUnit);
        switch (pUnit->enmType)
        {
            case SSMUNITTYPE_DEV:
                rc = pUnit->u.Dev.pfnLiveExec(pUnit->u.Dev.pDevIns, pSSM, uPass);
                break;
            case SSMUNITTYPE_DRV:
                rc = pUnit->u.Drv.pfnLiveExec(pUnit->u.Drv.pDrvIns, pSSM, uPass);
                break;
            case SSMUNITTYPE_USB:
                rc = pUnit->u.Usb.pfnLiveExec(pUnit->u.Usb.pUsbIns, pSSM, uPass);
                break;
            case SSMUNITTYPE_INTERNAL:
                rc = pUnit->u.Internal.pfnLiveExec(pVM, pSSM, uPass);
                break;
            case SSMUNITTYPE_EXTERNAL:
                rc = pUnit->u.External.pfnLiveExec(pSSM, VMMR3GetVTable(), pUnit->u.External.pvUser, uPass);
                break;
            default:
                rc = VERR_SSM_IPE_1;
                break;
        }
        ssmR3UnitCritSectLeave(pVM, pUnit);
        pUnit->fCalled = true;
        if (RT_FAILURE(rc) && RT_SUCCESS_NP(pSSM->rc))
            pSSM->rc = rc;
        else
        {
            if (rc == VINF_SSM_DONT_CALL_AGAIN)
                pUnit->fDoneLive = true;
            rc = ssmR3DataFlushBuffer(pSSM); /* will return SSMHANDLE::rc if it is set */
        }
        if (RT_FAILURE(rc))
        {
            LogRel(("SSM: Execute save failed with rc=%Rrc for data unit '%s'/#%u.\n", rc, pUnit->szName, pUnit->u32Instance));
            if (RT_SUCCESS(pSSM->rc))
                pSSM->rc = rc;
            return rc;
        }

        /*
         * Write the termination record and flush the compression stream.
         */
        SSMRECTERM TermRec;
        TermRec.u8TypeAndFlags   = SSM_REC_FLAGS_FIXED | SSM_REC_FLAGS_IMPORTANT | SSM_REC_TYPE_TERM;
        TermRec.cbRec            = sizeof(TermRec) - 2;
        if (pSSM->Strm.fChecksummed)
        {
            TermRec.fFlags       = SSMRECTERM_FLAGS_CRC32;
            TermRec.u32StreamCRC = RTCrc32Finish(RTCrc32Process(ssmR3StrmCurCRC(&pSSM->Strm), &TermRec, 2));
        }
        else
        {
            TermRec.fFlags       = 0;
            TermRec.u32StreamCRC = 0;
        }
        TermRec.cbUnit           = pSSM->offUnit + sizeof(TermRec);
        rc = ssmR3DataWriteRaw(pSSM, &TermRec, sizeof(TermRec));
        if (RT_SUCCESS(rc))
            rc = ssmR3DataWriteFinish(pSSM);
        if (RT_FAILURE(rc))
        {
            LogRel(("SSM: Failed terminating unit: %Rrc (pass=%u)\n", rc, uPass));
            return pSSM->rc = rc;
        }
    } /* for each unit */

    return VINF_SUCCESS;
}


/**
 * Implements the live exec+vote loop.
 *
 * @returns VBox status code (no need to check pSSM->rc).
 * @param   pVM                 The cross context VM structure.
 * @param   pSSM                The saved state handle.
 */
static int ssmR3DoLiveExecVoteLoop(PVM pVM, PSSMHANDLE pSSM)
{
    /*
     * Calc the max saved state size before we should give up because of insane
     * amounts of data.
     */
#define SSM_MAX_GROWTH_FILE      10000
#define SSM_MAX_GROWTH_REMOTE   100000
    uint64_t cbSum = 0;
    for (PSSMUNIT pUnit = pVM->ssm.s.pHead; pUnit; pUnit = pUnit->pNext)
        cbSum += pUnit->cbGuess;
    uint64_t cbMax = cbSum * (pSSM->pszFilename ? SSM_MAX_GROWTH_FILE : SSM_MAX_GROWTH_REMOTE);
    AssertLogRelMsgReturn(cbMax > cbSum, ("cbMax=%#RX64, cbSum=%#RX64\n", cbMax, cbSum), pSSM->rc = VERR_OUT_OF_RANGE);
    if (cbMax < _1G)
        cbMax = _1G;

    /*
     * The pass loop.
     *
     * The number of iterations is restricted for two reasons, first
     * to make sure
     */
#define SSM_MAX_PASSES  _1M
    for (uint32_t uPass = 0; uPass < SSM_MAX_PASSES; uPass++)
    {
        pVM->ssm.s.uPass = uPass;

        /*
         * Save state and vote on whether we need more passes or not.
         */
        int rc = ssmR3LiveDoExecRun(pVM, pSSM, uPass);
        if (RT_FAILURE(rc))
            return rc;
        rc = ssmR3LiveDoVoteRun(pVM, pSSM, uPass);
        if (rc == VINF_SUCCESS)
        {
            pSSM->enmOp = SSMSTATE_LIVE_STEP2;
            return VINF_SUCCESS;
        }
        if (RT_FAILURE(rc))
            return rc;

        /*
         * Check that we're still within sane data amounts.
         */
        uint64_t cbSaved = ssmR3StrmTell(&pSSM->Strm);
        if (cbSaved > cbMax)
        {
            LogRel(("SSM: Giving up: Exceeded max state size. (cbSaved=%#RX64, cbMax=%#RX64)\n", cbSaved, cbMax));
            return pSSM->rc = VERR_SSM_STATE_GREW_TOO_BIG;
        }

        /*
         * Check that the stream is still OK.
         */
        rc = ssmR3StrmCheckAndFlush(&pSSM->Strm);
        if (RT_FAILURE(rc))
            return pSSM->rc = rc;
    }

    LogRel(("SSM: Giving up: Too many passes! (%u)\n", SSM_MAX_PASSES));
    return pSSM->rc = VERR_SSM_TOO_MANY_PASSES;
}


/**
 * Calls pfnLivePrep for all units.
 *
 * @returns VBox status code (no need to check pSSM->rc).
 * @param   pVM                 The cross context VM structure.
 * @param   pSSM                The saved state handle.
 */
static int ssmR3DoLivePrepRun(PVM pVM, PSSMHANDLE pSSM)
{
    /*
     * Do the prepare run.
     */
    pSSM->rc = VINF_SUCCESS;
    pSSM->enmOp = SSMSTATE_SAVE_PREP;
    for (PSSMUNIT pUnit = pVM->ssm.s.pHead; pUnit; pUnit = pUnit->pNext)
    {
        if (pUnit->u.Common.pfnLivePrep)
        {
            int rc;
            ssmR3UnitCritSectEnter(pVM, pUnit);
            switch (pUnit->enmType)
            {
                case SSMUNITTYPE_DEV:
                    rc = pUnit->u.Dev.pfnLivePrep(pUnit->u.Dev.pDevIns, pSSM);
                    break;
                case SSMUNITTYPE_DRV:
                    rc = pUnit->u.Drv.pfnLivePrep(pUnit->u.Drv.pDrvIns, pSSM);
                    break;
                case SSMUNITTYPE_USB:
                    rc = pUnit->u.Usb.pfnLivePrep(pUnit->u.Usb.pUsbIns, pSSM);
                    break;
                case SSMUNITTYPE_INTERNAL:
                    rc = pUnit->u.Internal.pfnLivePrep(pVM, pSSM);
                    break;
                case SSMUNITTYPE_EXTERNAL:
                    rc = pUnit->u.External.pfnLivePrep(pSSM, VMMR3GetVTable(), pUnit->u.External.pvUser);
                    break;
                default:
                    rc = VERR_SSM_IPE_1;
                    break;
            }
            ssmR3UnitCritSectLeave(pVM, pUnit);
            pUnit->fCalled = true;
            if (RT_FAILURE(rc) && RT_SUCCESS_NP(pSSM->rc))
                pSSM->rc = rc;
            else
                rc = pSSM->rc;
            if (RT_FAILURE(rc))
            {
                LogRel(("SSM: Prepare save failed with rc=%Rrc for data unit '%s.\n", rc, pUnit->szName));
                return rc;
            }
        }

        pSSM->cbEstTotal += pUnit->cbGuess;
    }

    /*
     * Work the progress indicator if we got one.
     */
    if (pSSM->pfnProgress)
        pSSM->pfnProgress(pVM->pUVM, 2, pSSM->pvUser);
    pSSM->uPercent = 2;

    return VINF_SUCCESS;
}


/**
 * Continue a live state saving operation on the worker thread.
 *
 * @returns VBox status code.
 *
 * @param   pSSM            The SSM handle returned by SSMR3LiveSave.
 *
 * @thread  Non-EMT thread. Will involve the EMT at the end of the operation.
 */
VMMR3_INT_DECL(int) SSMR3LiveDoStep1(PSSMHANDLE pSSM)
{
    LogFlow(("SSMR3LiveDoStep1: pSSM=%p\n", pSSM));

    /*
     * Validate input.
     */
    AssertPtrReturn(pSSM, VERR_INVALID_POINTER);
    PVM pVM = pSSM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_OTHER_THREAD(pVM);
    AssertMsgReturn(   pSSM->enmAfter == SSMAFTER_DESTROY
                    || pSSM->enmAfter == SSMAFTER_CONTINUE
                    || pSSM->enmAfter == SSMAFTER_TELEPORT,
                    ("%d\n", pSSM->enmAfter),
                    VERR_INVALID_PARAMETER);
    AssertMsgReturn(pSSM->enmOp == SSMSTATE_LIVE_STEP1, ("%d\n", pSSM->enmOp), VERR_INVALID_STATE);
    AssertRCReturn(pSSM->rc, pSSM->rc);

    /*
     * Do the prep run, then the exec+vote cycle.
     */
    int rc = ssmR3DoLivePrepRun(pVM, pSSM);
    if (RT_SUCCESS(rc))
        rc = ssmR3DoLiveExecVoteLoop(pVM, pSSM);
    return rc;
}


/**
 * Start saving the live state.
 *
 * Call SSMR3LiveDoStep1, SSMR3LiveDoStep2 and finally SSMR3LiveDone on success.
 * SSMR3LiveDone should be called even if SSMR3LiveDoStep1 or SSMR3LiveDoStep2
 * fails.
 *
 * @returns VBox status code.
 *
 * @param   pVM             The cross context VM structure.
 * @param   cMsMaxDowntime  The maximum downtime given as milliseconds.
 * @param   pszFilename     Name of the file to save the state in. This string
 *                          must remain valid until SSMR3LiveDone is called.
 *                          Must be NULL if pStreamOps is used.
 * @param   pStreamOps      The stream method table. NULL if pszFilename is
 *                          used.
 * @param   pvStreamOpsUser The user argument to the stream methods.
 * @param   enmAfter        What is planned after a successful save operation.
 * @param   pfnProgress     Progress callback. Optional.
 * @param   pvProgressUser  User argument for the progress callback.
 * @param   ppSSM           Where to return the saved state handle on success.
 *
 * @thread  EMT0
 */
VMMR3_INT_DECL(int) SSMR3LiveSave(PVM pVM, uint32_t cMsMaxDowntime,
                                  const char *pszFilename, PCSSMSTRMOPS pStreamOps, void *pvStreamOpsUser,
                                  SSMAFTER enmAfter, PFNVMPROGRESS pfnProgress, void *pvProgressUser,
                                  PSSMHANDLE *ppSSM)
{
    LogFlow(("SSMR3LiveSave: cMsMaxDowntime=%u pszFilename=%p:{%s} pStreamOps=%p pvStreamOpsUser=%p enmAfter=%d pfnProgress=%p pvProgressUser=%p\n",
             cMsMaxDowntime, pszFilename, pszFilename, pStreamOps, pvStreamOpsUser, enmAfter, pfnProgress, pvProgressUser));
    VM_ASSERT_EMT0(pVM);

    /*
     * Validate input.
     */
    AssertMsgReturn(   enmAfter == SSMAFTER_DESTROY
                    || enmAfter == SSMAFTER_CONTINUE
                    || enmAfter == SSMAFTER_TELEPORT,
                    ("%d\n", enmAfter),
                    VERR_INVALID_PARAMETER);
    AssertReturn(!pszFilename != !pStreamOps, VERR_INVALID_PARAMETER);
    if (pStreamOps)
    {
        AssertReturn(pStreamOps->u32Version == SSMSTRMOPS_VERSION, VERR_INVALID_MAGIC);
        AssertReturn(pStreamOps->u32EndVersion == SSMSTRMOPS_VERSION, VERR_INVALID_MAGIC);
        AssertReturn(pStreamOps->pfnWrite, VERR_INVALID_PARAMETER);
        AssertReturn(pStreamOps->pfnRead, VERR_INVALID_PARAMETER);
        AssertReturn(pStreamOps->pfnSeek, VERR_INVALID_PARAMETER);
        AssertReturn(pStreamOps->pfnTell, VERR_INVALID_PARAMETER);
        AssertReturn(pStreamOps->pfnSize, VERR_INVALID_PARAMETER);
        AssertReturn(pStreamOps->pfnClose, VERR_INVALID_PARAMETER);
    }

    /*
     * Create the saved state file and handle.
     *
     * Note that there might be quite some work to do after executing the saving,
     * so we reserve 20% for the 'Done' period.
     */
    PSSMHANDLE pSSM;
    int rc = ssmR3SaveDoCreateFile(pVM, pszFilename, pStreamOps, pvStreamOpsUser,
                                   enmAfter, pfnProgress, pvProgressUser, &pSSM);
    if (RT_FAILURE(rc))
        return rc;
    pSSM->uPercentLive           = 93;
    pSSM->uPercentPrepare        = 2;
    pSSM->uPercentDone           = 2;
    pSSM->fLiveSave              = true;
    pSSM->u.Write.cMsMaxDowntime = cMsMaxDowntime;

    /*
     * Write the saved state stream header and do the prep run for live saving.
     */
    Log(("SSM: Starting state save to file '%s'...\n", pszFilename));
    ssmR3StrmStartIoThread(&pSSM->Strm);
    rc = ssmR3WriteHeaderAndClearPerUnitData(pVM, pSSM);
    if (RT_SUCCESS(rc))
    {
        /*
         * Return and let the requestor thread do the pfnLiveExec/Vote part
         * via SSMR3SaveFinishLive
         */
        pSSM->enmOp = SSMSTATE_LIVE_STEP1;
        ssmR3SetCancellable(pVM, pSSM, true);
        *ppSSM = pSSM;
        return VINF_SUCCESS;
    }
    /* bail out. */
    int rc2 = ssmR3StrmClose(&pSSM->Strm, pSSM->rc == VERR_SSM_CANCELLED);
    RTMemFree(pSSM);
    rc2 = RTFileDelete(pszFilename);
    AssertRC(rc2);
    return rc;
}

#endif /* !SSM_STANDALONE */


/* ... Loading and reading starts here ... */
/* ... Loading and reading starts here ... */
/* ... Loading and reading starts here ... */
/* ... Loading and reading starts here ... */
/* ... Loading and reading starts here ... */
/* ... Loading and reading starts here ... */
/* ... Loading and reading starts here ... */
/* ... Loading and reading starts here ... */
/* ... Loading and reading starts here ... */
/* ... Loading and reading starts here ... */
/* ... Loading and reading starts here ... */
/* ... Loading and reading starts here ... */
/* ... Loading and reading starts here ... */
/* ... Loading and reading starts here ... */
/* ... Loading and reading starts here ... */
/* ... Loading and reading starts here ... */
/* ... Loading and reading starts here ... */


#ifndef SSM_STANDALONE
/**
 * Closes the decompressor of a data unit.
 *
 * @returns pSSM->rc.
 * @param   pSSM            The saved state handle.
 */
static int ssmR3DataReadFinishV1(PSSMHANDLE pSSM)
{
    if (pSSM->u.Read.pZipDecompV1)
    {
        int rc = RTZipDecompDestroy(pSSM->u.Read.pZipDecompV1);
        AssertRC(rc);
        pSSM->u.Read.pZipDecompV1 = NULL;
    }
    return pSSM->rc;
}
#endif /* !SSM_STANDALONE */


/**
 * Callback for reading compressed data into the input buffer of the
 * decompressor, for saved file format version 1.
 *
 * @returns VBox status code. Set pSSM->rc on error.
 * @param   pvSSM       The SSM handle.
 * @param   pvBuf       Where to store the compressed data.
 * @param   cbBuf       Size of the buffer.
 * @param   pcbRead     Number of bytes actually stored in the buffer.
 */
static DECLCALLBACK(int) ssmR3ReadInV1(void *pvSSM, void *pvBuf, size_t cbBuf, size_t *pcbRead)
{
    PSSMHANDLE  pSSM   = (PSSMHANDLE)pvSSM;
    size_t      cbRead = cbBuf;
    if (pSSM->cbUnitLeftV1 < cbBuf)
        cbRead = (size_t)pSSM->cbUnitLeftV1;
    if (cbRead)
    {
        //Log2(("ssmR3ReadInV1: %#010llx cbBug=%#x cbRead=%#x\n", ssmR3StrmTell(&pSSM->Strm), cbBuf, cbRead));
        int rc = ssmR3StrmRead(&pSSM->Strm, pvBuf, cbRead);
        if (RT_SUCCESS(rc))
        {
            pSSM->cbUnitLeftV1 -= cbRead;
            if (pcbRead)
                *pcbRead = cbRead;
            ssmR3ProgressByByte(pSSM, cbRead);
            return VINF_SUCCESS;
        }
        return pSSM->rc = rc;
    }

    if (pSSM->enmAfter != SSMAFTER_DEBUG_IT)
        AssertMsgFailed(("SSM: attempted reading more than the unit!\n"));
    return pSSM->rc = VERR_SSM_LOADED_TOO_MUCH;
}


/**
 * Internal read worker for reading data from a version 1 unit.
 *
 * @returns VBox status code, pSSM->rc is set on error.
 *
 * @param   pSSM            The saved state handle.
 * @param   pvBuf           Where to store the read data.
 * @param   cbBuf           Number of bytes to read.
 */
static int ssmR3DataReadV1(PSSMHANDLE pSSM, void *pvBuf, size_t cbBuf)
{
    /*
     * Open the decompressor on the first read.
     */
    if (!pSSM->u.Read.pZipDecompV1)
    {
        pSSM->rc = RTZipDecompCreate(&pSSM->u.Read.pZipDecompV1, pSSM, ssmR3ReadInV1);
        if (RT_FAILURE(pSSM->rc))
            return pSSM->rc;
    }

    /*
     * Do the requested read.
     */
    int rc = pSSM->rc = RTZipDecompress(pSSM->u.Read.pZipDecompV1, pvBuf, cbBuf, NULL);
    if (RT_SUCCESS(rc))
    {
        Log2(("ssmR3DataRead: pvBuf=%p cbBuf=%#x offUnit=%#llx %.*Rhxs%s\n", pvBuf, cbBuf, pSSM->offUnit, RT_MIN(cbBuf, SSM_LOG_BYTES), pvBuf, cbBuf > SSM_LOG_BYTES ? "..." : ""));
        pSSM->offUnit     += cbBuf;
        pSSM->offUnitUser += cbBuf;
        return VINF_SUCCESS;
    }
    AssertMsgFailed(("rc=%Rrc cbBuf=%#x\n", rc, cbBuf));
    return rc;
}


/**
 * Creates the decompressor for the data unit.
 *
 * pSSM->rc will be set on error.
 *
 * @param   pSSM            The saved state handle.
 */
static void ssmR3DataReadBeginV2(PSSMHANDLE pSSM)
{
    Assert(!pSSM->u.Read.cbDataBuffer || pSSM->u.Read.cbDataBuffer == pSSM->u.Read.offDataBuffer);
    Assert(!pSSM->u.Read.cbRecLeft);

    pSSM->offUnit     = 0;
    pSSM->offUnitUser = 0;
    pSSM->u.Read.cbRecLeft      = 0;
    pSSM->u.Read.cbDataBuffer   = 0;
    pSSM->u.Read.offDataBuffer  = 0;
    pSSM->u.Read.fEndOfData     = false;
    pSSM->u.Read.u8TypeAndFlags = 0;
}


#ifndef SSM_STANDALONE
/**
 * Checks for the termination record and closes the decompressor.
 *
 * pSSM->rc will be set on error.
 *
 * @returns pSSM->rc.
 * @param   pSSM            The saved state handle.
 */
static int ssmR3DataReadFinishV2(PSSMHANDLE pSSM)
{
    /*
     * If we haven't encountered the end of the record, it must be the next one.
     */
    int rc = pSSM->rc;
    if (    !pSSM->u.Read.fEndOfData
        &&  RT_SUCCESS(rc))
    {
        if (   pSSM->u.Read.cbDataBuffer != pSSM->u.Read.offDataBuffer
            && pSSM->u.Read.cbDataBuffer > 0)
        {
            LogRel(("SSM: At least %#x bytes left to read\n", pSSM->u.Read.cbDataBuffer - pSSM->u.Read.offDataBuffer));
            rc = VERR_SSM_LOADED_TOO_LITTLE;
        }
        else
        {
            rc = ssmR3DataReadRecHdrV2(pSSM);
            if (    RT_SUCCESS(rc)
                &&  !pSSM->u.Read.fEndOfData)
            {
                LogRel(("SSM: At least %#x bytes left to read\n", pSSM->u.Read.cbDataBuffer));
                rc = VERR_SSM_LOADED_TOO_LITTLE;
                AssertFailed();
            }
        }
        pSSM->rc = rc;
    }
    return rc;
}
#endif /* !SSM_STANDALONE */


/**
 * Read raw record bytes, work the progress indicator and unit offset.
 *
 * @returns VBox status code. Does NOT set pSSM->rc.
 * @param   pSSM            The saved state handle.
 * @param   pvBuf           Where to put the bits
 * @param   cbToRead        How many bytes to read.
 */
DECLINLINE(int) ssmR3DataReadV2Raw(PSSMHANDLE pSSM, void *pvBuf, size_t cbToRead)
{
    int rc = ssmR3StrmRead(&pSSM->Strm, pvBuf, cbToRead);
    if (RT_SUCCESS(rc))
    {
        pSSM->offUnit += cbToRead;
        ssmR3ProgressByByte(pSSM, cbToRead);
        return VINF_SUCCESS;
    }

    if (rc == VERR_SSM_CANCELLED)
        return rc;

    if (pSSM->enmAfter != SSMAFTER_DEBUG_IT && rc == VERR_EOF)
        AssertMsgFailedReturn(("SSM: attempted reading more than the unit! rc=%Rrc\n", rc), VERR_SSM_LOADED_TOO_MUCH);
    return VERR_SSM_STREAM_ERROR;
}


/**
 * Reads and checks the LZF "header".
 *
 * @returns VBox status code. Sets pSSM->rc on error.
 * @param   pSSM            The saved state handle..
 * @param   pcbDecompr      Where to store the size of the decompressed data.
 */
DECLINLINE(int) ssmR3DataReadV2RawLzfHdr(PSSMHANDLE pSSM, uint32_t *pcbDecompr)
{
    *pcbDecompr = 0; /* shuts up gcc. */
    AssertLogRelMsgReturn(   pSSM->u.Read.cbRecLeft > 1
                          && pSSM->u.Read.cbRecLeft <= RT_SIZEOFMEMB(SSMHANDLE, u.Read.abComprBuffer) + 2,
                          ("%#x\n", pSSM->u.Read.cbRecLeft),
                          pSSM->rc = VERR_SSM_INTEGRITY_DECOMPRESSION);

    uint8_t cKB;
    int rc = ssmR3DataReadV2Raw(pSSM, &cKB, 1);
    if (RT_FAILURE(rc))
        return pSSM->rc = rc;
    pSSM->u.Read.cbRecLeft -= sizeof(cKB);

    uint32_t cbDecompr = (uint32_t)cKB * _1K;
    AssertLogRelMsgReturn(   cbDecompr >= pSSM->u.Read.cbRecLeft
                          && cbDecompr <= RT_SIZEOFMEMB(SSMHANDLE, u.Read.abDataBuffer),
                          ("%#x\n", cbDecompr),
                          pSSM->rc = VERR_SSM_INTEGRITY_DECOMPRESSION);

    *pcbDecompr = cbDecompr;
    return VINF_SUCCESS;
}


/**
 * Reads an LZF block from the stream and decompresses into the specified
 * buffer.
 *
 * @returns VBox status code. Sets pSSM->rc on error.
 * @param   pSSM            The saved state handle.
 * @param   pvDst           Pointer to the output buffer.
 * @param   cbDecompr       The size of the decompressed data.
 */
static int ssmR3DataReadV2RawLzf(PSSMHANDLE pSSM, void *pvDst, size_t cbDecompr)
{
    int         rc;
    uint32_t    cbCompr    = pSSM->u.Read.cbRecLeft;
    pSSM->u.Read.cbRecLeft = 0;

    /*
     * Try use the stream buffer directly to avoid copying things around.
     */
    uint8_t const *pb = ssmR3StrmReadDirect(&pSSM->Strm, cbCompr);
    if (pb)
    {
        pSSM->offUnit += cbCompr;
        ssmR3ProgressByByte(pSSM, cbCompr);
    }
    else
    {
        rc = ssmR3DataReadV2Raw(pSSM, &pSSM->u.Read.abComprBuffer[0], cbCompr);
        if (RT_FAILURE(rc))
            return pSSM->rc = rc;
        pb = &pSSM->u.Read.abComprBuffer[0];
    }

    /*
     * Decompress it.
     */
    size_t cbDstActual;
    rc = RTZipBlockDecompress(RTZIPTYPE_LZF, 0 /*fFlags*/,
                              pb, cbCompr, NULL /*pcbSrcActual*/,
                              pvDst, cbDecompr, &cbDstActual);
    if (RT_SUCCESS(rc))
    {
        AssertLogRelMsgReturn(cbDstActual == cbDecompr, ("%#x %#x\n", cbDstActual, cbDecompr), pSSM->rc = VERR_SSM_INTEGRITY_DECOMPRESSION);
        return VINF_SUCCESS;
    }

    AssertLogRelMsgFailed(("cbCompr=%#x cbDecompr=%#x rc=%Rrc\n", cbCompr, cbDecompr, rc));
    return pSSM->rc = VERR_SSM_INTEGRITY_DECOMPRESSION;
}


/**
 * Reads and checks the raw zero "header".
 *
 * @returns VBox status code. Sets pSSM->rc on error.
 * @param   pSSM            The saved state handle..
 * @param   pcbZero         Where to store the size of the zero data.
 */
DECLINLINE(int) ssmR3DataReadV2RawZeroHdr(PSSMHANDLE pSSM, uint32_t *pcbZero)
{
    *pcbZero = 0; /* shuts up gcc. */
    AssertLogRelMsgReturn(pSSM->u.Read.cbRecLeft == 1, ("%#x\n", pSSM->u.Read.cbRecLeft), pSSM->rc = VERR_SSM_INTEGRITY_DECOMPRESSION);

    uint8_t cKB;
    int rc = ssmR3DataReadV2Raw(pSSM, &cKB, 1);
    if (RT_FAILURE(rc))
        return pSSM->rc = rc;
    pSSM->u.Read.cbRecLeft = 0;

    uint32_t cbZero = (uint32_t)cKB * _1K;
    AssertLogRelMsgReturn(cbZero <= RT_SIZEOFMEMB(SSMHANDLE, u.Read.abDataBuffer),
                          ("%#x\n", cbZero), pSSM->rc = VERR_SSM_INTEGRITY_DECOMPRESSION);

    *pcbZero = cbZero;
    return VINF_SUCCESS;
}


/**
 * Worker for reading the record header.
 *
 * It sets pSSM->u.Read.cbRecLeft, pSSM->u.Read.u8TypeAndFlags and
 * pSSM->u.Read.fEndOfData.  When a termination record is encounter, it will be
 * read in full and validated, the fEndOfData indicator is set, and VINF_SUCCESS
 * is returned.
 *
 * @returns VBox status code. Does not set pSSM->rc.
 * @param   pSSM            The saved state handle.
 */
static int ssmR3DataReadRecHdrV2(PSSMHANDLE pSSM)
{
    AssertLogRelReturn(!pSSM->u.Read.fEndOfData, VERR_SSM_LOADED_TOO_MUCH);

    /*
     * Read the two mandatory bytes.
     */
    uint8_t  abHdr[8];
    int rc = ssmR3DataReadV2Raw(pSSM, abHdr, 2);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Validate the first byte and check for the termination records.
     */
    pSSM->u.Read.u8TypeAndFlags = abHdr[0];
    AssertLogRelMsgReturn(SSM_REC_ARE_TYPE_AND_FLAGS_VALID(abHdr[0]), ("%#x %#x\n", abHdr[0], abHdr[1]), VERR_SSM_INTEGRITY_REC_HDR);
    if ((abHdr[0] & SSM_REC_TYPE_MASK) == SSM_REC_TYPE_TERM)
    {
        pSSM->u.Read.cbRecLeft = 0;
        pSSM->u.Read.fEndOfData = true;
        AssertLogRelMsgReturn(abHdr[1] == sizeof(SSMRECTERM) - 2, ("%#x\n", abHdr[1]), VERR_SSM_INTEGRITY_REC_TERM);
        AssertLogRelMsgReturn(abHdr[0] & SSM_REC_FLAGS_IMPORTANT, ("%#x\n", abHdr[0]), VERR_SSM_INTEGRITY_REC_TERM);

        /* get the rest */
        uint32_t    u32StreamCRC = ssmR3StrmFinalCRC(&pSSM->Strm);
        SSMRECTERM  TermRec;
        rc = ssmR3DataReadV2Raw(pSSM, (uint8_t *)&TermRec + 2, sizeof(SSMRECTERM) - 2);
        if (RT_FAILURE(rc))
            return rc;

        /* validate integrity */
        AssertLogRelMsgReturn(TermRec.cbUnit == pSSM->offUnit,
                              ("cbUnit=%#llx offUnit=%#llx\n", TermRec.cbUnit, pSSM->offUnit),
                              VERR_SSM_INTEGRITY_REC_TERM);
        AssertLogRelMsgReturn(!(TermRec.fFlags & ~SSMRECTERM_FLAGS_CRC32), ("%#x\n", TermRec.fFlags), VERR_SSM_INTEGRITY_REC_TERM);
        if (!(TermRec.fFlags & SSMRECTERM_FLAGS_CRC32))
            AssertLogRelMsgReturn(TermRec.u32StreamCRC == 0, ("%#x\n", TermRec.u32StreamCRC), VERR_SSM_INTEGRITY_REC_TERM);
        else if (pSSM->Strm.fChecksummed)
            AssertLogRelMsgReturn(TermRec.u32StreamCRC == u32StreamCRC, ("%#x, %#x\n", TermRec.u32StreamCRC, u32StreamCRC),
                                  VERR_SSM_INTEGRITY_REC_TERM_CRC);

        Log3(("ssmR3DataReadRecHdrV2: %08llx|%08llx: TERM\n", ssmR3StrmTell(&pSSM->Strm) - sizeof(SSMRECTERM), pSSM->offUnit));
        return VINF_SUCCESS;
    }

    /*
     * Figure the size. The 2nd byte is encoded in UTF-8 fashion, so this
     * is can be highly enjoyable.
     */
    uint32_t cbHdr = 2;
    uint32_t cb = abHdr[1];
    if (!(cb & 0x80))
        pSSM->u.Read.cbRecLeft = cb;
    else
    {
        /*
         * Need more data. Figure how much and read it.
         */
        if (!(cb & RT_BIT(5)))
            cb = 2;
        else if (!(cb & RT_BIT(4)))
            cb = 3;
        else if (!(cb & RT_BIT(3)))
            cb = 4;
        else if (!(cb & RT_BIT(2)))
            cb = 5;
        else if (!(cb & RT_BIT(1)))
            cb = 6;
        else
            AssertLogRelMsgFailedReturn(("Invalid record size byte: %#x\n", cb), VERR_SSM_INTEGRITY_REC_HDR);
        cbHdr = cb + 1;

        rc = ssmR3DataReadV2Raw(pSSM, &abHdr[2], cb - 1);
        if (RT_FAILURE(rc))
            return rc;

        /*
         * Validate what we've read.
         */
        switch (cb)
        {
            case 6:
                AssertLogRelMsgReturn((abHdr[6] & 0xc0) == 0x80, ("6/%u: %.*Rhxs\n", cb, cb + 1, &abHdr[0]), VERR_SSM_INTEGRITY_REC_HDR);
                RT_FALL_THRU();
            case 5:
                AssertLogRelMsgReturn((abHdr[5] & 0xc0) == 0x80, ("5/%u: %.*Rhxs\n", cb, cb + 1, &abHdr[0]), VERR_SSM_INTEGRITY_REC_HDR);
                RT_FALL_THRU();
            case 4:
                AssertLogRelMsgReturn((abHdr[4] & 0xc0) == 0x80, ("4/%u: %.*Rhxs\n", cb, cb + 1, &abHdr[0]), VERR_SSM_INTEGRITY_REC_HDR);
                RT_FALL_THRU();
            case 3:
                AssertLogRelMsgReturn((abHdr[3] & 0xc0) == 0x80, ("3/%u: %.*Rhxs\n", cb, cb + 1, &abHdr[0]), VERR_SSM_INTEGRITY_REC_HDR);
                RT_FALL_THRU();
            case 2:
                AssertLogRelMsgReturn((abHdr[2] & 0xc0) == 0x80, ("2/%u: %.*Rhxs\n", cb, cb + 1, &abHdr[0]), VERR_SSM_INTEGRITY_REC_HDR);
                break;
            default:
                return VERR_IPE_NOT_REACHED_DEFAULT_CASE;
        }

        /*
         * Decode it and validate the range.
         */
        switch (cb)
        {
            case 6:
                cb =             (abHdr[6] & 0x3f)
                    | ((uint32_t)(abHdr[5] & 0x3f) << 6)
                    | ((uint32_t)(abHdr[4] & 0x3f) << 12)
                    | ((uint32_t)(abHdr[3] & 0x3f) << 18)
                    | ((uint32_t)(abHdr[2] & 0x3f) << 24)
                    | ((uint32_t)(abHdr[1] & 0x01) << 30);
                AssertLogRelMsgReturn(cb >= 0x04000000 && cb <= 0x7fffffff, ("cb=%#x\n", cb), VERR_SSM_INTEGRITY_REC_HDR);
                break;
            case 5:
                cb =             (abHdr[5] & 0x3f)
                    | ((uint32_t)(abHdr[4] & 0x3f) << 6)
                    | ((uint32_t)(abHdr[3] & 0x3f) << 12)
                    | ((uint32_t)(abHdr[2] & 0x3f) << 18)
                    | ((uint32_t)(abHdr[1] & 0x03) << 24);
                AssertLogRelMsgReturn(cb >= 0x00200000 && cb <= 0x03ffffff, ("cb=%#x\n", cb), VERR_SSM_INTEGRITY_REC_HDR);
                break;
            case 4:
                cb =             (abHdr[4] & 0x3f)
                    | ((uint32_t)(abHdr[3] & 0x3f) << 6)
                    | ((uint32_t)(abHdr[2] & 0x3f) << 12)
                    | ((uint32_t)(abHdr[1] & 0x07) << 18);
                AssertLogRelMsgReturn(cb >= 0x00010000 && cb <= 0x001fffff, ("cb=%#x\n", cb), VERR_SSM_INTEGRITY_REC_HDR);
                break;
            case 3:
                cb =             (abHdr[3] & 0x3f)
                    | ((uint32_t)(abHdr[2] & 0x3f) << 6)
                    | ((uint32_t)(abHdr[1] & 0x0f) << 12);
#if 0  /* disabled to optimize buffering */
                AssertLogRelMsgReturn(cb >= 0x00000800 && cb <= 0x0000ffff, ("cb=%#x\n", cb), VERR_SSM_INTEGRITY_REC_HDR);
#endif
                break;
            case 2:
                cb =             (abHdr[2] & 0x3f)
                    | ((uint32_t)(abHdr[1] & 0x1f) << 6);
#if 0  /* disabled to optimize buffering */
                AssertLogRelMsgReturn(cb >= 0x00000080 && cb <= 0x000007ff, ("cb=%#x\n", cb), VERR_SSM_INTEGRITY_REC_HDR);
#endif
                break;
            default:
                return VERR_IPE_NOT_REACHED_DEFAULT_CASE;
        }

        pSSM->u.Read.cbRecLeft = cb;
    }

    Log3(("ssmR3DataReadRecHdrV2: %08llx|%08llx/%08x: Type=%02x fImportant=%RTbool cbHdr=%u\n",
          ssmR3StrmTell(&pSSM->Strm), pSSM->offUnit, pSSM->u.Read.cbRecLeft,
          pSSM->u.Read.u8TypeAndFlags & SSM_REC_TYPE_MASK,
          !!(pSSM->u.Read.u8TypeAndFlags & SSM_REC_FLAGS_IMPORTANT),
          cbHdr
        )); NOREF(cbHdr);
    return VINF_SUCCESS;
}


/**
 * Buffer miss, do an unbuffered read.
 *
 * @returns VBox status code. Sets pSSM->rc on error.
 * @param   pSSM            The saved state handle.
 * @param   pvBuf           Where to store the read data.
 * @param   cbBuf           Number of bytes to read.
 */
static int ssmR3DataReadUnbufferedV2(PSSMHANDLE pSSM, void *pvBuf, size_t cbBuf)
{
    void   const *pvBufOrg = pvBuf; NOREF(pvBufOrg);
    size_t const  cbBufOrg = cbBuf; NOREF(cbBufOrg);

    /*
     * Copy out what we've got in the buffer.
     */
    uint32_t off        = pSSM->u.Read.offDataBuffer;
    int32_t  cbInBuffer = pSSM->u.Read.cbDataBuffer - off;
    Log4(("ssmR3DataReadUnbufferedV2: %08llx|%08llx/%08x/%08x: cbBuf=%#x\n", ssmR3StrmTell(&pSSM->Strm), pSSM->offUnit, pSSM->u.Read.cbRecLeft, cbInBuffer, cbBufOrg));
    if (cbInBuffer > 0)
    {
        uint32_t const cbToCopy = (uint32_t)cbInBuffer;
        Assert(cbBuf > cbToCopy);
        memcpy(pvBuf, &pSSM->u.Read.abDataBuffer[off], cbToCopy);
        pvBuf  = (uint8_t *)pvBuf + cbToCopy;
        cbBuf -= cbToCopy;
        pSSM->u.Read.cbDataBuffer  = 0;
        pSSM->u.Read.offDataBuffer = 0;
    }

    /*
     * Read data.
     */
    do
    {
        /*
         * Read the next record header if no more data.
         */
        if (!pSSM->u.Read.cbRecLeft)
        {
            int rc = ssmR3DataReadRecHdrV2(pSSM);
            if (RT_FAILURE(rc))
                return pSSM->rc = rc;
        }
        AssertLogRelMsgReturn(!pSSM->u.Read.fEndOfData, ("cbBuf=%zu\n", cbBuf), pSSM->rc = VERR_SSM_LOADED_TOO_MUCH);

        /*
         * Read data from the current record.
         */
        uint32_t cbToRead;
        switch (pSSM->u.Read.u8TypeAndFlags & SSM_REC_TYPE_MASK)
        {
            case SSM_REC_TYPE_RAW:
            {
                cbToRead = (uint32_t)RT_MIN(cbBuf, pSSM->u.Read.cbRecLeft);
                int rc = ssmR3DataReadV2Raw(pSSM, pvBuf, cbToRead);
                if (RT_FAILURE(rc))
                    return pSSM->rc = rc;
                pSSM->u.Read.cbRecLeft -= cbToRead;
                break;
            }

            case SSM_REC_TYPE_RAW_LZF:
            {
                int rc = ssmR3DataReadV2RawLzfHdr(pSSM, &cbToRead);
                if (RT_FAILURE(rc))
                    return rc;
                if (cbToRead <= cbBuf)
                {
                    rc = ssmR3DataReadV2RawLzf(pSSM, pvBuf, cbToRead);
                    if (RT_FAILURE(rc))
                        return rc;
                }
                else
                {
                    /* The output buffer is too small, use the data buffer. */
                    rc = ssmR3DataReadV2RawLzf(pSSM, &pSSM->u.Read.abDataBuffer[0], cbToRead);
                    if (RT_FAILURE(rc))
                        return rc;
                    pSSM->u.Read.cbDataBuffer  = cbToRead;
                    cbToRead = (uint32_t)cbBuf;
                    pSSM->u.Read.offDataBuffer = cbToRead;
                    memcpy(pvBuf, &pSSM->u.Read.abDataBuffer[0], cbToRead);
                }
                break;
            }

            case SSM_REC_TYPE_RAW_ZERO:
            {
                int rc = ssmR3DataReadV2RawZeroHdr(pSSM, &cbToRead);
                if (RT_FAILURE(rc))
                    return rc;
                if (cbToRead > cbBuf)
                {
                    /* Spill the remainder into the data buffer. */
                    memset(&pSSM->u.Read.abDataBuffer[0], 0, cbToRead - cbBuf);
                    pSSM->u.Read.cbDataBuffer  = cbToRead - (uint32_t)cbBuf;
                    pSSM->u.Read.offDataBuffer = 0;
                    cbToRead = (uint32_t)cbBuf;
                }
                memset(pvBuf, 0, cbToRead);
                break;
            }

            default:
                AssertMsgFailedReturn(("%x\n", pSSM->u.Read.u8TypeAndFlags), pSSM->rc = VERR_SSM_BAD_REC_TYPE);
        }

        pSSM->offUnitUser += cbToRead;
        cbBuf -= cbToRead;
        pvBuf = (uint8_t *)pvBuf + cbToRead;
    } while (cbBuf > 0);

    Log4(("ssmR3DataReadUnBufferedV2: %08llx|%08llx/%08x/%08x: cbBuf=%#x %.*Rhxs%s\n",
          ssmR3StrmTell(&pSSM->Strm), pSSM->offUnit, pSSM->u.Read.cbRecLeft, 0, cbBufOrg, RT_MIN(SSM_LOG_BYTES, cbBufOrg), pvBufOrg, cbBufOrg > SSM_LOG_BYTES ? "..." : ""));
    return VINF_SUCCESS;
}


/**
 * Buffer miss, do a buffered read.
 *
 * @returns VBox status code. Sets pSSM->rc on error.
 *
 * @param   pSSM            The saved state handle.
 * @param   pvBuf           Where to store the read data.
 * @param   cbBuf           Number of bytes to read.
 */
static int ssmR3DataReadBufferedV2(PSSMHANDLE pSSM, void *pvBuf, size_t cbBuf)
{
    void   const *pvBufOrg = pvBuf; NOREF(pvBufOrg);
    size_t const  cbBufOrg = cbBuf; NOREF(cbBufOrg);

    /*
     * Copy out what we've got in the buffer.
     */
    uint32_t off        = pSSM->u.Read.offDataBuffer;
    int32_t  cbInBuffer = pSSM->u.Read.cbDataBuffer - off;
    Log4(("ssmR3DataReadBufferedV2: %08llx|%08llx/%08x/%08x: cbBuf=%#x\n", ssmR3StrmTell(&pSSM->Strm), pSSM->offUnit, pSSM->u.Read.cbRecLeft, cbInBuffer, cbBufOrg));
    if (cbInBuffer > 0)
    {
        uint32_t const cbToCopy = (uint32_t)cbInBuffer;
        Assert(cbBuf > cbToCopy);
        memcpy(pvBuf, &pSSM->u.Read.abDataBuffer[off], cbToCopy);
        pvBuf  = (uint8_t *)pvBuf + cbToCopy;
        cbBuf -= cbToCopy;
        pSSM->offUnitUser += cbToCopy;
        pSSM->u.Read.cbDataBuffer  = 0;
        pSSM->u.Read.offDataBuffer = 0;
    }

    /*
     * Buffer more data.
     */
    do
    {
        /*
         * Read the next record header if no more data.
         */
        if (!pSSM->u.Read.cbRecLeft)
        {
            int rc = ssmR3DataReadRecHdrV2(pSSM);
            if (RT_FAILURE(rc))
                return pSSM->rc = rc;
        }
        AssertLogRelMsgReturn(!pSSM->u.Read.fEndOfData, ("cbBuf=%zu\n", cbBuf), pSSM->rc = VERR_SSM_LOADED_TOO_MUCH);

        /*
         * Read data from the current record.
         * LATER: optimize by reading directly into the output buffer for some cases.
         */
        uint32_t cbToRead;
        switch (pSSM->u.Read.u8TypeAndFlags & SSM_REC_TYPE_MASK)
        {
            case SSM_REC_TYPE_RAW:
            {
                cbToRead = RT_MIN(sizeof(pSSM->u.Read.abDataBuffer), pSSM->u.Read.cbRecLeft);
                int rc = ssmR3DataReadV2Raw(pSSM, &pSSM->u.Read.abDataBuffer[0], cbToRead);
                if (RT_FAILURE(rc))
                    return pSSM->rc = rc;
                pSSM->u.Read.cbRecLeft   -= cbToRead;
                pSSM->u.Read.cbDataBuffer = cbToRead;
                break;
            }

            case SSM_REC_TYPE_RAW_LZF:
            {
                int rc = ssmR3DataReadV2RawLzfHdr(pSSM, &cbToRead);
                if (RT_FAILURE(rc))
                    return rc;
                rc = ssmR3DataReadV2RawLzf(pSSM, &pSSM->u.Read.abDataBuffer[0], cbToRead);
                if (RT_FAILURE(rc))
                    return rc;
                pSSM->u.Read.cbDataBuffer = cbToRead;
                break;
            }

            case SSM_REC_TYPE_RAW_ZERO:
            {
                int rc = ssmR3DataReadV2RawZeroHdr(pSSM, &cbToRead);
                if (RT_FAILURE(rc))
                    return rc;
                memset(&pSSM->u.Read.abDataBuffer[0], 0, cbToRead);
                pSSM->u.Read.cbDataBuffer = cbToRead;
                break;
            }

            default:
                AssertMsgFailedReturn(("%x\n", pSSM->u.Read.u8TypeAndFlags), pSSM->rc = VERR_SSM_BAD_REC_TYPE);
        }
        /*pSSM->u.Read.offDataBuffer = 0;*/

        /*
         * Copy data from the buffer.
         */
        uint32_t cbToCopy = (uint32_t)RT_MIN(cbBuf, cbToRead);
        memcpy(pvBuf, &pSSM->u.Read.abDataBuffer[0], cbToCopy);
        cbBuf -= cbToCopy;
        pvBuf = (uint8_t *)pvBuf + cbToCopy;
        pSSM->offUnitUser       += cbToCopy;
        pSSM->u.Read.offDataBuffer = cbToCopy;
    } while (cbBuf > 0);

    Log4(("ssmR3DataReadBufferedV2: %08llx|%08llx/%08x/%08x: cbBuf=%#x %.*Rhxs%s\n",
          ssmR3StrmTell(&pSSM->Strm), pSSM->offUnit, pSSM->u.Read.cbRecLeft, pSSM->u.Read.cbDataBuffer - pSSM->u.Read.offDataBuffer,
          cbBufOrg, RT_MIN(SSM_LOG_BYTES, cbBufOrg), pvBufOrg, cbBufOrg > SSM_LOG_BYTES ? "..." : ""));
    return VINF_SUCCESS;
}


/**
 * Inlined worker that handles format checks and buffered reads.
 *
 * @param   pSSM            The saved state handle.
 * @param   pvBuf           Where to store the read data.
 * @param   cbBuf           Number of bytes to read.
 */
DECLINLINE(int) ssmR3DataRead(PSSMHANDLE pSSM, void *pvBuf, size_t cbBuf)
{
    /*
     * Fend off previous errors and V1 data units.
     */
    if (RT_SUCCESS(pSSM->rc))
    {
        if (RT_LIKELY(pSSM->u.Read.uFmtVerMajor != 1))
        {
            /*
             * Check if the requested data is buffered.
             */
            uint32_t off = pSSM->u.Read.offDataBuffer;
            if (   off + cbBuf > pSSM->u.Read.cbDataBuffer
                || cbBuf > sizeof(pSSM->u.Read.abDataBuffer))
            {
                if (cbBuf <= sizeof(pSSM->u.Read.abDataBuffer) / 8)
                    return ssmR3DataReadBufferedV2(pSSM, pvBuf, cbBuf);
                return ssmR3DataReadUnbufferedV2(pSSM, pvBuf, cbBuf);
            }

            memcpy(pvBuf, &pSSM->u.Read.abDataBuffer[off], cbBuf);
            pSSM->u.Read.offDataBuffer = off + (uint32_t)cbBuf;
            pSSM->offUnitUser += cbBuf;
            Log4((cbBuf
                  ? "ssmR3DataRead: %08llx|%08llx/%08x/%08x: cbBuf=%#x %.*Rhxs%s\n"
                  : "ssmR3DataRead: %08llx|%08llx/%08x/%08x: cbBuf=%#x\n",
                  ssmR3StrmTell(&pSSM->Strm), pSSM->offUnit, pSSM->u.Read.cbRecLeft, pSSM->u.Read.cbDataBuffer - pSSM->u.Read.offDataBuffer,
                  cbBuf, RT_MIN(SSM_LOG_BYTES, cbBuf), pvBuf, cbBuf > SSM_LOG_BYTES ? "..." : ""));

            return VINF_SUCCESS;
        }
        return ssmR3DataReadV1(pSSM, pvBuf, cbBuf);
    }
    return pSSM->rc;
}


/**
 * Gets a structure.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pvStruct        The structure address.
 * @param   paFields        The array of structure fields descriptions.
 *                          The array must be terminated by a SSMFIELD_ENTRY_TERM().
 */
VMMR3DECL(int) SSMR3GetStruct(PSSMHANDLE pSSM, void *pvStruct, PCSSMFIELD paFields)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    AssertPtr(pvStruct);
    AssertPtr(paFields);

    /* begin marker. */
    uint32_t u32Magic;
    int rc = SSMR3GetU32(pSSM, &u32Magic);
    if (RT_FAILURE(rc))
        return rc;
    AssertMsgReturn(u32Magic == SSMR3STRUCT_BEGIN, ("u32Magic=%#RX32\n", u32Magic), pSSM->rc = VERR_SSM_STRUCTURE_MAGIC);

    /* get the fields */
    for (PCSSMFIELD pCur = paFields;
         pCur->cb != UINT32_MAX && pCur->off != UINT32_MAX;
         pCur++)
    {
        if (pCur->uFirstVer <= pSSM->u.Read.uCurUnitVer)
        {
            uint8_t *pbField = (uint8_t *)pvStruct + pCur->off;
            switch ((uintptr_t)pCur->pfnGetPutOrTransformer)
            {
                case SSMFIELDTRANS_NO_TRANSFORMATION:
                    rc = ssmR3DataRead(pSSM, pbField, pCur->cb);
                    break;

                case SSMFIELDTRANS_GCPTR:
                    AssertMsgBreakStmt(pCur->cb == sizeof(RTGCPTR), ("%#x (%s)\n", pCur->cb, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = SSMR3GetGCPtr(pSSM, (PRTGCPTR)pbField);
                    break;

                case SSMFIELDTRANS_GCPHYS:
                    AssertMsgBreakStmt(pCur->cb == sizeof(RTGCPHYS), ("%#x (%s)\n", pCur->cb, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = SSMR3GetGCPhys(pSSM, (PRTGCPHYS)pbField);
                    break;

                case SSMFIELDTRANS_RCPTR:
                    AssertMsgBreakStmt(pCur->cb == sizeof(RTRCPTR), ("%#x (%s)\n", pCur->cb, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = SSMR3GetRCPtr(pSSM, (PRTRCPTR)pbField);
                    break;

                case SSMFIELDTRANS_RCPTR_ARRAY:
                {
                    uint32_t const cEntries = pCur->cb / sizeof(RTRCPTR);
                    AssertMsgBreakStmt(pCur->cb == cEntries * sizeof(RTRCPTR) && cEntries, ("%#x (%s)\n", pCur->cb, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = VINF_SUCCESS;
                    for (uint32_t i = 0; i < cEntries && RT_SUCCESS(rc); i++)
                        rc = SSMR3GetRCPtr(pSSM, &((PRTRCPTR)pbField)[i]);
                    break;
                }

                default:
                    AssertMsgFailedBreakStmt(("%#x\n", pCur->pfnGetPutOrTransformer), rc = VERR_SSM_FIELD_COMPLEX);
            }
            if (RT_FAILURE(rc))
            {
                if (RT_SUCCESS(pSSM->rc))
                    pSSM->rc = rc;
                return rc;
            }
        }
    }

    /* end marker */
    rc = SSMR3GetU32(pSSM, &u32Magic);
    if (RT_FAILURE(rc))
        return rc;
    AssertMsgReturn(u32Magic == SSMR3STRUCT_END, ("u32Magic=%#RX32\n", u32Magic), pSSM->rc = VERR_SSM_STRUCTURE_MAGIC);
    return rc;
}


/**
 * SSMR3GetStructEx helper that gets a HCPTR that is used as a NULL indicator.
 *
 * @returns VBox status code.
 *
 * @param   pSSM            The saved state handle.
 * @param   ppv             Where to return the value (0/1).
 * @param   fFlags          SSMSTRUCT_FLAGS_XXX.
 */
DECLINLINE(int) ssmR3GetHCPtrNI(PSSMHANDLE pSSM, void **ppv, uint32_t fFlags)
{
    uintptr_t uPtrNI;
    if (fFlags & SSMSTRUCT_FLAGS_DONT_IGNORE)
    {
        if (ssmR3GetHostBits(pSSM) == 64)
        {
            uint64_t u;
            int rc = ssmR3DataRead(pSSM, &u, sizeof(u));
            if (RT_FAILURE(rc))
                return rc;
            uPtrNI = u ? 1 : 0;
        }
        else
        {
            uint32_t u;
            int rc = ssmR3DataRead(pSSM, &u, sizeof(u));
            if (RT_FAILURE(rc))
                return rc;
            uPtrNI = u ? 1 : 0;
        }
    }
    else
    {
        bool f;
        int rc = SSMR3GetBool(pSSM, &f);
        if (RT_FAILURE(rc))
            return rc;
        uPtrNI = f ? 1 : 0;
    }
    *ppv = (void *)uPtrNI;
    return VINF_SUCCESS;
}


/**
 * Gets a structure, extended API.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pvStruct        The structure address.
 * @param   cbStruct        The size of the struct (use for validation only).
 * @param   fFlags          Combination of SSMSTRUCT_FLAGS_XXX defines.
 * @param   paFields        The array of structure fields descriptions. The
 *                          array must be terminated by a SSMFIELD_ENTRY_TERM().
 * @param   pvUser          User argument for any callbacks that paFields might
 *                          contain.
 */
VMMR3DECL(int) SSMR3GetStructEx(PSSMHANDLE pSSM, void *pvStruct, size_t cbStruct,
                                uint32_t fFlags, PCSSMFIELD paFields, void *pvUser)
{
    int         rc;
    uint32_t    u32Magic;

    /*
     * Validation.
     */
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    AssertMsgReturn(!(fFlags & ~SSMSTRUCT_FLAGS_VALID_MASK), ("%#x\n", fFlags), pSSM->rc = VERR_INVALID_PARAMETER);
    AssertPtr(pvStruct);
    AssertPtr(paFields);

    /*
     * Begin marker.
     */
    if (!(fFlags & (SSMSTRUCT_FLAGS_NO_MARKERS | SSMSTRUCT_FLAGS_NO_LEAD_MARKER)))
    {
        rc = SSMR3GetU32(pSSM, &u32Magic);
        if (RT_FAILURE(rc))
            return rc;
        AssertMsgReturn(u32Magic == SSMR3STRUCT_BEGIN, ("u32Magic=%#RX32\n", u32Magic), pSSM->rc = VERR_SSM_STRUCTURE_MAGIC);
    }

    /*
     * Put the fields
     */
    rc = VINF_SUCCESS;
    uint32_t off = 0;
    for (PCSSMFIELD pCur = paFields;
         pCur->cb != UINT32_MAX && pCur->off != UINT32_MAX;
         pCur++)
    {
        uint32_t const offField =    (!SSMFIELDTRANS_IS_PADDING(pCur->pfnGetPutOrTransformer) || pCur->off != UINT32_MAX / 2)
                                  &&  !SSMFIELDTRANS_IS_OLD(pCur->pfnGetPutOrTransformer)
                                ? pCur->off
                                : off;
        uint32_t const cbField  = SSMFIELDTRANS_IS_OLD(pCur->pfnGetPutOrTransformer)
                                ? 0
                                : SSMFIELDTRANS_IS_PADDING(pCur->pfnGetPutOrTransformer)
                                ? RT_HIWORD(pCur->cb)
                                : pCur->cb;
        AssertMsgReturn(   cbField            <= cbStruct
                        && offField + cbField <= cbStruct
                        && offField + cbField >= offField,
                        ("off=%#x cb=%#x cbStruct=%#x (%s)\n", cbField, offField, cbStruct, pCur->pszName),
                        pSSM->rc = VERR_SSM_FIELD_OUT_OF_BOUNDS);
        AssertMsgReturn(    !(fFlags & SSMSTRUCT_FLAGS_FULL_STRUCT)
                        || off == offField,
                        ("off=%#x offField=%#x (%s)\n", off, offField, pCur->pszName),
                        pSSM->rc = VERR_SSM_FIELD_NOT_CONSECUTIVE);

        if (pCur->uFirstVer <= pSSM->u.Read.uCurUnitVer)
        {
            rc = VINF_SUCCESS;
            uint8_t       *pbField  = (uint8_t *)pvStruct + offField;
            switch ((uintptr_t)pCur->pfnGetPutOrTransformer)
            {
                case SSMFIELDTRANS_NO_TRANSFORMATION:
                    rc = ssmR3DataRead(pSSM, pbField, cbField);
                    break;

                case SSMFIELDTRANS_GCPHYS:
                    AssertMsgBreakStmt(cbField == sizeof(RTGCPHYS), ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = SSMR3GetGCPhys(pSSM, (PRTGCPHYS)pbField);
                    break;

                case SSMFIELDTRANS_GCPTR:
                    AssertMsgBreakStmt(cbField == sizeof(RTGCPTR), ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = SSMR3GetGCPtr(pSSM, (PRTGCPTR)pbField);
                    break;

                case SSMFIELDTRANS_RCPTR:
                    AssertMsgBreakStmt(cbField == sizeof(RTRCPTR), ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = SSMR3GetRCPtr(pSSM, (PRTRCPTR)pbField);
                    break;

                case SSMFIELDTRANS_RCPTR_ARRAY:
                {
                    uint32_t const cEntries = cbField / sizeof(RTRCPTR);
                    AssertMsgBreakStmt(cbField == cEntries * sizeof(RTRCPTR) && cEntries, ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = VINF_SUCCESS;
                    for (uint32_t i = 0; i < cEntries && RT_SUCCESS(rc); i++)
                        rc = SSMR3GetRCPtr(pSSM, &((PRTRCPTR)pbField)[i]);
                    break;
                }

                case SSMFIELDTRANS_HCPTR_NI:
                    AssertMsgBreakStmt(cbField == sizeof(void *), ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = ssmR3GetHCPtrNI(pSSM, (void **)pbField, fFlags);
                    break;

                case SSMFIELDTRANS_HCPTR_NI_ARRAY:
                {
                    uint32_t const cEntries = cbField / sizeof(void *);
                    AssertMsgBreakStmt(cbField == cEntries * sizeof(void *) && cEntries, ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = VINF_SUCCESS;
                    for (uint32_t i = 0; i < cEntries && RT_SUCCESS(rc); i++)
                        rc = ssmR3GetHCPtrNI(pSSM, &((void **)pbField)[i], fFlags);
                    break;
                }

                case SSMFIELDTRANS_HCPTR_HACK_U32:
                    AssertMsgBreakStmt(cbField == sizeof(void *), ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    *(uintptr_t *)pbField = 0;
                    rc = ssmR3DataRead(pSSM, pbField, sizeof(uint32_t));
                    if ((fFlags & SSMSTRUCT_FLAGS_DONT_IGNORE) && ssmR3GetHostBits(pSSM) == 64)
                    {
                        uint32_t u32;
                        rc = ssmR3DataRead(pSSM, &u32, sizeof(uint32_t));
                        AssertMsgBreakStmt(RT_FAILURE(rc) || u32 == 0 || (fFlags & SSMSTRUCT_FLAGS_SAVED_AS_MEM),
                                           ("high=%#x low=%#x (%s)\n", u32, *(uint32_t *)pbField, pCur->pszName),
                                           rc = VERR_SSM_FIELD_INVALID_VALUE);
                    }
                    break;

                case SSMFIELDTRANS_U32_ZX_U64:
                    AssertMsgBreakStmt(cbField == sizeof(uint64_t), ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    ((uint32_t *)pbField)[1] = 0;
                    rc = SSMR3GetU32(pSSM, (uint32_t *)pbField);
                    break;


                case SSMFIELDTRANS_IGNORE:
                    if (fFlags & SSMSTRUCT_FLAGS_DONT_IGNORE)
                        rc = SSMR3Skip(pSSM, cbField);
                    break;

                case SSMFIELDTRANS_IGN_GCPHYS:
                    AssertMsgBreakStmt(cbField == sizeof(RTGCPHYS), ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    if (fFlags & SSMSTRUCT_FLAGS_DONT_IGNORE)
                        rc = SSMR3Skip(pSSM, pSSM->u.Read.cbGCPhys);
                    break;

                case SSMFIELDTRANS_IGN_GCPTR:
                    AssertMsgBreakStmt(cbField == sizeof(RTGCPTR), ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    if (fFlags & SSMSTRUCT_FLAGS_DONT_IGNORE)
                        rc = SSMR3Skip(pSSM, pSSM->u.Read.cbGCPtr);
                    break;

                case SSMFIELDTRANS_IGN_RCPTR:
                    AssertMsgBreakStmt(cbField == sizeof(RTRCPTR), ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    if (fFlags & SSMSTRUCT_FLAGS_DONT_IGNORE)
                        rc = SSMR3Skip(pSSM, sizeof(RTRCPTR));
                    break;

                case SSMFIELDTRANS_IGN_HCPTR:
                    AssertMsgBreakStmt(cbField == sizeof(void *), ("%#x (%s)\n", cbField, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    if (fFlags & SSMSTRUCT_FLAGS_DONT_IGNORE)
                        rc = SSMR3Skip(pSSM, ssmR3GetHostBits(pSSM) / 8);
                    break;


                case SSMFIELDTRANS_OLD:
                    AssertMsgBreakStmt(pCur->off == UINT32_MAX / 2, ("%#x %#x (%s)\n", pCur->cb, pCur->off, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = SSMR3Skip(pSSM, pCur->cb);
                    break;

                case SSMFIELDTRANS_OLD_GCPHYS:
                    AssertMsgBreakStmt(pCur->cb == sizeof(RTGCPHYS) && pCur->off == UINT32_MAX / 2, ("%#x %#x (%s)\n", pCur->cb, pCur->off, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = SSMR3Skip(pSSM, pSSM->u.Read.cbGCPhys);
                    break;

                case SSMFIELDTRANS_OLD_GCPTR:
                    AssertMsgBreakStmt(pCur->cb == sizeof(RTGCPTR) && pCur->off == UINT32_MAX / 2, ("%#x %#x (%s)\n", pCur->cb, pCur->off, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = SSMR3Skip(pSSM, pSSM->u.Read.cbGCPtr);
                    break;

                case SSMFIELDTRANS_OLD_RCPTR:
                    AssertMsgBreakStmt(pCur->cb == sizeof(RTRCPTR) && pCur->off == UINT32_MAX / 2, ("%#x %#x (%s)\n", pCur->cb, pCur->off, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = SSMR3Skip(pSSM, sizeof(RTRCPTR));
                    break;

                case SSMFIELDTRANS_OLD_HCPTR:
                    AssertMsgBreakStmt(pCur->cb == sizeof(void *) && pCur->off == UINT32_MAX / 2, ("%#x %#x (%s)\n", pCur->cb, pCur->off, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = SSMR3Skip(pSSM, ssmR3GetHostBits(pSSM) / 8);
                    break;

                case SSMFIELDTRANS_OLD_PAD_HC:
                    AssertMsgBreakStmt(pCur->off == UINT32_MAX / 2, ("%#x %#x (%s)\n", pCur->cb, pCur->off, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    rc = SSMR3Skip(pSSM, ssmR3GetHostBits(pSSM) == 64 ? RT_HIWORD(pCur->cb) : RT_LOWORD(pCur->cb));
                    break;

                case SSMFIELDTRANS_OLD_PAD_MSC32:
                    AssertMsgBreakStmt(pCur->off == UINT32_MAX / 2, ("%#x %#x (%s)\n", pCur->cb, pCur->off, pCur->pszName), rc = VERR_SSM_FIELD_INVALID_SIZE);
                    if (ssmR3IsHostMsc32(pSSM))
                        rc = SSMR3Skip(pSSM, pCur->cb);
                    break;


                case SSMFIELDTRANS_PAD_HC:
                case SSMFIELDTRANS_PAD_HC32:
                case SSMFIELDTRANS_PAD_HC64:
                case SSMFIELDTRANS_PAD_HC_AUTO:
                case SSMFIELDTRANS_PAD_MSC32_AUTO:
                {
                    uint32_t cb32    = RT_BYTE1(pCur->cb);
                    uint32_t cb64    = RT_BYTE2(pCur->cb);
                    uint32_t cbCtx   =    HC_ARCH_BITS == 64
                                       || (   (uintptr_t)pCur->pfnGetPutOrTransformer == SSMFIELDTRANS_PAD_MSC32_AUTO
                                           && !SSM_HOST_IS_MSC_32)
                                     ? cb64 : cb32;
                    uint32_t cbSaved =    ssmR3GetHostBits(pSSM) == 64
                                       || (   (uintptr_t)pCur->pfnGetPutOrTransformer == SSMFIELDTRANS_PAD_MSC32_AUTO
                                           && !ssmR3IsHostMsc32(pSSM))
                                     ? cb64 : cb32;
                    AssertMsgBreakStmt(    cbField == cbCtx
                                       &&  (   (   pCur->off == UINT32_MAX / 2
                                                && (   cbField == 0
                                                    || (uintptr_t)pCur->pfnGetPutOrTransformer == SSMFIELDTRANS_PAD_HC_AUTO
                                                    || (uintptr_t)pCur->pfnGetPutOrTransformer == SSMFIELDTRANS_PAD_MSC32_AUTO
                                                   )
                                               )
                                            || (pCur->off != UINT32_MAX / 2 && cbField != 0)
                                            )
                                       , ("cbField=%#x cb32=%#x cb64=%#x HC_ARCH_BITS=%u cbCtx=%#x cbSaved=%#x off=%#x\n",
                                          cbField, cb32, cb64, HC_ARCH_BITS, cbCtx, cbSaved, pCur->off),
                                       rc = VERR_SSM_FIELD_INVALID_PADDING_SIZE);
                    if (fFlags & SSMSTRUCT_FLAGS_DONT_IGNORE)
                        rc = SSMR3Skip(pSSM, cbSaved);
                    break;
                }

                default:
                    AssertBreakStmt(pCur->pfnGetPutOrTransformer, rc = VERR_SSM_FIELD_INVALID_CALLBACK);
                    rc = pCur->pfnGetPutOrTransformer(pSSM, pCur, pvStruct, fFlags, true /*fGetOrPut*/, pvUser);
                    break;
            }
            if (RT_FAILURE(rc))
                break;
        }

        off = offField + cbField;
    }

    if (RT_SUCCESS(rc))
        AssertMsgStmt(   !(fFlags & SSMSTRUCT_FLAGS_FULL_STRUCT)
                      || off == cbStruct,
                      ("off=%#x cbStruct=%#x\n", off, cbStruct),
                      rc = VERR_SSM_FIELD_NOT_CONSECUTIVE);

    if (RT_FAILURE(rc))
    {
        if (RT_SUCCESS(pSSM->rc))
            pSSM->rc = rc;
        return rc;
    }

    /*
     * End marker
     */
    if (!(fFlags & (SSMSTRUCT_FLAGS_NO_MARKERS | SSMSTRUCT_FLAGS_NO_TAIL_MARKER)))
    {
        rc = SSMR3GetU32(pSSM, &u32Magic);
        if (RT_FAILURE(rc))
            return rc;
        AssertMsgReturn(u32Magic == SSMR3STRUCT_END, ("u32Magic=%#RX32\n", u32Magic), pSSM->rc = VERR_SSM_STRUCTURE_MAGIC);
    }

    return VINF_SUCCESS;
}


/**
 * Loads a boolean item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pfBool          Where to store the item.
 */
VMMR3DECL(int) SSMR3GetBool(PSSMHANDLE pSSM, bool *pfBool)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    uint8_t u8; /* see SSMR3PutBool */
    int rc = ssmR3DataRead(pSSM, &u8, sizeof(u8));
    if (RT_SUCCESS(rc))
    {
        Assert(u8 <= 1);
        *pfBool = RT_BOOL(u8);
    }
    return rc;
}


/**
 * Loads a volatile boolean item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pfBool          Where to store the item.
 */
VMMR3DECL(int) SSMR3GetBoolV(PSSMHANDLE pSSM, bool volatile *pfBool)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    uint8_t u8; /* see SSMR3PutBool */
    int rc = ssmR3DataRead(pSSM, &u8, sizeof(u8));
    if (RT_SUCCESS(rc))
    {
        Assert(u8 <= 1);
        *pfBool = RT_BOOL(u8);
    }
    return rc;
}


/**
 * Loads a 8-bit unsigned integer item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pu8             Where to store the item.
 */
VMMR3DECL(int) SSMR3GetU8(PSSMHANDLE pSSM, uint8_t *pu8)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, pu8, sizeof(*pu8));
}


/**
 * Loads a volatile 8-bit unsigned integer item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pu8             Where to store the item.
 */
VMMR3DECL(int) SSMR3GetU8V(PSSMHANDLE pSSM, uint8_t volatile *pu8)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, (void *)pu8, sizeof(*pu8));
}


/**
 * Loads a 8-bit signed integer item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pi8             Where to store the item.
 */
VMMR3DECL(int) SSMR3GetS8(PSSMHANDLE pSSM, int8_t *pi8)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, pi8, sizeof(*pi8));
}


/**
 * Loads a volatile 8-bit signed integer item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pi8             Where to store the item.
 */
VMMR3DECL(int) SSMR3GetS8V(PSSMHANDLE pSSM, int8_t volatile *pi8)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, (void *)pi8, sizeof(*pi8));
}


/**
 * Loads a 16-bit unsigned integer item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pu16            Where to store the item.
 */
VMMR3DECL(int) SSMR3GetU16(PSSMHANDLE pSSM, uint16_t *pu16)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, pu16, sizeof(*pu16));
}


/**
 * Loads a volatile 16-bit unsigned integer item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pu16            Where to store the item.
 */
VMMR3DECL(int) SSMR3GetU16V(PSSMHANDLE pSSM, uint16_t volatile *pu16)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, (void *)pu16, sizeof(*pu16));
}


/**
 * Loads a 16-bit signed integer item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pi16            Where to store the item.
 */
VMMR3DECL(int) SSMR3GetS16(PSSMHANDLE pSSM, int16_t *pi16)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, pi16, sizeof(*pi16));
}


/**
 * Loads a volatile 16-bit signed integer item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pi16            Where to store the item.
 */
VMMR3DECL(int) SSMR3GetS16V(PSSMHANDLE pSSM, int16_t volatile *pi16)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, (void *)pi16, sizeof(*pi16));
}


/**
 * Loads a 32-bit unsigned integer item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pu32            Where to store the item.
 */
VMMR3DECL(int) SSMR3GetU32(PSSMHANDLE pSSM, uint32_t *pu32)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, pu32, sizeof(*pu32));
}


/**
 * Loads a volatile 32-bit unsigned integer item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pu32            Where to store the item.
 */
VMMR3DECL(int) SSMR3GetU32V(PSSMHANDLE pSSM, uint32_t volatile *pu32)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, (void *)pu32, sizeof(*pu32));
}


/**
 * Loads a 32-bit signed integer item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pi32            Where to store the item.
 */
VMMR3DECL(int) SSMR3GetS32(PSSMHANDLE pSSM, int32_t *pi32)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, pi32, sizeof(*pi32));
}


/**
 * Loads a volatile 32-bit signed integer item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pi32            Where to store the item.
 */
VMMR3DECL(int) SSMR3GetS32V(PSSMHANDLE pSSM, int32_t volatile *pi32)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, (void *)pi32, sizeof(*pi32));
}


/**
 * Loads a 64-bit unsigned integer item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pu64            Where to store the item.
 */
VMMR3DECL(int) SSMR3GetU64(PSSMHANDLE pSSM, uint64_t *pu64)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, pu64, sizeof(*pu64));
}


/**
 * Loads a volatile 64-bit unsigned integer item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pu64            Where to store the item.
 */
VMMR3DECL(int) SSMR3GetU64V(PSSMHANDLE pSSM, uint64_t volatile *pu64)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, (void *)pu64, sizeof(*pu64));
}


/**
 * Loads a 64-bit signed integer item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pi64            Where to store the item.
 */
VMMR3DECL(int) SSMR3GetS64(PSSMHANDLE pSSM, int64_t *pi64)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, pi64, sizeof(*pi64));
}


/**
 * Loads a volatile 64-bit signed integer item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pi64            Where to store the item.
 */
VMMR3DECL(int) SSMR3GetS64V(PSSMHANDLE pSSM, int64_t volatile *pi64)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, (void *)pi64, sizeof(*pi64));
}


/**
 * Loads a 128-bit unsigned integer item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pu128           Where to store the item.
 */
VMMR3DECL(int) SSMR3GetU128(PSSMHANDLE pSSM, uint128_t *pu128)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, pu128, sizeof(*pu128));
}


/**
 * Loads a volatile 128-bit unsigned integer item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pu128           Where to store the item.
 */
VMMR3DECL(int) SSMR3GetU128V(PSSMHANDLE pSSM, uint128_t volatile *pu128)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, (void *)pu128, sizeof(*pu128));
}


/**
 * Loads a 128-bit signed integer item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pi128           Where to store the item.
 */
VMMR3DECL(int) SSMR3GetS128(PSSMHANDLE pSSM, int128_t *pi128)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, pi128, sizeof(*pi128));
}


/**
 * Loads a volatile 128-bit signed integer item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pi128           Where to store the item.
 */
VMMR3DECL(int) SSMR3GetS128V(PSSMHANDLE pSSM, int128_t volatile *pi128)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, (void *)pi128, sizeof(*pi128));
}


/**
 * Loads a VBox unsigned integer item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pu              Where to store the integer.
 */
VMMR3DECL(int) SSMR3GetUInt(PSSMHANDLE pSSM, PRTUINT pu)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, pu, sizeof(*pu));
}


/**
 * Loads a VBox signed integer item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pi              Where to store the integer.
 */
VMMR3DECL(int) SSMR3GetSInt(PSSMHANDLE pSSM, PRTINT pi)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, pi, sizeof(*pi));
}


/**
 * Loads a GC natural unsigned integer item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pu              Where to store the integer.
 *
 * @deprecated Silly type with an incorrect size, don't use it.
 */
VMMR3DECL(int) SSMR3GetGCUInt(PSSMHANDLE pSSM, PRTGCUINT pu)
{
    AssertCompile(sizeof(RTGCPTR) == sizeof(*pu));
    return SSMR3GetGCPtr(pSSM, (PRTGCPTR)pu);
}


/**
 * Loads a GC unsigned integer register item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pu              Where to store the integer.
 */
VMMR3DECL(int) SSMR3GetGCUIntReg(PSSMHANDLE pSSM, PRTGCUINTREG pu)
{
    AssertCompile(sizeof(RTGCPTR) == sizeof(*pu));
    return SSMR3GetGCPtr(pSSM, (PRTGCPTR)pu);
}


/**
 * Loads a 32 bits GC physical address item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pGCPhys         Where to store the GC physical address.
 */
VMMR3DECL(int) SSMR3GetGCPhys32(PSSMHANDLE pSSM, PRTGCPHYS32 pGCPhys)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, pGCPhys, sizeof(*pGCPhys));
}


/**
 * Loads a 32 bits GC physical address item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pGCPhys         Where to store the GC physical address.
 */
VMMR3DECL(int) SSMR3GetGCPhys32V(PSSMHANDLE pSSM, RTGCPHYS32 volatile *pGCPhys)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, (void *)pGCPhys, sizeof(*pGCPhys));
}


/**
 * Loads a 64 bits GC physical address item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pGCPhys         Where to store the GC physical address.
 */
VMMR3DECL(int) SSMR3GetGCPhys64(PSSMHANDLE pSSM, PRTGCPHYS64 pGCPhys)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, pGCPhys, sizeof(*pGCPhys));
}


/**
 * Loads a volatile 64 bits GC physical address item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pGCPhys         Where to store the GC physical address.
 */
VMMR3DECL(int) SSMR3GetGCPhys64V(PSSMHANDLE pSSM, RTGCPHYS64 volatile *pGCPhys)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, (void *)pGCPhys, sizeof(*pGCPhys));
}


/**
 * Loads a GC physical address item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pGCPhys         Where to store the GC physical address.
 */
VMMR3DECL(int) SSMR3GetGCPhys(PSSMHANDLE pSSM, PRTGCPHYS pGCPhys)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);

    /*
     * Default size?
     */
    if (RT_LIKELY(sizeof(*pGCPhys) == pSSM->u.Read.cbGCPhys))
        return ssmR3DataRead(pSSM, pGCPhys, sizeof(*pGCPhys));

    /*
     * Fiddly.
     */
    Assert(sizeof(*pGCPhys)      == sizeof(uint64_t) || sizeof(*pGCPhys)      == sizeof(uint32_t));
    Assert(pSSM->u.Read.cbGCPhys == sizeof(uint64_t) || pSSM->u.Read.cbGCPhys == sizeof(uint32_t));
    if (pSSM->u.Read.cbGCPhys == sizeof(uint64_t))
    {
        /* 64-bit saved, 32-bit load: try truncate it. */
        uint64_t u64;
        int rc = ssmR3DataRead(pSSM, &u64, sizeof(uint64_t));
        if (RT_FAILURE(rc))
            return rc;
        if (u64 >= _4G)
            return VERR_SSM_GCPHYS_OVERFLOW;
        *pGCPhys = (RTGCPHYS)u64;
        return rc;
    }

    /* 32-bit saved, 64-bit load: clear the high part. */
    *pGCPhys = 0;
    return ssmR3DataRead(pSSM, pGCPhys, sizeof(uint32_t));
}

/**
 * Loads a volatile GC physical address item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pGCPhys         Where to store the GC physical address.
 */
VMMR3DECL(int) SSMR3GetGCPhysV(PSSMHANDLE pSSM, RTGCPHYS volatile *pGCPhys)
{
    return SSMR3GetGCPhys(pSSM, (PRTGCPHYS)pGCPhys);
}


/**
 * Loads a GC virtual address item from the current data unit.
 *
 * Only applies to in the 1.1 format:
 *  - SSMR3GetGCPtr
 *  - SSMR3GetGCUIntPtr
 *  - SSMR3GetGCUInt
 *  - SSMR3GetGCUIntReg
 *
 * Put functions are not affected.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   cbGCPtr         Size of RTGCPTR
 *
 * @remarks This interface only works with saved state version 1.1, if the
 *          format isn't 1.1 the call will be ignored.
 */
VMMR3_INT_DECL(int) SSMR3HandleSetGCPtrSize(PSSMHANDLE pSSM, unsigned cbGCPtr)
{
    Assert(cbGCPtr == sizeof(RTGCPTR32) || cbGCPtr == sizeof(RTGCPTR64));
    if (!pSSM->u.Read.fFixedGCPtrSize)
    {
        Log(("SSMR3SetGCPtrSize: %u -> %u bytes\n", pSSM->u.Read.cbGCPtr, cbGCPtr));
        pSSM->u.Read.cbGCPtr = cbGCPtr;
        pSSM->u.Read.fFixedGCPtrSize = true;
    }
    else if (   pSSM->u.Read.cbGCPtr != cbGCPtr
             && pSSM->u.Read.uFmtVerMajor == 1
             && pSSM->u.Read.uFmtVerMinor == 1)
        AssertMsgFailed(("SSMR3SetGCPtrSize: already fixed at %u bytes; requested %u bytes\n", pSSM->u.Read.cbGCPtr, cbGCPtr));

    return VINF_SUCCESS;
}


/**
 * Loads a GC virtual address item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pGCPtr          Where to store the GC virtual address.
 */
VMMR3DECL(int) SSMR3GetGCPtr(PSSMHANDLE pSSM, PRTGCPTR pGCPtr)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);

    /*
     * Default size?
     */
    if (RT_LIKELY(sizeof(*pGCPtr) == pSSM->u.Read.cbGCPtr))
        return ssmR3DataRead(pSSM, pGCPtr, sizeof(*pGCPtr));

    /*
     * Fiddly.
     */
    Assert(sizeof(*pGCPtr)      == sizeof(uint64_t) || sizeof(*pGCPtr)      == sizeof(uint32_t));
    Assert(pSSM->u.Read.cbGCPtr == sizeof(uint64_t) || pSSM->u.Read.cbGCPtr == sizeof(uint32_t));
    if (pSSM->u.Read.cbGCPtr == sizeof(uint64_t))
    {
        /* 64-bit saved, 32-bit load: try truncate it. */
        uint64_t u64;
        int rc = ssmR3DataRead(pSSM, &u64, sizeof(uint64_t));
        if (RT_FAILURE(rc))
            return rc;
        if (u64 >= _4G)
            return VERR_SSM_GCPTR_OVERFLOW;
        *pGCPtr = (RTGCPTR)u64;
        return rc;
    }

    /* 32-bit saved, 64-bit load: clear the high part. */
    *pGCPtr = 0;
    return ssmR3DataRead(pSSM, pGCPtr, sizeof(uint32_t));
}


/**
 * Loads a GC virtual address (represented as unsigned integer) item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pGCPtr          Where to store the GC virtual address.
 */
VMMR3DECL(int) SSMR3GetGCUIntPtr(PSSMHANDLE pSSM, PRTGCUINTPTR pGCPtr)
{
    AssertCompile(sizeof(RTGCPTR) == sizeof(*pGCPtr));
    return SSMR3GetGCPtr(pSSM, (PRTGCPTR)pGCPtr);
}


/**
 * Loads an RC virtual address item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pRCPtr          Where to store the RC virtual address.
 */
VMMR3DECL(int) SSMR3GetRCPtr(PSSMHANDLE pSSM, PRTRCPTR pRCPtr)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, pRCPtr, sizeof(*pRCPtr));
}


/**
 * Loads a I/O port address item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pIOPort         Where to store the I/O port address.
 */
VMMR3DECL(int) SSMR3GetIOPort(PSSMHANDLE pSSM, PRTIOPORT pIOPort)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, pIOPort, sizeof(*pIOPort));
}


/**
 * Loads a selector item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pSel            Where to store the selector.
 */
VMMR3DECL(int) SSMR3GetSel(PSSMHANDLE pSSM, PRTSEL pSel)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, pSel, sizeof(*pSel));
}


/**
 * Loads a memory item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pv              Where to store the item.
 * @param   cb              Size of the item.
 */
VMMR3DECL(int) SSMR3GetMem(PSSMHANDLE pSSM, void *pv, size_t cb)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    return ssmR3DataRead(pSSM, pv, cb);
}


/**
 * Loads a string item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   psz             Where to store the item.
 * @param   cbMax           Max size of the item (including '\\0').
 */
VMMR3DECL(int) SSMR3GetStrZ(PSSMHANDLE pSSM, char *psz, size_t cbMax)
{
    return SSMR3GetStrZEx(pSSM, psz, cbMax, NULL);
}


/**
 * Loads a string item from the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   psz             Where to store the item.
 * @param   cbMax           Max size of the item (including '\\0').
 * @param   pcbStr          The length of the loaded string excluding the '\\0'. (optional)
 */
VMMR3DECL(int) SSMR3GetStrZEx(PSSMHANDLE pSSM, char *psz, size_t cbMax, size_t *pcbStr)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);

    /* read size prefix. */
    uint32_t u32;
    int rc = SSMR3GetU32(pSSM, &u32);
    if (RT_SUCCESS(rc))
    {
        if (pcbStr)
            *pcbStr = u32;
        if (u32 < cbMax)
        {
            /* terminate and read string content. */
            psz[u32] = '\0';
            return ssmR3DataRead(pSSM, psz, u32);
        }
        return VERR_TOO_MUCH_DATA;
    }
    return rc;
}


/**
 * Skips a number of bytes in the current data unit.
 *
 * @returns VBox status code.
 * @param   pSSM                The SSM handle.
 * @param   cb                  The number of bytes to skip.
 */
VMMR3DECL(int) SSMR3Skip(PSSMHANDLE pSSM, size_t cb)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    while (cb > 0)
    {
        uint8_t abBuf[8192];
        size_t  cbCur = RT_MIN(sizeof(abBuf), cb);
        cb -= cbCur;
        int rc = ssmR3DataRead(pSSM, abBuf, cbCur);
        if (RT_FAILURE(rc))
            return rc;
    }

    return VINF_SUCCESS;
}


/**
 * Skips to the end of the current data unit.
 *
 * Since version 2 of the format, the load exec callback have to explicitly call
 * this API if it wish to be lazy for some reason.  This is because there seldom
 * is a good reason to not read your entire data unit and it was hiding bugs.
 *
 * @returns VBox status code.
 * @param   pSSM                The saved state handle.
 */
VMMR3DECL(int) SSMR3SkipToEndOfUnit(PSSMHANDLE pSSM)
{
    SSM_ASSERT_READABLE_RET(pSSM);
    SSM_CHECK_CANCELLED_RET(pSSM);
    if (pSSM->u.Read.uFmtVerMajor >= 2)
    {
        /*
         * Read until we the end of data condition is raised.
         */
        pSSM->u.Read.cbDataBuffer  = 0;
        pSSM->u.Read.offDataBuffer = 0;
        if (!pSSM->u.Read.fEndOfData)
        {
            do
            {
                /* read the rest of the current record */
                while (pSSM->u.Read.cbRecLeft)
                {
                    uint8_t  abBuf[8192];
                    uint32_t cbToRead = RT_MIN(pSSM->u.Read.cbRecLeft, sizeof(abBuf));
                    int rc = ssmR3DataReadV2Raw(pSSM, abBuf, cbToRead);
                    if (RT_FAILURE(rc))
                        return pSSM->rc = rc;
                    pSSM->u.Read.cbRecLeft -= cbToRead;
                }

                /* read the next header. */
                int rc = ssmR3DataReadRecHdrV2(pSSM);
                if (RT_FAILURE(rc))
                    return pSSM->rc = rc;
            } while (!pSSM->u.Read.fEndOfData);
        }
    }
    /* else: Doesn't matter for the version 1 loading. */

    return VINF_SUCCESS;
}


/**
 * Calculate the checksum of a file portion.
 *
 * @returns VBox status code.
 * @param   pStrm       The stream handle
 * @param   off         Where to start checksumming.
 * @param   cb          How much to checksum.
 * @param   pu32CRC     Where to store the calculated checksum.
 */
static int ssmR3CalcChecksum(PSSMSTRM pStrm, uint64_t off, uint64_t cb, uint32_t *pu32CRC)
{
    /*
     * Allocate a buffer.
     */
    const size_t cbBuf = _32K;
    void *pvBuf = RTMemTmpAlloc(cbBuf);
    if (!pvBuf)
        return VERR_NO_TMP_MEMORY;

    /*
     * Loop reading and calculating CRC32.
     */
    int         rc     = VINF_SUCCESS;
    uint32_t    u32CRC = RTCrc32Start();
    while (cb > 0)
    {
        /* read chunk */
        size_t cbToRead = cbBuf;
        if (cb < cbBuf)
            cbToRead = cb;
        rc = ssmR3StrmPeekAt(pStrm, off, pvBuf, cbToRead, NULL);
        if (RT_FAILURE(rc))
        {
            AssertMsgFailed(("Failed with rc=%Rrc while calculating crc.\n", rc));
            RTMemTmpFree(pvBuf);
            return rc;
        }

        /* advance */
        cb  -= cbToRead;
        off += cbToRead;

        /* calc crc32. */
        u32CRC = RTCrc32Process(u32CRC, pvBuf, cbToRead);
    }
    RTMemTmpFree(pvBuf);

    /* store the calculated crc */
    u32CRC = RTCrc32Finish(u32CRC);
    Log(("SSM: u32CRC=0x%08x\n", u32CRC));
    *pu32CRC = u32CRC;

    return VINF_SUCCESS;
}


/**
 * Validates a version 2 footer.
 *
 * @returns VBox status code.
 *
 * @param   pFooter             The footer.
 * @param   offFooter           The stream offset of the footer.
 * @param   cDirEntries         The number of directory entries. UINT32_MAX if
 *                              unknown.
 * @param   fStreamCrc32        Whether the stream is checksummed using CRC-32.
 * @param   u32StreamCRC        The stream checksum.
 */
static int ssmR3ValidateFooter(PSSMFILEFTR pFooter, uint64_t offFooter, uint32_t cDirEntries, bool fStreamCrc32, uint32_t u32StreamCRC)
{
    if (memcmp(pFooter->szMagic, SSMFILEFTR_MAGIC, sizeof(pFooter->szMagic)))
    {
        LogRel(("SSM: Bad footer magic: %.*Rhxs\n", sizeof(pFooter->szMagic), &pFooter->szMagic[0]));
        return VERR_SSM_INTEGRITY_FOOTER;
    }
    SSM_CHECK_CRC32_RET(pFooter, sizeof(*pFooter), ("Footer CRC mismatch: %08x, correct is %08x\n", u32CRC, u32ActualCRC));
    if (pFooter->offStream != offFooter)
    {
        LogRel(("SSM: SSMFILEFTR::offStream is wrong: %llx, expected %llx\n", pFooter->offStream, offFooter));
        return VERR_SSM_INTEGRITY_FOOTER;
    }
    if (pFooter->u32Reserved)
    {
        LogRel(("SSM: Reserved footer field isn't zero: %08x\n", pFooter->u32Reserved));
        return VERR_SSM_INTEGRITY_FOOTER;
    }
    if (cDirEntries != UINT32_MAX)
        AssertLogRelMsgReturn(pFooter->cDirEntries == cDirEntries,
                              ("Footer: cDirEntries=%#x, expected %#x\n", pFooter->cDirEntries, cDirEntries),
                              VERR_SSM_INTEGRITY_FOOTER);
    else
        AssertLogRelMsgReturn(pFooter->cDirEntries < _64K,
                              ("Footer: cDirEntries=%#x\n", pFooter->cDirEntries),
                              VERR_SSM_INTEGRITY_FOOTER);
    if (    !fStreamCrc32
        &&  pFooter->u32StreamCRC)
    {
        LogRel(("SSM: u32StreamCRC field isn't zero, but header says stream checksumming is disabled.\n"));
        return VERR_SSM_INTEGRITY_FOOTER;
    }
    if (    fStreamCrc32
        &&  pFooter->u32StreamCRC != u32StreamCRC)
    {
        LogRel(("SSM: Bad stream CRC: %#x, expected %#x.\n", pFooter->u32StreamCRC, u32StreamCRC));
        return VERR_SSM_INTEGRITY_CRC;
    }
    return VINF_SUCCESS;
}


/**
 * Validates the header information stored in the handle.
 *
 * @returns VBox status code.
 *
 * @param   pSSM            The handle.
 * @param   fHaveHostBits   Set if the host bits field is valid.
 * @param   fHaveVersion    Set if we have a version.
 */
static int ssmR3ValidateHeaderInfo(PSSMHANDLE pSSM, bool fHaveHostBits, bool fHaveVersion)
{
    Assert(pSSM->u.Read.cbFileHdr < 256 && pSSM->u.Read.cbFileHdr > 32);
    Assert(pSSM->u.Read.uFmtVerMajor == 1 || pSSM->u.Read.uFmtVerMajor == 2);
    Assert(pSSM->u.Read.uFmtVerMinor <= 2);

    if (fHaveVersion)
    {
        if (    pSSM->u.Read.u16VerMajor == 0
            ||  pSSM->u.Read.u16VerMajor > 1000
            ||  pSSM->u.Read.u16VerMinor > 1000
            ||  pSSM->u.Read.u32VerBuild > _1M
            ||  pSSM->u.Read.u32SvnRev == 0
            ||  pSSM->u.Read.u32SvnRev > 10000000 /*100M*/)
        {
            LogRel(("SSM: Incorrect version values: %u.%u.%u.r%u\n",
                    pSSM->u.Read.u16VerMajor, pSSM->u.Read.u16VerMinor, pSSM->u.Read.u32VerBuild, pSSM->u.Read.u32SvnRev));
            return VERR_SSM_INTEGRITY_VBOX_VERSION;
        }
    }
    else
        AssertLogRelReturn(   pSSM->u.Read.u16VerMajor == 0
                           && pSSM->u.Read.u16VerMinor == 0
                           && pSSM->u.Read.u32VerBuild == 0
                           && pSSM->u.Read.u32SvnRev   == 0,
                           VERR_SSM_INTEGRITY_VBOX_VERSION);

    if (fHaveHostBits)
    {
        if (    pSSM->u.Read.cHostBits != 32
            &&  pSSM->u.Read.cHostBits != 64)
        {
            LogRel(("SSM: Incorrect cHostBits value: %u\n", pSSM->u.Read.cHostBits));
            return VERR_SSM_INTEGRITY_HEADER;
        }
    }
    else
        AssertLogRelReturn(pSSM->u.Read.cHostBits == 0, VERR_SSM_INTEGRITY_HEADER);

    if (    pSSM->u.Read.cbGCPhys != sizeof(uint32_t)
        &&  pSSM->u.Read.cbGCPhys != sizeof(uint64_t))
    {
        LogRel(("SSM: Incorrect cbGCPhys value: %d\n", pSSM->u.Read.cbGCPhys));
        return VERR_SSM_INTEGRITY_HEADER;
    }
    if (    pSSM->u.Read.cbGCPtr != sizeof(uint32_t)
        &&  pSSM->u.Read.cbGCPtr != sizeof(uint64_t))
    {
        LogRel(("SSM: Incorrect cbGCPtr value: %d\n", pSSM->u.Read.cbGCPtr));
        return VERR_SSM_INTEGRITY_HEADER;
    }

    return VINF_SUCCESS;
}


/**
 * Reads the header, detects the format version and performs integrity
 * validations.
 *
 * @returns VBox status code.
 * @param   pSSM                The saved state handle.  A number of field will
 *                              be updated, mostly header related information.
 *                              fLiveSave is also set if appropriate.
 * @param   fChecksumIt         Whether to checksum the file or not.  This will
 *                              be ignored if it the stream isn't a file.
 * @param   fChecksumOnRead     Whether to validate the checksum while reading
 *                              the stream instead of up front. If not possible,
 *                              verify the checksum up front.
 */
static int ssmR3HeaderAndValidate(PSSMHANDLE pSSM, bool fChecksumIt, bool fChecksumOnRead)
{
    /*
     * Read and check the header magic.
     */
    union
    {
        SSMFILEHDR          v2_0;
        SSMFILEHDRV12       v1_2;
        SSMFILEHDRV11       v1_1;
    } uHdr;
    int rc = ssmR3StrmRead(&pSSM->Strm, &uHdr, sizeof(uHdr.v2_0.szMagic));
    if (RT_FAILURE(rc))
    {
        LogRel(("SSM: Failed to read file magic header. rc=%Rrc\n", rc));
        return rc;
    }
    if (memcmp(uHdr.v2_0.szMagic, SSMFILEHDR_MAGIC_BASE, sizeof(SSMFILEHDR_MAGIC_BASE) - 1))
    {
        Log(("SSM: Not a saved state file. magic=%.*s\n", sizeof(uHdr.v2_0.szMagic) - 1, uHdr.v2_0.szMagic));
        return VERR_SSM_INTEGRITY_MAGIC;
    }

    /*
     * Find the header size and read the rest.
     */
    static const struct
    {
        char        szMagic[sizeof(SSMFILEHDR_MAGIC_V2_0)];
        uint32_t    cbHdr;
        unsigned    uFmtVerMajor;
        unsigned    uFmtVerMinor;
    }   s_aVers[] =
    {
        { SSMFILEHDR_MAGIC_V2_0, sizeof(SSMFILEHDR),    2, 0 },
        { SSMFILEHDR_MAGIC_V1_2, sizeof(SSMFILEHDRV12), 1, 2 },
        { SSMFILEHDR_MAGIC_V1_1, sizeof(SSMFILEHDRV11), 1, 1 },
    };
    int iVer = RT_ELEMENTS(s_aVers);
    while (iVer-- > 0)
        if (!memcmp(uHdr.v2_0.szMagic, s_aVers[iVer].szMagic, sizeof(uHdr.v2_0.szMagic)))
            break;
    if (iVer < 0)
    {
        Log(("SSM: Unknown file format version. magic=%.*s\n", sizeof(uHdr.v2_0.szMagic) - 1, uHdr.v2_0.szMagic));
        return VERR_SSM_INTEGRITY_VERSION;
    }
    pSSM->u.Read.uFmtVerMajor   = s_aVers[iVer].uFmtVerMajor;
    pSSM->u.Read.uFmtVerMinor   = s_aVers[iVer].uFmtVerMinor;
    pSSM->u.Read.cbFileHdr      = s_aVers[iVer].cbHdr;

    rc = ssmR3StrmRead(&pSSM->Strm, (uint8_t *)&uHdr + sizeof(uHdr.v2_0.szMagic), pSSM->u.Read.cbFileHdr - sizeof(uHdr.v2_0.szMagic));
    if (RT_FAILURE(rc))
    {
        LogRel(("SSM: Failed to read the file header. rc=%Rrc\n", rc));
        return rc;
    }

    /*
     * Make version specific adjustments.
     */
    if (pSSM->u.Read.uFmtVerMajor >= 2)
    {
        /*
         * Version 2.0 and later.
         */
        if (pSSM->u.Read.uFmtVerMinor == 0)
        {
            /* validate the header. */
            SSM_CHECK_CRC32_RET(&uHdr.v2_0, sizeof(uHdr.v2_0), ("Header CRC mismatch: %08x, correct is %08x\n", u32CRC, u32ActualCRC));
            if (uHdr.v2_0.u8Reserved)
            {
                LogRel(("SSM: Reserved header field isn't zero: %02x\n", uHdr.v2_0.u8Reserved));
                return VERR_SSM_INTEGRITY;
            }
            if (uHdr.v2_0.fFlags & ~(SSMFILEHDR_FLAGS_STREAM_CRC32 | SSMFILEHDR_FLAGS_STREAM_LIVE_SAVE))
            {
                LogRel(("SSM: Unknown header flags: %08x\n", uHdr.v2_0.fFlags));
                return VERR_SSM_INTEGRITY;
            }
            if (    uHdr.v2_0.cbMaxDecompr > sizeof(pSSM->u.Read.abDataBuffer)
                ||  uHdr.v2_0.cbMaxDecompr < _1K
                ||  (uHdr.v2_0.cbMaxDecompr & 0xff) != 0)
            {
                LogRel(("SSM: The cbMaxDecompr header field is out of range: %#x\n", uHdr.v2_0.cbMaxDecompr));
                return VERR_SSM_INTEGRITY;
            }

            /* set the header info. */
            pSSM->u.Read.cHostBits      = uHdr.v2_0.cHostBits;
            pSSM->u.Read.u16VerMajor    = uHdr.v2_0.u16VerMajor;
            pSSM->u.Read.u16VerMinor    = uHdr.v2_0.u16VerMinor;
            pSSM->u.Read.u32VerBuild    = uHdr.v2_0.u32VerBuild;
            pSSM->u.Read.u32SvnRev      = uHdr.v2_0.u32SvnRev;
            pSSM->u.Read.cbGCPhys       = uHdr.v2_0.cbGCPhys;
            pSSM->u.Read.cbGCPtr        = uHdr.v2_0.cbGCPtr;
            pSSM->u.Read.fFixedGCPtrSize= true;
            pSSM->u.Read.fStreamCrc32   = !!(uHdr.v2_0.fFlags & SSMFILEHDR_FLAGS_STREAM_CRC32);
            pSSM->fLiveSave             = !!(uHdr.v2_0.fFlags & SSMFILEHDR_FLAGS_STREAM_LIVE_SAVE);
        }
        else
            AssertFailedReturn(VERR_SSM_IPE_2);
        if (!pSSM->u.Read.fStreamCrc32)
            ssmR3StrmDisableChecksumming(&pSSM->Strm);

        /*
         * Read and validate the footer if it's a file.
         */
        if (ssmR3StrmIsFile(&pSSM->Strm))
        {
            SSMFILEFTR  Footer;
            uint64_t    offFooter;
            rc = ssmR3StrmPeekAt(&pSSM->Strm, -(RTFOFF)sizeof(SSMFILEFTR), &Footer, sizeof(Footer), &offFooter);
            AssertLogRelRCReturn(rc, rc);

            rc = ssmR3ValidateFooter(&Footer, offFooter, UINT32_MAX, pSSM->u.Read.fStreamCrc32, Footer.u32StreamCRC);
            if (RT_FAILURE(rc))
                return rc;

            pSSM->u.Read.cbLoadFile = offFooter + sizeof(Footer);
            pSSM->u.Read.u32LoadCRC = Footer.u32StreamCRC;
        }
        else
        {
            pSSM->u.Read.cbLoadFile = UINT64_MAX;
            pSSM->u.Read.u32LoadCRC = 0;
        }

        /*
         * Validate the header info we've set in the handle.
         */
        rc = ssmR3ValidateHeaderInfo(pSSM, true /*fHaveHostBits*/, true /*fHaveVersion*/);
        if (RT_FAILURE(rc))
            return rc;

        /*
         * Check the checksum if that's called for and possible.
         */
        if (    pSSM->u.Read.fStreamCrc32
            &&  fChecksumIt
            &&  !fChecksumOnRead
            &&  ssmR3StrmIsFile(&pSSM->Strm))
        {
            uint32_t u32CRC;
            rc = ssmR3CalcChecksum(&pSSM->Strm, 0, pSSM->u.Read.cbLoadFile - sizeof(SSMFILEFTR), &u32CRC);
            if (RT_FAILURE(rc))
                return rc;
            if (u32CRC != pSSM->u.Read.u32LoadCRC)
            {
                LogRel(("SSM: Invalid CRC! Calculated %#010x, in footer %#010x\n", u32CRC, pSSM->u.Read.u32LoadCRC));
                return VERR_SSM_INTEGRITY_CRC;
            }
        }
    }
    else
    {
        /*
         * Version 1.x of the format.
         */
        bool        fHaveHostBits = true;
        bool        fHaveVersion  = false;
        RTUUID      MachineUuidFromHdr;

        ssmR3StrmDisableChecksumming(&pSSM->Strm);
        if (pSSM->u.Read.uFmtVerMinor == 1)
        {
            pSSM->u.Read.cHostBits      = 0; /* unknown */
            pSSM->u.Read.u16VerMajor    = 0;
            pSSM->u.Read.u16VerMinor    = 0;
            pSSM->u.Read.u32VerBuild    = 0;
            pSSM->u.Read.u32SvnRev      = 0;
            pSSM->u.Read.cbLoadFile     = uHdr.v1_1.cbFile;
            pSSM->u.Read.u32LoadCRC     = uHdr.v1_1.u32CRC;
            pSSM->u.Read.cbGCPhys       = sizeof(RTGCPHYS);
            pSSM->u.Read.cbGCPtr        = sizeof(RTGCPTR);
            pSSM->u.Read.fFixedGCPtrSize = false; /* settable */
            pSSM->u.Read.fStreamCrc32   = false;

            MachineUuidFromHdr  = uHdr.v1_1.MachineUuid;
            fHaveHostBits       = false;
        }
        else if (pSSM->u.Read.uFmtVerMinor == 2)
        {
            pSSM->u.Read.cHostBits      = uHdr.v1_2.cHostBits;
            pSSM->u.Read.u16VerMajor    = uHdr.v1_2.u16VerMajor;
            pSSM->u.Read.u16VerMinor    = uHdr.v1_2.u16VerMinor;
            pSSM->u.Read.u32VerBuild    = uHdr.v1_2.u32VerBuild;
            pSSM->u.Read.u32SvnRev      = uHdr.v1_2.u32SvnRev;
            pSSM->u.Read.cbLoadFile     = uHdr.v1_2.cbFile;
            pSSM->u.Read.u32LoadCRC     = uHdr.v1_2.u32CRC;
            pSSM->u.Read.cbGCPhys       = uHdr.v1_2.cbGCPhys;
            pSSM->u.Read.cbGCPtr        = uHdr.v1_2.cbGCPtr;
            pSSM->u.Read.fFixedGCPtrSize = true;
            pSSM->u.Read.fStreamCrc32   = false;

            MachineUuidFromHdr  = uHdr.v1_2.MachineUuid;
            fHaveVersion        = true;
        }
        else
            AssertFailedReturn(VERR_SSM_IPE_1);

        /*
         * The MachineUuid must be NULL (was never used).
         */
        if (!RTUuidIsNull(&MachineUuidFromHdr))
        {
            LogRel(("SSM: The UUID of the saved state doesn't match the running VM.\n"));
            return VERR_SMM_INTEGRITY_MACHINE;
        }

        /*
         * Verify the file size.
         */
        uint64_t cbFile = ssmR3StrmGetSize(&pSSM->Strm);
        if (cbFile != pSSM->u.Read.cbLoadFile)
        {
            LogRel(("SSM: File size mismatch. hdr.cbFile=%lld actual %lld\n", pSSM->u.Read.cbLoadFile, cbFile));
            return VERR_SSM_INTEGRITY_SIZE;
        }

        /*
         * Validate the header info we've set in the handle.
         */
        rc = ssmR3ValidateHeaderInfo(pSSM, fHaveHostBits, fHaveVersion);
        if (RT_FAILURE(rc))
            return rc;

        /*
         * Verify the checksum if requested.
         *
         * Note! The checksum is not actually generated for the whole file,
         *       this is of course a bug in the v1.x code that we cannot do
         *       anything about.
         */
        if (    fChecksumIt
            ||  fChecksumOnRead)
        {
            uint32_t u32CRC;
            rc = ssmR3CalcChecksum(&pSSM->Strm,
                                   RT_UOFFSETOF(SSMFILEHDRV11, u32CRC) + sizeof(uHdr.v1_1.u32CRC),
                                   cbFile - pSSM->u.Read.cbFileHdr,
                                   &u32CRC);
            if (RT_FAILURE(rc))
                return rc;
            if (u32CRC != pSSM->u.Read.u32LoadCRC)
            {
                LogRel(("SSM: Invalid CRC! Calculated %#010x, in header %#010x\n", u32CRC, pSSM->u.Read.u32LoadCRC));
                return VERR_SSM_INTEGRITY_CRC;
            }
        }
    }

    return VINF_SUCCESS;
}


/**
 * Open a saved state for reading.
 *
 * The file will be positioned at the first data unit upon successful return.
 *
 * @returns VBox status code.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   pszFilename         The filename. NULL if pStreamOps is used.
 * @param   pStreamOps          The stream method table. NULL if pszFilename is
 *                              used.
 * @param   pvUser              The user argument to the stream methods.
 * @param   fChecksumIt         Check the checksum for the entire file.
 * @param   fChecksumOnRead     Whether to validate the checksum while reading
 *                              the stream instead of up front. If not possible,
 *                              verify the checksum up front.
 * @param   pSSM                Pointer to the handle structure. This will be
 *                              completely initialized on success.
 * @param   cBuffers            The number of stream buffers.
 */
static int ssmR3OpenFile(PVM pVM, const char *pszFilename, PCSSMSTRMOPS pStreamOps, void *pvUser,
                         bool fChecksumIt, bool fChecksumOnRead, uint32_t cBuffers, PSSMHANDLE pSSM)
{
    /*
     * Initialize the handle.
     */
    pSSM->pVM                   = pVM;
    pSSM->enmOp                 = SSMSTATE_INVALID;
    pSSM->enmAfter              = SSMAFTER_INVALID;
    pSSM->fCancelled            = SSMHANDLE_OK;
    pSSM->rc                    = VINF_SUCCESS;
    pSSM->cbUnitLeftV1          = 0;
    pSSM->offUnit               = UINT64_MAX;
    pSSM->offUnitUser           = UINT64_MAX;
    pSSM->fLiveSave             = false;
    pSSM->pfnProgress           = NULL;
    pSSM->pvUser                = NULL;
    pSSM->uPercent              = 0;
    pSSM->offEstProgress        = 0;
    pSSM->cbEstTotal            = 0;
    pSSM->offEst                = 0;
    pSSM->offEstUnitEnd         = 0;
    pSSM->uPercentLive          = 0;
    pSSM->uPercentPrepare       = 5;
    pSSM->uPercentDone          = 2;
    pSSM->uReportedLivePercent  = 0;
    pSSM->pszFilename           = pszFilename;

    pSSM->u.Read.pZipDecompV1   = NULL;
    pSSM->u.Read.uFmtVerMajor   = UINT32_MAX;
    pSSM->u.Read.uFmtVerMinor   = UINT32_MAX;
    pSSM->u.Read.cbFileHdr      = UINT32_MAX;
    pSSM->u.Read.cbGCPhys       = UINT8_MAX;
    pSSM->u.Read.cbGCPtr        = UINT8_MAX;
    pSSM->u.Read.fFixedGCPtrSize= false;
    pSSM->u.Read.fIsHostMsc32   = SSM_HOST_IS_MSC_32;
    RT_ZERO(pSSM->u.Read.szHostOSAndArch);
    pSSM->u.Read.u16VerMajor    = UINT16_MAX;
    pSSM->u.Read.u16VerMinor    = UINT16_MAX;
    pSSM->u.Read.u32VerBuild    = UINT32_MAX;
    pSSM->u.Read.u32SvnRev      = UINT32_MAX;
    pSSM->u.Read.cHostBits      = UINT8_MAX;
    pSSM->u.Read.cbLoadFile     = UINT64_MAX;

    pSSM->u.Read.cbRecLeft      = 0;
    pSSM->u.Read.cbDataBuffer   = 0;
    pSSM->u.Read.offDataBuffer  = 0;
    pSSM->u.Read.fEndOfData     = 0;
    pSSM->u.Read.u8TypeAndFlags = 0;

    pSSM->u.Read.pCurUnit       = NULL;
    pSSM->u.Read.uCurUnitVer    = UINT32_MAX;
    pSSM->u.Read.uCurUnitPass   = 0;
    pSSM->u.Read.fHaveSetError  = false;

    /*
     * Try open and validate the file.
     */
    int rc;
    if (pStreamOps)
        rc = ssmR3StrmInit(&pSSM->Strm, pStreamOps, pvUser, false /*fWrite*/, fChecksumOnRead, cBuffers);
    else
        rc = ssmR3StrmOpenFile(&pSSM->Strm, pszFilename, false /*fWrite*/, fChecksumOnRead, cBuffers);
    if (RT_SUCCESS(rc))
    {
        rc = ssmR3HeaderAndValidate(pSSM, fChecksumIt, fChecksumOnRead);
        if (RT_SUCCESS(rc))
            return rc;

        /* failure path */
        ssmR3StrmClose(&pSSM->Strm, pSSM->rc == VERR_SSM_CANCELLED);
    }
    else
        Log(("SSM: Failed to open save state file '%s', rc=%Rrc.\n",  pszFilename, rc));
    return rc;
}


/**
 * Verifies the directory.
 *
 * @returns VBox status code.
 *
 * @param   pDir        The full directory.
 * @param   cbDir       The size of the directory.
 * @param   offDir      The directory stream offset.
 * @param   cDirEntries The directory entry count from the footer.
 * @param   cbHdr       The header size.
 * @param   uSvnRev     The SVN revision that saved the state. Bug detection.
 */
static int ssmR3ValidateDirectory(PSSMFILEDIR pDir, size_t cbDir, uint64_t offDir, uint32_t cDirEntries,
                                  uint32_t cbHdr, uint32_t uSvnRev)
{
    AssertLogRelReturn(!memcmp(pDir->szMagic, SSMFILEDIR_MAGIC, sizeof(pDir->szMagic)), VERR_SSM_INTEGRITY_DIR_MAGIC);
    SSM_CHECK_CRC32_RET(pDir, cbDir, ("Bad directory CRC: %08x, actual %08x\n", u32CRC, u32ActualCRC));
    AssertLogRelMsgReturn(pDir->cEntries == cDirEntries,
                          ("Bad directory entry count: %#x, expected %#x (from the footer)\n", pDir->cEntries, cDirEntries),
                           VERR_SSM_INTEGRITY_DIR);
    AssertLogRelReturn(RT_UOFFSETOF_DYN(SSMFILEDIR, aEntries[pDir->cEntries]) == cbDir, VERR_SSM_INTEGRITY_DIR);

    for (uint32_t i = 0; i < pDir->cEntries; i++)
    {
        AssertLogRelMsgReturn(  (   pDir->aEntries[i].off >= cbHdr
                                 && pDir->aEntries[i].off <  offDir)
                              || (   pDir->aEntries[i].off == 0 /* bug in unreleased code */
                                  && uSvnRev < 53365),
                              ("off=%#llx cbHdr=%#x offDir=%#llx\n", pDir->aEntries[i].off, cbHdr, offDir),
                              VERR_SSM_INTEGRITY_DIR);
    }
    return VINF_SUCCESS;
}

#ifndef SSM_STANDALONE

/**
 * LogRel the unit content.
 *
 * @param   pSSM                The save state handle.
 * @param   pUnitHdr            The unit head (for cbName).
 * @param   offUnit             The offset of the unit header.
 * @param   offStart            Where to start.
 * @param   offEnd              Where to end.
 */
static void ssmR3StrmLogUnitContent(PSSMHANDLE pSSM, SSMFILEUNITHDRV2 const *pUnitHdr, uint64_t offUnit,
                                    uint64_t offStart, uint64_t offEnd)
{
    /*
     * Stop the I/O thread (if present).
     */
    ssmR3StrmStopIoThread(&pSSM->Strm);

    /*
     * Save the current status, resetting it so we can read + log the unit bytes.
     */
    int rcSaved = pSSM->rc;
    pSSM->rc = VINF_SUCCESS;

    /*
     * Reverse back to the start of the unit if we can.
     */
    uint32_t cbUnitHdr = RT_UOFFSETOF_DYN(SSMFILEUNITHDRV2, szName[pUnitHdr->cbName]);
    int rc = ssmR3StrmSeek(&pSSM->Strm, offUnit/* + cbUnitHdr*/, RTFILE_SEEK_BEGIN, pUnitHdr->u32CurStreamCRC);
    if (RT_SUCCESS(rc))
    {
        SSMFILEUNITHDRV2 UnitHdr2;
        rc = ssmR3StrmRead(&pSSM->Strm, &UnitHdr2, cbUnitHdr);
        if (   RT_SUCCESS(rc)
            && memcmp(&UnitHdr2, pUnitHdr, cbUnitHdr) == 0)
        {
            pSSM->u.Read.cbDataBuffer = 0; /* avoid assertions */
            pSSM->u.Read.cbRecLeft    = 0;
            ssmR3DataReadBeginV2(pSSM);

            /*
             * Read the unit, dumping the requested bits.
             */
            uint8_t     cbLine = 0;
            uint8_t     abLine[16];
            uint64_t    offCur = 0;
            offStart &= ~(uint64_t)(sizeof(abLine) - 1);
            Assert(offStart < offEnd);
            LogRel(("SSM: Unit '%s' contents:\n", pUnitHdr->szName));

            do
            {
                /*
                 * Read the next 16 bytes into abLine.  We have to take some care to
                 * get all the bytes in the unit, since we don't really know its size.
                 */
                while (   cbLine < sizeof(abLine)
                       && !pSSM->u.Read.fEndOfData
                       && RT_SUCCESS(pSSM->rc))
                {
                    uint32_t cbToRead = sizeof(abLine) - cbLine;
                    if (cbToRead > 1)
                    {
                        int32_t cbInBuffer = pSSM->u.Read.cbDataBuffer - pSSM->u.Read.offDataBuffer;
                        if ((int32_t)cbToRead > cbInBuffer)
                        {
                            if (cbInBuffer > 0)
                                cbToRead = cbInBuffer;
                            else if (pSSM->u.Read.cbRecLeft)
                                cbToRead = 1;
                            else
                            {
                                rc = ssmR3DataReadRecHdrV2(pSSM);
                                if (RT_FAILURE(rc))
                                {
                                    pSSM->rc = rc;
                                    break;
                                }
                                if (pSSM->u.Read.fEndOfData)
                                    break;
                            }
                        }
                    }
                    rc = ssmR3DataRead(pSSM, &abLine[cbLine], cbToRead);
                    if (RT_SUCCESS(rc))
                        cbLine += cbToRead;
                    else
                        break;
                }

                /*
                 * Display the bytes if in the requested range.
                 */
                if (   offCur >= offStart
                    && offCur <= offEnd)
                {
                    char    szLine[132];
                    char   *pchDst = szLine;
                    uint8_t offSrc = 0;
                    while (offSrc < cbLine)
                    {
                        static char const s_szHex[17] = "0123456789abcdef";
                        uint8_t const b = abLine[offSrc++];
                        *pchDst++ = s_szHex[b >> 4];
                        *pchDst++ = s_szHex[b & 0xf];
                        *pchDst++ = offSrc != 8 ? ' ' : '-';
                    }
                    while (offSrc < sizeof(abLine))
                    {
                        *pchDst++ = ' ';
                        *pchDst++ = ' ';
                        *pchDst++ = offSrc != 7 ? ' ' : '-';
                        offSrc++;
                    }
                    *pchDst++ = ' ';

                    offSrc = 0;
                    while (offSrc < cbLine)
                    {
                        char const ch = (int8_t)abLine[offSrc++];
                        if (ch < 0x20 || ch >= 0x7f)
                            *pchDst++ = '.';
                        else
                            *pchDst++ = ch;
                    }
                    *pchDst = '\0';
                    Assert((uintptr_t)(pchDst - &szLine[0]) < sizeof(szLine));
                    Assert(strchr(szLine, '\0') == pchDst);

                    LogRel(("%#010llx: %s\n", offCur, szLine));
                }
                offCur += cbLine;
                cbLine = 0;
            } while (   !pSSM->u.Read.fEndOfData
                     && RT_SUCCESS(pSSM->rc));
            LogRel(("SSM: offCur=%#llx fEndOfData=%d (rc=%Rrc)\n", offCur, pSSM->u.Read.fEndOfData, rc));
        }
        else if (RT_SUCCESS(rc))
            LogRel(("SSM: Cannot dump unit - mismatching unit head\n"));
        else
            LogRel(("SSM: Cannot dump unit - unit header read error: %Rrc\n", rc));
    }
    else
        LogRel(("SSM: Cannot dump unit - ssmR3StrmSeek error: %Rrc\n", rc));

    pSSM->rc = rcSaved;
}


/**
 * Find a data unit by name.
 *
 * @returns Pointer to the unit.
 * @returns NULL if not found.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pszName         Data unit name.
 * @param   uInstance       The data unit instance id.
 */
static PSSMUNIT ssmR3Find(PVM pVM, const char *pszName, uint32_t uInstance)
{
    size_t   cchName = strlen(pszName);
    PSSMUNIT pUnit = pVM->ssm.s.pHead;
    while (     pUnit
           &&   (   pUnit->u32Instance != uInstance
                 || pUnit->cchName != cchName
                 || memcmp(pUnit->szName, pszName, cchName)))
        pUnit = pUnit->pNext;
    return pUnit;
}


/**
 * Executes the loading of a V1.X file.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pSSM                The saved state handle.
 */
static int ssmR3LoadExecV1(PVM pVM, PSSMHANDLE pSSM)
{
    int     rc;
    char   *pszName = NULL;
    size_t  cchName = 0;
    pSSM->enmOp = SSMSTATE_LOAD_EXEC;
    for (;;)
    {
        /*
         * Save the current file position and read the data unit header.
         */
        uint64_t         offUnit = ssmR3StrmTell(&pSSM->Strm);
        SSMFILEUNITHDRV1 UnitHdr;
        rc = ssmR3StrmRead(&pSSM->Strm, &UnitHdr, RT_UOFFSETOF(SSMFILEUNITHDRV1, szName));
        if (RT_SUCCESS(rc))
        {
            /*
             * Check the magic and see if it's valid and whether it is a end header or not.
             */
            if (memcmp(&UnitHdr.achMagic[0], SSMFILEUNITHDR_MAGIC, sizeof(SSMFILEUNITHDR_MAGIC)))
            {
                if (!memcmp(&UnitHdr.achMagic[0], SSMFILEUNITHDR_END, sizeof(SSMFILEUNITHDR_END)))
                {
                    Log(("SSM: EndOfFile: offset %#9llx size %9d\n", offUnit, UnitHdr.cbUnit));
                    /* Complete the progress bar (pending 99% afterwards). */
                    ssmR3ProgressByByte(pSSM, pSSM->cbEstTotal - pSSM->offEst);
                    break;
                }
                LogRel(("SSM: Invalid unit magic at offset %#llx (%lld), '%.*s'!\n",
                        offUnit, offUnit, sizeof(UnitHdr.achMagic) - 1, &UnitHdr.achMagic[0]));
                rc = VERR_SSM_INTEGRITY_UNIT_MAGIC;
                break;
            }

            /*
             * Read the name.
             * Adjust the name buffer first.
             */
            if (cchName < UnitHdr.cchName)
            {
                if (pszName)
                    RTMemTmpFree(pszName);
                cchName = RT_ALIGN_Z(UnitHdr.cchName, 64);
                pszName = (char *)RTMemTmpAlloc(cchName);
            }
            if (pszName)
            {
                rc = ssmR3StrmRead(&pSSM->Strm, pszName, UnitHdr.cchName);
                if (RT_SUCCESS(rc))
                {
                    if (pszName[UnitHdr.cchName - 1])
                    {
                        LogRel(("SSM: Unit name '%.*s' was not properly terminated.\n", UnitHdr.cchName, pszName));
                        rc = VERR_SSM_INTEGRITY_UNIT;
                        break;
                    }
                    Log(("SSM: Data unit: offset %#9llx size %9lld '%s'\n", offUnit, UnitHdr.cbUnit, pszName));

                    /*
                     * Find the data unit in our internal table.
                     */
                    PSSMUNIT pUnit = ssmR3Find(pVM, pszName, UnitHdr.u32Instance);
                    if (pUnit)
                    {
                        /*
                         * Call the execute handler.
                         */
                        pSSM->cbUnitLeftV1 = UnitHdr.cbUnit - RT_UOFFSETOF_DYN(SSMFILEUNITHDRV1, szName[UnitHdr.cchName]);
                        pSSM->offUnit      = 0;
                        pSSM->offUnitUser  = 0;
                        pSSM->u.Read.uCurUnitVer  = UnitHdr.u32Version;
                        pSSM->u.Read.uCurUnitPass = SSM_PASS_FINAL;
                        pSSM->u.Read.pCurUnit     = pUnit;
                        if (!pUnit->u.Common.pfnLoadExec)
                        {
                            LogRel(("SSM: No load exec callback for unit '%s'!\n", pszName));
                            pSSM->rc = rc = VERR_SSM_NO_LOAD_EXEC;
                            break;
                        }
                        ssmR3UnitCritSectEnter(pVM, pUnit);
                        switch (pUnit->enmType)
                        {
                            case SSMUNITTYPE_DEV:
                                rc = pUnit->u.Dev.pfnLoadExec(pUnit->u.Dev.pDevIns, pSSM, UnitHdr.u32Version, SSM_PASS_FINAL);
                                break;
                            case SSMUNITTYPE_DRV:
                                rc = pUnit->u.Drv.pfnLoadExec(pUnit->u.Drv.pDrvIns, pSSM, UnitHdr.u32Version, SSM_PASS_FINAL);
                                break;
                            case SSMUNITTYPE_USB:
                                rc = pUnit->u.Usb.pfnLoadExec(pUnit->u.Usb.pUsbIns, pSSM, UnitHdr.u32Version, SSM_PASS_FINAL);
                                break;
                            case SSMUNITTYPE_INTERNAL:
                                rc = pUnit->u.Internal.pfnLoadExec(pVM, pSSM, UnitHdr.u32Version, SSM_PASS_FINAL);
                                break;
                            case SSMUNITTYPE_EXTERNAL:
                                rc = pUnit->u.External.pfnLoadExec(pSSM, VMMR3GetVTable(), pUnit->u.External.pvUser,
                                                                   UnitHdr.u32Version, SSM_PASS_FINAL);
                                break;
                            default:
                                rc = VERR_SSM_IPE_1;
                                break;
                        }
                        ssmR3UnitCritSectLeave(pVM, pUnit);
                        pUnit->fCalled = true;
                        if (RT_FAILURE(rc) && RT_SUCCESS_NP(pSSM->rc))
                            pSSM->rc = rc;

                        /*
                         * Close the reader stream.
                         */
                        rc = ssmR3DataReadFinishV1(pSSM);
                        if (RT_SUCCESS(rc))
                        {
                            /*
                             * Now, we'll check the current position to see if all, or
                             * more than all, the data was read.
                             *
                             * Note! Because of buffering / compression we'll only see the
                             * really bad ones here.
                             */
                            uint64_t off = ssmR3StrmTell(&pSSM->Strm);
                            int64_t i64Diff = off - (offUnit + UnitHdr.cbUnit);
                            if (i64Diff < 0)
                            {
                                Log(("SSM: Unit '%s' left %lld bytes unread!\n", pszName, -i64Diff));
                                rc = ssmR3StrmSkipTo(&pSSM->Strm, offUnit + UnitHdr.cbUnit);
                                ssmR3ProgressByByte(pSSM, offUnit + UnitHdr.cbUnit - pSSM->offEst);
                            }
                            else if (i64Diff > 0)
                            {
                                LogRel(("SSM: Unit '%s' read %lld bytes too much!\n", pszName, i64Diff));
                                if (!ASMAtomicXchgBool(&pSSM->u.Read.fHaveSetError, true))
                                    rc = VMSetError(pVM, VERR_SSM_LOADED_TOO_MUCH, RT_SRC_POS,
                                                    N_("Unit '%s' read %lld bytes too much"), pszName, i64Diff);
                                break;
                            }

                            pSSM->offUnit     = UINT64_MAX;
                            pSSM->offUnitUser = UINT64_MAX;
                        }
                        else
                        {
                            LogRel(("SSM: Load exec failed for '%s' instance #%u ! (version %u)\n",
                                    pszName, UnitHdr.u32Instance, UnitHdr.u32Version));
                            if (!ASMAtomicXchgBool(&pSSM->u.Read.fHaveSetError, true))
                            {
                                if (rc == VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION)
                                    VMSetError(pVM, rc, RT_SRC_POS, N_("Unsupported version %u of data unit '%s' (instance #%u)"),
                                               UnitHdr.u32Version, UnitHdr.szName, UnitHdr.u32Instance);
                                else
                                    VMSetError(pVM, rc, RT_SRC_POS, N_("Load exec failed for '%s' instance #%u (version %u)"),
                                               pszName, UnitHdr.u32Instance, UnitHdr.u32Version);
                            }
                            break;
                        }

                        pSSM->u.Read.pCurUnit       = NULL;
                        pSSM->u.Read.uCurUnitVer    = UINT32_MAX;
                        pSSM->u.Read.uCurUnitPass   = 0;
                    }
                    else
                    {
                        /*
                         * SSM unit wasn't found - ignore this when loading for the debugger.
                         */
                        LogRel(("SSM: Found no handler for unit '%s'!\n", pszName));
                        rc = VERR_SSM_INTEGRITY_UNIT_NOT_FOUND;
                        if (pSSM->enmAfter != SSMAFTER_DEBUG_IT)
                            break;
                        rc = ssmR3StrmSkipTo(&pSSM->Strm, offUnit + UnitHdr.cbUnit);
                    }
                }
            }
            else
                rc = VERR_NO_TMP_MEMORY;
        }

        /*
         * I/O errors ends up here (yea, I know, very nice programming).
         */
        if (RT_FAILURE(rc))
        {
            LogRel(("SSM: I/O error. rc=%Rrc\n", rc));
            break;
        }

        /*
         * Check for cancellation.
         */
        if (RT_UNLIKELY(ASMAtomicUoReadU32(&(pSSM)->fCancelled) == SSMHANDLE_CANCELLED))
        {
            LogRel(("SSM: Cancelled!n"));
            rc = pSSM->rc;
            if (RT_SUCCESS(pSSM->rc))
                pSSM->rc = rc = VERR_SSM_CANCELLED;
            break;
        }
    }

    RTMemTmpFree(pszName);
    return rc;
}


/**
 * Reads and verifies the directory and footer.
 *
 * @returns VBox status code.
 * @param   pSSM                The saved state handle.
 */
static int ssmR3LoadDirectoryAndFooter(PSSMHANDLE pSSM)
{
    /*
     * The directory.
     *
     * Get the header containing the number of entries first.  Then read the
     * entries and pass the combined block to the validation function.
     */
    uint64_t        off      = ssmR3StrmTell(&pSSM->Strm);
    size_t const    cbDirHdr = RT_UOFFSETOF(SSMFILEDIR, aEntries);
    SSMFILEDIR      DirHdr;
    int rc = ssmR3StrmRead(&pSSM->Strm, &DirHdr, cbDirHdr);
    if (RT_FAILURE(rc))
        return rc;
    AssertLogRelMsgReturn(!memcmp(DirHdr.szMagic, SSMFILEDIR_MAGIC, sizeof(DirHdr.szMagic)),
                          ("Invalid directory magic at %#llx (%lld): %.*Rhxs\n", off, off, sizeof(DirHdr.szMagic), DirHdr.szMagic),
                          VERR_SSM_INTEGRITY_DIR_MAGIC);
    AssertLogRelMsgReturn(DirHdr.cEntries < _64K,
                          ("Too many directory entries at %#llx (%lld): %#x\n", off, off, DirHdr.cEntries),
                          VERR_SSM_INTEGRITY_DIR);

    size_t      cbDir = RT_UOFFSETOF_DYN(SSMFILEDIR, aEntries[DirHdr.cEntries]);
    PSSMFILEDIR pDir  = (PSSMFILEDIR)RTMemTmpAlloc(cbDir);
    if (!pDir)
        return VERR_NO_TMP_MEMORY;
    memcpy(pDir, &DirHdr, cbDirHdr);
    rc = ssmR3StrmRead(&pSSM->Strm, (uint8_t *)pDir + cbDirHdr, cbDir - cbDirHdr);
    if (RT_SUCCESS(rc))
        rc = ssmR3ValidateDirectory(pDir, cbDir, off, DirHdr.cEntries, pSSM->u.Read.cbFileHdr, pSSM->u.Read.u32SvnRev);
    RTMemTmpFree(pDir);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Read and validate the footer.
     */
    off = ssmR3StrmTell(&pSSM->Strm);
    uint32_t    u32StreamCRC = ssmR3StrmFinalCRC(&pSSM->Strm);
    SSMFILEFTR  Footer;
    rc = ssmR3StrmRead(&pSSM->Strm, &Footer, sizeof(Footer));
    if (RT_FAILURE(rc))
        return rc;
    return ssmR3ValidateFooter(&Footer, off, DirHdr.cEntries, pSSM->u.Read.fStreamCrc32, u32StreamCRC);
}


/**
 * Executes the loading of a V2.X file.
 *
 * @returns VBox status code.  May or may not set pSSM->rc, the returned
 *          status code is ALWAYS the more accurate of the two.
 * @param   pVM                 The cross context VM structure.
 * @param   pSSM                The saved state handle.
 */
static int ssmR3LoadExecV2(PVM pVM, PSSMHANDLE pSSM)
{
    pSSM->enmOp = SSMSTATE_LOAD_EXEC;
    for (;;)
    {
        /*
         * Read the unit header and check its integrity.
         */
        uint64_t            offUnit         = ssmR3StrmTell(&pSSM->Strm);
        uint32_t            u32CurStreamCRC = ssmR3StrmCurCRC(&pSSM->Strm);
        SSMFILEUNITHDRV2    UnitHdr;
        int rc = ssmR3StrmRead(&pSSM->Strm, &UnitHdr, RT_UOFFSETOF(SSMFILEUNITHDRV2, szName));
        if (RT_FAILURE(rc))
            return rc;
        if (RT_UNLIKELY(    memcmp(&UnitHdr.szMagic[0], SSMFILEUNITHDR_MAGIC, sizeof(UnitHdr.szMagic))
                        &&  memcmp(&UnitHdr.szMagic[0], SSMFILEUNITHDR_END,   sizeof(UnitHdr.szMagic))))
        {
            LogRel(("SSM: Unit at %#llx (%lld): Invalid unit magic: %.*Rhxs!\n",
                    offUnit, offUnit, sizeof(UnitHdr.szMagic) - 1, &UnitHdr.szMagic[0]));
            pSSM->u.Read.fHaveSetError = true;
            return VMSetError(pVM, VERR_SSM_INTEGRITY_UNIT_MAGIC, RT_SRC_POS,
                              N_("Unit at %#llx (%lld): Invalid unit magic"), offUnit, offUnit);
        }
        if (UnitHdr.cbName)
        {
            AssertLogRelMsgReturn(UnitHdr.cbName <= sizeof(UnitHdr.szName),
                                  ("Unit at %#llx (%lld): UnitHdr.cbName=%u > %u\n",
                                   offUnit, offUnit, UnitHdr.cbName, sizeof(UnitHdr.szName)),
                                  VERR_SSM_INTEGRITY_UNIT);
            rc = ssmR3StrmRead(&pSSM->Strm, &UnitHdr.szName[0], UnitHdr.cbName);
            if (RT_FAILURE(rc))
                return rc;
            AssertLogRelMsgReturn(!UnitHdr.szName[UnitHdr.cbName - 1],
                                  ("Unit at %#llx (%lld): Name %.*Rhxs was not properly terminated.\n",
                                   offUnit, offUnit, UnitHdr.cbName, UnitHdr.szName),
                                  VERR_SSM_INTEGRITY_UNIT);
        }
        SSM_CHECK_CRC32_RET(&UnitHdr, RT_UOFFSETOF_DYN(SSMFILEUNITHDRV2, szName[UnitHdr.cbName]),
                            ("Unit at %#llx (%lld): CRC mismatch: %08x, correct is %08x\n", offUnit, offUnit, u32CRC, u32ActualCRC));
        AssertLogRelMsgReturn(UnitHdr.offStream == offUnit,
                              ("Unit at %#llx (%lld): offStream=%#llx, expected %#llx\n", offUnit, offUnit, UnitHdr.offStream, offUnit),
                              VERR_SSM_INTEGRITY_UNIT);
        AssertLogRelMsgReturn(UnitHdr.u32CurStreamCRC == u32CurStreamCRC || !pSSM->Strm.fChecksummed,
                              ("Unit at %#llx (%lld): Stream CRC mismatch: %08x, correct is %08x\n", offUnit, offUnit, UnitHdr.u32CurStreamCRC, u32CurStreamCRC),
                              VERR_SSM_INTEGRITY_UNIT);
        AssertLogRelMsgReturn(!UnitHdr.fFlags, ("Unit at %#llx (%lld): fFlags=%08x\n", offUnit, offUnit, UnitHdr.fFlags),
                              VERR_SSM_INTEGRITY_UNIT);
        if (!memcmp(&UnitHdr.szMagic[0], SSMFILEUNITHDR_END,   sizeof(UnitHdr.szMagic)))
        {
            AssertLogRelMsgReturn(   UnitHdr.cbName       == 0
                                  && UnitHdr.u32Instance  == 0
                                  && UnitHdr.u32Version   == 0
                                  && UnitHdr.u32Pass      == SSM_PASS_FINAL,
                                  ("Unit at %#llx (%lld): Malformed END unit\n", offUnit, offUnit),
                                  VERR_SSM_INTEGRITY_UNIT);

            /*
             * Complete the progress bar (pending 99% afterwards) and RETURN.
             */
            Log(("SSM: Unit at %#9llx: END UNIT\n", offUnit));
            ssmR3ProgressByByte(pSSM, pSSM->cbEstTotal - pSSM->offEst);
            return ssmR3LoadDirectoryAndFooter(pSSM);
        }
        AssertLogRelMsgReturn(UnitHdr.cbName > 1, ("Unit at %#llx (%lld): No name\n", offUnit, offUnit), VERR_SSM_INTEGRITY);

        Log(("SSM: Unit at %#9llx: '%s', instance %u, pass %#x, version %u\n",
             offUnit, UnitHdr.szName, UnitHdr.u32Instance, UnitHdr.u32Pass, UnitHdr.u32Version));

        /*
         * Find the data unit in our internal table.
         */
        PSSMUNIT pUnit = ssmR3Find(pVM, UnitHdr.szName, UnitHdr.u32Instance);
        if (pUnit)
        {
            /*
             * Call the execute handler.
             */
            AssertLogRelMsgReturn(pUnit->u.Common.pfnLoadExec,
                                  ("SSM: No load exec callback for unit '%s'!\n", UnitHdr.szName),
                                  VERR_SSM_NO_LOAD_EXEC);
            pSSM->u.Read.uCurUnitVer  = UnitHdr.u32Version;
            pSSM->u.Read.uCurUnitPass = UnitHdr.u32Pass;
            pSSM->u.Read.pCurUnit     = pUnit;
            ssmR3DataReadBeginV2(pSSM);
            ssmR3UnitCritSectEnter(pVM, pUnit);
            switch (pUnit->enmType)
            {
                case SSMUNITTYPE_DEV:
                    rc = pUnit->u.Dev.pfnLoadExec(pUnit->u.Dev.pDevIns, pSSM, UnitHdr.u32Version, UnitHdr.u32Pass);
                    break;
                case SSMUNITTYPE_DRV:
                    rc = pUnit->u.Drv.pfnLoadExec(pUnit->u.Drv.pDrvIns, pSSM, UnitHdr.u32Version, UnitHdr.u32Pass);
                    break;
                case SSMUNITTYPE_USB:
                    rc = pUnit->u.Usb.pfnLoadExec(pUnit->u.Usb.pUsbIns, pSSM, UnitHdr.u32Version, UnitHdr.u32Pass);
                    break;
                case SSMUNITTYPE_INTERNAL:
                    rc = pUnit->u.Internal.pfnLoadExec(pVM, pSSM, UnitHdr.u32Version, UnitHdr.u32Pass);
                    break;
                case SSMUNITTYPE_EXTERNAL:
                    rc = pUnit->u.External.pfnLoadExec(pSSM, VMMR3GetVTable(), pUnit->u.External.pvUser,
                                                       UnitHdr.u32Version, UnitHdr.u32Pass);
                    break;
                default:
                    rc = VERR_SSM_IPE_1;
                    break;
            }
            ssmR3UnitCritSectLeave(pVM, pUnit);
            pUnit->fCalled = true;
            if (RT_FAILURE(rc) && RT_SUCCESS_NP(pSSM->rc))
                pSSM->rc = rc;
            rc = ssmR3DataReadFinishV2(pSSM);
            if (RT_SUCCESS(rc))
            {
                pSSM->offUnit     = UINT64_MAX;
                pSSM->offUnitUser = UINT64_MAX;
            }
            else
            {
                LogRel(("SSM: LoadExec failed for '%s' instance #%u (version %u, pass %#x): %Rrc\n",
                        UnitHdr.szName, UnitHdr.u32Instance, UnitHdr.u32Version, UnitHdr.u32Pass, rc));
                LogRel(("SSM: Unit at %#llx, current position: offUnit=%#llx offUnitUser=%#llx\n",
                        offUnit, pSSM->offUnit, pSSM->offUnitUser));

                if (!ASMAtomicXchgBool(&pSSM->u.Read.fHaveSetError, true))
                {
                    if (rc == VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION)
                        rc = VMSetError(pVM, rc, RT_SRC_POS, N_("Unsupported version %u of data unit '%s' (instance #%u, pass %#x)"),
                                        UnitHdr.u32Version, UnitHdr.szName, UnitHdr.u32Instance, UnitHdr.u32Pass);
                    else
                        rc = VMSetError(pVM, rc, RT_SRC_POS, N_("Failed to load unit '%s'"), UnitHdr.szName);
                }

                /* Try log the unit content, unless it's too big. */
                if (pSSM->offUnitUser < _512K)
                    ssmR3StrmLogUnitContent(pSSM, &UnitHdr, offUnit, 0, pSSM->offUnitUser + _16K);
                else
                    ssmR3StrmLogUnitContent(pSSM, &UnitHdr, offUnit, pSSM->offUnitUser - _256K, pSSM->offUnitUser + _16K);
                return rc;
            }
        }
        else
        {
            /*
             * SSM unit wasn't found - ignore this when loading for the debugger.
             */
            LogRel(("SSM: Found no handler for unit '%s' instance #%u!\n", UnitHdr.szName, UnitHdr.u32Instance));
            if (pSSM->enmAfter != SSMAFTER_DEBUG_IT)
            {
                pSSM->u.Read.fHaveSetError = true;
                return VMSetError(pVM, VERR_SSM_INTEGRITY_UNIT_NOT_FOUND, RT_SRC_POS,
                                  N_("Found no handler for unit '%s' instance #%u"), UnitHdr.szName, UnitHdr.u32Instance);
            }
            SSMR3SkipToEndOfUnit(pSSM);
            ssmR3DataReadFinishV2(pSSM);
        }

        /*
         * Check for cancellation.
         */
        if (RT_UNLIKELY(ASMAtomicUoReadU32(&(pSSM)->fCancelled) == SSMHANDLE_CANCELLED))
        {
            LogRel(("SSM: Cancelled!\n"));
            if (RT_SUCCESS(pSSM->rc))
                pSSM->rc = VERR_SSM_CANCELLED;
            return pSSM->rc;
        }
    }
    /* won't get here */
}




/**
 * Load VM save operation.
 *
 * @returns VBox status code.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pszFilename     The name of the saved state file. NULL if pStreamOps
 *                          is used.
 * @param   pStreamOps      The stream method table. NULL if pszFilename is
 *                          used.
 * @param   pvStreamOpsUser The user argument for the stream methods.
 * @param   enmAfter        What is planned after a successful load operation.
 *                          Only acceptable values are SSMAFTER_RESUME and SSMAFTER_DEBUG_IT.
 * @param   pfnProgress     Progress callback. Optional.
 * @param   pvProgressUser  User argument for the progress callback.
 *
 * @thread  EMT
 */
VMMR3DECL(int) SSMR3Load(PVM pVM, const char *pszFilename, PCSSMSTRMOPS pStreamOps, void *pvStreamOpsUser,
                         SSMAFTER enmAfter, PFNVMPROGRESS pfnProgress, void *pvProgressUser)
{
    LogFlow(("SSMR3Load: pszFilename=%p:{%s} pStreamOps=%p pvStreamOpsUser=%p enmAfter=%d pfnProgress=%p pvProgressUser=%p\n",
             pszFilename, pszFilename, pStreamOps, pvStreamOpsUser, enmAfter, pfnProgress, pvProgressUser));
    VM_ASSERT_EMT0(pVM);

    /*
     * Validate input.
     */
    AssertMsgReturn(   enmAfter == SSMAFTER_RESUME
                    || enmAfter == SSMAFTER_TELEPORT
                    || enmAfter == SSMAFTER_DEBUG_IT,
                    ("%d\n", enmAfter),
                    VERR_INVALID_PARAMETER);
    AssertReturn(!pszFilename != !pStreamOps, VERR_INVALID_PARAMETER);
    if (pStreamOps)
    {
        AssertReturn(pStreamOps->u32Version == SSMSTRMOPS_VERSION, VERR_INVALID_MAGIC);
        AssertReturn(pStreamOps->u32EndVersion == SSMSTRMOPS_VERSION, VERR_INVALID_MAGIC);
        AssertReturn(pStreamOps->pfnWrite, VERR_INVALID_PARAMETER);
        AssertReturn(pStreamOps->pfnRead, VERR_INVALID_PARAMETER);
        AssertReturn(pStreamOps->pfnSeek, VERR_INVALID_PARAMETER);
        AssertReturn(pStreamOps->pfnTell, VERR_INVALID_PARAMETER);
        AssertReturn(pStreamOps->pfnSize, VERR_INVALID_PARAMETER);
        AssertReturn(pStreamOps->pfnClose, VERR_INVALID_PARAMETER);
    }

    /*
     * Create the handle and open the file.
     */
    SSMHANDLE Handle;
    int rc = ssmR3OpenFile(pVM, pszFilename, pStreamOps, pvStreamOpsUser, false /* fChecksumIt */,
                           true /* fChecksumOnRead */, 8 /*cBuffers*/, &Handle);
    if (RT_SUCCESS(rc))
    {
        ssmR3StrmStartIoThread(&Handle.Strm);
        ssmR3SetCancellable(pVM, &Handle, true);

        Handle.enmAfter         = enmAfter;
        Handle.pfnProgress      = pfnProgress;
        Handle.pvUser           = pvProgressUser;
        Handle.uPercentLive     = 0;
        Handle.uPercentPrepare  = 2;
        Handle.uPercentDone     = 2;

        if (Handle.u.Read.u16VerMajor)
            LogRel(("SSM: File header: Format %u.%u, VirtualBox Version %u.%u.%u r%u, %u-bit host, cbGCPhys=%u, cbGCPtr=%u\n",
                    Handle.u.Read.uFmtVerMajor, Handle.u.Read.uFmtVerMinor,
                    Handle.u.Read.u16VerMajor, Handle.u.Read.u16VerMinor, Handle.u.Read.u32VerBuild, Handle.u.Read.u32SvnRev,
                    Handle.u.Read.cHostBits, Handle.u.Read.cbGCPhys, Handle.u.Read.cbGCPtr));
        else
            LogRel(("SSM: File header: Format %u.%u, %u-bit host, cbGCPhys=%u, cbGCPtr=%u\n" ,
                    Handle.u.Read.uFmtVerMajor, Handle.u.Read.uFmtVerMinor,
                    Handle.u.Read.cHostBits, Handle.u.Read.cbGCPhys, Handle.u.Read.cbGCPtr));

        if (pfnProgress)
            pfnProgress(pVM->pUVM, Handle.uPercent, pvProgressUser);

        /*
         * Clear the per unit flags.
         */
        PSSMUNIT pUnit;
        for (pUnit = pVM->ssm.s.pHead; pUnit; pUnit = pUnit->pNext)
            pUnit->fCalled = false;

        /*
         * Do the prepare run.
         */
        Handle.rc = VINF_SUCCESS;
        Handle.enmOp = SSMSTATE_LOAD_PREP;
        for (pUnit = pVM->ssm.s.pHead; pUnit; pUnit = pUnit->pNext)
        {
            if (pUnit->u.Common.pfnLoadPrep)
            {
                Handle.u.Read.pCurUnit = pUnit;
                pUnit->fCalled = true;
                ssmR3UnitCritSectEnter(pVM, pUnit);
                switch (pUnit->enmType)
                {
                    case SSMUNITTYPE_DEV:
                        rc = pUnit->u.Dev.pfnLoadPrep(pUnit->u.Dev.pDevIns, &Handle);
                        break;
                    case SSMUNITTYPE_DRV:
                        rc = pUnit->u.Drv.pfnLoadPrep(pUnit->u.Drv.pDrvIns, &Handle);
                        break;
                    case SSMUNITTYPE_USB:
                        rc = pUnit->u.Usb.pfnLoadPrep(pUnit->u.Usb.pUsbIns, &Handle);
                        break;
                    case SSMUNITTYPE_INTERNAL:
                        rc = pUnit->u.Internal.pfnLoadPrep(pVM, &Handle);
                        break;
                    case SSMUNITTYPE_EXTERNAL:
                        rc = pUnit->u.External.pfnLoadPrep(&Handle, VMMR3GetVTable(), pUnit->u.External.pvUser);
                        break;
                    default:
                        rc = VERR_SSM_IPE_1;
                        break;
                }
                ssmR3UnitCritSectLeave(pVM, pUnit);
                Handle.u.Read.pCurUnit = NULL;
                if (RT_FAILURE(rc) && RT_SUCCESS_NP(Handle.rc))
                    Handle.rc = rc;
                else
                    rc = Handle.rc;
                if (RT_FAILURE(rc))
                {
                    LogRel(("SSM: Prepare load failed with rc=%Rrc for data unit '%s.\n", rc, pUnit->szName));
                    break;
                }
            }
        }

        /* end of prepare % */
        if (pfnProgress)
            pfnProgress(pVM->pUVM, Handle.uPercentPrepare - 1, pvProgressUser);
        Handle.uPercent      = Handle.uPercentPrepare;
        Handle.cbEstTotal    = Handle.u.Read.cbLoadFile;
        Handle.offEstUnitEnd = Handle.u.Read.cbLoadFile;

        /*
         * Do the execute run.
         */
        if (RT_SUCCESS(rc))
        {
            if (Handle.u.Read.uFmtVerMajor >= 2)
                rc = ssmR3LoadExecV2(pVM, &Handle);
            else
                rc = ssmR3LoadExecV1(pVM, &Handle);
            Handle.u.Read.pCurUnit       = NULL;
            Handle.u.Read.uCurUnitVer    = UINT32_MAX;
            Handle.u.Read.uCurUnitPass   = 0;

            /* (progress should be pending 99% now) */
            AssertMsg(   Handle.fLiveSave
                      || RT_FAILURE(rc)
                      || Handle.uPercent == 101 - Handle.uPercentDone, ("%d\n", Handle.uPercent));
        }

        /*
         * Do the done run.
         */
        Handle.rc = rc;
        Handle.enmOp = SSMSTATE_LOAD_DONE;
        for (pUnit = pVM->ssm.s.pHead; pUnit; pUnit = pUnit->pNext)
        {
            if (    pUnit->u.Common.pfnLoadDone
                && (   pUnit->fCalled
                    || (!pUnit->u.Common.pfnLoadPrep && !pUnit->u.Common.pfnLoadExec)))
            {
                Handle.u.Read.pCurUnit = pUnit;
                int const rcOld = Handle.rc;
                rc = VINF_SUCCESS;
                ssmR3UnitCritSectEnter(pVM, pUnit);
                switch (pUnit->enmType)
                {
                    case SSMUNITTYPE_DEV:
                        rc = pUnit->u.Dev.pfnLoadDone(pUnit->u.Dev.pDevIns, &Handle);
                        break;
                    case SSMUNITTYPE_DRV:
                        rc = pUnit->u.Drv.pfnLoadDone(pUnit->u.Drv.pDrvIns, &Handle);
                        break;
                    case SSMUNITTYPE_USB:
                        rc = pUnit->u.Usb.pfnLoadDone(pUnit->u.Usb.pUsbIns, &Handle);
                        break;
                    case SSMUNITTYPE_INTERNAL:
                        rc = pUnit->u.Internal.pfnLoadDone(pVM, &Handle);
                        break;
                    case SSMUNITTYPE_EXTERNAL:
                        rc = pUnit->u.External.pfnLoadDone(&Handle, VMMR3GetVTable(), pUnit->u.External.pvUser);
                        break;
                    default:
                        rc = VERR_SSM_IPE_1;
                        break;
                }
                ssmR3UnitCritSectLeave(pVM, pUnit);
                Handle.u.Read.pCurUnit = NULL;
                if (RT_SUCCESS(rc) && Handle.rc != rcOld)
                    rc = Handle.rc;
                if (RT_FAILURE(rc))
                {
                    LogRel(("SSM: LoadDone failed with rc=%Rrc for data unit '%s' instance #%u.\n",
                            rc, pUnit->szName, pUnit->u32Instance));
                    if (!ASMAtomicXchgBool(&Handle.u.Read.fHaveSetError, true))
                        VMSetError(pVM, rc, RT_SRC_POS, N_("LoadDone failed with rc=%Rrc for data unit '%s' instance #%u."),
                                   rc, pUnit->szName, pUnit->u32Instance);
                    if (RT_SUCCESS_NP(Handle.rc))
                        Handle.rc = rc;
                }
            }
        }

        /* progress */
        if (pfnProgress)
            pfnProgress(pVM->pUVM, 99, pvProgressUser);

        ssmR3SetCancellable(pVM, &Handle, false);
        ssmR3StrmClose(&Handle.Strm, Handle.rc == VERR_SSM_CANCELLED);
        rc = Handle.rc;
    }

    /*
     * Done
     */
    if (RT_SUCCESS(rc))
    {
        /* progress */
        if (pfnProgress)
            pfnProgress(pVM->pUVM, 100, pvProgressUser);
        Log(("SSM: Load of '%s' completed!\n", pszFilename));
    }
    return rc;
}


/**
 * VMSetError wrapper for load errors that inserts the saved state details.
 *
 * @returns rc.
 * @param   pSSM                The saved state handle.
 * @param   rc                  The status code of the error. Use RT_SRC_POS.
 * @param   SRC_POS             The source location.
 * @param   pszFormat           The message format string.
 * @param   ...                 Variable argument list.
 */
VMMR3DECL(int) SSMR3SetLoadError(PSSMHANDLE pSSM, int rc, RT_SRC_POS_DECL, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    rc = SSMR3SetLoadErrorV(pSSM, rc, RT_SRC_POS_ARGS, pszFormat, va);
    va_end(va);
    return rc;
}


/**
 * VMSetError wrapper for load errors that inserts the saved state details.
 *
 * @returns rc.
 * @param   pSSM                The saved state handle.
 * @param   rc                  The status code of the error.
 * @param   SRC_POS             The error location, use RT_SRC_POS.
 * @param   pszFormat           The message format string.
 * @param   va                  Variable argument list.
 */
VMMR3DECL(int) SSMR3SetLoadErrorV(PSSMHANDLE pSSM, int rc, RT_SRC_POS_DECL, const char *pszFormat, va_list va)
{
    /*
     * Input validations.
     */
    SSM_ASSERT_READABLE_RET(pSSM);
    AssertPtr(pszFormat);
    Assert(RT_FAILURE_NP(rc));

    /*
     * Format the incoming error.
     */
    char *pszMsg;
    RTStrAPrintfV(&pszMsg, pszFormat, va);
    if (!pszMsg)
    {
        VMSetError(pSSM->pVM, VERR_NO_MEMORY, RT_SRC_POS,
                   N_("SSMR3SetLoadErrorV ran out of memory formatting: %s\n"), pszFormat);
        return rc;
    }

    /*
     * Forward to VMSetError with the additional info.
     */
    PSSMUNIT    pUnit       = pSSM->u.Read.pCurUnit;
    const char *pszName     = pUnit ? pUnit->szName      : "unknown";
    uint32_t    uInstance   = pUnit ? pUnit->u32Instance : 0;
    if (   pSSM->enmOp == SSMSTATE_LOAD_EXEC
        && pSSM->u.Read.uCurUnitPass == SSM_PASS_FINAL)
        rc = VMSetError(pSSM->pVM, rc, RT_SRC_POS_ARGS, N_("%s#%u: %s [ver=%u pass=final]"),
                        pszName, uInstance, pszMsg, pSSM->u.Read.uCurUnitVer);
    else if (pSSM->enmOp == SSMSTATE_LOAD_EXEC)
        rc = VMSetError(pSSM->pVM, rc, RT_SRC_POS_ARGS, N_("%s#%u: %s [ver=%u pass=#%u]"),
                        pszName, uInstance, pszMsg, pSSM->u.Read.uCurUnitVer, pSSM->u.Read.uCurUnitPass);
    else if (pSSM->enmOp == SSMSTATE_LOAD_PREP)
        rc = VMSetError(pSSM->pVM, rc, RT_SRC_POS_ARGS, N_("%s#%u: %s [prep]"),
                        pszName, uInstance, pszMsg);
    else if (pSSM->enmOp == SSMSTATE_LOAD_DONE)
        rc = VMSetError(pSSM->pVM, rc, RT_SRC_POS_ARGS, N_("%s#%u: %s [done]"),
                        pszName, uInstance, pszMsg);
    else if (pSSM->enmOp == SSMSTATE_OPEN_READ)
        rc = VMSetError(pSSM->pVM, rc, RT_SRC_POS_ARGS, N_("%s#%u: %s [read]"),
                        pszName, uInstance, pszMsg);
    else
        AssertFailed();
    pSSM->u.Read.fHaveSetError = true;
    RTStrFree(pszMsg);
    return rc;
}


/**
 * SSMR3SetLoadError wrapper that returns VERR_SSM_LOAD_CONFIG_MISMATCH.
 *
 * @returns VERR_SSM_LOAD_CONFIG_MISMATCH.
 * @param   pSSM                The saved state handle.
 * @param   SRC_POS             The error location, use RT_SRC_POS.
 * @param   pszFormat           The message format string.
 * @param   ...                 Variable argument list.
 */
VMMR3DECL(int) SSMR3SetCfgError(PSSMHANDLE pSSM, RT_SRC_POS_DECL, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    int rc = SSMR3SetLoadErrorV(pSSM, VERR_SSM_LOAD_CONFIG_MISMATCH, RT_SRC_POS_ARGS, pszFormat, va);
    va_end(va);
    return rc;
}


/**
 * SSMR3SetLoadError wrapper that returns VERR_SSM_LOAD_CONFIG_MISMATCH.
 *
 * @returns VERR_SSM_LOAD_CONFIG_MISMATCH.
 * @param   pSSM                The saved state handle.
 * @param   SRC_POS             The error location, use RT_SRC_POS.
 * @param   pszFormat           The message format string.
 * @param   va                  Variable argument list.
 */
VMMR3DECL(int) SSMR3SetCfgErrorV(PSSMHANDLE pSSM, RT_SRC_POS_DECL, const char *pszFormat, va_list va)
{
    return SSMR3SetLoadErrorV(pSSM, VERR_SSM_LOAD_CONFIG_MISMATCH, RT_SRC_POS_ARGS, pszFormat, va);
}

#endif /* !SSM_STANDALONE */

/**
 * Validates a file as a validate SSM saved state.
 *
 * This will only verify the file format, the format and content of individual
 * data units are not inspected.
 *
 * @returns VINF_SUCCESS if valid.
 * @returns VBox status code on other failures.
 *
 * @param   pszFilename     The path to the file to validate.
 * @param   pStreamOps      The stream method table. NULL if pszFilename is
 *                          used.
 * @param   pvStreamOps     The user argument to the stream methods.
 * @param   fChecksumIt     Whether to checksum the file or not.
 *
 * @thread  Any.
 */
VMMR3DECL(int) SSMR3ValidateFile(const char *pszFilename, PCSSMSTRMOPS pStreamOps, void *pvStreamOps, bool fChecksumIt)
{
    LogFlow(("SSMR3ValidateFile: pszFilename=%p:{%s} fChecksumIt=%RTbool\n", pszFilename, pszFilename, fChecksumIt));

    /*
     * Try open the file and validate it.
     */
    SSMHANDLE Handle;
    int rc = ssmR3OpenFile(NULL, pszFilename, pStreamOps, pvStreamOps, fChecksumIt,
                           false /*fChecksumOnRead*/, 1 /*cBuffers*/, &Handle);
    if (RT_SUCCESS(rc))
        ssmR3StrmClose(&Handle.Strm, false /*fCancelled*/);
    else
        Log(("SSM: Failed to open saved state file '%s', rc=%Rrc.\n",  pszFilename, rc));
    return rc;
}


/**
 * Opens a saved state file for reading.
 *
 * @returns VBox status code.
 *
 * @param   pszFilename     The path to the saved state file.
 * @param   pStreamOps      The stream method table. NULL if pszFilename is
 *                          used.
 * @param   pvStreamOps     The user argument to the stream methods.
 * @param   fFlags          Open flags, SSM_OPEN_F_XXXX.
 * @param   ppSSM           Where to store the SSM handle.
 *
 * @thread  Any.
 */
VMMR3DECL(int) SSMR3Open(const char *pszFilename, PCSSMSTRMOPS pStreamOps, void *pvStreamOps,
                         unsigned fFlags, PSSMHANDLE *ppSSM)
{
    LogFlow(("SSMR3Open: pszFilename=%p:{%s} fFlags=%#x ppSSM=%p\n", pszFilename, pszFilename, fFlags, ppSSM));

    /*
     * Validate input.
     */
    AssertReturn(!pszFilename != !pStreamOps, VERR_INVALID_POINTER);
#ifndef SSM_STANDALONE
    AssertMsgReturn(!(fFlags & ~SSM_OPEN_F_FOR_WRITING), ("%#x\n", fFlags), VERR_INVALID_FLAGS);
#else
    AssertMsgReturn(!fFlags, ("%#x\n", fFlags), VERR_INVALID_FLAGS);
#endif
    AssertPtrReturn(ppSSM, VERR_INVALID_POINTER);

    int rc;
#ifndef SSM_STANDALONE
    if (fFlags & SSM_OPEN_F_FOR_WRITING)
    {
        /*
         * Try create a new file.
         */
        PSSMHANDLE pSSM;
        rc = ssmR3SaveDoCreateFile(NULL, pszFilename, pStreamOps, pvStreamOps, SSMAFTER_OPENED, NULL, NULL, &pSSM);
        if (RT_SUCCESS(rc))
        {
            Log(("SSM: Starting state save to file '%s'...\n", pszFilename));
            ssmR3StrmStartIoThread(&pSSM->Strm);
            pSSM->enmOp = SSMSTATE_OPEN_WRITE;
            *ppSSM = pSSM;
            return VINF_SUCCESS;
        }
        Log(("SSMR3Open: Failed to open saved state file '%s' for writing: rc=%Rrc.\n",  pszFilename, rc));
    }
    else
#endif /* !SSM_STANDALONE */
    {
        /*
         * Allocate a handle.
         */
        PSSMHANDLE pSSM = (PSSMHANDLE)RTMemAllocZ(sizeof(*pSSM));
        AssertReturn(pSSM, VERR_NO_MEMORY);

        /*
         * Try open the file and validate the header and stuff.
         */
        rc = ssmR3OpenFile(NULL, pszFilename, pStreamOps, pvStreamOps, false /*fChecksumIt*/,
                               true /*fChecksumOnRead*/, 1 /*cBuffers*/, pSSM);
        if (RT_SUCCESS(rc))
        {
            pSSM->enmAfter = SSMAFTER_OPENED;
            pSSM->enmOp    = SSMSTATE_OPEN_READ;
            *ppSSM = pSSM;
            LogFlow(("SSMR3Open: returns VINF_SUCCESS *ppSSM=%p\n", *ppSSM));
            return VINF_SUCCESS;
        }

        Log(("SSMR3Open: Failed to open saved state file '%s', rc=%Rrc.\n",  pszFilename, rc));
        RTMemFree(pSSM);
    }

    return rc;
}


/**
 * Closes a saved state file opened by SSMR3Open().
 *
 * @returns VBox status code.
 *
 * @param   pSSM            The SSM handle returned by SSMR3Open().
 *
 * @thread  Any, but the caller is responsible for serializing calls per handle.
 */
VMMR3DECL(int) SSMR3Close(PSSMHANDLE pSSM)
{
    LogFlow(("SSMR3Close: pSSM=%p\n", pSSM));

    /*
     * Validate input.
     */
    AssertPtrReturn(pSSM, VERR_INVALID_POINTER);
    AssertMsgReturn(pSSM->enmAfter == SSMAFTER_OPENED, ("%d\n", pSSM->enmAfter),VERR_INVALID_PARAMETER);
    AssertMsgReturn(pSSM->enmOp == SSMSTATE_OPEN_READ || pSSM->enmOp == SSMSTATE_OPEN_WRITE,
                    ("%d\n", pSSM->enmOp), VERR_INVALID_PARAMETER);
    Assert(pSSM->fCancelled == SSMHANDLE_OK);

    /*
     * Close the stream and free the handle.
     */
    int rc = ssmR3StrmClose(&pSSM->Strm, pSSM->rc == VERR_SSM_CANCELLED);
    if (pSSM->enmOp == SSMSTATE_OPEN_READ && pSSM->u.Read.pZipDecompV1)
    {
        RTZipDecompDestroy(pSSM->u.Read.pZipDecompV1);
        pSSM->u.Read.pZipDecompV1 = NULL;
    }
    if (pSSM->enmOp == SSMSTATE_OPEN_WRITE && pSSM->u.Write.pDir)
    {
        RTMemFree(pSSM->u.Write.pDir);
        pSSM->u.Write.pDir = NULL;
    }
    RTMemFree(pSSM);
    return rc;
}


/**
 * Worker for SSMR3Seek that seeks version 1 saved state files.
 *
 * @returns VBox status code.
 * @param   pSSM                The SSM handle.
 * @param   pszUnit             The unit to seek to.
 * @param   iInstance           The particular instance we seek.
 * @param   piVersion           Where to store the unit version number.
 */
static int ssmR3FileSeekV1(PSSMHANDLE pSSM, const char *pszUnit, uint32_t iInstance, uint32_t *piVersion)
{
    /*
     * Walk the data units until we find EOF or a match.
     */
    size_t              cbUnitNm = strlen(pszUnit) + 1;
    AssertLogRelReturn(cbUnitNm <= SSM_MAX_NAME_SIZE, VERR_SSM_UNIT_NOT_FOUND);
    char                szName[SSM_MAX_NAME_SIZE];
    SSMFILEUNITHDRV1    UnitHdr;
    for (RTFOFF off = pSSM->u.Read.cbFileHdr; ; off += UnitHdr.cbUnit)
    {
        /*
         * Read the unit header and verify it.
         */
        int rc = ssmR3StrmPeekAt(&pSSM->Strm, off, &UnitHdr, RT_UOFFSETOF(SSMFILEUNITHDRV1, szName), NULL);
        AssertRCReturn(rc, rc);
        if (!memcmp(&UnitHdr.achMagic[0], SSMFILEUNITHDR_MAGIC, sizeof(SSMFILEUNITHDR_MAGIC)))
        {
            /*
             * Does what we've got match, if so read the name.
             */
            if (    UnitHdr.u32Instance == iInstance
                &&  UnitHdr.cchName     == cbUnitNm)
            {
                rc = ssmR3StrmPeekAt(&pSSM->Strm, off + RT_UOFFSETOF(SSMFILEUNITHDRV1, szName), szName, cbUnitNm, NULL);
                AssertRCReturn(rc, rc);
                AssertLogRelMsgReturn(!szName[UnitHdr.cchName - 1],
                                      (" Unit name '%.*s' was not properly terminated.\n", cbUnitNm, szName),
                                      VERR_SSM_INTEGRITY_UNIT);

                /*
                 * Does the name match?
                 */
                if (!memcmp(szName, pszUnit, cbUnitNm))
                {
                    rc = ssmR3StrmSeek(&pSSM->Strm, off + RT_UOFFSETOF(SSMFILEUNITHDRV1, szName) + cbUnitNm, RTFILE_SEEK_BEGIN, 0);
                    pSSM->cbUnitLeftV1 = UnitHdr.cbUnit - RT_UOFFSETOF_DYN(SSMFILEUNITHDRV1, szName[cbUnitNm]);
                    pSSM->offUnit      = 0;
                    pSSM->offUnitUser  = 0;
                    if (piVersion)
                        *piVersion = UnitHdr.u32Version;
                    return VINF_SUCCESS;
                }
            }
        }
        else if (!memcmp(&UnitHdr.achMagic[0], SSMFILEUNITHDR_END, sizeof(SSMFILEUNITHDR_END)))
            return VERR_SSM_UNIT_NOT_FOUND;
        else
            AssertLogRelMsgFailedReturn(("Invalid unit magic at offset %RTfoff, '%.*s'!\n",
                                         off, sizeof(UnitHdr.achMagic) - 1, &UnitHdr.achMagic[0]),
                                        VERR_SSM_INTEGRITY_UNIT_MAGIC);
    }
    /* won't get here. */
}


/**
 * Worker for ssmR3FileSeekV2 for simplifying memory cleanup.
 *
 * @returns VBox status code.
 * @param   pSSM                The SSM handle.
 * @param   pDir                The directory buffer.
 * @param   cbDir               The size of the directory.
 * @param   cDirEntries         The number of directory entries.
 * @param   offDir              The directory offset in the file.
 * @param   pszUnit             The unit to seek to.
 * @param   iInstance           The particular instance we seek.
 * @param   piVersion           Where to store the unit version number.
 */
static int ssmR3FileSeekSubV2(PSSMHANDLE pSSM, PSSMFILEDIR pDir, size_t cbDir, uint32_t cDirEntries, uint64_t offDir,
                              const char *pszUnit, uint32_t iInstance, uint32_t *piVersion)
{
    /*
     * Read it.
     */
    int rc = ssmR3StrmPeekAt(&pSSM->Strm, offDir, pDir, cbDir, NULL);
    AssertLogRelRCReturn(rc, rc);
    rc = ssmR3ValidateDirectory(pDir, (uint32_t)cbDir, offDir, cDirEntries, pSSM->u.Read.cbFileHdr, pSSM->u.Read.u32SvnRev);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Search the directory.
     */
    size_t          cbUnitNm   = strlen(pszUnit) + 1;
    uint32_t const  u32NameCRC = RTCrc32(pszUnit, cbUnitNm - 1);
    for (uint32_t i = 0; i < cDirEntries; i++)
    {
        if (    pDir->aEntries[i].u32NameCRC  == u32NameCRC
            &&  pDir->aEntries[i].u32Instance == iInstance
            &&  pDir->aEntries[i].off != 0 /* bug in unreleased code */
           )
        {
            /*
             * Read and validate the unit header.
             */
            SSMFILEUNITHDRV2    UnitHdr;
            size_t              cbToRead = sizeof(UnitHdr);
            if (pDir->aEntries[i].off + cbToRead > offDir)
            {
                cbToRead = offDir - pDir->aEntries[i].off;
                RT_ZERO(UnitHdr);
            }
            rc = ssmR3StrmPeekAt(&pSSM->Strm, pDir->aEntries[i].off, &UnitHdr, cbToRead, NULL);
            AssertLogRelRCReturn(rc, rc);

            AssertLogRelMsgReturn(!memcmp(UnitHdr.szMagic, SSMFILEUNITHDR_MAGIC, sizeof(UnitHdr.szMagic)),
                                  ("Bad unit header or dictionary offset: i=%u off=%lld\n", i, pDir->aEntries[i].off),
                                  VERR_SSM_INTEGRITY_UNIT);
            AssertLogRelMsgReturn(UnitHdr.offStream == pDir->aEntries[i].off,
                                  ("Bad unit header: i=%d off=%lld offStream=%lld\n", i, pDir->aEntries[i].off, UnitHdr.offStream),
                                  VERR_SSM_INTEGRITY_UNIT);
            AssertLogRelMsgReturn(UnitHdr.u32Instance == pDir->aEntries[i].u32Instance,
                                  ("Bad unit header: i=%d off=%lld u32Instance=%u Dir.u32Instance=%u\n",
                                   i, pDir->aEntries[i].off, UnitHdr.u32Instance, pDir->aEntries[i].u32Instance),
                                  VERR_SSM_INTEGRITY_UNIT);
            uint32_t cbUnitHdr = RT_UOFFSETOF_DYN(SSMFILEUNITHDRV2, szName[UnitHdr.cbName]);
            AssertLogRelMsgReturn(   UnitHdr.cbName > 0
                                  && UnitHdr.cbName < sizeof(UnitHdr)
                                  && cbUnitHdr <= cbToRead,
                                  ("Bad unit header: i=%u off=%lld cbName=%#x cbToRead=%#x\n", i, pDir->aEntries[i].off, UnitHdr.cbName, cbToRead),
                                  VERR_SSM_INTEGRITY_UNIT);
            SSM_CHECK_CRC32_RET(&UnitHdr, RT_UOFFSETOF_DYN(SSMFILEUNITHDRV2, szName[UnitHdr.cbName]),
                                ("Bad unit header CRC: i=%u off=%lld u32CRC=%#x u32ActualCRC=%#x\n",
                                 i, pDir->aEntries[i].off, u32CRC, u32ActualCRC));

            /*
             * Ok, it is valid, get on with the comparing now.
             */
            if (    UnitHdr.cbName == cbUnitNm
                &&  !memcmp(UnitHdr.szName, pszUnit, cbUnitNm))
            {
                if (piVersion)
                    *piVersion = UnitHdr.u32Version;
                rc = ssmR3StrmSeek(&pSSM->Strm, pDir->aEntries[i].off + cbUnitHdr, RTFILE_SEEK_BEGIN,
                                   RTCrc32Process(UnitHdr.u32CurStreamCRC, &UnitHdr, cbUnitHdr));
                AssertLogRelRCReturn(rc, rc);
                ssmR3DataReadBeginV2(pSSM);
                return VINF_SUCCESS;
            }
        }
    }

    return VERR_SSM_UNIT_NOT_FOUND;
}


/**
 * Worker for SSMR3Seek that seeks version 2 saved state files.
 *
 * @returns VBox status code.
 * @param   pSSM                The SSM handle.
 * @param   pszUnit             The unit to seek to.
 * @param   iInstance           The particular instance we seek.
 * @param   piVersion           Where to store the unit version number.
 */
static int ssmR3FileSeekV2(PSSMHANDLE pSSM, const char *pszUnit, uint32_t iInstance, uint32_t *piVersion)
{
    /*
     * Read the footer, allocate a temporary buffer for the dictionary and
     * pass it down to a worker to simplify cleanup.
     */
    uint64_t        offFooter;
    SSMFILEFTR      Footer;
    int rc = ssmR3StrmPeekAt(&pSSM->Strm, -(RTFOFF)sizeof(Footer), &Footer, sizeof(Footer), &offFooter);
    AssertLogRelRCReturn(rc, rc);
    AssertLogRelReturn(!memcmp(Footer.szMagic, SSMFILEFTR_MAGIC, sizeof(Footer.szMagic)), VERR_SSM_INTEGRITY);
    SSM_CHECK_CRC32_RET(&Footer, sizeof(Footer), ("Bad footer CRC: %08x, actual %08x\n", u32CRC, u32ActualCRC));

    size_t const    cbDir = RT_UOFFSETOF_DYN(SSMFILEDIR, aEntries[Footer.cDirEntries]);
    PSSMFILEDIR     pDir  = (PSSMFILEDIR)RTMemTmpAlloc(cbDir);
    if (RT_UNLIKELY(!pDir))
        return VERR_NO_TMP_MEMORY;
    rc = ssmR3FileSeekSubV2(pSSM, pDir, cbDir, Footer.cDirEntries, offFooter - cbDir,
                            pszUnit, iInstance, piVersion);
    RTMemTmpFree(pDir);

    return rc;
}


/**
 * Seeks to a specific data unit.
 *
 * After seeking it's possible to use the getters to on
 * that data unit.
 *
 * @returns VBox status code.
 * @returns VERR_SSM_UNIT_NOT_FOUND if the unit+instance wasn't found.
 *
 * @param   pSSM            The SSM handle returned by SSMR3Open().
 * @param   pszUnit         The name of the data unit.
 * @param   iInstance       The instance number.
 * @param   piVersion       Where to store the version number. (Optional)
 *
 * @thread  Any, but the caller is responsible for serializing calls per handle.
 */
VMMR3DECL(int) SSMR3Seek(PSSMHANDLE pSSM, const char *pszUnit, uint32_t iInstance, uint32_t *piVersion)
{
    LogFlow(("SSMR3Seek: pSSM=%p pszUnit=%p:{%s} iInstance=%RU32 piVersion=%p\n",
             pSSM, pszUnit, pszUnit, iInstance, piVersion));

    /*
     * Validate input.
     */
    AssertPtrReturn(pSSM, VERR_INVALID_PARAMETER);
    AssertMsgReturn(pSSM->enmAfter == SSMAFTER_OPENED, ("%d\n", pSSM->enmAfter),VERR_INVALID_PARAMETER);
    AssertMsgReturn(pSSM->enmOp == SSMSTATE_OPEN_READ, ("%d\n", pSSM->enmOp), VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszUnit, VERR_INVALID_POINTER);
    AssertPtrNullReturn(piVersion, VERR_INVALID_POINTER);

    /*
     * Reset the state.
     */
    if (pSSM->u.Read.pZipDecompV1)
    {
        RTZipDecompDestroy(pSSM->u.Read.pZipDecompV1);
        pSSM->u.Read.pZipDecompV1 = NULL;
    }
    pSSM->cbUnitLeftV1  = 0;
    pSSM->offUnit       = UINT64_MAX;
    pSSM->offUnitUser   = UINT64_MAX;

    /*
     * Call the version specific workers.
     */
    if (pSSM->u.Read.uFmtVerMajor >= 2)
        pSSM->rc = ssmR3FileSeekV2(pSSM, pszUnit, iInstance, piVersion);
    else
        pSSM->rc = ssmR3FileSeekV1(pSSM, pszUnit, iInstance, piVersion);
    return pSSM->rc;
}



/* ... Misc APIs ... */
/* ... Misc APIs ... */
/* ... Misc APIs ... */
/* ... Misc APIs ... */
/* ... Misc APIs ... */
/* ... Misc APIs ... */
/* ... Misc APIs ... */
/* ... Misc APIs ... */
/* ... Misc APIs ... */
/* ... Misc APIs ... */
/* ... Misc APIs ... */



/**
 * Query what the VBox status code of the operation is.
 *
 * This can be used for putting and getting a batch of values
 * without bother checking the result till all the calls have
 * been made.
 *
 * @returns SSMAFTER enum value.
 * @param   pSSM            The saved state handle.
 */
VMMR3DECL(int) SSMR3HandleGetStatus(PSSMHANDLE pSSM)
{
    SSM_ASSERT_VALID_HANDLE(pSSM);
    return pSSM->rc;
}


/**
 * Fail the load operation.
 *
 * This is mainly intended for sub item loaders (like timers) which
 * return code isn't necessarily heeded by the caller but is important
 * to SSM.
 *
 * @returns VBox status code of the handle, or VERR_INVALID_PARAMETER.
 * @param   pSSM            The saved state handle.
 * @param   iStatus         Failure status code. This MUST be a VERR_*.
 */
VMMR3DECL(int) SSMR3HandleSetStatus(PSSMHANDLE pSSM, int iStatus)
{
    SSM_ASSERT_VALID_HANDLE(pSSM);
    Assert(pSSM->enmOp != SSMSTATE_LIVE_VOTE);
    if (RT_FAILURE(iStatus))
    {
        int rc = pSSM->rc;
        if (RT_SUCCESS(rc))
            pSSM->rc = rc = iStatus;
        return rc;
    }
    AssertMsgFailed(("iStatus=%d %Rrc\n", iStatus, iStatus));
    return VERR_INVALID_PARAMETER;
}


/**
 * Get what to do after this operation.
 *
 * @returns SSMAFTER enum value.
 * @param   pSSM            The saved state handle.
 */
VMMR3DECL(SSMAFTER) SSMR3HandleGetAfter(PSSMHANDLE pSSM)
{
    SSM_ASSERT_VALID_HANDLE(pSSM);
    return pSSM->enmAfter;
}


/**
 * Checks if it is a live save operation or not.
 *
 * @returns True if it is, false if it isn't.
 * @param   pSSM            The saved state handle.
 */
VMMR3DECL(bool) SSMR3HandleIsLiveSave(PSSMHANDLE pSSM)
{
    SSM_ASSERT_VALID_HANDLE(pSSM);
    return pSSM->fLiveSave;
}


/**
 * Gets the maximum downtime for a live operation.
 *
 * @returns The max downtime in milliseconds.  Can be anything from 0 thru
 *          UINT32_MAX.
 *
 * @param   pSSM            The saved state handle.
 */
VMMR3DECL(uint32_t) SSMR3HandleMaxDowntime(PSSMHANDLE pSSM)
{
    SSM_ASSERT_VALID_HANDLE(pSSM);
    if (pSSM->enmOp <= SSMSTATE_SAVE_DONE)
        return pSSM->u.Write.cMsMaxDowntime;
    return UINT32_MAX;
}


/**
 * Gets the host bit count of a saved state.
 *
 * @returns 32 or 64. If pSSM is invalid, 0 is returned.
 * @param   pSSM            The saved state handle.
 *
 * @remarks This method should ONLY be used for hacks when loading OLDER saved
 *          state that have data layout or semantic changes without the
 *          compulsory version number change.
 */
VMMR3DECL(uint32_t) SSMR3HandleHostBits(PSSMHANDLE pSSM)
{
    SSM_ASSERT_VALID_HANDLE(pSSM);
    return ssmR3GetHostBits(pSSM);
}


/**
 * Get the VirtualBox SVN revision that created the saved state.
 *
 * @returns The revision number on success.
 *          form.  If we don't know, it's 0.
 * @param   pSSM            The saved state handle.
 *
 * @remarks This method should ONLY be used for hacks when loading OLDER saved
 *          state that have data layout or semantic changes without the
 *          compulsory version number change.  Be VERY careful with this
 *          function since it will return different values for OSE builds!
 */
VMMR3DECL(uint32_t)     SSMR3HandleRevision(PSSMHANDLE pSSM)
{
    if (pSSM->enmOp >= SSMSTATE_LOAD_PREP)
        return pSSM->u.Read.u32SvnRev;
#ifdef SSM_STANDALONE
    return 0;
#else
    return VMMGetSvnRev();
#endif
}


/**
 * Gets the VirtualBox version that created the saved state.
 *
 * @returns VBOX_FULL_VERSION style version number.
 *          Returns UINT32_MAX if unknown or somehow out of range.
 *
 * @param   pSSM            The saved state handle.
 *
 * @remarks This method should ONLY be used for hacks when loading OLDER saved
 *          state that have data layout or semantic changes without the
 *          compulsory version number change.
 */
VMMR3DECL(uint32_t)     SSMR3HandleVersion(PSSMHANDLE pSSM)
{
    if (pSSM->enmOp >= SSMSTATE_LOAD_PREP)
    {
        if (    !pSSM->u.Read.u16VerMajor
            &&  !pSSM->u.Read.u16VerMinor
            &&  !pSSM->u.Read.u32VerBuild)
            return UINT32_MAX;
        AssertReturn(pSSM->u.Read.u16VerMajor <=   0xff, UINT32_MAX);
        AssertReturn(pSSM->u.Read.u16VerMinor <=   0xff, UINT32_MAX);
        AssertReturn(pSSM->u.Read.u32VerBuild <= 0xffff, UINT32_MAX);
        return VBOX_FULL_VERSION_MAKE(pSSM->u.Read.u16VerMajor, pSSM->u.Read.u16VerMinor, pSSM->u.Read.u32VerBuild);
    }
    return VBOX_FULL_VERSION;
}


/**
 * Get the host OS and architecture where the saved state was created.
 *
 * @returns Pointer to a read only string.  When known, this is on the os.arch
 *          form.  If we don't know, it's an empty string.
 * @param   pSSM            The saved state handle.
 *
 * @remarks This method should ONLY be used for hacks when loading OLDER saved
 *          state that have data layout or semantic changes without the
 *          compulsory version number change.
 */
VMMR3DECL(const char *) SSMR3HandleHostOSAndArch(PSSMHANDLE pSSM)
{
    if (pSSM->enmOp >= SSMSTATE_LOAD_PREP)
        return pSSM->u.Read.szHostOSAndArch;
    return KBUILD_TARGET "." KBUILD_TARGET_ARCH;
}


#ifdef DEBUG
/**
 * Gets current data offset, relative to the start of the unit - only for debugging
 */
VMMR3DECL(uint64_t) SSMR3HandleTellInUnit(PSSMHANDLE pSSM)
{
    return ssmR3StrmTell(&pSSM->Strm) - pSSM->offUnitUser;
}
#endif


#ifndef SSM_STANDALONE
/**
 * Asynchronously cancels the current SSM operation ASAP.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_SSM_NO_PENDING_OPERATION if nothing around that can be
 *          cancelled.
 * @retval  VERR_SSM_ALREADY_CANCELLED if the operation as already been
 *          cancelled.
 *
 * @param   pUVM            The VM handle.
 *
 * @thread  Any.
 */
VMMR3DECL(int) SSMR3Cancel(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    int rc = RTCritSectEnter(&pVM->ssm.s.CancelCritSect);
    AssertRCReturn(rc, rc);

    PSSMHANDLE pSSM = pVM->ssm.s.pSSM;
    if (pSSM)
    {
        uint32_t u32Old;
        if (ASMAtomicCmpXchgExU32(&pSSM->fCancelled, SSMHANDLE_CANCELLED, SSMHANDLE_OK, &u32Old))
        {
            LogRel(("SSM: Cancelled pending operation\n"));
            rc = VINF_SUCCESS;
        }
        else if (u32Old == SSMHANDLE_CANCELLED)
            rc = VERR_SSM_ALREADY_CANCELLED;
        else
        {
            AssertLogRelMsgFailed(("fCancelled=%RX32 enmOp=%d\n", u32Old, pSSM->enmOp));
            rc = VERR_SSM_IPE_3;
        }
    }
    else
        rc = VERR_SSM_NO_PENDING_OPERATION;

    RTCritSectLeave(&pVM->ssm.s.CancelCritSect);
    return rc;
}
#endif /* !SSM_STANDALONE */

