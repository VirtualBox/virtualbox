/* $Id: UnattendedImpl.cpp 107984 2025-01-21 13:54:00Z serkan.bayraktar@oracle.com $ */
/** @file
 * Unattended class implementation
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
#include "UnattendedImpl.h"
#include "UnattendedInstaller.h"
#include "UnattendedScript.h"
#include "VirtualBoxImpl.h"
#include "SystemPropertiesImpl.h"
#include "MachineImpl.h"
#include "Global.h"
#include "StringifyEnums.h"

#include <VBox/err.h>
#include <iprt/cpp/xml.h>
#include <iprt/ctype.h>
#include <iprt/file.h>
#ifndef RT_OS_WINDOWS
# include <iprt/formats/mz.h>
# include <iprt/formats/pecoff.h>
#endif
#include <iprt/formats/wim.h>
#include <iprt/fsvfs.h>
#include <iprt/inifile.h>
#include <iprt/locale.h>
#include <iprt/path.h>
#include <iprt/vfs.h>

using namespace std;


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Controller slot for a DVD drive.
 *
 * The slot can be free and needing a drive to be attached along with the ISO
 * image, or it may already be there and only need mounting the ISO.  The
 * ControllerSlot::fFree member indicates which it is.
 */
struct ControllerSlot
{
    StorageBus_T    enmBus;
    Utf8Str         strControllerName;
    LONG            iPort;
    LONG            iDevice;
    bool            fFree;

    ControllerSlot(StorageBus_T a_enmBus, const Utf8Str &a_rName, LONG a_iPort, LONG a_iDevice, bool a_fFree)
        : enmBus(a_enmBus), strControllerName(a_rName), iPort(a_iPort), iDevice(a_iDevice), fFree(a_fFree)
    {}

    bool operator<(const ControllerSlot &rThat) const
    {
        if (enmBus == rThat.enmBus)
        {
            if (strControllerName == rThat.strControllerName)
            {
                if (iPort == rThat.iPort)
                    return iDevice < rThat.iDevice;
                return iPort < rThat.iPort;
            }
            return strControllerName < rThat.strControllerName;
        }

        /*
         * Bus comparsion in boot priority order.
         */
        /* IDE first. */
        if (enmBus == StorageBus_IDE)
            return true;
        if (rThat.enmBus == StorageBus_IDE)
            return false;
        /* SATA next */
        if (enmBus == StorageBus_SATA)
            return true;
        if (rThat.enmBus == StorageBus_SATA)
            return false;
        /* SCSI next */
        if (enmBus == StorageBus_SCSI)
            return true;
        if (rThat.enmBus == StorageBus_SCSI)
            return false;
        /* numerical */
        return (int)enmBus < (int)rThat.enmBus;
    }

    bool operator==(const ControllerSlot &rThat) const
    {
        return enmBus            == rThat.enmBus
            && strControllerName == rThat.strControllerName
            && iPort             == rThat.iPort
            && iDevice           == rThat.iDevice;
    }
};

/**
 * Installation disk.
 *
 * Used when reconfiguring the VM.
 */
typedef struct UnattendedInstallationDisk
{
    StorageBus_T    enmBusType;         /**< @todo nobody is using this... */
    Utf8Str         strControllerName;
    DeviceType_T    enmDeviceType;
    AccessMode_T    enmAccessType;
    LONG            iPort;
    LONG            iDevice;
    bool            fMountOnly;
    Utf8Str         strImagePath;
    bool            fAuxiliary;

    UnattendedInstallationDisk(StorageBus_T a_enmBusType, Utf8Str const &a_rBusName, DeviceType_T a_enmDeviceType,
                               AccessMode_T a_enmAccessType, LONG a_iPort, LONG a_iDevice, bool a_fMountOnly,
                               Utf8Str const &a_rImagePath, bool a_fAuxiliary)
        : enmBusType(a_enmBusType), strControllerName(a_rBusName), enmDeviceType(a_enmDeviceType), enmAccessType(a_enmAccessType)
        , iPort(a_iPort), iDevice(a_iDevice), fMountOnly(a_fMountOnly), strImagePath(a_rImagePath), fAuxiliary(a_fAuxiliary)
    {
        Assert(strControllerName.length() > 0);
    }

    UnattendedInstallationDisk(std::list<ControllerSlot>::const_iterator const &itDvdSlot, Utf8Str const &a_rImagePath,
                               bool a_fAuxiliary)
        : enmBusType(itDvdSlot->enmBus), strControllerName(itDvdSlot->strControllerName), enmDeviceType(DeviceType_DVD)
        , enmAccessType(AccessMode_ReadOnly), iPort(itDvdSlot->iPort), iDevice(itDvdSlot->iDevice)
        , fMountOnly(!itDvdSlot->fFree), strImagePath(a_rImagePath), fAuxiliary(a_fAuxiliary)
    {
        Assert(strControllerName.length() > 0);
    }
} UnattendedInstallationDisk;


/**
 * OS/2 syslevel file header.
 */
#pragma pack(1)
typedef struct OS2SYSLEVELHDR
{
    uint16_t    uMinusOne;          /**< 0x00: UINT16_MAX */
    char        achSignature[8];    /**< 0x02: "SYSLEVEL" */
    uint8_t     abReserved1[5];     /**< 0x0a: Usually zero. Ignore.  */
    uint16_t    uSyslevelFileVer;   /**< 0x0f: The syslevel file version: 1. */
    uint8_t     abReserved2[16];    /**< 0x11: Zero. Ignore.  */
    uint32_t    offTable;           /**< 0x21: Offset of the syslevel table. */
} OS2SYSLEVELHDR;
#pragma pack()
AssertCompileSize(OS2SYSLEVELHDR, 0x25);

/**
 * OS/2 syslevel table entry.
 */
#pragma pack(1)
typedef struct OS2SYSLEVELENTRY
{
    uint16_t    id;                 /**< 0x00: ? */
    uint8_t     bEdition;           /**< 0x02: The OS/2 edition: 0=standard, 1=extended, x=component defined */
    uint8_t     bVersion;           /**< 0x03: 0x45 = 4.5 */
    uint8_t     bModify;            /**< 0x04: Lower nibble is added to bVersion, so 0x45 0x02 => 4.52 */
    uint8_t     abReserved1[2];     /**< 0x05: Zero. Ignore. */
    char        achCsdLevel[8];     /**< 0x07: The current CSD level. */
    char        achCsdPrior[8];     /**< 0x0f: The prior CSD level. */
    char        szName[80];         /**< 0x5f: System/component name. */
    char        achId[9];           /**< 0x67: System/component ID. */
    uint8_t     bRefresh;           /**< 0x70: Single digit refresh version, ignored if zero. */
    char        szType[9];          /**< 0x71: Some kind of type string. Optional */
    uint8_t     abReserved2[6];     /**< 0x7a: Zero. Ignore. */
} OS2SYSLEVELENTRY;
#pragma pack()
AssertCompileSize(OS2SYSLEVELENTRY, 0x80);



/**
 * Concatenate image name and version strings and return.
 *
 * A possible output would be "Windows 10 Home (10.0.19041.330 / x64)".
 *
 * @returns Name string to use.
 * @param   r_strName   String object that can be formatted into and returned.
 */
const Utf8Str &WIMImage::formatName(Utf8Str &r_strName) const
{
    /* We skip the mFlavor as it's typically part of the description already. */

    if (mVersion.isEmpty() && mArch.isEmpty() && mDefaultLanguage.isEmpty() && mLanguages.size() == 0)
        return mName;

    r_strName = mName;
    bool fFirst = true;
    if (mVersion.isNotEmpty())
    {
        r_strName.appendPrintf(fFirst ? " (%s" : " / %s", mVersion.c_str());
        fFirst = false;
    }
    if (mArch.isNotEmpty())
    {
        r_strName.appendPrintf(fFirst ? " (%s" : " / %s", mArch.c_str());
        fFirst = false;
    }
    if (mDefaultLanguage.isNotEmpty())
    {
        r_strName.appendPrintf(fFirst ? " (%s" : " / %s", mDefaultLanguage.c_str());
        fFirst = false;
    }
    else
        for (size_t i = 0; i < mLanguages.size(); i++)
        {
            r_strName.appendPrintf(fFirst ? " (%s" : " / %s", mLanguages[i].c_str());
            fFirst = false;
        }
    r_strName.append(")");
    return r_strName;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////
/*
*
*
*  Implementation Unattended functions
*
*/
//////////////////////////////////////////////////////////////////////////////////////////////////////

Unattended::Unattended()
    : mhThreadReconfigureVM(NIL_RTNATIVETHREAD), mfRtcUseUtc(false), mfGuestOs64Bit(false)
    , mpInstaller(NULL), mpTimeZoneInfo(NULL), mfIsDefaultAuxiliaryBasePath(true), mfDoneDetectIsoOS(false)
    , mfAvoidUpdatesOverNetwork(false), mfDoneSupportedGuestOSList(false)
{ }

Unattended::~Unattended()
{
    if (mpInstaller)
    {
        delete mpInstaller;
        mpInstaller = NULL;
    }
}

HRESULT Unattended::FinalConstruct()
{
    return BaseFinalConstruct();
}

void Unattended::FinalRelease()
{
    uninit();

    BaseFinalRelease();
}

void Unattended::uninit()
{
    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    unconst(mParent) = NULL;
    mMachine.setNull();
}

/**
 * Initializes the unattended object.
 *
 * @param aParent  Pointer to the parent object.
 */
HRESULT Unattended::initUnattended(VirtualBox *aParent)
{
    LogFlowThisFunc(("aParent=%p\n", aParent));
    ComAssertRet(aParent, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mParent) = aParent;

    /*
     * Fill public attributes (IUnattended) with useful defaults.
     */
    try
    {
        mStrUser                    = "vboxuser";
        mStrUserPassword            = "";
        /* Note: For mStrAdminPassword see Unattended::i_getAdminPassword(). */
        mfInstallGuestAdditions     = false;
        mfInstallTestExecService    = false;
        mfInstallUserPayload        = false;
        midxImage                   = 1;

        HRESULT hrc = mParent->i_getSystemProperties()->i_getDefaultAdditionsISO(mStrAdditionsIsoPath);
        ComAssertComRCRet(hrc, hrc);
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    /*
     * Confirm a successful initialization
     */
    autoInitSpan.setSucceeded();

    return S_OK;
}

HRESULT Unattended::detectIsoOS()
{
    HRESULT       hrc;

    /* Populate list of supported guest OSs in case it has not been done yet. Do this before locking. */
    if (!mfDoneSupportedGuestOSList)
        i_getListOfSupportedGuestOS();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /** @todo once UDF is implemented properly and we've tested this code a lot
     *        more, replace E_NOTIMPL with E_FAIL. */

    /*
     * Reset output state before we start
     */
    mStrDetectedOSTypeId.setNull();
    mStrDetectedOSVersion.setNull();
    mStrDetectedOSFlavor.setNull();
    mDetectedOSLanguages.clear();
    mStrDetectedOSHints.setNull();
    mDetectedImages.clear();

    /*
     * Open the ISO.
     */
    RTVFSFILE hVfsFileIso;
    int vrc = RTVfsFileOpenNormal(mStrIsoPath.c_str(), RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE, &hVfsFileIso);
    if (RT_FAILURE(vrc))
        return setErrorBoth(E_NOTIMPL, vrc, tr("Failed to open '%s' (%Rrc)"), mStrIsoPath.c_str(), vrc);

    RTERRINFOSTATIC ErrInfo;
    RTVFS hVfsIso;
    vrc = RTFsIso9660VolOpen(hVfsFileIso, 0 /*fFlags*/, &hVfsIso, RTErrInfoInitStatic(&ErrInfo));
    if (RT_SUCCESS(vrc))
    {
        /*
         * Try do the detection.  Repeat for different file system variations (nojoliet, noudf).
         */
        hrc = i_innerDetectIsoOS(hVfsIso);

        RTVfsRelease(hVfsIso);

        /* If detecting the ISO failed, print everything we got to the VBoxSVC release log,
         * to (hopefully) provide us more clues about which distros don't work. */
        if (FAILED(hrc))
        {
            Utf8Str strLangs;
            for (size_t i = 0; i < mDetectedOSLanguages.size(); i++)
            {
                if (i)
                    strLangs += ", ";
                strLangs += mDetectedOSLanguages[i];
            }

            Utf8Str strImages;
            for (size_t i = 0; i < mDetectedImages.size(); i++)
            {
                if (i)
                    strImages += ", ";
                strImages += mDetectedImages[i].mName;
            }

            LogRel(("Unattended: Detection summary:\n"
                    "Unattended: OS type ID : %s\n"
                    "Unattended: OS version : %s\n"
                    "Unattended: OS flavor  : %s\n"
                    "Unattended: OS language: %s\n"
                    "Unattended: OS hints   : %s\n"
                    "Unattended: Images     : %s\n",
                    mStrDetectedOSTypeId.c_str(),
                    mStrDetectedOSVersion.c_str(),
                    mStrDetectedOSFlavor.c_str(),
                    strLangs.c_str(),
                    mStrDetectedOSHints.c_str(),
                    strImages.c_str()));
        }

        if (hrc == S_FALSE) /** @todo Finish the linux and windows detection code. Only OS/2 returns S_OK right now. */
            hrc = E_NOTIMPL;
    }
    else if (RTErrInfoIsSet(&ErrInfo.Core))
        hrc = setErrorBoth(E_NOTIMPL, vrc, tr("Failed to open '%s' as ISO FS (%Rrc) - %s"),
                           mStrIsoPath.c_str(), vrc, ErrInfo.Core.pszMsg);
    else
        hrc = setErrorBoth(E_NOTIMPL, vrc, tr("Failed to open '%s' as ISO FS (%Rrc)"), mStrIsoPath.c_str(), vrc);
    RTVfsFileRelease(hVfsFileIso);

    /*
     * Just fake up some windows installation media locale (for <UILanguage>).
     * Note! The translation here isn't perfect.  Feel free to send us a patch.
     */
    if (mDetectedOSLanguages.size() == 0)
    {
        char        szTmp[16];
        const char *pszFilename = RTPathFilename(mStrIsoPath.c_str());
        if (   pszFilename
            && RT_C_IS_ALPHA(pszFilename[0])
            && RT_C_IS_ALPHA(pszFilename[1])
            && (pszFilename[2] == '-' || pszFilename[2] == '_') )
        {
            szTmp[0] = (char)RT_C_TO_LOWER(pszFilename[0]);
            szTmp[1] = (char)RT_C_TO_LOWER(pszFilename[1]);
            szTmp[2] = '-';
            if (szTmp[0] == 'e' && szTmp[1] == 'n')
                strcpy(&szTmp[3], "US");
            else if (szTmp[0] == 'a' && szTmp[1] == 'r')
                strcpy(&szTmp[3], "SA");
            else if (szTmp[0] == 'd' && szTmp[1] == 'a')
                strcpy(&szTmp[3], "DK");
            else if (szTmp[0] == 'e' && szTmp[1] == 't')
                strcpy(&szTmp[3], "EE");
            else if (szTmp[0] == 'e' && szTmp[1] == 'l')
                strcpy(&szTmp[3], "GR");
            else if (szTmp[0] == 'h' && szTmp[1] == 'e')
                strcpy(&szTmp[3], "IL");
            else if (szTmp[0] == 'j' && szTmp[1] == 'a')
                strcpy(&szTmp[3], "JP");
            else if (szTmp[0] == 's' && szTmp[1] == 'v')
                strcpy(&szTmp[3], "SE");
            else if (szTmp[0] == 'u' && szTmp[1] == 'k')
                strcpy(&szTmp[3], "UA");
            else if (szTmp[0] == 'c' && szTmp[1] == 's')
                strcpy(szTmp, "cs-CZ");
            else if (szTmp[0] == 'n' && szTmp[1] == 'o')
                strcpy(szTmp, "nb-NO");
            else if (szTmp[0] == 'p' && szTmp[1] == 'p')
                strcpy(szTmp, "pt-PT");
            else if (szTmp[0] == 'p' && szTmp[1] == 't')
                strcpy(szTmp, "pt-BR");
            else if (szTmp[0] == 'c' && szTmp[1] == 'n')
                strcpy(szTmp, "zh-CN");
            else if (szTmp[0] == 'h' && szTmp[1] == 'k')
                strcpy(szTmp, "zh-HK");
            else if (szTmp[0] == 't' && szTmp[1] == 'w')
                strcpy(szTmp, "zh-TW");
            else if (szTmp[0] == 's' && szTmp[1] == 'r')
                strcpy(szTmp, "sr-Latn-CS"); /* hmm */
            else
            {
                szTmp[3] = (char)RT_C_TO_UPPER(pszFilename[0]);
                szTmp[4] = (char)RT_C_TO_UPPER(pszFilename[1]);
                szTmp[5] = '\0';
            }
        }
        else
            strcpy(szTmp, "en-US");
        try
        {
            mDetectedOSLanguages.append(szTmp);
        }
        catch (std::bad_alloc &)
        {
            return E_OUTOFMEMORY;
        }
    }

    /* Check if detected OS type is supported (covers platform architecture). */
    bool fSupported = false;
    for (size_t i = 0; i < mSupportedGuestOSTypes.size() && !fSupported; ++i)
    {
        ComPtr<IGuestOSType> guestOSType = mSupportedGuestOSTypes[i];

        Bstr guestId;
        guestOSType->COMGETTER(Id)(guestId.asOutParam());
        if (guestId == mStrDetectedOSTypeId)
            fSupported = true;
    }
    if (!fSupported)
    {
        mStrDetectedOSTypeId.setNull();
        mStrDetectedOSVersion.setNull();
        mStrDetectedOSFlavor.setNull();
        mDetectedOSLanguages.clear();
        mStrDetectedOSHints.setNull();
        mDetectedImages.clear();
        hrc = E_FAIL;
    }

    /** @todo implement actual detection logic. */
    return hrc;
}

HRESULT Unattended::i_innerDetectIsoOS(RTVFS hVfsIso)
{
    DETECTBUFFER uBuf;
    mEnmOsType = VBOXOSTYPE_Unknown;
    HRESULT hrc = i_innerDetectIsoOSWindows(hVfsIso, &uBuf);
    if (hrc == S_FALSE && mEnmOsType == VBOXOSTYPE_Unknown)
        hrc = i_innerDetectIsoOSLinux(hVfsIso, &uBuf);
    if (hrc == S_FALSE && mEnmOsType == VBOXOSTYPE_Unknown)
        hrc = i_innerDetectIsoOSOs2(hVfsIso, &uBuf);
    if (hrc == S_FALSE && mEnmOsType == VBOXOSTYPE_Unknown)
        hrc = i_innerDetectIsoOSFreeBsd(hVfsIso, &uBuf);
    if (mEnmOsType != VBOXOSTYPE_Unknown)
    {
        try {  mStrDetectedOSTypeId = Global::OSTypeId(mEnmOsType); }
        catch (std::bad_alloc &) { hrc = E_OUTOFMEMORY; }
    }
    return hrc;
}

/**
 * Tries to parse a LANGUAGES element, with the following structure.
 * @verbatim
 * <LANGUAGES>
 *     <LANGUAGE>
 *         en-US
 *     </LANGUAGE>
 *     <DEFAULT>
 *         en-US
 *     </DEFAULT>
 * </LANGUAGES>
 * @endverbatim
 *
 * Will set mLanguages and mDefaultLanguage success.
 *
 * @param   pElmLanguages   Points to the LANGUAGES XML node.
 * @param   rImage          Out reference to an WIMImage instance.
 */
static void parseLangaguesElement(const xml::ElementNode *pElmLanguages, WIMImage &rImage)
{
    /*
     * The languages.
     */
    ElementNodesList children;
    int cChildren = pElmLanguages->getChildElements(children, "LANGUAGE");
    if (cChildren == 0)
        cChildren = pElmLanguages->getChildElements(children, "language");
    if (cChildren == 0)
        cChildren = pElmLanguages->getChildElements(children, "Language");
    for (ElementNodesList::iterator iterator = children.begin(); iterator != children.end(); ++iterator)
    {
        const ElementNode * const pElmLanguage = *(iterator);
        if (pElmLanguage)
        {
            const char *pszValue = pElmLanguage->getValue();
            if (pszValue && *pszValue != '\0')
                rImage.mLanguages.append(pszValue);
        }
    }

    /*
     * Default language.
     */
    const xml::ElementNode *pElmDefault;
    if (   (pElmDefault = pElmLanguages->findChildElement("DEFAULT")) != NULL
        || (pElmDefault = pElmLanguages->findChildElement("default")) != NULL
        || (pElmDefault = pElmLanguages->findChildElement("Default")) != NULL)
        rImage.mDefaultLanguage = pElmDefault->getValue();
}


/**
 * Tries to set the image architecture.
 *
 * Input examples (x86 and amd64 respectively):
 * @verbatim
 * <ARCH>0</ARCH>
 * <ARCH>9</ARCH>
 * @endverbatim
 *
 * Will set mArch and update mOSType on success.
 *
 * @param   pElmArch    Points to the ARCH XML node.
 * @param   rImage      Out reference to an WIMImage instance.
 */
static void parseArchElement(const xml::ElementNode *pElmArch, WIMImage &rImage)
{
    /* These are from winnt.h */
    static struct { const char *pszArch;  VBOXOSTYPE enmArch; } s_aArches[] =
    {
        /* PROCESSOR_ARCHITECTURE_INTEL         / [0]  = */ { "x86",                 VBOXOSTYPE_x86         },
        /* PROCESSOR_ARCHITECTURE_MIPS          / [1]  = */ { "mips",                VBOXOSTYPE_UnknownArch },
        /* PROCESSOR_ARCHITECTURE_ALPHA         / [2]  = */ { "alpha",               VBOXOSTYPE_UnknownArch },
        /* PROCESSOR_ARCHITECTURE_PPC           / [3]  = */ { "ppc",                 VBOXOSTYPE_UnknownArch },
        /* PROCESSOR_ARCHITECTURE_SHX           / [4]  = */ { "shx",                 VBOXOSTYPE_UnknownArch },
        /* PROCESSOR_ARCHITECTURE_ARM           / [5]  = */ { "arm32",               VBOXOSTYPE_arm32       },
        /* PROCESSOR_ARCHITECTURE_IA64          / [6]  = */ { "ia64",                VBOXOSTYPE_UnknownArch },
        /* PROCESSOR_ARCHITECTURE_ALPHA64       / [7]  = */ { "alpha64",             VBOXOSTYPE_UnknownArch },
        /* PROCESSOR_ARCHITECTURE_MSIL          / [8]  = */ { "msil",                VBOXOSTYPE_UnknownArch },
        /* PROCESSOR_ARCHITECTURE_AMD64         / [9]  = */ { "x64",                 VBOXOSTYPE_x64         },
        /* PROCESSOR_ARCHITECTURE_IA32_ON_WIN64 / [10] = */ { "x86-on-x64",          VBOXOSTYPE_UnknownArch },
        /* PROCESSOR_ARCHITECTURE_NEUTRAL       / [11] = */ { "noarch",              VBOXOSTYPE_UnknownArch },
        /* PROCESSOR_ARCHITECTURE_ARM64         / [12] = */ { "arm64",               VBOXOSTYPE_arm64       },
        /* PROCESSOR_ARCHITECTURE_ARM32_ON_WIN64/ [13] = */ { "arm32-on-arm64",      VBOXOSTYPE_UnknownArch },
        /* PROCESSOR_ARCHITECTURE_IA32_ON_ARM64 / [14] = */ { "x86-on-arm32",        VBOXOSTYPE_UnknownArch },
    };
    const char *pszArch = pElmArch->getValue();
    if (pszArch && *pszArch)
    {
        uint32_t uArch;
        int vrc = RTStrToUInt32Ex(pszArch, NULL, 10 /*uBase*/, &uArch);
        if (   RT_SUCCESS(vrc)
            && vrc != VWRN_NUMBER_TOO_BIG
            && vrc != VWRN_NEGATIVE_UNSIGNED
            && uArch < RT_ELEMENTS(s_aArches))
        {
            rImage.mArch   = s_aArches[uArch].pszArch;
            rImage.mOSType = (VBOXOSTYPE)(s_aArches[uArch].enmArch | (rImage.mOSType & VBOXOSTYPE_OsMask));
        }
        else
            LogRel(("Unattended: bogus ARCH element value: '%s'\n", pszArch));
    }
}

/**
 * Parses XML Node assuming a structure as follows
 * @verbatim
 * <VERSION>
 *     <MAJOR>10</MAJOR>
 *     <MINOR>0</MINOR>
 *     <BUILD>19041</BUILD>
 *     <SPBUILD>1</SPBUILD>
 * </VERSION>
 * @endverbatim
 *
 * Will update mOSType, mEnmOsType as well as setting mVersion on success.
 *
 * @param   pNode          Points to the vesion XML node,
 * @param   image          Out reference to an WIMImage instance.
 */
static void parseVersionElement(const xml::ElementNode *pNode, WIMImage &image)
{
    /* Major part: */
    const xml::ElementNode *pElmMajor;
    if (   (pElmMajor = pNode->findChildElement("MAJOR")) != NULL
        || (pElmMajor = pNode->findChildElement("major")) != NULL
        || (pElmMajor = pNode->findChildElement("Major")) != NULL)
    if (pElmMajor)
    {
        const char * const pszMajor = pElmMajor->getValue();
        if (pszMajor && *pszMajor)
        {
            /* Minor part: */
            const ElementNode *pElmMinor;
            if (   (pElmMinor = pNode->findChildElement("MINOR")) != NULL
                || (pElmMinor = pNode->findChildElement("minor")) != NULL
                || (pElmMinor = pNode->findChildElement("Minor")) != NULL)
            {
                const char * const pszMinor = pElmMinor->getValue();
                if (pszMinor && *pszMinor)
                {
                    /* Build: */
                    const ElementNode *pElmBuild;
                    if (   (pElmBuild = pNode->findChildElement("BUILD")) != NULL
                        || (pElmBuild = pNode->findChildElement("build")) != NULL
                        || (pElmBuild = pNode->findChildElement("Build")) != NULL)
                    {
                        const char * const pszBuild = pElmBuild->getValue();
                        if (pszBuild && *pszBuild)
                        {
                            /* SPBuild: */
                            const ElementNode *pElmSpBuild;
                            if (   (   (pElmSpBuild = pNode->findChildElement("SPBUILD")) != NULL
                                    || (pElmSpBuild = pNode->findChildElement("spbuild")) != NULL
                                    || (pElmSpBuild = pNode->findChildElement("Spbuild")) != NULL
                                    || (pElmSpBuild = pNode->findChildElement("SpBuild")) != NULL)
                                && pElmSpBuild->getValue()
                                && *pElmSpBuild->getValue() != '\0')
                                image.mVersion.printf("%s.%s.%s.%s", pszMajor, pszMinor, pszBuild, pElmSpBuild->getValue());
                            else
                                image.mVersion.printf("%s.%s.%s", pszMajor, pszMinor, pszBuild);

                            /*
                             * Convert that to a version windows OS ID (newest first!).
                             */
                            VBOXOSTYPE enmVersion = VBOXOSTYPE_Unknown;
                            if (RTStrVersionCompare(image.mVersion.c_str(), "10.0.22000.0") >= 0)
                                enmVersion = VBOXOSTYPE_Win11_x64;
                            else if (RTStrVersionCompare(image.mVersion.c_str(), "10.0") >= 0)
                                enmVersion = VBOXOSTYPE_Win10;
                            else if (RTStrVersionCompare(image.mVersion.c_str(), "6.3") >= 0)
                                enmVersion = VBOXOSTYPE_Win81;
                            else if (RTStrVersionCompare(image.mVersion.c_str(), "6.2") >= 0)
                                enmVersion = VBOXOSTYPE_Win8;
                            else if (RTStrVersionCompare(image.mVersion.c_str(), "6.1") >= 0)
                                enmVersion = VBOXOSTYPE_Win7;
                            else if (RTStrVersionCompare(image.mVersion.c_str(), "6.0") >= 0)
                                enmVersion = VBOXOSTYPE_WinVista;
                            if (image.mFlavor.contains("server", Utf8Str::CaseInsensitive))
                            {
                                if (RTStrVersionCompare(image.mVersion.c_str(), "10.0.20348") >= 0)
                                    enmVersion = VBOXOSTYPE_Win2k22_x64;
                                else if (RTStrVersionCompare(image.mVersion.c_str(), "10.0.17763") >= 0)
                                    enmVersion = VBOXOSTYPE_Win2k19_x64;
                                else if (RTStrVersionCompare(image.mVersion.c_str(), "10.0") >= 0)
                                    enmVersion = VBOXOSTYPE_Win2k16_x64;
                                else if (RTStrVersionCompare(image.mVersion.c_str(), "6.2") >= 0)
                                    enmVersion = VBOXOSTYPE_Win2k12_x64;
                                else if (RTStrVersionCompare(image.mVersion.c_str(), "6.0") >= 0)
                                    enmVersion = VBOXOSTYPE_Win2k8;
                            }
                            if (enmVersion != VBOXOSTYPE_Unknown)
                                image.mOSType = (VBOXOSTYPE)(  (image.mOSType & VBOXOSTYPE_ArchitectureMask)
                                                             | (enmVersion    & VBOXOSTYPE_OsMask));
                            return;
                        }
                    }
                }
            }
        }
    }
    Log(("Unattended: Warning! Bogus/missing version info for image #%u / %s\n", image.mImageIndex, image.mName.c_str()));
}

/**
 * Parses XML tree assuming th following structure
 * @verbatim
 * <WIM>
 *     ...
 *     <IMAGE INDEX="1">
 *         ...
 *         <DISPLAYNAME>Windows 10 Home</DISPLAYNAME>
 *         <WINDOWS>
 *             <ARCH>NN</ARCH>
 *             <VERSION>
 *                 ...
 *             </VERSION>
 *             <LANGUAGES>
 *                 <LANGUAGE>
 *                     en-US
 *                 </LANGUAGE>
 *                 <DEFAULT>
 *                     en-US
 *                 </DEFAULT>
 *             </LANGUAGES>
 *         </WINDOWS>
 *     </IMAGE>
 * </WIM>
 * @endverbatim
 *
 * @param   pElmRoot   Pointer to the root node of the tree,
 * @param   imageList  Detected images are appended to this list.
 */
static void parseWimXMLData(const xml::ElementNode *pElmRoot, RTCList<WIMImage> &imageList)
{
    if (!pElmRoot)
        return;

    ElementNodesList children;
    int cChildren = pElmRoot->getChildElements(children, "IMAGE");
    if (cChildren == 0)
        cChildren = pElmRoot->getChildElements(children, "image");
    if (cChildren == 0)
        cChildren = pElmRoot->getChildElements(children, "Image");

    for (ElementNodesList::iterator iterator = children.begin(); iterator != children.end(); ++iterator)
    {
        const ElementNode *pChild = *(iterator);
        if (!pChild)
            continue;

        WIMImage newImage;

        if (   !pChild->getAttributeValue("INDEX", &newImage.mImageIndex)
            && !pChild->getAttributeValue("index", &newImage.mImageIndex)
            && !pChild->getAttributeValue("Index", &newImage.mImageIndex))
            continue;

        const ElementNode *pElmName;
        if (   (pElmName = pChild->findChildElement("DISPLAYNAME")) == NULL
            && (pElmName = pChild->findChildElement("displayname")) == NULL
            && (pElmName = pChild->findChildElement("Displayname")) == NULL
            && (pElmName = pChild->findChildElement("DisplayName")) == NULL
                /* Early vista images didn't have DISPLAYNAME. */
            && (pElmName = pChild->findChildElement("NAME")) == NULL
            && (pElmName = pChild->findChildElement("name")) == NULL
            && (pElmName = pChild->findChildElement("Name")) == NULL)
            continue;
        newImage.mName = pElmName->getValue();
        if (newImage.mName.isEmpty())
            continue;

        const ElementNode *pElmWindows;
        if (   (pElmWindows = pChild->findChildElement("WINDOWS")) != NULL
            || (pElmWindows = pChild->findChildElement("windows")) != NULL
            || (pElmWindows = pChild->findChildElement("Windows")) != NULL)
        {
            /* Do edition/flags before the version so it can better determin
               the OS version enum value.  Old windows version (vista) typically
               doesn't have an EDITIONID element, so fall back on the FLAGS element
               under IMAGE as it is pretty similar (case differences). */
            const ElementNode *pElmEditionId;
            if (   (pElmEditionId = pElmWindows->findChildElement("EDITIONID")) != NULL
                || (pElmEditionId = pElmWindows->findChildElement("editionid")) != NULL
                || (pElmEditionId = pElmWindows->findChildElement("Editionid")) != NULL
                || (pElmEditionId = pElmWindows->findChildElement("EditionId")) != NULL
                || (pElmEditionId = pChild->findChildElement("FLAGS")) != NULL
                || (pElmEditionId = pChild->findChildElement("flags")) != NULL
                || (pElmEditionId = pChild->findChildElement("Flags")) != NULL)
                if (   pElmEditionId->getValue()
                    && *pElmEditionId->getValue() != '\0')
                    newImage.mFlavor = pElmEditionId->getValue();

            const ElementNode *pElmVersion;
            if (   (pElmVersion = pElmWindows->findChildElement("VERSION")) != NULL
                || (pElmVersion = pElmWindows->findChildElement("version")) != NULL
                || (pElmVersion = pElmWindows->findChildElement("Version")) != NULL)
                parseVersionElement(pElmVersion, newImage);

            /* The ARCH element contains a number from the
               PROCESSOR_ARCHITECTURE_XXX set of defines in winnt.h: */
            const ElementNode *pElmArch;
            if (   (pElmArch = pElmWindows->findChildElement("ARCH")) != NULL
                || (pElmArch = pElmWindows->findChildElement("arch")) != NULL
                || (pElmArch = pElmWindows->findChildElement("Arch")) != NULL)
                parseArchElement(pElmArch, newImage);

            /* Extract languages and default language: */
            const ElementNode *pElmLang;
            if (   (pElmLang = pElmWindows->findChildElement("LANGUAGES")) != NULL
                || (pElmLang = pElmWindows->findChildElement("languages")) != NULL
                || (pElmLang = pElmWindows->findChildElement("Languages")) != NULL)
                parseLangaguesElement(pElmLang, newImage);
        }


        imageList.append(newImage);
    }
}

/**
 * Detect Windows ISOs.
 *
 * @returns COM status code.
 * @retval  S_OK if detected
 * @retval  S_FALSE if not fully detected.
 *
 * @param   hVfsIso     The ISO file system.
 * @param   pBuf        Read buffer.
 */
HRESULT Unattended::i_innerDetectIsoOSWindows(RTVFS hVfsIso, DETECTBUFFER *pBuf)
{
    /** @todo The 'sources/' path can differ. */

    // globalinstallorder.xml - vista beta2
    // sources/idwbinfo.txt   - ditto.
    // sources/lang.ini       - ditto.

    /*
     * The install.wim file contains an XML document describing the install
     * images it contains.  This includes all the info we need for a successful
     * detection.
     */
    RTVFSFILE hVfsFile;
    int vrc = RTVfsFileOpen(hVfsIso, "sources/install.wim", RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN, &hVfsFile);
    if (RT_SUCCESS(vrc))
    {
        WIMHEADERV1 header;
        size_t cbRead = 0;
        vrc = RTVfsFileRead(hVfsFile, &header, sizeof(header), &cbRead);
        if (RT_SUCCESS(vrc) && cbRead == sizeof(header))
        {
            /* If the xml data is not compressed, xml data is not empty, and not too big. */
            if (    (header.XmlData.bFlags & RESHDR_FLAGS_METADATA)
                && !(header.XmlData.bFlags & RESHDR_FLAGS_COMPRESSED)
                &&  header.XmlData.cbOriginal >= 32
                &&  header.XmlData.cbOriginal < _32M
                &&  header.XmlData.cbOriginal == header.XmlData.cb)
            {
                size_t const cbXmlData = (size_t)header.XmlData.cbOriginal;
                char *pachXmlBuf = (char *)RTMemTmpAlloc(cbXmlData);
                if (pachXmlBuf)
                {
                    vrc = RTVfsFileReadAt(hVfsFile, (RTFOFF)header.XmlData.off, pachXmlBuf, cbXmlData, NULL);
                    if (RT_SUCCESS(vrc))
                    {
                        LogRel2(("Unattended: XML Data (%#zx bytes):\n%32.*Rhxd\n", cbXmlData, cbXmlData, pachXmlBuf));

                        /* Parse the XML: */
                        xml::Document doc;
                        xml::XmlMemParser parser;
                        try
                        {
                            RTCString strFileName = "source/install.wim";
                            parser.read(pachXmlBuf, cbXmlData, strFileName, doc);
                        }
                        catch (xml::XmlError &rErr)
                        {
                            LogRel(("Unattended: An error has occured during XML parsing: %s\n", rErr.what()));
                            vrc = VERR_XAR_TOC_XML_PARSE_ERROR;
                        }
                        catch (std::bad_alloc &)
                        {
                            LogRel(("Unattended: std::bad_alloc\n"));
                            vrc = VERR_NO_MEMORY;
                        }
                        catch (...)
                        {
                            LogRel(("Unattended: An unknown error has occured during XML parsing.\n"));
                            vrc = VERR_UNEXPECTED_EXCEPTION;
                        }
                        if (RT_SUCCESS(vrc))
                        {
                            /* Extract the information we need from the XML document: */
                            xml::ElementNode *pElmRoot = doc.getRootElement();
                            if (pElmRoot)
                            {
                                Assert(mDetectedImages.size() == 0);
                                try
                                {
                                    mDetectedImages.clear(); /* debugging convenience  */
                                    parseWimXMLData(pElmRoot, mDetectedImages);
                                }
                                catch (std::bad_alloc &)
                                {
                                    vrc = VERR_NO_MEMORY;
                                }

                                /*
                                 * If we found images, update the detected info attributes.
                                 */
                                if (RT_SUCCESS(vrc) && mDetectedImages.size() > 0)
                                {
                                    size_t i;
                                    for (i = 0; i < mDetectedImages.size(); i++)
                                        if (mDetectedImages[i].mImageIndex == midxImage)
                                            break;
                                    if (i >= mDetectedImages.size())
                                        i = 0; /* use the first one if midxImage wasn't found */
                                    if (i_updateDetectedAttributeForImage(mDetectedImages[i]))
                                    {
                                        LogRel2(("Unattended: happy with mDetectedImages[%u]\n", i));
                                        mEnmOsType = mDetectedImages[i].mOSType;
                                        return S_OK;
                                    }
                                }
                            }
                            else
                                LogRel(("Unattended: No root element found in XML Metadata of install.wim\n"));
                        }
                    }
                    else
                        LogRel(("Unattended: Failed during reading XML Metadata out of install.wim\n"));
                    RTMemTmpFree(pachXmlBuf);
                }
                else
                {
                    LogRel(("Unattended: Failed to allocate %#zx bytes for XML Metadata\n", cbXmlData));
                    vrc = VERR_NO_TMP_MEMORY;
                }
            }
            else
                LogRel(("Unattended: XML Metadata of install.wim is either compressed, empty, or too big (bFlags=%#x cbOriginal=%#RX64 cb=%#RX64)\n",
                        header.XmlData.bFlags, header.XmlData.cbOriginal, header.XmlData.cb));
        }
        RTVfsFileRelease(hVfsFile);

        /* Bail out if we ran out of memory here. */
        if (vrc == VERR_NO_MEMORY || vrc == VERR_NO_TMP_MEMORY)
            return setErrorBoth(E_OUTOFMEMORY, vrc, tr("Out of memory"));
    }

    const char *pszVersion = NULL;
    const char *pszProduct = NULL;
    /*
     * Try look for the 'sources/idwbinfo.txt' file containing windows build info.
     * This file appeared with Vista beta 2 from what we can tell.  Before windows 10
     * it contains easily decodable branch names, after that things goes weird.
     */
    vrc = RTVfsFileOpen(hVfsIso, "sources/idwbinfo.txt", RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN, &hVfsFile);
    if (RT_SUCCESS(vrc))
    {
        mEnmOsType = VBOXOSTYPE_WinNT_x64;

        RTINIFILE hIniFile;
        vrc = RTIniFileCreateFromVfsFile(&hIniFile, hVfsFile, RTINIFILE_F_READONLY);
        RTVfsFileRelease(hVfsFile);
        if (RT_SUCCESS(vrc))
        {
            vrc = RTIniFileQueryValue(hIniFile, "BUILDINFO", "BuildArch", pBuf->sz, sizeof(*pBuf), NULL);
            if (RT_SUCCESS(vrc))
            {
                LogRelFlow(("Unattended: sources/idwbinfo.txt: BuildArch=%s\n", pBuf->sz));
                if (   RTStrNICmp(pBuf->sz, RT_STR_TUPLE("amd64")) == 0
                    || RTStrNICmp(pBuf->sz, RT_STR_TUPLE("x64"))   == 0 /* just in case */ )
                    mEnmOsType = VBOXOSTYPE_WinNT_x64;
                else if (RTStrNICmp(pBuf->sz, RT_STR_TUPLE("x86")) == 0)
                    mEnmOsType = VBOXOSTYPE_WinNT;
                else
                {
                    LogRel(("Unattended: sources/idwbinfo.txt: Unknown: BuildArch=%s\n", pBuf->sz));
                    mEnmOsType = VBOXOSTYPE_WinNT_x64;
                }
            }

            vrc = RTIniFileQueryValue(hIniFile, "BUILDINFO", "BuildBranch", pBuf->sz, sizeof(*pBuf), NULL);
            if (RT_SUCCESS(vrc))
            {
                LogRelFlow(("Unattended: sources/idwbinfo.txt: BuildBranch=%s\n", pBuf->sz));
                if (   RTStrNICmp(pBuf->sz, RT_STR_TUPLE("vista")) == 0
                    || RTStrNICmp(pBuf->sz, RT_STR_TUPLE("winmain_beta")) == 0)
                    mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_WinVista);
                else if (RTStrNICmp(pBuf->sz, RT_STR_TUPLE("lh_sp2rtm")) == 0)
                {
                    mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_WinVista);
                    pszVersion = "sp2";
                }
                else if (RTStrNICmp(pBuf->sz, RT_STR_TUPLE("longhorn_rtm")) == 0)
                {
                    mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_WinVista);
                    pszVersion = "sp1";
                }
                else if (RTStrNICmp(pBuf->sz, RT_STR_TUPLE("win7")) == 0)
                    mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Win7);
                else if (   RTStrNICmp(pBuf->sz, RT_STR_TUPLE("winblue")) == 0
                         || RTStrNICmp(pBuf->sz, RT_STR_TUPLE("winmain_blue")) == 0
                         || RTStrNICmp(pBuf->sz, RT_STR_TUPLE("win81")) == 0 /* not seen, but just in case its out there */ )
                    mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Win81);
                else if (   RTStrNICmp(pBuf->sz, RT_STR_TUPLE("win8")) == 0
                         || RTStrNICmp(pBuf->sz, RT_STR_TUPLE("winmain_win8")) == 0 )
                    mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Win8);
                else if (RTStrNICmp(pBuf->sz, RT_STR_TUPLE("th1")) == 0)
                {
                    pszVersion = "1507";    // aka. GA, retroactively 1507
                    mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Win10);
                }
                else if (RTStrNICmp(pBuf->sz, RT_STR_TUPLE("th2")) == 0)
                {
                    pszVersion = "1511";    // aka. threshold 2
                    mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Win10);
                }
                else if (RTStrNICmp(pBuf->sz, RT_STR_TUPLE("rs1_release")) == 0)
                {
                    pszVersion = "1607";    // aka. anniversay update; rs=redstone
                    mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Win10);
                }
                else if (RTStrNICmp(pBuf->sz, RT_STR_TUPLE("rs2_release")) == 0)
                {
                    pszVersion = "1703";    // aka. creators update
                    mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Win10);
                }
                else if (RTStrNICmp(pBuf->sz, RT_STR_TUPLE("rs3_release")) == 0)
                {
                    pszVersion = "1709";    // aka. fall creators update
                    mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Win10);
                }
                else if (RTStrNICmp(pBuf->sz, RT_STR_TUPLE("rs4_release")) == 0)
                {
                    pszVersion = "1803";
                    mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Win10);
                }
                else if (RTStrNICmp(pBuf->sz, RT_STR_TUPLE("rs5_release")) == 0)
                {
                    pszVersion = "1809";
                    mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Win10);
                }
                else if (RTStrNICmp(pBuf->sz, RT_STR_TUPLE("19h1_release")) == 0)
                {
                    pszVersion = "1903";
                    mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Win10);
                }
                else if (RTStrNICmp(pBuf->sz, RT_STR_TUPLE("19h2_release")) == 0)
                {
                    pszVersion = "1909";    // ??
                    mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Win10);
                }
                else if (RTStrNICmp(pBuf->sz, RT_STR_TUPLE("20h1_release")) == 0)
                {
                    pszVersion = "2003";    // ??
                    mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Win10);
                }
                else if (RTStrNICmp(pBuf->sz, RT_STR_TUPLE("vb_release")) == 0)
                {
                    pszVersion = "2004";    // ?? vb=Vibranium
                    mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Win10);
                }
                else if (RTStrNICmp(pBuf->sz, RT_STR_TUPLE("20h2_release")) == 0)
                {
                    pszVersion = "2009";    // ??
                    mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Win10);
                }
                else if (RTStrNICmp(pBuf->sz, RT_STR_TUPLE("21h1_release")) == 0)
                {
                    pszVersion = "2103";    // ??
                    mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Win10);
                }
                else if (RTStrNICmp(pBuf->sz, RT_STR_TUPLE("21h2_release")) == 0)
                {
                    pszVersion = "2109";    // ??
                    mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Win10);
                }
                else if (RTStrNICmp(pBuf->sz, RT_STR_TUPLE("co_release")) == 0)
                {
                    pszVersion = "21H2";    // ??
                    mEnmOsType = VBOXOSTYPE_Win11_x64;
                }
                else
                    LogRel(("Unattended: sources/idwbinfo.txt: Unknown: BuildBranch=%s\n", pBuf->sz));
            }
            RTIniFileRelease(hIniFile);
        }
    }
    bool fClarifyProd = false;
    if (RT_FAILURE(vrc))
    {
        /*
         * Check a INF file with a DriverVer that is updated with each service pack.
         *      DriverVer=10/01/2002,5.2.3790.3959
         */
        vrc = RTVfsFileOpen(hVfsIso, "AMD64/HIVESYS.INF", RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN, &hVfsFile);
        if (RT_SUCCESS(vrc))
            mEnmOsType = VBOXOSTYPE_WinNT_x64;
        else
        {
            vrc = RTVfsFileOpen(hVfsIso, "I386/HIVESYS.INF", RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN, &hVfsFile);
            if (RT_SUCCESS(vrc))
                mEnmOsType = VBOXOSTYPE_WinNT;
        }
        if (RT_SUCCESS(vrc))
        {
            RTINIFILE hIniFile;
            vrc = RTIniFileCreateFromVfsFile(&hIniFile, hVfsFile, RTINIFILE_F_READONLY);
            RTVfsFileRelease(hVfsFile);
            if (RT_SUCCESS(vrc))
            {
                vrc = RTIniFileQueryValue(hIniFile, "Version", "DriverVer", pBuf->sz, sizeof(*pBuf), NULL);
                if (RT_SUCCESS(vrc))
                {
                    LogRelFlow(("Unattended: HIVESYS.INF: DriverVer=%s\n", pBuf->sz));
                    const char *psz = strchr(pBuf->sz, ',');
                    psz = psz ? psz + 1 : pBuf->sz;
                    if (RTStrVersionCompare(psz, "6.0.0") >= 0)
                        LogRel(("Unattended: HIVESYS.INF: unknown: DriverVer=%s\n", psz));
                    else if (RTStrVersionCompare(psz, "5.2.0") >= 0) /* W2K3, XP64 */
                    {
                        fClarifyProd = true;
                        mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Win2k3);
                        if (RTStrVersionCompare(psz, "5.2.3790.3959") >= 0)
                            pszVersion = "sp2";
                        else if (RTStrVersionCompare(psz, "5.2.3790.1830") >= 0)
                            pszVersion = "sp1";
                    }
                    else if (RTStrVersionCompare(psz, "5.1.0") >= 0) /* XP */
                    {
                        mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_WinXP);
                        if (RTStrVersionCompare(psz, "5.1.2600.5512") >= 0)
                            pszVersion = "sp3";
                        else if (RTStrVersionCompare(psz, "5.1.2600.2180") >= 0)
                            pszVersion = "sp2";
                        else if (RTStrVersionCompare(psz, "5.1.2600.1105") >= 0)
                            pszVersion = "sp1";
                    }
                    else if (RTStrVersionCompare(psz, "5.0.0") >= 0)
                    {
                        mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Win2k);
                        if (RTStrVersionCompare(psz, "5.0.2195.6717") >= 0)
                            pszVersion = "sp4";
                        else if (RTStrVersionCompare(psz, "5.0.2195.5438") >= 0)
                            pszVersion = "sp3";
                        else if (RTStrVersionCompare(psz, "5.0.2195.1620") >= 0)
                            pszVersion = "sp1";
                    }
                    else
                        LogRel(("Unattended: HIVESYS.INF: unknown: DriverVer=%s\n", psz));
                }
                RTIniFileRelease(hIniFile);
            }
        }
    }
    if (RT_FAILURE(vrc) || fClarifyProd)
    {
        /*
         * NT 4 and older does not have DriverVer entries, we consult the PRODSPEC.INI, which
         * works for NT4 & W2K. It does usually not reflect the service pack.
         */
        vrc = RTVfsFileOpen(hVfsIso, "AMD64/PRODSPEC.INI", RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN, &hVfsFile);
        if (RT_SUCCESS(vrc))
            mEnmOsType = VBOXOSTYPE_WinNT_x64;
        else
        {
            vrc = RTVfsFileOpen(hVfsIso, "I386/PRODSPEC.INI", RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN, &hVfsFile);
            if (RT_SUCCESS(vrc))
                mEnmOsType = VBOXOSTYPE_WinNT;
        }
        if (RT_SUCCESS(vrc))
        {

            RTINIFILE hIniFile;
            vrc = RTIniFileCreateFromVfsFile(&hIniFile, hVfsFile, RTINIFILE_F_READONLY);
            RTVfsFileRelease(hVfsFile);
            if (RT_SUCCESS(vrc))
            {
                vrc = RTIniFileQueryValue(hIniFile, "Product Specification", "Version", pBuf->sz, sizeof(*pBuf), NULL);
                if (RT_SUCCESS(vrc))
                {
                    LogRelFlow(("Unattended: PRODSPEC.INI: Version=%s\n", pBuf->sz));
                    if (RTStrVersionCompare(pBuf->sz, "5.1") >= 0) /* Shipped with XP + W2K3, but version stuck at 5.0. */
                        LogRel(("Unattended: PRODSPEC.INI: unknown: DriverVer=%s\n", pBuf->sz));
                    else if (RTStrVersionCompare(pBuf->sz, "5.0") >= 0) /* 2000 */
                    {
                        vrc = RTIniFileQueryValue(hIniFile, "Product Specification", "Product", pBuf->sz, sizeof(*pBuf), NULL);
                        if (RT_SUCCESS(vrc) && RTStrNICmp(pBuf->sz, RT_STR_TUPLE("Windows XP")) == 0)
                            mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_WinXP);
                        else if (RT_SUCCESS(vrc) && RTStrNICmp(pBuf->sz, RT_STR_TUPLE("Windows Server 2003")) == 0)
                            mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Win2k3);
                        else
                            mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Win2k);

                        if (RT_SUCCESS(vrc) && (strstr(pBuf->sz, "Server") || strstr(pBuf->sz, "server")))
                            pszProduct = "Server";
                    }
                    else if (RTStrVersionCompare(pBuf->sz, "4.0") >= 0) /* NT4 */
                        mEnmOsType = VBOXOSTYPE_WinNT4;
                    else
                        LogRel(("Unattended: PRODSPEC.INI: unknown: DriverVer=%s\n", pBuf->sz));

                    vrc = RTIniFileQueryValue(hIniFile, "Product Specification", "ProductType", pBuf->sz, sizeof(*pBuf), NULL);
                    if (RT_SUCCESS(vrc))
                        pszProduct = strcmp(pBuf->sz, "0") == 0 ? "Workstation" : /* simplification: */ "Server";
                }
                RTIniFileRelease(hIniFile);
            }
        }
        if (fClarifyProd)
            vrc = VINF_SUCCESS;
    }
    if (RT_FAILURE(vrc))
    {
        /*
         * NT 3.x we look at the LoadIdentifier (boot manager) string in TXTSETUP.SIF/TXT.
         */
        vrc = RTVfsFileOpen(hVfsIso, "I386/TXTSETUP.SIF", RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN, &hVfsFile);
        if (RT_FAILURE(vrc))
            vrc = RTVfsFileOpen(hVfsIso, "I386/TXTSETUP.INF", RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN, &hVfsFile);
        if (RT_SUCCESS(vrc))
        {
            mEnmOsType = VBOXOSTYPE_WinNT;

            RTINIFILE hIniFile;
            vrc = RTIniFileCreateFromVfsFile(&hIniFile, hVfsFile, RTINIFILE_F_READONLY);
            RTVfsFileRelease(hVfsFile);
            if (RT_SUCCESS(vrc))
            {
                vrc = RTIniFileQueryValue(hIniFile, "SetupData", "ProductType", pBuf->sz, sizeof(*pBuf), NULL);
                if (RT_SUCCESS(vrc))
                    pszProduct = strcmp(pBuf->sz, "0") == 0 ? "Workstation" : /* simplification: */ "Server";

                vrc = RTIniFileQueryValue(hIniFile, "SetupData", "LoadIdentifier", pBuf->sz, sizeof(*pBuf), NULL);
                if (RT_SUCCESS(vrc))
                {
                    LogRelFlow(("Unattended: TXTSETUP.SIF: LoadIdentifier=%s\n", pBuf->sz));
                    char *psz = pBuf->sz;
                    while (!RT_C_IS_DIGIT(*psz) && *psz)
                        psz++;
                    char *psz2 = psz;
                    while (RT_C_IS_DIGIT(*psz2) || *psz2 == '.')
                        psz2++;
                    *psz2 = '\0';
                    if (RTStrVersionCompare(psz, "6.0") >= 0)
                        LogRel(("Unattended: TXTSETUP.SIF: unknown: LoadIdentifier=%s\n", pBuf->sz));
                    else if (RTStrVersionCompare(psz, "4.0") >= 0)
                        mEnmOsType = VBOXOSTYPE_WinNT4;
                    else if (RTStrVersionCompare(psz, "3.1") >= 0)
                    {
                        mEnmOsType = VBOXOSTYPE_WinNT3x;
                        pszVersion = psz;
                    }
                    else
                        LogRel(("Unattended: TXTSETUP.SIF: unknown: LoadIdentifier=%s\n", pBuf->sz));
                }
                RTIniFileRelease(hIniFile);
            }
        }
    }

    if (pszVersion)
        try { mStrDetectedOSVersion = pszVersion; }
        catch (std::bad_alloc &) { return E_OUTOFMEMORY; }
    if (pszProduct)
        try { mStrDetectedOSFlavor = pszProduct; }
        catch (std::bad_alloc &) { return E_OUTOFMEMORY; }

    /*
     * Look for sources/lang.ini and try parse it to get the languages out of it.
     */
    /** @todo We could also check sources/??-* and boot/??-* if lang.ini is not
     *        found or unhelpful. */
    vrc = RTVfsFileOpen(hVfsIso, "sources/lang.ini", RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN, &hVfsFile);
    if (RT_SUCCESS(vrc))
    {
        RTINIFILE hIniFile;
        vrc = RTIniFileCreateFromVfsFile(&hIniFile, hVfsFile, RTINIFILE_F_READONLY);
        RTVfsFileRelease(hVfsFile);
        if (RT_SUCCESS(vrc))
        {
            mDetectedOSLanguages.clear();

            uint32_t idxPair;
            for (idxPair = 0; idxPair < 256; idxPair++)
            {
                size_t cbHalf   = sizeof(*pBuf) / 2;
                char  *pszKey   = pBuf->sz;
                char  *pszValue = &pBuf->sz[cbHalf];
                vrc = RTIniFileQueryPair(hIniFile, "Available UI Languages", idxPair,
                                         pszKey, cbHalf, NULL, pszValue, cbHalf, NULL);
                if (RT_SUCCESS(vrc))
                {
                    try
                    {
                        mDetectedOSLanguages.append(pszKey);
                    }
                    catch (std::bad_alloc &)
                    {
                        RTIniFileRelease(hIniFile);
                        return E_OUTOFMEMORY;
                    }
                }
                else if (vrc == VERR_NOT_FOUND)
                    break;
                else
                    Assert(vrc == VERR_BUFFER_OVERFLOW);
            }
            if (idxPair == 0)
                LogRel(("Unattended: Warning! Empty 'Available UI Languages' section in sources/lang.ini\n"));
            RTIniFileRelease(hIniFile);
        }
    }

    return S_FALSE;
}

/**
 * Architecture strings for Linux and the like.
 */
static struct { const char *pszArch; uint32_t cchArch; VBOXOSTYPE fArch; } const g_aLinuxArches[] =
{
    { RT_STR_TUPLE("amd64"),   VBOXOSTYPE_x64 },
    { RT_STR_TUPLE("x86_64"),  VBOXOSTYPE_x64 },
    { RT_STR_TUPLE("x86-64"),  VBOXOSTYPE_x64 }, /* just in case */
    { RT_STR_TUPLE("x64"),     VBOXOSTYPE_x64 }, /* ditto */

    { RT_STR_TUPLE("arm"),     VBOXOSTYPE_arm64 },
    { RT_STR_TUPLE("arm64"),   VBOXOSTYPE_arm64 },
    { RT_STR_TUPLE("arm-64"),  VBOXOSTYPE_arm64 },
    { RT_STR_TUPLE("arm_64"),  VBOXOSTYPE_arm64 },
    { RT_STR_TUPLE("aarch64"), VBOXOSTYPE_arm64 }, /* mostly RHEL. */

    { RT_STR_TUPLE("arm32"),   VBOXOSTYPE_arm32 },
    { RT_STR_TUPLE("arm-32"),  VBOXOSTYPE_arm32 },
    { RT_STR_TUPLE("arm_32"),  VBOXOSTYPE_arm32 },
    { RT_STR_TUPLE("armel"),   VBOXOSTYPE_arm32 }, /* mostly Debians. */

    { RT_STR_TUPLE("x86"),     VBOXOSTYPE_x86 },
    { RT_STR_TUPLE("i386"),    VBOXOSTYPE_x86 },
    { RT_STR_TUPLE("i486"),    VBOXOSTYPE_x86 },
    { RT_STR_TUPLE("i586"),    VBOXOSTYPE_x86 },
    { RT_STR_TUPLE("i686"),    VBOXOSTYPE_x86 },
    { RT_STR_TUPLE("i786"),    VBOXOSTYPE_x86 },
    { RT_STR_TUPLE("i886"),    VBOXOSTYPE_x86 },
    { RT_STR_TUPLE("i986"),    VBOXOSTYPE_x86 },
};

/**
 * Detects linux architecture.
 *
 * @returns true if detected, false if not.
 * @param   pszArch             The architecture string.
 * @param   penmOsType          Where to return the arch and type on success.
 * @param   enmBaseOsType       The base (x86) OS type to return.
 */
static bool detectLinuxArch(const char *pszArch, VBOXOSTYPE *penmOsType, VBOXOSTYPE enmBaseOsType)
{
    for (size_t i = 0; i < RT_ELEMENTS(g_aLinuxArches); i++)
        if (RTStrNICmp(pszArch, g_aLinuxArches[i].pszArch, g_aLinuxArches[i].cchArch) == 0)
        {
            *penmOsType = (VBOXOSTYPE)(enmBaseOsType | g_aLinuxArches[i].fArch);
            return true;
        }
    /** @todo check for 'noarch' since source CDs have been seen to use that. */
    return false;
}

/**
 * Detects linux architecture by searching for the architecture substring in @p pszArch.
 *
 * @returns true if detected, false if not.
 * @param   pszArch             The architecture string.
 * @param   penmOsType          Where to return the arch and type on success.
 * @param   enmBaseOsType       The base (x86) OS type to return.
 * @param   ppszHit             Where to return the pointer to the architecture
 *                              specifier. Optional.
 * @param   ppszNext            Where to return the pointer to the char
 *                              following the architecuture specifier. Optional.
 */
static bool detectLinuxArchII(const char *pszArch, VBOXOSTYPE *penmOsType, VBOXOSTYPE enmBaseOsType,
                              char **ppszHit = NULL, char **ppszNext = NULL)
{
    for (size_t i = 0; i < RT_ELEMENTS(g_aLinuxArches); i++)
    {
        const char *pszHit = RTStrIStr(pszArch, g_aLinuxArches[i].pszArch);
        if (pszHit != NULL)
        {
            if (ppszHit)
                *ppszHit = (char *)pszHit;
            if (ppszNext)
                *ppszNext = (char *)pszHit + g_aLinuxArches[i].cchArch;
            *penmOsType = (VBOXOSTYPE)(enmBaseOsType | g_aLinuxArches[i].fArch);
            return true;
        }
    }
    return false;
}

static bool detectLinuxDistroName(const char *pszOsAndVersion, VBOXOSTYPE *penmOsType, const char **ppszNext)
{
    bool fRet = true;

    if (    RTStrNICmp(pszOsAndVersion, RT_STR_TUPLE("Red")) == 0
        && !RT_C_IS_ALNUM(pszOsAndVersion[3]))

    {
        pszOsAndVersion = RTStrStripL(pszOsAndVersion + 3);
        if (   RTStrNICmp(pszOsAndVersion, RT_STR_TUPLE("Hat")) == 0
            && !RT_C_IS_ALNUM(pszOsAndVersion[3]))
        {
            *penmOsType = (VBOXOSTYPE)((*penmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_RedHat);
            pszOsAndVersion = RTStrStripL(pszOsAndVersion + 3);
        }
        else
            fRet = false;
    }
    else if (   RTStrNICmp(pszOsAndVersion, RT_STR_TUPLE("OpenSUSE")) == 0
             && !RT_C_IS_ALNUM(pszOsAndVersion[8]))
    {
        *penmOsType = (VBOXOSTYPE)((*penmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_OpenSUSE);
        pszOsAndVersion = RTStrStripL(pszOsAndVersion + 8);
    }
    else if (   RTStrNICmp(pszOsAndVersion, RT_STR_TUPLE("Oracle")) == 0
             && !RT_C_IS_ALNUM(pszOsAndVersion[6]))
    {
        *penmOsType = (VBOXOSTYPE)((*penmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Oracle);
        pszOsAndVersion = RTStrStripL(pszOsAndVersion + 6);
    }
    else if (   RTStrNICmp(pszOsAndVersion, RT_STR_TUPLE("CentOS")) == 0
             && !RT_C_IS_ALNUM(pszOsAndVersion[6]))
    {
        *penmOsType = (VBOXOSTYPE)((*penmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_RedHat);
        pszOsAndVersion = RTStrStripL(pszOsAndVersion + 6);
    }
    else if (   RTStrNICmp(pszOsAndVersion, RT_STR_TUPLE("Fedora")) == 0
             && !RT_C_IS_ALNUM(pszOsAndVersion[6]))
    {
        *penmOsType = (VBOXOSTYPE)((*penmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_FedoraCore);
        pszOsAndVersion = RTStrStripL(pszOsAndVersion + 6);
    }
    else if (   RTStrNICmp(pszOsAndVersion, RT_STR_TUPLE("Ubuntu")) == 0
             && !RT_C_IS_ALNUM(pszOsAndVersion[6]))
    {
        *penmOsType = (VBOXOSTYPE)((*penmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Ubuntu);
        pszOsAndVersion = RTStrStripL(pszOsAndVersion + 6);
    }
    else if (   RTStrNICmp(pszOsAndVersion, RT_STR_TUPLE("Linux Mint")) == 0
             && !RT_C_IS_ALNUM(pszOsAndVersion[10]))
    {
        *penmOsType = (VBOXOSTYPE)((*penmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Ubuntu);
        pszOsAndVersion = RTStrStripL(pszOsAndVersion + 10);
    }
    else if (    (   RTStrNICmp(pszOsAndVersion, RT_STR_TUPLE("Xubuntu")) == 0
                  || RTStrNICmp(pszOsAndVersion, RT_STR_TUPLE("Kubuntu")) == 0
                  || RTStrNICmp(pszOsAndVersion, RT_STR_TUPLE("Lubuntu")) == 0)
             && !RT_C_IS_ALNUM(pszOsAndVersion[7]))
    {
        *penmOsType = (VBOXOSTYPE)((*penmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Ubuntu);
        pszOsAndVersion = RTStrStripL(pszOsAndVersion + 7);
    }
    else if (   RTStrNICmp(pszOsAndVersion, RT_STR_TUPLE("Debian")) == 0
             && !RT_C_IS_ALNUM(pszOsAndVersion[6]))
    {
        *penmOsType = (VBOXOSTYPE)((*penmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Debian);
        pszOsAndVersion = RTStrStripL(pszOsAndVersion + 6);
    }
    else
        fRet = false;

    /*
     * Skip forward till we get a number.
     */
    if (ppszNext)
    {
        *ppszNext = pszOsAndVersion;
        char ch;
        for (const char *pszVersion = pszOsAndVersion; (ch = *pszVersion) != '\0'; pszVersion++)
            if (RT_C_IS_DIGIT(ch))
            {
                *ppszNext = pszVersion;
                break;
            }
    }
    return fRet;
}

static bool detectLinuxDistroNameII(const char *pszOsAndVersion, VBOXOSTYPE *penmOsType, const char **ppszNext)
{
    bool fRet = true;
    if (   RTStrIStr(pszOsAndVersion, "RedHat")  != NULL
        || RTStrIStr(pszOsAndVersion, "Red Hat") != NULL)
        *penmOsType = (VBOXOSTYPE)((*penmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_RedHat);
    else if (RTStrIStr(pszOsAndVersion, "Oracle") != NULL)
        *penmOsType = (VBOXOSTYPE)((*penmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Oracle);
    else if (RTStrIStr(pszOsAndVersion, "CentOS") != NULL)
        *penmOsType = (VBOXOSTYPE)((*penmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_RedHat);
    else if (RTStrIStr(pszOsAndVersion, "Fedora") != NULL)
        *penmOsType = (VBOXOSTYPE)((*penmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_FedoraCore);
    else if (RTStrIStr(pszOsAndVersion, "Ubuntu") != NULL)
        *penmOsType = (VBOXOSTYPE)((*penmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Ubuntu);
    else if (RTStrIStr(pszOsAndVersion, "Mint") != NULL)
        *penmOsType = (VBOXOSTYPE)((*penmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Ubuntu);
    else if (RTStrIStr(pszOsAndVersion, "Debian"))
        *penmOsType = (VBOXOSTYPE)((*penmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_Debian);
    else
        fRet = false;

    /*
     * Skip forward till we get a number.
     */
    if (ppszNext)
    {
        *ppszNext = pszOsAndVersion;
        char ch;
        for (const char *pszVersion = pszOsAndVersion; (ch = *pszVersion) != '\0'; pszVersion++)
            if (RT_C_IS_DIGIT(ch))
            {
                *ppszNext = pszVersion;
                break;
            }
    }
    return fRet;
}


/**
 * Helps detecting linux distro flavor by finding substring position of non numerical
 * part of the disk name.
 *
 * @returns true if detected, false if not.
 * @param   pszDiskName Name of the disk as it is read from .disk/info or
 *                      README.diskdefines file.
 * @param   poffVersion String position where first numerical character is
 *                      found. We use substring upto this position as OS flavor
 */
static bool detectLinuxDistroFlavor(const char *pszDiskName, size_t *poffVersion)
{
    Assert(poffVersion);
    if (!pszDiskName)
        return false;
    char ch;
    while ((ch = *pszDiskName) != '\0' && !RT_C_IS_DIGIT(ch))
    {
        ++pszDiskName;
        *poffVersion += 1;
    }
    return true;
}

/**
 * Detect Linux distro ISOs.
 *
 * @returns COM status code.
 * @retval  S_OK if detected
 * @retval  S_FALSE if not fully detected.
 *
 * @param   hVfsIso     The ISO file system.
 * @param   pBuf        Read buffer.
 */
HRESULT Unattended::i_innerDetectIsoOSLinux(RTVFS hVfsIso, DETECTBUFFER *pBuf)
{
    /*
     * Redhat and derivatives may have a .treeinfo (ini-file style) with useful info
     * or at least a barebone .discinfo file.
     */

    /*
     * Start with .treeinfo: https://release-engineering.github.io/productmd/treeinfo-1.0.html
     */
    RTVFSFILE hVfsFile;
    int vrc = RTVfsFileOpen(hVfsIso, ".treeinfo", RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN, &hVfsFile);
    if (RT_SUCCESS(vrc))
    {
        RTINIFILE hIniFile;
        vrc = RTIniFileCreateFromVfsFile(&hIniFile, hVfsFile, RTINIFILE_F_READONLY);
        RTVfsFileRelease(hVfsFile);
        if (RT_SUCCESS(vrc))
        {
            /* Try figure the architecture first (like with windows). */
            vrc = RTIniFileQueryValue(hIniFile, "tree", "arch", pBuf->sz, sizeof(*pBuf), NULL);
            if (RT_FAILURE(vrc) || !pBuf->sz[0])
                vrc = RTIniFileQueryValue(hIniFile, "general", "arch", pBuf->sz, sizeof(*pBuf), NULL);
            if (RT_FAILURE(vrc))
                LogRel(("Unattended: .treeinfo: No 'arch' property.\n"));
            else
            {
                LogRelFlow(("Unattended: .treeinfo: arch=%s\n", pBuf->sz));
                if (detectLinuxArch(pBuf->sz, &mEnmOsType, VBOXOSTYPE_RedHat))
                {
                    /* Try figure the release name, it doesn't have to be redhat. */
                    vrc = RTIniFileQueryValue(hIniFile, "release", "name", pBuf->sz, sizeof(*pBuf), NULL);
                    if (RT_FAILURE(vrc) || !pBuf->sz[0])
                        vrc = RTIniFileQueryValue(hIniFile, "product", "name", pBuf->sz, sizeof(*pBuf), NULL);
                    if (RT_FAILURE(vrc) || !pBuf->sz[0])
                        vrc = RTIniFileQueryValue(hIniFile, "general", "family", pBuf->sz, sizeof(*pBuf), NULL);
                    if (RT_SUCCESS(vrc))
                    {
                        LogRelFlow(("Unattended: .treeinfo: name/family=%s\n", pBuf->sz));
                        if (!detectLinuxDistroName(pBuf->sz, &mEnmOsType, NULL))
                        {
                            LogRel(("Unattended: .treeinfo: Unknown: name/family='%s', assuming Red Hat\n", pBuf->sz));
                            mEnmOsType = (VBOXOSTYPE)((mEnmOsType & VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_RedHat);
                        }
                    }

                    /* Try figure the version. */
                    vrc = RTIniFileQueryValue(hIniFile, "release", "version", pBuf->sz, sizeof(*pBuf), NULL);
                    if (RT_FAILURE(vrc) || !pBuf->sz[0])
                        vrc = RTIniFileQueryValue(hIniFile, "product", "version", pBuf->sz, sizeof(*pBuf), NULL);
                    if (RT_FAILURE(vrc) || !pBuf->sz[0])
                        vrc = RTIniFileQueryValue(hIniFile, "general", "version", pBuf->sz, sizeof(*pBuf), NULL);
                    if (RT_SUCCESS(vrc))
                    {
                        LogRelFlow(("Unattended: .treeinfo: version=%s\n", pBuf->sz));
                        try { mStrDetectedOSVersion = RTStrStrip(pBuf->sz); }
                        catch (std::bad_alloc &) { return E_OUTOFMEMORY; }

                        size_t cchVersionPosition = 0;
                        if (detectLinuxDistroFlavor(pBuf->sz, &cchVersionPosition))
                        {
                            try { mStrDetectedOSFlavor = Utf8Str(pBuf->sz, cchVersionPosition); }
                            catch (std::bad_alloc &) { return E_OUTOFMEMORY; }
                        }
                    }
                }
                else
                    LogRel(("Unattended: .treeinfo: Unknown: arch='%s'\n", pBuf->sz));
            }

            RTIniFileRelease(hIniFile);
        }

        if (mEnmOsType != VBOXOSTYPE_Unknown)
            return S_FALSE;
    }

    /*
     * Try .discinfo next: https://release-engineering.github.io/productmd/discinfo-1.0.html
     * We will probably need additional info here...
     */
    vrc = RTVfsFileOpen(hVfsIso, ".discinfo", RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN, &hVfsFile);
    if (RT_SUCCESS(vrc))
    {
        size_t cchIgn;
        vrc = RTVfsFileRead(hVfsFile, pBuf->sz, sizeof(*pBuf) - 1, &cchIgn);
        pBuf->sz[RT_SUCCESS(vrc) ? cchIgn : 0] = '\0';
        RTVfsFileRelease(hVfsFile);

        /* Parse and strip the first 5 lines. */
        const char *apszLines[5];
        char       *psz = pBuf->sz;
        for (unsigned i = 0; i < RT_ELEMENTS(apszLines); i++)
        {
            apszLines[i] = psz;
            if (*psz)
            {
                char *pszEol = (char *)strchr(psz, '\n');
                if (!pszEol)
                    psz = strchr(psz, '\0');
                else
                {
                    *pszEol = '\0';
                    apszLines[i] = RTStrStrip(psz);
                    psz = pszEol + 1;
                }
            }
        }

        /* Do we recognize the architecture? */
        LogRelFlow(("Unattended: .discinfo: arch=%s\n", apszLines[2]));
        if (detectLinuxArch(apszLines[2], &mEnmOsType, VBOXOSTYPE_RedHat))
        {
            /* Do we recognize the release string? */
            LogRelFlow(("Unattended: .discinfo: product+version=%s\n", apszLines[1]));
            const char *pszVersion = NULL;
            if (!detectLinuxDistroName(apszLines[1], &mEnmOsType, &pszVersion))
                LogRel(("Unattended: .discinfo: Unknown: release='%s'\n", apszLines[1]));

            if (*pszVersion)
            {
                LogRelFlow(("Unattended: .discinfo: version=%s\n", pszVersion));
                try { mStrDetectedOSVersion = RTStrStripL(pszVersion); }
                catch (std::bad_alloc &) { return E_OUTOFMEMORY; }

                /* CentOS likes to call their release 'Final' without mentioning the actual version
                   number (e.g. CentOS-4.7-x86_64-binDVD.iso), so we need to go look elsewhere.
                   This is only important for centos 4.x and 3.x releases. */
                if (RTStrNICmp(pszVersion, RT_STR_TUPLE("Final")) == 0)
                {
                    static const char * const s_apszDirs[] = { "CentOS/RPMS/", "RedHat/RPMS", "Server", "Workstation" };
                    for (unsigned iDir = 0; iDir < RT_ELEMENTS(s_apszDirs); iDir++)
                    {
                        RTVFSDIR hVfsDir;
                        vrc = RTVfsDirOpen(hVfsIso, s_apszDirs[iDir], 0, &hVfsDir);
                        if (RT_FAILURE(vrc))
                            continue;
                        char szRpmDb[128];
                        char szReleaseRpm[128];
                        szRpmDb[0] = '\0';
                        szReleaseRpm[0] = '\0';
                        for (;;)
                        {
                            RTDIRENTRYEX DirEntry;
                            size_t       cbDirEntry = sizeof(DirEntry);
                            vrc = RTVfsDirReadEx(hVfsDir, &DirEntry, &cbDirEntry, RTFSOBJATTRADD_NOTHING);
                            if (RT_FAILURE(vrc))
                                break;

                            /* redhat-release-4WS-2.4.i386.rpm
                               centos-release-4-7.x86_64.rpm, centos-release-4-4.3.i386.rpm
                               centos-release-5-3.el5.centos.1.x86_64.rpm */
                            if (   (psz = strstr(DirEntry.szName, "-release-")) != NULL
                                   || (psz = strstr(DirEntry.szName, "-RELEASE-")) != NULL)
                            {
                                psz += 9;
                                if (RT_C_IS_DIGIT(*psz))
                                    RTStrCopy(szReleaseRpm, sizeof(szReleaseRpm), psz);
                            }
                            /* rpmdb-redhat-4WS-2.4.i386.rpm,
                               rpmdb-CentOS-4.5-0.20070506.i386.rpm,
                               rpmdb-redhat-3.9-0.20070703.i386.rpm. */
                            else if (   (   RTStrStartsWith(DirEntry.szName, "rpmdb-")
                                            || RTStrStartsWith(DirEntry.szName, "RPMDB-"))
                                        && RT_C_IS_DIGIT(DirEntry.szName[6]) )
                                RTStrCopy(szRpmDb, sizeof(szRpmDb), &DirEntry.szName[6]);
                        }
                        RTVfsDirRelease(hVfsDir);

                        /* Did we find anything relvant? */
                        psz = szRpmDb;
                        if (!RT_C_IS_DIGIT(*psz))
                            psz = szReleaseRpm;
                        if (RT_C_IS_DIGIT(*psz))
                        {
                            /* Convert '-' to '.' and strip stuff which doesn't look like a version string. */
                            char *pszCur = psz + 1;
                            for (char ch = *pszCur; ch != '\0'; ch = *++pszCur)
                                if (ch == '-')
                                    *pszCur = '.';
                                else if (ch != '.' && !RT_C_IS_DIGIT(ch))
                                {
                                    *pszCur = '\0';
                                    break;
                                }
                            while (&pszCur[-1] != psz && pszCur[-1] == '.')
                                *--pszCur = '\0';

                            /* Set it and stop looking. */
                            try { mStrDetectedOSVersion = psz; }
                            catch (std::bad_alloc &) { return E_OUTOFMEMORY; }
                            break;
                        }
                    }
                }
            }
            size_t cchVersionPosition = 0;
            if (detectLinuxDistroFlavor(apszLines[1], &cchVersionPosition))
            {
                try { mStrDetectedOSFlavor = Utf8Str(apszLines[1], cchVersionPosition); }
                catch (std::bad_alloc &) { return E_OUTOFMEMORY; }
            }
        }
        else
            LogRel(("Unattended: .discinfo: Unknown: arch='%s'\n", apszLines[2]));

        if (mEnmOsType != VBOXOSTYPE_Unknown)
            return S_FALSE;
    }

    /*
     * Ubuntu has a README.diskdefines file on their ISO (already on 4.10 / warty warthog).
     * Example content:
     *  #define DISKNAME  Ubuntu 4.10 "Warty Warthog" - Preview amd64 Binary-1
     *  #define TYPE  binary
     *  #define TYPEbinary  1
     *  #define ARCH  amd64
     *  #define ARCHamd64  1
     *  #define DISKNUM  1
     *  #define DISKNUM1  1
     *  #define TOTALNUM  1
     *  #define TOTALNUM1  1
     */
    vrc = RTVfsFileOpen(hVfsIso, "README.diskdefines", RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN, &hVfsFile);
    if (RT_SUCCESS(vrc))
    {
        size_t cchIgn;
        vrc = RTVfsFileRead(hVfsFile, pBuf->sz, sizeof(*pBuf) - 1, &cchIgn);
        pBuf->sz[RT_SUCCESS(vrc) ? cchIgn : 0] = '\0';
        RTVfsFileRelease(hVfsFile);

        /* Find the DISKNAME and ARCH defines. */
        const char *pszDiskName = NULL;
        const char *pszArch     = NULL;
        char       *psz         = pBuf->sz;
        while (*psz != '\0')
        {
            while (RT_C_IS_BLANK(*psz))
                psz++;

            /* Match #define: */
            static const char s_szDefine[] = "#define";
            if (   strncmp(psz, s_szDefine, sizeof(s_szDefine) - 1) == 0
                && RT_C_IS_BLANK(psz[sizeof(s_szDefine) - 1]))
            {
                psz = &psz[sizeof(s_szDefine) - 1];
                while (RT_C_IS_BLANK(*psz))
                    psz++;

                /* Match the identifier: */
                char *pszIdentifier = psz;
                if (RT_C_IS_ALPHA(*psz) || *psz == '_')
                {
                    do
                        psz++;
                    while (RT_C_IS_ALNUM(*psz) || *psz == '_');
                    size_t cchIdentifier = (size_t)(psz - pszIdentifier);

                    /* Skip to the value. */
                    while (RT_C_IS_BLANK(*psz))
                        psz++;
                    char *pszValue = psz;

                    /* Skip to EOL and strip the value. */
                    char *pszEol = psz = strchr(psz, '\n');
                    if (psz)
                        *psz++ = '\0';
                    else
                        pszEol = strchr(pszValue, '\0');
                    while (pszEol > pszValue && RT_C_IS_SPACE(pszEol[-1]))
                        *--pszEol = '\0';

                    LogRelFlow(("Unattended: README.diskdefines: %.*s=%s\n", cchIdentifier, pszIdentifier, pszValue));

                    /* Do identifier matching: */
                    if (cchIdentifier == sizeof("DISKNAME") - 1 && strncmp(pszIdentifier, RT_STR_TUPLE("DISKNAME")) == 0)
                        pszDiskName = pszValue;
                    else if (cchIdentifier == sizeof("ARCH") - 1 && strncmp(pszIdentifier, RT_STR_TUPLE("ARCH")) == 0)
                        pszArch = pszValue;
                    else
                        continue;
                    if (pszDiskName == NULL || pszArch == NULL)
                        continue;
                    break;
                }
            }

            /* Next line: */
            psz = strchr(psz, '\n');
            if (!psz)
                break;
            psz++;
        }

        /* Did we find both of them? */
        if (pszDiskName && pszArch)
        {
            if (detectLinuxArch(pszArch, &mEnmOsType, VBOXOSTYPE_Ubuntu))
            {
                const char *pszVersion = NULL;
                if (detectLinuxDistroName(pszDiskName, &mEnmOsType, &pszVersion))
                {
                    LogRelFlow(("Unattended: README.diskdefines: version=%s\n", pszVersion));
                    try { mStrDetectedOSVersion = RTStrStripL(pszVersion); }
                    catch (std::bad_alloc &) { return E_OUTOFMEMORY; }

                    size_t cchVersionPosition = 0;
                    if (detectLinuxDistroFlavor(pszDiskName, &cchVersionPosition))
                    {
                        try { mStrDetectedOSFlavor = Utf8Str(pszDiskName, cchVersionPosition); }
                        catch (std::bad_alloc &) { return E_OUTOFMEMORY; }
                    }
                }
                else
                    LogRel(("Unattended: README.diskdefines: Unknown: diskname='%s'\n", pszDiskName));
            }
            else
                LogRel(("Unattended: README.diskdefines: Unknown: arch='%s'\n", pszArch));
        }
        else
            LogRel(("Unattended: README.diskdefines: Did not find both DISKNAME and ARCH. :-/\n"));

        if (mEnmOsType != VBOXOSTYPE_Unknown)
            return S_FALSE;
    }

    /*
     * All of the debian based distro versions I checked have a single line ./disk/info
     * file.  Only info I could find related to .disk folder is:
     *      https://lists.debian.org/debian-cd/2004/01/msg00069.html
     *
     * Some example content from several install ISOs is as follows:
     *   Ubuntu 4.10 "Warty Warthog" - Preview amd64 Binary-1 (20041020)
     *   Linux Mint 20.3 "Una" - Release amd64 20220104
     *   Debian GNU/Linux 11.2.0 "Bullseye" - Official amd64 NETINST 20211218-11:12
     *   Debian GNU/Linux 9.13.0 "Stretch" - Official amd64 DVD Binary-1 20200718-11:07
     *   Xubuntu 20.04.2.0 LTS "Focal Fossa" - Release amd64 (20210209.1)
     *   Ubuntu 17.10 "Artful Aardvark" - Release amd64 (20180105.1)
     *   Ubuntu 16.04.6 LTS "Xenial Xerus" - Release i386 (20190227.1)
     *   Debian GNU/Linux 8.11.1 "Jessie" - Official amd64 CD Binary-1 20190211-02:10
     *   Kali GNU/Linux 2021.3a "Kali-last-snapshot" - Official amd64 BD Binary-1 with firmware 20211015-16:55
     *   Official Debian GNU/Linux Live 10.10.0 cinnamon 2021-06-19T12:13
     *   Ubuntu 23.10.1 "Mantic Minotaur" - Release amd64 (20231016.1)
     *   Ubuntu-Server 22.04.3 LTS "Jammy Jellyfish" - Release amd64 (20230810)
     */
    vrc = RTVfsFileOpen(hVfsIso, ".disk/info", RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN, &hVfsFile);
    if (RT_SUCCESS(vrc))
    {
        size_t cchIgn;
        vrc = RTVfsFileRead(hVfsFile, pBuf->sz, sizeof(*pBuf) - 1, &cchIgn);
        pBuf->sz[RT_SUCCESS(vrc) ? cchIgn : 0] = '\0';

        pBuf->sz[sizeof(*pBuf) - 1] = '\0';
        RTVfsFileRelease(hVfsFile);

        char *psz         = pBuf->sz;
        char *pszDiskName = psz;
        char *pszArch     = NULL;

        /* Only care about the first line of the file even if it is multi line and assume disk name ended with ' - '.*/
        psz = RTStrStr(pBuf->sz, " - ");
        if (psz && memchr(pBuf->sz, '\n', (size_t)(psz - pBuf->sz)) == NULL)
        {
            *psz = '\0';
            psz += 3;
            if (*psz)
                pszArch = psz;
        }

        /* Some Debian Live ISO's have info file content as follows:
         * Official Debian GNU/Linux Live 10.10.0 cinnamon 2021-06-19T12:13
         * thus  pszArch stays empty. Try Volume Id (label) if we get lucky and get architecture from that. */
        if (!pszArch)
        {
            char szVolumeId[128];
            vrc = RTVfsQueryLabel(hVfsIso, false /*fAlternative*/, szVolumeId, sizeof(szVolumeId), NULL);
            if (RT_SUCCESS(vrc))
            {
                if (!detectLinuxArchII(szVolumeId, &mEnmOsType, VBOXOSTYPE_Ubuntu))
                    LogRel(("Unattended: .disk/info: Unknown: arch='%s'\n", szVolumeId));
            }
            else
                LogRel(("Unattended: .disk/info No Volume Label found\n"));
        }
        else
        {
            if (!detectLinuxArchII(pszArch, &mEnmOsType, VBOXOSTYPE_Ubuntu))
                LogRel(("Unattended: .disk/info: Unknown: arch='%s'\n", pszArch));
        }

        if (pszDiskName)
        {
            const char *pszVersion = NULL;
            if (detectLinuxDistroNameII(pszDiskName, &mEnmOsType, &pszVersion))
            {
                LogRelFlow(("Unattended: .disk/info: version=%s\n", pszVersion));
                try { mStrDetectedOSVersion = RTStrStripL(pszVersion); }
                catch (std::bad_alloc &) { return E_OUTOFMEMORY; }

                size_t cchVersionPosition = 0;
                if (detectLinuxDistroFlavor(pszDiskName, &cchVersionPosition))
                {
                    try { mStrDetectedOSFlavor = Utf8Str(pszDiskName, cchVersionPosition); }
                    catch (std::bad_alloc &) { return E_OUTOFMEMORY; }
                }
            }
            else
                LogRel(("Unattended: .disk/info: Unknown: diskname='%s'\n", pszDiskName));
        }

        if (mEnmOsType == VBOXOSTYPE_Unknown)
            LogRel(("Unattended: .disk/info: Did not find DISKNAME or/and ARCH. :-/\n"));
        else
            return S_FALSE;
    }

    /*
     * Fedora live iso should be recognizable from the primary volume ID (the
     * joliet one is usually truncated).  We set fAlternative = true here to
     * get the primary volume ID.
     */
    char szVolumeId[128];
    vrc = RTVfsQueryLabel(hVfsIso, true /*fAlternative*/, szVolumeId, sizeof(szVolumeId), NULL);
    if (RT_SUCCESS(vrc) && RTStrStartsWith(szVolumeId, "Fedora-"))
        return i_innerDetectIsoOSLinuxFedora(hVfsIso, pBuf, &szVolumeId[sizeof("Fedora-") - 1]);
    return S_FALSE;
}


/**
 * Continues working a Fedora ISO image after the caller found a "Fedora-*"
 * volume ID.
 *
 * Sample Volume IDs:
 *  - Fedora-WS-Live-34-1-2  (joliet: Fedora-WS-Live-3)
 *  - Fedora-S-dvd-x86_64-34 (joliet: Fedora-S-dvd-x86)
 *  - Fedora-WS-dvd-i386-25  (joliet: Fedora-WS-dvd-i3)
 */
HRESULT Unattended::i_innerDetectIsoOSLinuxFedora(RTVFS hVfsIso, DETECTBUFFER *pBuf, char *pszVolId)
{
    char * const pszFlavor = pszVolId;
    char *       psz       = pszVolId;

    /* The volume id may or may not include an arch, component.
       We ASSUME that it includes a numeric part with the version, or at least
       part of it. */
    char *pszVersion = NULL;
    char *pszArch    = NULL;
    if (detectLinuxArchII(psz, &mEnmOsType, VBOXOSTYPE_FedoraCore, &pszArch, &pszVersion))
    {
        while (*pszVersion == '-')
            pszVersion++;
        *pszArch = '\0';
    }
    else
    {
        mEnmOsType = (VBOXOSTYPE)(VBOXOSTYPE_FedoraCore | VBOXOSTYPE_UnknownArch);

        char ch;
        while ((ch = *psz) != '\0' && (!RT_C_IS_DIGIT(ch) || !RT_C_IS_PUNCT(psz[-1])))
            psz++;
        if (ch != '\0')
            pszVersion = psz;
    }

    /*
     * Replace '-' with '.' in the version part and use it as the version.
     */
    if (pszVersion)
    {
        psz = pszVersion;
        while ((psz = strchr(psz, '-')) != NULL)
            *psz++ = '.';
        try { mStrDetectedOSVersion = RTStrStrip(pszVersion); }
        catch (std::bad_alloc &) { return E_OUTOFMEMORY; }

        *pszVersion = '\0'; /* don't include in flavor */
    }

    /*
     * Split up the pre-arch/version bits into words and use them as the flavor.
     */
    psz = pszFlavor;
    while ((psz = strchr(psz, '-')) != NULL)
        *psz++ = ' ';
    try { mStrDetectedOSFlavor = RTStrStrip(pszFlavor); }
    catch (std::bad_alloc &) { return E_OUTOFMEMORY; }

    /*
     * If we don't have an architecture, we look at the vmlinuz file as the x86
     * and AMD64 versions starts with a MZ+PE header giving the architecture.
     */
    if ((mEnmOsType & VBOXOSTYPE_ArchitectureMask) == VBOXOSTYPE_UnknownArch)
    {
        static const char * const s_apszVmLinuz[] = { "images/pxeboot/vmlinuz", "isolinux/vmlinuz" };
        for (size_t i = 0; i < RT_ELEMENTS(s_apszVmLinuz); i++)
        {
            RTVFSFILE hVfsFileLinuz = NIL_RTVFSFILE;
            int vrc = RTVfsFileOpen(hVfsIso, s_apszVmLinuz[i], RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE,
                                    &hVfsFileLinuz);
            if (RT_SUCCESS(vrc))
            {
                /* DOS signature: */
                PIMAGE_DOS_HEADER pDosHdr = (PIMAGE_DOS_HEADER)&pBuf->ab[0];
                AssertCompile(sizeof(*pBuf) > sizeof(*pDosHdr));
                vrc = RTVfsFileReadAt(hVfsFileLinuz, 0, pDosHdr, sizeof(*pDosHdr), NULL);
                if (RT_SUCCESS(vrc) && pDosHdr->e_magic == IMAGE_DOS_SIGNATURE)
                {
                    /* NT signature - only need magic + file header, so use the 64 version for better debugging: */
                    PIMAGE_NT_HEADERS64 pNtHdrs = (PIMAGE_NT_HEADERS64)&pBuf->ab[0];
                    vrc = RTVfsFileReadAt(hVfsFileLinuz, pDosHdr->e_lfanew, pNtHdrs, sizeof(*pNtHdrs), NULL);
                    AssertCompile(sizeof(*pBuf) > sizeof(*pNtHdrs));
                    if (RT_SUCCESS(vrc) && pNtHdrs->Signature == IMAGE_NT_SIGNATURE)
                    {
                        if (pNtHdrs->FileHeader.Machine == IMAGE_FILE_MACHINE_I386)
                            mEnmOsType = (VBOXOSTYPE)((mEnmOsType & ~VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_x86);
                        else if (pNtHdrs->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64)
                            mEnmOsType = (VBOXOSTYPE)((mEnmOsType & ~VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_x64);
                        else if (pNtHdrs->FileHeader.Machine == IMAGE_FILE_MACHINE_ARM64)
                            mEnmOsType = (VBOXOSTYPE)((mEnmOsType & ~VBOXOSTYPE_ArchitectureMask) | VBOXOSTYPE_arm64);
                        else
                            AssertFailed();
                    }
                }

                RTVfsFileRelease(hVfsFileLinuz);
                if ((mEnmOsType & VBOXOSTYPE_ArchitectureMask) != VBOXOSTYPE_UnknownArch)
                    break;
            }
        }
    }

    /*
     * If that failed, look for other files that gives away the arch.
     */
    if ((mEnmOsType & VBOXOSTYPE_ArchitectureMask) == VBOXOSTYPE_UnknownArch)
    {
        static struct { const char *pszFile; VBOXOSTYPE fArch; } const s_aArchSpecificFiles[] =
        {
            { "EFI/BOOT/grubaa64.efi", VBOXOSTYPE_arm64 },
            { "EFI/BOOT/BOOTAA64.EFI", VBOXOSTYPE_arm64 },
        };
        PRTFSOBJINFO pObjInfo = (PRTFSOBJINFO)&pBuf->ab[0];
        AssertCompile(sizeof(*pBuf) > sizeof(*pObjInfo));
        for (size_t i = 0; i < RT_ELEMENTS(s_aArchSpecificFiles); i++)
        {
            int vrc = RTVfsQueryPathInfo(hVfsIso, s_aArchSpecificFiles[i].pszFile, pObjInfo,
                                         RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
            if (RT_SUCCESS(vrc) && RTFS_IS_FILE(pObjInfo->Attr.fMode))
            {
                mEnmOsType = (VBOXOSTYPE)((mEnmOsType & ~VBOXOSTYPE_ArchitectureMask) | s_aArchSpecificFiles[i].fArch);
                break;
            }
        }
    }

    /*
     * If we like, we could parse grub.conf to look for fullly spelled out
     * flavor, though the menu items typically only contains the major version
     * number, so little else to add, really.
     */

    return (mEnmOsType & VBOXOSTYPE_ArchitectureMask) != VBOXOSTYPE_UnknownArch ? S_OK : S_FALSE;
}


/**
 * Detect OS/2 installation ISOs.
 *
 * Mainly aiming at ACP2/MCP2 as that's what we currently use in our testing.
 *
 * @returns COM status code.
 * @retval  S_OK if detected
 * @retval  S_FALSE if not fully detected.
 *
 * @param   hVfsIso     The ISO file system.
 * @param   pBuf        Read buffer.
 */
HRESULT Unattended::i_innerDetectIsoOSOs2(RTVFS hVfsIso, DETECTBUFFER *pBuf)
{
    /*
     * The OS2SE20.SRC contains the location of the tree with the diskette
     * images, typically "\OS2IMAGE".
     */
    RTVFSFILE hVfsFile;
    int vrc = RTVfsFileOpen(hVfsIso, "OS2SE20.SRC", RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN, &hVfsFile);
    if (RT_SUCCESS(vrc))
    {
        size_t cbRead = 0;
        vrc = RTVfsFileRead(hVfsFile, pBuf->sz, sizeof(pBuf->sz) - 1, &cbRead);
        RTVfsFileRelease(hVfsFile);
        if (RT_SUCCESS(vrc))
        {
            pBuf->sz[cbRead] = '\0';
            RTStrStrip(pBuf->sz);
            vrc = RTStrValidateEncoding(pBuf->sz);
            if (RT_SUCCESS(vrc))
                LogRelFlow(("Unattended: OS2SE20.SRC=%s\n", pBuf->sz));
            else
                LogRel(("Unattended: OS2SE20.SRC invalid encoding: %Rrc, %.*Rhxs\n", vrc, cbRead, pBuf->sz));
        }
        else
            LogRel(("Unattended: Error reading OS2SE20.SRC: %\n", vrc));
    }
    /*
     * ArcaOS has dropped the file, assume it's \OS2IMAGE and see if it's there.
     */
    else if (vrc == VERR_FILE_NOT_FOUND)
        RTStrCopy(pBuf->sz, sizeof(pBuf->sz), "\\OS2IMAGE");
    else
        return S_FALSE;

    /*
     * Check that the directory directory exists and has a DISK_0 under it
     * with an OS2LDR on it.
     */
    size_t const cchOs2Image = strlen(pBuf->sz);
    vrc = RTPathAppend(pBuf->sz, sizeof(pBuf->sz), "DISK_0/OS2LDR");
    RTFSOBJINFO ObjInfo = {0};
    vrc = RTVfsQueryPathInfo(hVfsIso, pBuf->sz, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
    if (vrc == VERR_FILE_NOT_FOUND)
    {
        RTStrCat(pBuf->sz, sizeof(pBuf->sz), "."); /* eCS 2.0 image includes the dot from the 8.3 name.  */
        vrc = RTVfsQueryPathInfo(hVfsIso, pBuf->sz, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
    }
    if (   RT_FAILURE(vrc)
        || !RTFS_IS_FILE(ObjInfo.Attr.fMode))
    {
        LogRel(("Unattended: RTVfsQueryPathInfo(, '%s' (from OS2SE20.SRC),) -> %Rrc, fMode=%#x\n",
                pBuf->sz, vrc, ObjInfo.Attr.fMode));
        return S_FALSE;
    }

    /*
     * So, it's some kind of OS/2 2.x or later ISO alright.
     */
    mEnmOsType = VBOXOSTYPE_OS2;
    mStrDetectedOSHints.printf("OS2SE20.SRC=%.*s", cchOs2Image, pBuf->sz);

    /*
     * ArcaOS ISOs seems to have a AOSBOOT dir on them.
     * This contains a ARCANOAE.FLG file with content we can use for the version:
     *      ArcaOS 5.0.7 EN
     *      Built 2021-12-07 18:34:34
     * We drop the "ArcaOS" bit, as it's covered by mEnmOsType.  Then we pull up
     * the second line.
     *
     * Note! Yet to find a way to do unattended install of ArcaOS, as it comes
     *       with no CD-boot floppy images, only simple .PF archive files for
     *       unpacking onto the ram disk or whatever.  Modifying these is
     *       possible (ibsen's aPLib v0.36 compression with some simple custom
     *       headers), but it would probably be a royal pain.  Could perhaps
     *       cook something from OS2IMAGE\DISK_0 thru 3...
     */
    vrc = RTVfsQueryPathInfo(hVfsIso, "AOSBOOT", &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
    if (   RT_SUCCESS(vrc)
        && RTFS_IS_DIRECTORY(ObjInfo.Attr.fMode))
    {
        mEnmOsType = VBOXOSTYPE_ArcaOS;

        /* Read the version file:  */
        vrc = RTVfsFileOpen(hVfsIso, "SYS/ARCANOAE.FLG", RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN, &hVfsFile);
        if (RT_SUCCESS(vrc))
        {
            size_t cbRead = 0;
            vrc = RTVfsFileRead(hVfsFile, pBuf->sz, sizeof(pBuf->sz) - 1, &cbRead);
            RTVfsFileRelease(hVfsFile);
            pBuf->sz[cbRead] = '\0';
            if (RT_SUCCESS(vrc))
            {
                /* Strip the OS name: */
                char *pszVersion = RTStrStrip(pBuf->sz);
                static char s_szArcaOS[] = "ArcaOS";
                if (RTStrStartsWith(pszVersion, s_szArcaOS))
                    pszVersion = RTStrStripL(pszVersion + sizeof(s_szArcaOS) - 1);

                /* Pull up the 2nd line if it, condensing the \r\n into a single space. */
                char *pszNewLine = strchr(pszVersion, '\n');
                if (pszNewLine && RTStrStartsWith(pszNewLine + 1, "Built 20"))
                {
                    size_t offRemove = 0;
                    while (RT_C_IS_SPACE(pszNewLine[-1 - (ssize_t)offRemove]))
                        offRemove++;
                    if (offRemove > 0)
                    {
                        pszNewLine -= offRemove;
                        memmove(pszNewLine, pszNewLine + offRemove, strlen(pszNewLine + offRemove) - 1);
                    }
                    *pszNewLine = ' ';
                }

                /* Drop any additional lines: */
                pszNewLine = strchr(pszVersion, '\n');
                if (pszNewLine)
                    *pszNewLine = '\0';
                RTStrStripR(pszVersion);

                /* Done (hope it makes some sense). */
                mStrDetectedOSVersion = pszVersion;
            }
            else
                LogRel(("Unattended: failed to read AOSBOOT/ARCANOAE.FLG: %Rrc\n", vrc));
        }
        else
            LogRel(("Unattended: failed to open AOSBOOT/ARCANOAE.FLG for reading: %Rrc\n", vrc));
    }
    /*
     * Similarly, eCS has an ECS directory and it typically contains a
     * ECS_INST.FLG file with the version info.  Content differs a little:
     *      eComStation 2.0 EN_US Thu May 13 10:27:54 pm 2010
     *      Built on ECS60441318
     * Here we drop the "eComStation" bit and leave the 2nd line as it.
     *
     * Note! At least 2.0 has a DISKIMGS folder with what looks like boot
     *       disks, so we could probably get something going here without
     *       needing to write an OS2 boot sector...
     */
    else
    {
        vrc = RTVfsQueryPathInfo(hVfsIso, "ECS", &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
        if (   RT_SUCCESS(vrc)
            && RTFS_IS_DIRECTORY(ObjInfo.Attr.fMode))
        {
            mEnmOsType = VBOXOSTYPE_ECS;

            /* Read the version file:  */
            vrc = RTVfsFileOpen(hVfsIso, "ECS/ECS_INST.FLG", RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN, &hVfsFile);
            if (RT_SUCCESS(vrc))
            {
                size_t cbRead = 0;
                vrc = RTVfsFileRead(hVfsFile, pBuf->sz, sizeof(pBuf->sz) - 1, &cbRead);
                RTVfsFileRelease(hVfsFile);
                pBuf->sz[cbRead] = '\0';
                if (RT_SUCCESS(vrc))
                {
                    /* Strip the OS name: */
                    char *pszVersion = RTStrStrip(pBuf->sz);
                    static char s_szECS[] = "eComStation";
                    if (RTStrStartsWith(pszVersion, s_szECS))
                        pszVersion = RTStrStripL(pszVersion + sizeof(s_szECS) - 1);

                    /* Drop any additional lines: */
                    char *pszNewLine = strchr(pszVersion, '\n');
                    if (pszNewLine)
                        *pszNewLine = '\0';
                    RTStrStripR(pszVersion);

                    /* Done (hope it makes some sense). */
                    mStrDetectedOSVersion = pszVersion;
                }
                else
                    LogRel(("Unattended: failed to read ECS/ECS_INST.FLG: %Rrc\n", vrc));
            }
            else
                LogRel(("Unattended: failed to open ECS/ECS_INST.FLG for reading: %Rrc\n", vrc));
        }
        else
        {
            /*
             * Official IBM OS/2 builds doesn't have any .FLG file on them,
             * so need to pry the information out in some other way.  Best way
             * is to read the SYSLEVEL.OS2 file, which is typically on disk #2,
             * though on earlier versions (warp3) it was disk #1.
             */
            vrc = RTPathJoin(pBuf->sz, sizeof(pBuf->sz), strchr(mStrDetectedOSHints.c_str(), '=') + 1,
                             "/DISK_2/SYSLEVEL.OS2");
            if (RT_SUCCESS(vrc))
            {
                vrc = RTVfsFileOpen(hVfsIso, pBuf->sz, RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN, &hVfsFile);
                if (vrc == VERR_FILE_NOT_FOUND)
                {
                    RTPathJoin(pBuf->sz, sizeof(pBuf->sz), strchr(mStrDetectedOSHints.c_str(), '=') + 1, "/DISK_1/SYSLEVEL.OS2");
                    vrc = RTVfsFileOpen(hVfsIso, pBuf->sz, RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN, &hVfsFile);
                }
                if (RT_SUCCESS(vrc))
                {
                    RT_ZERO(pBuf->ab);
                    size_t cbRead = 0;
                    vrc = RTVfsFileRead(hVfsFile, pBuf->ab, sizeof(pBuf->ab), &cbRead);
                    RTVfsFileRelease(hVfsFile);
                    if (RT_SUCCESS(vrc))
                    {
                        /* Check the header. */
                        OS2SYSLEVELHDR   const *pHdr   = (OS2SYSLEVELHDR const *)&pBuf->ab[0];
                        if (   pHdr->uMinusOne == UINT16_MAX
                            && pHdr->uSyslevelFileVer == 1
                            && memcmp(pHdr->achSignature, RT_STR_TUPLE("SYSLEVEL")) == 0
                            && pHdr->offTable < cbRead
                            && pHdr->offTable + sizeof(OS2SYSLEVELENTRY) <= cbRead)
                        {
                            OS2SYSLEVELENTRY *pEntry = (OS2SYSLEVELENTRY *)&pBuf->ab[pHdr->offTable];
                            if (   RT_SUCCESS(RTStrValidateEncodingEx(pEntry->szName, sizeof(pEntry->szName),
                                                                      RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED))
                                && RT_SUCCESS(RTStrValidateEncodingEx(pEntry->achCsdLevel, sizeof(pEntry->achCsdLevel), 0))
                                && pEntry->bVersion != 0
                                && ((pEntry->bVersion >> 4) & 0xf) < 10
                                && (pEntry->bVersion & 0xf) < 10
                                && pEntry->bModify  < 10
                                && pEntry->bRefresh < 10)
                            {
                                /* Flavor: */
                                char *pszName = RTStrStrip(pEntry->szName);
                                if (pszName)
                                    mStrDetectedOSFlavor = pszName;

                                /* Version: */
                                if (pEntry->bRefresh != 0)
                                    mStrDetectedOSVersion.printf("%d.%d%d.%d", pEntry->bVersion >> 4, pEntry->bVersion & 0xf,
                                                                 pEntry->bModify, pEntry->bRefresh);
                                else
                                    mStrDetectedOSVersion.printf("%d.%d%d", pEntry->bVersion >> 4, pEntry->bVersion & 0xf,
                                                                 pEntry->bModify);
                                pEntry->achCsdLevel[sizeof(pEntry->achCsdLevel) - 1] = '\0';
                                char *pszCsd = RTStrStrip(pEntry->achCsdLevel);
                                if (*pszCsd != '\0')
                                {
                                    mStrDetectedOSVersion.append(' ');
                                    mStrDetectedOSVersion.append(pszCsd);
                                }
                                if (RTStrVersionCompare(mStrDetectedOSVersion.c_str(), "4.50") >= 0)
                                    mEnmOsType = VBOXOSTYPE_OS2Warp45;
                                else if (RTStrVersionCompare(mStrDetectedOSVersion.c_str(), "4.00") >= 0)
                                    mEnmOsType = VBOXOSTYPE_OS2Warp4;
                                else if (RTStrVersionCompare(mStrDetectedOSVersion.c_str(), "3.00") >= 0)
                                    mEnmOsType = VBOXOSTYPE_OS2Warp3;
                            }
                            else
                                LogRel(("Unattended: bogus SYSLEVEL.OS2 file entry: %.128Rhxd\n", pEntry));
                        }
                        else
                            LogRel(("Unattended: bogus SYSLEVEL.OS2 file header: uMinusOne=%#x uSyslevelFileVer=%#x achSignature=%.8Rhxs offTable=%#x vs cbRead=%#zx\n",
                                    pHdr->uMinusOne, pHdr->uSyslevelFileVer, pHdr->achSignature, pHdr->offTable, cbRead));
                    }
                    else
                        LogRel(("Unattended: failed to read SYSLEVEL.OS2: %Rrc\n", vrc));
                }
                else
                    LogRel(("Unattended: failed to open '%s' for reading: %Rrc\n", pBuf->sz, vrc));
            }
        }
    }

    /** @todo language detection? */

    /*
     * Only tested ACP2, so only return S_OK for it.
     */
    if (   mEnmOsType == VBOXOSTYPE_OS2Warp45
        && RTStrVersionCompare(mStrDetectedOSVersion.c_str(), "4.52") >= 0
        && mStrDetectedOSFlavor.contains("Server", RTCString::CaseInsensitive))
        return S_OK;

    return S_FALSE;
}


/**
 * Detect FreeBSD distro ISOs.
 *
 * @returns COM status code.
 * @retval  S_OK if detected
 * @retval  S_FALSE if not fully detected.
 *
 * @param   hVfsIso     The ISO file system.
 * @param   pBuf        Read buffer.
 */
HRESULT Unattended::i_innerDetectIsoOSFreeBsd(RTVFS hVfsIso, DETECTBUFFER *pBuf)
{
    RT_NOREF(pBuf);

    /*
     * FreeBSD since 10.0 has a .profile file in the root which can be used to determine that this is FreeBSD
     * along with the version.
     */

    RTVFSFILE hVfsFile;
    HRESULT hrc = S_FALSE;
    int vrc = RTVfsFileOpen(hVfsIso, ".profile", RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN, &hVfsFile);
    if (RT_SUCCESS(vrc))
    {
        static const uint8_t s_abFreeBsdHdr[] = "# $FreeBSD: releng/";
        char abRead[32];

        vrc = RTVfsFileRead(hVfsFile, &abRead[0], sizeof(abRead), NULL /*pcbRead*/);
        if (   RT_SUCCESS(vrc)
            && !memcmp(&abRead[0], &s_abFreeBsdHdr[0], sizeof(s_abFreeBsdHdr) - 1)) /* Skip terminator */
        {
            abRead[sizeof(abRead) - 1] = '\0';

            /* Detect the architecture using the volume label. */
            char szVolumeId[128];
            size_t cchVolumeId;
            vrc = RTVfsQueryLabel(hVfsIso, false /*fAlternative*/, szVolumeId, 128, &cchVolumeId);
            if (RT_SUCCESS(vrc))
            {
                /* Can re-use the Linux code here. */
                if (!detectLinuxArchII(szVolumeId, &mEnmOsType, VBOXOSTYPE_FreeBSD))
                    LogRel(("Unattended/FBSD: Unknown: arch='%s'\n", szVolumeId));

                /* Detect the version from the string coming after the needle in .profile. */
                AssertCompile(sizeof(s_abFreeBsdHdr) - 1 < sizeof(abRead));

                char *pszVersionStart = &abRead[sizeof(s_abFreeBsdHdr) - 1];
                char *pszVersionEnd = pszVersionStart;

                while (RT_C_IS_DIGIT(*pszVersionEnd))
                    pszVersionEnd++;
                if (*pszVersionEnd == '.')
                {
                    pszVersionEnd++; /* Skip the . */

                    while (RT_C_IS_DIGIT(*pszVersionEnd))
                        pszVersionEnd++;

                    /* Terminate the version string. */
                    *pszVersionEnd = '\0';

                    try { mStrDetectedOSVersion = pszVersionStart; }
                    catch (std::bad_alloc &) { hrc = E_OUTOFMEMORY; }
                }
                else
                    LogRel(("Unattended/FBSD: Unknown: version='%s'\n", &abRead[0]));
            }
            else
            {
                LogRel(("Unattended/FBSD: No Volume Label found\n"));
                mEnmOsType = VBOXOSTYPE_FreeBSD;
            }

            hrc = S_OK;
        }

        RTVfsFileRelease(hVfsFile);
    }

    return hrc;
}


HRESULT Unattended::prepare()
{
    LogFlow(("Unattended::prepare: enter\n"));

    /*
     * Must have a machine.
     */
    ComPtr<Machine> ptrMachine;
    Guid            MachineUuid;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        ptrMachine = mMachine;
        if (ptrMachine.isNull())
            return setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("No machine associated with this IUnatteded instance"));
        MachineUuid = mMachineUuid;
    }

    /*
     * Before we write lock ourselves, we must get stuff from Machine and
     * VirtualBox because their locks have higher priorities than ours.
     */
    Utf8Str strGuestOsTypeId;
    Utf8Str strMachineName;
    Utf8Str strDefaultAuxBasePath;
    HRESULT hrc;
    try
    {
        Bstr bstrTmp;
        hrc = ptrMachine->COMGETTER(OSTypeId)(bstrTmp.asOutParam());
        if (SUCCEEDED(hrc))
        {
            strGuestOsTypeId = bstrTmp;
            hrc = ptrMachine->COMGETTER(Name)(bstrTmp.asOutParam());
            if (SUCCEEDED(hrc))
                strMachineName = bstrTmp;
        }
        int vrc = ptrMachine->i_calculateFullPath(Utf8StrFmt("Unattended-%RTuuid-", MachineUuid.raw()), strDefaultAuxBasePath);
        if (RT_FAILURE(vrc))
            return setErrorBoth(E_FAIL, vrc);
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }
    bool const fIs64Bit = i_isGuestOSArchX64(strGuestOsTypeId);

    ComPtr<IPlatform> pPlatform;
    hrc = ptrMachine->COMGETTER(Platform)(pPlatform.asOutParam());
    AssertComRCReturn(hrc, hrc);

    BOOL fRtcUseUtc = FALSE;
    hrc = pPlatform->COMGETTER(RTCUseUTC)(&fRtcUseUtc);
    if (FAILED(hrc))
        return hrc;

    ComPtr<IFirmwareSettings> pFirmwareSettings;
    hrc = ptrMachine->COMGETTER(FirmwareSettings)(pFirmwareSettings.asOutParam());
    AssertComRCReturn(hrc, hrc);

    FirmwareType_T enmFirmware = FirmwareType_BIOS;
    hrc = pFirmwareSettings->COMGETTER(FirmwareType)(&enmFirmware);
    if (FAILED(hrc))
        return hrc;

    /* Populate list of supported guest OSs in case it has not been done yet. Do this before locking. */
    if (!mfDoneSupportedGuestOSList)
        i_getListOfSupportedGuestOS();

    /*
     * Write lock this object and set attributes we got from IMachine.
     */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    mStrGuestOsTypeId = strGuestOsTypeId;
    mfGuestOs64Bit    = fIs64Bit;
    mfRtcUseUtc       = RT_BOOL(fRtcUseUtc);
    menmFirmwareType  = enmFirmware;

    /*
     * Do some state checks.
     */
    if (mpInstaller != NULL)
        return setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("The prepare method has been called (must call done to restart)"));
    if ((Machine *)ptrMachine != (Machine *)mMachine)
        return setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("The 'machine' while we were using it - please don't do that"));

    /*
     * Check if the specified ISOs and files exist.
     */
    if (!RTFileExists(mStrIsoPath.c_str()))
        return setErrorBoth(E_FAIL, VERR_FILE_NOT_FOUND, tr("Could not locate the installation ISO file '%s'"),
                            mStrIsoPath.c_str());
    if (mfInstallGuestAdditions && !RTFileExists(mStrAdditionsIsoPath.c_str()))
        return setErrorBoth(E_FAIL, VERR_FILE_NOT_FOUND, tr("Could not locate the Guest Additions ISO file '%s'"),
                            mStrAdditionsIsoPath.c_str());
    if (mfInstallTestExecService && !RTFileExists(mStrValidationKitIsoPath.c_str()))
        return setErrorBoth(E_FAIL, VERR_FILE_NOT_FOUND, tr("Could not locate the validation kit ISO file '%s'"),
                            mStrValidationKitIsoPath.c_str());
    if (mfInstallUserPayload && !RTFileExists(mStrUserPayloadIsoPath.c_str()))
        return setErrorBoth(E_FAIL, VERR_FILE_NOT_FOUND, tr("Could not locate the User Payload ISO file '%s'"),
                            mStrUserPayloadIsoPath.c_str());
    if (mStrScriptTemplatePath.isNotEmpty() && !RTFileExists(mStrScriptTemplatePath.c_str()))
        return setErrorBoth(E_FAIL, VERR_FILE_NOT_FOUND, tr("Could not locate unattended installation script template '%s'"),
                            mStrScriptTemplatePath.c_str());

    /*
     * Do media detection if it haven't been done yet.
     */
    if (!mfDoneDetectIsoOS)
    {
        hrc = detectIsoOS();
        if (FAILED(hrc) && hrc != E_NOTIMPL)
            return hrc;
    }

    /*
     * We can now check midxImage against mDetectedImages, since the latter is
     * populated during the detectIsoOS call.  We ignore midxImage if no images
     * were detected, assuming that it's not relevant or used for different purposes.
     */
    if (mDetectedImages.size() > 0)
    {
        bool fImageFound = false;
        for (size_t i = 0; i < mDetectedImages.size(); ++i)
            if (midxImage == mDetectedImages[i].mImageIndex)
            {
                i_updateDetectedAttributeForImage(mDetectedImages[i]);
                fImageFound = true;
                break;
            }
        if (!fImageFound)
            return setErrorBoth(E_FAIL, VERR_NOT_FOUND, tr("imageIndex value %u not found in detectedImageIndices"), midxImage);
    }

    /*
     * Get the ISO's detect guest OS type info and make it's a known one (just
     * in case the above step doesn't work right).
     */
    uint32_t const   idxIsoOSType = Global::getOSTypeIndexFromId(mStrDetectedOSTypeId.c_str());
    VBOXOSTYPE const enmIsoOSType = idxIsoOSType < Global::cOSTypes ? Global::sOSTypes[idxIsoOSType].osType : VBOXOSTYPE_Unknown;
    if ((enmIsoOSType & VBOXOSTYPE_OsFamilyMask) == VBOXOSTYPE_Unknown)
        return setError(E_FAIL, tr("The supplied ISO file does not contain an OS currently supported for unattended installation"));

    /*
     * Get the VM's configured guest OS type info.
     */
    uint32_t const   idxMachineOSType = Global::getOSTypeIndexFromId(mStrGuestOsTypeId.c_str());
    VBOXOSTYPE const enmMachineOSType = idxMachineOSType < Global::cOSTypes
                                      ? Global::sOSTypes[idxMachineOSType].osType : VBOXOSTYPE_Unknown;
#if 0 /** @todo r=bird: Unused, see below.  */
    uint32_t const   fMachineOsHints  = idxMachineOSType < Global::cOSTypes
                                      ? Global::sOSTypes[idxMachineOSType].osHint : 0;
#endif

    /*
     * Check that the detected guest OS type for the ISO is compatible with
     * that of the VM, broadly speaking.
     */
    if (idxMachineOSType != idxIsoOSType)
    {
        /* Check that the architecture is compatible: */
        if (   (enmIsoOSType & VBOXOSTYPE_ArchitectureMask) != (enmMachineOSType & VBOXOSTYPE_ArchitectureMask)
            && (   (enmIsoOSType     & VBOXOSTYPE_ArchitectureMask) != VBOXOSTYPE_x86
                || (enmMachineOSType & VBOXOSTYPE_ArchitectureMask) != VBOXOSTYPE_x64))
            return setError(E_FAIL, tr("The supplied ISO file is incompatible with the guest OS type of the VM: CPU architecture mismatch"));

        /** @todo check BIOS/EFI requirement */
    }

#if 0 /** @todo r=bird: this is misleading, since it is checking the machine. */
    /* We don't support guest OSes w/ EFI, as that requires UDF remastering support we don't have yet. */
    if (   (fMachineOsHints & VBOXOSHINT_EFI)
        && (enmIsoOSType & VBOXOSTYPE_ArchitectureMask) != VBOXOSTYPE_arm64)
        return setError(E_FAIL, tr("The machine is configured with EFI which is currently not supported by unatteded installation"));
#endif

    /* Set the guest additions install package name. */
    mStrAdditionsInstallPackage = Global::sOSTypes[idxMachineOSType].guestAdditionsInstallPkgName;

    /*
     * Do some default property stuff and check other properties.
     */
    try
    {
        char szTmp[128];

        if (mStrLocale.isEmpty())
        {
            int vrc = RTLocaleQueryNormalizedBaseLocaleName(szTmp, sizeof(szTmp));
            if (   RT_SUCCESS(vrc)
                && RTLOCALE_IS_LANGUAGE2_UNDERSCORE_COUNTRY2(szTmp))
                mStrLocale.assign(szTmp, 5);
            else
                mStrLocale = "en_US";
            Assert(RTLOCALE_IS_LANGUAGE2_UNDERSCORE_COUNTRY2(mStrLocale));
        }

        if (mStrLanguage.isEmpty())
        {
            if (mDetectedOSLanguages.size() > 0)
                mStrLanguage = mDetectedOSLanguages[0];
            else
                mStrLanguage.assign(mStrLocale).findReplace('_', '-');
        }

        if (mStrCountry.isEmpty())
        {
            int vrc = RTLocaleQueryUserCountryCode(szTmp);
            if (RT_SUCCESS(vrc))
                mStrCountry = szTmp;
            else if (   mStrLocale.isNotEmpty()
                     && RTLOCALE_IS_LANGUAGE2_UNDERSCORE_COUNTRY2(mStrLocale))
                mStrCountry.assign(mStrLocale, 3, 2);
            else
                mStrCountry = "US";
        }

        if (mStrTimeZone.isEmpty())
        {
            int vrc = RTTimeZoneGetCurrent(szTmp, sizeof(szTmp));
            if (   RT_SUCCESS(vrc)
                && strcmp(szTmp, "localtime") != 0 /* Typcial solaris TZ that isn't very helpful. */)
                mStrTimeZone = szTmp;
            else
                mStrTimeZone = "Etc/UTC";
            Assert(mStrTimeZone.isNotEmpty());
        }
        mpTimeZoneInfo = RTTimeZoneGetInfoByUnixName(mStrTimeZone.c_str());
        if (!mpTimeZoneInfo)
            mpTimeZoneInfo = RTTimeZoneGetInfoByWindowsName(mStrTimeZone.c_str());
        Assert(mpTimeZoneInfo || mStrTimeZone != "Etc/UTC");
        if (!mpTimeZoneInfo)
            LogRel(("Unattended::prepare: warning: Unknown time zone '%s'\n", mStrTimeZone.c_str()));

        if (mStrHostname.isEmpty())
        {
            /* Mangle the VM name into a valid hostname. */
            for (size_t i = 0; i < strMachineName.length(); i++)
            {
                char ch = strMachineName[i];
                if (   (unsigned)ch < 127
                    && RT_C_IS_ALNUM(ch))
                    mStrHostname.append(ch);
                else if (mStrHostname.isNotEmpty() && RT_C_IS_PUNCT(ch) && !mStrHostname.endsWith("-"))
                    mStrHostname.append('-');
            }
            if (mStrHostname.length() == 0)
                mStrHostname.printf("%RTuuid-vm", MachineUuid.raw());
            else if (mStrHostname.length() < 3)
                mStrHostname.append("-vm");
            mStrHostname.append(".myguest.virtualbox.org");
        }

        if (mStrAuxiliaryBasePath.isEmpty())
        {
            mStrAuxiliaryBasePath = strDefaultAuxBasePath;
            mfIsDefaultAuxiliaryBasePath = true;
        }
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    /*
     * Instatiate the guest installer matching the ISO.
     */
    mpInstaller = UnattendedInstaller::createInstance(enmIsoOSType, mStrDetectedOSTypeId, mStrDetectedOSVersion,
                                                      mStrDetectedOSFlavor, mStrDetectedOSHints, this);
    if (mpInstaller != NULL)
    {
        hrc = mpInstaller->initInstaller();
        if (SUCCEEDED(hrc))
        {
            /*
             * Do the script preps (just reads them).
             */
            hrc = mpInstaller->prepareUnattendedScripts();
            if (SUCCEEDED(hrc))
            {
                LogFlow(("Unattended::prepare: returns S_OK\n"));
                return S_OK;
            }
        }

        /* Destroy the installer instance. */
        delete mpInstaller;
        mpInstaller = NULL;
    }
    else
        hrc = setErrorBoth(E_FAIL, VERR_NOT_FOUND,
                           tr("Unattended installation is not supported for guest type '%s'"), mStrGuestOsTypeId.c_str());
    LogRelFlow(("Unattended::prepare: failed with %Rhrc\n", hrc));
    return hrc;
}

HRESULT Unattended::constructMedia()
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    LogFlow(("===========================================================\n"));
    LogFlow(("Call Unattended::constructMedia()\n"));

    if (mpInstaller == NULL)
        return setErrorBoth(E_FAIL, VERR_WRONG_ORDER, "prepare() not yet called");

    return mpInstaller->prepareMedia();
}

HRESULT Unattended::reconfigureVM()
{
    LogFlow(("===========================================================\n"));
    LogFlow(("Call Unattended::reconfigureVM()\n"));

    /*
     * Interrogate VirtualBox/IGuestOSType before we lock stuff and create ordering issues.
     */
    StorageBus_T enmRecommendedStorageBus = StorageBus_IDE;
    {
        Bstr bstrGuestOsTypeId;
        Bstr bstrDetectedOSTypeId;
        {
            AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
            if (mpInstaller == NULL)
                return setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("prepare() not yet called"));
            bstrGuestOsTypeId    = mStrGuestOsTypeId;
            bstrDetectedOSTypeId = mStrDetectedOSTypeId;
        }
        ComPtr<IGuestOSType> ptrGuestOSType;
        HRESULT hrc = mParent->GetGuestOSType(bstrGuestOsTypeId.raw(), ptrGuestOSType.asOutParam());
        if (SUCCEEDED(hrc))
        {
            if (!ptrGuestOSType.isNull())
                hrc = ptrGuestOSType->COMGETTER(RecommendedDVDStorageBus)(&enmRecommendedStorageBus);
        }
        if (FAILED(hrc))
            return hrc;

        /* If the detected guest OS type differs, log a warning if their DVD storage
           bus recommendations differ.  */
        if (bstrGuestOsTypeId != bstrDetectedOSTypeId)
        {
            StorageBus_T enmRecommendedStorageBus2 = StorageBus_IDE;
            hrc = mParent->GetGuestOSType(bstrDetectedOSTypeId.raw(), ptrGuestOSType.asOutParam());
            if (SUCCEEDED(hrc) && !ptrGuestOSType.isNull())
                hrc = ptrGuestOSType->COMGETTER(RecommendedDVDStorageBus)(&enmRecommendedStorageBus2);
            if (FAILED(hrc))
                return hrc;

            if (enmRecommendedStorageBus != enmRecommendedStorageBus2)
                LogRel(("Unattended::reconfigureVM: DVD storage bus recommendations differs for the VM and the ISO guest OS types: VM: %s (%ls), ISO: %s (%ls)\n",
                        ::stringifyStorageBus(enmRecommendedStorageBus), bstrGuestOsTypeId.raw(),
                        ::stringifyStorageBus(enmRecommendedStorageBus2), bstrDetectedOSTypeId.raw() ));
        }
    }

    /*
     * Take write lock (for lock order reasons, write lock our parent object too)
     * then make sure we're the only caller of this method.
     */
    AutoMultiWriteLock2 alock(mMachine, this COMMA_LOCKVAL_SRC_POS);
    HRESULT hrc;
    if (mhThreadReconfigureVM == NIL_RTNATIVETHREAD)
    {
        RTNATIVETHREAD const hNativeSelf = RTThreadNativeSelf();
        mhThreadReconfigureVM = hNativeSelf;

        /*
         * Create a new session, lock the machine and get the session machine object.
         * Do the locking without pinning down the write locks, just to be on the safe side.
         */
        ComPtr<ISession> ptrSession;
        try
        {
            hrc = ptrSession.createInprocObject(CLSID_Session);
        }
        catch (std::bad_alloc &)
        {
            hrc = E_OUTOFMEMORY;
        }
        if (SUCCEEDED(hrc))
        {
            alock.release();
            hrc = mMachine->LockMachine(ptrSession, LockType_Shared);
            alock.acquire();
            if (SUCCEEDED(hrc))
            {
                ComPtr<IMachine> ptrSessionMachine;
                hrc = ptrSession->COMGETTER(Machine)(ptrSessionMachine.asOutParam());
                if (SUCCEEDED(hrc))
                {
                    /*
                     * Hand the session to the inner work and let it do it job.
                     */
                    try
                    {
                        hrc = i_innerReconfigureVM(alock, enmRecommendedStorageBus, ptrSessionMachine);
                    }
                    catch (...)
                    {
                        hrc = E_UNEXPECTED;
                    }
                }

                /* Paranoia: release early in case we it a bump below.  */
                Assert(mhThreadReconfigureVM == hNativeSelf);
                mhThreadReconfigureVM = NIL_RTNATIVETHREAD;

                /*
                 * While unlocking the machine we'll have to drop the locks again.
                 */
                alock.release();

                ptrSessionMachine.setNull();
                HRESULT hrc2 = ptrSession->UnlockMachine();
                AssertLogRelMsg(SUCCEEDED(hrc2), ("UnlockMachine -> %Rhrc\n", hrc2));

                ptrSession.setNull();

                alock.acquire();
            }
            else
                mhThreadReconfigureVM = NIL_RTNATIVETHREAD;
        }
        else
            mhThreadReconfigureVM = NIL_RTNATIVETHREAD;
    }
    else
        hrc = setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("reconfigureVM running on other thread"));
    return hrc;
}


HRESULT Unattended::i_innerReconfigureVM(AutoMultiWriteLock2 &rAutoLock, StorageBus_T enmRecommendedStorageBus,
                                         ComPtr<IMachine> const &rPtrSessionMachine)
{
    if (mpInstaller == NULL)
        return setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("prepare() not yet called"));

    // Fetch all available storage controllers
    com::SafeIfaceArray<IStorageController> arrayOfControllers;
    HRESULT hrc = rPtrSessionMachine->COMGETTER(StorageControllers)(ComSafeArrayAsOutParam(arrayOfControllers));
    AssertComRCReturn(hrc, hrc);

    /*
     * Figure out where the images are to be mounted, adding controllers/ports as needed.
     */
    std::vector<UnattendedInstallationDisk> vecInstallationDisks;
    if (mpInstaller->isAuxiliaryFloppyNeeded())
    {
        hrc = i_reconfigureFloppy(arrayOfControllers, vecInstallationDisks, rPtrSessionMachine, rAutoLock);
        if (FAILED(hrc))
            return hrc;
    }

    hrc = i_reconfigureIsos(arrayOfControllers, vecInstallationDisks, rPtrSessionMachine, rAutoLock, enmRecommendedStorageBus);
    if (FAILED(hrc))
        return hrc;

    /*
     * Mount the images.
     */
    for (size_t idxImage = 0; idxImage < vecInstallationDisks.size(); idxImage++)
    {
        UnattendedInstallationDisk const *pImage = &vecInstallationDisks.at(idxImage);
        Assert(pImage->strImagePath.isNotEmpty());
        hrc = i_attachImage(pImage, rPtrSessionMachine, rAutoLock);
        if (FAILED(hrc))
            return hrc;
    }

    /*
     * Set the boot order.
     *
     * ASSUME that the HD isn't bootable when we start out, but it will be what
     * we boot from after the first stage of the installation is done.  Setting
     * it first prevents endless reboot cylces.
     */
    /** @todo consider making 100% sure the disk isn't bootable (edit partition
     *        table active bits and EFI stuff). */
    Assert(   mpInstaller->getBootableDeviceType() == DeviceType_DVD
           || mpInstaller->getBootableDeviceType() == DeviceType_Floppy);
    hrc = rPtrSessionMachine->SetBootOrder(1, DeviceType_HardDisk);
    if (SUCCEEDED(hrc))
        hrc = rPtrSessionMachine->SetBootOrder(2, mpInstaller->getBootableDeviceType());
    if (SUCCEEDED(hrc))
        hrc = rPtrSessionMachine->SetBootOrder(3, mpInstaller->getBootableDeviceType() == DeviceType_DVD
                                                  ? DeviceType_Floppy : DeviceType_DVD);
    if (FAILED(hrc))
        return hrc;

    /*
     * Essential step.
     *
     * HACK ALERT! We have to release the lock here or we'll get into trouble with
     *             the VirtualBox lock (via i_saveHardware/NetworkAdaptger::i_hasDefaults/VirtualBox::i_findGuestOSType).
     */
    if (SUCCEEDED(hrc))
    {
        rAutoLock.release();
        hrc = rPtrSessionMachine->SaveSettings();
        rAutoLock.acquire();
    }

    return hrc;
}

/**
 * Makes sure we've got a floppy drive attached to a floppy controller, adding
 * the auxiliary floppy image to the installation disk vector.
 *
 * @returns COM status code.
 * @param   rControllers            The existing controllers.
 * @param   rVecInstallatationDisks The list of image to mount.
 * @param   rPtrSessionMachine      The session machine smart pointer.
 * @param   rAutoLock               The lock.
 */
HRESULT Unattended::i_reconfigureFloppy(com::SafeIfaceArray<IStorageController> &rControllers,
                                        std::vector<UnattendedInstallationDisk> &rVecInstallatationDisks,
                                        ComPtr<IMachine> const &rPtrSessionMachine,
                                        AutoMultiWriteLock2 &rAutoLock)
{
    Assert(mpInstaller->isAuxiliaryFloppyNeeded());

    /*
     * Look for a floppy controller with a primary drive (A:) we can "insert"
     * the auxiliary floppy image.  Add a controller and/or a drive if necessary.
     */
    bool    fFoundPort0Dev0 = false;
    Bstr    bstrControllerName;
    Utf8Str strControllerName;

    for (size_t i = 0; i < rControllers.size(); ++i)
    {
        StorageBus_T enmStorageBus;
        HRESULT hrc = rControllers[i]->COMGETTER(Bus)(&enmStorageBus);
        AssertComRCReturn(hrc, hrc);
        if (enmStorageBus == StorageBus_Floppy)
        {

            /*
             * Found a floppy controller.
             */
            hrc = rControllers[i]->COMGETTER(Name)(bstrControllerName.asOutParam());
            AssertComRCReturn(hrc, hrc);

            /*
             * Check the attchments to see if we've got a device 0 attached on port 0.
             *
             * While we're at it we eject flppies from all floppy drives we encounter,
             * we don't want any confusion at boot or during installation.
             */
            com::SafeIfaceArray<IMediumAttachment> arrayOfMediumAttachments;
            hrc = rPtrSessionMachine->GetMediumAttachmentsOfController(bstrControllerName.raw(),
                                                                       ComSafeArrayAsOutParam(arrayOfMediumAttachments));
            AssertComRCReturn(hrc, hrc);
            strControllerName = bstrControllerName;
            AssertLogRelReturn(strControllerName.isNotEmpty(), setErrorBoth(E_UNEXPECTED, VERR_INTERNAL_ERROR_2));

            for (size_t j = 0; j < arrayOfMediumAttachments.size(); j++)
            {
                LONG iPort = -1;
                hrc = arrayOfMediumAttachments[j]->COMGETTER(Port)(&iPort);
                AssertComRCReturn(hrc, hrc);

                LONG iDevice = -1;
                hrc = arrayOfMediumAttachments[j]->COMGETTER(Device)(&iDevice);
                AssertComRCReturn(hrc, hrc);

                DeviceType_T enmType;
                hrc = arrayOfMediumAttachments[j]->COMGETTER(Type)(&enmType);
                AssertComRCReturn(hrc, hrc);

                if (enmType == DeviceType_Floppy)
                {
                    ComPtr<IMedium> ptrMedium;
                    hrc = arrayOfMediumAttachments[j]->COMGETTER(Medium)(ptrMedium.asOutParam());
                    AssertComRCReturn(hrc, hrc);

                    if (ptrMedium.isNotNull())
                    {
                        ptrMedium.setNull();
                        rAutoLock.release();
                        hrc = rPtrSessionMachine->UnmountMedium(bstrControllerName.raw(), iPort, iDevice, TRUE /*fForce*/);
                        rAutoLock.acquire();
                    }

                    if (iPort == 0 && iDevice == 0)
                        fFoundPort0Dev0 = true;
                }
                else if (iPort == 0 && iDevice == 0)
                    return setError(E_FAIL,
                                    tr("Found non-floppy device attached to port 0 device 0 on the floppy controller '%ls'"),
                                    bstrControllerName.raw());
            }
        }
    }

    /*
     * Add a floppy controller if we need to.
     */
    if (strControllerName.isEmpty())
    {
        bstrControllerName = strControllerName = "Floppy";
        ComPtr<IStorageController> ptrControllerIgnored;
        HRESULT hrc = rPtrSessionMachine->AddStorageController(bstrControllerName.raw(), StorageBus_Floppy,
                                                               ptrControllerIgnored.asOutParam());
        LogRelFunc(("Machine::addStorageController(Floppy) -> %Rhrc \n", hrc));
        if (FAILED(hrc))
            return hrc;
    }

    /*
     * Adding a floppy drive (if needed) and mounting the auxiliary image is
     * done later together with the ISOs.
     */
    rVecInstallatationDisks.push_back(UnattendedInstallationDisk(StorageBus_Floppy, strControllerName,
                                                                 DeviceType_Floppy, AccessMode_ReadWrite,
                                                                 0, 0,
                                                                 fFoundPort0Dev0 /*fMountOnly*/,
                                                                 mpInstaller->getAuxiliaryFloppyFilePath(), false));
    return S_OK;
}

/**
 * Reconfigures DVD drives of the VM to mount all the ISOs we need.
 *
 * This will umount all DVD media.
 *
 * @returns COM status code.
 * @param   rControllers            The existing controllers.
 * @param   rVecInstallatationDisks The list of image to mount.
 * @param   rPtrSessionMachine      The session machine smart pointer.
 * @param   rAutoLock               The lock.
 * @param   enmRecommendedStorageBus The recommended storage bus type for adding
 *                                   DVD drives on.
 */
HRESULT Unattended::i_reconfigureIsos(com::SafeIfaceArray<IStorageController> &rControllers,
                                      std::vector<UnattendedInstallationDisk> &rVecInstallatationDisks,
                                      ComPtr<IMachine> const &rPtrSessionMachine,
                                      AutoMultiWriteLock2 &rAutoLock, StorageBus_T enmRecommendedStorageBus)
{
    /*
     * Enumerate the attachements of every controller, looking for DVD drives,
     * ASSUMEING all drives are bootable.
     *
     * Eject the medium from all the drives (don't want any confusion) and look
     * for the recommended storage bus in case we need to add more drives.
     */
    HRESULT                    hrc;
    std::list<ControllerSlot>  lstControllerDvdSlots;
    Utf8Str                    strRecommendedControllerName; /* non-empty if recommended bus found. */
    Utf8Str                    strControllerName;
    Bstr                       bstrControllerName;
    for (size_t i = 0; i < rControllers.size(); ++i)
    {
        hrc = rControllers[i]->COMGETTER(Name)(bstrControllerName.asOutParam());
        AssertComRCReturn(hrc, hrc);
        strControllerName = bstrControllerName;

        /* Look for recommended storage bus. */
        StorageBus_T enmStorageBus;
        hrc = rControllers[i]->COMGETTER(Bus)(&enmStorageBus);
        AssertComRCReturn(hrc, hrc);
        if (enmStorageBus == enmRecommendedStorageBus)
        {
            strRecommendedControllerName = bstrControllerName;
            AssertLogRelReturn(strControllerName.isNotEmpty(), setErrorBoth(E_UNEXPECTED, VERR_INTERNAL_ERROR_2));
        }

        /* Scan the controller attachments. */
        com::SafeIfaceArray<IMediumAttachment> arrayOfMediumAttachments;
        hrc = rPtrSessionMachine->GetMediumAttachmentsOfController(bstrControllerName.raw(),
                                                                  ComSafeArrayAsOutParam(arrayOfMediumAttachments));
        AssertComRCReturn(hrc, hrc);

        for (size_t j = 0; j < arrayOfMediumAttachments.size(); j++)
        {
            DeviceType_T enmType;
            hrc = arrayOfMediumAttachments[j]->COMGETTER(Type)(&enmType);
            AssertComRCReturn(hrc, hrc);
            if (enmType == DeviceType_DVD)
            {
                LONG iPort = -1;
                hrc = arrayOfMediumAttachments[j]->COMGETTER(Port)(&iPort);
                AssertComRCReturn(hrc, hrc);

                LONG iDevice = -1;
                hrc = arrayOfMediumAttachments[j]->COMGETTER(Device)(&iDevice);
                AssertComRCReturn(hrc, hrc);

                /* Remeber it. */
                lstControllerDvdSlots.push_back(ControllerSlot(enmStorageBus, strControllerName, iPort, iDevice, false /*fFree*/));

                /* Eject the medium, if any. */
                ComPtr<IMedium> ptrMedium;
                hrc = arrayOfMediumAttachments[j]->COMGETTER(Medium)(ptrMedium.asOutParam());
                AssertComRCReturn(hrc, hrc);
                if (ptrMedium.isNotNull())
                {
                    ptrMedium.setNull();

                    rAutoLock.release();
                    hrc = rPtrSessionMachine->UnmountMedium(bstrControllerName.raw(), iPort, iDevice, TRUE /*fForce*/);
                    rAutoLock.acquire();
                }
            }
        }
    }

    /*
     * How many drives do we need? Add more if necessary.
     */
    ULONG cDvdDrivesNeeded = 0;
    if (mpInstaller->isAuxiliaryIsoNeeded())
        cDvdDrivesNeeded++;
    if (mpInstaller->isOriginalIsoNeeded())
        cDvdDrivesNeeded++;
#if 0 /* These are now in the AUX VISO. */
    if (mpInstaller->isAdditionsIsoNeeded())
        cDvdDrivesNeeded++;
    if (mpInstaller->isValidationKitIsoNeeded())
        cDvdDrivesNeeded++;
#endif
    Assert(cDvdDrivesNeeded > 0);
    if (cDvdDrivesNeeded > lstControllerDvdSlots.size())
    {
        /* Do we need to add the recommended controller? */
        if (strRecommendedControllerName.isEmpty())
        {
            strRecommendedControllerName = StorageController::i_controllerNameFromBusType(enmRecommendedStorageBus);

            ComPtr<IStorageController> ptrControllerIgnored;
            hrc = rPtrSessionMachine->AddStorageController(Bstr(strRecommendedControllerName).raw(), enmRecommendedStorageBus,
                                                           ptrControllerIgnored.asOutParam());
            LogRelFunc(("Machine::addStorageController(%s) -> %Rhrc \n", strRecommendedControllerName.c_str(), hrc));
            if (FAILED(hrc))
                return hrc;
        }

        /* Add free controller slots, maybe raising the port limit on the controller if we can. */
        hrc = i_findOrCreateNeededFreeSlots(strRecommendedControllerName, enmRecommendedStorageBus, rPtrSessionMachine,
                                            cDvdDrivesNeeded, lstControllerDvdSlots);
        if (FAILED(hrc))
            return hrc;
        if (cDvdDrivesNeeded > lstControllerDvdSlots.size())
        {
            /* We could in many cases create another controller here, but it's not worth the effort. */
            return setError(E_FAIL, tr("Not enough free slots on controller '%s' to add %u DVD drive(s)", "",
                                       cDvdDrivesNeeded - lstControllerDvdSlots.size()),
                            strRecommendedControllerName.c_str(), cDvdDrivesNeeded - lstControllerDvdSlots.size());
        }
        Assert(cDvdDrivesNeeded == lstControllerDvdSlots.size());
    }

    /*
     * Sort the DVD slots in boot order.
     */
    lstControllerDvdSlots.sort();

    /*
     * Prepare ISO mounts.
     *
     * Boot order depends on bootFromAuxiliaryIso() and we must grab DVD slots
     * according to the boot order.
     */
    std::list<ControllerSlot>::const_iterator itDvdSlot = lstControllerDvdSlots.begin();
    if (mpInstaller->isAuxiliaryIsoNeeded() && mpInstaller->bootFromAuxiliaryIso())
    {
        rVecInstallatationDisks.push_back(UnattendedInstallationDisk(itDvdSlot, mpInstaller->getAuxiliaryIsoFilePath(), true));
        ++itDvdSlot;
    }

    if (mpInstaller->isOriginalIsoNeeded())
    {
        rVecInstallatationDisks.push_back(UnattendedInstallationDisk(itDvdSlot, i_getIsoPath(), false));
        ++itDvdSlot;
    }

    if (mpInstaller->isAuxiliaryIsoNeeded() && !mpInstaller->bootFromAuxiliaryIso())
    {
        rVecInstallatationDisks.push_back(UnattendedInstallationDisk(itDvdSlot, mpInstaller->getAuxiliaryIsoFilePath(), true));
        ++itDvdSlot;
    }

#if 0 /* These are now in the AUX VISO. */
    if (mpInstaller->isAdditionsIsoNeeded())
    {
        rVecInstallatationDisks.push_back(UnattendedInstallationDisk(itDvdSlot, i_getAdditionsIsoPath(), false));
        ++itDvdSlot;
    }

    if (mpInstaller->isValidationKitIsoNeeded())
    {
        rVecInstallatationDisks.push_back(UnattendedInstallationDisk(itDvdSlot, i_getValidationKitIsoPath(), false));
        ++itDvdSlot;
    }
#endif

    return S_OK;
}

/**
 * Used to find more free slots for DVD drives during VM reconfiguration.
 *
 * This may modify the @a portCount property of the given controller.
 *
 * @returns COM status code.
 * @param   rStrControllerName      The name of the controller to find/create
 *                                  free slots on.
 * @param   enmStorageBus           The storage bus type.
 * @param   rPtrSessionMachine      Reference to the session machine.
 * @param   cSlotsNeeded            Total slots needed (including those we've
 *                                  already found).
 * @param   rDvdSlots               The slot collection for DVD drives to add
 *                                  free slots to as we find/create them.
 */
HRESULT Unattended::i_findOrCreateNeededFreeSlots(const Utf8Str &rStrControllerName, StorageBus_T enmStorageBus,
                                                  ComPtr<IMachine> const &rPtrSessionMachine, uint32_t cSlotsNeeded,
                                                  std::list<ControllerSlot> &rDvdSlots)
{
    Assert(cSlotsNeeded > rDvdSlots.size());

    /*
     * Get controlleer stats.
     */
    ComPtr<IStorageController> pController;
    HRESULT hrc = rPtrSessionMachine->GetStorageControllerByName(Bstr(rStrControllerName).raw(), pController.asOutParam());
    AssertComRCReturn(hrc, hrc);

    ULONG cMaxDevicesPerPort = 1;
    hrc = pController->COMGETTER(MaxDevicesPerPortCount)(&cMaxDevicesPerPort);
    AssertComRCReturn(hrc, hrc);
    AssertLogRelReturn(cMaxDevicesPerPort > 0, E_UNEXPECTED);

    ULONG cPorts = 0;
    hrc = pController->COMGETTER(PortCount)(&cPorts);
    AssertComRCReturn(hrc, hrc);

    /*
     * Get the attachment list and turn into an internal list for lookup speed.
     */
    com::SafeIfaceArray<IMediumAttachment> arrayOfMediumAttachments;
    hrc = rPtrSessionMachine->GetMediumAttachmentsOfController(Bstr(rStrControllerName).raw(),
                                                               ComSafeArrayAsOutParam(arrayOfMediumAttachments));
    AssertComRCReturn(hrc, hrc);

    std::vector<ControllerSlot> arrayOfUsedSlots;
    for (size_t i = 0; i < arrayOfMediumAttachments.size(); i++)
    {
        LONG iPort = -1;
        hrc = arrayOfMediumAttachments[i]->COMGETTER(Port)(&iPort);
        AssertComRCReturn(hrc, hrc);

        LONG iDevice = -1;
        hrc = arrayOfMediumAttachments[i]->COMGETTER(Device)(&iDevice);
        AssertComRCReturn(hrc, hrc);

        arrayOfUsedSlots.push_back(ControllerSlot(enmStorageBus, Utf8Str::Empty, iPort, iDevice, false /*fFree*/));
    }

    /*
     * Iterate thru all possible slots, adding those not found in arrayOfUsedSlots.
     */
    for (int32_t iPort = 0; iPort < (int32_t)cPorts; iPort++)
        for (int32_t iDevice = 0; iDevice < (int32_t)cMaxDevicesPerPort; iDevice++)
        {
            bool fFound = false;
            for (size_t i = 0; i < arrayOfUsedSlots.size(); i++)
                if (   arrayOfUsedSlots[i].iPort   == iPort
                    && arrayOfUsedSlots[i].iDevice == iDevice)
                {
                    fFound = true;
                    break;
                }
            if (!fFound)
            {
                rDvdSlots.push_back(ControllerSlot(enmStorageBus, rStrControllerName, iPort, iDevice, true /*fFree*/));
                if (rDvdSlots.size() >= cSlotsNeeded)
                    return S_OK;
            }
        }

    /*
     * Okay we still need more ports.  See if increasing the number of controller
     * ports would solve it.
     */
    ULONG cMaxPorts = 1;
    hrc = pController->COMGETTER(MaxPortCount)(&cMaxPorts);
    AssertComRCReturn(hrc, hrc);
    if (cMaxPorts <= cPorts)
        return S_OK;
    size_t cNewPortsNeeded = (cSlotsNeeded - rDvdSlots.size() + cMaxDevicesPerPort - 1) / cMaxDevicesPerPort;
    if (cPorts + cNewPortsNeeded > cMaxPorts)
        return S_OK;

    /*
     * Raise the port count and add the free slots we've just created.
     */
    hrc = pController->COMSETTER(PortCount)(cPorts + (ULONG)cNewPortsNeeded);
    AssertComRCReturn(hrc, hrc);
    int32_t const cPortsNew = (int32_t)(cPorts + cNewPortsNeeded);
    for (int32_t iPort = (int32_t)cPorts; iPort < cPortsNew; iPort++)
        for (int32_t iDevice = 0; iDevice < (int32_t)cMaxDevicesPerPort; iDevice++)
        {
            rDvdSlots.push_back(ControllerSlot(enmStorageBus, rStrControllerName, iPort, iDevice, true /*fFree*/));
            if (rDvdSlots.size() >= cSlotsNeeded)
                return S_OK;
        }

    /* We should not get here! */
    AssertLogRelFailedReturn(E_UNEXPECTED);
}

HRESULT Unattended::done()
{
    LogFlow(("Unattended::done\n"));
    if (mpInstaller)
    {
        LogRelFlow(("Unattended::done: Deleting installer object (%p)\n", mpInstaller));
        delete mpInstaller;
        mpInstaller = NULL;
    }
    return S_OK;
}

HRESULT Unattended::getIsoPath(com::Utf8Str &isoPath)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    isoPath = mStrIsoPath;
    return S_OK;
}

HRESULT Unattended::setIsoPath(const com::Utf8Str &isoPath)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("Cannot change after prepare() has been called")));
    mStrIsoPath       = isoPath;
    mfDoneDetectIsoOS = false;
    return S_OK;
}

HRESULT Unattended::getUser(com::Utf8Str &user)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    user = mStrUser;
    return S_OK;
}


HRESULT Unattended::setUser(const com::Utf8Str &user)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("Cannot change after prepare() has been called")));
    mStrUser = user;
    return S_OK;
}

HRESULT Unattended::getUserPassword(com::Utf8Str &password)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    password = mStrUserPassword;
    return S_OK;
}

HRESULT Unattended::setUserPassword(const com::Utf8Str &password)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("Cannot change after prepare() has been called")));
    mStrUserPassword = password;
    return S_OK;
}

HRESULT Unattended::getAdminPassword(com::Utf8Str &password)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    password = mStrAdminPassword;
    return S_OK;
}

HRESULT Unattended::setAdminPassword(const com::Utf8Str &password)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("Cannot change after prepare() has been called")));
    mStrAdminPassword = password;
    return S_OK;
}

HRESULT Unattended::getFullUserName(com::Utf8Str &fullUserName)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    fullUserName = mStrFullUserName;
    return S_OK;
}

HRESULT Unattended::setFullUserName(const com::Utf8Str &fullUserName)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("Cannot change after prepare() has been called")));
    mStrFullUserName = fullUserName;
    return S_OK;
}

HRESULT Unattended::getProductKey(com::Utf8Str &productKey)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    productKey = mStrProductKey;
    return S_OK;
}

HRESULT Unattended::setProductKey(const com::Utf8Str &productKey)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("Cannot change after prepare() has been called")));
    mStrProductKey = productKey;
    return S_OK;
}

HRESULT Unattended::getAdditionsIsoPath(com::Utf8Str &additionsIsoPath)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    additionsIsoPath = mStrAdditionsIsoPath;
    return S_OK;
}

HRESULT Unattended::setAdditionsIsoPath(const com::Utf8Str &additionsIsoPath)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("Cannot change after prepare() has been called")));
    mStrAdditionsIsoPath = additionsIsoPath;
    return S_OK;
}

HRESULT Unattended::getInstallGuestAdditions(BOOL *installGuestAdditions)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *installGuestAdditions = mfInstallGuestAdditions;
    return S_OK;
}

HRESULT Unattended::setInstallGuestAdditions(BOOL installGuestAdditions)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("Cannot change after prepare() has been called")));
    mfInstallGuestAdditions = installGuestAdditions != FALSE;
    return S_OK;
}

HRESULT Unattended::getValidationKitIsoPath(com::Utf8Str &aValidationKitIsoPath)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aValidationKitIsoPath = mStrValidationKitIsoPath;
    return S_OK;
}

HRESULT Unattended::setValidationKitIsoPath(const com::Utf8Str &aValidationKitIsoPath)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("Cannot change after prepare() has been called")));
    mStrValidationKitIsoPath = aValidationKitIsoPath;
    return S_OK;
}

HRESULT Unattended::getInstallTestExecService(BOOL *aInstallTestExecService)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aInstallTestExecService = mfInstallTestExecService;
    return S_OK;
}

HRESULT Unattended::setInstallTestExecService(BOOL aInstallTestExecService)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("Cannot change after prepare() has been called")));
    mfInstallTestExecService = aInstallTestExecService != FALSE;
    return S_OK;
}

HRESULT Unattended::getUserPayloadIsoPath(com::Utf8Str &aUserPayloadIsoPath)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aUserPayloadIsoPath = mStrUserPayloadIsoPath;
    return S_OK;
}

HRESULT Unattended::setUserPayloadIsoPath(const com::Utf8Str &aUserPayloadIsoPath)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("Cannot change after prepare() has been called")));
    mStrUserPayloadIsoPath = aUserPayloadIsoPath;
    return S_OK;
}

HRESULT Unattended::getInstallUserPayload(BOOL *aInstallUserPayload)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aInstallUserPayload = mfInstallUserPayload;
    return S_OK;
}

HRESULT Unattended::setInstallUserPayload(BOOL aInstallUserPayload)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("Cannot change after prepare() has been called")));
    mfInstallUserPayload = aInstallUserPayload != FALSE;
    return S_OK;
}

HRESULT Unattended::getTimeZone(com::Utf8Str &aTimeZone)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aTimeZone = mStrTimeZone;
    return S_OK;
}

HRESULT Unattended::setTimeZone(const com::Utf8Str &aTimezone)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("Cannot change after prepare() has been called")));
    mStrTimeZone = aTimezone;
    return S_OK;
}

HRESULT Unattended::getKeyboardLayout(com::Utf8Str &aKeyboardLayout)
{
    RT_NOREF(aKeyboardLayout);
    return E_NOTIMPL;
}

HRESULT Unattended::setKeyboardLayout(const com::Utf8Str &aKeyboardLayout)
{
    RT_NOREF(aKeyboardLayout);
    return E_NOTIMPL;
}

HRESULT Unattended::getKeyboardVariant(com::Utf8Str &aKeyboardVariant)
{
    RT_NOREF(aKeyboardVariant);
    return E_NOTIMPL;
}

HRESULT Unattended::setKeyboardVariant(const com::Utf8Str &aKeyboardVariant)
{
    RT_NOREF(aKeyboardVariant);
    return E_NOTIMPL;
}

HRESULT Unattended::getLocale(com::Utf8Str &aLocale)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aLocale = mStrLocale;
    return S_OK;
}

HRESULT Unattended::setLocale(const com::Utf8Str &aLocale)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("Cannot change after prepare() has been called")));
    if (    aLocale.isEmpty() /* use default */
        || (   aLocale.length() == 5
            && RT_C_IS_LOWER(aLocale[0])
            && RT_C_IS_LOWER(aLocale[1])
            && aLocale[2] == '_'
            && RT_C_IS_UPPER(aLocale[3])
            && RT_C_IS_UPPER(aLocale[4])) )
    {
        mStrLocale = aLocale;
        return S_OK;
    }
    return setError(E_INVALIDARG, tr("Expected two lower cased letters, an underscore, and two upper cased letters"));
}

HRESULT Unattended::getLanguage(com::Utf8Str &aLanguage)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aLanguage = mStrLanguage;
    return S_OK;
}

HRESULT Unattended::setLanguage(const com::Utf8Str &aLanguage)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("Cannot change after prepare() has been called")));
    mStrLanguage = aLanguage;
    return S_OK;
}

HRESULT Unattended::getCountry(com::Utf8Str &aCountry)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aCountry = mStrCountry;
    return S_OK;
}

HRESULT Unattended::setCountry(const com::Utf8Str &aCountry)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("Cannot change after prepare() has been called")));
    if (   aCountry.isEmpty()
        || (   aCountry.length() == 2
            && RT_C_IS_UPPER(aCountry[0])
            && RT_C_IS_UPPER(aCountry[1])) )
    {
        mStrCountry = aCountry;
        return S_OK;
    }
    return setError(E_INVALIDARG, tr("Expected two upper cased letters"));
}

HRESULT Unattended::getProxy(com::Utf8Str &aProxy)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aProxy = mStrProxy; /// @todo turn schema map into string or something.
    return S_OK;
}

HRESULT Unattended::setProxy(const com::Utf8Str &aProxy)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("Cannot change after prepare() has been called")));
    if (aProxy.isEmpty())
    {
        /* set default proxy */
        /** @todo BUGBUG! implement this */
    }
    else if (aProxy.equalsIgnoreCase("none"))
    {
        /* clear proxy config */
        mStrProxy.setNull();
    }
    else
    {
        /** @todo Parse and set proxy config into a schema map or something along those lines. */
        /** @todo BUGBUG! implement this */
        // return E_NOTIMPL;
        mStrProxy = aProxy;
    }
    return S_OK;
}

HRESULT Unattended::getPackageSelectionAdjustments(com::Utf8Str &aPackageSelectionAdjustments)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aPackageSelectionAdjustments = RTCString::join(mPackageSelectionAdjustments, ";");
    return S_OK;
}

HRESULT Unattended::setPackageSelectionAdjustments(const com::Utf8Str &aPackageSelectionAdjustments)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("Cannot change after prepare() has been called")));
    if (aPackageSelectionAdjustments.isEmpty())
        mPackageSelectionAdjustments.clear();
    else
    {
        RTCList<RTCString, RTCString *> arrayStrSplit = aPackageSelectionAdjustments.split(";");
        for (size_t i = 0; i < arrayStrSplit.size(); i++)
        {
            if (arrayStrSplit[i].equals("minimal"))
            { /* okay */ }
            else
                return setError(E_INVALIDARG, tr("Unknown keyword: %s"), arrayStrSplit[i].c_str());
        }
        mPackageSelectionAdjustments = arrayStrSplit;
    }
    return S_OK;
}

HRESULT Unattended::getHostname(com::Utf8Str &aHostname)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aHostname = mStrHostname;
    return S_OK;
}

HRESULT Unattended::setHostname(const com::Utf8Str &aHostname)
{
    /*
     * Validate input.
     */
    if (aHostname.length() > (aHostname.endsWith(".") ? 254U : 253U))
        return setErrorBoth(E_INVALIDARG, VERR_INVALID_NAME,
                            tr("Hostname '%s' is %zu bytes long, max is 253 (excluding trailing dot)", "", aHostname.length()),
                            aHostname.c_str(), aHostname.length());
    size_t      cLabels  = 0;
    const char *pszSrc   = aHostname.c_str();
    for (;;)
    {
        size_t cchLabel = 1;
        char ch = *pszSrc++;
        if (RT_C_IS_ALNUM(ch))
        {
            cLabels++;
            while ((ch = *pszSrc++) != '.' && ch != '\0')
            {
                if (RT_C_IS_ALNUM(ch) || ch == '-')
                {
                    if (cchLabel < 63)
                        cchLabel++;
                    else
                        return setErrorBoth(E_INVALIDARG, VERR_INVALID_NAME,
                                            tr("Invalid hostname '%s' - label %u is too long, max is 63."),
                                            aHostname.c_str(), cLabels);
                }
                else
                    return setErrorBoth(E_INVALIDARG, VERR_INVALID_NAME,
                                        tr("Invalid hostname '%s' - illegal char '%c' at position %zu"),
                                        aHostname.c_str(), ch, pszSrc - aHostname.c_str() - 1);
            }
            if (cLabels == 1 && cchLabel < 2)
                return setErrorBoth(E_INVALIDARG, VERR_INVALID_NAME,
                                    tr("Invalid hostname '%s' - the name part must be at least two characters long"),
                                    aHostname.c_str());
            if (ch == '\0')
                break;
        }
        else if (ch != '\0')
            return setErrorBoth(E_INVALIDARG, VERR_INVALID_NAME,
                                tr("Invalid hostname '%s' - illegal lead char '%c' at position %zu"),
                                aHostname.c_str(), ch, pszSrc - aHostname.c_str() - 1);
        else
            return setErrorBoth(E_INVALIDARG, VERR_INVALID_NAME,
                                tr("Invalid hostname '%s' - trailing dot not permitted"), aHostname.c_str());
    }
    if (cLabels < 2)
        return setErrorBoth(E_INVALIDARG, VERR_INVALID_NAME,
                            tr("Incomplete hostname '%s' - must include both a name and a domain"), aHostname.c_str());

    /*
     * Make the change.
     */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("Cannot change after prepare() has been called")));
    mStrHostname = aHostname;
    return S_OK;
}

HRESULT Unattended::getAuxiliaryBasePath(com::Utf8Str &aAuxiliaryBasePath)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aAuxiliaryBasePath = mStrAuxiliaryBasePath;
    return S_OK;
}

HRESULT Unattended::setAuxiliaryBasePath(const com::Utf8Str &aAuxiliaryBasePath)
{
    if (aAuxiliaryBasePath.isEmpty())
        return setError(E_INVALIDARG, tr("Empty base path is not allowed"));
    if (!RTPathStartsWithRoot(aAuxiliaryBasePath.c_str()))
        return setError(E_INVALIDARG, tr("Base path must be absolute"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("Cannot change after prepare() has been called")));
    mStrAuxiliaryBasePath = aAuxiliaryBasePath;
    mfIsDefaultAuxiliaryBasePath = mStrAuxiliaryBasePath.isEmpty();
    return S_OK;
}

HRESULT Unattended::getImageIndex(ULONG *index)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *index = midxImage;
    return S_OK;
}

HRESULT Unattended::setImageIndex(ULONG index)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("Cannot change after prepare() has been called")));

    /* Validate the selection if detection was done already: */
    if (mDetectedImages.size() > 0)
    {
        for (size_t i = 0; i < mDetectedImages.size(); i++)
            if (mDetectedImages[i].mImageIndex == index)
            {
                midxImage = index;
                i_updateDetectedAttributeForImage(mDetectedImages[i]);
                return S_OK;
            }
        LogRel(("Unattended: Setting invalid index=%u\n", index)); /** @todo fail? */
    }

    midxImage = index;
    return S_OK;
}

HRESULT Unattended::getMachine(ComPtr<IMachine> &aMachine)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    return mMachine.queryInterfaceTo(aMachine.asOutParam());
}

HRESULT Unattended::setMachine(const ComPtr<IMachine> &aMachine)
{
    /*
     * Lookup the VM so we can safely get the Machine instance.
     * (Don't want to test how reliable XPCOM and COM are with finding
     * the local object instance when a client passes a stub back.)
     */
    Bstr bstrUuidMachine;
    HRESULT hrc = aMachine->COMGETTER(Id)(bstrUuidMachine.asOutParam());
    if (SUCCEEDED(hrc))
    {
        Guid UuidMachine(bstrUuidMachine);
        ComObjPtr<Machine> ptrMachine;
        hrc = mParent->i_findMachine(UuidMachine, false /*fPermitInaccessible*/, true /*aSetError*/, &ptrMachine);
        if (SUCCEEDED(hrc))
        {
            AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
            AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER,
                                                           tr("Cannot change after prepare() has been called")));
            mMachine     = ptrMachine;
            mMachineUuid = UuidMachine;
            if (mfIsDefaultAuxiliaryBasePath)
                mStrAuxiliaryBasePath.setNull();
            hrc = S_OK;
        }
    }
    return hrc;
}

HRESULT Unattended::getScriptTemplatePath(com::Utf8Str &aScriptTemplatePath)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (   mStrScriptTemplatePath.isNotEmpty()
        || mpInstaller == NULL)
        aScriptTemplatePath = mStrScriptTemplatePath;
    else
        aScriptTemplatePath = mpInstaller->getTemplateFilePath();
    return S_OK;
}

HRESULT Unattended::setScriptTemplatePath(const com::Utf8Str &aScriptTemplatePath)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("Cannot change after prepare() has been called")));
    mStrScriptTemplatePath = aScriptTemplatePath;
    return S_OK;
}

HRESULT Unattended::getPostInstallScriptTemplatePath(com::Utf8Str &aPostInstallScriptTemplatePath)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (   mStrPostInstallScriptTemplatePath.isNotEmpty()
        || mpInstaller == NULL)
        aPostInstallScriptTemplatePath = mStrPostInstallScriptTemplatePath;
    else
        aPostInstallScriptTemplatePath = mpInstaller->getPostTemplateFilePath();
    return S_OK;
}

HRESULT Unattended::setPostInstallScriptTemplatePath(const com::Utf8Str &aPostInstallScriptTemplatePath)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("Cannot change after prepare() has been called")));
    mStrPostInstallScriptTemplatePath = aPostInstallScriptTemplatePath;
    return S_OK;
}

HRESULT Unattended::getPostInstallCommand(com::Utf8Str &aPostInstallCommand)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aPostInstallCommand = mStrPostInstallCommand;
    return S_OK;
}

HRESULT Unattended::setPostInstallCommand(const com::Utf8Str &aPostInstallCommand)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("Cannot change after prepare() has been called")));
    mStrPostInstallCommand = aPostInstallCommand;
    return S_OK;
}

HRESULT Unattended::getExtraInstallKernelParameters(com::Utf8Str &aExtraInstallKernelParameters)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (   mStrExtraInstallKernelParameters.isNotEmpty()
        || mpInstaller == NULL)
        aExtraInstallKernelParameters = mStrExtraInstallKernelParameters;
    else
        aExtraInstallKernelParameters = mpInstaller->getDefaultExtraInstallKernelParameters();
    return S_OK;
}

HRESULT Unattended::setExtraInstallKernelParameters(const com::Utf8Str &aExtraInstallKernelParameters)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("Cannot change after prepare() has been called")));
    mStrExtraInstallKernelParameters = aExtraInstallKernelParameters;
    return S_OK;
}

HRESULT Unattended::getDetectedOSTypeId(com::Utf8Str &aDetectedOSTypeId)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aDetectedOSTypeId = mStrDetectedOSTypeId;
    return S_OK;
}

HRESULT Unattended::getDetectedOSVersion(com::Utf8Str &aDetectedOSVersion)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aDetectedOSVersion = mStrDetectedOSVersion;
    return S_OK;
}

HRESULT Unattended::getDetectedOSFlavor(com::Utf8Str &aDetectedOSFlavor)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aDetectedOSFlavor = mStrDetectedOSFlavor;
    return S_OK;
}

HRESULT Unattended::getDetectedOSLanguages(com::Utf8Str &aDetectedOSLanguages)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aDetectedOSLanguages = RTCString::join(mDetectedOSLanguages, " ");
    return S_OK;
}

HRESULT Unattended::getDetectedOSHints(com::Utf8Str &aDetectedOSHints)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aDetectedOSHints = mStrDetectedOSHints;
    return S_OK;
}

HRESULT Unattended::getDetectedImageNames(std::vector<com::Utf8Str> &aDetectedImageNames)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aDetectedImageNames.clear();
    for (size_t i = 0; i < mDetectedImages.size(); ++i)
    {
        Utf8Str strTmp;
        aDetectedImageNames.push_back(mDetectedImages[i].formatName(strTmp));
    }
    return S_OK;
}

HRESULT Unattended::getDetectedImageIndices(std::vector<ULONG> &aDetectedImageIndices)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aDetectedImageIndices.clear();
    for (size_t i = 0; i < mDetectedImages.size(); ++i)
        aDetectedImageIndices.push_back(mDetectedImages[i].mImageIndex);
    return S_OK;
}

HRESULT Unattended::getIsUnattendedInstallSupported(BOOL *aIsUnattendedInstallSupported)
{
    /*
     * Take the initial position that it's not supported, so we can return
     * right away when we decide it's not possible.
     */
    *aIsUnattendedInstallSupported = false;

    /* Unattended is disabled by default if we could not detect OS type. */
    if (mStrDetectedOSTypeId.isEmpty())
        return S_OK;

    /* Note! Includes the OS family and the distro (linux) or (part) of the
             major OS version. Use with care. */
    const VBOXOSTYPE enmOsTypeMasked = (VBOXOSTYPE)(mEnmOsType & VBOXOSTYPE_OsTypeMask);

    /* We require a version to have been detected, except for windows where the
       field is generally only used for the service pack number at present and
       will be empty for RTMs isos. */
    if (   (   enmOsTypeMasked <= VBOXOSTYPE_WinNT
            || enmOsTypeMasked >= VBOXOSTYPE_OS2)
        && mStrDetectedOSVersion.isEmpty())
        return S_OK;

    /*
     * Sort out things that we know doesn't work.  Order by VBOXOSTYPE value.
     */

    /* We do not support any of the DOS based windows version, nor DOS, in case
       any of that gets detected (it shouldn't): */
    if (enmOsTypeMasked >= VBOXOSTYPE_DOS && enmOsTypeMasked < VBOXOSTYPE_WinNT)
        return S_OK;

    /* Windows NT 3.x doesn't work, also skip unknown windows NT version: */
    if (enmOsTypeMasked >= VBOXOSTYPE_WinNT && enmOsTypeMasked < VBOXOSTYPE_WinNT4)
        return S_OK;

    /* For OS/2 we only support OS2 4.5 (actually only 4.52 server has been
       tested, but we'll get to the others eventually): */
    if (   enmOsTypeMasked >= VBOXOSTYPE_OS2
        && enmOsTypeMasked < VBOXOSTYPE_Linux
        && enmOsTypeMasked != VBOXOSTYPE_OS2Warp45 /* probably works */ )
        return S_OK;

    /* Old Debians fail since package repos have been move to some other mirror location. */
    if (   enmOsTypeMasked == VBOXOSTYPE_Debian
        && RTStrVersionCompare(mStrDetectedOSVersion.c_str(), "9.0") < 0)
        return S_OK;

    /* Skip all OpenSUSE variants for now. */
    if (enmOsTypeMasked == VBOXOSTYPE_OpenSUSE)
        return S_OK;

    if (enmOsTypeMasked == VBOXOSTYPE_Ubuntu)
    {
        /* We cannot install Ubuntus older than 11.04. */
        if (RTStrVersionCompare(mStrDetectedOSVersion.c_str(), "11.04") < 0)
            return S_OK;
        /* Lubuntu, starting with 20.04, has switched to calamares, which cannot be automated. */
        if (   RTStrIStr(mStrDetectedOSFlavor.c_str(), "lubuntu")
            && RTStrVersionCompare(mStrDetectedOSVersion.c_str(), "20.04") > 0)
            return S_OK;
    }

    /* Earlier than OL 6.4 cannot be installed. OL 6.x fails with unsupported hardware error (CPU family). */
    if (   enmOsTypeMasked == VBOXOSTYPE_Oracle
        && RTStrVersionCompare(mStrDetectedOSVersion.c_str(), "6.4") < 0)
        return S_OK;

    /* Fredora ISOs cannot be installed at present. */
    if (enmOsTypeMasked == VBOXOSTYPE_FedoraCore)
        return S_OK;

    /*
     * Assume the rest works.
     */
    *aIsUnattendedInstallSupported = true;
    return S_OK;
}

HRESULT Unattended::getAvoidUpdatesOverNetwork(BOOL *aAvoidUpdatesOverNetwork)
{
    *aAvoidUpdatesOverNetwork = mfAvoidUpdatesOverNetwork;
    return S_OK;
}

HRESULT Unattended::setAvoidUpdatesOverNetwork(BOOL aAvoidUpdatesOverNetwork)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mpInstaller == NULL, setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("Cannot change after prepare() has been called")));
    mfAvoidUpdatesOverNetwork = RT_BOOL(aAvoidUpdatesOverNetwork);
    return S_OK;
}

/*
 * Getters that the installer and script classes can use.
 */
Utf8Str const &Unattended::i_getIsoPath() const
{
    Assert(isReadLockedOnCurrentThread());
    return mStrIsoPath;
}

Utf8Str const &Unattended::i_getUser() const
{
    Assert(isReadLockedOnCurrentThread());
    return mStrUser;
}

Utf8Str const &Unattended::i_getUserPassword() const
{
    Assert(isReadLockedOnCurrentThread());
    return mStrUserPassword;
}

Utf8Str const &Unattended::i_getAdminPassword() const
{
    Assert(isReadLockedOnCurrentThread());

    /* If no Administrator / 'root' password is being set, the user password will be used instead.
     * Also see API documentation. */
    return mStrAdminPassword.isEmpty() ? mStrUserPassword : mStrAdminPassword;
}

Utf8Str const &Unattended::i_getFullUserName() const
{
    Assert(isReadLockedOnCurrentThread());
    return mStrFullUserName.isNotEmpty() ? mStrFullUserName : mStrUser;
}

Utf8Str const &Unattended::i_getProductKey() const
{
    Assert(isReadLockedOnCurrentThread());
    return mStrProductKey;
}

Utf8Str const &Unattended::i_getProxy() const
{
    Assert(isReadLockedOnCurrentThread());
    return mStrProxy;
}

Utf8Str const &Unattended::i_getAdditionsIsoPath() const
{
    Assert(isReadLockedOnCurrentThread());
    return mStrAdditionsIsoPath;
}

bool           Unattended::i_getInstallGuestAdditions() const
{
    Assert(isReadLockedOnCurrentThread());
    return mfInstallGuestAdditions;
}

Utf8Str const &Unattended::i_getValidationKitIsoPath() const
{
    Assert(isReadLockedOnCurrentThread());
    return mStrValidationKitIsoPath;
}

bool           Unattended::i_getInstallTestExecService() const
{
    Assert(isReadLockedOnCurrentThread());
    return mfInstallTestExecService;
}

Utf8Str const &Unattended::i_getUserPayloadIsoPath() const
{
    Assert(isReadLockedOnCurrentThread());
    return mStrUserPayloadIsoPath;
}

bool           Unattended::i_getInstallUserPayload() const
{
    Assert(isReadLockedOnCurrentThread());
    return mfInstallUserPayload;
}

Utf8Str const &Unattended::i_getTimeZone() const
{
    Assert(isReadLockedOnCurrentThread());
    return mStrTimeZone;
}

PCRTTIMEZONEINFO Unattended::i_getTimeZoneInfo() const
{
    Assert(isReadLockedOnCurrentThread());
    return mpTimeZoneInfo;
}

Utf8Str const &Unattended::i_getLocale() const
{
    Assert(isReadLockedOnCurrentThread());
    return mStrLocale;
}

Utf8Str const &Unattended::i_getLanguage() const
{
    Assert(isReadLockedOnCurrentThread());
    return mStrLanguage;
}

Utf8Str const &Unattended::i_getCountry() const
{
    Assert(isReadLockedOnCurrentThread());
    return mStrCountry;
}

bool Unattended::i_isMinimalInstallation() const
{
    size_t i = mPackageSelectionAdjustments.size();
    while (i-- > 0)
        if (mPackageSelectionAdjustments[i].equals("minimal"))
            return true;
    return false;
}

Utf8Str const &Unattended::i_getHostname() const
{
    Assert(isReadLockedOnCurrentThread());
    return mStrHostname;
}

Utf8Str const &Unattended::i_getAuxiliaryBasePath() const
{
    Assert(isReadLockedOnCurrentThread());
    return mStrAuxiliaryBasePath;
}

ULONG Unattended::i_getImageIndex() const
{
    Assert(isReadLockedOnCurrentThread());
    return midxImage;
}

Utf8Str const &Unattended::i_getScriptTemplatePath() const
{
    Assert(isReadLockedOnCurrentThread());
    return mStrScriptTemplatePath;
}

Utf8Str const &Unattended::i_getPostInstallScriptTemplatePath() const
{
    Assert(isReadLockedOnCurrentThread());
    return mStrPostInstallScriptTemplatePath;
}

Utf8Str const &Unattended::i_getPostInstallCommand() const
{
    Assert(isReadLockedOnCurrentThread());
    return mStrPostInstallCommand;
}

Utf8Str const &Unattended::i_getAuxiliaryInstallDir() const
{
    Assert(isReadLockedOnCurrentThread());
    /* Only the installer knows, forward the call. */
    AssertReturn(mpInstaller != NULL, Utf8Str::Empty);
    return mpInstaller->getAuxiliaryInstallDir();
}

Utf8Str const &Unattended::i_getExtraInstallKernelParameters() const
{
    Assert(isReadLockedOnCurrentThread());
    return mStrExtraInstallKernelParameters;
}

Utf8Str const &Unattended::i_getAdditionsInstallPackage() const
{
    Assert(isReadLockedOnCurrentThread());
    return mStrAdditionsInstallPackage;
}

bool Unattended::i_isRtcUsingUtc() const
{
    Assert(isReadLockedOnCurrentThread());
    return mfRtcUseUtc;
}

bool Unattended::i_isGuestOs64Bit() const
{
    Assert(isReadLockedOnCurrentThread());
    return mfGuestOs64Bit;
}

bool Unattended::i_isFirmwareEFI() const
{
    Assert(isReadLockedOnCurrentThread());
    return menmFirmwareType != FirmwareType_BIOS;
}

Utf8Str const &Unattended::i_getDetectedOSVersion()
{
    Assert(isReadLockedOnCurrentThread());
    return mStrDetectedOSVersion;
}

bool Unattended::i_getAvoidUpdatesOverNetwork() const
{
    Assert(isReadLockedOnCurrentThread());
    return mfAvoidUpdatesOverNetwork;
}

HRESULT Unattended::i_attachImage(UnattendedInstallationDisk const *pImage, ComPtr<IMachine> const &rPtrSessionMachine,
                                  AutoMultiWriteLock2 &rLock)
{
    /*
     * Attach the disk image
     * HACK ALERT! Temporarily release the Unattended lock.
     */
    rLock.release();

    ComPtr<IMedium> ptrMedium;
    HRESULT hrc = mParent->OpenMedium(Bstr(pImage->strImagePath).raw(),
                                     pImage->enmDeviceType,
                                     pImage->enmAccessType,
                                     true,
                                     ptrMedium.asOutParam());
    LogRelFlowFunc(("VirtualBox::openMedium -> %Rhrc\n", hrc));
    if (SUCCEEDED(hrc))
    {
        if (pImage->fAuxiliary && pImage->strImagePath.endsWith(".viso"))
        {
            hrc = ptrMedium->SetProperty(Bstr("UnattendedInstall").raw(), Bstr("1").raw());
            LogRelFlowFunc(("Medium::SetProperty -> %Rhrc\n", hrc));
        }
        if (pImage->fMountOnly)
        {
            // mount the opened disk image
            hrc = rPtrSessionMachine->MountMedium(Bstr(pImage->strControllerName).raw(), pImage->iPort,
                                                  pImage->iDevice, ptrMedium, TRUE /*fForce*/);
            LogRelFlowFunc(("Machine::MountMedium -> %Rhrc\n", hrc));
        }
        else
        {
            //attach the opened disk image to the controller
            hrc = rPtrSessionMachine->AttachDevice(Bstr(pImage->strControllerName).raw(), pImage->iPort,
                                                   pImage->iDevice, pImage->enmDeviceType, ptrMedium);
            LogRelFlowFunc(("Machine::AttachDevice -> %Rhrc\n", hrc));
        }
    }

    rLock.acquire();
    return hrc;
}

bool Unattended::i_isGuestOSArchX64(Utf8Str const &rStrGuestOsTypeId)
{
    ComPtr<IGuestOSType> pGuestOSType;
    HRESULT hrc = mParent->GetGuestOSType(Bstr(rStrGuestOsTypeId).raw(), pGuestOSType.asOutParam());
    if (SUCCEEDED(hrc))
    {
        BOOL fIs64Bit = FALSE;
        if (!pGuestOSType.isNull())
            hrc = pGuestOSType->COMGETTER(Is64Bit)(&fIs64Bit);
        if (SUCCEEDED(hrc))
            return fIs64Bit != FALSE;
    }
    return false;
}


bool Unattended::i_updateDetectedAttributeForImage(WIMImage const &rImage)
{
    bool fRet = true;

    /*
     * If the image doesn't have a valid value, we don't change it.
     * This is obviously a little bit bogus, but what can we do...
     */
    const char *pszOSTypeId = Global::OSTypeId(rImage.mOSType);
    if (pszOSTypeId && !RTStrStartsWith(pszOSTypeId, GUEST_OS_ID_STR_PARTIAL("Other"))) /** @todo set x64/a64 other variants or not? */
        mStrDetectedOSTypeId = pszOSTypeId;
    else
        fRet = false;

    if (rImage.mVersion.isNotEmpty())
        mStrDetectedOSVersion = rImage.mVersion;
    else
        fRet = false;

    if (rImage.mFlavor.isNotEmpty())
        mStrDetectedOSFlavor = rImage.mFlavor;
    else
        fRet = false;

    if (rImage.mLanguages.size() > 0)
        mDetectedOSLanguages  = rImage.mLanguages;
    else
        fRet = false;

    mEnmOsType = rImage.mOSType;

    return fRet;
}

HRESULT Unattended::i_getListOfSupportedGuestOS()
{
    HRESULT       hrc;
    if (mfDoneSupportedGuestOSList)
        return S_OK;
    mfDoneSupportedGuestOSList = true;

    /* Get a list of guest OS Type Ids supported by the host. */
    ComPtr<ISystemProperties> pSystemProperties;

    hrc = mParent->COMGETTER(SystemProperties)(pSystemProperties.asOutParam());
    if (SUCCEEDED(hrc))
    {
        ComPtr<IPlatformProperties> pPlatformProperties;
        hrc = pSystemProperties->COMGETTER(Platform)(pPlatformProperties.asOutParam());
        if (SUCCEEDED(hrc))
        {
            hrc = pPlatformProperties->COMGETTER(SupportedGuestOSTypes)(ComSafeArrayAsOutParam(mSupportedGuestOSTypes));
            if (!SUCCEEDED(hrc))
                mSupportedGuestOSTypes.resize(0);
        }
    }
    return hrc;
}
