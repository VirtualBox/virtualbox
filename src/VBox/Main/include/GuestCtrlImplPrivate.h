/* $Id: GuestCtrlImplPrivate.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef MAIN_INCLUDED_GuestCtrlImplPrivate_h
#define MAIN_INCLUDED_GuestCtrlImplPrivate_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "ConsoleImpl.h"
#include "Global.h"

#include <iprt/asm-mem.h>
#include <iprt/asm.h>
#include <iprt/env.h>
#include <iprt/semaphore.h>
#include <iprt/cpp/utils.h>

#include <VBox/com/com.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/string.h>
#include <VBox/com/VirtualBox.h>
#include <VBox/err.h> /* VERR_GSTCTL_GUEST_ERROR */

#include <map>
#include <vector>

using namespace com;

#ifdef VBOX_WITH_GUEST_CONTROL
# include <VBox/GuestHost/GuestControl.h>
# include <VBox/HostServices/GuestControlSvc.h>
using namespace guestControl;
#endif

/** Vector holding a process' CPU affinity. */
typedef std::vector<LONG> ProcessAffinity;
/** Vector holding process startup arguments. */
typedef std::vector<Utf8Str> ProcessArguments;

class GuestToolboxStreamBlock;
class GuestSession;


/**
 * Simple structure mantaining guest credentials.
 */
struct GuestCredentials
{
    Utf8Str                     mUser;
    Utf8Str                     mPassword;
    Utf8Str                     mDomain;
};


/**
 * Wrapper around the RTEnv API, unusable base class.
 *
 * @remarks Feel free to elevate this class to iprt/cpp/env.h as RTCEnv.
 */
class GuestEnvironmentBase
{
public:
    /**
     * Default constructor.
     *
     * The user must invoke one of the init methods before using the object.
     */
    GuestEnvironmentBase(void)
        : m_hEnv(NIL_RTENV)
        , m_cRefs(1)
        , m_fFlags(0)
    { }

    /**
     * Destructor.
     */
    virtual ~GuestEnvironmentBase(void)
    {
        Assert(m_cRefs <= 1);
        int vrc = RTEnvDestroy(m_hEnv); AssertRC(vrc);
        m_hEnv = NIL_RTENV;
    }

    /**
     * Retains a reference to this object.
     * @returns New reference count.
     * @remarks Sharing an object is currently only safe if no changes are made to
     *          it because RTENV does not yet implement any locking.  For the only
     *          purpose we need this, implementing IGuestProcess::environment by
     *          using IGuestSession::environmentBase, that's fine as the session
     *          base environment is immutable.
     */
    uint32_t retain(void)
    {
        uint32_t cRefs = ASMAtomicIncU32(&m_cRefs);
        Assert(cRefs > 1); Assert(cRefs < _1M);
        return cRefs;

    }
    /** Useful shortcut. */
    uint32_t retainConst(void) const { return unconst(this)->retain(); }

    /**
     * Releases a reference to this object, deleting the object when reaching zero.
     * @returns New reference count.
     */
    uint32_t release(void)
    {
        uint32_t cRefs = ASMAtomicDecU32(&m_cRefs);
        Assert(cRefs < _1M);
        if (cRefs == 0)
            delete this;
        return cRefs;
    }

    /** Useful shortcut. */
    uint32_t releaseConst(void) const { return unconst(this)->retain(); }

    /**
     * Checks if the environment has been successfully initialized or not.
     *
     * @returns @c true if initialized, @c false if not.
     */
    bool isInitialized(void) const
    {
        return m_hEnv != NIL_RTENV;
    }

    /**
     * Returns the variable count.
     * @return Number of variables.
     * @sa      RTEnvCountEx
     */
    uint32_t count(void) const
    {
        return RTEnvCountEx(m_hEnv);
    }

    /**
     * Deletes the environment change record entirely.
     *
     * The count() method will return zero after this call.
     *
     * @sa      RTEnvReset
     */
    void reset(void)
    {
        int vrc = RTEnvReset(m_hEnv);
        AssertRC(vrc);
    }

    /**
     * Exports the environment change block as an array of putenv style strings.
     *
     *
     * @returns VINF_SUCCESS or VERR_NO_MEMORY.
     * @param   pArray              The output array.
     */
    int queryPutEnvArray(std::vector<com::Utf8Str> *pArray) const
    {
        uint32_t cVars = RTEnvCountEx(m_hEnv);
        try
        {
            pArray->resize(cVars);
            for (uint32_t iVar = 0; iVar < cVars; iVar++)
            {
                const char *psz = RTEnvGetByIndexRawEx(m_hEnv, iVar);
                AssertReturn(psz, VERR_INTERNAL_ERROR_3); /* someone is racing us! */
                (*pArray)[iVar] = psz;
            }
            return VINF_SUCCESS;
        }
        catch (std::bad_alloc &)
        {
            return VERR_NO_MEMORY;
        }
    }

    /**
     * Applies an array of putenv style strings.
     *
     * @returns IPRT status code.
     * @param   rArray          The array with the putenv style strings.
     * @param   pidxError       Where to return the index causing trouble on
     *                          failure.  Optional.
     * @sa      RTEnvPutEx
     */
    int applyPutEnvArray(const std::vector<com::Utf8Str> &rArray, size_t *pidxError = NULL)
    {
        size_t const cArray = rArray.size();
        for (size_t i = 0; i < cArray; i++)
        {
            int vrc = RTEnvPutEx(m_hEnv, rArray[i].c_str());
            if (RT_FAILURE(vrc))
            {
                if (pidxError)
                    *pidxError = i;
                return vrc;
            }
        }
        return VINF_SUCCESS;
    }

    /**
     * Applies the changes from another environment to this.
     *
     * @returns IPRT status code.
     * @param   rChanges        Reference to an environment which variables will be
     *                          imported and, if it's a change record, schedule
     *                          variable unsets will be applied.
     * @sa      RTEnvApplyChanges
     */
    int applyChanges(const GuestEnvironmentBase &rChanges)
    {
        return RTEnvApplyChanges(m_hEnv, rChanges.m_hEnv);
    }

    /**
     * See RTEnvQueryUtf8Block for details.
     * @returns IPRT status code.
     * @param   ppszzBlock      Where to return the block pointer.
     * @param   pcbBlock        Where to optionally return the block size.
     * @sa      RTEnvQueryUtf8Block
     */
    int queryUtf8Block(char **ppszzBlock, size_t *pcbBlock)
    {
        return RTEnvQueryUtf8Block(m_hEnv, true /*fSorted*/, ppszzBlock, pcbBlock);
    }

    /**
     * Frees what queryUtf8Block returned, NULL ignored.
     * @sa      RTEnvFreeUtf8Block
     */
    static void freeUtf8Block(char *pszzBlock)
    {
        return RTEnvFreeUtf8Block(pszzBlock);
    }

    /**
     * Applies a block on the format returned by queryUtf8Block.
     *
     * @returns IPRT status code.
     * @param   pszzBlock           Pointer to the block.
     * @param   cbBlock             The size of the block.
     * @param   fNoEqualMeansUnset  Whether the lack of a '=' (equal) sign in a
     *                              string means it should be unset (@c true), or if
     *                              it means the variable should be defined with an
     *                              empty value (@c false, the default).
     * @todo move this to RTEnv!
     */
    int copyUtf8Block(const char *pszzBlock, size_t cbBlock, bool fNoEqualMeansUnset = false)
    {
        int vrc = VINF_SUCCESS;
        while (cbBlock > 0 && *pszzBlock != '\0')
        {
            const char *pszEnd = (const char *)memchr(pszzBlock, '\0', cbBlock);
            if (!pszEnd)
                return VERR_BUFFER_UNDERFLOW;
            int vrc2;
            if (fNoEqualMeansUnset || strchr(pszzBlock, '='))
                vrc2 = RTEnvPutEx(m_hEnv, pszzBlock);
            else
                vrc2 = RTEnvSetEx(m_hEnv, pszzBlock, "");
            if (RT_FAILURE(vrc2) && RT_SUCCESS(vrc))
                vrc = vrc2;

            /* Advance. */
            cbBlock -= pszEnd - pszzBlock;
            if (cbBlock < 2)
                return VERR_BUFFER_UNDERFLOW;
            cbBlock--;
            pszzBlock = pszEnd + 1;
        }

        /* The remainder must be zero padded. */
        if (RT_SUCCESS(vrc))
        {
            if (ASMMemIsZero(pszzBlock, cbBlock))
                return VINF_SUCCESS;
            return VERR_TOO_MUCH_DATA;
        }
        return vrc;
    }

    /**
     * Get an environment variable.
     *
     * @returns IPRT status code.
     * @param   rName               The variable name.
     * @param   pValue              Where to return the value.
     * @sa      RTEnvGetEx
     */
    int getVariable(const com::Utf8Str &rName, com::Utf8Str *pValue) const
    {
        size_t cchNeeded;
        int vrc = RTEnvGetEx(m_hEnv, rName.c_str(), NULL, 0, &cchNeeded);
        if (   RT_SUCCESS(vrc)
            || vrc == VERR_BUFFER_OVERFLOW)
        {
            try
            {
                pValue->reserve(cchNeeded + 1);
                vrc = RTEnvGetEx(m_hEnv, rName.c_str(), pValue->mutableRaw(), pValue->capacity(), NULL);
                pValue->jolt();
            }
            catch (std::bad_alloc &)
            {
                vrc = VERR_NO_STR_MEMORY;
            }
        }
        return vrc;
    }

    /**
     * Checks if the given variable exists.
     *
     * @returns @c true if it exists, @c false if not or if it's an scheduled unset
     *          in a environment change record.
     * @param   rName               The variable name.
     * @sa      RTEnvExistEx
     */
    bool doesVariableExist(const com::Utf8Str &rName) const
    {
        return RTEnvExistEx(m_hEnv, rName.c_str());
    }

    /**
     * Set an environment variable.
     *
     * @returns IPRT status code.
     * @param   rName               The variable name.
     * @param   rValue              The value of the variable.
     * @sa      RTEnvSetEx
     */
    int setVariable(const com::Utf8Str &rName, const com::Utf8Str &rValue)
    {
        return RTEnvSetEx(m_hEnv, rName.c_str(), rValue.c_str());
    }

    /**
     * Unset an environment variable.
     *
     * @returns IPRT status code.
     * @param   rName               The variable name.
     * @sa      RTEnvUnsetEx
     */
    int unsetVariable(const com::Utf8Str &rName)
    {
        return RTEnvUnsetEx(m_hEnv, rName.c_str());
    }

protected:
    /**
     * Copy constructor.
     * @throws HRESULT
     */
    GuestEnvironmentBase(const GuestEnvironmentBase &rThat, bool fChangeRecord, uint32_t fFlags = 0)
        : m_hEnv(NIL_RTENV)
        , m_cRefs(1)
        , m_fFlags(fFlags)
    {
        int vrc = cloneCommon(rThat, fChangeRecord);
        if (RT_FAILURE(vrc))
            throw Global::vboxStatusCodeToCOM(vrc);
    }

    /**
     * Common clone/copy method with type conversion abilities.
     *
     * @returns IPRT status code.
     * @param   rThat           The object to clone.
     * @param   fChangeRecord   Whether the this instance is a change record (true)
     *                          or normal (false) environment.
     */
    int cloneCommon(const GuestEnvironmentBase &rThat, bool fChangeRecord)
    {
        int   vrc = VINF_SUCCESS;
        RTENV hNewEnv = NIL_RTENV;
        if (rThat.m_hEnv != NIL_RTENV)
        {
            /*
             * Clone it.
             */
            if (RTEnvIsChangeRecord(rThat.m_hEnv) == fChangeRecord)
                vrc = RTEnvClone(&hNewEnv, rThat.m_hEnv);
            else
            {
                /* Need to type convert it. */
                if (fChangeRecord)
                    vrc = RTEnvCreateChangeRecordEx(&hNewEnv, rThat.m_fFlags);
                else
                    vrc = RTEnvCreateEx(&hNewEnv, rThat.m_fFlags);
                if (RT_SUCCESS(vrc))
                {
                    vrc = RTEnvApplyChanges(hNewEnv, rThat.m_hEnv);
                    if (RT_FAILURE(vrc))
                        RTEnvDestroy(hNewEnv);
                }
            }
        }
        else
        {
            /*
             * Create an empty one so the object works smoothly.
             * (Relevant for GuestProcessStartupInfo and internal commands.)
             */
            if (fChangeRecord)
                vrc = RTEnvCreateChangeRecordEx(&hNewEnv, rThat.m_fFlags);
            else
                vrc = RTEnvCreateEx(&hNewEnv, rThat.m_fFlags);
        }
        if (RT_SUCCESS(vrc))
        {
            RTEnvDestroy(m_hEnv);
            m_hEnv = hNewEnv;
            m_fFlags = rThat.m_fFlags;
        }
        return vrc;
    }


    /** The environment change record. */
    RTENV               m_hEnv;
    /** Reference counter. */
    uint32_t volatile   m_cRefs;
    /** RTENV_CREATE_F_XXX. */
    uint32_t            m_fFlags;
};

class GuestEnvironmentChanges;


/**
 * Wrapper around the RTEnv API for a normal environment.
 */
class GuestEnvironment : public GuestEnvironmentBase
{
public:
    /**
     * Default constructor.
     *
     * The user must invoke one of the init methods before using the object.
     */
    GuestEnvironment(void)
        : GuestEnvironmentBase()
    { }

    /**
     * Copy operator.
     * @param   rThat       The object to copy.
     * @throws HRESULT
     */
    GuestEnvironment(const GuestEnvironment &rThat)
        : GuestEnvironmentBase(rThat, false /*fChangeRecord*/)
    { }

    /**
     * Copy operator.
     * @param   rThat       The object to copy.
     * @throws HRESULT
     */
    GuestEnvironment(const GuestEnvironmentBase &rThat)
        : GuestEnvironmentBase(rThat, false /*fChangeRecord*/)
    { }

    /**
     * Initialize this as a normal environment block.
     * @returns IPRT status code.
     * @param   fFlags      RTENV_CREATE_F_XXX
     */
    int initNormal(uint32_t fFlags)
    {
        AssertReturn(m_hEnv == NIL_RTENV, VERR_WRONG_ORDER);
        m_fFlags = fFlags;
        return RTEnvCreateEx(&m_hEnv, fFlags);
    }

    /**
     * Replaces this environemnt with that in @a rThat.
     *
     * @returns IPRT status code
     * @param   rThat       The environment to copy. If it's a different type
     *                      we'll convert the data to a normal environment block.
     */
    int copy(const GuestEnvironmentBase &rThat)
    {
        return cloneCommon(rThat, false /*fChangeRecord*/);
    }

    /**
     * @copydoc GuestEnvironment::copy()
     */
    GuestEnvironment &operator=(const GuestEnvironmentBase &rThat)
    {
        int vrc = copy(rThat);
        if (RT_FAILURE(vrc))
            throw Global::vboxStatusCodeToCOM(vrc);
        return *this;
    }

    /** @copydoc GuestEnvironment::copy() */
    GuestEnvironment &operator=(const GuestEnvironment &rThat)
    {   return operator=((const GuestEnvironmentBase &)rThat); }

    /** @copydoc GuestEnvironment::copy() */
    GuestEnvironment &operator=(const GuestEnvironmentChanges &rThat)
    {   return operator=((const GuestEnvironmentBase &)rThat); }

};


/**
 * Wrapper around the RTEnv API for a environment change record.
 *
 * This class is used as a record of changes to be applied to a different
 * environment block (in VBoxService before launching a new process).
 */
class GuestEnvironmentChanges : public GuestEnvironmentBase
{
public:
    /**
     * Default constructor.
     *
     * The user must invoke one of the init methods before using the object.
     */
    GuestEnvironmentChanges(void)
        : GuestEnvironmentBase()
    { }

    /**
     * Copy operator.
     * @param   rThat       The object to copy.
     * @throws HRESULT
     */
    GuestEnvironmentChanges(const GuestEnvironmentChanges &rThat)
        : GuestEnvironmentBase(rThat, true /*fChangeRecord*/)
    { }

    /**
     * Copy operator.
     * @param   rThat       The object to copy.
     * @throws HRESULT
     */
    GuestEnvironmentChanges(const GuestEnvironmentBase &rThat)
        : GuestEnvironmentBase(rThat, true /*fChangeRecord*/)
    { }

    /**
     * Initialize this as a environment change record.
     * @returns IPRT status code.
     * @param   fFlags      RTENV_CREATE_F_XXX
     */
    int initChangeRecord(uint32_t fFlags)
    {
        AssertReturn(m_hEnv == NIL_RTENV, VERR_WRONG_ORDER);
        m_fFlags = fFlags;
        return RTEnvCreateChangeRecordEx(&m_hEnv, fFlags);
    }

    /**
     * Replaces this environemnt with that in @a rThat.
     *
     * @returns IPRT status code
     * @param   rThat       The environment to copy. If it's a different type
     *                      we'll convert the data to a set of changes.
     */
    int copy(const GuestEnvironmentBase &rThat)
    {
        return cloneCommon(rThat, true /*fChangeRecord*/);
    }

    /**
     * @copydoc GuestEnvironmentChanges::copy()
     * @throws  HRESULT
     */
    GuestEnvironmentChanges &operator=(const GuestEnvironmentBase &rThat)
    {
        int vrc = copy(rThat);
        if (RT_FAILURE(vrc))
            throw Global::vboxStatusCodeToCOM(vrc);
        return *this;
    }

    /** @copydoc GuestEnvironmentChanges::copy()
     * @throws  HRESULT */
    GuestEnvironmentChanges &operator=(const GuestEnvironmentChanges &rThat)
    {   return operator=((const GuestEnvironmentBase &)rThat); }

    /** @copydoc GuestEnvironmentChanges::copy()
     * @throws  HRESULT */
    GuestEnvironmentChanges &operator=(const GuestEnvironment &rThat)
    {   return operator=((const GuestEnvironmentBase &)rThat); }
};

/**
 * Class for keeping guest error information.
 */
class GuestErrorInfo
{
public:

    /**
     * Enumeration for specifying the guest error type.
     */
    enum Type
    {
        /** Guest error is anonymous. Avoid this. */
        Type_Anonymous = 0,
        /** Guest error is from a guest session. */
        Type_Session,
        /** Guest error is from a guest process. */
        Type_Process,
        /** Guest error is from a guest file object. */
        Type_File,
        /** Guest error is from a guest directory object. */
        Type_Directory,
        /** Guest error is from a file system operation. */
        Type_Fs,
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
        /** Guest error is from a the built-in toolbox "vbox_ls" command. */
        Type_ToolLs,
        /** Guest error is from a the built-in toolbox "vbox_rm" command. */
        Type_ToolRm,
        /** Guest error is from a the built-in toolbox "vbox_mkdir" command. */
        Type_ToolMkDir,
        /** Guest error is from a the built-in toolbox "vbox_mktemp" command. */
        Type_ToolMkTemp,
        /** Guest error is from a the built-in toolbox "vbox_stat" command. */
        Type_ToolStat,
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT */
        /** The usual 32-bit hack. */
        Type_32BIT_HACK = 0x7fffffff
    };

    /**
     * Initialization constructor.
     *
     * @param   eType           Error type to use.
     * @param   vrc             VBox status code to use.
     * @param   pcszWhat        Subject to use.
     */
    GuestErrorInfo(GuestErrorInfo::Type eType, int vrc, const char *pcszWhat)
    {
        int vrc2 = setV(eType, vrc, pcszWhat);
        if (RT_FAILURE(vrc2))
            throw vrc2;
    }

    /**
     * Returns the VBox status code for this error.
     *
     * @returns VBox status code.
     */
    int getVrc(void) const { return mVrc; }

    /**
     * Returns the type of this error.
     *
     * @returns Error type.
     */
    Type getType(void) const { return mType; }

    /**
     * Returns the subject of this error.
     *
     * @returns Subject as a string.
     */
    Utf8Str getWhat(void) const { return mWhat; }

    /**
     * Sets the error information using a variable arguments list (va_list).
     *
     * @returns VBox status code.
     * @param   eType           Error type to use.
     * @param   vrc             VBox status code to use.
     * @param   pcszWhat        Subject to use.
     */
    int setV(GuestErrorInfo::Type eType, int vrc, const char *pcszWhat)
    {
        mType = eType;
        mVrc  = vrc;
        mWhat = pcszWhat;

        return VINF_SUCCESS;
    }

protected:

    /** Error type. */
    Type    mType;
    /** VBox status (error) code. */
    int     mVrc;
    /** Subject string related to this error. */
    Utf8Str mWhat;
};

/**
 * Structure for keeping all the relevant guest directory
 * information around.
 */
struct GuestDirectoryOpenInfo
{
    GuestDirectoryOpenInfo(void)
        : menmFilter(GSTCTLDIRFILTER_NONE)
        , mFlags(GSTCTLDIR_F_NONE) { }

    /** The directory path. */
    Utf8Str                 mPath;
    /** The filter to use (wildcard style). */
    Utf8Str                 mFilter;
    /** The filter option to use. */
    GSTCTLDIRFILTER         menmFilter;
    /** Opening flags (of type GSTCTLDIR_F_XXX). */
    uint32_t                mFlags;
};


/**
 * Structure for keeping all the relevant guest file
 * information around.
 */
struct GuestFileOpenInfo
{
    GuestFileOpenInfo(void)
        : mAccessMode((FileAccessMode_T)0)
        , mOpenAction((FileOpenAction_T)0)
        , mSharingMode((FileSharingMode_T)0)
        , mCreationMode(0)
        , mfOpenEx(0) { }

    /**
     * Validates a file open info.
     *
     * @returns \c true if valid, \c false if not.
     */
    bool IsValid(void) const
    {
        if (mfOpenEx) /** @todo Open flags not implemented yet. */
            return false;

        switch (mOpenAction)
        {
            case FileOpenAction_OpenExisting:
                break;
            case FileOpenAction_OpenOrCreate:
                break;
            case FileOpenAction_CreateNew:
                break;
            case FileOpenAction_CreateOrReplace:
                break;
            case FileOpenAction_OpenExistingTruncated:
            {
                if (   mAccessMode == FileAccessMode_ReadOnly
                    || mAccessMode == FileAccessMode_AppendOnly
                    || mAccessMode == FileAccessMode_AppendRead)
                    return false;
                break;
            }
            case FileOpenAction_AppendOrCreate: /* Deprecated, do not use. */
                break;
            default:
                AssertFailedReturn(false);
                break;
        }

        return true; /** @todo Do we need more checks here? */
    }

    /** The filename. */
    Utf8Str                 mFilename;
    /** The file access mode. */
    FileAccessMode_T        mAccessMode;
    /** The file open action.  */
    FileOpenAction_T        mOpenAction;
    /** The file sharing mode. */
    FileSharingMode_T       mSharingMode;
    /** Octal creation mode. */
    uint32_t                mCreationMode;
    /** Extended open flags (currently none defined). */
    uint32_t                mfOpenEx;
};


/**
 * Helper class for guest file system operations.
 */
class GuestFs
{
    DECLARE_TRANSLATE_METHODS(GuestFs)

private:

    /* Not directly instantiable. */
    GuestFs(void) { }

public:

    static Utf8Str guestErrorToString(const GuestErrorInfo &guestErrorInfo);
};


/**
 * Structure representing information of a
 * file system object.
 */
struct GuestFsObjData
{
    GuestFsObjData(const Utf8Str &strName = "")
        : mType(FsObjType_Unknown)
        , mObjectSize(0)
        , mAllocatedSize(0)
        , mAccessTime(0)
        , mBirthTime(0)
        , mChangeTime(0)
        , mModificationTime(0)
        , mUID(0)
        , mGID(0)
        , mNodeID(0)
        , mNodeIDDevice(0)
        , mNumHardLinks(0)
        , mDeviceNumber(0)
        , mGenerationID(0)
        , mUserFlags(0) { mName = strName; }

    void Init(const Utf8Str &strName) { mName = strName; }

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
    int FromGuestDirEntryEx(PCGSTCTLDIRENTRYEX pDirEntryEx, const Utf8Str &strUser = "", const Utf8Str &strGroups = "");
    int FromGuestFsObjInfo(PCGSTCTLFSOBJINFO pFsObjInfo, const Utf8Str &strUser = "", const Utf8Str &strGroups = "");
#endif

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
    /** @name Helper functions to extract the data from a certin VBoxService tool's guest stream block.
     * @{ */
    int FromToolboxLs(const GuestToolboxStreamBlock &strmBlk, bool fLong);
    int FromToolboxRm(const GuestToolboxStreamBlock &strmBlk);
    int FromToolboxStat(const GuestToolboxStreamBlock &strmBlk);
    int FromToolboxMkTemp(const GuestToolboxStreamBlock &strmBlk);
    /** @}  */
#endif

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
    /** @name Static helper functions to work with time from stream block keys.
     * @{ */
    static PRTTIMESPEC TimeSpecFromKey(const GuestToolboxStreamBlock &strmBlk, const Utf8Str &strKey, PRTTIMESPEC pTimeSpec);
    static int64_t UnixEpochNsFromKey(const GuestToolboxStreamBlock &strmBlk, const Utf8Str &strKey);
    /** @}  */
#endif

    /** @name helper functions to work with IPRT stuff.
     * @{ */
    RTFMODE GetFileMode(void) const;
    /** @}  */

    Utf8Str              mName;
    FsObjType_T          mType;
    Utf8Str              mFileAttrs;
    int64_t              mObjectSize;
    int64_t              mAllocatedSize;
    int64_t              mAccessTime;
    int64_t              mBirthTime;
    int64_t              mChangeTime;
    int64_t              mModificationTime;
    Utf8Str              mUserName;
    int32_t              mUID;
    int32_t              mGID;
    Utf8Str              mGroupName;
    Utf8Str              mACL;
    int64_t              mNodeID;
    uint32_t             mNodeIDDevice;
    uint32_t             mNumHardLinks;
    uint32_t             mDeviceNumber;
    uint32_t             mGenerationID;
    uint32_t             mUserFlags;
};


/**
 * Structure for keeping all the relevant guest session
 * startup parameters around.
 */
class GuestSessionStartupInfo
{
public:

    GuestSessionStartupInfo(void)
        : mID(UINT32_MAX)
        , mIsInternal(false /* Non-internal session */)
        , mOpenTimeoutMS(GSTCTL_DEFAULT_TIMEOUT_MS)
        , mOpenFlags(0 /* No opening flags set */) { }

    /** The session's friendly name. Optional. */
    Utf8Str                     mName;
    /** The session's unique ID. Used to encode a context ID.
     *  UINT32_MAX if not initialized. */
    uint32_t                    mID;
    /** Flag indicating if this is an internal session
     *  or not. Internal session are not accessible by
     *  public API clients. */
    bool                        mIsInternal;
    /** Timeout (in ms) used for opening the session. */
    uint32_t                    mOpenTimeoutMS;
    /** Session opening flags. */
    uint32_t                    mOpenFlags;
};


/**
 * Structure for keeping all the relevant guest process
 * startup parameters around.
 */
class GuestProcessStartupInfo
{
public:

    GuestProcessStartupInfo(void)
        : mFlags(ProcessCreateFlag_None)
        , mTimeoutMS(UINT32_MAX /* No timeout by default */)
        , mPriority(ProcessPriority_Default)
        , mAffinity(0) { }

    /** The process' friendly name. */
    Utf8Str                     mName;
    /** The executable. */
    Utf8Str                     mExecutable;
    /** The working directory. Optional, can be empty if not used. */
    Utf8Str                     mCwd;
    /** Arguments vector (starting with argument \#0). */
    ProcessArguments            mArguments;
    /** The process environment change record.  */
    GuestEnvironmentChanges     mEnvironmentChanges;
    /** Process creation flags. */
    uint32_t                    mFlags;
    /** Timeout (in ms) the process is allowed to run.
     *  Specify UINT32_MAX if no timeout (unlimited run time) is given. */
    ULONG                       mTimeoutMS;
    /** Process priority. */
    ProcessPriority_T           mPriority;
    /** Process affinity. At the moment we
     *  only support 64 VCPUs. API and
     *  guest can do more already!  */
    uint64_t                    mAffinity;
};

/**
 * Generic class for handling a guest process output (i.e. stdout / stderr) stream.
 */
class GuestProcessOutputStream
{
public:

    GuestProcessOutputStream();

    virtual ~GuestProcessOutputStream();

public:

    int AddData(const BYTE *pbData, size_t cbData);

    void Destroy();

#ifdef DEBUG
    void Dump(const char *pszFile);
#endif

    size_t GetOffset(void) const { return m_offBuf; }

    size_t GetSize(void) const { return m_cbUsed; }

    const BYTE *GetData(void) const { return m_pbBuffer; }

protected:

    /** Maximum allowed size the stream buffer can grow to.
     *  Defaults to 32 MB. */
    size_t m_cbMax;
    /** Currently allocated size of internal stream buffer. */
    size_t m_cbAllocated;
    /** Currently used size at m_offBuffer. */
    size_t m_cbUsed;
    /** Current byte offset within the internal stream buffer. */
    size_t m_offBuf;
    /** Internal stream buffer. */
    BYTE  *m_pbBuffer;
};

/**
 * Class representing the "value" side of a "key=value" pair.
 */
class GuestToolboxStreamValue
{
public:

    GuestToolboxStreamValue(void) { }
    GuestToolboxStreamValue(const char *pszValue, size_t cwcValue = RTSTR_MAX)
        : mValue(pszValue, cwcValue) {}

    GuestToolboxStreamValue(const GuestToolboxStreamValue& aThat)
           : mValue(aThat.mValue) { }

    /** Copy assignment operator. */
    GuestToolboxStreamValue &operator=(GuestToolboxStreamValue const &a_rThat) RT_NOEXCEPT
    {
        mValue = a_rThat.mValue;

        return *this;
    }

    Utf8Str mValue;
};

/** Map containing "key=value" pairs of a guest process stream. */
typedef std::pair< Utf8Str, GuestToolboxStreamValue > GuestCtrlStreamPair;
typedef std::map < Utf8Str, GuestToolboxStreamValue > GuestCtrlStreamPairMap;
typedef std::map < Utf8Str, GuestToolboxStreamValue >::iterator GuestCtrlStreamPairMapIter;
typedef std::map < Utf8Str, GuestToolboxStreamValue >::const_iterator GuestCtrlStreamPairMapIterConst;

class GuestToolboxStream;

/**
 * Class representing a block of stream pairs (key=value). Each block in a raw guest
 * output stream is separated by "\0\0", each pair is separated by "\0". The overall
 * end of a guest stream is marked by "\0\0\0\0".
 *
 * An empty stream block will be treated as being incomplete.
 *
 * Only used for the busybox-like toolbox commands within VBoxService.
 *
 * Deprecated, do not use anymore.
 */
class GuestToolboxStreamBlock
{
    friend GuestToolboxStream;

public:

    GuestToolboxStreamBlock(void);

    virtual ~GuestToolboxStreamBlock(void);

public:

    void Clear(void);

#ifdef DEBUG
    void DumpToLog(void) const;
#endif

    const char *GetString(const char *pszKey) const;
    size_t      GetCount(void) const;
    int         GetVrc(bool fSucceedIfNotFound = false) const;
    int         GetInt64Ex(const char *pszKey, int64_t *piVal) const;
    int64_t     GetInt64(const char *pszKey) const;
    int         GetUInt32Ex(const char *pszKey, uint32_t *puVal) const;
    uint32_t    GetUInt32(const char *pszKey, uint32_t uDefault = 0) const;
    int32_t     GetInt32(const char *pszKey, int32_t iDefault = 0) const;

    bool        IsComplete(void) const { return !m_mapPairs.empty() && m_fComplete; }
    bool        IsEmpty(void) const { return m_mapPairs.empty(); }

    int         SetValueEx(const char *pszKey, size_t cwcKey, const char *pszValue, size_t cwcValue, bool fOverwrite = false);
    int         SetValue(const char *pszKey, const char *pszValue);

protected:

    /** Wheter the stream block is marked as complete.
     *  An empty stream block is considered as incomplete. */
    bool                   m_fComplete;
    /** Map of stream pairs this block contains.*/
    GuestCtrlStreamPairMap m_mapPairs;
};

/** Vector containing multiple allocated stream pair objects. */
typedef std::vector< GuestToolboxStreamBlock > GuestCtrlStreamObjects;
typedef std::vector< GuestToolboxStreamBlock >::iterator GuestCtrlStreamObjectsIter;
typedef std::vector< GuestToolboxStreamBlock >::const_iterator GuestCtrlStreamObjectsIterConst;

/** Defines a single terminator as a single char. */
#define GUESTTOOLBOX_STRM_TERM                      '\0'
/** Defines a single terminator as a string. */
#define GUESTTOOLBOX_STRM_TERM_STR                  "\0"
/** Defines the termination sequence for a single key/value pair. */
#define GUESTTOOLBOX_STRM_TERM_PAIR_STR             GUESTTOOLBOX_STRM_TERM_STR
/** Defines the termination sequence for a single stream block. */
#define GUESTTOOLBOX_STRM_TERM_BLOCK_STR            GUESTTOOLBOX_STRM_TERM_STR GUESTTOOLBOX_STRM_TERM_STR
/** Defines the termination sequence for the stream. */
#define GUESTTOOLBOX_STRM_TERM_STREAM_STR           GUESTTOOLBOX_STRM_TERM_STR GUESTTOOLBOX_STRM_TERM_STR GUESTTOOLBOX_STRM_TERM_STR GUESTTOOLBOX_STRM_TERM_STR
/** Defines how many consequtive terminators a key/value pair has. */
#define GUESTTOOLBOX_STRM_PAIR_TERM_CNT             1
/** Defines how many consequtive terminators a stream block has. */
#define GUESTTOOLBOX_STRM_BLK_TERM_CNT              2
/** Defines how many consequtive terminators a stream has. */
#define GUESTTOOLBOX_STRM_TERM_CNT                  4

/**
 * Class for parsing machine-readable guest process output by VBoxService'
 * toolbox commands ("vbox_ls", "vbox_stat" etc), aka "guest stream".
 *
 * Deprecated, do not use anymore.
 */
class GuestToolboxStream : public GuestProcessOutputStream
{

public:

    GuestToolboxStream();

    virtual ~GuestToolboxStream();

public:

    size_t GetBlocks(void) const { return m_cBlocks; }

    int ParseBlock(GuestToolboxStreamBlock &streamBlock);

protected:

    /** How many completed stream blocks already were processed. */
    size_t m_cBlocks;
};

class Guest;
class Progress;

class GuestWaitEventPayload
{

public:

    GuestWaitEventPayload(void)
        : uType(0)
        , cbData(0)
        , pvData(NULL)
    { }

    /**
     * Initialization constructor.
     *
     * @throws  VBox status code (vrc).
     *
     * @param   uTypePayload    Payload type to set.
     * @param   pvPayload       Pointer to payload data to set (deep copy).
     * @param   cbPayload       Size (in bytes) of payload data to set.
     */
    GuestWaitEventPayload(uint32_t uTypePayload, const void *pvPayload, uint32_t cbPayload)
        : uType(0)
        , cbData(0)
        , pvData(NULL)
    {
        int vrc = copyFrom(uTypePayload, pvPayload, cbPayload);
        if (RT_FAILURE(vrc))
            throw vrc;
    }

    virtual ~GuestWaitEventPayload(void)
    {
        Clear();
    }

    GuestWaitEventPayload& operator=(const GuestWaitEventPayload &that)
    {
        CopyFromDeep(that);
        return *this;
    }

public:

    void Clear(void)
    {
        if (pvData)
        {
            Assert(cbData);
            RTMemFree(pvData);
            cbData = 0;
            pvData = NULL;
        }
        uType = 0;
    }

    int CopyFromDeep(const GuestWaitEventPayload &payload)
    {
        return copyFrom(payload.uType, payload.pvData, payload.cbData);
    }

    const void* Raw(void) const { return pvData; }

    size_t Size(void) const { return cbData; }

    uint32_t Type(void) const { return uType; }

    void* MutableRaw(void) { return pvData; }

    Utf8Str ToString(void)
    {
        const char  *pszStr = (const char *)pvData;
              size_t cbStr  = cbData;

        if (RT_FAILURE(RTStrValidateEncodingEx(pszStr, cbStr,
                                               RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED | RTSTR_VALIDATE_ENCODING_EXACT_LENGTH)))
        {
            AssertFailed();
            return "";
        }

        return Utf8Str(pszStr, cbStr);
    }

    /**
     * Returns the payload as a vector of strings, validated.
     *
     * The payload data must contain the strings separated by a string zero terminator each,
     * ending with a separate zero terminator. Incomplete data will considered as invalid data.
     *
     * Example: 'foo\0bar\0baz\0\0'.
     *
     * @returns VBox status code.
     * @param   vecStrings      Where to return the vector of strings on success.
     */
    int ToStringVector(std::vector<Utf8Str> &vecStrings)
    {
        int vrc = VINF_SUCCESS;

        vecStrings.clear();

        const char *psz = (const char *)pvData;
        if (psz)
        {
            size_t cb  = cbData;
            while (cb)
            {
                size_t const cch = strnlen(psz, cb);
                if (!cch)
                    break;
                size_t const cbStr = RT_MIN(cb, cch + 1 /* String terminator */);
                vrc = RTStrValidateEncodingEx(psz, cbStr,
                                              RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED | RTSTR_VALIDATE_ENCODING_EXACT_LENGTH);
                if (RT_FAILURE(vrc))
                    break;
                try
                {
                    vecStrings.push_back(Utf8Str(psz, cch));
                }
                catch (std::bad_alloc &)
                {
                    AssertFailedBreakStmt(vrc = VERR_NO_MEMORY);
                }
                AssertBreakStmt(cb >= cbStr, vrc = VERR_INVALID_PARAMETER);
                cb  -= cbStr;
                psz += cbStr;
            }

            if (RT_SUCCESS(vrc))
                AssertStmt(cb <= 1 /* Ending terminator */, vrc = VERR_INVALID_PARAMETER);
        }
        return vrc;
    }

protected:

    int copyFrom(uint32_t uTypePayload, const void *pvPayload, uint32_t cbPayload)
    {
        if (cbPayload > _64K) /* Paranoia. */
            return VERR_TOO_MUCH_DATA;

        Clear();

        int vrc = VINF_SUCCESS;
        if (cbPayload)
        {
            pvData = RTMemAlloc(cbPayload);
            if (pvData)
            {
                uType = uTypePayload;

                memcpy(pvData, pvPayload, cbPayload);
                cbData = cbPayload;
            }
            else
                vrc = VERR_NO_MEMORY;
        }
        else
        {
            uType = uTypePayload;

            pvData = NULL;
            cbData = 0;
        }

        return vrc;
    }

protected:

    /** Type of payload. */
    uint32_t uType;
    /** Size (in bytes) of payload. */
    uint32_t cbData;
    /** Pointer to actual payload data. */
    void    *pvData;
};

class GuestWaitEventBase
{

protected:

    GuestWaitEventBase(void);
    virtual ~GuestWaitEventBase(void);

public:

    uint32_t               ContextID(void) const { return mCID; };
    int                    GuestResult(void) const { return mGuestRc; }
    bool                   HasGuestError(void) const { return mVrc == VERR_GSTCTL_GUEST_ERROR; }
    int                    Result(void) const { return mVrc; }
    GuestWaitEventPayload &Payload(void) { return mPayload; }
    int                    SignalInternal(int vrc, int vrcGuest, const GuestWaitEventPayload *pPayload);
    int                    Wait(RTMSINTERVAL uTimeoutMS);

protected:

    int             Init(uint32_t uCID);

protected:

    /** Shutdown indicator. */
    bool                       mfAborted;
    /** Associated context ID (CID). */
    uint32_t                   mCID;
    /** The event semaphore for triggering the actual event. */
    RTSEMEVENT                 mEventSem;
    /** The event's overall result.
     * If set to VERR_GSTCTL_GUEST_ERROR, mGuestRc will contain the actual
     * error code from the guest side. */
    int                        mVrc;
    /** The event'S overall result from the guest side.
     * If used, mVrc must be set to VERR_GSTCTL_GUEST_ERROR. */
    int                        mGuestRc;
    /** The event's payload data. Optional. */
    GuestWaitEventPayload      mPayload;
};

/** List of public guest event types. */
typedef std::list < VBoxEventType_T > GuestEventTypes;

class GuestWaitEvent : public GuestWaitEventBase
{

public:

    GuestWaitEvent(void);
    virtual ~GuestWaitEvent(void);

public:

    int                              Init(uint32_t uCID);
    int                              Init(uint32_t uCID, const GuestEventTypes &lstEvents);
    int                              Cancel(void);
    const ComPtr<IEvent>             Event(void) const { return mEvent; }
    int                              SignalExternal(IEvent *pEvent);
    const GuestEventTypes           &Types(void) const { return mEventTypes; }
    size_t                           TypeCount(void) const { return mEventTypes.size(); }

protected:

    /** List of public event types this event should
     *  be signalled on. Optional. */
    GuestEventTypes            mEventTypes;
    /** Pointer to the actual public event, if any. */
    ComPtr<IEvent>             mEvent;
};
/** Map of pointers to guest events. The primary key
 *  contains the context ID. */
typedef std::map < uint32_t, GuestWaitEvent* > GuestWaitEvents;
/** Map of wait events per public guest event. Nice for
 *  faster lookups when signalling a whole event group. */
typedef std::map < VBoxEventType_T, GuestWaitEvents > GuestEventGroup;

class GuestBase
{

public:

    GuestBase(void);
    virtual ~GuestBase(void);

public:

    /** Signals a wait event using a public guest event; also used for
     *  for external event listeners. */
    int signalWaitEvent(VBoxEventType_T aType, IEvent *aEvent);
    /** Signals a wait event using a guest vrc. */
    int signalWaitEventInternal(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, int vrcGuest, const GuestWaitEventPayload *pPayload);
    /** Signals a wait event without letting public guest events know,
     *  extended director's cut version. */
    int signalWaitEventInternalEx(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, int vrc, int vrcGuest, const GuestWaitEventPayload *pPayload);

public:

    int baseInit(void);
    void baseUninit(void);
    int cancelWaitEvents(void);
    int dispatchGeneric(PVBOXGUESTCTRLHOSTCBCTX pCtxCb, PVBOXGUESTCTRLHOSTCALLBACK pSvcCb);
    int generateContextID(uint32_t uSessionID, uint32_t uObjectID, uint32_t *puContextID);
    int registerWaitEvent(uint32_t uSessionID, uint32_t uObjectID, GuestWaitEvent **ppEvent);
    int registerWaitEventEx(uint32_t uSessionID, uint32_t uObjectID, const GuestEventTypes &lstEvents, GuestWaitEvent **ppEvent);
    int unregisterWaitEvent(GuestWaitEvent *pEvent);
    int waitForEvent(GuestWaitEvent *pEvent, uint32_t uTimeoutMS, VBoxEventType_T *pType, IEvent **ppEvent);

public:

    static FsObjType_T fileModeToFsObjType(RTFMODE fMode);
    static const char *fsObjTypeToStr(FsObjType_T enmType);
    static const char *pathStyleToStr(PathStyle_T enmPathStyle);
    static Utf8Str getErrorAsString(const Utf8Str &strAction, const GuestErrorInfo& guestErrorInfo);
    static Utf8Str getErrorAsString(const GuestErrorInfo &guestErrorInfo);

protected:

    /** Pointer to the console object. Needed
     *  for HGCM (VMMDev) communication. */
    Console                 *mConsole;
    /**  The next context ID counter component for this object. */
    uint32_t                 mNextContextID;
    /** Local listener for handling the waiting events
     *  internally. */
    ComPtr<IEventListener>   mLocalListener;
    /** Critical section for wait events access. */
    RTCRITSECT               mWaitEventCritSect;
    /** Map of registered wait events per event group. */
    GuestEventGroup          mWaitEventGroups;
    /** Map of registered wait events. */
    GuestWaitEvents          mWaitEvents;
};

/**
 * Virtual class (interface) for guest objects (processes, files, ...) --
 * contains all per-object callback management.
 */
class GuestObject : public GuestBase
{
    friend class GuestSession;

public:

    GuestObject(void);
    virtual ~GuestObject(void);

public:

    ULONG getObjectID(void) { return mObjectID; }

protected:

    /**
     * Called by IGuestSession when the session status has been changed.
     *
     * @returns VBox status code.
     * @param   enmSessionStatus    New session status.
     */
    virtual int i_onSessionStatusChange(GuestSessionStatus_T enmSessionStatus) = 0;

    /**
     * Called by IGuestSession right before this object gets
     * unregistered (removed) from the public object list.
     */
    virtual int i_onUnregister(void) = 0;

    /** Callback dispatcher -- must be implemented by the actual object. */
    virtual int i_callbackDispatcher(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCb) = 0;

protected:

    int bindToSession(Console *pConsole, GuestSession *pSession, uint32_t uObjectID);
    int registerWaitEvent(const GuestEventTypes &lstEvents, GuestWaitEvent **ppEvent);
    int sendMessage(uint32_t uFunction, uint32_t cParms, PVBOXHGCMSVCPARM paParms);

protected:

    /** @name Common parameters for all derived objects.  They have their own
     * mData structure to keep their specific data around.
     * @{ */
    /** Pointer to parent session. Per definition
     *  this objects *always* lives shorter than the
     *  parent.
     * @todo r=bird: When wanting to use mSession in the
     * IGuestProcess::getEnvironment() implementation I wanted to access
     * GuestSession::mData::mpBaseEnvironment.  Seeing the comment in
     * GuestProcess::terminate() saying:
     *      "Now only API clients still can hold references to it."
     * and recalling seeing similar things in VirtualBox.xidl or some such place,
     * I'm wondering how this "per definition" behavior is enforced.  Is there any
     * GuestProcess:uninit() call or similar magic that invalidates objects that
     * GuestSession loses track of in place like GuestProcess::terminate() that I've
     * failed to spot?
     *
     * Please enlighten me.
     */
    GuestSession            *mSession;
    /** The object ID -- must be unique for each guest
     *  object and is encoded into the context ID. Must
     *  be set manually when initializing the object.
     *
     *  For guest processes this is the internal PID,
     *  for guest files this is the internal file ID. */
    uint32_t                 mObjectID;
    /** @} */
};

/** Returns the path separator based on \a a_enmPathStyle as a C-string. */
#define PATH_STYLE_SEP_STR(a_enmPathStyle) (a_enmPathStyle == PathStyle_DOS ? "\\" : "/")
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
# define PATH_STYLE_NATIVE               PathStyle_DOS
#else
# define PATH_STYLE_NATIVE               PathStyle_UNIX
#endif

/**
 * Class for handling guest / host path functions.
 */
class GuestPath
{
private:

    /**
     * Default constructor.
     *
     * Not directly instantiable (yet).
     */
    GuestPath(void) { }

public:

    /** @name Static helper functions.
     * @{ */
    static int BuildDestinationPath(const Utf8Str &strSrcPath, PathStyle_T enmSrcPathStyle, Utf8Str &strDstPath, PathStyle_T enmDstPathStyle);
    static int Translate(Utf8Str &strPath, PathStyle_T enmSrcPathStyle, PathStyle_T enmDstPathStyle, bool fForce = false);
    /** @}  */
};


/*********************************************************************************************************************************
 * Callback data structures.                                                                                                     *
 *                                                                                                                               *
 * These structures make up the actual low level HGCM callback data sent from                                                    *
 * the guest back to the host.                                                                                                   *
 ********************************************************************************************************************************/

/**
 * The guest control callback data header. Must come first
 * on each callback structure defined below this struct.
 */
typedef struct CALLBACKDATA_HEADER
{
    /** Context ID to identify callback data. This is
     *  and *must* be the very first parameter in this
     *  structure to still be backwards compatible. */
    uint32_t uContextID;
} CALLBACKDATA_HEADER;
/** Pointer to a CALLBACKDATA_HEADER struct. */
typedef CALLBACKDATA_HEADER *PCALLBACKDATA_HEADER;

/**
 * Host service callback data when a HGCM client disconnected.
 */
typedef struct CALLBACKDATA_CLIENT_DISCONNECTED
{
    /** Callback data header. */
    CALLBACKDATA_HEADER hdr;
} CALLBACKDATA_CLIENT_DISCONNECTED;
/** Pointer to a CALLBACKDATA_CLIENT_DISCONNECTED struct. */
typedef CALLBACKDATA_CLIENT_DISCONNECTED *PCALLBACKDATA_CLIENT_DISCONNECTED;

/**
 * Host service callback data for a generic guest reply.
 */
typedef struct CALLBACKDATA_MSG_REPLY
{
    /** Callback data header. */
    CALLBACKDATA_HEADER hdr;
    /** Notification type. */
    uint32_t uType;
    /** Notification result. Note: int vs. uint32! */
    uint32_t rc;
    /** Pointer to optional payload. */
    void *pvPayload;
    /** Payload size (in bytes). */
    uint32_t cbPayload;
} CALLBACKDATA_MSG_REPLY;
/** Pointer to a CALLBACKDATA_MSG_REPLY struct. */
typedef CALLBACKDATA_MSG_REPLY *PCALLBACKDATA_MSG_REPLY;

/**
 * Host service callback data for guest session notifications.
 */
typedef struct CALLBACKDATA_SESSION_NOTIFY
{
    /** Callback data header. */
    CALLBACKDATA_HEADER hdr;
    /** Notification type. */
    uint32_t uType;
    /** Notification result. Note: int vs. uint32! */
    uint32_t uResult;
} CALLBACKDATA_SESSION_NOTIFY;
/** Pointer to a CALLBACKDATA_SESSION_NOTIFY struct. */
typedef CALLBACKDATA_SESSION_NOTIFY *PCALLBACKDATA_SESSION_NOTIFY;

/**
 * Host service callback data for guest process status notifications.
 */
typedef struct CALLBACKDATA_PROC_STATUS
{
    /** Callback data header. */
    CALLBACKDATA_HEADER hdr;
    /** The process ID (PID). */
    uint32_t uPID;
    /** The process status. */
    uint32_t uStatus;
    /** Optional flags, varies, based on u32Status. */
    uint32_t uFlags;
    /** Optional data buffer (not used atm). */
    void *pvData;
    /** Size of optional data buffer (not used atm). */
    uint32_t cbData;
} CALLBACKDATA_PROC_STATUS;
/** Pointer to a CALLBACKDATA_PROC_OUTPUT struct. */
typedef CALLBACKDATA_PROC_STATUS* PCALLBACKDATA_PROC_STATUS;

/**
 * Host service callback data for guest process output notifications.
 */
typedef struct CALLBACKDATA_PROC_OUTPUT
{
    /** Callback data header. */
    CALLBACKDATA_HEADER hdr;
    /** The process ID (PID). */
    uint32_t uPID;
    /** The handle ID (stdout/stderr). */
    uint32_t uHandle;
    /** Optional flags (not used atm). */
    uint32_t uFlags;
    /** Optional data buffer. */
    void *pvData;
    /** Size (in bytes) of optional data buffer. */
    uint32_t cbData;
} CALLBACKDATA_PROC_OUTPUT;
/** Pointer to a CALLBACKDATA_PROC_OUTPUT struct. */
typedef CALLBACKDATA_PROC_OUTPUT *PCALLBACKDATA_PROC_OUTPUT;

/**
 * Host service callback data guest process input notifications.
 */
typedef struct CALLBACKDATA_PROC_INPUT
{
    /** Callback data header. */
    CALLBACKDATA_HEADER hdr;
    /** The process ID (PID). */
    uint32_t uPID;
    /** Current input status. */
    uint32_t uStatus;
    /** Optional flags. */
    uint32_t uFlags;
    /** Size (in bytes) of processed input data. */
    uint32_t uProcessed;
} CALLBACKDATA_PROC_INPUT;
/** Pointer to a CALLBACKDATA_PROC_INPUT struct. */
typedef CALLBACKDATA_PROC_INPUT *PCALLBACKDATA_PROC_INPUT;

/**
 * General guest file notification callback.
 */
typedef struct CALLBACKDATA_FILE_NOTIFY
{
    /** Callback data header. */
    CALLBACKDATA_HEADER hdr;
    /** Notification type. */
    uint32_t uType;
    /** IPRT result of overall operation. */
    uint32_t rc;
    union
    {
        struct
        {
            /** Guest file handle. */
            uint32_t uHandle;
        } open;
        /** Note: Close does not have any additional data (yet). */
        struct
        {
            /** How much data (in bytes) have been read. */
            uint32_t cbData;
            /** Actual data read (if any). */
            void *pvData;
        } read;
        struct
        {
            /** How much data (in bytes) have been successfully written. */
            uint32_t cbWritten;
        } write;
        struct
        {
            /** New file offset after successful seek. */
            uint64_t uOffActual;
        } seek;
        struct
        {
            /** New file offset after successful tell. */
            uint64_t uOffActual;
        } tell;
        struct
        {
            /** The new file siz.e */
            uint64_t cbSize;
        } SetSize;
    } u;
} CALLBACKDATA_FILE_NOTIFY;
/** Pointer to a CALLBACKDATA_FILE_NOTIFY, struct. */
typedef CALLBACKDATA_FILE_NOTIFY *PCALLBACKDATA_FILE_NOTIFY;

/**
 * Callback data for a single GSTCTLDIRENTRYEX entry.
 */
typedef struct CALLBACKDATA_DIR_ENTRY
{
    /** Pointer to directory entry information. */
    PGSTCTLDIRENTRYEX pDirEntryEx;
    /** Size (in bytes) of directory entry information. */
    uint32_t          cbDirEntryEx;
    /** Resolved user name.
     *  This is the object owner for UNIX-y Oses. */
    char             *pszUser;
    /** Size (in bytes) of \a pszUser. */
    uint32_t          cbUser;
    /** Resolved user group(s). */
    char             *pszGroups;
    /** Size (in bytes) of \a pszGroups. */
    uint32_t          cbGroups;
} CALLBACKDATA_DIR_ENTRY;
/** Pointer to a CALLBACKDATA_DIR_ENTRY struct. */
typedef CALLBACKDATA_DIR_ENTRY *PCALLBACKDATA_DIR_ENTRY;

/**
 * Callback data for guest directory operations.
 */
typedef struct CALLBACKDATA_DIR_NOTIFY
{
    /** Callback data header. */
    CALLBACKDATA_HEADER hdr;
    /** Notification type. */
    uint32_t uType;
    /** IPRT result of overall operation. */
    uint32_t rc;
    union
    {
        struct
        {
            /** Pointer to directory information. */
            PGSTCTLFSOBJINFO pObjInfo;
        } info;
        struct
        {
            /** Guest directory handle. */
            uint32_t uHandle;
        } open;
        /** Note: Close does not have any additional data (yet). */
        struct
        {
            /** Single entry read. */
            CALLBACKDATA_DIR_ENTRY Entry;
        } read;
        struct
        {
            /** Number of entries in \a paEntries. */
            uint32_t                 cEntries;
            /** Array of entries read. */
            CALLBACKDATA_DIR_ENTRY **paEntries;
        } list;
    } u;
} CALLBACKDATA_DIR_NOTIFY;
/** Pointer to a CALLBACKDATA_DIR_NOTIFY struct. */
typedef CALLBACKDATA_DIR_NOTIFY *PCALLBACKDATA_DIR_NOTIFY;

/**
 * Callback data for guest file system operations.
 */
typedef struct CALLBACKDATA_FS_NOTIFY
{
    /** Callback data header. */
    CALLBACKDATA_HEADER hdr;
    /** Notification type (of type GUEST_FS_NOTIFYTYPE_XXX). */
    uint32_t uType;
    /** IPRT result of overall operation. */
    uint32_t rc;
    union
    {
        /** Holds information for GUEST_FS_NOTIFYTYPE_CREATE_TEMP. */
        struct
        {
            /** Path of created temporary file / directory. */
            char    *pszPath;
            /** Size (in bytes) of \a pszPath. */
            uint32_t cbPath;
        } CreateTemp;
        /** Holds information for GUEST_FS_NOTIFYTYPE_QUERY_OBJ_INFO. */
        struct
        {
            GSTCTLFSOBJINFO objInfo;
            /** Resolved user name. */
            char           *pszUser;
            /** Size (in bytes) of \a pszUser. */
            uint32_t        cbUser;
            /** Resolved user group(s). */
            char           *pszGroups;
            /** Size (in bytes) of \a pszGroups. */
            uint32_t        cbGroups;
        } QueryObjInfo;
        /** Holds information for GUEST_FS_NOTIFYTYPE_QUERY_INFO. */
        struct
        {
            /** The actual filesystem information. */
            GSTCTLFSINFO    fsInfo;
        } QueryInfo;
    } u;
} CALLBACKDATA_FS_NOTIFY;
/** Pointer to a CALLBACKDATA_FS_NOTIFY struct. */
typedef CALLBACKDATA_FS_NOTIFY *PCALLBACKDATA_FS_NOTIFY;
#endif /* !MAIN_INCLUDED_GuestCtrlImplPrivate_h */

