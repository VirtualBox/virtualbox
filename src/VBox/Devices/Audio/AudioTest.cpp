/* $Id: AudioTest.cpp 110494 2025-07-31 10:11:09Z andreas.loeffler@oracle.com $ */
/** @file
 * Audio testing routines.
 *
 * Common code which is being used by the ValidationKit and the
 * debug / ValdikationKit audio driver(s).
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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
#include <package-generated.h>
#include "product-generated.h"

#include <iprt/buildconfig.h>
#include <iprt/cdefs.h>
#include <iprt/dir.h>
#include <iprt/env.h>
#include <iprt/file.h>
#include <iprt/formats/riff.h>
#include <iprt/inifile.h>
#include <iprt/list.h>
#include <iprt/message.h> /** @todo Get rid of this once we have own log hooks. */
#include <iprt/rand.h>
#include <iprt/stream.h>
#include <iprt/system.h>
#include <iprt/uuid.h>
#include <iprt/vfs.h>
#include <iprt/zip.h>

#define _USE_MATH_DEFINES
#include <math.h> /* sin, M_PI */

#define LOG_GROUP LOG_GROUP_AUDIO_TEST
#include <VBox/log.h>

#include <VBox/version.h>
#include <VBox/vmm/pdmaudioifs.h>
#include <VBox/vmm/pdmaudioinline.h>

#include "AudioTest.h"


/*********************************************************************************************************************************
*   Defines                                                                                                                      *
*********************************************************************************************************************************/
/** The test manifest file name. */
#define AUDIOTEST_MANIFEST_FILE_STR "vkat_manifest.ini"
/** The current test manifest version. */
#define AUDIOTEST_MANIFEST_VER      1
/** Audio test archive default suffix.
 *  According to IPRT terminology this always contains the dot. */
#define AUDIOTEST_ARCHIVE_SUFF_STR  ".tar.gz"

/** Test manifest header name. */
#define AUDIOTEST_SEC_HDR_STR        "header"
/** Maximum section name length (in UTF-8 characters). */
#define AUDIOTEST_MAX_SEC_LEN        128
/** Maximum object name length (in UTF-8 characters). */
#define AUDIOTEST_MAX_OBJ_LEN        128


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Enumeration for an audio test object type.
 */
typedef enum AUDIOTESTOBJTYPE
{
    /** Unknown / invalid, do not use. */
    AUDIOTESTOBJTYPE_UNKNOWN = 0,
    /** The test object is a file. */
    AUDIOTESTOBJTYPE_FILE,
    /** The usual 32-bit hack. */
    AUDIOTESTOBJTYPE_32BIT_HACK = 0x7fffffff
} AUDIOTESTOBJTYPE;

/**
 * Structure for keeping an audio test object file.
 */
typedef struct AUDIOTESTOBJFILE
{
    /** File handle. */
    RTFILE              hFile;
    /** Total size (in bytes). */
    size_t              cbSize;
} AUDIOTESTOBJFILE;
/** Pointer to an audio test object file. */
typedef AUDIOTESTOBJFILE *PAUDIOTESTOBJFILE;

/**
 * Enumeration for an audio test object meta data type.
 */
typedef enum AUDIOTESTOBJMETADATATYPE
{
    /** Unknown / invalid, do not use. */
    AUDIOTESTOBJMETADATATYPE_INVALID = 0,
    /** Meta data is an UTF-8 string. */
    AUDIOTESTOBJMETADATATYPE_STRING,
    /** The usual 32-bit hack. */
    AUDIOTESTOBJMETADATATYPE_32BIT_HACK = 0x7fffffff
} AUDIOTESTOBJMETADATATYPE;

/**
 * Structure for keeping a meta data block.
 */
typedef struct AUDIOTESTOBJMETA
{
    /** List node. */
    RTLISTNODE                Node;
    /** Meta data type. */
    AUDIOTESTOBJMETADATATYPE  enmType;
    /** Meta data block. */
    void                     *pvMeta;
    /** Size (in bytes) of \a pvMeta. */
    size_t                    cbMeta;
} AUDIOTESTOBJMETA;
/** Pointer to an audio test object file. */
typedef AUDIOTESTOBJMETA *PAUDIOTESTOBJMETA;

/**
 * Structure for keeping a single audio test object.
 *
 * A test object is data which is needed in order to perform and verify one or
 * more audio test case(s).
 */
typedef struct AUDIOTESTOBJINT
{
    /** List node. */
    RTLISTNODE           Node;
    /** Pointer to test set this handle is bound to. */
    PAUDIOTESTSET        pSet;
    /** As we only support .INI-style files for now, this only has the object's section name in it. */
    /** @todo Make this more generic (union, ++). */
    char                 szSec[AUDIOTEST_MAX_SEC_LEN];
    /** The UUID of the object.
     *  Used to identify an object within a test set. */
    RTUUID               Uuid;
    /** Number of references to this test object. */
    uint32_t             cRefs;
    /** Name of the test object.
     *  Must not contain a path and has to be able to serialize to disk. */
    char                 szName[256];
    /** The test type. */
    AUDIOTESTTYPE        enmTestType;
    /** The object type. */
    AUDIOTESTOBJTYPE     enmType;
    /** Meta data list. */
    RTLISTANCHOR         lstMeta;
    /** Union for holding the object type-specific data. */
    union
    {
        AUDIOTESTOBJFILE File;
    };
} AUDIOTESTOBJINT;
/** Pointer to an audio test object. */
typedef AUDIOTESTOBJINT *PAUDIOTESTOBJINT;

/**
 * Structure for keeping an audio test verification job.
 */
typedef struct AUDIOTESTVERIFYJOB
{
    /** Pointer to set A. */
    PAUDIOTESTSET       pSetA;
    /** Pointer to set B. */
    PAUDIOTESTSET       pSetB;
    /** Pointer to the error description to use. */
    PAUDIOTESTERRORDESC pErr;
    /** Zero-based index of current test being verified. */
    uint32_t            idxTest;
    /** The verification options to use. */
    AUDIOTESTVERIFYOPTS Opts;
    /** PCM properties to use for verification. */
    PDMAUDIOPCMPROPS    PCMProps;
} AUDIOTESTVERIFYJOB;
/** Pointer to an audio test verification job. */
typedef AUDIOTESTVERIFYJOB *PAUDIOTESTVERIFYJOB;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Well-known frequency selection test tones. */
static const double s_aAudioTestToneFreqsHz[] =
{
     349.2282 /*F4*/,
     440.0000 /*A4*/,
     523.2511 /*C5*/,
     698.4565 /*F5*/,
     880.0000 /*A5*/,
    1046.502  /*C6*/,
    1174.659  /*D6*/,
    1396.913  /*F6*/,
    1760.0000 /*A6*/
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int  audioTestObjClose(PAUDIOTESTOBJINT pObj);
static void audioTestObjFinalize(PAUDIOTESTOBJINT pObj);
static void audioTestObjInit(PAUDIOTESTOBJINT pObj);
static bool audioTestObjIsOpen(PAUDIOTESTOBJINT pObj);


/**
 * Initializes a test tone with a specific frequency (in Hz).
 *
 * @returns Used tone frequency (in Hz).
 * @param   pTone               Pointer to test tone to initialize.
 * @param   pProps              PCM properties to use for the test tone.
 * @param   dbFreq              Frequency (in Hz) to initialize tone with.
 *                              When set to 0.0, a random frequency will be chosen.
 */
double AudioTestToneInit(PAUDIOTESTTONE pTone, PPDMAUDIOPCMPROPS pProps, double dbFreq)
{
    if (dbFreq == 0.0)
        dbFreq = AudioTestToneGetRandomFreq();

    pTone->rdFreqHz = dbFreq;
    pTone->rdFixed  = 2.0 * M_PI * pTone->rdFreqHz / PDMAudioPropsHz(pProps);
    pTone->uSample  = 0;

    memcpy(&pTone->Props, pProps, sizeof(PDMAUDIOPCMPROPS));

    pTone->enmType = AUDIOTESTTONETYPE_SINE; /* Only type implemented so far. */

    return dbFreq;
}

/**
 * Initializes a test tone by picking a random but well-known frequency (in Hz).
 *
 * @returns Randomly picked tone frequency (in Hz).
 * @param   pTone               Pointer to test tone to initialize.
 * @param   pProps              PCM properties to use for the test tone.
 */
double AudioTestToneInitRandom(PAUDIOTESTTONE pTone, PPDMAUDIOPCMPROPS pProps)
{
    return AudioTestToneInit(pTone, pProps,
                             /* Pick a frequency from our selection, so that every time a recording starts
                              * we'll hopfully generate a different note. */
                             0.0);
}

/**
 * Writes (and iterates) a given test tone to an output buffer.
 *
 * @returns VBox status code.
 * @param   pTone               Pointer to test tone to write.
 * @param   pvBuf               Pointer to output buffer to write test tone to.
 * @param   cbBuf               Size (in bytes) of output buffer.
 * @param   pcbWritten          How many bytes were written on success.
 */
int AudioTestToneGenerate(PAUDIOTESTTONE pTone, void *pvBuf, uint32_t cbBuf, uint32_t *pcbWritten)
{
    /*
     * Clear the buffer first so we don't need to think about additional channels.
     */
    uint32_t cFrames   = PDMAudioPropsBytesToFrames(&pTone->Props, cbBuf);

    /* Input cbBuf not necessarily is aligned to the frames, so re-calculate it. */
    const uint32_t cbToWrite = PDMAudioPropsFramesToBytes(&pTone->Props, cFrames);

    PDMAudioPropsClearBuffer(&pTone->Props, pvBuf, cbBuf, cFrames);

    /*
     * Generate the select sin wave in the first channel:
     */
    uint32_t const cbFrame   = PDMAudioPropsFrameSize(&pTone->Props);
    double const   rdFixed   = pTone->rdFixed;
    uint64_t       iSrcFrame = pTone->uSample;
    switch (PDMAudioPropsSampleSize(&pTone->Props))
    {
        case 1:
            /* untested */
            if (PDMAudioPropsIsSigned(&pTone->Props))
            {
                int8_t *piSample = (int8_t *)pvBuf;
                while (cFrames-- > 0)
                {
                    *piSample = (int8_t)(126 /*Amplitude*/ * sin(rdFixed * (double)iSrcFrame));
                    iSrcFrame++;
                    piSample += cbFrame;
                }
            }
            else
            {
                /* untested */
                uint8_t *pbSample = (uint8_t *)pvBuf;
                while (cFrames-- > 0)
                {
                    *pbSample = (uint8_t)(126 /*Amplitude*/ * sin(rdFixed * (double)iSrcFrame) + 0x80);
                    iSrcFrame++;
                    pbSample += cbFrame;
                }
            }
            break;

        case 2:
            if (PDMAudioPropsIsSigned(&pTone->Props))
            {
                int16_t *piSample = (int16_t *)pvBuf;
                while (cFrames-- > 0)
                {
                    *piSample = (int16_t)(32760 /*Amplitude*/ * sin(rdFixed * (double)iSrcFrame));
                    iSrcFrame++;
                    piSample = (int16_t *)((uint8_t *)piSample + cbFrame);
                }
            }
            else
            {
                /* untested */
                uint16_t *puSample = (uint16_t *)pvBuf;
                while (cFrames-- > 0)
                {
                    *puSample = (uint16_t)(32760 /*Amplitude*/ * sin(rdFixed * (double)iSrcFrame) + 0x8000);
                    iSrcFrame++;
                    puSample = (uint16_t *)((uint8_t *)puSample + cbFrame);
                }
            }
            break;

        case 4:
            /* untested */
            if (PDMAudioPropsIsSigned(&pTone->Props))
            {
                int32_t *piSample = (int32_t *)pvBuf;
                while (cFrames-- > 0)
                {
                    *piSample = (int32_t)((32760 << 16) /*Amplitude*/ * sin(rdFixed * (double)iSrcFrame));
                    iSrcFrame++;
                    piSample = (int32_t *)((uint8_t *)piSample + cbFrame);
                }
            }
            else
            {
                uint32_t *puSample = (uint32_t *)pvBuf;
                while (cFrames-- > 0)
                {
                    *puSample = (uint32_t)((32760 << 16) /*Amplitude*/ * sin(rdFixed * (double)iSrcFrame) + UINT32_C(0x80000000));
                    iSrcFrame++;
                    puSample = (uint32_t *)((uint8_t *)puSample + cbFrame);
                }
            }
            break;

        default:
            AssertFailedReturn(VERR_NOT_SUPPORTED);
    }

    pTone->uSample = iSrcFrame;

    if (pcbWritten)
        *pcbWritten = cbToWrite;

    return VINF_SUCCESS;
}

/**
 * Returns a random test tone frequency.
 */
double AudioTestToneGetRandomFreq(void)
{
    return s_aAudioTestToneFreqsHz[RTRandU32Ex(0, RT_ELEMENTS(s_aAudioTestToneFreqsHz) - 1)];
}

/**
 * Finds the next audible *or* silent audio sample and returns its offset.
 *
 * @returns Offset (in bytes) of the next found sample, or \a cbMax if not found / invalid parameters.
 * @param   hFile               File handle of file to search in.
 * @param   fFindSilence        Whether to search for a silent sample or not (i.e. audible).
 *                              What a silent sample is depends on \a pToneParms PCM parameters.
 * @param   uOff                Absolute offset (in bytes) to start searching from.
 * @param   cbMax               Maximum amount of bytes to process.
 * @param   pToneParms          Tone parameters to use.
 * @param   cbWindow            Search window size (in bytes).
 */
static uint64_t audioTestToneFileFind(RTFILE hFile, bool fFindSilence, uint64_t uOff, uint64_t cbMax,
                                      PAUDIOTESTTONEPARMS pToneParms, size_t cbWindow)
{
    int rc = RTFileSeek(hFile, uOff, RTFILE_SEEK_BEGIN, NULL);
    AssertRCReturn(rc, UINT64_MAX);

    uint64_t offFound = 0;
    uint8_t  abBuf[_64K];

    size_t const cbFrame  = PDMAudioPropsFrameSize(&pToneParms->Props);
    AssertReturn(cbFrame, UINT64_MAX);

    AssertReturn(PDMAudioPropsIsSizeAligned(&pToneParms->Props, (uint32_t)cbWindow), UINT64_MAX);

    size_t cbRead;
    for (;;)
    {
        rc = RTFileRead(hFile, &abBuf, RT_MIN(cbWindow, sizeof(abBuf)), &cbRead);
        if (   RT_FAILURE(rc)
            || !cbRead)
            break;

        AssertReturn(PDMAudioPropsIsSizeAligned(&pToneParms->Props, (uint32_t)cbRead), UINT64_MAX);
        AssertReturn(cbRead % cbFrame == 0, UINT64_MAX);

        /** @todo Do we need to have a sliding window here? */

        for (size_t i = 0; i < cbRead; i += cbWindow) /** @todo Slow as heck, but works for now. */
        {
            bool const fIsSilence = PDMAudioPropsIsBufferSilence(&pToneParms->Props, (const uint8_t *)abBuf + i, cbWindow);
            if (fIsSilence != fFindSilence)
            {
                AssertReturn(PDMAudioPropsIsSizeAligned(&pToneParms->Props, offFound), 0);
                return offFound;
            }
            offFound += cbWindow;
        }
    }

    return cbMax;
}

/**
 * Generates a tag.
 *
 * @returns VBox status code.
 * @param   pszTag              The output buffer.
 * @param   cbTag               The size of the output buffer.
 *                              AUDIOTEST_TAG_MAX is a good size.
 */
int AudioTestGenTag(char *pszTag, size_t cbTag)
{
    RTUUID UUID;
    int rc = RTUuidCreate(&UUID);
    AssertRCReturn(rc, rc);
    rc = RTUuidToStr(&UUID, pszTag, cbTag);
    AssertRCReturn(rc, rc);
    return rc;
}

/**
 * Return the tag to use in the given buffer, generating one if needed.
 *
 * @returns VBox status code.
 * @param   pszTag              The output buffer.
 * @param   cbTag               The size of the output buffer.
 *                              AUDIOTEST_TAG_MAX is a good size.
 * @param   pszTagUser          User specified tag, optional.
 */
static int audioTestCopyOrGenTag(char *pszTag, size_t cbTag, const char *pszTagUser)
{
    if (pszTagUser && *pszTagUser)
        return RTStrCopy(pszTag, cbTag, pszTagUser);
    return AudioTestGenTag(pszTag, cbTag);
}


/**
 * Creates a new path (directory) for a specific audio test set tag.
 *
 * @returns VBox status code.
 * @param   pszPath             On input, specifies the absolute base path where to create the test set path.
 *                              On output this specifies the absolute path created.
 * @param   cbPath              Size (in bytes) of \a pszPath.
 * @param   pszTag              Tag to use for path creation.
 *
 * @note    Can be used multiple times with the same tag; a sub directory with an ISO time string will be used
 *          on each call.
 */
int AudioTestPathCreate(char *pszPath, size_t cbPath, const char *pszTag)
{
    char szTag[AUDIOTEST_TAG_MAX];
    int rc = audioTestCopyOrGenTag(szTag, sizeof(szTag), pszTag);
    AssertRCReturn(rc, rc);

    char szName[RT_ELEMENTS(AUDIOTEST_PATH_PREFIX_STR) + AUDIOTEST_TAG_MAX + 4];
    if (RTStrPrintf2(szName, sizeof(szName), "%s-%s", AUDIOTEST_PATH_PREFIX_STR, szTag) < 0)
        AssertFailedReturn(VERR_BUFFER_OVERFLOW);

    rc = RTPathAppend(pszPath, cbPath, szName);
    AssertRCReturn(rc, rc);

#ifndef DEBUG /* Makes debugging easier to have a deterministic directory. */
    char szTime[64];
    RTTIMESPEC time;
    if (!RTTimeSpecToString(RTTimeNow(&time), szTime, sizeof(szTime)))
        return VERR_BUFFER_UNDERFLOW;

    /* Colons aren't allowed in windows filenames, so change to dashes. */
    char *pszColon;
    while ((pszColon = strchr(szTime, ':')) != NULL)
        *pszColon = '-';

    rc = RTPathAppend(pszPath, cbPath, szTime);
    AssertRCReturn(rc, rc);
#endif

    return RTDirCreateFullPath(pszPath, RTFS_UNIX_IRWXU);
}

DECLINLINE(int) audioTestManifestWriteData(PAUDIOTESTSET pSet, const void *pvData, size_t cbData)
{
    /** @todo Use RTIniFileWrite once its implemented. */
    return RTFileWrite(pSet->f.hFile, pvData, cbData, NULL);
}

/**
 * Writes string data to a test set manifest.
 *
 * @returns VBox status code.
 * @param   pSet                Test set to write manifest for.
 * @param   pszFormat           Format string to write.
 * @param   args                Variable arguments for \a pszFormat.
 */
static int audioTestManifestWriteV(PAUDIOTESTSET pSet, const char *pszFormat, va_list args)
{
    /** @todo r=bird: Use RTStrmOpen + RTStrmPrintf instead of this slow
     *        do-it-all-yourself stuff. */
    char *psz = NULL;
    if (RTStrAPrintfV(&psz, pszFormat, args) == -1)
        return VERR_NO_MEMORY;
    AssertPtrReturn(psz, VERR_NO_MEMORY);

    int rc = audioTestManifestWriteData(pSet, psz, strlen(psz));
    AssertRC(rc);

    RTStrFree(psz);

    return rc;
}

/**
 * Writes a string to a test set manifest.
 * Convenience function.
 *
 * @returns VBox status code.
 * @param   pSet                Test set to write manifest for.
 * @param   pszFormat           Format string to write.
 * @param   ...                 Variable arguments for \a pszFormat. Optional.
 */
static int audioTestManifestWrite(PAUDIOTESTSET pSet, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);

    int rc = audioTestManifestWriteV(pSet, pszFormat, va);
    AssertRC(rc);

    va_end(va);

    return rc;
}

/**
 * Returns the current read/write offset (in bytes) of the opened manifest file.
 *
 * @returns Current read/write offset (in bytes).
 * @param   pSet                Set to return offset for.
 *                              Must have an opened manifest file.
 */
DECLINLINE(uint64_t) audioTestManifestGetOffsetAbs(PAUDIOTESTSET pSet)
{
    AssertReturn(RTFileIsValid(pSet->f.hFile), 0);
    return RTFileTell(pSet->f.hFile);
}

/**
 * Writes a section header to a test set manifest.
 *
 * @returns VBox status code.
 * @param   pSet                Test set to write manifest for.
 * @param   pszSection          Format string of section to write.
 * @param   ...                 Variable arguments for \a pszSection. Optional.
 */
static int audioTestManifestWriteSectionHdr(PAUDIOTESTSET pSet, const char *pszSection, ...)
{
    va_list va;
    va_start(va, pszSection);

    /** @todo Keep it as simple as possible for now. Improve this later. */
    int rc = audioTestManifestWrite(pSet, "[%N]\n", pszSection, &va);

    va_end(va);

    return rc;
}

/**
 * Initializes an audio test set, internal function.
 *
 * @param   pSet                Test set to initialize.
 */
static void audioTestSetInitInternal(PAUDIOTESTSET pSet)
{
    pSet->f.hFile = NIL_RTFILE;

    RTListInit(&pSet->lstObj);
    pSet->cObj = 0;

    RTListInit(&pSet->lstTest);
    pSet->cTests         = 0;
    pSet->cTestsRunning  = 0;
    pSet->offTestCount   = 0;
    pSet->pTestCur       = NULL;
    pSet->cObj           = 0;
    pSet->offObjCount    = 0;
    pSet->cTotalFailures = 0;
}

/**
 * Returns whether a test set's manifest file is open (and thus ready) or not.
 *
 * @returns \c true if open (and ready), or \c false if not.
 * @retval  VERR_
 * @param   pSet                Test set to return open status for.
 */
static bool audioTestManifestIsOpen(PAUDIOTESTSET pSet)
{
    if (   pSet->enmMode == AUDIOTESTSETMODE_TEST
        && pSet->f.hFile != NIL_RTFILE)
        return true;
    else if (   pSet->enmMode    == AUDIOTESTSETMODE_VERIFY
             && pSet->f.hIniFile != NIL_RTINIFILE)
        return true;

    return false;
}

/**
 * Initializes an audio test error description.
 *
 * @param   pErr                Test error description to initialize.
 */
static void audioTestErrorDescInit(PAUDIOTESTERRORDESC pErr)
{
    RTListInit(&pErr->List);
    pErr->cErrors = 0;
}

/**
 * Destroys an audio test error description.
 *
 * @param   pErr                Test error description to destroy.
 */
void AudioTestErrorDescDestroy(PAUDIOTESTERRORDESC pErr)
{
    if (!pErr)
        return;

    PAUDIOTESTERRORENTRY pErrEntry, pErrEntryNext;
    RTListForEachSafe(&pErr->List, pErrEntry, pErrEntryNext, AUDIOTESTERRORENTRY, Node)
    {
        RTListNodeRemove(&pErrEntry->Node);

        RTMemFree(pErrEntry);
    }

    pErr->cErrors = 0;
}

/**
 * Returns the the number of errors of an audio test error description.
 *
 * @returns Error count.
 * @param   pErr                Test error description to return error count for.
 */
uint32_t AudioTestErrorDescCount(PCAUDIOTESTERRORDESC pErr)
{
    return pErr->cErrors;
}

/**
 * Returns if an audio test error description contains any errors or not.
 *
 * @returns \c true if it contains errors, or \c false if not.
 * @param   pErr                Test error description to return error status for.
 */
bool AudioTestErrorDescFailed(PCAUDIOTESTERRORDESC pErr)
{
    if (pErr->cErrors)
    {
        Assert(!RTListIsEmpty(&pErr->List));
        return true;
    }

    return false;
}

/**
 * Adds a single error entry to an audio test error description, va_list version.
 *
 * @returns VBox status code.
 * @param   pErr                Test error description to add entry for.
 * @param   idxTest             Index of failing test (zero-based).
 * @param   rc                  Result code of entry to add.
 * @param   pszFormat           Error description format string to add.
 * @param   va                  Optional format arguments of \a pszDesc to add.
 */
static int audioTestErrorDescAddV(PAUDIOTESTERRORDESC pErr, uint32_t idxTest, int rc, const char *pszFormat, va_list va)
{
    PAUDIOTESTERRORENTRY pEntry = (PAUDIOTESTERRORENTRY)RTMemAlloc(sizeof(AUDIOTESTERRORENTRY));
    AssertPtrReturn(pEntry, VERR_NO_MEMORY);

    char *pszDescTmp;
    if (RTStrAPrintfV(&pszDescTmp, pszFormat, va) < 0)
        AssertFailedReturn(VERR_NO_MEMORY);

    const ssize_t cch = RTStrPrintf2(pEntry->szDesc, sizeof(pEntry->szDesc), "Test #%RU32 %s: %s",
                                     idxTest, RT_FAILURE(rc) ? "failed" : "info", pszDescTmp);
    RTStrFree(pszDescTmp);
    AssertReturn(cch > 0, VERR_BUFFER_OVERFLOW);

    pEntry->rc = rc;

    RTListAppend(&pErr->List, &pEntry->Node);

    if (RT_FAILURE(rc))
        pErr->cErrors++;

    return VINF_SUCCESS;
}

/**
 * Adds a single error entry to an audio test error description.
 *
 * @returns VBox status code.
 * @param   pErr                Test error description to add entry for.
 * @param   idxTest             Index of failing test (zero-based).
 * @param   pszFormat           Error description format string to add.
 * @param   ...                 Optional format arguments of \a pszDesc to add.
 */
static int audioTestErrorDescAddError(PAUDIOTESTERRORDESC pErr, uint32_t idxTest, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);

    int rc = audioTestErrorDescAddV(pErr, idxTest, VERR_GENERAL_FAILURE /** @todo Fudge! */, pszFormat, va);

    va_end(va);
    return rc;
}

/**
 * Adds a single info entry to an audio test error description, va_list version.
 *
 * @returns VBox status code.
 * @param   pErr                Test error description to add entry for.
 * @param   idxTest             Index of failing test (zero-based).
 * @param   pszFormat           Error description format string to add.
 * @param   ...                 Optional format arguments of \a pszDesc to add.
 */
static int audioTestErrorDescAddInfo(PAUDIOTESTERRORDESC pErr, uint32_t idxTest, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);

    int rc = audioTestErrorDescAddV(pErr, idxTest, VINF_SUCCESS, pszFormat, va);

    va_end(va);
    return rc;
}

#if 0
static int audioTestErrorDescAddRc(PAUDIOTESTERRORDESC pErr, int rc, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);

    int rc2 = audioTestErrorDescAddV(pErr, rc, pszFormat, va);

    va_end(va);
    return rc2;
}
#endif

/**
 * Retrieves the temporary directory.
 *
 * @returns VBox status code.
 * @param   pszPath             Where to return the absolute path of the created directory on success.
 * @param   cbPath              Size (in bytes) of \a pszPath.
 */
int AudioTestPathGetTemp(char *pszPath, size_t cbPath)
{
    int rc = RTEnvGetEx(RTENV_DEFAULT, "TESTBOX_PATH_SCRATCH", pszPath, cbPath, NULL);
    if (RT_FAILURE(rc))
    {
        rc = RTPathTemp(pszPath, cbPath);
        AssertRCReturn(rc, rc);
    }

    return rc;
}

/**
 * Creates a new temporary directory with a specific (test) tag.
 *
 * @returns VBox status code.
 * @param   pszPath             Where to return the absolute path of the created directory on success.
 * @param   cbPath              Size (in bytes) of \a pszPath.
 * @param   pszTag              Tag name to use for directory creation.
 *
 * @note    Can be used multiple times with the same tag; a sub directory with an ISO time string will be used
 *          on each call.
 */
int AudioTestPathCreateTemp(char *pszPath, size_t cbPath, const char *pszTag)
{
    AssertReturn(pszTag && strlen(pszTag) <= AUDIOTEST_TAG_MAX, VERR_INVALID_PARAMETER);

    char szTemp[RTPATH_MAX];
    int rc = AudioTestPathGetTemp(szTemp, sizeof(szTemp));
    AssertRCReturn(rc, rc);

    rc = AudioTestPathCreate(szTemp, sizeof(szTemp), pszTag);
    AssertRCReturn(rc, rc);

    return RTStrCopy(pszPath, cbPath, szTemp);
}

/**
 * Gets a value as string.
 *
 * @returns VBox status code.
 * @param   pObj               Object handle to get value for.
 * @param   pszKey              Key to get value from.
 * @param   pszVal              Where to return the value on success.
 * @param   cbVal               Size (in bytes) of \a pszVal.
 */
static int audioTestObjGetStr(PAUDIOTESTOBJINT pObj, const char *pszKey, char *pszVal, size_t cbVal)
{
    /** @todo For now we only support .INI-style files. */
    AssertPtrReturn(pObj->pSet, VERR_WRONG_ORDER);
    return RTIniFileQueryValue(pObj->pSet->f.hIniFile, pObj->szSec, pszKey, pszVal, cbVal, NULL);
}

/**
 * Gets a value as boolean.
 *
 * @returns VBox status code.
 * @param   pObj               Object handle to get value for.
 * @param   pszKey              Key to get value from.
 * @param   pbVal               Where to return the value on success.
 */
static int audioTestObjGetBool(PAUDIOTESTOBJINT pObj, const char *pszKey, bool *pbVal)
{
    char szVal[_1K];
    int rc = audioTestObjGetStr(pObj, pszKey, szVal, sizeof(szVal));
    if (RT_SUCCESS(rc))
        *pbVal =    (RTStrICmp(szVal, "true") == 0)
                 || (RTStrICmp(szVal, "1")    == 0) ? true : false;

    return rc;
}

/**
 * Gets a value as uint8_t.
 *
 * @returns VBox status code.
 * @param   pObj               Object handle to get value for.
 * @param   pszKey              Key to get value from.
 * @param   puVal               Where to return the value on success.
 */
static int audioTestObjGetUInt8(PAUDIOTESTOBJINT pObj, const char *pszKey, uint8_t *puVal)
{
    char szVal[_1K];
    int rc = audioTestObjGetStr(pObj, pszKey, szVal, sizeof(szVal));
    if (RT_SUCCESS(rc))
        *puVal = RTStrToUInt8(szVal);

    return rc;
}

/**
 * Gets a value as uint32_t.
 *
 * @returns VBox status code.
 * @param   pObj               Object handle to get value for.
 * @param   pszKey              Key to get value from.
 * @param   puVal               Where to return the value on success.
 */
static int audioTestObjGetUInt32(PAUDIOTESTOBJINT pObj, const char *pszKey, uint32_t *puVal)
{
    char szVal[_1K];
    int rc = audioTestObjGetStr(pObj, pszKey, szVal, sizeof(szVal));
    if (RT_SUCCESS(rc))
        *puVal = RTStrToUInt32(szVal);

    return rc;
}

/**
 * Returns the absolute path of a given audio test set object.
 *
 * @returns VBox status code.
 * @param   pSet                Test set the object contains.
 * @param   pszPathAbs          Where to return the absolute path on success.
 * @param   cbPathAbs           Size (in bytes) of \a pszPathAbs.
 * @param   pszObjName          Name of the object to create absolute path for.
 */
DECLINLINE(int) audioTestSetGetObjPath(PAUDIOTESTSET pSet, char *pszPathAbs, size_t cbPathAbs, const char *pszObjName)
{
    return RTPathJoin(pszPathAbs, cbPathAbs, pSet->szPathAbs, pszObjName);
}

/**
 * Returns the tag of a test set.
 *
 * @returns Test set tag.
 * @param   pSet                Test set to return tag for.
 */
const char *AudioTestSetGetTag(PAUDIOTESTSET pSet)
{
    return pSet->szTag;
}

/**
 * Returns the total number of registered tests.
 *
 * @returns Total number of registered tests.
 * @param   pSet                Test set to return value for.
 */
uint32_t AudioTestSetGetTestsTotal(PAUDIOTESTSET pSet)
{
    return pSet->cTests;
}

/**
 * Returns the total number of (still) running tests.
 *
 * @returns Total number of (still) running tests.
 * @param   pSet                Test set to return value for.
 */
uint32_t AudioTestSetGetTestsRunning(PAUDIOTESTSET pSet)
{
    return pSet->cTestsRunning;
}

/**
 * Returns the total number of test failures occurred.
 *
 * @returns Total number of test failures occurred.
 * @param   pSet                Test set to return value for.
 */
uint32_t AudioTestSetGetTotalFailures(PAUDIOTESTSET pSet)
{
    return pSet->cTotalFailures;
}

/**
 * Creates a new audio test set.
 *
 * @returns VBox status code.
 * @param   pSet                Test set to create.
 * @param   pszPath             Where to store the set set data.  If NULL, the
 *                              temporary directory will be used.
 * @param   pszTag              Tag name to use for this test set.
 */
int AudioTestSetCreate(PAUDIOTESTSET pSet, const char *pszPath, const char *pszTag)
{
    audioTestSetInitInternal(pSet);

    int rc = audioTestCopyOrGenTag(pSet->szTag, sizeof(pSet->szTag), pszTag);
    AssertRCReturn(rc, rc);

    /*
     * Test set directory.
     */
    if (pszPath)
    {
        rc = RTPathAbs(pszPath, pSet->szPathAbs, sizeof(pSet->szPathAbs));
        AssertRCReturn(rc, rc);

        rc = AudioTestPathCreate(pSet->szPathAbs, sizeof(pSet->szPathAbs), pSet->szTag);
    }
    else
        rc = AudioTestPathCreateTemp(pSet->szPathAbs, sizeof(pSet->szPathAbs), pSet->szTag);
    AssertRCReturn(rc, rc);

    /*
     * Create the manifest file.
     */
    char szTmp[RTPATH_MAX];
    rc = RTPathJoin(szTmp, sizeof(szTmp), pSet->szPathAbs, AUDIOTEST_MANIFEST_FILE_STR);
    AssertRCReturn(rc, rc);

    rc = RTFileOpen(&pSet->f.hFile, szTmp, RTFILE_O_CREATE | RTFILE_O_WRITE | RTFILE_O_DENY_WRITE);
    AssertRCReturn(rc, rc);

    rc = audioTestManifestWriteSectionHdr(pSet, "header");
    AssertRCReturn(rc, rc);

    rc = audioTestManifestWrite(pSet, "magic=vkat_ini\n"); /* VKAT Manifest, .INI-style. */
    AssertRCReturn(rc, rc);
    rc = audioTestManifestWrite(pSet, "ver=%d\n", AUDIOTEST_MANIFEST_VER);
    AssertRCReturn(rc, rc);
    rc = audioTestManifestWrite(pSet, "tag=%s\n", pSet->szTag);
    AssertRCReturn(rc, rc);

    AssertCompile(sizeof(szTmp) > RTTIME_STR_LEN);
    RTTIMESPEC Now;
    rc = audioTestManifestWrite(pSet, "date_created=%s\n", RTTimeSpecToString(RTTimeNow(&Now), szTmp, sizeof(szTmp)));
    AssertRCReturn(rc, rc);

    RTSystemQueryOSInfo(RTSYSOSINFO_PRODUCT, szTmp, sizeof(szTmp)); /* do NOT return on failure. */
    rc = audioTestManifestWrite(pSet, "os_product=%s\n", szTmp);
    AssertRCReturn(rc, rc);

    RTSystemQueryOSInfo(RTSYSOSINFO_RELEASE, szTmp, sizeof(szTmp)); /* do NOT return on failure. */
    rc = audioTestManifestWrite(pSet, "os_rel=%s\n", szTmp);
    AssertRCReturn(rc, rc);

    RTSystemQueryOSInfo(RTSYSOSINFO_VERSION, szTmp, sizeof(szTmp)); /* do NOT return on failure. */
    rc = audioTestManifestWrite(pSet, "os_ver=%s\n", szTmp);
    AssertRCReturn(rc, rc);

    rc = audioTestManifestWrite(pSet, "vbox_ver=%s r%u %s (%s %s)\n",
                                VBOX_VERSION_STRING, RTBldCfgRevision(), RTBldCfgTargetDotArch(), __DATE__, __TIME__);
    AssertRCReturn(rc, rc);

    rc = audioTestManifestWrite(pSet, "test_count=");
    AssertRCReturn(rc, rc);
    pSet->offTestCount = audioTestManifestGetOffsetAbs(pSet);
    rc = audioTestManifestWrite(pSet, "0000\n"); /* A bit messy, but does the trick for now. */
    AssertRCReturn(rc, rc);

    rc = audioTestManifestWrite(pSet, "obj_count=");
    AssertRCReturn(rc, rc);
    pSet->offObjCount = audioTestManifestGetOffsetAbs(pSet);
    rc = audioTestManifestWrite(pSet, "0000\n"); /* A bit messy, but does the trick for now. */
    AssertRCReturn(rc, rc);

    pSet->enmMode = AUDIOTESTSETMODE_TEST;

    return rc;
}

/**
 * Destroys a test set.
 *
 * @returns VBox status code.
 * @param   pSet                Test set to destroy.
 */
int AudioTestSetDestroy(PAUDIOTESTSET pSet)
{
    if (!pSet)
        return VINF_SUCCESS;

    /* No more validation (no / still running tests) here -- just pack all stuff we got so far
     * and let the verification routine deal with it later. */

    int rc = AudioTestSetClose(pSet);
    if (RT_FAILURE(rc))
        return rc;

    PAUDIOTESTOBJINT pObj, pObjNext;
    RTListForEachSafe(&pSet->lstObj, pObj, pObjNext, AUDIOTESTOBJINT, Node)
    {
        rc = audioTestObjClose(pObj);
        if (RT_SUCCESS(rc))
        {
            PAUDIOTESTOBJMETA pMeta, pMetaNext;
            RTListForEachSafe(&pObj->lstMeta, pMeta, pMetaNext, AUDIOTESTOBJMETA, Node)
            {
                switch (pMeta->enmType)
                {
                    case AUDIOTESTOBJMETADATATYPE_STRING:
                    {
                        RTStrFree((char *)pMeta->pvMeta);
                        break;
                    }

                    default:
                        AssertFailed();
                        break;
                }

                RTListNodeRemove(&pMeta->Node);
                RTMemFree(pMeta);
            }

            RTListNodeRemove(&pObj->Node);
            RTMemFree(pObj);

            Assert(pSet->cObj);
            pSet->cObj--;
        }
        else
            break;
    }

    if (RT_FAILURE(rc))
        return rc;

    Assert(pSet->cObj == 0);

    PAUDIOTESTENTRY pEntry, pEntryNext;
    RTListForEachSafe(&pSet->lstTest, pEntry, pEntryNext, AUDIOTESTENTRY, Node)
    {
        RTListNodeRemove(&pEntry->Node);
        RTMemFree(pEntry);

        Assert(pSet->cTests);
        pSet->cTests--;
    }

    if (RT_FAILURE(rc))
        return rc;

    Assert(pSet->cTests == 0);

    return rc;
}

/**
 * Opens an existing audio test set.
 *
 * @returns VBox status code.
 * @param   pSet                Test set to open.
 * @param   pszPath             Absolute path of the test set to open.
 */
int AudioTestSetOpen(PAUDIOTESTSET pSet, const char *pszPath)
{
    audioTestSetInitInternal(pSet);

    char szManifest[RTPATH_MAX];
    int rc = RTPathJoin(szManifest, sizeof(szManifest), pszPath, AUDIOTEST_MANIFEST_FILE_STR);
    AssertRCReturn(rc, rc);

    RTVFSFILE hVfsFile;
    rc = RTVfsFileOpenNormal(szManifest, RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_DENY_WRITE, &hVfsFile);
    if (RT_FAILURE(rc))
        return rc;

    rc = RTIniFileCreateFromVfsFile(&pSet->f.hIniFile, hVfsFile, RTINIFILE_F_READONLY);
    RTVfsFileRelease(hVfsFile);
    AssertRCReturn(rc, rc);

    rc = RTStrCopy(pSet->szPathAbs, sizeof(pSet->szPathAbs), pszPath);
    AssertRCReturn(rc, rc);

    pSet->enmMode = AUDIOTESTSETMODE_VERIFY;

    return rc;
}

/**
 * Closes an opened audio test set.
 *
 * @returns VBox status code.
 * @param   pSet                Test set to close.
 */
int AudioTestSetClose(PAUDIOTESTSET pSet)
{
    AssertPtrReturn(pSet, VERR_INVALID_POINTER);

    if (!audioTestManifestIsOpen(pSet))
        return VINF_SUCCESS;

    int rc;

    if (pSet->enmMode == AUDIOTESTSETMODE_TEST)
    {
        /* Update number of bound test objects. */
        PAUDIOTESTENTRY pTest;
        uint32_t        cTests = 0;
        RTListForEach(&pSet->lstTest, pTest, AUDIOTESTENTRY, Node)
        {
            rc = RTFileSeek(pSet->f.hFile, pTest->offObjCount, RTFILE_SEEK_BEGIN, NULL);
            AssertRCReturn(rc, rc);
            rc = audioTestManifestWrite(pSet, "%04RU32", pTest->cObj);
            AssertRCReturn(rc, rc);
            cTests++; /* Sanity checking. */
        }

        AssertMsgReturn(pSet->cTests == cTests, ("Test count and list don't match"), VERR_INTERNAL_ERROR);

        /*
         * Update number of total objects.
         */
        rc = RTFileSeek(pSet->f.hFile, pSet->offObjCount, RTFILE_SEEK_BEGIN, NULL);
        AssertRCReturn(rc, rc);
        rc = audioTestManifestWrite(pSet, "%04RU32", pSet->cObj);
        AssertRCReturn(rc, rc);

        /*
         * Update number of total tests.
         */
        rc = RTFileSeek(pSet->f.hFile, pSet->offTestCount, RTFILE_SEEK_BEGIN, NULL);
        AssertRCReturn(rc, rc);
        rc = audioTestManifestWrite(pSet, "%04RU32", pSet->cTests);
        AssertRCReturn(rc, rc);

        /*
         * Serialize all registered test objects.
         */
        rc = RTFileSeek(pSet->f.hFile, 0, RTFILE_SEEK_END, NULL);
        AssertRCReturn(rc, rc);

        PAUDIOTESTOBJINT pObj;
        uint32_t         cObj = 0;
        RTListForEach(&pSet->lstObj, pObj, AUDIOTESTOBJINT, Node)
        {
            /* First, close the object.
             * This also does some needed finalization. */
            rc = AudioTestObjClose(pObj);
            AssertRCReturn(rc, rc);
            rc = audioTestManifestWrite(pSet, "\n");
            AssertRCReturn(rc, rc);
            char szUuid[AUDIOTEST_MAX_SEC_LEN];
            rc = RTUuidToStr(&pObj->Uuid, szUuid, sizeof(szUuid));
            AssertRCReturn(rc, rc);
            rc = audioTestManifestWriteSectionHdr(pSet, "obj_%s", szUuid);
            AssertRCReturn(rc, rc);
            rc = audioTestManifestWrite(pSet, "obj_type=%RU32\n", pObj->enmType);
            AssertRCReturn(rc, rc);
            rc = audioTestManifestWrite(pSet, "obj_name=%s\n", pObj->szName);
            AssertRCReturn(rc, rc);

            switch (pObj->enmType)
            {
                case AUDIOTESTOBJTYPE_FILE:
                {
                    rc = audioTestManifestWrite(pSet, "obj_size=%RU64\n", pObj->File.cbSize);
                    AssertRCReturn(rc, rc);
                    break;
                }

                default:
                    AssertFailed();
                    break;
            }

            /*
             * Write all meta data.
             */
            PAUDIOTESTOBJMETA pMeta;
            RTListForEach(&pObj->lstMeta, pMeta, AUDIOTESTOBJMETA, Node)
            {
                switch (pMeta->enmType)
                {
                    case AUDIOTESTOBJMETADATATYPE_STRING:
                    {
                        rc = audioTestManifestWrite(pSet, (const char *)pMeta->pvMeta);
                        AssertRCReturn(rc, rc);
                        break;
                    }

                    default:
                        AssertFailed();
                        break;
                }
            }

            cObj++; /* Sanity checking. */
        }

        AssertMsgReturn(pSet->cObj == cObj, ("Object count and list don't match"), VERR_INTERNAL_ERROR);

        int rc2 = RTFileClose(pSet->f.hFile);
        if (RT_SUCCESS(rc2))
            pSet->f.hFile = NIL_RTFILE;

        if (RT_SUCCESS(rc))
            rc = rc2;
    }
    else if (pSet->enmMode == AUDIOTESTSETMODE_VERIFY)
    {
        RTIniFileRelease(pSet->f.hIniFile);
        pSet->f.hIniFile = NIL_RTINIFILE;

        rc = VINF_SUCCESS;
    }
    else
        AssertFailedStmt(rc = VERR_NOT_SUPPORTED);

    return rc;
}

/**
 * Physically wipes all related test set files off the disk.
 *
 * @returns VBox status code.
 * @param   pSet                Test set to wipe.
 */
int AudioTestSetWipe(PAUDIOTESTSET pSet)
{
    AssertPtrReturn(pSet, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    char szFilePath[RTPATH_MAX];

    PAUDIOTESTOBJINT pObj;
    RTListForEach(&pSet->lstObj, pObj, AUDIOTESTOBJINT, Node)
    {
        int rc2 = audioTestObjClose(pObj);
        if (RT_SUCCESS(rc2))
        {
            rc2 = audioTestSetGetObjPath(pSet, szFilePath, sizeof(szFilePath), pObj->szName);
            if (RT_SUCCESS(rc2))
                rc2 = RTFileDelete(szFilePath);
        }

        if (RT_SUCCESS(rc))
            rc = rc2;
        /* Keep going. */
    }

    if (RT_SUCCESS(rc))
    {
        rc = RTPathJoin(szFilePath, sizeof(szFilePath), pSet->szPathAbs, AUDIOTEST_MANIFEST_FILE_STR);
        if (RT_SUCCESS(rc))
            rc = RTFileDelete(szFilePath);
    }

    /* Remove the (hopefully now empty) directory. Otherwise let this fail. */
    if (RT_SUCCESS(rc))
        rc = RTDirRemove(pSet->szPathAbs);

    return rc;
}

/**
 * Creates and registers a new audio test object to the current running test.
 *
 * @returns VBox status code.
 * @param   pSet                Test set to create and register new object for.
 * @param   pszName             Name of new object to create.
 * @param   pObj                Where to return the pointer to the newly created object on success.
 */
int AudioTestSetObjCreateAndRegister(PAUDIOTESTSET pSet, const char *pszName, PAUDIOTESTOBJ pObj)
{
    AssertReturn(pSet->cTestsRunning == 1, VERR_WRONG_ORDER); /* No test nesting allowed. */

    AssertPtrReturn(pszName, VERR_INVALID_POINTER);

    PAUDIOTESTOBJINT pThis = (PAUDIOTESTOBJINT)RTMemAlloc(sizeof(AUDIOTESTOBJINT));
    AssertPtrReturn(pThis, VERR_NO_MEMORY);

    audioTestObjInit(pThis);

    if (RTStrPrintf2(pThis->szName, sizeof(pThis->szName), "%04RU32-%s", pSet->cObj, pszName) <= 0)
        AssertFailedReturn(VERR_BUFFER_OVERFLOW);

    /** @todo Generalize this function more once we have more object types. */

    char szObjPathAbs[RTPATH_MAX];
    int rc = audioTestSetGetObjPath(pSet, szObjPathAbs, sizeof(szObjPathAbs), pThis->szName);
    if (RT_SUCCESS(rc))
    {
        rc = RTFileOpen(&pThis->File.hFile, szObjPathAbs, RTFILE_O_CREATE | RTFILE_O_WRITE | RTFILE_O_DENY_WRITE);
        if (RT_SUCCESS(rc))
        {
            pThis->enmType = AUDIOTESTOBJTYPE_FILE;
            pThis->cRefs   = 1; /* Currently only 1:1 mapping. */

            RTListAppend(&pSet->lstObj, &pThis->Node);
            pSet->cObj++;

            /* Generate + set an UUID for the object and assign it to the current test. */
            rc = RTUuidCreate(&pThis->Uuid);
            AssertRCReturn(rc, rc);
            char szUuid[AUDIOTEST_MAX_OBJ_LEN];
            rc = RTUuidToStr(&pThis->Uuid, szUuid, sizeof(szUuid));
            AssertRCReturn(rc, rc);

            rc = audioTestManifestWrite(pSet, "obj%RU32_uuid=%s\n", pSet->pTestCur->cObj, szUuid);
            AssertRCReturn(rc, rc);

            AssertPtr(pSet->pTestCur);
            pSet->pTestCur->cObj++;

            *pObj = pThis;
        }
    }

    if (RT_FAILURE(rc))
        RTMemFree(pThis);

    return rc;
}

/**
 * Writes to a created audio test object.
 *
 * @returns VBox status code.
 * @param   hObj                Handle to the audio test object to write to.
 * @param   pvBuf               Pointer to data to write.
 * @param   cbBuf               Size (in bytes) of \a pvBuf to write.
 */
int AudioTestObjWrite(AUDIOTESTOBJ hObj, const void *pvBuf, size_t cbBuf)
{
    AUDIOTESTOBJINT *pThis = hObj;

    /** @todo Generalize this function more once we have more object types. */
    AssertReturn(pThis->enmType == AUDIOTESTOBJTYPE_FILE, VERR_INVALID_PARAMETER);

    return RTFileWrite(pThis->File.hFile, pvBuf, cbBuf, NULL);
}

/**
 * Adds meta data to a test object as a string, va_list version.
 *
 * @returns VBox status code.
 * @param   pObj                Test object to add meta data for.
 * @param   pszFormat           Format string to add.
 * @param   va                  Variable arguments list to use for the format string.
 */
static int audioTestObjAddMetadataStrV(PAUDIOTESTOBJINT pObj, const char *pszFormat, va_list va)
{
    PAUDIOTESTOBJMETA pMeta = (PAUDIOTESTOBJMETA)RTMemAlloc(sizeof(AUDIOTESTOBJMETA));
    AssertPtrReturn(pMeta, VERR_NO_MEMORY);

    pMeta->pvMeta = RTStrAPrintf2V(pszFormat, va);
    AssertPtrReturn(pMeta->pvMeta, VERR_BUFFER_OVERFLOW);
    pMeta->cbMeta = RTStrNLen((const char *)pMeta->pvMeta, RTSTR_MAX);

    pMeta->enmType = AUDIOTESTOBJMETADATATYPE_STRING;

    RTListAppend(&pObj->lstMeta, &pMeta->Node);

    return VINF_SUCCESS;
}

/**
 * Adds meta data to a test object as a string.
 *
 * @returns VBox status code.
 * @param   hObj                Handle to the test object to add meta data for.
 * @param   pszFormat           Format string to add.
 * @param   ...                 Variable arguments for the format string.
 */
int AudioTestObjAddMetadataStr(AUDIOTESTOBJ hObj, const char *pszFormat, ...)
{
    AUDIOTESTOBJINT *pThis = hObj;

    va_list va;

    va_start(va, pszFormat);
    int rc = audioTestObjAddMetadataStrV(pThis, pszFormat, va);
    va_end(va);

    return rc;
}

/**
 * Closes an opened audio test object.
 *
 * @returns VBox status code.
 * @param   hObj                Handle to the audio test object to close.
 */
int AudioTestObjClose(AUDIOTESTOBJ hObj)
{
    AUDIOTESTOBJINT *pThis = hObj;

    if (!pThis)
        return VINF_SUCCESS;

    audioTestObjFinalize(pThis);

    return audioTestObjClose(pThis);
}

/**
 * Begins a new test of a test set.
 *
 * @returns VBox status code.
 * @param   pSet                Test set to begin new test for.
 * @param   pszDesc             Test description.
 * @param   pParms              Test parameters to use.
 * @param   ppEntry             Where to return the new test
 */
int AudioTestSetTestBegin(PAUDIOTESTSET pSet, const char *pszDesc, PAUDIOTESTPARMS pParms, PAUDIOTESTENTRY *ppEntry)
{
    AssertReturn(pSet->cTestsRunning == 0, VERR_WRONG_ORDER); /* No test nesting allowed. */

    PAUDIOTESTENTRY pEntry = (PAUDIOTESTENTRY)RTMemAllocZ(sizeof(AUDIOTESTENTRY));
    AssertPtrReturn(pEntry, VERR_NO_MEMORY);

    int rc = RTStrCopy(pEntry->szDesc, sizeof(pEntry->szDesc), pszDesc);
    AssertRCReturn(rc, rc);

    memcpy(&pEntry->Parms, pParms, sizeof(AUDIOTESTPARMS));

    pEntry->pParent = pSet;
    pEntry->rc      = VERR_IPE_UNINITIALIZED_STATUS;

    rc = audioTestManifestWrite(pSet, "\n");
    AssertRCReturn(rc, rc);

    rc = audioTestManifestWriteSectionHdr(pSet, "test_%04RU32", pSet->cTests);
    AssertRCReturn(rc, rc);
    rc = audioTestManifestWrite(pSet, "test_desc=%s\n", pszDesc);
    AssertRCReturn(rc, rc);
    rc = audioTestManifestWrite(pSet, "test_type=%RU32\n", pParms->enmType);
    AssertRCReturn(rc, rc);
    rc = audioTestManifestWrite(pSet, "test_delay_ms=%RU32\n", pParms->msDelay);
    AssertRCReturn(rc, rc);
    rc = audioTestManifestWrite(pSet, "audio_direction=%s\n", PDMAudioDirGetName(pParms->enmDir));
    AssertRCReturn(rc, rc);

    rc = audioTestManifestWrite(pSet, "obj_count=");
    AssertRCReturn(rc, rc);
    pEntry->offObjCount = audioTestManifestGetOffsetAbs(pSet);
    rc = audioTestManifestWrite(pSet, "0000\n"); /* A bit messy, but does the trick for now. */
    AssertRCReturn(rc, rc);

    switch (pParms->enmType)
    {
        case AUDIOTESTTYPE_TESTTONE_PLAY:
            RT_FALL_THROUGH();
        case AUDIOTESTTYPE_TESTTONE_RECORD:
        {
            rc = audioTestManifestWrite(pSet, "tone_freq_hz=%RU16\n", (uint16_t)pParms->TestTone.dbFreqHz);
            AssertRCReturn(rc, rc);
            rc = audioTestManifestWrite(pSet, "tone_prequel_ms=%RU32\n", pParms->TestTone.msPrequel);
            AssertRCReturn(rc, rc);
            rc = audioTestManifestWrite(pSet, "tone_duration_ms=%RU32\n", pParms->TestTone.msDuration);
            AssertRCReturn(rc, rc);
            rc = audioTestManifestWrite(pSet, "tone_sequel_ms=%RU32\n", pParms->TestTone.msSequel);
            AssertRCReturn(rc, rc);
            rc = audioTestManifestWrite(pSet, "tone_volume_percent=%RU32\n", pParms->TestTone.uVolumePercent);
            AssertRCReturn(rc, rc);
            rc = audioTestManifestWrite(pSet, "tone_pcm_hz=%RU32\n", PDMAudioPropsHz(&pParms->TestTone.Props));
            AssertRCReturn(rc, rc);
            rc = audioTestManifestWrite(pSet, "tone_pcm_channels=%RU8\n", PDMAudioPropsChannels(&pParms->TestTone.Props));
            AssertRCReturn(rc, rc);
            rc = audioTestManifestWrite(pSet, "tone_pcm_bits=%RU8\n", PDMAudioPropsSampleBits(&pParms->TestTone.Props));
            AssertRCReturn(rc, rc);
            rc = audioTestManifestWrite(pSet, "tone_pcm_is_signed=%RTbool\n", PDMAudioPropsIsSigned(&pParms->TestTone.Props));
            AssertRCReturn(rc, rc);
            break;
        }

        default:
            AssertFailed();
            break;
    }

    RTListAppend(&pSet->lstTest, &pEntry->Node);

    pSet->cTests++;
    pSet->cTestsRunning++;
    pSet->pTestCur = pEntry;

    *ppEntry = pEntry;

    return rc;
}

/**
 * Marks a running test as failed.
 *
 * @returns VBox status code.
 * @param   pEntry              Test to mark.
 * @param   rc                  Error code.
 * @param   pszErr              Error description.
 */
int AudioTestSetTestFailed(PAUDIOTESTENTRY pEntry, int rc, const char *pszErr)
{
    AssertReturn(pEntry->pParent->cTestsRunning == 1,                             VERR_WRONG_ORDER); /* No test nesting allowed. */
    AssertReturn(pEntry->rc                     == VERR_IPE_UNINITIALIZED_STATUS, VERR_WRONG_ORDER);

    pEntry->rc = rc;

    int rc2 = audioTestManifestWrite(pEntry->pParent, "error_rc=%RI32\n", rc);
    AssertRCReturn(rc2, rc2);
    rc2 = audioTestManifestWrite(pEntry->pParent, "error_desc=%s\n", pszErr);
    AssertRCReturn(rc2, rc2);

    pEntry->pParent->cTestsRunning--;
    pEntry->pParent->pTestCur = NULL;

    return rc2;
}

/**
 * Marks a running test as successfully done.
 *
 * @returns VBox status code.
 * @param   pEntry              Test to mark.
 */
int AudioTestSetTestDone(PAUDIOTESTENTRY pEntry)
{
    AssertReturn(pEntry->pParent->cTestsRunning == 1,                             VERR_WRONG_ORDER); /* No test nesting allowed. */
    AssertReturn(pEntry->rc                     == VERR_IPE_UNINITIALIZED_STATUS, VERR_WRONG_ORDER);

    pEntry->rc = VINF_SUCCESS;

    int rc2 = audioTestManifestWrite(pEntry->pParent, "error_rc=%RI32\n", VINF_SUCCESS);
    AssertRCReturn(rc2, rc2);

    pEntry->pParent->cTestsRunning--;
    pEntry->pParent->pTestCur = NULL;

    return rc2;
}

/**
 * Returns whether a test is still running or not.
 *
 * @returns \c true if test is still running, or \c false if not.
 * @param   pEntry              Test to get running status for.
 */
bool AudioTestSetTestIsRunning(PAUDIOTESTENTRY pEntry)
{
    return (pEntry->rc == VERR_IPE_UNINITIALIZED_STATUS);
}

/**
 * Packs a closed audio test so that it's ready for transmission.
 *
 * @returns VBox status code.
 * @param   pSet                Test set to pack.
 * @param   pszOutDir           Directory where to store the packed test set.
 * @param   pszFileName         Where to return the final name of the packed test set. Optional and can be NULL.
 * @param   cbFileName          Size (in bytes) of \a pszFileName.
 */
int AudioTestSetPack(PAUDIOTESTSET pSet, const char *pszOutDir, char *pszFileName, size_t cbFileName)
{
    AssertPtrReturn(pSet, VERR_INVALID_POINTER);
    AssertReturn(!pszFileName || cbFileName, VERR_INVALID_PARAMETER);
    AssertReturn(!audioTestManifestIsOpen(pSet), VERR_WRONG_ORDER);

    /* No more validation (no / still running tests) here -- just pack all stuff we got so far
     * and let the verification routine deal with it later. */

    /** @todo Check and deny if \a pszOutDir is part of the set's path. */

    int rc = RTDirCreateFullPath(pszOutDir, 0755);
    if (RT_FAILURE(rc))
        return rc;

    char szOutName[RT_ELEMENTS(AUDIOTEST_PATH_PREFIX_STR) + AUDIOTEST_TAG_MAX + 16];
    if (RTStrPrintf2(szOutName, sizeof(szOutName), "%s-%s%s",
                     AUDIOTEST_PATH_PREFIX_STR, pSet->szTag, AUDIOTEST_ARCHIVE_SUFF_STR) <= 0)
        AssertFailedReturn(VERR_BUFFER_OVERFLOW);

    char szOutPath[RTPATH_MAX];
    rc = RTPathJoin(szOutPath, sizeof(szOutPath), pszOutDir, szOutName);
    AssertRCReturn(rc, rc);

    const char *apszArgs[10];
    unsigned    cArgs = 0;

    apszArgs[cArgs++] = "vkat";
    apszArgs[cArgs++] = "--create";
    apszArgs[cArgs++] = "--gzip";
    apszArgs[cArgs++] = "--directory";
    apszArgs[cArgs++] = pSet->szPathAbs;
    apszArgs[cArgs++] = "--file";
    apszArgs[cArgs++] = szOutPath;
    apszArgs[cArgs++] = ".";

    RTEXITCODE rcExit = RTZipTarCmd(cArgs, (char **)apszArgs);
    if (rcExit != RTEXITCODE_SUCCESS)
        rc = VERR_GENERAL_FAILURE; /** @todo Fudge! */

    if (RT_SUCCESS(rc))
    {
        if (pszFileName)
            rc = RTStrCopy(pszFileName, cbFileName, szOutPath);
    }

    return rc;
}

/**
 * Returns whether a test set archive is packed (as .tar.gz by default) or
 * a plain directory.
 *
 * @returns \c true if packed (as .tar.gz), or \c false if not (directory).
 * @param   pszPath             Path to return packed staus for.
 */
bool AudioTestSetIsPacked(const char *pszPath)
{
    /** @todo Improve this, good enough for now. */
    return (RTStrIStr(pszPath, AUDIOTEST_ARCHIVE_SUFF_STR) != NULL);
}

/**
 * Returns whether a test set has running (active) tests or not.
 *
 * @returns \c true if it has running tests, or \c false if not.
 * @param   pSet                Test set to return status for.
 */
bool AudioTestSetIsRunning(PAUDIOTESTSET pSet)
{
    return (pSet->cTestsRunning > 0);
}

/**
 * Unpacks a formerly packed audio test set.
 *
 * @returns VBox status code.
 * @param   pszFile             Test set file to unpack. Must contain the absolute path.
 * @param   pszOutDir           Directory where to unpack the test set into.
 *                              If the directory does not exist it will be created.
 */
int AudioTestSetUnpack(const char *pszFile, const char *pszOutDir)
{
    AssertReturn(pszFile && pszOutDir, VERR_INVALID_PARAMETER);

    int rc = VINF_SUCCESS;

    if (!RTDirExists(pszOutDir))
    {
        rc = RTDirCreateFullPath(pszOutDir, 0755);
        if (RT_FAILURE(rc))
            return rc;
    }

    const char *apszArgs[8];
    unsigned    cArgs = 0;

    apszArgs[cArgs++] = "vkat";
    apszArgs[cArgs++] = "--extract";
    apszArgs[cArgs++] = "--gunzip";
    apszArgs[cArgs++] = "--directory";
    apszArgs[cArgs++] = pszOutDir;
    apszArgs[cArgs++] = "--file";
    apszArgs[cArgs++] = pszFile;

    RTEXITCODE rcExit = RTZipTarCmd(cArgs, (char **)apszArgs);
    if (rcExit != RTEXITCODE_SUCCESS)
        rc = VERR_GENERAL_FAILURE; /** @todo Fudge! */

    return rc;
}

/**
 * Retrieves an object handle of a specific test set section.
 *
 * @returns VBox status code.
 * @param   pSet                Test set the section contains.
 * @param   pszSec              Name of section to retrieve object handle for.
 * @param   phSec               Where to store the object handle on success.
 */
static int audioTestSetGetSection(PAUDIOTESTSET pSet, const char *pszSec, PAUDIOTESTOBJINT phSec)
{
    int rc = RTStrCopy(phSec->szSec, sizeof(phSec->szSec), pszSec);
    if (RT_FAILURE(rc))
        return rc;

    phSec->pSet = pSet;

    /** @todo Check for section existence. */
    RT_NOREF(pSet);

    return VINF_SUCCESS;
}

/**
 * Retrieves an object handle of a specific test.
 *
 * @returns VBox status code.
 * @param   pSet                Test set the test contains.
 * @param   idxTst              Index of test to retrieve the object handle for.
 * @param   phTst               Where to store the object handle on success.
 */
static int audioTestSetGetTest(PAUDIOTESTSET pSet, uint32_t idxTst, PAUDIOTESTOBJINT phTst)
{
    char szSec[AUDIOTEST_MAX_SEC_LEN];
    if (RTStrPrintf2(szSec, sizeof(szSec), "test_%04RU32", idxTst) <= 0)
        return VERR_BUFFER_OVERFLOW;

    return audioTestSetGetSection(pSet, szSec, phTst);
}

/**
 * Initializes a test object.
 *
 * @param   pObj                Object to initialize.
 */
static void audioTestObjInit(PAUDIOTESTOBJINT pObj)
{
    RT_BZERO(pObj, sizeof(AUDIOTESTOBJINT));

    pObj->cRefs = 1;

    RTListInit(&pObj->lstMeta);
}

/**
 * Retrieves a child object of a specific parent object.
 *
 * @returns VBox status code.
 * @param   pParent             Parent object the child object contains.
 * @param   idxObj              Index of object to retrieve the object handle for.
 * @param   pObj                Where to store the object handle on success.
 */
static int audioTestObjGetChild(PAUDIOTESTOBJINT pParent, uint32_t idxObj, PAUDIOTESTOBJINT pObj)
{
    char szObj[AUDIOTEST_MAX_SEC_LEN];
    if (RTStrPrintf2(szObj, sizeof(szObj), "obj%RU32_uuid", idxObj) <= 0)
        AssertFailedReturn(VERR_BUFFER_OVERFLOW);

    char szUuid[AUDIOTEST_MAX_SEC_LEN];
    int rc = audioTestObjGetStr(pParent, szObj, szUuid, sizeof(szUuid));
    if (RT_SUCCESS(rc))
    {
        audioTestObjInit(pObj);

        AssertReturn(RTStrPrintf2(pObj->szSec, sizeof(pObj->szSec), "obj_%s", szUuid) > 0, VERR_BUFFER_OVERFLOW);

        /** @todo Check test section existence. */

        pObj->pSet = pParent->pSet;
    }

    return rc;
}

/**
 * Verifies a value of a test verification job.
 *
 * @returns VBox status code.
 * @returns Error if the verification failed and test verification job has fKeepGoing not set.
 * @param   pVerJob             Verification job to verify value for.
 * @param   pObjA               Object handle A to verify value for.
 * @param   pObjB               Object handle B to verify value for.
 * @param   pszKey              Key to verify.
 * @param   pszVal              Value to verify.
 * @param   pszErrFmt           Error format string in case the verification failed.
 * @param   ...                 Variable aruments for error format string.
 */
static int audioTestVerifyValue(PAUDIOTESTVERIFYJOB pVerJob,
                                PAUDIOTESTOBJINT pObjA, PAUDIOTESTOBJINT pObjB, const char *pszKey, const char *pszVal, const char *pszErrFmt, ...)
{
    va_list va;
    va_start(va, pszErrFmt);

    char szValA[_1K];
    int rc = audioTestObjGetStr(pObjA, pszKey, szValA, sizeof(szValA));
    if (RT_SUCCESS(rc))
    {
        char szValB[_1K];
        rc = audioTestObjGetStr(pObjB, pszKey, szValB, sizeof(szValB));
        if (RT_SUCCESS(rc))
        {
            if (RTStrCmp(szValA, szValB))
            {
                int rc2 = audioTestErrorDescAddError(pVerJob->pErr, pVerJob->idxTest,
                                                     "Values are not equal ('%s' vs. '%s')", szValA, szValB);
                AssertRC(rc2);
                rc = VERR_WRONG_TYPE; /** @todo Fudge! */
            }

            if (pszVal)
            {
                if (RTStrCmp(szValA, pszVal))
                {
                    int rc2 = audioTestErrorDescAddError(pVerJob->pErr, pVerJob->idxTest,
                                                         "Values don't match expected value (got '%s', expected '%s')", szValA, pszVal);
                    AssertRC(rc2);
                    rc = VERR_WRONG_TYPE; /** @todo Fudge! */
                }
            }
        }
    }

    if (RT_FAILURE(rc))
    {
        int rc2 = audioTestErrorDescAddV(pVerJob->pErr, pVerJob->idxTest, rc, pszErrFmt, va);
        AssertRC(rc2);
    }

    va_end(va);

    return pVerJob->Opts.fKeepGoing ? VINF_SUCCESS : rc;
}

/**
 * Opens a test object which is a regular file.
 *
 * @returns VBox status code.
 * @param   pObj                Test object to open.
 * @param   pszFile             Absolute file path of file to open.
 */
static int audioTestObjOpenFile(PAUDIOTESTOBJINT pObj, const char *pszFile)
{
    int rc = RTFileOpen(&pObj->File.hFile, pszFile, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE);
    if (RT_SUCCESS(rc))
    {
        int rc2 = RTStrCopy(pObj->szName, sizeof(pObj->szName), pszFile);
        AssertRC(rc2);

        pObj->enmType = AUDIOTESTOBJTYPE_FILE;
    }

    return rc;
}

/**
 * Opens an existing audio test object.
 *
 * @returns VBox status code.
 * @param   pObj                Object to open.
 */
static int audioTestObjOpen(PAUDIOTESTOBJINT pObj)
{
    AssertReturn(pObj->enmType == AUDIOTESTOBJTYPE_UNKNOWN, VERR_WRONG_ORDER);

    char szFileName[AUDIOTEST_MAX_SEC_LEN];
    int rc = audioTestObjGetStr(pObj, "obj_name", szFileName, sizeof(szFileName));
    if (RT_SUCCESS(rc))
    {
        char szFilePath[RTPATH_MAX];
        rc = RTPathJoin(szFilePath, sizeof(szFilePath), pObj->pSet->szPathAbs, szFileName);
        if (RT_SUCCESS(rc))
        {
            /** @todo Check "obj_type". */
            rc = audioTestObjOpenFile(pObj, szFilePath);
        }
    }
    return rc;
}

/**
 * Closes an audio test set object.
 *
 * @returns VBox status code.
 * @param   pObj                Object to close.
 */
static int audioTestObjClose(PAUDIOTESTOBJINT pObj)
{
    if (!audioTestObjIsOpen(pObj))
        return VINF_SUCCESS;

    int rc;

    /** @todo Generalize this function more once we have more object types. */

    if (RTFileIsValid(pObj->File.hFile))
    {
        rc = RTFileClose(pObj->File.hFile);
        if (RT_SUCCESS(rc))
            pObj->File.hFile = NIL_RTFILE;
    }
    else
        rc = VINF_SUCCESS;

    return rc;
}

/**
 * Returns whether a test set object is in opened state or not.
 *
 * @returns \c true if open, or \c false if not.
 * @param   pObj                Object to return status for.
 */
static bool audioTestObjIsOpen(PAUDIOTESTOBJINT pObj)
{
    return pObj->enmType != AUDIOTESTOBJTYPE_UNKNOWN;
}

/**
 * Finalizes an audio test set object.
 *
 * @param   pObj                Test object to finalize.
 */
static void audioTestObjFinalize(PAUDIOTESTOBJINT pObj)
{
    /** @todo Generalize this function more once we have more object types. */
    AssertReturnVoid(pObj->enmType == AUDIOTESTOBJTYPE_FILE);

    if (RTFileIsValid(pObj->File.hFile))
        pObj->File.cbSize = RTFileTell(pObj->File.hFile);
}

/**
 * Retrieves tone PCM properties of an object.
 *
 * @returns VBox status code.
 * @param   pObj               Object to retrieve PCM properties for.
 * @param   pProps              Where to store the PCM properties on success.
 */
static int audioTestObjGetTonePcmProps(PAUDIOTESTOBJINT pObj, PPDMAUDIOPCMPROPS pProps)
{
    int rc;
    uint32_t uHz;
    rc = audioTestObjGetUInt32(pObj, "tone_pcm_hz", &uHz);
    AssertRCReturn(rc, rc);
    uint8_t cBits;
    rc = audioTestObjGetUInt8(pObj, "tone_pcm_bits", &cBits);
    AssertRCReturn(rc, rc);
    uint8_t cChan;
    rc = audioTestObjGetUInt8(pObj, "tone_pcm_channels", &cChan);
    AssertRCReturn(rc, rc);
    bool    fSigned;
    rc = audioTestObjGetBool(pObj, "tone_pcm_is_signed", &fSigned);
    AssertRCReturn(rc, rc);

    PDMAudioPropsInit(pProps, (cBits / 8), fSigned, cChan, uHz);

    return VINF_SUCCESS;
}

/**
 * Normalizes PCM audio data.
 * Only supports 16 bit stereo PCM data for now.
 *
 * @returns VBox status code.
 * @param   hFileSrc            Source file handle of audio data to normalize.
 * @param   pProps              PCM properties to use for normalization.
 * @param   cbSize              Size (in bytes) of audio data to normalize.
 * @param   dbNormalizePercent  Normalization (percent) to achieve.
 * @param   hFileDst            Destiation file handle (must be open) where to write the normalized audio data to.
 * @param   pdbRatio            Where to store the normalization ratio used on success. Optional and can be NULL.
 *                              A ration of exactly 1 means no normalization.
 *
 * @note    The source file handle must point at the beginning of the PCM audio data to normalize.
 */
static int audioTestFileNormalizePCM(RTFILE hFileSrc, PCPDMAUDIOPCMPROPS pProps, uint64_t cbSize,
                                     double dbNormalizePercent, RTFILE hFileDst, double *pdbRatio)
{
    if (   !pProps->fSigned
        ||  pProps->cbSampleX != 2) /* Fend-off non-supported stuff first. */
        return VERR_NOT_SUPPORTED;

    int rc = VINF_SUCCESS; /* Shut up MSVC. */

    if (!cbSize)
    {
        rc = RTFileQuerySize(hFileSrc, &cbSize);
        AssertRCReturn(rc, rc);
    }
    else
        AssertReturn(PDMAudioPropsIsSizeAligned(pProps, (uint32_t)cbSize), VERR_INVALID_PARAMETER);

    uint64_t offStart = RTFileTell(hFileSrc);
    size_t   cbToRead = cbSize;

    /* Find minimum and maximum peaks. */
    int16_t iMin    = 0;
    int16_t iMax    = 0;
    double  dbRatio = 0.0;

    uint8_t auBuf[_64K];
    while (cbToRead)
    {
        size_t const cbChunk = RT_MIN(cbToRead, sizeof(auBuf));
        size_t       cbRead  = 0;
        rc = RTFileRead(hFileSrc, auBuf, cbChunk, &cbRead);
        if (rc == VERR_EOF)
            break;
        AssertRCBreak(rc);

        AssertBreak(PDMAudioPropsIsSizeAligned(pProps, (uint32_t)cbRead));

        switch (pProps->cbSampleX)
        {
            case 2: /* 16 bit signed */
            {
                int16_t *pi16Src = (int16_t *)auBuf;
                for (size_t i = 0; i < cbRead / pProps->cbSampleX; i += pProps->cbSampleX)
                {
                    if (*pi16Src < iMin)
                        iMin = *pi16Src;
                    if (*pi16Src > iMax)
                        iMax = *pi16Src;
                    pi16Src++;
                }
                break;
            }

            default:
                AssertMsgFailedBreakStmt(("Invalid bytes per sample: %RU8\n", pProps->cbSampleX), rc = VERR_NOT_SUPPORTED);
        }

        Assert(cbToRead >= cbRead);
        cbToRead -= cbRead;
    }

    if (RT_FAILURE(rc))
        return rc;

    /* Now rewind and do the actual gain / attenuation. */
    rc = RTFileSeek(hFileSrc, offStart, RTFILE_SEEK_BEGIN, NULL /* poffActual */);
    AssertRCReturn(rc, rc);
    cbToRead = cbSize;

    switch (pProps->cbSampleX)
    {
        case 2: /* 16 bit signed */
        {
            if (iMin == INT16_MIN)
                iMin = INT16_MIN + 1;
            if ((-iMin) > iMax)
                iMax = -iMin;

            dbRatio = iMax == 0 ? 1.0 : ((double)INT16_MAX * dbNormalizePercent) / ((double)iMax * 100.0);

            while (cbToRead)
            {
                size_t const cbChunk = RT_MIN(cbToRead, sizeof(auBuf));
                size_t       cbRead;
                rc = RTFileRead(hFileSrc, auBuf, cbChunk, &cbRead);
                if (rc == VERR_EOF)
                    break;
                AssertRCBreak(rc);

                int16_t *pi16Src = (int16_t *)auBuf;
                for (size_t i = 0; i < cbRead / pProps->cbSampleX; i += pProps->cbSampleX)
                {
                    /** @todo Optimize this -- use a lookup table for sample indices? */
                    if ((*pi16Src * dbRatio) > INT16_MAX)
                        *pi16Src = INT16_MAX;
                    else if ((*pi16Src * dbRatio) < INT16_MIN)
                        *pi16Src = INT16_MIN;
                    else
                        *pi16Src = (int16_t)(*pi16Src * dbRatio);
                    pi16Src++;
                }

                size_t cbWritten;
                rc = RTFileWrite(hFileDst, auBuf, cbChunk, &cbWritten);
                AssertRCBreak(rc);
                Assert(cbWritten == cbChunk);

                Assert(cbToRead >= cbRead);
                cbToRead -= cbRead;
            }
            break;
        }

        default:
            AssertMsgFailedBreakStmt(("Invalid bytes per sample: %RU8\n", pProps->cbSampleX), rc = VERR_NOT_SUPPORTED);
    }

    if (RT_SUCCESS(rc))
    {
        if (pdbRatio)
            *pdbRatio = dbRatio;
    }

    return rc;
}

/**
 * Normalizes a test set audio object's audio data, extended version.
 *
 * @returns VBox status code. On success the test set object will point to the (temporary) normalized file data.
 * @param   pVerJob             Verification job that contains \a pObj.
 * @param   pObj                Test set object to normalize.
 * @param   pProps              PCM properties to use for normalization.
 * @param   cbSize              Size (in bytes) of audio data to normalize.
 * @param   dbNormalizePercent  Normalization to achieve (in percent).
 *
 * @note    The test set's file pointer must point to beginning of PCM data to normalize.
 */
static int audioTestObjFileNormalizeEx(PAUDIOTESTVERIFYJOB pVerJob,
                                       PAUDIOTESTOBJINT pObj, PPDMAUDIOPCMPROPS pProps, uint64_t cbSize, double dbNormalizePercent)
{
    /* Store normalized file into a temporary file. */
    char szFileDst[RTPATH_MAX];
    int rc = RTPathTemp(szFileDst, sizeof(szFileDst));
    AssertRCReturn(rc, rc);

    rc = RTPathAppend(szFileDst, sizeof(szFileDst), "VBoxAudioTest-normalized-XXX.pcm");
    AssertRCReturn(rc, rc);

    rc = RTFileCreateTemp(szFileDst, 0600);
    AssertRCReturn(rc, rc);

    RTFILE hFileDst;
    rc = RTFileOpen(&hFileDst, szFileDst, RTFILE_O_OPEN | RTFILE_O_WRITE | RTFILE_O_DENY_WRITE);
    AssertRCReturn(rc, rc);

    double dbRatio = 0.0;
    rc = audioTestFileNormalizePCM(pObj->File.hFile, pProps, cbSize, dbNormalizePercent, hFileDst, &dbRatio);
    if (RT_SUCCESS(rc))
    {
        int rc2 = audioTestErrorDescAddInfo(pVerJob->pErr, pVerJob->idxTest, "Normalized '%s' -> '%s' (ratio is %u.%02u%%)\n",
                                            pObj->szName, szFileDst, (unsigned)dbRatio, (unsigned)(dbRatio * 100) % 100);
        AssertRC(rc2);
    }

    int rc2 = RTFileClose(hFileDst);
    if (RT_SUCCESS(rc))
        rc = rc2;

    if (RT_SUCCESS(rc))
    {
        /* Close the original test set object and use the (temporary) normalized file instead now. */
        rc = audioTestObjClose(pObj);
        if (RT_SUCCESS(rc))
            rc = audioTestObjOpenFile(pObj, szFileDst);
    }

    return rc;
}

/**
 * Normalizes a test set audio object's audio data.
 *
 * @returns VBox status code.
 * @param   pVerJob             Verification job that contains \a pObj.
 * @param   pObj                Test set object to normalize.
 * @param   pProps              PCM properties to use for normalization.
 *
 * @note    The test set's file pointer must point to beginning of PCM data to normalize.
 */
static int audioTestObjFileNormalize(PAUDIOTESTVERIFYJOB pVerJob, PAUDIOTESTOBJINT pObj, PPDMAUDIOPCMPROPS pProps)
{
    return audioTestObjFileNormalizeEx(pVerJob,
                                       pObj, pProps, 0 /* cbSize, 0 means all */, 100.0 /* dbNormalizePercent */);
}

/**
 * Structure for keeping file comparison parameters for one file.
 */
typedef struct AUDIOTESTFILECMPPARMS
{
    /** File name for logging purposes. */
    const char *pszName;
    /** File handle to file to compare. */
    RTFILE      hFile;
    /** Absolute offset (in bytes) to start comparing.
     *  Ignored when set to 0. */
    uint64_t    offStart;
    /** Size (in bytes) of area to compare.
     *  Starts at \a offStart. */
    uint64_t    cbSize;
} AUDIOTESTFILECMPPARMS;
/** Pointer to file comparison parameters for one file. */
typedef AUDIOTESTFILECMPPARMS *PAUDIOTESTFILECMPPARMS;

/**
 * Determines if a given file chunk contains all silence (i.e. non-audible audio data) or not.
 *
 * What "silence" means depends on the given PCM parameters.
 *
 * @returns VBox status code.
 * @param   phFile              File handle of file to determine silence for.
 * @param   pProps              PCM properties to use.
 * @param   offStart            Start offset (absolute, in bytes) to start at.
 * @param   cbSize              Size (in bytes) to process.
 * @param   pfIsSilence         Where to return the result.
 *
 * @note    Does *not* modify the file's current position.
 */
static int audioTestFileChunkIsSilence(PRTFILE phFile, PPDMAUDIOPCMPROPS pProps, uint64_t offStart, size_t cbSize,
                                       bool *pfIsSilence)
{
    bool fIsSilence = true;

    int rc = RTFileSeek(*phFile, offStart, RTFILE_SEEK_BEGIN, NULL);
    AssertRCReturn(rc, rc);

    uint8_t auBuf[_64K];
    while (cbSize)
    {
        size_t cbRead;
        rc = RTFileRead(*phFile, auBuf, RT_MIN(cbSize, sizeof(auBuf)), &cbRead);
        AssertRC(rc);

        if (!PDMAudioPropsIsBufferSilence(pProps, auBuf, cbRead))
        {
            fIsSilence = false;
            break;
        }

        AssertBreak(cbSize >= cbRead);
        cbSize -= cbRead;
    }

    if (RT_SUCCESS(rc))
        *pfIsSilence = fIsSilence;

    return RTFileSeek(*phFile, offStart, RTFILE_SEEK_BEGIN, NULL);
}

/**
 * Finds differences in two audio test files by binary comparing chunks.
 *
 * @returns Number of differences. 0 means they are equal (but not necessarily identical).
 * @param   pVerJob             Verification job to verify PCM data for.
 * @param   pCmpA               File comparison parameters to file A to compare file B with.
 * @param   pCmpB               File comparison parameters to file B to compare file A with.
 * @param   pToneParms          Tone parameters to use for comparison.
 */
static uint32_t audioTestFilesFindDiffsBinary(PAUDIOTESTVERIFYJOB pVerJob,
                                              PAUDIOTESTFILECMPPARMS pCmpA, PAUDIOTESTFILECMPPARMS pCmpB,
                                              PAUDIOTESTTONEPARMS pToneParms)
{
    uint8_t auBufA[_4K];
    uint8_t auBufB[_4K];

    int rc = RTFileSeek(pCmpA->hFile, pCmpA->offStart, RTFILE_SEEK_BEGIN, NULL);
    AssertRC(rc);

    rc = RTFileSeek(pCmpB->hFile, pCmpB->offStart, RTFILE_SEEK_BEGIN, NULL);
    AssertRC(rc);

    uint32_t cDiffs  = 0;
    uint64_t cbDiffs = 0;

    uint32_t const cbChunkSize = PDMAudioPropsFrameSize(&pToneParms->Props); /* Use the audio frame size as chunk size. */

    uint64_t offCur       = 0;
    uint64_t offDiffStart = 0;
    bool     fInDiff      = false;
    uint64_t cbSize       = RT_MIN(pCmpA->cbSize, pCmpB->cbSize);
    uint64_t cbToCompare  = cbSize;

    while (cbToCompare)
    {
        size_t cbReadA;
        rc = RTFileRead(pCmpA->hFile, auBufA, RT_MIN(cbToCompare, cbChunkSize), &cbReadA);
        AssertRCBreak(rc);
        size_t cbReadB;
        rc = RTFileRead(pCmpB->hFile, auBufB, RT_MIN(cbToCompare, cbChunkSize), &cbReadB);
        AssertRCBreak(rc);
        AssertBreakStmt(cbReadA == cbReadB, rc = VERR_INVALID_PARAMETER); /** @todo Find a better rc. */

        const size_t cbToCmp = RT_MIN(cbReadA, cbReadB);
        if (memcmp(auBufA, auBufB, cbToCmp) != 0)
        {
            if (!fInDiff) /* No consequitive different chunk? Count as new then. */
            {
                cDiffs++;
                offDiffStart = offCur;
                fInDiff  = true;
            }
        }
        else /* Reset and count next difference as new then. */
        {
            if (fInDiff)
            {
                bool fIsAllSilenceA;
                rc = audioTestFileChunkIsSilence(&pCmpA->hFile, &pToneParms->Props,
                                                 pCmpA->offStart + offDiffStart, offCur - offDiffStart, &fIsAllSilenceA);
                AssertRCBreak(rc);

                bool fIsAllSilenceB;
                rc = audioTestFileChunkIsSilence(&pCmpB->hFile, &pToneParms->Props,
                                                 pCmpB->offStart + offDiffStart, offCur - offDiffStart, &fIsAllSilenceB);
                AssertRCBreak(rc);

                uint32_t const cbDiff = offCur - offDiffStart;
                int rc2 = audioTestErrorDescAddInfo(pVerJob->pErr, pVerJob->idxTest, "Chunks differ: '%s' @ %#x [%08RU64-%08RU64] vs. '%s' @ %#x [%08RU64-%08RU64] (%RU64 bytes, %RU64ms)",
                                                                                     pCmpA->pszName, pCmpA->offStart + offDiffStart, pCmpA->offStart + offDiffStart, pCmpA->offStart + offCur,
                                                                                     pCmpB->pszName, pCmpB->offStart + offDiffStart, pCmpB->offStart + offDiffStart, pCmpB->offStart + offCur,
                                                                                     cbDiff, PDMAudioPropsBytesToMilli(&pToneParms->Props, cbDiff));
                AssertRC(rc2);
                if (   fIsAllSilenceA
                    || fIsAllSilenceB)
                {
                    rc2 = audioTestErrorDescAddInfo(pVerJob->pErr, pVerJob->idxTest, "Chunk %s @ %#x (%RU64 bytes, %RU64ms) is all silence",
                                                                                     fIsAllSilenceA ? pCmpA->pszName : pCmpB->pszName,
                                                                                     offDiffStart, cbDiff, PDMAudioPropsBytesToMilli(&pToneParms->Props, cbDiff));
                    AssertRC(rc2);
                }

                cbDiffs += cbDiff;
            }
            fInDiff = false;
        }

        AssertBreakStmt(cbToCompare >= cbReadA, rc = VERR_INTERNAL_ERROR);
        cbToCompare -= cbReadA;
        offCur      += cbReadA;
    }

    /* If we didn't mention the last diff yet, do so now. */
    if (fInDiff)
    {
        uint32_t const cbDiff = offCur - offDiffStart;
        int rc2 = audioTestErrorDescAddInfo(pVerJob->pErr, pVerJob->idxTest, "Chunks differ: '%s' @ %#x [%08RU64-%08RU64] vs. '%s' @ %#x [%08RU64-%08RU64] (%RU64 bytes, %RU64ms)",
                                                                             pCmpA->pszName, pCmpA->offStart + offDiffStart, pCmpA->offStart + offDiffStart, pCmpA->offStart + offCur,
                                                                             pCmpB->pszName, pCmpB->offStart + offDiffStart, pCmpB->offStart + offDiffStart, pCmpB->offStart + offCur,
                                                                             cbDiff, PDMAudioPropsBytesToMilli(&pToneParms->Props, cbDiff));
        AssertRC(rc2);

        cbDiffs += cbDiff;
    }

    if (   cbSize
        && cbDiffs)
    {
        uint8_t const uDiffPercent = cbDiffs / (cbSize * 100);
        if (uDiffPercent > pVerJob->Opts.uMaxDiffPercent)
        {
            int rc2 = audioTestErrorDescAddInfo(pVerJob->pErr, pVerJob->idxTest, "Files binary-differ too much (expected maximum %RU8%%, got %RU8%%)",
                                                                                 pVerJob->Opts.uMaxDiffPercent, uDiffPercent);
            AssertRC(rc2);
        }
    }

    return cDiffs;
}

/**
 * Initializes a audio test audio beacon.
 *
 * @param   pBeacon             Audio test beacon to (re-)initialize.
 * @param   uTest               Test number to set beacon to.
 * @param   enmType             Beacon type to set.
 * @param   pProps              PCM properties to use for producing audio beacon data.
 */
void AudioTestBeaconInit(PAUDIOTESTTONEBEACON pBeacon, uint8_t uTest, AUDIOTESTTONEBEACONTYPE enmType, PPDMAUDIOPCMPROPS pProps)
{
    AssertReturnVoid(PDMAudioPropsFrameSize(pProps) == 4); /** @todo Make this more dynamic. */

    RT_BZERO(pBeacon, sizeof(AUDIOTESTTONEBEACON));

    pBeacon->uTest   = uTest;
    pBeacon->enmType = enmType;
    memcpy(&pBeacon->Props, pProps, sizeof(PDMAUDIOPCMPROPS));

    pBeacon->cbSize = PDMAudioPropsFramesToBytes(&pBeacon->Props, AUDIOTEST_BEACON_SIZE_FRAMES);
}

/**
 * Returns the beacon byte of a beacon type.
 *
 * @returns Beacon byte if found, 0 otherwise.
 * @param   uTest               Test number to get beacon byte for.
 * @param   enmType             Beacon type to get beacon byte for.
 */
DECLINLINE(uint8_t) AudioTestBeaconByteFromType(uint8_t uTest, AUDIOTESTTONEBEACONTYPE enmType)
{
    switch (enmType)
    {
        case AUDIOTESTTONEBEACONTYPE_PLAY_PRE:  return AUDIOTEST_BEACON_MAKE_PRE(uTest);
        case AUDIOTESTTONEBEACONTYPE_PLAY_POST: return AUDIOTEST_BEACON_MAKE_POST(uTest);
        case AUDIOTESTTONEBEACONTYPE_REC_PRE:   return AUDIOTEST_BEACON_MAKE_PRE(uTest);
        case AUDIOTESTTONEBEACONTYPE_REC_POST:  return AUDIOTEST_BEACON_MAKE_POST(uTest);
        default:                                break;
    }

    AssertFailed();
    return 0;
}

/**
 * Returns the total expected (total) size of an audio beacon (in bytes).
 *
 * @returns  Beacon size in bytes.
 * @param   pBeacon             Beacon to get beacon size for.
 */
uint32_t AudioTestBeaconGetSize(PCAUDIOTESTTONEBEACON pBeacon)
{
    return pBeacon->cbSize;
}

/**
 * Returns the beacon type of an audio beacon.
 *
 * @returns Beacon type.
 * @param   pBeacon             Beacon to get beacon size for.
 */
AUDIOTESTTONEBEACONTYPE AudioTestBeaconGetType(PCAUDIOTESTTONEBEACON pBeacon)
{
    return pBeacon->enmType;
}

/**
 * Returns the remaining bytes (to be complete) of an audio beacon.
 *
 * @returns Remaining bytes.
 * @param   pBeacon             Beacon to get remaining size for.
 */
uint32_t AudioTestBeaconGetRemaining(PCAUDIOTESTTONEBEACON pBeacon)
{
    return pBeacon->cbSize - pBeacon->cbUsed;
}

/**
 * Returns the already used (received) bytes (to be complete) of an audio beacon.
 *
 * @returns Used bytes.
 * @param   pBeacon             Beacon to get remaining size for.
 */
uint32_t AudioTestBeaconGetUsed(PCAUDIOTESTTONEBEACON pBeacon)
{
    return pBeacon->cbUsed;
}

/**
 * Writes audio beacon data to a given buffer.
 *
 * @returns VBox status code.
 * @param   pBeacon             Beacon to write to buffer.
 * @param   pvBuf               Buffer to write to.
 * @param   cbBuf               Size (in bytes) of buffer to write to.
 */
int AudioTestBeaconWrite(PAUDIOTESTTONEBEACON pBeacon, void *pvBuf, uint32_t cbBuf)
{
    AssertReturn(pBeacon->cbUsed + cbBuf <= pBeacon->cbSize, VERR_BUFFER_OVERFLOW);

    memset(pvBuf, AudioTestBeaconByteFromType(pBeacon->uTest, pBeacon->enmType), cbBuf);

    pBeacon->cbUsed += cbBuf;

    return VINF_SUCCESS;
}

/**
 * Converts an audio beacon type to a string.
 *
 * @returns Pointer to read-only audio beacon type string on success,
 *          "illegal" if invalid command value.
 * @param   enmType             The type to convert.
 */
const char *AudioTestBeaconTypeGetName(AUDIOTESTTONEBEACONTYPE enmType)
{
    switch (enmType)
    {
        case AUDIOTESTTONEBEACONTYPE_PLAY_PRE:  return "pre-playback";
        case AUDIOTESTTONEBEACONTYPE_PLAY_POST: return "post-playback";
        case AUDIOTESTTONEBEACONTYPE_REC_PRE:   return "pre-recording";
        case AUDIOTESTTONEBEACONTYPE_REC_POST:  return "post-recording";
        default:                                break;
    }
    AssertMsgFailedReturn(("Invalid beacon type: #%x\n", enmType), "illegal");
}

/**
 * Adds audio data to a given beacon.
 *
 * @returns VBox status code, VERR_NOT_FOUND if not beacon data was not found.
 * @param   pBeacon             Beacon to add data for.
 * @param   pauBuf              Buffer of audio data to add.
 * @param   cbBuf               Size (in bytes) of \a pauBuf.
 * @param   pOff                Where to return the offset within \a pauBuf where beacon ended on success.
 *                              Optional and can be NULL.
 *
 * @note    The audio data must be a) match the beacon type and b) consecutive, that is, without any gaps,
 *          to be added as valid to the beacon.
 */
int AudioTestBeaconAddConsecutive(PAUDIOTESTTONEBEACON pBeacon, const uint8_t *pauBuf, size_t cbBuf, size_t *pOff)
{
    AssertPtrReturn(pBeacon, VERR_INVALID_POINTER);
    AssertPtrReturn(pauBuf,  VERR_INVALID_POINTER);
    /* pOff is optional. */

    uint64_t       offBeacon   = UINT64_MAX;
    uint32_t const cbFrameSize = PDMAudioPropsFrameSize(&pBeacon->Props); /* Use the audio frame size as chunk size. */

    uint8_t  const byBeacon      = AudioTestBeaconByteFromType(pBeacon->uTest, pBeacon->enmType);
    unsigned const cbStep        = cbFrameSize;

    /* Make sure that we do frame-aligned reads. */
    cbBuf = PDMAudioPropsFloorBytesToFrame(&pBeacon->Props, (uint32_t)cbBuf);

    for (size_t i = 0; i < cbBuf; i += cbStep)
    {
        if (   pauBuf[i]     == byBeacon
            && pauBuf[i + 1] == byBeacon
            && pauBuf[i + 2] == byBeacon
            && pauBuf[i + 3] == byBeacon)
        {
            offBeacon = i + cbStep; /* Point to data right *after* the beacon. */

            /* Make sure to handle overflows and let beacon start from scratch. */
            pBeacon->cbUsed = (pBeacon->cbUsed + cbStep) % pBeacon->cbSize;
            if (pBeacon->cbUsed == 0) /* Beacon complete (see module line above)? */
            {
                pBeacon->cbUsed = pBeacon->cbSize;
                break;
            }
        }
        else
        {
            /* If beacon is complete, just skip, otherwise we detected a gap here. Start all over then. */
            if (RT_UNLIKELY(pBeacon->cbUsed == pBeacon->cbSize))
                break;

            pBeacon->cbUsed = 0;
            offBeacon       = UINT64_MAX;
        }
    }

    if (offBeacon != UINT64_MAX)
    {
        if (pOff)
            *pOff = offBeacon;
    }

    return offBeacon == UINT64_MAX ? VERR_NOT_FOUND : VINF_SUCCESS;
}

/**
 * Returns whether a beacon is considered to be complete or not.
 *
 * A complete beacon means that all data for it has been retrieved.
 *
 * @returns \c true if complete, or \c false if not.
 * @param   pBeacon             Beacon to get completion status for.
 */
bool AudioTestBeaconIsComplete(PCAUDIOTESTTONEBEACON pBeacon)
{
    AssertReturn(pBeacon->cbUsed <= pBeacon->cbSize, true);
    return (pBeacon->cbUsed == pBeacon->cbSize);
}

/**
 * Verifies a pre/post beacon of a test tone.
 *
 * @returns VBox status code, VERR_NOT_FOUND if beacon was not found.
 * @param   pVerJob             Verification job to verify PCM data for.
 * @param   fIn                 Set to \c true for recording, \c false for playback.
 * @param   fPre                Set to \c true to verify a pre beacon, or \c false to verify a post beacon.
 * @param   pCmp                File comparison parameters to file to verify beacon for.
 * @param   pToneParms          Tone parameters to use for verification.
 * @param   puOff               Where to return the absolute file offset (in bytes) right after the found beacon on success.
 *                              Optional and can be NULL.
 */
static int audioTestToneVerifyBeacon(PAUDIOTESTVERIFYJOB pVerJob,
                                     bool fIn, bool fPre, PAUDIOTESTFILECMPPARMS pCmp, PAUDIOTESTTONEPARMS pToneParms,
                                     uint64_t *puOff)
{
    int rc = RTFileSeek(pCmp->hFile, pCmp->offStart, RTFILE_SEEK_BEGIN, NULL);
    AssertRCReturn(rc, rc);

    AUDIOTESTTONEBEACON Beacon;
    RT_ZERO(Beacon);
    AudioTestBeaconInit(&Beacon, pVerJob->idxTest,
                          fIn
                        ? (fPre ? AUDIOTESTTONEBEACONTYPE_PLAY_PRE : AUDIOTESTTONEBEACONTYPE_PLAY_POST)
                        : (fPre ? AUDIOTESTTONEBEACONTYPE_REC_PRE  : AUDIOTESTTONEBEACONTYPE_REC_POST), &pToneParms->Props);

    uint8_t        auBuf[_64K];
    uint64_t       cbToCompare   = pCmp->cbSize;
    uint32_t const cbFrameSize   = PDMAudioPropsFrameSize(&Beacon.Props);
    uint64_t       offBeaconLast = UINT64_MAX;

    Assert(sizeof(auBuf) % cbFrameSize == 0);

    while (cbToCompare)
    {
        size_t cbRead;
        rc = RTFileRead(pCmp->hFile, auBuf, RT_MIN(cbToCompare, sizeof(auBuf)), &cbRead);
        AssertRCBreak(rc);

        if (cbRead < cbFrameSize)
            break;

        size_t uOff;
        int rc2 = AudioTestBeaconAddConsecutive(&Beacon, auBuf, cbRead, &uOff);
        if (RT_SUCCESS(rc2))
        {
            /* Save the last found (absolute bytes, in file) position of a (partially) found beacon. */
            offBeaconLast = RTFileTell(pCmp->hFile) - (cbRead - uOff);
        }

        Assert(cbToCompare >= cbRead);
        cbToCompare -= cbRead;
    }

    uint32_t const cbBeacon = AudioTestBeaconGetUsed(&Beacon);

    if (!AudioTestBeaconIsComplete(&Beacon))
    {
        int rc2 = audioTestErrorDescAddError(pVerJob->pErr, pVerJob->idxTest, "File '%s': %s beacon %s (got %RU32 bytes, expected %RU32)",
                                             pCmp->pszName,
                                             AudioTestBeaconTypeGetName(Beacon.enmType),
                                             cbBeacon ? "found" : "not found", cbBeacon,
                                             AudioTestBeaconGetSize(&Beacon));
        AssertRC(rc2);
        return VERR_NOT_FOUND;
    }
    else
    {
        AssertReturn(AudioTestBeaconGetRemaining(&Beacon) == 0, VERR_INTERNAL_ERROR);
        AssertReturn(offBeaconLast != UINT32_MAX, VERR_INTERNAL_ERROR);
        AssertReturn(offBeaconLast >= AudioTestBeaconGetSize(&Beacon), VERR_INTERNAL_ERROR);

        int rc2 = audioTestErrorDescAddInfo(pVerJob->pErr, pVerJob->idxTest, "File '%s': %s beacon found at offset %RU64 and valid",
                                            pCmp->pszName, AudioTestBeaconTypeGetName(Beacon.enmType),
                                            offBeaconLast - AudioTestBeaconGetSize(&Beacon));
        AssertRC(rc2);

        if (puOff)
            *puOff = offBeaconLast;
    }

    return rc;
}

#define CHECK_RC_MAYBE_RET(a_rc, a_pVerJob) \
    if (RT_FAILURE(a_rc)) \
    { \
        if (!a_pVerJob->Opts.fKeepGoing) \
            return VINF_SUCCESS; \
    }

#define CHECK_RC_MSG_MAYBE_RET(a_rc, a_pVerJob, a_Msg) \
    if (RT_FAILURE(a_rc)) \
    { \
        int rc3 = audioTestErrorDescAddError(a_pVerJob->pErr, a_pVerJob->idxTest, a_Msg); \
        AssertRC(rc3); \
        if (!a_pVerJob->Opts.fKeepGoing) \
            return VINF_SUCCESS; \
    }

#define CHECK_RC_MSG_VA_MAYBE_RET(a_rc, a_pVerJob, a_Msg, ...) \
    if (RT_FAILURE(a_rc)) \
    { \
        int rc3 = audioTestErrorDescAddError(a_pVerJob->pErr, a_pVerJob->idxTest, a_Msg, __VA_ARGS__); \
        AssertRC(rc3); \
        if (!a_pVerJob->Opts.fKeepGoing) \
            return VINF_SUCCESS; \

/**
 * Does the actual PCM data verification of a test tone.
 *
 * @returns VBox status code.
 * @param   pVerJob             Verification job to verify PCM data for.
 * @param   phTestA             Test handle A of test to verify PCM data for.
 * @param   phTestB             Test handle B of test to verify PCM data for.
 */
static int audioTestVerifyTestToneData(PAUDIOTESTVERIFYJOB pVerJob, PAUDIOTESTOBJINT phTestA, PAUDIOTESTOBJINT phTestB)
{
    int rc;

    /** @todo For now ASSUME that we only have one object per test. */

    AUDIOTESTOBJINT ObjA;
    rc = audioTestObjGetChild(phTestA, 0 /* idxObj */, &ObjA);
    CHECK_RC_MSG_MAYBE_RET(rc, pVerJob, "Unable to get object A");

    rc = audioTestObjOpen(&ObjA);
    CHECK_RC_MSG_MAYBE_RET(rc, pVerJob, "Unable to open object A");

    AUDIOTESTOBJINT ObjB;
    rc = audioTestObjGetChild(phTestB, 0 /* idxObj */, &ObjB);
    CHECK_RC_MSG_MAYBE_RET(rc, pVerJob, "Unable to get object B");

    rc = audioTestObjOpen(&ObjB);
    CHECK_RC_MSG_MAYBE_RET(rc, pVerJob, "Unable to open object B");

    /*
     * Start with most obvious methods first.
     */
    uint64_t cbFileSizeA, cbFileSizeB;
    rc = RTFileQuerySize(ObjA.File.hFile, &cbFileSizeA);
    AssertRCReturn(rc, rc);
    rc = RTFileQuerySize(ObjB.File.hFile, &cbFileSizeB);
    AssertRCReturn(rc, rc);

    if (!cbFileSizeA)
    {
        int rc2 = audioTestErrorDescAddError(pVerJob->pErr, pVerJob->idxTest, "File '%s' is empty", ObjA.szName);
        AssertRC(rc2);
    }

    if (!cbFileSizeB)
    {
        int rc2 = audioTestErrorDescAddError(pVerJob->pErr, pVerJob->idxTest, "File '%s' is empty", ObjB.szName);
        AssertRC(rc2);
    }

    if (cbFileSizeA != cbFileSizeB)
    {
        size_t const cbDiffAbs = cbFileSizeA > cbFileSizeB ? cbFileSizeA - cbFileSizeB : cbFileSizeB - cbFileSizeA;

        int rc2 = audioTestErrorDescAddInfo(pVerJob->pErr, pVerJob->idxTest, "File '%s': %zu bytes (%RU64ms)",
                                            ObjA.szName, cbFileSizeA, PDMAudioPropsBytesToMilli(&pVerJob->PCMProps, cbFileSizeA));
        AssertRC(rc2);
        rc2 = audioTestErrorDescAddInfo(pVerJob->pErr, pVerJob->idxTest, "File '%s': %zu bytes (%RU64ms)",
                                        ObjB.szName, cbFileSizeB, PDMAudioPropsBytesToMilli(&pVerJob->PCMProps, cbFileSizeB));
        AssertRC(rc2);

        uint8_t const uSizeDiffPercentAbs
            = cbFileSizeA > cbFileSizeB ? 100 - ((cbFileSizeB * 100) / cbFileSizeA) : 100 - ((cbFileSizeA * 100) / cbFileSizeB);

        if (uSizeDiffPercentAbs > pVerJob->Opts.uMaxSizePercent)
        {
            rc2 = audioTestErrorDescAddError(pVerJob->pErr, pVerJob->idxTest,
                                             "File '%s' is %RU8%% (%zu bytes, %RU64ms) %s than '%s' (threshold is %RU8%%)",
                                             ObjA.szName,
                                             uSizeDiffPercentAbs,
                                             cbDiffAbs, PDMAudioPropsBytesToMilli(&pVerJob->PCMProps, (uint32_t)cbDiffAbs),
                                             cbFileSizeA > cbFileSizeB ? "bigger" : "smaller",
                                             ObjB.szName, pVerJob->Opts.uMaxSizePercent);
            AssertRC(rc2);
        }
    }

    /* Do normalization first if enabled. */
    if (pVerJob->Opts.fNormalize)
    {
        rc = audioTestObjFileNormalize(pVerJob, &ObjA, &pVerJob->PCMProps);
        CHECK_RC_MSG_MAYBE_RET(rc, pVerJob, "Normalizing object A failed");
        rc = audioTestObjFileNormalize(pVerJob, &ObjB, &pVerJob->PCMProps);
        CHECK_RC_MSG_MAYBE_RET(rc, pVerJob, "Normalizing object B failed");
    }

    /** @todo For now we only support comparison of data which do have identical PCM properties! */

    AUDIOTESTTONEPARMS ToneParmsA;
    RT_ZERO(ToneParmsA);
    ToneParmsA.Props = pVerJob->PCMProps;

    size_t cbSearchWindow = PDMAudioPropsMilliToBytes(&ToneParmsA.Props, pVerJob->Opts.msSearchWindow);

    AUDIOTESTFILECMPPARMS FileA;
    RT_ZERO(FileA);
    FileA.pszName   = ObjA.szName;
    FileA.hFile     = ObjA.File.hFile;
    FileA.offStart  = audioTestToneFileFind(ObjA.File.hFile, true /* fFindSilence */,
                                            0 /* uOff */, cbFileSizeA /* cbMax */, &ToneParmsA, cbSearchWindow);
    FileA.cbSize    = audioTestToneFileFind(ObjA.File.hFile, false /* fFindSilence */,
                                            FileA.offStart /* uOff */, cbFileSizeA - FileA.offStart /* cbMax */, &ToneParmsA, cbSearchWindow);
    AssertReturn(FileA.offStart + FileA.cbSize <= cbFileSizeA, VERR_INTERNAL_ERROR);

    AUDIOTESTTONEPARMS ToneParmsB;
    RT_ZERO(ToneParmsB);
    ToneParmsB.Props = pVerJob->PCMProps;

    AUDIOTESTFILECMPPARMS FileB;
    RT_ZERO(FileB);
    FileB.pszName   = ObjB.szName;
    FileB.hFile     = ObjB.File.hFile;
    FileB.offStart  = audioTestToneFileFind(ObjB.File.hFile, true /* fFindSilence */,
                                            0 /* uOff */, cbFileSizeB /* cbMax */, &ToneParmsB, cbSearchWindow);
    FileB.cbSize    = audioTestToneFileFind(ObjB.File.hFile, false /* fFindSilence */,
                                            FileB.offStart /* uOff */, cbFileSizeB - FileB.offStart /* cbMax */, &ToneParmsB, cbSearchWindow);
    AssertReturn(FileB.offStart + FileB.cbSize <= cbFileSizeB, VERR_INTERNAL_ERROR);

    int rc2;

    uint64_t offBeaconAbs;
    rc = audioTestToneVerifyBeacon(pVerJob, phTestA->enmTestType == AUDIOTESTTYPE_TESTTONE_PLAY /* fIn */,
                                   true /* fPre */, &FileA, &ToneParmsA, &offBeaconAbs);
    if (RT_SUCCESS(rc))
    {
        FileA.offStart = offBeaconAbs;
        FileA.cbSize   = cbFileSizeA - FileA.offStart;
        rc = audioTestToneVerifyBeacon(pVerJob, phTestA->enmTestType == AUDIOTESTTYPE_TESTTONE_PLAY /* fIn */,
                                       false /* fPre */, &FileA, &ToneParmsA, &offBeaconAbs);
        if (RT_SUCCESS(rc))
        {
            /* Adjust the size of the area to compare so that it's within the pre + post beacons. */
            Assert(offBeaconAbs >= FileA.offStart);
            FileA.cbSize = offBeaconAbs - FileA.offStart;
        }
    }

    rc = audioTestToneVerifyBeacon(pVerJob, phTestB->enmTestType == AUDIOTESTTYPE_TESTTONE_RECORD /* fIn */,
                                   true  /* fPre */,  &FileB, &ToneParmsB, &offBeaconAbs);
    if (RT_SUCCESS(rc))
    {
        FileB.offStart = offBeaconAbs;
        FileB.cbSize   = cbFileSizeB - FileB.offStart;
        rc = audioTestToneVerifyBeacon(pVerJob, phTestB->enmTestType == AUDIOTESTTYPE_TESTTONE_RECORD /* fIn */,
                                       false /* fPre */, &FileB, &ToneParmsB, &offBeaconAbs);
        if (RT_SUCCESS(rc))
        {
            /* Adjust the size of the area to compare so that it's within the pre + post beacons. */
            Assert(offBeaconAbs >= FileB.offStart);
            FileB.cbSize = offBeaconAbs - FileB.offStart;
        }
    }

    if (RT_SUCCESS(rc))
    {
        uint32_t const cDiffs = audioTestFilesFindDiffsBinary(pVerJob, &FileA, &FileB, &ToneParmsA);

        if (cDiffs > pVerJob->Opts.cMaxDiff)
        {
            rc2 = audioTestErrorDescAddError(pVerJob->pErr, pVerJob->idxTest,
                                             "Files '%s' and '%s' have too many different chunks (got %RU32, expected %RU32)",
                                             ObjA.szName, ObjB.szName, cDiffs, pVerJob->Opts.cMaxDiff);
            AssertRC(rc2);
        }
    }

    if (AudioTestErrorDescFailed(pVerJob->pErr))
    {
        rc2 = audioTestErrorDescAddInfo(pVerJob->pErr, pVerJob->idxTest, "Files '%s' and '%s' do not match",
                                        ObjA.szName, ObjB.szName);
        AssertRC(rc2);
    }

    rc = audioTestObjClose(&ObjA);
    AssertRCReturn(rc, rc);
    rc = audioTestObjClose(&ObjB);
    AssertRCReturn(rc, rc);

    return rc;
}

/**
 * Verifies a test tone test.
 *
 * @returns VBox status code.
 * @returns Error if the verification failed and test verification job has fKeepGoing not set.
 * @retval  VERR_
 * @param   pVerJob             Verification job to verify test tone for.
 * @param   phTestA             Test handle of test tone A to verify tone B with.
 * @param   phTestB             Test handle of test tone B to verify tone A with.*
 */
static int audioTestVerifyTestTone(PAUDIOTESTVERIFYJOB pVerJob, PAUDIOTESTOBJINT phTestA, PAUDIOTESTOBJINT phTestB)
{
    int rc;

    /*
     * Verify test parameters.
     * More important items have precedence.
     */
    rc = audioTestVerifyValue(pVerJob, phTestA, phTestB, "error_rc", "0", "Test was reported as failed");
    CHECK_RC_MAYBE_RET(rc, pVerJob);
    rc = audioTestVerifyValue(pVerJob, phTestA, phTestB, "obj_count", NULL, "Object counts don't match");
    CHECK_RC_MAYBE_RET(rc, pVerJob);
    rc = audioTestVerifyValue(pVerJob, phTestA, phTestB, "tone_freq_hz", NULL, "Tone frequency doesn't match");
    CHECK_RC_MAYBE_RET(rc, pVerJob);
    rc = audioTestVerifyValue(pVerJob, phTestA, phTestB, "tone_prequel_ms", NULL, "Tone prequel (ms) doesn't match");
    CHECK_RC_MAYBE_RET(rc, pVerJob);
    rc = audioTestVerifyValue(pVerJob, phTestA, phTestB, "tone_duration_ms", NULL, "Tone duration (ms) doesn't match");
    CHECK_RC_MAYBE_RET(rc, pVerJob);
    rc = audioTestVerifyValue(pVerJob, phTestA, phTestB, "tone_sequel_ms", NULL, "Tone sequel (ms) doesn't match");
    CHECK_RC_MAYBE_RET(rc, pVerJob);
    rc = audioTestVerifyValue(pVerJob, phTestA, phTestB, "tone_volume_percent", NULL, "Tone volume (percent) doesn't match");
    CHECK_RC_MAYBE_RET(rc, pVerJob);
    rc = audioTestVerifyValue(pVerJob, phTestA, phTestB, "tone_pcm_hz", NULL, "Tone PCM Hz doesn't match");
    CHECK_RC_MAYBE_RET(rc, pVerJob);
    rc = audioTestVerifyValue(pVerJob, phTestA, phTestB, "tone_pcm_channels", NULL, "Tone PCM channels don't match");
    CHECK_RC_MAYBE_RET(rc, pVerJob);
    rc = audioTestVerifyValue(pVerJob, phTestA, phTestB, "tone_pcm_bits", NULL, "Tone PCM bits don't match");
    CHECK_RC_MAYBE_RET(rc, pVerJob);
    rc = audioTestVerifyValue(pVerJob, phTestA, phTestB, "tone_pcm_is_signed", NULL, "Tone PCM signed bit doesn't match");
    CHECK_RC_MAYBE_RET(rc, pVerJob);

    rc = audioTestObjGetTonePcmProps(phTestA, &pVerJob->PCMProps);
    CHECK_RC_MAYBE_RET(rc, pVerJob);

    /*
     * Now the fun stuff, PCM data analysis.
     */
    rc = audioTestVerifyTestToneData(pVerJob, phTestA, phTestB);
    if (RT_FAILURE(rc))
    {
       int rc2 = audioTestErrorDescAddError(pVerJob->pErr, pVerJob->idxTest, "Verififcation of test tone data failed\n");
       AssertRC(rc2);
    }

    return VINF_SUCCESS;
}

/**
 * Verifies an opened audio test set, extended version.
 *
 * @returns VBox status code.
 * @param   pSetA               Test set A to verify.
 * @param   pSetB               Test set to verify test set A with.
 * @param   pOpts               Verification options to use.
 * @param   pErrDesc            Where to return the test verification errors.
 *
 * @note    Test verification errors have to be checked for errors, regardless of the
 *          actual return code.
 * @note    Uses the standard verification options. Use AudioTestSetVerifyEx() to specify
 *          own options.
 */
int AudioTestSetVerifyEx(PAUDIOTESTSET pSetA, PAUDIOTESTSET pSetB, PAUDIOTESTVERIFYOPTS pOpts, PAUDIOTESTERRORDESC pErrDesc)
{
    AssertPtrReturn(pSetA, VERR_INVALID_POINTER);
    AssertPtrReturn(pSetB, VERR_INVALID_POINTER);
    AssertReturn(audioTestManifestIsOpen(pSetA), VERR_WRONG_ORDER);
    AssertReturn(audioTestManifestIsOpen(pSetB), VERR_WRONG_ORDER);
    AssertPtrReturn(pOpts, VERR_INVALID_POINTER);

    /* We ASSUME the caller has not init'd pErrDesc. */
    audioTestErrorDescInit(pErrDesc);

    AUDIOTESTVERIFYJOB VerJob;
    RT_ZERO(VerJob);
    VerJob.pErr       = pErrDesc;
    VerJob.pSetA      = pSetA;
    VerJob.pSetB      = pSetB;

    memcpy(&VerJob.Opts, pOpts, sizeof(AUDIOTESTVERIFYOPTS));

    PAUDIOTESTVERIFYJOB pVerJob = &VerJob;

    int rc;

    /*
     * Compare obvious values first.
     */
    AUDIOTESTOBJINT hHdrA;
    rc = audioTestSetGetSection(pVerJob->pSetA, AUDIOTEST_SEC_HDR_STR, &hHdrA);
    CHECK_RC_MAYBE_RET(rc, pVerJob);

    AUDIOTESTOBJINT hHdrB;
    rc = audioTestSetGetSection(pVerJob->pSetB, AUDIOTEST_SEC_HDR_STR, &hHdrB);
    CHECK_RC_MAYBE_RET(rc, pVerJob);

    rc = audioTestVerifyValue(&VerJob, &hHdrA, &hHdrB,   "magic",        "vkat_ini",    "Manifest magic wrong");
    CHECK_RC_MAYBE_RET(rc, pVerJob);
    rc = audioTestVerifyValue(&VerJob, &hHdrA, &hHdrB,   "ver",          "1"       ,    "Manifest version wrong");
    CHECK_RC_MAYBE_RET(rc, pVerJob);
    rc = audioTestVerifyValue(&VerJob, &hHdrA, &hHdrB,   "tag",          NULL,          "Manifest tags don't match");
    CHECK_RC_MAYBE_RET(rc, pVerJob);
    rc = audioTestVerifyValue(&VerJob, &hHdrA, &hHdrB,   "test_count",   NULL,          "Test counts don't match");
    CHECK_RC_MAYBE_RET(rc, pVerJob);
    rc = audioTestVerifyValue(&VerJob, &hHdrA, &hHdrB,   "obj_count",    NULL,          "Object counts don't match");
    CHECK_RC_MAYBE_RET(rc, pVerJob);

    /*
     * Compare ran tests.
     */
    uint32_t cTests;
    rc = audioTestObjGetUInt32(&hHdrA, "test_count", &cTests);
    AssertRCReturn(rc, rc);

    for (uint32_t i = 0; i < cTests; i++)
    {
        VerJob.idxTest = i;

        AUDIOTESTOBJINT hTestA;
        rc = audioTestSetGetTest(VerJob.pSetA, i, &hTestA);
        CHECK_RC_MSG_MAYBE_RET(rc, pVerJob, "Test A not found");

        AUDIOTESTOBJINT hTestB;
        rc = audioTestSetGetTest(VerJob.pSetB, i, &hTestB);
        CHECK_RC_MSG_MAYBE_RET(rc, pVerJob, "Test B not found");

        rc = audioTestObjGetUInt32(&hTestA, "test_type", (uint32_t *)&hTestA.enmTestType);
        CHECK_RC_MSG_MAYBE_RET(rc, pVerJob, "Test type A not found");

        rc = audioTestObjGetUInt32(&hTestB, "test_type", (uint32_t *)&hTestB.enmTestType);
        CHECK_RC_MSG_MAYBE_RET(rc, pVerJob, "Test type B not found");

        switch (hTestA.enmTestType)
        {
            case AUDIOTESTTYPE_TESTTONE_PLAY:
            {
                if (hTestB.enmTestType == AUDIOTESTTYPE_TESTTONE_RECORD)
                    rc = audioTestVerifyTestTone(&VerJob, &hTestA, &hTestB);
                else
                    rc = audioTestErrorDescAddError(pErrDesc, i, "Playback test types don't match (set A=%#x, set B=%#x)",
                                                    hTestA.enmTestType, hTestB.enmTestType);
                break;
            }

            case AUDIOTESTTYPE_TESTTONE_RECORD:
            {
                if (hTestB.enmTestType == AUDIOTESTTYPE_TESTTONE_PLAY)
                    rc = audioTestVerifyTestTone(&VerJob, &hTestB, &hTestA);
                else
                    rc = audioTestErrorDescAddError(pErrDesc, i, "Recording test types don't match (set A=%#x, set B=%#x)",
                                                    hTestA.enmTestType, hTestB.enmTestType);
                break;
            }

            case AUDIOTESTTYPE_INVALID:
                rc = VERR_INVALID_PARAMETER;
                break;

            default:
                rc = VERR_NOT_IMPLEMENTED;
                break;
        }

        AssertRC(rc);
    }

    /* Only return critical stuff not related to actual testing here. */
    return VINF_SUCCESS;
}

/**
 * Initializes audio test verification options in a strict manner.
 *
 * @param   pOpts               Verification options to initialize.
 */
void AudioTestSetVerifyOptsInitStrict(PAUDIOTESTVERIFYOPTS pOpts)
{
    RT_BZERO(pOpts, sizeof(AUDIOTESTVERIFYOPTS));

    pOpts->fKeepGoing      = true;
    pOpts->fNormalize      = false; /* Skip normalization by default now, as we now use the OS' master volume to play/record tones. */
    pOpts->cMaxDiff        = 0;     /* By default we're very strict and consider any diff as being erroneous. */
    pOpts->uMaxSizePercent = 10;    /* 10% is okay for us for now; might be due to any buffering / setup phase.
                                       Anything above this is suspicious and should be reported for further investigation. */
    pOpts->msSearchWindow  = 10;    /* We use a search window of 10ms by default for finding (non-)silent parts. */
}

/**
 * Initializes audio test verification options with default values (strict!).
 *
 * @param   pOpts               Verification options to initialize.
 */
void AudioTestSetVerifyOptsInit(PAUDIOTESTVERIFYOPTS pOpts)
{
    AudioTestSetVerifyOptsInitStrict(pOpts);
}

/**
 * Returns whether two audio test verification options are equal.
 *
 * @returns \c true if equal, or \c false if not.
 * @param   pOptsA              Options A to compare.
 * @param   pOptsB              Options B to compare Options A with.
 */
bool AudioTestSetVerifyOptsAreEqual(PAUDIOTESTVERIFYOPTS pOptsA, PAUDIOTESTVERIFYOPTS pOptsB)
{
    if (pOptsA == pOptsB)
        return true;

    return (   pOptsA->cMaxDiff        == pOptsB->cMaxDiff
            && pOptsA->fKeepGoing      == pOptsB->fKeepGoing
            && pOptsA->fNormalize      == pOptsB->fNormalize
            && pOptsA->uMaxDiffPercent == pOptsB->uMaxDiffPercent
            && pOptsA->uMaxSizePercent == pOptsB->uMaxSizePercent
            && pOptsA->msSearchWindow  == pOptsB->msSearchWindow);
}

/**
 * Verifies an opened audio test set.
 *
 * @returns VBox status code.
 * @param   pSetA               Test set A to verify.
 * @param   pSetB               Test set to verify test set A with.
 * @param   pErrDesc            Where to return the test verification errors.
 *
 * @note    Test verification errors have to be checked for errors, regardless of the
 *          actual return code.
 * @note    Uses the standard verification options (strict!).
 *          Use AudioTestSetVerifyEx() to specify own options.
 */
int AudioTestSetVerify(PAUDIOTESTSET pSetA, PAUDIOTESTSET pSetB, PAUDIOTESTERRORDESC pErrDesc)
{
    AUDIOTESTVERIFYOPTS Opts;
    AudioTestSetVerifyOptsInitStrict(&Opts);

    return AudioTestSetVerifyEx(pSetA,pSetB, &Opts, pErrDesc);
}

#undef CHECK_RC_MAYBE_RET
#undef CHECK_RC_MSG_MAYBE_RET

/**
 * Converts an audio test state enum value to a string.
 *
 * @returns Pointer to read-only internal test state string on success,
 *          "illegal" if invalid command value.
 * @param   enmState            The state to convert.
 */
const char *AudioTestStateToStr(AUDIOTESTSTATE enmState)
{
    switch (enmState)
    {
        case AUDIOTESTSTATE_INIT: return "init";
        case AUDIOTESTSTATE_PRE:  return "pre";
        case AUDIOTESTSTATE_RUN:  return "run";
        case AUDIOTESTSTATE_POST: return "post";
        case AUDIOTESTSTATE_DONE: return "done";
        case AUDIOTESTSTATE_32BIT_HACK:
            break;
    }
    AssertMsgFailedReturn(("Invalid test state: #%x\n", enmState), "illegal");
}


/*********************************************************************************************************************************
*   WAVE File Reader.                                                                                                            *
*********************************************************************************************************************************/

/**
 * Counts the number of set bits in @a fMask.
 */
static unsigned audioTestWaveCountBits(uint32_t fMask)
{
    unsigned cBits = 0;
    while (fMask)
    {
        if (fMask & 1)
            cBits++;
        fMask >>= 1;
    }
    return cBits;
}

/**
 * Opens a wave (.WAV) file for reading.
 *
 * @returns VBox status code.
 * @param   pszFile     The file to open.
 * @param   pWaveFile   The open wave file structure to fill in on success.
 * @param   pErrInfo    Where to return addition error details on failure.
 */
int AudioTestWaveFileOpen(const char *pszFile, PAUDIOTESTWAVEFILE pWaveFile, PRTERRINFO pErrInfo)
{
    pWaveFile->u32Magic = AUDIOTESTWAVEFILE_MAGIC_DEAD;
    RT_ZERO(pWaveFile->Props);
    pWaveFile->hFile = NIL_RTFILE;
    int rc = RTFileOpen(&pWaveFile->hFile, pszFile, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE);
    if (RT_FAILURE(rc))
        return RTErrInfoSet(pErrInfo, rc, "RTFileOpen failed");
    uint64_t cbFile = 0;
    rc = RTFileQuerySize(pWaveFile->hFile, &cbFile);
    if (RT_SUCCESS(rc))
    {
        union
        {
            uint8_t                 ab[512];
            struct
            {
                RTRIFFHDR           Hdr;
                union
                {
                    RTRIFFWAVEFMTCHUNK      Fmt;
                    RTRIFFWAVEFMTEXTCHUNK   FmtExt;
                } u;
            } Wave;
            RTRIFFLIST              List;
            RTRIFFCHUNK             Chunk;
            RTRIFFWAVEDATACHUNK     Data;
        } uBuf;

        rc = RTFileRead(pWaveFile->hFile, &uBuf.Wave, sizeof(uBuf.Wave), NULL);
        if (RT_SUCCESS(rc))
        {
            rc = VERR_VFS_UNKNOWN_FORMAT;
            if (   uBuf.Wave.Hdr.uMagic    == RTRIFFHDR_MAGIC
                && uBuf.Wave.Hdr.uFileType == RTRIFF_FILE_TYPE_WAVE
                && uBuf.Wave.u.Fmt.Chunk.uMagic == RTRIFFWAVEFMT_MAGIC
                && uBuf.Wave.u.Fmt.Chunk.cbChunk >= sizeof(uBuf.Wave.u.Fmt.Data))
            {
                if (uBuf.Wave.Hdr.cbFile != cbFile - sizeof(RTRIFFCHUNK))
                    RTErrInfoSetF(pErrInfo, rc, "File size mismatch: %#x, actual %#RX64 (ignored)",
                                  uBuf.Wave.Hdr.cbFile, cbFile - sizeof(RTRIFFCHUNK));
                rc = VERR_VFS_BOGUS_FORMAT;
                if (   uBuf.Wave.u.Fmt.Data.uFormatTag != RTRIFFWAVEFMT_TAG_PCM
                    && uBuf.Wave.u.Fmt.Data.uFormatTag != RTRIFFWAVEFMT_TAG_EXTENSIBLE)
                    RTErrInfoSetF(pErrInfo, rc, "Unsupported uFormatTag value: %#x (expected %#x or %#x)",
                                  uBuf.Wave.u.Fmt.Data.uFormatTag, RTRIFFWAVEFMT_TAG_PCM, RTRIFFWAVEFMT_TAG_EXTENSIBLE);
                else if (   uBuf.Wave.u.Fmt.Data.cBitsPerSample != 8
                         && uBuf.Wave.u.Fmt.Data.cBitsPerSample != 16
                         /* && uBuf.Wave.u.Fmt.Data.cBitsPerSample != 24 - not supported by our stack */
                         && uBuf.Wave.u.Fmt.Data.cBitsPerSample != 32)
                    RTErrInfoSetF(pErrInfo, rc, "Unsupported cBitsPerSample value: %u", uBuf.Wave.u.Fmt.Data.cBitsPerSample);
                else if (   uBuf.Wave.u.Fmt.Data.cChannels < 1
                         || uBuf.Wave.u.Fmt.Data.cChannels >= 16)
                    RTErrInfoSetF(pErrInfo, rc, "Unsupported cChannels value: %u (expected 1..15)", uBuf.Wave.u.Fmt.Data.cChannels);
                else if (   uBuf.Wave.u.Fmt.Data.uHz < 4096
                         || uBuf.Wave.u.Fmt.Data.uHz > 768000)
                    RTErrInfoSetF(pErrInfo, rc, "Unsupported uHz value: %u (expected 4096..768000)", uBuf.Wave.u.Fmt.Data.uHz);
                else if (uBuf.Wave.u.Fmt.Data.cbFrame != uBuf.Wave.u.Fmt.Data.cChannels * uBuf.Wave.u.Fmt.Data.cBitsPerSample / 8)
                    RTErrInfoSetF(pErrInfo, rc, "Invalid cbFrame value: %u (expected %u)", uBuf.Wave.u.Fmt.Data.cbFrame,
                                  uBuf.Wave.u.Fmt.Data.cChannels * uBuf.Wave.u.Fmt.Data.cBitsPerSample / 8);
                else if (uBuf.Wave.u.Fmt.Data.cbRate != uBuf.Wave.u.Fmt.Data.cbFrame * uBuf.Wave.u.Fmt.Data.uHz)
                    RTErrInfoSetF(pErrInfo, rc, "Invalid cbRate value: %u (expected %u)", uBuf.Wave.u.Fmt.Data.cbRate,
                                  uBuf.Wave.u.Fmt.Data.cbFrame * uBuf.Wave.u.Fmt.Data.uHz);
                else if (   uBuf.Wave.u.Fmt.Data.uFormatTag == RTRIFFWAVEFMT_TAG_EXTENSIBLE
                         && uBuf.Wave.u.FmtExt.Data.cbExtra < RTRIFFWAVEFMTEXT_EXTRA_SIZE)
                    RTErrInfoSetF(pErrInfo, rc, "Invalid cbExtra value: %#x (expected at least %#x)",
                                  uBuf.Wave.u.FmtExt.Data.cbExtra, RTRIFFWAVEFMTEXT_EXTRA_SIZE);
                else if (   uBuf.Wave.u.Fmt.Data.uFormatTag == RTRIFFWAVEFMT_TAG_EXTENSIBLE
                         && audioTestWaveCountBits(uBuf.Wave.u.FmtExt.Data.fChannelMask) != uBuf.Wave.u.Fmt.Data.cChannels)
                    RTErrInfoSetF(pErrInfo, rc, "fChannelMask does not match cChannels: %#x (%u bits set) vs %u channels",
                                  uBuf.Wave.u.FmtExt.Data.fChannelMask,
                                  audioTestWaveCountBits(uBuf.Wave.u.FmtExt.Data.fChannelMask), uBuf.Wave.u.Fmt.Data.cChannels);
                else if (uBuf.Wave.u.Fmt.Data.cChannels > PDMAUDIO_MAX_CHANNELS)
                    RTErrInfoSetF(pErrInfo, rc, "More than %u channels are not supported (%u given)",
                                  PDMAUDIO_MAX_CHANNELS, uBuf.Wave.u.Fmt.Data.cChannels);
                else if (   uBuf.Wave.u.Fmt.Data.uFormatTag == RTRIFFWAVEFMT_TAG_EXTENSIBLE
                         && RTUuidCompareStr(&uBuf.Wave.u.FmtExt.Data.SubFormat, RTRIFFWAVEFMTEXT_SUBTYPE_PCM) != 0)
                    RTErrInfoSetF(pErrInfo, rc, "SubFormat is not PCM: %RTuuid (expected %s)",
                                  &uBuf.Wave.u.FmtExt.Data.SubFormat, RTRIFFWAVEFMTEXT_SUBTYPE_PCM);
                else
                {
                    /*
                     * Copy out the data we need from the file format structure.
                     */
                    PDMAudioPropsInit(&pWaveFile->Props, uBuf.Wave.u.Fmt.Data.cBitsPerSample / 8, true /*fSigned*/,
                                      uBuf.Wave.u.Fmt.Data.cChannels, uBuf.Wave.u.Fmt.Data.uHz);
                    pWaveFile->offSamples = sizeof(RTRIFFHDR) + sizeof(RTRIFFCHUNK) + uBuf.Wave.u.Fmt.Chunk.cbChunk;

                    /*
                     * Pick up channel assignments if present.
                     */
                    if (uBuf.Wave.u.Fmt.Data.uFormatTag == RTRIFFWAVEFMT_TAG_EXTENSIBLE)
                    {
                        static unsigned const   s_cStdIds = (unsigned)PDMAUDIOCHANNELID_END_STANDARD
                                                          - (unsigned)PDMAUDIOCHANNELID_FIRST_STANDARD;
                        unsigned                iCh       = 0;
                        for (unsigned idCh = 0; idCh < PDMAUDIO_MAX_CHANNELS && iCh < uBuf.Wave.u.Fmt.Data.cChannels; idCh++)
                            if (uBuf.Wave.u.FmtExt.Data.fChannelMask & RT_BIT_32(idCh))
                            {
                                pWaveFile->Props.aidChannels[iCh] = idCh < s_cStdIds
                                                                  ? idCh + (unsigned)PDMAUDIOCHANNELID_FIRST_STANDARD
                                                                  : (unsigned)PDMAUDIOCHANNELID_UNKNOWN;
                                iCh++;
                            }
                    }

                    /*
                     * Find the 'data' chunk with the audio samples.
                     *
                     * There can be INFO lists both preceding this and succeeding
                     * it, containing IART and other things we can ignored.  Thus
                     * we read a list header here rather than just a chunk header,
                     * since it doesn't matter if we read 4 bytes extra as
                     * AudioTestWaveFileRead uses RTFileReadAt anyway.
                     */
                    rc = RTFileReadAt(pWaveFile->hFile, pWaveFile->offSamples, &uBuf, sizeof(uBuf.List), NULL);
                    for (uint32_t i = 0;
                            i < 128
                         && RT_SUCCESS(rc)
                         && uBuf.Chunk.uMagic != RTRIFFWAVEDATACHUNK_MAGIC
                         && (uint64_t)uBuf.Chunk.cbChunk + sizeof(RTRIFFCHUNK) * 2 <= cbFile - pWaveFile->offSamples;
                         i++)
                    {
                        if (   uBuf.List.uMagic    == RTRIFFLIST_MAGIC
                            && uBuf.List.uListType == RTRIFFLIST_TYPE_INFO)
                        { /*skip*/ }
                        else if (uBuf.Chunk.uMagic == RTRIFFPADCHUNK_MAGIC)
                        { /*skip*/ }
                        else
                            break;
                        pWaveFile->offSamples += sizeof(RTRIFFCHUNK) + uBuf.Chunk.cbChunk;
                        rc = RTFileReadAt(pWaveFile->hFile, pWaveFile->offSamples, &uBuf, sizeof(uBuf.List), NULL);
                    }
                    if (RT_SUCCESS(rc))
                    {
                        pWaveFile->offSamples += sizeof(uBuf.Data.Chunk);
                        pWaveFile->cbSamples   = (uint32_t)cbFile - pWaveFile->offSamples;

                        rc = VERR_VFS_BOGUS_FORMAT;
                        if (   uBuf.Data.Chunk.uMagic == RTRIFFWAVEDATACHUNK_MAGIC
                            && uBuf.Data.Chunk.cbChunk <= pWaveFile->cbSamples
                            && PDMAudioPropsIsSizeAligned(&pWaveFile->Props, uBuf.Data.Chunk.cbChunk))
                        {
                            pWaveFile->cbSamples = uBuf.Data.Chunk.cbChunk;

                            /*
                             * We're good!
                             */
                            pWaveFile->offCur    = 0;
                            pWaveFile->fReadMode = true;
                            pWaveFile->u32Magic  = AUDIOTESTWAVEFILE_MAGIC;
                            return VINF_SUCCESS;
                        }

                        RTErrInfoSetF(pErrInfo, rc, "Bad data header: uMagic=%#x (expected %#x), cbChunk=%#x (max %#RX64, align %u)",
                                      uBuf.Data.Chunk.uMagic, RTRIFFWAVEDATACHUNK_MAGIC,
                                      uBuf.Data.Chunk.cbChunk, pWaveFile->cbSamples, PDMAudioPropsFrameSize(&pWaveFile->Props));
                    }
                    else
                        RTErrInfoSet(pErrInfo, rc, "Failed to read data header");
                }
            }
            else
                RTErrInfoSetF(pErrInfo, rc, "Bad file header: uMagic=%#x (vs. %#x), uFileType=%#x (vs %#x), uFmtMagic=%#x (vs %#x) cbFmtChunk=%#x (min %#x)",
                              uBuf.Wave.Hdr.uMagic, RTRIFFHDR_MAGIC, uBuf.Wave.Hdr.uFileType, RTRIFF_FILE_TYPE_WAVE,
                              uBuf.Wave.u.Fmt.Chunk.uMagic, RTRIFFWAVEFMT_MAGIC,
                              uBuf.Wave.u.Fmt.Chunk.cbChunk, sizeof(uBuf.Wave.u.Fmt.Data));
        }
        else
            rc = RTErrInfoSet(pErrInfo, rc, "Failed to read file header");
    }
    else
        rc = RTErrInfoSet(pErrInfo, rc, "Failed to query file size");

    RTFileClose(pWaveFile->hFile);
    pWaveFile->hFile = NIL_RTFILE;
    return rc;
}


/**
 * Creates a new wave file.
 *
 * @returns VBox status code.
 * @param   pszFile     The filename.
 * @param   pProps      The audio format properties.
 * @param   pWaveFile   The wave file structure to fill in on success.
 * @param   pErrInfo    Where to return addition error details on failure.
 */
int AudioTestWaveFileCreate(const char *pszFile, PCPDMAUDIOPCMPROPS pProps, PAUDIOTESTWAVEFILE pWaveFile, PRTERRINFO pErrInfo)
{
    /*
     * Construct the file header first (we'll do some input validation
     * here, so better do it before creating the file).
     */
    struct
    {
        RTRIFFHDR               Hdr;
        RTRIFFWAVEFMTEXTCHUNK   FmtExt;
        RTRIFFCHUNK             Data;
    } FileHdr;

    FileHdr.Hdr.uMagic                      = RTRIFFHDR_MAGIC;
    FileHdr.Hdr.cbFile                      = 0; /* need to update this later */
    FileHdr.Hdr.uFileType                   = RTRIFF_FILE_TYPE_WAVE;
    FileHdr.FmtExt.Chunk.uMagic             = RTRIFFWAVEFMT_MAGIC;
    FileHdr.FmtExt.Chunk.cbChunk            = sizeof(RTRIFFWAVEFMTEXTCHUNK) - sizeof(RTRIFFCHUNK);
    FileHdr.FmtExt.Data.Core.uFormatTag     = RTRIFFWAVEFMT_TAG_EXTENSIBLE;
    FileHdr.FmtExt.Data.Core.cChannels      = PDMAudioPropsChannels(pProps);
    FileHdr.FmtExt.Data.Core.uHz            = PDMAudioPropsHz(pProps);
    FileHdr.FmtExt.Data.Core.cbRate         = PDMAudioPropsFramesToBytes(pProps, PDMAudioPropsHz(pProps));
    FileHdr.FmtExt.Data.Core.cbFrame        = PDMAudioPropsFrameSize(pProps);
    FileHdr.FmtExt.Data.Core.cBitsPerSample = PDMAudioPropsSampleBits(pProps);
    FileHdr.FmtExt.Data.cbExtra             = sizeof(FileHdr.FmtExt.Data) - sizeof(FileHdr.FmtExt.Data.Core);
    FileHdr.FmtExt.Data.cValidBitsPerSample = PDMAudioPropsSampleBits(pProps);
    FileHdr.FmtExt.Data.fChannelMask        = 0;
    for (uintptr_t idxCh = 0; idxCh < FileHdr.FmtExt.Data.Core.cChannels; idxCh++)
    {
        PDMAUDIOCHANNELID const idCh = (PDMAUDIOCHANNELID)pProps->aidChannels[idxCh];
        if (   idCh >= PDMAUDIOCHANNELID_FIRST_STANDARD
            && idCh <  PDMAUDIOCHANNELID_END_STANDARD)
        {
            if (!(FileHdr.FmtExt.Data.fChannelMask & RT_BIT_32(idCh - PDMAUDIOCHANNELID_FIRST_STANDARD)))
                FileHdr.FmtExt.Data.fChannelMask |= RT_BIT_32(idCh - PDMAUDIOCHANNELID_FIRST_STANDARD);
            else
                return RTErrInfoSetF(pErrInfo, VERR_INVALID_PARAMETER, "Channel #%u repeats channel ID %d", idxCh, idCh);
        }
        else
            return RTErrInfoSetF(pErrInfo, VERR_INVALID_PARAMETER, "Invalid channel ID %d for channel #%u", idCh, idxCh);
    }

    RTUUID UuidTmp;
    int rc = RTUuidFromStr(&UuidTmp, RTRIFFWAVEFMTEXT_SUBTYPE_PCM);
    AssertRCReturn(rc, rc);
    FileHdr.FmtExt.Data.SubFormat = UuidTmp; /* (64-bit field maybe unaligned) */

    FileHdr.Data.uMagic  = RTRIFFWAVEDATACHUNK_MAGIC;
    FileHdr.Data.cbChunk = 0; /* need to update this later */

    /*
     * Create the file and write the header.
     */
    pWaveFile->hFile = NIL_RTFILE;
    rc = RTFileOpen(&pWaveFile->hFile, pszFile, RTFILE_O_CREATE | RTFILE_O_WRITE | RTFILE_O_DENY_WRITE);
    if (RT_FAILURE(rc))
        return RTErrInfoSet(pErrInfo, rc, "RTFileOpen failed");

    rc = RTFileWrite(pWaveFile->hFile, &FileHdr, sizeof(FileHdr), NULL);
    if (RT_SUCCESS(rc))
    {
        /*
         * Initialize the wave file structure.
         */
        pWaveFile->fReadMode       = false;
        pWaveFile->offCur          = 0;
        pWaveFile->offSamples      = 0;
        pWaveFile->cbSamples       = 0;
        pWaveFile->Props           = *pProps;
        pWaveFile->offSamples      = RTFileTell(pWaveFile->hFile);
        if (pWaveFile->offSamples != UINT32_MAX)
        {
            pWaveFile->u32Magic = AUDIOTESTWAVEFILE_MAGIC;
            return VINF_SUCCESS;
        }
        rc = RTErrInfoSet(pErrInfo, VERR_SEEK, "RTFileTell failed");
    }
    else
        RTErrInfoSet(pErrInfo, rc, "RTFileWrite failed writing header");

    RTFileClose(pWaveFile->hFile);
    pWaveFile->hFile    = NIL_RTFILE;
    pWaveFile->u32Magic = AUDIOTESTWAVEFILE_MAGIC_DEAD;

    RTFileDelete(pszFile);
    return rc;
}


/**
 * Closes a wave file.
 */
int AudioTestWaveFileClose(PAUDIOTESTWAVEFILE pWaveFile)
{
    AssertReturn(pWaveFile->u32Magic == AUDIOTESTWAVEFILE_MAGIC, VERR_INVALID_MAGIC);
    int rcRet = VINF_SUCCESS;
    int rc;

    /*
     * Update the size fields if writing.
     */
    if (!pWaveFile->fReadMode)
    {
        uint64_t cbFile = RTFileTell(pWaveFile->hFile);
        if (cbFile != UINT64_MAX)
        {
            uint32_t cbFile32 = cbFile - sizeof(RTRIFFCHUNK);
            rc = RTFileWriteAt(pWaveFile->hFile, RT_OFFSETOF(RTRIFFHDR, cbFile), &cbFile32, sizeof(cbFile32), NULL);
            AssertRCStmt(rc, rcRet = rc);

            uint32_t cbSamples = cbFile - pWaveFile->offSamples;
            rc = RTFileWriteAt(pWaveFile->hFile, pWaveFile->offSamples - sizeof(uint32_t), &cbSamples, sizeof(cbSamples), NULL);
            AssertRCStmt(rc, rcRet = rc);
        }
        else
            rcRet = VERR_SEEK;
    }

    /*
     * Close it.
     */
    rc = RTFileClose(pWaveFile->hFile);
    AssertRCStmt(rc, rcRet = rc);

    pWaveFile->hFile    = NIL_RTFILE;
    pWaveFile->u32Magic = AUDIOTESTWAVEFILE_MAGIC_DEAD;
    return rcRet;
}

/**
 * Reads samples from a wave file.
 *
 * @returns VBox status code.  See RTVfsFileRead for EOF status handling.
 * @param   pWaveFile   The file to read from.
 * @param   pvBuf       Where to put the samples.
 * @param   cbBuf       How much to read at most.
 * @param   pcbRead     Where to return the actual number of bytes read,
 *                      optional.
 */
int AudioTestWaveFileRead(PAUDIOTESTWAVEFILE pWaveFile, void *pvBuf, size_t cbBuf, size_t *pcbRead)
{
    AssertReturn(pWaveFile->u32Magic == AUDIOTESTWAVEFILE_MAGIC, VERR_INVALID_MAGIC);
    AssertReturn(pWaveFile->fReadMode, VERR_ACCESS_DENIED);

    bool fEofAdjusted;
    if (pWaveFile->offCur + cbBuf <= pWaveFile->cbSamples)
        fEofAdjusted = false;
    else if (pcbRead)
    {
        fEofAdjusted = true;
        cbBuf = pWaveFile->cbSamples - pWaveFile->offCur;
    }
    else
        return VERR_EOF;

    int rc = RTFileReadAt(pWaveFile->hFile, pWaveFile->offSamples + pWaveFile->offCur, pvBuf, cbBuf, pcbRead);
    if (RT_SUCCESS(rc))
    {
        if (pcbRead)
        {
            pWaveFile->offCur += (uint32_t)*pcbRead;
            if (fEofAdjusted || cbBuf > *pcbRead)
                rc = VINF_EOF;
            else if (!cbBuf && pWaveFile->offCur == pWaveFile->cbSamples)
                rc = VINF_EOF;
        }
        else
            pWaveFile->offCur += (uint32_t)cbBuf;
    }
    return rc;
}


/**
 * Writes samples to a wave file.
 *
 * @returns VBox status code.
 * @param   pWaveFile   The file to write to.
 * @param   pvBuf       The samples to write.
 * @param   cbBuf       How many bytes of samples to write.
 */
int AudioTestWaveFileWrite(PAUDIOTESTWAVEFILE pWaveFile, const void *pvBuf, size_t cbBuf)
{
    AssertReturn(pWaveFile->u32Magic == AUDIOTESTWAVEFILE_MAGIC, VERR_INVALID_MAGIC);
    AssertReturn(!pWaveFile->fReadMode, VERR_ACCESS_DENIED);

    pWaveFile->cbSamples += (uint32_t)cbBuf;
    return RTFileWrite(pWaveFile->hFile, pvBuf, cbBuf, NULL);
}

