#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
VMM Guest OS boot tests.
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
import os
import sys
import time


# Only the main script needs to modify the path.
try:    __file__                            # pylint: disable=used-before-assignment
except: __file__ = sys.argv[0]
g_ksValidationKitDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.append(g_ksValidationKitDir)

# Validation Kit imports.
from testdriver import vbox
from testdriver import base
from testdriver import reporter
from testdriver import vboxcon


class tdGuestOsBootTest1(vbox.TestDriver):
    """
    VMM Unit Tests Set.

    Scenario:
        - Create VM that corresponds to Guest OS pre-installed on selected HDD
        - Start VM and wait for TXS server connection (which is started after Guest successfully booted)
    """

    ksSataController = 'SATA Controller'
    ksIdeController  = 'IDE Controller'

    # VM parameters required to run HDD image.
    # Format: { HDD image filename: (sKind, HDD controller type) }
    kaoVmParams = {
        't-win80.vdi':        ( 'Windows 8 (64 bit)', ksSataController ),
    }

    # List of platforms which are able to suspend and resume host automatically.
    # In order to add new platform, self._SuspendResume() should be adapted.
    kasSuspendAllowedPlatforms = ( 'darwin' )

    kcMsVmStartLimit    = 5 * 60000
    kcMsVmShutdownLimit = 1 * 60000

    def __init__(self):
        """
        Reinitialize child class instance.
        """
        vbox.TestDriver.__init__(self)

        self.sVmName             = 'TestVM'
        self.sHddName            = None
        self.sHddPathBase        = os.path.join(self.sResourcePath, '4.2', 'nat', 'win80')
        self.oVM                 = None

        # TODO: that should be moved to some common place
        self.fEnableIOAPIC       = True
        self.cCpus               = 1
        self.fEnableNestedPaging = True
        self.fEnablePAE          = False
        self.fSuspendHost        = False
        self.cSecSuspendTime     = 60
        self.cShutdownIters      = 1
        self.fExtraVm            = False
        self.sExtraVmName        = "TestVM-Extra"
        self.oExtraVM            = None
        self.fLocalCatch         = False

    #
    # Overridden methods.
    #

    def showUsage(self):
        """
        Extend usage info
        """
        rc = vbox.TestDriver.showUsage(self)
        reporter.log('  --boot-hdd <HDD image file name>')

        reporter.log('  --cpus <# CPUs>')
        reporter.log('  --no-ioapic')
        reporter.log('  --no-nested-paging')
        reporter.log('  --pae')
        reporter.log('  --suspend-host')
        reporter.log('  --suspend-time <sec>')
        reporter.log('  --shutdown-iters <# iters>')
        reporter.log('  --extra-vm')
        reporter.log('  --local-catch')
        return rc

    def parseOption(self, asArgs, iArg):
        """
        Extend standard options set
        """
        if asArgs[iArg] == '--boot-hdd':
            iArg += 1
            if iArg >= len(asArgs): raise base.InvalidOption('The "--boot-hdd" option requires an argument')
            self.sHddName = asArgs[iArg]

        elif asArgs[iArg] == '--cpus':
            iArg += 1
            if iArg >= len(asArgs): raise base.InvalidOption('The "--cpus" option requires an argument')
            self.cCpus = int(asArgs[iArg])
        elif asArgs[iArg] == '--no-ioapic':
            self.fEnableIOAPIC = False
        elif asArgs[iArg] == '--no-nested-paging':
            self.fEnableNestedPaging = False
        elif asArgs[iArg] == '--pae':
            self.fEnablePAE = True
        elif asArgs[iArg] == '--suspend-host':
            self.fSuspendHost = True
        elif asArgs[iArg] == '--suspend-time':
            iArg += 1
            if iArg >= len(asArgs): raise base.InvalidOption('The "--suspend-time" option requires an argument')
            self.cSecSuspendTime = int(asArgs[iArg])
        elif asArgs[iArg] == '--shutdown-iters':
            iArg += 1
            if iArg >= len(asArgs): raise base.InvalidOption('The "--shutdown-iters" option requires an argument')
            self.cShutdownIters = int(asArgs[iArg])
        elif asArgs[iArg] == '--extra-vm':
            self.fExtraVm = True
        elif asArgs[iArg] == '--local-catch':
            self.fLocalCatch = True
        else:
            return vbox.TestDriver.parseOption(self, asArgs, iArg)

        return iArg + 1

    def getResourceSet(self):
        """
        Returns a set of file and/or directory names relative to
        TESTBOX_PATH_RESOURCES.
        """
        return [os.path.join(self.sHddPathBase, sRsrc) for sRsrc in self.kaoVmParams];

    def _addVM(self, sVmName, sNicTraceFile=None):
        """
        Create VM
        """
        # Get VM params specific to HDD image
        sKind, sController = self.kaoVmParams[self.sHddName]

        # Create VM itself
        eNic0AttachType = vboxcon.NetworkAttachmentType_NAT
        sHddPath = os.path.join(self.sHddPathBase, self.sHddName)
        assert os.path.isfile(sHddPath)

        oVM = \
            self.createTestVM(sVmName, 1, sKind=sKind, cCpus=self.cCpus,
                              eNic0AttachType=eNic0AttachType, sDvdImage = self.sVBoxValidationKitIso)
        assert oVM is not None

        oSession = self.openSession(oVM)

        # Attach an HDD
        fRc =         oSession.attachHd(sHddPath, sController, fImmutable=True)

        # Enable HW virt
        fRc = fRc and oSession.enableVirtExX86(True)

        # Enable I/O APIC
        fRc = fRc and oSession.enableIoApic(self.fEnableIOAPIC)

        # Enable Nested Paging
        fRc = fRc and oSession.enableNestedPagingX86(self.fEnableNestedPaging)

        # Enable PAE
        fRc = fRc and oSession.enablePaeX86(self.fEnablePAE)

        if (sNicTraceFile is not None):
            fRc = fRc and oSession.setNicTraceEnabled(True, sNicTraceFile)

        # Remote desktop
        oSession.setupVrdp(True)

        fRc = fRc and oSession.saveSettings()
        fRc = fRc and oSession.close()
        assert fRc is True

        return oVM

    def actionConfig(self):
        """
        Configure pre-conditions.
        """

        if not self.importVBoxApi():
            return False

        # Save time: do not start VM if there is no way to suspend host
        if (self.fSuspendHost is True and sys.platform not in self.kasSuspendAllowedPlatforms):
            reporter.log('Platform [%s] is not in the list of supported platforms' % sys.platform)
            return False

        assert self.sHddName is not None
        if self.sHddName not in self.kaoVmParams:
            reporter.log('Error: unknown HDD image specified: %s' % self.sHddName)
            return False

        if (self.fExtraVm is True):
            self.oExtraVM = self._addVM(self.sExtraVmName)

        self.oVM = self._addVM(self.sVmName)

        return vbox.TestDriver.actionConfig(self)

    def _SuspendResume(self, cSecTimeout):
        """
        Put host into sleep and automatically resume it after specified timeout.
        """
        fRc = False

        if (sys.platform == 'darwin'):
            tsStart = time.time()
            fRc  = os.system("/usr/bin/pmset relative wake %d" % self.cSecSuspendTime)
            fRc |= os.system("/usr/bin/pmset sleepnow")
            # Wait for host to wake up
            while ((time.time() - tsStart) < self.cSecSuspendTime):
                self.sleep(0.1)

        return fRc == 0

    def _waitKeyboardInterrupt(self):
        """
        Idle loop until user press CTRL+C
        """
        reporter.log('[LOCAL CATCH]: waiting for keyboard interrupt')
        while (True):
            try:
                self.sleep(1)
            except KeyboardInterrupt:
                reporter.log('[LOCAL CATCH]: keyboard interrupt occurred')
                break

    def actionExecute(self):
        """
        Execute the testcase itself.
        """
        #self.logVmInfo(self.oVM)

        reporter.testStart('SHUTDOWN GUEST')

        cIter = 0
        fRc = True

        if (self.fExtraVm is True):
            oExtraSession, oExtraTxsSession = self.startVmAndConnectToTxsViaTcp(self.sExtraVmName,
                                                                                fCdWait=False,
                                                                                cMsTimeout=self.kcMsVmStartLimit)
            if oExtraSession is None or oExtraTxsSession is None:
                reporter.error('Unable to start extra VM.')
                if (self.fLocalCatch is True):
                    self._waitKeyboardInterrupt()
                reporter.testDone()
                return False

        while (cIter < self.cShutdownIters):

            cIter += 1

            reporter.log("Starting iteration #%d." % cIter)

            oSession, oTxsSession = self.startVmAndConnectToTxsViaTcp(self.sVmName,
                                                                      fCdWait=False,
                                                                      cMsTimeout=self.kcMsVmStartLimit)
            if oSession is not None and oTxsSession is not None:
                # Wait until guest reported success
                reporter.log('Guest started. Connection to TXS service established.')

                if (self.fSuspendHost is True):
                    reporter.log("Disconnect form TXS.")
                    fRc = fRc and self.txsDisconnect(oSession, oTxsSession)
                    if (fRc is not True):
                        reporter.log("Disconnect form TXS failed.")
                    else:
                        reporter.log('Put host to sleep and resume it automatically after %d seconds.' % self.cSecSuspendTime)
                        fRc = fRc and self._SuspendResume(self.cSecSuspendTime)
                        if (fRc is True):
                            reporter.log("Sleep/resume success.")
                        else:
                            reporter.log("Sleep/resume failed.")
                        reporter.log("Re-connect to TXS in 10 seconds.")
                        self.sleep(10)
                        (fRc, oTxsSession) = self.txsDoConnectViaTcp(oSession, 2 * 60 * 10000)
                        if (fRc is not True):
                            reporter.log("Re-connect to TXS failed.")

                if (fRc is True):
                    reporter.log('Attempt to shutdown guest.')
                    fRc = fRc and oTxsSession.syncShutdown(cMsTimeout=(4 * 60 * 1000))
                    if (fRc is True):
                        reporter.log('Shutdown request issued successfully.')
                        self.waitOnDirectSessionClose(self.oVM, self.kcMsVmShutdownLimit)
                        reporter.log('Shutdown %s.' % ('success' if fRc is True else 'failed'))
                    else:
                        reporter.error('Shutdown request failed.')

                    # Do not terminate failing VM in order to catch it.
                    if (fRc is not True and self.fLocalCatch is True):
                        self._waitKeyboardInterrupt()
                        break

                    fRc = fRc and self.terminateVmBySession(oSession)
                    reporter.log('VM terminated.')

            else:
                reporter.error('Guest did not start (iteration %d of %d)' % (cIter, self.cShutdownIters))
                fRc = False

            # Stop if fail
            if (fRc is not True):
                break

        # Local catch at the end.
        if (self.fLocalCatch is True):
            reporter.log("Test completed. Waiting for user to press CTRL+C.")
            self._waitKeyboardInterrupt()

        if (self.fExtraVm is True):
            fRc = fRc and  self.terminateVmBySession(oExtraSession)

        reporter.testDone()
        return fRc is True

if __name__ == '__main__':
    sys.exit(tdGuestOsBootTest1().main(sys.argv))
