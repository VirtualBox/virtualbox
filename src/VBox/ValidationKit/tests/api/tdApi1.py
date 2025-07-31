#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: tdApi1.py 110219 2025-07-14 20:46:18Z brent.paulson@oracle.com $

"""
VirtualBox Validation Kit - API Test wrapper #1 combining all API sub-tests
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
__version__ = "$Revision: 110219 $"


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


class tdApi1(vbox.TestDriver):
    """
    API Test wrapper #1.
    """

    def __init__(self, aoSubTestDriverClasses = None):
        vbox.TestDriver.__init__(self)
        for oSubTestDriverClass in aoSubTestDriverClasses:
            self.addSubTestDriver(oSubTestDriverClass(self));

    #
    # Overridden methods.
    #

    def actionConfig(self):
        """
        Import the API.
        """
        if not self.importVBoxApi():
            return False
        return True

    def actionExecute(self):
        """
        Execute the testcase, i.e. all sub-tests.
        """
        fRc = True;
        for oSubTstDrv in self.aoSubTstDrvs:
            if oSubTstDrv.fEnabled:
                fRc = oSubTstDrv.testIt() and fRc;
        return fRc;


if __name__ == '__main__':
    sys.path.append(os.path.dirname(os.path.abspath(__file__)))
    from tdPython1       import SubTstDrvPython1;     # pylint: disable=relative-import
    from tdAppliance1    import SubTstDrvAppliance1;  # pylint: disable=relative-import
    from tdMoveMedium1   import SubTstDrvMoveMedium1; # pylint: disable=relative-import
    from tdTreeDepth1    import SubTstDrvTreeDepth1;  # pylint: disable=relative-import
    from tdMoveVm1       import SubTstDrvMoveVm1;     # pylint: disable=relative-import
    from tdCloneMedium1  import SubTstDrvCloneMedium1;# pylint: disable=relative-import
    from tdSnapshots1    import SubTstDrvNestedLiveSnapshots1;# pylint: disable=relative-import
    from tdImportExport1 import SubTstDrvImportExportEFIVM1;  # pylint: disable=relative-import
    sys.exit(tdApi1([SubTstDrvPython1, SubTstDrvAppliance1, SubTstDrvMoveMedium1,
                     SubTstDrvTreeDepth1, SubTstDrvMoveVm1, SubTstDrvCloneMedium1,
                     SubTstDrvNestedLiveSnapshots1, SubTstDrvImportExportEFIVM1]).main(sys.argv))
