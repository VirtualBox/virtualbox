#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: tdSelfTest4.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager / Suite Self Test #4 - Testing result overflow handling.
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
from testdriver         import reporter;
from testdriver.base    import TestDriverBase, InvalidOption;


class tdSelfTest4(TestDriverBase):
    """
    Test Manager / Suite Self Test #4 - Testing result overflow handling.
    """

    ## Valid tests.
    kasValidTests = [ 'immediate-sub-tests', 'total-sub-tests', 'immediate-values', 'total-values', 'immediate-messages'];

    def __init__(self):
        TestDriverBase.__init__(self);
        self.sOptWhich = 'immediate-sub-tests';

    def parseOption(self, asArgs, iArg):
        if asArgs[iArg] == '--test':
            iArg = self.requireMoreArgs(1, asArgs, iArg);
            if asArgs[iArg] not in self.kasValidTests:
                raise InvalidOption('Invalid test name "%s". Must be one of: %s'
                                    % (asArgs[iArg], ', '.join(self.kasValidTests),));
            self.sOptWhich = asArgs[iArg];
        else:
            return TestDriverBase.parseOption(self, asArgs, iArg);
        return iArg + 1;

    def actionExecute(self):
        # Too many immediate sub-tests.
        if self.sOptWhich == 'immediate-sub-tests':
            reporter.testStart('Too many immediate sub-tests (negative)');
            for i in range(1024):
                reporter.testStart('subsub%d' % i);
                reporter.testDone();
        # Too many sub-tests in total.
        elif self.sOptWhich == 'total-sub-tests':
            reporter.testStart('Too many sub-tests (negative)');
            # 32 * 256 = 2^(5+8) = 2^13 = 8192.
            for i in range(32):
                reporter.testStart('subsub%d' % i);
                for j in range(256):
                    reporter.testStart('subsubsub%d' % j);
                    reporter.testDone();
                reporter.testDone();
        # Too many immediate values.
        elif self.sOptWhich == 'immediate-values':
            reporter.testStart('Too many immediate values (negative)');
            for i in range(512):
                reporter.testValue('value%d' % i, i, 'times');
        # Too many values in total.
        elif self.sOptWhich == 'total-values':
            reporter.testStart('Too many sub-tests (negative)');
            for i in range(256):
                reporter.testStart('subsub%d' % i);
                for j in range(64):
                    reporter.testValue('value%d' % j, i * 10000 + j, 'times');
                reporter.testDone();
        # Too many failure reasons (only immediate since the limit is extremely low).
        elif self.sOptWhich == 'immediate-messages':
            reporter.testStart('Too many immediate messages (negative)');
            for i in range(16):
                reporter.testFailure('Detail %d' % i);
        else:
            reporter.testStart('Unknown test %s' % (self.sOptWhich,));
            reporter.error('Invalid test selected: %s' % (self.sOptWhich,));
        reporter.testDone();
        return True;


if __name__ == '__main__':
    sys.exit(tdSelfTest4().main(sys.argv));

