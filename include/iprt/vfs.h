/** @file
 * IPRT - Virtual Filesystem.
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

#ifndef IPRT_INCLUDED_vfs_h
#define IPRT_INCLUDED_vfs_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/dir.h>
#include <iprt/fs.h>
#include <iprt/handle.h>
#include <iprt/symlink.h>
#include <iprt/sg.h>
#include <iprt/time.h>


RT_C_DECLS_BEGIN

/** @defgroup grp_rt_vfs   RTVfs - Virtual Filesystem
 * @ingroup grp_rt
 *
 * The virtual filesystem APIs are intended to make it possible to work on
 * container files, file system sub-trees, file system overlays and other custom
 * filesystem configurations.  It also makes it possible to create filters, like
 * automatically gunzipping a tar.gz file before feeding it to the RTTar API for
 * unpacking - or vice versa.
 *
 * The virtual filesystem APIs are intended to mirror the RTDir, RTFile, RTPath
 * and RTFs APIs pretty closely so that rewriting a piece of code to work with
 * it should be easy.  However there are some differences to the way the APIs
 * works and the user should heed the documentation.  The differences are
 * usually motivated by simplification and in some case to make the VFS more
 * flexible.
 *
 * @{
 */

/**
 * The object type.
 */
typedef enum RTVFSOBJTYPE
{
    /** Invalid type. */
    RTVFSOBJTYPE_INVALID = 0,
    /** Pure base object.
     * This is returned by the filesystem stream to represent directories,
     * devices, fifos and similar that needs to be created. */
    RTVFSOBJTYPE_BASE,
    /** Virtual filesystem. */
    RTVFSOBJTYPE_VFS,
    /** Filesystem stream. */
    RTVFSOBJTYPE_FS_STREAM,
    /** Pure I/O stream. */
    RTVFSOBJTYPE_IO_STREAM,
    /** Directory. */
    RTVFSOBJTYPE_DIR,
    /** File. */
    RTVFSOBJTYPE_FILE,
    /** Symbolic link. */
    RTVFSOBJTYPE_SYMLINK,
    /** End of valid object types. */
    RTVFSOBJTYPE_END,
    /** Pure I/O stream. */
    RTVFSOBJTYPE_32BIT_HACK = 0x7fffffff
} RTVFSOBJTYPE;
/** Pointer to a VFS object type. */
typedef RTVFSOBJTYPE *PRTVFSOBJTYPE;

/**
 * Translates a RTVFSOBJTYPE value into a string.
 *
 * @returns Pointer to readonly name.
 * @param   enmType             The object type to name.
 */
RTDECL(const char *) RTVfsTypeName(RTVFSOBJTYPE enmType);



/** @name RTVfsCreate flags
 * @{ */
/** Whether the file system is read-only. */
#define RTVFS_C_READONLY                RT_BIT(0)
/** Whether we the VFS should be thread safe (i.e. automaticaly employ
 * locks). */
#define RTVFS_C_THREAD_SAFE             RT_BIT(1)
/** @}  */

/**
 * Creates an empty virtual filesystem.
 *
 * @returns IPRT status code.
 * @param   pszName     Name, for logging and such.
 * @param   fFlags      Flags, MBZ.
 * @param   phVfs       Where to return the VFS handle.  Release the returned
 *                      reference by calling RTVfsRelease.
 */
RTDECL(int)         RTVfsCreate(const char *pszName, uint32_t fFlags, PRTVFS phVfs);
RTDECL(uint32_t)    RTVfsRetain(RTVFS hVfs);
RTDECL(uint32_t)    RTVfsRetainDebug(RTVFS hVfs, RT_SRC_POS_DECL);
RTDECL(uint32_t)    RTVfsRelease(RTVFS hVfs);

/** @name RTVFSMNT_F_XXX - Flags for RTVfsMount
 * @{ */
/** Mount read-only. */
#define RTVFSMNT_F_READ_ONLY            RT_BIT_32(0)
/** Purpose is . */
#define RTVFSMNT_F_FOR_RANGE_IN_USE     RT_BIT_32(1)
/** Valid mask. */
#define RTVFSMNT_F_VALID_MASK           UINT32_C(0x00000003)
/** @} */

/**
 * Does the file system detection and mounting.
 *
 * @returns IPRT status code.
 * @retval  VERR_VFS_UNSUPPORTED_FORMAT if not recognized as a support file
 *          system.
 * @param   hVfsFileIn      The file handle of the volume.
 * @param   fFlags          RTVFSMTN_F_XXX.
 * @param   phVfs           Where to return the VFS handle on success.
 * @param   pErrInfo        Where to return additional error information.
 *                          Optional.
 */
RTDECL(int)         RTVfsMountVol(RTVFSFILE hVfsFileIn, uint32_t fFlags, PRTVFS phVfs, PRTERRINFO pErrInfo);

RTDECL(int)         RTVfsAttach(RTVFS hVfs, const char *pszMountPoint, uint32_t fFlags, RTVFS hVfsAttach);
RTDECL(int)         RTVfsDetach(RTVFS hVfs, const char *pszMountPoint, RTVFS hVfsToDetach, PRTVFS *phVfsDetached);
RTDECL(uint32_t)    RTVfsGetAttachmentCount(RTVFS hVfs);
RTDECL(int)         RTVfsGetAttachment(RTVFS hVfs, uint32_t iOrdinal, PRTVFS *phVfsAttached, uint32_t *pfFlags,
                                       char *pszMountPoint, size_t cbMountPoint);

/**
 * Opens the root director of the given VFS.
 *
 * @returns IPRT status code.
 * @param   hVfs        VFS handle.
 * @param   phDir       Where to return the root directory handle.
 */
RTDECL(int) RTVfsOpenRoot(RTVFS hVfs, PRTVFSDIR phDir);

/**
 * Queries information about a object in the virtual filesystem.
 *
 * @returns IPRT Status code.
 * @param   hVfs        VFS handle.
 * @param   pszPath     Path to the object, relative to the VFS root.
 * @param   pObjInfo    Where to return info.
 * @param   enmAddAttr  What to return.
 * @param   fFlags      RTPATH_F_XXX.
 * @sa      RTPathQueryInfoEx, RTVfsDirQueryPathInfo, RTVfsObjQueryInfo
 */
RTDECL(int) RTVfsQueryPathInfo(RTVFS hVfs, const char *pszPath, PRTFSOBJINFO pObjInfo,
                               RTFSOBJATTRADD enmAddAttr, uint32_t fFlags);

/**
 * Checks whether a given range is in use by the virtual filesystem.
 *
 * @returns IPRT status code.
 * @param   hVfs        VFS handle.
 * @param   off         Start offset to check.
 * @param   cb          Number of bytes to check.
 * @param   pfUsed      Where to store the result.
 */
RTDECL(int) RTVfsQueryRangeState(RTVFS hVfs, uint64_t off, size_t cb, bool *pfUsed);

/**
 * Queries the volume label.
 *
 * @returns IPRT status code.
 * @param   hVfs            VFS handle.
 * @param   fAlternative    For use with ISO files to retrieve the primary lable
 *                          rather than the joliet / UDF one that the mount
 *                          options would indicate.  For other file systems, as
 *                          well for ISO not mounted in joliet / UDF mode, the
 *                          flag is ignored.
 * @param   pszLabel        Where to store the lable.
 * @param   cbLabel         Size of the buffer @a pszLable points at.
 * @param   pcbActual       Where to return the label length, including the
 *                          terminator.  In case of VERR_BUFFER_OVERFLOW
 *                          returns, this will be set to the required buffer
 *                          size.  Optional.
 */
RTDECL(int) RTVfsQueryLabel(RTVFS hVfs, bool fAlternative, char *pszLabel, size_t cbLabel, size_t *pcbActual);


/** @defgroup grp_rt_vfs_obj        VFS Base Object API
 * @{
 */

/**
 * Retains a reference to the VFS base object handle.
 *
 * @returns New reference count on success, UINT32_MAX on failure.
 * @param   hVfsObj         The VFS base object handle.
 */
RTDECL(uint32_t)        RTVfsObjRetain(RTVFSOBJ hVfsObj);
RTDECL(uint32_t)        RTVfsObjRetainDebug(RTVFSOBJ hVfsObj, RT_SRC_POS_DECL);

/**
 * Releases a reference to the VFS base handle.
 *
 * @returns New reference count on success (0 if closed), UINT32_MAX on failure.
 * @param   hVfsObj         The VFS base object handle.
 */
RTDECL(uint32_t)        RTVfsObjRelease(RTVFSOBJ hVfsObj);

/** @name RTVFSOBJ_F_XXX - Flags or RTVfsObjOpen and RTVfsDirOpenObj.
 * @note Must leave space for RTPATH_F_XXX.
 * @{ */
/** Directory (RTFS_TYPE_DIRECTORY). */
#define RTVFSOBJ_F_OPEN_DIRECTORY           RT_BIT_32(8)
/** Symbolic link (RTFS_TYPE_SYMLINK). */
#define RTVFSOBJ_F_OPEN_SYMLINK             RT_BIT_32(9)
/** Regular file (RTFS_TYPE_FILE). */
#define RTVFSOBJ_F_OPEN_FILE                RT_BIT_32(10)
/** Character device (RTFS_TYPE_DEV_CHAR). */
#define RTVFSOBJ_F_OPEN_DEV_CHAR            RT_BIT_32(11)
/** Block device (RTFS_TYPE_DEV_BLOCK). */
#define RTVFSOBJ_F_OPEN_DEV_BLOCK           RT_BIT_32(12)
/** Named pipe (fifo) (RTFS_TYPE_FIFO). */
#define RTVFSOBJ_F_OPEN_FIFO                RT_BIT_32(13)
/** Socket (RTFS_TYPE_SOCKET). */
#define RTVFSOBJ_F_OPEN_SOCKET              RT_BIT_32(14)
/** Mounted VFS. */
#define RTVFSOBJ_F_OPEN_MOUNT               RT_BIT_32(15)
/** Mask object types we wish to open. */
#define RTVFSOBJ_F_OPEN_MASK                UINT32_C(0x0000ff00)
/** Any kind of object that translates to RTVFSOBJTYPE_FILE. */
#define RTVFSOBJ_F_OPEN_ANY_FILE            (RTVFSOBJ_F_OPEN_FILE | RTVFSOBJ_F_OPEN_DEV_BLOCK)
/** Any kind of object that translates to RTVFSOBJTYPE_IOS or
 *  RTVFSOBJTYPE_FILE. */
#define RTVFSOBJ_F_OPEN_ANY_IO_STREAM       (  RTVFSOBJ_F_ANY_OPEN_FILE | RTVFSOBJ_F_DEV_OPEN_BLOCK \
                                             | RTVFSOBJ_F_OPEN_FIFO     | RTVFSOBJ_F_OPEN_SOCKET)
/** Any kind of object. */
#define RTVFSOBJ_F_OPEN_ANY                 RTVFSOBJ_F_OPEN_MASK

/** Do't create anything, return file not found. */
#define RTVFSOBJ_F_CREATE_NOTHING           UINT32_C(0x00000000)
/** Create a file if the if the object was not found and the RTFILE_O_XXX
 * flags allows it. */
#define RTVFSOBJ_F_CREATE_FILE              UINT32_C(0x00010000)
/** Create a directory if the object was not found and the RTFILE_O_XXX
 * flags allows it. */
#define RTVFSOBJ_F_CREATE_DIRECTORY         UINT32_C(0x00020000)
/** The creation type mask. */
#define RTVFSOBJ_F_CREATE_MASK              UINT32_C(0x00070000)

/** Indicate that this call is for traversal.
 * @internal only  */
#define RTVFSOBJ_F_TRAVERSAL                RT_BIT_32(31)
/** Valid mask for external callers. */
#define RTVFSOBJ_F_VALID_MASK               UINT32_C(0x0007ff00)
/** @} */

/**
 * Opens any file system object in the given VFS.
 *
 * @returns IPRT status code.
 * @param   hVfs            The VFS to open the object within.
 * @param   pszPath         Path to the file.
 * @param   fFileOpen       RTFILE_O_XXX flags.
 * @param   fObjFlags       More flags: RTVFSOBJ_F_XXX, RTPATH_F_XXX.
 * @param   phVfsObj        Where to return the object handle.
 * @sa      RTVfsDirOpenObj, RTVfsDirOpenDir, RTVfsDirOpenFile
 */
RTDECL(int)             RTVfsObjOpen(RTVFS hVfs, const char *pszPath, uint64_t fFileOpen, uint32_t fObjFlags, PRTVFSOBJ phVfsObj);

/**
 * Query information about the object.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_SUPPORTED if the @a enmAddAttr value is not handled by the
 *          implementation.
 *
 * @param   hVfsObj         The VFS object handle.
 * @param   pObjInfo        Where to return the info.
 * @param   enmAddAttr      Which additional attributes should be retrieved.
 * @sa      RTVfsIoStrmQueryInfo, RTVfsFileQueryInfo, RTFileQueryInfo,
 *          RTPathQueryInfo
 */
RTDECL(int)             RTVfsObjQueryInfo(RTVFSOBJ hVfsObj, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr);

/**
 * Sets the file mode for the given VFS object.
 *
 * @returns IPRT status code.
 * @retval  VERR_INVALID_FUNCTION if the object type has no file mode to set.
 *          Only directories, files and symbolic links support this operation.
 *
 * @param   hVfsObj         The VFS object handle.
 * @param   fMode           The mode mask.
 * @param   fMask           The bits in the mode mask which should be changed.
 */
RTDECL(int)             RTVfsObjSetMode(RTVFSOBJ hVfsObj, RTFMODE fMode, RTFMODE fMask);

/**
 * Sets one or more timestamps for the given VFS object.
 *
 * @returns IPRT status code.
 * @retval  VERR_INVALID_FUNCTION if the object type has no file mode to set.
 *          Only directories, files and symbolic links support this operation.
 *
 * @param   hVfsObj             The VFS object handle.
 * @param   pAccessTime         Pointer to the new access time. NULL if not to
 *                              be changed.
 * @param   pModificationTime   Pointer to the new modifcation time. NULL if not
 *                              to be changed.
 * @param   pChangeTime         Pointer to the new change time. NULL if not to
 *                              be changed.
 * @param   pBirthTime          Pointer to the new time of birth. NULL if not to
 *                              be changed.
 *
 * @remarks See RTFileSetTimes for restrictions and behavior imposed by the
 *          host OS or underlying VFS provider.
 * @sa      RTFileSetTimes, RTPathSetTimes
 */
RTDECL(int)             RTVfsObjSetTimes(RTVFSOBJ hVfsObj, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                                         PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime);

/**
 * Set the unix style owner and group on the given VFS object.
 *
 * @returns IPRT status code.
 * @retval  VERR_INVALID_FUNCTION if the object type has no file mode to set.
 *          Only directories, files and symbolic links support this operation.
 *
 * @param   hVfsObj         The VFS object handle.
 * @param   uid             The user ID of the new owner.  NIL_RTUID if
 *                          unchanged.
 * @param   gid             The group ID of the new owner group. NIL_RTGID if
 *                          unchanged.
 *
 * @sa      RTFileSetOwner, RTPathSetOwner.
 */
RTDECL(int)             RTVfsObjSetOwner(RTVFSOBJ hVfsObj, RTUID uid, RTGID gid);


/**
 * Gets the type of a VFS object.
 *
 * @returns The VFS object type on success, RTVFSOBJTYPE_INVALID on failure.
 * @param   hVfsObj         The VFS base object handle.
 */
RTDECL(RTVFSOBJTYPE)    RTVfsObjGetType(RTVFSOBJ hVfsObj);

/**
 * Converts a VFS base object handle to a VFS handle.
 *
 * @returns Referenced handle on success, NIL on failure.
 * @param   hVfsObj         The VFS base object handle.
 */
RTDECL(RTVFS)           RTVfsObjToVfs(RTVFSOBJ hVfsObj);

/**
 * Converts a VFS base object handle to a VFS filesystem stream handle.
 *
 * @returns Referenced handle on success, NIL on failure.
 * @param   hVfsObj         The VFS base object handle.
 */
RTDECL(RTVFSFSSTREAM)   RTVfsObjToFsStream(RTVFSOBJ hVfsObj);

/**
 * Converts a VFS base object handle to a VFS directory handle.
 *
 * @returns Referenced handle on success, NIL on failure.
 * @param   hVfsObj         The VFS base object handle.
 */
RTDECL(RTVFSDIR)        RTVfsObjToDir(RTVFSOBJ hVfsObj);

/**
 * Converts a VFS base object handle to a VFS I/O stream handle.
 *
 * @returns Referenced handle on success, NIL on failure.
 * @param   hVfsObj         The VFS base object handle.
 */
RTDECL(RTVFSIOSTREAM)   RTVfsObjToIoStream(RTVFSOBJ hVfsObj);

/**
 * Converts a VFS base object handle to a VFS file handle.
 *
 * @returns Referenced handle on success, NIL on failure.
 * @param   hVfsObj         The VFS base object handle.
 */
RTDECL(RTVFSFILE)       RTVfsObjToFile(RTVFSOBJ hVfsObj);

/**
 * Converts a VFS base object handle to a VFS symbolic link handle.
 *
 * @returns Referenced handle on success, NIL on failure.
 * @param   hVfsObj         The VFS base object handle.
 */
RTDECL(RTVFSSYMLINK)    RTVfsObjToSymlink(RTVFSOBJ hVfsObj);


/**
 * Converts a VFS handle to a VFS base object handle.
 *
 * @returns Referenced handle on success, NIL if the input handle was invalid.
 * @param   hVfs            The VFS handle.
 */
RTDECL(RTVFSOBJ)        RTVfsObjFromVfs(RTVFS hVfs);

/**
 * Converts a VFS filesystem stream handle to a VFS base object handle.
 *
 * @returns Referenced handle on success, NIL if the input handle was invalid.
 * @param   hVfsFss         The VFS filesystem stream handle.
 */
RTDECL(RTVFSOBJ)        RTVfsObjFromFsStream(RTVFSFSSTREAM hVfsFss);

/**
 * Converts a VFS directory handle to a VFS base object handle.
 *
 * @returns Referenced handle on success, NIL if the input handle was invalid.
 * @param   hVfsDir          The VFS directory handle.
 */
RTDECL(RTVFSOBJ)        RTVfsObjFromDir(RTVFSDIR hVfsDir);

/**
 * Converts a VFS I/O stream handle to a VFS base object handle.
 *
 * @returns Referenced handle on success, NIL if the input handle was invalid.
 * @param   hVfsIos          The VFS I/O stream handle.
 */
RTDECL(RTVFSOBJ)        RTVfsObjFromIoStream(RTVFSIOSTREAM hVfsIos);

/**
 * Converts a VFS file handle to a VFS base object handle.
 *
 * @returns Referenced handle on success, NIL if the input handle was invalid.
 * @param   hVfsFile         The VFS file handle.
 */
RTDECL(RTVFSOBJ)        RTVfsObjFromFile(RTVFSFILE hVfsFile);

/**
 * Converts a VFS symbolic link handle to a VFS base object handle.
 *
 * @returns Referenced handle on success, NIL if the input handle was invalid.
 * @param   hVfsSym            The VFS symbolic link handle.
 */
RTDECL(RTVFSOBJ)        RTVfsObjFromSymlink(RTVFSSYMLINK hVfsSym);

/** @} */


/** @defgroup grp_rt_vfs_fsstream   VFS Filesystem Stream API
 *
 * Filesystem streams are for tar, cpio and similar.  Any virtual filesystem can
 * be turned into a filesystem stream using RTVfsFsStrmFromVfs.
 *
 * @{
 */

RTDECL(uint32_t)    RTVfsFsStrmRetain(RTVFSFSSTREAM hVfsFss);
RTDECL(uint32_t)    RTVfsFsStrmRetainDebug(RTVFSFSSTREAM hVfsFss, RT_SRC_POS_DECL);
RTDECL(uint32_t)    RTVfsFsStrmRelease(RTVFSFSSTREAM hVfsFss);
RTDECL(int)         RTVfsFsStrmQueryInfo(RTVFSFSSTREAM hVfsFss, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr);

/**
 * Gets the next object in the stream.
 *
 * This call may affect the stream posision of a previously returned object.
 *
 * The type of object returned here typically boils down to three types:
 *      - I/O streams (representing files),
 *      - symbolic links
 *      - base object
 * The base objects represent anything not convered by the two other, i.e.
 * directories, device nodes, fifos, sockets and whatnot.  The details can be
 * queried using RTVfsObjQueryInfo.
 *
 * That said, absolutely any object except for filesystem stream objects can be
 * returned by this call.  Any generic code is adviced to just deal with it all.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS if a new object was retrieved.
 * @retval  VERR_EOF when there are no more objects.
 * @retval  VERR_INVALID_FUNCTION if called on a non-readable stream.
 *
 * @param   hVfsFss     The file system stream handle.
 * @param   ppszName    Where to return the object name.  Must be freed by
 *                      calling RTStrFree.
 * @param   penmType    Where to return the object type.
 * @param   phVfsObj    Where to return the object handle (referenced). This
 *                      must be cast to the desired type before use.
 */
RTDECL(int)         RTVfsFsStrmNext(RTVFSFSSTREAM hVfsFss, char **ppszName, RTVFSOBJTYPE *penmType, PRTVFSOBJ phVfsObj);

/**
 * Appends a VFS object to the stream.
 *
 * The stream must be writable.
 *
 * @returns IPRT status code.
 * @retval  VERR_INVALID_FUNCTION if called on a non-writable stream.
 * @param   hVfsFss     The file system stream handle.
 * @param   pszPath     The path.
 * @param   hVfsObj     The VFS object to add.
 * @param   fFlags      RTVFSFSSTRM_ADD_F_XXX.
 */
RTDECL(int)         RTVfsFsStrmAdd(RTVFSFSSTREAM hVfsFss, const char *pszPath, RTVFSOBJ hVfsObj, uint32_t fFlags);

/** @name RTVFSFSSTRM_ADD_F_XXX - Flags for RTVfsFsStrmAdd.
 * @{ */
/** Input is an I/O stream of indeterminate length, read to the end and then
 * update the file header.
 * @note This is *only* possible if the output stream is actually a file. */
#define RTVFSFSSTRM_ADD_F_STREAM         RT_BIT_32(0)
/** Mask of flags specific to the target stream. */
#define RTVFSFSSTRM_ADD_F_SPECIFIC_MASK  UINT32_C(0xff000000)
/** Valid bits. */
#define RTVFSFSSTRM_ADD_F_VALID_MASK     UINT32_C(0xff000001)
/** @} */

/**
 * Pushes an byte stream onto the stream.
 *
 * The stream must be writable.
 *
 * This differs from RTVfsFsStrmAdd() in that it will create a regular file in
 * the output file system stream and provide the actual content bytes via the
 * returned I/O stream object.
 *
 * @returns IPRT status code.
 * @retval  VERR_INVALID_FUNCTION if called on a non-writable stream.
 * @param   hVfsFss     The file system stream handle.
 * @param   pszPath     The path to the file.
 * @param   cbFile      The file size.  This can also be set to UINT64_MAX if
 *                      the file system stream is backed by a file.
 * @param   paObjInfo   Array of zero or more RTFSOBJINFO structures containing
 *                      different pieces of information about the file.  If any
 *                      provided, the first one should be a RTFSOBJATTRADD_UNIX
 *                      one, additional can be supplied if wanted.  What exactly
 *                      is needed depends on the underlying FS stream
 *                      implementation.
 * @param   cObjInfo    Number of items in the array @a paObjInfo points at.
 * @param   fFlags      RTVFSFSSTRM_PUSH_F_XXX.
 * @param   phVfsIos    Where to return the I/O stream to feed the file content
 *                      to.  If the FS stream is backed by a file, the returned
 *                      handle can be cast to a file if necessary.
 */
RTDECL(int)         RTVfsFsStrmPushFile(RTVFSFSSTREAM hVfsFss, const char *pszPath, uint64_t cbFile,
                                        PCRTFSOBJINFO paObjInfo, uint32_t cObjInfo, uint32_t fFlags, PRTVFSIOSTREAM phVfsIos);

/** @name RTVFSFSSTRM_PUSH_F_XXX - Flags for RTVfsFsStrmPushFile.
 * @{ */
/** Input is an I/O stream of indeterminate length, read to the end and then
 * update the file header.
 * @note This is *only* possible if the output stream is actually a file. */
#define RTVFSFSSTRM_PUSH_F_STREAM        RT_BIT_32(0)
/** Mask of flags specific to the target stream. */
#define RTVFSFSSTRM_PUSH_F_SPECIFIC_MASK UINT32_C(0xff000000)
/** Valid bits. */
#define RTVFSFSSTRM_PUSH_F_VALID_MASK    UINT32_C(0xff000001)
/** @} */

/**
 * Marks the end of the stream.
 *
 * The stream must be writable.
 *
 * @returns IPRT status code.
 * @retval  VERR_INVALID_FUNCTION if called on a non-writable stream.
 * @param   hVfsFss     The file system stream handle.
 */
RTDECL(int)         RTVfsFsStrmEnd(RTVFSFSSTREAM hVfsFss);

/** @} */


/** @defgroup grp_rt_vfs_dir        VFS Directory API
 * @{
 */

/**
 * Retains a reference to the VFS directory handle.
 *
 * @returns New reference count on success, UINT32_MAX on failure.
 * @param   hVfsDir         The VFS directory handle.
 */
RTDECL(uint32_t)    RTVfsDirRetain(RTVFSDIR hVfsDir);
RTDECL(uint32_t)    RTVfsDirRetainDebug(RTVFSDIR hVfsDir, RT_SRC_POS_DECL);

/**
 * Releases a reference to the VFS directory handle.
 *
 * @returns New reference count on success (0 if closed), UINT32_MAX on failure.
 * @param   hVfsDir         The VFS directory handle.
 */
RTDECL(uint32_t)    RTVfsDirRelease(RTVFSDIR hVfsDir);

/**
 * Opens a directory in the specified file system.
 *
 * @returns IPRT status code.
 * @param   hVfs            The VFS to open the directory within.
 * @param   pszPath         Path to the directory, relative to the root.
 * @param   fFlags          Reserved, MBZ.
 * @param   phVfsDir        Where to return the directory.
 */
RTDECL(int) RTVfsDirOpen(RTVFS hVfs, const char *pszPath, uint32_t fFlags, PRTVFSDIR phVfsDir);

/**
 * Opens any file system object in or under the given directory.
 *
 * @returns IPRT status code.
 * @param   hVfsDir         The VFS directory start walking the @a pszPath
 *                          relative to.
 * @param   pszPath         Path to the file.
 * @param   fFileOpen       RTFILE_O_XXX flags.
 * @param   fObjFlags       More flags: RTVFSOBJ_F_XXX, RTPATH_F_XXX.
 * @param   phVfsObj        Where to return the object handle.
 * @sa      RTVfsObjOpen, RTVfsDirOpenDir, RTVfsDirOpenFile
 */
RTDECL(int) RTVfsDirOpenObj(RTVFSDIR hVfsDir, const char *pszPath, uint64_t fFileOpen, uint32_t fObjFlags, PRTVFSOBJ phVfsObj);

/**
 * Opens a file in or under the given directory.
 *
 * @returns IPRT status code.
 * @param   hVfsDir         The VFS directory start walking the @a pszPath
 *                          relative to.
 * @param   pszPath         Path to the file.
 * @param   fOpen           RTFILE_O_XXX flags.
 * @param   phVfsFile       Where to return the file.
 * @sa      RTVfsDirOpenFileAsIoStream
 */
RTDECL(int) RTVfsDirOpenFile(RTVFSDIR hVfsDir, const char *pszPath, uint64_t fOpen, PRTVFSFILE phVfsFile);

/**
 * Convenience wrapper around RTVfsDirOpenFile that returns an I/O stream.
 *
 * @returns IPRT status code.
 * @param   hVfsDir         The VFS directory start walking the @a pszPath
 *                          relative to.
 * @param   pszPath         Path to the file.
 * @param   fOpen           RTFILE_O_XXX flags.
 * @param   phVfsIos        Where to return the I/O stream handle of the file.
 * @sa      RTVfsDirOpenFile
 */
RTDECL(int) RTVfsDirOpenFileAsIoStream(RTVFSDIR hVfsDir, const char *pszPath, uint64_t fOpen, PRTVFSIOSTREAM phVfsIos);

/**
 * Opens a directory in or under the given directory.
 *
 * @returns IPRT status code.
 * @param   hVfsDir         The VFS directory start walking the @a pszPath
 *                          relative to.
 * @param   pszPath         Path to the file.
 * @param   fFlags          Reserved, MBZ.
 * @param   phVfsDir        Where to return the directory.
 */
RTDECL(int) RTVfsDirOpenDir(RTVFSDIR hVfsDir, const char *pszPath, uint32_t fFlags, PRTVFSDIR phVfsDir);

/**
 * Creates a directory relative to @a hVfsDir.
 *
 * @returns IPRT status code
 * @param   hVfsDir             The directory the path is relative to.
 * @param   pszRelPath          The relative path to the new directory.
 * @param   fMode               The file mode for the new directory.
 * @param   fFlags              Directory creation flags, RTDIRCREATE_FLAGS_XXX.
 * @param   phVfsDir            Where to return the handle to the newly created
 *                              directory.  Optional.
 * @sa      RTDirCreate, RTDirRelDirCreate
 */
RTDECL(int) RTVfsDirCreateDir(RTVFSDIR hVfsDir, const char *pszRelPath, RTFMODE fMode, uint32_t fFlags, PRTVFSDIR phVfsDir);

/**
 * Create a VFS directory handle from a standard IPRT directory handle (RTDIR).
 *
 * @returns IPRT status code.
 * @param   hDir            The standard IPRT directory handle.
 * @param   fLeaveOpen      Whether to leave the handle open when the VFS
 *                          directory is released, or to close it (@c false).
 * @param   phVfsDir        Where to return the VFS directory handle.
 */
RTDECL(int) RTVfsDirFromRTDir(RTDIR hDir, bool fLeaveOpen, PRTVFSDIR phVfsDir);

/**
 * RTDirOpen + RTVfsDirFromRTDir.
 *
 * @returns IPRT status code.
 * @param   pszPath         The path to the directory.
 * @param   fFlags          RTDIR_F_XXX.
 * @param   phVfsDir        Where to return the VFS directory handle.
 */
RTDECL(int) RTVfsDirOpenNormal(const char *pszPath, uint32_t fFlags, PRTVFSDIR phVfsDir);

/** Checks if @a hVfsDir was opened using RTVfsDirOpenNormal() or
 *  RTVfsDirFromRTDir(), either directly or indirectly. */
RTDECL(bool) RTVfsDirIsStdDir(RTVFSDIR hVfsDir);

/**
 * Queries information about a object in or under the given directory.
 *
 * @returns IPRT Status code.
 * @param   hVfsDir         The VFS directory start walking the @a pszPath
 *                          relative to.
 * @param   pszPath         Path to the object.
 * @param   pObjInfo        Where to return info.
 * @param   enmAddAttr      What to return.
 * @param   fFlags          RTPATH_F_XXX.
 * @sa      RTPathQueryInfoEx, RTVfsQueryPathInfo, RTVfsObjQueryInfo
 */
RTDECL(int) RTVfsDirQueryPathInfo(RTVFSDIR hVfsDir, const char *pszPath, PRTFSOBJINFO pObjInfo,
                                  RTFSOBJATTRADD enmAddAttr, uint32_t fFlags);

/**
 * Removes a directory relative to @a hVfsDir.
 *
 * @returns IPRT status code.
 * @param   hVfsDir         The VFS directory to start walking the @a pszRelPath
 *                          relative to.
 * @param   pszRelPath      The path to the directory that should be removed.
 * @param   fFlags          Reserved, MBZ.
 */
RTDECL(int) RTVfsDirRemoveDir(RTVFSDIR hVfsDir, const char *pszRelPath, uint32_t fFlags);

/**
 * Reads the next entry in the directory returning extended information.
 *
 * @returns VINF_SUCCESS and data in pDirEntry on success.
 * @returns VERR_NO_MORE_FILES when the end of the directory has been reached.
 * @returns VERR_BUFFER_OVERFLOW if the buffer is too small to contain the filename. If
 *          pcbDirEntry is specified it will be updated with the required buffer size.
 * @returns suitable iprt status code on other errors.
 *
 * @param   hVfsDir         The VFS directory.
 * @param   pDirEntry       Where to store the information about the next
 *                          directory entry on success.
 * @param   pcbDirEntry     Optional parameter used for variable buffer size.
 *
 *                          On input the variable pointed to contains the size of the pDirEntry
 *                          structure. This must be at least OFFSET(RTDIRENTRYEX, szName[2]) bytes.
 *
 *                          On successful output the field is updated to
 *                          OFFSET(RTDIRENTRYEX, szName[pDirEntry->cbName + 1]).
 *
 *                          When the data doesn't fit in the buffer and VERR_BUFFER_OVERFLOW is
 *                          returned, this field contains the required buffer size.
 *
 *                          The value is unchanged in all other cases.
 * @param   enmAddAttr      Which set of additional attributes to request.
 *                          Use RTFSOBJATTRADD_NOTHING if this doesn't matter.
 *
 * @sa      RTDirReadEx
 */
RTDECL(int) RTVfsDirReadEx(RTVFSDIR hVfsDir, PRTDIRENTRYEX pDirEntry, size_t *pcbDirEntry, RTFSOBJATTRADD enmAddAttr);

/**
 * Rewind and restart the directory reading.
 *
 * @returns IRPT status code.
 * @param   hVfsDir         The VFS directory.
 */
RTDECL(int) RTVfsDirRewind(RTVFSDIR hVfsDir);

/** @}  */


/** @defgroup grp_rt_vfs_symlink    VFS Symbolic Link API
 *
 * @remarks The TAR VFS and filesystem stream uses symbolic links for
 *          describing hard links as well.  The users must use RTFS_IS_SYMLINK
 *          to check if it is a real symlink in those cases.
 *
 * @remarks Any VFS which is backed by a real file system may be subject to
 *          races with other processes or threads, so the user may get
 *          unexpected errors when this happends.  This is a bit host specific,
 *          i.e. it might be prevent on windows if we care.
 *
 * @{
 */


/**
 * Retains a reference to the VFS symbolic link handle.
 *
 * @returns New reference count on success, UINT32_MAX on failure.
 * @param   hVfsSym         The VFS symbolic link handle.
 */
RTDECL(uint32_t)    RTVfsSymlinkRetain(RTVFSSYMLINK hVfsSym);
RTDECL(uint32_t)    RTVfsSymlinkRetainDebug(RTVFSSYMLINK hVfsSym, RT_SRC_POS_DECL);

/**
 * Releases a reference to the VFS symbolic link handle.
 *
 * @returns New reference count on success (0 if closed), UINT32_MAX on failure.
 * @param   hVfsSym         The VFS symbolic link handle.
 */
RTDECL(uint32_t)    RTVfsSymlinkRelease(RTVFSSYMLINK hVfsSym);

/**
 * Query information about the symbolic link.
 *
 * @returns IPRT status code.
 * @param   hVfsSym         The VFS symbolic link handle.
 * @param   pObjInfo        Where to return the info.
 * @param   enmAddAttr      Which additional attributes should be retrieved.
 *
 * @sa      RTFileQueryInfo, RTPathQueryInfo, RTPathQueryInfoEx
 */
RTDECL(int)         RTVfsSymlinkQueryInfo(RTVFSSYMLINK hVfsSym, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr);

/**
 * Set the unix style owner and group.
 *
 * @returns IPRT status code.
 * @param   hVfsSym         The VFS symbolic link handle.
 * @param   fMode           The new mode bits.
 * @param   fMask           The mask indicating which bits we are changing.
 * @sa      RTFileSetMode, RTPathSetMode
 */
RTDECL(int)         RTVfsSymlinkSetMode(RTVFSSYMLINK hVfsSym, RTFMODE fMode, RTFMODE fMask);

/**
 * Set the timestamps associated with the object.
 *
 * @returns IPRT status code.
 * @param   hVfsSym         The VFS symbolic link handle.
 * @param   pAccessTime     Pointer to the new access time. NULL if not
 *                          to be changed.
 * @param   pModificationTime   Pointer to the new modifcation time. NULL if
 *                              not to be changed.
 * @param   pChangeTime     Pointer to the new change time. NULL if not to be
 *                          changed.
 * @param   pBirthTime      Pointer to the new time of birth. NULL if not to be
 *                          changed.
 * @remarks See RTFileSetTimes for restrictions and behavior imposed by the
 *          host OS or underlying VFS provider.
 * @sa      RTFileSetTimes, RTPathSetTimes
 */
RTDECL(int)         RTVfsSymlinkSetTimes(RTVFSSYMLINK hVfsSym, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                                         PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime);

/**
 * Set the unix style owner and group.
 *
 * @returns IPRT status code.
 * @param   hVfsSym         The VFS symbolic link handle.
 * @param   uid             The user ID of the new owner.  NIL_RTUID if
 *                          unchanged.
 * @param   gid             The group ID of the new owner group. NIL_RTGID if
 *                          unchanged.
 * @sa      RTFileSetOwner, RTPathSetOwner.
 */
RTDECL(int)         RTVfsSymlinkSetOwner(RTVFSSYMLINK hVfsSym, RTUID uid, RTGID gid);

/**
 * Read the symbolic link target.
 *
 * @returns IPRT status code.
 * @param   hVfsSym         The VFS symbolic link handle.
 * @param   pszTarget       The target buffer.
 * @param   cbTarget        The size of the target buffer.
 * @sa      RTSymlinkRead
 */
RTDECL(int)         RTVfsSymlinkRead(RTVFSSYMLINK hVfsSym, char *pszTarget, size_t cbTarget);

/** @}  */



/** @defgroup grp_rt_vfs_iostream   VFS I/O Stream API
 * @{
 */

/**
 * Creates a VFS file from a memory buffer.
 *
 * @returns IPRT status code.
 *
 * @param   fFlags          A combination of RTFILE_O_READ and RTFILE_O_WRITE.
 * @param   pvBuf           The buffer.  This will be copied and not referenced
 *                          after this function returns.
 * @param   cbBuf           The buffer size.
 * @param   phVfsIos        Where to return the VFS I/O stream handle.
 */
RTDECL(int)         RTVfsIoStrmFromBuffer(uint32_t fFlags, void const *pvBuf, size_t cbBuf, PRTVFSIOSTREAM phVfsIos);

/**
 * Creates a VFS I/O stream handle from a standard IPRT file handle (RTFILE).
 *
 * @returns IPRT status code.
 * @param   hFile           The standard IPRT file handle.
 * @param   fOpen           The flags the handle was opened with.  Pass 0 to
 *                          have these detected.
 * @param   fLeaveOpen      Whether to leave the handle open when the VFS file
 *                          is released, or to close it (@c false).
 * @param   phVfsIos        Where to return the VFS I/O stream handle.
 */
RTDECL(int)         RTVfsIoStrmFromRTFile(RTFILE hFile, uint64_t fOpen, bool fLeaveOpen, PRTVFSIOSTREAM phVfsIos);

/**
 * Creates a VFS I/O stream handle from a standard IPRT pipe handle (RTPIPE).
 *
 * @returns IPRT status code.
 * @param   hPipe           The standard IPRT pipe handle.
 * @param   fLeaveOpen      Whether to leave the handle open when the VFS file
 *                          is released, or to close it (@c false).
 * @param   phVfsIos        Where to return the VFS I/O stream handle.
 */
RTDECL(int)         RTVfsIoStrmFromRTPipe(RTPIPE hPipe, bool fLeaveOpen, PRTVFSIOSTREAM phVfsIos);

/**
 * Convenience function combining RTFileOpen with RTVfsIoStrmFromRTFile.
 *
 * @returns IPRT status code.
 * @param   pszFilename     The path to the file in the normal file system.
 * @param   fOpen           The flags to pass to RTFileOpen when opening the
 *                          file, i.e. RTFILE_O_XXX.
 * @param   phVfsIos        Where to return the VFS I/O stream handle.
 */
RTDECL(int)         RTVfsIoStrmOpenNormal(const char *pszFilename, uint64_t fOpen, PRTVFSIOSTREAM phVfsIos);

/**
 * Create a VFS I/O stream handle from one of the standard handles.
 *
 * @returns IPRT status code.
 * @param   enmStdHandle    The standard IPRT file handle.
 * @param   fOpen           The flags the handle was opened with.  Pass 0 to
 *                          have these detected.
 * @param   fLeaveOpen      Whether to leave the handle open when the VFS file
 *                          is released, or to close it (@c false).
 * @param   phVfsIos        Where to return the VFS I/O stream handle.
 */
RTDECL(int)         RTVfsIoStrmFromStdHandle(RTHANDLESTD enmStdHandle, uint64_t fOpen, bool fLeaveOpen,
                                             PRTVFSIOSTREAM phVfsIos);

/**
 * Retains a reference to the VFS I/O stream handle.
 *
 * @returns New reference count on success, UINT32_MAX on failure.
 * @param   hVfsIos         The VFS I/O stream handle.
 */
RTDECL(uint32_t)    RTVfsIoStrmRetain(RTVFSIOSTREAM hVfsIos);
RTDECL(uint32_t)    RTVfsIoStrmRetainDebug(RTVFSIOSTREAM hVfsIos, RT_SRC_POS_DECL);

/**
 * Releases a reference to the VFS I/O stream handle.
 *
 * @returns New reference count on success (0 if closed), UINT32_MAX on failure.
 * @param   hVfsIos         The VFS I/O stream handle.
 */
RTDECL(uint32_t)    RTVfsIoStrmRelease(RTVFSIOSTREAM hVfsIos);

/**
 * Convert the VFS I/O stream handle to a VFS file handle.
 *
 * @returns The VFS file handle on success, this must be released.
 *          NIL_RTVFSFILE if the I/O stream handle is invalid.
 * @param   hVfsIos         The VFS I/O stream handle.
 * @sa      RTVfsFileToIoStream
 */
RTDECL(RTVFSFILE)   RTVfsIoStrmToFile(RTVFSIOSTREAM hVfsIos);

/**
 * Query information about the I/O stream.
 *
 * @returns IPRT status code.
 * @param   hVfsIos         The VFS I/O stream handle.
 * @param   pObjInfo        Where to return the info.
 * @param   enmAddAttr      Which additional attributes should be retrieved.
 * @sa      RTFileQueryInfo
 */
RTDECL(int)         RTVfsIoStrmQueryInfo(RTVFSIOSTREAM hVfsIos, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr);

/**
 * Read bytes from the I/O stream.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS and the number of bytes read written to @a pcbRead.
 * @retval  VINF_TRY_AGAIN if @a fBlocking is @c false, @a pcbRead is not NULL,
 *          and no data was available. @a *pcbRead will be set to 0.
 * @retval  VINF_EOF when trying to read __beyond__ the end of the stream and
 *          @a pcbRead is not NULL (it will be set to the number of bytes read,
 *          or 0 if the end of the stream was reached before this call).
 *          When the last byte of the read request is the last byte in the
 *          stream, this status code will not be used.  However, VINF_EOF is
 *          returned when attempting to read 0 bytes while standing at the end
 *          of the stream.
 * @retval  VERR_EOF when trying to read __beyond__ the end of the stream and
 *          @a pcbRead is NULL.
 * @retval  VERR_ACCESS_DENIED if the stream is not readable.
 *
 * @param   hVfsIos         The VFS I/O stream handle.
 * @param   pvBuf           Where to store the read bytes.
 * @param   cbToRead        The number of bytes to read.
 * @param   fBlocking       Whether the call is blocking (@c true) or not.  If
 *                          not, the @a pcbRead parameter must not be NULL.
 * @param   pcbRead         Where to always store the number of bytes actually
 *                          read.  This can be NULL if @a fBlocking is true.
 * @sa      RTVfsFileRead, RTFileRead, RTPipeRead, RTPipeReadBlocking,
 *          RTSocketRead
 */
RTDECL(int)         RTVfsIoStrmRead(RTVFSIOSTREAM hVfsIos, void *pvBuf, size_t cbToRead, bool fBlocking, size_t *pcbRead);

/**
 * Read bytes from the I/O stream, optionally with offset.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS and the number of bytes read written to @a pcbRead.
 * @retval  VINF_TRY_AGAIN if @a fBlocking is @c false, @a pcbRead is not NULL,
 *          and no data was available. @a *pcbRead will be set to 0.
 * @retval  VINF_EOF when trying to read __beyond__ the end of the stream and
 *          @a pcbRead is not NULL (it will be set to the number of bytes read,
 *          or 0 if the end of the stream was reached before this call).
 *          When the last byte of the read request is the last byte in the
 *          stream, this status code will not be used.  However, VINF_EOF is
 *          returned when attempting to read 0 bytes while standing at the end
 *          of the stream.
 * @retval  VERR_EOF when trying to read __beyond__ the end of the stream and
 *          @a pcbRead is NULL.
 * @retval  VERR_ACCESS_DENIED if the stream is not readable.
 *
 * @param   hVfsIos         The VFS I/O stream handle.
 * @param   off             Where to read at, -1 for the current position.
 * @param   pvBuf           Where to store the read bytes.
 * @param   cbToRead        The number of bytes to read.
 * @param   fBlocking       Whether the call is blocking (@c true) or not.  If
 *                          not, the @a pcbRead parameter must not be NULL.
 * @param   pcbRead         Where to always store the number of bytes actually
 *                          read.  This can be NULL if @a fBlocking is true.
 * @sa      RTVfsFileRead, RTFileRead, RTPipeRead, RTPipeReadBlocking,
 *          RTSocketRead
 */
RTDECL(int)         RTVfsIoStrmReadAt(RTVFSIOSTREAM hVfsIos, RTFOFF off, void *pvBuf, size_t cbToRead, bool fBlocking, size_t *pcbRead);

/**
 * Reads the remainder of the stream into a memory buffer.
 *
 * For simplifying string-style processing, there is a zero byte after the
 * returned buffer, making sure it can be used as a zero terminated string.
 *
 * @returns IPRT status code.
 * @param   hVfsIos         The VFS I/O stream handle.
 * @param   ppvBuf          Where to return the buffer.  Must pass to
 *                          RTVfsIoStrmReadAllFree for freeing, not RTMemFree!
 * @param   pcbBuf          Where to return the buffer size.
 * @sa      RTVfsFileReadAll
 */
RTDECL(int)         RTVfsIoStrmReadAll(RTVFSIOSTREAM hVfsIos, void **ppvBuf, size_t *pcbBuf);

/**
 * Free memory buffer returned by RTVfsIoStrmReadAll.
 *
 * @param   pvBuf           What RTVfsIoStrmReadAll returned.
 * @param   cbBuf           What RTVfsIoStrmReadAll returned.
 * @sa      RTVfsFileReadAllFree
 */
RTDECL(void)        RTVfsIoStrmReadAllFree(void *pvBuf, size_t cbBuf);

/**
 * Write bytes to the I/O stream.
 *
 * @returns IPRT status code.
 * @retval  VERR_ACCESS_DENIED if the stream is not writable.
 *
 * @param   hVfsIos         The VFS I/O stream handle.
 * @param   pvBuf           The bytes to write.
 * @param   cbToWrite       The number of bytes to write.
 * @param   fBlocking       Whether the call is blocking (@c true) or not.  If
 *                          not, the @a pcbWritten parameter must not be NULL.
 * @param   pcbWritten      Where to always store the number of bytes actually
 *                          written.  This can be NULL if @a fBlocking is true.
 * @sa      RTVfsFileWrite, RTFileWrite, RTPipeWrite, RTPipeWriteBlocking,
 *          RTSocketWrite
 */
RTDECL(int)         RTVfsIoStrmWrite(RTVFSIOSTREAM hVfsIos, const void *pvBuf, size_t cbToWrite, bool fBlocking, size_t *pcbWritten);
RTDECL(int)         RTVfsIoStrmWriteAt(RTVFSIOSTREAM hVfsIos, RTFOFF off, const void *pvBuf, size_t cbToWrite, bool fBlocking, size_t *pcbWritten);

/**
 * Reads bytes from the I/O stream into a scatter buffer.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS and the number of bytes read written to @a pcbRead.
 * @retval  VINF_TRY_AGAIN if @a fBlocking is @c false, @a pcbRead is not NULL,
 *          and no data was available. @a *pcbRead will be set to 0.
 * @retval  VINF_EOF when trying to read __beyond__ the end of the stream and
 *          @a pcbRead is not NULL (it will be set to the number of bytes read,
 *          or 0 if the end of the stream was reached before this call).
 *          When the last byte of the read request is the last byte in the
 *          stream, this status code will not be used.  However, VINF_EOF is
 *          returned when attempting to read 0 bytes while standing at the end
 *          of the stream.
 * @retval  VERR_EOF when trying to read __beyond__ the end of the stream and
 *          @a pcbRead is NULL.
 * @retval  VERR_ACCESS_DENIED if the stream is not readable.
 *
 * @param   hVfsIos         The VFS I/O stream handle.
 * @param   off             Where to read at, -1 for the current position.
 * @param   pSgBuf          Pointer to a scatter buffer descriptor.  The number
 *                          of bytes described by the segments is what will be
 *                          attemted read.
 * @param   fBlocking       Whether the call is blocking (@c true) or not.  If
 *                          not, the @a pcbRead parameter must not be NULL.
 * @param   pcbRead         Where to always store the number of bytes actually
 *                          read.  This can be NULL if @a fBlocking is true.
 * @sa      RTFileSgRead, RTSocketSgRead, RTPipeRead, RTPipeReadBlocking
 */
RTDECL(int)         RTVfsIoStrmSgRead(RTVFSIOSTREAM hVfsIos, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbRead);

/**
 * Write bytes to the I/O stream from a gather buffer.
 *
 * @returns IPRT status code.
 * @retval  VERR_ACCESS_DENIED if the stream is not writable.
 *
 * @param   hVfsIos         The VFS I/O stream handle.
 * @param   off             Where to write at, -1 for the current position.
 * @param   pSgBuf          Pointer to a gather buffer descriptor.  The number
 *                          of bytes described by the segments is what will be
 *                          attemted written.
 * @param   fBlocking       Whether the call is blocking (@c true) or not.  If
 *                          not, the @a pcbWritten parameter must not be NULL.
 * @param   pcbWritten      Where to always store the number of bytes actually
 *                          written.  This can be NULL if @a fBlocking is true.
 * @sa      RTFileSgWrite, RTSocketSgWrite
 */
RTDECL(int)         RTVfsIoStrmSgWrite(RTVFSIOSTREAM hVfsIos, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbWritten);

/**
 * Flush any buffered data to the I/O stream.
 *
 * @returns IPRT status code.
 * @param   hVfsIos         The VFS I/O stream handle.
 * @sa      RTVfsFileFlush, RTFileFlush, RTPipeFlush
 */
RTDECL(int)         RTVfsIoStrmFlush(RTVFSIOSTREAM hVfsIos);

/**
 * Poll for events.
 *
 * @returns IPRT status code.
 * @param   hVfsIos         The VFS I/O stream handle.
 * @param   fEvents         The events to poll for (RTPOLL_EVT_XXX).
 * @param   cMillies        How long to wait for event to eventuate.
 * @param   fIntr           Whether the wait is interruptible and can return
 *                          VERR_INTERRUPTED (@c true) or if this condition
 *                          should be hidden from the caller (@c false).
 * @param   pfRetEvents     Where to return the event mask.
 * @sa      RTVfsFilePoll, RTPollSetAdd, RTPoll, RTPollNoResume.
 */
RTDECL(int)         RTVfsIoStrmPoll(RTVFSIOSTREAM hVfsIos, uint32_t fEvents, RTMSINTERVAL cMillies, bool fIntr,
                                    uint32_t *pfRetEvents);
/**
 * Tells the current I/O stream position.
 *
 * @returns Zero or higher - where to return the I/O stream offset.  Values
 *          below zero are IPRT status codes (VERR_XXX).
 * @param   hVfsIos         The VFS I/O stream handle.
 * @sa      RTFileTell
 */
RTDECL(RTFOFF)      RTVfsIoStrmTell(RTVFSIOSTREAM hVfsIos);

/**
 * Skips @a cb ahead in the stream.
 *
 * @returns IPRT status code.
 * @param   hVfsIos         The VFS I/O stream handle.
 * @param   cb              The number bytes to skip.
 */
RTDECL(int)         RTVfsIoStrmSkip(RTVFSIOSTREAM hVfsIos, RTFOFF cb);

/**
 * Fills the stream with @a cb zeros.
 *
 * @returns IPRT status code.
 * @param   hVfsIos         The VFS I/O stream handle.
 * @param   cb              The number of zero bytes to insert.
 */
RTDECL(int)         RTVfsIoStrmZeroFill(RTVFSIOSTREAM hVfsIos, RTFOFF cb);

/**
 * Checks if we're at the end of the I/O stream.
 *
 * @returns true if at EOS, otherwise false.
 * @param   hVfsIos         The VFS I/O stream handle.
 */
RTDECL(bool)        RTVfsIoStrmIsAtEnd(RTVFSIOSTREAM hVfsIos);

/**
 * Get the RTFILE_O_XXX flags for the I/O stream.
 *
 * @returns RTFILE_O_XXX, 0 on failure.
 * @param   hVfsIos         The VFS I/O stream handle.
 */
RTDECL(uint64_t)    RTVfsIoStrmGetOpenFlags(RTVFSIOSTREAM hVfsIos);

/**
 * Process the rest of the stream, checking if it's all valid UTF-8 encoding.
 *
 * @returns IPRT status code.
 *
 * @param   hVfsIos         The VFS I/O stream handle.
 * @param   fFlags          Flags governing the validation, see
 *                          RTVFS_VALIDATE_UTF8_XXX.
 * @param   poffError       Where to return the error offset. Optional.
 *                          Not implemented yet!
 */
RTDECL(int)        RTVfsIoStrmValidateUtf8Encoding(RTVFSIOSTREAM hVfsIos, uint32_t fFlags, PRTFOFF poffError);

/** @defgroup RTVFS_VALIDATE_UTF8_XXX   RTVfsIoStrmValidateUtf8Encoding flags.
 * @{ */
/** The text must not contain any null terminator codepoints. */
#define RTVFS_VALIDATE_UTF8_NO_NULL         RT_BIT_32(0)
/** The codepoints must be in the range covered by RTC-3629.  */
#define RTVFS_VALIDATE_UTF8_BY_RTC_3629     RT_BIT_32(1)
/** Mask of valid flags. */
#define RTVFS_VALIDATE_UTF8_VALID_MASK      UINT32_C(0x00000003)
/** @}  */

/**
 * Printf-like write function.
 *
 * @returns Number of characters written on success, negative error status on
 *          failure.
 * @param   hVfsIos         The VFS I/O stream handle to write to.
 * @param   pszFormat       The format string.
 * @param   ...             Format arguments.
 */
RTDECL(ssize_t)     RTVfsIoStrmPrintf(RTVFSIOSTREAM hVfsIos, const char *pszFormat, ...);

/**
 * Printf-like write function.
 *
 * @returns Number of characters written on success, negative error status on
 *          failure.
 * @param   hVfsIos         The VFS I/O stream handle to write to.
 * @param   pszFormat       The format string.
 * @param   va              Format arguments.
 */
RTDECL(ssize_t)     RTVfsIoStrmPrintfV(RTVFSIOSTREAM hVfsIos, const char *pszFormat, va_list va);

/**
 * VFS I/O stream output buffer structure to use with
 * RTVfsIoStrmStrOutputCallback().
 */
typedef struct VFSIOSTRMOUTBUF
{
    /** The I/O stream handle. */
    RTVFSIOSTREAM   hVfsIos;
    /** Size of this structure (for sanity). */
    size_t          cbSelf;
    /** Status code of the operation. */
    int             rc;
    /** Current offset into szBuf (number of output bytes pending). */
    size_t          offBuf;
    /** Modest output buffer. */
    char            szBuf[256];
} VFSIOSTRMOUTBUF;
/** Pointer to an VFS I/O stream output buffer for use with
 *  RTVfsIoStrmStrOutputCallback() */
typedef VFSIOSTRMOUTBUF *PVFSIOSTRMOUTBUF;

/** Initializer for a VFS I/O stream output buffer. */
#define VFSIOSTRMOUTBUF_INIT(a_pOutBuf, a_hVfsIos) \
    do { \
        (a_pOutBuf)->hVfsIos  = a_hVfsIos; \
        (a_pOutBuf)->cbSelf   = sizeof(*(a_pOutBuf)); \
        (a_pOutBuf)->rc       = VINF_SUCCESS; \
        (a_pOutBuf)->offBuf   = 0; \
        (a_pOutBuf)->szBuf[0] = '\0'; \
    } while (0)

/**
 * @callback_method_impl{FNRTSTROUTPUT,
 * For use with VFSIOSTRMOUTBUF.
 *
 * Users must use VFSIOSTRMOUTBUF_INIT to initialize a VFSIOSTRMOUTBUF and pass
 * that as the outputter argument to the function this callback is handed to.}
 */
RTDECL(size_t) RTVfsIoStrmStrOutputCallback(void *pvArg, const char *pachChars, size_t cbChars);

/** @} */


/** @defgroup grp_rt_vfs_file       VFS File API
 * @{
 */
RTDECL(int)         RTVfsFileOpen(RTVFS hVfs, const char *pszFilename, uint64_t fOpen, PRTVFSFILE phVfsFile);

/**
 * Create a VFS file handle from a standard IPRT file handle (RTFILE).
 *
 * @returns IPRT status code.
 * @param   hFile           The standard IPRT file handle.
 * @param   fOpen           The flags the handle was opened with.  Pass 0 to
 *                          have these detected.
 * @param   fLeaveOpen      Whether to leave the handle open when the VFS file
 *                          is released, or to close it (@c false).
 * @param   phVfsFile       Where to return the VFS file handle.
 */
RTDECL(int)         RTVfsFileFromRTFile(RTFILE hFile, uint64_t fOpen, bool fLeaveOpen, PRTVFSFILE phVfsFile);
RTDECL(RTHCUINTPTR) RTVfsFileToNative(RTFILE hVfsFile);

/**
 * Convenience function combining RTFileOpen with RTVfsFileFromRTFile.
 *
 * @returns IPRT status code.
 * @param   pszFilename     The path to the file in the normal file system.
 * @param   fOpen           The flags to pass to RTFileOpen when opening the
 *                          file, i.e. RTFILE_O_XXX.
 * @param   phVfsFile       Where to return the VFS file handle.
 */
RTDECL(int)         RTVfsFileOpenNormal(const char *pszFilename, uint64_t fOpen, PRTVFSFILE phVfsFile);

/**
 * Convert the VFS file handle to a VFS I/O stream handle.
 *
 * @returns The VFS I/O stream handle on success, this must be released.
 *          NIL_RTVFSIOSTREAM if the file handle is invalid.
 * @param   hVfsFile        The VFS file handle.
 * @sa      RTVfsIoStrmToFile
 */
RTDECL(RTVFSIOSTREAM) RTVfsFileToIoStream(RTVFSFILE hVfsFile);

/**
 * Retains a reference to the VFS file handle.
 *
 * @returns New reference count on success, UINT32_MAX on failure.
 * @param   hVfsFile        The VFS file handle.
 */
RTDECL(uint32_t)    RTVfsFileRetain(RTVFSFILE hVfsFile);
RTDECL(uint32_t)    RTVfsFileRetainDebug(RTVFSFILE hVfsFile, RT_SRC_POS_DECL);

/**
 * Releases a reference to the VFS file handle.
 *
 * @returns New reference count on success (0 if closed), UINT32_MAX on failure.
 * @param   hVfsFile        The VFS file handle.
 */
RTDECL(uint32_t)    RTVfsFileRelease(RTVFSFILE hVfsFile);

/**
 * Query information about the object.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_SUPPORTED if the @a enmAddAttr value is not handled by the
 *          implementation.
 *
 * @param   hVfsFile        The VFS file handle.
 * @param   pObjInfo        Where to return the info.
 * @param   enmAddAttr      Which additional attributes should be retrieved.
 * @sa      RTVfsObjQueryInfo, RTVfsFsStrmQueryInfo, RTVfsDirQueryInfo,
 *          RTVfsIoStrmQueryInfo, RTVfsFileQueryInfo, RTFileQueryInfo,
 *          RTPathQueryInfo.
 */
RTDECL(int)         RTVfsFileQueryInfo(RTVFSFILE hVfsFile, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr);

/**
 * Read bytes from the file at the current position.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS and the number of bytes read written to @a pcbRead.
 * @retval  VINF_EOF when trying to read __beyond__ the end of the file and
 *          @a pcbRead is not NULL (it will be set to the number of bytes read,
 *          or 0 if the end of the file was reached before this call).
 *          When the last byte of the read request is the last byte in the
 *          file, this status code will not be used.  However, VINF_EOF is
 *          returned when attempting to read 0 bytes while standing at the end
 *          of the file.
 * @retval  VERR_EOF when trying to read __beyond__ the end of the file and
 *          @a pcbRead is NULL.
 * @retval  VERR_ACCESS_DENIED if the file is not readable.
 *
 * @param   hVfsFile        The VFS file handle.
 * @param   pvBuf           Where to store the read bytes.
 * @param   cbToRead        The number of bytes to read.
 * @param   pcbRead         Where to always store the number of bytes actually
 *                          read.  Optional.
 * @sa      RTVfsIoStrmRead, RTFileRead, RTPipeRead, RTPipeReadBlocking,
 *          RTSocketRead
 */
RTDECL(int)         RTVfsFileRead(RTVFSFILE hVfsFile, void *pvBuf, size_t cbToRead, size_t *pcbRead);
RTDECL(int)         RTVfsFileReadAt(RTVFSFILE hVfsFile, RTFOFF off, void *pvBuf, size_t cbToRead, size_t *pcbRead);

/**
 * Reads the remainder of the file into a memory buffer.
 *
 * For simplifying string-style processing, there is a zero byte after the
 * returned buffer, making sure it can be used as a zero terminated string.
 *
 * @returns IPRT status code.
 * @param   hVfsFile        The VFS file handle.
 * @param   ppvBuf          Where to return the buffer.  Must pass to
 *                          RTVfsFileReadAllFree for freeing, not RTMemFree!
 * @param   pcbBuf          Where to return the buffer size.
 * @sa      RTVfsIoStrmReadAll
 */
RTDECL(int)         RTVfsFileReadAll(RTVFSFILE hVfsFile, void **ppvBuf, size_t *pcbBuf);

/**
 * Free memory buffer returned by RTVfsFileReadAll.
 *
 * @param   pvBuf           What RTVfsFileReadAll returned.
 * @param   cbBuf           What RTVfsFileReadAll returned.
 * @sa      RTVfsIoStrmReadAllFree
 */
RTDECL(void)        RTVfsFileReadAllFree(void *pvBuf, size_t cbBuf);

/**
 * Write bytes to the file at the current position.
 *
 * @returns IPRT status code.
 * @retval  VERR_ACCESS_DENIED if the file is not writable.
 *
 * @param   hVfsFile        The VFS file handle.
 * @param   pvBuf           The bytes to write.
 * @param   cbToWrite       The number of bytes to write.
 * @param   pcbWritten      Where to always store the number of bytes actually
 *                          written.  This can be NULL.
 * @sa      RTVfsIoStrmRead, RTFileWrite, RTPipeWrite, RTPipeWriteBlocking,
 *          RTSocketWrite
 */
RTDECL(int)         RTVfsFileWrite(RTVFSFILE hVfsFile, const void *pvBuf, size_t cbToWrite, size_t *pcbWritten);
RTDECL(int)         RTVfsFileWriteAt(RTVFSFILE hVfsFile, RTFOFF off, const void *pvBuf, size_t cbToWrite, size_t *pcbWritten);


/**
 * Reads bytes from the file into a scatter buffer.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS and the number of bytes read written to @a pcbRead.
 * @retval  VINF_TRY_AGAIN if @a fBlocking is @c false, @a pcbRead is not NULL,
 *          and no data was available. @a *pcbRead will be set to 0.
 * @retval  VINF_EOF when trying to read __beyond__ the end of the stream and
 *          @a pcbRead is not NULL (it will be set to the number of bytes read,
 *          or 0 if the end of the stream was reached before this call).
 *          When the last byte of the read request is the last byte in the
 *          stream, this status code will not be used.  However, VINF_EOF is
 *          returned when attempting to read 0 bytes while standing at the end
 *          of the stream.
 * @retval  VERR_EOF when trying to read __beyond__ the end of the stream and
 *          @a pcbRead is NULL.
 * @retval  VERR_ACCESS_DENIED if the stream is not readable.
 *
 * @param   hVfsFile        The VFS file handle.
 * @param   off             Where to read at, -1 for the current position.
 * @param   pSgBuf          Pointer to a scatter buffer descriptor.  The number
 *                          of bytes described by the segments is what will be
 *                          attemted read.
 * @param   fBlocking       Whether the call is blocking (@c true) or not.  If
 *                          not, the @a pcbRead parameter must not be NULL.
 * @param   pcbRead         Where to always store the number of bytes actually
 *                          read.  This can be NULL if @a fBlocking is true.
 * @sa      RTFileSgRead, RTSocketSgRead, RTPipeRead, RTPipeReadBlocking
 */
RTDECL(int)         RTVfsFileSgRead(RTVFSFILE hVfsFile, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbRead);

/**
 * Write bytes to the file from a gather buffer.
 *
 * @returns IPRT status code.
 * @retval  VERR_ACCESS_DENIED if the stream is not writable.
 *
 * @param   hVfsFile        The VFS file handle.
 * @param   off             Where to write at, -1 for the current position.
 * @param   pSgBuf          Pointer to a gather buffer descriptor.  The number
 *                          of bytes described by the segments is what will be
 *                          attemted written.
 * @param   fBlocking       Whether the call is blocking (@c true) or not.  If
 *                          not, the @a pcbWritten parameter must not be NULL.
 * @param   pcbWritten      Where to always store the number of bytes actually
 *                          written.  This can be NULL if @a fBlocking is true.
 * @sa      RTFileSgWrite, RTSocketSgWrite
 */
RTDECL(int)         RTVfsFileSgWrite(RTVFSFILE hVfsFile, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbWritten);

/**
 * Flush any buffered data to the file.
 *
 * @returns IPRT status code.
 * @param   hVfsFile        The VFS file handle.
 * @sa      RTVfsIoStrmFlush, RTFileFlush, RTPipeFlush
 */
RTDECL(int)         RTVfsFileFlush(RTVFSFILE hVfsFile);

/**
 * Poll for events.
 *
 * @returns IPRT status code.
 * @param   hVfsFile        The VFS file handle.
 * @param   fEvents         The events to poll for (RTPOLL_EVT_XXX).
 * @param   cMillies        How long to wait for event to eventuate.
 * @param   fIntr           Whether the wait is interruptible and can return
 *                          VERR_INTERRUPTED (@c true) or if this condition
 *                          should be hidden from the caller (@c false).
 * @param   pfRetEvents     Where to return the event mask.
 * @sa      RTVfsIoStrmPoll, RTPollSetAdd, RTPoll, RTPollNoResume.
 */
RTDECL(RTFOFF)      RTVfsFilePoll(RTVFSFILE hVfsFile, uint32_t fEvents, RTMSINTERVAL cMillies, bool fIntr,
                                  uint32_t *pfRetEvents);

/**
 * Tells the current file position.
 *
 * @returns Zero or higher - where to return the file offset.  Values
 *          below zero are IPRT status codes (VERR_XXX).
 * @param   hVfsFile        The VFS file handle.
 * @sa      RTFileTell, RTVfsIoStrmTell.
 */
RTDECL(RTFOFF)      RTVfsFileTell(RTVFSFILE hVfsFile);

/**
 * Changes the current read/write position of a file.
 *
 * @returns IPRT status code.
 *
 * @param   hVfsFile        The VFS file handle.
 * @param   offSeek         The seek offset.
 * @param   uMethod         The seek method.
 * @param   poffActual      Where to optionally return the new file offset.
 *
 * @sa      RTFileSeek
 */
RTDECL(int)         RTVfsFileSeek(RTVFSFILE hVfsFile, RTFOFF offSeek, uint32_t uMethod, uint64_t *poffActual);

/**
 * Sets the size of a file.
 *
 * This may also be used for preallocating space
 * (RTVFSFILE_SIZE_F_PREALLOC_KEEP_SIZE).
 *
 * @returns IPRT status code.
 * @retval  VERR_ACCESS_DENIED if handle isn't writable.
 * @retval  VERR_WRITE_PROTECT if read-only file system.
 * @retval  VERR_FILE_TOO_BIG if cbSize is larger than what the file system can
 *          theoretically deal with.
 * @retval  VERR_DISK_FULL if the file system if full.
 * @retval  VERR_NOT_SUPPORTED if fFlags indicates some operation that's not
 *          supported by the file system / host operating system.
 *
 * @param   hVfsFile        The VFS file handle.
 * @param   cbSize          The new file size.
 * @param   fFlags          RTVFSFILE_SIZE_F_NORMAL, RTVFSFILE_SIZE_F_GROW, or
 *                          RTVFSFILE_SIZE_F_GROW_KEEP_SIZE.
 *
 * @sa      RTFileSetSize, RTFileSetAllocationSize
 */
RTDECL(int)         RTVfsFileSetSize(RTVFSFILE hVfsFile, uint64_t cbSize, uint32_t fFlags);

/** @name RTVFSFILE_SIZE_F_XXX - RTVfsFileSetSize flags.
 * @{ */
/** Normal truncate or grow (zero'ed) like RTFileSetSize . */
#define RTVFSFILE_SIZE_F_NORMAL             UINT32_C(0x00000001)
/** Only grow the file, ignore call if cbSize would truncate the file.
 * This is what RTFileSetAllocationSize does by default.  */
#define RTVFSFILE_SIZE_F_GROW               UINT32_C(0x00000002)
/** Only grow the file, ignore call if cbSize would truncate the file.
 * This is what RTFileSetAllocationSize does by default.  */
#define RTVFSFILE_SIZE_F_GROW_KEEP_SIZE     UINT32_C(0x00000003)
/** Action mask. */
#define RTVFSFILE_SIZE_F_ACTION_MASK        UINT32_C(0x00000003)
/** Validate the flags.
 * Will reference @a a_fFlags more than once.  */
#define RTVFSFILE_SIZE_F_IS_VALID(a_fFlags) \
    ( !((a_fFlags) & ~RTVFSFILE_SIZE_F_ACTION_MASK) && ((a_fFlags) & RTVFSFILE_SIZE_F_ACTION_MASK) != 0 )
/** Mask of valid flags. */
#define RTFILE_ALLOC_SIZE_F_VALID           (RTFILE_ALLOC_SIZE_F_KEEP_SIZE)
/** @} */


RTDECL(int)         RTVfsFileQuerySize(RTVFSFILE hVfsFile, uint64_t *pcbSize);
RTDECL(RTFOFF)      RTVfsFileGetMaxSize(RTVFSFILE hVfsFile);
RTDECL(int)         RTVfsFileQueryMaxSize(RTVFSFILE hVfsFile, uint64_t *pcbMax);

/**
 * Get the RTFILE_O_XXX flags for the I/O stream.
 *
 * @returns RTFILE_O_XXX, 0 on failure.
 * @param   hVfsFile        The VFS file handle.
 */
RTDECL(uint64_t)    RTVfsFileGetOpenFlags(RTVFSFILE hVfsFile);

/**
 * Printf-like write function.
 *
 * @returns Number of characters written on success, negative error status on
 *          failure.
 * @param   hVfsFile        The VFS file handle to write to.
 * @param   pszFormat       The format string.
 * @param   ...             Format arguments.
 */
RTDECL(ssize_t)     RTVfsFilePrintf(RTVFSFILE hVfsFile, const char *pszFormat, ...);

/**
 * Printf-like write function.
 *
 * @returns Number of characters written on success, negative error status on
 *          failure.
 * @param   hVfsFile        The VFS file handle to write to.
 * @param   pszFormat       The format string.
 * @param   va              Format arguments.
 */
RTDECL(ssize_t)     RTVfsFilePrintfV(RTVFSFILE hVfsFile, const char *pszFormat, va_list va);

/** @} */


#ifdef DEBUG
# undef RTVfsRetain
# define RTVfsRetain(hVfs)          RTVfsRetainDebug(hVfs, RT_SRC_POS)
# undef RTVfsObjRetain
# define RTVfsObjRetain(hVfsObj)    RTVfsObjRetainDebug(hVfsObj, RT_SRC_POS)
# undef RTVfsDirRetain
# define RTVfsDirRetain(hVfsDir)    RTVfsDirRetainDebug(hVfsDir, RT_SRC_POS)
# undef RTVfsFileRetain
# define RTVfsFileRetain(hVfsFile)  RTVfsFileRetainDebug(hVfsFile, RT_SRC_POS)
# undef RTVfsIoStrmRetain
# define RTVfsIoStrmRetain(hVfsIos) RTVfsIoStrmRetainDebug(hVfsIos, RT_SRC_POS)
# undef RTVfsFsStrmRetain
# define RTVfsFsStrmRetain(hVfsFss) RTVfsFsStrmRetainDebug(hVfsFss, RT_SRC_POS)
#endif



/** @defgroup grp_rt_vfs_misc       VFS Miscellaneous
 * @{
 */

/**
 * Memorizes the I/O stream as a file backed by memory.
 *
 * @returns IPRT status code.
 *
 * @param   hVfsIos         The VFS I/O stream to memorize.  This will be read
 *                          to the end on success, on failure its position is
 *                          undefined.
 * @param   fFlags          A combination of RTFILE_O_READ and RTFILE_O_WRITE.
 * @param   phVfsFile       Where to return the handle to the memory file on
 *                          success.
 */
RTDECL(int) RTVfsMemorizeIoStreamAsFile(RTVFSIOSTREAM hVfsIos, uint32_t fFlags, PRTVFSFILE phVfsFile);

/**
 * Creates a VFS file from a memory buffer.
 *
 * @returns IPRT status code.
 *
 * @param   fFlags          A combination of RTFILE_O_READ and RTFILE_O_WRITE.
 * @param   pvBuf           The buffer.  This will be copied and not referenced
 *                          after this function returns.
 * @param   cbBuf           The buffer size.
 * @param   phVfsFile       Where to return the handle to the memory file on
 *                          success.
 */
RTDECL(int) RTVfsFileFromBuffer(uint32_t fFlags, void const *pvBuf, size_t cbBuf, PRTVFSFILE phVfsFile);

/**
 * Creates a memory backed VFS file object for read and write.
 *
 * @returns IPRT status code.
 *
 * @param   hVfsIos         The VFS I/O stream to memorize.  This will be read
 *                          to the end on success, on failure its position is
 *                          undefined.
 * @param   cbEstimate      The estimated file size.
 * @param   phVfsFile       Where to return the handle to the memory file on
 *                          success.
 * @sa      RTVfsMemIoStrmCreate
 */
RTDECL(int) RTVfsMemFileCreate(RTVFSIOSTREAM hVfsIos, size_t cbEstimate, PRTVFSFILE phVfsFile);

/**
 * Creates a memory backed VFS file object for read and write.
 *
 * @returns IPRT status code.
 *
 * @param   hVfsIos         The VFS I/O stream to memorize.  This will be read
 *                          to the end on success, on failure its position is
 *                          undefined.
 * @param   cbEstimate      The estimated file size.
 * @param   phVfsIos        Where to return the handle to the memory I/O stream
 *                          on success.
 * @sa      RTVfsMemFileCreate
 */
RTDECL(int) RTVfsMemIoStrmCreate(RTVFSIOSTREAM hVfsIos, size_t cbEstimate, PRTVFSIOSTREAM phVfsIos);

/**
 * Pumps data from one I/O stream to another.
 *
 * The data is read in chunks from @a hVfsIosSrc and written to @a hVfsIosDst
 * until @a hVfsIosSrc indicates end of stream.
 *
 * @returns IPRT status code
 *
 * @param   hVfsIosSrc  The input stream.
 * @param   hVfsIosDst  The output stream.
 * @param   cbBufHint   Hints at a good temporary buffer size, pass 0 if
 *                      clueless.
 */
RTDECL(int) RTVfsUtilPumpIoStreams(RTVFSIOSTREAM hVfsIosSrc, RTVFSIOSTREAM hVfsIosDst, size_t cbBufHint);


/**
 * Creates a progress wrapper for an I/O stream.
 *
 * @returns IRPT status code.
 * @param   hVfsIos             The I/O stream to wrap.
 * @param   pfnProgress         The progress callback.  The return code is
 *                              ignored by default, see
 *                              RTVFSPROGRESS_F_CANCELABLE.
 * @param   pvUser              The user argument to @a pfnProgress.
 * @param   fFlags              RTVFSPROGRESS_F_XXX
 * @param   cbExpectedRead      The expected number of bytes read.
 * @param   cbExpectedWritten   The execpted number of bytes written.
 * @param   phVfsIos            Where to return the I/O stream handle.
 */
RTDECL(int) RTVfsCreateProgressForIoStream(RTVFSIOSTREAM hVfsIos, PFNRTPROGRESS pfnProgress, void *pvUser, uint32_t fFlags,
                                           uint64_t cbExpectedRead, uint64_t cbExpectedWritten, PRTVFSIOSTREAM phVfsIos);

/**
 * Creates a progress wrapper for a file stream.
 *
 * @returns IRPT status code.
 * @param   hVfsFile            The file to wrap.
 * @param   pfnProgress         The progress callback.  The return code is
 *                              ignored by default, see
 *                              RTVFSPROGRESS_F_CANCELABLE.
 * @param   pvUser              The user argument to @a pfnProgress.
 * @param   fFlags              RTVFSPROGRESS_F_XXX
 * @param   cbExpectedRead      The expected number of bytes read.
 * @param   cbExpectedWritten   The execpted number of bytes written.
 * @param   phVfsFile           Where to return the file handle.
 */
RTDECL(int) RTVfsCreateProgressForFile(RTVFSFILE hVfsFile, PFNRTPROGRESS pfnProgress, void *pvUser, uint32_t fFlags,
                                       uint64_t cbExpectedRead, uint64_t cbExpectedWritten, PRTVFSFILE phVfsFile);

/** @name RTVFSPROGRESS_F_XXX - Flags for RTVfsCreateProcessForIoStream and
 *        RTVfsCreateProcessForFile.
 * @{ */
/** Cancel if the callback returns a failure status code.
 * This isn't default behavior because the cancelation is delayed one I/O
 * operation in most cases and it's uncertain how the VFS user will handle the
 * cancellation status code. */
#define RTVFSPROGRESS_F_CANCELABLE             RT_BIT_32(0)
/** Account forward seeks as reads. */
#define RTVFSPROGRESS_F_FORWARD_SEEK_AS_READ    RT_BIT_32(1)
/** Account fprward seeks as writes. */
#define RTVFSPROGRESS_F_FORWARD_SEEK_AS_WRITE   RT_BIT_32(2)
/** Valid bits.   */
#define RTVFSPROGRESS_F_VALID_MASK              UINT32_C(0x00000007)
/** @} */


/**
 * Create an I/O stream instance performing simple sequential read-ahead.
 *
 * @returns IPRT status code.
 * @param   hVfsIos     The input stream to perform read ahead on.  If this is
 *                      actually for a file object, the returned I/O stream
 *                      handle can also be cast to a file handle.
 * @param   fFlags      Flags reserved for future use, MBZ.
 * @param   cBuffers    How many read ahead buffers to use. Specify 0 for
 *                      default value.
 * @param   cbBuffer    The size of each read ahead buffer. Specify 0 for
 *                      default value.
 * @param   phVfsIos    Where to return the read ahead I/O stream handle.
 *
 * @remarks Careful using this on a message pipe or socket.  The reads are
 *          performed in blocked mode and it may be host and/or implementation
 *          dependent whether they will return ready data immediate or wait
 *          until there's a whole @a cbBuffer (or default) worth ready.
 *
 * @sa      RTVfsCreateReadAheadForFile
 */
RTDECL(int) RTVfsCreateReadAheadForIoStream(RTVFSIOSTREAM hVfsIos, uint32_t fFlags, uint32_t cBuffers, uint32_t cbBuffer,
                                            PRTVFSIOSTREAM phVfsIos);

/**
 * Create an I/O stream instance performing simple sequential read-ahead.
 *
 * @returns IPRT status code.
 * @param   hVfsFile    The input file to perform read ahead on.
 * @param   fFlags      Flags reserved for future use, MBZ.
 * @param   cBuffers    How many read ahead buffers to use. Specify 0 for
 *                      default value.
 * @param   cbBuffer    The size of each read ahead buffer. Specify 0 for
 *                      default value.
 * @param   phVfsFile   Where to return the read ahead file handle.
 * @sa      RTVfsCreateReadAheadForIoStream
 */
RTDECL(int) RTVfsCreateReadAheadForFile(RTVFSFILE hVfsFile, uint32_t fFlags, uint32_t cBuffers, uint32_t cbBuffer,
                                        PRTVFSFILE phVfsFile);


/**
 * Create a file system stream for writing to a directory.
 *
 * This is just supposed to be a drop in replacement for the TAR creator stream
 * that instead puts the files and stuff in a directory instead of a TAR
 * archive.  In addition, it has an undo feature for simplying cleaning up after
 * a botched run
 *
 * @returns IPRT status code.
 * @param   hVfsBaseDir The base directory.
 * @param   fFlags      RTVFSFSS2DIR_F_XXX
 * @param   phVfsFss    Where to return the FSS handle.
 * @sa      RTVfsFsStrmToNormalDir, RTVfsFsStrmToDirUndo
 */
RTDECL(int) RTVfsFsStrmToDir(RTVFSDIR hVfsBaseDir, uint32_t fFlags, PRTVFSFSSTREAM phVfsFss);

/**
 * Create a file system stream for writing to a normal directory.
 *
 * This is just supposed to be a drop in replacement for the TAR creator stream
 * that instead puts the files and stuff in a directory instead of a TAR
 * archive.  In addition, it has an undo feature for simplying cleaning up after
 * a botched run
 *
 * @returns IPRT status code.
 * @param   pszBaseDir  The base directory.  Must exist.
 * @param   fFlags      RTVFSFSS2DIR_F_XXX
 * @param   phVfsFss    Where to return the FSS handle.
 * @sa      RTVfsFsStrmToDir, RTVfsFsStrmToDirUndo
 */
RTDECL(int) RTVfsFsStrmToNormalDir(const char *pszBaseDir, uint32_t fFlags, PRTVFSFSSTREAM phVfsFss);

/** @name RTVFSFSS2DIR_F_XXX - Flags for RTVfsFsStrmToNormalDir
 * @{ */
/** Overwrite existing files (default is to not overwrite anything). */
#define RTVFSFSS2DIR_F_OVERWRITE_FILES      RT_BIT_32(0)
/** Valid bits.   */
#define RTVFSFSS2DIR_F_VALID_MASK           UINT32_C(0x00000001)
/** @} */

/**
 * Deletes files, directories, symlinks and stuff created by a FSS returned by
 * RTVfsFsStrmToNormalDir or RTVfsFsStrmToDir.
 *
 * @returns IPRT status code.
 * @param   hVfsFss     The write-to-directory FSS handle.
 */
RTDECL(int) RTVfsFsStrmToDirUndo(RTVFSFSSTREAM hVfsFss);



/** @}  */


/** @defgroup grp_rt_vfs_chain  VFS Chains
 *
 * VFS chains is for doing pipe like things with VFS objects from the command
 * line.  Imagine you want to cat the readme.gz of an ISO you could do
 * something like:
 *      RTCat :iprtvfs:file(stdfile,live.iso)|vfs(isofs)|iso(open,readme.gz)|ios(gunzip)
 * or
 *      RTCat :iprtvfs:file(stdfile,live.iso)|ios(isofs,readme.gz)|ios(gunzip)
 *
 * Or say you want to read the README.TXT on a floppy image:
 *      RTCat :iprtvfs:file(stdfile,floppy.img,r)|vfs(fat)|ios(open,README.TXT)
 * or
 *      RTCat :iprtvfs:file(stdfile,floppy.img,r)|vfs(fat)|README.TXT
 *
 * Or in the other direction, you want to write a STUFF.TGZ file to the above
 * floppy image, using a lazy writer thread for compressing the data:
 *      RTTar cf :iprtvfs:file(stdfile,floppy.img,rw)|ios(fat,STUFF.TGZ)|ios(gzip)|ios(push) .
 *
 *
 * A bit more formally:
 *      :iprtvfs:{type}({provider}[,provider-args])[{separator}{type}...][{separator}{path}]
 *
 * The @c type refers to VFS object that should be created by the @c provider.
 * Valid types:
 *      - vfs:  A virtual file system (volume).
 *      - fss:  A file system stream (e.g. tar).
 *      - ios:  An I/O stream.
 *      - file: A file.
 *      - dir:  A directory.
 *      - sym:  A symbolic link (not sure how useful this is).
 *
 * The @c provider refers to registered chain element providers (see
 * RTVFSCHAINELEMENTREG for how that works internally).  These are asked to
 * create a VFS object of the specified type using the given arguments (if any).
 * Default providers:
 *      - std:      Standard file, directory and file system.
 *      - open:     Opens a file, I/O stream or directory in a vfs or directory object.
 *      - pull:     Read-ahead buffering thread on file or I/O stream.
 *      - push:     Lazy-writer buffering thread on file or I/O stream.
 *      - gzip:     Compresses an I/O stream.
 *      - gunzip:   Decompresses an I/O stream.
 *      - fat:      FAT file system accessor.
 *      - isofs:    ISOFS file system accessor.
 *
 * As element @c separator we allow both colon (':') and the pipe character
 * ('|'). The latter the conventional one, but since it's inconvenient on the
 * command line, colon is provided as an alternative.
 *
 * In the final element we allow a simple @a path to be specified instead of the
 * type-provider-arguments stuff.  The previous object must be a directory, file
 * system or file system stream.  The application will determin exactly which
 * operation or operations which will be performed.
 *
 * @{
 */

/** The path prefix used to identify an VFS chain specification. */
#define RTVFSCHAIN_SPEC_PREFIX   ":iprtvfs:"

RTDECL(int) RTVfsChainOpenVfs(const char *pszSpec, PRTVFS phVfs, uint32_t *poffError, PRTERRINFO pErrInfo);
RTDECL(int) RTVfsChainOpenFsStream(const char *pszSpec, PRTVFSFSSTREAM  phVfsFss, uint32_t *poffError, PRTERRINFO pErrInfo);

/**
 * Opens any kind of file system object.
 *
 * @returns IPRT status code.
 * @param   pszSpec         The VFS chain specification or plain path.
 * @param   fFileOpen       RTFILE_O_XXX flags.
 * @param   fObjFlags       More flags: RTVFSOBJ_F_XXX, RTPATH_F_XXX.
 * @param   phVfsObj        Where to return the handle to the opened object.
 * @param   poffError       Where to on error return an offset into @a pszSpec
 *                          of what cause the error.  Optional.
 * @param   pErrInfo        Where to return additional error information.
 *                          Optional.
 */
RTDECL(int) RTVfsChainOpenObj(const char *pszSpec, uint64_t fFileOpen, uint32_t fObjFlags,
                              PRTVFSOBJ phVfsObj, uint32_t *poffError, PRTERRINFO pErrInfo);

RTDECL(int) RTVfsChainOpenDir(const char *pszSpec, uint32_t fOpen, PRTVFSDIR phVfsDir, uint32_t *poffError, PRTERRINFO pErrInfo);
RTDECL(int) RTVfsChainOpenParentDir(const char *pszSpec, uint32_t fOpen, PRTVFSDIR phVfsDir, const char **ppszChild,
                                    uint32_t *poffError, PRTERRINFO pErrInfo);
RTDECL(int) RTVfsChainOpenFile(const char *pszSpec, uint64_t fOpen, PRTVFSFILE phVfsFile, uint32_t *poffError, PRTERRINFO pErrInfo);
RTDECL(int) RTVfsChainOpenIoStream(const char *pszSpec, uint64_t fOpen, PRTVFSIOSTREAM phVfsIos, uint32_t *poffError, PRTERRINFO pErrInfo);
RTDECL(int) RTVfsChainOpenSymlink(const char *pszSpec, PRTVFSSYMLINK phVfsSym, uint32_t *poffError, PRTERRINFO pErrInfo);

RTDECL(int) RTVfsChainQueryInfo(const char *pszSpec, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAdditionalAttribs,
                                uint32_t fFlags, uint32_t *poffError, PRTERRINFO pErrInfo);

/**
 * Tests if the given string is a chain specification or not.
 *
 * @returns true if it is, false if it isn't.
 * @param   pszSpec         The alleged chain spec.
 */
RTDECL(bool) RTVfsChainIsSpec(const char *pszSpec);

/**
 * Queries the path from the final element.
 *
 * @returns IPRT status code.
 * @retval  VERR_VFS_CHAIN_NOT_PATH_ONLY if the final element isn't just a
 *          simple path.
 * @param   pszSpec         The chain spec.
 * @param   ppszFinalPath   Where to return a copy of the final path on success.
 *                          Call RTStrFree when done.
 * @param   poffError       Where to on error return an offset into @a pszSpec
 *                          of what cause the error.  Optional.
 *
 */
RTDECL(int) RTVfsChainQueryFinalPath(const char *pszSpec, char **ppszFinalPath, uint32_t *poffError);

/**
 * Splits the given chain spec into a final path and the preceding spec.
 *
 * This works on plain paths too.
 *
 * @returns IPRT status code.
 * @param   pszSpec         The chain spec to split.  This will be modified!
 * @param   ppszSpec        Where to return the pointer to the chain spec part.
 *                          This is set to NULL if it's a plain path or a chain
 *                          spec with only a final-path element.
 * @param   ppszFinalPath   Where to return the pointer to the final path.  This
 *                          is set to NULL if no final path.
 * @param   poffError       Where to on error return an offset into @a pszSpec
 *                          of what cause the error.  Optional.
 */
RTDECL(int) RTVfsChainSplitOffFinalPath(char *pszSpec, char **ppszSpec, char **ppszFinalPath, uint32_t *poffError);

/**
 * Common code for reporting errors of a RTVfsChainOpen* API.
 *
 * @param   pszFunction The API called.
 * @param   pszSpec     The VFS chain specification or file path passed to the.
 * @param   rc          The return code.
 * @param   offError    The error offset value returned (0 if not captured).
 * @param   pErrInfo    Additional error information.  Optional.
 *
 * @sa      RTVfsChainMsgErrorExitFailure
 * @sa      RTVfsChainOpenVfs, RTVfsChainOpenFsStream, RTVfsChainOpenDir,
 *          RTVfsChainOpenFile, RTVfsChainOpenIoStream, RTVfsChainOpenSymlink
 */
RTDECL(void) RTVfsChainMsgError(const char *pszFunction, const char *pszSpec, int rc, uint32_t offError, PRTERRINFO pErrInfo);

/**
 * Common code for reporting errors of a RTVfsChainOpen* API.
 *
 * @returns RTEXITCODE_FAILURE
 *
 * @param   pszFunction The API called.
 * @param   pszSpec     The VFS chain specification or file path passed to the.
 * @param   rc          The return code.
 * @param   offError    The error offset value returned (0 if not captured).
 * @param   pErrInfo    Additional error information.  Optional.
 *
 * @sa      RTVfsChainMsgError
 * @sa      RTVfsChainOpenVfs, RTVfsChainOpenFsStream, RTVfsChainOpenDir,
 *          RTVfsChainOpenFile, RTVfsChainOpenIoStream, RTVfsChainOpenSymlink
 */
RTDECL(RTEXITCODE) RTVfsChainMsgErrorExitFailure(const char *pszFunction, const char *pszSpec,
                                                 int rc, uint32_t offError, PRTERRINFO pErrInfo);


/** @} */


/** @} */

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_vfs_h */

