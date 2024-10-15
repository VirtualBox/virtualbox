/* $Id: UnattendedInstaller.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * UnattendedInstaller class and it's descendants implementation
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
#define LOG_GROUP LOG_GROUP_MAIN_UNATTENDED
#include "LoggingNew.h"
#include "VirtualBoxBase.h"
#include "VirtualBoxErrorInfoImpl.h"
#include "AutoCaller.h"
#include <VBox/com/ErrorInfo.h>

#include "UnattendedImpl.h"
#include "UnattendedInstaller.h"
#include "UnattendedScript.h"

#include <VBox/err.h>
#include <iprt/ctype.h>
#include <iprt/fsisomaker.h>
#include <iprt/fsvfs.h>
#include <iprt/getopt.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/rand.h>
#include <iprt/sha.h>
#include <iprt/stream.h>
#include <iprt/vfs.h>
#ifdef RT_OS_SOLARIS
# undef ES /* Workaround for someone dragging the namespace pollutor sys/regset.h. Sigh. */
#endif
#include <iprt/formats/iso9660.h>
#include <iprt/cpp/path.h>


using namespace std;


/* static */ UnattendedInstaller *
UnattendedInstaller::createInstance(VBOXOSTYPE enmDetectedOSType, const Utf8Str &strDetectedOSType,
                                    const Utf8Str &strDetectedOSVersion, const Utf8Str &strDetectedOSFlavor,
                                    const Utf8Str &strDetectedOSHints, Unattended *pParent)
{
    UnattendedInstaller *pUinstaller = NULL;

    if (strDetectedOSType.find("Windows") != RTCString::npos)
    {
        if (enmDetectedOSType >= VBOXOSTYPE_WinVista)
            pUinstaller = new UnattendedWindowsXmlInstaller(pParent);
        else
            pUinstaller = new UnattendedWindowsSifInstaller(pParent);
    }
    else if (enmDetectedOSType >= VBOXOSTYPE_OS2 && enmDetectedOSType < VBOXOSTYPE_Linux)
        pUinstaller = new UnattendedOs2Installer(pParent, strDetectedOSHints);
    else
    {
        if (   enmDetectedOSType >= VBOXOSTYPE_Debian
            && (   enmDetectedOSType <= VBOXOSTYPE_Debian_latest_x64
                || enmDetectedOSType <= VBOXOSTYPE_Debian_latest_arm64))
            pUinstaller = new UnattendedDebianInstaller(pParent);
        else if (   enmDetectedOSType >= VBOXOSTYPE_Ubuntu
                 && (   enmDetectedOSType <= VBOXOSTYPE_Ubuntu_latest_x64
                     || enmDetectedOSType <= VBOXOSTYPE_Ubuntu_latest_arm64))
        {
            /*
             * Here we have to decide, based on the Ubuntu version, which exact installer flavor we have to use:
             * - The preseed installer for older Ubuntu distros, or
             * - The autoinstall installer for newer Ubuntu desktop or Ubuntu server versions.
             */
            if (/* Ubuntu Desktop >= 22.10 switch to the autoinstall installer. */
                   RTStrVersionCompare(strDetectedOSVersion.c_str(), "22.10") >= 0
                /* Ubuntu Server >= 20.04 also uses autoinstall installer. Before that no unattended installation was possible. */
                || (   RTStrVersionCompare(strDetectedOSVersion.c_str(), "20.04") >= 0
                    && strDetectedOSFlavor.contains("Server", RTCString::CaseSensitivity::CaseSensitive))
               )
                pUinstaller = new UnattendedUbuntuAutoInstallInstaller(pParent);
            else
                pUinstaller = new UnattendedUbuntuPreseedInstaller(pParent);
        }
        else if (enmDetectedOSType >= VBOXOSTYPE_RedHat && enmDetectedOSType <= VBOXOSTYPE_RedHat_latest_x64)
        {
            if (RTStrVersionCompare(strDetectedOSVersion.c_str(), "8") >= 0)
                pUinstaller = new UnattendedRhel8Installer(pParent);
            else if (RTStrVersionCompare(strDetectedOSVersion.c_str(), "7") >= 0)
                pUinstaller = new UnattendedRhel7Installer(pParent);
            else if (RTStrVersionCompare(strDetectedOSVersion.c_str(), "6") >= 0)
                pUinstaller = new UnattendedRhel6Installer(pParent);
            else if (RTStrVersionCompare(strDetectedOSVersion.c_str(), "5") >= 0)
                pUinstaller = new UnattendedRhel5Installer(pParent);
            else if (RTStrVersionCompare(strDetectedOSVersion.c_str(), "4") >= 0)
                pUinstaller = new UnattendedRhel4Installer(pParent);
            else if (RTStrVersionCompare(strDetectedOSVersion.c_str(), "3") >= 0)
                pUinstaller = new UnattendedRhel3Installer(pParent);
            else
                pUinstaller = new UnattendedRhel6Installer(pParent);
        }
        else if (enmDetectedOSType >= VBOXOSTYPE_FedoraCore && enmDetectedOSType <= VBOXOSTYPE_FedoraCore_x64)
            pUinstaller = new UnattendedFedoraInstaller(pParent);
        else if (   enmDetectedOSType >= VBOXOSTYPE_Oracle
                 && (   enmDetectedOSType <= VBOXOSTYPE_Oracle_latest_x64
                     || enmDetectedOSType <= VBOXOSTYPE_Oracle_latest_arm64))
        {
            if (RTStrVersionCompare(strDetectedOSVersion.c_str(), "9") >= 0)
                pUinstaller = new UnattendedOracleLinux9Installer(pParent);
            else if (RTStrVersionCompare(strDetectedOSVersion.c_str(), "8") >= 0)
                pUinstaller = new UnattendedOracleLinux8Installer(pParent);
            else if (RTStrVersionCompare(strDetectedOSVersion.c_str(), "7") >= 0)
                pUinstaller = new UnattendedOracleLinux7Installer(pParent);
            else if (RTStrVersionCompare(strDetectedOSVersion.c_str(), "6") >= 0)
                pUinstaller = new UnattendedOracleLinux6Installer(pParent);
            else
                pUinstaller = new UnattendedOracleLinux6Installer(pParent);
        }
        else if (   enmDetectedOSType >= VBOXOSTYPE_FreeBSD
                 && (   enmDetectedOSType <= VBOXOSTYPE_FreeBSD_x64
                     || enmDetectedOSType <= VBOXOSTYPE_FreeBSD_arm64))
            pUinstaller = new UnattendedFreeBsdInstaller(pParent);
#if 0 /* doesn't work, so convert later. */
        else if (enmDetectedOSType == VBOXOSTYPE_OpenSUSE || enmDetectedOSType == VBOXOSTYPE_OpenSUSE_x64)
            pUinstaller = new UnattendedSuseInstaller(new UnattendedSUSEXMLScript(pParent), pParent);
#endif
    }
    RT_NOREF_PV(strDetectedOSFlavor);
    RT_NOREF_PV(strDetectedOSHints);
    return pUinstaller;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////
/*
*
*
*  Implementation Unattended functions
*
*/
//////////////////////////////////////////////////////////////////////////////////////////////////////

/*
 *
 * UnattendedInstaller public methods
 *
 */
UnattendedInstaller::UnattendedInstaller(Unattended *pParent,
                                         const char *pszMainScriptTemplateName, const char *pszPostScriptTemplateName,
                                         const char *pszMainScriptFilename,     const char *pszPostScriptFilename,
                                         DeviceType_T enmBootDevice  /*= DeviceType_DVD */)
    : mMainScript(pParent, pszMainScriptTemplateName, pszMainScriptFilename)
    , mPostScript(pParent, pszPostScriptTemplateName, pszPostScriptFilename)
    , mpParent(pParent)
    , meBootDevice(enmBootDevice)
{
    AssertPtr(pParent);
    Assert(*pszMainScriptTemplateName);
    Assert(*pszMainScriptFilename);
    Assert(*pszPostScriptTemplateName);
    Assert(*pszPostScriptFilename);
    Assert(enmBootDevice == DeviceType_DVD || enmBootDevice == DeviceType_Floppy);
}

UnattendedInstaller::~UnattendedInstaller()
{
    mpParent = NULL;
}

HRESULT UnattendedInstaller::initInstaller()
{
    /*
     * Calculate the full main script template location.
     */
    if (mpParent->i_getScriptTemplatePath().isNotEmpty())
        mStrMainScriptTemplate = mpParent->i_getScriptTemplatePath();
    else
    {
        int vrc = RTPathAppPrivateNoArchCxx(mStrMainScriptTemplate);
        if (RT_SUCCESS(vrc))
            vrc = RTPathAppendCxx(mStrMainScriptTemplate, "UnattendedTemplates");
        if (RT_SUCCESS(vrc))
            vrc = RTPathAppendCxx(mStrMainScriptTemplate, mMainScript.getDefaultTemplateFilename());
        if (RT_FAILURE(vrc))
            return mpParent->setErrorBoth(E_FAIL, vrc,
                                          tr("Failed to construct path to the unattended installer script templates (%Rrc)"),
                                          vrc);
    }

    /*
     * Calculate the full post script template location.
     */
    if (mpParent->i_getPostInstallScriptTemplatePath().isNotEmpty())
        mStrPostScriptTemplate = mpParent->i_getPostInstallScriptTemplatePath();
    else
    {
        int vrc = RTPathAppPrivateNoArchCxx(mStrPostScriptTemplate);
        if (RT_SUCCESS(vrc))
            vrc = RTPathAppendCxx(mStrPostScriptTemplate, "UnattendedTemplates");
        if (RT_SUCCESS(vrc))
            vrc = RTPathAppendCxx(mStrPostScriptTemplate, mPostScript.getDefaultTemplateFilename());
        if (RT_FAILURE(vrc))
            return mpParent->setErrorBoth(E_FAIL, vrc,
                                          tr("Failed to construct path to the unattended installer script templates (%Rrc)"),
                                          vrc);
    }

    /*
     * Construct paths we need.
     */
    if (isAuxiliaryFloppyNeeded())
    {
        mStrAuxiliaryFloppyFilePath = mpParent->i_getAuxiliaryBasePath();
        mStrAuxiliaryFloppyFilePath.append("aux-floppy.img");
    }
    if (isAuxiliaryIsoNeeded())
    {
        mStrAuxiliaryIsoFilePath = mpParent->i_getAuxiliaryBasePath();
        if (!isAuxiliaryIsoIsVISO())
            mStrAuxiliaryIsoFilePath.append("aux-iso.iso");
        else
            mStrAuxiliaryIsoFilePath.append("aux-iso.viso");
    }

    /*
     * Check that we've got the minimum of data available.
     */
    if (mpParent->i_getIsoPath().isEmpty())
        return mpParent->setError(E_INVALIDARG, tr("Cannot proceed with an empty installation ISO path"));
    if (mpParent->i_getUser().isEmpty())
        return mpParent->setError(E_INVALIDARG, tr("Empty user name is not allowed"));
    if (mpParent->i_getUserPassword().isEmpty())
        return mpParent->setError(E_INVALIDARG, tr("Empty user password is not allowed"));
    /* If admin password is empty, the user password will be used instead. */

    LogRelFunc(("UnattendedInstaller::savePassedData(): \n"));
    return S_OK;
}

#if 0  /* Always in AUX ISO */
bool UnattendedInstaller::isAdditionsIsoNeeded() const
{
    /* In the VISO case, we'll add the additions to the VISO in a subdir. */
    return !isAuxiliaryIsoIsVISO() && mpParent->i_getInstallGuestAdditions();
}

bool UnattendedInstaller::isValidationKitIsoNeeded() const
{
    /* In the VISO case, we'll add the validation kit to the VISO in a subdir. */
    return !isAuxiliaryIsoIsVISO() && mpParent->i_getInstallTestExecService();
}
#endif

bool UnattendedInstaller::isAuxiliaryIsoNeeded() const
{
    /* In the VISO case we use the AUX ISO for GAs, TXS, and User Payloads. */
    return isAuxiliaryIsoIsVISO()
        && (   mpParent->i_getInstallGuestAdditions()
            || mpParent->i_getInstallTestExecService()
            || mpParent->i_getInstallUserPayload());
}


HRESULT UnattendedInstaller::prepareUnattendedScripts()
{
    LogFlow(("UnattendedInstaller::prepareUnattendedScripts()\n"));

    /*
     * The script template editor calls setError, so status codes just needs to
     * be passed on to the caller.  Do the same for both scripts.
     */
    HRESULT hrc = mMainScript.read(getTemplateFilePath());
    if (SUCCEEDED(hrc))
    {
        hrc = mMainScript.parse();
        if (SUCCEEDED(hrc))
        {
            /* Ditto for the post script. */
            hrc = mPostScript.read(getPostTemplateFilePath());
            if (SUCCEEDED(hrc))
            {
                hrc = mPostScript.parse();
                if (SUCCEEDED(hrc))
                {
                    LogFlow(("UnattendedInstaller::prepareUnattendedScripts: returns S_OK\n"));
                    return S_OK;
                }
                LogFlow(("UnattendedInstaller::prepareUnattendedScripts: parse failed on post script (%Rhrc)\n", hrc));
            }
            else
                LogFlow(("UnattendedInstaller::prepareUnattendedScripts: error reading post install script template file (%Rhrc)\n", hrc));
        }
        else
            LogFlow(("UnattendedInstaller::prepareUnattendedScripts: parse failed (%Rhrc)\n", hrc));
    }
    else
        LogFlow(("UnattendedInstaller::prepareUnattendedScripts: error reading installation script template file (%Rhrc)\n", hrc));
    return hrc;
}

HRESULT UnattendedInstaller::prepareMedia(bool fOverwrite /*=true*/)
{
    LogRelFlow(("UnattendedInstaller::prepareMedia:\n"));
    HRESULT hrc = S_OK;
    if (isAuxiliaryFloppyNeeded())
        hrc = prepareAuxFloppyImage(fOverwrite);
    if (SUCCEEDED(hrc))
    {
        if (isAuxiliaryIsoNeeded())
        {
            hrc = prepareAuxIsoImage(fOverwrite);
            if (FAILED(hrc))
            {
                LogRelFlow(("UnattendedInstaller::prepareMedia: prepareAuxIsoImage failed\n"));

                /* Delete the floppy image if we created one.  */
                if (isAuxiliaryFloppyNeeded())
                    RTFileDelete(getAuxiliaryFloppyFilePath().c_str());
            }
        }
    }
    LogRelFlow(("UnattendedInstaller::prepareMedia: returns %Rrc\n", hrc));
    return hrc;
}

/*
 *
 * UnattendedInstaller protected methods
 *
 */
HRESULT UnattendedInstaller::prepareAuxFloppyImage(bool fOverwrite)
{
    Assert(isAuxiliaryFloppyNeeded());

    /*
     * Create the image.
     */
    RTVFSFILE hVfsFile;
    HRESULT hrc = newAuxFloppyImage(getAuxiliaryFloppyFilePath().c_str(), fOverwrite, &hVfsFile);
    if (SUCCEEDED(hrc))
    {
        /*
         * Open the FAT file system so we can copy files onto the floppy.
         */
        RTERRINFOSTATIC ErrInfo;
        RTVFS           hVfs;
        int vrc = RTFsFatVolOpen(hVfsFile, false /*fReadOnly*/,  0 /*offBootSector*/, &hVfs, RTErrInfoInitStatic(&ErrInfo));
        RTVfsFileRelease(hVfsFile);
        if (RT_SUCCESS(vrc))
        {
            /*
             * Call overridable method to copies the files onto it.
             */
            hrc = copyFilesToAuxFloppyImage(hVfs);

            /*
             * Release the VFS.  On failure, delete the floppy image so the operation can
             * be repeated in non-overwrite mode and so that we don't leave any mess behind.
             */
            RTVfsRelease(hVfs);
        }
        else if (RTErrInfoIsSet(&ErrInfo.Core))
            hrc = mpParent->setErrorBoth(E_FAIL, vrc,
                                         tr("Failed to open FAT file system on newly created floppy image '%s': %Rrc: %s"),
                                         getAuxiliaryFloppyFilePath().c_str(), vrc, ErrInfo.Core.pszMsg);
        else
            hrc = mpParent->setErrorBoth(E_FAIL, vrc,
                                         tr("Failed to open FAT file system onnewly created floppy image '%s': %Rrc"),
                                         getAuxiliaryFloppyFilePath().c_str(), vrc);
        if (FAILED(hrc))
            RTFileDelete(getAuxiliaryFloppyFilePath().c_str());
    }
    return hrc;
}

HRESULT UnattendedInstaller::newAuxFloppyImage(const char *pszFilename, bool fOverwrite, PRTVFSFILE phVfsFile)
{
    /*
     * Open the image file.
     */
    HRESULT     hrc;
    RTVFSFILE   hVfsFile;
    uint64_t    fOpen = RTFILE_O_READWRITE | RTFILE_O_DENY_ALL | (0660 << RTFILE_O_CREATE_MODE_SHIFT);
    if (fOverwrite)
        fOpen |= RTFILE_O_CREATE_REPLACE;
    else
        fOpen |= RTFILE_O_OPEN;
    int vrc = RTVfsFileOpenNormal(pszFilename, fOpen, &hVfsFile);
    if (RT_SUCCESS(vrc))
    {
        /*
         * Format it.
         */
        vrc = RTFsFatVolFormat144(hVfsFile, false /*fQuick*/);
        if (RT_SUCCESS(vrc))
        {
            *phVfsFile = hVfsFile;
            LogRelFlow(("UnattendedInstaller::newAuxFloppyImage: created and formatted  '%s'\n", pszFilename));
            return S_OK;
        }

        hrc = mpParent->setErrorBoth(E_FAIL, vrc, tr("Failed to format floppy image '%s': %Rrc"), pszFilename, vrc);
        RTVfsFileRelease(hVfsFile);
        RTFileDelete(pszFilename);
    }
    else
        hrc = mpParent->setErrorBoth(E_FAIL, vrc, tr("Failed to create floppy image '%s': %Rrc"), pszFilename, vrc);
    return hrc;
}

HRESULT UnattendedInstaller::copyFilesToAuxFloppyImage(RTVFS hVfs)
{
    HRESULT hrc = addScriptToFloppyImage(&mMainScript, hVfs);
    if (SUCCEEDED(hrc))
        hrc = addScriptToFloppyImage(&mPostScript, hVfs);
    return hrc;
}

HRESULT UnattendedInstaller::addScriptToFloppyImage(BaseTextScript *pEditor, RTVFS hVfs)
{
    /*
     * Open the destination file.
     */
    HRESULT   hrc;
    RTVFSFILE hVfsFileDst;
    int vrc = RTVfsFileOpen(hVfs, pEditor->getDefaultFilename(),
                            RTFILE_O_WRITE | RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_ALL
                            | (0660 << RTFILE_O_CREATE_MODE_SHIFT),
                            &hVfsFileDst);
    if (RT_SUCCESS(vrc))
    {
        /*
         * Save the content to a string.
         */
        Utf8Str strScript;
        hrc = pEditor->saveToString(strScript);
        if (SUCCEEDED(hrc))
        {
            /*
             * Write the string.
             */
            vrc = RTVfsFileWrite(hVfsFileDst, strScript.c_str(), strScript.length(), NULL);
            if (RT_SUCCESS(vrc))
                hrc = S_OK; /* done */
            else
                hrc = mpParent->setErrorBoth(E_FAIL, vrc,
                                             tr("Error writing %zu bytes to '%s' in floppy image '%s': %Rrc",
                                                "", strScript.length()),
                                             strScript.length(), pEditor->getDefaultFilename(),
                                             getAuxiliaryFloppyFilePath().c_str());
        }
        RTVfsFileRelease(hVfsFileDst);
    }
    else
        hrc = mpParent->setErrorBoth(E_FAIL, vrc,
                                     tr("Error creating '%s' in floppy image '%s': %Rrc"),
                                     pEditor->getDefaultFilename(), getAuxiliaryFloppyFilePath().c_str());
    return hrc;
}

HRESULT UnattendedInstaller::addFileToFloppyImage(RTVFS hVfs, const char *pszSrc, const char *pszDst)
{
    HRESULT hrc;

    /*
     * Open the source file.
     */
    RTVFSIOSTREAM hVfsIosSrc;
    int vrc = RTVfsIoStrmOpenNormal(pszSrc, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE, &hVfsIosSrc);
    if (RT_SUCCESS(vrc))
    {
        /*
         * Open the destination file.
         */
        RTVFSFILE hVfsFileDst;
        vrc = RTVfsFileOpen(hVfs, pszDst,
                            RTFILE_O_WRITE | RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_ALL | (0660 << RTFILE_O_CREATE_MODE_SHIFT),
                            &hVfsFileDst);
        if (RT_SUCCESS(vrc))
        {
            /*
             * Do the copying.
             */
            RTVFSIOSTREAM hVfsIosDst = RTVfsFileToIoStream(hVfsFileDst);
            vrc = RTVfsUtilPumpIoStreams(hVfsIosSrc, hVfsIosDst, 0);
            if (RT_SUCCESS(vrc))
                hrc = S_OK;
            else
                hrc = mpParent->setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Error writing copying '%s' to floppy image '%s': %Rrc"),
                                             pszSrc, getAuxiliaryFloppyFilePath().c_str(), vrc);
            RTVfsIoStrmRelease(hVfsIosDst);
            RTVfsFileRelease(hVfsFileDst);
        }
        else
            hrc = mpParent->setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Error opening '%s' on floppy image '%s' for writing: %Rrc"),
                                         pszDst, getAuxiliaryFloppyFilePath().c_str(), vrc);

        RTVfsIoStrmRelease(hVfsIosSrc);
    }
    else
        hrc = mpParent->setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Error opening '%s' for copying onto floppy image '%s': %Rrc"),
                                     pszSrc, getAuxiliaryFloppyFilePath().c_str(), vrc);
    return hrc;
}

HRESULT UnattendedInstaller::prepareAuxIsoImage(bool fOverwrite)
{
    /*
     * Open the original installation ISO.
     */
    RTVFS   hVfsOrgIso;
    HRESULT hrc = openInstallIsoImage(&hVfsOrgIso);
    if (SUCCEEDED(hrc))
    {
        /*
         * The next steps depends on the kind of image we're making.
         */
        if (!isAuxiliaryIsoIsVISO())
        {
            RTFSISOMAKER hIsoMaker;
            hrc = newAuxIsoImageMaker(&hIsoMaker);
            if (SUCCEEDED(hrc))
            {
                hrc = addFilesToAuxIsoImageMaker(hIsoMaker, hVfsOrgIso);
                if (SUCCEEDED(hrc))
                    hrc = finalizeAuxIsoImage(hIsoMaker, getAuxiliaryIsoFilePath().c_str(), fOverwrite);
                RTFsIsoMakerRelease(hIsoMaker);
            }
        }
        else
        {
            RTCList<RTCString> vecFiles(0);
            RTCList<RTCString> vecArgs(0);
            try
            {
                vecArgs.append() = "--iprt-iso-maker-file-marker-bourne-sh";
                RTUUID Uuid;
                int vrc = RTUuidCreate(&Uuid); AssertRC(vrc);
                char szTmp[RTUUID_STR_LENGTH + 1];
                vrc = RTUuidToStr(&Uuid, szTmp, sizeof(szTmp)); AssertRC(vrc);
                vecArgs.append() = szTmp;
                vecArgs.append() = "--file-mode=0444";
                vecArgs.append() = "--dir-mode=0555";
            }
            catch (std::bad_alloc &)
            {
                hrc = E_OUTOFMEMORY;
            }
            if (SUCCEEDED(hrc))
            {
                hrc = addFilesToAuxVisoVectors(vecArgs, vecFiles, hVfsOrgIso, fOverwrite);
                if (SUCCEEDED(hrc))
                    hrc = finalizeAuxVisoFile(vecArgs, getAuxiliaryIsoFilePath().c_str(), fOverwrite);

                if (FAILED(hrc))
                    for (size_t i = 0; i < vecFiles.size(); i++)
                        RTFileDelete(vecFiles[i].c_str());
            }
        }
        RTVfsRelease(hVfsOrgIso);
    }
    return hrc;
}

HRESULT UnattendedInstaller::openInstallIsoImage(PRTVFS phVfsIso, uint32_t fFlags /*= 0*/)
{
    /* Open the file. */
    const char *pszIsoPath = mpParent->i_getIsoPath().c_str();
    RTVFSFILE hOrgIsoFile;
    int vrc = RTVfsFileOpenNormal(pszIsoPath, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE, &hOrgIsoFile);
    if (RT_FAILURE(vrc))
        return mpParent->setErrorBoth(E_FAIL, vrc, tr("Failed to open ISO image '%s' (%Rrc)"), pszIsoPath, vrc);

    /* Pass the file to the ISO file system interpreter. */
    RTERRINFOSTATIC ErrInfo;
    vrc = RTFsIso9660VolOpen(hOrgIsoFile, fFlags, phVfsIso, RTErrInfoInitStatic(&ErrInfo));
    RTVfsFileRelease(hOrgIsoFile);
    if (RT_SUCCESS(vrc))
        return S_OK;
    if (RTErrInfoIsSet(&ErrInfo.Core))
        return mpParent->setErrorBoth(E_FAIL, vrc, tr("ISO reader fail to open '%s' (%Rrc): %s"),
                                      pszIsoPath, vrc, ErrInfo.Core.pszMsg);
    return mpParent->setErrorBoth(E_FAIL, vrc, tr("ISO reader fail to open '%s' (%Rrc)"), pszIsoPath, vrc);
}

HRESULT UnattendedInstaller::newAuxIsoImageMaker(PRTFSISOMAKER phIsoMaker)
{
    int vrc = RTFsIsoMakerCreate(phIsoMaker);
    if (RT_SUCCESS(vrc))
        return S_OK;
    return mpParent->setErrorBoth(E_FAIL, vrc, tr("RTFsIsoMakerCreate failed (%Rrc)"), vrc);
}

HRESULT UnattendedInstaller::addFilesToAuxIsoImageMaker(RTFSISOMAKER hIsoMaker, RTVFS hVfsOrgIso)
{
    RT_NOREF(hVfsOrgIso);

    /*
     * Add the two scripts to the image with default names.
     */
    HRESULT hrc = addScriptToIsoMaker(&mMainScript, hIsoMaker);
    if (SUCCEEDED(hrc))
        hrc = addScriptToIsoMaker(&mPostScript, hIsoMaker);
    return hrc;
}

HRESULT UnattendedInstaller::addScriptToIsoMaker(BaseTextScript *pEditor, RTFSISOMAKER hIsoMaker,
                                                 const char *pszDstFilename /*= NULL*/)
{
    /*
     * Calc default destination filename if desired.
     */
    RTCString strDstNameBuf;
    if (!pszDstFilename)
    {
        try
        {
            strDstNameBuf = RTPATH_SLASH_STR;
            strDstNameBuf.append(pEditor->getDefaultTemplateFilename());
            pszDstFilename = strDstNameBuf.c_str();
        }
        catch (std::bad_alloc &)
        {
            return E_OUTOFMEMORY;
        }
    }

    /*
     * Create a memory file for the script.
     */
    Utf8Str strScript;
    HRESULT hrc = pEditor->saveToString(strScript);
    if (SUCCEEDED(hrc))
    {
        RTVFSFILE hVfsScriptFile;
        size_t    cchScript = strScript.length();
        int vrc = RTVfsFileFromBuffer(RTFILE_O_READ, strScript.c_str(), strScript.length(), &hVfsScriptFile);
        strScript.setNull();
        if (RT_SUCCESS(vrc))
        {
            /*
             * Add it to the ISO.
             */
            vrc = RTFsIsoMakerAddFileWithVfsFile(hIsoMaker, pszDstFilename, hVfsScriptFile, NULL);
            RTVfsFileRelease(hVfsScriptFile);
            if (RT_SUCCESS(vrc))
                hrc = S_OK;
            else
                hrc = mpParent->setErrorBoth(E_FAIL, vrc,
                                             tr("RTFsIsoMakerAddFileWithVfsFile failed on the script '%s' (%Rrc)"),
                                             pszDstFilename, vrc);
        }
        else
            hrc = mpParent->setErrorBoth(E_FAIL, vrc,
                                         tr("RTVfsFileFromBuffer failed on the %zu byte script '%s' (%Rrc)", "", cchScript),
                                         cchScript, pszDstFilename, vrc);
    }
    return hrc;
}

HRESULT UnattendedInstaller::finalizeAuxIsoImage(RTFSISOMAKER hIsoMaker, const char *pszFilename, bool fOverwrite)
{
    /*
     * Finalize the image.
     */
    int vrc = RTFsIsoMakerFinalize(hIsoMaker);
    if (RT_FAILURE(vrc))
        return mpParent->setErrorBoth(E_FAIL, vrc, tr("RTFsIsoMakerFinalize failed (%Rrc)"), vrc);

    /*
     * Open the destination file.
     */
    uint64_t fOpen = RTFILE_O_WRITE | RTFILE_O_DENY_ALL;
    if (fOverwrite)
        fOpen |= RTFILE_O_CREATE_REPLACE;
    else
        fOpen |= RTFILE_O_CREATE;
    RTVFSFILE hVfsDstFile;
    vrc = RTVfsFileOpenNormal(pszFilename, fOpen, &hVfsDstFile);
    if (RT_FAILURE(vrc))
    {
        if (vrc == VERR_ALREADY_EXISTS)
            return mpParent->setErrorBoth(E_FAIL, vrc, tr("The auxiliary ISO image file '%s' already exists"),
                                          pszFilename);
        return mpParent->setErrorBoth(E_FAIL, vrc, tr("Failed to open the auxiliary ISO image file '%s' for writing (%Rrc)"),
                                      pszFilename, vrc);
    }

    /*
     * Get the source file from the image maker.
     */
    HRESULT   hrc;
    RTVFSFILE hVfsSrcFile;
    vrc = RTFsIsoMakerCreateVfsOutputFile(hIsoMaker, &hVfsSrcFile);
    if (RT_SUCCESS(vrc))
    {
        RTVFSIOSTREAM hVfsSrcIso = RTVfsFileToIoStream(hVfsSrcFile);
        RTVFSIOSTREAM hVfsDstIso = RTVfsFileToIoStream(hVfsDstFile);
        if (   hVfsSrcIso != NIL_RTVFSIOSTREAM
            && hVfsDstIso != NIL_RTVFSIOSTREAM)
        {
            vrc = RTVfsUtilPumpIoStreams(hVfsSrcIso, hVfsDstIso, 0 /*cbBufHint*/);
            if (RT_SUCCESS(vrc))
                hrc = S_OK;
            else
                hrc = mpParent->setErrorBoth(E_FAIL, vrc, tr("Error writing auxiliary ISO image '%s' (%Rrc)"),
                                             pszFilename, vrc);
        }
        else
            hrc = mpParent->setErrorBoth(E_FAIL, VERR_INTERNAL_ERROR_2,
                                         tr("Internal Error: Failed to case VFS file to VFS I/O stream"));
        RTVfsIoStrmRelease(hVfsSrcIso);
        RTVfsIoStrmRelease(hVfsDstIso);
    }
    else
        hrc = mpParent->setErrorBoth(E_FAIL, vrc, tr("RTFsIsoMakerCreateVfsOutputFile failed (%Rrc)"), vrc);
    RTVfsFileRelease(hVfsSrcFile);
    RTVfsFileRelease(hVfsDstFile);
    if (FAILED(hrc))
        RTFileDelete(pszFilename);
    return hrc;
}

HRESULT UnattendedInstaller::addFilesToAuxVisoVectors(RTCList<RTCString> &rVecArgs, RTCList<RTCString> &rVecFiles,
                                                      RTVFS hVfsOrgIso, bool fOverwrite)
{
    RT_NOREF(hVfsOrgIso);

    /*
     * Save and add the scripts.
     */
    HRESULT hrc = addScriptToVisoVectors(&mMainScript, rVecArgs, rVecFiles, fOverwrite);
    if (SUCCEEDED(hrc))
        hrc = addScriptToVisoVectors(&mPostScript, rVecArgs, rVecFiles, fOverwrite);
    if (SUCCEEDED(hrc))
    {
        try
        {
            /*
             * If we've got a Guest Additions ISO, add its content to a /vboxadditions dir.
             */
            if (mpParent->i_getInstallGuestAdditions())
            {
                rVecArgs.append().append("--push-iso=").append(mpParent->i_getAdditionsIsoPath());
                rVecArgs.append() = "/vboxadditions=/";
                rVecArgs.append() = "--pop";
            }

            /*
             * If we've got a Validation Kit ISO, add its content to a /vboxvalidationkit dir.
             */
            if (mpParent->i_getInstallTestExecService())
            {
                rVecArgs.append().append("--push-iso=").append(mpParent->i_getValidationKitIsoPath());
                rVecArgs.append() = "/vboxvalidationkit=/";
                rVecArgs.append() = "--pop";
            }

            /*
             * If we've got a User Payload ISO, add its content to a /vboxuserpayload dir.
             */
            if (mpParent->i_getInstallUserPayload())
            {
                rVecArgs.append().append("--push-iso=").append(mpParent->i_getUserPayloadIsoPath());
                rVecArgs.append() = "/vboxuserpayload=/";
                rVecArgs.append() = "--pop";
            }
        }
        catch (std::bad_alloc &)
        {
            hrc = E_OUTOFMEMORY;
        }
    }
    return hrc;
}

HRESULT UnattendedInstaller::addScriptToVisoVectors(BaseTextScript *pEditor, RTCList<RTCString> &rVecArgs,
                                                    RTCList<RTCString> &rVecFiles, bool fOverwrite)
{
    /*
     * Calc the aux script file name.
     */
    RTCString strScriptName;
    try
    {
        strScriptName = mpParent->i_getAuxiliaryBasePath();
        strScriptName.append(pEditor->getDefaultFilename());
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    /*
     * Save it.
     */
    HRESULT hrc = pEditor->save(strScriptName.c_str(), fOverwrite);
    if (SUCCEEDED(hrc))
    {
        /*
         * Add it to the vectors.
         */
        try
        {
            rVecArgs.append().append('/').append(pEditor->getDefaultFilename()).append('=').append(strScriptName);
            rVecFiles.append(strScriptName);
        }
        catch (std::bad_alloc &)
        {
            RTFileDelete(strScriptName.c_str());
            hrc = E_OUTOFMEMORY;
        }
    }
    return hrc;
}

HRESULT UnattendedInstaller::finalizeAuxVisoFile(RTCList<RTCString> const &rVecArgs, const char *pszFilename, bool fOverwrite)
{
    /*
     * Create a C-style argument vector and turn that into a command line string.
     */
    size_t const cArgs     = rVecArgs.size();
    const char **papszArgs = (const char **)RTMemTmpAlloc((cArgs + 1) * sizeof(const char *));
    if (!papszArgs)
        return E_OUTOFMEMORY;
    for (size_t i = 0; i < cArgs; i++)
        papszArgs[i] = rVecArgs[i].c_str();
    papszArgs[cArgs] = NULL;

    char *pszCmdLine;
    int vrc = RTGetOptArgvToString(&pszCmdLine, papszArgs, RTGETOPTARGV_CNV_QUOTE_BOURNE_SH);
    RTMemTmpFree(papszArgs);
    if (RT_FAILURE(vrc))
        return mpParent->setErrorBoth(E_FAIL, vrc, tr("RTGetOptArgvToString failed (%Rrc)"), vrc);

    /*
     * Open the file.
     */
    HRESULT  hrc;
    uint64_t fOpen = RTFILE_O_WRITE | RTFILE_O_DENY_WRITE | RTFILE_O_DENY_READ;
    if (fOverwrite)
        fOpen |= RTFILE_O_CREATE_REPLACE;
    else
        fOpen |= RTFILE_O_CREATE;
    RTFILE hFile;
    vrc = RTFileOpen(&hFile, pszFilename, fOpen);
    if (RT_SUCCESS(vrc))
    {
        vrc = RTFileWrite(hFile, pszCmdLine, strlen(pszCmdLine), NULL);
        if (RT_SUCCESS(vrc))
            vrc = RTFileClose(hFile);
        else
            RTFileClose(hFile);
        if (RT_SUCCESS(vrc))
            hrc = S_OK;
        else
            hrc = mpParent->setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Error writing '%s' (%Rrc)"), pszFilename, vrc);
    }
    else
        hrc = mpParent->setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Failed to create '%s' (%Rrc)"), pszFilename, vrc);

    RTStrFree(pszCmdLine);
    return hrc;
}

HRESULT UnattendedInstaller::loadAndParseFileFromIso(RTVFS hVfsOrgIso, const char *pszFilename, AbstractScript *pEditor)
{
    HRESULT   hrc;
    RTVFSFILE hVfsFile;
    int vrc = RTVfsFileOpen(hVfsOrgIso, pszFilename, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE, &hVfsFile);
    if (RT_SUCCESS(vrc))
    {
        hrc = pEditor->readFromHandle(hVfsFile, pszFilename);
        RTVfsFileRelease(hVfsFile);
        if (SUCCEEDED(hrc))
            hrc = pEditor->parse();
    }
    else
        hrc = mpParent->setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Failed to open '%s' on the ISO '%s' (%Rrc)"),
                                     pszFilename, mpParent->i_getIsoPath().c_str(), vrc);
    return hrc;
}



//////////////////////////////////////////////////////////////////////////////////////////////////////
/*
*
*
*  Implementation UnattendedLinuxInstaller functions
*
*/
//////////////////////////////////////////////////////////////////////////////////////////////////////
HRESULT UnattendedLinuxInstaller::editIsoLinuxCfg(GeneralTextScript *pEditor)
{
    try
    {
        /* Comment out 'display <filename>' directives that's used for displaying files at boot time. */
        std::vector<size_t> vecLineNumbers =  pEditor->findTemplate("display", RTCString::CaseInsensitive);
        for (size_t i = 0; i < vecLineNumbers.size(); ++i)
            if (pEditor->getContentOfLine(vecLineNumbers[i]).startsWithWord("display", RTCString::CaseInsensitive))
            {
                HRESULT hrc = pEditor->prependToLine(vecLineNumbers.at(i), "#");
                if (FAILED(hrc))
                    return hrc;
            }
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }
    return editIsoLinuxCommon(pEditor);
}

HRESULT UnattendedLinuxInstaller::editIsoLinuxCommon(GeneralTextScript *pEditor)
{
    try
    {
        /* Set timeouts to 4 seconds. */
        std::vector<size_t> vecLineNumbers = pEditor->findTemplate("timeout", RTCString::CaseInsensitive);
        for (size_t i = 0; i < vecLineNumbers.size(); ++i)
            if (pEditor->getContentOfLine(vecLineNumbers[i]).startsWithWord("timeout", RTCString::CaseInsensitive))
            {
                HRESULT hrc = pEditor->setContentOfLine(vecLineNumbers.at(i), "timeout 4");
                if (FAILED(hrc))
                    return hrc;
            }

        /* Modify kernel parameters. */
        vecLineNumbers = pEditor->findTemplate("append", RTCString::CaseInsensitive);
        if (vecLineNumbers.size() > 0)
        {
            Utf8Str const &rStrAppend = mpParent->i_getExtraInstallKernelParameters().isNotEmpty()
                                      ? mpParent->i_getExtraInstallKernelParameters()
                                      : mStrDefaultExtraInstallKernelParameters;

            for (size_t i = 0; i < vecLineNumbers.size(); ++i)
                if (pEditor->getContentOfLine(vecLineNumbers[i]).startsWithWord("append", RTCString::CaseInsensitive))
                {
                    Utf8Str strLine = pEditor->getContentOfLine(vecLineNumbers[i]);

                    /* Do removals. */
                    if (mArrStrRemoveInstallKernelParameters.size() > 0)
                    {
                        size_t offStart = strLine.find("append") + 5;
                        while (offStart < strLine.length() && !RT_C_IS_SPACE(strLine[offStart]))
                            offStart++;
                        while (offStart < strLine.length() && RT_C_IS_SPACE(strLine[offStart]))
                            offStart++;
                        if (offStart < strLine.length())
                        {
                            for (size_t iRemove = 0; iRemove < mArrStrRemoveInstallKernelParameters.size(); iRemove++)
                            {
                                RTCString const &rStrRemove = mArrStrRemoveInstallKernelParameters[iRemove];
                                for (size_t off = offStart; off < strLine.length(); )
                                {
                                    Assert(!RT_C_IS_SPACE(strLine[off]));

                                    /* Find the end of word. */
                                    size_t offEnd = off + 1;
                                    while (offEnd < strLine.length() && !RT_C_IS_SPACE(strLine[offEnd]))
                                        offEnd++;

                                    /* Check if it matches. */
                                    if (RTStrSimplePatternNMatch(rStrRemove.c_str(), rStrRemove.length(),
                                                                 strLine.c_str() + off, offEnd - off))
                                    {
                                        while (off > 0 && RT_C_IS_SPACE(strLine[off - 1]))
                                            off--;
                                        strLine.erase(off, offEnd - off);
                                    }

                                    /* Advance to the next word. */
                                    off = offEnd;
                                    while (off < strLine.length() && RT_C_IS_SPACE(strLine[off]))
                                        off++;
                                }
                            }
                        }
                    }

                    /* Do the appending. */
                    if (rStrAppend.isNotEmpty())
                    {
                        if (!rStrAppend.startsWith(" ") && !strLine.endsWith(" "))
                            strLine.append(' ');
                        strLine.append(rStrAppend);
                    }

                    /* Update line. */
                    HRESULT hrc = pEditor->setContentOfLine(vecLineNumbers.at(i), strLine);
                    if (FAILED(hrc))
                        return hrc;
                }
        }
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }
    return S_OK;
}


HRESULT UnattendedLinuxInstaller::editGrubCfg(GeneralTextScript *pEditor)
{
    /* Default menu entry of grub.cfg is set in /etc/deafult/grub file. */
    try
    {
        /* Set timeouts to 4 seconds. */
        std::vector<size_t> vecLineNumbers = pEditor->findTemplate("set timeout", RTCString::CaseInsensitive);
        if (vecLineNumbers.size() > 0)
        {
            for (size_t i = 0; i < vecLineNumbers.size(); ++i)
                if (pEditor->getContentOfLine(vecLineNumbers[i]).startsWithWord("set timeout", RTCString::CaseInsensitive))
                {
                    HRESULT hrc = pEditor->setContentOfLine(vecLineNumbers.at(i), "set timeout=4");
                    if (FAILED(hrc))
                        return hrc;
                }
        }
        else
        {
            /* Append timeout if not set (happens with arm64 iso images at least). */
            HRESULT hrc = pEditor->appendLine("set timeout=4");
            if (FAILED(hrc))
                return hrc;
        }

        /* Modify kernel lines assuming that they starts with 'linux' keyword and 2nd word is the kernel command.*
         * we remove whatever comes after command and add our own command line options. */
        vecLineNumbers = pEditor->findTemplate("linux", RTCString::CaseInsensitive);
        if (vecLineNumbers.size() > 0)
        {
            Utf8Str const &rStrAppend = mpParent->i_getExtraInstallKernelParameters().isNotEmpty()
                                      ? mpParent->i_getExtraInstallKernelParameters()
                                      : mStrDefaultExtraInstallKernelParameters;

            for (size_t i = 0; i < vecLineNumbers.size(); ++i)
            {
                HRESULT hrc = S_OK;
                if (pEditor->getContentOfLine(vecLineNumbers[i]).startsWithWord("linux", RTCString::CaseInsensitive))
                {
                    Utf8Str strLine = pEditor->getContentOfLine(vecLineNumbers[i]);
                    size_t cbPos = strLine.find("linux") + strlen("linux");
                    bool fSecondWord = false;
                    /* Find the end of 2nd word assuming that it is kernel command. */
                    while (cbPos < strLine.length())
                    {
                        if (!fSecondWord)
                        {
                            if (strLine[cbPos] != '\t' && strLine[cbPos] != ' ')
                                fSecondWord = true;
                        }
                        else
                        {
                            if (strLine[cbPos] == '\t' || strLine[cbPos] == ' ')
                                break;
                        }
                        ++cbPos;
                    }
                    if (!fSecondWord)
                        hrc = E_FAIL;

                    if (SUCCEEDED(hrc))
                    {
                        strLine.erase(cbPos, strLine.length() - cbPos);

                        /* Do the appending. */
                        if (rStrAppend.isNotEmpty())
                        {
                            if (!rStrAppend.startsWith(" ") && !strLine.endsWith(" "))
                                strLine.append(' ');
                            strLine.append(rStrAppend);
                        }

                        /* Update line. */
                        hrc = pEditor->setContentOfLine(vecLineNumbers.at(i), strLine);
                    }
                    if (FAILED(hrc))
                        return hrc;
                }
            }
        }
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }
    return S_OK;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////
/*
*
*
*  Implementation UnattendedDebianInstaller functions
*
*/
//////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Helper for checking if a file exists.
 * @todo promote to IPRT?
 */
static bool hlpVfsFileExists(RTVFS hVfs, const char *pszPath)
{
    RTFSOBJINFO ObjInfo;
    int vrc = RTVfsQueryPathInfo(hVfs, pszPath, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK);
    return RT_SUCCESS(vrc) && RTFS_IS_FILE(ObjInfo.Attr.fMode);
}

HRESULT UnattendedDebianInstaller::addFilesToAuxVisoVectors(RTCList<RTCString> &rVecArgs, RTCList<RTCString> &rVecFiles,
                                                            RTVFS hVfsOrgIso, bool fOverwrite)
{
    /*
     * Figure out the name of the menu config file that we have to edit.
     */
    bool        fMenuConfigIsGrub     = false;
    const char *pszMenuConfigFilename = "/isolinux/txt.cfg";
    if (!hlpVfsFileExists(hVfsOrgIso, pszMenuConfigFilename))
    {
        /* On Debian Live ISOs (at least from 9 to 11) the there is only menu.cfg. */
        if (hlpVfsFileExists(hVfsOrgIso, "/isolinux/menu.cfg"))
            pszMenuConfigFilename     =  "/isolinux/menu.cfg";
        /* On Linux Mint 20.3, 21, and 19 (at least) there is only isolinux.cfg. */
        else if (hlpVfsFileExists(hVfsOrgIso, "/isolinux/isolinux.cfg"))
            pszMenuConfigFilename     =  "/isolinux/isolinux.cfg";
        /* Ubuntus 21.10+ are UEFI only. No isolinux directory. We modify grub.cfg. */
        else if (hlpVfsFileExists(hVfsOrgIso, "/boot/grub/grub.cfg"))
        {
            pszMenuConfigFilename     =       "/boot/grub/grub.cfg";
            fMenuConfigIsGrub         = true;
        }
    }

    /* Check for existence of isolinux.cfg since UEFI-only ISOs do not have this file.  */
    bool const fIsoLinuxCfgExists = hlpVfsFileExists(hVfsOrgIso, "isolinux/isolinux.cfg");
    Assert(!fIsoLinuxCfgExists || !fMenuConfigIsGrub); /** @todo r=bird: Perhaps prefix the hlpVfsFileExists call with 'fIsoLinuxCfgExists &&' above ? */

    /*
     * VISO bits and filenames.
     */
    RTCString strIsoLinuxCfg;
    RTCString strTxtCfg;
    try
    {
        /* Remaster ISO. */
        rVecArgs.append() = "--no-file-mode";
        rVecArgs.append() = "--no-dir-mode";

        rVecArgs.append() = "--import-iso";
        rVecArgs.append(mpParent->i_getIsoPath());

        rVecArgs.append() = "--file-mode=0444";
        rVecArgs.append() = "--dir-mode=0555";

        /* Replace the isolinux.cfg configuration file. */
        if (fIsoLinuxCfgExists)
        {
            /* First remove. */
            rVecArgs.append() = "isolinux/isolinux.cfg=:must-remove:";
            /* Then add the modified file. */
            strIsoLinuxCfg = mpParent->i_getAuxiliaryBasePath();
            strIsoLinuxCfg.append("isolinux-isolinux.cfg");
            rVecArgs.append().append("isolinux/isolinux.cfg=").append(strIsoLinuxCfg);
        }

        /*
         * Replace menu configuration file as well.
         * Some distros (Linux Mint) has only isolinux.cfg. No menu.cfg or txt.cfg.
         */
        if (RTStrICmp(pszMenuConfigFilename, "/isolinux/isolinux.cfg") != 0)
        {

            /* Replace menu configuration file as well. */
            rVecArgs.append().assign(pszMenuConfigFilename).append("=:must-remove:");
            strTxtCfg = mpParent->i_getAuxiliaryBasePath();
            if (fMenuConfigIsGrub)
                strTxtCfg.append("grub.cfg");
            else
                strTxtCfg.append("isolinux-txt.cfg");
            rVecArgs.append().assign(pszMenuConfigFilename).append("=").append(strTxtCfg);
        }
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    /*
     * Edit the isolinux.cfg file if it is there.
     */
    if (fIsoLinuxCfgExists)
    {
        GeneralTextScript Editor(mpParent);
        HRESULT hrc = loadAndParseFileFromIso(hVfsOrgIso, "/isolinux/isolinux.cfg", &Editor);
        if (SUCCEEDED(hrc))
            hrc = editIsoLinuxCfg(&Editor, RTPathFilename(pszMenuConfigFilename));
        if (SUCCEEDED(hrc))
        {
            hrc = Editor.save(strIsoLinuxCfg, fOverwrite);
            if (SUCCEEDED(hrc))
            {
                try
                {
                    rVecFiles.append(strIsoLinuxCfg);
                }
                catch (std::bad_alloc &)
                {
                    RTFileDelete(strIsoLinuxCfg.c_str());
                    hrc = E_OUTOFMEMORY;
                }
            }
        }
        if (FAILED(hrc))
            return hrc;
    }

    /*
     * Edit the menu config file.
     * Some distros (Linux Mint) has only isolinux.cfg. No menu.cfg or txt.cfg.
    */
    if (RTStrICmp(pszMenuConfigFilename, "/isolinux/isolinux.cfg") != 0)
    {
        GeneralTextScript Editor(mpParent);
        HRESULT hrc = loadAndParseFileFromIso(hVfsOrgIso, pszMenuConfigFilename, &Editor);
        if (SUCCEEDED(hrc))
        {
            if (fMenuConfigIsGrub)
                hrc = editGrubCfg(&Editor);
            else
                    hrc = editDebianMenuCfg(&Editor);
            if (SUCCEEDED(hrc))
            {
                hrc = Editor.save(strTxtCfg, fOverwrite);
                if (SUCCEEDED(hrc))
                {
                    try
                    {
                        rVecFiles.append(strTxtCfg);
                    }
                    catch (std::bad_alloc &)
                    {
                        RTFileDelete(strTxtCfg.c_str());
                        hrc = E_OUTOFMEMORY;
                    }
                }
            }
        }
        if (FAILED(hrc))
            return hrc;
    }

    /*
     * Call parent to add the preseed file from mAlg.
     */
    return UnattendedLinuxInstaller::addFilesToAuxVisoVectors(rVecArgs, rVecFiles, hVfsOrgIso, fOverwrite);
}

HRESULT UnattendedDebianInstaller::editIsoLinuxCfg(GeneralTextScript *pEditor, const char *pszMenuConfigFileName)
{
    try
    {
        /* Include menu config file. Since it can be txt.cfg, menu.cfg or something else we need to parametrize this. */
        if (pszMenuConfigFileName && pszMenuConfigFileName[0] != '\0')
        {
            std::vector<size_t> vecLineNumbers = pEditor->findTemplate("include", RTCString::CaseInsensitive);
            for (size_t i = 0; i < vecLineNumbers.size(); ++i)
            {
                if (pEditor->getContentOfLine(vecLineNumbers[i]).startsWithWord("include", RTCString::CaseInsensitive))
                {
                    Utf8Str strIncludeLine("include ");
                    strIncludeLine.append(pszMenuConfigFileName);
                    HRESULT hrc = pEditor->setContentOfLine(vecLineNumbers.at(i), strIncludeLine);
                    if (FAILED(hrc))
                        return hrc;
                }
            }
        }

        /* Comment out default directives since in Debian case default is handled in menu config file. */
        std::vector<size_t> vecLineNumbers =  pEditor->findTemplate("default", RTCString::CaseInsensitive);
        for (size_t i = 0; i < vecLineNumbers.size(); ++i)
            if (pEditor->getContentOfLine(vecLineNumbers[i]).startsWithWord("default", RTCString::CaseInsensitive)
                && !pEditor->getContentOfLine(vecLineNumbers[i]).contains("default vesa", RTCString::CaseInsensitive))
            {
                HRESULT hrc = pEditor->prependToLine(vecLineNumbers.at(i), "#");
                if (FAILED(hrc))
                    return hrc;
            }

        /* Comment out "ui gfxboot bootlogo" line as it somehow messes things up on Kubuntu 20.04 (possibly others as well). */
        vecLineNumbers =  pEditor->findTemplate("ui gfxboot", RTCString::CaseInsensitive);
        for (size_t i = 0; i < vecLineNumbers.size(); ++i)
            if (pEditor->getContentOfLine(vecLineNumbers[i]).startsWithWord("ui gfxboot", RTCString::CaseInsensitive))
            {
                HRESULT hrc = pEditor->prependToLine(vecLineNumbers.at(i), "#");
                if (FAILED(hrc))
                    return hrc;
            }
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }
    return UnattendedLinuxInstaller::editIsoLinuxCfg(pEditor);
}

HRESULT UnattendedDebianInstaller::editDebianMenuCfg(GeneralTextScript *pEditor)
{
    /*
     * Unlike Redhats, Debian variants define boot menu not in isolinux.cfg but some other
     * menu configuration files. They are mostly called txt.cfg and/or menu.cfg (and possibly some other names)
     * In this functions we attempt to set menu's default label (default menu item) to the one containing the word 'install',
     * failing to find such a label (on Kubuntu 20.04 for example) we pick the first label with name 'live'.
     */
    try
    {
        HRESULT hrc = S_OK;
        std::vector<size_t> vecLineNumbers = pEditor->findTemplate("label", RTCString::CaseInsensitive);
        const char *pszNewLabelName = "VBoxUnatendedInstall";
        bool fLabelFound = modifyLabelLine(pEditor, vecLineNumbers, "install", pszNewLabelName);
        if (!fLabelFound)
            fLabelFound = modifyLabelLine(pEditor, vecLineNumbers, "live", pszNewLabelName);

        if (!fLabelFound)
            hrc = E_FAIL;;

        if (SUCCEEDED(hrc))
        {
            /* Modify the content of default lines so that they point to label we have chosen above. */
            Utf8Str strNewContent("default ");
            strNewContent.append(pszNewLabelName);

            std::vector<size_t> vecDefaultLineNumbers = pEditor->findTemplate("default", RTCString::CaseInsensitive);
            if (!vecDefaultLineNumbers.empty())
            {
                for (size_t j = 0; j < vecDefaultLineNumbers.size(); ++j)
                {
                    hrc = pEditor->setContentOfLine(vecDefaultLineNumbers[j], strNewContent);
                    if (FAILED(hrc))
                        break;
                }
            }
            /* Add a defaul label line. */
            else
                hrc = pEditor->appendLine(strNewContent);
        }
        if (FAILED(hrc))
            return hrc;
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }
    return UnattendedLinuxInstaller::editIsoLinuxCommon(pEditor);
}

bool UnattendedDebianInstaller::modifyLabelLine(GeneralTextScript *pEditor, const std::vector<size_t> &vecLineNumbers,
                                                const char *pszKeyWord, const char *pszNewLabelName)
{
    if (!pEditor)
        return false;
    Utf8Str strNewLabel("label ");
    strNewLabel.append(pszNewLabelName);
    HRESULT hrc = S_OK;
    for (size_t i = 0; i < vecLineNumbers.size(); ++i)
    {
        RTCString const &rContent = pEditor->getContentOfLine(vecLineNumbers[i]);
        /* Skip this line if it does not start with the word 'label'. */
        if (!RTStrIStartsWith(rContent.c_str(), "label"))
            continue;
        /* Use the first menu item starting with word label and includes pszKeyWord.*/
        if (RTStrIStr(rContent.c_str(), pszKeyWord) != NULL)
        {
            /* Set the content of the line. It looks like multiple word labels (like label Debian Installer)
             * does not work very well in some cases. */
            hrc = pEditor->setContentOfLine(vecLineNumbers[i], strNewLabel);
            if (SUCCEEDED(hrc))
                return true;
        }
    }
    return false;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////
/*
*
*
*  Implementation UnattendedUbuntuAutoInstallInstaller functions
*
*/
//////////////////////////////////////////////////////////////////////////////////////////////////////
HRESULT UnattendedUbuntuAutoInstallInstaller::addFilesToAuxVisoVectors(RTCList<RTCString> &rVecArgs, RTCList<RTCString> &rVecFiles,
                                                                       RTVFS hVfsOrgIso, bool fOverwrite)
{
    try
    {
        /* Add the (empty) meta-data file to the ISO: */
        Utf8Str strUnattendedTemplates;
        int vrc = RTPathAppPrivateNoArchCxx(strUnattendedTemplates);
        AssertRCReturn(vrc, mpParent->setErrorVrc(vrc));
        vrc = RTPathAppendCxx(strUnattendedTemplates, "UnattendedTemplates");
        AssertRCReturn(vrc, mpParent->setErrorVrc(vrc));
        rVecArgs.append().assign("/meta-data=").append(strUnattendedTemplates).append("/ubuntu_autoinstall_meta_data");
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    /*
     * Call parent to add default Debian-based stuff.
     */
    return UnattendedDebianInstaller::addFilesToAuxVisoVectors(rVecArgs, rVecFiles, hVfsOrgIso, fOverwrite);
}


//////////////////////////////////////////////////////////////////////////////////////////////////////
/*
*
*
*  Implementation UnattendedRhel6Installer functions
*
*/
//////////////////////////////////////////////////////////////////////////////////////////////////////
HRESULT UnattendedRhelInstaller::addFilesToAuxVisoVectors(RTCList<RTCString> &rVecArgs, RTCList<RTCString> &rVecFiles,
                                                          RTVFS hVfsOrgIso, bool fOverwrite)
{
    /*
     * Figure out the name of the menu config file that we have to edit.
     */
    bool        fMenuConfigIsGrub     = false;
    const char *pszMenuConfigFilename = "/isolinux/isolinux.cfg";
    if (!hlpVfsFileExists(hVfsOrgIso, pszMenuConfigFilename))
    {
        /* arm64 variants of Oracle Linux 9 have grub. */
        if (hlpVfsFileExists(hVfsOrgIso, "/EFI/BOOT/grub.cfg"))
        {
            pszMenuConfigFilename     =  "/EFI/BOOT/grub.cfg";
            fMenuConfigIsGrub         = true;
        }
        else
            AssertFailed();
    }

    /*
     * VISO bits and filenames.
     */
    RTCString strBootCfg;
    try
    {
#if 1
        /* Remaster ISO. */
        rVecArgs.append() = "--no-file-mode";
        rVecArgs.append() = "--no-dir-mode";

        rVecArgs.append() = "--import-iso";
        rVecArgs.append(mpParent->i_getIsoPath());

        rVecArgs.append() = "--file-mode=0444";
        rVecArgs.append() = "--dir-mode=0555";

        /* Replace the grub.cfg/isolinux.cfg configuration file. */
        if (fMenuConfigIsGrub)
        {
            /* Replace menu configuration file as well. */
            rVecArgs.append().assign(pszMenuConfigFilename).append("=:must-remove:");
            strBootCfg = mpParent->i_getAuxiliaryBasePath();
            strBootCfg.append("grub.cfg");
            rVecArgs.append().assign(pszMenuConfigFilename).append("=").append(strBootCfg);
        }
        else
        {
            /* First remove. */
            rVecArgs.append() = "isolinux/isolinux.cfg=:must-remove:";
            strBootCfg = mpParent->i_getAuxiliaryBasePath();
            strBootCfg.append("isolinux-isolinux.cfg");
            rVecArgs.append().append("isolinux/isolinux.cfg=").append(strBootCfg);
        }
#else
        /** @todo Maybe we should just remaster the ISO for redhat derivatives too?
         *        One less CDROM to mount. */
        /* Name the ISO. */
        rVecArgs.append() = "--volume-id=VBox Unattended Boot";

        /* Copy the isolinux directory from the original install ISO. */
        rVecArgs.append().append("--push-iso=").append(mpParent->i_getIsoPath());
        rVecArgs.append() = "/isolinux=/isolinux";
        rVecArgs.append() = "--pop";

        /* We replace isolinux.cfg with our edited version (see further down). */
        rVecArgs.append() = "/isolinux/isolinux.cfg=:must-remove:";

        strBootCfg = mpParent->i_getAuxiliaryBasePath();
        strBootCfg.append("isolinux-isolinux.cfg");
        rVecArgs.append().append("/isolinux/isolinux.cfg=").append(strBootCfg);

        /* Configure booting /isolinux/isolinux.bin. */
        rVecArgs.append() = "--eltorito-boot";
        rVecArgs.append() = "/isolinux/isolinux.bin";
        rVecArgs.append() = "--no-emulation-boot";
        rVecArgs.append() = "--boot-info-table";
        rVecArgs.append() = "--boot-load-seg=0x07c0";
        rVecArgs.append() = "--boot-load-size=4";

        /* Make the boot catalog visible in the file system. */
        rVecArgs.append() = "--boot-catalog=/isolinux/vboxboot.cat";
#endif
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    {
        GeneralTextScript Editor(mpParent);
        HRESULT hrc = loadAndParseFileFromIso(hVfsOrgIso, pszMenuConfigFilename, &Editor);
        if (SUCCEEDED(hrc))
        {
            if (fMenuConfigIsGrub)
                hrc = editGrubCfg(&Editor);
            else
                hrc = editIsoLinuxCfg(&Editor);
            if (SUCCEEDED(hrc))
            {
                hrc = Editor.save(strBootCfg, fOverwrite);
                if (SUCCEEDED(hrc))
                {
                    try
                    {
                        rVecFiles.append(strBootCfg);
                    }
                    catch (std::bad_alloc &)
                    {
                        RTFileDelete(strBootCfg.c_str());
                        hrc = E_OUTOFMEMORY;
                    }
                }
            }
        }
        if (FAILED(hrc))
            return hrc;
    }

    /*
     * Call parent to add the ks.cfg file from mAlg.
     */
    return UnattendedLinuxInstaller::addFilesToAuxVisoVectors(rVecArgs, rVecFiles, hVfsOrgIso, fOverwrite);
}


//////////////////////////////////////////////////////////////////////////////////////////////////////
/*
*
*
*  Implementation UnattendedSuseInstaller functions
*
*/
//////////////////////////////////////////////////////////////////////////////////////////////////////
#if 0 /* doesn't work, so convert later */
/*
 *
 * UnattendedSuseInstaller protected methods
 *
*/
HRESULT UnattendedSuseInstaller::setUserData()
{
    HRESULT hrc = S_OK;
    //here base class function must be called first
    //because user home directory is set after user name
    hrc = UnattendedInstaller::setUserData();

    hrc = mAlg->setField(USERHOMEDIR_ID, "");
    if (FAILED(hrc))
        return hrc;

    return hrc;
}

/*
 *
 * UnattendedSuseInstaller private methods
 *
*/

HRESULT UnattendedSuseInstaller::iv_initialPhase()
{
    Assert(isAuxiliaryIsoNeeded());
    if (mParent->i_isGuestOs64Bit())
        mFilesAndDirsToExtractFromIso.append("boot/x86_64/loader/ ");
    else
        mFilesAndDirsToExtractFromIso.append("boot/i386/loader/ ");
    return extractOriginalIso(mFilesAndDirsToExtractFromIso);
}


HRESULT UnattendedSuseInstaller::setupScriptOnAuxiliaryCD(const Utf8Str &path)
{
    HRESULT hrc = S_OK;

    GeneralTextScript isoSuseCfgScript(mpParent);
    hrc = isoSuseCfgScript.read(path);
    hrc = isoSuseCfgScript.parse();
    //fix linux core bootable parameters: add path to the preseed script

    std::vector<size_t> listOfLines = isoSuseCfgScript.findTemplate("append");
    for(unsigned int i=0; i<listOfLines.size(); ++i)
    {
        isoSuseCfgScript.appendToLine(listOfLines.at(i),
                                      " auto=true priority=critical autoyast=default instmode=cd quiet splash noprompt noshell --");
    }

    //find all lines with "label" inside
    listOfLines = isoSuseCfgScript.findTemplate("label");
    for(unsigned int i=0; i<listOfLines.size(); ++i)
    {
        Utf8Str content = isoSuseCfgScript.getContentOfLine(listOfLines.at(i));

        //suppose general string looks like "label linux", two words separated by " ".
        RTCList<RTCString> partsOfcontent = content.split(" ");

        if (partsOfcontent.at(1).contains("linux"))
        {
            std::vector<size_t> listOfDefault = isoSuseCfgScript.findTemplate("default");
            //handle the lines more intelligently
            for(unsigned int j=0; j<listOfDefault.size(); ++j)
            {
                Utf8Str newContent("default ");
                newContent.append(partsOfcontent.at(1));
                isoSuseCfgScript.setContentOfLine(listOfDefault.at(j), newContent);
            }
        }
    }

    hrc = isoSuseCfgScript.save(path, true);

    LogRelFunc(("UnattendedSuseInstaller::setupScriptsOnAuxiliaryCD(): The file %s has been changed\n", path.c_str()));

    return hrc;
}
#endif


//////////////////////////////////////////////////////////////////////////////////////////////////////
/*
*
*
*  Implementation UnattendedFreeBsdInstaller functions
*
*/
//////////////////////////////////////////////////////////////////////////////////////////////////////
HRESULT UnattendedFreeBsdInstaller::addFilesToAuxVisoVectors(RTCList<RTCString> &rVecArgs, RTCList<RTCString> &rVecFiles,
                                                             RTVFS hVfsOrgIso, bool fOverwrite)
{
    try
    {
        RTCString strScriptName;
        strScriptName = mpParent->i_getAuxiliaryBasePath();
        strScriptName.append(mMainScript.getDefaultFilename());

        /* Need to retain the original file permissions for executables. */
        rVecArgs.append() = "--no-file-mode";
        rVecArgs.append() = "--no-dir-mode";

        rVecArgs.append() = "--import-iso";
        rVecArgs.append(mpParent->i_getIsoPath());

        rVecArgs.append() = "--file-mode=0444";
        rVecArgs.append() = "--dir-mode=0555";

        /* Remaster ISO, the installer config has to go into /etc. */
        rVecArgs.append().append("/etc/installerconfig=").append(strScriptName);
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    /*
     * Call parent to add the remaining files
     */
    return UnattendedInstaller::addFilesToAuxVisoVectors(rVecArgs, rVecFiles, hVfsOrgIso, fOverwrite);
}
