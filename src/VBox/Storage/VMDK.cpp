/* $Id: VMDK.cpp 169035 2025-05-28 14:43:19Z jack.doherty@oracle.com $ */
/** @file
 * VMDK disk image, core code.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_VD_VMDK
#include <VBox/log.h>           /* before VBox/vd-ifs.h */
#include <VBox/vd-plugin.h>
#include <VBox/err.h>

#include <iprt/assert.h>
#include <iprt/alloc.h>
#include <iprt/base64.h>
#include <iprt/ctype.h>
#include <iprt/crc.h>
#include <iprt/dvm.h>
#include <iprt/uuid.h>
#include <iprt/path.h>
#include <iprt/rand.h>
#include <iprt/sg.h>
#include <iprt/sort.h>
#include <iprt/string.h>
#include <iprt/zip.h>
#include <iprt/asm.h>
#include <iprt/zero.h>
#ifdef RT_OS_WINDOWS
# include <iprt/utf16.h>
# include <iprt/uni.h>
# include <iprt/uni.h>
# include <iprt/nt/nt-and-windows.h>
# include <winioctl.h>
#endif
#ifdef RT_OS_LINUX
# include <errno.h>
# include <sys/stat.h>
# include <iprt/dir.h>
# include <iprt/symlink.h>
# include <iprt/linux/sysfs.h>
#endif
#ifdef RT_OS_FREEBSD
#include <libgeom.h>
#include <sys/stat.h>
#include <stdlib.h>
#endif
#ifdef RT_OS_SOLARIS
#include <sys/dkio.h>
#include <sys/vtoc.h>
#include <sys/efi_partition.h>
#include <unistd.h>
#include <errno.h>
#endif
#ifdef RT_OS_DARWIN
# include <sys/stat.h>
# include <sys/disk.h>
# include <errno.h>
/* The following structure and IOCTLs are defined in znu bsd/sys/disk.h but
   inside KERNEL ifdefs and thus stripped from the SDK edition of the header.
   While we could try include the header from the Kernel.framework, it's a lot
   easier to just add the structure and 4 defines here. */
typedef struct
{
    uint64_t offset;
    uint64_t length;
    uint8_t  reserved0128[12];
    dev_t    dev;
} dk_physical_extent_t;
# define DKIOCGETBASE               _IOR( 'd', 73, uint64_t)
# define DKIOCLOCKPHYSICALEXTENTS   _IO(  'd', 81)
# define DKIOCGETPHYSICALEXTENT     _IOWR('d', 82, dk_physical_extent_t)
# define DKIOCUNLOCKPHYSICALEXTENTS _IO(  'd', 83)
#endif /* RT_OS_DARWIN */

#include "VDBackends.h"


/*********************************************************************************************************************************
*   Constants And Macros, Structures and Typedefs                                                                                *
*********************************************************************************************************************************/

/** Maximum encoded string size (including NUL) we allow for VMDK images.
 * Deliberately not set high to avoid running out of descriptor space. */
#define VMDK_ENCODED_COMMENT_MAX 1024

/** VMDK descriptor DDB entry for PCHS cylinders. */
#define VMDK_DDB_GEO_PCHS_CYLINDERS "ddb.geometry.cylinders"

/** VMDK descriptor DDB entry for PCHS heads. */
#define VMDK_DDB_GEO_PCHS_HEADS "ddb.geometry.heads"

/** VMDK descriptor DDB entry for PCHS sectors. */
#define VMDK_DDB_GEO_PCHS_SECTORS "ddb.geometry.sectors"

/** VMDK descriptor DDB entry for LCHS cylinders. */
#define VMDK_DDB_GEO_LCHS_CYLINDERS "ddb.geometry.biosCylinders"

/** VMDK descriptor DDB entry for LCHS heads. */
#define VMDK_DDB_GEO_LCHS_HEADS "ddb.geometry.biosHeads"

/** VMDK descriptor DDB entry for LCHS sectors. */
#define VMDK_DDB_GEO_LCHS_SECTORS "ddb.geometry.biosSectors"

/** VMDK descriptor DDB entry for image UUID. */
#define VMDK_DDB_IMAGE_UUID "ddb.uuid.image"

/** VMDK descriptor DDB entry for image modification UUID. */
#define VMDK_DDB_MODIFICATION_UUID "ddb.uuid.modification"

/** VMDK descriptor DDB entry for parent image UUID. */
#define VMDK_DDB_PARENT_UUID "ddb.uuid.parent"

/** VMDK descriptor DDB entry for parent image modification UUID. */
#define VMDK_DDB_PARENT_MODIFICATION_UUID "ddb.uuid.parentmodification"

/** No compression for streamOptimized files. */
#define VMDK_COMPRESSION_NONE 0

/** Deflate compression for streamOptimized files. */
#define VMDK_COMPRESSION_DEFLATE 1

/** Marker that the actual GD value is stored in the footer. */
#define VMDK_GD_AT_END 0xffffffffffffffffULL

/** Marker for end-of-stream in streamOptimized images. */
#define VMDK_MARKER_EOS 0

/** Marker for grain table block in streamOptimized images. */
#define VMDK_MARKER_GT 1

/** Marker for grain directory block in streamOptimized images. */
#define VMDK_MARKER_GD 2

/** Marker for footer in streamOptimized images. */
#define VMDK_MARKER_FOOTER 3

/** Marker for unknown purpose in streamOptimized images.
 * Shows up in very recent images created by vSphere, but only sporadically.
 * They "forgot" to document that one in the VMDK specification. */
#define VMDK_MARKER_UNSPECIFIED 4

/** Dummy marker for "don't check the marker value". */
#define VMDK_MARKER_IGNORE 0xffffffffU

/**
 * Magic number for hosted images created by VMware Workstation 4, VMware
 * Workstation 5, VMware Server or VMware Player. Not necessarily sparse.
 */
#define VMDK_SPARSE_MAGICNUMBER 0x564d444b /* 'V' 'M' 'D' 'K' */

/** VMDK sector size in bytes. */
#define VMDK_SECTOR_SIZE 512
/** Max string buffer size for uint64_t with null term */
#define UINT64_MAX_BUFF_SIZE 21
/** Grain directory entry size in bytes */
#define VMDK_GRAIN_DIR_ENTRY_SIZE 4
/** Grain table size in bytes */
#define VMDK_GRAIN_TABLE_SIZE 2048

/**
 * VMDK hosted binary extent header. The "Sparse" is a total misnomer, as
 * this header is also used for monolithic flat images.
 */
#pragma pack(1)
typedef struct SparseExtentHeader
{
    uint32_t    magicNumber;
    uint32_t    version;
    uint32_t    flags;
    uint64_t    capacity;
    uint64_t    grainSize;
    uint64_t    descriptorOffset;
    uint64_t    descriptorSize;
    uint32_t    numGTEsPerGT;
    uint64_t    rgdOffset;
    uint64_t    gdOffset;
    uint64_t    overHead;
    bool        uncleanShutdown;
    char        singleEndLineChar;
    char        nonEndLineChar;
    char        doubleEndLineChar1;
    char        doubleEndLineChar2;
    uint16_t    compressAlgorithm;
    uint8_t     pad[433];
} SparseExtentHeader;
#pragma pack()

/** The maximum allowed descriptor size in the extent header in sectors. */
#define VMDK_SPARSE_DESCRIPTOR_SIZE_MAX UINT64_C(20480) /* 10MB */

/** VMDK capacity for a single chunk when 2G splitting is turned on. Should be
 * divisible by the default grain size (64K) */
#define VMDK_2G_SPLIT_SIZE (2047 * 1024 * 1024)

/** VMDK streamOptimized file format marker. The type field may or may not
 * be actually valid, but there's always data to read there. */
#pragma pack(1)
typedef struct VMDKMARKER
{
    uint64_t uSector;
    uint32_t cbSize;
    uint32_t uType;
} VMDKMARKER, *PVMDKMARKER;
#pragma pack()


/** Convert sector number/size to byte offset/size. */
#define VMDK_SECTOR2BYTE(u) ((uint64_t)(u) << 9)

/** Convert byte offset/size to sector number/size. */
#define VMDK_BYTE2SECTOR(u) ((u) >> 9)

/**
 * VMDK extent type.
 */
typedef enum VMDKETYPE
{
    /** Hosted sparse extent. */
    VMDKETYPE_HOSTED_SPARSE = 1,
    /** Flat extent. */
    VMDKETYPE_FLAT,
    /** Zero extent. */
    VMDKETYPE_ZERO,
    /** VMFS extent, used by ESX. */
    VMDKETYPE_VMFS
} VMDKETYPE, *PVMDKETYPE;

/**
 * VMDK access type for a extent.
 */
typedef enum VMDKACCESS
{
    /** No access allowed. */
    VMDKACCESS_NOACCESS = 0,
    /** Read-only access. */
    VMDKACCESS_READONLY,
    /** Read-write access. */
    VMDKACCESS_READWRITE
} VMDKACCESS, *PVMDKACCESS;

/** Forward declaration for PVMDKIMAGE. */
typedef struct VMDKIMAGE *PVMDKIMAGE;

/**
 * Extents files entry. Used for opening a particular file only once.
 */
typedef struct VMDKFILE
{
    /** Pointer to file path. Local copy. */
    const char      *pszFilename;
    /** Pointer to base name. Local copy. */
    const char      *pszBasename;
    /** File open flags for consistency checking. */
    unsigned         fOpen;
    /** Handle for sync/async file abstraction.*/
    PVDIOSTORAGE     pStorage;
    /** Reference counter. */
    unsigned         uReferences;
    /** Flag whether the file should be deleted on last close. */
    bool             fDelete;
    /** Pointer to the image we belong to (for debugging purposes). */
    PVMDKIMAGE       pImage;
    /** Pointer to next file descriptor. */
    struct VMDKFILE *pNext;
    /** Pointer to the previous file descriptor. */
    struct VMDKFILE *pPrev;
} VMDKFILE, *PVMDKFILE;

/**
 * VMDK extent data structure.
 */
typedef struct VMDKEXTENT
{
    /** File handle. */
    PVMDKFILE    pFile;
    /** Base name of the image extent. */
    const char  *pszBasename;
    /** Full name of the image extent. */
    const char  *pszFullname;
    /** Number of sectors in this extent. */
    uint64_t    cSectors;
    /** Number of sectors per block (grain in VMDK speak). */
    uint64_t    cSectorsPerGrain;
    /** Starting sector number of descriptor. */
    uint64_t    uDescriptorSector;
    /** Size of descriptor in sectors. */
    uint64_t    cDescriptorSectors;
    /** Starting sector number of grain directory. */
    uint64_t    uSectorGD;
    /** Starting sector number of redundant grain directory. */
    uint64_t    uSectorRGD;
    /** Total number of metadata sectors. */
    uint64_t    cOverheadSectors;
    /** Nominal size (i.e. as described by the descriptor) of this extent. */
    uint64_t    cNominalSectors;
    /** Sector offset (i.e. as described by the descriptor) of this extent. */
    uint64_t    uSectorOffset;
    /** Number of entries in a grain table. */
    uint32_t    cGTEntries;
    /** Number of sectors reachable via a grain directory entry. */
    uint32_t    cSectorsPerGDE;
    /** Number of entries in the grain directory. */
    uint32_t    cGDEntries;
    /** Pointer to the next free sector. Legacy information. Do not use. */
    uint32_t    uFreeSector;
    /** Number of this extent in the list of images. */
    uint32_t    uExtent;
    /** Pointer to the descriptor (NULL if no descriptor in this extent). */
    char        *pDescData;
    /** Pointer to the grain directory. */
    uint32_t    *pGD;
    /** Pointer to the redundant grain directory. */
    uint32_t    *pRGD;
    /** VMDK version of this extent. 1=1.0/1.1 */
    uint32_t    uVersion;
    /** Type of this extent. */
    VMDKETYPE   enmType;
    /** Access to this extent. */
    VMDKACCESS  enmAccess;
    /** Flag whether this extent is marked as unclean. */
    bool        fUncleanShutdown;
    /** Flag whether the metadata in the extent header needs to be updated. */
    bool        fMetaDirty;
    /** Flag whether there is a footer in this extent. */
    bool        fFooter;
    /** Compression type for this extent. */
    uint16_t    uCompression;
    /** Append position for writing new grain. Only for sparse extents. */
    uint64_t    uAppendPosition;
    /** Last grain which was accessed. Only for streamOptimized extents. */
    uint32_t    uLastGrainAccess;
    /** Starting sector corresponding to the grain buffer. */
    uint32_t    uGrainSectorAbs;
    /** Grain number corresponding to the grain buffer. */
    uint32_t    uGrain;
    /** Actual size of the compressed data, only valid for reading. */
    uint32_t    cbGrainStreamRead;
    /** Size of compressed grain buffer for streamOptimized extents. */
    size_t      cbCompGrain;
    /** Compressed grain buffer for streamOptimized extents, with marker. */
    void        *pvCompGrain;
    /** Decompressed grain buffer for streamOptimized extents. */
    void        *pvGrain;
    /** Reference to the image in which this extent is used. Do not use this
     * on a regular basis to avoid passing pImage references to functions
     * explicitly. */
    struct VMDKIMAGE *pImage;
} VMDKEXTENT, *PVMDKEXTENT;

/**
 * Grain table cache size. Allocated per image.
 */
#define VMDK_GT_CACHE_SIZE 256

/**
 * Grain table block size. Smaller than an actual grain table block to allow
 * more grain table blocks to be cached without having to allocate excessive
 * amounts of memory for the cache.
 */
#define VMDK_GT_CACHELINE_SIZE 128


/**
 * Maximum number of lines in a descriptor file. Not worth the effort of
 * making it variable. Descriptor files are generally very short (~20 lines),
 * with the exception of sparse files split in 2G chunks, which need for the
 * maximum size (almost 2T) exactly 1025 lines for the disk database.
 */
#define VMDK_DESCRIPTOR_LINES_MAX   1100U

/**
 * Parsed descriptor information. Allows easy access and update of the
 * descriptor (whether separate file or not). Free form text files suck.
 */
typedef struct VMDKDESCRIPTOR
{
    /** Line number of first entry of the disk descriptor. */
    unsigned    uFirstDesc;
    /** Line number of first entry in the extent description. */
    unsigned    uFirstExtent;
    /** Line number of first disk database entry. */
    unsigned    uFirstDDB;
    /** Total number of lines. */
    unsigned    cLines;
    /** Total amount of memory available for the descriptor. */
    size_t      cbDescAlloc;
    /** Set if descriptor has been changed and not yet written to disk. */
    bool        fDirty;
    /** Array of pointers to the data in the descriptor. */
    char        *aLines[VMDK_DESCRIPTOR_LINES_MAX];
    /** Array of line indices pointing to the next non-comment line. */
    unsigned    aNextLines[VMDK_DESCRIPTOR_LINES_MAX];
} VMDKDESCRIPTOR, *PVMDKDESCRIPTOR;


/**
 * Cache entry for translating extent/sector to a sector number in that
 * extent.
 */
typedef struct VMDKGTCACHEENTRY
{
    /** Extent number for which this entry is valid. */
    uint32_t    uExtent;
    /** GT data block number. */
    uint64_t    uGTBlock;
    /** Data part of the cache entry. */
    uint32_t    aGTData[VMDK_GT_CACHELINE_SIZE];
} VMDKGTCACHEENTRY, *PVMDKGTCACHEENTRY;

/**
 * Cache data structure for blocks of grain table entries. For now this is a
 * fixed size direct mapping cache, but this should be adapted to the size of
 * the sparse image and maybe converted to a set-associative cache. The
 * implementation below implements a write-through cache with write allocate.
 */
typedef struct VMDKGTCACHE
{
    /** Cache entries. */
    VMDKGTCACHEENTRY    aGTCache[VMDK_GT_CACHE_SIZE];
    /** Number of cache entries (currently unused). */
    unsigned            cEntries;
} VMDKGTCACHE, *PVMDKGTCACHE;

/**
 * Complete VMDK image data structure. Mainly a collection of extents and a few
 * extra global data fields.
 */
typedef struct VMDKIMAGE
{
    /** Image name. */
    const char        *pszFilename;
    /** Descriptor file if applicable. */
    PVMDKFILE         pFile;

    /** Pointer to the per-disk VD interface list. */
    PVDINTERFACE      pVDIfsDisk;
    /** Pointer to the per-image VD interface list. */
    PVDINTERFACE      pVDIfsImage;

    /** Error interface. */
    PVDINTERFACEERROR pIfError;
    /** I/O interface. */
    PVDINTERFACEIOINT pIfIo;


    /** Pointer to the image extents. */
    PVMDKEXTENT     pExtents;
    /** Number of image extents. */
    unsigned        cExtents;
    /** Pointer to the files list, for opening a file referenced multiple
     * times only once (happens mainly with raw partition access). */
    PVMDKFILE       pFiles;

    /**
     * Pointer to an array of segment entries for async I/O.
     * This is an optimization because the task number to submit is not known
     * and allocating/freeing an array in the read/write functions every time
     * is too expensive.
     */
    PRTSGSEG        paSegments;
    /** Entries available in the segments array. */
    unsigned        cSegments;

    /** Open flags passed by VBoxHD layer. */
    unsigned        uOpenFlags;
    /** Image flags defined during creation or determined during open. */
    unsigned        uImageFlags;
    /** Total size of the image. */
    uint64_t        cbSize;
    /** Physical geometry of this image. */
    VDGEOMETRY      PCHSGeometry;
    /** Logical geometry of this image. */
    VDGEOMETRY      LCHSGeometry;
    /** Image UUID. */
    RTUUID          ImageUuid;
    /** Image modification UUID. */
    RTUUID          ModificationUuid;
    /** Parent image UUID. */
    RTUUID          ParentUuid;
    /** Parent image modification UUID. */
    RTUUID          ParentModificationUuid;

    /** Pointer to grain table cache, if this image contains sparse extents. */
    PVMDKGTCACHE    pGTCache;
    /** Pointer to the descriptor (NULL if no separate descriptor file). */
    char            *pDescData;
    /** Allocation size of the descriptor file. */
    size_t          cbDescAlloc;
    /** Parsed descriptor file content. */
    VMDKDESCRIPTOR  Descriptor;
    /** The static region list. */
    VDREGIONLIST    RegionList;
} VMDKIMAGE;


/** State for the input/output callout of the inflate reader/deflate writer. */
typedef struct VMDKCOMPRESSIO
{
    /* Image this operation relates to. */
    PVMDKIMAGE pImage;
    /* Current read position. */
    ssize_t iOffset;
    /* Size of the compressed grain buffer (available data). */
    size_t cbCompGrain;
    /* Pointer to the compressed grain buffer. */
    void *pvCompGrain;
} VMDKCOMPRESSIO;


/** Tracks async grain allocation. */
typedef struct VMDKGRAINALLOCASYNC
{
    /** Flag whether the allocation failed. */
    bool        fIoErr;
    /** Current number of transfers pending.
     * If reached 0 and there is an error the old state is restored. */
    unsigned    cIoXfersPending;
    /** Sector number */
    uint64_t    uSector;
    /** Flag whether the grain table needs to be updated. */
    bool        fGTUpdateNeeded;
    /** Extent the allocation happens. */
    PVMDKEXTENT pExtent;
    /** Position of the new grain, required for the grain table update. */
    uint64_t    uGrainOffset;
    /** Grain table sector. */
    uint64_t    uGTSector;
    /** Backup grain table sector. */
    uint64_t    uRGTSector;
} VMDKGRAINALLOCASYNC, *PVMDKGRAINALLOCASYNC;

/**
 * State information for vmdkRename() and helpers.
 */
typedef struct VMDKRENAMESTATE
{
    /** Array of old filenames. */
    char           **apszOldName;
    /** Array of new filenames. */
    char           **apszNewName;
    /** Array of new lines in the extent descriptor. */
    char           **apszNewLines;
    /** Name of the old descriptor file if not a sparse image. */
    char           *pszOldDescName;
    /** Flag whether we called vmdkFreeImage(). */
    bool           fImageFreed;
    /** Flag whther the descriptor is embedded in the image (sparse) or
     * in a separate file. */
    bool           fEmbeddedDesc;
    /** Number of extents in the image. */
    unsigned       cExtents;
    /** New base filename. */
    char           *pszNewBaseName;
    /** The old base filename. */
    char           *pszOldBaseName;
    /** New full filename. */
    char           *pszNewFullName;
    /** Old full filename. */
    char           *pszOldFullName;
    /** The old image name. */
    const char     *pszOldImageName;
    /** Copy of the original VMDK descriptor. */
    VMDKDESCRIPTOR DescriptorCopy;
    /** Copy of the extent state for sparse images. */
    VMDKEXTENT     ExtentCopy;
} VMDKRENAMESTATE;
/** Pointer to a VMDK rename state. */
typedef VMDKRENAMESTATE *PVMDKRENAMESTATE;


/*********************************************************************************************************************************
*   Static Variables                                                                                                             *
*********************************************************************************************************************************/

/** NULL-terminated array of supported file extensions. */
static const VDFILEEXTENSION s_aVmdkFileExtensions[] =
{
    {"vmdk", VDTYPE_HDD},
    {NULL, VDTYPE_INVALID}
};

/** NULL-terminated array of configuration option. */
static const VDCONFIGINFO s_aVmdkConfigInfo[] =
{
    /* Options for VMDK raw disks */
    { "RawDrive",                       NULL,                             VDCFGVALUETYPE_STRING,       0 },
    { "Partitions",                     NULL,                             VDCFGVALUETYPE_STRING,       0 },
    { "BootSector",                     NULL,                             VDCFGVALUETYPE_BYTES,        0 },
    { "Relative",                       NULL,                             VDCFGVALUETYPE_INTEGER,      0 },

    /* End of options list */
    { NULL,                             NULL,                             VDCFGVALUETYPE_INTEGER,      0 }
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

static void vmdkFreeStreamBuffers(PVMDKEXTENT pExtent);
static int vmdkFreeExtentData(PVMDKIMAGE pImage, PVMDKEXTENT pExtent,
                              bool fDelete);

static int vmdkCreateExtents(PVMDKIMAGE pImage, unsigned cExtents);
static int vmdkFlushImage(PVMDKIMAGE pImage, PVDIOCTX pIoCtx);
static int vmdkSetImageComment(PVMDKIMAGE pImage, const char *pszComment);
static int vmdkFreeImage(PVMDKIMAGE pImage, bool fDelete, bool fFlush);

static DECLCALLBACK(int) vmdkAllocGrainComplete(void *pBackendData, PVDIOCTX pIoCtx,
                                                void *pvUser, int rcReq);

/**
 * Internal: open a file (using a file descriptor cache to ensure each file
 * is only opened once - anything else can cause locking problems).
 */
static int vmdkFileOpen(PVMDKIMAGE pImage, PVMDKFILE *ppVmdkFile,
                        const char *pszBasename, const char *pszFilename, uint32_t fOpen)
{
    int rc = VINF_SUCCESS;
    PVMDKFILE pVmdkFile;

    for (pVmdkFile = pImage->pFiles;
         pVmdkFile != NULL;
         pVmdkFile = pVmdkFile->pNext)
    {
        if (!strcmp(pszFilename, pVmdkFile->pszFilename))
        {
            Assert(fOpen == pVmdkFile->fOpen);
            pVmdkFile->uReferences++;

            *ppVmdkFile = pVmdkFile;

            return rc;
        }
    }

    /* If we get here, there's no matching entry in the cache. */
    pVmdkFile = (PVMDKFILE)RTMemAllocZ(sizeof(VMDKFILE));
    if (!pVmdkFile)
    {
        *ppVmdkFile = NULL;
        return VERR_NO_MEMORY;
    }

    pVmdkFile->pszFilename = RTStrDup(pszFilename);
    if (!pVmdkFile->pszFilename)
    {
        RTMemFree(pVmdkFile);
        *ppVmdkFile = NULL;
        return VERR_NO_MEMORY;
    }

    if (pszBasename)
    {
        pVmdkFile->pszBasename = RTStrDup(pszBasename);
        if (!pVmdkFile->pszBasename)
        {
            RTStrFree((char *)(void *)pVmdkFile->pszFilename);
            RTMemFree(pVmdkFile);
            *ppVmdkFile = NULL;
            return VERR_NO_MEMORY;
        }
    }

    pVmdkFile->fOpen = fOpen;

    rc = vdIfIoIntFileOpen(pImage->pIfIo, pszFilename, fOpen,
                           &pVmdkFile->pStorage);
    if (RT_SUCCESS(rc))
    {
        pVmdkFile->uReferences = 1;
        pVmdkFile->pImage = pImage;
        pVmdkFile->pNext = pImage->pFiles;
        if (pImage->pFiles)
            pImage->pFiles->pPrev = pVmdkFile;
        pImage->pFiles = pVmdkFile;
        *ppVmdkFile = pVmdkFile;
    }
    else
    {
        RTStrFree((char *)(void *)pVmdkFile->pszFilename);
        RTMemFree(pVmdkFile);
        *ppVmdkFile = NULL;
    }

    return rc;
}

/**
 * Internal: close a file, updating the file descriptor cache.
 */
static int vmdkFileClose(PVMDKIMAGE pImage, PVMDKFILE *ppVmdkFile, bool fDelete)
{
    int rc = VINF_SUCCESS;
    PVMDKFILE pVmdkFile = *ppVmdkFile;

    AssertPtr(pVmdkFile);

    pVmdkFile->fDelete |= fDelete;
    Assert(pVmdkFile->uReferences);
    pVmdkFile->uReferences--;
    if (pVmdkFile->uReferences == 0)
    {
        PVMDKFILE pPrev;
        PVMDKFILE pNext;

        /* Unchain the element from the list. */
        pPrev = pVmdkFile->pPrev;
        pNext = pVmdkFile->pNext;

        if (pNext)
            pNext->pPrev = pPrev;
        if (pPrev)
            pPrev->pNext = pNext;
        else
            pImage->pFiles = pNext;

        rc = vdIfIoIntFileClose(pImage->pIfIo, pVmdkFile->pStorage);

        bool fFileDel = pVmdkFile->fDelete;
        if (   pVmdkFile->pszBasename
            && fFileDel)
        {
            const char *pszSuffix = RTPathSuffix(pVmdkFile->pszBasename);
            if (   RTPathHasPath(pVmdkFile->pszBasename)
                || !pszSuffix
                || (   strcmp(pszSuffix, ".vmdk")
                    && strcmp(pszSuffix, ".bin")
                    && strcmp(pszSuffix, ".img")))
                fFileDel = false;
        }

        if (fFileDel)
        {
            int rc2 = vdIfIoIntFileDelete(pImage->pIfIo, pVmdkFile->pszFilename);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
        else if (pVmdkFile->fDelete)
            LogRel(("VMDK: Denying deletion of %s\n", pVmdkFile->pszBasename));
        RTStrFree((char *)(void *)pVmdkFile->pszFilename);
        if (pVmdkFile->pszBasename)
            RTStrFree((char *)(void *)pVmdkFile->pszBasename);
        RTMemFree(pVmdkFile);
    }

    *ppVmdkFile = NULL;
    return rc;
}

/*#define VMDK_USE_BLOCK_DECOMP_API - test and enable */
#ifndef VMDK_USE_BLOCK_DECOMP_API
static DECLCALLBACK(int) vmdkFileInflateHelper(void *pvUser, void *pvBuf, size_t cbBuf, size_t *pcbBuf)
{
    VMDKCOMPRESSIO *pInflateState = (VMDKCOMPRESSIO *)pvUser;
    size_t cbInjected = 0;

    Assert(cbBuf);
    if (pInflateState->iOffset < 0)
    {
        *(uint8_t *)pvBuf = RTZIPTYPE_ZLIB;
        pvBuf = (uint8_t *)pvBuf + 1;
        cbBuf--;
        cbInjected = 1;
        pInflateState->iOffset = RT_UOFFSETOF(VMDKMARKER, uType);
    }
    if (!cbBuf)
    {
        if (pcbBuf)
            *pcbBuf = cbInjected;
        return VINF_SUCCESS;
    }
    cbBuf = RT_MIN(cbBuf, pInflateState->cbCompGrain - pInflateState->iOffset);
    memcpy(pvBuf,
           (uint8_t *)pInflateState->pvCompGrain + pInflateState->iOffset,
           cbBuf);
    pInflateState->iOffset += cbBuf;
    Assert(pcbBuf);
    *pcbBuf = cbBuf + cbInjected;
    return VINF_SUCCESS;
}
#endif

/**
 * Internal: read from a file and inflate the compressed data,
 * distinguishing between async and normal operation
 */
DECLINLINE(int) vmdkFileInflateSync(PVMDKIMAGE pImage, PVMDKEXTENT pExtent,
                                    uint64_t uOffset, void *pvBuf,
                                    size_t cbToRead, const void *pcvMarker,
                                    uint64_t *puLBA, uint32_t *pcbMarkerData)
{
    int rc;
#ifndef VMDK_USE_BLOCK_DECOMP_API
    PRTZIPDECOMP pZip = NULL;
#endif
    VMDKMARKER *pMarker = (VMDKMARKER *)pExtent->pvCompGrain;
    size_t cbCompSize, cbActuallyRead;

    if (!pcvMarker)
    {
        rc = vdIfIoIntFileReadSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                   uOffset, pMarker, RT_UOFFSETOF(VMDKMARKER, uType));
        if (RT_FAILURE(rc))
            return rc;
    }
    else
    {
        memcpy(pMarker, pcvMarker, RT_UOFFSETOF(VMDKMARKER, uType));
        /* pcvMarker endianness has already been partially transformed, fix it */
        pMarker->uSector = RT_H2LE_U64(pMarker->uSector);
        pMarker->cbSize = RT_H2LE_U32(pMarker->cbSize);
    }

    cbCompSize = RT_LE2H_U32(pMarker->cbSize);
    if (cbCompSize == 0)
    {
        AssertMsgFailed(("VMDK: corrupted marker\n"));
        return VERR_VD_VMDK_INVALID_FORMAT;
    }

    /* Sanity check - the expansion ratio should be much less than 2. */
    Assert(cbCompSize < 2 * cbToRead);
    if (cbCompSize >= 2 * cbToRead)
        return VERR_VD_VMDK_INVALID_FORMAT;

    /* Compressed grain marker. Data follows immediately. */
    rc = vdIfIoIntFileReadSync(pImage->pIfIo, pExtent->pFile->pStorage,
                               uOffset + RT_UOFFSETOF(VMDKMARKER, uType),
                                (uint8_t *)pExtent->pvCompGrain
                              + RT_UOFFSETOF(VMDKMARKER, uType),
                               RT_ALIGN_Z(  cbCompSize
                                          + RT_UOFFSETOF(VMDKMARKER, uType),
                                          512)
                               - RT_UOFFSETOF(VMDKMARKER, uType));
    if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: Failed to read data from compressed image '%s'"), pExtent->pszFullname);

    if (puLBA)
        *puLBA = RT_LE2H_U64(pMarker->uSector);
    if (pcbMarkerData)
        *pcbMarkerData = RT_ALIGN(  cbCompSize
                                  + RT_UOFFSETOF(VMDKMARKER, uType),
                                  512);

#ifdef VMDK_USE_BLOCK_DECOMP_API
    rc = RTZipBlockDecompress(RTZIPTYPE_ZLIB, 0 /*fFlags*/,
                              pExtent->pvCompGrain, cbCompSize + RT_UOFFSETOF(VMDKMARKER, uType), NULL,
                              pvBuf, cbToRead, &cbActuallyRead);
#else
    VMDKCOMPRESSIO InflateState;
    InflateState.pImage = pImage;
    InflateState.iOffset = -1;
    InflateState.cbCompGrain = cbCompSize + RT_UOFFSETOF(VMDKMARKER, uType);
    InflateState.pvCompGrain = pExtent->pvCompGrain;

    rc = RTZipDecompCreate(&pZip, &InflateState, vmdkFileInflateHelper);
    if (RT_FAILURE(rc))
        return rc;
    rc = RTZipDecompress(pZip, pvBuf, cbToRead, &cbActuallyRead);
    RTZipDecompDestroy(pZip);
#endif /* !VMDK_USE_BLOCK_DECOMP_API */
    if (RT_FAILURE(rc))
    {
        if (rc == VERR_ZIP_CORRUPTED)
            rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: Compressed image is corrupted '%s'"), pExtent->pszFullname);
        return rc;
    }
    if (cbActuallyRead != cbToRead)
        rc = VERR_VD_VMDK_INVALID_FORMAT;
    return rc;
}

static DECLCALLBACK(int) vmdkFileDeflateHelper(void *pvUser, const void *pvBuf, size_t cbBuf)
{
    VMDKCOMPRESSIO *pDeflateState = (VMDKCOMPRESSIO *)pvUser;

    Assert(cbBuf);
    if (pDeflateState->iOffset < 0)
    {
        pvBuf = (const uint8_t *)pvBuf + 1;
        cbBuf--;
        pDeflateState->iOffset = RT_UOFFSETOF(VMDKMARKER, uType);
    }
    if (!cbBuf)
        return VINF_SUCCESS;
    if (pDeflateState->iOffset + cbBuf > pDeflateState->cbCompGrain)
        return VERR_BUFFER_OVERFLOW;
    memcpy((uint8_t *)pDeflateState->pvCompGrain + pDeflateState->iOffset,
           pvBuf, cbBuf);
    pDeflateState->iOffset += cbBuf;
    return VINF_SUCCESS;
}

/**
 * Internal: deflate the uncompressed data and write to a file,
 * distinguishing between async and normal operation
 */
DECLINLINE(int) vmdkFileDeflateSync(PVMDKIMAGE pImage, PVMDKEXTENT pExtent,
                                    uint64_t uOffset, const void *pvBuf,
                                    size_t cbToWrite, uint64_t uLBA,
                                    uint32_t *pcbMarkerData)
{
    int rc;
    PRTZIPCOMP pZip = NULL;
    VMDKCOMPRESSIO DeflateState;

    DeflateState.pImage = pImage;
    DeflateState.iOffset = -1;
    DeflateState.cbCompGrain = pExtent->cbCompGrain;
    DeflateState.pvCompGrain = pExtent->pvCompGrain;

    rc = RTZipCompCreate(&pZip, &DeflateState, vmdkFileDeflateHelper,
                         RTZIPTYPE_ZLIB, RTZIPLEVEL_DEFAULT);
    if (RT_FAILURE(rc))
        return rc;
    rc = RTZipCompress(pZip, pvBuf, cbToWrite);
    if (RT_SUCCESS(rc))
        rc = RTZipCompFinish(pZip);
    RTZipCompDestroy(pZip);
    if (RT_SUCCESS(rc))
    {
        Assert(   DeflateState.iOffset > 0
               && (size_t)DeflateState.iOffset <= DeflateState.cbCompGrain);

        /* pad with zeroes to get to a full sector size */
        uint32_t uSize = DeflateState.iOffset;
        if (uSize % 512)
        {
            uint32_t uSizeAlign = RT_ALIGN(uSize, 512);
            memset((uint8_t *)pExtent->pvCompGrain + uSize, '\0',
                   uSizeAlign - uSize);
            uSize = uSizeAlign;
        }

        if (pcbMarkerData)
            *pcbMarkerData = uSize;

        /* Compressed grain marker. Data follows immediately. */
        VMDKMARKER *pMarker = (VMDKMARKER *)pExtent->pvCompGrain;
        pMarker->uSector = RT_H2LE_U64(uLBA);
        pMarker->cbSize = RT_H2LE_U32(  DeflateState.iOffset
                                      - RT_UOFFSETOF(VMDKMARKER, uType));
        rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                    uOffset, pMarker, uSize);
        if (RT_FAILURE(rc))
            return rc;
    }
    return rc;
}


/**
 * Internal: check if all files are closed, prevent leaking resources.
 */
static int vmdkFileCheckAllClose(PVMDKIMAGE pImage)
{
    int rc = VINF_SUCCESS, rc2;
    PVMDKFILE pVmdkFile;

    Assert(pImage->pFiles == NULL);
    for (pVmdkFile = pImage->pFiles;
         pVmdkFile != NULL;
         pVmdkFile = pVmdkFile->pNext)
    {
        LogRel(("VMDK: leaking reference to file \"%s\"\n",
                pVmdkFile->pszFilename));
        pImage->pFiles = pVmdkFile->pNext;

        rc2 = vmdkFileClose(pImage, &pVmdkFile, pVmdkFile->fDelete);

        if (RT_SUCCESS(rc))
            rc = rc2;
    }
    return rc;
}

/**
 * Internal: truncate a string (at a UTF8 code point boundary) and encode the
 * critical non-ASCII characters.
 */
static char *vmdkEncodeString(const char *psz)
{
    char szEnc[VMDK_ENCODED_COMMENT_MAX + 3];
    char *pszDst = szEnc;

    AssertPtr(psz);

    for (; *psz; psz = RTStrNextCp(psz))
    {
        char *pszDstPrev = pszDst;
        RTUNICP Cp = RTStrGetCp(psz);
        if (Cp == '\\')
        {
            pszDst = RTStrPutCp(pszDst, Cp);
            pszDst = RTStrPutCp(pszDst, Cp);
        }
        else if (Cp == '\n')
        {
            pszDst = RTStrPutCp(pszDst, '\\');
            pszDst = RTStrPutCp(pszDst, 'n');
        }
        else if (Cp == '\r')
        {
            pszDst = RTStrPutCp(pszDst, '\\');
            pszDst = RTStrPutCp(pszDst, 'r');
        }
        else
            pszDst = RTStrPutCp(pszDst, Cp);
        if (pszDst - szEnc >= VMDK_ENCODED_COMMENT_MAX - 1)
        {
            pszDst = pszDstPrev;
            break;
        }
    }
    *pszDst = '\0';
    return RTStrDup(szEnc);
}

/**
 * Internal: decode a string and store it into the specified string.
 */
static int vmdkDecodeString(const char *pszEncoded, char *psz, size_t cb)
{
    int rc = VINF_SUCCESS;
    char szBuf[8];

    if (!cb)
        return VERR_BUFFER_OVERFLOW;

    AssertPtr(psz);

    for (; *pszEncoded; pszEncoded = RTStrNextCp(pszEncoded))
    {
        char *pszDst = szBuf;
        RTUNICP Cp = RTStrGetCp(pszEncoded);
        if (Cp == '\\')
        {
            pszEncoded = RTStrNextCp(pszEncoded);
            RTUNICP CpQ = RTStrGetCp(pszEncoded);
            if (CpQ == 'n')
                RTStrPutCp(pszDst, '\n');
            else if (CpQ == 'r')
                RTStrPutCp(pszDst, '\r');
            else if (CpQ == '\0')
            {
                rc = VERR_VD_VMDK_INVALID_HEADER;
                break;
            }
            else
                RTStrPutCp(pszDst, CpQ);
        }
        else
            pszDst = RTStrPutCp(pszDst, Cp);

        /* Need to leave space for terminating NUL. */
        if ((size_t)(pszDst - szBuf) + 1 >= cb)
        {
            rc = VERR_BUFFER_OVERFLOW;
            break;
        }
        memcpy(psz, szBuf, pszDst - szBuf);
        psz += pszDst - szBuf;
    }
    *psz = '\0';
    return rc;
}

/**
 * Internal: free all buffers associated with grain directories.
 */
static void vmdkFreeGrainDirectory(PVMDKEXTENT pExtent)
{
    if (pExtent->pGD)
    {
        RTMemFree(pExtent->pGD);
        pExtent->pGD = NULL;
    }
    if (pExtent->pRGD)
    {
        RTMemFree(pExtent->pRGD);
        pExtent->pRGD = NULL;
    }
}

/**
 * Internal: allocate the compressed/uncompressed buffers for streamOptimized
 * images.
 */
static int vmdkAllocStreamBuffers(PVMDKIMAGE pImage, PVMDKEXTENT pExtent)
{
    int rc = VINF_SUCCESS;

    if (pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED)
    {
        /* streamOptimized extents need a compressed grain buffer, which must
         * be big enough to hold uncompressible data (which needs ~8 bytes
         * more than the uncompressed data), the marker and padding. */
        pExtent->cbCompGrain = RT_ALIGN_Z(  VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain)
                                          + 8 + sizeof(VMDKMARKER), 512);
        pExtent->pvCompGrain = RTMemAlloc(pExtent->cbCompGrain);
        if (RT_LIKELY(pExtent->pvCompGrain))
        {
            /* streamOptimized extents need a decompressed grain buffer. */
            pExtent->pvGrain = RTMemAlloc(VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain));
            if (!pExtent->pvGrain)
                rc = VERR_NO_MEMORY;
        }
        else
            rc = VERR_NO_MEMORY;
    }

    if (RT_FAILURE(rc))
        vmdkFreeStreamBuffers(pExtent);
    return rc;
}

/**
 * Internal: allocate all buffers associated with grain directories.
 */
static int vmdkAllocGrainDirectory(PVMDKIMAGE pImage, PVMDKEXTENT pExtent)
{
    RT_NOREF1(pImage);
    int rc = VINF_SUCCESS;
    size_t cbGD = pExtent->cGDEntries * sizeof(uint32_t);

    pExtent->pGD = (uint32_t *)RTMemAllocZ(cbGD);
    if (RT_LIKELY(pExtent->pGD))
    {
        if (pExtent->uSectorRGD)
        {
            pExtent->pRGD = (uint32_t *)RTMemAllocZ(cbGD);
            if (RT_UNLIKELY(!pExtent->pRGD))
                rc = VERR_NO_MEMORY;
        }
    }
    else
        rc = VERR_NO_MEMORY;

    if (RT_FAILURE(rc))
        vmdkFreeGrainDirectory(pExtent);
    return rc;
}

/**
 * Converts the grain directory from little to host endianess.
 *
 * @param   pGD             The grain directory.
 * @param   cGDEntries      Number of entries in the grain directory to convert.
 */
DECLINLINE(void) vmdkGrainDirectoryConvToHost(uint32_t *pGD, uint32_t cGDEntries)
{
    uint32_t *pGDTmp = pGD;

    for (uint32_t i = 0; i < cGDEntries; i++, pGDTmp++)
        *pGDTmp = RT_LE2H_U32(*pGDTmp);
}

/**
 * Read the grain directory and allocated grain tables verifying them against
 * their back up copies if available.
 *
 * @returns VBox status code.
 * @param   pImage          Image instance data.
 * @param   pExtent         The VMDK extent.
 */
static int vmdkReadGrainDirectory(PVMDKIMAGE pImage, PVMDKEXTENT pExtent)
{
    int rc = VINF_SUCCESS;
    size_t cbGD = pExtent->cGDEntries * sizeof(uint32_t);

    AssertReturn((   pExtent->enmType == VMDKETYPE_HOSTED_SPARSE
                  && pExtent->uSectorGD != VMDK_GD_AT_END
                  && pExtent->uSectorRGD != VMDK_GD_AT_END), VERR_INTERNAL_ERROR);

    rc = vmdkAllocGrainDirectory(pImage, pExtent);
    if (RT_SUCCESS(rc))
    {
        /* The VMDK 1.1 spec seems to talk about compressed grain directories,
         * but in reality they are not compressed. */
        rc = vdIfIoIntFileReadSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                   VMDK_SECTOR2BYTE(pExtent->uSectorGD),
                                   pExtent->pGD, cbGD);
        if (RT_SUCCESS(rc))
        {
            vmdkGrainDirectoryConvToHost(pExtent->pGD, pExtent->cGDEntries);

            if (   pExtent->uSectorRGD
                && !(pImage->uOpenFlags & VD_OPEN_FLAGS_SKIP_CONSISTENCY_CHECKS))
            {
                /* The VMDK 1.1 spec seems to talk about compressed grain directories,
                 * but in reality they are not compressed. */
                rc = vdIfIoIntFileReadSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                           VMDK_SECTOR2BYTE(pExtent->uSectorRGD),
                                           pExtent->pRGD, cbGD);
                if (RT_SUCCESS(rc))
                {
                    vmdkGrainDirectoryConvToHost(pExtent->pRGD, pExtent->cGDEntries);

                    /* Check grain table and redundant grain table for consistency. */
                    size_t cbGT = pExtent->cGTEntries * sizeof(uint32_t);
                    size_t cbGTBuffers = cbGT; /* Start with space for one GT. */
                    size_t cbGTBuffersMax = _1M;

                    uint32_t *pTmpGT1 = (uint32_t *)RTMemAlloc(cbGTBuffers);
                    uint32_t *pTmpGT2 = (uint32_t *)RTMemAlloc(cbGTBuffers);

                    if (   !pTmpGT1
                        || !pTmpGT2)
                        rc = VERR_NO_MEMORY;

                    size_t i = 0;
                    uint32_t *pGDTmp = pExtent->pGD;
                    uint32_t *pRGDTmp = pExtent->pRGD;

                    /* Loop through all entries. */
                    while (i < pExtent->cGDEntries)
                    {
                        uint32_t uGTStart = *pGDTmp;
                        uint32_t uRGTStart = *pRGDTmp;
                        size_t   cbGTRead = cbGT;

                        /* If no grain table is allocated skip the entry. */
                        if (*pGDTmp == 0 && *pRGDTmp == 0)
                        {
                            i++;
                            continue;
                        }

                        if (*pGDTmp == 0 || *pRGDTmp == 0 || *pGDTmp == *pRGDTmp)
                        {
                            /* Just one grain directory entry refers to a not yet allocated
                             * grain table or both grain directory copies refer to the same
                             * grain table. Not allowed. */
                            rc = vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS,
                                           N_("VMDK: inconsistent references to grain directory in '%s'"), pExtent->pszFullname);
                            break;
                        }

                        i++;
                        pGDTmp++;
                        pRGDTmp++;

                        /*
                         * Read a few tables at once if adjacent to decrease the number
                         * of I/O requests. Read at maximum 1MB at once.
                         */
                        while (   i < pExtent->cGDEntries
                               && cbGTRead < cbGTBuffersMax)
                        {
                            /* If no grain table is allocated skip the entry. */
                            if (*pGDTmp == 0 && *pRGDTmp == 0)
                            {
                                i++;
                                continue;
                            }

                            if (*pGDTmp == 0 || *pRGDTmp == 0 || *pGDTmp == *pRGDTmp)
                            {
                                /* Just one grain directory entry refers to a not yet allocated
                                 * grain table or both grain directory copies refer to the same
                                 * grain table. Not allowed. */
                                rc = vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS,
                                               N_("VMDK: inconsistent references to grain directory in '%s'"), pExtent->pszFullname);
                                break;
                            }

                            /* Check that the start offsets are adjacent.*/
                            if (   VMDK_SECTOR2BYTE(uGTStart) + cbGTRead != VMDK_SECTOR2BYTE(*pGDTmp)
                                || VMDK_SECTOR2BYTE(uRGTStart) + cbGTRead != VMDK_SECTOR2BYTE(*pRGDTmp))
                                break;

                            i++;
                            pGDTmp++;
                            pRGDTmp++;
                            cbGTRead += cbGT;
                        }

                        /* Increase buffers if required. */
                        if (   RT_SUCCESS(rc)
                            && cbGTBuffers < cbGTRead)
                        {
                            uint32_t *pTmp;
                            pTmp = (uint32_t *)RTMemRealloc(pTmpGT1, cbGTRead);
                            if (pTmp)
                            {
                                pTmpGT1 = pTmp;
                                pTmp = (uint32_t *)RTMemRealloc(pTmpGT2, cbGTRead);
                                if (pTmp)
                                    pTmpGT2 = pTmp;
                                else
                                    rc = VERR_NO_MEMORY;
                            }
                            else
                                rc = VERR_NO_MEMORY;

                            if (rc == VERR_NO_MEMORY)
                            {
                                /* Reset to the old values. */
                                rc = VINF_SUCCESS;
                                i -= cbGTRead / cbGT;
                                cbGTRead = cbGT;

                                /* Don't try to increase the buffer again in the next run. */
                                cbGTBuffersMax = cbGTBuffers;
                            }
                        }

                        if (RT_SUCCESS(rc))
                        {
                           /* The VMDK 1.1 spec seems to talk about compressed grain tables,
                             * but in reality they are not compressed. */
                            rc = vdIfIoIntFileReadSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                                       VMDK_SECTOR2BYTE(uGTStart),
                                                       pTmpGT1, cbGTRead);
                            if (RT_FAILURE(rc))
                            {
                                rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                                               N_("VMDK: error reading grain table in '%s'"), pExtent->pszFullname);
                                break;
                            }
                            /* The VMDK 1.1 spec seems to talk about compressed grain tables,
                             * but in reality they are not compressed. */
                            rc = vdIfIoIntFileReadSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                                       VMDK_SECTOR2BYTE(uRGTStart),
                                                       pTmpGT2, cbGTRead);
                            if (RT_FAILURE(rc))
                            {
                                rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                                               N_("VMDK: error reading backup grain table in '%s'"), pExtent->pszFullname);
                                break;
                            }
                            if (memcmp(pTmpGT1, pTmpGT2, cbGTRead))
                            {
                                rc = vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS,
                                               N_("VMDK: inconsistency between grain table and backup grain table in '%s'"), pExtent->pszFullname);
                                break;
                            }
                        }
                    } /* while (i < pExtent->cGDEntries) */

                    /** @todo figure out what to do for unclean VMDKs. */
                    if (pTmpGT1)
                        RTMemFree(pTmpGT1);
                    if (pTmpGT2)
                        RTMemFree(pTmpGT2);
                }
                else
                    rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                                   N_("VMDK: could not read redundant grain directory in '%s'"), pExtent->pszFullname);
            }
        }
        else
            rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                           N_("VMDK: could not read grain directory in '%s': %Rrc"), pExtent->pszFullname, rc);
    }

    if (RT_FAILURE(rc))
        vmdkFreeGrainDirectory(pExtent);
    return rc;
}

/**
 * Creates a new grain directory for the given extent at the given start sector.
 *
 * @returns VBox status code.
 * @param   pImage          Image instance data.
 * @param   pExtent         The VMDK extent.
 * @param   uStartSector    Where the grain directory should be stored in the image.
 * @param   fPreAlloc       Flag whether to pre allocate the grain tables at this point.
 */
static int vmdkCreateGrainDirectory(PVMDKIMAGE pImage, PVMDKEXTENT pExtent,
                                    uint64_t uStartSector, bool fPreAlloc)
{
    int rc = VINF_SUCCESS;
    unsigned i;
    size_t cbGD = pExtent->cGDEntries * sizeof(uint32_t);
    size_t cbGDRounded = RT_ALIGN_64(cbGD, 512);
    size_t cbGTRounded;
    uint64_t cbOverhead;

    if (fPreAlloc)
    {
        cbGTRounded = RT_ALIGN_64(pExtent->cGDEntries * pExtent->cGTEntries * sizeof(uint32_t), 512);
        cbOverhead  = VMDK_SECTOR2BYTE(uStartSector) + cbGDRounded + cbGTRounded;
    }
    else
    {
        /* Use a dummy start sector for layout computation. */
        if (uStartSector == VMDK_GD_AT_END)
            uStartSector = 1;
        cbGTRounded = 0;
        cbOverhead = VMDK_SECTOR2BYTE(uStartSector) + cbGDRounded;
    }

    /* For streamOptimized extents there is only one grain directory,
     * and for all others take redundant grain directory into account. */
    if (pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED)
    {
        cbOverhead = RT_ALIGN_64(cbOverhead,
                                 VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain));
    }
    else
    {
        cbOverhead += cbGDRounded + cbGTRounded;
        cbOverhead = RT_ALIGN_64(cbOverhead,
                                 VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain));
        rc = vdIfIoIntFileSetSize(pImage->pIfIo, pExtent->pFile->pStorage, cbOverhead);
    }

    if (RT_SUCCESS(rc))
    {
        pExtent->uAppendPosition = cbOverhead;
        pExtent->cOverheadSectors = VMDK_BYTE2SECTOR(cbOverhead);

        if (pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED)
        {
            pExtent->uSectorRGD = 0;
            pExtent->uSectorGD = uStartSector;
        }
        else
        {
            pExtent->uSectorRGD = uStartSector;
            pExtent->uSectorGD = uStartSector + VMDK_BYTE2SECTOR(cbGDRounded + cbGTRounded);
        }

        rc = vmdkAllocStreamBuffers(pImage, pExtent);
        if (RT_SUCCESS(rc))
        {
            rc = vmdkAllocGrainDirectory(pImage, pExtent);
            if (   RT_SUCCESS(rc)
                && fPreAlloc)
            {
                uint32_t uGTSectorLE;
                uint64_t uOffsetSectors;

                if (pExtent->pRGD)
                {
                    uOffsetSectors = pExtent->uSectorRGD + VMDK_BYTE2SECTOR(cbGDRounded);
                    for (i = 0; i < pExtent->cGDEntries; i++)
                    {
                        pExtent->pRGD[i] = uOffsetSectors;
                        uGTSectorLE = RT_H2LE_U64(uOffsetSectors);
                        /* Write the redundant grain directory entry to disk. */
                        rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                                    VMDK_SECTOR2BYTE(pExtent->uSectorRGD) + i * sizeof(uGTSectorLE),
                                                    &uGTSectorLE, sizeof(uGTSectorLE));
                        if (RT_FAILURE(rc))
                        {
                            rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: cannot write new redundant grain directory entry in '%s'"), pExtent->pszFullname);
                            break;
                        }
                        uOffsetSectors += VMDK_BYTE2SECTOR(pExtent->cGTEntries * sizeof(uint32_t));
                    }
                }

                if (RT_SUCCESS(rc))
                {
                    uOffsetSectors = pExtent->uSectorGD + VMDK_BYTE2SECTOR(cbGDRounded);
                    for (i = 0; i < pExtent->cGDEntries; i++)
                    {
                        pExtent->pGD[i] = uOffsetSectors;
                        uGTSectorLE = RT_H2LE_U64(uOffsetSectors);
                        /* Write the grain directory entry to disk. */
                        rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                                    VMDK_SECTOR2BYTE(pExtent->uSectorGD) + i * sizeof(uGTSectorLE),
                                                    &uGTSectorLE, sizeof(uGTSectorLE));
                        if (RT_FAILURE(rc))
                        {
                            rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: cannot write new grain directory entry in '%s'"), pExtent->pszFullname);
                            break;
                        }
                        uOffsetSectors += VMDK_BYTE2SECTOR(pExtent->cGTEntries * sizeof(uint32_t));
                    }
                }
            }
        }
    }

    if (RT_FAILURE(rc))
        vmdkFreeGrainDirectory(pExtent);
    return rc;
}

/**
 * Unquotes the given string returning the result in a separate buffer.
 *
 * @returns VBox status code.
 * @param   pImage          The VMDK image state.
 * @param   pszStr          The string to unquote.
 * @param   ppszUnquoted    Where to store the return value, use RTMemTmpFree to
 *                          free.
 * @param   ppszNext        Where to store the pointer to any character following
 *                          the quoted value, optional.
 */
static int vmdkStringUnquote(PVMDKIMAGE pImage, const char *pszStr,
                             char **ppszUnquoted, char **ppszNext)
{
    const char *pszStart = pszStr;
    char *pszQ;
    char *pszUnquoted;

    /* Skip over whitespace. */
    while (*pszStr == ' ' || *pszStr == '\t')
        pszStr++;

    if (*pszStr != '"')
    {
        pszQ = (char *)pszStr;
        while (*pszQ && *pszQ != ' ' && *pszQ != '\t')
            pszQ++;
    }
    else
    {
        pszStr++;
        pszQ = (char *)strchr(pszStr, '"');
        if (pszQ == NULL)
            return vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS, N_("VMDK: incorrectly quoted value in descriptor in '%s' (raw value %s)"),
                             pImage->pszFilename, pszStart);
    }

    pszUnquoted = (char *)RTMemTmpAlloc(pszQ - pszStr + 1);
    if (!pszUnquoted)
        return VERR_NO_MEMORY;
    memcpy(pszUnquoted, pszStr, pszQ - pszStr);
    pszUnquoted[pszQ - pszStr] = '\0';
    *ppszUnquoted = pszUnquoted;
    if (ppszNext)
        *ppszNext = pszQ + 1;
    return VINF_SUCCESS;
}

static int vmdkDescInitStr(PVMDKIMAGE pImage, PVMDKDESCRIPTOR pDescriptor,
                           const char *pszLine)
{
    char *pEnd = pDescriptor->aLines[pDescriptor->cLines];
    ssize_t cbDiff = strlen(pszLine) + 1;

    if (    pDescriptor->cLines >= VMDK_DESCRIPTOR_LINES_MAX - 1
        &&  pEnd - pDescriptor->aLines[0] > (ptrdiff_t)pDescriptor->cbDescAlloc - cbDiff)
        return vdIfError(pImage->pIfError, VERR_BUFFER_OVERFLOW, RT_SRC_POS, N_("VMDK: descriptor too big in '%s'"), pImage->pszFilename);

    memcpy(pEnd, pszLine, cbDiff);
    pDescriptor->cLines++;
    pDescriptor->aLines[pDescriptor->cLines] = pEnd + cbDiff;
    pDescriptor->fDirty = true;

    return VINF_SUCCESS;
}

static bool vmdkDescGetStr(PVMDKDESCRIPTOR pDescriptor, unsigned uStart,
                           const char *pszKey, const char **ppszValue)
{
    size_t cbKey = strlen(pszKey);
    const char *pszValue;

    while (uStart != 0)
    {
        if (!strncmp(pDescriptor->aLines[uStart], pszKey, cbKey))
        {
            /* Key matches, check for a '=' (preceded by whitespace). */
            pszValue = pDescriptor->aLines[uStart] + cbKey;
            while (*pszValue == ' ' || *pszValue == '\t')
                pszValue++;
            if (*pszValue == '=')
            {
                *ppszValue = pszValue + 1;
                break;
            }
        }
        uStart = pDescriptor->aNextLines[uStart];
    }
    return !!uStart;
}

static int vmdkDescSetStr(PVMDKIMAGE pImage, PVMDKDESCRIPTOR pDescriptor,
                          unsigned uStart,
                          const char *pszKey, const char *pszValue)
{
    char *pszTmp = NULL; /* (MSC naturally cannot figure this isn't used uninitialized) */
    size_t cbKey = strlen(pszKey);
    unsigned uLast = 0;

    while (uStart != 0)
    {
        if (!strncmp(pDescriptor->aLines[uStart], pszKey, cbKey))
        {
            /* Key matches, check for a '=' (preceded by whitespace). */
            pszTmp = pDescriptor->aLines[uStart] + cbKey;
            while (*pszTmp == ' ' || *pszTmp == '\t')
                pszTmp++;
            if (*pszTmp == '=')
            {
                pszTmp++;
                /** @todo r=bird: Doesn't skipping trailing blanks here just cause unecessary
                 *        bloat and potentially out of space error? */
                while (*pszTmp == ' ' || *pszTmp == '\t')
                    pszTmp++;
                break;
            }
        }
        if (!pDescriptor->aNextLines[uStart])
            uLast = uStart;
        uStart = pDescriptor->aNextLines[uStart];
    }
    if (uStart)
    {
        if (pszValue)
        {
            /* Key already exists, replace existing value. */
            size_t cbOldVal = strlen(pszTmp);
            size_t cbNewVal = strlen(pszValue);
            ssize_t cbDiff = cbNewVal - cbOldVal;
            /* Check for buffer overflow. */
            if (  pDescriptor->aLines[pDescriptor->cLines] - pDescriptor->aLines[0]
                > (ptrdiff_t)pDescriptor->cbDescAlloc - cbDiff)
                return vdIfError(pImage->pIfError, VERR_BUFFER_OVERFLOW, RT_SRC_POS, N_("VMDK: descriptor too big in '%s'"), pImage->pszFilename);

            memmove(pszTmp + cbNewVal, pszTmp + cbOldVal,
                    pDescriptor->aLines[pDescriptor->cLines] - pszTmp - cbOldVal);
            memcpy(pszTmp, pszValue, cbNewVal + 1);
            for (unsigned i = uStart + 1; i <= pDescriptor->cLines; i++)
                pDescriptor->aLines[i] += cbDiff;
        }
        else
        {
            memmove(pDescriptor->aLines[uStart], pDescriptor->aLines[uStart+1],
                    pDescriptor->aLines[pDescriptor->cLines] - pDescriptor->aLines[uStart+1] + 1);
            for (unsigned i = uStart + 1; i <= pDescriptor->cLines; i++)
            {
                pDescriptor->aLines[i-1] = pDescriptor->aLines[i];
                if (pDescriptor->aNextLines[i])
                    pDescriptor->aNextLines[i-1] = pDescriptor->aNextLines[i] - 1;
                else
                    pDescriptor->aNextLines[i-1] = 0;
            }
            pDescriptor->cLines--;
            /* Adjust starting line numbers of following descriptor sections. */
            if (uStart < pDescriptor->uFirstExtent)
                pDescriptor->uFirstExtent--;
            if (uStart < pDescriptor->uFirstDDB)
                pDescriptor->uFirstDDB--;
        }
    }
    else
    {
        /* Key doesn't exist, append after the last entry in this category. */
        if (!pszValue)
        {
            /* Key doesn't exist, and it should be removed. Simply a no-op. */
            return VINF_SUCCESS;
        }
        cbKey = strlen(pszKey);
        size_t cbValue = strlen(pszValue);
        ssize_t cbDiff = cbKey + 1 + cbValue + 1;
        /* Check for buffer overflow. */
        if (   (pDescriptor->cLines >= VMDK_DESCRIPTOR_LINES_MAX - 1)
            || (  pDescriptor->aLines[pDescriptor->cLines]
                - pDescriptor->aLines[0] > (ptrdiff_t)pDescriptor->cbDescAlloc - cbDiff))
            return vdIfError(pImage->pIfError, VERR_BUFFER_OVERFLOW, RT_SRC_POS, N_("VMDK: descriptor too big in '%s'"), pImage->pszFilename);
        for (unsigned i = pDescriptor->cLines + 1; i > uLast + 1; i--)
        {
            pDescriptor->aLines[i] = pDescriptor->aLines[i - 1];
            if (pDescriptor->aNextLines[i - 1])
                pDescriptor->aNextLines[i] = pDescriptor->aNextLines[i - 1] + 1;
            else
                pDescriptor->aNextLines[i] = 0;
        }
        uStart = uLast + 1;
        pDescriptor->aNextLines[uLast] = uStart;
        pDescriptor->aNextLines[uStart] = 0;
        pDescriptor->cLines++;
        pszTmp = pDescriptor->aLines[uStart];
        memmove(pszTmp + cbDiff, pszTmp,
                pDescriptor->aLines[pDescriptor->cLines] - pszTmp);
        memcpy(pDescriptor->aLines[uStart], pszKey, cbKey);
        pDescriptor->aLines[uStart][cbKey] = '=';
        memcpy(pDescriptor->aLines[uStart] + cbKey + 1, pszValue, cbValue + 1);
        for (unsigned i = uStart + 1; i <= pDescriptor->cLines; i++)
            pDescriptor->aLines[i] += cbDiff;

        /* Adjust starting line numbers of following descriptor sections. */
        if (uStart <= pDescriptor->uFirstExtent)
            pDescriptor->uFirstExtent++;
        if (uStart <= pDescriptor->uFirstDDB)
            pDescriptor->uFirstDDB++;
    }
    pDescriptor->fDirty = true;
    return VINF_SUCCESS;
}

static int vmdkDescBaseGetU32(PVMDKDESCRIPTOR pDescriptor, const char *pszKey,
                              uint32_t *puValue)
{
    const char *pszValue;

    if (!vmdkDescGetStr(pDescriptor, pDescriptor->uFirstDesc, pszKey,
                        &pszValue))
        return VERR_VD_VMDK_VALUE_NOT_FOUND;
    return RTStrToUInt32Ex(pszValue, NULL, 10, puValue);
}

/**
 * Returns the value of the given key as a string allocating the necessary memory.
 *
 * @returns VBox status code.
 * @retval  VERR_VD_VMDK_VALUE_NOT_FOUND if the value could not be found.
 * @param   pImage          The VMDK image state.
 * @param   pDescriptor     The descriptor to fetch the value from.
 * @param   pszKey          The key to get the value from.
 * @param   ppszValue       Where to store the return value, use RTMemTmpFree to
 *                          free.
 */
static int vmdkDescBaseGetStr(PVMDKIMAGE pImage, PVMDKDESCRIPTOR pDescriptor,
                              const char *pszKey, char **ppszValue)
{
    const char *pszValue;
    char *pszValueUnquoted;

    if (!vmdkDescGetStr(pDescriptor, pDescriptor->uFirstDesc, pszKey,
                        &pszValue))
        return VERR_VD_VMDK_VALUE_NOT_FOUND;
    int rc = vmdkStringUnquote(pImage, pszValue, &pszValueUnquoted, NULL);
    if (RT_FAILURE(rc))
        return rc;
    *ppszValue = pszValueUnquoted;
    return rc;
}

static int vmdkDescBaseSetStr(PVMDKIMAGE pImage, PVMDKDESCRIPTOR pDescriptor,
                              const char *pszKey, const char *pszValue)
{
    char *pszValueQuoted;

    RTStrAPrintf(&pszValueQuoted, "\"%s\"", pszValue);
    if (!pszValueQuoted)
        return VERR_NO_STR_MEMORY;
    int rc = vmdkDescSetStr(pImage, pDescriptor, pDescriptor->uFirstDesc, pszKey,
                            pszValueQuoted);
    RTStrFree(pszValueQuoted);
    return rc;
}

static void vmdkDescExtRemoveDummy(PVMDKIMAGE pImage,
                                   PVMDKDESCRIPTOR pDescriptor)
{
    RT_NOREF1(pImage);
    unsigned uEntry = pDescriptor->uFirstExtent;
    ssize_t cbDiff;

    if (!uEntry)
        return;

    cbDiff = strlen(pDescriptor->aLines[uEntry]) + 1;
    /* Move everything including \0 in the entry marking the end of buffer. */
    memmove(pDescriptor->aLines[uEntry], pDescriptor->aLines[uEntry + 1],
            pDescriptor->aLines[pDescriptor->cLines] - pDescriptor->aLines[uEntry + 1] + 1);
    for (unsigned i = uEntry + 1; i <= pDescriptor->cLines; i++)
    {
        pDescriptor->aLines[i - 1] = pDescriptor->aLines[i] - cbDiff;
        if (pDescriptor->aNextLines[i])
            pDescriptor->aNextLines[i - 1] = pDescriptor->aNextLines[i] - 1;
        else
            pDescriptor->aNextLines[i - 1] = 0;
    }
    pDescriptor->cLines--;
    if (pDescriptor->uFirstDDB)
        pDescriptor->uFirstDDB--;

    return;
}

static void vmdkDescExtRemoveByLine(PVMDKIMAGE pImage,
                                   PVMDKDESCRIPTOR pDescriptor, unsigned uLine)
{
    RT_NOREF1(pImage);
    unsigned uEntry = uLine;
    ssize_t cbDiff;
    if (!uEntry)
        return;
    cbDiff = strlen(pDescriptor->aLines[uEntry]) + 1;
    /* Move everything including \0 in the entry marking the end of buffer. */
    memmove(pDescriptor->aLines[uEntry], pDescriptor->aLines[uEntry + 1],
            pDescriptor->aLines[pDescriptor->cLines] - pDescriptor->aLines[uEntry + 1] + 1);
    for (unsigned i = uEntry; i <= pDescriptor->cLines; i++)
    {
        if (i != uEntry)
            pDescriptor->aLines[i - 1] = pDescriptor->aLines[i] - cbDiff;
        if (pDescriptor->aNextLines[i])
            pDescriptor->aNextLines[i - 1] = pDescriptor->aNextLines[i] - 1;
        else
            pDescriptor->aNextLines[i - 1] = 0;
    }
    pDescriptor->cLines--;
    if (pDescriptor->uFirstDDB)
        pDescriptor->uFirstDDB--;
    return;
}

static int vmdkDescExtInsert(PVMDKIMAGE pImage, PVMDKDESCRIPTOR pDescriptor,
                             VMDKACCESS enmAccess, uint64_t cNominalSectors,
                             VMDKETYPE enmType, const char *pszBasename,
                             uint64_t uSectorOffset)
{
    static const char *apszAccess[] = { "NOACCESS", "RDONLY", "RW" };
    static const char *apszType[] = { "", "SPARSE", "FLAT", "ZERO", "VMFS" };
    char *pszTmp;
    unsigned uStart = pDescriptor->uFirstExtent, uLast = 0;
    char szExt[1024];
    ssize_t cbDiff;

    Assert((unsigned)enmAccess < RT_ELEMENTS(apszAccess));
    Assert((unsigned)enmType < RT_ELEMENTS(apszType));

    /* Find last entry in extent description. */
    while (uStart)
    {
        if (!pDescriptor->aNextLines[uStart])
            uLast = uStart;
        uStart = pDescriptor->aNextLines[uStart];
    }

    if (enmType == VMDKETYPE_ZERO)
    {
        RTStrPrintf(szExt, sizeof(szExt), "%s %llu %s ", apszAccess[enmAccess],
                    cNominalSectors, apszType[enmType]);
    }
    else if (enmType == VMDKETYPE_FLAT)
    {
        RTStrPrintf(szExt, sizeof(szExt), "%s %llu %s \"%s\" %llu",
                    apszAccess[enmAccess], cNominalSectors,
                    apszType[enmType], pszBasename, uSectorOffset);
    }
    else
    {
        RTStrPrintf(szExt, sizeof(szExt), "%s %llu %s \"%s\"",
                    apszAccess[enmAccess], cNominalSectors,
                    apszType[enmType], pszBasename);
    }
    cbDiff = strlen(szExt) + 1;

    /* Check for buffer overflow. */
    if (   (pDescriptor->cLines >= VMDK_DESCRIPTOR_LINES_MAX - 1)
        || (  pDescriptor->aLines[pDescriptor->cLines]
            - pDescriptor->aLines[0] > (ptrdiff_t)pDescriptor->cbDescAlloc - cbDiff))
    {
        if ((pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_SPLIT_2G)
            && !(pDescriptor->cLines >= VMDK_DESCRIPTOR_LINES_MAX - 1))
        {
            pImage->cbDescAlloc *= 2;
            pDescriptor->cbDescAlloc *= 2;
        }
        else
            return vdIfError(pImage->pIfError, VERR_BUFFER_OVERFLOW, RT_SRC_POS, N_("VMDK: descriptor too big in '%s'"), pImage->pszFilename);
    }

    for (unsigned i = pDescriptor->cLines + 1; i > uLast + 1; i--)
    {
        pDescriptor->aLines[i] = pDescriptor->aLines[i - 1];
        if (pDescriptor->aNextLines[i - 1])
            pDescriptor->aNextLines[i] = pDescriptor->aNextLines[i - 1] + 1;
        else
            pDescriptor->aNextLines[i] = 0;
    }
    uStart = uLast + 1;
    pDescriptor->aNextLines[uLast] = uStart;
    pDescriptor->aNextLines[uStart] = 0;
    pDescriptor->cLines++;
    pszTmp = pDescriptor->aLines[uStart];
    memmove(pszTmp + cbDiff, pszTmp,
            pDescriptor->aLines[pDescriptor->cLines] - pszTmp);
    memcpy(pDescriptor->aLines[uStart], szExt, cbDiff);
    for (unsigned i = uStart + 1; i <= pDescriptor->cLines; i++)
        pDescriptor->aLines[i] += cbDiff;

    /* Adjust starting line numbers of following descriptor sections. */
    if (uStart <= pDescriptor->uFirstDDB)
        pDescriptor->uFirstDDB++;

    pDescriptor->fDirty = true;
    return VINF_SUCCESS;
}

/**
 * Returns the value of the given key from the DDB as a string allocating
 * the necessary memory.
 *
 * @returns VBox status code.
 * @retval  VERR_VD_VMDK_VALUE_NOT_FOUND if the value could not be found.
 * @param   pImage          The VMDK image state.
 * @param   pDescriptor     The descriptor to fetch the value from.
 * @param   pszKey          The key to get the value from.
 * @param   ppszValue       Where to store the return value, use RTMemTmpFree to
 *                          free.
 */
static int vmdkDescDDBGetStr(PVMDKIMAGE pImage, PVMDKDESCRIPTOR pDescriptor,
                             const char *pszKey, char **ppszValue)
{
    const char *pszValue;
    char *pszValueUnquoted;

    if (!vmdkDescGetStr(pDescriptor, pDescriptor->uFirstDDB, pszKey,
                        &pszValue))
        return VERR_VD_VMDK_VALUE_NOT_FOUND;
    int rc = vmdkStringUnquote(pImage, pszValue, &pszValueUnquoted, NULL);
    if (RT_FAILURE(rc))
        return rc;
    *ppszValue = pszValueUnquoted;
    return rc;
}

static int vmdkDescDDBGetU32(PVMDKIMAGE pImage, PVMDKDESCRIPTOR pDescriptor,
                             const char *pszKey, uint32_t *puValue)
{
    const char *pszValue;
    char *pszValueUnquoted;

    if (!vmdkDescGetStr(pDescriptor, pDescriptor->uFirstDDB, pszKey,
                        &pszValue))
        return VERR_VD_VMDK_VALUE_NOT_FOUND;
    int rc = vmdkStringUnquote(pImage, pszValue, &pszValueUnquoted, NULL);
    if (RT_FAILURE(rc))
        return rc;
    rc = RTStrToUInt32Ex(pszValueUnquoted, NULL, 10, puValue);
    RTMemTmpFree(pszValueUnquoted);
    return rc;
}

static int vmdkDescDDBGetUuid(PVMDKIMAGE pImage, PVMDKDESCRIPTOR pDescriptor,
                              const char *pszKey, PRTUUID pUuid)
{
    const char *pszValue;
    char *pszValueUnquoted;

    if (!vmdkDescGetStr(pDescriptor, pDescriptor->uFirstDDB, pszKey,
                        &pszValue))
        return VERR_VD_VMDK_VALUE_NOT_FOUND;
    int rc = vmdkStringUnquote(pImage, pszValue, &pszValueUnquoted, NULL);
    if (RT_FAILURE(rc))
        return rc;
    rc = RTUuidFromStr(pUuid, pszValueUnquoted);
    RTMemTmpFree(pszValueUnquoted);
    return rc;
}

static int vmdkDescDDBSetStr(PVMDKIMAGE pImage, PVMDKDESCRIPTOR pDescriptor,
                             const char *pszKey, const char *pszVal)
{
    int rc;
    char *pszValQuoted;

    if (pszVal)
    {
        RTStrAPrintf(&pszValQuoted, "\"%s\"", pszVal);
        if (!pszValQuoted)
            return VERR_NO_STR_MEMORY;
    }
    else
        pszValQuoted = NULL;
    rc = vmdkDescSetStr(pImage, pDescriptor, pDescriptor->uFirstDDB, pszKey,
                        pszValQuoted);
    if (pszValQuoted)
        RTStrFree(pszValQuoted);
    return rc;
}

static int vmdkDescDDBSetUuid(PVMDKIMAGE pImage, PVMDKDESCRIPTOR pDescriptor,
                              const char *pszKey, PCRTUUID pUuid)
{
    char *pszUuid;

    RTStrAPrintf(&pszUuid, "\"%RTuuid\"", pUuid);
    if (!pszUuid)
        return VERR_NO_STR_MEMORY;
    int rc = vmdkDescSetStr(pImage, pDescriptor, pDescriptor->uFirstDDB, pszKey,
                            pszUuid);
    RTStrFree(pszUuid);
    return rc;
}

static int vmdkDescDDBSetU32(PVMDKIMAGE pImage, PVMDKDESCRIPTOR pDescriptor,
                             const char *pszKey, uint32_t uValue)
{
    char *pszValue;

    RTStrAPrintf(&pszValue, "\"%d\"", uValue);
    if (!pszValue)
        return VERR_NO_STR_MEMORY;
    int rc = vmdkDescSetStr(pImage, pDescriptor, pDescriptor->uFirstDDB, pszKey,
                            pszValue);
    RTStrFree(pszValue);
    return rc;
}

/**
 * Splits the descriptor data into individual lines checking for correct line
 * endings and descriptor size.
 *
 * @returns VBox status code.
 * @param   pImage          The image instance.
 * @param   pDesc           The descriptor.
 * @param   pszTmp          The raw descriptor data from the image.
 */
static int vmdkDescSplitLines(PVMDKIMAGE pImage, PVMDKDESCRIPTOR pDesc, char *pszTmp)
{
    unsigned cLine = 0;
    int rc = VINF_SUCCESS;

    while (   RT_SUCCESS(rc)
           && *pszTmp != '\0')
    {
        pDesc->aLines[cLine++] = pszTmp;
        if (cLine >= VMDK_DESCRIPTOR_LINES_MAX)
        {
            vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS, N_("VMDK: descriptor too big in '%s'"), pImage->pszFilename);
            rc = VERR_VD_VMDK_INVALID_HEADER;
            break;
        }

        while (*pszTmp != '\0' && *pszTmp != '\n')
        {
            if (*pszTmp == '\r')
            {
                if (*(pszTmp + 1) != '\n')
                {
                    rc = vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS, N_("VMDK: unsupported end of line in descriptor in '%s'"), pImage->pszFilename);
                    break;
                }
                else
                {
                    /* Get rid of CR character. */
                    *pszTmp = '\0';
                }
            }
            pszTmp++;
        }

        if (RT_FAILURE(rc))
            break;

        /* Get rid of LF character. */
        if (*pszTmp == '\n')
        {
            *pszTmp = '\0';
            pszTmp++;
        }
    }

    if (RT_SUCCESS(rc))
    {
        pDesc->cLines = cLine;
        /* Pointer right after the end of the used part of the buffer. */
        pDesc->aLines[cLine] = pszTmp;
    }

    return rc;
}

static int vmdkPreprocessDescriptor(PVMDKIMAGE pImage, char *pDescData,
                                    size_t cbDescData, PVMDKDESCRIPTOR pDescriptor)
{
    pDescriptor->cbDescAlloc = cbDescData;
    int rc = vmdkDescSplitLines(pImage, pDescriptor, pDescData);
    if (RT_SUCCESS(rc))
    {
        if (    strcmp(pDescriptor->aLines[0], "# Disk DescriptorFile")
            &&  strcmp(pDescriptor->aLines[0], "# Disk Descriptor File")
            &&  strcmp(pDescriptor->aLines[0], "#Disk Descriptor File")
            &&  strcmp(pDescriptor->aLines[0], "#Disk DescriptorFile"))
            rc = vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS,
                           N_("VMDK: descriptor does not start as expected in '%s'"), pImage->pszFilename);
        else
        {
            unsigned uLastNonEmptyLine = 0;

            /* Initialize those, because we need to be able to reopen an image. */
            pDescriptor->uFirstDesc = 0;
            pDescriptor->uFirstExtent = 0;
            pDescriptor->uFirstDDB = 0;
            for (unsigned i = 0; i < pDescriptor->cLines; i++)
            {
                if (*pDescriptor->aLines[i] != '#' && *pDescriptor->aLines[i] != '\0')
                {
                    if (    !strncmp(pDescriptor->aLines[i], "RW", 2)
                        ||  !strncmp(pDescriptor->aLines[i], "RDONLY", 6)
                        ||  !strncmp(pDescriptor->aLines[i], "NOACCESS", 8) )
                    {
                        /* An extent descriptor. */
                        if (!pDescriptor->uFirstDesc || pDescriptor->uFirstDDB)
                        {
                            /* Incorrect ordering of entries. */
                            rc = vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS,
                                           N_("VMDK: incorrect ordering of entries in descriptor in '%s'"), pImage->pszFilename);
                            break;
                        }
                        if (!pDescriptor->uFirstExtent)
                        {
                            pDescriptor->uFirstExtent = i;
                            uLastNonEmptyLine = 0;
                        }
                    }
                    else if (!strncmp(pDescriptor->aLines[i], "ddb.", 4))
                    {
                        /* A disk database entry. */
                        if (!pDescriptor->uFirstDesc || !pDescriptor->uFirstExtent)
                        {
                            /* Incorrect ordering of entries. */
                            rc = vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS,
                                           N_("VMDK: incorrect ordering of entries in descriptor in '%s'"), pImage->pszFilename);
                            break;
                        }
                        if (!pDescriptor->uFirstDDB)
                        {
                            pDescriptor->uFirstDDB = i;
                            uLastNonEmptyLine = 0;
                        }
                    }
                    else
                    {
                        /* A normal entry. */
                        if (pDescriptor->uFirstExtent || pDescriptor->uFirstDDB)
                        {
                            /* Incorrect ordering of entries. */
                            rc = vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS,
                                           N_("VMDK: incorrect ordering of entries in descriptor in '%s'"), pImage->pszFilename);
                            break;
                        }
                        if (!pDescriptor->uFirstDesc)
                        {
                            pDescriptor->uFirstDesc = i;
                            uLastNonEmptyLine = 0;
                        }
                    }
                    if (uLastNonEmptyLine)
                        pDescriptor->aNextLines[uLastNonEmptyLine] = i;
                    uLastNonEmptyLine = i;
                }
            }
        }
    }

    return rc;
}

static int vmdkDescSetPCHSGeometry(PVMDKIMAGE pImage,
                                   PCVDGEOMETRY pPCHSGeometry)
{
    int rc = vmdkDescDDBSetU32(pImage, &pImage->Descriptor,
                           VMDK_DDB_GEO_PCHS_CYLINDERS,
                           pPCHSGeometry->cCylinders);
    if (RT_FAILURE(rc))
        return rc;
    rc = vmdkDescDDBSetU32(pImage, &pImage->Descriptor,
                           VMDK_DDB_GEO_PCHS_HEADS,
                           pPCHSGeometry->cHeads);
    if (RT_FAILURE(rc))
        return rc;
    rc = vmdkDescDDBSetU32(pImage, &pImage->Descriptor,
                           VMDK_DDB_GEO_PCHS_SECTORS,
                           pPCHSGeometry->cSectors);
    return rc;
}

static int vmdkDescSetLCHSGeometry(PVMDKIMAGE pImage,
                                   PCVDGEOMETRY pLCHSGeometry)
{
    int rc = vmdkDescDDBSetU32(pImage, &pImage->Descriptor,
                           VMDK_DDB_GEO_LCHS_CYLINDERS,
                           pLCHSGeometry->cCylinders);
    if (RT_FAILURE(rc))
        return rc;
    rc = vmdkDescDDBSetU32(pImage, &pImage->Descriptor,
                           VMDK_DDB_GEO_LCHS_HEADS,

                           pLCHSGeometry->cHeads);
    if (RT_FAILURE(rc))
        return rc;
    rc = vmdkDescDDBSetU32(pImage, &pImage->Descriptor,
                           VMDK_DDB_GEO_LCHS_SECTORS,
                           pLCHSGeometry->cSectors);
    return rc;
}

static int vmdkCreateDescriptor(PVMDKIMAGE pImage, char *pDescData,
                                size_t cbDescData, PVMDKDESCRIPTOR pDescriptor)
{
    pDescriptor->uFirstDesc = 0;
    pDescriptor->uFirstExtent = 0;
    pDescriptor->uFirstDDB = 0;
    pDescriptor->cLines = 0;
    pDescriptor->cbDescAlloc = cbDescData;
    pDescriptor->fDirty = false;
    pDescriptor->aLines[pDescriptor->cLines] = pDescData;
    memset(pDescriptor->aNextLines, '\0', sizeof(pDescriptor->aNextLines));

    int rc = vmdkDescInitStr(pImage, pDescriptor, "# Disk DescriptorFile");
    if (RT_SUCCESS(rc))
        rc = vmdkDescInitStr(pImage, pDescriptor, "version=1");
    if (RT_SUCCESS(rc))
    {
        pDescriptor->uFirstDesc = pDescriptor->cLines - 1;
        rc = vmdkDescInitStr(pImage, pDescriptor, "");
    }
    if (RT_SUCCESS(rc))
        rc = vmdkDescInitStr(pImage, pDescriptor, "# Extent description");
    if (RT_SUCCESS(rc))
        rc = vmdkDescInitStr(pImage, pDescriptor, "NOACCESS 0 ZERO ");
    if (RT_SUCCESS(rc))
    {
        pDescriptor->uFirstExtent = pDescriptor->cLines - 1;
        rc = vmdkDescInitStr(pImage, pDescriptor, "");
    }
    if (RT_SUCCESS(rc))
    {
        /* The trailing space is created by VMware, too. */
        rc = vmdkDescInitStr(pImage, pDescriptor, "# The disk Data Base ");
    }
    if (RT_SUCCESS(rc))
        rc = vmdkDescInitStr(pImage, pDescriptor, "#DDB");
    if (RT_SUCCESS(rc))
        rc = vmdkDescInitStr(pImage, pDescriptor, "");
    if (RT_SUCCESS(rc))
        rc = vmdkDescInitStr(pImage, pDescriptor, "ddb.virtualHWVersion = \"4\"");
    if (RT_SUCCESS(rc))
    {
        pDescriptor->uFirstDDB = pDescriptor->cLines - 1;

        /* Now that the framework is in place, use the normal functions to insert
         * the remaining keys. */
        char szBuf[9];
        RTStrPrintf(szBuf, sizeof(szBuf), "%08x", RTRandU32());
        rc = vmdkDescSetStr(pImage, pDescriptor, pDescriptor->uFirstDesc,
                            "CID", szBuf);
    }
    if (RT_SUCCESS(rc))
        rc = vmdkDescSetStr(pImage, pDescriptor, pDescriptor->uFirstDesc,
                            "parentCID", "ffffffff");
    if (RT_SUCCESS(rc))
        rc = vmdkDescDDBSetStr(pImage, pDescriptor, "ddb.adapterType", "ide");

    return rc;
}

static int vmdkParseDescriptor(PVMDKIMAGE pImage, char *pDescData, size_t cbDescData)
{
    int rc;
    unsigned cExtents;
    unsigned uLine;
    unsigned i;

    rc = vmdkPreprocessDescriptor(pImage, pDescData, cbDescData,
                                  &pImage->Descriptor);
    if (RT_FAILURE(rc))
        return rc;

    /* Check version, must be 1. */
    uint32_t uVersion;
    rc = vmdkDescBaseGetU32(&pImage->Descriptor, "version", &uVersion);
    if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: error finding key 'version' in descriptor in '%s'"), pImage->pszFilename);
    if (uVersion != 1)
        return vdIfError(pImage->pIfError, VERR_VD_VMDK_UNSUPPORTED_VERSION, RT_SRC_POS, N_("VMDK: unsupported format version in descriptor in '%s'"), pImage->pszFilename);

    /* Get image creation type and determine image flags. */
    char *pszCreateType = NULL;   /* initialized to make gcc shut up */
    rc = vmdkDescBaseGetStr(pImage, &pImage->Descriptor, "createType",
                            &pszCreateType);
    if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: cannot get image type from descriptor in '%s'"), pImage->pszFilename);
    if (    !strcmp(pszCreateType, "twoGbMaxExtentSparse")
        ||  !strcmp(pszCreateType, "twoGbMaxExtentFlat"))
        pImage->uImageFlags |= VD_VMDK_IMAGE_FLAGS_SPLIT_2G;
    else if (   !strcmp(pszCreateType, "partitionedDevice")
             || !strcmp(pszCreateType, "fullDevice"))
        pImage->uImageFlags |= VD_VMDK_IMAGE_FLAGS_RAWDISK;
    else if (!strcmp(pszCreateType, "streamOptimized"))
        pImage->uImageFlags |= VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED;
    else if (!strcmp(pszCreateType, "vmfs"))
        pImage->uImageFlags |= VD_IMAGE_FLAGS_FIXED | VD_VMDK_IMAGE_FLAGS_ESX;
    RTMemTmpFree(pszCreateType);

    /* Count the number of extent config entries. */
    for (uLine = pImage->Descriptor.uFirstExtent, cExtents = 0;
         uLine != 0;
         uLine = pImage->Descriptor.aNextLines[uLine], cExtents++)
        /* nothing */;

    if (!pImage->pDescData && cExtents != 1)
    {
        /* Monolithic image, must have only one extent (already opened). */
        return vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS, N_("VMDK: monolithic image may only have one extent in '%s'"), pImage->pszFilename);
    }

    if (pImage->pDescData)
    {
        /* Non-monolithic image, extents need to be allocated. */
        rc = vmdkCreateExtents(pImage, cExtents);
        if (RT_FAILURE(rc))
            return rc;
    }

    for (i = 0, uLine = pImage->Descriptor.uFirstExtent;
         i < cExtents; i++, uLine = pImage->Descriptor.aNextLines[uLine])
    {
        char *pszLine = pImage->Descriptor.aLines[uLine];

        /* Access type of the extent. */
        if (!strncmp(pszLine, "RW", 2))
        {
            pImage->pExtents[i].enmAccess = VMDKACCESS_READWRITE;
            pszLine += 2;
        }
        else if (!strncmp(pszLine, "RDONLY", 6))
        {
            pImage->pExtents[i].enmAccess = VMDKACCESS_READONLY;
            pszLine += 6;
        }
        else if (!strncmp(pszLine, "NOACCESS", 8))
        {
            pImage->pExtents[i].enmAccess = VMDKACCESS_NOACCESS;
            pszLine += 8;
        }
        else
            return vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS, N_("VMDK: parse error in extent description in '%s'"), pImage->pszFilename);
        if (*pszLine++ != ' ')
            return vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS, N_("VMDK: parse error in extent description in '%s'"), pImage->pszFilename);

        /* Nominal size of the extent. */
        rc = RTStrToUInt64Ex(pszLine, &pszLine, 10,
                             &pImage->pExtents[i].cNominalSectors);
        if (RT_FAILURE(rc))
            return vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS, N_("VMDK: parse error in extent description in '%s'"), pImage->pszFilename);
        if (*pszLine++ != ' ')
            return vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS, N_("VMDK: parse error in extent description in '%s'"), pImage->pszFilename);

        /* Type of the extent. */
        if (!strncmp(pszLine, "SPARSE", 6))
        {
            pImage->pExtents[i].enmType = VMDKETYPE_HOSTED_SPARSE;
            pszLine += 6;
        }
        else if (!strncmp(pszLine, "FLAT", 4))
        {
            pImage->pExtents[i].enmType = VMDKETYPE_FLAT;
            pszLine += 4;
        }
        else if (!strncmp(pszLine, "ZERO", 4))
        {
            pImage->pExtents[i].enmType = VMDKETYPE_ZERO;
            pszLine += 4;
        }
        else if (!strncmp(pszLine, "VMFS", 4))
        {
            pImage->pExtents[i].enmType = VMDKETYPE_VMFS;
            pszLine += 4;
        }
        else
            return vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS, N_("VMDK: parse error in extent description in '%s'"), pImage->pszFilename);

        if (pImage->pExtents[i].enmType == VMDKETYPE_ZERO)
        {
            /* This one has no basename or offset. */
            if (*pszLine == ' ')
                pszLine++;
            if (*pszLine != '\0')
                return vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS, N_("VMDK: parse error in extent description in '%s'"), pImage->pszFilename);
            pImage->pExtents[i].pszBasename = NULL;
        }
        else
        {
            /* All other extent types have basename and optional offset. */
            if (*pszLine++ != ' ')
                return vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS, N_("VMDK: parse error in extent description in '%s'"), pImage->pszFilename);

            /* Basename of the image. Surrounded by quotes. */
            char *pszBasename;
            rc = vmdkStringUnquote(pImage, pszLine, &pszBasename, &pszLine);
            if (RT_FAILURE(rc))
                return rc;
            pImage->pExtents[i].pszBasename = pszBasename;
            if (*pszLine == ' ')
            {
                pszLine++;
                if (*pszLine != '\0')
                {
                    /* Optional offset in extent specified. */
                    rc = RTStrToUInt64Ex(pszLine, &pszLine, 10,
                                         &pImage->pExtents[i].uSectorOffset);
                    if (RT_FAILURE(rc))
                        return vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS, N_("VMDK: parse error in extent description in '%s'"), pImage->pszFilename);
                }
            }

            if (*pszLine != '\0')
                return vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS, N_("VMDK: parse error in extent description in '%s'"), pImage->pszFilename);
        }
    }

    /* Determine PCHS geometry (autogenerate if necessary). */
    rc = vmdkDescDDBGetU32(pImage, &pImage->Descriptor,
                           VMDK_DDB_GEO_PCHS_CYLINDERS,
                           &pImage->PCHSGeometry.cCylinders);
    if (rc == VERR_VD_VMDK_VALUE_NOT_FOUND)
        pImage->PCHSGeometry.cCylinders = 0;
    else if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: error getting PCHS geometry from extent description in '%s'"), pImage->pszFilename);
    rc = vmdkDescDDBGetU32(pImage, &pImage->Descriptor,
                           VMDK_DDB_GEO_PCHS_HEADS,
                           &pImage->PCHSGeometry.cHeads);
    if (rc == VERR_VD_VMDK_VALUE_NOT_FOUND)
        pImage->PCHSGeometry.cHeads = 0;
    else if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: error getting PCHS geometry from extent description in '%s'"), pImage->pszFilename);
    rc = vmdkDescDDBGetU32(pImage, &pImage->Descriptor,
                           VMDK_DDB_GEO_PCHS_SECTORS,
                           &pImage->PCHSGeometry.cSectors);
    if (rc == VERR_VD_VMDK_VALUE_NOT_FOUND)
        pImage->PCHSGeometry.cSectors = 0;
    else if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: error getting PCHS geometry from extent description in '%s'"), pImage->pszFilename);
    if (    pImage->PCHSGeometry.cCylinders == 0
        ||  pImage->PCHSGeometry.cHeads == 0
        ||  pImage->PCHSGeometry.cHeads > 16
        ||  pImage->PCHSGeometry.cSectors == 0
        ||  pImage->PCHSGeometry.cSectors > 63)
    {
        /* Mark PCHS geometry as not yet valid (can't do the calculation here
         * as the total image size isn't known yet). */
        pImage->PCHSGeometry.cCylinders = 0;
        pImage->PCHSGeometry.cHeads = 16;
        pImage->PCHSGeometry.cSectors = 63;
    }

    /* Determine LCHS geometry (set to 0 if not specified). */
    rc = vmdkDescDDBGetU32(pImage, &pImage->Descriptor,
                           VMDK_DDB_GEO_LCHS_CYLINDERS,
                           &pImage->LCHSGeometry.cCylinders);
    if (rc == VERR_VD_VMDK_VALUE_NOT_FOUND)
        pImage->LCHSGeometry.cCylinders = 0;
    else if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: error getting LCHS geometry from extent description in '%s'"), pImage->pszFilename);
    rc = vmdkDescDDBGetU32(pImage, &pImage->Descriptor,
                           VMDK_DDB_GEO_LCHS_HEADS,
                           &pImage->LCHSGeometry.cHeads);
    if (rc == VERR_VD_VMDK_VALUE_NOT_FOUND)
        pImage->LCHSGeometry.cHeads = 0;
    else if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: error getting LCHS geometry from extent description in '%s'"), pImage->pszFilename);
    rc = vmdkDescDDBGetU32(pImage, &pImage->Descriptor,
                           VMDK_DDB_GEO_LCHS_SECTORS,
                           &pImage->LCHSGeometry.cSectors);
    if (rc == VERR_VD_VMDK_VALUE_NOT_FOUND)
        pImage->LCHSGeometry.cSectors = 0;
    else if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: error getting LCHS geometry from extent description in '%s'"), pImage->pszFilename);
    if (    pImage->LCHSGeometry.cCylinders == 0
        ||  pImage->LCHSGeometry.cHeads == 0
        ||  pImage->LCHSGeometry.cSectors == 0)
    {
        pImage->LCHSGeometry.cCylinders = 0;
        pImage->LCHSGeometry.cHeads = 0;
        pImage->LCHSGeometry.cSectors = 0;
    }

    /* Get image UUID. */
    rc = vmdkDescDDBGetUuid(pImage, &pImage->Descriptor, VMDK_DDB_IMAGE_UUID,
                            &pImage->ImageUuid);
    if (rc == VERR_VD_VMDK_VALUE_NOT_FOUND)
    {
        /* Image without UUID. Probably created by VMware and not yet used
         * by VirtualBox. Can only be added for images opened in read/write
         * mode, so don't bother producing a sensible UUID otherwise. */
        if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
            RTUuidClear(&pImage->ImageUuid);
        else
        {
            rc = RTUuidCreate(&pImage->ImageUuid);
            if (RT_FAILURE(rc))
                return rc;
            rc = vmdkDescDDBSetUuid(pImage, &pImage->Descriptor,
                                    VMDK_DDB_IMAGE_UUID, &pImage->ImageUuid);
            if (RT_FAILURE(rc))
                return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: error storing image UUID in descriptor in '%s'"), pImage->pszFilename);
        }
    }
    else if (RT_FAILURE(rc))
        return rc;

    /* Get image modification UUID. */
    rc = vmdkDescDDBGetUuid(pImage, &pImage->Descriptor,
                            VMDK_DDB_MODIFICATION_UUID,
                            &pImage->ModificationUuid);
    if (rc == VERR_VD_VMDK_VALUE_NOT_FOUND)
    {
        /* Image without UUID. Probably created by VMware and not yet used
         * by VirtualBox. Can only be added for images opened in read/write
         * mode, so don't bother producing a sensible UUID otherwise. */
        if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
            RTUuidClear(&pImage->ModificationUuid);
        else
        {
            rc = RTUuidCreate(&pImage->ModificationUuid);
            if (RT_FAILURE(rc))
                return rc;
            rc = vmdkDescDDBSetUuid(pImage, &pImage->Descriptor,
                                    VMDK_DDB_MODIFICATION_UUID,
                                    &pImage->ModificationUuid);
            if (RT_FAILURE(rc))
                return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: error storing image modification UUID in descriptor in '%s'"), pImage->pszFilename);
        }
    }
    else if (RT_FAILURE(rc))
        return rc;

    /* Get UUID of parent image. */
    rc = vmdkDescDDBGetUuid(pImage, &pImage->Descriptor, VMDK_DDB_PARENT_UUID,
                            &pImage->ParentUuid);
    if (rc == VERR_VD_VMDK_VALUE_NOT_FOUND)
    {
        /* Image without UUID. Probably created by VMware and not yet used
         * by VirtualBox. Can only be added for images opened in read/write
         * mode, so don't bother producing a sensible UUID otherwise. */
        if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
            RTUuidClear(&pImage->ParentUuid);
        else
        {
            rc = RTUuidClear(&pImage->ParentUuid);
            if (RT_FAILURE(rc))
                return rc;
            rc = vmdkDescDDBSetUuid(pImage, &pImage->Descriptor,
                                    VMDK_DDB_PARENT_UUID, &pImage->ParentUuid);
            if (RT_FAILURE(rc))
                return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: error storing parent UUID in descriptor in '%s'"), pImage->pszFilename);
        }
    }
    else if (RT_FAILURE(rc))
        return rc;

    /* Get parent image modification UUID. */
    rc = vmdkDescDDBGetUuid(pImage, &pImage->Descriptor,
                            VMDK_DDB_PARENT_MODIFICATION_UUID,
                            &pImage->ParentModificationUuid);
    if (rc == VERR_VD_VMDK_VALUE_NOT_FOUND)
    {
        /* Image without UUID. Probably created by VMware and not yet used
         * by VirtualBox. Can only be added for images opened in read/write
         * mode, so don't bother producing a sensible UUID otherwise. */
        if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
            RTUuidClear(&pImage->ParentModificationUuid);
        else
        {
            RTUuidClear(&pImage->ParentModificationUuid);
            rc = vmdkDescDDBSetUuid(pImage, &pImage->Descriptor,
                                    VMDK_DDB_PARENT_MODIFICATION_UUID,
                                    &pImage->ParentModificationUuid);
            if (RT_FAILURE(rc))
                return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: error storing parent modification UUID in descriptor in '%s'"), pImage->pszFilename);
        }
    }
    else if (RT_FAILURE(rc))
        return rc;

    return VINF_SUCCESS;
}

/**
 * Internal : Prepares the descriptor to write to the image.
 */
static int vmdkDescriptorPrepare(PVMDKIMAGE pImage, uint64_t cbLimit,
                                 void **ppvData, size_t *pcbData)
{
    int rc = VINF_SUCCESS;

    /*
     * Allocate temporary descriptor buffer.
     * In case there is no limit allocate a default
     * and increase if required.
     */
    size_t cbDescriptor = cbLimit ? cbLimit : 4 * _1K;
    char *pszDescriptor = (char *)RTMemAllocZ(cbDescriptor);
    size_t offDescriptor = 0;

    if (!pszDescriptor)
        return VERR_NO_MEMORY;

    for (unsigned i = 0; i < pImage->Descriptor.cLines; i++)
    {
        const char *psz = pImage->Descriptor.aLines[i];
        size_t cb = strlen(psz);

        /*
         * Increase the descriptor if there is no limit and
         * there is not enough room left for this line.
         */
        if (offDescriptor + cb + 1 > cbDescriptor)
        {
            if (cbLimit)
            {
                rc = vdIfError(pImage->pIfError, VERR_BUFFER_OVERFLOW, RT_SRC_POS, N_("VMDK: descriptor too long in '%s'"), pImage->pszFilename);
                break;
            }
            else
            {
                char *pszDescriptorNew = NULL;
                LogFlow(("Increasing descriptor cache\n"));

                pszDescriptorNew = (char *)RTMemRealloc(pszDescriptor, cbDescriptor + cb + 4 * _1K);
                if (!pszDescriptorNew)
                {
                    rc = VERR_NO_MEMORY;
                    break;
                }
                pszDescriptor = pszDescriptorNew;
                cbDescriptor += cb + 4 * _1K;
            }
        }

        if (cb > 0)
        {
            memcpy(pszDescriptor + offDescriptor, psz, cb);
            offDescriptor += cb;
        }

        memcpy(pszDescriptor + offDescriptor, "\n", 1);
        offDescriptor++;
    }

    if (RT_SUCCESS(rc))
    {
        *ppvData = pszDescriptor;
        *pcbData = offDescriptor;
    }
    else if (pszDescriptor)
        RTMemFree(pszDescriptor);

    return rc;
}

/**
 * Internal: write/update the descriptor part of the image.
 */
static int vmdkWriteDescriptor(PVMDKIMAGE pImage, PVDIOCTX pIoCtx)
{
    int rc = VINF_SUCCESS;
    uint64_t cbLimit;
    uint64_t uOffset;
    PVMDKFILE pDescFile;
    void *pvDescriptor = NULL;
    size_t cbDescriptor;

    if (pImage->pDescData)
    {
        /* Separate descriptor file. */
        uOffset = 0;
        cbLimit = 0;
        pDescFile = pImage->pFile;
    }
    else
    {
        /* Embedded descriptor file. */
        uOffset = VMDK_SECTOR2BYTE(pImage->pExtents[0].uDescriptorSector);
        cbLimit = VMDK_SECTOR2BYTE(pImage->pExtents[0].cDescriptorSectors);
        pDescFile = pImage->pExtents[0].pFile;
    }
    /* Bail out if there is no file to write to. */
    if (pDescFile == NULL)
        return VERR_INVALID_PARAMETER;

    rc = vmdkDescriptorPrepare(pImage, cbLimit, &pvDescriptor, &cbDescriptor);
    if (RT_SUCCESS(rc))
    {
        rc = vdIfIoIntFileWriteMeta(pImage->pIfIo, pDescFile->pStorage,
                                    uOffset, pvDescriptor,
                                    cbLimit ? cbLimit : cbDescriptor,
                                    pIoCtx, NULL, NULL);
        if (   RT_FAILURE(rc)
            && rc != VERR_VD_ASYNC_IO_IN_PROGRESS)
            rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: error writing descriptor in '%s'"), pImage->pszFilename);
    }

    if (RT_SUCCESS(rc) && !cbLimit)
    {
        rc = vdIfIoIntFileSetSize(pImage->pIfIo, pDescFile->pStorage, cbDescriptor);
        if (RT_FAILURE(rc))
            rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: error truncating descriptor in '%s'"), pImage->pszFilename);
    }

    if (RT_SUCCESS(rc))
        pImage->Descriptor.fDirty = false;

    if (pvDescriptor)
        RTMemFree(pvDescriptor);
    return rc;

}

/**
 * Internal: validate the consistency check values in a binary header.
 */
static int vmdkValidateHeader(PVMDKIMAGE pImage, PVMDKEXTENT pExtent, const SparseExtentHeader *pHeader)
{
    int rc = VINF_SUCCESS;
    if (RT_LE2H_U32(pHeader->magicNumber) != VMDK_SPARSE_MAGICNUMBER)
    {
        rc = vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS, N_("VMDK: incorrect magic in sparse extent header in '%s'"), pExtent->pszFullname);
        return rc;
    }
    if (RT_LE2H_U32(pHeader->version) != 1 && RT_LE2H_U32(pHeader->version) != 3)
    {
        rc = vdIfError(pImage->pIfError, VERR_VD_VMDK_UNSUPPORTED_VERSION, RT_SRC_POS, N_("VMDK: incorrect version in sparse extent header in '%s', not a VMDK 1.0/1.1 conforming file"), pExtent->pszFullname);
        return rc;
    }
    if (    (RT_LE2H_U32(pHeader->flags) & 1)
        &&  (   pHeader->singleEndLineChar != '\n'
             || pHeader->nonEndLineChar != ' '
             || pHeader->doubleEndLineChar1 != '\r'
             || pHeader->doubleEndLineChar2 != '\n') )
    {
        rc = vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS, N_("VMDK: corrupted by CR/LF translation in '%s'"), pExtent->pszFullname);
        return rc;
    }
    if (RT_LE2H_U64(pHeader->descriptorSize) > VMDK_SPARSE_DESCRIPTOR_SIZE_MAX)
    {
        rc = vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS, N_("VMDK: descriptor size out of bounds (%llu vs %llu) '%s'"),
                       pExtent->pszFullname, RT_LE2H_U64(pHeader->descriptorSize), VMDK_SPARSE_DESCRIPTOR_SIZE_MAX);
        return rc;
    }
    return rc;
}

/**
 * Internal: read metadata belonging to an extent with binary header, i.e.
 * as found in monolithic files.
 */
static int vmdkReadBinaryMetaExtent(PVMDKIMAGE pImage, PVMDKEXTENT pExtent,
                                    bool fMagicAlreadyRead)
{
    SparseExtentHeader Header;
    int rc;

    if (!fMagicAlreadyRead)
        rc = vdIfIoIntFileReadSync(pImage->pIfIo, pExtent->pFile->pStorage, 0,
                                   &Header, sizeof(Header));
    else
    {
        Header.magicNumber = RT_H2LE_U32(VMDK_SPARSE_MAGICNUMBER);
        rc = vdIfIoIntFileReadSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                   RT_UOFFSETOF(SparseExtentHeader, version),
                                   &Header.version,
                                     sizeof(Header)
                                   - RT_UOFFSETOF(SparseExtentHeader, version));
    }

    if (RT_SUCCESS(rc))
    {
        rc = vmdkValidateHeader(pImage, pExtent, &Header);
        if (RT_SUCCESS(rc))
        {
            uint64_t cbFile = 0;

            if (    (RT_LE2H_U32(Header.flags) & RT_BIT(17))
                &&  RT_LE2H_U64(Header.gdOffset) == VMDK_GD_AT_END)
                pExtent->fFooter = true;

            if (   !(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
                || (   pExtent->fFooter
                    && !(pImage->uOpenFlags & VD_OPEN_FLAGS_SEQUENTIAL)))
            {
                rc = vdIfIoIntFileGetSize(pImage->pIfIo, pExtent->pFile->pStorage, &cbFile);
                if (RT_FAILURE(rc))
                    rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: cannot get size of '%s'"), pExtent->pszFullname);
            }

            if (RT_SUCCESS(rc))
            {
                if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
                    pExtent->uAppendPosition = RT_ALIGN_64(cbFile, 512);

                if (   pExtent->fFooter
                    && (   !(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
                        || !(pImage->uOpenFlags & VD_OPEN_FLAGS_SEQUENTIAL)))
                {
                    /* Read the footer, which comes before the end-of-stream marker. */
                    rc = vdIfIoIntFileReadSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                               cbFile - 2*512, &Header,
                                               sizeof(Header));
                    if (RT_FAILURE(rc))
                    {
                        vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: error reading extent footer in '%s'"), pExtent->pszFullname);
                        rc = VERR_VD_VMDK_INVALID_HEADER;
                    }

                    if (RT_SUCCESS(rc))
                        rc = vmdkValidateHeader(pImage, pExtent, &Header);
                    /* Prohibit any writes to this extent. */
                    pExtent->uAppendPosition = 0;
                }

                if (RT_SUCCESS(rc))
                {
                    pExtent->uVersion           = RT_LE2H_U32(Header.version);
                    pExtent->enmType            = VMDKETYPE_HOSTED_SPARSE; /* Just dummy value, changed later. */
                    pExtent->cSectors           = RT_LE2H_U64(Header.capacity);
                    pExtent->cSectorsPerGrain   = RT_LE2H_U64(Header.grainSize);
                    pExtent->uDescriptorSector  = RT_LE2H_U64(Header.descriptorOffset);
                    pExtent->cDescriptorSectors = RT_LE2H_U64(Header.descriptorSize);
                    pExtent->cGTEntries         = RT_LE2H_U32(Header.numGTEsPerGT);
                    pExtent->cOverheadSectors   = RT_LE2H_U64(Header.overHead);
                    pExtent->fUncleanShutdown   = !!Header.uncleanShutdown;
                    pExtent->uCompression       = RT_LE2H_U16(Header.compressAlgorithm);
                    if (RT_LE2H_U32(Header.flags) & RT_BIT(1))
                    {
                        pExtent->uSectorRGD     = RT_LE2H_U64(Header.rgdOffset);
                        pExtent->uSectorGD      = RT_LE2H_U64(Header.gdOffset);
                    }
                    else
                    {
                        pExtent->uSectorGD      = RT_LE2H_U64(Header.gdOffset);
                        pExtent->uSectorRGD     = 0;
                    }

                    if (pExtent->uDescriptorSector && !pExtent->cDescriptorSectors)
                        rc = vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS,
                                       N_("VMDK: inconsistent embedded descriptor config in '%s'"), pExtent->pszFullname);

                    if (   RT_SUCCESS(rc)
                        && (   pExtent->uSectorGD == VMDK_GD_AT_END
                            || pExtent->uSectorRGD == VMDK_GD_AT_END)
                        && (   !(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
                            || !(pImage->uOpenFlags & VD_OPEN_FLAGS_SEQUENTIAL)))
                        rc = vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS,
                                       N_("VMDK: cannot resolve grain directory offset in '%s'"), pExtent->pszFullname);

                    if (RT_SUCCESS(rc))
                    {
                        uint64_t cSectorsPerGDE = pExtent->cGTEntries * pExtent->cSectorsPerGrain;
                        if (!cSectorsPerGDE || cSectorsPerGDE > UINT32_MAX)
                            rc = vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS,
                                           N_("VMDK: incorrect grain directory size in '%s'"), pExtent->pszFullname);
                        else
                        {
                            pExtent->cSectorsPerGDE = cSectorsPerGDE;
                            pExtent->cGDEntries = (pExtent->cSectors + cSectorsPerGDE - 1) / cSectorsPerGDE;

                            /* Fix up the number of descriptor sectors, as some flat images have
                             * really just one, and this causes failures when inserting the UUID
                             * values and other extra information. */
                            if (pExtent->cDescriptorSectors != 0 && pExtent->cDescriptorSectors < 4)
                            {
                                /* Do it the easy way - just fix it for flat images which have no
                                 * other complicated metadata which needs space too. */
                                if (    pExtent->uDescriptorSector + 4 < pExtent->cOverheadSectors
                                    &&  pExtent->cGTEntries * pExtent->cGDEntries == 0)
                                    pExtent->cDescriptorSectors = 4;
                            }
                        }
                    }
                }
            }
        }
    }
    else
    {
        vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: error reading extent header in '%s'"), pExtent->pszFullname);
        rc = VERR_VD_VMDK_INVALID_HEADER;
    }

    if (RT_FAILURE(rc))
        vmdkFreeExtentData(pImage, pExtent, false);

    return rc;
}

/**
 * Internal: read additional metadata belonging to an extent. For those
 * extents which have no additional metadata just verify the information.
 */
static int vmdkReadMetaExtent(PVMDKIMAGE pImage, PVMDKEXTENT pExtent)
{
    int rc = VINF_SUCCESS;

/* disabled the check as there are too many truncated vmdk images out there */
#ifdef VBOX_WITH_VMDK_STRICT_SIZE_CHECK
    uint64_t cbExtentSize;
    /* The image must be a multiple of a sector in size and contain the data
     * area (flat images only). If not, it means the image is at least
     * truncated, or even seriously garbled. */
    rc = vdIfIoIntFileGetSize(pImage->pIfIo, pExtent->pFile->pStorage, &cbExtentSize);
    if (RT_FAILURE(rc))
        rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: error getting size in '%s'"), pExtent->pszFullname);
    else if (    cbExtentSize != RT_ALIGN_64(cbExtentSize, 512)
        &&  (pExtent->enmType != VMDKETYPE_FLAT || pExtent->cNominalSectors + pExtent->uSectorOffset > VMDK_BYTE2SECTOR(cbExtentSize)))
        rc = vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS,
                       N_("VMDK: file size is not a multiple of 512 in '%s', file is truncated or otherwise garbled"), pExtent->pszFullname);
#endif /* VBOX_WITH_VMDK_STRICT_SIZE_CHECK */
    if (   RT_SUCCESS(rc)
        && pExtent->enmType == VMDKETYPE_HOSTED_SPARSE)
    {
        /* The spec says that this must be a power of two and greater than 8,
         * but probably they meant not less than 8. */
        if (    (pExtent->cSectorsPerGrain & (pExtent->cSectorsPerGrain - 1))
            ||  pExtent->cSectorsPerGrain < 8)
            rc = vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS,
                           N_("VMDK: invalid extent grain size %u in '%s'"), pExtent->cSectorsPerGrain, pExtent->pszFullname);
        else
        {
            /* This code requires that a grain table must hold a power of two multiple
             * of the number of entries per GT cache entry. */
            if (    (pExtent->cGTEntries & (pExtent->cGTEntries - 1))
                ||  pExtent->cGTEntries < VMDK_GT_CACHELINE_SIZE)
                rc = vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS,
                               N_("VMDK: grain table cache size problem in '%s'"), pExtent->pszFullname);
            else
            {
                rc = vmdkAllocStreamBuffers(pImage, pExtent);
                if (RT_SUCCESS(rc))
                {
                    /* Prohibit any writes to this streamOptimized extent. */
                    if (pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED)
                        pExtent->uAppendPosition = 0;

                    if (   !(pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED)
                        || !(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
                        || !(pImage->uOpenFlags & VD_OPEN_FLAGS_SEQUENTIAL))
                        rc = vmdkReadGrainDirectory(pImage, pExtent);
                    else
                    {
                        pExtent->uGrainSectorAbs = pExtent->cOverheadSectors;
                        pExtent->cbGrainStreamRead = 0;
                    }
                }
            }
        }
    }

    if (RT_FAILURE(rc))
        vmdkFreeExtentData(pImage, pExtent, false);

    return rc;
}

/**
 * Internal: write/update the metadata for a sparse extent.
 */
static int vmdkWriteMetaSparseExtent(PVMDKIMAGE pImage, PVMDKEXTENT pExtent,
                                     uint64_t uOffset, PVDIOCTX pIoCtx)
{
    SparseExtentHeader Header;

    memset(&Header, '\0', sizeof(Header));
    Header.magicNumber = RT_H2LE_U32(VMDK_SPARSE_MAGICNUMBER);
    Header.version = RT_H2LE_U32(pExtent->uVersion);
    Header.flags = RT_H2LE_U32(RT_BIT(0));
    if (pExtent->pRGD)
        Header.flags |= RT_H2LE_U32(RT_BIT(1));
    if (pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED)
        Header.flags |= RT_H2LE_U32(RT_BIT(16) | RT_BIT(17));
    Header.capacity = RT_H2LE_U64(pExtent->cSectors);
    Header.grainSize = RT_H2LE_U64(pExtent->cSectorsPerGrain);
    Header.descriptorOffset = RT_H2LE_U64(pExtent->uDescriptorSector);
    Header.descriptorSize = RT_H2LE_U64(pExtent->cDescriptorSectors);
    Header.numGTEsPerGT = RT_H2LE_U32(pExtent->cGTEntries);
    if (pExtent->fFooter && uOffset == 0)
    {
        if (pExtent->pRGD)
        {
            Assert(pExtent->uSectorRGD);
            Header.rgdOffset = RT_H2LE_U64(VMDK_GD_AT_END);
            Header.gdOffset = RT_H2LE_U64(VMDK_GD_AT_END);
        }
        else
            Header.gdOffset = RT_H2LE_U64(VMDK_GD_AT_END);
    }
    else
    {
        if (pExtent->pRGD)
        {
            Assert(pExtent->uSectorRGD);
            Header.rgdOffset = RT_H2LE_U64(pExtent->uSectorRGD);
            Header.gdOffset = RT_H2LE_U64(pExtent->uSectorGD);
        }
        else
            Header.gdOffset = RT_H2LE_U64(pExtent->uSectorGD);
    }
    Header.overHead = RT_H2LE_U64(pExtent->cOverheadSectors);
    Header.uncleanShutdown = pExtent->fUncleanShutdown;
    Header.singleEndLineChar = '\n';
    Header.nonEndLineChar = ' ';
    Header.doubleEndLineChar1 = '\r';
    Header.doubleEndLineChar2 = '\n';
    Header.compressAlgorithm = RT_H2LE_U16(pExtent->uCompression);

    int rc = vdIfIoIntFileWriteMeta(pImage->pIfIo, pExtent->pFile->pStorage,
                                    uOffset, &Header, sizeof(Header),
                                    pIoCtx, NULL, NULL);
    if (RT_FAILURE(rc) && (rc != VERR_VD_ASYNC_IO_IN_PROGRESS))
        rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: error writing extent header in '%s'"), pExtent->pszFullname);
    return rc;
}

/**
 * Internal: free the buffers used for streamOptimized images.
 */
static void vmdkFreeStreamBuffers(PVMDKEXTENT pExtent)
{
    if (pExtent->pvCompGrain)
    {
        RTMemFree(pExtent->pvCompGrain);
        pExtent->pvCompGrain = NULL;
    }
    if (pExtent->pvGrain)
    {
        RTMemFree(pExtent->pvGrain);
        pExtent->pvGrain = NULL;
    }
}

/**
 * Internal: free the memory used by the extent data structure, optionally
 * deleting the referenced files.
 *
 * @returns VBox status code.
 * @param   pImage    Pointer to the image instance data.
 * @param   pExtent   The extent to free.
 * @param   fDelete   Flag whether to delete the backing storage.
 */
static int vmdkFreeExtentData(PVMDKIMAGE pImage, PVMDKEXTENT pExtent,
                              bool fDelete)
{
    int rc = VINF_SUCCESS;

    vmdkFreeGrainDirectory(pExtent);
    if (pExtent->pDescData)
    {
        RTMemFree(pExtent->pDescData);
        pExtent->pDescData = NULL;
    }
    if (pExtent->pFile != NULL)
    {
        /* Do not delete raw extents, these have full and base names equal. */
        rc = vmdkFileClose(pImage, &pExtent->pFile,
                              fDelete
                           && pExtent->pszFullname
                           && pExtent->pszBasename
                           && strcmp(pExtent->pszFullname, pExtent->pszBasename));
    }
    if (pExtent->pszBasename)
    {
        RTMemTmpFree((void *)pExtent->pszBasename);
        pExtent->pszBasename = NULL;
    }
    if (pExtent->pszFullname)
    {
        RTStrFree((char *)(void *)pExtent->pszFullname);
        pExtent->pszFullname = NULL;
    }
    vmdkFreeStreamBuffers(pExtent);

    return rc;
}

/**
 * Internal: allocate grain table cache if necessary for this image.
 */
static int vmdkAllocateGrainTableCache(PVMDKIMAGE pImage)
{
    PVMDKEXTENT pExtent;

    /* Allocate grain table cache if any sparse extent is present. */
    for (unsigned i = 0; i < pImage->cExtents; i++)
    {
        pExtent = &pImage->pExtents[i];
        if (pExtent->enmType == VMDKETYPE_HOSTED_SPARSE)
        {
            /* Allocate grain table cache. */
            pImage->pGTCache = (PVMDKGTCACHE)RTMemAllocZ(sizeof(VMDKGTCACHE));
            if (!pImage->pGTCache)
                return VERR_NO_MEMORY;
            for (unsigned j = 0; j < VMDK_GT_CACHE_SIZE; j++)
            {
                PVMDKGTCACHEENTRY pGCE = &pImage->pGTCache->aGTCache[j];
                pGCE->uExtent = UINT32_MAX;
            }
            pImage->pGTCache->cEntries = VMDK_GT_CACHE_SIZE;
            break;
        }
    }

    return VINF_SUCCESS;
}

/**
 * Internal: allocate the given number of extents.
 */
static int vmdkCreateExtents(PVMDKIMAGE pImage, unsigned cExtents)
{
    int rc = VINF_SUCCESS;
    PVMDKEXTENT pExtents = (PVMDKEXTENT)RTMemAllocZ(cExtents * sizeof(VMDKEXTENT));
    if (pExtents)
    {
        for (unsigned i = 0; i < cExtents; i++)
        {
            pExtents[i].pFile = NULL;
            pExtents[i].pszBasename = NULL;
            pExtents[i].pszFullname = NULL;
            pExtents[i].pGD = NULL;
            pExtents[i].pRGD = NULL;
            pExtents[i].pDescData = NULL;
            pExtents[i].uVersion = 1;
            pExtents[i].uCompression = VMDK_COMPRESSION_NONE;
            pExtents[i].uExtent = i;
            pExtents[i].pImage = pImage;
        }
        pImage->pExtents = pExtents;
        pImage->cExtents = cExtents;
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

/**
 * Internal: Create an additional file backed extent in split images.
 * Supports split sparse and flat images.
 *
 * @returns VBox status code.
 * @param   pImage              VMDK image instance.
 * @param   cbSize              Desiried size in bytes of new extent.
 */
static int vmdkAddFileBackedExtent(PVMDKIMAGE pImage, uint64_t cbSize)
{
    int rc = VINF_SUCCESS;
    unsigned uImageFlags = pImage->uImageFlags;

    /* Check for unsupported image type. */
    if ((uImageFlags & VD_VMDK_IMAGE_FLAGS_ESX)
        || (uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED)
        || (uImageFlags & VD_VMDK_IMAGE_FLAGS_RAWDISK))
    {
        return VERR_NOT_SUPPORTED;
    }

    /* Allocate array of extents and copy existing extents to it. */
    PVMDKEXTENT pNewExtents = (PVMDKEXTENT)RTMemAllocZ((pImage->cExtents + 1) * sizeof(VMDKEXTENT));
    if (!pNewExtents)
    {
        return VERR_NO_MEMORY;
    }

    memcpy(pNewExtents, pImage->pExtents, pImage->cExtents * sizeof(VMDKEXTENT));

    /* Locate newly created extent and populate default metadata. */
    PVMDKEXTENT pExtent = &pNewExtents[pImage->cExtents];

    pExtent->pFile = NULL;
    pExtent->pszBasename = NULL;
    pExtent->pszFullname = NULL;
    pExtent->pGD = NULL;
    pExtent->pRGD = NULL;
    pExtent->pDescData = NULL;
    pExtent->uVersion = 1;
    pExtent->uCompression = VMDK_COMPRESSION_NONE;
    pExtent->uExtent = pImage->cExtents;
    pExtent->pImage = pImage;
    pExtent->cNominalSectors = VMDK_BYTE2SECTOR(cbSize);
    pExtent->enmAccess = VMDKACCESS_READWRITE;
    pExtent->uSectorOffset = 0;
    pExtent->fMetaDirty = true;

    /* Apply image type specific meta data. */
    if (uImageFlags & VD_IMAGE_FLAGS_FIXED)
    {
        pExtent->enmType = VMDKETYPE_FLAT;
    }
    else
    {
        pExtent->enmType = VMDKETYPE_HOSTED_SPARSE;
        pExtent->cSectors = VMDK_BYTE2SECTOR(RT_ALIGN_64(cbSize, _64K));
        pExtent->cSectorsPerGrain = VMDK_BYTE2SECTOR(_64K);
        pExtent->cGTEntries = 512;

        uint64_t const cSectorsPerGDE = pExtent->cGTEntries * pExtent->cSectorsPerGrain;
        pExtent->cSectorsPerGDE = cSectorsPerGDE;
        pExtent->cGDEntries = (pExtent->cSectors + cSectorsPerGDE - 1) / cSectorsPerGDE;
    }

    /* Allocate and set file name for extent. */
    char *pszBasenameSubstr = RTPathFilename(pImage->pszFilename);
    AssertPtr(pszBasenameSubstr);

    char *pszBasenameSuff = RTPathSuffix(pszBasenameSubstr);
    char *pszBasenameBase = RTStrDup(pszBasenameSubstr);
    RTPathStripSuffix(pszBasenameBase);
    char *pszTmp;
    size_t cbTmp;

    if (pImage->uImageFlags & VD_IMAGE_FLAGS_FIXED)
        RTStrAPrintf(&pszTmp, "%s-f%03d%s", pszBasenameBase,
                        pExtent->uExtent + 1, pszBasenameSuff);
    else
        RTStrAPrintf(&pszTmp, "%s-s%03d%s", pszBasenameBase, pExtent->uExtent + 1,
                        pszBasenameSuff);

    RTStrFree(pszBasenameBase);
    if (!pszTmp)
        return VERR_NO_STR_MEMORY;
    cbTmp = strlen(pszTmp) + 1;
    char *pszBasename = (char *)RTMemTmpAlloc(cbTmp);
    if (!pszBasename)
    {
        RTStrFree(pszTmp);
        return VERR_NO_MEMORY;
    }

    memcpy(pszBasename, pszTmp, cbTmp);
    RTStrFree(pszTmp);

    pExtent->pszBasename = pszBasename;

    char *pszBasedirectory = RTStrDup(pImage->pszFilename);
    if (!pszBasedirectory)
        return VERR_NO_STR_MEMORY;
    RTPathStripFilename(pszBasedirectory);
    char *pszFullname = RTPathJoinA(pszBasedirectory, pExtent->pszBasename);
    RTStrFree(pszBasedirectory);
    if (!pszFullname)
        return VERR_NO_STR_MEMORY;
    pExtent->pszFullname = pszFullname;

    /* Create file for extent. */
    rc = vmdkFileOpen(pImage, &pExtent->pFile, pExtent->pszBasename, pExtent->pszFullname,
                        VDOpenFlagsToFileOpenFlags(pImage->uOpenFlags,
                                                    true /* fCreate */));
    if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not create new file '%s'"), pExtent->pszFullname);

    if (uImageFlags & VD_IMAGE_FLAGS_FIXED)
    {
        /* For flat images: Pre allocate file space. */
        rc = vdIfIoIntFileSetAllocationSize(pImage->pIfIo, pExtent->pFile->pStorage, cbSize,
                                            0 /* fFlags */, NULL, 0, 0);
        if (RT_FAILURE(rc))
            return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not set size of new file '%s'"), pExtent->pszFullname);
    }
    else
    {
        /* For sparse images: Allocate new grain directories/tables. */
        /* fPreAlloc should never be false because VMware can't use such images. */
        rc = vmdkCreateGrainDirectory(pImage, pExtent,
                                      RT_MAX( pExtent->uDescriptorSector
                                              + pExtent->cDescriptorSectors,
                                              1),
                                      true /* fPreAlloc */);
        if (RT_FAILURE(rc))
            return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not create new grain directory in '%s'"), pExtent->pszFullname);
    }

    /* Insert new extent into descriptor file. */
    rc = vmdkDescExtInsert(pImage, &pImage->Descriptor, pExtent->enmAccess,
                           pExtent->cNominalSectors, pExtent->enmType,
                           pExtent->pszBasename, pExtent->uSectorOffset);
    if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not insert the extent list into descriptor in '%s'"), pImage->pszFilename);

    pImage->pExtents = pNewExtents;
    pImage->cExtents++;

    return rc;
}

/**
 * Reads and processes the descriptor embedded in sparse images.
 *
 * @returns VBox status code.
 * @param   pImage         VMDK image instance.
 * @param   pFile          The sparse file handle.
 */
static int vmdkDescriptorReadSparse(PVMDKIMAGE pImage, PVMDKFILE pFile)
{
    /* It's a hosted single-extent image. */
    int rc = vmdkCreateExtents(pImage, 1);
    if (RT_SUCCESS(rc))
    {
        /* The opened file is passed to the extent. No separate descriptor
         * file, so no need to keep anything open for the image. */
        PVMDKEXTENT pExtent = &pImage->pExtents[0];
        pExtent->pFile = pFile;
        pImage->pFile = NULL;
        pExtent->pszFullname = RTPathAbsDup(pImage->pszFilename);
        if (RT_LIKELY(pExtent->pszFullname))
        {
            /* As we're dealing with a monolithic image here, there must
             * be a descriptor embedded in the image file. */
            rc = vmdkReadBinaryMetaExtent(pImage, pExtent, true /* fMagicAlreadyRead */);
            if (   RT_SUCCESS(rc)
                && pExtent->uDescriptorSector
                && pExtent->cDescriptorSectors)
            {
                /* HACK: extend the descriptor if it is unusually small and it fits in
                 * the unused space after the image header. Allows opening VMDK files
                 * with extremely small descriptor in read/write mode.
                 *
                 * The previous version introduced a possible regression for VMDK stream
                 * optimized images from VMware which tend to have only a single sector sized
                 * descriptor. Increasing the descriptor size resulted in adding the various uuid
                 * entries required to make it work with VBox but for stream optimized images
                 * the updated binary header wasn't written to the disk creating a mismatch
                 * between advertised and real descriptor size.
                 *
                 * The descriptor size will be increased even if opened readonly now if there
                 * enough room but the new value will not be written back to the image.
                 */
                if (    pExtent->cDescriptorSectors < 3
                    &&  (int64_t)pExtent->uSectorGD - pExtent->uDescriptorSector >= 4
                    &&  (!pExtent->uSectorRGD || (int64_t)pExtent->uSectorRGD - pExtent->uDescriptorSector >= 4))
                {
                    uint64_t cDescriptorSectorsOld = pExtent->cDescriptorSectors;

                    pExtent->cDescriptorSectors = 4;
                    if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
                    {
                        /*
                         * Update the on disk number now to make sure we don't introduce inconsistencies
                         * in case of stream optimized images from VMware where the descriptor is just
                         * one sector big (the binary header is not written to disk for complete
                         * stream optimized images in vmdkFlushImage()).
                         */
                        uint64_t u64DescSizeNew = RT_H2LE_U64(pExtent->cDescriptorSectors);
                        rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pFile->pStorage,
                                                    RT_UOFFSETOF(SparseExtentHeader, descriptorSize),
                                                    &u64DescSizeNew, sizeof(u64DescSizeNew));
                        if (RT_FAILURE(rc))
                        {
                            LogFlowFunc(("Increasing the descriptor size failed with %Rrc\n", rc));
                            /* Restore the old size and carry on. */
                            pExtent->cDescriptorSectors = cDescriptorSectorsOld;
                        }
                    }
                }
                /* Read the descriptor from the extent. */
                pExtent->pDescData = (char *)RTMemAllocZ(VMDK_SECTOR2BYTE(pExtent->cDescriptorSectors));
                if (RT_LIKELY(pExtent->pDescData))
                {
                    rc = vdIfIoIntFileReadSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                               VMDK_SECTOR2BYTE(pExtent->uDescriptorSector),
                                               pExtent->pDescData,
                                               VMDK_SECTOR2BYTE(pExtent->cDescriptorSectors));
                    if (RT_SUCCESS(rc))
                    {
                        rc = vmdkParseDescriptor(pImage, pExtent->pDescData,
                                                 VMDK_SECTOR2BYTE(pExtent->cDescriptorSectors));
                        if (   RT_SUCCESS(rc)
                            && (   !(pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED)
                                || !(pImage->uOpenFlags & VD_OPEN_FLAGS_ASYNC_IO)))
                        {
                            rc = vmdkReadMetaExtent(pImage, pExtent);
                            if (RT_SUCCESS(rc))
                            {
                                /* Mark the extent as unclean if opened in read-write mode. */
                                if (   !(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
                                    && !(pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED))
                                {
                                    pExtent->fUncleanShutdown = true;
                                    pExtent->fMetaDirty = true;
                                }
                            }
                        }
                        else if (RT_SUCCESS(rc))
                            rc = VERR_NOT_SUPPORTED;
                    }
                    else
                        rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: read error for descriptor in '%s'"), pExtent->pszFullname);
                }
                else
                    rc = VERR_NO_MEMORY;
            }
            else if (RT_SUCCESS(rc))
                rc = vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS, N_("VMDK: monolithic image without descriptor in '%s'"), pImage->pszFilename);
        }
        else
            rc = VERR_NO_MEMORY;
    }

    return rc;
}

/**
 * Reads the descriptor from a pure text file.
 *
 * @returns VBox status code.
 * @param   pImage          VMDK image instance.
 * @param   pFile           The descriptor file handle.
 */
static int vmdkDescriptorReadAscii(PVMDKIMAGE pImage, PVMDKFILE pFile)
{
    /* Allocate at least 10K, and make sure that there is 5K free space
     * in case new entries need to be added to the descriptor. Never
     * allocate more than 128K, because that's no valid descriptor file
     * and will result in the correct "truncated read" error handling. */
    uint64_t cbFileSize;
    int rc = vdIfIoIntFileGetSize(pImage->pIfIo, pFile->pStorage, &cbFileSize);
    if (   RT_SUCCESS(rc)
        && cbFileSize >= 50)
    {
        uint64_t cbSize = cbFileSize;
        if (cbSize % VMDK_SECTOR2BYTE(10))
            cbSize += VMDK_SECTOR2BYTE(20) - cbSize % VMDK_SECTOR2BYTE(10);
        else
            cbSize += VMDK_SECTOR2BYTE(10);
        cbSize = RT_MIN(cbSize, _128K);
        pImage->cbDescAlloc = RT_MAX(VMDK_SECTOR2BYTE(20), cbSize);
        pImage->pDescData = (char *)RTMemAllocZ(pImage->cbDescAlloc);
        if (RT_LIKELY(pImage->pDescData))
        {
            rc = vdIfIoIntFileReadSync(pImage->pIfIo, pFile->pStorage, 0, pImage->pDescData,
                                       RT_MIN(pImage->cbDescAlloc, cbFileSize));
            if (RT_SUCCESS(rc))
            {
#if 0 /** @todo Revisit */
                cbRead += sizeof(u32Magic);
                if (cbRead == pImage->cbDescAlloc)
                {
                    /* Likely the read is truncated. Better fail a bit too early
                     * (normally the descriptor is much smaller than our buffer). */
                    rc = vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS, N_("VMDK: cannot read descriptor in '%s'"), pImage->pszFilename);
                    goto out;
                }
#endif
                rc = vmdkParseDescriptor(pImage, pImage->pDescData,
                                         pImage->cbDescAlloc);
                if (RT_SUCCESS(rc))
                {
                    for (unsigned i = 0; i < pImage->cExtents && RT_SUCCESS(rc); i++)
                    {
                        PVMDKEXTENT pExtent = &pImage->pExtents[i];
                        if (pExtent->pszBasename)
                        {
                            /* Hack to figure out whether the specified name in the
                             * extent descriptor is absolute. Doesn't always work, but
                             * should be good enough for now. */
                            char *pszFullname;
                            /** @todo implement proper path absolute check. */
                            if (pExtent->pszBasename[0] == RTPATH_SLASH)
                            {
                                pszFullname = RTStrDup(pExtent->pszBasename);
                                if (!pszFullname)
                                {
                                    rc = VERR_NO_MEMORY;
                                    break;
                                }
                            }
                            else
                            {
                                char *pszDirname = RTStrDup(pImage->pszFilename);
                                if (!pszDirname)
                                {
                                    rc = VERR_NO_MEMORY;
                                    break;
                                }
                                RTPathStripFilename(pszDirname);
                                pszFullname = RTPathJoinA(pszDirname, pExtent->pszBasename);
                                RTStrFree(pszDirname);
                                if (!pszFullname)
                                {
                                    rc = VERR_NO_STR_MEMORY;
                                    break;
                                }
                            }
                            pExtent->pszFullname = pszFullname;
                        }
                        else
                            pExtent->pszFullname = NULL;

                        unsigned uOpenFlags = pImage->uOpenFlags | ((pExtent->enmAccess == VMDKACCESS_READONLY) ? VD_OPEN_FLAGS_READONLY : 0);
                        switch (pExtent->enmType)
                        {
                            case VMDKETYPE_HOSTED_SPARSE:
                                rc = vmdkFileOpen(pImage, &pExtent->pFile, pExtent->pszBasename, pExtent->pszFullname,
                                                  VDOpenFlagsToFileOpenFlags(uOpenFlags, false /* fCreate */));
                                if (RT_FAILURE(rc))
                                {
                                    /* Do NOT signal an appropriate error here, as the VD
                                     * layer has the choice of retrying the open if it
                                     * failed. */
                                    break;
                                }
                                rc = vmdkReadBinaryMetaExtent(pImage, pExtent,
                                                              false /* fMagicAlreadyRead */);
                                if (RT_FAILURE(rc))
                                    break;
                                rc = vmdkReadMetaExtent(pImage, pExtent);
                                if (RT_FAILURE(rc))
                                    break;

                                /* Mark extent as unclean if opened in read-write mode. */
                                if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
                                {
                                    pExtent->fUncleanShutdown = true;
                                    pExtent->fMetaDirty = true;
                                }
                                break;
                            case VMDKETYPE_VMFS:
                            case VMDKETYPE_FLAT:
                                rc = vmdkFileOpen(pImage, &pExtent->pFile, pExtent->pszBasename, pExtent->pszFullname,
                                                  VDOpenFlagsToFileOpenFlags(uOpenFlags, false /* fCreate */));
                                if (RT_FAILURE(rc))
                                {
                                    /* Do NOT signal an appropriate error here, as the VD
                                     * layer has the choice of retrying the open if it
                                     * failed. */
                                    break;
                                }
                                break;
                            case VMDKETYPE_ZERO:
                                /* Nothing to do. */
                                break;
                            default:
                                AssertMsgFailed(("unknown vmdk extent type %d\n", pExtent->enmType));
                        }
                    }
                }
            }
            else
                rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: read error for descriptor in '%s'"), pImage->pszFilename);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else if (RT_SUCCESS(rc))
        rc = vdIfError(pImage->pIfError, VERR_VD_VMDK_INVALID_HEADER, RT_SRC_POS, N_("VMDK: descriptor in '%s' is too short"), pImage->pszFilename);

    return rc;
}

/**
 * Read and process the descriptor based on the image type.
 *
 * @returns VBox status code.
 * @param   pImage    VMDK image instance.
 * @param   pFile     VMDK file handle.
 */
static int vmdkDescriptorRead(PVMDKIMAGE pImage, PVMDKFILE pFile)
{
    uint32_t u32Magic;

    /* Read magic (if present). */
    int rc = vdIfIoIntFileReadSync(pImage->pIfIo, pFile->pStorage, 0,
                                   &u32Magic, sizeof(u32Magic));
    if (RT_SUCCESS(rc))
    {
        /* Handle the file according to its magic number. */
        if (RT_LE2H_U32(u32Magic) == VMDK_SPARSE_MAGICNUMBER)
            rc = vmdkDescriptorReadSparse(pImage, pFile);
        else
            rc = vmdkDescriptorReadAscii(pImage, pFile);
    }
    else
    {
        vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: error reading the magic number in '%s'"), pImage->pszFilename);
        rc = VERR_VD_VMDK_INVALID_HEADER;
    }

    return rc;
}

/**
 * Internal: Open an image, constructing all necessary data structures.
 */
static int vmdkOpenImage(PVMDKIMAGE pImage, unsigned uOpenFlags)
{
    pImage->uOpenFlags = uOpenFlags;
    pImage->pIfError   = VDIfErrorGet(pImage->pVDIfsDisk);
    pImage->pIfIo      = VDIfIoIntGet(pImage->pVDIfsImage);
    AssertPtrReturn(pImage->pIfIo, VERR_INVALID_PARAMETER);

    /*
     * Open the image.
     * We don't have to check for asynchronous access because
     * we only support raw access and the opened file is a description
     * file were no data is stored.
     */
    PVMDKFILE pFile;
    int rc = vmdkFileOpen(pImage, &pFile, NULL, pImage->pszFilename,
                          VDOpenFlagsToFileOpenFlags(uOpenFlags, false /* fCreate */));
    if (RT_SUCCESS(rc))
    {
        pImage->pFile = pFile;

        rc = vmdkDescriptorRead(pImage, pFile);
        if (RT_SUCCESS(rc))
        {
            /* Determine PCHS geometry if not set. */
            if (pImage->PCHSGeometry.cCylinders == 0)
            {
                uint64_t cCylinders =   VMDK_BYTE2SECTOR(pImage->cbSize)
                                      / pImage->PCHSGeometry.cHeads
                                      / pImage->PCHSGeometry.cSectors;
                pImage->PCHSGeometry.cCylinders = (unsigned)RT_MIN(cCylinders, 16383);
                if (   !(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
                    && !(pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED))
                {
                    rc = vmdkDescSetPCHSGeometry(pImage, &pImage->PCHSGeometry);
                    AssertRC(rc);
                }
            }

            /* Update the image metadata now in case has changed. */
            rc = vmdkFlushImage(pImage, NULL);
            if (RT_SUCCESS(rc))
            {
                /* Figure out a few per-image constants from the extents. */
                pImage->cbSize = 0;
                for (unsigned i = 0; i < pImage->cExtents; i++)
                {
                    PVMDKEXTENT pExtent = &pImage->pExtents[i];
                    if (pExtent->enmType == VMDKETYPE_HOSTED_SPARSE)
                    {
                        /* Here used to be a check whether the nominal size of an extent
                         * is a multiple of the grain size. The spec says that this is
                         * always the case, but unfortunately some files out there in the
                         * wild violate the spec (e.g. ReactOS 0.3.1). */
                    }
                    else if (    pExtent->enmType == VMDKETYPE_FLAT
                             ||  pExtent->enmType == VMDKETYPE_ZERO)
                        pImage->uImageFlags |= VD_IMAGE_FLAGS_FIXED;

                    pImage->cbSize += VMDK_SECTOR2BYTE(pExtent->cNominalSectors);
                }

                if (   !(pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED)
                    || !(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
                    || !(pImage->uOpenFlags & VD_OPEN_FLAGS_SEQUENTIAL))
                    rc = vmdkAllocateGrainTableCache(pImage);
            }
        }
    }
    /* else: Do NOT signal an appropriate error here, as the VD layer has the
     *       choice of retrying the open if it failed. */

    if (RT_SUCCESS(rc))
    {
        PVDREGIONDESC pRegion = &pImage->RegionList.aRegions[0];
        pImage->RegionList.fFlags   = 0;
        pImage->RegionList.cRegions = 1;

        pRegion->offRegion            = 0; /* Disk start. */
        pRegion->cbBlock              = 512;
        pRegion->enmDataForm          = VDREGIONDATAFORM_RAW;
        pRegion->enmMetadataForm      = VDREGIONMETADATAFORM_NONE;
        pRegion->cbData               = 512;
        pRegion->cbMetadata           = 0;
        pRegion->cRegionBlocksOrBytes = pImage->cbSize;
    }
    else
        vmdkFreeImage(pImage, false, false /*fFlush*/); /* Don't try to flush anything if opening failed. */
    return rc;
}

/**
 * Frees a raw descriptor.
 * @internal
 */
static int vmdkRawDescFree(PVDISKRAW pRawDesc)
{
    if (!pRawDesc)
        return VINF_SUCCESS;

    RTStrFree(pRawDesc->pszRawDisk);
    pRawDesc->pszRawDisk = NULL;

    /* Partitions: */
    for (unsigned i = 0; i < pRawDesc->cPartDescs; i++)
    {
        RTStrFree(pRawDesc->pPartDescs[i].pszRawDevice);
        pRawDesc->pPartDescs[i].pszRawDevice = NULL;

        RTMemFree(pRawDesc->pPartDescs[i].pvPartitionData);
        pRawDesc->pPartDescs[i].pvPartitionData = NULL;
    }

    RTMemFree(pRawDesc->pPartDescs);
    pRawDesc->pPartDescs = NULL;

    RTMemFree(pRawDesc);
    return VINF_SUCCESS;
}

/**
 * Helper that grows the raw partition descriptor table by @a cToAdd entries,
 * returning the pointer to the first new entry.
 * @internal
 */
static int vmdkRawDescAppendPartDesc(PVMDKIMAGE pImage, PVDISKRAW pRawDesc, uint32_t cToAdd, PVDISKRAWPARTDESC *ppRet)
{
    uint32_t const    cOld = pRawDesc->cPartDescs;
    uint32_t const    cNew   = cOld + cToAdd;
    PVDISKRAWPARTDESC paNew = (PVDISKRAWPARTDESC)RTMemReallocZ(pRawDesc->pPartDescs,
                                                               cOld * sizeof(pRawDesc->pPartDescs[0]),
                                                               cNew * sizeof(pRawDesc->pPartDescs[0]));
    if (paNew)
    {
        pRawDesc->cPartDescs = cNew;
        pRawDesc->pPartDescs = paNew;

        *ppRet = &paNew[cOld];
        return VINF_SUCCESS;
    }
    *ppRet = NULL;
    return vdIfError(pImage->pIfError, VERR_NO_MEMORY, RT_SRC_POS,
                     N_("VMDK: Image path: '%s'. Out of memory growing the partition descriptors (%u -> %u)."),
                     pImage->pszFilename, cOld, cNew);
}

/**
 * @callback_method_impl{FNRTSORTCMP}
 */
static DECLCALLBACK(int) vmdkRawDescPartComp(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    RT_NOREF(pvUser);
    int64_t const iDelta = ((PVDISKRAWPARTDESC)pvElement1)->offStartInVDisk - ((PVDISKRAWPARTDESC)pvElement2)->offStartInVDisk;
    return iDelta < 0 ? -1 : iDelta > 0 ? 1 : 0;
}

/**
 * Post processes the partition descriptors.
 *
 * Sorts them and check that they don't overlap.
 */
static int vmdkRawDescPostProcessPartitions(PVMDKIMAGE pImage, PVDISKRAW pRawDesc, uint64_t cbSize)
{
    /*
     * Sort data areas in ascending order of start.
     */
    RTSortShell(pRawDesc->pPartDescs, pRawDesc->cPartDescs, sizeof(pRawDesc->pPartDescs[0]), vmdkRawDescPartComp, NULL);

    /*
     * Check that we don't have overlapping descriptors.  If we do, that's an
     * indication that the drive is corrupt or that the RTDvm code is buggy.
     */
    VDISKRAWPARTDESC const *paPartDescs = pRawDesc->pPartDescs;
    for (uint32_t i = 0; i < pRawDesc->cPartDescs; i++)
    {
        uint64_t offLast = paPartDescs[i].offStartInVDisk + paPartDescs[i].cbData;
        if (offLast <= paPartDescs[i].offStartInVDisk)
            return vdIfError(pImage->pIfError, VERR_FILESYSTEM_CORRUPT /*?*/, RT_SRC_POS,
                             N_("VMDK: Image path: '%s'. Bogus partition descriptor #%u (%#RX64 LB %#RX64%s): Wrap around or zero"),
                             pImage->pszFilename, i, paPartDescs[i].offStartInVDisk, paPartDescs[i].cbData,
                             paPartDescs[i].pvPartitionData ? " (data)" : "");
        offLast -= 1;

        if (i + 1 < pRawDesc->cPartDescs && offLast >= paPartDescs[i + 1].offStartInVDisk)
            return vdIfError(pImage->pIfError, VERR_FILESYSTEM_CORRUPT /*?*/, RT_SRC_POS,
                             N_("VMDK: Image path: '%s'. Partition descriptor #%u (%#RX64 LB %#RX64%s) overlaps with the next (%#RX64 LB %#RX64%s)"),
                             pImage->pszFilename, i, paPartDescs[i].offStartInVDisk, paPartDescs[i].cbData,
                             paPartDescs[i].pvPartitionData ? " (data)" : "", paPartDescs[i + 1].offStartInVDisk,
                             paPartDescs[i + 1].cbData, paPartDescs[i + 1].pvPartitionData ? " (data)" : "");
        if (offLast >= cbSize)
            return vdIfError(pImage->pIfError, VERR_FILESYSTEM_CORRUPT /*?*/, RT_SRC_POS,
                             N_("VMDK: Image path: '%s'. Partition descriptor #%u (%#RX64 LB %#RX64%s) goes beyond the end of the drive (%#RX64)"),
                             pImage->pszFilename, i, paPartDescs[i].offStartInVDisk, paPartDescs[i].cbData,
                             paPartDescs[i].pvPartitionData ? " (data)" : "", cbSize);
    }

    return VINF_SUCCESS;
}


#ifdef RT_OS_LINUX
/**
 * Searches the dir specified in @a pszBlockDevDir for subdirectories with a
 * 'dev' file matching @a uDevToLocate.
 *
 * This is used both
 *
 * @returns IPRT status code, errors have been reported properly.
 * @param   pImage          For error reporting.
 * @param   pszBlockDevDir  Input: Path to the directory search under.
 *                          Output: Path to the directory containing information
 *                          for @a uDevToLocate.
 * @param   cbBlockDevDir   The size of the buffer @a pszBlockDevDir points to.
 * @param   uDevToLocate    The device number of the block device info dir to
 *                          locate.
 * @param   pszDevToLocate  For error reporting.
 */
static int vmdkFindSysBlockDevPath(PVMDKIMAGE pImage, char *pszBlockDevDir, size_t cbBlockDevDir,
                                   dev_t uDevToLocate, const char *pszDevToLocate)
{
    size_t const cchDir = RTPathEnsureTrailingSeparator(pszBlockDevDir, cbBlockDevDir);
    AssertReturn(cchDir > 0, VERR_BUFFER_OVERFLOW);

    RTDIR hDir = NIL_RTDIR;
    int rc = RTDirOpen(&hDir, pszBlockDevDir);
    if (RT_SUCCESS(rc))
    {
        for (;;)
        {
            RTDIRENTRY Entry;
            rc = RTDirRead(hDir, &Entry, NULL);
            if (RT_SUCCESS(rc))
            {
                /* We're interested in directories and symlinks. */
                if (   Entry.enmType == RTDIRENTRYTYPE_DIRECTORY
                    || Entry.enmType == RTDIRENTRYTYPE_SYMLINK
                    || Entry.enmType == RTDIRENTRYTYPE_UNKNOWN)
                {
                    rc = RTStrCopy(&pszBlockDevDir[cchDir], cbBlockDevDir - cchDir, Entry.szName);
                    AssertContinue(RT_SUCCESS(rc)); /* should not happen! */

                    dev_t uThisDevNo = ~uDevToLocate;
                    rc = RTLinuxSysFsReadDevNumFile(&uThisDevNo, "%s/dev", pszBlockDevDir);
                    if (RT_SUCCESS(rc) && uThisDevNo == uDevToLocate)
                        break;
                }
            }
            else
            {
                pszBlockDevDir[cchDir] = '\0';
                if (rc == VERR_NO_MORE_FILES)
                    rc = vdIfError(pImage->pIfError, VERR_NOT_FOUND, RT_SRC_POS,
                                   N_("VMDK: Image path: '%s'. Failed to locate device corresponding to '%s' under '%s'"),
                                   pImage->pszFilename, pszDevToLocate, pszBlockDevDir);
                else
                    rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                                   N_("VMDK: Image path: '%s'. RTDirRead failed enumerating '%s': %Rrc"),
                                   pImage->pszFilename, pszBlockDevDir, rc);
                break;
            }
        }
        RTDirClose(hDir);
    }
    else
        rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                       N_("VMDK: Image path: '%s'. Failed to open dir '%s' for listing: %Rrc"),
                       pImage->pszFilename, pszBlockDevDir, rc);
    return rc;
}
#endif /* RT_OS_LINUX */

#ifdef RT_OS_FREEBSD


/**
 * Reads the config data from the provider and returns offset and size
 *
 * @return IPRT status code
 * @param pProvider   GEOM provider representing partition
 * @param pcbOffset   Placeholder for the offset of the partition
 * @param pcbSize     Placeholder for the size of the partition
 */
static int vmdkReadPartitionsParamsFromProvider(gprovider *pProvider, uint64_t *pcbOffset, uint64_t *pcbSize)
{
    gconfig *pConfEntry;
    int rc = VERR_NOT_FOUND;

    /*
     * Required parameters are located in the list containing key/value pairs.
     * Both key and value are in text form. Manuals tells nothing about the fact
     * that the both parameters should be present in the list. Thus, there are
     * cases when only one parameter is presented. To handle such cases we treat
     * absent params as zero allowing the caller decide the case is either correct
     * or an error.
     */
    uint64_t cbOffset = 0;
    uint64_t cbSize = 0;
    LIST_FOREACH(pConfEntry, &pProvider->lg_config, lg_config)
    {
        if (RTStrCmp(pConfEntry->lg_name, "offset") == 0)
        {
            cbOffset = RTStrToUInt64(pConfEntry->lg_val);
            rc = VINF_SUCCESS;
        }
        else if (RTStrCmp(pConfEntry->lg_name, "length") == 0)
        {
            cbSize = RTStrToUInt64(pConfEntry->lg_val);
            rc = VINF_SUCCESS;
        }
    }
    if (RT_SUCCESS(rc))
    {
        *pcbOffset = cbOffset;
        *pcbSize = cbSize;
    }
    return rc;
}


/**
 * Searches the partition specified by name and calculates its size and absolute offset.
 *
 * @return IPRT status code.
 * @param pParentClass       Class containing pParentGeom
 * @param pszParentGeomName  Name of the parent geom where we are looking for provider
 * @param pszProviderName    Name of the provider we are looking for
 * @param pcbAbsoluteOffset  Placeholder for the absolute offset of the partition, i.e. offset from the beginning of the disk
 * @param psbSize            Placeholder for the size of the partition.
 */
static int vmdkFindPartitionParamsByName(gclass *pParentClass, const char *pszParentGeomName, const char *pszProviderName,
                                         uint64_t *pcbAbsoluteOffset, uint64_t *pcbSize)
{
    AssertReturn(pParentClass,       VERR_INVALID_PARAMETER);
    AssertReturn(pszParentGeomName,  VERR_INVALID_PARAMETER);
    AssertReturn(pszProviderName,    VERR_INVALID_PARAMETER);
    AssertReturn(pcbAbsoluteOffset,  VERR_INVALID_PARAMETER);
    AssertReturn(pcbSize,            VERR_INVALID_PARAMETER);

    ggeom *pParentGeom;
    int rc = VERR_NOT_FOUND;
    LIST_FOREACH(pParentGeom, &pParentClass->lg_geom, lg_geom)
    {
        if (RTStrCmp(pParentGeom->lg_name, pszParentGeomName) == 0)
        {
            rc = VINF_SUCCESS;
            break;
        }
    }
    if (RT_FAILURE(rc))
        return rc;

    gprovider *pProvider;
    /*
     * First, go over providers without handling EBR or BSDLabel
     * partitions for case when looking provider is child
     * of the givng geom, to reduce searching time
     */
    LIST_FOREACH(pProvider, &pParentGeom->lg_provider, lg_provider)
    {
        if (RTStrCmp(pProvider->lg_name, pszProviderName) == 0)
            return vmdkReadPartitionsParamsFromProvider(pProvider, pcbAbsoluteOffset, pcbSize);
    }

    /*
     * No provider found. Go over the parent geom again
     * and make recursions if geom represents EBR or BSDLabel.
     * In this case given parent geom contains only EBR or BSDLabel
     * partition itself and their own partitions are in the separate
     * geoms. Also, partition offsets are relative to geom, so
     * we have to add offset from child provider with parent geoms
     * provider
     */

    LIST_FOREACH(pProvider, &pParentGeom->lg_provider, lg_provider)
    {
        uint64_t cbOffset = 0;
        uint64_t cbSize = 0;
        rc = vmdkReadPartitionsParamsFromProvider(pProvider, &cbOffset, &cbSize);
        if (RT_FAILURE(rc))
            return rc;

        uint64_t cbProviderOffset = 0;
        uint64_t cbProviderSize = 0;
        rc = vmdkFindPartitionParamsByName(pParentClass, pProvider->lg_name, pszProviderName, &cbProviderOffset, &cbProviderSize);
        if (RT_SUCCESS(rc))
        {
            *pcbAbsoluteOffset = cbOffset + cbProviderOffset;
            *pcbSize = cbProviderSize;
            return rc;
        }
    }

    return VERR_NOT_FOUND;
}
#endif


/**
 * Attempts to verify the raw partition path.
 *
 * We don't want to trust RTDvm and the partition device node morphing blindly.
 */
static int vmdkRawDescVerifyPartitionPath(PVMDKIMAGE pImage, PVDISKRAWPARTDESC pPartDesc, uint32_t idxPartition,
                                          const char *pszRawDrive, RTFILE hRawDrive, uint32_t cbSector, RTDVMVOLUME hVol)
{
    RT_NOREF(pImage, pPartDesc, idxPartition, pszRawDrive, hRawDrive, cbSector, hVol);

    /*
     * Try open the raw partition device.
     */
    RTFILE hRawPart = NIL_RTFILE;
    int rc = RTFileOpen(&hRawPart, pPartDesc->pszRawDevice, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE);
    if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                         N_("VMDK: Image path: '%s'. Failed to open partition #%u on '%s' via '%s' (%Rrc)"),
                         pImage->pszFilename, idxPartition, pszRawDrive, pPartDesc->pszRawDevice, rc);

    /*
     * Compare the partition UUID if we can get it.
     */
#ifdef RT_OS_WINDOWS
    DWORD cbReturned;

    /* 1. Get the device numbers for both handles, they should have the same disk. */
    STORAGE_DEVICE_NUMBER DevNum1;
    RT_ZERO(DevNum1);
    if (!DeviceIoControl((HANDLE)RTFileToNative(hRawDrive), IOCTL_STORAGE_GET_DEVICE_NUMBER,
                         NULL /*pvInBuffer*/, 0 /*cbInBuffer*/, &DevNum1, sizeof(DevNum1), &cbReturned, NULL /*pOverlapped*/))
        rc = vdIfError(pImage->pIfError, RTErrConvertFromWin32(GetLastError()), RT_SRC_POS,
                       N_("VMDK: Image path: '%s'. IOCTL_STORAGE_GET_DEVICE_NUMBER failed on '%s': %u"),
                       pImage->pszFilename, pszRawDrive, GetLastError());

    STORAGE_DEVICE_NUMBER DevNum2;
    RT_ZERO(DevNum2);
    if (!DeviceIoControl((HANDLE)RTFileToNative(hRawPart), IOCTL_STORAGE_GET_DEVICE_NUMBER,
                         NULL /*pvInBuffer*/, 0 /*cbInBuffer*/, &DevNum2, sizeof(DevNum2), &cbReturned, NULL /*pOverlapped*/))
        rc = vdIfError(pImage->pIfError, RTErrConvertFromWin32(GetLastError()), RT_SRC_POS,
                       N_("VMDK: Image path: '%s'. IOCTL_STORAGE_GET_DEVICE_NUMBER failed on '%s': %u"),
                       pImage->pszFilename, pPartDesc->pszRawDevice, GetLastError());
    if (   RT_SUCCESS(rc)
        && (   DevNum1.DeviceNumber != DevNum2.DeviceNumber
            || DevNum1.DeviceType   != DevNum2.DeviceType))
        rc = vdIfError(pImage->pIfError, VERR_MISMATCH, RT_SRC_POS,
                       N_("VMDK: Image path: '%s'. Partition #%u path ('%s') verification failed on '%s' (%#x != %#x || %#x != %#x)"),
                       pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive,
                       DevNum1.DeviceNumber, DevNum2.DeviceNumber, DevNum1.DeviceType, DevNum2.DeviceType);
    if (RT_SUCCESS(rc))
    {
        /* Get the partitions from the raw drive and match up with the volume info
           from RTDvm.  The partition number is found in DevNum2. */
        DWORD cbNeeded = 0;
        if (   DeviceIoControl((HANDLE)RTFileToNative(hRawDrive), IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
                               NULL /*pvInBuffer*/, 0 /*cbInBuffer*/, NULL, 0, &cbNeeded, NULL /*pOverlapped*/)
            || cbNeeded < RT_UOFFSETOF_DYN(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry[1]))
            cbNeeded = RT_UOFFSETOF_DYN(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry[64]);
        cbNeeded += sizeof(PARTITION_INFORMATION_EX) * 2; /* just in case */
        DRIVE_LAYOUT_INFORMATION_EX *pLayout = (DRIVE_LAYOUT_INFORMATION_EX *)RTMemTmpAllocZ(cbNeeded);
        if (pLayout)
        {
            cbReturned = 0;
            if (DeviceIoControl((HANDLE)RTFileToNative(hRawDrive), IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
                                NULL /*pvInBuffer*/, 0 /*cbInBuffer*/, pLayout, cbNeeded, &cbReturned, NULL /*pOverlapped*/))
            {
                /* Find the entry with the given partition number (it's not an index, array contains empty MBR entries ++). */
                unsigned iEntry = 0;
                while (   iEntry < pLayout->PartitionCount
                       && pLayout->PartitionEntry[iEntry].PartitionNumber != DevNum2.PartitionNumber)
                    iEntry++;
                if (iEntry < pLayout->PartitionCount)
                {
                    /* Compare the basics */
                    PARTITION_INFORMATION_EX const * const pLayoutEntry = &pLayout->PartitionEntry[iEntry];
                    if (pLayoutEntry->StartingOffset.QuadPart != (int64_t)pPartDesc->offStartInVDisk)
                        rc = vdIfError(pImage->pIfError, VERR_MISMATCH, RT_SRC_POS,
                                       N_("VMDK: Image path: '%s'. Partition #%u path ('%s') verification failed on '%s': StartingOffset %RU64, expected %RU64"),
                                       pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive,
                                       pLayoutEntry->StartingOffset.QuadPart, pPartDesc->offStartInVDisk);
                    else if (pLayoutEntry->PartitionLength.QuadPart != (int64_t)pPartDesc->cbData)
                        rc = vdIfError(pImage->pIfError, VERR_MISMATCH, RT_SRC_POS,
                                       N_("VMDK: Image path: '%s'. Partition #%u path ('%s') verification failed on '%s': PartitionLength %RU64, expected %RU64"),
                                       pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive,
                                       pLayoutEntry->PartitionLength.QuadPart, pPartDesc->cbData);
                    /** @todo We could compare the MBR type, GPT type and ID. */
                    RT_NOREF(hVol);
                }
                else
                    rc = vdIfError(pImage->pIfError, VERR_MISMATCH, RT_SRC_POS,
                                   N_("VMDK: Image path: '%s'. Partition #%u path ('%s') verification failed on '%s': PartitionCount (%#x vs %#x)"),
                                   pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive,
                                   DevNum2.PartitionNumber, pLayout->PartitionCount);
# ifndef LOG_ENABLED
                if (RT_FAILURE(rc))
# endif
                {
                    LogRel(("VMDK: Windows reports %u partitions for '%s':\n", pLayout->PartitionCount, pszRawDrive));
                    PARTITION_INFORMATION_EX const *pEntry = &pLayout->PartitionEntry[0];
                    for (DWORD i = 0; i < pLayout->PartitionCount; i++, pEntry++)
                    {
                        LogRel(("VMDK: #%u/%u: %016RU64 LB %016RU64 style=%d rewrite=%d",
                                i, pEntry->PartitionNumber, pEntry->StartingOffset.QuadPart, pEntry->PartitionLength.QuadPart,
                                pEntry->PartitionStyle, pEntry->RewritePartition));
                        if (pEntry->PartitionStyle == PARTITION_STYLE_MBR)
                            LogRel((" type=%#x boot=%d rec=%d hidden=%u\n", pEntry->Mbr.PartitionType, pEntry->Mbr.BootIndicator,
                                    pEntry->Mbr.RecognizedPartition, pEntry->Mbr.HiddenSectors));
                        else if (pEntry->PartitionStyle == PARTITION_STYLE_GPT)
                            LogRel((" type=%RTuuid id=%RTuuid aatrib=%RX64 name=%.36ls\n", &pEntry->Gpt.PartitionType,
                                    &pEntry->Gpt.PartitionId, pEntry->Gpt.Attributes, &pEntry->Gpt.Name[0]));
                        else
                            LogRel(("\n"));
                    }
                    LogRel(("VMDK: Looked for partition #%u (%u, '%s') at %RU64 LB %RU64\n", DevNum2.PartitionNumber,
                            idxPartition, pPartDesc->pszRawDevice, pPartDesc->offStartInVDisk, pPartDesc->cbData));
               }
            }
            else
                rc = vdIfError(pImage->pIfError, RTErrConvertFromWin32(GetLastError()), RT_SRC_POS,
                               N_("VMDK: Image path: '%s'. IOCTL_DISK_GET_DRIVE_LAYOUT_EX failed on '%s': %u (cb %u, cbRet %u)"),
                               pImage->pszFilename, pPartDesc->pszRawDevice, GetLastError(), cbNeeded, cbReturned);
            RTMemTmpFree(pLayout);
        }
        else
            rc = VERR_NO_TMP_MEMORY;
    }

#elif defined(RT_OS_LINUX)
    RT_NOREF(hVol);

    /* Stat the two devices first to get their device numbers.  (We probably
       could make some assumptions here about the major & minor number assignments
       for legacy nodes, but it doesn't hold up for nvme, so we'll skip that.) */
    struct stat StDrive, StPart;
    if (fstat((int)RTFileToNative(hRawDrive), &StDrive) != 0)
        rc = vdIfError(pImage->pIfError, RTErrConvertFromErrno(errno), RT_SRC_POS,
                       N_("VMDK: Image path: '%s'. fstat failed on '%s': %d"), pImage->pszFilename, pszRawDrive, errno);
    else if (fstat((int)RTFileToNative(hRawPart), &StPart) != 0)
        rc = vdIfError(pImage->pIfError, RTErrConvertFromErrno(errno), RT_SRC_POS,
                       N_("VMDK: Image path: '%s'. fstat failed on '%s': %d"), pImage->pszFilename, pPartDesc->pszRawDevice, errno);
    else
    {
        /* Scan the directories immediately under /sys/block/ for one with a
           'dev' file matching the drive's device number: */
        char szSysPath[RTPATH_MAX];
        rc = RTLinuxConstructPath(szSysPath, sizeof(szSysPath), "block/");
        AssertRCReturn(rc, rc); /* this shall not fail */
        if (RTDirExists(szSysPath))
        {
            rc = vmdkFindSysBlockDevPath(pImage, szSysPath, sizeof(szSysPath), StDrive.st_rdev, pszRawDrive);

            /* Now, scan the directories under that again for a partition device
               matching the hRawPart device's number: */
            if (RT_SUCCESS(rc))
                rc = vmdkFindSysBlockDevPath(pImage, szSysPath, sizeof(szSysPath), StPart.st_rdev, pPartDesc->pszRawDevice);

            /* Having found the /sys/block/device/partition/ path, we can finally
               read the partition attributes and compare with hVol. */
            if (RT_SUCCESS(rc))
            {
                /* partition number: */
                int64_t iLnxPartition = 0;
                rc = RTLinuxSysFsReadIntFile(10, &iLnxPartition, "%s/partition", szSysPath);
                if (RT_SUCCESS(rc) && iLnxPartition != idxPartition)
                    rc = vdIfError(pImage->pIfError, VERR_MISMATCH, RT_SRC_POS,
                                   N_("VMDK: Image path: '%s'. Partition #%u path ('%s') verification failed on '%s': Partition number %RI64, expected %RU32"),
                                   pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive, iLnxPartition, idxPartition);
                /* else: ignore failure? */

                /* start offset: */
                uint32_t const cbLnxSector = 512; /* It's hardcoded in the Linux kernel */
                if (RT_SUCCESS(rc))
                {
                    int64_t offLnxStart = -1;
                    rc = RTLinuxSysFsReadIntFile(10, &offLnxStart, "%s/start", szSysPath);
                    offLnxStart *= cbLnxSector;
                    if (RT_SUCCESS(rc) && offLnxStart != (int64_t)pPartDesc->offStartInVDisk)
                        rc = vdIfError(pImage->pIfError, VERR_MISMATCH, RT_SRC_POS,
                                       N_("VMDK: Image path: '%s'. Partition #%u path ('%s') verification failed on '%s': Start offset %RI64, expected %RU64"),
                                       pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive, offLnxStart, pPartDesc->offStartInVDisk);
                    /* else: ignore failure? */
                }

                /* the size: */
                if (RT_SUCCESS(rc))
                {
                    int64_t cbLnxData = -1;
                    rc = RTLinuxSysFsReadIntFile(10, &cbLnxData, "%s/size", szSysPath);
                    cbLnxData *= cbLnxSector;
                    if (RT_SUCCESS(rc) && cbLnxData != (int64_t)pPartDesc->cbData)
                        rc = vdIfError(pImage->pIfError, VERR_MISMATCH, RT_SRC_POS,
                                       N_("VMDK: Image path: '%s'. Partition #%u path ('%s') verification failed on '%s': Size %RI64, expected %RU64"),
                                       pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive, cbLnxData, pPartDesc->cbData);
                    /* else: ignore failure? */
                }
            }
        }
        /* else: We've got nothing to work on, so only do content comparison. */
    }

#elif defined(RT_OS_FREEBSD)
    char szDriveDevName[256];
    char* pszDevName = fdevname_r(RTFileToNative(hRawDrive), szDriveDevName, 256);
    if (pszDevName == NULL)
        rc = vdIfError(pImage->pIfError, VERR_INVALID_PARAMETER, RT_SRC_POS,
                       N_("VMDK: Image path: '%s'. '%s' is not a drive path"), pImage->pszFilename, pszRawDrive);
    char szPartDevName[256];
    if (RT_SUCCESS(rc))
    {
        pszDevName = fdevname_r(RTFileToNative(hRawPart), szPartDevName, 256);
        if (pszDevName == NULL)
            rc = vdIfError(pImage->pIfError, VERR_INVALID_PARAMETER, RT_SRC_POS,
                           N_("VMDK: Image path: '%s'. '%s' is not a partition path"), pImage->pszFilename, pPartDesc->pszRawDevice);
    }
    if (RT_SUCCESS(rc))
    {
        gmesh geomMesh;
        int err = geom_gettree(&geomMesh);
        if (err == 0)
        {
            /* Find root class containg partitions info */
            gclass* pPartClass;
            LIST_FOREACH(pPartClass, &geomMesh.lg_class, lg_class)
            {
                if (RTStrCmp(pPartClass->lg_name, "PART") == 0)
                    break;
            }
            if (pPartClass == NULL || RTStrCmp(pPartClass->lg_name, "PART") != 0)
                rc = vdIfError(pImage->pIfError, VERR_GENERAL_FAILURE, RT_SRC_POS,
                               N_("VMDK: Image path: '%s'. 'PART' class not found in the GEOM tree"), pImage->pszFilename);


            if (RT_SUCCESS(rc))
            {
                /* Find provider representing partition device */
                uint64_t cbOffset;
                uint64_t cbSize;
                rc = vmdkFindPartitionParamsByName(pPartClass, szDriveDevName, szPartDevName, &cbOffset, &cbSize);
                if (RT_SUCCESS(rc))
                {
                    if (cbOffset != pPartDesc->offStartInVDisk)
                        rc = vdIfError(pImage->pIfError, VERR_MISMATCH, RT_SRC_POS,
                                       N_("VMDK: Image path: '%s'. Partition #%u path ('%s') verification failed on '%s': Start offset %RU64, expected %RU64"),
                                       pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive, cbOffset, pPartDesc->offStartInVDisk);
                    if (cbSize != pPartDesc->cbData)
                        rc = vdIfError(pImage->pIfError, VERR_MISMATCH, RT_SRC_POS,
                                       N_("VMDK: Image path: '%s'. Partition #%u path ('%s') verification failed on '%s': Size %RU64, expected %RU64"),
                                       pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive, cbSize, pPartDesc->cbData);
                }
                else
                    rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                                   N_("VMDK: Image path: '%s'. Error getting geom provider for the partition '%s' of the drive '%s' in the GEOM tree: %Rrc"),
                                   pImage->pszFilename, pPartDesc->pszRawDevice, pszRawDrive, rc);
            }

            geom_deletetree(&geomMesh);
        }
        else
            rc = vdIfError(pImage->pIfError, RTErrConvertFromErrno(err), RT_SRC_POS,
                           N_("VMDK: Image path: '%s'. geom_gettree failed: %d"), pImage->pszFilename, err);
    }

#elif defined(RT_OS_SOLARIS)
    RT_NOREF(hVol);

    dk_cinfo dkiDriveInfo;
    dk_cinfo dkiPartInfo;
    if (ioctl(RTFileToNative(hRawDrive), DKIOCINFO, (caddr_t)&dkiDriveInfo) == -1)
        rc = vdIfError(pImage->pIfError, RTErrConvertFromErrno(errno), RT_SRC_POS,
                       N_("VMDK: Image path: '%s'. DKIOCINFO failed on '%s': %d"), pImage->pszFilename, pszRawDrive, errno);
    else if (ioctl(RTFileToNative(hRawPart), DKIOCINFO, (caddr_t)&dkiPartInfo) == -1)
        rc = vdIfError(pImage->pIfError, RTErrConvertFromErrno(errno), RT_SRC_POS,
                       N_("VMDK: Image path: '%s'. DKIOCINFO failed on '%s': %d"), pImage->pszFilename, pszRawDrive, errno);
    else if (  dkiDriveInfo.dki_ctype != dkiPartInfo.dki_ctype
            || dkiDriveInfo.dki_cnum  != dkiPartInfo.dki_cnum
            || dkiDriveInfo.dki_addr  != dkiPartInfo.dki_addr
            || dkiDriveInfo.dki_unit  != dkiPartInfo.dki_unit
            || dkiDriveInfo.dki_slave != dkiPartInfo.dki_slave)
        rc = vdIfError(pImage->pIfError, VERR_MISMATCH, RT_SRC_POS,
                       N_("VMDK: Image path: '%s'. Partition #%u path ('%s') verification failed on '%s' (%#x != %#x || %#x != %#x || %#x != %#x || %#x != %#x || %#x != %#x)"),
                       pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive,
                       dkiDriveInfo.dki_ctype, dkiPartInfo.dki_ctype, dkiDriveInfo.dki_cnum, dkiPartInfo.dki_cnum,
                       dkiDriveInfo.dki_addr, dkiPartInfo.dki_addr, dkiDriveInfo.dki_unit, dkiPartInfo.dki_unit,
                       dkiDriveInfo.dki_slave, dkiPartInfo.dki_slave);
    else
    {
        uint64_t cbOffset = 0;
        uint64_t cbSize = 0;
        dk_gpt *pEfi = NULL;
        int idxEfiPart = efi_alloc_and_read(RTFileToNative(hRawPart), &pEfi);
        if (idxEfiPart >= 0)
        {
            if ((uint32_t)dkiPartInfo.dki_partition + 1 == idxPartition)
            {
                cbOffset = pEfi->efi_parts[idxEfiPart].p_start * pEfi->efi_lbasize;
                cbSize   = pEfi->efi_parts[idxEfiPart].p_size  * pEfi->efi_lbasize;
            }
            else
                rc = vdIfError(pImage->pIfError, VERR_MISMATCH, RT_SRC_POS,
                               N_("VMDK: Image path: '%s'. Partition #%u number ('%s') verification failed on '%s' (%#x != %#x)"),
                               pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive,
                               idxPartition, (uint32_t)dkiPartInfo.dki_partition + 1);
            efi_free(pEfi);
        }
        else
        {
            /*
             * Manual says the efi_alloc_and_read returns VT_EINVAL if no EFI partition table found.
             * Actually, the function returns any error, e.g. VT_ERROR. Thus, we are not sure, is it
             * real error or just no EFI table found. Therefore, let's try to obtain partition info
             * using another way. If there is an error, it returns errno which will be handled below.
             */

            uint32_t numPartition = (uint32_t)dkiPartInfo.dki_partition;
            if (numPartition > NDKMAP)
                numPartition -= NDKMAP;
            if (numPartition != idxPartition)
                rc = vdIfError(pImage->pIfError, VERR_MISMATCH, RT_SRC_POS,
                               N_("VMDK: Image path: '%s'. Partition #%u number ('%s') verification failed on '%s' (%#x != %#x)"),
                               pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive,
                               idxPartition, numPartition);
            else
            {
                dk_minfo_ext mediaInfo;
                if (ioctl(RTFileToNative(hRawPart), DKIOCGMEDIAINFOEXT, (caddr_t)&mediaInfo) == -1)
                    rc = vdIfError(pImage->pIfError, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                   N_("VMDK: Image path: '%s'. Partition #%u number ('%s') verification failed on '%s'. Can not obtain partition info: %d"),
                                   pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive, errno);
                else
                {
                    extpart_info extPartInfo;
                    if (ioctl(RTFileToNative(hRawPart), DKIOCEXTPARTINFO, (caddr_t)&extPartInfo) != -1)
                    {
                        cbOffset = (uint64_t)extPartInfo.p_start * mediaInfo.dki_lbsize;
                        cbSize   = (uint64_t)extPartInfo.p_length * mediaInfo.dki_lbsize;
                    }
                    else
                        rc = vdIfError(pImage->pIfError, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                       N_("VMDK: Image path: '%s'. Partition #%u number ('%s') verification failed on '%s'. Can not obtain partition info: %d"),
                                       pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive, errno);
                }
            }
        }
        if (RT_SUCCESS(rc) && cbOffset != pPartDesc->offStartInVDisk)
            rc = vdIfError(pImage->pIfError, VERR_MISMATCH, RT_SRC_POS,
                           N_("VMDK: Image path: '%s'. Partition #%u path ('%s') verification failed on '%s': Start offset %RI64, expected %RU64"),
                           pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive, cbOffset, pPartDesc->offStartInVDisk);

        if (RT_SUCCESS(rc) && cbSize != pPartDesc->cbData)
            rc = vdIfError(pImage->pIfError, VERR_MISMATCH, RT_SRC_POS,
                           N_("VMDK: Image path: '%s'. Partition #%u path ('%s') verification failed on '%s': Size %RI64, expected %RU64"),
                           pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive, cbSize, pPartDesc->cbData);
    }

#elif defined(RT_OS_DARWIN)
    /* Stat the drive get its device number. */
    struct stat StDrive;
    if (fstat((int)RTFileToNative(hRawDrive), &StDrive) != 0)
        rc = vdIfError(pImage->pIfError, RTErrConvertFromErrno(errno), RT_SRC_POS,
                       N_("VMDK: Image path: '%s'. fstat failed on '%s' (errno=%d)"), pImage->pszFilename, pszRawDrive, errno);
    else
    {
        if (ioctl(RTFileToNative(hRawPart), DKIOCLOCKPHYSICALEXTENTS, NULL) == -1)
            rc = vdIfError(pImage->pIfError, RTErrConvertFromErrno(errno), RT_SRC_POS,
                           N_("VMDK: Image path: '%s'. Partition #%u number ('%s') verification failed on '%s': Unable to lock the partition (errno=%d)"),
                           pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive, errno);
        else
        {
            uint32_t cbBlockSize = 0;
            uint64_t cbOffset = 0;
            uint64_t cbSize = 0;
            if (ioctl(RTFileToNative(hRawPart), DKIOCGETBLOCKSIZE, (caddr_t)&cbBlockSize) == -1)
                rc = vdIfError(pImage->pIfError, RTErrConvertFromErrno(errno), RT_SRC_POS,
                               N_("VMDK: Image path: '%s'. Partition #%u number ('%s') verification failed on '%s': Unable to obtain the sector size of the partition (errno=%d)"),
                               pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive, errno);
            else if (ioctl(RTFileToNative(hRawPart), DKIOCGETBASE, (caddr_t)&cbOffset) == -1)
                rc = vdIfError(pImage->pIfError, RTErrConvertFromErrno(errno), RT_SRC_POS,
                               N_("VMDK: Image path: '%s'. Partition #%u number ('%s') verification failed on '%s': Unable to obtain the start offset of the partition (errno=%d)"),
                               pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive, errno);
            else if (ioctl(RTFileToNative(hRawPart), DKIOCGETBLOCKCOUNT, (caddr_t)&cbSize) == -1)
                rc = vdIfError(pImage->pIfError, RTErrConvertFromErrno(errno), RT_SRC_POS,
                               N_("VMDK: Image path: '%s'. Partition #%u number ('%s') verification failed on '%s': Unable to obtain the size of the partition (errno=%d)"),
                               pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive, errno);
            else
            {
                cbSize *= (uint64_t)cbBlockSize;
                dk_physical_extent_t dkPartExtent = {0};
                dkPartExtent.offset = 0;
                dkPartExtent.length = cbSize;
                if (ioctl(RTFileToNative(hRawPart), DKIOCGETPHYSICALEXTENT, (caddr_t)&dkPartExtent) == -1)
                    rc = vdIfError(pImage->pIfError, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                   N_("VMDK: Image path: '%s'. Partition #%u number ('%s') verification failed on '%s': Unable to obtain partition info (errno=%d)"),
                                   pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive, errno);
                else
                {
                    if (dkPartExtent.dev != StDrive.st_rdev)
                        rc = vdIfError(pImage->pIfError, VERR_MISMATCH, RT_SRC_POS,
                                       N_("VMDK: Image path: '%s'. Partition #%u path ('%s') verification failed on '%s': Drive does not contain the partition"),
                                       pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive);
                    else if (cbOffset != pPartDesc->offStartInVDisk)
                        rc = vdIfError(pImage->pIfError, VERR_MISMATCH, RT_SRC_POS,
                                       N_("VMDK: Image path: '%s'. Partition #%u path ('%s') verification failed on '%s': Start offset %RU64, expected %RU64"),
                                       pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive, cbOffset, pPartDesc->offStartInVDisk);
                    else if (cbSize != pPartDesc->cbData)
                        rc = vdIfError(pImage->pIfError, VERR_MISMATCH, RT_SRC_POS,
                                       N_("VMDK: Image path: '%s'. Partition #%u path ('%s') verification failed on '%s': Size %RU64, expected %RU64"),
                                       pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive, cbSize, pPartDesc->cbData);
                }
            }

            if (ioctl(RTFileToNative(hRawPart), DKIOCUNLOCKPHYSICALEXTENTS, NULL) == -1)
            {
                int rc2 = vdIfError(pImage->pIfError, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                    N_("VMDK: Image path: '%s'. Partition #%u number ('%s') verification failed on '%s': Unable to unlock the partition (errno=%d)"),
                                    pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive, errno);
                if (RT_SUCCESS(rc))
                    rc = rc2;
            }
        }
    }

#else
    RT_NOREF(hVol); /* PORTME */
    rc = VERR_NOT_SUPPORTED;
#endif
    if (RT_SUCCESS(rc))
    {
        /*
         * Compare the first 32 sectors of the partition.
         *
         * This might not be conclusive, but for partitions formatted with the more
         * common file systems it should be as they have a superblock copy at or near
         * the start of the partition (fat, fat32, ntfs, and ext4 does at least).
         */
        size_t const cbToCompare = (size_t)RT_MIN(pPartDesc->cbData / cbSector, 32) * cbSector;
        uint8_t *pbSector1 = (uint8_t *)RTMemTmpAlloc(cbToCompare * 2);
        if (pbSector1 != NULL)
        {
            uint8_t *pbSector2 = pbSector1 + cbToCompare;

            /* Do the comparing, we repeat if it fails and the data might be volatile. */
            uint64_t uPrevCrc1 = 0;
            uint64_t uPrevCrc2 = 0;
            uint32_t cStable   = 0;
            for (unsigned iTry = 0; iTry < 256; iTry++)
            {
                rc = RTFileReadAt(hRawDrive, pPartDesc->offStartInVDisk, pbSector1, cbToCompare, NULL);
                if (RT_SUCCESS(rc))
                {
                    rc = RTFileReadAt(hRawPart, pPartDesc->offStartInDevice, pbSector2, cbToCompare, NULL);
                    if (RT_SUCCESS(rc))
                    {
                        if (memcmp(pbSector1, pbSector2, cbToCompare) != 0)
                        {
                            rc = VERR_MISMATCH;

                            /* Do data stability checks before repeating: */
                            uint64_t const uCrc1 = RTCrc64(pbSector1, cbToCompare);
                            uint64_t const uCrc2 = RTCrc64(pbSector2, cbToCompare);
                            if (   uPrevCrc1 != uCrc1
                                || uPrevCrc2 != uCrc2)
                                cStable = 0;
                            else if (++cStable > 4)
                                break;
                            uPrevCrc1 = uCrc1;
                            uPrevCrc2 = uCrc2;
                            continue;
                        }
                        rc = VINF_SUCCESS;
                    }
                    else
                        rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                                       N_("VMDK: Image path: '%s'. Error reading %zu bytes from '%s' at offset %RU64 (%Rrc)"),
                                       pImage->pszFilename, cbToCompare, pPartDesc->pszRawDevice, pPartDesc->offStartInDevice, rc);
                }
                else
                    rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                                   N_("VMDK: Image path: '%s'. Error reading %zu bytes from '%s' at offset %RU64 (%Rrc)"),
                                   pImage->pszFilename, cbToCompare, pszRawDrive, pPartDesc->offStartInVDisk, rc);
                break;
            }
            if (rc == VERR_MISMATCH)
            {
                /* Find the first mismatching bytes: */
                size_t offMissmatch = 0;
                while (offMissmatch < cbToCompare && pbSector1[offMissmatch] == pbSector2[offMissmatch])
                    offMissmatch++;
                int cbSample = (int)RT_MIN(cbToCompare - offMissmatch, 16);

                if (cStable > 0)
                    rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                                   N_("VMDK: Image path: '%s'. Partition #%u path ('%s') verification failed on '%s' (cStable=%d @%#zx: %.*Rhxs vs %.*Rhxs)"),
                                   pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive, cStable,
                                   offMissmatch, cbSample, &pbSector1[offMissmatch], cbSample, &pbSector2[offMissmatch]);
                else
                {
                    LogRel(("VMDK: Image path: '%s'. Partition #%u path ('%s') verification undecided on '%s' because of unstable data! (@%#zx: %.*Rhxs vs %.*Rhxs)\n",
                            pImage->pszFilename, idxPartition, pPartDesc->pszRawDevice, pszRawDrive,
                            offMissmatch, cbSample, &pbSector1[offMissmatch], cbSample, &pbSector2[offMissmatch]));
                    rc = -rc;
                }
            }

            RTMemTmpFree(pbSector1);
        }
        else
            rc = vdIfError(pImage->pIfError, VERR_NO_TMP_MEMORY, RT_SRC_POS,
                           N_("VMDK: Image path: '%s'. Failed to allocate %zu bytes for a temporary read buffer\n"),
                           pImage->pszFilename, cbToCompare * 2);
    }
    RTFileClose(hRawPart);
    return rc;
}

#ifdef RT_OS_WINDOWS
/**
 * Construct the device name for the given partition number.
 */
static int vmdkRawDescWinMakePartitionName(PVMDKIMAGE pImage, const char *pszRawDrive, RTFILE hRawDrive, uint32_t idxPartition,
                                           char **ppszRawPartition)
{
    int                   rc         = VINF_SUCCESS;
    DWORD                 cbReturned = 0;
    STORAGE_DEVICE_NUMBER DevNum;
    RT_ZERO(DevNum);
    if (DeviceIoControl((HANDLE)RTFileToNative(hRawDrive), IOCTL_STORAGE_GET_DEVICE_NUMBER,
                        NULL /*pvInBuffer*/, 0 /*cbInBuffer*/, &DevNum, sizeof(DevNum), &cbReturned, NULL /*pOverlapped*/))
        RTStrAPrintf(ppszRawPartition, "\\\\.\\Harddisk%uPartition%u", DevNum.DeviceNumber, idxPartition);
    else
        rc = vdIfError(pImage->pIfError, RTErrConvertFromWin32(GetLastError()), RT_SRC_POS,
                       N_("VMDK: Image path: '%s'. IOCTL_STORAGE_GET_DEVICE_NUMBER failed on '%s': %u"),
                       pImage->pszFilename, pszRawDrive, GetLastError());
    return rc;
}
#endif /* RT_OS_WINDOWS */

/**
 * Worker for vmdkMakeRawDescriptor that adds partition descriptors when the
 * 'Partitions' configuration value is present.
 *
 * @returns VBox status code, error message has been set on failure.
 *
 * @note    Caller is assumed to clean up @a pRawDesc and release
 *          @a *phVolToRelease.
 * @internal
 */
static int vmdkRawDescDoPartitions(PVMDKIMAGE pImage, RTDVM hVolMgr, PVDISKRAW pRawDesc,
                                   RTFILE hRawDrive, const char *pszRawDrive, uint32_t cbSector,
                                   uint32_t fPartitions, uint32_t fPartitionsReadOnly, bool fRelative,
                                   PRTDVMVOLUME phVolToRelease)
{
    *phVolToRelease = NIL_RTDVMVOLUME;

    /* Check sanity/understanding. */
    Assert(fPartitions);
    Assert((fPartitions & fPartitionsReadOnly) == fPartitionsReadOnly); /* RO should be a sub-set */

    /*
     * Allocate on descriptor for each volume up front.
     */
    uint32_t const cVolumes = RTDvmMapGetValidVolumes(hVolMgr);

    PVDISKRAWPARTDESC paPartDescs = NULL;
    int rc = vmdkRawDescAppendPartDesc(pImage, pRawDesc, cVolumes, &paPartDescs);
    AssertRCReturn(rc, rc);

    /*
     * Enumerate the partitions (volumes) on the disk and create descriptors for each of them.
     */
    uint32_t    fPartitionsLeft = fPartitions;
    RTDVMVOLUME hVol            = NIL_RTDVMVOLUME; /* the current volume, needed for getting the next. */
    for (uint32_t i = 0; i < cVolumes; i++)
    {
        /*
         * Get the next/first volume and release the current.
         */
        RTDVMVOLUME hVolNext = NIL_RTDVMVOLUME;
        if (i == 0)
            rc = RTDvmMapQueryFirstVolume(hVolMgr, &hVolNext);
        else
            rc = RTDvmMapQueryNextVolume(hVolMgr, hVol, &hVolNext);
        if (RT_FAILURE(rc))
            return vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                             N_("VMDK: Image path: '%s'. Volume enumeration failed at volume #%u on '%s' (%Rrc)"),
                             pImage->pszFilename, i, pszRawDrive, rc);
        uint32_t cRefs = RTDvmVolumeRelease(hVol);
        Assert(cRefs != UINT32_MAX); RT_NOREF(cRefs);
        *phVolToRelease = hVol = hVolNext;

        /*
         * Depending on the fPartitions selector and associated read-only mask,
         * the guest either gets read-write or read-only access (bits set)
         * or no access (selector bit clear, access directed to the VMDK).
         */
        paPartDescs[i].cbData = RTDvmVolumeGetSize(hVol);

        uint64_t offVolumeEndIgnored = 0;
        rc = RTDvmVolumeQueryRange(hVol, &paPartDescs[i].offStartInVDisk, &offVolumeEndIgnored);
        if (RT_FAILURE(rc))
            return vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                             N_("VMDK: Image path: '%s'. Failed to get location of volume #%u on '%s' (%Rrc)"),
                             pImage->pszFilename, i, pszRawDrive, rc);
        Assert(paPartDescs[i].cbData == offVolumeEndIgnored + 1 - paPartDescs[i].offStartInVDisk);

        /* Note! The index must match IHostDrivePartition::number. */
        uint32_t idxPartition = RTDvmVolumeGetIndex(hVol, RTDVMVOLIDX_HOST);
        if (   idxPartition < 32
            && (fPartitions & RT_BIT_32(idxPartition)))
        {
            fPartitionsLeft &= ~RT_BIT_32(idxPartition);
            if (fPartitionsReadOnly & RT_BIT_32(idxPartition))
                paPartDescs[i].uFlags |= VDISKRAW_READONLY;

            if (!fRelative)
            {
                /*
                 * Accessing the drive thru the main device node (pRawDesc->pszRawDisk).
                 */
                paPartDescs[i].offStartInDevice = paPartDescs[i].offStartInVDisk;
                paPartDescs[i].pszRawDevice     = RTStrDup(pszRawDrive);
                AssertPtrReturn(paPartDescs[i].pszRawDevice, VERR_NO_STR_MEMORY);
            }
            else
            {
                /*
                 * Relative means access the partition data via the device node for that
                 * partition, allowing the sysadmin/OS to allow a user access to individual
                 * partitions without necessarily being able to compromise the host OS.
                 * Obviously, the creation of the VMDK requires read access to the main
                 * device node for the drive, but that's a one-time thing and can be done
                 * by the sysadmin.   Here data starts at offset zero in the device node.
                 */
                paPartDescs[i].offStartInDevice = 0;

#if defined(RT_OS_DARWIN) || defined(RT_OS_FREEBSD)
                /* /dev/rdisk1 -> /dev/rdisk1s2 (s=slice) */
                RTStrAPrintf(&paPartDescs[i].pszRawDevice, "%ss%u", pszRawDrive, idxPartition);
#elif defined(RT_OS_LINUX)
                /* Two naming schemes here: /dev/nvme0n1 -> /dev/nvme0n1p1;  /dev/sda -> /dev/sda1 */
                RTStrAPrintf(&paPartDescs[i].pszRawDevice,
                             RT_C_IS_DIGIT(pszRawDrive[strlen(pszRawDrive) - 1]) ? "%sp%u" : "%s%u", pszRawDrive, idxPartition);
#elif defined(RT_OS_WINDOWS)
                rc = vmdkRawDescWinMakePartitionName(pImage, pszRawDrive, hRawDrive, idxPartition, &paPartDescs[i].pszRawDevice);
                AssertRCReturn(rc, rc);
#elif defined(RT_OS_SOLARIS)
                if (pRawDesc->enmPartitioningType == VDISKPARTTYPE_MBR)
                {
                    /*
                     * MBR partitions have device nodes in form /dev/(r)dsk/cXtYdZpK
                     * where X is the controller,
                     *       Y is target (SCSI device number),
                     *       Z is disk number,
                     *       K is partition number,
                     *         where p0 is the whole disk
                     *               p1-pN are the partitions of the disk
                     */
                    const char *pszRawDrivePath = pszRawDrive;
                    char szDrivePath[RTPATH_MAX];
                    size_t cbRawDrive = strlen(pszRawDrive);
                    if (  cbRawDrive > 1 && strcmp(&pszRawDrive[cbRawDrive - 2], "p0") == 0)
                    {
                        memcpy(szDrivePath, pszRawDrive, cbRawDrive - 2);
                        szDrivePath[cbRawDrive - 2] = '\0';
                        pszRawDrivePath = szDrivePath;
                    }
                    RTStrAPrintf(&paPartDescs[i].pszRawDevice, "%sp%u", pszRawDrivePath, idxPartition);
                }
                else /* GPT */
                {
                    /*
                     * GPT partitions have device nodes in form /dev/(r)dsk/cXtYdZsK
                     * where X is the controller,
                     *       Y is target (SCSI device number),
                     *       Z is disk number,
                     *       K is partition number, zero based. Can be only from 0 to 6.
                     *         Thus, only partitions numbered 0 through 6 have device nodes.
                     */
                    if (idxPartition > 7)
                        return vdIfError(pImage->pIfError, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                         N_("VMDK: Image path: '%s'. the partition #%u on '%s' has no device node and can not be specified with 'Relative' property"),
                                         pImage->pszFilename, idxPartition, pszRawDrive);
                    RTStrAPrintf(&paPartDescs[i].pszRawDevice, "%ss%u", pszRawDrive, idxPartition - 1);
                }
#else
                AssertFailedReturn(VERR_INTERNAL_ERROR_4); /* The option parsing code should have prevented this - PORTME */
#endif
                AssertPtrReturn(paPartDescs[i].pszRawDevice, VERR_NO_STR_MEMORY);

                rc = vmdkRawDescVerifyPartitionPath(pImage, &paPartDescs[i], idxPartition, pszRawDrive, hRawDrive, cbSector, hVol);
                AssertRCReturn(rc, rc);
            }
        }
        else
        {
            /* Not accessible to the guest. */
            paPartDescs[i].offStartInDevice = 0;
            paPartDescs[i].pszRawDevice     = NULL;
        }
    } /* for each volume */

    RTDvmVolumeRelease(hVol);
    *phVolToRelease = NIL_RTDVMVOLUME;

    /*
     * Check that we found all the partitions the user selected.
     */
    if (fPartitionsLeft)
    {
        char   szLeft[3 * sizeof(fPartitions) * 8];
        size_t cchLeft = 0;
        for (unsigned i = 0; i < sizeof(fPartitions) * 8; i++)
            if (fPartitionsLeft & RT_BIT_32(i))
                cchLeft += RTStrPrintf(&szLeft[cchLeft], sizeof(szLeft) - cchLeft, cchLeft ? "%u" : ",%u", i);
        return vdIfError(pImage->pIfError, VERR_INVALID_PARAMETER, RT_SRC_POS,
                             N_("VMDK: Image path: '%s'. Not all the specified partitions for drive '%s' was found: %s"),
                             pImage->pszFilename, pszRawDrive, szLeft);
    }

    return VINF_SUCCESS;
}

/**
 * Worker for vmdkMakeRawDescriptor that adds partition descriptors with copies
 * of the partition tables and associated padding areas when the 'Partitions'
 * configuration value is present.
 *
 * The guest is not allowed access to the partition tables, however it needs
 * them to be able to access the drive.  So, create descriptors for each of the
 * tables and attach the current disk content.  vmdkCreateRawImage() will later
 * write the content to the VMDK.  Any changes the guest later makes to the
 * partition tables will then go to the VMDK copy, rather than the host drive.
 *
 * @returns VBox status code, error message has been set on failure.
 *
 * @note    Caller is assumed to clean up @a pRawDesc
 * @internal
 */
static int vmdkRawDescDoCopyPartitionTables(PVMDKIMAGE pImage, RTDVM hVolMgr, PVDISKRAW pRawDesc,
                                            const char *pszRawDrive, RTFILE hRawDrive, void *pvBootSector, size_t cbBootSector)
{
    /*
     * Query the locations.
     */
    /* Determin how many locations there are: */
    size_t cLocations = 0;
    int rc = RTDvmMapQueryTableLocations(hVolMgr, RTDVMMAPQTABLOC_F_INCLUDE_LEGACY, NULL, 0, &cLocations);
    if (rc != VERR_BUFFER_OVERFLOW)
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                         N_("VMDK: Image path: '%s'. RTDvmMapQueryTableLocations failed on '%s' (%Rrc)"),
                         pImage->pszFilename, pszRawDrive, rc);
    AssertReturn(cLocations > 0 && cLocations < _16M, VERR_INTERNAL_ERROR_5);

    /* We can allocate the partition descriptors here to save an intentation level. */
    PVDISKRAWPARTDESC paPartDescs = NULL;
    rc = vmdkRawDescAppendPartDesc(pImage, pRawDesc, (uint32_t)cLocations, &paPartDescs);
    AssertRCReturn(rc, rc);

    /* Allocate the result table and repeat the location table query: */
    PRTDVMTABLELOCATION paLocations = (PRTDVMTABLELOCATION)RTMemAllocZ(sizeof(paLocations[0]) * cLocations);
    if (!paLocations)
        return vdIfError(pImage->pIfError, VERR_NO_MEMORY, RT_SRC_POS, N_("VMDK: Image path: '%s'. Failed to allocate %zu bytes"),
                         pImage->pszFilename, sizeof(paLocations[0]) * cLocations);
    rc = RTDvmMapQueryTableLocations(hVolMgr, RTDVMMAPQTABLOC_F_INCLUDE_LEGACY, paLocations, cLocations, NULL);
    if (RT_SUCCESS(rc))
    {
        /*
         * Translate them into descriptors.
         *
         * We restrict the amount of partition alignment padding to 4MiB as more
         * will just be a waste of space.  The use case for including the padding
         * are older boot loaders and boot manager (including one by a team member)
         * that put data and code in the 62 sectors between the MBR and the first
         * partition (total of 63).  Later CHS was abandond and partition started
         * being aligned on power of two sector boundraries (typically 64KiB or
         * 1MiB depending on the media size).
         */
        for (size_t i = 0; i < cLocations && RT_SUCCESS(rc); i++)
        {
            Assert(paLocations[i].cb > 0);
            if (paLocations[i].cb <= _64M)
            {
                /* Create the partition descriptor entry: */
                //paPartDescs[i].pszRawDevice      = NULL;
                //paPartDescs[i].offStartInDevice  = 0;
                //paPartDescs[i].uFlags            = 0;
                paPartDescs[i].offStartInVDisk   = paLocations[i].off;
                paPartDescs[i].cbData            = paLocations[i].cb;
                if (paPartDescs[i].cbData < _4M)
                    paPartDescs[i].cbData = RT_MIN(paPartDescs[i].cbData + paLocations[i].cbPadding, _4M);
                paPartDescs[i].pvPartitionData = RTMemAllocZ((size_t)paPartDescs[i].cbData);
                if (paPartDescs[i].pvPartitionData)
                {
                    /* Read the content from the drive: */
                    rc = RTFileReadAt(hRawDrive, paPartDescs[i].offStartInVDisk, paPartDescs[i].pvPartitionData,
                                      (size_t)paPartDescs[i].cbData, NULL);
                    if (RT_SUCCESS(rc))
                    {
                        /* Do we have custom boot sector code? */
                        if (pvBootSector && cbBootSector && paPartDescs[i].offStartInVDisk == 0)
                        {
                            /* Note! Old code used to quietly drop the bootsector if it was considered too big.
                                     Instead we fail as we weren't able to do what the user requested us to do.
                                     Better if the user knows than starts questioning why the guest isn't
                                     booting as expected. */
                            if (cbBootSector <= paPartDescs[i].cbData)
                                memcpy(paPartDescs[i].pvPartitionData, pvBootSector, cbBootSector);
                            else
                                rc = vdIfError(pImage->pIfError, VERR_TOO_MUCH_DATA, RT_SRC_POS,
                                               N_("VMDK: Image path: '%s'. The custom boot sector is too big: %zu bytes, %RU64 bytes available"),
                                               pImage->pszFilename, cbBootSector, paPartDescs[i].cbData);
                        }
                    }
                    else
                        rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                                       N_("VMDK: Image path: '%s'. Failed to read partition at off %RU64 length %zu from '%s' (%Rrc)"),
                                       pImage->pszFilename, paPartDescs[i].offStartInVDisk,
                                       (size_t)paPartDescs[i].cbData, pszRawDrive, rc);
                }
                else
                    rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                                   N_("VMDK: Image path: '%s'. Failed to allocate %zu bytes for copying the partition table at off %RU64"),
                                      pImage->pszFilename, (size_t)paPartDescs[i].cbData, paPartDescs[i].offStartInVDisk);
            }
            else
                rc = vdIfError(pImage->pIfError, VERR_TOO_MUCH_DATA, RT_SRC_POS,
                               N_("VMDK: Image path: '%s'. Partition table #%u at offset %RU64 in '%s' is to big: %RU64 bytes"),
                                  pImage->pszFilename, i, paLocations[i].off, pszRawDrive, paLocations[i].cb);
        }
    }
    else
        rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                       N_("VMDK: Image path: '%s'. RTDvmMapQueryTableLocations failed on '%s' (%Rrc)"),
                          pImage->pszFilename, pszRawDrive, rc);
    RTMemFree(paLocations);
    return rc;
}

/**
 * Opens the volume manager for the raw drive when in selected-partition mode.
 *
 * @param   pImage      The VMDK image (for errors).
 * @param   hRawDrive   The raw drive handle.
 * @param   pszRawDrive The raw drive device path (for errors).
 * @param   cbSector    The sector size.
 * @param   phVolMgr    Where to return the handle to the volume manager on
 *                      success.
 * @returns VBox status code, errors have been reported.
 * @internal
 */
static int vmdkRawDescOpenVolMgr(PVMDKIMAGE pImage, RTFILE hRawDrive, const char *pszRawDrive, uint32_t cbSector, PRTDVM phVolMgr)
{
    *phVolMgr = NIL_RTDVM;

    RTVFSFILE hVfsFile = NIL_RTVFSFILE;
    int rc = RTVfsFileFromRTFile(hRawDrive, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE, true /*fLeaveOpen*/, &hVfsFile);
    if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                         N_("VMDK: Image path: '%s'.  RTVfsFileFromRTFile failed for '%s' handle (%Rrc)"),
                         pImage->pszFilename, pszRawDrive, rc);

    RTDVM hVolMgr = NIL_RTDVM;
    rc = RTDvmCreate(&hVolMgr, hVfsFile, cbSector, 0 /*fFlags*/);

    RTVfsFileRelease(hVfsFile);

    if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                         N_("VMDK: Image path: '%s'. Failed to create volume manager instance for '%s' (%Rrc)"),
                         pImage->pszFilename, pszRawDrive, rc);

    rc = RTDvmMapOpen(hVolMgr);
    if (RT_SUCCESS(rc))
    {
        *phVolMgr = hVolMgr;
        return VINF_SUCCESS;
    }
    RTDvmRelease(hVolMgr);
    return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: Image path: '%s'. RTDvmMapOpen failed for '%s' (%Rrc)"),
                     pImage->pszFilename, pszRawDrive, rc);
}

/**
 * Opens the raw drive device and get the sizes for it.
 *
 * @param   pImage          The image (for error reporting).
 * @param   pszRawDrive     The device/whatever to open.
 * @param   phRawDrive      Where to return the file handle.
 * @param   pcbRawDrive     Where to return the size.
 * @param   pcbSector       Where to return the sector size.
 * @returns IPRT status code, errors have been reported.
 * @internal
 */
static int vmkdRawDescOpenDevice(PVMDKIMAGE pImage, const char *pszRawDrive,
                                 PRTFILE phRawDrive, uint64_t *pcbRawDrive, uint32_t *pcbSector)
{
    /*
     * Open the device for the raw drive.
     */
    RTFILE hRawDrive = NIL_RTFILE;
    int rc = RTFileOpen(&hRawDrive, pszRawDrive, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE);
    if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                         N_("VMDK: Image path: '%s'. Failed to open the raw drive '%s' for reading (%Rrc)"),
                         pImage->pszFilename, pszRawDrive, rc);

    /*
     * Get the sector size.
     */
    uint32_t cbSector = 0;
    rc = RTFileQuerySectorSize(hRawDrive, &cbSector);
    if (RT_SUCCESS(rc))
    {
        /* sanity checks */
        if (   cbSector >= 512
            && cbSector <= _64K
            && RT_IS_POWER_OF_TWO(cbSector))
        {
            /*
             * Get the size.
             */
            uint64_t cbRawDrive = 0;
            rc = RTFileQuerySize(hRawDrive, &cbRawDrive);
            if (RT_SUCCESS(rc))
            {
                /* Check whether cbSize is actually sensible. */
                if (cbRawDrive > cbSector && (cbRawDrive % cbSector) == 0)
                {
                    *phRawDrive  = hRawDrive;
                    *pcbRawDrive = cbRawDrive;
                    *pcbSector   = cbSector;
                    return VINF_SUCCESS;
                }
                rc = vdIfError(pImage->pIfError, VERR_INVALID_PARAMETER, RT_SRC_POS,
                               N_("VMDK: Image path: '%s'.  Got a bogus size for the raw drive '%s': %RU64 (sector size %u)"),
                               pImage->pszFilename, pszRawDrive, cbRawDrive, cbSector);
            }
            else
                rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                               N_("VMDK: Image path: '%s'. Failed to query size of the drive '%s' (%Rrc)"),
                               pImage->pszFilename, pszRawDrive, rc);
        }
        else
            rc = vdIfError(pImage->pIfError, VERR_OUT_OF_RANGE, RT_SRC_POS,
                           N_("VMDK: Image path: '%s'. Unsupported sector size for '%s': %u (%#x)"),
                           pImage->pszFilename, pszRawDrive, cbSector, cbSector);
    }
    else
        rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                       N_("VMDK: Image path: '%s'. Failed to get the sector size for '%s' (%Rrc)"),
                       pImage->pszFilename, pszRawDrive, rc);
    RTFileClose(hRawDrive);
    return rc;
}

/**
 * Reads the raw disk configuration, leaving initalization and cleanup to the
 * caller (regardless of return status).
 *
 * @returns VBox status code, errors properly reported.
 * @internal
 */
static int vmdkRawDescParseConfig(PVMDKIMAGE pImage, char **ppszRawDrive,
                                  uint32_t *pfPartitions, uint32_t *pfPartitionsReadOnly,
                                  void **ppvBootSector, size_t *pcbBootSector, bool *pfRelative,
                                  char **ppszFreeMe)
{
    PVDINTERFACECONFIG pImgCfg = VDIfConfigGet(pImage->pVDIfsImage);
    if (!pImgCfg)
        return vdIfError(pImage->pIfError, VERR_INVALID_PARAMETER, RT_SRC_POS,
                         N_("VMDK: Image path: '%s'. Getting config interface failed"), pImage->pszFilename);

    /*
     * RawDrive = path
     */
    int rc = VDCFGQueryStringAlloc(pImgCfg, "RawDrive", ppszRawDrive);
    if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                         N_("VMDK: Image path: '%s'. Getting 'RawDrive' configuration failed (%Rrc)"), pImage->pszFilename, rc);
    AssertPtrReturn(*ppszRawDrive, VERR_INTERNAL_ERROR_3);

    /*
     * Partitions=n[r][,...]
     */
    uint32_t const cMaxPartitionBits = sizeof(*pfPartitions) * 8 /* ASSUMES 8 bits per char */;
    *pfPartitions = *pfPartitionsReadOnly = 0;

    rc = VDCFGQueryStringAlloc(pImgCfg, "Partitions", ppszFreeMe);
    if (RT_SUCCESS(rc))
    {
        char *psz = *ppszFreeMe;
        while (*psz != '\0')
        {
            char *pszNext;
            uint32_t u32;
            rc = RTStrToUInt32Ex(psz, &pszNext, 0, &u32);
            if (rc == VWRN_NUMBER_TOO_BIG || rc == VWRN_NEGATIVE_UNSIGNED)
                rc = -rc;
            if (RT_FAILURE(rc))
                return vdIfError(pImage->pIfError, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                 N_("VMDK: Image path: '%s'. Parsing 'Partitions' config value failed. Incorrect value (%Rrc): %s"),
                                 pImage->pszFilename, rc, psz);
            if (u32 >= cMaxPartitionBits)
                return vdIfError(pImage->pIfError, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                 N_("VMDK: Image path: '%s'. 'Partitions' config sub-value out of range: %RU32, max %RU32"),
                                 pImage->pszFilename, u32, cMaxPartitionBits);
            *pfPartitions |= RT_BIT_32(u32);
            psz = pszNext;
            if (*psz == 'r')
            {
                *pfPartitionsReadOnly |= RT_BIT_32(u32);
                psz++;
            }
            if (*psz == ',')
                psz++;
            else if (*psz != '\0')
                return vdIfError(pImage->pIfError, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                 N_("VMDK: Image path: '%s'. Malformed 'Partitions' config value, expected separator: %s"),
                                 pImage->pszFilename, psz);
        }

        RTStrFree(*ppszFreeMe);
        *ppszFreeMe = NULL;
    }
    else if (rc != VERR_CFGM_VALUE_NOT_FOUND)
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                         N_("VMDK: Image path: '%s'. Getting 'Partitions' configuration failed (%Rrc)"), pImage->pszFilename, rc);

    /*
     * BootSector=base64
     */
    rc = VDCFGQueryStringAlloc(pImgCfg, "BootSector", ppszFreeMe);
    if (RT_SUCCESS(rc))
    {
        ssize_t cbBootSector = RTBase64DecodedSize(*ppszFreeMe, NULL);
        if (cbBootSector < 0)
            return vdIfError(pImage->pIfError, VERR_INVALID_BASE64_ENCODING, RT_SRC_POS,
                             N_("VMDK: Image path: '%s'. BASE64 decoding failed on the custom bootsector for '%s'"),
                             pImage->pszFilename, *ppszRawDrive);
        if (cbBootSector == 0)
            return vdIfError(pImage->pIfError, VERR_INVALID_PARAMETER, RT_SRC_POS,
                             N_("VMDK: Image path: '%s'. Custom bootsector for '%s' is zero bytes big"),
                             pImage->pszFilename, *ppszRawDrive);
        if (cbBootSector > _4M) /* this is just a preliminary max */
            return vdIfError(pImage->pIfError, VERR_INVALID_PARAMETER, RT_SRC_POS,
                             N_("VMDK: Image path: '%s'. Custom bootsector for '%s' is way too big: %zu bytes, max 4MB"),
                             pImage->pszFilename, *ppszRawDrive, cbBootSector);

        /* Refuse the boot sector if whole-drive.  This used to be done quietly,
           however, bird disagrees and thinks the user should be told that what
           he/she/it tries to do isn't possible.  There should be less head
           scratching this way when the guest doesn't do the expected thing. */
        if (!*pfPartitions)
            return vdIfError(pImage->pIfError, VERR_INVALID_PARAMETER, RT_SRC_POS,
                             N_("VMDK: Image path: '%s'. Custom bootsector for '%s' is not supported for whole-drive configurations, only when selecting partitions"),
                             pImage->pszFilename, *ppszRawDrive);

        *pcbBootSector = (size_t)cbBootSector;
        *ppvBootSector = RTMemAlloc((size_t)cbBootSector);
        if (!*ppvBootSector)
            return vdIfError(pImage->pIfError, VERR_NO_MEMORY, RT_SRC_POS,
                             N_("VMDK: Image path: '%s'. Failed to allocate %zd bytes for the custom bootsector for '%s'"),
                             pImage->pszFilename, cbBootSector, *ppszRawDrive);

        rc = RTBase64Decode(*ppszFreeMe, *ppvBootSector, cbBootSector, NULL /*pcbActual*/, NULL /*ppszEnd*/);
        if (RT_FAILURE(rc))
            return  vdIfError(pImage->pIfError, VERR_NO_MEMORY, RT_SRC_POS,
                              N_("VMDK: Image path: '%s'. Base64 decoding of the custom boot sector for '%s' failed (%Rrc)"),
                              pImage->pszFilename, *ppszRawDrive, rc);

        RTStrFree(*ppszFreeMe);
        *ppszFreeMe = NULL;
    }
    else if (rc != VERR_CFGM_VALUE_NOT_FOUND)
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                         N_("VMDK: Image path: '%s'. Getting 'BootSector' configuration failed (%Rrc)"), pImage->pszFilename, rc);

    /*
     * Relative=0/1
     */
    *pfRelative = false;
    rc = VDCFGQueryBool(pImgCfg, "Relative", pfRelative);
    if (RT_SUCCESS(rc))
    {
        if (!*pfPartitions && *pfRelative != false)
            return vdIfError(pImage->pIfError, VERR_INVALID_PARAMETER, RT_SRC_POS,
                             N_("VMDK: Image path: '%s'. The 'Relative' option is not supported for whole-drive configurations, only when selecting partitions"),
                             pImage->pszFilename);
#if !defined(RT_OS_DARWIN) && !defined(RT_OS_LINUX) && !defined(RT_OS_FREEBSD) && !defined(RT_OS_WINDOWS) && !defined(RT_OS_SOLARIS) /* PORTME */
        if (*pfRelative == true)
            return vdIfError(pImage->pIfError, VERR_INVALID_PARAMETER, RT_SRC_POS,
                             N_("VMDK: Image path: '%s'. The 'Relative' option is not supported on this host OS"),
                             pImage->pszFilename);
#endif
    }
    else if (rc != VERR_CFGM_VALUE_NOT_FOUND)
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                         N_("VMDK: Image path: '%s'. Getting 'Relative' configuration failed (%Rrc)"), pImage->pszFilename, rc);
    else
#ifdef RT_OS_DARWIN /* different default on macOS, see ticketref:1461 (comment 20). */
        *pfRelative = true;
#else
        *pfRelative = false;
#endif

    return VINF_SUCCESS;
}

/**
 * Creates a raw drive (nee disk) descriptor.
 *
 * This was originally done in VBoxInternalManage.cpp, but was copied (not move)
 * here much later.  That's one of the reasons why we produce a descriptor just
 * like it does, rather than mixing directly into the vmdkCreateRawImage code.
 *
 * @returns VBox status code.
 * @param   pImage      The image.
 * @param   ppRaw       Where to return the raw drive descriptor.  Caller must
 *                      free it using vmdkRawDescFree regardless of the status
 *                      code.
 * @internal
 */
static int vmdkMakeRawDescriptor(PVMDKIMAGE pImage, PVDISKRAW *ppRaw)
{
    /* Make sure it's NULL. */
    *ppRaw = NULL;

    /*
     * Read the configuration.
     */
    char       *pszRawDrive         = NULL;
    uint32_t    fPartitions         = 0;    /* zero if whole-drive */
    uint32_t    fPartitionsReadOnly = 0;    /* (subset of fPartitions) */
    void       *pvBootSector        = NULL;
    size_t      cbBootSector        = 0;
    bool        fRelative           = false;
    char       *pszFreeMe           = NULL; /* lazy bird cleanup. */
    int rc = vmdkRawDescParseConfig(pImage, &pszRawDrive, &fPartitions, &fPartitionsReadOnly,
                                    &pvBootSector, &cbBootSector, &fRelative, &pszFreeMe);
    RTStrFree(pszFreeMe);
    if (RT_SUCCESS(rc))
    {
        /*
         * Open the device, getting the sector size and drive size.
         */
        uint64_t  cbSize    = 0;
        uint32_t  cbSector  = 0;
        RTFILE    hRawDrive = NIL_RTFILE;
        rc = vmkdRawDescOpenDevice(pImage, pszRawDrive, &hRawDrive, &cbSize, &cbSector);
        if (RT_SUCCESS(rc))
        {
            pImage->cbSize = cbSize;
            /*
             * Create the raw-drive descriptor
             */
            PVDISKRAW pRawDesc = (PVDISKRAW)RTMemAllocZ(sizeof(*pRawDesc));
            if (pRawDesc)
            {
                pRawDesc->szSignature[0] = 'R';
                pRawDesc->szSignature[1] = 'A';
                pRawDesc->szSignature[2] = 'W';
                //pRawDesc->szSignature[3] = '\0';
                if (!fPartitions)
                {
                    /*
                     * It's simple for when doing the whole drive.
                     */
                    pRawDesc->uFlags = VDISKRAW_DISK;
                    rc = RTStrDupEx(&pRawDesc->pszRawDisk, pszRawDrive);
                }
                else
                {
                    /*
                     * In selected partitions mode we've got a lot more work ahead of us.
                     */
                    pRawDesc->uFlags = VDISKRAW_NORMAL;
                    //pRawDesc->pszRawDisk = NULL;
                    //pRawDesc->cPartDescs = 0;
                    //pRawDesc->pPartDescs = NULL;

                    /* We need to parse the partition map to complete the descriptor: */
                    RTDVM hVolMgr = NIL_RTDVM;
                    rc = vmdkRawDescOpenVolMgr(pImage, hRawDrive, pszRawDrive, cbSector, &hVolMgr);
                    if (RT_SUCCESS(rc))
                    {
                        RTDVMFORMATTYPE enmFormatType = RTDvmMapGetFormatType(hVolMgr);
                        if (   enmFormatType == RTDVMFORMATTYPE_MBR
                            || enmFormatType == RTDVMFORMATTYPE_GPT)
                        {
                            pRawDesc->enmPartitioningType = enmFormatType == RTDVMFORMATTYPE_MBR
                                                          ? VDISKPARTTYPE_MBR : VDISKPARTTYPE_GPT;

                            /* Add copies of the partition tables:  */
                            rc = vmdkRawDescDoCopyPartitionTables(pImage, hVolMgr, pRawDesc, pszRawDrive, hRawDrive,
                                                                  pvBootSector, cbBootSector);
                            if (RT_SUCCESS(rc))
                            {
                                /* Add descriptors for the partitions/volumes, indicating which
                                   should be accessible and how to access them: */
                                RTDVMVOLUME hVolRelease = NIL_RTDVMVOLUME;
                                rc = vmdkRawDescDoPartitions(pImage, hVolMgr, pRawDesc, hRawDrive, pszRawDrive, cbSector,
                                                             fPartitions, fPartitionsReadOnly, fRelative, &hVolRelease);
                                RTDvmVolumeRelease(hVolRelease);

                                /* Finally, sort the partition and check consistency (overlaps, etc): */
                                if (RT_SUCCESS(rc))
                                    rc = vmdkRawDescPostProcessPartitions(pImage, pRawDesc, cbSize);
                            }
                        }
                        else
                            rc = vdIfError(pImage->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                                           N_("VMDK: Image path: '%s'. Unsupported partitioning for the disk '%s': %s"),
                                           pImage->pszFilename, pszRawDrive, RTDvmMapGetFormatType(hVolMgr));
                        RTDvmRelease(hVolMgr);
                    }
                }
                if (RT_SUCCESS(rc))
                {
                    /*
                     * We succeeded.
                     */
                    *ppRaw = pRawDesc;
                    Log(("vmdkMakeRawDescriptor: fFlags=%#x enmPartitioningType=%d cPartDescs=%u pszRawDisk=%s\n",
                         pRawDesc->uFlags, pRawDesc->enmPartitioningType, pRawDesc->cPartDescs, pRawDesc->pszRawDisk));
                    if (pRawDesc->cPartDescs)
                    {
                        Log(("#      VMDK offset         Length  Device offset  PartDataPtr  Device\n"));
                        for (uint32_t i = 0; i < pRawDesc->cPartDescs; i++)
                            Log(("%2u  %14RU64 %14RU64 %14RU64 %#18p %s\n", i, pRawDesc->pPartDescs[i].offStartInVDisk,
                                 pRawDesc->pPartDescs[i].cbData, pRawDesc->pPartDescs[i].offStartInDevice,
                                 pRawDesc->pPartDescs[i].pvPartitionData, pRawDesc->pPartDescs[i].pszRawDevice));
                    }
                }
                else
                    vmdkRawDescFree(pRawDesc);
            }
            else
                rc = vdIfError(pImage->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                               N_("VMDK: Image path: '%s'. Failed to allocate %u bytes for the raw drive descriptor"),
                               pImage->pszFilename, sizeof(*pRawDesc));
            RTFileClose(hRawDrive);
        }
    }
    RTStrFree(pszRawDrive);
    RTMemFree(pvBootSector);
    return rc;
}

/**
 * Internal: create VMDK images for raw disk/partition access.
 */
static int vmdkCreateRawImage(PVMDKIMAGE pImage, const PVDISKRAW pRaw,
                              uint64_t cbSize)
{
    int rc = VINF_SUCCESS;
    PVMDKEXTENT pExtent;

    if (pRaw->uFlags & VDISKRAW_DISK)
    {
        /* Full raw disk access. This requires setting up a descriptor
         * file and open the (flat) raw disk. */
        rc = vmdkCreateExtents(pImage, 1);
        if (RT_FAILURE(rc))
            return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not create new extent list in '%s'"), pImage->pszFilename);
        pExtent = &pImage->pExtents[0];
        /* Create raw disk descriptor file. */
        rc = vmdkFileOpen(pImage, &pImage->pFile, NULL, pImage->pszFilename,
                          VDOpenFlagsToFileOpenFlags(pImage->uOpenFlags,
                                                     true /* fCreate */));
        if (RT_FAILURE(rc))
            return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not create new file '%s'"), pImage->pszFilename);

        /* Set up basename for extent description. Cannot use StrDup. */
        size_t cbBasename = strlen(pRaw->pszRawDisk) + 1;
        char *pszBasename = (char *)RTMemTmpAlloc(cbBasename);
        if (!pszBasename)
            return VERR_NO_MEMORY;
        memcpy(pszBasename, pRaw->pszRawDisk, cbBasename);
        pExtent->pszBasename = pszBasename;
        /* For raw disks the full name is identical to the base name. */
        pExtent->pszFullname = RTStrDup(pszBasename);
        if (!pExtent->pszFullname)
            return VERR_NO_MEMORY;
        pExtent->enmType = VMDKETYPE_FLAT;
        pExtent->cNominalSectors = VMDK_BYTE2SECTOR(cbSize);
        pExtent->uSectorOffset = 0;
        pExtent->enmAccess = (pRaw->uFlags & VDISKRAW_READONLY) ? VMDKACCESS_READONLY : VMDKACCESS_READWRITE;
        pExtent->fMetaDirty = false;

        /* Open flat image, the raw disk. */
        rc = vmdkFileOpen(pImage, &pExtent->pFile, pExtent->pszBasename, pExtent->pszFullname,
                          VDOpenFlagsToFileOpenFlags(pImage->uOpenFlags | ((pExtent->enmAccess == VMDKACCESS_READONLY) ? VD_OPEN_FLAGS_READONLY : 0),
                                                     false /* fCreate */));
        if (RT_FAILURE(rc))
            return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not open raw disk file '%s'"), pExtent->pszFullname);
    }
    else
    {
        /* Raw partition access. This requires setting up a descriptor
         * file, write the partition information to a flat extent and
         * open all the (flat) raw disk partitions. */

        /* First pass over the partition data areas to determine how many
         * extents we need. One data area can require up to 2 extents, as
         * it might be necessary to skip over unpartitioned space. */
        unsigned cExtents = 0;
        uint64_t uStart = 0;
        for (unsigned i = 0; i < pRaw->cPartDescs; i++)
        {
            PVDISKRAWPARTDESC pPart = &pRaw->pPartDescs[i];
            if (uStart > pPart->offStartInVDisk)
                return vdIfError(pImage->pIfError, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                 N_("VMDK: incorrect partition data area ordering set up by the caller in '%s'"), pImage->pszFilename);

            if (uStart < pPart->offStartInVDisk)
                cExtents++;
            uStart = pPart->offStartInVDisk + pPart->cbData;
            cExtents++;
        }
        /* Another extent for filling up the rest of the image. */
        if (uStart != cbSize)
            cExtents++;

        rc = vmdkCreateExtents(pImage, cExtents);
        if (RT_FAILURE(rc))
            return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not create new extent list in '%s'"), pImage->pszFilename);

        /* Create raw partition descriptor file. */
        rc = vmdkFileOpen(pImage, &pImage->pFile, NULL, pImage->pszFilename,
                          VDOpenFlagsToFileOpenFlags(pImage->uOpenFlags,
                                                     true /* fCreate */));
        if (RT_FAILURE(rc))
            return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not create new file '%s'"), pImage->pszFilename);

        /* Create base filename for the partition table extent. */
        /** @todo remove fixed buffer without creating memory leaks. */
        char pszPartition[1024];
        const char *pszBase = RTPathFilename(pImage->pszFilename);
        const char *pszSuff = RTPathSuffix(pszBase);
        if (pszSuff == NULL)
            return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: invalid filename '%s'"), pImage->pszFilename);
        char *pszBaseBase = RTStrDup(pszBase);
        if (!pszBaseBase)
            return VERR_NO_MEMORY;
        RTPathStripSuffix(pszBaseBase);
        RTStrPrintf(pszPartition, sizeof(pszPartition), "%s-pt%s",
                    pszBaseBase, pszSuff);
        RTStrFree(pszBaseBase);

        /* Second pass over the partitions, now define all extents. */
        uint64_t uPartOffset = 0;
        cExtents = 0;
        uStart = 0;
        for (unsigned i = 0; i < pRaw->cPartDescs; i++)
        {
            PVDISKRAWPARTDESC pPart = &pRaw->pPartDescs[i];
            pExtent = &pImage->pExtents[cExtents++];

            if (uStart < pPart->offStartInVDisk)
            {
                pExtent->pszBasename = NULL;
                pExtent->pszFullname = NULL;
                pExtent->enmType = VMDKETYPE_ZERO;
                pExtent->cNominalSectors = VMDK_BYTE2SECTOR(pPart->offStartInVDisk - uStart);
                pExtent->uSectorOffset = 0;
                pExtent->enmAccess = VMDKACCESS_READWRITE;
                pExtent->fMetaDirty = false;
                /* go to next extent */
                pExtent = &pImage->pExtents[cExtents++];
            }
            uStart = pPart->offStartInVDisk + pPart->cbData;

            if (pPart->pvPartitionData)
            {
                /* Set up basename for extent description. Can't use StrDup. */
                size_t cbBasename = strlen(pszPartition) + 1;
                char *pszBasename = (char *)RTMemTmpAlloc(cbBasename);
                if (!pszBasename)
                    return VERR_NO_MEMORY;
                memcpy(pszBasename, pszPartition, cbBasename);
                pExtent->pszBasename = pszBasename;

                /* Set up full name for partition extent. */
                char *pszDirname = RTStrDup(pImage->pszFilename);
                if (!pszDirname)
                    return VERR_NO_STR_MEMORY;
                RTPathStripFilename(pszDirname);
                char *pszFullname = RTPathJoinA(pszDirname, pExtent->pszBasename);
                RTStrFree(pszDirname);
                if (!pszFullname)
                    return VERR_NO_STR_MEMORY;
                pExtent->pszFullname = pszFullname;
                pExtent->enmType = VMDKETYPE_FLAT;
                pExtent->cNominalSectors = VMDK_BYTE2SECTOR(pPart->cbData);
                pExtent->uSectorOffset = uPartOffset;
                pExtent->enmAccess = VMDKACCESS_READWRITE;
                pExtent->fMetaDirty = false;

                /* Create partition table flat image. */
                rc = vmdkFileOpen(pImage, &pExtent->pFile, pExtent->pszBasename, pExtent->pszFullname,
                                  VDOpenFlagsToFileOpenFlags(pImage->uOpenFlags | ((pExtent->enmAccess == VMDKACCESS_READONLY) ? VD_OPEN_FLAGS_READONLY : 0),
                                                             true /* fCreate */));
                if (RT_FAILURE(rc))
                    return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not create new partition data file '%s'"), pExtent->pszFullname);
                rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                            VMDK_SECTOR2BYTE(uPartOffset),
                                            pPart->pvPartitionData,
                                            pPart->cbData);
                if (RT_FAILURE(rc))
                    return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not write partition data to '%s'"), pExtent->pszFullname);
                uPartOffset += VMDK_BYTE2SECTOR(pPart->cbData);
            }
            else
            {
                if (pPart->pszRawDevice)
                {
                    /* Set up basename for extent descr. Can't use StrDup. */
                    size_t cbBasename = strlen(pPart->pszRawDevice) + 1;
                    char *pszBasename = (char *)RTMemTmpAlloc(cbBasename);
                    if (!pszBasename)
                        return VERR_NO_MEMORY;
                    memcpy(pszBasename, pPart->pszRawDevice, cbBasename);
                    pExtent->pszBasename = pszBasename;
                    /* For raw disks full name is identical to base name. */
                    pExtent->pszFullname = RTStrDup(pszBasename);
                    if (!pExtent->pszFullname)
                        return VERR_NO_MEMORY;
                    pExtent->enmType = VMDKETYPE_FLAT;
                    pExtent->cNominalSectors = VMDK_BYTE2SECTOR(pPart->cbData);
                    pExtent->uSectorOffset = VMDK_BYTE2SECTOR(pPart->offStartInDevice);
                    pExtent->enmAccess = (pPart->uFlags & VDISKRAW_READONLY) ? VMDKACCESS_READONLY : VMDKACCESS_READWRITE;
                    pExtent->fMetaDirty = false;

                    /* Open flat image, the raw partition. */
                    rc = vmdkFileOpen(pImage, &pExtent->pFile, pExtent->pszBasename, pExtent->pszFullname,
                                      VDOpenFlagsToFileOpenFlags(pImage->uOpenFlags | ((pExtent->enmAccess == VMDKACCESS_READONLY) ? VD_OPEN_FLAGS_READONLY : 0),
                                                                 false /* fCreate */));
                    if (RT_FAILURE(rc))
                        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not open raw partition file '%s'"), pExtent->pszFullname);
                }
                else
                {
                    pExtent->pszBasename = NULL;
                    pExtent->pszFullname = NULL;
                    pExtent->enmType = VMDKETYPE_ZERO;
                    pExtent->cNominalSectors = VMDK_BYTE2SECTOR(pPart->cbData);
                    pExtent->uSectorOffset = 0;
                    pExtent->enmAccess = VMDKACCESS_READWRITE;
                    pExtent->fMetaDirty = false;
                }
            }
        }
        /* Another extent for filling up the rest of the image. */
        if (uStart != cbSize)
        {
            pExtent = &pImage->pExtents[cExtents++];
            pExtent->pszBasename = NULL;
            pExtent->pszFullname = NULL;
            pExtent->enmType = VMDKETYPE_ZERO;
            pExtent->cNominalSectors = VMDK_BYTE2SECTOR(cbSize - uStart);
            pExtent->uSectorOffset = 0;
            pExtent->enmAccess = VMDKACCESS_READWRITE;
            pExtent->fMetaDirty = false;
        }
    }

    rc = vmdkDescBaseSetStr(pImage, &pImage->Descriptor, "createType",
                            (pRaw->uFlags & VDISKRAW_DISK) ?
                            "fullDevice" : "partitionedDevice");
    if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not set the image type in '%s'"), pImage->pszFilename);
    return rc;
}

/**
 * Internal: create a regular (i.e. file-backed) VMDK image.
 */
static int vmdkCreateRegularImage(PVMDKIMAGE pImage, uint64_t cbSize,
                                  unsigned uImageFlags, PVDINTERFACEPROGRESS pIfProgress,
                                  unsigned uPercentStart, unsigned uPercentSpan)
{
    int rc = VINF_SUCCESS;
    unsigned cExtents = 1;
    uint64_t cbOffset = 0;
    uint64_t cbRemaining = cbSize;

    if (uImageFlags & VD_VMDK_IMAGE_FLAGS_SPLIT_2G)
    {
        cExtents = cbSize / VMDK_2G_SPLIT_SIZE;
        /* Do proper extent computation: need one smaller extent if the total
         * size isn't evenly divisible by the split size. */
        if (cbSize % VMDK_2G_SPLIT_SIZE)
            cExtents++;
    }
    rc = vmdkCreateExtents(pImage, cExtents);
    if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not create new extent list in '%s'"), pImage->pszFilename);

    /* Basename strings needed for constructing the extent names. */
    char *pszBasenameSubstr = RTPathFilename(pImage->pszFilename);
    AssertPtr(pszBasenameSubstr);
    size_t cbBasenameSubstr = strlen(pszBasenameSubstr) + 1;

    /* Create separate descriptor file if necessary. */
    if (cExtents != 1 || (uImageFlags & VD_IMAGE_FLAGS_FIXED))
    {
        rc = vmdkFileOpen(pImage, &pImage->pFile, NULL, pImage->pszFilename,
                          VDOpenFlagsToFileOpenFlags(pImage->uOpenFlags,
                                                     true /* fCreate */));
        if (RT_FAILURE(rc))
            return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not create new sparse descriptor file '%s'"), pImage->pszFilename);
    }
    else
        pImage->pFile = NULL;

    /* Set up all extents. */
    for (unsigned i = 0; i < cExtents; i++)
    {
        PVMDKEXTENT pExtent = &pImage->pExtents[i];
        uint64_t cbExtent = cbRemaining;

        /* Set up fullname/basename for extent description. Cannot use StrDup
         * for basename, as it is not guaranteed that the memory can be freed
         * with RTMemTmpFree, which must be used as in other code paths
         * StrDup is not usable. */
        if (cExtents == 1 && !(uImageFlags & VD_IMAGE_FLAGS_FIXED))
        {
            char *pszBasename = (char *)RTMemTmpAlloc(cbBasenameSubstr);
            if (!pszBasename)
                return VERR_NO_MEMORY;
            memcpy(pszBasename, pszBasenameSubstr, cbBasenameSubstr);
            pExtent->pszBasename = pszBasename;
        }
        else
        {
            char *pszBasenameSuff = RTPathSuffix(pszBasenameSubstr);
            char *pszBasenameBase = RTStrDup(pszBasenameSubstr);
            RTPathStripSuffix(pszBasenameBase);
            char *pszTmp;
            size_t cbTmp;
            if (uImageFlags & VD_IMAGE_FLAGS_FIXED)
            {
                if (cExtents == 1)
                    RTStrAPrintf(&pszTmp, "%s-flat%s", pszBasenameBase,
                                 pszBasenameSuff);
                else
                    RTStrAPrintf(&pszTmp, "%s-f%03d%s", pszBasenameBase,
                                 i+1, pszBasenameSuff);
            }
            else
                RTStrAPrintf(&pszTmp, "%s-s%03d%s", pszBasenameBase, i+1,
                             pszBasenameSuff);
            RTStrFree(pszBasenameBase);
            if (!pszTmp)
                return VERR_NO_STR_MEMORY;
            cbTmp = strlen(pszTmp) + 1;
            char *pszBasename = (char *)RTMemTmpAlloc(cbTmp);
            if (!pszBasename)
            {
                RTStrFree(pszTmp);
                return VERR_NO_MEMORY;
            }
            memcpy(pszBasename, pszTmp, cbTmp);
            RTStrFree(pszTmp);
            pExtent->pszBasename = pszBasename;
            if (uImageFlags & VD_VMDK_IMAGE_FLAGS_SPLIT_2G)
                cbExtent = RT_MIN(cbRemaining, VMDK_2G_SPLIT_SIZE);
        }
        char *pszBasedirectory = RTStrDup(pImage->pszFilename);
        if (!pszBasedirectory)
            return VERR_NO_STR_MEMORY;
        RTPathStripFilename(pszBasedirectory);
        char *pszFullname = RTPathJoinA(pszBasedirectory, pExtent->pszBasename);
        RTStrFree(pszBasedirectory);
        if (!pszFullname)
            return VERR_NO_STR_MEMORY;
        pExtent->pszFullname = pszFullname;

        /* Create file for extent. */
        rc = vmdkFileOpen(pImage, &pExtent->pFile, pExtent->pszBasename, pExtent->pszFullname,
                          VDOpenFlagsToFileOpenFlags(pImage->uOpenFlags,
                                                     true /* fCreate */));
        if (RT_FAILURE(rc))
            return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not create new file '%s'"), pExtent->pszFullname);
        if (uImageFlags & VD_IMAGE_FLAGS_FIXED)
        {
            rc = vdIfIoIntFileSetAllocationSize(pImage->pIfIo, pExtent->pFile->pStorage, cbExtent,
                                                0 /* fFlags */, pIfProgress,
                                                uPercentStart + cbOffset * uPercentSpan / cbSize,
                                                cbExtent * uPercentSpan / cbSize);
            if (RT_FAILURE(rc))
                return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not set size of new file '%s'"), pExtent->pszFullname);
        }

        /* Place descriptor file information (where integrated). */
        if (cExtents == 1 && !(uImageFlags & VD_IMAGE_FLAGS_FIXED))
        {
            pExtent->uDescriptorSector = 1;
            pExtent->cDescriptorSectors = VMDK_BYTE2SECTOR(pImage->cbDescAlloc);
            /* The descriptor is part of the (only) extent. */
            pExtent->pDescData = pImage->pDescData;
            pImage->pDescData = NULL;
        }

        if (!(uImageFlags & VD_IMAGE_FLAGS_FIXED))
        {
            pExtent->enmType = VMDKETYPE_HOSTED_SPARSE;
            pExtent->cSectors = VMDK_BYTE2SECTOR(RT_ALIGN_64(cbExtent, _64K));
            pExtent->cSectorsPerGrain = VMDK_BYTE2SECTOR(_64K);
            pExtent->cGTEntries = 512;

            uint64_t const cSectorsPerGDE = pExtent->cGTEntries * pExtent->cSectorsPerGrain;
            pExtent->cSectorsPerGDE = cSectorsPerGDE;
            pExtent->cGDEntries = (pExtent->cSectors + cSectorsPerGDE - 1) / cSectorsPerGDE;
            if (pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED)
            {
                /* The spec says version is 1 for all VMDKs, but the vast
                 * majority of streamOptimized VMDKs actually contain
                 * version 3 - so go with the majority. Both are accepted. */
                pExtent->uVersion = 3;
                pExtent->uCompression = VMDK_COMPRESSION_DEFLATE;
            }
        }
        else
        {
            if (uImageFlags & VD_VMDK_IMAGE_FLAGS_ESX)
                pExtent->enmType = VMDKETYPE_VMFS;
            else
                pExtent->enmType = VMDKETYPE_FLAT;
        }

        pExtent->enmAccess = VMDKACCESS_READWRITE;
        pExtent->fUncleanShutdown = true;
        pExtent->cNominalSectors = VMDK_BYTE2SECTOR(cbExtent);
        pExtent->uSectorOffset = 0;
        pExtent->fMetaDirty = true;

        if (!(uImageFlags & VD_IMAGE_FLAGS_FIXED))
        {
            /* fPreAlloc should never be false because VMware can't use such images. */
            rc = vmdkCreateGrainDirectory(pImage, pExtent,
                                          RT_MAX(  pExtent->uDescriptorSector
                                                 + pExtent->cDescriptorSectors,
                                                 1),
                                          true /* fPreAlloc */);
            if (RT_FAILURE(rc))
                return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not create new grain directory in '%s'"), pExtent->pszFullname);
        }

        cbOffset += cbExtent;

        if (RT_SUCCESS(rc))
            vdIfProgress(pIfProgress, uPercentStart + cbOffset * uPercentSpan / cbSize);

        cbRemaining -= cbExtent;
    }

    if (pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_ESX)
    {
        /* VirtualBox doesn't care, but VMWare ESX freaks out if the wrong
         * controller type is set in an image. */
        rc = vmdkDescDDBSetStr(pImage, &pImage->Descriptor, "ddb.adapterType", "lsilogic");
        if (RT_FAILURE(rc))
            return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not set controller type to lsilogic in '%s'"), pImage->pszFilename);
    }

    const char *pszDescType = NULL;
    if (uImageFlags & VD_IMAGE_FLAGS_FIXED)
    {
        if (pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_ESX)
            pszDescType = "vmfs";
        else
            pszDescType =   (cExtents == 1)
                          ? "monolithicFlat" : "twoGbMaxExtentFlat";
    }
    else
    {
        if (pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED)
            pszDescType = "streamOptimized";
        else
        {
            pszDescType =   (cExtents == 1)
                          ? "monolithicSparse" : "twoGbMaxExtentSparse";
        }
    }
    rc = vmdkDescBaseSetStr(pImage, &pImage->Descriptor, "createType",
                            pszDescType);
    if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not set the image type in '%s'"), pImage->pszFilename);
    return rc;
}

/**
 * Internal: Create a real stream optimized VMDK using only linear writes.
 */
static int vmdkCreateStreamImage(PVMDKIMAGE pImage, uint64_t cbSize)
{
    int rc = vmdkCreateExtents(pImage, 1);
    if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not create new extent list in '%s'"), pImage->pszFilename);

    /* Basename strings needed for constructing the extent names. */
    const char *pszBasenameSubstr = RTPathFilename(pImage->pszFilename);
    AssertPtr(pszBasenameSubstr);
    size_t cbBasenameSubstr = strlen(pszBasenameSubstr) + 1;

    /* No separate descriptor file. */
    pImage->pFile = NULL;

    /* Set up all extents. */
    PVMDKEXTENT pExtent = &pImage->pExtents[0];

    /* Set up fullname/basename for extent description. Cannot use StrDup
     * for basename, as it is not guaranteed that the memory can be freed
     * with RTMemTmpFree, which must be used as in other code paths
     * StrDup is not usable. */
    char *pszBasename = (char *)RTMemTmpAlloc(cbBasenameSubstr);
    if (!pszBasename)
        return VERR_NO_MEMORY;
    memcpy(pszBasename, pszBasenameSubstr, cbBasenameSubstr);
    pExtent->pszBasename = pszBasename;

    char *pszBasedirectory = RTStrDup(pImage->pszFilename);
    RTPathStripFilename(pszBasedirectory);
    char *pszFullname = RTPathJoinA(pszBasedirectory, pExtent->pszBasename);
    RTStrFree(pszBasedirectory);
    if (!pszFullname)
        return VERR_NO_STR_MEMORY;
    pExtent->pszFullname = pszFullname;

    /* Create file for extent. Make it write only, no reading allowed. */
    rc = vmdkFileOpen(pImage, &pExtent->pFile, pExtent->pszBasename, pExtent->pszFullname,
                        VDOpenFlagsToFileOpenFlags(pImage->uOpenFlags,
                                                   true /* fCreate */)
                      & ~RTFILE_O_READ);
    if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not create new file '%s'"), pExtent->pszFullname);

    /* Place descriptor file information. */
    pExtent->uDescriptorSector = 1;
    pExtent->cDescriptorSectors = VMDK_BYTE2SECTOR(pImage->cbDescAlloc);
    /* The descriptor is part of the (only) extent. */
    pExtent->pDescData = pImage->pDescData;
    pImage->pDescData = NULL;

    pExtent->enmType = VMDKETYPE_HOSTED_SPARSE;
    pExtent->cSectors = VMDK_BYTE2SECTOR(RT_ALIGN_64(cbSize, _64K));
    pExtent->cSectorsPerGrain = VMDK_BYTE2SECTOR(_64K);
    pExtent->cGTEntries = 512;

    uint64_t const cSectorsPerGDE = pExtent->cGTEntries * pExtent->cSectorsPerGrain;
    pExtent->cSectorsPerGDE = cSectorsPerGDE;
    pExtent->cGDEntries = (pExtent->cSectors + cSectorsPerGDE - 1) / cSectorsPerGDE;

    /* The spec says version is 1 for all VMDKs, but the vast
     * majority of streamOptimized VMDKs actually contain
     * version 3 - so go with the majority. Both are accepted. */
    pExtent->uVersion = 3;
    pExtent->uCompression = VMDK_COMPRESSION_DEFLATE;
    pExtent->fFooter = true;

    pExtent->enmAccess = VMDKACCESS_READONLY;
    pExtent->fUncleanShutdown = false;
    pExtent->cNominalSectors = VMDK_BYTE2SECTOR(cbSize);
    pExtent->uSectorOffset = 0;
    pExtent->fMetaDirty = true;

    /* Create grain directory, without preallocating it straight away. It will
     * be constructed on the fly when writing out the data and written when
     * closing the image. The end effect is that the full grain directory is
     * allocated, which is a requirement of the VMDK specs. */
    rc = vmdkCreateGrainDirectory(pImage, pExtent, VMDK_GD_AT_END,
                                  false /* fPreAlloc */);
    if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not create new grain directory in '%s'"), pExtent->pszFullname);

    rc = vmdkDescBaseSetStr(pImage, &pImage->Descriptor, "createType",
                            "streamOptimized");
    if (RT_FAILURE(rc))
        rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not set the image type in '%s'"), pImage->pszFilename);

    return rc;
}

/**
 * Initializes the UUID fields in the DDB.
 *
 * @returns VBox status code.
 * @param   pImage          The VMDK image instance.
 */
static int vmdkCreateImageDdbUuidsInit(PVMDKIMAGE pImage)
{
    int rc = vmdkDescDDBSetUuid(pImage, &pImage->Descriptor, VMDK_DDB_IMAGE_UUID, &pImage->ImageUuid);
    if (RT_SUCCESS(rc))
    {
        rc = vmdkDescDDBSetUuid(pImage, &pImage->Descriptor, VMDK_DDB_PARENT_UUID, &pImage->ParentUuid);
        if (RT_SUCCESS(rc))
        {
            rc = vmdkDescDDBSetUuid(pImage, &pImage->Descriptor, VMDK_DDB_MODIFICATION_UUID,
                                    &pImage->ModificationUuid);
            if (RT_SUCCESS(rc))
            {
                rc = vmdkDescDDBSetUuid(pImage, &pImage->Descriptor, VMDK_DDB_PARENT_MODIFICATION_UUID,
                                        &pImage->ParentModificationUuid);
                if (RT_FAILURE(rc))
                    rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                                   N_("VMDK: error storing parent modification UUID in new descriptor in '%s'"), pImage->pszFilename);
            }
            else
                rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                               N_("VMDK: error storing modification UUID in new descriptor in '%s'"), pImage->pszFilename);
        }
        else
            rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                           N_("VMDK: error storing parent image UUID in new descriptor in '%s'"), pImage->pszFilename);
    }
    else
        rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                       N_("VMDK: error storing image UUID in new descriptor in '%s'"), pImage->pszFilename);

    return rc;
}

/**
 * Internal: The actual code for creating any VMDK variant currently in
 * existence on hosted environments.
 */
static int vmdkCreateImage(PVMDKIMAGE pImage, uint64_t cbSize,
                           unsigned uImageFlags, const char *pszComment,
                           PCVDGEOMETRY pPCHSGeometry,
                           PCVDGEOMETRY pLCHSGeometry, PCRTUUID pUuid,
                           PVDINTERFACEPROGRESS pIfProgress,
                           unsigned uPercentStart, unsigned uPercentSpan)
{
    pImage->uImageFlags = uImageFlags;

    pImage->pIfError = VDIfErrorGet(pImage->pVDIfsDisk);
    pImage->pIfIo = VDIfIoIntGet(pImage->pVDIfsImage);
    AssertPtrReturn(pImage->pIfIo, VERR_INVALID_PARAMETER);

    int rc = vmdkCreateDescriptor(pImage, pImage->pDescData, pImage->cbDescAlloc,
                                  &pImage->Descriptor);
    if (RT_SUCCESS(rc))
    {
        if (uImageFlags & VD_VMDK_IMAGE_FLAGS_RAWDISK)
        {
            /* Raw disk image (includes raw partition). */
            PVDISKRAW pRaw = NULL;
            rc = vmdkMakeRawDescriptor(pImage, &pRaw);
            if (RT_FAILURE(rc))
                return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not create raw descriptor for '%s'"),
                    pImage->pszFilename);
            if (!cbSize)
                cbSize = pImage->cbSize;

            rc = vmdkCreateRawImage(pImage, pRaw, cbSize);
            vmdkRawDescFree(pRaw);
        }
        else if (uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED)
        {
            /* Stream optimized sparse image (monolithic). */
            rc = vmdkCreateStreamImage(pImage, cbSize);
        }
        else
        {
            /* Regular fixed or sparse image (monolithic or split). */
            rc = vmdkCreateRegularImage(pImage, cbSize, uImageFlags,
                                        pIfProgress, uPercentStart,
                                        uPercentSpan * 95 / 100);
        }

        if (RT_SUCCESS(rc))
        {
            vdIfProgress(pIfProgress, uPercentStart + uPercentSpan * 98 / 100);

            pImage->cbSize = cbSize;

            for (unsigned i = 0; i < pImage->cExtents; i++)
            {
                PVMDKEXTENT pExtent = &pImage->pExtents[i];

                rc = vmdkDescExtInsert(pImage, &pImage->Descriptor, pExtent->enmAccess,
                                       pExtent->cNominalSectors, pExtent->enmType,
                                       pExtent->pszBasename, pExtent->uSectorOffset);
                if (RT_FAILURE(rc))
                {
                    rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not insert the extent list into descriptor in '%s'"), pImage->pszFilename);
                    break;
                }
            }

            if (RT_SUCCESS(rc))
                vmdkDescExtRemoveDummy(pImage, &pImage->Descriptor);

            pImage->LCHSGeometry = *pLCHSGeometry;
            pImage->PCHSGeometry = *pPCHSGeometry;

            if (RT_SUCCESS(rc))
            {
                if (   pPCHSGeometry->cCylinders != 0
                    && pPCHSGeometry->cHeads != 0
                    && pPCHSGeometry->cSectors != 0)
                    rc = vmdkDescSetPCHSGeometry(pImage, pPCHSGeometry);
                else if (uImageFlags & VD_VMDK_IMAGE_FLAGS_RAWDISK)
                {
                    VDGEOMETRY RawDiskPCHSGeometry;
                    RawDiskPCHSGeometry.cCylinders = (uint32_t)RT_MIN(pImage->cbSize / 512 / 16 / 63, 16383);
                    RawDiskPCHSGeometry.cHeads = 16;
                    RawDiskPCHSGeometry.cSectors = 63;
                    rc = vmdkDescSetPCHSGeometry(pImage, &RawDiskPCHSGeometry);
                }
            }

            if (   RT_SUCCESS(rc)
                && pLCHSGeometry->cCylinders != 0
                && pLCHSGeometry->cHeads != 0
                && pLCHSGeometry->cSectors != 0)
                rc = vmdkDescSetLCHSGeometry(pImage, pLCHSGeometry);

            pImage->ImageUuid = *pUuid;
            RTUuidClear(&pImage->ParentUuid);
            RTUuidClear(&pImage->ModificationUuid);
            RTUuidClear(&pImage->ParentModificationUuid);

            if (RT_SUCCESS(rc))
                rc = vmdkCreateImageDdbUuidsInit(pImage);

            if (RT_SUCCESS(rc))
                rc = vmdkAllocateGrainTableCache(pImage);

            if (RT_SUCCESS(rc))
            {
                rc = vmdkSetImageComment(pImage, pszComment);
                if (RT_FAILURE(rc))
                    rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: cannot set image comment in '%s'"), pImage->pszFilename);
            }

            if (RT_SUCCESS(rc))
            {
                vdIfProgress(pIfProgress, uPercentStart + uPercentSpan * 99 / 100);

                if (pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED)
                {
                    /* streamOptimized is a bit special, we cannot trigger the flush
                     * until all data has been written. So we write the necessary
                     * information explicitly. */
                    pImage->pExtents[0].cDescriptorSectors = VMDK_BYTE2SECTOR(RT_ALIGN_64(  pImage->Descriptor.aLines[pImage->Descriptor.cLines]
                                                                                          - pImage->Descriptor.aLines[0], 512));
                    rc = vmdkWriteMetaSparseExtent(pImage, &pImage->pExtents[0], 0, NULL);
                    if (RT_SUCCESS(rc))
                    {
                        rc = vmdkWriteDescriptor(pImage, NULL);
                        if (RT_FAILURE(rc))
                            rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: cannot write VMDK descriptor in '%s'"), pImage->pszFilename);
                    }
                    else
                        rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: cannot write VMDK header in '%s'"), pImage->pszFilename);
                }
                else
                    rc = vmdkFlushImage(pImage, NULL);
            }
        }
    }
    else
        rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not create new descriptor in '%s'"), pImage->pszFilename);


    if (RT_SUCCESS(rc))
    {
        PVDREGIONDESC pRegion = &pImage->RegionList.aRegions[0];
        pImage->RegionList.fFlags   = 0;
        pImage->RegionList.cRegions = 1;

        pRegion->offRegion            = 0; /* Disk start. */
        pRegion->cbBlock              = 512;
        pRegion->enmDataForm          = VDREGIONDATAFORM_RAW;
        pRegion->enmMetadataForm      = VDREGIONMETADATAFORM_NONE;
        pRegion->cbData               = 512;
        pRegion->cbMetadata           = 0;
        pRegion->cRegionBlocksOrBytes = pImage->cbSize;

        vdIfProgress(pIfProgress, uPercentStart + uPercentSpan);
    }
    else
        vmdkFreeImage(pImage, rc != VERR_ALREADY_EXISTS, false /*fFlush*/);
    return rc;
}

/**
 * Internal: Update image comment.
 */
static int vmdkSetImageComment(PVMDKIMAGE pImage, const char *pszComment)
{
    char *pszCommentEncoded = NULL;
    if (pszComment)
    {
        pszCommentEncoded = vmdkEncodeString(pszComment);
        if (!pszCommentEncoded)
            return VERR_NO_MEMORY;
    }

    int rc = vmdkDescDDBSetStr(pImage, &pImage->Descriptor,
                               "ddb.comment", pszCommentEncoded);
    if (pszCommentEncoded)
        RTStrFree(pszCommentEncoded);
    if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: error storing image comment in descriptor in '%s'"), pImage->pszFilename);
    return VINF_SUCCESS;
}

/**
 * Internal. Clear the grain table buffer for real stream optimized writing.
 */
static void vmdkStreamClearGT(PVMDKIMAGE pImage, PVMDKEXTENT pExtent)
{
    uint32_t cCacheLines = RT_ALIGN(pExtent->cGTEntries, VMDK_GT_CACHELINE_SIZE) / VMDK_GT_CACHELINE_SIZE;
    for (uint32_t i = 0; i < cCacheLines; i++)
        memset(&pImage->pGTCache->aGTCache[i].aGTData[0], '\0',
               VMDK_GT_CACHELINE_SIZE * sizeof(uint32_t));
}

/**
 * Internal. Flush the grain table buffer for real stream optimized writing.
 */
static int vmdkStreamFlushGT(PVMDKIMAGE pImage, PVMDKEXTENT pExtent,
                             uint32_t uGDEntry)
{
    int rc = VINF_SUCCESS;
    uint32_t cCacheLines = RT_ALIGN(pExtent->cGTEntries, VMDK_GT_CACHELINE_SIZE) / VMDK_GT_CACHELINE_SIZE;

    /* VMware does not write out completely empty grain tables in the case
     * of streamOptimized images, which according to my interpretation of
     * the VMDK 1.1 spec is bending the rules. Since they do it and we can
     * handle it without problems do it the same way and save some bytes. */
    bool fAllZero = true;
    for (uint32_t i = 0; i < cCacheLines; i++)
    {
        /* Convert the grain table to little endian in place, as it will not
         * be used at all after this function has been called. */
        uint32_t *pGTTmp = &pImage->pGTCache->aGTCache[i].aGTData[0];
        for (uint32_t j = 0; j < VMDK_GT_CACHELINE_SIZE; j++, pGTTmp++)
            if (*pGTTmp)
            {
                fAllZero = false;
                break;
            }
        if (!fAllZero)
            break;
    }
    if (fAllZero)
        return VINF_SUCCESS;

    uint64_t uFileOffset = pExtent->uAppendPosition;
    if (!uFileOffset)
        return VERR_INTERNAL_ERROR;
    /* Align to sector, as the previous write could have been any size. */
    uFileOffset = RT_ALIGN_64(uFileOffset, 512);

    /* Grain table marker. */
    uint8_t aMarker[512];
    PVMDKMARKER pMarker = (PVMDKMARKER)&aMarker[0];
    memset(pMarker, '\0', sizeof(aMarker));
    pMarker->uSector = RT_H2LE_U64(VMDK_BYTE2SECTOR((uint64_t)pExtent->cGTEntries * sizeof(uint32_t)));
    pMarker->uType = RT_H2LE_U32(VMDK_MARKER_GT);
    rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pExtent->pFile->pStorage, uFileOffset,
                                aMarker, sizeof(aMarker));
    AssertRC(rc);
    uFileOffset += 512;

    if (!pExtent->pGD || pExtent->pGD[uGDEntry])
        return VERR_INTERNAL_ERROR;

    pExtent->pGD[uGDEntry] = VMDK_BYTE2SECTOR(uFileOffset);

    for (uint32_t i = 0; i < cCacheLines; i++)
    {
        /* Convert the grain table to little endian in place, as it will not
         * be used at all after this function has been called. */
        uint32_t *pGTTmp = &pImage->pGTCache->aGTCache[i].aGTData[0];
        for (uint32_t j = 0; j < VMDK_GT_CACHELINE_SIZE; j++, pGTTmp++)
            *pGTTmp = RT_H2LE_U32(*pGTTmp);

        rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pExtent->pFile->pStorage, uFileOffset,
                                    &pImage->pGTCache->aGTCache[i].aGTData[0],
                                    VMDK_GT_CACHELINE_SIZE * sizeof(uint32_t));
        uFileOffset += VMDK_GT_CACHELINE_SIZE * sizeof(uint32_t);
        if (RT_FAILURE(rc))
            break;
    }
    Assert(!(uFileOffset % 512));
    pExtent->uAppendPosition = RT_ALIGN_64(uFileOffset, 512);
    return rc;
}

/**
 * Internal. Free all allocated space for representing an image, and optionally
 * delete the image from disk.
 */
static int vmdkFreeImage(PVMDKIMAGE pImage, bool fDelete, bool fFlush)
{
    int rc = VINF_SUCCESS;

    /* Freeing a never allocated image (e.g. because the open failed) is
     * not signalled as an error. After all nothing bad happens. */
    if (pImage)
    {
        if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
        {
            if (pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED)
            {
                /* Check if all extents are clean. */
                for (unsigned i = 0; i < pImage->cExtents; i++)
                {
                    Assert(!pImage->pExtents[i].fUncleanShutdown);
                }
            }
            else
            {
                /* Mark all extents as clean. */
                for (unsigned i = 0; i < pImage->cExtents; i++)
                {
                    if (   pImage->pExtents[i].enmType == VMDKETYPE_HOSTED_SPARSE
                        && pImage->pExtents[i].fUncleanShutdown)
                    {
                        pImage->pExtents[i].fUncleanShutdown = false;
                        pImage->pExtents[i].fMetaDirty = true;
                    }

                    /* From now on it's not safe to append any more data. */
                    pImage->pExtents[i].uAppendPosition = 0;
                }
            }
        }

        if (pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED)
        {
            /* No need to write any pending data if the file will be deleted
             * or if the new file wasn't successfully created. */
            if (   !fDelete && pImage->pExtents
                && pImage->pExtents[0].cGTEntries
                && pImage->pExtents[0].uAppendPosition)
            {
                PVMDKEXTENT pExtent = &pImage->pExtents[0];
                uint32_t uLastGDEntry = pExtent->uLastGrainAccess / pExtent->cGTEntries;
                rc = vmdkStreamFlushGT(pImage, pExtent, uLastGDEntry);
                AssertRC(rc);
                vmdkStreamClearGT(pImage, pExtent);
                for (uint32_t i = uLastGDEntry + 1; i < pExtent->cGDEntries; i++)
                {
                    rc = vmdkStreamFlushGT(pImage, pExtent, i);
                    AssertRC(rc);
                }

                uint64_t uFileOffset = pExtent->uAppendPosition;
                if (!uFileOffset)
                    return VERR_INTERNAL_ERROR;
                uFileOffset = RT_ALIGN_64(uFileOffset, 512);

                /* From now on it's not safe to append any more data. */
                pExtent->uAppendPosition = 0;

                /* Grain directory marker. */
                uint8_t aMarker[512];
                PVMDKMARKER pMarker = (PVMDKMARKER)&aMarker[0];
                memset(pMarker, '\0', sizeof(aMarker));
                pMarker->uSector = VMDK_BYTE2SECTOR(RT_ALIGN_64(RT_H2LE_U64((uint64_t)pExtent->cGDEntries * sizeof(uint32_t)), 512));
                pMarker->uType = RT_H2LE_U32(VMDK_MARKER_GD);
                rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pExtent->pFile->pStorage, uFileOffset,
                                            aMarker, sizeof(aMarker));
                AssertRC(rc);
                uFileOffset += 512;

                /* Write grain directory in little endian style. The array will
                 * not be used after this, so convert in place. */
                uint32_t *pGDTmp = pExtent->pGD;
                for (uint32_t i = 0; i < pExtent->cGDEntries; i++, pGDTmp++)
                    *pGDTmp = RT_H2LE_U32(*pGDTmp);
                rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                            uFileOffset, pExtent->pGD,
                                            pExtent->cGDEntries * sizeof(uint32_t));
                AssertRC(rc);

                pExtent->uSectorGD = VMDK_BYTE2SECTOR(uFileOffset);
                pExtent->uSectorRGD = VMDK_BYTE2SECTOR(uFileOffset);
                uFileOffset = RT_ALIGN_64(  uFileOffset
                                          + pExtent->cGDEntries * sizeof(uint32_t),
                                          512);

                /* Footer marker. */
                memset(pMarker, '\0', sizeof(aMarker));
                pMarker->uSector = VMDK_BYTE2SECTOR(512);
                pMarker->uType = RT_H2LE_U32(VMDK_MARKER_FOOTER);
                rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                            uFileOffset, aMarker, sizeof(aMarker));
                AssertRC(rc);

                uFileOffset += 512;
                rc = vmdkWriteMetaSparseExtent(pImage, pExtent, uFileOffset, NULL);
                AssertRC(rc);

                uFileOffset += 512;
                /* End-of-stream marker. */
                memset(pMarker, '\0', sizeof(aMarker));
                rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                            uFileOffset, aMarker, sizeof(aMarker));
                AssertRC(rc);
            }
        }
        else if (!fDelete && fFlush)
            vmdkFlushImage(pImage, NULL);

        if (pImage->pExtents != NULL)
        {
            for (unsigned i = 0 ; i < pImage->cExtents; i++)
            {
                int rc2 = vmdkFreeExtentData(pImage, &pImage->pExtents[i], fDelete);
                if (RT_SUCCESS(rc))
                    rc = rc2; /* Propogate any error when closing the file. */
            }
            RTMemFree(pImage->pExtents);
            pImage->pExtents = NULL;
        }
        pImage->cExtents = 0;
        if (pImage->pFile != NULL)
        {
            int rc2 = vmdkFileClose(pImage, &pImage->pFile, fDelete);
            if (RT_SUCCESS(rc))
                rc = rc2; /* Propogate any error when closing the file. */
        }
        int rc2 = vmdkFileCheckAllClose(pImage);
        if (RT_SUCCESS(rc))
            rc = rc2; /* Propogate any error when closing the file. */

        if (pImage->pGTCache)
        {
            RTMemFree(pImage->pGTCache);
            pImage->pGTCache = NULL;
        }
        if (pImage->pDescData)
        {
            RTMemFree(pImage->pDescData);
            pImage->pDescData = NULL;
        }
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Internal. Flush image data (and metadata) to disk.
 */
static int vmdkFlushImage(PVMDKIMAGE pImage, PVDIOCTX pIoCtx)
{
    PVMDKEXTENT pExtent;
    int rc = VINF_SUCCESS;

    /* Update descriptor if changed. */
    if (pImage->Descriptor.fDirty)
        rc = vmdkWriteDescriptor(pImage, pIoCtx);

    if (RT_SUCCESS(rc))
    {
        for (unsigned i = 0; i < pImage->cExtents; i++)
        {
            pExtent = &pImage->pExtents[i];
            if (pExtent->pFile != NULL && pExtent->fMetaDirty)
            {
                switch (pExtent->enmType)
                {
                    case VMDKETYPE_HOSTED_SPARSE:
                        if (!pExtent->fFooter)
                            rc = vmdkWriteMetaSparseExtent(pImage, pExtent, 0, pIoCtx);
                        else
                        {
                            uint64_t uFileOffset = pExtent->uAppendPosition;
                            /* Simply skip writing anything if the streamOptimized
                             * image hasn't been just created. */
                            if (!uFileOffset)
                                break;
                            uFileOffset = RT_ALIGN_64(uFileOffset, 512);
                            rc = vmdkWriteMetaSparseExtent(pImage, pExtent,
                                                           uFileOffset, pIoCtx);
                        }
                        break;
                    case VMDKETYPE_VMFS:
                    case VMDKETYPE_FLAT:
                        /* Nothing to do. */
                        break;
                    case VMDKETYPE_ZERO:
                    default:
                        AssertMsgFailed(("extent with type %d marked as dirty\n",
                                         pExtent->enmType));
                        break;
                }
            }

            if (RT_FAILURE(rc))
                break;

            switch (pExtent->enmType)
            {
                case VMDKETYPE_HOSTED_SPARSE:
                case VMDKETYPE_VMFS:
                case VMDKETYPE_FLAT:
                    /** @todo implement proper path absolute check. */
                    if (   pExtent->pFile != NULL
                        && !(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
                        && !(pExtent->pszBasename[0] == RTPATH_SLASH))
                        rc = vdIfIoIntFileFlush(pImage->pIfIo, pExtent->pFile->pStorage, pIoCtx,
                                                NULL, NULL);
                    break;
                case VMDKETYPE_ZERO:
                    /* No need to do anything for this extent. */
                    break;
                default:
                    AssertMsgFailed(("unknown extent type %d\n", pExtent->enmType));
                    break;
            }
        }
    }

    return rc;
}

/**
 * Internal. Find extent corresponding to the sector number in the disk.
 */
static int vmdkFindExtent(PVMDKIMAGE pImage, uint64_t offSector,
                          PVMDKEXTENT *ppExtent, uint64_t *puSectorInExtent)
{
    PVMDKEXTENT pExtent = NULL;
    int rc = VINF_SUCCESS;

    for (unsigned i = 0; i < pImage->cExtents; i++)
    {
        if (offSector < pImage->pExtents[i].cNominalSectors)
        {
            pExtent = &pImage->pExtents[i];
            *puSectorInExtent = offSector + pImage->pExtents[i].uSectorOffset;
            break;
        }
        offSector -= pImage->pExtents[i].cNominalSectors;
    }

    if (pExtent)
        *ppExtent = pExtent;
    else
        rc = VERR_IO_SECTOR_NOT_FOUND;

    return rc;
}

/**
 * Internal. Hash function for placing the grain table hash entries.
 */
static uint32_t vmdkGTCacheHash(PVMDKGTCACHE pCache, uint64_t uSector,
                                unsigned uExtent)
{
    /** @todo this hash function is quite simple, maybe use a better one which
     * scrambles the bits better. */
    return (uSector + uExtent) % pCache->cEntries;
}

/**
 * Internal. Get sector number in the extent file from the relative sector
 * number in the extent.
 */
static int vmdkGetSector(PVMDKIMAGE pImage, PVDIOCTX pIoCtx,
                         PVMDKEXTENT pExtent, uint64_t uSector,
                         uint64_t *puExtentSector)
{
    PVMDKGTCACHE pCache = pImage->pGTCache;
    uint64_t uGDIndex, uGTSector, uGTBlock;
    uint32_t uGTHash, uGTBlockIndex;
    PVMDKGTCACHEENTRY pGTCacheEntry;
    uint32_t aGTDataTmp[VMDK_GT_CACHELINE_SIZE];
    int rc;

    /* For newly created and readonly/sequentially opened streamOptimized
     * images this must be a no-op, as the grain directory is not there. */
    if (   (   pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED
            && pExtent->uAppendPosition)
        || (   pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED
            && pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY
            && pImage->uOpenFlags & VD_OPEN_FLAGS_SEQUENTIAL))
    {
        *puExtentSector = 0;
        return VINF_SUCCESS;
    }

    uGDIndex = uSector / pExtent->cSectorsPerGDE;
    if (uGDIndex >= pExtent->cGDEntries)
        return VERR_OUT_OF_RANGE;
    uGTSector = pExtent->pGD[uGDIndex];
    if (!uGTSector)
    {
        /* There is no grain table referenced by this grain directory
         * entry. So there is absolutely no data in this area. */
        *puExtentSector = 0;
        return VINF_SUCCESS;
    }

    uGTBlock = uSector / (pExtent->cSectorsPerGrain * VMDK_GT_CACHELINE_SIZE);
    uGTHash = vmdkGTCacheHash(pCache, uGTBlock, pExtent->uExtent);
    pGTCacheEntry = &pCache->aGTCache[uGTHash];
    if (    pGTCacheEntry->uExtent != pExtent->uExtent
        ||  pGTCacheEntry->uGTBlock != uGTBlock)
    {
        /* Cache miss, fetch data from disk. */
        PVDMETAXFER pMetaXfer;
        rc = vdIfIoIntFileReadMeta(pImage->pIfIo, pExtent->pFile->pStorage,
                                   VMDK_SECTOR2BYTE(uGTSector) + (uGTBlock % (pExtent->cGTEntries / VMDK_GT_CACHELINE_SIZE)) * sizeof(aGTDataTmp),
                                   aGTDataTmp, sizeof(aGTDataTmp), pIoCtx, &pMetaXfer, NULL, NULL);
        if (RT_FAILURE(rc))
            return rc;
        /* We can release the metadata transfer immediately. */
        vdIfIoIntMetaXferRelease(pImage->pIfIo, pMetaXfer);
        pGTCacheEntry->uExtent = pExtent->uExtent;
        pGTCacheEntry->uGTBlock = uGTBlock;
        for (unsigned i = 0; i < VMDK_GT_CACHELINE_SIZE; i++)
            pGTCacheEntry->aGTData[i] = RT_LE2H_U32(aGTDataTmp[i]);
    }
    uGTBlockIndex = (uSector / pExtent->cSectorsPerGrain) % VMDK_GT_CACHELINE_SIZE;
    uint32_t uGrainSector = pGTCacheEntry->aGTData[uGTBlockIndex];
    if (uGrainSector)
        *puExtentSector = uGrainSector + uSector % pExtent->cSectorsPerGrain;
    else
        *puExtentSector = 0;
    return VINF_SUCCESS;
}

/**
 * Internal. Writes the grain and also if necessary the grain tables.
 * Uses the grain table cache as a true grain table.
 */
static int vmdkStreamAllocGrain(PVMDKIMAGE pImage, PVMDKEXTENT pExtent,
                                uint64_t uSector, PVDIOCTX pIoCtx,
                                uint64_t cbWrite)
{
    uint32_t uGrain;
    uint32_t uGDEntry, uLastGDEntry;
    uint32_t cbGrain = 0;
    uint32_t uCacheLine, uCacheEntry;
    const void *pData;
    int rc;

    /* Very strict requirements: always write at least one full grain, with
     * proper alignment. Everything else would require reading of already
     * written data, which we don't support for obvious reasons. The only
     * exception is the last grain, and only if the image size specifies
     * that only some portion holds data. In any case the write must be
     * within the image limits, no "overshoot" allowed. */
    if (   cbWrite == 0
        || (   cbWrite < VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain)
            && pExtent->cNominalSectors - uSector >= pExtent->cSectorsPerGrain)
        || uSector % pExtent->cSectorsPerGrain
        || uSector + VMDK_BYTE2SECTOR(cbWrite) > pExtent->cNominalSectors)
        return VERR_INVALID_PARAMETER;

    /* Clip write range to at most the rest of the grain. */
    cbWrite = RT_MIN(cbWrite, VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain - uSector % pExtent->cSectorsPerGrain));

    /* Do not allow to go back. */
    uGrain = uSector / pExtent->cSectorsPerGrain;
    uCacheLine = uGrain % pExtent->cGTEntries / VMDK_GT_CACHELINE_SIZE;
    uCacheEntry = uGrain % VMDK_GT_CACHELINE_SIZE;
    uGDEntry = uGrain / pExtent->cGTEntries;
    uLastGDEntry = pExtent->uLastGrainAccess / pExtent->cGTEntries;
    if (uGrain < pExtent->uLastGrainAccess)
        return VERR_VD_VMDK_INVALID_WRITE;

    /* Zero byte write optimization. Since we don't tell VBoxHDD that we need
     * to allocate something, we also need to detect the situation ourself. */
    if (   !(pImage->uOpenFlags & VD_OPEN_FLAGS_HONOR_ZEROES)
        && vdIfIoIntIoCtxIsZero(pImage->pIfIo, pIoCtx, cbWrite, true /* fAdvance */))
        return VINF_SUCCESS;

    if (uGDEntry != uLastGDEntry)
    {
        rc = vmdkStreamFlushGT(pImage, pExtent, uLastGDEntry);
        if (RT_FAILURE(rc))
            return rc;
        vmdkStreamClearGT(pImage, pExtent);
        for (uint32_t i = uLastGDEntry + 1; i < uGDEntry; i++)
        {
            rc = vmdkStreamFlushGT(pImage, pExtent, i);
            if (RT_FAILURE(rc))
                return rc;
        }
    }

    uint64_t uFileOffset;
    uFileOffset = pExtent->uAppendPosition;
    if (!uFileOffset)
        return VERR_INTERNAL_ERROR;
    /* Align to sector, as the previous write could have been any size. */
    uFileOffset = RT_ALIGN_64(uFileOffset, 512);

    /* Paranoia check: extent type, grain table buffer presence and
     * grain table buffer space. Also grain table entry must be clear. */
    if (   pExtent->enmType != VMDKETYPE_HOSTED_SPARSE
        || !pImage->pGTCache
        || pExtent->cGTEntries > VMDK_GT_CACHE_SIZE * VMDK_GT_CACHELINE_SIZE
        || pImage->pGTCache->aGTCache[uCacheLine].aGTData[uCacheEntry])
        return VERR_INTERNAL_ERROR;

    /* Update grain table entry. */
    pImage->pGTCache->aGTCache[uCacheLine].aGTData[uCacheEntry] = VMDK_BYTE2SECTOR(uFileOffset);

    if (cbWrite != VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain))
    {
        vdIfIoIntIoCtxCopyFrom(pImage->pIfIo, pIoCtx, pExtent->pvGrain, cbWrite);
        memset((char *)pExtent->pvGrain + cbWrite, '\0',
               VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain) - cbWrite);
        pData = pExtent->pvGrain;
    }
    else
    {
        RTSGSEG Segment;
        unsigned cSegments = 1;

        size_t cbSeg = vdIfIoIntIoCtxSegArrayCreate(pImage->pIfIo, pIoCtx, &Segment,
                                                    &cSegments, VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain));
        Assert(cbSeg == VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain)); RT_NOREF(cbSeg);
        pData = Segment.pvSeg;
    }
    rc = vmdkFileDeflateSync(pImage, pExtent, uFileOffset, pData,
                             VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain),
                             uSector, &cbGrain);
    if (RT_FAILURE(rc))
    {
        pExtent->uGrainSectorAbs = 0;
        AssertRC(rc);
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: cannot write compressed data block in '%s'"), pExtent->pszFullname);
    }
    pExtent->uLastGrainAccess = uGrain;
    pExtent->uAppendPosition += cbGrain;

    return rc;
}

/**
 * Internal: Updates the grain table during grain allocation.
 */
static int vmdkAllocGrainGTUpdate(PVMDKIMAGE pImage, PVMDKEXTENT pExtent, PVDIOCTX pIoCtx,
                                  PVMDKGRAINALLOCASYNC pGrainAlloc)
{
    int rc = VINF_SUCCESS;
    PVMDKGTCACHE pCache = pImage->pGTCache;
    uint32_t aGTDataTmp[VMDK_GT_CACHELINE_SIZE];
    uint32_t uGTHash, uGTBlockIndex;
    uint64_t uGTSector, uRGTSector, uGTBlock;
    uint64_t uSector = pGrainAlloc->uSector;
    PVMDKGTCACHEENTRY pGTCacheEntry;

    LogFlowFunc(("pImage=%#p pExtent=%#p pCache=%#p pIoCtx=%#p pGrainAlloc=%#p\n",
                 pImage, pExtent, pCache, pIoCtx, pGrainAlloc));

    uGTSector = pGrainAlloc->uGTSector;
    uRGTSector = pGrainAlloc->uRGTSector;
    LogFlow(("uGTSector=%llu uRGTSector=%llu\n", uGTSector, uRGTSector));

    /* Update the grain table (and the cache). */
    uGTBlock = uSector / (pExtent->cSectorsPerGrain * VMDK_GT_CACHELINE_SIZE);
    uGTHash = vmdkGTCacheHash(pCache, uGTBlock, pExtent->uExtent);
    pGTCacheEntry = &pCache->aGTCache[uGTHash];
    if (    pGTCacheEntry->uExtent != pExtent->uExtent
        ||  pGTCacheEntry->uGTBlock != uGTBlock)
    {
        /* Cache miss, fetch data from disk. */
        LogFlow(("Cache miss, fetch data from disk\n"));
        PVDMETAXFER pMetaXfer = NULL;
        rc = vdIfIoIntFileReadMeta(pImage->pIfIo, pExtent->pFile->pStorage,
                                   VMDK_SECTOR2BYTE(uGTSector) + (uGTBlock % (pExtent->cGTEntries / VMDK_GT_CACHELINE_SIZE)) * sizeof(aGTDataTmp),
                                   aGTDataTmp, sizeof(aGTDataTmp), pIoCtx,
                                   &pMetaXfer, vmdkAllocGrainComplete, pGrainAlloc);
        if (rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
        {
            pGrainAlloc->cIoXfersPending++;
            pGrainAlloc->fGTUpdateNeeded = true;
            /* Leave early, we will be called  again after the read completed. */
            LogFlowFunc(("Metadata read in progress, leaving\n"));
            return rc;
        }
        else if (RT_FAILURE(rc))
            return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: cannot read allocated grain table entry in '%s'"), pExtent->pszFullname);
        vdIfIoIntMetaXferRelease(pImage->pIfIo, pMetaXfer);
        pGTCacheEntry->uExtent = pExtent->uExtent;
        pGTCacheEntry->uGTBlock = uGTBlock;
        for (unsigned i = 0; i < VMDK_GT_CACHELINE_SIZE; i++)
            pGTCacheEntry->aGTData[i] = RT_LE2H_U32(aGTDataTmp[i]);
    }
    else
    {
        /* Cache hit. Convert grain table block back to disk format, otherwise
         * the code below will write garbage for all but the updated entry. */
        for (unsigned i = 0; i < VMDK_GT_CACHELINE_SIZE; i++)
            aGTDataTmp[i] = RT_H2LE_U32(pGTCacheEntry->aGTData[i]);
    }
    pGrainAlloc->fGTUpdateNeeded = false;
    uGTBlockIndex = (uSector / pExtent->cSectorsPerGrain) % VMDK_GT_CACHELINE_SIZE;
    aGTDataTmp[uGTBlockIndex] = RT_H2LE_U32(VMDK_BYTE2SECTOR(pGrainAlloc->uGrainOffset));
    pGTCacheEntry->aGTData[uGTBlockIndex] = VMDK_BYTE2SECTOR(pGrainAlloc->uGrainOffset);
    /* Update grain table on disk. */
    rc = vdIfIoIntFileWriteMeta(pImage->pIfIo, pExtent->pFile->pStorage,
                                VMDK_SECTOR2BYTE(uGTSector) + (uGTBlock % (pExtent->cGTEntries / VMDK_GT_CACHELINE_SIZE)) * sizeof(aGTDataTmp),
                                aGTDataTmp, sizeof(aGTDataTmp), pIoCtx,
                                vmdkAllocGrainComplete, pGrainAlloc);
    if (rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
        pGrainAlloc->cIoXfersPending++;
    else if (RT_FAILURE(rc))
        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: cannot write updated grain table in '%s'"), pExtent->pszFullname);
    if (pExtent->pRGD)
    {
        /* Update backup grain table on disk. */
        rc = vdIfIoIntFileWriteMeta(pImage->pIfIo, pExtent->pFile->pStorage,
                                    VMDK_SECTOR2BYTE(uRGTSector) + (uGTBlock % (pExtent->cGTEntries / VMDK_GT_CACHELINE_SIZE)) * sizeof(aGTDataTmp),
                                    aGTDataTmp, sizeof(aGTDataTmp), pIoCtx,
                                    vmdkAllocGrainComplete, pGrainAlloc);
        if (rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
            pGrainAlloc->cIoXfersPending++;
        else if (RT_FAILURE(rc))
            return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: cannot write updated backup grain table in '%s'"), pExtent->pszFullname);
    }

    LogFlowFunc(("leaving rc=%Rrc\n", rc));
    return rc;
}

/**
 * Internal - complete the grain allocation by updating disk grain table if required.
 */
static DECLCALLBACK(int) vmdkAllocGrainComplete(void *pBackendData, PVDIOCTX pIoCtx, void *pvUser, int rcReq)
{
    RT_NOREF1(rcReq);
    int rc = VINF_SUCCESS;
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    PVMDKGRAINALLOCASYNC pGrainAlloc = (PVMDKGRAINALLOCASYNC)pvUser;

    LogFlowFunc(("pBackendData=%#p pIoCtx=%#p pvUser=%#p rcReq=%Rrc\n",
                 pBackendData, pIoCtx, pvUser, rcReq));

    pGrainAlloc->cIoXfersPending--;
    if (!pGrainAlloc->cIoXfersPending && pGrainAlloc->fGTUpdateNeeded)
        rc = vmdkAllocGrainGTUpdate(pImage, pGrainAlloc->pExtent, pIoCtx, pGrainAlloc);

    if (!pGrainAlloc->cIoXfersPending)
    {
        /* Grain allocation completed. */
        RTMemFree(pGrainAlloc);
    }

    LogFlowFunc(("Leaving rc=%Rrc\n", rc));
    return rc;
}

/**
 * Internal. Allocates a new grain table (if necessary).
 */
static int vmdkAllocGrain(PVMDKIMAGE pImage, PVMDKEXTENT pExtent, PVDIOCTX pIoCtx,
                          uint64_t uSector, uint64_t cbWrite)
{
    PVMDKGTCACHE pCache = pImage->pGTCache; NOREF(pCache);
    uint64_t uGDIndex, uGTSector, uRGTSector;
    uint64_t uFileOffset;
    PVMDKGRAINALLOCASYNC pGrainAlloc = NULL;
    int rc;

    LogFlowFunc(("pCache=%#p pExtent=%#p pIoCtx=%#p uSector=%llu cbWrite=%llu\n",
                 pCache, pExtent, pIoCtx, uSector, cbWrite));

    pGrainAlloc = (PVMDKGRAINALLOCASYNC)RTMemAllocZ(sizeof(VMDKGRAINALLOCASYNC));
    if (!pGrainAlloc)
        return VERR_NO_MEMORY;

    pGrainAlloc->pExtent = pExtent;
    pGrainAlloc->uSector = uSector;

    uGDIndex = uSector / pExtent->cSectorsPerGDE;
    if (uGDIndex >= pExtent->cGDEntries)
    {
        RTMemFree(pGrainAlloc);
        return VERR_OUT_OF_RANGE;
    }
    uGTSector = pExtent->pGD[uGDIndex];
    if (pExtent->pRGD)
        uRGTSector = pExtent->pRGD[uGDIndex];
    else
        uRGTSector = 0; /**< avoid compiler warning */
    if (!uGTSector)
    {
        LogFlow(("Allocating new grain table\n"));

        /* There is no grain table referenced by this grain directory
         * entry. So there is absolutely no data in this area. Allocate
         * a new grain table and put the reference to it in the GDs. */
        uFileOffset = pExtent->uAppendPosition;
        if (!uFileOffset)
        {
            RTMemFree(pGrainAlloc);
            return VERR_INTERNAL_ERROR;
        }
        Assert(!(uFileOffset % 512));

        uFileOffset = RT_ALIGN_64(uFileOffset, 512);
        uGTSector = VMDK_BYTE2SECTOR(uFileOffset);

        /* Normally the grain table is preallocated for hosted sparse extents
         * that support more than 32 bit sector numbers. So this shouldn't
         * ever happen on a valid extent. */
        if (uGTSector > UINT32_MAX)
        {
            RTMemFree(pGrainAlloc);
            return VERR_VD_VMDK_INVALID_HEADER;
        }

        /* Write grain table by writing the required number of grain table
         * cache chunks. Allocate memory dynamically here or we flood the
         * metadata cache with very small entries. */
        size_t cbGTDataTmp = pExtent->cGTEntries * sizeof(uint32_t);
        uint32_t *paGTDataTmp = (uint32_t *)RTMemTmpAllocZ(cbGTDataTmp);

        if (!paGTDataTmp)
        {
            RTMemFree(pGrainAlloc);
            return VERR_NO_MEMORY;
        }

        memset(paGTDataTmp, '\0', cbGTDataTmp);
        rc = vdIfIoIntFileWriteMeta(pImage->pIfIo, pExtent->pFile->pStorage,
                                    VMDK_SECTOR2BYTE(uGTSector),
                                    paGTDataTmp, cbGTDataTmp, pIoCtx,
                                    vmdkAllocGrainComplete, pGrainAlloc);
        if (rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
            pGrainAlloc->cIoXfersPending++;
        else if (RT_FAILURE(rc))
        {
            RTMemTmpFree(paGTDataTmp);
            RTMemFree(pGrainAlloc);
            return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: cannot write grain table allocation in '%s'"), pExtent->pszFullname);
        }
        pExtent->uAppendPosition = RT_ALIGN_64(  pExtent->uAppendPosition
                                               + cbGTDataTmp, 512);

        if (pExtent->pRGD)
        {
            AssertReturn(!uRGTSector, VERR_VD_VMDK_INVALID_HEADER);
            uFileOffset = pExtent->uAppendPosition;
            if (!uFileOffset)
                return VERR_INTERNAL_ERROR;
            Assert(!(uFileOffset % 512));
            uRGTSector = VMDK_BYTE2SECTOR(uFileOffset);

            /* Normally the redundant grain table is preallocated for hosted
             * sparse extents that support more than 32 bit sector numbers. So
             * this shouldn't ever happen on a valid extent. */
            if (uRGTSector > UINT32_MAX)
            {
                RTMemTmpFree(paGTDataTmp);
                return VERR_VD_VMDK_INVALID_HEADER;
            }

            /* Write grain table by writing the required number of grain table
             * cache chunks. Allocate memory dynamically here or we flood the
             * metadata cache with very small entries. */
            rc = vdIfIoIntFileWriteMeta(pImage->pIfIo, pExtent->pFile->pStorage,
                                        VMDK_SECTOR2BYTE(uRGTSector),
                                        paGTDataTmp, cbGTDataTmp, pIoCtx,
                                        vmdkAllocGrainComplete, pGrainAlloc);
            if (rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
                pGrainAlloc->cIoXfersPending++;
            else if (RT_FAILURE(rc))
            {
                RTMemTmpFree(paGTDataTmp);
                return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: cannot write backup grain table allocation in '%s'"), pExtent->pszFullname);
            }

            pExtent->uAppendPosition = pExtent->uAppendPosition + cbGTDataTmp;
        }

        RTMemTmpFree(paGTDataTmp);

        /* Update the grain directory on disk (doing it before writing the
         * grain table will result in a garbled extent if the operation is
         * aborted for some reason. Otherwise the worst that can happen is
         * some unused sectors in the extent. */
        uint32_t uGTSectorLE = RT_H2LE_U64(uGTSector);
        rc = vdIfIoIntFileWriteMeta(pImage->pIfIo, pExtent->pFile->pStorage,
                                    VMDK_SECTOR2BYTE(pExtent->uSectorGD) + uGDIndex * sizeof(uGTSectorLE),
                                    &uGTSectorLE, sizeof(uGTSectorLE), pIoCtx,
                                    vmdkAllocGrainComplete, pGrainAlloc);
        if (rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
            pGrainAlloc->cIoXfersPending++;
        else if (RT_FAILURE(rc))
            return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: cannot write grain directory entry in '%s'"), pExtent->pszFullname);
        if (pExtent->pRGD)
        {
            uint32_t uRGTSectorLE = RT_H2LE_U64(uRGTSector);
            rc = vdIfIoIntFileWriteMeta(pImage->pIfIo, pExtent->pFile->pStorage,
                                        VMDK_SECTOR2BYTE(pExtent->uSectorRGD) + uGDIndex * sizeof(uGTSectorLE),
                                        &uRGTSectorLE, sizeof(uRGTSectorLE), pIoCtx,
                                        vmdkAllocGrainComplete, pGrainAlloc);
            if (rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
                pGrainAlloc->cIoXfersPending++;
            else if (RT_FAILURE(rc))
                return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: cannot write backup grain directory entry in '%s'"), pExtent->pszFullname);
        }

        /* As the final step update the in-memory copy of the GDs. */
        pExtent->pGD[uGDIndex] = uGTSector;
        if (pExtent->pRGD)
            pExtent->pRGD[uGDIndex] = uRGTSector;
    }

    LogFlow(("uGTSector=%llu uRGTSector=%llu\n", uGTSector, uRGTSector));
    pGrainAlloc->uGTSector = uGTSector;
    pGrainAlloc->uRGTSector = uRGTSector;

    uFileOffset = pExtent->uAppendPosition;
    if (!uFileOffset)
        return VERR_INTERNAL_ERROR;
    Assert(!(uFileOffset % 512));

    pGrainAlloc->uGrainOffset = uFileOffset;

    if (pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED)
    {
        AssertMsgReturn(vdIfIoIntIoCtxIsSynchronous(pImage->pIfIo, pIoCtx),
                        ("Accesses to stream optimized images must be synchronous\n"),
                        VERR_INVALID_STATE);

        if (cbWrite != VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain))
            return vdIfError(pImage->pIfError, VERR_INTERNAL_ERROR, RT_SRC_POS, N_("VMDK: not enough data for a compressed data block in '%s'"), pExtent->pszFullname);

        /* Invalidate cache, just in case some code incorrectly allows mixing
         * of reads and writes. Normally shouldn't be needed. */
        pExtent->uGrainSectorAbs = 0;

        /* Write compressed data block and the markers. */
        uint32_t cbGrain = 0;
        RTSGSEG Segment;
        unsigned cSegments = 1;

        size_t cbSeg = vdIfIoIntIoCtxSegArrayCreate(pImage->pIfIo, pIoCtx, &Segment,
                                                    &cSegments, cbWrite);
        Assert(cbSeg == cbWrite); RT_NOREF(cbSeg);

        rc = vmdkFileDeflateSync(pImage, pExtent, uFileOffset,
                                 Segment.pvSeg, cbWrite, uSector, &cbGrain);
        if (RT_FAILURE(rc))
        {
            AssertRC(rc);
            return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: cannot write allocated compressed data block in '%s'"), pExtent->pszFullname);
        }
        pExtent->uLastGrainAccess = uSector / pExtent->cSectorsPerGrain;
        pExtent->uAppendPosition += cbGrain;
    }
    else
    {
        /* Write the data. Always a full grain, or we're in big trouble. */
        rc = vdIfIoIntFileWriteUser(pImage->pIfIo, pExtent->pFile->pStorage,
                                    uFileOffset, pIoCtx, cbWrite,
                                    vmdkAllocGrainComplete, pGrainAlloc);
        if (rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
            pGrainAlloc->cIoXfersPending++;
        else if (RT_FAILURE(rc))
            return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: cannot write allocated data block in '%s'"), pExtent->pszFullname);

        pExtent->uAppendPosition += cbWrite;
    }

    rc = vmdkAllocGrainGTUpdate(pImage, pExtent, pIoCtx, pGrainAlloc);

    if (!pGrainAlloc->cIoXfersPending)
    {
        /* Grain allocation completed. */
        RTMemFree(pGrainAlloc);
    }

    LogFlowFunc(("leaving rc=%Rrc\n", rc));

    return rc;
}

/**
 * Internal. Reads the contents by sequentially going over the compressed
 * grains (hoping that they are in sequence).
 */
static int vmdkStreamReadSequential(PVMDKIMAGE pImage, PVMDKEXTENT pExtent,
                                    uint64_t uSector, PVDIOCTX pIoCtx,
                                    uint64_t cbRead)
{
    int rc;

    LogFlowFunc(("pImage=%#p pExtent=%#p uSector=%llu pIoCtx=%#p cbRead=%llu\n",
                 pImage, pExtent, uSector, pIoCtx, cbRead));

    AssertMsgReturn(vdIfIoIntIoCtxIsSynchronous(pImage->pIfIo, pIoCtx),
                    ("Async I/O not supported for sequential stream optimized images\n"),
                    VERR_INVALID_STATE);

    /* Do not allow to go back. */
    uint32_t uGrain = uSector / pExtent->cSectorsPerGrain;
    if (uGrain < pExtent->uLastGrainAccess)
        return VERR_VD_VMDK_INVALID_STATE;
    pExtent->uLastGrainAccess = uGrain;

    /* After a previous error do not attempt to recover, as it would need
     * seeking (in the general case backwards which is forbidden). */
    if (!pExtent->uGrainSectorAbs)
        return VERR_VD_VMDK_INVALID_STATE;

    /* Check if we need to read something from the image or if what we have
     * in the buffer is good to fulfill the request. */
    if (!pExtent->cbGrainStreamRead || uGrain > pExtent->uGrain)
    {
        uint32_t uGrainSectorAbs =   pExtent->uGrainSectorAbs
                                   + VMDK_BYTE2SECTOR(pExtent->cbGrainStreamRead);

        /* Get the marker from the next data block - and skip everything which
         * is not a compressed grain. If it's a compressed grain which is for
         * the requested sector (or after), read it. */
        VMDKMARKER Marker;
        do
        {
            RT_ZERO(Marker);
            rc = vdIfIoIntFileReadSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                       VMDK_SECTOR2BYTE(uGrainSectorAbs),
                                       &Marker, RT_UOFFSETOF(VMDKMARKER, uType));
            if (RT_FAILURE(rc))
                return rc;
            Marker.uSector = RT_LE2H_U64(Marker.uSector);
            Marker.cbSize = RT_LE2H_U32(Marker.cbSize);

            if (Marker.cbSize == 0)
            {
                /* A marker for something else than a compressed grain. */
                rc = vdIfIoIntFileReadSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                             VMDK_SECTOR2BYTE(uGrainSectorAbs)
                                           + RT_UOFFSETOF(VMDKMARKER, uType),
                                           &Marker.uType, sizeof(Marker.uType));
                if (RT_FAILURE(rc))
                    return rc;
                Marker.uType = RT_LE2H_U32(Marker.uType);
                switch (Marker.uType)
                {
                    case VMDK_MARKER_EOS:
                        uGrainSectorAbs++;
                        /* Read (or mostly skip) to the end of file. Uses the
                         * Marker (LBA sector) as it is unused anyway. This
                         * makes sure that really everything is read in the
                         * success case. If this read fails it means the image
                         * is truncated, but this is harmless so ignore. */
                        vdIfIoIntFileReadSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                                VMDK_SECTOR2BYTE(uGrainSectorAbs)
                                              + 511,
                                              &Marker.uSector, 1);
                        break;
                    case VMDK_MARKER_GT:
                        uGrainSectorAbs += 1 + VMDK_BYTE2SECTOR(pExtent->cGTEntries * sizeof(uint32_t));
                        break;
                    case VMDK_MARKER_GD:
                        uGrainSectorAbs += 1 + VMDK_BYTE2SECTOR(RT_ALIGN(pExtent->cGDEntries * sizeof(uint32_t), 512));
                        break;
                    case VMDK_MARKER_FOOTER:
                        uGrainSectorAbs += 2;
                        break;
                    case VMDK_MARKER_UNSPECIFIED:
                        /* Skip over the contents of the unspecified marker
                         * type 4 which exists in some vSphere created files. */
                        /** @todo figure out what the payload means. */
                        uGrainSectorAbs += 1;
                        break;
                    default:
                        AssertMsgFailed(("VMDK: corrupted marker, type=%#x\n", Marker.uType));
                        pExtent->uGrainSectorAbs = 0;
                        return VERR_VD_VMDK_INVALID_STATE;
                }
                pExtent->cbGrainStreamRead = 0;
            }
            else
            {
                /* A compressed grain marker. If it is at/after what we're
                 * interested in read and decompress data. */
                if (uSector > Marker.uSector + pExtent->cSectorsPerGrain)
                {
                    uGrainSectorAbs += VMDK_BYTE2SECTOR(RT_ALIGN(Marker.cbSize + RT_UOFFSETOF(VMDKMARKER, uType), 512));
                    continue;
                }
                uint64_t uLBA = 0;
                uint32_t cbGrainStreamRead = 0;
                rc = vmdkFileInflateSync(pImage, pExtent,
                                         VMDK_SECTOR2BYTE(uGrainSectorAbs),
                                         pExtent->pvGrain,
                                         VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain),
                                         &Marker, &uLBA, &cbGrainStreamRead);
                if (RT_FAILURE(rc))
                {
                    pExtent->uGrainSectorAbs = 0;
                    return rc;
                }
                if (   pExtent->uGrain
                    && uLBA / pExtent->cSectorsPerGrain <= pExtent->uGrain)
                {
                    pExtent->uGrainSectorAbs = 0;
                    return VERR_VD_VMDK_INVALID_STATE;
                }
                pExtent->uGrain = uLBA / pExtent->cSectorsPerGrain;
                pExtent->cbGrainStreamRead = cbGrainStreamRead;
                break;
            }
        } while (Marker.uType != VMDK_MARKER_EOS);

        pExtent->uGrainSectorAbs = uGrainSectorAbs;

        if (!pExtent->cbGrainStreamRead && Marker.uType == VMDK_MARKER_EOS)
        {
            pExtent->uGrain = UINT32_MAX;
            /* Must set a non-zero value for pExtent->cbGrainStreamRead or
             * the next read would try to get more data, and we're at EOF. */
            pExtent->cbGrainStreamRead = 1;
        }
    }

    if (pExtent->uGrain > uSector / pExtent->cSectorsPerGrain)
    {
        /* The next data block we have is not for this area, so just return
         * that there is no data. */
        LogFlowFunc(("returns VERR_VD_BLOCK_FREE\n"));
        return VERR_VD_BLOCK_FREE;
    }

    uint32_t uSectorInGrain = uSector % pExtent->cSectorsPerGrain;
    vdIfIoIntIoCtxCopyTo(pImage->pIfIo, pIoCtx,
                         (uint8_t *)pExtent->pvGrain + VMDK_SECTOR2BYTE(uSectorInGrain),
                         cbRead);
    LogFlowFunc(("returns VINF_SUCCESS\n"));
    return VINF_SUCCESS;
}

/**
 * Replaces a fragment of a string with the specified string.
 *
 * @returns Pointer to the allocated UTF-8 string.
 * @param   pszWhere        UTF-8 string to search in.
 * @param   pszWhat         UTF-8 string to search for.
 * @param   pszByWhat       UTF-8 string to replace the found string with.
 *
 * @note    r=bird: This is only used by vmdkRenameWorker().  The first use is
 *          for updating the base name in the descriptor, the second is for
 *          generating new filenames for extents.  This code borked when
 *          RTPathAbs started correcting the driver letter case on windows,
 *          when strstr failed because the pExtent->pszFullname was not
 *          subjected to RTPathAbs but while pExtent->pszFullname was.  I fixed
 *          this by apply RTPathAbs to the places it wasn't applied.
 *
 *          However, this highlights some undocumented ASSUMPTIONS as well as
 *          terrible short commings of the approach.
 *
 *          Given the right filename, it may also screw up the descriptor.  Take
 *          the descriptor text 'RW 2048 SPARSE "Test0.vmdk"' for instance,
 *          we'll be asked to replace "Test0" with something, no problem.  No,
 *          imagine 'RW 2048 SPARSE "SPARSE.vmdk"', 'RW 2048 SPARSE "RW.vmdk"'
 *          or 'RW 2048 SPARSE "2048.vmdk"', and the strstr approach falls on
 *          its bum.  The descriptor string must be parsed and reconstructed,
 *          the lazy strstr approach doesn't cut it.
 *
 *          I'm also curious as to what would be the correct escaping of '"' in
 *          the file name and how that is supposed to be handled, because it
 *          needs to be or such names must be rejected in several places (maybe
 *          they are, I didn't check).
 *
 *          When this function is used to replace the start of a path, I think
 *          the assumption from the prep/setup code is that we kind of knows
 *          what we're working on (I could be wrong).  However, using strstr
 *          instead of strncmp/RTStrNICmp makes no sense and isn't future proof.
 *          Especially on unix systems, weird stuff could happen if someone
 *          unwittingly tinkers with the prep/setup code.  What should really be
 *          done here is using a new RTPathStartEx function that (via flags)
 *          allows matching partial final component and returns the length of
 *          what it matched up (in case it skipped slashes and '.' components).
 *
 */
static char *vmdkStrReplace(const char *pszWhere, const char *pszWhat,
                            const char *pszByWhat)
{
    AssertPtr(pszWhere);
    AssertPtr(pszWhat);
    AssertPtr(pszByWhat);
    const char *pszFoundStr = strstr(pszWhere, pszWhat);
    if (!pszFoundStr)
    {
        LogFlowFunc(("Failed to find '%s' in '%s'!\n", pszWhat, pszWhere));
        return NULL;
    }
    size_t cbFinal = strlen(pszWhere) + 1 + strlen(pszByWhat) - strlen(pszWhat);
    char *pszNewStr = RTStrAlloc(cbFinal);
    if (pszNewStr)
    {
        char *pszTmp = pszNewStr;
        memcpy(pszTmp, pszWhere, pszFoundStr - pszWhere);
        pszTmp += pszFoundStr - pszWhere;
        memcpy(pszTmp, pszByWhat, strlen(pszByWhat));
        pszTmp += strlen(pszByWhat);
        strcpy(pszTmp, pszFoundStr + strlen(pszWhat));
    }
    return pszNewStr;
}


/** @copydoc VDIMAGEBACKEND::pfnProbe */
static DECLCALLBACK(int) vmdkProbe(const char *pszFilename, PVDINTERFACE pVDIfsDisk,
                                   PVDINTERFACE pVDIfsImage, VDTYPE enmDesiredType, VDTYPE *penmType)
{
    RT_NOREF(enmDesiredType);
    LogFlowFunc(("pszFilename=\"%s\" pVDIfsDisk=%#p pVDIfsImage=%#p penmType=%#p\n",
                 pszFilename, pVDIfsDisk, pVDIfsImage, penmType));
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename != '\0', VERR_INVALID_PARAMETER);

    int rc = VINF_SUCCESS;
    PVMDKIMAGE pImage = (PVMDKIMAGE)RTMemAllocZ(RT_UOFFSETOF(VMDKIMAGE, RegionList.aRegions[1]));
    if (RT_LIKELY(pImage))
    {
        pImage->pszFilename = pszFilename;
        pImage->pFile = NULL;
        pImage->pExtents = NULL;
        pImage->pFiles = NULL;
        pImage->pGTCache = NULL;
        pImage->pDescData = NULL;
        pImage->pVDIfsDisk = pVDIfsDisk;
        pImage->pVDIfsImage = pVDIfsImage;
        /** @todo speed up this test open (VD_OPEN_FLAGS_INFO) by skipping as
         * much as possible in vmdkOpenImage. */
        rc = vmdkOpenImage(pImage, VD_OPEN_FLAGS_INFO | VD_OPEN_FLAGS_READONLY);
        vmdkFreeImage(pImage, false, false /*fFlush*/);
        RTMemFree(pImage);

        if (RT_SUCCESS(rc))
            *penmType = VDTYPE_HDD;
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnOpen */
static DECLCALLBACK(int) vmdkOpen(const char *pszFilename, unsigned uOpenFlags,
                                  PVDINTERFACE pVDIfsDisk, PVDINTERFACE pVDIfsImage,
                                  VDTYPE enmType, void **ppBackendData)
{
    RT_NOREF1(enmType); /**< @todo r=klaus make use of the type info. */

    LogFlowFunc(("pszFilename=\"%s\" uOpenFlags=%#x pVDIfsDisk=%#p pVDIfsImage=%#p enmType=%u ppBackendData=%#p\n",
                 pszFilename, uOpenFlags, pVDIfsDisk, pVDIfsImage, enmType, ppBackendData));
    int rc;

    /* Check open flags. All valid flags are supported. */
    AssertReturn(!(uOpenFlags & ~VD_OPEN_FLAGS_MASK), VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename != '\0', VERR_INVALID_PARAMETER);


    PVMDKIMAGE pImage = (PVMDKIMAGE)RTMemAllocZ(RT_UOFFSETOF(VMDKIMAGE, RegionList.aRegions[1]));
    if (RT_LIKELY(pImage))
    {
        pImage->pszFilename = pszFilename;
        pImage->pFile = NULL;
        pImage->pExtents = NULL;
        pImage->pFiles = NULL;
        pImage->pGTCache = NULL;
        pImage->pDescData = NULL;
        pImage->pVDIfsDisk = pVDIfsDisk;
        pImage->pVDIfsImage = pVDIfsImage;

        rc = vmdkOpenImage(pImage, uOpenFlags);
        if (RT_SUCCESS(rc))
            *ppBackendData = pImage;
        else
            RTMemFree(pImage);
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFunc(("returns %Rrc (pBackendData=%#p)\n", rc, *ppBackendData));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnCreate */
static DECLCALLBACK(int) vmdkCreate(const char *pszFilename, uint64_t cbSize,
                                    unsigned uImageFlags, const char *pszComment,
                                    PCVDGEOMETRY pPCHSGeometry, PCVDGEOMETRY pLCHSGeometry,
                                    PCRTUUID pUuid, unsigned uOpenFlags,
                                    unsigned uPercentStart, unsigned uPercentSpan,
                                    PVDINTERFACE pVDIfsDisk, PVDINTERFACE pVDIfsImage,
                                    PVDINTERFACE pVDIfsOperation, VDTYPE enmType,
                                    void **ppBackendData)
{
    LogFlowFunc(("pszFilename=\"%s\" cbSize=%llu uImageFlags=%#x pszComment=\"%s\" pPCHSGeometry=%#p pLCHSGeometry=%#p Uuid=%RTuuid uOpenFlags=%#x uPercentStart=%u uPercentSpan=%u pVDIfsDisk=%#p pVDIfsImage=%#p pVDIfsOperation=%#p enmType=%u ppBackendData=%#p\n",
                 pszFilename, cbSize, uImageFlags, pszComment, pPCHSGeometry, pLCHSGeometry, pUuid, uOpenFlags, uPercentStart, uPercentSpan, pVDIfsDisk, pVDIfsImage, pVDIfsOperation, enmType, ppBackendData));
    int rc;

    /* Check the VD container type and image flags. */
    if (   enmType != VDTYPE_HDD
        || (uImageFlags & ~VD_VMDK_IMAGE_FLAGS_MASK) != 0)
        return VERR_VD_INVALID_TYPE;

    /* Check size. Maximum 256TB-64K for sparse images, otherwise unlimited. */
    if (   !(uImageFlags & VD_VMDK_IMAGE_FLAGS_RAWDISK)
        && (   !cbSize
            || (!(uImageFlags & VD_IMAGE_FLAGS_FIXED) && cbSize >= _1T * 256 - _64K)))
        return VERR_VD_INVALID_SIZE;

    /* Check image flags for invalid combinations. */
    if (   (uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED)
        && (uImageFlags & ~(VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED | VD_IMAGE_FLAGS_DIFF)))
        return VERR_INVALID_PARAMETER;

    /* Check open flags. All valid flags are supported. */
    AssertReturn(!(uOpenFlags & ~VD_OPEN_FLAGS_MASK), VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename != '\0', VERR_INVALID_PARAMETER);
    AssertPtrReturn(pPCHSGeometry, VERR_INVALID_POINTER);
    AssertPtrReturn(pLCHSGeometry, VERR_INVALID_POINTER);
    AssertReturn(!(   uImageFlags & VD_VMDK_IMAGE_FLAGS_ESX
                   && !(uImageFlags & VD_IMAGE_FLAGS_FIXED)),
                 VERR_INVALID_PARAMETER);

    PVMDKIMAGE pImage = (PVMDKIMAGE)RTMemAllocZ(RT_UOFFSETOF(VMDKIMAGE, RegionList.aRegions[1]));
    if (RT_LIKELY(pImage))
    {
        PVDINTERFACEPROGRESS pIfProgress = VDIfProgressGet(pVDIfsOperation);

        pImage->pszFilename = pszFilename;
        pImage->pFile = NULL;
        pImage->pExtents = NULL;
        pImage->pFiles = NULL;
        pImage->pGTCache = NULL;
        pImage->pDescData = NULL;
        pImage->pVDIfsDisk = pVDIfsDisk;
        pImage->pVDIfsImage = pVDIfsImage;
        /* Descriptors for split images can be pretty large, especially if the
         * filename is long. So prepare for the worst, and allocate quite some
         * memory for the descriptor in this case. */
        if (uImageFlags & VD_VMDK_IMAGE_FLAGS_SPLIT_2G)
            pImage->cbDescAlloc = VMDK_SECTOR2BYTE(200);
        else
            pImage->cbDescAlloc = VMDK_SECTOR2BYTE(20);
        pImage->pDescData = (char *)RTMemAllocZ(pImage->cbDescAlloc);
        if (RT_LIKELY(pImage->pDescData))
        {
            rc = vmdkCreateImage(pImage, cbSize, uImageFlags, pszComment,
                                 pPCHSGeometry, pLCHSGeometry, pUuid,
                                 pIfProgress, uPercentStart, uPercentSpan);
            if (RT_SUCCESS(rc))
            {
                /* So far the image is opened in read/write mode. Make sure the
                 * image is opened in read-only mode if the caller requested that. */
                if (uOpenFlags & VD_OPEN_FLAGS_READONLY)
                {
                    vmdkFreeImage(pImage, false, true /*fFlush*/);
                    rc = vmdkOpenImage(pImage, uOpenFlags);
                }

                if (RT_SUCCESS(rc))
                    *ppBackendData = pImage;
            }

            if (RT_FAILURE(rc))
                RTMemFree(pImage->pDescData);
        }
        else
            rc = VERR_NO_MEMORY;

        if (RT_FAILURE(rc))
            RTMemFree(pImage);
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFunc(("returns %Rrc (pBackendData=%#p)\n", rc, *ppBackendData));
    return rc;
}

/**
 * Prepares the state for renaming a VMDK image, setting up the state and allocating
 * memory.
 *
 * @returns VBox status code.
 * @param   pImage          VMDK image instance.
 * @param   pRenameState    The state to initialize.
 * @param   pszFilename     The new filename.
 */
static int vmdkRenameStatePrepare(PVMDKIMAGE pImage, PVMDKRENAMESTATE pRenameState, const char *pszFilename)
{
    AssertReturn(RTPathFilename(pszFilename) != NULL, VERR_INVALID_PARAMETER);

    int rc = VINF_SUCCESS;

    memset(&pRenameState->DescriptorCopy, 0, sizeof(pRenameState->DescriptorCopy));

    /*
     * Allocate an array to store both old and new names of renamed files
     * in case we have to roll back the changes. Arrays are initialized
     * with zeros. We actually save stuff when and if we change it.
     */
    pRenameState->cExtents     = pImage->cExtents;
    pRenameState->apszOldName  = (char **)RTMemTmpAllocZ((pRenameState->cExtents + 1) * sizeof(char *));
    pRenameState->apszNewName  = (char **)RTMemTmpAllocZ((pRenameState->cExtents + 1) * sizeof(char *));
    pRenameState->apszNewLines = (char **)RTMemTmpAllocZ(pRenameState->cExtents * sizeof(char *));
    if (   pRenameState->apszOldName
        && pRenameState->apszNewName
        && pRenameState->apszNewLines)
    {
        /* Save the descriptor size and position. */
        if (pImage->pDescData)
        {
            /* Separate descriptor file. */
            pRenameState->fEmbeddedDesc = false;
        }
        else
        {
            /* Embedded descriptor file. */
            pRenameState->ExtentCopy = pImage->pExtents[0];
            pRenameState->fEmbeddedDesc = true;
        }

        /* Save the descriptor content. */
        pRenameState->DescriptorCopy.cLines = pImage->Descriptor.cLines;
        for (unsigned i = 0; i < pRenameState->DescriptorCopy.cLines; i++)
        {
            pRenameState->DescriptorCopy.aLines[i] = RTStrDup(pImage->Descriptor.aLines[i]);
            if (!pRenameState->DescriptorCopy.aLines[i])
            {
                rc = VERR_NO_MEMORY;
                break;
            }
        }

        if (RT_SUCCESS(rc))
        {
            /* Prepare both old and new base names used for string replacement. */
            pRenameState->pszNewBaseName = RTStrDup(RTPathFilename(pszFilename));
            AssertReturn(pRenameState->pszNewBaseName, VERR_NO_STR_MEMORY);
            RTPathStripSuffix(pRenameState->pszNewBaseName);

            pRenameState->pszOldBaseName = RTStrDup(RTPathFilename(pImage->pszFilename));
            AssertReturn(pRenameState->pszOldBaseName, VERR_NO_STR_MEMORY);
            RTPathStripSuffix(pRenameState->pszOldBaseName);

            /* Prepare both old and new full names used for string replacement.
               Note! Must abspath the stuff here, so the strstr weirdness later in
                     the renaming process get a match against abspath'ed extent paths.
                     See RTPathAbsDup call in vmdkDescriptorReadSparse(). */
            pRenameState->pszNewFullName = RTPathAbsDup(pszFilename);
            AssertReturn(pRenameState->pszNewFullName, VERR_NO_STR_MEMORY);
            RTPathStripSuffix(pRenameState->pszNewFullName);

            pRenameState->pszOldFullName = RTPathAbsDup(pImage->pszFilename);
            AssertReturn(pRenameState->pszOldFullName, VERR_NO_STR_MEMORY);
            RTPathStripSuffix(pRenameState->pszOldFullName);

            /* Save the old name for easy access to the old descriptor file. */
            pRenameState->pszOldDescName = RTStrDup(pImage->pszFilename);
            AssertReturn(pRenameState->pszOldDescName, VERR_NO_STR_MEMORY);

            /* Save old image name. */
            pRenameState->pszOldImageName = pImage->pszFilename;
        }
    }
    else
        rc = VERR_NO_TMP_MEMORY;

    return rc;
}

/**
 * Destroys the given rename state, freeing all allocated memory.
 *
 * @param   pRenameState    The rename state to destroy.
 */
static void vmdkRenameStateDestroy(PVMDKRENAMESTATE pRenameState)
{
    for (unsigned i = 0; i < pRenameState->DescriptorCopy.cLines; i++)
        if (pRenameState->DescriptorCopy.aLines[i])
            RTStrFree(pRenameState->DescriptorCopy.aLines[i]);
    if (pRenameState->apszOldName)
    {
        for (unsigned i = 0; i <= pRenameState->cExtents; i++)
            if (pRenameState->apszOldName[i])
                RTStrFree(pRenameState->apszOldName[i]);
        RTMemTmpFree(pRenameState->apszOldName);
    }
    if (pRenameState->apszNewName)
    {
        for (unsigned i = 0; i <= pRenameState->cExtents; i++)
            if (pRenameState->apszNewName[i])
                RTStrFree(pRenameState->apszNewName[i]);
        RTMemTmpFree(pRenameState->apszNewName);
    }
    if (pRenameState->apszNewLines)
    {
        for (unsigned i = 0; i < pRenameState->cExtents; i++)
            if (pRenameState->apszNewLines[i])
                RTStrFree(pRenameState->apszNewLines[i]);
        RTMemTmpFree(pRenameState->apszNewLines);
    }
    if (pRenameState->pszOldDescName)
        RTStrFree(pRenameState->pszOldDescName);
    if (pRenameState->pszOldBaseName)
        RTStrFree(pRenameState->pszOldBaseName);
    if (pRenameState->pszNewBaseName)
        RTStrFree(pRenameState->pszNewBaseName);
    if (pRenameState->pszOldFullName)
        RTStrFree(pRenameState->pszOldFullName);
    if (pRenameState->pszNewFullName)
        RTStrFree(pRenameState->pszNewFullName);
}

/**
 * Rolls back the rename operation to the original state.
 *
 * @returns VBox status code.
 * @param   pImage          VMDK image instance.
 * @param   pRenameState    The rename state.
 */
static int vmdkRenameRollback(PVMDKIMAGE pImage, PVMDKRENAMESTATE pRenameState)
{
    int rc = VINF_SUCCESS;

    if (!pRenameState->fImageFreed)
    {
        /*
         * Some extents may have been closed, close the rest. We will
         * re-open the whole thing later.
         */
        vmdkFreeImage(pImage, false, true /*fFlush*/);
    }

    /* Rename files back. */
    for (unsigned i = 0; i <= pRenameState->cExtents; i++)
    {
        if (pRenameState->apszOldName[i])
        {
            rc = vdIfIoIntFileMove(pImage->pIfIo, pRenameState->apszNewName[i], pRenameState->apszOldName[i], 0);
            AssertRC(rc);
        }
    }
    /* Restore the old descriptor. */
    PVMDKFILE pFile;
    rc = vmdkFileOpen(pImage, &pFile, NULL, pRenameState->pszOldDescName,
                      VDOpenFlagsToFileOpenFlags(VD_OPEN_FLAGS_NORMAL,
                                                 false /* fCreate */));
    AssertRC(rc);
    if (pRenameState->fEmbeddedDesc)
    {
        pRenameState->ExtentCopy.pFile = pFile;
        pImage->pExtents = &pRenameState->ExtentCopy;
    }
    else
    {
        /* Shouldn't be null for separate descriptor.
         * There will be no access to the actual content.
         */
        pImage->pDescData = pRenameState->pszOldDescName;
        pImage->pFile = pFile;
    }
    pImage->Descriptor = pRenameState->DescriptorCopy;
    vmdkWriteDescriptor(pImage, NULL);
    vmdkFileClose(pImage, &pFile, false);
    /* Get rid of the stuff we implanted. */
    pImage->pExtents = NULL;
    pImage->pFile = NULL;
    pImage->pDescData = NULL;
    /* Re-open the image back. */
    pImage->pszFilename = pRenameState->pszOldImageName;
    rc = vmdkOpenImage(pImage, pImage->uOpenFlags);

    return rc;
}

/**
 * Rename worker doing the real work.
 *
 * @returns VBox status code.
 * @param   pImage          VMDK image instance.
 * @param   pRenameState    The rename state.
 * @param   pszFilename     The new filename.
 */
static int vmdkRenameWorker(PVMDKIMAGE pImage, PVMDKRENAMESTATE pRenameState, const char *pszFilename)
{
    int rc = VINF_SUCCESS;
    unsigned i, line;

    /* Update the descriptor with modified extent names. */
    for (i = 0, line = pImage->Descriptor.uFirstExtent;
        i < pRenameState->cExtents;
        i++, line = pImage->Descriptor.aNextLines[line])
    {
        /* Update the descriptor. */
        pRenameState->apszNewLines[i] = vmdkStrReplace(pImage->Descriptor.aLines[line],
                                                       pRenameState->pszOldBaseName,
                                                       pRenameState->pszNewBaseName);
        if (!pRenameState->apszNewLines[i])
        {
            rc = VERR_NO_MEMORY;
            break;
        }
        pImage->Descriptor.aLines[line] = pRenameState->apszNewLines[i];
    }

    if (RT_SUCCESS(rc))
    {
        /* Make sure the descriptor gets written back. */
        pImage->Descriptor.fDirty = true;
        /* Flush the descriptor now, in case it is embedded. */
        vmdkFlushImage(pImage, NULL);

        /* Close and rename/move extents. */
        for (i = 0; i < pRenameState->cExtents; i++)
        {
            PVMDKEXTENT pExtent = &pImage->pExtents[i];
            /* Compose new name for the extent. */
            pRenameState->apszNewName[i] = vmdkStrReplace(pExtent->pszFullname,
                                                          pRenameState->pszOldFullName,
                                                          pRenameState->pszNewFullName);
            if (!pRenameState->apszNewName[i])
            {
                rc = VERR_NO_MEMORY;
                break;
            }
            /* Close the extent file. */
            rc = vmdkFileClose(pImage, &pExtent->pFile, false);
            if (RT_FAILURE(rc))
                break;;

            /* Rename the extent file. */
            rc = vdIfIoIntFileMove(pImage->pIfIo, pExtent->pszFullname, pRenameState->apszNewName[i], 0);
            if (RT_FAILURE(rc))
                break;
            /* Remember the old name. */
            pRenameState->apszOldName[i] = RTStrDup(pExtent->pszFullname);
        }

        if (RT_SUCCESS(rc))
        {
            /* Release all old stuff. */
            rc = vmdkFreeImage(pImage, false, true /*fFlush*/);
            if (RT_SUCCESS(rc))
            {
                pRenameState->fImageFreed = true;

                /* Last elements of new/old name arrays are intended for
                 * storing descriptor's names.
                 */
                pRenameState->apszNewName[pRenameState->cExtents] = RTStrDup(pszFilename);
                /* Rename the descriptor file if it's separate. */
                if (!pRenameState->fEmbeddedDesc)
                {
                    rc = vdIfIoIntFileMove(pImage->pIfIo, pImage->pszFilename, pRenameState->apszNewName[pRenameState->cExtents], 0);
                    if (RT_SUCCESS(rc))
                    {
                        /* Save old name only if we may need to change it back. */
                        pRenameState->apszOldName[pRenameState->cExtents] = RTStrDup(pszFilename);
                    }
                }

                /* Update pImage with the new information. */
                pImage->pszFilename = pszFilename;

                /* Open the new image. */
                rc = vmdkOpenImage(pImage, pImage->uOpenFlags);
            }
        }
    }

    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnRename */
static DECLCALLBACK(int) vmdkRename(void *pBackendData, const char *pszFilename)
{
    LogFlowFunc(("pBackendData=%#p pszFilename=%#p\n", pBackendData, pszFilename));

    PVMDKIMAGE  pImage  = (PVMDKIMAGE)pBackendData;
    VMDKRENAMESTATE RenameState;

    memset(&RenameState, 0, sizeof(RenameState));

    /* Check arguments. */
    AssertPtrReturn(pImage, VERR_INVALID_POINTER);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename != '\0', VERR_INVALID_PARAMETER);
    AssertReturn(!(pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_RAWDISK), VERR_INVALID_PARAMETER);

    int rc = vmdkRenameStatePrepare(pImage, &RenameState, pszFilename);
    if (RT_SUCCESS(rc))
    {
        /* --- Up to this point we have not done any damage yet. --- */

        rc = vmdkRenameWorker(pImage, &RenameState, pszFilename);
        /* Roll back all changes in case of failure. */
        if (RT_FAILURE(rc))
        {
            int rrc = vmdkRenameRollback(pImage, &RenameState);
            AssertRC(rrc);
        }
    }

    vmdkRenameStateDestroy(&RenameState);
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnClose */
static DECLCALLBACK(int) vmdkClose(void *pBackendData, bool fDelete)
{
    LogFlowFunc(("pBackendData=%#p fDelete=%d\n", pBackendData, fDelete));
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;

    int rc = vmdkFreeImage(pImage, fDelete, true /*fFlush*/);
    RTMemFree(pImage);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnRead */
static DECLCALLBACK(int) vmdkRead(void *pBackendData, uint64_t uOffset, size_t cbToRead,
                                  PVDIOCTX pIoCtx, size_t *pcbActuallyRead)
{
    LogFlowFunc(("pBackendData=%#p uOffset=%llu pIoCtx=%#p cbToRead=%zu pcbActuallyRead=%#p\n",
                 pBackendData, uOffset, pIoCtx, cbToRead, pcbActuallyRead));
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;

    AssertPtr(pImage);
    Assert(uOffset % 512 == 0);
    Assert(cbToRead % 512 == 0);
    AssertPtrReturn(pIoCtx, VERR_INVALID_POINTER);
    AssertReturn(cbToRead, VERR_INVALID_PARAMETER);
    AssertReturn(uOffset + cbToRead <= pImage->cbSize, VERR_INVALID_PARAMETER);

    /* Find the extent and check access permissions as defined in the extent descriptor. */
    PVMDKEXTENT pExtent;
    uint64_t uSectorExtentRel;
    int rc = vmdkFindExtent(pImage, VMDK_BYTE2SECTOR(uOffset),
                            &pExtent, &uSectorExtentRel);
    if (   RT_SUCCESS(rc)
        && pExtent->enmAccess != VMDKACCESS_NOACCESS)
    {
        /* Clip read range to remain in this extent. */
        cbToRead = RT_MIN(cbToRead, VMDK_SECTOR2BYTE(pExtent->uSectorOffset + pExtent->cNominalSectors - uSectorExtentRel));

        /* Handle the read according to the current extent type. */
        switch (pExtent->enmType)
        {
            case VMDKETYPE_HOSTED_SPARSE:
            {
                uint64_t uSectorExtentAbs;

                rc = vmdkGetSector(pImage, pIoCtx, pExtent, uSectorExtentRel, &uSectorExtentAbs);
                if (RT_FAILURE(rc))
                    break;
                /* Clip read range to at most the rest of the grain. */
                cbToRead = RT_MIN(cbToRead, VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain - uSectorExtentRel % pExtent->cSectorsPerGrain));
                Assert(!(cbToRead % 512));
                if (uSectorExtentAbs == 0)
                {
                    if (   !(pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED)
                        || !(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
                        || !(pImage->uOpenFlags & VD_OPEN_FLAGS_SEQUENTIAL))
                        rc = VERR_VD_BLOCK_FREE;
                    else
                        rc = vmdkStreamReadSequential(pImage, pExtent,
                                                      uSectorExtentRel,
                                                      pIoCtx, cbToRead);
                }
                else
                {
                    if (pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED)
                    {
                        AssertMsg(vdIfIoIntIoCtxIsSynchronous(pImage->pIfIo, pIoCtx),
                                  ("Async I/O is not supported for stream optimized VMDK's\n"));

                        uint32_t uSectorInGrain = uSectorExtentRel % pExtent->cSectorsPerGrain;
                        uSectorExtentAbs -= uSectorInGrain;
                        if (pExtent->uGrainSectorAbs != uSectorExtentAbs)
                        {
                            uint64_t uLBA = 0; /* gcc maybe uninitialized */
                            rc = vmdkFileInflateSync(pImage, pExtent,
                                                     VMDK_SECTOR2BYTE(uSectorExtentAbs),
                                                     pExtent->pvGrain,
                                                     VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain),
                                                     NULL, &uLBA, NULL);
                            if (RT_FAILURE(rc))
                            {
                                pExtent->uGrainSectorAbs = 0;
                                break;
                            }
                            pExtent->uGrainSectorAbs = uSectorExtentAbs;
                            pExtent->uGrain = uSectorExtentRel / pExtent->cSectorsPerGrain;
                            Assert(uLBA == uSectorExtentRel);
                        }
                        vdIfIoIntIoCtxCopyTo(pImage->pIfIo, pIoCtx,
                                               (uint8_t *)pExtent->pvGrain
                                             + VMDK_SECTOR2BYTE(uSectorInGrain),
                                             cbToRead);
                    }
                    else
                        rc = vdIfIoIntFileReadUser(pImage->pIfIo, pExtent->pFile->pStorage,
                                                   VMDK_SECTOR2BYTE(uSectorExtentAbs),
                                                   pIoCtx, cbToRead);
                }
                break;
            }
            case VMDKETYPE_VMFS:
            case VMDKETYPE_FLAT:
                rc = vdIfIoIntFileReadUser(pImage->pIfIo, pExtent->pFile->pStorage,
                                           VMDK_SECTOR2BYTE(uSectorExtentRel),
                                           pIoCtx, cbToRead);
                break;
            case VMDKETYPE_ZERO:
            {
                size_t cbSet = vdIfIoIntIoCtxSet(pImage->pIfIo, pIoCtx, 0, cbToRead);
                Assert(cbSet == cbToRead); RT_NOREF(cbSet);
                break;
            }
        }
        if (pcbActuallyRead)
            *pcbActuallyRead = cbToRead;
    }
    else if (RT_SUCCESS(rc))
        rc = VERR_VD_VMDK_INVALID_STATE;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnWrite */
static DECLCALLBACK(int) vmdkWrite(void *pBackendData, uint64_t uOffset, size_t cbToWrite,
                                   PVDIOCTX pIoCtx, size_t *pcbWriteProcess, size_t *pcbPreRead,
                                   size_t *pcbPostRead, unsigned fWrite)
{
    LogFlowFunc(("pBackendData=%#p uOffset=%llu pIoCtx=%#p cbToWrite=%zu pcbWriteProcess=%#p pcbPreRead=%#p pcbPostRead=%#p\n",
                 pBackendData, uOffset, pIoCtx, cbToWrite, pcbWriteProcess, pcbPreRead, pcbPostRead));
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    int rc;

    AssertPtr(pImage);
    Assert(uOffset % 512 == 0);
    Assert(cbToWrite % 512 == 0);
    AssertPtrReturn(pIoCtx, VERR_INVALID_POINTER);
    AssertReturn(cbToWrite, VERR_INVALID_PARAMETER);

    if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
    {
        PVMDKEXTENT pExtent;
        uint64_t uSectorExtentRel;
        uint64_t uSectorExtentAbs;

        /* No size check here, will do that later when the extent is located.
         * There are sparse images out there which according to the spec are
         * invalid, because the total size is not a multiple of the grain size.
         * Also for sparse images which are stitched together in odd ways (not at
         * grain boundaries, and with the nominal size not being a multiple of the
         * grain size), this would prevent writing to the last grain. */

        rc = vmdkFindExtent(pImage, VMDK_BYTE2SECTOR(uOffset),
                            &pExtent, &uSectorExtentRel);
        if (RT_SUCCESS(rc))
        {
            if (   pExtent->enmAccess != VMDKACCESS_READWRITE
                && (   !(pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED)
                    && !pImage->pExtents[0].uAppendPosition
                    && pExtent->enmAccess != VMDKACCESS_READONLY))
                rc = VERR_VD_VMDK_INVALID_STATE;
            else
            {
                /* Handle the write according to the current extent type. */
                switch (pExtent->enmType)
                {
                    case VMDKETYPE_HOSTED_SPARSE:
                        rc = vmdkGetSector(pImage, pIoCtx, pExtent, uSectorExtentRel, &uSectorExtentAbs);
                        if (RT_SUCCESS(rc))
                        {
                            if (    pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED
                                &&  uSectorExtentRel < (uint64_t)pExtent->uLastGrainAccess * pExtent->cSectorsPerGrain)
                                rc = VERR_VD_VMDK_INVALID_WRITE;
                            else
                            {
                                /* Clip write range to at most the rest of the grain. */
                                cbToWrite = RT_MIN(cbToWrite,
                                                   VMDK_SECTOR2BYTE(  pExtent->cSectorsPerGrain
                                                                    - uSectorExtentRel % pExtent->cSectorsPerGrain));
                                if (uSectorExtentAbs == 0)
                                {
                                    if (!(pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED))
                                    {
                                        if (cbToWrite == VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain))
                                        {
                                            /* Full block write to a previously unallocated block.
                                             * Check if the caller wants to avoid the automatic alloc. */
                                            if (!(fWrite & VD_WRITE_NO_ALLOC))
                                            {
                                                /* Allocate GT and find out where to store the grain. */
                                                rc = vmdkAllocGrain(pImage, pExtent, pIoCtx,
                                                                    uSectorExtentRel, cbToWrite);
                                            }
                                            else
                                                rc = VERR_VD_BLOCK_FREE;
                                            *pcbPreRead = 0;
                                            *pcbPostRead = 0;
                                        }
                                        else
                                        {
                                            /* Clip write range to remain in this extent. */
                                            cbToWrite = RT_MIN(cbToWrite,
                                                               VMDK_SECTOR2BYTE(  pExtent->uSectorOffset
                                                                                + pExtent->cNominalSectors - uSectorExtentRel));
                                            *pcbPreRead = VMDK_SECTOR2BYTE(uSectorExtentRel % pExtent->cSectorsPerGrain);
                                            *pcbPostRead = VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain) - cbToWrite - *pcbPreRead;
                                            rc = VERR_VD_BLOCK_FREE;
                                        }
                                    }
                                    else
                                        rc = vmdkStreamAllocGrain(pImage, pExtent, uSectorExtentRel,
                                                                  pIoCtx, cbToWrite);
                                }
                                else
                                {
                                    if (pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED)
                                    {
                                        /* A partial write to a streamOptimized image is simply
                                         * invalid. It requires rewriting already compressed data
                                         * which is somewhere between expensive and impossible. */
                                        rc = VERR_VD_VMDK_INVALID_STATE;
                                        pExtent->uGrainSectorAbs = 0;
                                        AssertRC(rc);
                                    }
                                    else
                                    {
                                        Assert(!(pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED));
                                        rc = vdIfIoIntFileWriteUser(pImage->pIfIo, pExtent->pFile->pStorage,
                                                                    VMDK_SECTOR2BYTE(uSectorExtentAbs),
                                                                    pIoCtx, cbToWrite, NULL, NULL);
                                    }
                                }
                            }
                        }
                        break;
                    case VMDKETYPE_VMFS:
                    case VMDKETYPE_FLAT:
                        /* Clip write range to remain in this extent. */
                        cbToWrite = RT_MIN(cbToWrite, VMDK_SECTOR2BYTE(pExtent->uSectorOffset + pExtent->cNominalSectors - uSectorExtentRel));
                        rc = vdIfIoIntFileWriteUser(pImage->pIfIo, pExtent->pFile->pStorage,
                                                    VMDK_SECTOR2BYTE(uSectorExtentRel),
                                                    pIoCtx, cbToWrite, NULL, NULL);
                        break;
                    case VMDKETYPE_ZERO:
                        /* Clip write range to remain in this extent. */
                        cbToWrite = RT_MIN(cbToWrite, VMDK_SECTOR2BYTE(pExtent->uSectorOffset + pExtent->cNominalSectors - uSectorExtentRel));
                        break;
                }
            }

            if (pcbWriteProcess)
                *pcbWriteProcess = cbToWrite;
        }
    }
    else
        rc = VERR_VD_IMAGE_READ_ONLY;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnFlush */
static DECLCALLBACK(int) vmdkFlush(void *pBackendData, PVDIOCTX pIoCtx)
{
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;

    return vmdkFlushImage(pImage, pIoCtx);
}

/** @copydoc VDIMAGEBACKEND::pfnGetVersion */
static DECLCALLBACK(unsigned) vmdkGetVersion(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;

    AssertPtrReturn(pImage, 0);

    return VMDK_IMAGE_VERSION;
}

/** @copydoc VDIMAGEBACKEND::pfnGetFileSize */
static DECLCALLBACK(uint64_t) vmdkGetFileSize(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    uint64_t cb = 0;

    AssertPtrReturn(pImage, 0);

    if (pImage->pFile != NULL)
    {
        uint64_t cbFile;
        int rc = vdIfIoIntFileGetSize(pImage->pIfIo, pImage->pFile->pStorage, &cbFile);
        if (RT_SUCCESS(rc))
            cb += cbFile;
    }
    for (unsigned i = 0; i < pImage->cExtents; i++)
    {
        if (pImage->pExtents[i].pFile != NULL)
        {
            uint64_t cbFile;
            int rc = vdIfIoIntFileGetSize(pImage->pIfIo, pImage->pExtents[i].pFile->pStorage, &cbFile);
            if (RT_SUCCESS(rc))
                cb += cbFile;
        }
    }

    LogFlowFunc(("returns %lld\n", cb));
    return cb;
}

/** @copydoc VDIMAGEBACKEND::pfnGetPCHSGeometry */
static DECLCALLBACK(int) vmdkGetPCHSGeometry(void *pBackendData, PVDGEOMETRY pPCHSGeometry)
{
    LogFlowFunc(("pBackendData=%#p pPCHSGeometry=%#p\n", pBackendData, pPCHSGeometry));
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    if (pImage->PCHSGeometry.cCylinders)
        *pPCHSGeometry = pImage->PCHSGeometry;
    else
        rc = VERR_VD_GEOMETRY_NOT_SET;

    LogFlowFunc(("returns %Rrc (PCHS=%u/%u/%u)\n", rc, pPCHSGeometry->cCylinders, pPCHSGeometry->cHeads, pPCHSGeometry->cSectors));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnSetPCHSGeometry */
static DECLCALLBACK(int) vmdkSetPCHSGeometry(void *pBackendData, PCVDGEOMETRY pPCHSGeometry)
{
    LogFlowFunc(("pBackendData=%#p pPCHSGeometry=%#p PCHS=%u/%u/%u\n",
                 pBackendData, pPCHSGeometry, pPCHSGeometry->cCylinders, pPCHSGeometry->cHeads, pPCHSGeometry->cSectors));
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
    {
        if (!(pImage->uOpenFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED))
        {
            rc = vmdkDescSetPCHSGeometry(pImage, pPCHSGeometry);
            if (RT_SUCCESS(rc))
                pImage->PCHSGeometry = *pPCHSGeometry;
        }
        else
            rc = VERR_NOT_SUPPORTED;
    }
    else
        rc = VERR_VD_IMAGE_READ_ONLY;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetLCHSGeometry */
static DECLCALLBACK(int) vmdkGetLCHSGeometry(void *pBackendData, PVDGEOMETRY pLCHSGeometry)
{
    LogFlowFunc(("pBackendData=%#p pLCHSGeometry=%#p\n", pBackendData, pLCHSGeometry));
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    if (pImage->LCHSGeometry.cCylinders)
        *pLCHSGeometry = pImage->LCHSGeometry;
    else
        rc = VERR_VD_GEOMETRY_NOT_SET;

    LogFlowFunc(("returns %Rrc (LCHS=%u/%u/%u)\n", rc, pLCHSGeometry->cCylinders, pLCHSGeometry->cHeads, pLCHSGeometry->cSectors));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnSetLCHSGeometry */
static DECLCALLBACK(int) vmdkSetLCHSGeometry(void *pBackendData, PCVDGEOMETRY pLCHSGeometry)
{
    LogFlowFunc(("pBackendData=%#p pLCHSGeometry=%#p LCHS=%u/%u/%u\n",
                 pBackendData, pLCHSGeometry, pLCHSGeometry->cCylinders, pLCHSGeometry->cHeads, pLCHSGeometry->cSectors));
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
    {
        if (!(pImage->uOpenFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED))
        {
            rc = vmdkDescSetLCHSGeometry(pImage, pLCHSGeometry);
            if (RT_SUCCESS(rc))
                pImage->LCHSGeometry = *pLCHSGeometry;
        }
        else
            rc = VERR_NOT_SUPPORTED;
    }
    else
        rc = VERR_VD_IMAGE_READ_ONLY;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnQueryRegions */
static DECLCALLBACK(int) vmdkQueryRegions(void *pBackendData, PCVDREGIONLIST *ppRegionList)
{
    LogFlowFunc(("pBackendData=%#p ppRegionList=%#p\n", pBackendData, ppRegionList));
    PVMDKIMAGE pThis = (PVMDKIMAGE)pBackendData;

    AssertPtrReturn(pThis, VERR_VD_NOT_OPENED);

    *ppRegionList = &pThis->RegionList;
    LogFlowFunc(("returns %Rrc\n", VINF_SUCCESS));
    return VINF_SUCCESS;
}

/** @copydoc VDIMAGEBACKEND::pfnRegionListRelease */
static DECLCALLBACK(void) vmdkRegionListRelease(void *pBackendData, PCVDREGIONLIST pRegionList)
{
    RT_NOREF1(pRegionList);
    LogFlowFunc(("pBackendData=%#p pRegionList=%#p\n", pBackendData, pRegionList));
    PVMDKIMAGE pThis = (PVMDKIMAGE)pBackendData;
    AssertPtr(pThis); RT_NOREF(pThis);

    /* Nothing to do here. */
}

/** @copydoc VDIMAGEBACKEND::pfnGetImageFlags */
static DECLCALLBACK(unsigned) vmdkGetImageFlags(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;

    AssertPtrReturn(pImage, 0);

    LogFlowFunc(("returns %#x\n", pImage->uImageFlags));
    return pImage->uImageFlags;
}

/** @copydoc VDIMAGEBACKEND::pfnGetOpenFlags */
static DECLCALLBACK(unsigned) vmdkGetOpenFlags(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;

    AssertPtrReturn(pImage, 0);

    LogFlowFunc(("returns %#x\n", pImage->uOpenFlags));
    return pImage->uOpenFlags;
}

/** @copydoc VDIMAGEBACKEND::pfnSetOpenFlags */
static DECLCALLBACK(int) vmdkSetOpenFlags(void *pBackendData, unsigned uOpenFlags)
{
    LogFlowFunc(("pBackendData=%#p uOpenFlags=%#x\n", pBackendData, uOpenFlags));
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    int rc;

    /* Image must be opened and the new flags must be valid. */
    if (!pImage || (uOpenFlags & ~(  VD_OPEN_FLAGS_READONLY | VD_OPEN_FLAGS_INFO
                                   | VD_OPEN_FLAGS_ASYNC_IO | VD_OPEN_FLAGS_SHAREABLE
                                   | VD_OPEN_FLAGS_SEQUENTIAL | VD_OPEN_FLAGS_SKIP_CONSISTENCY_CHECKS)))
        rc = VERR_INVALID_PARAMETER;
    else
    {
        /* StreamOptimized images need special treatment: reopen is prohibited. */
        if (pImage->uImageFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED)
        {
            if (pImage->uOpenFlags == uOpenFlags)
                rc = VINF_SUCCESS;
            else
                rc = VERR_INVALID_PARAMETER;
        }
        else
        {
            /* Implement this operation via reopening the image. */
            vmdkFreeImage(pImage, false, true /*fFlush*/);
            rc = vmdkOpenImage(pImage, uOpenFlags);
        }
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetComment */
static DECLCALLBACK(int) vmdkGetComment(void *pBackendData, char *pszComment, size_t cbComment)
{
    LogFlowFunc(("pBackendData=%#p pszComment=%#p cbComment=%zu\n", pBackendData, pszComment, cbComment));
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    char *pszCommentEncoded = NULL;
    int rc = vmdkDescDDBGetStr(pImage, &pImage->Descriptor,
                               "ddb.comment", &pszCommentEncoded);
    if (rc == VERR_VD_VMDK_VALUE_NOT_FOUND)
    {
        pszCommentEncoded = NULL;
        rc = VINF_SUCCESS;
    }

    if (RT_SUCCESS(rc))
    {
        if (pszComment && pszCommentEncoded)
            rc = vmdkDecodeString(pszCommentEncoded, pszComment, cbComment);
        else if (pszComment)
                *pszComment = '\0';

        if (pszCommentEncoded)
            RTMemTmpFree(pszCommentEncoded);
    }

    LogFlowFunc(("returns %Rrc comment='%s'\n", rc, pszComment));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnSetComment */
static DECLCALLBACK(int) vmdkSetComment(void *pBackendData, const char *pszComment)
{
    LogFlowFunc(("pBackendData=%#p pszComment=\"%s\"\n", pBackendData, pszComment));
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    int rc;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
    {
        if (!(pImage->uOpenFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED))
            rc = vmdkSetImageComment(pImage, pszComment);
        else
            rc = VERR_NOT_SUPPORTED;
    }
    else
        rc = VERR_VD_IMAGE_READ_ONLY;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetUuid */
static DECLCALLBACK(int) vmdkGetUuid(void *pBackendData, PRTUUID pUuid)
{
    LogFlowFunc(("pBackendData=%#p pUuid=%#p\n", pBackendData, pUuid));
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    *pUuid = pImage->ImageUuid;

    LogFlowFunc(("returns %Rrc (%RTuuid)\n", VINF_SUCCESS, pUuid));
    return VINF_SUCCESS;
}

/** @copydoc VDIMAGEBACKEND::pfnSetUuid */
static DECLCALLBACK(int) vmdkSetUuid(void *pBackendData, PCRTUUID pUuid)
{
    LogFlowFunc(("pBackendData=%#p Uuid=%RTuuid\n", pBackendData, pUuid));
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
    {
        if (!(pImage->uOpenFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED))
        {
            pImage->ImageUuid = *pUuid;
            rc = vmdkDescDDBSetUuid(pImage, &pImage->Descriptor,
                                    VMDK_DDB_IMAGE_UUID, pUuid);
            if (RT_FAILURE(rc))
                rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                               N_("VMDK: error storing image UUID in descriptor in '%s'"), pImage->pszFilename);
        }
        else
            rc = VERR_NOT_SUPPORTED;
    }
    else
        rc = VERR_VD_IMAGE_READ_ONLY;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetModificationUuid */
static DECLCALLBACK(int) vmdkGetModificationUuid(void *pBackendData, PRTUUID pUuid)
{
    LogFlowFunc(("pBackendData=%#p pUuid=%#p\n", pBackendData, pUuid));
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    *pUuid = pImage->ModificationUuid;

    LogFlowFunc(("returns %Rrc (%RTuuid)\n", VINF_SUCCESS, pUuid));
    return VINF_SUCCESS;
}

/** @copydoc VDIMAGEBACKEND::pfnSetModificationUuid */
static DECLCALLBACK(int) vmdkSetModificationUuid(void *pBackendData, PCRTUUID pUuid)
{
    LogFlowFunc(("pBackendData=%#p Uuid=%RTuuid\n", pBackendData, pUuid));
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
    {
        if (!(pImage->uOpenFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED))
        {
            /* Only touch the modification uuid if it changed. */
            if (RTUuidCompare(&pImage->ModificationUuid, pUuid))
            {
                pImage->ModificationUuid = *pUuid;
                rc = vmdkDescDDBSetUuid(pImage, &pImage->Descriptor,
                                        VMDK_DDB_MODIFICATION_UUID, pUuid);
                if (RT_FAILURE(rc))
                    rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: error storing modification UUID in descriptor in '%s'"), pImage->pszFilename);
            }
        }
        else
            rc = VERR_NOT_SUPPORTED;
    }
    else
        rc = VERR_VD_IMAGE_READ_ONLY;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetParentUuid */
static DECLCALLBACK(int) vmdkGetParentUuid(void *pBackendData, PRTUUID pUuid)
{
    LogFlowFunc(("pBackendData=%#p pUuid=%#p\n", pBackendData, pUuid));
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    *pUuid = pImage->ParentUuid;

    LogFlowFunc(("returns %Rrc (%RTuuid)\n", VINF_SUCCESS, pUuid));
    return VINF_SUCCESS;
}

/** @copydoc VDIMAGEBACKEND::pfnSetParentUuid */
static DECLCALLBACK(int) vmdkSetParentUuid(void *pBackendData, PCRTUUID pUuid)
{
    LogFlowFunc(("pBackendData=%#p Uuid=%RTuuid\n", pBackendData, pUuid));
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
    {
        if (!(pImage->uOpenFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED))
        {
            pImage->ParentUuid = *pUuid;
            rc = vmdkDescDDBSetUuid(pImage, &pImage->Descriptor,
                                    VMDK_DDB_PARENT_UUID, pUuid);
            if (RT_FAILURE(rc))
                rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                               N_("VMDK: error storing parent image UUID in descriptor in '%s'"), pImage->pszFilename);
        }
        else
            rc = VERR_NOT_SUPPORTED;
    }
    else
        rc = VERR_VD_IMAGE_READ_ONLY;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetParentModificationUuid */
static DECLCALLBACK(int) vmdkGetParentModificationUuid(void *pBackendData, PRTUUID pUuid)
{
    LogFlowFunc(("pBackendData=%#p pUuid=%#p\n", pBackendData, pUuid));
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    *pUuid = pImage->ParentModificationUuid;

    LogFlowFunc(("returns %Rrc (%RTuuid)\n", VINF_SUCCESS, pUuid));
    return VINF_SUCCESS;
}

/** @copydoc VDIMAGEBACKEND::pfnSetParentModificationUuid */
static DECLCALLBACK(int) vmdkSetParentModificationUuid(void *pBackendData, PCRTUUID pUuid)
{
    LogFlowFunc(("pBackendData=%#p Uuid=%RTuuid\n", pBackendData, pUuid));
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
    {
        if (!(pImage->uOpenFlags & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED))
        {
            pImage->ParentModificationUuid = *pUuid;
            rc = vmdkDescDDBSetUuid(pImage, &pImage->Descriptor,
                                    VMDK_DDB_PARENT_MODIFICATION_UUID, pUuid);
            if (RT_FAILURE(rc))
                rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: error storing parent image UUID in descriptor in '%s'"), pImage->pszFilename);
        }
        else
            rc = VERR_NOT_SUPPORTED;
    }
    else
        rc = VERR_VD_IMAGE_READ_ONLY;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnDump */
static DECLCALLBACK(void) vmdkDump(void *pBackendData)
{
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;

    AssertPtrReturnVoid(pImage);
    vdIfErrorMessage(pImage->pIfError, "Header: Geometry PCHS=%u/%u/%u LCHS=%u/%u/%u cbSector=%llu\n",
                     pImage->PCHSGeometry.cCylinders, pImage->PCHSGeometry.cHeads, pImage->PCHSGeometry.cSectors,
                     pImage->LCHSGeometry.cCylinders, pImage->LCHSGeometry.cHeads, pImage->LCHSGeometry.cSectors,
                     VMDK_BYTE2SECTOR(pImage->cbSize));
    vdIfErrorMessage(pImage->pIfError, "Header: uuidCreation={%RTuuid}\n", &pImage->ImageUuid);
    vdIfErrorMessage(pImage->pIfError, "Header: uuidModification={%RTuuid}\n", &pImage->ModificationUuid);
    vdIfErrorMessage(pImage->pIfError, "Header: uuidParent={%RTuuid}\n", &pImage->ParentUuid);
    vdIfErrorMessage(pImage->pIfError, "Header: uuidParentModification={%RTuuid}\n", &pImage->ParentModificationUuid);
}


/**
 * Returns the size, in bytes, of the sparse extent overhead for
 * the number of desired total sectors and based on the current
 * sectors of the extent.
 *
 * @returns uint64_t size of new overhead in bytes.
 * @param   pExtent         VMDK extent instance.
 * @param   cSectorsNew     Number of desired total sectors.
 */
static uint64_t vmdkGetNewOverhead(PVMDKEXTENT pExtent, uint64_t cSectorsNew)
{
    uint32_t cGDEntries = (cSectorsNew + pExtent->cSectorsPerGDE - 1) / pExtent->cSectorsPerGDE;
    size_t cbGD = cGDEntries * sizeof(uint32_t);
    size_t cbGDRounded = RT_ALIGN_64(cbGD, 512);
    size_t cbGTRounded = RT_ALIGN_64(cGDEntries * pExtent->cGTEntries * sizeof(uint32_t), 512);
    size_t cbOverhead  = VMDK_SECTOR2BYTE(RT_MAX(pExtent->uDescriptorSector + pExtent->cDescriptorSectors, 1)) + cbGDRounded + cbGTRounded;
    cbOverhead += cbGDRounded + cbGTRounded; // Add redundant copies
    cbOverhead = RT_ALIGN_64(cbOverhead,
                             VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain)); // Align to grain
    return cbOverhead;
}

/**
 * Internal: Replaces the size (in sectors) of an extent in the descriptor file.
 *
 * @returns VBox status code.
 * @param   pImage              VMDK image instance.
 * @param   pExtent             VMDK extent instance.
 * @param   uLine               Line number of descriptor to change.
 * @param   cSectorsOld         Existing number of sectors.
 * @param   cSectorsNew         New number of sectors.
 */
static int vmdkReplaceExtentSize(PVMDKIMAGE pImage, PVMDKEXTENT pExtent, unsigned uLine, uint64_t cSectorsOld,
                                 uint64_t cSectorsNew)
{
    char szOldExtentSectors[UINT64_MAX_BUFF_SIZE];
    char szNewExtentSectors[UINT64_MAX_BUFF_SIZE];

    ssize_t cbWritten = RTStrPrintf2(szOldExtentSectors, sizeof(szOldExtentSectors), "%llu", cSectorsOld);
    if (cbWritten <= 0 || cbWritten > (ssize_t)sizeof(szOldExtentSectors))
        return VERR_BUFFER_OVERFLOW;

    cbWritten = RTStrPrintf2(szNewExtentSectors, sizeof(szNewExtentSectors), "%llu", cSectorsNew);
    if (cbWritten <= 0 || cbWritten > (ssize_t)sizeof(szNewExtentSectors))
        return VERR_BUFFER_OVERFLOW;

    char *pszNewExtentLine = vmdkStrReplace(pImage->Descriptor.aLines[uLine],
                                            szOldExtentSectors,
                                            szNewExtentSectors);

    if (RT_UNLIKELY(!pszNewExtentLine))
        return VERR_INVALID_PARAMETER;

    vmdkDescExtRemoveByLine(pImage, &pImage->Descriptor, uLine);
    vmdkDescExtInsert(pImage, &pImage->Descriptor,
                      pExtent->enmAccess, cSectorsNew,
                      pExtent->enmType, pExtent->pszBasename, pExtent->uSectorOffset);

    RTStrFree(pszNewExtentLine);
    pszNewExtentLine = NULL;

    pImage->Descriptor.fDirty = true;

    return VINF_SUCCESS;
}

/**
 * Moves sectors down to make room for new overhead.
 * Used for sparse extent resize.
 *
 * @returns VBox status code.
 * @param   pImage                  VMDK image instance.
 * @param   pExtent                 VMDK extent instance.
 * @param   cSectorsNew             Number of sectors after resize.
 * @param   cNewOverheadSectors     The size of new overhead in sectors. This
 *                                  includes the header and all grain tables/directories.
 */
static int vmdkRelocateSectorsForSparseResize(PVMDKIMAGE pImage, PVMDKEXTENT pExtent,
                                              uint64_t cSectorsNew, uint64_t cNewOverheadSectors)
{
    int rc = VINF_SUCCESS;

    pExtent->fMetaDirty = true;

    size_t cbGD = pExtent->cGDEntries * sizeof(uint32_t);
    size_t cbGDRounded = RT_ALIGN_64(cbGD, 512);
    size_t cbGTRounded = RT_ALIGN_64(pExtent->cGDEntries * pExtent->cGTEntries * sizeof(uint32_t), 512);
    size_t cbOverhead  = VMDK_SECTOR2BYTE(RT_MAX(pExtent->uDescriptorSector + pExtent->cDescriptorSectors, 1)) + cbGDRounded + cbGTRounded;
    cbOverhead += cbGDRounded + cbGTRounded; // Add redundant copies
    cbOverhead = RT_ALIGN_64(cbOverhead,
                             VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain)); // Align to grain

    uint64_t uOldDataStartSector = pExtent->cOverheadSectors;
    size_t uSectorDiff = cNewOverheadSectors - uOldDataStartSector;

    pExtent->uAppendPosition += RT_ALIGN_64(VMDK_SECTOR2BYTE(cNewOverheadSectors), _64K);

    if (pExtent->cSectors < cSectorsNew)
    {
        uint64_t cbFile;
        rc = vdIfIoIntFileGetSize(pImage->pIfIo, pExtent->pFile->pStorage, &cbFile);
        if (RT_SUCCESS(rc))
        {
            rc = vdIfIoIntFileSetSize(pImage->pIfIo, pExtent->pFile->pStorage, cbFile + VMDK_SECTOR2BYTE(uSectorDiff) + _64K);
            if (RT_FAILURE(rc))
                return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: cannot set size of '%s'"), pExtent->pszFullname);
        }
        else
            return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: cannot get size of '%s'"), pExtent->pszFullname);
    }


    // 1. Move GT down
    size_t cbGT = pExtent->cGTEntries * sizeof(uint32_t);
    Assert(cbGT > 0);

    uint32_t *pTmpGT1 = (uint32_t *)RTMemAlloc(cbGT);
    uint32_t *pTmpGT2 = (uint32_t *)RTMemAlloc(cbGT);

    if (   !pTmpGT1
        || !pTmpGT2)
        return VERR_NO_MEMORY;

    size_t i = pExtent->cGDEntries;
    uint32_t *pGDTmp = pExtent->pGD;
    uint32_t *pRGDTmp = pExtent->pRGD;

    /* Loop through all entries. */
    while (i-- > 0)
    {
        uint32_t uGTFlag = pGDTmp[i];
        uint32_t uRGTFlag = pRGDTmp[i];
        size_t   cbGTRead = cbGT;

        /* If no grain table is allocated skip the entry. */
        if (*pGDTmp == 0 && *pRGDTmp == 0)
        {
            i--;
            continue;
        }

        if (RT_SUCCESS(rc))
        {

            rc = vdIfIoIntFileReadSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                       VMDK_SECTOR2BYTE(uGTFlag),
                                       pTmpGT1, cbGTRead);
            if (RT_FAILURE(rc))
            {
                rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                               N_("VMDK: error reading grain table in '%s'"), pExtent->pszFullname);
                break;
            }

            rc = vdIfIoIntFileReadSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                       VMDK_SECTOR2BYTE(uRGTFlag),
                                       pTmpGT2, cbGTRead);
            if (RT_FAILURE(rc))
            {
                rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                               N_("VMDK: error reading backup grain table in '%s'"), pExtent->pszFullname);
                break;
            }

            size_t cGTEsRead = cbGTRead / sizeof(uint32_t);
            char *pGrainTmp = (char *)RTMemAlloc(VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain));
            for (int64_t j = cGTEsRead - 1; j >= 0; j--)
            {
                if (pTmpGT1[j] != 0)
                {
                    uint32_t uGTSector = RT_LE2H_U32(pTmpGT1[j]);
                    rc = vdIfIoIntFileReadSync(pImage->pIfIo, pExtent->pFile->pStorage,
                        VMDK_SECTOR2BYTE(uGTSector),
                        pGrainTmp, VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain));

                    pTmpGT1[j] = RT_H2LE_U32(VMDK_BYTE2SECTOR(pExtent->uAppendPosition));
                    pTmpGT2[j] = RT_H2LE_U32(VMDK_BYTE2SECTOR(pExtent->uAppendPosition));

                    /* Write the grain to disk, moved down a bit. */
                    rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pExtent->pFile->pStorage,
                        pExtent->uAppendPosition,
                        pGrainTmp, VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain));

                    pExtent->uAppendPosition += VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain);
                }
            }

            if (pGrainTmp)
                RTMemFree(pGrainTmp);

            /* Write the grain table to disk, with updated locations of grains. */
            rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pExtent->pFile->pStorage,
                VMDK_SECTOR2BYTE(uGTFlag),
                pTmpGT1, cbGTRead);

            rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pExtent->pFile->pStorage,
                VMDK_SECTOR2BYTE(uRGTFlag),
                pTmpGT2, cbGTRead);
        }
    } /* while (i >= 0) */

    /** @todo figure out what to do for unclean VMDKs. */
    if (pTmpGT1)
        RTMemFree(pTmpGT1);
    if (pTmpGT2)
        RTMemFree(pTmpGT2);

    if (RT_SUCCESS(rc))
    {
        pExtent->cOverheadSectors = cNewOverheadSectors;
        pExtent->cSectors = cSectorsNew;
    }

    return rc;
}

/**
 * Resizes meta/overhead for sparse extent resize.
 *
 * @returns VBox status code.
 * @param   pImage          VMDK image instance.
 * @param   pExtent         VMDK extent instance.
 * @param   cSectorsNew     Size of new image in sectors.
 */
static int vmdkResizeSparseMeta(PVMDKIMAGE pImage, PVMDKEXTENT pExtent, uint64_t cSectorsNew)
{
    int rc = VINF_SUCCESS;

    pExtent->fMetaDirty = true;

    size_t cbOldGD = pExtent->cGDEntries * sizeof(uint32_t);
    size_t cbOldGDRounded = RT_ALIGN_64(cbOldGD, 512);

    uint32_t cNewGDEntries = (cSectorsNew + pExtent->cSectorsPerGDE - 1) / pExtent->cSectorsPerGDE;
    size_t cbNewGD = cNewGDEntries * sizeof(uint32_t);
    size_t cbNewGDRounded = RT_ALIGN_64(cbNewGD, 512);
    size_t cbNewGTRounded = RT_ALIGN_64(cNewGDEntries * pExtent->cGTEntries * sizeof(uint32_t), 512);

    {
        // move and grow the grain table
        size_t cbGT = pExtent->cGTEntries * sizeof(uint32_t);
        uint32_t *pTmpGT1 = (uint32_t *)RTMemAlloc(cbGT);
        if (!pTmpGT1)
            return VERR_NO_MEMORY;

#ifdef RT_STRICT
        uint64_t uGTMin = RT_MAX(pExtent->uDescriptorSector + pExtent->cDescriptorSectors, 1) + VMDK_BYTE2SECTOR(cbNewGDRounded + cbNewGTRounded) + VMDK_BYTE2SECTOR(cbNewGDRounded); //inclusive
        uint64_t uGTMax = pExtent->cOverheadSectors; //not inclusive
#endif

        size_t i = pExtent->cGDEntries - 1;
        /* Loop through all entries. */
        do
        {
            uint64_t uNewGTStart = RT_MAX(pExtent->uDescriptorSector + pExtent->cDescriptorSectors, 1) + VMDK_BYTE2SECTOR(cbNewGDRounded + cbNewGTRounded + cbNewGDRounded) + (i * sizeof(uint32_t));
            uint64_t uOldGTStart = pExtent->pGD[i];
            size_t   cbGTRead = cbGT;

            Assert(uNewGTStart < uGTMax && uNewGTStart >= uGTMin);

            rc = vdIfIoIntFileReadSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                    VMDK_SECTOR2BYTE(uOldGTStart),
                                    pTmpGT1, cbGTRead);
            if (RT_FAILURE(rc))
            {
                rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                            N_("VMDK: error reading grain table in '%s'"), pExtent->pszFullname);
                break;
            }

            /* Write the grain to disk, moved down a bit. */
            rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                        VMDK_SECTOR2BYTE(uNewGTStart),
                                        pTmpGT1, cbGTRead);

            if (RT_FAILURE(rc))
            {
                rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                            N_("VMDK: error relocating grain table in '%s'"), pExtent->pszFullname);
                break;
            }
        } while (i-- > 0);

        /** @todo figure out what to do for unclean VMDKs. */
        if (pTmpGT1)
            RTMemFree(pTmpGT1);
    }

    // rewrite grain directory
    if (RT_SUCCESS(rc))
    {
        // resize internal grain directories (plural) structs
        pExtent->pGD = (uint32_t *)RTMemReallocZ(pExtent->pGD, cbOldGDRounded, cbNewGD);
        if (RT_UNLIKELY(!pExtent->pGD))
            rc = VERR_NO_MEMORY;
        else
        {
            uint32_t uGTSectorLE;
            uint64_t uOffsetSectors = RT_MAX(pExtent->uDescriptorSector + pExtent->cDescriptorSectors, 1) + VMDK_BYTE2SECTOR(cbNewGDRounded + cbNewGTRounded + cbNewGDRounded);
            uint64_t uNewGD = RT_MAX(pExtent->uDescriptorSector + pExtent->cDescriptorSectors, 1) + VMDK_BYTE2SECTOR(cbNewGDRounded + cbNewGTRounded);
            for (size_t i = 0; i < cNewGDEntries; i++)
            {
                pExtent->pGD[i] = uOffsetSectors;
                uGTSectorLE = RT_H2LE_U64(uOffsetSectors);
                /* Write the grain directory entry to disk. */
                rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                            VMDK_SECTOR2BYTE(uNewGD) + i * sizeof(uGTSectorLE),
                                            &uGTSectorLE, sizeof(uGTSectorLE));
                if (RT_FAILURE(rc))
                {
                    rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: cannot write new grain directory entry in '%s'"), pExtent->pszFullname);
                    break;
                }

                uOffsetSectors += VMDK_BYTE2SECTOR(pExtent->cGTEntries * sizeof(uint32_t));
            }
        }
    }

    // zero out new grain tables
    if (RT_SUCCESS(rc))
    {
        size_t cbGTDataTmp = pExtent->cGTEntries * sizeof(uint32_t);
        uint32_t *paGTDataTmp = (uint32_t *)RTMemTmpAllocZ(cbGTDataTmp);

        if (RT_UNLIKELY(!paGTDataTmp))
            rc = VERR_NO_MEMORY;
        else
        {
            memset(paGTDataTmp, '\0', cbGTDataTmp);

            for (size_t i = pExtent->cGDEntries; i < cNewGDEntries; i++)
            {
                uint32_t uGTSector = pExtent->pGD[i];
                /* Zero out new grain table. */
                rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                            VMDK_SECTOR2BYTE(uGTSector),
                                            paGTDataTmp, cbGTDataTmp);

                if (RT_FAILURE(rc))
                {
                    rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: cannot write zero'd grain in '%s'"), pExtent->pszFullname);
                    break;
                }
            }
        }
    }

    // rewrite the redundant grain table
    if (RT_SUCCESS(rc))
    {
        size_t cbGT = pExtent->cGTEntries * sizeof(uint32_t);
        size_t cbGTBuffers = cbGT; /* Start with space for one GT. */

        uint32_t *pTmpGT1 = (uint32_t *)RTMemAlloc(cbGTBuffers);

#ifdef RT_STRICT
        uint64_t uGTMin = pExtent->pRGD[0] + VMDK_BYTE2SECTOR(cbNewGDRounded - cbOldGDRounded); //inclusive
        uint64_t uGTMax = RT_MAX(pExtent->uDescriptorSector + pExtent->cDescriptorSectors, 1) + VMDK_BYTE2SECTOR(cbNewGDRounded + cbNewGTRounded); //not inclusive
#endif

        if (RT_UNLIKELY(!pTmpGT1))
            rc = VERR_NO_MEMORY;
        else
        {
            size_t i = pExtent->cGDEntries - 1;

            /* Loop through all entries. */
            do
            {
                uint64_t uNewGTStart = pExtent->uSectorRGD + VMDK_BYTE2SECTOR(cbNewGDRounded) + (i * sizeof(uint32_t));
                uint64_t uOldGTStart = pExtent->pRGD[i];
                size_t   cbGTRead = cbGT;

                Assert(uNewGTStart < uGTMax && uNewGTStart >= uGTMin);

                if (RT_SUCCESS(rc))
                {
                    rc = vdIfIoIntFileReadSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                            VMDK_SECTOR2BYTE(uOldGTStart),
                                            pTmpGT1, cbGTRead);
                    if (RT_FAILURE(rc))
                    {
                        rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                                    N_("VMDK: error reading grain table in '%s'"), pExtent->pszFullname);
                        break;
                    }

                    /* Write the grain to disk, moved down a bit. */
                    rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                                VMDK_SECTOR2BYTE(uNewGTStart),
                                                pTmpGT1, cbGTRead);

                    if (RT_FAILURE(rc))
                    {
                        rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                                    N_("VMDK: error writing grain table in '%s'"), pExtent->pszFullname);
                        break;
                    }
                }
            } while (i-- > 0);
        }
    }

    // rewrite redundant grain directory
    if (RT_SUCCESS(rc))
    {
        if (pExtent->uSectorRGD)
        {
            pExtent->pRGD = (uint32_t *)RTMemReallocZ(pExtent->pRGD, cbOldGDRounded, cbNewGD);
            if (RT_UNLIKELY(!pExtent->pRGD))
                rc = VERR_NO_MEMORY;
        }

        if (RT_SUCCESS(rc) && pExtent->pRGD)
        {
            // uSectorRGD doesn't get changed unless the descriptor changes (which it shouldn't on resize)
            uint32_t uGTSectorLE;
            uint64_t uOffsetSectors = pExtent->uSectorRGD + VMDK_BYTE2SECTOR(cbNewGDRounded);
            for (size_t i = 0; i < cNewGDEntries; i++)
            {
                pExtent->pRGD[i] = uOffsetSectors;
                uGTSectorLE = RT_H2LE_U64(uOffsetSectors);
                /* Write the redundant grain directory entry to disk. */
                rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                            VMDK_SECTOR2BYTE(pExtent->uSectorRGD) + i * sizeof(uGTSectorLE),
                                            &uGTSectorLE, sizeof(uGTSectorLE));
                if (RT_FAILURE(rc))
                {
                    rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: cannot write new redundant grain directory entry in '%s'"), pExtent->pszFullname);
                    break;
                }
                uOffsetSectors += VMDK_BYTE2SECTOR(pExtent->cGTEntries * sizeof(uint32_t));
            }
        }
    }

    // zero out new redundant grain tables
    if (RT_SUCCESS(rc))
    {
        size_t cbGTDataTmp = pExtent->cGTEntries * sizeof(uint32_t);
        uint32_t *paGTDataTmp = (uint32_t *)RTMemTmpAllocZ(cbGTDataTmp);

        if (RT_UNLIKELY(!paGTDataTmp))
            rc = VERR_NO_MEMORY;
        else
        {
            memset(paGTDataTmp, '\0', cbGTDataTmp);

            for (uint32_t i = pExtent->cGDEntries; i < cNewGDEntries; i++)
            {
                uint32_t uGTSector = pExtent->pRGD[i];
                uint32_t uGTSectorLE = RT_H2LE_U64(uGTSector);
                /* Write the zero'd grain to disk */
                rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pExtent->pFile->pStorage,
                                            VMDK_SECTOR2BYTE(uGTSectorLE),
                                            paGTDataTmp, cbGTDataTmp);

                if (RT_FAILURE(rc))
                {
                    rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: cannot write zero'd grain in '%s'"), pExtent->pszFullname);
                    break;
                }
            }

            // finalize metadata
            pExtent->uSectorGD = RT_MAX(pExtent->uDescriptorSector + pExtent->cDescriptorSectors, 1) + VMDK_BYTE2SECTOR(cbNewGDRounded + cbNewGTRounded);
            pExtent->cGDEntries = (cSectorsNew + pExtent->cSectorsPerGDE - 1) / pExtent->cSectorsPerGDE;
        }
    }

    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnResize */
static DECLCALLBACK(int) vmdkResize(void *pBackendData, uint64_t cbSize,
                                   PCVDGEOMETRY pPCHSGeometry, PCVDGEOMETRY pLCHSGeometry,
                                   unsigned uPercentStart, unsigned uPercentSpan,
                                   PVDINTERFACE pVDIfsDisk, PVDINTERFACE pVDIfsImage,
                                   PVDINTERFACE pVDIfsOperation)
{
    RT_NOREF5(uPercentStart, uPercentSpan, pVDIfsDisk, pVDIfsImage, pVDIfsOperation);

    int rc = VINF_SUCCESS;
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    unsigned uImageFlags = pImage->uImageFlags;
    PVMDKEXTENT pExtent = &pImage->pExtents[0];
    pExtent->fMetaDirty = true;

    uint64_t cSectorsNew = cbSize / VMDK_SECTOR_SIZE;
    if (cbSize % VMDK_SECTOR_SIZE)
        cSectorsNew++;
    cSectorsNew = VMDK_BYTE2SECTOR(RT_ALIGN_64(cbSize, _64K));

    uint64_t cSectorsOld = pImage->cbSize / VMDK_SECTOR_SIZE;
    if (pImage->cbSize % VMDK_SECTOR_SIZE)
        cSectorsOld++;
    unsigned cExtents = pImage->cExtents;

    /* Check size is within min/max bounds. */
    if ( !(uImageFlags & VD_VMDK_IMAGE_FLAGS_RAWDISK)
        && (   !cbSize
            || (!(uImageFlags & VD_IMAGE_FLAGS_FIXED) && cbSize >= _1T * 256 - _64K)) )
        return VERR_VD_INVALID_SIZE;

    /*
     * Making the image smaller is not supported at the moment.
     */
    /** @todo implement making the image smaller, it is the responsibility of
     * the user to know what they're doing. */
    if (cbSize < pImage->cbSize)
        rc = VERR_VD_SHRINK_NOT_SUPPORTED;
    else if (cbSize > pImage->cbSize)
    {
        /*
         * monolithicFlat. FIXED flag and not split up into 2 GB parts.
         */
        if ((uImageFlags & VD_IMAGE_FLAGS_FIXED) && !(uImageFlags & VD_VMDK_IMAGE_FLAGS_SPLIT_2G))
        {
            /** Required space in bytes for the extent after the resize. */
            uint64_t cbSectorSpaceNew = cSectorsNew * VMDK_SECTOR_SIZE;
            pExtent = &pImage->pExtents[0];

            rc = vdIfIoIntFileSetAllocationSize(pImage->pIfIo, pExtent->pFile->pStorage, cbSectorSpaceNew,
                                                0 /* fFlags */, NULL,
                                                uPercentStart, uPercentSpan);
            if (RT_FAILURE(rc))
                return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not set size of new file '%s'"), pExtent->pszFullname);

            rc = vmdkReplaceExtentSize(pImage, pExtent, pImage->Descriptor.uFirstExtent, cSectorsOld, cSectorsNew);
            if (RT_FAILURE(rc))
                return rc;
        }

        /*
         * twoGbMaxExtentFlat. FIXED flag and SPLIT into 2 GB parts.
         */
        if ((uImageFlags & VD_IMAGE_FLAGS_FIXED) && (uImageFlags & VD_VMDK_IMAGE_FLAGS_SPLIT_2G))
        {
            /* Check to see how much space remains in last extent */
            bool fSpaceAvailible = false;
            uint64_t cLastExtentRemSectors = cSectorsOld % VMDK_BYTE2SECTOR(VMDK_2G_SPLIT_SIZE);
            if (cLastExtentRemSectors)
                fSpaceAvailible = true;

            uint64_t cSectorsNeeded = cSectorsNew - cSectorsOld;

            /** Space remaining in current last extent file that we don't need to create another one. */
            if (fSpaceAvailible && cSectorsNeeded + cLastExtentRemSectors <= VMDK_BYTE2SECTOR(VMDK_2G_SPLIT_SIZE))
            {
                pExtent = &pImage->pExtents[cExtents - 1];
                rc = vdIfIoIntFileSetAllocationSize(pImage->pIfIo, pExtent->pFile->pStorage,
                                                    VMDK_SECTOR2BYTE(cSectorsNeeded + cLastExtentRemSectors),
                                                    0 /* fFlags */, NULL, uPercentStart, uPercentSpan);
                if (RT_FAILURE(rc))
                    return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not set size of new file '%s'"), pExtent->pszFullname);

                rc = vmdkReplaceExtentSize(pImage, pExtent, pImage->Descriptor.uFirstExtent + cExtents - 1,
                                           pExtent->cNominalSectors, cSectorsNeeded + cLastExtentRemSectors);
                if (RT_FAILURE(rc))
                    return rc;
            }
            /* Need more extent files to handle all the requested space. */
            else
            {
                if (fSpaceAvailible)
                {
                    pExtent = &pImage->pExtents[cExtents - 1];
                    rc = vdIfIoIntFileSetAllocationSize(pImage->pIfIo, pExtent->pFile->pStorage, VMDK_2G_SPLIT_SIZE,
                                                        0 /* fFlags */, NULL,
                                                        uPercentStart, uPercentSpan);
                    if (RT_FAILURE(rc))
                        return vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VMDK: could not set size of new file '%s'"), pExtent->pszFullname);

                    cSectorsNeeded = cSectorsNeeded - VMDK_BYTE2SECTOR(VMDK_2G_SPLIT_SIZE) + cLastExtentRemSectors;

                    rc = vmdkReplaceExtentSize(pImage, pExtent, pImage->Descriptor.uFirstExtent + cExtents - 1,
                                               pExtent->cNominalSectors, VMDK_BYTE2SECTOR(VMDK_2G_SPLIT_SIZE));
                    if (RT_FAILURE(rc))
                        return rc;
                }

                unsigned cNewExtents = VMDK_SECTOR2BYTE(cSectorsNeeded) / VMDK_2G_SPLIT_SIZE;
                if (cNewExtents % VMDK_2G_SPLIT_SIZE || cNewExtents < VMDK_2G_SPLIT_SIZE)
                    cNewExtents++;

                for (unsigned i = cExtents;
                     i < cExtents + cNewExtents && cSectorsNeeded >= VMDK_BYTE2SECTOR(VMDK_2G_SPLIT_SIZE);
                     i++)
                {
                    rc = vmdkAddFileBackedExtent(pImage, VMDK_2G_SPLIT_SIZE);
                    if (RT_FAILURE(rc))
                        return rc;

                    pExtent = &pImage->pExtents[i];

                    pExtent->cSectors = VMDK_BYTE2SECTOR(VMDK_2G_SPLIT_SIZE);
                    cSectorsNeeded -= VMDK_BYTE2SECTOR(VMDK_2G_SPLIT_SIZE);
                }

                if (cSectorsNeeded)
                {
                    rc = vmdkAddFileBackedExtent(pImage, VMDK_SECTOR2BYTE(cSectorsNeeded));
                    if (RT_FAILURE(rc))
                        return rc;
                }
            }
        }

        /*
         * monolithicSparse.
         */
        if (pExtent->enmType == VMDKETYPE_HOSTED_SPARSE && !(uImageFlags & VD_VMDK_IMAGE_FLAGS_SPLIT_2G))
        {
            // 1. Calculate sectors needed for new overhead.
            uint64_t cbNewOverhead = vmdkGetNewOverhead(pExtent, cSectorsNew);
            uint64_t cOverheadSectorDiff = VMDK_BYTE2SECTOR(cbNewOverhead) - pExtent->cOverheadSectors;

            // 2. Relocate sectors to make room for new GD/GT, update entries in GD/GT
            if (cOverheadSectorDiff > 0)
            {
                if (pExtent->cSectors > 0)
                {
                    /* Do the relocation. */
                    LogFlow(("Relocating VMDK sectors\n"));
                    rc = vmdkRelocateSectorsForSparseResize(pImage, pExtent, cSectorsNew, VMDK_BYTE2SECTOR(cbNewOverhead));
                }

                if (RT_SUCCESS(rc))
                    rc = vmdkResizeSparseMeta(pImage, pExtent, cSectorsNew);

                /** @todo r=jack: only supports single extent */
                if (RT_SUCCESS(rc))
                    rc = vmdkReplaceExtentSize(pImage, pExtent, pImage->Descriptor.uFirstExtent, cSectorsOld, cSectorsNew);
            }
        }

        /*
         * twoGbSparseExtent
         */
        if (pExtent->enmType == VMDKETYPE_HOSTED_SPARSE && (uImageFlags & VD_VMDK_IMAGE_FLAGS_SPLIT_2G))
        {
            /* Check to see how much space remains in last extent */
            bool fSpaceAvailible = false;
            uint64_t cLastExtentRemSectors = cSectorsOld % VMDK_BYTE2SECTOR(VMDK_2G_SPLIT_SIZE);
            if (cLastExtentRemSectors)
                fSpaceAvailible = true;

            uint64_t cSectorsNeeded = cSectorsNew - cSectorsOld;

            if (fSpaceAvailible && cSectorsNeeded + cLastExtentRemSectors <= VMDK_BYTE2SECTOR(VMDK_2G_SPLIT_SIZE))
            {
                pExtent = &pImage->pExtents[cExtents - 1];
                rc = vmdkRelocateSectorsForSparseResize(pImage, pExtent, cSectorsNeeded + cLastExtentRemSectors, vmdkGetNewOverhead(pExtent, cSectorsNeeded + cLastExtentRemSectors));
                if (RT_FAILURE(rc))
                    return rc;

                rc = vmdkFlushImage(pImage, NULL);
                if (RT_FAILURE(rc))
                    return rc;

                rc = vmdkResizeSparseMeta(pImage, pExtent, cSectorsNeeded + cLastExtentRemSectors);
                if (RT_FAILURE(rc))
                    return rc;
            }
            else
            {
                if (fSpaceAvailible)
                {
                    pExtent = &pImage->pExtents[cExtents - 1];
                    rc = vmdkRelocateSectorsForSparseResize(pImage, pExtent, VMDK_BYTE2SECTOR(VMDK_2G_SPLIT_SIZE), vmdkGetNewOverhead(pExtent, VMDK_BYTE2SECTOR(VMDK_2G_SPLIT_SIZE)));
                    if (RT_FAILURE(rc))
                        return rc;

                    rc = vmdkFlushImage(pImage, NULL);
                    if (RT_FAILURE(rc))
                        return rc;

                    rc = vmdkResizeSparseMeta(pImage, pExtent, VMDK_BYTE2SECTOR(VMDK_2G_SPLIT_SIZE));
                    if (RT_FAILURE(rc))
                        return rc;

                    cSectorsNeeded = cSectorsNeeded - VMDK_BYTE2SECTOR(VMDK_2G_SPLIT_SIZE) + cLastExtentRemSectors;
                }

                unsigned cNewExtents = VMDK_SECTOR2BYTE(cSectorsNeeded) / VMDK_2G_SPLIT_SIZE;
                if (cNewExtents % VMDK_2G_SPLIT_SIZE || cNewExtents < VMDK_2G_SPLIT_SIZE)
                    cNewExtents++;

                for (unsigned i = cExtents;
                     i < cExtents + cNewExtents && cSectorsNeeded >= VMDK_BYTE2SECTOR(VMDK_2G_SPLIT_SIZE);
                     i++)
                {
                    rc = vmdkAddFileBackedExtent(pImage, VMDK_2G_SPLIT_SIZE);
                    if (RT_FAILURE(rc))
                        return rc;

                    pExtent = &pImage->pExtents[i];

                    pExtent->cSectors = VMDK_BYTE2SECTOR(VMDK_2G_SPLIT_SIZE);
                    cSectorsNeeded -= VMDK_BYTE2SECTOR(VMDK_2G_SPLIT_SIZE);
                }

                if (cSectorsNeeded)
                {
                    rc = vmdkAddFileBackedExtent(pImage, VMDK_SECTOR2BYTE(cSectorsNeeded));
                    if (RT_FAILURE(rc))
                        return rc;

                    pExtent = &pImage->pExtents[pImage->cExtents];

                    rc = vmdkFlushImage(pImage, NULL);
                    if (RT_FAILURE(rc))
                        return rc;
                }
            }
        }

        /* Successful resize. Update metadata */
        if (RT_SUCCESS(rc))
        {
            /* Update size and new block count. */
            pImage->cbSize = cbSize;
            pExtent->cNominalSectors = cSectorsNew;
            pExtent->cSectors = cSectorsNew;

            /* Update geometry. */
            pImage->PCHSGeometry = *pPCHSGeometry;
            pImage->LCHSGeometry = *pLCHSGeometry;
        }

        /* Update header information in base image file. */
        rc = vmdkWriteDescriptor(pImage, NULL);
        if (RT_SUCCESS(rc))
            rc = vmdkFlushImage(pImage, NULL);
    }
    /* Same size doesn't change the image at all. */

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

const VDIMAGEBACKEND g_VmdkBackend =
{
    /* u32Version */
    VD_IMGBACKEND_VERSION,
    /* pszBackendName */
    "VMDK",
    /* uBackendCaps */
      VD_CAP_UUID | VD_CAP_CREATE_FIXED | VD_CAP_CREATE_DYNAMIC
    | VD_CAP_CREATE_SPLIT_2G | VD_CAP_DIFF | VD_CAP_FILE | VD_CAP_ASYNC
    | VD_CAP_VFS | VD_CAP_PREFERRED,
    /* paFileExtensions */
    s_aVmdkFileExtensions,
    /* paConfigInfo */
    s_aVmdkConfigInfo,
    /* pfnProbe */
    vmdkProbe,
    /* pfnOpen */
    vmdkOpen,
    /* pfnCreate */
    vmdkCreate,
    /* pfnRename */
    vmdkRename,
    /* pfnClose */
    vmdkClose,
    /* pfnRead */
    vmdkRead,
    /* pfnWrite */
    vmdkWrite,
    /* pfnFlush */
    vmdkFlush,
    /* pfnDiscard */
    NULL,
    /* pfnGetVersion */
    vmdkGetVersion,
    /* pfnGetFileSize */
    vmdkGetFileSize,
    /* pfnGetPCHSGeometry */
    vmdkGetPCHSGeometry,
    /* pfnSetPCHSGeometry */
    vmdkSetPCHSGeometry,
    /* pfnGetLCHSGeometry */
    vmdkGetLCHSGeometry,
    /* pfnSetLCHSGeometry */
    vmdkSetLCHSGeometry,
    /* pfnQueryRegions */
    vmdkQueryRegions,
    /* pfnRegionListRelease */
    vmdkRegionListRelease,
    /* pfnGetImageFlags */
    vmdkGetImageFlags,
    /* pfnGetOpenFlags */
    vmdkGetOpenFlags,
    /* pfnSetOpenFlags */
    vmdkSetOpenFlags,
    /* pfnGetComment */
    vmdkGetComment,
    /* pfnSetComment */
    vmdkSetComment,
    /* pfnGetUuid */
    vmdkGetUuid,
    /* pfnSetUuid */
    vmdkSetUuid,
    /* pfnGetModificationUuid */
    vmdkGetModificationUuid,
    /* pfnSetModificationUuid */
    vmdkSetModificationUuid,
    /* pfnGetParentUuid */
    vmdkGetParentUuid,
    /* pfnSetParentUuid */
    vmdkSetParentUuid,
    /* pfnGetParentModificationUuid */
    vmdkGetParentModificationUuid,
    /* pfnSetParentModificationUuid */
    vmdkSetParentModificationUuid,
    /* pfnDump */
    vmdkDump,
    /* pfnGetTimestamp */
    NULL,
    /* pfnGetParentTimestamp */
    NULL,
    /* pfnSetParentTimestamp */
    NULL,
    /* pfnGetParentFilename */
    NULL,
    /* pfnSetParentFilename */
    NULL,
    /* pfnComposeLocation */
    genericFileComposeLocation,
    /* pfnComposeName */
    genericFileComposeName,
    /* pfnCompact */
    NULL,
    /* pfnResize */
    vmdkResize,
    /* pfnRepair */
    NULL,
    /* pfnTraverseMetadata */
    NULL,
    /* u32VersionEnd */
    VD_IMGBACKEND_VERSION
};
