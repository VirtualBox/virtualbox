/* $Id: coredumper-solaris.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Custom Core Dumper, Solaris.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef IPRT_INCLUDED_SRC_r3_solaris_coredumper_solaris_h
#define IPRT_INCLUDED_SRC_r3_solaris_coredumper_solaris_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>

#ifdef RT_OS_SOLARIS
# if defined(RT_ARCH_X86) && _FILE_OFFSET_BITS==64
/*
 * Solaris' procfs cannot be used with large file environment in 32-bit.
 */
# undef _FILE_OFFSET_BITS
# define _FILE_OFFSET_BITS 32
# include <procfs.h>
# include <sys/procfs.h>
# include <sys/old_procfs.h>
# undef _FILE_OFFSET_BITS
# define _FILE_OFFSET_BITS 64
#else
# include <procfs.h>
# include <sys/procfs.h>
# include <sys/old_procfs.h>
#endif
# include <limits.h>
# include <thread.h>
# include <sys/auxv.h>
# include <sys/lwp.h>
# include <sys/zone.h>
# include <sys/utsname.h>

#ifdef RT_ARCH_AMD64
# define _ELF64
# undef _ELF32_COMPAT
#endif
# include <sys/corectl.h>
#endif


#ifdef RT_OS_SOLARIS
/**
 * Memory mapping descriptor employed by the solaris core dumper.
 */
typedef struct RTSOLCOREMAPINFO
{
    prmap_t                         pMap;                       /**< Proc description of this mapping */
    int                             fError;                     /**< Any error reading this mapping (errno) */
    struct RTSOLCOREMAPINFO        *pNext;                      /**< Pointer to the next mapping */
} RTSOLCOREMAPINFO;
/** Pointer to a solaris memory mapping descriptor. */
typedef RTSOLCOREMAPINFO *PRTSOLCOREMAPINFO;

/**
 * Whether this is an old or new style solaris core.
 */
typedef enum RTSOLCORETYPE
{
    enmOldEra = 0x01d,   /**< old */
    enmNewEra = 0x5c1f1  /**< sci-fi */
} RTSOLCORETYPE;

/**
 * Per-Thread information employed by the solaris core dumper.
 */
typedef struct RTSOLCORETHREADINFO
{
    lwpsinfo_t                      Info;                       /**< Proc description of this thread */
    lwpstatus_t                    *pStatus;                    /**< Proc description of this thread's status (can be NULL, zombie lwp) */
    struct RTSOLCORETHREADINFO     *pNext;                      /**< Pointer to the next thread */
} RTSOLCORETHREADINFO;
typedef RTSOLCORETHREADINFO *PRTSOLCORETHREADINFO;
#endif


/**
 * Current (also the core target) process information.
 */
typedef struct RTSOLCOREPROCESS
{
    RTPROCESS                       Process;                    /**< The pid of the process */
    char                            szExecPath[PATH_MAX];       /**< Path of the executable */
    char                           *pszExecName;                /**< Name of the executable file */
#ifdef RT_OS_SOLARIS
    void                           *pvProcInfo;                 /**< Process info. */
    size_t                          cbProcInfo;                 /**< Size of the process info. */
    prpsinfo_t                      ProcInfoOld;                /**< Process info. Older version (for GDB compat.) */
    pstatus_t                       ProcStatus;                 /**< Process status info. */
    thread_t                        hCurThread;                 /**< The current thread */
    ucontext_t                     *pCurThreadCtx;              /**< Context info. of current thread before starting to dump */
    int                             fdAs;                       /**< proc/pid/as file handle */
    auxv_t                         *pAuxVecs;                   /**< Aux vector of process */
    int                             cAuxVecs;                   /**< Number of aux vector entries */
    PRTSOLCOREMAPINFO               pMapInfoHead;               /**< Pointer to the head of list of mappings */
    uint32_t                        cMappings;                  /**< Number of mappings (count of pMapInfoHead list) */
    PRTSOLCORETHREADINFO            pThreadInfoHead;            /**< Pointer to the head of list of threads */
    uint64_t                        cThreads;                   /**< Number of threads (count of pThreadInfoHead list) */
    char                            szPlatform[SYS_NMLN];       /**< Platform name  */
    char                            szZoneName[ZONENAME_MAX];   /**< Zone name */
    struct utsname                  UtsName;                    /**< UTS name */
    void                           *pvCred;                     /**< Process credential info. */
    size_t                          cbCred;                     /**< Size of process credential info. */
    void                           *pvLdt;                      /**< Process LDT info. */
    size_t                          cbLdt;                      /**< Size of the LDT info. */
    prpriv_t                       *pPriv;                      /**< Process privilege info. */
    size_t                          cbPriv;                     /**< Size of process privilege info. */
    const priv_impl_info_t         *pcPrivImpl;                 /**< Process privilege implementation info. (opaque handle) */
    core_content_t                  CoreContent;                /**< What information goes in the core */
#else
# error Port Me!
#endif

} RTSOLCOREPROCESS;
typedef RTSOLCOREPROCESS *PRTSOLCOREPROCESS;

typedef int (*PFNRTCOREREADER)(int fdFile, void *pv, size_t cb);
typedef int (*PFNRTCOREWRITER)(int fdhFile, const void *pcv, size_t cb);

/**
 * The solaris core file object.
 */
typedef struct RTSOLCORE
{
    char                            szCorePath[PATH_MAX];       /**< Path of the core file */
    RTSOLCOREPROCESS                SolProc;                    /**< Current process information */
    void                           *pvCore;                     /**< Pointer to memory area during dumping */
    size_t                          cbCore;                     /**< Size of memory area during dumping */
    void                           *pvFree;                     /**< Pointer to base of free range in preallocated memory area */
    bool                            fIsValid;                   /**< Whether core information has been fully collected */
    PFNRTCOREREADER                 pfnReader;                  /**< Reader function */
    PFNRTCOREWRITER                 pfnWriter;                  /**< Writer function */
    int                             fdCoreFile;                 /**< Core file (used only while writing the core) */
    RTFOFF                          offWrite;                   /**< Segment/section offset (used only while writing the core) */
} RTSOLCORE;
typedef RTSOLCORE *PRTSOLCORE;

typedef int (*PFNRTSOLCOREACCUMULATOR)(PRTSOLCORE pSolCore);
typedef int (*PFNRTSOLCORETHREADWORKER)(PRTSOLCORE pSolCore, void *pvThreadInfo);

#endif /* !IPRT_INCLUDED_SRC_r3_solaris_coredumper_solaris_h */

