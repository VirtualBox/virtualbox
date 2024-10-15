/* $Id: VBoxBugReport.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxBugReport - VirtualBox command-line diagnostics tool, internal header file.
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

#ifndef VBOX_INCLUDED_SRC_VBoxBugReport_VBoxBugReport_h
#define VBOX_INCLUDED_SRC_VBoxBugReport_VBoxBugReport_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/*
 * Introduction.
 *
 * In the most general sense a bug report is a collection of data obtained from
 * the user's host system. It may include files common for all VMs, like the
 * VBoxSVC.log file, as well as files related to particular machines. It may
 * also contain the output of commands executed on the host, as well as data
 * collected via OS APIs.
 */

/** @todo not sure if using a separate namespace would be beneficial */

#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/vfs.h>
#include <iprt/zip.h>
#include <iprt/cpp/list.h>

#ifdef RT_OS_WINDOWS
#define VBOXMANAGE "VBoxManage.exe"
#else /* !RT_OS_WINDOWS */
#define VBOXMANAGE "VBoxManage"
#endif /* !RT_OS_WINDOWS */

/* Base */

DECL_INLINE_THROW(void) handleRtError(int rc, const char *pszMsgFmt, ...)
{
    if (RT_FAILURE(rc))
    {
        va_list va;
        va_start(va, pszMsgFmt);
        RTCString strMsg(pszMsgFmt, va);
        va_end(va);
        strMsg.appendPrintfNoThrow(". %Rrf\n", rc);
        throw RTCError(strMsg);
    }
}

DECL_INLINE_THROW(void) handleComError(HRESULT hr, const char *pszMsgFmt, ...)
{
    if (FAILED(hr))
    {
        va_list va;
        va_start(va, pszMsgFmt);
        RTCString strMsg(pszMsgFmt, va);
        va_end(va);
        strMsg.appendPrintfNoThrow(". (hr=0x%x %Rhrc)\n", hr, hr);
        throw RTCError(strMsg);
    }
}

/*
 * An auxiliary class to facilitate in-place path joins.
 */
class PathJoin
{
public:
    PathJoin(const char *folder, const char *file) { m_path = RTPathJoinA(folder, file); }
    ~PathJoin() { RTStrFree(m_path); };
    operator char*() const { return m_path; };
private:
    char *m_path;
};


/*
 * An abstract class serving as the root of the bug report filter tree.
 * A child provides an implementation of the 'apply' method. A child
 * should modify the input buffer (provided via pvSource) in place, or
 * allocate a new buffer via 'allocateBuffer'. Allocated buffers are
 * released automatically when another buffer is allocated, which means
 * that NEXT CALL TO 'APPLY' INVALIDATES BUFFERS RETURNED IN PREVIOUS
 * CALLS!
 */
class BugReportFilter
{
public:
    BugReportFilter();
    virtual ~BugReportFilter();
    virtual void *apply(void *pvSource, size_t *pcbInOut) = 0;
protected:
    void *allocateBuffer(size_t cbNeeded);
private:
    void *m_pvBuffer;
    size_t m_cbBuffer;
};


/*
 * An abstract class serving as the root of the bug report item tree.
 */
class BugReportItem
{
public:
    BugReportItem(const char *pszTitle);
    virtual ~BugReportItem();
    virtual const char *getTitle(void);
    virtual RTVFSIOSTREAM getStream(void) = 0;
    void addFilter(BugReportFilter *filter);
    void *applyFilter(void *pvSource, size_t *pcbInOut);
private:
    char *m_pszTitle;
    BugReportFilter *m_filter;
};

/*
 * An abstract class to serve as a base class for all report types.
 */
class BugReport
{
public:
    BugReport(const char *pszFileName);
    virtual ~BugReport();

    void addItem(BugReportItem* item, BugReportFilter *filter = 0);
    int  getItemCount(void);
    void process();
    void *applyFilters(BugReportItem* item, void *pvSource, size_t *pcbInOut);

    virtual void processItem(BugReportItem* item) = 0;
    virtual void complete(void) = 0;

protected:
    char *m_pszFileName;
    RTCList<BugReportItem*> m_Items;
};

/*
 * An auxiliary class providing formatted output into a temporary file for item
 * classes that obtain data via host OS APIs.
 */
class BugReportStream : public BugReportItem
{
public:
    BugReportStream(const char *pszTitle);
    virtual ~BugReportStream();
    virtual RTVFSIOSTREAM getStream(void);
protected:
    int printf(const char *pszFmt, ...);
    int putStr(const char *pszString);
private:
    RTVFSIOSTREAM m_hVfsIos;
    char m_szFileName[RTPATH_MAX];
};


/* Generic */

/*
 * This class reports everything into a single text file.
 */
class BugReportText : public BugReport
{
public:
    BugReportText(const char *pszFileName);
    virtual ~BugReportText();
    virtual void processItem(BugReportItem* item);
    virtual void complete(void) {};
private:
    PRTSTREAM m_StrmTxt;
};

/*
 * This class reports items as individual files archived into a single compressed TAR file.
 */
class BugReportTarGzip : public BugReport
{
public:
    BugReportTarGzip(const char *pszFileName);
    virtual ~BugReportTarGzip();
    virtual void processItem(BugReportItem* item);
    virtual void complete(void);
private:
    void dumpExceptionToArchive(RTCString &strTarFile, RTCError &e);

    /*
     * Helper class to release handles going out of scope.
     */
    class VfsIoStreamHandle
    {
    public:
        VfsIoStreamHandle() : m_hVfsStream(NIL_RTVFSIOSTREAM) {};
        ~VfsIoStreamHandle() { release(); }
        PRTVFSIOSTREAM getPtr(void) { return &m_hVfsStream; };
        RTVFSIOSTREAM get(void) { return m_hVfsStream; };
        void release(void)
        {
            if (m_hVfsStream != NIL_RTVFSIOSTREAM)
                RTVfsIoStrmRelease(m_hVfsStream);
            m_hVfsStream = NIL_RTVFSIOSTREAM;
        };
    private:
        RTVFSIOSTREAM m_hVfsStream;
    };

    VfsIoStreamHandle m_hVfsGzip;

    RTVFSFSSTREAM m_hTarFss;
    char m_szTarName[RTPATH_MAX];
};


/*
 * BugReportFile adds a file as an item to a report.
 */
class BugReportFile : public BugReportItem
{
public:
    BugReportFile(const char *pszPath, const char *pcszName);
    virtual ~BugReportFile();
    virtual RTVFSIOSTREAM getStream(void);

private:
    char *m_pszPath;
    RTVFSIOSTREAM m_hVfsIos;
};

/*
 * A base class for item classes that collect CLI output.
 */
class BugReportCommand : public BugReportItem
{
public:
    BugReportCommand(const char *pszTitle, const char *pszExec, ...);
    virtual ~BugReportCommand();
    virtual RTVFSIOSTREAM getStream(void);
private:
    RTVFSIOSTREAM m_hVfsIos;
    char m_szFileName[RTPATH_MAX];
    char *m_papszArgs[32];
};

/*
 * A base class for item classes that provide temp output file to a command.
 */
class BugReportCommandTemp : public BugReportItem
{
public:
    BugReportCommandTemp(const char *pszTitle, const char *pszExec, ...);
    virtual ~BugReportCommandTemp();
    virtual RTVFSIOSTREAM getStream(void);
private:
    RTVFSIOSTREAM m_hVfsIos;
    char m_szFileName[RTPATH_MAX];
    char m_szErrFileName[RTPATH_MAX];
    char *m_papszArgs[32];
};

/* Platform-specific */

void createBugReportOsSpecific(BugReport* report, const char *pszHome);

#endif /* !VBOX_INCLUDED_SRC_VBoxBugReport_VBoxBugReport_h */
