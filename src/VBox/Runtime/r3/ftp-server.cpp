/* $Id: ftp-server.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Generic FTP server (RFC 959) implementation.
 *
 * Partly also implements RFC 3659 (Extensions to FTP, for "SIZE", ++).
 *
 * Known limitations so far:
 * - UTF-8 support only.
 * - Only supports ASCII + binary (image type) file streams for now.
 * - No directory / file caching yet.
 * - No support for writing / modifying ("DELE", "MKD", "RMD", "STOR", ++).
 * - No FTPS / SFTP support.
 * - No passive mode ("PASV") support.
 * - No IPv6 support.
 * - No proxy support.
 * - No FXP support.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_FTP
#include <iprt/ftp.h>
#include "internal/iprt.h"
#include "internal/magics.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/circbuf.h>
#include <iprt/err.h>
#include <iprt/file.h> /* For file mode flags. */
#include <iprt/getopt.h>
#include <iprt/mem.h>
#include <iprt/log.h>
#include <iprt/path.h>
#include <iprt/poll.h>
#include <iprt/socket.h>
#include <iprt/sort.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/tcp.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Internal FTP server instance.
 */
typedef struct RTFTPSERVERINTERNAL
{
    /** Magic value. */
    uint32_t                u32Magic;
    /** Callback table. */
    RTFTPSERVERCALLBACKS    Callbacks;
    /** Pointer to TCP server instance. */
    PRTTCPSERVER            pTCPServer;
    /** Number of currently connected clients. */
    uint32_t                cClients;
    /** Pointer to user-specific data. Optional. */
    void                   *pvUser;
    /** Size of user-specific data. Optional. */
    size_t                  cbUser;
} RTFTPSERVERINTERNAL;
/** Pointer to an internal FTP server instance. */
typedef RTFTPSERVERINTERNAL *PRTFTPSERVERINTERNAL;

/**
 * FTP directory entry.
 */
typedef struct RTFTPDIRENTRY
{
    /** The information about the entry. */
    RTFSOBJINFO Info;
    /** Symbolic link target (allocated after the name). */
    const char *pszTarget;
    /** Owner if applicable (allocated after the name). */
    const char *pszOwner;
    /** Group if applicable (allocated after the name). */
    const char *pszGroup;
    /** The length of szName. */
    size_t      cchName;
    /** The entry name. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    char        szName[RT_FLEXIBLE_ARRAY];
} RTFTPDIRENTRY;
/** Pointer to a FTP directory entry. */
typedef RTFTPDIRENTRY *PRTFTPDIRENTRY;
/** Pointer to a FTP directory entry pointer. */
typedef PRTFTPDIRENTRY *PPRTFTPDIRENTRY;

/**
 * Collection of directory entries.
 * Used for also caching stuff.
 */
typedef struct RTFTPDIRCOLLECTION
{
    /** Current size of papEntries. */
    size_t                cEntries;
    /** Memory allocated for papEntries. */
    size_t                cEntriesAllocated;
    /** Current entries pending sorting and display. */
    PPRTFTPDIRENTRY       papEntries;

    /** Total number of bytes allocated for the above entries. */
    uint64_t              cbTotalAllocated;
    /** Total number of file content bytes.    */
    uint64_t              cbTotalFiles;

} RTFTPDIRCOLLECTION;
/** Pointer to a directory collection. */
typedef RTFTPDIRCOLLECTION *PRTFTPDIRCOLLECTION;
/** Pointer to a directory entry collection pointer. */
typedef PRTFTPDIRCOLLECTION *PPRTFTPDIRCOLLECTION;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Validates a handle and returns VERR_INVALID_HANDLE if not valid. */
#define RTFTPSERVER_VALID_RETURN_RC(hFTPServer, a_rc) \
    do { \
        AssertPtrReturn((hFTPServer), (a_rc)); \
        AssertReturn((hFTPServer)->u32Magic == RTFTPSERVER_MAGIC, (a_rc)); \
    } while (0)

/** Validates a handle and returns VERR_INVALID_HANDLE if not valid. */
#define RTFTPSERVER_VALID_RETURN(hFTPServer) RTFTPSERVER_VALID_RETURN_RC((hFTPServer), VERR_INVALID_HANDLE)

/** Validates a handle and returns (void) if not valid. */
#define RTFTPSERVER_VALID_RETURN_VOID(hFTPServer) \
    do { \
        AssertPtrReturnVoid(hFTPServer); \
        AssertReturnVoid((hFTPServer)->u32Magic == RTFTPSERVER_MAGIC); \
    } while (0)


/** Handles a FTP server callback with no arguments and returns. */
#define RTFTPSERVER_HANDLE_CALLBACK_RET(a_Name) \
    do \
    { \
        PRTFTPSERVERCALLBACKS pCallbacks = &pClient->pServer->Callbacks; \
        if (pCallbacks->a_Name) \
        { \
            RTFTPCALLBACKDATA Data = { &pClient->State }; \
            return pCallbacks->a_Name(&Data); \
        } \
        return VERR_NOT_IMPLEMENTED; \
    } while (0)

/** Handles a FTP server callback with no arguments and sets rc accordingly. */
#define RTFTPSERVER_HANDLE_CALLBACK(a_Name) \
    do \
    { \
        PRTFTPSERVERCALLBACKS pCallbacks = &pClient->pServer->Callbacks; \
        if (pCallbacks->a_Name) \
        { \
            RTFTPCALLBACKDATA Data = { &pClient->State, pClient->pServer->pvUser, pClient->pServer->cbUser }; \
            rc = pCallbacks->a_Name(&Data); \
        } \
        else \
            rc = VERR_NOT_IMPLEMENTED; \
    } while (0)

/** Handles a FTP server callback with arguments and sets rc accordingly. */
#define RTFTPSERVER_HANDLE_CALLBACK_VA(a_Name, ...) \
    do \
    { \
        PRTFTPSERVERCALLBACKS pCallbacks = &pClient->pServer->Callbacks; \
        if (pCallbacks->a_Name) \
        { \
            RTFTPCALLBACKDATA Data = { &pClient->State, pClient->pServer->pvUser, pClient->pServer->cbUser }; \
            rc = pCallbacks->a_Name(&Data, __VA_ARGS__); \
        } \
        else \
            rc = VERR_NOT_IMPLEMENTED; \
    } while (0)

/** Handles a FTP server callback with arguments and returns. */
#define RTFTPSERVER_HANDLE_CALLBACK_VA_RET(a_Name, ...) \
    do \
    { \
        PRTFTPSERVERCALLBACKS pCallbacks = &pClient->pServer->Callbacks; \
        if (pCallbacks->a_Name) \
        { \
            RTFTPCALLBACKDATA Data = { &pClient->State, pClient->pServer->pvUser, pClient->pServer->cbUser }; \
            return pCallbacks->a_Name(&Data, __VA_ARGS__); \
        } \
        return VERR_NOT_IMPLEMENTED; \
    } while (0)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Supported FTP server command IDs.
 *  Alphabetically, named after their official command names. */
typedef enum RTFTPSERVERCMD
{
    /** Invalid command, do not use. Always must come first. */
    RTFTPSERVERCMD_INVALID = 0,
    /** Aborts the current command on the server. */
    RTFTPSERVERCMD_ABOR,
    /** Changes the current working directory. */
    RTFTPSERVERCMD_CDUP,
    /** Changes the current working directory. */
    RTFTPSERVERCMD_CWD,
    /** Reports features supported by the server. */
    RTFTPSERVERCMD_FEAT,
    /** Lists a directory. */
    RTFTPSERVERCMD_LIST,
    /** Sets the transfer mode. */
    RTFTPSERVERCMD_MODE,
    /** Sends a nop ("no operation") to the server. */
    RTFTPSERVERCMD_NOOP,
    /** Sets the password for authentication. */
    RTFTPSERVERCMD_PASS,
    /** Sets the port to use for the data connection. */
    RTFTPSERVERCMD_PORT,
    /** Gets the current working directory. */
    RTFTPSERVERCMD_PWD,
    /** Get options. Needed in conjunction with the FEAT command. */
    RTFTPSERVERCMD_OPTS,
    /** Terminates the session (connection). */
    RTFTPSERVERCMD_QUIT,
    /** Retrieves a specific file. */
    RTFTPSERVERCMD_RETR,
    /** Retrieves the size of a file. */
    RTFTPSERVERCMD_SIZE,
    /** Retrieves the current status of a transfer. */
    RTFTPSERVERCMD_STAT,
    /** Sets the structure type to use. */
    RTFTPSERVERCMD_STRU,
    /** Gets the server's OS info. */
    RTFTPSERVERCMD_SYST,
    /** Sets the (data) representation type. */
    RTFTPSERVERCMD_TYPE,
    /** Sets the user name for authentication. */
    RTFTPSERVERCMD_USER,
    /** End marker. */
    RTFTPSERVERCMD_END,
    /** The usual 32-bit hack. */
    RTFTPSERVERCMD_32BIT_HACK = 0x7fffffff
} RTFTPSERVERCMD;

struct RTFTPSERVERCLIENT;

/**
 * Structure for maintaining a single data connection.
 */
typedef struct RTFTPSERVERDATACONN
{
    /** Pointer to associated client of this data connection. */
    RTFTPSERVERCLIENT          *pClient;
    /** Data connection IP. */
    RTNETADDRIPV4               Addr;
    /** Data connection port number. */
    uint16_t                    uPort;
    /** The current data socket to use.
     *  Can be NIL_RTSOCKET if no data port has been specified (yet) or has been closed. */
    RTSOCKET                    hSocket;
    /** Thread serving the data connection. */
    RTTHREAD                    hThread;
    /** Thread started indicator. */
    volatile bool               fStarted;
    /** Thread stop indicator. */
    volatile bool               fStop;
    /** Thread stopped indicator. */
    volatile bool               fStopped;
    /** Overall result when closing the data connection. */
    int                         rc;
    /** Number of command arguments. */
    uint8_t                     cArgs;
    /** Command arguments array. Optional and can be NULL.
     *  Will be free'd by the data connection thread. */
    char**                      papszArgs;
    /** Circular buffer for caching data before writing. */
    PRTCIRCBUF                  pCircBuf;
} RTFTPSERVERDATACONN;
/** Pointer to a data connection struct. */
typedef RTFTPSERVERDATACONN *PRTFTPSERVERDATACONN;

/**
 * Structure for maintaining an internal FTP server client.
 */
typedef struct RTFTPSERVERCLIENT
{
    /** Pointer to internal server state. */
    PRTFTPSERVERINTERNAL        pServer;
    /** Socket handle the client is bound to. */
    RTSOCKET                    hSocket;
    /** Actual client state. */
    RTFTPSERVERCLIENTSTATE      State;
    /** The last set data connection IP. */
    RTNETADDRIPV4               DataConnAddr;
    /** The last set data connection port number. */
    uint16_t                    uDataConnPort;
    /** Data connection information.
     *  At the moment we only allow one data connection per client at a time. */
    PRTFTPSERVERDATACONN        pDataConn;
} RTFTPSERVERCLIENT;
/** Pointer to an internal FTP server client state. */
typedef RTFTPSERVERCLIENT *PRTFTPSERVERCLIENT;

/** Function pointer declaration for a specific FTP server command handler. */
typedef DECLCALLBACKTYPE(int, FNRTFTPSERVERCMD,(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apszArgs));
/** Pointer to a FNRTFTPSERVERCMD(). */
typedef FNRTFTPSERVERCMD *PFNRTFTPSERVERCMD;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int  rtFtpServerDataConnOpen(PRTFTPSERVERDATACONN pDataConn, PRTNETADDRIPV4 pAddr, uint16_t uPort);
static int  rtFtpServerDataConnClose(PRTFTPSERVERDATACONN pDataConn);
static void rtFtpServerDataConnReset(PRTFTPSERVERDATACONN pDataConn);
static int  rtFtpServerDataConnStart(PRTFTPSERVERDATACONN pDataConn, PFNRTTHREAD pfnThread, uint8_t cArgs, const char * const *apszArgs);
static int  rtFtpServerDataConnStop(PRTFTPSERVERDATACONN pDataConn);
static void rtFtpServerDataConnDestroy(PRTFTPSERVERDATACONN pDataConn);
static int  rtFtpServerDataConnFlush(PRTFTPSERVERDATACONN pDataConn);

static void rtFtpServerClientStateReset(PRTFTPSERVERCLIENTSTATE pState);

/** @name Command handlers.
 * @{
 */
static FNRTFTPSERVERCMD rtFtpServerHandleABOR;
static FNRTFTPSERVERCMD rtFtpServerHandleCDUP;
static FNRTFTPSERVERCMD rtFtpServerHandleCWD;
static FNRTFTPSERVERCMD rtFtpServerHandleFEAT;
static FNRTFTPSERVERCMD rtFtpServerHandleLIST;
static FNRTFTPSERVERCMD rtFtpServerHandleMODE;
static FNRTFTPSERVERCMD rtFtpServerHandleNOOP;
static FNRTFTPSERVERCMD rtFtpServerHandlePASS;
static FNRTFTPSERVERCMD rtFtpServerHandlePORT;
static FNRTFTPSERVERCMD rtFtpServerHandlePWD;
static FNRTFTPSERVERCMD rtFtpServerHandleOPTS;
static FNRTFTPSERVERCMD rtFtpServerHandleQUIT;
static FNRTFTPSERVERCMD rtFtpServerHandleRETR;
static FNRTFTPSERVERCMD rtFtpServerHandleSIZE;
static FNRTFTPSERVERCMD rtFtpServerHandleSTAT;
static FNRTFTPSERVERCMD rtFtpServerHandleSTRU;
static FNRTFTPSERVERCMD rtFtpServerHandleSYST;
static FNRTFTPSERVERCMD rtFtpServerHandleTYPE;
static FNRTFTPSERVERCMD rtFtpServerHandleUSER;
/** @} */

/**
 * Structure for maintaining a single command entry for the command table.
 */
typedef struct RTFTPSERVERCMD_ENTRY
{
    /** Command ID. */
    RTFTPSERVERCMD      enmCmd;
    /** Command represented as ASCII string. */
    char                szCmd[RTFTPSERVER_MAX_CMD_LEN];
    /** Whether the commands needs a logged in (valid) user. */
    bool                fNeedsUser;
    /** Function pointer invoked to handle the command. */
    PFNRTFTPSERVERCMD   pfnCmd;
} RTFTPSERVERCMD_ENTRY;
/** Pointer to a command entry. */
typedef RTFTPSERVERCMD_ENTRY *PRTFTPSERVERCMD_ENTRY;



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Table of handled commands.
 */
static const RTFTPSERVERCMD_ENTRY g_aCmdMap[] =
{
    { RTFTPSERVERCMD_ABOR,      "ABOR", true,  rtFtpServerHandleABOR },
    { RTFTPSERVERCMD_CDUP,      "CDUP", true,  rtFtpServerHandleCDUP },
    { RTFTPSERVERCMD_CWD,       "CWD",  true,  rtFtpServerHandleCWD  },
    { RTFTPSERVERCMD_FEAT,      "FEAT", false, rtFtpServerHandleFEAT },
    { RTFTPSERVERCMD_LIST,      "LIST", true,  rtFtpServerHandleLIST },
    { RTFTPSERVERCMD_MODE,      "MODE", true,  rtFtpServerHandleMODE },
    { RTFTPSERVERCMD_NOOP,      "NOOP", true,  rtFtpServerHandleNOOP },
    { RTFTPSERVERCMD_PASS,      "PASS", false, rtFtpServerHandlePASS },
    { RTFTPSERVERCMD_PORT,      "PORT", true,  rtFtpServerHandlePORT },
    { RTFTPSERVERCMD_PWD,       "PWD",  true,  rtFtpServerHandlePWD  },
    { RTFTPSERVERCMD_OPTS,      "OPTS", false, rtFtpServerHandleOPTS },
    { RTFTPSERVERCMD_QUIT,      "QUIT", false, rtFtpServerHandleQUIT },
    { RTFTPSERVERCMD_RETR,      "RETR", true,  rtFtpServerHandleRETR },
    { RTFTPSERVERCMD_SIZE,      "SIZE", true,  rtFtpServerHandleSIZE },
    { RTFTPSERVERCMD_STAT,      "STAT", true,  rtFtpServerHandleSTAT },
    { RTFTPSERVERCMD_STRU,      "STRU", true,  rtFtpServerHandleSTRU },
    { RTFTPSERVERCMD_SYST,      "SYST", false, rtFtpServerHandleSYST },
    { RTFTPSERVERCMD_TYPE,      "TYPE", true,  rtFtpServerHandleTYPE },
    { RTFTPSERVERCMD_USER,      "USER", false, rtFtpServerHandleUSER },
    { RTFTPSERVERCMD_END,       "",     false, NULL }
};

/** RFC-1123 month of the year names. */
static const char * const g_apszMonths[1+12] =
{
    "000", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/** Feature string which represents all commands we support in addition to RFC 959 (see RFC 2398).
 *  Must match the command table above.
 *
 *  Don't forget the beginning space (" ") at each feature. */
#define RTFTPSERVER_FEATURES_STRING \
    " SIZE\r\n" \
    " UTF8"

/** Maximum length in characters a FTP server path can have (excluding termination). */
#define RTFTPSERVER_MAX_PATH        RTPATH_MAX


/*********************************************************************************************************************************
*   Protocol Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Replies a (three digit) reply code back to the client.
 *
 * @returns VBox status code.
 * @param   pClient             Client to reply to.
 * @param   enmReply            Reply code to send.
 */
static int rtFtpServerSendReplyRc(PRTFTPSERVERCLIENT pClient, RTFTPSERVER_REPLY enmReply)
{
    /* Note: If we don't supply any additional text, make sure to include an empty stub, as
     *       some clients expect this as part of their parsing code. */
    char szReply[32];
    RTStrPrintf2(szReply, sizeof(szReply), "%RU32 -\r\n", enmReply);

    LogFlowFunc(("Sending reply code %RU32\n", enmReply));

    return RTTcpWrite(pClient->hSocket, szReply, strlen(szReply));
}

/**
 * Replies a (three digit) reply code with a custom message back to the client.
 *
 * @returns VBox status code.
 * @param   pClient             Client to reply to.
 * @param   enmReply            Reply code to send.
 * @param   pszFormat           Format string of message to send with the reply code.
 */
static int rtFtpServerSendReplyRcEx(PRTFTPSERVERCLIENT pClient, RTFTPSERVER_REPLY enmReply,
                                    const char *pszFormat, ...)
{
    char *pszMsg = NULL;

    va_list args;
    va_start(args, pszFormat);
    char *pszFmt = NULL;
    const int cch = RTStrAPrintfV(&pszFmt, pszFormat, args);
    va_end(args);
    AssertReturn(cch > 0, VERR_NO_MEMORY);

    int rc = RTStrAPrintf(&pszMsg, "%RU32 -", enmReply);
    AssertRCReturn(rc, rc);

    /** @todo Support multi-line replies (see 4.2ff). */

    if (pszFmt)
    {
        rc = RTStrAAppend(&pszMsg, " ");
        AssertRCReturn(rc, rc);

        rc = RTStrAAppend(&pszMsg, pszFmt);
        AssertRCReturn(rc, rc);
    }


    rc = RTStrAAppend(&pszMsg, "\r\n");
    AssertRCReturn(rc, rc);

    RTStrFree(pszFmt);

    rc = RTTcpWrite(pClient->hSocket, pszMsg, strlen(pszMsg));

    RTStrFree(pszMsg);

    return rc;
}

/**
 * Replies a string back to the client.
 *
 * @returns VBox status code.
 * @param   pClient             Client to reply to.
 * @param   pszFormat           Format to reply.
 * @param   ...                 Format arguments.
 */
static int rtFtpServerSendReplyStr(PRTFTPSERVERCLIENT pClient, const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    char *psz = NULL;
    const int cch = RTStrAPrintfV(&psz, pszFormat, args);
    va_end(args);
    AssertReturn(cch > 0, VERR_NO_MEMORY);

    int rc = RTStrAAppend(&psz, "\r\n");
    AssertRCReturn(rc, rc);

    LogFlowFunc(("Sending reply '%s'\n", psz));

    rc = RTTcpWrite(pClient->hSocket, psz, strlen(psz));

    RTStrFree(psz);

    return rc;
}

/**
 * Validates if a given absolute path is valid or not.
 *
 * @returns \c true if path is valid, or \c false if not.
 * @param   pszPath             Path to check.
 * @param   fIsAbsolute         Whether the path to check is an absolute path or not.
 */
static bool rtFtpServerPathIsValid(const char *pszPath, bool fIsAbsolute)
{
    if (!pszPath)
        return false;

    bool fIsValid =    strlen(pszPath)
                    && RTStrIsValidEncoding(pszPath)
                    && RTStrStr(pszPath, "..") == NULL;     /** @todo Very crude for now -- improve this. */
    if (   fIsValid
        && fIsAbsolute)
    {
        RTFSOBJINFO objInfo;
        int rc2 = RTPathQueryInfo(pszPath, &objInfo, RTFSOBJATTRADD_NOTHING);
        if (RT_SUCCESS(rc2))
        {
            fIsValid =    RTFS_IS_DIRECTORY(objInfo.Attr.fMode)
                       || RTFS_IS_FILE(objInfo.Attr.fMode);

            /* No symlinks and other stuff not allowed. */
        }
        else
            fIsValid = false;
    }

    LogFlowFunc(("pszPath=%s -> %RTbool\n", pszPath, fIsValid));
    return fIsValid;
}

/**
 * Sets the current working directory for a client.
 *
 * @returns VBox status code.
 * @param   pState              Client state to set current working directory for.
 * @param   pszPath             Working directory to set.
 */
static int rtFtpSetCWD(PRTFTPSERVERCLIENTSTATE pState, const char *pszPath)
{
    RTStrFree(pState->pszCWD);

    if (!rtFtpServerPathIsValid(pszPath, false /* fIsAbsolute */))
        return VERR_INVALID_PARAMETER;

    pState->pszCWD = RTStrDup(pszPath);

    LogFlowFunc(("Current CWD is now '%s'\n", pState->pszCWD));

    int rc = pState->pszCWD ? VINF_SUCCESS : VERR_NO_MEMORY;
    AssertRC(rc);
    return rc;
}

/**
 * Looks up an user account.
 *
 * @returns VBox status code, or VERR_NOT_FOUND if user has not been found.
 * @param   pClient             Client to look up user for.
 * @param   pszUser             User name to look up.
 */
static int rtFtpServerLookupUser(PRTFTPSERVERCLIENT pClient, const char *pszUser)
{
    RTFTPSERVER_HANDLE_CALLBACK_VA_RET(pfnOnUserConnect, pszUser);
}

/**
 * Handles the actual client authentication.
 *
 * @returns VBox status code, or VERR_ACCESS_DENIED if authentication failed.
 * @param   pClient             Client to authenticate.
 * @param   pszUser             User name to authenticate with.
 * @param   pszPassword         Password to authenticate with.
 */
static int rtFtpServerAuthenticate(PRTFTPSERVERCLIENT pClient, const char *pszUser, const char *pszPassword)
{
    RTFTPSERVER_HANDLE_CALLBACK_VA_RET(pfnOnUserAuthenticate, pszUser, pszPassword);
}

/**
 * Converts a RTFSOBJINFO struct to a string.
 *
 * @returns VBox status code.
 * @param   pObjInfo            RTFSOBJINFO object to convert.
 * @param   pszFsObjInfo        Where to store the output string.
 * @param   cbFsObjInfo         Size of the output string in bytes.
 */
static int rtFtpServerFsObjInfoToStr(PRTFSOBJINFO pObjInfo, char *pszFsObjInfo, size_t cbFsObjInfo)
{
    RTFMODE fMode = pObjInfo->Attr.fMode;
    char chFileType;
    switch (fMode & RTFS_TYPE_MASK)
    {
        case RTFS_TYPE_FIFO:        chFileType = 'f'; break;
        case RTFS_TYPE_DEV_CHAR:    chFileType = 'c'; break;
        case RTFS_TYPE_DIRECTORY:   chFileType = 'd'; break;
        case RTFS_TYPE_DEV_BLOCK:   chFileType = 'b'; break;
        case RTFS_TYPE_FILE:        chFileType = '-'; break;
        case RTFS_TYPE_SYMLINK:     chFileType = 'l'; break;
        case RTFS_TYPE_SOCKET:      chFileType = 's'; break;
        case RTFS_TYPE_WHITEOUT:    chFileType = 'w'; break;
        default:                    chFileType = '?'; break;
    }

    char szTimeBirth[RTTIME_STR_LEN];
    char szTimeChange[RTTIME_STR_LEN];
    char szTimeModification[RTTIME_STR_LEN];
    char szTimeAccess[RTTIME_STR_LEN];

#define INFO_TO_STR(a_Format, ...) \
    do \
    { \
        const ssize_t cchSize = RTStrPrintf2(szTemp, sizeof(szTemp), a_Format, __VA_ARGS__); \
        AssertReturn(cchSize > 0, VERR_BUFFER_OVERFLOW); \
        const int rc2 = RTStrCat(pszFsObjInfo, cbFsObjInfo, szTemp); \
        AssertRCReturn(rc2, rc2); \
    } while (0);

    char szTemp[128];

    INFO_TO_STR("%c", chFileType);
    INFO_TO_STR("%c%c%c",
                fMode & RTFS_UNIX_IRUSR ? 'r' : '-',
                fMode & RTFS_UNIX_IWUSR ? 'w' : '-',
                fMode & RTFS_UNIX_IXUSR ? 'x' : '-');
    INFO_TO_STR("%c%c%c",
                fMode & RTFS_UNIX_IRGRP ? 'r' : '-',
                fMode & RTFS_UNIX_IWGRP ? 'w' : '-',
                fMode & RTFS_UNIX_IXGRP ? 'x' : '-');
    INFO_TO_STR("%c%c%c",
                fMode & RTFS_UNIX_IROTH ? 'r' : '-',
                fMode & RTFS_UNIX_IWOTH ? 'w' : '-',
                fMode & RTFS_UNIX_IXOTH ? 'x' : '-');

    INFO_TO_STR( " %c%c%c%c%c%c%c%c%c%c%c%c%c%c",
                fMode & RTFS_DOS_READONLY          ? 'R' : '-',
                fMode & RTFS_DOS_HIDDEN            ? 'H' : '-',
                fMode & RTFS_DOS_SYSTEM            ? 'S' : '-',
                fMode & RTFS_DOS_DIRECTORY         ? 'D' : '-',
                fMode & RTFS_DOS_ARCHIVED          ? 'A' : '-',
                fMode & RTFS_DOS_NT_DEVICE         ? 'd' : '-',
                fMode & RTFS_DOS_NT_NORMAL         ? 'N' : '-',
                fMode & RTFS_DOS_NT_TEMPORARY      ? 'T' : '-',
                fMode & RTFS_DOS_NT_SPARSE_FILE    ? 'P' : '-',
                fMode & RTFS_DOS_NT_REPARSE_POINT  ? 'J' : '-',
                fMode & RTFS_DOS_NT_COMPRESSED     ? 'C' : '-',
                fMode & RTFS_DOS_NT_OFFLINE        ? 'O' : '-',
                fMode & RTFS_DOS_NT_NOT_CONTENT_INDEXED ? 'I' : '-',
                fMode & RTFS_DOS_NT_ENCRYPTED      ? 'E' : '-');

    INFO_TO_STR( " %d %4d %4d %10lld %10lld",
                pObjInfo->Attr.u.Unix.cHardlinks,
                pObjInfo->Attr.u.Unix.uid,
                pObjInfo->Attr.u.Unix.gid,
                pObjInfo->cbObject,
                pObjInfo->cbAllocated);

    INFO_TO_STR( " %s %s %s %s",
                RTTimeSpecToString(&pObjInfo->BirthTime,        szTimeBirth,        sizeof(szTimeBirth)),
                RTTimeSpecToString(&pObjInfo->ChangeTime,       szTimeChange,       sizeof(szTimeChange)),
                RTTimeSpecToString(&pObjInfo->ModificationTime, szTimeModification, sizeof(szTimeModification)),
                RTTimeSpecToString(&pObjInfo->AccessTime,       szTimeAccess,       sizeof(szTimeAccess)) );

#undef INFO_TO_STR

    return VINF_SUCCESS;
}

/**
 * Parses a string which consists of an IPv4 (ww,xx,yy,zz) and a port number (hi,lo), all separated by comma delimiters.
 * See RFC 959, 4.1.2.
 *
 * @returns VBox status code.
 * @param   pszStr              String to parse.
 * @param   pAddr               Where to store the IPv4 address on success.
 * @param   puPort              Where to store the port number on success.
 */
static int rtFtpParseHostAndPort(const char *pszStr, PRTNETADDRIPV4 pAddr, uint16_t *puPort)
{
    AssertPtrReturn(pszStr, VERR_INVALID_POINTER);
    AssertPtrReturn(pAddr, VERR_INVALID_POINTER);
    AssertPtrReturn(puPort, VERR_INVALID_POINTER);

    char *pszNext;
    int rc;

    /* Parse IP (v4). */
    /** @todo I don't think IPv6 ever will be a thing here, or will it? */
    rc = RTStrToUInt8Ex(pszStr, &pszNext, 10, &pAddr->au8[0]);
    if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_CHARS)
        return VERR_INVALID_PARAMETER;
    if (*pszNext++ != ',')
        return VERR_INVALID_PARAMETER;

    rc = RTStrToUInt8Ex(pszNext, &pszNext, 10, &pAddr->au8[1]);
    if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_CHARS)
        return VERR_INVALID_PARAMETER;
    if (*pszNext++ != ',')
        return VERR_INVALID_PARAMETER;

    rc = RTStrToUInt8Ex(pszNext, &pszNext, 10, &pAddr->au8[2]);
    if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_CHARS)
        return VERR_INVALID_PARAMETER;
    if (*pszNext++ != ',')
        return VERR_INVALID_PARAMETER;

    rc = RTStrToUInt8Ex(pszNext, &pszNext, 10, &pAddr->au8[3]);
    if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_SPACES && rc != VWRN_TRAILING_CHARS)
        return VERR_INVALID_PARAMETER;
    if (*pszNext++ != ',')
        return VERR_INVALID_PARAMETER;

    /* Parse port. */
    uint8_t uPortHi;
    rc = RTStrToUInt8Ex(pszNext, &pszNext, 10, &uPortHi);
    if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_SPACES && rc != VWRN_TRAILING_CHARS)
        return VERR_INVALID_PARAMETER;
    if (*pszNext++ != ',')
        return VERR_INVALID_PARAMETER;
    uint8_t uPortLo;
    rc = RTStrToUInt8Ex(pszNext, &pszNext, 10, &uPortLo);
    if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_SPACES && rc != VWRN_TRAILING_CHARS)
        return VERR_INVALID_PARAMETER;

    *puPort = RT_MAKE_U16(uPortLo, uPortHi);

    return rc;
}

/**
 * Duplicates a command argument vector.
 *
 * @returns Duplicated argument vector or NULL if failed or no arguments given. Needs to be free'd with rtFtpCmdArgsFree().
 * @param   cArgs               Number of arguments in argument vector.
 * @param   apszArgs            Pointer to argument vector to duplicate.
 */
static char** rtFtpCmdArgsDup(uint8_t cArgs, const char * const *apszArgs)
{
    if (!cArgs)
        return NULL;

    char **apszArgsDup = (char **)RTMemAlloc(cArgs * sizeof(char *));
    if (!apszArgsDup)
    {
        AssertFailed();
        return NULL;
    }

    int rc2 = VINF_SUCCESS;

    uint8_t i;
    for (i = 0; i < cArgs; i++)
    {
        apszArgsDup[i] = RTStrDup(apszArgs[i]);
        if (!apszArgsDup[i])
            rc2 = VERR_NO_MEMORY;
    }

    if (RT_FAILURE(rc2))
    {
        while (i--)
            RTStrFree(apszArgsDup[i]);

        RTMemFree(apszArgsDup);
        return NULL;
    }

    return apszArgsDup;
}

/**
 * Frees a command argument vector.
 *
 * @param   cArgs               Number of arguments in argument vector.
 * @param   papszArgs           Pointer to argument vector to free.
 */
static void rtFtpCmdArgsFree(uint8_t cArgs, char **papszArgs)
{
    while (cArgs--)
        RTStrFree(papszArgs[cArgs]);

    RTMemFree(papszArgs);
}

/**
 * Opens a data connection to the client.
 *
 * @returns VBox status code.
 * @param   pDataConn           Data connection to open.
 * @param   pAddr               Address for the data connection.
 * @param   uPort               Port for the data connection.
 */
static int rtFtpServerDataConnOpen(PRTFTPSERVERDATACONN pDataConn, PRTNETADDRIPV4 pAddr, uint16_t uPort)
{
    LogFlowFuncEnter();

    /** @todo Implement IPv6 handling here. */
    char szAddress[32];
    const ssize_t cchAdddress = RTStrPrintf2(szAddress, sizeof(szAddress), "%RU8.%RU8.%RU8.%RU8",
                                             pAddr->au8[0], pAddr->au8[1], pAddr->au8[2], pAddr->au8[3]);
    AssertReturn(cchAdddress > 0, VERR_NO_MEMORY);

    int rc = VINF_SUCCESS; /* Shut up MSVC. */

    /* Try a bit harder if the data connection is not ready (yet). */
    for (int i = 0; i < 10; i++)
    {
        rc = RTTcpClientConnect(szAddress, uPort, &pDataConn->hSocket);
        if (RT_SUCCESS(rc))
            break;
        RTThreadSleep(100);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Closes a data connection to the client.
 *
 * @returns VBox status code.
 * @param   pDataConn           Data connection to close.
 */
static int rtFtpServerDataConnClose(PRTFTPSERVERDATACONN pDataConn)
{
    int rc = VINF_SUCCESS;

    if (pDataConn->hSocket != NIL_RTSOCKET)
    {
        LogFlowFuncEnter();

        rtFtpServerDataConnFlush(pDataConn);

        rc = RTTcpClientClose(pDataConn->hSocket);
        pDataConn->hSocket = NIL_RTSOCKET;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Writes data to the data connection.
 *
 * @returns VBox status code.
 * @param   pDataConn           Data connection to write to.
 * @param   pvData              Data to write.
 * @param   cbData              Size (in bytes) of data to write.
 * @param   pcbWritten          How many bytes were written. Optional.
 */
static int rtFtpServerDataConnWrite(PRTFTPSERVERDATACONN pDataConn, const void *pvData, size_t cbData, size_t *pcbWritten)
{
    int rc = RTTcpWrite(pDataConn->hSocket, pvData, cbData);
    if (RT_SUCCESS(rc))
    {
        if (pcbWritten)
            *pcbWritten = cbData;
    }

    return rc;
}

/**
 * Flushes a data connection.
 *
 * @returns VBox status code.
 * @param   pDataConn           Data connection to flush.
 */
static int rtFtpServerDataConnFlush(PRTFTPSERVERDATACONN pDataConn)
{
    int rc = VINF_SUCCESS;

    size_t cbUsed = RTCircBufUsed(pDataConn->pCircBuf);
    while (cbUsed)
    {
        void   *pvBlock;
        size_t  cbBlock;
        RTCircBufAcquireReadBlock(pDataConn->pCircBuf, cbUsed, &pvBlock, &cbBlock);
        if (cbBlock)
        {
            size_t cbWritten = 0;
            rc = rtFtpServerDataConnWrite(pDataConn, pvBlock, cbBlock, &cbWritten);
            if (RT_SUCCESS(rc))
            {
                AssertBreak(cbUsed >= cbWritten);
                cbUsed -= cbWritten;
            }

            RTCircBufReleaseReadBlock(pDataConn->pCircBuf, cbWritten);

            if (RT_FAILURE(rc))
                break;
        }
    }

    return rc;
}

/**
 * Checks if flushing a data connection is necessary, and if so, flush it.
 *
 * @returns VBox status code.
 * @param   pDataConn           Data connection to check / do flushing for.
 */
static int rtFtpServerDataCheckFlush(PRTFTPSERVERDATACONN pDataConn)
{
    int rc = VINF_SUCCESS;

    size_t cbUsed = RTCircBufUsed(pDataConn->pCircBuf);
    if (cbUsed >= _4K) /** @todo Make this more dynamic. */
    {
        rc = rtFtpServerDataConnFlush(pDataConn);
    }

    return rc;
}

/**
 * Adds new data for a data connection to be sent.
 *
 * @returns VBox status code.
 * @param   pDataConn           Data connection to add new data to.
 * @param   pvData              Pointer to data to add.
 * @param   cbData              Size (in bytes) of data to add.
 */
static int rtFtpServerDataConnAddData(PRTFTPSERVERDATACONN pDataConn, const void *pvData, size_t cbData)
{
    AssertReturn(cbData <= RTCircBufFree(pDataConn->pCircBuf), VERR_BUFFER_OVERFLOW);

    int rc = VINF_SUCCESS;

    size_t cbToWrite = cbData;
    do
    {
        void   *pvBlock;
        size_t  cbBlock;
        RTCircBufAcquireWriteBlock(pDataConn->pCircBuf, cbToWrite, &pvBlock, &cbBlock);
        if (cbBlock)
        {
            AssertBreak(cbData >= cbBlock);
            memcpy(pvBlock, pvData, cbBlock);

            AssertBreak(cbToWrite >= cbBlock);
            cbToWrite -= cbBlock;

            RTCircBufReleaseWriteBlock(pDataConn->pCircBuf, cbBlock);
        }

    } while (cbToWrite);

    if (RT_SUCCESS(rc))
        rc = rtFtpServerDataCheckFlush(pDataConn);

    return rc;
}

/**
 * Does a printf-style write on a data connection.
 *
 * @returns VBox status code.
 * @param   pDataConn           Data connection to write to.
 * @param   pszFormat           Format string to send. No (terminal) termination added.
 */
static int rtFtpServerDataConnPrintf(PRTFTPSERVERDATACONN pDataConn, const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    char *pszFmt = NULL;
    const int cch = RTStrAPrintfV(&pszFmt, pszFormat, args);
    va_end(args);
    AssertReturn(cch > 0, VERR_NO_MEMORY);

    char *pszMsg = NULL;
    int rc = RTStrAAppend(&pszMsg, pszFmt);
    AssertRCReturn(rc, rc);

    RTStrFree(pszFmt);

    rc = rtFtpServerDataConnAddData(pDataConn, pszMsg, strlen(pszMsg));

    RTStrFree(pszMsg);

    return rc;
}

/**
 * Data connection thread for writing (sending) a file to the client.
 *
 * @returns VBox status code.
 * @param   ThreadSelf          Thread handle. Unused at the moment.
 * @param   pvUser              Pointer to user-provided data. Of type PRTFTPSERVERCLIENT.
 */
static DECLCALLBACK(int) rtFtpServerDataConnFileWriteThread(RTTHREAD ThreadSelf, void *pvUser)
{
    RT_NOREF(ThreadSelf);

    PRTFTPSERVERCLIENT pClient = (PRTFTPSERVERCLIENT)pvUser;
    AssertPtr(pClient);

    PRTFTPSERVERDATACONN pDataConn = pClient->pDataConn;
    AssertPtr(pDataConn);

    LogFlowFuncEnter();

    uint32_t cbBuf = _64K; /** @todo Improve this. */
    void *pvBuf = RTMemAlloc(cbBuf);
    if (!pvBuf)
        return VERR_NO_MEMORY;

    int rc;

    /* Set start indicator. */
    pDataConn->fStarted = true;

    RTThreadUserSignal(RTThreadSelf());

    AssertPtr(pDataConn->papszArgs);
    const char *pszFile = pDataConn->papszArgs[0];
    AssertPtr(pszFile);

    void *pvHandle = NULL; /* Opaque handle known to the actual implementation. */

    RTFTPSERVER_HANDLE_CALLBACK_VA(pfnOnFileOpen, pszFile,
                                   RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE, &pvHandle);
    if (RT_SUCCESS(rc))
    {
        LogFlowFunc(("Transfer started\n"));

        do
        {
            size_t cbRead = 0;
            RTFTPSERVER_HANDLE_CALLBACK_VA(pfnOnFileRead, pvHandle, pvBuf, cbBuf, &cbRead);
            if (   RT_SUCCESS(rc)
                && cbRead)
            {
                rc = rtFtpServerDataConnWrite(pDataConn, pvBuf, cbRead, NULL /* pcbWritten */);
            }

            if (   !cbRead
                || ASMAtomicReadBool(&pDataConn->fStop))
            {
                break;
            }
        }
        while (RT_SUCCESS(rc));

        RTFTPSERVER_HANDLE_CALLBACK_VA(pfnOnFileClose, pvHandle);

        LogFlowFunc(("Transfer done\n"));
    }

    RTMemFree(pvBuf);
    pvBuf = NULL;

    pDataConn->fStopped = true;
    pDataConn->rc       = rc;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Creates a data connection.
 *
 * @returns VBox status code.
 * @param   pClient             Client to create data connection for.
 * @param   ppDataConn          Where to return the (allocated) data connection.
 */
static int rtFtpServerDataConnCreate(PRTFTPSERVERCLIENT pClient, PRTFTPSERVERDATACONN *ppDataConn)
{
    if (pClient->pDataConn)
        return VERR_FTP_DATA_CONN_LIMIT_REACHED;

    PRTFTPSERVERDATACONN pDataConn = (PRTFTPSERVERDATACONN)RTMemAllocZ(sizeof(RTFTPSERVERDATACONN));
    if (!pDataConn)
        return VERR_NO_MEMORY;

    rtFtpServerDataConnReset(pDataConn);

    pDataConn->pClient = pClient;

    /* Use the last configured addr + port. */
    pDataConn->Addr    = pClient->DataConnAddr;
    pDataConn->uPort   = pClient->uDataConnPort;

    int rc = RTCircBufCreate(&pDataConn->pCircBuf, _16K); /** @todo Some random value; improve. */
    if (RT_SUCCESS(rc))
    {
        *ppDataConn = pDataConn;
    }

    LogFlowFuncLeaveRC(VINF_SUCCESS);
    return rc;
}

/**
 * Starts a data connection.
 *
 * @returns VBox status code.
 * @param   pDataConn           Data connection to start.
 * @param   pfnThread           Thread function for the data connection to use.
 * @param   cArgs               Number of arguments.
 * @param   apszArgs            Array of arguments.
 */
static int rtFtpServerDataConnStart(PRTFTPSERVERDATACONN pDataConn, PFNRTTHREAD pfnThread,
                                    uint8_t cArgs, const char * const *apszArgs)
{
    AssertPtrReturn(pDataConn, VERR_INVALID_POINTER);
    AssertPtrReturn(pfnThread, VERR_INVALID_POINTER);

    AssertReturn(!pDataConn->fStarted, VERR_WRONG_ORDER);
    AssertReturn(!pDataConn->fStop,    VERR_WRONG_ORDER);
    AssertReturn(!pDataConn->fStopped, VERR_WRONG_ORDER);

    int rc = VINF_SUCCESS;

    if (cArgs)
    {
        pDataConn->papszArgs = rtFtpCmdArgsDup(cArgs, apszArgs);
        if (!pDataConn->papszArgs)
            rc = VERR_NO_MEMORY;
    }

    if (RT_SUCCESS(rc))
    {
        pDataConn->cArgs = cArgs;

        rc = rtFtpServerDataConnOpen(pDataConn, &pDataConn->Addr, pDataConn->uPort);
        if (RT_SUCCESS(rc))
        {
            rc = RTThreadCreate(&pDataConn->hThread, pfnThread,
                                pDataConn->pClient, 0, RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE,
                                "ftpdata");
            if (RT_SUCCESS(rc))
            {
                int rc2 = RTThreadUserWait(pDataConn->hThread, 30 * 1000 /* Timeout in ms */);
                AssertRC(rc2);

                if (!pDataConn->fStarted) /* Did the thread indicate that it started correctly? */
                    rc = VERR_FTP_DATA_CONN_INIT_FAILED;
            }

            if (RT_FAILURE(rc))
                rtFtpServerDataConnClose(pDataConn);
        }
    }

    if (RT_FAILURE(rc))
    {
        rtFtpCmdArgsFree(pDataConn->cArgs, pDataConn->papszArgs);

        pDataConn->cArgs     = 0;
        pDataConn->papszArgs = NULL;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Stops a data connection.
 *
 * @returns VBox status code.
 * @param   pDataConn           Data connection to stop.
 */
static int rtFtpServerDataConnStop(PRTFTPSERVERDATACONN pDataConn)
{
    if (!pDataConn)
        return VINF_SUCCESS;

    LogFlowFuncEnter();

    int rc = VINF_SUCCESS;

    if (pDataConn->hThread != NIL_RTTHREAD)
    {
        /* Set stop indicator. */
        pDataConn->fStop = true;

        int rcThread = VERR_WRONG_ORDER;
        rc = RTThreadWait(pDataConn->hThread, 30 * 1000 /* Timeout in ms */, &rcThread);
    }

    if (RT_SUCCESS(rc))
        rtFtpServerDataConnClose(pDataConn);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Destroys a data connection.
 *
 * @param   pDataConn           Data connection to destroy. The pointer is not valid anymore after successful return.
 */
static void rtFtpServerDataConnDestroy(PRTFTPSERVERDATACONN pDataConn)
{
    if (!pDataConn)
        return;

    LogFlowFuncEnter();

    rtFtpServerDataConnClose(pDataConn);
    rtFtpCmdArgsFree(pDataConn->cArgs, pDataConn->papszArgs);

    RTCircBufDestroy(pDataConn->pCircBuf);

    RTMemFree(pDataConn);
    pDataConn = NULL;

    LogFlowFuncLeave();
    return;
}

/**
 * Resets a data connection structure.
 *
 * @param   pDataConn           Data connection structure to reset.
 */
static void rtFtpServerDataConnReset(PRTFTPSERVERDATACONN pDataConn)
{
    LogFlowFuncEnter();

    pDataConn->hSocket  = NIL_RTSOCKET;
    pDataConn->uPort    = 20; /* Default port to use. */
    pDataConn->hThread  = NIL_RTTHREAD;
    pDataConn->fStarted = false;
    pDataConn->fStop    = false;
    pDataConn->fStopped = false;
    pDataConn->rc       = VERR_IPE_UNINITIALIZED_STATUS;
}


/*********************************************************************************************************************************
*   Command Protocol Handlers                                                                                                    *
*********************************************************************************************************************************/

static DECLCALLBACK(int) rtFtpServerHandleABOR(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apszArgs)
{
    RT_NOREF(cArgs, apszArgs);

    int rc = rtFtpServerDataConnClose(pClient->pDataConn);
    if (RT_SUCCESS(rc))
    {
        rtFtpServerDataConnDestroy(pClient->pDataConn);
        pClient->pDataConn = NULL;

        rc = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_OKAY);
    }

    return rc;
}

static DECLCALLBACK(int) rtFtpServerHandleCDUP(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apszArgs)
{
    RT_NOREF(cArgs, apszArgs);

    int rc;

    RTFTPSERVER_HANDLE_CALLBACK(pfnOnPathUp);

    if (RT_SUCCESS(rc))
    {
        const size_t cbPath   = sizeof(char) * RTFTPSERVER_MAX_PATH;
        char        *pszPath  = RTStrAlloc(cbPath);
        if (pszPath)
        {
            RTFTPSERVER_HANDLE_CALLBACK_VA(pfnOnPathGetCurrent, pszPath, cbPath);

            if (RT_SUCCESS(rc))
                rc = rtFtpSetCWD(&pClient->State, pszPath);

            RTStrFree(pszPath);

            rc = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_OKAY);
        }
        else
            rc = VERR_NO_MEMORY;
    }

    if (RT_FAILURE(rc))
    {
        int rc2 = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_CONN_REQ_FILE_ACTION_NOT_TAKEN);
        AssertRC(rc2);
    }

    return rc;
}

static DECLCALLBACK(int) rtFtpServerHandleCWD(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apszArgs)
{
    if (cArgs != 1)
        return VERR_INVALID_PARAMETER;

    int rc;

    const char *pszPath = apszArgs[0];

    if (!rtFtpServerPathIsValid(pszPath, false /* fIsAbsolute */))
        return VERR_INVALID_PARAMETER;

    RTFTPSERVER_HANDLE_CALLBACK_VA(pfnOnPathSetCurrent, pszPath);

    if (RT_SUCCESS(rc))
        rc = rtFtpSetCWD(&pClient->State, pszPath);

    return rtFtpServerSendReplyRc(pClient,
                                    RT_SUCCESS(rc)
                                  ? RTFTPSERVER_REPLY_OKAY : RTFTPSERVER_REPLY_CONN_REQ_FILE_ACTION_NOT_TAKEN);
}

static DECLCALLBACK(int) rtFtpServerHandleFEAT(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apszArgs)
{
    RT_NOREF(cArgs, apszArgs);

    int rc = rtFtpServerSendReplyStr(pClient, "211-BEGIN Features:");
    if (RT_SUCCESS(rc))
    {
        rc = rtFtpServerSendReplyStr(pClient, RTFTPSERVER_FEATURES_STRING);
        if (RT_SUCCESS(rc))
            rc = rtFtpServerSendReplyStr(pClient, "211 END Features");
    }

    return rc;
}

/**
 * Formats the given user ID according to the specified options.
 *
 * @returns pszDst
 * @param   uid             The UID to format.
 * @param   pszOwner        The owner returned by the FS.
 * @param   pszDst          The output buffer.
 * @param   cbDst           The output buffer size.
 */
static const char *rtFtpServerDecimalFormatOwner(RTUID uid, const char *pszOwner, char *pszDst, size_t cbDst)
{
    if (pszOwner)
    {
        RTStrCopy(pszDst, cbDst, pszOwner);
        return pszDst;
    }
    if (uid == NIL_RTUID)
        return "<Nil>";

    RTStrFormatU64(pszDst, cbDst, uid, 10, 0, 0, 0);
    return pszDst;
}

/**
 * Formats the given group ID according to the specified options.
 *
 * @returns pszDst
 * @param   gid             The GID to format.
 * @param   pszGroup        The group returned by the FS.
 * @param   pszDst          The output buffer.
 * @param   cbDst           The output buffer size.
 */
static const char *rtFtpServerDecimalFormatGroup(RTGID gid, const char *pszGroup, char *pszDst, size_t cbDst)
{
    if (pszGroup)
    {
        RTStrCopy(pszDst, cbDst, pszGroup);
        return pszDst;
    }
    if (gid == NIL_RTGID)
        return "<Nil>";

    RTStrFormatU64(pszDst, cbDst, gid, 10, 0, 0, 0);
    return pszDst;
}

/**
 * Format file size.
 */
static const char *rtFtpServerFormatSize(uint64_t cb, char *pszDst, size_t cbDst)
{
    RTStrFormatU64(pszDst, cbDst, cb, 10, 0, 0, 0);
    return pszDst;
}

/**
 * Formats the given timestamp according to (non-standardized) FTP LIST command.
 *
 * @returns pszDst
 * @param   pTimestamp      The timestamp to format.
 * @param   pszDst          The output buffer.
 * @param   cbDst           The output buffer size.
 */
static const char *rtFtpServerFormatTimestamp(PCRTTIMESPEC pTimestamp, char *pszDst, size_t cbDst)
{
    RTTIME Time;
    RTTimeExplode(&Time, pTimestamp);

    /* Calc the UTC offset part. */
    int32_t offUtc = Time.offUTC;
    Assert(offUtc <= 840 && offUtc >= -840);
    char     chSign;
    if (offUtc >= 0)
        chSign = '+';
    else
    {
        chSign = '-';
        offUtc = -offUtc;
    }
    uint32_t offUtcHour   = (uint32_t)offUtc / 60;
    uint32_t offUtcMinute = (uint32_t)offUtc % 60;

    /** @todo Cache this. */
    RTTIMESPEC TimeSpecNow;
    RTTimeNow(&TimeSpecNow);
    RTTIME TimeNow;
    RTTimeExplode(&TimeNow, &TimeSpecNow);

    /* Only include the year if it's not the same year as today. */
    if (TimeNow.i32Year != Time.i32Year)
    {
        RTStrPrintf(pszDst, cbDst, "%s  %02RU8  %5RU32",
                    g_apszMonths[Time.u8Month], Time.u8MonthDay, Time.i32Year);
    }
    else /* ... otherwise include the (rough) time (as GMT). */
    {
        RTStrPrintf(pszDst, cbDst, "%s  %02RU8  %02RU32:%02RU32",
                    g_apszMonths[Time.u8Month], Time.u8MonthDay, offUtcHour, offUtcMinute);
    }

    return pszDst;
}

/**
 * Format name, i.e. escape, hide, quote stuff.
 */
static const char *rtFtpServerFormatName(const char *pszName, char *pszDst, size_t cbDst)
{
    /** @todo implement name formatting.   */
    RT_NOREF(pszDst, cbDst);
    return pszName;
}

/**
 * Figures out the length for a 32-bit number when formatted as decimal.
 * @returns Number of digits.
 * @param   uValue              The number.
 */
DECLINLINE(size_t) rtFtpServerDecimalFormatLengthU32(uint32_t uValue)
{
    if (uValue < 10)
        return 1;
    if (uValue < 100)
        return 2;
    if (uValue < 1000)
        return 3;
    if (uValue < 10000)
        return 4;
    if (uValue < 100000)
        return 5;
    if (uValue < 1000000)
        return 6;
    if (uValue < 10000000)
        return 7;
    if (uValue < 100000000)
        return 8;
    if (uValue < 1000000000)
        return 9;
    return 10;
}

/**
 * Allocates a new directory collection.
 *
 * @returns The collection allocated.
 */
static PRTFTPDIRCOLLECTION rtFtpServerDataConnDirCollAlloc(void)
{
    return (PRTFTPDIRCOLLECTION)RTMemAllocZ(sizeof(RTFTPDIRCOLLECTION));
}

/**
 * Frees a directory collection and its entries.
 *
 * @param   pCollection         The collection to free.
 */
static void rtFtpServerDataConnDirCollFree(PRTFTPDIRCOLLECTION pCollection)
{
    PPRTFTPDIRENTRY    papEntries  = pCollection->papEntries;
    size_t             j           = pCollection->cEntries;
    while (j-- > 0)
    {
        RTMemFree(papEntries[j]);
        papEntries[j] = NULL;
    }
    RTMemFree(papEntries);
    pCollection->papEntries        = NULL;
    pCollection->cEntries          = 0;
    pCollection->cEntriesAllocated = 0;
    RTMemFree(pCollection);
}

/**
 * Adds one entry to a collection.
 *
 * @returns VBox status code.
 * @param   pCollection         The collection to add entry to.
 * @param   pszEntry            The entry name.
 * @param   pInfo               The entry info.
 * @param   pszOwner            The owner name if available, otherwise NULL.
 * @param   pszGroup            The group anme if available, otherwise NULL.
 * @param   pszTarget           The symbolic link target if applicable and
 *                              available, otherwise NULL.
 */
static int rtFtpServerDataConnDirCollAddEntry(PRTFTPDIRCOLLECTION pCollection, const char *pszEntry, PRTFSOBJINFO pInfo,
                                              const char *pszOwner, const char *pszGroup, const char *pszTarget)
{
    /* Filter out entries we don't want to report to the client, even if they were reported by the actual implementation. */
    if (   !RTStrCmp(pszEntry, ".")
        || !RTStrCmp(pszEntry, ".."))
    {
        return VINF_SUCCESS;
    }

    /* Anything else besides files and directores is not allowed; just don't show them at all for the moment. */
    switch (pInfo->Attr.fMode & RTFS_TYPE_MASK)
    {
        case RTFS_TYPE_DIRECTORY:
            RT_FALL_THROUGH();
        case RTFS_TYPE_FILE:
            break;

        default:
            return VINF_SUCCESS;
    }

    /* Make sure there is space in the collection for the new entry. */
    if (pCollection->cEntries >= pCollection->cEntriesAllocated)
    {
        size_t cNew = pCollection->cEntriesAllocated ? pCollection->cEntriesAllocated * 2 : 16;
        void *pvNew = RTMemRealloc(pCollection->papEntries, cNew * sizeof(pCollection->papEntries[0]));
        if (!pvNew)
            return VERR_NO_MEMORY;
        pCollection->papEntries        = (PPRTFTPDIRENTRY)pvNew;
        pCollection->cEntriesAllocated = cNew;
    }

    /* Create and insert a new entry. */
    size_t const cchEntry = strlen(pszEntry);
    size_t const cbOwner  = pszOwner  ? strlen(pszOwner)  + 1 : 0;
    size_t const cbGroup  = pszGroup  ? strlen(pszGroup)  + 1 : 0;
    size_t const cbTarget = pszTarget ? strlen(pszTarget) + 1 : 0;
    size_t const cbEntry  = RT_UOFFSETOF_DYN(RTFTPDIRENTRY, szName[cchEntry + 1 + cbOwner + cbGroup + cbTarget]);
    PRTFTPDIRENTRY pEntry = (PRTFTPDIRENTRY)RTMemAlloc(cbEntry);
    if (pEntry)
    {
        pEntry->Info      = *pInfo;
        pEntry->pszTarget = NULL; /** @todo symbolic links. */
        pEntry->pszOwner  = NULL;
        pEntry->pszGroup  = NULL;
        pEntry->cchName   = cchEntry;
        memcpy(pEntry->szName, pszEntry, cchEntry);
        pEntry->szName[cchEntry] = '\0';

        char *psz = &pEntry->szName[cchEntry + 1];
        if (pszTarget)
        {
            pEntry->pszTarget = psz;
            memcpy(psz, pszTarget, cbTarget);
            psz += cbTarget;
        }
        if (pszOwner)
        {
            pEntry->pszOwner = psz;
            memcpy(psz, pszOwner, cbOwner);
            psz += cbOwner;
        }
        if (pszGroup)
        {
            pEntry->pszGroup = psz;
            memcpy(psz, pszGroup, cbGroup);
        }

        pCollection->papEntries[pCollection->cEntries++] = pEntry;
        pCollection->cbTotalAllocated += pEntry->Info.cbAllocated;
        pCollection->cbTotalFiles     += pEntry->Info.cbObject;
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;
}

/** @callback_method_impl{FNRTSORTCMP, Name} */
static DECLCALLBACK(int) rtFtpServerCollEntryCmpName(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    RT_NOREF(pvUser);
    PRTFTPDIRENTRY pEntry1 = (PRTFTPDIRENTRY)pvElement1;
    PRTFTPDIRENTRY pEntry2 = (PRTFTPDIRENTRY)pvElement2;
    return RTStrCmp(pEntry1->szName, pEntry2->szName);
}

/** @callback_method_impl{FNRTSORTCMP, Dirs first + Name} */
static DECLCALLBACK(int) rtFtpServerCollEntryCmpDirFirstName(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    RT_NOREF(pvUser);
    PRTFTPDIRENTRY pEntry1 = (PRTFTPDIRENTRY)pvElement1;
    PRTFTPDIRENTRY pEntry2 = (PRTFTPDIRENTRY)pvElement2;
    int iDiff = !RTFS_IS_DIRECTORY(pEntry1->Info.Attr.fMode) - !RTFS_IS_DIRECTORY(pEntry2->Info.Attr.fMode);
    if (!iDiff)
        iDiff = rtFtpServerCollEntryCmpName(pEntry1, pEntry2, pvUser);
    return iDiff;
}

/**
 * Sorts a given directory collection according to the FTP server's LIST style.
 *
 * @param   pCollection         Collection to sort.
 */
static void rtFtpServerCollSort(PRTFTPDIRCOLLECTION pCollection)
{
    PFNRTSORTCMP pfnCmp = rtFtpServerCollEntryCmpDirFirstName;
    if (pfnCmp)
        RTSortApvShell((void **)pCollection->papEntries, pCollection->cEntries, pfnCmp, NULL);
}

/**
 * Writes a directory collection to a specific data connection.
 *
 * @returns VBox status code.
 * @param   pDataConn           Data connection to write directory collection to.
 * @param   pCollection         Collection to write.
 * @param   pszTmp              Temporary buffer used for writing.
 * @param   cbTmp               Size (in bytes) of temporary buffer used for writing.
 */
static int rtFtpServerDataConnDirCollWrite(PRTFTPSERVERDATACONN pDataConn, PRTFTPDIRCOLLECTION pCollection,
                                           char *pszTmp, size_t cbTmp)
{
    size_t cchSizeCol  = 4;
    size_t cchLinkCol  = 1;
    size_t cchUidCol   = 1;
    size_t cchGidCol   = 1;

    size_t i = pCollection->cEntries;
    while (i-- > 0)
    {
        PRTFTPDIRENTRY pEntry = pCollection->papEntries[i];

        rtFtpServerFormatSize(pEntry->Info.cbObject, pszTmp, cbTmp);
        size_t cchTmp = strlen(pszTmp);
        if (cchTmp > cchSizeCol)
            cchSizeCol = cchTmp;

        cchTmp = rtFtpServerDecimalFormatLengthU32(pEntry->Info.Attr.u.Unix.cHardlinks) + 1;
        if (cchTmp > cchLinkCol)
            cchLinkCol = cchTmp;

        rtFtpServerDecimalFormatOwner(pEntry->Info.Attr.u.Unix.uid, pEntry->pszOwner, pszTmp, cbTmp);
        cchTmp = strlen(pszTmp);
        if (cchTmp > cchUidCol)
            cchUidCol = cchTmp;

        rtFtpServerDecimalFormatGroup(pEntry->Info.Attr.u.Unix.gid, pEntry->pszGroup, pszTmp, cbTmp);
        cchTmp = strlen(pszTmp);
        if (cchTmp > cchGidCol)
            cchGidCol = cchTmp;
    }

    size_t offTime = RT_UOFFSETOF(RTFTPDIRENTRY, Info.ModificationTime);

    /*
     * Display the entries.
     */
    for (i = 0; i < pCollection->cEntries; i++)
    {
        PRTFTPDIRENTRY pEntry = pCollection->papEntries[i];

        RTFMODE fMode = pEntry->Info.Attr.fMode;
        switch (fMode & RTFS_TYPE_MASK)
        {
            case RTFS_TYPE_FIFO:        rtFtpServerDataConnPrintf(pDataConn, "f"); break;
            case RTFS_TYPE_DEV_CHAR:    rtFtpServerDataConnPrintf(pDataConn, "c"); break;
            case RTFS_TYPE_DIRECTORY:   rtFtpServerDataConnPrintf(pDataConn, "d"); break;
            case RTFS_TYPE_DEV_BLOCK:   rtFtpServerDataConnPrintf(pDataConn, "b"); break;
            case RTFS_TYPE_FILE:        rtFtpServerDataConnPrintf(pDataConn, "-"); break;
            case RTFS_TYPE_SYMLINK:     rtFtpServerDataConnPrintf(pDataConn, "l"); break;
            case RTFS_TYPE_SOCKET:      rtFtpServerDataConnPrintf(pDataConn, "s"); break;
            case RTFS_TYPE_WHITEOUT:    rtFtpServerDataConnPrintf(pDataConn, "w"); break;
            default:                    rtFtpServerDataConnPrintf(pDataConn, "?"); AssertFailed(); break;
        }

        rtFtpServerDataConnPrintf(pDataConn, "%c%c%c",
                                  fMode & RTFS_UNIX_IRUSR ? 'r' : '-',
                                  fMode & RTFS_UNIX_IWUSR ? 'w' : '-',
                                  fMode & RTFS_UNIX_IXUSR ? 'x' : '-');
        rtFtpServerDataConnPrintf(pDataConn, "%c%c%c",
                                  fMode & RTFS_UNIX_IRGRP ? 'r' : '-',
                                  fMode & RTFS_UNIX_IWGRP ? 'w' : '-',
                                  fMode & RTFS_UNIX_IXGRP ? 'x' : '-');
        rtFtpServerDataConnPrintf(pDataConn, "%c%c%c",
                                  fMode & RTFS_UNIX_IROTH ? 'r' : '-',
                                  fMode & RTFS_UNIX_IWOTH ? 'w' : '-',
                                  fMode & RTFS_UNIX_IXOTH ? 'x' : '-');

        rtFtpServerDataConnPrintf(pDataConn, " %*u",
                                  cchLinkCol, pEntry->Info.Attr.u.Unix.cHardlinks);

        if (cchUidCol)
            rtFtpServerDataConnPrintf(pDataConn, " %*s", cchUidCol,
                                      rtFtpServerDecimalFormatOwner(pEntry->Info.Attr.u.Unix.uid, pEntry->pszOwner, pszTmp, cbTmp));
        if (cchGidCol)
            rtFtpServerDataConnPrintf(pDataConn, " %*s", cchGidCol,
                                      rtFtpServerDecimalFormatGroup(pEntry->Info.Attr.u.Unix.gid, pEntry->pszGroup, pszTmp, cbTmp));

        rtFtpServerDataConnPrintf(pDataConn, "%*s", cchSizeCol, rtFtpServerFormatSize(pEntry->Info.cbObject, pszTmp, cbTmp));

        PCRTTIMESPEC pTime = (PCRTTIMESPEC)((uintptr_t)pEntry + offTime);
        rtFtpServerDataConnPrintf(pDataConn," %s", rtFtpServerFormatTimestamp(pTime, pszTmp, cbTmp));

        rtFtpServerDataConnPrintf(pDataConn," %s\r\n", rtFtpServerFormatName(pEntry->szName, pszTmp, cbTmp));
    }

    return VINF_SUCCESS;
}

/**
 * Thread for handling the LIST command's output in a separate data connection.
 *
 * @returns VBox status code.
 * @param   ThreadSelf          Thread handle. Unused.
 * @param   pvUser              User-provided arguments. Of type PRTFTPSERVERCLIENT.
 */
static DECLCALLBACK(int) rtFtpServerDataConnListThread(RTTHREAD ThreadSelf, void *pvUser)
{
    RT_NOREF(ThreadSelf);

    PRTFTPSERVERCLIENT pClient = (PRTFTPSERVERCLIENT)pvUser;
    AssertPtr(pClient);

    PRTFTPSERVERDATACONN pDataConn = pClient->pDataConn;
    AssertPtr(pDataConn);

    LogFlowFuncEnter();

    int rc;

    char szTmp[RTPATH_MAX * 2];
    PRTFTPDIRCOLLECTION pColl = rtFtpServerDataConnDirCollAlloc();
    AssertPtrReturn(pColl, VERR_NO_MEMORY);

    /* Set start indicator. */
    pDataConn->fStarted = true;

    RTThreadUserSignal(RTThreadSelf());

    /* The first argument might indicate a directory to list.
     * If no argument is given, the implementation must use the last directory set. */
    char *pszPath = RTStrDup(  pDataConn->cArgs == 1
                             ? pDataConn->papszArgs[0] : pDataConn->pClient->State.pszCWD); /** @todo Needs locking. */
    AssertPtrReturnStmt(pszPath, rtFtpServerDataConnDirCollFree(pColl), VERR_NO_MEMORY);
    /* The paths already have been validated in the actual command handlers. */

    void *pvHandle = NULL; /* Shut up MSVC. */
    RTFTPSERVER_HANDLE_CALLBACK_VA(pfnOnDirOpen, pszPath, &pvHandle);

    for (;;)
    {
        RTFSOBJINFO objInfo;
        RT_ZERO(objInfo);

        char *pszEntry  = NULL;
        char *pszOwner  = NULL;
        char *pszGroup  = NULL;
        char *pszTarget = NULL;

        RTFTPSERVER_HANDLE_CALLBACK_VA(pfnOnDirRead, pvHandle, &pszEntry,
                                       &objInfo, &pszOwner, &pszGroup, &pszTarget);
        if (RT_SUCCESS(rc))
        {
            int rc2 = rtFtpServerDataConnDirCollAddEntry(pColl, pszEntry,
                                                         &objInfo, pszOwner, pszGroup, pszTarget);

            RTStrFree(pszEntry);
            pszEntry = NULL;

            RTStrFree(pszOwner);
            pszOwner = NULL;

            RTStrFree(pszGroup);
            pszGroup = NULL;

            RTStrFree(pszTarget);
            pszTarget = NULL;

            if (RT_SUCCESS(rc))
                rc = rc2;
        }
        else
        {
            if (rc == VERR_NO_MORE_FILES)
            {
                rc = VINF_SUCCESS;
                break;
            }
        }

        if (RT_FAILURE(rc))
            break;

        if (ASMAtomicReadBool(&pDataConn->fStop))
            break;
    }

    RTFTPSERVER_HANDLE_CALLBACK_VA(pfnOnDirClose, pvHandle);
    pvHandle = NULL;

    rtFtpServerCollSort(pColl);

    if (RT_SUCCESS(rc))
    {
        int rc2 = rtFtpServerDataConnDirCollWrite(pDataConn, pColl, szTmp, sizeof(szTmp));
        AssertRC(rc2);
    }

    rtFtpServerDataConnDirCollFree(pColl);

    RTStrFree(pszPath);

    pDataConn->fStopped = true;
    pDataConn->rc       = rc;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

static DECLCALLBACK(int) rtFtpServerHandleLIST(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apszArgs)
{
    /* If no argument is given, use the server's CWD as the path. */
    const char *pszPath = cArgs ? apszArgs[0] : pClient->State.pszCWD;
    AssertPtr(pszPath);

    int rc = VINF_SUCCESS;

    if (!rtFtpServerPathIsValid(pszPath, false /* fIsAbsolute */))
    {
        int rc2 = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_CONN_REQ_FILE_ACTION_NOT_TAKEN);
        AssertRC(rc2);
    }
    else
    {
        RTFTPSERVER_HANDLE_CALLBACK_VA(pfnOnFileStat, pszPath, NULL /* PRTFSOBJINFO */);

        if (RT_SUCCESS(rc))
        {
            if (pClient->pDataConn == NULL)
            {
                rc = rtFtpServerDataConnCreate(pClient, &pClient->pDataConn);
                if (RT_SUCCESS(rc))
                    rc = rtFtpServerDataConnStart(pClient->pDataConn, rtFtpServerDataConnListThread, cArgs, apszArgs);

                int rc2 = rtFtpServerSendReplyRc(  pClient, RT_SUCCESS(rc)
                                                 ? RTFTPSERVER_REPLY_DATACONN_ALREADY_OPEN
                                                 : RTFTPSERVER_REPLY_CANT_OPEN_DATA_CONN);
                AssertRC(rc2);
            }
            else
            {
                 int rc2 = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_DATACONN_ALREADY_OPEN);
                 AssertRC(rc2);
            }
        }
        else
        {
            int rc2 = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_CONN_REQ_FILE_ACTION_NOT_TAKEN);
            AssertRC(rc2);
        }
    }

    return rc;
}

static DECLCALLBACK(int) rtFtpServerHandleMODE(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apszArgs)
{
    RT_NOREF(pClient, cArgs, apszArgs);

    /** @todo Anything to do here? */
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) rtFtpServerHandleNOOP(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apszArgs)
{
    RT_NOREF(cArgs, apszArgs);

    /* Save timestamp of last command sent. */
    pClient->State.tsLastCmdMs = RTTimeMilliTS();

    return rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_OKAY);
}

static DECLCALLBACK(int) rtFtpServerHandlePASS(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apszArgs)
{
    if (cArgs != 1)
        return rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_ERROR_INVALID_PARAMETERS);

    const char *pszPassword = apszArgs[0];
    AssertPtrReturn(pszPassword, VERR_INVALID_PARAMETER);

    int rc = rtFtpServerAuthenticate(pClient, pClient->State.pszUser, pszPassword);
    if (RT_SUCCESS(rc))
    {
        rc = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_LOGGED_IN_PROCEED);
    }
    else
    {
        pClient->State.cFailedLoginAttempts++;

        int rc2 = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_NOT_LOGGED_IN);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    return rc;
}

static DECLCALLBACK(int) rtFtpServerHandlePORT(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apszArgs)
{
    if (cArgs != 1)
        return rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_ERROR_INVALID_PARAMETERS);

    RTFTPSERVER_REPLY rcClient;

    int rc = rtFtpParseHostAndPort(apszArgs[0], &pClient->DataConnAddr, &pClient->uDataConnPort);
    if (RT_SUCCESS(rc))
        rcClient = RTFTPSERVER_REPLY_OKAY;
    else
        rcClient = RTFTPSERVER_REPLY_ERROR_INVALID_PARAMETERS;

    int rc2 = rtFtpServerSendReplyRc(pClient, rcClient);
    if (RT_SUCCESS(rc))
        rc = rc2;

    return rc;
}

static DECLCALLBACK(int) rtFtpServerHandlePWD(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apszArgs)
{
    RT_NOREF(cArgs, apszArgs);

    int rc;

    char szPWD[RTPATH_MAX];

    RTFTPSERVER_HANDLE_CALLBACK_VA(pfnOnPathGetCurrent, szPWD, sizeof(szPWD));

    if (RT_SUCCESS(rc))
       rc = rtFtpServerSendReplyRcEx(pClient, RTFTPSERVER_REPLY_PATHNAME_OK, "\"%s\"", szPWD); /* See RFC 959, APPENDIX II. */

    return rc;
}

static DECLCALLBACK(int) rtFtpServerHandleOPTS(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apszArgs)
{
    RT_NOREF(cArgs, apszArgs);

    int rc = VINF_SUCCESS;

    int rc2 = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_OKAY);
    if (RT_SUCCESS(rc))
        rc = rc2;

    return rc;
}

static DECLCALLBACK(int) rtFtpServerHandleQUIT(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apszArgs)
{
    RT_NOREF(cArgs, apszArgs);

    int rc = VINF_SUCCESS;

    if (pClient->pDataConn)
    {
        rc = rtFtpServerDataConnClose(pClient->pDataConn);
        if (RT_SUCCESS(rc))
        {
            rtFtpServerDataConnDestroy(pClient->pDataConn);
            pClient->pDataConn = NULL;
        }
    }

    int rc2 = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_OKAY);
    if (RT_SUCCESS(rc))
        rc = rc2;

    return rc;
}

static DECLCALLBACK(int) rtFtpServerHandleRETR(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apszArgs)
{
    if (cArgs != 1) /* File name needs to be present. */
        return VERR_INVALID_PARAMETER;

    int rc;

    const char *pszPath = apszArgs[0];

    RTFTPSERVER_HANDLE_CALLBACK_VA(pfnOnFileStat, pszPath, NULL /* PRTFSOBJINFO */);

    if (RT_SUCCESS(rc))
    {
        if (RT_SUCCESS(rc))
        {
            if (pClient->pDataConn == NULL)
            {
                rc = rtFtpServerDataConnCreate(pClient, &pClient->pDataConn);
                if (RT_SUCCESS(rc))
                    rc = rtFtpServerDataConnStart(pClient->pDataConn, rtFtpServerDataConnFileWriteThread, cArgs, apszArgs);

                int rc2 = rtFtpServerSendReplyRc(  pClient, RT_SUCCESS(rc)
                                                 ? RTFTPSERVER_REPLY_DATACONN_ALREADY_OPEN
                                                 : RTFTPSERVER_REPLY_CANT_OPEN_DATA_CONN);
                AssertRC(rc2);
            }
            else
            {
                 int rc2 = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_DATACONN_ALREADY_OPEN);
                 AssertRC(rc2);
            }
        }
        else
        {
            int rc2 = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_CONN_REQ_FILE_ACTION_NOT_TAKEN);
            AssertRC(rc2);
        }
    }

    if (RT_FAILURE(rc))
    {
        int rc2 = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_REQ_ACTION_NOT_TAKEN);
        AssertRC(rc2);
    }

    return rc;
}

static DECLCALLBACK(int) rtFtpServerHandleSIZE(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apszArgs)
{
    if (cArgs != 1)
        return VERR_INVALID_PARAMETER;

    int rc;

    const char *pszPath = apszArgs[0];
    uint64_t uSize = 0;

    RTFTPSERVER_HANDLE_CALLBACK_VA(pfnOnFileGetSize, pszPath, &uSize);

    if (RT_SUCCESS(rc))
    {
        rc = rtFtpServerSendReplyStr(pClient, "213 %RU64\r\n", uSize);
    }
    else
    {
        int rc2 = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_REQ_ACTION_NOT_TAKEN);
        AssertRC(rc2);
    }

    return rc;
}

static DECLCALLBACK(int) rtFtpServerHandleSTAT(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apszArgs)
{
    if (cArgs != 1)
        return VERR_INVALID_PARAMETER;

    int rc;

    RTFSOBJINFO objInfo;
    RT_ZERO(objInfo);

    const char *pszPath = apszArgs[0];

    RTFTPSERVER_HANDLE_CALLBACK_VA(pfnOnFileStat, pszPath, &objInfo);

    if (RT_SUCCESS(rc))
    {
        char szFsObjInfo[_4K]; /** @todo Check this size. */
        rc = rtFtpServerFsObjInfoToStr(&objInfo, szFsObjInfo, sizeof(szFsObjInfo));
        if (RT_SUCCESS(rc))
        {
            char szFsPathInfo[RTPATH_MAX + 16];
            const ssize_t cchPathInfo = RTStrPrintf2(szFsPathInfo, sizeof(szFsPathInfo), " %2zu %s\n", strlen(pszPath), pszPath);
            if (cchPathInfo > 0)
            {
                rc = RTStrCat(szFsObjInfo, sizeof(szFsObjInfo), szFsPathInfo);
                if (RT_SUCCESS(rc))
                    rc = rtFtpServerSendReplyStr(pClient, szFsObjInfo);
            }
            else
                rc = VERR_BUFFER_OVERFLOW;
        }
    }

    if (RT_FAILURE(rc))
    {
        int rc2 = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_REQ_ACTION_NOT_TAKEN);
        AssertRC(rc2);
    }

    return rc;
}

static DECLCALLBACK(int) rtFtpServerHandleSTRU(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apszArgs)
{
    if (cArgs != 1)
        return VERR_INVALID_PARAMETER;

    const char *pszType = apszArgs[0];

    int rc;

    if (!RTStrICmp(pszType, "F"))
    {
        pClient->State.enmStructType = RTFTPSERVER_STRUCT_TYPE_FILE;

        rc = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_OKAY);
    }
    else
        rc = VERR_NOT_IMPLEMENTED;

    return rc;
}

static DECLCALLBACK(int) rtFtpServerHandleSYST(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apszArgs)
{
    RT_NOREF(cArgs, apszArgs);

    char szOSInfo[64];
    int rc = RTSystemQueryOSInfo(RTSYSOSINFO_PRODUCT, szOSInfo, sizeof(szOSInfo));
    if (RT_SUCCESS(rc))
        rc = rtFtpServerSendReplyStr(pClient, "215 %s", szOSInfo);

    return rc;
}

static DECLCALLBACK(int) rtFtpServerHandleTYPE(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apszArgs)
{
    if (cArgs != 1)
        return VERR_INVALID_PARAMETER;

    const char *pszType = apszArgs[0];

    int rc = VINF_SUCCESS;

    if (!RTStrICmp(pszType, "A"))
    {
        pClient->State.enmDataType = RTFTPSERVER_DATA_TYPE_ASCII;
    }
    else if (!RTStrICmp(pszType, "I")) /* Image (binary). */
    {
        pClient->State.enmDataType = RTFTPSERVER_DATA_TYPE_IMAGE;
    }
    else /** @todo Support "E" (EBCDIC) and/or "L <size>" (custom)? */
        rc = VERR_NOT_IMPLEMENTED;

    if (RT_SUCCESS(rc))
        rc = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_OKAY);

    return rc;
}

static DECLCALLBACK(int) rtFtpServerHandleUSER(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apszArgs)
{
    if (cArgs != 1)
        return VERR_INVALID_PARAMETER;

    const char *pszUser = apszArgs[0];
    AssertPtrReturn(pszUser, VERR_INVALID_PARAMETER);

    rtFtpServerClientStateReset(&pClient->State);

    int rc = rtFtpServerLookupUser(pClient, pszUser);
    if (RT_SUCCESS(rc))
    {
        pClient->State.pszUser = RTStrDup(pszUser);
        AssertPtrReturn(pClient->State.pszUser, VERR_NO_MEMORY);

        rc = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_USERNAME_OKAY_NEED_PASSWORD);
    }
    else
    {
        pClient->State.cFailedLoginAttempts++;

        int rc2 = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_NOT_LOGGED_IN);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    return rc;
}


/*********************************************************************************************************************************
*   Internal server functions                                                                                                    *
*********************************************************************************************************************************/

/**
 * Parses FTP command arguments handed in by the client.
 *
 * @returns VBox status code.
 * @param   pszCmdParms         Pointer to command arguments, if any. Can be NULL if no arguments are given.
 * @param   pcArgs              Returns the number of parsed arguments, separated by a space (hex 0x20).
 * @param   ppapszArgs          Returns the string array of parsed arguments. Needs to be free'd with rtFtpServerCmdArgsFree().
 */
static int rtFtpServerCmdArgsParse(const char *pszCmdParms, uint8_t *pcArgs, char ***ppapszArgs)
{
    *pcArgs      = 0;
    *ppapszArgs = NULL;

    if (!pszCmdParms) /* No parms given? Bail out early. */
        return VINF_SUCCESS;

    /** @todo Anything else to do here? */
    /** @todo Check if quoting is correct. */

    int cArgs = 0;
    int rc = RTGetOptArgvFromString(ppapszArgs, &cArgs, pszCmdParms, RTGETOPTARGV_CNV_QUOTE_MS_CRT, " " /* Separators */);
    if (RT_SUCCESS(rc))
    {
        if ((unsigned int)cArgs <= (unsigned int)UINT8_MAX)
        {
            *pcArgs = (uint8_t)cArgs;
        }
        else
            rc = VERR_INVALID_PARAMETER;
    }

    return rc;
}

/**
 * Frees a formerly argument string array parsed by rtFtpServerCmdArgsParse().
 *
 * @param   ppapszArgs          Argument string array to free.
 */
static void rtFtpServerCmdArgsFree(char **ppapszArgs)
{
    RTGetOptArgvFree(ppapszArgs);
}

/**
 * Main function for processing client commands for the control connection.
 *
 * @returns VBox status code.
 * @param   pClient             Client to process commands for.
 * @param   pszCmd              Command string to parse and handle.
 * @param   cbCmd               Size (in bytes) of command string.
 */
static int rtFtpServerProcessCommands(PRTFTPSERVERCLIENT pClient, char *pszCmd, size_t cbCmd)
{
    /* Make sure to terminate the string in any case. */
    pszCmd[RT_MIN(RTFTPSERVER_MAX_CMD_LEN, cbCmd)] = '\0';

    /* A tiny bit of sanitation. */
    RTStrStripL(pszCmd);

    /* First, terminate string by finding the command end marker (telnet style). */
    /** @todo Not sure if this is entirely correct and/or needs tweaking; good enough for now as it seems. */
    char *pszCmdEnd = RTStrStr(pszCmd, "\r\n");
    if (pszCmdEnd)
        *pszCmdEnd = '\0';

    /* Reply which gets sent back to the client. */
    RTFTPSERVER_REPLY rcClient = RTFTPSERVER_REPLY_INVALID;

    int rcCmd = VINF_SUCCESS;

    uint8_t cArgs     = 0;
    char  **papszArgs = NULL;
    int rc = rtFtpServerCmdArgsParse(pszCmd, &cArgs, &papszArgs);
    if (   RT_SUCCESS(rc)
        && cArgs) /* At least the actual command (without args) must be present. */
    {
        LogFlowFunc(("Handling command '%s'\n", papszArgs[0]));
        for (uint8_t a = 0; a < cArgs; a++)
            LogFlowFunc(("\targ[%RU8] = '%s'\n", a, papszArgs[a]));

        unsigned i = 0;
        for (; i < RT_ELEMENTS(g_aCmdMap); i++)
        {
            const RTFTPSERVERCMD_ENTRY *pCmdEntry = &g_aCmdMap[i];

            if (!RTStrICmp(papszArgs[0], pCmdEntry->szCmd))
            {
                /* Some commands need a valid user before they can be executed. */
                if (   pCmdEntry->fNeedsUser
                    && pClient->State.pszUser == NULL)
                {
                    rcClient = RTFTPSERVER_REPLY_NOT_LOGGED_IN;
                    break;
                }

                /* Save timestamp of last command sent. */
                pClient->State.tsLastCmdMs = RTTimeMilliTS();

                /* Hand in arguments only without the actual command. */
                rcCmd = pCmdEntry->pfnCmd(pClient, cArgs - 1, cArgs > 1 ? &papszArgs[1] : NULL);
                if (RT_FAILURE(rcCmd))
                {
                    LogFunc(("Handling command '%s' failed with %Rrc\n", papszArgs[0], rcCmd));

                    switch (rcCmd)
                    {
                        case VERR_INVALID_PARAMETER:
                            RT_FALL_THROUGH();
                        case VERR_INVALID_POINTER:
                            rcClient = RTFTPSERVER_REPLY_ERROR_INVALID_PARAMETERS;
                            break;

                        case VERR_NOT_IMPLEMENTED:
                            rcClient = RTFTPSERVER_REPLY_ERROR_CMD_NOT_IMPL;
                            break;

                        default:
                            break;
                    }
                }
                break;
            }
        }

        rtFtpServerCmdArgsFree(papszArgs);

        if (i == RT_ELEMENTS(g_aCmdMap))
        {
            LogFlowFunc(("Command not implemented\n"));
            Assert(rcClient == RTFTPSERVER_REPLY_INVALID);
            rcClient = RTFTPSERVER_REPLY_ERROR_CMD_NOT_IMPL;
        }

        const bool fDisconnect =    g_aCmdMap[i].enmCmd == RTFTPSERVERCMD_QUIT
                                 || pClient->State.cFailedLoginAttempts >= 3; /** @todo Make this dynamic. */
        if (fDisconnect)
        {
            RTFTPSERVER_HANDLE_CALLBACK_VA(pfnOnUserDisconnect, pClient->State.pszUser);

            rtFtpServerClientStateReset(&pClient->State);

            Assert(rcClient == RTFTPSERVER_REPLY_INVALID);
            rcClient = RTFTPSERVER_REPLY_CLOSING_CTRL_CONN;
        }
    }
    else
        rcClient = RTFTPSERVER_REPLY_ERROR_INVALID_PARAMETERS;

    if (rcClient != RTFTPSERVER_REPLY_INVALID)
    {
        int rc2 = rtFtpServerSendReplyRc(pClient, rcClient);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Main loop for processing client commands.
 *
 * @returns VBox status code.
 * @param   pClient             Client to process commands for.
 */
static int rtFtpServerClientMain(PRTFTPSERVERCLIENT pClient)
{
    int rc;

    size_t cbRead;
    char   szCmd[RTFTPSERVER_MAX_CMD_LEN + 1];

    for (;;)
    {
        rc = RTTcpSelectOne(pClient->hSocket, 200 /* ms */); /** @todo Can we improve here? Using some poll events or so? */
        if (RT_SUCCESS(rc))
        {
            rc = RTTcpReadNB(pClient->hSocket, szCmd, sizeof(szCmd), &cbRead);
            if (   RT_SUCCESS(rc)
                && cbRead)
            {
                AssertBreakStmt(cbRead <= sizeof(szCmd), rc = VERR_BUFFER_OVERFLOW);
                rc = rtFtpServerProcessCommands(pClient, szCmd, cbRead);
            }
        }
        else if (rc == VERR_TIMEOUT)
            rc = VINF_SUCCESS;
        else
            break;

        /*
         * Handle data connection replies.
         */
        if (pClient->pDataConn)
        {
            if (   ASMAtomicReadBool(&pClient->pDataConn->fStarted)
                && ASMAtomicReadBool(&pClient->pDataConn->fStopped))
            {
                Assert(pClient->pDataConn->rc != VERR_IPE_UNINITIALIZED_STATUS);

                int rc2 = rtFtpServerSendReplyRc(pClient,
                                                   RT_SUCCESS(pClient->pDataConn->rc)
                                                 ? RTFTPSERVER_REPLY_CLOSING_DATA_CONN : RTFTPSERVER_REPLY_CONN_REQ_FILE_ACTION_NOT_TAKEN);
                AssertRC(rc2);

                rc = rtFtpServerDataConnStop(pClient->pDataConn);
                if (RT_SUCCESS(rc))
                {
                    rtFtpServerDataConnDestroy(pClient->pDataConn);
                    pClient->pDataConn = NULL;
                }
            }
        }
    }

    /* Make sure to destroy all data connections. */
    rtFtpServerDataConnDestroy(pClient->pDataConn);
    pClient->pDataConn = NULL;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Resets the client's state.
 *
 * @param   pState              Client state to reset.
 */
static void rtFtpServerClientStateReset(PRTFTPSERVERCLIENTSTATE pState)
{
    LogFlowFuncEnter();

    RTStrFree(pState->pszUser);
    pState->pszUser = NULL;

    int rc2 = rtFtpSetCWD(pState, "/");
    AssertRC(rc2);

    pState->cFailedLoginAttempts = 0;
    pState->tsLastCmdMs          = RTTimeMilliTS();
    pState->enmDataType          = RTFTPSERVER_DATA_TYPE_ASCII;
    pState->enmStructType        = RTFTPSERVER_STRUCT_TYPE_FILE;
}

/**
 * Per-client thread for serving the server's control connection.
 *
 * @returns VBox status code.
 * @param   hSocket             Socket handle to use for the control connection.
 * @param   pvUser              User-provided arguments. Of type PRTFTPSERVERINTERNAL.
 */
static DECLCALLBACK(int) rtFtpServerClientThread(RTSOCKET hSocket, void *pvUser)
{
    PRTFTPSERVERINTERNAL pThis = (PRTFTPSERVERINTERNAL)pvUser;
    RTFTPSERVER_VALID_RETURN(pThis);

    RTFTPSERVERCLIENT Client;
    RT_ZERO(Client);

    Client.pServer     = pThis;
    Client.hSocket     = hSocket;

    LogFlowFunc(("New client connected\n"));

    rtFtpServerClientStateReset(&Client.State);

    /*
     * Send welcome message.
     * Note: Some clients (like FileZilla / Firefox) expect a message together with the reply code,
     *       so make sure to include at least *something*.
     */
    int rc = rtFtpServerSendReplyRcEx(&Client, RTFTPSERVER_REPLY_READY_FOR_NEW_USER,
                                     "Welcome!");
    if (RT_SUCCESS(rc))
    {
        ASMAtomicIncU32(&pThis->cClients);

        rc = rtFtpServerClientMain(&Client);

        ASMAtomicDecU32(&pThis->cClients);
    }

    rtFtpServerClientStateReset(&Client.State);

    return rc;
}

RTR3DECL(int) RTFtpServerCreate(PRTFTPSERVER phFTPServer, const char *pszAddress, uint16_t uPort,
                                PRTFTPSERVERCALLBACKS pCallbacks, void *pvUser, size_t cbUser)
{
    AssertPtrReturn(phFTPServer,  VERR_INVALID_POINTER);
    AssertPtrReturn(pszAddress,  VERR_INVALID_POINTER);
    AssertReturn   (uPort,        VERR_INVALID_PARAMETER);
    AssertPtrReturn(pCallbacks,   VERR_INVALID_POINTER);
    /* pvUser is optional. */

    int rc;

    PRTFTPSERVERINTERNAL pThis = (PRTFTPSERVERINTERNAL)RTMemAllocZ(sizeof(RTFTPSERVERINTERNAL));
    if (pThis)
    {
        pThis->u32Magic  = RTFTPSERVER_MAGIC;
        pThis->Callbacks = *pCallbacks;
        pThis->pvUser    = pvUser;
        pThis->cbUser    = cbUser;

        rc = RTTcpServerCreate(pszAddress, uPort, RTTHREADTYPE_DEFAULT, "ftpsrv",
                               rtFtpServerClientThread, pThis /* pvUser */, &pThis->pTCPServer);
        if (RT_SUCCESS(rc))
        {
            *phFTPServer = (RTFTPSERVER)pThis;
        }
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

RTR3DECL(int) RTFtpServerDestroy(RTFTPSERVER hFTPServer)
{
    if (hFTPServer == NIL_RTFTPSERVER)
        return VINF_SUCCESS;

    PRTFTPSERVERINTERNAL pThis = hFTPServer;
    RTFTPSERVER_VALID_RETURN(pThis);

    AssertPtr(pThis->pTCPServer);

    int rc = RTTcpServerDestroy(pThis->pTCPServer);
    if (RT_SUCCESS(rc))
    {
        pThis->u32Magic = RTFTPSERVER_MAGIC_DEAD;

        RTMemFree(pThis);
    }

    return rc;
}

