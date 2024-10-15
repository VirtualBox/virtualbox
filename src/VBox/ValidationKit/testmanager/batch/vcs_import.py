#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: vcs_import.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Cron job for importing revision history for a repository.
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
from optparse import OptionParser; # pylint: disable=deprecated-module
import xml.etree.ElementTree as ET;

# Add Test Manager's modules path
g_ksTestManagerDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))));
sys.path.append(g_ksTestManagerDir);

# Test Manager imports
from testmanager.config                 import g_kdBugTrackers;
from testmanager.core.db                import TMDatabaseConnection;
from testmanager.core.vcsrevisions      import VcsRevisionData, VcsRevisionLogic;
from testmanager.core.vcsbugreference   import VcsBugReferenceData, VcsBugReferenceLogic;
from common                             import utils;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    long = int;     # pylint: disable=redefined-builtin,invalid-name


class VcsImport(object): # pylint: disable=too-few-public-methods
    """
    Imports revision history from a VSC into the Test Manager database.
    """

    class BugTracker(object):
        def __init__(self, sDbName, sTag):
            self.sDbName = sDbName;
            self.sTag    = sTag;


    def __init__(self):
        """
        Parse command line.
        """

        oParser = OptionParser()
        oParser.add_option('-b', '--only-bug-refs', dest = 'fBugRefsOnly', action = 'store_true',
                           help = 'Only do bug references, not revisions.');
        oParser.add_option('-e', '--extra-option', dest = 'asExtraOptions', metavar = 'vcsoption', action = 'append',
                           help = 'Adds a extra option to the command retrieving the log.');
        oParser.add_option('-f', '--full', dest = 'fFull', action = 'store_true',
                           help = 'Full revision history import.');
        oParser.add_option('-q', '--quiet', dest = 'fQuiet', action = 'store_true',
                           help = 'Quiet execution');
        oParser.add_option('-R', '--repository', dest = 'sRepository', metavar = '<repository>',
                           help = 'Version control repository name.');
        oParser.add_option('-s', '--start-revision', dest = 'iStartRevision', metavar = 'start-revision',
                           type = "int", default = 0,
                           help = 'The revision to start at when doing a full import.');
        oParser.add_option('-t', '--type', dest = 'sType', metavar = '<type>',
                           help = 'The VCS type (default: svn)', choices = [ 'svn', ], default = 'svn');
        oParser.add_option('-u', '--url', dest = 'sUrl', metavar = '<url>',
                           help = 'The VCS URL');

        (self.oConfig, _) = oParser.parse_args();

        # Check command line
        asMissing = [];
        if self.oConfig.sUrl is None:               asMissing.append('--url');
        if self.oConfig.sRepository is None:        asMissing.append('--repository');
        if asMissing:
            sys.stderr.write('syntax error: Missing: %s\n' % (asMissing,));
            sys.exit(1);

        assert self.oConfig.sType == 'svn';

    def main(self):
        """
        Main function.
        """
        oDb = TMDatabaseConnection();
        oLogic = VcsRevisionLogic(oDb);
        oBugLogic = VcsBugReferenceLogic(oDb);

        # Where to start.
        iStartRev = 0;
        if not self.oConfig.fFull:
            if not self.oConfig.fBugRefsOnly:
                iStartRev = oLogic.getLastRevision(self.oConfig.sRepository);
            else:
                iStartRev = oBugLogic.getLastRevision(self.oConfig.sRepository);
        if iStartRev == 0:
            iStartRev = self.oConfig.iStartRevision;

        # Construct a command line.
        os.environ['LC_ALL'] = 'en_US.utf-8';
        asArgs = [
            'svn',
            'log',
            '--xml',
            '--revision', str(iStartRev) + ':HEAD',
        ];
        if self.oConfig.asExtraOptions is not None:
            asArgs.extend(self.oConfig.asExtraOptions);
        asArgs.append(self.oConfig.sUrl);
        if not self.oConfig.fQuiet:
            print('Executing: %s' % (asArgs,));
        sLogXml = utils.processOutputChecked(asArgs);

        # Parse the XML and add the entries to the database.
        oParser = ET.XMLParser(target = ET.TreeBuilder(), encoding = 'utf-8');
        oParser.feed(sLogXml.encode('utf-8')); # Does its own decoding; processOutputChecked always gives us decoded utf-8 now.
        oRoot = oParser.close();

        for oLogEntry in oRoot.findall('logentry'):
            iRevision = int(oLogEntry.get('revision'));
            sAuthor  = oLogEntry.findtext('author', 'unspecified').strip(); # cvs2svn entries doesn't have an author.
            sDate    = oLogEntry.findtext('date').strip();
            sRawMsg  = oLogEntry.findtext('msg', '').strip();
            sMessage = sRawMsg;
            if sMessage == '':
                sMessage = ' ';
            elif len(sMessage) > VcsRevisionData.kcchMax_sMessage:
                sMessage = sMessage[:VcsRevisionData.kcchMax_sMessage - 4] + ' ...';
            if not self.oConfig.fQuiet:
                utils.printOut(u'sDate=%s iRev=%u sAuthor=%s sMsg[%s]=%s'
                               % (sDate, iRevision, sAuthor, type(sMessage).__name__, sMessage));

            if not self.oConfig.fBugRefsOnly:
                oData = VcsRevisionData().initFromValues(self.oConfig.sRepository, iRevision, sDate, sAuthor, sMessage);
                oLogic.addVcsRevision(oData);

            # Analyze the raw message looking for bug tracker references.
            for oBugTracker in g_kdBugTrackers.values():
                for sTag in oBugTracker.asCommitTags:
                    off = sRawMsg.find(sTag);
                    while off >= 0:
                        off += len(sTag);
                        while off < len(sRawMsg) and sRawMsg[off].isspace():
                            off += 1;

                        if off < len(sRawMsg) and sRawMsg[off].isdigit():
                            offNum = off;
                            while off < len(sRawMsg) and sRawMsg[off].isdigit():
                                off += 1;
                            try:
                                iBugNo = long(sRawMsg[offNum:off]);
                            except Exception as oXcpt:
                                utils.printErr(u'error! exception(r%s,"%s"): -> %s' % (iRevision, sRawMsg[offNum:off], oXcpt,));
                            else:
                                if not self.oConfig.fQuiet:
                                    utils.printOut(u' r%u -> sBugTracker=%s iBugNo=%s'
                                                   % (iRevision, oBugTracker.sDbId, iBugNo,));

                                oBugData = VcsBugReferenceData().initFromValues(self.oConfig.sRepository, iRevision,
                                                                                oBugTracker.sDbId, iBugNo);
                                oBugLogic.addVcsBugReference(oBugData);

                        # next
                        off = sRawMsg.find(sTag, off);

        oDb.commit();

        oDb.close();
        return 0;

if __name__ == '__main__':
    sys.exit(VcsImport().main());

