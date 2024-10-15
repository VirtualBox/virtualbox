/* $Id: tstUnattendedScript.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * tstUnattendedScript - testcases for UnattendedScript.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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
#include "UnattendedScript.h"

#include <VBox/com/VirtualBox.h>
#include <VBox/com/errorprint.h>

#include <iprt/crypto/shacrypt.h>

#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/test.h>
#include <iprt/stream.h>

#include "VirtualBoxBase.h"
#include "UnattendedImpl.h"
#include "UnattendedScript.h"
#include "VirtualBoxImpl.h"
#include "MachineImpl.h"

using namespace std;


/*********************************************************************************************************************************
*   Unattended Stub Implementation                                                                                               *
*********************************************************************************************************************************/
Unattended::Unattended()
    : mhThreadReconfigureVM(NIL_RTNATIVETHREAD), mfRtcUseUtc(false), mfGuestOs64Bit(false)
    , mpInstaller(NULL), mpTimeZoneInfo(NULL), mfIsDefaultAuxiliaryBasePath(true), mfDoneDetectIsoOS(false)
{
    mStrUser                            = "vboxuser";
    mStrUserPassword                    = "changeme";
    mStrAdminPassword                   = "adminpw";
    mStrFullUserName                    = "VBox & VBox;";
    mStrProductKey                      = "911";
    mStrIsoPath                         = "/iso/path/file.iso";
    mStrAdditionsIsoPath                = "/iso/path/addition.iso";
    mfInstallGuestAdditions             = true;
    mfInstallTestExecService            = true;
    mStrValidationKitIsoPath            = "/iso/path/valkit.iso";
    mStrTimeZone                        = "cet";
    mpTimeZoneInfo                      = NULL;
    mStrLocale                          = "dk_DK";
    mStrLanguage                        = "dk";
    mStrCountry                         = "DK";
    //mPackageSelectionAdjustments      = "minimal";
    mStrHostname                        = "my-extra-long-name.hostname.com";
    mStrAuxiliaryBasePath               = "/aux/path/pfx-";
    mfIsDefaultAuxiliaryBasePath        = false;
    midxImage                           = 42;
    mStrScriptTemplatePath              = "/path/to/script-template.file";
    mStrPostInstallScriptTemplatePath   = "/path/to/post-install-template.file";
    mStrPostInstallCommand              = "/bin/post-install-command arg1 arg2 --amp=& --lt=< --gt=> --dq-word=\"word\" --sq-word='word'";
    mStrExtraInstallKernelParameters    = "extra=kernel parameters quiet amp=& lt=< gt=>";
    mStrProxy                           = "http://proxy.intranet.com:443";

    mfDoneDetectIsoOS                   = true;
    mStrDetectedOSTypeId                = "MyOSTypeId";
    mStrDetectedOSVersion               = "3.4.2";
    mStrDetectedOSFlavor                = "server";
    //mDetectedOSLanguages                = "en_UK"
    mStrDetectedOSHints                 = "nudge nudge wink wink";
    mStrAdditionsInstallPackage         = "Something-none-existing.run";
}

Unattended::~Unattended()
{
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
}

HRESULT Unattended::initUnattended(VirtualBox *aParent)
{
    unconst(mParent)            = aParent;
    return S_OK;
}

HRESULT Unattended::detectIsoOS()
{
    return E_NOTIMPL;
}


HRESULT Unattended::prepare()
{
    return E_NOTIMPL;
}

HRESULT Unattended::constructMedia()
{
    return E_NOTIMPL;
}

HRESULT Unattended::reconfigureVM()
{
    return E_NOTIMPL;
}

HRESULT Unattended::done()
{
    return E_NOTIMPL;
}

HRESULT Unattended::getIsoPath(com::Utf8Str &isoPath)
{
    RT_NOREF(isoPath);
    return E_NOTIMPL;
}

HRESULT Unattended::setIsoPath(const com::Utf8Str &isoPath)
{
    RT_NOREF(isoPath);
    return E_NOTIMPL;
}

HRESULT Unattended::getUser(com::Utf8Str &user)
{
    RT_NOREF(user);
    return E_NOTIMPL;
}


HRESULT Unattended::setUser(const com::Utf8Str &user)
{
    RT_NOREF(user);
    return E_NOTIMPL;
}

HRESULT Unattended::getUserPassword(com::Utf8Str &password)
{
    RT_NOREF(password);
    return E_NOTIMPL;
}

HRESULT Unattended::setUserPassword(const com::Utf8Str &password)
{
    RT_NOREF(password);
    return E_NOTIMPL;
}

HRESULT Unattended::getAdminPassword(com::Utf8Str &password)
{
    RT_NOREF(password);
    return E_NOTIMPL;
}

HRESULT Unattended::setAdminPassword(const com::Utf8Str &password)
{
    RT_NOREF(password);
    return E_NOTIMPL;
}

HRESULT Unattended::getFullUserName(com::Utf8Str &fullUserName)
{
    RT_NOREF(fullUserName);
    return E_NOTIMPL;
}

HRESULT Unattended::setFullUserName(const com::Utf8Str &fullUserName)
{
    RT_NOREF(fullUserName);
    return E_NOTIMPL;
}

HRESULT Unattended::getProductKey(com::Utf8Str &productKey)
{
    RT_NOREF(productKey);
    return E_NOTIMPL;
}

HRESULT Unattended::setProductKey(const com::Utf8Str &productKey)
{
    RT_NOREF(productKey);
    return E_NOTIMPL;
}

HRESULT Unattended::getAdditionsIsoPath(com::Utf8Str &additionsIsoPath)
{
    RT_NOREF(additionsIsoPath);
    return E_NOTIMPL;
}

HRESULT Unattended::setAdditionsIsoPath(const com::Utf8Str &additionsIsoPath)
{
    RT_NOREF(additionsIsoPath);
    return E_NOTIMPL;
}

HRESULT Unattended::getInstallGuestAdditions(BOOL *installGuestAdditions)
{
    RT_NOREF(installGuestAdditions);
    return E_NOTIMPL;
}

HRESULT Unattended::setInstallGuestAdditions(BOOL installGuestAdditions)
{
    RT_NOREF(installGuestAdditions);
    return E_NOTIMPL;
}

HRESULT Unattended::getValidationKitIsoPath(com::Utf8Str &aValidationKitIsoPath)
{
    RT_NOREF(aValidationKitIsoPath);
    return E_NOTIMPL;
}

HRESULT Unattended::setValidationKitIsoPath(const com::Utf8Str &aValidationKitIsoPath)
{
    RT_NOREF(aValidationKitIsoPath);
    return E_NOTIMPL;
}

HRESULT Unattended::getInstallTestExecService(BOOL *aInstallTestExecService)
{
    RT_NOREF(aInstallTestExecService);
    return E_NOTIMPL;
}

HRESULT Unattended::setInstallTestExecService(BOOL aInstallTestExecService)
{
    RT_NOREF(aInstallTestExecService);
    return E_NOTIMPL;
}

HRESULT Unattended::getUserPayloadIsoPath(com::Utf8Str &userPayloadIsoPath)
{
    RT_NOREF(userPayloadIsoPath);
    return E_NOTIMPL;
}

HRESULT Unattended::setUserPayloadIsoPath(const com::Utf8Str &userPayloadIsoPath)
{
    RT_NOREF(userPayloadIsoPath);
    return E_NOTIMPL;
}

HRESULT Unattended::getInstallUserPayload(BOOL *installUserPayload)
{
    RT_NOREF(installUserPayload);
    return E_NOTIMPL;
}

HRESULT Unattended::setInstallUserPayload(BOOL installUserPayload)
{
    RT_NOREF(installUserPayload);
    return E_NOTIMPL;
}

HRESULT Unattended::getTimeZone(com::Utf8Str &aTimeZone)
{
    RT_NOREF(aTimeZone);
    return E_NOTIMPL;
}

HRESULT Unattended::setTimeZone(const com::Utf8Str &aTimezone)
{
    RT_NOREF(aTimezone);
    return E_NOTIMPL;
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
    RT_NOREF(aLocale);
    return E_NOTIMPL;
}

HRESULT Unattended::setLocale(const com::Utf8Str &aLocale)
{
    RT_NOREF(aLocale);
    return E_NOTIMPL;
}

HRESULT Unattended::getLanguage(com::Utf8Str &aLanguage)
{
    RT_NOREF(aLanguage);
    return E_NOTIMPL;
}

HRESULT Unattended::setLanguage(const com::Utf8Str &aLanguage)
{
    RT_NOREF(aLanguage);
    return E_NOTIMPL;
}

HRESULT Unattended::getCountry(com::Utf8Str &aCountry)
{
    RT_NOREF(aCountry);
    return E_NOTIMPL;
}

HRESULT Unattended::setCountry(const com::Utf8Str &aCountry)
{
    RT_NOREF(aCountry);
    return E_NOTIMPL;
}

HRESULT Unattended::getProxy(com::Utf8Str &aProxy)
{
    RT_NOREF(aProxy);
    return E_NOTIMPL;
}

HRESULT Unattended::setProxy(const com::Utf8Str &aProxy)
{
    RT_NOREF(aProxy);
    return E_NOTIMPL;
}

HRESULT Unattended::getPackageSelectionAdjustments(com::Utf8Str &aPackageSelectionAdjustments)
{
    RT_NOREF(aPackageSelectionAdjustments);
    return E_NOTIMPL;
}

HRESULT Unattended::setPackageSelectionAdjustments(const com::Utf8Str &aPackageSelectionAdjustments)
{
    RT_NOREF(aPackageSelectionAdjustments);
    return E_NOTIMPL;
}

HRESULT Unattended::getHostname(com::Utf8Str &aHostname)
{
    RT_NOREF(aHostname);
    return E_NOTIMPL;
}

HRESULT Unattended::setHostname(const com::Utf8Str &aHostname)
{
    RT_NOREF(aHostname);
    return E_NOTIMPL;
}

HRESULT Unattended::getAuxiliaryBasePath(com::Utf8Str &aAuxiliaryBasePath)
{
    RT_NOREF(aAuxiliaryBasePath);
    return E_NOTIMPL;
}

HRESULT Unattended::setAuxiliaryBasePath(const com::Utf8Str &aAuxiliaryBasePath)
{
    RT_NOREF(aAuxiliaryBasePath);
    return E_NOTIMPL;
}

HRESULT Unattended::getImageIndex(ULONG *index)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *index = midxImage;
    return S_OK;
}

HRESULT Unattended::setImageIndex(ULONG index)
{
    RT_NOREF(index);
    return E_NOTIMPL;
}

HRESULT Unattended::getMachine(ComPtr<IMachine> &aMachine)
{
    RT_NOREF(aMachine);
    return E_NOTIMPL;
}

HRESULT Unattended::setMachine(const ComPtr<IMachine> &aMachine)
{
    RT_NOREF(aMachine);
    return E_NOTIMPL;
}

HRESULT Unattended::getScriptTemplatePath(com::Utf8Str &aScriptTemplatePath)
{
    RT_NOREF(aScriptTemplatePath);
    return E_NOTIMPL;
}

HRESULT Unattended::setScriptTemplatePath(const com::Utf8Str &aScriptTemplatePath)
{
    RT_NOREF(aScriptTemplatePath);
    return E_NOTIMPL;

}

HRESULT Unattended::getPostInstallScriptTemplatePath(com::Utf8Str &aPostInstallScriptTemplatePath)
{
    RT_NOREF(aPostInstallScriptTemplatePath);
    return E_NOTIMPL;
}

HRESULT Unattended::setPostInstallScriptTemplatePath(const com::Utf8Str &aPostInstallScriptTemplatePath)
{
    RT_NOREF(aPostInstallScriptTemplatePath);
    return E_NOTIMPL;
}

HRESULT Unattended::getPostInstallCommand(com::Utf8Str &aPostInstallCommand)
{
    RT_NOREF(aPostInstallCommand);
    return E_NOTIMPL;
}

HRESULT Unattended::setPostInstallCommand(const com::Utf8Str &aPostInstallCommand)
{
    RT_NOREF(aPostInstallCommand);
    return E_NOTIMPL;
}

HRESULT Unattended::getExtraInstallKernelParameters(com::Utf8Str &aExtraInstallKernelParameters)
{
    RT_NOREF(aExtraInstallKernelParameters);
    return E_NOTIMPL;
}

HRESULT Unattended::setExtraInstallKernelParameters(const com::Utf8Str &aExtraInstallKernelParameters)
{
    RT_NOREF(aExtraInstallKernelParameters);
    return E_NOTIMPL;
}

HRESULT Unattended::getDetectedOSTypeId(com::Utf8Str &aDetectedOSTypeId)
{
    RT_NOREF(aDetectedOSTypeId);
    return E_NOTIMPL;
}

HRESULT Unattended::getDetectedOSVersion(com::Utf8Str &aDetectedOSVersion)
{
    RT_NOREF(aDetectedOSVersion);
    return E_NOTIMPL;
}

HRESULT Unattended::getDetectedOSFlavor(com::Utf8Str &aDetectedOSFlavor)
{
    RT_NOREF(aDetectedOSFlavor);
    return E_NOTIMPL;
}

HRESULT Unattended::getDetectedOSLanguages(com::Utf8Str &aDetectedOSLanguages)
{
    RT_NOREF(aDetectedOSLanguages);
    return E_NOTIMPL;
}

HRESULT Unattended::getDetectedOSHints(com::Utf8Str &aDetectedOSHints)
{
    RT_NOREF(aDetectedOSHints);
    return E_NOTIMPL;
}

HRESULT Unattended::getDetectedImageNames(std::vector<com::Utf8Str> &aDetectedImageNames)
{
    RT_NOREF(aDetectedImageNames);
    return E_NOTIMPL;
}

HRESULT Unattended::getDetectedImageIndices(std::vector<ULONG> &aDetectedImageIndices)
{
    RT_NOREF(aDetectedImageIndices);
    return E_NOTIMPL;
}

HRESULT Unattended::getIsUnattendedInstallSupported(BOOL *aIsUnattendedInstallSupported)
{
    RT_NOREF(aIsUnattendedInstallSupported);
    return E_NOTIMPL;
}

HRESULT Unattended::getAvoidUpdatesOverNetwork(BOOL *aAvoidUpdatesOverNetwork)
{
    RT_NOREF(aAvoidUpdatesOverNetwork);
    return E_NOTIMPL;
}

HRESULT Unattended::setAvoidUpdatesOverNetwork(BOOL aAvoidUpdatesOverNetwork)
{
    RT_NOREF(aAvoidUpdatesOverNetwork);
    return E_NOTIMPL;
}


/*
 * Getters that the installer and script classes can use.
 */
Utf8Str const &Unattended::i_getIsoPath() const
{
    return mStrIsoPath;
}

Utf8Str const &Unattended::i_getUser() const
{
    return mStrUser;
}

Utf8Str const &Unattended::i_getUserPassword() const
{
    return mStrUserPassword;
}

Utf8Str const &Unattended::i_getAdminPassword() const
{
    /* If no Administrator / 'root' password is being set, the user password will be used instead.
     * Also see API documentation. */
    return mStrAdminPassword.isEmpty() ? mStrUserPassword : mStrAdminPassword;
}

Utf8Str const &Unattended::i_getFullUserName() const
{
    return mStrFullUserName.isNotEmpty() ? mStrFullUserName : mStrUser;
}

Utf8Str const &Unattended::i_getProductKey() const
{
    return mStrProductKey;
}

Utf8Str const &Unattended::i_getProxy() const
{
    return mStrProxy;
}

Utf8Str const &Unattended::i_getAdditionsIsoPath() const
{
    return mStrAdditionsIsoPath;
}

bool           Unattended::i_getInstallGuestAdditions() const
{
    return mfInstallGuestAdditions;
}

Utf8Str const &Unattended::i_getValidationKitIsoPath() const
{
    return mStrValidationKitIsoPath;
}

bool Unattended::i_getInstallTestExecService() const
{
    return mfInstallTestExecService;
}

Utf8Str const &Unattended::i_getTimeZone() const
{
    return mStrTimeZone;
}

PCRTTIMEZONEINFO Unattended::i_getTimeZoneInfo() const
{
    return mpTimeZoneInfo;
}

Utf8Str const &Unattended::i_getLocale() const
{
    return mStrLocale;
}

Utf8Str const &Unattended::i_getLanguage() const
{
    return mStrLanguage;
}

Utf8Str const &Unattended::i_getCountry() const
{
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
    return mStrHostname;
}

Utf8Str const &Unattended::i_getAuxiliaryBasePath() const
{
    return mStrAuxiliaryBasePath;
}

ULONG Unattended::i_getImageIndex() const
{
    return midxImage;
}

Utf8Str const &Unattended::i_getScriptTemplatePath() const
{
    return mStrScriptTemplatePath;
}

Utf8Str const &Unattended::i_getPostInstallScriptTemplatePath() const
{
    return mStrPostInstallScriptTemplatePath;
}

Utf8Str const &Unattended::i_getPostInstallCommand() const
{
    return mStrPostInstallCommand;
}

Utf8Str const &Unattended::i_getAuxiliaryInstallDir() const
{
    static Utf8Str s_strAuxInstallDir("/aux/install/dir");
    return s_strAuxInstallDir;
}

Utf8Str const &Unattended::i_getExtraInstallKernelParameters() const
{
    return mStrExtraInstallKernelParameters;
}

Utf8Str const &Unattended::i_getAdditionsInstallPackage() const
{
    return mStrAdditionsInstallPackage;
}

bool Unattended::i_isRtcUsingUtc() const
{
    return mfRtcUseUtc;
}

bool Unattended::i_isGuestOs64Bit() const
{
    return mfGuestOs64Bit;
}

bool Unattended::i_isFirmwareEFI() const
{
    return menmFirmwareType != FirmwareType_BIOS;
}

Utf8Str const &Unattended::i_getDetectedOSVersion()
{
    return mStrDetectedOSVersion;
}

bool Unattended::i_getAvoidUpdatesOverNetwork() const
{
    return mfAvoidUpdatesOverNetwork;
}


/*********************************************************************************************************************************
*   The Testcase                                                                                                                 *
*********************************************************************************************************************************/

static bool loadFileAsString(const char *pszFilename, Utf8Str &rstrContent)
{
    rstrContent.setNull();

    char szPath[RTPATH_MAX];
    RTTESTI_CHECK_RC_RET(RTPathExecDir(szPath, sizeof(szPath)), VINF_SUCCESS, false);
    RTTESTI_CHECK_RC_RET(RTPathAppend(szPath, sizeof(szPath), pszFilename), VINF_SUCCESS, false);

    RTFILE hFile;
    RTTESTI_CHECK_RC_RET(RTFileOpen(&hFile, szPath, RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_DENY_WRITE), VINF_SUCCESS, false);

    uint64_t cbFile = 0;
    RTTESTI_CHECK_RC_RET(RTFileQuerySize(hFile, &cbFile), VINF_SUCCESS, false);

    rstrContent.reserve((size_t)cbFile + 1);
    RTTESTI_CHECK_RC_RET(RTFileRead(hFile, rstrContent.mutableRaw(), (size_t)cbFile, NULL), VINF_SUCCESS, false);
    rstrContent.mutableRaw()[cbFile] = '\0';
    rstrContent.jolt();

    RTTESTI_CHECK_RC_RET(RTFileClose(hFile), VINF_SUCCESS, false);

    return true;
}

static void doTest1()
{
    RTTestISub("tstUnattendedScript-1.template");

    /* Create the parent class instance: */
    ComObjPtr<Unattended> ptrParent;
    HRESULT hrc = ptrParent.createObject();
    RTTESTI_CHECK_MSG_RETV(SUCCEEDED(hrc), ("hrc=%Rhrc\n", hrc));

    /* Instantiate the script editor. */
    UnattendedScriptTemplate Tmpl(ptrParent, "template.ext", "file.ext");
#define CHECK_HRESULT(a_Expr) do { \
        HRESULT hrcThis = a_Expr; \
        if (SUCCEEDED(hrcThis)) break; \
        RTTestIFailed("line %d: %s -> %Rhrc", __LINE__, #a_Expr, hrcThis);  \
        GlueHandleComError(ptrParent, NULL, hrcThis, NULL, __LINE__); \
    } while (0)

    /* Load the exercise script. */
    char szPath[RTPATH_MAX];
    RTTESTI_CHECK_RC_RETV(RTPathExecDir(szPath, sizeof(szPath)), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTPathAppend(szPath, sizeof(szPath), "tstUnattendedScript-1.template"), VINF_SUCCESS);
    CHECK_HRESULT(Tmpl.read(szPath));

    /* Save the template to string. */
    Utf8Str strActual;
    CHECK_HRESULT(Tmpl.saveToString(strActual));

    /* Load the expected result. */
    Utf8Str strExpected;
    RTTESTI_CHECK_RETV(loadFileAsString("tstUnattendedScript-1.expected", strExpected));

    /* Compare the two. */
    if (strExpected != strActual)
    {
        RTTestIFailed("Output does not match tstUnattendedScript-1.expected!");
        RTTestIFailureDetails("------ BEGIN OUTPUT ------\n");
        RTStrmWrite(g_pStdErr, strActual.c_str(), strActual.length());
        RTTestIFailureDetails("------- END OUTPUT -------\n");

        RTCList<RTCString, RTCString *> const lstActual = strActual.split("\n");
        RTCList<RTCString, RTCString *> const lstExpected = strExpected.split("\n");
        size_t const cLines = RT_MIN(lstActual.size(), lstExpected.size());
        for (size_t i = 0; i < cLines; i++)
            if (lstActual[i] != lstExpected[i])
            {
                RTTestIFailureDetails("First difference on line %u:\n%s\nexpected:\n%s\n",
                                      i + 1, lstActual[i].c_str(), lstExpected[i].c_str());
                break;
            }
    }
}

int main()
{
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstUnattendedScript", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

#ifdef RT_OS_WINDOWS
    /*ATL::CComModule *g_pAtlComModule = */ new(ATL::CComModule);
#endif

    doTest1();

    return RTTestSummaryAndDestroy(hTest);
}
