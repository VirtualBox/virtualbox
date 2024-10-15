/* $Id: DBGPlugInLinuxModuleCodeTmpl.cpp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGPlugInLinux - Code template for struct module processing.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifndef LNX_MK_VER
# define LNX_MK_VER(major, minor, build) (((major) << 22) | ((minor) << 12) | (build))
#endif
#if LNX_64BIT
# define LNX_ULONG_T uint64_t
#else
# define LNX_ULONG_T uint32_t
#endif
#if LNX_64BIT
# define PAD32ON64(seq) uint32_t RT_CONCAT(u32Padding,seq);
#else
# define PAD32ON64(seq)
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Kernel module symbol (hasn't changed in ages).
 */
typedef struct RT_CONCAT(LNXMODKSYM,LNX_SUFFIX)
{
    LNX_ULONG_T         uValue;
    LNX_PTR_T           uPtrSymName;
} RT_CONCAT(LNXMODKSYM,LNX_SUFFIX);


#if LNX_VER >= LNX_MK_VER(2,6,11)
typedef struct RT_CONCAT(LNXMODKOBJECT,LNX_SUFFIX)
{
    LNX_PTR_T           uPtrKName;
# if LNX_VER < LNX_MK_VER(2,6,24)
    char                name[20];
# endif
# if LNX_VER < LNX_MK_VER(2,6,27)
    int32_t             cRefs;
#  if LNX_VER >= LNX_MK_VER(2,6,24)
    PAD32ON64(0)
#  endif
# endif
    LNX_PTR_T           uPtrNext;
    LNX_PTR_T           uPtrPrev;
    LNX_PTR_T           uPtrParent; /**< struct kobject pointer */
    LNX_PTR_T           uPtrKset;   /**< struct kset pointer */
    LNX_PTR_T           uPtrKtype;  /**< struct kobj_type pointer */
    LNX_PTR_T           uPtrDirEntry; /**< struct dentry pointer; 2.6.23+ sysfs_dirent. */
# if LNX_VER >= LNX_MK_VER(2,6,17) && LNX_VER < LNX_MK_VER(2,6,24)
    LNX_PTR_T           aPtrWaitQueueHead[3];
# endif
# if LNX_VER >= LNX_MK_VER(2,6,27)
    int32_t             cRefs;
    uint32_t            uStateStuff;
# elif LNX_VER >= LNX_MK_VER(2,6,25)
    LNX_ULONG_T         uStateStuff;
# endif
    /* non-kobject: */
    LNX_PTR_T           uPtrModule;     /**< struct module pointer. */
#  if LNX_VER >= LNX_MK_VER(2,6,21)
    LNX_PTR_T           uPtrDriverDir;  /**< Points to struct kobject. */
#  endif
#  if LNX_VER >= LNX_MK_VER(4,5,0)
    LNX_PTR_T           uPtrMp;
    LNX_PTR_T           uPtrCompletion; /**< Points to struct completion. */
#  endif
} RT_CONCAT(LNXMODKOBJECT,LNX_SUFFIX);
#endif
#if LNX_VER == LNX_MK_VER(2,6,24) && LNX_64BIT
AssertCompileMemberOffset(RT_CONCAT(LNXMODKOBJECT,LNX_SUFFIX), uPtrParent, 32);
AssertCompileMemberOffset(RT_CONCAT(LNXMODKOBJECT,LNX_SUFFIX), uPtrParent, 32);
AssertCompileSize(RT_CONCAT(LNXMODKOBJECT,LNX_SUFFIX), 80);
#endif


#if LNX_VER >= LNX_MK_VER(4,5,0)
/**
 * Red black tree node.
 */
typedef struct RT_CONCAT(LNXRBNODE,LNX_SUFFIX)
{
    LNX_ULONG_T         uRbParentColor;
    LNX_PTR_T           uPtrRbRight;
    LNX_PTR_T           uPtrRbLeft;
} RT_CONCAT(LNXRBNODE,LNX_SUFFIX);


/**
 * Latch tree node.
 */
typedef struct RT_CONCAT(LNXLATCHTREENODE,LNX_SUFFIX)
{
    RT_CONCAT(LNXRBNODE,LNX_SUFFIX) aNode[2];
} RT_CONCAT(LNXLATCHTREENODE,LNX_SUFFIX);


/**
 * Module tree node.
 */
typedef struct RT_CONCAT(LNXMODTREENODE,LNX_SUFFIX)
{
    LNX_PTR_T                              uPtrKMod;
    RT_CONCAT(LNXLATCHTREENODE,LNX_SUFFIX) Node;
} RT_CONCAT(LNXMODTREENODE,LNX_SUFFIX);


/**
 * Module layout.
 */
typedef struct RT_CONCAT(LNXKMODLAYOUT,LNX_SUFFIX)
{
    LNX_PTR_T                               uPtrBase; /**< Base pointer to text and data. */
    uint32_t                                cb;       /**< Size of the module. */
    uint32_t                                cbText;   /**< Size of the text section. */
    uint32_t                                cbRo;     /**< Size of the readonly portion (text + ro data). */
    RT_CONCAT(LNXMODTREENODE,LNX_SUFFIX)    ModTreeNd; /**< Only available when CONFIG_MODULES_TREE_LOOKUP is set (default). */
} RT_CONCAT(LNXKMODLAYOUT,LNX_SUFFIX);


/**
 * Mutex.
 */
typedef struct RT_CONCAT(LNXMUTEX,LNX_SUFFIX)
{
    LNX_ULONG_T                             uOwner;
    uint32_t                                wait_lock; /**< Actually spinlock_t */
    PAD32ON64(0)
    LNX_PTR_T                               uWaitLstPtrNext;
    LNX_PTR_T                               uWaitLstPtrPrev;
} RT_CONCAT(LNXMUTEX,LNX_SUFFIX);
#endif


/**
 * Maps to the start of struct module in include/linux/module.h.
 */
typedef struct RT_CONCAT(LNXKMODULE,LNX_SUFFIX)
{
#if LNX_VER >= LNX_MK_VER(4,5,0)
    /* Completely new layout to not feed the spaghetti dragons further. */
    int32_t                             state;
    PAD32ON64(0)
    LNX_PTR_T                           uPtrNext;
    LNX_PTR_T                           uPtrPrev;
    char                                name[64 - sizeof(LNX_PTR_T)];

    RT_CONCAT(LNXMODKOBJECT,LNX_SUFFIX) mkobj;
    LNX_PTR_T                           uPtrModInfoAttrs;   /**< Points to struct module_attribute. */
    LNX_PTR_T                           uPtrVersion;        /**< String pointers. */
    LNX_PTR_T                           uPtrSrcVersion;     /**< String pointers. */
    LNX_PTR_T                           uPtrHolderDir;      /**< Points to struct kobject. */

    /** @name Exported Symbols
     * @{ */
    LNX_PTR_T                           uPtrSyms;           /**< Array of struct kernel_symbol. */
    LNX_PTR_T                           uPtrCrcs;           /**< unsigned long array */
    uint32_t                            num_syms;
    /** @} */

    /** @name Kernel parameters
     * @{ */
    RT_CONCAT(LNXMUTEX,LNX_SUFFIX)      Mtx;                /**< Mutex. */
    LNX_PTR_T                           uPtrKp;             /**< Points to struct kernel_param */
    uint32_t                            num_kp;
    /** @} */

    /** @name GPL Symbols
     * @{ */
    uint32_t                            num_gpl_syms;
    LNX_PTR_T                           uPtrGplSyms;        /**< Array of struct kernel_symbol. */
    LNX_PTR_T                           uPtrGplCrcs;        /**< unsigned long array */
    /** @} */

    /** @name Unused symbols
     * @{ */
    LNX_PTR_T                           uPtrUnusedSyms;     /**< Array of struct kernel_symbol. */
    LNX_PTR_T                           uPtrUnusedCrcs;     /**< unsigned long array */
    uint32_t                            num_unused_syms;
    uint32_t                            num_unused_gpl_syms;
    LNX_PTR_T                           uPtrUnusedGplSyms;  /**< Array of struct kernel_symbol. */
    LNX_PTR_T                           uPtrUnusedGplCrcs;  /**< unsigned long array */
    /** @} */

    uint8_t                             sig_ok;
    uint8_t                             async_probe_requested;

    /** @name Future GPL Symbols
     * @{ */
    LNX_PTR_T                           uPtrGplFutureSyms;  /**< Array of struct kernel_symbol. */
    LNX_PTR_T                           uPtrGplFutureCrcs;  /**< unsigned long array */
    uint32_t                            num_gpl_future_syms;
    /** @} */

    /** @name Exception table.
     * @{ */
    uint32_t                            num_exentries;
    LNX_PTR_T                           uPtrEntries;        /**< struct exception_table_entry array. */
    /** @} */

    LNX_PTR_T                           pfnInit;
    RT_CONCAT(LNXKMODLAYOUT,LNX_SUFFIX) CoreLayout;         /**< Should be aligned on a cache line. */
    RT_CONCAT(LNXKMODLAYOUT,LNX_SUFFIX) InitLayout;

#elif LNX_VER >= LNX_MK_VER(2,5,48)
    /*
     * This first part is mostly always the same.
     */
    int32_t     state;
    PAD32ON64(0)
    LNX_PTR_T   uPtrNext;
    LNX_PTR_T   uPtrPrev;
    char        name[64 - sizeof(LNX_PTR_T)];

    /*
     * Here be spaghetti dragons.
     */
# if LNX_VER >= LNX_MK_VER(2,6,11)
    RT_CONCAT(LNXMODKOBJECT,LNX_SUFFIX) mkobj; /**< Was just kobj for a while. */
    LNX_PTR_T   uPtrParamAttrs;     /**< Points to struct module_param_attrs. */
#  if LNX_VER >= LNX_MK_VER(2,6,17)
    LNX_PTR_T   uPtrModInfoAttrs;   /**< Points to struct module_attribute. */
#  endif
#  if LNX_VER == LNX_MK_VER(2,6,20)
    LNX_PTR_T   uPtrDriverDir;      /**< Points to struct kobject. */
#  elif LNX_VER >= LNX_MK_VER(2,6,21)
    LNX_PTR_T   uPtrHolderDir;      /**< Points to struct kobject. */
#  endif
#  if LNX_VER >= LNX_MK_VER(2,6,13)
    LNX_PTR_T   uPtrVersion;        /**< String pointers. */
    LNX_PTR_T   uPtrSrcVersion;     /**< String pointers. */
#  endif
# else
#  if LNX_VER >= LNX_MK_VER(2,6,7)
    LNX_PTR_T   uPtrMkObj;
#  endif
#  if LNX_VER >= LNX_MK_VER(2,6,10)
    LNX_PTR_T   uPtrParamsKobject;
#  endif
# endif

    /** @name Exported Symbols
     * @{ */
# if LNX_VER < LNX_MK_VER(2,5,67)
    LNX_PTR_T   uPtrSymsNext, uPtrSymsPrev, uPtrSymsOwner;
#  if LNX_VER >= LNX_MK_VER(2,5,55)
    int32_t     syms_gplonly;
    uint32_t    num_syms;
# else
    uint32_t    num_syms;
    PAD32ON64(1)
#  endif
# endif
    LNX_PTR_T   uPtrSyms; /**< Array of struct kernel_symbol. */
# if LNX_VER >= LNX_MK_VER(2,5,67)
    uint32_t    num_syms;
    PAD32ON64(1)
# endif
# if LNX_VER >= LNX_MK_VER(2,5,60)
    LNX_PTR_T   uPtrCrcs; /**< unsigned long array */
# endif
    /** @} */

    /** @name GPL Symbols
     * @since 2.5.55
     * @{ */
# if LNX_VER >= LNX_MK_VER(2,5,55)
#  if LNX_VER < LNX_MK_VER(2,5,67)
    LNX_PTR_T   uPtrGplSymsNext, uPtrGplSymsPrev, uPtrGplSymsOwner;
#   if LNX_VER >= LNX_MK_VER(2,5,55)
    int32_t     gpl_syms_gplonly;
    uint32_t    num_gpl_syms;
#  else
    uint32_t    num_gpl_syms;
    PAD32ON64(2)
#   endif
#  endif
    LNX_PTR_T   uPtrGplSyms; /**< Array of struct kernel_symbol. */
#  if LNX_VER >= LNX_MK_VER(2,5,67)
    uint32_t    num_gpl_syms;
    PAD32ON64(2)
#  endif
#  if LNX_VER >= LNX_MK_VER(2,5,60)
    LNX_PTR_T   uPtrGplCrcs; /**< unsigned long array */
#  endif
# endif /* > 2.5.55 */
    /** @} */

    /** @name Unused Exported Symbols
     * @since 2.6.18
     * @{ */
# if LNX_VER >= LNX_MK_VER(2,6,18)
    LNX_PTR_T   uPtrUnusedSyms; /**< Array of struct kernel_symbol. */
    uint32_t    num_unused_syms;
    PAD32ON64(4)
    LNX_PTR_T   uPtrUnusedCrcs; /**< unsigned long array */
# endif
    /** @} */

    /** @name Unused GPL Symbols
     * @since 2.6.18
     * @{ */
# if LNX_VER >= LNX_MK_VER(2,6,18)
    LNX_PTR_T   uPtrUnusedGplSyms; /**< Array of struct kernel_symbol. */
    uint32_t    num_unused_gpl_syms;
    PAD32ON64(5)
    LNX_PTR_T   uPtrUnusedGplCrcs; /**< unsigned long array */
# endif
    /** @} */

    /** @name Future GPL Symbols
     * @since 2.6.17
     * @{ */
# if LNX_VER >= LNX_MK_VER(2,6,17)
    LNX_PTR_T   uPtrGplFutureSyms; /**< Array of struct kernel_symbol. */
    uint32_t    num_gpl_future_syms;
    PAD32ON64(3)
    LNX_PTR_T   uPtrGplFutureCrcs; /**< unsigned long array */
# endif
    /** @} */

    /** @name Exception table.
     * @{ */
# if LNX_VER < LNX_MK_VER(2,5,67)
    LNX_PTR_T   uPtrXcptTabNext, uPtrXcptTabPrev;
# endif
    uint32_t    num_exentries;
    PAD32ON64(6)
    LNX_PTR_T   uPtrEntries; /**< struct exception_table_entry array. */
    /** @} */

    /*
     * Hopefully less spaghetti from here on...
     */
    LNX_PTR_T   pfnInit;
    LNX_PTR_T   uPtrModuleInit;
    LNX_PTR_T   uPtrModuleCore;
    LNX_ULONG_T cbInit;
    LNX_ULONG_T cbCore;
# if LNX_VER >= LNX_MK_VER(2,5,74)
    LNX_ULONG_T cbInitText;
    LNX_ULONG_T cbCoreText;
# endif

# if LNX_VER >= LNX_MK_VER(2,6,18)
    LNX_PTR_T   uPtrUnwindInfo;
# endif
#else
   uint32_t     structure_size;

#endif
} RT_CONCAT(LNXKMODULE,LNX_SUFFIX);

# if LNX_VER == LNX_MK_VER(2,6,24) && LNX_64BIT
AssertCompileMemberOffset(RT_CONCAT(LNXKMODULE,LNX_SUFFIX), uPtrParamAttrs, 160);
AssertCompileMemberOffset(RT_CONCAT(LNXKMODULE,LNX_SUFFIX), num_syms, 208);
AssertCompileMemberOffset(RT_CONCAT(LNXKMODULE,LNX_SUFFIX), num_gpl_syms, 232);
AssertCompileMemberOffset(RT_CONCAT(LNXKMODULE,LNX_SUFFIX), num_unused_syms, 256);
AssertCompileMemberOffset(RT_CONCAT(LNXKMODULE,LNX_SUFFIX), num_unused_gpl_syms, 280);
AssertCompileMemberOffset(RT_CONCAT(LNXKMODULE,LNX_SUFFIX), num_gpl_future_syms, 304);
AssertCompileMemberOffset(RT_CONCAT(LNXKMODULE,LNX_SUFFIX), num_exentries, 320);
AssertCompileMemberOffset(RT_CONCAT(LNXKMODULE,LNX_SUFFIX), uPtrModuleCore, 352);
AssertCompileMemberOffset(RT_CONCAT(LNXKMODULE,LNX_SUFFIX), uPtrUnwindInfo, 392);
#endif



/**
 * Loads the kernel symbols at the given start address.
 *
 * @returns VBox status code.
 * @param   pUVM                Pointer to the user-mode VM instance.
 * @param   hDbgMod             The module handle to add the loaded symbols to.
 * @param   uPtrModuleStart     The virtual address where the kernel module starts we want to  extract symbols from.
 * @param   uPtrSymStart        The start address of the array of symbols.
 * @param   cSyms               Number of symbols in the array.
 */
static int RT_CONCAT(dbgDiggerLinuxLoadModuleSymbols,LNX_SUFFIX)(PUVM pUVM, PCVMMR3VTABLE pVMM, RTDBGMOD hDbgMod,
                                                                 LNX_PTR_T uPtrModuleStart, LNX_PTR_T uPtrSymStart, uint32_t cSyms)
{
    int rc = VINF_SUCCESS;
    DBGFADDRESS AddrSym;
    pVMM->pfnDBGFR3AddrFromFlat(pUVM, &AddrSym, uPtrSymStart);

    while (   cSyms
           && RT_SUCCESS(rc))
    {
        RT_CONCAT(LNXMODKSYM,LNX_SUFFIX) aSyms[64];
        uint32_t cThisLoad = RT_MIN(cSyms, RT_ELEMENTS(aSyms));

        rc = pVMM->pfnDBGFR3MemRead(pUVM, 0, &AddrSym, &aSyms[0], cThisLoad * sizeof(aSyms[0]));
        if (RT_SUCCESS(rc))
        {
            cSyms -= cThisLoad;
            pVMM->pfnDBGFR3AddrAdd(&AddrSym, cThisLoad * sizeof(aSyms[0]));

            for (uint32_t i = 0; i < cThisLoad; i++)
            {
                char szSymName[128];
                DBGFADDRESS AddrSymName;
                rc = pVMM->pfnDBGFR3MemRead(pUVM, 0, pVMM->pfnDBGFR3AddrFromFlat(pUVM, &AddrSymName, aSyms[i].uPtrSymName),
                                            &szSymName[0], sizeof(szSymName));
                if (RT_FAILURE(rc))
                    break;

                /* Verify string encoding - ignore the symbol if it fails. */
                rc = RTStrValidateEncodingEx(&szSymName[0], sizeof(szSymName), RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED);
                if (RT_FAILURE(rc))
                    continue;

                Assert(aSyms[i].uValue >= uPtrModuleStart);
                rc = RTDbgModSymbolAdd(hDbgMod, szSymName, RTDBGSEGIDX_RVA, aSyms[i].uValue - uPtrModuleStart,
                                       0 /*cb*/, 0 /*fFlags*/, NULL);
                if (RT_SUCCESS(rc))
                    LogFlowFunc(("Added symbol '%s' successfully\n", szSymName));
                else
                {
                    LogFlowFunc(("Adding symbol '%s' failed with: %Rrc\n", szSymName, rc));
                    rc = VINF_SUCCESS;
                }
            }
        }
    }

    return rc;
}


/**
 * Version specific module processing code.
 */
static uint64_t RT_CONCAT(dbgDiggerLinuxLoadModule,LNX_SUFFIX)(PDBGDIGGERLINUX pThis, PUVM pUVM,
                                                               PCVMMR3VTABLE pVMM, PDBGFADDRESS pAddrModule)
{
    RT_CONCAT(LNXKMODULE,LNX_SUFFIX) Module;

    int rc = pVMM->pfnDBGFR3MemRead(pUVM, 0, pVMM->pfnDBGFR3AddrSub(pAddrModule, RT_UOFFSETOF(RT_CONCAT(LNXKMODULE,LNX_SUFFIX),
                                                                                              uPtrNext)),
                                    &Module, sizeof(Module));
    if (RT_FAILURE(rc))
    {
        LogRelFunc(("Failed to read module structure at %#RX64: %Rrc\n", pAddrModule->FlatPtr, rc));
        return 0;
    }

    /*
     * Check the module name.
     */
#if LNX_VER >= LNX_MK_VER(2,5,48)
    const char  *pszName = Module.name;
    size_t const cbName  = sizeof(Module.name);
#else

#endif
    if (   RTStrNLen(pszName, cbName) >= cbName
        || RT_FAILURE(RTStrValidateEncoding(pszName))
        || *pszName == '\0')
    {
        LogRelFunc(("%#RX64: Bad name: %.*Rhxs\n", pAddrModule->FlatPtr, (int)cbName, pszName));
        return 0;
    }

    /*
     * Create a simple module for it.
     */
#if LNX_VER >= LNX_MK_VER(4,5,0)
    LNX_PTR_T uPtrModuleCore = Module.CoreLayout.uPtrBase;
    uint32_t cbCore          = Module.CoreLayout.cb;
#else
    LNX_PTR_T uPtrModuleCore = Module.uPtrModuleCore;
    uint32_t cbCore          = (uint32_t)Module.cbCore;
#endif
    LogRelFunc((" %#RX64: %#RX64 LB %#RX32 %s\n", pAddrModule->FlatPtr, uPtrModuleCore, cbCore, pszName));

    RTDBGMOD hDbgMod;
    rc = RTDbgModCreate(&hDbgMod, pszName, cbCore, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
    {
        rc = RTDbgModSetTag(hDbgMod, DIG_LNX_MOD_TAG);
        if (RT_SUCCESS(rc))
        {
            RTDBGAS hAs = pVMM->pfnDBGFR3AsResolveAndRetain(pUVM, DBGF_AS_KERNEL);
            rc = RTDbgAsModuleLink(hAs, hDbgMod, uPtrModuleCore, RTDBGASLINK_FLAGS_REPLACE /*fFlags*/);
            RTDbgAsRelease(hAs);
            if (RT_SUCCESS(rc))
            {
                rc = RT_CONCAT(dbgDiggerLinuxLoadModuleSymbols,LNX_SUFFIX)(pUVM, pVMM, hDbgMod, uPtrModuleCore,
                                                                           Module.uPtrSyms, Module.num_syms);
                if (RT_FAILURE(rc))
                    LogRelFunc((" Faild to load symbols: %Rrc\n", rc));

#if LNX_VER >= LNX_MK_VER(2,5,55)
                rc = RT_CONCAT(dbgDiggerLinuxLoadModuleSymbols,LNX_SUFFIX)(pUVM, pVMM, hDbgMod, uPtrModuleCore,
                                                                           Module.uPtrGplSyms, Module.num_gpl_syms);
                if (RT_FAILURE(rc))
                    LogRelFunc((" Faild to load GPL symbols: %Rrc\n", rc));
#endif

#if LNX_VER >= LNX_MK_VER(2,6,17)
                rc = RT_CONCAT(dbgDiggerLinuxLoadModuleSymbols,LNX_SUFFIX)(pUVM, pVMM, hDbgMod, uPtrModuleCore,
                                                                           Module.uPtrGplFutureSyms, Module.num_gpl_future_syms);
                if (RT_FAILURE(rc))
                    LogRelFunc((" Faild to load future GPL symbols: %Rrc\n", rc));
#endif

#if LNX_VER >= LNX_MK_VER(2,6,18)
                rc = RT_CONCAT(dbgDiggerLinuxLoadModuleSymbols,LNX_SUFFIX)(pUVM, pVMM, hDbgMod, uPtrModuleCore,
                                                                           Module.uPtrUnusedSyms, Module.num_unused_syms);
                if (RT_FAILURE(rc))
                    LogRelFunc((" Faild to load unused symbols: %Rrc\n", rc));

                rc = RT_CONCAT(dbgDiggerLinuxLoadModuleSymbols,LNX_SUFFIX)(pUVM, pVMM, hDbgMod, uPtrModuleCore,
                                                                           Module.uPtrUnusedGplSyms, Module.num_unused_gpl_syms);
                if (RT_FAILURE(rc))
                    LogRelFunc((" Faild to load unused GPL symbols: %Rrc\n", rc));
#endif
            }
        }
        else
            LogRel(("DbgDiggerOs2: RTDbgModSetTag failed: %Rrc\n", rc));
        RTDbgModRelease(hDbgMod);
    }

    RT_NOREF(pThis);
    return Module.uPtrNext;
}

#undef LNX_VER
#undef LNX_SUFFIX
#undef LNX_ULONG_T
#undef PAD32ON64
