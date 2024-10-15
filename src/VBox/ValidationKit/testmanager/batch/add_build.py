#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: add_build.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
# pylint: disable=line-too-long

"""
Interface used by the tinderbox server side software to add a fresh build.
"""

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
import sys;
import os;
from optparse import OptionParser;  # pylint: disable=deprecated-module

# Add Test Manager's modules path
g_ksTestManagerDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))));
sys.path.append(g_ksTestManagerDir);

# Test Manager imports
from testmanager.core.db    import TMDatabaseConnection;
from testmanager.core.build import BuildDataEx, BuildLogic, BuildCategoryData;

class Build(object): # pylint: disable=too-few-public-methods
    """
    Add build info into Test Manager database.
    """

    def __init__(self):
        """
        Parse command line.
        """

        oParser = OptionParser();
        oParser.add_option('-q', '--quiet', dest = 'fQuiet', action = 'store_true',
                           help = 'Quiet execution');
        oParser.add_option('-b', '--branch', dest = 'sBranch', metavar = '<branch>',
                           help = 'branch name (default: trunk)', default = 'trunk');
        oParser.add_option('-p', '--product', dest = 'sProductName', metavar = '<name>',
                           help = 'The product name.');
        oParser.add_option('-r', '--revision', dest = 'iRevision', metavar = '<rev>',
                           help = 'revision number');
        oParser.add_option('-R', '--repository', dest = 'sRepository', metavar = '<repository>',
                           help = 'Version control repository name.');
        oParser.add_option('-t', '--type', dest = 'sBuildType', metavar = '<type>',
                           help = 'build type (debug, release etc.)');
        oParser.add_option('-v', '--version', dest = 'sProductVersion', metavar = '<ver>',
                           help = 'The product version number (suitable for RTStrVersionCompare)');
        oParser.add_option('-o', '--os-arch', dest = 'asTargetOsArches', metavar = '<os.arch>', action = 'append',
                           help = 'Target OS and architecture. This option can be repeated.');
        oParser.add_option('-l', '--log', dest = 'sBuildLogPath', metavar = '<url>',
                           help = 'URL to the build logs (optional).');
        oParser.add_option('-f', '--file', dest = 'asFiles', metavar = '<file|url>', action = 'append',
                           help = 'URLs or build share relative path to a build output file. This option can be repeated.');

        (self.oConfig, _) = oParser.parse_args();

        # Check command line
        asMissing = [];
        if self.oConfig.sBranch is None:            asMissing.append('--branch');
        if self.oConfig.iRevision is None:          asMissing.append('--revision');
        if self.oConfig.sProductVersion is None:    asMissing.append('--version');
        if self.oConfig.sProductName is None:       asMissing.append('--product');
        if self.oConfig.sBuildType is None:         asMissing.append('--type');
        if self.oConfig.asTargetOsArches is None:   asMissing.append('--os-arch');
        if self.oConfig.asFiles is None:            asMissing.append('--file');
        if asMissing:
            sys.stderr.write('syntax error: Missing: %s\n' % (asMissing,));
            sys.exit(1);
        # Temporary default.
        if self.oConfig.sRepository is None:
            self.oConfig.sRepository = 'vbox';

    def add(self):
        """
        Add build data record into database.
        """
        oDb = TMDatabaseConnection()

        # Assemble the build data.
        oBuildData = BuildDataEx()
        oBuildData.idBuildCategory    = None;
        oBuildData.iRevision          = self.oConfig.iRevision
        oBuildData.sVersion           = self.oConfig.sProductVersion
        oBuildData.sLogUrl            = self.oConfig.sBuildLogPath
        oBuildData.sBinaries          = ','.join(self.oConfig.asFiles);
        oBuildData.oCat = BuildCategoryData().initFromValues(sProduct    = self.oConfig.sProductName,
                                                             sRepository = self.oConfig.sRepository,
                                                             sBranch     = self.oConfig.sBranch,
                                                             sType       = self.oConfig.sBuildType,
                                                             asOsArches  = self.oConfig.asTargetOsArches);

        # Add record to database
        try:
            BuildLogic(oDb).addEntry(oBuildData, fCommit = True);
        except:
            if self.oConfig.fQuiet:
                sys.exit(1);
            raise;
        oDb.close();
        return 0;

if __name__ == '__main__':
    sys.exit(Build().add());

