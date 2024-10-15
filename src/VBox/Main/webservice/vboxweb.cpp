/* $Id: vboxweb.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * vboxweb.cpp:
 *      hand-coded parts of the webservice server. This is linked with the
 *      generated code in out/.../src/VBox/Main/webservice/methodmaps.cpp
 *      (plus static gSOAP server code) to implement the actual webservice
 *      server, to which clients can connect.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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

// shared webservice header
#include "vboxweb.h"

// vbox headers
#include <VBox/com/com.h>
#include <VBox/com/array.h>
#include <VBox/com/string.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>
#include <VBox/com/listeners.h>
#include <VBox/com/NativeEventQueue.h>
#include <VBox/VBoxAuth.h>
#include <VBox/version.h>
#include <VBox/log.h>

#include <iprt/buildconfig.h>
#include <iprt/ctype.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/ldr.h>
#include <iprt/message.h>
#include <iprt/process.h>
#include <iprt/rand.h>
#include <iprt/semaphore.h>
#include <iprt/critsect.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/time.h>
#include <iprt/path.h>
#include <iprt/system.h>
#include <iprt/base64.h>
#include <iprt/stream.h>
#include <iprt/asm.h>

#ifdef WITH_OPENSSL
# include <openssl/opensslv.h>
#endif

#ifndef RT_OS_WINDOWS
# include <signal.h>
#endif

// workaround for compile problems on gcc 4.1
#ifdef __GNUC__
#pragma GCC visibility push(default)
#endif

// gSOAP headers (must come after vbox includes because it checks for conflicting defs)
#include "soapH.h"

// standard headers
#include <map>
#include <list>

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

// include generated namespaces table
#include "vboxwebsrv.nsmap"

RT_C_DECLS_BEGIN

// declarations for the generated WSDL text
extern const unsigned char g_abVBoxWebWSDL[];
extern const unsigned g_cbVBoxWebWSDL;

RT_C_DECLS_END

static void WebLogSoapError(struct soap *soap);

/****************************************************************************
 *
 * private typedefs
 *
 ****************************************************************************/

typedef std::map<uint64_t, ManagedObjectRef*>   ManagedObjectsMapById;
typedef ManagedObjectsMapById::iterator         ManagedObjectsIteratorById;
typedef std::map<uintptr_t, ManagedObjectRef*>  ManagedObjectsMapByPtr;
typedef ManagedObjectsMapByPtr::iterator        ManagedObjectsIteratorByPtr;

typedef std::map<uint64_t, WebServiceSession*>  WebsessionsMap;
typedef WebsessionsMap::iterator                WebsessionsMapIterator;

typedef std::map<RTTHREAD, com::Utf8Str> ThreadsMap;

static DECLCALLBACK(int) fntWatchdog(RTTHREAD ThreadSelf, void *pvUser);

/****************************************************************************
 *
 * Read-only global variables
 *
 ****************************************************************************/

static ComPtr<IVirtualBoxClient> g_pVirtualBoxClient = NULL;

// generated strings in methodmaps.cpp
extern const char       *g_pcszISession,
                        *g_pcszIVirtualBox,
                        *g_pcszIVirtualBoxErrorInfo;

// globals for vboxweb command-line arguments
#define DEFAULT_TIMEOUT_SECS 300
#define DEFAULT_TIMEOUT_SECS_STRING "300"
static int              g_iWatchdogTimeoutSecs = DEFAULT_TIMEOUT_SECS;
static int              g_iWatchdogCheckInterval = 5;

static const char       *g_pcszBindToHost = NULL;       // host; NULL = localhost
static unsigned int     g_uBindToPort = 18083;          // port
static unsigned int     g_uBacklog = 100;               // backlog = max queue size for connections

#ifdef WITH_OPENSSL
static bool             g_fSSL = false;                 // if SSL is enabled
static const char       *g_pcszKeyFile = NULL;          // server key file
static const char       *g_pcszPassword = NULL;         // password for server key
static const char       *g_pcszCACert = NULL;           // file with trusted CA certificates
static const char       *g_pcszCAPath = NULL;           // directory with trusted CA certificates
static const char       *g_pcszDHFile = NULL;           // DH file name or DH key length in bits, NULL=use RSA
static const char       *g_pcszRandFile = NULL;         // file with random data seed
static const char       *g_pcszSID = "vboxwebsrv";      // server ID for SSL session cache
#endif /* WITH_OPENSSL */

static unsigned int     g_cMaxWorkerThreads = 100;      // max. no. of worker threads
static unsigned int     g_cMaxKeepAlive = 100;          // maximum number of soap requests in one connection

static const char       *g_pcszAuthentication = NULL;   // web service authentication

static uint32_t         g_cHistory = 10;                // enable log rotation, 10 files
static uint32_t         g_uHistoryFileTime = RT_SEC_1DAY; // max 1 day per file
static uint64_t         g_uHistoryFileSize = 100 * _1M; // max 100MB per file
bool                    g_fVerbose = false;             // be verbose

static bool             g_fDaemonize = false;           // run in background.
static volatile bool    g_fKeepRunning = true;          // controlling the exit

const WSDLT_ID          g_EmptyWSDLID;                  // for NULL MORs

/****************************************************************************
 *
 * Writeable global variables
 *
 ****************************************************************************/

// The one global SOAP queue created by main().
class SoapQ;
static SoapQ                    *g_pSoapQ = NULL;

// this mutex protects the auth lib and authentication
static util::WriteLockHandle    *g_pAuthLibLockHandle;

// this mutex protects the global VirtualBox reference below
static util::RWLockHandle       *g_pVirtualBoxLockHandle;

static ComPtr<IVirtualBox>      g_pVirtualBox = NULL;

// this mutex protects all of the below
util::WriteLockHandle           *g_pWebsessionsLockHandle;

static WebsessionsMap           g_mapWebsessions;
static ULONG64                  g_cManagedObjects = 0;

// this mutex protects g_mapThreads
static util::RWLockHandle       *g_pThreadsLockHandle;

// Threads map, so we can quickly map an RTTHREAD struct to a logger prefix
static ThreadsMap               g_mapThreads;

/****************************************************************************
 *
 *  Command line help
 *
 ****************************************************************************/

static const RTGETOPTDEF g_aOptions[]
    = {
        { "--help",             'h', RTGETOPT_REQ_NOTHING }, /* for DisplayHelp() */
#if defined(RT_OS_DARWIN) || defined(RT_OS_LINUX) || defined (RT_OS_SOLARIS) || defined(RT_OS_FREEBSD)
        { "--background",       'b', RTGETOPT_REQ_NOTHING },
#endif
        { "--host",             'H', RTGETOPT_REQ_STRING },
        { "--port",             'p', RTGETOPT_REQ_UINT32 },
#ifdef WITH_OPENSSL
        { "--ssl",              's', RTGETOPT_REQ_NOTHING },
        { "--keyfile",          'K', RTGETOPT_REQ_STRING },
        { "--passwordfile",     'a', RTGETOPT_REQ_STRING },
        { "--cacert",           'c', RTGETOPT_REQ_STRING },
        { "--capath",           'C', RTGETOPT_REQ_STRING },
        { "--dhfile",           'D', RTGETOPT_REQ_STRING },
        { "--randfile",         'r', RTGETOPT_REQ_STRING },
#endif /* WITH_OPENSSL */
        { "--timeout",          't', RTGETOPT_REQ_UINT32 },
        { "--check-interval",   'i', RTGETOPT_REQ_UINT32 },
        { "--threads",          'T', RTGETOPT_REQ_UINT32 },
        { "--keepalive",        'k', RTGETOPT_REQ_UINT32 },
        { "--authentication",   'A', RTGETOPT_REQ_STRING },
        { "--verbose",          'v', RTGETOPT_REQ_NOTHING },
        { "--pidfile",          'P', RTGETOPT_REQ_STRING },
        { "--logfile",          'F', RTGETOPT_REQ_STRING },
        { "--logrotate",        'R', RTGETOPT_REQ_UINT32 },
        { "--logsize",          'S', RTGETOPT_REQ_UINT64 },
        { "--loginterval",      'I', RTGETOPT_REQ_UINT32 }
    };

static void DisplayHelp()
{
    RTStrmPrintf(g_pStdErr, "\nUsage: vboxwebsrv [options]\n\nSupported options (default values in brackets):\n");
    for (unsigned i = 0;
         i < RT_ELEMENTS(g_aOptions);
         ++i)
    {
        std::string str(g_aOptions[i].pszLong);
        str += ", -";
        str += g_aOptions[i].iShort;
        str += ":";

        const char *pcszDescr = "";

        switch (g_aOptions[i].iShort)
        {
            case 'h':
                pcszDescr = "Print this help message and exit.";
                break;

#if defined(RT_OS_DARWIN) || defined(RT_OS_LINUX) || defined (RT_OS_SOLARIS) || defined(RT_OS_FREEBSD)
            case 'b':
                pcszDescr = "Run in background (daemon mode).";
                break;
#endif

            case 'H':
                pcszDescr = "The host to bind to (localhost).";
                break;

            case 'p':
                pcszDescr = "The port to bind to (18083).";
                break;

#ifdef WITH_OPENSSL
            case 's':
                pcszDescr = "Enable SSL/TLS encryption.";
                break;

            case 'K':
                pcszDescr = "Server key and certificate file, PEM format (\"\").";
                break;

            case 'a':
                pcszDescr = "File name for password to server key (\"\").";
                break;

            case 'c':
                pcszDescr = "CA certificate file, PEM format (\"\").";
                break;

            case 'C':
                pcszDescr = "CA certificate path (\"\").";
                break;

            case 'D':
                pcszDescr = "DH file name or DH key length in bits (\"\").";
                break;

            case 'r':
                pcszDescr = "File containing seed for random number generator (\"\").";
                break;
#endif /* WITH_OPENSSL */

            case 't':
                pcszDescr = "Session timeout in seconds; 0 = disable timeouts (" DEFAULT_TIMEOUT_SECS_STRING ").";
                break;

            case 'T':
                pcszDescr = "Maximum number of worker threads to run in parallel (100).";
                break;

            case 'k':
                pcszDescr = "Maximum number of requests before a connection will be closed (100).";
                break;

            case 'A':
                pcszDescr = "Authentication method for the webservice (\"\").";
                break;

            case 'i':
                pcszDescr = "Frequency of timeout checks in seconds (5).";
                break;

            case 'v':
                pcszDescr = "Be verbose.";
                break;

            case 'P':
                pcszDescr = "Name of the PID file which is created when the daemon was started.";
                break;

            case 'F':
                pcszDescr = "Name of file to write log to (no file).";
                break;

            case 'R':
                pcszDescr = "Number of log files (0 disables log rotation).";
                break;

            case 'S':
                pcszDescr = "Maximum size of a log file to trigger rotation (bytes).";
                break;

            case 'I':
                pcszDescr = "Maximum time interval to trigger log rotation (seconds).";
                break;
        }

        RTStrmPrintf(g_pStdErr, "%-23s%s\n", str.c_str(), pcszDescr);
    }
}

/****************************************************************************
 *
 * SoapQ, SoapThread (multithreading)
 *
 ****************************************************************************/

class SoapQ;

class SoapThread
{
public:
    /**
     * Constructor. Creates the new thread and makes it call process() for processing the queue.
     * @param u Thread number. (So we can count from 1 and be readable.)
     * @param q SoapQ instance which has the queue to process.
     */
    SoapThread(size_t u,
               SoapQ &q)
        : m_u(u),
          m_pQ(&q)
    {
         m_strThread = com::Utf8StrFmt("SQW%02d", m_u);
        int vrc = RTThreadCreate(&m_pThread,
                                 fntWrapper,
                                 this,           // pvUser
                                 0,              // cbStack
                                 RTTHREADTYPE_MAIN_HEAVY_WORKER,
                                 0,
                                 m_strThread.c_str());
        if (RT_FAILURE(vrc))
        {
            RTMsgError("Cannot start worker thread %d: %Rrc\n", u, vrc);
            exit(1);
        }
    }

    void process();

    static int fnHttpGet(struct soap *soap)
    {
        char *s = strchr(soap->path, '?');
        if (!s || strcmp(s, "?wsdl"))
            return SOAP_GET_METHOD;
        soap_response(soap, SOAP_HTML);
        soap_send_raw(soap, (const char *)g_abVBoxWebWSDL, g_cbVBoxWebWSDL);
        soap_end_send(soap);
        return SOAP_OK;
    }

    /**
     * Static function that can be passed to RTThreadCreate and that calls
     * process() on the SoapThread instance passed as the thread parameter.
     *
     * @param   hThreadSelf
     * @param   pvThread
     * @return
     */
    static DECLCALLBACK(int) fntWrapper(RTTHREAD hThreadSelf, void *pvThread)
    {
        RT_NOREF(hThreadSelf);
        SoapThread *pst = (SoapThread*)pvThread;
        pst->process();
        return VINF_SUCCESS;
    }

    size_t          m_u;            // thread number
    com::Utf8Str    m_strThread;    // thread name ("SoapQWrkXX")
    SoapQ           *m_pQ;          // the single SOAP queue that all the threads service
    RTTHREAD        m_pThread;      // IPRT thread struct for this thread
};

/**
 * SOAP queue encapsulation. There is only one instance of this, to
 * which add() adds a queue item (called on the main thread),
 * and from which get() fetch items, called from each queue thread.
 */
class SoapQ
{
public:

    /**
     * Constructor. Creates the soap queue.
     */
    SoapQ()
        : m_mutex(util::LOCKCLASS_OBJECTSTATE),     // lowest lock order, no other may be held while this is held
          m_cIdleThreads(0)
    {
        RTSemEventMultiCreate(&m_event);
    }

    ~SoapQ()
    {
        /* Tell the threads to terminate. */
        RTSemEventMultiSignal(m_event);
        {
            util::AutoWriteLock qlock(m_mutex COMMA_LOCKVAL_SRC_POS);
            int i = 0;
            while (m_llAllThreads.size() && i++ <= 30)
            {
                qlock.release();
                RTThreadSleep(1000);
                RTSemEventMultiSignal(m_event);
                qlock.acquire();
            }
            LogRel(("ending queue processing (%d out of %d threads idle)\n", m_cIdleThreads, m_llAllThreads.size()));
        }

        RTSemEventMultiDestroy(m_event);

        while (!m_llSocketsQ.empty())
        {
            struct soap *pSoap = m_llSocketsQ.front();
            m_llSocketsQ.pop_front();

            if (pSoap == NULL || !soap_valid_socket(pSoap->socket))
                continue;

            soap_destroy(pSoap); // clean up class instances
            soap_end(pSoap); // clean up everything and close socket
            soap_free(pSoap); // free soap connection
        }
    }

    /**
     * Adds the given soap connection to the SOAP queue and posts the
     * member event sem to wake up the workers. Called on the main thread
     * whenever a connection comes in. Creates a new SOAP thread on the
     * first call or when all existing threads are busy.
     * @param pSoap connection
     */
    size_t add(const struct soap *pSoap)
    {
        size_t cItems;
        util::AutoWriteLock qlock(m_mutex COMMA_LOCKVAL_SRC_POS);

        // if no threads have yet been created, or if all threads are busy,
        // create a new SOAP thread
        if (    !m_cIdleThreads
                // but only if we're not exceeding the global maximum (default is 100)
             && (m_llAllThreads.size() < g_cMaxWorkerThreads)
           )
        {
            SoapThread *pst = new SoapThread(m_llAllThreads.size() + 1,
                                             *this);
            m_llAllThreads.push_back(pst);
            util::AutoWriteLock thrLock(g_pThreadsLockHandle COMMA_LOCKVAL_SRC_POS);
            g_mapThreads[pst->m_pThread] = com::Utf8StrFmt("[%3u]", pst->m_u);
            ++m_cIdleThreads;
        }

        // enqueue this connection and post eventsem so that one of the threads
        // (possibly the one just created) can pick it up
        m_llSocketsQ.push_back(soap_copy(pSoap));
        cItems = m_llSocketsQ.size();
        qlock.release();

        // unblock one of the worker threads
        RTSemEventMultiSignal(m_event);

        return cItems;
    }

    /**
     * Blocks the current thread until work comes in; then returns
     * the SOAP connection which has work to do. This reduces m_cIdleThreads
     * by one, and the caller MUST call done() when it's done processing.
     * Called from the worker threads.
     * @param cIdleThreads out: no. of threads which are currently idle (not counting the caller)
     * @param cThreads out: total no. of SOAP threads running
     * @return
     */
    struct soap *get(size_t &cIdleThreads, size_t &cThreads)
    {
        while (g_fKeepRunning)
        {
            // wait for something to happen
            RTSemEventMultiWait(m_event, RT_INDEFINITE_WAIT);

            if (!g_fKeepRunning)
                break;

            util::AutoWriteLock qlock(m_mutex COMMA_LOCKVAL_SRC_POS);
            if (!m_llSocketsQ.empty())
            {
                struct soap *pSoap = m_llSocketsQ.front();
                m_llSocketsQ.pop_front();
                cIdleThreads = --m_cIdleThreads;
                cThreads = m_llAllThreads.size();

                // reset the multi event only if the queue is now empty; otherwise
                // another thread will also wake up when we release the mutex and
                // process another one
                if (m_llSocketsQ.empty())
                    RTSemEventMultiReset(m_event);

                qlock.release();

                return pSoap;
            }

            // nothing to do: keep looping
        }
        return NULL;
    }

    /**
     * To be called by a worker thread after fetching an item from the
     * queue via get() and having finished its lengthy processing.
     */
    void done()
    {
        util::AutoWriteLock qlock(m_mutex COMMA_LOCKVAL_SRC_POS);
        ++m_cIdleThreads;
    }

    /**
     * To be called by a worker thread when signing off, i.e. no longer
     * willing to process SOAP connections.
     */
    void signoff(SoapThread *th)
    {
        {
            util::AutoWriteLock thrLock(g_pThreadsLockHandle COMMA_LOCKVAL_SRC_POS);
            size_t c = g_mapThreads.erase(th->m_pThread);
            AssertReturnVoid(c == 1);
        }
        {
            util::AutoWriteLock qlock(m_mutex COMMA_LOCKVAL_SRC_POS);
            m_llAllThreads.remove(th);
            --m_cIdleThreads;
        }
    }

    util::WriteLockHandle   m_mutex;
    RTSEMEVENTMULTI         m_event;            // posted by add(), blocked on by get()

    std::list<SoapThread*>  m_llAllThreads;     // all the threads created by the constructor
    size_t                  m_cIdleThreads;     // threads which are currently idle (statistics)

    // A std::list abused as a queue; this contains the actual jobs to do,
    // each entry being a connection from soap_accept() passed through SoapQ::add.
    std::list<struct soap *>  m_llSocketsQ;
};

/**
 * Thread function for each of the SOAP queue worker threads. This keeps
 * running, blocks on the event semaphore in SoapThread.SoapQ and picks
 * up a connection from the queue therein, which has been put there by
 * SoapQ::add().
 */
void SoapThread::process()
{
    LogRel(("New SOAP thread started\n"));

    while (g_fKeepRunning)
    {
        // wait for a job to arrive on the queue
        size_t cIdleThreads = 0, cThreads = 0;
        struct soap *pSoap = m_pQ->get(cIdleThreads, cThreads);

        if (pSoap == NULL || !soap_valid_socket(pSoap->socket))
            continue;

        pSoap->fget = fnHttpGet;

        /* The soap.max_keep_alive value can be set to the maximum keep-alive calls allowed,
         * which is important to avoid a client from holding a thread indefinitely.
         * http://www.cs.fsu.edu/~engelen/soapdoc2.html#sec:keepalive
         *
         * Strings with 8-bit content can hold ASCII (default) or UTF8. The latter is
         * possible by enabling the SOAP_C_UTFSTRING flag.
         */
        soap_set_omode(pSoap, SOAP_IO_KEEPALIVE | SOAP_C_UTFSTRING);
        soap_set_imode(pSoap, SOAP_IO_KEEPALIVE | SOAP_C_UTFSTRING);
        pSoap->max_keep_alive = g_cMaxKeepAlive;

        LogRel(("Processing connection from IP=%RTnaipv4 socket=%d (%d out of %d threads idle)\n",
                RT_H2N_U32(pSoap->ip), pSoap->socket, cIdleThreads, cThreads));

        // Ensure that we don't get stuck indefinitely for connections using
        // keepalive, otherwise stale connections tie up worker threads.
        pSoap->send_timeout = 60;
        pSoap->recv_timeout = 60;
        // Limit the maximum SOAP request size to a generous amount, just to
        // be on the safe side (SOAP is quite wordy when representing arrays,
        // and some API uses need to deal with large arrays). Good that binary
        // data is no longer represented by byte arrays...
        pSoap->recv_maxlength = _16M;
        // process the request; this goes into the COM code in methodmaps.cpp

        do {
#ifdef WITH_OPENSSL
            if (g_fSSL && soap_ssl_accept(pSoap))
            {
                WebLogSoapError(pSoap);
                break;
            }
#endif /* WITH_OPENSSL */
            soap_serve(pSoap);
        } while (0);

        soap_destroy(pSoap); // clean up class instances
        soap_end(pSoap); // clean up everything and close connection
        soap_free(pSoap); // free soap connection

        // tell the queue we're idle again
        m_pQ->done();
    }
    m_pQ->signoff(this);
}

/****************************************************************************
 *
 * VirtualBoxClient event listener
 *
 ****************************************************************************/

class VirtualBoxClientEventListener
{
public:
    VirtualBoxClientEventListener()
    {
    }

    virtual ~VirtualBoxClientEventListener()
    {
    }

    HRESULT init()
    {
       return S_OK;
    }

    void uninit()
    {
    }


    STDMETHOD(HandleEvent)(VBoxEventType_T aType, IEvent *aEvent)
    {
        switch (aType)
        {
            case VBoxEventType_OnVBoxSVCAvailabilityChanged:
            {
                ComPtr<IVBoxSVCAvailabilityChangedEvent> pVSACEv = aEvent;
                Assert(pVSACEv);
                BOOL fAvailable = FALSE;
                pVSACEv->COMGETTER(Available)(&fAvailable);
                if (!fAvailable)
                {
                    LogRel(("VBoxSVC became unavailable\n"));
                    {
                        util::AutoWriteLock vlock(g_pVirtualBoxLockHandle COMMA_LOCKVAL_SRC_POS);
                        g_pVirtualBox.setNull();
                    }
                    {
                        // we're messing with websessions, so lock them
                        util::AutoWriteLock lock(g_pWebsessionsLockHandle COMMA_LOCKVAL_SRC_POS);
                        WEBDEBUG(("SVC unavailable: deleting %d websessions\n", g_mapWebsessions.size()));

                        WebsessionsMapIterator it = g_mapWebsessions.begin(),
                                               itEnd = g_mapWebsessions.end();
                        while (it != itEnd)
                        {
                            WebServiceSession *pWebsession = it->second;
                            WEBDEBUG(("SVC unavailable: websession %#llx stale, deleting\n", pWebsession->getID()));
                            delete pWebsession;
                            it = g_mapWebsessions.begin();
                        }
                    }
                }
                else
                {
                    LogRel(("VBoxSVC became available\n"));
                    util::AutoWriteLock vlock(g_pVirtualBoxLockHandle COMMA_LOCKVAL_SRC_POS);
                    HRESULT hrc = g_pVirtualBoxClient->COMGETTER(VirtualBox)(g_pVirtualBox.asOutParam());
                    AssertComRC(hrc);
                }
                break;
            }
            default:
                AssertFailed();
        }

        return S_OK;
    }

private:
};

typedef ListenerImpl<VirtualBoxClientEventListener> VirtualBoxClientEventListenerImpl;

VBOX_LISTENER_DECLARE(VirtualBoxClientEventListenerImpl)

/**
 * Helper for printing SOAP error messages.
 * @param soap
 */
/*static*/
void WebLogSoapError(struct soap *soap)
{
    if (soap_check_state(soap))
    {
        LogRel(("Error: soap struct not initialized\n"));
        return;
    }

    const char *pcszFaultString = *soap_faultstring(soap);
    const char **ppcszDetail = soap_faultcode(soap);
    LogRel(("#### SOAP FAULT: %s [%s]\n",
            pcszFaultString ? pcszFaultString : "[no fault string available]",
            (ppcszDetail && *ppcszDetail) ? *ppcszDetail : "no details available"));
}

/**
 * Helper for decoding AuthResult.
 * @param result AuthResult
 */
static const char * decodeAuthResult(AuthResult result)
{
    switch (result)
    {
        case AuthResultAccessDenied:    return "access DENIED";
        case AuthResultAccessGranted:   return "access granted";
        case AuthResultDelegateToGuest: return "delegated to guest";
        default:                        return "unknown AuthResult";
    }
}

#if defined(WITH_OPENSSL) && (OPENSSL_VERSION_NUMBER < 0x10100000 || defined(LIBRESSL_VERSION_NUMBER))
/****************************************************************************
 *
 * OpenSSL convenience functions for multithread support.
 * Not required for OpenSSL 1.1+
 *
 ****************************************************************************/

static RTCRITSECT *g_pSSLMutexes = NULL;

struct CRYPTO_dynlock_value
{
    RTCRITSECT mutex;
};

static unsigned long CRYPTO_id_function()
{
    return (unsigned long)RTThreadNativeSelf();
}

static void CRYPTO_locking_function(int mode, int n, const char * /*file*/, int /*line*/)
{
    if (mode & CRYPTO_LOCK)
        RTCritSectEnter(&g_pSSLMutexes[n]);
    else
        RTCritSectLeave(&g_pSSLMutexes[n]);
}

static struct CRYPTO_dynlock_value *CRYPTO_dyn_create_function(const char * /*file*/, int /*line*/)
{
    static uint32_t s_iCritSectDynlock = 0;
    struct CRYPTO_dynlock_value *value = (struct CRYPTO_dynlock_value *)RTMemAlloc(sizeof(struct CRYPTO_dynlock_value));
    if (value)
        RTCritSectInitEx(&value->mutex, RTCRITSECT_FLAGS_NO_LOCK_VAL,
                         NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE,
                         "openssl-dyn-%u", ASMAtomicIncU32(&s_iCritSectDynlock) - 1);

    return value;
}

static void CRYPTO_dyn_lock_function(int mode, struct CRYPTO_dynlock_value *value, const char * /*file*/, int /*line*/)
{
    if (mode & CRYPTO_LOCK)
        RTCritSectEnter(&value->mutex);
    else
        RTCritSectLeave(&value->mutex);
}

static void CRYPTO_dyn_destroy_function(struct CRYPTO_dynlock_value *value, const char * /*file*/, int /*line*/)
{
    if (value)
    {
        RTCritSectDelete(&value->mutex);
        free(value);
    }
}

static int CRYPTO_thread_setup()
{
    int num_locks = CRYPTO_num_locks();
    g_pSSLMutexes = (RTCRITSECT *)RTMemAlloc(num_locks * sizeof(RTCRITSECT));
    if (!g_pSSLMutexes)
        return SOAP_EOM;

    for (int i = 0; i < num_locks; i++)
    {
        int vrc = RTCritSectInitEx(&g_pSSLMutexes[i], RTCRITSECT_FLAGS_NO_LOCK_VAL,
                                   NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE,
                                   "openssl-%d", i);
        if (RT_FAILURE(vrc))
        {
            for ( ; i >= 0; i--)
                RTCritSectDelete(&g_pSSLMutexes[i]);
            RTMemFree(g_pSSLMutexes);
            g_pSSLMutexes = NULL;
            return SOAP_EOM;
        }
    }

    CRYPTO_set_id_callback(CRYPTO_id_function);
    CRYPTO_set_locking_callback(CRYPTO_locking_function);
    CRYPTO_set_dynlock_create_callback(CRYPTO_dyn_create_function);
    CRYPTO_set_dynlock_lock_callback(CRYPTO_dyn_lock_function);
    CRYPTO_set_dynlock_destroy_callback(CRYPTO_dyn_destroy_function);

    return SOAP_OK;
}

static void CRYPTO_thread_cleanup()
{
    if (!g_pSSLMutexes)
        return;

    CRYPTO_set_id_callback(NULL);
    CRYPTO_set_locking_callback(NULL);
    CRYPTO_set_dynlock_create_callback(NULL);
    CRYPTO_set_dynlock_lock_callback(NULL);
    CRYPTO_set_dynlock_destroy_callback(NULL);

    int num_locks = CRYPTO_num_locks();
    for (int i = 0; i < num_locks; i++)
        RTCritSectDelete(&g_pSSLMutexes[i]);

    RTMemFree(g_pSSLMutexes);
    g_pSSLMutexes = NULL;
}
#endif /* WITH_OPENSSL && (OPENSSL_VERSION_NUMBER < 0x10100000 || defined(LIBRESSL_VERSION_NUMBER)) */

/****************************************************************************
 *
 * SOAP queue pumper thread
 *
 ****************************************************************************/

static void doQueuesLoop()
{
#if defined(WITH_OPENSSL) && (OPENSSL_VERSION_NUMBER < 0x10100000 || defined(LIBRESSL_VERSION_NUMBER))
    if (g_fSSL && CRYPTO_thread_setup())
    {
        LogRel(("Failed to set up OpenSSL thread mutex!"));
        exit(RTEXITCODE_FAILURE);
    }
#endif

    // set up gSOAP
    struct soap soap;
    soap_init(&soap);

#ifdef WITH_OPENSSL
    if (g_fSSL && soap_ssl_server_context(&soap, SOAP_SSL_REQUIRE_SERVER_AUTHENTICATION | SOAP_TLSv1, g_pcszKeyFile,
                                         g_pcszPassword, g_pcszCACert, g_pcszCAPath,
                                         g_pcszDHFile, g_pcszRandFile, g_pcszSID))
    {
        WebLogSoapError(&soap);
        exit(RTEXITCODE_FAILURE);
    }
#endif /* WITH_OPENSSL */

    soap.accept_timeout = 60;
    soap.bind_flags |= SO_REUSEADDR;
            // avoid EADDRINUSE on bind()

    SOAP_SOCKET m, s; // master and slave sockets
    m = soap_bind(&soap,
                  g_pcszBindToHost ? g_pcszBindToHost : "localhost",    // safe default host
                  g_uBindToPort,    // port
                  g_uBacklog);      // backlog = max queue size for connections
    if (m == SOAP_INVALID_SOCKET)
        WebLogSoapError(&soap);
    else
    {
#ifdef WITH_OPENSSL
        const char *pszSsl = g_fSSL ? "SSL, " : "";
#else /* !WITH_OPENSSL */
        const char *pszSsl = "";
#endif /*!WITH_OPENSSL */
        LogRel(("Socket connection successful: host = %s, port = %u, %smaster socket = %d\n",
                (g_pcszBindToHost) ? g_pcszBindToHost : "default (localhost)",
                g_uBindToPort, pszSsl, m));

        // initialize thread queue, mutex and eventsem
        g_pSoapQ = new SoapQ();

        uint64_t cAccepted = 1;
        while (g_fKeepRunning)
        {
            s = soap_accept(&soap);
            if (!soap_valid_socket(s))
            {
                if (soap.errnum != SOAP_EINTR)
                    WebLogSoapError(&soap);
                continue;
            }

            // add the connection to the queue and tell worker threads to
            // pick up the job
            size_t cItemsOnQ = g_pSoapQ->add(&soap);
            LogRel(("Connection %llu on socket %d queued for processing (%d items on Q)\n", cAccepted, s, cItemsOnQ));
            cAccepted++;
        }

        delete g_pSoapQ;
        g_pSoapQ = NULL;

        LogRel(("Ending SOAP connection handling\n"));

    }
    soap_done(&soap); // close master socket and detach environment

#if defined(WITH_OPENSSL) && (OPENSSL_VERSION_NUMBER < 0x10100000 || defined(LIBRESSL_VERSION_NUMBER))
    if (g_fSSL)
        CRYPTO_thread_cleanup();
#endif
}

/**
 * Thread function for the "queue pumper" thread started from main(). This implements
 * the loop that takes SOAP calls from HTTP and serves them by handing connections to the
 * SOAP queue worker threads.
 */
static DECLCALLBACK(int) fntQPumper(RTTHREAD hThreadSelf, void *pvUser)
{
    RT_NOREF(hThreadSelf, pvUser);

    // store a log prefix for this thread
    util::AutoWriteLock thrLock(g_pThreadsLockHandle COMMA_LOCKVAL_SRC_POS);
    g_mapThreads[RTThreadSelf()] = "[ P ]";
    thrLock.release();

    doQueuesLoop();

    thrLock.acquire();
    g_mapThreads.erase(RTThreadSelf());
    return VINF_SUCCESS;
}

#ifdef RT_OS_WINDOWS
/**
 * "Signal" handler for cleanly terminating the event loop.
 */
static BOOL WINAPI websrvSignalHandler(DWORD dwCtrlType)
{
    bool fEventHandled = FALSE;
    switch (dwCtrlType)
    {
        /* User pressed CTRL+C or CTRL+BREAK or an external event was sent
         * via GenerateConsoleCtrlEvent(). */
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_C_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
        {
            ASMAtomicWriteBool(&g_fKeepRunning, false);
            com::NativeEventQueue *pQ = com::NativeEventQueue::getMainEventQueue();
            pQ->interruptEventQueueProcessing();
            fEventHandled = TRUE;
            break;
        }
        default:
            break;
    }
    return fEventHandled;
}
#else
/**
 * Signal handler for cleanly terminating the event loop.
 */
static void websrvSignalHandler(int iSignal)
{
    NOREF(iSignal);
    ASMAtomicWriteBool(&g_fKeepRunning, false);
    com::NativeEventQueue *pQ = com::NativeEventQueue::getMainEventQueue();
    pQ->interruptEventQueueProcessing();
}
#endif


/**
 * Start up the webservice server. This keeps running and waits
 * for incoming SOAP connections; for each request that comes in,
 * it calls method implementation code, most of it in the generated
 * code in methodmaps.cpp.
 *
 * @param argc
 * @param argv[]
 * @return
 */
int main(int argc, char *argv[])
{
    // initialize runtime
    int vrc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(vrc))
        return RTMsgInitFailure(vrc);
#ifdef RT_OS_WINDOWS
    ATL::CComModule _Module; /* Required internally by ATL (constructor records instance in global variable). */
#endif

    // store a log prefix for this thread
    g_mapThreads[RTThreadSelf()] = "[M  ]";

    RTStrmPrintf(g_pStdErr, VBOX_PRODUCT " web service Version " VBOX_VERSION_STRING "\n"
                            "Copyright (C) 2007-" VBOX_C_YEAR " " VBOX_VENDOR "\n");

    int c;
    const char *pszLogFile = NULL;
    const char *pszPidFile = NULL;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, g_aOptions, RT_ELEMENTS(g_aOptions), 1, 0 /*fFlags*/);
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 'H':
                if (!ValueUnion.psz || !*ValueUnion.psz)
                {
                    /* Normalize NULL/empty string to NULL, which will be
                     * interpreted as "localhost" below. */
                    g_pcszBindToHost = NULL;
                }
                else
                    g_pcszBindToHost = ValueUnion.psz;
                break;

            case 'p':
                g_uBindToPort = ValueUnion.u32;
                break;

#ifdef WITH_OPENSSL
            case 's':
                g_fSSL = true;
                break;

            case 'K':
                g_pcszKeyFile = ValueUnion.psz;
                break;

            case 'a':
                if (ValueUnion.psz[0] == '\0')
                    g_pcszPassword = NULL;
                else
                {
                    PRTSTREAM StrmIn;
                    if (!strcmp(ValueUnion.psz, "-"))
                        StrmIn = g_pStdIn;
                    else
                    {
                        vrc = RTStrmOpen(ValueUnion.psz, "r", &StrmIn);
                        if (RT_FAILURE(vrc))
                            return RTMsgErrorExit(RTEXITCODE_FAILURE, "failed to open password file (%s, %Rrc)", ValueUnion.psz, vrc);
                    }
                    char szPasswd[512];
                    vrc = RTStrmGetLine(StrmIn, szPasswd, sizeof(szPasswd));
                    if (RT_FAILURE(vrc))
                        return RTMsgErrorExit(RTEXITCODE_FAILURE, "failed to read password (%s, %Rrc)", ValueUnion.psz, vrc);
                    g_pcszPassword = RTStrDup(szPasswd);
                    memset(szPasswd, '\0', sizeof(szPasswd));
                    if (StrmIn != g_pStdIn)
                        RTStrmClose(StrmIn);
                }
                break;

            case 'c':
                g_pcszCACert = ValueUnion.psz;
                break;

            case 'C':
                g_pcszCAPath = ValueUnion.psz;
                break;

            case 'D':
                g_pcszDHFile = ValueUnion.psz;
                break;

            case 'r':
                g_pcszRandFile = ValueUnion.psz;
                break;
#endif /* WITH_OPENSSL */

            case 't':
                g_iWatchdogTimeoutSecs = ValueUnion.u32;
                break;

            case 'i':
                g_iWatchdogCheckInterval = ValueUnion.u32;
                break;

            case 'F':
                pszLogFile = ValueUnion.psz;
                break;

            case 'R':
                g_cHistory = ValueUnion.u32;
                break;

            case 'S':
                g_uHistoryFileSize = ValueUnion.u64;
                break;

            case 'I':
                g_uHistoryFileTime = ValueUnion.u32;
                break;

            case 'P':
                pszPidFile = ValueUnion.psz;
                break;

            case 'T':
                g_cMaxWorkerThreads = ValueUnion.u32;
                break;

            case 'k':
                g_cMaxKeepAlive = ValueUnion.u32;
                break;

            case 'A':
                g_pcszAuthentication = ValueUnion.psz;
                break;

            case 'h':
                DisplayHelp();
                return 0;

            case 'v':
                g_fVerbose = true;
                break;

#if defined(RT_OS_DARWIN) || defined(RT_OS_LINUX) || defined (RT_OS_SOLARIS) || defined(RT_OS_FREEBSD)
            case 'b':
                g_fDaemonize = true;
                break;
#endif
            case 'V':
                RTPrintf("%sr%s\n", RTBldCfgVersion(), RTBldCfgRevisionStr());
                return 0;

            default:
                return RTGetOptPrintError(c, &ValueUnion);
        }
    }

    /* create release logger, to stdout */
    RTERRINFOSTATIC ErrInfo;
    vrc = com::VBoxLogRelCreate("web service", g_fDaemonize ? NULL : pszLogFile,
                                RTLOGFLAGS_PREFIX_THREAD | RTLOGFLAGS_PREFIX_TIME_PROG,
                                "all", "VBOXWEBSRV_RELEASE_LOG",
                                RTLOGDEST_STDOUT, UINT32_MAX /* cMaxEntriesPerGroup */,
                                g_cHistory, g_uHistoryFileTime, g_uHistoryFileSize,
                                RTErrInfoInitStatic(&ErrInfo));
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "failed to open release log (%s, %Rrc)", ErrInfo.Core.pszMsg, vrc);

#if defined(RT_OS_DARWIN) || defined(RT_OS_LINUX) || defined (RT_OS_SOLARIS) || defined(RT_OS_FREEBSD)
    if (g_fDaemonize)
    {
        /* prepare release logging */
        char szLogFile[RTPATH_MAX];

        if (!pszLogFile || !*pszLogFile)
        {
            vrc = com::GetVBoxUserHomeDirectory(szLogFile, sizeof(szLogFile));
            if (RT_FAILURE(vrc))
                 return RTMsgErrorExit(RTEXITCODE_FAILURE, "could not get base directory for logging: %Rrc", vrc);
            vrc = RTPathAppend(szLogFile, sizeof(szLogFile), "vboxwebsrv.log");
            if (RT_FAILURE(vrc))
                 return RTMsgErrorExit(RTEXITCODE_FAILURE, "could not construct logging path: %Rrc", vrc);
            pszLogFile = szLogFile;
        }

        vrc = RTProcDaemonizeUsingFork(false /* fNoChDir */, false /* fNoClose */, pszPidFile);
        if (RT_FAILURE(vrc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "failed to daemonize, vrc=%Rrc. exiting.", vrc);

        /* create release logger, to file */
        vrc = com::VBoxLogRelCreate("web service", pszLogFile,
                                    RTLOGFLAGS_PREFIX_THREAD | RTLOGFLAGS_PREFIX_TIME_PROG,
                                    "all", "VBOXWEBSRV_RELEASE_LOG",
                                    RTLOGDEST_FILE, UINT32_MAX /* cMaxEntriesPerGroup */,
                                    g_cHistory, g_uHistoryFileTime, g_uHistoryFileSize,
                                    RTErrInfoInitStatic(&ErrInfo));
        if (RT_FAILURE(vrc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "failed to open release log (%s, %Rrc)", ErrInfo.Core.pszMsg, vrc);
    }
#endif

    // initialize SOAP SSL support if enabled
#ifdef WITH_OPENSSL
    if (g_fSSL)
        soap_ssl_init();
#endif /* WITH_OPENSSL */

    // initialize COM/XPCOM
    HRESULT hrc = com::Initialize();
#ifdef VBOX_WITH_XPCOM
    if (hrc == NS_ERROR_FILE_ACCESS_DENIED)
    {
        char szHome[RTPATH_MAX] = "";
        com::GetVBoxUserHomeDirectory(szHome, sizeof(szHome));
        return RTMsgErrorExit(RTEXITCODE_FAILURE,
               "Failed to initialize COM because the global settings directory '%s' is not accessible!", szHome);
    }
#endif
    if (FAILED(hrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "failed to initialize COM! hrc=%Rhrc\n", hrc);

    hrc = g_pVirtualBoxClient.createInprocObject(CLSID_VirtualBoxClient);
    if (FAILED(hrc))
    {
        RTMsgError("failed to create the VirtualBoxClient object!");
        com::ErrorInfo info;
        if (!info.isFullAvailable() && !info.isBasicAvailable())
        {
            com::GluePrintRCMessage(hrc);
            RTMsgError("Most likely, the VirtualBox COM server is not running or failed to start.");
        }
        else
            com::GluePrintErrorInfo(info);
        return RTEXITCODE_FAILURE;
    }

    hrc = g_pVirtualBoxClient->COMGETTER(VirtualBox)(g_pVirtualBox.asOutParam());
    if (FAILED(hrc))
    {
        RTMsgError("Failed to get VirtualBox object (hrc=%Rhrc)!", hrc);
        return RTEXITCODE_FAILURE;
    }

    // set the authentication method if requested
    if (g_pVirtualBox && g_pcszAuthentication && g_pcszAuthentication[0])
    {
        ComPtr<ISystemProperties> pSystemProperties;
        g_pVirtualBox->COMGETTER(SystemProperties)(pSystemProperties.asOutParam());
        if (pSystemProperties)
            pSystemProperties->COMSETTER(WebServiceAuthLibrary)(com::Bstr(g_pcszAuthentication).raw());
    }

    /* VirtualBoxClient events registration. */
    ComPtr<IEventListener> vboxClientListener;
    {
        ComPtr<IEventSource> pES;
        CHECK_ERROR(g_pVirtualBoxClient, COMGETTER(EventSource)(pES.asOutParam()));
        ComObjPtr<VirtualBoxClientEventListenerImpl> clientListener;
        clientListener.createObject();
        clientListener->init(new VirtualBoxClientEventListener());
        vboxClientListener = clientListener;
        com::SafeArray<VBoxEventType_T> eventTypes;
        eventTypes.push_back(VBoxEventType_OnVBoxSVCAvailabilityChanged);
        CHECK_ERROR(pES, RegisterListener(vboxClientListener, ComSafeArrayAsInParam(eventTypes), true));
    }

    // create the global mutexes
    g_pAuthLibLockHandle = new util::WriteLockHandle(util::LOCKCLASS_WEBSERVICE);
    g_pVirtualBoxLockHandle = new util::RWLockHandle(util::LOCKCLASS_WEBSERVICE);
    g_pWebsessionsLockHandle = new util::WriteLockHandle(util::LOCKCLASS_WEBSERVICE);
    g_pThreadsLockHandle = new util::RWLockHandle(util::LOCKCLASS_OBJECTSTATE);

    // SOAP queue pumper thread
    RTTHREAD threadQPumper;
    vrc = RTThreadCreate(&threadQPumper,
                         fntQPumper,
                         NULL,        // pvUser
                         0,           // cbStack (default)
                         RTTHREADTYPE_MAIN_WORKER,
                         RTTHREADFLAGS_WAITABLE,
                         "SQPmp");
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Cannot start SOAP queue pumper thread: %Rrc", vrc);

    // watchdog thread
    RTTHREAD threadWatchdog = NIL_RTTHREAD;
    if (g_iWatchdogTimeoutSecs > 0)
    {
        // start our watchdog thread
        vrc = RTThreadCreate(&threadWatchdog,
                             fntWatchdog,
                             NULL,
                             0,
                             RTTHREADTYPE_MAIN_WORKER,
                             RTTHREADFLAGS_WAITABLE,
                             "Watchdog");
        if (RT_FAILURE(vrc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Cannot start watchdog thread: %Rrc", vrc);
    }

#ifdef RT_OS_WINDOWS
    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)websrvSignalHandler, TRUE /* Add handler */))
    {
        vrc = RTErrConvertFromWin32(GetLastError());
        RTMsgError("Unable to install console control handler, vrc=%Rrc\n", vrc);
    }
#else
    signal(SIGINT,   websrvSignalHandler);
    signal(SIGTERM,  websrvSignalHandler);
# ifdef SIGBREAK
    signal(SIGBREAK, websrvSignalHandler);
# endif
#endif

    com::NativeEventQueue *pQ = com::NativeEventQueue::getMainEventQueue();
    while (g_fKeepRunning)
    {
        // we have to process main event queue
        WEBDEBUG(("Pumping COM event queue\n"));
        vrc = pQ->processEventQueue(RT_INDEFINITE_WAIT);
        if (RT_FAILURE(vrc))
            RTMsgError("processEventQueue -> %Rrc", vrc);
    }

    LogRel(("requested termination, cleaning up\n"));

#ifdef RT_OS_WINDOWS
    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)websrvSignalHandler, FALSE /* Remove handler */))
    {
        vrc = RTErrConvertFromWin32(GetLastError());
        RTMsgError("Unable to remove console control handler, vrc=%Rrc\n", vrc);
    }
#else
    signal(SIGINT,   SIG_DFL);
    signal(SIGTERM,  SIG_DFL);
# ifdef SIGBREAK
    signal(SIGBREAK, SIG_DFL);
# endif
#endif

#ifndef RT_OS_WINDOWS
    RTThreadPoke(threadQPumper);
#endif
    RTThreadWait(threadQPumper, 30000, NULL);
    if (threadWatchdog != NIL_RTTHREAD)
    {
#ifndef RT_OS_WINDOWS
        RTThreadPoke(threadWatchdog);
#endif
        RTThreadWait(threadWatchdog, g_iWatchdogCheckInterval * 1000 + 10000, NULL);
    }

    /* VirtualBoxClient events unregistration. */
    if (vboxClientListener)
    {
        ComPtr<IEventSource> pES;
        CHECK_ERROR(g_pVirtualBoxClient, COMGETTER(EventSource)(pES.asOutParam()));
        if (!pES.isNull())
            CHECK_ERROR(pES, UnregisterListener(vboxClientListener));
        vboxClientListener.setNull();
    }

    {
        util::AutoWriteLock vlock(g_pVirtualBoxLockHandle COMMA_LOCKVAL_SRC_POS);
        g_pVirtualBox.setNull();
    }
    {
        util::AutoWriteLock lock(g_pWebsessionsLockHandle COMMA_LOCKVAL_SRC_POS);
        WebsessionsMapIterator it = g_mapWebsessions.begin(),
                               itEnd = g_mapWebsessions.end();
        while (it != itEnd)
        {
            WebServiceSession *pWebsession = it->second;
            WEBDEBUG(("SVC unavailable: websession %#llx stale, deleting\n", pWebsession->getID()));
            delete pWebsession;
            it = g_mapWebsessions.begin();
        }
    }
    g_pVirtualBoxClient.setNull();

    com::Shutdown();

    return 0;
}

/****************************************************************************
 *
 * Watchdog thread
 *
 ****************************************************************************/

/**
 * Watchdog thread, runs in the background while the webservice is alive.
 *
 * This gets started by main() and runs in the background to check all websessions
 * for whether they have been no requests in a configurable timeout period. In
 * that case, the websession is automatically logged off.
 */
static DECLCALLBACK(int) fntWatchdog(RTTHREAD hThreadSelf, void *pvUser)
{
    RT_NOREF(hThreadSelf, pvUser);

    // store a log prefix for this thread
    util::AutoWriteLock thrLock(g_pThreadsLockHandle COMMA_LOCKVAL_SRC_POS);
    g_mapThreads[RTThreadSelf()] = "[W  ]";
    thrLock.release();

    WEBDEBUG(("Watchdog thread started\n"));

    uint32_t tNextStat = 0;

    while (g_fKeepRunning)
    {
        WEBDEBUG(("Watchdog: sleeping %d seconds\n", g_iWatchdogCheckInterval));
        RTThreadSleep(g_iWatchdogCheckInterval * 1000);

        uint32_t tNow = RTTimeProgramSecTS();

        // we're messing with websessions, so lock them
        util::AutoWriteLock lock(g_pWebsessionsLockHandle COMMA_LOCKVAL_SRC_POS);
        WEBDEBUG(("Watchdog: checking %d websessions\n", g_mapWebsessions.size()));

        WebsessionsMapIterator it = g_mapWebsessions.begin(),
                               itEnd = g_mapWebsessions.end();
        while (it != itEnd)
        {
            WebServiceSession *pWebsession = it->second;
            WEBDEBUG(("Watchdog: tNow: %d, websession timestamp: %d\n", tNow, pWebsession->getLastObjectLookup()));
            if (tNow > pWebsession->getLastObjectLookup() + g_iWatchdogTimeoutSecs)
            {
                WEBDEBUG(("Watchdog: websession %#llx timed out, deleting\n", pWebsession->getID()));
                delete pWebsession;
                it = g_mapWebsessions.begin();
            }
            else
                ++it;
        }

        // re-set the authentication method in case it has been changed
        if (g_pVirtualBox && g_pcszAuthentication && g_pcszAuthentication[0])
        {
            ComPtr<ISystemProperties> pSystemProperties;
            g_pVirtualBox->COMGETTER(SystemProperties)(pSystemProperties.asOutParam());
            if (pSystemProperties)
                pSystemProperties->COMSETTER(WebServiceAuthLibrary)(com::Bstr(g_pcszAuthentication).raw());
        }

        // Log some MOR usage statistics every 5 minutes, but only if there's
        // something worth logging (at least one reference or a transition to
        // zero references). Avoids useless log spamming in idle webservice.
        if (tNow >= tNextStat)
        {
            size_t cMOR = 0;
            it = g_mapWebsessions.begin();
            itEnd = g_mapWebsessions.end();
            while (it != itEnd)
            {
                cMOR += it->second->CountRefs();
                ++it;
            }
            static bool fLastZero = false;
            if (cMOR || !fLastZero)
                LogRel(("Statistics: %zu websessions, %zu references\n",
                        g_mapWebsessions.size(), cMOR));
            fLastZero = (cMOR == 0);
            while (tNextStat <= tNow)
                tNextStat += 5 * 60; /* 5 minutes */
        }
    }

    thrLock.acquire();
    g_mapThreads.erase(RTThreadSelf());

    LogRel(("ending Watchdog thread\n"));
    return 0;
}

/****************************************************************************
 *
 * SOAP exceptions
 *
 ****************************************************************************/

/**
 * Helper function to raise a SOAP fault. Called by the other helper
 * functions, which raise specific SOAP faults.
 *
 * @param soap
 * @param pcsz
 * @param extype
 * @param ex
 */
static void RaiseSoapFault(struct soap *soap,
                           const char *pcsz,
                           int extype,
                           void *ex)
{
    // raise the fault
    soap_sender_fault(soap, pcsz, NULL);

    struct SOAP_ENV__Detail *pDetail = (struct SOAP_ENV__Detail*)soap_malloc(soap, sizeof(struct SOAP_ENV__Detail));

    // without the following, gSOAP crashes miserably when sending out the
    // data because it will try to serialize all fields (stupid documentation)
    memset(pDetail, 0, sizeof(struct SOAP_ENV__Detail));

    // fill extended info depending on SOAP version
    if (soap->version == 2) // SOAP 1.2 is used
    {
        soap->fault->SOAP_ENV__Detail = pDetail;
        soap->fault->SOAP_ENV__Detail->__type = extype;
        soap->fault->SOAP_ENV__Detail->fault = ex;
        soap->fault->SOAP_ENV__Detail->__any = NULL; // no other XML data
    }
    else
    {
        soap->fault->detail = pDetail;
        soap->fault->detail->__type = extype;
        soap->fault->detail->fault = ex;
        soap->fault->detail->__any = NULL; // no other XML data
    }
}

/**
 * Raises a SOAP fault that signals that an invalid object was passed.
 *
 * @param soap
 * @param obj
 */
void RaiseSoapInvalidObjectFault(struct soap *soap,
                                 WSDLT_ID obj)
{
    _vbox__InvalidObjectFault *ex = soap_new__vbox__InvalidObjectFault(soap, 1);
    ex->badObjectID = obj;

    std::string str("VirtualBox error: ");
    str += "Invalid managed object reference \"" + obj + "\"";

    RaiseSoapFault(soap,
                   str.c_str(),
                   SOAP_TYPE__vbox__InvalidObjectFault,
                   ex);
}

/**
 * Return a safe C++ string from the given COM string,
 * without crashing if the COM string is empty.
 * @param bstr
 * @return
 */
std::string ConvertComString(const com::Bstr &bstr)
{
    com::Utf8Str ustr(bstr);
    return ustr.c_str();        /// @todo r=dj since the length is known, we can probably use a better std::string allocator
}

/**
 * Return a safe C++ string from the given COM UUID,
 * without crashing if the UUID is empty.
 * @param uuid
 * @return
 */
std::string ConvertComString(const com::Guid &uuid)
{
    com::Utf8Str ustr(uuid.toString());
    return ustr.c_str();        /// @todo r=dj since the length is known, we can probably use a better std::string allocator
}

/** Code to handle string <-> byte arrays base64 conversion. */
std::string Base64EncodeByteArray(ComSafeArrayIn(BYTE, aData))
{

    com::SafeArray<BYTE> sfaData(ComSafeArrayInArg(aData));
    ssize_t cbData = sfaData.size();

    if (cbData == 0)
        return "";

    ssize_t cchOut = RTBase64EncodedLength(cbData);

    RTCString aStr;

    aStr.reserve(cchOut+1);
    int vrc = RTBase64Encode(sfaData.raw(), cbData, aStr.mutableRaw(), aStr.capacity(), NULL);
    AssertRC(vrc);
    aStr.jolt();

    return aStr.c_str();
}

#define DECODE_STR_MAX _1M
void Base64DecodeByteArray(struct soap *soap, const std::string& aStr, ComSafeArrayOut(BYTE, aData), const WSDLT_ID &idThis, const char *pszMethodName, IUnknown *pObj, const com::Guid &iid)
{
    const char* pszStr = aStr.c_str();
    ssize_t cbOut = RTBase64DecodedSize(pszStr, NULL);

    if (cbOut > DECODE_STR_MAX)
    {
        LogRel(("Decode string too long.\n"));
        RaiseSoapRuntimeFault(soap, idThis, pszMethodName, E_INVALIDARG, pObj, iid);
    }

    com::SafeArray<BYTE> result(cbOut);
    int vrc = RTBase64Decode(pszStr, result.raw(), cbOut, NULL, NULL);
    if (FAILED(vrc))
    {
        LogRel(("String Decoding Failed. Error code: %Rrc\n", vrc));
        RaiseSoapRuntimeFault(soap, idThis, pszMethodName, E_INVALIDARG, pObj, iid);
    }

    result.detachTo(ComSafeArrayOutArg(aData));
}

/**
 * Raises a SOAP runtime fault.
 *
 * @param soap
 * @param idThis
 * @param pcszMethodName
 * @param apirc
 * @param pObj
 * @param iid
 */
void RaiseSoapRuntimeFault(struct soap *soap,
                           const WSDLT_ID &idThis,
                           const char *pcszMethodName,
                           HRESULT apirc,
                           IUnknown *pObj,
                           const com::Guid &iid)
{
    com::ErrorInfo info(pObj, iid.ref());

    WEBDEBUG(("   error, raising SOAP exception\n"));

    LogRel(("API method name:            %s\n", pcszMethodName));
    LogRel(("API return code:            %#10lx (%Rhrc)\n", apirc, apirc));
    if (info.isFullAvailable() || info.isBasicAvailable())
    {
        const com::ErrorInfo *pInfo = &info;
        do
        {
            LogRel(("COM error info result code: %#10lx (%Rhrc)\n", pInfo->getResultCode(), pInfo->getResultCode()));
            LogRel(("COM error info text:        %ls\n", pInfo->getText().raw()));

            pInfo = pInfo->getNext();
        }
        while (pInfo);
    }

    // compose descriptive message
    com::Utf8StrFmt str("VirtualBox error: apirc=%#lx", apirc);
    if (info.isFullAvailable() || info.isBasicAvailable())
    {
        const com::ErrorInfo *pInfo = &info;
        do
        {
            str += com::Utf8StrFmt(" %ls (%#lx)", pInfo->getText().raw(), pInfo->getResultCode());
            pInfo = pInfo->getNext();
        }
        while (pInfo);
    }

    // allocate our own soap fault struct
    _vbox__RuntimeFault *ex = soap_new__vbox__RuntimeFault(soap, 1);
    ComPtr<IVirtualBoxErrorInfo> pVirtualBoxErrorInfo;
    info.getVirtualBoxErrorInfo(pVirtualBoxErrorInfo);
    ex->resultCode = apirc;
    ex->returnval = createOrFindRefFromComPtr(idThis, g_pcszIVirtualBoxErrorInfo, pVirtualBoxErrorInfo);

    RaiseSoapFault(soap,
                   str.c_str(),
                   SOAP_TYPE__vbox__RuntimeFault,
                   ex);
}

/****************************************************************************
 *
 *  splitting and merging of object IDs
 *
 ****************************************************************************/

/**
 * Splits a managed object reference (in string form, as passed in from a SOAP
 * method call) into two integers for websession and object IDs, respectively.
 *
 * @param id
 * @param pWebsessId
 * @param pObjId
 * @return
 */
static bool SplitManagedObjectRef(const WSDLT_ID &id,
                                  uint64_t *pWebsessId,
                                  uint64_t *pObjId)
{
    // 64-bit numbers in hex have 16 digits; hence
    // the object-ref string must have 16 + "-" + 16 characters
    if (    id.length() == 33
         && id[16] == '-'
       )
    {
        char psz[34];
        memcpy(psz, id.c_str(), 34);
        psz[16] = '\0';
        if (pWebsessId)
            RTStrToUInt64Full(psz, 16, pWebsessId);
        if (pObjId)
            RTStrToUInt64Full(psz + 17, 16, pObjId);
        return true;
    }

    return false;
}

/**
 * Creates a managed object reference (in string form) from
 * two integers representing a websession and object ID, respectively.
 *
 * @param sz Buffer with at least 34 bytes space to receive MOR string.
 * @param websessId
 * @param objId
 */
static void MakeManagedObjectRef(char *sz,
                                 uint64_t websessId,
                                 uint64_t objId)
{
    RTStrFormatNumber(sz, websessId, 16, 16, 0, RTSTR_F_64BIT | RTSTR_F_ZEROPAD);
    sz[16] = '-';
    RTStrFormatNumber(sz + 17, objId, 16, 16, 0, RTSTR_F_64BIT | RTSTR_F_ZEROPAD);
}

/****************************************************************************
 *
 *  class WebServiceSession
 *
 ****************************************************************************/

class WebServiceSessionPrivate
{
    public:
        ManagedObjectsMapById       _mapManagedObjectsById;
        ManagedObjectsMapByPtr      _mapManagedObjectsByPtr;
};

/**
 * Constructor for the websession object.
 *
 * Preconditions: Caller must have locked g_pWebsessionsLockHandle.
 */
WebServiceSession::WebServiceSession()
    : _uNextObjectID(1),        // avoid 0 for no real reason
      _fDestructing(false),
      _tLastObjectLookup(0)
{
    _pp = new WebServiceSessionPrivate;
    _uWebsessionID = RTRandU64();

    // register this websession globally
    Assert(g_pWebsessionsLockHandle->isWriteLockOnCurrentThread());
    g_mapWebsessions[_uWebsessionID] = this;
}

/**
 * Destructor. Cleans up and destroys all contained managed object references on the way.
 *
 * Preconditions: Caller must have locked g_pWebsessionsLockHandle.
 */
WebServiceSession::~WebServiceSession()
{
    // delete us from global map first so we can't be found
    // any more while we're cleaning up
    Assert(g_pWebsessionsLockHandle->isWriteLockOnCurrentThread());
    g_mapWebsessions.erase(_uWebsessionID);

    // notify ManagedObjectRef destructor so it won't
    // remove itself from the maps; this avoids rebalancing
    // the map's tree on every delete as well
    _fDestructing = true;

    ManagedObjectsIteratorById it,
                               end = _pp->_mapManagedObjectsById.end();
    for (it = _pp->_mapManagedObjectsById.begin();
         it != end;
         ++it)
    {
        ManagedObjectRef *pRef = it->second;
        delete pRef;        // this frees the contained ComPtr as well
    }

    delete _pp;
}

/**
 *  Authenticate the username and password against an authentication authority.
 *
 *  @return 0 if the user was successfully authenticated, or an error code
 *  otherwise.
 */
int WebServiceSession::authenticate(const char *pcszUsername,
                                    const char *pcszPassword,
                                    IVirtualBox **ppVirtualBox)
{
    int vrc = VERR_WEB_NOT_AUTHENTICATED;
    ComPtr<IVirtualBox> pVirtualBox;
    {
        util::AutoReadLock vlock(g_pVirtualBoxLockHandle COMMA_LOCKVAL_SRC_POS);
        pVirtualBox = g_pVirtualBox;
    }
    if (pVirtualBox.isNull())
        return vrc;
    pVirtualBox.queryInterfaceTo(ppVirtualBox);

    util::AutoReadLock lock(g_pAuthLibLockHandle COMMA_LOCKVAL_SRC_POS);

    static bool fAuthLibLoaded = false;
    static PAUTHENTRY pfnAuthEntry = NULL;
    static PAUTHENTRY2 pfnAuthEntry2 = NULL;
    static PAUTHENTRY3 pfnAuthEntry3 = NULL;

    if (!fAuthLibLoaded)
    {
        // retrieve authentication library from system properties
        ComPtr<ISystemProperties> systemProperties;
        pVirtualBox->COMGETTER(SystemProperties)(systemProperties.asOutParam());

        com::Bstr authLibrary;
        systemProperties->COMGETTER(WebServiceAuthLibrary)(authLibrary.asOutParam());
        com::Utf8Str filename = authLibrary;

        LogRel(("External authentication library is '%ls'\n", authLibrary.raw()));

        if (filename == "null")
            // authentication disabled, let everyone in:
            fAuthLibLoaded = true;
        else
        {
            RTLDRMOD hlibAuth = 0;
            do
            {
                if (RTPathHavePath(filename.c_str()))
                    vrc = RTLdrLoad(filename.c_str(), &hlibAuth);
                else
                    vrc = RTLdrLoadAppPriv(filename.c_str(), &hlibAuth);
                if (RT_FAILURE(vrc))
                {
                    WEBDEBUG(("%s() Failed to load external authentication library '%s'. Error code: %Rrc\n",
                              __FUNCTION__, filename.c_str(), vrc));
                    break;
                }

                if (RT_FAILURE(vrc = RTLdrGetSymbol(hlibAuth, AUTHENTRY3_NAME, (void**)&pfnAuthEntry3)))
                {
                    WEBDEBUG(("%s(): Could not resolve import '%s'. Error code: %Rrc\n",
                              __FUNCTION__, AUTHENTRY3_NAME, vrc));

                    if (RT_FAILURE(vrc = RTLdrGetSymbol(hlibAuth, AUTHENTRY2_NAME, (void**)&pfnAuthEntry2)))
                    {
                        WEBDEBUG(("%s(): Could not resolve import '%s'. Error code: %Rrc\n",
                                  __FUNCTION__, AUTHENTRY2_NAME, vrc));

                        if (RT_FAILURE(vrc = RTLdrGetSymbol(hlibAuth, AUTHENTRY_NAME, (void**)&pfnAuthEntry)))
                            WEBDEBUG(("%s(): Could not resolve import '%s'. Error code: %Rrc\n",
                                      __FUNCTION__, AUTHENTRY_NAME, vrc));
                    }
                }

                if (pfnAuthEntry || pfnAuthEntry2 || pfnAuthEntry3)
                    fAuthLibLoaded = true;

            } while (0);
        }
    }

    if (strlen(pcszUsername) >= _1K)
    {
        LogRel(("Access denied, excessive username length: %zu\n", strlen(pcszUsername)));
        vrc = VERR_WEB_NOT_AUTHENTICATED;
    }
    else if (strlen(pcszPassword) >= _1K)
    {
        LogRel(("Access denied, excessive password length: %zu\n", strlen(pcszPassword)));
        vrc = VERR_WEB_NOT_AUTHENTICATED;
    }
    else if (pfnAuthEntry3 || pfnAuthEntry2 || pfnAuthEntry)
    {
        const char *pszFn;
        AuthResult result;
        if (pfnAuthEntry3)
        {
            result = pfnAuthEntry3("webservice", NULL, AuthGuestNotAsked, pcszUsername, pcszPassword, NULL, true, 0);
            pszFn = AUTHENTRY3_NAME;
        }
        else if (pfnAuthEntry2)
        {
            result = pfnAuthEntry2(NULL, AuthGuestNotAsked, pcszUsername, pcszPassword, NULL, true, 0);
            pszFn = AUTHENTRY2_NAME;
        }
        else
        {
            result = pfnAuthEntry(NULL, AuthGuestNotAsked, pcszUsername, pcszPassword, NULL);
            pszFn = AUTHENTRY_NAME;
        }
        WEBDEBUG(("%s(): result of %s('%s', [%d]): %d (%s)\n",
                  __FUNCTION__, pszFn, pcszUsername, strlen(pcszPassword), result, decodeAuthResult(result)));
        if (result == AuthResultAccessGranted)
        {
            LogRel(("Access for user '%s' granted\n", pcszUsername));
            vrc = VINF_SUCCESS;
        }
        else
        {
            if (result == AuthResultAccessDenied)
                LogRel(("Access for user '%s' denied\n", pcszUsername));
            vrc = VERR_WEB_NOT_AUTHENTICATED;
        }
    }
    else if (fAuthLibLoaded)
    {
        // fAuthLibLoaded = true but all pointers are NULL:
        // The authlib was "null" and auth was disabled
        vrc = VINF_SUCCESS;
    }
    else
    {
        WEBDEBUG(("Could not resolve AuthEntry, VRDPAuth2 or VRDPAuth entry point"));
        vrc = VERR_WEB_NOT_AUTHENTICATED;
    }

    lock.release();

    return vrc;
}

/**
 *  Look up, in this websession, whether a ManagedObjectRef has already been
 *  created for the given COM pointer.
 *
 *  Note how we require that a ComPtr<IUnknown> is passed, which causes a
 *  queryInterface call when the caller passes in a different type, since
 *  a ComPtr<IUnknown> will point to something different than a
 *  ComPtr<IVirtualBox>, for example. As we store the ComPtr<IUnknown> in
 *  our private hash table, we must search for one too.
 *
 * Preconditions: Caller must have locked g_pWebsessionsLockHandle.
 *
 * @param pObject pointer to a COM object.
 * @return The existing ManagedObjectRef that represents the COM object, or NULL if there's none yet.
 */
ManagedObjectRef* WebServiceSession::findRefFromPtr(const IUnknown *pObject)
{
    Assert(g_pWebsessionsLockHandle->isWriteLockOnCurrentThread());

    uintptr_t ulp = (uintptr_t)pObject;
    // WEBDEBUG(("   %s: looking up %#lx\n", __FUNCTION__, ulp));
    ManagedObjectsIteratorByPtr it = _pp->_mapManagedObjectsByPtr.find(ulp);
    if (it != _pp->_mapManagedObjectsByPtr.end())
    {
        ManagedObjectRef *pRef = it->second;
        WEBDEBUG(("   %s: found existing ref %s (%s) for COM obj %#lx\n", __FUNCTION__, pRef->getWSDLID().c_str(), pRef->getInterfaceName(), ulp));
        return pRef;
    }

    return NULL;
}

/**
 * Static method which attempts to find the websession for which the given
 * managed object reference was created, by splitting the reference into the
 * websession and object IDs and then looking up the websession object.
 *
 * Preconditions: Caller must have locked g_pWebsessionsLockHandle in read mode.
 *
 * @param id Managed object reference (with combined websession and object IDs).
 * @return
 */
WebServiceSession *WebServiceSession::findWebsessionFromRef(const WSDLT_ID &id)
{
    Assert(g_pWebsessionsLockHandle->isWriteLockOnCurrentThread());

    WebServiceSession *pWebsession = NULL;
    uint64_t websessId;
    if (SplitManagedObjectRef(id,
                              &websessId,
                              NULL))
    {
        WebsessionsMapIterator it = g_mapWebsessions.find(websessId);
        if (it != g_mapWebsessions.end())
            pWebsession = it->second;
    }
    return pWebsession;
}

/**
 * Touches the websession to prevent it from timing out.
 *
 * Each websession has an internal timestamp that records the last request made
 * to it from the client that started it. If no request was made within a
 * configurable timeframe, then the client is logged off automatically,
 * by calling IWebsessionManager::logoff()
 */
void WebServiceSession::touch()
{
    _tLastObjectLookup = RTTimeProgramSecTS();
}

/**
 * Counts the number of managed object references in this websession.
 */
size_t WebServiceSession::CountRefs()
{
    return _pp->_mapManagedObjectsById.size();
}


/****************************************************************************
 *
 *  class ManagedObjectRef
 *
 ****************************************************************************/

/**
 *  Constructor, which assigns a unique ID to this managed object
 *  reference and stores it in two hashes (living in the associated
 *  WebServiceSession object):
 *
 *   a) _mapManagedObjectsById, which maps ManagedObjectID's to
 *      instances of this class; this hash is then used by the
 *      findObjectFromRef() template function in vboxweb.h
 *      to quickly retrieve the COM object from its managed
 *      object ID (mostly in the context of the method mappers
 *      in methodmaps.cpp, when a web service client passes in
 *      a managed object ID);
 *
 *   b) _mapManagedObjectsByPtr, which maps COM pointers to
 *      instances of this class; this hash is used by
 *      createRefFromObject() to quickly figure out whether an
 *      instance already exists for a given COM pointer.
 *
 *  This constructor calls AddRef() on the given COM object, and
 *  the destructor will call Release(). We require two input pointers
 *  for that COM object, one generic IUnknown* pointer which is used
 *  as the map key, and a specific interface pointer (e.g. IMachine*)
 *  which must support the interface given in guidInterface. All
 *  three values are returned by getPtr(), which gives future callers
 *  a chance to reuse the specific interface pointer without having
 *  to call QueryInterface, which can be expensive.
 *
 *  This does _not_ check whether another instance already
 *  exists in the hash. This gets called only from the
 *  createOrFindRefFromComPtr() template function in vboxweb.h, which
 *  does perform that check.
 *
 * Preconditions: Caller must have locked g_pWebsessionsLockHandle.
 *
 * @param websession Websession to which the MOR will be added.
 * @param pobjUnknown Pointer to IUnknown* interface for the COM object; this will be used in the hashes.
 * @param pobjInterface Pointer to a specific interface for the COM object, described by guidInterface.
 * @param guidInterface Interface which pobjInterface points to.
 * @param pcszInterface String representation of that interface (e.g. "IMachine") for readability and logging.
 */
ManagedObjectRef::ManagedObjectRef(WebServiceSession &websession,
                                   IUnknown *pobjUnknown,
                                   void *pobjInterface,
                                   const com::Guid &guidInterface,
                                   const char *pcszInterface)
    : _websession(websession),
      _pobjUnknown(pobjUnknown),
      _pobjInterface(pobjInterface),
      _guidInterface(guidInterface),
      _pcszInterface(pcszInterface)
{
    Assert(pobjUnknown);
    Assert(pobjInterface);

    // keep both stubs alive while this MOR exists (matching Release() calls are in destructor)
    uint32_t cRefs1 = pobjUnknown->AddRef();
    uint32_t cRefs2 = ((IUnknown*)pobjInterface)->AddRef();
    _ulp = (uintptr_t)pobjUnknown;

    Assert(g_pWebsessionsLockHandle->isWriteLockOnCurrentThread());
    _id = websession.createObjectID();
    // and count globally
    ULONG64 cTotal = ++g_cManagedObjects;           // raise global count and make a copy for the debug message below

    char sz[34];
    MakeManagedObjectRef(sz, websession._uWebsessionID, _id);
    _strID = sz;

    websession._pp->_mapManagedObjectsById[_id] = this;
    websession._pp->_mapManagedObjectsByPtr[_ulp] = this;

    websession.touch();

    WEBDEBUG(("   * %s: MOR created for %s*=%#p (IUnknown*=%#p; COM refcount now %RI32/%RI32), new ID is %#llx; now %lld objects total\n",
              __FUNCTION__,
              pcszInterface,
              pobjInterface,
              pobjUnknown,
              cRefs1,
              cRefs2,
              _id,
              cTotal));
}

/**
 * Destructor; removes the instance from the global hash of
 * managed objects. Calls Release() on the contained COM object.
 *
 * Preconditions: Caller must have locked g_pWebsessionsLockHandle.
 */
ManagedObjectRef::~ManagedObjectRef()
{
    Assert(g_pWebsessionsLockHandle->isWriteLockOnCurrentThread());
    ULONG64 cTotal = --g_cManagedObjects;

    Assert(_pobjUnknown);
    Assert(_pobjInterface);

    // we called AddRef() on both interfaces, so call Release() on
    // both as well, but in reverse order
    uint32_t cRefs2 = ((IUnknown*)_pobjInterface)->Release();
    uint32_t cRefs1 = _pobjUnknown->Release();
    WEBDEBUG(("   * %s: deleting MOR for ID %#llx (%s; COM refcount now %RI32/%RI32); now %lld objects total\n", __FUNCTION__, _id, _pcszInterface, cRefs1, cRefs2, cTotal));

    // if we're being destroyed from the websession's destructor,
    // then that destructor is iterating over the maps, so
    // don't remove us there! (data integrity + speed)
    if (!_websession._fDestructing)
    {
        WEBDEBUG(("   * %s: removing from websession maps\n", __FUNCTION__));
        _websession._pp->_mapManagedObjectsById.erase(_id);
        if (_websession._pp->_mapManagedObjectsByPtr.erase(_ulp) != 1)
            WEBDEBUG(("   WARNING: could not find %#llx in _mapManagedObjectsByPtr\n", _ulp));
    }
}

/**
 * Static helper method for findObjectFromRef() template that actually
 * looks up the object from a given integer ID.
 *
 * This has been extracted into this non-template function to reduce
 * code bloat as we have the actual STL map lookup only in this function.
 *
 * This also "touches" the timestamp in the websession whose ID is encoded
 * in the given integer ID, in order to prevent the websession from timing
 * out.
 *
 * Preconditions: Caller must have locked g_pWebsessionsLockHandle.
 *
 * @param   id
 * @param   pRef
 * @param   fNullAllowed
 * @return
 */
int ManagedObjectRef::findRefFromId(const WSDLT_ID &id,
                                    ManagedObjectRef **pRef,
                                    bool fNullAllowed)
{
    int vrc = VINF_SUCCESS;

    do
    {
        // allow NULL (== empty string) input reference, which should return a NULL pointer
        if (!id.length() && fNullAllowed)
        {
            *pRef = NULL;
            return 0;
        }

        uint64_t websessId;
        uint64_t objId;
        WEBDEBUG(("   %s(): looking up objref %s\n", __FUNCTION__, id.c_str()));
        if (!SplitManagedObjectRef(id,
                                   &websessId,
                                   &objId))
        {
            vrc = VERR_WEB_INVALID_MANAGED_OBJECT_REFERENCE;
            break;
        }

        WebsessionsMapIterator it = g_mapWebsessions.find(websessId);
        if (it == g_mapWebsessions.end())
        {
            WEBDEBUG(("   %s: cannot find websession for objref %s\n", __FUNCTION__, id.c_str()));
            vrc = VERR_WEB_INVALID_SESSION_ID;
            break;
        }

        WebServiceSession *pWebsession = it->second;
        // "touch" websession to prevent it from timing out
        pWebsession->touch();

        ManagedObjectsIteratorById iter = pWebsession->_pp->_mapManagedObjectsById.find(objId);
        if (iter == pWebsession->_pp->_mapManagedObjectsById.end())
        {
            WEBDEBUG(("   %s: cannot find comobj for objref %s\n", __FUNCTION__, id.c_str()));
            vrc = VERR_WEB_INVALID_OBJECT_ID;
            break;
        }

        *pRef = iter->second;

    } while (0);

    return vrc;
}

/****************************************************************************
 *
 * interface IManagedObjectRef
 *
 ****************************************************************************/

/**
 * This is the hard-coded implementation for the IManagedObjectRef::getInterfaceName()
 * that our WSDL promises to our web service clients. This method returns a
 * string describing the interface that this managed object reference
 * supports, e.g. "IMachine".
 *
 * @param soap
 * @param req
 * @param resp
 * @return
 */
int __vbox__IManagedObjectRef_USCOREgetInterfaceName(
    struct soap *soap,
    _vbox__IManagedObjectRef_USCOREgetInterfaceName *req,
    _vbox__IManagedObjectRef_USCOREgetInterfaceNameResponse *resp)
{
    RT_NOREF(soap);
    HRESULT hrc = S_OK; /** @todo r=bird: hrc is not set.... */
    WEBDEBUG(("-- entering %s\n", __FUNCTION__));

    do
    {
        // findRefFromId require the lock
        util::AutoWriteLock lock(g_pWebsessionsLockHandle COMMA_LOCKVAL_SRC_POS);

        ManagedObjectRef *pRef;
        if (!ManagedObjectRef::findRefFromId(req->_USCOREthis, &pRef, false))
            resp->returnval = pRef->getInterfaceName();

    } while (0);

    WEBDEBUG(("-- leaving %s, hrc: %#lx\n", __FUNCTION__, hrc));
    if (FAILED(hrc))
        return SOAP_FAULT;
    return SOAP_OK;
}

/**
 * This is the hard-coded implementation for the IManagedObjectRef::release()
 * that our WSDL promises to our web service clients. This method releases
 * a managed object reference and removes it from our stacks.
 *
 * @param soap
 * @param req
 * @param resp
 * @return
 */
int __vbox__IManagedObjectRef_USCORErelease(
    struct soap *soap,
    _vbox__IManagedObjectRef_USCORErelease *req,
    _vbox__IManagedObjectRef_USCOREreleaseResponse *resp)
{
    RT_NOREF(resp);
    HRESULT hrc;
    WEBDEBUG(("-- entering %s\n", __FUNCTION__));

    {
        // findRefFromId and the delete call below require the lock
        util::AutoWriteLock lock(g_pWebsessionsLockHandle COMMA_LOCKVAL_SRC_POS);

        ManagedObjectRef *pRef;
        hrc = ManagedObjectRef::findRefFromId(req->_USCOREthis, &pRef, false);
        if (hrc == S_OK)
        {
            WEBDEBUG(("   found reference; deleting!\n"));
            // this removes the object from all stacks; since
            // there's a ComPtr<> hidden inside the reference,
            // this should also invoke Release() on the COM
            // object
            delete pRef;
        }
        else
            RaiseSoapInvalidObjectFault(soap, req->_USCOREthis);
    }

    WEBDEBUG(("-- leaving %s, hrc: %#lx\n", __FUNCTION__, hrc));
    if (FAILED(hrc))
        return SOAP_FAULT;
    return SOAP_OK;
}

/****************************************************************************
 *
 * interface IWebsessionManager
 *
 ****************************************************************************/

/**
 * Hard-coded implementation for IWebsessionManager::logon. As opposed to the underlying
 * COM API, this is the first method that a webservice client must call before the
 * webservice will do anything useful.
 *
 * This returns a managed object reference to the global IVirtualBox object; into this
 * reference a websession ID is encoded which remains constant with all managed object
 * references returned by other methods.
 *
 * When the webservice client is done, it should call IWebsessionManager::logoff. This
 * will clean up internally (destroy all remaining managed object references and
 * related COM objects used internally).
 *
 * After logon, an internal timeout ensures that if the webservice client does not
 * call any methods, after a configurable number of seconds, the webservice will log
 * off the client automatically. This is to ensure that the webservice does not
 * drown in managed object references and eventually deny service. Still, it is
 * a much better solution, both for performance and cleanliness, for the webservice
 * client to clean up itself.
 *
 * @param soap
 * @param req
 * @param resp
 * @return
 */
int __vbox__IWebsessionManager_USCORElogon(
        struct soap *soap,
        _vbox__IWebsessionManager_USCORElogon *req,
        _vbox__IWebsessionManager_USCORElogonResponse *resp)
{
    RT_NOREF(soap);
    HRESULT hrc = S_OK;
    WEBDEBUG(("-- entering %s\n", __FUNCTION__));

    do
    {
        // WebServiceSession constructor tinkers with global MOR map and requires a write lock
        util::AutoWriteLock lock(g_pWebsessionsLockHandle COMMA_LOCKVAL_SRC_POS);

        // create new websession; the constructor stores the new websession
        // in the global map automatically
        WebServiceSession *pWebsession = new WebServiceSession();
        ComPtr<IVirtualBox> pVirtualBox;

        // authenticate the user
        if (!(pWebsession->authenticate(req->username.c_str(),
                                        req->password.c_str(),
                                        pVirtualBox.asOutParam())))
        {
            // fake up a "root" MOR for this websession
            char sz[34];
            MakeManagedObjectRef(sz, pWebsession->getID(), 0ULL);
            WSDLT_ID id = sz;

            // in the new websession, create a managed object reference (MOR) for the
            // global VirtualBox object; this encodes the websession ID in the MOR so
            // that it will be implicitly be included in all future requests of this
            // webservice client
            resp->returnval = createOrFindRefFromComPtr(id, g_pcszIVirtualBox, pVirtualBox);
            WEBDEBUG(("VirtualBox object ref is %s\n", resp->returnval.c_str()));
        }
        else
            hrc = E_FAIL;
    } while (0);

    WEBDEBUG(("-- leaving %s, hrc: %#lx\n", __FUNCTION__, hrc));
    if (FAILED(hrc))
        return SOAP_FAULT;
    return SOAP_OK;
}

/**
 * Returns a new ISession object every time.
 *
 * No longer connected in any way to logons, one websession can easily
 * handle multiple sessions.
 */
int __vbox__IWebsessionManager_USCOREgetSessionObject(
        struct soap*,
        _vbox__IWebsessionManager_USCOREgetSessionObject *req,
        _vbox__IWebsessionManager_USCOREgetSessionObjectResponse *resp)
{
    HRESULT hrc = S_OK;
    WEBDEBUG(("-- entering %s\n", __FUNCTION__));

    do
    {
        // create a new ISession object
        ComPtr<ISession> pSession;
        hrc = g_pVirtualBoxClient->COMGETTER(Session)(pSession.asOutParam());
        if (FAILED(hrc))
        {
            WEBDEBUG(("ERROR: cannot create session object!"));
            break;
        }

        // return its MOR
        resp->returnval = createOrFindRefFromComPtr(req->refIVirtualBox, g_pcszISession, pSession);
        WEBDEBUG(("Session object ref is %s\n", resp->returnval.c_str()));
    } while (0);

    WEBDEBUG(("-- leaving %s, hrc: %#lx\n", __FUNCTION__, hrc));
    if (FAILED(hrc))
        return SOAP_FAULT;
    return SOAP_OK;
}

/**
 * hard-coded implementation for IWebsessionManager::logoff.
 *
 * @param req
 * @param resp
 * @return
 */
int __vbox__IWebsessionManager_USCORElogoff(
        struct soap*,
        _vbox__IWebsessionManager_USCORElogoff *req,
        _vbox__IWebsessionManager_USCORElogoffResponse *resp)
{
    RT_NOREF(resp);
    HRESULT hrc = S_OK;
    WEBDEBUG(("-- entering %s\n", __FUNCTION__));

    {
        // findWebsessionFromRef and the websession destructor require the lock
        util::AutoWriteLock lock(g_pWebsessionsLockHandle COMMA_LOCKVAL_SRC_POS);

        WebServiceSession *pWebsession = WebServiceSession::findWebsessionFromRef(req->refIVirtualBox);
        if (pWebsession)
        {
            WEBDEBUG(("websession logoff, deleting websession %#llx\n", pWebsession->getID()));
            delete pWebsession;
                // destructor cleans up

            WEBDEBUG(("websession destroyed, %d websessions left open\n", g_mapWebsessions.size()));
        }
    }

    WEBDEBUG(("-- leaving %s, hrc: %#lx\n", __FUNCTION__, hrc));
    if (FAILED(hrc))
        return SOAP_FAULT;
    return SOAP_OK;
}
