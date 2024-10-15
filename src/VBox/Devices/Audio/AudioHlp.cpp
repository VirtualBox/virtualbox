/* $Id: AudioHlp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Audio helper routines.
 *
 * These are used with both drivers and devices.
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
#include <iprt/assert.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include <iprt/formats/riff.h>

#define LOG_GROUP LOG_GROUP_DRV_AUDIO
#include <VBox/log.h>

#include <VBox/err.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/pdmaudioinline.h>

#include "AudioHlp.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct AUDIOWAVEFILEHDR
{
    RTRIFFHDR               Hdr;
    RTRIFFWAVEFMTEXTCHUNK   FmtExt;
    RTRIFFCHUNK             Data;
} AUDIOWAVEFILEHDR;


#if 0 /* unused, no header prototypes */

/**
 * Retrieves the matching PDMAUDIOFMT for the given bits + signing flag.
 *
 * @return  Matching PDMAUDIOFMT value.
 * @retval  PDMAUDIOFMT_INVALID if unsupported @a cBits value.
 *
 * @param   cBits       The number of bits in the audio format.
 * @param   fSigned     Whether the audio format is signed @c true or not.
 */
PDMAUDIOFMT DrvAudioAudFmtBitsToFormat(uint8_t cBits, bool fSigned)
{
    if (fSigned)
    {
        switch (cBits)
        {
            case 8:  return PDMAUDIOFMT_S8;
            case 16: return PDMAUDIOFMT_S16;
            case 32: return PDMAUDIOFMT_S32;
            default: AssertMsgFailedReturn(("Bogus audio bits %RU8\n", cBits), PDMAUDIOFMT_INVALID);
        }
    }
    else
    {
        switch (cBits)
        {
            case 8:  return PDMAUDIOFMT_U8;
            case 16: return PDMAUDIOFMT_U16;
            case 32: return PDMAUDIOFMT_U32;
            default: AssertMsgFailedReturn(("Bogus audio bits %RU8\n", cBits), PDMAUDIOFMT_INVALID);
        }
    }
}

/**
 * Returns an unique file name for this given audio connector instance.
 *
 * @return  Allocated file name. Must be free'd using RTStrFree().
 * @param   uInstance           Driver / device instance.
 * @param   pszPath             Path name of the file to delete. The path must exist.
 * @param   pszSuffix           File name suffix to use.
 */
char *DrvAudioDbgGetFileNameA(uint8_t uInstance, const char *pszPath, const char *pszSuffix)
{
    char szFileName[64];
    RTStrPrintf(szFileName, sizeof(szFileName), "drvAudio%RU8-%s", uInstance, pszSuffix);

    char szFilePath[RTPATH_MAX];
    int rc2 = RTStrCopy(szFilePath, sizeof(szFilePath), pszPath);
    AssertRC(rc2);
    rc2 = RTPathAppend(szFilePath, sizeof(szFilePath), szFileName);
    AssertRC(rc2);

    return RTStrDup(szFilePath);
}

#endif /* unused */

/**
 * Checks whether a given stream configuration is valid or not.
 *
 * @note    See notes on AudioHlpPcmPropsAreValid().
 *
 * Returns @c true if configuration is valid, @c false if not.
 * @param   pCfg                Stream configuration to check.
 */
bool AudioHlpStreamCfgIsValid(PCPDMAUDIOSTREAMCFG pCfg)
{
    /* Ugly! HDA attach code calls us with uninitialized (all zero) config. */
    if (PDMAudioPropsHz(&pCfg->Props) != 0)
    {
        if (PDMAudioStrmCfgIsValid(pCfg))
        {
            if (   pCfg->enmDir == PDMAUDIODIR_IN
                || pCfg->enmDir == PDMAUDIODIR_OUT)
                return AudioHlpPcmPropsAreValidAndSupported(&pCfg->Props);
        }
    }
    return false;
}

/**
 * Calculates the audio bit rate of the given bits per sample, the Hz and the number
 * of audio channels.
 *
 * Divide the result by 8 to get the byte rate.
 *
 * @returns Bitrate.
 * @param   cBits               Number of bits per sample.
 * @param   uHz                 Hz (Hertz) rate.
 * @param   cChannels           Number of audio channels.
 */
uint32_t AudioHlpCalcBitrate(uint8_t cBits, uint32_t uHz, uint8_t cChannels)
{
    return cBits * uHz * cChannels;
}


/**
 * Checks whether given PCM properties are valid *and* supported by the audio stack or not.
 *
 * @returns @c true if the properties are valid and supported, @c false if not.
 * @param   pProps      The PCM properties to check.
 *
 * @note    Use PDMAudioPropsAreValid() to just check the validation bits.
 */
bool AudioHlpPcmPropsAreValidAndSupported(PCPDMAUDIOPCMPROPS pProps)
{
    AssertPtrReturn(pProps, false);

    if (!PDMAudioPropsAreValid(pProps))
        return false;

    /* Properties seem valid, now check if we actually support those. */
    switch (PDMAudioPropsSampleSize(pProps))
    {
        case 1: /* 8 bit */
            /* Signed / unsigned. */
            break;
        case 2: /* 16 bit */
            /* Signed / unsigned. */
            break;
        /** @todo Do we need support for 24 bit samples? */
        case 4: /* 32 bit */
            /* Signed / unsigned. */
            break;
        case 8: /* 64-bit raw */
            if (   !PDMAudioPropsIsSigned(pProps)
                || !pProps->fRaw)
                return false;
            break;
        default:
            return false;
    }

    if (!pProps->fSwapEndian) /** @todo Handling Big Endian audio data is not supported yet. */
        return true;
    return false;
}


/*********************************************************************************************************************************
*   Audio File Helpers                                                                                                           *
*********************************************************************************************************************************/

/**
 * Constructs an unique file name, based on the given path and the audio file type.
 *
 * @returns VBox status code.
 * @param   pszDst      Where to store the constructed file name.
 * @param   cbDst       Size of the destination buffer (bytes; incl terminator).
 * @param   pszPath     Base path to use.  If NULL or empty, the user's
 *                      temporary directory will be used.
 * @param   pszNameFmt  A name for better identifying the file.
 * @param   va          Arguments for @a pszNameFmt.
 * @param   uInstance   Device / driver instance which is using this file.
 * @param   enmType     Audio file type to construct file name for.
 * @param   fFlags      File naming flags, AUDIOHLPFILENAME_FLAGS_XXX.
 * @param   chTweak     Retry tweak character.
 */
static int audioHlpConstructPathWorker(char *pszDst, size_t cbDst, const char *pszPath, const char *pszNameFmt, va_list va,
                                       uint32_t uInstance, AUDIOHLPFILETYPE enmType, uint32_t fFlags, char chTweak)
{
    /*
     * Validate input.
     */
    AssertPtrNullReturn(pszPath, VERR_INVALID_POINTER);
    AssertPtrReturn(pszNameFmt, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~AUDIOHLPFILENAME_FLAGS_VALID_MASK), VERR_INVALID_FLAGS);

    /* Validate the type and translate it into a suffix. */
    const char *pszSuffix = NULL;
    switch (enmType)
    {
        case AUDIOHLPFILETYPE_RAW: pszSuffix = ".pcm"; break;
        case AUDIOHLPFILETYPE_WAV: pszSuffix = ".wav"; break;
        case AUDIOHLPFILETYPE_INVALID:
        case AUDIOHLPFILETYPE_32BIT_HACK:
            break; /* no default */
    }
    AssertMsgReturn(pszSuffix, ("enmType=%d\n", enmType), VERR_INVALID_PARAMETER);

    /*
     * The directory.  Make sure it exists and ends with a path separator.
     */
    int rc;
    if (!pszPath || !*pszPath)
        rc = RTPathTemp(pszDst, cbDst);
    else
    {
        AssertPtrReturn(pszDst, VERR_INVALID_POINTER);
        rc = RTStrCopy(pszDst, cbDst, pszPath);
    }
    AssertRCReturn(rc, rc);

    if (!RTDirExists(pszDst))
    {
        rc = RTDirCreateFullPath(pszDst, RTFS_UNIX_IRWXU);
        AssertRCReturn(rc, rc);
    }

    size_t offDst = RTPathEnsureTrailingSeparator(pszDst, cbDst);
    AssertReturn(offDst > 0, VERR_BUFFER_OVERFLOW);
    Assert(offDst < cbDst);

    /*
     * The filename.
     */
    /* Start with a ISO timestamp w/ colons replaced by dashes if requested. */
    if (fFlags & AUDIOHLPFILENAME_FLAGS_TS)
    {
        RTTIMESPEC NowTimeSpec;
        RTTIME     NowUtc;
        AssertReturn(RTTimeToString(RTTimeExplode(&NowUtc, RTTimeNow(&NowTimeSpec)), &pszDst[offDst], cbDst - offDst),
                     VERR_BUFFER_OVERFLOW);

        /* Change the two colons in the time part to dashes.  */
        char *pchColon = &pszDst[offDst];
        while ((pchColon = strchr(pchColon, ':')) != NULL)
            *pchColon++ = '-';

        offDst += strlen(&pszDst[offDst]);
        Assert(pszDst[offDst - 1] == 'Z');

        /* Append a dash to separate the timestamp from the name. */
        AssertReturn(offDst + 2 <= cbDst, VERR_BUFFER_OVERFLOW);
        pszDst[offDst++] = '-';
        pszDst[offDst]   = '\0';
    }

    /* Append the filename, instance, retry-tweak and suffix. */
    va_list vaCopy;
    va_copy(vaCopy, va);
    ssize_t cchTail;
    if (chTweak == '\0')
        cchTail = RTStrPrintf2(&pszDst[offDst], cbDst - offDst, "%N-%u%s", pszNameFmt, &vaCopy, uInstance, pszSuffix);
    else
        cchTail = RTStrPrintf2(&pszDst[offDst], cbDst - offDst, "%N-%u%c%s", pszNameFmt, &vaCopy, uInstance, chTweak, pszSuffix);
    va_end(vaCopy);
    AssertReturn(cchTail > 0, VERR_BUFFER_OVERFLOW);

    return VINF_SUCCESS;
}


/**
 * Worker for AudioHlpFileCreateF and AudioHlpFileCreateAndOpenEx that allocates
 * and initializes a AUDIOHLPFILE instance.
 */
static int audioHlpFileCreateWorker(PAUDIOHLPFILE *ppFile, uint32_t fFlags, AUDIOHLPFILETYPE enmType, const char *pszPath)
{
    AssertReturn(!(fFlags & ~AUDIOHLPFILE_FLAGS_VALID_MASK), VERR_INVALID_FLAGS);

    size_t const cbPath = strlen(pszPath) + 1;
    PAUDIOHLPFILE pFile = (PAUDIOHLPFILE)RTMemAllocVar(RT_UOFFSETOF_DYN(AUDIOHLPFILE, szName[cbPath]));
    AssertPtrReturn(pFile, VERR_NO_MEMORY);

    pFile->enmType      = enmType;
    pFile->fFlags       = fFlags;
    pFile->cbWaveData   = 0;
    pFile->hFile        = NIL_RTFILE;
    memcpy(pFile->szName, pszPath, cbPath);

    *ppFile = pFile;
    return VINF_SUCCESS;
}


/**
 * Creates an instance of AUDIOHLPFILE with the given filename and type.
 *
 * @note This does <b>NOT</b> create the file, see AudioHlpFileOpen for that.
 *
 * @returns VBox status code.
 * @param   ppFile      Where to return the pointer to the audio debug file
 *                      instance on success.
 * @param   fFlags      AUDIOHLPFILE_FLAGS_XXX.
 * @param   enmType     The audio file type to produce.
 * @param   pszPath     The directory path.  The temporary directory will be
 *                      used if NULL or empty.
 * @param   fFilename   AUDIOHLPFILENAME_FLAGS_XXX.
 * @param   uInstance   The instance number (will be appended to the filename
 *                      with a dash inbetween).
 * @param   pszNameFmt  The filename format string.
 * @param   ...         Arguments to the filename format string.
 */
int AudioHlpFileCreateF(PAUDIOHLPFILE *ppFile, uint32_t fFlags, AUDIOHLPFILETYPE enmType,
                        const char *pszPath, uint32_t fFilename, uint32_t uInstance, const char *pszNameFmt, ...)
{
    *ppFile = NULL;

    /*
     * Construct the filename first.
     */
    char szPath[RTPATH_MAX];
    va_list va;
    va_start(va, pszNameFmt);
    int rc = audioHlpConstructPathWorker(szPath, sizeof(szPath), pszPath, pszNameFmt, va, uInstance, enmType, fFilename, '\0');
    va_end(va);
    AssertRCReturn(rc, rc);

    /*
     * Allocate and initializes a debug file instance with that filename path.
     */
    return audioHlpFileCreateWorker(ppFile, fFlags, enmType, szPath);
}


/**
 * Destroys a formerly created audio file.
 *
 * @param   pFile               Audio file (object) to destroy.
 */
void AudioHlpFileDestroy(PAUDIOHLPFILE pFile)
{
    if (pFile)
    {
        AudioHlpFileClose(pFile);
        RTMemFree(pFile);
    }
}


/**
 * Opens or creates an audio file.
 *
 * @returns VBox status code.
 * @param   pFile               Pointer to audio file handle to use.
 * @param   fOpen               Open flags.
 *                              Use AUDIOHLPFILE_DEFAULT_OPEN_FLAGS for the default open flags.
 * @param   pProps              PCM properties to use.
 */
int AudioHlpFileOpen(PAUDIOHLPFILE pFile, uint64_t fOpen, PCPDMAUDIOPCMPROPS pProps)
{
    int rc;

    AssertPtrReturn(pFile,   VERR_INVALID_POINTER);
    /** @todo Validate fOpen flags. */
    AssertPtrReturn(pProps,  VERR_INVALID_POINTER);
    Assert(PDMAudioPropsAreValid(pProps));

    /*
     * Raw files just needs to be opened.
     */
    if (pFile->enmType == AUDIOHLPFILETYPE_RAW)
        rc = RTFileOpen(&pFile->hFile, pFile->szName, fOpen);
    /*
     * Wave files needs a header to be constructed and we need to take note of where
     * there are sizes to update later when closing the file.
     */
    else if (pFile->enmType == AUDIOHLPFILETYPE_WAV)
    {
        /* Construct the header. */
        AUDIOWAVEFILEHDR FileHdr;
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
            AssertLogRelMsgReturn(idCh >= PDMAUDIOCHANNELID_FIRST_STANDARD && idCh < PDMAUDIOCHANNELID_END_STANDARD,
                                  ("Invalid channel ID %d for channel #%u", idCh, idxCh), VERR_INVALID_PARAMETER);
            AssertLogRelMsgReturn(!(FileHdr.FmtExt.Data.fChannelMask & RT_BIT_32(idCh - PDMAUDIOCHANNELID_FIRST_STANDARD)),
                                  ("Channel #%u repeats channel ID %d", idxCh, idCh), VERR_INVALID_PARAMETER);
            FileHdr.FmtExt.Data.fChannelMask |= RT_BIT_32(idCh - PDMAUDIOCHANNELID_FIRST_STANDARD);
        }

        RTUUID UuidTmp;
        rc = RTUuidFromStr(&UuidTmp, RTRIFFWAVEFMTEXT_SUBTYPE_PCM);
        AssertRCReturn(rc, rc);
        FileHdr.FmtExt.Data.SubFormat = UuidTmp; /* (64-bit field maybe unaligned) */

        FileHdr.Data.uMagic  = RTRIFFWAVEDATACHUNK_MAGIC;
        FileHdr.Data.cbChunk = 0; /* need to update this later */

        /* Open the file and write out the header. */
        rc = RTFileOpen(&pFile->hFile, pFile->szName, fOpen);
        if (RT_SUCCESS(rc))
        {
            rc = RTFileWrite(pFile->hFile, &FileHdr, sizeof(FileHdr), NULL);
            if (RT_FAILURE(rc))
            {
                RTFileClose(pFile->hFile);
                pFile->hFile = NIL_RTFILE;
            }
        }
    }
    else
        AssertFailedStmt(rc = VERR_INTERNAL_ERROR_3);
    if (RT_SUCCESS(rc))
    {
        pFile->cbWaveData = 0;
        LogRel2(("Audio: Opened file '%s'\n", pFile->szName));
    }
    else
        LogRel(("Audio: Failed opening file '%s': %Rrc\n", pFile->szName, rc));
    return rc;
}


/**
 * Creates a debug file structure and opens a file for it, extended version.
 *
 * @returns VBox status code.
 * @param   ppFile      Where to return the debug file instance on success.
 * @param   enmType     The file type.
 * @param   pszDir      The directory to open the file in.
 * @param   iInstance   The device/driver instance.
 * @param   fFilename   AUDIOHLPFILENAME_FLAGS_XXX.
 * @param   fCreate     AUDIOHLPFILE_FLAGS_XXX.
 * @param   pProps      PCM audio properties for the file.
 * @param   fOpen       RTFILE_O_XXX or AUDIOHLPFILE_DEFAULT_OPEN_FLAGS.
 * @param   pszNameFmt  The base filename.
 * @param   ...         Filename format arguments.
 */
int AudioHlpFileCreateAndOpenEx(PAUDIOHLPFILE *ppFile, AUDIOHLPFILETYPE enmType, const char *pszDir,
                                uint32_t iInstance, uint32_t fFilename, uint32_t fCreate,
                                PCPDMAUDIOPCMPROPS pProps, uint64_t fOpen, const char *pszNameFmt, ...)
{
    *ppFile = NULL;

    for (uint32_t iTry = 0; ; iTry++)
    {
        /* Format the path to the filename. */
        char szFile[RTPATH_MAX];
        va_list va;
        va_start(va, pszNameFmt);
        int rc = audioHlpConstructPathWorker(szFile, sizeof(szFile), pszDir, pszNameFmt, va, iInstance, enmType, fFilename,
                                             iTry == 0 ? '\0' : iTry + 'a');
        va_end(va);
        AssertRCReturn(rc, rc);

        /* Create an debug audio file instance with the filename path. */
        PAUDIOHLPFILE pFile = NULL;
        rc = audioHlpFileCreateWorker(&pFile, fCreate, enmType, szFile);
        AssertRCReturn(rc, rc);

        /* Try open it. */
        rc = AudioHlpFileOpen(pFile, fOpen, pProps);
        if (RT_SUCCESS(rc))
        {
            *ppFile = pFile;
            return rc;
        }
        AudioHlpFileDestroy(pFile);

        AssertReturn(iTry < 16, rc);
    }
}


/**
 * Creates a debug wav-file structure and opens a file for it, default flags.
 *
 * @returns VBox status code.
 * @param   ppFile      Where to return the debug file instance on success.
 * @param   pszDir      The directory to open the file in.
 * @param   pszName     The base filename.
 * @param   iInstance   The device/driver instance.
 * @param   pProps      PCM audio properties for the file.
 */
int AudioHlpFileCreateAndOpen(PAUDIOHLPFILE *ppFile, const char *pszDir, const char *pszName,
                              uint32_t iInstance, PCPDMAUDIOPCMPROPS pProps)
{
    return AudioHlpFileCreateAndOpenEx(ppFile, AUDIOHLPFILETYPE_WAV, pszDir, iInstance,
                                       AUDIOHLPFILENAME_FLAGS_NONE, AUDIOHLPFILE_FLAGS_NONE,
                                       pProps, AUDIOHLPFILE_DEFAULT_OPEN_FLAGS, "%s", pszName);
}


/**
 * Closes an audio file.
 *
 * @returns VBox status code.
 * @param   pFile               Audio file handle to close.
 */
int AudioHlpFileClose(PAUDIOHLPFILE pFile)
{
    if (!pFile || pFile->hFile == NIL_RTFILE)
        return VINF_SUCCESS;

    /*
     * Wave files needs to update the data size and file size in the header.
     */
    if (pFile->enmType == AUDIOHLPFILETYPE_WAV)
    {
        uint32_t const cbFile = sizeof(AUDIOWAVEFILEHDR) - sizeof(RTRIFFCHUNK) + (uint32_t)pFile->cbWaveData;
        uint32_t const cbData = (uint32_t)pFile->cbWaveData;

        int rc2;
        rc2 = RTFileWriteAt(pFile->hFile, RT_UOFFSETOF(AUDIOWAVEFILEHDR, Hdr.cbFile),   &cbFile, sizeof(cbFile), NULL);
        AssertRC(rc2);
        rc2 = RTFileWriteAt(pFile->hFile, RT_UOFFSETOF(AUDIOWAVEFILEHDR, Data.cbChunk), &cbData, sizeof(cbData), NULL);
        AssertRC(rc2);
    }

    /*
     * Do the closing.
     */
    int rc = RTFileClose(pFile->hFile);
    if (RT_SUCCESS(rc) || rc == VERR_INVALID_HANDLE)
        pFile->hFile = NIL_RTFILE;

    if (RT_SUCCESS(rc))
        LogRel2(("Audio: Closed file '%s' (%'RU64 bytes PCM data)\n", pFile->szName, pFile->cbWaveData));
    else
        LogRel(("Audio: Failed closing file '%s': %Rrc\n", pFile->szName, rc));

    /*
     * Delete empty file if requested.
     */
    if (   !(pFile->fFlags & AUDIOHLPFILE_FLAGS_KEEP_IF_EMPTY)
        && pFile->cbWaveData == 0
        && RT_SUCCESS(rc))
        AudioHlpFileDelete(pFile);

    return rc;
}


/**
 * Deletes an audio file.
 *
 * @returns VBox status code.
 * @param   pFile               Audio file to delete.
 */
int AudioHlpFileDelete(PAUDIOHLPFILE pFile)
{
    AssertPtrReturn(pFile, VERR_INVALID_POINTER);

    int rc = RTFileDelete(pFile->szName);
    if (RT_SUCCESS(rc))
        LogRel2(("Audio: Deleted file '%s'\n", pFile->szName));
    else if (rc == VERR_FILE_NOT_FOUND) /* Don't bitch if the file is not around anymore. */
        rc = VINF_SUCCESS;

    if (RT_FAILURE(rc))
        LogRel(("Audio: Failed deleting file '%s', rc=%Rrc\n", pFile->szName, rc));

    return rc;
}


/**
 * Returns whether the given audio file is open and in use or not.
 *
 * @returns True if open, false if not.
 * @param   pFile               Audio file to check open status for.
 */
bool AudioHlpFileIsOpen(PAUDIOHLPFILE pFile)
{
    if (!pFile || pFile->hFile == NIL_RTFILE)
        return false;

    return RTFileIsValid(pFile->hFile);
}


/**
 * Write PCM data to a wave (.WAV) file.
 *
 * @returns VBox status code.
 * @param   pFile               Audio file to write PCM data to.
 * @param   pvBuf               Audio data to write.
 * @param   cbBuf               Size (in bytes) of audio data to write.
 */
int AudioHlpFileWrite(PAUDIOHLPFILE pFile, const void *pvBuf, size_t cbBuf)
{
    AssertPtrReturn(pFile, VERR_INVALID_POINTER);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);

    if (!cbBuf)
        return VINF_SUCCESS;

    int rc = RTFileWrite(pFile->hFile, pvBuf, cbBuf, NULL);
    if (RT_SUCCESS(rc))
        pFile->cbWaveData += cbBuf;

    return rc;
}

