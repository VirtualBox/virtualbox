/* $Id: GuestCtrlPrivate.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Internal helpers/structures for guest control functionality.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_GUEST_CONTROL
#include "LoggingNew.h"

#ifndef VBOX_WITH_GUEST_CONTROL
# error "VBOX_WITH_GUEST_CONTROL must defined in this file"
#endif
#include "GuestCtrlImplPrivate.h"
#include "GuestSessionImpl.h"
#include "VMMDev.h"

#include <iprt/asm.h>
#include <iprt/cpp/utils.h> /* For unconst(). */
#include <iprt/ctype.h>
#ifdef DEBUG
# include <iprt/file.h>
#endif
#include <iprt/fs.h>
#include <iprt/path.h>
#include <iprt/rand.h>
#include <iprt/time.h>
#include <VBox/AssertGuest.h>


/**
 * Returns a stringyfied error of a guest fs error.
 *
 * @returns Stringyfied error.
 * @param   guestErrorInfo      Guest error info to get stringyfied error for.
 */
/* static */
Utf8Str GuestFs::guestErrorToString(const GuestErrorInfo &guestErrorInfo)
{
    Utf8Str strErr;

    /** @todo pData->u32Flags: int vs. uint32 -- IPRT errors are *negative* !!! */
    switch (guestErrorInfo.getVrc())
    {
        case VERR_ACCESS_DENIED:
            strErr.printf(tr("Access to \"%s\" denied"), guestErrorInfo.getWhat().c_str());
            break;

        case VERR_FILE_NOT_FOUND: /* This is the most likely error. */
            RT_FALL_THROUGH();
        case VERR_PATH_NOT_FOUND:
            strErr.printf(tr("No such file or directory \"%s\""), guestErrorInfo.getWhat().c_str());
            break;

        case VERR_CANT_CREATE:
            strErr.printf(tr("File or directory \"%s\" can't be created"), guestErrorInfo.getWhat().c_str());
            break;

        case VERR_INVALID_PARAMETER:
            strErr.printf(tr("Invalid parameter specified"));
            break;

        case VERR_INVALID_VM_HANDLE:
            strErr.printf(tr("VMM device is not available (is the VM running?)"));
            break;

        case VERR_HGCM_SERVICE_NOT_FOUND:
            strErr.printf(tr("The guest execution service is not available"));
            break;

        case VERR_BAD_EXE_FORMAT:
            strErr.printf(tr("The file \"%s\" is not an executable format"), guestErrorInfo.getWhat().c_str());
            break;

        case VERR_AUTHENTICATION_FAILURE:
            strErr.printf(tr("The user \"%s\" was not able to logon"), guestErrorInfo.getWhat().c_str());
            break;

        case VERR_INVALID_NAME:
            strErr.printf(tr("The file \"%s\" is an invalid name"), guestErrorInfo.getWhat().c_str());
            break;

        case VERR_TIMEOUT:
            strErr.printf(tr("The guest did not respond within time"));
            break;

        case VERR_CANCELLED:
            strErr.printf(tr("The execution operation was canceled"));
            break;

        case VERR_GSTCTL_MAX_CID_OBJECTS_REACHED:
            strErr.printf(tr("Maximum number of concurrent guest processes has been reached"));
            break;

        case VERR_NOT_FOUND:
            strErr.printf(tr("The guest execution service is not ready (yet)"));
            break;

        case VERR_NOT_SUPPORTED:
            strErr.printf(tr("Specified mode or flag is not supported on the guest"));
            break;

        default:
            strErr.printf(tr("Unhandled error %Rrc for \"%s\" occurred on guest -- please file a bug report"),
                          guestErrorInfo.getVrc(), guestErrorInfo.getWhat().c_str());
            break;
    }

    return strErr;
}

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
/**
 * Sets the file system object data from a given GSTCTLDIRENTRYEX struct.
 *
 * @returns VBox status code.
 * @param   pDirEntryEx         Pointer to GSTCTLDIRENTRYEX struct to use.
 * @param   strUser             Resolved user name owning the object on the guest.
 * @param   strGroups           Resolved user group(s) the object on the guest is associated with.
 *                              On Windows there can be multiple groups assigned. The groups are separated with ";"
 *                              The first group found is always the primary group.
 */
int GuestFsObjData::FromGuestDirEntryEx(PCGSTCTLDIRENTRYEX pDirEntryEx, const Utf8Str &strUser /* = "" */, const Utf8Str &strGroups /* = "" */)
{
    mName = pDirEntryEx->szName;

    return FromGuestFsObjInfo(&pDirEntryEx->Info, strUser, strGroups);
}

/**
 * Sets the file system object data from a given GSTCTLFSOBJINFO struct.
 *
 * @returns VBox status code.
 * @param   pFsObjInfo          Pointer to GSTCTLFSOBJINFO struct to use.
 * @param   strUser             Resolved user name owning the object on the guest.
 * @param   strGroups           Resolved user group(s) the object on the guest is associated with.
 *                              On Windows there can be multiple groups assigned. The groups are separated with ";"
 *                              The first group found is always the primary group.
 */
int GuestFsObjData::FromGuestFsObjInfo(PCGSTCTLFSOBJINFO pFsObjInfo,
                                       const Utf8Str &strUser /* = "" */, const Utf8Str &strGroups /* = "" */)
{
    int vrc = VINF_SUCCESS;

    mType = GuestBase::fileModeToFsObjType(pFsObjInfo->Attr.fMode);

    mFileAttrs = "";
    switch (mType)
    {
        case FsObjType_File:      mFileAttrs += '-'; break;
        case FsObjType_Directory: mFileAttrs += 'd'; break;
        case FsObjType_Symlink:   mFileAttrs += 'l'; break;
        case FsObjType_DevChar:   mFileAttrs += 'c'; break;
        case FsObjType_DevBlock:  mFileAttrs += 'b'; break;
        case FsObjType_Fifo:      mFileAttrs += 'f'; break;
        case FsObjType_Socket:    mFileAttrs += 's'; break;
        case FsObjType_WhiteOut:  mFileAttrs += 'w'; break;
        default:
            mFileAttrs += '?';
            AssertFailedStmt(vrc = VERR_NOT_SUPPORTED);
            break;
    }

#define ADD_ATTR(a_Flag, a_Set, a_Clear) \
    mFileAttrs += pFsObjInfo->Attr.fMode & a_Flag ? a_Set : a_Clear

    ADD_ATTR(RTFS_UNIX_IRUSR, 'r', '-');
    ADD_ATTR(RTFS_UNIX_IWUSR, 'w', '-');
    ADD_ATTR(RTFS_UNIX_IXUSR, 'x', '-');

    ADD_ATTR(RTFS_UNIX_IRGRP, 'r', '-');
    ADD_ATTR(RTFS_UNIX_IWGRP, 'w', '-');
    ADD_ATTR(RTFS_UNIX_IXGRP, 'x', '-');

    ADD_ATTR(RTFS_UNIX_IROTH, 'r', '-');
    ADD_ATTR(RTFS_UNIX_IWOTH, 'w', '-');
    ADD_ATTR(RTFS_UNIX_IXOTH, 'x', '-');

    /** @todo Implement sticky bits. */
    mFileAttrs += "   "; /* Reserve 3 chars for sticky bits. */

    mFileAttrs += " "; /* Separator. */

    ADD_ATTR(RTFS_DOS_READONLY              , 'R', '-');
    ADD_ATTR(RTFS_DOS_HIDDEN                , 'H', '-');
    ADD_ATTR(RTFS_DOS_SYSTEM                , 'S', '-');
    ADD_ATTR(RTFS_DOS_DIRECTORY             , 'D', '-');
    ADD_ATTR(RTFS_DOS_ARCHIVED              , 'A', '-');
    ADD_ATTR(RTFS_DOS_NT_DEVICE             , 'd', '-');
    ADD_ATTR(RTFS_DOS_NT_NORMAL             , 'N', '-');
    ADD_ATTR(RTFS_DOS_NT_TEMPORARY          , 'T', '-');
    ADD_ATTR(RTFS_DOS_NT_SPARSE_FILE        , 'P', '-');
    ADD_ATTR(RTFS_DOS_NT_REPARSE_POINT      , 'J', '-');
    ADD_ATTR(RTFS_DOS_NT_COMPRESSED         , 'C', '-');
    ADD_ATTR(RTFS_DOS_NT_OFFLINE            , 'O', '-');
    ADD_ATTR(RTFS_DOS_NT_NOT_CONTENT_INDEXED, 'I', '-');
    ADD_ATTR(RTFS_DOS_NT_ENCRYPTED          , 'E', '-');

#undef ADD_ATTR

    mObjectSize       = pFsObjInfo->cbObject;
    mAllocatedSize    = pFsObjInfo->cbAllocated;
    mAccessTime       = pFsObjInfo->AccessTime.i64NanosecondsRelativeToUnixEpoch;
    mBirthTime        = pFsObjInfo->BirthTime.i64NanosecondsRelativeToUnixEpoch;
    mChangeTime       = pFsObjInfo->ChangeTime.i64NanosecondsRelativeToUnixEpoch;
    mModificationTime = pFsObjInfo->ModificationTime.i64NanosecondsRelativeToUnixEpoch;
    mUserName         = strUser;
    mUID              = pFsObjInfo->Attr.u.Unix.uid;
    mGID              = pFsObjInfo->Attr.u.Unix.gid;
    mGroupName        = strGroups; /** @todo Separate multiple group. */
    mNumHardLinks     = pFsObjInfo->Attr.u.Unix.cHardlinks;
    mNodeIDDevice     = pFsObjInfo->Attr.u.Unix.INodeIdDevice;
    mNodeID           = pFsObjInfo->Attr.u.Unix.INodeId;
    mDeviceNumber     =    RTFS_IS_DEV_BLOCK(pFsObjInfo->Attr.fMode)
                        || RTFS_IS_DEV_CHAR (pFsObjInfo->Attr.fMode) ? pFsObjInfo->Attr.u.Unix.Device : 0;
    mGenerationID     = pFsObjInfo->Attr.u.Unix.GenerationId;
    mUserFlags        = 0;

    mACL              = ""; /** @todo Implement ACL handling. */

    return vrc;
}
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
/**
 * Extracts the timespec from a given stream block key.
 *
 * @return Pointer to handed-in timespec, or NULL if invalid / not found.
 * @param  strmBlk              Stream block to extract timespec from.
 * @param  strKey               Key to get timespec for.
 * @param  pTimeSpec            Where to store the extracted timespec.
 */
/* static */
PRTTIMESPEC GuestFsObjData::TimeSpecFromKey(const GuestToolboxStreamBlock &strmBlk, const Utf8Str &strKey, PRTTIMESPEC pTimeSpec)
{
    AssertPtrReturn(pTimeSpec, NULL);

    Utf8Str strTime = strmBlk.GetString(strKey.c_str());
    if (strTime.isEmpty())
        return NULL;

    if (!RTTimeSpecFromString(pTimeSpec, strTime.c_str()))
        return NULL;

    return pTimeSpec;
}

/**
 * Extracts the nanoseconds relative from Unix epoch for a given stream block key.
 *
 * @return Nanoseconds relative from Unix epoch, or 0 if invalid / not found.
 * @param  strmBlk              Stream block to extract nanoseconds from.
 * @param  strKey               Key to get nanoseconds for.
 */
/* static */
int64_t GuestFsObjData::UnixEpochNsFromKey(const GuestToolboxStreamBlock &strmBlk, const Utf8Str &strKey)
{
    RTTIMESPEC TimeSpec;
    if (!GuestFsObjData::TimeSpecFromKey(strmBlk, strKey, &TimeSpec))
        return 0;

    return TimeSpec.i64NanosecondsRelativeToUnixEpoch;
}

/**
 * Initializes this object data with a stream block from VBOXSERVICE_TOOL_LS.
 *
 * This is also used by FromStat since the output should be identical given that
 * they use the same output function on the guest side when fLong is true.
 *
 * @return VBox status code.
 * @param  strmBlk              Stream block to use for initialization.
 * @param  fLong                Whether the stream block contains long (detailed) information or not.
 */
int GuestFsObjData::FromToolboxLs(const GuestToolboxStreamBlock &strmBlk, bool fLong)
{
    LogFlowFunc(("\n"));
#ifdef DEBUG
    strmBlk.DumpToLog();
#endif

    /* Object name. */
    mName = strmBlk.GetString("name");
    ASSERT_GUEST_RETURN(mName.isNotEmpty(), VERR_NOT_FOUND);

    /* Type & attributes. */
    bool fHaveAttribs = false;
    char szAttribs[32];
    memset(szAttribs, '?', sizeof(szAttribs) - 1);
    mType = FsObjType_Unknown;
    const char *psz = strmBlk.GetString("ftype");
    if (psz)
    {
        fHaveAttribs = true;
        szAttribs[0] = *psz;
        switch (*psz)
        {
            case '-':   mType = FsObjType_File; break;
            case 'd':   mType = FsObjType_Directory; break;
            case 'l':   mType = FsObjType_Symlink; break;
            case 'c':   mType = FsObjType_DevChar; break;
            case 'b':   mType = FsObjType_DevBlock; break;
            case 'f':   mType = FsObjType_Fifo; break;
            case 's':   mType = FsObjType_Socket; break;
            case 'w':   mType = FsObjType_WhiteOut; break;
            default:
                AssertMsgFailed(("%s\n", psz));
                szAttribs[0] = '?';
                fHaveAttribs = false;
                break;
        }
    }
    psz = strmBlk.GetString("owner_mask");
    if (   psz
        && (psz[0] == '-' || psz[0] == 'r')
        && (psz[1] == '-' || psz[1] == 'w')
        && (psz[2] == '-' || psz[2] == 'x'))
    {
        szAttribs[1] = psz[0];
        szAttribs[2] = psz[1];
        szAttribs[3] = psz[2];
        fHaveAttribs = true;
    }
    psz = strmBlk.GetString("group_mask");
    if (   psz
        && (psz[0] == '-' || psz[0] == 'r')
        && (psz[1] == '-' || psz[1] == 'w')
        && (psz[2] == '-' || psz[2] == 'x'))
    {
        szAttribs[4] = psz[0];
        szAttribs[5] = psz[1];
        szAttribs[6] = psz[2];
        fHaveAttribs = true;
    }
    psz = strmBlk.GetString("other_mask");
    if (   psz
        && (psz[0] == '-' || psz[0] == 'r')
        && (psz[1] == '-' || psz[1] == 'w')
        && (psz[2] == '-' || psz[2] == 'x'))
    {
        szAttribs[7] = psz[0];
        szAttribs[8] = psz[1];
        szAttribs[9] = psz[2];
        fHaveAttribs = true;
    }
    szAttribs[10] = ' '; /* Reserve three chars for sticky bits. */
    szAttribs[11] = ' ';
    szAttribs[12] = ' ';
    szAttribs[13] = ' '; /* Separator. */
    psz = strmBlk.GetString("dos_mask");
    if (   psz
        && (psz[ 0] == '-' || psz[ 0] == 'R')
        && (psz[ 1] == '-' || psz[ 1] == 'H')
        && (psz[ 2] == '-' || psz[ 2] == 'S')
        && (psz[ 3] == '-' || psz[ 3] == 'D')
        && (psz[ 4] == '-' || psz[ 4] == 'A')
        && (psz[ 5] == '-' || psz[ 5] == 'd')
        && (psz[ 6] == '-' || psz[ 6] == 'N')
        && (psz[ 7] == '-' || psz[ 7] == 'T')
        && (psz[ 8] == '-' || psz[ 8] == 'P')
        && (psz[ 9] == '-' || psz[ 9] == 'J')
        && (psz[10] == '-' || psz[10] == 'C')
        && (psz[11] == '-' || psz[11] == 'O')
        && (psz[12] == '-' || psz[12] == 'I')
        && (psz[13] == '-' || psz[13] == 'E'))
    {
        memcpy(&szAttribs[14], psz, 14);
        fHaveAttribs = true;
    }
    szAttribs[28] = '\0';
    if (fHaveAttribs)
        mFileAttrs = szAttribs;

    /* Object size. */
    int vrc = strmBlk.GetInt64Ex("st_size", &mObjectSize);
    ASSERT_GUEST_RC_RETURN(vrc, vrc);
    strmBlk.GetInt64Ex("alloc", &mAllocatedSize);

    /* INode number and device. */
    psz = strmBlk.GetString("node_id");
    if (!psz)
        psz = strmBlk.GetString("cnode_id"); /* copy & past error fixed in 6.0 RC1 */
    if (psz)
        mNodeID = RTStrToInt64(psz);
    mNodeIDDevice = strmBlk.GetUInt32("inode_dev"); /* (Produced by GAs prior to 6.0 RC1.) */

    if (fLong)
    {
        /* Dates. */
        mAccessTime       = GuestFsObjData::UnixEpochNsFromKey(strmBlk, "st_atime");
        mBirthTime        = GuestFsObjData::UnixEpochNsFromKey(strmBlk, "st_birthtime");
        mChangeTime       = GuestFsObjData::UnixEpochNsFromKey(strmBlk, "st_ctime");
        mModificationTime = GuestFsObjData::UnixEpochNsFromKey(strmBlk, "st_mtime");

        /* Owner & group. */
        mUID = strmBlk.GetInt32("uid");
        psz = strmBlk.GetString("username");
        if (psz)
            mUserName = psz;
        mGID = strmBlk.GetInt32("gid");
        psz = strmBlk.GetString("groupname");
        if (psz)
            mGroupName = psz;

        /* Misc attributes: */
        mNumHardLinks = strmBlk.GetUInt32("hlinks", 1);
        mDeviceNumber = strmBlk.GetUInt32("st_rdev");
        mGenerationID = strmBlk.GetUInt32("st_gen");
        mUserFlags    = strmBlk.GetUInt32("st_flags");

        /** @todo ACL */
    }

    LogFlowFuncLeave();
    return VINF_SUCCESS;
}

/**
 * Parses stream block output data which came from the 'rm' (vbox_rm)
 * VBoxService toolbox command. The result will be stored in this object.
 *
 * @returns VBox status code.
 * @param   strmBlk             Stream block output data to parse.
 */
int GuestFsObjData::FromToolboxRm(const GuestToolboxStreamBlock &strmBlk)
{
#ifdef DEBUG
    strmBlk.DumpToLog();
#endif
    /* Object name. */
    mName = strmBlk.GetString("fname"); /* Note: RTPathRmCmd() only sets this on failure. */

    /* Return the stream block's vrc. */
    return strmBlk.GetVrc(true /* fSucceedIfNotFound */);
}

/**
 * Parses stream block output data which came from the 'stat' (vbox_stat)
 * VBoxService toolbox command. The result will be stored in this object.
 *
 * @returns VBox status code.
 * @param   strmBlk             Stream block output data to parse.
 */
int GuestFsObjData::FromToolboxStat(const GuestToolboxStreamBlock &strmBlk)
{
    /* Should be identical output. */
    return GuestFsObjData::FromToolboxLs(strmBlk, true /*fLong*/);
}

/**
 * Parses stream block output data which came from the 'mktemp' (vbox_mktemp)
 * VBoxService toolbox command. The result will be stored in this object.
 *
 * @returns VBox status code.
 * @param   strmBlk             Stream block output data to parse.
 */
int GuestFsObjData::FromToolboxMkTemp(const GuestToolboxStreamBlock &strmBlk)
{
    LogFlowFunc(("\n"));

#ifdef DEBUG
    strmBlk.DumpToLog();
#endif
    /* Object name. */
    mName = strmBlk.GetString("name");
    ASSERT_GUEST_RETURN(mName.isNotEmpty(), VERR_NOT_FOUND);

    /* Assign the stream block's vrc. */
    int const vrc = strmBlk.GetVrc();
    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

#endif /* VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT */

/**
 * Returns the IPRT-compatible file mode.
 * Note: Only handling RTFS_TYPE_ flags are implemented for now.
 *
 * @return IPRT file mode.
 */
RTFMODE GuestFsObjData::GetFileMode(void) const
{
    RTFMODE fMode = 0;

    switch (mType)
    {
        case FsObjType_Directory:
            fMode |= RTFS_TYPE_DIRECTORY;
            break;

        case FsObjType_File:
            fMode |= RTFS_TYPE_FILE;
            break;

        case FsObjType_Symlink:
            fMode |= RTFS_TYPE_SYMLINK;
            break;

        default:
            break;
    }

    /** @todo Implement more stuff. */

    return fMode;
}

///////////////////////////////////////////////////////////////////////////////

GuestProcessOutputStream::GuestProcessOutputStream(void)
    : m_cbMax(_32M)
    , m_cbAllocated(0)
    , m_cbUsed(0)
    , m_offBuf(0)
    , m_pbBuffer(NULL) { }

GuestProcessOutputStream::~GuestProcessOutputStream(void)
{
    Destroy();
}

/**
 * Adds data to the internal parser buffer. Useful if there
 * are multiple rounds of adding data needed.
 *
 * @return  VBox status code. Will return VERR_TOO_MUCH_DATA if the buffer's maximum (limit) has been reached.
 * @param   pbData              Pointer to data to add.
 * @param   cbData              Size (in bytes) of data to add.
 */
int GuestProcessOutputStream::AddData(const BYTE *pbData, size_t cbData)
{
    AssertPtrReturn(pbData, VERR_INVALID_POINTER);
    AssertReturn(cbData, VERR_INVALID_PARAMETER);

    int vrc = VINF_SUCCESS;

    /* Rewind the buffer if it's empty. */
    size_t     cbInBuf   = m_cbUsed - m_offBuf;
    bool const fAddToSet = cbInBuf == 0;
    if (fAddToSet)
        m_cbUsed = m_offBuf = 0;

    /* Try and see if we can simply append the data. */
    if (cbData + m_cbUsed <= m_cbAllocated)
    {
        memcpy(&m_pbBuffer[m_cbUsed], pbData, cbData);
        m_cbUsed += cbData;
    }
    else
    {
        /* Move any buffered data to the front. */
        cbInBuf = m_cbUsed - m_offBuf;
        if (cbInBuf == 0)
            m_cbUsed = m_offBuf = 0;
        else if (m_offBuf) /* Do we have something to move? */
        {
            memmove(m_pbBuffer, &m_pbBuffer[m_offBuf], cbInBuf);
            m_cbUsed = cbInBuf;
            m_offBuf = 0;
        }

        /* Do we need to grow the buffer? */
        if (cbData + m_cbUsed > m_cbAllocated)
        {
            size_t cbAlloc = m_cbUsed + cbData;
            if (cbAlloc <= m_cbMax)
            {
                cbAlloc = RT_ALIGN_Z(cbAlloc, _64K);
                void *pvNew = RTMemRealloc(m_pbBuffer, cbAlloc);
                if (pvNew)
                {
                    m_pbBuffer = (uint8_t *)pvNew;
                    m_cbAllocated = cbAlloc;
                }
                else
                    vrc = VERR_NO_MEMORY;
            }
            else
                vrc = VERR_TOO_MUCH_DATA;
        }

        /* Finally, copy the data. */
        if (RT_SUCCESS(vrc))
        {
            if (cbData + m_cbUsed <= m_cbAllocated)
            {
                memcpy(&m_pbBuffer[m_cbUsed], pbData, cbData);
                m_cbUsed += cbData;
            }
            else
                vrc = VERR_BUFFER_OVERFLOW;
        }
    }

    return vrc;
}

/**
 * Destroys the internal data buffer.
 */
void GuestProcessOutputStream::Destroy(void)
{
    if (m_pbBuffer)
    {
        RTMemFree(m_pbBuffer);
        m_pbBuffer = NULL;
    }

    m_cbAllocated = 0;
    m_cbUsed = 0;
    m_offBuf = 0;
}

#ifdef DEBUG
/**
 * Dumps the raw guest process output to a file on the host.
 * If the file on the host already exists, it will be overwritten.
 *
 * @param   pszFile             Absolute path to host file to dump the output to.
 */
void GuestProcessOutputStream::Dump(const char *pszFile)
{
    LogFlowFunc(("Dumping contents of stream=0x%p (cbAlloc=%u, cbSize=%u, cbOff=%u) to %s\n",
                 m_pbBuffer, m_cbAllocated, m_cbUsed, m_offBuf, pszFile));

    RTFILE hFile;
    int vrc = RTFileOpen(&hFile, pszFile, RTFILE_O_CREATE_REPLACE | RTFILE_O_WRITE | RTFILE_O_DENY_WRITE);
    if (RT_SUCCESS(vrc))
    {
        vrc = RTFileWrite(hFile, m_pbBuffer, m_cbUsed, NULL /* pcbWritten */);
        RTFileClose(hFile);
    }
}
#endif /* DEBUG */

///////////////////////////////////////////////////////////////////////////////

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
/** @todo *NOT* thread safe yet! */
/** @todo Add exception handling for STL stuff! */

GuestToolboxStreamBlock::GuestToolboxStreamBlock(void)
    : m_fComplete(false) { }

GuestToolboxStreamBlock::~GuestToolboxStreamBlock()
{
    Clear();
}

/**
 * Clears (destroys) the currently stored stream pairs.
 */
void GuestToolboxStreamBlock::Clear(void)
{
    m_fComplete = false;
    m_mapPairs.clear();
}

#ifdef DEBUG
/**
 * Dumps the currently stored stream pairs to the (debug) log.
 */
void GuestToolboxStreamBlock::DumpToLog(void) const
{
    LogFlowFunc(("Dumping contents of stream block=0x%p (%ld items, fComplete=%RTbool):\n",
                 this, m_mapPairs.size(), m_fComplete));

    for (GuestCtrlStreamPairMapIterConst it = m_mapPairs.begin();
         it != m_mapPairs.end(); ++it)
    {
        LogFlowFunc(("\t%s=%s\n", it->first.c_str(), it->second.mValue.c_str()));
    }
}
#endif

/**
 * Returns a 64-bit signed integer of a specified key.
 *
 * @return VBox status code. VERR_NOT_FOUND if key was not found.
 * @param  pszKey               Name of key to get the value for.
 * @param  piVal                Pointer to value to return.
 */
int GuestToolboxStreamBlock::GetInt64Ex(const char *pszKey, int64_t *piVal) const
{
    AssertPtrReturn(pszKey, VERR_INVALID_POINTER);
    AssertPtrReturn(piVal, VERR_INVALID_POINTER);
    const char *pszValue = GetString(pszKey);
    if (pszValue)
    {
        *piVal = RTStrToInt64(pszValue);
        return VINF_SUCCESS;
    }
    return VERR_NOT_FOUND;
}

/**
 * Returns a 64-bit integer of a specified key.
 *
 * @return  int64_t             Value to return, 0 if not found / on failure.
 * @param   pszKey              Name of key to get the value for.
 */
int64_t GuestToolboxStreamBlock::GetInt64(const char *pszKey) const
{
    int64_t iVal;
    if (RT_SUCCESS(GetInt64Ex(pszKey, &iVal)))
        return iVal;
    return 0;
}

/**
 * Returns the current number of stream pairs.
 *
 * @return  uint32_t            Current number of stream pairs.
 */
size_t GuestToolboxStreamBlock::GetCount(void) const
{
    return m_mapPairs.size();
}

/**
 * Gets the return code (name = "rc") of this stream block.
 *
 * @return  VBox status code.
 * @retval  VERR_NOT_FOUND if the return code string ("rc") was not found.
 * @param   fSucceedIfNotFound  When set to @c true, this reports back VINF_SUCCESS when the key ("rc") is not found.
 *                              This can happen with some (older) IPRT-provided tools such as RTPathRmCmd(), which only outputs
 *                              rc on failure but not on success. Defaults to @c false.
 */
int GuestToolboxStreamBlock::GetVrc(bool fSucceedIfNotFound /* = false */) const
{
    const char *pszValue = GetString("rc");
    if (pszValue)
        return RTStrToInt16(pszValue);
    if (fSucceedIfNotFound)
        return VINF_SUCCESS;
    /** @todo We probably should have a dedicated error for that, VERR_GSTCTL_GUEST_TOOLBOX_whatever. */
    return VERR_NOT_FOUND;
}

/**
 * Returns a string value of a specified key.
 *
 * @return  uint32_t            Pointer to string to return, NULL if not found / on failure.
 * @param   pszKey              Name of key to get the value for.
 */
const char *GuestToolboxStreamBlock::GetString(const char *pszKey) const
{
    AssertPtrReturn(pszKey, NULL);

    try
    {
        GuestCtrlStreamPairMapIterConst itPairs = m_mapPairs.find(pszKey);
        if (itPairs != m_mapPairs.end())
            return itPairs->second.mValue.c_str();
    }
    catch (const std::exception &ex)
    {
        RT_NOREF(ex);
    }
    return NULL;
}

/**
 * Returns a 32-bit unsigned integer of a specified key.
 *
 * @return  VBox status code. VERR_NOT_FOUND if key was not found.
 * @param   pszKey              Name of key to get the value for.
 * @param   puVal               Pointer to value to return.
 */
int GuestToolboxStreamBlock::GetUInt32Ex(const char *pszKey, uint32_t *puVal) const
{
    const char *pszValue = GetString(pszKey);
    if (pszValue)
    {
        *puVal = RTStrToUInt32(pszValue);
        return VINF_SUCCESS;
    }
    return VERR_NOT_FOUND;
}

/**
 * Returns a 32-bit signed integer of a specified key.
 *
 * @returns 32-bit signed value
 * @param   pszKey              Name of key to get the value for.
 * @param   iDefault            The default to return on error if not found.
 */
int32_t GuestToolboxStreamBlock::GetInt32(const char *pszKey, int32_t iDefault) const
{
    const char *pszValue = GetString(pszKey);
    if (pszValue)
    {
        int32_t iRet;
        int vrc = RTStrToInt32Full(pszValue, 0, &iRet);
        if (RT_SUCCESS(vrc))
            return iRet;
        ASSERT_GUEST_MSG_FAILED(("%s=%s\n", pszKey, pszValue));
    }
    return iDefault;
}

/**
 * Returns a 32-bit unsigned integer of a specified key.
 *
 * @return  uint32_t            Value to return, 0 if not found / on failure.
 * @param   pszKey              Name of key to get the value for.
 * @param   uDefault            The default value to return.
 */
uint32_t GuestToolboxStreamBlock::GetUInt32(const char *pszKey, uint32_t uDefault /*= 0*/) const
{
    uint32_t uVal;
    if (RT_SUCCESS(GetUInt32Ex(pszKey, &uVal)))
        return uVal;
    return uDefault;
}

/**
 * Sets a value to a key or deletes a key by setting a NULL value. Extended version.
 *
 * @return  VBox status code.
 * @param   pszKey              Key name to process.
 * @param   cwcKey              Maximum characters of \a pszKey to process.
 * @param   pszValue            Value to set. Set NULL for deleting the key.
 * @param   cwcValue            Maximum characters of \a pszValue to process.
 * @param   fOverwrite          Whether a key can be overwritten with a new value if it already exists. Will assert otherwise.
 */
int GuestToolboxStreamBlock::SetValueEx(const char *pszKey, size_t cwcKey, const char *pszValue, size_t cwcValue,
                                        bool fOverwrite /* = false */)
{
    AssertPtrReturn(pszKey, VERR_INVALID_POINTER);
    AssertReturn(cwcKey, VERR_INVALID_PARAMETER);

    int vrc = VINF_SUCCESS;
    try
    {
        Utf8Str const strKey(pszKey, cwcKey);

        /* Take a shortcut and prevent crashes on some funny versions
         * of STL if map is empty initially. */
        if (!m_mapPairs.empty())
        {
            GuestCtrlStreamPairMapIter it = m_mapPairs.find(strKey);
            if (it != m_mapPairs.end())
            {
                if (pszValue == NULL)
                    m_mapPairs.erase(it);
                else if (!fOverwrite)
                    AssertMsgFailedReturn(("Key '%*s' already exists! Value is '%s'\n", cwcKey, pszKey, m_mapPairs[strKey].mValue.c_str()),
                                          VERR_ALREADY_EXISTS);
            }
        }

        if (pszValue)
        {
            GuestToolboxStreamValue val(pszValue, cwcValue);
            Log3Func(("strKey='%s', strValue='%s'\n", strKey.c_str(), val.mValue.c_str()));
            m_mapPairs[strKey] = val;
        }
    }
    catch (const std::exception &)
    {
        /** @todo set vrc?   */
    }
    return vrc;
}

/**
 * Sets a value to a key or deletes a key by setting a NULL value.
 *
 * @return  VBox status code.
 * @param   pszKey              Key name to process.
 * @param   pszValue            Value to set. Set NULL for deleting the key.
 */
int GuestToolboxStreamBlock::SetValue(const char *pszKey, const char *pszValue)
{
    return SetValueEx(pszKey, RTSTR_MAX, pszValue, RTSTR_MAX);
}

///////////////////////////////////////////////////////////////////////////////

GuestToolboxStream::GuestToolboxStream(void)
    : m_cBlocks(0)
{
}

GuestToolboxStream::~GuestToolboxStream(void)
{
}

/**
 * Tries to parse the next upcoming pair block within the internal buffer.
 *
 * Parsing behavior:
 * - A stream can contain one or multiple blocks and is terminated by four (4) "\0".
 * - A block (or "object") contains one or multiple key=value pairs and is terminated with two (2) "\0".
 * - Each key=value pair is terminated by a single (1) "\0".
 *
 * As new data can arrive at a later time eventually completing a pair / block / stream,
 * the algorithm needs to be careful not intepreting its current data too early. So only skip termination
 * sequences if we really know that the termination sequence is complete. See comments down below.
 *
 * No locking done.
 *
 * @return VBox status code.
 * @retval VINF_EOF if the stream reached its end.
 * @param  streamBlock              Reference to guest stream block to fill
 */
int GuestToolboxStream::ParseBlock(GuestToolboxStreamBlock &streamBlock)
{
    AssertMsgReturn(streamBlock.m_fComplete == false, ("Block object already marked as being completed\n"), VERR_WRONG_ORDER);

    if (   !m_pbBuffer
        || !m_cbUsed)
        return VINF_EOF;

    AssertReturn(m_offBuf <= m_cbUsed, VERR_INVALID_PARAMETER);
    if (m_offBuf == m_cbUsed)
        return VINF_EOF;

    char * const  pszStart = (char *)&m_pbBuffer[m_offBuf];

    size_t        cbLeftParsed    = m_offBuf < m_cbUsed ? m_cbUsed - m_offBuf : 0;
    size_t        cbLeftLookAhead = cbLeftParsed;

    char         *pszLookAhead = pszStart; /* Look ahead pointer to count terminators. */
    char         *pszParsed    = pszStart; /* Points to data considered as being parsed already. */

    Log4Func(("Current @ %zu/%zu:\n%.*Rhxd\n", m_offBuf, m_cbUsed, RT_MIN(cbLeftParsed, _1K), pszStart));

    size_t cTerm = 0;

    /*
     * We have to be careful when handling single terminators ('\0') here, as we might not know yet
     * if it's part of a multi-terminator seqeuence.
     *
     * So handle and skip those *only* when we hit a non-terminator char again.
     */
    int vrc = VINF_SUCCESS;
    while (cbLeftLookAhead)
    {
        /* Count consequtive terminators. */
        if (*pszLookAhead == GUESTTOOLBOX_STRM_TERM)
        {
            cTerm++;
            pszLookAhead++;
            cbLeftLookAhead--;
            continue;
        }

        pszParsed    = pszLookAhead;
        cbLeftParsed = cbLeftLookAhead;

        /* We hit a non-terminator (again); now interpret where we are, and
         * bail out if we need to. */
        if (cTerm >= 2)
        {
            Log2Func(("Hit end of termination sequence (%zu)\n", cTerm));
            break;
        }

        cTerm = 0; /* Reset consequtive counter. */

        char * const pszPairEnd = RTStrEnd(pszParsed, cbLeftParsed);
        if (!pszPairEnd) /* No zero terminator found (yet), try next time. */
            break;

        Log3Func(("Pair '%s' (%u)\n", pszParsed, strlen(pszParsed)));

        Assert(pszPairEnd != pszParsed);
        size_t const cbPair = (size_t)(pszPairEnd - pszParsed);
        Assert(cbPair);
        const char  *pszSep = (const char *)memchr(pszParsed, '=', cbPair);
        if (!pszSep) /* No separator found (yet), try next time. */
            break;

        /* Skip the separator so that pszSep points to the actual value. */
        pszSep++;

        char const * const pszKey = pszParsed;
        char const * const pszVal = pszSep;

        vrc = streamBlock.SetValueEx(pszKey, pszSep - pszKey - 1, pszVal, pszPairEnd - pszVal);
        if (RT_FAILURE(vrc))
            return vrc;

        if (cbPair >= cbLeftParsed)
            break;

        /* Accounting for next iteration. */
        pszParsed       = pszPairEnd;
        Assert(cbLeftParsed >= cbPair);
        cbLeftParsed   -= cbPair;

        pszLookAhead    = pszPairEnd;
        cbLeftLookAhead = cbLeftParsed;

        if (cbLeftParsed)
            Log4Func(("Next iteration @ %zu:\n%.*Rhxd\n", pszParsed - pszStart, cbLeftParsed, pszParsed));
    }

    if (cbLeftParsed)
        Log4Func(("Done @ %zu:\n%.*Rhxd\n", pszParsed - pszStart, cbLeftParsed, pszParsed));

    m_offBuf += pszParsed - pszStart; /* Only account really parsed content. */
    Assert(m_offBuf <= m_cbUsed);

    /* Did we hit a block or stream termination sequence? */
    if (cTerm >= GUESTTOOLBOX_STRM_BLK_TERM_CNT)
    {
        if (!streamBlock.IsEmpty()) /* Only account and complete blocks which have values in it. */
        {
            m_cBlocks++;
            streamBlock.m_fComplete = true;
#ifdef DEBUG
            streamBlock.DumpToLog();
#endif
        }

        if (cTerm >= GUESTTOOLBOX_STRM_TERM_CNT)
        {
            m_offBuf = m_cbUsed;
            vrc = VINF_EOF;
        }
    }

    LogFlowThisFunc(("cbLeft=%zu, offBuffer=%zu / cbUsed=%zu, cBlocks=%zu, cTerm=%zu -> current block has %RU64 pairs (complete = %RTbool), rc=%Rrc\n",
                     cbLeftParsed, m_offBuf, m_cbUsed, m_cBlocks, cTerm, streamBlock.GetCount(), streamBlock.IsComplete(), vrc));

    return vrc;
}
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT */

GuestBase::GuestBase(void)
    : mConsole(NULL)
    , mNextContextID(RTRandU32() % VBOX_GUESTCTRL_MAX_CONTEXTS)
{
}

GuestBase::~GuestBase(void)
{
}

/**
 * Separate initialization function for the base class.
 *
 * @returns VBox status code.
 */
int GuestBase::baseInit(void)
{
    int const vrc = RTCritSectInit(&mWaitEventCritSect);
    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Separate uninitialization function for the base class.
 */
void GuestBase::baseUninit(void)
{
    LogFlowThisFuncEnter();

    /* Make sure to cancel any outstanding wait events. */
    int vrc2 = cancelWaitEvents();
    AssertRC(vrc2);

    vrc2 = RTCritSectDelete(&mWaitEventCritSect);
    AssertRC(vrc2);

    LogFlowFuncLeaveRC(vrc2);
    /* No return value. */
}

/**
 * Cancels all outstanding wait events.
 *
 * @returns VBox status code.
 */
int GuestBase::cancelWaitEvents(void)
{
    LogFlowThisFuncEnter();

    int vrc = RTCritSectEnter(&mWaitEventCritSect);
    if (RT_SUCCESS(vrc))
    {
        GuestEventGroup::iterator itEventGroups = mWaitEventGroups.begin();
        while (itEventGroups != mWaitEventGroups.end())
        {
            GuestWaitEvents::iterator itEvents = itEventGroups->second.begin();
            while (itEvents != itEventGroups->second.end())
            {
                GuestWaitEvent *pEvent = itEvents->second;
                AssertPtr(pEvent);

                /*
                 * Just cancel the event, but don't remove it from the
                 * wait events map. Don't delete it though, this (hopefully)
                 * is done by the caller using unregisterWaitEvent().
                 */
                int vrc2 = pEvent->Cancel();
                AssertRC(vrc2);

                ++itEvents;
            }

            ++itEventGroups;
        }

        int vrc2 = RTCritSectLeave(&mWaitEventCritSect);
        if (RT_SUCCESS(vrc))
            vrc = vrc2;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Handles generic messages not bound to a specific object type.
 *
 * @return VBox status code. VERR_NOT_FOUND if no handler has been found or VERR_NOT_SUPPORTED
 *         if this class does not support the specified callback.
 * @param  pCtxCb               Host callback context.
 * @param  pSvcCb               Service callback data.
 */
int GuestBase::dispatchGeneric(PVBOXGUESTCTRLHOSTCBCTX pCtxCb, PVBOXGUESTCTRLHOSTCALLBACK pSvcCb)
{
    LogFlowFunc(("pCtxCb=%p, pSvcCb=%p\n", pCtxCb, pSvcCb));

    AssertPtrReturn(pCtxCb, VERR_INVALID_POINTER);
    AssertPtrReturn(pSvcCb, VERR_INVALID_POINTER);

    int vrc;

    try
    {
        Log2Func(("uFunc=%RU32, cParms=%RU32\n", pCtxCb->uMessage, pSvcCb->mParms));

        switch (pCtxCb->uMessage)
        {
            case GUEST_MSG_PROGRESS_UPDATE:
                vrc = VINF_SUCCESS;
                break;

            case GUEST_MSG_REPLY:
            {
                if (pSvcCb->mParms >= 4)
                {
                    int idx = 1; /* Current parameter index. */
                    CALLBACKDATA_MSG_REPLY dataCb;
                    /* pSvcCb->mpaParms[0] always contains the context ID. */
                    vrc = HGCMSvcGetU32(&pSvcCb->mpaParms[idx++], &dataCb.uType);
                    AssertRCReturn(vrc, vrc);
                    vrc = HGCMSvcGetU32(&pSvcCb->mpaParms[idx++], &dataCb.rc);
                    AssertRCReturn(vrc, vrc);
                    vrc = HGCMSvcGetPv(&pSvcCb->mpaParms[idx++], &dataCb.pvPayload, &dataCb.cbPayload);
                    AssertRCReturn(vrc, vrc);

                    try
                    {
                        GuestWaitEventPayload evPayload(dataCb.uType, dataCb.pvPayload, dataCb.cbPayload);
                        vrc = signalWaitEventInternal(pCtxCb, dataCb.rc, &evPayload);
                    }
                    catch (int vrcEx) /* Thrown by GuestWaitEventPayload constructor. */
                    {
                        vrc = vrcEx;
                    }
                }
                else
                    vrc = VERR_INVALID_PARAMETER;
                break;
            }

            default:
                vrc = VERR_NOT_SUPPORTED;
                break;
        }
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }
    catch (int vrcCatch)
    {
        vrc = vrcCatch;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Generates a context ID (CID) by incrementing the object's count.
 * A CID consists of a session ID, an object ID and a count.
 *
 * Note: This function does not guarantee that the returned CID is unique;
 *       the caller has to take care of that and eventually retry.
 *
 * @returns VBox status code.
 * @param   uSessionID          Session ID to use for CID generation.
 * @param   uObjectID           Object ID to use for CID generation.
 * @param   puContextID         Where to store the generated CID on success.
 */
int GuestBase::generateContextID(uint32_t uSessionID, uint32_t uObjectID, uint32_t *puContextID)
{
    AssertPtrReturn(puContextID, VERR_INVALID_POINTER);

    if (   uSessionID >= VBOX_GUESTCTRL_MAX_SESSIONS
        || uObjectID  >= VBOX_GUESTCTRL_MAX_OBJECTS)
        return VERR_INVALID_PARAMETER;

    uint32_t uCount = ASMAtomicIncU32(&mNextContextID);
    uCount %= VBOX_GUESTCTRL_MAX_CONTEXTS;

    uint32_t uNewContextID = VBOX_GUESTCTRL_CONTEXTID_MAKE(uSessionID, uObjectID, uCount);

    *puContextID = uNewContextID;

#if 0
    LogFlowThisFunc(("mNextContextID=%RU32, uSessionID=%RU32, uObjectID=%RU32, uCount=%RU32, uNewContextID=%RU32\n",
                     mNextContextID, uSessionID, uObjectID, uCount, uNewContextID));
#endif
    return VINF_SUCCESS;
}

/**
 * Registers (creates) a new wait event based on a given session and object ID.
 *
 * From those IDs an unique context ID (CID) will be built, which only can be
 * around once at a time.
 *
 * @returns VBox status code.
 * @retval  VERR_GSTCTL_MAX_CID_COUNT_REACHED if unable to generate a free context ID (CID, the count part (bits 15:0)).
 * @param   uSessionID              Session ID to register wait event for.
 * @param   uObjectID               Object ID to register wait event for.
 * @param   ppEvent                 Pointer to registered (created) wait event on success.
 *                                  Must be destroyed with unregisterWaitEvent().
 */
int GuestBase::registerWaitEvent(uint32_t uSessionID, uint32_t uObjectID, GuestWaitEvent **ppEvent)
{
    GuestEventTypes eventTypesEmpty;
    return registerWaitEventEx(uSessionID, uObjectID, eventTypesEmpty, ppEvent);
}

/**
 * Creates and registers a new wait event object that waits on a set of events
 * related to a given object within the session.
 *
 * From the session ID and object ID a one-time unique context ID (CID) is built
 * for this wait object.  Normally the CID is then passed to the guest along
 * with a request, and the guest passed the CID back with the reply.  The
 * handler for the reply then emits a signal on the event type associated with
 * the reply, which includes signalling the object returned by this method and
 * the waking up the thread waiting on it.
 *
 * @returns VBox status code.
 * @retval  VERR_GSTCTL_MAX_CID_COUNT_REACHED if unable to generate a free context ID (CID, the count part (bits 15:0)).
 * @param   uSessionID              Session ID to register wait event for.
 * @param   uObjectID               Object ID to register wait event for.
 * @param   lstEvents               List of events to register the wait event for.
 * @param   ppEvent                 Pointer to registered (created) wait event on success.
 *                                  Must be destroyed with unregisterWaitEvent().
 */
int GuestBase::registerWaitEventEx(uint32_t uSessionID, uint32_t uObjectID, const GuestEventTypes &lstEvents,
                                   GuestWaitEvent **ppEvent)
{
    AssertPtrReturn(ppEvent, VERR_INVALID_POINTER);

    uint32_t idContext;
    int vrc = generateContextID(uSessionID, uObjectID, &idContext);
    AssertRCReturn(vrc, vrc);

    GuestWaitEvent *pEvent = new GuestWaitEvent();
    AssertPtrReturn(pEvent, VERR_NO_MEMORY);

    vrc = pEvent->Init(idContext, lstEvents);
    AssertRCReturn(vrc, vrc);

    LogFlowThisFunc(("New event=%p, CID=%RU32\n", pEvent, idContext));

    vrc = RTCritSectEnter(&mWaitEventCritSect);
    if (RT_SUCCESS(vrc))
    {
        /*
         * Check that we don't have any context ID collisions (should be very unlikely).
         *
         * The ASSUMPTION here is that mWaitEvents has all the same events as
         * mWaitEventGroups, so it suffices to check one of the two.
         */
        if (mWaitEvents.find(idContext) != mWaitEvents.end())
        {
            uint32_t cTries = 0;
            do
            {
                vrc = generateContextID(uSessionID, uObjectID, &idContext);
                AssertRCBreak(vrc);
                LogFunc(("Found context ID duplicate; trying a different context ID: %#x\n", idContext));
                if (mWaitEvents.find(idContext) != mWaitEvents.end())
                    vrc = VERR_GSTCTL_MAX_CID_COUNT_REACHED;
            } while (RT_FAILURE_NP(vrc) && cTries++ < 10);
        }
        if (RT_SUCCESS(vrc))
        {
            /*
             * Insert event into matching event group. This is for faster per-group lookup of all events later.
             */
            uint32_t cInserts = 0;
            for (GuestEventTypes::const_iterator ItType = lstEvents.begin(); ItType != lstEvents.end(); ++ItType)
            {
                GuestWaitEvents &eventGroup = mWaitEventGroups[*ItType];
                if (eventGroup.find(idContext) == eventGroup.end())
                {
                    try
                    {
                        eventGroup.insert(std::pair<uint32_t, GuestWaitEvent *>(idContext, pEvent));
                        cInserts++;
                    }
                    catch (std::bad_alloc &)
                    {
                        while (ItType != lstEvents.begin())
                        {
                            --ItType;
                            mWaitEventGroups[*ItType].erase(idContext);
                        }
                        vrc = VERR_NO_MEMORY;
                        break;
                    }
                }
                else
                    Assert(cInserts > 0); /* else: lstEvents has duplicate entries. */
            }
            if (RT_SUCCESS(vrc))
            {
                Assert(cInserts > 0 || lstEvents.size() == 0);
                RT_NOREF(cInserts);

                /*
                 * Register event in the regular event list.
                 */
                try
                {
                    mWaitEvents[idContext] = pEvent;
                }
                catch (std::bad_alloc &)
                {
                    for (GuestEventTypes::const_iterator ItType = lstEvents.begin(); ItType != lstEvents.end(); ++ItType)
                        mWaitEventGroups[*ItType].erase(idContext);
                    vrc = VERR_NO_MEMORY;
                }
            }
        }

        RTCritSectLeave(&mWaitEventCritSect);
    }
    if (RT_SUCCESS(vrc))
    {
        *ppEvent = pEvent;
        return vrc;
    }

    if (pEvent)
        delete pEvent;

    return vrc;
}

/**
 * Signals all wait events of a specific type (if found)
 * and notifies external events accordingly.
 *
 * @returns VBox status code.
 * @param   aType               Event type to signal.
 * @param   aEvent              Which external event to notify.
 */
int GuestBase::signalWaitEvent(VBoxEventType_T aType, IEvent *aEvent)
{
    int vrc = RTCritSectEnter(&mWaitEventCritSect);
#ifdef DEBUG
    uint32_t cEvents = 0;
#endif
    if (RT_SUCCESS(vrc))
    {
        GuestEventGroup::iterator itGroup = mWaitEventGroups.find(aType);
        if (itGroup != mWaitEventGroups.end())
        {
            /* Signal all events in the group, leaving the group empty afterwards. */
            GuestWaitEvents::iterator ItWaitEvt;
            while ((ItWaitEvt = itGroup->second.begin()) != itGroup->second.end())
            {
                LogFlowThisFunc(("Signalling event=%p, type=%ld (CID %#x: Session=%RU32, Object=%RU32, Count=%RU32) ...\n",
                                 ItWaitEvt->second, aType, ItWaitEvt->first, VBOX_GUESTCTRL_CONTEXTID_GET_SESSION(ItWaitEvt->first),
                                 VBOX_GUESTCTRL_CONTEXTID_GET_OBJECT(ItWaitEvt->first), VBOX_GUESTCTRL_CONTEXTID_GET_COUNT(ItWaitEvt->first)));

                int vrc2 = ItWaitEvt->second->SignalExternal(aEvent);
                AssertRC(vrc2);

                /* Take down the wait event object details before we erase it from this list and invalid ItGrpEvt. */
                const GuestEventTypes &EvtTypes  = ItWaitEvt->second->Types();
                uint32_t               idContext = ItWaitEvt->first;
                itGroup->second.erase(ItWaitEvt);

                for (GuestEventTypes::const_iterator ItType = EvtTypes.begin(); ItType != EvtTypes.end(); ++ItType)
                {
                    GuestEventGroup::iterator EvtTypeGrp = mWaitEventGroups.find(*ItType);
                    if (EvtTypeGrp != mWaitEventGroups.end())
                    {
                        ItWaitEvt = EvtTypeGrp->second.find(idContext);
                        if (ItWaitEvt != EvtTypeGrp->second.end())
                        {
                            LogFlowThisFunc(("Removing event %p (CID %#x) from type %d group\n", ItWaitEvt->second, idContext, *ItType));
                            EvtTypeGrp->second.erase(ItWaitEvt);
                            LogFlowThisFunc(("%zu events left for type %d\n", EvtTypeGrp->second.size(), *ItType));
                            Assert(EvtTypeGrp->second.find(idContext) == EvtTypeGrp->second.end()); /* no duplicates */
                        }
                    }
                }
            }
        }

        int vrc2 = RTCritSectLeave(&mWaitEventCritSect);
        if (RT_SUCCESS(vrc))
            vrc = vrc2;
    }

#ifdef DEBUG
    LogFlowThisFunc(("Signalled %RU32 events, vrc=%Rrc\n", cEvents, vrc));
#endif
    return vrc;
}

/**
 * Signals a wait event which is registered to a specific callback (bound to a context ID (CID)).
 *
 * @returns VBox status code.
 * @param   pCbCtx      Pointer to host service callback context.
 * @param   vrcGuest    Guest return VBox status code to set.
 * @param   pPayload    Additional wait event payload data set set on return. Optional.
 */
int GuestBase::signalWaitEventInternal(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, int vrcGuest, const GuestWaitEventPayload *pPayload)
{
    if (RT_SUCCESS(vrcGuest))
        return signalWaitEventInternalEx(pCbCtx, VINF_SUCCESS, VINF_SUCCESS /* vrcGuest */, pPayload);

    return signalWaitEventInternalEx(pCbCtx, VERR_GSTCTL_GUEST_ERROR, vrcGuest, pPayload);
}

/**
 * Signals a wait event which is registered to a specific callback (bound to a context ID (CID)).
 * Extended version.
 *
 * @returns VBox status code.
 * @param   pCbCtx      Pointer to host service callback context.
 * @param   vrc         Return VBox status code to set as wait result.
 * @param   vrcGuest    Guest return VBox status code to set additionally, if
 *                      vrc is set to VERR_GSTCTL_GUEST_ERROR.
 * @param   pPayload    Additional wait event payload data set set on return. Optional.
 */
int GuestBase::signalWaitEventInternalEx(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, int vrc, int vrcGuest,
                                         const GuestWaitEventPayload *pPayload)
{
    AssertPtrReturn(pCbCtx, VERR_INVALID_POINTER);
    /* pPayload is optional. */

    int vrc2 = RTCritSectEnter(&mWaitEventCritSect);
    if (RT_SUCCESS(vrc2))
    {
        GuestWaitEvents::iterator itEvent = mWaitEvents.find(pCbCtx->uContextID);
        if (itEvent != mWaitEvents.end())
        {
            LogFlowThisFunc(("Signalling event=%p (CID %RU32, vrc=%Rrc, vrcGuest=%Rrc, pPayload=%p) ...\n",
                             itEvent->second, itEvent->first, vrc, vrcGuest, pPayload));
            GuestWaitEvent *pEvent = itEvent->second;
            AssertPtr(pEvent);
            vrc2 = pEvent->SignalInternal(vrc, vrcGuest, pPayload);
        }
        else
            vrc2 = VERR_NOT_FOUND;

        int vrc3 = RTCritSectLeave(&mWaitEventCritSect);
        if (RT_SUCCESS(vrc2))
            vrc2 = vrc3;
    }

    return vrc2;
}

/**
 * Unregisters (deletes) a wait event.
 *
 * After successful unregistration the event will not be valid anymore.
 *
 * @returns VBox status code.
 * @param   pWaitEvt        Wait event to unregister (delete).
 */
int GuestBase::unregisterWaitEvent(GuestWaitEvent *pWaitEvt)
{
    if (!pWaitEvt) /* Nothing to unregister. */
        return VINF_SUCCESS;

    int vrc = RTCritSectEnter(&mWaitEventCritSect);
    if (RT_SUCCESS(vrc))
    {
        LogFlowThisFunc(("pWaitEvt=%p\n", pWaitEvt));

/** @todo r=bird: One way of optimizing this would be to use the pointer
 * instead of the context ID as index into the groups, i.e. revert the value
 * pair for the GuestWaitEvents type.
 *
 * An even more efficent way, would be to not use sexy std::xxx containers for
 * the types, but iprt/list.h, as that would just be a RTListNodeRemove call for
 * each type w/o needing to iterate much at all.  I.e. add a struct {
 * RTLISTNODE, GuestWaitEvent *pSelf} array to GuestWaitEvent, and change
 * GuestEventGroup to std::map<VBoxEventType_T, RTListAnchorClass>
 * (RTListAnchorClass == RTLISTANCHOR wrapper with a constructor)).
 *
 * P.S. the try/catch is now longer needed after I changed pWaitEvt->Types() to
 * return a const reference rather than a copy of the type list (and it think it
 * is safe to assume iterators are not hitting the heap).  Copy vs reference is
 * an easy mistake to make in C++.
 *
 * P.P.S. The mWaitEventGroups optimization is probably just a lot of extra work
 * with little payoff.
 */
        try
        {
            /* Remove the event from all event type groups. */
            const GuestEventTypes &lstTypes = pWaitEvt->Types();
            for (GuestEventTypes::const_iterator itType = lstTypes.begin();
                 itType != lstTypes.end(); ++itType)
            {
                /** @todo Slow O(n) lookup. Optimize this. */
                GuestWaitEvents::iterator itCurEvent = mWaitEventGroups[(*itType)].begin();
                while (itCurEvent != mWaitEventGroups[(*itType)].end())
                {
                    if (itCurEvent->second == pWaitEvt)
                    {
                        mWaitEventGroups[(*itType)].erase(itCurEvent);
                        break;
                    }
                    ++itCurEvent;
                }
            }

            /* Remove the event from the general event list as well. */
            GuestWaitEvents::iterator itEvent = mWaitEvents.find(pWaitEvt->ContextID());

            Assert(itEvent != mWaitEvents.end());
            Assert(itEvent->second == pWaitEvt);

            mWaitEvents.erase(itEvent);

            delete pWaitEvt;
            pWaitEvt = NULL;
        }
        catch (const std::exception &ex)
        {
            RT_NOREF(ex);
            AssertFailedStmt(vrc = VERR_NOT_FOUND);
        }

        int vrc2 = RTCritSectLeave(&mWaitEventCritSect);
        if (RT_SUCCESS(vrc))
            vrc = vrc2;
    }

    return vrc;
}

/**
 * Waits for an already registered guest wait event.
 *
 * @return  VBox status code.
 * @retval  VERR_GSTCTL_GUEST_ERROR may be returned, call GuestResult() to get
 *          the actual result.
 *
 * @param   pWaitEvt                Pointer to event to wait for.
 * @param   msTimeout               Timeout (in ms) for waiting.
 * @param   pType                   Event type of following IEvent. Optional.
 * @param   ppEvent                 Pointer to IEvent which got triggered for this event. Optional.
 */
int GuestBase::waitForEvent(GuestWaitEvent *pWaitEvt, uint32_t msTimeout, VBoxEventType_T *pType, IEvent **ppEvent)
{
    AssertPtrReturn(pWaitEvt, VERR_INVALID_POINTER);
    /* pType is optional. */
    /* ppEvent is optional. */

    int vrc = pWaitEvt->Wait(msTimeout);
    if (RT_SUCCESS(vrc))
    {
        const ComPtr<IEvent> pThisEvent = pWaitEvt->Event();
        if (pThisEvent.isNotNull()) /* Make sure that we actually have an event associated. */
        {
            if (pType)
            {
                HRESULT hrc = pThisEvent->COMGETTER(Type)(pType);
                if (FAILED(hrc))
                    vrc = VERR_COM_UNEXPECTED;
            }
            if (   RT_SUCCESS(vrc)
                && ppEvent)
                pThisEvent.queryInterfaceTo(ppEvent);

            unconst(pThisEvent).setNull();
        }
    }

    return vrc;
}

#ifndef VBOX_GUESTCTRL_TEST_CASE
/**
 * Convenience function to return a pre-formatted string using an action description and a guest error information.
 *
 * @returns Pre-formatted string with a user-friendly error string.
 * @param   strAction           Action of when the error occurred.
 * @param   guestErrorInfo      Related guest error information to use.
 */
/* static */ Utf8Str GuestBase::getErrorAsString(const Utf8Str& strAction, const GuestErrorInfo& guestErrorInfo)
{
    Assert(strAction.isNotEmpty());
    return Utf8StrFmt("%s: %s", strAction.c_str(), getErrorAsString(guestErrorInfo).c_str());
}

/**
 * Returns a user-friendly error message from a given GuestErrorInfo object.
 *
 * @returns Error message string.
 * @param   guestErrorInfo      Guest error info to return error message for.
 */
/* static */ Utf8Str GuestBase::getErrorAsString(const GuestErrorInfo& guestErrorInfo)
{
    AssertMsg(RT_FAILURE(guestErrorInfo.getVrc()), ("Guest vrc does not indicate a failure\n"));

    Utf8Str strErr;

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
# define CASE_TOOL_ERROR(a_eType, a_strTool) \
    case a_eType: \
    { \
        strErr = GuestProcessToolbox::guestErrorToString(a_strTool, guestErrorInfo); \
        break; \
    }
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT */

    switch (guestErrorInfo.getType())
    {
        case GuestErrorInfo::Type_Session:
            strErr = GuestSession::i_guestErrorToString(guestErrorInfo.getVrc());
            break;

        case GuestErrorInfo::Type_Process:
            strErr = GuestProcess::i_guestErrorToString(guestErrorInfo.getVrc(), guestErrorInfo.getWhat().c_str());
            break;

        case GuestErrorInfo::Type_File:
            strErr = GuestFile::i_guestErrorToString(guestErrorInfo.getVrc(), guestErrorInfo.getWhat().c_str());
            break;

        case GuestErrorInfo::Type_Directory:
            strErr = GuestDirectory::i_guestErrorToString(guestErrorInfo.getVrc(), guestErrorInfo.getWhat().c_str());
            break;

        case GuestErrorInfo::Type_Fs:
            strErr = GuestFs::guestErrorToString(guestErrorInfo);
            break;

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
        CASE_TOOL_ERROR(GuestErrorInfo::Type_ToolLs,     VBOXSERVICE_TOOL_LS);
        CASE_TOOL_ERROR(GuestErrorInfo::Type_ToolMkDir,  VBOXSERVICE_TOOL_MKDIR);
        CASE_TOOL_ERROR(GuestErrorInfo::Type_ToolMkTemp, VBOXSERVICE_TOOL_MKTEMP);
        CASE_TOOL_ERROR(GuestErrorInfo::Type_ToolRm,     VBOXSERVICE_TOOL_RM);
        CASE_TOOL_ERROR(GuestErrorInfo::Type_ToolStat,   VBOXSERVICE_TOOL_STAT);
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT */
        default:
            AssertMsgFailed(("Type not implemented (type=%RU32, vrc=%Rrc)\n", guestErrorInfo.getType(), guestErrorInfo.getVrc()));
            strErr = Utf8StrFmt("Unknown / Not implemented -- Please file a bug report (type=%RU32, vrc=%Rrc)\n",
                                guestErrorInfo.getType(), guestErrorInfo.getVrc());
            break;
    }

    return strErr;
}

#endif /* VBOX_GUESTCTRL_TEST_CASE */

/**
 * Converts RTFMODE to FsObjType_T.
 *
 * @return  Converted FsObjType_T type.
 * @param   fMode               RTFMODE to convert.
 */
/* static */
FsObjType_T GuestBase::fileModeToFsObjType(RTFMODE fMode)
{
         if (RTFS_IS_FIFO(fMode))      return FsObjType_Fifo;
    else if (RTFS_IS_DEV_CHAR(fMode))  return FsObjType_DevChar;
    else if (RTFS_IS_DIRECTORY(fMode)) return FsObjType_Directory;
    else if (RTFS_IS_DEV_BLOCK(fMode)) return FsObjType_DevBlock;
    else if (RTFS_IS_FILE(fMode))      return FsObjType_File;
    else if (RTFS_IS_SYMLINK(fMode))   return FsObjType_Symlink;
    else if (RTFS_IS_SOCKET(fMode))    return FsObjType_Socket;
    else if (RTFS_IS_WHITEOUT(fMode))  return FsObjType_WhiteOut;

    return FsObjType_Unknown;
}

/**
 * Converts a FsObjType_T to a human-readable string.
 *
 * @returns Human-readable string of FsObjType_T.
 * @param   enmType             FsObjType_T to convert.
 */
/* static */
const char *GuestBase::fsObjTypeToStr(FsObjType_T enmType)
{
    switch (enmType)
    {
        case FsObjType_Directory: return "directory";
        case FsObjType_Symlink:   return "symbolic link";
        case FsObjType_File:      return "file";
        default:                  break;
    }

    return "unknown";
}

/**
 * Converts a PathStyle_T to a human-readable string.
 *
 * @returns Human-readable string of PathStyle_T.
 * @param   enmPathStyle        PathStyle_T to convert.
 */
/* static */
const char *GuestBase::pathStyleToStr(PathStyle_T enmPathStyle)
{
    switch (enmPathStyle)
    {
        case PathStyle_DOS:     return "DOS";
        case PathStyle_UNIX:    return "UNIX";
        case PathStyle_Unknown: return "Unknown";
        default:                break;
    }

    return "<invalid>";
}

GuestObject::GuestObject(void)
    : mSession(NULL),
      mObjectID(0)
{
}

GuestObject::~GuestObject(void)
{
}

/**
 * Binds this guest (control) object to a specific guest (control) session.
 *
 * @returns VBox status code.
 * @param   pConsole            Pointer to console object to use.
 * @param   pSession            Pointer to session to bind this object to.
 * @param   uObjectID           Object ID for this object to use within that specific session.
 *                              Each object ID must be unique per session.
 */
int GuestObject::bindToSession(Console *pConsole, GuestSession *pSession, uint32_t uObjectID)
{
    AssertPtrReturn(pConsole, VERR_INVALID_POINTER);
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);

    mConsole  = pConsole;
    mSession  = pSession;
    mObjectID = uObjectID;

    return VINF_SUCCESS;
}

/**
 * Registers (creates) a new wait event.
 *
 * @returns VBox status code.
 * @param   lstEvents           List of events which the new wait event gets triggered at.
 * @param   ppEvent             Returns the new wait event on success.
 */
int GuestObject::registerWaitEvent(const GuestEventTypes &lstEvents,
                                   GuestWaitEvent **ppEvent)
{
    AssertPtr(mSession);
    return GuestBase::registerWaitEventEx(mSession->i_getId(), mObjectID, lstEvents, ppEvent);
}

/**
 * Sends a HGCM message to the guest (via the guest control host service).
 *
 * @returns VBox status code.
 * @param   uMessage            Message ID of message to send.
 * @param   cParms              Number of HGCM message parameters to send.
 * @param   paParms             Array of HGCM message parameters to send.
 */
int GuestObject::sendMessage(uint32_t uMessage, uint32_t cParms, PVBOXHGCMSVCPARM paParms)
{
#ifndef VBOX_GUESTCTRL_TEST_CASE
    ComObjPtr<Console> pConsole = mConsole;
    Assert(!pConsole.isNull());

    int vrc = VERR_HGCM_SERVICE_NOT_FOUND;

    /* Forward the information to the VMM device. */
    VMMDev *pVMMDev = pConsole->i_getVMMDev();
    if (pVMMDev)
    {
        /* HACK ALERT! We extend the first parameter to 64-bit and use the
                       two topmost bits for call destination information. */
        Assert(paParms[0].type == VBOX_HGCM_SVC_PARM_32BIT);
        paParms[0].type = VBOX_HGCM_SVC_PARM_64BIT;
        paParms[0].u.uint64 = (uint64_t)paParms[0].u.uint32 | VBOX_GUESTCTRL_DST_SESSION;

        /* Make the call. */
        LogFlowThisFunc(("uMessage=%RU32, cParms=%RU32\n", uMessage, cParms));
        vrc = pVMMDev->hgcmHostCall(HGCMSERVICE_NAME, uMessage, cParms, paParms);
        if (RT_FAILURE(vrc))
        {
            /** @todo What to do here? */
        }
    }
#else
    LogFlowThisFuncEnter();

    /* Not needed within testcases. */
    RT_NOREF(uMessage, cParms, paParms);
    int vrc = VINF_SUCCESS;
#endif
    return vrc;
}

GuestWaitEventBase::GuestWaitEventBase(void)
    : mfAborted(false),
      mCID(0),
      mEventSem(NIL_RTSEMEVENT),
      mVrc(VINF_SUCCESS),
      mGuestRc(VINF_SUCCESS)
{
}

GuestWaitEventBase::~GuestWaitEventBase(void)
{
    if (mEventSem != NIL_RTSEMEVENT)
    {
        RTSemEventDestroy(mEventSem);
        mEventSem = NIL_RTSEMEVENT;
    }
}

/**
 * Initializes a wait event with a specific context ID (CID).
 *
 * @returns VBox status code.
 * @param   uCID                Context ID (CID) to initialize wait event with.
 */
int GuestWaitEventBase::Init(uint32_t uCID)
{
    mCID = uCID;

    return RTSemEventCreate(&mEventSem);
}

/**
 * Signals a wait event.
 *
 * @returns VBox status code.
 * @param   vrc         Return VBox status code to set as wait result.
 * @param   vrcGuest    Guest return VBox status code to set additionally, if
 *                      @a vrc is set to VERR_GSTCTL_GUEST_ERROR.
 * @param   pPayload    Additional wait event payload data set set on return. Optional.
 */
int GuestWaitEventBase::SignalInternal(int vrc, int vrcGuest, const GuestWaitEventPayload *pPayload)
{
    if (mfAborted)
        return VERR_CANCELLED;

#ifdef VBOX_STRICT
    if (vrc == VERR_GSTCTL_GUEST_ERROR)
        AssertMsg(RT_FAILURE(vrcGuest), ("Guest error indicated but no actual guest error set (%Rrc)\n", vrcGuest));
    else
        AssertMsg(RT_SUCCESS(vrcGuest), ("No guest error indicated but actual guest error set (%Rrc)\n", vrcGuest));
#endif

    int vrc2;
    if (pPayload)
        vrc2 = mPayload.CopyFromDeep(*pPayload);
    else
        vrc2 = VINF_SUCCESS;
    if (RT_SUCCESS(vrc2))
    {
        mVrc = vrc;
        mGuestRc = vrcGuest;

        vrc2 = RTSemEventSignal(mEventSem);
    }

    return vrc2;
}

/**
 * Waits for the event to get triggered. Will return success if the
 * wait was successufl (e.g. was being triggered), otherwise an error will be returned.
 *
 * @returns VBox status code.
 * @retval  VERR_GSTCTL_GUEST_ERROR may be returned, call GuestResult() to get
 *          the actual result from the guest side.
 *
 * @param   msTimeout           Timeout (in ms) to wait.
 *                              Specifiy 0 to wait indefinitely.
 */
int GuestWaitEventBase::Wait(RTMSINTERVAL msTimeout)
{
    int vrc;
    if (!mfAborted)
    {
        AssertReturn(mEventSem != NIL_RTSEMEVENT, VERR_CANCELLED);

        vrc = RTSemEventWait(mEventSem, msTimeout ? msTimeout : RT_INDEFINITE_WAIT);
        if (   RT_SUCCESS(vrc)
            && mfAborted)
            vrc = VERR_CANCELLED;

        if (RT_SUCCESS(vrc))
        {
            /* If waiting succeeded, return the overall
             * result code. */
            vrc = mVrc;
        }
    }
    else
        vrc = VERR_CANCELLED;
    return vrc;
}

GuestWaitEvent::GuestWaitEvent(void)
{
}

GuestWaitEvent::~GuestWaitEvent(void)
{

}

/**
 * Cancels the event.
 */
int GuestWaitEvent::Cancel(void)
{
    if (mfAborted) /* Already aborted? */
        return VINF_SUCCESS;

    mfAborted = true;

#ifdef DEBUG_andy
    LogFlowThisFunc(("Cancelling %p ...\n"));
#endif
    return RTSemEventSignal(mEventSem);
}

/**
 * Initializes a wait event with a given context ID (CID).
 *
 * @returns VBox status code.
 * @param   uCID                Context ID to initialize wait event with.
 */
int GuestWaitEvent::Init(uint32_t uCID)
{
    return GuestWaitEventBase::Init(uCID);
}

/**
 * Initializes a wait event with a given context ID (CID) and a list of event types to wait for.
 *
 * @returns VBox status code.
 * @param   uCID                Context ID to initialize wait event with.
 * @param   lstEvents           List of event types to wait for this wait event to get signalled.
 */
int GuestWaitEvent::Init(uint32_t uCID, const GuestEventTypes &lstEvents)
{
    int vrc = GuestWaitEventBase::Init(uCID);
    if (RT_SUCCESS(vrc))
        mEventTypes = lstEvents;

    return vrc;
}

/**
 * Signals the event.
 *
 * @return  VBox status code.
 * @param   pEvent              Public IEvent to associate.
 *                              Optional.
 */
int GuestWaitEvent::SignalExternal(IEvent *pEvent)
{
    if (pEvent)
        mEvent = pEvent;

    return RTSemEventSignal(mEventSem);
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GuestPath
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Builds a (final) destination path from a given source + destination path.
 *
 * This does not utilize any file system access whatsoever. Used for guest and host paths.
 *
 * @returns VBox status code.
 * @param   strSrcPath          Source path to build destination path for.
 * @param   enmSrcPathStyle     Path style the source path is in.
 * @param   strDstPath          Destination path to use for building the (final) destination path.
 * @param   enmDstPathStyle     Path style the destination path is in.
 *
 * @note    See rules within the function.
 */
/* static */
int GuestPath::BuildDestinationPath(const Utf8Str &strSrcPath, PathStyle_T enmSrcPathStyle,
                                    Utf8Str &strDstPath, PathStyle_T enmDstPathStyle)
{
    /*
     * Rules:
     *
     * #    source       dest             final dest                        remarks
     *
     * 1    /src/path1/  /dst/path2/      /dst/path2/<contents of path1>    Just copies contents of <contents of path1>, not the path1 itself.
     * 2    /src/path1   /dst/path2/      /dst/path2/path1                  Copies path1 into path2.
     * 3    /src/path1   /dst/path2       /dst/path2                        Overwrites stuff from path2 with stuff from path1.
     * 4    Dotdot ("..") directories are forbidden for security reasons.
     */
    const char *pszSrcName = RTPathFilenameEx(strSrcPath.c_str(),
                                                enmSrcPathStyle == PathStyle_DOS
                                              ? RTPATH_STR_F_STYLE_DOS : RTPATH_STR_F_STYLE_UNIX);

    const char *pszDstName = RTPathFilenameEx(strDstPath.c_str(),
                                                enmDstPathStyle == PathStyle_DOS
                                              ? RTPATH_STR_F_STYLE_DOS : RTPATH_STR_F_STYLE_UNIX);

    if (   (!pszSrcName && !pszDstName)  /* #1 */
        || ( pszSrcName &&  pszDstName)) /* #3 */
    {
        /* Note: Must have DirectoryFlag_CopyIntoExisting + FileFlag_NoReplace *not* set. */
    }
    else if (pszSrcName && !pszDstName) /* #2 */
    {
        if (!strDstPath.endsWith(PATH_STYLE_SEP_STR(enmDstPathStyle)))
            strDstPath += PATH_STYLE_SEP_STR(enmDstPathStyle);
        strDstPath += pszSrcName;
    }

    /* Translate the built destination path to a path compatible with the destination. */
    int vrc = GuestPath::Translate(strDstPath, enmSrcPathStyle, enmDstPathStyle);
    if (RT_SUCCESS(vrc))
    {
        union
        {
            RTPATHPARSED    Parsed;
            RTPATHSPLIT     Split;
            uint8_t         ab[4096];
        } u;
        vrc = RTPathParse(strDstPath.c_str(), &u.Parsed, sizeof(u),
                          enmDstPathStyle == PathStyle_DOS ? RTPATH_STR_F_STYLE_DOS : RTPATH_STR_F_STYLE_UNIX);
        if (RT_SUCCESS(vrc))
        {
            if (u.Parsed.fProps & RTPATH_PROP_DOTDOT_REFS) /* #4 */
                vrc = VERR_INVALID_PARAMETER;
        }
    }

    LogRel2(("Guest Control: Building destination path for '%s' (%s) -> '%s' (%s): %Rrc\n",
             strSrcPath.c_str(), GuestBase::pathStyleToStr(enmSrcPathStyle),
             strDstPath.c_str(), GuestBase::pathStyleToStr(enmDstPathStyle), vrc));

    return vrc;
}

/**
 * Translates a path from a specific path style into another.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_SUPPORTED if a conversion is not supported.
 * @retval  VERR_NOT_IMPLEMENTED if path style conversion is not implemented yet.
 * @param   strPath             Path to translate. Will contain the translated path on success. UTF-8 only.
 * @param   enmSrcPathStyle     Source path style \a strPath is expected in.
 * @param   enmDstPathStyle     Destination path style to convert to.
 * @param   fForce              Whether to force the translation to the destination path style or not.
 *
 * @note    This does NOT remove any trailing slashes and/or perform file system lookups!
 */
/* static */
int GuestPath::Translate(Utf8Str &strPath, PathStyle_T enmSrcPathStyle, PathStyle_T enmDstPathStyle, bool fForce /* = false */)
{
    if (strPath.isEmpty())
        return VINF_SUCCESS;

    AssertReturn(RTStrIsValidEncoding(strPath.c_str()), VERR_INVALID_PARAMETER);

    int vrc = VINF_SUCCESS;

    Utf8Str strTranslated;

    if (   (   enmSrcPathStyle == PathStyle_DOS
            && enmDstPathStyle == PathStyle_UNIX)
        || (fForce && enmDstPathStyle == PathStyle_UNIX))
    {
        strTranslated = strPath;
        RTPathChangeToUnixSlashes(strTranslated.mutableRaw(), true /* fForce */);
    }
    else if (  (   enmSrcPathStyle == PathStyle_UNIX
                && enmDstPathStyle == PathStyle_DOS)
            || (fForce && enmDstPathStyle == PathStyle_DOS))

    {
        strTranslated = strPath;
        RTPathChangeToDosSlashes(strTranslated.mutableRaw(), true /* fForce */);
    }

    if (   strTranslated.isEmpty() /* Not forced. */
        && enmSrcPathStyle == enmDstPathStyle)
    {
        strTranslated = strPath;
    }

    if (RT_FAILURE(vrc))
    {
        LogRel(("Guest Control: Translating path '%s' (%s) -> '%s' (%s) failed, vrc=%Rrc\n",
                strPath.c_str(), GuestBase::pathStyleToStr(enmSrcPathStyle),
                strTranslated.c_str(), GuestBase::pathStyleToStr(enmDstPathStyle), vrc));
        return vrc;
    }

    /* Cleanup. */
    const char  *psz = strTranslated.mutableRaw();
    size_t const cch = strTranslated.length();
    size_t       off = 0;
    while (off < cch)
    {
        if (off + 1 > cch)
            break;
        /* Remove double back slashes (DOS only). */
        if (   enmDstPathStyle == PathStyle_DOS
            && psz[off]     == '\\'
            && psz[off + 1] == '\\')
        {
            strTranslated.erase(off + 1, 1);
            off++;
        }
        /* Remove double forward slashes (UNIX only). */
        if (   enmDstPathStyle == PathStyle_UNIX
            && psz[off]     == '/'
            && psz[off + 1] == '/')
        {
            strTranslated.erase(off + 1, 1);
            off++;
        }
        off++;
    }

    /* Note: Do not trim() paths here, as technically it's possible to create paths with trailing spaces. */

    strTranslated.jolt();

    LogRel2(("Guest Control: Translating '%s' (%s) -> '%s' (%s): %Rrc\n",
             strPath.c_str(), GuestBase::pathStyleToStr(enmSrcPathStyle),
             strTranslated.c_str(), GuestBase::pathStyleToStr(enmDstPathStyle), vrc));

    if (RT_SUCCESS(vrc))
        strPath = strTranslated;

    return vrc;
}

