/* $Id: HostHardwareLinux.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - Classes for handling hardware detection under Linux.
 *
 * Please feel free to expand these to work for other systems (Solaris!) or to
 * add new ones for other systems.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_HostHardwareLinux_h
#define MAIN_INCLUDED_HostHardwareLinux_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/err.h>
#include <iprt/cpp/ministring.h>
#include <vector>
#include <vector.h>

/**
 * Class for probing and returning information about host DVD and floppy
 * drives.  To use this class, create an instance, call one of the update
 * methods to do the actual probing and use the iterator methods to get the
 * result of the probe.
 */
class VBoxMainDriveInfo
{
public:
    /** Structure describing a host drive */
    struct DriveInfo
    {
        /** The device node of the drive. */
        RTCString mDevice;
        /** A unique identifier for the device, if available.  This should be
         * kept consistent across different probing methods of a given
         * platform if at all possible. */
        RTCString mUdi;
        /** A textual description of the drive. */
        RTCString mDescription;

        /** Constructors */
        DriveInfo(const RTCString &aDevice,
                  const RTCString &aUdi = "",
                  const RTCString &aDescription = "")
            : mDevice(aDevice),
              mUdi(aUdi),
              mDescription(aDescription)
        { }
    };

    /** List (resp vector) holding drive information */
    typedef std::vector<DriveInfo> DriveInfoList;

    /**
     * Search for host floppy drives and rebuild the list, which remains empty
     * until the first time this method is called.
     * @returns iprt status code
     */
    int updateFloppies() RT_NOEXCEPT;

    /**
     * Search for host DVD drives and rebuild the list, which remains empty
     * until the first time this method is called.
     * @returns iprt status code
     */
    int updateDVDs() RT_NOEXCEPT;

    /**
     * Search for fixed disks (HDDs) and rebuild the list, which remains empty until
     * the first time this method is called.
     * @returns iprt status code
     */
    int updateFixedDrives() RT_NOEXCEPT;

    /** Get the first element in the list of floppy drives. */
    DriveInfoList::const_iterator FloppyBegin()
    {
        return mFloppyList.begin();
    }

    /** Get the last element in the list of floppy drives. */
    DriveInfoList::const_iterator FloppyEnd()
    {
        return mFloppyList.end();
    }

    /** Get the first element in the list of DVD drives. */
    DriveInfoList::const_iterator DVDBegin()
    {
        return mDVDList.begin();
    }

    /** Get the last element in the list of DVD drives. */
    DriveInfoList::const_iterator DVDEnd()
    {
        return mDVDList.end();
    }

    /** Get the first element in the list of fixed drives. */
    DriveInfoList::const_iterator FixedDriveBegin()
    {
        return mFixedDriveList.begin();
    }

    /** Get the last element in the list of fixed drives. */
    DriveInfoList::const_iterator FixedDriveEnd()
    {
        return mFixedDriveList.end();
    }
private:
    /** The list of currently available floppy drives */
    DriveInfoList mFloppyList;
    /** The list of currently available DVD drives */
    DriveInfoList mDVDList;
    /** The list of currently available fixed drives */
    DriveInfoList mFixedDriveList;
};

/** Convenience typedef. */
typedef VBoxMainDriveInfo::DriveInfoList DriveInfoList;
/** Convenience typedef. */
typedef VBoxMainDriveInfo::DriveInfo DriveInfo;

/** Implementation of the hotplug waiter class below */
class VBoxMainHotplugWaiterImpl
{
public:
    VBoxMainHotplugWaiterImpl(void) {}
    virtual ~VBoxMainHotplugWaiterImpl(void) {}
    /** @copydoc VBoxMainHotplugWaiter::Wait */
    virtual int Wait(RTMSINTERVAL cMillies) = 0;
    /** @copydoc VBoxMainHotplugWaiter::Interrupt */
    virtual void Interrupt(void) = 0;
    /** @copydoc VBoxMainHotplugWaiter::getStatus */
    virtual int getStatus(void) = 0;
};

/**
 * Class for waiting for a hotplug event.  To use this class, create an
 * instance and call the @a Wait() method, which blocks until an event or a
 * user-triggered interruption occurs.  Call @a Interrupt() to interrupt the
 * wait before an event occurs.
 */
class VBoxMainHotplugWaiter
{
    /** Class implementation. */
    VBoxMainHotplugWaiterImpl *mImpl;
public:
    /** Constructor.  Responsible for selecting the implementation. */
    VBoxMainHotplugWaiter(const char *pcszDevicesRoot);
    /** Destructor. */
    ~VBoxMainHotplugWaiter (void)
    {
        delete mImpl;
    }
    /**
     * Wait for a hotplug event.
     *
     * @returns  VINF_SUCCESS if an event occurred or if Interrupt() was called.
     * @returns  VERR_TRY_AGAIN if the wait failed but this might (!) be a
     *           temporary failure.
     * @returns  VERR_NOT_SUPPORTED if the wait failed and will definitely not
     *           succeed if retried.
     * @returns  Possibly other iprt status codes otherwise.
     * @param    cMillies   How long to wait for at most.
     */
    int Wait (RTMSINTERVAL cMillies)
    {
        return mImpl->Wait(cMillies);
    }
    /**
     * Interrupts an active wait.  In the current implementation, the wait
     * may not return until up to two seconds after calling this method.
     */
    void Interrupt (void)
    {
        mImpl->Interrupt();
    }

    int getStatus(void)
    {
        return mImpl ? mImpl->getStatus() : VERR_NO_MEMORY;
    }
};

#endif /* !MAIN_INCLUDED_HostHardwareLinux_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
