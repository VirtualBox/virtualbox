/* $Id: DevPS2.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PS/2 devices - Internal header file.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_Input_DevPS2_h
#define VBOX_INCLUDED_SRC_Input_DevPS2_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/** @defgroup grp_devps2    PS/2 Device
 * @{
 */

/** Pointer to the shared keyboard (PS/2) controller / device state. */
typedef struct KBDSTATE *PKBDSTATE;


/** @name PS/2 Input Queue Primitive
 * @{ */
typedef struct PS2QHDR
{
    uint32_t volatile       rpos;
    uint32_t volatile       wpos;
    uint32_t volatile       cUsed;
    uint32_t                uPadding;
    R3PTRTYPE(const char *) pszDescR3;
} PS2QHDR;
/** Pointer to a queue header. */
typedef PS2QHDR *PPS2QHDR;

/** Define a simple PS/2 input device queue. */
#define DEF_PS2Q_TYPE(name, size) \
     typedef struct { \
        PS2QHDR     Hdr; \
        uint8_t     abQueue[size];  \
     } name

void PS2CmnClearQueue(PPS2QHDR pQHdr, size_t cElements);
void PS2CmnInsertQueue(PPS2QHDR pQHdr, size_t cElements, uint8_t *pbElements, uint8_t bValue);
int  PS2CmnRemoveQueue(PPS2QHDR pQHdr, size_t cElements, uint8_t const *pbElements, uint8_t *pbValue);
void PS2CmnR3SaveQueue(PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM, PPS2QHDR pQHdr, size_t cElements, uint8_t const *pbElements);
int  PS2CmnR3LoadQueue(PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM, PPS2QHDR pQHdr, size_t cElements, uint8_t *pbElements);

#define PS2Q_CLEAR(a_pQueue) \
    PS2CmnClearQueue(&(a_pQueue)->Hdr, RT_ELEMENTS((a_pQueue)->abQueue))
#define PS2Q_INSERT(a_pQueue, a_bValue) \
    PS2CmnInsertQueue(&(a_pQueue)->Hdr, RT_ELEMENTS((a_pQueue)->abQueue), (a_pQueue)->abQueue, (a_bValue))
#define PS2Q_REMOVE(a_pQueue, a_pbValue) \
    PS2CmnRemoveQueue(&(a_pQueue)->Hdr, RT_ELEMENTS((a_pQueue)->abQueue), (a_pQueue)->abQueue, (a_pbValue))
#define PS2Q_SAVE(a_pHlp, a_pSSM, a_pQueue) \
    PS2CmnR3SaveQueue((a_pHlp), (a_pSSM), &(a_pQueue)->Hdr, RT_ELEMENTS((a_pQueue)->abQueue), (a_pQueue)->abQueue)
#define PS2Q_LOAD(a_pHlp, a_pSSM, a_pQueue) \
    PS2CmnR3LoadQueue((a_pHlp), (a_pSSM), &(a_pQueue)->Hdr, RT_ELEMENTS((a_pQueue)->abQueue), (a_pQueue)->abQueue)
#define PS2Q_SIZE(a_pQueue)     RT_ELEMENTS((a_pQueue)->abQueue)
#define PS2Q_COUNT(a_pQueue)    ((a_pQueue)->Hdr.cUsed)
#define PS2Q_RD_POS(a_pQueue)   ((a_pQueue)->Hdr.rpos)
#define PS2Q_WR_POS(a_pQueue)   ((a_pQueue)->Hdr.wpos)
/** @} */


/** @defgroup grp_devps2k   DevPS2K - Keyboard
 * @{
 */

/** @name HID modifier range.
 * @{ */
#define HID_MODIFIER_FIRST  0xE0
#define HID_MODIFIER_LAST   0xE8
/** @} */

/** @name USB HID additional constants
 * @{ */
/** The highest USB usage code reported by VirtualBox. */
#define VBOX_USB_MAX_USAGE_CODE     0xE7
/** The size of an array needed to store all USB usage codes */
#define VBOX_USB_USAGE_ARRAY_SIZE   (VBOX_USB_MAX_USAGE_CODE + 1)
/** @} */

/* Internal keyboard queue sizes. The input queue doesn't need to be
 * extra huge and the command queue only needs to handle a few bytes.
 */
#define KBD_KEY_QUEUE_SIZE         64
#define KBD_CMD_QUEUE_SIZE          4

DEF_PS2Q_TYPE(KbdKeyQ, KBD_KEY_QUEUE_SIZE);
DEF_PS2Q_TYPE(KbdCmdQ, KBD_CMD_QUEUE_SIZE);

/** Typematic state. */
typedef enum {
    KBD_TMS_IDLE    = 0,    /* No typematic key active. */
    KBD_TMS_DELAY   = 1,    /* In the initial delay period. */
    KBD_TMS_REPEAT  = 2,    /* Key repeating at set rate. */
    KBD_TMS_32BIT_HACK = 0x7fffffff
} tmatic_state_t;


/**
 * The shared PS/2 keyboard instance data.
 */
typedef struct PS2K
{
    /** Set if keyboard is enabled ('scans' for input). */
    bool                fScanning;
    /** Set NumLock is on. */
    bool                fNumLockOn;
    /** Selected scan set. */
    uint8_t             u8ScanSet;
    /** Modifier key state. */
    uint8_t             u8Modifiers;
    /** Currently processed command (if any). */
    uint8_t             u8CurrCmd;
    /** Status indicator (LED) state. */
    uint8_t             u8LEDs;
    /** Selected typematic delay/rate. */
    uint8_t             u8TypematicCfg;
    uint8_t             bAlignment1;
    /** Usage code of current typematic key, if any. */
    uint32_t            u32TypematicKey;
    /** Current typematic repeat state. */
    tmatic_state_t      enmTypematicState;
    /** Buffer holding scan codes to be sent to the host. */
    KbdKeyQ             keyQ;
    /** Command response queue (priority). */
    KbdCmdQ             cmdQ;
    /** Currently depressed keys. */
    uint8_t             abDepressedKeys[VBOX_USB_USAGE_ARRAY_SIZE];
    /** Typematic delay in milliseconds. */
    uint32_t            uTypematicDelay;
    /** Typematic repeat period in milliseconds. */
    uint32_t            uTypematicRepeat;
    /** Set if the throttle delay is currently active. */
    bool                fThrottleActive;
    /** Set if the input rate should be throttled. */
    bool                fThrottleEnabled;
    /** Set if the serial line is disabled on the KBC. */
    bool                fLineDisabled;
    uint8_t             abAlignment2[1];

    /** Command delay timer. */
    TMTIMERHANDLE       hKbdDelayTimer;
    /** Typematic timer. */
    TMTIMERHANDLE       hKbdTypematicTimer;
    /** Input throttle timer. */
    TMTIMERHANDLE       hThrottleTimer;
} PS2K;
/** Pointer to the shared PS/2 keyboard instance data. */
typedef PS2K *PPS2K;


/**
 * The PS/2 keyboard instance data for ring-3.
 */
typedef struct PS2KR3
{
    /** The device instance.
     * @note Only for getting our bearings in interface methods. */
    PPDMDEVINSR3        pDevIns;

    /**
     * Keyboard port - LUN#0.
     *
     * @implements  PDMIBASE
     * @implements  PDMIKEYBOARDPORT
     */
    struct
    {
        /** The base interface for the keyboard port. */
        PDMIBASE                            IBase;
        /** The keyboard port base interface. */
        PDMIKEYBOARDPORT                    IPort;

        /** The base interface of the attached keyboard driver. */
        R3PTRTYPE(PPDMIBASE)                pDrvBase;
        /** The keyboard interface of the attached keyboard driver. */
        R3PTRTYPE(PPDMIKEYBOARDCONNECTOR)   pDrv;
    } Keyboard;
} PS2KR3;
/** Pointer to the PS/2 keyboard instance data for ring-3. */
typedef PS2KR3 *PPS2KR3;


int  PS2KByteToKbd(PPDMDEVINS pDevIns, PPS2K pThis, uint8_t cmd);
int  PS2KByteFromKbd(PPDMDEVINS pDevIns, PPS2K pThis, uint8_t *pVal);

void PS2KLineDisable(PPS2K pThis);
void PS2KLineEnable(PPS2K pThis);

int  PS2KR3Construct(PPDMDEVINS pDevIns, PPS2K pThis, PPS2KR3 pThisCC, PCFGMNODE pCfg);
int  PS2KR3Attach(PPDMDEVINS pDevIns, PPS2KR3 pThisCC, unsigned iLUN, uint32_t fFlags);
void PS2KR3Reset(PPDMDEVINS pDevIns, PPS2K pThis, PPS2KR3 pThisCC);
void PS2KR3SaveState(PPDMDEVINS pDevIns, PPS2K pThis, PSSMHANDLE pSSM);
int  PS2KR3LoadState(PPDMDEVINS pDevIns, PPS2K pThis, PSSMHANDLE pSSM, uint32_t uVersion);
int  PS2KR3LoadDone(PPDMDEVINS pDevIns, PPS2K pThis, PPS2KR3 pThisCC);
/** @} */


/** @defgroup grp_devps2m DevPS2M - Auxiliary Device (Mouse)
 * @{
 */

/* Internal mouse queue sizes. The input queue is relatively large,
 * but the command queue only needs to handle a few bytes.
 */
#define AUX_EVT_QUEUE_SIZE        256
#define AUX_CMD_QUEUE_SIZE          8

DEF_PS2Q_TYPE(AuxEvtQ, AUX_EVT_QUEUE_SIZE);
DEF_PS2Q_TYPE(AuxCmdQ, AUX_CMD_QUEUE_SIZE);

/** Auxiliary device special modes of operation. */
typedef enum {
    AUX_MODE_STD,           /* Standard operation. */
    AUX_MODE_RESET,         /* Currently in reset. */
    AUX_MODE_WRAP           /* Wrap mode (echoing input). */
} PS2M_MODE;

/** Auxiliary device operational state. */
typedef enum {
    AUX_STATE_RATE_ERR  = RT_BIT(0),    /* Invalid rate received. */
    AUX_STATE_RES_ERR   = RT_BIT(1),    /* Invalid resolution received. */
    AUX_STATE_SCALING   = RT_BIT(4),    /* 2:1 scaling in effect. */
    AUX_STATE_ENABLED   = RT_BIT(5),    /* Reporting enabled in stream mode. */
    AUX_STATE_REMOTE    = RT_BIT(6)     /* Remote mode (reports on request). */
} PS2M_STATE;

/** Externally visible state bits. */
#define AUX_STATE_EXTERNAL  (AUX_STATE_SCALING | AUX_STATE_ENABLED | AUX_STATE_REMOTE)

/** Protocols supported by the PS/2 mouse. */
typedef enum {
    PS2M_PROTO_PS2STD      = 0,  /* Standard PS/2 mouse protocol. */
    PS2M_PROTO_IMPS2       = 3,  /* IntelliMouse PS/2 protocol. */
    PS2M_PROTO_IMEX        = 4,  /* IntelliMouse Explorer protocol. */
    PS2M_PROTO_IMEX_HORZ   = 5   /* IntelliMouse Explorer with horizontal reports. */
} PS2M_PROTO;

/** Protocol selection 'knock' states. */
typedef enum {
    PS2M_KNOCK_INITIAL,
    PS2M_KNOCK_1ST,
    PS2M_KNOCK_IMPS2_2ND,
    PS2M_KNOCK_IMEX_2ND,
    PS2M_KNOCK_IMEX_HORZ_2ND
} PS2M_KNOCK_STATE;

/**
 * The shared PS/2 auxiliary device instance data.
 */
typedef struct PS2M
{
    /** Operational state. */
    uint8_t             u8State;
    /** Configured sampling rate. */
    uint8_t             u8SampleRate;
    /** Configured resolution. */
    uint8_t             u8Resolution;
    /** Currently processed command (if any). */
    uint8_t             u8CurrCmd;
    /** Set if the serial line is disabled on the KBC. */
    bool                fLineDisabled;
    /** Set if the throttle delay is active. */
    bool                fThrottleActive;
    /** Set if the throttle delay is active. */
    bool                fDelayReset;
    /** Operational mode. */
    PS2M_MODE           enmMode;
    /** Currently used protocol. */
    PS2M_PROTO          enmProtocol;
    /** Currently used protocol. */
    PS2M_KNOCK_STATE    enmKnockState;
    /** Buffer holding mouse events to be sent to the host. */
    AuxEvtQ             evtQ;
    /** Command response queue (priority). */
    AuxCmdQ             cmdQ;
    /** Accumulated horizontal movement. */
    int32_t             iAccumX;
    /** Accumulated vertical movement. */
    int32_t             iAccumY;
    /** Accumulated Z axis (vertical scroll) movement. */
    int32_t             iAccumZ;
    /** Accumulated W axis (horizontal scroll) movement. */
    int32_t             iAccumW;
    /** Accumulated button presses. */
    uint32_t            fAccumB;
    /** Instantaneous button data. */
    uint32_t            fCurrB;
    /** Button state last sent to the guest. */
    uint32_t            fReportedB;
    /** Throttling delay in milliseconds. */
    uint32_t            uThrottleDelay;

    /** Command delay timer. */
    TMTIMERHANDLE       hDelayTimer;
    /** Interrupt throttling timer. */
    TMTIMERHANDLE       hThrottleTimer;
} PS2M;
/** Pointer to the shared PS/2 auxiliary device instance data. */
typedef PS2M *PPS2M;

/**
 * The PS/2 auxiliary device instance data for ring-3.
 */
typedef struct PS2MR3
{
    /** The device instance.
     * @note Only for getting our bearings in interface methods. */
    PPDMDEVINSR3        pDevIns;

    /**
     * Mouse port - LUN#1.
     *
     * @implements  PDMIBASE
     * @implements  PDMIMOUSEPORT
     */
    struct
    {
        /** The base interface for the mouse port. */
        PDMIBASE                            IBase;
        /** The keyboard port base interface. */
        PDMIMOUSEPORT                       IPort;

        /** The base interface of the attached mouse driver. */
        R3PTRTYPE(PPDMIBASE)                pDrvBase;
        /** The keyboard interface of the attached mouse driver. */
        R3PTRTYPE(PPDMIMOUSECONNECTOR)      pDrv;
    } Mouse;
} PS2MR3;
/** Pointer to the PS/2 auxiliary device instance data for ring-3. */
typedef PS2MR3 *PPS2MR3;

int  PS2MByteToAux(PPDMDEVINS pDevIns, PPS2M pThis, uint8_t cmd);
int  PS2MByteFromAux(PPS2M pThis, uint8_t *pVal);

void PS2MLineDisable(PPS2M pThis);
void PS2MLineEnable(PPS2M pThis);

int  PS2MR3Construct(PPDMDEVINS pDevIns, PPS2M pThis, PPS2MR3 pThisCC);
int  PS2MR3Attach(PPDMDEVINS pDevIns, PPS2MR3 pThisCC, unsigned iLUN, uint32_t fFlags);
void PS2MR3Reset(PPS2M pThis);
void PS2MR3SaveState(PPDMDEVINS pDevIns, PPS2M pThis, PSSMHANDLE pSSM);
int  PS2MR3LoadState(PPDMDEVINS pDevIns, PPS2M pThis, PPS2MR3 pThisCC, PSSMHANDLE pSSM, uint32_t uVersion);
void PS2MR3FixupState(PPS2M pThis, PPS2MR3 pThisCC, uint8_t u8State, uint8_t u8Rate, uint8_t u8Proto);
/** @} */


/**
 * The shared keyboard controller/device state.
 *
 * @note We use the default critical section for serialize data access.
 */
typedef struct KBDSTATE
{
    uint8_t write_cmd; /* if non zero, write data to port 60 is expected */
    uint8_t status;
    uint8_t mode;
    uint8_t dbbout;    /* data buffer byte */
    /* keyboard state */
    int32_t translate;
    int32_t xlat_state;

    /** I/O port 60h. */
    IOMIOPORTHANDLE     hIoPortData;
    /** I/O port 64h. */
    IOMIOPORTHANDLE     hIoPortCmdStatus;

    /** Shared keyboard state (implemented in separate PS2K module). */
    PS2K                Kbd;
    /** Shared mouse state (implemented in separate PS2M module). */
    PS2M                Aux;
} KBDSTATE;


/**
 * The ring-3 keyboard controller/device state.
 */
typedef struct KBDSTATER3
{
    /** Keyboard state for ring-3 (implemented in separate PS2K module). */
    PS2KR3  Kbd;
    /** Mouse state for ring-3 (implemented in separate PS2M module). */
    PS2MR3  Aux;
} KBDSTATER3;
/** Pointer to the keyboard (PS/2) controller / device state for ring-3. */
typedef KBDSTATER3 *PKBDSTATER3;


/* Shared keyboard/aux internal interface. */
void KBCUpdateInterrupts(PPDMDEVINS pDevIns);

/** @}  */

#endif /* !VBOX_INCLUDED_SRC_Input_DevPS2_h */

