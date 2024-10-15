/* $Id: AutoLock.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Automatic locks, implementation.
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
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define GLUE_USE_CRITSECTRW


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/cdefs.h>
#include <iprt/critsect.h>
#include <iprt/thread.h>
#include <iprt/semaphore.h>

#include <iprt/errcore.h>
#include <iprt/assert.h>

#if defined(RT_LOCK_STRICT)
# include <iprt/asm.h> // for ASMReturnAddress
#endif

#include <iprt/string.h>
#include <iprt/path.h>
#include <iprt/stream.h>

#include "VBox/com/AutoLock.h"
#include <VBox/com/string.h>

#include <vector>
#include <list>
#include <map>


namespace util
{

////////////////////////////////////////////////////////////////////////////////
//
// RuntimeLockClass
//
////////////////////////////////////////////////////////////////////////////////

#ifdef VBOX_WITH_MAIN_LOCK_VALIDATION
typedef std::map<VBoxLockingClass, RTLOCKVALCLASS> LockValidationClassesMap;
LockValidationClassesMap g_mapLockValidationClasses;
#endif

/**
 * Called from initterm.cpp on process initialization (on the main thread)
 * to give us a chance to initialize lock validation runtime data.
 */
void InitAutoLockSystem()
{
#ifdef VBOX_WITH_MAIN_LOCK_VALIDATION
    struct
    {
        VBoxLockingClass    cls;
        const char          *pcszDescription;
    } aClasses[] =
    {
        { LOCKCLASS_VIRTUALBOXOBJECT,   "2-VIRTUALBOXOBJECT" },
        { LOCKCLASS_HOSTOBJECT,         "3-HOSTOBJECT" },
        { LOCKCLASS_LISTOFMACHINES,     "4-LISTOFMACHINES" },
        { LOCKCLASS_MACHINEOBJECT,      "5-MACHINEOBJECT" },
        { LOCKCLASS_SNAPSHOTOBJECT,     "6-SNAPSHOTOBJECT" },
        { LOCKCLASS_MEDIUMQUERY,        "7-MEDIUMQUERY" },
        { LOCKCLASS_LISTOFMEDIA,        "8-LISTOFMEDIA" },
        { LOCKCLASS_LISTOFOTHEROBJECTS, "9-LISTOFOTHEROBJECTS" },
        { LOCKCLASS_OTHEROBJECT,        "10-OTHEROBJECT" },
        { LOCKCLASS_PROGRESSLIST,       "11-PROGRESSLIST" },
        { LOCKCLASS_OBJECTSTATE,        "12-OBJECTSTATE" },
        { LOCKCLASS_TRANSLATOR,         "13-TRANSLATOR" }
    };

    RTLOCKVALCLASS hClass;
    int vrc;
    for (unsigned i = 0; i < RT_ELEMENTS(aClasses); ++i)
    {
        vrc = RTLockValidatorClassCreate(&hClass,
                                         true, /*fAutodidact*/
                                         RT_SRC_POS,
                                         aClasses[i].pcszDescription);
        AssertRC(vrc);

        // teach the new class that the classes created previously can be held
        // while the new class is being acquired
        for (LockValidationClassesMap::iterator it = g_mapLockValidationClasses.begin();
             it != g_mapLockValidationClasses.end();
             ++it)
        {
            RTLOCKVALCLASS &canBeHeld = it->second;
            vrc = RTLockValidatorClassAddPriorClass(hClass,
                                                    canBeHeld);
            AssertRC(vrc);
        }

        // and store the new class
        g_mapLockValidationClasses[aClasses[i].cls] = hClass;
    }

/*    WriteLockHandle critsect1(LOCKCLASS_VIRTUALBOXOBJECT);
    WriteLockHandle critsect2(LOCKCLASS_VIRTUALBOXLIST);

    AutoWriteLock lock1(critsect1 COMMA_LOCKVAL_SRC_POS);
    AutoWriteLock lock2(critsect2 COMMA_LOCKVAL_SRC_POS);*/
#endif
}

bool AutoLockHoldsLocksInClass(VBoxLockingClass lockClass)
{
#ifdef VBOX_WITH_MAIN_LOCK_VALIDATION
    return RTLockValidatorHoldsLocksInClass(NIL_RTTHREAD, g_mapLockValidationClasses[lockClass]);
#else
    RT_NOREF(lockClass);
    return false;
#endif
}

////////////////////////////////////////////////////////////////////////////////
//
// RWLockHandle
//
////////////////////////////////////////////////////////////////////////////////

struct RWLockHandle::Data
{
    Data()
    { }

#ifdef GLUE_USE_CRITSECTRW
    mutable RTCRITSECTRW    CritSect;
#else
    RTSEMRW                 sem;
#endif
    VBoxLockingClass        lockClass;

#ifdef VBOX_WITH_MAIN_LOCK_VALIDATION
    com::Utf8Str            strDescription;
#endif
};

RWLockHandle::RWLockHandle(VBoxLockingClass lockClass, const char *pszName /*= NULL*/)
{
    Assert(!pszName || (!strchr(pszName, '%') && *pszName));
    m = new Data();

    m->lockClass = lockClass;
#ifdef VBOX_WITH_MAIN_LOCK_VALIDATION
    if (pszName)
        m->strDescription.printf("r/w %RCv %s", this, pszName);
    else
        m->strDescription.printf("r/w %RCv", this);
#endif

#ifdef GLUE_USE_CRITSECTRW
# ifdef VBOX_WITH_MAIN_LOCK_VALIDATION
    int vrc = RTCritSectRwInitEx(&m->CritSect, 0 /*fFlags*/, g_mapLockValidationClasses[lockClass], RTLOCKVAL_SUB_CLASS_ANY, pszName);
# else
    int vrc = RTCritSectRwInitEx(&m->CritSect, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_ANY, pszName);
# endif
#else
# ifdef VBOX_WITH_MAIN_LOCK_VALIDATION
    int vrc = RTSemRWCreateEx(&m->sem, 0 /*fFlags*/, g_mapLockValidationClasses[lockClass], RTLOCKVAL_SUB_CLASS_ANY, pszName);
# else
    int vrc = RTSemRWCreateEx(&m->sem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_ANY, pszName);
# endif
#endif
    AssertRC(vrc);
}

/*virtual*/ RWLockHandle::~RWLockHandle()
{
#ifdef GLUE_USE_CRITSECTRW
    RTCritSectRwDelete(&m->CritSect);
#else
    RTSemRWDestroy(m->sem);
#endif
    delete m;
}

/*virtual*/ bool RWLockHandle::isWriteLockOnCurrentThread() const
{
#ifdef GLUE_USE_CRITSECTRW
    return RTCritSectRwIsWriteOwner(&m->CritSect);
#else
    return RTSemRWIsWriteOwner(m->sem);
#endif
}

/*virtual*/ void RWLockHandle::lockWrite(LOCKVAL_SRC_POS_DECL)
{
#ifdef GLUE_USE_CRITSECTRW
# ifdef VBOX_WITH_MAIN_LOCK_VALIDATION
    int vrc = RTCritSectRwEnterExclDebug(&m->CritSect, (uintptr_t)ASMReturnAddress(), RT_SRC_POS_ARGS);
# else
    int vrc = RTCritSectRwEnterExcl(&m->CritSect);
# endif
#else
# ifdef VBOX_WITH_MAIN_LOCK_VALIDATION
    int vrc = RTSemRWRequestWriteDebug(m->sem, RT_INDEFINITE_WAIT, (uintptr_t)ASMReturnAddress(), RT_SRC_POS_ARGS);
# else
    int vrc = RTSemRWRequestWrite(m->sem, RT_INDEFINITE_WAIT);
# endif
#endif
    AssertRC(vrc);
}

/*virtual*/ void RWLockHandle::unlockWrite()
{
#ifdef GLUE_USE_CRITSECTRW
    int vrc = RTCritSectRwLeaveExcl(&m->CritSect);
#else
    int vrc = RTSemRWReleaseWrite(m->sem);
#endif
    AssertRC(vrc);

}

/*virtual*/ bool RWLockHandle::isReadLockedOnCurrentThread(bool fWannaHear) const
{
#ifdef GLUE_USE_CRITSECTRW
    return RTCritSectRwIsReadOwner(&m->CritSect, fWannaHear);
#else
    return RTSemRWIsReadOwner(m->sem, fWannaHear);
#endif
}

/*virtual*/ void RWLockHandle::lockRead(LOCKVAL_SRC_POS_DECL)
{
#ifdef GLUE_USE_CRITSECTRW
# ifdef VBOX_WITH_MAIN_LOCK_VALIDATION
    int vrc = RTCritSectRwEnterSharedDebug(&m->CritSect, (uintptr_t)ASMReturnAddress(), RT_SRC_POS_ARGS);
# else
    int vrc = RTCritSectRwEnterShared(&m->CritSect);
# endif
#else
# ifdef VBOX_WITH_MAIN_LOCK_VALIDATION
    int vrc = RTSemRWRequestReadDebug(m->sem, RT_INDEFINITE_WAIT, (uintptr_t)ASMReturnAddress(), RT_SRC_POS_ARGS);
# else
    int vrc = RTSemRWRequestRead(m->sem, RT_INDEFINITE_WAIT);
# endif
#endif
    AssertRC(vrc);
}

/*virtual*/ void RWLockHandle::unlockRead()
{
#ifdef GLUE_USE_CRITSECTRW
    int vrc = RTCritSectRwLeaveShared(&m->CritSect);
#else
    int vrc = RTSemRWReleaseRead(m->sem);
#endif
    AssertRC(vrc);
}

/*virtual*/ uint32_t RWLockHandle::writeLockLevel() const
{
    /* Note! This does not include read recursions done by the writer! */
#ifdef GLUE_USE_CRITSECTRW
    return RTCritSectRwGetWriteRecursion(&m->CritSect);
#else
    return RTSemRWGetWriteRecursion(m->sem);
#endif
}

#ifdef VBOX_WITH_MAIN_LOCK_VALIDATION
/*virtual*/ const char* RWLockHandle::describe() const
{
    return m->strDescription.c_str();
}
#endif

////////////////////////////////////////////////////////////////////////////////
//
// WriteLockHandle
//
////////////////////////////////////////////////////////////////////////////////

struct WriteLockHandle::Data
{
    Data()
    { }

    mutable RTCRITSECT          sem;
    VBoxLockingClass   lockClass;

#ifdef VBOX_WITH_MAIN_LOCK_VALIDATION
    com::Utf8Str                strDescription;
#endif
};

WriteLockHandle::WriteLockHandle(VBoxLockingClass lockClass)
{
    m = new Data;

    m->lockClass = lockClass;

#ifdef VBOX_WITH_MAIN_LOCK_VALIDATION
    m->strDescription = com::Utf8StrFmt("crit %RCv", this);
    int vrc = RTCritSectInitEx(&m->sem, 0/*fFlags*/, g_mapLockValidationClasses[lockClass], RTLOCKVAL_SUB_CLASS_ANY, NULL);
#else
    int vrc = RTCritSectInitEx(&m->sem, 0/*fFlags*/, NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_ANY, NULL);
#endif
    AssertRC(vrc);
}

WriteLockHandle::~WriteLockHandle()
{
    RTCritSectDelete(&m->sem);
    delete m;
}

/*virtual*/ bool WriteLockHandle::isWriteLockOnCurrentThread() const
{
    return RTCritSectIsOwner(&m->sem);
}

/*virtual*/ void WriteLockHandle::lockWrite(LOCKVAL_SRC_POS_DECL)
{
#ifdef VBOX_WITH_MAIN_LOCK_VALIDATION
    RTCritSectEnterDebug(&m->sem, (uintptr_t)ASMReturnAddress(), RT_SRC_POS_ARGS);
#else
    RTCritSectEnter(&m->sem);
#endif
}

/*virtual*/ bool WriteLockHandle::isReadLockedOnCurrentThread(bool fWannaHear) const
{
    RT_NOREF(fWannaHear);
    return RTCritSectIsOwner(&m->sem);
}

/*virtual*/ void WriteLockHandle::unlockWrite()
{
    RTCritSectLeave(&m->sem);
}

/*virtual*/ void WriteLockHandle::lockRead(LOCKVAL_SRC_POS_DECL)
{
    lockWrite(LOCKVAL_SRC_POS_ARGS);
}

/*virtual*/ void WriteLockHandle::unlockRead()
{
    unlockWrite();
}

/*virtual*/ uint32_t WriteLockHandle::writeLockLevel() const
{
    return RTCritSectGetRecursion(&m->sem);
}

#ifdef VBOX_WITH_MAIN_LOCK_VALIDATION
/*virtual*/ const char* WriteLockHandle::describe() const
{
    return m->strDescription.c_str();
}
#endif

////////////////////////////////////////////////////////////////////////////////
//
// AutoLockBase
//
////////////////////////////////////////////////////////////////////////////////

typedef std::vector<LockHandle*> HandlesVector;

struct AutoLockBase::Data
{
    Data(size_t cHandles
#ifdef VBOX_WITH_MAIN_LOCK_VALIDATION
         , const char *pcszFile_,
         unsigned uLine_,
         const char *pcszFunction_
#endif
        )
        : fIsLocked(false),
          aHandles(cHandles)        // size of array
#ifdef VBOX_WITH_MAIN_LOCK_VALIDATION
          , pcszFile(pcszFile_),
          uLine(uLine_),
          pcszFunction(pcszFunction_)
#endif
    {
        for (uint32_t i = 0; i < cHandles; ++i)
            aHandles[i] = NULL;
    }

    bool            fIsLocked;          // if true, then all items in aHandles are locked by this AutoLock and
                                        // need to be unlocked in the destructor
    HandlesVector   aHandles;           // array (vector) of LockHandle instances; in the case of AutoWriteLock
                                        // and AutoReadLock, there will only be one item on the list; with the
                                        // AutoMulti* derivatives, there will be multiple

#ifdef VBOX_WITH_MAIN_LOCK_VALIDATION
    // information about where the lock occurred (passed down from the AutoLock classes)
    const char      *pcszFile;
    unsigned        uLine;
    const char      *pcszFunction;
#endif
};

AutoLockBase::AutoLockBase(uint32_t cHandles
                           COMMA_LOCKVAL_SRC_POS_DECL)
{
    m = new Data(cHandles COMMA_LOCKVAL_SRC_POS_ARGS);
}

AutoLockBase::AutoLockBase(uint32_t cHandles,
                           LockHandle *pHandle
                           COMMA_LOCKVAL_SRC_POS_DECL)
{
    Assert(cHandles == 1); NOREF(cHandles);
    m = new Data(1 COMMA_LOCKVAL_SRC_POS_ARGS);
    m->aHandles[0] = pHandle;
}

AutoLockBase::~AutoLockBase()
{
    delete m;
}

/**
 * Requests ownership of all contained lock handles by calling
 * the pure virtual callLockImpl() function on each of them,
 * which must be implemented by the descendant class; in the
 * implementation, AutoWriteLock will request a write lock
 * whereas AutoReadLock will request a read lock.
 *
 * Does *not* modify the lock counts in the member variables.
 */
void AutoLockBase::callLockOnAllHandles()
{
    for (HandlesVector::iterator it = m->aHandles.begin();
         it != m->aHandles.end();
         ++it)
    {
        LockHandle *pHandle = *it;
        if (pHandle)
            // call virtual function implemented in AutoWriteLock or AutoReadLock
            this->callLockImpl(*pHandle);
    }
}

/**
 * Releases ownership of all contained lock handles by calling
 * the pure virtual callUnlockImpl() function on each of them,
 * which must be implemented by the descendant class; in the
 * implementation, AutoWriteLock will release a write lock
 * whereas AutoReadLock will release a read lock.
 *
 * Does *not* modify the lock counts in the member variables.
 */
void AutoLockBase::callUnlockOnAllHandles()
{
    // unlock in reverse order!
    for (HandlesVector::reverse_iterator it = m->aHandles.rbegin();
         it != m->aHandles.rend();
         ++it)
    {
        LockHandle *pHandle = *it;
        if (pHandle)
            // call virtual function implemented in AutoWriteLock or AutoReadLock
            this->callUnlockImpl(*pHandle);
    }
}

/**
 * Destructor implementation that can also be called explicitly, if required.
 * Restores the exact state before the AutoLock was created; that is, unlocks
 * all contained semaphores.
 */
void AutoLockBase::cleanup()
{
    if (m->fIsLocked)
        callUnlockOnAllHandles();
}

/**
 * Requests ownership of all contained semaphores. Public method that can
 * only be called once and that also gets called by the AutoLock constructors.
 */
void AutoLockBase::acquire()
{
    AssertMsgReturnVoid(!m->fIsLocked, ("m->fIsLocked is true, attempting to lock twice!"));
    callLockOnAllHandles();
    m->fIsLocked = true;
}

/**
 * Releases ownership of all contained semaphores. Public method.
 */
void AutoLockBase::release()
{
    AssertMsgReturnVoid(m->fIsLocked, ("m->fIsLocked is false, cannot release!"));
    callUnlockOnAllHandles();
    m->fIsLocked = false;
}

////////////////////////////////////////////////////////////////////////////////
//
// AutoReadLock
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Release all read locks acquired by this instance through the #lock()
 * call and destroys the instance.
 *
 * Note that if there there are nested #lock() calls without the
 * corresponding number of #unlock() calls when the destructor is called, it
 * will assert. This is because having an unbalanced number of nested locks
 * is a program logic error which must be fixed.
 */
/*virtual*/ AutoReadLock::~AutoReadLock()
{
    LockHandle *pHandle = m->aHandles[0];

    if (pHandle)
    {
        if (m->fIsLocked)
            callUnlockImpl(*pHandle);
    }
}

/**
 * Implementation of the pure virtual declared in AutoLockBase.
 * This gets called by AutoLockBase.acquire() to actually request
 * the semaphore; in the AutoReadLock implementation, we request
 * the semaphore in read mode.
 */
/*virtual*/ void AutoReadLock::callLockImpl(LockHandle &l)
{
#ifdef VBOX_WITH_MAIN_LOCK_VALIDATION
    l.lockRead(m->pcszFile, m->uLine, m->pcszFunction);
#else
    l.lockRead();
#endif
}

/**
 * Implementation of the pure virtual declared in AutoLockBase.
 * This gets called by AutoLockBase.release() to actually release
 * the semaphore; in the AutoReadLock implementation, we release
 * the semaphore in read mode.
 */
/*virtual*/ void AutoReadLock::callUnlockImpl(LockHandle &l)
{
    l.unlockRead();
}

////////////////////////////////////////////////////////////////////////////////
//
// AutoWriteLockBase
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Implementation of the pure virtual declared in AutoLockBase.
 * This gets called by AutoLockBase.acquire() to actually request
 * the semaphore; in the AutoWriteLock implementation, we request
 * the semaphore in write mode.
 */
/*virtual*/ void AutoWriteLockBase::callLockImpl(LockHandle &l)
{
#ifdef VBOX_WITH_MAIN_LOCK_VALIDATION
    l.lockWrite(m->pcszFile, m->uLine, m->pcszFunction);
#else
    l.lockWrite();
#endif
}

/**
 * Implementation of the pure virtual declared in AutoLockBase.
 * This gets called by AutoLockBase.release() to actually release
 * the semaphore; in the AutoWriteLock implementation, we release
 * the semaphore in write mode.
 */
/*virtual*/ void AutoWriteLockBase::callUnlockImpl(LockHandle &l)
{
    l.unlockWrite();
}

////////////////////////////////////////////////////////////////////////////////
//
// AutoWriteLock
//
////////////////////////////////////////////////////////////////////////////////

AutoWriteLock::AutoWriteLock(uint32_t cHandles,
                             LockHandle** pHandles
                             COMMA_LOCKVAL_SRC_POS_DECL)
  : AutoWriteLockBase(cHandles
                      COMMA_LOCKVAL_SRC_POS_ARGS)
{
    Assert(cHandles);
    Assert(pHandles);

    for (uint32_t i = 0; i < cHandles; ++i)
        m->aHandles[i] = pHandles[i];

    acquire();
}



/**
 * Attaches another handle to this auto lock instance.
 *
 * The previous object's lock is completely released before the new one is
 * acquired. The lock level of the new handle will be the same. This
 * also means that if the lock was not acquired at all before #attach(), it
 * will not be acquired on the new handle too.
 *
 * @param aHandle   New handle to attach.
 */
void AutoWriteLock::attach(LockHandle *aHandle)
{
    LockHandle *pHandle = m->aHandles[0];

    /* detect simple self-reattachment */
    if (pHandle != aHandle)
    {
        bool fWasLocked = m->fIsLocked;

        cleanup();

        m->aHandles[0] = aHandle;
        m->fIsLocked = fWasLocked;

        if (aHandle)
            if (fWasLocked)
                callLockImpl(*aHandle);
    }
}

/**
 * Returns @c true if the current thread holds a write lock on the managed
 * read/write semaphore. Returns @c false if the managed semaphore is @c
 * NULL.
 *
 * @note Intended for debugging only.
 */
bool AutoWriteLock::isWriteLockOnCurrentThread() const
{
    return m->aHandles[0] ? m->aHandles[0]->isWriteLockOnCurrentThread() : false;
}

 /**
 * Returns the current write lock level of the managed semaphore. The lock
 * level determines the number of nested #lock() calls on the given
 * semaphore handle. Returns @c 0 if the managed semaphore is @c
 * NULL.
 *
 * Note that this call is valid only when the current thread owns a write
 * lock on the given semaphore handle and will assert otherwise.
 *
 * @note Intended for debugging only.
 */
uint32_t AutoWriteLock::writeLockLevel() const
{
    return m->aHandles[0] ? m->aHandles[0]->writeLockLevel() : 0;
}

/**
 * Returns @c true if the current thread holds a write lock on the managed
 * read/write semaphore. Returns @c false if the managed semaphore is @c
 * NULL.
 *
 * @note Intended for debugging only (esp. considering fWannaHear).
 */
bool AutoWriteLock::isReadLockedOnCurrentThread(bool fWannaHear) const
{
    return m->aHandles[0] ? m->aHandles[0]->isReadLockedOnCurrentThread(fWannaHear) : false;
}

////////////////////////////////////////////////////////////////////////////////
//
// AutoMultiWriteLock*
//
////////////////////////////////////////////////////////////////////////////////

AutoMultiWriteLock2::AutoMultiWriteLock2(Lockable *pl1,
                                         Lockable *pl2
                                         COMMA_LOCKVAL_SRC_POS_DECL)
    : AutoWriteLockBase(2
                        COMMA_LOCKVAL_SRC_POS_ARGS)
{
    if (pl1)
        m->aHandles[0] = pl1->lockHandle();
    if (pl2)
        m->aHandles[1] = pl2->lockHandle();
    acquire();
}

AutoMultiWriteLock2::AutoMultiWriteLock2(LockHandle *pl1,
                                         LockHandle *pl2
                                         COMMA_LOCKVAL_SRC_POS_DECL)
    : AutoWriteLockBase(2
                        COMMA_LOCKVAL_SRC_POS_ARGS)
{
    m->aHandles[0] = pl1;
    m->aHandles[1] = pl2;
    acquire();
}

AutoMultiWriteLock3::AutoMultiWriteLock3(Lockable *pl1,
                                         Lockable *pl2,
                                         Lockable *pl3
                                         COMMA_LOCKVAL_SRC_POS_DECL)
    : AutoWriteLockBase(3
                        COMMA_LOCKVAL_SRC_POS_ARGS)
{
    if (pl1)
        m->aHandles[0] = pl1->lockHandle();
    if (pl2)
        m->aHandles[1] = pl2->lockHandle();
    if (pl3)
        m->aHandles[2] = pl3->lockHandle();
    acquire();
}

AutoMultiWriteLock3::AutoMultiWriteLock3(LockHandle *pl1,
                                         LockHandle *pl2,
                                         LockHandle *pl3
                                         COMMA_LOCKVAL_SRC_POS_DECL)
    : AutoWriteLockBase(3
                        COMMA_LOCKVAL_SRC_POS_ARGS)
{
    m->aHandles[0] = pl1;
    m->aHandles[1] = pl2;
    m->aHandles[2] = pl3;
    acquire();
}

AutoMultiWriteLock4::AutoMultiWriteLock4(Lockable *pl1,
                                         Lockable *pl2,
                                         Lockable *pl3,
                                         Lockable *pl4
                                         COMMA_LOCKVAL_SRC_POS_DECL)
    : AutoWriteLockBase(4
                        COMMA_LOCKVAL_SRC_POS_ARGS)
{
    if (pl1)
        m->aHandles[0] = pl1->lockHandle();
    if (pl2)
        m->aHandles[1] = pl2->lockHandle();
    if (pl3)
        m->aHandles[2] = pl3->lockHandle();
    if (pl4)
        m->aHandles[3] = pl4->lockHandle();
    acquire();
}

AutoMultiWriteLock4::AutoMultiWriteLock4(LockHandle *pl1,
                                         LockHandle *pl2,
                                         LockHandle *pl3,
                                         LockHandle *pl4
                                         COMMA_LOCKVAL_SRC_POS_DECL)
    : AutoWriteLockBase(4
                        COMMA_LOCKVAL_SRC_POS_ARGS)
{
    m->aHandles[0] = pl1;
    m->aHandles[1] = pl2;
    m->aHandles[2] = pl3;
    m->aHandles[3] = pl4;
    acquire();
}

} /* namespace util */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
