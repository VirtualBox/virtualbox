/* $Id: VirtualBoxTranslator.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Translator class.
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
#define LOG_GROUP LOG_GROUP_MAIN_VIRTUALBOXCLIENT /** @todo add separate logging group! */
#include "LoggingNew.h"

#include <iprt/asm.h>
#include <iprt/ctype.h>
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/locale.h>
#include <iprt/once.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/strcache.h>

#ifdef RT_OS_DARWIN
#include <CoreFoundation/CFLocale.h>
#include <CoreFoundation/CFString.h>
#endif

#include "Global.h"
#include "VirtualBoxBase.h"
#include "QMTranslator.h"
#include "VirtualBoxTranslator.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define TRANSLATOR_CACHE_SIZE 32


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Init once for the critical section. */
static RTONCE               g_Once = RTONCE_INITIALIZER;
RTCRITSECTRW                VirtualBoxTranslator::s_instanceRwLock;
VirtualBoxTranslator       *VirtualBoxTranslator::s_pInstance = NULL;
/** TLS index that points to the translated text. */
static RTTLS                g_idxTlsTr = NIL_RTTLS;
/** TLS index that points to the original text. */
static RTTLS                g_idxTlsSrc = NIL_RTTLS;


/**
 * @callback_method_impl{FNRTONCE}
 */
static DECLCALLBACK(int32_t) initLock(void *pvUser)
{
    RT_NOREF(pvUser);
    return VirtualBoxTranslator::initCritSect();
}


/**
 * Obtains the user language code in ll_CC form depending on platform
 *
 * @returns VBox status code
 * @param pszName   The buffer for storing user language code
 * @param cbName    Size of the pszName buffer
 */
static int vboxGetDefaultUserLanguage(char *pszName, size_t cbName)
{
    AssertReturn(pszName, VERR_INVALID_PARAMETER);
    AssertReturn(cbName >= 6, VERR_INVALID_PARAMETER); /* 5 chars for language + null termination */

#ifdef RT_OS_WINDOWS
    if (   GetLocaleInfoA(GetUserDefaultLCID(), LOCALE_SISO639LANGNAME, pszName, (int)cbName) == 3
        && GetLocaleInfoA(GetUserDefaultLCID(), LOCALE_SISO3166CTRYNAME, &pszName[3], (int)cbName - 4) == 3)
    {
        pszName[2] = '_';
        Assert(RTLOCALE_IS_LANGUAGE2_UNDERSCORE_COUNTRY2(pszName));
        return VINF_SUCCESS;
    }
#elif RT_OS_DARWIN
    CFLocaleRef locale = CFLocaleCopyCurrent();
    CFTypeRef localeId = CFLocaleGetValue (locale, kCFLocaleIdentifier);
    char szLocale[256] = { 0 };
    if (CFGetTypeID(localeId) == CFStringGetTypeID())
        CFStringGetCString((CFStringRef)localeId, szLocale, sizeof(szLocale), kCFStringEncodingUTF8);
    /* Some cleanup */
    CFRelease(locale);
    if (szLocale[0] == '\0')
    {
        pszName[0] = 'C';
        pszName[1] = 0;
        return VINF_SUCCESS;
    }
    else
        return RTStrCopy(pszName, cbName, szLocale);

#elif defined(RT_OS_LINUX) || defined(RT_OS_FREEBSD) || defined(RT_OS_NETBSD) || defined(RT_OS_OPENBSD) || defined(RT_OS_SOLARIS)
    const char *pszValue = RTEnvGet("LC_ALL");
    if (pszValue == 0)
        pszValue = RTEnvGet("LC_MESSAGES");
    if (pszValue == 0)
        pszValue = RTEnvGet("LANG");
    if (pszValue != 0)
    {
        /* ignore codepage part, i.e. ignore ".UTF-8" in "ru_RU.UTF-8" */
        const char *pszDot = strchr(pszValue, '.');
        size_t cbValue = strlen(pszValue);
        if (pszDot != NULL)
          cbValue = RT_MIN(cbValue, (size_t)(pszDot - pszValue));

        if (   (   cbValue == 2
                && RT_C_IS_LOWER(pszValue[0])
                && RT_C_IS_LOWER(pszValue[1]))
            || (   cbValue == 5
                && RTLOCALE_IS_LANGUAGE2_UNDERSCORE_COUNTRY2(pszValue)))
            return RTStrCopyEx(pszName, cbName, pszValue, cbValue);
    }
#endif
    return RTLocaleQueryNormalizedBaseLocaleName(pszName, cbName);
}

VirtualBoxTranslator::VirtualBoxTranslator()
    : util::RWLockHandle(util::LOCKCLASS_TRANSLATOR, "VirtualBoxTranslator")
    , m_cInstanceRefs(0)
    , m_pDefaultComponent(NULL)
    , m_strLanguage("C")
    , m_hStrCache(NIL_RTSTRCACHE)
{
    g_idxTlsTr  = RTTlsAlloc();
    g_idxTlsSrc = RTTlsAlloc();
    int vrc = RTStrCacheCreate(&m_hStrCache, "API Translation");
    m_rcCache = vrc;
    if (RT_FAILURE(vrc))
        m_hStrCache = NIL_RTSTRCACHE; /* (loadLanguage will fail) */
    LogFlowFunc(("m_rcCache=%Rrc g_idxTlsTr=%#x g_idxTlsSrc=%#x\n", m_rcCache, g_idxTlsTr, g_idxTlsSrc));
}


VirtualBoxTranslator::~VirtualBoxTranslator()
{
    LogFlowFunc(("enter\n"));

    /* Write-lock the object as we could be racing language change
       notifications processing during XPCOM shutdown. (risky?) */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    RTTlsFree(g_idxTlsTr);
    g_idxTlsTr = NIL_RTTLS;
    RTTlsFree(g_idxTlsSrc);
    g_idxTlsSrc = NIL_RTTLS;

    m_pDefaultComponent = NULL;

    for (TranslatorList::iterator it = m_lTranslators.begin();
         it != m_lTranslators.end();
         ++it)
    {
        if (it->pTranslator != NULL)
            delete it->pTranslator;
        it->pTranslator = NULL;
    }
    if (m_hStrCache != NIL_RTSTRCACHE)
    {
        RTStrCacheDestroy(m_hStrCache);
        m_hStrCache = NIL_RTSTRCACHE;
        m_rcCache = VERR_WRONG_ORDER;
    }
    LogFlowFunc(("returns\n"));
}


/**
 * Get or create a translator instance (singelton), referenced.
 *
 * The main reference is held by the main VBox singelton objects (VirtualBox,
 * VirtualBoxClient) tying it's lifetime to theirs.
 */
/* static */
VirtualBoxTranslator *VirtualBoxTranslator::instance()
{
    int vrc = RTOnce(&g_Once, initLock, NULL);
    if (RT_SUCCESS(vrc))
    {
        RTCritSectRwEnterShared(&s_instanceRwLock);
        VirtualBoxTranslator *pInstance = s_pInstance;
        if (RT_LIKELY(pInstance != NULL))
        {
            uint32_t cRefs = ASMAtomicIncU32(&pInstance->m_cInstanceRefs);
            Assert(cRefs > 1); Assert(cRefs < _8K); RT_NOREF(cRefs);
            RTCritSectRwLeaveShared(&s_instanceRwLock);
            return pInstance;
        }

        /* Maybe create the instance: */
        RTCritSectRwLeaveShared(&s_instanceRwLock);
        RTCritSectRwEnterExcl(&s_instanceRwLock);
        pInstance = s_pInstance;
        if (pInstance == NULL)
            s_pInstance = pInstance = new VirtualBoxTranslator();
        ASMAtomicIncU32(&pInstance->m_cInstanceRefs);
        RTCritSectRwLeaveExcl(&s_instanceRwLock);
        return pInstance;
    }
    return NULL;
}


/* static */
VirtualBoxTranslator *VirtualBoxTranslator::tryInstance() RT_NOEXCEPT
{
    int vrc = RTOnce(&g_Once, initLock, NULL);
    if (RT_SUCCESS(vrc))
    {
        RTCritSectRwEnterShared(&s_instanceRwLock);
        VirtualBoxTranslator *pInstance = s_pInstance;
        if (RT_LIKELY(pInstance != NULL))
        {
            uint32_t cRefs = ASMAtomicIncU32(&pInstance->m_cInstanceRefs);
            Assert(cRefs > 1); Assert(cRefs < _8K); RT_NOREF(cRefs);
        }
        RTCritSectRwLeaveShared(&s_instanceRwLock);
        return pInstance;
    }
    return NULL;
}


/**
 * Release translator reference previous obtained via instance() or
 * tryinstance().
 */
void VirtualBoxTranslator::release()
{
    RTCritSectRwEnterShared(&s_instanceRwLock);
    uint32_t cRefs = ASMAtomicDecU32(&m_cInstanceRefs);
    Assert(cRefs < _8K);
    if (RT_LIKELY(cRefs > 0))
        RTCritSectRwLeaveShared(&s_instanceRwLock);
    else
    {
        /* Looks like we've got the last reference. Must switch to exclusive
           mode for safe cleanup. */
        ASMAtomicIncU32(&m_cInstanceRefs);
        RTCritSectRwLeaveShared(&s_instanceRwLock);
        RTCritSectRwEnterExcl(&s_instanceRwLock);
        cRefs = ASMAtomicDecU32(&m_cInstanceRefs);
        Assert(cRefs < _8K);
        if (cRefs == 0)
        {
            s_pInstance = NULL;
            delete this;
        }
        RTCritSectRwLeaveExcl(&s_instanceRwLock);
    }
}


HRESULT VirtualBoxTranslator::loadLanguage(ComPtr<IVirtualBox> aVirtualBox)
{
    AssertReturn(aVirtualBox, E_INVALIDARG);

    ComPtr<ISystemProperties> pSystemProperties;
    HRESULT hrc = aVirtualBox->COMGETTER(SystemProperties)(pSystemProperties.asOutParam());
    if (SUCCEEDED(hrc))
    {
        com::Bstr bstrLocale;
        hrc = pSystemProperties->COMGETTER(LanguageId)(bstrLocale.asOutParam());
        if (SUCCEEDED(hrc))
        {
            int vrc = i_loadLanguage(com::Utf8Str(bstrLocale).c_str());
            if (RT_FAILURE(vrc))
                hrc = Global::vboxStatusCodeToCOM(vrc);
        }
    }
    return hrc;
}


com::Utf8Str VirtualBoxTranslator::language()
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    return m_strLanguage;
}


int VirtualBoxTranslator::i_loadLanguage(const char *pszLang)
{
    LogFlowFunc(("pszLang=%s\n", pszLang));
    int  vrc = VINF_SUCCESS;
    char szLocale[256];
    if (pszLang == NULL || *pszLang == '\0')
    {
        vrc = vboxGetDefaultUserLanguage(szLocale, sizeof(szLocale));
        if (RT_SUCCESS(vrc))
            pszLang = szLocale;
    }
    else
    {
        /* check the pszLang looks like language code, i.e. {ll} or {ll}_{CC} */
        size_t cbLang = strlen(pszLang);
        if (   !(cbLang == 1 && pszLang[0] == 'C')
            && !(cbLang == 2 && RT_C_IS_LOWER(pszLang[0]) && RT_C_IS_LOWER(pszLang[1]))
            && !(cbLang == 5 && RTLOCALE_IS_LANGUAGE2_UNDERSCORE_COUNTRY2(pszLang)))
            vrc = VERR_INVALID_PARAMETER;
    }
    if (RT_SUCCESS(vrc))
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        m_strLanguage = pszLang;

        for (TranslatorList::iterator it = m_lTranslators.begin();
             it != m_lTranslators.end();
             ++it)
        {
            /* ignore errors from particular translator allowing the use of others */
            i_loadLanguageForComponent(&(*it), pszLang);
        }
    }
    return vrc;
}


int VirtualBoxTranslator::i_loadLanguageForComponent(TranslatorComponent *aComponent, const char *aLang)
{
    AssertReturn(aComponent, VERR_INVALID_PARAMETER);
    LogFlow(("aComponent=%s aLang=%s\n", aComponent->strPath.c_str(), aLang));

    int vrc;
    if (strcmp(aLang, "C") != 0)
    {
        /* Construct the base filename for the translations: */
        char szNlsPath[RTPATH_MAX];
        /* Try load language file on form 'VirtualBoxAPI_ll_CC.qm' if it exists
           where 'll_CC' could for example be 'en_US' or 'de_CH': */
        ssize_t cchOkay = RTStrPrintf2(szNlsPath, sizeof(szNlsPath), "%s_%s.qm",
                                       aComponent->strPath.c_str(), aLang);
        if (cchOkay > 0)
            vrc = i_setLanguageFile(aComponent, szNlsPath);
        else
            vrc = VERR_FILENAME_TOO_LONG;
        if (RT_FAILURE(vrc))
        {
            /* No luck, drop the country part, i.e. 'VirtualBoxAPI_de.qm' or 'VirtualBoxAPI_en.qm': */
            const char *pszDash = strchr(aLang, '_');
            if (pszDash && pszDash != aLang)
            {
                cchOkay = RTStrPrintf2(szNlsPath, sizeof(szNlsPath), "%s_%.*s.qm",
                                       aComponent->strPath.c_str(), pszDash - aLang, aLang);
                if (cchOkay > 0)
                    vrc = i_setLanguageFile(aComponent, szNlsPath);
            }
        }
    }
    else
    {
        /* No translator needed for 'C' */
        delete aComponent->pTranslator;
        aComponent->pTranslator = NULL;
        vrc = VINF_SUCCESS;
    }
    return vrc;
}


int VirtualBoxTranslator::i_setLanguageFile(TranslatorComponent *aComponent, const char *aFileName)
{
    AssertReturn(aComponent, VERR_INVALID_PARAMETER);

    int vrc = m_rcCache;
    if (m_hStrCache != NIL_RTSTRCACHE)
    {
        QMTranslator *pNewTranslator;
        try { pNewTranslator = new QMTranslator(); }
        catch (std::bad_alloc &) { pNewTranslator = NULL; }
        if (pNewTranslator)
        {
            vrc = pNewTranslator->load(aFileName, m_hStrCache);
            if (RT_SUCCESS(vrc))
            {
                if (aComponent->pTranslator)
                    delete aComponent->pTranslator;
                aComponent->pTranslator = pNewTranslator;
            }
            else
                delete pNewTranslator;
        }
        else
            vrc = VERR_NO_MEMORY;
    }
    else
        Assert(RT_FAILURE_NP(vrc));
    return vrc;
}


int VirtualBoxTranslator::registerTranslation(const char *aTranslationPath,
                                              bool aDefault,
                                              PTRCOMPONENT *aComponent)
{
    VirtualBoxTranslator *pCurrInstance = VirtualBoxTranslator::tryInstance();
    int vrc = VERR_GENERAL_FAILURE;
    if (pCurrInstance != NULL)
    {
        vrc = pCurrInstance->i_registerTranslation(aTranslationPath, aDefault, aComponent);
        pCurrInstance->release();
    }
    return vrc;
}


int VirtualBoxTranslator::i_registerTranslation(const char *aTranslationPath,
                                                bool aDefault,
                                                PTRCOMPONENT *aComponent)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    TranslatorComponent *pComponent;
    for (TranslatorList::iterator it = m_lTranslators.begin();
         it != m_lTranslators.end();
         ++it)
    {
        if (it->strPath == aTranslationPath)
        {
            pComponent = &(*it);
            if (aDefault)
                m_pDefaultComponent = pComponent;
            *aComponent = (PTRCOMPONENT)pComponent;
            return VINF_SUCCESS;
        }
    }

    try
    {
        m_lTranslators.push_back(TranslatorComponent());
        pComponent = &m_lTranslators.back();
    }
    catch(std::bad_alloc &)
    {
        return VERR_NO_MEMORY;
    }

    pComponent->strPath = aTranslationPath;
    if (aDefault)
        m_pDefaultComponent = pComponent;
    *aComponent = (PTRCOMPONENT)pComponent;
    /* ignore the error during loading because path
     * could contain no translation for current language */
    i_loadLanguageForComponent(pComponent, m_strLanguage.c_str());
    return VINF_SUCCESS;
}


int VirtualBoxTranslator::unregisterTranslation(PTRCOMPONENT aComponent)
{
    int vrc;
    if (aComponent != NULL)
    {
        VirtualBoxTranslator *pCurrInstance = VirtualBoxTranslator::tryInstance();
        if (pCurrInstance != NULL)
        {
            vrc = pCurrInstance->i_unregisterTranslation(aComponent);
            pCurrInstance->release();
        }
        else
            vrc = VERR_GENERAL_FAILURE;
    }
    else
        vrc = VWRN_NOT_FOUND;
    return vrc;
}


int VirtualBoxTranslator::i_unregisterTranslation(PTRCOMPONENT aComponent)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (aComponent == m_pDefaultComponent)
        m_pDefaultComponent = NULL;

    for (TranslatorList::iterator it = m_lTranslators.begin();
         it != m_lTranslators.end();
         ++it)
    {
        if (&(*it) == aComponent)
        {
            delete aComponent->pTranslator;
            m_lTranslators.erase(it);
            return VINF_SUCCESS;
        }
    }

    return VERR_NOT_FOUND;
}


const char *VirtualBoxTranslator::translate(PTRCOMPONENT aComponent,
                                            const char  *aContext,
                                            const char  *aSourceText,
                                            const char  *aComment,
                                            const size_t aNum) RT_NOEXCEPT
{
    VirtualBoxTranslator *pCurrInstance = VirtualBoxTranslator::tryInstance();
    const char *pszTranslation = aSourceText;
    if (pCurrInstance != NULL)
    {
        pszTranslation = pCurrInstance->i_translate(aComponent, aContext, aSourceText, aComment, aNum);
        pCurrInstance->release();
    }
    return pszTranslation;
}


const char *VirtualBoxTranslator::i_translate(PTRCOMPONENT aComponent,
                                              const char  *aContext,
                                              const char  *aSourceText,
                                              const char  *aComment,
                                              const size_t aNum) RT_NOEXCEPT
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (aComponent == NULL)
        aComponent = m_pDefaultComponent;

    if (   aComponent == NULL
        || aComponent->pTranslator == NULL)
        return aSourceText;

    const char *pszSafeSource  = NULL;
    const char *pszTranslation = aComponent->pTranslator->translate(aContext, aSourceText, &pszSafeSource, aComment, aNum);
    if (pszSafeSource && g_idxTlsSrc != NIL_RTTLS && g_idxTlsTr != NIL_RTTLS)
    {
        RTTlsSet(g_idxTlsTr, (void *)pszTranslation);
        RTTlsSet(g_idxTlsSrc, (void *)pszSafeSource);
    }

    return pszTranslation;
}


const char *VirtualBoxTranslator::trSource(const char *aTranslation) RT_NOEXCEPT
{
    const char *pszSource = aTranslation;
    VirtualBoxTranslator *pCurInstance = VirtualBoxTranslator::tryInstance(); /* paranoia */
    if (pCurInstance != NULL)
    {
        if (g_idxTlsSrc != NIL_RTTLS && g_idxTlsTr != NIL_RTTLS)
        {
            const char * const pszTranslationTls = (const char *)RTTlsGet(g_idxTlsTr);
            const char * const pszSourceTls      = (const char *)RTTlsGet(g_idxTlsSrc);
            if (   pszSourceTls      != NULL
                && pszTranslationTls != NULL
                && (   pszTranslationTls == aTranslation
                    || strcmp(pszTranslationTls, aTranslation) == 0))
                pszSource = pszSourceTls;
        }
        pCurInstance->release();
    }
    return pszSource;
}


int32_t VirtualBoxTranslator::initCritSect()
{
    return RTCritSectRwInit(&s_instanceRwLock);
}
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
