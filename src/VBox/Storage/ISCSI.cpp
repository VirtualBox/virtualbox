/* $Id: ISCSI.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * iSCSI initiator driver, VD backend.
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
#define LOG_GROUP LOG_GROUP_VD_ISCSI
#include <VBox/vd-plugin.h>
#include <VBox/err.h>

#include <VBox/log.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/uuid.h>
#include <iprt/string.h>
#include <iprt/asm.h>
#include <iprt/thread.h>
#include <iprt/semaphore.h>
#include <iprt/md5.h>
#include <iprt/tcp.h>
#include <iprt/time.h>
#include <VBox/scsi.h>

#include "VDBackends.h"
#include "VDBackendsInline.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** The maximum number of release log entries per image. */
#define MAX_LOG_REL_ERRORS  1024

/** Default port number to use for iSCSI. */
#define ISCSI_DEFAULT_PORT 3260


/** Converts a number in the range of 0 - 15 into the corresponding hex char. */
#define NUM_2_HEX(b) ('0' + (b) + (((b) > 9) ? 39 : 0))
/** Converts a hex char into the corresponding number in the range 0-15. */
#define HEX_2_NUM(c) (((c) <= '9') ? ((c) - '0') : (((c - 'A' + 10) & 0xf)))
/* Converts a base64 char into the corresponding number in the range 0-63. */
#define B64_2_NUM(c) ((c >= 'A' && c <= 'Z') ? (c - 'A') : (c >= 'a' && c <= 'z') ? (c - 'a' + 26) : (c >= '0' && c <= '9') ? (c - '0' + 52) : (c == '+') ? 62 : (c == '/') ? 63 : -1)


/** Minimum CHAP_MD5 challenge length in bytes. */
#define CHAP_MD5_CHALLENGE_MIN 16
/** Maximum CHAP_MD5 challenge length in bytes. */
#define CHAP_MD5_CHALLENGE_MAX 24


/**
 * SCSI peripheral device type. */
typedef enum SCSIDEVTYPE
{
    /** direct-access device. */
    SCSI_DEVTYPE_DISK = 0,
    /** sequential-access device. */
    SCSI_DEVTYPE_TAPE,
    /** printer device. */
    SCSI_DEVTYPE_PRINTER,
    /** processor device. */
    SCSI_DEVTYPE_PROCESSOR,
    /** write-once device. */
    SCSI_DEVTYPE_WORM,
    /** CD/DVD device. */
    SCSI_DEVTYPE_CDROM,
    /** scanner device. */
    SCSI_DEVTYPE_SCANNER,
    /** optical memory device. */
    SCSI_DEVTYPE_OPTICAL,
    /** medium changer. */
    SCSI_DEVTYPE_CHANGER,
    /** communications device. */
    SCSI_DEVTYPE_COMMUNICATION,
    /** storage array controller device. */
    SCSI_DEVTYPE_RAIDCTL = 0x0c,
    /** enclosure services device. */
    SCSI_DEVTYPE_ENCLOSURE,
    /** simplified direct-access device. */
    SCSI_DEVTYPE_SIMPLEDISK,
    /** optical card reader/writer device. */
    SCSI_DEVTYPE_OCRW,
    /** bridge controller device. */
    SCSI_DEVTYPE_BRIDGE,
    /** object-based storage device. */
    SCSI_DEVTYPE_OSD
} SCSIDEVTYPE;

/** Mask for extracting the SCSI device type out of the first byte of the INQUIRY response. */
#define SCSI_DEVTYPE_MASK 0x1f

/** Mask to extract the CmdQue bit out of the seventh byte of the INQUIRY response. */
#define SCSI_INQUIRY_CMDQUE_MASK 0x02

/** Maximum PDU payload size we can handle in one piece. Greater or equal than
 * s_iscsiConfigDefaultWriteSplit. */
#define ISCSI_DATA_LENGTH_MAX _256K

/** Maximum PDU size we can handle in one piece. */
#define ISCSI_RECV_PDU_BUFFER_SIZE (ISCSI_DATA_LENGTH_MAX + ISCSI_BHS_SIZE)


/** Version of the iSCSI standard which this initiator driver can handle. */
#define ISCSI_MY_VERSION 0


/** Length of ISCSI basic header segment. */
#define ISCSI_BHS_SIZE 48


/** Reserved task tag value. */
#define ISCSI_TASK_TAG_RSVD 0xffffffff


/**
 * iSCSI opcodes. */
typedef enum ISCSIOPCODE
{
    /** NOP-Out. */
    ISCSIOP_NOP_OUT = 0x00000000,
    /** SCSI command. */
    ISCSIOP_SCSI_CMD = 0x01000000,
    /** SCSI task management request. */
    ISCSIOP_SCSI_TASKMGMT_REQ = 0x02000000,
    /** Login request. */
    ISCSIOP_LOGIN_REQ = 0x03000000,
    /** Text request. */
    ISCSIOP_TEXT_REQ = 0x04000000,
    /** SCSI Data-Out. */
    ISCSIOP_SCSI_DATA_OUT = 0x05000000,
    /** Logout request. */
    ISCSIOP_LOGOUT_REQ = 0x06000000,
    /** SNACK request. */
    ISCSIOP_SNACK_REQ = 0x10000000,

    /** NOP-In. */
    ISCSIOP_NOP_IN = 0x20000000,
    /** SCSI response. */
    ISCSIOP_SCSI_RES = 0x21000000,
    /** SCSI Task Management response. */
    ISCSIOP_SCSI_TASKMGMT_RES = 0x22000000,
    /** Login response. */
    ISCSIOP_LOGIN_RES = 0x23000000,
    /** Text response. */
    ISCSIOP_TEXT_RES = 0x24000000,
    /** SCSI Data-In. */
    ISCSIOP_SCSI_DATA_IN = 0x25000000,
    /** Logout response. */
    ISCSIOP_LOGOUT_RES = 0x26000000,
    /** Ready To Transfer (R2T). */
    ISCSIOP_R2T = 0x31000000,
    /** Asynchronous message. */
    ISCSIOP_ASYN_MSG = 0x32000000,
    /** Reject. */
    ISCSIOP_REJECT = 0x3f000000
} ISCSIOPCODE;

/** Mask for extracting the iSCSI opcode out of the first header word. */
#define ISCSIOP_MASK 0x3f000000


/** ISCSI BHS word 0: Request should be processed immediately. */
#define ISCSI_IMMEDIATE_DELIVERY_BIT 0x40000000

/** ISCSI BHS word 0: This is the final PDU for this request/response. */
#define ISCSI_FINAL_BIT 0x00800000
/** ISCSI BHS word 0: Mask for extracting the CSG. */
#define ISCSI_CSG_MASK 0x000c0000
/** ISCSI BHS word 0: Shift offset for extracting the CSG. */
#define ISCSI_CSG_SHIFT 18
/** ISCSI BHS word 0: Mask for extracting the NSG. */
#define ISCSI_NSG_MASK 0x00030000
/** ISCSI BHS word 0: Shift offset for extracting the NSG. */
#define ISCSI_NSG_SHIFT 16

/** ISCSI BHS word 0: task attribute untagged */
#define ISCSI_TASK_ATTR_UNTAGGED 0x00000000
/** ISCSI BHS word 0: task attribute simple */
#define ISCSI_TASK_ATTR_SIMPLE 0x00010000
/** ISCSI BHS word 0: task attribute ordered */
#define ISCSI_TASK_ATTR_ORDERED 0x00020000
/** ISCSI BHS word 0: task attribute head of queue */
#define ISCSI_TASK_ATTR_HOQ 0x00030000
/** ISCSI BHS word 0: task attribute ACA */
#define ISCSI_TASK_ATTR_ACA 0x00040000

/** ISCSI BHS word 0: transit to next login phase. */
#define ISCSI_TRANSIT_BIT 0x00800000
/** ISCSI BHS word 0: continue with login negotiation. */
#define ISCSI_CONTINUE_BIT 0x00400000

/** ISCSI BHS word 0: residual underflow. */
#define ISCSI_RESIDUAL_UNFL_BIT 0x00020000
/** ISCSI BHS word 0: residual overflow. */
#define ISCSI_RESIDUAL_OVFL_BIT 0x00040000
/** ISCSI BHS word 0: Bidirectional read residual underflow. */
#define ISCSI_BI_READ_RESIDUAL_UNFL_BIT 0x00080000
/** ISCSI BHS word 0: Bidirectional read residual overflow. */
#define ISCSI_BI_READ_RESIDUAL_OVFL_BIT 0x00100000

/** ISCSI BHS word 0: SCSI response mask. */
#define ISCSI_SCSI_RESPONSE_MASK 0x0000ff00
/** ISCSI BHS word 0: SCSI status mask. */
#define ISCSI_SCSI_STATUS_MASK 0x000000ff

/** ISCSI BHS word 0: response includes status. */
#define ISCSI_STATUS_BIT 0x00010000

/** Maximum number of scatter/gather segments needed to send a PDU. */
#define ISCSI_SG_SEGMENTS_MAX 4

/** Number of entries in the command table. */
#define ISCSI_CMD_WAITING_ENTRIES 32

/**
 * iSCSI login status class. */
typedef enum ISCSILOGINSTATUSCLASS
{
    /** Success. */
    ISCSI_LOGIN_STATUS_CLASS_SUCCESS = 0,
    /** Redirection. */
    ISCSI_LOGIN_STATUS_CLASS_REDIRECTION,
    /** Initiator error. */
    ISCSI_LOGIN_STATUS_CLASS_INITIATOR_ERROR,
    /** Target error. */
    ISCSI_LOGIN_STATUS_CLASS_TARGET_ERROR
} ISCSILOGINSTATUSCLASS;


/**
 * iSCSI connection state. */
typedef enum ISCSISTATE
{
    /** Not having a connection/session at all. */
    ISCSISTATE_FREE,
    /** Currently trying to login. */
    ISCSISTATE_IN_LOGIN,
    /** Normal operation, corresponds roughly to the Full Feature Phase. */
    ISCSISTATE_NORMAL,
    /** Currently trying to logout. */
    ISCSISTATE_IN_LOGOUT
} ISCSISTATE;

/**
 * iSCSI PDU send/receive flags (and maybe more in the future). */
typedef enum ISCSIPDUFLAGS
{
    /** No special flags */
    ISCSIPDU_DEFAULT = 0,
    /** Do not attempt to re-attach to the target if the connection is lost */
    ISCSIPDU_NO_REATTACH = RT_BIT(1)
} ISCSIPDUFLAGS;


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * iSCSI login negotiation parameter
 */
typedef struct ISCSIPARAMETER
{
    /** Name of the parameter. */
    const char *pszParamName;
    /** Value of the parameter. */
    const char *pszParamValue;
    /** Length of the binary parameter. 0=zero-terminated string. */
    size_t cbParamValue;
} ISCSIPARAMETER;


/**
 * iSCSI Response PDU buffer (scatter).
 */
typedef struct ISCSIRES
{
    /** Length of PDU segment. */
    size_t cbSeg;
    /** Pointer to PDU segment. */
    void *pvSeg;
} ISCSIRES;
/** Pointer to an iSCSI Response PDU buffer. */
typedef ISCSIRES *PISCSIRES;
/** Pointer to a const iSCSI Response PDU buffer. */
typedef ISCSIRES const *PCISCSIRES;


/**
 * iSCSI Request PDU buffer (gather).
 */
typedef struct ISCSIREQ
{
    /** Length of PDU segment in bytes. */
    size_t cbSeg;
    /** Pointer to PDU segment. */
    const void *pcvSeg;
} ISCSIREQ;
/** Pointer to an iSCSI Request PDU buffer. */
typedef ISCSIREQ *PISCSIREQ;
/** Pointer to a const iSCSI Request PDU buffer. */
typedef ISCSIREQ const *PCISCSIREQ;


/**
 * SCSI transfer directions.
 */
typedef enum SCSIXFER
{
    SCSIXFER_NONE = 0,
    SCSIXFER_TO_TARGET,
    SCSIXFER_FROM_TARGET,
    SCSIXFER_TO_FROM_TARGET
} SCSIXFER, *PSCSIXFER;

/** Forward declaration. */
typedef struct ISCSIIMAGE *PISCSIIMAGE;

/**
 * SCSI request structure.
 */
typedef struct SCSIREQ
{
    /** I/O context associated with this request. */
    PVDIOCTX        pIoCtx;
    /** Transfer direction. */
    SCSIXFER        enmXfer;
    /** Length of command block. */
    size_t          cbCDB;
    /** Length of Initiator2Target data buffer. */
    size_t          cbI2TData;
    /** Length of Target2Initiator data buffer. */
    size_t          cbT2IData;
    /** Length of sense buffer
     * This contains the number of sense bytes received upon completion. */
    size_t          cbSense;
    /** Completion status of the command. */
    uint8_t         status;
    /** The CDB. */
    uint8_t         abCDB[16];
    /** The sense buffer. */
    uint8_t         abSense[96];
    /** Status code to return if we got sense data. */
    int             rcSense;
    /** Pointer to the Initiator2Target S/G list. */
    PRTSGSEG        paI2TSegs;
    /** Number of entries in the I2T S/G list. */
    unsigned        cI2TSegs;
    /** Pointer to the Target2Initiator S/G list. */
    PRTSGSEG        paT2ISegs;
    /** Number of entries in the T2I S/G list. */
    unsigned        cT2ISegs;
    /** S/G buffer for the target to initiator bits. */
    RTSGBUF         SgBufT2I;
    /** Number of retries if the command completes with sense
     * data before we return with an error.
     */
    unsigned        cSenseRetries;
    /** The S/G list - variable in size.
     * This array holds both the I2T and T2I segments.
     * The I2T segments are first and the T2I are second.
     */
    RTSGSEG         aSegs[1];
} SCSIREQ, *PSCSIREQ;

typedef enum ISCSICMDTYPE
{
    /** Process a SCSI request. */
    ISCSICMDTYPE_REQ = 0,
    /** Call a function in the I/O thread. */
    ISCSICMDTYPE_EXEC,
    /** Usual 32bit hack. */
    ISCSICMDTYPE_32BIT_HACK = 0x7fffffff
} ISCSICMDTYPE;


/** The command completion function. */
typedef DECLCALLBACKTYPE(void, FNISCSICMDCOMPLETED,(PISCSIIMAGE pImage, int rcReq, void *pvUser));
/** Pointer to a command completion function. */
typedef FNISCSICMDCOMPLETED *PFNISCSICMDCOMPLETED;

/** The command execution function. */
typedef DECLCALLBACKTYPE(int, FNISCSIEXEC,(void *pvUser));
/** Pointer to a command execution function. */
typedef FNISCSIEXEC *PFNISCSIEXEC;

/**
 * Structure used to complete a synchronous request.
 */
typedef struct ISCSICMDSYNC
{
    /** Event semaphore to wakeup the waiting thread. */
    RTSEMEVENT EventSem;
    /** Status code of the command. */
    int        rcCmd;
} ISCSICMDSYNC, *PISCSICMDSYNC;

/**
 * iSCSI command.
 * Used to forward requests to the I/O thread
 * if existing.
 */
typedef struct ISCSICMD
{
    /** Next one in the list. */
    struct ISCSICMD      *pNext;
    /** Assigned ITT. */
    uint32_t              Itt;
    /** Completion callback. */
    PFNISCSICMDCOMPLETED  pfnComplete;
    /** Opaque user data. */
    void                 *pvUser;
    /** Command to execute. */
    ISCSICMDTYPE          enmCmdType;
    /** Command type dependent data. */
    union
    {
        /** Process a SCSI request. */
        struct
        {
            /** The SCSI request to process. */
            PSCSIREQ      pScsiReq;
        } ScsiReq;
        /** Call a function in the I/O thread. */
        struct
        {
            /** The method to execute. */
            PFNISCSIEXEC  pfnExec;
            /** User data. */
            void         *pvUser;
        } Exec;
    } CmdType;
} ISCSICMD, *PISCSICMD;

/**
 *  Send iSCSI PDU.
 *  Contains all necessary data to send a PDU.
 */
typedef struct ISCSIPDUTX
{
    /** Pointer to the next PDu to send. */
    struct ISCSIPDUTX *pNext;
    /** The BHS. */
    uint32_t    aBHS[12];
    /** Assigned CmdSN for this PDU. */
    uint32_t    CmdSN;
    /** The S/G buffer used for sending. */
    RTSGBUF     SgBuf;
    /** Number of bytes to send until the PDU completed. */
    size_t      cbSgLeft;
    /** The iSCSI command this PDU belongs to. */
    PISCSICMD   pIScsiCmd;
    /** Number of segments in the request segments array. */
    unsigned    cISCSIReq;
    /** The request segments - variable in size. */
    RTSGSEG     aISCSIReq[1];
} ISCSIPDUTX, *PISCSIPDUTX;

/**
 * Block driver instance data.
 */
typedef struct ISCSIIMAGE
{
    /** Pointer to the filename (location). Not really used. */
    const char          *pszFilename;
    /** Pointer to the initiator name. */
    char                *pszInitiatorName;
    /** Pointer to the target name. */
    char                *pszTargetName;
    /** Pointer to the target address. */
    char                *pszTargetAddress;
    /** Pointer to the user name for authenticating the Initiator. */
    char                *pszInitiatorUsername;
    /** Pointer to the secret for authenticating the Initiator. */
    uint8_t             *pbInitiatorSecret;
    /** Length of the secret for authenticating the Initiator. */
    size_t              cbInitiatorSecret;
    /** Pointer to the user name for authenticating the Target. */
    char                *pszTargetUsername;
    /** Pointer to the secret for authenticating the Initiator. */
    uint8_t             *pbTargetSecret;
    /** Length of the secret for authenticating the Initiator. */
    size_t              cbTargetSecret;
    /** Limit for iSCSI writes, essentially limiting the amount of data
     * written in a single write. This is negotiated with the target, so
     * the actual size might be smaller. */
    uint32_t            cbWriteSplit;
    /** Initiator session identifier. */
    uint64_t            ISID;
    /** SCSI Logical Unit Number. */
    uint64_t            LUN;
    /** Pointer to the per-disk VD interface list. */
    PVDINTERFACE        pVDIfsDisk;
    /** Pointer to the per-image VD interface list. */
    PVDINTERFACE        pVDIfsImage;
    /** Error interface. */
    PVDINTERFACEERROR   pIfError;
    /** Config interface. */
    PVDINTERFACECONFIG  pIfConfig;
    /** I/O interface. */
    PVDINTERFACEIOINT   pIfIo;
    /** TCP network stack interface. */
    PVDINTERFACETCPNET  pIfNet;
    /** Image open flags. */
    unsigned            uOpenFlags;
    /** Number of re-login retries when a connection fails. */
    uint32_t            cISCSIRetries;
    /** Sector size on volume. */
    uint32_t            cbSector;
    /** Size of volume in sectors. */
    uint64_t            cVolume;
    /** Total volume size in bytes. Easier than multiplying the above values all the time. */
    uint64_t            cbSize;

    /** Negotiated maximum data length when sending to target. */
    uint32_t            cbSendDataLength;
    /** Negotiated maximum data length when receiving from target. */
    uint32_t            cbRecvDataLength;

    /** Current state of the connection/session. */
    ISCSISTATE          state;
    /** Flag whether the first Login Response PDU has been seen. */
    bool                FirstRecvPDU;
    /** Initiator Task Tag of the last iSCSI request PDU. */
    uint32_t            ITT;
    /** Sequence number of the last command. */
    uint32_t            CmdSN;
    /** Sequence number of the next command expected by the target. */
    uint32_t            ExpCmdSN;
    /** Maximum sequence number accepted by the target (determines size of window). */
    uint32_t            MaxCmdSN;
    /** Expected sequence number of next status. */
    uint32_t            ExpStatSN;
    /** Currently active request. */
    PISCSIREQ           paCurrReq;
    /** Segment number of currently active request. */
    uint32_t            cnCurrReq;
    /** Pointer to receive PDU buffer. (Freed by RT) */
    void                *pvRecvPDUBuf;
    /** Length of receive PDU buffer. */
    size_t              cbRecvPDUBuf;
    /** Mutex protecting against concurrent use from several threads. */
    RTSEMMUTEX          Mutex;

    /** Pointer to the target hostname. */
    char                *pszHostname;
    /** Port to use on the target host. */
    uint32_t            uPort;
    /** Socket handle of the TCP connection. */
    VDSOCKET            Socket;
    /** Timeout for read operations on the TCP connection (in milliseconds). */
    uint32_t            uReadTimeout;
    /** Flag whether to automatically generate the initiator name. */
    bool                fAutomaticInitiatorName;
    /** Flag whether to automatically determine the LUN. */
    bool                fAutomaticLUN;
    /** Flag whether to use the host IP stack or DevINIP. */
    bool                fHostIP;
    /** Flag whether to dump malformed packets in the release log. */
    bool                fDumpMalformedPackets;
    /** Flag whtether the target is readonly. */
    bool                fTargetReadOnly;
    /** Flag whether to retry the connection before processing new requests. */
    bool                fTryReconnect;

    /** Head of request queue */
    PISCSICMD           pScsiReqQueue;
    /** Mutex protecting the request queue from concurrent access. */
    RTSEMMUTEX          MutexReqQueue;
    /** I/O thread. */
    RTTHREAD            hThreadIo;
    /** Flag whether the thread should be still running. */
    volatile bool       fRunning;
    /* Flag whether the target supports command queuing. */
    bool                fCmdQueuingSupported;
    /** Flag whether extended select is supported. */
    bool                fExtendedSelectSupported;
    /** Padding used for aligning the PDUs. */
    uint8_t             aPadding[4];
    /** Socket events to poll for. */
    uint32_t            fPollEvents;
    /** Number of bytes to read to complete the current PDU. */
    size_t              cbRecvPDUResidual;
    /** Current position in the PDU buffer. */
    uint8_t             *pbRecvPDUBufCur;
    /** Flag whether we are currently reading the BHS. */
    bool                fRecvPDUBHS;
    /** List of PDUs waiting to get transmitted. */
    PISCSIPDUTX         pIScsiPDUTxHead;
    /** Tail of PDUs waiting to get transmitted. */
    PISCSIPDUTX         pIScsiPDUTxTail;
    /** PDU we are currently transmitting. */
    PISCSIPDUTX         pIScsiPDUTxCur;
    /** Number of commands waiting for an answer from the target.
     * Used for timeout handling for poll.
     */
    unsigned            cCmdsWaiting;
    /** Table of commands waiting for a response from the target. */
    PISCSICMD           aCmdsWaiting[ISCSI_CMD_WAITING_ENTRIES];
    /** Number of logins since last successful I/O.
     * Used to catch the case where logging succeeds but
     * processing read/write/flushes cause a disconnect.
     */
    volatile uint32_t   cLoginsSinceIo;

    /** Release log counter. */
    unsigned            cLogRelErrors;
    /** The static region list. */
    VDREGIONLIST        RegionList;
} ISCSIIMAGE;


/*********************************************************************************************************************************
*   Static Variables                                                                                                             *
*********************************************************************************************************************************/

/** Default initiator basename. */
static const char *s_iscsiDefaultInitiatorBasename = "iqn.2009-08.com.sun.virtualbox.initiator";

/** Default LUN. */
static const char *s_iscsiConfigDefaultLUN = "0";

/** Default timeout, 10 seconds. */
static const char *s_iscsiConfigDefaultTimeout = "10000";

/** Default write split value, less or equal to ISCSI_DATA_LENGTH_MAX. */
static const char *s_iscsiConfigDefaultWriteSplit = "262144";

/** Default host IP stack. */
static const char *s_iscsiConfigDefaultHostIPStack = "1";

/** Default dump malformed packet configuration value. */
static const char *s_iscsiConfigDefaultDumpMalformedPackets = "0";

/** Description of all accepted config parameters. */
static const VDCONFIGINFO s_iscsiConfigInfo[] =
{
    { "TargetName",           NULL,                                      VDCFGVALUETYPE_STRING,  VD_CFGKEY_MANDATORY },
    /* LUN is defined of string type to handle the "enc" prefix. */
    { "LUN",                  s_iscsiConfigDefaultLUN,                   VDCFGVALUETYPE_STRING,  VD_CFGKEY_MANDATORY },
    { "TargetAddress",        NULL,                                      VDCFGVALUETYPE_STRING,  VD_CFGKEY_MANDATORY },
    { "InitiatorName",        NULL,                                      VDCFGVALUETYPE_STRING,  0 },
    { "InitiatorUsername",    NULL,                                      VDCFGVALUETYPE_STRING,  0 },
    { "InitiatorSecret",      NULL,                                      VDCFGVALUETYPE_BYTES,   0 },
    { "TargetUsername",       NULL,                                      VDCFGVALUETYPE_STRING,  VD_CFGKEY_EXPERT },
    { "TargetSecret",         NULL,                                      VDCFGVALUETYPE_BYTES,   VD_CFGKEY_EXPERT },
    { "WriteSplit",           s_iscsiConfigDefaultWriteSplit,            VDCFGVALUETYPE_INTEGER, VD_CFGKEY_EXPERT },
    { "Timeout",              s_iscsiConfigDefaultTimeout,               VDCFGVALUETYPE_INTEGER, VD_CFGKEY_EXPERT },
    { "HostIPStack",          s_iscsiConfigDefaultHostIPStack,           VDCFGVALUETYPE_INTEGER, VD_CFGKEY_EXPERT },
    { "DumpMalformedPackets", s_iscsiConfigDefaultDumpMalformedPackets,  VDCFGVALUETYPE_INTEGER, VD_CFGKEY_EXPERT },
    { NULL,                   NULL,                                      VDCFGVALUETYPE_INTEGER, 0 }
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/* iSCSI low-level functions (only to be used from the iSCSI high-level functions). */
static uint32_t iscsiNewITT(PISCSIIMAGE pImage);
static int iscsiSendPDU(PISCSIIMAGE pImage, PISCSIREQ paReq, uint32_t cnReq, uint32_t uFlags);
static int iscsiRecvPDU(PISCSIIMAGE pImage, uint32_t itt, PISCSIRES paRes, uint32_t cnRes, uint32_t fFlags);
static int iscsiRecvPDUAsync(PISCSIIMAGE pImage);
static int iscsiSendPDUAsync(PISCSIIMAGE pImage);
static int iscsiValidatePDU(PISCSIRES paRes, uint32_t cnRes);
static int iscsiRecvPDUProcess(PISCSIIMAGE pImage, PISCSIRES paRes, uint32_t cnRes);
static int iscsiPDUTxPrepare(PISCSIIMAGE pImage, PISCSICMD pIScsiCmd);
static int iscsiRecvPDUUpdateRequest(PISCSIIMAGE pImage, PISCSIRES paRes, uint32_t cnRes);
static void iscsiCmdComplete(PISCSIIMAGE pImage, PISCSICMD pIScsiCmd, int rcCmd);
static int iscsiTextAddKeyValue(uint8_t *pbBuf, size_t cbBuf, size_t *pcbBufCurr, const char *pcszKey, const char *pcszValue, size_t cbValue);
static int iscsiTextGetKeyValue(const uint8_t *pbBuf, size_t cbBuf, const char *pcszKey, const char **ppcszValue);
static int iscsiStrToBinary(const char *pcszValue, uint8_t *pbValue, size_t *pcbValue);
static int iscsiUpdateParameters(PISCSIIMAGE pImage, const uint8_t *pbBuf, size_t cbBuf);

/* Serial number arithmetic comparison. */
static bool serial_number_less(uint32_t sn1, uint32_t sn2);
static bool serial_number_greater(uint32_t sn1, uint32_t sn2);

/* CHAP-MD5 functions. */
#ifdef IMPLEMENT_TARGET_AUTH
static void chap_md5_generate_challenge(uint8_t *pbChallenge, size_t *pcbChallenge);
#endif
static void chap_md5_compute_response(uint8_t *pbResponse, uint8_t id, const uint8_t *pbChallenge, size_t cbChallenge,
                                      const uint8_t *pbSecret, size_t cbSecret);

/**
 * Internal: release log wrapper limiting the number of entries.
 */
DECLINLINE(void) iscsiLogRel(PISCSIIMAGE pImage, const char *pcszFormat, ...)
{
    if (pImage->cLogRelErrors++ < MAX_LOG_REL_ERRORS)
    {
        va_list va;

        va_start(va, pcszFormat);
        LogRel(("%N\n", pcszFormat, &va));
        va_end(va);
    }
}

DECLINLINE(bool) iscsiIsClientConnected(PISCSIIMAGE pImage)
{
    return    pImage->Socket != NIL_VDSOCKET
           && pImage->pIfNet->pfnIsClientConnected(pImage->Socket);
}

/**
 * Calculates the hash for the given ITT used
 * to look up the command in the table.
 */
DECLINLINE(uint32_t) iscsiIttHash(uint32_t Itt)
{
    return Itt % ISCSI_CMD_WAITING_ENTRIES;
}

static PISCSICMD iscsiCmdGetFromItt(PISCSIIMAGE pImage, uint32_t Itt)
{
    PISCSICMD pIScsiCmd = NULL;

    pIScsiCmd = pImage->aCmdsWaiting[iscsiIttHash(Itt)];

    while (   pIScsiCmd
           && pIScsiCmd->Itt != Itt)
        pIScsiCmd = pIScsiCmd->pNext;

    return pIScsiCmd;
}

static void iscsiCmdInsert(PISCSIIMAGE pImage, PISCSICMD pIScsiCmd)
{
    PISCSICMD pIScsiCmdOld;
    uint32_t idx = iscsiIttHash(pIScsiCmd->Itt);

    Assert(!pIScsiCmd->pNext);

    pIScsiCmdOld = pImage->aCmdsWaiting[idx];
    pIScsiCmd->pNext = pIScsiCmdOld;
    pImage->aCmdsWaiting[idx] = pIScsiCmd;
    pImage->cCmdsWaiting++;
}

static PISCSICMD iscsiCmdRemove(PISCSIIMAGE pImage, uint32_t Itt)
{
    PISCSICMD pIScsiCmd = NULL;
    PISCSICMD pIScsiCmdPrev = NULL;
    uint32_t idx = iscsiIttHash(Itt);

    pIScsiCmd = pImage->aCmdsWaiting[idx];

    while (   pIScsiCmd
           && pIScsiCmd->Itt != Itt)
    {
        pIScsiCmdPrev = pIScsiCmd;
        pIScsiCmd = pIScsiCmd->pNext;
    }

    if (pIScsiCmd)
    {
        if (pIScsiCmdPrev)
        {
            AssertPtrNull(pIScsiCmd->pNext);
            pIScsiCmdPrev->pNext = pIScsiCmd->pNext;
        }
        else
        {
            pImage->aCmdsWaiting[idx] = pIScsiCmd->pNext;
            AssertPtrNull(pImage->aCmdsWaiting[idx]);
        }
        pImage->cCmdsWaiting--;
    }

    return pIScsiCmd;
}

/**
 * Removes all commands from the table and returns the
 * list head
 *
 * @returns Pointer to the head of the command list.
 * @param   pImage    iSCSI connection to use.
 */
static PISCSICMD iscsiCmdRemoveAll(PISCSIIMAGE pImage)
{
    PISCSICMD pIScsiCmdHead = NULL;

    for (unsigned idx = 0; idx < RT_ELEMENTS(pImage->aCmdsWaiting); idx++)
    {
        PISCSICMD pHead;
        PISCSICMD pTail;

        pHead = pImage->aCmdsWaiting[idx];
        pImage->aCmdsWaiting[idx] = NULL;

        if (pHead)
        {
            /* Get the tail. */
            pTail = pHead;
            while (pTail->pNext)
                pTail = pTail->pNext;

            /* Concatenate. */
            pTail->pNext = pIScsiCmdHead;
            pIScsiCmdHead = pHead;
        }
    }
    pImage->cCmdsWaiting = 0;

    return pIScsiCmdHead;
}

/**
 * Dumps an iSCSI packet if enabled.
 *
 * @param   pImage         The iSCSI image instance data.
 * @param   paISCSISegs    Pointer to the segments array.
 * @param   cnISCSISegs    Number of segments in the array.
 * @param   rc             Status code for this packet.
 * @param   fRequest       Flag whether this is request or response packet.
 */
static void iscsiDumpPacket(PISCSIIMAGE pImage, PISCSIREQ paISCSISegs, unsigned cnISCSISegs, int rc, bool fRequest)
{
    if (pImage->fDumpMalformedPackets)
    {
        LogRel(("iSCSI{%s}: Dumping %s packet completed with status code %Rrc\n", pImage->pszTargetName, fRequest ? "request" : "response", rc));
        for (unsigned i = 0; i < cnISCSISegs; i++)
        {
            if (paISCSISegs[i].cbSeg)
            {
                LogRel(("iSCSI{%s}: Segment %u, size %zu\n"
                        "%.*Rhxd\n",
                        pImage->pszTargetName, i, paISCSISegs[i].cbSeg,
                        paISCSISegs[i].cbSeg, paISCSISegs[i].pcvSeg));
            }
        }
    }
}

static int iscsiTransportConnect(PISCSIIMAGE pImage)
{
    int rc;
    if (!pImage->pszHostname)
        return VERR_NET_DEST_ADDRESS_REQUIRED;

    rc = pImage->pIfNet->pfnClientConnect(pImage->Socket, pImage->pszHostname, pImage->uPort, pImage->uReadTimeout);
    if (RT_FAILURE(rc))
    {
        if (   rc == VERR_NET_CONNECTION_REFUSED
            || rc == VERR_NET_CONNECTION_RESET
            || rc == VERR_NET_UNREACHABLE
            || rc == VERR_NET_HOST_UNREACHABLE
            || rc == VERR_NET_CONNECTION_TIMED_OUT)
        {
            /* Standardize return value for no connection. */
            rc = VERR_NET_CONNECTION_REFUSED;
        }
        return rc;
    }

    /* Disable Nagle algorithm, we want things to be sent immediately. */
    pImage->pIfNet->pfnSetSendCoalescing(pImage->Socket, false);

    /* Make initiator name and ISID unique on this host. */
    RTNETADDR LocalAddr;
    rc = pImage->pIfNet->pfnGetLocalAddress(pImage->Socket, &LocalAddr);
    if (RT_FAILURE(rc))
        return rc;
    if (   LocalAddr.uPort == RTNETADDR_PORT_NA
        || LocalAddr.uPort > 65535)
        return VERR_NET_ADDRESS_FAMILY_NOT_SUPPORTED;
    pImage->ISID &= ~65535ULL;
    pImage->ISID |= LocalAddr.uPort;
    /* Eliminate the port so that it isn't included below. */
    LocalAddr.uPort = RTNETADDR_PORT_NA;
    if (pImage->fAutomaticInitiatorName)
    {
        if (pImage->pszInitiatorName)
            RTStrFree(pImage->pszInitiatorName);
        RTStrAPrintf(&pImage->pszInitiatorName, "%s:01:%RTnaddr",
                     s_iscsiDefaultInitiatorBasename, &LocalAddr);
        if (!pImage->pszInitiatorName)
            return VERR_NO_MEMORY;
    }
    LogRel(("iSCSI: connect from initiator %s with source port %u\n", pImage->pszInitiatorName, pImage->ISID & 65535));
    return VINF_SUCCESS;
}


static int iscsiTransportClose(PISCSIIMAGE pImage)
{
    int rc;

    LogFlowFunc(("(%s:%d)\n", pImage->pszHostname, pImage->uPort));
    if (iscsiIsClientConnected(pImage))
    {
        LogRel(("iSCSI: disconnect from initiator %s with source port %u\n", pImage->pszInitiatorName, pImage->ISID & 65535));
        rc = pImage->pIfNet->pfnClientClose(pImage->Socket);
    }
    else
        rc = VINF_SUCCESS;
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


static int iscsiTransportRead(PISCSIIMAGE pImage, PISCSIRES paResponse, unsigned int cnResponse)
{
    int rc = VINF_SUCCESS;
    unsigned int i = 0;
    size_t cbToRead, cbActuallyRead, residual, cbSegActual = 0, cbAHSLength, cbDataLength;
    char *pDst;

    LogFlowFunc(("cnResponse=%d (%s:%d)\n", cnResponse, pImage->pszHostname, pImage->uPort));
    if (!iscsiIsClientConnected(pImage))
    {
        /* Reconnecting makes no sense in this case, as there will be nothing
         * to receive. We would just run into a timeout. */
        rc = VERR_BROKEN_PIPE;
    }

    if (RT_SUCCESS(rc) && paResponse[0].cbSeg >= ISCSI_BHS_SIZE)
    {
        cbToRead = 0;
        residual = ISCSI_BHS_SIZE;  /* Do not read more than the BHS length before the true PDU length is known. */
        cbSegActual = residual;
        pDst = (char *)paResponse[i].pvSeg;
        uint64_t u64Timeout = RTTimeMilliTS() + pImage->uReadTimeout;
        do
        {
            int64_t cMilliesRemaining = u64Timeout - RTTimeMilliTS();
            if (cMilliesRemaining <= 0)
            {
                rc = VERR_TIMEOUT;
                break;
            }
            Assert(cMilliesRemaining < 1000000);
            rc = pImage->pIfNet->pfnSelectOne(pImage->Socket, cMilliesRemaining);
            if (RT_FAILURE(rc))
                break;
            rc = pImage->pIfNet->pfnRead(pImage->Socket, pDst, residual, &cbActuallyRead);
            if (RT_FAILURE(rc))
                break;
            if (cbActuallyRead == 0)
            {
                /* The other end has closed the connection. */
                iscsiTransportClose(pImage);
                pImage->state = ISCSISTATE_FREE;
                rc = VERR_NET_CONNECTION_RESET;
                break;
            }
            if (cbToRead == 0)
            {
                /* Currently reading the BHS. */
                residual -= cbActuallyRead;
                pDst += cbActuallyRead;
                if (residual <= 40)
                {
                    /* Enough data read to figure out the actual PDU size. */
                    uint32_t word1 = RT_N2H_U32(((uint32_t *)(paResponse[0].pvSeg))[1]);
                    cbAHSLength = (word1 & 0xff000000) >> 24;
                    cbAHSLength = ((cbAHSLength - 1) | 3) + 1;      /* Add padding. */
                    cbDataLength = word1 & 0x00ffffff;
                    cbDataLength = ((cbDataLength - 1) | 3) + 1;    /* Add padding. */
                    cbToRead = residual + cbAHSLength + cbDataLength;
                    residual += paResponse[0].cbSeg - ISCSI_BHS_SIZE;
                    if (residual > cbToRead)
                        residual = cbToRead;
                    cbSegActual = ISCSI_BHS_SIZE + cbAHSLength + cbDataLength;
                    /* Check whether we are already done with this PDU (no payload). */
                    if (cbToRead == 0)
                        break;
                }
            }
            else
            {
                cbToRead -= cbActuallyRead;
                if (cbToRead == 0)
                    break;
                pDst += cbActuallyRead;
                residual -= cbActuallyRead;
            }
            if (residual == 0)
            {
                i++;
                if (i >= cnResponse)
                {
                    /* No space left in receive buffers. */
                    rc = VERR_BUFFER_OVERFLOW;
                    break;
                }
                pDst = (char *)paResponse[i].pvSeg;
                residual = paResponse[i].cbSeg;
                if (residual > cbToRead)
                    residual = cbToRead;
                cbSegActual = residual;
            }
            LogFlowFunc(("cbToRead=%u residual=%u cbSegActual=%u cbActuallRead=%u\n",
                         cbToRead, residual, cbSegActual, cbActuallyRead));
        } while (true);
    }
    else
    {
        if (RT_SUCCESS(rc))
            rc = VERR_BUFFER_OVERFLOW;
    }
    if (RT_SUCCESS(rc))
    {
        paResponse[i].cbSeg = cbSegActual;
        for (i++; i < cnResponse; i++)
            paResponse[i].cbSeg = 0;
    }

    if (RT_UNLIKELY(    RT_FAILURE(rc)
                    && (   rc == VERR_NET_CONNECTION_RESET
                        || rc == VERR_NET_CONNECTION_ABORTED
                        || rc == VERR_NET_CONNECTION_RESET_BY_PEER
                        || rc == VERR_NET_CONNECTION_REFUSED
                        || rc == VERR_BROKEN_PIPE)))
    {
        /* Standardize return value for broken connection. */
        rc = VERR_BROKEN_PIPE;
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


static int iscsiTransportWrite(PISCSIIMAGE pImage, PISCSIREQ paRequest, unsigned int cnRequest)
{
    int rc = VINF_SUCCESS;
    unsigned int i;

    LogFlowFunc(("cnRequest=%d (%s:%d)\n", cnRequest, pImage->pszHostname, pImage->uPort));
    if (!iscsiIsClientConnected(pImage))
    {
        /* Attempt to reconnect if the connection was previously broken. */
        rc = iscsiTransportConnect(pImage);
    }

    if (RT_SUCCESS(rc))
    {
        /* Construct scatter/gather buffer for entire request, worst case
         * needs twice as many entries to allow for padding. */
        unsigned cBuf = 0;
        for (i = 0; i < cnRequest; i++)
        {
            cBuf++;
            if (paRequest[i].cbSeg & 3)
                cBuf++;
        }
        Assert(cBuf < ISCSI_SG_SEGMENTS_MAX);
        RTSGBUF buf;
        RTSGSEG aSeg[ISCSI_SG_SEGMENTS_MAX];
        static char aPad[4] = { 0, 0, 0, 0 };
        RTSgBufInit(&buf, &aSeg[0], cBuf);
        unsigned iBuf = 0;
        for (i = 0; i < cnRequest; i++)
        {
            /* Actual data chunk. */
            aSeg[iBuf].pvSeg = (void *)paRequest[i].pcvSeg;
            aSeg[iBuf].cbSeg = paRequest[i].cbSeg;
            iBuf++;
            /* Insert proper padding before the next chunk. */
            if (paRequest[i].cbSeg & 3)
            {
                aSeg[iBuf].pvSeg = &aPad[0];
                aSeg[iBuf].cbSeg = 4 - (paRequest[i].cbSeg & 3);
                iBuf++;
            }
        }
        /* Send out the request, the socket is set to send data immediately,
         * avoiding unnecessary delays. */
        rc = pImage->pIfNet->pfnSgWrite(pImage->Socket, &buf);

    }

    if (RT_UNLIKELY(    RT_FAILURE(rc)
                    && (   rc == VERR_NET_CONNECTION_RESET
                        || rc == VERR_NET_CONNECTION_ABORTED
                        || rc == VERR_NET_CONNECTION_RESET_BY_PEER
                        || rc == VERR_NET_CONNECTION_REFUSED
                        || rc == VERR_BROKEN_PIPE)))
    {
        /* Standardize return value for broken connection. */
        rc = VERR_BROKEN_PIPE;
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


static int iscsiTransportOpen(PISCSIIMAGE pImage)
{
    int rc = VINF_SUCCESS;
    size_t cbHostname = 0; /* shut up gcc */
    const char *pcszPort = NULL; /* shut up gcc */
    char *pszPortEnd;
    uint16_t uPort;

    /* Clean up previous connection data. */
    iscsiTransportClose(pImage);
    if (pImage->pszHostname)
    {
        RTMemFree(pImage->pszHostname);
        pImage->pszHostname = NULL;
        pImage->uPort = 0;
    }

    /* Locate the port number via the colon separating the hostname from the port. */
    if (*pImage->pszTargetAddress)
    {
        if (*pImage->pszTargetAddress != '[')
        {
            /* Normal hostname or IPv4 dotted decimal. */
            pcszPort = strchr(pImage->pszTargetAddress, ':');
            if (pcszPort != NULL)
            {
                cbHostname = pcszPort - pImage->pszTargetAddress;
                pcszPort++;
            }
            else
                cbHostname = strlen(pImage->pszTargetAddress);
        }
        else
        {
            /* IPv6 literal address. Contains colons, so skip to closing square bracket. */
            pcszPort = strchr(pImage->pszTargetAddress, ']');
            if (pcszPort != NULL)
            {
                pcszPort++;
                cbHostname = pcszPort - pImage->pszTargetAddress;
                if (*pcszPort == '\0')
                    pcszPort = NULL;
                else if (*pcszPort != ':')
                    rc = VERR_PARSE_ERROR;
                else
                    pcszPort++;
            }
            else
                rc = VERR_PARSE_ERROR;
        }
    }
    else
        rc = VERR_PARSE_ERROR;

    /* Now split address into hostname and port. */
    if (RT_SUCCESS(rc))
    {
        pImage->pszHostname = (char *)RTMemAlloc(cbHostname + 1);
        if (!pImage->pszHostname)
            rc = VERR_NO_MEMORY;
        else
        {
            if (pImage->pszTargetAddress[0] == '[')
                memcpy(pImage->pszHostname, pImage->pszTargetAddress + 1, cbHostname);
            else
                memcpy(pImage->pszHostname, pImage->pszTargetAddress, cbHostname);
            pImage->pszHostname[cbHostname] = '\0';
            if (pcszPort != NULL)
            {
                rc = RTStrToUInt16Ex(pcszPort, &pszPortEnd, 0, &uPort);
                /* Note that RT_SUCCESS() macro to check the rc value is not strict enough in this case. */
                if (rc == VINF_SUCCESS && *pszPortEnd == '\0' && uPort != 0)
                {
                    pImage->uPort = uPort;
                }
                else
                {
                    rc = VERR_PARSE_ERROR;
                }
            }
            else
                pImage->uPort = ISCSI_DEFAULT_PORT;
        }
    }

    if (RT_SUCCESS(rc))
    {
        if (!iscsiIsClientConnected(pImage))
            rc = iscsiTransportConnect(pImage);
    }
    else
    {
        if (pImage->pszHostname)
        {
            RTMemFree(pImage->pszHostname);
            pImage->pszHostname = NULL;
        }
        pImage->uPort = 0;
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Returns a human readable version of the given initiator login error detail.
 *
 * @returns String with the error detail.
 * @param   u8Detail        The detail indicator from the response.
 */
static const char *iscsiGetLoginErrorDetail(uint8_t u8Detail)
{
    const char *pszDetail = NULL;

    switch (u8Detail)
    {
        case 0x00:
            pszDetail = "Miscelleanous iSCSI intiaitor error";
            break;
        case 0x01:
            pszDetail = "Authentication failure";
            break;
        case 0x02:
            pszDetail = "Authorization failure";
            break;
        case 0x03:
            pszDetail = "Not found";
            break;
        case 0x04:
            pszDetail = "Target removed";
            break;
        case 0x05:
            pszDetail = "Unsupported version";
            break;
        case 0x06:
            pszDetail = "Too many connections";
            break;
        case 0x07:
            pszDetail = "Missing parameter";
            break;
        case 0x08:
            pszDetail = "Can't include in session";
            break;
        case 0x09:
            pszDetail = "Session type not supported";
            break;
        case 0x0a:
            pszDetail = "Session does not exist";
            break;
        case 0x0b:
            pszDetail = "Invalid request type during login";
            break;
        default:
            pszDetail = "Unknown status detail";
    }

    return pszDetail;
}

/**
 * Attempts one login attempt to the given target.
 *
 * @returns VBox status code.
 * @retval  VINF_TRY_AGAIN when getting redirected and having to start over.
 * @retval  VERR_TRY_AGAIN in case the connection was lost while receiving a reply
 *                         from the target and the login attempt can be repeated.
 * @param   pImage      The iSCSI connection state to be used.
 */
static int iscsiLogin(PISCSIIMAGE pImage)
{
    int rc = VINF_SUCCESS;
    uint32_t itt;
    uint32_t csg, nsg, substate;
    uint64_t isid_tsih;
    uint8_t bBuf[4096];         /* Should be large enough even for large authentication values. */
    size_t cbBuf;
    bool transit;
    uint8_t pbChallenge[1024];  /* RFC3720 specifies this as maximum. */
    size_t cbChallenge = 0;     /* shut up gcc */
    uint8_t bChapIdx = 0;       /* (MSC is used uninitialized) */
    uint8_t aResponse[RTMD5HASHSIZE];
    uint32_t cnISCSIReq = 0;
    ISCSIREQ aISCSIReq[4];
    uint32_t aReqBHS[12];
    uint32_t cnISCSIRes = 0;
    ISCSIRES aISCSIRes[2];
    uint32_t aResBHS[12];
    char *pszNext;
    bool fParameterNeg = true;
    pImage->cbRecvDataLength = ISCSI_DATA_LENGTH_MAX;
    pImage->cbSendDataLength = RT_MIN(ISCSI_DATA_LENGTH_MAX, pImage->cbWriteSplit);
    char szMaxDataLength[16];
    RTStrPrintf(szMaxDataLength, sizeof(szMaxDataLength), "%u", ISCSI_DATA_LENGTH_MAX);
    ISCSIPARAMETER aParameterNeg[] =
    {
        { "HeaderDigest", "None", 0 },
        { "DataDigest", "None", 0 },
        { "MaxConnections", "1", 0 },
        { "InitialR2T", "No", 0 },
        { "ImmediateData", "Yes", 0 },
        { "MaxRecvDataSegmentLength", szMaxDataLength, 0 },
        { "MaxBurstLength", szMaxDataLength, 0 },
        { "FirstBurstLength", szMaxDataLength, 0 },
        { "DefaultTime2Wait", "0", 0 },
        { "DefaultTime2Retain", "60", 0 },
        { "DataPDUInOrder", "Yes", 0 },
        { "DataSequenceInOrder", "Yes", 0 },
        { "ErrorRecoveryLevel", "0", 0 },
        { "MaxOutstandingR2T", "1", 0 }
    };

    if (!iscsiIsClientConnected(pImage))
    {
        rc = iscsiTransportOpen(pImage);
        if (RT_FAILURE(rc))
            return rc;
    }

    pImage->state = ISCSISTATE_IN_LOGIN;
    pImage->ITT = 1;
    pImage->FirstRecvPDU = true;
    pImage->CmdSN = 1;
    pImage->ExpCmdSN = 0;
    pImage->MaxCmdSN = 1;
    pImage->ExpStatSN = 0;

    /*
     * Send login request to target.
     */
    itt = iscsiNewITT(pImage);
    csg = 0;
    nsg = 0;
    substate = 0;
    isid_tsih = pImage->ISID << 16;  /* TSIH field currently always 0 */

    do
    {
        transit = false;
        cbBuf = 0;
        /* Handle all cases with a single switch statement. */
        switch (csg << 8 | substate)
        {
            case 0x0000:    /* security negotiation, step 0: propose authentication. */
                rc = iscsiTextAddKeyValue(bBuf, sizeof(bBuf), &cbBuf, "SessionType", "Normal", 0);
                if (RT_FAILURE(rc))
                    break;
                rc = iscsiTextAddKeyValue(bBuf, sizeof(bBuf), &cbBuf, "InitiatorName", pImage->pszInitiatorName, 0);
                if (RT_FAILURE(rc))
                    break;
                rc = iscsiTextAddKeyValue(bBuf, sizeof(bBuf), &cbBuf, "TargetName", pImage->pszTargetName, 0);
                if (RT_FAILURE(rc))
                    break;
                if (pImage->pszInitiatorUsername == NULL)
                {
                    /* No authentication. Immediately switch to next phase. */
                    rc = iscsiTextAddKeyValue(bBuf, sizeof(bBuf), &cbBuf, "AuthMethod", "None", 0);
                    if (RT_FAILURE(rc))
                        break;
                    nsg = 1;
                    transit = true;
                }
                else
                    rc = iscsiTextAddKeyValue(bBuf, sizeof(bBuf), &cbBuf, "AuthMethod", "CHAP,None", 0);
                break;
            case 0x0001:    /* security negotiation, step 1: propose CHAP_MD5 variant. */
                rc = iscsiTextAddKeyValue(bBuf, sizeof(bBuf), &cbBuf, "CHAP_A", "5", 0);
                break;
            case 0x0002:    /* security negotiation, step 2: send authentication info. */
                rc = iscsiTextAddKeyValue(bBuf, sizeof(bBuf), &cbBuf, "CHAP_N", pImage->pszInitiatorUsername, 0);
                if (RT_FAILURE(rc))
                    break;
                chap_md5_compute_response(aResponse, bChapIdx, pbChallenge, cbChallenge,
                                          pImage->pbInitiatorSecret, pImage->cbInitiatorSecret);
                rc = iscsiTextAddKeyValue(bBuf, sizeof(bBuf), &cbBuf, "CHAP_R", (const char *)aResponse, RTMD5HASHSIZE);
                if (RT_FAILURE(rc))
                    break;
                nsg = 1;
                transit = true;
                break;
            case 0x0100:    /* login operational negotiation, step 0: set parameters. */
                if (fParameterNeg)
                {
                    for (unsigned i = 0; i < RT_ELEMENTS(aParameterNeg); i++)
                    {
                        rc = iscsiTextAddKeyValue(bBuf, sizeof(bBuf), &cbBuf,
                                                  aParameterNeg[i].pszParamName,
                                                  aParameterNeg[i].pszParamValue,
                                                  aParameterNeg[i].cbParamValue);
                        if (RT_FAILURE(rc))
                            break;
                    }
                    fParameterNeg = false;
                }

                nsg = 3;
                transit = true;
                break;
            case 0x0300:    /* full feature phase. */
            default:
                /* Should never come here. */
                AssertMsgFailed(("send: Undefined login state %d substate %d\n", csg, substate));
                break;
        }

        if (RT_FAILURE(rc))
            break;

        aReqBHS[0] = RT_H2N_U32(    ISCSI_IMMEDIATE_DELIVERY_BIT
                                |   (csg << ISCSI_CSG_SHIFT)
                                |   (transit ? (nsg << ISCSI_NSG_SHIFT | ISCSI_TRANSIT_BIT) : 0)
                                |   ISCSI_MY_VERSION            /* Minimum version. */
                                |   (ISCSI_MY_VERSION << 8)     /* Maximum version. */
                                |   ISCSIOP_LOGIN_REQ);     /* C=0 */
        aReqBHS[1] = RT_H2N_U32((uint32_t)cbBuf);     /* TotalAHSLength=0 */
        aReqBHS[2] = RT_H2N_U32(isid_tsih >> 32);
        aReqBHS[3] = RT_H2N_U32(isid_tsih & 0xffffffff);
        aReqBHS[4] = itt;
        aReqBHS[5] = RT_H2N_U32(1 << 16);   /* CID=1,reserved */
        aReqBHS[6] = RT_H2N_U32(pImage->CmdSN);
        aReqBHS[7] = RT_H2N_U32(pImage->ExpStatSN);
        aReqBHS[8] = 0;             /* reserved */
        aReqBHS[9] = 0;             /* reserved */
        aReqBHS[10] = 0;            /* reserved */
        aReqBHS[11] = 0;            /* reserved */

        cnISCSIReq = 0;
        aISCSIReq[cnISCSIReq].pcvSeg = aReqBHS;
        aISCSIReq[cnISCSIReq].cbSeg = sizeof(aReqBHS);
        cnISCSIReq++;

        aISCSIReq[cnISCSIReq].pcvSeg = bBuf;
        aISCSIReq[cnISCSIReq].cbSeg = cbBuf;
        cnISCSIReq++;

        rc = iscsiSendPDU(pImage, aISCSIReq, cnISCSIReq, ISCSIPDU_NO_REATTACH);
        if (RT_SUCCESS(rc))
        {
            ISCSIOPCODE cmd;
            ISCSILOGINSTATUSCLASS loginStatusClass;

            cnISCSIRes = 0;
            aISCSIRes[cnISCSIRes].pvSeg = aResBHS;
            aISCSIRes[cnISCSIRes].cbSeg = sizeof(aResBHS);
            cnISCSIRes++;
            aISCSIRes[cnISCSIRes].pvSeg = bBuf;
            aISCSIRes[cnISCSIRes].cbSeg = sizeof(bBuf);
            cnISCSIRes++;

            rc = iscsiRecvPDU(pImage, itt, aISCSIRes, cnISCSIRes, ISCSIPDU_NO_REATTACH);
            if (RT_FAILURE(rc))
            {
                /*
                 * We lost connection to the target while receiving the answer,
                 * start from the beginning.
                 */
                if (rc == VERR_BROKEN_PIPE || rc == VERR_NET_CONNECTION_REFUSED)
                    rc = VERR_TRY_AGAIN;
                break;
            }

            /** @todo collect partial login responses with Continue bit set. */
            Assert(aISCSIRes[0].pvSeg == aResBHS);
            Assert(aISCSIRes[0].cbSeg >= ISCSI_BHS_SIZE);
            Assert((RT_N2H_U32(aResBHS[0]) & ISCSI_CONTINUE_BIT) == 0);

            cmd = (ISCSIOPCODE)(RT_N2H_U32(aResBHS[0]) & ISCSIOP_MASK);
            if (cmd == ISCSIOP_LOGIN_RES)
            {
                if ((RT_N2H_U32(aResBHS[0]) & 0xff) != ISCSI_MY_VERSION)
                {
                    iscsiTransportClose(pImage);
                    rc = VERR_PARSE_ERROR;
                    break;  /* Give up immediately, as a RFC violation in version fields is very serious. */
                }

                loginStatusClass = (ISCSILOGINSTATUSCLASS)(RT_N2H_U32(aResBHS[9]) >> 24);
                switch (loginStatusClass)
                {
                    case ISCSI_LOGIN_STATUS_CLASS_SUCCESS:
                        uint32_t targetCSG;
                        uint32_t targetNSG;
                        bool targetTransit;

                        if (pImage->FirstRecvPDU)
                        {
                            pImage->FirstRecvPDU = false;
                            pImage->ExpStatSN = RT_N2H_U32(aResBHS[6]) + 1;
                        }

                        targetCSG = (RT_N2H_U32(aResBHS[0]) & ISCSI_CSG_MASK) >> ISCSI_CSG_SHIFT;
                        targetNSG = (RT_N2H_U32(aResBHS[0]) & ISCSI_NSG_MASK) >> ISCSI_NSG_SHIFT;
                        targetTransit = !!(RT_N2H_U32(aResBHS[0]) & ISCSI_TRANSIT_BIT);

                        /* Handle all cases with a single switch statement. */
                        switch (csg << 8 | substate)
                        {
                            case 0x0000:    /* security negotiation, step 0: receive final authentication. */
                                rc = iscsiUpdateParameters(pImage, bBuf, aISCSIRes[1].cbSeg);
                                if (RT_FAILURE(rc))
                                    break;

                                const char *pcszAuthMethod;

                                rc = iscsiTextGetKeyValue(bBuf, aISCSIRes[1].cbSeg, "AuthMethod", &pcszAuthMethod);
                                if (RT_FAILURE(rc))
                                {
                                    rc = VERR_PARSE_ERROR;
                                    break;
                                }
                                if (strcmp(pcszAuthMethod, "None") == 0)
                                {
                                    /* Authentication offered, but none required.  Skip to operational parameters. */
                                    csg = 1;
                                    nsg = 1;
                                    transit = true;
                                    substate = 0;
                                    break;
                                }
                                else if (strcmp(pcszAuthMethod, "CHAP") == 0 && targetNSG == 0 && !targetTransit)
                                {
                                    /* CHAP authentication required, continue with next substate. */
                                    substate++;
                                    break;
                                }

                                /* Unknown auth method or login response PDU headers incorrect. */
                                rc = VERR_PARSE_ERROR;
                                break;
                            case 0x0001:    /* security negotiation, step 1: receive final CHAP variant and challenge. */
                            {
                                rc = iscsiUpdateParameters(pImage, bBuf, aISCSIRes[1].cbSeg);
                                if (RT_FAILURE(rc))
                                    break;

                                const char *pcszChapAuthMethod;
                                const char *pcszChapIdxTarget;
                                const char *pcszChapChallengeStr;

                                rc = iscsiTextGetKeyValue(bBuf, aISCSIRes[1].cbSeg, "CHAP_A", &pcszChapAuthMethod);
                                if (RT_FAILURE(rc))
                                {
                                    rc = VERR_PARSE_ERROR;
                                    break;
                                }
                                if (strcmp(pcszChapAuthMethod, "5") != 0)
                                {
                                    rc = VERR_PARSE_ERROR;
                                    break;
                                }
                                rc = iscsiTextGetKeyValue(bBuf, aISCSIRes[1].cbSeg, "CHAP_I", &pcszChapIdxTarget);
                                if (RT_FAILURE(rc))
                                {
                                    rc = VERR_PARSE_ERROR;
                                    break;
                                }
                                rc = RTStrToUInt8Ex(pcszChapIdxTarget, &pszNext, 0, &bChapIdx);
/** @todo r=bird: Unsafe use of pszNext on failure.  The code should probably
 *        use RTStrToUInt8Full and check for rc != VINF_SUCCESS. */
                                if (rc > VINF_SUCCESS || *pszNext != '\0')
                                {
                                    rc = VERR_PARSE_ERROR;
                                    break;
                                }
                                rc = iscsiTextGetKeyValue(bBuf, aISCSIRes[1].cbSeg, "CHAP_C", &pcszChapChallengeStr);
                                if (RT_FAILURE(rc))
                                {
                                    rc = VERR_PARSE_ERROR;
                                    break;
                                }
                                cbChallenge = sizeof(pbChallenge);
                                rc = iscsiStrToBinary(pcszChapChallengeStr, pbChallenge, &cbChallenge);
                                if (RT_FAILURE(rc))
                                    break;
                                substate++;
                                transit = true;
                                break;
                            }
                            case 0x0002:    /* security negotiation, step 2: check authentication success. */
                                rc = iscsiUpdateParameters(pImage, bBuf, aISCSIRes[1].cbSeg);
                                if (RT_FAILURE(rc))
                                    break;

                                if (targetCSG == 0 && targetNSG == 1 && targetTransit)
                                {
                                    /* Target wants to continue in login operational state, authentication success. */
                                    csg = 1;
                                    nsg = 3;
                                    substate = 0;
                                    break;
                                }
                                rc = VERR_PARSE_ERROR;
                                break;
                            case 0x0100:    /* login operational negotiation, step 0: check results. */
                                rc = iscsiUpdateParameters(pImage, bBuf, aISCSIRes[1].cbSeg);
                                if (RT_FAILURE(rc))
                                    break;

                                if (targetCSG == 1 && targetNSG == 3 && targetTransit)
                                {
                                    /* Target wants to continue in full feature phase, login finished. */
                                    csg = 3;
                                    nsg = 3;
                                    substate = 0;
                                    break;
                                }
                                else if (targetCSG == 1 && (targetNSG == 1 || !targetTransit))
                                {
                                    /* Target wants to negotiate certain parameters and
                                     * stay in login operational negotiation. */
                                    csg = 1;
                                    nsg = 3;
                                    substate = 0;
                                    break;
                                }
                                rc = VERR_PARSE_ERROR;
                                break;
                            case 0x0300:    /* full feature phase. */
                            default:
                                AssertMsgFailed(("recv: Undefined login state %d substate %d\n", csg, substate));
                                rc = VERR_PARSE_ERROR;
                                break;
                        }
                        break;
                    case ISCSI_LOGIN_STATUS_CLASS_REDIRECTION:
                        const char *pcszTargetRedir;

                        /* Target has moved to some other location, as indicated in the TargetAddress key. */
                        rc = iscsiTextGetKeyValue(bBuf, aISCSIRes[1].cbSeg, "TargetAddress", &pcszTargetRedir);
                        if (RT_FAILURE(rc))
                        {
                            rc = VERR_PARSE_ERROR;
                            break;
                        }
                        if (pImage->pszTargetAddress)
                            RTMemFree(pImage->pszTargetAddress);
                        {
                            size_t cb = strlen(pcszTargetRedir) + 1;
                            pImage->pszTargetAddress = (char *)RTMemAlloc(cb);
                            if (!pImage->pszTargetAddress)
                            {
                                rc = VERR_NO_MEMORY;
                                break;
                            }
                            memcpy(pImage->pszTargetAddress, pcszTargetRedir, cb);
                        }
                        rc = VINF_TRY_AGAIN;
                        break;
                    case ISCSI_LOGIN_STATUS_CLASS_INITIATOR_ERROR:
                    {
                        LogRel(("iSCSI: login to target failed with: %s\n",
                                iscsiGetLoginErrorDetail((RT_N2H_U32(aResBHS[9]) >> 16) & 0xff)));
                        iscsiTransportClose(pImage);
                        rc = VERR_IO_GEN_FAILURE;
                        break;
                    }
                    case ISCSI_LOGIN_STATUS_CLASS_TARGET_ERROR:
                        iscsiTransportClose(pImage);
                        rc = VINF_EOF;
                        break;
                    default:
                        rc = VERR_PARSE_ERROR;
                }

                if (RT_FAILURE(rc) || rc == VINF_TRY_AGAIN)
                    break;

                if (csg == 3)
                {
                    /*
                     * Finished login, continuing with Full Feature Phase.
                     */
                    rc = VINF_SUCCESS;
                    break;
                }
            }
            else
                AssertMsgFailed(("%s: ignoring unexpected PDU with first word = %#08x\n", __FUNCTION__, RT_N2H_U32(aResBHS[0])));
        }
        else
            break;
    } while (true);

    if (   RT_FAILURE(rc)
        && rc != VERR_TRY_AGAIN)
    {
        /*
         * Dump the last request and response of we are supposed to do so and there is a request
         * or response.
         */
        if (cnISCSIReq)
            iscsiDumpPacket(pImage, aISCSIReq, cnISCSIReq, VINF_SUCCESS, true /* fRequest */);

        if (cnISCSIRes)
            iscsiDumpPacket(pImage, (PISCSIREQ)aISCSIRes, cnISCSIRes, rc, false /* fRequest */);

        /*
         * Close connection to target.
         */
        iscsiTransportClose(pImage);
        pImage->state = ISCSISTATE_FREE;
    }
    else if (rc == VINF_SUCCESS)
        pImage->state = ISCSISTATE_NORMAL;

    return rc;
}

/**
 * Attach to an iSCSI target. Performs all operations necessary to enter
 * Full Feature Phase.
 *
 * @returns VBox status code.
 * @param   pvUser      The iSCSI connection state to be used as opaque user data.
 */
static DECLCALLBACK(int) iscsiAttach(void *pvUser)
{
    int rc = VINF_SUCCESS;
    unsigned cRetries = 5;
    PISCSIIMAGE pImage = (PISCSIIMAGE)pvUser;

    LogFlowFunc(("entering\n"));

    Assert(pImage->state == ISCSISTATE_FREE);

    /*
     * If there were too many logins without any successful I/O just fail
     * and assume the target is not working properly.
     */
    if (ASMAtomicReadU32(&pImage->cLoginsSinceIo) == 3)
        return VERR_BROKEN_PIPE;

    RTSemMutexRequest(pImage->Mutex, RT_INDEFINITE_WAIT);

    /* Make 100% sure the connection isn't reused for a new login. */
    iscsiTransportClose(pImage);

    /* Try to log in a few number of times. */
    while (cRetries > 0)
    {
        rc = iscsiLogin(pImage);
        if (rc == VINF_SUCCESS) /* Login succeeded, continue with full feature phase. */
            break;
        else if (rc == VERR_TRY_AGAIN) /* Lost connection during receive. */
            cRetries--;
        else if (RT_FAILURE(rc))
            break;
        else /* For redirects try again. */
            AssertMsg(rc == VINF_TRY_AGAIN, ("Unexpected status code %Rrc\n", rc));
    }

    if (RT_SUCCESS(rc))
        ASMAtomicIncU32(&pImage->cLoginsSinceIo);

    RTSemMutexRelease(pImage->Mutex);

    LogFlowFunc(("returning %Rrc\n", rc));
    LogRel(("iSCSI: login to target %s %s (%Rrc)\n", pImage->pszTargetName, RT_SUCCESS(rc) ? "successful" : "failed", rc));
    return rc;
}


/**
 * Detach from an iSCSI target.
 *
 * @returns VBox status code.
 * @param   pvUser      The iSCSI connection state to be used as opaque user data.
 */
static DECLCALLBACK(int) iscsiDetach(void *pvUser)
{
    int rc;
    uint32_t itt;
    uint32_t cnISCSIReq = 0;
    ISCSIREQ aISCSIReq[4];
    uint32_t aReqBHS[12];
    PISCSIIMAGE pImage = (PISCSIIMAGE)pvUser;

    LogFlowFunc(("entering\n"));

    RTSemMutexRequest(pImage->Mutex, RT_INDEFINITE_WAIT);

    if (pImage->state != ISCSISTATE_FREE && pImage->state != ISCSISTATE_IN_LOGOUT)
    {
        pImage->state = ISCSISTATE_IN_LOGOUT;

        /*
         * Send logout request to target.
         */
        itt = iscsiNewITT(pImage);
        aReqBHS[0] = RT_H2N_U32(ISCSI_FINAL_BIT | ISCSIOP_LOGOUT_REQ);  /* I=0,F=1,Reason=close session */
        aReqBHS[1] = RT_H2N_U32(0); /* TotalAHSLength=0,DataSementLength=0 */
        aReqBHS[2] = 0;             /* reserved */
        aReqBHS[3] = 0;             /* reserved */
        aReqBHS[4] = itt;
        aReqBHS[5] = 0;             /* reserved */
        aReqBHS[6] = RT_H2N_U32(pImage->CmdSN);
        aReqBHS[7] = RT_H2N_U32(pImage->ExpStatSN);
        aReqBHS[8] = 0;             /* reserved */
        aReqBHS[9] = 0;             /* reserved */
        aReqBHS[10] = 0;            /* reserved */
        aReqBHS[11] = 0;            /* reserved */
        pImage->CmdSN++;

        aISCSIReq[cnISCSIReq].pcvSeg = aReqBHS;
        aISCSIReq[cnISCSIReq].cbSeg = sizeof(aReqBHS);
        cnISCSIReq++;

        rc = iscsiSendPDU(pImage, aISCSIReq, cnISCSIReq, ISCSIPDU_NO_REATTACH);
        if (RT_SUCCESS(rc))
        {
            /*
             * Read logout response from target.
             */
            ISCSIRES aISCSIRes;
            uint32_t aResBHS[12];

            aISCSIRes.pvSeg = aResBHS;
            aISCSIRes.cbSeg = sizeof(aResBHS);
            rc = iscsiRecvPDU(pImage, itt, &aISCSIRes, 1, ISCSIPDU_NO_REATTACH);
            if (RT_SUCCESS(rc))
            {
                if (RT_N2H_U32(aResBHS[0]) != (ISCSI_FINAL_BIT | ISCSIOP_LOGOUT_RES))
                    AssertMsgFailed(("iSCSI Logout response invalid\n"));
            }
            else
                AssertMsgFailed(("iSCSI Logout response error, rc=%Rrc\n", rc));
        }
        else
            AssertMsgFailed(("Could not send iSCSI Logout request, rc=%Rrc\n", rc));
    }

    if (pImage->state != ISCSISTATE_FREE)
    {
        /*
         * Close connection to target.
         */
        rc = iscsiTransportClose(pImage);
        if (RT_FAILURE(rc))
            AssertMsgFailed(("Could not close connection to target, rc=%Rrc\n", rc));
    }

    pImage->state = ISCSISTATE_FREE;

    RTSemMutexRelease(pImage->Mutex);

    LogFlowFunc(("leaving\n"));
    LogRel(("iSCSI: logout to target %s\n", pImage->pszTargetName));
    return VINF_SUCCESS;
}


/**
 * Perform a command on an iSCSI target. Target must be already in
 * Full Feature Phase.
 *
 * @returns VBox status code.
 * @param   pImage      The iSCSI connection state to be used.
 * @param   pRequest    Command descriptor. Contains all information about
 *                      the command, its transfer directions and pointers
 *                      to the buffer(s) used for transferring data and
 *                      status information.
 */
static int iscsiCommand(PISCSIIMAGE pImage, PSCSIREQ pRequest)
{
    int rc;
    uint32_t itt;
    uint32_t cbData;
    uint32_t cnISCSIReq = 0;
    ISCSIREQ aISCSIReq[4];
    uint32_t aReqBHS[12];

    uint32_t *pDst = NULL;
    size_t cbBufLength;
    uint32_t aStatus[256]; /**< Plenty of buffer for status information. */
    bool final = false;


    LogFlowFunc(("entering, CmdSN=%d\n", pImage->CmdSN));

    Assert(pRequest->enmXfer != SCSIXFER_TO_FROM_TARGET);   /**< @todo not yet supported, would require AHS. */
    Assert(pRequest->cbI2TData <= 0xffffff);    /* larger transfers would require R2T support. */
    Assert(pRequest->cbCDB <= 16);      /* would cause buffer overrun below. */

    /* If not in normal state, then the transport connection was dropped. Try
     * to reestablish by logging in, the target might be responsive again. */
    if (pImage->state == ISCSISTATE_FREE)
        rc = iscsiAttach(pImage);

    /* If still not in normal state, then the underlying transport connection
     * cannot be established. Get out before bad things happen (and make
     * sure the caller suspends the VM again). */
    if (pImage->state == ISCSISTATE_NORMAL)
    {
        /*
         * Send SCSI command to target with all I2T data included.
         */
        cbData = 0;
        if (pRequest->enmXfer == SCSIXFER_FROM_TARGET)
            cbData = (uint32_t)pRequest->cbT2IData;
        else
            cbData = (uint32_t)pRequest->cbI2TData;

        RTSemMutexRequest(pImage->Mutex, RT_INDEFINITE_WAIT);

        itt = iscsiNewITT(pImage);
        memset(aReqBHS, 0, sizeof(aReqBHS));
        aReqBHS[0] = RT_H2N_U32(    ISCSI_FINAL_BIT | ISCSI_TASK_ATTR_SIMPLE | ISCSIOP_SCSI_CMD
                                |   (pRequest->enmXfer << 21)); /* I=0,F=1,Attr=Simple */
        aReqBHS[1] = RT_H2N_U32(0x00000000 | ((uint32_t)pRequest->cbI2TData & 0xffffff)); /* TotalAHSLength=0 */
        aReqBHS[2] = RT_H2N_U32(pImage->LUN >> 32);
        aReqBHS[3] = RT_H2N_U32(pImage->LUN & 0xffffffff);
        aReqBHS[4] = itt;
        aReqBHS[5] = RT_H2N_U32(cbData);
        aReqBHS[6] = RT_H2N_U32(pImage->CmdSN);
        aReqBHS[7] = RT_H2N_U32(pImage->ExpStatSN);
        memcpy(aReqBHS + 8, pRequest->abCDB, pRequest->cbCDB);
        pImage->CmdSN++;

        aISCSIReq[cnISCSIReq].pcvSeg = aReqBHS;
        aISCSIReq[cnISCSIReq].cbSeg = sizeof(aReqBHS);
        cnISCSIReq++;

        if (    pRequest->enmXfer == SCSIXFER_TO_TARGET
            ||  pRequest->enmXfer == SCSIXFER_TO_FROM_TARGET)
        {
            Assert(pRequest->cI2TSegs == 1);
            aISCSIReq[cnISCSIReq].pcvSeg = pRequest->paI2TSegs[0].pvSeg;
            aISCSIReq[cnISCSIReq].cbSeg = pRequest->paI2TSegs[0].cbSeg;  /* Padding done by transport. */
            cnISCSIReq++;
        }

        rc = iscsiSendPDU(pImage, aISCSIReq, cnISCSIReq, ISCSIPDU_DEFAULT);
        if (RT_SUCCESS(rc))
        {
            /* Place SCSI request in queue. */
            pImage->paCurrReq = aISCSIReq;
            pImage->cnCurrReq = cnISCSIReq;

            /*
             * Read SCSI response/data in PDUs from target.
             */
            if (    pRequest->enmXfer == SCSIXFER_FROM_TARGET
                ||  pRequest->enmXfer == SCSIXFER_TO_FROM_TARGET)
            {
                Assert(pRequest->cT2ISegs == 1);
                pDst = (uint32_t *)pRequest->paT2ISegs[0].pvSeg;
                cbBufLength = pRequest->paT2ISegs[0].cbSeg;
            }
            else
                cbBufLength = 0;

            do
            {
                uint32_t cnISCSIRes = 0;
                ISCSIRES aISCSIRes[4];
                uint32_t aResBHS[12];

                aISCSIRes[cnISCSIRes].pvSeg = aResBHS;
                aISCSIRes[cnISCSIRes].cbSeg = sizeof(aResBHS);
                cnISCSIRes++;
                if (cbBufLength != 0 &&
                    (   pRequest->enmXfer == SCSIXFER_FROM_TARGET
                     ||  pRequest->enmXfer == SCSIXFER_TO_FROM_TARGET))
                {
                    aISCSIRes[cnISCSIRes].pvSeg = pDst;
                    aISCSIRes[cnISCSIRes].cbSeg = cbBufLength;
                    cnISCSIRes++;
                }
                /* Always reserve space for the status - it's impossible to tell
                 * beforehand whether this will be the final PDU or not. */
                aISCSIRes[cnISCSIRes].pvSeg = aStatus;
                aISCSIRes[cnISCSIRes].cbSeg = sizeof(aStatus);
                cnISCSIRes++;

                rc = iscsiRecvPDU(pImage, itt, aISCSIRes, cnISCSIRes, ISCSIPDU_DEFAULT);
                if (RT_FAILURE(rc))
                    break;

                final = !!(RT_N2H_U32(aResBHS[0]) & ISCSI_FINAL_BIT);
                ISCSIOPCODE cmd = (ISCSIOPCODE)(RT_N2H_U32(aResBHS[0]) & ISCSIOP_MASK);
                if (cmd == ISCSIOP_SCSI_RES)
                {
                    /* This is the final PDU which delivers the status (and may be omitted if
                     * the last Data-In PDU included successful completion status). Note
                     * that ExpStatSN has been bumped already in iscsiRecvPDU. */
                    if (!final || ((RT_N2H_U32(aResBHS[0]) & 0x0000ff00) != 0) || (RT_N2H_U32(aResBHS[6]) != pImage->ExpStatSN - 1))
                    {
                        /* SCSI Response in the wrong place or with a (target) failure. */
                        rc = VERR_PARSE_ERROR;
                        break;
                    }
                    /* The following is a bit tricky, as in error situations we may
                     * get the status only instead of the result data plus optional
                     * status. Thus the status may have ended up partially in the
                     * data area. */
                    pRequest->status = RT_N2H_U32(aResBHS[0]) & 0x000000ff;
                    cbData = RT_N2H_U32(aResBHS[1]) & 0x00ffffff;
                    if (cbData >= 2)
                    {
                        uint32_t cbStat = RT_N2H_U32(((uint32_t *)aISCSIRes[1].pvSeg)[0]) >> 16;
                        if (cbStat + 2 > cbData)
                        {
                            rc = VERR_BUFFER_OVERFLOW;
                            break;
                        }
                        /* Truncate sense data if it doesn't fit into the buffer. */
                        pRequest->cbSense = RT_MIN(cbStat, pRequest->cbSense);
                        memcpy(pRequest->abSense,
                               ((const char *)aISCSIRes[1].pvSeg) + 2,
                               RT_MIN(aISCSIRes[1].cbSeg - 2, pRequest->cbSense));
                        if (   cnISCSIRes > 2 && aISCSIRes[2].cbSeg
                            && (ssize_t)pRequest->cbSense - aISCSIRes[1].cbSeg + 2 > 0)
                        {
                            memcpy((char *)pRequest->abSense + aISCSIRes[1].cbSeg - 2,
                                   aISCSIRes[2].pvSeg,
                                   pRequest->cbSense - aISCSIRes[1].cbSeg + 2);
                        }
                    }
                    else if (cbData == 1)
                    {
                        rc = VERR_PARSE_ERROR;
                        break;
                    }
                    else
                        pRequest->cbSense = 0;
                    break;
                }
                else if (cmd == ISCSIOP_SCSI_DATA_IN)
                {
                    /* A Data-In PDU carries some data that needs to be added to the received
                     * data in response to the command. There may be both partial and complete
                     * Data-In PDUs, so collect data until the status is included or the status
                     * is sent in a separate SCSI Result frame (see above). */
                    if (final && aISCSIRes[2].cbSeg != 0)
                    {
                        /* The received PDU is partially stored in the buffer for status.
                         * Must not happen under normal circumstances and is a target error. */
                        rc = VERR_BUFFER_OVERFLOW;
                        break;
                    }
                    uint32_t len = RT_N2H_U32(aResBHS[1]) & 0x00ffffff;
                    pDst = (uint32_t *)((char *)pDst + len);
                    cbBufLength -= len;
                    if (final && (RT_N2H_U32(aResBHS[0]) & ISCSI_STATUS_BIT) != 0)
                    {
                        pRequest->status = RT_N2H_U32(aResBHS[0]) & 0x000000ff;
                        pRequest->cbSense = 0;
                        break;
                    }
                }
                else
                {
                    rc = VERR_PARSE_ERROR;
                    break;
                }
            } while (true);

            /* Remove SCSI request from queue. */
            pImage->paCurrReq = NULL;
            pImage->cnCurrReq = 0;
        }

        if (rc == VERR_TIMEOUT)
        {
            /* Drop connection in case the target plays dead. Much better than
             * delaying the next requests until the timed out command actually
             * finishes. Also keep in mind that command shouldn't take longer than
             * about 30-40 seconds, or the guest will lose its patience. */
            iscsiTransportClose(pImage);
            pImage->state = ISCSISTATE_FREE;
            rc = VERR_BROKEN_PIPE;
        }
        RTSemMutexRelease(pImage->Mutex);
    }
    else
        rc = VERR_NET_CONNECTION_REFUSED;

    if (RT_SUCCESS(rc))
        ASMAtomicWriteU32(&pImage->cLoginsSinceIo, 0);
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


/**
 * Generate a new Initiator Task Tag.
 *
 * @returns Initiator Task Tag.
 * @param   pImage      The iSCSI connection state to be used.
 */
static uint32_t iscsiNewITT(PISCSIIMAGE pImage)
{
    uint32_t next_itt;

    next_itt = pImage->ITT++;
    if (pImage->ITT == ISCSI_TASK_TAG_RSVD)
        pImage->ITT = 0;
    return RT_H2N_U32(next_itt);
}


/**
 * Send an iSCSI request. The request can consist of several segments, which
 * are padded to 4 byte boundaries and concatenated.
 *
 * @returns VBOX status
 * @param   pImage      The iSCSI connection state to be used.
 * @param   paReq       Pointer to array of iSCSI request sections.
 * @param   cnReq       Number of valid iSCSI request sections in the array.
 * @param   uFlags      Flags controlling the exact send semantics.
 */
static int iscsiSendPDU(PISCSIIMAGE pImage, PISCSIREQ paReq, uint32_t cnReq,
                        uint32_t uFlags)
{
    int rc = VINF_SUCCESS;
    /** @todo return VERR_VD_ISCSI_INVALID_STATE in the appropriate situations,
     * needs cleaning up of timeout/disconnect handling a bit, as otherwise
     * too many incorrect errors are signalled. */
    Assert(cnReq >= 1);
    Assert(paReq[0].cbSeg >= ISCSI_BHS_SIZE);

    for (uint32_t i = 0; i < pImage->cISCSIRetries; i++)
    {
        rc = iscsiTransportWrite(pImage, paReq, cnReq);
        if (RT_SUCCESS(rc))
            break;
        if (   (uFlags & ISCSIPDU_NO_REATTACH)
            || (rc != VERR_BROKEN_PIPE && rc != VERR_NET_CONNECTION_REFUSED))
            break;
        /* No point in reestablishing the connection for a logout */
        if (pImage->state == ISCSISTATE_IN_LOGOUT)
            break;
        RTThreadSleep(500);
        if (pImage->state != ISCSISTATE_IN_LOGIN)
        {
            /* Attempt to re-login when a connection fails, but only when not
             * currently logging in. */
            rc = iscsiAttach(pImage);
            if (RT_FAILURE(rc))
                break;
        }
    }
    return rc;
}


/**
 * Wait for an iSCSI response with a matching Initiator Target Tag. The response is
 * split into several segments, as requested by the caller-provided buffer specification.
 * Remember that the response can be split into several PDUs by the sender, so make
 * sure that all parts are collected and processed appropriately by the caller.
 *
 * @returns VBOX status
 * @param   pImage      The iSCSI connection state to be used.
 * @param   itt         The initiator task tag.
 * @param   paRes       Pointer to array of iSCSI response sections.
 * @param   cnRes       Number of valid iSCSI response sections in the array.
 * @param   fRecvFlags  PDU receive flags.
 */
static int iscsiRecvPDU(PISCSIIMAGE pImage, uint32_t itt, PISCSIRES paRes, uint32_t cnRes,
                        uint32_t fRecvFlags)
{
    int rc = VINF_SUCCESS;
    ISCSIRES aResBuf;

    for (uint32_t i = 0; i < pImage->cISCSIRetries; i++)
    {
        aResBuf.pvSeg = pImage->pvRecvPDUBuf;
        aResBuf.cbSeg = pImage->cbRecvPDUBuf;
        rc = iscsiTransportRead(pImage, &aResBuf, 1);
        if (RT_FAILURE(rc))
        {
            if (rc == VERR_BROKEN_PIPE || rc == VERR_NET_CONNECTION_REFUSED)
            {
                /* No point in reestablishing the connection for a logout */
                if (pImage->state == ISCSISTATE_IN_LOGOUT)
                    break;
                /* Connection broken while waiting for a response - wait a while and
                 * try to restart by re-sending the original request (if any).
                 * This also handles the connection reestablishment (login etc.). */
                RTThreadSleep(500);
                if (   pImage->state != ISCSISTATE_IN_LOGIN
                    && !(fRecvFlags & ISCSIPDU_NO_REATTACH))
                {
                    /* Attempt to re-login when a connection fails, but only when not
                     * currently logging in. */
                    rc = iscsiAttach(pImage);
                    if (RT_FAILURE(rc))
                        break;

                    if (pImage->paCurrReq != NULL)
                    {
                        rc = iscsiSendPDU(pImage, pImage->paCurrReq, pImage->cnCurrReq, ISCSIPDU_DEFAULT);
                        if (RT_FAILURE(rc))
                            break;
                    }
                }
            }
            else
            {
                /* Signal other errors (VERR_BUFFER_OVERFLOW etc.) to the caller. */
                break;
            }
        }
        else
        {
            ISCSIOPCODE cmd;
            const uint32_t *pcvResSeg = (const uint32_t *)aResBuf.pvSeg;

            /* Check whether the received PDU is valid, and update the internal state of
             * the iSCSI connection/session. */
            rc = iscsiValidatePDU(&aResBuf, 1);
            if (RT_FAILURE(rc))
            {
                iscsiDumpPacket(pImage, (PISCSIREQ)&aResBuf, 1, rc, false /* fRequest */);
                continue;
            }
            cmd = (ISCSIOPCODE)(RT_N2H_U32(pcvResSeg[0]) & ISCSIOP_MASK);
            switch (cmd)
            {
                case ISCSIOP_SCSI_RES:
                case ISCSIOP_SCSI_TASKMGMT_RES:
                case ISCSIOP_SCSI_DATA_IN:
                case ISCSIOP_R2T:
                case ISCSIOP_ASYN_MSG:
                case ISCSIOP_TEXT_RES:
                case ISCSIOP_LOGIN_RES:
                case ISCSIOP_LOGOUT_RES:
                case ISCSIOP_REJECT:
                case ISCSIOP_NOP_IN:
                    if (serial_number_less(pImage->MaxCmdSN, RT_N2H_U32(pcvResSeg[8])))
                        pImage->MaxCmdSN = RT_N2H_U32(pcvResSeg[8]);
                    if (serial_number_less(pImage->ExpCmdSN, RT_N2H_U32(pcvResSeg[7])))
                        pImage->ExpCmdSN = RT_N2H_U32(pcvResSeg[7]);
                    break;
                default:
                    rc = VERR_PARSE_ERROR;
                    iscsiDumpPacket(pImage, (PISCSIREQ)&aResBuf, 1, rc, false /* fRequest */);
            }
            if (RT_FAILURE(rc))
                continue;
            if (    !pImage->FirstRecvPDU
                &&  (cmd != ISCSIOP_SCSI_DATA_IN || (RT_N2H_U32(pcvResSeg[0]) & ISCSI_STATUS_BIT))
                &&  (   cmd != ISCSIOP_LOGIN_RES
                     || (ISCSILOGINSTATUSCLASS)((RT_N2H_U32(pcvResSeg[9]) >> 24) == ISCSI_LOGIN_STATUS_CLASS_SUCCESS)))
            {
                if (pImage->ExpStatSN == RT_N2H_U32(pcvResSeg[6]))
                {
                    /* StatSN counter is not advanced on R2T and on a target SN update NOP-In. */
                    if (    (cmd != ISCSIOP_R2T)
                        &&  ((cmd != ISCSIOP_NOP_IN) || (RT_N2H_U32(pcvResSeg[4]) != ISCSI_TASK_TAG_RSVD)))
                        pImage->ExpStatSN++;
                }
                else
                {
                    rc = VERR_PARSE_ERROR;
                    iscsiDumpPacket(pImage, (PISCSIREQ)&aResBuf, 1, rc, false /* fRequest */);
                    continue;
                }
            }
            /* Finally check whether the received PDU matches what the caller wants. */
            if (   itt == pcvResSeg[4]
                && itt != ISCSI_TASK_TAG_RSVD)
            {
                /* Copy received PDU (one segment) to caller-provided buffers. */
                uint32_t j;
                size_t cbSeg;
                const uint8_t *pSrc;

                pSrc = (const uint8_t *)aResBuf.pvSeg;
                cbSeg = aResBuf.cbSeg;
                for (j = 0; j < cnRes; j++)
                {
                    if (cbSeg > paRes[j].cbSeg)
                    {
                        memcpy(paRes[j].pvSeg, pSrc, paRes[j].cbSeg);
                        pSrc += paRes[j].cbSeg;
                        cbSeg -= paRes[j].cbSeg;
                    }
                    else
                    {
                        memcpy(paRes[j].pvSeg, pSrc, cbSeg);
                        paRes[j].cbSeg = cbSeg;
                        cbSeg = 0;
                        break;
                    }
                }
                if (cbSeg != 0)
                {
                    rc = VERR_BUFFER_OVERFLOW;
                    break;
                }
                for (j++; j < cnRes; j++)
                    paRes[j].cbSeg = 0;
                break;
            }
            else if (   cmd == ISCSIOP_NOP_IN
                     && RT_N2H_U32(pcvResSeg[5]) != ISCSI_TASK_TAG_RSVD)
            {
                uint32_t cnISCSIReq;
                ISCSIREQ aISCSIReq[4];
                uint32_t aReqBHS[12];

                aReqBHS[0] = RT_H2N_U32(ISCSI_IMMEDIATE_DELIVERY_BIT | ISCSI_FINAL_BIT | ISCSIOP_NOP_OUT);
                aReqBHS[1] = RT_H2N_U32(0); /* TotalAHSLength=0,DataSementLength=0 */
                aReqBHS[2] = pcvResSeg[2];      /* copy LUN from NOP-In */
                aReqBHS[3] = pcvResSeg[3];      /* copy LUN from NOP-In */
                aReqBHS[4] = RT_H2N_U32(ISCSI_TASK_TAG_RSVD); /* ITT, reply */
                aReqBHS[5] = pcvResSeg[5];      /* copy TTT from NOP-In */
                aReqBHS[6] = RT_H2N_U32(pImage->CmdSN);
                aReqBHS[7] = RT_H2N_U32(pImage->ExpStatSN);
                aReqBHS[8] = 0;             /* reserved */
                aReqBHS[9] = 0;             /* reserved */
                aReqBHS[10] = 0;            /* reserved */
                aReqBHS[11] = 0;            /* reserved */

                cnISCSIReq = 0;
                aISCSIReq[cnISCSIReq].pcvSeg = aReqBHS;
                aISCSIReq[cnISCSIReq].cbSeg = sizeof(aReqBHS);
                cnISCSIReq++;

                iscsiSendPDU(pImage, aISCSIReq, cnISCSIReq, ISCSIPDU_NO_REATTACH);
                /* Break if the caller wanted to process the NOP-in only. */
                if (itt == ISCSI_TASK_TAG_RSVD)
                    break;
            }
        }
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}


/**
 * Reset the PDU buffer
 *
 * @param   pImage      The iSCSI connection state to be used.
 */
static void iscsiRecvPDUReset(PISCSIIMAGE pImage)
{
    pImage->cbRecvPDUResidual = ISCSI_BHS_SIZE;
    pImage->fRecvPDUBHS       = true;
    pImage->pbRecvPDUBufCur   = (uint8_t *)pImage->pvRecvPDUBuf;
}

static void iscsiPDUTxAdd(PISCSIIMAGE pImage, PISCSIPDUTX pIScsiPDUTx, bool fFront)
{
    if (!fFront)
    {
        /* Insert PDU at the tail of the list. */
        if (!pImage->pIScsiPDUTxHead)
            pImage->pIScsiPDUTxHead = pIScsiPDUTx;
        else
            pImage->pIScsiPDUTxTail->pNext = pIScsiPDUTx;
        pImage->pIScsiPDUTxTail = pIScsiPDUTx;
    }
    else
    {
        /* Insert PDU at the beginning of the list. */
        pIScsiPDUTx->pNext = pImage->pIScsiPDUTxHead;
        pImage->pIScsiPDUTxHead = pIScsiPDUTx;
        if (!pImage->pIScsiPDUTxTail)
            pImage->pIScsiPDUTxTail = pIScsiPDUTx;
    }
}

/**
 * Receives a PDU in a non blocking way.
 *
 * @returns VBOX status code.
 * @param   pImage      The iSCSI connection state to be used.
 */
static int iscsiRecvPDUAsync(PISCSIIMAGE pImage)
{
    size_t cbActuallyRead = 0;
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pImage=%#p\n", pImage));

    /* Check if we are in the middle of a PDU receive. */
    if (pImage->cbRecvPDUResidual == 0)
    {
        /*
         * We are receiving a new PDU, don't read more than the BHS initially
         * until we know the real size of the PDU.
         */
        iscsiRecvPDUReset(pImage);
        LogFlow(("Receiving new PDU\n"));
    }

    rc = pImage->pIfNet->pfnReadNB(pImage->Socket, pImage->pbRecvPDUBufCur,
                                                   pImage->cbRecvPDUResidual, &cbActuallyRead);
    if (RT_SUCCESS(rc) && cbActuallyRead == 0)
        rc = VERR_BROKEN_PIPE;

    if (RT_SUCCESS(rc))
    {
        LogFlow(("Received %zu bytes\n", cbActuallyRead));
        pImage->cbRecvPDUResidual -= cbActuallyRead;
        pImage->pbRecvPDUBufCur   += cbActuallyRead;

        /* Check if we received everything we wanted. */
        if (   !pImage->cbRecvPDUResidual
            && pImage->fRecvPDUBHS)
        {
            size_t cbAHSLength, cbDataLength;

            /* If we were reading the BHS first get the actual PDU size now. */
            uint32_t word1 = RT_N2H_U32(((uint32_t *)(pImage->pvRecvPDUBuf))[1]);
            cbAHSLength = (word1 & 0xff000000) >> 24;
            cbAHSLength = ((cbAHSLength - 1) | 3) + 1;      /* Add padding. */
            cbDataLength = word1 & 0x00ffffff;
            cbDataLength = ((cbDataLength - 1) | 3) + 1;    /* Add padding. */
            pImage->cbRecvPDUResidual = cbAHSLength + cbDataLength;
            pImage->fRecvPDUBHS = false; /* Start receiving the rest of the PDU. */
        }

        if (!pImage->cbRecvPDUResidual)
        {
            /* We received the complete PDU with or without any payload now. */
            LogFlow(("Received complete PDU\n"));
            ISCSIRES aResBuf;
            aResBuf.pvSeg = pImage->pvRecvPDUBuf;
            aResBuf.cbSeg = pImage->cbRecvPDUBuf;
            rc = iscsiRecvPDUProcess(pImage, &aResBuf, 1);
        }
    }
    else
        LogFlowFunc(("Reading from the socket returned with rc=%Rrc\n", rc));

    return rc;
}

static int iscsiSendPDUAsync(PISCSIIMAGE pImage)
{
    size_t cbSent = 0;
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pImage=%#p\n", pImage));

    do
    {
        /*
         * If there is no PDU active, get the first one from the list.
         * Check that we are allowed to transfer the PDU by comparing the
         * command sequence number and the maximum sequence number allowed by the target.
         */
        if (!pImage->pIScsiPDUTxCur)
        {
            if (   !pImage->pIScsiPDUTxHead
                || serial_number_greater(pImage->pIScsiPDUTxHead->CmdSN, pImage->MaxCmdSN))
                break;

            pImage->pIScsiPDUTxCur = pImage->pIScsiPDUTxHead;
            pImage->pIScsiPDUTxHead = pImage->pIScsiPDUTxCur->pNext;
            if (!pImage->pIScsiPDUTxHead)
                pImage->pIScsiPDUTxTail = NULL;
        }

        /* Send as much as we can. */
        rc = pImage->pIfNet->pfnSgWriteNB(pImage->Socket, &pImage->pIScsiPDUTxCur->SgBuf, &cbSent);
        LogFlow(("SgWriteNB returned rc=%Rrc cbSent=%zu\n", rc, cbSent));
        if (RT_SUCCESS(rc))
        {
            LogFlow(("Sent %zu bytes for PDU %#p\n", cbSent, pImage->pIScsiPDUTxCur));
            pImage->pIScsiPDUTxCur->cbSgLeft -= cbSent;
            RTSgBufAdvance(&pImage->pIScsiPDUTxCur->SgBuf, cbSent);
            if (!pImage->pIScsiPDUTxCur->cbSgLeft)
            {
                /* PDU completed, free it and place the command on the waiting for response list. */
                if (pImage->pIScsiPDUTxCur->pIScsiCmd)
                {
                    LogFlow(("Sent complete PDU, placing on waiting list\n"));
                    iscsiCmdInsert(pImage, pImage->pIScsiPDUTxCur->pIScsiCmd);
                }
                RTMemFree(pImage->pIScsiPDUTxCur);
                pImage->pIScsiPDUTxCur = NULL;
            }
        }
    } while (   RT_SUCCESS(rc)
             && !pImage->pIScsiPDUTxCur);

    if (rc == VERR_TRY_AGAIN)
        rc = VINF_SUCCESS;

    /* Add the write poll flag if we still have something to send, clear it otherwise. */
    if (pImage->pIScsiPDUTxCur)
        pImage->fPollEvents |= VD_INTERFACETCPNET_EVT_WRITE;
    else
        pImage->fPollEvents &= ~VD_INTERFACETCPNET_EVT_WRITE;

    LogFlowFunc(("rc=%Rrc pIScsiPDUTxCur=%#p\n", rc, pImage->pIScsiPDUTxCur));
    return rc;
}

/**
 * Process a received PDU.
 *
 * @return VBOX status code.
 * @param  pImage      The iSCSI connection state to be used.
 * @param  paRes       Pointer to the array of iSCSI response sections.
 * @param  cnRes       Number of valid iSCSI response sections in the array.
 */
static int iscsiRecvPDUProcess(PISCSIIMAGE pImage, PISCSIRES paRes, uint32_t cnRes)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pImage=%#p paRes=%#p cnRes=%u\n", pImage, paRes, cnRes));

    /* Validate the PDU first. */
    rc = iscsiValidatePDU(paRes, cnRes);
    if (RT_SUCCESS(rc))
    {
        ISCSIOPCODE cmd;
        const uint32_t *pcvResSeg = (const uint32_t *)paRes[0].pvSeg;

        Assert(paRes[0].cbSeg > 9 * sizeof(uint32_t));

        do
        {
            cmd = (ISCSIOPCODE)(RT_N2H_U32(pcvResSeg[0]) & ISCSIOP_MASK);
            switch (cmd)
            {
                case ISCSIOP_SCSI_RES:
                case ISCSIOP_SCSI_TASKMGMT_RES:
                case ISCSIOP_SCSI_DATA_IN:
                case ISCSIOP_R2T:
                case ISCSIOP_ASYN_MSG:
                case ISCSIOP_TEXT_RES:
                case ISCSIOP_LOGIN_RES:
                case ISCSIOP_LOGOUT_RES:
                case ISCSIOP_REJECT:
                case ISCSIOP_NOP_IN:
                    if (serial_number_less(pImage->MaxCmdSN, RT_N2H_U32(pcvResSeg[8])))
                        pImage->MaxCmdSN = RT_N2H_U32(pcvResSeg[8]);
                    if (serial_number_less(pImage->ExpCmdSN, RT_N2H_U32(pcvResSeg[7])))
                        pImage->ExpCmdSN = RT_N2H_U32(pcvResSeg[7]);
                    break;
                default:
                    rc = VERR_PARSE_ERROR;
                    iscsiDumpPacket(pImage, (PISCSIREQ)paRes, cnRes, rc, false /* fRequest */);
            }

            if (RT_FAILURE(rc))
                break;

            if (    !pImage->FirstRecvPDU
                &&  (cmd != ISCSIOP_SCSI_DATA_IN || (RT_N2H_U32(pcvResSeg[0]) & ISCSI_STATUS_BIT)))
            {
                if (pImage->ExpStatSN == RT_N2H_U32(pcvResSeg[6]))
                {
                    /* StatSN counter is not advanced on R2T and on a target SN update NOP-In. */
                    if (    (cmd != ISCSIOP_R2T)
                        &&  ((cmd != ISCSIOP_NOP_IN) || (RT_N2H_U32(pcvResSeg[4]) != ISCSI_TASK_TAG_RSVD)))
                        pImage->ExpStatSN++;
                }
                else
                {
                    rc = VERR_PARSE_ERROR;
                    iscsiDumpPacket(pImage, (PISCSIREQ)paRes, cnRes, rc, false /* fRequest */);
                    break;
                }
            }

            if (pcvResSeg[4] != ISCSI_TASK_TAG_RSVD)
            {
                /*
                 * This is a response from the target for a request from the initiator.
                 * Get the request and update its state.
                 */
                rc = iscsiRecvPDUUpdateRequest(pImage, paRes, cnRes);
                /* Try to send more PDUs now that we updated the MaxCmdSN field */
                if (   RT_SUCCESS(rc)
                    && !pImage->pIScsiPDUTxCur)
                    rc = iscsiSendPDUAsync(pImage);
            }
            else
            {
                /* This is a target initiated request (we handle only NOP-In request at the moment). */
                if (   cmd == ISCSIOP_NOP_IN
                    && RT_N2H_U32(pcvResSeg[5]) != ISCSI_TASK_TAG_RSVD)
                {
                    PISCSIPDUTX pIScsiPDUTx;
                    uint32_t cnISCSIReq;
                    uint32_t *paReqBHS;

                    LogFlowFunc(("Sending NOP-Out\n"));

                    /* Allocate a new PDU initialize it and put onto the waiting list. */
                    pIScsiPDUTx = (PISCSIPDUTX)RTMemAllocZ(sizeof(ISCSIPDUTX));
                    if (!pIScsiPDUTx)
                    {
                        rc = VERR_NO_MEMORY;
                        break;
                    }
                    paReqBHS = &pIScsiPDUTx->aBHS[0];
                    paReqBHS[0] = RT_H2N_U32(ISCSI_IMMEDIATE_DELIVERY_BIT | ISCSI_FINAL_BIT | ISCSIOP_NOP_OUT);
                    paReqBHS[1] = RT_H2N_U32(0); /* TotalAHSLength=0,DataSementLength=0 */
                    paReqBHS[2] = pcvResSeg[2];      /* copy LUN from NOP-In */
                    paReqBHS[3] = pcvResSeg[3];      /* copy LUN from NOP-In */
                    paReqBHS[4] = RT_H2N_U32(ISCSI_TASK_TAG_RSVD); /* ITT, reply */
                    paReqBHS[5] = pcvResSeg[5];      /* copy TTT from NOP-In */
                    paReqBHS[6] = RT_H2N_U32(pImage->CmdSN);
                    paReqBHS[7] = RT_H2N_U32(pImage->ExpStatSN);
                    paReqBHS[8] = 0;             /* reserved */
                    paReqBHS[9] = 0;             /* reserved */
                    paReqBHS[10] = 0;            /* reserved */
                    paReqBHS[11] = 0;            /* reserved */

                    cnISCSIReq = 0;
                    pIScsiPDUTx->aISCSIReq[cnISCSIReq].pvSeg = paReqBHS;
                    pIScsiPDUTx->aISCSIReq[cnISCSIReq].cbSeg = sizeof(pIScsiPDUTx->aBHS);
                    cnISCSIReq++;
                    pIScsiPDUTx->cbSgLeft = sizeof(pIScsiPDUTx->aBHS);
                    RTSgBufInit(&pIScsiPDUTx->SgBuf, pIScsiPDUTx->aISCSIReq, cnISCSIReq);

                    /*
                     * Link the PDU to the list.
                     * Insert at the front of the list to send the response as soon as possible
                     * to avoid frequent reconnects for a slow connection when there are many PDUs
                     * waiting.
                     */
                    iscsiPDUTxAdd(pImage, pIScsiPDUTx, true /* fFront */);

                    /* Start transfer of a PDU if there is no one active at the moment. */
                    if (!pImage->pIScsiPDUTxCur)
                        rc = iscsiSendPDUAsync(pImage);
                }
            }
        } while (0);
    }
    else
        iscsiDumpPacket(pImage, (PISCSIREQ)paRes, cnRes, rc, false /* fRequest */);

    return rc;
}

/**
 * Check the static (not dependent on the connection/session state) validity of an iSCSI response PDU.
 *
 * @returns VBOX status
 * @param   paRes       Pointer to array of iSCSI response sections.
 * @param   cnRes       Number of valid iSCSI response sections in the array.
 */
static int iscsiValidatePDU(PISCSIRES paRes, uint32_t cnRes)
{
    RT_NOREF1(cnRes);
    const uint32_t *pcrgResBHS;
    uint32_t hw0;
    Assert(cnRes >= 1);
    Assert(paRes[0].cbSeg >= ISCSI_BHS_SIZE);

    LogFlowFunc(("paRes=%#p cnRes=%u\n", paRes, cnRes));

    pcrgResBHS = (const uint32_t *)(paRes[0].pvSeg);
    hw0 = RT_N2H_U32(pcrgResBHS[0]);
    switch (hw0 & ISCSIOP_MASK)
    {
        case ISCSIOP_NOP_IN:
            /* NOP-In responses must not be split into several PDUs nor it may contain
             * ping data for target-initiated pings nor may both task tags be valid task tags. */
            if (    (hw0 & ISCSI_FINAL_BIT) == 0
                ||  (    RT_N2H_U32(pcrgResBHS[4]) == ISCSI_TASK_TAG_RSVD
                     &&  RT_N2H_U32(pcrgResBHS[1]) != 0)
                ||  (    RT_N2H_U32(pcrgResBHS[4]) != ISCSI_TASK_TAG_RSVD
                     &&  RT_N2H_U32(pcrgResBHS[5]) != ISCSI_TASK_TAG_RSVD))
                return VERR_PARSE_ERROR;
            break;
        case ISCSIOP_SCSI_RES:
            /* SCSI responses must not be split into several PDUs nor must the residual
             * bits be contradicting each other nor may the residual bits be set for PDUs
             * containing anything else but a completed command response. Underflow
             * is no reason for declaring a PDU as invalid, as the target may choose
             * to return less data than we assume to get. */
            if (    (hw0 & ISCSI_FINAL_BIT) == 0
                ||  ((hw0 & ISCSI_BI_READ_RESIDUAL_OVFL_BIT) && (hw0 & ISCSI_BI_READ_RESIDUAL_UNFL_BIT))
                ||  ((hw0 & ISCSI_RESIDUAL_OVFL_BIT) && (hw0 & ISCSI_RESIDUAL_UNFL_BIT))
                ||  (    ((hw0 & ISCSI_SCSI_RESPONSE_MASK) == 0)
                     &&  ((hw0 & ISCSI_SCSI_STATUS_MASK) == SCSI_STATUS_OK)
                     &&  (hw0 & (  ISCSI_BI_READ_RESIDUAL_OVFL_BIT | ISCSI_BI_READ_RESIDUAL_UNFL_BIT
                                 | ISCSI_RESIDUAL_OVFL_BIT))))
                return VERR_PARSE_ERROR;
            else
                LogFlowFunc(("good SCSI response, first word %#08x\n", RT_N2H_U32(pcrgResBHS[0])));
            break;
        case ISCSIOP_LOGIN_RES:
            /* Login responses must not contain contradicting transit and continue bits. */
            if ((hw0 & ISCSI_CONTINUE_BIT) && ((hw0 & ISCSI_TRANSIT_BIT) != 0))
                return VERR_PARSE_ERROR;
            break;
        case ISCSIOP_TEXT_RES:
            /* Text responses must not contain contradicting final and continue bits nor
             * may the final bit be set for PDUs containing a target transfer tag other than
             * the reserved transfer tag (and vice versa). */
            if (    (((hw0 & ISCSI_CONTINUE_BIT) && (hw0 & ISCSI_FINAL_BIT) != 0))
                ||  (((hw0 & ISCSI_FINAL_BIT) && (RT_N2H_U32(pcrgResBHS[5]) != ISCSI_TASK_TAG_RSVD)))
                ||  (((hw0 & ISCSI_FINAL_BIT) == 0) && (RT_N2H_U32(pcrgResBHS[5]) == ISCSI_TASK_TAG_RSVD)))
                return VERR_PARSE_ERROR;
            break;
        case ISCSIOP_SCSI_DATA_IN:
            /* SCSI Data-in responses must not contain contradicting residual bits when
             * status bit is set. */
            if ((hw0 & ISCSI_STATUS_BIT) && (hw0 & ISCSI_RESIDUAL_OVFL_BIT) && (hw0 & ISCSI_RESIDUAL_UNFL_BIT))
                return VERR_PARSE_ERROR;
            break;
        case ISCSIOP_LOGOUT_RES:
            /* Logout responses must not have the final bit unset and may not contain any
             * data or additional header segments. */
            if (    ((hw0 & ISCSI_FINAL_BIT) == 0)
                ||  (RT_N2H_U32(pcrgResBHS[1]) != 0))
                return VERR_PARSE_ERROR;
            break;
        case ISCSIOP_ASYN_MSG:
            /* Asynchronous Messages must not have the final bit unset and may not contain
             * an initiator task tag. */
            if (    ((hw0 & ISCSI_FINAL_BIT) == 0)
                ||  (RT_N2H_U32(pcrgResBHS[4]) != ISCSI_TASK_TAG_RSVD))
                return VERR_PARSE_ERROR;
            break;
        case ISCSIOP_SCSI_TASKMGMT_RES:
        case ISCSIOP_R2T:
        case ISCSIOP_REJECT:
        default:
            /* Do some logging, ignore PDU. */
            LogFlowFunc(("ignore unhandled PDU, first word %#08x\n", RT_N2H_U32(pcrgResBHS[0])));
            return VERR_PARSE_ERROR;
    }
    /* A target must not send PDUs with MaxCmdSN less than ExpCmdSN-1. */

    if (serial_number_less(RT_N2H_U32(pcrgResBHS[8]), RT_N2H_U32(pcrgResBHS[7])-1))
        return VERR_PARSE_ERROR;

    return VINF_SUCCESS;
}


/**
 * Prepares a PDU to transfer for the given command and adds it to the list.
 */
static int iscsiPDUTxPrepare(PISCSIIMAGE pImage, PISCSICMD pIScsiCmd)
{
    int rc = VINF_SUCCESS;
    uint32_t *paReqBHS;
    size_t cbData = 0;
    size_t cbSegs = 0;
    PSCSIREQ pScsiReq;
    PISCSIPDUTX pIScsiPDU = NULL;

    LogFlowFunc(("pImage=%#p pIScsiCmd=%#p\n", pImage, pIScsiCmd));

    Assert(pIScsiCmd->enmCmdType == ISCSICMDTYPE_REQ);

    pIScsiCmd->Itt = iscsiNewITT(pImage);
    pScsiReq = pIScsiCmd->CmdType.ScsiReq.pScsiReq;

    if (pScsiReq->cT2ISegs)
        RTSgBufInit(&pScsiReq->SgBufT2I, pScsiReq->paT2ISegs, pScsiReq->cT2ISegs);

    /*
     * Allocate twice as much entries as required for padding (worst case).
     * The additional segment is for the BHS.
     */
    size_t cI2TSegs = 2*(pScsiReq->cI2TSegs + 1);
    pIScsiPDU = (PISCSIPDUTX)RTMemAllocZ(RT_UOFFSETOF_DYN(ISCSIPDUTX, aISCSIReq[cI2TSegs]));
    if (!pIScsiPDU)
        return VERR_NO_MEMORY;

    pIScsiPDU->pIScsiCmd = pIScsiCmd;

    if (pScsiReq->enmXfer == SCSIXFER_FROM_TARGET)
        cbData = (uint32_t)pScsiReq->cbT2IData;
    else
        cbData = (uint32_t)pScsiReq->cbI2TData;

    paReqBHS = pIScsiPDU->aBHS;

    /* Setup the BHS. */
    paReqBHS[0] = RT_H2N_U32(  ISCSI_FINAL_BIT | ISCSI_TASK_ATTR_SIMPLE | ISCSIOP_SCSI_CMD
                             | (pScsiReq->enmXfer << 21)); /* I=0,F=1,Attr=Simple */
    paReqBHS[1] = RT_H2N_U32(0x00000000 | ((uint32_t)pScsiReq->cbI2TData & 0xffffff)); /* TotalAHSLength=0 */
    paReqBHS[2] = RT_H2N_U32(pImage->LUN >> 32);
    paReqBHS[3] = RT_H2N_U32(pImage->LUN & 0xffffffff);
    paReqBHS[4] = pIScsiCmd->Itt;
    paReqBHS[5] = RT_H2N_U32((uint32_t)cbData); Assert((uint32_t)cbData == cbData);
    paReqBHS[6] = RT_H2N_U32(pImage->CmdSN);
    paReqBHS[7] = RT_H2N_U32(pImage->ExpStatSN);
    memcpy(paReqBHS + 8, pScsiReq->abCDB, pScsiReq->cbCDB);

    pIScsiPDU->CmdSN = pImage->CmdSN;
    pImage->CmdSN++;

    /* Setup the S/G buffers. */
    uint32_t cnISCSIReq = 0;
    pIScsiPDU->aISCSIReq[cnISCSIReq].cbSeg = sizeof(pIScsiPDU->aBHS);
    pIScsiPDU->aISCSIReq[cnISCSIReq].pvSeg = pIScsiPDU->aBHS;
    cnISCSIReq++;
    cbSegs = sizeof(pIScsiPDU->aBHS);
    /* Padding is not necessary for the BHS. */

    if (pScsiReq->cbI2TData)
    {
        for (unsigned cSeg = 0; cSeg < pScsiReq->cI2TSegs; cSeg++)
        {
            Assert(cnISCSIReq < cI2TSegs);
            pIScsiPDU->aISCSIReq[cnISCSIReq].cbSeg = pScsiReq->paI2TSegs[cSeg].cbSeg;
            pIScsiPDU->aISCSIReq[cnISCSIReq].pvSeg = pScsiReq->paI2TSegs[cSeg].pvSeg;
            cbSegs += pScsiReq->paI2TSegs[cSeg].cbSeg;
            cnISCSIReq++;

            /* Add padding if necessary. */
            if (pScsiReq->paI2TSegs[cSeg].cbSeg & 3)
            {
                Assert(cnISCSIReq < cI2TSegs);
                pIScsiPDU->aISCSIReq[cnISCSIReq].pvSeg = &pImage->aPadding[0];
                pIScsiPDU->aISCSIReq[cnISCSIReq].cbSeg = 4 - (pScsiReq->paI2TSegs[cSeg].cbSeg & 3);
                cbSegs += pIScsiPDU->aISCSIReq[cnISCSIReq].cbSeg;
                cnISCSIReq++;
            }
        }
    }

    pIScsiPDU->cISCSIReq = cnISCSIReq;
    pIScsiPDU->cbSgLeft  = cbSegs;
    RTSgBufInit(&pIScsiPDU->SgBuf, pIScsiPDU->aISCSIReq, cnISCSIReq);

    /* Link the PDU to the list. */
    iscsiPDUTxAdd(pImage, pIScsiPDU, false /* fFront */);

    /* Start transfer of a PDU if there is no one active at the moment. */
    if (!pImage->pIScsiPDUTxCur)
        rc = iscsiSendPDUAsync(pImage);

    return rc;
}


/**
 * Updates the state of a request from the PDU we received.
 *
 * @return VBox status code.
 * @param   pImage      iSCSI connection state to use.
 * @param   paRes       Pointer to array of iSCSI response sections.
 * @param   cnRes       Number of valid iSCSI response sections in the array.
 */
static int iscsiRecvPDUUpdateRequest(PISCSIIMAGE pImage, PISCSIRES paRes, uint32_t cnRes)
{
    int rc = VINF_SUCCESS;
    PISCSICMD pIScsiCmd;
    uint32_t *paResBHS;

    LogFlowFunc(("pImage=%#p paRes=%#p cnRes=%u\n", pImage, paRes, cnRes));

    Assert(cnRes == 1);
    Assert(paRes[0].cbSeg >= ISCSI_BHS_SIZE);

    paResBHS = (uint32_t *)paRes[0].pvSeg;

    pIScsiCmd = iscsiCmdGetFromItt(pImage, paResBHS[4]);

    if (pIScsiCmd)
    {
        bool final = false;
        PSCSIREQ pScsiReq;

        LogFlow(("Found SCSI command %#p for Itt=%#u\n", pIScsiCmd, paResBHS[4]));

        Assert(pIScsiCmd->enmCmdType == ISCSICMDTYPE_REQ);
        pScsiReq = pIScsiCmd->CmdType.ScsiReq.pScsiReq;

        final = !!(RT_N2H_U32(paResBHS[0]) & ISCSI_FINAL_BIT);
        ISCSIOPCODE cmd = (ISCSIOPCODE)(RT_N2H_U32(paResBHS[0]) & ISCSIOP_MASK);
        if (cmd == ISCSIOP_SCSI_RES)
        {
            /* This is the final PDU which delivers the status (and may be omitted if
             * the last Data-In PDU included successful completion status). Note
             * that ExpStatSN has been bumped already in iscsiRecvPDU. */
            if (!final || ((RT_N2H_U32(paResBHS[0]) & 0x0000ff00) != 0) || (RT_N2H_U32(paResBHS[6]) != pImage->ExpStatSN - 1))
            {
                /* SCSI Response in the wrong place or with a (target) failure. */
                LogFlow(("Wrong ExpStatSN value in PDU\n"));
                rc = VERR_PARSE_ERROR;
            }
            else
            {
                pScsiReq->status = RT_N2H_U32(paResBHS[0]) & 0x000000ff;
                size_t cbData = RT_N2H_U32(paResBHS[1]) & 0x00ffffff;
                void *pvSense = (uint8_t *)paRes[0].pvSeg + ISCSI_BHS_SIZE;

                if (cbData >= 2)
                {
                    uint32_t cbStat = RT_N2H_U32(((uint32_t *)pvSense)[0]) >> 16;
                    if (cbStat + 2 > cbData)
                    {
                        rc = VERR_BUFFER_OVERFLOW;
                    }
                    else
                    {
                        /* Truncate sense data if it doesn't fit into the buffer. */
                        pScsiReq->cbSense = RT_MIN(cbStat, pScsiReq->cbSense);
                        memcpy(pScsiReq->abSense, (uint8_t *)pvSense + 2,
                               RT_MIN(paRes[0].cbSeg - ISCSI_BHS_SIZE - 2, pScsiReq->cbSense));
                    }
                }
                else if (cbData == 1)
                    rc = VERR_PARSE_ERROR;
                else
                    pScsiReq->cbSense = 0;
            }
            iscsiCmdComplete(pImage, pIScsiCmd, rc);
        }
        else if (cmd == ISCSIOP_SCSI_DATA_IN)
        {
            /* A Data-In PDU carries some data that needs to be added to the received
             * data in response to the command. There may be both partial and complete
             * Data-In PDUs, so collect data until the status is included or the status
             * is sent in a separate SCSI Result frame (see above). */
            size_t cbData = RT_N2H_U32(paResBHS[1]) & 0x00ffffff;
            void   *pvData = (uint8_t *)paRes[0].pvSeg + ISCSI_BHS_SIZE;

            if (final && cbData > pScsiReq->cbT2IData)
            {
                /* The received PDU is bigger than what we requested.
                 * Must not happen under normal circumstances and is a target error. */
                rc = VERR_BUFFER_OVERFLOW;
            }
            else
            {
                /* Copy data from the received PDU into the T2I segments. */
                size_t cbCopied = RTSgBufCopyFromBuf(&pScsiReq->SgBufT2I, pvData, cbData);
                Assert(cbCopied == cbData); NOREF(cbCopied);

                if (final && (RT_N2H_U32(paResBHS[0]) & ISCSI_STATUS_BIT) != 0)
                {
                    pScsiReq->status = RT_N2H_U32(paResBHS[0]) & 0x000000ff;
                    pScsiReq->cbSense = 0;
                    iscsiCmdComplete(pImage, pIScsiCmd, VINF_SUCCESS);
                }
            }
        }
        else
            rc = VERR_PARSE_ERROR;
    }

    /* Log any errors here but ignore the PDU. */
    if (RT_FAILURE(rc))
    {
        LogRel(("iSCSI: Received malformed PDU from target %s (rc=%Rrc), ignoring\n", pImage->pszTargetName, rc));
        iscsiDumpPacket(pImage, (PISCSIREQ)paRes, cnRes, rc, false /* fRequest */);
        rc = VINF_SUCCESS;
    }

    return rc;
}

/**
 * Appends a key-value pair to the buffer. Normal ASCII strings (cbValue == 0) and large binary values
 * of a given length (cbValue > 0) are directly supported. Other value types must be converted to ASCII
 * by the caller. Strings must be in UTF-8 encoding.
 *
 * @returns VBOX status
 * @param   pbBuf       Pointer to the key-value buffer.
 * @param   cbBuf       Length of the key-value buffer.
 * @param   pcbBufCurr  Currently used portion of the key-value buffer.
 * @param   pcszKey     Pointer to a string containing the key.
 * @param   pcszValue   Pointer to either a string containing the value or to a large binary value.
 * @param   cbValue     Length of the binary value if applicable.
 */
static int iscsiTextAddKeyValue(uint8_t *pbBuf, size_t cbBuf, size_t *pcbBufCurr, const char *pcszKey,
                                   const char *pcszValue, size_t cbValue)
{
    size_t cbBufTmp = *pcbBufCurr;
    size_t cbKey = strlen(pcszKey);
    size_t cbValueEnc;
    uint8_t *pbCurr;

    if (cbValue == 0)
        cbValueEnc = strlen(pcszValue);
    else
        cbValueEnc = cbValue * 2 + 2;   /* 2 hex bytes per byte, 2 bytes prefix */

    if (cbBuf < cbBufTmp + cbKey + 1 + cbValueEnc + 1)
    {
        /* Buffer would overflow, signal error. */
        return VERR_BUFFER_OVERFLOW;
    }

    /*
     * Append a key=value pair (zero terminated string) to the end of the buffer.
     */
    pbCurr = pbBuf + cbBufTmp;
    memcpy(pbCurr, pcszKey, cbKey);
    pbCurr += cbKey;
    *pbCurr++ = '=';
    if (cbValue == 0)
    {
        memcpy(pbCurr, pcszValue, cbValueEnc);
        pbCurr += cbValueEnc;
    }
    else
    {
        *pbCurr++ = '0';
        *pbCurr++ = 'x';
        for (uint32_t i = 0; i < cbValue; i++)
        {
            uint8_t b;
            b = pcszValue[i];
            *pbCurr++ = NUM_2_HEX(b >> 4);
            *pbCurr++ = NUM_2_HEX(b & 0xf);
        }
    }
    *pbCurr = '\0';
    *pcbBufCurr = cbBufTmp + cbKey + 1 + cbValueEnc + 1;

    return VINF_SUCCESS;
}


/**
 * Retrieve the value for a given key from the key=value buffer.
 *
 * @returns VBox status code.
 * @param   pbBuf       Buffer containing key=value pairs.
 * @param   cbBuf       Length of buffer with key=value pairs.
 * @param   pcszKey     Pointer to key for which to retrieve the value.
 * @param   ppcszValue  Pointer to value string pointer.
 */
static int iscsiTextGetKeyValue(const uint8_t *pbBuf, size_t cbBuf, const char *pcszKey, const char **ppcszValue)
{
    size_t cbKey = strlen(pcszKey);

    while (cbBuf != 0)
    {
        size_t cbKeyValNull = strlen((const char *)pbBuf) + 1;

        if (strncmp(pcszKey, (const char *)pbBuf, cbKey) == 0 && pbBuf[cbKey] == '=')
        {
            *ppcszValue = (const char *)(pbBuf + cbKey + 1);
            return VINF_SUCCESS;
        }
        pbBuf += cbKeyValNull;
        cbBuf -= cbKeyValNull;
    }
    return VERR_INVALID_NAME;
}


/**
 * Convert a long-binary value from a value string to the binary representation.
 *
 * @returns VBOX status
 * @param   pcszValue   Pointer to a string containing the textual value representation.
 * @param   pbValue     Pointer to the value buffer for the binary value.
 * @param   pcbValue    In: length of value buffer, out: actual length of binary value.
 */
static int iscsiStrToBinary(const char *pcszValue, uint8_t *pbValue, size_t *pcbValue)
{
    size_t cbValue = *pcbValue;
    char c1, c2, c3, c4;
    Assert(cbValue >= 1);

    if (strlen(pcszValue) < 3)
        return VERR_PARSE_ERROR;
    if (*pcszValue++ != '0')
        return VERR_PARSE_ERROR;
    switch (*pcszValue++)
    {
        case 'x':
        case 'X':
            if (strlen(pcszValue) & 1)
            {
                c1 = *pcszValue++;
                *pbValue++ = HEX_2_NUM(c1);
                cbValue--;
            }
            while (*pcszValue != '\0')
            {
                if (cbValue == 0)
                    return VERR_BUFFER_OVERFLOW;
                c1 = *pcszValue++;
                if ((c1 < '0' || c1 > '9') && (c1 < 'a' || c1 > 'f') && (c1 < 'A' || c1 > 'F'))
                    return VERR_PARSE_ERROR;
                c2 = *pcszValue++;
                if ((c2 < '0' || c2 > '9') && (c2 < 'a' || c2 > 'f') && (c2 < 'A' || c2 > 'F'))
                    return VERR_PARSE_ERROR;
                *pbValue++ = (HEX_2_NUM(c1) << 4) | HEX_2_NUM(c2);
                cbValue--;
            }
            *pcbValue -= cbValue;
            break;
        case 'b':
        case 'B':
            if ((strlen(pcszValue) & 3) != 0)
                return VERR_PARSE_ERROR;
            while (*pcszValue != '\0')
            {
                uint32_t temp;
                if (cbValue == 0)
                    return VERR_BUFFER_OVERFLOW;
                c1 = *pcszValue++;
                if ((c1 < 'A' || c1 > 'Z') && (c1 < 'a' || c1 >'z') && (c1 < '0' || c1 > '9') && (c1 != '+') && (c1 != '/'))
                    return VERR_PARSE_ERROR;
                c2 = *pcszValue++;
                if ((c2 < 'A' || c2 > 'Z') && (c2 < 'a' || c2 >'z') && (c2 < '0' || c2 > '9') && (c2 != '+') && (c2 != '/'))
                    return VERR_PARSE_ERROR;
                c3 = *pcszValue++;
                if ((c3 < 'A' || c3 > 'Z') && (c3 < 'a' || c3 >'z') && (c3 < '0' || c3 > '9') && (c3 != '+') && (c3 != '/') && (c3 != '='))
                    return VERR_PARSE_ERROR;
                c4 = *pcszValue++;
                if (    (c3 == '=' && c4 != '=')
                    ||  ((c4 < 'A' || c4 > 'Z') && (c4 < 'a' || c4 >'z') && (c4 < '0' || c4 > '9') && (c4 != '+') && (c4 != '/') && (c4 != '=')))
                    return VERR_PARSE_ERROR;
                temp = (B64_2_NUM(c1) << 18) | (B64_2_NUM(c2) << 12);
                if (c3 == '=') {
                    if (*pcszValue != '\0')
                        return VERR_PARSE_ERROR;
                    *pbValue++ = temp >> 16;
                    cbValue--;
                } else {
                    temp |= B64_2_NUM(c3) << 6;
                    if (c4 == '=') {
                        if (*pcszValue != '\0')
                            return VERR_PARSE_ERROR;
                        if (cbValue < 2)
                            return VERR_BUFFER_OVERFLOW;
                        *pbValue++ = temp >> 16;
                        *pbValue++ = (temp >> 8) & 0xff;
                        cbValue -= 2;
                    }
                    else
                    {
                        temp |= B64_2_NUM(c4);
                        if (cbValue < 3)
                            return VERR_BUFFER_OVERFLOW;
                        *pbValue++ = temp >> 16;
                        *pbValue++ = (temp >> 8) & 0xff;
                        *pbValue++ = temp & 0xff;
                        cbValue -= 3;
                    }
                }
            }
            *pcbValue -= cbValue;
            break;
        default:
            return VERR_PARSE_ERROR;
    }
    return VINF_SUCCESS;
}


/**
 * Retrieve the relevant parameter values and update the initiator state.
 *
 * @returns VBox status code.
 * @param   pImage      Current iSCSI initiator state.
 * @param   pbBuf       Buffer containing key=value pairs.
 * @param   cbBuf       Length of buffer with key=value pairs.
 */
static int iscsiUpdateParameters(PISCSIIMAGE pImage, const uint8_t *pbBuf, size_t cbBuf)
{
    int rc;
    const char *pcszMaxRecvDataSegmentLength = NULL;
    const char *pcszMaxBurstLength = NULL;
    const char *pcszFirstBurstLength = NULL;
    rc = iscsiTextGetKeyValue(pbBuf, cbBuf, "MaxRecvDataSegmentLength", &pcszMaxRecvDataSegmentLength);
    if (rc == VERR_INVALID_NAME)
        rc = VINF_SUCCESS;
    if (RT_FAILURE(rc))
        return VERR_PARSE_ERROR;
    rc = iscsiTextGetKeyValue(pbBuf, cbBuf, "MaxBurstLength", &pcszMaxBurstLength);
    if (rc == VERR_INVALID_NAME)
        rc = VINF_SUCCESS;
    if (RT_FAILURE(rc))
        return VERR_PARSE_ERROR;
    rc = iscsiTextGetKeyValue(pbBuf, cbBuf, "FirstBurstLength", &pcszFirstBurstLength);
    if (rc == VERR_INVALID_NAME)
        rc = VINF_SUCCESS;
    if (RT_FAILURE(rc))
        return VERR_PARSE_ERROR;
    if (pcszMaxRecvDataSegmentLength)
    {
        uint32_t cb = pImage->cbSendDataLength;
        rc = RTStrToUInt32Full(pcszMaxRecvDataSegmentLength, 0, &cb);
        AssertRC(rc);
        pImage->cbSendDataLength = RT_MIN(pImage->cbSendDataLength, cb);
    }
    if (pcszMaxBurstLength)
    {
        uint32_t cb = pImage->cbSendDataLength;
        rc = RTStrToUInt32Full(pcszMaxBurstLength, 0, &cb);
        AssertRC(rc);
        pImage->cbSendDataLength = RT_MIN(pImage->cbSendDataLength, cb);
    }
    if (pcszFirstBurstLength)
    {
        uint32_t cb = pImage->cbSendDataLength;
        rc = RTStrToUInt32Full(pcszFirstBurstLength, 0, &cb);
        AssertRC(rc);
        pImage->cbSendDataLength = RT_MIN(pImage->cbSendDataLength, cb);
    }
    return VINF_SUCCESS;
}


static bool serial_number_less(uint32_t s1, uint32_t s2)
{
    return (s1 < s2 && s2 - s1 < 0x80000000) || (s1 > s2 && s1 - s2 > 0x80000000);
}

static bool serial_number_greater(uint32_t s1, uint32_t s2)
{
    return (s1 < s2 && s2 - s1 > 0x80000000) || (s1 > s2 && s1 - s2 < 0x80000000);
}


#ifdef IMPLEMENT_TARGET_AUTH
static void chap_md5_generate_challenge(uint8_t *pbChallenge, size_t *pcbChallenge)
{
    uint8_t cbChallenge;

    cbChallenge = RTrand_U8(CHAP_MD5_CHALLENGE_MIN, CHAP_MD5_CHALLENGE_MAX);
    RTrand_bytes(pbChallenge, cbChallenge);
    *pcbChallenge = cbChallenge;
}
#endif


static void chap_md5_compute_response(uint8_t *pbResponse, uint8_t id, const uint8_t *pbChallenge, size_t cbChallenge,
                                      const uint8_t *pbSecret, size_t cbSecret)
{
    RTMD5CONTEXT ctx;
    uint8_t bId;

    bId = id;
    RTMd5Init(&ctx);
    RTMd5Update(&ctx, &bId, 1);
    RTMd5Update(&ctx, pbSecret, cbSecret);
    RTMd5Update(&ctx, pbChallenge, cbChallenge);
    RTMd5Final(pbResponse, &ctx);
}

/**
 * Internal. - Wrapper around the extended select callback of the net interface.
 */
DECLINLINE(int) iscsiIoThreadWait(PISCSIIMAGE pImage, RTMSINTERVAL cMillies, uint32_t fEvents, uint32_t *pfEvents)
{
    return pImage->pIfNet->pfnSelectOneEx(pImage->Socket, fEvents, pfEvents, cMillies);
}

/**
 * Internal. - Pokes a thread waiting for I/O.
 */
DECLINLINE(int) iscsiIoThreadPoke(PISCSIIMAGE pImage)
{
    return pImage->pIfNet->pfnPoke(pImage->Socket);
}

/**
 * Internal. - Get the next request from the queue.
 */
DECLINLINE(PISCSICMD) iscsiCmdGet(PISCSIIMAGE pImage)
{
    int rc;
    PISCSICMD pIScsiCmd = NULL;

    rc = RTSemMutexRequest(pImage->MutexReqQueue, RT_INDEFINITE_WAIT);
    AssertRC(rc);

    pIScsiCmd = pImage->pScsiReqQueue;
    if (pIScsiCmd)
    {
        pImage->pScsiReqQueue = pIScsiCmd->pNext;
        pIScsiCmd->pNext = NULL;
    }

    rc = RTSemMutexRelease(pImage->MutexReqQueue);
    AssertRC(rc);

    return pIScsiCmd;
}


/**
 * Internal. - Adds the given command to the queue.
 */
DECLINLINE(int) iscsiCmdPut(PISCSIIMAGE pImage, PISCSICMD pIScsiCmd)
{
    int rc = RTSemMutexRequest(pImage->MutexReqQueue, RT_INDEFINITE_WAIT);
    AssertRC(rc);

    pIScsiCmd->pNext = pImage->pScsiReqQueue;
    pImage->pScsiReqQueue = pIScsiCmd;

    rc = RTSemMutexRelease(pImage->MutexReqQueue);
    AssertRC(rc);

    iscsiIoThreadPoke(pImage);

    return rc;
}

/**
 * Internal. - Completes the request with the appropriate action.
 *             Synchronous requests are completed with waking up the thread
 *             and asynchronous ones by continuing the associated I/O context.
 */
static void iscsiCmdComplete(PISCSIIMAGE pImage, PISCSICMD pIScsiCmd, int rcCmd)
{
    LogFlowFunc(("pImage=%#p pIScsiCmd=%#p rcCmd=%Rrc\n", pImage, pIScsiCmd, rcCmd));

    /* Remove from the table first. */
    iscsiCmdRemove(pImage, pIScsiCmd->Itt);

    /* Call completion callback. */
    pIScsiCmd->pfnComplete(pImage, rcCmd, pIScsiCmd->pvUser);

    /* Free command structure. */
#ifdef DEBUG
    memset(pIScsiCmd, 0xff, sizeof(ISCSICMD));
#endif
    RTMemFree(pIScsiCmd);
}

/**
 * Clears all RX/TX PDU states and returns the command for the current
 * pending TX PDU if existing.
 *
 * @returns Pointer to the iSCSI command for the current PDU transmitted or NULL
 *          if none is waiting.
 * @param   pImage    iSCSI connection state.
 */
static PISCSICMD iscsiPDURxTxClear(PISCSIIMAGE pImage)
{
    PISCSICMD pIScsiCmdHead = NULL;
    PISCSIPDUTX pIScsiPDUTx = NULL;

    /* Reset PDU we are receiving. */
    iscsiRecvPDUReset(pImage);

    /*
     * Abort all PDUs we are about to transmit,
     * the command need a new Itt if the relogin is successful.
     */
    while (pImage->pIScsiPDUTxHead)
    {
        pIScsiPDUTx = pImage->pIScsiPDUTxHead;
        pImage->pIScsiPDUTxHead = pIScsiPDUTx->pNext;

        PISCSICMD pIScsiCmd = pIScsiPDUTx->pIScsiCmd;
        if (pIScsiCmd)
        {
            /* Place on command list. */
            pIScsiCmd->pNext = pIScsiCmdHead;
            pIScsiCmdHead = pIScsiCmd;
        }
        RTMemFree(pIScsiPDUTx);
    }

    /* Clear the tail pointer (safety precaution). */
    pImage->pIScsiPDUTxTail = NULL;

    /* Clear the current PDU too. */
    if (pImage->pIScsiPDUTxCur)
    {
        pIScsiPDUTx = pImage->pIScsiPDUTxCur;

        pImage->pIScsiPDUTxCur = NULL;
        PISCSICMD pIScsiCmd = pIScsiPDUTx->pIScsiCmd;
        if (pIScsiCmd)
        {
            pIScsiCmd->pNext = pIScsiCmdHead;
            pIScsiCmdHead = pIScsiCmd;
        }
        RTMemFree(pIScsiPDUTx);
    }

    return pIScsiCmdHead;
}

/**
 * Rests the iSCSI connection state and returns a list of iSCSI commands pending
 * when this was called.
 *
 * @returns Pointer to the head of the pending iSCSI command list.
 * @param   pImage    iSCSI connection state.
 */
static PISCSICMD iscsiReset(PISCSIIMAGE pImage)
{
    PISCSICMD pIScsiCmdHead = NULL;
    PISCSICMD pIScsiCmdCur = NULL;

    /* Clear all in flight PDUs. */
    pIScsiCmdHead = iscsiPDURxTxClear(pImage);

    /*
     * Get all commands which are waiting for a response
     * They need to be resend too after a successful reconnect.
     */
    PISCSICMD pIScsiCmd = iscsiCmdRemoveAll(pImage);
    if (pIScsiCmd)
    {
        pIScsiCmdCur = pIScsiCmd;
        while (pIScsiCmdCur->pNext)
            pIScsiCmdCur = pIScsiCmdCur->pNext;

        /*
         * Place them in front of the list because they are the oldest requests
         * and need to be processed first to minimize the risk to time out.
         */
        pIScsiCmdCur->pNext = pIScsiCmdHead;
        pIScsiCmdHead = pIScsiCmd;
    }

    return pIScsiCmdHead;
}

/**
 * Reattaches the to the target after an error aborting
 * pending commands and resending them.
 *
 * @param    pImage    iSCSI connection state.
 */
static void iscsiReattach(PISCSIIMAGE pImage)
{
    /* Close connection. */
    iscsiTransportClose(pImage);
    pImage->state = ISCSISTATE_FREE;

    /* Reset the state and get the currently pending commands. */
    PISCSICMD pIScsiCmdHead = iscsiReset(pImage);

    /* Try to attach. */
    int rc = iscsiAttach(pImage);
    if (RT_SUCCESS(rc))
    {
        /* Phew, we have a connection again.
         * Prepare new PDUs for the aborted commands.
         */
        while (pIScsiCmdHead)
        {
            PISCSICMD pIScsiCmd = pIScsiCmdHead;
            pIScsiCmdHead = pIScsiCmdHead->pNext;

            pIScsiCmd->pNext = NULL;

            rc = iscsiPDUTxPrepare(pImage, pIScsiCmd);
            if (RT_FAILURE(rc))
                break;
        }

        if (RT_FAILURE(rc))
        {
            /* Another error, just give up and report an error. */
            PISCSICMD pIScsiCmd = iscsiReset(pImage);

            /* Concatenate both lists together so we can abort all requests below. */
            if (pIScsiCmd)
            {
                PISCSICMD pIScsiCmdCur = pIScsiCmd;
                while (pIScsiCmdCur->pNext)
                    pIScsiCmdCur = pIScsiCmdCur->pNext;

                pIScsiCmdCur->pNext = pIScsiCmdHead;
                pIScsiCmdHead = pIScsiCmd;
            }
        }
    }

    if (RT_FAILURE(rc))
    {
        /*
         * Still no luck, complete commands with error so the caller
         * has a chance to inform the user and maybe resend the command.
         */
        while (pIScsiCmdHead)
        {
            PISCSICMD pIScsiCmd = pIScsiCmdHead;
            pIScsiCmdHead = pIScsiCmdHead->pNext;

            iscsiCmdComplete(pImage, pIScsiCmd, VERR_BROKEN_PIPE);
        }
    }
}

/**
 * Internal. Main iSCSI I/O worker.
 */
static DECLCALLBACK(int) iscsiIoThreadWorker(RTTHREAD hThreadSelf, void *pvUser)
{
    RT_NOREF1(hThreadSelf);
    PISCSIIMAGE pImage = (PISCSIIMAGE)pvUser;

    /* Initialize the initial event mask. */
    pImage->fPollEvents = VD_INTERFACETCPNET_EVT_READ | VD_INTERFACETCPNET_EVT_ERROR;

    while (pImage->fRunning)
    {
        uint32_t fEvents;
        int rc;

        fEvents = 0;

        /* Wait for work or for data from the target. */
        RTMSINTERVAL msWait;

        if (pImage->cCmdsWaiting)
        {
            pImage->fPollEvents &= ~VD_INTERFACETCPNET_HINT_INTERRUPT;
            msWait = pImage->uReadTimeout;
        }
        else
        {
            pImage->fPollEvents |= VD_INTERFACETCPNET_HINT_INTERRUPT;
            msWait = RT_INDEFINITE_WAIT;
        }

        LogFlow(("Waiting for events fPollEvents=%#x\n", pImage->fPollEvents));
        rc = iscsiIoThreadWait(pImage, msWait, pImage->fPollEvents, &fEvents);
        if (rc == VERR_INTERRUPTED)
        {
            /* Check the queue. */
            PISCSICMD pIScsiCmd = iscsiCmdGet(pImage);

            while (pIScsiCmd)
            {
                switch (pIScsiCmd->enmCmdType)
                {
                    case ISCSICMDTYPE_REQ:
                    {
                        if (   !iscsiIsClientConnected(pImage)
                            && pImage->fTryReconnect)
                        {
                            pImage->fTryReconnect = false;
                            iscsiReattach(pImage);
                        }

                        /* If there is no connection complete the command with an error. */
                        if (RT_LIKELY(iscsiIsClientConnected(pImage)))
                        {
                            rc = iscsiPDUTxPrepare(pImage, pIScsiCmd);
                            if (RT_FAILURE(rc))
                                iscsiReattach(pImage);
                        }
                        else
                            iscsiCmdComplete(pImage, pIScsiCmd, VERR_NET_CONNECTION_REFUSED);
                        break;
                    }
                    case ISCSICMDTYPE_EXEC:
                    {
                        rc = pIScsiCmd->CmdType.Exec.pfnExec(pIScsiCmd->CmdType.Exec.pvUser);
                        iscsiCmdComplete(pImage, pIScsiCmd, rc);
                        break;
                    }
                    default:
                        AssertMsgFailed(("Invalid command type %d\n", pIScsiCmd->enmCmdType));
                }

                pIScsiCmd = iscsiCmdGet(pImage);
            }
        }
        else if (rc == VERR_TIMEOUT && pImage->cCmdsWaiting)
        {
            /*
             * We are waiting for a response from the target but
             * it didn't answered yet.
             * We assume the connection is broken and try to reconnect.
             */
            LogFlow(("Timed out while waiting for an answer from the target, reconnecting\n"));
            iscsiReattach(pImage);
        }
        else if (RT_SUCCESS(rc) || rc == VERR_TIMEOUT)
        {
            Assert(pImage->state == ISCSISTATE_NORMAL);
            LogFlow(("Got socket events %#x\n", fEvents));

            if (fEvents & VD_INTERFACETCPNET_EVT_READ)
            {
                /* Continue or start a new PDU receive task */
                LogFlow(("There is data on the socket\n"));
                rc = iscsiRecvPDUAsync(pImage);
                if (rc == VERR_BROKEN_PIPE)
                    iscsiReattach(pImage);
                else if (RT_FAILURE(rc))
                    iscsiLogRel(pImage, "iSCSI: Handling incoming request failed %Rrc\n", rc);
            }

            if (fEvents & VD_INTERFACETCPNET_EVT_WRITE)
            {
                LogFlow(("The socket is writable\n"));
                rc = iscsiSendPDUAsync(pImage);
                if (RT_FAILURE(rc))
                {
                    /*
                     * Something unexpected happened, log the error and try to reset everything
                     * by reattaching to the target.
                     */
                    iscsiLogRel(pImage, "iSCSI: Sending PDU failed %Rrc\n", rc);
                    iscsiReattach(pImage);
                }
            }

            if (fEvents & VD_INTERFACETCPNET_EVT_ERROR)
            {
                LogFlow(("An error ocurred\n"));
                iscsiReattach(pImage);
            }
        }
        else
            iscsiLogRel(pImage, "iSCSI: Waiting for I/O failed rc=%Rrc\n", rc);
    }

    return VINF_SUCCESS;
}

/**
 * Internal. - Enqueues a request asynchronously.
 */
static int iscsiCommandAsync(PISCSIIMAGE pImage, PSCSIREQ pScsiReq,
                             PFNISCSICMDCOMPLETED pfnComplete, void *pvUser)
{
    int rc;

    if (pImage->fExtendedSelectSupported)
    {
        PISCSICMD pIScsiCmd = (PISCSICMD)RTMemAllocZ(sizeof(ISCSICMD));
        if (!pIScsiCmd)
            return VERR_NO_MEMORY;

        /* Init the command structure. */
        pIScsiCmd->pNext                    = NULL;
        pIScsiCmd->enmCmdType               = ISCSICMDTYPE_REQ;
        pIScsiCmd->pfnComplete              = pfnComplete;
        pIScsiCmd->pvUser                   = pvUser;
        pIScsiCmd->CmdType.ScsiReq.pScsiReq = pScsiReq;

        rc = iscsiCmdPut(pImage, pIScsiCmd);
        if (RT_FAILURE(rc))
            RTMemFree(pIScsiCmd);
    }
    else
        rc = VERR_NOT_SUPPORTED;

    return rc;
}

static DECLCALLBACK(void) iscsiCommandCompleteSync(PISCSIIMAGE pImage, int rcReq, void *pvUser)
{
    RT_NOREF1(pImage);
    PISCSICMDSYNC pIScsiCmdSync = (PISCSICMDSYNC)pvUser;

    pIScsiCmdSync->rcCmd = rcReq;
    int rc = RTSemEventSignal(pIScsiCmdSync->EventSem);
    AssertRC(rc);
}

/**
 * Internal. - Enqueues a request in a synchronous fashion
 * i.e. returns when the request completed.
 */
static int iscsiCommandSync(PISCSIIMAGE pImage, PSCSIREQ pScsiReq, bool fRetry, int rcSense)
{
    int rc;

    if (pImage->fExtendedSelectSupported)
    {
        ISCSICMDSYNC IScsiCmdSync;

        /* Create event semaphore. */
        rc = RTSemEventCreate(&IScsiCmdSync.EventSem);
        if (RT_FAILURE(rc))
            return rc;

        if (fRetry)
        {
            for (unsigned i = 0; i < 10; i++)
            {
                rc = iscsiCommandAsync(pImage, pScsiReq, iscsiCommandCompleteSync, &IScsiCmdSync);
                if (RT_FAILURE(rc))
                    break;

                rc = RTSemEventWait(IScsiCmdSync.EventSem, RT_INDEFINITE_WAIT);
                AssertRC(rc);
                rc = IScsiCmdSync.rcCmd;

                if (    (RT_SUCCESS(rc) && !pScsiReq->cbSense)
                    ||  RT_FAILURE(rc))
                    break;
                rc = rcSense;
            }
        }
        else
        {
            rc = iscsiCommandAsync(pImage, pScsiReq, iscsiCommandCompleteSync, &IScsiCmdSync);
            if (RT_SUCCESS(rc))
            {
                rc = RTSemEventWait(IScsiCmdSync.EventSem, RT_INDEFINITE_WAIT);
                AssertRC(rc);
                rc = IScsiCmdSync.rcCmd;

                if (RT_FAILURE(rc) || pScsiReq->cbSense > 0)
                    rc = rcSense;
            }
        }

        RTSemEventDestroy(IScsiCmdSync.EventSem);
    }
    else
    {
        if (fRetry)
        {
            rc = VINF_SUCCESS; /* (MSC incorrectly thinks it can be uninitialized) */
            for (unsigned i = 0; i < 10; i++)
            {
                rc = iscsiCommand(pImage, pScsiReq);
                if (    (RT_SUCCESS(rc) && !pScsiReq->cbSense)
                    ||  RT_FAILURE(rc))
                    break;
                rc = rcSense;
            }
        }
        else
        {
            rc = iscsiCommand(pImage, pScsiReq);
            if (RT_FAILURE(rc) || pScsiReq->cbSense > 0)
                rc = rcSense;
        }
    }

    return rc;
}


/**
 * Internal. - Executes a given function in a synchronous fashion
 *             on the I/O thread if available.
 */
static int iscsiExecSync(PISCSIIMAGE pImage, PFNISCSIEXEC pfnExec, void *pvUser)
{
    int rc;

    if (pImage->fExtendedSelectSupported)
    {
        ISCSICMDSYNC IScsiCmdSync;
        PISCSICMD pIScsiCmd = (PISCSICMD)RTMemAllocZ(sizeof(ISCSICMD));
        if (!pIScsiCmd)
            return VERR_NO_MEMORY;

        /* Create event semaphore. */
        rc = RTSemEventCreate(&IScsiCmdSync.EventSem);
        if (RT_FAILURE(rc))
        {
            RTMemFree(pIScsiCmd);
            return rc;
        }

        /* Init the command structure. */
        pIScsiCmd->pNext                = NULL;
        pIScsiCmd->enmCmdType           = ISCSICMDTYPE_EXEC;
        pIScsiCmd->pfnComplete          = iscsiCommandCompleteSync;
        pIScsiCmd->pvUser               = &IScsiCmdSync;
        pIScsiCmd->CmdType.Exec.pfnExec = pfnExec;
        pIScsiCmd->CmdType.Exec.pvUser  = pvUser;

        rc = iscsiCmdPut(pImage, pIScsiCmd);
        if (RT_FAILURE(rc))
            RTMemFree(pIScsiCmd);
        else
        {
            rc = RTSemEventWait(IScsiCmdSync.EventSem, RT_INDEFINITE_WAIT);
            AssertRC(rc);
            rc = IScsiCmdSync.rcCmd;
        }

        RTSemEventDestroy(IScsiCmdSync.EventSem);
    }
    else
    {
        /* No I/O thread, execute in the current thread. */
        rc = pfnExec(pvUser);
    }

    return rc;
}


static DECLCALLBACK(void) iscsiCommandAsyncComplete(PISCSIIMAGE pImage, int rcReq, void *pvUser)
{
    bool fComplete = true;
    size_t cbTransfered = 0;
    PSCSIREQ pScsiReq = (PSCSIREQ)pvUser;

    if (RT_SUCCESS(rcReq))
        ASMAtomicWriteU32(&pImage->cLoginsSinceIo, 0);

    if (   RT_SUCCESS(rcReq)
        && pScsiReq->cbSense > 0)
    {
        /* Try again if possible. */
        if (pScsiReq->cSenseRetries > 0)
        {
            pScsiReq->cSenseRetries--;
            pScsiReq->cbSense = sizeof(pScsiReq->abSense);
            int rc = iscsiCommandAsync(pImage, pScsiReq, iscsiCommandAsyncComplete, pScsiReq);
            if (RT_SUCCESS(rc))
                fComplete = false;
            else
                rcReq = pScsiReq->rcSense;
        }
        else
            rcReq = pScsiReq->rcSense;
    }

    if (fComplete)
    {
        if (pScsiReq->enmXfer == SCSIXFER_FROM_TARGET)
            cbTransfered = pScsiReq->cbT2IData;
        else if (pScsiReq->enmXfer == SCSIXFER_TO_TARGET)
            cbTransfered = pScsiReq->cbI2TData;
        else
            AssertMsg(pScsiReq->enmXfer == SCSIXFER_NONE, ("To/From transfers are not supported yet\n"));

        /* Continue I/O context. */
        pImage->pIfIo->pfnIoCtxCompleted(pImage->pIfIo->Core.pvUser,
                                         pScsiReq->pIoCtx, rcReq,
                                         cbTransfered);

        RTMemFree(pScsiReq);
    }
}


/**
 * Internal. Free all allocated space for representing an image, and optionally
 * delete the image from disk.
 */
static int iscsiFreeImage(PISCSIIMAGE pImage, bool fDelete)
{
    int rc = VINF_SUCCESS;
    Assert(!fDelete); NOREF(fDelete); /* This MUST be false, the flag isn't supported. */

    /* Freeing a never allocated image (e.g. because the open failed) is
     * not signalled as an error. After all nothing bad happens. */
    if (pImage)
    {
        if (pImage->Mutex != NIL_RTSEMMUTEX)
        {
            /* Detaching only makes sense when the mutex is there. Otherwise the
             * failure happened long before we could attach to the target. */
            iscsiExecSync(pImage, iscsiDetach, pImage);
            RTSemMutexDestroy(pImage->Mutex);
            pImage->Mutex = NIL_RTSEMMUTEX;
        }
        if (pImage->hThreadIo != NIL_RTTHREAD)
        {
            ASMAtomicXchgBool(&pImage->fRunning, false);
            rc = iscsiIoThreadPoke(pImage);
            AssertRC(rc);

            /* Wait for the thread to terminate. */
            rc = RTThreadWait(pImage->hThreadIo, RT_INDEFINITE_WAIT, NULL);
            AssertRC(rc);
        }
        /* Destroy the socket. */
        if (pImage->Socket != NIL_VDSOCKET)
        {
            pImage->pIfNet->pfnSocketDestroy(pImage->Socket);
        }
        if (pImage->MutexReqQueue != NIL_RTSEMMUTEX)
        {
            RTSemMutexDestroy(pImage->MutexReqQueue);
            pImage->MutexReqQueue = NIL_RTSEMMUTEX;
        }
        if (pImage->pszTargetName)
        {
            RTMemFree(pImage->pszTargetName);
            pImage->pszTargetName = NULL;
        }
        if (pImage->pszTargetAddress)
        {
            RTMemFree(pImage->pszTargetAddress);
            pImage->pszTargetAddress = NULL;
        }
        if (pImage->pszInitiatorName)
        {
            if (pImage->fAutomaticInitiatorName)
                RTStrFree(pImage->pszInitiatorName);
            else
                RTMemFree(pImage->pszInitiatorName);
            pImage->pszInitiatorName = NULL;
        }
        if (pImage->pszInitiatorUsername)
        {
            RTMemFree(pImage->pszInitiatorUsername);
            pImage->pszInitiatorUsername = NULL;
        }
        if (pImage->pbInitiatorSecret)
        {
            RTMemFree(pImage->pbInitiatorSecret);
            pImage->pbInitiatorSecret = NULL;
        }
        if (pImage->pszTargetUsername)
        {
            RTMemFree(pImage->pszTargetUsername);
            pImage->pszTargetUsername = NULL;
        }
        if (pImage->pbTargetSecret)
        {
            RTMemFree(pImage->pbTargetSecret);
            pImage->pbTargetSecret = NULL;
        }
        if (pImage->pvRecvPDUBuf)
        {
            RTMemFree(pImage->pvRecvPDUBuf);
            pImage->pvRecvPDUBuf = NULL;
        }
        if (pImage->pszHostname)
        {
            RTMemFree(pImage->pszHostname);
            pImage->pszHostname = NULL;
        }

        pImage->cbRecvPDUResidual = 0;
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Inits the basic iSCSI image state, allocating vital resources.
 *
 * @returns VBox status code.
 * @param   pImage          The iSCSI image instance.
 */
static int iscsiOpenImageInit(PISCSIIMAGE pImage)
{
    int rc = VINF_SUCCESS;

    /* Get error signalling interface. */
    pImage->pIfError = VDIfErrorGet(pImage->pVDIfsDisk);

    /* Get TCP network stack interface. */
    pImage->pIfNet = VDIfTcpNetGet(pImage->pVDIfsImage);
    if (pImage->pIfNet)
    {
        /* Get configuration interface. */
        pImage->pIfConfig = VDIfConfigGet(pImage->pVDIfsImage);
        if (pImage->pIfConfig)
        {
            /* Get I/O interface. */
            pImage->pIfIo = VDIfIoIntGet(pImage->pVDIfsImage);
            if (pImage->pIfIo)
            {
                /* This ISID will be adjusted later to make it unique on this host. */
                pImage->pszHostname          = NULL;
                pImage->uPort                = 0;
                pImage->Socket               = NIL_VDSOCKET;
                pImage->ISID                 = 0x800000000000ULL | 0x001234560000ULL;
                pImage->cISCSIRetries        = 10;
                pImage->state                = ISCSISTATE_FREE;
                pImage->cLoginsSinceIo       = 0;
                pImage->Mutex                = NIL_RTSEMMUTEX;
                pImage->MutexReqQueue        = NIL_RTSEMMUTEX;
                pImage->pszInitiatorUsername = NULL;
                pImage->pbInitiatorSecret    = NULL;
                pImage->cbInitiatorSecret    = 0;
                pImage->pszTargetUsername    = NULL;
                pImage->pbTargetSecret       = NULL;
                pImage->cbTargetSecret       = 0;

                memset(pImage->aCmdsWaiting, 0, sizeof(pImage->aCmdsWaiting));
                pImage->cbRecvPDUResidual = 0;

                pImage->pvRecvPDUBuf    = RTMemAlloc(ISCSI_RECV_PDU_BUFFER_SIZE);
                pImage->cbRecvPDUBuf    = ISCSI_RECV_PDU_BUFFER_SIZE;
                if (!pImage->pvRecvPDUBuf)
                    rc = VERR_NO_MEMORY;

                if (RT_SUCCESS(rc))
                    rc = RTSemMutexCreate(&pImage->Mutex);
                if (RT_SUCCESS(rc))
                    rc = RTSemMutexCreate(&pImage->MutexReqQueue);
            }
            else
                rc = vdIfError(pImage->pIfError, VERR_VD_UNKNOWN_INTERFACE,
                               RT_SRC_POS, N_("iSCSI: I/O interface missing"));
        }
        else
            rc = vdIfError(pImage->pIfError, VERR_VD_UNKNOWN_INTERFACE,
                           RT_SRC_POS, N_("iSCSI: configuration interface missing"));
    }
    else
        rc = vdIfError(pImage->pIfError, VERR_VD_UNKNOWN_INTERFACE,
                       RT_SRC_POS, N_("iSCSI: TCP network stack interface missing"));

    return rc;
}

/**
 * Parses the user supplied config before opening the connection to the target.
 *
 * @returns VBox status code.
 * @param   pImage          The iSCSI image instance.
 */
static int iscsiOpenImageParseCfg(PISCSIIMAGE pImage)
{
    char *pszLUN = NULL, *pszLUNInitial = NULL;
    bool fLunEncoded = false;
    uint32_t uWriteSplitDef = 0;
    uint32_t uTimeoutDef = 0;
    uint64_t uCfgTmp = 0;
    bool fHostIPDef = false;
    bool fDumpMalformedPacketsDef = false;

    int rc = RTStrToUInt32Full(s_iscsiConfigDefaultWriteSplit, 0, &uWriteSplitDef);
    AssertRC(rc);
    rc = RTStrToUInt32Full(s_iscsiConfigDefaultTimeout, 0, &uTimeoutDef);
    AssertRC(rc);
    rc = RTStrToUInt64Full(s_iscsiConfigDefaultHostIPStack, 0, &uCfgTmp);
    AssertRC(rc);
    fHostIPDef = RT_BOOL(uCfgTmp);
    rc = RTStrToUInt64Full(s_iscsiConfigDefaultDumpMalformedPackets, 0, &uCfgTmp);
    AssertRC(rc);
    fDumpMalformedPacketsDef = RT_BOOL(uCfgTmp);

    /* Validate configuration, detect unknown keys. */
    if (!VDCFGAreKeysValid(pImage->pIfConfig,
                           "TargetName\0"
                           "InitiatorName\0"
                           "LUN\0"
                           "TargetAddress\0"
                           "InitiatorUsername\0"
                           "InitiatorSecret\0"
                           "InitiatorSecretEncrypted\0"
                           "TargetUsername\0"
                           "TargetSecret\0"
                           "WriteSplit\0"
                           "Timeout\0"
                           "HostIPStack\0"
                           "DumpMalformedPackets\0"))
        return vdIfError(pImage->pIfError, VERR_VD_UNKNOWN_CFG_VALUES, RT_SRC_POS, N_("iSCSI: configuration error: unknown configuration keys present"));

    /* Query the iSCSI upper level configuration. */
    rc = VDCFGQueryStringAlloc(pImage->pIfConfig, "TargetName", &pImage->pszTargetName);
    if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("iSCSI: configuration error: failed to read TargetName as string"));

    rc = VDCFGQueryStringAlloc(pImage->pIfConfig, "InitiatorName", &pImage->pszInitiatorName);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND || rc == VERR_CFGM_NO_PARENT)
        pImage->fAutomaticInitiatorName = true;
    else if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("iSCSI: configuration error: failed to read InitiatorName as string"));

    rc = VDCFGQueryStringAlloc(pImage->pIfConfig, "LUN", &pszLUN);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND || rc == VERR_CFGM_NO_PARENT)
    {
        rc = VINF_SUCCESS;
        pImage->fAutomaticLUN = true;
    }
    else if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("iSCSI: configuration error: failed to read LUN as string"));

    if (pImage->fAutomaticLUN)
        pImage->LUN = 0;    /* Default to LUN 0. */
    else
    {
        pszLUNInitial = pszLUN;
        if (!strncmp(pszLUN, "enc", 3))
        {
            fLunEncoded = true;
            pszLUN += 3;
        }
        rc = RTStrToUInt64Full(pszLUN, 0, &pImage->LUN);
        if (RT_FAILURE(rc))
            rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("iSCSI: configuration error: failed to convert LUN to integer"));

        RTMemFree(pszLUNInitial);
    }
    if (RT_SUCCESS(rc) && !fLunEncoded)
    {
        if (pImage->LUN <= 255)
            pImage->LUN = pImage->LUN << 48; /* uses peripheral device addressing method */
        else if (pImage->LUN <= 16383)
            pImage->LUN = (pImage->LUN << 48) | RT_BIT_64(62); /* uses flat space addressing method */
        else
            rc = vdIfError(pImage->pIfError, VERR_OUT_OF_RANGE, RT_SRC_POS, N_("iSCSI: configuration error: LUN number out of range (0-16383)"));
    }

    if (RT_FAILURE(rc))
        return rc;

    rc = VDCFGQueryStringAlloc(pImage->pIfConfig, "TargetAddress", &pImage->pszTargetAddress);
    if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("iSCSI: configuration error: failed to read TargetAddress as string"));

    rc = VDCFGQueryStringAlloc(pImage->pIfConfig, "InitiatorUsername", &pImage->pszInitiatorUsername);
    if (RT_FAILURE(rc) && rc != VERR_CFGM_VALUE_NOT_FOUND && rc != VERR_CFGM_NO_PARENT)
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("iSCSI: configuration error: failed to read InitiatorUsername as string"));

    rc = VDCFGQueryBytesAlloc(pImage->pIfConfig, "InitiatorSecret",
                              (void **)&pImage->pbInitiatorSecret, &pImage->cbInitiatorSecret);
    if (RT_FAILURE(rc) && rc != VERR_CFGM_VALUE_NOT_FOUND && rc != VERR_CFGM_NO_PARENT)
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("iSCSI: configuration error: failed to read InitiatorSecret as byte string"));

    void *pvInitiatorSecretEncrypted;
    size_t cbInitiatorSecretEncrypted;
    rc = VDCFGQueryBytesAlloc(pImage->pIfConfig, "InitiatorSecretEncrypted",
                              &pvInitiatorSecretEncrypted, &cbInitiatorSecretEncrypted);
    if (RT_SUCCESS(rc))
    {
        RTMemFree(pvInitiatorSecretEncrypted);
        if (!pImage->pbInitiatorSecret)
        {
            /* we have an encrypted initiator secret but not an unencrypted one */
            return vdIfError(pImage->pIfError, VERR_VD_ISCSI_SECRET_ENCRYPTED, RT_SRC_POS, N_("iSCSI: initiator secret not decrypted"));
        }
    }

    rc = VDCFGQueryStringAlloc(pImage->pIfConfig, "TargetUsername", &pImage->pszTargetUsername);
    if (RT_FAILURE(rc) && rc != VERR_CFGM_VALUE_NOT_FOUND && rc != VERR_CFGM_NO_PARENT)
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("iSCSI: configuration error: failed to read TargetUsername as string"));

    rc = VDCFGQueryBytesAlloc(pImage->pIfConfig, "TargetSecret",
                              (void **)&pImage->pbTargetSecret, &pImage->cbTargetSecret);
    if (RT_FAILURE(rc) && rc != VERR_CFGM_VALUE_NOT_FOUND && rc != VERR_CFGM_NO_PARENT)
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("iSCSI: configuration error: failed to read TargetSecret as byte string"));

    rc = VDCFGQueryU32Def(pImage->pIfConfig, "WriteSplit", &pImage->cbWriteSplit, uWriteSplitDef);
    if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("iSCSI: configuration error: failed to read WriteSplit as U32"));

    /* Query the iSCSI lower level configuration. */
    rc = VDCFGQueryU32Def(pImage->pIfConfig, "Timeout", &pImage->uReadTimeout, uTimeoutDef);
    if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("iSCSI: configuration error: failed to read Timeout as U32"));

    rc = VDCFGQueryBoolDef(pImage->pIfConfig, "HostIPStack", &pImage->fHostIP, fHostIPDef);
    if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("iSCSI: configuration error: failed to read HostIPStack as boolean"));

    rc = VDCFGQueryBoolDef(pImage->pIfConfig, "DumpMalformedPackets",
                           &pImage->fDumpMalformedPackets, fDumpMalformedPacketsDef);
    if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("iSCSI: configuration error: failed to read DumpMalformedPackets as boolean"));

    return VINF_SUCCESS;
}

/**
 * Creates the necessary socket structure.
 *
 * @returns VBox status code.
 * @param   pImage          The iSCSI image instance.
 */
static int iscsiOpenImageSocketCreate(PISCSIIMAGE pImage)
{
    /* Create the socket structure. */
    int rc = pImage->pIfNet->pfnSocketCreate(VD_INTERFACETCPNET_CONNECT_EXTENDED_SELECT,
                                             &pImage->Socket);
    if (RT_SUCCESS(rc))
    {
        pImage->fExtendedSelectSupported = true;
        pImage->fRunning = true;
        rc = RTThreadCreate(&pImage->hThreadIo, iscsiIoThreadWorker, pImage, 0,
                            RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "iSCSI-Io");
        if (RT_FAILURE(rc))
            LogFunc(("Creating iSCSI I/O thread failed rc=%Rrc\n", rc));
    }
    else if (rc == VERR_NOT_SUPPORTED)
    {
        /* Async I/O is not supported without extended select. */
        if ((pImage->uOpenFlags & VD_OPEN_FLAGS_ASYNC_IO))
            LogFunc(("Extended select is not supported by the interface but async I/O is requested -> %Rrc\n", rc));
        else
        {
            pImage->fExtendedSelectSupported = false;
            rc = pImage->pIfNet->pfnSocketCreate(0, &pImage->Socket);
        }
    }

    if (RT_FAILURE(rc))
        LogFunc(("Creating socket failed -> %Rrc\n", rc));

    return rc;
}

/**
 * Issues a REPORT LUNS to the target.
 *
 * @returns VBox status code.
 * @param   pImage          The iSCSI image instance.
 */
static int iscsiOpenImageReportLuns(PISCSIIMAGE pImage)
{
    SCSIREQ sr;
    RTSGSEG DataSeg;
    uint8_t rlundata[16];

    /*
     * Inquire available LUNs.
     */
    RT_ZERO(sr.abCDB);
    sr.abCDB[0] = SCSI_REPORT_LUNS;
    sr.abCDB[1] = 0;        /* reserved */
    sr.abCDB[2] = 0;        /* reserved */
    sr.abCDB[3] = 0;        /* reserved */
    sr.abCDB[4] = 0;        /* reserved */
    sr.abCDB[5] = 0;        /* reserved */
    sr.abCDB[6] = sizeof(rlundata) >> 24;
    sr.abCDB[7] = (sizeof(rlundata) >> 16) & 0xff;
    sr.abCDB[8] = (sizeof(rlundata) >> 8) & 0xff;
    sr.abCDB[9] = sizeof(rlundata) & 0xff;
    sr.abCDB[10] = 0;       /* reserved */
    sr.abCDB[11] = 0;       /* control */

    DataSeg.pvSeg = rlundata;
    DataSeg.cbSeg = sizeof(rlundata);

    sr.enmXfer   = SCSIXFER_FROM_TARGET;
    sr.cbCDB     = 12;
    sr.cbI2TData = 0;
    sr.paI2TSegs = NULL;
    sr.cI2TSegs  = 0;
    sr.cbT2IData = DataSeg.cbSeg;
    sr.paT2ISegs = &DataSeg;
    sr.cT2ISegs  = 1;
    sr.cbSense   = sizeof(sr.abSense);
    int rc = iscsiCommandSync(pImage, &sr, false, VERR_INVALID_STATE);
    if (RT_FAILURE(rc))
        LogRel(("iSCSI: Could not get LUN info for target %s, rc=%Rrc\n", pImage->pszTargetName, rc));

    /* If there is a single LUN on the target, then either verify that it matches the explicitly
     * configured LUN, or just use it if a LUN was not configured (defaulted to 0). For multi-LUN targets,
     * require a correctly configured LUN.
     */
    uint32_t    cbLuns = (rlundata[0] << 24) | (rlundata[1] << 16) | (rlundata[2] << 8) | rlundata[3];
    unsigned    cLuns  = cbLuns / 8;

    /* Dig out the first LUN. */
    uint64_t    uTgtLun = 0;
    if ((rlundata[8] & 0xc0) == 0)
    {
        /* Single-byte LUN in 0-255 range. */
        uTgtLun = rlundata[9];
    }
    else if ((rlundata[8] & 0xc0) == 0x40)
    {
        /* Two-byte LUN in 256-16383 range. */
        uTgtLun = rlundata[9] | ((rlundata[8] & 0x3f) << 8);
        uTgtLun = (uTgtLun << 48) | RT_BIT_64(62);
    }
    else
        rc = vdIfError(pImage->pIfError, VERR_OUT_OF_RANGE, RT_SRC_POS, N_("iSCSI: Reported LUN number out of range (0-16383)"));
    if (RT_FAILURE(rc))
        return rc;

    LogRel(("iSCSI: %u LUN(s), first LUN %RX64\n", cLuns, uTgtLun));

    /* Convert the LUN back into the 64-bit format. */
    if (uTgtLun <= 255)
        uTgtLun = uTgtLun << 48;
    else
    {
        Assert(uTgtLun <= 16383);
        uTgtLun = (uTgtLun << 48) | RT_BIT_64(62);
    }

    if (cLuns == 1)
    {
        /* NB: It is valid to have a single LUN other than zero, at least in SPC-3. */
        if (pImage->fAutomaticLUN)
            pImage->LUN = uTgtLun;
        else if (pImage->LUN != uTgtLun)
            rc = vdIfError(pImage->pIfError, VERR_VD_ISCSI_INVALID_TYPE, RT_SRC_POS, N_("iSCSI: configuration error: Configured LUN does not match what target provides"));
    }

    return rc;
}

/**
 * Issues the INQUIRY command to the target and checks for the correct device type.
 *
 * @returns VBox status code.
 * @param   pImage          The iSCSI image instance.
 */
static int iscsiOpenImageInquiry(PISCSIIMAGE pImage)
{
    SCSIREQ sr;
    RTSGSEG DataSeg;
    uint8_t data8[8];

    /*
     * Inquire device characteristics - no tapes, scanners etc., please.
     */
    RT_ZERO(sr.abCDB);
    sr.abCDB[0] = SCSI_INQUIRY;
    sr.abCDB[1] = 0;         /* reserved */
    sr.abCDB[2] = 0;         /* reserved */
    sr.abCDB[3] = 0;         /* reserved */
    sr.abCDB[4] = sizeof(data8);
    sr.abCDB[5] = 0;         /* control */

    DataSeg.pvSeg = data8;
    DataSeg.cbSeg = sizeof(data8);

    sr.enmXfer   = SCSIXFER_FROM_TARGET;
    sr.cbCDB     = 6;
    sr.cbI2TData = 0;
    sr.paI2TSegs = NULL;
    sr.cI2TSegs  = 0;
    sr.cbT2IData = DataSeg.cbSeg;
    sr.paT2ISegs = &DataSeg;
    sr.cT2ISegs  = 1;
    sr.cbSense   = sizeof(sr.abSense);
    int rc = iscsiCommandSync(pImage, &sr, true /* fRetry */, VERR_INVALID_STATE);
    if (RT_SUCCESS(rc))
    {
        uint8_t devType = (sr.cbT2IData > 0) ? data8[0] & SCSI_DEVTYPE_MASK : 255;
        if (devType == SCSI_DEVTYPE_DISK)
        {
            uint8_t uCmdQueue = (sr.cbT2IData >= 8) ? data8[7] & SCSI_INQUIRY_CMDQUE_MASK : 0;
            if (uCmdQueue > 0)
                pImage->fCmdQueuingSupported = true;
            else if (pImage->uOpenFlags & VD_OPEN_FLAGS_ASYNC_IO)
                rc = VERR_NOT_SUPPORTED;
            else
                LogRel(("iSCSI: target address %s, target name %s, %s command queuing\n",
                        pImage->pszTargetAddress, pImage->pszTargetName,
                        pImage->fCmdQueuingSupported ? "supports" : "doesn't support"));
        }
        else
        {
            rc = vdIfError(pImage->pIfError, VERR_VD_ISCSI_INVALID_TYPE,
                            RT_SRC_POS, N_("iSCSI: target address %s, target name %s, SCSI LUN %lld reports device type=%u"),
                            pImage->pszTargetAddress, pImage->pszTargetName,
                            pImage->LUN, devType);
            LogRel(("iSCSI: Unsupported SCSI peripheral device type %d for target %s\n", devType & SCSI_DEVTYPE_MASK, pImage->pszTargetName));
        }
    }
    else
        LogRel(("iSCSI: Could not get INQUIRY info for target %s, rc=%Rrc\n", pImage->pszTargetName, rc));

    return rc;
}

/**
 * Checks that the target allows write access if the caller requested it.
 *
 * @returns VBox status code.
 * @param   pImage          The iSCSI image instance.
 */
static int iscsiOpenImageCheckWriteAccess(PISCSIIMAGE pImage)
{
    SCSIREQ sr;
    RTSGSEG DataSeg;
    uint8_t data4[4];

    /*
     * Query write disable bit in the device specific parameter entry in the
     * mode parameter header. Refuse read/write opening of read only disks.
     */
    RT_ZERO(sr.abCDB);
    sr.abCDB[0] = SCSI_MODE_SENSE_6;
    sr.abCDB[1] = 0;             /* dbd=0/reserved */
    sr.abCDB[2] = 0x3f;          /* pc=0/page code=0x3f, ask for all pages */
    sr.abCDB[3] = 0;             /* subpage code=0, return everything in page_0 format */
    sr.abCDB[4] = sizeof(data4); /* allocation length=4 */
    sr.abCDB[5] = 0;             /* control */

    DataSeg.pvSeg = data4;
    DataSeg.cbSeg = sizeof(data4);

    sr.enmXfer   = SCSIXFER_FROM_TARGET;
    sr.cbCDB     = 6;
    sr.cbI2TData = 0;
    sr.paI2TSegs = NULL;
    sr.cI2TSegs  = 0;
    sr.cbT2IData = DataSeg.cbSeg;
    sr.paT2ISegs = &DataSeg;
    sr.cT2ISegs  = 1;
    sr.cbSense   = sizeof(sr.abSense);
    int rc = iscsiCommandSync(pImage, &sr, true /* fRetry */, VERR_INVALID_STATE);
    if (RT_SUCCESS(rc))
    {
        pImage->fTargetReadOnly = !!(data4[2] & 0x80);
        if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY) && pImage->fTargetReadOnly)
            rc = VERR_VD_IMAGE_READ_ONLY;
    }
    else
        LogRel(("iSCSI: Could not get MODE SENSE info for target %s, rc=%Rrc\n", pImage->pszTargetName, rc));

    return rc;
}

/**
 * Queries the media and sector size of the target.
 *
 * @returns VBox status code.
 * @param   pImage          The iSCSI image instance.
 */
static int iscsiOpenImageQueryTargetSizes(PISCSIIMAGE pImage)
{
    SCSIREQ sr;
    RTSGSEG DataSeg;
    uint8_t data12[12];

    /*
     * Determine sector size and capacity of the volume immediately.
     */
    RT_ZERO(data12);
    RT_ZERO(sr.abCDB);
    sr.abCDB[0] = SCSI_SERVICE_ACTION_IN_16;
    sr.abCDB[1] = SCSI_SVC_ACTION_IN_READ_CAPACITY_16;   /* subcommand */
    sr.abCDB[10+3] = sizeof(data12);                     /* allocation length (dword) */

    DataSeg.pvSeg = data12;
    DataSeg.cbSeg = sizeof(data12);

    sr.enmXfer   = SCSIXFER_FROM_TARGET;
    sr.cbCDB     = 16;
    sr.cbI2TData = 0;
    sr.paI2TSegs = NULL;
    sr.cI2TSegs  = 0;
    sr.cbT2IData = DataSeg.cbSeg;
    sr.paT2ISegs = &DataSeg;
    sr.cT2ISegs  = 1;
    sr.cbSense   = sizeof(sr.abSense);

    int rc = iscsiCommandSync(pImage, &sr, false /* fRetry */, VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        bool fEnd = false;
        uint8_t cMaxRetries = 10;
        do
        {
            switch (sr.status)
            {
                case SCSI_STATUS_OK:
                {
                    pImage->cVolume = RT_BE2H_U64(*(uint64_t *)&data12[0]);
                    pImage->cVolume++;
                    pImage->cbSector = RT_BE2H_U32(*(uint32_t *)&data12[8]);
                    pImage->cbSize = pImage->cVolume * pImage->cbSector;
                    if (pImage->cVolume == 0 || pImage->cbSize < pImage->cVolume)
                    {
                        rc = vdIfError(pImage->pIfError, VERR_VD_ISCSI_INVALID_TYPE,
                                       RT_SRC_POS, N_("iSCSI: target address %s, target name %s, SCSI LUN %lld reports media sector count=%llu sector size=%u"),
                                       pImage->pszTargetAddress, pImage->pszTargetName,
                                       pImage->LUN, pImage->cVolume, pImage->cbSector);
                    }
                    fEnd = true;
                    break;
                }
                case SCSI_STATUS_CHECK_CONDITION:
                {
                    if((sr.abSense[2] & 0x0f) == SCSI_SENSE_UNIT_ATTENTION)
                    {
                        if(   sr.abSense[12] == SCSI_ASC_POWER_ON_RESET_BUS_DEVICE_RESET_OCCURRED
                           && sr.abSense[13] == SCSI_ASCQ_POWER_ON_RESET_BUS_DEVICE_RESET_OCCURRED)
                        {
                            /** @todo for future: prepare and send command "REQUEST SENSE" which will
                             *                    return the status of target and will clear any unit attention
                             *                    condition that it reports */
                            rc = iscsiCommandSync(pImage, &sr, false /* fRetry */, VINF_SUCCESS);
                            if (RT_FAILURE(rc))
                                fEnd = true;
                            cMaxRetries--;
                            break;
                        }
                    }
                    break;
                }
                default:
                {
                    rc = iscsiCommandSync(pImage, &sr, false /* fRetry */, VINF_SUCCESS);
                    if (RT_FAILURE(rc))
                        fEnd = true;
                    cMaxRetries--;
                    break;
                }
            }
            if (!cMaxRetries)
                fEnd = true;
        } while(!fEnd);
    }
    else
    {
        uint8_t data8[8];

        RT_ZERO(data8);
        sr.abCDB[0] = SCSI_READ_CAPACITY;
        sr.abCDB[1] = 0;   /* reserved */
        sr.abCDB[2] = 0;   /* reserved */
        sr.abCDB[3] = 0;   /* reserved */
        sr.abCDB[4] = 0;   /* reserved */
        sr.abCDB[5] = 0;   /* reserved */
        sr.abCDB[6] = 0;   /* reserved */
        sr.abCDB[7] = 0;   /* reserved */
        sr.abCDB[8] = 0;   /* reserved */
        sr.abCDB[9] = 0;   /* control */

        DataSeg.pvSeg = data8;
        DataSeg.cbSeg = sizeof(data8);

        sr.enmXfer   = SCSIXFER_FROM_TARGET;
        sr.cbCDB     = 10;
        sr.cbI2TData = 0;
        sr.paI2TSegs = NULL;
        sr.cI2TSegs  = 0;
        sr.cbT2IData = DataSeg.cbSeg;
        sr.paT2ISegs = &DataSeg;
        sr.cT2ISegs  = 1;
        sr.cbSense   = sizeof(sr.abSense);
        rc = iscsiCommandSync(pImage, &sr, false /* fRetry */, VINF_SUCCESS);
        if (RT_SUCCESS(rc))
        {
            bool fEnd = false;
            uint8_t cMaxRetries = 10;
            do
            {
                switch (sr.status)
                {
                    case SCSI_STATUS_OK:
                    {
                        pImage->cVolume = (data8[0] << 24) | (data8[1] << 16) | (data8[2] << 8) | data8[3];
                        pImage->cVolume++;
                        pImage->cbSector = (data8[4] << 24) | (data8[5] << 16) | (data8[6] << 8) | data8[7];
                        pImage->cbSize = pImage->cVolume * pImage->cbSector;
                        if (pImage->cVolume == 0)
                        {
                            rc = vdIfError(pImage->pIfError, VERR_VD_ISCSI_INVALID_TYPE,
                                           RT_SRC_POS, N_("iSCSI: fallback capacity detection for target address %s, target name %s, SCSI LUN %lld reports media sector count=%llu sector size=%u"),
                                           pImage->pszTargetAddress, pImage->pszTargetName,
                                           pImage->LUN, pImage->cVolume, pImage->cbSector);
                        }

                        fEnd = true;
                        break;
                    }
                    case SCSI_STATUS_CHECK_CONDITION:
                    {
                        if((sr.abSense[2] & 0x0f) == SCSI_SENSE_UNIT_ATTENTION)
                        {
                            if(   sr.abSense[12] == SCSI_ASC_POWER_ON_RESET_BUS_DEVICE_RESET_OCCURRED
                               && sr.abSense[13] == SCSI_ASCQ_POWER_ON_RESET_BUS_DEVICE_RESET_OCCURRED)
                            {
                                /** @todo for future: prepare and send command "REQUEST SENSE" which will
                                 *                    return the status of target and will clear any unit attention
                                 *                    condition that it reports */
                                rc = iscsiCommandSync(pImage, &sr, false /* fRetry */, VINF_SUCCESS);
                                if (RT_FAILURE(rc))
                                    fEnd = true;
                                cMaxRetries--;
                                break;

                            }
                        }
                        break;
                    }
                    default:
                    {
                        rc = iscsiCommandSync(pImage, &sr, false /* fRetry */, VINF_SUCCESS);
                        if (RT_FAILURE(rc))
                            fEnd = true;
                        cMaxRetries--;
                        break;
                    }
                }
                if (!cMaxRetries)
                    fEnd = true;
            } while(!fEnd);
        }
        else
            LogRel(("iSCSI: Could not determine capacity of target %s, rc=%Rrc\n", pImage->pszTargetName, rc));
    }

    return rc;
}

/**
 * Queries the state of the read/write caches and tries to enable them if disabled.
 *
 * @returns VBox status code.
 * @param   pImage          The iSCSI image instance.
 */
static int iscsiOpenImageEnableReadWriteCache(PISCSIIMAGE pImage)
{
    /*
     * Check the read and write cache bits.
     * Try to enable the cache if it is disabled.
     *
     * We already checked that this is a block access device. No need
     * to do it again.
     */
    SCSIREQ sr;
    RTSGSEG DataSeg;
    uint8_t aCachingModePage[32];

    memset(aCachingModePage, '\0', sizeof(aCachingModePage));
    sr.abCDB[0] = SCSI_MODE_SENSE_6;
    sr.abCDB[1] = 0;
    sr.abCDB[2] = (0x00 << 6) | (0x08 & 0x3f); /* Current values and caching mode page */
    sr.abCDB[3] = 0; /* Sub page code. */
    sr.abCDB[4] = sizeof(aCachingModePage) & 0xff;
    sr.abCDB[5] = 0;

    DataSeg.pvSeg = aCachingModePage;
    DataSeg.cbSeg = sizeof(aCachingModePage);

    sr.enmXfer   = SCSIXFER_FROM_TARGET;
    sr.cbCDB     = 6;
    sr.cbI2TData = 0;
    sr.paI2TSegs = NULL;
    sr.cI2TSegs  = 0;
    sr.cbT2IData = DataSeg.cbSeg;
    sr.paT2ISegs = &DataSeg;
    sr.cT2ISegs  = 1;
    sr.cbSense = sizeof(sr.abSense);
    int rc = iscsiCommandSync(pImage, &sr, false /* fRetry */, VINF_SUCCESS);
    if (   RT_SUCCESS(rc)
        && (sr.status == SCSI_STATUS_OK)
        && (aCachingModePage[0] >= 15)
        && (aCachingModePage[4 + aCachingModePage[3]] & 0x3f) == 0x08
        && (aCachingModePage[4 + aCachingModePage[3] + 1] > 3))
    {
        uint32_t Offset = 4 + aCachingModePage[3];
        /*
         * Check if the read and/or the write cache is disabled.
         * The write cache is disabled if bit 2 (WCE) is zero and
         * the read cache is disabled if bit 0 (RCD) is set.
         */
        if (!ASMBitTest(&aCachingModePage[Offset + 2], 2) || ASMBitTest(&aCachingModePage[Offset + 2], 0))
        {
            /*
             * Write Cache Enable (WCE) bit is zero or the Read Cache Disable (RCD) is one
             * So one of the caches is disabled. Enable both caches.
             * The rest is unchanged.
             */
            ASMBitSet(&aCachingModePage[Offset + 2], 2);
            ASMBitClear(&aCachingModePage[Offset + 2], 0);

            sr.abCDB[0] = SCSI_MODE_SELECT_6;
            sr.abCDB[1] = 0; /* Don't write the page into NV RAM. */
            sr.abCDB[2] = 0;
            sr.abCDB[3] = 0;
            sr.abCDB[4] = sizeof(aCachingModePage) & 0xff;
            sr.abCDB[5] = 0;

            DataSeg.pvSeg = aCachingModePage;
            DataSeg.cbSeg = sizeof(aCachingModePage);

            sr.enmXfer   = SCSIXFER_TO_TARGET;
            sr.cbCDB     = 6;
            sr.cbI2TData = DataSeg.cbSeg;
            sr.paI2TSegs = &DataSeg;
            sr.cI2TSegs  = 1;
            sr.cbT2IData = 0;
            sr.paT2ISegs = NULL;
            sr.cT2ISegs  = 0;
            sr.cbSense   = sizeof(sr.abSense);
            sr.status    = 0;
            rc = iscsiCommandSync(pImage, &sr, false /* fRetry */, VINF_SUCCESS);
            if (   RT_SUCCESS(rc)
                && (sr.status == SCSI_STATUS_OK))
                LogRel(("iSCSI: Enabled read and write cache of target %s\n", pImage->pszTargetName));
            else
            {
                /* Log failures but continue. */
                LogRel(("iSCSI: Could not enable read and write cache of target %s, rc=%Rrc status=%#x\n",
                        pImage->pszTargetName, rc, sr.status));
                LogRel(("iSCSI: Sense:\n%.*Rhxd\n", sr.cbSense, sr.abSense));
                rc = VINF_SUCCESS;
            }
        }
    }
    else
    {
        /* Log errors but continue. */
        LogRel(("iSCSI: Could not check write cache of target %s, rc=%Rrc, got mode page %#x\n", pImage->pszTargetName, rc, aCachingModePage[0] & 0x3f));
        LogRel(("iSCSI: Sense:\n%.*Rhxd\n", sr.cbSense, sr.abSense));
        rc = VINF_SUCCESS;
    }

    return rc;
}

/**
 * Internal: Open an image, constructing all necessary data structures.
 */
static int iscsiOpenImage(PISCSIIMAGE pImage, unsigned uOpenFlags)
{
    pImage->uOpenFlags = uOpenFlags;

    int rc = iscsiOpenImageInit(pImage);
    if (RT_SUCCESS(rc))
        rc = iscsiOpenImageParseCfg(pImage);

    if (RT_SUCCESS(rc))
    {
        /* Don't actually establish iSCSI transport connection if this is just an
         * open to query the image information and the host IP stack isn't used.
         * Even trying is rather useless, as in this context the InTnet IP stack
         * isn't present. Returning dummies is the best possible result anyway. */
        if ((uOpenFlags & VD_OPEN_FLAGS_INFO) && !pImage->fHostIP)
            LogFunc(("Not opening the transport connection as IntNet IP stack is not available. Will return dummies\n"));
        else
        {
            rc = iscsiOpenImageSocketCreate(pImage);
            if (RT_SUCCESS(rc))
            {
                    /*
                     * Attach to the iSCSI target. This implicitly establishes the iSCSI
                     * transport connection.
                     */
                rc = iscsiExecSync(pImage, iscsiAttach, pImage);
                if (RT_SUCCESS(rc))
                {
                    LogFlowFunc(("target '%s' opened successfully\n", pImage->pszTargetName));

                    rc = iscsiOpenImageReportLuns(pImage);
                    if (RT_SUCCESS(rc))
                        rc = iscsiOpenImageInquiry(pImage);
                    if (RT_SUCCESS(rc))
                        rc = iscsiOpenImageCheckWriteAccess(pImage);
                    if (RT_SUCCESS(rc))
                        rc = iscsiOpenImageQueryTargetSizes(pImage);
                    if (RT_SUCCESS(rc))
                        rc = iscsiOpenImageEnableReadWriteCache(pImage);
                }
                else
                    LogRel(("iSCSI: could not open target %s, rc=%Rrc\n", pImage->pszTargetName, rc));
            }
        }
    }

    if (RT_SUCCESS(rc))
    {
        PVDREGIONDESC pRegion = &pImage->RegionList.aRegions[0];
        pImage->RegionList.fFlags   = 0;
        pImage->RegionList.cRegions = 1;

        pRegion->offRegion            = 0; /* Disk start. */
        pRegion->cbBlock              = pImage->cbSector;
        pRegion->enmDataForm          = VDREGIONDATAFORM_RAW;
        pRegion->enmMetadataForm      = VDREGIONMETADATAFORM_NONE;
        pRegion->cbData               = pImage->cbSector;
        pRegion->cbMetadata           = 0;
        pRegion->cRegionBlocksOrBytes = pImage->cbSize;
    }
    else
        iscsiFreeImage(pImage, false);
    return rc;
}


/** @copydoc VDIMAGEBACKEND::pfnProbe */
static DECLCALLBACK(int) iscsiProbe(const char *pszFilename, PVDINTERFACE pVDIfsDisk,
                                    PVDINTERFACE pVDIfsImage, VDTYPE enmDesiredType, VDTYPE *penmType)
{
    RT_NOREF(pszFilename, pVDIfsDisk, pVDIfsImage, enmDesiredType, penmType);
    LogFlowFunc(("pszFilename=\"%s\"\n", pszFilename));

    /* iSCSI images can't be checked for validity this way, as the filename
     * just can't supply enough configuration information. */
    int rc = VERR_VD_ISCSI_INVALID_HEADER;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnOpen */
static DECLCALLBACK(int) iscsiOpen(const char *pszFilename, unsigned uOpenFlags,
                                   PVDINTERFACE pVDIfsDisk, PVDINTERFACE pVDIfsImage,
                                   VDTYPE enmType, void **ppBackendData)
{
    RT_NOREF1(enmType); /**< @todo r=klaus make use of the type info. */

    LogFlowFunc(("pszFilename=\"%s\" uOpenFlags=%#x pVDIfsDisk=%#p pVDIfsImage=%#p enmType=%u ppBackendData=%#p\n",
                 pszFilename, uOpenFlags, pVDIfsDisk, pVDIfsImage, enmType, ppBackendData));
    int rc;

    /* Check open flags. All valid flags are supported. */
    AssertReturn(!(uOpenFlags & ~VD_OPEN_FLAGS_MASK), VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename != '\0', VERR_INVALID_PARAMETER);

    PISCSIIMAGE pImage = (PISCSIIMAGE)RTMemAllocZ(RT_UOFFSETOF(ISCSIIMAGE, RegionList.aRegions[1]));
    if (RT_LIKELY(pImage))
    {
        pImage->pszFilename = pszFilename;
        pImage->pszInitiatorName = NULL;
        pImage->pszTargetName = NULL;
        pImage->pszTargetAddress = NULL;
        pImage->pszInitiatorUsername = NULL;
        pImage->pbInitiatorSecret = NULL;
        pImage->pszTargetUsername = NULL;
        pImage->pbTargetSecret = NULL;
        pImage->paCurrReq = NULL;
        pImage->pvRecvPDUBuf = NULL;
        pImage->pszHostname = NULL;
        pImage->pVDIfsDisk = pVDIfsDisk;
        pImage->pVDIfsImage = pVDIfsImage;
        pImage->cLogRelErrors = 0;

        rc = iscsiOpenImage(pImage, uOpenFlags);
        if (RT_SUCCESS(rc))
        {
            LogFlowFunc(("target %s cVolume %d, cbSector %d\n", pImage->pszTargetName, pImage->cVolume, pImage->cbSector));
            LogRel(("iSCSI: target address %s, target name %s, SCSI LUN %lld\n", pImage->pszTargetAddress, pImage->pszTargetName, pImage->LUN));
            *ppBackendData = pImage;
        }
        else
            RTMemFree(pImage);
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFunc(("returns %Rrc (pBackendData=%#p)\n", rc, *ppBackendData));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnCreate */
static DECLCALLBACK(int) iscsiCreate(const char *pszFilename, uint64_t cbSize,
                                     unsigned uImageFlags, const char *pszComment,
                                     PCVDGEOMETRY pPCHSGeometry, PCVDGEOMETRY pLCHSGeometry,
                                     PCRTUUID pUuid, unsigned uOpenFlags,
                                     unsigned uPercentStart, unsigned uPercentSpan,
                                     PVDINTERFACE pVDIfsDisk, PVDINTERFACE pVDIfsImage,
                                     PVDINTERFACE pVDIfsOperation, VDTYPE enmType,
                                     void **ppBackendData)
{
    RT_NOREF8(pszFilename, cbSize, uImageFlags, pszComment, pPCHSGeometry, pLCHSGeometry, pUuid, uOpenFlags);
    RT_NOREF7(uPercentStart, uPercentSpan, pVDIfsDisk, pVDIfsImage, pVDIfsOperation, enmType, ppBackendData);
    LogFlowFunc(("pszFilename=\"%s\" cbSize=%llu uImageFlags=%#x pszComment=\"%s\" pPCHSGeometry=%#p pLCHSGeometry=%#p Uuid=%RTuuid uOpenFlags=%#x uPercentStart=%u uPercentSpan=%u pVDIfsDisk=%#p pVDIfsImage=%#p pVDIfsOperation=%#p enmType=%u ppBackendData=%#p",
                 pszFilename, cbSize, uImageFlags, pszComment, pPCHSGeometry, pLCHSGeometry, pUuid, uOpenFlags, uPercentStart, uPercentSpan, pVDIfsDisk, pVDIfsImage, pVDIfsOperation, enmType, ppBackendData));
    int rc = VERR_NOT_SUPPORTED;

    LogFlowFunc(("returns %Rrc (pBackendData=%#p)\n", rc, *ppBackendData));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnClose */
static DECLCALLBACK(int) iscsiClose(void *pBackendData, bool fDelete)
{
    LogFlowFunc(("pBackendData=%#p fDelete=%d\n", pBackendData, fDelete));
    PISCSIIMAGE pImage = (PISCSIIMAGE)pBackendData;
    int rc;

    Assert(!fDelete);   /* This flag is unsupported. */

    rc = iscsiFreeImage(pImage, fDelete);
    RTMemFree(pImage);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnRead */
static DECLCALLBACK(int) iscsiRead(void *pBackendData, uint64_t uOffset, size_t cbToRead,
                                   PVDIOCTX pIoCtx, size_t *pcbActuallyRead)
{
    PISCSIIMAGE pImage = (PISCSIIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pBackendData=%p uOffset=%#llx pIoCtx=%#p cbToRead=%u pcbActuallyRead=%p\n",
                 pBackendData, uOffset, pIoCtx, cbToRead, pcbActuallyRead));

    if (   uOffset + cbToRead > pImage->cbSize
        || cbToRead == 0)
        return VERR_INVALID_PARAMETER;

    /*
     * Clip read size to a value which is supported by the target.
     */
    cbToRead = RT_MIN(cbToRead, pImage->cbRecvDataLength);

    unsigned cT2ISegs = 0;
    size_t   cbSegs = 0;

    /* Get the number of segments. */
    cbSegs = pImage->pIfIo->pfnIoCtxSegArrayCreate(pImage->pIfIo->Core.pvUser, pIoCtx,
                                                   NULL, &cT2ISegs, cbToRead);
    Assert(cbSegs == cbToRead);

    PSCSIREQ pReq = (PSCSIREQ)RTMemAllocZ(RT_UOFFSETOF_DYN(SCSIREQ, aSegs[cT2ISegs]));
    if (RT_LIKELY(pReq))
    {
        uint64_t lba;
        uint16_t tls;
        uint8_t *pbCDB = &pReq->abCDB[0];
        size_t cbCDB;

        lba = uOffset / pImage->cbSector;
        tls = (uint16_t)(cbToRead / pImage->cbSector);

        cbSegs = pImage->pIfIo->pfnIoCtxSegArrayCreate(pImage->pIfIo->Core.pvUser, pIoCtx,
                                                       &pReq->aSegs[0],
                                                       &cT2ISegs, cbToRead);
        Assert(cbSegs == cbToRead);

        if (pImage->cVolume < _4G)
        {
            cbCDB = 10;
            pbCDB[0] = SCSI_READ_10;
            pbCDB[1] = 0;       /* reserved */
            pbCDB[2] = (lba >> 24) & 0xff;
            pbCDB[3] = (lba >> 16) & 0xff;
            pbCDB[4] = (lba >> 8) & 0xff;
            pbCDB[5] = lba & 0xff;
            pbCDB[6] = 0;       /* reserved */
            pbCDB[7] = (tls >> 8) & 0xff;
            pbCDB[8] = tls & 0xff;
            pbCDB[9] = 0;       /* control */
        }
        else
        {
            cbCDB = 16;
            pbCDB[0] = SCSI_READ_16;
            pbCDB[1] = 0;       /* reserved */
            pbCDB[2] = (lba >> 56) & 0xff;
            pbCDB[3] = (lba >> 48) & 0xff;
            pbCDB[4] = (lba >> 40) & 0xff;
            pbCDB[5] = (lba >> 32) & 0xff;
            pbCDB[6] = (lba >> 24) & 0xff;
            pbCDB[7] = (lba >> 16) & 0xff;
            pbCDB[8] = (lba >> 8) & 0xff;
            pbCDB[9] = lba & 0xff;
            pbCDB[10] = 0;      /* tls unused */
            pbCDB[11] = 0;      /* tls unused */
            pbCDB[12] = (tls >> 8) & 0xff;
            pbCDB[13] = tls & 0xff;
            pbCDB[14] = 0;      /* reserved */
            pbCDB[15] = 0;      /* reserved */
        }

        pReq->enmXfer       = SCSIXFER_FROM_TARGET;
        pReq->cbCDB         = cbCDB;
        pReq->cbI2TData     = 0;
        pReq->paI2TSegs     = NULL;
        pReq->cI2TSegs      = 0;
        pReq->cbT2IData     = cbToRead;
        pReq->paT2ISegs     = &pReq->aSegs[pReq->cI2TSegs];
        pReq->cbSense       = sizeof(pReq->abSense);
        pReq->cT2ISegs      = cT2ISegs;
        pReq->pIoCtx        = pIoCtx;
        pReq->cSenseRetries = 10;
        pReq->rcSense       = VERR_READ_ERROR;

        if (vdIfIoIntIoCtxIsSynchronous(pImage->pIfIo, pIoCtx))
        {
            rc = iscsiCommandSync(pImage, pReq, true, VERR_READ_ERROR);
            if (RT_FAILURE(rc))
            {
                LogFlow(("iscsiCommandSync(%s, %#llx) -> %Rrc\n", pImage->pszTargetName, uOffset, rc));
                *pcbActuallyRead = 0;
            }
            else
                *pcbActuallyRead = pReq->cbT2IData;
        }
        else
        {
            rc = iscsiCommandAsync(pImage, pReq, iscsiCommandAsyncComplete, pReq);
            if (RT_FAILURE(rc))
                AssertMsgFailed(("iscsiCommandAsync(%s, %#llx) -> %Rrc\n", pImage->pszTargetName, uOffset, rc));
            else
            {
                *pcbActuallyRead = cbToRead;
                return VERR_VD_IOCTX_HALT; /* Halt the I/O context until further notification from the I/O thread. */
            }
        }

        RTMemFree(pReq);
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnWrite */
static DECLCALLBACK(int) iscsiWrite(void *pBackendData, uint64_t uOffset, size_t cbToWrite,
                                    PVDIOCTX pIoCtx, size_t *pcbWriteProcess, size_t *pcbPreRead,
                                    size_t *pcbPostRead, unsigned fWrite)
{
    RT_NOREF3(pcbPreRead, pcbPostRead, fWrite);
    LogFlowFunc(("pBackendData=%p uOffset=%llu pIoCtx=%#p cbToWrite=%u pcbWriteProcess=%p pcbPreRead=%p pcbPostRead=%p fWrite=%u\n",
                 pBackendData, uOffset, pIoCtx, cbToWrite, pcbWriteProcess, pcbPreRead, pcbPostRead, fWrite));
    PISCSIIMAGE pImage = (PISCSIIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    AssertPtr(pImage);
    Assert(uOffset % 512 == 0);
    Assert(cbToWrite % 512 == 0);

    if (uOffset + cbToWrite > pImage->cbSize)
        return VERR_INVALID_PARAMETER;

    /*
     * Clip read size to a value which is supported by the target.
     */
    cbToWrite = RT_MIN(cbToWrite, pImage->cbSendDataLength);

    unsigned cI2TSegs = 0;
    size_t   cbSegs = 0;

    /* Get the number of segments. */
    cbSegs = pImage->pIfIo->pfnIoCtxSegArrayCreate(pImage->pIfIo->Core.pvUser, pIoCtx,
                                                   NULL, &cI2TSegs, cbToWrite);
    Assert(cbSegs == cbToWrite);

    PSCSIREQ pReq = (PSCSIREQ)RTMemAllocZ(RT_UOFFSETOF_DYN(SCSIREQ, aSegs[cI2TSegs]));
    if (RT_LIKELY(pReq))
    {
        uint64_t lba;
        uint16_t tls;
        uint8_t *pbCDB = &pReq->abCDB[0];
        size_t cbCDB;

        lba = uOffset / pImage->cbSector;
        tls = (uint16_t)(cbToWrite / pImage->cbSector);

        cbSegs = pImage->pIfIo->pfnIoCtxSegArrayCreate(pImage->pIfIo->Core.pvUser, pIoCtx,
                                                       &pReq->aSegs[0],
                                                       &cI2TSegs, cbToWrite);
        Assert(cbSegs == cbToWrite);

        if (pImage->cVolume < _4G)
        {
            cbCDB = 10;
            pbCDB[0] = SCSI_WRITE_10;
            pbCDB[1] = 0;       /* reserved */
            pbCDB[2] = (lba >> 24) & 0xff;
            pbCDB[3] = (lba >> 16) & 0xff;
            pbCDB[4] = (lba >> 8) & 0xff;
            pbCDB[5] = lba & 0xff;
            pbCDB[6] = 0;       /* reserved */
            pbCDB[7] = (tls >> 8) & 0xff;
            pbCDB[8] = tls & 0xff;
            pbCDB[9] = 0;       /* control */
        }
        else
        {
            cbCDB = 16;
            pbCDB[0] = SCSI_WRITE_16;
            pbCDB[1] = 0;       /* reserved */
            pbCDB[2] = (lba >> 56) & 0xff;
            pbCDB[3] = (lba >> 48) & 0xff;
            pbCDB[4] = (lba >> 40) & 0xff;
            pbCDB[5] = (lba >> 32) & 0xff;
            pbCDB[6] = (lba >> 24) & 0xff;
            pbCDB[7] = (lba >> 16) & 0xff;
            pbCDB[8] = (lba >> 8) & 0xff;
            pbCDB[9] = lba & 0xff;
            pbCDB[10] = 0;      /* tls unused */
            pbCDB[11] = 0;      /* tls unused */
            pbCDB[12] = (tls >> 8) & 0xff;
            pbCDB[13] = tls & 0xff;
            pbCDB[14] = 0;      /* reserved */
            pbCDB[15] = 0;      /* reserved */
        }

        pReq->enmXfer       = SCSIXFER_TO_TARGET;
        pReq->cbCDB         = cbCDB;
        pReq->cbI2TData     = cbToWrite;
        pReq->paI2TSegs     = &pReq->aSegs[0];
        pReq->cI2TSegs      = cI2TSegs;
        pReq->cbT2IData     = 0;
        pReq->paT2ISegs     = NULL;
        pReq->cT2ISegs      = 0;
        pReq->cbSense       = sizeof(pReq->abSense);
        pReq->pIoCtx        = pIoCtx;
        pReq->cSenseRetries = 10;
        pReq->rcSense       = VERR_WRITE_ERROR;

        if (vdIfIoIntIoCtxIsSynchronous(pImage->pIfIo, pIoCtx))
        {
            rc = iscsiCommandSync(pImage, pReq, true, VERR_WRITE_ERROR);
            if (RT_FAILURE(rc))
            {
                LogFlow(("iscsiCommandSync(%s, %#llx) -> %Rrc\n", pImage->pszTargetName, uOffset, rc));
                *pcbWriteProcess = 0;
            }
            else
                *pcbWriteProcess = cbToWrite;
        }
        else
        {
            rc = iscsiCommandAsync(pImage, pReq, iscsiCommandAsyncComplete, pReq);
            if (RT_FAILURE(rc))
                AssertMsgFailed(("iscsiCommandAsync(%s, %#llx) -> %Rrc\n", pImage->pszTargetName, uOffset, rc));
            else
            {
                *pcbWriteProcess = cbToWrite;
                return VERR_VD_IOCTX_HALT; /* Halt the I/O context until further notification from the I/O thread. */
            }
        }

        RTMemFree(pReq);
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnFlush */
static DECLCALLBACK(int) iscsiFlush(void *pBackendData, PVDIOCTX pIoCtx)
{
    LogFlowFunc(("pBackendData=%p pIoCtx=%#p\n", pBackendData, pIoCtx));
    PISCSIIMAGE pImage = (PISCSIIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    PSCSIREQ pReq = (PSCSIREQ)RTMemAllocZ(sizeof(SCSIREQ));
    if (RT_LIKELY(pReq))
    {
        uint8_t *pbCDB = &pReq->abCDB[0];

        pbCDB[0] = SCSI_SYNCHRONIZE_CACHE;
        pbCDB[1] = 0;         /* reserved */
        pbCDB[2] = 0;         /* reserved */
        pbCDB[3] = 0;         /* reserved */
        pbCDB[4] = 0;         /* reserved */
        pbCDB[5] = 0;         /* reserved */
        pbCDB[6] = 0;         /* reserved */
        pbCDB[7] = 0;         /* reserved */
        pbCDB[8] = 0;         /* reserved */
        pbCDB[9] = 0;         /* control */

        pReq->enmXfer       = SCSIXFER_NONE;
        pReq->cbCDB         = 10;
        pReq->cbI2TData     = 0;
        pReq->paI2TSegs     = NULL;
        pReq->cI2TSegs      = 0;
        pReq->cbT2IData     = 0;
        pReq->paT2ISegs     = NULL;
        pReq->cT2ISegs      = 0;
        pReq->cbSense       = sizeof(pReq->abSense);
        pReq->pIoCtx        = pIoCtx;
        pReq->cSenseRetries = 0;
        pReq->rcSense       = VINF_SUCCESS;

        if (vdIfIoIntIoCtxIsSynchronous(pImage->pIfIo, pIoCtx))
        {
            rc = iscsiCommandSync(pImage, pReq, false, VINF_SUCCESS);
            if (RT_FAILURE(rc))
                AssertMsgFailed(("iscsiCommand(%s) -> %Rrc\n", pImage->pszTargetName, rc));
        }
        else
        {
            rc = iscsiCommandAsync(pImage, pReq, iscsiCommandAsyncComplete, pReq);
            if (RT_FAILURE(rc))
                AssertMsgFailed(("iscsiCommand(%s) -> %Rrc\n", pImage->pszTargetName, rc));
            else
                return VERR_VD_IOCTX_HALT; /* Halt the I/O context until further notification from the I/O thread. */
        }

        RTMemFree(pReq);
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetVersion */
static DECLCALLBACK(unsigned) iscsiGetVersion(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PISCSIIMAGE pImage = (PISCSIIMAGE)pBackendData;

    AssertPtr(pImage);
    RT_NOREF1(pImage);

    return 0;
}

/** @copydoc VDIMAGEBACKEND::pfnGetFileSize */
static DECLCALLBACK(uint64_t) iscsiGetFileSize(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PISCSIIMAGE pImage = (PISCSIIMAGE)pBackendData;

    AssertPtrReturn(pImage, 0);

    return pImage->cbSize;
}

/** @copydoc VDIMAGEBACKEND::pfnGetPCHSGeometry */
static DECLCALLBACK(int) iscsiGetPCHSGeometry(void *pBackendData, PVDGEOMETRY pPCHSGeometry)
{
    RT_NOREF1(pPCHSGeometry);
    LogFlowFunc(("pBackendData=%#p pPCHSGeometry=%#p\n", pBackendData, pPCHSGeometry));
    PISCSIIMAGE pImage = (PISCSIIMAGE)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    LogFlowFunc(("returns %Rrc (PCHS=%u/%u/%u)\n", VERR_VD_GEOMETRY_NOT_SET,
                 pPCHSGeometry->cCylinders, pPCHSGeometry->cHeads, pPCHSGeometry->cSectors));
    return VERR_VD_GEOMETRY_NOT_SET;
}

/** @copydoc VDIMAGEBACKEND::pfnSetPCHSGeometry */
static DECLCALLBACK(int) iscsiSetPCHSGeometry(void *pBackendData, PCVDGEOMETRY pPCHSGeometry)
{
    RT_NOREF1(pPCHSGeometry);
    LogFlowFunc(("pBackendData=%#p pPCHSGeometry=%#p PCHS=%u/%u/%u\n", pBackendData, pPCHSGeometry, pPCHSGeometry->cCylinders, pPCHSGeometry->cHeads, pPCHSGeometry->cSectors));
    PISCSIIMAGE pImage = (PISCSIIMAGE)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    int rc;
    if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
        rc = VERR_VD_IMAGE_READ_ONLY;
    else
        rc = VERR_VD_GEOMETRY_NOT_SET;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetLCHSGeometry */
static DECLCALLBACK(int) iscsiGetLCHSGeometry(void *pBackendData, PVDGEOMETRY pLCHSGeometry)
{
    RT_NOREF1(pLCHSGeometry);
    LogFlowFunc(("pBackendData=%#p pLCHSGeometry=%#p\n", pBackendData, pLCHSGeometry));
    PISCSIIMAGE pImage = (PISCSIIMAGE)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    LogFlowFunc(("returns %Rrc (LCHS=%u/%u/%u)\n", VERR_VD_GEOMETRY_NOT_SET,
                 pLCHSGeometry->cCylinders, pLCHSGeometry->cHeads, pLCHSGeometry->cSectors));
    return VERR_VD_GEOMETRY_NOT_SET;
}

/** @copydoc VDIMAGEBACKEND::pfnSetLCHSGeometry */
static DECLCALLBACK(int) iscsiSetLCHSGeometry(void *pBackendData, PCVDGEOMETRY pLCHSGeometry)
{
    RT_NOREF1(pLCHSGeometry);
    LogFlowFunc(("pBackendData=%#p pLCHSGeometry=%#p LCHS=%u/%u/%u\n", pBackendData, pLCHSGeometry, pLCHSGeometry->cCylinders, pLCHSGeometry->cHeads, pLCHSGeometry->cSectors));
    PISCSIIMAGE pImage = (PISCSIIMAGE)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    int rc;
    if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
        rc = VERR_VD_IMAGE_READ_ONLY;
    else
        rc = VERR_VD_GEOMETRY_NOT_SET;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnQueryRegions */
static DECLCALLBACK(int) iscsiQueryRegions(void *pBackendData, PCVDREGIONLIST *ppRegionList)
{
    LogFlowFunc(("pBackendData=%#p ppRegionList=%#p\n", pBackendData, ppRegionList));
    PISCSIIMAGE pImage = (PISCSIIMAGE)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    *ppRegionList = &pImage->RegionList;
    LogFlowFunc(("returns %Rrc\n", VINF_SUCCESS));
    return VINF_SUCCESS;
}

/** @copydoc VDIMAGEBACKEND::pfnRegionListRelease */
static DECLCALLBACK(void) iscsiRegionListRelease(void *pBackendData, PCVDREGIONLIST pRegionList)
{
    RT_NOREF1(pRegionList);
    LogFlowFunc(("pBackendData=%#p pRegionList=%#p\n", pBackendData, pRegionList));
    PISCSIIMAGE pImage = (PISCSIIMAGE)pBackendData;
    AssertPtr(pImage); RT_NOREF(pImage);

    /* Nothing to do here. */
}

/** @copydoc VDIMAGEBACKEND::pfnGetImageFlags */
static DECLCALLBACK(unsigned) iscsiGetImageFlags(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PISCSIIMAGE pImage = (PISCSIIMAGE)pBackendData;

    AssertPtrReturn(pImage, 0);

    LogFlowFunc(("returns %#x\n", VD_IMAGE_FLAGS_FIXED));
    return VD_IMAGE_FLAGS_FIXED;
}

/** @copydoc VDIMAGEBACKEND::pfnGetOpenFlags */
static DECLCALLBACK(unsigned) iscsiGetOpenFlags(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PISCSIIMAGE pImage = (PISCSIIMAGE)pBackendData;

    AssertPtrReturn(pImage, 0);

    LogFlowFunc(("returns %#x\n", pImage->uOpenFlags));
    return pImage->uOpenFlags;
}

/** @copydoc VDIMAGEBACKEND::pfnSetOpenFlags */
static DECLCALLBACK(int) iscsiSetOpenFlags(void *pBackendData, unsigned uOpenFlags)
{
    LogFlowFunc(("pBackendData=%#p uOpenFlags=%#x\n", pBackendData, uOpenFlags));
    PISCSIIMAGE pImage = (PISCSIIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    /* Image must be opened and the new flags must be valid. */
    AssertReturn(pImage && !(uOpenFlags & ~(  VD_OPEN_FLAGS_READONLY | VD_OPEN_FLAGS_INFO
                                            | VD_OPEN_FLAGS_ASYNC_IO | VD_OPEN_FLAGS_SHAREABLE
                                            | VD_OPEN_FLAGS_SEQUENTIAL | VD_OPEN_FLAGS_SKIP_CONSISTENCY_CHECKS)),
                 VERR_INVALID_PARAMETER);

    /*
     * A read/write -> readonly transition is always possible,
     * for the reverse direction check that the target didn't present itself
     * as readonly during the first attach.
     */
    if (   !(uOpenFlags & VD_OPEN_FLAGS_READONLY)
        && (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
        && pImage->fTargetReadOnly)
        rc = VERR_VD_IMAGE_READ_ONLY;
    else
    {
        pImage->uOpenFlags = uOpenFlags;
        pImage->fTryReconnect = true;
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetComment */
VD_BACKEND_CALLBACK_GET_COMMENT_DEF_NOT_SUPPORTED(iscsiGetComment);

/** @copydoc VDIMAGEBACKEND::pfnSetComment */
VD_BACKEND_CALLBACK_SET_COMMENT_DEF_NOT_SUPPORTED(iscsiSetComment, PISCSIIMAGE);

/** @copydoc VDIMAGEBACKEND::pfnGetUuid */
VD_BACKEND_CALLBACK_GET_UUID_DEF_NOT_SUPPORTED(iscsiGetUuid);

/** @copydoc VDIMAGEBACKEND::pfnSetUuid */
VD_BACKEND_CALLBACK_SET_UUID_DEF_NOT_SUPPORTED(iscsiSetUuid, PISCSIIMAGE);

/** @copydoc VDIMAGEBACKEND::pfnGetModificationUuid */
VD_BACKEND_CALLBACK_GET_UUID_DEF_NOT_SUPPORTED(iscsiGetModificationUuid);

/** @copydoc VDIMAGEBACKEND::pfnSetModificationUuid */
VD_BACKEND_CALLBACK_SET_UUID_DEF_NOT_SUPPORTED(iscsiSetModificationUuid, PISCSIIMAGE);

/** @copydoc VDIMAGEBACKEND::pfnGetParentUuid */
VD_BACKEND_CALLBACK_GET_UUID_DEF_NOT_SUPPORTED(iscsiGetParentUuid);

/** @copydoc VDIMAGEBACKEND::pfnSetParentUuid */
VD_BACKEND_CALLBACK_SET_UUID_DEF_NOT_SUPPORTED(iscsiSetParentUuid, PISCSIIMAGE);

/** @copydoc VDIMAGEBACKEND::pfnGetParentModificationUuid */
VD_BACKEND_CALLBACK_GET_UUID_DEF_NOT_SUPPORTED(iscsiGetParentModificationUuid);

/** @copydoc VDIMAGEBACKEND::pfnSetParentModificationUuid */
VD_BACKEND_CALLBACK_SET_UUID_DEF_NOT_SUPPORTED(iscsiSetParentModificationUuid, PISCSIIMAGE);

/** @copydoc VDIMAGEBACKEND::pfnDump */
static DECLCALLBACK(void) iscsiDump(void *pBackendData)
{
    PISCSIIMAGE pImage = (PISCSIIMAGE)pBackendData;

    AssertPtrReturnVoid(pImage);
    /** @todo put something useful here */
    vdIfErrorMessage(pImage->pIfError, "Header: cVolume=%u\n", pImage->cVolume);
}

/** @copydoc VDIMAGEBACKEND::pfnComposeLocation */
static DECLCALLBACK(int) iscsiComposeLocation(PVDINTERFACE pConfig, char **pszLocation)
{
    char *pszTarget  = NULL;
    char *pszLUN     = NULL;
    char *pszAddress = NULL;

    int rc;
    PVDINTERFACECONFIG pIfCfg = VDIfConfigGet(pConfig);
    if (pIfCfg)
    {
        rc = VDCFGQueryStringAlloc(pIfCfg, "TargetName", &pszTarget);
        if (RT_SUCCESS(rc))
        {
            rc = VDCFGQueryStringAlloc(pIfCfg, "LUN", &pszLUN);
            if (RT_SUCCESS(rc))
            {
                rc = VDCFGQueryStringAlloc(pIfCfg, "TargetAddress", &pszAddress);
                if (RT_SUCCESS(rc))
                {
                    if (RTStrAPrintf(pszLocation, "iscsi://%s/%s/%s",
                                     pszAddress, pszTarget, pszLUN) < 0)
                        rc = VERR_NO_MEMORY;

                    RTMemFree(pszAddress);
                }
                RTMemFree(pszLUN);
            }
            RTMemFree(pszTarget);
        }
    }
    else
        rc = VERR_INVALID_PARAMETER;

    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnComposeName */
static DECLCALLBACK(int) iscsiComposeName(PVDINTERFACE pConfig, char **pszName)
{
    char *pszTarget  = NULL;
    char *pszLUN     = NULL;
    char *pszAddress = NULL;

    int rc;
    PVDINTERFACECONFIG pIfCfg = VDIfConfigGet(pConfig);
    if (pIfCfg)
    {
        rc = VDCFGQueryStringAlloc(pIfCfg, "TargetName", &pszTarget);
        if (RT_SUCCESS(rc))
        {
            rc = VDCFGQueryStringAlloc(pIfCfg, "LUN", &pszLUN);
            if (RT_SUCCESS(rc))
            {
                rc = VDCFGQueryStringAlloc(pIfCfg, "TargetAddress", &pszAddress);
                if (RT_SUCCESS(rc))
                {
                    /** @todo think about a nicer looking location scheme for iSCSI */
                    if (RTStrAPrintf(pszName, "%s/%s/%s",
                                     pszAddress, pszTarget, pszLUN) < 0)
                        rc = VERR_NO_MEMORY;

                    RTMemFree(pszAddress);
                }
                RTMemFree(pszLUN);
            }
            RTMemFree(pszTarget);
        }
    }
    else
        rc = VERR_INVALID_PARAMETER;

    return rc;
}


const VDIMAGEBACKEND g_ISCSIBackend =
{
    /* u32Version */
    VD_IMGBACKEND_VERSION,
    /* pszBackendName */
    "iSCSI",
    /* uBackendCaps */
    VD_CAP_CONFIG | VD_CAP_TCPNET | VD_CAP_ASYNC,
    /* papszFileExtensions */
    NULL,
    /* paConfigInfo */
    s_iscsiConfigInfo,
    /* prnProbe */
    iscsiProbe,
    /* pfnOpen */
    iscsiOpen,
    /* pfnCreate */
    iscsiCreate,
    /* pfnRename */
    NULL,
    /* pfnClose */
    iscsiClose,
    /* pfnRead */
    iscsiRead,
    /* pfnWrite */
    iscsiWrite,
    /* pfnFlush */
    iscsiFlush,
    /* pfnDiscard */
    NULL,
    /* pfnGetVersion */
    iscsiGetVersion,
    /* pfnGetFileSize */
    iscsiGetFileSize,
    /* pfnGetPCHSGeometry */
    iscsiGetPCHSGeometry,
    /* pfnSetPCHSGeometry */
    iscsiSetPCHSGeometry,
    /* pfnGetLCHSGeometry */
    iscsiGetLCHSGeometry,
    /* pfnSetLCHSGeometry */
    iscsiSetLCHSGeometry,
    /* pfnQueryRegions */
    iscsiQueryRegions,
    /* pfnRegionListRelease */
    iscsiRegionListRelease,
    /* pfnGetImageFlags */
    iscsiGetImageFlags,
    /* pfnGetOpenFlags */
    iscsiGetOpenFlags,
    /* pfnSetOpenFlags */
    iscsiSetOpenFlags,
    /* pfnGetComment */
    iscsiGetComment,
    /* pfnSetComment */
    iscsiSetComment,
    /* pfnGetUuid */
    iscsiGetUuid,
    /* pfnSetUuid */
    iscsiSetUuid,
    /* pfnGetModificationUuid */
    iscsiGetModificationUuid,
    /* pfnSetModificationUuid */
    iscsiSetModificationUuid,
    /* pfnGetParentUuid */
    iscsiGetParentUuid,
    /* pfnSetParentUuid */
    iscsiSetParentUuid,
    /* pfnGetParentModificationUuid */
    iscsiGetParentModificationUuid,
    /* pfnSetParentModificationUuid */
    iscsiSetParentModificationUuid,
    /* pfnDump */
    iscsiDump,
    /* pfnGetTimestamp */
    NULL,
    /* pfnGetParentTimestamp */
    NULL,
    /* pfnSetParentTimestamp */
    NULL,
    /* pfnGetParentFilename */
    NULL,
    /* pfnSetParentFilename */
    NULL,
    /* pfnComposeLocation */
    iscsiComposeLocation,
    /* pfnComposeName */
    iscsiComposeName,
    /* pfnCompact */
    NULL,
    /* pfnResize */
    NULL,
    /* pfnRepair */
    NULL,
    /* pfnTraverseMetadata */
    NULL,
    /* u32VersionEnd */
    VD_IMGBACKEND_VERSION
};
