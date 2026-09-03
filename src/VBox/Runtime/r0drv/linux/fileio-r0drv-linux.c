/* $Id: fileio-r0drv-linux.c 115152 2026-09-03 10:41:19Z andreas.loeffler@oracle.com $ */
/** @file
 * IPRT - File I/O, R0 Driver, Linux.
 */

/*
 * Copyright (C) 2011-2026 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "the-linux-kernel.h"
#if RTLNX_VER_MIN(2,6,13)
# include <linux/fsnotify.h>
#endif
#if RTLNX_VER_MIN(4,11,0)
# include <linux/sched/xacct.h>
#endif
#include <linux/file.h>

#include <iprt/file.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include "internal/magics.h"


#if RTLNX_VER_MIN(3,16,0)  /** @todo support this for older kernels (see also dbgkrnlinfo-r0drv-linux.c and fileio-r0drv-linux.c) */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Darwin kernel file handle data.
 */
typedef struct RTFILEINT
{
    /** Magic value (RTFILE_MAGIC). */
    uint32_t        u32Magic;
    /** The open mode flags passed to the kernel API. */
    int             fOpenMode;
    /** The open flags passed to RTFileOpen. */
    uint64_t        fOpen;
    /** The current file offset. */
    uint64_t        offFile;
    /** The linux file structure representing the opened file. */
    struct file    *pFile;
} RTFILEINT;
/** Magic number for RTFILEINT::u32Magic (Mick Herron). */
#define RTFILE_MAGIC                    UINT32_C(0x19630711)


RTDECL(int) RTFileOpen(PRTFILE phFile, const char *pszFilename, uint64_t fOpen)
{
    RTFILEINT  *pThis;
    int         fOpenMode;
    int         rc;
    struct path Path;

    *phFile = NIL_RTFILE;
    AssertReturn(!(fOpen & RTFILE_O_TEMP_AUTO_DELETE), VERR_NOT_SUPPORTED);

    /*
     * Convert flags.
     */
    fOpenMode = 0;
    if (fOpen & RTFILE_O_NON_BLOCK)
        fOpenMode |= O_NONBLOCK;
    if (fOpen & RTFILE_O_WRITE_THROUGH)
        fOpenMode |= O_SYNC;

    /* create/truncate file */
    switch (fOpen & RTFILE_O_ACTION_MASK)
    {
        case RTFILE_O_OPEN:             break;
        //case RTFILE_O_OPEN_CREATE:      fOpenMode |= O_CREAT; break;
        //case RTFILE_O_CREATE:           fOpenMode |= O_CREAT | O_EXCL; break;
        //case RTFILE_O_CREATE_REPLACE:   fOpenMode |= O_CREAT | O_TRUNC; break; /** @todo replacing needs fixing, this is *not* a 1:1 mapping! */
        default:
            AssertMsgFailedReturn(("RTFileOpen doesn't implement file creation (fOpen=%#x)\n", fOpen), VERR_NOT_IMPLEMENTED);
    }
    if (fOpen & RTFILE_O_TRUNCATE)
        fOpenMode |= O_TRUNC;

    switch (fOpen & RTFILE_O_ACCESS_MASK)
    {
        case RTFILE_O_READ:
            fOpenMode |= O_RDONLY;
            break;
        case RTFILE_O_WRITE:
            fOpenMode |= fOpen & RTFILE_O_APPEND ? O_APPEND | O_WRONLY : O_WRONLY;
            break;
        case RTFILE_O_READWRITE:
            fOpenMode |= fOpen & RTFILE_O_APPEND ? O_APPEND | O_RDWR : O_RDWR;
            break;
        default:
            AssertMsgFailedReturn(("RTFileOpen received an invalid RW value, fOpen=%#x\n", fOpen), VERR_INVALID_FLAGS);
    }

    pThis = (RTFILEINT *)RTMemAllocZ(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;
    IPRT_LINUX_SAVE_EFL_AC();

    pThis->u32Magic  = RTFILE_MAGIC;
    pThis->fOpen     = fOpen;
    pThis->fOpenMode = fOpenMode;

    /*
     * Lookup the parent directory entry.
     */
# if RTLNX_VER_MIN(2,6,28)
    rc = kern_path(pszFilename, 0, &Path);
# else
#  error "port me"
# endif
    if (!rc)
    {
        /*
         * Open it.  RHEL 9.8 replaced open_with_fake_path() with
         * kernel_file_open() between kernel releases 5.14.0-687.17.1 and
         * 5.14.0-687.19.1.  The newer API also defines FMODE_BACKING, so use
         * that macro to distinguish the two APIs.
         */
# if RTLNX_VER_MIN(6,10,0)
        struct file *pFile = kernel_file_open(&Path, fOpenMode, current_cred());
# elif RTLNX_VER_MIN(6,5,0) || (RTLNX_RHEL_RANGE(9,8, 9,99) && defined(FMODE_BACKING))
        struct file *pFile = kernel_file_open(&Path, fOpenMode, d_inode(Path.dentry), current_cred());
# elif RTLNX_VER_MIN(4,19,0)
        struct file *pFile = open_with_fake_path(&Path, fOpenMode, d_inode(Path.dentry), current_cred());
# elif RTLNX_VER_MIN(3,6,0)
        struct file *pFile = dentry_open(&Path, fOpenMode, current_cred());
# else
#  error "port me"
# endif
        path_put(&Path);
        if (!IS_ERR(pFile))
        {
            pThis->pFile = pFile;
            *phFile = pThis;
            IPRT_LINUX_RESTORE_EFL_AC();
            return VINF_SUCCESS;
        }
        rc = PTR_ERR(pFile);
    }
    rc = RTErrConvertFromErrno(-rc);

    RTMemFree(pThis);
    IPRT_LINUX_RESTORE_EFL_AC();
    return rc;
}


RTDECL(int) RTFileClose(RTFILE hFile)
{
    if (hFile == NIL_RTFILE)
        return VINF_SUCCESS;

    RTFILEINT *pThis = hFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTFILE_MAGIC, VERR_INVALID_HANDLE);
    pThis->u32Magic = ~RTFILE_MAGIC;

    /** @todo use filp_close?   */
    fput(pThis->pFile);
    pThis->pFile = NULL;

    RTMemFree(pThis);
    return VINF_SUCCESS;
}


RTDECL(int) RTFileReadAt(RTFILE hFile, RTFOFF off, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    RTFILEINT      *pThis     = hFile;
    loff_t          offNative = (loff_t)off;
    ssize_t         cbRead;
    struct file    *pFile;
    int             rc;

    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTFILE_MAGIC, VERR_INVALID_HANDLE);
    pFile = pThis->pFile;
    AssertPtrReturn(pFile, VERR_INTERNAL_ERROR_2);
    AssertPtrReturn(pFile->f_op, VERR_INTERNAL_ERROR_3);

    AssertReturn(off >= 0, VERR_OUT_OF_RANGE);
    AssertReturn((RTFOFF)offNative == off, VERR_OUT_OF_RANGE);
    IPRT_LINUX_SAVE_EFL_AC();

    /*
     * If the file has a read_iter function, it can be passed kernel buffers
     * directly and life is relatively simple...
     *
     * With Linux 5.10 they got rid of this DS_KERNEL stuff, and 'read' was
     * no longer able to handle kernel buffers.  kernel_read() started check
     * that only 'read_iter' was implemented and would fail if missing but
     * also if 'read' was implemented (claiming complicated semantics).
     */
# if RTLNX_VER_MIN(5,10,0)
    if (pFile->f_op->read_iter)
    {
        struct kvec     KVec;
        struct iov_iter IovIter;

        KVec.iov_base = pvBuf;
        KVec.iov_len  = RT_MIN(cbToRead, (size_t)MAX_RW_COUNT);
#  if defined(ITER_DEST)
        iov_iter_kvec(&IovIter, ITER_DEST, &KVec, 1, KVec.iov_len);
#  else
        iov_iter_kvec(&IovIter, READ, &KVec, 1, KVec.iov_len);
#  endif

#  if RTLNX_VER_MIN(4,13,0)
        cbRead = vfs_iter_read(pFile, &IovIter, &offNative, 0 /*fFlags*/);
#  else
        cbRead = vfs_iter_read(pFile, &IovIter, &offNative);
#  endif

# elif RTLNX_VER_MIN(4,14,0)
        cbRead = kernel_read(pThis->pFile, (char *)pvBuf, cbToRead, &offNative);
# elif RTLNX_VER_MIN(2,6,31)
        cbRead = kernel_read(pThis->pFile, offNative, (char *)pvBuf, cbToRead);
# else
        cbRead = kernel_read(pThis->pFile, (unsigned long)offNative, (char *)pvBuf, cbToRead);
# endif
        if (cbRead >= 0)
            rc = VINF_SUCCESS;
        else
            rc = RTErrConvertFromErrno((int)-cbRead);

# if RTLNX_VER_MIN(5,10,0)
    }
    /*
     * HACK ALERT! If we cannot use 'read_iter', we must try use the 'read'
     *             function directly with a temporary userland bounce buffer.
     *             This is very ugly and we know it. :-)
     */
    else
    {
        /*
         * Do pre-write checks that makes sure there is a 'read' function and
         * that the descriptor is opened in read-mode.
         */
        struct mm_struct * const pMm = current->mm;
        cbRead = 0;
        if (   !(pThis->fOpen & RTFILE_O_READ)
            || !(pFile->f_mode & (FMODE_READ | FMODE_CAN_READ))
            || !pFile->f_op->read
            || !pMm)
            rc = VERR_ACCESS_DENIED;
        else
        {
#  if RTLNX_VER_MIN(5,18,0) /** @todo any better solution for 5.10-5.17.999? */
            rc = rw_verify_area(READ, pFile, &offNative, cbToRead);
            if (!rc)
                rc = RTErrConvertFromErrno(-rc);
#  else
            rc = VINF_SUCCESS;
#  endif
        }
        if (RT_SUCCESS(rc))
        {
            /*
             * Allocate a page and map it into the user context.
             */
            struct page *pPage = alloc_page(GFP_USER | __GFP_ZERO);
            if (pPage)
            {
                void * const        pvKrnlAddr = phys_to_virt(page_to_phys(pPage));
                unsigned long const ulAddr     = vm_mmap(NULL, 0, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0);
                //printk("pvKrnlAddr=%#lx pPage=%#lx phys=%#lx; user ulAddr=%lx\n", (long)pvKrnlAddr, (long)pPage, (long)page_to_phys(pPage), ulAddr);
                if (!(ulAddr & ~PAGE_MASK)) /* ~PAGE_MASK == PAGE_OFFSET_MASK */
                {
                    struct vm_area_struct *vma;
                    LNX_MM_DOWN_WRITE(pMm);
                    vma = find_vma(pMm, ulAddr); /* this is probably the same for all the pages... */
                    if (vma)
                    {
                        pgprot_t fPg = PAGE_SHARED; /* not entirely safe, but PAGE_KERNEL doesn't work */
                        SetPageReserved(pPage);
                        rc = remap_pfn_range(vma, ulAddr, page_to_pfn(pPage), PAGE_SIZE, fPg);
                        LNX_MM_UP_WRITE(pMm);
                        if (!rc)
                        {
                            /*
                             * Bounce the read request via this user buffer.
                             */
                            char __user * const pbUserAddr = (char __user *)ulAddr;
                            size_t cbLeftToRead = cbToRead;
                            while (cbLeftToRead > 0)
                            {
                                size_t  const cbCurToRead = RT_MIN(cbLeftToRead, PAGE_SIZE);
                                //printk("Calling f_op->read(%#lx, %#lx, %#lx, %#lx(=%#lx))\n", (long)pFile, (long)pbUserAddr, (long)cbCurToRead, (long)&offNative, (long)offNative);
                                ssize_t const cbCurRead   = pFile->f_op->read(pFile, pbUserAddr, cbCurToRead, &offNative);
                                //printk("f_op->read -> %ld offNative=%lx\n", (long)cbCurRead, (long)offNative);
                                if (cbCurRead > 0)
                                {
                                    memcpy(pvBuf, pvKrnlAddr, cbCurRead);
                                    memset(pvKrnlAddr, 0,  cbCurRead);
# if RTLNX_VER_MIN(2,6,36)
                                    fsnotify_access(pFile);
# else
                                    fsnotify_access(pFile->f_path.dentry);
# endif
                                    pvBuf         = (char *)pvBuf + cbCurRead;
                                    cbRead       += cbCurRead;
                                    cbLeftToRead -= (size_t)cbCurRead;
                                }
                                else
                                {
                                    if (cbCurRead != 0)
                                        rc = RTErrConvertFromErrno((int)-cbCurRead);
                                    break;
                                }
                            }
# if RTLNX_VER_MIN(4,11,0)
                            if (cbRead > 0)
                                add_rchar(current, cbRead);
                            inc_syscr(current);
# endif
                        }
                        else
                            rc = VERR_MAP_FAILED;
                        ClearPageReserved(pPage);
                    }
                    else
                    {
                        LNX_MM_UP_WRITE(pMm);
                        rc = VERR_MAP_FAILED;
                    }
                    vm_munmap(ulAddr, PAGE_SIZE);
                }
                else
                    rc = VERR_MAP_FAILED;
                __free_page(pPage);
            }
            else
                rc = VERR_NO_PAGE_MEMORY;
        }
    }
# endif /* >= 4.0.0 */
    if (RT_SUCCESS(rc))
    {
        rc = VINF_SUCCESS;

        pThis->offFile = (uint64_t)off + (uint64_t)cbRead;
        if (pThis->offFile < (uint64_t)off)
            rc = VERR_FILE_IO_ERROR;

        if (pcbRead)
            *pcbRead = (size_t)cbRead;
        else if ((size_t)cbRead != cbToRead)
            rc = VERR_EOF;
    }

    IPRT_LINUX_RESTORE_EFL_AC();
    return rc;
}


RTDECL(int) RTFileRead(RTFILE hFile, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    RTFILEINT *pThis = hFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTFILE_MAGIC, VERR_INVALID_HANDLE);

    return RTFileReadAt(hFile, pThis->offFile, pvBuf, cbToRead, pcbRead);
}


RTDECL(int) RTFileQuerySize(RTFILE hFile, uint64_t *pcbSize)
{
    RTFILEINT      *pThis = hFile;
# if RTLNX_VER_MIN(2,5,22)
    struct kstat    Stats;
# endif
    int             rc;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTFILE_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Query the data size attribute.
     */
    RT_ZERO(Stats);
# if RTLNX_VER_MIN(4,11,0)
    rc = vfs_getattr(&pThis->pFile->f_path, &Stats, STATX_BASIC_STATS, 0);
# elif RTLNX_VER_MIN(3,9,0)
    rc = vfs_getattr(&pThis->pFile->f_path, &Stats);
# elif RTLNX_VER_MIN(2,5,22)
    rc = vfs_getattr(pThis->pFile->f_vfsmnt, pThis->pFile->f_dentry, &Stats);
# else
    rc = -ENOSYS;
# endif
    if (!rc)
    {
        *pcbSize = Stats.size;
        return VINF_SUCCESS;
    }

    return RTErrConvertFromErrno(rc);
}


RTDECL(int) RTFileSeek(RTFILE hFile, int64_t offSeek, unsigned uMethod, uint64_t *poffActual)
{
    RTFILEINT *pThis = hFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTFILE_MAGIC, VERR_INVALID_HANDLE);

    uint64_t offNew;
    switch (uMethod)
    {
        case RTFILE_SEEK_BEGIN:
            AssertReturn(offSeek >= 0, VERR_NEGATIVE_SEEK);
            offNew = offSeek;
            break;

        case RTFILE_SEEK_CURRENT:
            offNew = pThis->offFile + offSeek;
            break;

        case RTFILE_SEEK_END:
        {
            uint64_t cbFile = 0;
            int rc = RTFileQuerySize(hFile, &cbFile);
            if (RT_SUCCESS(rc))
                offNew = cbFile + offSeek;
            else
                return rc;
            break;
        }

        default:
            return VERR_INVALID_PARAMETER;
    }

    if ((RTFOFF)offNew >= 0)
    {
        pThis->offFile = offNew;
        if (poffActual)
            *poffActual = offNew;
        return VINF_SUCCESS;
    }
    return VERR_NEGATIVE_SEEK;
}

#endif /* RTLNX_VER_MIN(6,7,0) */

