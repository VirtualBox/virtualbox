/* $Id: HostDnsServiceLinux.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Linux specific DNS information fetching.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_MAIN_HOST
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/initterm.h>
#include <iprt/file.h>
#include <VBox/log.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>

#include <linux/limits.h>

/* Workaround for <sys/cdef.h> defining __flexarr to [] which beats us in
 * struct inotify_event (char name __flexarr). */
#include <sys/cdefs.h>
#undef __flexarr
#define __flexarr [RT_FLEXIBLE_ARRAY]
#include <sys/inotify.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "../HostDnsService.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static const char g_szEtcFolder[]          = "/etc";
static const char g_szResolvConfPath[]     = "/etc/resolv.conf";
static const char g_szResolvConfFilename[] = "resolv.conf";


HostDnsServiceLinux::~HostDnsServiceLinux()
{
    if (m_fdShutdown >= 0)
    {
        close(m_fdShutdown);
        m_fdShutdown = -1;
    }
}

HRESULT HostDnsServiceLinux::init(HostDnsMonitorProxy *pProxy)
{
    return HostDnsServiceResolvConf::init(pProxy, "/etc/resolv.conf");
}

int HostDnsServiceLinux::monitorThreadShutdown(RTMSINTERVAL uTimeoutMs)
{
    RT_NOREF(uTimeoutMs);

    if (m_fdShutdown >= 0)
        send(m_fdShutdown, "", 1, MSG_NOSIGNAL);

    return VINF_SUCCESS;
}

/**
 * Format the notifcation event mask into a buffer for logging purposes.
 */
static const char *InotifyMaskToStr(char *psz, size_t cb, uint32_t fMask)
{
    static struct { const char *pszName; uint32_t cchName, fFlag; } const s_aFlags[] =
    {
# define ENTRY(fFlag)   { #fFlag, sizeof(#fFlag) - 1, fFlag }
        ENTRY(IN_ACCESS),
        ENTRY(IN_MODIFY),
        ENTRY(IN_ATTRIB),
        ENTRY(IN_CLOSE_WRITE),
        ENTRY(IN_CLOSE_NOWRITE),
        ENTRY(IN_OPEN),
        ENTRY(IN_MOVED_FROM),
        ENTRY(IN_MOVED_TO),
        ENTRY(IN_CREATE),
        ENTRY(IN_DELETE),
        ENTRY(IN_DELETE_SELF),
        ENTRY(IN_MOVE_SELF),
        ENTRY(IN_Q_OVERFLOW),
        ENTRY(IN_IGNORED),
        ENTRY(IN_UNMOUNT),
        ENTRY(IN_ISDIR),
    };
    size_t offDst = 0;
    for (size_t i = 0; i < RT_ELEMENTS(s_aFlags); i++)
        if (fMask & s_aFlags[i].fFlag)
        {
            if (offDst && offDst < cb)
                psz[offDst++] = ' ';
            if (offDst < cb)
            {
                size_t cbToCopy = RT_MIN(s_aFlags[i].cchName, cb - offDst);
                memcpy(&psz[offDst], s_aFlags[i].pszName, cbToCopy);
                offDst += cbToCopy;
            }

            fMask &= ~s_aFlags[i].fFlag;
            if (!fMask)
                break;
        }
    if (fMask && offDst < cb)
        RTStrPrintf(&psz[offDst], cb - offDst, offDst ? " %#x" : "%#x", fMask);
    else
        psz[RT_MIN(offDst, cb - 1)] = '\0';
    return psz;
}

/**
 * Helper for HostDnsServiceLinux::monitorThreadProc.
 */
static int monitorSymlinkedDir(int iInotifyFd, char szRealResolvConf[PATH_MAX], size_t *poffFilename)
{
    RT_BZERO(szRealResolvConf, PATH_MAX);

    /* Check that it's a symlink first. */
    struct stat st;
    if (   lstat(g_szResolvConfPath, &st) >= 0
        && S_ISLNK(st.st_mode))
    {
        /* If realpath fails, the file must've been deleted while we were busy: */
        if (   realpath(g_szResolvConfPath, szRealResolvConf)
            && strchr(szRealResolvConf, '/'))
        {
            /* Cut of the filename part. We only need that for deletion checks and such. */
            size_t const offFilename = strrchr(szRealResolvConf, '/') - &szRealResolvConf[0];
            *poffFilename = offFilename + 1;
            szRealResolvConf[offFilename] = '\0';

            /* Try set up directory monitoring. (File monitoring is done via the symlink.) */
            return inotify_add_watch(iInotifyFd, szRealResolvConf, IN_MOVE | IN_CREATE | IN_DELETE);
        }
    }

    *poffFilename = 0;
    szRealResolvConf[0] = '\0';
    return -1;
}

/** @todo If this code is needed elsewhere, we should abstract it into an IPRT
 *        thingy that monitors a file (path) for changes.  This code is a little
 *        bit too complex to be duplicated. */
int HostDnsServiceLinux::monitorThreadProc(void)
{
    /*
     * Create a socket pair for signalling shutdown (see monitorThreadShutdown).
     * ASSUME Linux 2.6.27 or later and that we can use SOCK_CLOEXEC.
     */
    int aiStopPair[2];
    int iRc = socketpair(AF_LOCAL, SOCK_DGRAM | SOCK_CLOEXEC, 0, aiStopPair);
    int iErr = errno;
    AssertLogRelMsgReturn(iRc == 0, ("socketpair: failed (%d: %s)\n", iErr, strerror(iErr)), RTErrConvertFromErrno(iErr));

    m_fdShutdown = aiStopPair[0];

    onMonitorThreadInitDone();

    /*
     * inotify initialization (using inotify_init1 w/ IN_CLOEXEC introduced
     * in 2.6.27 shouldn't be a problem any more).
     *
     * Note! Ignoring failures here is safe, because poll will ignore entires
     *       with negative fd values.
     */
    int const iNotifyFd = inotify_init1(IN_CLOEXEC);
    if (iNotifyFd < 0)
        LogRel(("HostDnsServiceLinux::monitorThreadProc: Warning! inotify_init failed (errno=%d)\n", errno));

    /* Monitor the /etc directory so we can detect moves, creating and unlinking
       involving /etc/resolv.conf:  */
    int const iWdDir = inotify_add_watch(iNotifyFd, g_szEtcFolder, IN_MOVE | IN_CREATE | IN_DELETE);

    /* In case g_szResolvConfPath is a symbolic link, monitor the target directory
       too for changes to what it links to (kept up to date via iWdDir). */
    char   szRealResolvConf[PATH_MAX];
    size_t offRealResolvConfName = 0;
    int iWdSymDir = ::monitorSymlinkedDir(iNotifyFd, szRealResolvConf, &offRealResolvConfName);

    /* Monitor the resolv.conf itself if it exists, following all symlinks. */
    int iWdFile = inotify_add_watch(iNotifyFd, g_szResolvConfPath, IN_CLOSE_WRITE | IN_DELETE_SELF);

    LogRel5(("HostDnsServiceLinux::monitorThreadProc: inotify: %d - iWdDir=%d iWdSymDir=%d iWdFile=%d\n",
             iNotifyFd, iWdDir, iWdSymDir, iWdFile));

    /*
     * poll initialization:
     */
    pollfd aFdPolls[2];
    RT_ZERO(aFdPolls);

    aFdPolls[0].fd = iNotifyFd;
    aFdPolls[0].events = POLLIN;

    aFdPolls[1].fd = aiStopPair[1];
    aFdPolls[1].events = POLLIN;

    /*
     * The monitoring loop.
     */
    int vrcRet = VINF_SUCCESS;
    for (;;)
    {
        /*
         * Wait for something to happen.
         */
        iRc = poll(aFdPolls, RT_ELEMENTS(aFdPolls), -1 /*infinite timeout*/);
        if (iRc == -1)
        {
            if (errno != EINTR)
            {
                LogRelMax(32, ("HostDnsServiceLinux::monitorThreadProc: poll failed %d: errno=%d\n", iRc, errno));
                RTThreadSleep(1);
            }
            continue;
        }
        Log5Func(("poll returns %d: [0]=%#x [1]=%#x\n", iRc, aFdPolls[1].revents, aFdPolls[0].revents));

        AssertMsgBreakStmt(   (aFdPolls[0].revents & (POLLERR | POLLNVAL)) == 0 /* (ok for fd=-1 too, revents=0 then) */
                           && (aFdPolls[1].revents & (POLLERR | POLLNVAL)) == 0,
                              ("Debug Me: [0]=%d,%#x [1]=%d, %#x\n",
                               aFdPolls[0].fd, aFdPolls[0].revents, aFdPolls[0].fd, aFdPolls[1].revents),
                           vrcRet = VERR_INTERNAL_ERROR);

        /*
         * Check for shutdown first.
         */
        if (aFdPolls[1].revents & POLLIN)
            break; /** @todo should probably drain aiStopPair[1] here if we're really paranoid.
                    * we'll be closing our end of the socket/pipe, so any stuck write
                    * should return too (ECONNRESET, ENOTCONN or EPIPE). */

        if (aFdPolls[0].revents & POLLIN)
        {
            /*
             * Read the notification event.
             */
#define INOTIFY_EVENT_SIZE  (RT_UOFFSETOF(struct inotify_event, name))
            union
            {
                uint8_t     abBuf[(INOTIFY_EVENT_SIZE * 2 - 1 + NAME_MAX) / INOTIFY_EVENT_SIZE * INOTIFY_EVENT_SIZE * 4];
                uint64_t    uAlignTrick[2];
            } uEvtBuf;

            ssize_t cbEvents = read(iNotifyFd, &uEvtBuf, sizeof(uEvtBuf));
            Log5Func(("read(inotify) -> %zd\n", cbEvents));
            if (cbEvents > 0)
                Log5(("%.*Rhxd\n", cbEvents, &uEvtBuf));

            /*
             * Process the events.
             *
             * We'll keep the old watch descriptor number till after we're done
             * parsing this block of events.  Even so, the removal of watches
             * isn't race free, as they'll get automatically removed when what
             * is being watched is unliked.
             */
            int                         iWdFileNew   = iWdFile;
            int                         iWdSymDirNew = iWdSymDir;
            bool                        fTryReRead   = false;
            struct inotify_event const *pCurEvt      = (struct inotify_event const *)&uEvtBuf;
            while (cbEvents >= (ssize_t)INOTIFY_EVENT_SIZE)
            {
                char szTmp[64];
                if (pCurEvt->len == 0)
                    LogRel5(("HostDnsServiceLinux::monitorThreadProc: event: wd=%#x mask=%#x (%s) cookie=%#x\n",
                             pCurEvt->wd, pCurEvt->mask, InotifyMaskToStr(szTmp, sizeof(szTmp), pCurEvt->mask), pCurEvt->cookie));
                else
                    LogRel5(("HostDnsServiceLinux::monitorThreadProc: event: wd=%#x mask=%#x (%s) cookie=%#x len=%#x '%s'\n",
                              pCurEvt->wd, pCurEvt->mask, InotifyMaskToStr(szTmp, sizeof(szTmp), pCurEvt->mask),
                              pCurEvt->cookie, pCurEvt->len, pCurEvt->name));

                /*
                 * The file itself (symlinks followed, remember):
                 */
                if (pCurEvt->wd == iWdFile)
                {
                    if (pCurEvt->mask & IN_CLOSE_WRITE)
                    {
                        Log5Func(("file: close-after-write => trigger re-read\n"));
                        fTryReRead = true;
                    }
                    else if (pCurEvt->mask & IN_DELETE_SELF)
                    {
                        Log5Func(("file: deleted self\n"));
                        if (iWdFileNew != -1)
                        {
                            iRc = inotify_rm_watch(iNotifyFd, iWdFileNew);
                            AssertMsg(iRc >= 0, ("%d/%d\n", iRc, errno));
                            iWdFileNew = -1;
                        }
                    }
                    else if (pCurEvt->mask & IN_IGNORED)
                        iWdFileNew = -1; /* file deleted */
                    else
                        AssertMsgFailed(("file: mask=%#x\n", pCurEvt->mask));
                }
                /*
                 * The /etc directory
                 *
                 * We only care about events relating to the creation, deletion and
                 * renaming of 'resolv.conf'.  We'll restablish both the direct file
                 * watching and the watching of any symlinked directory on all of
                 * these events, although for the former we'll delay the re-starting
                 * of the watching till all events have been processed.
                 */
                else if (pCurEvt->wd == iWdDir)
                {
                    if (   pCurEvt->len > 0
                        && strcmp(g_szResolvConfFilename, pCurEvt->name) == 0)
                    {
                        if (pCurEvt->mask & (IN_MOVE | IN_CREATE | IN_DELETE))
                        {
                            if (iWdFileNew >= 0)
                            {
                                iRc = inotify_rm_watch(iNotifyFd, iWdFileNew);
                                Log5Func(("dir: moved / created / deleted: dropped file watch (%d - iRc=%d/err=%d)\n",
                                          iWdFileNew, iRc, errno));
                                iWdFileNew = -1;
                            }
                            if (iWdSymDirNew >= 0)
                            {
                                iRc = inotify_rm_watch(iNotifyFd, iWdSymDirNew);
                                Log5Func(("dir: moved / created / deleted: dropped symlinked dir watch (%d - %s/%s - iRc=%d/err=%d)\n",
                                          iWdSymDirNew, szRealResolvConf, &szRealResolvConf[offRealResolvConfName], iRc, errno));
                                iWdSymDirNew = -1;
                                offRealResolvConfName = 0;
                            }
                            if (pCurEvt->mask & (IN_MOVED_TO | IN_CREATE))
                            {
                                Log5Func(("dir: moved_to / created: trigger re-read\n"));
                                fTryReRead = true;

                                iWdSymDirNew = ::monitorSymlinkedDir(iNotifyFd, szRealResolvConf, &offRealResolvConfName);
                                if (iWdSymDirNew < 0)
                                    Log5Func(("dir: moved_to / created: re-stablished symlinked-directory monitoring: iWdSymDir=%d (%s/%s)\n",
                                              iWdSymDirNew, szRealResolvConf, &szRealResolvConf[offRealResolvConfName]));
                            }
                        }
                        else
                            AssertMsgFailed(("dir: %#x\n", pCurEvt->mask));
                    }
                }
                /*
                 * The directory of a symlinked resolv.conf.
                 *
                 * Where we only care when the symlink target is created, moved_to,
                 * deleted or moved_from - i.e. a minimal version of the /etc event
                 * processing above.
                 *
                 * Note! Since we re-statablish monitoring above, szRealResolvConf
                 *       might not match the event we're processing.  Fortunately,
                 *       this shouldn't be important except for debug logging.
                 */
                else if (pCurEvt->wd == iWdSymDir)
                {
                    if (   pCurEvt->len > 0
                        && offRealResolvConfName > 0
                        && strcmp(&szRealResolvConf[offRealResolvConfName], pCurEvt->name) == 0)
                    {
                        if (iWdFileNew >= 0)
                        {
                            iRc = inotify_rm_watch(iNotifyFd, iWdFileNew);
                            Log5Func(("symdir: moved / created / deleted: drop file watch (%d - iRc=%d/err=%d)\n",
                                      iWdFileNew, iRc, errno));
                            iWdFileNew = -1;
                        }
                        if (pCurEvt->mask & (IN_MOVED_TO | IN_CREATE))
                        {
                            Log5Func(("symdir: moved_to / created: trigger re-read\n"));
                            fTryReRead = true;
                        }
                    }
                }
                /* We can get here it seems if our inotify_rm_watch calls above takes
                   place after new events relating to the two descriptors happens. */
                else
                    Log5Func(("Unknown (obsoleted) wd value: %d (mask=%#x cookie=%#x len=%#x)\n",
                              pCurEvt->wd, pCurEvt->mask, pCurEvt->cookie, pCurEvt->len));

                /* advance to the next event */
                Assert(pCurEvt->len / INOTIFY_EVENT_SIZE * INOTIFY_EVENT_SIZE == pCurEvt->len);
                size_t const cbCurEvt = INOTIFY_EVENT_SIZE + pCurEvt->len;
                pCurEvt   = (struct inotify_event const *)((uintptr_t)pCurEvt + cbCurEvt);
                cbEvents -= cbCurEvt;
            }

            /*
             * Commit the new watch descriptor numbers now that we're
             * done processing event using the old ones.
             */
            iWdFile   = iWdFileNew;
            iWdSymDir = iWdSymDirNew;

            /*
             * If the resolv.conf watch descriptor is -1, try restablish it here.
             */
            if (iWdFile == -1)
            {
                iWdFile = inotify_add_watch(iNotifyFd, g_szResolvConfPath, IN_CLOSE_WRITE | IN_DELETE_SELF);
                if (iWdFile >= 0)
                {
                    Log5Func(("Re-established file watcher: iWdFile=%d\n", iWdFile));
                    fTryReRead = true;
                }
            }

            /*
             * If any of the events indicate that we should re-read the file, we
             * do so now.  Should reduce number of unnecessary re-reads.
             */
            if (fTryReRead)
            {
                Log5Func(("Calling readResolvConf()...\n"));
                try
                {
                    readResolvConf();
                }
                catch (...)
                {
                    LogRel(("HostDnsServiceLinux::monitorThreadProc: readResolvConf threw exception!\n"));
                }
            }
        }
    }

    /*
     * Close file descriptors.
     */
    if (aiStopPair[0] == m_fdShutdown) /* paranoia */
    {
        m_fdShutdown = -1;
        close(aiStopPair[0]);
    }
    close(aiStopPair[1]);
    close(iNotifyFd);
    LogRel5(("HostDnsServiceLinux::monitorThreadProc: returns %Rrc\n", vrcRet));
    return vrcRet;
}

