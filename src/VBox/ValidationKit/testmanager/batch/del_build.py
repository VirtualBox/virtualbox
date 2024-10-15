#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: del_build.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
# pylint: disable=line-too-long

"""
Interface used by the tinderbox server side software to mark build binaries
deleted.
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

# Standard python imports
import sys
import os
from optparse import OptionParser;  # pylint: disable=deprecated-module

# Add Test Manager's modules path
g_ksTestManagerDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.append(g_ksTestManagerDir)

# Test Manager imports
from testmanager.core.db    import TMDatabaseConnection
from testmanager.core.build import BuildLogic


def markBuildsDeleted():
    """
    Marks the builds using the specified binaries as deleted.
    """

    oParser = OptionParser()
    oParser.add_option('-q', '--quiet', dest='fQuiet', action='store_true',
                       help='Quiet execution');

    (oConfig, asArgs) = oParser.parse_args()
    if not asArgs:
        if not oConfig.fQuiet:
            sys.stderr.write('syntax error: No builds binaries specified\n');
        return 1;


    oDb = TMDatabaseConnection()
    oLogic = BuildLogic(oDb)

    for sBuildBin in asArgs:
        try:
            cBuilds = oLogic.markDeletedByBinaries(sBuildBin, fCommit = True)
        except:
            if oConfig.fQuiet:
                sys.exit(1);
            raise;
        if not oConfig.fQuiet:
            print("del_build.py: Marked %u builds associated with '%s' as deleted." % (cBuilds, sBuildBin,));

    oDb.close()
    return 0;

if __name__ == '__main__':
    sys.exit(markBuildsDeleted())

