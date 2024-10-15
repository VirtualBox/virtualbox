/* $Id: DBGPlugInSolaris.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGPlugInSolaris - Debugger and Guest OS Digger Plugin For Solaris.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include "DBGPlugInCommonELF.h"
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/vmmr3vtable.h>
#include <iprt/asm.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/stream.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Solaris on little endian ASCII systems. */
#define DIG_SOL_MOD_TAG     UINT64_C(0x00736972616c6f53)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/** @name InternalSolaris structures
 * @{ */

/** sys/modctl.h */
typedef struct SOL32v11_modctl
{
    uint32_t    mod_next;               /**<  0 */
    uint32_t    mod_prev;               /**<  4 */
    int32_t     mod_id;                 /**<  8 */
    uint32_t    mod_mp;                 /**<  c Pointer to the kernel runtime loader bits. */
    uint32_t    mod_inprogress_thread;  /**< 10 */
    uint32_t    mod_modinfo;            /**< 14 */
    uint32_t    mod_linkage;            /**< 18 */
    uint32_t    mod_filename;           /**< 1c */
    uint32_t    mod_modname;            /**< 20 */
    int8_t      mod_busy;               /**< 24 */
    int8_t      mod_want;               /**< 25 */
    int8_t      mod_prim;               /**< 26 this is 1 for 'unix' and a few others. */
    int8_t      mod_unused_padding;     /**< 27 */
    int32_t     mod_ref;                /**< 28 */
    int8_t      mod_loaded;             /**< 2c */
    int8_t      mod_installed;          /**< 2d */
    int8_t      mod_loadflags;          /**< 2e */
    int8_t      mod_delay_unload;       /**< 2f */
    uint32_t    mod_requisites;         /**< 30 */
    uint32_t    mod___unused;           /**< 34 */
    int32_t     mod_loadcnt;            /**< 38 */
    int32_t     mod_nenabled;           /**< 3c */
    uint32_t    mod_text;               /**< 40 */
    uint32_t    mod_text_size;          /**< 44 */
    int32_t     mod_gencount;           /**< 48 */
    uint32_t    mod_requisite_loading;  /**< 4c */
} SOL32v11_modctl_t;
AssertCompileSize(SOL32v11_modctl_t, 0x50);

typedef struct SOL64v11_modctl
{
    uint64_t    mod_next;               /**<  0 */
    uint64_t    mod_prev;               /**<  8 */
    int32_t     mod_id;                 /**< 10 */
    int32_t     mod_padding0;
    uint64_t    mod_mp;                 /**< 18 Pointer to the kernel runtime loader bits. */
    uint64_t    mod_inprogress_thread;  /**< 20 */
    uint64_t    mod_modinfo;            /**< 28 */
    uint64_t    mod_linkage;            /**< 30 */
    uint64_t    mod_filename;           /**< 38 */
    uint64_t    mod_modname;            /**< 40 */
    int8_t      mod_busy;               /**< 48 */
    int8_t      mod_want;               /**< 49 */
    int8_t      mod_prim;               /**< 4a this is 1 for 'unix' and a few others. */
    int8_t      mod_unused_padding;     /**< 4b */
    int32_t     mod_ref;                /**< 4c */
    int8_t      mod_loaded;             /**< 50 */
    int8_t      mod_installed;          /**< 51 */
    int8_t      mod_loadflags;          /**< 52 */
    int8_t      mod_delay_unload;       /**< 53 */
    int32_t     mod_padding1;
    uint64_t    mod_requisites;         /**< 58 */
    uint64_t    mod___unused;           /**< 60 */
    int32_t     mod_loadcnt;            /**< 68 */
    int32_t     mod_nenabled;           /**< 6c */
    uint64_t    mod_text;               /**< 70 */
    uint64_t    mod_text_size;          /**< 78 */
    int32_t     mod_gencount;           /**< 80 */
    int32_t     mod_padding2;
    uint64_t    mod_requisite_loading;  /**< 88 */
} SOL64v11_modctl_t;
AssertCompileSize(SOL64v11_modctl_t, 0x90);

typedef struct SOL32v9_modctl
{
    uint32_t    mod_next;               /**<  0 */
    uint32_t    mod_prev;               /**<  4 */
    int32_t     mod_id;                 /**<  8 */
    uint32_t    mod_mp;                 /**<  c Pointer to the kernel runtime loader bits. */
    uint32_t    mod_inprogress_thread;  /**< 10 */
    uint32_t    mod_modinfo;            /**< 14 */
    uint32_t    mod_linkage;            /**< 18 */
    uint32_t    mod_filename;           /**< 1c */
    uint32_t    mod_modname;            /**< 20 */
    int32_t     mod_busy;               /**< 24 */
    int32_t     mod_stub;               /**< 28 DIFF 1 */
    int8_t      mod_loaded;             /**< 2c */
    int8_t      mod_installed;          /**< 2d */
    int8_t      mod_loadflags;          /**< 2e */
    int8_t      mod_want;               /**< 2f DIFF 2 */
    uint32_t    mod_requisites;         /**< 30 */
    uint32_t    mod_dependents;         /**< 34 DIFF 3 */
    int32_t     mod_loadcnt;            /**< 38 */
                                             /* DIFF 4: 4 bytes added in v11 */
    uint32_t    mod_text;               /**< 3c */
    uint32_t    mod_text_size;          /**< 40 */
                                             /* DIFF 5: 8 bytes added in v11 */
} SOL32v9_modctl_t;
AssertCompileSize(SOL32v9_modctl_t, 0x44);

typedef struct SOL64v9_modctl
{
    uint64_t    mod_next;               /**<  0 */
    uint64_t    mod_prev;               /**<  8 */
    int32_t     mod_id;                 /**< 10 */
    int32_t     mod_padding0;
    uint64_t    mod_mp;                 /**< 18 Pointer to the kernel runtime loader bits. */
    uint64_t    mod_inprogress_thread;  /**< 20 */
    uint64_t    mod_modinfo;            /**< 28 */
    uint64_t    mod_linkage;            /**< 30 */
    uint64_t    mod_filename;           /**< 38 */
    uint64_t    mod_modname;            /**< 40 */
    int32_t     mod_busy;               /**< 48 */
    int32_t     mod_stub;               /**< 4c DIFF 1 - is this a pointer? */
    int8_t      mod_loaded;             /**< 50 */
    int8_t      mod_installed;          /**< 51 */
    int8_t      mod_loadflags;          /**< 52 */
    int8_t      mod_want;               /**< 53 DIFF 2 */
    int32_t     mod_padding1;
    uint64_t    mod_requisites;         /**< 58 */
    uint64_t    mod_dependencies;       /**< 60 DIFF 3 */
    int32_t     mod_loadcnt;            /**< 68 */
    int32_t     mod_padding3;           /**< 6c DIFF 4 */
    uint64_t    mod_text;               /**< 70 */
    uint64_t    mod_text_size;          /**< 78 */
                                             /* DIFF 5: 8 bytes added in v11 */
} SOL64v9_modctl_t;
AssertCompileSize(SOL64v9_modctl_t, 0x80);

typedef union SOL_modctl
{
    SOL32v9_modctl_t    v9_32;
    SOL32v11_modctl_t   v11_32;
    SOL64v9_modctl_t    v9_64;
    SOL64v11_modctl_t   v11_64;
} SOL_modctl_t;

/** sys/kobj.h */
typedef struct SOL32_module
{
    int32_t     total_allocated;        /**<  0 */
    Elf32_Ehdr  hdr;                    /**<  4 Easy to validate */
    uint32_t    shdrs;                  /**< 38 */
    uint32_t    symhdr;                 /**< 3c */
    uint32_t    strhdr;                 /**< 40 */
    uint32_t    depends_on;             /**< 44 */
    uint32_t    symsize;                /**< 48 */
    uint32_t    symspace;               /**< 4c */
    int32_t     flags;                  /**< 50 */
    uint32_t    text_size;              /**< 54 */
    uint32_t    data_size;              /**< 58 */
    uint32_t    text;                   /**< 5c */
    uint32_t    data;                   /**< 60 */
    uint32_t    symtbl_section;         /**< 64 */
    uint32_t    symtbl;                 /**< 68 */
    uint32_t    strings;                /**< 6c */
    uint32_t    hashsize;               /**< 70 */
    uint32_t    buckets;                /**< 74 */
    uint32_t    chains;                 /**< 78 */
    uint32_t    nsyms;                  /**< 7c */
    uint32_t    bss_align;              /**< 80 */
    uint32_t    bss_size;               /**< 84 */
    uint32_t    bss;                    /**< 88 */
    uint32_t    filename;               /**< 8c */
    uint32_t    head;                   /**< 90 */
    uint32_t    tail;                   /**< 94 */
    uint32_t    destination;            /**< 98 */
    uint32_t    machdata;               /**< 9c */
    uint32_t    ctfdata;                /**< a0 */
    uint32_t    ctfsize;                /**< a4 */
    uint32_t    fbt_tab;                /**< a8 */
    uint32_t    fbt_size;               /**< ac */
    uint32_t    fbt_nentries;           /**< b0 */
    uint32_t    textwin;                /**< b4 */
    uint32_t    textwin_base;           /**< b8 */
    uint32_t    sdt_probes;             /**< bc */
    uint32_t    sdt_nprobes;            /**< c0 */
    uint32_t    sdt_tab;                /**< c4 */
    uint32_t    sdt_size;               /**< c8 */
    uint32_t    sigdata;                /**< cc */
    uint32_t    sigsize;                /**< d0 */
} SOL32_module_t;
AssertCompileSize(Elf32_Ehdr, 0x34);
AssertCompileSize(SOL32_module_t, 0xd4);

typedef struct SOL64_module
{
    int32_t     total_allocated;        /**<  0 */
    int32_t     padding0;
    Elf64_Ehdr  hdr;                    /**<  8 Easy to validate */
    uint64_t    shdrs;                  /**< 48 */
    uint64_t    symhdr;                 /**< 50 */
    uint64_t    strhdr;                 /**< 58 */
    uint64_t    depends_on;             /**< 60 */
    uint64_t    symsize;                /**< 68 */
    uint64_t    symspace;               /**< 70 */
    int32_t     flags;                  /**< 78 */
    int32_t     padding1;
    uint64_t    text_size;              /**< 80 */
    uint64_t    data_size;              /**< 88 */
    uint64_t    text;                   /**< 90 */
    uint64_t    data;                   /**< 98 */
    uint32_t    symtbl_section;         /**< a0 */
    int32_t     padding2;
    uint64_t    symtbl;                 /**< a8 */
    uint64_t    strings;                /**< b0 */
    uint32_t    hashsize;               /**< b8 */
    int32_t     padding3;
    uint64_t    buckets;                /**< c0 */
    uint64_t    chains;                 /**< c8 */
    uint32_t    nsyms;                  /**< d0 */
    uint32_t    bss_align;              /**< d4 */
    uint64_t    bss_size;               /**< d8 */
    uint64_t    bss;                    /**< e0 */
    uint64_t    filename;               /**< e8 */
    uint64_t    head;                   /**< f0 */
    uint64_t    tail;                   /**< f8 */
    uint64_t    destination;            /**< 100 */
    uint64_t    machdata;               /**< 108 */
    uint64_t    ctfdata;                /**< 110 */
    uint64_t    ctfsize;                /**< 118 */
    uint64_t    fbt_tab;                /**< 120 */
    uint64_t    fbt_size;               /**< 128 */
    uint64_t    fbt_nentries;           /**< 130 */
    uint64_t    textwin;                /**< 138 */
    uint64_t    textwin_base;           /**< 140 */
    uint64_t    sdt_probes;             /**< 148 */
    uint64_t    sdt_nprobes;            /**< 150 */
    uint64_t    sdt_tab;                /**< 158 */
    uint64_t    sdt_size;               /**< 160 */
    uint64_t    sigdata;                /**< 168 */
    uint64_t    sigsize;                /**< 170 */
} SOL64_module_t;
AssertCompileSize(Elf64_Ehdr, 0x40);
AssertCompileSize(SOL64_module_t, 0x178);

typedef struct SOL_utsname
{
    char        sysname[257];
    char        nodename[257];
    char        release[257];
    char        version[257];
    char        machine[257];
} SOL_utsname_t;
AssertCompileSize(SOL_utsname_t, 5 * 257);

/** @} */


/**
 * Solaris guest OS digger instance data.
 */
typedef struct DBGDIGGERSOLARIS
{
    /** Whether the information is valid or not.
     * (For fending off illegal interface method calls.) */
    bool fValid;

    /** Address of the 'unix' text segment.
     * This is set during probing. */
    DBGFADDRESS AddrUnixText;
    /** Address of the 'unix' text segment.
     * This is set during probing. */
    DBGFADDRESS AddrUnixData;
    /** Address of the 'unix' modctl_t (aka modules). */
    DBGFADDRESS AddrUnixModCtl;
    /** modctl_t version number. */
    int         iModCtlVer;
    /** 64-bit/32-bit indicator. */
    bool        f64Bit;

} DBGDIGGERSOLARIS;
/** Pointer to the solaris guest OS digger instance data. */
typedef DBGDIGGERSOLARIS *PDBGDIGGERSOLARIS;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Min kernel address. */
#define SOL32_MIN_KRNL_ADDR             UINT32_C(0x80000000)
/** Max kernel address.  */
#define SOL32_MAX_KRNL_ADDR             UINT32_C(0xfffff000)

/** Min kernel address.  */
#define SOL64_MIN_KRNL_ADDR             UINT64_C(0xFFFFC00000000000)
/** Max kernel address.  */
#define SOL64_MAX_KRNL_ADDR             UINT64_C(0xFFFFFFFFFFF00000)


/** Validates a 32-bit solaris kernel address */
#if 0 /* OpenSolaris, early boot have symspace at 0x27a2000 */
# define SOL32_VALID_ADDRESS(Addr)      ((Addr) > SOL32_MIN_KRNL_ADDR && (Addr) < SOL32_MAX_KRNL_ADDR)
#else
# define SOL32_VALID_ADDRESS(Addr)      (   ((Addr) > SOL32_MIN_KRNL_ADDR && (Addr) < SOL32_MAX_KRNL_ADDR) \
                                         || ((Addr) > UINT32_C(0x02000000) && (Addr) < UINT32_C(0x04000000)) /* boot */ )
#endif

/** Validates a 64-bit solaris kernel address */
#define SOL64_VALID_ADDRESS(Addr)       (   (Addr) > SOL64_MIN_KRNL_ADDR \
                                         && (Addr) < SOL64_MAX_KRNL_ADDR)

/** The max data segment size of the 'unix' module. */
#define SOL_UNIX_MAX_DATA_SEG_SIZE      0x01000000

/** The max code segment size of the 'unix' module.
 * This is the same for both 64-bit and 32-bit.  */
#define SOL_UNIX_MAX_CODE_SEG_SIZE      0x00400000


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int)  dbgDiggerSolarisInit(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData);



/**
 * @copydoc DBGFOSREG::pfnStackUnwindAssist
 */
static DECLCALLBACK(int) dbgDiggerSolarisStackUnwindAssist(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData, VMCPUID idCpu,
                                                           PDBGFSTACKFRAME pFrame, PRTDBGUNWINDSTATE pState,
                                                           PCCPUMCTX pInitialCtx, RTDBGAS hAs, uint64_t *puScratch)
{
    RT_NOREF(pUVM, pVMM, pvData, idCpu, pFrame, pState, pInitialCtx, hAs, puScratch);
    return VINF_SUCCESS;
}


/**
 * @copydoc DBGFOSREG::pfnQueryInterface
 */
static DECLCALLBACK(void *) dbgDiggerSolarisQueryInterface(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData, DBGFOSINTERFACE enmIf)
{
    RT_NOREF(pUVM, pVMM, pvData, enmIf);
    return NULL;
}


/**
 * @copydoc DBGFOSREG::pfnQueryVersion
 */
static DECLCALLBACK(int)  dbgDiggerSolarisQueryVersion(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData,
                                                       char *pszVersion, size_t cchVersion)
{
    PDBGDIGGERSOLARIS pThis = (PDBGDIGGERSOLARIS)pvData;
    Assert(pThis->fValid);

    /*
     * It's all in the utsname symbol...
     */
    SOL_utsname_t UtsName;
    RT_ZERO(UtsName);                   /* Make MSC happy. */
    DBGFADDRESS Addr;
    RTDBGSYMBOL SymUtsName;
    int rc = pVMM->pfnDBGFR3AsSymbolByName(pUVM, DBGF_AS_KERNEL, "utsname", &SymUtsName, NULL);
    if (RT_SUCCESS(rc))
        rc = pVMM->pfnDBGFR3MemRead(pUVM, 0, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, SymUtsName.Value),
                                    &UtsName, sizeof(UtsName));
    if (RT_FAILURE(rc))
    {
        /*
         * Try searching by the name...
         */
        memset(&UtsName, '\0', sizeof(UtsName));
        strcpy(&UtsName.sysname[0], "SunOS");
        rc = pVMM->pfnDBGFR3MemScan(pUVM, 0, &pThis->AddrUnixData, SOL_UNIX_MAX_DATA_SEG_SIZE, 1,
                                    &UtsName.sysname[0], sizeof(UtsName.sysname), &Addr);
        if (RT_SUCCESS(rc))
            rc = pVMM->pfnDBGFR3MemRead(pUVM, 0, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr,
                                                                             Addr.FlatPtr - RT_OFFSETOF(SOL_utsname_t, sysname)),
                                        &UtsName, sizeof(UtsName));
    }

    /*
     * Copy out the result (if any).
     */
    if (RT_SUCCESS(rc))
    {
        if (    UtsName.sysname[sizeof(UtsName.sysname) - 1] != '\0'
            ||  UtsName.nodename[sizeof(UtsName.nodename) - 1] != '\0'
            ||  UtsName.release[sizeof(UtsName.release) - 1] != '\0'
            ||  UtsName.version[sizeof(UtsName.version) - 1] != '\0'
            ||  UtsName.machine[sizeof(UtsName.machine) - 1] != '\0')
        {
            //rc = VERR_DBGF_UNEXPECTED_OS_DATA;
            rc = VERR_GENERAL_FAILURE;
            RTStrPrintf(pszVersion, cchVersion, "failed - bogus utsname");
        }
        else
            RTStrPrintf(pszVersion, cchVersion, "%s %s", UtsName.version, UtsName.release);
    }
    else
        RTStrPrintf(pszVersion, cchVersion, "failed - %Rrc", rc);

    return rc;
}



/**
 * Processes a modctl_t.
 *
 * @param   pUVM    The user mode VM handle.
 * @param   pVMM    The VMM function table.
 * @param   pThis   Our instance data.
 * @param   pModCtl Pointer to the modctl structure.
 */
static void dbgDiggerSolarisProcessModCtl32(PUVM pUVM, PCVMMR3VTABLE pVMM, PDBGDIGGERSOLARIS pThis, SOL_modctl_t const *pModCtl)
{
    RT_NOREF1(pThis);

    /* skip it if it's not loaded and installed */
    AssertCompile2MemberOffsets(SOL_modctl_t, v11_32.mod_loaded,    v9_32.mod_loaded);
    AssertCompile2MemberOffsets(SOL_modctl_t, v11_32.mod_installed, v9_32.mod_installed);
    AssertCompile2MemberOffsets(SOL_modctl_t, v11_32.mod_id,        v9_32.mod_id);
    if (    (   !pModCtl->v9_32.mod_loaded
             || !pModCtl->v9_32.mod_installed)
        &&  pModCtl->v9_32.mod_id > 3)
        return;

    /*
     * Read the module and file names first
     */
    AssertCompile2MemberOffsets(SOL_modctl_t, v11_32.mod_modname, v9_32.mod_modname);
    char szModName[64];
    DBGFADDRESS Addr;
    int rc = pVMM->pfnDBGFR3MemReadString(pUVM, 0, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, pModCtl->v9_32.mod_modname),
                                          szModName, sizeof(szModName));
    if (RT_FAILURE(rc))
        return;
    if (!RTStrEnd(szModName, sizeof(szModName)))
        szModName[sizeof(szModName) - 1] = '\0';

    AssertCompile2MemberOffsets(SOL_modctl_t, v11_32.mod_filename, v9_32.mod_filename);
    char szFilename[256];
    rc = pVMM->pfnDBGFR3MemReadString(pUVM, 0, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, pModCtl->v9_32.mod_filename),
                                      szFilename, sizeof(szFilename));
    if (RT_FAILURE(rc))
        strcpy(szFilename, szModName);
    else if (!RTStrEnd(szFilename, sizeof(szFilename)))
        szFilename[sizeof(szFilename) - 1] = '\0';

    /*
     * Then read the module struct and validate it.
     */
    AssertCompile2MemberOffsets(SOL_modctl_t, v11_32.mod_mp, v9_32.mod_mp);
    struct SOL32_module Module;
    rc = pVMM->pfnDBGFR3MemRead(pUVM, 0, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, pModCtl->v9_32.mod_mp), &Module, sizeof(Module));
    if (RT_FAILURE(rc))
        return;

    /* Basic validations of the elf header. */
    if (    Module.hdr.e_ident[EI_MAG0] != ELFMAG0
        ||  Module.hdr.e_ident[EI_MAG1] != ELFMAG1
        ||  Module.hdr.e_ident[EI_MAG2] != ELFMAG2
        ||  Module.hdr.e_ident[EI_MAG3] != ELFMAG3
        ||  Module.hdr.e_ident[EI_CLASS] != ELFCLASS32
        ||  Module.hdr.e_ident[EI_DATA] != ELFDATA2LSB
        ||  Module.hdr.e_ident[EI_VERSION] != EV_CURRENT
        ||  !ASMMemIsZero(&Module.hdr.e_ident[EI_PAD], EI_NIDENT - EI_PAD)
        )
        return;
    if (Module.hdr.e_version != EV_CURRENT)
        return;
    if (Module.hdr.e_ehsize != sizeof(Module.hdr))
        return;
    if (    Module.hdr.e_type != ET_DYN
        &&  Module.hdr.e_type != ET_REL
        &&  Module.hdr.e_type != ET_EXEC) //??
        return;
    if (    Module.hdr.e_machine != EM_386
        &&  Module.hdr.e_machine != EM_486)
        return;
    if (    Module.hdr.e_phentsize != sizeof(Elf32_Phdr)
        &&  Module.hdr.e_phentsize) //??
        return;
    if (Module.hdr.e_shentsize != sizeof(Elf32_Shdr))
        return;

    /* Basic validations of the rest of the stuff. */
    if (    !SOL32_VALID_ADDRESS(Module.shdrs)
        ||  !SOL32_VALID_ADDRESS(Module.symhdr)
        ||  !SOL32_VALID_ADDRESS(Module.strhdr)
        ||  (!SOL32_VALID_ADDRESS(Module.symspace) && Module.symspace)
        ||  !SOL32_VALID_ADDRESS(Module.text)
        ||  !SOL32_VALID_ADDRESS(Module.data)
        ||  (!SOL32_VALID_ADDRESS(Module.symtbl) && Module.symtbl)
        ||  (!SOL32_VALID_ADDRESS(Module.strings) && Module.strings)
        ||  (!SOL32_VALID_ADDRESS(Module.head) && Module.head)
        ||  (!SOL32_VALID_ADDRESS(Module.tail) && Module.tail)
        ||  !SOL32_VALID_ADDRESS(Module.filename))
        return;
    if (    Module.symsize > _4M
        ||  Module.hdr.e_shnum > 4096
        ||  Module.nsyms > _256K)
        return;

    /* Ignore modules without symbols. */
    if (!Module.symtbl || !Module.strings || !Module.symspace || !Module.symsize)
        return;

    /* Check that the symtbl and strings points inside the symspace. */
    if (Module.strings - Module.symspace >= Module.symsize)
        return;
    if (Module.symtbl - Module.symspace >= Module.symsize)
        return;

    /*
     * Read the section headers, symbol table and string tables.
     */
    size_t cb = Module.hdr.e_shnum * sizeof(Elf32_Shdr);
    Elf32_Shdr *paShdrs = (Elf32_Shdr *)RTMemTmpAlloc(cb);
    if (!paShdrs)
        return;
    rc = pVMM->pfnDBGFR3MemRead(pUVM, 0, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, Module.shdrs), paShdrs, cb);
    if (RT_SUCCESS(rc))
    {
        void *pvSymSpace = RTMemTmpAlloc(Module.symsize + 1);
        if (pvSymSpace)
        {
            rc = pVMM->pfnDBGFR3MemRead(pUVM, 0, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, Module.symspace),
                                        pvSymSpace, Module.symsize);
            if (RT_SUCCESS(rc))
            {
                ((uint8_t *)pvSymSpace)[Module.symsize] = 0;

                /*
                 * Hand it over to the common ELF32 module parser.
                 */
                char const *pbStrings = (char const *)pvSymSpace + (Module.strings - Module.symspace);
                size_t cbMaxStrings = Module.symsize - (Module.strings - Module.symspace);

                Elf32_Sym const *paSyms = (Elf32_Sym const *)((uintptr_t)pvSymSpace + (Module.symtbl - Module.symspace));
                size_t cMaxSyms = (Module.symsize - (Module.symtbl - Module.symspace)) / sizeof(Elf32_Sym);
                cMaxSyms = RT_MIN(cMaxSyms, Module.nsyms);

                DBGDiggerCommonParseElf32Mod(pUVM, pVMM, szModName, szFilename, DBG_DIGGER_ELF_FUNNY_SHDRS,
                                             &Module.hdr, paShdrs, paSyms, cMaxSyms, pbStrings, cbMaxStrings,
                                             SOL32_MIN_KRNL_ADDR, SOL32_MAX_KRNL_ADDR - 1, DIG_SOL_MOD_TAG);
            }
            RTMemTmpFree(pvSymSpace);
        }
    }

    RTMemTmpFree(paShdrs);
    return;
}


/**
 * Processes a modctl_t.
 *
 * @param   pUVM    The user mode VM handle.
 * @param   pVMM    The VMM function table.
 * @param   pThis   Our instance data.
 * @param   pModCtl Pointer to the modctl structure.
 */
static void dbgDiggerSolarisProcessModCtl64(PUVM pUVM, PCVMMR3VTABLE pVMM, PDBGDIGGERSOLARIS pThis, SOL_modctl_t const *pModCtl)
{
    RT_NOREF1(pThis);

    /* skip it if it's not loaded and installed */
    AssertCompile2MemberOffsets(SOL_modctl_t, v11_64.mod_loaded,    v9_64.mod_loaded);
    AssertCompile2MemberOffsets(SOL_modctl_t, v11_64.mod_installed, v9_64.mod_installed);
    AssertCompile2MemberOffsets(SOL_modctl_t, v11_64.mod_id,        v9_64.mod_id);
    if (    (   !pModCtl->v9_64.mod_loaded
             || !pModCtl->v9_64.mod_installed)
        &&  pModCtl->v9_64.mod_id > 3)
        return;

    /*
     * Read the module and file names first
     */
    AssertCompile2MemberOffsets(SOL_modctl_t, v11_64.mod_modname, v9_64.mod_modname);
    char szModName[64];
    DBGFADDRESS Addr;
    int rc = pVMM->pfnDBGFR3MemReadString(pUVM, 0, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, pModCtl->v9_64.mod_modname),
                                          szModName, sizeof(szModName));
    if (RT_FAILURE(rc))
        return;
    if (!RTStrEnd(szModName, sizeof(szModName)))
        szModName[sizeof(szModName) - 1] = '\0';

    AssertCompile2MemberOffsets(SOL_modctl_t, v11_64.mod_filename, v9_64.mod_filename);
    char szFilename[256];
    rc = pVMM->pfnDBGFR3MemReadString(pUVM, 0, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, pModCtl->v9_64.mod_filename),
                                      szFilename, sizeof(szFilename));
    if (RT_FAILURE(rc))
        strcpy(szFilename, szModName);
    else if (!RTStrEnd(szFilename, sizeof(szFilename)))
        szFilename[sizeof(szFilename) - 1] = '\0';

    /*
     * Then read the module struct and validate it.
     */
    AssertCompile2MemberOffsets(SOL_modctl_t, v11_64.mod_mp, v9_64.mod_mp);
    struct SOL64_module Module;
    rc = pVMM->pfnDBGFR3MemRead(pUVM, 0, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, pModCtl->v9_64.mod_mp), &Module, sizeof(Module));
    if (RT_FAILURE(rc))
        return;

    /* Basic validations of the elf header. */
    if (    Module.hdr.e_ident[EI_MAG0] != ELFMAG0
        ||  Module.hdr.e_ident[EI_MAG1] != ELFMAG1
        ||  Module.hdr.e_ident[EI_MAG2] != ELFMAG2
        ||  Module.hdr.e_ident[EI_MAG3] != ELFMAG3
        ||  Module.hdr.e_ident[EI_CLASS] != ELFCLASS64
        ||  Module.hdr.e_ident[EI_DATA] != ELFDATA2LSB
        ||  Module.hdr.e_ident[EI_VERSION] != EV_CURRENT
        ||  !ASMMemIsZero(&Module.hdr.e_ident[EI_PAD], EI_NIDENT - EI_PAD)
        )
        return;
    if (Module.hdr.e_version != EV_CURRENT)
        return;
    if (Module.hdr.e_ehsize != sizeof(Module.hdr))
        return;
    if (    Module.hdr.e_type != ET_DYN
        &&  Module.hdr.e_type != ET_REL
        &&  Module.hdr.e_type != ET_EXEC) //??
        return;
    if (Module.hdr.e_machine != EM_X86_64)
        return;
    if (    Module.hdr.e_phentsize != sizeof(Elf64_Phdr)
        &&  Module.hdr.e_phentsize) //??
        return;
    if (Module.hdr.e_shentsize != sizeof(Elf64_Shdr))
        return;

    /* Basic validations of the rest of the stuff. */
    if (    !SOL64_VALID_ADDRESS(Module.shdrs)
        ||  !SOL64_VALID_ADDRESS(Module.symhdr)
        ||  !SOL64_VALID_ADDRESS(Module.strhdr)
        ||  (!SOL64_VALID_ADDRESS(Module.symspace) && Module.symspace)
        ||  !SOL64_VALID_ADDRESS(Module.text)
        ||  !SOL64_VALID_ADDRESS(Module.data)
        ||  (!SOL64_VALID_ADDRESS(Module.symtbl) && Module.symtbl)
        ||  (!SOL64_VALID_ADDRESS(Module.strings) && Module.strings)
        ||  (!SOL64_VALID_ADDRESS(Module.head) && Module.head)
        ||  (!SOL64_VALID_ADDRESS(Module.tail) && Module.tail)
        ||  !SOL64_VALID_ADDRESS(Module.filename))
        return;
    if (    Module.symsize > _4M
        ||  Module.hdr.e_shnum > 4096
        ||  Module.nsyms > _256K)
        return;

    /* Ignore modules without symbols. */
    if (!Module.symtbl || !Module.strings || !Module.symspace || !Module.symsize)
        return;

    /* Check that the symtbl and strings points inside the symspace. */
    if (Module.strings - Module.symspace >= Module.symsize)
        return;
    if (Module.symtbl - Module.symspace >= Module.symsize)
        return;

    /*
     * Read the section headers, symbol table and string tables.
     */
    size_t cb = Module.hdr.e_shnum * sizeof(Elf64_Shdr);
    Elf64_Shdr *paShdrs = (Elf64_Shdr *)RTMemTmpAlloc(cb);
    if (!paShdrs)
        return;
    rc = pVMM->pfnDBGFR3MemRead(pUVM, 0, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, Module.shdrs), paShdrs, cb);
    if (RT_SUCCESS(rc))
    {
        void *pvSymSpace = RTMemTmpAlloc(Module.symsize + 1);
        if (pvSymSpace)
        {
            rc = pVMM->pfnDBGFR3MemRead(pUVM, 0, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, Module.symspace),
                                        pvSymSpace, Module.symsize);
            if (RT_SUCCESS(rc))
            {
                ((uint8_t *)pvSymSpace)[Module.symsize] = 0;

                /*
                 * Hand it over to the common ELF64 module parser.
                 */
                char const *pbStrings = (char const *)pvSymSpace + (Module.strings - Module.symspace);
                size_t cbMaxStrings = Module.symsize - (Module.strings - Module.symspace);

                Elf64_Sym const *paSyms = (Elf64_Sym const *)((uintptr_t)pvSymSpace + (uintptr_t)(Module.symtbl - Module.symspace));
                size_t cMaxSyms = (Module.symsize - (Module.symtbl - Module.symspace)) / sizeof(Elf32_Sym);
                cMaxSyms = RT_MIN(cMaxSyms, Module.nsyms);

                DBGDiggerCommonParseElf64Mod(pUVM, pVMM, szModName, szFilename, DBG_DIGGER_ELF_FUNNY_SHDRS,
                                             &Module.hdr, paShdrs, paSyms, cMaxSyms, pbStrings, cbMaxStrings,
                                             SOL64_MIN_KRNL_ADDR, SOL64_MAX_KRNL_ADDR - 1, DIG_SOL_MOD_TAG);
            }
            RTMemTmpFree(pvSymSpace);
        }
    }

    RTMemTmpFree(paShdrs);
    return;
}


/**
 * @copydoc DBGFOSREG::pfnTerm
 */
static DECLCALLBACK(void)  dbgDiggerSolarisTerm(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    PDBGDIGGERSOLARIS pThis = (PDBGDIGGERSOLARIS)pvData;
    RT_NOREF(pUVM, pVMM);
    Assert(pThis->fValid);

    pThis->fValid = false;
}


/**
 * @copydoc DBGFOSREG::pfnRefresh
 */
static DECLCALLBACK(int)  dbgDiggerSolarisRefresh(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    PDBGDIGGERSOLARIS pThis = (PDBGDIGGERSOLARIS)pvData;
    RT_NOREF(pThis);
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
                if (RTDbgModGetTag(hMod) == DIG_SOL_MOD_TAG)
                {
                    int rc = RTDbgAsModuleUnlink(hDbgAs, hMod);
                    AssertRC(rc);
                }
                RTDbgModRelease(hMod);
            }
        }
        RTDbgAsRelease(hDbgAs);
    }

    dbgDiggerSolarisTerm(pUVM, pVMM, pvData);
    return dbgDiggerSolarisInit(pUVM, pVMM, pvData);
}


/**
 * @copydoc DBGFOSREG::pfnInit
 */
static DECLCALLBACK(int)  dbgDiggerSolarisInit(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    PDBGDIGGERSOLARIS pThis = (PDBGDIGGERSOLARIS)pvData;
    Assert(!pThis->fValid);
    int rc;
    size_t cbModCtl = 0;

    /*
     * On Solaris the kernel and is the global address space.
     */
    pVMM->pfnDBGFR3AsSetAlias(pUVM, DBGF_AS_KERNEL, DBGF_AS_GLOBAL);

/** @todo Use debug_info, build 7x / S10U6. */

    /*
     * Find the 'unix' modctl_t structure (aka modules).
     * We know it resides in the unix data segment.
     */
    pVMM->pfnDBGFR3AddrFromFlat(pUVM, &pThis->AddrUnixModCtl, 0);

    DBGFADDRESS     CurAddr = pThis->AddrUnixData;
    DBGFADDRESS     MaxAddr;
    pVMM->pfnDBGFR3AddrFromFlat(pUVM, &MaxAddr, CurAddr.FlatPtr + SOL_UNIX_MAX_DATA_SEG_SIZE);
    const uint8_t  *pbExpr = (const uint8_t *)&pThis->AddrUnixText.FlatPtr;
    const uint32_t  cbExpr = pThis->f64Bit ? sizeof(uint64_t) : sizeof(uint32_t);
    while (   CurAddr.FlatPtr < MaxAddr.FlatPtr
           && CurAddr.FlatPtr >= pThis->AddrUnixData.FlatPtr)
    {
        DBGFADDRESS HitAddr;
        rc = pVMM->pfnDBGFR3MemScan(pUVM, 0, &CurAddr, MaxAddr.FlatPtr - CurAddr.FlatPtr, 1, pbExpr, cbExpr, &HitAddr);
        if (RT_FAILURE(rc))
            break;

        /*
         * Read out the modctl_t structure.
         */
        DBGFADDRESS ModCtlAddr;

        /* v11 */
        if (pThis->f64Bit)
        {
            pVMM->pfnDBGFR3AddrFromFlat(pUVM, &ModCtlAddr, HitAddr.FlatPtr - RT_OFFSETOF(SOL32v11_modctl_t, mod_text));
            SOL64v11_modctl_t ModCtlv11;
            rc = pVMM->pfnDBGFR3MemRead(pUVM, 0, &ModCtlAddr, &ModCtlv11, sizeof(ModCtlv11));
            if (RT_SUCCESS(rc))
            {
                if (    SOL64_VALID_ADDRESS(ModCtlv11.mod_next)
                    &&  SOL64_VALID_ADDRESS(ModCtlv11.mod_prev)
                    &&  ModCtlv11.mod_id == 0
                    &&  SOL64_VALID_ADDRESS(ModCtlv11.mod_mp)
                    &&  SOL64_VALID_ADDRESS(ModCtlv11.mod_filename)
                    &&  SOL64_VALID_ADDRESS(ModCtlv11.mod_modname)
                    &&  ModCtlv11.mod_prim == 1
                    &&  ModCtlv11.mod_loaded == 1
                    &&  ModCtlv11.mod_installed == 1
                    &&  ModCtlv11.mod_requisites == 0
                    &&  ModCtlv11.mod_loadcnt == 1
                    /*&&  ModCtlv11.mod_text == pThis->AddrUnixText.FlatPtr*/
                    &&  ModCtlv11.mod_text_size < SOL_UNIX_MAX_CODE_SEG_SIZE
                    &&  ModCtlv11.mod_text_size >= _128K)
                {
                    char szUnix[5];
                    DBGFADDRESS NameAddr;
                    pVMM->pfnDBGFR3AddrFromFlat(pUVM, &NameAddr, ModCtlv11.mod_modname);
                    rc = pVMM->pfnDBGFR3MemRead(pUVM, 0, &NameAddr, &szUnix, sizeof(szUnix));
                    if (RT_SUCCESS(rc))
                    {
                        if (!strcmp(szUnix, "unix"))
                        {
                            pThis->AddrUnixModCtl = ModCtlAddr;
                            pThis->iModCtlVer = 11;
                            cbModCtl = sizeof(ModCtlv11);
                            break;
                        }
                        Log(("sol64 mod_name=%.*s v11\n", sizeof(szUnix), szUnix));
                    }
                }
            }
        }
        else
        {
            pVMM->pfnDBGFR3AddrFromFlat(pUVM, &ModCtlAddr, HitAddr.FlatPtr - RT_OFFSETOF(SOL32v11_modctl_t, mod_text));
            SOL32v11_modctl_t ModCtlv11;
            rc = pVMM->pfnDBGFR3MemRead(pUVM, 0, &ModCtlAddr, &ModCtlv11, sizeof(ModCtlv11));
            if (RT_SUCCESS(rc))
            {
                if (    SOL32_VALID_ADDRESS(ModCtlv11.mod_next)
                    &&  SOL32_VALID_ADDRESS(ModCtlv11.mod_prev)
                    &&  ModCtlv11.mod_id == 0
                    &&  SOL32_VALID_ADDRESS(ModCtlv11.mod_mp)
                    &&  SOL32_VALID_ADDRESS(ModCtlv11.mod_filename)
                    &&  SOL32_VALID_ADDRESS(ModCtlv11.mod_modname)
                    &&  ModCtlv11.mod_prim == 1
                    &&  ModCtlv11.mod_loaded == 1
                    &&  ModCtlv11.mod_installed == 1
                    &&  ModCtlv11.mod_requisites == 0
                    &&  ModCtlv11.mod_loadcnt == 1
                    /*&&  ModCtlv11.mod_text == pThis->AddrUnixText.FlatPtr*/
                    &&  ModCtlv11.mod_text_size < SOL_UNIX_MAX_CODE_SEG_SIZE
                    &&  ModCtlv11.mod_text_size >= _128K)
                {
                    char szUnix[5];
                    DBGFADDRESS NameAddr;
                    pVMM->pfnDBGFR3AddrFromFlat(pUVM, &NameAddr, ModCtlv11.mod_modname);
                    rc = pVMM->pfnDBGFR3MemRead(pUVM, 0, &NameAddr, &szUnix, sizeof(szUnix));
                    if (RT_SUCCESS(rc))
                    {
                        if (!strcmp(szUnix, "unix"))
                        {
                            pThis->AddrUnixModCtl = ModCtlAddr;
                            pThis->iModCtlVer = 11;
                            cbModCtl = sizeof(ModCtlv11);
                            break;
                        }
                        Log(("sol32 mod_name=%.*s v11\n", sizeof(szUnix), szUnix));
                    }
                }
            }
        }

        /* v9 */
        if (pThis->f64Bit)
        {
            pVMM->pfnDBGFR3AddrFromFlat(pUVM, &ModCtlAddr, HitAddr.FlatPtr - RT_OFFSETOF(SOL64v9_modctl_t, mod_text));
            SOL64v9_modctl_t ModCtlv9;
            rc = pVMM->pfnDBGFR3MemRead(pUVM, 0, &ModCtlAddr, &ModCtlv9, sizeof(ModCtlv9));
            if (RT_SUCCESS(rc))
            {
                if (    SOL64_VALID_ADDRESS(ModCtlv9.mod_next)
                    &&  SOL64_VALID_ADDRESS(ModCtlv9.mod_prev)
                    &&  ModCtlv9.mod_id == 0
                    &&  SOL64_VALID_ADDRESS(ModCtlv9.mod_mp)
                    &&  SOL64_VALID_ADDRESS(ModCtlv9.mod_filename)
                    &&  SOL64_VALID_ADDRESS(ModCtlv9.mod_modname)
                    &&  (ModCtlv9.mod_loaded == 1    || ModCtlv9.mod_loaded == 0)
                    &&  (ModCtlv9.mod_installed == 1 || ModCtlv9.mod_installed == 0)
                    &&  ModCtlv9.mod_requisites == 0
                    &&  (ModCtlv9.mod_loadcnt == 1   || ModCtlv9.mod_loadcnt == 0)
                    /*&&  ModCtlv9.mod_text == pThis->AddrUnixText.FlatPtr*/
                    &&  ModCtlv9.mod_text_size < SOL_UNIX_MAX_CODE_SEG_SIZE)
                {
                    char szUnix[5];
                    DBGFADDRESS NameAddr;
                    pVMM->pfnDBGFR3AddrFromFlat(pUVM, &NameAddr, ModCtlv9.mod_modname);
                    rc = pVMM->pfnDBGFR3MemRead(pUVM, 0, &NameAddr, &szUnix, sizeof(szUnix));
                    if (RT_SUCCESS(rc))
                    {
                        if (!strcmp(szUnix, "unix"))
                        {
                            pThis->AddrUnixModCtl = ModCtlAddr;
                            pThis->iModCtlVer = 9;
                            cbModCtl = sizeof(ModCtlv9);
                            break;
                        }
                        Log(("sol64 mod_name=%.*s v9\n", sizeof(szUnix), szUnix));
                    }
                }
            }
        }
        else
        {
            pVMM->pfnDBGFR3AddrFromFlat(pUVM, &ModCtlAddr, HitAddr.FlatPtr - RT_OFFSETOF(SOL32v9_modctl_t, mod_text));
            SOL32v9_modctl_t ModCtlv9;
            rc = pVMM->pfnDBGFR3MemRead(pUVM, 0, &ModCtlAddr, &ModCtlv9, sizeof(ModCtlv9));
            if (RT_SUCCESS(rc))
            {
                if (    SOL32_VALID_ADDRESS(ModCtlv9.mod_next)
                    &&  SOL32_VALID_ADDRESS(ModCtlv9.mod_prev)
                    &&  ModCtlv9.mod_id == 0
                    &&  SOL32_VALID_ADDRESS(ModCtlv9.mod_mp)
                    &&  SOL32_VALID_ADDRESS(ModCtlv9.mod_filename)
                    &&  SOL32_VALID_ADDRESS(ModCtlv9.mod_modname)
                    &&  (ModCtlv9.mod_loaded == 1    || ModCtlv9.mod_loaded == 0)
                    &&  (ModCtlv9.mod_installed == 1 || ModCtlv9.mod_installed == 0)
                    &&  ModCtlv9.mod_requisites == 0
                    &&  (ModCtlv9.mod_loadcnt == 1   || ModCtlv9.mod_loadcnt == 0)
                    /*&&  ModCtlv9.mod_text == pThis->AddrUnixText.FlatPtr*/
                    &&  ModCtlv9.mod_text_size < SOL_UNIX_MAX_CODE_SEG_SIZE )
                {
                    char szUnix[5];
                    DBGFADDRESS NameAddr;
                    pVMM->pfnDBGFR3AddrFromFlat(pUVM, &NameAddr, ModCtlv9.mod_modname);
                    rc = pVMM->pfnDBGFR3MemRead(pUVM, 0, &NameAddr, &szUnix, sizeof(szUnix));
                    if (RT_SUCCESS(rc))
                    {
                        if (!strcmp(szUnix, "unix"))
                        {
                            pThis->AddrUnixModCtl = ModCtlAddr;
                            pThis->iModCtlVer = 9;
                            cbModCtl = sizeof(ModCtlv9);
                            break;
                        }
                        Log(("sol32 mod_name=%.*s v9\n", sizeof(szUnix), szUnix));
                    }
                }
            }
        }

        /* next */
        pVMM->pfnDBGFR3AddrFromFlat(pUVM, &CurAddr, HitAddr.FlatPtr + cbExpr);
    }

    /*
     * Walk the module chain and add the modules and their symbols.
     */
    if (pThis->AddrUnixModCtl.FlatPtr)
    {
        int iMod = 0;
        CurAddr = pThis->AddrUnixModCtl;
        do
        {
            /* read it */
            SOL_modctl_t ModCtl;
            rc = pVMM->pfnDBGFR3MemRead(pUVM, 0, &CurAddr, &ModCtl, cbModCtl);
            if (RT_FAILURE(rc))
            {
                LogRel(("sol: bad modctl_t chain for module %d: %RGv - %Rrc\n", iMod, CurAddr.FlatPtr, rc));
                break;
            }

            /* process it. */
            if (pThis->f64Bit)
                dbgDiggerSolarisProcessModCtl64(pUVM, pVMM, pThis, &ModCtl);
            else
                dbgDiggerSolarisProcessModCtl32(pUVM, pVMM, pThis, &ModCtl);

            /* next */
            if (pThis->f64Bit)
            {
                AssertCompile2MemberOffsets(SOL_modctl_t, v11_64.mod_next, v9_64.mod_next);
                if (!SOL64_VALID_ADDRESS(ModCtl.v9_64.mod_next))
                {
                    LogRel(("sol64: bad modctl_t chain for module %d at %RGv: %RGv\n", iMod, CurAddr.FlatPtr, (RTGCUINTPTR)ModCtl.v9_64.mod_next));
                    break;
                }
                pVMM->pfnDBGFR3AddrFromFlat(pUVM, &CurAddr, ModCtl.v9_64.mod_next);
            }
            else
            {
                AssertCompile2MemberOffsets(SOL_modctl_t, v11_32.mod_next, v9_32.mod_next);
                if (!SOL32_VALID_ADDRESS(ModCtl.v9_32.mod_next))
                {
                    LogRel(("sol32: bad modctl_t chain for module %d at %RGv: %RGv\n", iMod, CurAddr.FlatPtr, (RTGCUINTPTR)ModCtl.v9_32.mod_next));
                    break;
                }
                pVMM->pfnDBGFR3AddrFromFlat(pUVM, &CurAddr, ModCtl.v9_32.mod_next);
            }
            if (++iMod >= 1024)
            {
                LogRel(("sol32: too many modules (%d)\n", iMod));
                break;
            }
        } while (CurAddr.FlatPtr != pThis->AddrUnixModCtl.FlatPtr);
    }

    pThis->fValid = true;
    return VINF_SUCCESS;
}


/**
 * @copydoc DBGFOSREG::pfnProbe
 */
static DECLCALLBACK(bool)  dbgDiggerSolarisProbe(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    PDBGDIGGERSOLARIS pThis = (PDBGDIGGERSOLARIS)pvData;

    /*
     * Look for "SunOS Release" in the text segment.
     */
    DBGFADDRESS Addr;
    bool        f64Bit = false;

    /* 32-bit search range. */
    pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, 0xfe800000);
    RTGCUINTPTR cbRange = 0xfec00000 - 0xfe800000;

    DBGFADDRESS HitAddr;
    static const uint8_t s_abSunRelease[] = "SunOS Release ";
    int rc = pVMM->pfnDBGFR3MemScan(pUVM, 0, &Addr, cbRange, 1, s_abSunRelease, sizeof(s_abSunRelease) - 1, &HitAddr);
    if (RT_FAILURE(rc))
    {
        /* 64-bit.... */
        pVMM->pfnDBGFR3AddrFromFlat(pUVM, &Addr, UINT64_C(0xfffffffffb800000));
        cbRange = UINT64_C(0xfffffffffbd00000) - UINT64_C(0xfffffffffb800000);
        rc = pVMM->pfnDBGFR3MemScan(pUVM, 0, &Addr, cbRange, 1, s_abSunRelease, sizeof(s_abSunRelease) - 1, &HitAddr);
        if (RT_FAILURE(rc))
            return false;
        f64Bit = true;
    }

    /*
     * Look for the copyright string too, just to be sure.
     */
    static const uint8_t s_abSMI[] = "Sun Microsystems, Inc.";
    static const uint8_t s_abORCL[] = "Oracle and/or its affiliates.";
    rc = pVMM->pfnDBGFR3MemScan(pUVM, 0, &Addr, cbRange, 1, s_abSMI, sizeof(s_abSMI) - 1, &HitAddr);
    if (RT_FAILURE(rc))
    {
        /* Try the alternate copyright string. */
        rc = pVMM->pfnDBGFR3MemScan(pUVM, 0, &Addr, cbRange, 1, s_abORCL, sizeof(s_abORCL) - 1, &HitAddr);
        if (RT_FAILURE(rc))
            return false;
    }

    /*
     * Remember the unix text and data addresses and bitness.
     */
    pThis->AddrUnixText = Addr;
    pVMM->pfnDBGFR3AddrAdd(&Addr, SOL_UNIX_MAX_CODE_SEG_SIZE);
    pThis->AddrUnixData = Addr;
    pThis->f64Bit       = f64Bit;

    return true;
}


/**
 * @copydoc DBGFOSREG::pfnDestruct
 */
static DECLCALLBACK(void)  dbgDiggerSolarisDestruct(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    RT_NOREF(pUVM, pVMM, pvData);
}


/**
 * @copydoc DBGFOSREG::pfnConstruct
 */
static DECLCALLBACK(int)  dbgDiggerSolarisConstruct(PUVM pUVM, PCVMMR3VTABLE pVMM, void *pvData)
{
    RT_NOREF(pUVM, pVMM, pvData);
    return VINF_SUCCESS;
}


const DBGFOSREG g_DBGDiggerSolaris =
{
    /* .u32Magic = */               DBGFOSREG_MAGIC,
    /* .fFlags = */                 0,
    /* .cbData = */                 sizeof(DBGDIGGERSOLARIS),
    /* .szName = */                 "Solaris",
    /* .pfnConstruct = */           dbgDiggerSolarisConstruct,
    /* .pfnDestruct = */            dbgDiggerSolarisDestruct,
    /* .pfnProbe = */               dbgDiggerSolarisProbe,
    /* .pfnInit = */                dbgDiggerSolarisInit,
    /* .pfnRefresh = */             dbgDiggerSolarisRefresh,
    /* .pfnTerm = */                dbgDiggerSolarisTerm,
    /* .pfnQueryVersion = */        dbgDiggerSolarisQueryVersion,
    /* .pfnQueryInterface = */      dbgDiggerSolarisQueryInterface,
    /* .pfnStackUnwindAssist = */   dbgDiggerSolarisStackUnwindAssist,
    /* .u32EndMagic = */            DBGFOSREG_MAGIC
};

