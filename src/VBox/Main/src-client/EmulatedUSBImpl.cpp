/* $Id: EmulatedUSBImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Emulated USB manager implementation.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN_EMULATEDUSB
#include "LoggingNew.h"

#include "EmulatedUSBImpl.h"
#include "ConsoleImpl.h"

#include <VBox/vmm/pdmusb.h>
#include <VBox/vmm/vmmr3vtable.h>


/*
 * Emulated USB webcam device instance.
 */
typedef std::map <Utf8Str, Utf8Str> EUSBSettingsMap;

typedef enum EUSBDEVICESTATUS
{
    EUSBDEVICE_CREATED,
    EUSBDEVICE_ATTACHING,
    EUSBDEVICE_ATTACHED
} EUSBDEVICESTATUS;

class EUSBWEBCAM /* : public EUSBDEVICE */
{
private:
    int32_t volatile mcRefs;

    EmulatedUSB *mpEmulatedUSB;

    RTUUID mUuid;
    char mszUuid[RTUUID_STR_LENGTH];

    Utf8Str mPath;
    Utf8Str mSettings;

    EUSBSettingsMap mDevSettings;
    EUSBSettingsMap mDrvSettings;

    void *mpvObject;

    static DECLCALLBACK(int) emulatedWebcamAttach(PUVM pUVM, PCVMMR3VTABLE pVMM, EUSBWEBCAM *pThis, const char *pszDriver);
    static DECLCALLBACK(int) emulatedWebcamDetach(PUVM pUVM, PCVMMR3VTABLE pVMM, EUSBWEBCAM *pThis);

    HRESULT settingsParse(void);

    ~EUSBWEBCAM()
    {
    }

public:
    EUSBWEBCAM()
        :
        mcRefs(1),
        mpEmulatedUSB(NULL),
        mpvObject(NULL),
        enmStatus(EUSBDEVICE_CREATED)
    {
        RT_ZERO(mUuid);
        RT_ZERO(mszUuid);
    }

    int32_t AddRef(void)
    {
        return ASMAtomicIncS32(&mcRefs);
    }

    void Release(void)
    {
        int32_t c = ASMAtomicDecS32(&mcRefs);
        if (c == 0)
        {
            delete this;
        }
    }

    HRESULT Initialize(Console *pConsole,
                       EmulatedUSB *pEmulatedUSB,
                       const com::Utf8Str *aPath,
                       const com::Utf8Str *aSettings,
                       void *pvObject);
    HRESULT Attach(Console *pConsole, PUVM pUVM, PCVMMR3VTABLE pVMM, const char *pszDriver);
    HRESULT Detach(Console *pConsole, PUVM pUVM, PCVMMR3VTABLE pVMM);

    bool HasId(const char *pszId) { return RTStrCmp(pszId, mszUuid) == 0;}

    void *getObjectPtr() { return mpvObject; }

    EUSBDEVICESTATUS enmStatus;
};


static int emulatedWebcamInsertSettings(PCFGMNODE pConfig, PCVMMR3VTABLE pVMM, EUSBSettingsMap *pSettings)
{
    for (EUSBSettingsMap::const_iterator it = pSettings->begin(); it != pSettings->end(); ++it)
    {
        /* Convert some well known settings for backward compatibility. */
        int vrc;
        if (   RTStrCmp(it->first.c_str(), "MaxPayloadTransferSize") == 0
            || RTStrCmp(it->first.c_str(), "MaxFramerate") == 0)
        {
            uint32_t u32 = 0;
            vrc = RTStrToUInt32Full(it->second.c_str(), 10, &u32);
            if (vrc == VINF_SUCCESS)
                vrc = pVMM->pfnCFGMR3InsertInteger(pConfig, it->first.c_str(), u32);
            else if (RT_SUCCESS(vrc)) /* VWRN_* */
                vrc = VERR_INVALID_PARAMETER;
        }
        else
            vrc = pVMM->pfnCFGMR3InsertString(pConfig, it->first.c_str(), it->second.c_str());
        if (RT_FAILURE(vrc))
            return vrc;
    }

    return VINF_SUCCESS;
}

/*static*/ DECLCALLBACK(int)
EUSBWEBCAM::emulatedWebcamAttach(PUVM pUVM, PCVMMR3VTABLE pVMM, EUSBWEBCAM *pThis, const char *pszDriver)
{
    PCFGMNODE pInstance = pVMM->pfnCFGMR3CreateTree(pUVM);
    PCFGMNODE pConfig;
    int vrc = pVMM->pfnCFGMR3InsertNode(pInstance,   "Config", &pConfig);
    AssertRCReturn(vrc, vrc);
    vrc = emulatedWebcamInsertSettings(pConfig, pVMM, &pThis->mDevSettings);
    AssertRCReturn(vrc, vrc);

    PCFGMNODE pEUSB;
    vrc = pVMM->pfnCFGMR3InsertNode(pConfig,       "EmulatedUSB", &pEUSB);
    AssertRCReturn(vrc, vrc);
    vrc = pVMM->pfnCFGMR3InsertString(pEUSB,         "Id", pThis->mszUuid);
    AssertRCReturn(vrc, vrc);

    PCFGMNODE pLunL0;
    vrc = pVMM->pfnCFGMR3InsertNode(pInstance,   "LUN#0", &pLunL0);
    AssertRCReturn(vrc, vrc);
    vrc = pVMM->pfnCFGMR3InsertString(pLunL0,      "Driver", pszDriver);
    AssertRCReturn(vrc, vrc);
    vrc = pVMM->pfnCFGMR3InsertNode(pLunL0,        "Config", &pConfig);
    AssertRCReturn(vrc, vrc);
    vrc = pVMM->pfnCFGMR3InsertString(pConfig,       "DevicePath", pThis->mPath.c_str());
    AssertRCReturn(vrc, vrc);
    vrc = pVMM->pfnCFGMR3InsertString(pConfig,       "Id", pThis->mszUuid);
    AssertRCReturn(vrc, vrc);
    vrc = emulatedWebcamInsertSettings(pConfig, pVMM, &pThis->mDrvSettings);
    AssertRCReturn(vrc, vrc);

    /* pInstance will be used by PDM and deallocated on error. */
    vrc = pVMM->pfnPDMR3UsbCreateEmulatedDevice(pUVM, "Webcam", pInstance, &pThis->mUuid, NULL);
    LogRelFlowFunc(("PDMR3UsbCreateEmulatedDevice %Rrc\n", vrc));
    return vrc;
}

/*static*/ DECLCALLBACK(int)
EUSBWEBCAM::emulatedWebcamDetach(PUVM pUVM, PCVMMR3VTABLE pVMM, EUSBWEBCAM *pThis)
{
    return pVMM->pfnPDMR3UsbDetachDevice(pUVM, &pThis->mUuid);
}

HRESULT EUSBWEBCAM::Initialize(Console *pConsole,
                               EmulatedUSB *pEmulatedUSB,
                               const com::Utf8Str *aPath,
                               const com::Utf8Str *aSettings,
                               void *pvObject)
{
    HRESULT hrc = S_OK;

    int vrc = RTUuidCreate(&mUuid);
    AssertRCReturn(vrc, pConsole->setError(vrc, EmulatedUSB::tr("Init emulated USB webcam (RTUuidCreate -> %Rrc)"), vrc));

    RTStrPrintf(mszUuid, sizeof(mszUuid), "%RTuuid", &mUuid);
    hrc = mPath.assignEx(*aPath);
    if (SUCCEEDED(hrc))
    {
        hrc = mSettings.assignEx(*aSettings);
        if (SUCCEEDED(hrc))
        {
            hrc = settingsParse();
            if (SUCCEEDED(hrc))
            {
                mpEmulatedUSB = pEmulatedUSB;
                mpvObject = pvObject;
            }
        }
    }

    return hrc;
}

HRESULT EUSBWEBCAM::settingsParse(void)
{
    HRESULT hr = S_OK;

    /* Parse mSettings string:
     * "[dev:|drv:]Name1=Value1;[dev:|drv:]Name2=Value2"
     */
    char *pszSrc = mSettings.mutableRaw();

    if (pszSrc)
    {
        while (*pszSrc)
        {
            /* Does the setting belong to device of driver. Default is both. */
            bool fDev = true;
            bool fDrv = true;
            if (RTStrNICmp(pszSrc, RT_STR_TUPLE("drv:")) == 0)
            {
                pszSrc += sizeof("drv:")-1;
                fDev = false;
            }
            else if (RTStrNICmp(pszSrc, RT_STR_TUPLE("dev:")) == 0)
            {
                pszSrc += sizeof("dev:")-1;
                fDrv = false;
            }

            char *pszEq = strchr(pszSrc, '=');
            if (!pszEq)
            {
                hr = E_INVALIDARG;
                break;
            }

            char *pszEnd = strchr(pszEq, ';');
            if (!pszEnd)
                pszEnd = pszEq + strlen(pszEq);

            *pszEq = 0;
            char chEnd = *pszEnd;
            *pszEnd = 0;

            /* Empty strings not allowed. */
            if (*pszSrc != 0 && pszEq[1] != 0)
            {
                if (fDev)
                    mDevSettings[pszSrc] = &pszEq[1];
                if (fDrv)
                    mDrvSettings[pszSrc] = &pszEq[1];
            }

            *pszEq = '=';
            *pszEnd = chEnd;

            pszSrc = pszEnd;
            if (*pszSrc == ';')
                pszSrc++;
        }

        if (SUCCEEDED(hr))
        {
            EUSBSettingsMap::const_iterator it;
            for (it = mDevSettings.begin(); it != mDevSettings.end(); ++it)
                LogRelFlowFunc(("[dev:%s] = [%s]\n", it->first.c_str(), it->second.c_str()));
            for (it = mDrvSettings.begin(); it != mDrvSettings.end(); ++it)
                LogRelFlowFunc(("[drv:%s] = [%s]\n", it->first.c_str(), it->second.c_str()));
        }
    }

    return hr;
}

HRESULT EUSBWEBCAM::Attach(Console *pConsole, PUVM pUVM, PCVMMR3VTABLE pVMM, const char *pszDriver)
{
    int vrc = pVMM->pfnVMR3ReqCallWaitU(pUVM, 0 /* idDstCpu (saved state, see #6232) */, (PFNRT)emulatedWebcamAttach, 4,
                                        pUVM, pVMM, this, pszDriver);
    if (RT_SUCCESS(vrc))
        return S_OK;
    LogFlowThisFunc(("%Rrc\n", vrc));
    return pConsole->setErrorBoth(VBOX_E_VM_ERROR, vrc, EmulatedUSB::tr("Attach emulated USB webcam (%Rrc)"), vrc);
}

HRESULT EUSBWEBCAM::Detach(Console *pConsole, PUVM pUVM, PCVMMR3VTABLE pVMM)
{
    int vrc = pVMM->pfnVMR3ReqCallWaitU(pUVM, 0 /* idDstCpu (saved state, see #6232) */, (PFNRT)emulatedWebcamDetach, 3,
                                        pUVM, pVMM, this);
    if (RT_SUCCESS(vrc))
        return S_OK;
    LogFlowThisFunc(("%Rrc\n", vrc));
    return pConsole->setErrorBoth(VBOX_E_VM_ERROR, vrc, EmulatedUSB::tr("Detach emulated USB webcam (%Rrc)"), vrc);
}


/*
 * EmulatedUSB implementation.
 */
DEFINE_EMPTY_CTOR_DTOR(EmulatedUSB)

HRESULT EmulatedUSB::FinalConstruct()
{
    return BaseFinalConstruct();
}

void EmulatedUSB::FinalRelease()
{
    uninit();

    BaseFinalRelease();
}

/*
 * Initializes the instance.
 *
 * @param pConsole   The owner.
 */
HRESULT EmulatedUSB::init(ComObjPtr<Console> pConsole)
{
    LogFlowThisFunc(("\n"));

    ComAssertRet(!pConsole.isNull(), E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m.pConsole = pConsole;

    mEmUsbIf.pvUser = this;
    mEmUsbIf.pfnQueryEmulatedUsbDataById = EmulatedUSB::i_QueryEmulatedUsbDataById;

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/*
 * Uninitializes the instance.
 * Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void EmulatedUSB::uninit()
{
    LogFlowThisFunc(("\n"));

    m.pConsole.setNull();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    for (WebcamsMap::iterator it = m.webcams.begin(); it != m.webcams.end(); ++it)
    {
        EUSBWEBCAM *p = it->second;
        if (p)
        {
            it->second = NULL;
            p->Release();
        }
    }
    m.webcams.clear();
    alock.release();

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;
}

HRESULT EmulatedUSB::getWebcams(std::vector<com::Utf8Str> &aWebcams)
{
    HRESULT hrc = S_OK;

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    try
    {
        aWebcams.resize(m.webcams.size());
    }
    catch (std::bad_alloc &)
    {
        hrc = E_OUTOFMEMORY;
    }
    catch (...)
    {
        hrc = E_FAIL;
    }

    if (SUCCEEDED(hrc))
    {
        size_t i;
        WebcamsMap::const_iterator it;
        for (i = 0, it = m.webcams.begin(); it != m.webcams.end(); ++it)
            aWebcams[i++] = it->first;
    }

    return hrc;
}

PEMULATEDUSBIF EmulatedUSB::i_getEmulatedUsbIf()
{
    return &mEmUsbIf;
}

static const Utf8Str s_pathDefault(".0");

HRESULT EmulatedUSB::webcamAttach(const com::Utf8Str &aPath,
                                  const com::Utf8Str &aSettings)
{
    return i_webcamAttachInternal(aPath, aSettings, "HostWebcam", NULL);
}

HRESULT EmulatedUSB::i_webcamAttachInternal(const com::Utf8Str &aPath,
                                            const com::Utf8Str &aSettings,
                                            const char *pszDriver,
                                            void *pvObject)
{
    HRESULT hrc = S_OK;

    const Utf8Str &path = aPath.isEmpty() || aPath == "."? s_pathDefault: aPath;

    Console::SafeVMPtr ptrVM(m.pConsole);
    if (ptrVM.isOk())
    {
        EUSBWEBCAM *p = new EUSBWEBCAM();
        if (p)
        {
            hrc = p->Initialize(m.pConsole, this, &path, &aSettings, pvObject);
            if (SUCCEEDED(hrc))
            {
                AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
                WebcamsMap::const_iterator it = m.webcams.find(path);
                if (it == m.webcams.end())
                {
                    p->AddRef();
                    try
                    {
                        m.webcams[path] = p;
                    }
                    catch (std::bad_alloc &)
                    {
                        hrc = E_OUTOFMEMORY;
                    }
                    catch (...)
                    {
                        hrc = E_FAIL;
                    }
                    p->enmStatus = EUSBDEVICE_ATTACHING;
                }
                else
                {
                    hrc = E_FAIL;
                }
            }

            if (SUCCEEDED(hrc))
                hrc = p->Attach(m.pConsole, ptrVM.rawUVM(), ptrVM.vtable(), pszDriver);

            AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
            if (SUCCEEDED(hrc))
                p->enmStatus = EUSBDEVICE_ATTACHED;
            else if (p->enmStatus != EUSBDEVICE_CREATED)
                m.webcams.erase(path);
            alock.release();

            p->Release();
        }
        else
        {
            hrc = E_OUTOFMEMORY;
        }
    }
    else
    {
        hrc = VBOX_E_INVALID_VM_STATE;
    }

    return hrc;
}

HRESULT EmulatedUSB::webcamDetach(const com::Utf8Str &aPath)
{
    return i_webcamDetachInternal(aPath);
}

HRESULT EmulatedUSB::i_webcamDetachInternal(const com::Utf8Str &aPath)
{
    HRESULT hrc = S_OK;

    const Utf8Str &path = aPath.isEmpty() || aPath == "."? s_pathDefault: aPath;

    Console::SafeVMPtr ptrVM(m.pConsole);
    if (ptrVM.isOk())
    {
        EUSBWEBCAM *p = NULL;

        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        WebcamsMap::iterator it = m.webcams.find(path);
        if (it != m.webcams.end())
        {
            if (it->second->enmStatus == EUSBDEVICE_ATTACHED)
            {
                p = it->second;
                m.webcams.erase(it);
            }
        }
        alock.release();

        if (p)
        {
            hrc = p->Detach(m.pConsole, ptrVM.rawUVM(), ptrVM.vtable());
            p->Release();
        }
        else
        {
            hrc = E_INVALIDARG;
        }
    }
    else
    {
        hrc = VBOX_E_INVALID_VM_STATE;
    }

    return hrc;
}

/*static*/ DECLCALLBACK(int)
EmulatedUSB::eusbCallbackEMT(EmulatedUSB *pThis, char *pszId, uint32_t iEvent, void *pvData, uint32_t cbData)
{
    LogRelFlowFunc(("id %s event %d, data %p %d\n", pszId, iEvent, pvData, cbData));

    NOREF(cbData);

    int vrc = VINF_SUCCESS;
    if (iEvent == 0)
    {
        com::Utf8Str path;
        HRESULT hrc = pThis->webcamPathFromId(&path, pszId);
        if (SUCCEEDED(hrc))
        {
            hrc = pThis->webcamDetach(path);
            if (FAILED(hrc))
            {
                vrc = VERR_INVALID_STATE;
            }
        }
        else
        {
            vrc = VERR_NOT_FOUND;
        }
    }
    else
    {
        vrc = VERR_INVALID_PARAMETER;
    }

    RTMemFree(pszId);
    RTMemFree(pvData);

    LogRelFlowFunc(("vrc %Rrc\n", vrc));
    return vrc;
}

/* static */ DECLCALLBACK(int)
EmulatedUSB::i_eusbCallback(void *pv, const char *pszId, uint32_t iEvent, const void *pvData, uint32_t cbData)
{
    /* Make a copy of parameters, forward to EMT and leave the callback to not hold any lock in the device. */
    int vrc = VINF_SUCCESS;
    void *pvDataCopy = NULL;
    if (cbData > 0)
    {
       pvDataCopy = RTMemDup(pvData, cbData);
       if (!pvDataCopy)
           vrc = VERR_NO_MEMORY;
    }
    if (RT_SUCCESS(vrc))
    {
        void *pvIdCopy = RTMemDup(pszId, strlen(pszId) + 1);
        if (pvIdCopy)
        {
            if (RT_SUCCESS(vrc))
            {
                EmulatedUSB *pThis = (EmulatedUSB *)pv;
                Console::SafeVMPtr ptrVM(pThis->m.pConsole);
                if (ptrVM.isOk())
                {
                    /* No wait. */
                    vrc = ptrVM.vtable()->pfnVMR3ReqCallNoWaitU(ptrVM.rawUVM(), 0 /* idDstCpu */,
                                                                (PFNRT)EmulatedUSB::eusbCallbackEMT, 5,
                                                                pThis, pvIdCopy, iEvent, pvDataCopy, cbData);
                    if (RT_SUCCESS(vrc))
                        return vrc;
                }
                else
                    vrc = VERR_INVALID_STATE;
            }
            RTMemFree(pvIdCopy);
        }
        else
            vrc = VERR_NO_MEMORY;
        RTMemFree(pvDataCopy);
    }
    return vrc;
}

/*static*/
DECLCALLBACK(int) EmulatedUSB::i_QueryEmulatedUsbDataById(void *pvUser, const char *pszId, void **ppvEmUsbCb, void **ppvEmUsbCbData, void **ppvObject)
{
    EmulatedUSB *pEmUsb = (EmulatedUSB *)pvUser;

    AutoReadLock alock(pEmUsb COMMA_LOCKVAL_SRC_POS);
    WebcamsMap::const_iterator it;
    for (it = pEmUsb->m.webcams.begin(); it != pEmUsb->m.webcams.end(); ++it)
    {
        EUSBWEBCAM *p = it->second;
        if (p->HasId(pszId))
        {
            if (ppvEmUsbCb)
                *ppvEmUsbCb = (void *)EmulatedUSB::i_eusbCallback;
            if (ppvEmUsbCbData)
                *ppvEmUsbCbData = pEmUsb;
            if (ppvObject)
                *ppvObject = p->getObjectPtr();

            return VINF_SUCCESS;
        }
    }

    return VERR_NOT_FOUND;
}

HRESULT EmulatedUSB::webcamPathFromId(com::Utf8Str *pPath, const char *pszId)
{
    HRESULT hrc = S_OK;

    Console::SafeVMPtr ptrVM(m.pConsole);
    if (ptrVM.isOk())
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        WebcamsMap::const_iterator it;
        for (it = m.webcams.begin(); it != m.webcams.end(); ++it)
        {
            EUSBWEBCAM *p = it->second;
            if (p->HasId(pszId))
            {
                *pPath = it->first;
                break;
            }
        }

        if (it == m.webcams.end())
        {
            hrc = E_FAIL;
        }
        alock.release();
    }
    else
    {
        hrc = VBOX_E_INVALID_VM_STATE;
    }

    return hrc;
}

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
