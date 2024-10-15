#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: tdSmokeTest1.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
VirtualBox Validation Kit - Smoke Test #1.
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
import sys;

# Only the main script needs to modify the path.
try:    __file__                            # pylint: disable=used-before-assignment
except: __file__ = sys.argv[0];
g_ksValidationKitDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))));
sys.path.append(g_ksValidationKitDir);

# Validation Kit imports.
from testdriver import reporter;
from testdriver import base;
from testdriver import vbox;
from testdriver import vboxcon;


class tdSmokeTest1(vbox.TestDriver):
    """
    VBox Smoke Test #1.
    """

    def __init__(self):
        vbox.TestDriver.__init__(self);
        self.asRsrcs            = None;
        self.oTestVmSet         = self.oTestVmManager.getSmokeVmSet();
        self.sNicAttachmentDef  = 'mixed';
        self.sNicAttachment     = self.sNicAttachmentDef;
        self.fQuick             = False;

    #
    # Overridden methods.
    #
    def showUsage(self):
        rc = vbox.TestDriver.showUsage(self);
        reporter.log('');
        reporter.log('Smoke Test #1 options:');
        reporter.log('  --nic-attachment <bridged|nat|mixed>');
        reporter.log('      Default: %s' % (self.sNicAttachmentDef));
        reporter.log('  --quick');
        reporter.log('      Very selective testing.')
        return rc;

    def parseOption(self, asArgs, iArg):
        if asArgs[iArg] == '--nic-attachment':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--nic-attachment" takes an argument');
            self.sNicAttachment = asArgs[iArg];
            if self.sNicAttachment not in ('bridged', 'nat', 'mixed'):
                raise base.InvalidOption('The "--nic-attachment" value "%s" is not supported. Valid values are: bridged, nat' \
                        % (self.sNicAttachment));
        elif asArgs[iArg] == '--quick':
            # Disable all but a few VMs and configurations.
            for oTestVm in self.oTestVmSet.aoTestVms:
                if oTestVm.sVmName == 'tst-win2k3ent':          # 32-bit paging
                    oTestVm.asVirtModesSup  = [ 'hwvirt' ];
                    oTestVm.acCpusSup       = range(1, 2);
                elif oTestVm.sVmName == 'tst-rhel5':            # 32-bit paging
                    oTestVm.asVirtModesSup  = [ 'raw' ];
                    oTestVm.acCpusSup       = range(1, 2);
                elif oTestVm.sVmName == 'tst-win2k8':           # 64-bit
                    oTestVm.asVirtModesSup  = [ 'hwvirt-np' ];
                    oTestVm.acCpusSup       = range(1, 2);
                elif oTestVm.sVmName == 'tst-sol10-64':         # SMP, 64-bit
                    oTestVm.asVirtModesSup  = [ 'hwvirt' ];
                    oTestVm.acCpusSup       = range(2, 3);
                elif oTestVm.sVmName == 'tst-sol10':            # SMP, 32-bit
                    oTestVm.asVirtModesSup  = [ 'hwvirt-np' ];
                    oTestVm.acCpusSup       = range(2, 3);
                elif oTestVm.sVmName == 'tst-nsthwvirt-ubuntu-64':  # Nested hw.virt, 64-bit
                    oTestVm.asVirtModesSup  = [ 'hwvirt-np' ];
                    oTestVm.acCpusSup       = range(1, 2);
                else:
                    oTestVm.fSkip = True;
        else:
            return vbox.TestDriver.parseOption(self, asArgs, iArg);
        return iArg + 1;

    def actionVerify(self):
        if self.sVBoxValidationKitIso is None or not os.path.isfile(self.sVBoxValidationKitIso):
            reporter.error('Cannot find the VBoxValidationKit.iso! (%s)'
                           'Please unzip a Validation Kit build in the current directory or in some parent one.'
                           % (self.sVBoxValidationKitIso,) );
            return False;
        return vbox.TestDriver.actionVerify(self);

    def actionConfig(self):
        # Make sure vboxapi has been imported so we can use the constants.
        if not self.importVBoxApi():
            return False;

        # Do the configuring.
        if self.sNicAttachment == 'nat':        eNic0AttachType = vboxcon.NetworkAttachmentType_NAT;
        elif self.sNicAttachment == 'bridged':  eNic0AttachType = vboxcon.NetworkAttachmentType_Bridged;
        else:                                   eNic0AttachType = None;
        assert self.sVBoxValidationKitIso is not None;
        return self.oTestVmSet.actionConfig(self, eNic0AttachType = eNic0AttachType, sDvdImage = self.sVBoxValidationKitIso);

    def actionExecute(self):
        """
        Execute the testcase.
        """
        return self.oTestVmSet.actionExecute(self, self.testOneVmConfig)


    #
    # Test execution helpers.
    #

    def testOneVmConfig(self, oVM, oTestVm):
        """
        Runs the specified VM thru test #1.
        """

        # Simple test.
        self.logVmInfo(oVM);
        # Try waiting for a bit longer (15 minutes) until the CD is available to avoid running into timeouts.
        oSession, oTxsSession = self.startVmAndConnectToTxsViaTcp(oTestVm.sVmName, fCdWait = True, cMsCdWait = 15 * 60 * 1000);
        if oSession is not None:
            self.addTask(oTxsSession);

            ## @todo do some quick tests: save, restore, execute some test program, shut down the guest.

            # cleanup.
            self.removeTask(oTxsSession);
            self.terminateVmBySession(oSession)
            return True;
        return None;

if __name__ == '__main__':
    sys.exit(tdSmokeTest1().main(sys.argv));
