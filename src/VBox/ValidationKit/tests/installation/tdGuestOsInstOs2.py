#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: tdGuestOsInstOs2.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
VirtualBox Validation Kit - OS/2 install tests.
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


class tdGuestOsInstOs2(vbox.TestDriver):
    """
    OS/2 unattended installation.

    Scenario:
        - Create new VM that corresponds specified installation ISO image
        - Create HDD that corresponds to OS type that will be installed
        - Set VM boot order: HDD, Floppy, ISO
        - Start VM: sinse there is no OS installed on HDD, VM will booted from floppy
        - After first reboot VM will continue installation from HDD automatically
        - Wait for incomming TCP connection (guest should initiate such a
          connection in case installation has been completed successfully)
    """

    ksSataController = 'SATA Controller'
    ksIdeController  = 'IDE Controller'

    # VM parameters required to run ISO image.
    # Format: (cBytesHdd, sKind)
    kaoVmParams = {
        'acp2-txs.iso': ( 2*1024*1024*1024, 'OS2', ksIdeController ),
        'mcp2-txs.iso': ( 2*1024*1024*1024, 'OS2', ksIdeController ),
    }

    def __init__(self):
        """
        Reinitialize child class instance.
        """
        vbox.TestDriver.__init__(self)

        self.sVmName             = 'TestVM'
        self.sHddName            = 'TestHdd.vdi'
        self.sIso                = None
        self.sFloppy             = None
        self.sIsoPathBase        = os.path.join(self.sResourcePath, '4.2', 'isos')
        self.fEnableIOAPIC       = True
        self.cCpus               = 1
        self.fEnableNestedPaging = True
        self.fEnablePAE          = False
        self.asExtraData         = []

    #
    # Overridden methods.
    #

    def showUsage(self):
        """
        Extend usage info
        """
        rc = vbox.TestDriver.showUsage(self)
        reporter.log('  --install-iso <ISO file name>')
        reporter.log('  --cpus <# CPUs>')
        reporter.log('  --no-ioapic')
        reporter.log('  --no-nested-paging')
        reporter.log('  --pae')
        reporter.log('  --set-extradata <key>:value')
        reporter.log('      Set VM extra data. This command line option might be used multiple times.')
        return rc

    def parseOption(self, asArgs, iArg):
        """
        Extend standard options set
        """
        if asArgs[iArg] == '--install-iso':
            iArg += 1
            if iArg >= len(asArgs): raise base.InvalidOption('The "--install-iso" option requires an argument')
            self.sIso = asArgs[iArg]
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
        elif asArgs[iArg] == '--extra-mem':
            self.fEnablePAE = True
        elif asArgs[iArg] == '--set-extradata':
            iArg = self.requireMoreArgs(1, asArgs, iArg)
            self.asExtraData.append(asArgs[iArg])
        else:
            return vbox.TestDriver.parseOption(self, asArgs, iArg)

        return iArg + 1

    def actionConfig(self):
        """
        Configure pre-conditions.
        """

        if not self.importVBoxApi():
            return False

        assert self.sIso is not None
        if self.sIso not in self.kaoVmParams:
            reporter.log('Error: unknown ISO image specified: %s' % self.sIso)
            return False

        # Get VM params specific to ISO image
        cBytesHdd, sKind, sController = self.kaoVmParams[self.sIso]

        # Create VM itself
        eNic0AttachType = vboxcon.NetworkAttachmentType_NAT
        self.sIso = os.path.join(self.sIsoPathBase, self.sIso)
        assert os.path.isfile(self.sIso)

        self.sFloppy = os.path.join(self.sIsoPathBase, os.path.splitext(self.sIso)[0] + '.img')

        oVM = self.createTestVM(self.sVmName, 1, sKind = sKind, sDvdImage = self.sIso, cCpus = self.cCpus,
                                sFloppy = self.sFloppy, eNic0AttachType = eNic0AttachType)
        assert oVM is not None

        oSession = self.openSession(oVM)

        # Create HDD
        sHddPath = os.path.join(self.sScratchPath, self.sHddName)
        fRc = True
        if sController == self.ksSataController:
            fRc = oSession.setStorageControllerType(vboxcon.StorageControllerType_IntelAhci, sController)

        fRc = fRc and oSession.createAndAttachHd(sHddPath, cb = cBytesHdd,
                                                 sController = sController, iPort = 0, fImmutable=False)
        if sController == self.ksSataController:
            fRc = fRc and oSession.setStorageControllerPortCount(sController, 1)

        # Set proper boot order
        fRc = fRc and oSession.setBootOrder(1, vboxcon.DeviceType_HardDisk)
        fRc = fRc and oSession.setBootOrder(2, vboxcon.DeviceType_Floppy)

        # Enable HW virt
        fRc = fRc and oSession.enableVirtExX86(True)

        # Enable I/O APIC
        fRc = fRc and oSession.enableIoApic(self.fEnableIOAPIC)

        # Enable Nested Paging
        fRc = fRc and oSession.enableNestedPagingX86(self.fEnableNestedPaging)

        # Enable PAE
        fRc = fRc and oSession.enablePaeX86(self.fEnablePAE)

        # Remote desktop
        oSession.setupVrdp(True)

        # Set extra data
        if self.asExtraData:
            for sExtraData in self.asExtraData:
                try:
                    sKey, sValue = sExtraData.split(':')
                except ValueError:
                    raise base.InvalidOption('Invalid extradata specified: %s' % sExtraData)
                reporter.log('Set extradata: %s => %s' % (sKey, sValue))
                fRc = fRc and oSession.setExtraData(sKey, sValue)

        fRc = fRc and oSession.saveSettings()
        fRc = oSession.close()
        assert fRc is True

        return vbox.TestDriver.actionConfig(self)

    def actionExecute(self):
        """
        Execute the testcase itself.
        """
        if not self.importVBoxApi():
            return False
        return self.testDoInstallGuestOs()

    #
    # Test execution helpers.
    #

    def testDoInstallGuestOs(self):
        """
        Install guest OS and wait for result
        """
        reporter.testStart('Installing %s' % (os.path.basename(self.sIso),))

        cMsTimeout = 40*60000;
        if not reporter.isLocal(): ## @todo need to figure a better way of handling timeouts on the testboxes ...
            cMsTimeout = 180 * 60000; # will be adjusted down.

        oSession, _ = self.startVmAndConnectToTxsViaTcp(self.sVmName, fCdWait = False, cMsTimeout = cMsTimeout)
        if oSession is not None:
            # Wait until guest reported success
            reporter.log('Guest reported success')
            reporter.testDone()
            fRc = self.terminateVmBySession(oSession)
            return fRc is True
        reporter.error('Installation of %s has failed' % (self.sIso,))
        reporter.testDone()
        return False

if __name__ == '__main__':
    sys.exit(tdGuestOsInstOs2().main(sys.argv));

