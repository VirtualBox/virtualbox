/** $Id: VBoxSFInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxSF - OS/2 Shared Folder IFS, Internal Header.
 */

/*
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef GA_INCLUDED_SRC_os2_VBoxSF_VBoxSFInternal_h
#define GA_INCLUDED_SRC_os2_VBoxSF_VBoxSFInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


#define INCL_BASE
#define INCL_ERROR
#define INCL_LONGLONG
#define OS2EMX_PLAIN_CHAR
#include <os2ddk/bsekee.h>
#include <os2ddk/devhlp.h>
#include <os2ddk/unikern.h>
#include <os2ddk/fsd.h>
#undef RT_MAX

#include <iprt/types.h>
#include <iprt/assert.h>
#include <iprt/list.h>
#include <VBox/VBoxGuest.h>
#include <VBox/VBoxGuestLibSharedFoldersInline.h>


/** Allocation header used by RTMemAlloc.
 * This should be subtracted from round numbers. */
#define ALLOC_HDR_SIZE  (0x10 + 4)


/**
 * A shared folder
 */
typedef struct VBOXSFFOLDER
{
    /** For the shared folder list. */
    RTLISTNODE          ListEntry;
    /** Magic number (VBOXSFFOLDER_MAGIC). */
    uint32_t            u32Magic;
    /** Number of active references to this folder. */
    uint32_t volatile   cRefs;
    /** Number of open files referencing this folder.   */
    uint32_t volatile   cOpenFiles;
    /** Number of open searches referencing this folder.   */
    uint32_t volatile   cOpenSearches;
    /** Number of drives this is attached to. */
    uint8_t volatile    cDrives;

    /** The host folder handle. */
    SHFLROOT            idHostRoot;

    /** OS/2 volume handle. */
    USHORT              hVpb;

    /** The length of the name and tag, including zero terminators and such. */
    uint16_t            cbNameAndTag;
    /** The length of the folder name. */
    uint8_t             cchName;
    /** The shared folder name.  If there is a tag it follows as a second string. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    char                szName[RT_FLEXIBLE_ARRAY];
} VBOXSFFOLDER;
/** Pointer to a shared folder. */
typedef VBOXSFFOLDER *PVBOXSFFOLDER;
/** Magic value for VBOXSFVP (Neal Town Stephenson). */
#define VBOXSFFOLDER_MAGIC      UINT32_C(0x19591031)

/** The shared mutex protecting folders list, drives and the connection. */
extern MutexLock_t      g_MtxFolders;
/** List of active folder (PVBOXSFFOLDER). */
extern RTLISTANCHOR     g_FolderHead;


/**
 * VBoxSF Volume Parameter Structure.
 *
 * @remarks Overlays the 36 byte VPFSD structure (fsd.h).
 * @note    No self pointer as the kernel may reallocate these.
 */
typedef struct VBOXSFVP
{
    /** Magic value (VBOXSFVP_MAGIC). */
    uint32_t         u32Magic;
    /** The folder. */
    PVBOXSFFOLDER    pFolder;
} VBOXSFVP;
AssertCompile(sizeof(VBOXSFVP) <= sizeof(VPFSD));
/** Pointer to a VBOXSFVP struct. */
typedef VBOXSFVP *PVBOXSFVP;
/** Magic value for VBOXSFVP (Laurence van Cott Niven). */
#define VBOXSFVP_MAGIC          UINT32_C(0x19380430)


/**
 * VBoxSF Current Directory Structure.
 *
 * @remark  Overlays the 8 byte CDFSD structure (fsd.h).
 */
typedef struct VBOXSFCD
{
    uint32_t u32Dummy;
} VBOXSFCD;
AssertCompile(sizeof(VBOXSFCD) <= sizeof(CDFSD));
/** Pointer to a VBOXSFCD struct. */
typedef VBOXSFCD *PVBOXSFCD;


/**
 * VBoxSF System File Structure.
 *
 * @remark  Overlays the 30 byte SFFSD structure (fsd.h).
 */
typedef struct VBOXSFSYFI
{
    /** Magic value (VBOXSFSYFI_MAGIC). */
    uint32_t            u32Magic;
    /** Self pointer for quick 16:16 to flat translation. */
    struct VBOXSFSYFI  *pSelf;
    /** The host file handle. */
    SHFLHANDLE          hHostFile;
    /** The shared folder (referenced). */
    PVBOXSFFOLDER       pFolder;
} VBOXSFSYFI;
AssertCompile(sizeof(VBOXSFSYFI) <= sizeof(SFFSD));
/** Pointer to a VBOXSFSYFI struct. */
typedef VBOXSFSYFI *PVBOXSFSYFI;
/** Magic value for VBOXSFSYFI (Jon Ellis Meacham). */
#define VBOXSFSYFI_MAGIC         UINT32_C(0x19690520)


/**
 * More file search data (on physical heap).
 */
typedef struct VBOXSFFSBUF /**< @todo rename as is no longer buffer. */
{
    /** The request (must be first). */
    VBOXSFLISTDIRREQ    Req;
    /** A magic number (VBOXSFFSBUF_MAGIC). */
    uint32_t            u32Magic;
    /** The filter string (full path), NULL if all files are request. */
    PSHFLSTRING         pFilter;
    /** Size of the buffer for directory entries. */
    uint32_t            cbBuf;
    /** Buffer for directory entries on the physical heap. */
    PSHFLDIRINFO        pBuf;
    /** Must have attributes (shifted down DOS attributes).  */
    uint8_t             fMustHaveAttribs;
    /** Non-matching attributes (shifted down DOS attributes).  */
    uint8_t             fExcludedAttribs;
    /** Set if FF_ATTR_LONG_FILENAME. */
    bool                fLongFilenames : 1;
    uint8_t             bPadding1;
    /** The local time offset to use for this search. */
    int16_t             cMinLocalTimeDelta;
    uint8_t             abPadding2[2];
    /** Number of valid bytes in the buffer. */
    uint32_t            cbValid;
    /** Number of entries left in the buffer.   */
    uint32_t            cEntriesLeft;
    /** The next entry. */
    PSHFLDIRINFO        pEntry;
    //uint32_t            uPadding3;
    /** Staging area for staging a full FILEFINDBUF4L (+ 32 safe bytes). */
    uint8_t             abStaging[RT_ALIGN_32(sizeof(FILEFINDBUF4L) + 32, 8)];
} VBOXSFFSBUF;
/** Pointer to a file search buffer. */
typedef VBOXSFFSBUF *PVBOXSFFSBUF;
/** Magic number for VBOXSFFSBUF (Robert Anson Heinlein). */
#define VBOXSFFSBUF_MAGIC       UINT32_C(0x19070707)


/**
 * VBoxSF File Search Structure.
 *
 * @remark  Overlays the 24 byte FSFSD structure (fsd.h).
 * @note    No self pointer as the kernel may reallocate these.
 */
typedef struct VBOXSFFS
{
    /** Magic value (VBOXSFFS_MAGIC). */
    uint32_t            u32Magic;
    /** The last file position position. */
    uint32_t            offLastFile;
    /** The host directory handle. */
    SHFLHANDLE          hHostDir;
    /** The shared folder (referenced). */
    PVBOXSFFOLDER       pFolder;
    /** Search data buffer. */
    PVBOXSFFSBUF        pBuf;
} VBOXSFFS;
AssertCompile(sizeof(VBOXSFFS) <= sizeof(FSFSD));
/** Pointer to a VBOXSFFS struct. */
typedef VBOXSFFS *PVBOXSFFS;
/** Magic number for VBOXSFFS (Isaak Azimov). */
#define VBOXSFFS_MAGIC          UINT32_C(0x19200102)


extern VBGLSFCLIENT g_SfClient;
extern uint32_t g_fHostFeatures;

void        vboxSfOs2InitFileBuffers(void);
PSHFLSTRING vboxSfOs2StrAlloc(size_t cwcLength);
PSHFLSTRING vboxSfOs2StrDup(PCSHFLSTRING pSrc);
void        vboxSfOs2StrFree(PSHFLSTRING pStr);

APIRET      vboxSfOs2ResolvePath(const char *pszPath, PVBOXSFCD pCdFsd, LONG offCurDirEnd,
                                 PVBOXSFFOLDER *ppFolder, PSHFLSTRING *ppStrFolderPath);
APIRET      vboxSfOs2ResolvePathEx(const char *pszPath, PVBOXSFCD pCdFsd, LONG offCurDirEnd, uint32_t offStrInBuf,
                                   PVBOXSFFOLDER *ppFolder, void **ppvBuf);
void        vboxSfOs2ReleasePathAndFolder(PSHFLSTRING pStrPath, PVBOXSFFOLDER pFolder);
void        vboxSfOs2ReleaseFolder(PVBOXSFFOLDER pFolder);
APIRET      vboxSfOs2ConvertStatusToOs2(int vrc, APIRET rcDefault);
int16_t     vboxSfOs2GetLocalTimeDelta(void);
void        vboxSfOs2DateTimeFromTimeSpec(FDATE *pDosDate, FTIME *pDosTime, RTTIMESPEC SrcTimeSpec, int16_t cMinLocalTimeDelta);
PRTTIMESPEC vboxSfOs2DateTimeToTimeSpec(FDATE DosDate, FTIME DosTime, int16_t cMinLocalTimeDelta, PRTTIMESPEC pDstTimeSpec);
APIRET      vboxSfOs2FileStatusFromObjInfo(PBYTE pbDst, ULONG cbDst, ULONG uLevel, SHFLFSOBJINFO const *pSrc);
APIRET      vboxSfOs2SetInfoCommonWorker(PVBOXSFFOLDER pFolder, SHFLHANDLE hHostFile, ULONG fAttribs,
                                         PFILESTATUS pTimestamps, PSHFLFSOBJINFO pObjInfoBuf, uint32_t offObjInfoInAlloc);

APIRET      vboxSfOs2CheckEaOpForCreation(EAOP const *pEaOp);
APIRET      vboxSfOs2MakeEmptyEaList(PEAOP pEaOp, ULONG uLevel);
APIRET      vboxSfOs2MakeEmptyEaListEx(PEAOP pEaOp, ULONG uLevel, ULONG cbFullEasLeft, uint32_t *pcbWritten, ULONG *poffError);

DECLASM(PVBOXSFVP)  Fsh32GetVolParams(USHORT hVbp, PVPFSI *ppVpFsi /*optional*/);
DECLASM(APIRET)     SafeKernStrToUcs(PUconvObj, UniChar *, char *, LONG, LONG);
DECLASM(APIRET)     SafeKernStrFromUcs(PUconvObj, char *, UniChar *, LONG, LONG);


#endif /* !GA_INCLUDED_SRC_os2_VBoxSF_VBoxSFInternal_h */

