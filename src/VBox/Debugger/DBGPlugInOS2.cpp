/* $Id: DBGPlugInOS2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGPlugInOS2 - Debugger and Guest OS Digger Plugin For OS/2.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DBGF /// @todo add new log group.
#include "DBGPlugIns.h"
#include <VBox/vmm/vmmr3vtable.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <iprt/string.h>
#include <iprt/mem.h>
#include <iprt/stream.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

typedef enum DBGDIGGEROS2VER
{
    DBGDIGGEROS2VER_UNKNOWN,
    DBGDIGGEROS2VER_1_x,
    DBGDIGGEROS2VER_2_x,
    DBGDIGGEROS2VER_3_0,
    DBGDIGGEROS2VER_4_0,
    DBGDIGGEROS2VER_4_5
} DBGDIGGEROS2VER;

/**
 * OS/2 guest OS digger instance data.
 */
typedef struct DBGDIGGEROS2
{
    /** The user-mode VM handle for use in info handlers. */
    PUVM                pUVM;
    /** The VMM function table for use in info handlers. */
    PCVMMR3VTABLE       pVMM;

    /** Whether the information is valid or not.
     * (For fending off illegal interface method calls.) */
    bool                fValid;
    /** 32-bit (true) or 16-bit (false) */
    bool                f32Bit;

    /** The OS/2 guest version. */
    DBGDIGGEROS2VER     enmVer;
    uint8_t             OS2MajorVersion;
    uint8_t             OS2MinorVersion;

    /** Guest's Global Info Segment selector. */
    uint16_t            selGis;
    /** The 16:16 address of the LIS. */
    RTFAR32             Lis;

    /** The kernel virtual address (excluding DOSMVDMINSTDATA & DOSSWAPINSTDATA). */
    uint32_t            uKernelAddr;
    /** The kernel size. */
    uint32_t            cbKernel;

} DBGDIGGEROS2;
/** Pointer to the OS/2 guest OS digger instance data. */
typedef DBGDIGGEROS2 *PDBGDIGGEROS2;

/**
 * 32-bit OS/2 loader module table entry (V3.0 and later).
 */
typedef struct LDRMTE
{
    uint16_t    mte_flags2;
    uint16_t    mte_handle;
    uint32_t    mte_swapmte;    /**< Pointer to LDRSMTE. */
    uint32_t    mte_link;       /**< Pointer to next LDRMTE. */
    uint32_t    mte_flags1;
    uint32_t    mte_impmodcnt;
    uint16_t    mte_sfn;
    uint16_t    mte_usecnt;
    char        mte_modname[8];
    uint32_t    mte_RAS;        /**< added later */
    uint32_t    mte_modver;     /**< added even later. */
} LDRMTE;
AssertCompileSize(LDRMTE, 40);

/**
 * 32-bit OS/2 loader module table entry (V2.11).
 */
typedef struct LDRMTE2
{
    uint16_t    mte_flags2;
    uint16_t    mte_handle;
    uint32_t    mte_swapmte;    /**< Pointer to LDRSMTE. */
    uint32_t    mte_modname;    /**< Pointer to module name. */
    uint32_t    mte_link;       /**< Pointer to next LDRMTE2. */
    uint32_t    mte_flags1;
    uint32_t    mte_impmodcnt;
    uint16_t    mte_sfn;
    uint16_t    mte_usecnt;
} LDRMTE2;
AssertCompileSize(LDRMTE2, 28);

/** @name LDRMTE::mte_flag2 values
 * @{ */
#define MTEFORMATMASK       UINT16_C(0x0003)
#define MTEFORMATR1         UINT16_C(0x0000)
#define MTEFORMATNE         UINT16_C(0x0001)
#define MTEFORMATLX         UINT16_C(0x0002)
#define MTEFORMATR2         UINT16_C(0x0003)
#define MTESYSTEMDLL        UINT16_C(0x0004)
#define MTELOADORATTACH     UINT16_C(0x0008)
#define MTECIRCLEREF        UINT16_C(0x0010)
#define MTEFREEFIXUPS       UINT16_C(0x0020) /* had different meaning earlier */
#define MTEPRELOADED        UINT16_C(0x0040)
#define MTEGETMTEDONE       UINT16_C(0x0080)
#define MTEPACKSEGDONE      UINT16_C(0x0100)
#define MTE20LIELIST        UINT16_C(0x0200)
#define MTESYSPROCESSED     UINT16_C(0x0400)
#define MTEPSDMOD           UINT16_C(0x0800)
#define MTEDLLONEXTLST      UINT16_C(0x1000)
#define MTEPDUMPCIRCREF     UINT16_C(0x2000)
/** @} */
/** @name LDRMTE::mte_flag1 values
 * @{ */
#define MTE1_NOAUTODS           UINT32_C(0x00000000)
#define MTE1_SOLO               UINT32_C(0x00000001)
#define MTE1_INSTANCEDS         UINT32_C(0x00000002)
#define MTE1_INSTLIBINIT        UINT32_C(0x00000004)
#define MTE1_GINISETUP          UINT32_C(0x00000008)
#define MTE1_NOINTERNFIXUPS     UINT32_C(0x00000010)
#define MTE1_NOEXTERNFIXUPS     UINT32_C(0x00000020)
#define MTE1_CLASS_ALL          UINT32_C(0x00000000)
#define MTE1_CLASS_PROGRAM      UINT32_C(0x00000040)
#define MTE1_CLASS_GLOBAL       UINT32_C(0x00000080)
#define MTE1_CLASS_SPECIFIC     UINT32_C(0x000000c0)
#define MTE1_CLASS_MASK         UINT32_C(0x000000c0)
#define MTE1_MTEPROCESSED       UINT32_C(0x00000100)
#define MTE1_USED               UINT32_C(0x00000200)
#define MTE1_DOSLIB             UINT32_C(0x00000400)
#define MTE1_DOSMOD             UINT32_C(0x00000800) /**< The OS/2 kernel (DOSCALLS).*/
#define MTE1_MEDIAFIXED         UINT32_C(0x00001000)
#define MTE1_LDRINVALID         UINT32_C(0x00002000)
#define MTE1_PROGRAMMOD         UINT32_C(0x00000000)
#define MTE1_DEVDRVMOD          UINT32_C(0x00004000)
#define MTE1_LIBRARYMOD         UINT32_C(0x00008000)
#define MTE1_VDDMOD             UINT32_C(0x00010000)
#define MTE1_MVDMMOD            UINT32_C(0x00020000)
#define MTE1_INGRAPH            UINT32_C(0x00040000)
#define MTE1_GINIDONE           UINT32_C(0x00080000)
#define MTE1_ADDRALLOCED        UINT32_C(0x00100000)
#define MTE1_FSDMOD             UINT32_C(0x00200000)
#define MTE1_FSHMOD             UINT32_C(0x00400000)
#define MTE1_LONGNAMES          UINT32_C(0x00800000)
#define MTE1_MEDIACONTIG        UINT32_C(0x01000000)
#define MTE1_MEDIA16M           UINT32_C(0x02000000)
#define MTE1_SWAPONLOAD         UINT32_C(0x04000000)
#define MTE1_PORTHOLE           UINT32_C(0x08000000)
#define MTE1_MODPROT            UINT32_C(0x10000000)
#define MTE1_NEWMOD             UINT32_C(0x20000000)
#define MTE1_DLLTERM            UINT32_C(0x40000000)
#define MTE1_SYMLOADED          UINT32_C(0x80000000)
/** @} */


/**
 * 32-bit OS/2 swappable module table entry.
 */
typedef struct LDRSMTE
{
    uint32_t    smte_mpages;      /**< 0x00: module page count. */
    uint32_t    smte_startobj;    /**< 0x04: Entrypoint segment number. */
    uint32_t    smte_eip;         /**< 0x08: Entrypoint offset value. */
    uint32_t    smte_stackobj;    /**< 0x0c: Stack segment number. */
    uint32_t    smte_esp;         /**< 0x10: Stack offset value*/
    uint32_t    smte_pageshift;   /**< 0x14: Page shift value. */
    uint32_t    smte_fixupsize;   /**< 0x18: Size of the fixup section. */
    uint32_t    smte_objtab;      /**< 0x1c: Pointer to LDROTE array. */
    uint32_t    smte_objcnt;      /**< 0x20: Number of segments. */
    uint32_t    smte_objmap;      /**< 0x20: Address of the object page map. */
    uint32_t    smte_itermap;     /**< 0x20: File offset of the iterated data map*/
    uint32_t    smte_rsrctab;     /**< 0x20: Pointer to resource table? */
    uint32_t    smte_rsrccnt;     /**< 0x30: Number of resource table entries. */
    uint32_t    smte_restab;      /**< 0x30: Pointer to the resident name table. */
    uint32_t    smte_enttab;      /**< 0x30: Possibly entry point table address, if not file offset. */
    uint32_t    smte_fpagetab;    /**< 0x30 */
    uint32_t    smte_frectab;     /**< 0x40 */
    uint32_t    smte_impmod;      /**< 0x44 */
    uint32_t    smte_impproc;     /**< 0x48 */
    uint32_t    smte_datapage;    /**< 0x4c */
    uint32_t    smte_nrestab;     /**< 0x50 */
    uint32_t    smte_cbnrestab;   /**< 0x54 */
    uint32_t    smte_autods;      /**< 0x58 */
    uint32_t    smte_debuginfo;   /**< 0x5c */
    uint32_t    smte_debuglen;    /**< 0x60 */
    uint32_t    smte_heapsize;    /**< 0x64 */
    uint32_t    smte_path;        /**< 0x68 Address of full name string. */
    uint16_t    smte_semcount;    /**< 0x6c */
    uint16_t    smte_semowner;    /**< 0x6e */
    uint32_t    smte_pfilecache;  /**< 0x70: Address of cached data if replace-module is used. */
    uint32_t    smte_stacksize;   /**< 0x74: Stack size for .exe thread 1. */
    uint16_t    smte_alignshift;  /**< 0x78: */
    uint16_t    smte_NEexpver;    /**< 0x7a: */
    uint16_t    smte_pathlen;     /**< 0x7c: Length of smte_path */
    uint16_t    smte_NEexetype;   /**< 0x7e: */
    uint16_t    smte_csegpack;    /**< 0x80: */
    uint8_t     smte_major_os;    /**< 0x82: added later to lie about OS version */
    uint8_t     smte_minor_os;    /**< 0x83: added later to lie about OS version */
} LDRSMTE;
AssertCompileSize(LDRSMTE, 0x84);

typedef struct LDROTE
{
    uint32_t    ote_size;
    uint32_t    ote_base;
    uint32_t    ote_flags;
    uint32_t    ote_pagemap;
    uint32_t    ote_mapsize;
    union
    {
        uint32_t ote_vddaddr;
        uint32_t ote_krnaddr;
        struct
        {
            uint16_t ote_selector;
            uint16_t ote_handle;
        } s;
    };
} LDROTE;
AssertCompileSize(LDROTE, 24);


/**
 * 32-bit system anchor block segment header.
 */
typedef struct SAS
{
    uint8_t     SAS_signature[4];
    uint16_t    SAS_tables_data;    /**< Offset to SASTABLES.  */
    uint16_t    SAS_flat_sel;       /**< 32-bit kernel DS (flat). */
    uint16_t    SAS_config_data;    /**< Offset to SASCONFIG. */
    uint16_t    SAS_dd_data;        /**< Offset to SASDD. */
    uint16_t    SAS_vm_data;        /**< Offset to SASVM. */
    uint16_t    SAS_task_data;      /**< Offset to SASTASK. */
    uint16_t    SAS_RAS_data;       /**< Offset to SASRAS. */
    uint16_t    SAS_file_data;      /**< Offset to SASFILE. */
    uint16_t    SAS_info_data;      /**< Offset to SASINFO. */
    uint16_t    SAS_mp_data;        /**< Offset to SASMP. SMP only. */
} SAS;
#define SAS_SIGNATURE "SAS "

typedef struct SASTABLES
{
    uint16_t    SAS_tbl_GDT;
    uint16_t    SAS_tbl_LDT;
    uint16_t    SAS_tbl_IDT;
    uint16_t    SAS_tbl_GDTPOOL;
} SASTABLES;

typedef struct SASCONFIG
{
    uint16_t    SAS_config_table;
} SASCONFIG;

typedef struct SASDD
{
    uint16_t    SAS_dd_bimodal_chain;
    uint16_t    SAS_dd_real_chain;
    uint16_t    SAS_dd_DPB_segment;
    uint16_t    SAS_dd_CDA_anchor_p;
    uint16_t    SAS_dd_CDA_anchor_r;
    uint16_t    SAS_dd_FSC;
} SASDD;

typedef struct SASVM
{
    uint32_t    SAS_vm_arena;
    uint32_t    SAS_vm_object;
    uint32_t    SAS_vm_context;
    uint32_t    SAS_vm_krnl_mte;    /**< Flat address of kernel MTE. */
    uint32_t    SAS_vm_glbl_mte;    /**< Flat address of global MTE list head pointer variable. */
    uint32_t    SAS_vm_pft;
    uint32_t    SAS_vm_prt;
    uint32_t    SAS_vm_swap;
    uint32_t    SAS_vm_idle_head;
    uint32_t    SAS_vm_free_head;
    uint32_t    SAS_vm_heap_info;
    uint32_t    SAS_vm_all_mte;     /**< Flat address of global MTE list head pointer variable. */
} SASVM;


#pragma pack(1)
typedef struct SASTASK
{
    uint16_t    SAS_task_PTDA;        /**< Current PTDA selector. */
    uint32_t    SAS_task_ptdaptrs;    /**< Flat address of process tree root. */
    uint32_t    SAS_task_threadptrs;  /**< Flat address array of thread pointer array. */
    uint32_t    SAS_task_tasknumber;  /**< Flat address of the TaskNumber variable. */
    uint32_t    SAS_task_threadcount; /**< Flat address of the ThreadCount variable. */
} SASTASK;
#pragma pack()


#pragma pack(1)
typedef struct SASRAS
{
    uint16_t    SAS_RAS_STDA_p;
    uint16_t    SAS_RAS_STDA_r;
    uint16_t    SAS_RAS_event_mask;
    uint32_t    SAS_RAS_Perf_Buff;
} SASRAS;
#pragma pack()

typedef struct SASFILE
{
    uint32_t    SAS_file_MFT;       /**< Handle. */
    uint16_t    SAS_file_SFT;       /**< Selector. */
    uint16_t    SAS_file_VPB;       /**< Selector. */
    uint16_t    SAS_file_CDS;       /**< Selector. */
    uint16_t    SAS_file_buffers;   /**< Selector. */
} SASFILE;

#pragma pack(1)
typedef struct SASINFO
{
    uint16_t    SAS_info_global;    /**< GIS selector. */
    uint32_t    SAS_info_local;     /**< 16:16 address of LIS for current task. */
    uint32_t    SAS_info_localRM;
    uint16_t    SAS_info_CDIB;      /**< Selector. */
} SASINFO;
#pragma pack()

typedef struct SASMP
{
    uint32_t    SAS_mp_PCBFirst;        /**< Flat address of PCB head. */
    uint32_t    SAS_mp_pLockHandles;    /**< Flat address of lock handles. */
    uint32_t    SAS_mp_cProcessors;     /**< Flat address of CPU count variable. */
    uint32_t    SAS_mp_pIPCInfo;        /**< Flat address of IPC info pointer variable. */
    uint32_t    SAS_mp_pIPCHistory;     /**< Flat address of IPC history pointer. */
    uint32_t    SAS_mp_IPCHistoryIdx;   /**< Flat address of IPC history index variable. */
    uint32_t    SAS_mp_pFirstPSA;       /**< Flat address of PSA. Added later. */
    uint32_t    SAS_mp_pPSAPages;       /**< Flat address of PSA pages. */
} SASMP;


typedef struct OS2GIS
{
    uint32_t    time;
    uint32_t    msecs;
    uint8_t     hour;
    uint8_t     minutes;
    uint8_t     seconds;
    uint8_t     hundredths;
    int16_t     timezone;
    uint16_t    cusecTimerInterval;
    uint8_t     day;
    uint8_t     month;
    uint16_t    year;
    uint8_t     weekday;
    uint8_t     uchMajorVersion;
    uint8_t     uchMinorVersion;
    uint8_t     chRevisionLetter;
    uint8_t     sgCurrent;
    uint8_t     sgMax;
    uint8_t     cHugeShift;
    uint8_t     fProtectModeOnly;
    uint16_t    pidForeground;
    uint8_t     fDynamicSched;
    uint8_t     csecMaxWait;
    uint16_t    cmsecMinSlice;
    uint16_t    cmsecMaxSlice;
    uint16_t    bootdrive;
    uint8_t     amecRAS[32];
    uint8_t     csgWindowableVioMax;
    uint8_t     csgPMMax;
    uint16_t    SIS_Syslog;
    uint16_t    SIS_MMIOBase;
    uint16_t    SIS_MMIOAddr;
    uint8_t     SIS_MaxVDMs;
    uint8_t     SIS_Reserved;
} OS2GIS;

typedef struct OS2LIS
{
    uint16_t    pidCurrent;
    uint16_t    pidParent;
    uint16_t    prtyCurrent;
    uint16_t    tidCurrent;
    uint16_t    sgCurrent;
    uint8_t     rfProcStatus;
    uint8_t     bReserved1;
    uint16_t    fForeground;
    uint8_t     typeProcess;
    uint8_t     bReserved2;
    uint16_t    selEnvironment;
    uint16_t    offCmdLine;
    uint16_t    cbDataSegment;
    uint16_t    cbStack;
    uint16_t    cbHeap;
    uint16_t    hmod;
    uint16_t    selDS;
} OS2LIS;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The 'SAS ' signature. */
#define DIG_OS2_SAS_SIG     RT_MAKE_U32_FROM_U8('S','A','S',' ')

/** OS/2Warp on little endian ASCII systems. */
#define DIG_OS2_MOD_TAG     UINT64_C(0x43532f3257617270)


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int)  dbgDiggerOS2Init(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData);


static int dbgDiggerOS2DisplaySelectorAndInfoEx(PDBGDIGGEROS2 pThis, PCDBGFINFOHLP pHlp, uint16_t uSel, uint32_t off,
                                                int cchWidth, const char *pszMessage, PDBGFSELINFO pSelInfo)
{
    RT_ZERO(*pSelInfo);
    int rc = pThis->pVMM->pfnDBGFR3SelQueryInfo(pThis->pUVM, 0 /*idCpu*/, uSel, DBGFSELQI_FLAGS_DT_GUEST, pSelInfo);
    if (RT_SUCCESS(rc))
    {
        if (off == UINT32_MAX)
            pHlp->pfnPrintf(pHlp, "%*s: %#06x (%RGv LB %#RX64 flags=%#x)\n",
                            cchWidth, pszMessage, uSel, pSelInfo->GCPtrBase, pSelInfo->cbLimit, pSelInfo->fFlags);
        else
            pHlp->pfnPrintf(pHlp, "%*s: %04x:%04x (%RGv LB %#RX64 flags=%#x)\n",
                            cchWidth, pszMessage, uSel, off, pSelInfo->GCPtrBase + off, pSelInfo->cbLimit - off, pSelInfo->fFlags);
    }
    else if (off == UINT32_MAX)
        pHlp->pfnPrintf(pHlp, "%*s: %#06x (%Rrc)\n", cchWidth, pszMessage, uSel, rc);
    else
        pHlp->pfnPrintf(pHlp, "%*s: %04x:%04x (%Rrc)\n", cchWidth, pszMessage, uSel, off, rc);
    return rc;
}

DECLINLINE(int) dbgDiggerOS2DisplaySelectorAndInfo(PDBGDIGGEROS2 pThis, PCDBGFINFOHLP pHlp, uint16_t uSel, uint32_t off,
                                                   int cchWidth, const char *pszMessage)
{
    DBGFSELINFO SelInfo;
    return dbgDiggerOS2DisplaySelectorAndInfoEx(pThis, pHlp, uSel, off, cchWidth, pszMessage, &SelInfo);
}


/**
 * @callback_method_impl{FNDBGFHANDLEREXT,
 *  Display the OS/2 system anchor segment}
 */
static DECLCALLBACK(void) dbgDiggerOS2InfoSas(void *pvUser, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pszArgs);
    PDBGDIGGEROS2 const pThis = (PDBGDIGGEROS2)pvUser;
    PUVM const          pUVM  = pThis->pUVM;
    PCVMMR3VTABLE const pVMM  = pThis->pVMM;

    DBGFSELINFO SelInfo;
    int rc = pVMM->pfnDBGFR3SelQueryInfo(pUVM, 0 /*idCpu*/, 0x70, DBGFSELQI_FLAGS_DT_GUEST, &SelInfo);
    if (RT_FAILURE(rc))
    {
        pHlp->pfnPrintf(pHlp, "DBGFR3SelQueryInfo failed on selector 0x70: %Rrc\n", rc);
        return;
    }
    pHlp->pfnPrintf(pHlp, "Selector 0x70: %RGv LB %#RX64 (flags %#x)\n",
                    SelInfo.GCPtrBase, (uint64_t)SelInfo.cbLimit, SelInfo.fFlags);

    /*
     * The SAS header.
     */
    union
    {
        SAS Sas;
        uint16_t au16Sas[sizeof(SAS) / sizeof(uint16_t)];
    };
    DBGFADDRESS Addr;
    rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, SelInfo.GCPtrBase),
                                &Sas, sizeof(Sas));
    if (RT_FAILURE(rc))
    {
        pHlp->pfnPrintf(pHlp, "Failed to read SAS header: %Rrc\n", rc);
        return;
    }
    if (memcmp(&Sas.SAS_signature[0], SAS_SIGNATURE, sizeof(Sas.SAS_signature)) != 0)
    {
        pHlp->pfnPrintf(pHlp, "Invalid SAS signature: %#x %#x %#x %#x (expected %#x %#x %#x %#x)\n",
                        Sas.SAS_signature[0], Sas.SAS_signature[1], Sas.SAS_signature[2], Sas.SAS_signature[3],
                        SAS_SIGNATURE[0], SAS_SIGNATURE[1], SAS_SIGNATURE[2], SAS_SIGNATURE[3]);
        return;
    }
    dbgDiggerOS2DisplaySelectorAndInfo(pThis, pHlp, Sas.SAS_flat_sel, UINT32_MAX, 15, "Flat kernel DS");
    pHlp->pfnPrintf(pHlp, "SAS_tables_data: %#06x (%#RGv)\n", Sas.SAS_tables_data, SelInfo.GCPtrBase + Sas.SAS_tables_data);
    pHlp->pfnPrintf(pHlp, "SAS_config_data: %#06x (%#RGv)\n", Sas.SAS_config_data, SelInfo.GCPtrBase + Sas.SAS_config_data);
    pHlp->pfnPrintf(pHlp, "    SAS_dd_data: %#06x (%#RGv)\n", Sas.SAS_dd_data,     SelInfo.GCPtrBase + Sas.SAS_dd_data);
    pHlp->pfnPrintf(pHlp, "    SAS_vm_data: %#06x (%#RGv)\n", Sas.SAS_vm_data,     SelInfo.GCPtrBase + Sas.SAS_vm_data);
    pHlp->pfnPrintf(pHlp, "  SAS_task_data: %#06x (%#RGv)\n", Sas.SAS_task_data,   SelInfo.GCPtrBase + Sas.SAS_task_data);
    pHlp->pfnPrintf(pHlp, "   SAS_RAS_data: %#06x (%#RGv)\n", Sas.SAS_RAS_data,    SelInfo.GCPtrBase + Sas.SAS_RAS_data);
    pHlp->pfnPrintf(pHlp, "  SAS_file_data: %#06x (%#RGv)\n", Sas.SAS_file_data,   SelInfo.GCPtrBase + Sas.SAS_file_data);
    pHlp->pfnPrintf(pHlp, "  SAS_info_data: %#06x (%#RGv)\n", Sas.SAS_info_data,   SelInfo.GCPtrBase + Sas.SAS_info_data);
    bool fIncludeMP = true;
    if (Sas.SAS_mp_data < sizeof(Sas))
        fIncludeMP = false;
    else
        for (unsigned i = 2; i < RT_ELEMENTS(au16Sas) - 1; i++)
            if (au16Sas[i] < sizeof(SAS))
            {
                fIncludeMP = false;
                break;
            }
    if (fIncludeMP)
        pHlp->pfnPrintf(pHlp, "    SAS_mp_data: %#06x (%#RGv)\n", Sas.SAS_mp_data, SelInfo.GCPtrBase + Sas.SAS_mp_data);

    /* shared databuf */
    union
    {
        SASINFO Info;
    } u;

    /*
     * Info data.
     */
    rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/,
                                pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, SelInfo.GCPtrBase + Sas.SAS_info_data),
                                &u.Info, sizeof(u.Info));
    if (RT_SUCCESS(rc))
    {
        pHlp->pfnPrintf(pHlp, "SASINFO:\n");
        dbgDiggerOS2DisplaySelectorAndInfo(pThis, pHlp, u.Info.SAS_info_global, UINT32_MAX, 28, "Global info segment");
        pHlp->pfnPrintf(pHlp, "%28s: %#010x\n", "Local info segment", u.Info.SAS_info_local);
        pHlp->pfnPrintf(pHlp, "%28s: %#010x\n", "Local info segment (RM)", u.Info.SAS_info_localRM);
        dbgDiggerOS2DisplaySelectorAndInfo(pThis, pHlp, u.Info.SAS_info_CDIB, UINT32_MAX, 28, "SAS_info_CDIB");
    }
    else
        pHlp->pfnPrintf(pHlp, "Failed to read SAS info data: %Rrc\n", rc);

    /** @todo more    */
}


/**
 * @callback_method_impl{FNDBGFHANDLEREXT,
 *  Display the OS/2 global info segment}
 */
static DECLCALLBACK(void) dbgDiggerOS2InfoGis(void *pvUser, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pszArgs);
    PDBGDIGGEROS2 const pThis = (PDBGDIGGEROS2)pvUser;
    PUVM const          pUVM  = pThis->pUVM;
    PCVMMR3VTABLE const pVMM  = pThis->pVMM;

    DBGFSELINFO SelInfo;
    int rc = dbgDiggerOS2DisplaySelectorAndInfoEx(pThis, pHlp, pThis->selGis, UINT32_MAX, 0, "Global info segment", &SelInfo);
    if (RT_FAILURE(rc))
        return;

    /*
     * Read the GIS.
     */
    DBGFADDRESS Addr;
    OS2GIS      Gis;
    RT_ZERO(Gis);
    rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, SelInfo.GCPtrBase), &Gis,
                                RT_MIN(sizeof(Gis), SelInfo.cbLimit + 1));
    if (RT_FAILURE(rc))
    {
        pHlp->pfnPrintf(pHlp, "Failed to read GIS: %Rrc\n", rc);
        return;
    }
    pHlp->pfnPrintf(pHlp, "               time: %#010x\n", Gis.time);
    pHlp->pfnPrintf(pHlp, "              msecs: %#010x\n", Gis.msecs);
    pHlp->pfnPrintf(pHlp, "          timestamp: %04u-%02u-%02u %02u:%02u:%02u.%02u\n",
                    Gis.year, Gis.month, Gis.day, Gis.hour, Gis.minutes, Gis.seconds, Gis.hundredths);
    pHlp->pfnPrintf(pHlp, "           timezone: %+2d (min delta)\n", (int)Gis.timezone);
    pHlp->pfnPrintf(pHlp, "            weekday: %u\n", Gis.weekday);
    pHlp->pfnPrintf(pHlp, " cusecTimerInterval: %u\n", Gis.cusecTimerInterval);
    pHlp->pfnPrintf(pHlp, "            version: %u.%u\n", Gis.uchMajorVersion, Gis.uchMinorVersion);
    pHlp->pfnPrintf(pHlp, "           revision: %#04x (%c)\n", Gis.chRevisionLetter, Gis.chRevisionLetter);
    pHlp->pfnPrintf(pHlp, " current screen grp: %#04x (%u)\n", Gis.sgCurrent, Gis.sgCurrent);
    pHlp->pfnPrintf(pHlp, "  max screen groups: %#04x (%u)\n", Gis.sgMax, Gis.sgMax);
    pHlp->pfnPrintf(pHlp, "csgWindowableVioMax: %#x (%u)\n", Gis.csgWindowableVioMax, Gis.csgWindowableVioMax);
    pHlp->pfnPrintf(pHlp, "           csgPMMax: %#x (%u)\n", Gis.csgPMMax, Gis.csgPMMax);
    pHlp->pfnPrintf(pHlp, "         cHugeShift: %#04x\n", Gis.cHugeShift);
    pHlp->pfnPrintf(pHlp, "   fProtectModeOnly: %d\n", Gis.fProtectModeOnly);
    pHlp->pfnPrintf(pHlp, "      pidForeground: %#04x (%u)\n", Gis.pidForeground, Gis.pidForeground);
    pHlp->pfnPrintf(pHlp, "      fDynamicSched: %u\n", Gis.fDynamicSched);
    pHlp->pfnPrintf(pHlp, "        csecMaxWait: %u\n", Gis.csecMaxWait);
    pHlp->pfnPrintf(pHlp, "      cmsecMinSlice: %u\n", Gis.cmsecMinSlice);
    pHlp->pfnPrintf(pHlp, "      cmsecMaxSlice: %u\n", Gis.cmsecMaxSlice);
    pHlp->pfnPrintf(pHlp, "          bootdrive: %#x\n", Gis.bootdrive);
    pHlp->pfnPrintf(pHlp, "            amecRAS: %.32Rhxs\n", &Gis.amecRAS[0]);
    pHlp->pfnPrintf(pHlp, "         SIS_Syslog: %#06x (%u)\n", Gis.SIS_Syslog, Gis.SIS_Syslog);
    pHlp->pfnPrintf(pHlp, "       SIS_MMIOBase: %#06x\n", Gis.SIS_MMIOBase);
    pHlp->pfnPrintf(pHlp, "       SIS_MMIOAddr: %#06x\n", Gis.SIS_MMIOAddr);
    pHlp->pfnPrintf(pHlp, "        SIS_MaxVDMs: %#04x (%u)\n", Gis.SIS_MaxVDMs, Gis.SIS_MaxVDMs);
    pHlp->pfnPrintf(pHlp, "       SIS_Reserved: %#04x\n", Gis.SIS_Reserved);
}


/**
 * @callback_method_impl{FNDBGFHANDLEREXT,
 *  Display the OS/2 local info segment}
 */
static DECLCALLBACK(void) dbgDiggerOS2InfoLis(void *pvUser, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pszArgs);
    PDBGDIGGEROS2 const pThis = (PDBGDIGGEROS2)pvUser;
    PUVM const          pUVM  = pThis->pUVM;
    PCVMMR3VTABLE const pVMM  = pThis->pVMM;

    DBGFSELINFO SelInfo;
    int rc = dbgDiggerOS2DisplaySelectorAndInfoEx(pThis, pHlp, pThis->Lis.sel, pThis->Lis.off, 19, "Local info segment", &SelInfo);
    if (RT_FAILURE(rc))
        return;

    /*
     * Read the LIS.
     */
    DBGFADDRESS Addr;
    OS2LIS      Lis;
    RT_ZERO(Lis);
    rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, SelInfo.GCPtrBase + pThis->Lis.off),
                                &Lis, sizeof(Lis));
    if (RT_FAILURE(rc))
    {
        pHlp->pfnPrintf(pHlp, "Failed to read LIS: %Rrc\n", rc);
        return;
    }
    pHlp->pfnPrintf(pHlp, "         pidCurrent: %#06x (%u)\n", Lis.pidCurrent, Lis.pidCurrent);
    pHlp->pfnPrintf(pHlp, "          pidParent: %#06x (%u)\n", Lis.pidParent, Lis.pidParent);
    pHlp->pfnPrintf(pHlp, "        prtyCurrent: %#06x (%u)\n", Lis.prtyCurrent, Lis.prtyCurrent);
    pHlp->pfnPrintf(pHlp, "         tidCurrent: %#06x (%u)\n", Lis.tidCurrent, Lis.tidCurrent);
    pHlp->pfnPrintf(pHlp, "          sgCurrent: %#06x (%u)\n", Lis.sgCurrent, Lis.sgCurrent);
    pHlp->pfnPrintf(pHlp, "       rfProcStatus: %#04x\n", Lis.rfProcStatus);
    if (Lis.bReserved1)
        pHlp->pfnPrintf(pHlp, "         bReserved1: %#04x\n", Lis.bReserved1);
    pHlp->pfnPrintf(pHlp, "        fForeground: %#04x (%u)\n", Lis.fForeground, Lis.fForeground);
    pHlp->pfnPrintf(pHlp, "        typeProcess: %#04x (%u)\n", Lis.typeProcess, Lis.typeProcess);
    if (Lis.bReserved2)
        pHlp->pfnPrintf(pHlp, "         bReserved2: %#04x\n", Lis.bReserved2);
    dbgDiggerOS2DisplaySelectorAndInfo(pThis, pHlp, Lis.selEnvironment, UINT32_MAX, 19, "selEnvironment");
    pHlp->pfnPrintf(pHlp, "         offCmdLine: %#06x (%u)\n", Lis.offCmdLine, Lis.offCmdLine);
    pHlp->pfnPrintf(pHlp, "      cbDataSegment: %#06x (%u)\n", Lis.cbDataSegment, Lis.cbDataSegment);
    pHlp->pfnPrintf(pHlp, "            cbStack: %#06x (%u)\n", Lis.cbStack, Lis.cbStack);
    pHlp->pfnPrintf(pHlp, "             cbHeap: %#06x (%u)\n", Lis.cbHeap, Lis.cbHeap);
    pHlp->pfnPrintf(pHlp, "               hmod: %#06x\n", Lis.hmod); /** @todo look up the name*/
    dbgDiggerOS2DisplaySelectorAndInfo(pThis, pHlp, Lis.selDS, UINT32_MAX, 19, "selDS");
}


/**
 * @callback_method_impl{FNDBGFHANDLEREXT,
 *  Display the OS/2 panic message}
 */
static DECLCALLBACK(void) dbgDiggerOS2InfoPanic(void *pvUser, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pszArgs);
    PDBGDIGGEROS2 const pThis = (PDBGDIGGEROS2)pvUser;
    PUVM const          pUVM  = pThis->pUVM;
    PCVMMR3VTABLE const pVMM  = pThis->pVMM;

    DBGFADDRESS HitAddr;
    int rc = pVMM->pfnDBGFR3MemScan(pUVM, 0 /*idCpu*/, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &HitAddr, pThis->uKernelAddr),
                                    pThis->cbKernel, 1, RT_STR_TUPLE("Exception in module:"), &HitAddr);
    if (RT_FAILURE(rc))
        rc = pVMM->pfnDBGFR3MemScan(pUVM, 0 /*idCpu&*/, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &HitAddr, pThis->uKernelAddr),
                                    pThis->cbKernel, 1, RT_STR_TUPLE("Exception in device driver:"), &HitAddr);
    /** @todo support pre-2001 kernels w/o the module/drivce name.   */
    if (RT_SUCCESS(rc))
    {
        char szMsg[728 + 1];
        RT_ZERO(szMsg);
        rc = pVMM->pfnDBGFR3MemRead(pUVM, 0, &HitAddr, szMsg, sizeof(szMsg) - 1);
        if (szMsg[0] != '\0')
        {
            RTStrPurgeEncoding(szMsg);
            char *psz = szMsg;
            while (*psz != '\0')
            {
                char *pszEol = strchr(psz, '\r');
                if (pszEol)
                    *pszEol = '\0';
                pHlp->pfnPrintf(pHlp, "%s\n", psz);
                if (!pszEol)
                    break;
                psz = ++pszEol;
                if (*psz == '\n')
                    psz++;
            }
        }
        else
            pHlp->pfnPrintf(pHlp, "DBGFR3MemRead -> %Rrc\n", rc);
    }
    else
        pHlp->pfnPrintf(pHlp, "Unable to locate OS/2 panic message. (%Rrc)\n", rc);
}



/**
 * @copydoc DBGFOSREG::pfnStackUnwindAssist
 */
static DECLCALLBACK(int) dbgDiggerOS2StackUnwindAssist(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData, VMCPUID idCpu,
                                                       PDBGFSTACKFRAME pFrame, PRTDBGUNWINDSTATE pState, PCCPUMCTX pInitialCtx,
                                                       RTDBGAS hAs, uint64_t *puScratch)
{
    RT_NOREF(pUVM, pVMM, pvData, idCpu, pFrame, pState, pInitialCtx, hAs, puScratch);
    return VINF_SUCCESS;
}


/**
 * @copydoc DBGFOSREG::pfnQueryInterface
 */
static DECLCALLBACK(void *) dbgDiggerOS2QueryInterface(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData, DBGFOSINTERFACE enmIf)
{
    RT_NOREF(pUVM, pVMM, pvData, enmIf);
    return NULL;
}


/**
 * @copydoc DBGFOSREG::pfnQueryVersion
 */
static DECLCALLBACK(int)  dbgDiggerOS2QueryVersion(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData,
                                                   char *pszVersion, size_t cchVersion)
{
    PDBGDIGGEROS2 pThis = (PDBGDIGGEROS2)pvData;
    RT_NOREF(pUVM, pVMM);
    Assert(pThis->fValid);

    char *achOS2ProductType[32];
    char *pszOS2ProductType = (char *)achOS2ProductType;

    if (pThis->OS2MajorVersion == 10)
    {
        RTStrPrintf(pszOS2ProductType, sizeof(achOS2ProductType), "OS/2 1.%02d", pThis->OS2MinorVersion);
        pThis->enmVer = DBGDIGGEROS2VER_1_x;
    }
    else if (pThis->OS2MajorVersion == 20)
    {
        if (pThis->OS2MinorVersion < 30)
        {
            RTStrPrintf(pszOS2ProductType, sizeof(achOS2ProductType), "OS/2 2.%02d", pThis->OS2MinorVersion);
            pThis->enmVer = DBGDIGGEROS2VER_2_x;
        }
        else if (pThis->OS2MinorVersion < 40)
        {
            RTStrPrintf(pszOS2ProductType, sizeof(achOS2ProductType), "OS/2 Warp");
            pThis->enmVer = DBGDIGGEROS2VER_3_0;
        }
        else if (pThis->OS2MinorVersion == 40)
        {
            RTStrPrintf(pszOS2ProductType, sizeof(achOS2ProductType), "OS/2 Warp 4");
            pThis->enmVer = DBGDIGGEROS2VER_4_0;
        }
        else
        {
            RTStrPrintf(pszOS2ProductType, sizeof(achOS2ProductType), "OS/2 Warp %d.%d",
                        pThis->OS2MinorVersion / 10, pThis->OS2MinorVersion % 10);
            pThis->enmVer = DBGDIGGEROS2VER_4_5;
        }
    }
    RTStrPrintf(pszVersion, cchVersion, "%u.%u (%s)", pThis->OS2MajorVersion, pThis->OS2MinorVersion, pszOS2ProductType);
    return VINF_SUCCESS;
}


/**
 * @copydoc DBGFOSREG::pfnTerm
 */
static DECLCALLBACK(void)  dbgDiggerOS2Term(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    PDBGDIGGEROS2 pThis = (PDBGDIGGEROS2)pvData;
    Assert(pThis->fValid);

    pVMM->pfnDBGFR3InfoDeregisterExternal(pUVM, "sas");
    pVMM->pfnDBGFR3InfoDeregisterExternal(pUVM, "gis");
    pVMM->pfnDBGFR3InfoDeregisterExternal(pUVM, "lis");
    pVMM->pfnDBGFR3InfoDeregisterExternal(pUVM, "panic");

    pThis->fValid = false;
}


/**
 * @copydoc DBGFOSREG::pfnRefresh
 */
static DECLCALLBACK(int)  dbgDiggerOS2Refresh(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    PDBGDIGGEROS2 pThis = (PDBGDIGGEROS2)pvData;
    NOREF(pThis);
    Assert(pThis->fValid);

    /*
     * For now we'll flush and reload everything.
     */
    RTDBGAS hDbgAs = pVMM->pfnDBGFR3AsResolveAndRetain(pUVM, DBGF_AS_KERNEL);
    if (hDbgAs != NIL_RTDBGAS)
    {
        uint32_t iMod = RTDbgAsModuleCount(hDbgAs);
        while (iMod-- > 0)
        {
            RTDBGMOD hMod = RTDbgAsModuleByIndex(hDbgAs, iMod);
            if (hMod != NIL_RTDBGMOD)
            {
                if (RTDbgModGetTag(hMod) == DIG_OS2_MOD_TAG)
                {
                    int rc = RTDbgAsModuleUnlink(hDbgAs, hMod);
                    AssertRC(rc);
                }
                RTDbgModRelease(hMod);
            }
        }
        RTDbgAsRelease(hDbgAs);
    }

    dbgDiggerOS2Term(pUVM, pVMM, pvData);
    return dbgDiggerOS2Init(pUVM, pVMM, pvData);
}


/** Buffer shared by dbgdiggerOS2ProcessModule and dbgDiggerOS2Init.*/
typedef union DBGDIGGEROS2BUF
{
    uint8_t             au8[0x2000];
    uint16_t            au16[0x2000/2];
    uint32_t            au32[0x2000/4];
    RTUTF16             wsz[0x2000/2];
    char                ach[0x2000];
    LDROTE              aOtes[0x2000 / sizeof(LDROTE)];
    SAS                 sas;
    SASVM               sasvm;
    LDRMTE              mte;
    LDRSMTE             smte;
    LDROTE              ote;
} DBGDIGGEROS2BUF;

/** Arguments dbgdiggerOS2ProcessModule passes to the module open callback.  */
typedef struct
{
    const char     *pszModPath;
    const char     *pszModName;
    LDRMTE const   *pMte;
    LDRSMTE const  *pSwapMte;
} DBGDIGGEROS2OPEN;


/**
 * @callback_method_impl{FNRTDBGCFGOPEN, Debug image/image searching callback.}
 */
static DECLCALLBACK(int) dbgdiggerOs2OpenModule(RTDBGCFG hDbgCfg, const char *pszFilename, void *pvUser1, void *pvUser2)
{
    DBGDIGGEROS2OPEN *pArgs = (DBGDIGGEROS2OPEN *)pvUser1;

    RTDBGMOD hDbgMod = NIL_RTDBGMOD;
    int rc = RTDbgModCreateFromImage(&hDbgMod, pszFilename, pArgs->pszModName, RTLDRARCH_WHATEVER, hDbgCfg);
    if (RT_SUCCESS(rc))
    {
        /** @todo Do some info matching before using it? */

        *(PRTDBGMOD)pvUser2 = hDbgMod;
        return VINF_CALLBACK_RETURN;
    }
    LogRel(("DbgDiggerOs2: dbgdiggerOs2OpenModule: %Rrc - %s\n", rc, pszFilename));
    return rc;
}


static void dbgdiggerOS2FixupMTE(PUVM pUVM, PCVMMR3VTABLE pVMM, PDBGDIGGEROS2 pThis, DBGDIGGEROS2BUF *pBuf)
{
    /** @todo OS/2 2.0 used a different format of SAS and likely MTE as well; not supported */
    if (pThis->OS2MajorVersion == 20 && pThis->OS2MinorVersion < 30)
    {
        /*
         * The MTE was laid out differently in OS/2 2.11, but contained the
         * same information as in 3.0. After reading the MTE from guest memory,
         * we can just shuffle things around.
         * NB: The OS/2 Debugging Handbook doesn't exactly explain that in the
         * old MTE format, mte_modname is actually a pointer to the ASCII string,
         * preceded by a length byte.
         * Must be called before dbgdiggerOS2ProcessModule().
         */
        LDRMTE2 OldMte;
        char    achNameBuf[9];

        memcpy(&OldMte, &pBuf->mte, sizeof(OldMte));

        pBuf->mte.mte_flags2    = OldMte.mte_flags2;
        pBuf->mte.mte_handle    = OldMte.mte_handle;
        pBuf->mte.mte_swapmte   = OldMte.mte_swapmte;
        pBuf->mte.mte_link      = OldMte.mte_link;
        pBuf->mte.mte_flags1    = OldMte.mte_flags1;
        pBuf->mte.mte_impmodcnt = OldMte.mte_impmodcnt;
        pBuf->mte.mte_sfn       = OldMte.mte_sfn;
        pBuf->mte.mte_usecnt    = OldMte.mte_usecnt;

        /*
         * Deal with the module name. We assume that in OS/2 V2.x the module name was in
         * practice restricted to 8 chars, just like it is in V3.0 (pretty safe assumption).
         */
        DBGFADDRESS     Addr;
        int rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, OldMte.mte_modname),
                                        &achNameBuf, sizeof(achNameBuf));
        if (RT_SUCCESS(rc))
        {
            memset(pBuf->mte.mte_modname, 0, 8);
            if((uint8_t)achNameBuf[0] <= 8)
                memcpy(pBuf->mte.mte_modname, &achNameBuf[1], achNameBuf[0]);
            else
                memcpy(pBuf->mte.mte_modname, "!NameErr", 8);
        }
        else
        {
            memcpy(pBuf->mte.mte_modname, "!RdErr", 6);
        }
    }
}


static void dbgdiggerOS2ProcessModule(PUVM pUVM, PCVMMR3VTABLE pVMM, PDBGDIGGEROS2 pThis, DBGDIGGEROS2BUF *pBuf,
                                      const char *pszCacheSubDir, RTDBGAS hAs, RTDBGCFG hDbgCfg)
{
    RT_NOREF(pThis);

    /*
     * Save the MTE.
     */
    static const char * const s_apszMteFmts[4] = { "Reserved1", "NE", "LX", "Reserved2" };
    LDRMTE const Mte = pBuf->mte;
    if ((Mte.mte_flags2 & MTEFORMATMASK) != MTEFORMATLX)
    {
        LogRel(("DbgDiggerOs2: MTE format not implemented: %s (%d)\n",
                s_apszMteFmts[(Mte.mte_flags2 & MTEFORMATMASK)], Mte.mte_flags2 & MTEFORMATMASK));
        return;
    }

    /*
     * Don't load program modules into the global address spaces.
     */
    if ((Mte.mte_flags1 & MTE1_CLASS_MASK) == MTE1_CLASS_PROGRAM)
    {
        LogRel(("DbgDiggerOs2: Program module, skipping.\n"));
        return;
    }

    /*
     * Try read the swappable MTE.  Save it too.
     */
    DBGFADDRESS     Addr;
    int rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, Mte.mte_swapmte),
                                    &pBuf->smte, sizeof(pBuf->smte));
    if (RT_FAILURE(rc))
    {
        LogRel(("DbgDiggerOs2: Error reading swap mte @ %RX32: %Rrc\n", Mte.mte_swapmte, rc));
        return;
    }
    LDRSMTE const   SwapMte = pBuf->smte;

    /* Ignore empty modules or modules with too many segments. */
    if (SwapMte.smte_objcnt == 0 || SwapMte.smte_objcnt > RT_ELEMENTS(pBuf->aOtes))
    {
        LogRel(("DbgDiggerOs2: Skipping: smte_objcnt= %#RX32\n", SwapMte.smte_objcnt));
        return;
    }

    /*
     * Try read the path name, falling back on module name.
     */
    char szModPath[260];
    rc = VERR_READ_ERROR;
    if (SwapMte.smte_path != 0 && SwapMte.smte_pathlen > 0)
    {
        uint32_t cbToRead = RT_MIN(SwapMte.smte_path, sizeof(szModPath) - 1);
        rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, SwapMte.smte_path),
                                    szModPath, cbToRead);
        szModPath[cbToRead] = '\0';
    }
    if (RT_FAILURE(rc))
    {
        memcpy(szModPath, Mte.mte_modname, sizeof(Mte.mte_modname));
        szModPath[sizeof(Mte.mte_modname)] = '\0';
        RTStrStripR(szModPath);
    }
    LogRel(("DbgDiggerOS2: szModPath='%s'\n", szModPath));

    /*
     * Sanitize the module name.
     */
    char szModName[16];
    memcpy(szModName, Mte.mte_modname, sizeof(Mte.mte_modname));
    szModName[sizeof(Mte.mte_modname)] = '\0';
    RTStrStripR(szModName);

    /*
     * Read the object table into the buffer.
     */
    rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, SwapMte.smte_objtab),
                                &pBuf->aOtes[0], sizeof(pBuf->aOtes[0]) * SwapMte.smte_objcnt);
    if (RT_FAILURE(rc))
    {
        LogRel(("DbgDiggerOs2: Error reading object table @ %#RX32 LB %#zx: %Rrc\n",
                SwapMte.smte_objtab, sizeof(pBuf->aOtes[0]) * SwapMte.smte_objcnt, rc));
        return;
    }
    for (uint32_t i = 0; i < SwapMte.smte_objcnt; i++)
    {
        LogRel(("DbgDiggerOs2:  seg%u: %RX32 LB %#x\n", i, pBuf->aOtes[i].ote_base, pBuf->aOtes[i].ote_size));
        /** @todo validate it. */
    }

    /*
     * If it is the kernel, take down the general address range so we can easily search
     * it all in one go when looking for panic messages and such.
     */
    if (Mte.mte_flags1 & MTE1_DOSMOD)
    {
        uint32_t uMax = 0;
        uint32_t uMin = UINT32_MAX;
        for (uint32_t i = 0; i < SwapMte.smte_objcnt; i++)
            if (pBuf->aOtes[i].ote_base > _512M)
            {
                if (pBuf->aOtes[i].ote_base < uMin)
                    uMin = pBuf->aOtes[i].ote_base;
                uint32_t uTmp = pBuf->aOtes[i].ote_base + pBuf->aOtes[i].ote_size;
                if (uTmp > uMax)
                    uMax = uTmp;
            }
        if (uMax != 0)
        {
            pThis->uKernelAddr = uMin;
            pThis->cbKernel    = uMax - uMin;
            LogRel(("DbgDiggerOs2: High kernel range: %#RX32 LB %#RX32 (%#RX32)\n", uMin, pThis->cbKernel, uMax));
        }
    }

    /*
     * No need to continue without an address space (shouldn't happen).
     */
    if (hAs == NIL_RTDBGAS)
        return;

    /*
     * Try find a debug file for this module.
     */
    RTDBGMOD hDbgMod = NIL_RTDBGMOD;
    if (hDbgCfg != NIL_RTDBGCFG)
    {
        DBGDIGGEROS2OPEN Args = { szModPath, szModName, &Mte, &SwapMte };
        RTDbgCfgOpenEx(hDbgCfg, szModPath, pszCacheSubDir, NULL,
                       RT_OPSYS_OS2 | RTDBGCFG_O_CASE_INSENSITIVE | RTDBGCFG_O_EXECUTABLE_IMAGE
                       | RTDBGCFG_O_RECURSIVE | RTDBGCFG_O_NO_SYSTEM_PATHS,
                       dbgdiggerOs2OpenModule, &Args, &hDbgMod);
    }

    /*
     * Fallback is a simple module into which we insert sections.
     */
    uint32_t cSegments = SwapMte.smte_objcnt;
    if (hDbgMod == NIL_RTDBGMOD)
    {
        rc = RTDbgModCreate(&hDbgMod, szModName, 0 /*cbSeg*/, 0 /*fFlags*/);
        if (RT_SUCCESS(rc))
        {
            uint32_t uRva = 0;
            for (uint32_t i = 0; i < SwapMte.smte_objcnt; i++)
            {
                char szSegNm[16];
                RTStrPrintf(szSegNm, sizeof(szSegNm), "seg%u", i);
                rc = RTDbgModSegmentAdd(hDbgMod, uRva, pBuf->aOtes[i].ote_size, szSegNm, 0 /*fFlags*/, NULL);
                if (RT_FAILURE(rc))
                {
                    LogRel(("DbgDiggerOs2: RTDbgModSegmentAdd failed (i=%u, ote_size=%#x): %Rrc\n",
                            i, pBuf->aOtes[i].ote_size, rc));
                    cSegments = i;
                    break;
                }
                uRva += RT_ALIGN_32(pBuf->aOtes[i].ote_size, _4K);
            }
        }
        else
        {
            LogRel(("DbgDiggerOs2: RTDbgModCreate failed: %Rrc\n", rc));
            return;
        }
    }

    /*
     * Tag the module and link its segments.
     */
    rc = RTDbgModSetTag(hDbgMod, DIG_OS2_MOD_TAG);
    if (RT_SUCCESS(rc))
    {
        for (uint32_t i = 0; i < cSegments; i++)
            if (pBuf->aOtes[i].ote_base != 0)
            {
                rc = RTDbgAsModuleLinkSeg(hAs, hDbgMod, i, pBuf->aOtes[i].ote_base, RTDBGASLINK_FLAGS_REPLACE /*fFlags*/);
                if (RT_FAILURE(rc))
                    LogRel(("DbgDiggerOs2: RTDbgAsModuleLinkSeg failed (i=%u, ote_base=%#x): %Rrc\n",
                            i, pBuf->aOtes[i].ote_base, rc));
            }
    }
    else
        LogRel(("DbgDiggerOs2: RTDbgModSetTag failed: %Rrc\n", rc));
    RTDbgModRelease(hDbgMod);
}


/**
 * @copydoc DBGFOSREG::pfnInit
 */
static DECLCALLBACK(int)  dbgDiggerOS2Init(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    PDBGDIGGEROS2 pThis = (PDBGDIGGEROS2)pvData;
    Assert(!pThis->fValid);

    DBGDIGGEROS2BUF uBuf;
    DBGFADDRESS     Addr;
    int             rc;

    /*
     * Determine the OS/2 version.
     */
    /* Version info is at GIS:15h (major/minor/revision). */
    rc = pVMM->pfnDBGFR3AddrFromSelOff(pUVM, 0 /*idCpu*/, &Addr, pThis->selGis, 0x15);
    if (RT_FAILURE(rc))
        return VERR_NOT_SUPPORTED;
    rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &Addr, uBuf.au32, sizeof(uint32_t));
    if (RT_FAILURE(rc))
        return VERR_NOT_SUPPORTED;

    pThis->OS2MajorVersion = uBuf.au8[0];
    pThis->OS2MinorVersion = uBuf.au8[1];

    pThis->fValid = true;

    /*
     * Try use SAS to find the module list.
     */
    rc = pVMM->pfnDBGFR3AddrFromSelOff(pUVM, 0 /*idCpu*/, &Addr, 0x70, 0x00);
    if (RT_SUCCESS(rc))
    {
        rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &Addr, &uBuf.sas, sizeof(uBuf.sas));
        if (RT_SUCCESS(rc))
        {
            rc = pVMM->pfnDBGFR3AddrFromSelOff(pUVM, 0 /*idCpu*/, &Addr, 0x70, uBuf.sas.SAS_vm_data);
            if (RT_SUCCESS(rc))
                rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &Addr, &uBuf.sasvm, sizeof(uBuf.sasvm));
            if (RT_SUCCESS(rc))
            {
                /*
                 * Work the module list.
                 */
                rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, uBuf.sasvm.SAS_vm_all_mte),
                                            &uBuf.au32[0], sizeof(uBuf.au32[0]));
                if (RT_SUCCESS(rc))
                {
                    uint32_t uOs2Krnl = UINT32_MAX;
                    RTDBGCFG hDbgCfg  = pVMM->pfnDBGFR3AsGetConfig(pUVM); /* (don't release this) */
                    RTDBGAS  hAs      = pVMM->pfnDBGFR3AsResolveAndRetain(pUVM, DBGF_AS_GLOBAL);

                    char szCacheSubDir[24];
                    RTStrPrintf(szCacheSubDir, sizeof(szCacheSubDir), "os2-%u.%u", pThis->OS2MajorVersion, pThis->OS2MinorVersion);

                    pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, uBuf.au32[0]);
                    while (Addr.FlatPtr != 0 && Addr.FlatPtr != UINT32_MAX)
                    {
                        rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &Addr, &uBuf.mte, sizeof(uBuf.mte));
                        if (RT_FAILURE(rc))
                            break;
                        dbgdiggerOS2FixupMTE(pUVM, pVMM, pThis, &uBuf);
                        LogRel(("DbgDiggerOs2: Module @ %#010RX32: %.8s %#x %#x\n", (uint32_t)Addr.FlatPtr,
                                uBuf.mte.mte_modname, uBuf.mte.mte_flags1, uBuf.mte.mte_flags2));
                        if (uBuf.mte.mte_flags1 & MTE1_DOSMOD)
                            uOs2Krnl = (uint32_t)Addr.FlatPtr;

                        pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, uBuf.mte.mte_link);
                        dbgdiggerOS2ProcessModule(pUVM, pVMM, pThis, &uBuf, szCacheSubDir, hAs, hDbgCfg);
                    }

                    /* Load the kernel again. To make sure we didn't drop any segments due
                       to overlap/conflicts/whatever.  */
                    if (uOs2Krnl != UINT32_MAX)
                    {
                        rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, uOs2Krnl),
                                                    &uBuf.mte, sizeof(uBuf.mte));
                        if (RT_SUCCESS(rc))
                        {
                            dbgdiggerOS2FixupMTE(pUVM, pVMM, pThis, &uBuf);
                            LogRel(("DbgDiggerOs2: Module @ %#010RX32: %.8s %#x %#x [again]\n", (uint32_t)Addr.FlatPtr,
                                    uBuf.mte.mte_modname, uBuf.mte.mte_flags1, uBuf.mte.mte_flags2));
                            dbgdiggerOS2ProcessModule(pUVM, pVMM, pThis, &uBuf, szCacheSubDir, hAs, hDbgCfg);
                        }
                    }

                    RTDbgAsRelease(hAs);
                }
            }
        }
    }

    /*
     * Register info handlers.
     */
    pVMM->pfnDBGFR3InfoRegisterExternal(pUVM, "sas",   "Dumps the OS/2 system anchor block (SAS).", dbgDiggerOS2InfoSas, pThis);
    pVMM->pfnDBGFR3InfoRegisterExternal(pUVM, "gis",   "Dumps the OS/2 global info segment (GIS).", dbgDiggerOS2InfoGis, pThis);
    pVMM->pfnDBGFR3InfoRegisterExternal(pUVM, "lis",   "Dumps the OS/2 local info segment (current process).", dbgDiggerOS2InfoLis, pThis);
    pVMM->pfnDBGFR3InfoRegisterExternal(pUVM, "panic", "Dumps the OS/2 system panic message.",      dbgDiggerOS2InfoPanic, pThis);

    return VINF_SUCCESS;
}


/**
 * @copydoc DBGFOSREG::pfnProbe
 */
static DECLCALLBACK(bool)  dbgDiggerOS2Probe(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    PDBGDIGGEROS2   pThis = (PDBGDIGGEROS2)pvData;
    DBGFADDRESS     Addr;
    int             rc;
    uint16_t        offInfo;
    union
    {
        uint8_t             au8[8192];
        uint16_t            au16[8192/2];
        uint32_t            au32[8192/4];
        RTUTF16             wsz[8192/2];
    } u;

    /*
     * If the DWORD at 70:0 is 'SAS ' it's quite unlikely that this wouldn't be OS/2.
     * Note: The SAS layout is similar between 16-bit and 32-bit OS/2, but not identical.
     * 32-bit OS/2 will have the flat kernel data selector at SAS:06. The selector is 168h
     * or similar. For 16-bit OS/2 the field contains a table offset into the SAS which will
     * be much smaller. Fun fact: The global infoseg selector in the SAS is bimodal in 16-bit
     * OS/2 and will work in real mode as well.
     */
    do {
        rc = pVMM->pfnDBGFR3AddrFromSelOff(pUVM, 0 /*idCpu*/, &Addr, 0x70, 0x00);
        if (RT_FAILURE(rc))
            break;
        rc = pVMM->pfnDBGFR3MemRead(pUVM, 0 /*idCpu*/, &Addr, u.au32, 256);
        if (RT_FAILURE(rc))
            break;
        if (u.au32[0] != DIG_OS2_SAS_SIG)
            break;

        /* This sure looks like OS/2, but a bit of paranoia won't hurt. */
        if (u.au16[2] >= u.au16[4])
            break;

        /* If 4th word is bigger than 5th, it's the flat kernel mode selector. */
        if (u.au16[3] > u.au16[4])
            pThis->f32Bit = true;

        /* Offset into info table is either at SAS:14h or SAS:16h. */
        if (pThis->f32Bit)
            offInfo = u.au16[0x14/2];
        else
            offInfo = u.au16[0x16/2];

        /* The global infoseg selector is the first entry in the info table. */
        SASINFO const *pInfo = (SASINFO const *)&u.au8[offInfo];
        pThis->selGis   = pInfo->SAS_info_global;
        pThis->Lis.sel  = RT_HI_U16(pInfo->SAS_info_local);
        pThis->Lis.off  = RT_LO_U16(pInfo->SAS_info_local);
        return true;
    } while (0);

    return false;
}


/**
 * @copydoc DBGFOSREG::pfnDestruct
 */
static DECLCALLBACK(void)  dbgDiggerOS2Destruct(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    RT_NOREF(pUVM, pVMM, pvData);
}


/**
 * @copydoc DBGFOSREG::pfnConstruct
 */
static DECLCALLBACK(int)  dbgDiggerOS2Construct(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    PDBGDIGGEROS2 pThis = (PDBGDIGGEROS2)pvData;
    pThis->fValid = false;
    pThis->f32Bit = false;
    pThis->enmVer = DBGDIGGEROS2VER_UNKNOWN;
    pThis->pUVM   = pUVM;
    pThis->pVMM   = pVMM;
    return VINF_SUCCESS;
}


const DBGFOSREG g_DBGDiggerOS2 =
{
    /* .u32Magic = */               DBGFOSREG_MAGIC,
    /* .fFlags = */                 0,
    /* .cbData = */                 sizeof(DBGDIGGEROS2),
    /* .szName = */                 "OS/2",
    /* .pfnConstruct = */           dbgDiggerOS2Construct,
    /* .pfnDestruct = */            dbgDiggerOS2Destruct,
    /* .pfnProbe = */               dbgDiggerOS2Probe,
    /* .pfnInit = */                dbgDiggerOS2Init,
    /* .pfnRefresh = */             dbgDiggerOS2Refresh,
    /* .pfnTerm = */                dbgDiggerOS2Term,
    /* .pfnQueryVersion = */        dbgDiggerOS2QueryVersion,
    /* .pfnQueryInterface = */      dbgDiggerOS2QueryInterface,
    /* .pfnStackUnwindAssist = */   dbgDiggerOS2StackUnwindAssist,
    /* .u32EndMagic = */            DBGFOSREG_MAGIC
};
