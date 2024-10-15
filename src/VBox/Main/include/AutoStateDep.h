/* $Id: AutoStateDep.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */

#ifndef MAIN_INCLUDED_AutoStateDep_h
#define MAIN_INCLUDED_AutoStateDep_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/** @file
 *
 * AutoStateDep template classes, formerly in MachineImpl.h. Use these if
 * you need to ensure that the machine state does not change over a certain
 * period of time.
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

    /**
     *  Helper class that safely manages the machine state dependency by
     *  calling Machine::addStateDependency() on construction and
     *  Machine::releaseStateDependency() on destruction. Intended for Machine
     *  children. The usage pattern is:
     *
     *  @code
     *      AutoCaller autoCaller(this);
     *      if (FAILED(autoCaller.hrc())) return autoCaller.hrc();
     *
     *      Machine::AutoStateDependency<MutableStateDep> adep(mParent);
     *      if (FAILED(stateDep.hrc())) return stateDep.hrc();
     *      ...
     *      // code that depends on the particular machine state
     *      ...
     *  @endcode
     *
     *  Note that it is more convenient to use the following individual
     *  shortcut classes instead of using this template directly:
     *  AutoAnyStateDependency, AutoMutableStateDependency,
     *  AutoMutableOrSavedStateDependency, AutoMutableOrRunningStateDependency
     *  or AutoMutableOrSavedOrRunningStateDependency. The usage pattern is
     *  exactly the same as above except that there is no need to specify the
     *  template argument because it is already done by the shortcut class.
     *
     *  @param taDepType    Dependency type to manage.
     */
    template <Machine::StateDependency taDepType = Machine::AnyStateDep>
    class AutoStateDependency
    {
    public:

        AutoStateDependency(Machine *aThat)
            : mThat(aThat), mRC(S_OK),
              mMachineState(MachineState_Null),
              mRegistered(FALSE)
        {
            if (RT_VALID_PTR(aThat))
                mRC = aThat->i_addStateDependency(taDepType, &mMachineState,
                                                  &mRegistered);
        }
        ~AutoStateDependency()
        {
            if (RT_VALID_PTR(mThat) && SUCCEEDED(mRC))
                mThat->i_releaseStateDependency();
        }

        /** Decreases the number of dependencies before the instance is
         *  destroyed. Note that will reset #hrc() to E_FAIL. */
        void release()
        {
            AssertReturnVoid(SUCCEEDED(mRC));
            if (RT_VALID_PTR(mThat))
                mThat->i_releaseStateDependency();
            mRC = E_FAIL;
        }

        /** Restores the number of callers after by #release(). #hrc() will be
         *  reset to the result of calling addStateDependency() and must be
         *  rechecked to ensure the operation succeeded. */
        void add()
        {
            AssertReturnVoid(!SUCCEEDED(mRC));
            if (RT_VALID_PTR(mThat))
                mRC = mThat->i_addStateDependency(taDepType, &mMachineState,
                                                  &mRegistered);
            else
                mRC = S_OK;
        }

        /** Returns the result of Machine::addStateDependency(). */
        HRESULT hrc() const { return mRC; }

        /** Shortcut to SUCCEEDED(hrc()). */
        bool isOk() const { return SUCCEEDED(mRC); }

        /** Returns the machine state value as returned by
         *  Machine::addStateDependency(). */
        MachineState_T machineState() const { return mMachineState; }

        /** Returns the machine state value as returned by
         *  Machine::addStateDependency(). */
        BOOL machineRegistered() const { return mRegistered; }

    protected:

        Machine *mThat;
        HRESULT mRC;
        MachineState_T mMachineState;
        BOOL mRegistered;

    private:

        DECLARE_CLS_COPY_CTOR_ASSIGN_NOOP(AutoStateDependency);
        DECLARE_CLS_NEW_DELETE_NOOP(AutoStateDependency);
    };

    /**
     *  Shortcut to AutoStateDependency<AnyStateDep>.
     *  See AutoStateDependency to get the usage pattern.
     *
     *  Accepts any machine state and guarantees the state won't change before
     *  this object is destroyed. If the machine state cannot be protected (as
     *  a result of the state change currently in progress), this instance's
     *  #hrc() method will indicate a failure, and the caller is not allowed to
     *  rely on any particular machine state and should return the failed
     *  result code to the upper level.
     */
    typedef AutoStateDependency<Machine::AnyStateDep> AutoAnyStateDependency;

    /**
     *  Shortcut to AutoStateDependency<MutableStateDep>.
     *  See AutoStateDependency to get the usage pattern.
     *
     *  Succeeds only if the machine state is in one of the mutable states, and
     *  guarantees the given mutable state won't change before this object is
     *  destroyed. If the machine is not mutable, this instance's #hrc() method
     *  will indicate a failure, and the caller is not allowed to rely on any
     *  particular machine state and should return the failed result code to
     *  the upper level.
     *
     *  Intended to be used within all setter methods of IMachine
     *  children objects (DVDDrive, NetworkAdapter, AudioAdapter, etc.) to
     *  provide data protection and consistency. There must be no VM process,
     *  i.e. use for settings changes which are valid when the VM is shut down.
     */
    typedef AutoStateDependency<Machine::MutableStateDep> AutoMutableStateDependency;

    /**
     *  Shortcut to AutoStateDependency<MutableOrSavedStateDep>.
     *  See AutoStateDependency to get the usage pattern.
     *
     *  Succeeds only if the machine state is in one of the mutable states, or
     *  if the machine is in the Saved state, and guarantees the given mutable
     *  state won't change before this object is destroyed. If the machine is
     *  not mutable, this instance's #hrc() method will indicate a failure, and
     *  the caller is not allowed to rely on any particular machine state and
     *  should return the failed result code to the upper level.
     *
     *  Intended to be used within setter methods of IMachine
     *  children objects that may operate on shut down or Saved machines.
     */
    typedef AutoStateDependency<Machine::MutableOrSavedStateDep> AutoMutableOrSavedStateDependency;

    /**
     *  Shortcut to AutoStateDependency<MutableOrRunningStateDep>.
     *  See AutoStateDependency to get the usage pattern.
     *
     *  Succeeds only if the machine state is in one of the mutable states, or
     *  if the machine is in the Running or Paused state, and guarantees the
     *  given mutable state won't change before this object is destroyed. If
     *  the machine is not mutable, this instance's #hrc() method will indicate
     *  a failure, and the caller is not allowed to rely on any particular
     *  machine state and should return the failed result code to the upper
     *  level.
     *
     *  Intended to be used within setter methods of IMachine
     *  children objects that may operate on shut down or running machines.
     */
    typedef AutoStateDependency<Machine::MutableOrRunningStateDep> AutoMutableOrRunningStateDependency;

    /**
     *  Shortcut to AutoStateDependency<MutableOrSavedOrRunningStateDep>.
     *  See AutoStateDependency to get the usage pattern.
     *
     *  Succeeds only if the machine state is in one of the mutable states, or
     *  if the machine is in the Running, Paused or Saved state, and guarantees
     *  the given mutable state won't change before this object is destroyed.
     *  If the machine is not mutable, this instance's #hrc() method will
     *  indicate a failure, and the caller is not allowed to rely on any
     *  particular machine state and should return the failed result code to
     *  the upper level.
     *
     *  Intended to be used within setter methods of IMachine
     *  children objects that may operate on shut down, running or saved
     *  machines.
     */
    typedef AutoStateDependency<Machine::MutableOrSavedOrRunningStateDep> AutoMutableOrSavedOrRunningStateDependency;

#endif /* !MAIN_INCLUDED_AutoStateDep_h */

