#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: regen_sched_queues.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
# pylint: disable=line-too-long

"""
Interface used by the admin to regenerate scheduling queues.
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
import sys;
import os;
from optparse import OptionParser;  # pylint: disable=deprecated-module

# Add Test Manager's modules path
g_ksTestManagerDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))));
sys.path.append(g_ksTestManagerDir);

# Test Manager imports
from testmanager.core.db            import TMDatabaseConnection;
from testmanager.core.schedulerbase import SchedulerBase;
from testmanager.core.schedgroup    import SchedGroupLogic;



class RegenSchedQueues(object): # pylint: disable=too-few-public-methods
    """
    Regenerates all the scheduling queues.
    """

    def __init__(self):
        """
        Parse command line.
        """

        oParser = OptionParser();
        oParser.add_option('-q', '--quiet', dest = 'fQuiet', action = 'store_true', default = False,
                           help = 'Quiet execution');
        oParser.add_option('-u', '--uid', dest = 'uid', action = 'store', type = 'int', default = 1,
                           help = 'User ID to accredit with this job');
        oParser.add_option('--profile', dest = 'fProfile', action = 'store_true', default = False,
                           help = 'User ID to accredit with this job');

        (self.oConfig, _) = oParser.parse_args();


    def doIt(self):
        """
        Does the job.
        """
        oDb = TMDatabaseConnection();

        aoGroups = SchedGroupLogic(oDb).getAll();
        iRc = 0;
        for oGroup in aoGroups:
            if not self.oConfig.fQuiet:
                print('%s (ID %#d):' % (oGroup.sName, oGroup.idSchedGroup,));
            try:
                (aoErrors, asMessages) = SchedulerBase.recreateQueue(oDb, self.oConfig.uid, oGroup.idSchedGroup, 2);
            except Exception as oXcpt:
                oDb.rollback();
                print('  !!Hit exception processing "%s": %s' % (oGroup.sName, oXcpt,));
            else:
                if not aoErrors:
                    if not self.oConfig.fQuiet:
                        print('  Successfully regenerated.');
                else:
                    iRc = 1;
                    print('  %d errors:' % (len(aoErrors,)));
                    for oError in aoErrors:
                        if oError[1]  is None:
                            print('  !!%s' % (oError[0],));
                        else:
                            print('  !!%s (%s)' % (oError[0], oError[1]));
                if asMessages and not self.oConfig.fQuiet:
                    print('  %d messages:' % (len(asMessages),));
                    for sMsg in asMessages:
                        print('  ##%s' % (sMsg,));
        return iRc;

    @staticmethod
    def main():
        """ Main function. """
        oMain = RegenSchedQueues();
        if oMain.oConfig.fProfile is not True:
            iRc = oMain.doIt();
        else:
            import cProfile;
            oProfiler = cProfile.Profile();
            iRc = oProfiler.runcall(oMain.doIt);
            oProfiler.print_stats(sort = 'time');
            oProfiler = None;
        return iRc;

if __name__ == '__main__':
    sys.exit(RegenSchedQueues().main());

