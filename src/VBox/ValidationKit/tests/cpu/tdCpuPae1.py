#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: tdCpuPae1.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
VirtualBox Validation Kit - Catch PAE not enabled.

Test that switching into PAE mode when it isn't enable, check that it produces
the right runtime error.
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


class tdCpuPae1ConsoleCallbacks(vbox.ConsoleEventHandlerBase):
    """
    For catching the PAE runtime error.
    """
    def __init__(self, dArgs):
        oTstDrv  = dArgs['oTstDrv'];
        oVBoxMgr = dArgs['oVBoxMgr']; _ = oVBoxMgr;

        vbox.ConsoleEventHandlerBase.__init__(self, dArgs, 'tdCpuPae1');
        self.oTstDrv  = oTstDrv;

    def onRuntimeError(self, fFatal, sErrId, sMessage):
        """ Verify the error. """
        reporter.log('onRuntimeError: fFatal=%s sErrId="%s" sMessage="%s"' % (fFatal, sErrId, sMessage));
        if sErrId != 'PAEmode':
            reporter.testFailure('sErrId=%s, expected PAEmode' % (sErrId,));
        elif fFatal is not True:
            reporter.testFailure('fFatal=%s, expected True' % (fFatal,));
        else:
            self.oTstDrv.fCallbackSuccess = True;
        self.oTstDrv.fCallbackFired = True;
        self.oVBoxMgr.interruptWaitEvents();
        return None;


class tdCpuPae1(vbox.TestDriver):
    """
    PAE Test #1.
    """

    def __init__(self):
        vbox.TestDriver.__init__(self);
        self.asSkipTests        = [];
        self.asVirtModesDef     = ['hwvirt', 'hwvirt-np', 'raw',]
        self.asVirtModes        = self.asVirtModesDef
        self.acCpusDef          = [1, 2,]
        self.acCpus             = self.acCpusDef;
        self.fCallbackFired     = False;
        self.fCallbackSuccess   = False;

    #
    # Overridden methods.
    #
    def showUsage(self):
        rc = vbox.TestDriver.showUsage(self);
        reporter.log('');
        reporter.log('tdCpuPae1 Options:');
        reporter.log('  --virt-modes   <m1[:m2[:]]');
        reporter.log('      Default: %s' % (':'.join(self.asVirtModesDef)));
        reporter.log('  --cpu-counts   <c1[:c2[:]]');
        reporter.log('      Default: %s' % (':'.join(str(c) for c in self.acCpusDef)));
        reporter.log('  --quick');
        reporter.log('      Shorthand for: --virt-modes raw --cpu-counts 1 32');
        return rc;

    def parseOption(self, asArgs, iArg):
        if asArgs[iArg] == '--virt-modes':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--virt-modes" takes a colon separated list of modes');
            self.asVirtModes = asArgs[iArg].split(':');
            for s in self.asVirtModes:
                if s not in self.asVirtModesDef:
                    raise base.InvalidOption('The "--virt-modes" value "%s" is not valid; valid values are: %s' \
                        % (s, ' '.join(self.asVirtModesDef)));
        elif asArgs[iArg] == '--cpu-counts':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--cpu-counts" takes a colon separated list of cpu counts');
            self.acCpus = [];
            for s in asArgs[iArg].split(':'):
                try: c = int(s);
                except: raise base.InvalidOption('The "--cpu-counts" value "%s" is not an integer' % (s,));
                if c <= 0:  raise base.InvalidOption('The "--cpu-counts" value "%s" is zero or negative' % (s,));
                self.acCpus.append(c);
        elif asArgs[iArg] == '--quick':
            self.asVirtModes        = ['raw',];
            self.acCpus             = [1,];
        else:
            return vbox.TestDriver.parseOption(self, asArgs, iArg);
        return iArg + 1;

    def getResourceSet(self):
        return [];

    def actionConfig(self):
        # Make sure vboxapi has been imported so we can use the constants.
        if not self.importVBoxApi():
            return False;

        #
        # Configure a VM with the PAE bootsector as floppy image.
        #

        oVM = self.createTestVM('tst-bs-pae', 2, sKind = 'Other', fVirtEx = False, fPae = False, \
                                sFloppy = os.path.join(self.sVBoxBootSectors, 'bootsector-pae.img') );
        if oVM is None:
            return False;
        return True;

    def actionExecute(self):
        """
        Execute the testcase.
        """
        return self.test1();



    #
    # Test execution helpers.
    #

    def test1OneCfg(self, oVM, cCpus, fHwVirt, fNestedPaging):
        """
        Runs the specified VM thru test #1.

        Returns a success indicator on the general test execution. This is not
        the actual test result.
        """

        # Reconfigure the VM
        fRc = True;
        oSession = self.openSession(oVM);
        if oSession is not None:
            fRc = fRc and oSession.enableVirtExX86(fHwVirt);
            fRc = fRc and oSession.enableNestedPagingX86(fNestedPaging);
            fRc = fRc and oSession.setCpuCount(cCpus);
            fRc = fRc and oSession.setupBootLogo(True, 2500); # Race avoidance fudge.
            fRc = fRc and oSession.saveSettings();
            fRc = oSession.close() and fRc and True; # pychecker hack.
            oSession = None;
        else:
            fRc = False;

        # Zap the state (used by the callback).
        self.fCallbackFired = False;
        self.fCallbackSuccess = False;

        # Start up.
        if fRc is True:
            self.logVmInfo(oVM);
            oSession = self.startVm(oVM)
            if oSession is not None:
                # Set up a callback for catching the runtime error. !Races the guest bootup!
                oConsoleCallbacks = oSession.registerDerivedEventHandler(tdCpuPae1ConsoleCallbacks, {'oTstDrv':self,})

                fRc = False;
                if oConsoleCallbacks is not None:
                    # Wait for 30 seconds for something to finish.
                    tsStart = base.timestampMilli();
                    while base.timestampMilli() - tsStart < 30000:
                        oTask = self.waitForTasks(1000);
                        if oTask is not None:
                            break;
                        if self.fCallbackFired:
                            break;
                    if not self.fCallbackFired:
                        reporter.testFailure('the callback did not fire');
                    fRc = self.fCallbackSuccess;

                    # cleanup.
                    oConsoleCallbacks.unregister();
                self.terminateVmBySession(oSession) #, fRc);
            else:
                fRc = False;
        return fRc;


    def test1(self):
        """
        Executes test #1 - Negative API testing.

        ASSUMES that the VMs are
        """
        reporter.testStart('Test 1');
        oVM = self.getVmByName('tst-bs-pae');

        for cCpus in self.acCpus:
            if cCpus == 1:  reporter.testStart('1 cpu');
            else:           reporter.testStart('%u cpus' % (cCpus));

            for sVirtMode in self.asVirtModes:
                if sVirtMode == 'raw' and cCpus > 1:
                    continue;

                hsVirtModeDesc = {};
                hsVirtModeDesc['raw']       = 'Raw-mode';
                hsVirtModeDesc['hwvirt']    = 'HwVirt';
                hsVirtModeDesc['hwvirt-np'] = 'NestedPaging';
                reporter.testStart(hsVirtModeDesc[sVirtMode]);

                fHwVirt       = sVirtMode != 'raw';
                fNestedPaging = sVirtMode == 'hwvirt-np';
                self.test1OneCfg(oVM, cCpus, fHwVirt, fNestedPaging);

                reporter.testDone();
            reporter.testDone();

        return reporter.testDone()[1] == 0;



if __name__ == '__main__':
    sys.exit(tdCpuPae1().main(sys.argv));

