/* $Id: DevVirtioSCSI.cpp 108103 2025-01-28 15:44:18Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox storage devices - Virtio SCSI Driver
 *
 * Log-levels used:
 *    - Level 1:   The most important (but usually rare) things to note
 *    - Level 2:   SCSI command logging
 *    - Level 3:   Vector and I/O transfer summary (shows what client sent an expects and fulfillment)
 *    - Level 6:   Device <-> Guest Driver negotation, traffic, notifications and state handling
 *    - Level 12:  Brief formatted hex dumps of I/O data
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
//#define LOG_GROUP LOG_GROUP_DRV_SCSI
#define LOG_GROUP LOG_GROUP_DEV_VIRTIO

#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pdmstorageifs.h>
#include <VBox/vmm/pdmcritsect.h>
#include <VBox/AssertGuest.h>
#include <VBox/msi.h>
#include <VBox/version.h>
#include <VBox/log.h>
#include <iprt/errcore.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <VBox/sup.h>
#include "../build/VBoxDD.h"
#include <VBox/scsi.h>
#ifdef IN_RING3
# include <iprt/alloc.h>
# include <iprt/memcache.h>
# include <iprt/semaphore.h>
# include <iprt/sg.h>
# include <iprt/param.h>
# include <iprt/uuid.h>
#endif
#include "../VirtIO/VirtioCore.h"

#include "VBoxSCSI.h"
#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The current saved state version. */
#define VIRTIOSCSI_SAVED_STATE_VERSION          UINT32_C(1)


#define LUN0    0
/** @name VirtIO 1.0 SCSI Host feature bits (See VirtIO 1.0 specification, Section 5.6.3)
 * @{  */
#define VIRTIO_SCSI_F_INOUT                RT_BIT_64(0)         /** Request is device readable AND writeable         */
#define VIRTIO_SCSI_F_HOTPLUG              RT_BIT_64(1)         /** Host allows hotplugging SCSI LUNs & targets      */
#define VIRTIO_SCSI_F_CHANGE               RT_BIT_64(2)         /** Host LUNs chgs via VIRTIOSCSI_T_PARAM_CHANGE evt */
#define VIRTIO_SCSI_F_T10_PI               RT_BIT_64(3)         /** Add T10 port info (DIF/DIX) in SCSI req hdr      */
/** @} */


#define VIRTIOSCSI_HOST_SCSI_FEATURES_ALL \
    (VIRTIO_SCSI_F_INOUT | VIRTIO_SCSI_F_HOTPLUG | VIRTIO_SCSI_F_CHANGE | VIRTIO_SCSI_F_T10_PI)

#define VIRTIOSCSI_HOST_SCSI_FEATURES_NONE          0

#define VIRTIOSCSI_HOST_SCSI_FEATURES_OFFERED       VIRTIOSCSI_HOST_SCSI_FEATURES_NONE

#define VIRTIOSCSI_REQ_VIRTQ_CNT                    4           /**< T.B.D. Consider increasing                      */
#define VIRTIOSCSI_VIRTQ_CNT                        (VIRTIOSCSI_REQ_VIRTQ_CNT + 2)
#define VIRTIOSCSI_MAX_TARGETS                      256         /**< T.B.D. Figure out a a good value for this.      */
#define VIRTIOSCSI_MAX_LUN                          1           /**< VirtIO specification, section 5.6.4             */
#define VIRTIOSCSI_MAX_COMMANDS_PER_LUN             128         /**< T.B.D. What is a good value for this?           */
#define VIRTIOSCSI_MAX_SEG_COUNT                    126         /**< T.B.D. What is a good value for this?           */
#define VIRTIOSCSI_MAX_SECTORS_HINT                 0x10000     /**< VirtIO specification, section 5.6.4             */
#define VIRTIOSCSI_MAX_CHANNEL_HINT                 0           /**< VirtIO specification, section 5.6.4 should be 0 */

#define PCI_DEVICE_ID_VIRTIOSCSI_HOST               0x1048      /**< Informs guest driver of type of VirtIO device   */
#define PCI_CLASS_BASE_MASS_STORAGE                 0x01        /**< PCI Mass Storage device class                   */
#define PCI_CLASS_SUB_SCSI_STORAGE_CONTROLLER       0x00        /**< PCI SCSI Controller subclass                    */
#define PCI_CLASS_PROG_UNSPECIFIED                  0x00        /**< Programming interface. N/A.                     */
#define VIRTIOSCSI_PCI_CLASS                        0x01        /**< Base class Mass Storage?                        */

#define VIRTIOSCSI_SENSE_SIZE_DEFAULT               96          /**< VirtIO 1.0: 96 on reset, guest can change       */
#define VIRTIOSCSI_SENSE_SIZE_MAX                   4096        /**< Picked out of thin air by bird.                 */
#define VIRTIOSCSI_CDB_SIZE_DEFAULT                 32          /**< VirtIO 1.0: 32 on reset, guest can change       */
#define VIRTIOSCSI_CDB_SIZE_MAX                     255         /**< Picked out of thin air by bird.                 */
#define VIRTIOSCSI_PI_BYTES_IN                      1           /**< Value TBD (see section 5.6.6.1)                 */
#define VIRTIOSCSI_PI_BYTES_OUT                     1           /**< Value TBD (see section 5.6.6.1)                 */
#define VIRTIOSCSI_DATA_OUT                         512         /**< Value TBD (see section 5.6.6.1)                 */

/**
 * VirtIO SCSI Host Device device-specific queue indicies.
 * (Note: # of request queues is determined by virtio_scsi_config.num_queues. VirtIO 1.0, 5.6.4)
 */
#define CONTROLQ_IDX                                0           /**< VirtIO Spec-defined Index of control queue      */
#define EVENTQ_IDX                                  1           /**< VirtIO Spec-defined Index of event queue        */
#define VIRTQ_REQ_BASE                              2           /**< VirtIO Spec-defined base index of req. queues   */

#define VIRTQNAME(uVirtqNbr) (pThis->aszVirtqNames[uVirtqNbr])  /**< Macro to get queue name from its index          */
#define CBVIRTQNAME(uVirtqNbr) RTStrNLen(VIRTQNAME(uVirtqNbr), sizeof(VIRTQNAME(uVirtqNbr)))

#define IS_REQ_VIRTQ(uVirtqNbr) (uVirtqNbr >= VIRTQ_REQ_BASE && uVirtqNbr < VIRTIOSCSI_VIRTQ_CNT)

#define VIRTIO_IS_IN_DIRECTION(pMediaExTxDirEnumValue) \
    ((pMediaExTxDirEnumValue) == PDMMEDIAEXIOREQSCSITXDIR_FROM_DEVICE)

#define VIRTIO_IS_OUT_DIRECTION(pMediaExTxDirEnumValue) \
    ((pMediaExTxDirEnumValue) == PDMMEDIAEXIOREQSCSITXDIR_TO_DEVICE)

#define IS_VIRTQ_EMPTY(pDevIns, pVirtio, uVirtqNbr) \
            (virtioCoreVirtqAvailBufCount(pDevIns, pVirtio, uVirtqNbr) == 0)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * VirtIO SCSI Host Device device-specific configuration (see VirtIO 1.0, section 5.6.4)
 * VBox VirtIO core issues callback to this VirtIO device-specific implementation to handle
 * MMIO accesses to device-specific configuration parameters.
 */
typedef struct virtio_scsi_config
{
    uint32_t uNumVirtqs;                                        /**< num_queues       \# of req q's exposed by dev   */
    uint32_t uSegMax;                                           /**< seg_max          Max \# of segs allowed in cmd  */
    uint32_t uMaxSectors;                                       /**< max_sectors      Hint to guest max xfer to use  */
    uint32_t uCmdPerLun;                                        /**< cmd_per_lun      Max \# of link cmd sent per lun */
    uint32_t uEventInfoSize;                                    /**< event_info_size  Fill max, evtq bufs            */
    uint32_t uSenseSize;                                        /**< sense_size       Max sense data size dev writes */
    uint32_t uCdbSize;                                          /**< cdb_size         Max CDB size driver writes     */
    uint16_t uMaxChannel;                                       /**< max_channel      Hint to guest driver           */
    uint16_t uMaxTarget;                                        /**< max_target       Hint to guest driver           */
    uint32_t uMaxLun;                                           /**< max_lun          Hint to guest driver           */
} VIRTIOSCSI_CONFIG_T, PVIRTIOSCSI_CONFIG_T;

/** @name VirtIO 1.0 SCSI Host Device device specific control types
 * @{  */
#define VIRTIOSCSI_T_NO_EVENT                       0
#define VIRTIOSCSI_T_TRANSPORT_RESET                1
#define VIRTIOSCSI_T_ASYNC_NOTIFY                   2           /**< Asynchronous notification                       */
#define VIRTIOSCSI_T_PARAM_CHANGE                   3
/** @} */

/**
 * Device operation: eventq
 */
#define VIRTIOSCSI_T_EVENTS_MISSED                  UINT32_C(0x80000000)
typedef struct virtio_scsi_event
{
    // Device-writable part
    uint32_t uEvent;                                            /**< event                                           */
    uint8_t  abVirtioLun[8];                                    /**< lun                                             */
    uint32_t uReason;                                           /**< reason                                          */
} VIRTIOSCSI_EVENT_T, *PVIRTIOSCSI_EVENT_T;

/** @name VirtIO 1.0 SCSI Host Device device specific event types
 * @{  */
#define VIRTIOSCSI_EVT_RESET_HARD                   0           /**<                                                 */
#define VIRTIOSCSI_EVT_RESET_RESCAN                 1           /**<                                                 */
#define VIRTIOSCSI_EVT_RESET_REMOVED                2           /**<                                                 */
/** @} */

/**
 * Device operation: reqestq
 */
#pragma pack(1)
typedef struct REQ_CMD_HDR_T
{
    uint8_t  abVirtioLun[8];                                    /**< lun                                          */
    uint64_t uId;                                               /**< id                                           */
    uint8_t  uTaskAttr;                                         /**< task_attr                                    */
    uint8_t  uPrio;                                             /**< prio                                         */
    uint8_t  uCrn;                                              /**< crn                                          */
} REQ_CMD_HDR_T;
#pragma pack()
AssertCompileSize(REQ_CMD_HDR_T, 19);

typedef struct REQ_CMD_PI_T
{
    uint32_t uPiBytesOut;                                       /**< pi_bytesout                                  */
    uint32_t uPiBytesIn;                                        /**< pi_bytesin                                   */
} REQ_CMD_PI_T;
AssertCompileSize(REQ_CMD_PI_T, 8);

typedef struct REQ_RESP_HDR_T
{
    uint32_t cbSenseLen;                                        /**< sense_len                                    */
    uint32_t uResidual;                                         /**< residual                                     */
    uint16_t uStatusQualifier;                                  /**< status_qualifier                             */
    uint8_t  uStatus;                                           /**< status            SCSI status code           */
    uint8_t  uResponse;                                         /**< response                                     */
} REQ_RESP_HDR_T;
AssertCompileSize(REQ_RESP_HDR_T, 12);

#pragma pack(1)
typedef struct VIRTIOSCSI_REQ_CMD_T
{
    /** Device-readable section
     * @{ */
    REQ_CMD_HDR_T  ReqHdr;
    uint8_t  uCdb[1];                                           /**< cdb                                          */

    REQ_CMD_PI_T piHdr;                                         /**< T10 Pi block integrity (optional feature)    */
    uint8_t  uPiOut[1];                                         /**< pi_out[]          T10 pi block integrity     */
    uint8_t  uDataOut[1];                                       /**< dataout                                      */
    /** @} */

    /** @name Device writable section
     * @{ */
    REQ_RESP_HDR_T respHdr;
    uint8_t  uSense[1];                                         /**< sense                                        */
    uint8_t  uPiIn[1];                                          /**< pi_in[]           T10 Pi block integrity     */
    uint8_t  uDataIn[1];                                        /**< detain;                                      */
    /** @} */
} VIRTIOSCSI_REQ_CMD_T, *PVIRTIOSCSI_REQ_CMD_T;
#pragma pack()
AssertCompileSize(VIRTIOSCSI_REQ_CMD_T, 19+8+12+6);

/** @name VirtIO 1.0 SCSI Host Device Req command-specific response values
 * @{  */
#define VIRTIOSCSI_S_OK                             0           /**< control, command                                 */
#define VIRTIOSCSI_S_OVERRUN                        1           /**< control                                          */
#define VIRTIOSCSI_S_ABORTED                        2           /**< control                                          */
#define VIRTIOSCSI_S_BAD_TARGET                     3           /**< control, command                                 */
#define VIRTIOSCSI_S_RESET                          4           /**< control                                          */
#define VIRTIOSCSI_S_BUSY                           5           /**< control, command                                 */
#define VIRTIOSCSI_S_TRANSPORT_FAILURE              6           /**< control, command                                 */
#define VIRTIOSCSI_S_TARGET_FAILURE                 7           /**< control, command                                 */
#define VIRTIOSCSI_S_NEXUS_FAILURE                  8           /**< control, command                                 */
#define VIRTIOSCSI_S_FAILURE                        9           /**< control, command                                 */
#define VIRTIOSCSI_S_INCORRECT_LUN                  12          /**< command                                          */
/** @} */

/** @name VirtIO 1.0 SCSI Host Device command-specific task_attr values
 * @{  */
#define VIRTIOSCSI_S_SIMPLE                         0           /**<                                                  */
#define VIRTIOSCSI_S_ORDERED                        1           /**<                                                  */
#define VIRTIOSCSI_S_HEAD                           2           /**<                                                  */
#define VIRTIOSCSI_S_ACA                            3           /**<                                                  */
/** @} */

/**
 * VirtIO 1.0 SCSI Host Device Control command before we know type (5.6.6.2)
 */
typedef struct VIRTIOSCSI_CTRL_T
{
    uint32_t uType;
} VIRTIOSCSI_CTRL_T, *PVIRTIOSCSI_CTRL_T;

/** @name VirtIO 1.0 SCSI Host Device command-specific TMF values
 * @{  */
#define VIRTIOSCSI_T_TMF                            0           /**<                                                  */
#define VIRTIOSCSI_T_TMF_ABORT_TASK                 0           /**<                                                  */
#define VIRTIOSCSI_T_TMF_ABORT_TASK_SET             1           /**<                                                  */
#define VIRTIOSCSI_T_TMF_CLEAR_ACA                  2           /**<                                                  */
#define VIRTIOSCSI_T_TMF_CLEAR_TASK_SET             3           /**<                                                  */
#define VIRTIOSCSI_T_TMF_I_T_NEXUS_RESET            4           /**<                                                  */
#define VIRTIOSCSI_T_TMF_LOGICAL_UNIT_RESET         5           /**<                                                  */
#define VIRTIOSCSI_T_TMF_QUERY_TASK                 6           /**<                                                  */
#define VIRTIOSCSI_T_TMF_QUERY_TASK_SET             7           /**<                                                  */
/** @} */

#pragma pack(1)
typedef struct VIRTIOSCSI_CTRL_TMF_T
{
    uint32_t uType;                                             /**< type                                             */
    uint32_t uSubtype;                                          /**< subtype                                          */
    uint8_t  abScsiLun[8];                                      /**< lun                                              */
    uint64_t uId;                                               /**< id                                               */
} VIRTIOSCSI_CTRL_TMF_T, *PVIRTIOSCSI_CTRL_TMF_T;
#pragma pack()
AssertCompileSize(VIRTIOSCSI_CTRL_TMF_T, 24);

/** VirtIO 1.0 section 5.6.6.2, CTRL TMF response is an 8-bit status */

/** @name VirtIO 1.0 SCSI Host Device device specific tmf control response values
 * @{  */
#define VIRTIOSCSI_S_FUNCTION_COMPLETE              0           /**<                                                   */
#define VIRTIOSCSI_S_FUNCTION_SUCCEEDED             10          /**<                                                   */
#define VIRTIOSCSI_S_FUNCTION_REJECTED              11          /**<                                                   */
/** @} */

#define VIRTIOSCSI_T_AN_QUERY                       1           /**< Asynchronous notification query                    */
#define VIRTIOSCSI_T_AN_SUBSCRIBE                   2           /**< Asynchronous notification subscription             */

#pragma pack(1)
typedef struct VIRTIOSCSI_CTRL_AN_T
{
    uint32_t  uType;                                            /**< type                                              */
    uint8_t   abScsiLun[8];                                     /**< lun                                               */
    uint32_t  fEventsRequested;                                 /**< event_requested                                   */
}  VIRTIOSCSI_CTRL_AN_T, *PVIRTIOSCSI_CTRL_AN_T;
#pragma pack()
AssertCompileSize(VIRTIOSCSI_CTRL_AN_T, 16);

/** VirtIO 1.0, Section 5.6.6.2, CTRL AN response is 4-byte evt mask + 8-bit status */

typedef union VIRTIO_SCSI_CTRL_UNION_T
{
    VIRTIOSCSI_CTRL_T       Type;
    VIRTIOSCSI_CTRL_TMF_T   Tmf;
    VIRTIOSCSI_CTRL_AN_T    AsyncNotify;
    uint8_t                 ab[24];
} VIRTIO_SCSI_CTRL_UNION_T, *PVIRTIO_SCSI_CTRL_UNION_T;
AssertCompile(sizeof(VIRTIO_SCSI_CTRL_UNION_T) == 24); /* VIRTIOSCSI_CTRL_T forces 4 byte alignment, the other two are byte packed. */

/** @name VirtIO 1.0 SCSI Host Device device specific tmf control response values
 * @{  */
#define VIRTIOSCSI_EVT_ASYNC_OPERATIONAL_CHANGE  2              /**<                                                   */
#define VIRTIOSCSI_EVT_ASYNC_POWER_MGMT          4              /**<                                                   */
#define VIRTIOSCSI_EVT_ASYNC_EXTERNAL_REQUEST    8              /**<                                                   */
#define VIRTIOSCSI_EVT_ASYNC_MEDIA_CHANGE        16             /**<                                                   */
#define VIRTIOSCSI_EVT_ASYNC_MULTI_HOST          32             /**<                                                   */
#define VIRTIOSCSI_EVT_ASYNC_DEVICE_BUSY         64             /**<                                                   */
/** @} */

#define SUBSCRIBABLE_EVENTS \
    (  VIRTIOSCSI_EVT_ASYNC_OPERATIONAL_CHANGE \
     | VIRTIOSCSI_EVT_ASYNC_POWER_MGMT \
     | VIRTIOSCSI_EVT_ASYNC_EXTERNAL_REQUEST \
     | VIRTIOSCSI_EVT_ASYNC_MEDIA_CHANGE \
     | VIRTIOSCSI_EVT_ASYNC_MULTI_HOST \
     | VIRTIOSCSI_EVT_ASYNC_DEVICE_BUSY )

#define SUPPORTED_EVENTS            0                           /* TBD */

/**
 * Worker thread context, shared state.
 */
typedef struct VIRTIOSCSIWORKER
{
    SUPSEMEVENT                     hEvtProcess;                /**< handle of associated sleep/wake-up semaphore      */
    bool volatile                   fSleeping;                  /**< Flags whether worker thread is sleeping or not    */
    bool volatile                   fNotified;                  /**< Flags whether worker thread notified              */
} VIRTIOSCSIWORKER;
/** Pointer to a VirtIO SCSI worker. */
typedef VIRTIOSCSIWORKER *PVIRTIOSCSIWORKER;

/**
 * Worker thread context, ring-3 state.
 */
typedef struct VIRTIOSCSIWORKERR3
{
    R3PTRTYPE(PPDMTHREAD)           pThread;                    /**< pointer to worker thread's handle                 */
    RTCRITSECT                      CritSectVirtq;              /**< Protecting the virtq against concurrent thread access. */
    uint16_t                        auRedoDescs[VIRTQ_SIZE];    /**< List of previously suspended reqs to re-submit    */
    uint16_t                        cRedoDescs;                 /**< Number of redo desc chain head desc idxes in list */
} VIRTIOSCSIWORKERR3;
/** Pointer to a VirtIO SCSI worker. */
typedef VIRTIOSCSIWORKERR3 *PVIRTIOSCSIWORKERR3;

/**
 * State of a target attached to the VirtIO SCSI Host
 */
typedef struct VIRTIOSCSITARGET
{
    /** The ring-3 device instance so we can easily get our bearings. */
    PPDMDEVINSR3                    pDevIns;

    /** Pointer to attached driver's base interface. */
    R3PTRTYPE(PPDMIBASE)            pDrvBase;

    /** Target number (PDM LUN) */
    uint32_t                        uTarget;

    /** Target Description */
    R3PTRTYPE(char *)               pszTargetName;

    /** Target base interface. */
    PDMIBASE                        IBase;

    /** Flag whether device is present. */
    bool                            fPresent;

    /** Media port interface. */
    PDMIMEDIAPORT                   IMediaPort;

    /** Pointer to the attached driver's media interface. */
    R3PTRTYPE(PPDMIMEDIA)           pDrvMedia;

    /** Extended media port interface. */
    PDMIMEDIAEXPORT                 IMediaExPort;

    /** Pointer to the attached driver's extended media interface. */
    R3PTRTYPE(PPDMIMEDIAEX)         pDrvMediaEx;

    /** Status LED interface */
    PDMILEDPORTS                    ILed;

    /** The status LED state for this device. */
    PDMLED                          led;

} VIRTIOSCSITARGET, *PVIRTIOSCSITARGET;

/**
 * VirtIO Host SCSI device state, shared edition.
 *
 * @extends     VIRTIOCORE
 */
typedef struct VIRTIOSCSI
{
    /** The core virtio state.   */
    VIRTIOCORE                      Virtio;

    /** VirtIO Host SCSI device runtime configuration parameters */
    VIRTIOSCSI_CONFIG_T             virtioScsiConfig;

    bool                            fBootable;
    bool                            afPadding0[3];

    /** Number of targets in paTargetInstances. */
    uint32_t                        cTargets;

    /** Per device-bound virtq worker-thread contexts (eventq slot unused) */
    VIRTIOSCSIWORKER                aWorkers[VIRTIOSCSI_VIRTQ_CNT];

    /** Instance name */
    char                            szInstance[16];

    /** Device-specific spec-based VirtIO VIRTQNAMEs */
    char                            aszVirtqNames[VIRTIOSCSI_VIRTQ_CNT][VIRTIO_MAX_VIRTQ_NAME_SIZE];

    /** Track which VirtIO queues we've attached to */
    bool                            afVirtqAttached[VIRTIOSCSI_VIRTQ_CNT];

    /** Set if events missed due to lack of bufs avail on eventq */
    bool                            fEventsMissed;

    /** Explicit alignment padding. */
    bool                            afPadding1[2];

    /** Mask of VirtIO Async Event types this device will deliver */
    uint32_t                        fAsyncEvtsEnabled;

    /** Total number of requests active across all targets */
    volatile uint32_t               cActiveReqs;


    /** True if the guest/driver and VirtIO framework are in the ready state */
    uint32_t                        fVirtioReady;

    /** True if VIRTIO_SCSI_F_T10_PI was negotiated */
    uint32_t                        fHasT10pi;

    /** True if VIRTIO_SCSI_F_HOTPLUG was negotiated */
    uint32_t                        fHasHotplug;

    /** True if VIRTIO_SCSI_F_INOUT was negotiated */
    uint32_t                        fHasInOutBufs;

    /** True if VIRTIO_SCSI_F_CHANGE was negotiated */
    uint32_t                        fHasLunChange;

    /** True if in the process of resetting */
    uint32_t                        fResetting;

} VIRTIOSCSI;
/** Pointer to the shared state of the VirtIO Host SCSI device. */
typedef VIRTIOSCSI *PVIRTIOSCSI;


/**
 * VirtIO Host SCSI device state, ring-3 edition.
 *
 * @extends     VIRTIOCORER3
 */
typedef struct VIRTIOSCSIR3
{
    /** The core virtio ring-3 state. */
    VIRTIOCORER3                    Virtio;

    /** Array of per-target data. */
    R3PTRTYPE(PVIRTIOSCSITARGET)    paTargetInstances;

    /** Per device-bound virtq worker-thread contexts (eventq slot unused) */
    VIRTIOSCSIWORKERR3              aWorkers[VIRTIOSCSI_VIRTQ_CNT];

    /** Device base interface. */
    PDMIBASE                        IBase;

    /** Pointer to the device instance.
     * @note Only used in interface callbacks. */
    PPDMDEVINSR3                    pDevIns;

    /** Status Target: LEDs port interface. */
    PDMILEDPORTS                    ILeds;

    /** IMediaExPort: Media ejection notification */
    R3PTRTYPE(PPDMIMEDIANOTIFY)     pMediaNotify;

    /** Virtq to send tasks to R3. - HC ptr */
    R3PTRTYPE(PPDMQUEUE)            pNotifierVirtqR3;

    /** True if in the process of quiescing I/O */
    uint32_t                        fQuiescing;

    /** For which purpose we're quiescing. */
    VIRTIOVMSTATECHANGED            enmQuiescingFor;

} VIRTIOSCSIR3;
/** Pointer to the ring-3 state of the VirtIO Host SCSI device. */
typedef VIRTIOSCSIR3 *PVIRTIOSCSIR3;


/**
 * VirtIO Host SCSI device state, ring-0 edition.
 */
typedef struct VIRTIOSCSIR0
{
    /** The core virtio ring-0 state. */
    VIRTIOCORER0                    Virtio;
} VIRTIOSCSIR0;
/** Pointer to the ring-0 state of the VirtIO Host SCSI device. */
typedef VIRTIOSCSIR0 *PVIRTIOSCSIR0;


/**
 * VirtIO Host SCSI device state, raw-mode edition.
 */
typedef struct VIRTIOSCSIRC
{
    /** The core virtio raw-mode state. */
    VIRTIOCORERC                    Virtio;
} VIRTIOSCSIRC;
/** Pointer to the ring-0 state of the VirtIO Host SCSI device. */
typedef VIRTIOSCSIRC *PVIRTIOSCSIRC;


/** @typedef VIRTIOSCSICC
 * The instance data for the current context. */
typedef CTX_SUFF(VIRTIOSCSI) VIRTIOSCSICC;
/** @typedef PVIRTIOSCSICC
 * Pointer to the instance data for the current context. */
typedef CTX_SUFF(PVIRTIOSCSI) PVIRTIOSCSICC;


/**
 * Request structure for IMediaEx (Associated Interfaces implemented by DrvSCSI)
 * @note cbIn, cbOUt, cbDataOut mostly for debugging
 */
typedef struct VIRTIOSCSIREQ
{
    PDMMEDIAEXIOREQ                hIoReq;                      /**< Handle of I/O request                             */
    PVIRTIOSCSITARGET              pTarget;                     /**< Target                                            */
    uint16_t                       uVirtqNbr;                   /**< Index of queue this request arrived on            */
    PVIRTQBUF                      pVirtqBuf;                   /**< Prepared desc chain pulled from virtq avail ring  */
    size_t                         cbDataIn;                    /**< size of datain buffer                             */
    size_t                         cbDataOut;                   /**< size of dataout buffer                            */
    uint16_t                       uDataInOff;                  /**< Fixed size of respHdr + sense (precede datain)    */
    uint16_t                       uDataOutOff;                 /**< Fixed size of reqhdr + cdb (precede dataout)      */
    uint32_t                       cbSenseAlloc;                /**< Size of sense buffer                              */
    size_t                         cbSenseLen;                  /**< Receives \# bytes written into sense buffer       */
    uint8_t                       *pbSense;                     /**< Pointer to R3 sense buffer                        */
    PDMMEDIAEXIOREQSCSITXDIR       enmTxDir;                    /**< Receives transfer direction of I/O req            */
    uint8_t                        uStatus;                     /**< SCSI status code                                  */
} VIRTIOSCSIREQ;
typedef VIRTIOSCSIREQ *PVIRTIOSCSIREQ;


/**
 * callback_method_impl{VIRTIOCORER0,pfnVirtqNotified}
 * @todo this causes burn if I prefix with at-sign. This callback is in VIRTIOCORER0 and VIRTIOCORER3
 */
static DECLCALLBACK(void) virtioScsiNotified(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t uVirtqNbr)
{

    RT_NOREF(pVirtio);
    PVIRTIOSCSI pThis   = PDMDEVINS_2_DATA(pDevIns, PVIRTIOSCSI);

    AssertReturnVoid(uVirtqNbr < VIRTIOSCSI_VIRTQ_CNT);
    PVIRTIOSCSIWORKER pWorker = &pThis->aWorkers[uVirtqNbr];

#if defined (IN_RING3) && defined (LOG_ENABLED)
   RTLogFlush(NULL);
#endif

    if (uVirtqNbr == CONTROLQ_IDX || IS_REQ_VIRTQ(uVirtqNbr))
    {
        Log6Func(("%s has available data\n", VIRTQNAME(uVirtqNbr)));
        /* Wake queue's worker thread up if sleeping */
        if (!ASMAtomicXchgBool(&pWorker->fNotified, true))
        {
            if (ASMAtomicReadBool(&pWorker->fSleeping))
            {
                Log6Func(("waking %s worker.\n", VIRTQNAME(uVirtqNbr)));
                int rc = PDMDevHlpSUPSemEventSignal(pDevIns, pWorker->hEvtProcess);
                AssertRC(rc);
            }
        }
    }
    else if (uVirtqNbr == EVENTQ_IDX)
    {
        Log3Func(("Driver queued buffer(s) to %s\n", VIRTQNAME(uVirtqNbr)));
//        if (ASMAtomicXchgBool(&pThis->fEventsMissed, false))
//            virtioScsiR3ReportEventsMissed(pDevIns, pThis, 0);
    }
    else
        LogFunc(("Unexpected queue idx (ignoring): %d\n", uVirtqNbr));
}


#ifdef IN_RING3 /* spans most of the file, at the moment. */


DECLINLINE(void) virtioScsiSetVirtqNames(PVIRTIOSCSI pThis)
{
    RTStrCopy(pThis->aszVirtqNames[CONTROLQ_IDX], VIRTIO_MAX_VIRTQ_NAME_SIZE, "controlq");
    RTStrCopy(pThis->aszVirtqNames[EVENTQ_IDX],   VIRTIO_MAX_VIRTQ_NAME_SIZE, "eventq");
    for (uint16_t uVirtqNbr = VIRTQ_REQ_BASE; uVirtqNbr < VIRTQ_REQ_BASE + VIRTIOSCSI_REQ_VIRTQ_CNT; uVirtqNbr++)
        RTStrPrintf(pThis->aszVirtqNames[uVirtqNbr], VIRTIO_MAX_VIRTQ_NAME_SIZE,
                    "requestq<%d>", uVirtqNbr - VIRTQ_REQ_BASE);
}

#ifdef LOG_ENABLED


DECLINLINE(const char *) virtioGetTxDirText(uint32_t enmTxDir)
{
    switch (enmTxDir)
    {
        case PDMMEDIAEXIOREQSCSITXDIR_UNKNOWN:          return "<UNKNOWN>";
        case PDMMEDIAEXIOREQSCSITXDIR_FROM_DEVICE:      return "<DEV-TO-GUEST>";
        case PDMMEDIAEXIOREQSCSITXDIR_TO_DEVICE:        return "<GUEST-TO-DEV>";
        case PDMMEDIAEXIOREQSCSITXDIR_NONE:             return "<NONE>";
        default:                                        return "<BAD ENUM>";
    }
}

DECLINLINE(const char *) virtioGetTMFTypeText(uint32_t uSubType)
{
    switch (uSubType)
    {
        case VIRTIOSCSI_T_TMF_ABORT_TASK:               return "ABORT TASK";
        case VIRTIOSCSI_T_TMF_ABORT_TASK_SET:           return "ABORT TASK SET";
        case VIRTIOSCSI_T_TMF_CLEAR_ACA:                return "CLEAR ACA";
        case VIRTIOSCSI_T_TMF_CLEAR_TASK_SET:           return "CLEAR TASK SET";
        case VIRTIOSCSI_T_TMF_I_T_NEXUS_RESET:          return "I T NEXUS RESET";
        case VIRTIOSCSI_T_TMF_LOGICAL_UNIT_RESET:       return "LOGICAL UNIT RESET";
        case VIRTIOSCSI_T_TMF_QUERY_TASK:               return "QUERY TASK";
        case VIRTIOSCSI_T_TMF_QUERY_TASK_SET:           return "QUERY TASK SET";
        default:                                        return "<unknown>";
    }
}

DECLINLINE(const char *) virtioGetReqRespText(uint32_t vboxRc)
{
    switch (vboxRc)
    {
        case VIRTIOSCSI_S_OK:                           return "OK/COMPLETE";
        case VIRTIOSCSI_S_OVERRUN:                      return "OVERRRUN";
        case VIRTIOSCSI_S_ABORTED:                      return "ABORTED";
        case VIRTIOSCSI_S_BAD_TARGET:                   return "BAD TARGET";
        case VIRTIOSCSI_S_RESET:                        return "RESET";
        case VIRTIOSCSI_S_TRANSPORT_FAILURE:            return "TRANSPORT FAILURE";
        case VIRTIOSCSI_S_TARGET_FAILURE:               return "TARGET FAILURE";
        case VIRTIOSCSI_S_NEXUS_FAILURE:                return "NEXUS FAILURE";
        case VIRTIOSCSI_S_BUSY:                         return "BUSY";
        case VIRTIOSCSI_S_FAILURE:                      return "FAILURE";
        case VIRTIOSCSI_S_INCORRECT_LUN:                return "INCORRECT LUN";
        case VIRTIOSCSI_S_FUNCTION_SUCCEEDED:           return "FUNCTION SUCCEEDED";
        case VIRTIOSCSI_S_FUNCTION_REJECTED:            return "FUNCTION REJECTED";
        default:                                        return "<unknown>";
    }
}

DECLINLINE(void) virtioGetControlAsyncMaskText(char *pszOutput, uint32_t cbOutput, uint32_t fAsyncTypesMask)
{
    RTStrPrintf(pszOutput, cbOutput, "%s%s%s%s%s%s",
                fAsyncTypesMask & VIRTIOSCSI_EVT_ASYNC_OPERATIONAL_CHANGE ? "CHANGE_OPERATION  "   : "",
                fAsyncTypesMask & VIRTIOSCSI_EVT_ASYNC_POWER_MGMT         ? "POWER_MGMT  "         : "",
                fAsyncTypesMask & VIRTIOSCSI_EVT_ASYNC_EXTERNAL_REQUEST   ? "EXTERNAL_REQ  "       : "",
                fAsyncTypesMask & VIRTIOSCSI_EVT_ASYNC_MEDIA_CHANGE       ? "MEDIA_CHANGE  "       : "",
                fAsyncTypesMask & VIRTIOSCSI_EVT_ASYNC_MULTI_HOST         ? "MULTI_HOST  "         : "",
                fAsyncTypesMask & VIRTIOSCSI_EVT_ASYNC_DEVICE_BUSY        ? "DEVICE_BUSY  "        : "");
}

static uint8_t virtioScsiEstimateCdbLen(uint8_t uCmd, uint8_t cbMax)
{
    if (uCmd < 0x1f)
        return RT_MIN(6, cbMax);
    if (uCmd >= 0x20 && uCmd < 0x60)
        return RT_MIN(10, cbMax);
    if (uCmd >= 0x60 && uCmd < 0x80)
        return cbMax;
    if (uCmd >= 0x80 && uCmd < 0xa0)
        return RT_MIN(16, cbMax);
    if (uCmd >= 0xa0 && uCmd < 0xc0)
        return RT_MIN(12, cbMax);
    return cbMax;
}

#endif /* LOG_ENABLED */


/**
 * Wrapper around virtioCoreR3VirtqUsedBufPut() and virtioCoreVirtqUsedRingSync() doing some device locking.
 *
 * @param   pDevIns         The PDM device instance.
 * @param   pVirtio         Pointer to the shared virtio core structure.
 * @param   uVirtqNbr       The virtq number.
 * @param   pSgVirtReturn   The S/G buffer to return data from.
 * @param   pVirtqBuf       The virtq buffer.
 *
 * @note This is a temporary fix, see @bugref{9440#c164} on why it is necessary. Should be moved to VirtioCore or
 *       VirtioCore should be changed to not require locking.
 */
DECLINLINE(void) virtioScsiR3VirtqUsedBufPutAndSync(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t uVirtqNbr, PRTSGBUF pSgVirtReturn,
                                                    PVIRTQBUF pVirtqBuf)
{
    PVIRTIOSCSICC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIOSCSICC);

    RTCritSectEnter(&pThisCC->aWorkers[uVirtqNbr].CritSectVirtq);
    virtioCoreR3VirtqUsedBufPut(pThisCC->pDevIns, pVirtio, uVirtqNbr, pSgVirtReturn, pVirtqBuf, true /* fFence */);
    virtioCoreVirtqUsedRingSync(pThisCC->pDevIns, pVirtio, uVirtqNbr);
    RTCritSectLeave(&pThisCC->aWorkers[uVirtqNbr].CritSectVirtq);
}


/*
 * @todo Figure out how to implement this with R0 changes. Not used by current linux driver
 */

#if 0
static int virtioScsiR3SendEvent(PPDMDEVINS pDevIns, PVIRTIOSCSI pThis, uint16_t uTarget, uint32_t uEventType, uint32_t uReason)
{
    switch (uEventType)
    {
        case VIRTIOSCSI_T_NO_EVENT:
            Log6Func(("(target=%d, LUN=%d): Warning event info guest queued is shorter than configured\n", uTarget, LUN0));
            break;
        case VIRTIOSCSI_T_NO_EVENT | VIRTIOSCSI_T_EVENTS_MISSED:
            Log6Func(("(target=%d, LUN=%d): Warning driver that events were missed\n", uTarget, LUN0));
            break;
        case VIRTIOSCSI_T_TRANSPORT_RESET:
            switch (uReason)
            {
                case VIRTIOSCSI_EVT_RESET_REMOVED:
                    Log6Func(("(target=%d, LUN=%d): Target or LUN removed\n", uTarget, LUN0));
                    break;
                case VIRTIOSCSI_EVT_RESET_RESCAN:
                    Log6Func(("(target=%d, LUN=%d): Target or LUN added\n", uTarget, LUN0));
                    break;
                case VIRTIOSCSI_EVT_RESET_HARD:
                    Log6Func(("(target=%d, LUN=%d): Target was reset\n", uTarget, LUN0));
                    break;
            }
            break;
        case VIRTIOSCSI_T_ASYNC_NOTIFY:
        {
#ifdef LOG_ENABLED
            char szTypeText[128];
            virtioGetControlAsyncMaskText(szTypeText, sizeof(szTypeText), uReason);
            Log6Func(("(target=%d, LUN=%d): Delivering subscribed async notification %s\n", uTarget, LUN0, szTypeText));
#endif
            break;
        }
        case VIRTIOSCSI_T_PARAM_CHANGE:
            LogFunc(("(target=%d, LUN=%d): PARAM_CHANGE sense code: 0x%x sense qualifier: 0x%x\n",
                     uTarget, LUN0, uReason & 0xff, (uReason >> 8) & 0xff));
            break;
        default:
            Log6Func(("(target=%d, LUN=%d): Unknown event type: %d, ignoring\n", uTarget, LUN0, uEventType));
            return VINF_SUCCESS;
    }

    PVIRTQBUF pVirtqBuf = NULL;
    int rc = virtioCoreR3VirtqAvailBufGet(pDevIns, &pThis->Virtio, EVENTQ_IDX, &pVirtqBuf, true);
    if (rc == VERR_NOT_AVAILABLE)
    {
        LogFunc(("eventq is empty, events missed (driver didn't preload queue)!\n"));
        ASMAtomicWriteBool(&pThis->fEventsMissed, true);
        return VINF_SUCCESS;
    }
    AssertRCReturn(rc, rc);

    VIRTIOSCSI_EVENT_T Event;
    Event.uEvent = uEventType;
    Event.uReason = uReason;
    Event.abVirtioLun[0] = 1;
    Event.abVirtioLun[1] = uTarget;
    Event.abVirtioLun[2] = (LUN0 >> 8) & 0x40;
    Event.abVirtioLun[3] = LUN0 & 0xff;
    Event.abVirtioLun[4] = 0;
    Event.abVirtioLun[5] = 0;
    Event.abVirtioLun[6] = 0;
    Event.abVirtioLun[7] = 0;

    RTSGSEG aReqSegs[1];
    aReqSegs[0].pvSeg = &Event;
    aReqSegs[0].cbSeg = sizeof(Event);

    RTSGBUF ReqSgBuf;
    RTSgBufInit(&ReqSgBuf, aReqSegs, RT_ELEMENTS(aReqSegs));

    virtioScsiR3VirtqUsedBufPutAndSync(pDevIns, &pThis->Virtio, EVENTQ_IDX, &ReqSgBuf, pVirtqBuf);
    virtioCoreR3VirtqBufRelease(&pThis->Virtio, pVirtqBuf);

    return rc;
}
#endif

/**
 * Releases one reference from the given controller instances active request counter.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       VirtIO SCSI shared instance data.
 * @param   pThisCC     VirtIO SCSI ring-3 instance data.
 */
DECLINLINE(void) virtioScsiR3Release(PPDMDEVINS pDevIns, PVIRTIOSCSI pThis, PVIRTIOSCSICC pThisCC)
{
    Assert(pThis->cActiveReqs);

    if (!ASMAtomicDecU32(&pThis->cActiveReqs) && pThisCC->fQuiescing)
        PDMDevHlpAsyncNotificationCompleted(pDevIns);
}

/**
 * Retains one reference for the given controller instances active request counter.
 *
 * @param   pThis       VirtIO SCSI shared instance data.
 */
DECLINLINE(void) virtioScsiR3Retain(PVIRTIOSCSI pThis)
{
    ASMAtomicIncU32(&pThis->cActiveReqs);
}

/** Internal worker. */
static void virtioScsiR3FreeReq(PVIRTIOSCSITARGET pTarget, PVIRTIOSCSIREQ pReq)
{
    PVIRTIOSCSI pThis = PDMDEVINS_2_DATA(pTarget->pDevIns, PVIRTIOSCSI);
    RTMemFree(pReq->pbSense);
    pReq->pbSense = NULL;
    virtioCoreR3VirtqBufRelease(&pThis->Virtio, pReq->pVirtqBuf);
    pReq->pVirtqBuf = NULL;
    pTarget->pDrvMediaEx->pfnIoReqFree(pTarget->pDrvMediaEx, pReq->hIoReq);
}

/**
 * This is called to complete a request immediately
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       VirtIO SCSI shared instance data.
 * @param   uVirtqNbr        Virtq index
 * @param   pVirtqBuf  Pointer to pre-processed descriptor chain pulled from virtq
 * @param   pRespHdr    Response header
 * @param   pbSense     Pointer to sense buffer or NULL if none.
 * @param   cbSenseCfg  The configured sense buffer size.
 *
 * @returns VINF_SUCCESS
 */
static int virtioScsiR3ReqErr(PPDMDEVINS pDevIns, PVIRTIOSCSI pThis, uint16_t uVirtqNbr,
                              PVIRTQBUF pVirtqBuf, REQ_RESP_HDR_T *pRespHdr, uint8_t *pbSense,
                              size_t cbSenseCfg)
{
    Log2Func(("   status: %s    response: %s\n",
              SCSIStatusText(pRespHdr->uStatus), virtioGetReqRespText(pRespHdr->uResponse)));

    RTSGSEG aReqSegs[2];

    /* Segment #1: Response header*/
    aReqSegs[0].pvSeg = pRespHdr;
    aReqSegs[0].cbSeg = sizeof(*pRespHdr);

    /* Segment #2: Sense data. */
    uint8_t abSenseBuf[VIRTIOSCSI_SENSE_SIZE_MAX];
    AssertCompile(VIRTIOSCSI_SENSE_SIZE_MAX <= 4096);
    Assert(cbSenseCfg <= sizeof(abSenseBuf));

    RT_ZERO(abSenseBuf);
    if (pbSense && pRespHdr->cbSenseLen)
        memcpy(abSenseBuf, pbSense, RT_MIN(pRespHdr->cbSenseLen, sizeof(abSenseBuf)));
    else
        pRespHdr->cbSenseLen = 0;

    aReqSegs[1].pvSeg = abSenseBuf;
    aReqSegs[1].cbSeg = cbSenseCfg;

    /* Init S/G buffer. */
    RTSGBUF ReqSgBuf;
    RTSgBufInit(&ReqSgBuf, aReqSegs, RT_ELEMENTS(aReqSegs));

    if (pThis->fResetting)
        pRespHdr->uResponse = VIRTIOSCSI_S_RESET;

    virtioScsiR3VirtqUsedBufPutAndSync(pDevIns, &pThis->Virtio, uVirtqNbr, &ReqSgBuf, pVirtqBuf);
    Log2(("---------------------------------------------------------------------------------\n"));

    return VINF_SUCCESS;
}


/**
 * Variant of virtioScsiR3ReqErr that takes four (4) REQ_RESP_HDR_T member
 * fields rather than a pointer to an initialized structure.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       VirtIO SCSI shared instance data.
 * @param   uVirtqNbr   Virtq index
 * @param   pVirtqBuf   Pointer to pre-processed descriptor chain pulled from virtq
 * @param   cbResidual  The number of residual bytes or something like that.
 * @param   bStatus     The SCSI status code.
 * @param   bResponse   The virtio SCSI response code.
 * @param   pbSense     Pointer to sense buffer or NULL if none.
 * @param   cbSense     The number of bytes of sense data. Zero if none.
 * @param   cbSenseCfg  The configured sense buffer size.
 *
 * @returns VINF_SUCCESS
 */
static int virtioScsiR3ReqErr4(PPDMDEVINS pDevIns, PVIRTIOSCSI pThis, uint16_t uVirtqNbr,
                               PVIRTQBUF pVirtqBuf, size_t cbResidual, uint8_t bStatus, uint8_t bResponse,
                               uint8_t *pbSense, size_t cbSense, size_t cbSenseCfg)
{
    REQ_RESP_HDR_T RespHdr;
    RespHdr.cbSenseLen       = cbSense & UINT32_MAX;
    RespHdr.uResidual        = cbResidual & UINT32_MAX;
    RespHdr.uStatusQualifier = 0;
    RespHdr.uStatus          = bStatus;
    RespHdr.uResponse        = bResponse;

    return virtioScsiR3ReqErr(pDevIns, pThis, uVirtqNbr, pVirtqBuf, &RespHdr, pbSense, cbSenseCfg);
}

static void virtioScsiR3SenseKeyToVirtioResp(REQ_RESP_HDR_T *respHdr, uint8_t uSenseKey)
{
    switch (uSenseKey)
    {
        case SCSI_SENSE_ABORTED_COMMAND:
            respHdr->uResponse = VIRTIOSCSI_S_ABORTED;
            break;
        case SCSI_SENSE_COPY_ABORTED:
            respHdr->uResponse = VIRTIOSCSI_S_ABORTED;
            break;
        case SCSI_SENSE_UNIT_ATTENTION:
            respHdr->uResponse = VIRTIOSCSI_S_TARGET_FAILURE;
            break;
        case SCSI_SENSE_HARDWARE_ERROR:
            respHdr->uResponse = VIRTIOSCSI_S_TARGET_FAILURE;
            break;
        case SCSI_SENSE_NOT_READY:
            /* Not sure what to return for this. See choices at VirtIO 1.0,  5.6.6.1.1 */
            respHdr->uResponse = VIRTIOSCSI_S_FAILURE;
            /* respHdr->uResponse = VIRTIOSCSI_S_BUSY; */ /* BUSY is VirtIO's 'retryable' response */
            break;
        default:
            respHdr->uResponse = VIRTIOSCSI_S_FAILURE;
            break;
    }
}

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqCompleteNotify}
 */
static DECLCALLBACK(int) virtioScsiR3IoReqFinish(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                 void *pvIoReqAlloc, int rcReq)
{
    PVIRTIOSCSITARGET   pTarget   = RT_FROM_MEMBER(pInterface, VIRTIOSCSITARGET, IMediaExPort);
    PPDMDEVINS          pDevIns   = pTarget->pDevIns;
    PVIRTIOSCSI         pThis     = PDMDEVINS_2_DATA(pDevIns, PVIRTIOSCSI);
    PVIRTIOSCSICC       pThisCC   = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIOSCSICC);
    PPDMIMEDIAEX        pIMediaEx = pTarget->pDrvMediaEx;
    PVIRTIOSCSIREQ      pReq      = (PVIRTIOSCSIREQ)pvIoReqAlloc;

    size_t cbResidual = 0;
    int rc = pIMediaEx->pfnIoReqQueryResidual(pIMediaEx, hIoReq, &cbResidual);
    AssertRC(rc);

    size_t cbXfer = 0;
    rc = pIMediaEx->pfnIoReqQueryXferSize(pIMediaEx, hIoReq, &cbXfer);
    AssertRC(rc);

    /* Masking deals with data type size discrepancies between
     * The APIs (virtio and VBox). Windows C-compiler complains otherwise */
    Assert(!(cbXfer & 0xffffffff00000000));
    uint32_t cbXfer32 = cbXfer & 0xffffffff;
    REQ_RESP_HDR_T respHdr = { 0 };
    respHdr.cbSenseLen = pReq->pbSense[2] == SCSI_SENSE_NONE ? 0 : (uint32_t)pReq->cbSenseLen;
    AssertMsg(!(cbResidual & 0xffffffff00000000),
            ("WARNING: Residual size larger than sizeof(uint32_t), truncating"));
    respHdr.uResidual = (uint32_t)(cbResidual & 0xffffffff);
    respHdr.uStatus   = pReq->uStatus;

    /*  VirtIO 1.0 spec 5.6.6.1.1 says device MUST return a VirtIO response byte value.
     *  Some are returned during the submit phase, and a few are not mapped at all,
     *  wherein anything that can't map specifically gets mapped to VIRTIOSCSI_S_FAILURE
     */
    if (pThis->fResetting)
        respHdr.uResponse = VIRTIOSCSI_S_RESET;
    else
    {
        switch (rcReq)
        {
            case SCSI_STATUS_OK:
            {
                if (pReq->uStatus != SCSI_STATUS_CHECK_CONDITION)
                    respHdr.uResponse = VIRTIOSCSI_S_OK;
                else
                    virtioScsiR3SenseKeyToVirtioResp(&respHdr, pReq->pbSense[2]);
                break;
            }
            case SCSI_STATUS_CHECK_CONDITION:
                virtioScsiR3SenseKeyToVirtioResp(&respHdr, pReq->pbSense[2]);
                break;

            default:
                respHdr.uResponse = VIRTIOSCSI_S_FAILURE;
                break;
        }
    }

    Log2Func(("status: (%d) %s,   response: (%d) %s\n", pReq->uStatus, SCSIStatusText(pReq->uStatus),
              respHdr.uResponse, virtioGetReqRespText(respHdr.uResponse)));

    if (RT_FAILURE(rcReq))
        Log2Func(("rcReq:  %Rrc\n", rcReq));

    if (LogIs3Enabled())
    {
        LogFunc(("cbDataIn = %u, cbDataOut = %u (cbIn = %u, cbOut = %u)\n",
                  pReq->cbDataIn, pReq->cbDataOut, pReq->pVirtqBuf->cbPhysReturn, pReq->pVirtqBuf->cbPhysSend));
        LogFunc(("xfer = %lu, residual = %u\n", cbXfer, cbResidual));
        LogFunc(("xfer direction: %s, sense written = %d, sense size = %d\n",
                 virtioGetTxDirText(pReq->enmTxDir), respHdr.cbSenseLen, pThis->virtioScsiConfig.uSenseSize));
    }

    if (respHdr.cbSenseLen && LogIs2Enabled())
    {
        LogFunc(("Sense: %s\n", SCSISenseText(pReq->pbSense[2])));
        LogFunc(("Sense Ext3: %s\n", SCSISenseExtText(pReq->pbSense[12], pReq->pbSense[13])));
    }

    if (   (VIRTIO_IS_IN_DIRECTION(pReq->enmTxDir)  && cbXfer32 > pReq->cbDataIn)
        || (VIRTIO_IS_OUT_DIRECTION(pReq->enmTxDir) && cbXfer32 > pReq->cbDataOut))
    {
        Log2Func((" * * * * Data overrun, returning sense\n"));
        uint8_t abSense[] = { RT_BIT(7) | SCSI_SENSE_RESPONSE_CODE_CURR_FIXED,
                              0, SCSI_SENSE_ILLEGAL_REQUEST, 0, 0, 0, 0, 10, 0, 0, 0 };
        respHdr.cbSenseLen = sizeof(abSense);
        respHdr.uStatus    = SCSI_STATUS_CHECK_CONDITION;
        respHdr.uResponse  = VIRTIOSCSI_S_OVERRUN;
        respHdr.uResidual  = pReq->cbDataIn & UINT32_MAX;

        virtioScsiR3ReqErr(pDevIns, pThis, pReq->uVirtqNbr, pReq->pVirtqBuf, &respHdr, abSense,
                           RT_MIN(pThis->virtioScsiConfig.uSenseSize, VIRTIOSCSI_SENSE_SIZE_MAX));
    }
    else
    {
        Assert(pReq->pbSense != NULL);

        /* req datain bytes already in guest phys mem. via virtioScsiIoReqCopyFromBuf() */
        RTSGSEG aReqSegs[2];

        aReqSegs[0].pvSeg = &respHdr;
        aReqSegs[0].cbSeg = sizeof(respHdr);

        aReqSegs[1].pvSeg = pReq->pbSense;
        aReqSegs[1].cbSeg = pReq->cbSenseAlloc; /* VirtIO 1.0 spec 5.6.4/5.6.6.1 */

        RTSGBUF ReqSgBuf;
        RTSgBufInit(&ReqSgBuf, aReqSegs, RT_ELEMENTS(aReqSegs));

        size_t cbReqSgBuf = RTSgBufCalcTotalLength(&ReqSgBuf);
        /** @todo r=bird: Returning here looks a little bogus... */
        AssertMsgReturn(cbReqSgBuf <= pReq->pVirtqBuf->cbPhysReturn,
                       ("Guest expected less req data (space needed: %zu, avail: %u)\n",
                        cbReqSgBuf, pReq->pVirtqBuf->cbPhysReturn),
                        VERR_BUFFER_OVERFLOW);

        virtioScsiR3VirtqUsedBufPutAndSync(pDevIns, &pThis->Virtio, pReq->uVirtqNbr, &ReqSgBuf, pReq->pVirtqBuf);
        Log2(("-----------------------------------------------------------------------------------------\n"));
    }

    virtioScsiR3FreeReq(pTarget, pReq);
    virtioScsiR3Release(pDevIns, pThis, pThisCC);
    return rc;
}

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqCopyFromBuf}

 * Copy virtual memory from VSCSI layer to guest physical memory
 */
static DECLCALLBACK(int) virtioScsiR3IoReqCopyFromBuf(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                      void *pvIoReqAlloc, uint32_t offDst, PRTSGBUF pSgBuf, size_t cbCopy)
{
    PVIRTIOSCSITARGET   pTarget   = RT_FROM_MEMBER(pInterface, VIRTIOSCSITARGET, IMediaExPort);
    PPDMDEVINS          pDevIns   = pTarget->pDevIns;
    PVIRTIOSCSIREQ      pReq      = (PVIRTIOSCSIREQ)pvIoReqAlloc;
    RT_NOREF(hIoReq, cbCopy);

    if (!pReq->cbDataIn)
        return VINF_SUCCESS;

    AssertReturn(pReq->pVirtqBuf, VERR_INVALID_PARAMETER);

    PVIRTIOSGBUF pSgPhysReturn = pReq->pVirtqBuf->pSgPhysReturn;
    virtioCoreGCPhysChainAdvance(pSgPhysReturn, offDst);

    size_t cbCopied = 0;
    size_t cbRemain = pReq->cbDataIn;

    /* Skip past the REQ_RESP_HDR_T and sense code if we're at the start of the buffer. */
    if (!pSgPhysReturn->idxSeg && pSgPhysReturn->cbSegLeft == pSgPhysReturn->paSegs[0].cbSeg)
        virtioCoreGCPhysChainAdvance(pSgPhysReturn, pReq->uDataInOff);

    while (cbRemain)
    {
        cbCopied = RT_MIN(pSgBuf->cbSegLeft,  pSgPhysReturn->cbSegLeft);
        Assert(cbCopied > 0);
        PDMDevHlpPCIPhysWriteUser(pDevIns, pSgPhysReturn->GCPhysCur, pSgBuf->pvSegCur, cbCopied);
        RTSgBufAdvance(pSgBuf, cbCopied);
        virtioCoreGCPhysChainAdvance(pSgPhysReturn, cbCopied);
        cbRemain -= cbCopied;
    }
    RT_UNTRUSTED_NONVOLATILE_COPY_FENCE(); /* needed? */

    Log3Func((".... Copied %lu bytes from %lu byte guest buffer, residual=%lu\n",
              cbCopy, pReq->pVirtqBuf->cbPhysReturn, pReq->pVirtqBuf->cbPhysReturn - cbCopy));

    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqCopyToBuf}
 *
 * Copy guest physical memory to VSCSI layer virtual memory
 */
static DECLCALLBACK(int) virtioScsiR3IoReqCopyToBuf(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                    void *pvIoReqAlloc, uint32_t offSrc, PRTSGBUF pSgBuf, size_t cbCopy)
{
    PVIRTIOSCSITARGET   pTarget   = RT_FROM_MEMBER(pInterface, VIRTIOSCSITARGET, IMediaExPort);
    PPDMDEVINS          pDevIns   = pTarget->pDevIns;
    PVIRTIOSCSIREQ      pReq      = (PVIRTIOSCSIREQ)pvIoReqAlloc;
    RT_NOREF(hIoReq, cbCopy);

    if (!pReq->cbDataOut)
        return VINF_SUCCESS;

    PVIRTIOSGBUF pSgPhysSend = pReq->pVirtqBuf->pSgPhysSend;
    virtioCoreGCPhysChainAdvance(pSgPhysSend, offSrc);

    size_t cbCopied = 0;
    size_t cbRemain = pReq->cbDataOut;
    while (cbRemain)
    {
        cbCopied = RT_MIN(pSgBuf->cbSegLeft, pSgPhysSend->cbSegLeft);
        Assert(cbCopied > 0);
        PDMDevHlpPCIPhysReadUser(pDevIns, pSgPhysSend->GCPhysCur, pSgBuf->pvSegCur, cbCopied);
        RTSgBufAdvance(pSgBuf, cbCopied);
        virtioCoreGCPhysChainAdvance(pSgPhysSend, cbCopied);
        cbRemain -= cbCopied;
    }

    Log2Func((".... Copied %lu bytes to %lu byte guest buffer, residual=%lu\n",
              cbCopy, pReq->pVirtqBuf->cbPhysReturn, pReq->pVirtqBuf->cbPhysReturn - cbCopy));

    return VINF_SUCCESS;
}

/**
 * Handles request queues for/on a worker thread.
 *
 * @returns VBox status code (logged by caller).
 */
static int virtioScsiR3ReqSubmit(PPDMDEVINS pDevIns, PVIRTIOSCSI pThis, PVIRTIOSCSICC pThisCC,
                                 uint16_t uVirtqNbr, PVIRTQBUF pVirtqBuf)
{
    /*
     * Validate configuration values we use here before we start.
     */
    uint32_t const cbCdb      = pThis->virtioScsiConfig.uCdbSize;
    uint32_t const cbSenseCfg = pThis->virtioScsiConfig.uSenseSize;
    /** @todo Report these as errors to the guest or does the caller do that? */
    ASSERT_GUEST_LOGREL_MSG_RETURN(cbCdb <= VIRTIOSCSI_CDB_SIZE_MAX, ("cbCdb=%#x\n", cbCdb), VERR_OUT_OF_RANGE);
    ASSERT_GUEST_LOGREL_MSG_RETURN(cbSenseCfg <= VIRTIOSCSI_SENSE_SIZE_MAX, ("cbSenseCfg=%#x\n", cbSenseCfg), VERR_OUT_OF_RANGE);

    /*
     * Extract command header and CDB from guest physical memory
     * The max size is rather small here (19 + 255 = 274), so put
     * it on the stack.
     */
    size_t const cbReqHdr = sizeof(REQ_CMD_HDR_T) + cbCdb;
    AssertReturn(pVirtqBuf && pVirtqBuf->cbPhysSend >= cbReqHdr, VERR_INVALID_PARAMETER);

    AssertCompile(VIRTIOSCSI_CDB_SIZE_MAX < 4096);
    union
    {
        RT_GCC_EXTENSION struct
        {
            REQ_CMD_HDR_T       ReqHdr;
            uint8_t             abCdb[VIRTIOSCSI_CDB_SIZE_MAX];
        } ;
        uint8_t                 ab[sizeof(REQ_CMD_HDR_T) + VIRTIOSCSI_CDB_SIZE_MAX];
        uint64_t                au64Align[(sizeof(REQ_CMD_HDR_T) + VIRTIOSCSI_CDB_SIZE_MAX) / sizeof(uint64_t)];
    } VirtqReq;
    RT_ZERO(VirtqReq);

    for (size_t offReq = 0; offReq < cbReqHdr; )
    {
        size_t cbSeg = cbReqHdr - offReq;
        RTGCPHYS GCPhys = virtioCoreGCPhysChainGetNextSeg(pVirtqBuf->pSgPhysSend, &cbSeg);
        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhys, &VirtqReq.ab[offReq], cbSeg);
        offReq += cbSeg;
    }

    uint8_t  const uType    = VirtqReq.ReqHdr.abVirtioLun[0];
    uint8_t  const uTarget  = VirtqReq.ReqHdr.abVirtioLun[1];
    uint32_t       uScsiLun = RT_MAKE_U16(VirtqReq.ReqHdr.abVirtioLun[3], VirtqReq.ReqHdr.abVirtioLun[2]) & 0x3fff;

    bool fBadLUNFormat = false;
    if (uType == 0xc1 && uTarget == 0x01)
    {
        LogRel(("* * * WARNING: REPORT LUNS LU ACCESSED. FEATURE NOT IMPLEMENTED SEE DevVirtioScsi.cpp * * * "));
        /* Force rejection. */ /** @todo figure out right way to handle. Note this is a very
         * vague and confusing part of the VirtIO spec (which deviates from the SCSI standard).
         * I have not been able to determine how to implement this properly.  I've checked the
         * source code of Guest drivers, and so far none seem to use it. If this message is logged,
         * meaning a guest expects this feature, implementing it can be re-visited */
        uScsiLun = 0xff;
    }
    else
    if (uType != 1)
        fBadLUNFormat = true;

    LogFunc(("[%s] (Target: %d LUN: %d)  CDB: %.*Rhxs\n",
             SCSICmdText(VirtqReq.abCdb[0]), uTarget, uScsiLun,
             virtioScsiEstimateCdbLen(VirtqReq.abCdb[0], cbCdb), &VirtqReq.abCdb[0]));

    Log3Func(("cmd id: %RX64, attr: %x, prio: %d, crn: %x\n",
              VirtqReq.ReqHdr.uId, VirtqReq.ReqHdr.uTaskAttr, VirtqReq.ReqHdr.uPrio, VirtqReq.ReqHdr.uCrn));

    /*
     * Calculate request offsets and data sizes.
     */
    uint32_t const offDataOut = sizeof(REQ_CMD_HDR_T)  + cbCdb;
    uint32_t const offDataIn  = sizeof(REQ_RESP_HDR_T) + cbSenseCfg;
    size_t   const cbDataOut  = pVirtqBuf->cbPhysSend - offDataOut;
    /** @todo r=bird: Validate cbPhysReturn properly? I've just RT_MAX'ed it for now. */
    size_t   const cbDataIn   = RT_MAX(pVirtqBuf->cbPhysReturn, offDataIn) - offDataIn;
    Assert(offDataOut <= UINT16_MAX);
    Assert(offDataIn  <= UINT16_MAX);

    /*
     * Handle submission errors
     */
    if (RT_LIKELY(!fBadLUNFormat))
    { /*  likely */ }
    else
    {
        Log2Func(("Error submitting request, bad LUN format\n"));
        return virtioScsiR3ReqErr4(pDevIns, pThis, uVirtqNbr, pVirtqBuf, cbDataIn + cbDataOut, 0 /*bStatus*/,
                                   VIRTIOSCSI_S_FAILURE, NULL /*pbSense*/, 0 /*cbSense*/, cbSenseCfg);
    }

    PVIRTIOSCSITARGET const pTarget = &pThisCC->paTargetInstances[uTarget];
    if (RT_LIKELY(   uTarget < pThis->cTargets
                  && pTarget->fPresent
                  && pTarget->pDrvMediaEx))
    { /*  likely */ }
    else
    {
        Log2Func(("Error submitting request to bad target (%d) or bad LUN (%d)\n", uTarget, uScsiLun));
        uint8_t abSense[] = { RT_BIT(7) | SCSI_SENSE_RESPONSE_CODE_CURR_FIXED,
                              0, SCSI_SENSE_ILLEGAL_REQUEST,
                              0, 0, 0, 0, 10, SCSI_ASC_LOGICAL_UNIT_NOT_SUPPORTED, 0, 0 };
        return virtioScsiR3ReqErr4(pDevIns, pThis, uVirtqNbr, pVirtqBuf, cbDataIn + cbDataOut, SCSI_STATUS_CHECK_CONDITION,
                                   VIRTIOSCSI_S_BAD_TARGET, abSense, sizeof(abSense), cbSenseCfg);
    }
    if (RT_LIKELY(uScsiLun == 0))
    { /*  likely */ }
    else
    {
        Log2Func(("Error submitting request to bad target (%d) or bad LUN (%d)\n", uTarget, uScsiLun));
        uint8_t abSense[] = { RT_BIT(7) | SCSI_SENSE_RESPONSE_CODE_CURR_FIXED,
                              0, SCSI_SENSE_ILLEGAL_REQUEST,
                              0, 0, 0, 0, 10, SCSI_ASC_LOGICAL_UNIT_NOT_SUPPORTED, 0, 0 };
        return virtioScsiR3ReqErr4(pDevIns, pThis, uVirtqNbr, pVirtqBuf, cbDataIn + cbDataOut, SCSI_STATUS_CHECK_CONDITION,
                                   VIRTIOSCSI_S_OK, abSense, sizeof(abSense), cbSenseCfg);
    }
    if (RT_LIKELY(!pThis->fResetting))
    { /*  likely */ }
    else
    {
        Log2Func(("Aborting req submission because reset is in progress\n"));
        return virtioScsiR3ReqErr4(pDevIns, pThis, uVirtqNbr, pVirtqBuf, cbDataIn + cbDataOut, SCSI_STATUS_OK,
                                   VIRTIOSCSI_S_RESET, NULL /*pbSense*/, 0 /*cbSense*/, cbSenseCfg);
    }

#if 0
    if (RT_LIKELY(!cbDataIn || !cbDataOut || pThis->fHasInOutBufs)) /* VirtIO 1.0, 5.6.6.1.1 */
    { /*  likely */ }
    else
    {
        Log2Func(("Error submitting request, got datain & dataout bufs w/o INOUT feature negotated\n"));
        uint8_t abSense[] = { RT_BIT(7) | SCSI_SENSE_RESPONSE_CODE_CURR_FIXED,
                              0, SCSI_SENSE_ILLEGAL_REQUEST, 0, 0, 0, 0, 10, 0, 0, 0 };
        return virtioScsiR3ReqErr4(pDevIns, pThis, uVirtqNbr, pVirtqBuf, cbDataIn + cbDataOut, SCSI_STATUS_CHECK_CONDITION,
                                   VIRTIOSCSI_S_FAILURE, abSense, sizeof(abSense), cbSenseCfg);
    }
#endif
    /*
     * Have underlying driver allocate a req of size set during initialization of this device.
     */
    virtioScsiR3Retain(pThis);

    PDMMEDIAEXIOREQ     hIoReq    = NULL;
    PVIRTIOSCSIREQ      pReq      = NULL;
    PPDMIMEDIAEX        pIMediaEx = pTarget->pDrvMediaEx;

    int rc = pIMediaEx->pfnIoReqAlloc(pIMediaEx, &hIoReq, (void **)&pReq, 0 /* uIoReqId */,
                                      PDMIMEDIAEX_F_SUSPEND_ON_RECOVERABLE_ERR);

    if (RT_FAILURE(rc))
    {
        virtioScsiR3Release(pDevIns, pThis, pThisCC);
        return rc;
    }

    pReq->hIoReq      = hIoReq;
    pReq->pTarget     = pTarget;
    pReq->uVirtqNbr   = uVirtqNbr;
    pReq->cbDataIn    = cbDataIn;
    pReq->cbDataOut   = cbDataOut;
    pReq->pVirtqBuf   = pVirtqBuf;
    virtioCoreR3VirtqBufRetain(pVirtqBuf); /* (For pReq->pVirtqBuf. Released by virtioScsiR3FreeReq.) */
    pReq->uDataInOff  = offDataIn;
    pReq->uDataOutOff = offDataOut;

    pReq->cbSenseAlloc = cbSenseCfg;
    pReq->pbSense      = (uint8_t *)RTMemAllocZ(pReq->cbSenseAlloc);
    AssertMsgReturnStmt(pReq->pbSense, ("Out of memory allocating sense buffer"),
                        virtioScsiR3FreeReq(pTarget, pReq);, VERR_NO_MEMORY);

    /* Note: DrvSCSI allocates one virtual memory buffer for input and output phases of the request */
    rc = pIMediaEx->pfnIoReqSendScsiCmd(pIMediaEx, pReq->hIoReq, uScsiLun,
                                        &VirtqReq.abCdb[0], cbCdb,
                                        PDMMEDIAEXIOREQSCSITXDIR_UNKNOWN, &pReq->enmTxDir,
                                        RT_MAX(cbDataIn, cbDataOut),
                                        pReq->pbSense, pReq->cbSenseAlloc, &pReq->cbSenseLen,
                                        &pReq->uStatus, RT_MS_30SEC);

    if (rc != VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS)
    {
        /*
         * Getting here means the request failed in early in the submission to the lower level driver,
         * and there will be no callback to the finished/completion function for this request
         */
        Assert(RT_FAILURE_NP(rc));
        Log2Func(("Request-submission error from lower-level driver\n"));
        uint8_t uASC, uASCQ = 0;
        switch (rc)
        {
            case VERR_NO_MEMORY:
                uASC = SCSI_ASC_SYSTEM_RESOURCE_FAILURE;
                break;
            default:
                uASC = SCSI_ASC_INTERNAL_TARGET_FAILURE;
                break;
        }
        uint8_t abSense[] = { RT_BIT(7) | SCSI_SENSE_RESPONSE_CODE_CURR_FIXED,
                              0, SCSI_SENSE_VENDOR_SPECIFIC,
                              0, 0, 0, 0, 10, uASC, uASCQ, 0 };
        REQ_RESP_HDR_T respHdr = { 0 };
        respHdr.cbSenseLen = sizeof(abSense);
        respHdr.uStatus    = SCSI_STATUS_CHECK_CONDITION;
        respHdr.uResponse  = VIRTIOSCSI_S_FAILURE;
        respHdr.uResidual  = (cbDataIn + cbDataOut) & UINT32_MAX;
        virtioScsiR3ReqErr(pDevIns, pThis, uVirtqNbr, pVirtqBuf, &respHdr, abSense, cbSenseCfg);
        virtioScsiR3FreeReq(pTarget, pReq);
        virtioScsiR3Release(pDevIns, pThis, pThisCC);
    }
    return VINF_SUCCESS;
}

/**
 * Handles control transfers for/on a worker thread.
 *
 * @returns VBox status code (ignored by the caller).
 * @param   pDevIns     The device instance.
 * @param   pThis       VirtIO SCSI shared instance data.
 * @param   pThisCC     VirtIO SCSI ring-3 instance data.
 * @param   uVirtqNbr   CONTROLQ_IDX
 * @param   pVirtqBuf   Descriptor chain to process.
 */
static int virtioScsiR3Ctrl(PPDMDEVINS pDevIns, PVIRTIOSCSI pThis, PVIRTIOSCSICC pThisCC,
                            uint16_t uVirtqNbr, PVIRTQBUF pVirtqBuf)
{
    AssertReturn(pVirtqBuf->cbPhysSend >= RT_MIN(sizeof(VIRTIOSCSI_CTRL_AN_T),
                                                  sizeof(VIRTIOSCSI_CTRL_TMF_T)), 0);

    /*
     * Allocate buffer and read in the control command
     */
    VIRTIO_SCSI_CTRL_UNION_T ScsiCtrlUnion;
    RT_ZERO(ScsiCtrlUnion);

    size_t const cb = RT_MIN(pVirtqBuf->cbPhysSend, sizeof(VIRTIO_SCSI_CTRL_UNION_T));
    for (size_t uOffset = 0; uOffset < cb; )
    {
        size_t cbSeg = cb - uOffset;
        RTGCPHYS GCPhys = virtioCoreGCPhysChainGetNextSeg(pVirtqBuf->pSgPhysSend, &cbSeg);
        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhys, &ScsiCtrlUnion.ab[uOffset], cbSeg);
        uOffset += cbSeg;
    }

    AssertReturn(   (ScsiCtrlUnion.Type.uType == VIRTIOSCSI_T_TMF
                     && pVirtqBuf->cbPhysSend >= sizeof(VIRTIOSCSI_CTRL_TMF_T))
                 || ( (   ScsiCtrlUnion.Type.uType == VIRTIOSCSI_T_AN_QUERY
                       || ScsiCtrlUnion.Type.uType == VIRTIOSCSI_T_AN_SUBSCRIBE)
                     && pVirtqBuf->cbPhysSend >= sizeof(VIRTIOSCSI_CTRL_AN_T)),
                    0 /** @todo r=bird: what kind of status is '0' here? */);

    union
    {
        uint32_t fSupportedEvents;
    }       uData;
    uint8_t bResponse = VIRTIOSCSI_S_OK;
    uint8_t cSegs;
    RTSGSEG aReqSegs[2];
    switch (ScsiCtrlUnion.Type.uType)
    {
        case VIRTIOSCSI_T_TMF: /* Task Management Functions */
        {
            uint8_t  uTarget  = ScsiCtrlUnion.Tmf.abScsiLun[1];
            uint32_t uScsiLun = RT_MAKE_U16(ScsiCtrlUnion.Tmf.abScsiLun[3], ScsiCtrlUnion.Tmf.abScsiLun[2]) & 0x3fff;
            Log2Func(("[%s] (Target: %d LUN: %d)  Task Mgt Function: %s\n",
                      VIRTQNAME(uVirtqNbr), uTarget, uScsiLun, virtioGetTMFTypeText(ScsiCtrlUnion.Tmf.uSubtype)));

            if (uTarget >= pThis->cTargets || !pThisCC->paTargetInstances[uTarget].fPresent)
                bResponse = VIRTIOSCSI_S_BAD_TARGET;
            else
            if (uScsiLun != 0)
                bResponse = VIRTIOSCSI_S_INCORRECT_LUN;
            else
                switch (ScsiCtrlUnion.Tmf.uSubtype)
                {
                    case VIRTIOSCSI_T_TMF_ABORT_TASK:
                        bResponse = VIRTIOSCSI_S_FUNCTION_SUCCEEDED;
                        break;
                    case VIRTIOSCSI_T_TMF_ABORT_TASK_SET:
                        bResponse = VIRTIOSCSI_S_FUNCTION_SUCCEEDED;
                        break;
                    case VIRTIOSCSI_T_TMF_CLEAR_ACA:
                        bResponse = VIRTIOSCSI_S_FUNCTION_SUCCEEDED;
                        break;
                    case VIRTIOSCSI_T_TMF_CLEAR_TASK_SET:
                        bResponse = VIRTIOSCSI_S_FUNCTION_SUCCEEDED;
                        break;
                    case VIRTIOSCSI_T_TMF_I_T_NEXUS_RESET:
                        bResponse = VIRTIOSCSI_S_FUNCTION_SUCCEEDED;
                        break;
                    case VIRTIOSCSI_T_TMF_LOGICAL_UNIT_RESET:
                        bResponse = VIRTIOSCSI_S_FUNCTION_SUCCEEDED;
                        break;
                    case VIRTIOSCSI_T_TMF_QUERY_TASK:
                        bResponse = VIRTIOSCSI_S_FUNCTION_REJECTED;
                        break;
                    case VIRTIOSCSI_T_TMF_QUERY_TASK_SET:
                        bResponse = VIRTIOSCSI_S_FUNCTION_REJECTED;
                        break;
                    default:
                        LogFunc(("Unknown TMF type\n"));
                        bResponse = VIRTIOSCSI_S_FAILURE;
                }
            cSegs = 0; /* only bResponse */
            break;
        }
        case VIRTIOSCSI_T_AN_QUERY: /* Guest SCSI driver is querying supported async event notifications */
        {
            uint8_t  uTarget  = ScsiCtrlUnion.AsyncNotify.abScsiLun[1];
            uint32_t uScsiLun = RT_MAKE_U16(ScsiCtrlUnion.AsyncNotify.abScsiLun[3],
                                            ScsiCtrlUnion.AsyncNotify.abScsiLun[2]) & 0x3fff;

            if (uTarget >= pThis->cTargets || !pThisCC->paTargetInstances[uTarget].fPresent)
                bResponse = VIRTIOSCSI_S_BAD_TARGET;
            else
            if (uScsiLun != 0)
                bResponse = VIRTIOSCSI_S_INCORRECT_LUN;
            else
                bResponse = VIRTIOSCSI_S_FUNCTION_COMPLETE;

#ifdef LOG_ENABLED
            if (LogIs2Enabled())
            {
                char szTypeText[128];
                virtioGetControlAsyncMaskText(szTypeText, sizeof(szTypeText), ScsiCtrlUnion.AsyncNotify.fEventsRequested);
                Log2Func(("[%s] (Target: %d LUN: %d)  Async. Notification Query: %s\n",
                          VIRTQNAME(uVirtqNbr), uTarget, uScsiLun, szTypeText));
            }
#endif
            uData.fSupportedEvents = SUPPORTED_EVENTS;
            aReqSegs[0].pvSeg = &uData.fSupportedEvents;
            aReqSegs[0].cbSeg = sizeof(uData.fSupportedEvents);
            cSegs = 1;
            break;
        }
        case VIRTIOSCSI_T_AN_SUBSCRIBE: /* Guest SCSI driver is subscribing to async event notification(s) */
        {
            if (ScsiCtrlUnion.AsyncNotify.fEventsRequested & ~SUBSCRIBABLE_EVENTS)
                LogFunc(("Unsupported bits in event subscription event mask: %#x\n",
                         ScsiCtrlUnion.AsyncNotify.fEventsRequested));

            uint8_t  uTarget  = ScsiCtrlUnion.AsyncNotify.abScsiLun[1];
            uint32_t uScsiLun = RT_MAKE_U16(ScsiCtrlUnion.AsyncNotify.abScsiLun[3],
                                            ScsiCtrlUnion.AsyncNotify.abScsiLun[2]) & 0x3fff;

#ifdef LOG_ENABLED
            if (LogIs2Enabled())
            {
                char szTypeText[128];
                virtioGetControlAsyncMaskText(szTypeText, sizeof(szTypeText), ScsiCtrlUnion.AsyncNotify.fEventsRequested);
                Log2Func(("[%s] (Target: %d LUN: %d)  Async. Notification Subscribe: %s\n",
                          VIRTQNAME(uVirtqNbr), uTarget, uScsiLun, szTypeText));
            }
#endif
            if (uTarget >= pThis->cTargets || !pThisCC->paTargetInstances[uTarget].fPresent)
                bResponse = VIRTIOSCSI_S_BAD_TARGET;
            else
            if (uScsiLun != 0)
                bResponse = VIRTIOSCSI_S_INCORRECT_LUN;
            else
            {
                bResponse = VIRTIOSCSI_S_FUNCTION_SUCCEEDED; /* or VIRTIOSCSI_S_FUNCTION_COMPLETE? */
                pThis->fAsyncEvtsEnabled = SUPPORTED_EVENTS & ScsiCtrlUnion.AsyncNotify.fEventsRequested;
            }

            aReqSegs[0].pvSeg = &pThis->fAsyncEvtsEnabled;
            aReqSegs[0].cbSeg = sizeof(pThis->fAsyncEvtsEnabled);
            cSegs = 1;
            break;
        }
        default:
        {
            LogFunc(("Unknown control type extracted from %s: %u\n", VIRTQNAME(uVirtqNbr), ScsiCtrlUnion.Type.uType));

            bResponse = VIRTIOSCSI_S_FAILURE;
            cSegs = 0; /* only bResponse */
            break;
        }
    }

    /* Add the response code: */
    aReqSegs[cSegs].pvSeg = &bResponse;
    aReqSegs[cSegs].cbSeg = sizeof(bResponse);
    cSegs++;
    Assert(cSegs <= RT_ELEMENTS(aReqSegs));

    LogFunc(("Response code: %s\n", virtioGetReqRespText(bResponse)));

    RTSGBUF ReqSgBuf;
    RTSgBufInit(&ReqSgBuf, aReqSegs, cSegs);

    virtioScsiR3VirtqUsedBufPutAndSync(pDevIns, &pThis->Virtio, uVirtqNbr, &ReqSgBuf, pVirtqBuf);
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNPDMTHREADWAKEUPDEV}
 */
static DECLCALLBACK(int) virtioScsiR3WorkerWakeUp(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    PVIRTIOSCSI pThis = PDMDEVINS_2_DATA(pDevIns, PVIRTIOSCSI);
    return PDMDevHlpSUPSemEventSignal(pDevIns, pThis->aWorkers[(uintptr_t)pThread->pvUser].hEvtProcess);
}

/**
 * @callback_method_impl{FNPDMTHREADDEV}
 */
static DECLCALLBACK(int) virtioScsiR3WorkerThread(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    uint16_t const      uVirtqNbr  = (uint16_t)(uintptr_t)pThread->pvUser;
    PVIRTIOSCSI         pThis     = PDMDEVINS_2_DATA(pDevIns, PVIRTIOSCSI);
    PVIRTIOSCSICC       pThisCC   = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIOSCSICC);
    PVIRTIOSCSIWORKER   pWorker   = &pThis->aWorkers[uVirtqNbr];
    PVIRTIOSCSIWORKERR3 pWorkerR3 = &pThisCC->aWorkers[uVirtqNbr];

    if (pThread->enmState == PDMTHREADSTATE_INITIALIZING)
        return VINF_SUCCESS;

    Log6Func(("[Re]starting %s worker\n", VIRTQNAME(uVirtqNbr)));
    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        if (    !pWorkerR3->cRedoDescs
             && IS_VIRTQ_EMPTY(pDevIns, &pThis->Virtio, uVirtqNbr))
        {
            /* Atomic interlocks avoid missing alarm while going to sleep & notifier waking the awoken */
            ASMAtomicWriteBool(&pWorker->fSleeping, true);
            bool fNotificationSent = ASMAtomicXchgBool(&pWorker->fNotified, false);
            if (!fNotificationSent)
            {
                Log6Func(("%s worker sleeping...\n", VIRTQNAME(uVirtqNbr)));
                Assert(ASMAtomicReadBool(&pWorker->fSleeping));
                int rc = PDMDevHlpSUPSemEventWaitNoResume(pDevIns, pWorker->hEvtProcess, RT_INDEFINITE_WAIT);
                AssertLogRelMsgReturn(RT_SUCCESS(rc) || rc == VERR_INTERRUPTED, ("%Rrc\n", rc), rc);
                if (RT_UNLIKELY(pThread->enmState != PDMTHREADSTATE_RUNNING))
                {
                    Log6Func(("%s worker thread not running, exiting\n", VIRTQNAME(uVirtqNbr)));
                    return VINF_SUCCESS;
                }
                if (rc == VERR_INTERRUPTED)
                {
                    Log6Func(("%s worker interrupted ... continuing\n", VIRTQNAME(uVirtqNbr)));
                    continue;
                }
                Log6Func(("%s worker woken\n", VIRTQNAME(uVirtqNbr)));
                ASMAtomicWriteBool(&pWorker->fNotified, false);
            }
            ASMAtomicWriteBool(&pWorker->fSleeping, false);
        }
        if (!virtioCoreIsVirtqEnabled(&pThis->Virtio, uVirtqNbr))
        {
            LogFunc(("%s queue not enabled, worker aborting...\n", VIRTQNAME(uVirtqNbr)));
            break;
        }

        if (!pThis->afVirtqAttached[uVirtqNbr])
        {
            LogFunc(("%s queue not attached, worker aborting...\n", VIRTQNAME(uVirtqNbr)));
            break;
        }
        if (!pThisCC->fQuiescing)
        {
            /* Process any reqs that were suspended saved to the redo queue in save exec. */
            for (int i = 0; i < pWorkerR3->cRedoDescs; i++)
            {
                PVIRTQBUF pVirtqBuf = virtioCoreR3VirtqBufAlloc();
                if (!pVirtqBuf)
                {
                    LogRel(("Failed to allocate memory for VIRTQBUF\n"));
                    break;  /* No point in trying to allocate memory for other descriptor chains */
                }
                int rc = virtioCoreR3VirtqAvailBufGet(pDevIns, &pThis->Virtio, uVirtqNbr,
                                                      pWorkerR3->auRedoDescs[i], pVirtqBuf);
                if (RT_FAILURE(rc))
                    LogRel(("Error fetching desc chain to redo, %Rrc", rc));

                rc = virtioScsiR3ReqSubmit(pDevIns, pThis, pThisCC, uVirtqNbr, pVirtqBuf);
                if (RT_FAILURE(rc))
                    LogRel(("Error submitting req packet, resetting %Rrc", rc));

                virtioCoreR3VirtqBufRelease(&pThis->Virtio, pVirtqBuf);
            }
            pWorkerR3->cRedoDescs = 0;

            Log6Func(("fetching next descriptor chain from %s\n", VIRTQNAME(uVirtqNbr)));
            PVIRTQBUF pVirtqBuf = virtioCoreR3VirtqBufAlloc();
            if (!pVirtqBuf)
                LogRel(("Failed to allocate memory for VIRTQBUF\n"));
            else
            {
                int rc = virtioCoreR3VirtqAvailBufGet(pDevIns, &pThis->Virtio, uVirtqNbr, pVirtqBuf, true);
                if (rc == VERR_NOT_AVAILABLE)
                {
                    Log6Func(("Nothing found in %s\n", VIRTQNAME(uVirtqNbr)));
                    virtioCoreR3VirtqBufRelease(&pThis->Virtio, pVirtqBuf);
                    continue;
                }

                AssertRC(rc);
                if (uVirtqNbr == CONTROLQ_IDX)
                    virtioScsiR3Ctrl(pDevIns, pThis, pThisCC, uVirtqNbr, pVirtqBuf);
                else /* request queue index */
                {
                    rc = virtioScsiR3ReqSubmit(pDevIns, pThis, pThisCC, uVirtqNbr, pVirtqBuf);
                    if (RT_FAILURE(rc))
                        LogRel(("Error submitting req packet, resetting %Rrc", rc));
                }

                virtioCoreR3VirtqBufRelease(&pThis->Virtio, pVirtqBuf);
            }
        }
    }
    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   Sending events
*********************************************************************************************************************************/

/*
 * @todo Figure out how to implement this with R0 changes. Not used by current linux driver
 */

#if 0
DECLINLINE(void) virtioScsiR3ReportEventsMissed(PPDMDEVINS pDevIns, PVIRTIOSCSI pThis, uint16_t uTarget)
{
    virtioScsiR3SendEvent(pDevIns, pThis, uTarget, VIRTIOSCSI_T_NO_EVENT | VIRTIOSCSI_T_EVENTS_MISSED, 0);
}
#endif

#if 0
/* SUBSCRIBABLE EVENT - not sure when to call this or how to detect when media is added or removed
 *                      via the VBox GUI */
DECLINLINE(void) virtioScsiR3ReportMediaChange(PPDMDEVINS pDevIns, PVIRTIOSCSI pThis, uint16_t uTarget)
{
    if (pThis->fAsyncEvtsEnabled & VIRTIOSCSI_EVT_ASYNC_MEDIA_CHANGE)
        virtioScsiR3SendEvent(pDevIns, pThis, uTarget, VIRTIOSCSI_T_ASYNC_NOTIFY, VIRTIOSCSI_EVT_ASYNC_MEDIA_CHANGE);
}

/* ESSENTIAL (NON-SUBSCRIBABLE) EVENT TYPES (most guest virtio-scsi drivers ignore?)  */

DECLINLINE(void) virtioScsiR3ReportTransportReset(PDMDEVINS pDevIns, PVIRTIOSCSI pThis, uint16_t uTarget)
{
    virtioScsiR3SendEvent(pDevIns, pThis, uTarget, VIRTIOSCSI_T_TRANSPORT_RESET, VIRTIOSCSI_EVT_RESET_HARD);
}

DECLINLINE(void) virtioScsiR3ReportParamChange(PDMDEVINS pDevIns, PVIRTIOSCSI pThis, uint16_t uTarget,
                                               uint32_t uSenseCode, uint32_t uSenseQualifier)
{
    uint32_t uReason = uSenseQualifier << 8 | uSenseCode;
    virtioScsiR3SendEvent(pDevIns, pThis, uTarget, VIRTIOSCSI_T_PARAM_CHANGE, uReason);

}

DECLINLINE(void) virtioScsiR3ReportTargetRemoved(PDMDEVINS pDevIns, PVIRTIOSCSI pThis, uint16_t uTarget)
{
    if (pThis->fHasHotplug)
        virtioScsiR3SendEvent(pDevIns, pThis, uTarget, VIRTIOSCSI_T_TRANSPORT_RESET, VIRTIOSCSI_EVT_RESET_REMOVED);
}

DECLINLINE(void) virtioScsiR3ReportTargetAdded(PDMDEVINS pDevInsPVIRTIOSCSI pThis, uint16_t uTarget)
{
    if (pThis->fHasHotplug)
        virtioScsiR3SendEvent(pDevIns, pThis, uTarget, VIRTIOSCSI_T_TRANSPORT_RESET, VIRTIOSCSI_EVT_RESET_RESCAN);
}

#endif

/**
 * @callback_method_impl{VIRTIOCORER3,pfnStatusChanged}
 */
static DECLCALLBACK(void) virtioScsiR3StatusChanged(PVIRTIOCORE pVirtio, PVIRTIOCORECC pVirtioCC, uint32_t fVirtioReady)
{
    PVIRTIOSCSI     pThis     = RT_FROM_MEMBER(pVirtio, VIRTIOSCSI, Virtio);
    PVIRTIOSCSICC   pThisCC   = RT_FROM_MEMBER(pVirtioCC, VIRTIOSCSICC, Virtio);

    pThis->fVirtioReady = fVirtioReady;

    if (fVirtioReady)
    {
        LogFunc(("VirtIO ready\n-----------------------------------------------------------------------------------------\n"));
        uint64_t fFeatures   = virtioCoreGetNegotiatedFeatures(&pThis->Virtio);
        pThis->fHasT10pi     = fFeatures & VIRTIO_SCSI_F_T10_PI;
        pThis->fHasHotplug   = fFeatures & VIRTIO_SCSI_F_HOTPLUG;
        pThis->fHasInOutBufs = fFeatures & VIRTIO_SCSI_F_INOUT;
        pThis->fHasLunChange = fFeatures & VIRTIO_SCSI_F_CHANGE;
        pThis->fResetting    = false;
        pThisCC->fQuiescing  = false;

        for (unsigned i = 0; i < VIRTIOSCSI_VIRTQ_CNT; i++)
            pThis->afVirtqAttached[i] = true;
    }
    else
    {
        LogFunc(("VirtIO is resetting\n"));
        for (unsigned i = 0; i < VIRTIOSCSI_VIRTQ_CNT; i++)
            pThis->afVirtqAttached[i] = false;

        /*
         * BIOS may change these values. When the OS comes up, and KVM driver accessed
         * through Windows, it assumes they are the default size. So as per the VirtIO 1.0 spec,
         * 5.6.4, these device configuration values must be set to default upon device reset.
         */
        pThis->virtioScsiConfig.uSenseSize = VIRTIOSCSI_SENSE_SIZE_DEFAULT;
        pThis->virtioScsiConfig.uCdbSize   = VIRTIOSCSI_CDB_SIZE_DEFAULT;
    }


}


/*********************************************************************************************************************************
*   LEDs                                                                                                                         *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{PDMILEDPORTS,pfnQueryStatusLed, Target level.}
 */
static DECLCALLBACK(int) virtioScsiR3TargetQueryStatusLed(PPDMILEDPORTS pInterface, unsigned iLUN, PPDMLED *ppLed)
{
    PVIRTIOSCSITARGET pTarget = RT_FROM_MEMBER(pInterface, VIRTIOSCSITARGET, ILed);
    if (iLUN == 0)
    {
        *ppLed = &pTarget->led;
        Assert((*ppLed)->u32Magic == PDMLED_MAGIC);
        return VINF_SUCCESS;
    }
    return VERR_PDM_LUN_NOT_FOUND;
}
/**
 * @interface_method_impl{PDMILEDPORTS,pfnQueryStatusLed, Device level.}
 */
static DECLCALLBACK(int) virtioScsiR3DeviceQueryStatusLed(PPDMILEDPORTS pInterface, unsigned iLUN, PPDMLED *ppLed)
{
    PVIRTIOSCSICC pThisCC = RT_FROM_MEMBER(pInterface, VIRTIOSCSICC, ILeds);
    PVIRTIOSCSI   pThis   = PDMDEVINS_2_DATA(pThisCC->pDevIns, PVIRTIOSCSI);
    if (iLUN < pThis->cTargets)
    {
        *ppLed = &pThisCC->paTargetInstances[iLUN].led;
        Assert((*ppLed)->u32Magic == PDMLED_MAGIC);
        return VINF_SUCCESS;
    }
    return VERR_PDM_LUN_NOT_FOUND;
}


/*********************************************************************************************************************************
*   PDMIMEDIAPORT (target)                                                                                                       *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{PDMIMEDIAPORT,pfnQueryDeviceLocation, Target level.}
 */
static DECLCALLBACK(int) virtioScsiR3QueryDeviceLocation(PPDMIMEDIAPORT pInterface, const char **ppcszController,
                                                         uint32_t *piInstance, uint32_t *piLUN)
{
    PVIRTIOSCSITARGET pTarget = RT_FROM_MEMBER(pInterface, VIRTIOSCSITARGET, IMediaPort);
    PPDMDEVINS        pDevIns = pTarget->pDevIns;

    AssertPtrReturn(ppcszController, VERR_INVALID_POINTER);
    AssertPtrReturn(piInstance, VERR_INVALID_POINTER);
    AssertPtrReturn(piLUN, VERR_INVALID_POINTER);

    *ppcszController = pDevIns->pReg->szName;
    *piInstance = pDevIns->iInstance;
    *piLUN = pTarget->uTarget;

    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   Virtio config.                                                                                                               *
*********************************************************************************************************************************/

/**
 * Worker for virtioScsiR3DevCapWrite and virtioScsiR3DevCapRead.
 */
static int virtioScsiR3CfgAccessed(PVIRTIOSCSI pThis, uint32_t uOffsetOfAccess, void *pv, uint32_t cb, bool fWrite)
{
    AssertReturn(pv && cb <= sizeof(uint32_t), fWrite ? VINF_SUCCESS : VINF_IOM_MMIO_UNUSED_00);

    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(    uNumVirtqs,     VIRTIOSCSI_CONFIG_T, uOffsetOfAccess))
        VIRTIO_DEV_CONFIG_ACCESS_READONLY( uNumVirtqs,     VIRTIOSCSI_CONFIG_T, uOffsetOfAccess, &pThis->virtioScsiConfig);
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(    uSegMax,        VIRTIOSCSI_CONFIG_T, uOffsetOfAccess))
        VIRTIO_DEV_CONFIG_ACCESS_READONLY( uSegMax,        VIRTIOSCSI_CONFIG_T, uOffsetOfAccess, &pThis->virtioScsiConfig);
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(    uMaxSectors,    VIRTIOSCSI_CONFIG_T, uOffsetOfAccess))
        VIRTIO_DEV_CONFIG_ACCESS_READONLY( uMaxSectors,    VIRTIOSCSI_CONFIG_T, uOffsetOfAccess, &pThis->virtioScsiConfig);
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(    uCmdPerLun,     VIRTIOSCSI_CONFIG_T, uOffsetOfAccess))
        VIRTIO_DEV_CONFIG_ACCESS_READONLY( uCmdPerLun,     VIRTIOSCSI_CONFIG_T, uOffsetOfAccess, &pThis->virtioScsiConfig);
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(    uEventInfoSize, VIRTIOSCSI_CONFIG_T, uOffsetOfAccess))
        VIRTIO_DEV_CONFIG_ACCESS_READONLY( uEventInfoSize, VIRTIOSCSI_CONFIG_T, uOffsetOfAccess, &pThis->virtioScsiConfig);
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(    uSenseSize,     VIRTIOSCSI_CONFIG_T, uOffsetOfAccess))
        VIRTIO_DEV_CONFIG_ACCESS(          uSenseSize,     VIRTIOSCSI_CONFIG_T, uOffsetOfAccess, &pThis->virtioScsiConfig);
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(    uCdbSize,       VIRTIOSCSI_CONFIG_T, uOffsetOfAccess))
        VIRTIO_DEV_CONFIG_ACCESS(          uCdbSize,       VIRTIOSCSI_CONFIG_T, uOffsetOfAccess, &pThis->virtioScsiConfig);
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(    uMaxChannel,    VIRTIOSCSI_CONFIG_T, uOffsetOfAccess))
        VIRTIO_DEV_CONFIG_ACCESS_READONLY( uMaxChannel,    VIRTIOSCSI_CONFIG_T, uOffsetOfAccess, &pThis->virtioScsiConfig);
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(    uMaxTarget,     VIRTIOSCSI_CONFIG_T, uOffsetOfAccess))
        VIRTIO_DEV_CONFIG_ACCESS_READONLY( uMaxTarget,     VIRTIOSCSI_CONFIG_T, uOffsetOfAccess, &pThis->virtioScsiConfig);
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(    uMaxLun,        VIRTIOSCSI_CONFIG_T, uOffsetOfAccess))
        VIRTIO_DEV_CONFIG_ACCESS_READONLY( uMaxLun,        VIRTIOSCSI_CONFIG_T, uOffsetOfAccess, &pThis->virtioScsiConfig);
    else
    {
        LogFunc(("Bad access by guest to virtio_scsi_config: off=%u (%#x), cb=%u\n", uOffsetOfAccess, uOffsetOfAccess, cb));
        return fWrite ? VINF_SUCCESS : VINF_IOM_MMIO_UNUSED_00;
    }
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{VIRTIOCORER3,pfnDevCapRead}
 */
static DECLCALLBACK(int) virtioScsiR3DevCapRead(PPDMDEVINS pDevIns, uint32_t uOffset, void *pv, uint32_t cb)
{
    return virtioScsiR3CfgAccessed(PDMDEVINS_2_DATA(pDevIns, PVIRTIOSCSI), uOffset, pv, cb, false /*fRead*/);
}

/**
 * @callback_method_impl{VIRTIOCORER3,pfnDevCapWrite}
 */
static DECLCALLBACK(int) virtioScsiR3DevCapWrite(PPDMDEVINS pDevIns, uint32_t uOffset, const void *pv, uint32_t cb)
{
    return virtioScsiR3CfgAccessed(PDMDEVINS_2_DATA(pDevIns, PVIRTIOSCSI), uOffset, (void *)pv, cb, true /*fWrite*/);
}


/*********************************************************************************************************************************
*   IBase for device and targets                                                                                                 *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface, Target level.}
 */
static DECLCALLBACK(void *) virtioScsiR3TargetQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
     PVIRTIOSCSITARGET pTarget = RT_FROM_MEMBER(pInterface, VIRTIOSCSITARGET, IBase);
     PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE,        &pTarget->IBase);
     PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMEDIAPORT,   &pTarget->IMediaPort);
     PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMEDIAEXPORT, &pTarget->IMediaExPort);
     PDMIBASE_RETURN_INTERFACE(pszIID, PDMILEDPORTS,    &pTarget->ILed);
     return NULL;
}

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface, Device level.}
 */
static DECLCALLBACK(void *) virtioScsiR3DeviceQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PVIRTIOSCSICC pThisCC = RT_FROM_MEMBER(pInterface, VIRTIOSCSICC, IBase);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE,         &pThisCC->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMILEDPORTS,     &pThisCC->ILeds);

    return NULL;
}


/*********************************************************************************************************************************
*   Misc                                                                                                                         *
*********************************************************************************************************************************/

/**
 * @callback_method_impl{FNDBGFHANDLERDEV, virtio-scsi debugger info callback.}
 */
static DECLCALLBACK(void) virtioScsiR3Info(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PVIRTIOSCSI pThis = PDMDEVINS_2_DATA(pDevIns, PVIRTIOSCSI);

    /* Parse arguments. */
    RT_NOREF(pszArgs); //bool fVerbose = pszArgs && strstr(pszArgs, "verbose") != NULL;

    /* Show basic information. */
    pHlp->pfnPrintf(pHlp, "%s#%d: virtio-scsci ",
                    pDevIns->pReg->szName,
                    pDevIns->iInstance);
    pHlp->pfnPrintf(pHlp, "numTargets=%lu", pThis->cTargets);
}


/*********************************************************************************************************************************
*   Saved state                                                                                                                  *
*********************************************************************************************************************************/

/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) virtioScsiR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PVIRTIOSCSI     pThis   = PDMDEVINS_2_DATA(pDevIns, PVIRTIOSCSI);
    PVIRTIOSCSICC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIOSCSICC);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;

    LogFunc(("LOAD EXEC!!\n"));

    AssertReturn(uPass == SSM_PASS_FINAL, VERR_SSM_UNEXPECTED_PASS);
    AssertLogRelMsgReturn(uVersion == VIRTIOSCSI_SAVED_STATE_VERSION,
                          ("uVersion=%u\n", uVersion), VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION);

    virtioScsiSetVirtqNames(pThis);
    for (int uVirtqNbr = 0; uVirtqNbr < VIRTIOSCSI_VIRTQ_CNT; uVirtqNbr++)
        pHlp->pfnSSMGetBool(pSSM, &pThis->afVirtqAttached[uVirtqNbr]);

    pHlp->pfnSSMGetU32(pSSM,  &pThis->virtioScsiConfig.uNumVirtqs);
    pHlp->pfnSSMGetU32(pSSM,  &pThis->virtioScsiConfig.uSegMax);
    pHlp->pfnSSMGetU32(pSSM,  &pThis->virtioScsiConfig.uMaxSectors);
    pHlp->pfnSSMGetU32(pSSM,  &pThis->virtioScsiConfig.uCmdPerLun);
    pHlp->pfnSSMGetU32(pSSM,  &pThis->virtioScsiConfig.uEventInfoSize);
    pHlp->pfnSSMGetU32(pSSM,  &pThis->virtioScsiConfig.uSenseSize);
    pHlp->pfnSSMGetU32(pSSM,  &pThis->virtioScsiConfig.uCdbSize);
    pHlp->pfnSSMGetU16(pSSM,  &pThis->virtioScsiConfig.uMaxChannel);
    pHlp->pfnSSMGetU16(pSSM,  &pThis->virtioScsiConfig.uMaxTarget);
    pHlp->pfnSSMGetU32(pSSM,  &pThis->virtioScsiConfig.uMaxLun);
    pHlp->pfnSSMGetU32(pSSM,  &pThis->fAsyncEvtsEnabled);
    pHlp->pfnSSMGetBool(pSSM, &pThis->fEventsMissed);
    pHlp->pfnSSMGetU32(pSSM,  &pThis->fVirtioReady);
    pHlp->pfnSSMGetU32(pSSM,  &pThis->fHasT10pi);
    pHlp->pfnSSMGetU32(pSSM,  &pThis->fHasHotplug);
    pHlp->pfnSSMGetU32(pSSM,  &pThis->fHasInOutBufs);
    pHlp->pfnSSMGetU32(pSSM,  &pThis->fHasLunChange);
    pHlp->pfnSSMGetU32(pSSM,  &pThis->fResetting);

    uint32_t cTargets;
    int rc = pHlp->pfnSSMGetU32(pSSM, &cTargets);
    AssertRCReturn(rc, rc);
    AssertReturn(cTargets == pThis->cTargets,
                 pHlp->pfnSSMSetLoadError(pSSM, VERR_SSM_LOAD_CONFIG_MISMATCH, RT_SRC_POS,
                                          N_("target count has changed: %u saved, %u configured now"),
                                          cTargets, pThis->cTargets));

    for (uint16_t uTarget = 0; uTarget < pThis->cTargets; uTarget++)
    {
        PVIRTIOSCSITARGET pTarget = &pThisCC->paTargetInstances[uTarget];

        /* Place all suspended requests in the request queue. */
        if (pTarget->pDrvMediaEx)
        {

            uint16_t cReqsRedo;
            rc = pHlp->pfnSSMGetU16(pSSM, &cReqsRedo);
            AssertRCReturn(rc, rc);
            AssertReturn(cReqsRedo < VIRTQ_SIZE,
                         pHlp->pfnSSMSetLoadError(pSSM, VERR_SSM_DATA_UNIT_FORMAT_CHANGED, RT_SRC_POS,
                                                  N_("Bad count of I/O transactions to re-do in saved state (%#x, max %#x - 1)"),
                                                  cReqsRedo, VIRTQ_SIZE));

            for (uint16_t uVirtqNbr = VIRTQ_REQ_BASE; uVirtqNbr < VIRTIOSCSI_VIRTQ_CNT; uVirtqNbr++)
            {
                PVIRTIOSCSIWORKERR3 pWorkerR3 = &pThisCC->aWorkers[uVirtqNbr];
                pWorkerR3->cRedoDescs = 0;
            }

            for (int i = 0; i < cReqsRedo; i++)
            {
                uint16_t uVirtqNbr;
                rc = pHlp->pfnSSMGetU16(pSSM, &uVirtqNbr);
                AssertRCReturn(rc, rc);
                AssertReturn(uVirtqNbr < VIRTIOSCSI_VIRTQ_CNT,
                             pHlp->pfnSSMSetLoadError(pSSM, VERR_SSM_DATA_UNIT_FORMAT_CHANGED, RT_SRC_POS,
                                                      N_("Bad queue index for re-do in saved state (%#x, max %#x)"),
                                                      uVirtqNbr, VIRTIOSCSI_VIRTQ_CNT - 1));

                uint16_t idxHead;
                rc = pHlp->pfnSSMGetU16(pSSM, &idxHead);
                AssertRCReturn(rc, rc);
                AssertReturn(idxHead < VIRTQ_SIZE,
                             pHlp->pfnSSMSetLoadError(pSSM, VERR_SSM_DATA_UNIT_FORMAT_CHANGED, RT_SRC_POS,
                                                      N_("Bad queue element index for re-do in saved state (%#x, max %#x)"),
                                                      idxHead, VIRTQ_SIZE - 1));

                PVIRTIOSCSIWORKERR3 pWorkerR3 = &pThisCC->aWorkers[uVirtqNbr];
                pWorkerR3->auRedoDescs[pWorkerR3->cRedoDescs++] = idxHead;
                pWorkerR3->cRedoDescs %= VIRTQ_SIZE;
            }
        }
    }

    /*
     * Call the virtio core to let it load its state.
     */
    rc = virtioCoreR3ModernDeviceLoadExec(&pThis->Virtio, pDevIns->pHlpR3, pSSM,
                                           uVersion, VIRTIOSCSI_SAVED_STATE_VERSION, pThis->virtioScsiConfig.uNumVirtqs);

    /*
     * Nudge request queue workers
     */
    for (int uVirtqNbr = VIRTQ_REQ_BASE; uVirtqNbr < VIRTIOSCSI_VIRTQ_CNT; uVirtqNbr++)
    {
        if (pThis->afVirtqAttached[uVirtqNbr])
        {
            LogFunc(("Waking %s worker.\n", VIRTQNAME(uVirtqNbr)));
            int rc2 = PDMDevHlpSUPSemEventSignal(pDevIns, pThis->aWorkers[uVirtqNbr].hEvtProcess);
            AssertRCReturn(rc, rc2);
        }
    }

    return rc;
}

/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) virtioScsiR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PVIRTIOSCSI     pThis   = PDMDEVINS_2_DATA(pDevIns, PVIRTIOSCSI);
    PVIRTIOSCSICC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIOSCSICC);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;

    LogFunc(("SAVE EXEC!!\n"));

    for (int uVirtqNbr = 0; uVirtqNbr < VIRTIOSCSI_VIRTQ_CNT; uVirtqNbr++)
        pHlp->pfnSSMPutBool(pSSM, pThis->afVirtqAttached[uVirtqNbr]);

    pHlp->pfnSSMPutU32(pSSM,  pThis->virtioScsiConfig.uNumVirtqs);
    pHlp->pfnSSMPutU32(pSSM,  pThis->virtioScsiConfig.uSegMax);
    pHlp->pfnSSMPutU32(pSSM,  pThis->virtioScsiConfig.uMaxSectors);
    pHlp->pfnSSMPutU32(pSSM,  pThis->virtioScsiConfig.uCmdPerLun);
    pHlp->pfnSSMPutU32(pSSM,  pThis->virtioScsiConfig.uEventInfoSize);
    pHlp->pfnSSMPutU32(pSSM,  pThis->virtioScsiConfig.uSenseSize);
    pHlp->pfnSSMPutU32(pSSM,  pThis->virtioScsiConfig.uCdbSize);
    pHlp->pfnSSMPutU16(pSSM,  pThis->virtioScsiConfig.uMaxChannel);
    pHlp->pfnSSMPutU16(pSSM,  pThis->virtioScsiConfig.uMaxTarget);
    pHlp->pfnSSMPutU32(pSSM,  pThis->virtioScsiConfig.uMaxLun);
    pHlp->pfnSSMPutU32(pSSM,  pThis->fAsyncEvtsEnabled);
    pHlp->pfnSSMPutBool(pSSM, pThis->fEventsMissed);
    pHlp->pfnSSMPutU32(pSSM,  pThis->fVirtioReady);
    pHlp->pfnSSMPutU32(pSSM,  pThis->fHasT10pi);
    pHlp->pfnSSMPutU32(pSSM,  pThis->fHasHotplug);
    pHlp->pfnSSMPutU32(pSSM,  pThis->fHasInOutBufs);
    pHlp->pfnSSMPutU32(pSSM,  pThis->fHasLunChange);
    pHlp->pfnSSMPutU32(pSSM,  pThis->fResetting);

    AssertMsg(!pThis->cActiveReqs, ("There are still outstanding requests on this device\n"));

    pHlp->pfnSSMPutU32(pSSM, pThis->cTargets);

    for (uint16_t uTarget = 0; uTarget < pThis->cTargets; uTarget++)
    {
        PVIRTIOSCSITARGET pTarget = &pThisCC->paTargetInstances[uTarget];

        /* Query all suspended requests and store them in the request queue. */
        if (pTarget->pDrvMediaEx)
        {
            uint32_t cReqsRedo = pTarget->pDrvMediaEx->pfnIoReqGetSuspendedCount(pTarget->pDrvMediaEx);

            pHlp->pfnSSMPutU16(pSSM, cReqsRedo);

            if (cReqsRedo)
            {
                PDMMEDIAEXIOREQ hIoReq;
                PVIRTIOSCSIREQ pReq;

                int rc = pTarget->pDrvMediaEx->pfnIoReqQuerySuspendedStart(pTarget->pDrvMediaEx, &hIoReq,
                                                                           (void **)&pReq);
                AssertRCBreak(rc);

                while(--cReqsRedo)
                {
                    pHlp->pfnSSMPutU16(pSSM, pReq->uVirtqNbr);
                    pHlp->pfnSSMPutU16(pSSM, pReq->pVirtqBuf->uHeadIdx);

                    rc = pTarget->pDrvMediaEx->pfnIoReqQuerySuspendedNext(pTarget->pDrvMediaEx, hIoReq,
                                                                          &hIoReq, (void **)&pReq);
                    AssertRCBreak(rc);
                }
            }
        }
    }

    /*
     * Call the virtio core to let it save its state.
     */
    return virtioCoreR3SaveExec(&pThis->Virtio, pDevIns->pHlpR3, pSSM, VIRTIOSCSI_SAVED_STATE_VERSION, VIRTIOSCSI_VIRTQ_CNT);
}


/*********************************************************************************************************************************
*   Device interface.                                                                                                            *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{PDMDEVREGR3,pfnDetach}
 *
 * One harddisk at one port has been unplugged.
 * The VM is suspended at this point.
 */
static DECLCALLBACK(void) virtioScsiR3Detach(PPDMDEVINS pDevIns, unsigned uTarget, uint32_t fFlags)
{
    PVIRTIOSCSI       pThis   = PDMDEVINS_2_DATA(pDevIns, PVIRTIOSCSI);
    PVIRTIOSCSICC     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIOSCSICC);
    AssertReturnVoid(uTarget < pThis->cTargets);
    PVIRTIOSCSITARGET pTarget = &pThisCC->paTargetInstances[uTarget];

    LogFunc((""));

    AssertMsg(fFlags & PDM_TACH_FLAGS_NOT_HOT_PLUG,
              ("virtio-scsi: Device does not support hotplugging\n"));
    RT_NOREF(fFlags);

    /*
     * Zero all important members.
     */
    pTarget->fPresent       = false;
    pTarget->pDrvBase       = NULL;
    pTarget->pDrvMedia      = NULL;
    pTarget->pDrvMediaEx    = NULL;
}

/**
 * @interface_method_impl{PDMDEVREGR3,pfnAttach}
 *
 * This is called when we change block driver.
 */
static DECLCALLBACK(int) virtioScsiR3Attach(PPDMDEVINS pDevIns, unsigned uTarget, uint32_t fFlags)
{
    PVIRTIOSCSI       pThis   = PDMDEVINS_2_DATA(pDevIns, PVIRTIOSCSI);
    PVIRTIOSCSICC     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIOSCSICC);
    AssertReturn(uTarget < pThis->cTargets, VERR_PDM_LUN_NOT_FOUND);
    PVIRTIOSCSITARGET pTarget = &pThisCC->paTargetInstances[uTarget];

    Assert(pTarget->pDevIns == pDevIns);
    AssertMsgReturn(fFlags & PDM_TACH_FLAGS_NOT_HOT_PLUG,
                    ("virtio-scsi: Device does not support hotplugging\n"),
                    VERR_INVALID_PARAMETER);

    AssertRelease(!pTarget->pDrvBase);
    Assert(pTarget->uTarget == uTarget);

    /*
     * Try attach the SCSI driver and get the interfaces, required as well as optional.
     */
    int rc = PDMDevHlpDriverAttach(pDevIns, pTarget->uTarget, &pDevIns->IBase, &pTarget->pDrvBase, pTarget->pszTargetName);
    if (RT_SUCCESS(rc))
    {
        pTarget->fPresent = true;
        pTarget->pDrvMedia = PDMIBASE_QUERY_INTERFACE(pTarget->pDrvBase, PDMIMEDIA);
        AssertMsgReturn(RT_VALID_PTR(pTarget->pDrvMedia),
                        ("virtio-scsi configuration error: LUN#%d missing basic media interface!\n", uTarget),
                        VERR_PDM_MISSING_INTERFACE);

        /* Get the extended media interface. */
        pTarget->pDrvMediaEx = PDMIBASE_QUERY_INTERFACE(pTarget->pDrvBase, PDMIMEDIAEX);
        AssertMsgReturn(RT_VALID_PTR(pTarget->pDrvMediaEx),
                        ("virtio-scsi configuration error: LUN#%d missing extended media interface!\n", uTarget),
                        VERR_PDM_MISSING_INTERFACE);

        rc = pTarget->pDrvMediaEx->pfnIoReqAllocSizeSet(pTarget->pDrvMediaEx, sizeof(VIRTIOSCSIREQ));
        AssertMsgReturn(RT_VALID_PTR(pTarget->pDrvMediaEx),
                        ("virtio-scsi configuration error: LUN#%u: Failed to set I/O request size!\n", uTarget),
                        rc);
    }
    else
        AssertMsgFailed(("Failed to attach %s. rc=%Rrc\n", pTarget->pszTargetName, rc));

    if (RT_FAILURE(rc))
    {
        pTarget->fPresent      = false;
        pTarget->pDrvBase       = NULL;
        pTarget->pDrvMedia      = NULL;
        pTarget->pDrvMediaEx    = NULL;
        pThisCC->pMediaNotify   = NULL;
    }
    return rc;
}

/**
 * @callback_method_impl{FNPDMDEVASYNCNOTIFY}
 */
static DECLCALLBACK(bool) virtioScsiR3DeviceQuiesced(PPDMDEVINS pDevIns)
{
    PVIRTIOSCSI     pThis   = PDMDEVINS_2_DATA(pDevIns, PVIRTIOSCSI);
    PVIRTIOSCSICC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIOSCSICC);

    if (ASMAtomicReadU32(&pThis->cActiveReqs))
        return false;

    LogFunc(("Device I/O activity quiesced: %s\n",
        virtioCoreGetStateChangeText(pThisCC->enmQuiescingFor)));

    virtioCoreR3VmStateChanged(&pThis->Virtio, pThisCC->enmQuiescingFor);

    pThis->fResetting = false;
    pThisCC->fQuiescing = false;

    return true;
}

/**
 * Worker for virtioScsiR3Reset() and virtioScsiR3SuspendOrPowerOff().
 */
static void virtioScsiR3QuiesceDevice(PPDMDEVINS pDevIns, VIRTIOVMSTATECHANGED enmQuiscingFor)
{
    PVIRTIOSCSI     pThis   = PDMDEVINS_2_DATA(pDevIns, PVIRTIOSCSI);
    PVIRTIOSCSICC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIOSCSICC);

    /* Prevent worker threads from removing/processing elements from virtq's */
    pThisCC->fQuiescing = true;
    pThisCC->enmQuiescingFor = enmQuiscingFor;

    PDMDevHlpSetAsyncNotification(pDevIns, virtioScsiR3DeviceQuiesced);

    /* If already quiesced invoke async callback.  */
    if (!ASMAtomicReadU32(&pThis->cActiveReqs))
        PDMDevHlpAsyncNotificationCompleted(pDevIns);
}

/**
 * @interface_method_impl{PDMDEVREGR3,pfnReset}
 */
static DECLCALLBACK(void) virtioScsiR3Reset(PPDMDEVINS pDevIns)
{
    LogFunc(("\n"));
    PVIRTIOSCSI pThis = PDMDEVINS_2_DATA(pDevIns, PVIRTIOSCSI);
    pThis->fResetting = true;
    virtioScsiR3QuiesceDevice(pDevIns, kvirtIoVmStateChangedReset);
}

/**
 * @interface_method_impl{PDMDEVREGR3,pfnPowerOff}
 */
static DECLCALLBACK(void) virtioScsiR3SuspendOrPowerOff(PPDMDEVINS pDevIns, VIRTIOVMSTATECHANGED enmType)
{
    LogFunc(("\n"));

    PVIRTIOSCSI     pThis   = PDMDEVINS_2_DATA(pDevIns, PVIRTIOSCSI);
    PVIRTIOSCSICC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIOSCSICC);

    /* VM is halted, thus no new I/O being dumped into queues by the guest.
     * Workers have been flagged to stop pulling stuff already queued-up by the guest.
     * Now tell lower-level to to suspend reqs (for example, DrvVD suspends all reqs
     * on its wait queue, and we will get a callback as the state changes to
     * suspended (and later, resumed) for each).
     */
    for (uint32_t i = 0; i < pThis->cTargets; i++)
    {
        PVIRTIOSCSITARGET pTarget = &pThisCC->paTargetInstances[i];
        if (pTarget->pDrvMediaEx)
            pTarget->pDrvMediaEx->pfnNotifySuspend(pTarget->pDrvMediaEx);
    }

    virtioScsiR3QuiesceDevice(pDevIns, enmType);
}

/**
 * @interface_method_impl{PDMDEVREGR3,pfnSuspend}
 */
static DECLCALLBACK(void) virtioScsiR3PowerOff(PPDMDEVINS pDevIns)
{
    LogFunc(("\n"));
    virtioScsiR3SuspendOrPowerOff(pDevIns, kvirtIoVmStateChangedPowerOff);
}

/**
 * @interface_method_impl{PDMDEVREGR3,pfnSuspend}
 */
static DECLCALLBACK(void) virtioScsiR3Suspend(PPDMDEVINS pDevIns)
{
    LogFunc(("\n"));
    virtioScsiR3SuspendOrPowerOff(pDevIns, kvirtIoVmStateChangedSuspend);
}

/**
 * @interface_method_impl{PDMDEVREGR3,pfnResume}
 */
static DECLCALLBACK(void) virtioScsiR3Resume(PPDMDEVINS pDevIns)
{
    PVIRTIOSCSI     pThis   = PDMDEVINS_2_DATA(pDevIns, PVIRTIOSCSI);
    PVIRTIOSCSICC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIOSCSICC);
    LogFunc(("\n"));

    pThisCC->fQuiescing = false;

    /* Wake worker threads flagged to skip pulling queue entries during quiesce
     * to ensure they re-check their queues. Active request queues may already
     * be awake due to new reqs coming in.
     */
    for (uint16_t uVirtqNbr = 0; uVirtqNbr < VIRTIOSCSI_REQ_VIRTQ_CNT; uVirtqNbr++)
    {
        if (   virtioCoreIsVirtqEnabled(&pThis->Virtio, uVirtqNbr)
            && ASMAtomicReadBool(&pThis->aWorkers[uVirtqNbr].fSleeping))
        {
            Log6Func(("waking %s worker.\n", VIRTQNAME(uVirtqNbr)));
            int rc = PDMDevHlpSUPSemEventSignal(pDevIns, pThis->aWorkers[uVirtqNbr].hEvtProcess);
            AssertRC(rc);
        }
    }
    /* Ensure guest is working the queues too. */
    virtioCoreR3VmStateChanged(&pThis->Virtio, kvirtIoVmStateChangedResume);
}

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnMediumEjected}
 */
static DECLCALLBACK(void) virtioScsiR3MediumEjected(PPDMIMEDIAEXPORT pInterface)
{
    PVIRTIOSCSITARGET   pTarget   = RT_FROM_MEMBER(pInterface, VIRTIOSCSITARGET, IMediaExPort);
    PPDMDEVINS          pDevIns   = pTarget->pDevIns;
    PVIRTIOSCSICC       pThisCC   = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIOSCSICC);

#if 0 /* need more info about how to use this event. The VirtIO 1.0 specification
       * lists several SCSI related event types but presumes the reader knows
       * how to use them without providing references. */
    virtioScsiR3ReportMediaChange(pDevIns, pThis, pTarget->uTarget);
#endif

    if (pThisCC->pMediaNotify)
    {
        int rc = PDMDevHlpVMReqCallNoWait(pDevIns, VMCPUID_ANY,
                                          (PFNRT)pThisCC->pMediaNotify->pfnEjected, 2,
                                          pThisCC->pMediaNotify, pTarget->uTarget);
        AssertRC(rc);
    }
}

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqStateChanged}
 */
static DECLCALLBACK(void) virtioScsiR3IoReqStateChanged(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                        void *pvIoReqAlloc, PDMMEDIAEXIOREQSTATE enmState)
{
    PVIRTIOSCSITARGET   pTarget   = RT_FROM_MEMBER(pInterface, VIRTIOSCSITARGET, IMediaExPort);
    PPDMDEVINS          pDevIns   = pTarget->pDevIns;
    PVIRTIOSCSI         pThis     = PDMDEVINS_2_DATA(pDevIns, PVIRTIOSCSI);
    PVIRTIOSCSICC       pThisCC   = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIOSCSICC);
    RT_NOREF(hIoReq, pvIoReqAlloc);

    switch (enmState)
    {
        case PDMMEDIAEXIOREQSTATE_SUSPENDED:
        {
            /* Stop considering this request active */
            virtioScsiR3Release(pDevIns, pThis, pThisCC);
            break;
        }
        case PDMMEDIAEXIOREQSTATE_ACTIVE:
            virtioScsiR3Retain(pThis);
            break;
        default:
            AssertMsgFailed(("Invalid request state given %u\n", enmState));
    }
}

/**
 * @interface_method_impl{PDMDEVREGR3,pfnDestruct}
 */
static DECLCALLBACK(int) virtioScsiR3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);
    PVIRTIOSCSI   pThis   = PDMDEVINS_2_DATA(pDevIns, PVIRTIOSCSI);
    PVIRTIOSCSICC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIOSCSICC);

    RTMemFree(pThisCC->paTargetInstances);
    pThisCC->paTargetInstances = NULL;
    pThisCC->pMediaNotify = NULL;

    for (unsigned uVirtqNbr = 0; uVirtqNbr < VIRTIOSCSI_VIRTQ_CNT; uVirtqNbr++)
    {
        PVIRTIOSCSIWORKER pWorker = &pThis->aWorkers[uVirtqNbr];
        if (pWorker->hEvtProcess != NIL_SUPSEMEVENT)
        {
            PDMDevHlpSUPSemEventClose(pDevIns, pWorker->hEvtProcess);
            pWorker->hEvtProcess = NIL_SUPSEMEVENT;
        }

        if (pThisCC->aWorkers[uVirtqNbr].pThread)
        {
            /* Destroy the thread. */
            int rcThread;
            int rc = PDMDevHlpThreadDestroy(pDevIns, pThisCC->aWorkers[uVirtqNbr].pThread, &rcThread);
            if (RT_FAILURE(rc) || RT_FAILURE(rcThread))
                AssertMsgFailed(("%s Failed to destroythread rc=%Rrc rcThread=%Rrc\n",
                                 __FUNCTION__, rc, rcThread));
            pThisCC->aWorkers[uVirtqNbr].pThread = NULL;
        }

        if (RTCritSectIsInitialized(&pThisCC->aWorkers[uVirtqNbr].CritSectVirtq))
            RTCritSectDelete(&pThisCC->aWorkers[uVirtqNbr].CritSectVirtq);
    }

    virtioCoreR3Term(pDevIns, &pThis->Virtio, &pThisCC->Virtio);
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMDEVREGR3,pfnConstruct}
 */
static DECLCALLBACK(int) virtioScsiR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PVIRTIOSCSI   pThis   = PDMDEVINS_2_DATA(pDevIns, PVIRTIOSCSI);
    PVIRTIOSCSICC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIOSCSICC);
    PCPDMDEVHLPR3 pHlp    = pDevIns->pHlpR3;

    /*
     * Quick initialization of the state data, making sure that the destructor always works.
     */
    pThisCC->pDevIns = pDevIns;

    LogFunc(("PDM device instance: %d\n", iInstance));
    RTStrPrintf(pThis->szInstance, sizeof(pThis->szInstance), "VIRTIOSCSI%d", iInstance);

    pThisCC->IBase.pfnQueryInterface = virtioScsiR3DeviceQueryInterface;
    pThisCC->ILeds.pfnQueryStatusLed = virtioScsiR3DeviceQueryStatusLed;

    /*
     * Validate and read configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "NumTargets"
                                           "|Bootable"
                                           "|MmioBase"
                                           "|Irq", "");

    int rc = pHlp->pfnCFGMQueryU32Def(pCfg, "NumTargets", &pThis->cTargets, 1);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("virtio-scsi configuration error: failed to read NumTargets as integer"));
    if (pThis->cTargets < 1 || pThis->cTargets > VIRTIOSCSI_MAX_TARGETS)
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("virtio-scsi configuration error: NumTargets=%u is out of range (1..%u)"),
                                   pThis->cTargets, VIRTIOSCSI_MAX_TARGETS);

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "Bootable", &pThis->fBootable, true);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("virtio-scsi configuration error: failed to read Bootable as boolean"));

    LogRel(("%s: Targets=%u Bootable=%RTbool (unimplemented) R0Enabled=%RTbool RCEnabled=%RTbool\n",
            pThis->szInstance, pThis->cTargets, pThis->fBootable, pDevIns->fR0Enabled, pDevIns->fRCEnabled));


    /*
     * Do core virtio initialization.
     */

    /* Configure virtio_scsi_config that transacts via VirtIO implementation's Dev. Specific Cap callbacks */
    pThis->virtioScsiConfig.uNumVirtqs      = VIRTIOSCSI_REQ_VIRTQ_CNT;
    pThis->virtioScsiConfig.uSegMax         = VIRTIOSCSI_MAX_SEG_COUNT;
    pThis->virtioScsiConfig.uMaxSectors     = VIRTIOSCSI_MAX_SECTORS_HINT;
    pThis->virtioScsiConfig.uCmdPerLun      = VIRTIOSCSI_MAX_COMMANDS_PER_LUN;
    pThis->virtioScsiConfig.uEventInfoSize  = sizeof(VIRTIOSCSI_EVENT_T); /*VirtIO 1.0 Spec says at least this size! */
    pThis->virtioScsiConfig.uSenseSize      = VIRTIOSCSI_SENSE_SIZE_DEFAULT;
    pThis->virtioScsiConfig.uCdbSize        = VIRTIOSCSI_CDB_SIZE_DEFAULT;
    pThis->virtioScsiConfig.uMaxChannel     = VIRTIOSCSI_MAX_CHANNEL_HINT;
    pThis->virtioScsiConfig.uMaxTarget      = pThis->cTargets;
    pThis->virtioScsiConfig.uMaxLun         = VIRTIOSCSI_MAX_LUN;

    /* Initialize the generic Virtio core: */
    pThisCC->Virtio.pfnVirtqNotified        = virtioScsiNotified;
    pThisCC->Virtio.pfnStatusChanged        = virtioScsiR3StatusChanged;
    pThisCC->Virtio.pfnDevCapRead           = virtioScsiR3DevCapRead;
    pThisCC->Virtio.pfnDevCapWrite          = virtioScsiR3DevCapWrite;

    VIRTIOPCIPARAMS VirtioPciParams;
    VirtioPciParams.uDeviceId               = PCI_DEVICE_ID_VIRTIOSCSI_HOST;
    VirtioPciParams.uClassBase              = PCI_CLASS_BASE_MASS_STORAGE;
    VirtioPciParams.uClassSub               = PCI_CLASS_SUB_SCSI_STORAGE_CONTROLLER;
    VirtioPciParams.uClassProg              = PCI_CLASS_PROG_UNSPECIFIED;
    VirtioPciParams.uSubsystemId            = PCI_DEVICE_ID_VIRTIOSCSI_HOST;  /* VirtIO 1.0 spec allows PCI Device ID here */
    VirtioPciParams.uInterruptLine          = 0x00;
    VirtioPciParams.uInterruptPin           = 0x01;
    VirtioPciParams.uDeviceType             = VIRTIO_DEVICE_TYPE_SCSI_HOST;

    rc = virtioCoreR3Init(pDevIns, &pThis->Virtio, &pThisCC->Virtio, &VirtioPciParams, pThis->szInstance,
                          VIRTIOSCSI_HOST_SCSI_FEATURES_OFFERED, 0 /*fOfferLegacy*/,
                          &pThis->virtioScsiConfig /*pvDevSpecificCap*/, sizeof(pThis->virtioScsiConfig));
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("virtio-scsi: failed to initialize VirtIO"));

    /*
     * Initialize queues.
     */

    virtioScsiSetVirtqNames(pThis);

    /* Attach the queues and create worker threads for them: */
    for (uint16_t uVirtqNbr = 0; uVirtqNbr < VIRTIOSCSI_VIRTQ_CNT; uVirtqNbr++)
    {
        rc = virtioCoreR3VirtqAttach(&pThis->Virtio, uVirtqNbr, VIRTQNAME(uVirtqNbr));
        if (RT_FAILURE(rc))
            continue;
        if (uVirtqNbr == CONTROLQ_IDX || IS_REQ_VIRTQ(uVirtqNbr))
        {
            rc = PDMDevHlpThreadCreate(pDevIns, &pThisCC->aWorkers[uVirtqNbr].pThread,
                                       (void *)(uintptr_t)uVirtqNbr, virtioScsiR3WorkerThread,
                                       virtioScsiR3WorkerWakeUp, 0, RTTHREADTYPE_IO, VIRTQNAME(uVirtqNbr));
            if (rc != VINF_SUCCESS)
            {
                LogRel(("Error creating thread for Virtual Virtq %s: %Rrc\n", VIRTQNAME(uVirtqNbr), rc));
                return rc;
            }

            rc = PDMDevHlpSUPSemEventCreate(pDevIns, &pThis->aWorkers[uVirtqNbr].hEvtProcess);
            if (RT_FAILURE(rc))
                return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                           N_("DevVirtioSCSI: Failed to create SUP event semaphore"));
        }
        pThis->afVirtqAttached[uVirtqNbr] = true;
        rc = RTCritSectInit(&pThisCC->aWorkers[uVirtqNbr].CritSectVirtq);
        if (RT_FAILURE(rc))
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                       N_("DevVirtioSCSI: Failed to create worker critical section"));
    }

    /*
     * Initialize per device instances (targets).
     */
    Log2Func(("Probing %d targets ...\n", pThis->cTargets));

    pThisCC->paTargetInstances = (PVIRTIOSCSITARGET)RTMemAllocZ(sizeof(VIRTIOSCSITARGET) * pThis->cTargets);
    if (!pThisCC->paTargetInstances)
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Failed to allocate memory for target states"));

    for (uint32_t uTarget = 0; uTarget < pThis->cTargets; uTarget++)
    {
        PVIRTIOSCSITARGET pTarget = &pThisCC->paTargetInstances[uTarget];

        if (RTStrAPrintf(&pTarget->pszTargetName, "VSCSI%u", uTarget) < 0)
            AssertLogRelFailedReturn(VERR_NO_MEMORY);

        /* Initialize static parts of the device. */
        pTarget->pDevIns = pDevIns;
        pTarget->uTarget = uTarget;

        pTarget->IBase.pfnQueryInterface                 = virtioScsiR3TargetQueryInterface;

        /* IMediaPort and IMediaExPort interfaces provide callbacks for VD media and downstream driver access */
        pTarget->IMediaPort.pfnQueryDeviceLocation       = virtioScsiR3QueryDeviceLocation;
        pTarget->IMediaPort.pfnQueryScsiInqStrings       = NULL;
        pTarget->IMediaExPort.pfnIoReqCompleteNotify     = virtioScsiR3IoReqFinish;
        pTarget->IMediaExPort.pfnIoReqCopyFromBuf        = virtioScsiR3IoReqCopyFromBuf;
        pTarget->IMediaExPort.pfnIoReqCopyToBuf          = virtioScsiR3IoReqCopyToBuf;
        pTarget->IMediaExPort.pfnIoReqStateChanged       = virtioScsiR3IoReqStateChanged;
        pTarget->IMediaExPort.pfnMediumEjected           = virtioScsiR3MediumEjected;
        pTarget->IMediaExPort.pfnIoReqQueryBuf           = NULL; /* When used avoids copyFromBuf CopyToBuf*/
        pTarget->IMediaExPort.pfnIoReqQueryDiscardRanges = NULL;

        pTarget->IBase.pfnQueryInterface                 = virtioScsiR3TargetQueryInterface;
        pTarget->ILed.pfnQueryStatusLed                  = virtioScsiR3TargetQueryStatusLed;
        pTarget->led.u32Magic                            = PDMLED_MAGIC;

        LogFunc(("Attaching LUN: %s\n", pTarget->pszTargetName));

        AssertReturn(uTarget < pThis->cTargets, VERR_PDM_NO_SUCH_LUN);
        rc = PDMDevHlpDriverAttach(pDevIns, uTarget, &pTarget->IBase, &pTarget->pDrvBase, pTarget->pszTargetName);
        if (RT_SUCCESS(rc))
        {
            pTarget->fPresent = true;

            pTarget->pDrvMedia = PDMIBASE_QUERY_INTERFACE(pTarget->pDrvBase, PDMIMEDIA);
            AssertMsgReturn(RT_VALID_PTR(pTarget->pDrvMedia),
                            ("virtio-scsi configuration error: LUN#%d missing basic media interface!\n", uTarget),
                            VERR_PDM_MISSING_INTERFACE);
            /* Get the extended media interface. */
            pTarget->pDrvMediaEx = PDMIBASE_QUERY_INTERFACE(pTarget->pDrvBase, PDMIMEDIAEX);
            AssertMsgReturn(RT_VALID_PTR(pTarget->pDrvMediaEx),
                            ("virtio-scsi configuration error: LUN#%d missing extended media interface!\n", uTarget),
                            VERR_PDM_MISSING_INTERFACE);

            rc = pTarget->pDrvMediaEx->pfnIoReqAllocSizeSet(pTarget->pDrvMediaEx, sizeof(VIRTIOSCSIREQ));
            AssertMsgReturn(RT_VALID_PTR(pTarget->pDrvMediaEx),
                            ("virtio-scsi configuration error: LUN#%u: Failed to set I/O request size!\n", uTarget),
                            rc);
        }
        else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
        {
            pTarget->fPresent = false;
            pTarget->pDrvBase = NULL;
            Log(("virtio-scsi: no driver attached to device %s\n", pTarget->pszTargetName));
            rc = VINF_SUCCESS;
        }
        else
        {
            AssertLogRelMsgFailed(("virtio-scsi: Failed to attach %s: %Rrc\n", pTarget->pszTargetName, rc));
            return rc;
        }
    }

    /*
     * Status driver (optional).
     */
    PPDMIBASE pUpBase = NULL;
    AssertCompile(PDM_STATUS_LUN >= VIRTIOSCSI_MAX_TARGETS);
    rc = PDMDevHlpDriverAttach(pDevIns, PDM_STATUS_LUN, &pThisCC->IBase, &pUpBase, "Status Port");
    if (RT_FAILURE(rc) && rc != VERR_PDM_NO_ATTACHED_DRIVER)
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Failed to attach the status LUN"));
    if (RT_SUCCESS(rc) && pUpBase)
        pThisCC->pMediaNotify = PDMIBASE_QUERY_INTERFACE(pUpBase, PDMIMEDIANOTIFY);


    /*
     * Register saved state.
     */
    rc = PDMDevHlpSSMRegister(pDevIns, VIRTIOSCSI_SAVED_STATE_VERSION, sizeof(*pThis),
                              virtioScsiR3SaveExec, virtioScsiR3LoadExec);
    AssertRCReturn(rc, rc);

    /*
     * Register the debugger info callback (ignore errors).
     */
    char szTmp[128];
    RTStrPrintf(szTmp, sizeof(szTmp), "%s%u", pDevIns->pReg->szName, pDevIns->iInstance);
    PDMDevHlpDBGFInfoRegister(pDevIns, szTmp, "virtio-scsi info", virtioScsiR3Info);

    return rc;
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) virtioScsiRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);

    PVIRTIOSCSI   pThis   = PDMDEVINS_2_DATA(pDevIns, PVIRTIOSCSI);
    PVIRTIOSCSICC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIOSCSICC);


    pThisCC->Virtio.pfnVirtqNotified = virtioScsiNotified;
    return virtioCoreRZInit(pDevIns, &pThis->Virtio);
}

#endif /* !IN_RING3 */


/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceVirtioSCSI =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "virtio-scsi",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE
                                    | PDM_DEVREG_FLAGS_FIRST_SUSPEND_NOTIFICATION
                                    | PDM_DEVREG_FLAGS_FIRST_POWEROFF_NOTIFICATION,
    /* .fClass = */                 PDM_DEVREG_CLASS_STORAGE,
    /* .cMaxInstances = */          ~0U,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(VIRTIOSCSI),
    /* .cbInstanceCC = */           sizeof(VIRTIOSCSICC),
    /* .cbInstanceRC = */           sizeof(VIRTIOSCSIRC),
    /* .cMaxPciDevices = */         1,
    /* .cMaxMsixVectors = */        VBOX_MSIX_MAX_ENTRIES,
    /* .pszDescription = */         "Virtio Host SCSI.\n",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           virtioScsiR3Construct,
    /* .pfnDestruct = */            virtioScsiR3Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               virtioScsiR3Reset,
    /* .pfnSuspend = */             virtioScsiR3Suspend,
    /* .pfnResume = */              virtioScsiR3Resume,
    /* .pfnAttach = */              virtioScsiR3Attach,
    /* .pfnDetach = */              virtioScsiR3Detach,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        NULL,
    /* .pfnPowerOff = */            virtioScsiR3PowerOff,
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
    /* .pfnConstruct = */           virtioScsiRZConstruct,
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
    /* .pfnConstruct = */           virtioScsiRZConstruct,
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

