/* $Id: UINetworkReply.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UINetworkReply stuff implementation.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

/* Qt includes: */
#include <QDir>
#include <QFile>
#include <QThread>
#include <QVector>
#include <QVariant>

/* GUI includes: */
#include "UILoggingDefs.h"
#include "UINetworkReply.h"
#include "UINetworkRequestManager.h"
#include "UIExtraDataManager.h"
#ifndef VBOX_GUI_IN_TST_SSL_CERT_DOWNLOADS
# include "UIGlobalSession.h"
# include "VBoxUtils.h"
# include "CSystemProperties.h"
#endif

/* Other VBox includes: */
#include <iprt/initterm.h>
#include <iprt/crypto/pem.h>
#include <iprt/crypto/store.h>
#include <iprt/err.h>
#include <iprt/http.h>
#include <iprt/path.h>
#include <iprt/sha.h>
#include <iprt/string.h>
#include <iprt/zip.h>


/** QThread extension
  * used as network-reply private thread interface. */
class UINetworkReplyPrivateThread : public QThread
{
#ifndef VBOX_GUI_IN_TST_SSL_CERT_DOWNLOADS
    Q_OBJECT;

signals:

    /** Notifies listeners about reply progress change.
      * @param  iBytesReceived  Holds the current amount of bytes received.
      * @param  iBytesTotal     Holds the total amount of bytes to be received. */
    void sigDownloadProgress(qint64 iBytesReceived, qint64 iBytesTotal);
#endif /* !VBOX_GUI_IN_TST_SSL_CERT_DOWNLOADS */

public:

    /** Constructs network-reply thread of the passed @a type for the passed @a url and @a requestHeaders. */
    UINetworkReplyPrivateThread(UINetworkRequestType type, const QUrl &url, const QString &strTarget, const UserDictionary &requestHeaders);

    /** @name APIs
     * @{ */
        /** Aborts reply. */
        void abort();

        /** Returns the URL of the reply which is the URL of the request for now. */
        const QUrl& url() const { return m_url; }

        /** Returns the last cached IPRT HTTP error of the reply. */
        int error() const { return m_iError; }

        /** Returns binary content of the reply. */
        const QByteArray& readAll() const { return m_reply; }
        /** Returns value for the cached reply header of the passed @a type. */
        QString header(UINetworkReply::KnownHeader type) const;

        /** Returns short descriptive context of thread's current operation. */
        const QString context() const { return m_strContext; }
    /** @} */

private:

    /** @name Helpers for HTTP and Certificates handling.
     * @{ */
        /** Applies configuration. */
        int applyConfiguration();
        /** Applies proxy rules. */
        int applyProxyRules();
        /** Applies security certificates. */
        int applyHttpsCertificates();
        /** Applies raw headers. */
        int applyRawHeaders();
        /** Performs main request. */
        int performMainRequest();

        /** Performs whole thread functionality. */
        void run() RT_OVERRIDE RT_FINAL;

        /** Handles download progress callback.
          * @param  cbDownloadTotal  Brings the total amount of bytes to be received.
          * @param  cbDownloaded     Brings the current amount of bytes received. */
        void handleProgressChange(uint64_t cbDownloadTotal, uint64_t cbDownloaded);
    /** @} */

    /** @name Static helpers for HTTP and Certificates handling.
     * @{ */
        /** Returns full certificate file-name. */
        static QString fullCertificateFileName();

        /** Applies proxy rules.
          * @remarks  Implementation doesn't exists, to be removed? */
        static int applyProxyRules(RTHTTP hHttp, const QString &strHostName, int iPort);

        /** Applies raw headers.
          * @param  hHttp    Brings the HTTP client instance.
          * @param  headers  Brings the map of headers to be applied. */
        static int applyRawHeaders(RTHTTP hHttp, const UserDictionary &headers);

        /** Returns the number of certificates found in a search result array.
          * @param  pafFoundCerts  Brings the array parallel to s_aCerts with the status of each wanted certificate. */
        static unsigned countCertsFound(bool const *pafFoundCerts);

        /** Returns whether we've found all the necessary certificates.
          * @param  pafFoundCerts  Brings the array parallel to s_aCerts with the status of each wanted certificate. */
        static bool areAllCertsFound(bool const *pafFoundCerts);

        /** Refreshes the certificates.
          * @param  phStore        On input, this holds the current store, so that we can fish out wanted
          *                        certificates from it. On successful return, this is replaced with a new
          *                        store reflecting the refrehsed content of @a pszCaCertFile.
          * @param  pafFoundCerts  On input, this holds the certificates found in the current store.
          *                        On return, this reflects what is current in the @a pszCaCertFile.
          *                        The array runs parallel to s_aCerts.
          * @param  pszCaCertFile  Where to write the refreshed certificates if we've managed to gather
          *                        a collection that is at least as good as the old one. */
        static int refreshCertificates(PRTCRSTORE phStore, bool *pafFoundCerts, const char *pszCaCertFile);

        /** Redirects download progress callback to particular object which can handle it.
          * @param  hHttp            Brings the HTTP client instance.
          * @param  pvUser           Brings the convenience pointer for the
          *                          user-agent object which should handle that callback.
          * @param  cbDownloadTotal  Brings the total amount of bytes to be received.
          * @param  cbDownloaded     Brings the current amount of bytes received. */
        static DECLCALLBACK(void) handleProgressChange(RTHTTP hHttp, void *pvUser, uint64_t cbDownloadTotal, uint64_t cbDownloaded);
    /** @} */

    /** Holds the request type. */
    const UINetworkRequestType m_type;
    /** Holds the request url. */
    const QUrl m_url;
    /** Holds the request target. */
    const QString m_strTarget;
    /** Holds the request headers. */
    const UserDictionary m_requestHeaders;

    /** Holds the IPRT HTTP client instance handle. */
    RTHTTP m_hHttp;
    /** Holds the last cached IPRT HTTP error of the reply. */
    int m_iError;
    /** Holds short descriptive context of thread's current operation. */
    QString m_strContext;
    /** Holds the reply instance. */
    QByteArray m_reply;
    /** Holds the cached reply headers. */
    UserDictionary m_headers;

    /** Holds the details on the certificates we are after. */
    static const RTCRCERTWANTED s_aCerts[];
    /** Holds the certificate file name (no path). */
    static const QString s_strCertificateFileName;

#ifdef VBOX_GUI_IN_TST_SSL_CERT_DOWNLOADS
public:
    /** Starts the test routine. */
    static void testIt(RTTEST hTest);
#endif /* VBOX_GUI_IN_TST_SSL_CERT_DOWNLOADS */
};


#ifndef VBOX_GUI_IN_TST_SSL_CERT_DOWNLOADS
/** QObject extension
  * used as network-reply private data interface. */
class UINetworkReplyPrivate : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies listeners about reply progress change.
      * @param  iBytesReceived  Holds the current amount of bytes received.
      * @param  iBytesTotal     Holds the total amount of bytes to be received. */
    void downloadProgress(qint64 iBytesReceived, qint64 iBytesTotal);

    /** Notifies listeners about reply has finished processing. */
    void finished();

public:

    /** Constructs network-reply private data of the passed @a type for the passed @a url and @a requestHeaders. */
    UINetworkReplyPrivate(UINetworkRequestType type, const QUrl &url, const QString &strTarget, const UserDictionary &requestHeaders);
    /** Destructs reply private data. */
    ~UINetworkReplyPrivate();

    /** Aborts reply. */
    void abort() { m_pThread->abort(); }

    /** Returns URL of the reply. */
    QUrl url() const { return m_pThread->url(); }

    /** Returns the last cached error of the reply. */
    UINetworkReply::NetworkError error() const { return m_error; }
    /** Returns the user-oriented string corresponding to the last cached error of the reply. */
    QString errorString() const;

    /** Returns binary content of the reply. */
    QByteArray readAll() const { return m_pThread->readAll(); }
    /** Returns value for the cached reply header of the passed @a type. */
    QString header(UINetworkReply::KnownHeader type) const { return m_pThread->header(type); }

private slots:

    /** Handles signal about reply has finished processing. */
    void sltFinished();

private:

    /** Holds the last cached error of the reply. */
    UINetworkReply::NetworkError m_error;

    /** Holds the reply thread instance. */
    UINetworkReplyPrivateThread *m_pThread;
};
#endif /* !VBOX_GUI_IN_TST_SSL_CERT_DOWNLOADS */


/*********************************************************************************************************************************
*   Class UINetworkReplyPrivateThread implementation.                                                                            *
*********************************************************************************************************************************/

/* static */
const RTCRCERTWANTED UINetworkReplyPrivateThread::s_aCerts[] =
{
    /*[0] =*/
    {
        /*.pszSubject        =*/
        "C=US, O=DigiCert Inc, OU=www.digicert.com, CN=DigiCert Global Root CA",
         /*.cbEncoded         =*/    0x3b3,
        /*.Sha1Fingerprint   =*/    true,
        /*.Sha512Fingerprint =*/    true,
        /*.abSha1            =*/
        {
            0xa8, 0x98, 0x5d, 0x3a, 0x65, 0xe5, 0xe5, 0xc4, 0xb2, 0xd7,
            0xd6, 0x6d, 0x40, 0xc6, 0xdd, 0x2f, 0xb1, 0x9c, 0x54, 0x36
        },
        /*.abSha512          =*/
        {
            0x53, 0xb4, 0x44, 0xe5, 0x65, 0x18, 0x32, 0x01,
            0xa6, 0x1e, 0xeb, 0x46, 0x12, 0x09, 0xb2, 0xdc,
            0x30, 0x89, 0x5e, 0xec, 0xa4, 0x87, 0x23, 0x8d,
            0x15, 0xa0, 0x26, 0x73, 0x5f, 0x22, 0x9a, 0x81,
            0x9e, 0x5b, 0x19, 0xcb, 0xd7, 0xe2, 0xfa, 0x27,
            0x68, 0xab, 0x2a, 0x64, 0xf6, 0xeb, 0xcd, 0x9d,
            0x1e, 0x72, 0x13, 0x41, 0xc9, 0xed, 0x5d, 0xd0,
            0x9f, 0xc0, 0xd5, 0xe4, 0x3d, 0x68, 0xbc, 0xa7
        },
        /*.pvUser            =*/ NULL,
    },
};

/* static */
const QString UINetworkReplyPrivateThread::s_strCertificateFileName = QString("vbox-ssl-cacertificate.crt");

UINetworkReplyPrivateThread::UINetworkReplyPrivateThread(UINetworkRequestType type,
                                                         const QUrl &url,
                                                         const QString &strTarget,
                                                         const UserDictionary &requestHeaders)
    : m_type(type)
    , m_url(url)
    , m_strTarget(strTarget)
    , m_requestHeaders(requestHeaders)
    , m_hHttp(NIL_RTHTTP)
    , m_iError(VINF_SUCCESS)
{
}

void UINetworkReplyPrivateThread::abort()
{
    /* Call for abort: */
    if (m_hHttp != NIL_RTHTTP)
        RTHttpAbort(m_hHttp);
}

QString UINetworkReplyPrivateThread::header(UINetworkReply::KnownHeader type) const
{
    /* Look for known header type: */
    switch (type)
    {
        case UINetworkReply::ContentTypeHeader:   return m_headers.value("Content-Type");
        case UINetworkReply::ContentLengthHeader: return m_headers.value("Content-Length");
        case UINetworkReply::LastModifiedHeader:  return m_headers.value("Last-Modified");
        case UINetworkReply::LocationHeader:      return m_headers.value("Location");
        default: break;
    }
    /* Return null-string by default: */
    return QString();
}

int UINetworkReplyPrivateThread::applyConfiguration()
{
    /* Install downloading progress callback: */
    return RTHttpSetDownloadProgressCallback(m_hHttp, &UINetworkReplyPrivateThread::handleProgressChange, this);
}

int UINetworkReplyPrivateThread::applyProxyRules()
{
    /* Set thread context: */
    m_strContext = tr("During proxy configuration");

#ifndef VBOX_GUI_IN_TST_SSL_CERT_DOWNLOADS
    /* If the specific proxy settings are enabled, we'll use them
     * unless user disabled that functionality manually. */
    const CSystemProperties comProperties = gpGlobalSession->virtualBox().GetSystemProperties();
    const KProxyMode enmProxyMode = comProperties.GetProxyMode();
    AssertReturn(comProperties.isOk(), VERR_INTERNAL_ERROR_3);
    switch (enmProxyMode)
    {
        case KProxyMode_Manual:
            return RTHttpSetProxyByUrl(m_hHttp, comProperties.GetProxyURL().toUtf8().constData());
        case KProxyMode_NoProxy:
            return VINF_SUCCESS;
        default:
            break;
    }
#endif /* VBOX_GUI_IN_TST_SSL_CERT_DOWNLOADS */

    /* By default, use system proxy: */
    return RTHttpUseSystemProxySettings(m_hHttp);
}

int UINetworkReplyPrivateThread::applyHttpsCertificates()
{
    /* Check if we really need SSL: */
    if (!url().toString().startsWith("https:", Qt::CaseInsensitive))
        return VINF_SUCCESS;

    /* Set thread context: */
    m_strContext = tr("During certificate downloading");

    /*
     * Calc the filename of the CA certificate file.
     */
    const QString strFullCertificateFileName(fullCertificateFileName());
    QByteArray utf8FullCertificateFileName = strFullCertificateFileName.toUtf8();
    const char *pszCaCertFile = utf8FullCertificateFileName.constData();

    /*
     * Check the state of our CA certificate file, it's one of the following:
     *      - Missing, recreate from scratch (= refresh).
     *      - Everything is there and it is less than 28 days old, do nothing.
     *      - Everything is there but it's older than 28 days, refresh.
     *      - Missing certificates and is older than 1 min, refresh.
     *
     * Start by creating a store for loading the current state into, as we'll
     * be need that for the refresh.
     */
    RTCRSTORE hCurStore = NIL_RTCRSTORE;
    int rc = RTCrStoreCreateInMem(&hCurStore, 256);
    if (RT_SUCCESS(rc))
    {
        bool fRefresh    = true;
        bool afCertsFound[RT_ELEMENTS(s_aCerts)];
        RT_ZERO(afCertsFound);

        /*
         * Load the file if it exists.
         *
         * To effect regular updates, we need the modification date of the file,
         * so we use RTPathQueryInfoEx here and not RTFileExists.
         */
        RTFSOBJINFO Info;
        rc = RTPathQueryInfoEx(pszCaCertFile, &Info, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK);
        if (   RT_SUCCESS(rc)
            && RTFS_IS_FILE(Info.Attr.fMode))
        {
            RTERRINFOSTATIC StaticErrInfo;
            rc = RTCrStoreCertAddFromFile(hCurStore, RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR, pszCaCertFile,
                                          RTErrInfoInitStatic(&StaticErrInfo));
            if (RTErrInfoIsSet(&StaticErrInfo.Core))
                LogRel(("checkCertificates: %s\n", StaticErrInfo.Core.pszMsg));
            else
                AssertRC(rc);

            /*
             * Scan the store the for certificates we need, then see what we
             * need to do wrt file age.
             */
            rc = RTCrStoreCertCheckWanted(hCurStore, s_aCerts, RT_ELEMENTS(s_aCerts), afCertsFound);
            AssertRC(rc);
            RTTIMESPEC RefreshAge;
            uint32_t   cSecRefresh = rc == VINF_SUCCESS  ? 28 * RT_SEC_1DAY /* all found */ : 60 /* stuff missing */;
            fRefresh = RTTimeSpecCompare(&Info.ModificationTime, RTTimeSpecSubSeconds(RTTimeNow(&RefreshAge), cSecRefresh)) <= 0;
        }

        /*
         * Refresh the file if necessary.
         */
        if (fRefresh)
            refreshCertificates(&hCurStore, afCertsFound, pszCaCertFile);

        RTCrStoreRelease(hCurStore);

        /*
         * Final verdict.
         */
        if (areAllCertsFound(afCertsFound))
            rc = VINF_SUCCESS;
        else
            rc = VERR_NOT_FOUND; /** @todo r=bird: Why not try and let RTHttpGet* bitch if the necessary certs are missing? */

        /*
         * Set our custom CA file.
         */
        if (RT_SUCCESS(rc))
            rc = RTHttpSetCAFile(m_hHttp, pszCaCertFile);
    }
    return rc;
}

int UINetworkReplyPrivateThread::applyRawHeaders()
{
    /* Set thread context: */
    m_strContext = tr("During network request");

    /* Make sure we have a raw headers at all: */
    if (m_requestHeaders.isEmpty())
        return VINF_SUCCESS;

    /* Apply raw headers: */
    return applyRawHeaders(m_hHttp, m_requestHeaders);
}

int UINetworkReplyPrivateThread::performMainRequest()
{
    /* Set thread context: */
    m_strContext = tr("During network request");

    /* Paranoia: */
    m_reply.clear();

    /* Prepare result: */
    int rc = 0;

    /* Depending on request type: */
    switch (m_type)
    {
        case UINetworkRequestType_HEAD:
        {
            /* Perform blocking HTTP HEAD request: */
            void   *pvResponse = 0;
            size_t  cbResponse = 0;
            rc = RTHttpGetHeaderBinary(m_hHttp, m_url.toString().toUtf8().constData(), &pvResponse, &cbResponse);
            if (RT_SUCCESS(rc))
            {
                m_reply = QByteArray((char*)pvResponse, (int)cbResponse);
                RTHttpFreeResponse(pvResponse);
            }

            /* Paranoia: */
            m_headers.clear();

            /* Parse header contents: */
            const QString strHeaders = QString(m_reply);
            foreach (const QString &strHeader, strHeaders.split("\n", Qt::SkipEmptyParts))
            {
                const QStringList values = strHeader.split(": ", Qt::SkipEmptyParts);
                if (values.size() > 1)
                    m_headers[values.at(0)] = values.at(1);
            }

            /* Special handling of redirection header: */
            if (rc == VERR_HTTP_REDIRECTED)
            {
                char *pszBuf = 0;
                const int rrc = RTHttpGetRedirLocation(m_hHttp, &pszBuf);
                if (RT_SUCCESS(rrc))
                    m_headers["Location"] = QString(pszBuf);
                if (pszBuf)
                    RTMemFree(pszBuf);
            }

            break;
        }
        case UINetworkRequestType_GET:
        {
            /* Perform blocking HTTP GET request.
             * Keep in mind that if the target parameter is provided,
             * we are trying to download contents to file directly,
             * otherwise it will be downloaded to memory and it's
             * customer responsibility to save it afterwards. */
            if (m_strTarget.isEmpty())
            {
                void   *pvResponse = 0;
                size_t  cbResponse = 0;
                rc = RTHttpGetBinary(m_hHttp, m_url.toString().toUtf8().constData(), &pvResponse, &cbResponse);
                if (RT_SUCCESS(rc))
                {
                    m_reply = QByteArray((char*)pvResponse, (int)cbResponse);
                    RTHttpFreeResponse(pvResponse);
                }
            }
            else
            {
                rc = RTHttpGetFile(m_hHttp, m_url.toString().toUtf8().constData(), m_strTarget.toUtf8().constData());
                if (RT_SUCCESS(rc))
                {
                    QFile file(m_strTarget);
                    if (file.open(QIODevice::ReadOnly))
                        m_reply = file.readAll();
                }
            }

            break;
        }
        default:
            break;
    }

    /* Return result: */
    return rc;
}

void UINetworkReplyPrivateThread::run()
{
    /* Create HTTP client: */
    m_iError = RTHttpCreate(&m_hHttp);
    if (RT_SUCCESS(m_iError))
    {
        /* Apply configuration: */
        if (RT_SUCCESS(m_iError))
            m_iError = applyConfiguration();

        /* Apply proxy-rules: */
        if (RT_SUCCESS(m_iError))
            m_iError = applyProxyRules();

        /* Apply https-certificates: */
        if (RT_SUCCESS(m_iError))
            m_iError = applyHttpsCertificates();

        /* Assign raw-headers: */
        if (RT_SUCCESS(m_iError))
            m_iError = applyRawHeaders();

        /* Perform main request: */
        if (RT_SUCCESS(m_iError))
            m_iError = performMainRequest();

        /* Destroy HTTP client: */
        RTHTTP hHttp = m_hHttp;
        if (hHttp != NIL_RTHTTP)
        {
            /** @todo r=bird: There is a race here between this and abort()! */
            m_hHttp = NIL_RTHTTP;
            RTHttpDestroy(hHttp);
        }
    }
}

void UINetworkReplyPrivateThread::handleProgressChange(uint64_t cbDownloadTotal, uint64_t cbDownloaded)
{
#ifndef VBOX_GUI_IN_TST_SSL_CERT_DOWNLOADS
    /* Notify listeners about progress change: */
    emit sigDownloadProgress(cbDownloaded, cbDownloadTotal);
#else /* VBOX_GUI_IN_TST_SSL_CERT_DOWNLOADS */
    Q_UNUSED(cbDownloaded);
    Q_UNUSED(cbDownloadTotal);
#endif /* VBOX_GUI_IN_TST_SSL_CERT_DOWNLOADS */
}

/* static */
QString UINetworkReplyPrivateThread::fullCertificateFileName()
{
#ifndef VBOX_GUI_IN_TST_SSL_CERT_DOWNLOADS
    const QDir homeDir(QDir::toNativeSeparators(gpGlobalSession->homeFolder()));
    return QDir::toNativeSeparators(homeDir.absoluteFilePath(s_strCertificateFileName));
#else /* VBOX_GUI_IN_TST_SSL_CERT_DOWNLOADS */
    return QString("/not/such/agency/non-existing-file.cer");
#endif /* VBOX_GUI_IN_TST_SSL_CERT_DOWNLOADS */
}

/* static */
int UINetworkReplyPrivateThread::applyRawHeaders(RTHTTP hHttp, const UserDictionary &headers)
{
    /* Make sure HTTP is created: */
    if (hHttp == NIL_RTHTTP)
        return VERR_INVALID_HANDLE;

    /* We should format them first: */
    QVector<QByteArray> formattedHeaders;
    QVector<const char*> formattedHeaderPointers;
    foreach (const QString &header, headers.keys())
    {
        /* Prepare formatted representation: */
        QString strFormattedString = QString("%1: %2").arg(header, headers.value(header));
        formattedHeaders << strFormattedString.toUtf8();
        formattedHeaderPointers << formattedHeaders.last().constData();
    }
    const char **ppFormattedHeaders = formattedHeaderPointers.data();

    /* Apply HTTP headers: */
    return RTHttpSetHeaders(hHttp, formattedHeaderPointers.size(), ppFormattedHeaders);
}

/* static */
unsigned UINetworkReplyPrivateThread::countCertsFound(bool const *pafFoundCerts)
{
    unsigned cFound = 0;
    for (uint32_t i = 0; i < RT_ELEMENTS(s_aCerts); i++)
        cFound += pafFoundCerts[i];
    return cFound;
}

/* static */
bool UINetworkReplyPrivateThread::areAllCertsFound(bool const *pafFoundCerts)
{
    for (uint32_t i = 0; i < RT_ELEMENTS(s_aCerts); i++)
        if (!pafFoundCerts[i])
            return false;
    return true;
}

/* static */
int UINetworkReplyPrivateThread::refreshCertificates(PRTCRSTORE phStore, bool *pafFoundCerts, const char *pszCaCertFile)
{
    /*
     * Collect the standard assortment of SSL certificates.
     */
    uint32_t  cHint = RTCrStoreCertCount(*phStore);
    RTCRSTORE hNewStore;
    int rc = RTCrStoreCreateInMem(&hNewStore, cHint > 32 && cHint < _32K ? cHint + 16 : 256);
    if (RT_SUCCESS(rc))
    {
        RTERRINFOSTATIC StaticErrInfo;
        rc = RTHttpGatherCaCertsInStore(hNewStore, 0 /*fFlags*/, RTErrInfoInitStatic(&StaticErrInfo));
        if (RTErrInfoIsSet(&StaticErrInfo.Core))
            LogRel(("refreshCertificates/#1: %s\n", StaticErrInfo.Core.pszMsg));
        else if (rc == VERR_NOT_FOUND)
            LogRel(("refreshCertificates/#1: No trusted SSL certs found on the system, will try download...\n"));
        else
            AssertLogRelRC(rc);
        if (RT_SUCCESS(rc) || rc == VERR_NOT_FOUND)
        {
            /*
             * Check and see what we've got.  If we haven't got all we desire,
             * try add it from the previous store.
             */
            bool afNewFoundCerts[RT_ELEMENTS(s_aCerts)];
            RT_ZERO(afNewFoundCerts); /* paranoia */

            rc = RTCrStoreCertCheckWanted(hNewStore, s_aCerts, RT_ELEMENTS(s_aCerts), afNewFoundCerts);
            AssertLogRelRC(rc);
            Assert(rc != VINF_SUCCESS || areAllCertsFound(afNewFoundCerts));
            if (rc != VINF_SUCCESS)
            {
                rc = RTCrStoreCertAddWantedFromStore(hNewStore,
                                                     RTCRCERTCTX_F_ADD_IF_NOT_FOUND | RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR,
                                                     *phStore, s_aCerts, RT_ELEMENTS(s_aCerts), afNewFoundCerts);
                AssertLogRelRC(rc);
                Assert(rc != VINF_SUCCESS || areAllCertsFound(afNewFoundCerts));
            }

            /*
             * If that didn't help, seek out certificates in more obscure places,
             * like java, mozilla and mutt.
             */
            if (rc != VINF_SUCCESS)
            {
                rc = RTCrStoreCertAddWantedFromFishingExpedition(hNewStore,
                                                                 RTCRCERTCTX_F_ADD_IF_NOT_FOUND
                                                                 | RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR,
                                                                 s_aCerts, RT_ELEMENTS(s_aCerts), afNewFoundCerts,
                                                                 RTErrInfoInitStatic(&StaticErrInfo));
                if (RTErrInfoIsSet(&StaticErrInfo.Core))
                    LogRel(("refreshCertificates/#2: %s\n", StaticErrInfo.Core.pszMsg));
                Assert(rc != VINF_SUCCESS || areAllCertsFound(afNewFoundCerts));
            }

            /*
             * If we've got the same or better hit rate than the old store,
             * replace the CA certs file.
             */
            if (   areAllCertsFound(afNewFoundCerts)
                || countCertsFound(afNewFoundCerts) >= countCertsFound(pafFoundCerts) )
            {
                rc = RTCrStoreCertExportAsPem(hNewStore, 0 /*fFlags*/, pszCaCertFile);
                if (RT_SUCCESS(rc))
                {
                    LogRel(("refreshCertificates/#3: Found %u/%u SSL certs we/you trust (previously %u/%u).\n",
                            countCertsFound(afNewFoundCerts), RTCrStoreCertCount(hNewStore),
                            countCertsFound(pafFoundCerts), RTCrStoreCertCount(*phStore) ));

                    memcpy(pafFoundCerts, afNewFoundCerts, sizeof(afNewFoundCerts));
                    RTCrStoreRelease(*phStore);
                    *phStore  = hNewStore;
                    hNewStore = NIL_RTCRSTORE;
                }
                else
                {
                    RT_ZERO(pafFoundCerts);
                    LogRel(("refreshCertificates/#3: RTCrStoreCertExportAsPem unexpectedly failed with %Rrc\n", rc));
                }
            }
            else
                LogRel(("refreshCertificates/#3: Sticking with the old file, missing essential certs.\n"));
        }
        RTCrStoreRelease(hNewStore);
    }
    return rc;
}

/* static */
DECLCALLBACK(void) UINetworkReplyPrivateThread::handleProgressChange(RTHTTP hHttp, void *pvUser, uint64_t cbDownloadTotal, uint64_t cbDownloaded)
{
    /* Redirect callback to particular object: */
    Q_UNUSED(hHttp);
    AssertPtrReturnVoid(pvUser);
    static_cast<UINetworkReplyPrivateThread*>(pvUser)->handleProgressChange(cbDownloadTotal, cbDownloaded);
}


#ifndef VBOX_GUI_IN_TST_SSL_CERT_DOWNLOADS


/*********************************************************************************************************************************
*   Class UINetworkReplyPrivate implementation.                                                                                  *
*********************************************************************************************************************************/

UINetworkReplyPrivate::UINetworkReplyPrivate(UINetworkRequestType type, const QUrl &url, const QString &strTarget, const UserDictionary &requestHeaders)
    : m_error(UINetworkReply::NoError)
    , m_pThread(0)
{
    /* Create and run reply thread: */
    m_pThread = new UINetworkReplyPrivateThread(type, url, strTarget, requestHeaders);
    connect(m_pThread, &UINetworkReplyPrivateThread::sigDownloadProgress,
            this, &UINetworkReplyPrivate::downloadProgress, Qt::QueuedConnection);
    connect(m_pThread, &UINetworkReplyPrivateThread::finished,
            this, &UINetworkReplyPrivate::sltFinished);
    m_pThread->start();
}

UINetworkReplyPrivate::~UINetworkReplyPrivate()
{
    /* Terminate network-reply thread: */
    m_pThread->abort();
    m_pThread->wait();
    delete m_pThread;
    m_pThread = 0;
}

QString UINetworkReplyPrivate::errorString() const
{
    /* Look for known error codes: */
    switch (m_error)
    {
        case UINetworkReply::NoError:                     break;
        case UINetworkReply::RemoteHostClosedError:       return QString("%1: %2").arg(m_pThread->context(), tr("Unable to initialize HTTP library"));
        case UINetworkReply::UrlNotFoundError:            return QString("%1: %2").arg(m_pThread->context(), tr("Url not found on the server"));
        case UINetworkReply::HostNotFoundError:           return QString("%1: %2").arg(m_pThread->context(), tr("Host not found"));
        case UINetworkReply::ContentAccessDenied:         return QString("%1: %2").arg(m_pThread->context(), tr("Content access denied"));
        case UINetworkReply::ProtocolFailure:             return QString("%1: %2").arg(m_pThread->context(), tr("Protocol failure"));
        case UINetworkReply::ConnectionRefusedError:      return QString("%1: %2").arg(m_pThread->context(), tr("Connection refused"));
        case UINetworkReply::SslHandshakeFailedError:     return QString("%1: %2").arg(m_pThread->context(), tr("SSL authentication failed"));
        case UINetworkReply::AuthenticationRequiredError: return QString("%1: %2").arg(m_pThread->context(), tr("Wrong SSL certificate format"));
        case UINetworkReply::ContentReSendError:          return QString("%1: %2").arg(m_pThread->context(), tr("Content moved"));
        case UINetworkReply::ProxyNotFoundError:          return QString("%1: %2").arg(m_pThread->context(), tr("Proxy not found"));
        default:                                          return QString("%1: %2").arg(m_pThread->context(), tr("Unknown reason"));
    }
    /* Return null-string by default: */
    return QString();
}

void UINetworkReplyPrivate::sltFinished()
{
    /* Look for known error codes: */
    switch (m_pThread->error())
    {
        case VINF_SUCCESS:                         m_error = UINetworkReply::NoError; break;
        case VERR_HTTP_INIT_FAILED:                m_error = UINetworkReply::RemoteHostClosedError; break;
        case VERR_HTTP_NOT_FOUND:                  m_error = UINetworkReply::UrlNotFoundError; break;
        case VERR_HTTP_HOST_NOT_FOUND:             m_error = UINetworkReply::HostNotFoundError; break;
        case VERR_HTTP_ACCESS_DENIED:              m_error = UINetworkReply::ContentAccessDenied; break;
        case VERR_HTTP_BAD_REQUEST:                m_error = UINetworkReply::ProtocolFailure; break;
        case VERR_HTTP_COULDNT_CONNECT:            m_error = UINetworkReply::ConnectionRefusedError; break;
        case VERR_HTTP_SSL_CONNECT_ERROR:          m_error = UINetworkReply::SslHandshakeFailedError; break;
        case VERR_HTTP_CACERT_WRONG_FORMAT:        m_error = UINetworkReply::AuthenticationRequiredError; break;
        case VERR_HTTP_CACERT_CANNOT_AUTHENTICATE: m_error = UINetworkReply::AuthenticationRequiredError; break;
        case VERR_HTTP_ABORTED:                    m_error = UINetworkReply::OperationCanceledError; break;
        case VERR_HTTP_REDIRECTED:                 m_error = UINetworkReply::ContentReSendError; break;
        case VERR_HTTP_PROXY_NOT_FOUND:            m_error = UINetworkReply::ProxyNotFoundError; break;
        default:                                   m_error = UINetworkReply::UnknownNetworkError; break;
    }
    /* Redirect signal to external listeners: */
    emit finished();
}


/*********************************************************************************************************************************
*   Class UINetworkReply implementation.                                                                                         *
*********************************************************************************************************************************/

UINetworkReply::UINetworkReply(UINetworkRequestType type, const QUrl &url, const QString &strTarget, const UserDictionary &requestHeaders)
    : m_pReply(new UINetworkReplyPrivate(type, url, strTarget, requestHeaders))
{
    /* Prepare network-reply object connections: */
    connect(m_pReply, &UINetworkReplyPrivate::downloadProgress, this, &UINetworkReply::downloadProgress);
    connect(m_pReply, &UINetworkReplyPrivate::finished,         this, &UINetworkReply::finished);
}

UINetworkReply::~UINetworkReply()
{
    /* Cleanup network-reply object: */
    if (m_pReply)
    {
        delete m_pReply;
        m_pReply = 0;
    }
}

void UINetworkReply::abort()
{
    return m_pReply->abort();
}

QUrl UINetworkReply::url() const
{
    return m_pReply->url();
}

UINetworkReply::NetworkError UINetworkReply::error() const
{
    return m_pReply->error();
}

QString UINetworkReply::errorString() const
{
    return m_pReply->errorString();
}

QByteArray UINetworkReply::readAll() const
{
    return m_pReply->readAll();
}

QVariant UINetworkReply::header(UINetworkReply::KnownHeader header) const
{
    return m_pReply->header(header);
}

#include "UINetworkReply.moc"

#endif /* !VBOX_GUI_IN_TST_SSL_CERT_DOWNLOADS */
