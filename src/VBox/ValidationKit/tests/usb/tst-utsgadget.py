# -*- coding: utf-8 -*-
# $Id: tst-utsgadget.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Simple testcase for usbgadget2.py.
"""

__copyright__ = \
"""
Copyright (C) 2016-2024 Oracle and/or its affiliates.

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

# Standard python imports.
import sys

# Validation Kit imports.
sys.path.insert(0, '.');
sys.path.insert(0, '..');
sys.path.insert(0, '../..');
from common import utils;
from testdriver import reporter;
import usbgadget;


# Python 3 hacks:
if sys.version_info[0] >= 3:
    long = int;     # pylint: disable=redefined-builtin,invalid-name


g_cTests = 0;
g_cFailures = 0

def boolRes(rc, fExpect = True):
    """Checks a boolean result."""
    global g_cTests, g_cFailures;
    g_cTests = g_cTests + 1;
    if isinstance(rc, bool):
        if rc == fExpect:
            return 'PASSED';
    g_cFailures = g_cFailures + 1;
    return 'FAILED';

def stringRes(rc, sExpect):
    """Checks a string result."""
    global g_cTests, g_cFailures;
    g_cTests = g_cTests + 1;
    if utils.isString(rc):
        if rc == sExpect:
            return 'PASSED';
    g_cFailures = g_cFailures + 1;
    return 'FAILED';

def main(asArgs): # pylint: disable=missing-docstring,too-many-locals,too-many-statements
    cMsTimeout      = long(30*1000);
    sAddress        = 'localhost';
    uPort           = None;
    fStdTests       = True;

    i = 1;
    while i < len(asArgs):
        if asArgs[i] == '--hostname':
            sAddress = asArgs[i + 1];
            i = i + 2;
        elif asArgs[i] == '--port':
            uPort = int(asArgs[i + 1]);
            i = i + 2;
        elif asArgs[i] == '--timeout':
            cMsTimeout = long(asArgs[i + 1]);
            i = i + 2;
        elif asArgs[i] == '--help':
            print('tst-utsgadget.py [--hostname <addr|name>] [--port <num>] [--timeout <cMS>]');
            return 0;
        else:
            print('Unknown argument: %s' % (asArgs[i],));
            return 2;

    oGadget = usbgadget.UsbGadget();
    if uPort is None:
        rc = oGadget.connectTo(cMsTimeout, sAddress);
    else:
        rc = oGadget.connectTo(cMsTimeout, sAddress, uPort = uPort);
    if rc is False:
        print('connectTo failed');
        return 1;

    if fStdTests:
        rc = oGadget.getUsbIpPort() is not None;
        print('%s: getUsbIpPort() -> %s' % (boolRes(rc), oGadget.getUsbIpPort(),));

        rc = oGadget.impersonate(usbgadget.g_ksGadgetImpersonationTest);
        print('%s: impersonate()' % (boolRes(rc),));

        rc = oGadget.disconnectUsb();
        print('%s: disconnectUsb()' % (boolRes(rc),));

        rc = oGadget.connectUsb();
        print('%s: connectUsb()' % (boolRes(rc),));

        rc = oGadget.clearImpersonation();
        print('%s: clearImpersonation()' % (boolRes(rc),));

        # Test super speed (and therefore passing configuration items)
        rc = oGadget.impersonate(usbgadget.g_ksGadgetImpersonationTest, True);
        print('%s: impersonate(, True)' % (boolRes(rc),));

        rc = oGadget.clearImpersonation();
        print('%s: clearImpersonation()' % (boolRes(rc),));

        # Done
        rc = oGadget.disconnectFrom();
        print('%s: disconnectFrom() -> %s' % (boolRes(rc), rc,));

    if g_cFailures != 0:
        print('tst-utsgadget.py: %u out of %u test failed' % (g_cFailures, g_cTests,));
        return 1;
    print('tst-utsgadget.py: all %u tests passed!' % (g_cTests,));
    return 0;


if __name__ == '__main__':
    reporter.incVerbosity();
    reporter.incVerbosity();
    reporter.incVerbosity();
    reporter.incVerbosity();
    sys.exit(main(sys.argv));

