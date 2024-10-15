/* $Id: SerialPortImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation
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

#define LOG_GROUP LOG_GROUP_MAIN_SERIALPORT
#include "SerialPortImpl.h"
#include "MachineImpl.h"
#include "VirtualBoxImpl.h"
#include "GuestOSTypeImpl.h"

#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/cpp/utils.h>

#include <VBox/settings.h>

#include "AutoStateDep.h"
#include "AutoCaller.h"
#include "LoggingNew.h"

//////////////////////////////////////////////////////////////////////////////////
//
// SerialPort private data definition
//
//////////////////////////////////////////////////////////////////////////////////

struct SerialPort::Data
{
    Data()
        : fModified(false),
          pMachine(NULL)
    { }

    bool                                fModified;
    Machine * const                     pMachine;
    const ComObjPtr<SerialPort>         pPeer;
    Backupable<settings::SerialPort>    bd;
};

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(SerialPort)

HRESULT SerialPort::FinalConstruct()
{
    return BaseFinalConstruct();
}

void SerialPort::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 *  Initializes the Serial Port object.
 *
 *  @param aParent  Handle of the parent object.
 *  @param aSlot    Slot number the serial port is plugged into.
 */
HRESULT SerialPort::init(Machine *aParent, ULONG aSlot)
{
    LogFlowThisFunc(("aParent=%p, aSlot=%d\n", aParent, aSlot));

    ComAssertRet(aParent, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data();

    unconst(m->pMachine) = aParent;
    /* m->pPeer is left null */

    m->bd.allocate();

    /* initialize data */
    m->bd->ulSlot = aSlot;

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Initializes the Serial Port object given another serial port object
 *  (a kind of copy constructor). This object shares data with
 *  the object passed as an argument.
 *
 *  @note This object must be destroyed before the original object
 *  it shares data with is destroyed.
 *
 *  @note Locks @a aThat object for reading.
 */
HRESULT SerialPort::init(Machine *aParent, SerialPort *aThat)
{
    LogFlowThisFunc(("aParent=%p, aThat=%p\n", aParent, aThat));

    ComAssertRet(aParent && aThat, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data();

    unconst(m->pMachine) = aParent;
    unconst(m->pPeer) = aThat;

    AutoCaller thatCaller(aThat);
    AssertComRCReturnRC(thatCaller.hrc());

    AutoReadLock thatLock(aThat COMMA_LOCKVAL_SRC_POS);
    m->bd.share(aThat->m->bd);

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Initializes the guest object given another guest object
 *  (a kind of copy constructor). This object makes a private copy of data
 *  of the original object passed as an argument.
 *
 *  @note Locks @a aThat object for reading.
 */
HRESULT SerialPort::initCopy(Machine *aParent, SerialPort *aThat)
{
    LogFlowThisFunc(("aParent=%p, aThat=%p\n", aParent, aThat));

    ComAssertRet(aParent && aThat, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data();

    unconst(m->pMachine) = aParent;
    /* pPeer is left null */

    AutoCaller thatCaller(aThat);
    AssertComRCReturnRC(thatCaller.hrc());

    AutoReadLock thatLock(aThat COMMA_LOCKVAL_SRC_POS);
    m->bd.attachCopy(aThat->m->bd);

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Uninitializes the instance and sets the ready flag to FALSE.
 *  Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void SerialPort::uninit()
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    m->bd.free();

    unconst(m->pPeer) = NULL;
    unconst(m->pMachine) = NULL;

    delete m;
    m = NULL;
}

// ISerialPort properties
/////////////////////////////////////////////////////////////////////////////

HRESULT SerialPort::getEnabled(BOOL *aEnabled)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aEnabled = m->bd->fEnabled;

    return S_OK;
}


HRESULT SerialPort::setEnabled(BOOL aEnabled)
{
    LogFlowThisFunc(("aEnabled=%RTbool\n", aEnabled));

    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->fEnabled != RT_BOOL(aEnabled))
    {
        m->bd.backup();
        m->bd->fEnabled = RT_BOOL(aEnabled);

        m->fModified = true;
        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);
        m->pMachine->i_setModified(Machine::IsModified_SerialPorts);
        mlock.release();

        m->pMachine->i_onSerialPortChange(this);
    }

    return S_OK;
}


HRESULT SerialPort::getHostMode(PortMode_T *aHostMode)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aHostMode = m->bd->portMode;

    return S_OK;
}

HRESULT SerialPort::setHostMode(PortMode_T aHostMode)
{
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->portMode != aHostMode)
    {
        switch (aHostMode)
        {
            case PortMode_RawFile:
                if (m->bd->strPath.isEmpty())
                    return setError(E_INVALIDARG,
                                    tr("Cannot set the raw file mode of the serial port %d "
                                       "because the file path is empty or null"),
                                    m->bd->ulSlot);
                break;
            case PortMode_HostPipe:
                if (m->bd->strPath.isEmpty())
                    return setError(E_INVALIDARG,
                                    tr("Cannot set the host pipe mode of the serial port %d "
                                       "because the pipe path is empty or null"),
                                    m->bd->ulSlot);
                break;
            case PortMode_HostDevice:
                if (m->bd->strPath.isEmpty())
                    return setError(E_INVALIDARG,
                                    tr("Cannot set the host device mode of the serial port %d "
                                       "because the device path is empty or null"),
                                    m->bd->ulSlot);
                break;
            case PortMode_TCP:
                if (m->bd->strPath.isEmpty())
                    return setError(E_INVALIDARG,
                                    tr("Cannot set the host device mode of the serial port %d "
                                       "because the server address or TCP port is invalid"),
                                    m->bd->ulSlot);
                break;
            case PortMode_Disconnected:
                break;
#ifdef VBOX_WITH_XPCOM_CPP_ENUM_HACK
            case PortMode_32BitHack: /* (compiler warnings) */
                AssertFailedBreak();
#endif
        }

        m->bd.backup();
        m->bd->portMode = aHostMode;

        m->fModified = true;
        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);
        m->pMachine->i_setModified(Machine::IsModified_SerialPorts);
        mlock.release();

        m->pMachine->i_onSerialPortChange(this);
    }

    return S_OK;
}

HRESULT SerialPort::getSlot(ULONG *aSlot)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aSlot = m->bd->ulSlot;

    return S_OK;
}


HRESULT SerialPort::getIRQ(ULONG *aIRQ)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aIRQ = m->bd->ulIRQ;

    return S_OK;
}


HRESULT SerialPort::setIRQ(ULONG aIRQ)
{
    /* check IRQ limits
     * (when changing this, make sure it corresponds to XML schema */
    if (aIRQ > 255)
        return setError(E_INVALIDARG,
                        tr("Invalid IRQ number of the serial port %d: %lu (must be in range [0, %lu])"),
                        m->bd->ulSlot, aIRQ, 255);

    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->ulIRQ != aIRQ)
    {
        m->bd.backup();
        m->bd->ulIRQ = aIRQ;

        m->fModified = true;
        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);
        m->pMachine->i_setModified(Machine::IsModified_SerialPorts);
        mlock.release();

        m->pMachine->i_onSerialPortChange(this);
    }

    return S_OK;
}


HRESULT SerialPort::getIOAddress(ULONG *aIOBase)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aIOBase = m->bd->ulIOAddress;

    return S_OK;
}

HRESULT SerialPort::setIOAddress(ULONG aIOBase)
{
    /* check IOBase limits
     * (when changing this, make sure it corresponds to XML schema */
    if (aIOBase > 0xFFFF) /** @todo BUGBUG Check with ARM. */
        return setError(E_INVALIDARG,
                        tr("Invalid I/O port base address of the serial port %d: %lu (must be in range [0, 0x%X])"),
                        m->bd->ulSlot, aIOBase, 0, 0xFFFF);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    if (m->bd->ulIOAddress != aIOBase)
    {
        m->bd.backup();
        m->bd->ulIOAddress = aIOBase;

        m->fModified = true;
        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);
        m->pMachine->i_setModified(Machine::IsModified_SerialPorts);
        mlock.release();

        m->pMachine->i_onSerialPortChange(this);
    }

    return hrc;
}

HRESULT SerialPort::getPath(com::Utf8Str &aPath)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aPath = m->bd->strPath;

    return S_OK;
}


HRESULT SerialPort::setPath(const com::Utf8Str &aPath)
{
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (aPath != m->bd->strPath)
    {
        HRESULT hrc = i_checkSetPath(aPath);
        if (FAILED(hrc)) return hrc;

        m->bd.backup();
        m->bd->strPath = aPath;

        m->fModified = true;
        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);
        m->pMachine->i_setModified(Machine::IsModified_SerialPorts);
        mlock.release();

        m->pMachine->i_onSerialPortChange(this);
    }

    return S_OK;
}

HRESULT SerialPort::getServer(BOOL *aServer)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aServer = m->bd->fServer;

    return S_OK;
}

HRESULT SerialPort::setServer(BOOL aServer)
{
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->fServer != RT_BOOL(aServer))
    {
        m->bd.backup();
        m->bd->fServer = RT_BOOL(aServer);

        m->fModified = true;
        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);
        m->pMachine->i_setModified(Machine::IsModified_SerialPorts);
        mlock.release();

        m->pMachine->i_onSerialPortChange(this);
    }

    return S_OK;
}

HRESULT SerialPort::getUartType(UartType_T *aUartType)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aUartType = m->bd->uartType;

    return S_OK;
}

HRESULT SerialPort::setUartType(UartType_T aUartType)
{
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->bd->uartType != aUartType)
    {
        m->bd.backup();
        m->bd->uartType = aUartType;

        m->fModified = true;
        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(m->pMachine COMMA_LOCKVAL_SRC_POS);
        m->pMachine->i_setModified(Machine::IsModified_SerialPorts);
        mlock.release();

        m->pMachine->i_onSerialPortChange(this);
    }

    return S_OK;
}

// public methods only for internal purposes
////////////////////////////////////////////////////////////////////////////////

/**
 *  Loads settings from the given port node.
 *  May be called once right after this object creation.
 *
 *  @param data Configuration settings.
 *
 *  @note Locks this object for writing.
 */
HRESULT SerialPort::i_loadSettings(const settings::SerialPort &data)
{

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    // simply copy
    *m->bd.data() = data;

    return S_OK;
}

/**
 *  Saves the port settings to the given port node.
 *
 *  Note that the given Port node is completely empty on input.
 *
 *  @param data Configuration settings.
 *
 *  @note Locks this object for reading.
 */
HRESULT SerialPort::i_saveSettings(settings::SerialPort &data)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    // simply copy
    data = *m->bd.data();

    return S_OK;
}

/**
 * Returns true if any setter method has modified settings of this instance.
 * @return
 */
bool SerialPort::i_isModified()
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    return m->fModified;
}

/**
 *  @note Locks this object for writing.
 */
void SerialPort::i_rollback()
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd.rollback();
}

/**
 *  @note Locks this object for writing, together with the peer object (also
 *  for writing) if there is one.
 */
void SerialPort::i_commit()
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    /* sanity too */
    AutoCaller peerCaller(m->pPeer);
    AssertComRCReturnVoid(peerCaller.hrc());

    /* lock both for writing since we modify both (pPeer is "master" so locked
     * first) */
    AutoMultiWriteLock2 alock(m->pPeer, this COMMA_LOCKVAL_SRC_POS);

    if (m->bd.isBackedUp())
    {
        m->bd.commit();
        if (m->pPeer)
        {
            /* attach new data to the peer and reshare it */
            m->pPeer->m->bd.attach(m->bd);
        }
    }
}

/**
 *  @note Locks this object for writing, together with the peer object
 *  represented by @a aThat (locked for reading).
 */
void SerialPort::i_copyFrom(SerialPort *aThat)
{
    AssertReturnVoid(aThat != NULL);

    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    /* sanity too */
    AutoCaller thatCaller(aThat);
    AssertComRCReturnVoid(thatCaller.hrc());

    /* peer is not modified, lock it for reading (aThat is "master" so locked
     * first) */
    AutoReadLock rl(aThat COMMA_LOCKVAL_SRC_POS);
    AutoWriteLock wl(this COMMA_LOCKVAL_SRC_POS);

    /* this will back up current data */
    m->bd.assignCopy(aThat->m->bd);
}

/**
 * Applies the defaults for this serial port.
 *
 * @note This method currently assumes that the object is in the state after
 * calling init(), it does not set defaults from an arbitrary state.
 */
void SerialPort::i_applyDefaults(GuestOSType *aOsType)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Set some more defaults. */
    switch (m->bd->ulSlot)
    {
        case 0:
        {
            m->bd->ulIOAddress = 0x3f8;
            m->bd->ulIRQ = 4;
            break;
        }
        case 1:
        {
            m->bd->ulIOAddress = 0x2f8;
            m->bd->ulIRQ = 3;
            break;
        }
        case 2:
        {
            m->bd->ulIOAddress = 0x3e8;
            m->bd->ulIRQ = 4;
            break;
        }
        case 3:
        {
            m->bd->ulIOAddress = 0x2e8;
            m->bd->ulIRQ = 3;
            break;
        }
        default:
            AssertMsgFailed(("Serial port slot %u exceeds limit\n", m->bd->ulSlot));
            break;
    }

    uint32_t numSerialEnabled = 0;
    if (aOsType)
        numSerialEnabled = aOsType->i_numSerialEnabled();

    /* Enable port if requested */
    if (m->bd->ulSlot < numSerialEnabled)
    {
        m->bd->fEnabled = true;
    }
}

bool SerialPort::i_hasDefaults()
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturn(autoCaller.hrc(), true);

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (   !m->bd->fEnabled
        && m->bd->portMode == PortMode_Disconnected
        && !m->bd->fServer)
    {
        /* Could be default, check the IO base and IRQ. */
        switch (m->bd->ulSlot)
        {
            case 0:
                if (m->bd->ulIOAddress == 0x3f8 && m->bd->ulIRQ == 4)
                    return true;
                break;
            case 1:
                if (m->bd->ulIOAddress == 0x2f8 && m->bd->ulIRQ == 3)
                    return true;
                break;
            case 2:
                if (m->bd->ulIOAddress == 0x3e8 && m->bd->ulIRQ == 4)
                    return true;
                break;
            case 3:
                if (m->bd->ulIOAddress == 0x2e8 && m->bd->ulIRQ == 3)
                    return true;
                break;
            default:
                AssertMsgFailed(("Serial port slot %u exceeds limit\n", m->bd->ulSlot));
                break;
        }

        /* Detect old-style defaults (0x3f8, irq 4) in any slot, they are still
         * in place for many VMs created by old VirtualBox versions. */
        if (m->bd->ulIOAddress == 0x3f8 && m->bd->ulIRQ == 4)
            return true;
    }

    return false;
}

/**
 *  Validates COMSETTER(Path) arguments.
 */
HRESULT SerialPort::i_checkSetPath(const Utf8Str &str)
{
    AssertReturn(isWriteLockOnCurrentThread(), E_FAIL);

    if (    (    m->bd->portMode == PortMode_HostDevice
              || m->bd->portMode == PortMode_HostPipe
              || m->bd->portMode == PortMode_TCP
              || m->bd->portMode == PortMode_RawFile
            ) && str.isEmpty()
       )
        return setError(E_INVALIDARG,
                        tr("Path of the serial port %d may not be empty or null in "
                           "host pipe, host device or TCP mode"),
                        m->bd->ulSlot);

    return S_OK;
}

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
