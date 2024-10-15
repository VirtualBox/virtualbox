/* $Id: DragAndDrop.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DnD - Shared functions between host and guest.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_GuestHost_DragAndDrop_h
#define VBOX_INCLUDED_GuestHost_DragAndDrop_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/assert.h>
#include <iprt/fs.h>
#include <iprt/list.h>

#include <VBox/GuestHost/DragAndDropDefs.h>

/** DnDURIDroppedFiles flags. */
typedef uint32_t DNDURIDROPPEDFILEFLAGS;

/** No flags specified. */
#define DNDURIDROPPEDFILE_FLAGS_NONE                   0

/**
 * Structure for keeping a DnD dropped files entry.
 */
typedef struct DNDDROPPEDFILESENTRY
{
    RTLISTNODE  Node;
    char       *pszPath;
} DNDDROPPEDFILESENTRY;
/** Pointer to a DnD dropped files entry. */
typedef DNDDROPPEDFILESENTRY *PDNDDROPPEDFILESENTRY;

/**
 * Structure for maintaining a "dropped files" directory
 * on the host or guest. This will contain all received files & directories
 * for a single drag and drop operation.
 *
 * In case of a failed drag and drop operation this can also
 * perform a gentle rollback if required.
 */
typedef struct DNDDROPPEDFILES
{
    /** Open flags. */
    uint32_t     m_fOpen;
    /** Directory handle for drop directory. */
    RTDIR        m_hDir;
    /** Absolute path to drop directory. */
    char        *pszPathAbs;
    /** List for holding created directories in the case of a rollback. */
    RTLISTANCHOR m_lstDirs;
    /** List for holding created files in the case of a rollback. */
    RTLISTANCHOR m_lstFiles;
} DNDDROPPEDFILES;
/** Pointer to a DnD dropped files directory. */
typedef DNDDROPPEDFILES *PDNDDROPPEDFILES;

int DnDDroppedFilesInit(PDNDDROPPEDFILES pDF);
int DnDDroppedFilesInitEx(PDNDDROPPEDFILES pDF, const char *pszPath, DNDURIDROPPEDFILEFLAGS fFlags);
void DnDDroppedFilesDestroy(PDNDDROPPEDFILES pDF);
int DnDDroppedFilesAddFile(PDNDDROPPEDFILES pDF, const char *pszFile);
int DnDDroppedFilesAddDir(PDNDDROPPEDFILES pDF, const char *pszDir);
int DnDDroppedFilesClose(PDNDDROPPEDFILES pDF);
bool DnDDroppedFilesIsOpen(PDNDDROPPEDFILES pDF);
int DnDDroppedFilesOpenEx(PDNDDROPPEDFILES pDF, const char *pszPath, DNDURIDROPPEDFILEFLAGS fFlags);
int DnDDroppedFilesOpenTemp(PDNDDROPPEDFILES pDF, DNDURIDROPPEDFILEFLAGS fFlags);
const char *DnDDroppedFilesGetDirAbs(PDNDDROPPEDFILES pDF);
int DnDDroppedFilesReopen(PDNDDROPPEDFILES pDF);
int DnDDroppedFilesReset(PDNDDROPPEDFILES pDF, bool fDelete);
int DnDDroppedFilesRollback(PDNDDROPPEDFILES pDF);

const char *DnDHostMsgToStr(uint32_t uMsg);
const char *DnDGuestMsgToStr(uint32_t uMsg);
const char *DnDActionToStr(VBOXDNDACTION uAction);
      char *DnDActionListToStrA(VBOXDNDACTIONLIST fActionList);
const char *DnDStateToStr(VBOXDNDSTATE enmState);

bool DnDMIMEHasFileURLs(const char *pcszFormat, size_t cchFormatMax);
bool DnDMIMENeedsDropDir(const char *pcszFormat, size_t cchFormatMax);

int DnDPathValidate(const char *pcszPath, bool fMustExist);

/** DnD path conversion flags. */
typedef uint32_t DNDPATHCONVERTFLAGS;

/** No flags specified.
 *  This will convert the path to the universal tansport style. */
#define DNDPATHCONVERT_FLAGS_TRANSPORT            0
/** Converts the path to a OS-dependent path. */
#define DNDPATHCONVERT_FLAGS_TO_DOS               RT_BIT(0)

/** Mask of all valid DnD path conversion flags. */
#define DNDPATHCONVERT_FLAGS_VALID_MASK           UINT32_C(0x1)

int DnDPathConvert(char *pszPath, size_t cbPath, DNDPATHCONVERTFLAGS fFlags);
int DnDPathSanitizeFileName(char *pszPath, size_t cbPath);
int DnDPathRebase(const char *pcszPathAbs, const char *pcszBaseOld, const char *pcszBaseNew, char **ppszPath);

/** DnDTransferObject flags. */
typedef uint32_t DNDTRANSFEROBJECTFLAGS;

/** No flags specified. */
#define DNDTRANSFEROBJECT_FLAGS_NONE                   0

/** Mask of all valid DnD transfer object flags. */
#define DNDTRANSFEROBJECT_FLAGS_VALID_MASK             UINT32_C(0x0)

/**
 * Enumeration for specifying a transfer object type.
 */
typedef enum DNDTRANSFEROBJTYPE
{
    /** Unknown type, do not use. */
    DNDTRANSFEROBJTYPE_UNKNOWN = 0,
    /** Object is a file. */
    DNDTRANSFEROBJTYPE_FILE,
    /** Object is a directory. */
    DNDTRANSFEROBJTYPE_DIRECTORY,
    /** The usual 32-bit hack. */
    DNDTRANSFEROBJTYPE_32BIT_HACK = 0x7fffffff
} DNDTRANSFEROBJTYPE;

/**
 * Enumeration for specifying a path style.
 */
typedef enum DNDTRANSFEROBJPATHSTYLE
{
    /** Transport style (UNIX-y), the default. */
    DNDTRANSFEROBJPATHSTYLE_TRANSPORT = 0,
    /** DOS style, containing back slashes. */
    DNDTRANSFEROBJPATHSTYLE_DOS,
    /** The usual 32-bit hack. */
    DNDTRANSFEROBJPATHSTYLE_32BIT_HACK = 0x7fffffff
} DNDTRANSFEROBJPATHSTYLE;

/**
 * Structure for keeping a DnD transfer object.
 */
typedef struct DNDTRANSFEROBJECT
{
    RTLISTNODE         Node;
    /** The object's type. */
    DNDTRANSFEROBJTYPE enmType;
    /** Index (in characters, UTF-8) at which the first destination segment starts. */
    uint16_t           idxDst;
    /** Allocated path. Includdes the absolute source path (if any) + destination segments.
     *  Transport (IPRT) style. */
    char              *pszPath;

    /** Union containing data depending on the object's type. */
    union
    {
        /** Structure containing members for objects that
         *  are files. */
        struct
        {
            /** File handle. */
            RTFILE      hFile;
            /** File system object information of this file. */
            RTFSOBJINFO objInfo;
            /** Bytes to proces for reading/writing. */
            uint64_t    cbToProcess;
            /** Bytes processed reading/writing. */
            uint64_t    cbProcessed;
        } File;
        struct
        {
            /** Directory handle. */
            RTDIR       hDir;
            /** File system object information of this directory. */
            RTFSOBJINFO objInfo;
        } Dir;
    } u;
} DNDTRANSFEROBJECT;
/** Pointer to a DnD transfer object. */
typedef DNDTRANSFEROBJECT *PDNDTRANSFEROBJECT;

int DnDTransferObjectInit(PDNDTRANSFEROBJECT pObj);
int DnDTransferObjectInitEx(PDNDTRANSFEROBJECT pObj, DNDTRANSFEROBJTYPE enmType, const char *pcszPathSrcAbs, const char *pcszPathDst);
void DnDTransferObjectDestroy(PDNDTRANSFEROBJECT pObj);
int DnDTransferObjectClose(PDNDTRANSFEROBJECT pObj);
void DnDTransferObjectReset(PDNDTRANSFEROBJECT pObj);
const char *DnDTransferObjectGetSourcePath(PDNDTRANSFEROBJECT pObj);
const char *DnDTransferObjectGetDestPath(PDNDTRANSFEROBJECT pObj);
int DnDTransferObjectGetDestPathEx(PDNDTRANSFEROBJECT pObj, DNDTRANSFEROBJPATHSTYLE enmStyle, char *pszBuf, size_t cbBuf);
RTFMODE DnDTransferObjectGetMode(PDNDTRANSFEROBJECT pObj);
uint64_t DnDTransferObjectGetProcessed(PDNDTRANSFEROBJECT pObj);
uint64_t DnDTransferObjectGetSize(PDNDTRANSFEROBJECT pObj);
DNDTRANSFEROBJTYPE DnDTransferObjectGetType(PDNDTRANSFEROBJECT pObj);
int DnDTransferObjectSetSize(PDNDTRANSFEROBJECT pObj, uint64_t cbSize);
bool DnDTransferObjectIsComplete(PDNDTRANSFEROBJECT pObj);
bool DnDTransferObjectIsOpen(PDNDTRANSFEROBJECT pObj);
int DnDTransferObjectOpen(PDNDTRANSFEROBJECT pObj, uint64_t fOpen, RTFMODE fMode, DNDTRANSFEROBJECTFLAGS fFlags);
int DnDTransferObjectQueryInfo(PDNDTRANSFEROBJECT pObj);
int DnDTransferObjectRead(PDNDTRANSFEROBJECT pObj, void *pvBuf, size_t cbBuf, uint32_t *pcbRead);
int DnDTransferObjectWrite(PDNDTRANSFEROBJECT pObj, const void *pvBuf, size_t cbBuf, uint32_t *pcbWritten);

/** Defines the default chunk size of DnD data transfers.
 *  Supported on all (older) Guest Additions which also support DnD. */
#define DND_DEFAULT_CHUNK_SIZE                       _64K

/** Separator for a formats list. */
#define DND_FORMATS_SEPARATOR_STR                    "\r\n"

/** Default URI list path separator, if not specified otherwise.
 *
 *  This is there for hysterical raisins, to not break older Guest Additions.
 ** @todo Get rid of this.  */
#define DND_PATH_SEPARATOR_STR                       "\r\n"

/** DnDTransferList flags. */
typedef uint32_t DNDTRANSFERLISTFLAGS;

/** No flags specified. */
#define DNDTRANSFERLIST_FLAGS_NONE                   0
/** Enables recurisve directory handling. */
#define DNDTRANSFERLIST_FLAGS_RECURSIVE              RT_BIT(0)
/** Resolve all symlinks. Currently not supported and will be ignored. */
#define DNDTRANSFERLIST_FLAGS_RESOLVE_SYMLINKS       RT_BIT(1)
/** Keep the files + directory entries open while
 *  being in this list. */
#define DNDTRANSFERLIST_FLAGS_KEEP_OPEN              RT_BIT(2)
/** Lazy loading: Only enumerate sub directories when needed. Not implemented yet.
 ** @todo Implement lazy loading.  */
#define DNDTRANSFERLIST_FLAGS_LAZY                   RT_BIT(3)

/** Mask of all valid DnD transfer list flags. */
#define DNDTRANSFERLIST_FLAGS_VALID_MASK             UINT32_C(0xF)

/**
 * Enumeration for specifying a transfer list format.
 */
typedef enum DNDTRANSFERLISTFMT
{
    /** Unknown format, do not use. */
    DNDTRANSFERLISTFMT_UNKNOWN = 0,
    /** Native format. */
    DNDTRANSFERLISTFMT_NATIVE,
    /** URI format. */
    DNDTRANSFERLISTFMT_URI,
    /** The usual 32-bit hack. */
    DNDTRANSFERLISTFMT_32BIT_HACK = 0x7fffffff
} DNDTRANSFERLISTFMT;

/**
 * Structure for keeping a DnD transfer list root entry.
 *
 * A root entry always is relative to the parent list maintaining it.
 */
typedef struct DNDTRANSFERLISTROOT
{
    /** List node. */
    RTLISTNODE Node;
    /** Pointer to the allocated root path.
     *  - Relative to the list's root path
     *  - Always ends with a trailing slash
     *  - Always stored in transport style (UNIX-y). */
    char      *pszPathRoot;
} DNDTRANSFERLISTROOT;
/** Pointer to a DnD list root entry. */
typedef DNDTRANSFERLISTROOT *PDNDTRANSFERLISTROOT;

/**
 * Struct for keeping a DnD transfer list.
 *
 * All entries must share a common (absolute) root path. For different root paths another transfer list is needed.
 */
typedef struct DNDTRANSFERLIST
{
    /** Absolute root path of this transfer list, in native path style.
     *  Always ends with a separator. */
    char                   *pszPathRootAbs;
    /** List of all relative (to \a pszPathRootAbs) top-level file/directory entries, of type DNDTRANSFERLISTROOT.
     *  Note: All paths are stored internally in transport style (UNIX paths) for
     *        easier conversion/handling!  */
    RTLISTANCHOR            lstRoot;
    /** Total number of all transfer root entries. */
    uint64_t                cRoots;
    /** List of all transfer objects added, of type DNDTRANSFEROBJECT.
     *
     *  The order of objects being added is crucial for traversing the tree.
     *  In other words, sub directories must come first before its contents. */
    RTLISTANCHOR            lstObj;
    /** Total number of all transfer objects. */
    uint64_t                cObj;
    /** Total size of all transfer objects, that is, the file
     *  size of all objects (in bytes).
     *  Note: Do *not* size_t here, as we also want to support large files
     *        on 32-bit guests. */
    uint64_t                cbObjTotal;
} DNDTRANSFERLIST;
/** Pointer to a DNDTRANSFERLIST struct. */
typedef DNDTRANSFERLIST *PDNDTRANSFERLIST;

int DnDTransferListInit(PDNDTRANSFERLIST pList);
int DnDTransferListInitEx(PDNDTRANSFERLIST pList, const char *pcszRootPathAbs, DNDTRANSFERLISTFMT enmFmt);
void DnDTransferListDestroy(PDNDTRANSFERLIST pList);
void DnDTransferListReset(PDNDTRANSFERLIST pList);

int DnDTransferListAppendPath(PDNDTRANSFERLIST pList, DNDTRANSFERLISTFMT enmFmt, const char *pszPath, DNDTRANSFERLISTFLAGS fFlags);
int DnDTransferListAppendPathsFromBuffer(PDNDTRANSFERLIST pList, DNDTRANSFERLISTFMT enmFmt, const char *pszPaths, size_t cbPaths, const char *pcszSeparator, DNDTRANSFERLISTFLAGS fFlags);
int DnDTransferListAppendPathsFromArray(PDNDTRANSFERLIST pList, DNDTRANSFERLISTFMT enmFmt, const char * const *papcszPaths, size_t cPaths, DNDTRANSFERLISTFLAGS fFlags);
int DnDTransferListAppendRootsFromBuffer(PDNDTRANSFERLIST pList, DNDTRANSFERLISTFMT enmFmt, const char *pszPaths, size_t cbPaths, const char *pcszSeparator, DNDTRANSFERLISTFLAGS fFlags);
int DnDTransferListAppendRootsFromArray(PDNDTRANSFERLIST pList, DNDTRANSFERLISTFMT enmFmt, const char * const *papcszPaths, size_t cPaths, DNDTRANSFERLISTFLAGS fFlags);

int DnDTransferListGetRootsEx(PDNDTRANSFERLIST pList, DNDTRANSFERLISTFMT enmFmt, const char *pcszPathBase, const char *pcszSeparator, char **ppszBuffer, size_t *pcbBuffer);
int DnDTransferListGetRoots(PDNDTRANSFERLIST pList, DNDTRANSFERLISTFMT enmFmt, char **ppszBuffer, size_t *pcbBuffer);
uint64_t DnDTransferListGetRootCount(PDNDTRANSFERLIST pList);
const char *DnDTransferListGetRootPathAbs(PDNDTRANSFERLIST pList);

PDNDTRANSFEROBJECT DnDTransferListObjGetFirst(PDNDTRANSFERLIST pList);
void DnDTransferListObjRemove(PDNDTRANSFERLIST pList, PDNDTRANSFEROBJECT pObj);
void DnDTransferListObjRemoveFirst(PDNDTRANSFERLIST pList);
uint64_t DnDTransferListObjCount(PDNDTRANSFERLIST pList);
uint64_t DnDTransferListObjTotalBytes(PDNDTRANSFERLIST pList);

#endif /* !VBOX_INCLUDED_GuestHost_DragAndDrop_h */

