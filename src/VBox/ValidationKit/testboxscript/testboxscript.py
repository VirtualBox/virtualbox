#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: testboxscript.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
TestBox Script Wrapper.

This script aimes at respawning the Test Box Script when it terminates
abnormally or due to an UPGRADE request.
"""

from __future__ import print_function;

__copyright__ = \
"""
Copyright (C) 2012-2024 Oracle and/or its affiliates.

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

import platform;
import subprocess;
import sys;
import os;
import time;


## @name Test Box script exit statuses (see also RTEXITCODE)
# @remarks These will _never_ change
# @{
TBS_EXITCODE_FAILURE        = 1;        # RTEXITCODE_FAILURE
TBS_EXITCODE_SYNTAX         = 2;        # RTEXITCODE_SYNTAX
TBS_EXITCODE_NEED_UPGRADE   = 9;
## @}


class TestBoxScriptWrapper(object): # pylint: disable=too-few-public-methods
    """
    Wrapper class
    """

    TESTBOX_SCRIPT_FILENAME = 'testboxscript_real.py'

    def __init__(self):
        """
        Init
        """
        self.oTask = None

    def __del__(self):
        """
        Cleanup
        """
        if self.oTask is not None:
            print('Wait for child task...');
            self.oTask.terminate()
            self.oTask.wait()
            print('done. Exiting');
            self.oTask = None;

    def run(self):
        """
        Start spawning the real TestBox script.
        """

        # Figure out where we live first.
        try:
            __file__                        # pylint: disable=used-before-assignment
        except:
            __file__ = sys.argv[0];
        sTestBoxScriptDir = os.path.dirname(os.path.abspath(__file__));

        # Construct the argument list for the real script (same dir).
        sRealScript = os.path.join(sTestBoxScriptDir, TestBoxScriptWrapper.TESTBOX_SCRIPT_FILENAME);
        asArgs = sys.argv[1:];
        asArgs.insert(0, sRealScript);
        if sys.executable:
            asArgs.insert(0, sys.executable);

        # Look for --pidfile <name> and write a pid file.
        sPidFile = None;
        for i, _ in enumerate(asArgs):
            if asArgs[i] == '--pidfile' and i + 1 < len(asArgs):
                sPidFile = asArgs[i + 1];
                break;
            if asArgs[i] == '--':
                break;
        if sPidFile:
            with open(sPidFile, 'w') as oPidFile:   # pylint: disable=unspecified-encoding
                oPidFile.write(str(os.getpid()));

        # Execute the testbox script almost forever in a relaxed loop.
        rcExit = TBS_EXITCODE_FAILURE;
        while True:
            fCreationFlags = 0;
            if platform.system() == 'Windows':
                fCreationFlags = getattr(subprocess, 'CREATE_NEW_PROCESS_GROUP', 0x00000200); # for Ctrl-C isolation (python 2.7)
            self.oTask = subprocess.Popen(asArgs, shell = False,           # pylint: disable=consider-using-with
                                          creationflags = fCreationFlags);
            rcExit = self.oTask.wait();
            self.oTask = None;
            if rcExit == TBS_EXITCODE_SYNTAX:
                break;

            # Relax.
            time.sleep(1);
        return rcExit;

if __name__ == '__main__':
    sys.exit(TestBoxScriptWrapper().run());

