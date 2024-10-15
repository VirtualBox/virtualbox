/* $Id: SharedClipboard-transfers.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Shared Clipboard - Shared transfer functions between host and guest.
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

#ifndef VBOX_INCLUDED_GuestHost_SharedClipboard_transfers_h
#define VBOX_INCLUDED_GuestHost_SharedClipboard_transfers_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/fs.h>
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
# include <iprt/http-server.h>
#endif
#include <iprt/list.h>

#include <iprt/cpp/list.h>
#include <iprt/cpp/ministring.h>

#include <VBox/GuestHost/SharedClipboard.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>


struct SHCLTRANSFER;
/** Pointer to a single shared clipboard transfer. */
typedef struct SHCLTRANSFER *PSHCLTRANSFER;


/** @name Shared Clipboard transfer definitions.
 *  @{
 */

/** No Shared Clipboard list feature flags defined. */
#define SHCL_TRANSFER_LIST_FEATURE_F_NONE            UINT32_C(0)
/** Is a root list. */
#define SHCL_TRANSFER_LIST_FEATURE_F_ROOT            RT_BIT(0)
/** Shared Clipboard feature flags valid mask. */
#define SHCL_TRANSFER_LIST_FEATURE_F_VALID_MASK      0x1

/** Defines the maximum length (in chars) a Shared Clipboard transfer path can have. */
#define SHCL_TRANSFER_PATH_MAX                  RTPATH_MAX
/** Defines the default maximum transfer chunk size (in bytes) of a Shared Clipboard transfer. */
#define SHCL_TRANSFER_DEFAULT_MAX_CHUNK_SIZE    _64K
/** Defines the default maximum list handles a Shared Clipboard transfer can have. */
#define SHCL_TRANSFER_DEFAULT_MAX_LIST_HANDLES  _4K
/** Defines the default maximum object handles a Shared Clipboard transfer can have. */
#define SHCL_TRANSFER_DEFAULT_MAX_OBJ_HANDLES   _4K
/** Defines the separator for the entries of an URI list (as a string). */
#define SHCL_TRANSFER_URI_LIST_SEP_STR          "\r\n"

/**
 * Defines the transfer status codes.
 */
typedef enum
{
    /** No status set. */
    SHCLTRANSFERSTATUS_NONE = 0,
    /** Requests a transfer to be initialized by the host. Only used for H->G transfers.
     *  Needed as only the host creates new transfer IDs. */
    SHCLTRANSFERSTATUS_REQUESTED = 8,
    /** The transfer has been initialized and is ready to go, but is not running yet.
     *  At this stage the other party can start reading the root list and other stuff. */
    SHCLTRANSFERSTATUS_INITIALIZED = 1,
    /** The transfer has been uninitialized and is not usable anymore. */
    SHCLTRANSFERSTATUS_UNINITIALIZED = 2,
    /** The transfer is active and running. */
    SHCLTRANSFERSTATUS_STARTED = 3,
    /** The transfer has been successfully completed. */
    SHCLTRANSFERSTATUS_COMPLETED = 4,
    /** The transfer has been canceled. */
    SHCLTRANSFERSTATUS_CANCELED = 5,
    /** The transfer has been killed. */
    SHCLTRANSFERSTATUS_KILLED = 6,
    /** The transfer ran into an unrecoverable error.
     *  This results in completely aborting the operation. */
    SHCLTRANSFERSTATUS_ERROR = 7,
    /** The usual 32-bit hack. */
    SHCLTRANSFERSTATUS_32BIT_SIZE_HACK = 0x7fffffff
} SHCLTRANSFERSTATUSENUM;

/** Defines a transfer status. */
typedef uint32_t SHCLTRANSFERSTATUS;

/** @} */

/** @name Shared Clipboard handles.
 *  @{
 */

/** A Shared Clipboard list handle. */
typedef uint64_t SHCLLISTHANDLE;
/** Pointer to a Shared Clipboard list handle. */
typedef SHCLLISTHANDLE *PSHCLLISTHANDLE;
/** Specifies an invalid Shared Clipboard list handle. */
#define NIL_SHCLLISTHANDLE ((SHCLLISTHANDLE)UINT64_MAX)

/** A Shared Clipboard object handle. */
typedef uint64_t SHCLOBJHANDLE;
/** Pointer to a Shared Clipboard object handle. */
typedef SHCLOBJHANDLE *PSHCLOBJHANDLE;
/** Specifies an invalid Shared Clipboard object handle. */
#define NIL_SHCLOBJHANDLE ((SHCLOBJHANDLE)UINT64_MAX)

/** @} */

/** @name Shared Clipboard open/create flags.
 *  @{
 */
/** No flags. Initialization value. */
#define SHCL_OBJ_CF_NONE                    UINT32_C(0x00000000)

#if 0 /* These probably won't be needed either */
/** Lookup only the object, do not return a handle. All other flags are ignored. */
#define SHCL_OBJ_CF_LOOKUP                  UINT32_C(0x00000001)
/** Create/open a directory. */
#define SHCL_OBJ_CF_DIRECTORY               UINT32_C(0x00000004)
#endif

/** Read/write requested access for the object. */
#define SHCL_OBJ_CF_ACCESS_MASK_RW          UINT32_C(0x00001000)
/** No access requested. */
#define SHCL_OBJ_CF_ACCESS_NONE             UINT32_C(0x00000000)
/** Read access requested. */
#define SHCL_OBJ_CF_ACCESS_READ             UINT32_C(0x00001000)

/** Requested share access for the object. */
#define SHCL_OBJ_CF_ACCESS_MASK_DENY        UINT32_C(0x00008000)
/** Allow any access. */
#define SHCL_OBJ_CF_ACCESS_DENYNONE         UINT32_C(0x00000000)
/** Do not allow write. */
#define SHCL_OBJ_CF_ACCESS_DENYWRITE        UINT32_C(0x00008000)

/** Requested access to attributes of the object. */
#define SHCL_OBJ_CF_ACCESS_MASK_ATTR        UINT32_C(0x00010000)
/** No access requested. */
#define SHCL_OBJ_CF_ACCESS_ATTR_NONE        UINT32_C(0x00000000)
/** Read access requested. */
#define SHCL_OBJ_CF_ACCESS_ATTR_READ        UINT32_C(0x00010000)

/** Valid bits. */
#define SHCL_OBJ_CF_VALID_MASK              UINT32_C(0x00019000)
/** @} */

/**
 * The available additional information in a SHCLFSOBJATTR object.
 * @sa RTFSOBJATTRADD
 */
typedef enum _SHCLFSOBJATTRADD
{
    /** No additional information is available / requested. */
    SHCLFSOBJATTRADD_NOTHING = 1,
    /** The additional unix attributes (SHCLFSOBJATTR::u::Unix) are
     *  available / requested. */
    SHCLFSOBJATTRADD_UNIX,
    /** The additional extended attribute size (SHCLFSOBJATTR::u::EASize) is
     *  available / requested. */
    SHCLFSOBJATTRADD_EASIZE,
    /** The last valid item (inclusive).
     * The valid range is SHCLFSOBJATTRADD_NOTHING thru
     * SHCLFSOBJATTRADD_LAST. */
    SHCLFSOBJATTRADD_LAST = SHCLFSOBJATTRADD_EASIZE,
    /** The usual 32-bit hack. */
    SHCLFSOBJATTRADD_32BIT_SIZE_HACK = 0x7fffffff
} SHCLFSOBJATTRADD;


/* Assert sizes of the IRPT types we're using below. */
AssertCompileSize(RTFMODE,      4);
AssertCompileSize(RTFOFF,       8);
AssertCompileSize(RTINODE,      8);
AssertCompileSize(RTTIMESPEC,   8);
AssertCompileSize(RTDEV,        4);
AssertCompileSize(RTUID,        4);

/**
 * Shared Clipboard filesystem object attributes.
 *
 * @sa RTFSOBJATTR
 */
typedef struct _SHCLFSOBJATTR
{
    /** Mode flags (st_mode). RTFS_UNIX_*, RTFS_TYPE_*, and RTFS_DOS_*.
     * @remarks We depend on a number of RTFS_ defines to remain unchanged.
     *          Fortuntately, these are depending on windows, dos and unix
     *          standard values, so this shouldn't be much of a pain. */
    RTFMODE          fMode;

    /** The additional attributes available. */
    SHCLFSOBJATTRADD enmAdditional;

    /**
     * Additional attributes.
     *
     * Unless explicitly specified to an API, the API can provide additional
     * data as it is provided by the underlying OS.
     */
    union SHCLFSOBJATTRUNION
    {
        /** Additional Unix Attributes
         * These are available when SHCLFSOBJATTRADD is set in fUnix.
         */
         struct SHCLFSOBJATTRUNIX
         {
            /** The user owning the filesystem object (st_uid).
             * This field is ~0U if not supported. */
            RTUID           uid;

            /** The group the filesystem object is assigned (st_gid).
             * This field is ~0U if not supported. */
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
             * This field is 0 if the information is not available. */
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
        } Unix;

        /**
         * Extended attribute size.
         */
        struct SHCLFSOBJATTREASIZE
        {
            /** Size of EAs. */
            RTFOFF          cb;
        } EASize;

        /** Padding the structure to a multiple of 8 bytes. */
        uint64_t au64Padding[5];
    } u;
} SHCLFSOBJATTR;
AssertCompileSize(SHCLFSOBJATTR, 48);
/** Pointer to a Shared Clipboard filesystem object attributes structure. */
typedef SHCLFSOBJATTR *PSHCLFSOBJATTR;
/** Pointer to a const Shared Clipboard filesystem object attributes structure. */
typedef const SHCLFSOBJATTR *PCSHCLFSOBJATTR;

/**
 * Shared Clipboard file system object information structure.
 *
 * @sa RTFSOBJINFO
 */
typedef struct _SHCLFSOBJINFO
{
   /** Logical size (st_size).
    * For normal files this is the size of the file.
    * For symbolic links, this is the length of the path name contained
    * in the symbolic link.
    * For other objects this fields needs to be specified.
    */
   RTFOFF       cbObject;

   /** Disk allocation size (st_blocks * DEV_BSIZE). */
   RTFOFF       cbAllocated;

   /** Time of last access (st_atime).
    * @remarks  Here (and other places) we depend on the IPRT timespec to
    *           remain unchanged. */
   RTTIMESPEC   AccessTime;

   /** Time of last data modification (st_mtime). */
   RTTIMESPEC   ModificationTime;

   /** Time of last status change (st_ctime).
    * If not available this is set to ModificationTime.
    */
   RTTIMESPEC   ChangeTime;

   /** Time of file birth (st_birthtime).
    * If not available this is set to ChangeTime.
    */
   RTTIMESPEC   BirthTime;

   /** Attributes. */
   SHCLFSOBJATTR Attr;

} SHCLFSOBJINFO;
AssertCompileSize(SHCLFSOBJINFO, 96);
/** Pointer to a Shared Clipboard filesystem object information structure. */
typedef SHCLFSOBJINFO *PSHCLFSOBJINFO;
/** Pointer to a const Shared Clipboard filesystem object information
 *  structure. */
typedef const SHCLFSOBJINFO *PCSHCLFSOBJINFO;

/**
 * Structure for keeping object open/create parameters.
 */
typedef struct _SHCLOBJOPENCREATEPARMS
{
    /** Path to object to open / create.
     *  Always stored as UNIX-style paths ('/').
     *  Backslashes ('\') can be part of a UNIX path though. */
    char                       *pszPath;
    /** Size (in bytes) of path to to object. */
    uint32_t                    cbPath;
    /** SHCL_OBJ_CF_* */
    uint32_t                    fCreate;
    /**
     * Attributes of object to open/create and
     * returned actual attributes of opened/created object.
     */
    SHCLFSOBJINFO    ObjInfo;
} SHCLOBJOPENCREATEPARMS;
/** Pointer to Shared Clipboard object open/create parameters. */
typedef SHCLOBJOPENCREATEPARMS *PSHCLOBJOPENCREATEPARMS;

/**
 * Structure for keeping a reply message.
 */
typedef struct _SHCLREPLY
{
    /** Message type (of type VBOX_SHCL_TX_REPLYMSGTYPE_TRANSFER_XXX). */
    uint32_t uType;
    /** IPRT result of overall operation. Note: int vs. uint32! */
    uint32_t rc;
    union
    {
        /** For VBOX_SHCL_TX_REPLYMSGTYPE_TRANSFER_STATUS. */
        struct
        {
            SHCLTRANSFERSTATUS uStatus;
        } TransferStatus;
        /** For VBOX_SHCL_TX_REPLYMSGTYPE_LIST_OPEN. */
        struct
        {
            SHCLLISTHANDLE uHandle;
        } ListOpen;
        /** For VBOX_SHCL_TX_REPLYMSGTYPE_LIST_CLOSE. */
        struct
        {
            SHCLLISTHANDLE uHandle;
        } ListClose;
        /** For VBOX_SHCL_TX_REPLYMSGTYPE_OBJ_OPEN. */
        struct
        {
            SHCLOBJHANDLE uHandle;
        } ObjOpen;
        /** For VBOX_SHCL_TX_REPLYMSGTYPE_OBJ_CLOSE. */
        struct
        {
            SHCLOBJHANDLE uHandle;
        } ObjClose;
    } u;
    /** Pointer to optional payload. */
    void    *pvPayload;
    /** Payload size (in bytes). */
    uint32_t cbPayload;
} SHCLREPLY;
/** Pointer to a Shared Clipboard reply. */
typedef SHCLREPLY *PSHCLREPLY;

struct _SHCLLISTENTRY;
typedef _SHCLLISTENTRY SHCLLISTENTRY;

/**
 * Structure for maintaining Shared Clipboard list open parameters.
 */
typedef struct _SHCLLISTOPENPARMS
{
    /** Listing flags (see VBOX_SHCL_LIST_FLAG_XXX). */
    uint32_t fList;
    /** Size (in bytes) of the filter string. */
    uint32_t cbFilter;
    /** Filter string. DOS wilcard-style. */
    char    *pszFilter;
    /** Size (in bytes) of the listing path. */
    uint32_t cbPath;
    /** Listing path (absolute). If empty or NULL the listing's root path will be opened.
     *  We always use UNIX-style paths. */
    char    *pszPath;
} SHCLLISTOPENPARMS;
/** Pointer to Shared Clipboard list open parameters. */
typedef SHCLLISTOPENPARMS *PSHCLLISTOPENPARMS;

/**
 * Structure for keeping a Shared Clipboard list header.
 */
typedef struct _SHCLLISTHDR
{
    /** Feature flag(s) of type SHCL_TRANSFER_LIST_FEATURE_F_XXX. */
    uint32_t fFeatures;
    /** Total entries of the list. */
    uint64_t cEntries;
    /** Total size (in bytes) returned. */
    uint64_t cbTotalSize;
} SHCLLISTHDR;
/** Pointer to a Shared Clipboard list header. */
typedef SHCLLISTHDR *PSHCLLISTHDR;

/**
 * Structure for a generic Shared Clipboard list entry.
 */
typedef struct _SHCLLISTENTRY
{
    /** List node. */
    RTLISTNODE Node;
    /** Entry name. */
    char      *pszName;
    /** Size (in bytes) of entry name.
     *  Includes terminator. */
    uint32_t   cbName;
    /** Information flag(s). Of type VBOX_SHCL_INFO_F_XXX. */
    uint32_t   fInfo;
    /** Size (in bytes) of the actual list entry. */
    uint32_t   cbInfo;
    /** Data of the actual list entry. */
    void      *pvInfo;
} SHCLLISTENTRY;
/** Pointer to a Shared Clipboard list entry. */
typedef SHCLLISTENTRY *PSHCLLISTENTRY;
/** Pointer to a const Shared Clipboard list entry. */
typedef SHCLLISTENTRY *PCSHCLLISTENTRY;

/** Maximum length (in UTF-8 characters) of a list entry name. Includes terminator. */
#define SHCLLISTENTRY_MAX_NAME     4096

/**
 * Structure for a generic Shared Clipboard list.
 */
typedef struct _SHCLLIST
{
    /** List header. */
    SHCLLISTHDR    Hdr;
    /** List entries of type PSHCLLISTENTRY. */
    RTLISTANCHOR   lstEntries;
} SHCLLIST;
/** Pointer to a generic Shared Clipboard transfer transfer list. */
typedef SHCLLIST *PSHCLLIST;

/**
 * Structure for keeping a Shared Clipboard object data chunk.
 */
typedef struct _SHCLOBJDATACHUNK
{
    /** Handle of object this data chunk is related to. */
    uint64_t  uHandle;
    /** Pointer to actual data chunk. */
    void     *pvData;
    /** Size (in bytes) of data chunk. */
    uint32_t  cbData;
} SHCLOBJDATACHUNK;
/** Pointer to a Shared Clipboard transfer object data chunk. */
typedef SHCLOBJDATACHUNK *PSHCLOBJDATACHUNK;

/**
 * Structure for handling a single transfer object context.
 */
typedef struct _SHCLCLIENTTRANSFEROBJCTX
{
    /** Pointer to the actual transfer object of this context. */
    SHCLTRANSFER *pTransfer;
    /** Object handle of this transfer context. */
    SHCLOBJHANDLE uHandle;
} SHCLCLIENTTRANSFEROBJCTX;
/** Pointer to a Shared Clipboard transfer object context. */
typedef SHCLCLIENTTRANSFEROBJCTX *PSHCLCLIENTTRANSFEROBJCTX;

typedef struct _SHCLTRANSFEROBJSTATE
{
    /** How many bytes were processed (read / write) so far. */
    uint64_t cbProcessed;
} SHCLTRANSFEROBJSTATE;
/** Pointer to a Shared Clipboard transfer object state. */
typedef SHCLTRANSFEROBJSTATE *PSHCLTRANSFEROBJSTATE;

/**
 * Enumeration for specifying a Shared Clipboard object type.
 */
typedef enum _SHCLOBJTYPE
{
    /** Invalid object type. */
    SHCLOBJTYPE_INVALID = 0,
    /** Object is a directory. */
    SHCLOBJTYPE_DIRECTORY,
    /** Object is a file. */
    SHCLOBJTYPE_FILE,
    /** Object is a symbolic link. */
    SHCLOBJTYPE_SYMLINK,
    /** The usual 32-bit hack. */
    SHCLOBJTYPE_32BIT_SIZE_HACK = 0x7fffffff
} SHCLOBJTYPE;

/**
 * Structure for a single Shared Clipboard transfer object.
 */
typedef struct _SHCLTRANSFEROBJ
{
    /** The list node. */
    RTLISTNODE           Node;
    /** Handle of the object. */
    SHCLOBJHANDLE        hObj;
    /** Absolute (local) path of the object. Source-style path. */
    char                *pszPathLocalAbs;
    /** Object file system information. */
    SHCLFSOBJINFO        objInfo;
    /** Source the object originates from. */
    SHCLSOURCE           enmSource;
    /** Current state. */
    SHCLTRANSFEROBJSTATE State;
    /** Type of object handle. */
    SHCLOBJTYPE          enmType;
    /** Data union, based on \a enmType. */
    union
    {
        /** Local data. */
        struct
        {
            union
            {
                RTDIR  hDir;
                RTFILE hFile;
            };
        } Local;
    } u;
} SHCLTRANSFEROBJ;
/** Pointer to a Shared Clipboard transfer object. */
typedef SHCLTRANSFEROBJ *PSHCLTRANSFEROBJ;

/**
 * Structure for keeping transfer list handle information.
 *
 * This is using to map own (local) handles to the underlying file system.
 */
typedef struct _SHCLLISTHANDLEINFO
{
    /** The list node. */
    RTLISTNODE      Node;
    /** The list's handle. */
    SHCLLISTHANDLE  hList;
    /** Type of list handle. */
    SHCLOBJTYPE     enmType;
    /** Absolute local path of the list object. */
    char           *pszPathLocalAbs;
    union
    {
        /** Local data, based on enmType. */
        struct
        {
            union
            {
                RTDIR  hDir;
                RTFILE hFile;
            };
        } Local;
    } u;
} SHCLLISTHANDLEINFO;
/** Pointer to a Shared Clipboard transfer list handle info. */
typedef SHCLLISTHANDLEINFO *PSHCLLISTHANDLEINFO;

/**
 * Structure for maintaining an Shared Clipboard transfer state.
 * Everything in here will be part of a saved state (later).
 */
typedef struct _SHCLTRANSFERSTATE
{
    /** The transfer's (local) ID. */
    SHCLTRANSFERID     uID;
    /** The transfer's current status. */
    SHCLTRANSFERSTATUS enmStatus;
    /** The transfer's direction, seen from the perspective who created the transfer. */
    SHCLTRANSFERDIR    enmDir;
    /** The transfer's source, seen from the perspective who created the transfer. */
    SHCLSOURCE         enmSource;
} SHCLTRANSFERSTATE;
/** Pointer to a Shared Clipboard transfer state. */
typedef SHCLTRANSFERSTATE *PSHCLTRANSFERSTATE;

/**
 * Structure maintaining clipboard transfer provider context data.
 *
 * This is handed-in to the provider interface implementations.
 */
typedef struct _SHCLTXPROVIDERCTX
{
    /** Pointer to the related Shared Clipboard transfer. */
    PSHCLTRANSFER pTransfer;
    /** User-defined data pointer. Can be NULL if not needed. */
    void         *pvUser;
    /** Size (in bytes) of data at user pointer. */
    size_t        cbUser;
} SHCLTXPROVIDERCTX;
/** Pointer to Shared Clipboard transfer provider context data. */
typedef SHCLTXPROVIDERCTX *PSHCLTXPROVIDERCTX;

struct _SHCLTRANSFERCTX;
typedef struct _SHCLTRANSFERCTX *PSHCLTRANSFERCTX;

/**
 * Shared Clipboard transfer provider interface table.
 *
 * A transfer provider inteface implementation realizes all low level functions
 * needed for making a Shared Clipboard transfer happen.
 */
typedef struct _SHCLTXPROVIDERIFACE
{
    /**
     * Reads the clipboard transfer root list.
     *
     * Depending on the provider, this queries information for the root entries.
     *
     * @returns VBox status code.
     * @param   pCtx                Provider context to use.
     */
    DECLCALLBACKMEMBER(int, pfnRootListRead,(PSHCLTXPROVIDERCTX pCtx));
    /**
     * Opens a transfer list.
     *
     * @returns VBox status code.
     * @param   pCtx                Provider context to use.
     * @param   pOpenParms          List open parameters to use for opening.
     * @param   phList              Where to store the List handle of opened list on success.
     */
    DECLCALLBACKMEMBER(int, pfnListOpen,(PSHCLTXPROVIDERCTX pCtx, PSHCLLISTOPENPARMS pOpenParms, PSHCLLISTHANDLE phList));
    /**
     * Closes a transfer list.
     *
     * @returns VBox status code.
     * @param   pCtx                Provider context to use.
     * @param   hList               Handle of list to close.
     */
    DECLCALLBACKMEMBER(int, pfnListClose,(PSHCLTXPROVIDERCTX pCtx, SHCLLISTHANDLE hList));
    /**
     * Reads the list header.
     *
     * @returns VBox status code.
     * @param   pCtx                Provider context to use.
     * @param   hList               List handle of list to read header for.
     * @param   pListHdr            Where to store the list header read.
     */
    DECLCALLBACKMEMBER(int, pfnListHdrRead,(PSHCLTXPROVIDERCTX pCtx, SHCLLISTHANDLE hList, PSHCLLISTHDR pListHdr));
    /**
     * Writes the list header.
     *
     * @returns VBox status code.
     * @param   pCtx                Provider context to use.
     * @param   hList               List handle of list to write header for.
     * @param   pListHdr            List header to write.
     */
    DECLCALLBACKMEMBER(int, pfnListHdrWrite,(PSHCLTXPROVIDERCTX pCtx, SHCLLISTHANDLE hList, PSHCLLISTHDR pListHdr));
    /**
     * Reads a single transfer list entry.
     *
     * @returns VBox status code or VERR_NO_MORE_FILES if the end of the list has been reached.
     * @param   pCtx                Provider context to use.
     * @param   hList               List handle of list to read from.
     * @param   pListEntry          Where to store the read information.
     */
    DECLCALLBACKMEMBER(int, pfnListEntryRead,(PSHCLTXPROVIDERCTX pCtx, SHCLLISTHANDLE hList, PSHCLLISTENTRY pListEntry));
    /**
     * Writes a single transfer list entry.
     *
     * @returns VBox status code.
     * @param   pCtx                Provider context to use.
     * @param   hList               List handle of list to write to.
     * @param   pListEntry          Entry information to write.
     */
    DECLCALLBACKMEMBER(int, pfnListEntryWrite,(PSHCLTXPROVIDERCTX pCtx, SHCLLISTHANDLE hList, PSHCLLISTENTRY pListEntry));
    /**
     * Opens a transfer object.
     *
     * @returns VBox status code.
     * @param   pCtx                Provider context to use.
     * @param   pCreateParms        Open / create parameters of transfer object to open / create.
     * @param   phObj               Where to store the handle of transfer object opened on success.
     */
    DECLCALLBACKMEMBER(int, pfnObjOpen,(PSHCLTXPROVIDERCTX pCtx, PSHCLOBJOPENCREATEPARMS pCreateParms, PSHCLOBJHANDLE phObj));
    /**
     * Closes a transfer object.
     *
     * @returns VBox status code.
     * @param   pCtx                Provider context to use.
     * @param   hObj                Handle of transfer object to close.
     */
    DECLCALLBACKMEMBER(int, pfnObjClose,(PSHCLTXPROVIDERCTX pCtx, SHCLOBJHANDLE hObj));
    /**
     * Reads from a transfer object.
     *
     * @returns VBox status code.
     * @param   pCtx                Provider context to use.
     * @param   hObj                Handle of transfer object to read from.
     * @param   pvData              Buffer for where to store the read data.
     * @param   cbData              Size (in bytes) of buffer.
     * @param   fFlags              Read flags. Optional.
     * @param   pcbRead             Where to return how much bytes were read on success. Optional.
     */
    DECLCALLBACKMEMBER(int, pfnObjRead,(PSHCLTXPROVIDERCTX pCtx, SHCLOBJHANDLE hObj, void *pvData, uint32_t cbData,
                                        uint32_t fFlags, uint32_t *pcbRead));
    /**
     * Writes to a transfer object.
     *
     * @returns VBox status code.
     * @param   pCtx                Provider context to use.
     * @param   hObj                Handle of transfer object to write to.
     * @param   pvData              Buffer of data to write.
     * @param   cbData              Size (in bytes) of buffer to write.
     * @param   fFlags              Write flags. Optional.
     * @param   pcbWritten          How much bytes were writtenon success. Optional.
     */
    DECLCALLBACKMEMBER(int, pfnObjWrite,(PSHCLTXPROVIDERCTX pCtx, SHCLOBJHANDLE hObj, void *pvData, uint32_t cbData,
                                         uint32_t fFlags, uint32_t *pcbWritten));
} SHCLTXPROVIDERIFACE;
/** Pointer to a Shared Clipboard transfer provider interface table. */
typedef SHCLTXPROVIDERIFACE *PSHCLTXPROVIDERIFACE;

/** Queries (assigns) a Shared Clipboard provider interface. */
#define SHCLTXPROVIDERIFACE_QUERY(a_Iface, a_Name) \
    a_Iface->pfnRootListRead   = a_Name ## RootListRead; \
    a_Iface->pfnListOpen       = a_Name ## ListOpen; \
    a_Iface->pfnListClose      = a_Name ## ListClose; \
    a_Iface->pfnListHdrRead    = a_Name ## ListHdrRead; \
    a_Iface->pfnListHdrWrite   = a_Name ## ListHdrWrite; \
    a_Iface->pfnListEntryRead  = a_Name ## ListEntryRead; \
    a_Iface->pfnListEntryWrite = a_Name ## ListEntryWrite; \
    a_Iface->pfnObjOpen        = a_Name ## ObjOpen; \
    a_Iface->pfnObjClose       = a_Name ## ObjClose; \
    a_Iface->pfnObjRead        = a_Name ## ObjRead; \
    a_Iface->pfnObjWrite       = a_Name ## ObjWrite;

/** Queries (assigns) a Shared Clipboard provider interface + returns the interface pointer. */
#define SHCLTXPROVIDERIFACE_QUERY_RET(a_Iface, a_Name) \
    SHCLTXPROVIDERIFACE_QUERY(a_Iface, a_Name); return a_Iface;

/**
 * Structure for Shared Clipboard transfer provider.
 */
typedef struct _SHCLTXPROVIDER
{
    /** Specifies what the source of the provider is. */
    SHCLSOURCE           enmSource;
    /** The provider interface table to use. */
    SHCLTXPROVIDERIFACE  Interface;
    /** User-provided callback data. */
    void                *pvUser;
    /** Size (in bytes) of data at user pointer. */
    size_t               cbUser;
} SHCLTXPROVIDER;
/** Pointer to Shared Clipboard transfer provider. */
typedef SHCLTXPROVIDER *PSHCLTXPROVIDER;

/**
 * Structure maintaining clipboard transfer callback context data.
 */
typedef struct _SHCLTRANSFERCALLBACKCTX
{
    /** Pointer to the related Shared Clipboard transfer. */
    PSHCLTRANSFER pTransfer;
    /** User-defined data pointer. Can be NULL if not needed. */
    void         *pvUser;
    /** Size (in bytes) of data at user pointer. */
    size_t        cbUser;
} SHCLTRANSFERCALLBACKCTX;
/** Pointer to a Shared Clipboard transfer callback context. */
typedef SHCLTRANSFERCALLBACKCTX *PSHCLTRANSFERCALLBACKCTX;

/**
 * Shared Clipboard transfer callback table.
 *
 * All callbacks are optional.
 * Callbacks which can veto the caller have a return value.
 */
typedef struct _SHCLTRANSFERCALLBACKS
{
    /**
     * Called after the transfer got created.
     *
     * @param   pCbCtx              Pointer to callback context to use.
     */
    DECLCALLBACKMEMBER(void,  pfnOnCreated,(PSHCLTRANSFERCALLBACKCTX pCbCtx));
    /**
     * Called when the transfer gets initialized.
     *
     * @return  VBox status code. On error the intialization will will be treated as failed.
     * @param   pCbCtx              Pointer to callback context to use.
    */
    DECLCALLBACKMEMBER(int,  pfnOnInitialize,(PSHCLTRANSFERCALLBACKCTX pCbCtx));
    /**
     * Called after the transfer got initialized.
     *
     * @param   pCbCtx              Pointer to callback context to use.
     */
    DECLCALLBACKMEMBER(void,  pfnOnInitialized,(PSHCLTRANSFERCALLBACKCTX pCbCtx));
    /**
     * Called before the transfer gets destroyed.
     *
     * @param   pCbCtx              Pointer to callback context to use.
     */
    DECLCALLBACKMEMBER(void,  pfnOnDestroy,(PSHCLTRANSFERCALLBACKCTX pCbCtx));
    /**
     * Called after the transfer entered the started state.
     *
     * @param   pCbCtx              Pointer to callback context to use.
     */
    DECLCALLBACKMEMBER(void,  pfnOnStarted,(PSHCLTRANSFERCALLBACKCTX pCbCtx));
    /**
     * Called when the transfer has been completed.
     *
     * @param   pCbCtx              Pointer to callback context to use.
     * @param   rcCompletion        Completion result.
     *                              VERR_CANCELED if transfer has been canceled.
     */
    DECLCALLBACKMEMBER(void, pfnOnCompleted,(PSHCLTRANSFERCALLBACKCTX pCbCtx, int rcCompletion));
    /**
     * Called when transfer resulted in an unrecoverable error.
     *
     * @param   pCbCtx              Pointer to callback context to use.
     * @param   rcError             Error reason, IPRT-style.
     */
    DECLCALLBACKMEMBER(void, pfnOnError,(PSHCLTRANSFERCALLBACKCTX pCbCtx, int rcError));
    /**
     * Called after a transfer got registered to a transfer context.
     *
     * @param   pCbCtx              Pointer to callback context to use.
     * @param   pTransferCtx        Transfer context transfer was registered to.
     */
    DECLCALLBACKMEMBER(void, pfnOnRegistered,(PSHCLTRANSFERCALLBACKCTX pCbCtx, PSHCLTRANSFERCTX pTransferCtx));
    /**
     * Called after a transfer got unregistered from a transfer context.
     *
     * @param   pCbCtx              Pointer to callback context to use.
     * @param   pTransferCtx        Transfer context transfer was unregistered from.
     */
    DECLCALLBACKMEMBER(void, pfnOnUnregistered,(PSHCLTRANSFERCALLBACKCTX pCbCtx, PSHCLTRANSFERCTX pTransferCtx));

    /** User-provided callback data. Can be NULL if not used. */
    void  *pvUser;
    /** Size (in bytes) of data pointer at \a pvUser. */
    size_t cbUser;
} SHCLTRANSFERCALLBACKS;
/** Pointer to a Shared Clipboard transfer callback table. */
typedef SHCLTRANSFERCALLBACKS *PSHCLTRANSFERCALLBACKS;

/** Function pointer for a transfer thread function. */
typedef DECLCALLBACKPTR(int, PFNSHCLTRANSFERTHREAD,(PSHCLTRANSFER pTransfer, void *pvUser));

/**
 * Structure for thread-related members for a single Shared Clipboard transfer.
 */
typedef struct _SHCLTRANSFERTHREAD
{
    /** Thread handle for the reading / writing thread.
     *  Can be NIL_RTTHREAD if not being used. */
    RTTHREAD                    hThread;
    /** Thread started indicator. */
    volatile bool               fStarted;
    /** Thread stop flag. */
    volatile bool               fStop;
    /** Thread cancelled flag / indicator. */
    volatile bool               fCancelled;
} SHCLTRANSFERTHREAD;
/** Pointer to a Shared Clipboard transfer thread. */
typedef SHCLTRANSFERTHREAD *PSHCLTRANSFERTHREAD;

/**
 * A single Shared Clipboard transfer.
 *
 ** @todo Not yet thread safe.
 */
typedef struct SHCLTRANSFER
{
    /** The node member for using this struct in a RTList. */
    RTLISTNODE                Node;
    /** Critical section for serializing access. */
    RTCRITSECT                CritSect;
    /** Number of references to this transfer. */
    uint32_t                  cRefs;
    /** The transfer's state (for SSM, later). */
    SHCLTRANSFERSTATE         State;
    /** Absolute path to root entries. */
    char                     *pszPathRootAbs;
    /** Timeout (in ms) for waiting of events. */
    RTMSINTERVAL              uTimeoutMs;
    /** Maximum data chunk size (in bytes) to transfer. Default is 64K. */
    uint32_t                  cbMaxChunkSize;
    /** Status change event. */
    RTSEMEVENT                StatusChangeEvent;
    /** The transfer's own event source. */
    SHCLEVENTSOURCE           Events;
    /** Current number of concurrent list handles in \a lstHandles. */
    uint32_t                  cListHandles;
    /** Maximum number of concurrent list handles allowed. */
    uint32_t                  cMaxListHandles;
    /** Next upcoming list handle. For book keeping. */
    SHCLLISTHANDLE            uListHandleNext;
    /** List of all list handles related to this transfer. */
    RTLISTANCHOR              lstHandles;
    /** List of root entries of this transfer. */
    SHCLLIST                  lstRoots;
    /** Current number of concurrent object handles. in \a lstObj. */
    uint32_t                  cObjHandles;
    /** Maximum number of concurrent object handles. */
    uint32_t                  cMaxObjHandles;
    /** Next upcoming object handle. For book keeping. */
    SHCLOBJHANDLE             uObjHandleNext;
    /** Map of all objects handles related to this transfer. */
    RTLISTANCHOR              lstObj;
    /** The transfer's own provider context. */
    SHCLTXPROVIDERCTX         ProviderCtx;
    /** The transfer's provider interface. */
    SHCLTXPROVIDERIFACE       ProviderIface;
    /** The transfer's callback context. */
    SHCLTRANSFERCALLBACKCTX   CallbackCtx;
    /** The transfer's callback table. */
    SHCLTRANSFERCALLBACKS     Callbacks;
    /** Opaque pointer to implementation-specific parameters. */
    void                     *pvUser;
    /** Size (in bytes) of implementation-specific parameters. */
    size_t                    cbUser;
    /** Contains thread-related attributes. */
    SHCLTRANSFERTHREAD        Thread;
} SHCLTRANSFER;
/** Pointer to a Shared Clipboard transfer. */
typedef SHCLTRANSFER *PSHCLTRANSFER;

/**
 * Structure for keeping an Shared Clipboard transfer status report.
 */
typedef struct _SHCLTRANSFERREPORT
{
    /** Actual status to report. */
    SHCLTRANSFERSTATUS    uStatus;
    /** Result code (rc) to report; might be unused / invalid, based on enmStatus. */
    int                   rc;
    /** Reporting flags. Currently unused and must be 0. */
    uint32_t              fFlags;
} SHCLTRANSFERREPORT;
/** Pointer to Shared Clipboard transfer status. */
typedef SHCLTRANSFERREPORT *PSHCLTRANSFERREPORT;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
/**
 * Enumeration for HTTP server status changes.
 *
 * Keep those as flags, so that we can wait for multiple statuses, if ever needed.
 */
typedef enum _SHCLHTTPSERVERSTATUS
{
    /** No status set yet. */
    SHCLHTTPSERVERSTATUS_NONE                  = 0x0,
    /** A new transfer got registered. */
    SHCLHTTPSERVERSTATUS_STARTED               = 0x1,
    /** A new transfer got registered. */
    SHCLHTTPSERVERSTATUS_STOPPED               = 0x2,
    /** A new transfer got registered. */
    SHCLHTTPSERVERSTATUS_TRANSFER_REGISTERED   = 0x4,
    /** A transfer got unregistered. */
    SHCLHTTPSERVERSTATUS_TRANSFER_UNREGISTERED = 0x8
} SHCLHTTPSERVERSTATUS;

/**
 * Structure for keeping a Shared Clipboard HTTP server instance.
 */
typedef struct _SHCLHTTPSERVER
{
    /** Critical section for serializing access. */
    RTCRITSECT           CritSect;
    /** Status event for callers to wait for.
     *  Updates \a enmStatus. */
    RTSEMEVENT           StatusEvent;
    /** Initialized indicator. */
    bool                 fInitialized;
    /** Running indicator. */
    bool                 fRunning;
    /** Current status. */
    SHCLHTTPSERVERSTATUS enmStatus;
    /** Handle of the HTTP server instance. */
    RTHTTPSERVER         hHTTPServer;
    /** Port number the HTTP server is running on. 0 if not running. */
    uint16_t             uPort;
    /** List of registered HTTP transfers. */
    RTLISTANCHOR         lstTransfers;
    /** Number of registered HTTP transfers. */
    uint32_t             cTransfers;
    /** Number of files served (via GET) so far.
     *  Only complete downloads count (i.e. no aborted). */
    uint32_t             cDownloaded;
    /** Cached response data. */
    RTHTTPSERVERRESP     Resp;
} SHCLHTTPSERVER;
/** Pointer to Shared Clipboard HTTP server. */
typedef SHCLHTTPSERVER *PSHCLHTTPSERVER;

/**
 * Structure for keeping a Shared Clipboard HTTP context around.
 *
 * This contains the HTTP server instance, among other things.
 */
typedef struct _SHCLHTTPCONTEXT
{
    /** HTTP server instance data. */
    SHCLHTTPSERVER      HttpServer;
} SHCLHTTPCONTEXT;
/** Pointer to Shared Clipboard HTTP transfer context. */
typedef SHCLHTTPCONTEXT *PSHCLHTTPCONTEXT;

#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP */

/**
 * Structure for keeping a single transfer context event.
 */
typedef struct _SHCLTRANSFERCTXEVENT
{
    /** Transfer bound to this event.
     *  Can be NULL if not being used. */
    PSHCLTRANSFER  pTransfer;
    /** Whether a transfer was registered or not. */
    bool           fRegistered;
} SHCLTRANSFERCTXEVENT;
/** Pointer to Shared Clipboard transfer context event. */
typedef SHCLTRANSFERCTXEVENT *PSHCLTRANSFERCTXEVENT;

/**
 * Structure for keeping Shared Clipboard transfer context around.
 *
 * A transfer context contains a list of (grouped) transfers for book keeping.
 */
typedef struct _SHCLTRANSFERCTX
{
    /** Critical section for serializing access. */
    RTCRITSECT                  CritSect;
    /** Event used for waiting. for transfer context changes. */
    RTSEMEVENT                  ChangedEvent;
    /** Event data for \a ChangedEvent. */
    SHCLTRANSFERCTXEVENT        ChangedEventData;
    /** List of transfers. */
    RTLISTANCHOR                List;
    /** Transfer ID allocation bitmap; clear bits are free, set bits are busy. */
    uint64_t                    bmTransferIds[VBOX_SHCL_MAX_TRANSFERS / sizeof(uint64_t) / 8];
    /** Number of running (concurrent) transfers. */
    uint16_t                    cRunning;
    /** Maximum Number of running (concurrent) transfers. */
    uint16_t                    cMaxRunning;
    /** Number of total transfers (in list). */
    uint16_t                    cTransfers;
} SHCLTRANSFERCTX;
/** Pointer to Shared Clipboard transfer context. */
typedef SHCLTRANSFERCTX *PSHCLTRANSFERCTX;

/** @name Shared Clipboard transfer interface providers.
 *  @{
 */
PSHCLTXPROVIDERIFACE ShClTransferProviderLocalQueryInterface(PSHCLTXPROVIDER pProvider);
/** @} */

/** @name Shared Clipboard transfer object API.
 *  @{
 */
int ShClTransferObjCtxInit(PSHCLCLIENTTRANSFEROBJCTX pObjCtx);
void ShClTransferObjCtxDestroy(PSHCLCLIENTTRANSFEROBJCTX pObjCtx);
bool ShClTransferObjCtxIsValid(PSHCLCLIENTTRANSFEROBJCTX pObjCtx);

int ShClTransferObjInit(PSHCLTRANSFEROBJ pObj);
void ShClTransferObjDestroy(PSHCLTRANSFEROBJ pObj);

int ShClTransferObjOpenParmsInit(PSHCLOBJOPENCREATEPARMS pParms);
int ShClTransferObjOpenParmsCopy(PSHCLOBJOPENCREATEPARMS pParmsDst, PSHCLOBJOPENCREATEPARMS pParmsSrc);
void ShClTransferObjOpenParmsDestroy(PSHCLOBJOPENCREATEPARMS pParms);

int ShClTransferObjOpen(PSHCLTRANSFER pTransfer, PSHCLOBJOPENCREATEPARMS pOpenCreateParms, PSHCLOBJHANDLE phObj);
int ShClTransferObjClose(PSHCLTRANSFER pTransfer, SHCLOBJHANDLE hObj);
bool ShClTransferObjIsComplete(PSHCLTRANSFER pTransfer, SHCLOBJHANDLE hObj);
int ShClTransferObjRead(PSHCLTRANSFER pTransfer, SHCLOBJHANDLE hObj, void *pvBuf, uint32_t cbBuf, uint32_t fFlags, uint32_t *pcbRead);
int ShClTransferObjWrite(PSHCLTRANSFER pTransfer, SHCLOBJHANDLE hObj, void *pvBuf, uint32_t cbBuf, uint32_t fFlags, uint32_t *pcbWritten);
PSHCLTRANSFEROBJ ShClTransferObjGet(PSHCLTRANSFER pTransfer, SHCLOBJHANDLE hObj);

PSHCLOBJDATACHUNK ShClTransferObjDataChunkDup(PSHCLOBJDATACHUNK pDataChunk);
void ShClTransferObjDataChunkDestroy(PSHCLOBJDATACHUNK pDataChunk);
void ShClTransferObjDataChunkFree(PSHCLOBJDATACHUNK pDataChunk);
/** @} */

/** @name Shared Clipboard transfer API.
 *  @{
 */
int ShClTransferCreateEx(SHCLTRANSFERDIR enmDir, SHCLSOURCE enmSource, uint32_t cbMaxChunkSize, uint32_t cMaxListHandles, uint32_t cMaxObjHandles, PSHCLTRANSFER *ppTransfer);
int ShClTransferCreate(SHCLTRANSFERDIR enmDir, SHCLSOURCE enmSource, PSHCLTRANSFERCALLBACKS pCallbacks, PSHCLTRANSFER *ppTransfer);
int ShClTransferInit(PSHCLTRANSFER pTransfer);
int ShClTransferDestroy(PSHCLTRANSFER pTransfer);
void ShClTransferReset(PSHCLTRANSFER pTransfer);

bool ShClTransferIsRunning(PSHCLTRANSFER pTransfer);
bool ShClTransferIsComplete(PSHCLTRANSFER pTransfer);
bool ShClTransferIsAborted(PSHCLTRANSFER pTransfer);

int ShClTransferRun(PSHCLTRANSFER pTransfer, PFNSHCLTRANSFERTHREAD pfnThreadFunc, void *pvUser);
int ShClTransferStart(PSHCLTRANSFER pTransfer);
int ShClTransferStop(PSHCLTRANSFER pTransfer);
int ShClTransferComplete(PSHCLTRANSFER pTransfer);
int ShClTransferCancel(PSHCLTRANSFER pTransfer);
int ShClTransferKill(PSHCLTRANSFER pTransfer);
int ShClTransferError(PSHCLTRANSFER pTransfer, int rc);

uint32_t ShClTransferAcquire(PSHCLTRANSFER pTransfer);
uint32_t ShClTransferRelease(PSHCLTRANSFER pTransfer);

SHCLTRANSFERID ShClTransferGetID(PSHCLTRANSFER pTransfer);
SHCLTRANSFERDIR ShClTransferGetDir(PSHCLTRANSFER pTransfer);
int ShClTransferGetRootPathAbs(PSHCLTRANSFER pTransfer, char *pszPath, size_t cbPath);
SHCLSOURCE ShClTransferGetSource(PSHCLTRANSFER pTransfer);
SHCLTRANSFERSTATUS ShClTransferGetStatus(PSHCLTRANSFER pTransfer);
int ShClTransferWaitForStatus(PSHCLTRANSFER pTransfer, RTMSINTERVAL msTimeout, SHCLTRANSFERSTATUS enmStatus);
int ShClTransferWaitForStatusChange(PSHCLTRANSFER pTransfer, RTMSINTERVAL msTimeout, SHCLTRANSFERSTATUS *penmStatus);

int ShClTransferListOpen(PSHCLTRANSFER pTransfer, PSHCLLISTOPENPARMS pOpenParms, PSHCLLISTHANDLE phList);
int ShClTransferListClose(PSHCLTRANSFER pTransfer, SHCLLISTHANDLE hList);
int ShClTransferListGetHeader(PSHCLTRANSFER pTransfer, SHCLLISTHANDLE hList, PSHCLLISTHDR pHdr);
PSHCLLISTHANDLEINFO ShClTransferListGetByHandle(PSHCLTRANSFER pTransfer, SHCLLISTHANDLE hList);
PSHCLTRANSFEROBJ ShClTransferListGetObj(PSHCLTRANSFER pTransfer, SHCLLISTHANDLE hList, uint64_t uIdx);
int ShClTransferListRead(PSHCLTRANSFER pTransfer, SHCLLISTHANDLE hList, PSHCLLISTENTRY pEntry);
int ShClTransferListWrite(PSHCLTRANSFER pTransfer, SHCLLISTHANDLE hList, PSHCLLISTENTRY pEntry);
bool ShClTransferListHandleIsValid(PSHCLTRANSFER pTransfer, SHCLLISTHANDLE hList);

PSHCLLIST ShClTransferListAlloc(void);
void ShClTransferListFree(PSHCLLIST pList);
void ShClTransferListInit(PSHCLLIST pList);
void ShClTransferListDestroy(PSHCLLIST pList);
int ShClTransferListAddEntry(PSHCLLIST pList, PSHCLLISTENTRY pEntry, bool fAppend);

int ShClTransferListHandleInfoInit(PSHCLLISTHANDLEINFO pInfo);
void ShClTransferListHandleInfoDestroy(PSHCLLISTHANDLEINFO pInfo);

int ShClTransferListHdrAlloc(PSHCLLISTHDR *ppListHdr);
void ShClTransferListHdrFree(PSHCLLISTHDR pListHdr);
PSHCLLISTHDR ShClTransferListHdrDup(PSHCLLISTHDR pListHdr);
int ShClTransferListHdrInit(PSHCLLISTHDR pListHdr);
void ShClTransferListHdrDestroy(PSHCLLISTHDR pListHdr);
void ShClTransferListHdrReset(PSHCLLISTHDR pListHdr);
bool ShClTransferListHdrIsValid(PSHCLLISTHDR pListHdr);

int ShClTransferListOpenParmsCopy(PSHCLLISTOPENPARMS pDst, PSHCLLISTOPENPARMS pSrc);
PSHCLLISTOPENPARMS ShClTransferListOpenParmsDup(PSHCLLISTOPENPARMS pParms);
int ShClTransferListOpenParmsInit(PSHCLLISTOPENPARMS pParms);
void ShClTransferListOpenParmsDestroy(PSHCLLISTOPENPARMS pParms);

int ShClTransferListEntryAlloc(PSHCLLISTENTRY *ppListEntry);
void ShClTransferListEntryFree(PSHCLLISTENTRY pListEntry);
int ShClTransferListEntryInitEx(PSHCLLISTENTRY pListEntry, uint32_t fInfo, const char *pszName, void *pvInfo, uint32_t cbInfo);
int ShClTransferListEntryInit(PSHCLLISTENTRY pListEntry);
void ShClTransferListEntryDestroy(PSHCLLISTENTRY pListEntry);
bool ShClTransferListEntryIsValid(PSHCLLISTENTRY pListEntry);
int ShClTransferListEntryCopy(PSHCLLISTENTRY pDst, PSHCLLISTENTRY pSrc);
PSHCLLISTENTRY ShClTransferListEntryDup(PSHCLLISTENTRY pListEntry);

int ShClTransferSetProvider(PSHCLTRANSFER pTransfer, PSHCLTXPROVIDER pProvider);

int ShClTransferRootsSetFromStringListEx(PSHCLTRANSFER pTransfer, const char *pszRoots, size_t cbRoots, const char *pszSep);
int ShClTransferRootsSetFromStringList(PSHCLTRANSFER pTransfer, const char *pszRoots, size_t cbRoots);
int ShClTransferRootsSetFromStringListUnicode(PSHCLTRANSFER pTransfer, PRTUTF16 pwszRoots, size_t cbRoots);
int ShClTransferRootsSetFromPath(PSHCLTRANSFER pTransfer, const char *pszPath);
uint64_t ShClTransferRootsCount(PSHCLTRANSFER pTransfer);
PCSHCLLISTENTRY ShClTransferRootsEntryGet(PSHCLTRANSFER pTransfer, uint64_t uIndex);
int ShClTransferRootListRead(PSHCLTRANSFER pTransfer);
/** @} */

/** @name Shared Clipboard transfer context API.
 *  @{
 */
int ShClTransferCtxInit(PSHCLTRANSFERCTX pTransferCtx);
void ShClTransferCtxDestroy(PSHCLTRANSFERCTX pTransferCtx);
void ShClTransferCtxReset(PSHCLTRANSFERCTX pTransferCtx);
PSHCLTRANSFER ShClTransferCtxGetTransferById(PSHCLTRANSFERCTX pTransferCtx, uint32_t uID);
PSHCLTRANSFER ShClTransferCtxGetTransferByIndex(PSHCLTRANSFERCTX pTransferCtx, uint32_t uIdx);
PSHCLTRANSFER ShClTransferCtxGetTransferLast(PSHCLTRANSFERCTX pTransferCtx);
uint32_t ShClTransferCtxGetRunningTransfers(PSHCLTRANSFERCTX pTransferCtx);
uint32_t ShClTransferCtxGetTotalTransfers(PSHCLTRANSFERCTX pTransferCtx);
void ShClTransferCtxCleanup(PSHCLTRANSFERCTX pTransferCtx);
bool ShClTransferCtxIsMaximumReached(PSHCLTRANSFERCTX pTransferCtx);
int ShClTransferCtxCreateId(PSHCLTRANSFERCTX pTransferCtx, PSHCLTRANSFERID pidTransfer);
int ShClTransferCtxRegister(PSHCLTRANSFERCTX pTransferCtx, PSHCLTRANSFER pTransfer, PSHCLTRANSFERID pidTransfer);
int ShClTransferCtxRegisterById(PSHCLTRANSFERCTX pTransferCtx, PSHCLTRANSFER pTransfer, SHCLTRANSFERID idTransfer);
int ShClTransferCtxUnregisterById(PSHCLTRANSFERCTX pTransferCtx, SHCLTRANSFERID idTransfer);
int ShClTransferCtxWait(PSHCLTRANSFERCTX pTransferCtx, RTMSINTERVAL msTimeout, bool fRegister, SHCLTRANSFERID idTransfer, PSHCLTRANSFER *ppTransfer);
/** @} */

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
/** Namespace used as a prefix for HTTP(S) transfer URLs. */
#define SHCL_HTTPT_URL_NAMESPACE "vbcl"

/** @name Shared Clipboard HTTP context API.
 *  @{
 */
int ShClTransferHttpServerMaybeStart(PSHCLHTTPCONTEXT pCtx);
int ShClTransferHttpServerMaybeStop(PSHCLHTTPCONTEXT pCtx);
/** @} */

/** @name Shared Clipboard HTTP server API.
 *  @{
 */
int ShClTransferHttpServerInit(PSHCLHTTPSERVER pSrv);
int ShClTransferHttpServerDestroy(PSHCLHTTPSERVER pSrv);
int ShClTransferHttpServerStart(PSHCLHTTPSERVER pSrv, unsigned cMaxAttempts, uint16_t *puPort);
int ShClTransferHttpServerStartEx(PSHCLHTTPSERVER pSrv, uint16_t uPort);
int ShClTransferHttpServerStop(PSHCLHTTPSERVER pSrv);
int ShClTransferHttpServerRegisterTransfer(PSHCLHTTPSERVER pSrv, PSHCLTRANSFER pTransfer);
int ShClTransferHttpServerUnregisterTransfer(PSHCLHTTPSERVER pSrv, PSHCLTRANSFER pTransfer);
PSHCLTRANSFER ShClTransferHttpServerGetTransferFirst(PSHCLHTTPSERVER pSrv);
PSHCLTRANSFER ShClTransferHttpServerGetTransferLast(PSHCLHTTPSERVER pSrv);
bool ShClTransferHttpServerGetTransfer(PSHCLHTTPSERVER pSrv, SHCLTRANSFERID idTransfer);
uint16_t ShClTransferHttpServerGetPort(PSHCLHTTPSERVER pSrv);
uint32_t ShClTransferHttpServerGetTransferCount(PSHCLHTTPSERVER pSrv);
char *ShClTransferHttpServerGetAddressA(PSHCLHTTPSERVER pSrv);
char *ShClTransferHttpServerGetUrlA(PSHCLHTTPSERVER pSrv, SHCLTRANSFERID idTransfer, uint64_t idxEntry);
int ShClTransferHttpConvertToStringList(PSHCLHTTPSERVER pSrv, PSHCLTRANSFER pTransfer, char **ppszData, size_t *pcbData);
bool ShClTransferHttpServerIsInitialized(PSHCLHTTPSERVER pSrv);
bool ShClTransferHttpServerIsRunning(PSHCLHTTPSERVER pSrv);
int ShClTransferHttpServerWaitForStatusChange(PSHCLHTTPSERVER pSrv, SHCLHTTPSERVERSTATUS fStatus, RTMSINTERVAL msTimeout);
/** @} */
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP */

/** @name Shared Clipboard transfers utility functions.
 *  @{
 */
int ShClPathSanitizeFilename(char *pszPath, size_t cbPath);
int ShClPathSanitize(char *pszPath, size_t cbPath);
const char *ShClTransferStatusToStr(SHCLTRANSFERSTATUS enmStatus);
int ShClTransferTransformPath(char *pszPath, size_t cbPath);
int ShClTransferValidatePath(const char *pcszPath, bool fMustExist);
int ShClTransferResolvePathAbs(PSHCLTRANSFER pTransfer, const char *pszPath, uint32_t fFlags, char **ppszResolved);
int ShClTransferConvertFileCreateFlags(uint32_t fShClFlags, uint64_t *pfOpen);
int ShClFsObjInfoQueryLocal(const char *pszPath, PSHCLFSOBJINFO pObjInfo);
int ShClFsObjInfoFromIPRT(PSHCLFSOBJINFO pDst, PCRTFSOBJINFO pSrc);
/** @} */

/** @name Shared Clipboard MIME functions.
 *  @{
 */
bool ShClMIMEHasFileURLs(const char *pcszFormat, size_t cchFormatMax);
bool ShClMIMENeedsCache(const char *pcszFormat, size_t cchFormatMax);
/** @} */

#endif /* !VBOX_INCLUDED_GuestHost_SharedClipboard_transfers_h */
