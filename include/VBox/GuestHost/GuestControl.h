/* $Id: GuestControl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest Control - Common Guest and Host Code.
 *
 * @todo r=bird: Just merge this with GuestControlSvc.h!
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_GuestHost_GuestControl_h
#define VBOX_INCLUDED_GuestHost_GuestControl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/time.h>
#include <iprt/path.h>
#include <iprt/types.h>

/* Everything defined in this file lives in this namespace. */
namespace guestControl {

/** Default timeout (in ms) for guest control operations. */
#define GSTCTL_DEFAULT_TIMEOUT_MS   RT_MS_30SEC

/**
 * Process status when executed in the guest.
 */
enum eProcessStatus
{
    /** Process is in an undefined state. */
    PROC_STS_UNDEFINED = 0,
    /** Process has been started. */
    PROC_STS_STARTED = 1,
    /** Process terminated normally. */
    PROC_STS_TEN = 2,
    /** Process terminated via signal. */
    PROC_STS_TES = 3,
    /** Process terminated abnormally. */
    PROC_STS_TEA = 4,
    /** Process timed out and was killed. */
    PROC_STS_TOK = 5,
    /** Process timed out and was not killed successfully. */
    PROC_STS_TOA = 6,
    /** Service/OS is stopping, process was killed. */
    PROC_STS_DWN = 7,
    /** Something went wrong (error code in flags). */
    PROC_STS_ERROR = 8
};

/**
 * Input flags, set by the host. This is needed for
 * handling flags on the guest side.
 * Note: Has to match Main's ProcessInputFlag_* flags!
 */
#define GUEST_PROC_IN_FLAG_NONE                     0x0
#define GUEST_PROC_IN_FLAG_EOF                      RT_BIT(0)

/**
 * Guest session creation flags.
 * Only handled internally at the moment.
 */
#define SESSIONCREATIONFLAG_NONE            0x0

/** @name DIRREMOVEREC_FLAG_XXX - Guest directory removement flags.
 * Essentially using what IPRT's RTDIRRMREC_F_
 * defines have to offer.
 * @{
 */
/** No remove flags specified. */
#define DIRREMOVEREC_FLAG_NONE                 UINT32_C(0x0)
/** Recursively deletes the directory contents. */
#define DIRREMOVEREC_FLAG_RECURSIVE            RT_BIT(0)
/** Delete the content of the directory and the directory itself. */
#define DIRREMOVEREC_FLAG_CONTENT_AND_DIR      RT_BIT(1)
/** Only delete the content of the directory, omit the directory it self. */
#define DIRREMOVEREC_FLAG_CONTENT_ONLY         RT_BIT(2)
/** Mask of valid flags. */
#define DIRREMOVEREC_FLAG_VALID_MASK           UINT32_C(0x00000007)
/** @}   */

/** @name GUEST_PROC_CREATE_FLAG_XXX - Guest process creation flags.
 * @note Has to match Main's ProcessCreateFlag_* flags!
 * @{
 */
#define GUEST_PROC_CREATE_FLAG_NONE             UINT32_C(0x0)
#define GUEST_PROC_CREATE_FLAG_WAIT_START       RT_BIT(0)
#define GUEST_PROC_CREATE_FLAG_IGNORE_ORPHANED  RT_BIT(1)
#define GUEST_PROC_CREATE_FLAG_HIDDEN           RT_BIT(2)
#define GUEST_PROC_CREATE_FLAG_PROFILE          RT_BIT(3)
#define GUEST_PROC_CREATE_FLAG_WAIT_STDOUT      RT_BIT(4)
#define GUEST_PROC_CREATE_FLAG_WAIT_STDERR      RT_BIT(5)
#define GUEST_PROC_CREATE_FLAG_EXPAND_ARGUMENTS RT_BIT(6)
#define GUEST_PROC_CREATE_FLAG_UNQUOTED_ARGS    RT_BIT(7)
/** @} */

/** @name GUEST_PROC_OUT_H_XXX - Pipe handle IDs used internally for referencing
 *        to a certain pipe buffer.
 * @{
 */
#define GUEST_PROC_OUT_H_STDOUT_DEPRECATED  0 /**< Needed for VBox hosts < 4.1.0. */
#define GUEST_PROC_OUT_H_STDOUT             1
#define GUEST_PROC_OUT_H_STDERR             2
/** @} */

/** @name PATHRENAME_FLAG_XXX - Guest path rename flags.
 * Essentially using what IPRT's RTPATHRENAME_FLAGS_XXX have to offer.
 * @{
 */
/** Do not replace anything. */
#define PATHRENAME_FLAG_NO_REPLACE          UINT32_C(0)
/** This will replace attempt any target which isn't a directory. */
#define PATHRENAME_FLAG_REPLACE             RT_BIT(0)
/** Don't allow symbolic links as part of the path. */
#define PATHRENAME_FLAG_NO_SYMLINKS         RT_BIT(1)
/** Mask of valid flags. */
#define PATHRENAME_FLAG_VALID_MASK          UINT32_C(0x00000003)
/** @} */

/** @name GSTCTL_CREATETEMP_F_XXX - Guest temporary directory/file creation flags.
 * @{
 */
/** Does not specify anything. */
#define GSTCTL_CREATETEMP_F_NONE            UINT32_C(0)
/** Creates a directory instead of a file. */
#define GSTCTL_CREATETEMP_F_DIRECTORY       RT_BIT(0)
/** Creates a secure temporary file / directory.
 *  Might not be supported on all (guest) OSes.
 *
 *  @sa IPRT's implementation of RTDirCreateTempSecure() / RTFileCreateTempSecure(). */
#define GSTCTL_CREATETEMP_F_SECURE          RT_BIT(1)
/** Mask of valid flags. */
#define GSTCTL_CREATETEMP_F_VALID_MASK      UINT32_C(0x00000003)
/** @} */

/** @name GSTCTL_CREATEDIRECTORY_F_XXX - Guest directory creation flags.
 * @{
 */
/** Does not specify anything. */
#define GSTCTL_CREATEDIRECTORY_F_NONE                              UINT32_C(0)
/** Also creates parent directories if they don't exist yet. */
#define GSTCTL_CREATEDIRECTORY_F_PARENTS                           RT_BIT(0)
/** Don't allow symbolic links as part of the path. */
#define GSTCTL_CREATEDIRECTORY_F_NO_SYMLINKS                       RT_BIT(1)
/** Set the not-content-indexed flag (default).  Windows only atm. */
#define GSTCTL_CREATEDIRECTORY_F_NOT_CONTENT_INDEXED_DONT_SET      RT_BIT(2)
/** Ignore errors setting the not-content-indexed flag.  Windows only atm. */
#define GSTCTL_CREATEDIRECTORY_F_NOT_CONTENT_INDEXED_NOT_CRITICAL  RT_BIT(3)
/** Ignore umask when applying the mode. */
#define GSTCTL_CREATEDIRECTORY_F_IGNORE_UMASK                      RT_BIT(4)
/** Mask of valid flags. */
#define GSTCTL_CREATEDIRECTORY_F_VALID_MASK                        UINT32_C(0x0000001F)
/** @} */

/** @name GUEST_SHUTDOWN_FLAG_XXX - Guest shutdown flags.
 *
 * @note Must match Main's GuestShutdownFlag_ definitions.
 * @{
 */
#define GUEST_SHUTDOWN_FLAG_NONE            UINT32_C(0)
#define GUEST_SHUTDOWN_FLAG_POWER_OFF       RT_BIT(0)
#define GUEST_SHUTDOWN_FLAG_REBOOT          RT_BIT(1)
#define GUEST_SHUTDOWN_FLAG_FORCE           RT_BIT(2)
/** @} */

/** @name Defines for default (initial) guest process buffer lengths.
 * Note: These defaults were the maximum values before; so be careful when raising those in order to
 *       not break running with older Guest Additions.
 * @{
 */
#define GUEST_PROC_DEF_CMD_LEN        _1K
#define GUEST_PROC_DEF_CWD_LEN        _1K
#define GUEST_PROC_DEF_ARGS_LEN       _1K
#define GUEST_PROC_DEF_ENV_LEN        _1K
#define GUEST_PROC_DEF_USER_LEN       128
#define GUEST_PROC_DEF_PASSWORD_LEN   128
#define GUEST_PROC_DEF_DOMAIN_LEN     256
/** @} */

/** @name Defines for maximum guest process buffer lengths.
 * @{
 */
#define GUEST_PROC_MAX_CMD_LEN            _1M
#define GUEST_PROC_MAX_CWD_LEN            RTPATH_MAX
#define GUEST_PROC_MAX_ARGS_LEN           _2M
#define GUEST_PROC_MAX_ENV_LEN            _4M
#define GUEST_PROC_MAX_USER_LEN           _64K
#define GUEST_PROC_MAX_PASSWORD_LEN       _64K
#define GUEST_PROC_MAX_DOMAIN_LEN         _64K
/** @} */

/** @name Internal tools built into VBoxService which are used in order
 *        to accomplish tasks host<->guest.
 *
 *  @deprecated Since 7.1 the built-in VBoxService tools aren't used anymore.
 * @{
 */
#define VBOXSERVICE_TOOL_CAT        "vbox_cat"
#define VBOXSERVICE_TOOL_LS         "vbox_ls"
#define VBOXSERVICE_TOOL_RM         "vbox_rm"
#define VBOXSERVICE_TOOL_MKDIR      "vbox_mkdir"
#define VBOXSERVICE_TOOL_MKTEMP     "vbox_mktemp"
#define VBOXSERVICE_TOOL_STAT       "vbox_stat"
/** @} */

/** Special process exit codes for "vbox_cat".
 *
 * @deprecated Since 7.1 the built-in VBoxService tools aren't used anymore.
 */
typedef enum VBOXSERVICETOOLBOX_CAT_EXITCODE
{
    VBOXSERVICETOOLBOX_CAT_EXITCODE_ACCESS_DENIED = RTEXITCODE_END,
    VBOXSERVICETOOLBOX_CAT_EXITCODE_FILE_NOT_FOUND,
    VBOXSERVICETOOLBOX_CAT_EXITCODE_PATH_NOT_FOUND,
    VBOXSERVICETOOLBOX_CAT_EXITCODE_SHARING_VIOLATION,
    VBOXSERVICETOOLBOX_CAT_EXITCODE_IS_A_DIRECTORY,
    /** The usual 32-bit type hack. */
    VBOXSERVICETOOLBOX_CAT_32BIT_HACK = 0x7fffffff
} VBOXSERVICETOOLBOX_CAT_EXITCODE;

/** Special process exit codes for "vbox_stat".
 *
 * @deprecated Since 7.1 the built-in VBoxService tools aren't used anymore.
 */
typedef enum VBOXSERVICETOOLBOX_STAT_EXITCODE
{
    VBOXSERVICETOOLBOX_STAT_EXITCODE_ACCESS_DENIED = RTEXITCODE_END,
    VBOXSERVICETOOLBOX_STAT_EXITCODE_FILE_NOT_FOUND,
    VBOXSERVICETOOLBOX_STAT_EXITCODE_PATH_NOT_FOUND,
    VBOXSERVICETOOLBOX_STAT_EXITCODE_NET_PATH_NOT_FOUND,
    VBOXSERVICETOOLBOX_STAT_EXITCODE_INVALID_NAME,
    /** The usual 32-bit type hack. */
    VBOXSERVICETOOLBOX_STAT_32BIT_HACK = 0x7fffffff
} VBOXSERVICETOOLBOX_STAT_EXITCODE;

/**
 * Input status, reported by the client.
 */
enum eInputStatus
{
    /** Input is in an undefined state. */
    INPUT_STS_UNDEFINED = 0,
    /** Input was written (partially, see cbProcessed). */
    INPUT_STS_WRITTEN = 1,
    /** Input failed with an error (see flags for rc). */
    INPUT_STS_ERROR = 20,
    /** Process has abandoned / terminated input handling. */
    INPUT_STS_TERMINATED = 21,
    /** Too much input data. */
    INPUT_STS_OVERFLOW = 30
};

/** @name Guest filesystem flags.
 *
 * @{
 */
/** No guest file system flags set. */
#define GSTCTLFSINFO_F_NONE                         UINT32_C(0)
/** If the filesystem is remote or not. */
#define GSTCTLFSINFO_F_IS_REMOTE                    RT_BIT(0)
/** If the filesystem is case sensitive or not. */
#define GSTCTLFSINFO_F_IS_CASE_SENSITIVE            RT_BIT(1)
/** If the filesystem is mounted read only or not. */
#define GSTCTLFSINFO_F_IS_READ_ONLY                 RT_BIT(2)
/** If the filesystem is compressed or not. */
#define GSTCTLFSINFO_F_IS_COMPRESSED                RT_BIT(3)
/** Valid mask. */
#define GSTCTLFSINFO_F_VALID_MASK                   0xF
/** @} */

/** @name Guest filesystem feature flags.
 *
 * @{
 */
/** No guest file system feature flags set. */
#define GSTCTLFSINFO_FEATURE_F_NONE                 UINT32_C(0)
/** If the filesystem can handle Unicode or not. */
#define GSTCTLFSINFO_FEATURE_F_UNICODE              RT_BIT(0)
/** If the filesystem supports sparse files or not. */
#define GSTCTLFSINFO_FEATURE_F_SPARSE_FILES         RT_BIT(1)
/** If the filesystem features compression of individual files or not. */
#define GSTCTLFSINFO_FEATURE_F_FILE_COMPRESSION     RT_BIT(2)
/** Valid mask. */
#define GSTCTLFSINFO_FEATURE_F_VALID_MASK           0x7
/** @} */

/** Maximum length (in characters) of a guest file name. */
#define GSTCTL_FS_NAME_MAX                          255
/** Maximum length (in characters) of a guest file label. */
#define GSTCTL_FS_LABEL_MAX                         255
/** Maximum length (in characters) of a guest filesystem mount point. */
#define GSTCTL_FS_MOUNTPOINT_MAX                    RTPATH_MAX

/**
 * Guest filesystem information.
 */
#pragma pack(1)
typedef struct GSTCTLFSINFO
{
    /** Remaining free space (in bytes) of the filesystem. */
    uint64_t cbFree;
    /** Total space (in bytes) of the filesystem. */
    uint64_t cbTotalSize;
    /** Block size (in bytes) of the filesystem. */
    uint32_t cbBlockSize;
    /** Sector size (in bytes) of the filesystem. */
    uint32_t cbSectorSize;
    /** Serial number of the filesystem. */
    uint32_t uSerialNumber;
    /** Flags (of type GSTCTLFSINFO_F_XXX). */
    uint32_t fFlags;
    /** Feature flags (of type GSTCTLFSINFO_FEATURE_F_XXX). */
    uint32_t fFeatures;
    /** The maximum size (in characters) of a filesystem object name. */
    uint32_t cMaxComponent;
    /** Name of the filesystem type. */
    char     szName[GSTCTL_FS_NAME_MAX];
    /** Label of the filesystem. */
    char     szLabel[GSTCTL_FS_LABEL_MAX];
    /** Size (in bytes) of \a szMountpoint. */
    uint16_t cbMountpoint;
    /** Mount point of the filesystem.
     *  Will be dynamically allocated, based on \a cbMountpoint. */
    char     szMountpoint[1];
} GSTCTLFSINFO;
#pragma pack()
AssertCompileSize(GSTCTLFSINFO, 553);
/** Pointer to a guest filesystem structure. */
typedef GSTCTLFSINFO *PGSTCTLFSINFO;
/** Pointer to a const guest filesystem structure. */
typedef const GSTCTLFSINFO *PCGSTCTLFSINFO;

/**
 * Guest file system object -- additional information in a GSTCTLFSOBJATTR object.
 */
enum GSTCTLFSOBJATTRADD
{
    /** No additional information is available / requested. */
    GSTCTLFSOBJATTRADD_NOTHING = 1,
    /** The additional unix attributes (RTFSOBJATTR::u::Unix) are available /
     *  requested. */
    GSTCTLFSOBJATTRADD_UNIX,
    /** The additional unix attributes (RTFSOBJATTR::u::UnixOwner) are
     * available / requested. */
    GSTCTLFSOBJATTRADD_UNIX_OWNER,
    /** The additional unix attributes (RTFSOBJATTR::u::UnixGroup) are
     * available / requested. */
    GSTCTLFSOBJATTRADD_UNIX_GROUP,
    /** The additional extended attribute size (RTFSOBJATTR::u::EASize) is available / requested. */
    GSTCTLFSOBJATTRADD_EASIZE,
    /** The last valid item (inclusive).
     * The valid range is RTFSOBJATTRADD_NOTHING thru RTFSOBJATTRADD_LAST.  */
    GSTCTLFSOBJATTRADD_LAST = GSTCTLFSOBJATTRADD_EASIZE,

    /** The usual 32-bit hack. */
    GSTCTLFSOBJATTRADD_32BIT_SIZE_HACK = 0x7fffffff
};

/** The number of bytes reserved for the additional attribute union. */
#define GSTCTLFSOBJATTRUNION_MAX_SIZE       128

/* Validate stuff used in the structures used below. */
AssertCompileSize(RTINODE, 8);
AssertCompileSize(RTDEV,   4);
AssertCompileSize(RTGID,   4);
AssertCompileSize(RTUID,   4);

/**
 * Additional Unix Attributes (GSTCTLFSOBJATTRADD_UNIX).
 */
#pragma pack(1)
typedef struct GSTCTLFSOBJATTRUNIX
{
    /** The user owning the filesystem object (st_uid).
     * This field is NIL_RTUID if not supported. */
    RTUID           uid;

    /** The group the filesystem object is assigned (st_gid).
     * This field is NIL_RTGID if not supported. */
    RTGID           gid;

    /** Number of hard links to this filesystem object (st_nlink).
     * This field is 1 if the filesystem doesn't support hardlinking or
     * the information isn't available.
     */
    uint32_t        cHardlinks;

    /** The device number of the device which this filesystem object resides on (st_dev).
     * This field is 0 if this information is not available. */
    RTDEV           INodeIdDevice;

    /** The unique identifier (within the filesystem) of this filesystem object (st_ino).
     * Together with INodeIdDevice, this field can be used as a OS wide unique id
     * when both their values are not 0.
     * This field is 0 if the information is not available.
     *
     * @remarks  The special '..' dir always shows up with 0 on NTFS/Windows. */
    RTINODE         INodeId;

    /** User flags (st_flags).
     * This field is 0 if this information is not available. */
    uint32_t        fFlags;

    /** The current generation number (st_gen).
     * This field is 0 if this information is not available. */
    uint32_t        GenerationId;

    /** The device number of a character or block device type object (st_rdev).
     * This field is 0 if the file isn't of a character or block device type and
     * when the OS doesn't subscribe to the major+minor device idenfication scheme. */
    RTDEV           Device;
} GSTCTLFSOBJATTRUNIX;
#pragma pack()
AssertCompileSize(GSTCTLFSOBJATTRUNIX, 36);

/**
 * Additional guest Unix attributes (GSTCTLFSOBJATTRADD_UNIX_OWNER).
 */
typedef struct GSTCTLFSOBJATTRUNIXOWNER
{
    /** The user owning the filesystem object (st_uid).
     * This field is NIL_RTUID if not supported. */
    RTUID           uid;
    /** The user name.
     * Empty if not available or not supported, truncated if too long. */
    char            szName[GSTCTLFSOBJATTRUNION_MAX_SIZE - sizeof(RTUID)];
} GSTCTLFSOBJATTRUNIXOWNER;
AssertCompileSize(GSTCTLFSOBJATTRUNIXOWNER, 128);

/**
 * Additional guest Unix attributes (GSTCTLFSOBJATTRADD_UNIX_GROUP).
 */
typedef struct GSTCTLFSOBJATTRUNIXGROUP
{
    /** The user owning the filesystem object (st_uid).
     * This field is NIL_RTUID if not supported. */
    RTGID           gid;
    /** The group name.
     * Empty if not available or not supported, truncated if too long. */
    char            szName[GSTCTLFSOBJATTRUNION_MAX_SIZE - sizeof(RTGID)];
} GSTCTLFSOBJATTRUNIXGROUP;
AssertCompileSize(GSTCTLFSOBJATTRUNIXGROUP, 128);

/**
 * Guest filesystem object attributes.
 */
typedef struct GSTCTLFSOBJATTR
{
    /** Mode flags (st_mode). RTFS_UNIX_*, RTFS_TYPE_*, and RTFS_DOS_*. */
    RTFMODE                     fMode;

    /** The additional attributes available. */
    GSTCTLFSOBJATTRADD          enmAdditional;

    /**
     * Additional attributes.
     *
     * Unless explicitly specified to an API, the API can provide additional
     * data as it is provided by the underlying OS.
     */
    union GSTCTLFSOBJATTRUNION
    {
        /** Additional Unix Attributes - GUEST_FSOBJATTRADD_UNIX. */
        GSTCTLFSOBJATTRUNIX         Unix;
        /** Additional Unix Owner Attributes - GUEST_FSOBJATTRADD_UNIX_OWNER. */
        GSTCTLFSOBJATTRUNIXOWNER    UnixOwner;
        /** Additional Unix Group Attributes - GUEST_FSOBJATTRADD_UNIX_GROUP. */
        GSTCTLFSOBJATTRUNIXGROUP    UnixGroup;

        /**
         * Extended attribute size is available when RTFS_DOS_HAVE_EA_SIZE is set.
         */
        struct GSTCTLFSOBJATTREASIZE
        {
            /** Size of EAs. */
            RTFOFF          cb;
        } EASize;
        /** Reserved space. */
        uint8_t         abReserveSpace[128];
    } u;
} GSTCTLFSOBJATTR;
AssertCompileSize(GSTCTLFSOBJATTR /* 136 */, sizeof(RTFMODE) + sizeof(GSTCTLFSOBJATTRADD) + 128);
/** Pointer to a guest filesystem object attributes structure. */
typedef GSTCTLFSOBJATTR *PGSTCTLFSOBJATTR;
/** Pointer to a const guest filesystem object attributes structure. */
typedef const GSTCTLFSOBJATTR *PCGSTCTLFSOBJATTR;

/** @name GSTCTL_PATH_F_XXX - Generic flags for querying guest file system information.
 * @{ */
/** No guest stat flags specified. */
#define GSTCTL_PATH_F_NONE               UINT32_C(0)
/** Last component: Work on the link. */
#define GSTCTL_PATH_F_ON_LINK            RT_BIT_32(0)
/** Last component: Follow if link. */
#define GSTCTL_PATH_F_FOLLOW_LINK        RT_BIT_32(1)
/** Don't allow symbolic links as part of the path. */
#define GSTCTL_PATH_F_NO_SYMLINKS        RT_BIT_32(2)
/** GSTCTL_PATH_F_XXX flag valid mask. */
#define GSTCTL_PATH_F_VALID_MASK         UINT32_C(0x00000007)
/** @} */

/** @name GSTCTL_DIRLIST_F_XXX - Flags for guest directory listings.
 * @{ */
/** No guest listing flags specified. */
#define GSTCTL_DIRLIST_F_NONE               UINT32_C(0)
/** GSTCTL_DIRLIST_F_XXX valid mask. */
#define GSTCTL_DIRLIST_F_VALID_MASK         UINT32_C(0x00000000)
/** @} */

/**
 * Filter option for HOST_MSG_DIR_OPEN.
 *
 * @note Currently *must* match RTDIRFILTER, so be careful when changing this!
 */
typedef enum GSTCTLDIRFILTER
{
    /** The usual invalid 0 entry. */
    GSTCTLDIRFILTER_INVALID = 0,
    /** No filter should be applied (and none was specified). */
    GSTCTLDIRFILTER_NONE,
    /** The Windows NT filter.
     * The following wildcard chars: *, ?, <, > and "
     * The matching is done on the uppercased strings.  */
    GSTCTLDIRFILTER_WINNT,
    /** The UNIX filter.
     * The following wildcard chars: *, ?, [..]
     * The matching is done on exact case. */
    GSTCTLDIRFILTER_UNIX,
    /** The UNIX filter, uppercased matching.
     * Same as GSTCTLDIRFILTER_UNIX except that the strings are uppercased before comparing. */
    GSTCTLDIRFILTER_UNIX_UPCASED,

    /** The usual full 32-bit value. */
    GSTCTLDIRFILTER_32BIT_HACK = 0x7fffffff
} GSTCTLDIRFILTER;

/** @name GSTCTLDIR_F_XXX - Directory flags for HOST_MSG_DIR_OPEN.
 * @{ */
/** No directory open flags specified. */
#define GSTCTLDIR_F_NONE            UINT32_C(0)
/** Don't allow symbolic links as part of the path.
 * @remarks this flag is currently not implemented and will be ignored. */
#define GSTCTLDIR_F_NO_SYMLINKS     RT_BIT_32(0)
/** Deny relative opening of anything above this directory. */
#define GSTCTLDIR_F_DENY_ASCENT     RT_BIT_32(1)
/** Don't follow symbolic links in the final component. */
#define GSTCTLDIR_F_NO_FOLLOW       RT_BIT_32(2)
/** Long path hack: Don't apply RTPathAbs to the path. */
#define GSTCTLDIR_F_NO_ABS_PATH     RT_BIT_32(3)
/** Valid flag mask.   */
#define GSTCTLDIR_F_VALID_MASK      UINT32_C(0x0000000f)

/**
 * Guest filesystem object information structure.
 *
 * This is returned by
 *     - GUEST_FS_NOTIFYTYPE_QUERY_INFO
 *     - GUEST_DIR_NOTIFYTYPE_READ
 */
typedef struct GSTCTLFSOBJINFO
{
   /** Logical size (st_size).
    * For normal files this is the size of the file.
    * For symbolic links, this is the length of the path name contained
    * in the symbolic link.
    * For other objects this fields needs to be specified.
    */
   RTFOFF           cbObject;

   /** Disk allocation size (st_blocks * DEV_BSIZE). */
   RTFOFF           cbAllocated;

   /** Time of last access (st_atime). */
   RTTIMESPEC       AccessTime;

   /** Time of last data modification (st_mtime). */
   RTTIMESPEC       ModificationTime;

   /** Time of last status change (st_ctime).
    * If not available this is set to ModificationTime.
    */
   RTTIMESPEC       ChangeTime;

   /** Time of file birth (st_birthtime).
    * If not available this is set to ChangeTime.
    */
   RTTIMESPEC       BirthTime;

   /** Attributes. */
   GSTCTLFSOBJATTR  Attr;

} GSTCTLFSOBJINFO;
AssertCompileSize(GSTCTLFSOBJINFO /* 184 */, 48 + sizeof(GSTCTLFSOBJATTR));
/** Pointer to a guest filesystem object information structure. */
typedef GSTCTLFSOBJINFO *PGSTCTLFSOBJINFO;
/** Pointer to a const guest filesystem object information structure. */
typedef const GSTCTLFSOBJINFO *PCGSTCTLFSOBJINFO;

/**
 * Guest directory entry with extended information.
 *
 * This is inspired by IPRT + the PC interfaces.
 */
#pragma pack(1)
typedef struct GSTCTLDIRENTRYEX
{
    /** Full information about the guest object. */
    GSTCTLFSOBJINFO  Info;
    /** The length of the short field (number of RTUTF16 entries (not chars)).
     * It is 16-bit for reasons of alignment. */
    uint16_t        cwcShortName;
    /** The short name for 8.3 compatibility.
     * Empty string if not available.
     * Since the length is a bit tricky for a UTF-8 encoded name, and since this
     * is practically speaking only a windows thing, it is encoded as UCS-2. */
    RTUTF16         wszShortName[14];
    /** The length of the filename. */
    uint16_t        cbName;
    /** The filename. (no path)
     * Using the pcbDirEntry parameter of RTDirReadEx makes this field variable in size. */
    char            szName[260];
} GSTCTLDIRENTRYEX;
#pragma pack()
AssertCompileSize(GSTCTLDIRENTRYEX, sizeof(GSTCTLFSOBJINFO) + 292);
/** Pointer to a guest directory entry. */
typedef GSTCTLDIRENTRYEX *PGSTCTLDIRENTRYEX;
/** Pointer to a const guest directory entry. */
typedef GSTCTLDIRENTRYEX const *PCGSTCTLDIRENTRYEX;

/** The maximum size (in bytes) of an entry file name (at least RT_UOFFSETOF(GSTCTLDIRENTRYEX, szName[2]). */
#define GSTCTL_DIRENTRY_MAX_SIZE                4096
/** Maximum characters of the resolved user name. Including terminator. */
#define GSTCTL_DIRENTRY_MAX_USER_NAME           255
/** Maximum characters of the resolved user groups list. Including terminator. */
#define GSTCTL_DIRENTRY_MAX_USER_GROUPS         _1K
/** The resolved user groups delimiter as a string. */
#define GSTCTL_DIRENTRY_GROUPS_DELIMITER_STR    "\r\n"

/**
 * Guest directory entry header.
 *
 * This is needed for (un-)packing multiple directory entries with its resolved user name + groups
 * with the HOST_MSG_DIR_LIST command.
 *
 * The order of the attributes also mark their packed order, so be careful when changing this!
 *
 * @since 7.1
 */
#pragma pack(1)
typedef struct GSTCTLDIRENTRYLISTHDR
{
    /** Size (in bytes) of the directory header). */
    uint32_t          cbDirEntryEx;
    /** Size (in bytes) of the resolved user name as a string
     *  Includes terminator. */
    uint32_t          cbUser;
    /** Size (in bytes) of the resolved user groups as a string.
     *  Delimited by GSTCTL_DIRENTRY_GROUPS_DELIMITER_STR. Includes terminator. */
    uint32_t          cbGroups;
} GSTCTLDIRENTRYBLOCK;
/** Pointer to a guest directory header entry. */
typedef GSTCTLDIRENTRYLISTHDR *PGSTCTLDIRENTRYLISTHDR;
#pragma pack()
} /* namespace guestControl */

#endif /* !VBOX_INCLUDED_GuestHost_GuestControl_h */

