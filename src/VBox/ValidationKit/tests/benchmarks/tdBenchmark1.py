#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: tdBenchmark1.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
VirtualBox Validation Kit - Test that runs various benchmarks.
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
from testdriver import vbox;
from testdriver import vboxtestvms;


class tdBenchmark1(vbox.TestDriver):
    """
    Benchmark #1.
    """

    def __init__(self):
        vbox.TestDriver.__init__(self);
        oTestVm = vboxtestvms.BootSectorTestVm(self.oTestVmSet, 'tst-bs-test1',
                                               os.path.join(self.sVBoxBootSectors, 'bootsector2-test1.img'));
        self.oTestVmSet.aoTestVms.append(oTestVm);


    #
    # Overridden methods.
    #


    def actionConfig(self):
        self._detectValidationKit();
        return self.oTestVmSet.actionConfig(self);

    def actionExecute(self):
        return self.oTestVmSet.actionExecute(self, self.testOneCfg);



    #
    # Test execution helpers.
    #

    def testOneCfg(self, oVM, oTestVm):
        """
        Runs the specified VM thru the tests.

        Returns a success indicator on the general test execution. This is not
        the actual test result.
        """
        fRc = False;

        sXmlFile = self.prepareResultFile();
        asEnv = [ 'IPRT_TEST_FILE=' + sXmlFile];

        self.logVmInfo(oVM);
        oSession = self.startVm(oVM, sName = oTestVm.sVmName, asEnv = asEnv);
        if oSession is not None:
            cMsTimeout = 15*60*1000;
            if not reporter.isLocal(): ## @todo need to figure a better way of handling timeouts on the testboxes ...
                cMsTimeout = self.adjustTimeoutMs(180 * 60000);

            oRc = self.waitForTasks(cMsTimeout);
            if oRc == oSession:
                fRc = oSession.assertPoweredOff();
            else:
                reporter.error('oRc=%s, expected %s' % (oRc, oSession));

            reporter.addSubXmlFile(sXmlFile);
            self.terminateVmBySession(oSession);
        return fRc;



if __name__ == '__main__':
    sys.exit(tdBenchmark1().main(sys.argv));

