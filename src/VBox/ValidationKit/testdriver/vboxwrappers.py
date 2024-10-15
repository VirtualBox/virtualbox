# -*- coding: utf-8 -*-
# $Id: vboxwrappers.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
# pylint: disable=too-many-lines

"""
VirtualBox Wrapper Classes
"""

__copyright__ = \
"""
Copyright (C) 2010-2024 Oracle and/or its affiliates.

This file is part of VirtualBox base platform packages, as
available from https://www.virtualbox.org.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation, in version 3 of the
License.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <https://www.gnu.org/licenses>.

The contents of this file may alternatively be used under the terms
of the Common Development and Distribution License Version 1.0
(CDDL), a copy of it is provided in the "COPYING.CDDL" file included
in the VirtualBox distribution, in which case the provisions of the
CDDL are applicable instead of those of the GPL.

You may elect to license modified versions of this file under the
terms and conditions of either the GPL or the CDDL or both.

SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
"""
__version__ = "$Revision: 106320 $"


# Standard Python imports.
import os;
import socket;
import sys;
import uuid;

# Validation Kit imports.
from common     import utils;
from common     import netutils;
from testdriver import base;
from testdriver import reporter;
from testdriver import txsclient;
from testdriver import vboxcon;
from testdriver import vbox;
from testdriver.base    import TdTaskBase;


def _ControllerNameToBusAndType(sController):
    """ Translate a controller name to a storage bus. """
    if sController == "IDE Controller":
        eBus  = vboxcon.StorageBus_IDE;
        eType = vboxcon.StorageControllerType_PIIX4;
    elif sController == "SATA Controller":
        eBus  = vboxcon.StorageBus_SATA;
        eType = vboxcon.StorageControllerType_IntelAhci;
    elif sController == "Floppy Controller":
        eType = vboxcon.StorageControllerType_I82078;
        eBus  = vboxcon.StorageBus_Floppy;
    elif sController == "SAS Controller":
        eBus  = vboxcon.StorageBus_SAS;
        eType = vboxcon.StorageControllerType_LsiLogicSas;
    elif sController == "SCSI Controller":
        eBus  = vboxcon.StorageBus_SCSI;
        eType = vboxcon.StorageControllerType_LsiLogic;
    elif sController == "BusLogic SCSI Controller":
        eBus  = vboxcon.StorageBus_SCSI;
        eType = vboxcon.StorageControllerType_BusLogic;
    elif sController == "NVMe Controller":
        eBus  = vboxcon.StorageBus_PCIe;
        eType = vboxcon.StorageControllerType_NVMe;
    elif sController == "VirtIO SCSI Controller":
        eBus  = vboxcon.StorageBus_VirtioSCSI;
        eType = vboxcon.StorageControllerType_VirtioSCSI;
    else:
        eBus  = vboxcon.StorageBus_Null;
        eType = vboxcon.StorageControllerType_Null;
    return (eBus, eType);


def _nameMachineState(eState):
    """ Gets the name (string) of a machine state."""
    if eState == vboxcon.MachineState_PoweredOff: return 'PoweredOff';
    if eState == vboxcon.MachineState_Saved: return 'Saved';
    if eState == vboxcon.MachineState_Teleported: return 'Teleported';
    if eState == vboxcon.MachineState_Aborted: return 'Aborted';
    if eState == vboxcon.MachineState_Running: return 'Running';
    if eState == vboxcon.MachineState_Paused: return 'Paused';
    if eState == vboxcon.MachineState_Stuck: return 'GuruMeditation';
    if eState == vboxcon.MachineState_Teleporting: return 'Teleporting';
    if eState == vboxcon.MachineState_LiveSnapshotting: return 'LiveSnapshotting';
    if eState == vboxcon.MachineState_Starting: return 'Starting';
    if eState == vboxcon.MachineState_Stopping: return 'Stopping';
    if eState == vboxcon.MachineState_Saving: return 'Saving';
    if eState == vboxcon.MachineState_Restoring: return 'Restoring';
    if eState == vboxcon.MachineState_TeleportingPausedVM: return 'TeleportingPausedVM';
    if eState == vboxcon.MachineState_TeleportingIn: return 'TeleportingIn';
    if eState == vboxcon.MachineState_DeletingSnapshotOnline: return 'DeletingSnapshotOnline';
    if eState == vboxcon.MachineState_DeletingSnapshotPaused: return 'DeletingSnapshotPaused';
    if eState == vboxcon.MachineState_RestoringSnapshot: return 'RestoringSnapshot';
    if eState == vboxcon.MachineState_DeletingSnapshot: return 'DeletingSnapshot';
    if eState == vboxcon.MachineState_SettingUp: return 'SettingUp';
    if hasattr(vboxcon, 'MachineState_FaultTolerantSyncing'):
        if eState == vboxcon.MachineState_FaultTolerantSyncing: return 'FaultTolerantSyncing';
    if hasattr(vboxcon, 'MachineState_AbortedSaved'): # since r147033 / 7.0
        if eState == vboxcon.MachineState_AbortedSaved: return 'Aborted-Saved';
    return 'Unknown-%s' % (eState,);


class VirtualBoxWrapper(object): # pylint: disable=too-few-public-methods
    """
    Wrapper around the IVirtualBox object that adds some (hopefully) useful
    utility methods

    The real object can be accessed thru the o member.  That said, members can
    be accessed directly as well.
    """

    def __init__(self, oVBox, oVBoxMgr, fpApiVer, oTstDrv):
        self.o          = oVBox;
        self.oVBoxMgr   = oVBoxMgr;
        self.fpApiVer   = fpApiVer;
        self.oTstDrv    = oTstDrv;

    def __getattr__(self, sName):
        # Try ourselves first.
        try:
            oAttr = self.__dict__[sName];
        except:
            #try:
            #    oAttr = dir(self)[sName];
            #except AttributeError:
            oAttr = getattr(self.o, sName);
        return oAttr;

    #
    # Utilities.
    #

    def registerDerivedEventHandler(self, oSubClass, dArgs = None):
        """
        Create an instance of the given VirtualBoxEventHandlerBase sub-class
        and register it.

        The new instance is returned on success.  None is returned on error.
        """
        dArgsCopy = dArgs.copy() if dArgs is not None else {};
        dArgsCopy['oVBox'] = self;
        return oSubClass.registerDerivedEventHandler(self.oVBoxMgr, self.fpApiVer, oSubClass, dArgsCopy,
                                                     self.o, 'IVirtualBox', 'IVirtualBoxCallback');

    def deleteHdByLocation(self, sHdLocation):
        """
        Deletes a disk image from the host, given it's location.
        Returns True on success and False on failure. Error information is logged.
        """
        try:
            oIMedium = self.o.findHardDisk(sHdLocation);
        except:
            try:
                if self.fpApiVer >= 4.1:
                    oIMedium = self.o.openMedium(sHdLocation, vboxcon.DeviceType_HardDisk,
                                                 vboxcon.AccessMode_ReadWrite, False);
                elif self.fpApiVer >= 4.0:
                    oIMedium = self.o.openMedium(sHdLocation, vboxcon.DeviceType_HardDisk,
                                                 vboxcon.AccessMode_ReadWrite);
                else:
                    oIMedium = self.o.openHardDisk(sHdLocation, vboxcon.AccessMode_ReadOnly, False, "", False, "");
            except:
                return reporter.errorXcpt('failed to open hd "%s"' % (sHdLocation));
        return self.deleteHdByMedium(oIMedium)

    def deleteHdByMedium(self, oIMedium):
        """
        Deletes a disk image from the host, given an IMedium reference.
        Returns True on success and False on failure. Error information is logged.
        """
        try:    oProgressCom = oIMedium.deleteStorage();
        except: return reporter.errorXcpt('deleteStorage() for disk %s failed' % (oIMedium,));
        try:    oProgress = ProgressWrapper(oProgressCom, self.oVBoxMgr, self.oTstDrv, 'delete disk %s' % (oIMedium.location));
        except: return reporter.errorXcpt();
        oProgress.wait();
        oProgress.logResult();
        return oProgress.isSuccess();



class ProgressWrapper(TdTaskBase):
    """
    Wrapper around a progress object for making it a task and providing useful
    utility methods.
    The real progress object can be accessed thru the o member.
    """

    def __init__(self, oProgress, oVBoxMgr, oTstDrv, sName):
        TdTaskBase.__init__(self, utils.getCallerName());
        self.o          = oProgress;
        self.oVBoxMgr   = oVBoxMgr;
        self.oTstDrv    = oTstDrv;
        self.sName      = sName;

    def toString(self):
        return '<%s sName=%s, oProgress=%s >' \
             % (TdTaskBase.toString(self), self.sName, self.o);

    #
    # TdTaskBase overrides.
    #

    def pollTask(self, fLocked = False):
        """
        Overrides TdTaskBase.pollTask().

        This method returns False until the progress object has completed.
        """
        self.doQuickApiTest();
        try:
            try:
                if self.o.completed:
                    return True;
            except:
                pass;
        finally:
            self.oTstDrv.processPendingEvents();
        return False;

    def waitForTask(self, cMsTimeout = 0):
        """
        Overrides TdTaskBase.waitForTask().
        Process XPCOM/COM events while waiting.
        """
        msStart = base.timestampMilli();
        fState  = self.pollTask(False);
        while not fState:
            cMsElapsed = base.timestampMilli() - msStart;
            if cMsElapsed > cMsTimeout:
                break;
            cMsToWait = cMsTimeout - cMsElapsed;
            cMsToWait = min(cMsToWait, 500);
            try:
                self.o.waitForCompletion(cMsToWait);
            except KeyboardInterrupt: raise;
            except: pass;
            if self.fnProcessEvents:
                self.fnProcessEvents();
            reporter.doPollWork('ProgressWrapper.waitForTask');
            fState = self.pollTask(False);
        return fState;

    #
    # Utility methods.
    #

    def isSuccess(self):
        """
        Tests if the progress object completed successfully.
        Returns True on success, False on failure or incomplete.
        """
        if not self.isCompleted():
            return False;
        return self.getResult() >= 0;

    def isCompleted(self):
        """
        Wrapper around IProgress.completed.
        """
        return self.pollTask();

    def isCancelable(self):
        """
        Wrapper around IProgress.cancelable.
        """
        try:
            fRc = self.o.cancelable;
        except:
            reporter.logXcpt();
            fRc = False;
        return fRc;

    def wasCanceled(self):
        """
        Wrapper around IProgress.canceled.
        """
        try:
            fRc = self.o.canceled;
        except:
            reporter.logXcpt(self.sName);
            fRc = False;
        return fRc;

    def cancel(self):
        """
        Wrapper around IProgress.cancel()
        Returns True on success, False on failure (logged as error).
        """
        try:
            self.o.cancel();
        except:
            reporter.errorXcpt(self.sName);
            return False;
        return True;

    def getResult(self):
        """
        Wrapper around IProgress.resultCode.
        """
        try:
            iRc = self.o.resultCode;
        except:
            reporter.logXcpt(self.sName);
            iRc = -1;
        return iRc;

    def getErrInfoResultCode(self):
        """
        Wrapper around IProgress.errorInfo.resultCode.

        Returns the string on success, -1 on bad objects (logged as error), and
        -2 on missing errorInfo object.
        """
        iRc = -1;
        try:
            oErrInfo = self.o.errorInfo;
        except:
            reporter.errorXcpt(self.sName);
        else:
            if oErrInfo is None:
                iRc = -2;
            else:
                try:
                    iRc = oErrInfo.resultCode;
                except:
                    reporter.errorXcpt();
        return iRc;

    def getErrInfoText(self):
        """
        Wrapper around IProgress.errorInfo.text.

        Returns the string on success, None on failure.  Missing errorInfo is
        not logged as an error, all other failures are.
        """
        sText = None;
        try:
            oErrInfo = self.o.errorInfo;
        except:
            reporter.log2Xcpt(self.sName);
        else:
            if oErrInfo is not None:
                try:
                    sText = oErrInfo.text;
                except:
                    reporter.errorXcpt();
        return sText;

    def stringifyErrorInfo(self):
        """
        Formats IProgress.errorInfo into a string.
        """
        try:
            oErrInfo = self.o.errorInfo;
        except:
            reporter.logXcpt(self.sName);
            sErr = 'no error info';
        else:
            sErr = vbox.stringifyErrorInfo(oErrInfo);
        return sErr;

    def stringifyResult(self):
        """
        Stringify the result.
        """
        if self.isCompleted():
            if self.wasCanceled():
                sRet = 'Progress %s: Canceled, hrc=%s' % (self.sName, vbox.ComError.toString(self.getResult()));
            elif self.getResult() == 0:
                sRet = 'Progress %s: Success' % (self.sName,);
            elif self.getResult() > 0:
                sRet = 'Progress %s: Success (hrc=%s)' % (self.sName, vbox.ComError.toString(self.getResult()));
            else:
                sRet = 'Progress %s: Failed! %s' % (self.sName, self.stringifyErrorInfo());
        else:
            sRet = 'Progress %s: Not completed yet...' % (self.sName);
        return sRet;

    def logResult(self, fIgnoreErrors = False):
        """
        Logs the result, failure logged as error unless fIgnoreErrors is True.
        Return True on success, False on failure (and fIgnoreErrors is false).
        """
        sText = self.stringifyResult();
        if self.isCompleted() and self.getResult() < 0 and fIgnoreErrors is False:
            return reporter.error(sText);
        reporter.log(sText);
        return True;

    def waitOnProgress(self, cMsInterval = 1000):
        """
        See vbox.TestDriver.waitOnProgress.
        """
        self.doQuickApiTest();
        return self.oTstDrv.waitOnProgress(self.o, cMsInterval);

    def wait(self, cMsTimeout = 60000, fErrorOnTimeout = True, cMsInterval = 1000):
        """
        Wait on the progress object for a while.

        Returns the resultCode of the progress object if completed.
        Returns -1 on timeout, logged as error if fErrorOnTimeout is set.
        Returns -2 is the progress object is invalid or waitForCompletion
        fails (logged as errors).
        """
        msStart = base.timestampMilli();
        while True:
            self.oTstDrv.processPendingEvents();
            self.doQuickApiTest();
            try:
                if self.o.completed:
                    break;
            except:
                reporter.errorXcpt(self.sName);
                return -2;
            self.oTstDrv.processPendingEvents();

            cMsElapsed = base.timestampMilli() - msStart;
            if cMsElapsed > cMsTimeout:
                if fErrorOnTimeout:
                    reporter.error('Timing out after waiting for %u s on "%s"' % (cMsTimeout / 1000, self.sName))
                return -1;

            try:
                self.o.waitForCompletion(cMsInterval);
            except:
                reporter.errorXcpt(self.sName);
                return -2;
            reporter.doPollWork('ProgressWrapper.wait');

        try:
            rc = self.o.resultCode;
        except:
            rc = -2;
            reporter.errorXcpt(self.sName);
        self.oTstDrv.processPendingEvents();
        return rc;

    def waitForOperation(self, iOperation, cMsTimeout = 60000, fErrorOnTimeout = True, cMsInterval = 1000, \
                         fIgnoreErrors = False):
        """
        Wait for the completion of a operation.

        Negative iOperation values are relative to operationCount (this
        property may changed at runtime).

        Returns 0 if the operation completed normally.
        Returns -1 on timeout, logged as error if fErrorOnTimeout is set.
        Returns -2 is the progress object is invalid or waitForCompletion
        fails (logged as errors).
        Returns -3 if if the operation completed with an error, this is logged
        as an error.
        """
        msStart = base.timestampMilli();
        while True:
            self.oTstDrv.processPendingEvents();
            self.doQuickApiTest();
            try:
                iCurrentOperation = self.o.operation;
                cOperations       = self.o.operationCount;
                if iOperation >= 0:
                    iRealOperation = iOperation;
                else:
                    iRealOperation = cOperations + iOperation;

                if iCurrentOperation > iRealOperation:
                    return 0;
                if iCurrentOperation == iRealOperation \
                   and iRealOperation >= cOperations - 1 \
                   and self.o.completed:
                    if self.o.resultCode < 0:
                        self.logResult(fIgnoreErrors);
                        return -3;
                    return 0;
            except:
                if fIgnoreErrors:
                    reporter.logXcpt();
                else:
                    reporter.errorXcpt();
                return -2;
            self.oTstDrv.processPendingEvents();

            cMsElapsed = base.timestampMilli() - msStart;
            if cMsElapsed > cMsTimeout:
                if fErrorOnTimeout:
                    if fIgnoreErrors:
                        reporter.log('Timing out after waiting for %s s on "%s" operation %d' \
                                     % (cMsTimeout / 1000, self.sName, iOperation))
                    else:
                        reporter.error('Timing out after waiting for %s s on "%s" operation %d' \
                                       % (cMsTimeout / 1000, self.sName, iOperation))
                return -1;

            try:
                self.o.waitForOperationCompletion(iRealOperation, cMsInterval);
            except:
                if fIgnoreErrors:
                    reporter.logXcpt(self.sName);
                else:
                    reporter.errorXcpt(self.sName);
                return -2;
            reporter.doPollWork('ProgressWrapper.waitForOperation');
        # Not reached.
        return -3; # Make pylin happy (for now).

    def doQuickApiTest(self):
        """
        Queries everything that is stable and easy to get at and checks that
        they don't throw errors.
        """
        if True is True: # pylint: disable=comparison-with-itself,comparison-of-constants
            try:
                iPct        = self.o.operationPercent;
                sDesc       = self.o.description;
                fCancelable = self.o.cancelable;
                cSecsRemain = self.o.timeRemaining;
                fCanceled   = self.o.canceled;
                fCompleted  = self.o.completed;
                iOp         = self.o.operation;
                cOps        = self.o.operationCount;
                iOpPct      = self.o.operationPercent;
                sOpDesc     = self.o.operationDescription;
            except:
                reporter.errorXcpt('%s: %s' % (self.sName, self.o,));
                return False;
            try:
                # Very noisy -- only enable for debugging purposes.
                #reporter.log2('%s: op=%u/%u/%s: %u%%; total=%u%% cancel=%s/%s compl=%s rem=%us; desc=%s' \
                #              % (self.sName, iOp, cOps, sOpDesc, iOpPct, iPct, fCanceled, fCancelable, fCompleted, \
                #                 cSecsRemain, sDesc));
                _ = iPct; _ = sDesc;  _ = fCancelable; _ = cSecsRemain; _ = fCanceled; _ = fCompleted; _ = iOp;
                _ = cOps; _ = iOpPct; _ = sOpDesc;
            except:
                reporter.errorXcpt();
                return False;

        return True;


class SessionWrapper(TdTaskBase):
    """
    Wrapper around a machine session.  The real session object can be accessed
    thru the o member (short is good, right :-).
    """

    def __init__(self, oSession, oVM, oVBox, oVBoxMgr, oTstDrv, fRemoteSession, sFallbackName = None, sLogFile = None):
        """
        Initializes the session wrapper.
        """
        TdTaskBase.__init__(self, utils.getCallerName());
        self.o                      = oSession;
        self.oVBox                  = oVBox;
        self.oVBoxMgr               = oVBoxMgr;
        self.oVM                    = oVM;  # Not the session machine. Useful backdoor...
        self.oTstDrv                = oTstDrv;
        self.fpApiVer               = oTstDrv.fpApiVer;
        self.fRemoteSession         = fRemoteSession;
        self.sLogFile               = sLogFile;
        self.oConsoleEventHandler   = None;
        self.uPid                   = None;
        self.fPidFile               = True;
        self.fHostMemoryLow         = False;    # see signalHostMemoryLow; read-only for outsiders.

        try:
            self.sName              = oSession.machine.name;
        except:
            if sFallbackName is not None:
                self.sName          = sFallbackName;
            else:
                try:    self.sName  = str(oSession.machine);
                except: self.sName  = 'is-this-vm-already-off'

        try:
            self.sUuid              = oSession.machine.id;
        except:
            self.sUuid              = None;

        # Try cache the SessionPID.
        self.getPid();

    def __del__(self):
        """
        Destructor that makes sure the callbacks are deregistered and
        that the session is closed.
        """
        self.deregisterEventHandlerForTask();

        if self.o is not None:
            try:
                self.close();
                reporter.log('close session %s' % (self.o));
            except:
                pass;
            self.o = None;

        TdTaskBase.__del__(self);

    def toString(self):
        return '<%s: sUuid=%s, sName=%s, uPid=%s, sDbgCreated=%s, fRemoteSession=%s, oSession=%s,' \
               ' oConsoleEventHandler=%s, oVM=%s >' \
             % (type(self).__name__, self.sUuid, self.sName, self.uPid, self.sDbgCreated, self.fRemoteSession,
                self.o, self.oConsoleEventHandler, self.oVM,);

    def __str__(self):
        return self.toString();

    #
    # TdTaskBase overrides.
    #

    def __pollTask(self):
        """ Internal poller """
        # Poll for events after doing the remote GetState call, otherwise we
        # might end up sleepless because XPCOM queues a cleanup event.
        try:
            try:
                eState = self.o.machine.state;
            except Exception as oXcpt:
                if vbox.ComError.notEqual(oXcpt, vbox.ComError.E_UNEXPECTED):
                    reporter.logXcpt();
                return True;
        finally:
            self.oTstDrv.processPendingEvents();

        # Switch
        if eState == vboxcon.MachineState_Running:
            return False;
        if eState == vboxcon.MachineState_Paused:
            return False;
        if eState == vboxcon.MachineState_Teleporting:
            return False;
        if eState == vboxcon.MachineState_LiveSnapshotting:
            return False;
        if eState == vboxcon.MachineState_Starting:
            return False;
        if eState == vboxcon.MachineState_Stopping:
            return False;
        if eState == vboxcon.MachineState_Saving:
            return False;
        if eState == vboxcon.MachineState_Restoring:
            return False;
        if eState == vboxcon.MachineState_TeleportingPausedVM:
            return False;
        if eState == vboxcon.MachineState_TeleportingIn:
            return False;

        # *Beeep* fudge!
        if self.fpApiVer < 3.2 \
          and eState == vboxcon.MachineState_PoweredOff \
          and self.getAgeAsMs() < 3000:
            return False;

        reporter.log('SessionWrapper::pollTask: eState=%s' % (eState));
        return True;


    def pollTask(self, fLocked = False):
        """
        Overrides TdTaskBase.pollTask().

        This method returns False while the VM is online and running normally.
        """

        # Call super to check if the task was signalled by runtime error or similar,
        # if not then check the VM state via __pollTask.
        fRc = super(SessionWrapper, self).pollTask(fLocked);
        if not fRc:
            fRc = self.__pollTask();

        # HACK ALERT: Lazily try registering the console event handler if
        #             we're not ready.
        if not fRc and self.oConsoleEventHandler is None:
            self.registerEventHandlerForTask();

        # HACK ALERT: Lazily try get the PID and add it to the PID file.
        if not fRc and self.uPid is None:
            self.getPid();

        return fRc;

    def waitForTask(self, cMsTimeout = 0):
        """
        Overrides TdTaskBase.waitForTask().
        Process XPCOM/COM events while waiting.
        """
        msStart = base.timestampMilli();
        fState  = self.pollTask(False);
        while not fState:
            cMsElapsed = base.timestampMilli() - msStart;
            if cMsElapsed > cMsTimeout:
                break;
            cMsSleep = cMsTimeout - cMsElapsed;
            cMsSleep = min(cMsSleep, 10000);
            try:    self.oVBoxMgr.waitForEvents(cMsSleep);
            except KeyboardInterrupt: raise;
            except: pass;
            if self.fnProcessEvents:
                self.fnProcessEvents();
            reporter.doPollWork('SessionWrapper.waitForTask');
            fState = self.pollTask(False);
        return fState;

    def setTaskOwner(self, oOwner):
        """
        HACK ALERT!
        Overrides TdTaskBase.setTaskOwner() so we can try call
        registerEventHandlerForTask() again when when the testdriver calls
        addTask() after VM has been spawned.  Related to pollTask() above.

        The testdriver must not add the task too early for this to work!
        """
        if oOwner is not None:
            self.registerEventHandlerForTask()
        return TdTaskBase.setTaskOwner(self, oOwner);


    #
    # Task helpers.
    #

    def registerEventHandlerForTask(self):
        """
        Registers the console event handlers for working the task state.
        """
        if self.oConsoleEventHandler is not None:
            return True;
        self.oConsoleEventHandler = self.registerDerivedEventHandler(vbox.SessionConsoleEventHandler, {}, False);
        return self.oConsoleEventHandler is not None;

    def deregisterEventHandlerForTask(self):
        """
        Deregisters the console event handlers.
        """
        if self.oConsoleEventHandler is not None:
            self.oConsoleEventHandler.unregister();
            self.oConsoleEventHandler = None;

    def signalHostMemoryLow(self):
        """
        Used by a runtime error event handler to indicate that we're low on memory.
        Signals the task.
        """
        self.fHostMemoryLow = True;
        self.signalTask();
        return True;

    def needsPoweringOff(self):
        """
        Examins the machine state to see if the VM needs powering off.
        """
        try:
            try:
                eState = self.o.machine.state;
            except Exception as oXcpt:
                if vbox.ComError.notEqual(oXcpt, vbox.ComError.E_UNEXPECTED):
                    reporter.logXcpt();
                return False;
        finally:
            self.oTstDrv.processPendingEvents();

        # Switch
        if eState == vboxcon.MachineState_Running:
            return True;
        if eState == vboxcon.MachineState_Paused:
            return True;
        if eState == vboxcon.MachineState_Stuck:
            return True;
        if eState == vboxcon.MachineState_Teleporting:
            return True;
        if eState == vboxcon.MachineState_LiveSnapshotting:
            return True;
        if eState == vboxcon.MachineState_Starting:
            return True;
        if eState == vboxcon.MachineState_Saving:
            return True;
        if eState == vboxcon.MachineState_Restoring:
            return True;
        if eState == vboxcon.MachineState_TeleportingPausedVM:
            return True;
        if eState == vboxcon.MachineState_TeleportingIn:
            return True;
        if hasattr(vboxcon, 'MachineState_FaultTolerantSyncing'):
            if eState == vboxcon.MachineState_FaultTolerantSyncing:
                return True;
        return False;

    def assertPoweredOff(self):
        """
        Asserts that the VM is powered off, reporting an error if not.
        Returns True if powered off, False + error msg if not.
        """
        try:
            try:
                eState = self.oVM.state;
            except Exception:
                reporter.errorXcpt();
                return True;
        finally:
            self.oTstDrv.processPendingEvents();

        if eState == vboxcon.MachineState_PoweredOff:
            return True;
        reporter.error('Expected machine state "PoweredOff", machine is in the "%s" state instead.'
                       % (_nameMachineState(eState),));
        return False;

    def getMachineStateWithName(self):
        """
        Gets the current machine state both as a constant number/whatever and
        as a human readable string.  On error, the constants will be set to
        None and the string will be the error message.
        """
        try:
            eState = self.oVM.state;
        except:
            return (None, '[error getting state: %s]' % (self.oVBoxMgr.xcptToString(),));
        finally:
            self.oTstDrv.processPendingEvents();
        return (eState, _nameMachineState(eState));

    def reportPrematureTermination(self, sPrefix = ''):
        """
        Reports a premature virtual machine termination.
        Returns False to facilitate simpler error paths.
        """

        reporter.error(sPrefix + 'The virtual machine terminated prematurely!!');
        (enmState, sStateNm) = self.getMachineStateWithName();
        reporter.error(sPrefix + 'Machine state: %s' % (sStateNm,));

        if    enmState is not None \
          and enmState == vboxcon.MachineState_Aborted \
          and self.uPid is not None:
            #
            # Look for process crash info.
            #
            def addCrashFile(sLogFile, fBinary):
                """ processCollectCrashInfo callback. """
                reporter.addLogFile(sLogFile, 'crash/dump/vm' if fBinary else 'crash/report/vm');
            utils.processCollectCrashInfo(self.uPid, reporter.log, addCrashFile);

        return False;



    #
    # ISession / IMachine / ISomethingOrAnother wrappers.
    #

    def close(self):
        """
        Closes the session if it's open and removes it from the
        vbox.TestDriver.aoRemoteSessions list.
        Returns success indicator.
        """
        fRc = True;
        if self.o is not None:
            # Get the pid in case we need to kill the process later on.
            self.getPid();

            # Try close it.
            try:
                if self.fpApiVer < 3.3:
                    self.o.close();
                else:
                    self.o.unlockMachine();
                self.o = None;
            except KeyboardInterrupt:
                raise;
            except:
                # Kludge to ignore VBoxSVC's closing of our session when the
                # direct session closes / VM process terminates.  Fun!
                try:    fIgnore = self.o.state == vboxcon.SessionState_Unlocked;
                except: fIgnore = False;
                if fIgnore:
                    self.o  = None; # Must prevent a retry during GC.
                else:
                    reporter.errorXcpt('ISession::unlockMachine failed on %s' % (self.o));
                    fRc = False;

            # Remove it from the remote session list if applicable (not 100% clean).
            if fRc and self.fRemoteSession:
                try:
                    if self in self.oTstDrv.aoRemoteSessions:
                        reporter.log2('SessionWrapper::close: Removing myself from oTstDrv.aoRemoteSessions');
                        self.oTstDrv.aoRemoteSessions.remove(self)
                except:
                    reporter.logXcpt();

                if self.uPid is not None and self.fPidFile:
                    self.oTstDrv.pidFileRemove(self.uPid);
                    self.fPidFile = False;

        # It's only logical to deregister the event handler after the session
        # is closed. It also avoids circular references between the session
        # and the listener, which causes trouble with garbage collection.
        self.deregisterEventHandlerForTask();

        self.oTstDrv.processPendingEvents();
        return fRc;

    def saveSettings(self, fClose = False):
        """
        Saves the settings and optionally closes the session.
        Returns success indicator.
        """
        try:
            try:
                self.o.machine.saveSettings();
            except:
                reporter.errorXcpt('saveSettings failed on %s' % (self.o));
                return False;
        finally:
            self.oTstDrv.processPendingEvents();
        if fClose:
            return self.close();
        return True;

    def discardSettings(self, fClose = False):
        """
        Discards the settings and optionally closes the session.
        """
        try:
            try:
                self.o.machine.discardSettings();
            except:
                reporter.errorXcpt('discardSettings failed on %s' % (self.o));
                return False;
        finally:
            self.oTstDrv.processPendingEvents();
        if fClose:
            return self.close();
        return True;

    def isPlatformArch(self, enmPlatformArch):
        """
        Returns if the machine is of the given platform architecture or not.
        """
        if not self.o:
            return False;
        if self.fpApiVer >= 7.1:
            return self.o.machine.platform.architecture == enmPlatformArch;
        return enmPlatformArch == vboxcon.PlatformArchitecture_x86; # VBox < 7.1 only supported x86.

    def isPlatformARM(self):
        """
        Returns if the machine is of the ARM platform architecture or not.
        """
        if self.fpApiVer >= 7.1:
            return self.isPlatformArch(vboxcon.PlatformArchitecture_ARM);
        return False; # For VBox < 7.1 this always is false (x86 only).

    def isPlatformX86(self):
        """
        Returns if the machine is of the x86 platform architecture or not.
        """
        if self.fpApiVer >= 7.1:
            return self.isPlatformArch(vboxcon.PlatformArchitecture_x86);
        return True; # For VBox < 7.1 this always is true (x86 only).

    def enableVirtExX86(self, fEnable):
        """
        Enables or disables AMD-V/VT-x. x86 only.
        Returns True on success and False on failure.  Error information is logged.

        Ignored on non-x86 platform architectures.
        """
        if not self.isPlatformX86(): return True;
        # Enable/disable it.
        fRc = True;
        try:
            if self.fpApiVer >= 7.1:
                self.o.machine.platform.x86.setHWVirtExProperty(vboxcon.HWVirtExPropertyType_Enabled, fEnable);
            else:
                self.o.machine.setHWVirtExProperty(vboxcon.HWVirtExPropertyType_Enabled, fEnable);
        except:
            reporter.errorXcpt('failed to set HWVirtExPropertyType_Enabled=%s for "%s"' % (fEnable, self.sName));
            fRc = False;
        else:
            reporter.log('set HWVirtExPropertyType_Enabled=%s for "%s"' % (fEnable, self.sName));

        # Force/unforce it.
        if fRc and hasattr(vboxcon, 'HWVirtExPropertyType_Force'):
            try:
                if self.fpApiVer >= 7.1:
                    self.o.machine.platform.x86.setHWVirtExProperty(vboxcon.HWVirtExPropertyType_Force, fEnable);
                else:
                    self.o.machine.setHWVirtExProperty(vboxcon.HWVirtExPropertyType_Force, fEnable);
            except:
                reporter.errorXcpt('failed to set HWVirtExPropertyType_Force=%s for "%s"' % (fEnable, self.sName));
                fRc = False;
            else:
                reporter.log('set HWVirtExPropertyType_Force=%s for "%s"' % (fEnable, self.sName));
        else:
            reporter.log('Warning! vboxcon has no HWVirtExPropertyType_Force attribute.');
            ## @todo Modify CFGM to do the same for old VBox versions?

        self.oTstDrv.processPendingEvents();
        return fRc;

    def enableNestedPagingX86(self, fEnable):
        """
        Enables or disables nested paging. x86 only.
        Returns True on success and False on failure.  Error information is logged.

        Ignored on non-x86 platform architectures.
        """
        if not self.isPlatformX86(): return True;
        ## @todo Add/remove force CFGM thing, we don't want fallback logic when testing.
        fRc = True;
        try:
            if self.fpApiVer >= 7.1:
                self.o.machine.platform.x86.setHWVirtExProperty(vboxcon.HWVirtExPropertyType_NestedPaging, fEnable);
            else:
                self.o.machine.setHWVirtExProperty(vboxcon.HWVirtExPropertyType_NestedPaging, fEnable);
        except:
            reporter.errorXcpt('failed to set HWVirtExPropertyType_NestedPaging=%s for "%s"' % (fEnable, self.sName));
            fRc = False;
        else:
            reporter.log('set HWVirtExPropertyType_NestedPaging=%s for "%s"' % (fEnable, self.sName));
            self.oTstDrv.processPendingEvents();
        return fRc;

    def enableLongModeX86(self, fEnable):
        """
        Enables or disables LongMode. x86 only.
        Returns True on success and False on failure.  Error information is logged.

        Ignored on non-x86 platform architectures.
        """
        if not self.isPlatformX86(): return True;
        # Supported.
        if self.fpApiVer < 4.2  or  not hasattr(vboxcon, 'HWVirtExPropertyType_LongMode'):
            return True;

        # Enable/disable it.
        fRc = True;
        try:
            if self.fpApiVer >= 7.1:
                self.o.machine.platform.x86.setCPUProperty(vboxcon.CPUPropertyTypeX86_LongMode, fEnable);
            else:
                self.o.machine.setCPUProperty(vboxcon.CPUPropertyType_LongMode, fEnable);
        except:
            reporter.errorXcpt('failed to set CPUPropertyType_LongMode=%s for "%s"' % (fEnable, self.sName));
            fRc = False;
        else:
            reporter.log('set CPUPropertyType_LongMode=%s for "%s"' % (fEnable, self.sName));
        self.oTstDrv.processPendingEvents();
        return fRc;

    def enableNestedHwVirtX86(self, fEnable):
        """
        Enables or disables Nested Hardware-Virtualization. x86 only.
        Returns True on success and False on failure.  Error information is logged.

        Ignored on non-x86 platform architectures.
        """
        if not self.isPlatformX86(): return True;
        # Supported.
        if self.fpApiVer < 5.3:
            return True;
        if self.fpApiVer < 7.1 and not hasattr(vboxcon, 'CPUPropertyType_HWVirt'):
            return True;
        if self.fpApiVer >= 7.1 and not hasattr(vboxcon, 'CPUPropertyTypeX86_HWVirt'):
            return True;

        # Enable/disable it.
        fRc = True;
        try:
            if self.fpApiVer >= 7.1:
                self.o.machine.platform.x86.setCPUProperty(vboxcon.CPUPropertyTypeX86_HWVirt, fEnable);
            else:
                self.o.machine.setCPUProperty(vboxcon.CPUPropertyType_HWVirt, fEnable);
        except:
            reporter.errorXcpt('failed to set CPUPropertyType_HWVirt=%s for "%s"' % (fEnable, self.sName));
            fRc = False;
        else:
            reporter.log('set CPUPropertyType_HWVirt=%s for "%s"' % (fEnable, self.sName));
        self.oTstDrv.processPendingEvents();
        return fRc;

    def enablePaeX86(self, fEnable):
        """
        Enables or disables PAE. x86 only.
        Returns True on success and False on failure.  Error information is logged.

        Ignored on non-x86 platform architectures.
        """
        if not self.isPlatformX86(): return True;
        fRc = True;
        try:
            if self.fpApiVer >= 7.1:
                self.o.machine.platform.x86.setCPUProperty(vboxcon.CPUPropertyTypeX86_PAE, fEnable);
            elif self.fpApiVer >= 3.2:    # great, ain't it?
                self.o.machine.setCPUProperty(vboxcon.CPUPropertyType_PAE, fEnable);
            else:
                self.o.machine.setCpuProperty(vboxcon.CpuPropertyType_PAE, fEnable);
        except:
            reporter.errorXcpt('failed to set CPUPropertyType_PAE=%s for "%s"' % (fEnable, self.sName));
            fRc = False;
        else:
            reporter.log('set CPUPropertyType_PAE=%s for "%s"' % (fEnable, self.sName));
        self.oTstDrv.processPendingEvents();
        return fRc;

    def enableIoApic(self, fEnable):
        """
        Enables or disables the IO-APIC.
        Returns True on success and False on failure.  Error information is logged.
        """
        fRc = True;
        try:
            if self.fpApiVer >= 7.1:
                self.o.machine.firmwareSettings.IOAPICEnabled = fEnable;
            else:
                self.o.machine.BIOSSettings.IOAPICEnabled = fEnable;
        except:
            reporter.errorXcpt('failed to set firmwareSettings.IOAPICEnabled=%s for "%s"' % (fEnable, self.sName));
            fRc = False;
        else:
            reporter.log('set firmwareSettings.IOAPICEnabled=%s for "%s"' % (fEnable, self.sName));
        self.oTstDrv.processPendingEvents();
        return fRc;

    def enableHpetX86(self, fEnable):
        """
        Enables or disables the HPET. x86 only.
        Returns True on success and False on failure.  Error information is logged.

        Ignored on non-x86 platform architectures.
        """
        if not self.isPlatformX86(): return True;
        fRc = True;
        try:
            if self.fpApiVer >= 7.1:
                self.o.machine.platform.x86.HPETEnabled = fEnable;
            elif self.fpApiVer >= 4.2:
                self.o.machine.HPETEnabled = fEnable;
            else:
                self.o.machine.hpetEnabled = fEnable;
        except:
            reporter.errorXcpt('failed to set HpetEnabled=%s for "%s"' % (fEnable, self.sName));
            fRc = False;
        else:
            reporter.log('set HpetEnabled=%s for "%s"' % (fEnable, self.sName));
        self.oTstDrv.processPendingEvents();
        return fRc;

    def enableUsbHid(self, fEnable):
        """
        Enables or disables the USB HID.
        Returns True on success and False on failure.  Error information is logged.
        """
        fRc = True;
        try:
            if fEnable:
                if self.fpApiVer >= 4.3:
                    cOhciCtls = self.o.machine.getUSBControllerCountByType(vboxcon.USBControllerType_OHCI);
                    if cOhciCtls == 0:
                        self.o.machine.addUSBController('OHCI', vboxcon.USBControllerType_OHCI);
                else:
                    self.o.machine.usbController.enabled = True;

                if self.fpApiVer >= 4.2:
                    self.o.machine.pointingHIDType = vboxcon.PointingHIDType_ComboMouse;
                    self.o.machine.keyboardHIDType = vboxcon.KeyboardHIDType_ComboKeyboard;
                else:
                    self.o.machine.pointingHidType = vboxcon.PointingHidType_ComboMouse;
                    self.o.machine.keyboardHidType = vboxcon.KeyboardHidType_ComboKeyboard;
            else:
                if self.fpApiVer >= 4.2:
                    self.o.machine.pointingHIDType = vboxcon.PointingHIDType_PS2Mouse;
                    self.o.machine.keyboardHIDType = vboxcon.KeyboardHIDType_PS2Keyboard;
                else:
                    self.o.machine.pointingHidType = vboxcon.PointingHidType_PS2Mouse;
                    self.o.machine.keyboardHidType = vboxcon.KeyboardHidType_PS2Keyboard;
        except:
            reporter.errorXcpt('failed to change UsbHid to %s for "%s"' % (fEnable, self.sName));
            fRc = False;
        else:
            reporter.log('changed UsbHid to %s for "%s"' % (fEnable, self.sName));
        self.oTstDrv.processPendingEvents();
        return fRc;

    def enableUsbOhci(self, fEnable):
        """
        Enables or disables the USB OHCI controller.
        Returns True on success and False on failure.  Error information is logged.
        """
        fRc = True;
        try:
            if fEnable:
                if self.fpApiVer >= 4.3:
                    cOhciCtls = self.o.machine.getUSBControllerCountByType(vboxcon.USBControllerType_OHCI);
                    if cOhciCtls == 0:
                        self.o.machine.addUSBController('OHCI', vboxcon.USBControllerType_OHCI);
                else:
                    self.o.machine.usbController.enabled = True;
            else:
                if self.fpApiVer >= 4.3:
                    cOhciCtls = self.o.machine.getUSBControllerCountByType(vboxcon.USBControllerType_OHCI);
                    if cOhciCtls == 1:
                        self.o.machine.removeUSBController('OHCI');
                else:
                    self.o.machine.usbController.enabled = False;
        except:
            reporter.errorXcpt('failed to change OHCI to %s for "%s"' % (fEnable, self.sName));
            fRc = False;
        else:
            reporter.log('changed OHCI to %s for "%s"' % (fEnable, self.sName));
        self.oTstDrv.processPendingEvents();
        return fRc;

    def enableUsbEhci(self, fEnable):
        """
        Enables or disables the USB EHCI controller, enables also OHCI if it is still disabled.
        Returns True on success and False on failure.  Error information is logged.
        """
        fRc = True;
        try:
            if fEnable:
                if self.fpApiVer >= 4.3:
                    cOhciCtls = self.o.machine.getUSBControllerCountByType(vboxcon.USBControllerType_OHCI);
                    if cOhciCtls == 0:
                        self.o.machine.addUSBController('OHCI', vboxcon.USBControllerType_OHCI);

                    cEhciCtls = self.o.machine.getUSBControllerCountByType(vboxcon.USBControllerType_EHCI);
                    if cEhciCtls == 0:
                        self.o.machine.addUSBController('EHCI', vboxcon.USBControllerType_EHCI);
                else:
                    self.o.machine.usbController.enabled = True;
                    self.o.machine.usbController.enabledEHCI = True;
            else:
                if self.fpApiVer >= 4.3:
                    cEhciCtls = self.o.machine.getUSBControllerCountByType(vboxcon.USBControllerType_EHCI);
                    if cEhciCtls == 1:
                        self.o.machine.removeUSBController('EHCI');
                else:
                    self.o.machine.usbController.enabledEHCI = False;
        except:
            reporter.errorXcpt('failed to change EHCI to %s for "%s"' % (fEnable, self.sName));
            fRc = False;
        else:
            reporter.log('changed EHCI to %s for "%s"' % (fEnable, self.sName));
        self.oTstDrv.processPendingEvents();
        return fRc;

    def enableUsbXhci(self, fEnable):
        """
        Enables or disables the USB XHCI controller. Error information is logged.
        """
        fRc = True;
        try:
            if fEnable:
                cXhciCtls = self.o.machine.getUSBControllerCountByType(vboxcon.USBControllerType_XHCI);
                if cXhciCtls == 0:
                    self.o.machine.addUSBController('XHCI', vboxcon.USBControllerType_XHCI);
            else:
                cXhciCtls = self.o.machine.getUSBControllerCountByType(vboxcon.USBControllerType_XHCI);
                if cXhciCtls == 1:
                    self.o.machine.removeUSBController('XHCI');
        except:
            reporter.errorXcpt('failed to change XHCI to %s for "%s"' % (fEnable, self.sName));
            fRc = False;
        else:
            reporter.log('changed XHCI to %s for "%s"' % (fEnable, self.sName));
        self.oTstDrv.processPendingEvents();
        return fRc;

    def setFirmwareType(self, eType):
        """
        Sets the firmware type.
        Returns True on success and False on failure.  Error information is logged.
        """
        fRc = True;
        try:
            if self.fpApiVer >= 7.1:
                self.o.machine.firmwareSettings.firmwareType = eType;
            else:
                self.o.machine.firmwareType = eType;
        except:
            reporter.errorXcpt('failed to set firmwareType=%s for "%s"' % (eType, self.sName));
            fRc = False;
        else:
            reporter.log('set firmwareType=%s for "%s"' % (eType, self.sName));
        self.oTstDrv.processPendingEvents();
        return fRc;

    def enableSecureBoot(self, fEnable, sUefiMokPathPrefix = None):
        """
        Enables or disables Secure Boot. Error information is logged.
        """

        if self.fpApiVer >= 7.0:

            fRc = True;
            try:
                self.o.machine.nonVolatileStore.initUefiVariableStore(0);

                # Enroll necessary keys and signatures in case if Secure Boot needs to be turned ON.
                if fEnable:
                    self.o.machine.nonVolatileStore.uefiVariableStore.enrollDefaultMsSignatures();
                    self.o.machine.nonVolatileStore.uefiVariableStore.enrollOraclePlatformKey();
                    if sUefiMokPathPrefix:
                        if self.oTstDrv.uRevision >= 156564: # Backported IUefiVariableStore::addSignatureToMok() to 7.0.
                            sFullName = self.oTstDrv.getFullResourceName(sUefiMokPathPrefix) + '.der';
                            with open(sFullName, "rb") as der_file:
                                self.o.machine.nonVolatileStore.uefiVariableStore.addSignatureToMok(bytearray(der_file.read()), \
                                                                                                    uuid.uuid4().hex, \
                                                                                                    vboxcon.SignatureType_X509);
                        else:
                            reporter.log('Warning: Enrolling own keys / signatures only available for 7.0 >= r156564. ' \
                                         'Guest Additions installation might fail!');

                self.o.machine.nonVolatileStore.uefiVariableStore.secureBootEnabled = fEnable;
            except:
                reporter.errorXcpt('failed to change Secure Boot to %s for "%s"' % (fEnable, self.sName));
                fRc = False;
            else:
                reporter.log('changed Secure Boot to %s for "%s"' % (fEnable, self.sName));
            self.oTstDrv.processPendingEvents();

        else:
            reporter.log('Secure Boot is only supported for API 7.0 or newer');
            fRc = False;

        return fRc;

    def setChipsetType(self, eType):
        """
        Sets the chipset type.
        Returns True on success and False on failure.  Error information is logged.
        """
        fRc = True;
        try:
            if self.fpApiVer >= 7.1:
                self.o.machine.platform.chipsetType = eType;
            else:
                self.o.machine.chipsetType = eType;
        except:
            reporter.errorXcpt('failed to set chipsetType=%s for "%s"' % (eType, self.sName));
            fRc = False;
        else:
            reporter.log('set chipsetType=%s for "%s"' % (eType, self.sName));
        self.oTstDrv.processPendingEvents();
        return fRc;

    def setIommuType(self, eType):
        """
        Sets the IOMMU type.
        Returns True on success and False on failure.  Error information is logged.
        """
        # Supported.
        if self.fpApiVer < 6.2 or not hasattr(vboxcon, 'IommuType_Intel') or not hasattr(vboxcon, 'IommuType_AMD'):
            return True;
        fRc = True;
        try:
            if self.fpApiVer >= 7.1:
                self.o.machine.platform.iommuType = eType;
            else:
                self.o.machine.iommuType = eType;
        except:
            reporter.errorXcpt('failed to set iommuType=%s for "%s"' % (eType, self.sName));
            fRc = False;
        else:
            reporter.log('set iommuType=%s for "%s"' % (eType, self.sName));
        self.oTstDrv.processPendingEvents();
        return fRc;

    def setupBootLogo(self, fEnable, cMsLogoDisplay = 0):
        """
        Sets up the boot logo.  fEnable toggles the fade and boot menu
        settings as well as the mode.
        """
        fRc = True;
        try:
            if self.fpApiVer >= 7.1:
                fwSettings = self.o.machine.firmwareSettings;
                if fEnable:
                    fwSettings.bootMenuMode = vboxcon.FirmwareBootMenuMode_Disabled;
                else:
                    fwSettings.bootMenuMode = vboxcon.FirmwareBootMenuMode_MessageAndMenu;
            else:
                fwSettings = self.o.machine.BIOSSettings;
                if fEnable:
                    fwSettings.bootMenuMode = vboxcon.BIOSBootMenuMode_Disabled;
                else:
                    fwSettings.bootMenuMode = vboxcon.BIOSBootMenuMode_MessageAndMenu;
            fwSettings.logoFadeIn       = not fEnable;
            fwSettings.logoFadeOut      = not fEnable;
            fwSettings.logoDisplayTime  = cMsLogoDisplay;
        except:
            reporter.errorXcpt('failed to set logoFadeIn/logoFadeOut/bootMenuMode=%s for "%s"' % (fEnable, self.sName));
            fRc = False;
        else:
            reporter.log('set logoFadeIn/logoFadeOut/bootMenuMode=%s for "%s"' % (fEnable, self.sName));
        self.oTstDrv.processPendingEvents();
        return fRc;

    def setupVrdp(self, fEnable, uPort = None):
        """
        Configures VRDP.
        """
        fRc = True;
        try:
            if self.fpApiVer >= 4.0:
                self.o.machine.VRDEServer.enabled = fEnable;
            else:
                self.o.machine.VRDPServer.enabled = fEnable;
        except:
            reporter.errorXcpt('failed to set VRDEServer::enabled=%s for "%s"' % (fEnable, self.sName));
            fRc = False;

        if uPort is not None and fRc:
            try:
                if self.fpApiVer >= 4.0:
                    self.o.machine.VRDEServer.setVRDEProperty("TCP/Ports", str(uPort));
                else:
                    self.o.machine.VRDPServer.ports = str(uPort);
            except:
                reporter.errorXcpt('failed to set VRDEServer::ports=%s for "%s"' % (uPort, self.sName));
                fRc = False;
        if fRc:
            reporter.log('set VRDEServer.enabled/ports=%s/%s for "%s"' % (fEnable, uPort, self.sName));
        self.oTstDrv.processPendingEvents();
        return fRc;

    def getNicDriverNameFromType(self, eNicType):
        """
        Helper that translate the adapter type into a driver name.
        """
        if eNicType in (vboxcon.NetworkAdapterType_Am79C970A, vboxcon.NetworkAdapterType_Am79C973):
            sName = 'pcnet';
        elif eNicType in (vboxcon.NetworkAdapterType_I82540EM,
                          vboxcon.NetworkAdapterType_I82543GC,
                          vboxcon.NetworkAdapterType_I82545EM):
            sName = 'e1000';
        elif eNicType == vboxcon.NetworkAdapterType_Virtio:
            sName = 'virtio-net';
        else:
            reporter.error('Unknown adapter type "%s" (VM: "%s")' % (eNicType, self.sName));
            sName = 'pcnet';
        return sName;

    def setupNatForwardingForTxs(self, iNic = 0, iHostPort = 5042):
        """
        Sets up NAT forwarding for port 5042 if applicable, cleans up if not.
        """
        try:
            oNic = self.o.machine.getNetworkAdapter(iNic);
        except:
            reporter.errorXcpt('getNetworkAdapter(%s) failed for "%s"' % (iNic, self.sName));
            return False;

        # Nuke the old setup for all possible adapter types (in case we're
        # called after it changed).
        for sName in ('pcnet', 'e1000', 'virtio-net'):
            for sConfig in ('VBoxInternal/Devices/%s/%u/LUN#0/AttachedDriver/Config' % (sName, iNic), \
                            'VBoxInternal/Devices/%s/%u/LUN#0/Config' % (sName, iNic)):
                try:
                    self.o.machine.setExtraData('%s/txs/Protocol'  % (sConfig), '');
                    self.o.machine.setExtraData('%s/txs/HostPort'  % (sConfig), '');
                    self.o.machine.setExtraData('%s/txs/GuestPort' % (sConfig), '');
                except:
                    reporter.errorXcpt();

        # Set up port forwarding if NAT attachment.
        try:
            eAttType = oNic.attachmentType;
        except:
            reporter.errorXcpt('attachmentType on %s failed for "%s"' % (iNic, self.sName));
            return False;
        if eAttType != vboxcon.NetworkAttachmentType_NAT:
            return True;

        try:
            eNicType = oNic.adapterType;
            fTraceEnabled = oNic.traceEnabled;
        except:
            reporter.errorXcpt('attachmentType/traceEnabled on %s failed for "%s"' % (iNic, self.sName));
            return False;

        if self.fpApiVer >= 4.1:
            try:
                if self.fpApiVer >= 4.2:
                    oNatEngine = oNic.NATEngine;
                else:
                    oNatEngine = oNic.natDriver;
            except:
                reporter.errorXcpt('Failed to get INATEngine data on "%s"' % (self.sName));
                return False;
            try:    oNatEngine.removeRedirect('txs');
            except: pass;
            try:
                oNatEngine.addRedirect('txs', vboxcon.NATProtocol_TCP, '127.0.0.1', '%s' % (iHostPort), '', '5042');
            except:
                reporter.errorXcpt('Failed to add a addRedirect redirect on "%s"' % (self.sName));
                return False;

        else:
            sName = self.getNicDriverNameFromType(eNicType);
            if fTraceEnabled:
                sConfig = 'VBoxInternal/Devices/%s/%u/LUN#0/AttachedDriver/Config' % (sName, iNic)
            else:
                sConfig = 'VBoxInternal/Devices/%s/%u/LUN#0/Config' % (sName, iNic)

            try:
                self.o.machine.setExtraData('%s/txs/Protocol'  % (sConfig), 'TCP');
                self.o.machine.setExtraData('%s/txs/HostPort'  % (sConfig), '%s' % (iHostPort));
                self.o.machine.setExtraData('%s/txs/GuestPort' % (sConfig), '5042');
            except:
                reporter.errorXcpt('Failed to set NAT extra data on "%s"' % (self.sName));
                return False;
        return True;

    def setNicType(self, eType, iNic = 0):
        """
        Sets the NIC type of the specified NIC.
        Returns True on success and False on failure.  Error information is logged.
        """
        try:
            try:
                oNic = self.o.machine.getNetworkAdapter(iNic);
            except:
                reporter.errorXcpt('getNetworkAdapter(%s) failed for "%s"' % (iNic, self.sName));
                return False;
            try:
                oNic.adapterType = eType;
            except:
                reporter.errorXcpt('failed to set NIC type on slot %s to %s for VM "%s"' % (iNic, eType, self.sName));
                return False;
        finally:
            self.oTstDrv.processPendingEvents();

        if not self.setupNatForwardingForTxs(iNic):
            return False;
        reporter.log('set NIC type on slot %s to %s for VM "%s"' % (iNic, eType, self.sName));
        return True;

    def setNicTraceEnabled(self, fTraceEnabled, sTraceFile, iNic = 0):
        """
        Sets the NIC trace enabled flag and file path.
        Returns True on success and False on failure.  Error information is logged.
        """
        try:
            try:
                oNic = self.o.machine.getNetworkAdapter(iNic);
            except:
                reporter.errorXcpt('getNetworkAdapter(%s) failed for "%s"' % (iNic, self.sName));
                return False;
            try:
                oNic.traceEnabled = fTraceEnabled;
                oNic.traceFile = sTraceFile;
            except:
                reporter.errorXcpt('failed to set NIC trace flag on slot %s to %s for VM "%s"' \
                                   % (iNic, fTraceEnabled, self.sName));
                return False;
        finally:
            self.oTstDrv.processPendingEvents();

        if not self.setupNatForwardingForTxs(iNic):
            return False;
        reporter.log('set NIC trace on slot %s to "%s" (path "%s") for VM "%s"' %
                        (iNic, fTraceEnabled, sTraceFile, self.sName));
        return True;

    def getDefaultNicName(self, eAttachmentType):
        """
        Return the default network / interface name for the NIC attachment type.
        """
        sRetName = '';
        if eAttachmentType == vboxcon.NetworkAttachmentType_Bridged:
            if self.oTstDrv.sDefBridgedNic is not None:
                sRetName = self.oTstDrv.sDefBridgedNic;
            else:
                sRetName = 'eth0';
                try:
                    aoHostNics = self.oVBoxMgr.getArray(self.oVBox.host, 'networkInterfaces');
                    for oHostNic in aoHostNics:
                        if   oHostNic.interfaceType == vboxcon.HostNetworkInterfaceType_Bridged \
                         and oHostNic.status == vboxcon.HostNetworkInterfaceStatus_Up:
                            sRetName = oHostNic.name;
                            break;
                except:
                    reporter.errorXcpt();

        elif eAttachmentType == vboxcon.NetworkAttachmentType_HostOnly:
            try:
                aoHostNics = self.oVBoxMgr.getArray(self.oVBox.host, 'networkInterfaces');
                for oHostNic in aoHostNics:
                    if oHostNic.interfaceType == vboxcon.HostNetworkInterfaceType_HostOnly:
                        if oHostNic.status == vboxcon.HostNetworkInterfaceStatus_Up:
                            sRetName = oHostNic.name;
                            break;
                        if sRetName == '':
                            sRetName = oHostNic.name;
            except:
                reporter.errorXcpt();
            if sRetName == '':
                # Create a new host-only interface.
                reporter.log("Creating host only NIC ...");
                try:
                    (oIProgress, oIHostOnly) = self.oVBox.host.createHostOnlyNetworkInterface();
                    oProgress = ProgressWrapper(oIProgress, self.oVBoxMgr, self.oTstDrv, 'Create host only NIC');
                    oProgress.wait();
                    if oProgress.logResult() is False:
                        return '';
                    sRetName = oIHostOnly.name;
                except:
                    reporter.errorXcpt();
                    return '';
                reporter.log("Created host only NIC: '%s'" % (sRetName,));

        elif self.fpApiVer >= 7.0 and eAttachmentType == vboxcon.NetworkAttachmentType_HostOnlyNetwork:
            aoHostNetworks = self.oVBoxMgr.getArray(self.oVBox, 'hostOnlyNetworks');
            if aoHostNetworks:
                sRetName = aoHostNetworks[0].networkName;
            else:
                try:
                    oHostOnlyNet = self.oVBox.createHostOnlyNetwork('Host-only Test Network');
                    oHostOnlyNet.lowerIP = '192.168.56.1';
                    oHostOnlyNet.upperIP = '192.168.56.199';
                    oHostOnlyNet.networkMask = '255.255.255.0';
                    sRetName = oHostOnlyNet.networkName;
                except:
                    reporter.errorXcpt();
                    return '';

        elif eAttachmentType == vboxcon.NetworkAttachmentType_Internal:
            sRetName = 'VBoxTest';

        elif eAttachmentType == vboxcon.NetworkAttachmentType_NAT:
            sRetName = '';

        else: ## @todo Support NetworkAttachmentType_NATNetwork
            reporter.error('eAttachmentType=%s is not known' % (eAttachmentType));
        return sRetName;

    def setNicAttachment(self, eAttachmentType, sName = None, iNic = 0):
        """
        Sets the attachment type of the specified NIC.
        Returns True on success and False on failure.  Error information is logged.
        """
        try:
            oNic = self.o.machine.getNetworkAdapter(iNic);
        except:
            reporter.errorXcpt('getNetworkAdapter(%s) failed for "%s"' % (iNic, self.sName));
            return False;

        try:
            if eAttachmentType is not None:
                try:
                    if self.fpApiVer >= 4.1:
                        oNic.attachmentType = eAttachmentType;
                    else:
                        if eAttachmentType == vboxcon.NetworkAttachmentType_NAT:
                            oNic.attachToNAT();
                        elif eAttachmentType == vboxcon.NetworkAttachmentType_Bridged:
                            oNic.attachToBridgedInterface();
                        elif eAttachmentType == vboxcon.NetworkAttachmentType_Internal:
                            oNic.attachToInternalNetwork();
                        elif eAttachmentType == vboxcon.NetworkAttachmentType_HostOnly:
                            oNic.attachToHostOnlyInterface();
                        else:
                            raise base.GenError("eAttachmentType=%s is invalid" % (eAttachmentType));
                except:
                    reporter.errorXcpt('failed to set the attachment type on slot %s to %s for VM "%s"' \
                        % (iNic, eAttachmentType, self.sName));
                    return False;
            else:
                try:
                    eAttachmentType = oNic.attachmentType;
                except:
                    reporter.errorXcpt('failed to get the attachment type on slot %s for VM "%s"' % (iNic, self.sName));
                    return False;
        finally:
            self.oTstDrv.processPendingEvents();

        if sName is not None:
            # Resolve the special 'default' name.
            if sName == 'default':
                sName = self.getDefaultNicName(eAttachmentType);

            # The name translate to different attributes depending on the
            # attachment type.
            try:
                if eAttachmentType == vboxcon.NetworkAttachmentType_Bridged:
                    ## @todo check this out on windows, may have to do a
                    # translation of the name there or smth IIRC.
                    try:
                        if self.fpApiVer >= 4.1:
                            oNic.bridgedInterface = sName;
                        else:
                            oNic.hostInterface = sName;
                    except:
                        reporter.errorXcpt('failed to set the hostInterface property on slot %s to "%s" for VM "%s"'
                                           % (iNic, sName, self.sName,));
                        return False;
                elif eAttachmentType == vboxcon.NetworkAttachmentType_HostOnly:
                    try:
                        if self.fpApiVer >= 4.1:
                            oNic.hostOnlyInterface = sName;
                        else:
                            oNic.hostInterface = sName;
                    except:
                        reporter.errorXcpt('failed to set the internalNetwork property on slot %s to "%s" for VM "%s"'
                                           % (iNic, sName, self.sName,));
                        return False;
                elif self.fpApiVer >= 7.0 and eAttachmentType == vboxcon.NetworkAttachmentType_HostOnlyNetwork:
                    try:
                        oNic.hostOnlyNetwork = sName;
                    except:
                        reporter.errorXcpt('failed to set the hostOnlyNetwork property on slot %s to "%s" for VM "%s"'
                                           % (iNic, sName, self.sName,));
                        return False;
                elif eAttachmentType == vboxcon.NetworkAttachmentType_Internal:
                    try:
                        oNic.internalNetwork = sName;
                    except:
                        reporter.errorXcpt('failed to set the internalNetwork property on slot %s to "%s" for VM "%s"'
                                           % (iNic, sName, self.sName,));
                        return False;
                elif eAttachmentType == vboxcon.NetworkAttachmentType_NAT:
                    try:
                        oNic.NATNetwork = sName;
                    except:
                        reporter.errorXcpt('failed to set the NATNetwork property on slot %s to "%s" for VM "%s"'
                                           % (iNic, sName, self.sName,));
                        return False;
            finally:
                self.oTstDrv.processPendingEvents();

        if not self.setupNatForwardingForTxs(iNic):
            return False;
        reporter.log('set NIC attachment type on slot %s to %s for VM "%s"' % (iNic, eAttachmentType, self.sName));
        return True;

    def setNicLocalhostReachable(self, fReachable, iNic = 0):
        """
        Sets whether the specified NIC can reach the host or not.
        Only affects (enabled) NICs configured to NAT at the moment.

        Returns True on success and False on failure.  Error information is logged.
        """
        try:
            oNic = self.o.machine.getNetworkAdapter(iNic);
        except:
            return reporter.errorXcpt('getNetworkAdapter(%s) failed for "%s"' % (iNic, self.sName,));

        try:
            if not oNic.enabled: # NIC not enabled? Nothing to do here.
                return True;
        except:
            return reporter.errorXcpt('NIC enabled status (%s) failed for "%s"' % (iNic, self.sName,));

        reporter.log('Setting "LocalhostReachable" for network adapter in slot %d to %s' % (iNic, fReachable));

        try:
            oNatEngine = oNic.NATEngine;
        except:
            return reporter.errorXcpt('Getting NIC NAT engine (%s) failed for "%s"' % (iNic, self.sName,));

        try:
            if hasattr(oNatEngine, "localhostReachable"):
                oNatEngine.localhostReachable = fReachable;
            else:
                oNatEngine.LocalhostReachable = fReachable;
        except:
            return reporter.errorXcpt('LocalhostReachable (%s) failed for "%s"' % (iNic, self.sName,));

        return True;

    def setNicMacAddress(self, sMacAddr, iNic = 0):
        """
        Sets the MAC address of the specified NIC.

        The sMacAddr parameter is a string supplying the tail end of the MAC
        address, missing quads are supplied from a constant byte (2), the IPv4
        address of the host, and the NIC number.

        Returns True on success and False on failure.  Error information is logged.
        """

        # Resolve missing MAC address prefix by feeding in the host IP address bytes.
        cchMacAddr = len(sMacAddr);
        if 0 < cchMacAddr < 12:
            sHostIP = netutils.getPrimaryHostIp();
            abHostIP = socket.inet_aton(sHostIP);
            if sys.version_info[0] < 3:
                abHostIP = (ord(abHostIP[0]), ord(abHostIP[1]), ord(abHostIP[2]), ord(abHostIP[3]));

            if   abHostIP[0] == 127 \
              or (abHostIP[0] == 169 and abHostIP[1] == 254) \
              or (abHostIP[0] == 192 and abHostIP[1] == 168 and abHostIP[2] == 56):
                return reporter.error('host IP for "%s" is %s, most likely not unique.' % (netutils.getHostnameFqdn(), sHostIP,));

            sDefaultMac = '%02X%02X%02X%02X%02X%02X' % (0x02, abHostIP[0], abHostIP[1], abHostIP[2], abHostIP[3], iNic);
            sMacAddr = sDefaultMac[0:(12 - cchMacAddr)] + sMacAddr;

        # Get the NIC object and try set it address.
        try:
            oNic = self.o.machine.getNetworkAdapter(iNic);
        except:
            return reporter.errorXcpt('getNetworkAdapter(%s) failed for "%s"' % (iNic, self.sName,));

        try:
            oNic.MACAddress = sMacAddr;
        except:
            return reporter.errorXcpt('failed to set the MAC address on slot %s to "%s" for VM "%s"'
                                      % (iNic, sMacAddr, self.sName));

        reporter.log('set MAC address on slot %s to %s for VM "%s"' % (iNic, sMacAddr, self.sName,));
        return True;

    def setRamSize(self, cMB):
        """
        Set the RAM size of the VM.
        Returns True on success and False on failure.  Error information is logged.
        """
        fRc = True;
        try:
            self.o.machine.memorySize = cMB;
        except:
            reporter.errorXcpt('failed to set the RAM size of "%s" to %s' % (self.sName, cMB));
            fRc = False;
        else:
            reporter.log('set the RAM size of "%s" to %s' % (self.sName, cMB));
        self.oTstDrv.processPendingEvents();
        return fRc;

    def setLargePagesX86(self, fUseLargePages):
        """
        Configures whether the VM should use large pages or not. x86 only.
        Returns True on success and False on failure.  Error information is logged.

        Ignored on non-x86 platform architectures.
        """
        if not self.isPlatformX86(): return True;
        fRc = True;
        try:
            if self.fpApiVer >= 7.1:
                self.o.machine.platform.x86.setHWVirtExProperty(vboxcon.HWVirtExPropertyType_LargePages, fUseLargePages);
            else:
                self.o.machine.setHWVirtExProperty(vboxcon.HWVirtExPropertyType_LargePages, fUseLargePages);
        except:
            reporter.errorXcpt('failed to set large pages of "%s" to %s' % (self.sName, fUseLargePages));
            fRc = False;
        else:
            reporter.log('set the large pages of "%s" to %s' % (self.sName, fUseLargePages));
        self.oTstDrv.processPendingEvents();
        return fRc;

    def setVRamSize(self, cMB):
        """
        Set the RAM size of the VM.
        Returns True on success and False on failure.  Error information is logged.
        """
        fRc = True;
        try:
            if self.fpApiVer >= 6.1 and hasattr(self.o.machine, 'graphicsAdapter'):
                self.o.machine.graphicsAdapter.VRAMSize = cMB;
            else:
                self.o.machine.VRAMSize = cMB;
        except:
            reporter.errorXcpt('failed to set the VRAM size of "%s" to %s' % (self.sName, cMB));
            fRc = False;
        else:
            reporter.log('set the VRAM size of "%s" to %s' % (self.sName, cMB));
        self.oTstDrv.processPendingEvents();
        return fRc;

    def setVideoControllerType(self, eControllerType):
        """
        Set the video controller type of the VM.
        Returns True on success and False on failure.  Error information is logged.
        """
        fRc = True;
        try:
            if self.fpApiVer >= 6.1 and hasattr(self.o.machine, 'graphicsAdapter'):
                self.o.machine.graphicsAdapter.graphicsControllerType = eControllerType;
            else:
                self.o.machine.graphicsControllerType = eControllerType;
        except:
            reporter.errorXcpt('failed to set the video controller type of "%s" to %s' % (self.sName, eControllerType));
            fRc = False;
        else:
            reporter.log('set the video controller type of "%s" to %s' % (self.sName, eControllerType));
        self.oTstDrv.processPendingEvents();
        return fRc;

    def setAccelerate3DEnabled(self, fEnabled):
        """
        Set the video controller type of the VM.
        Returns True on success and False on failure.  Error information is logged.
        """
        fRc = True;
        try:
            if self.fpApiVer >= 6.1 and hasattr(self.o.machine, 'graphicsAdapter'):
                if self.fpApiVer >= 7.1 and hasattr(self.o.machine.graphicsAdapter, 'isFeatureEnabled'):
                    self.o.machine.graphicsAdapter.setFeatureEnabled(vboxcon.GraphicsFeature_Acceleration3D, fEnabled);
                else:
                    self.o.machine.graphicsAdapter.accelerate3DEnabled = fEnabled;
            else:
                self.o.machine.accelerate3DEnabled = fEnabled;
        except:
            reporter.errorXcpt('failed to set the accelerate3DEnabled of "%s" to %s' % (self.sName, fEnabled));
            fRc = False;
        else:
            reporter.log('set the accelerate3DEnabled of "%s" to %s' % (self.sName, fEnabled));
        self.oTstDrv.processPendingEvents();
        return fRc;

    def setCpuCount(self, cCpus):
        """
        Set the number of CPUs.
        Returns True on success and False on failure.  Error information is logged.
        """
        fRc = True;
        try:
            self.o.machine.CPUCount = cCpus;
        except:
            reporter.errorXcpt('failed to set the CPU count of "%s" to %s' % (self.sName, cCpus));
            fRc = False;
        else:
            reporter.log('set the CPU count of "%s" to %s' % (self.sName, cCpus));
        self.oTstDrv.processPendingEvents();
        return fRc;

    def getCpuCount(self):
        """
        Returns the number of CPUs.
        Returns the number of CPUs on success and 0 on failure. Error information is logged.
        """
        cCpus = 0;
        try:
            cCpus = self.o.machine.CPUCount;
        except:
            reporter.errorXcpt('failed to get the CPU count of "%s"' % (self.sName,));

        self.oTstDrv.processPendingEvents();
        return cCpus;

    def ensureControllerAttached(self, sController):
        """
        Makes sure the specified controller is attached to the VM, attaching it
        if necessary.
        """
        try:
            try:
                self.o.machine.getStorageControllerByName(sController);
            except:
                (eBus, eType) = _ControllerNameToBusAndType(sController);
                try:
                    oCtl = self.o.machine.addStorageController(sController, eBus);
                except:
                    reporter.errorXcpt('addStorageController("%s",%s) failed on "%s"' % (sController, eBus, self.sName) );
                    return False;
                try:
                    oCtl.controllerType = eType;
                    reporter.log('added storage controller "%s" (bus %s, type %s) to %s'
                                % (sController, eBus, eType, self.sName));
                except:
                    reporter.errorXcpt('controllerType = %s on ("%s" / %s) failed on "%s"'
                                       % (eType, sController, eBus, self.sName) );
                    return False;
        finally:
            self.oTstDrv.processPendingEvents();
        return True;

    def setStorageControllerPortCount(self, sController, iPortCount):
        """
        Set maximum ports count for storage controller
        """
        try:
            oCtl = self.o.machine.getStorageControllerByName(sController)
            oCtl.portCount = iPortCount
            self.oTstDrv.processPendingEvents()
            reporter.log('set controller "%s" port count to value %d' % (sController, iPortCount))
            return True
        except:
            reporter.log('unable to set storage controller "%s" ports count to %d' % (sController, iPortCount))

        return False

    def setStorageControllerHostIoCache(self, sController, fUseHostIoCache):
        """
        Set maximum ports count for storage controller
        """
        try:
            oCtl = self.o.machine.getStorageControllerByName(sController);
            oCtl.useHostIOCache = fUseHostIoCache;
            self.oTstDrv.processPendingEvents();
            reporter.log('set controller "%s" host I/O cache setting to %r' % (sController, fUseHostIoCache));
            return True;
        except:
            reporter.log('unable to set storage controller "%s" host I/O cache setting to %r' % (sController, fUseHostIoCache));

        return False;

    def setBootOrder(self, iPosition, eType):
        """
        Set guest boot order type
        @param iPosition    boot order position
        @param eType        device type (vboxcon.DeviceType_HardDisk,
                            vboxcon.DeviceType_DVD, vboxcon.DeviceType_Floppy)
        """
        try:
            self.o.machine.setBootOrder(iPosition, eType)
        except:
            return reporter.errorXcpt('Unable to set boot order.')

        reporter.log('Set boot order [%d] for device %s' % (iPosition, str(eType)))
        self.oTstDrv.processPendingEvents();

        return True

    def setStorageControllerType(self, eType, sController = "IDE Controller"):
        """
        Similar to ensureControllerAttached, except it will change the type.
        """
        try:
            oCtl = self.o.machine.getStorageControllerByName(sController);
        except:
            (eBus, _) = _ControllerNameToBusAndType(sController);
            try:
                oCtl = self.o.machine.addStorageController(sController, eBus);
                reporter.log('added storage controller "%s" (bus %s) to %s' % (sController, eBus, self.sName));
            except:
                reporter.errorXcpt('addStorageController("%s",%s) failed on "%s"' % (sController, eBus, self.sName) );
                return False;
        try:
            oCtl.controllerType = eType;
        except:
            reporter.errorXcpt('failed to set controller type of "%s" on "%s" to %s' % (sController, self.sName, eType) );
            return False;
        reporter.log('set controller type of "%s" on "%s" to %s' % (sController, self.sName, eType) );
        self.oTstDrv.processPendingEvents();
        return True;

    def attachDvd(self, sImage = None, sController = "IDE Controller", iPort = 1, iDevice = 0):
        """
        Attaches a DVD drive to a VM, optionally with an ISO inserted.
        Returns True on success and False on failure.  Error information is logged.
        """
        # Input validation.
        if sImage is not None and not self.oTstDrv.isResourceFile(sImage)\
            and not os.path.isabs(sImage): ## fixme - testsuite unzip ++
            reporter.fatal('"%s" is not in the resource set' % (sImage));
            return None;

        if not self.ensureControllerAttached(sController):
            return False;

        # Find/register the image if specified.
        oImage = None;
        sImageUuid = "";
        if sImage is not None:
            sFullName = self.oTstDrv.getFullResourceName(sImage)
            try:
                oImage = self.oVBox.findDVDImage(sFullName);
            except:
                try:
                    if self.fpApiVer >= 4.1:
                        oImage = self.oVBox.openMedium(sFullName, vboxcon.DeviceType_DVD, vboxcon.AccessMode_ReadOnly, False);
                    elif self.fpApiVer >= 4.0:
                        oImage = self.oVBox.openMedium(sFullName, vboxcon.DeviceType_DVD, vboxcon.AccessMode_ReadOnly);
                    else:
                        oImage = self.oVBox.openDVDImage(sFullName, "");
                except vbox.ComException as oXcpt:
                    if oXcpt.errno != -1:
                        reporter.errorXcpt('failed to open DVD image "%s" xxx' % (sFullName));
                    else:
                        reporter.errorXcpt('failed to open DVD image "%s" yyy' % (sFullName));
                    return False;
                except:
                    reporter.errorXcpt('failed to open DVD image "%s"' % (sFullName));
                    return False;
            try:
                sImageUuid = oImage.id;
            except:
                reporter.errorXcpt('failed to get the UUID of "%s"' % (sFullName));
                return False;

        # Attach the DVD.
        fRc = True;
        try:
            if self.fpApiVer >= 4.0:
                self.o.machine.attachDevice(sController, iPort, iDevice, vboxcon.DeviceType_DVD, oImage);
            else:
                self.o.machine.attachDevice(sController, iPort, iDevice, vboxcon.DeviceType_DVD, sImageUuid);
        except:
            reporter.errorXcpt('attachDevice("%s",%s,%s,HardDisk,"%s") failed on "%s"' \
                               % (sController, iPort, iDevice, sImageUuid, self.sName) );
            fRc = False;
        else:
            reporter.log('attached DVD to %s, image="%s"' % (self.sName, sImage));
        self.oTstDrv.processPendingEvents();
        return fRc;

    def attachHd(self, sHd, sController = "IDE Controller", iPort = 0, iDevice = 0, fImmutable = True, fForceResource = True):
        """
        Attaches a HD to a VM.
        Returns True on success and False on failure.  Error information is logged.
        """
        # Input validation.
        if fForceResource and not self.oTstDrv.isResourceFile(sHd):
            reporter.fatal('"%s" is not in the resource set' % (sHd,));
            return None;

        if not self.ensureControllerAttached(sController):
            return False;

        # Find the HD, registering it if necessary (as immutable).
        if fForceResource:
            sFullName = self.oTstDrv.getFullResourceName(sHd);
        else:
            sFullName = sHd;
        try:
            oHd = self.oVBox.findHardDisk(sFullName);
        except:
            try:
                if self.fpApiVer >= 4.1:
                    oHd = self.oVBox.openMedium(sFullName, vboxcon.DeviceType_HardDisk, vboxcon.AccessMode_ReadOnly, False);
                elif self.fpApiVer >= 4.0:
                    oHd = self.oVBox.openMedium(sFullName, vboxcon.DeviceType_HardDisk, vboxcon.AccessMode_ReadOnly);
                else:
                    oHd = self.oVBox.openHardDisk(sFullName, vboxcon.AccessMode_ReadOnly, False, "", False, "");
            except:
                reporter.errorXcpt('failed to open hd "%s"' % (sFullName));
                return False;
        try:
            if fImmutable:
                oHd.type = vboxcon.MediumType_Immutable;
            else:
                oHd.type = vboxcon.MediumType_Normal;
        except:
            if fImmutable:
                reporter.errorXcpt('failed to set hd "%s" immutable' % (sHd));
            else:
                reporter.errorXcpt('failed to set hd "%s" normal' % (sHd));
            return False;

        # Attach it.
        fRc = True;
        try:
            if self.fpApiVer >= 4.0:
                self.o.machine.attachDevice(sController, iPort, iDevice, vboxcon.DeviceType_HardDisk, oHd);
            else:
                self.o.machine.attachDevice(sController, iPort, iDevice, vboxcon.DeviceType_HardDisk, oHd.id);
        except:
            reporter.errorXcpt('attachDevice("%s",%s,%s,HardDisk,"%s") failed on "%s"' \
                               % (sController, iPort, iDevice, oHd.id, self.sName) );
            fRc = False;
        else:
            reporter.log('attached "%s" to %s' % (sHd, self.sName));
        self.oTstDrv.processPendingEvents();
        return fRc;

    def createBaseHd(self, sHd, sFmt = "VDI", cb = 10*1024*1024*1024, cMsTimeout = 60000, tMediumVariant = None):
        """
        Creates a base HD.
        Returns Medium object on success and None on failure.  Error information is logged.
        """
        if tMediumVariant is None:
            tMediumVariant = (vboxcon.MediumVariant_Standard, );

        try:
            if self.fpApiVer >= 5.0:
                oHd = self.oVBox.createMedium(sFmt, sHd, vboxcon.AccessMode_ReadWrite, vboxcon.DeviceType_HardDisk);
            else:
                oHd = self.oVBox.createHardDisk(sFmt, sHd);
            oProgressXpcom = oHd.createBaseStorage(cb, tMediumVariant);
            oProgress = ProgressWrapper(oProgressXpcom, self.oVBoxMgr, self.oTstDrv, 'create base disk %s' % (sHd));
            oProgress.wait(cMsTimeout);
            oProgress.logResult();
        except:
            reporter.errorXcpt('failed to create base hd "%s"' % (sHd));
            oHd = None

        return oHd;

    def createDiffHd(self, oParentHd, sHd, sFmt = "VDI"):
        """
        Creates a differencing HD.
        Returns Medium object on success and None on failure.  Error information is logged.
        """
        # Detect the proper format if requested
        if sFmt is None:
            try:
                oHdFmt = oParentHd.mediumFormat;
                lstCaps = self.oVBoxMgr.getArray(oHdFmt, 'capabilities');
                if vboxcon.MediumFormatCapabilities_Differencing in lstCaps:
                    sFmt = oHdFmt.id;
                else:
                    sFmt = 'VDI';
            except:
                reporter.errorXcpt('failed to get preferred diff format for "%s"' % (sHd));
                return None;
        try:
            if self.fpApiVer >= 5.0:
                oHd = self.oVBox.createMedium(sFmt, sHd, vboxcon.AccessMode_ReadWrite, vboxcon.DeviceType_HardDisk);
            else:
                oHd = self.oVBox.createHardDisk(sFmt, sHd);
            oProgressXpcom = oParentHd.createDiffStorage(oHd, (vboxcon.MediumVariant_Standard, ))
            oProgress = ProgressWrapper(oProgressXpcom, self.oVBoxMgr, self.oTstDrv, 'create diff disk %s' % (sHd));
            oProgress.wait();
            oProgress.logResult();
        except:
            reporter.errorXcpt('failed to create diff hd "%s"' % (sHd));
            oHd = None

        return oHd;

    def createAndAttachHd(self, sHd, sFmt = "VDI", sController = "IDE Controller", cb = 10*1024*1024*1024, # pylint: disable=too-many-arguments
                          iPort = 0, iDevice = 0, fImmutable = True, cMsTimeout = 60000, tMediumVariant = None):
        """
        Creates and attaches a HD to a VM.
        Returns True on success and False on failure.  Error information is logged.
        """
        if not self.ensureControllerAttached(sController):
            return False;

        oHd = self.createBaseHd(sHd, sFmt, cb, cMsTimeout, tMediumVariant);
        if oHd is None:
            return False;

        fRc = True;
        try:
            if fImmutable:
                oHd.type = vboxcon.MediumType_Immutable;
            else:
                oHd.type = vboxcon.MediumType_Normal;
        except:
            if fImmutable:
                reporter.errorXcpt('failed to set hd "%s" immutable' % (sHd));
            else:
                reporter.errorXcpt('failed to set hd "%s" normal' % (sHd));
            fRc = False;

        # Attach it.
        if fRc is True:
            try:
                if self.fpApiVer >= 4.0:
                    self.o.machine.attachDevice(sController, iPort, iDevice, vboxcon.DeviceType_HardDisk, oHd);
                else:
                    self.o.machine.attachDevice(sController, iPort, iDevice, vboxcon.DeviceType_HardDisk, oHd.id);
            except:
                reporter.errorXcpt('attachDevice("%s",%s,%s,HardDisk,"%s") failed on "%s"' \
                                   % (sController, iPort, iDevice, oHd.id, self.sName) );
                fRc = False;
            else:
                reporter.log('attached "%s" to %s' % (sHd, self.sName));

        # Delete disk in case of an error
        if fRc is False:
            try:
                oProgressCom = oHd.deleteStorage();
            except:
                reporter.errorXcpt('deleteStorage() for disk %s failed' % (sHd,));
            else:
                oProgress = ProgressWrapper(oProgressCom, self.oVBoxMgr, self.oTstDrv, 'delete disk %s' % (sHd));
                oProgress.wait();
                oProgress.logResult();

        self.oTstDrv.processPendingEvents();
        return fRc;

    def detachHd(self, sController = "IDE Controller", iPort = 0, iDevice = 0):
        """
        Detaches a HD, if attached, and returns a reference to it (IMedium).

        In order to delete the detached medium, the caller must first save
        the changes made in this session.

        Returns (fRc, oHd), where oHd is None unless fRc is True, and fRc is
        your standard success indicator.  Error information is logged.
        """

        # What's attached?
        try:
            oHd = self.o.machine.getMedium(sController, iPort, iDevice);
        except:
            if    self.oVBoxMgr.xcptIsOurXcptKind() \
              and self.oVBoxMgr.xcptIsEqual(None, self.oVBoxMgr.constants.VBOX_E_OBJECT_NOT_FOUND):
                reporter.log('No HD attached (to %s %s:%s)' % (sController, iPort, iDevice));
                return (True, None);
            return (reporter.errorXcpt('Error getting media at port %s, device %s, on %s.'
                                       % (iPort, iDevice, sController)), None);
        # Detach it.
        try:
            self.o.machine.detachDevice(sController, iPort, iDevice);
        except:
            return (reporter.errorXcpt('detachDevice("%s",%s,%s) failed on "%s"' \
                                       % (sController, iPort, iDevice, self.sName) ), None);
        reporter.log('detached HD ("%s",%s,%s) from %s' % (sController, iPort, iDevice, self.sName));
        return (True, oHd);

    def attachFloppy(self, sFloppy, sController = "Floppy Controller", iPort = 0, iDevice = 0):
        """
        Attaches a floppy image to a VM.
        Returns True on success and False on failure.  Error information is logged.
        """
        # Input validation.
        ## @todo Fix this wrt to bootsector-xxx.img from the validationkit.zip.
        ##if not self.oTstDrv.isResourceFile(sFloppy):
        ##    reporter.fatal('"%s" is not in the resource set' % (sFloppy));
        ##     return None;

        if not self.ensureControllerAttached(sController):
            return False;

        # Find the floppy image, registering it if necessary (as immutable).
        sFullName = self.oTstDrv.getFullResourceName(sFloppy);
        try:
            oFloppy = self.oVBox.findFloppyImage(sFullName);
        except:
            try:
                if self.fpApiVer >= 4.1:
                    oFloppy = self.oVBox.openMedium(sFullName, vboxcon.DeviceType_Floppy, vboxcon.AccessMode_ReadOnly, False);
                elif self.fpApiVer >= 4.0:
                    oFloppy = self.oVBox.openMedium(sFullName, vboxcon.DeviceType_Floppy, vboxcon.AccessMode_ReadOnly);
                else:
                    oFloppy = self.oVBox.openFloppyImage(sFullName, "");
            except:
                reporter.errorXcpt('failed to open floppy "%s"' % (sFullName));
                return False;
        ## @todo the following works but causes trouble below (asserts in main).
        #try:
        #    oFloppy.type = vboxcon.MediumType_Immutable;
        #except:
        #    reporter.errorXcpt('failed to make floppy "%s" immutable' % (sFullName));
        #    return False;

        # Attach it.
        fRc = True;
        try:
            if self.fpApiVer >= 4.0:
                self.o.machine.attachDevice(sController, iPort, iDevice, vboxcon.DeviceType_Floppy, oFloppy);
            else:
                self.o.machine.attachDevice(sController, iPort, iDevice, vboxcon.DeviceType_Floppy, oFloppy.id);
        except:
            reporter.errorXcpt('attachDevice("%s",%s,%s,Floppy,"%s") failed on "%s"' \
                               % (sController, iPort, iDevice, oFloppy.id, self.sName) );
            fRc = False;
        else:
            reporter.log('attached "%s" to %s' % (sFloppy, self.sName));
        self.oTstDrv.processPendingEvents();
        return fRc;

    def setupNic(self, sType, sXXX):
        """
        Sets up a NIC to a VM.
        Returns True on success and False on failure.  Error information is logged.
        """
        if sType == "PCNet":        enmType = vboxcon.NetworkAdapterType_Am79C973;
        elif sType == "PCNetOld":   enmType = vboxcon.NetworkAdapterType_Am79C970A;
        elif sType == "E1000":      enmType = vboxcon.NetworkAdapterType_I82545EM; # MT Server
        elif sType == "E1000Desk":  enmType = vboxcon.NetworkAdapterType_I82540EM; # MT Desktop
        elif sType == "E1000Srv2":  enmType = vboxcon.NetworkAdapterType_I82543GC; # T Server
        elif sType == "Virtio":     enmType = vboxcon.NetworkAdapterType_Virtio;
        else:
            reporter.error('Invalid NIC type: "%s" (sXXX=%s)' % (sType, sXXX));
            return False;
        ## @todo Implement me!
        if enmType is not None: pass
        return True;

    def setupAudio(self, eAudioControllerType, fEnable = True, fEnableIn = False, fEnableOut = True, eAudioDriverType = None):
        """
        Sets up audio.

        :param eAudioControllerType:    The audio controller type (vboxcon.AudioControllerType_XXX).
        :param fEnable:                 Whether to enable or disable the audio controller (default enable).
        :param fEnableIn:               Whether to enable or disable audio input (default disable).
        :param fEnableOut:              Whether to enable or disable audio output (default enable).
        :param eAudioDriverType:        The audio driver type (vboxcon.AudioDriverType_XXX), picks something suitable
                                        if None is passed (default).
        """
        try:
            if self.fpApiVer >= 7.0:
                oAdapter = self.o.machine.audioSettings.adapter;
            else:
                oAdapter = self.o.machine.audioAdapter;
        except: return reporter.errorXcpt('Failed to get the audio adapter.');

        try:    oAdapter.audioController = eAudioControllerType;
        except: return reporter.errorXcpt('Failed to set the audio controller to %s.' % (eAudioControllerType,));

        if eAudioDriverType is None:
            if self.fpApiVer >= 7.1:
                eAudioDriverType = vboxcon.AudioDriverType_Default;
            else:
                sHost = utils.getHostOs()
                if   sHost == 'darwin':    eAudioDriverType = vboxcon.AudioDriverType_CoreAudio;
                elif sHost == 'win':       eAudioDriverType = vboxcon.AudioDriverType_DirectSound;
                elif sHost == 'linux':     eAudioDriverType = vboxcon.AudioDriverType_Pulse;
                elif sHost == 'solaris':   eAudioDriverType = vboxcon.AudioDriverType_OSS;
                else:
                    reporter.error('PORTME: Do not know which audio driver to pick for: %s!' % (sHost,));
                    eAudioDriverType = vboxcon.AudioDriverType_Null;

        try:    oAdapter.audioDriver = eAudioDriverType;
        except: return reporter.errorXcpt('Failed to set the audio driver to %s.' % (eAudioDriverType,))

        try:    oAdapter.enabled = fEnable;
        except: return reporter.errorXcpt('Failed to set the "enabled" property to %s.' % (fEnable,));

        try:    oAdapter.enabledIn = fEnableIn;
        except: return reporter.errorXcpt('Failed to set the "enabledIn" property to %s.' % (fEnable,));

        try:    oAdapter.enabledOut = fEnableOut;
        except: return reporter.errorXcpt('Failed to set the "enabledOut" property to %s.' % (fEnable,));

        reporter.log('set audio controller type to %d, driver to %d, and enabled to %s (input is %s, output is %s)'
                     % (eAudioControllerType, eAudioDriverType, fEnable, fEnableIn, fEnableOut,));
        self.oTstDrv.processPendingEvents();
        return True;

    def setupPreferredConfig(self):                                             # pylint: disable=too-many-locals
        """
        Configures the VM according to the preferences of the guest type.
        """
        try:
            sOsTypeId = self.o.machine.OSTypeId;
        except:
            reporter.errorXcpt('failed to obtain the OSTypeId for "%s"' % (self.sName));
            return False;

        try:
            oOsType = self.oVBox.getGuestOSType(sOsTypeId);
        except:
            reporter.errorXcpt('getGuestOSType("%s") failed for "%s"' % (sOsTypeId, self.sName));
            return False;

        # get the attributes.
        try:
            #sFamilyId       = oOsType.familyId;
            #f64Bit          = oOsType.is64Bit;
            fIoApic         = oOsType.recommendedIOAPIC;
            fVirtEx         = oOsType.recommendedVirtEx;
            cMBRam          = oOsType.recommendedRAM;
            cMBVRam         = oOsType.recommendedVRAM;
            #cMBHdd          = oOsType.recommendedHDD;
            eNicType        = oOsType.adapterType;
            if self.fpApiVer >= 3.2:
                if self.fpApiVer >= 4.2:
                    fPae            = oOsType.recommendedPAE;
                    fUsbHid         = oOsType.recommendedUSBHID;
                    fHpet           = oOsType.recommendedHPET;
                    eStorCtlType    = oOsType.recommendedHDStorageController;
                else:
                    fPae            = oOsType.recommendedPae;
                    fUsbHid         = oOsType.recommendedUsbHid;
                    fHpet           = oOsType.recommendedHpet;
                    eStorCtlType    = oOsType.recommendedHdStorageController;
                eFirmwareType   = oOsType.recommendedFirmware;
            else:
                fPae            = False;
                fUsbHid         = False;
                fHpet           = False;
                eFirmwareType   = -1;
                eStorCtlType    = vboxcon.StorageControllerType_PIIX4;
            if self.fpApiVer >= 4.0:
                eAudioCtlType   = oOsType.recommendedAudioController;
        except:
            reporter.errorXcpt('exception reading IGuestOSType(%s) attribute' % (sOsTypeId));
            self.oTstDrv.processPendingEvents();
            return False;
        self.oTstDrv.processPendingEvents();

        # Do the setting. Continue applying settings on error in case the
        # caller ignores the return code
        fRc = True;
        if not self.enableIoApic(fIoApic):              fRc = False;
        if not self.enableVirtExX86(fVirtEx):           fRc = False;
        if not self.enablePaeX86(fPae):                 fRc = False;
        if not self.setRamSize(cMBRam):                 fRc = False;
        if not self.setVRamSize(cMBVRam):               fRc = False;
        if not self.setNicType(eNicType, 0):            fRc = False;
        if self.fpApiVer >= 3.2:
            if not self.setFirmwareType(eFirmwareType): fRc = False;
            if not self.enableUsbHid(fUsbHid):          fRc = False;
            if not self.enableHpetX86(fHpet):           fRc = False;
        if eStorCtlType in (vboxcon.StorageControllerType_PIIX3,
                            vboxcon.StorageControllerType_PIIX4,
                            vboxcon.StorageControllerType_ICH6,):
            if not self.setStorageControllerType(eStorCtlType, "IDE Controller"):
                fRc = False;
        if self.fpApiVer >= 4.0:
            if not self.setupAudio(eAudioCtlType):      fRc = False;

        return fRc;

    def addUsbDeviceFilter(self, sName, sVendorId = None, sProductId = None, sRevision = None, # pylint: disable=too-many-arguments
                           sManufacturer = None, sProduct = None, sSerialNumber = None,
                           sPort = None, sRemote = None):
        """
        Creates a USB device filter and inserts it into the VM.
        Returns True on success.
        Returns False on failure (logged).
        """
        fRc = True;

        try:
            oUsbDevFilter = self.o.machine.USBDeviceFilters.createDeviceFilter(sName);
            oUsbDevFilter.active = True;
            if sVendorId is not None:
                oUsbDevFilter.vendorId = sVendorId;
            if sProductId is not None:
                oUsbDevFilter.productId = sProductId;
            if sRevision is not None:
                oUsbDevFilter.revision = sRevision;
            if sManufacturer is not None:
                oUsbDevFilter.manufacturer = sManufacturer;
            if sProduct is not None:
                oUsbDevFilter.product = sProduct;
            if sSerialNumber is not None:
                oUsbDevFilter.serialnumber = sSerialNumber;
            if sPort is not None:
                oUsbDevFilter.port = sPort;
            if sRemote is not None:
                oUsbDevFilter.remote = sRemote;
            try:
                self.o.machine.USBDeviceFilters.insertDeviceFilter(0, oUsbDevFilter);
            except:
                reporter.errorXcpt('insertDeviceFilter(%s) failed on "%s"' \
                                   % (0, self.sName) );
                fRc = False;
            else:
                reporter.log('inserted USB device filter "%s" to %s' % (sName, self.sName));
        except:
            reporter.errorXcpt('createDeviceFilter("%s") failed on "%s"' \
                               % (sName, self.sName) );
            fRc = False;
        return fRc;

    def getGuestPropertyValue(self, sName):
        """
        Gets a guest property value.
        Returns the value on success, None on failure (logged).
        """
        try:
            sValue = self.o.machine.getGuestPropertyValue(sName);
        except:
            reporter.errorXcpt('IMachine::getGuestPropertyValue("%s") failed' % (sName));
            return None;
        return sValue;

    def setGuestPropertyValue(self, sName, sValue):
        """
        Sets a guest property value.
        Returns the True on success, False on failure (logged).
        """
        try:
            self.o.machine.setGuestPropertyValue(sName, sValue);
        except:
            reporter.errorXcpt('IMachine::setGuestPropertyValue("%s","%s") failed' % (sName, sValue));
            return False;
        return True;

    def delGuestPropertyValue(self, sName):
        """
        Deletes a guest property value.
        Returns the True on success, False on failure (logged).
        """
        try:
            oMachine = self.o.machine;
            if self.fpApiVer >= 4.2:
                oMachine.deleteGuestProperty(sName);
            else:
                oMachine.setGuestPropertyValue(sName, '');
        except:
            reporter.errorXcpt('Unable to delete guest property "%s"' % (sName,));
            return False;
        return True;

    def setExtraData(self, sKey, sValue):
        """
        Sets extra data.
        Returns the True on success, False on failure (logged).
        """
        try:
            self.o.machine.setExtraData(sKey, sValue);
        except:
            reporter.errorXcpt('IMachine::setExtraData("%s","%s") failed' % (sKey, sValue));
            return False;
        return True;

    def getExtraData(self, sKey):
        """
        Gets extra data.
        Returns value on success, None on failure.
        """
        try:
            sValue = self.o.machine.getExtraData(sKey)
        except:
            reporter.errorXcpt('IMachine::getExtraData("%s") failed' % (sKey,))
            return None
        return sValue

    def setupTeleporter(self, fEnabled=True, uPort = 6500, sAddress = '', sPassword = ''):
        """
        Sets up the teleporter for the VM.
        Returns True on success, False on failure (logged).
        """
        try:
            self.o.machine.teleporterAddress  = sAddress;
            self.o.machine.teleporterPort     = uPort;
            self.o.machine.teleporterPassword = sPassword;
            self.o.machine.teleporterEnabled  = fEnabled;
        except:
            reporter.errorXcpt('setupTeleporter(%s, %s, %s, %s)' % (fEnabled, sPassword, uPort, sAddress));
            return False;
        return True;

    def enableTeleporter(self, fEnable=True):
        """
        Enables or disables the teleporter of the VM.
        Returns True on success, False on failure (logged).
        """
        try:
            self.o.machine.teleporterEnabled = fEnable;
        except:
            reporter.errorXcpt('IMachine::teleporterEnabled=%s failed' % (fEnable));
            return False;
        return True;

    def teleport(self, sHostname = 'localhost', uPort = 6500, sPassword = 'password', cMsMaxDowntime = 250):
        """
        Wrapper around the IConsole::teleport() method.
        Returns a progress object on success, None on failure (logged).
        """
        reporter.log2('"%s"::teleport(%s,%s,%s,%s)...' % (self.sName, sHostname, uPort, sPassword, cMsMaxDowntime));
        try:
            oProgress = self.o.console.teleport(sHostname, uPort, sPassword, cMsMaxDowntime)
        except:
            reporter.errorXcpt('IConsole::teleport(%s,%s,%s,%s) failed' % (sHostname, uPort, sPassword, cMsMaxDowntime));
            return None;
        return ProgressWrapper(oProgress, self.oVBoxMgr, self.oTstDrv, 'teleport %s' % (self.sName,));

    def getOsType(self):
        """
        Gets the IGuestOSType interface for the machine.

        return IGuestOSType interface on success, None + errorXcpt on failure.
        No exceptions raised.
        """
        try:
            sOsTypeId = self.o.machine.OSTypeId;
        except:
            reporter.errorXcpt('failed to obtain the OSTypeId for "%s"' % (self.sName));
            return None;

        try:
            oOsType = self.oVBox.getGuestOSType(sOsTypeId);
        except:
            reporter.errorXcpt('getGuestOSType("%s") failed for "%s"' % (sOsTypeId, self.sName));
            return None;

        return oOsType;

    def setOsType(self, sNewTypeId):
        """
        Changes the OS type.

        returns True on success, False + errorXcpt on failure.
        No exceptions raised.
        """
        try:
            self.o.machine.OSTypeId = sNewTypeId;
        except:
            reporter.errorXcpt('failed to set the OSTypeId for "%s" to "%s"' % (self.sName, sNewTypeId));
            return False;
        return True;


    def setParavirtProvider(self, iProvider):
        """
        Sets a paravirtualisation provider.
        Returns the True on success, False on failure (logged).
        """
        try:
            self.o.machine.paravirtProvider = iProvider
        except:
            reporter.errorXcpt('Unable to set paravirtualisation provider "%s"' % (iProvider,))
            return False;
        return True;


    def setupSerialToRawFile(self, iSerialPort, sRawFile):
        """
        Enables the given serial port (zero based) and redirects it to sRawFile.
        Returns the True on success, False on failure (logged).
        """
        try:
            oPort = self.o.machine.getSerialPort(iSerialPort);
        except:
            fRc = reporter.errorXcpt('failed to get serial port #%u' % (iSerialPort,));
        else:
            try:
                oPort.path = sRawFile;
            except:
                fRc = reporter.errorXcpt('failed to set the "path" property on serial port #%u to "%s"'
                                          % (iSerialPort, sRawFile));
            else:
                try:
                    oPort.hostMode = vboxcon.PortMode_RawFile;
                except:
                    fRc = reporter.errorXcpt('failed to set the "hostMode" property on serial port #%u to PortMode_RawFile'
                                             % (iSerialPort,));
                else:
                    try:
                        oPort.enabled = True;
                    except:
                        fRc = reporter.errorXcpt('failed to set the "enable" property on serial port #%u to True'
                                                 % (iSerialPort,));
                    else:
                        reporter.log('set SerialPort[%s].enabled/hostMode/path=True/RawFile/%s' % (iSerialPort, sRawFile,));
                        fRc = True;
        self.oTstDrv.processPendingEvents();
        return fRc;


    def enableSerialPort(self, iSerialPort):
        """
        Enables the given serial port setting the initial port mode to disconnected.
        """
        try:
            oPort = self.o.machine.getSerialPort(iSerialPort);
        except:
            fRc = reporter.errorXcpt('failed to get serial port #%u' % (iSerialPort,));
        else:
            try:
                oPort.hostMode = vboxcon.PortMode_Disconnected;
            except:
                fRc = reporter.errorXcpt('failed to set the "hostMode" property on serial port #%u to PortMode_Disconnected'
                                         % (iSerialPort,));
            else:
                try:
                    oPort.enabled = True;
                except:
                    fRc = reporter.errorXcpt('failed to set the "enable" property on serial port #%u to True'
                                             % (iSerialPort,));
                else:
                    reporter.log('set SerialPort[%s].enabled/hostMode/=True/Disconnected' % (iSerialPort,));
                    fRc = True;
        self.oTstDrv.processPendingEvents();
        return fRc;


    def changeSerialPortAttachment(self, iSerialPort, ePortMode, sPath, fServer):
        """
        Changes the attachment of the given serial port to the attachment config given.
        """
        try:
            oPort = self.o.machine.getSerialPort(iSerialPort);
        except:
            fRc = reporter.errorXcpt('failed to get serial port #%u' % (iSerialPort,));
        else:
            try:
                # Change port mode to disconnected first so changes get picked up by a potentially running VM.
                oPort.hostMode = vboxcon.PortMode_Disconnected;
            except:
                fRc = reporter.errorXcpt('failed to set the "hostMode" property on serial port #%u to PortMode_Disconnected'
                                         % (iSerialPort,));
            else:
                try:
                    oPort.path     = sPath;
                    oPort.server   = fServer;
                    oPort.hostMode = ePortMode;
                except:
                    fRc = reporter.errorXcpt('failed to configure the serial port');
                else:
                    reporter.log('set SerialPort[%s].hostMode/path/server=%s/%s/%s'
                                 % (iSerialPort, ePortMode, sPath, fServer));
                    fRc = True;
        self.oTstDrv.processPendingEvents();
        return fRc;

    def setExecutionEngine(self, iVmExecEngine):
        """
        Sets a VM execution engine.
        Returns the True on success, False on failure (logged).
        """
        try:
            self.o.machine.VMExecutionEngine = iVmExecEngine;
        except:
            reporter.errorXcpt('Unable to set VM execution engine "%s"' % (iVmExecEngine,))
            return False;
        return True;

    #
    # IConsole wrappers.
    #

    def powerOff(self, fFudgeOnFailure = True):
        """
        Powers off the VM.

        Returns True on success.
        Returns False on IConsole::powerDown() failure.
        Returns None if the progress object returns failure.
        """
        #
        # Deregister event handler before we power off the VM, otherwise we're
        # racing for VM process termination and cause misleading spurious
        # error messages in the event handling code, because the event objects
        # disappear.
        #
        # Note! Doing this before powerDown to try prevent numerous smoketest
        #       timeouts on XPCOM hosts.
        #
        self.deregisterEventHandlerForTask();


        # Try power if off.
        try:
            oProgress = self.o.console.powerDown();
        except:
            reporter.logXcpt('IConsole::powerDown failed on %s' % (self.sName));
            if fFudgeOnFailure:
                self.oTstDrv.waitOnDirectSessionClose(self.oVM, 5000); # fudge
                self.waitForTask(1000);                                # fudge
            return False;

        # Wait on power off operation to complete.
        rc = self.oTstDrv.waitOnProgress(oProgress);
        if rc < 0:
            self.close();
            if fFudgeOnFailure:
                vbox.reportError(oProgress, 'powerDown for "%s" failed' % (self.sName));
                self.oTstDrv.waitOnDirectSessionClose(self.oVM, 5000); # fudge
            return None;

        # Wait for the VM to really power off or we'll fail to open a new session to it.
        self.oTstDrv.waitOnDirectSessionClose(self.oVM, 5000);         # fudge
        return self.waitForTask(30 * 1000);                            # fudge

    def saveState(self, fPause = True):
        """
        Saves state of the VM.

        Returns True on success.
        Returns False on IConsole::saveState() failure.
        Returns None if the progress object returns Failure.
        """

        if     fPause is True \
           and self.oVM.state is vboxcon.MachineState_Running:
            self.o.console.pause();
        if self.oVM.state is not vboxcon.MachineState_Paused:
            reporter.error('pause for "%s" failed' % (self.sName));
        # Try saving state.
        try:
            if self.fpApiVer >= 5.0:
                oProgress = self.o.machine.saveState()
            else:
                oProgress = self.o.console.saveState()
        except:
            reporter.logXcpt('IMachine::saveState failed on %s' % (self.sName));
            return False;

        # Wait for saving state operation to complete.
        rc = self.oTstDrv.waitOnProgress(oProgress);
        if rc < 0:
            self.close();
            return None;

        # Wait for the VM to really terminate or we'll fail to open a new session to it.
        self.oTstDrv.waitOnDirectSessionClose(self.oVM, 5000);         # fudge
        return self.waitForTask(30 * 1000);                            # fudge

    def discardSavedState(self, fRemove = True):
        """
        Discards saved state of the VM.

        Returns True on success.
        Returns False on IConsole::discardSaveState() failure.
        """

        try:
            if self.fpApiVer >= 5.0:
                self.o.machine.discardSavedState(fRemove)
            else:
                self.o.console.discardSavedState(fRemove)
        except:
            reporter.logXcpt('IMachine::discardSavedState failed on %s' % (self.sName))
            return False
        return True

    def restoreSnapshot(self, oSnapshot, fFudgeOnFailure = True):
        """
        Restores the given snapshot.

        Returns True on success.
        Returns False on IMachine::restoreSnapshot() failure.
        Returns None if the progress object returns failure.
        """
        try:
            if self.fpApiVer >= 5.0:
                oProgress = self.o.machine.restoreSnapshot(oSnapshot);
            else:
                oProgress = self.o.console.restoreSnapshot(oSnapshot);
        except:
            reporter.logXcpt('IMachine::restoreSnapshot failed on %s' % (self.sName));
            if fFudgeOnFailure:
                self.oTstDrv.waitOnDirectSessionClose(self.oVM, 5000); # fudge
                self.waitForTask(1000);                                # fudge
            return False;

        rc = self.oTstDrv.waitOnProgress(oProgress);
        if rc < 0:
            self.close();
            if fFudgeOnFailure:
                vbox.reportError(oProgress, 'restoreSnapshot for "%s" failed' % (self.sName));
            return None;

        return self.waitForTask(30 * 1000);

    def deleteSnapshot(self, oSnapshot, fFudgeOnFailure = True, cMsTimeout = 30 * 1000):
        """
        Deletes the given snapshot merging the diff image into the base.

        Returns True on success.
        Returns False on IMachine::deleteSnapshot() failure.
        """
        try:
            if self.fpApiVer >= 5.0:
                oProgressCom = self.o.machine.deleteSnapshot(oSnapshot);
            else:
                oProgressCom = self.o.console.deleteSnapshot(oSnapshot);
            oProgress = ProgressWrapper(oProgressCom, self.oVBoxMgr, self.oTstDrv, 'Delete Snapshot %s' % (oSnapshot));
            oProgress.wait(cMsTimeout);
            oProgress.logResult();
        except:
            reporter.logXcpt('IMachine::deleteSnapshot failed on %s' % (self.sName));
            if fFudgeOnFailure:
                self.oTstDrv.waitOnDirectSessionClose(self.oVM, 5000); # fudge
                self.waitForTask(1000);                                # fudge
            return False;

        return True;

    def takeSnapshot(self, sName, sDescription = '', fPause = True, fFudgeOnFailure = True, cMsTimeout = 30 * 1000):
        """
        Takes a snapshot with the given name

        Returns True on success.
        Returns False on IMachine::takeSnapshot() or VM state change failure.
        """
        try:
            if     fPause is True \
               and self.oVM.state is vboxcon.MachineState_Running:
                self.o.console.pause();
            if self.fpApiVer >= 5.0:
                (oProgressCom, _) = self.o.machine.takeSnapshot(sName, sDescription, True);
            else:
                oProgressCom = self.o.console.takeSnapshot(sName, sDescription);
            oProgress = ProgressWrapper(oProgressCom, self.oVBoxMgr, self.oTstDrv, 'Take Snapshot %s' % (sName));
            oProgress.wait(cMsTimeout);
            oProgress.logResult();
        except:
            reporter.logXcpt('IMachine::takeSnapshot failed on %s' % (self.sName));
            if fFudgeOnFailure:
                self.oTstDrv.waitOnDirectSessionClose(self.oVM, 5000); # fudge
                self.waitForTask(1000);                                # fudge
            return False;

        if     fPause is True \
           and self.oVM.state is vboxcon.MachineState_Paused:
            self.o.console.resume();

        return True;

    def findSnapshot(self, sName):
        """
        Returns the snapshot object with the given name

        Returns snapshot object on success.
        Returns None if there is no snapshot with the given name.
        """
        return self.oVM.findSnapshot(sName);

    def takeScreenshot(self, sFilename, iScreenId=0):
        """
        Take screenshot from the given display and save it to specified file.

        Returns True on success
        Returns False on failure.
        """
        try:
            if self.fpApiVer >= 5.0:
                iWidth, iHeight, _, _, _, _ = self.o.console.display.getScreenResolution(iScreenId)
                aPngData = self.o.console.display.takeScreenShotToArray(iScreenId, iWidth, iHeight,
                                                                        vboxcon.BitmapFormat_PNG)
            else:
                iWidth, iHeight, _, _, _ = self.o.console.display.getScreenResolution(iScreenId)
                aPngData = self.o.console.display.takeScreenShotPNGToArray(iScreenId, iWidth, iHeight)
        except:
            reporter.logXcpt("Unable to take screenshot")
            return False

        with open(sFilename, 'wb') as oFile: # pylint: disable=unspecified-encoding
            oFile.write(aPngData)

        return True

    def attachUsbDevice(self, sUuid, sCaptureFilename = None):
        """
        Attach given USB device UUID to the VM.

        Returns True on success
        Returns False on failure.
        """
        fRc = True;
        try:
            if sCaptureFilename is None:
                self.o.console.attachUSBDevice(sUuid, '');
            else:
                self.o.console.attachUSBDevice(sUuid, sCaptureFilename);
        except:
            reporter.logXcpt('Unable to attach USB device %s' % (sUuid,));
            fRc = False;

        return fRc;

    def detachUsbDevice(self, sUuid):
        """
        Detach given USB device UUID from the VM.

        Returns True on success
        Returns False on failure.
        """
        fRc = True;
        try:
            _ = self.o.console.detachUSBDevice(sUuid);
        except:
            reporter.logXcpt('Unable to detach USB device %s' % (sUuid,));
            fRc = False;

        return fRc;


    #
    # IMachineDebugger wrappers.
    #

    def queryOsKernelLog(self):
        """
        Tries to get the OS kernel log using the VM debugger interface.

        Returns string containing the kernel log on success.
        Returns None on failure.
        """
        sOsKernelLog = None;
        try:
            self.o.console.debugger.loadPlugIn('all');
        except:
            reporter.logXcpt('Unable to load debugger plugins');
        else:
            try:
                sOsDetected = self.o.console.debugger.detectOS();
            except:
                reporter.logXcpt('Failed to detect the guest OS');
            else:
                try:
                    sOsKernelLog = self.o.console.debugger.queryOSKernelLog(0);
                except:
                    reporter.logXcpt('Unable to get the guest OS (%s) kernel log' % (sOsDetected,));
        return sOsKernelLog;

    def queryDbgInfo(self, sItem, sArg = '', sDefault = None):
        """
        Simple wrapper around IMachineDebugger::info.

        Returns string on success, sDefault on failure (logged).
        """
        try:
            return self.o.console.debugger.info(sItem, sArg);
        except:
            reporter.logXcpt('Unable to query "%s" with arg "%s"' % (sItem, sArg,));
        return sDefault;

    def queryDbgInfoVgaText(self, sArg = 'all'):
        """
        Tries to get the 'info vgatext' output, provided we're in next mode.

        Returns string containing text on success.
        Returns None on failure or not text mode.
        """
        sVgaText = None;
        try:
            sVgaText = self.o.console.debugger.info('vgatext', sArg);
            if sVgaText.startswith('Not in text mode!'):
                sVgaText = None;
        except:
            reporter.logXcpt('Unable to query vgatext with arg "%s"' % (sArg,));
        return sVgaText;

    def queryDbgGuestStack(self, iCpu = 0):
        """
        Returns the guest stack for the given VCPU.

        Returns string containing the guest stack for the selected VCPU on success.
        Returns None on failure.
        """

        #
        # Load all plugins first and try to detect the OS so we can
        # get nicer stack traces.
        #
        try:
            self.o.console.debugger.loadPlugIn('all');
        except:
            reporter.logXcpt('Unable to load debugger plugins');
        else:
            try:
                sOsDetected = self.o.console.debugger.detectOS();
                _ = sOsDetected;
            except:
                reporter.logXcpt('Failed to detect the guest OS');

        sGuestStack = None;
        try:
            sGuestStack = self.o.console.debugger.dumpGuestStack(iCpu);
        except:
            reporter.logXcpt('Unable to query guest stack for CPU %s' % (iCpu, ));

        return sGuestStack;


    #
    # Other methods.
    #

    def getPrimaryIp(self):
        """
        Tries to obtain the primary IP address of the guest via the guest
        properties.

        Returns IP address on success.
        Returns empty string on failure.
        """
        sIpAddr = self.getGuestPropertyValue('/VirtualBox/GuestInfo/Net/0/V4/IP');
        if vbox.isIpAddrValid(sIpAddr):
            return sIpAddr;
        return '';

    def getPid(self):
        """
        Gets the process ID for the direct session unless it's ourselves.
        """
        if self.uPid is None and self.o is not None and self.fRemoteSession:
            try:
                if self.fpApiVer >= 4.2:
                    uPid = self.o.machine.sessionPID;
                else:
                    uPid = self.o.machine.sessionPid;
                if uPid != os.getpid() and uPid != 0xffffffff:
                    self.uPid = uPid;
            except Exception as oXcpt:
                if vbox.ComError.equal(oXcpt, vbox.ComError.E_UNEXPECTED):
                    try:
                        if self.fpApiVer >= 4.2:
                            uPid = self.oVM.sessionPID;
                        else:
                            uPid = self.oVM.sessionPid;
                        if uPid != os.getpid() and uPid != 0xffffffff:
                            self.uPid = uPid;
                    except:
                        reporter.log2Xcpt();
                else:
                    reporter.log2Xcpt();
            if self.uPid is not None:
                reporter.log2('getPid: %u' % (self.uPid,));
                self.fPidFile = self.oTstDrv.pidFileAdd(self.uPid, 'vm_%s' % (self.sName,), # Set-uid-to-root is similar to SUDO.
                                                        fSudo = True);
        return self.uPid;

    def addLogsToReport(self, cReleaseLogs = 1):
        """
        Retrieves and adds the release and debug logs to the test report.
        """
        fRc = True;

        # Add each of the requested release logs to the report.
        for iLog in range(0, cReleaseLogs):
            try:
                if self.fpApiVer >= 3.2:
                    sLogFile = self.oVM.queryLogFilename(iLog);
                elif iLog > 0:
                    sLogFile = '%s/VBox.log' % (self.oVM.logFolder,);
                else:
                    sLogFile = '%s/VBox.log.%u' % (self.oVM.logFolder, iLog);
            except:
                reporter.logXcpt('iLog=%s' % (iLog,));
                fRc = False;
            else:
                if sLogFile is not None and sLogFile != '': # the None bit is for a 3.2.0 bug.
                    reporter.addLogFile(sLogFile, 'log/release/vm', '%s #%u' % (self.sName, iLog),
                                        sAltName = '%s-%s' % (self.sName, os.path.basename(sLogFile),));

        # Now for the hardened windows startup log.
        try:
            sLogFile = os.path.join(self.oVM.logFolder, 'VBoxHardening.log');
        except:
            reporter.logXcpt();
            fRc = False;
        else:
            if os.path.isfile(sLogFile):
                reporter.addLogFile(sLogFile, 'log/release/vm', '%s hardening log' % (self.sName, ),
                                    sAltName = '%s-%s' % (self.sName, os.path.basename(sLogFile),));

        # Now for the debug log.
        if self.sLogFile is not None and os.path.isfile(self.sLogFile):
            reporter.addLogFile(self.sLogFile, 'log/debug/vm', '%s debug' % (self.sName, ),
                                sAltName = '%s-%s' % (self.sName, os.path.basename(self.sLogFile),));

        return fRc;

    def registerDerivedEventHandler(self, oSubClass, dArgs = None, fMustSucceed = True):
        """
        Create an instance of the given ConsoleEventHandlerBase sub-class and
        register it.

        The new instance is returned on success.  None is returned on error.
        """

        # We need a console object.
        try:
            oConsole = self.o.console;
        except Exception as oXcpt:
            if fMustSucceed or vbox.ComError.notEqual(oXcpt, vbox.ComError.E_UNEXPECTED):
                reporter.errorXcpt('Failed to get ISession::console for "%s"' % (self.sName, ));
            return None;

        # Add the base class arguments.
        dArgsCopy = dArgs.copy() if dArgs is not None else {};
        dArgsCopy['oSession'] = self;
        dArgsCopy['oConsole'] = oConsole;
        sLogSuffix = 'on %s' % (self.sName,)
        return oSubClass.registerDerivedEventHandler(self.oVBoxMgr, self.fpApiVer, oSubClass, dArgsCopy,
                                                     oConsole, 'IConsole', 'IConsoleCallback',
                                                     fMustSucceed = fMustSucceed, sLogSuffix = sLogSuffix);

    def enableVmmDevTestingPart(self, fEnabled, fEnableMMIO = False):
        """
        Enables the testing part of the VMMDev.

        Returns True on success and False on failure.  Error information is logged.
        """
        fRc = True;
        try:
            self.o.machine.setExtraData('VBoxInternal/Devices/VMMDev/0/Config/TestingEnabled',
                                        '1' if fEnabled else '');
            self.o.machine.setExtraData('VBoxInternal/Devices/VMMDev/0/Config/TestingMMIO',
                                        '1' if fEnableMMIO and fEnabled else '');
        except:
            reporter.errorXcpt('VM name "%s", fEnabled=%s' % (self.sName, fEnabled));
            fRc = False;
        else:
            reporter.log('set VMMDevTesting=%s for "%s"' % (fEnabled, self.sName));
        self.oTstDrv.processPendingEvents();
        return fRc;

    #
    # Test eXecution Service methods.
    #

    def txsConnectViaTcp(self, cMsTimeout = 10*60000, sIpAddr = None, fNatForwardingForTxs = False):
        """
        Connects to the TXS using TCP/IP as transport.  If no IP or MAC is
        addresses are specified, we'll get the IP from the guest additions.

        Returns a TxsConnectTask object on success, None + log on failure.
        """
        # If the VM is configured with a NAT interface, connect to local host.
        fReversedSetup = False;
        fUseNatForTxs  = False;
        sMacAddr       = None;
        oIDhcpServer   = None;
        if sIpAddr is None:
            try:
                oNic              = self.oVM.getNetworkAdapter(0);
                enmAttachmentType = oNic.attachmentType;
                if enmAttachmentType == vboxcon.NetworkAttachmentType_NAT:
                    fUseNatForTxs = True;
                elif enmAttachmentType == vboxcon.NetworkAttachmentType_HostOnly and not sIpAddr:
                    # Get the MAC address and find the DHCP server.
                    sMacAddr      = oNic.MACAddress;
                    sHostOnlyNIC  = oNic.hostOnlyInterface;
                    oIHostOnlyIf  = self.oVBox.host.findHostNetworkInterfaceByName(sHostOnlyNIC);
                    sHostOnlyNet  = oIHostOnlyIf.networkName;
                    oIDhcpServer  = self.oVBox.findDHCPServerByNetworkName(sHostOnlyNet);
            except:
                reporter.errorXcpt();
                return None;

        if fUseNatForTxs:
            fReversedSetup = not fNatForwardingForTxs;
            sIpAddr = '127.0.0.1';

        # Kick off the task.
        try:
            oTask = TxsConnectTask(self, cMsTimeout, sIpAddr, sMacAddr, oIDhcpServer, fReversedSetup,
                                   fnProcessEvents = self.oTstDrv.processPendingEvents);
        except:
            reporter.errorXcpt();
            oTask = None;
        return oTask;

    def txsTryConnectViaTcp(self, cMsTimeout, sHostname, fReversed = False):
        """
        Attempts to connect to a TXS instance.

        Returns True if a connection was established, False if not (only grave
        failures are logged as errors).

        Note!   The timeout is more of a guideline...
        """

        if sHostname is None  or  sHostname.strip() == '':
            raise base.GenError('Empty sHostname is not implemented yet');

        oTxsSession = txsclient.tryOpenTcpSession(cMsTimeout, sHostname, fReversedSetup = fReversed,
                                                  cMsIdleFudge = cMsTimeout // 2,
                                                  fnProcessEvents = self.oTstDrv.processPendingEvents);
        if oTxsSession is None:
            return False;

        # Wait for the connect task to time out.
        self.oTstDrv.addTask(oTxsSession);
        self.oTstDrv.processPendingEvents();
        oRc = self.oTstDrv.waitForTasks(cMsTimeout);
        self.oTstDrv.removeTask(oTxsSession);
        if oRc != oTxsSession:
            if oRc is not None:
                reporter.log('oRc=%s, expected %s' % (oRc, oTxsSession));
            self.oTstDrv.processPendingEvents();
            oTxsSession.cancelTask(); # this is synchronous
            return False;

        # Check the status.
        reporter.log2('TxsSession is ready, isSuccess() -> %s.' % (oTxsSession.isSuccess(),));
        if not oTxsSession.isSuccess():
            return False;

        reporter.log2('Disconnecting from TXS...');
        return oTxsSession.syncDisconnect();



class TxsConnectTask(TdTaskBase):
    """
    Class that takes care of connecting to a VM.
    """

    class TxsConnectTaskVBoxCallback(vbox.VirtualBoxEventHandlerBase):
        """ Class for looking for IPv4 address changes on interface 0."""
        def __init__(self, dArgs):
            vbox.VirtualBoxEventHandlerBase.__init__(self, dArgs);
            self.oParentTask = dArgs['oParentTask'];
            self.sMachineId  = dArgs['sMachineId'];

        def onGuestPropertyChange(self, sMachineId, sName, sValue, sFlags, fWasDeleted):
            """Look for IP address."""
            reporter.log2('onGuestPropertyChange(,%s,%s,%s,%s,%s)' % (sMachineId, sName, sValue, sFlags, fWasDeleted));
            if    sMachineId == self.sMachineId \
              and sName  == '/VirtualBox/GuestInfo/Net/0/V4/IP':
                oParentTask = self.oParentTask;
                if oParentTask:
                    oParentTask._setIp(sValue);                                # pylint: disable=protected-access


    def __init__(self, oSession, cMsTimeout, sIpAddr, sMacAddr, oIDhcpServer, fReversedSetup, fnProcessEvents = None):
        TdTaskBase.__init__(self, utils.getCallerName(), fnProcessEvents = fnProcessEvents);
        self.cMsTimeout         = cMsTimeout;
        self.fnProcessEvents    = fnProcessEvents;
        self.sIpAddr            = None;
        self.sNextIpAddr        = None;
        self.sMacAddr           = sMacAddr;
        self.oIDhcpServer       = oIDhcpServer;
        self.fReversedSetup     = fReversedSetup;
        self.oVBoxEventHandler  = None;
        self.oTxsSession        = None;

        # Check that the input makes sense:
        if   (sMacAddr is None) != (oIDhcpServer is None)  \
          or (sMacAddr and fReversedSetup) \
          or (sMacAddr and sIpAddr):
            reporter.error('TxsConnectTask sMacAddr=%s oIDhcpServer=%s sIpAddr=%s fReversedSetup=%s'
                           % (sMacAddr, oIDhcpServer, sIpAddr, fReversedSetup,));
            raise base.GenError();

        reporter.log2('TxsConnectTask: sIpAddr=%s fReversedSetup=%s' % (sIpAddr, fReversedSetup))
        if fReversedSetup is True:
            self._openTcpSession(sIpAddr, fReversedSetup = True);
        elif sIpAddr is not None and sIpAddr.strip() != '':
            self._openTcpSession(sIpAddr, cMsIdleFudge = 5000);
        else:
            #
            # If we've got no IP address, register callbacks that listens for
            # the primary network adaptor of the VM to set a IPv4 guest prop.
            # Note! The order in which things are done here is kind of important.
            #

            # 0. The caller zaps the property before starting the VM.
            #try:
            #    oSession.delGuestPropertyValue('/VirtualBox/GuestInfo/Net/0/V4/IP');
            #except:
            #    reporter.logXcpt();

            # 1. Register the callback / event listener object.
            dArgs = {'oParentTask':self, 'sMachineId':oSession.o.machine.id};
            self.oVBoxEventHandler = oSession.oVBox.registerDerivedEventHandler(self.TxsConnectTaskVBoxCallback, dArgs);

            # 2. Query the guest properties.
            try:
                sIpAddr = oSession.getGuestPropertyValue('/VirtualBox/GuestInfo/Net/0/V4/IP');
            except:
                reporter.errorXcpt('IMachine::getGuestPropertyValue("/VirtualBox/GuestInfo/Net/0/V4/IP") failed');
                self._deregisterEventHandler();
                raise;
            if sIpAddr is not None:
                self._setIp(sIpAddr);

            #
            # If the network adapter of the VM is host-only we can talk poll IDHCPServer
            # for the guest IP, allowing us to detect it for VMs without guest additions.
            # This will when we're polled.
            #
            if sMacAddr is not None:
                assert self.oIDhcpServer is not None;


        # end __init__

    def __del__(self):
        """ Make sure we deregister the callback. """
        self._deregisterEventHandler();
        return TdTaskBase.__del__(self);

    def toString(self):
        return '<%s cMsTimeout=%s, sIpAddr=%s, sNextIpAddr=%s, sMacAddr=%s, fReversedSetup=%s,' \
               ' oTxsSession=%s oVBoxEventHandler=%s>' \
             % (TdTaskBase.toString(self), self.cMsTimeout, self.sIpAddr, self.sNextIpAddr, self.sMacAddr, self.fReversedSetup,
                self.oTxsSession, self.oVBoxEventHandler);

    def _deregisterEventHandler(self):
        """Deregisters the event handler."""
        fRc = True;
        oVBoxEventHandler = self.oVBoxEventHandler;
        if oVBoxEventHandler is not None:
            self.oVBoxEventHandler = None;
            fRc = oVBoxEventHandler.unregister();
            oVBoxEventHandler.oParentTask = None; # Try avoid cylic deps.
        return fRc;

    def _setIp(self, sIpAddr, fInitCall = False):
        """Called when we get an IP. Will create a TXS session and signal the task."""
        sIpAddr = sIpAddr.strip();

        if   sIpAddr is not None \
         and sIpAddr != '':
            if vbox.isIpAddrValid(sIpAddr) or fInitCall:
                try:
                    for s in sIpAddr.split('.'):
                        i = int(s);
                        if str(i) != s:
                            raise Exception();
                except:
                    reporter.fatalXcpt();
                else:
                    reporter.log('TxsConnectTask: opening session to ip "%s"' % (sIpAddr));
                    self._openTcpSession(sIpAddr, cMsIdleFudge = 5000);
                    return None;

            reporter.log('TxsConnectTask: Ignoring Bad ip "%s"' % (sIpAddr));
        else:
            reporter.log2('TxsConnectTask: Ignoring empty ip "%s"' % (sIpAddr));
        return None;

    def _openTcpSession(self, sIpAddr, uPort = None, fReversedSetup = False, cMsIdleFudge = 0):
        """
        Calls txsclient.openTcpSession and switches our task to reflect the
        state of the subtask.
        """
        self.oCv.acquire();
        if self.oTxsSession is None:
            reporter.log2('_openTcpSession: sIpAddr=%s, uPort=%d, fReversedSetup=%s' %
                          (sIpAddr, uPort if uPort is not None else 0, fReversedSetup));
            self.sIpAddr     = sIpAddr;
            self.oTxsSession = txsclient.openTcpSession(self.cMsTimeout, sIpAddr, uPort, fReversedSetup,
                                                        cMsIdleFudge, fnProcessEvents = self.fnProcessEvents);
            self.oTxsSession.setTaskOwner(self);
        else:
            self.sNextIpAddr = sIpAddr;
            reporter.log2('_openTcpSession: sNextIpAddr=%s' % (sIpAddr,));
        self.oCv.release();
        return None;

    def notifyAboutReadyTask(self, oTxsSession):
        """
        Called by the TXS session task when it's done.

        We'll signal the task completed or retry depending on the result.
        """

        self.oCv.acquire();

        # Disassociate ourselves with the session (avoid cyclic ref)
        oTxsSession.setTaskOwner(None);
        fSuccess = oTxsSession.isSuccess();
        if self.oTxsSession is not None:
            if not fSuccess:
                self.oTxsSession = None;
            if fSuccess and self.fReversedSetup:
                self.sIpAddr = oTxsSession.oTransport.sHostname;
        else:
            fSuccess = False;

        # Signal done, or retry?
        fDeregister = False;
        if   fSuccess \
          or self.fReversedSetup \
          or self.getAgeAsMs() >= self.cMsTimeout:
            self.signalTaskLocked();
            fDeregister = True;
        else:
            sIpAddr = self.sNextIpAddr if self.sNextIpAddr is not None else self.sIpAddr;
            self._openTcpSession(sIpAddr, cMsIdleFudge = 5000);

        self.oCv.release();

        # If we're done, deregister the callback (w/o owning lock).  It will
        if fDeregister:
            self._deregisterEventHandler();
        return True;

    def _pollDhcpServer(self):
        """
        Polls the DHCP server by MAC address in host-only setups.
        """

        if self.sIpAddr:
            return False;

        if self.oIDhcpServer is None or not self.sMacAddr:
            return False;

        try:
            (sIpAddr, sState, secIssued, secExpire) = self.oIDhcpServer.findLeaseByMAC(self.sMacAddr, 0);
        except:
            reporter.log4Xcpt('sMacAddr=%s' % (self.sMacAddr,));
            return False;

        secNow = utils.secondsSinceUnixEpoch();
        reporter.log2('dhcp poll: secNow=%s secExpire=%s secIssued=%s sState=%s sIpAddr=%s'
                      % (secNow, secExpire, secIssued, sState, sIpAddr,));
        if secNow > secExpire or sState != 'acked' or not sIpAddr:
            return False;

        reporter.log('dhcp poll: sIpAddr=%s secExpire=%s (%s TTL) secIssued=%s (%s ago)'
                     % (sIpAddr, secExpire, secExpire - secNow, secIssued, secNow - secIssued,));
        self._setIp(sIpAddr);
        return True;

    #
    # Task methods
    #

    def pollTask(self, fLocked = False):
        """
        Overridden pollTask method.
        """
        self._pollDhcpServer();
        return TdTaskBase.pollTask(self, fLocked);

    #
    # Public methods
    #

    def getResult(self):
        """
        Returns the connected TXS session object on success.
        Returns None on failure or if the task has not yet completed.
        """
        self.oCv.acquire();
        oTxsSession = self.oTxsSession;
        self.oCv.release();

        if oTxsSession is not None  and  not oTxsSession.isSuccess():
            oTxsSession = None;
        return oTxsSession;

    def cancelTask(self):
        """ Cancels the task. """
        self._deregisterEventHandler(); # (make sure to avoid cyclic fun)
        self.oCv.acquire();
        if not self.fSignalled:
            oTxsSession = self.oTxsSession;
            if oTxsSession is not None:
                self.oCv.release();
                oTxsSession.setTaskOwner(None);
                oTxsSession.cancelTask();
                oTxsSession.waitForTask(1000);
                self.oCv.acquire();
            self.signalTaskLocked();
        self.oCv.release();
        return True;



class AdditionsStatusTask(TdTaskBase):
    """
    Class that takes care of waiting till the guest additions are in a given state.
    """

    class AdditionsStatusTaskCallback(vbox.EventHandlerBase):
        """ Class for looking for IPv4 address changes on interface 0."""
        def __init__(self, dArgs):
            self.oParentTask = dArgs['oParentTask'];
            vbox.EventHandlerBase.__init__(self, dArgs, self.oParentTask.oSession.fpApiVer,
                                           'AdditionsStatusTaskCallback/%s' % (self.oParentTask.oSession.sName,));

        def handleEvent(self, oEvt):
            try:
                enmType = oEvt.type;
            except:
                reporter.errorXcpt();
            else:
                reporter.log2('AdditionsStatusTaskCallback:handleEvent: enmType=%s' % (enmType,));
                if enmType == vboxcon.VBoxEventType_OnGuestAdditionsStatusChanged:
                    oParentTask = self.oParentTask;
                    if oParentTask:
                        oParentTask.pollTask();

            # end


    def __init__(self, oSession, oIGuest, cMsTimeout = 120000, aenmWaitForRunLevels = None, aenmWaitForActive = None,
                 aenmWaitForInactive = None):
        """
        aenmWaitForRunLevels - List of run level values to wait for (success if one matches).
        aenmWaitForActive    - List facilities (type values) that must be active.
        aenmWaitForInactive  - List facilities (type values) that must be inactive.

        The default is to wait for AdditionsRunLevelType_Userland if all three lists
        are unspecified or empty.
        """
        TdTaskBase.__init__(self, utils.getCallerName());
        self.oSession               = oSession      # type: vboxwrappers.SessionWrapper
        self.oIGuest                = oIGuest;
        self.cMsTimeout             = cMsTimeout;
        self.fSucceeded             = False;
        self.oVBoxEventHandler      = None;
        self.aenmWaitForRunLevels   = aenmWaitForRunLevels if aenmWaitForRunLevels else [];
        self.aenmWaitForActive      = aenmWaitForActive    if aenmWaitForActive    else [];
        self.aenmWaitForInactive    = aenmWaitForInactive  if aenmWaitForInactive  else [];

        # Provide a sensible default if nothing is given.
        if not self.aenmWaitForRunLevels and not self.aenmWaitForActive and not self.aenmWaitForInactive:
            self.aenmWaitForRunLevels = [vboxcon.AdditionsRunLevelType_Userland,];

        # Register the event handler on hosts which has it:
        if oSession.fpApiVer >= 6.1 or hasattr(vboxcon, 'VBoxEventType_OnGuestAdditionsStatusChanged'):
            aenmEvents = (vboxcon.VBoxEventType_OnGuestAdditionsStatusChanged,);
            dArgs = {
                'oParentTask': self,
            };
            self.oVBoxEventHandler = vbox.EventHandlerBase.registerDerivedEventHandler(oSession.oVBoxMgr,
                                                                                       oSession.fpApiVer,
                                                                                       self.AdditionsStatusTaskCallback,
                                                                                       dArgs,
                                                                                       oIGuest,
                                                                                       'IGuest',
                                                                                       'AdditionsStatusTaskCallback',
                                                                                       aenmEvents = aenmEvents);
        reporter.log2('AdditionsStatusTask: %s' % (self.toString(), ));

    def __del__(self):
        """ Make sure we deregister the callback. """
        self._deregisterEventHandler();
        self.oIGuest = None;
        return TdTaskBase.__del__(self);

    def toString(self):
        return '<%s cMsTimeout=%s, fSucceeded=%s, aenmWaitForRunLevels=%s, aenmWaitForActive=%s, aenmWaitForInactive=%s, ' \
               'oVBoxEventHandler=%s>' \
             % (TdTaskBase.toString(self), self.cMsTimeout, self.fSucceeded, self.aenmWaitForRunLevels, self.aenmWaitForActive,
                self.aenmWaitForInactive, self.oVBoxEventHandler,);

    def _deregisterEventHandler(self):
        """Deregisters the event handler."""
        fRc = True;
        oVBoxEventHandler = self.oVBoxEventHandler;
        if oVBoxEventHandler is not None:
            self.oVBoxEventHandler = None;
            fRc = oVBoxEventHandler.unregister();
            oVBoxEventHandler.oParentTask = None; # Try avoid cylic deps.
        return fRc;

    def _poll(self):
        """
        Internal worker for pollTask() that returns the new signalled state.
        """

        #
        # Check if any of the runlevels we wait for have been reached:
        #
        if self.aenmWaitForRunLevels:
            try:
                enmRunLevel = self.oIGuest.additionsRunLevel;
            except:
                reporter.errorXcpt();
                return True;
            if enmRunLevel not in self.aenmWaitForRunLevels:
                reporter.log6('AdditionsStatusTask/poll: enmRunLevel=%s not in %s' % (enmRunLevel, self.aenmWaitForRunLevels,));
                return False;
            reporter.log2('AdditionsStatusTask/poll: enmRunLevel=%s matched %s!' % (enmRunLevel, self.aenmWaitForRunLevels,));


        #
        # Check for the facilities that must all be active.
        #
        for enmFacility in self.aenmWaitForActive:
            try:
                (enmStatus, _) = self.oIGuest.getFacilityStatus(enmFacility);
            except:
                reporter.errorXcpt('enmFacility=%s' % (enmFacility,));
                return True;
            if enmStatus != vboxcon.AdditionsFacilityStatus_Active:
                reporter.log2('AdditionsStatusTask/poll: enmFacility=%s not active: %s' % (enmFacility, enmStatus,));
                return False;

        #
        # Check for the facilities that must all be inactive or terminated.
        #
        for enmFacility in self.aenmWaitForInactive:
            try:
                (enmStatus, _) = self.oIGuest.getFacilityStatus(enmFacility);
            except:
                reporter.errorXcpt('enmFacility=%s' % (enmFacility,));
                return True;
            if enmStatus not in (vboxcon.AdditionsFacilityStatus_Inactive,
                                 vboxcon.AdditionsFacilityStatus_Terminated):
                reporter.log2('AdditionsStatusTask/poll: enmFacility=%s not inactive: %s' % (enmFacility, enmStatus,));
                return False;


        reporter.log('AdditionsStatusTask: Poll succeeded, signalling...');
        self.fSucceeded = True;
        return True;


    #
    # Task methods
    #

    def pollTask(self, fLocked = False):
        """
        Overridden pollTask method.
        """
        if not fLocked:
            self.lockTask();

        fDeregister = False;
        fRc = self.fSignalled;
        if not fRc:
            fRc = self._poll();
            if fRc or self.getAgeAsMs() >= self.cMsTimeout:
                self.signalTaskLocked();
                fDeregister = True;

        if not fLocked:
            self.unlockTask();

        # If we're done, deregister the event callback (w/o owning lock).
        if fDeregister:
            self._deregisterEventHandler();
        return fRc;

    def getResult(self):
        """
        Returns true if the we succeeded.
        Returns false if not.  If the task is signalled already, then we
        encountered a problem while polling.
        """
        return self.fSucceeded;

    def cancelTask(self):
        """
        Cancels the task.
        Just to actively disengage the event handler.
        """
        self._deregisterEventHandler();
        return True;
