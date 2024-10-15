/* $Id: VBoxPS2NT.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox NT4 Mouse Driver
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_DRV_MOUSE
#include <iprt/asm.h>
#include <iprt/initterm.h>
#include <iprt/errcore.h>
#include <VBox/log.h>

#include <stdarg.h>
#include <string.h>
#undef PAGE_SIZE
#undef PAGE_SHIFT
#include <iprt/nt/ntddk.h>
RT_C_DECLS_BEGIN
#include <ntddkbd.h>
#include <ntddmou.h>
RT_C_DECLS_END

#include <VBox/VMMDev.h>
#include <VBox/VBoxGuestLib.h>

/* not available on NT4 */
#undef ExFreePool
#undef ExAllocatePool

/* KeQueryTickCount is a macro accessing KeTickCount data export from NT 3.50+. */
#if 0 //def TARGET_NT3
# undef KeQueryTickCount
extern "C" NTKERNELAPI VOID NTAPI KeQueryTickCount(PLARGE_INTEGER);
#endif

/* i8042 mouse status bits */
#define LEFT_BUTTON_DOWN                        0x01
#define RIGHT_BUTTON_DOWN                       0x02
#define MIDDLE_BUTTON_DOWN                      0x04
#define X_DATA_SIGN                             0x10
#define Y_DATA_SIGN                             0x20
#define X_OVERFLOW                              0x40
#define Y_OVERFLOW                              0x80

#define MOUSE_SIGN_OVERFLOW_MASK (X_DATA_SIGN | Y_DATA_SIGN | X_OVERFLOW | Y_OVERFLOW)

#define MOUSE_MAXIMUM_POSITIVE_DELTA            0x000000FF
#define MOUSE_MAXIMUM_NEGATIVE_DELTA            0xFFFFFF00

#define KEYBOARD_HARDWARE_PRESENT               0x01
#define MOUSE_HARDWARE_PRESENT                  0x02
#define BALLPOINT_HARDWARE_PRESENT              0x04
#define WHEELMOUSE_HARDWARE_PRESENT             0x08

#define I8X_PUT_COMMAND_BYTE(Address, Byte)     WRITE_PORT_UCHAR(Address, (UCHAR) Byte)
#define I8X_PUT_DATA_BYTE(Address, Byte)        WRITE_PORT_UCHAR(Address, (UCHAR) Byte)
#define I8X_GET_STATUS_BYTE(Address)            READ_PORT_UCHAR(Address)
#define I8X_GET_DATA_BYTE(Address)              READ_PORT_UCHAR(Address)

/* commands to the i8042 controller */
#define I8042_READ_CONTROLLER_COMMAND_BYTE      0x20
#define I8042_WRITE_CONTROLLER_COMMAND_BYTE     0x60
#define I8042_DISABLE_MOUSE_DEVICE              0xA7
#define I8042_ENABLE_MOUSE_DEVICE               0xA8
#define I8042_AUXILIARY_DEVICE_TEST             0xA9
#define I8042_KEYBOARD_DEVICE_TEST              0xAB
#define I8042_DISABLE_KEYBOARD_DEVICE           0xAD
#define I8042_ENABLE_KEYBOARD_DEVICE            0xAE
#define I8042_WRITE_TO_AUXILIARY_DEVICE         0xD4

/* i8042 Controller Command Byte */
#define CCB_ENABLE_KEYBOARD_INTERRUPT           0x01
#define CCB_ENABLE_MOUSE_INTERRUPT              0x02
#define CCB_DISABLE_KEYBOARD_DEVICE             0x10
#define CCB_DISABLE_MOUSE_DEVICE                0x20
#define CCB_KEYBOARD_TRANSLATE_MODE             0x40

/* i8042 Controller Status Register bits */
#define OUTPUT_BUFFER_FULL                      0x01
#define INPUT_BUFFER_FULL                       0x02
#define MOUSE_OUTPUT_BUFFER_FULL                0x20

/* i8042 responses */
#define ACKNOWLEDGE                             0xFA
#define RESEND                                  0xFE

/* commands to the keyboard (through the 8042 data port) */
#define SET_KEYBOARD_INDICATORS                 0xED
#define SELECT_SCAN_CODE_SET                    0xF0
#define READ_KEYBOARD_ID                        0xF2
#define SET_KEYBOARD_TYPEMATIC                  0xF3
#define SET_ALL_TYPEMATIC_MAKE_BREAK            0xFA
#define KEYBOARD_RESET                          0xFF

/* commands to the mouse (through the 8042 data port) */
#define SET_MOUSE_SCALING_1TO1                  0xE6
#define SET_MOUSE_RESOLUTION                    0xE8
#define READ_MOUSE_STATUS                       0xE9
#define GET_DEVICE_ID                           0xF2
#define SET_MOUSE_SAMPLING_RATE                 0xF3
#define ENABLE_MOUSE_TRANSMISSION               0xF4
#define MOUSE_RESET                             0xFF

/* mouse responses */
#define MOUSE_COMPLETE                          0xAA
#define MOUSE_ID_BYTE                           0x00
#define WHEELMOUSE_ID_BYTE                      0x03

/* maximum number of pointer/keyboard port names the port driver */
#define POINTER_PORTS_MAXIMUM                   8
#define KEYBOARD_PORTS_MAXIMUM                  8

/* NtDeviceIoControlFile internal IoControlCode values for keyboard device */
#define IOCTL_INTERNAL_KEYBOARD_CONNECT    CTL_CODE(FILE_DEVICE_KEYBOARD, 0x0080, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_INTERNAL_KEYBOARD_DISCONNECT CTL_CODE(FILE_DEVICE_KEYBOARD, 0x0100, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_INTERNAL_KEYBOARD_ENABLE     CTL_CODE(FILE_DEVICE_KEYBOARD, 0x0200, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_INTERNAL_KEYBOARD_DISABLE    CTL_CODE(FILE_DEVICE_KEYBOARD, 0x0400, METHOD_NEITHER, FILE_ANY_ACCESS)

/* NtDeviceIoControlFile internal IoControlCode values for mouse device */
#define IOCTL_INTERNAL_MOUSE_CONNECT    CTL_CODE(FILE_DEVICE_MOUSE, 0x0080, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_INTERNAL_MOUSE_DISCONNECT CTL_CODE(FILE_DEVICE_MOUSE, 0x0100, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_INTERNAL_MOUSE_ENABLE     CTL_CODE(FILE_DEVICE_MOUSE, 0x0200, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_INTERNAL_MOUSE_DISABLE    CTL_CODE(FILE_DEVICE_MOUSE, 0x0400, METHOD_NEITHER, FILE_ANY_ACCESS)

/* i8042 controller input/output ports */
typedef enum _I8042IOPORTTYPE
{
    i8042Dat = 0,
    i8042Cmd,
    i8042MaxPorts
} I8042IOPORTTYPE;

/* device types attached to the i8042 controller */
typedef enum _I8042DEVTYPE
{
    CtrlDevType,
    KbdDevType,
    MouDevType,
    NoDevice
} I8042DEVTYPE;

/* keyboard output states */
typedef enum _KBDSTATE
{
    Idle,
    SendFirstByte,
    SendLastByte
} KBDSTATE;

/* keyboard scan code input states */
typedef enum _KBDSCANSTATE
{
    Normal,
    GotE0,
    GotE1
} KBDSCANSTATE;

/* mouse states */
typedef enum _MOUSTATE
{
    MouseIdle,
    XMovement,
    YMovement,
    ZMovement,
    MouseExpectingACK
} MOUSTATE;

typedef VOID (*PSERVICECALLBACK)(PVOID Ctx, PVOID pArg1, PVOID pArg2, PVOID pArg3);

typedef struct _CONNECT_DATA
{
    PDEVICE_OBJECT        ClassDeviceObject;
    PSERVICECALLBACK      ClassService;
} CONNECT_DATA, *PCONNECT_DATA;

typedef struct _KBDSETPACKET
{
    USHORT State;
    UCHAR  FirstByte;
    UCHAR  LastByte;
} KBDSETPACKET, *PKBDSETPACKET;

typedef struct _I8042CFGINF
{
    INTERFACE_TYPE        InterfaceType;                /**< bus interface type */
    ULONG                 uBusNr;                       /**< bus number */
    ULONG                 cPorts;
    CM_PARTIAL_RESOURCE_DESCRIPTOR aPorts[i8042MaxPorts];
    CM_PARTIAL_RESOURCE_DESCRIPTOR KbdInt;
    CM_PARTIAL_RESOURCE_DESCRIPTOR MouInt;
    BOOLEAN               fFloatSave;                   /**< whether to save floating point context */
    USHORT                iResend;                      /**< number of retries allowed */
    USHORT                PollingIterations;            /**< number of polling iterations */
    USHORT                PollingIterationsMaximum;
    USHORT                PollStatusIterations;
    USHORT                StallMicroseconds;
    KEYBOARD_ATTRIBUTES   KbdAttr;
    KEYBOARD_TYPEMATIC_PARAMETERS KeyRepeatCurrent;
    KEYBOARD_INDICATOR_PARAMETERS KbdInd;
    MOUSE_ATTRIBUTES      MouAttr;
    USHORT                MouseResolution;
    ULONG                 EnableWheelDetection;
} I8042CFGINF, *PI8042CFGINF;

typedef struct _PORTKBDEXT
{
    CONNECT_DATA ConnectData;
    ULONG                 cInput;
    PKEYBOARD_INPUT_DATA  InputData;
    PKEYBOARD_INPUT_DATA  DataIn;
    PKEYBOARD_INPUT_DATA  DataOut;
    PKEYBOARD_INPUT_DATA  DataEnd;
    KEYBOARD_INPUT_DATA   CurrentInput;
    KBDSCANSTATE          CurrentScanState;
    KBDSETPACKET          CurrentOutput;
    USHORT                ResendCount;
    KTIMER                DataConsumptionTimer;
    USHORT                UnitId;
} PORTKBDEXT, *PPORTKBDEXT;

typedef struct _PORTMOUEXT
{
    CONNECT_DATA          ConnectData;
    ULONG                 cInput;
    PMOUSE_INPUT_DATA     InputData;
    PMOUSE_INPUT_DATA     DataIn;
    PMOUSE_INPUT_DATA     DataOut;
    PMOUSE_INPUT_DATA     DataEnd;
    MOUSE_INPUT_DATA      CurrentInput;
    USHORT                InputState;
    UCHAR                 uCurrSignAndOverflow;
    UCHAR                 uPrevSignAndOverflow;
    UCHAR                 PreviousButtons;
    KTIMER                DataConsumptionTimer;
    LARGE_INTEGER         PreviousTick;
    USHORT                UnitId;
    ULONG                 SynchTickCount;
    UCHAR                 LastByteReceived;
} PORTMOUEXT, *PPORTMOUEXT;

typedef struct _DEVEXT
{
    ULONG HardwarePresent;
    volatile uint32_t     KeyboardEnableCount;
    volatile uint32_t     MouseEnableCount;
    PDEVICE_OBJECT        pDevObj;
    PUCHAR                DevRegs[i8042MaxPorts];
    PORTKBDEXT            KbdExt;
    PORTMOUEXT            MouExt;
    I8042CFGINF           Cfg;
    PKINTERRUPT           KbdIntObj;
    PKINTERRUPT           MouIntObj;
    KSPIN_LOCK            ShIntObj;
    KDPC                  RetriesExceededDpc;
    KDPC                  KeyboardIsrDpc;
    KDPC                  KeyboardIsrDpcRetry;
    LONG                  DpcInterlockKeyboard;
    KDPC                  MouseIsrDpc;
    KDPC                  MouseIsrDpcRetry;
    LONG                  DpcInterlockMouse;
    KDPC                  TimeOutDpc;
    KTIMER                CommandTimer;
    LONG                  TimerCount;
    BOOLEAN               fUnmapRegs;
    VMMDevReqMouseStatus  *pReq;
} DEVEXT, *PDEVEXT;

typedef struct _INITEXT
{
    DEVEXT                DevExt;
} INITEXT, *PINITEXT;

typedef struct _I8042INITDATACTX
{
    PDEVEXT               pDevExt;
    int                   DevType;
} I8042INITDATACTX, *PI8042INITDATACTX;

typedef struct _I8042TRANSMITCCBCTX
{
    ULONG                 HwDisEnMask;
    BOOLEAN               fAndOp;
    UCHAR                 ByteMask;
    NTSTATUS              Status;
} I8042TRANSMITCCBCTX, *PI8042TRANSMITCCBCTX;

typedef struct _GETDATAPTRCTX
{
    PDEVEXT               pDevExt;
    int                   DevType;
    PVOID                 DataIn;
    PVOID                 DataOut;
    ULONG                 cInput;
} GETDATAPTRCTX, *PGETDATAPTRCTX;

typedef struct _SETDATAPTRCTX
{
    PDEVEXT               pDevExt;
    int                   DevType;
    ULONG                 cInput;
    PVOID                 DataOut;
} SETDATAPTRCTX, *PSETDATAPTRCTX;

typedef struct _TIMERCTX
{
    PDEVICE_OBJECT        pDevObj;
    PLONG                 TimerCounter;
    LONG                  NewTimerCount;
} TIMERCTX, *PTIMERCTX;

typedef struct _KBDINITIATECTX
{
    PDEVICE_OBJECT        pDevObj;
    UCHAR                 FirstByte;
    UCHAR                 LastByte;
} KBDINITIATECTX, *PKBDINITIATECTX;

typedef enum _OPTYPE
{
    IncrementOperation,
    DecrementOperation,
    WriteOperation,
} OPTYPE;

typedef struct _VAROPCTX
{
    PLONG                 VariableAddress;
    OPTYPE                Operation;
    PLONG                 NewValue;
} VAROPCTX, *PVAROPCTX;

typedef struct _KBDTYPEINFO
{
    USHORT cFunctionKeys;
    USHORT cIndicators;
    USHORT cKeysTotal;
} KBDTYPEINFO;

static const INDICATOR_LIST s_aIndicators[3] =
{
    {0x3A, KEYBOARD_CAPS_LOCK_ON},
    {0x45, KEYBOARD_NUM_LOCK_ON},
    {0x46, KEYBOARD_SCROLL_LOCK_ON}
};

static const KBDTYPEINFO s_aKeybType[4] =
{
    {10, 3,  84},    /* PC/XT 83- 84-key keyboard (and compatibles) */
    {12, 3, 102},    /* Olivetti M24 102-key keyboard (and compatibles) */
    {10, 3,  84},    /* All AT type keyboards (84-86 keys) */
    {12, 3, 101}     /* Enhanced 101- or 102-key keyboards (and compatibles) */
};

RT_C_DECLS_BEGIN
static NTSTATUS MouFindWheel(PDEVICE_OBJECT pDevObj);
static VOID     MouGetRegstry(PINITEXT pInit, PUNICODE_STRING RegistryPath,
                              PUNICODE_STRING KeyboardDeviceName, PUNICODE_STRING PointerDeviceName);
static NTSTATUS MouInitHw(PDEVICE_OBJECT pDevObj);
static VOID     KbdGetRegstry(PINITEXT pInit, PUNICODE_STRING RegistryPath,
                              PUNICODE_STRING KeyboardDeviceName, PUNICODE_STRING PointerDeviceName);
static NTSTATUS KbdInitHw(PDEVICE_OBJECT pDevObj);
static VOID     HwGetRegstry(PINITEXT pInit, PUNICODE_STRING RegistryPath,
                             PUNICODE_STRING KeyboardDeviceName, PUNICODE_STRING PointerDeviceName);
static VOID     CreateResList(PDEVEXT pDevExt, PCM_RESOURCE_LIST *pResList, PULONG pResListSize);
static VOID     InitHw(PDEVICE_OBJECT pDevObj);
static NTSTATUS MouCallOut(PVOID pCtx, PUNICODE_STRING PathName,
                           INTERFACE_TYPE BusType, ULONG uBusNr, PKEY_VALUE_FULL_INFORMATION *pBusInf,
                           CONFIGURATION_TYPE uCtrlType, ULONG uCtrlNr, PKEY_VALUE_FULL_INFORMATION *pCtrlInf,
                           CONFIGURATION_TYPE uPrfType, ULONG uPrfNr, PKEY_VALUE_FULL_INFORMATION *pPrfInf);
static NTSTATUS KbdCallOut(PVOID pCtx, PUNICODE_STRING PathName,
                           INTERFACE_TYPE BusType, ULONG uBusNr, PKEY_VALUE_FULL_INFORMATION *pBusInf,
                           CONFIGURATION_TYPE uCtrlType, ULONG uCtrlNr, PKEY_VALUE_FULL_INFORMATION *pCtrlInf,
                           CONFIGURATION_TYPE uPrfType, ULONG uPrfNr, PKEY_VALUE_FULL_INFORMATION *pPrfInf);
/*  */ NTSTATUS DriverEntry(PDRIVER_OBJECT pDrvObj, PUNICODE_STRING RegistryPath);
RT_C_DECLS_END

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT,CreateResList)
#pragma alloc_text(INIT,MouFindWheel)
#pragma alloc_text(INIT,KbdInitHw)
#pragma alloc_text(INIT,KbdGetRegstry)
#pragma alloc_text(INIT,KbdCallOut)
#pragma alloc_text(INIT,MouInitHw)
#pragma alloc_text(INIT,MouGetRegstry)
#pragma alloc_text(INIT,MouCallOut)
#pragma alloc_text(INIT,InitHw)
#pragma alloc_text(INIT,HwGetRegstry)
#pragma alloc_text(INIT,DriverEntry)
#endif

static BOOLEAN MouDataToQueue(PPORTMOUEXT MouExt, PMOUSE_INPUT_DATA InputData)
{
    if (   MouExt->DataIn == MouExt->DataOut
        && MouExt->cInput)
        return FALSE;

    *(MouExt->DataIn) = *InputData;
    MouExt->cInput++;
    MouExt->DataIn++;
    if (MouExt->DataIn == MouExt->DataEnd)
        MouExt->DataIn = MouExt->InputData;
    return TRUE;
}

static BOOLEAN KbdDataToQueue(PPORTKBDEXT KbdExt, PKEYBOARD_INPUT_DATA InputData)
{
    PKEYBOARD_INPUT_DATA previousDataIn;

    if (   KbdExt->DataIn == KbdExt->DataOut
        && KbdExt->cInput)
    {
        if (KbdExt->DataIn == KbdExt->InputData)
            previousDataIn = KbdExt->DataEnd;
        else
            previousDataIn = KbdExt->DataIn - 1;
        previousDataIn->MakeCode = KEYBOARD_OVERRUN_MAKE_CODE;
        previousDataIn->Flags = 0;
        return FALSE;
    }

    *(KbdExt->DataIn) = *InputData;
    KbdExt->cInput++;
    KbdExt->DataIn++;
    if (KbdExt->DataIn == KbdExt->DataEnd)
        KbdExt->DataIn = KbdExt->InputData;
    return TRUE;
}

/**
 * Queues the current input data to be processed by a DPC outside the ISR
 */
static VOID QueueInput(PDEVICE_OBJECT pDevObj)
{
    PDEVEXT pDevExt = (PDEVEXT)pDevObj->DeviceExtension;
    if (pDevExt->MouseEnableCount)
    {
        pDevExt->MouExt.CurrentInput.UnitId = pDevExt->MouExt.UnitId;
        if (!MouDataToQueue(&pDevExt->MouExt, &pDevExt->MouExt.CurrentInput))
        {
        }
        else if (pDevExt->DpcInterlockMouse >= 0)
           pDevExt->DpcInterlockMouse++;
        else
           KeInsertQueueDpc(&pDevExt->MouseIsrDpc, pDevObj->CurrentIrp, NULL);
    }
}

/**
 * Drain the i8042 controller buffer.
 */
static VOID DrainOutBuf(PUCHAR DataAddress, PUCHAR CommandAddress)
{
    UCHAR byte;
    for (unsigned i = 0; i < 2000; i++)
    {
        if (!(I8X_GET_STATUS_BYTE(CommandAddress) & INPUT_BUFFER_FULL))
            break;
        KeStallExecutionProcessor(500);
    }
    while (I8X_GET_STATUS_BYTE(CommandAddress) & OUTPUT_BUFFER_FULL)
        byte = I8X_GET_DATA_BYTE(DataAddress);
}

/**
 * Read a data byte from the controller, keyboard or mouse in polling mode.
 */
static NTSTATUS GetBytePoll(int DevType, PDEVEXT pDevExt, PUCHAR Byte)
{
    UCHAR byte;

    ULONG i = 0;
    UCHAR fMask = (DevType == MouDevType) ? (UCHAR)(OUTPUT_BUFFER_FULL | MOUSE_OUTPUT_BUFFER_FULL)
                                          : (UCHAR) OUTPUT_BUFFER_FULL;
    while (   (i < (ULONG)pDevExt->Cfg.PollingIterations)
           && ((UCHAR)((byte = I8X_GET_STATUS_BYTE(pDevExt->DevRegs[i8042Cmd])) & fMask) != fMask))
    {
        if (byte & OUTPUT_BUFFER_FULL)
            *Byte = I8X_GET_DATA_BYTE(pDevExt->DevRegs[i8042Dat]);
        else
        {
            KeStallExecutionProcessor(pDevExt->Cfg.StallMicroseconds);
            i++;
        }
    }
    if (i >= (ULONG)pDevExt->Cfg.PollingIterations)
        return STATUS_IO_TIMEOUT;

    *Byte = I8X_GET_DATA_BYTE(pDevExt->DevRegs[i8042Dat]);
    return STATUS_SUCCESS;
}

/**
 * Send a command or data byte to the controller, keyboard or mouse.
 */
static NTSTATUS PutBytePoll(CCHAR PortType, BOOLEAN fWaitForAck, int AckDevType, PDEVEXT pDevExt, UCHAR Byte)
{
    NTSTATUS status;

    if (AckDevType == MouDevType)
    {
        /* switch to AUX device */
        PutBytePoll(i8042Cmd, FALSE /*=wait*/, NoDevice, pDevExt, I8042_WRITE_TO_AUXILIARY_DEVICE);
    }

    PUCHAR dataAddress = pDevExt->DevRegs[i8042Dat];
    PUCHAR commandAddress = pDevExt->DevRegs[i8042Cmd];
    for (unsigned j = 0; j < (unsigned)pDevExt->Cfg.iResend; j++)
    {
        unsigned i = 0;
        while (   i++ < (ULONG)pDevExt->Cfg.PollingIterations
               && (I8X_GET_STATUS_BYTE(commandAddress) & INPUT_BUFFER_FULL))
            KeStallExecutionProcessor(pDevExt->Cfg.StallMicroseconds);
        if (i >= (ULONG)pDevExt->Cfg.PollingIterations)
            return STATUS_IO_TIMEOUT;

        DrainOutBuf(dataAddress, commandAddress);

        if (PortType == i8042Cmd)
            I8X_PUT_COMMAND_BYTE(commandAddress, Byte);
        else
            I8X_PUT_DATA_BYTE(dataAddress, Byte);

        if (!fWaitForAck)
            return STATUS_SUCCESS;

        BOOLEAN fKeepTrying = FALSE;
        UCHAR byte;
        while ((status = GetBytePoll(AckDevType, pDevExt, &byte)) == STATUS_SUCCESS)
        {
            if (byte == ACKNOWLEDGE)
                break;
            else if (byte == RESEND)
            {
                if (AckDevType == MouDevType)
                    PutBytePoll(i8042Cmd, FALSE /*=wait*/, NoDevice, pDevExt, I8042_WRITE_TO_AUXILIARY_DEVICE);
                fKeepTrying = TRUE;
                break;
            }
        }

        if (!fKeepTrying)
            return status;
    }

    return STATUS_IO_TIMEOUT;
}

/**
 * Read a byte from controller, keyboard or mouse
 */
static VOID GetByteAsync(int DevType, PDEVEXT pDevExt, PUCHAR pByte)
{
    UCHAR byte;
    UCHAR fMask;

    ULONG i = 0;
    fMask = (DevType == MouDevType)
                               ? (UCHAR) (OUTPUT_BUFFER_FULL | MOUSE_OUTPUT_BUFFER_FULL)
                               : (UCHAR) OUTPUT_BUFFER_FULL;

    while (   i < (ULONG)pDevExt->Cfg.PollingIterations
           && ((UCHAR)((byte = I8X_GET_STATUS_BYTE(pDevExt->DevRegs[i8042Cmd])) & fMask) != fMask))
    {
        if (byte & OUTPUT_BUFFER_FULL)
            *pByte = I8X_GET_DATA_BYTE(pDevExt->DevRegs[i8042Dat]);
        else
            i++;
    }
    if (i >= (ULONG)pDevExt->Cfg.PollingIterations)
        return;

    *pByte = I8X_GET_DATA_BYTE(pDevExt->DevRegs[i8042Dat]);
}

/**
 * Send a command or data byte to the controller, keyboard or mouse
 * asynchronously.
 */
static VOID PutByteAsync(CCHAR PortType, PDEVEXT pDevExt, UCHAR Byte)
{
    unsigned i = 0;
    while (I8X_GET_STATUS_BYTE(pDevExt->DevRegs[i8042Cmd]) & INPUT_BUFFER_FULL)
        if (i++ >= (ULONG)pDevExt->Cfg.PollingIterations)
            return;

    if (PortType == i8042Cmd)
        I8X_PUT_COMMAND_BYTE(pDevExt->DevRegs[i8042Cmd], Byte);
    else
        I8X_PUT_DATA_BYTE(pDevExt->DevRegs[i8042Dat], Byte);
}

/**
 * Initiaze an I/O operation for the keyboard device.
 */
static VOID KbdStartIO(PVOID pCtx)
{
    PDEVICE_OBJECT pDevObj = (PDEVICE_OBJECT)pCtx;
    PDEVEXT pDevExt = (PDEVEXT)pDevObj->DeviceExtension;

    pDevExt->TimerCount = 3;

    KBDSETPACKET keyboardPacket = pDevExt->KbdExt.CurrentOutput;

    if (pDevExt->KbdExt.CurrentOutput.State == SendFirstByte)
        PutByteAsync(i8042Dat, pDevExt, keyboardPacket.FirstByte);
    else if (pDevExt->KbdExt.CurrentOutput.State == SendLastByte)
        PutByteAsync(i8042Dat, pDevExt, keyboardPacket.LastByte);
    else
        ASSERT(FALSE);
}

static BOOLEAN KbdStartWrapper(PVOID pCtx)
{
    PDEVICE_OBJECT pDevObj = ((PKBDINITIATECTX)pCtx)->pDevObj;
    PDEVEXT pDevExt = (PDEVEXT) pDevObj->DeviceExtension;
    pDevExt->KbdExt.CurrentOutput.State = SendFirstByte;
    pDevExt->KbdExt.CurrentOutput.FirstByte = ((PKBDINITIATECTX)pCtx)->FirstByte;
    pDevExt->KbdExt.CurrentOutput.LastByte = ((PKBDINITIATECTX)pCtx)->LastByte;
    pDevExt->KbdExt.ResendCount = 0;
    KbdStartIO(pDevObj);
    return TRUE;
}

static BOOLEAN DecTimer(PVOID pCtx)
{
    PTIMERCTX pTmCtx = (PTIMERCTX)pCtx;
    PDEVICE_OBJECT pDevObj = pTmCtx->pDevObj;
    PDEVEXT pDevExt = (PDEVEXT)pDevObj->DeviceExtension;

    if (*(pTmCtx->TimerCounter) != -1)
        (*(pTmCtx->TimerCounter))--;

    pTmCtx->NewTimerCount = *(pTmCtx->TimerCounter);

    if (*(pTmCtx->TimerCounter) == 0)
    {
        pDevExt->KbdExt.CurrentOutput.State = Idle;
        pDevExt->KbdExt.ResendCount = 0;
    }
    return TRUE;
}

/**
 * Perform an operation on the InterlockedDpcVariable.
 */
static BOOLEAN DpcVarOp(PVOID pCtx)
{
    PVAROPCTX pOpCtx = (PVAROPCTX)pCtx;
    switch (pOpCtx->Operation)
    {
        case IncrementOperation:
            (*pOpCtx->VariableAddress)++;
            break;
        case DecrementOperation:
            (*pOpCtx->VariableAddress)--;
            break;
        case WriteOperation:
            *pOpCtx->VariableAddress = *pOpCtx->NewValue;
            break;
        default:
            ASSERT(FALSE);
            break;
    }

    *(pOpCtx->NewValue) = *(pOpCtx->VariableAddress);
    return TRUE;
}

static BOOLEAN GetDataQueuePtr(PVOID pCtx)
{
    PDEVEXT pDevExt = (PDEVEXT)((PGETDATAPTRCTX)pCtx)->pDevExt;
    CCHAR DevType = (CCHAR)((PGETDATAPTRCTX)pCtx)->DevType;

    if (DevType == KbdDevType)
    {
        ((PGETDATAPTRCTX)pCtx)->DataIn  = pDevExt->KbdExt.DataIn;
        ((PGETDATAPTRCTX)pCtx)->DataOut = pDevExt->KbdExt.DataOut;
        ((PGETDATAPTRCTX)pCtx)->cInput  = pDevExt->KbdExt.cInput;
    }
    else if (DevType == MouDevType)
    {
        ((PGETDATAPTRCTX)pCtx)->DataIn  = pDevExt->MouExt.DataIn;
        ((PGETDATAPTRCTX)pCtx)->DataOut = pDevExt->MouExt.DataOut;
        ((PGETDATAPTRCTX)pCtx)->cInput  = pDevExt->MouExt.cInput;
    }
    else
        ASSERT(FALSE);
    return TRUE;
}

static BOOLEAN InitDataQueue(PVOID pCtx)
{
    PDEVEXT pDevExt = (PDEVEXT)((PI8042INITDATACTX)pCtx)->pDevExt;
    CCHAR DevType = (CCHAR) ((PI8042INITDATACTX)pCtx)->DevType;

    if (DevType == KbdDevType)
    {
        pDevExt->KbdExt.cInput  = 0;
        pDevExt->KbdExt.DataIn  = pDevExt->KbdExt.InputData;
        pDevExt->KbdExt.DataOut = pDevExt->KbdExt.InputData;
    }
    else if (DevType == MouDevType)
    {
        pDevExt->MouExt.cInput  = 0;
        pDevExt->MouExt.DataIn  = pDevExt->MouExt.InputData;
        pDevExt->MouExt.DataOut = pDevExt->MouExt.InputData;
    }
    else
        ASSERT(FALSE);
    return TRUE;
}

static BOOLEAN SetDataQueuePtr(PVOID pCtx)
{
    PDEVEXT pDevExt = (PDEVEXT)((PSETDATAPTRCTX)pCtx)->pDevExt;
    CCHAR DevType = (CCHAR) ((PSETDATAPTRCTX)pCtx)->DevType;

    if (DevType == KbdDevType)
    {
        pDevExt->KbdExt.DataOut = (PKEYBOARD_INPUT_DATA)((PSETDATAPTRCTX)pCtx)->DataOut;
        pDevExt->KbdExt.cInput -= ((PSETDATAPTRCTX)pCtx)->cInput;
    }
    else if (DevType == MouDevType)
    {
        pDevExt->MouExt.DataOut = (PMOUSE_INPUT_DATA)((PSETDATAPTRCTX)pCtx)->DataOut;
        pDevExt->MouExt.cInput -= ((PSETDATAPTRCTX)pCtx)->cInput;
    }
    else
        ASSERT(FALSE);
    return TRUE;
}

/**
 * DISPATCH_LEVEL IRQL: Complete requests.
 */
static VOID CompleteDpc(PKDPC Dpc, PDEVICE_OBJECT pDevObj, PIRP Irp, PVOID pCtx)
{
    NOREF(Dpc);
    NOREF(pCtx);

    PDEVEXT pDevExt = (PDEVEXT)pDevObj->DeviceExtension;

    KeCancelTimer(&pDevExt->CommandTimer);

    Irp = pDevObj->CurrentIrp;
    ASSERT(Irp);

    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    switch (irpSp->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_KEYBOARD_SET_INDICATORS:
            pDevExt->Cfg.KbdInd = *(PKEYBOARD_INDICATOR_PARAMETERS)Irp->AssociatedIrp.SystemBuffer;
            break;

        case IOCTL_KEYBOARD_SET_TYPEMATIC:
            pDevExt->Cfg.KeyRepeatCurrent = *(PKEYBOARD_TYPEMATIC_PARAMETERS)Irp->AssociatedIrp.SystemBuffer;
            break;

        default:
            break;
    }

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoStartNextPacket(pDevObj, FALSE);
    IoCompleteRequest (Irp, IO_KEYBOARD_INCREMENT);
}

static NTSTATUS I8042Flush(PDEVICE_OBJECT pDevObj, PIRP Irp)
{
    NOREF(pDevObj);
    NOREF(Irp);

    return STATUS_NOT_IMPLEMENTED;
}

/**
 * Dispatch internal device control requests.
 */
static NTSTATUS I8042DevCtrl(PDEVICE_OBJECT pDevObj, PIRP Irp)
{
    NTSTATUS status;
    I8042INITDATACTX initDataCtx;
    PVOID pParams;
    PKEYBOARD_ATTRIBUTES KbdAttr;
    ULONG cbTrans;

    PDEVEXT pDevExt = (PDEVEXT)pDevObj->DeviceExtension;

    Irp->IoStatus.Information = 0;
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);

    switch (irpSp->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_INTERNAL_KEYBOARD_CONNECT:
            if ((pDevExt->HardwarePresent & KEYBOARD_HARDWARE_PRESENT) != KEYBOARD_HARDWARE_PRESENT)
            {
                status = STATUS_NO_SUCH_DEVICE;
                break;
            }
            else if (pDevExt->KbdExt.ConnectData.ClassService)
            {
                status = STATUS_SHARING_VIOLATION;
                break;
            }
            else if (irpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(CONNECT_DATA))
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            pDevExt->KbdExt.ConnectData = *((PCONNECT_DATA) (irpSp->Parameters.DeviceIoControl.Type3InputBuffer));
            initDataCtx.pDevExt = pDevExt;
            initDataCtx.DevType = KbdDevType;
            KeSynchronizeExecution(pDevExt->KbdIntObj, InitDataQueue, &initDataCtx);
            status = STATUS_SUCCESS;
            break;

        case IOCTL_INTERNAL_MOUSE_CONNECT:
            if ((pDevExt->HardwarePresent & MOUSE_HARDWARE_PRESENT) != MOUSE_HARDWARE_PRESENT)
            {
                status = STATUS_NO_SUCH_DEVICE;
                break;
            }
            else if (pDevExt->MouExt.ConnectData.ClassService)
            {
                status = STATUS_SHARING_VIOLATION;
                break;
            }
            else if (irpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(CONNECT_DATA))
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            pDevExt->MouExt.ConnectData = *((PCONNECT_DATA) (irpSp->Parameters.DeviceIoControl.Type3InputBuffer));
            initDataCtx.pDevExt = pDevExt;
            initDataCtx.DevType = MouDevType;
            KeSynchronizeExecution(pDevExt->MouIntObj, InitDataQueue, &initDataCtx);
            status = STATUS_SUCCESS;
            break;

        case IOCTL_INTERNAL_KEYBOARD_DISCONNECT:
        case IOCTL_INTERNAL_MOUSE_DISCONNECT:
            status = STATUS_NOT_IMPLEMENTED;
            break;

        case IOCTL_INTERNAL_KEYBOARD_ENABLE:
        case IOCTL_INTERNAL_KEYBOARD_DISABLE:
        case IOCTL_INTERNAL_MOUSE_ENABLE:
        case IOCTL_INTERNAL_MOUSE_DISABLE:
            status = STATUS_PENDING;
            break;

        case IOCTL_KEYBOARD_QUERY_ATTRIBUTES:
            if (irpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(KEYBOARD_ATTRIBUTES))
                status = STATUS_BUFFER_TOO_SMALL;
            else
            {
                *(PKEYBOARD_ATTRIBUTES) Irp->AssociatedIrp.SystemBuffer = pDevExt->Cfg.KbdAttr;
                Irp->IoStatus.Information = sizeof(KEYBOARD_ATTRIBUTES);
                status = STATUS_SUCCESS;
            }
            break;

        case IOCTL_KEYBOARD_QUERY_INDICATOR_TRANSLATION:
            cbTrans = sizeof(KEYBOARD_INDICATOR_TRANSLATION)
                    + (sizeof(INDICATOR_LIST) * (pDevExt->Cfg.KbdAttr.NumberOfIndicators - 1));

            if (irpSp->Parameters.DeviceIoControl.OutputBufferLength < cbTrans)
                status = STATUS_BUFFER_TOO_SMALL;
            else
            {
                ((PKEYBOARD_INDICATOR_TRANSLATION)
                   Irp->AssociatedIrp.SystemBuffer)->NumberOfIndicatorKeys = pDevExt->Cfg.KbdAttr.NumberOfIndicators;
                RtlMoveMemory(((PKEYBOARD_INDICATOR_TRANSLATION)
                               Irp->AssociatedIrp.SystemBuffer)->IndicatorList, (PCHAR)s_aIndicators, cbTrans);

                Irp->IoStatus.Information = cbTrans;
                status = STATUS_SUCCESS;
            }
            break;

        case IOCTL_KEYBOARD_QUERY_INDICATORS:
            if (irpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(KEYBOARD_INDICATOR_PARAMETERS))
                status = STATUS_BUFFER_TOO_SMALL;
            else
            {
                *(PKEYBOARD_INDICATOR_PARAMETERS)Irp->AssociatedIrp.SystemBuffer = pDevExt->Cfg.KbdInd;
                Irp->IoStatus.Information = sizeof(KEYBOARD_INDICATOR_PARAMETERS);
                status = STATUS_SUCCESS;
            }
            break;

        case IOCTL_KEYBOARD_SET_INDICATORS:
            if (   (irpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(KEYBOARD_INDICATOR_PARAMETERS))
                || ( (((PKEYBOARD_INDICATOR_PARAMETERS)Irp->AssociatedIrp.SystemBuffer)->LedFlags
                    & ~(KEYBOARD_SCROLL_LOCK_ON | KEYBOARD_NUM_LOCK_ON | KEYBOARD_CAPS_LOCK_ON)) != 0))
                status = STATUS_INVALID_PARAMETER;
            else
                status = STATUS_PENDING;
            break;

        case IOCTL_KEYBOARD_QUERY_TYPEMATIC:
            if (irpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(KEYBOARD_TYPEMATIC_PARAMETERS))
                status = STATUS_BUFFER_TOO_SMALL;
            else
            {
                *(PKEYBOARD_TYPEMATIC_PARAMETERS)Irp->AssociatedIrp.SystemBuffer = pDevExt->Cfg.KeyRepeatCurrent;
                Irp->IoStatus.Information = sizeof(KEYBOARD_TYPEMATIC_PARAMETERS);
                status = STATUS_SUCCESS;
            }
            break;

        case IOCTL_KEYBOARD_SET_TYPEMATIC:
            pParams = Irp->AssociatedIrp.SystemBuffer;
            KbdAttr = &pDevExt->Cfg.KbdAttr;
            if (   irpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(KEYBOARD_TYPEMATIC_PARAMETERS)
                || ((PKEYBOARD_TYPEMATIC_PARAMETERS)pParams)->Rate  < KbdAttr->KeyRepeatMinimum.Rate
                || ((PKEYBOARD_TYPEMATIC_PARAMETERS)pParams)->Rate  > KbdAttr->KeyRepeatMaximum.Rate
                || ((PKEYBOARD_TYPEMATIC_PARAMETERS)pParams)->Delay < KbdAttr->KeyRepeatMinimum.Delay
                || ((PKEYBOARD_TYPEMATIC_PARAMETERS)pParams)->Delay > KbdAttr->KeyRepeatMaximum.Delay)
                status = STATUS_INVALID_PARAMETER;
            else
                status = STATUS_PENDING;
            break;

        case IOCTL_MOUSE_QUERY_ATTRIBUTES:
            if (irpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(MOUSE_ATTRIBUTES))
                status = STATUS_BUFFER_TOO_SMALL;
            else
            {
                *(PMOUSE_ATTRIBUTES) Irp->AssociatedIrp.SystemBuffer = pDevExt->Cfg.MouAttr;

                Irp->IoStatus.Information = sizeof(MOUSE_ATTRIBUTES);
                status = STATUS_SUCCESS;
            }
            break;

        default:
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    Irp->IoStatus.Status = status;
    if (status == STATUS_PENDING)
    {
        IoMarkIrpPending(Irp);
        IoStartPacket(pDevObj, Irp, (PULONG)NULL, NULL);
    }
    else
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

/**
 * Dispatch routine for create/open and close requests.
 */
static NTSTATUS I8042OpenClose(PDEVICE_OBJECT pDevObj, PIRP Irp)
{
    NOREF(pDevObj);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/**
 * DISPATCH_LEVEL IRQL: Complete requests that have exceeded the maximum
 * number of retries.
 */
static VOID CtrlRetriesExceededDpc(PKDPC Dpc, PDEVICE_OBJECT pDevObj, PIRP Irp, PVOID pCtx)
{
    RT_NOREF(Dpc, pCtx);

    Irp->IoStatus.Status = STATUS_IO_TIMEOUT;

    IoStartNextPacket(pDevObj, FALSE);
    IoCompleteRequest(Irp, IO_KEYBOARD_INCREMENT);
}

static UCHAR TypematicPeriod[] =
{
    31, 31, 28, 26, 23, 20, 18, 17, 15, 13, 12, 11, 10,  9,
     9,  8,  7,  6,  5,  4,  4,  3,  3,  2,  2,  1,  1,  1
};

/**
 * Convert typematic rate/delay to a value expected by the keyboard:
 *  - bit 7 is zero
 *  - bits 5...6 indicate the delay
 *  - bits 0...4 indicate the rate
 */
static UCHAR ConvertTypematic(USHORT Rate, USHORT Delay)
{
    UCHAR value = (UCHAR) ((Delay / 250) - 1);
    value <<= 5;
    if (Rate <= 27)
        value |= TypematicPeriod[Rate];

    return value;
}

/**
 * Start an I/O operation for the device.
 */
static VOID I8042StartIo(PDEVICE_OBJECT pDevObj, PIRP Irp)
{
    KBDINITIATECTX keyboardInitiateContext;
    LARGE_INTEGER deltaTime;
    LONG interlockedResult;

    PDEVEXT pDevExt = (PDEVEXT)pDevObj->DeviceExtension;

    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    switch (irpSp->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_INTERNAL_KEYBOARD_ENABLE:
            interlockedResult = ASMAtomicIncU32(&pDevExt->KeyboardEnableCount);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoStartNextPacket(pDevObj, FALSE);
            IoCompleteRequest(Irp, IO_KEYBOARD_INCREMENT);
            break;

        case IOCTL_INTERNAL_KEYBOARD_DISABLE:
            if (pDevExt->KeyboardEnableCount == 0)
                Irp->IoStatus.Status = STATUS_DEVICE_DATA_ERROR;
            else
            {
                ASMAtomicDecU32(&pDevExt->KeyboardEnableCount);
                Irp->IoStatus.Status = STATUS_SUCCESS;
            }
            IoStartNextPacket(pDevObj, FALSE);
            IoCompleteRequest(Irp, IO_KEYBOARD_INCREMENT);
            break;

        case IOCTL_INTERNAL_MOUSE_ENABLE:
            ASMAtomicIncU32(&pDevExt->MouseEnableCount);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoStartNextPacket(pDevObj, FALSE);
            IoCompleteRequest(Irp, IO_MOUSE_INCREMENT);
            break;

        case IOCTL_INTERNAL_MOUSE_DISABLE:
            if (pDevExt->MouseEnableCount == 0)
                Irp->IoStatus.Status = STATUS_DEVICE_DATA_ERROR;
            else
            {
                ASMAtomicDecU32(&pDevExt->MouseEnableCount);
                Irp->IoStatus.Status = STATUS_SUCCESS;
            }
            IoStartNextPacket(pDevObj, FALSE);
            IoCompleteRequest(Irp, IO_MOUSE_INCREMENT);
            break;

        case IOCTL_KEYBOARD_SET_INDICATORS:
            keyboardInitiateContext.pDevObj = pDevObj;
            keyboardInitiateContext.FirstByte = SET_KEYBOARD_INDICATORS;
            keyboardInitiateContext.LastByte  =
                (UCHAR) ((PKEYBOARD_INDICATOR_PARAMETERS)Irp->AssociatedIrp.SystemBuffer)->LedFlags;
            KeSynchronizeExecution(pDevExt->KbdIntObj, KbdStartWrapper, &keyboardInitiateContext);
            deltaTime.LowPart = (ULONG)(-10 * 1000 * 1000);
            deltaTime.HighPart = -1;
            KeSetTimer(&pDevExt->CommandTimer, deltaTime, &pDevExt->TimeOutDpc);
            break;

        case IOCTL_KEYBOARD_SET_TYPEMATIC:
            keyboardInitiateContext.pDevObj = pDevObj;
            keyboardInitiateContext.FirstByte = SET_KEYBOARD_TYPEMATIC;
            keyboardInitiateContext.LastByte  =
                ConvertTypematic(((PKEYBOARD_TYPEMATIC_PARAMETERS)Irp->AssociatedIrp.SystemBuffer)->Rate,
                                              ((PKEYBOARD_TYPEMATIC_PARAMETERS)Irp->AssociatedIrp.SystemBuffer)->Delay);

            KeSynchronizeExecution(pDevExt->KbdIntObj, KbdStartWrapper, &keyboardInitiateContext);
            deltaTime.LowPart = (ULONG)(-10 * 1000 * 1000);
            deltaTime.HighPart = -1;
            KeSetTimer(&pDevExt->CommandTimer, deltaTime, &pDevExt->TimeOutDpc);
            break;

        default:
            ASSERT(FALSE);
            break;
    }
}

/**
 * Driver's command timeout routine.
 */
static VOID CtrlTimeoutDpc(PKDPC Dpc, PDEVICE_OBJECT pDevObj, PVOID SystemContext1, PVOID SystemContext2)
{
    RT_NOREF(Dpc, SystemContext1, SystemContext2);
    PDEVEXT pDevExt = (PDEVEXT)pDevObj->DeviceExtension;

    KIRQL cancelIrql;
    IoAcquireCancelSpinLock(&cancelIrql);
    PIRP irp = pDevObj->CurrentIrp;
    if (!irp)
    {
        IoReleaseCancelSpinLock(cancelIrql);
        return;
    }
    IoSetCancelRoutine(irp, NULL);
    IoReleaseCancelSpinLock(cancelIrql);

    TIMERCTX timerContext;
    timerContext.pDevObj = pDevObj;
    timerContext.TimerCounter = &pDevExt->TimerCount;
    KeSynchronizeExecution(pDevExt->KbdIntObj, DecTimer, &timerContext);

    if (timerContext.NewTimerCount == 0)
    {
        pDevObj->CurrentIrp->IoStatus.Information = 0;
        pDevObj->CurrentIrp->IoStatus.Status = STATUS_IO_TIMEOUT;

        IoStartNextPacket(pDevObj, FALSE);
        IoCompleteRequest(irp, IO_KEYBOARD_INCREMENT);
    }
    else
    {
        LARGE_INTEGER deltaTime;
        deltaTime.LowPart = (ULONG)(-10 * 1000 * 1000);
        deltaTime.HighPart = -1;
        KeSetTimer(&pDevExt->CommandTimer, deltaTime, &pDevExt->TimeOutDpc);
    }
}

/**
 * DISPATCH_LEVEL IRQL: Finish processing for keyboard interrupts.
 */
static VOID CtrlKbdIsrDpc(PKDPC Dpc, PDEVICE_OBJECT pDevObj, PIRP Irp, PVOID pCtx)
{
    NOREF(Dpc);
    NOREF(Irp);
    NOREF(pCtx);

    PDEVEXT pDevExt = (PDEVEXT) pDevObj->DeviceExtension;

    VAROPCTX opCtx;
    LONG interlockedResult;
    opCtx.VariableAddress = &pDevExt->DpcInterlockKeyboard;
    opCtx.Operation = IncrementOperation;
    opCtx.NewValue = &interlockedResult;
    KeSynchronizeExecution(pDevExt->KbdIntObj, DpcVarOp, (PVOID)&opCtx);
    BOOLEAN fContinue = (interlockedResult == 0) ? TRUE : FALSE;

    while (fContinue)
    {
        ULONG cbNotConsumed = 0;
        ULONG inputDataConsumed = 0;

        GETDATAPTRCTX getPtrCtx;
        getPtrCtx.pDevExt = pDevExt;
        getPtrCtx.DevType = KbdDevType;
        SETDATAPTRCTX setPtrCtx;
        setPtrCtx.pDevExt = pDevExt;
        setPtrCtx.DevType = KbdDevType;
        setPtrCtx.cInput = 0;
        KeSynchronizeExecution(pDevExt->KbdIntObj, GetDataQueuePtr, &getPtrCtx);

        if (getPtrCtx.cInput)
        {
            PVOID classDeviceObject = pDevExt->KbdExt.ConnectData.ClassDeviceObject;
            PSERVICECALLBACK classService = pDevExt->KbdExt.ConnectData.ClassService;
            ASSERT(classService);

            if (getPtrCtx.DataOut >= getPtrCtx.DataIn)
            {
                classService(classDeviceObject, getPtrCtx.DataOut, pDevExt->KbdExt.DataEnd, &inputDataConsumed);
                cbNotConsumed = (((PUCHAR) pDevExt->KbdExt.DataEnd - (PUCHAR) getPtrCtx.DataOut)
                                  / sizeof(KEYBOARD_INPUT_DATA)) - inputDataConsumed;

                setPtrCtx.cInput += inputDataConsumed;

                if (cbNotConsumed)
                {
                    setPtrCtx.DataOut = ((PUCHAR)getPtrCtx.DataOut)
                                              + (inputDataConsumed * sizeof(KEYBOARD_INPUT_DATA));
                }
                else
                {
                    setPtrCtx.DataOut = pDevExt->KbdExt.InputData;
                    getPtrCtx.DataOut = setPtrCtx.DataOut;
                }
            }

            if (   cbNotConsumed == 0
                && inputDataConsumed < getPtrCtx.cInput)
            {
                classService(classDeviceObject, getPtrCtx.DataOut, getPtrCtx.DataIn, &inputDataConsumed);
                cbNotConsumed = (((PUCHAR) getPtrCtx.DataIn - (PUCHAR) getPtrCtx.DataOut)
                      / sizeof(KEYBOARD_INPUT_DATA)) - inputDataConsumed;

                setPtrCtx.DataOut = ((PUCHAR)getPtrCtx.DataOut) +
                    (inputDataConsumed * sizeof(KEYBOARD_INPUT_DATA));
                setPtrCtx.cInput += inputDataConsumed;
            }

            KeSynchronizeExecution(pDevExt->KbdIntObj, SetDataQueuePtr, &setPtrCtx);
        }

        if (cbNotConsumed)
        {
            opCtx.Operation = WriteOperation;
            interlockedResult = -1;
            opCtx.NewValue = &interlockedResult;
            KeSynchronizeExecution(pDevExt->KbdIntObj, DpcVarOp, &opCtx);

            LARGE_INTEGER deltaTime;
            deltaTime.LowPart = (ULONG)(-10 * 1000 * 1000);
            deltaTime.HighPart = -1;
            KeSetTimer(&pDevExt->KbdExt.DataConsumptionTimer, deltaTime, &pDevExt->KeyboardIsrDpcRetry);
            fContinue = FALSE;
        }
        else
        {
            opCtx.Operation = DecrementOperation;
            opCtx.NewValue = &interlockedResult;
            KeSynchronizeExecution(pDevExt->KbdIntObj, DpcVarOp, &opCtx);
            if (interlockedResult != -1)
            {
                opCtx.Operation = WriteOperation;
                interlockedResult = 0;
                opCtx.NewValue = &interlockedResult;
                KeSynchronizeExecution(pDevExt->KbdIntObj, DpcVarOp, &opCtx);
            }
            else
                fContinue = FALSE;
        }
    }
}

/**
 * DISPATCH_LEVEL IRQL: Finish processing of mouse interrupts
 */
static VOID CtrlMouIsrDpc(PKDPC Dpc, PDEVICE_OBJECT pDevObj, PIRP Irp, PVOID pCtx)
{
    NOREF(Dpc);
    NOREF(Irp);
    NOREF(pCtx);

    PDEVEXT pDevExt = (PDEVEXT) pDevObj->DeviceExtension;

    VAROPCTX opCtx;
    LONG interlockedResult;
    opCtx.VariableAddress = &pDevExt->DpcInterlockMouse;
    opCtx.Operation = IncrementOperation;
    opCtx.NewValue = &interlockedResult;
    KeSynchronizeExecution(pDevExt->MouIntObj, DpcVarOp, &opCtx);
    BOOLEAN fContinue = (interlockedResult == 0) ? TRUE : FALSE;
    while (fContinue)
    {
        ULONG cbNotConsumed = 0;
        ULONG inputDataConsumed = 0;

        GETDATAPTRCTX getPtrCtx;
        getPtrCtx.pDevExt = pDevExt;
        getPtrCtx.DevType = MouDevType;
        SETDATAPTRCTX setPtrCtx;
        setPtrCtx.pDevExt = pDevExt;
        setPtrCtx.DevType = MouDevType;
        setPtrCtx.cInput = 0;
        KeSynchronizeExecution(pDevExt->MouIntObj, GetDataQueuePtr, &getPtrCtx);
        if (getPtrCtx.cInput)
        {
            PVOID classDeviceObject = pDevExt->MouExt.ConnectData.ClassDeviceObject;
            PSERVICECALLBACK classService = pDevExt->MouExt.ConnectData.ClassService;
            ASSERT(classService);

            if (getPtrCtx.DataOut >= getPtrCtx.DataIn)
            {
                classService(classDeviceObject, getPtrCtx.DataOut, pDevExt->MouExt.DataEnd, &inputDataConsumed);
                cbNotConsumed = (((PUCHAR)pDevExt->MouExt.DataEnd - (PUCHAR) getPtrCtx.DataOut)
                                / sizeof(MOUSE_INPUT_DATA)) - inputDataConsumed;

                setPtrCtx.cInput += inputDataConsumed;
                if (cbNotConsumed)
                {
                    setPtrCtx.DataOut = ((PUCHAR)getPtrCtx.DataOut) + (inputDataConsumed * sizeof(MOUSE_INPUT_DATA));
                }
                else
                {
                    setPtrCtx.DataOut = pDevExt->MouExt.InputData;
                    getPtrCtx.DataOut = setPtrCtx.DataOut;
                }
            }

            if (   cbNotConsumed == 0
                && inputDataConsumed < getPtrCtx.cInput)
            {
                classService(classDeviceObject, getPtrCtx.DataOut, getPtrCtx.DataIn, &inputDataConsumed);
                cbNotConsumed = (((PUCHAR) getPtrCtx.DataIn - (PUCHAR) getPtrCtx.DataOut)
                                / sizeof(MOUSE_INPUT_DATA)) - inputDataConsumed;

                setPtrCtx.DataOut = ((PUCHAR)getPtrCtx.DataOut) + (inputDataConsumed * sizeof(MOUSE_INPUT_DATA));
                setPtrCtx.cInput += inputDataConsumed;
            }
            KeSynchronizeExecution(pDevExt->MouIntObj, SetDataQueuePtr, &setPtrCtx);
        }

        if (cbNotConsumed)
        {
            opCtx.Operation = WriteOperation;
            interlockedResult = -1;
            opCtx.NewValue = &interlockedResult;
            KeSynchronizeExecution(pDevExt->MouIntObj, DpcVarOp, &opCtx);

            LARGE_INTEGER deltaTime;
            deltaTime.LowPart = (ULONG)(-10 * 1000 * 1000);
            deltaTime.HighPart = -1;
            KeSetTimer(&pDevExt->MouExt.DataConsumptionTimer, deltaTime, &pDevExt->MouseIsrDpcRetry);
            fContinue = FALSE;
        }
        else
        {
            opCtx.Operation = DecrementOperation;
            opCtx.NewValue = &interlockedResult;
            KeSynchronizeExecution(pDevExt->MouIntObj, DpcVarOp, &opCtx);

            if (interlockedResult != -1)
            {
                opCtx.Operation = WriteOperation;
                interlockedResult = 0;
                opCtx.NewValue = &interlockedResult;
                KeSynchronizeExecution(pDevExt->MouIntObj, DpcVarOp, &opCtx);
            }
            else
                fContinue = FALSE;
        }
    }
}

/**
 * Interrupt service routine for the mouse device.
 */
static BOOLEAN MouIntHandler(PKINTERRUPT Interrupt, PVOID pCtx)
{
    UCHAR uPrevSignAndOverflow;

    NOREF(Interrupt);

    PDEVICE_OBJECT pDevObj = (PDEVICE_OBJECT)pCtx;
    PDEVEXT pDevExt = (PDEVEXT)pDevObj->DeviceExtension;

    if ((I8X_GET_STATUS_BYTE(pDevExt->DevRegs[i8042Cmd]) & (OUTPUT_BUFFER_FULL|MOUSE_OUTPUT_BUFFER_FULL))
                                                        != (OUTPUT_BUFFER_FULL|MOUSE_OUTPUT_BUFFER_FULL))
    {
        KeStallExecutionProcessor(10);
        if ((I8X_GET_STATUS_BYTE(pDevExt->DevRegs[i8042Cmd]) & (OUTPUT_BUFFER_FULL|MOUSE_OUTPUT_BUFFER_FULL))
                                                            != (OUTPUT_BUFFER_FULL|MOUSE_OUTPUT_BUFFER_FULL))
            return FALSE;
    }

    UCHAR byte;
    GetByteAsync(MouDevType, pDevExt, &byte);

    if (   pDevExt->MouExt.LastByteReceived == 0xaa
        && byte == 0x00)
    {
        pDevExt->HardwarePresent &= ~WHEELMOUSE_HARDWARE_PRESENT;
        pDevExt->Cfg.MouAttr.NumberOfButtons = 2;

        PutByteAsync(i8042Cmd, pDevExt, I8042_WRITE_TO_AUXILIARY_DEVICE);
        PutByteAsync(i8042Dat, pDevExt, ENABLE_MOUSE_TRANSMISSION);

        pDevExt->MouExt.InputState = MouseExpectingACK;
    }

    pDevExt->MouExt.LastByteReceived = byte;

    LARGE_INTEGER tickDelta, newTick;
    KeQueryTickCount(&newTick);
    tickDelta.QuadPart = newTick.QuadPart - pDevExt->MouExt.PreviousTick.QuadPart;
    if (   pDevExt->MouExt.InputState != MouseIdle
        && pDevExt->MouExt.InputState != MouseExpectingACK
        && (tickDelta.LowPart >= pDevExt->MouExt.SynchTickCount || tickDelta.HighPart != 0))
        pDevExt->MouExt.InputState = MouseIdle;
    pDevExt->MouExt.PreviousTick = newTick;

    switch (pDevExt->MouExt.InputState)
    {
        case MouseIdle:
        {
            UCHAR fPrevBtns = pDevExt->MouExt.PreviousButtons;
            pDevExt->MouExt.CurrentInput.ButtonFlags = 0;
            pDevExt->MouExt.CurrentInput.ButtonData = 0;

            if ((!(fPrevBtns & LEFT_BUTTON_DOWN)) && (byte & LEFT_BUTTON_DOWN))
                pDevExt->MouExt.CurrentInput.ButtonFlags |= MOUSE_LEFT_BUTTON_DOWN;
            else if ((fPrevBtns & LEFT_BUTTON_DOWN) && !(byte & LEFT_BUTTON_DOWN))
                pDevExt->MouExt.CurrentInput.ButtonFlags |= MOUSE_LEFT_BUTTON_UP;
            if ((!(fPrevBtns & RIGHT_BUTTON_DOWN)) && (byte & RIGHT_BUTTON_DOWN))
                pDevExt->MouExt.CurrentInput.ButtonFlags |= MOUSE_RIGHT_BUTTON_DOWN;
            else if ((fPrevBtns & RIGHT_BUTTON_DOWN) && !(byte & RIGHT_BUTTON_DOWN))
                pDevExt->MouExt.CurrentInput.ButtonFlags |= MOUSE_RIGHT_BUTTON_UP;
            if ((!(fPrevBtns & MIDDLE_BUTTON_DOWN)) && (byte & MIDDLE_BUTTON_DOWN))
                pDevExt->MouExt.CurrentInput.ButtonFlags |= MOUSE_MIDDLE_BUTTON_DOWN;
            else if ((fPrevBtns & MIDDLE_BUTTON_DOWN) && !(byte & MIDDLE_BUTTON_DOWN))
                pDevExt->MouExt.CurrentInput.ButtonFlags |= MOUSE_MIDDLE_BUTTON_UP;

            pDevExt->MouExt.PreviousButtons = byte & (RIGHT_BUTTON_DOWN|MIDDLE_BUTTON_DOWN|LEFT_BUTTON_DOWN);
            pDevExt->MouExt.uCurrSignAndOverflow = (UCHAR) (byte & MOUSE_SIGN_OVERFLOW_MASK);
            pDevExt->MouExt.InputState = XMovement;
            break;
        }

        case XMovement:
        {
            if (pDevExt->MouExt.uCurrSignAndOverflow & X_OVERFLOW)
            {
                uPrevSignAndOverflow = pDevExt->MouExt.uPrevSignAndOverflow;
                if (uPrevSignAndOverflow & X_OVERFLOW)
                {
                    if ((uPrevSignAndOverflow & X_DATA_SIGN) != (pDevExt->MouExt.uCurrSignAndOverflow & X_DATA_SIGN))
                        pDevExt->MouExt.uCurrSignAndOverflow ^= X_DATA_SIGN;
                }
                if (pDevExt->MouExt.uCurrSignAndOverflow & X_DATA_SIGN)
                    pDevExt->MouExt.CurrentInput.LastX = MOUSE_MAXIMUM_NEGATIVE_DELTA;
                else
                    pDevExt->MouExt.CurrentInput.LastX = MOUSE_MAXIMUM_POSITIVE_DELTA;
            }
            else
            {
                pDevExt->MouExt.CurrentInput.LastX = (ULONG) byte;
                if (pDevExt->MouExt.uCurrSignAndOverflow & X_DATA_SIGN)
                    pDevExt->MouExt.CurrentInput.LastX |= MOUSE_MAXIMUM_NEGATIVE_DELTA;
            }
            pDevExt->MouExt.InputState = YMovement;
            break;
        }

        case YMovement:
        {
            if (pDevExt->MouExt.uCurrSignAndOverflow & Y_OVERFLOW)
            {
                uPrevSignAndOverflow = pDevExt->MouExt.uPrevSignAndOverflow;
                if (uPrevSignAndOverflow & Y_OVERFLOW)
                {
                    if ((uPrevSignAndOverflow & Y_DATA_SIGN) != (pDevExt->MouExt.uCurrSignAndOverflow & Y_DATA_SIGN))
                        pDevExt->MouExt.uCurrSignAndOverflow ^= Y_DATA_SIGN;
                }
                if (pDevExt->MouExt.uCurrSignAndOverflow & Y_DATA_SIGN)
                    pDevExt->MouExt.CurrentInput.LastY = MOUSE_MAXIMUM_POSITIVE_DELTA;
                else
                    pDevExt->MouExt.CurrentInput.LastY = MOUSE_MAXIMUM_NEGATIVE_DELTA;
            }
            else
            {
                pDevExt->MouExt.CurrentInput.LastY = (ULONG) byte;
                if (pDevExt->MouExt.uCurrSignAndOverflow & Y_DATA_SIGN)
                    pDevExt->MouExt.CurrentInput.LastY |= MOUSE_MAXIMUM_NEGATIVE_DELTA;
                 pDevExt->MouExt.CurrentInput.LastY = -pDevExt->MouExt.CurrentInput.LastY;
            }
            pDevExt->MouExt.uPrevSignAndOverflow = pDevExt->MouExt.uCurrSignAndOverflow;

            if (pDevExt->HardwarePresent & WHEELMOUSE_HARDWARE_PRESENT)
                pDevExt->MouExt.InputState = ZMovement;
            else
            {
                pDevExt->MouExt.CurrentInput.Flags = MOUSE_MOVE_RELATIVE;

                {
                    VMMDevReqMouseStatus *pReq = pDevExt->pReq;
                    if (pReq)
                    {
                        int rc = VbglR0GRPerform (&pReq->header);
                        if (RT_SUCCESS(rc))
                        {
                            if (pReq->mouseFeatures & VMMDEV_MOUSE_HOST_WANTS_ABSOLUTE)
                            {
                                /* make it an absolute move */
                                pDevExt->MouExt.CurrentInput.Flags = MOUSE_MOVE_ABSOLUTE;
                                pDevExt->MouExt.CurrentInput.LastX = pReq->pointerXPos;
                                pDevExt->MouExt.CurrentInput.LastY = pReq->pointerYPos;
                            }
                        }
                        else
                            Log(("VBoxMouseNT: ERROR querying mouse capabilities from VMMDev. rc = %Rrc\n", rc));
                    }
                }
                QueueInput(pDevObj);
                pDevExt->MouExt.InputState = MouseIdle;
            }
            break;
        }

        case ZMovement:
        {
#if 0
            if (byte && pDevExt->MouExt.CurrentInput.Buttons == 0)
#else
            if (byte)
#endif
            {
                if (byte & 0x80)
                    pDevExt->MouExt.CurrentInput.ButtonData = 0x0078;
                else
                    pDevExt->MouExt.CurrentInput.ButtonData = 0xFF88;
                pDevExt->MouExt.CurrentInput.ButtonFlags |= MOUSE_WHEEL;
            }

            pDevExt->MouExt.CurrentInput.Flags = MOUSE_MOVE_RELATIVE;

            {
                VMMDevReqMouseStatus *pReq = pDevExt->pReq;
                if (pReq)
                {
                    int rc = VbglR0GRPerform(&pReq->header);
                    if (RT_SUCCESS(rc))
                    {
                        if (pReq->mouseFeatures & VMMDEV_MOUSE_HOST_WANTS_ABSOLUTE)
                        {
                            /* make it an absolute move */
                            pDevExt->MouExt.CurrentInput.Flags = MOUSE_MOVE_ABSOLUTE;
                            pDevExt->MouExt.CurrentInput.LastX = pReq->pointerXPos;
                            pDevExt->MouExt.CurrentInput.LastY = pReq->pointerYPos;
                        }
                    }
                    else
                        Log(("VBoxMouseNT: ERROR querying mouse capabilities from VMMDev. rc = %Rrc\n", rc));
                }
            }

            QueueInput(pDevObj);
            pDevExt->MouExt.InputState = MouseIdle;
            break;
        }

        case MouseExpectingACK:
        {
            if (byte == ACKNOWLEDGE)
                pDevExt->MouExt.InputState = MouseIdle;
            else if (byte == RESEND)
            {
                PutByteAsync(i8042Cmd, pDevExt, I8042_WRITE_TO_AUXILIARY_DEVICE);
                PutByteAsync(i8042Dat, pDevExt, ENABLE_MOUSE_TRANSMISSION);
            }
            break;
        }

        default:
        {
            ASSERT(FALSE);
            break;
        }
    }

    return TRUE;
}

/**
 * Interrupt service routine.
 */
static BOOLEAN KbdIntHandler(PKINTERRUPT Interrupt, PVOID pCtx)
{
    NOREF(Interrupt);

    PDEVICE_OBJECT pDevObj = (PDEVICE_OBJECT)pCtx;
    PDEVEXT pDevExt = (PDEVEXT) pDevObj->DeviceExtension;

    if ((I8X_GET_STATUS_BYTE(pDevExt->DevRegs[i8042Cmd]) & (OUTPUT_BUFFER_FULL|MOUSE_OUTPUT_BUFFER_FULL))
                                                         != OUTPUT_BUFFER_FULL)
    {
        for (unsigned i = 0; i < (ULONG)pDevExt->Cfg.PollStatusIterations; i++)
        {
            KeStallExecutionProcessor(1);
            if ((I8X_GET_STATUS_BYTE(pDevExt->DevRegs[i8042Cmd]) & (OUTPUT_BUFFER_FULL|MOUSE_OUTPUT_BUFFER_FULL))
                                                                == (OUTPUT_BUFFER_FULL))
                break;
        }

        if ((I8X_GET_STATUS_BYTE(pDevExt->DevRegs[i8042Cmd]) & (OUTPUT_BUFFER_FULL|MOUSE_OUTPUT_BUFFER_FULL))
                                                            != (OUTPUT_BUFFER_FULL))
        {
            if (pDevExt->KeyboardEnableCount == 0)
            {
                UCHAR scanCode = I8X_GET_DATA_BYTE(pDevExt->DevRegs[i8042Dat]);
                NOREF(scanCode);
            }
            return FALSE;
        }
    }

    UCHAR scanCode;
    GetByteAsync(KbdDevType, pDevExt, &scanCode);
    switch (scanCode)
    {
        case RESEND:
            if (pDevExt->TimerCount == 0)
                break;
            pDevExt->TimerCount = -1;

            if (   pDevExt->KbdExt.CurrentOutput.State==Idle
                || !pDevObj->CurrentIrp)
                goto ScanCodeCase;
            else if (pDevExt->KbdExt.ResendCount < pDevExt->Cfg.iResend)
            {
                pDevExt->KbdExt.ResendCount++;
                KbdStartIO(pDevObj);
            }
            else
            {
                pDevExt->KbdExt.CurrentOutput.State = Idle;
                KeInsertQueueDpc(&pDevExt->RetriesExceededDpc, pDevObj->CurrentIrp, NULL);
            }
            break;

        case ACKNOWLEDGE:
            if (pDevExt->TimerCount == 0)
                break;

            pDevExt->TimerCount = -1;

            pDevExt->KbdExt.ResendCount = 0;
            if (pDevExt->KbdExt.CurrentOutput.State == SendFirstByte)
            {
                pDevExt->KbdExt.CurrentOutput.State = SendLastByte;
                KbdStartIO(pDevObj);
            }
            else if (pDevExt->KbdExt.CurrentOutput.State == SendLastByte)
            {
                pDevExt->KbdExt.CurrentOutput.State = Idle;
                IoRequestDpc(pDevObj, pDevObj->CurrentIrp, NULL);
            }
            break;

        ScanCodeCase:
        default:
        {
            PKEYBOARD_INPUT_DATA input = &pDevExt->KbdExt.CurrentInput;
            KBDSCANSTATE *pScanState = &pDevExt->KbdExt.CurrentScanState;

            if (scanCode == (UCHAR) 0xFF)
            {
                input->MakeCode = KEYBOARD_OVERRUN_MAKE_CODE;
                input->Flags = 0;
                *pScanState = Normal;
            }
            else
            {
                switch (*pScanState)
                {
                    case Normal:
                        if (scanCode == (UCHAR) 0xE0)
                        {
                            input->Flags |= KEY_E0;
                            *pScanState = GotE0;
                            break;
                        }
                        else if (scanCode == (UCHAR) 0xE1)
                        {
                            input->Flags |= KEY_E1;
                            *pScanState = GotE1;
                            break;
                        }
                        /* fall through */
                  case GotE0:
                  case GotE1:
                    if (scanCode > 0x7F)
                    {
                        input->MakeCode = scanCode & 0x7F;
                        input->Flags |= KEY_BREAK;
                    }
                    else
                        input->MakeCode = scanCode;
                    *pScanState = Normal;
                    break;

                  default:
                    ASSERT(FALSE);
                    break;
                }
            }

            if (*pScanState == Normal)
            {
                if (pDevExt->KeyboardEnableCount)
                {
                    pDevExt->KbdExt.CurrentInput.UnitId = pDevExt->KbdExt.UnitId;
                    if (!KbdDataToQueue(&pDevExt->KbdExt, input))
                    {
                    }
                    else if (pDevExt->DpcInterlockKeyboard >= 0)
                       pDevExt->DpcInterlockKeyboard++;
                    else
                        KeInsertQueueDpc(&pDevExt->KeyboardIsrDpc, pDevObj->CurrentIrp, NULL);
                }
                input->Flags = 0;
            }
            break;
        }
    }
    return TRUE;
}

static NTSTATUS MouEnableTrans(PDEVICE_OBJECT pDevObj)
{
    NTSTATUS status;

    PDEVEXT pDevExt = (PDEVEXT) pDevObj->DeviceExtension;
    status = PutBytePoll(i8042Dat, FALSE /*=wait*/, MouDevType, pDevExt, ENABLE_MOUSE_TRANSMISSION);
    return status;
}

/**
 * Configuration information for the keyboard.
 */
static VOID KbdGetRegstry(PINITEXT pInit, PUNICODE_STRING RegistryPath,
                          PUNICODE_STRING KeyboardDeviceName, PUNICODE_STRING PointerDeviceName)
{
    PDEVEXT pDevExt = &pInit->DevExt;
    for (unsigned i = 0; i < MaximumInterfaceType; i++)
    {
        INTERFACE_TYPE interfaceType = (INTERFACE_TYPE)i;
        CONFIGURATION_TYPE controllerType = KeyboardController;
        CONFIGURATION_TYPE peripheralType = KeyboardPeripheral;
        NTSTATUS status = IoQueryDeviceDescription(&interfaceType, NULL,
                                                   &controllerType, NULL,
                                                   &peripheralType, NULL,
                                                   KbdCallOut, pInit);
        NOREF(status); /* how diligent of us */

        if (pDevExt->HardwarePresent & KEYBOARD_HARDWARE_PRESENT)
        {
            HwGetRegstry(pInit, RegistryPath, KeyboardDeviceName, PointerDeviceName);

            PI8042CFGINF pCfg = &pInit->DevExt.Cfg;
            PKEYBOARD_ID keyboardId = &pCfg->KbdAttr.KeyboardIdentifier;
            if (!ENHANCED_KEYBOARD(*keyboardId))
                pCfg->PollingIterations = pCfg->PollingIterationsMaximum;

            pCfg->KbdAttr.NumberOfFunctionKeys   = s_aKeybType[keyboardId->Type-1].cFunctionKeys;
            pCfg->KbdAttr.NumberOfIndicators     = s_aKeybType[keyboardId->Type-1].cIndicators;
            pCfg->KbdAttr.NumberOfKeysTotal      = s_aKeybType[keyboardId->Type-1].cKeysTotal;
            pCfg->KbdAttr.KeyboardMode           = 1;
            pCfg->KbdAttr.KeyRepeatMinimum.Rate  = 2;
            pCfg->KbdAttr.KeyRepeatMinimum.Delay = 250;
            pCfg->KbdAttr.KeyRepeatMaximum.Rate  = 30;
            pCfg->KbdAttr.KeyRepeatMaximum.Delay = 1000;
            pCfg->KeyRepeatCurrent.Rate          = 30;
            pCfg->KeyRepeatCurrent.Delay         = 250;
            break;
        }
    }
}

/**
 * Retrieve the configuration information for the mouse.
 */
static VOID MouGetRegstry(PINITEXT pInit, PUNICODE_STRING RegistryPath,
                          PUNICODE_STRING KeyboardDeviceName, PUNICODE_STRING PointerDeviceName)
{
    NTSTATUS status = STATUS_SUCCESS;
    INTERFACE_TYPE interfaceType;
    CONFIGURATION_TYPE controllerType = PointerController;
    CONFIGURATION_TYPE peripheralType = PointerPeripheral;

    for (unsigned i = 0; i < MaximumInterfaceType; i++)
    {
        interfaceType = (INTERFACE_TYPE)i;
        status = IoQueryDeviceDescription(&interfaceType, NULL,
                                          &controllerType, NULL,
                                          &peripheralType, NULL,
                                          MouCallOut, pInit);

        if (pInit->DevExt.HardwarePresent & MOUSE_HARDWARE_PRESENT)
        {
            if (!(pInit->DevExt.HardwarePresent & KEYBOARD_HARDWARE_PRESENT))
                HwGetRegstry(pInit, RegistryPath, KeyboardDeviceName, PointerDeviceName);
            pInit->DevExt.Cfg.MouAttr.MouseIdentifier = MOUSE_I8042_HARDWARE;
            break;
        }
    }
}

/**
 * Initialize the driver.
 */
NTSTATUS DriverEntry(PDRIVER_OBJECT pDrvObj, PUNICODE_STRING RegistryPath)
{
    PDEVICE_OBJECT pPortDevObj = NULL;
    PDEVEXT pDevExt = NULL;
    NTSTATUS status = STATUS_SUCCESS;
    KIRQL IrqlCoord = 0;
    ULONG IntVecKbd;
    ULONG IntVecMou;
    KIRQL IrqlKbd;
    KIRQL IrqlMou;
    KAFFINITY AffKbd;
    KAFFINITY AffMou;
    ULONG addressSpace;
    PHYSICAL_ADDRESS Phys;
    BOOLEAN fConflict;

    ULONG resourceListSize = 0;
    PCM_RESOURCE_LIST resources = NULL;

    UNICODE_STRING KbdNameFull          = { 0, 0, NULL };
    UNICODE_STRING MouNameFull          = { 0, 0, NULL };
    UNICODE_STRING KbdNameBase          = { 0, 0, NULL };
    UNICODE_STRING MouNameBase          = { 0, 0, NULL };
    UNICODE_STRING DevNameSuff          = { 0, 0, NULL };
    UNICODE_STRING resourceDeviceClass  = { 0, 0, NULL };
    UNICODE_STRING registryPath         = { 0, 0, NULL };

#define NAME_MAX 256
    WCHAR keyboardBuffer[NAME_MAX];
    WCHAR pointerBuffer[NAME_MAX];

    int rc = RTR0Init(0);
    if (RT_FAILURE(rc))
        return STATUS_UNSUCCESSFUL;

    LogFlow(("VBoxMouseNT::DriverEntry: enter\n"));

    PINITEXT pInit = (PINITEXT)ExAllocatePool(NonPagedPool, sizeof(INITEXT));
    if (!pInit)
    {
        status = STATUS_UNSUCCESSFUL;
        goto fail;
    }

    RtlZeroMemory(pInit, sizeof(INITEXT));
    RtlZeroMemory(keyboardBuffer, NAME_MAX * sizeof(WCHAR));
    KbdNameBase.Buffer = keyboardBuffer;
    KbdNameBase.Length = 0;
    KbdNameBase.MaximumLength = NAME_MAX * sizeof(WCHAR);
    RtlZeroMemory(pointerBuffer, NAME_MAX * sizeof(WCHAR));
    MouNameBase.Buffer = pointerBuffer;
    MouNameBase.Length = 0;
    MouNameBase.MaximumLength = NAME_MAX * sizeof(WCHAR);

    registryPath.Buffer = (PWSTR)ExAllocatePool(PagedPool, RegistryPath->Length + sizeof(UNICODE_NULL));
    if (!registryPath.Buffer)
    {
        status = STATUS_UNSUCCESSFUL;
        goto fail;
    }
    else
    {
        registryPath.Length = RegistryPath->Length + sizeof(UNICODE_NULL);
        registryPath.MaximumLength = registryPath.Length;

        RtlZeroMemory(registryPath.Buffer, registryPath.Length);
        RtlMoveMemory(registryPath.Buffer, RegistryPath->Buffer, RegistryPath->Length);
    }

    KbdGetRegstry(pInit, &registryPath, &KbdNameBase, &MouNameBase);
    MouGetRegstry(pInit, &registryPath, &KbdNameBase, &MouNameBase);
    if (pInit->DevExt.HardwarePresent == 0)
    {
        status = STATUS_NO_SUCH_DEVICE;
        goto fail;
    }
    else if (!(pInit->DevExt.HardwarePresent & KEYBOARD_HARDWARE_PRESENT))
        status = STATUS_NO_SUCH_DEVICE;

    RtlInitUnicodeString(&DevNameSuff, NULL);

    DevNameSuff.MaximumLength = (KEYBOARD_PORTS_MAXIMUM > POINTER_PORTS_MAXIMUM)
                                 ? KEYBOARD_PORTS_MAXIMUM * sizeof(WCHAR)
                                 : POINTER_PORTS_MAXIMUM * sizeof(WCHAR);
    DevNameSuff.MaximumLength += sizeof(UNICODE_NULL);
    DevNameSuff.Buffer = (PWSTR)ExAllocatePool(PagedPool, DevNameSuff.MaximumLength);
    if (!DevNameSuff.Buffer)
    {
        status = STATUS_UNSUCCESSFUL;
        goto fail;
    }

    RtlZeroMemory(DevNameSuff.Buffer, DevNameSuff.MaximumLength);

    RtlInitUnicodeString(&KbdNameFull, NULL);
    KbdNameFull.MaximumLength = sizeof(L"\\Device\\") + KbdNameBase.Length + DevNameSuff.MaximumLength;
    KbdNameFull.Buffer = (PWSTR)ExAllocatePool(PagedPool, KbdNameFull.MaximumLength);
    if (!KbdNameFull.Buffer)
    {
        status = STATUS_UNSUCCESSFUL;
        goto fail;
    }

    RtlZeroMemory(KbdNameFull.Buffer, KbdNameFull.MaximumLength);
    RtlAppendUnicodeToString(&KbdNameFull, L"\\Device\\");
    RtlAppendUnicodeToString(&KbdNameFull, KbdNameBase.Buffer);

    for (unsigned i = 0; i < KEYBOARD_PORTS_MAXIMUM; i++)
    {
        status = RtlIntegerToUnicodeString(i, 10, &DevNameSuff);
        if (!NT_SUCCESS(status))
            break;
        RtlAppendUnicodeStringToString(&KbdNameFull, &DevNameSuff);

        LogFlow(("VBoxMouseNT::DriverEntry: Creating device object named %S\n", KbdNameFull.Buffer));

        status = IoCreateDevice(pDrvObj, sizeof(DEVEXT), &KbdNameFull,
                                FILE_DEVICE_8042_PORT, 0, FALSE, &pPortDevObj);
        if (NT_SUCCESS(status))
            break;
        else
           KbdNameFull.Length -= DevNameSuff.Length;
    }

    if (!NT_SUCCESS(status))
        goto fail;

    pDevExt = (PDEVEXT)pPortDevObj->DeviceExtension;
    *pDevExt = pInit->DevExt;
    pDevExt->pDevObj = pPortDevObj;

    CreateResList(pDevExt, &resources, &resourceListSize);

    RtlInitUnicodeString(&resourceDeviceClass, NULL);

    resourceDeviceClass.MaximumLength = KbdNameBase.Length + sizeof(L"/") + MouNameBase.Length;
    resourceDeviceClass.Buffer = (PWSTR)ExAllocatePool(PagedPool, resourceDeviceClass.MaximumLength);
    if (!resourceDeviceClass.Buffer)
    {
        status = STATUS_UNSUCCESSFUL;
        goto fail;
    }

    RtlZeroMemory(resourceDeviceClass.Buffer, resourceDeviceClass.MaximumLength);
    RtlAppendUnicodeStringToString(&resourceDeviceClass, &KbdNameBase);
    RtlAppendUnicodeToString(&resourceDeviceClass, L"/");
    RtlAppendUnicodeStringToString(&resourceDeviceClass, &MouNameBase);

    IoReportResourceUsage(&resourceDeviceClass, pDrvObj, NULL, 0, pPortDevObj,
                          resources, resourceListSize, FALSE, &fConflict);
    if (fConflict)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto fail;
    }

    for (unsigned i = 0; i < pDevExt->Cfg.cPorts; i++)
    {
        addressSpace = (pDevExt->Cfg.aPorts[i].Flags & CM_RESOURCE_PORT_IO) == CM_RESOURCE_PORT_IO ? 1 : 0;
        if (!HalTranslateBusAddress(pDevExt->Cfg.InterfaceType,
                                    pDevExt->Cfg.uBusNr,
                                    pDevExt->Cfg.aPorts[i].u.Port.Start,
                                    &addressSpace, &Phys))
        {
            addressSpace = 1;
            Phys.QuadPart = 0;
        }

        if (!addressSpace)
        {
            pDevExt->fUnmapRegs = TRUE;
            pDevExt->DevRegs[i] = (PUCHAR)MmMapIoSpace(Phys, pDevExt->Cfg.aPorts[i].u.Port.Length,
                                                       (MEMORY_CACHING_TYPE)FALSE);
        }
        else
        {
            pDevExt->fUnmapRegs = FALSE;
            pDevExt->DevRegs[i] = (PUCHAR)Phys.LowPart;
        }

        if (!pDevExt->DevRegs[i])
        {
            status = STATUS_NONE_MAPPED;
            goto fail;
        }
    }

    pPortDevObj->Flags |= DO_BUFFERED_IO;

    InitHw(pPortDevObj);

    KeInitializeSpinLock(&pDevExt->ShIntObj);

    if (pDevExt->HardwarePresent & KEYBOARD_HARDWARE_PRESENT)
    {
        pDevExt->KbdExt.InputData = (PKEYBOARD_INPUT_DATA)ExAllocatePool(NonPagedPool, pDevExt->Cfg.KbdAttr.InputDataQueueLength);
        if (!pDevExt->KbdExt.InputData)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
            goto fail;
        }

        pDevExt->KbdExt.DataEnd =
            (PKEYBOARD_INPUT_DATA)((PCHAR) (pDevExt->KbdExt.InputData) + pDevExt->Cfg.KbdAttr.InputDataQueueLength);

        RtlZeroMemory(pDevExt->KbdExt.InputData, pDevExt->Cfg.KbdAttr.InputDataQueueLength);
    }

    if (pDevExt->HardwarePresent & MOUSE_HARDWARE_PRESENT)
    {
        RtlInitUnicodeString(&MouNameFull, NULL);

        MouNameFull.MaximumLength = sizeof(L"\\Device\\") + MouNameBase.Length + DevNameSuff.MaximumLength;
        MouNameFull.Buffer = (PWSTR)ExAllocatePool(PagedPool, MouNameFull.MaximumLength);
        if (!MouNameFull.Buffer)
        {
            status = STATUS_UNSUCCESSFUL;
            goto fail;
        }

        RtlZeroMemory(MouNameFull.Buffer, MouNameFull.MaximumLength);
        RtlAppendUnicodeToString(&MouNameFull, L"\\Device\\");
        RtlAppendUnicodeToString(&MouNameFull, MouNameBase.Buffer);

        RtlZeroMemory(DevNameSuff.Buffer, DevNameSuff.MaximumLength);
        DevNameSuff.Length = 0;

        for (unsigned i = 0; i < POINTER_PORTS_MAXIMUM; i++)
        {
            status = RtlIntegerToUnicodeString(i, 10, &DevNameSuff);
            if (!NT_SUCCESS(status))
                break;

            RtlAppendUnicodeStringToString(&MouNameFull, &DevNameSuff);
            LogFlow(("VBoxMouseNT::DriverEntry: pointer port name (symbolic link) = %S\n", MouNameFull.Buffer));

            status = IoCreateSymbolicLink(&MouNameFull, &KbdNameFull);
            if (NT_SUCCESS(status))
                break;
            else
               MouNameFull.Length -= DevNameSuff.Length;
        }
        if (!NT_SUCCESS(status))
            goto fail;

        pDevExt->MouExt.InputData = (PMOUSE_INPUT_DATA)ExAllocatePool(NonPagedPool, pDevExt->Cfg.MouAttr.InputDataQueueLength);
        if (!pDevExt->MouExt.InputData)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
            goto fail;
        }

        pDevExt->MouExt.DataEnd = (PMOUSE_INPUT_DATA)((PCHAR) (pDevExt->MouExt.InputData) + pDevExt->Cfg.MouAttr.InputDataQueueLength);

        RtlZeroMemory(pDevExt->MouExt.InputData, pDevExt->Cfg.MouAttr.InputDataQueueLength);
    }

    pDevExt->KbdExt.ConnectData.ClassDeviceObject = NULL;
    pDevExt->KbdExt.ConnectData.ClassService = NULL;
    pDevExt->MouExt.ConnectData.ClassDeviceObject = NULL;
    pDevExt->MouExt.ConnectData.ClassService = NULL;

    I8042INITDATACTX initDataCtx;
    initDataCtx.pDevExt = pDevExt;
    initDataCtx.DevType = KbdDevType;
    InitDataQueue(&initDataCtx);
    initDataCtx.DevType = MouDevType;
    InitDataQueue(&initDataCtx);

    pDevExt->DpcInterlockKeyboard = -1;
    pDevExt->DpcInterlockMouse = -1;

    IoInitializeDpcRequest(pPortDevObj, CompleteDpc);
    KeInitializeDpc(&pDevExt->RetriesExceededDpc,  (PKDEFERRED_ROUTINE)CtrlRetriesExceededDpc, pPortDevObj);
    KeInitializeDpc(&pDevExt->KeyboardIsrDpc,      (PKDEFERRED_ROUTINE)CtrlKbdIsrDpc, pPortDevObj);
    KeInitializeDpc(&pDevExt->KeyboardIsrDpcRetry, (PKDEFERRED_ROUTINE)CtrlKbdIsrDpc, pPortDevObj);
    KeInitializeDpc(&pDevExt->MouseIsrDpc,         (PKDEFERRED_ROUTINE)CtrlMouIsrDpc, pPortDevObj);
    KeInitializeDpc(&pDevExt->MouseIsrDpcRetry,    (PKDEFERRED_ROUTINE)CtrlMouIsrDpc, pPortDevObj);
    KeInitializeDpc(&pDevExt->TimeOutDpc,          (PKDEFERRED_ROUTINE)CtrlTimeoutDpc, pPortDevObj);

    KeInitializeTimer(&pDevExt->CommandTimer);
    pDevExt->TimerCount = -1;

    KeInitializeTimer(&pDevExt->KbdExt.DataConsumptionTimer);
    KeInitializeTimer(&pDevExt->MouExt.DataConsumptionTimer);

    IntVecKbd = HalGetInterruptVector(pDevExt->Cfg.InterfaceType,
                                      pDevExt->Cfg.uBusNr,
                                      pDevExt->Cfg.KbdInt.u.Interrupt.Level,
                                      pDevExt->Cfg.KbdInt.u.Interrupt.Vector,
                                      &IrqlKbd, &AffKbd);

    IntVecMou = HalGetInterruptVector(pDevExt->Cfg.InterfaceType,
                                      pDevExt->Cfg.uBusNr,
                                      pDevExt->Cfg.MouInt.u.Interrupt.Level,
                                      pDevExt->Cfg.MouInt.u.Interrupt.Vector,
                                      &IrqlMou, &AffMou);

    if (   (pDevExt->HardwarePresent & (KEYBOARD_HARDWARE_PRESENT | MOUSE_HARDWARE_PRESENT))
        ==                             (KEYBOARD_HARDWARE_PRESENT | MOUSE_HARDWARE_PRESENT))
        IrqlCoord = IrqlKbd > IrqlMou ? IrqlKbd : IrqlMou;

    if (pDevExt->HardwarePresent & MOUSE_HARDWARE_PRESENT)
    {
        status = IoConnectInterrupt(&pDevExt->MouIntObj, MouIntHandler, pPortDevObj,
                                    &pDevExt->ShIntObj, IntVecMou, IrqlMou,
                                    (KIRQL) ((IrqlCoord == (KIRQL)0) ? IrqlMou : IrqlCoord),
                                    pDevExt->Cfg.MouInt.Flags == CM_RESOURCE_INTERRUPT_LATCHED
                                                                       ? Latched : LevelSensitive,
                                    pDevExt->Cfg.MouInt.ShareDisposition,
                                    AffMou, pDevExt->Cfg.fFloatSave);
        if (!NT_SUCCESS(status))
            goto fail;

        status = MouEnableTrans(pPortDevObj);
        if (!NT_SUCCESS(status))
            status = STATUS_SUCCESS;
    }

    if (pDevExt->HardwarePresent & KEYBOARD_HARDWARE_PRESENT)
    {
        status = IoConnectInterrupt(&pDevExt->KbdIntObj, KbdIntHandler, pPortDevObj,
                                    &pDevExt->ShIntObj, IntVecKbd, IrqlKbd,
                                    (KIRQL) ((IrqlCoord == (KIRQL)0) ? IrqlKbd : IrqlCoord),
                                    pDevExt->Cfg.KbdInt.Flags == CM_RESOURCE_INTERRUPT_LATCHED
                                                                     ? Latched : LevelSensitive,
                                    pDevExt->Cfg.KbdInt.ShareDisposition,
                                    AffKbd, pDevExt->Cfg.fFloatSave);
        if (!NT_SUCCESS(status))
            goto fail;
    }

    if (pDevExt->HardwarePresent & KEYBOARD_HARDWARE_PRESENT)
    {
        status = RtlWriteRegistryValue(RTL_REGISTRY_DEVICEMAP,
                                       KbdNameBase.Buffer, KbdNameFull.Buffer,
                                       REG_SZ,
                                       registryPath.Buffer, registryPath.Length);
        if (!NT_SUCCESS(status))
            goto fail;
    }

    if (pDevExt->HardwarePresent & MOUSE_HARDWARE_PRESENT)
    {
        status = RtlWriteRegistryValue(RTL_REGISTRY_DEVICEMAP,
                                       MouNameBase.Buffer, MouNameFull.Buffer,
                                       REG_SZ,
                                       registryPath.Buffer, registryPath.Length);
        if (!NT_SUCCESS(status))
            goto fail;
    }

    ASSERT(status == STATUS_SUCCESS);

    int rcVBox = VbglR0InitClient();
    if (RT_FAILURE(rcVBox))
    {
        Log(("VBoxMouseNT::DriverEntry: could not initialize guest library, rc = %Rrc\n", rcVBox));
        /* Continue working in non-VBox mode. */
    }
    else
    {
        VMMDevReqMouseStatus *pReq = NULL;

        rcVBox = VbglR0GRAlloc((VMMDevRequestHeader**)&pReq, sizeof(VMMDevReqMouseStatus), VMMDevReq_SetMouseStatus);
        if (RT_SUCCESS(rcVBox))
        {
            /* Inform host that we support absolute */
            pReq->mouseFeatures = VMMDEV_MOUSE_GUEST_CAN_ABSOLUTE;
            pReq->pointerXPos = 0;
            pReq->pointerYPos = 0;
            rcVBox = VbglR0GRPerform(&pReq->header);
            if (RT_FAILURE(rcVBox))
                Log(("VBoxMouseNT::DriverEntry: ERROR communicating new mouse capabilities to VMMDev. rc = %Rrc\n", rcVBox));
            else
            {
                /* We will use the allocated request buffer in the ServiceCallback to GET mouse status. */
                pReq->header.requestType = VMMDevReq_GetMouseStatus;
                pDevExt->pReq = pReq;
            }
        }
        else
        {
            VbglR0TerminateClient();
            Log(("VBoxMouseNT::DriverEntry: could not allocate request buffer, rc = %Rrc\n", rcVBox));
            /* Continue working in non-VBox mode. */
        }
    }

    pDrvObj->DriverStartIo                                 = I8042StartIo;
    pDrvObj->MajorFunction[IRP_MJ_CREATE]                  = I8042OpenClose;
    pDrvObj->MajorFunction[IRP_MJ_CLOSE]                   = I8042OpenClose;
    pDrvObj->MajorFunction[IRP_MJ_FLUSH_BUFFERS]           = I8042Flush;
    pDrvObj->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = I8042DevCtrl;

fail:
    if (!NT_SUCCESS(status))
    {
        if (resources)
        {
            resources->Count = 0;
            IoReportResourceUsage(&resourceDeviceClass, pDrvObj, NULL,
                                  0, pPortDevObj, resources, resourceListSize, FALSE, &fConflict);
        }

        if (pDevExt)
        {
            if (pDevExt->KbdIntObj)
                IoDisconnectInterrupt(pDevExt->KbdIntObj);
            if (pDevExt->MouIntObj)
                IoDisconnectInterrupt(pDevExt->MouIntObj);
            if (pDevExt->KbdExt.InputData)
                ExFreePool(pDevExt->KbdExt.InputData);
            if (pDevExt->MouExt.InputData)
                ExFreePool(pDevExt->MouExt.InputData);
            if (pDevExt->fUnmapRegs)
            {
                for (unsigned i = 0; i < pDevExt->Cfg.cPorts; i++)
                    if (pDevExt->DevRegs[i])
                        MmUnmapIoSpace(pDevExt->DevRegs[i], pDevExt->Cfg.aPorts[i].u.Port.Length);
            }
        }
        if (pPortDevObj)
        {
            if (MouNameFull.Length > 0)
                IoDeleteSymbolicLink(&MouNameFull);
            IoDeleteDevice(pPortDevObj);
        }
    }

    if (resources)
        ExFreePool(resources);
    if (pInit)
        ExFreePool(pInit);
    if (DevNameSuff.MaximumLength)
        ExFreePool(DevNameSuff.Buffer);
    if (KbdNameFull.MaximumLength)
        ExFreePool(KbdNameFull.Buffer);
    if (MouNameFull.MaximumLength)
        ExFreePool(MouNameFull.Buffer);
    if (resourceDeviceClass.MaximumLength)
        ExFreePool(resourceDeviceClass.Buffer);
    if (registryPath.MaximumLength)
        ExFreePool(registryPath.Buffer);

    LogFlow(("VBoxMouseNT::DriverEntry: leave, status = %d\n", status));
    RTR0Term();
    return status;
}

static VOID I8042Unload(PDRIVER_OBJECT pDrvObj)
{
    NOREF(pDrvObj);
}

/**
 * Build a resource list.
 */
static VOID CreateResList(PDEVEXT pDevExt, PCM_RESOURCE_LIST *pResList, PULONG pResListSize)
{
    ULONG cPorts = pDevExt->Cfg.cPorts;
    if (pDevExt->Cfg.KbdInt.Type == CmResourceTypeInterrupt)
        cPorts++;
    if (pDevExt->Cfg.MouInt.Type == CmResourceTypeInterrupt)
        cPorts++;

    *pResListSize = sizeof(CM_RESOURCE_LIST) + ((cPorts - 1) * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR));
    *pResList = (PCM_RESOURCE_LIST)ExAllocatePool(PagedPool, *pResListSize);
    if (!*pResList)
    {
        *pResListSize = 0;
        return;
    }

    RtlZeroMemory(*pResList, *pResListSize);

    (*pResList)->Count = 1;
    (*pResList)->List[0].InterfaceType = pDevExt->Cfg.InterfaceType;
    (*pResList)->List[0].BusNumber     = pDevExt->Cfg.uBusNr;

    (*pResList)->List[0].PartialResourceList.Count = cPorts;
    ULONG i = 0;
    if (pDevExt->Cfg.KbdInt.Type == CmResourceTypeInterrupt)
        (*pResList)->List[0].PartialResourceList.PartialDescriptors[i++] = pDevExt->Cfg.KbdInt;
    if (pDevExt->Cfg.MouInt.Type == CmResourceTypeInterrupt)
        (*pResList)->List[0].PartialResourceList.PartialDescriptors[i++] = pDevExt->Cfg.MouInt;
    for (unsigned j = 0; j < pDevExt->Cfg.cPorts; j++)
        (*pResList)->List[0].PartialResourceList.PartialDescriptors[i++] = pDevExt->Cfg.aPorts[j];
}

/**
 * Read the i8042 controller command byte
 */
static NTSTATUS GetCtrlCmd(ULONG HwDisEnMask, PDEVEXT pDevExt, PUCHAR pByte)
{
    NTSTATUS status;
    if (HwDisEnMask & KEYBOARD_HARDWARE_PRESENT)
    {
        status = PutBytePoll(i8042Cmd, FALSE /*=wait*/, NoDevice, pDevExt, I8042_DISABLE_KEYBOARD_DEVICE);
        if (!NT_SUCCESS(status))
            return status;
    }

    if (HwDisEnMask & MOUSE_HARDWARE_PRESENT)
    {
        status = PutBytePoll(i8042Cmd, FALSE /*=wait*/, NoDevice, pDevExt, I8042_DISABLE_MOUSE_DEVICE);
        if (!NT_SUCCESS(status))
        {
            if (HwDisEnMask & KEYBOARD_HARDWARE_PRESENT)
                PutBytePoll(i8042Cmd, FALSE/*=wait*/, NoDevice, pDevExt, I8042_ENABLE_KEYBOARD_DEVICE);
            return status;
        }
    }

    status = PutBytePoll(i8042Cmd, FALSE /*=wait*/, NoDevice, pDevExt, I8042_READ_CONTROLLER_COMMAND_BYTE);
    if (NT_SUCCESS(status))
    {
        for (unsigned iRetry = 0; iRetry < 5; iRetry++)
        {
            status = GetBytePoll(CtrlDevType, pDevExt, pByte);
            if (NT_SUCCESS(status))
                break;
            if (status == STATUS_IO_TIMEOUT)
                KeStallExecutionProcessor(50);
            else
                break;
        }
    }

    NTSTATUS status2;
    if (HwDisEnMask & KEYBOARD_HARDWARE_PRESENT)
    {
        status2 = PutBytePoll(i8042Cmd, FALSE /*=wait*/, NoDevice, pDevExt, I8042_ENABLE_KEYBOARD_DEVICE);
        if (!NT_SUCCESS(status2))
        {
            if (NT_SUCCESS(status))
                status = status2;
        }
        else if (status == STATUS_SUCCESS)
            *pByte &= (UCHAR)~CCB_DISABLE_KEYBOARD_DEVICE;
    }

    if (HwDisEnMask & MOUSE_HARDWARE_PRESENT)
    {
        status2 = PutBytePoll(i8042Cmd, FALSE /*=wait*/, NoDevice, pDevExt, I8042_ENABLE_MOUSE_DEVICE);
        if (!NT_SUCCESS(status2))
        {
            if (NT_SUCCESS(status))
                status = status2;
        }
        else if (NT_SUCCESS(status))
            *pByte &= (UCHAR)~CCB_DISABLE_MOUSE_DEVICE;
    }
    return status;
}

/**
 * Write the i8042 controller command byte.
 */
static NTSTATUS PutCtrlCmd(PDEVEXT pDevExt, UCHAR Byte)
{
    NTSTATUS status;
    status = PutBytePoll(i8042Cmd, FALSE /*=wait*/, NoDevice, pDevExt, I8042_WRITE_CONTROLLER_COMMAND_BYTE);
    if (!NT_SUCCESS(status))
        return status;

    return (PutBytePoll(i8042Dat, FALSE /*=wait*/, NoDevice, pDevExt, Byte));
}

/**
 * Read the i8042 controller command byte.
 */
static VOID TransCtrlCmd(PDEVEXT pDevExt, PI8042TRANSMITCCBCTX pCtx)
{
    UCHAR  bCtrlCmd;
    pCtx->Status = GetCtrlCmd(pCtx->HwDisEnMask, pDevExt, &bCtrlCmd);
    if (!NT_SUCCESS(pCtx->Status))
        return;

    if (pCtx->fAndOp)
        bCtrlCmd &= pCtx->ByteMask;
    else
        bCtrlCmd |= pCtx->ByteMask;

    pCtx->Status = PutCtrlCmd(pDevExt, bCtrlCmd);

    UCHAR  bVrfyCmd;
    pCtx->Status = GetCtrlCmd(pCtx->HwDisEnMask, pDevExt, &bVrfyCmd);

    if (   NT_SUCCESS(pCtx->Status)
        && bVrfyCmd != bCtrlCmd)
        pCtx->Status = STATUS_DEVICE_DATA_ERROR;
}

/**
 * Detect the number of mouse buttons.
 */
static NTSTATUS MouQueryButtons(PDEVICE_OBJECT pDevObj, PUCHAR pNumButtons)
{
    PDEVEXT pDevExt = (PDEVEXT)pDevObj->DeviceExtension;

    NTSTATUS status = PutBytePoll(i8042Dat, TRUE /*=wait*/, MouDevType, pDevExt, SET_MOUSE_RESOLUTION);
    if (!NT_SUCCESS(status))
        return status;

    status = PutBytePoll(i8042Dat, TRUE /*=wait*/, MouDevType, pDevExt, 0x00);
    if (!NT_SUCCESS(status))
        return status;

    for (unsigned i = 0; i < 3; i++)
    {
        status = PutBytePoll(i8042Dat, TRUE /*=wait*/, MouDevType, pDevExt, SET_MOUSE_SCALING_1TO1);
        if (!NT_SUCCESS(status))
            return status;
    }

    status = PutBytePoll(i8042Dat, TRUE /*=wait*/, MouDevType, pDevExt, READ_MOUSE_STATUS);
    if (!NT_SUCCESS(status))
        return status;
    UCHAR byte;
    status = GetBytePoll(CtrlDevType, pDevExt, &byte);
    if (!NT_SUCCESS(status))
        return status;
    UCHAR buttons;
    status = GetBytePoll(CtrlDevType, pDevExt, &buttons);
    if (!NT_SUCCESS(status))
        return status;
    status = GetBytePoll(CtrlDevType, pDevExt, &byte);
    if (!NT_SUCCESS(status))
        return status;

    if (buttons == 2 || buttons == 3)
        *pNumButtons = buttons;
    else
        *pNumButtons = 0;

    return status;
}

/**
 * Initialize the i8042 mouse hardware.
 */
static NTSTATUS MouInitHw(PDEVICE_OBJECT pDevObj)
{
    PDEVEXT pDevExt = (PDEVEXT) pDevObj->DeviceExtension;

    NTSTATUS status = PutBytePoll(i8042Dat, TRUE /*=wait*/, MouDevType, pDevExt, MOUSE_RESET);
    if (!NT_SUCCESS(status))
        goto fail;

    UCHAR byte;
    for (unsigned i = 0; i < 11200; i++)
    {
        status = GetBytePoll(CtrlDevType, pDevExt, &byte);
        if (NT_SUCCESS(status) && byte == (UCHAR) MOUSE_COMPLETE)
            break;
        if (status != STATUS_IO_TIMEOUT)
            break;
        KeStallExecutionProcessor(50);
    }

    if (!NT_SUCCESS(status))
        goto fail;

    status = GetBytePoll(CtrlDevType, pDevExt, &byte);
    if ((!NT_SUCCESS(status)) || (byte != MOUSE_ID_BYTE))
        goto fail;

    MouFindWheel(pDevObj);

    UCHAR numButtons;
    status = MouQueryButtons(pDevObj, &numButtons);
    if (!NT_SUCCESS(status))
        goto fail;
    else if (numButtons)
        pDevExt->Cfg.MouAttr.NumberOfButtons = numButtons;

    status = PutBytePoll(i8042Dat, TRUE /*=wait*/, MouDevType, pDevExt, SET_MOUSE_SAMPLING_RATE);
    if (!NT_SUCCESS(status))
        goto fail;

    status = PutBytePoll(i8042Dat, TRUE /*=wait*/, MouDevType, pDevExt, 60);
    if (!NT_SUCCESS(status))
        goto fail;

    status = PutBytePoll(i8042Dat, TRUE /*=wait*/, MouDevType, pDevExt, SET_MOUSE_RESOLUTION);
    if (!NT_SUCCESS(status))
        goto fail;

    status = PutBytePoll(i8042Dat, TRUE /*=wait*/, MouDevType, pDevExt, (UCHAR)pDevExt->Cfg.MouseResolution);

fail:
    pDevExt->MouExt.uPrevSignAndOverflow = 0;
    pDevExt->MouExt.InputState = MouseExpectingACK;
    pDevExt->MouExt.LastByteReceived = 0;

    return status;
}

/**
 * Initialize the i8042 keyboard hardware.
 */
static NTSTATUS KbdInitHw(PDEVICE_OBJECT pDevObj)
{
    NTSTATUS status = STATUS_SUCCESS; /* Shut up MSC. */
    BOOLEAN fWaitForAck = TRUE;
    PDEVEXT pDevExt = (PDEVEXT)pDevObj->DeviceExtension;

retry:
    PutBytePoll(i8042Dat, fWaitForAck, KbdDevType, pDevExt, KEYBOARD_RESET);

    LARGE_INTEGER startOfSpin;
    KeQueryTickCount(&startOfSpin);
    for (unsigned i = 0; i < 11200; i++)
    {
        UCHAR byte;
        status = GetBytePoll(KbdDevType, pDevExt, &byte);
        if (NT_SUCCESS(status))
            break;
        if (status == STATUS_IO_TIMEOUT)
        {
            LARGE_INTEGER nextQuery, difference, tenSeconds;
            KeStallExecutionProcessor(50);
            KeQueryTickCount(&nextQuery);
            difference.QuadPart = nextQuery.QuadPart - startOfSpin.QuadPart;
            tenSeconds.QuadPart = 10*10*1000*1000;
            ASSERT(KeQueryTimeIncrement() <= MAXLONG);
            if (difference.QuadPart*KeQueryTimeIncrement() >= tenSeconds.QuadPart)
                break;
        }
        else
            break;
    }

    if (!NT_SUCCESS(status))
    {
        if (fWaitForAck)
        {
            fWaitForAck = FALSE;
            goto retry;
        }
        goto fail;
    }

    I8042TRANSMITCCBCTX Ctx;
    Ctx.HwDisEnMask = 0;
    Ctx.fAndOp = TRUE;
    Ctx.ByteMask = (UCHAR) ~((UCHAR)CCB_KEYBOARD_TRANSLATE_MODE);

    TransCtrlCmd(pDevExt, &Ctx);
    if (!NT_SUCCESS(Ctx.Status))
        TransCtrlCmd(pDevExt, &Ctx);
    if (!NT_SUCCESS(Ctx.Status))
    {
        status = Ctx.Status;
        goto fail;
    }

    PKEYBOARD_ID pId = &pDevExt->Cfg.KbdAttr.KeyboardIdentifier;
    status = PutBytePoll(i8042Dat, TRUE /*=wait*/, KbdDevType, pDevExt, SET_KEYBOARD_TYPEMATIC);
    if (status == STATUS_SUCCESS)
    {
        status = PutBytePoll(i8042Dat, TRUE /*=wait*/, KbdDevType, pDevExt,
                             ConvertTypematic(pDevExt->Cfg.KeyRepeatCurrent.Rate,
                                              pDevExt->Cfg.KeyRepeatCurrent.Delay));
        /* ignore errors */
    }

    status = PutBytePoll(i8042Dat, TRUE /*=wait*/, KbdDevType, pDevExt, SET_KEYBOARD_INDICATORS);
    if (status == STATUS_SUCCESS)
    {
        status = PutBytePoll(i8042Dat, TRUE /*=wait*/, KbdDevType, pDevExt,
                                  (UCHAR)pDevExt->Cfg.KbdInd.LedFlags);
        /* ignore errors */
    }
    status = STATUS_SUCCESS;

    if (pDevExt->Cfg.KbdAttr.KeyboardMode == 1)
    {
        Ctx.HwDisEnMask = 0;
        Ctx.fAndOp = FALSE;
        Ctx.ByteMask = CCB_KEYBOARD_TRANSLATE_MODE;
        TransCtrlCmd(pDevExt, &Ctx);
        if (!NT_SUCCESS(Ctx.Status))
        {
            if (Ctx.Status == STATUS_DEVICE_DATA_ERROR)
            {
                if (ENHANCED_KEYBOARD(*pId))
                {
                    status = PutBytePoll(i8042Dat, TRUE /*=wait*/, KbdDevType, pDevExt, SELECT_SCAN_CODE_SET);
                    if (!NT_SUCCESS(status))
                        pDevExt->Cfg.KbdAttr.KeyboardMode = 2;
                    else
                    {
                        status = PutBytePoll(i8042Dat, TRUE /*=wait*/, KbdDevType, pDevExt, 1);
                        if (!NT_SUCCESS(status))
                            pDevExt->Cfg.KbdAttr.KeyboardMode = 2;
                    }
                }
            }
            else
            {
                status = Ctx.Status;
                goto fail;
            }
        }
    }

fail:
    pDevExt->KbdExt.CurrentOutput.State = Idle;
    pDevExt->KbdExt.CurrentOutput.FirstByte = 0;
    pDevExt->KbdExt.CurrentOutput.LastByte = 0;

    return status;
}

/**
 * Initialize the i8042 controller, keyboard and mouse.
 */
static VOID InitHw(PDEVICE_OBJECT pDevObj)
{
    PDEVEXT pDevExt = (PDEVEXT) pDevObj->DeviceExtension;
    PUCHAR dataAddress = pDevExt->DevRegs[i8042Dat];
    PUCHAR commandAddress = pDevExt->DevRegs[i8042Cmd];

    DrainOutBuf(dataAddress, commandAddress);

    I8042TRANSMITCCBCTX Ctx;
    Ctx.HwDisEnMask = 0;
    Ctx.fAndOp = TRUE;
    Ctx.ByteMask = (UCHAR) ~((UCHAR)CCB_ENABLE_KEYBOARD_INTERRUPT | (UCHAR)CCB_ENABLE_MOUSE_INTERRUPT);
    TransCtrlCmd(pDevExt, &Ctx);
    if (!NT_SUCCESS(Ctx.Status))
        return;

    DrainOutBuf(dataAddress, commandAddress);

    if (pDevExt->HardwarePresent & MOUSE_HARDWARE_PRESENT)
    {
        NTSTATUS status = MouInitHw(pDevObj);
        if (!NT_SUCCESS(status))
            pDevExt->HardwarePresent &= ~MOUSE_HARDWARE_PRESENT;
    }

    if (pDevExt->HardwarePresent & KEYBOARD_HARDWARE_PRESENT)
    {
        NTSTATUS status = KbdInitHw(pDevObj);
        if (!NT_SUCCESS(status))
            pDevExt->HardwarePresent &= ~KEYBOARD_HARDWARE_PRESENT;
    }

    if (pDevExt->HardwarePresent & KEYBOARD_HARDWARE_PRESENT)
    {
        NTSTATUS status = PutBytePoll(i8042Cmd, FALSE /*=wait*/, NoDevice, pDevExt, I8042_ENABLE_KEYBOARD_DEVICE);
        if (!NT_SUCCESS(status))
            pDevExt->HardwarePresent &= ~KEYBOARD_HARDWARE_PRESENT;

        DrainOutBuf(dataAddress, commandAddress);
    }

    if (pDevExt->HardwarePresent & MOUSE_HARDWARE_PRESENT)
    {
        NTSTATUS status = PutBytePoll(i8042Cmd, FALSE /*=wait*/, NoDevice, pDevExt, I8042_ENABLE_MOUSE_DEVICE);
        if (!NT_SUCCESS(status))
            pDevExt->HardwarePresent &= ~MOUSE_HARDWARE_PRESENT;
        DrainOutBuf(dataAddress, commandAddress);
    }

    if (pDevExt->HardwarePresent)
    {
        Ctx.HwDisEnMask = pDevExt->HardwarePresent;
        Ctx.fAndOp = FALSE;
        Ctx.ByteMask = (pDevExt->HardwarePresent & KEYBOARD_HARDWARE_PRESENT)
                                    ? CCB_ENABLE_KEYBOARD_INTERRUPT : 0;
        Ctx.ByteMask |= (UCHAR)
            (pDevExt->HardwarePresent & MOUSE_HARDWARE_PRESENT) ? CCB_ENABLE_MOUSE_INTERRUPT : 0;
        TransCtrlCmd(pDevExt, &Ctx);
        if (!NT_SUCCESS(Ctx.Status))
        {
            /* ignore */
        }
    }
}

/**
 * retrieve the drivers service parameters from the registry
 */
static VOID HwGetRegstry(PINITEXT pInit, PUNICODE_STRING RegistryPath,
                         PUNICODE_STRING KeyboardDeviceName, PUNICODE_STRING PointerDeviceName)

{
    PRTL_QUERY_REGISTRY_TABLE aQuery = NULL;
    UNICODE_STRING parametersPath = { 0, 0, NULL }; /* Shut up MSC (actually badly structured code is a fault, but whatever). */
    UNICODE_STRING defaultPointerName;
    UNICODE_STRING defaultKeyboardName;
    USHORT   defaultResendIterations = 3;
    ULONG    iResend = 0;
    USHORT   defaultPollingIterations = 12000;
    ULONG    pollingIterations = 0;
    USHORT   defaultPollingIterationsMaximum = 12000;
    ULONG    pollingIterationsMaximum = 0;
    USHORT   defaultPollStatusIterations = 12000;
    ULONG    pollStatusIterations = 0;
    ULONG    defaultDataQueueSize = 100;
    ULONG    cButtons = 2;
    USHORT   cButtonsDef = 2;
    ULONG    sampleRate = 60;
    USHORT   defaultSampleRate = 60;
    ULONG    mouseResolution = 3;
    USHORT   defaultMouseResolution = 3;
    ULONG    overrideKeyboardType = 0;
    ULONG    invalidKeyboardType = 0;
    ULONG    overrideKeyboardSubtype = (ULONG)-1;
    ULONG    invalidKeyboardSubtype = (ULONG)-1;
    ULONG    defaultSynchPacket100ns = 10000000UL;
    ULONG    enableWheelDetection = 0;
    ULONG    defaultEnableWheelDetection = 1;
    PWSTR    path = NULL;
    USHORT   queries = 15;
    PI8042CFGINF pCfg = &pInit->DevExt.Cfg;
    NTSTATUS status = STATUS_SUCCESS;

    pCfg->StallMicroseconds = 50;
    parametersPath.Buffer = NULL;

    path = RegistryPath->Buffer;
    if (NT_SUCCESS(status))
    {
        aQuery = (PRTL_QUERY_REGISTRY_TABLE)ExAllocatePool(PagedPool, sizeof(RTL_QUERY_REGISTRY_TABLE) * (queries + 1));
        if (!aQuery)
            status = STATUS_UNSUCCESSFUL;
        else
        {
            RtlZeroMemory(aQuery, sizeof(RTL_QUERY_REGISTRY_TABLE) * (queries + 1));
            RtlInitUnicodeString(&parametersPath, NULL);
            parametersPath.MaximumLength = RegistryPath->Length + sizeof(L"\\Parameters");
            parametersPath.Buffer = (PWSTR)ExAllocatePool(PagedPool, parametersPath.MaximumLength);
            if (!parametersPath.Buffer)
                status = STATUS_UNSUCCESSFUL;
        }
    }

    if (NT_SUCCESS(status))
    {
        RtlZeroMemory(parametersPath.Buffer, parametersPath.MaximumLength);
        RtlAppendUnicodeToString(&parametersPath, path);
        RtlAppendUnicodeToString(&parametersPath, L"\\Parameters");

        RtlInitUnicodeString(&defaultKeyboardName, L"KeyboardPort");
        RtlInitUnicodeString(&defaultPointerName, L"PointerPort");

        aQuery[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
        aQuery[0].Name = L"iResend";
        aQuery[0].EntryContext = &iResend;
        aQuery[0].DefaultType = REG_DWORD;
        aQuery[0].DefaultData = &defaultResendIterations;
        aQuery[0].DefaultLength = sizeof(USHORT);

        aQuery[1].Flags = RTL_QUERY_REGISTRY_DIRECT;
        aQuery[1].Name = L"PollingIterations";
        aQuery[1].EntryContext = &pollingIterations;
        aQuery[1].DefaultType = REG_DWORD;
        aQuery[1].DefaultData = &defaultPollingIterations;
        aQuery[1].DefaultLength = sizeof(USHORT);

        aQuery[2].Flags = RTL_QUERY_REGISTRY_DIRECT;
        aQuery[2].Name = L"PollingIterationsMaximum";
        aQuery[2].EntryContext = &pollingIterationsMaximum;
        aQuery[2].DefaultType = REG_DWORD;
        aQuery[2].DefaultData = &defaultPollingIterationsMaximum;
        aQuery[2].DefaultLength = sizeof(USHORT);

        aQuery[3].Flags = RTL_QUERY_REGISTRY_DIRECT;
        aQuery[3].Name = L"KeyboardDataQueueSize";
        aQuery[3].EntryContext = &pCfg->KbdAttr.InputDataQueueLength;
        aQuery[3].DefaultType = REG_DWORD;
        aQuery[3].DefaultData = &defaultDataQueueSize;
        aQuery[3].DefaultLength = sizeof(ULONG);

        aQuery[4].Flags = RTL_QUERY_REGISTRY_DIRECT;
        aQuery[4].Name = L"MouseDataQueueSize";
        aQuery[4].EntryContext = &pCfg->MouAttr.InputDataQueueLength;
        aQuery[4].DefaultType = REG_DWORD;
        aQuery[4].DefaultData = &defaultDataQueueSize;
        aQuery[4].DefaultLength = sizeof(ULONG);

        aQuery[5].Flags = RTL_QUERY_REGISTRY_DIRECT;
        aQuery[5].Name = L"NumberOfButtons";
        aQuery[5].EntryContext = &cButtons;
        aQuery[5].DefaultType = REG_DWORD;
        aQuery[5].DefaultData = &cButtonsDef;
        aQuery[5].DefaultLength = sizeof(USHORT);

        aQuery[6].Flags = RTL_QUERY_REGISTRY_DIRECT;
        aQuery[6].Name = L"SampleRate";
        aQuery[6].EntryContext = &sampleRate;
        aQuery[6].DefaultType = REG_DWORD;
        aQuery[6].DefaultData = &defaultSampleRate;
        aQuery[6].DefaultLength = sizeof(USHORT);

        aQuery[7].Flags = RTL_QUERY_REGISTRY_DIRECT;
        aQuery[7].Name = L"MouseResolution";
        aQuery[7].EntryContext = &mouseResolution;
        aQuery[7].DefaultType = REG_DWORD;
        aQuery[7].DefaultData = &defaultMouseResolution;
        aQuery[7].DefaultLength = sizeof(USHORT);

        aQuery[8].Flags = RTL_QUERY_REGISTRY_DIRECT;
        aQuery[8].Name = L"OverrideKeyboardType";
        aQuery[8].EntryContext = &overrideKeyboardType;
        aQuery[8].DefaultType = REG_DWORD;
        aQuery[8].DefaultData = &invalidKeyboardType;
        aQuery[8].DefaultLength = sizeof(ULONG);

        aQuery[9].Flags = RTL_QUERY_REGISTRY_DIRECT;
        aQuery[9].Name = L"OverrideKeyboardSubtype";
        aQuery[9].EntryContext = &overrideKeyboardSubtype;
        aQuery[9].DefaultType = REG_DWORD;
        aQuery[9].DefaultData = &invalidKeyboardSubtype;
        aQuery[9].DefaultLength = sizeof(ULONG);

        aQuery[10].Flags = RTL_QUERY_REGISTRY_DIRECT;
        aQuery[10].Name = L"KeyboardDeviceBaseName";
        aQuery[10].EntryContext = KeyboardDeviceName;
        aQuery[10].DefaultType = REG_SZ;
        aQuery[10].DefaultData = defaultKeyboardName.Buffer;
        aQuery[10].DefaultLength = 0;

        aQuery[11].Flags = RTL_QUERY_REGISTRY_DIRECT;
        aQuery[11].Name = L"PointerDeviceBaseName";
        aQuery[11].EntryContext = PointerDeviceName;
        aQuery[11].DefaultType = REG_SZ;
        aQuery[11].DefaultData = defaultPointerName.Buffer;
        aQuery[11].DefaultLength = 0;

        aQuery[12].Flags = RTL_QUERY_REGISTRY_DIRECT;
        aQuery[12].Name = L"MouseSynchIn100ns";
        aQuery[12].EntryContext = &pInit->DevExt.MouExt.SynchTickCount;
        aQuery[12].DefaultType = REG_DWORD;
        aQuery[12].DefaultData = &defaultSynchPacket100ns;
        aQuery[12].DefaultLength = sizeof(ULONG);

        aQuery[13].Flags = RTL_QUERY_REGISTRY_DIRECT;
        aQuery[13].Name = L"PollStatusIterations";
        aQuery[13].EntryContext = &pollStatusIterations;
        aQuery[13].DefaultType = REG_DWORD;
        aQuery[13].DefaultData = &defaultPollStatusIterations;
        aQuery[13].DefaultLength = sizeof(USHORT);

        aQuery[14].Flags = RTL_QUERY_REGISTRY_DIRECT;
        aQuery[14].Name = L"EnableWheelDetection";
        aQuery[14].EntryContext = &enableWheelDetection;
        aQuery[14].DefaultType = REG_DWORD;
        aQuery[14].DefaultData = &defaultEnableWheelDetection;
        aQuery[14].DefaultLength = sizeof(ULONG);

        status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE | RTL_REGISTRY_OPTIONAL,
                                        parametersPath.Buffer, aQuery, NULL, NULL);
    }

    if (!NT_SUCCESS(status))
    {
        /* driver defaults */
        pCfg->iResend = defaultResendIterations;
        pCfg->PollingIterations = defaultPollingIterations;
        pCfg->PollingIterationsMaximum = defaultPollingIterationsMaximum;
        pCfg->PollStatusIterations = defaultPollStatusIterations;
        pCfg->KbdAttr.InputDataQueueLength = defaultDataQueueSize;
        pCfg->MouAttr.InputDataQueueLength = defaultDataQueueSize;
        pCfg->EnableWheelDetection = defaultEnableWheelDetection;
        pInit->DevExt.MouExt.SynchTickCount = defaultSynchPacket100ns;
        RtlCopyUnicodeString(KeyboardDeviceName, &defaultKeyboardName);
        RtlCopyUnicodeString(PointerDeviceName, &defaultPointerName);
    }
    else
    {
        pCfg->iResend = (USHORT)iResend;
        pCfg->PollingIterations = (USHORT) pollingIterations;
        pCfg->PollingIterationsMaximum = (USHORT) pollingIterationsMaximum;
        pCfg->PollStatusIterations = (USHORT) pollStatusIterations;
        pCfg->EnableWheelDetection = (ULONG) ((enableWheelDetection) ? 1 : 0);
    }

    if (pCfg->KbdAttr.InputDataQueueLength == 0)
        pCfg->KbdAttr.InputDataQueueLength = defaultDataQueueSize;
    pCfg->KbdAttr.InputDataQueueLength *= sizeof(KEYBOARD_INPUT_DATA);

    if (pCfg->MouAttr.InputDataQueueLength == 0)
        pCfg->MouAttr.InputDataQueueLength = defaultDataQueueSize;
    pCfg->MouAttr.InputDataQueueLength *= sizeof(MOUSE_INPUT_DATA);

    pCfg->MouAttr.NumberOfButtons = (USHORT)cButtons;
    pCfg->MouAttr.SampleRate = (USHORT)sampleRate;
    pCfg->MouseResolution = (USHORT)mouseResolution;

    if (overrideKeyboardType != invalidKeyboardType)
    {
        if (overrideKeyboardType <= RT_ELEMENTS(s_aKeybType))
            pCfg->KbdAttr.KeyboardIdentifier.Type = (UCHAR) overrideKeyboardType;
    }

    if (overrideKeyboardSubtype != invalidKeyboardSubtype)
        pCfg->KbdAttr.KeyboardIdentifier.Subtype = (UCHAR) overrideKeyboardSubtype;

    if (pInit->DevExt.MouExt.SynchTickCount == 0)
        pInit->DevExt.MouExt.SynchTickCount = defaultSynchPacket100ns;

    pInit->DevExt.MouExt.SynchTickCount /= KeQueryTimeIncrement();

    if (parametersPath.Buffer)
        ExFreePool(parametersPath.Buffer);
    if (aQuery)
        ExFreePool(aQuery);
}

static void GetDevIdentifier(PKEY_VALUE_FULL_INFORMATION *ppInf, PUNICODE_STRING pStr)
{
    pStr->Length = (USHORT)(*(ppInf + IoQueryDeviceIdentifier))->DataLength;
    if (!pStr->Length)
        return;
    pStr->MaximumLength = pStr->Length;
    pStr->Buffer = (PWSTR) (((PUCHAR)(*(ppInf + IoQueryDeviceIdentifier)))
                 +                   (*(ppInf + IoQueryDeviceIdentifier))->DataOffset);
}

static ULONG GetDevCfgData(PKEY_VALUE_FULL_INFORMATION *ppInf, PCM_PARTIAL_RESOURCE_LIST *ppData)
{
    ULONG DataLength = (*(ppInf + IoQueryDeviceConfigurationData))->DataLength;
    if (DataLength)
        *ppData = (PCM_PARTIAL_RESOURCE_LIST)(   ((PUCHAR) (*(ppInf + IoQueryDeviceConfigurationData)))
                                              +            (*(ppInf + IoQueryDeviceConfigurationData))->DataOffset
                                              + FIELD_OFFSET(CM_FULL_RESOURCE_DESCRIPTOR, PartialResourceList));
    return DataLength;
}

/**
 * Callout routine. Grab keyboard controller and peripheral configuration
 * information.
 */
static NTSTATUS KbdCallOut(PVOID pCtx, PUNICODE_STRING PathName,
                           INTERFACE_TYPE BusType, ULONG uBusNr, PKEY_VALUE_FULL_INFORMATION *pBusInf,
                           CONFIGURATION_TYPE uCtrlType, ULONG uCtrlNr, PKEY_VALUE_FULL_INFORMATION *pCtrlInf,
                           CONFIGURATION_TYPE uPrfType, ULONG uPrfNr, PKEY_VALUE_FULL_INFORMATION *pPrfInf)
{
    RT_NOREF(PathName, pBusInf, uCtrlType, uCtrlNr, uPrfType, uPrfNr);
    UNICODE_STRING unicodeIdentifier;
    GetDevIdentifier(pPrfInf, &unicodeIdentifier);

    PINITEXT pInit = (PINITEXT)pCtx;
    PDEVEXT pDevExt = &pInit->DevExt;
    if (    (pDevExt->HardwarePresent & KEYBOARD_HARDWARE_PRESENT)
         || !unicodeIdentifier.Length)
        return STATUS_SUCCESS;

    pDevExt->HardwarePresent |= KEYBOARD_HARDWARE_PRESENT;

    PI8042CFGINF pCfg = &pDevExt->Cfg;
    pCfg->KbdAttr.KeyboardIdentifier.Type = 0;
    pCfg->KbdAttr.KeyboardIdentifier.Subtype = 0;

    PCM_PARTIAL_RESOURCE_LIST pPrfData;
    if (GetDevCfgData(pPrfInf, &pPrfData))
    {
        unsigned cList = pPrfData->Count;
        PCM_PARTIAL_RESOURCE_DESCRIPTOR pResDesc = pPrfData->PartialDescriptors;
        for (unsigned i = 0; i < cList; i++, pResDesc++)
        {
            switch (pResDesc->Type)
            {
                case CmResourceTypeDeviceSpecific:
                {
                    PCM_KEYBOARD_DEVICE_DATA KbdData = (PCM_KEYBOARD_DEVICE_DATA)(((PUCHAR)pResDesc)
                                                     + sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR));
                    if (KbdData->Type <= RT_ELEMENTS(s_aKeybType))
                        pCfg->KbdAttr.KeyboardIdentifier.Type = KbdData->Type;
                    pCfg->KbdAttr.KeyboardIdentifier.Subtype = KbdData->Subtype;
                    pCfg->KbdInd.LedFlags = (KbdData->KeyboardFlags >> 4) & 7;
                    break;
                }
                default:
                    break;
            }
        }
    }

    if (pCfg->KbdAttr.KeyboardIdentifier.Type == 0)
    {
        pCfg->KbdAttr.KeyboardIdentifier.Type = 4;
        pCfg->KbdInd.LedFlags = 0;
    }

    pCfg->InterfaceType = BusType;
    pCfg->uBusNr = uBusNr;
    pCfg->fFloatSave = FALSE;

    BOOLEAN fDefIntShare;
    KINTERRUPT_MODE DefIntMode;
    if (BusType == MicroChannel)
    {
        fDefIntShare = TRUE;
        DefIntMode = LevelSensitive;
    }
    else
    {
        fDefIntShare = FALSE;
        DefIntMode = Latched;
    }

    PCM_PARTIAL_RESOURCE_LIST pCtrlData;
    if (GetDevCfgData(pCtrlInf, &pCtrlData))
    {
        unsigned cList = pCtrlData->Count;
        PCM_PARTIAL_RESOURCE_DESCRIPTOR pResDesc = pCtrlData->PartialDescriptors;
        for (unsigned i = 0; i < cList; i++, pResDesc++)
        {
            switch (pResDesc->Type)
            {
                case CmResourceTypePort:
                    ASSERT(pCfg->cPorts < i8042MaxPorts);
                    pCfg->aPorts[pCfg->cPorts] = *pResDesc;
                    pCfg->aPorts[pCfg->cPorts].ShareDisposition = CmResourceShareDriverExclusive;
                    pCfg->cPorts++;
                    break;

                case CmResourceTypeInterrupt:
                    pCfg->KbdInt = *pResDesc;
                    pCfg->KbdInt.ShareDisposition = fDefIntShare ? CmResourceShareShared
                                                                 : CmResourceShareDeviceExclusive;
                    break;

                case CmResourceTypeDeviceSpecific:
                    break;

                default:
                    break;
            }
        }
    }

    if (!(pCfg->KbdInt.Type & CmResourceTypeInterrupt))
    {
        pCfg->KbdInt.Type = CmResourceTypeInterrupt;
        pCfg->KbdInt.ShareDisposition = fDefIntShare ? CmResourceShareShared
                                                     : CmResourceShareDeviceExclusive;
        pCfg->KbdInt.Flags = (DefIntMode == Latched) ? CM_RESOURCE_INTERRUPT_LATCHED
                                                     : CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
        pCfg->KbdInt.u.Interrupt.Level = 1;
        pCfg->KbdInt.u.Interrupt.Vector = 1;
    }

    if (pCfg->cPorts == 0)
    {
        pCfg->aPorts[i8042Dat].Type = CmResourceTypePort;
        pCfg->aPorts[i8042Dat].Flags = CM_RESOURCE_PORT_IO;
        pCfg->aPorts[i8042Dat].ShareDisposition = CmResourceShareDriverExclusive;
        pCfg->aPorts[i8042Dat].u.Port.Start.LowPart  = 0x60;
        pCfg->aPorts[i8042Dat].u.Port.Start.HighPart = 0;
        pCfg->aPorts[i8042Dat].u.Port.Length = 1;

        pCfg->aPorts[i8042Cmd].Type = CmResourceTypePort;
        pCfg->aPorts[i8042Cmd].Flags = CM_RESOURCE_PORT_IO;
        pCfg->aPorts[i8042Cmd].ShareDisposition = CmResourceShareDriverExclusive;
        pCfg->aPorts[i8042Cmd].u.Port.Start.LowPart  = 0x64;
        pCfg->aPorts[i8042Cmd].u.Port.Start.HighPart = 0;
        pCfg->aPorts[i8042Cmd].u.Port.Length = 1;

        pCfg->cPorts = 2;
    }
    else if (pCfg->cPorts == 1)
    {
        pCfg->aPorts[i8042Dat].u.Port.Length = 1;
        pCfg->aPorts[i8042Cmd] = pCfg->aPorts[i8042Dat];
        pCfg->aPorts[i8042Cmd].u.Port.Start.LowPart += 4;
        pCfg->cPorts++;
    }
    else
    {
        if (pCfg->aPorts[i8042Cmd].u.Port.Start.LowPart < pCfg->aPorts[i8042Dat].u.Port.Start.LowPart)
        {
            CM_PARTIAL_RESOURCE_DESCRIPTOR Desc = pCfg->aPorts[i8042Dat];
            pCfg->aPorts[i8042Dat] = pCfg->aPorts[i8042Cmd];
            pCfg->aPorts[i8042Cmd] = Desc;
        }
    }

    return STATUS_SUCCESS;
}

/**
 * Callout routine. Grab the pointer controller and the peripheral
 * configuration information.
 */
static NTSTATUS MouCallOut(PVOID pCtx, PUNICODE_STRING PathName,
                           INTERFACE_TYPE BusType, ULONG uBusNr, PKEY_VALUE_FULL_INFORMATION *pBusInf,
                           CONFIGURATION_TYPE uCtrlType, ULONG uCtrlNr, PKEY_VALUE_FULL_INFORMATION *pCtrlInf,
                           CONFIGURATION_TYPE uPrfType, ULONG uPrfNr, PKEY_VALUE_FULL_INFORMATION *pPrfInf)
{
    RT_NOREF(PathName, pBusInf, uCtrlType, uCtrlNr, uPrfType, uPrfNr);
    NTSTATUS status = STATUS_SUCCESS;

    UNICODE_STRING unicodeIdentifier;
    GetDevIdentifier(pPrfInf, &unicodeIdentifier);

    PINITEXT pInit = (PINITEXT)pCtx;
    PDEVEXT pDevExt = &pInit->DevExt;

    if (   (pDevExt->HardwarePresent & MOUSE_HARDWARE_PRESENT)
        || unicodeIdentifier.Length == 0)
        return status;

    ANSI_STRING ansiString;
    status = RtlUnicodeStringToAnsiString(&ansiString, &unicodeIdentifier, TRUE);
    if (!NT_SUCCESS(status))
        return status;

    if (strstr(ansiString.Buffer, "PS2"))
         pDevExt->HardwarePresent |= MOUSE_HARDWARE_PRESENT;

    RtlFreeAnsiString(&ansiString);

    if (!(pDevExt->HardwarePresent & MOUSE_HARDWARE_PRESENT))
        return status;

    PI8042CFGINF pCfg = &pDevExt->Cfg;
    if (!(pDevExt->HardwarePresent & KEYBOARD_HARDWARE_PRESENT))
    {
        pCfg->InterfaceType = BusType;
        pCfg->uBusNr = uBusNr;
        pCfg->fFloatSave = FALSE;
    }

    BOOLEAN fDefIntShare;
    KINTERRUPT_MODE DefIntMode;
    if (pCfg->InterfaceType == MicroChannel)
    {
        fDefIntShare = TRUE;
        DefIntMode = LevelSensitive;
    }
    else
    {
        fDefIntShare = FALSE;
        DefIntMode = Latched;
    }

    PCM_PARTIAL_RESOURCE_LIST pCtrlData;
    if (GetDevCfgData(pCtrlInf, &pCtrlData))
    {
        unsigned cList = pCtrlData->Count;
        PCM_PARTIAL_RESOURCE_DESCRIPTOR pResDesc = pCtrlData->PartialDescriptors;
        BOOLEAN fPortInfoNeeded = pCfg->cPorts ? FALSE : TRUE;

        for (unsigned i = 0; i < cList; i++, pResDesc++)
        {
            switch (pResDesc->Type)
            {
                case CmResourceTypePort:
                    if (fPortInfoNeeded)
                    {
                        pCfg->aPorts[pCfg->cPorts] = *pResDesc;
                        pCfg->aPorts[pCfg->cPorts].ShareDisposition = CmResourceShareDriverExclusive;
                        pCfg->cPorts++;
                    }
                    break;

                case CmResourceTypeInterrupt:
                    pCfg->MouInt = *pResDesc;
                    pCfg->MouInt.ShareDisposition = fDefIntShare ? CmResourceShareShared
                                                                 : CmResourceShareDeviceExclusive;
                    break;

                default:
                    break;
            }
        }
    }

    if (!(pCfg->MouInt.Type & CmResourceTypeInterrupt))
    {
        pCfg->MouInt.Type = CmResourceTypeInterrupt;
        pCfg->MouInt.ShareDisposition = fDefIntShare ? CmResourceShareShared
                                                     : CmResourceShareDeviceExclusive;
        pCfg->MouInt.Flags = (DefIntMode == Latched) ? CM_RESOURCE_INTERRUPT_LATCHED
                                                     : CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
        pCfg->MouInt.u.Interrupt.Level = 12;
        pCfg->MouInt.u.Interrupt.Vector = 12;
    }

    if (pCfg->cPorts == 0)
    {
        pCfg->aPorts[i8042Dat].Type = CmResourceTypePort;
        pCfg->aPorts[i8042Dat].Flags = CM_RESOURCE_PORT_IO;
        pCfg->aPorts[i8042Dat].ShareDisposition = CmResourceShareDriverExclusive;
        pCfg->aPorts[i8042Dat].u.Port.Start.LowPart = 0x60;
        pCfg->aPorts[i8042Dat].u.Port.Start.HighPart = 0;
        pCfg->aPorts[i8042Dat].u.Port.Length = 1;

        pCfg->aPorts[i8042Cmd].Type = CmResourceTypePort;
        pCfg->aPorts[i8042Cmd].Flags = CM_RESOURCE_PORT_IO;
        pCfg->aPorts[i8042Cmd].ShareDisposition = CmResourceShareDriverExclusive;
        pCfg->aPorts[i8042Cmd].u.Port.Start.LowPart = 0x64;
        pCfg->aPorts[i8042Cmd].u.Port.Start.HighPart = 0;
        pCfg->aPorts[i8042Cmd].u.Port.Length = 1;

        pCfg->cPorts = 2;
    }
    else if (pCfg->cPorts == 1)
    {
        pCfg->aPorts[i8042Cmd] = pCfg->aPorts[i8042Dat];
        pCfg->aPorts[i8042Cmd].u.Port.Start.LowPart += 4;
        pCfg->cPorts++;
    }
    else
    {
        if (pCfg->aPorts[i8042Cmd].u.Port.Start.LowPart < pCfg->aPorts[i8042Dat].u.Port.Start.LowPart)
        {
            CM_PARTIAL_RESOURCE_DESCRIPTOR Desc = pCfg->aPorts[i8042Dat];
            pCfg->aPorts[i8042Dat] = pCfg->aPorts[i8042Cmd];
            pCfg->aPorts[i8042Cmd] = Desc;
        }
    }

    return status;
}

static const UCHAR s_ucCommands[] =
{
    SET_MOUSE_SAMPLING_RATE, 200,
    SET_MOUSE_SAMPLING_RATE, 100,
    SET_MOUSE_SAMPLING_RATE, 80,
    GET_DEVICE_ID, 0
};

static NTSTATUS MouFindWheel(PDEVICE_OBJECT pDevObj)
{
    NTSTATUS status = STATUS_SUCCESS; /* Shut up MSC. */
    PDEVEXT pDevExt = (PDEVEXT) pDevObj->DeviceExtension;

    if (!pDevExt->Cfg.EnableWheelDetection)
        return STATUS_NO_SUCH_DEVICE;

    KeStallExecutionProcessor(50);

    for (unsigned iCmd = 0; s_ucCommands[iCmd];)
    {
        status = PutBytePoll(i8042Dat, TRUE /*=wait*/, MouDevType, pDevExt, s_ucCommands[iCmd]);
        if (!NT_SUCCESS(status))
            goto fail;

        iCmd++;
        KeStallExecutionProcessor(50);
    }

    UCHAR byte = UINT8_MAX;
    for (unsigned i = 0; i < 5; i++)
    {
        status = GetBytePoll(CtrlDevType, pDevExt, &byte);
        if (status != STATUS_IO_TIMEOUT)
            break;
        KeStallExecutionProcessor(50);
    }

    if (    NT_SUCCESS(status)
        && (byte == MOUSE_ID_BYTE || byte == WHEELMOUSE_ID_BYTE))
    {
        if (byte == WHEELMOUSE_ID_BYTE)
        {
            pDevExt->HardwarePresent |= (WHEELMOUSE_HARDWARE_PRESENT | MOUSE_HARDWARE_PRESENT);
            pDevExt->Cfg.MouAttr.MouseIdentifier =  WHEELMOUSE_I8042_HARDWARE;
        }
        else
            pDevExt->HardwarePresent |= MOUSE_HARDWARE_PRESENT;
    }

fail:
    pDevExt->MouExt.uPrevSignAndOverflow = 0;
    pDevExt->MouExt.InputState = MouseExpectingACK;
    return status;
}
