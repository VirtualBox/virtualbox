/* $Id: DBGCRemoteKd.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGC - Debugger Console, Windows Kd Remote Stub.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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
#include <VBox/dbg.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/vmapi.h> /* VMR3GetVM() */
#include <VBox/vmm/hm.h>    /* HMR3IsEnabled */
#include <VBox/vmm/nem.h>   /* NEMR3IsEnabled */
#include <iprt/assertcompile.h>
#include <iprt/cdefs.h>
#include <iprt/err.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/sg.h>
#include <iprt/string.h>
#include <iprt/time.h>
#include <iprt/x86.h>
#include <iprt/formats/pecoff.h>
#include <iprt/formats/mz.h>

#include <stdlib.h>

#include "DBGCInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** Number of milliseconds we wait for new data to arrive when a new packet was detected. */
#define DBGC_KD_RECV_TIMEOUT_MS                     UINT32_C(1000)

/** NT status code - Success. */
#define NTSTATUS_SUCCESS                            0
/** NT status code - buffer overflow. */
#define NTSTATUS_BUFFER_OVERFLOW                    UINT32_C(0x80000005)
/** NT status code - operation unsuccesful. */
#define NTSTATUS_UNSUCCESSFUL                       UINT32_C(0xc0000001)
/** NT status code - operation not implemented. */
#define NTSTATUS_NOT_IMPLEMENTED                    UINT32_C(0xc0000002)
/** NT status code - Object not found. */
#define NTSTATUS_NOT_FOUND                          UINT32_C(0xc0000225)

/** Offset where the KD version block pointer is stored in the KPCR.
 * From: https://www.geoffchappell.com/studies/windows/km/ntoskrnl/structs/kprcb/amd64.htm */
#define KD_KPCR_VERSION_BLOCK_ADDR_OFF              0x34


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * KD packet header as sent over the wire.
 */
typedef struct KDPACKETHDR
{
    /** Packet signature (leader) - defines the type of packet. */
    uint32_t                    u32Signature;
    /** Packet (sub) type. */
    uint16_t                    u16SubType;
    /** Size of the packet body in bytes.*/
    uint16_t                    cbBody;
    /** Packet ID. */
    uint32_t                    idPacket;
    /** Checksum of the packet body. */
    uint32_t                    u32ChkSum;
} KDPACKETHDR;
AssertCompileSize(KDPACKETHDR, 16);
/** Pointer to a packet header. */
typedef KDPACKETHDR *PKDPACKETHDR;
/** Pointer to a const packet header. */
typedef const KDPACKETHDR *PCKDPACKETHDR;

/** Signature for a data packet. */
#define KD_PACKET_HDR_SIGNATURE_DATA                UINT32_C(0x30303030)
/** First byte for a data packet header. */
#define KD_PACKET_HDR_SIGNATURE_DATA_BYTE           0x30
/** Signature for a control packet. */
#define KD_PACKET_HDR_SIGNATURE_CONTROL             UINT32_C(0x69696969)
/** First byte for a control packet header. */
#define KD_PACKET_HDR_SIGNATURE_CONTROL_BYTE        0x69
/** Signature for a breakin packet. */
#define KD_PACKET_HDR_SIGNATURE_BREAKIN             UINT32_C(0x62626262)
/** First byte for a breakin packet header. */
#define KD_PACKET_HDR_SIGNATURE_BREAKIN_BYTE        0x62

/** @name Packet sub types.
 * @{ */
#define KD_PACKET_HDR_SUB_TYPE_STATE_CHANGE32       UINT16_C(1)
#define KD_PACKET_HDR_SUB_TYPE_STATE_MANIPULATE     UINT16_C(2)
#define KD_PACKET_HDR_SUB_TYPE_DEBUG_IO             UINT16_C(3)
#define KD_PACKET_HDR_SUB_TYPE_ACKNOWLEDGE          UINT16_C(4)
#define KD_PACKET_HDR_SUB_TYPE_RESEND               UINT16_C(5)
#define KD_PACKET_HDR_SUB_TYPE_RESET                UINT16_C(6)
#define KD_PACKET_HDR_SUB_TYPE_STATE_CHANGE64       UINT16_C(7)
#define KD_PACKET_HDR_SUB_TYPE_POLL_BREAKIN         UINT16_C(8)
#define KD_PACKET_HDR_SUB_TYPE_TRACE_IO             UINT16_C(9)
#define KD_PACKET_HDR_SUB_TYPE_CONTROL_REQUEST      UINT16_C(10)
#define KD_PACKET_HDR_SUB_TYPE_FILE_IO              UINT16_C(11)
#define KD_PACKET_HDR_SUB_TYPE_MAX                  UINT16_C(12)
/** @} */

/** Initial packet ID value. */
#define KD_PACKET_HDR_ID_INITIAL                    UINT32_C(0x80800800)
/** Packet ID value after a resync. */
#define KD_PACKET_HDR_ID_RESET                      UINT32_C(0x80800000)

/** Trailing byte of a packet. */
#define KD_PACKET_TRAILING_BYTE                     0xaa


/** Maximum number of parameters in the exception record. */
#define KDPACKETEXCP_PARMS_MAX                      15

/**
 * 64bit exception record.
 */
typedef struct KDPACKETEXCP64
{
    /** The exception code identifying the excpetion. */
    uint32_t                    u32ExcpCode;
    /** Flags associated with the exception. */
    uint32_t                    u32ExcpFlags;
    /** Pointer to a chained exception record. */
    uint64_t                    u64PtrExcpRecNested;
    /** Address where the exception occurred. */
    uint64_t                    u64PtrExcpAddr;
    /** Number of parameters in the exception information array. */
    uint32_t                    cExcpParms;
    /** Alignment. */
    uint32_t                    u32Alignment;
    /** Exception parameters array. */
    uint64_t                    au64ExcpParms[KDPACKETEXCP_PARMS_MAX];
} KDPACKETEXCP64;
AssertCompileSize(KDPACKETEXCP64, 152);
/** Pointer to an exception record. */
typedef KDPACKETEXCP64 *PKDPACKETEXCP64;
/** Pointer to a const exception record. */
typedef const KDPACKETEXCP64 *PCKDPACKETEXCP64;


/**
 * amd64 NT context structure.
 */
typedef struct NTCONTEXT64
{
    /** The P[1-6]Home members. */
    uint64_t                    au64PHome[6];
    /** Context flags indicating the valid bits, see NTCONTEXT_F_XXX. */
    uint32_t                    fContext;
    /** MXCSR register. */
    uint32_t                    u32RegMxCsr;
    /** CS selector. */
    uint16_t                    u16SegCs;
    /** DS selector. */
    uint16_t                    u16SegDs;
    /** ES selector. */
    uint16_t                    u16SegEs;
    /** FS selector. */
    uint16_t                    u16SegFs;
    /** GS selector. */
    uint16_t                    u16SegGs;
    /** SS selector. */
    uint16_t                    u16SegSs;
    /** EFlags register. */
    uint32_t                    u32RegEflags;
    /** DR0 register. */
    uint64_t                    u64RegDr0;
    /** DR1 register. */
    uint64_t                    u64RegDr1;
    /** DR2 register. */
    uint64_t                    u64RegDr2;
    /** DR3 register. */
    uint64_t                    u64RegDr3;
    /** DR6 register. */
    uint64_t                    u64RegDr6;
    /** DR7 register. */
    uint64_t                    u64RegDr7;
    /** RAX register. */
    uint64_t                    u64RegRax;
    /** RCX register. */
    uint64_t                    u64RegRcx;
    /** RDX register. */
    uint64_t                    u64RegRdx;
    /** RBX register. */
    uint64_t                    u64RegRbx;
    /** RSP register. */
    uint64_t                    u64RegRsp;
    /** RBP register. */
    uint64_t                    u64RegRbp;
    /** RSI register. */
    uint64_t                    u64RegRsi;
    /** RDI register. */
    uint64_t                    u64RegRdi;
    /** R8 register. */
    uint64_t                    u64RegR8;
    /** R9 register. */
    uint64_t                    u64RegR9;
    /** R10 register. */
    uint64_t                    u64RegR10;
    /** R11 register. */
    uint64_t                    u64RegR11;
    /** R12 register. */
    uint64_t                    u64RegR12;
    /** R13 register. */
    uint64_t                    u64RegR13;
    /** R14 register. */
    uint64_t                    u64RegR14;
    /** R15 register. */
    uint64_t                    u64RegR15;
    /** RIP register. */
    uint64_t                    u64RegRip;
    /** Extended floating point save area. */
    X86FXSTATE                  FxSave;
    /** AVX(?) vector registers. */
    RTUINT128U                  aRegsVec[26];
    /** Vector control register. */
    uint64_t                    u64RegVecCtrl;
    /** Debug control. */
    uint64_t                    u64DbgCtrl;
    /** @todo lbr */
    uint64_t                    u64LastBrToRip;
    uint64_t                    u64LastBrFromRip;
    uint64_t                    u64LastExcpToRip;
    uint64_t                    u64LastExcpFromRip;
} NTCONTEXT64;
AssertCompileSize(NTCONTEXT64, 1232);
AssertCompileMemberOffset(NTCONTEXT64, FxSave, 0x100);
AssertCompileMemberOffset(NTCONTEXT64, aRegsVec, 0x300);
/** Pointer to an amd64 NT context. */
typedef NTCONTEXT64 *PNTCONTEXT64;
/** Pointer to a const amd64 NT context. */
typedef const NTCONTEXT64 *PCNTCONTEXT64;


/**
 * 64bit [GI]DT descriptor.
 */
typedef struct NTKCONTEXTDESC64
{
    /** Alignment. */
    uint16_t                    au16Alignment[3];
    /** Limit. */
    uint16_t                    u16Limit;
    /** Base address. */
    uint64_t                    u64PtrBase;
} NTKCONTEXTDESC64;
AssertCompileSize(NTKCONTEXTDESC64, 2 * 8);
/** Pointer to a 64bit [GI]DT descriptor. */
typedef NTKCONTEXTDESC64 *PNTKCONTEXTDESC64;
/** Pointer to a const 64bit [GI]DT descriptor. */
typedef const NTKCONTEXTDESC64 *PCNTKCONTEXTDESC64;


/**
 * Kernel context as queried by KD_PACKET_MANIPULATE_REQ_READ_CTRL_SPACE
 */
typedef struct NTKCONTEXT64
{
    /** CR0 register. */
    uint64_t                    u64RegCr0;
    /** CR2 register. */
    uint64_t                    u64RegCr2;
    /** CR3 register. */
    uint64_t                    u64RegCr3;
    /** CR4 register. */
    uint64_t                    u64RegCr4;
    /** DR0 register. */
    uint64_t                    u64RegDr0;
    /** DR1 register. */
    uint64_t                    u64RegDr1;
    /** DR2 register. */
    uint64_t                    u64RegDr2;
    /** DR3 register. */
    uint64_t                    u64RegDr3;
    /** DR6 register. */
    uint64_t                    u64RegDr6;
    /** DR7 register. */
    uint64_t                    u64RegDr7;
    /** GDTR. */
    NTKCONTEXTDESC64            Gdtr;
    /** IDTR. */
    NTKCONTEXTDESC64            Idtr;
    /** TR register. */
    uint16_t                    u16RegTr;
    /** LDTR register. */
    uint16_t                    u16RegLdtr;
    /** MXCSR register. */
    uint32_t                    u32RegMxCsr;
    /** Debug control. */
    uint64_t                    u64DbgCtrl;
    /** @todo lbr */
    uint64_t                    u64LastBrToRip;
    uint64_t                    u64LastBrFromRip;
    uint64_t                    u64LastExcpToRip;
    uint64_t                    u64LastExcpFromRip;
    /** CR8 register. */
    uint64_t                    u64RegCr8;
    /** GS base MSR register. */
    uint64_t                    u64MsrGsBase;
    /** Kernel GS base MSR register. */
    uint64_t                    u64MsrKernelGsBase;
    /** STAR MSR register. */
    uint64_t                    u64MsrStar;
    /** LSTAR MSR register. */
    uint64_t                    u64MsrLstar;
    /** CSTAR MSR register. */
    uint64_t                    u64MsrCstar;
    /** SFMASK MSR register. */
    uint64_t                    u64MsrSfMask;
    /** XCR0 register. */
    uint64_t                    u64RegXcr0;
    /** Standard context. */
    NTCONTEXT64                 Ctx;
} NTKCONTEXT64;
AssertCompileMemberOffset(NTKCONTEXT64, Ctx, 224);
/** Pointer to an amd64 NT context. */
typedef NTKCONTEXT64 *PNTKCONTEXT64;
/** Pointer to a const amd64 NT context. */
typedef const NTKCONTEXT64 *PCNTKCONTEXT64;


/**
 * 32bit context FPU save area.
 */
typedef struct NTCONTEXT32_FPU_SAVE_AREA
{
    uint32_t                    u32CtrlWord;
    uint32_t                    u32StatusWord;
    uint32_t                    u32TagWord;
    uint32_t                    u32ErrorOff;
    uint32_t                    u32ErrorSel;
    uint32_t                    u32DataOff;
    uint32_t                    u32DataSel;
    X86FPUMMX                   aFpuRegs[8];
    uint32_t                    u32Cr0Npx;
} NTCONTEXT32_FPU_SAVE_AREA;
/** Pointer to an 32bit context FPU save area. */
typedef NTCONTEXT32_FPU_SAVE_AREA *PNTCONTEXT32_FPU_SAVE_AREA;
/** Pointer to a const 32bit context FPU save area. */
typedef const NTCONTEXT32_FPU_SAVE_AREA *PCNTCONTEXT32_FPU_SAVE_AREA;


/**
 * i386 NT context structure.
 */
typedef struct NTCONTEXT32
{
    /** Context flags indicating the valid bits, see NTCONTEXT_F_XXX. */
    uint32_t                    fContext;
    /** DR0 register. */
    uint32_t                    u32RegDr0;
    /** DR1 register. */
    uint32_t                    u32RegDr1;
    /** DR2 register. */
    uint32_t                    u32RegDr2;
    /** DR3 register. */
    uint32_t                    u32RegDr3;
    /** DR6 register. */
    uint32_t                    u32RegDr6;
    /** DR7 register. */
    uint32_t                    u32RegDr7;
    /** Floating point save area. */
    NTCONTEXT32_FPU_SAVE_AREA   FloatSave;
    /** GS segment. */
    uint32_t                    u32SegGs;
    /** FS segment. */
    uint32_t                    u32SegFs;
    /** ES segment. */
    uint32_t                    u32SegEs;
    /** DS segment. */
    uint32_t                    u32SegDs;
    /** EDI register. */
    uint32_t                    u32RegEdi;
    /** ESI register. */
    uint32_t                    u32RegEsi;
    /** EBX register. */
    uint32_t                    u32RegEbx;
    /** EDX register. */
    uint32_t                    u32RegEdx;
    /** ECX register. */
    uint32_t                    u32RegEcx;
    /** EAX register. */
    uint32_t                    u32RegEax;
    /** EBP register. */
    uint32_t                    u32RegEbp;
    /** EIP register. */
    uint32_t                    u32RegEip;
    /** CS segment. */
    uint32_t                    u32SegCs;
    /** EFLAGS register. */
    uint32_t                    u32RegEflags;
    /** ESP register. */
    uint32_t                    u32RegEsp;
    /** SS segment. */
    uint32_t                    u32SegSs;
    /** @todo Extended registers */
    uint8_t                     abRegsExtended[512];
} NTCONTEXT32;
AssertCompileSize(NTCONTEXT32, 716);
/** Pointer to an i386 NT context. */
typedef NTCONTEXT32 *PNTCONTEXT32;
/** Pointer to a const i386 NT context. */
typedef const NTCONTEXT32 *PCNTCONTEXT32;


/**
 * 32bit [GI]DT descriptor.
 */
typedef struct NTKCONTEXTDESC32
{
    /** Alignment. */
    uint16_t                    u16Alignment;
    /** Limit. */
    uint16_t                    u16Limit;
    /** Base address. */
    uint32_t                    u32PtrBase;
} NTKCONTEXTDESC32;
AssertCompileSize(NTKCONTEXTDESC32, 2 * 4);
/** Pointer to an 32bit [GI]DT descriptor. */
typedef NTKCONTEXTDESC32 *PNTKCONTEXTDESC32;
/** Pointer to a const 32bit [GI]DT descriptor. */
typedef const NTKCONTEXTDESC32 *PCNTKCONTEXTDESC32;


/**
 * 32bit Kernel context as queried by KD_PACKET_MANIPULATE_REQ_READ_CTRL_SPACE
 */
typedef struct NTKCONTEXT32
{
    /** CR0 register. */
    uint32_t                    u32RegCr0;
    /** CR2 register. */
    uint32_t                    u32RegCr2;
    /** CR3 register. */
    uint32_t                    u32RegCr3;
    /** CR4 register. */
    uint32_t                    u32RegCr4;
    /** DR0 register. */
    uint32_t                    u32RegDr0;
    /** DR1 register. */
    uint32_t                    u32RegDr1;
    /** DR2 register. */
    uint32_t                    u32RegDr2;
    /** DR3 register. */
    uint32_t                    u32RegDr3;
    /** DR6 register. */
    uint32_t                    u32RegDr6;
    /** DR7 register. */
    uint32_t                    u32RegDr7;
    /** GDTR. */
    NTKCONTEXTDESC32            Gdtr;
    /** IDTR. */
    NTKCONTEXTDESC32            Idtr;
    /** TR register. */
    uint16_t                    u16RegTr;
    /** LDTR register. */
    uint16_t                    u16RegLdtr;
    /** Padding. */
    uint8_t                     abPad[24];
} NTKCONTEXT32;
AssertCompileSize(NTKCONTEXT32, 84);
/** Pointer to an i386 NT context. */
typedef NTKCONTEXT32 *PNTKCONTEXT32;
/** Pointer to a const i386 NT context. */
typedef const NTKCONTEXT32 *PCNTKCONTEXT32;


/** x86 context. */
#define NTCONTEXT_F_X86                             UINT32_C(0x00010000)
/** AMD64 context. */
#define NTCONTEXT_F_AMD64                           UINT32_C(0x00100000)
/** Control registers valid (CS, (R)SP, (R)IP, FLAGS and BP). */
#define NTCONTEXT_F_CONTROL                         RT_BIT_32(0)
/** Integer registers valid. */
#define NTCONTEXT_F_INTEGER                         RT_BIT_32(1)
/** Segment registers valid. */
#define NTCONTEXT_F_SEGMENTS                        RT_BIT_32(2)
/** Floating point registers valid. */
#define NTCONTEXT_F_FLOATING_POINT                  RT_BIT_32(3)
/** Debug registers valid. */
#define NTCONTEXT_F_DEBUG                           RT_BIT_32(4)
/** Extended registers valid (x86 only). */
#define NTCONTEXT_F_EXTENDED                        RT_BIT_32(5)
/** Full x86 context valid. */
#define NTCONTEXT32_F_FULL (NTCONTEXT_F_X86 | NTCONTEXT_F_CONTROL | NTCONTEXT_F_INTEGER | NTCONTEXT_F_SEGMENTS)
/** Full amd64 context valid. */
#define NTCONTEXT64_F_FULL (NTCONTEXT_F_AMD64 | NTCONTEXT_F_CONTROL | NTCONTEXT_F_INTEGER | NTCONTEXT_F_SEGMENTS)


/**
 * 32bit exception record.
 */
typedef struct KDPACKETEXCP32
{
    /** The exception code identifying the excpetion. */
    uint32_t                    u32ExcpCode;
    /** Flags associated with the exception. */
    uint32_t                    u32ExcpFlags;
    /** Pointer to a chained exception record. */
    uint32_t                    u32PtrExcpRecNested;
    /** Address where the exception occurred. */
    uint32_t                    u32PtrExcpAddr;
    /** Number of parameters in the exception information array. */
    uint32_t                    cExcpParms;
    /** Exception parameters array. */
    uint32_t                    au32ExcpParms[KDPACKETEXCP_PARMS_MAX];
} KDPACKETEXCP32;
AssertCompileSize(KDPACKETEXCP32, 80);
/** Pointer to an exception record. */
typedef KDPACKETEXCP32 *PKDPACKETEXCP32;
/** Pointer to a const exception record. */
typedef const KDPACKETEXCP32 *PCKDPACKETEXCP32;


/** @name Exception codes.
 * @{ */
/** A breakpoint was hit. */
#define KD_PACKET_EXCP_CODE_BKPT                    UINT32_C(0x80000003)
/** An instruction was single stepped. */
#define KD_PACKET_EXCP_CODE_SINGLE_STEP             UINT32_C(0x80000004)
/** @} */


/** Maximum number of bytes in the instruction stream. */
#define KD_PACKET_CTRL_REPORT_INSN_STREAM_MAX       16

/**
 * 64bit control report record.
 */
typedef struct KDPACKETCTRLREPORT64
{
    /** Value of DR6. */
    uint64_t                    u64RegDr6;
    /** Value of DR7. */
    uint64_t                    u64RegDr7;
    /** EFLAGS. */
    uint32_t                    u32RegEflags;
    /** Number of instruction bytes in the instruction stream. */
    uint16_t                    cbInsnStream;
    /** Report flags. */
    uint16_t                    fFlags;
    /** The instruction stream. */
    uint8_t                     abInsn[KD_PACKET_CTRL_REPORT_INSN_STREAM_MAX];
    /** CS selector. */
    uint16_t                    u16SegCs;
    /** DS selector. */
    uint16_t                    u16SegDs;
    /** ES selector. */
    uint16_t                    u16SegEs;
    /** FS selector. */
    uint16_t                    u16SegFs;
} KDPACKETCTRLREPORT64;
AssertCompileSize(KDPACKETCTRLREPORT64, 2 * 8 + 4 + 2 * 2 + 16 + 4 * 2);
/** Pointer to a control report record. */
typedef KDPACKETCTRLREPORT64 *PKDPACKETCTRLREPORT64;
/** Pointer to a const control report record. */
typedef const KDPACKETCTRLREPORT64 *PCKDPACKETCTRLREPORT64;


/**
 * 64bit state change packet body.
 */
typedef struct KDPACKETSTATECHANGE64
{
    /** The new state. */
    uint32_t                    u32StateNew;
    /** The processor level. */
    uint16_t                    u16CpuLvl;
    /** The processor ID generating the state change. */
    uint16_t                    idCpu;
    /** Number of processors in the system. */
    uint32_t                    cCpus;
    /** Alignment. */
    uint32_t                    u32Alignment;
    /** The thread ID currently executing when the state change occurred. */
    uint64_t                    idThread;
    /** Program counter of the thread. */
    uint64_t                    u64RipThread;
    /** Data based on the state. */
    union
    {
        /** Exception occurred data. */
        struct
        {
            /** The exception record. */
            KDPACKETEXCP64      ExcpRec;
            /** First chance(?). */
            uint32_t            u32FirstChance;
        } Exception;
    } u;
    /** The control report */
    union
    {
        /** AMD64 control report. */
        KDPACKETCTRLREPORT64    Amd64;
    } uCtrlReport;
} KDPACKETSTATECHANGE64;
//AssertCompileSize(KDPACKETSTATECHANGE64, 4 + 2 * 2 + 2 * 4 + 2 * 8 + sizeof(KDPACKETEXCP64) + 4 + sizeof(KDPACKETCTRLREPORT64));
/** Pointer to a 64bit state change packet body. */
typedef KDPACKETSTATECHANGE64 *PKDPACKETSTATECHANGE64;
/** Pointer to a const 64bit state change packet body. */
typedef const KDPACKETSTATECHANGE64 *PCKDPACKETSTATECHANGE64;


/** @name State change state types.
 * @{ */
/** Minimum state change type. */
#define KD_PACKET_STATE_CHANGE_MIN                  UINT32_C(0x00003030)
/** An exception occured. */
#define KD_PACKET_STATE_CHANGE_EXCEPTION            KD_PACKET_STATE_CHANGE_MIN
/** Symbols were loaded(?). */
#define KD_PACKET_STATE_CHANGE_LOAD_SYMBOLS         UINT32_C(0x00003031)
/** Command string (custom command was executed?). */
#define KD_PACKET_STATE_CHANGE_CMD_STRING           UINT32_C(0x00003032)
/** Maximum state change type (exclusive). */
#define KD_PACKET_STATE_CHANGE_MAX                  UINT32_C(0x00003033)
/** @} */


/**
 * Debug I/O payload.
 */
typedef struct KDPACKETDEBUGIO
{
    /** Debug I/O payload type (KD_PACKET_DEBUG_IO_STRING). */
    uint32_t                    u32Type;
    /** The processor level. */
    uint16_t                    u16CpuLvl;
    /** The processor ID generating this packet. */
    uint16_t                    idCpu;
    /** Type dependent data. */
    union
    {
        /** Debug string sent. */
        struct
        {
            /** Length of the string following in bytes. */
            uint32_t            cbStr;
            /** Some padding it looks like. */
            uint32_t            u32Pad;
        } Str;
        /** Debug prompt. */
        struct
        {
            /** Length of prompt. */
            uint32_t            cbPrompt;
            /** Size of the string returned on success. */
            uint32_t            cbReturn;
        } Prompt;
    } u;
} KDPACKETDEBUGIO;
AssertCompileSize(KDPACKETDEBUGIO, 16);
/** Pointer to a Debug I/O payload. */
typedef KDPACKETDEBUGIO *PKDPACKETDEBUGIO;
/** Pointer to a const Debug I/O payload. */
typedef const KDPACKETDEBUGIO *PCKDPACKETDEBUGIO;


/** @name Debug I/O types.
 * @{ */
/** Debug string output (usually DbgPrint() and friends). */
#define KD_PACKET_DEBUG_IO_STRING                   UINT32_C(0x00003230)
/** Get debug string (DbgPrompt()). */
#define KD_PACKET_DEBUG_IO_GET_STRING               UINT32_C(0x00003231)
/** @} */


/**
 * 64bit get version manipulate payload.
 */
typedef struct KDPACKETMANIPULATE_GETVERSION64
{
    /** Major version. */
    uint16_t                    u16VersMaj;
    /** Minor version. */
    uint16_t                    u16VersMin;
    /** Protocol version. */
    uint8_t                     u8VersProtocol;
    /** KD secondary version. */
    uint8_t                     u8VersKdSecondary;
    /** Flags. */
    uint16_t                    fFlags;
    /** Machine type. */
    uint16_t                    u16MachineType;
    /** Maximum packet type. */
    uint8_t                     u8MaxPktType;
    /** Maximum state change */
    uint8_t                     u8MaxStateChange;
    /** Maximum manipulate request ID. */
    uint8_t                     u8MaxManipulate;
    /** Some simulation flag. */
    uint8_t                     u8Simulation;
    /** Padding. */
    uint16_t                    u16Padding;
    /** Kernel base. */
    uint64_t                    u64PtrKernBase;
    /** Pointer of the loaded module list head. */
    uint64_t                    u64PtrPsLoadedModuleList;
    /** Pointer of the debugger data list. */
    uint64_t                    u64PtrDebuggerDataList;
} KDPACKETMANIPULATE_GETVERSION64;
AssertCompileSize(KDPACKETMANIPULATE_GETVERSION64, 40);
/** Pointer to a 64bit get version manipulate payload. */
typedef KDPACKETMANIPULATE_GETVERSION64 *PKDPACKETMANIPULATE_GETVERSION64;
/** Pointer to a const 64bit get version manipulate payload. */
typedef const KDPACKETMANIPULATE_GETVERSION64 *PCKDPACKETMANIPULATE_GETVERSION64;


/** @name Get version flags.
 * @{ */
/** Flag whether this is a multi processor kernel. */
#define KD_PACKET_MANIPULATE64_GET_VERSION_F_MP     RT_BIT_32(0)
/** Flag whether the pointer is 64bit. */
#define KD_PACKET_MANIPULATE64_GET_VERSION_F_PTR64  RT_BIT_32(2)
/** @} */


/**
 * 64bit memory transfer manipulate payload.
 */
typedef struct KDPACKETMANIPULATE_XFERMEM64
{
    /** Target base address. */
    uint64_t                    u64PtrTarget;
    /** Requested number of bytes to transfer*/
    uint32_t                    cbXferReq;
    /** Number of bytes actually transferred (response). */
    uint32_t                    cbXfered;
    /** Some padding?. */
    uint64_t                    au64Pad[3];
} KDPACKETMANIPULATE_XFERMEM64;
AssertCompileSize(KDPACKETMANIPULATE_XFERMEM64, 40);
/** Pointer to a 64bit memory transfer manipulate payload. */
typedef KDPACKETMANIPULATE_XFERMEM64 *PKDPACKETMANIPULATE_XFERMEM64;
/** Pointer to a const 64bit memory transfer manipulate payload. */
typedef const KDPACKETMANIPULATE_XFERMEM64 *PCKDPACKETMANIPULATE_XFERMEM64;


/**
 * 64bit control space transfer manipulate payload.
 *
 * @note Same layout as the memory transfer but the pointer has a different meaning so
 *       we moved it into a separate request structure.
 */
typedef struct KDPACKETMANIPULATE_XFERCTRLSPACE64
{
    /** Identifier of the item to transfer in the control space. */
    uint64_t                    u64IdXfer;
    /** Requested number of bytes to transfer*/
    uint32_t                    cbXferReq;
    /** Number of bytes actually transferred (response). */
    uint32_t                    cbXfered;
    /** Some padding?. */
    uint64_t                    au64Pad[3];
} KDPACKETMANIPULATE_XFERCTRLSPACE64;
AssertCompileSize(KDPACKETMANIPULATE_XFERCTRLSPACE64, 40);
/** Pointer to a 64bit memory transfer manipulate payload. */
typedef KDPACKETMANIPULATE_XFERCTRLSPACE64 *PKDPACKETMANIPULATE_XFERCTRLSPACE64;
/** Pointer to a const 64bit memory transfer manipulate payload. */
typedef const KDPACKETMANIPULATE_XFERCTRLSPACE64 *PCKDPACKETMANIPULATE_XFERCTRLSPACE64;


/** @name Known control space identifiers.
 * @{ */
/** Read/Write KPCR address. */
#define KD_PACKET_MANIPULATE64_CTRL_SPACE_ID_KPCR   UINT64_C(0)
/** Read/Write KPCRB address. */
#define KD_PACKET_MANIPULATE64_CTRL_SPACE_ID_KPCRB  UINT64_C(1)
/** Read/Write Kernel context. */
#define KD_PACKET_MANIPULATE64_CTRL_SPACE_ID_KCTX   UINT64_C(2)
/** Read/Write current kernel thread. */
#define KD_PACKET_MANIPULATE64_CTRL_SPACE_ID_KTHRD  UINT64_C(3)
/** @} */


/**
 * 64bit restore breakpoint manipulate payload.
 */
typedef struct KDPACKETMANIPULATE_RESTOREBKPT64
{
    /** The breakpoint handle to restore. */
    uint32_t                    u32HndBkpt;
    /** Blows up the request to the required size. */
    uint8_t                     abPad[36];
} KDPACKETMANIPULATE_RESTOREBKPT64;
AssertCompileSize(KDPACKETMANIPULATE_RESTOREBKPT64, 40);
/** Pointer to a 64bit restore breakpoint manipulate payload. */
typedef KDPACKETMANIPULATE_RESTOREBKPT64 *PKDPACKETMANIPULATE_RESTOREBKPT64;
/** Pointer to a const 64bit restore breakpoint manipulate payload. */
typedef const KDPACKETMANIPULATE_RESTOREBKPT64 *PCKDPACKETMANIPULATE_RESTOREBKPT64;


/**
 * 64bit write breakpoint manipulate payload.
 */
typedef struct KDPACKETMANIPULATE_WRITEBKPT64
{
    /** Where to write the breakpoint. */
    uint64_t                    u64PtrBkpt;
    /** The breakpoint handle returned in the response. */
    uint32_t                    u32HndBkpt;
    /** Blows up the request to the required size. */
    uint8_t                     abPad[28];
} KDPACKETMANIPULATE_WRITEBKPT64;
AssertCompileSize(KDPACKETMANIPULATE_WRITEBKPT64, 40);
/** Pointer to a 64bit write breakpoint manipulate payload. */
typedef KDPACKETMANIPULATE_WRITEBKPT64 *PKDPACKETMANIPULATE_WRITEBKPT64;
/** Pointer to a const 64bit write breakpoint manipulate payload. */
typedef const KDPACKETMANIPULATE_WRITEBKPT64 *PCKDPACKETMANIPULATE_WRITEBKPT64;


/**
 * Context extended manipulate payload.
 */
typedef struct KDPACKETMANIPULATE_CONTEXTEX
{
    /** Where to start copying the context. */
    uint32_t                    offStart;
    /** Number of bytes to transfer. */
    uint32_t                    cbXfer;
    /** Number of bytes actually transfered. */
    uint32_t                    cbXfered;
    /** Blows up the request to the required size. */
    uint8_t                     abPad[28];
} KDPACKETMANIPULATE_CONTEXTEX;
AssertCompileSize(KDPACKETMANIPULATE_CONTEXTEX, 40);
/** Pointer to a context extended manipulate payload. */
typedef KDPACKETMANIPULATE_CONTEXTEX *PKDPACKETMANIPULATE_CONTEXTEX;
/** Pointer to a const context extended manipulate payload. */
typedef const KDPACKETMANIPULATE_CONTEXTEX *PCKDPACKETMANIPULATE_CONTEXTEX;


/**
 * Continue manipulate payload.
 */
typedef struct KDPACKETMANIPULATE_CONTINUE
{
    /** Continue (status?). */
    uint32_t                    u32NtContSts;
    /** Blows up the request to the required size. */
    uint8_t                     abPad[36];
} KDPACKETMANIPULATE_CONTINUE;
AssertCompileSize(KDPACKETMANIPULATE_CONTINUE, 40);
/** Pointer to a continue manipulate payload. */
typedef KDPACKETMANIPULATE_CONTINUE *PKDPACKETMANIPULATE_CONTINUE;
/** Pointer to a const continue manipulate payload. */
typedef const KDPACKETMANIPULATE_CONTINUE *PCKDPACKETMANIPULATE_CONTINUE;


/**
 * Continue 2 manipulate payload.
 */
typedef struct KDPACKETMANIPULATE_CONTINUE2
{
    /** Continue (status?). */
    uint32_t                    u32NtContSts;
    /** Trace flag. */
    uint32_t                    fTrace;
    /** Bitsize dependent data. */
    union
    {
        /** 32bit. */
        struct
        {
            /** DR7 value to continue with. */
            uint32_t            u32RegDr7;
            /** @todo (?) */
            uint32_t            u32SymCurStart;
            uint32_t            u32SymCurEnd;
        } x86;
        /** 64bit. */
        struct
        {
            /** DR7 value to continue with. */
            uint64_t            u64RegDr7;
            /** @todo (?) */
            uint64_t            u64SymCurStart;
            uint64_t            u64SymCurEnd;
        } amd64;
    } u;
    /** Blows up the request to the required size. */
    uint8_t                     abPad[8];
} KDPACKETMANIPULATE_CONTINUE2;
AssertCompileSize(KDPACKETMANIPULATE_CONTINUE2, 40);
/** Pointer to a continue 2 manipulate payload. */
typedef KDPACKETMANIPULATE_CONTINUE2 *PKDPACKETMANIPULATE_CONTINUE2;
/** Pointer to a const continue 2 manipulate payload. */
typedef const KDPACKETMANIPULATE_CONTINUE2 *PCKDPACKETMANIPULATE_CONTINUE2;


/**
 * Set context manipulate payload.
 */
typedef struct KDPACKETMANIPULATE_SETCONTEXT
{
    /** Continue (status?). */
    uint32_t                    u32CtxFlags;
    /** Blows up the request to the required size. */
    uint8_t                     abPad[36];
} KDPACKETMANIPULATE_SETCONTEXT;
AssertCompileSize(KDPACKETMANIPULATE_SETCONTEXT, 40);
/** Pointer to a set context manipulate payload. */
typedef KDPACKETMANIPULATE_SETCONTEXT *PKDPACKETMANIPULATE_SETCONTEXT;
/** Pointer to a const set context manipulate payload. */
typedef const KDPACKETMANIPULATE_SETCONTEXT *PCKDPACKETMANIPULATE_SETCONTEXT;


/**
 * Query memory properties payload.
 */
typedef struct KDPACKETMANIPULATE_QUERYMEMORY
{
    /** The address to query the properties for. */
    uint64_t                    u64GCPtr;
    /** Reserved. */
    uint64_t                    u64Rsvd;
    /** Address space type on return. */
    uint32_t                    u32AddrSpace;
    /** Protection flags. */
    uint32_t                    u32Flags;
    /** Blows up the request to the required size. */
    uint8_t                     abPad[16];
} KDPACKETMANIPULATE_QUERYMEMORY;
AssertCompileSize(KDPACKETMANIPULATE_QUERYMEMORY, 40);
/** Pointer to a query memory properties payload. */
typedef KDPACKETMANIPULATE_QUERYMEMORY *PKDPACKETMANIPULATE_QUERYMEMORY;
/** Pointer to a const query memory properties payload. */
typedef const KDPACKETMANIPULATE_QUERYMEMORY *PCKDPACKETMANIPULATE_QUERYMEMORY;


/** @name Query memory address space identifiers.
 * @{ */
/** Process memory space. */
#define KD_PACKET_MANIPULATE64_QUERY_MEMORY_ADDR_SPACE_PROCESS  UINT32_C(0)
/** Session memory space. */
#define KD_PACKET_MANIPULATE64_QUERY_MEMORY_ADDR_SPACE_SESSION  UINT32_C(1)
/** Kernel memory space. */
#define KD_PACKET_MANIPULATE64_QUERY_MEMORY_ADDR_SPACE_KERNEL UINT32_C(2)
/** @} */


/** @name Query memory address protection flags.
 * @{ */
/** Readable. */
#define KD_PACKET_MANIPULATE64_QUERY_MEMORY_ADDR_F_READ         RT_BIT_32(0)
/** Writable. */
#define KD_PACKET_MANIPULATE64_QUERY_MEMORY_ADDR_F_WRITE        RT_BIT_32(1)
/** Executable. */
#define KD_PACKET_MANIPULATE64_QUERY_MEMORY_ADDR_F_EXEC         RT_BIT_32(2)
/** Fixed address. */
#define KD_PACKET_MANIPULATE64_QUERY_MEMORY_ADDR_F_FIXED        RT_BIT_32(3)
/** @} */


/**
 * Search memory payload.
 */
typedef struct KDPACKETMANIPULATE_SEARCHMEMORY
{
    /** The address to start searching at on input, found address on output. */
    uint64_t                    u64GCPtr;
    /** Number of bytes to search. */
    uint64_t                    cbSearch;
    /** Length of the pattern to search for following the payload. */
    uint32_t                    cbPattern;
    /** Padding to the required size. */
    uint32_t                    au32Pad[5];
} KDPACKETMANIPULATE_SEARCHMEMORY;
AssertCompileSize(KDPACKETMANIPULATE_SEARCHMEMORY, 40);
/** Pointer to a search memory properties payload. */
typedef KDPACKETMANIPULATE_SEARCHMEMORY *PKDPACKETMANIPULATE_SEARCHMEMORY;
/** Pointer to a const search memory properties payload. */
typedef const KDPACKETMANIPULATE_SEARCHMEMORY *PCKDPACKETMANIPULATE_SEARCHMEMORY;


/**
 * Manipulate request packet header (Same for 32bit and 64bit).
 */
typedef struct KDPACKETMANIPULATEHDR
{
    /** The request to execute. */
    uint32_t                    idReq;
    /** The processor level to execute the request on. */
    uint16_t                    u16CpuLvl;
    /** The processor ID to execute the request on. */
    uint16_t                    idCpu;
    /** Return status code. */
    uint32_t                    u32NtStatus;
    /** Alignment. */
    uint32_t                    u32Alignment;
} KDPACKETMANIPULATEHDR;
AssertCompileSize(KDPACKETMANIPULATEHDR, 3 * 4 + 2 * 2);
/** Pointer to a manipulate request packet header. */
typedef KDPACKETMANIPULATEHDR *PKDPACKETMANIPULATEHDR;
/** Pointer to a const manipulate request packet header. */
typedef const KDPACKETMANIPULATEHDR *PCPKDPACKETMANIPULATEHDR;


/**
 * 64bit manipulate state request packet.
 */
typedef struct KDPACKETMANIPULATE64
{
    /** Header. */
    KDPACKETMANIPULATEHDR                  Hdr;
    /** Request payloads. */
    union
    {
        /** Get Version. */
        KDPACKETMANIPULATE_GETVERSION64    GetVersion;
        /** Read/Write memory. */
        KDPACKETMANIPULATE_XFERMEM64       XferMem;
        /** Continue. */
        KDPACKETMANIPULATE_CONTINUE        Continue;
        /** Continue2. */
        KDPACKETMANIPULATE_CONTINUE2       Continue2;
        /** Set context. */
        KDPACKETMANIPULATE_SETCONTEXT      SetContext;
        /** Read/Write control space. */
        KDPACKETMANIPULATE_XFERCTRLSPACE64 XferCtrlSpace;
        /** Restore breakpoint. */
        KDPACKETMANIPULATE_RESTOREBKPT64   RestoreBkpt;
        /** Write breakpoint. */
        KDPACKETMANIPULATE_WRITEBKPT64     WriteBkpt;
        /** Context extended. */
        KDPACKETMANIPULATE_CONTEXTEX       ContextEx;
        /** Query memory. */
        KDPACKETMANIPULATE_QUERYMEMORY     QueryMemory;
        /** Search memory. */
        KDPACKETMANIPULATE_SEARCHMEMORY    SearchMemory;
    } u;
} KDPACKETMANIPULATE64;
AssertCompileSize(KDPACKETMANIPULATE64, 16 + 40);
/** Pointer to a 64bit manipulate state request packet. */
typedef KDPACKETMANIPULATE64 *PKDPACKETMANIPULATE64;
/** Pointer to a const 64bit manipulate state request packet. */
typedef const KDPACKETMANIPULATE64 *PCKDPACKETMANIPULATE64;

/** @name Manipulate requests.
 * @{ */
/** Minimum available request. */
#define KD_PACKET_MANIPULATE_REQ_MIN                        UINT32_C(0x00003130)
/** Read virtual memory request. */
#define KD_PACKET_MANIPULATE_REQ_READ_VIRT_MEM              KD_PACKET_MANIPULATE_REQ_MIN
/** Write virtual memory request. */
#define KD_PACKET_MANIPULATE_REQ_WRITE_VIRT_MEM             UINT32_C(0x00003131)
/** Get context request. */
#define KD_PACKET_MANIPULATE_REQ_GET_CONTEXT                UINT32_C(0x00003132)
/** Set context request. */
#define KD_PACKET_MANIPULATE_REQ_SET_CONTEXT                UINT32_C(0x00003133)
/** Write breakpoint request. */
#define KD_PACKET_MANIPULATE_REQ_WRITE_BKPT                 UINT32_C(0x00003134)
/** Restore breakpoint request. */
#define KD_PACKET_MANIPULATE_REQ_RESTORE_BKPT               UINT32_C(0x00003135)
/** Continue request. */
#define KD_PACKET_MANIPULATE_REQ_CONTINUE                   UINT32_C(0x00003136)
/** Read control space request. */
#define KD_PACKET_MANIPULATE_REQ_READ_CTRL_SPACE            UINT32_C(0x00003137)
/** Write control space request. */
#define KD_PACKET_MANIPULATE_REQ_WRITE_CTRL_SPACE           UINT32_C(0x00003138)
/** Read I/O space request. */
#define KD_PACKET_MANIPULATE_REQ_READ_IO_SPACE              UINT32_C(0x00003139)
/** Write I/O space request. */
#define KD_PACKET_MANIPULATE_REQ_WRITE_IO_SPACE             UINT32_C(0x0000313a)
/** Reboot request. */
#define KD_PACKET_MANIPULATE_REQ_REBOOT                     UINT32_C(0x0000313b)
/** continue 2nd version request. */
#define KD_PACKET_MANIPULATE_REQ_CONTINUE2                  UINT32_C(0x0000313c)
/** Read physical memory request. */
#define KD_PACKET_MANIPULATE_REQ_READ_PHYS_MEM              UINT32_C(0x0000313d)
/** Write physical memory request. */
#define KD_PACKET_MANIPULATE_REQ_WRITE_PHYS_MEM             UINT32_C(0x0000313e)
/** Query special calls request. */
#define KD_PACKET_MANIPULATE_REQ_QUERY_SPEC_CALLS           UINT32_C(0x0000313f)
/** Set special calls request. */
#define KD_PACKET_MANIPULATE_REQ_SET_SPEC_CALLS             UINT32_C(0x00003140)
/** Clear special calls request. */
#define KD_PACKET_MANIPULATE_REQ_CLEAR_SPEC_CALLS           UINT32_C(0x00003141)
/** Set internal breakpoint request. */
#define KD_PACKET_MANIPULATE_REQ_SET_INTERNAL_BKPT          UINT32_C(0x00003142)
/** Get internal breakpoint request. */
#define KD_PACKET_MANIPULATE_REQ_GET_INTERNAL_BKPT          UINT32_C(0x00003143)
/** Read I/O space extended request. */
#define KD_PACKET_MANIPULATE_REQ_READ_IO_SPACE_EX           UINT32_C(0x00003144)
/** Write I/O space extended request. */
#define KD_PACKET_MANIPULATE_REQ_WRITE_IO_SPACE_EX          UINT32_C(0x00003145)
/** Get version request. */
#define KD_PACKET_MANIPULATE_REQ_GET_VERSION                UINT32_C(0x00003146)
/** Write breakpoint extended request. */
#define KD_PACKET_MANIPULATE_REQ_WRITE_BKPT_EX              UINT32_C(0x00003147)
/** Restore breakpoint extended request. */
#define KD_PACKET_MANIPULATE_REQ_RESTORE_BKPT_EX            UINT32_C(0x00003148)
/** Cause a bugcheck request. */
#define KD_PACKET_MANIPULATE_REQ_CAUSE_BUGCHECK             UINT32_C(0x00003149)
/** Cause a bugcheck request. */
#define KD_PACKET_MANIPULATE_REQ_SWITCH_PROCESSOR           UINT32_C(0x00003150)
/** @todo 0x3151-0x3155 */
/** Search memory for a pattern request. */
#define KD_PACKET_MANIPULATE_REQ_SEARCH_MEMORY              UINT32_C(0x00003156)
/** @todo 0x3157-0x3159 */
/** Clear all internal breakpoints request. */
#define KD_PACKET_MANIPULATE_REQ_CLEAR_ALL_INTERNAL_BKPT    UINT32_C(0x0000315a)
/** Fill memory. */
#define KD_PACKET_MANIPULATE_REQ_FILL_MEMORY                UINT32_C(0x0000315b)
/** Query memory properties. */
#define KD_PACKET_MANIPULATE_REQ_QUERY_MEMORY               UINT32_C(0x0000315c)
/** @todo 0x315d, 0x315e */
/** Get context extended request. */
#define KD_PACKET_MANIPULATE_REQ_GET_CONTEXT_EX             UINT32_C(0x0000315f)
/** @todo 0x3160 */
/** Maximum available request (exclusive). */
#define KD_PACKET_MANIPULATE_REQ_MAX                        UINT32_C(0x00003161)
/** @} */

/**
 * KD stub receive state.
 */
typedef enum KDRECVSTATE
{
    /** Invalid state. */
    KDRECVSTATE_INVALID = 0,
    /** Receiving the first byte of the packet header. */
    KDRECVSTATE_PACKET_HDR_FIRST_BYTE,
    /** Receiving the second byte of the packet header. */
    KDRECVSTATE_PACKET_HDR_SECOND_BYTE,
    /** Receiving the header. */
    KDRECVSTATE_PACKET_HDR,
    /** Receiving the packet body. */
    KDRECVSTATE_PACKET_BODY,
    /** Receiving the trailing byte. */
    KDRECVSTATE_PACKET_TRAILER,
    /** Blow up the enum to 32bits for easier alignment of members in structs. */
    KDRECVSTATE_32BIT_HACK = 0x7fffffff
} KDRECVSTATE;


/**
 * KD emulated hardware breakpoint.
 */
typedef struct KDCTXHWBP
{
    /** The DBGF breakpoint handle if active, NIL_DBGFBP if not active. */
    DBGFBP                      hDbgfBp;
    /** The linear address of the breakpoint if active. */
    RTGCPTR                     GCPtrBp;
    /** Access type of the breakpoint, see X86_DR7_RW_*. */
    uint8_t                     fAcc;
    /** Length flags of the breakpoint. */
    uint8_t                     fLen;
    /** Flag whether it is a local breakpoint. */
    bool                        fLocal;
    /** Flag whether it is a global breakpoint. */
    bool                        fGlobal;
    /** Flag whether the breakpoint has triggered since the last time of the reset. */
    bool                        fTriggered;
} KDCTXHWBP;
/** Pointer to an emulated hardware breakpoint. */
typedef KDCTXHWBP *PKDCTXHWBP;
/** Pointer to a const emulated hardware breakpoint. */
typedef const KDCTXHWBP *PCKDCTXHWBP;


/**
 * KD context data.
 */
typedef struct KDCTX
{
    /** Internal debugger console data. */
    DBGC                        Dbgc;
    /** Number of bytes received left for the current state. */
    size_t                      cbRecvLeft;
    /** Pointer where to write the next received data. */
    uint8_t                     *pbRecv;
    /** The current state when receiving a new packet. */
    KDRECVSTATE                 enmState;
    /** The timeout waiting for new data. */
    RTMSINTERVAL                msRecvTimeout;
    /** Timestamp when we last received data from the remote end. */
    uint64_t                    tsRecvLast;
    /** Packet header being received. */
    union
    {
        KDPACKETHDR             Fields;
        uint8_t                 ab[16];
    } PktHdr;
    /** The next packet ID to send. */
    uint32_t                    idPktNext;
    /** Offset into the body receive buffer. */
    size_t                      offBodyRecv;
    /** Body data. */
    uint8_t                     abBody[_4K];
    /** The trailer byte storage. */
    uint8_t                     bTrailer;
    /** Flag whether a breakin packet was received since the last time it was reset. */
    bool                        fBreakinRecv;
    /** Flag whether we entered the native VBox hypervisor through a bugcheck request. */
    bool                        fInVBoxDbg;

    /** Emulated hardware breakpoint handling. */
    KDCTXHWBP                   aHwBp[4];
    /** Flag whether a single step completed since last time this was cleared. */
    bool                        fSingleStepped;

    /** Pointer to the OS digger WinNt interface if a matching guest was detected. */
    PDBGFOSIWINNT               pIfWinNt;
    /** Flag whether the detected guest is 32bit (false if 64bit). */
    bool                        f32Bit;
} KDCTX;
/** Pointer to the KD context data. */
typedef KDCTX *PKDCTX;
/** Pointer to const KD context data. */
typedef const KDCTX *PCKDCTX;
/** Pointer to a KD context data pointer. */
typedef PKDCTX *PPKDCTX;


/**
 * Register mapping descriptor.
 */
typedef struct KDREGDESC
{
    /** The DBGF register enum. */
    DBGFREG                     enmReg;
    /** Register width. */
    DBGFREGVALTYPE              enmValType;
    /** The offset into the context structure where the value ends up. */
    uintptr_t                   offReg;
} KDREGDESC;
/** Pointer to a register mapping structure. */
typedef KDREGDESC *PKDREGDESC;
/** Pointer to a const register mapping structure. */
typedef const KDREGDESC *PCKDREGDESC;


/** Creates a possibly sign extended guest context pointer which is required for 32bit targets. */
#define KD_PTR_CREATE(a_pThis, a_GCPtr) ((a_pThis)->f32Bit && ((a_GCPtr) & RT_BIT_32(31)) ? (a_GCPtr) | UINT64_C(0xffffffff00000000) : (a_GCPtr))
/** Returns the value of a possibly sign extended guest context pointer received for 32bit targets. */
#define KD_PTR_GET(a_pThis, a_GCPtr) ((a_pThis)->f32Bit ? (a_GCPtr) & ~UINT64_C(0xffffffff00000000) : (a_GCPtr))


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

/** 64bit control register set. */
static const KDREGDESC g_aRegsCtrl64[] =
{
    { DBGFREG_CS,           DBGFREGVALTYPE_U16,     RT_UOFFSETOF(NTCONTEXT64, u16SegCs)             },
    { DBGFREG_SS,           DBGFREGVALTYPE_U16,     RT_UOFFSETOF(NTCONTEXT64, u16SegSs)             },
    { DBGFREG_RIP,          DBGFREGVALTYPE_U64,     RT_UOFFSETOF(NTCONTEXT64, u64RegRip)            },
    { DBGFREG_RSP,          DBGFREGVALTYPE_U64,     RT_UOFFSETOF(NTCONTEXT64, u64RegRsp)            },
    { DBGFREG_RBP,          DBGFREGVALTYPE_U64,     RT_UOFFSETOF(NTCONTEXT64, u64RegRbp)            },
    { DBGFREG_EFLAGS,       DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT64, u32RegEflags)         }
};


/** 64bit integer register set. */
static const KDREGDESC g_aRegsInt64[] =
{
    { DBGFREG_RAX,          DBGFREGVALTYPE_U64,     RT_UOFFSETOF(NTCONTEXT64, u64RegRax)            },
    { DBGFREG_RCX,          DBGFREGVALTYPE_U64,     RT_UOFFSETOF(NTCONTEXT64, u64RegRcx)            },
    { DBGFREG_RDX,          DBGFREGVALTYPE_U64,     RT_UOFFSETOF(NTCONTEXT64, u64RegRdx)            },
    { DBGFREG_RBX,          DBGFREGVALTYPE_U64,     RT_UOFFSETOF(NTCONTEXT64, u64RegRbx)            },
    { DBGFREG_RSI,          DBGFREGVALTYPE_U64,     RT_UOFFSETOF(NTCONTEXT64, u64RegRsi)            },
    { DBGFREG_RDI,          DBGFREGVALTYPE_U64,     RT_UOFFSETOF(NTCONTEXT64, u64RegRdi)            },
    { DBGFREG_R8,           DBGFREGVALTYPE_U64,     RT_UOFFSETOF(NTCONTEXT64, u64RegR8)             },
    { DBGFREG_R9,           DBGFREGVALTYPE_U64,     RT_UOFFSETOF(NTCONTEXT64, u64RegR9)             },
    { DBGFREG_R10,          DBGFREGVALTYPE_U64,     RT_UOFFSETOF(NTCONTEXT64, u64RegR10)            },
    { DBGFREG_R11,          DBGFREGVALTYPE_U64,     RT_UOFFSETOF(NTCONTEXT64, u64RegR11)            },
    { DBGFREG_R12,          DBGFREGVALTYPE_U64,     RT_UOFFSETOF(NTCONTEXT64, u64RegR12)            },
    { DBGFREG_R13,          DBGFREGVALTYPE_U64,     RT_UOFFSETOF(NTCONTEXT64, u64RegR13)            },
    { DBGFREG_R14,          DBGFREGVALTYPE_U64,     RT_UOFFSETOF(NTCONTEXT64, u64RegR14)            },
    { DBGFREG_R15,          DBGFREGVALTYPE_U64,     RT_UOFFSETOF(NTCONTEXT64, u64RegR15)            }
};


/** 64bit segments register set. */
static const KDREGDESC g_aRegsSegs64[] =
{
    { DBGFREG_DS,           DBGFREGVALTYPE_U16,     RT_UOFFSETOF(NTCONTEXT64, u16SegDs)             },
    { DBGFREG_ES,           DBGFREGVALTYPE_U16,     RT_UOFFSETOF(NTCONTEXT64, u16SegEs)             },
    { DBGFREG_FS,           DBGFREGVALTYPE_U16,     RT_UOFFSETOF(NTCONTEXT64, u16SegFs)             },
    { DBGFREG_GS,           DBGFREGVALTYPE_U16,     RT_UOFFSETOF(NTCONTEXT64, u16SegGs)             }
};


/** 64bit floating point register set. */
static const KDREGDESC g_aRegsFx64[] =
{
    { DBGFREG_FCW,          DBGFREGVALTYPE_U16,     RT_UOFFSETOF(NTCONTEXT64, FxSave.FCW)           },
    { DBGFREG_FSW,          DBGFREGVALTYPE_U16,     RT_UOFFSETOF(NTCONTEXT64, FxSave.FSW)           },
    { DBGFREG_FTW,          DBGFREGVALTYPE_U16,     RT_UOFFSETOF(NTCONTEXT64, FxSave.FTW)           },
    { DBGFREG_FOP,          DBGFREGVALTYPE_U16,     RT_UOFFSETOF(NTCONTEXT64, FxSave.FOP)           },
    { DBGFREG_FPUIP,        DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT64, FxSave.FPUIP)         },
    /// @todo Fails on Solaris { DBGFREG_FPUCS,        DBGFREGVALTYPE_U16,     RT_UOFFSETOF(NTCONTEXT64, FxSave.CS)            },
    { DBGFREG_FPUDP,        DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT64, FxSave.FPUDP)         },
    /// @todo Fails on Solaris { DBGFREG_FPUDS,        DBGFREGVALTYPE_U16,     RT_UOFFSETOF(NTCONTEXT64, FxSave.DS)            },
    { DBGFREG_MXCSR,        DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT64, FxSave.MXCSR)         },
    { DBGFREG_MXCSR_MASK,   DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT64, FxSave.MXCSR_MASK)    },
    { DBGFREG_ST0,          DBGFREGVALTYPE_R80,     RT_UOFFSETOF(NTCONTEXT64, FxSave.aRegs[0])      },
    { DBGFREG_ST1,          DBGFREGVALTYPE_R80,     RT_UOFFSETOF(NTCONTEXT64, FxSave.aRegs[1])      },
    { DBGFREG_ST2,          DBGFREGVALTYPE_R80,     RT_UOFFSETOF(NTCONTEXT64, FxSave.aRegs[2])      },
    { DBGFREG_ST3,          DBGFREGVALTYPE_R80,     RT_UOFFSETOF(NTCONTEXT64, FxSave.aRegs[3])      },
    { DBGFREG_ST4,          DBGFREGVALTYPE_R80,     RT_UOFFSETOF(NTCONTEXT64, FxSave.aRegs[4])      },
    { DBGFREG_ST5,          DBGFREGVALTYPE_R80,     RT_UOFFSETOF(NTCONTEXT64, FxSave.aRegs[5])      },
    { DBGFREG_ST6,          DBGFREGVALTYPE_R80,     RT_UOFFSETOF(NTCONTEXT64, FxSave.aRegs[6])      },
    { DBGFREG_ST7,          DBGFREGVALTYPE_R80,     RT_UOFFSETOF(NTCONTEXT64, FxSave.aRegs[7])      },
    { DBGFREG_XMM0,         DBGFREGVALTYPE_U128,    RT_UOFFSETOF(NTCONTEXT64, FxSave.aXMM[0])       },
    { DBGFREG_XMM1,         DBGFREGVALTYPE_U128,    RT_UOFFSETOF(NTCONTEXT64, FxSave.aXMM[1])       },
    { DBGFREG_XMM2,         DBGFREGVALTYPE_U128,    RT_UOFFSETOF(NTCONTEXT64, FxSave.aXMM[2])       },
    { DBGFREG_XMM3,         DBGFREGVALTYPE_U128,    RT_UOFFSETOF(NTCONTEXT64, FxSave.aXMM[3])       },
    { DBGFREG_XMM4,         DBGFREGVALTYPE_U128,    RT_UOFFSETOF(NTCONTEXT64, FxSave.aXMM[4])       },
    { DBGFREG_XMM5,         DBGFREGVALTYPE_U128,    RT_UOFFSETOF(NTCONTEXT64, FxSave.aXMM[5])       },
    { DBGFREG_XMM6,         DBGFREGVALTYPE_U128,    RT_UOFFSETOF(NTCONTEXT64, FxSave.aXMM[6])       },
    { DBGFREG_XMM7,         DBGFREGVALTYPE_U128,    RT_UOFFSETOF(NTCONTEXT64, FxSave.aXMM[7])       },
    { DBGFREG_XMM8,         DBGFREGVALTYPE_U128,    RT_UOFFSETOF(NTCONTEXT64, FxSave.aXMM[8])       },
    { DBGFREG_XMM9,         DBGFREGVALTYPE_U128,    RT_UOFFSETOF(NTCONTEXT64, FxSave.aXMM[9])       },
    { DBGFREG_XMM10,        DBGFREGVALTYPE_U128,    RT_UOFFSETOF(NTCONTEXT64, FxSave.aXMM[10])      },
    { DBGFREG_XMM11,        DBGFREGVALTYPE_U128,    RT_UOFFSETOF(NTCONTEXT64, FxSave.aXMM[11])      },
    { DBGFREG_XMM12,        DBGFREGVALTYPE_U128,    RT_UOFFSETOF(NTCONTEXT64, FxSave.aXMM[12])      },
    { DBGFREG_XMM13,        DBGFREGVALTYPE_U128,    RT_UOFFSETOF(NTCONTEXT64, FxSave.aXMM[13])      },
    { DBGFREG_XMM14,        DBGFREGVALTYPE_U128,    RT_UOFFSETOF(NTCONTEXT64, FxSave.aXMM[14])      },
    { DBGFREG_XMM15,        DBGFREGVALTYPE_U128,    RT_UOFFSETOF(NTCONTEXT64, FxSave.aXMM[15])      }
};


/** 32bit control register set. */
static const KDREGDESC g_aRegsCtrl32[] =
{
    { DBGFREG_CS,           DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, u32SegCs)             },
    { DBGFREG_SS,           DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, u32SegSs)             },
    { DBGFREG_EIP,          DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, u32RegEip)            },
    { DBGFREG_ESP,          DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, u32RegEsp)            },
    { DBGFREG_EBP,          DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, u32RegEbp)            },
    { DBGFREG_EFLAGS,       DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, u32RegEflags)         }
};


/** 32bit integer register set. */
static const KDREGDESC g_aRegsInt32[] =
{
    { DBGFREG_EAX,          DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, u32RegEax)            },
    { DBGFREG_ECX,          DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, u32RegEcx)            },
    { DBGFREG_EDX,          DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, u32RegEdx)            },
    { DBGFREG_EBX,          DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, u32RegEbx)            },
    { DBGFREG_ESI,          DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, u32RegEsi)            },
    { DBGFREG_EDI,          DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, u32RegEdi)            }
};


/** 32bit segments register set. */
static const KDREGDESC g_aRegsSegs32[] =
{
    { DBGFREG_DS,           DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, u32SegDs)             },
    { DBGFREG_ES,           DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, u32SegEs)             },
    { DBGFREG_FS,           DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, u32SegFs)             },
    { DBGFREG_GS,           DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, u32SegGs)             }
};


/** 32bit debug register set. */
static const KDREGDESC g_aRegsDbg32[] =
{
    { DBGFREG_DR0,          DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, u32RegDr0)            },
    { DBGFREG_DR1,          DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, u32RegDr1)            },
    { DBGFREG_DR2,          DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, u32RegDr2)            },
    { DBGFREG_DR3,          DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, u32RegDr3)            },
    { DBGFREG_DR6,          DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, u32RegDr6)            },
    { DBGFREG_DR7,          DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, u32RegDr7)            }
};


/** 32bit floating point register set. */
static const KDREGDESC g_aRegsFx32[] =
{
    { DBGFREG_FCW,          DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, FloatSave.u32CtrlWord)  },
    { DBGFREG_FSW,          DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, FloatSave.u32StatusWord)},
    { DBGFREG_FTW,          DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, FloatSave.u32TagWord)   },
    { DBGFREG_FCW,          DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, FloatSave.u32CtrlWord)  },
    { DBGFREG_FPUIP,        DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, FloatSave.u32ErrorOff)  },
    { DBGFREG_FPUCS,        DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, FloatSave.u32ErrorSel)  },
    { DBGFREG_FPUDS,        DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, FloatSave.u32DataOff)   },
    { DBGFREG_FPUDS,        DBGFREGVALTYPE_U32,     RT_UOFFSETOF(NTCONTEXT32, FloatSave.u32DataSel)   },
    { DBGFREG_ST0,          DBGFREGVALTYPE_R80,     RT_UOFFSETOF(NTCONTEXT32, FloatSave.aFpuRegs[0])  },
    { DBGFREG_ST1,          DBGFREGVALTYPE_R80,     RT_UOFFSETOF(NTCONTEXT32, FloatSave.aFpuRegs[1])  },
    { DBGFREG_ST2,          DBGFREGVALTYPE_R80,     RT_UOFFSETOF(NTCONTEXT32, FloatSave.aFpuRegs[2])  },
    { DBGFREG_ST3,          DBGFREGVALTYPE_R80,     RT_UOFFSETOF(NTCONTEXT32, FloatSave.aFpuRegs[3])  },
    { DBGFREG_ST4,          DBGFREGVALTYPE_R80,     RT_UOFFSETOF(NTCONTEXT32, FloatSave.aFpuRegs[4])  },
    { DBGFREG_ST5,          DBGFREGVALTYPE_R80,     RT_UOFFSETOF(NTCONTEXT32, FloatSave.aFpuRegs[5])  },
    { DBGFREG_ST6,          DBGFREGVALTYPE_R80,     RT_UOFFSETOF(NTCONTEXT32, FloatSave.aFpuRegs[6])  },
    { DBGFREG_ST7,          DBGFREGVALTYPE_R80,     RT_UOFFSETOF(NTCONTEXT32, FloatSave.aFpuRegs[7])  }
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void dbgcKdCtxMsgSend(PKDCTX pThis, bool fWarning, const char *pszMsg);


#ifdef LOG_ENABLED
/**
 * Returns a human readable string of the given packet sub type.
 *
 * @returns Pointer to sub type string.
 * @param   u16SubType          The sub type to convert to a string.
 */
static const char *dbgcKdPktDumpSubTypeToStr(uint16_t u16SubType)
{
    switch (u16SubType)
    {
        case KD_PACKET_HDR_SUB_TYPE_STATE_CHANGE32:     return "StateChange32";
        case KD_PACKET_HDR_SUB_TYPE_STATE_MANIPULATE:   return "Manipulate";
        case KD_PACKET_HDR_SUB_TYPE_DEBUG_IO:           return "DebugIo";
        case KD_PACKET_HDR_SUB_TYPE_ACKNOWLEDGE:        return "Ack";
        case KD_PACKET_HDR_SUB_TYPE_RESEND:             return "Resend";
        case KD_PACKET_HDR_SUB_TYPE_RESET:              return "Reset";
        case KD_PACKET_HDR_SUB_TYPE_STATE_CHANGE64:     return "StateChange64";
        case KD_PACKET_HDR_SUB_TYPE_POLL_BREAKIN:       return "PollBreakin";
        case KD_PACKET_HDR_SUB_TYPE_TRACE_IO:           return "TraceIo";
        case KD_PACKET_HDR_SUB_TYPE_CONTROL_REQUEST:    return "ControlRequest";
        case KD_PACKET_HDR_SUB_TYPE_FILE_IO:            return "FileIo";
        default:                                        break;
    }

    return "<UNKNOWN>";
}


/**
 * Returns a human readable string of the given manipulate request ID.
 *
 * @returns Human readable string (read only).
 * @param   idReq               Request ID (API number in KD speak).
 */
static const char *dbgcKdPktDumpManipulateReqToStr(uint32_t idReq)
{
    switch (idReq)
    {
        case KD_PACKET_MANIPULATE_REQ_READ_VIRT_MEM:            return "ReadVirtMem";
        case KD_PACKET_MANIPULATE_REQ_WRITE_VIRT_MEM:           return "WriteVirtMem";
        case KD_PACKET_MANIPULATE_REQ_GET_CONTEXT:              return "GetContext";
        case KD_PACKET_MANIPULATE_REQ_SET_CONTEXT:              return "SetContext";
        case KD_PACKET_MANIPULATE_REQ_WRITE_BKPT:               return "WriteBkpt";
        case KD_PACKET_MANIPULATE_REQ_RESTORE_BKPT:             return "RestoreBkpt";
        case KD_PACKET_MANIPULATE_REQ_CONTINUE:                 return "Continue";
        case KD_PACKET_MANIPULATE_REQ_READ_CTRL_SPACE:          return "ReadCtrlSpace";
        case KD_PACKET_MANIPULATE_REQ_WRITE_CTRL_SPACE:         return "WriteCtrlSpace";
        case KD_PACKET_MANIPULATE_REQ_READ_IO_SPACE:            return "ReadIoSpace";
        case KD_PACKET_MANIPULATE_REQ_WRITE_IO_SPACE:           return "WriteIoSpace";
        case KD_PACKET_MANIPULATE_REQ_REBOOT:                   return "Reboot";
        case KD_PACKET_MANIPULATE_REQ_CONTINUE2:                return "Continue2";
        case KD_PACKET_MANIPULATE_REQ_READ_PHYS_MEM:            return "ReadPhysMem";
        case KD_PACKET_MANIPULATE_REQ_WRITE_PHYS_MEM:           return "WritePhysMem";
        case KD_PACKET_MANIPULATE_REQ_QUERY_SPEC_CALLS:         return "QuerySpecCalls";
        case KD_PACKET_MANIPULATE_REQ_SET_SPEC_CALLS:           return "SetSpecCalls";
        case KD_PACKET_MANIPULATE_REQ_CLEAR_SPEC_CALLS:         return "ClrSpecCalls";
        case KD_PACKET_MANIPULATE_REQ_SET_INTERNAL_BKPT:        return "SetIntBkpt";
        case KD_PACKET_MANIPULATE_REQ_GET_INTERNAL_BKPT:        return "GetIntBkpt";
        case KD_PACKET_MANIPULATE_REQ_READ_IO_SPACE_EX:         return "ReadIoSpaceEx";
        case KD_PACKET_MANIPULATE_REQ_WRITE_IO_SPACE_EX:        return "WriteIoSpaceEx";
        case KD_PACKET_MANIPULATE_REQ_GET_VERSION:              return "GetVersion";
        case KD_PACKET_MANIPULATE_REQ_CLEAR_ALL_INTERNAL_BKPT:  return "ClrAllIntBkpt";
        case KD_PACKET_MANIPULATE_REQ_GET_CONTEXT_EX:           return "GetContextEx";
        case KD_PACKET_MANIPULATE_REQ_QUERY_MEMORY:             return "QueryMemory";
        case KD_PACKET_MANIPULATE_REQ_CAUSE_BUGCHECK:           return "CauseBugCheck";
        case KD_PACKET_MANIPULATE_REQ_SWITCH_PROCESSOR:         return "SwitchProcessor";
        case KD_PACKET_MANIPULATE_REQ_SEARCH_MEMORY:            return "SearchMemory";
        default:                                                break;
    }

    return "<UNKNOWN>";
}


/**
 * Dumps the content of a manipulate packet.
 *
 * @param   pSgBuf              S/G buffer containing the manipulate packet payload.
 */
static void dbgcKdPktDumpManipulate(PRTSGBUF pSgBuf)
{
    KDPACKETMANIPULATEHDR Hdr;
    size_t cbCopied = RTSgBufCopyToBuf(pSgBuf, &Hdr, sizeof(Hdr));

    if (cbCopied == sizeof(Hdr))
    {
        const char *pszReq = dbgcKdPktDumpManipulateReqToStr(Hdr.idReq);

        Log3(("    MANIPULATE(%#x (%s), %#x, %u, %#x)\n",
              Hdr.idReq, pszReq, Hdr.u16CpuLvl, Hdr.idCpu, Hdr.u32NtStatus));

        switch (Hdr.idReq)
        {
            case KD_PACKET_MANIPULATE_REQ_READ_VIRT_MEM:
            case KD_PACKET_MANIPULATE_REQ_WRITE_VIRT_MEM:
            case KD_PACKET_MANIPULATE_REQ_READ_PHYS_MEM:
            case KD_PACKET_MANIPULATE_REQ_WRITE_PHYS_MEM:
            {
                KDPACKETMANIPULATE_XFERMEM64 XferMem64;
                cbCopied = RTSgBufCopyToBuf(pSgBuf, &XferMem64, sizeof(XferMem64));
                if (cbCopied == sizeof(XferMem64))
                {
                    Log3(("        u64PtrTarget: %RX64\n"
                          "        cbXferReq:    %RX32\n"
                          "        cbXfered:     %RX32\n",
                          XferMem64.u64PtrTarget, XferMem64.cbXferReq, XferMem64.cbXfered));
                }
                else
                    Log3(("        Payload to small, expected %u, got %zu\n", sizeof(XferMem64), cbCopied));
                break;
            }
            case KD_PACKET_MANIPULATE_REQ_RESTORE_BKPT:
            {
                KDPACKETMANIPULATE_RESTOREBKPT64 RestoreBkpt64;
                cbCopied = RTSgBufCopyToBuf(pSgBuf, &RestoreBkpt64, sizeof(RestoreBkpt64));
                if (cbCopied == sizeof(RestoreBkpt64))
                    Log3(("        u32HndBkpt:   %RX32\n", RestoreBkpt64.u32HndBkpt));
                else
                    Log3(("        Payload to small, expected %u, got %zu\n", sizeof(RestoreBkpt64), cbCopied));
                break;
            }
            case KD_PACKET_MANIPULATE_REQ_WRITE_BKPT:
            {
                KDPACKETMANIPULATE_WRITEBKPT64 WriteBkpt64;
                cbCopied = RTSgBufCopyToBuf(pSgBuf, &WriteBkpt64, sizeof(WriteBkpt64));
                if (cbCopied == sizeof(WriteBkpt64))
                    Log3(("        u64PtrBkpt:   %RX64\n"
                          "        u32HndBkpt:   %RX32\n",
                          WriteBkpt64.u64PtrBkpt, WriteBkpt64.u32HndBkpt));
                else
                    Log3(("        Payload to small, expected %u, got %zu\n", sizeof(WriteBkpt64), cbCopied));
                break;
            }
            case KD_PACKET_MANIPULATE_REQ_CONTINUE:
            {
                KDPACKETMANIPULATE_CONTINUE Continue;
                cbCopied = RTSgBufCopyToBuf(pSgBuf, &Continue, sizeof(Continue));
                if (cbCopied == sizeof(Continue))
                    Log3(("        u32NtContSts: %RX32\n", Continue.u32NtContSts));
                else
                    Log3(("        Payload to small, expected %u, got %zu\n", sizeof(Continue), cbCopied));
                break;
            }
            case KD_PACKET_MANIPULATE_REQ_CONTINUE2:
            {
                KDPACKETMANIPULATE_CONTINUE2 Continue;
                cbCopied = RTSgBufCopyToBuf(pSgBuf, &Continue, sizeof(Continue));
                if (cbCopied == sizeof(Continue))
                    Log3(("        u32NtContSts: %RX32\n"
                          "        fTrace:       %RX32\n",
                          Continue.u32NtContSts, Continue.fTrace));
                else
                    Log3(("        Payload to small, expected %u, got %zu\n", sizeof(Continue), cbCopied));
                break;
            }
            case KD_PACKET_MANIPULATE_REQ_READ_CTRL_SPACE:
            case KD_PACKET_MANIPULATE_REQ_WRITE_CTRL_SPACE:
            {
                KDPACKETMANIPULATE_XFERCTRLSPACE64 XferCtrlSpace64;
                cbCopied = RTSgBufCopyToBuf(pSgBuf, &XferCtrlSpace64, sizeof(XferCtrlSpace64));
                if (cbCopied == sizeof(XferCtrlSpace64))
                {
                    Log3(("        u64IdXfer:    %RX64\n"
                          "        cbXferReq:    %RX32\n"
                          "        cbXfered:     %RX32\n",
                          XferCtrlSpace64.u64IdXfer, XferCtrlSpace64.cbXferReq, XferCtrlSpace64.cbXfered));
                }
                else
                    Log3(("        Payload to small, expected %u, got %zu\n", sizeof(XferCtrlSpace64), cbCopied));
                break;
            }
            case KD_PACKET_MANIPULATE_REQ_GET_CONTEXT_EX:
            {
                KDPACKETMANIPULATE_CONTEXTEX GetContextEx;
                cbCopied = RTSgBufCopyToBuf(pSgBuf, &GetContextEx, sizeof(GetContextEx));
                if (cbCopied == sizeof(GetContextEx))
                {
                    Log3(("        offStart:     %RX32\n"
                          "        cbXferReq:    %RX32\n"
                          "        cbXfered:     %RX32\n",
                          GetContextEx.offStart, GetContextEx.cbXfer, GetContextEx.cbXfered));
                }
                else
                    Log3(("        Payload to small, expected %u, got %zu\n", sizeof(GetContextEx), cbCopied));
                break;
            }
            case KD_PACKET_MANIPULATE_REQ_QUERY_MEMORY:
            {
                KDPACKETMANIPULATE_QUERYMEMORY QueryMemory;
                cbCopied = RTSgBufCopyToBuf(pSgBuf, &QueryMemory, sizeof(QueryMemory));
                if (cbCopied == sizeof(QueryMemory))
                {
                    Log3(("        u64GCPtr:     %RX64\n"
                          "        u32AddrSpace: %RX32\n"
                          "        u32Flags:     %RX32\n",
                          QueryMemory.u64GCPtr, QueryMemory.u32AddrSpace, QueryMemory.u32Flags));
                }
                else
                    Log3(("        Payload to small, expected %u, got %zu\n", sizeof(QueryMemory), cbCopied));
                break;
            }
            case KD_PACKET_MANIPULATE_REQ_SEARCH_MEMORY:
            {
                KDPACKETMANIPULATE_SEARCHMEMORY SearchMemory;
                cbCopied = RTSgBufCopyToBuf(pSgBuf, &SearchMemory, sizeof(SearchMemory));
                if (cbCopied == sizeof(SearchMemory))
                {
                    Log3(("        u64GCPtr:     %RX64\n"
                          "        cbSearch:     %RX64\n"
                          "        cbPattern:    %RX32\n",
                          SearchMemory.u64GCPtr, SearchMemory.cbSearch, SearchMemory.cbPattern));
                }
                else
                    Log3(("        Payload to small, expected %u, got %zu\n", sizeof(SearchMemory), cbCopied));
                break;
            }
            case KD_PACKET_MANIPULATE_REQ_SWITCH_PROCESSOR:
            default:
                break;
        }
    }
    else
        Log3(("    MANIPULATE(Header too small, expected %u, got %zu)\n", sizeof(Hdr), cbCopied));
}


/**
 * Dumps the received packet to the debug log.
 *
 * @returns VBox status code.
 * @param   pPktHdr             The packet header to dump.
 * @param   fRx                 Flag whether the packet was received (false indicates an outgoing packet).
 */
static void dbgcKdPktDump(PCKDPACKETHDR pPktHdr, PCRTSGSEG paSegs, uint32_t cSegs, bool fRx)
{
    RTSGBUF SgBuf;

    RTSgBufInit(&SgBuf, paSegs, cSegs);

    Log3(("%s KDPKTHDR(%#x, %#x (%s), %u, %#x, %#x)\n",
          fRx ? "=>" : "<=",
          pPktHdr->u32Signature, pPktHdr->u16SubType, dbgcKdPktDumpSubTypeToStr(pPktHdr->u16SubType),
          pPktHdr->cbBody, pPktHdr->idPacket, pPktHdr->u32ChkSum));
    switch (pPktHdr->u16SubType)
    {
        case KD_PACKET_HDR_SUB_TYPE_STATE_MANIPULATE:
            dbgcKdPktDumpManipulate(&SgBuf);
            break;
        default:
            break;
    }
}
#endif


/**
 * Resets the emulated hardware breakpoint state to a state similar after a reboot.
 *
 * @param   pThis               The KD context.
 */
static void dbgcKdCtxHwBpReset(PKDCTX pThis)
{
    pThis->fSingleStepped = false;

    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aHwBp); i++)
    {
        PKDCTXHWBP pBp = &pThis->aHwBp[i];

        if (pBp->hDbgfBp != NIL_DBGFBP)
        {
            int rc = DBGFR3BpClear(pThis->Dbgc.pUVM, pBp->hDbgfBp);
            AssertRC(rc);
        }

        pBp->hDbgfBp    = NIL_DBGFBP;
        pBp->GCPtrBp    = 0;
        pBp->fAcc       = 0;
        pBp->fLen       = 0;
        pBp->fLocal     = false;
        pBp->fGlobal    = false;
        pBp->fTriggered = false;
    }
}


/**
 * Updates the given breakpoint with the given properties.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   pBp                 The breakpoint to update.
 * @param   fAcc                Access mode.
 * @param   fLen                Access length.
 * @param   fGlobal             Global breakpoint.
 * @param   fLocal              Local breakpoint.
 * @param   GCPtrBp             Linear address of the breakpoint.
 */
static int dbgcKdCtxHwBpUpdate(PKDCTX pThis, PKDCTXHWBP pBp, uint8_t fAcc, uint8_t fLen,
                               bool fGlobal, bool fLocal, RTGCPTR GCPtrBp)
{
    int rc = VINF_SUCCESS;

    /* Did anything actually change?. */
    if (   pBp->fAcc != fAcc
        || pBp->fLen != fLen
        || pBp->fGlobal != fGlobal
        || pBp->fLocal != fLocal
        || pBp->GCPtrBp != GCPtrBp)
    {
        /* Clear the old breakpoint. */
        if (pBp->hDbgfBp != NIL_DBGFBP)
        {
            rc = DBGFR3BpClear(pThis->Dbgc.pUVM, pBp->hDbgfBp);
            AssertRC(rc);
            pBp->hDbgfBp = NIL_DBGFBP;
        }

        pBp->fAcc    = fAcc;
        pBp->fLen    = fLen;
        pBp->fGlobal = fGlobal;
        pBp->fLocal  = fLocal;
        pBp->GCPtrBp = GCPtrBp;
        if (pBp->fGlobal || pBp->fLocal)
        {
            DBGFADDRESS AddrBp;
            DBGFR3AddrFromFlat(pThis->Dbgc.pUVM, &AddrBp, GCPtrBp);

            uint8_t cb = 0;
            switch (pBp->fLen)
            {
                case X86_DR7_LEN_BYTE:
                    cb = 1;
                    break;
                case X86_DR7_LEN_WORD:
                    cb = 2;
                    break;
                case X86_DR7_LEN_DWORD:
                    cb = 4;
                    break;
                case X86_DR7_LEN_QWORD:
                    cb = 8;
                    break;
                default:
                    AssertFailed();
                    return VERR_NET_PROTOCOL_ERROR;
            }

            rc = DBGFR3BpSetReg(pThis->Dbgc.pUVM, &AddrBp, 0 /*iHitTrigger*/, UINT64_MAX /*iHitDisable*/,
                                pBp->fAcc, cb, &pBp->hDbgfBp);
        }
    }

    return rc;
}


/**
 * Updates emulated hardware breakpoints based on the written DR7 value.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   uDr7                The DR7 value which is written.
 */
static int dbgcKdCtxHwBpDr7Update(PKDCTX pThis, uint32_t uDr7)
{
    int rc = VINF_SUCCESS;

    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aHwBp); i++)
    {
        PKDCTXHWBP pBp = &pThis->aHwBp[i];
        uint8_t fAcc = X86_DR7_GET_RW(uDr7, i);
        uint8_t fLen = X86_DR7_GET_LEN(uDr7, i);
        bool fGlobal = (uDr7 & RT_BIT_32(1 + i * 2)) ? true : false;
        bool fLocal = (uDr7 & RT_BIT_32(i * 2)) ? true : false;

        int rc2 = dbgcKdCtxHwBpUpdate(pThis, pBp, fAcc, fLen, fGlobal, fLocal, pThis->aHwBp[i].GCPtrBp);
        if (   RT_FAILURE(rc2)
            && RT_SUCCESS(rc))
            rc = rc2;
    }

    return rc;
}


/**
 * Updates the linear guest pointer for the given hardware breakpoint.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   pBp                 The breakpoint to update.
 * @param   GCPtrBp             The linear breakpoint address.
 */
DECLINLINE(int) dbgcKdCtxHwBpGCPtrUpdate(PKDCTX pThis, PKDCTXHWBP pBp, RTGCPTR GCPtrBp)
{
    return dbgcKdCtxHwBpUpdate(pThis, pBp, pBp->fAcc, pBp->fLen, pBp->fGlobal, pBp->fLocal, GCPtrBp);
}


/**
 * Calculates the DR7 value based on the emulated hardware breakpoint state and returns it.
 *
 * @returns The emulated DR7 value.
 * @param   pThis               The KD context.
 */
static uint32_t dbgcKdCtxHwBpDr7Get(PKDCTX pThis)
{
    uint32_t uDr7 = 0;

    uDr7 |= X86_DR7_RW(0, pThis->aHwBp[0].fAcc);
    uDr7 |= X86_DR7_RW(1, pThis->aHwBp[1].fAcc);
    uDr7 |= X86_DR7_RW(2, pThis->aHwBp[2].fAcc);
    uDr7 |= X86_DR7_RW(3, pThis->aHwBp[3].fAcc);

    uDr7 |= X86_DR7_LEN(0, pThis->aHwBp[0].fLen);
    uDr7 |= X86_DR7_LEN(1, pThis->aHwBp[1].fLen);
    uDr7 |= X86_DR7_LEN(2, pThis->aHwBp[2].fLen);
    uDr7 |= X86_DR7_LEN(3, pThis->aHwBp[3].fLen);

    uDr7 |= pThis->aHwBp[0].fGlobal ? X86_DR7_G(0) : 0;
    uDr7 |= pThis->aHwBp[1].fGlobal ? X86_DR7_G(1) : 0;
    uDr7 |= pThis->aHwBp[2].fGlobal ? X86_DR7_G(2) : 0;
    uDr7 |= pThis->aHwBp[3].fGlobal ? X86_DR7_G(3) : 0;

    uDr7 |= pThis->aHwBp[0].fLocal ? X86_DR7_L(0) : 0;
    uDr7 |= pThis->aHwBp[1].fLocal ? X86_DR7_L(1) : 0;
    uDr7 |= pThis->aHwBp[2].fLocal ? X86_DR7_L(2) : 0;
    uDr7 |= pThis->aHwBp[3].fLocal ? X86_DR7_L(3) : 0;

    return uDr7;
}


/**
 * Updates emulated hardware breakpoints based on the written DR6 value.
 *
 * @param   pThis               The KD context.
 * @param   uDr6                The DR7 value which is written.
 */
static void dbgcKdCtxHwBpDr6Update(PKDCTX pThis, uint32_t uDr6)
{
    pThis->aHwBp[0].fTriggered = (uDr6 & X86_DR6_B0) ? true : false;
    pThis->aHwBp[1].fTriggered = (uDr6 & X86_DR6_B1) ? true : false;
    pThis->aHwBp[2].fTriggered = (uDr6 & X86_DR6_B2) ? true : false;
    pThis->aHwBp[3].fTriggered = (uDr6 & X86_DR6_B3) ? true : false;
    pThis->fSingleStepped = (uDr6 & X86_DR6_BS) ? true : false;
}


/**
 * Calculates the DR6 value based on the emulated hardware breakpoint state and returns it.
 *
 * @returns The emulated DR6 value.
 * @param   pThis               The KD context.
 */
static uint32_t dbgcKdCtxHwBpDr6Get(PKDCTX pThis)
{
    uint32_t uDr6 = 0;

    if (pThis->aHwBp[0].fTriggered)
        uDr6 |= X86_DR6_B0;
    if (pThis->aHwBp[1].fTriggered)
        uDr6 |= X86_DR6_B1;
    if (pThis->aHwBp[2].fTriggered)
        uDr6 |= X86_DR6_B2;
    if (pThis->aHwBp[3].fTriggered)
        uDr6 |= X86_DR6_B3;
    if (pThis->fSingleStepped)
        uDr6 |= X86_DR6_BS;

    return uDr6;
}


/**
 * Wrapper for the I/O interface write callback.
 *
 * @returns Status code.
 * @param   pThis               The KD context.
 * @param   pvPkt               The packet data to send.
 * @param   cbPkt               Size of the packet in bytes.
 */
DECLINLINE(int) dbgcKdCtxWrite(PKDCTX pThis, const void *pvPkt, size_t cbPkt)
{
    return pThis->Dbgc.pIo->pfnWrite(pThis->Dbgc.pIo, pvPkt, cbPkt, NULL /*pcbWritten*/);
}


/**
 * Queries a given register set and stores it into the given context buffer.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   idCpu               The CPU to query the context for.
 * @param   paRegs              The register set to query.
 * @param   cRegs               Number of entries in the register set.
 * @param   pvCtx               The context buffer to store the data into.
 */
static int dbgcKdCtxQueryRegs(PKDCTX pThis, VMCPUID idCpu, PCKDREGDESC paRegs, uint32_t cRegs, void *pvCtx)
{
    int rc = VINF_SUCCESS;

    for (uint32_t i = 0; i < cRegs && rc == VINF_SUCCESS; i++)
    {
        void *pvStart = (uint8_t *)pvCtx + paRegs[i].offReg;

        switch (paRegs[i].enmValType)
        {
            case DBGFREGVALTYPE_U16:  rc = DBGFR3RegCpuQueryU16(pThis->Dbgc.pUVM, idCpu, paRegs[i].enmReg, (uint16_t *)pvStart);    break;
            case DBGFREGVALTYPE_U32:  rc = DBGFR3RegCpuQueryU32(pThis->Dbgc.pUVM, idCpu, paRegs[i].enmReg, (uint32_t *)pvStart);    break;
            case DBGFREGVALTYPE_U64:  rc = DBGFR3RegCpuQueryU64(pThis->Dbgc.pUVM, idCpu, paRegs[i].enmReg, (uint64_t *)pvStart);    break;
            //case DBGFREGVALTYPE_R80:  rc = DBGFR3RegCpuQueryR80(pThis->Dbgc.pUVM, idCpu, paRegs[i].enmReg, (RTFLOAT80U *)pvStart); break;
            //case DBGFREGVALTYPE_U128: rc = DBGFR3RegCpuQueryU128(pThis->Dbgc.pUVM, idCpu, paRegs[i].enmReg, (PRTUINT128U)pvStart);  break;
            default: AssertMsgFailedBreakStmt(("Register type %u not implemented\n", paRegs[i].enmValType), rc = VERR_NOT_IMPLEMENTED);
        }

        if (   rc == VINF_DBGF_ZERO_EXTENDED_REGISTER
            || (   rc == VINF_DBGF_TRUNCATED_REGISTER
                && paRegs[i].enmReg == DBGFREG_RFLAGS)) /* KD protocol specifies 32bit but is really 64bit. */
            rc = VINF_SUCCESS;
    }

    if (   RT_SUCCESS(rc)
        && rc != VINF_SUCCESS)
        rc = VERR_DBGF_UNSUPPORTED_CAST;

    return rc;
}


/**
 * Fills in the given 64bit NT context structure with the requested values.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   idCpu               The CPU to query the context for.
 * @param   pNtCtx              The NT context structure to fill in.
 * @param   fCtxFlags           Combination of NTCONTEXT_F_XXX determining what to fill in.
 */
static int dbgcKdCtxQueryNtCtx64(PKDCTX pThis, VMCPUID idCpu, PNTCONTEXT64 pNtCtx, uint32_t fCtxFlags)
{
    RT_BZERO(pNtCtx, sizeof(*pNtCtx));

    pNtCtx->fContext = NTCONTEXT_F_AMD64;
    int rc = DBGFR3RegCpuQueryU32(pThis->Dbgc.pUVM, idCpu, DBGFREG_MXCSR, &pNtCtx->u32RegMxCsr);

    if (   RT_SUCCESS(rc)
        && fCtxFlags & NTCONTEXT_F_CONTROL)
    {
        rc = dbgcKdCtxQueryRegs(pThis, idCpu, &g_aRegsCtrl64[0], RT_ELEMENTS(g_aRegsCtrl64), pNtCtx);
        if (RT_SUCCESS(rc))
            pNtCtx->fContext |= NTCONTEXT_F_CONTROL;
    }

    if (   RT_SUCCESS(rc)
        && fCtxFlags & NTCONTEXT_F_INTEGER)
    {
        rc = dbgcKdCtxQueryRegs(pThis, idCpu, &g_aRegsInt64[0], RT_ELEMENTS(g_aRegsInt64), pNtCtx);
        if (RT_SUCCESS(rc))
            pNtCtx->fContext |= NTCONTEXT_F_INTEGER;
    }

    if (   RT_SUCCESS(rc)
        && fCtxFlags & NTCONTEXT_F_SEGMENTS)
    {
        rc = dbgcKdCtxQueryRegs(pThis, idCpu, &g_aRegsSegs64[0], RT_ELEMENTS(g_aRegsSegs64), pNtCtx);
        if (RT_SUCCESS(rc))
            pNtCtx->fContext |= NTCONTEXT_F_SEGMENTS;
    }

    if (   RT_SUCCESS(rc)
        && fCtxFlags & NTCONTEXT_F_FLOATING_POINT)
    {
        rc = dbgcKdCtxQueryRegs(pThis, idCpu, &g_aRegsFx64[0], RT_ELEMENTS(g_aRegsFx64), pNtCtx);
        if (RT_SUCCESS(rc))
            pNtCtx->fContext |= NTCONTEXT_F_FLOATING_POINT;
    }

    if (   RT_SUCCESS(rc)
        && fCtxFlags & NTCONTEXT_F_DEBUG)
    {
        /** @todo NTCONTEXT_F_DEBUG */
    }

    return rc;
}


/**
 * Fills in the given 32bit NT context structure with the requested values.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   idCpu               The CPU to query the context for.
 * @param   pNtCtx              The NT context structure to fill in.
 * @param   fCtxFlags           Combination of NTCONTEXT_F_XXX determining what to fill in.
 */
static int dbgcKdCtxQueryNtCtx32(PKDCTX pThis, VMCPUID idCpu, PNTCONTEXT32 pNtCtx, uint32_t fCtxFlags)
{
    RT_BZERO(pNtCtx, sizeof(*pNtCtx));

    pNtCtx->fContext = NTCONTEXT_F_X86;

    int rc = VINF_SUCCESS;
    if (fCtxFlags & NTCONTEXT_F_CONTROL)
    {
        rc = dbgcKdCtxQueryRegs(pThis, idCpu, &g_aRegsCtrl32[0], RT_ELEMENTS(g_aRegsCtrl32), pNtCtx);
        if (RT_SUCCESS(rc))
            pNtCtx->fContext |= NTCONTEXT_F_CONTROL;
    }

    if (   RT_SUCCESS(rc)
        && fCtxFlags & NTCONTEXT_F_INTEGER)
    {
        rc = dbgcKdCtxQueryRegs(pThis, idCpu, &g_aRegsInt32[0], RT_ELEMENTS(g_aRegsInt32), pNtCtx);
        if (RT_SUCCESS(rc))
            pNtCtx->fContext |= NTCONTEXT_F_INTEGER;
    }

    if (   RT_SUCCESS(rc)
        && fCtxFlags & NTCONTEXT_F_SEGMENTS)
    {
        rc = dbgcKdCtxQueryRegs(pThis, idCpu, &g_aRegsSegs32[0], RT_ELEMENTS(g_aRegsSegs32), pNtCtx);
        if (RT_SUCCESS(rc))
            pNtCtx->fContext |= NTCONTEXT_F_SEGMENTS;
    }

    if (   RT_SUCCESS(rc)
        && fCtxFlags & NTCONTEXT_F_FLOATING_POINT)
    {
        rc = dbgcKdCtxQueryRegs(pThis, idCpu, &g_aRegsFx32[0], RT_ELEMENTS(g_aRegsFx32), pNtCtx);
        if (RT_SUCCESS(rc))
            pNtCtx->fContext |= NTCONTEXT_F_FLOATING_POINT;
    }

    if (   RT_SUCCESS(rc)
        && fCtxFlags & NTCONTEXT_F_DEBUG)
    {
        rc = dbgcKdCtxQueryRegs(pThis, idCpu, &g_aRegsDbg32[0], RT_ELEMENTS(g_aRegsDbg32), pNtCtx);
        if (RT_SUCCESS(rc))
            pNtCtx->fContext |= NTCONTEXT_F_DEBUG;
    }

    return rc;
}


#define KD_REG_INIT(a_pszName, a_enmType, a_ValMember, a_Val) \
    do \
    { \
        aRegsSet[idxReg].pszName = a_pszName; \
        aRegsSet[idxReg].enmType = a_enmType; \
        aRegsSet[idxReg].Val.a_ValMember = a_Val; \
        idxReg++; \
    } while (0)
#define KD_REG_INIT_DTR(a_pszName, a_Base, a_Limit) \
    do \
    { \
        aRegsSet[idxReg].pszName = a_pszName; \
        aRegsSet[idxReg].enmType = DBGFREGVALTYPE_DTR; \
        aRegsSet[idxReg].Val.dtr.u64Base = a_Base; \
        aRegsSet[idxReg].Val.dtr.u32Limit = a_Limit; \
        idxReg++; \
    } while (0)
#define KD_REG_INIT_U16(a_pszName, a_Val) KD_REG_INIT(a_pszName, DBGFREGVALTYPE_U16, u16, a_Val)
#define KD_REG_INIT_U32(a_pszName, a_Val) KD_REG_INIT(a_pszName, DBGFREGVALTYPE_U32, u32, a_Val)
#define KD_REG_INIT_U64(a_pszName, a_Val) KD_REG_INIT(a_pszName, DBGFREGVALTYPE_U64, u64, a_Val)


/**
 * Writes the indicated values from the given context structure to the guests register set.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   idCpu               The CPU to query the context for.
 * @param   pNtCtx              The NT context structure to set.
 * @param   fCtxFlags           Combination of NTCONTEXT_F_XXX determining what to set.
 */
static int dbgcKdCtxSetNtCtx64(PKDCTX pThis, VMCPUID idCpu, PCNTCONTEXT64 pNtCtx, uint32_t fCtxFlags)
{
    uint32_t idxReg = 0;
    DBGFREGENTRYNM aRegsSet[64]; /** @todo Verify that this is enough when fully implemented. */

    KD_REG_INIT_U32("mxcsr", pNtCtx->u32RegMxCsr);

    if (fCtxFlags & NTCONTEXT_F_CONTROL)
    {
#if 0 /** @todo CPUM returns VERR_NOT_IMPLEMENTED */
        KD_REG_INIT_U16("cs", pNtCtx->u16SegCs);
        KD_REG_INIT_U16("ss", pNtCtx->u16SegSs);
#endif
        KD_REG_INIT_U64("rip", pNtCtx->u64RegRip);
        KD_REG_INIT_U64("rsp", pNtCtx->u64RegRsp);
        KD_REG_INIT_U64("rbp", pNtCtx->u64RegRbp);
        KD_REG_INIT_U32("rflags", pNtCtx->u32RegEflags);
    }

    if (fCtxFlags & NTCONTEXT_F_INTEGER)
    {
        KD_REG_INIT_U64("rax", pNtCtx->u64RegRax);
        KD_REG_INIT_U64("rcx", pNtCtx->u64RegRcx);
        KD_REG_INIT_U64("rdx", pNtCtx->u64RegRdx);
        KD_REG_INIT_U64("rbx", pNtCtx->u64RegRbx);
        KD_REG_INIT_U64("rsi", pNtCtx->u64RegRsi);
        KD_REG_INIT_U64("rdi", pNtCtx->u64RegRdi);
        KD_REG_INIT_U64("r8", pNtCtx->u64RegR8);
        KD_REG_INIT_U64("r9", pNtCtx->u64RegR9);
        KD_REG_INIT_U64("r10", pNtCtx->u64RegR10);
        KD_REG_INIT_U64("r11", pNtCtx->u64RegR11);
        KD_REG_INIT_U64("r12", pNtCtx->u64RegR12);
        KD_REG_INIT_U64("r13", pNtCtx->u64RegR13);
        KD_REG_INIT_U64("r14", pNtCtx->u64RegR14);
        KD_REG_INIT_U64("r15", pNtCtx->u64RegR15);
    }

    if (fCtxFlags & NTCONTEXT_F_SEGMENTS)
    {
#if 0 /** @todo CPUM returns VERR_NOT_IMPLEMENTED */
        KD_REG_INIT_U16("ds", pNtCtx->u16SegDs);
        KD_REG_INIT_U16("es", pNtCtx->u16SegEs);
        KD_REG_INIT_U16("fs", pNtCtx->u16SegFs);
        KD_REG_INIT_U16("gs", pNtCtx->u16SegGs);
#endif
    }

    if (fCtxFlags & NTCONTEXT_F_FLOATING_POINT)
    {
        /** @todo NTCONTEXT_F_FLOATING_POINT. */
    }

    if (fCtxFlags & NTCONTEXT_F_DEBUG)
        dbgcKdCtxMsgSend(pThis, true /*fWarning*/, "Setting local DR registers does not work!");

    return DBGFR3RegNmSetBatch(pThis->Dbgc.pUVM, idCpu, &aRegsSet[0], idxReg);
}


/**
 * Fills in the given 64bit NT kernel context structure with the requested values.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   idCpu               The CPU to query the context for.
 * @param   pKNtCtx             The NT context structure to fill in.
 * @param   fCtxFlags           Combination of NTCONTEXT_F_XXX determining what to fill in.
 */
static int dbgcKdCtxQueryNtKCtx64(PKDCTX pThis, VMCPUID idCpu, PNTKCONTEXT64 pKNtCtx, uint32_t fCtxFlags)
{
    RT_BZERO(pKNtCtx, sizeof(*pKNtCtx));

    int rc = DBGFR3RegCpuQueryU64(pThis->Dbgc.pUVM, idCpu, DBGFREG_CR0, &pKNtCtx->u64RegCr0);
    if (RT_SUCCESS(rc))
        rc = DBGFR3RegCpuQueryU64(pThis->Dbgc.pUVM, idCpu, DBGFREG_CR2, &pKNtCtx->u64RegCr2);
    if (RT_SUCCESS(rc))
        rc = DBGFR3RegCpuQueryU64(pThis->Dbgc.pUVM, idCpu, DBGFREG_CR3, &pKNtCtx->u64RegCr3);
    if (RT_SUCCESS(rc))
        rc = DBGFR3RegCpuQueryU64(pThis->Dbgc.pUVM, idCpu, DBGFREG_CR4, &pKNtCtx->u64RegCr4);
    if (RT_SUCCESS(rc))
        rc = DBGFR3RegCpuQueryU64(pThis->Dbgc.pUVM, idCpu, DBGFREG_CR8, &pKNtCtx->u64RegCr8);
    if (RT_SUCCESS(rc))
        rc = DBGFR3RegCpuQueryU16(pThis->Dbgc.pUVM, idCpu, DBGFREG_GDTR_LIMIT, &pKNtCtx->Gdtr.u16Limit);
    if (RT_SUCCESS(rc))
        rc = DBGFR3RegCpuQueryU64(pThis->Dbgc.pUVM, idCpu, DBGFREG_GDTR_BASE, &pKNtCtx->Gdtr.u64PtrBase);
    if (RT_SUCCESS(rc))
        rc = DBGFR3RegCpuQueryU16(pThis->Dbgc.pUVM, idCpu, DBGFREG_IDTR_LIMIT, &pKNtCtx->Idtr.u16Limit);
    if (RT_SUCCESS(rc))
        rc = DBGFR3RegCpuQueryU64(pThis->Dbgc.pUVM, idCpu, DBGFREG_IDTR_BASE, &pKNtCtx->Idtr.u64PtrBase);
    if (RT_SUCCESS(rc))
        rc = DBGFR3RegCpuQueryU16(pThis->Dbgc.pUVM, idCpu, DBGFREG_TR, &pKNtCtx->u16RegTr);
    if (RT_SUCCESS(rc))
        rc = DBGFR3RegCpuQueryU16(pThis->Dbgc.pUVM, idCpu, DBGFREG_LDTR, &pKNtCtx->u16RegLdtr);
    if (RT_SUCCESS(rc))
        rc = DBGFR3RegCpuQueryU32(pThis->Dbgc.pUVM, idCpu, DBGFREG_MXCSR, &pKNtCtx->u32RegMxCsr);

    if (RT_SUCCESS(rc))
        rc = DBGFR3RegCpuQueryU64(pThis->Dbgc.pUVM, idCpu, DBGFREG_MSR_K8_GS_BASE, &pKNtCtx->u64MsrGsBase);
    if (RT_SUCCESS(rc))
        rc = DBGFR3RegCpuQueryU64(pThis->Dbgc.pUVM, idCpu, DBGFREG_MSR_K8_KERNEL_GS_BASE, &pKNtCtx->u64MsrKernelGsBase);
    if (RT_SUCCESS(rc))
        rc = DBGFR3RegCpuQueryU64(pThis->Dbgc.pUVM, idCpu, DBGFREG_MSR_K6_STAR, &pKNtCtx->u64MsrStar);
    if (RT_SUCCESS(rc))
        rc = DBGFR3RegCpuQueryU64(pThis->Dbgc.pUVM, idCpu, DBGFREG_MSR_K8_LSTAR, &pKNtCtx->u64MsrLstar);
    if (RT_SUCCESS(rc))
        rc = DBGFR3RegCpuQueryU64(pThis->Dbgc.pUVM, idCpu, DBGFREG_MSR_K8_CSTAR, &pKNtCtx->u64MsrCstar);
    if (RT_SUCCESS(rc))
        rc = DBGFR3RegCpuQueryU64(pThis->Dbgc.pUVM, idCpu, DBGFREG_MSR_K8_SF_MASK, &pKNtCtx->u64MsrSfMask);
    /** @todo XCR0 */

    /* Get the emulated DR register state. */
    pKNtCtx->u64RegDr0 = pThis->aHwBp[0].GCPtrBp;
    pKNtCtx->u64RegDr1 = pThis->aHwBp[1].GCPtrBp;
    pKNtCtx->u64RegDr2 = pThis->aHwBp[2].GCPtrBp;
    pKNtCtx->u64RegDr3 = pThis->aHwBp[3].GCPtrBp;
    pKNtCtx->u64RegDr6 = dbgcKdCtxHwBpDr6Get(pThis);
    pKNtCtx->u64RegDr7 = dbgcKdCtxHwBpDr7Get(pThis);

    if (RT_SUCCESS(rc))
        rc = dbgcKdCtxQueryNtCtx64(pThis, idCpu, &pKNtCtx->Ctx, fCtxFlags);

    return rc;
}


/**
 * Fills in the given 32bit NT kernel context structure with the requested values.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   idCpu               The CPU to query the context for.
 * @param   pKNtCtx             The NT context structure to fill in.
 */
static int dbgcKdCtxQueryNtKCtx32(PKDCTX pThis, VMCPUID idCpu, PNTKCONTEXT32 pKNtCtx)
{
    RT_BZERO(pKNtCtx, sizeof(*pKNtCtx));

    int rc = DBGFR3RegCpuQueryU32(pThis->Dbgc.pUVM, idCpu, DBGFREG_CR0, &pKNtCtx->u32RegCr0);
    if (RT_SUCCESS(rc))
        rc = DBGFR3RegCpuQueryU32(pThis->Dbgc.pUVM, idCpu, DBGFREG_CR2, &pKNtCtx->u32RegCr2);
    if (RT_SUCCESS(rc))
        rc = DBGFR3RegCpuQueryU32(pThis->Dbgc.pUVM, idCpu, DBGFREG_CR3, &pKNtCtx->u32RegCr3);
    if (RT_SUCCESS(rc))
        rc = DBGFR3RegCpuQueryU32(pThis->Dbgc.pUVM, idCpu, DBGFREG_CR4, &pKNtCtx->u32RegCr4);

    if (RT_SUCCESS(rc))
        rc = DBGFR3RegCpuQueryU16(pThis->Dbgc.pUVM, idCpu, DBGFREG_GDTR_LIMIT, &pKNtCtx->Gdtr.u16Limit);
    if (RT_SUCCESS(rc))
        rc = DBGFR3RegCpuQueryU32(pThis->Dbgc.pUVM, idCpu, DBGFREG_GDTR_BASE, &pKNtCtx->Gdtr.u32PtrBase);
    if (RT_SUCCESS(rc))
        rc = DBGFR3RegCpuQueryU16(pThis->Dbgc.pUVM, idCpu, DBGFREG_IDTR_LIMIT, &pKNtCtx->Idtr.u16Limit);
    if (RT_SUCCESS(rc))
        rc = DBGFR3RegCpuQueryU32(pThis->Dbgc.pUVM, idCpu, DBGFREG_IDTR_BASE, &pKNtCtx->Idtr.u32PtrBase);
    if (RT_SUCCESS(rc))
        rc = DBGFR3RegCpuQueryU16(pThis->Dbgc.pUVM, idCpu, DBGFREG_TR, &pKNtCtx->u16RegTr);
    if (RT_SUCCESS(rc))
        rc = DBGFR3RegCpuQueryU16(pThis->Dbgc.pUVM, idCpu, DBGFREG_LDTR, &pKNtCtx->u16RegLdtr);

    /* Get the emulated DR register state. */
    pKNtCtx->u32RegDr0 = (uint32_t)pThis->aHwBp[0].GCPtrBp;
    pKNtCtx->u32RegDr1 = (uint32_t)pThis->aHwBp[1].GCPtrBp;
    pKNtCtx->u32RegDr2 = (uint32_t)pThis->aHwBp[2].GCPtrBp;
    pKNtCtx->u32RegDr3 = (uint32_t)pThis->aHwBp[3].GCPtrBp;
    pKNtCtx->u32RegDr6 = dbgcKdCtxHwBpDr6Get(pThis);
    pKNtCtx->u32RegDr7 = dbgcKdCtxHwBpDr7Get(pThis);

    return rc;
}


/**
 * Fills in the given 64bit NT kernel context structure with the requested values.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   idCpu               The CPU to query the context for.
 * @param   pKNtCtx             The NT context structure to fill in.
 * @param   cbSet               How many bytes of the context are valid.
 */
static int dbgcKdCtxSetNtKCtx64(PKDCTX pThis, VMCPUID idCpu, PCNTKCONTEXT64 pKNtCtx, size_t cbSet)
{
    AssertReturn(cbSet >= RT_UOFFSETOF(NTKCONTEXT64, Ctx), VERR_INVALID_PARAMETER);

    uint32_t idxReg = 0;
    DBGFREGENTRYNM aRegsSet[64]; /** @todo Verify that this is enough when fully implemented. */

    KD_REG_INIT_U64("cr0", pKNtCtx->u64RegCr0);
    KD_REG_INIT_U64("cr2", pKNtCtx->u64RegCr2);
    KD_REG_INIT_U64("cr3", pKNtCtx->u64RegCr3);
    KD_REG_INIT_U64("cr4", pKNtCtx->u64RegCr4);
    KD_REG_INIT_U64("cr8", pKNtCtx->u64RegCr8);

    KD_REG_INIT_DTR("gdtr", pKNtCtx->Gdtr.u64PtrBase, pKNtCtx->Gdtr.u16Limit);
    KD_REG_INIT_DTR("idtr", pKNtCtx->Idtr.u64PtrBase, pKNtCtx->Idtr.u16Limit);

#if 0 /** @todo CPUM returns VERR_NOT_IMPLEMENTED */
    KD_REG_INIT_U16("tr", pKNtCtx->u16RegTr);
    KD_REG_INIT_U16("ldtr", pKNtCtx->u16RegLdtr);
#endif
    KD_REG_INIT_U32("mxcsr", pKNtCtx->u32RegMxCsr);

    KD_REG_INIT_U64("msr_gs_base", pKNtCtx->u64MsrGsBase);
    KD_REG_INIT_U64("krnl_gs_base", pKNtCtx->u64MsrKernelGsBase);
    KD_REG_INIT_U64("star", pKNtCtx->u64MsrStar);
    KD_REG_INIT_U64("lstar", pKNtCtx->u64MsrLstar);
    KD_REG_INIT_U64("cstar", pKNtCtx->u64MsrCstar);
    KD_REG_INIT_U64("sf_mask", pKNtCtx->u64MsrSfMask);

    int rc = DBGFR3RegNmSetBatch(pThis->Dbgc.pUVM, idCpu, &aRegsSet[0], idxReg);
    if (   RT_SUCCESS(rc)
        && cbSet > RT_UOFFSETOF(NTKCONTEXT64, Ctx)) /** @todo Probably wrong. */
        rc = dbgcKdCtxSetNtCtx64(pThis, idCpu, &pKNtCtx->Ctx, pKNtCtx->Ctx.fContext);

    if (RT_SUCCESS(rc))
    {
        /* Update emulated hardware breakpoint state. */
        dbgcKdCtxHwBpDr6Update(pThis, (uint32_t)pKNtCtx->u64RegDr6);
        rc = dbgcKdCtxHwBpDr7Update(pThis, (uint32_t)pKNtCtx->u64RegDr7);
        if (RT_SUCCESS(rc))
            rc = dbgcKdCtxHwBpGCPtrUpdate(pThis, &pThis->aHwBp[0], pKNtCtx->u64RegDr0);
        if (RT_SUCCESS(rc))
            rc = dbgcKdCtxHwBpGCPtrUpdate(pThis, &pThis->aHwBp[1], pKNtCtx->u64RegDr1);
        if (RT_SUCCESS(rc))
            rc = dbgcKdCtxHwBpGCPtrUpdate(pThis, &pThis->aHwBp[2], pKNtCtx->u64RegDr2);
        if (RT_SUCCESS(rc))
            rc = dbgcKdCtxHwBpGCPtrUpdate(pThis, &pThis->aHwBp[3], pKNtCtx->u64RegDr3);
    }

    return rc;
}

#undef KD_REG_INIT_64
#undef KD_REG_INIT_32
#undef KD_REG_INIT_16
#undef KD_REG_INIT_DTR
#undef KD_REG_INIT


/**
 * Validates the given KD packet header.
 *
 * @returns Flag whether the packet header is valid, false if invalid.
 * @param   pPktHdr             The packet header to validate.
 */
static bool dbgcKdPktHdrValidate(PCKDPACKETHDR pPktHdr)
{
    if (   pPktHdr->u32Signature != KD_PACKET_HDR_SIGNATURE_DATA
        && pPktHdr->u32Signature != KD_PACKET_HDR_SIGNATURE_CONTROL
        && pPktHdr->u32Signature != KD_PACKET_HDR_SIGNATURE_BREAKIN)
        return false;

    if (pPktHdr->u16SubType >= KD_PACKET_HDR_SUB_TYPE_MAX)
        return false;

    uint32_t idPacket = pPktHdr->idPacket & UINT32_C(0xfffffffe);
    if (   idPacket != KD_PACKET_HDR_ID_INITIAL
        && idPacket != KD_PACKET_HDR_ID_RESET
        && idPacket != 0 /* Happens on the very first packet */)
        return false;

    return true;
}


/**
 * Generates a checksum from the given buffer.
 *
 * @returns Generated checksum.
 * @param   pv                  The data to generate a checksum from.
 * @param   cb                  Number of bytes to checksum.
 */
static uint32_t dbgcKdPktChkSumGen(const void *pv, size_t cb)
{
    const uint8_t *pb = (const uint8_t *)pv;
    uint32_t u32ChkSum = 0;

    while (cb--)
        u32ChkSum += *pb++;

    return u32ChkSum;
}


/**
 * Generates a checksum from the given segments.
 *
 * @returns Generated checksum.
 * @param   paSegs              Pointer to the array of segments containing the data.
 * @param   cSegs               Number of segments.
 * @param   pcbChkSum           Where to store the number of bytes checksummed, optional.
 */
static uint32_t dbgcKdPktChkSumGenSg(PCRTSGSEG paSegs, uint32_t cSegs, size_t *pcbChkSum)
{
    size_t cbChkSum = 0;
    uint32_t u32ChkSum = 0;

    for (uint32_t i = 0; i < cSegs; i++)
    {
        u32ChkSum += dbgcKdPktChkSumGen(paSegs[i].pvSeg, paSegs[i].cbSeg);
        cbChkSum += paSegs[i].cbSeg;
    }

    if (pcbChkSum)
        *pcbChkSum = cbChkSum;

    return u32ChkSum;
}


/**
 * Waits for an acknowledgment.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   msWait              Maximum number of milliseconds to wait for an acknowledge.
 * @param   pfResend            Where to store the resend requested flag on success.
 */
static int dbgcKdCtxPktWaitForAck(PKDCTX pThis, RTMSINTERVAL msWait, bool *pfResend)
{
    KDPACKETHDR PktAck;
    uint8_t *pbCur = (uint8_t *)&PktAck;
    size_t cbLeft = sizeof(PktAck);
    uint64_t tsStartMs = RTTimeMilliTS();
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pThis=%p msWait=%u pfResend=%p\n", pThis, msWait, pfResend));

    RT_ZERO(PktAck);

    /* There might be breakin packets in the queue, read until we get something else. */
    while (   msWait
           && RT_SUCCESS(rc))
    {
        if (pThis->Dbgc.pIo->pfnInput(pThis->Dbgc.pIo, msWait))
        {
            size_t cbRead = 0;
            rc = pThis->Dbgc.pIo->pfnRead(pThis->Dbgc.pIo, pbCur, 1, &cbRead);
            if (   RT_SUCCESS(rc)
                && cbRead == 1)
            {
                uint64_t tsSpanMs = RTTimeMilliTS() - tsStartMs;
                msWait -= RT_MIN(msWait, tsSpanMs);
                tsStartMs = RTTimeMilliTS();

                if (*pbCur == KD_PACKET_HDR_SIGNATURE_BREAKIN_BYTE)
                    pThis->fBreakinRecv = true;
                else
                {
                    pbCur++;
                    cbLeft--;
                    break;
                }
            }
        }
        else
            rc = VERR_TIMEOUT;
    }

    if (   RT_SUCCESS(rc)
        && !msWait)
        rc = VERR_TIMEOUT;

    if (RT_SUCCESS(rc))
    {
        while (   msWait
               && RT_SUCCESS(rc)
               && cbLeft)
        {
            if (pThis->Dbgc.pIo->pfnInput(pThis->Dbgc.pIo, msWait))
            {
                size_t cbRead = 0;
                rc = pThis->Dbgc.pIo->pfnRead(pThis->Dbgc.pIo, pbCur, cbLeft, &cbRead);
                if (RT_SUCCESS(rc))
                {
                    uint64_t tsSpanMs = RTTimeMilliTS() - tsStartMs;
                    msWait -= RT_MIN(msWait, tsSpanMs);
                    tsStartMs = RTTimeMilliTS();

                    cbLeft -= cbRead;
                    pbCur  += cbRead;
                }
            }
            else
                rc = VERR_TIMEOUT;
        }

        if (RT_SUCCESS(rc))
        {
            if (PktAck.u32Signature == KD_PACKET_HDR_SIGNATURE_CONTROL)
            {
                if (PktAck.u16SubType == KD_PACKET_HDR_SUB_TYPE_ACKNOWLEDGE)
                    rc = VINF_SUCCESS;
                else if (PktAck.u16SubType == KD_PACKET_HDR_SUB_TYPE_RESEND)
                {
                    *pfResend = true;
                    rc = VINF_SUCCESS;
                }
                else
                    rc = VERR_NET_PROTOCOL_ERROR;
            }
            else
                rc = VERR_NET_PROTOCOL_ERROR;
        }
    }

    LogFlowFunc(("returns rc=%Rrc *pfResend=%RTbool\n", rc, *pfResend));
    return rc;
}


/**
 * Sends the given packet header and optional segmented body (the trailing byte is sent automatically).
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   u32Signature        The signature to send.
 * @param   u16SubType          The sub type to send.
 * @param   paSegs              Pointer to the array of segments to send in the body, optional.
 * @param   cSegs               Number of segments.
 * @param   fAck                Flag whether to wait for an acknowledge.
 */
static int dbgcKdCtxPktSendSg(PKDCTX pThis, uint32_t u32Signature, uint16_t u16SubType,
                              PCRTSGSEG paSegs, uint32_t cSegs, bool fAck)
{
    int rc = VINF_SUCCESS;
    uint32_t cRetriesLeft = 3;
    uint8_t bTrailer = KD_PACKET_TRAILING_BYTE;
    KDPACKETHDR Hdr;

    size_t cbChkSum = 0;
    uint32_t u32ChkSum = dbgcKdPktChkSumGenSg(paSegs, cSegs, &cbChkSum);

    Hdr.u32Signature = u32Signature;
    Hdr.u16SubType   = u16SubType;
    Hdr.cbBody       = (uint16_t)cbChkSum;
    Hdr.idPacket     = pThis->idPktNext;
    Hdr.u32ChkSum    = u32ChkSum;

#ifdef LOG_ENABLED
    dbgcKdPktDump(&Hdr, paSegs, cSegs, false /*fRx*/);
#endif

    while (cRetriesLeft--)
    {
        bool fResend = false;

        if (pThis->Dbgc.pIo->pfnPktBegin)
        {
            rc = pThis->Dbgc.pIo->pfnPktBegin(pThis->Dbgc.pIo, 0 /*cbPktHint*/);
            AssertRC(rc);
        }

        rc = dbgcKdCtxWrite(pThis, &Hdr, sizeof(Hdr));
        if (   RT_SUCCESS(rc)
            && paSegs
            && cSegs)
        {
            for (uint32_t i = 0; i < cSegs && RT_SUCCESS(rc); i++)
                rc = dbgcKdCtxWrite(pThis, paSegs[i].pvSeg, paSegs[i].cbSeg);

            if (RT_SUCCESS(rc))
                rc = dbgcKdCtxWrite(pThis, &bTrailer, sizeof(bTrailer));
        }

        if (   RT_SUCCESS(rc)
            && pThis->Dbgc.pIo->pfnPktEnd)
            rc = pThis->Dbgc.pIo->pfnPktEnd(pThis->Dbgc.pIo);

        if (RT_SUCCESS(rc))
        {
            if (fAck)
                rc = dbgcKdCtxPktWaitForAck(pThis, 10 * 1000, &fResend);

            if (   RT_SUCCESS(rc)
                && !fResend)
                break;
        }
    }

    return rc;
}


/**
 * Sends the given packet header and optional body (the trailing byte is sent automatically).
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   u32Signature        The signature to send.
 * @param   u16SubType          The sub type to send.
 * @param   pvBody              The body to send, optional.
 * @param   cbBody              Body size in bytes.
 * @param   fAck                Flag whether to wait for an acknowledge.
 */
DECLINLINE(int) dbgcKdCtxPktSend(PKDCTX pThis, uint32_t u32Signature, uint16_t u16SubType,
                                 const void *pvBody, size_t cbBody,
                                 bool fAck)
{
    RTSGSEG Seg;

    Seg.pvSeg = (void *)pvBody;
    Seg.cbSeg = cbBody;
    return dbgcKdCtxPktSendSg(pThis, u32Signature, u16SubType, cbBody ? &Seg : NULL, cbBody ? 1 : 0, fAck);
}


/**
 * Sends a resend packet answer.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 */
DECLINLINE(int) dbgcKdCtxPktSendResend(PKDCTX pThis)
{
    return dbgcKdCtxPktSend(pThis, KD_PACKET_HDR_SIGNATURE_CONTROL, KD_PACKET_HDR_SUB_TYPE_RESEND,
                            NULL /*pvBody*/, 0 /*cbBody*/, false /*fAck*/);
}


/**
 * Sends a resend packet answer.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 */
DECLINLINE(int) dbgcKdCtxPktSendReset(PKDCTX pThis)
{
    pThis->idPktNext = KD_PACKET_HDR_ID_INITIAL;
    return dbgcKdCtxPktSend(pThis, KD_PACKET_HDR_SIGNATURE_CONTROL, KD_PACKET_HDR_SUB_TYPE_RESET,
                            NULL /*pvBody*/, 0 /*cbBody*/, false /*fAck*/);
}


/**
 * Sends an acknowledge packet answer.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 */
DECLINLINE(int) dbgcKdCtxPktSendAck(PKDCTX pThis)
{
    return dbgcKdCtxPktSend(pThis, KD_PACKET_HDR_SIGNATURE_CONTROL, KD_PACKET_HDR_SUB_TYPE_ACKNOWLEDGE,
                            NULL /*pvBody*/, 0 /*cbBody*/, false /*fAck*/);
}


/**
 * Resets the packet receive state machine.
 *
 * @param   pThis               The KD context.
 */
static void dbgcKdCtxPktRecvReset(PKDCTX pThis)
{
    pThis->enmState       = KDRECVSTATE_PACKET_HDR_FIRST_BYTE;
    pThis->pbRecv         = &pThis->PktHdr.ab[0];
    pThis->cbRecvLeft     = sizeof(pThis->PktHdr.ab[0]);
    pThis->msRecvTimeout  = RT_INDEFINITE_WAIT;
    pThis->tsRecvLast     = RTTimeMilliTS();
}


/**
 * Sends a Debug I/O string packet.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context data.
 * @param   idCpu               The CPU ID generating this packet.
 * @param   pachChars           The characters to send (ASCII).
 * @param   cbChars             Number of characters to send.
 */
static int dbgcKdCtxDebugIoStrSend(PKDCTX pThis, VMCPUID idCpu, const char *pachChars, size_t cbChars)
{
    KDPACKETDEBUGIO DebugIo;
    RT_ZERO(DebugIo);

    /* Fix your damn log strings if this exceeds 4GB... */
    if (cbChars != (uint32_t)cbChars)
        return VERR_BUFFER_OVERFLOW;

    DebugIo.u32Type     = KD_PACKET_DEBUG_IO_STRING;
    DebugIo.u16CpuLvl   = 0x6;
    DebugIo.idCpu       = (uint16_t)idCpu;
    DebugIo.u.Str.cbStr = (uint32_t)cbChars;

    RTSGSEG aRespSegs[2];

    aRespSegs[0].pvSeg = &DebugIo;
    aRespSegs[0].cbSeg = sizeof(DebugIo);
    aRespSegs[1].pvSeg = (void *)pachChars;
    aRespSegs[1].cbSeg = cbChars;

    int rc = dbgcKdCtxPktSendSg(pThis, KD_PACKET_HDR_SIGNATURE_DATA, KD_PACKET_HDR_SUB_TYPE_DEBUG_IO,
                                &aRespSegs[0], RT_ELEMENTS(aRespSegs), true /*fAck*/);
    if (RT_SUCCESS(rc))
        pThis->idPktNext ^= 0x1;

    return rc;
}


/**
 * Sends a message to the remotes end.
 *
 * @param   pThis               The KD context data.
 * @param   fWarning            Flag whether this is a warning or an informational message.
 * @param   pszMsg              The message to send.
 */
static void dbgcKdCtxMsgSend(PKDCTX pThis, bool fWarning, const char *pszMsg)
{
    size_t cchMsg = strlen(pszMsg);

    KDPACKETDEBUGIO DebugIo;
    RT_ZERO(DebugIo);

    DebugIo.u32Type     = KD_PACKET_DEBUG_IO_STRING;
    DebugIo.u16CpuLvl   = 0x6;
    DebugIo.idCpu       = 0;

    RTSGSEG aRespSegs[5];

    aRespSegs[0].pvSeg = &DebugIo;
    aRespSegs[0].cbSeg = sizeof(DebugIo);
    aRespSegs[1].pvSeg = (void *)"VBoxDbg ";
    aRespSegs[1].cbSeg = sizeof("VBoxDbg ") - 1;
    if (fWarning)
    {
        aRespSegs[2].pvSeg = (void *)"WARNING ";
        aRespSegs[2].cbSeg = sizeof("WARNING ") - 1;
    }
    else
    {
        aRespSegs[2].pvSeg = (void *)"INFO ";
        aRespSegs[2].cbSeg = sizeof("INFO ") - 1;
    }
    aRespSegs[3].pvSeg = (void *)pszMsg;
    aRespSegs[3].cbSeg = cchMsg;
    aRespSegs[4].pvSeg = (void *)"\r\n";
    aRespSegs[4].cbSeg = 2;

    DebugIo.u.Str.cbStr = (uint32_t)(  aRespSegs[1].cbSeg
                                     + aRespSegs[2].cbSeg
                                     + aRespSegs[3].cbSeg
                                     + aRespSegs[4].cbSeg);

    int rc = dbgcKdCtxPktSendSg(pThis, KD_PACKET_HDR_SIGNATURE_DATA, KD_PACKET_HDR_SUB_TYPE_DEBUG_IO,
                                &aRespSegs[0], RT_ELEMENTS(aRespSegs), true /*fAck*/);
    if (RT_SUCCESS(rc))
        pThis->idPktNext ^= 0x1;
}


/**
 * Queries some user input from the remotes end.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context data.
 * @param   idCpu               The CPU ID generating this packet.
 * @param   pachPrompt          The prompt to send (ASCII).
 * @param   cbPrompt            Number of characters to send for the prompt.
 * @param   cbResponseMax       Maximum size for the response.
 */
static int dbgcKdCtxDebugIoGetStrSend(PKDCTX pThis, VMCPUID idCpu, const char *pachPrompt, size_t cbPrompt,
                                      size_t cbResponseMax)
{
    KDPACKETDEBUGIO DebugIo;
    RT_ZERO(DebugIo);

    /* Fix your damn log strings if this exceeds 4GB... */
    if (   cbPrompt != (uint32_t)cbPrompt
        || cbResponseMax != (uint32_t)cbResponseMax)
        return VERR_BUFFER_OVERFLOW;

    DebugIo.u32Type           = KD_PACKET_DEBUG_IO_GET_STRING;
    DebugIo.u16CpuLvl         = 0x6;
    DebugIo.idCpu             = (uint16_t)idCpu;
    DebugIo.u.Prompt.cbPrompt = (uint32_t)cbPrompt;
    DebugIo.u.Prompt.cbReturn = (uint32_t)cbResponseMax;

    RTSGSEG aRespSegs[2];

    aRespSegs[0].pvSeg = &DebugIo;
    aRespSegs[0].cbSeg = sizeof(DebugIo);
    aRespSegs[1].pvSeg = (void *)pachPrompt;
    aRespSegs[1].cbSeg = cbPrompt;

    int rc = dbgcKdCtxPktSendSg(pThis, KD_PACKET_HDR_SIGNATURE_DATA, KD_PACKET_HDR_SUB_TYPE_DEBUG_IO,
                                &aRespSegs[0], RT_ELEMENTS(aRespSegs), true /*fAck*/);
    if (RT_SUCCESS(rc))
        pThis->idPktNext ^= 0x1;

    return rc;
}


/**
 * Sends a state change event packet.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context data.
 * @param   enmType             The event type.
 */
static int dbgcKdCtxStateChangeSend(PKDCTX pThis, DBGFEVENTTYPE enmType)
{
    LogFlowFunc(("pThis=%p enmType=%u\n", pThis, enmType));

    /* Select the record to send based on the CPU mode. */
    int rc = VINF_SUCCESS;
    KDPACKETSTATECHANGE64 StateChange64;
    RT_ZERO(StateChange64);

    StateChange64.u32StateNew = KD_PACKET_STATE_CHANGE_EXCEPTION;
    StateChange64.u16CpuLvl   = 0x6; /** @todo Figure this one out. */
    StateChange64.idCpu       = pThis->Dbgc.idCpu;
    StateChange64.cCpus       = (uint16_t)DBGFR3CpuGetCount(pThis->Dbgc.pUVM);
    rc = DBGFR3RegCpuQueryU64(pThis->Dbgc.pUVM, pThis->Dbgc.idCpu, DBGFREG_RIP, &StateChange64.u64RipThread);
    if (RT_SUCCESS(rc))
    {
        DBGFADDRESS AddrRip;
        DBGFR3AddrFromFlat(pThis->Dbgc.pUVM, &AddrRip, StateChange64.u64RipThread);

        StateChange64.u64RipThread = KD_PTR_CREATE(pThis, StateChange64.u64RipThread);

        /** @todo Properly fill in the exception record. */
        switch (enmType)
        {
            case DBGFEVENT_HALT_DONE:
            case DBGFEVENT_BREAKPOINT:
            case DBGFEVENT_BREAKPOINT_IO:
            case DBGFEVENT_BREAKPOINT_MMIO:
            case DBGFEVENT_BREAKPOINT_HYPER:
                StateChange64.u.Exception.ExcpRec.u32ExcpCode = KD_PACKET_EXCP_CODE_BKPT;
                break;
            case DBGFEVENT_STEPPED:
            case DBGFEVENT_STEPPED_HYPER:
                pThis->fSingleStepped = true; /* For emulation of DR6. */
                StateChange64.u.Exception.ExcpRec.u32ExcpCode = KD_PACKET_EXCP_CODE_SINGLE_STEP;
                break;
            default:
                AssertMsgFailed(("Invalid DBGF event type for state change %d!\n", enmType));
        }

        StateChange64.u.Exception.ExcpRec.cExcpParms = 3;
        StateChange64.u.Exception.u32FirstChance     = 0x1;

        /** @todo Properly fill in the control report. */
        rc = DBGFR3RegCpuQueryU64(pThis->Dbgc.pUVM, pThis->Dbgc.idCpu, DBGFREG_DR6, &StateChange64.uCtrlReport.Amd64.u64RegDr6);
        if (RT_SUCCESS(rc))
            rc = DBGFR3RegCpuQueryU64(pThis->Dbgc.pUVM, pThis->Dbgc.idCpu, DBGFREG_DR7, &StateChange64.uCtrlReport.Amd64.u64RegDr7);
        if (RT_SUCCESS(rc))
            rc = DBGFR3RegCpuQueryU32(pThis->Dbgc.pUVM, pThis->Dbgc.idCpu, DBGFREG_RFLAGS, &StateChange64.uCtrlReport.Amd64.u32RegEflags);
        if (RT_SUCCESS(rc))
            rc = DBGFR3RegCpuQueryU16(pThis->Dbgc.pUVM, pThis->Dbgc.idCpu, DBGFREG_CS, &StateChange64.uCtrlReport.Amd64.u16SegCs);
        if (RT_SUCCESS(rc))
            rc = DBGFR3RegCpuQueryU16(pThis->Dbgc.pUVM, pThis->Dbgc.idCpu, DBGFREG_DS, &StateChange64.uCtrlReport.Amd64.u16SegDs);
        if (RT_SUCCESS(rc))
            rc = DBGFR3RegCpuQueryU16(pThis->Dbgc.pUVM, pThis->Dbgc.idCpu, DBGFREG_ES, &StateChange64.uCtrlReport.Amd64.u16SegEs);
        if (RT_SUCCESS(rc))
            rc = DBGFR3RegCpuQueryU16(pThis->Dbgc.pUVM, pThis->Dbgc.idCpu, DBGFREG_FS, &StateChange64.uCtrlReport.Amd64.u16SegFs);
        if (RT_SUCCESS(rc))
        {
            /* Read instruction bytes. */
            StateChange64.uCtrlReport.Amd64.cbInsnStream = sizeof(StateChange64.uCtrlReport.Amd64.abInsn);
            rc = DBGFR3MemRead(pThis->Dbgc.pUVM, pThis->Dbgc.idCpu, &AddrRip,
                               &StateChange64.uCtrlReport.Amd64.abInsn[0], StateChange64.uCtrlReport.Amd64.cbInsnStream);
            if (RT_SUCCESS(rc))
            {
                pThis->idPktNext = KD_PACKET_HDR_ID_INITIAL;
                rc = dbgcKdCtxPktSend(pThis, KD_PACKET_HDR_SIGNATURE_DATA, KD_PACKET_HDR_SUB_TYPE_STATE_CHANGE64,
                                      &StateChange64, sizeof(StateChange64), false /*fAck*/);
            }
        }
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


/**
 * Processes a get version 64 request.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   pPktManip           The manipulate packet request.
 */
static int dbgcKdCtxPktManipulate64GetVersion(PKDCTX pThis, PCKDPACKETMANIPULATE64 pPktManip)
{
    KDPACKETMANIPULATE64 Resp;
    RT_ZERO(Resp);

    /* Fill in the generic part. */
    Resp.Hdr.idReq       = KD_PACKET_MANIPULATE_REQ_GET_VERSION;
    Resp.Hdr.u16CpuLvl   = pPktManip->Hdr.u16CpuLvl;
    Resp.Hdr.idCpu       = pPktManip->Hdr.idCpu;
    Resp.Hdr.u32NtStatus = NTSTATUS_SUCCESS;

    /* Build our own response in case there is no Windows interface available. */
    uint32_t NtBuildNumber = 0x0f2800; /* Used when there is no NT interface available, which probably breaks symbol loading. */
    bool f32Bit = false;
    if (pThis->pIfWinNt)
    {
        int rc = pThis->pIfWinNt->pfnQueryVersion(pThis->pIfWinNt, pThis->Dbgc.pUVM, VMMR3GetVTable(),
                                                  NULL /*puVersMajor*/, NULL /*puVersMinor*/,
                                                  &NtBuildNumber, &f32Bit);
        if (RT_SUCCESS(rc))
            rc = pThis->pIfWinNt->pfnQueryKernelPtrs(pThis->pIfWinNt, pThis->Dbgc.pUVM, VMMR3GetVTable(),
                                                     &Resp.u.GetVersion.u64PtrKernBase,
                                                     &Resp.u.GetVersion.u64PtrPsLoadedModuleList);
        if (RT_FAILURE(rc))
            LogRel(("DBGC/Kd: Failed to query WIndows version or kernel pointers rc=%Rrc\n", rc));
    }

    /* Fill in the request specific part. */
    Resp.u.GetVersion.u16VersMaj             = NtBuildNumber >> 16;
    Resp.u.GetVersion.u16VersMin             = NtBuildNumber & UINT32_C(0xffff);
    Resp.u.GetVersion.u8VersProtocol         = 0x6; /* From a Windows 10 guest. */
    Resp.u.GetVersion.u8VersKdSecondary      = pThis->f32Bit ? 0 : 0x2; /* amd64 has a versioned context (0 and 1 are obsolete). */
    Resp.u.GetVersion.fFlags                 = KD_PACKET_MANIPULATE64_GET_VERSION_F_MP;
    Resp.u.GetVersion.u8MaxPktType           = KD_PACKET_HDR_SUB_TYPE_MAX;
    Resp.u.GetVersion.u8MaxStateChange       = KD_PACKET_STATE_CHANGE_MAX - KD_PACKET_STATE_CHANGE_MIN;
    Resp.u.GetVersion.u8MaxManipulate        = KD_PACKET_MANIPULATE_REQ_MAX - KD_PACKET_MANIPULATE_REQ_MIN;
    Resp.u.GetVersion.u64PtrDebuggerDataList = 0;

    if (f32Bit)
    {
        Resp.u.GetVersion.u16MachineType           = IMAGE_FILE_MACHINE_I386;
        Resp.u.GetVersion.u64PtrKernBase           = KD_PTR_CREATE(pThis, Resp.u.GetVersion.u64PtrKernBase);
        Resp.u.GetVersion.u64PtrPsLoadedModuleList = KD_PTR_CREATE(pThis, Resp.u.GetVersion.u64PtrPsLoadedModuleList);
    }
    else
    {
        Resp.u.GetVersion.u16MachineType = IMAGE_FILE_MACHINE_AMD64;
        Resp.u.GetVersion.fFlags |= KD_PACKET_MANIPULATE64_GET_VERSION_F_PTR64;
    }

    return dbgcKdCtxPktSend(pThis, KD_PACKET_HDR_SIGNATURE_DATA, KD_PACKET_HDR_SUB_TYPE_STATE_MANIPULATE,
                            &Resp, sizeof(Resp), true /*fAck*/);
}


/**
 * Processes a read memory 64 request.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   pPktManip           The manipulate packet request.
 */
static int dbgcKdCtxPktManipulate64ReadMem(PKDCTX pThis, PCKDPACKETMANIPULATE64 pPktManip)
{
    KDPACKETMANIPULATEHDR RespHdr;
    KDPACKETMANIPULATE_XFERMEM64 XferMem64;
    uint8_t abMem[_4K];
    RT_ZERO(RespHdr); RT_ZERO(XferMem64);

    DBGFADDRESS AddrRead;
    uint32_t cbRead = RT_MIN(sizeof(abMem), pPktManip->u.XferMem.cbXferReq);
    if (pPktManip->Hdr.idReq == KD_PACKET_MANIPULATE_REQ_READ_VIRT_MEM)
        DBGFR3AddrFromFlat(pThis->Dbgc.pUVM, &AddrRead, KD_PTR_GET(pThis, pPktManip->u.XferMem.u64PtrTarget));
    else
        DBGFR3AddrFromPhys(pThis->Dbgc.pUVM, &AddrRead, KD_PTR_GET(pThis, pPktManip->u.XferMem.u64PtrTarget));

    RTSGSEG aRespSegs[3];
    uint32_t cSegs = 2; /* Gets incremented when read is successful. */
    RespHdr.idReq       = pPktManip->Hdr.idReq;
    RespHdr.u16CpuLvl   = pPktManip->Hdr.u16CpuLvl;
    RespHdr.idCpu       = pPktManip->Hdr.idCpu;
    RespHdr.u32NtStatus = NTSTATUS_SUCCESS;

    XferMem64.u64PtrTarget = pPktManip->u.XferMem.u64PtrTarget;
    XferMem64.cbXferReq    = pPktManip->u.XferMem.cbXferReq;
    XferMem64.cbXfered     = (uint32_t)cbRead;

    aRespSegs[0].pvSeg = &RespHdr;
    aRespSegs[0].cbSeg = sizeof(RespHdr);
    aRespSegs[1].pvSeg = &XferMem64;
    aRespSegs[1].cbSeg = sizeof(XferMem64);

    int rc = DBGFR3MemRead(pThis->Dbgc.pUVM, pThis->Dbgc.idCpu, &AddrRead, &abMem[0], cbRead);
    if (RT_SUCCESS(rc))
    {
        cSegs++;
        aRespSegs[2].pvSeg = &abMem[0];
        aRespSegs[2].cbSeg = cbRead;
    }
    else
        RespHdr.u32NtStatus = NTSTATUS_UNSUCCESSFUL; /** @todo Convert to an appropriate NT status code. */

    return dbgcKdCtxPktSendSg(pThis, KD_PACKET_HDR_SIGNATURE_DATA, KD_PACKET_HDR_SUB_TYPE_STATE_MANIPULATE,
                              &aRespSegs[0], cSegs, true /*fAck*/);
}


/**
 * Processes a write memory 64 request.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   pPktManip           The manipulate packet request.
 */
static int dbgcKdCtxPktManipulate64WriteMem(PKDCTX pThis, PCKDPACKETMANIPULATE64 pPktManip)
{
    KDPACKETMANIPULATEHDR RespHdr;
    KDPACKETMANIPULATE_XFERMEM64 XferMem64;
    RT_ZERO(RespHdr); RT_ZERO(XferMem64);

    DBGFADDRESS AddrWrite;
    const void *pv = &pThis->abBody[sizeof(*pPktManip)]; /* Data comes directly after the manipulate state body. */
    uint32_t cbWrite = RT_MIN(sizeof(pThis->abBody) - sizeof(*pPktManip), pPktManip->u.XferMem.cbXferReq);
    if (pPktManip->Hdr.idReq == KD_PACKET_MANIPULATE_REQ_WRITE_VIRT_MEM)
        DBGFR3AddrFromFlat(pThis->Dbgc.pUVM, &AddrWrite, KD_PTR_GET(pThis, pPktManip->u.XferMem.u64PtrTarget));
    else
        DBGFR3AddrFromPhys(pThis->Dbgc.pUVM, &AddrWrite, KD_PTR_GET(pThis, pPktManip->u.XferMem.u64PtrTarget));

    RTSGSEG aRespSegs[2];
    uint32_t cSegs = 2;
    RespHdr.idReq       = pPktManip->Hdr.idReq;
    RespHdr.u16CpuLvl   = pPktManip->Hdr.u16CpuLvl;
    RespHdr.idCpu       = pPktManip->Hdr.idCpu;
    RespHdr.u32NtStatus = NTSTATUS_SUCCESS;

    XferMem64.u64PtrTarget = pPktManip->u.XferMem.u64PtrTarget;
    XferMem64.cbXferReq    = pPktManip->u.XferMem.cbXferReq;
    XferMem64.cbXfered     = (uint32_t)cbWrite;

    aRespSegs[0].pvSeg = &RespHdr;
    aRespSegs[0].cbSeg = sizeof(RespHdr);
    aRespSegs[1].pvSeg = &XferMem64;
    aRespSegs[1].cbSeg = sizeof(XferMem64);

    int rc = DBGFR3MemWrite(pThis->Dbgc.pUVM, pThis->Dbgc.idCpu, &AddrWrite, pv, cbWrite);
    if (RT_FAILURE(rc))
        RespHdr.u32NtStatus = NTSTATUS_UNSUCCESSFUL; /** @todo Convert to an appropriate NT status code. */

    return dbgcKdCtxPktSendSg(pThis, KD_PACKET_HDR_SIGNATURE_DATA, KD_PACKET_HDR_SUB_TYPE_STATE_MANIPULATE,
                              &aRespSegs[0], cSegs, true /*fAck*/);
}


/**
 * Processes a continue request.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   pPktManip           The manipulate packet request.
 */
static int dbgcKdCtxPktManipulate64Continue(PKDCTX pThis, PCKDPACKETMANIPULATE64 pPktManip)
{
    RT_NOREF(pPktManip);
    int rc = VINF_SUCCESS;

    /* No response, just resume. */
    if (DBGFR3IsHalted(pThis->Dbgc.pUVM, VMCPUID_ALL))
        rc = DBGFR3Resume(pThis->Dbgc.pUVM, VMCPUID_ALL);

    return rc;
}


/**
 * Processes a continue request.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   pPktManip           The manipulate packet request.
 */
static int dbgcKdCtxPktManipulate64Continue2(PKDCTX pThis, PCKDPACKETMANIPULATE64 pPktManip)
{
    int rc = VINF_SUCCESS;

    /* Update DR7. */
    if (pThis->f32Bit)
        rc = dbgcKdCtxHwBpDr7Update(pThis, pPktManip->u.Continue2.u.x86.u32RegDr7);
    else
        rc = dbgcKdCtxHwBpDr7Update(pThis, (uint32_t)pPktManip->u.Continue2.u.amd64.u64RegDr7);

    /* Resume if not single stepping, the single step will get a state change when the VM stepped. */
    if (pPktManip->u.Continue2.fTrace)
    {
        PDBGFADDRESS pStackPop  = NULL;
        RTGCPTR      cbStackPop = 0;
        rc = DBGFR3StepEx(pThis->Dbgc.pUVM, pThis->Dbgc.idCpu, DBGF_STEP_F_INTO, NULL,
                          pStackPop, cbStackPop, 1 /*cMaxSteps*/);
    }
    else if (DBGFR3IsHalted(pThis->Dbgc.pUVM, VMCPUID_ALL))
        rc = DBGFR3Resume(pThis->Dbgc.pUVM, VMCPUID_ALL);

    return rc;
}


/**
 * Processes a set context request.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   pPktManip           The manipulate packet request.
 */
static int dbgcKdCtxPktManipulate64SetContext(PKDCTX pThis, PCKDPACKETMANIPULATE64 pPktManip)
{
    KDPACKETMANIPULATEHDR RespHdr;
    KDPACKETMANIPULATE_SETCONTEXT SetContext;
    RT_ZERO(RespHdr); RT_ZERO(SetContext);

    PCNTCONTEXT64 pNtCtx = (PCNTCONTEXT64)&pThis->abBody[sizeof(*pPktManip)]; /* Data comes directly after the manipulate state body. */

    RTSGSEG aRespSegs[2];
    uint32_t cSegs = 2;
    RespHdr.idReq       = pPktManip->Hdr.idReq;
    RespHdr.u16CpuLvl   = pPktManip->Hdr.u16CpuLvl;
    RespHdr.idCpu       = pPktManip->Hdr.idCpu;
    RespHdr.u32NtStatus = NTSTATUS_SUCCESS;

    /** @todo What do these flags mean? Can't be the context state to set because the valid one is
     * in NTCONTEXT64::fContext (observed with WinDbg). */
    SetContext.u32CtxFlags = pPktManip->u.SetContext.u32CtxFlags;

    aRespSegs[0].pvSeg = &RespHdr;
    aRespSegs[0].cbSeg = sizeof(RespHdr);
    aRespSegs[1].pvSeg = &SetContext;
    aRespSegs[1].cbSeg = sizeof(SetContext);

    int rc = dbgcKdCtxSetNtCtx64(pThis, pPktManip->Hdr.idCpu, pNtCtx, pNtCtx->fContext);
    if (RT_FAILURE(rc))
        RespHdr.u32NtStatus = NTSTATUS_UNSUCCESSFUL; /** @todo Convert to an appropriate NT status code. */

    return dbgcKdCtxPktSendSg(pThis, KD_PACKET_HDR_SIGNATURE_DATA, KD_PACKET_HDR_SUB_TYPE_STATE_MANIPULATE,
                              &aRespSegs[0], cSegs, true /*fAck*/);
}


/**
 * Processes a read control space 64 request.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   pPktManip           The manipulate packet request.
 */
static int dbgcKdCtxPktManipulate64ReadCtrlSpace(PKDCTX pThis, PCKDPACKETMANIPULATE64 pPktManip)
{
    KDPACKETMANIPULATEHDR RespHdr;
    KDPACKETMANIPULATE_XFERCTRLSPACE64 XferCtrlSpace64;
    uint8_t abResp[sizeof(NTKCONTEXT64)];
    uint32_t cbData = 0;
    RT_ZERO(RespHdr); RT_ZERO(XferCtrlSpace64);
    RT_ZERO(abResp);

    RTSGSEG aRespSegs[3];
    uint32_t cSegs = 2; /* Gets incremented when read is successful. */
    RespHdr.idReq       = KD_PACKET_MANIPULATE_REQ_READ_CTRL_SPACE;
    RespHdr.u16CpuLvl   = pPktManip->Hdr.u16CpuLvl;
    RespHdr.idCpu       = pPktManip->Hdr.idCpu;
    RespHdr.u32NtStatus = NTSTATUS_SUCCESS;

    XferCtrlSpace64.u64IdXfer = pPktManip->u.XferCtrlSpace.u64IdXfer;
    XferCtrlSpace64.cbXferReq = pPktManip->u.XferCtrlSpace.cbXferReq;

    aRespSegs[0].pvSeg = &RespHdr;
    aRespSegs[0].cbSeg = sizeof(RespHdr);
    aRespSegs[1].pvSeg = &XferCtrlSpace64;
    aRespSegs[1].cbSeg = sizeof(XferCtrlSpace64);

    int rc = VINF_SUCCESS;
    if (pThis->f32Bit)
    {
        if (pPktManip->u.XferCtrlSpace.u64IdXfer == sizeof(NTCONTEXT32))
        {
            /* Queries the kernel context. */
            rc = dbgcKdCtxQueryNtKCtx32(pThis, RespHdr.idCpu, (PNTKCONTEXT32)&abResp[0]);
            if (RT_SUCCESS(rc))
                cbData = sizeof(NTKCONTEXT32);
        }
    }
    else
    {
        switch (pPktManip->u.XferCtrlSpace.u64IdXfer)
        {
            case KD_PACKET_MANIPULATE64_CTRL_SPACE_ID_KPCR:
            {
                if (pThis->pIfWinNt)
                {
                    RTGCUINTPTR GCPtrKpcr = 0;

                    rc = pThis->pIfWinNt->pfnQueryKpcrForVCpu(pThis->pIfWinNt, pThis->Dbgc.pUVM, VMMR3GetVTable(), RespHdr.idCpu,
                                                              &GCPtrKpcr, NULL /*pKpcrb*/);
                    if (RT_SUCCESS(rc))
                        memcpy(&abResp[0], &GCPtrKpcr, sizeof(GCPtrKpcr));
                }

                cbData = sizeof(RTGCUINTPTR);
                break;
            }
            case KD_PACKET_MANIPULATE64_CTRL_SPACE_ID_KPCRB:
            {
                if (pThis->pIfWinNt)
                {
                    RTGCUINTPTR GCPtrKpcrb = 0;

                    rc = pThis->pIfWinNt->pfnQueryKpcrForVCpu(pThis->pIfWinNt, pThis->Dbgc.pUVM, VMMR3GetVTable(), RespHdr.idCpu,
                                                              NULL /*pKpcr*/, &GCPtrKpcrb);
                    if (RT_SUCCESS(rc))
                        memcpy(&abResp[0], &GCPtrKpcrb, sizeof(GCPtrKpcrb));
                }

                cbData = sizeof(RTGCUINTPTR);
                break;
            }
            case KD_PACKET_MANIPULATE64_CTRL_SPACE_ID_KCTX:
            {
                rc = dbgcKdCtxQueryNtKCtx64(pThis, RespHdr.idCpu, (PNTKCONTEXT64)&abResp[0], NTCONTEXT64_F_FULL);
                if (RT_SUCCESS(rc))
                    cbData = sizeof(NTKCONTEXT64);
                break;
            }
            case KD_PACKET_MANIPULATE64_CTRL_SPACE_ID_KTHRD:
            {
                if (pThis->pIfWinNt)
                {
                    RTGCUINTPTR GCPtrCurThrd = 0;

                    rc = pThis->pIfWinNt->pfnQueryCurThrdForVCpu(pThis->pIfWinNt, pThis->Dbgc.pUVM, VMMR3GetVTable(),
                                                                 RespHdr.idCpu, &GCPtrCurThrd);
                    if (RT_SUCCESS(rc))
                        memcpy(&abResp[0], &GCPtrCurThrd, sizeof(GCPtrCurThrd));
                }

                cbData = sizeof(RTGCUINTPTR);
                break;
            }
            default:
                rc = VERR_NOT_SUPPORTED;
                break;
        }
    }

    if (   RT_SUCCESS(rc)
        && cbData)
    {
        XferCtrlSpace64.cbXfered = RT_MIN(cbData, XferCtrlSpace64.cbXferReq);

        cSegs++;
        aRespSegs[2].pvSeg = &abResp[0];
        aRespSegs[2].cbSeg = cbData;
    }
    else if (RT_FAILURE(rc))
        RespHdr.u32NtStatus = NTSTATUS_UNSUCCESSFUL; /** @todo Convert to an appropriate NT status code. */

    return dbgcKdCtxPktSendSg(pThis, KD_PACKET_HDR_SIGNATURE_DATA, KD_PACKET_HDR_SUB_TYPE_STATE_MANIPULATE,
                              &aRespSegs[0], cSegs, true /*fAck*/);
}


/**
 * Processes a write control space 64 request.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   pPktManip           The manipulate packet request.
 */
static int dbgcKdCtxPktManipulate64WriteCtrlSpace(PKDCTX pThis, PCKDPACKETMANIPULATE64 pPktManip)
{
    KDPACKETMANIPULATEHDR RespHdr;
    KDPACKETMANIPULATE_XFERCTRLSPACE64 XferCtrlSpace64;
    uint32_t cbData = 0;
    RT_ZERO(RespHdr); RT_ZERO(XferCtrlSpace64);

    RTSGSEG aRespSegs[2];
    RespHdr.idReq       = KD_PACKET_MANIPULATE_REQ_WRITE_CTRL_SPACE;
    RespHdr.u16CpuLvl   = pPktManip->Hdr.u16CpuLvl;
    RespHdr.idCpu       = pPktManip->Hdr.idCpu;
    RespHdr.u32NtStatus = NTSTATUS_SUCCESS;

    XferCtrlSpace64.u64IdXfer = pPktManip->u.XferCtrlSpace.u64IdXfer;
    XferCtrlSpace64.cbXferReq = pPktManip->u.XferCtrlSpace.cbXferReq;

    aRespSegs[0].pvSeg = &RespHdr;
    aRespSegs[0].cbSeg = sizeof(RespHdr);
    aRespSegs[1].pvSeg = &XferCtrlSpace64;
    aRespSegs[1].cbSeg = sizeof(XferCtrlSpace64);

    int rc = VINF_SUCCESS;
    switch (pPktManip->u.XferCtrlSpace.u64IdXfer)
    {
        case KD_PACKET_MANIPULATE64_CTRL_SPACE_ID_KCTX:
        {
            PCNTKCONTEXT64 pNtKCtx = (PCNTKCONTEXT64)&pThis->abBody[sizeof(*pPktManip)]; /* Data comes directly after the manipulate state body. */
            rc = dbgcKdCtxSetNtKCtx64(pThis, RespHdr.idCpu, pNtKCtx, XferCtrlSpace64.cbXferReq);
            if (RT_SUCCESS(rc))
                cbData = RT_MIN(XferCtrlSpace64.cbXferReq, sizeof(NTKCONTEXT64));
            break;
        }
        case KD_PACKET_MANIPULATE64_CTRL_SPACE_ID_KPCR:
        case KD_PACKET_MANIPULATE64_CTRL_SPACE_ID_KPCRB:
        case KD_PACKET_MANIPULATE64_CTRL_SPACE_ID_KTHRD:
        default:
            rc = VERR_NOT_SUPPORTED;
            break;
    }

    if (RT_FAILURE(rc))
        RespHdr.u32NtStatus = NTSTATUS_UNSUCCESSFUL; /** @todo Convert to an appropriate NT status code. */
    else
        XferCtrlSpace64.cbXfered = cbData;

    return dbgcKdCtxPktSendSg(pThis, KD_PACKET_HDR_SIGNATURE_DATA, KD_PACKET_HDR_SUB_TYPE_STATE_MANIPULATE,
                              &aRespSegs[0], RT_ELEMENTS(aRespSegs), true /*fAck*/);
}


/**
 * Processes a restore breakpoint 64 request.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   pPktManip           The manipulate packet request.
 */
static int dbgcKdCtxPktManipulate64RestoreBkpt(PKDCTX pThis, PCKDPACKETMANIPULATE64 pPktManip)
{
    KDPACKETMANIPULATEHDR RespHdr;
    KDPACKETMANIPULATE_RESTOREBKPT64 RestoreBkpt64;
    RT_ZERO(RespHdr); RT_ZERO(RestoreBkpt64);

    RTSGSEG aRespSegs[2];
    RespHdr.idReq       = KD_PACKET_MANIPULATE_REQ_RESTORE_BKPT;
    RespHdr.u16CpuLvl   = pPktManip->Hdr.u16CpuLvl;
    RespHdr.idCpu       = pPktManip->Hdr.idCpu;
    RespHdr.u32NtStatus = NTSTATUS_SUCCESS;

    RestoreBkpt64.u32HndBkpt = pPktManip->u.RestoreBkpt.u32HndBkpt;

    aRespSegs[0].pvSeg = &RespHdr;
    aRespSegs[0].cbSeg = sizeof(RespHdr);
    aRespSegs[1].pvSeg = &RestoreBkpt64;
    aRespSegs[1].cbSeg = sizeof(RestoreBkpt64);

    int rc = DBGFR3BpClear(pThis->Dbgc.pUVM, pPktManip->u.RestoreBkpt.u32HndBkpt);
    if (RT_SUCCESS(rc))
    {
        rc = dbgcBpDelete(&pThis->Dbgc, pPktManip->u.RestoreBkpt.u32HndBkpt);
        AssertRC(rc);
    }
    else if (rc != VERR_DBGF_BP_NOT_FOUND)
        RespHdr.u32NtStatus = NTSTATUS_UNSUCCESSFUL;

    return dbgcKdCtxPktSendSg(pThis, KD_PACKET_HDR_SIGNATURE_DATA, KD_PACKET_HDR_SUB_TYPE_STATE_MANIPULATE,
                              &aRespSegs[0], RT_ELEMENTS(aRespSegs), true /*fAck*/);
}


/**
 * Processes a write breakpoint 64 request.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   pPktManip           The manipulate packet request.
 */
static int dbgcKdCtxPktManipulate64WriteBkpt(PKDCTX pThis, PCKDPACKETMANIPULATE64 pPktManip)
{
    KDPACKETMANIPULATEHDR RespHdr;
    KDPACKETMANIPULATE_WRITEBKPT64 WriteBkpt64;
    RT_ZERO(RespHdr); RT_ZERO(WriteBkpt64);

    RTSGSEG aRespSegs[2];
    RespHdr.idReq       = KD_PACKET_MANIPULATE_REQ_WRITE_BKPT;
    RespHdr.u16CpuLvl   = pPktManip->Hdr.u16CpuLvl;
    RespHdr.idCpu       = pPktManip->Hdr.idCpu;
    RespHdr.u32NtStatus = NTSTATUS_SUCCESS;

    aRespSegs[0].pvSeg = &RespHdr;
    aRespSegs[0].cbSeg = sizeof(RespHdr);
    aRespSegs[1].pvSeg = &WriteBkpt64;
    aRespSegs[1].cbSeg = sizeof(WriteBkpt64);

    WriteBkpt64.u64PtrBkpt = pPktManip->u.WriteBkpt.u64PtrBkpt;

    DBGFADDRESS BpAddr;
    DBGFR3AddrFromFlat(pThis->Dbgc.pUVM, &BpAddr, KD_PTR_GET(pThis, pPktManip->u.WriteBkpt.u64PtrBkpt));
    int rc = DBGFR3BpSetInt3(pThis->Dbgc.pUVM, pThis->Dbgc.idCpu, &BpAddr,
                             1 /*iHitTrigger*/, UINT64_MAX /*iHitDisable*/, &WriteBkpt64.u32HndBkpt);
    if (RT_SUCCESS(rc))
    {
        rc = dbgcBpAdd(&pThis->Dbgc, WriteBkpt64.u32HndBkpt, NULL /*pszCmd*/);
        if (RT_FAILURE(rc))
            DBGFR3BpClear(pThis->Dbgc.pUVM, WriteBkpt64.u32HndBkpt);
    }
    else
        RespHdr.u32NtStatus = NTSTATUS_UNSUCCESSFUL;

    return dbgcKdCtxPktSendSg(pThis, KD_PACKET_HDR_SIGNATURE_DATA, KD_PACKET_HDR_SUB_TYPE_STATE_MANIPULATE,
                              &aRespSegs[0], RT_ELEMENTS(aRespSegs), true /*fAck*/);
}


/**
 * Processes a get context extended 64 request.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   pPktManip           The manipulate packet request.
 */
static int dbgcKdCtxPktManipulate64GetContextEx(PKDCTX pThis, PCKDPACKETMANIPULATE64 pPktManip)
{
    KDPACKETMANIPULATEHDR RespHdr;
    KDPACKETMANIPULATE_CONTEXTEX ContextEx;
    union
    {
        NTCONTEXT64 v64;
        NTCONTEXT32 v32;
    } NtCtx;
    RT_ZERO(RespHdr); RT_ZERO(ContextEx); RT_ZERO(NtCtx);

    RTSGSEG aRespSegs[3];
    uint32_t cSegs = 2;
    RespHdr.idReq       = KD_PACKET_MANIPULATE_REQ_GET_CONTEXT_EX;
    RespHdr.u16CpuLvl   = pPktManip->Hdr.u16CpuLvl;
    RespHdr.idCpu       = pPktManip->Hdr.idCpu;
    RespHdr.u32NtStatus = NTSTATUS_UNSUCCESSFUL;

    ContextEx.offStart = pPktManip->u.ContextEx.offStart;
    ContextEx.cbXfer   = pPktManip->u.ContextEx.cbXfer;
    ContextEx.cbXfered = 0;

    aRespSegs[0].pvSeg = &RespHdr;
    aRespSegs[0].cbSeg = sizeof(RespHdr);
    aRespSegs[1].pvSeg = &ContextEx;
    aRespSegs[1].cbSeg = sizeof(ContextEx);

    int rc = VINF_SUCCESS;
    uint32_t cbCtx = pThis->f32Bit ? sizeof(NtCtx.v32) : sizeof(NtCtx.v64);
    if (pThis->f32Bit)
        dbgcKdCtxQueryNtCtx32(pThis, pPktManip->Hdr.idCpu, &NtCtx.v32, NTCONTEXT32_F_FULL);
    else
        dbgcKdCtxQueryNtCtx64(pThis, pPktManip->Hdr.idCpu, &NtCtx.v64, NTCONTEXT64_F_FULL);
    if (   RT_SUCCESS(rc)
        && pPktManip->u.ContextEx.offStart < cbCtx)
    {
        RespHdr.u32NtStatus = NTSTATUS_SUCCESS;
        ContextEx.cbXfered = RT_MIN(cbCtx - ContextEx.offStart, ContextEx.cbXfer);

        aRespSegs[2].pvSeg = (uint8_t *)&NtCtx + ContextEx.offStart;
        aRespSegs[2].cbSeg = ContextEx.cbXfered;
        cSegs++;
    }

    return dbgcKdCtxPktSendSg(pThis, KD_PACKET_HDR_SIGNATURE_DATA, KD_PACKET_HDR_SUB_TYPE_STATE_MANIPULATE,
                              &aRespSegs[0], cSegs, true /*fAck*/);
}


/**
 * Processes a query memory 64 request.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   pPktManip           The manipulate packet request.
 */
static int dbgcKdCtxPktManipulate64QueryMemory(PKDCTX pThis, PCKDPACKETMANIPULATE64 pPktManip)
{
    KDPACKETMANIPULATEHDR RespHdr;
    KDPACKETMANIPULATE_QUERYMEMORY QueryMemory;
    RT_ZERO(RespHdr); RT_ZERO(QueryMemory);

    RTSGSEG aRespSegs[2];
    RespHdr.idReq       = KD_PACKET_MANIPULATE_REQ_QUERY_MEMORY;
    RespHdr.u16CpuLvl   = pPktManip->Hdr.u16CpuLvl;
    RespHdr.idCpu       = pPktManip->Hdr.idCpu;
    RespHdr.u32NtStatus = NTSTATUS_SUCCESS;

    /** @todo Need DBGF API to query protection and privilege level from guest page tables. */
    QueryMemory.u64GCPtr     = pPktManip->u.QueryMemory.u64GCPtr;
    QueryMemory.u32AddrSpace = KD_PACKET_MANIPULATE64_QUERY_MEMORY_ADDR_SPACE_KERNEL;
    QueryMemory.u32Flags     =   KD_PACKET_MANIPULATE64_QUERY_MEMORY_ADDR_F_READ
                               | KD_PACKET_MANIPULATE64_QUERY_MEMORY_ADDR_F_WRITE
                               | KD_PACKET_MANIPULATE64_QUERY_MEMORY_ADDR_F_EXEC;

    aRespSegs[0].pvSeg = &RespHdr;
    aRespSegs[0].cbSeg = sizeof(RespHdr);
    aRespSegs[1].pvSeg = &QueryMemory;
    aRespSegs[1].cbSeg = sizeof(QueryMemory);

    return dbgcKdCtxPktSendSg(pThis, KD_PACKET_HDR_SIGNATURE_DATA, KD_PACKET_HDR_SUB_TYPE_STATE_MANIPULATE,
                              &aRespSegs[0], RT_ELEMENTS(aRespSegs), true /*fAck*/);
}


/**
 * Processes a search memory 64 request.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   pPktManip           The manipulate packet request.
 */
static int dbgcKdCtxPktManipulate64SearchMemory(PKDCTX pThis, PCKDPACKETMANIPULATE64 pPktManip)
{
    KDPACKETMANIPULATEHDR RespHdr;
    KDPACKETMANIPULATE_SEARCHMEMORY SearchMemory;
    RT_ZERO(RespHdr); RT_ZERO(SearchMemory);

    RTSGSEG aRespSegs[2];
    RespHdr.idReq       = KD_PACKET_MANIPULATE_REQ_SEARCH_MEMORY;
    RespHdr.u16CpuLvl   = pPktManip->Hdr.u16CpuLvl;
    RespHdr.idCpu       = pPktManip->Hdr.idCpu;
    RespHdr.u32NtStatus = NTSTATUS_SUCCESS;

    SearchMemory.u64GCPtr  = pPktManip->u.SearchMemory.u64GCPtr;
    SearchMemory.cbSearch  = pPktManip->u.SearchMemory.cbSearch;
    SearchMemory.cbPattern = pPktManip->u.SearchMemory.cbPattern;

    /* Validate the pattern length and start searching. */
    if (pPktManip->u.SearchMemory.cbPattern < sizeof(pThis->abBody) - sizeof(*pPktManip))
    {
        DBGFADDRESS StartAddress;
        DBGFADDRESS HitAddress;
        VMCPUID idCpu = pPktManip->Hdr.idCpu;
        DBGFR3AddrFromFlat(pThis->Dbgc.pUVM, &StartAddress, pPktManip->u.SearchMemory.u64GCPtr);

        /** @todo WindDbg sends CPU ID 32 sometimes, maybe that means continue search on last used CPU?. */
        if (idCpu >= DBGFR3CpuGetCount(pThis->Dbgc.pUVM))
            idCpu = pThis->Dbgc.idCpu;

        int rc = DBGFR3MemScan(pThis->Dbgc.pUVM, idCpu, &StartAddress, pPktManip->u.SearchMemory.cbSearch, 1,
                               &pThis->abBody[sizeof(*pPktManip)], pPktManip->u.SearchMemory.cbPattern, &HitAddress);
        if (RT_SUCCESS(rc))
            SearchMemory.u64GCPtr = HitAddress.FlatPtr;
        else if (rc == VERR_DBGF_MEM_NOT_FOUND)
            RespHdr.u32NtStatus = NTSTATUS_NOT_FOUND;
        else
            RespHdr.u32NtStatus = NTSTATUS_UNSUCCESSFUL;
    }
    else
        RespHdr.u32NtStatus = NTSTATUS_BUFFER_OVERFLOW;

    aRespSegs[0].pvSeg = &RespHdr;
    aRespSegs[0].cbSeg = sizeof(RespHdr);
    aRespSegs[1].pvSeg = &SearchMemory;
    aRespSegs[1].cbSeg = sizeof(SearchMemory);

    return dbgcKdCtxPktSendSg(pThis, KD_PACKET_HDR_SIGNATURE_DATA, KD_PACKET_HDR_SUB_TYPE_STATE_MANIPULATE,
                              &aRespSegs[0], RT_ELEMENTS(aRespSegs), true /*fAck*/);
}


/**
 * Processes a cause bugcheck 64 request.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   pPktManip           The manipulate packet request.
 *
 * @note We abuse this request to initiate a native VBox debugger command prompt from the remote end
  *      (There is monitor/Rcmd equivalent like with GDB unfortunately).
 */
static int dbgcKdCtxPktManipulate64CauseBugCheck(PKDCTX pThis, PCKDPACKETMANIPULATE64 pPktManip)
{
    RT_NOREF(pPktManip);
    pThis->fInVBoxDbg = true;
    return dbgcKdCtxDebugIoGetStrSend(pThis, pThis->Dbgc.idCpu, "VBoxDbg>", sizeof("VBoxDbg>") - 1,
                                      512 /*cbResponseMax*/);
}


/**
 * Processes a switch processor request.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 * @param   pPktManip           The manipulate packet request.
 */
static int dbgcKdCtxPktManipulate64SwitchProcessor(PKDCTX pThis, PCKDPACKETMANIPULATE64 pPktManip)
{
    int rc = VINF_SUCCESS;

    if (RT_UNLIKELY(pPktManip->Hdr.idCpu >= DBGFR3CpuGetCount(pThis->Dbgc.pUVM)))
    {
        KDPACKETMANIPULATEHDR RespHdr;
        RT_ZERO(RespHdr);

        RespHdr.idReq       = KD_PACKET_MANIPULATE_REQ_SWITCH_PROCESSOR;
        RespHdr.u16CpuLvl   = pPktManip->Hdr.u16CpuLvl;
        RespHdr.idCpu       = pPktManip->Hdr.idCpu;
        RespHdr.u32NtStatus = NTSTATUS_UNSUCCESSFUL; /** @todo Test this path. */
        rc = dbgcKdCtxPktSend(pThis, KD_PACKET_HDR_SIGNATURE_DATA, KD_PACKET_HDR_SUB_TYPE_STATE_MANIPULATE,
                              &RespHdr, sizeof(RespHdr), true /*fAck*/);
    }
    else
    {
        pThis->Dbgc.idCpu = pPktManip->Hdr.idCpu;
        rc = dbgcKdCtxStateChangeSend(pThis, DBGFEVENT_HALT_DONE);
    }

    return rc;
}


/**
 * Processes a manipulate packet.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 */
static int dbgcKdCtxPktManipulate64Process(PKDCTX pThis)
{
    int rc = VINF_SUCCESS;
    PCKDPACKETMANIPULATE64 pPktManip = (PCKDPACKETMANIPULATE64)&pThis->abBody[0];

    switch (pPktManip->Hdr.idReq)
    {
        case KD_PACKET_MANIPULATE_REQ_GET_VERSION:
        {
            rc = dbgcKdCtxPktManipulate64GetVersion(pThis, pPktManip);
            break;
        }
        case KD_PACKET_MANIPULATE_REQ_READ_VIRT_MEM:
        case KD_PACKET_MANIPULATE_REQ_READ_PHYS_MEM:
        {
            rc = dbgcKdCtxPktManipulate64ReadMem(pThis, pPktManip);
            break;
        }
        case KD_PACKET_MANIPULATE_REQ_WRITE_VIRT_MEM:
        case KD_PACKET_MANIPULATE_REQ_WRITE_PHYS_MEM:
        {
            rc = dbgcKdCtxPktManipulate64WriteMem(pThis, pPktManip);
            break;
        }
        case KD_PACKET_MANIPULATE_REQ_CONTINUE:
        {
            rc = dbgcKdCtxPktManipulate64Continue(pThis, pPktManip);
            break;
        }
        case KD_PACKET_MANIPULATE_REQ_CONTINUE2:
        {
            rc = dbgcKdCtxPktManipulate64Continue2(pThis, pPktManip);
            break;
        }
        case KD_PACKET_MANIPULATE_REQ_SET_CONTEXT:
        {
            rc = dbgcKdCtxPktManipulate64SetContext(pThis, pPktManip);
            break;
        }
        case KD_PACKET_MANIPULATE_REQ_READ_CTRL_SPACE:
        {
            rc = dbgcKdCtxPktManipulate64ReadCtrlSpace(pThis, pPktManip);
            break;
        }
        case KD_PACKET_MANIPULATE_REQ_WRITE_CTRL_SPACE:
        {
            rc = dbgcKdCtxPktManipulate64WriteCtrlSpace(pThis, pPktManip);
            break;
        }
        case KD_PACKET_MANIPULATE_REQ_RESTORE_BKPT:
        {
            rc = dbgcKdCtxPktManipulate64RestoreBkpt(pThis, pPktManip);
            break;
        }
        case KD_PACKET_MANIPULATE_REQ_WRITE_BKPT:
        {
            rc = dbgcKdCtxPktManipulate64WriteBkpt(pThis, pPktManip);
            break;
        }
        case KD_PACKET_MANIPULATE_REQ_CLEAR_ALL_INTERNAL_BKPT:
            /* WinDbg doesn't seem to expect an answer apart from the ACK here. */
            break;
        case KD_PACKET_MANIPULATE_REQ_GET_CONTEXT_EX:
        {
            rc = dbgcKdCtxPktManipulate64GetContextEx(pThis, pPktManip);
            break;
        }
        case KD_PACKET_MANIPULATE_REQ_QUERY_MEMORY:
        {
            rc = dbgcKdCtxPktManipulate64QueryMemory(pThis, pPktManip);
            break;
        }
        case KD_PACKET_MANIPULATE_REQ_SEARCH_MEMORY:
        {
            rc = dbgcKdCtxPktManipulate64SearchMemory(pThis, pPktManip);
            break;
        }
        case KD_PACKET_MANIPULATE_REQ_CAUSE_BUGCHECK:
        {
            rc = dbgcKdCtxPktManipulate64CauseBugCheck(pThis, pPktManip);
            break;
        }
        case KD_PACKET_MANIPULATE_REQ_SWITCH_PROCESSOR:
        {
            rc = dbgcKdCtxPktManipulate64SwitchProcessor(pThis, pPktManip);
            break;
        }
        case KD_PACKET_MANIPULATE_REQ_REBOOT:
        {
            rc = VMR3Reset(pThis->Dbgc.pUVM); /* Doesn't expect an answer here. */
            if (   RT_SUCCESS(rc)
                && DBGFR3IsHalted(pThis->Dbgc.pUVM, VMCPUID_ALL))
                rc = DBGFR3Resume(pThis->Dbgc.pUVM, VMCPUID_ALL);
            break;
        }
        default:
            KDPACKETMANIPULATEHDR RespHdr;
            RT_ZERO(RespHdr);

            RespHdr.idReq       = pPktManip->Hdr.idReq;
            RespHdr.u16CpuLvl   = pPktManip->Hdr.u16CpuLvl;
            RespHdr.idCpu       = pPktManip->Hdr.idCpu;
            RespHdr.u32NtStatus = NTSTATUS_NOT_IMPLEMENTED;
            rc = dbgcKdCtxPktSend(pThis, KD_PACKET_HDR_SIGNATURE_DATA, KD_PACKET_HDR_SUB_TYPE_STATE_MANIPULATE,
                                  &RespHdr, sizeof(RespHdr), true /*fAck*/);
            break;
    }

    return rc;
}


/**
 * Tries to detect the guest OS running in the VM looking specifically for the Windows NT kind.
 *
 * @param   pThis               The KD context.
 */
static void dbgcKdCtxDetectGstOs(PKDCTX pThis)
{
    pThis->pIfWinNt = NULL;

    /* Try detecting a Windows NT guest. */
    char szName[64];
    int rc = DBGFR3OSDetect(pThis->Dbgc.pUVM, szName, sizeof(szName));
    if (RT_SUCCESS(rc))
    {
        pThis->pIfWinNt = (PDBGFOSIWINNT)DBGFR3OSQueryInterface(pThis->Dbgc.pUVM, DBGFOSINTERFACE_WINNT);
        if (pThis->pIfWinNt)
            LogRel(("DBGC/Kd: Detected Windows NT guest OS (%s)\n", &szName[0]));
        else
            LogRel(("DBGC/Kd: Detected guest OS is not of the Windows NT kind (%s)\n", &szName[0]));
    }
    else
    {
        LogRel(("DBGC/Kd: Unable to detect any guest operating system type, rc=%Rrc\n", rc));
        rc = VINF_SUCCESS; /* Try to continue nevertheless. */
    }

    if (pThis->pIfWinNt)
    {
        rc = pThis->pIfWinNt->pfnQueryVersion(pThis->pIfWinNt, pThis->Dbgc.pUVM, VMMR3GetVTable(),
                                              NULL /*puVersMajor*/, NULL /*puVersMinor*/,
                                              NULL /*puBuildNumber*/, &pThis->f32Bit);
        AssertRC(rc);
    }
    else
    {
        /*
         * Try to detect bitness based on the current CPU mode which might fool us (32bit process running
         * inside of 64bit host).
         */
        CPUMMODE enmMode = DBGCCmdHlpGetCpuMode(&pThis->Dbgc.CmdHlp);
        if (enmMode == CPUMMODE_PROTECTED)
            pThis->f32Bit = true;
        else if (enmMode == CPUMMODE_LONG)
            pThis->f32Bit = false;
        else
            LogRel(("DBGC/Kd: Heh, trying to debug real mode code with WinDbg are we? Good luck with that...\n"));
    }
}


/**
 * Processes a fully received packet.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 */
static int dbgcKdCtxPktProcess(PKDCTX pThis)
{
    int rc = VINF_SUCCESS;

    pThis->fBreakinRecv = false;

    /* Verify checksum. */
    if (dbgcKdPktChkSumGen(&pThis->abBody[0], pThis->PktHdr.Fields.cbBody) == pThis->PktHdr.Fields.u32ChkSum)
    {
        /** @todo Check packet id. */
        if (pThis->PktHdr.Fields.u16SubType != KD_PACKET_HDR_SUB_TYPE_RESET)
        {
            pThis->idPktNext = pThis->PktHdr.Fields.idPacket;
            rc = dbgcKdCtxPktSendAck(pThis);
        }
        if (RT_SUCCESS(rc))
        {
#ifdef LOG_ENABLED
            RTSGSEG Seg;
            Seg.pvSeg = &pThis->abBody[0];
            Seg.cbSeg = pThis->PktHdr.Fields.cbBody;
            dbgcKdPktDump(&pThis->PktHdr.Fields, &Seg, 1 /*cSegs*/, true /*fRx*/);
#endif

            switch (pThis->PktHdr.Fields.u16SubType)
            {
                case KD_PACKET_HDR_SUB_TYPE_RESET:
                {
                    dbgcKdCtxDetectGstOs(pThis);

                    pThis->idPktNext = 0;
                    rc = dbgcKdCtxPktSendReset(pThis);
                    if (RT_SUCCESS(rc))
                    {
                        rc = DBGFR3Halt(pThis->Dbgc.pUVM, VMCPUID_ALL);
                        if (rc == VWRN_DBGF_ALREADY_HALTED)
                            rc = dbgcKdCtxStateChangeSend(pThis, DBGFEVENT_HALT_DONE);
                    }
                    pThis->idPktNext = KD_PACKET_HDR_ID_RESET;
                    break;
                }
                case KD_PACKET_HDR_SUB_TYPE_STATE_MANIPULATE:
                {
                    pThis->idPktNext = pThis->PktHdr.Fields.idPacket ^ 0x1;
                    rc = dbgcKdCtxPktManipulate64Process(pThis);
                    break;
                }
                case KD_PACKET_HDR_SUB_TYPE_ACKNOWLEDGE:
                case KD_PACKET_HDR_SUB_TYPE_RESEND:
                {
                    /* Don't do anything. */
                    rc = VINF_SUCCESS;
                    break;
                }
                case KD_PACKET_HDR_SUB_TYPE_DEBUG_IO:
                {
                    if (pThis->fInVBoxDbg)
                    {
                        pThis->idPktNext = pThis->PktHdr.Fields.idPacket ^ 0x1;
                        /* Get the string and execute it. */
                        PCKDPACKETDEBUGIO pPktDbgIo = (PCKDPACKETDEBUGIO)&pThis->abBody[0];
                        if (   pPktDbgIo->u32Type == KD_PACKET_DEBUG_IO_GET_STRING
                            && pPktDbgIo->u.Prompt.cbReturn <= sizeof(pThis->abBody) - sizeof(*pPktDbgIo) - 1)
                        {
                            if (pPktDbgIo->u.Prompt.cbReturn)
                            {
                                /* Terminate return value. */
                                pThis->abBody[sizeof(*pPktDbgIo) + pPktDbgIo->u.Prompt.cbReturn] = '\0';

                                const char *pszCmd = (const char *)&pThis->abBody[sizeof(*pPktDbgIo)];
                                /* Filter out 'exit' which is handled here directly and exits the debug loop. */
                                if (!strcmp(pszCmd, "exit"))
                                    pThis->fInVBoxDbg = false;
                                else
                                {
                                    rc = pThis->Dbgc.CmdHlp.pfnExec(&pThis->Dbgc.CmdHlp, pszCmd);
                                    if (RT_SUCCESS(rc))
                                        rc = dbgcKdCtxDebugIoGetStrSend(pThis, pThis->Dbgc.idCpu, "VBoxDbg>", sizeof("VBoxDbg>") - 1,
                                                                        512 /*cbResponseMax*/);
                                    else
                                        LogRel(("DBGC/Kd: Executing command \"%s\" failed with rc=%Rrc\n", pszCmd, rc));
                                }
                            }
                            else
                                rc = dbgcKdCtxDebugIoGetStrSend(pThis, pThis->Dbgc.idCpu, "VBoxDbg>", sizeof("VBoxDbg>") - 1,
                                                                512 /*cbResponseMax*/);
                        }
                        else
                            LogRel(("DBGC/Kd: Received invalid DEBUG_IO packet from remote end, ignoring\n"));
                    }
                    else
                        LogRel(("DBGC/Kd: Received out of band DEBUG_IO packet from remote end, ignoring\n"));
                    break;
                }
                default:
                    rc = VERR_NOT_IMPLEMENTED;
            }
        }
    }
    else
    {
        pThis->idPktNext = pThis->PktHdr.Fields.idPacket;
        rc = dbgcKdCtxPktSendResend(pThis);
    }

    if (pThis->fBreakinRecv)
    {
        pThis->fBreakinRecv = false;
        rc = DBGFR3Halt(pThis->Dbgc.pUVM, VMCPUID_ALL);
        if (rc == VWRN_DBGF_ALREADY_HALTED)
            rc = dbgcKdCtxStateChangeSend(pThis, DBGFEVENT_HALT_DONE);
    }

    /* Next packet. */
    dbgcKdCtxPktRecvReset(pThis);
    return rc;
}


/**
 * Processes the received data based on the current state.
 *
 * @returns VBox status code.
 * @param   pThis               The KD context.
 */
static int dbgcKdCtxRecvDataProcess(PKDCTX pThis)
{
    int rc = VINF_SUCCESS;

    switch (pThis->enmState)
    {
        case KDRECVSTATE_PACKET_HDR_FIRST_BYTE:
        {
            /* Does it look like a valid packet start?. */
            if (   pThis->PktHdr.ab[0] == KD_PACKET_HDR_SIGNATURE_DATA_BYTE
                || pThis->PktHdr.ab[0] == KD_PACKET_HDR_SIGNATURE_CONTROL_BYTE)
            {
                pThis->pbRecv        = &pThis->PktHdr.ab[1];
                pThis->cbRecvLeft    = sizeof(pThis->PktHdr.ab[1]);
                pThis->enmState      = KDRECVSTATE_PACKET_HDR_SECOND_BYTE;
                pThis->msRecvTimeout = DBGC_KD_RECV_TIMEOUT_MS;
            }
            else if (pThis->PktHdr.ab[0] == KD_PACKET_HDR_SIGNATURE_BREAKIN_BYTE)
            {
                rc = DBGFR3Halt(pThis->Dbgc.pUVM, VMCPUID_ALL);
                if (rc == VWRN_DBGF_ALREADY_HALTED)
                    rc = dbgcKdCtxStateChangeSend(pThis, DBGFEVENT_HALT_DONE);
                dbgcKdCtxPktRecvReset(pThis);
            }
            else
                dbgcKdCtxPktRecvReset(pThis); /* Reset and continue. */
            break;
        }
        case KDRECVSTATE_PACKET_HDR_SECOND_BYTE:
        {
            /*
             * If the first and second byte differ there might be a single breakin
             * packet byte received and this is actually the start of a new packet.
             */
            if (pThis->PktHdr.ab[0] != pThis->PktHdr.ab[1])
            {
                if (pThis->PktHdr.ab[0] == KD_PACKET_HDR_SIGNATURE_BREAKIN_BYTE)
                {
                    /* Halt the VM and rearrange the packet receiving state machine. */
                    LogFlow(("DbgKd: Halting VM!\n"));

                    rc = DBGFR3Halt(pThis->Dbgc.pUVM, VMCPUID_ALL);
                    pThis->PktHdr.ab[0] = pThis->PktHdr.ab[1]; /* Overwrite the first byte with the new start. */
                    pThis->pbRecv       = &pThis->PktHdr.ab[1];
                    pThis->cbRecvLeft   = sizeof(pThis->PktHdr.ab[1]);
                }
                else
                    rc = VERR_NET_PROTOCOL_ERROR; /* Refuse talking to the remote end any further. */
            }
            else
            {
                /* Normal packet receive continues with the rest of the header. */
                pThis->pbRecv     = &pThis->PktHdr.ab[2];
                pThis->cbRecvLeft = sizeof(pThis->PktHdr.Fields) - 2;
                pThis->enmState   = KDRECVSTATE_PACKET_HDR;
            }
            break;
        }
        case KDRECVSTATE_PACKET_HDR:
        {
            if (   dbgcKdPktHdrValidate(&pThis->PktHdr.Fields)
                && pThis->PktHdr.Fields.cbBody <= sizeof(pThis->abBody))
            {
                /* Start receiving the body. */
                if (pThis->PktHdr.Fields.cbBody)
                {
                    pThis->pbRecv     = &pThis->abBody[0];
                    pThis->cbRecvLeft = pThis->PktHdr.Fields.cbBody;
                    pThis->enmState   = KDRECVSTATE_PACKET_BODY;
                }
                else /* No body means no trailer byte it looks like. */
                    rc = dbgcKdCtxPktProcess(pThis);
            }
            else
                rc = VERR_NET_PROTOCOL_ERROR;
            break;
        }
        case KDRECVSTATE_PACKET_BODY:
        {
            pThis->enmState   = KDRECVSTATE_PACKET_TRAILER;
            pThis->bTrailer   = 0;
            pThis->pbRecv     = &pThis->bTrailer;
            pThis->cbRecvLeft = sizeof(pThis->bTrailer);
            break;
        }
        case KDRECVSTATE_PACKET_TRAILER:
        {
            if (pThis->bTrailer == KD_PACKET_TRAILING_BYTE)
                rc = dbgcKdCtxPktProcess(pThis);
            else
                rc = VERR_NET_PROTOCOL_ERROR;
            break;
        }
        default:
            AssertMsgFailed(("Invalid receive state %d\n", pThis->enmState));
    }

    return rc;
}


/**
 * Receive data and processes complete packets.
 *
 * @returns Status code.
 * @param   pThis               The KD context.
 */
static int dbgcKdCtxRecv(PKDCTX pThis)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pThis=%p{.cbRecvLeft=%zu}\n", pThis, pThis->cbRecvLeft));

    if (pThis->cbRecvLeft)
    {
        size_t cbRead = 0;
        rc = pThis->Dbgc.pIo->pfnRead(pThis->Dbgc.pIo, pThis->pbRecv, pThis->cbRecvLeft, &cbRead);
        if (RT_SUCCESS(rc))
        {
            pThis->tsRecvLast  = RTTimeMilliTS();
            pThis->cbRecvLeft -= cbRead;
            pThis->pbRecv     += cbRead;
            if (!pThis->cbRecvLeft)
                rc = dbgcKdCtxRecvDataProcess(pThis);
        }
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}


/**
 * Processes debugger events.
 *
 * @returns VBox status code.
 * @param   pThis   The KD context data.
 * @param   pEvent  Pointer to event data.
 */
static int dbgcKdCtxProcessEvent(PKDCTX pThis, PCDBGFEVENT pEvent)
{
    /*
     * Process the event.
     */
    PDBGC pDbgc = &pThis->Dbgc;
    pThis->Dbgc.pszScratch = &pThis->Dbgc.achInput[0];
    pThis->Dbgc.iArg       = 0;
    int rc = VINF_SUCCESS;
    VMCPUID idCpuOld = pDbgc->idCpu;
    pDbgc->idCpu = pEvent->idCpu;
    switch (pEvent->enmType)
    {
        /*
         * The first part is events we have initiated with commands.
         */
        case DBGFEVENT_HALT_DONE:
        {
            rc = dbgcKdCtxStateChangeSend(pThis, pEvent->enmType);
            break;
        }

        /*
         * The second part is events which can occur at any time.
         */
        case DBGFEVENT_FATAL_ERROR:
        {
            rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbf event: Fatal error! (%s)\n",
                                         dbgcGetEventCtx(pEvent->enmCtx));
            if (RT_SUCCESS(rc))
                rc = pDbgc->CmdHlp.pfnExec(&pDbgc->CmdHlp, "r");
            break;
        }

        case DBGFEVENT_BREAKPOINT:
        case DBGFEVENT_BREAKPOINT_IO:
        case DBGFEVENT_BREAKPOINT_MMIO:
        case DBGFEVENT_BREAKPOINT_HYPER:
        {
            rc = dbgcBpExec(pDbgc, pEvent->u.Bp.hBp);
            switch (rc)
            {
                case VERR_DBGC_BP_NOT_FOUND:
                    rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event: Unknown breakpoint %u! (%s)\n",
                                                 pEvent->u.Bp.hBp, dbgcGetEventCtx(pEvent->enmCtx));
                    break;

                case VINF_DBGC_BP_NO_COMMAND:
                    rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event: Breakpoint %u! (%s)\n",
                                                 pEvent->u.Bp.hBp, dbgcGetEventCtx(pEvent->enmCtx));
                    break;

                case VINF_BUFFER_OVERFLOW:
                    rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event: Breakpoint %u! Command too long to execute! (%s)\n",
                                                 pEvent->u.Bp.hBp, dbgcGetEventCtx(pEvent->enmCtx));
                    break;

                default:
                    break;
            }
            if (RT_SUCCESS(rc) && DBGFR3IsHalted(pDbgc->pUVM, VMCPUID_ALL))
            {
                rc = pDbgc->CmdHlp.pfnExec(&pDbgc->CmdHlp, "r");

                /* Set the resume flag to ignore the breakpoint when resuming execution. */
                if (   RT_SUCCESS(rc)
                    && pEvent->enmType == DBGFEVENT_BREAKPOINT)
                    rc = pDbgc->CmdHlp.pfnExec(&pDbgc->CmdHlp, "r eflags.rf = 1");
            }

            /* Figure out the breakpoint and set the triggered flag for emulation of DR6. */
            for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aHwBp); i++)
            {
                if (pThis->aHwBp[i].hDbgfBp == pEvent->u.Bp.hBp)
                {
                    pThis->aHwBp[i].fTriggered = true;
                    break;
                }
            }

            if (RT_SUCCESS(rc))
                rc = dbgcKdCtxStateChangeSend(pThis, pEvent->enmType);
            break;
        }

        case DBGFEVENT_STEPPED:
        case DBGFEVENT_STEPPED_HYPER:
        {
            pThis->fSingleStepped = true; /* For emulation of DR6. */
            rc = dbgcKdCtxStateChangeSend(pThis, pEvent->enmType);
            break;
        }

        case DBGFEVENT_ASSERTION_HYPER:
        {
            rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                                         "\ndbgf event: Hypervisor Assertion! (%s)\n"
                                         "%s"
                                         "%s"
                                         "\n",
                                         dbgcGetEventCtx(pEvent->enmCtx),
                                         pEvent->u.Assert.pszMsg1,
                                         pEvent->u.Assert.pszMsg2);
            if (RT_SUCCESS(rc))
                rc = pDbgc->CmdHlp.pfnExec(&pDbgc->CmdHlp, "r");
            break;
        }

        case DBGFEVENT_DEV_STOP:
        {
            rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                                         "\n"
                                         "dbgf event: DBGFSTOP (%s)\n"
                                         "File:     %s\n"
                                         "Line:     %d\n"
                                         "Function: %s\n",
                                         dbgcGetEventCtx(pEvent->enmCtx),
                                         pEvent->u.Src.pszFile,
                                         pEvent->u.Src.uLine,
                                         pEvent->u.Src.pszFunction);
            if (RT_SUCCESS(rc) && pEvent->u.Src.pszMessage && *pEvent->u.Src.pszMessage)
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                                             "Message:  %s\n",
                                             pEvent->u.Src.pszMessage);
            if (RT_SUCCESS(rc))
                rc = pDbgc->CmdHlp.pfnExec(&pDbgc->CmdHlp, "r");
            break;
        }


        case DBGFEVENT_INVALID_COMMAND:
        {
            rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf/dbgc error: Invalid command event!\n");
            break;
        }

        case DBGFEVENT_POWERING_OFF:
        {
            pThis->Dbgc.fReady = false;
            pThis->Dbgc.pIo->pfnSetReady(pThis->Dbgc.pIo, false);
            rc = VERR_GENERAL_FAILURE;
            break;
        }

        default:
        {
            /*
             * Probably a generic event. Look it up to find its name.
             */
            PCDBGCSXEVT pEvtDesc = dbgcEventLookup(pEvent->enmType);
            if (pEvtDesc)
            {
                if (pEvtDesc->enmKind == kDbgcSxEventKind_Interrupt)
                {
                    Assert(pEvtDesc->pszDesc);
                    Assert(pEvent->u.Generic.cArgs == 1);
                    rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event: %s no %#llx! (%s)\n",
                                                 pEvtDesc->pszDesc, pEvent->u.Generic.auArgs[0], pEvtDesc->pszName);
                }
                else if (pEvtDesc->fFlags & DBGCSXEVT_F_BUGCHECK)
                {
                    Assert(pEvent->u.Generic.cArgs >= 5);
                    char szDetails[512];
                    DBGFR3FormatBugCheck(pDbgc->pUVM, szDetails, sizeof(szDetails), pEvent->u.Generic.auArgs[0],
                                         pEvent->u.Generic.auArgs[1], pEvent->u.Generic.auArgs[2],
                                         pEvent->u.Generic.auArgs[3], pEvent->u.Generic.auArgs[4]);
                    rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event: %s %s%s!\n%s", pEvtDesc->pszName,
                                                 pEvtDesc->pszDesc ? "- " : "", pEvtDesc->pszDesc ? pEvtDesc->pszDesc : "",
                                                 szDetails);
                }
                else if (   (pEvtDesc->fFlags & DBGCSXEVT_F_TAKE_ARG)
                         || pEvent->u.Generic.cArgs > 1
                         || (   pEvent->u.Generic.cArgs == 1
                             && pEvent->u.Generic.auArgs[0] != 0))
                {
                    if (pEvtDesc->pszDesc)
                        rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event: %s - %s!",
                                                     pEvtDesc->pszName, pEvtDesc->pszDesc);
                    else
                        rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event: %s!", pEvtDesc->pszName);
                    if (RT_SUCCESS(rc))
                    {
                        if (pEvent->u.Generic.cArgs <= 1)
                            rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, " arg=%#llx\n", pEvent->u.Generic.auArgs[0]);
                        else
                        {
                            for (uint32_t i = 0; i < pEvent->u.Generic.cArgs; i++)
                                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, " args[%u]=%#llx", i, pEvent->u.Generic.auArgs[i]);
                            if (RT_SUCCESS(rc))
                                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\n");
                        }
                    }
                }
                else
                {
                    if (pEvtDesc->pszDesc)
                        rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event: %s - %s!\n",
                                                     pEvtDesc->pszName, pEvtDesc->pszDesc);
                    else
                        rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event: %s!\n", pEvtDesc->pszName);
                }
            }
            else
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf/dbgc error: Unknown event %d!\n", pEvent->enmType);
            break;
        }
    }

    pDbgc->idCpu = idCpuOld;
    return rc;
}


/**
 * Handle a receive timeout.
 *
 * @returns VBox status code.
 * @param   pThis   Pointer to the KD context.
 */
static int dbgcKdCtxRecvTimeout(PKDCTX pThis)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pThis=%p\n", pThis));

    /*
     * If a single breakin packet byte was received but the header is otherwise incomplete
     * the VM is halted and a state change will be sent in the event processing loop.
     */
    if (   pThis->enmState == KDRECVSTATE_PACKET_HDR_SECOND_BYTE
        && pThis->PktHdr.ab[0] == KD_PACKET_HDR_SIGNATURE_BREAKIN_BYTE)
    {
        LogFlow(("DbgKd: Halting VM!\n"));
        rc = DBGFR3Halt(pThis->Dbgc.pUVM, VMCPUID_ALL);
    }
    else /* Send a reset packet */ /** @todo Figure out the semantics in this case exactly. */
        rc = dbgcKdCtxPktSendReset(pThis);

    dbgcKdCtxPktRecvReset(pThis);

    LogFlowFunc(("rc=%Rrc\n", rc));
    return rc;
}


/**
 * @copydoc DBGC::pfnOutput
 */
static DECLCALLBACK(int) dbgcKdOutput(void *pvUser, const char *pachChars, size_t cbChars)
{
    PKDCTX pThis = (PKDCTX)pvUser;

    return dbgcKdCtxDebugIoStrSend(pThis, pThis->Dbgc.idCpu, pachChars, cbChars);
}


/**
 * Run the debugger console.
 *
 * @returns VBox status code.
 * @param   pThis   Pointer to the KD context.
 */
static int dbgcKdRun(PKDCTX pThis)
{
    /*
     * We're ready for commands now.
     */
    pThis->Dbgc.fReady = true;
    pThis->Dbgc.pIo->pfnSetReady(pThis->Dbgc.pIo, true);

    /*
     * Main Debugger Loop.
     *
     * This loop will either block on waiting for input or on waiting on
     * debug events. If we're forwarding the log we cannot wait for long
     * before we must flush the log.
     */
    int rc;
    for (;;)
    {
        rc = VERR_SEM_OUT_OF_TURN;
        if (pThis->Dbgc.pUVM)
            rc = DBGFR3QueryWaitable(pThis->Dbgc.pUVM);

        if (RT_SUCCESS(rc))
        {
            /*
             * Wait for a debug event.
             */
            DBGFEVENT Evt;
            rc = DBGFR3EventWait(pThis->Dbgc.pUVM, 32, &Evt);
            if (RT_SUCCESS(rc))
            {
                rc = dbgcKdCtxProcessEvent(pThis, &Evt);
                if (RT_FAILURE(rc))
                    break;
            }
            else if (rc != VERR_TIMEOUT)
                break;

            /*
             * Check for input.
             */
            if (pThis->Dbgc.pIo->pfnInput(pThis->Dbgc.pIo, 0))
            {
                rc = dbgcKdCtxRecv(pThis);
                if (RT_FAILURE(rc))
                    break;
            }
        }
        else if (rc == VERR_SEM_OUT_OF_TURN)
        {
            /*
             * Wait for input.
             */
            if (pThis->Dbgc.pIo->pfnInput(pThis->Dbgc.pIo, 1000))
                rc = dbgcKdCtxRecv(pThis);
            else if (   pThis->msRecvTimeout != RT_INDEFINITE_WAIT
                     && (RTTimeMilliTS() - pThis->tsRecvLast >= pThis->msRecvTimeout))
                rc = dbgcKdCtxRecvTimeout(pThis);

            if (RT_FAILURE(rc))
                break;
        }
        else
            break;
    }

    return rc;
}


/**
 * Creates a KD context instance with the given backend.
 *
 * @returns VBox status code.
 * @param   ppKdCtx                 Where to store the pointer to the KD stub context instance on success.
 * @param   pIo                     Pointer to the I/O callback table.
 * @param   fFlags                  Flags controlling the behavior.
 */
static int dbgcKdCtxCreate(PPKDCTX ppKdCtx, PCDBGCIO pIo, unsigned fFlags)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pIo, VERR_INVALID_POINTER);
    AssertMsgReturn(!fFlags, ("%#x", fFlags), VERR_INVALID_PARAMETER);

    /*
     * Allocate and initialize.
     */
    PKDCTX pThis = (PKDCTX)RTMemAllocZ(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;

    dbgcInitCmdHlp(&pThis->Dbgc);
    /*
     * This is compied from the native debug console (will be used for monitor commands)
     * in DBGCConsole.cpp. Try to keep both functions in sync.
     */
    pThis->Dbgc.pIo              = pIo;
    pThis->Dbgc.pfnOutput        = dbgcKdOutput;
    pThis->Dbgc.pvOutputUser     = pThis;
    pThis->Dbgc.pVM              = NULL;
    pThis->Dbgc.pUVM             = NULL;
    pThis->Dbgc.idCpu            = 0;
    pThis->Dbgc.hDbgAs           = DBGF_AS_GLOBAL;
    pThis->Dbgc.pszEmulation     = "CodeView/WinDbg";
    pThis->Dbgc.paEmulationCmds  = &g_aCmdsCodeView[0];
    pThis->Dbgc.cEmulationCmds   = g_cCmdsCodeView;
    pThis->Dbgc.paEmulationFuncs = &g_aFuncsCodeView[0];
    pThis->Dbgc.cEmulationFuncs  = g_cFuncsCodeView;
    //pThis->Dbgc.fLog             = false;
    pThis->Dbgc.fRegTerse        = true;
    pThis->Dbgc.fStepTraceRegs   = true;
    //pThis->Dbgc.cPagingHierarchyDumps = 0;
    //pThis->Dbgc.DisasmPos        = {0};
    //pThis->Dbgc.SourcePos        = {0};
    //pThis->Dbgc.DumpPos          = {0};
    pThis->Dbgc.pLastPos          = &pThis->Dbgc.DisasmPos;
    //pThis->Dbgc.cbDumpElement    = 0;
    //pThis->Dbgc.cVars            = 0;
    //pThis->Dbgc.paVars           = NULL;
    //pThis->Dbgc.pPlugInHead      = NULL;
    //pThis->Dbgc.pFirstBp         = NULL;
    //pThis->Dbgc.abSearch         = {0};
    //pThis->Dbgc.cbSearch         = 0;
    pThis->Dbgc.cbSearchUnit       = 1;
    pThis->Dbgc.cMaxSearchHits     = 1;
    //pThis->Dbgc.SearchAddr       = {0};
    //pThis->Dbgc.cbSearchRange    = 0;

    //pThis->Dbgc.uInputZero       = 0;
    //pThis->Dbgc.iRead            = 0;
    //pThis->Dbgc.iWrite           = 0;
    //pThis->Dbgc.cInputLines      = 0;
    //pThis->Dbgc.fInputOverflow   = false;
    pThis->Dbgc.fReady           = true;
    pThis->Dbgc.pszScratch       = &pThis->Dbgc.achScratch[0];
    //pThis->Dbgc.iArg             = 0;
    //pThis->Dbgc.rcOutput         = 0;
    //pThis->Dbgc.rcCmd            = 0;

    //pThis->Dbgc.pszHistoryFile       = NULL;
    //pThis->Dbgc.pszGlobalInitScript  = NULL;
    //pThis->Dbgc.pszLocalInitScript   = NULL;

    dbgcEvalInit();

    pThis->fBreakinRecv = false;
    pThis->fInVBoxDbg   = false;
    pThis->idPktNext    = KD_PACKET_HDR_ID_INITIAL;
    pThis->pIfWinNt     = NULL;
    pThis->f32Bit       = false;
    dbgcKdCtxPktRecvReset(pThis);

    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aHwBp); i++)
    {
        PKDCTXHWBP pBp = &pThis->aHwBp[i];
        pBp->hDbgfBp = NIL_DBGFBP;
    }

    dbgcKdCtxHwBpReset(pThis);

    *ppKdCtx = pThis;
    return VINF_SUCCESS;
}


/**
 * Destroys the given KD context.
 *
 * @param   pThis                   The KD context to destroy.
 */
static void dbgcKdCtxDestroy(PKDCTX pThis)
{
    AssertPtr(pThis);

    pThis->pIfWinNt = NULL;

    /* Detach from the VM. */
    if (pThis->Dbgc.pUVM)
        DBGFR3Detach(pThis->Dbgc.pUVM);

    /* Free config strings. */
    RTStrFree(pThis->Dbgc.pszGlobalInitScript);
    pThis->Dbgc.pszGlobalInitScript = NULL;
    RTStrFree(pThis->Dbgc.pszLocalInitScript);
    pThis->Dbgc.pszLocalInitScript = NULL;
    RTStrFree(pThis->Dbgc.pszHistoryFile);
    pThis->Dbgc.pszHistoryFile = NULL;

    /* Finally, free the instance memory. */
    RTMemFree(pThis);
}


DECL_HIDDEN_CALLBACK(int) dbgcKdStubRunloop(PUVM pUVM, PCDBGCIO pIo, unsigned fFlags)
{
    /*
     * Validate input.
     */
    AssertPtrNullReturn(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = NULL;
    if (pUVM)
    {
        pVM = VMR3GetVM(pUVM);
        AssertPtrReturn(pVM, VERR_INVALID_VM_HANDLE);
    }

    /*
     * Allocate and initialize instance data
     */
    PKDCTX pThis;
    int rc = dbgcKdCtxCreate(&pThis, pIo, fFlags);
    if (RT_FAILURE(rc))
        return rc;
    if (!HMR3IsEnabled(pUVM) && !NEMR3IsEnabled(pUVM))
        pThis->Dbgc.hDbgAs = DBGF_AS_RC_AND_GC_GLOBAL;

    /*
     * Attach to the specified VM.
     */
    if (RT_SUCCESS(rc) && pUVM)
    {
        rc = DBGFR3Attach(pUVM);
        if (RT_SUCCESS(rc))
        {
            pThis->Dbgc.pVM   = pVM;
            pThis->Dbgc.pUVM  = pUVM;
            pThis->Dbgc.idCpu = 0;
        }
        else
            rc = pThis->Dbgc.CmdHlp.pfnVBoxError(&pThis->Dbgc.CmdHlp, rc, "When trying to attach to VM %p\n", pThis->Dbgc.pVM);
    }

    /*
     * Load plugins.
     */
    if (RT_SUCCESS(rc))
    {
        if (pVM)
            DBGFR3PlugInLoadAll(pThis->Dbgc.pUVM);
        dbgcEventInit(&pThis->Dbgc);

        /*
         * Run the debugger main loop.
         */
        rc = dbgcKdRun(pThis);
        dbgcEventTerm(&pThis->Dbgc);
    }

    /*
     * Cleanup console debugger session.
     */
    dbgcKdCtxDestroy(pThis);
    return rc == VERR_DBGC_QUIT ? VINF_SUCCESS : rc;
}
