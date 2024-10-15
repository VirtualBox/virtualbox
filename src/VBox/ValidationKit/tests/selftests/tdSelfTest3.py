#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: tdSelfTest3.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager / Suite Self Test #3 - Bad XML input and other Failures.
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
from common             import utils;
from testdriver         import reporter;
from testdriver.base    import TestDriverBase;


class tdSelfTest3(TestDriverBase):
    """
    Test Manager / Suite Self Test #3 - Bad XML input and other Failures.
    """

    def __init__(self):
        TestDriverBase.__init__(self);


    def actionExecute(self):

        # Testing PushHint/PopHint.
        reporter.testStart('Negative XML #1');
        oSubXmlFile = reporter.FileWrapperTestPipe();
        oSubXmlFile.write('<Test timestamp="%s" name="foobar3">\n\n\t\n\r\n' % (utils.getIsoTimestamp(),));
        oSubXmlFile.write('<Test timestamp="%s" name="sub3">' % (utils.getIsoTimestamp(),));
        oSubXmlFile.write('<Test timestamp="%s" name="subsub1">' % (utils.getIsoTimestamp(),));
        oSubXmlFile.close();
        reporter.testDone();

        # Missing end, like we had with IRPT at one time.
        reporter.testStart('Negative XML #2 (IPRT)');
        oSubXmlFile = reporter.FileWrapperTestPipe();
        oSubXmlFile.write("""
<?xml version="1.0" encoding="UTF-8" ?>
<Test timestamp="2013-05-29T08:59:05.930602000Z" name="tstRTGetOpt">
  <Test timestamp="2013-05-29T08:59:05.930656000Z" name="Basics">
    <Passed timestamp="2013-05-29T08:59:05.930756000Z"/>
  </Test>
  <Test timestamp="2013-05-29T08:59:05.930995000Z" name="RTGetOpt - IPv4">
    <Passed timestamp="2013-05-29T08:59:05.931036000Z"/>
  </Test>
  <Test timestamp="2013-05-29T08:59:05.931161000Z" name="RTGetOpt - MAC Address">
    <Passed timestamp="2013-05-29T08:59:05.931194000Z"/>
  </Test>
  <Test timestamp="2013-05-29T08:59:05.931313000Z" name="RTGetOpt - Option w/ Index">
    <Passed timestamp="2013-05-29T08:59:05.931357000Z"/>
  </Test>
  <Test timestamp="2013-05-29T08:59:05.931475000Z" name="RTGetOptFetchValue">
    <Passed timestamp="2013-05-29T08:59:05.931516000Z"/>
  </Test>
  <Test timestamp="2013-05-29T08:59:05.931640000Z" name="RTGetOpt - bool on/off">
    <Passed timestamp="2013-05-29T08:59:05.931687000Z"/>
  </Test>
  <Test timestamp="2013-05-29T08:59:05.931807000Z" name="Standard options">
    <Passed timestamp="2013-05-29T08:59:05.931843000Z"/>
  </Test>
  <Test timestamp="2013-05-29T08:59:05.931963000Z" name="Options first">
    <Passed timestamp="2013-05-29T08:59:05.932035000Z"/>
  </Test>
""");
        oSubXmlFile.close();
        oSubXmlFile = None;
        reporter.testDone();

        # The use of testFailure.
        reporter.testStart('Using testFailure()');
        reporter.testValue('value-name3',  12345678, 'times');
        reporter.testFailure('failure detail message');
        reporter.testDone();

        return True;


if __name__ == '__main__':
    sys.exit(tdSelfTest3().main(sys.argv));

