#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: filearchiver.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
# pylint: disable=line-too-long

"""
A cronjob that compresses logs and other files, moving them to the
g_ksZipFileAreaRootDir storage area.
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
import time;
import zipfile;

# Add Test Manager's modules path
g_ksTestManagerDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.append(g_ksTestManagerDir)

# Test Manager imports
from common                     import utils;
from testmanager                import config;
from testmanager.core.db        import TMDatabaseConnection;
from testmanager.core.testset   import TestSetData, TestSetLogic;



class FileArchiverBatchJob(object): # pylint: disable=too-few-public-methods
    """
    Log+files comp
    """

    def __init__(self, oOptions):
        """
        Parse command line
        """
        self.fVerbose      = oOptions.fVerbose;
        self.sSrcDir       = config.g_ksFileAreaRootDir;
        self.sDstDir       = config.g_ksZipFileAreaRootDir;
        #self.oTestSetLogic = TestSetLogic(TMDatabaseConnection(self.dprint if self.fVerbose else None));
        self.oTestSetLogic = TestSetLogic(TMDatabaseConnection(None));
        self.fDryRun       = oOptions.fDryRun;

    def dprint(self, sText):
        """ Verbose output. """
        if self.fVerbose:
            print(sText);
        return True;

    def warning(self, sText):
        """Prints a warning."""
        print(sText);
        return True;

    def _processTestSet(self, idTestSet, asFiles, sCurDir):
        """
        Worker for processDir.
        Same return codes as processDir.
        """

        sBaseFilename = os.path.join(sCurDir, 'TestSet-%d' % (idTestSet,));
        if sBaseFilename[0:2] == ('.' + os.path.sep):
            sBaseFilename = sBaseFilename[2:];
        sSrcFileBase = os.path.join(self.sSrcDir, sBaseFilename + '-');

        #
        # Skip the file if the test set is still running.
        # But delete them if the testset is not found.
        #
        oTestSet = self.oTestSetLogic.tryFetch(idTestSet);
        if oTestSet is not None and sBaseFilename != oTestSet.sBaseFilename:
            self.warning('TestSet %d: Deleting because sBaseFilename differs: "%s" (disk) vs "%s" (db)' \
                         % (idTestSet, sBaseFilename, oTestSet.sBaseFilename,));
            oTestSet = None;

        if oTestSet is not None:
            if oTestSet.enmStatus == TestSetData.ksTestStatus_Running:
                self.dprint('Skipping test set #%d, still running' % (idTestSet,));
                return True;

            #
            # If we have a zip file already, don't try recreate it as we might
            # have had trouble removing the source files.
            #
            sDstDirPath = os.path.join(self.sDstDir, sCurDir);
            sZipFileNm  = os.path.join(sDstDirPath, 'TestSet-%d.zip' % (idTestSet,));
            if not os.path.exists(sZipFileNm):
                #
                # Create zip file with all testset files as members.
                #
                self.dprint('TestSet %d: Creating %s...' % (idTestSet, sZipFileNm,));
                if not self.fDryRun:

                    if not os.path.exists(sDstDirPath):
                        os.makedirs(sDstDirPath, 0o755);

                    utils.noxcptDeleteFile(sZipFileNm + '.tmp');
                    with zipfile.ZipFile(sZipFileNm + '.tmp', 'w', zipfile.ZIP_DEFLATED, allowZip64 = True) as oZipFile:
                        for sFile in asFiles:
                            sSuff = os.path.splitext(sFile)[1];
                            if sSuff in [ '.png', '.webm', '.gz', '.bz2', '.zip', '.mov', '.avi', '.mpg', '.gif', '.jpg' ]:
                                ## @todo Consider storing these files outside the zip if they are a little largish.
                                self.dprint('TestSet %d: Storing   %s...' % (idTestSet, sFile));
                                oZipFile.write(sSrcFileBase + sFile, sFile, zipfile.ZIP_STORED);
                            else:
                                self.dprint('TestSet %d: Deflating %s...' % (idTestSet, sFile));
                                oZipFile.write(sSrcFileBase + sFile, sFile, zipfile.ZIP_DEFLATED);

                    #
                    # .zip.tmp -> .zip.
                    #
                    utils.noxcptDeleteFile(sZipFileNm);
                    os.rename(sZipFileNm + '.tmp', sZipFileNm);

                #else: Dry run.
            else:
                self.dprint('TestSet %d: zip file exists already (%s)' % (idTestSet, sZipFileNm,));

        #
        # Delete the files.
        #
        fRc = True;
        if self.fVerbose:
            self.dprint('TestSet %d: deleting file: %s' % (idTestSet, asFiles));
        if not self.fDryRun:
            for sFile in asFiles:
                if utils.noxcptDeleteFile(sSrcFileBase + sFile) is False:
                    self.warning('TestSet %d: Failed to delete "%s" (%s)' % (idTestSet, sFile, sSrcFileBase + sFile,));
                    fRc = False;

        return fRc;


    def processDir(self, sCurDir):
        """
        Process the given directory (relative to sSrcDir and sDstDir).
        Returns success indicator.
        """
        if self.fVerbose:
            self.dprint('processDir: %s' % (sCurDir,));

        #
        # Sift thought the directory content, collecting subdirectories and
        # sort relevant files by test set.
        # Generally there will either be subdirs or there will be files.
        #
        asSubDirs = [];
        dTestSets = {};
        sCurPath = os.path.abspath(os.path.join(self.sSrcDir, sCurDir));
        for sFile in os.listdir(sCurPath):
            if os.path.isdir(os.path.join(sCurPath, sFile)):
                if sFile not in [ '.', '..' ]:
                    asSubDirs.append(sFile);
            elif sFile.startswith('TestSet-'):
                # Parse the file name. ASSUMES 'TestSet-%d-filename' format.
                iSlash1 = sFile.find('-');
                iSlash2 = sFile.find('-', iSlash1 + 1);
                if iSlash2 <= iSlash1:
                    self.warning('Bad filename (1): "%s"' % (sFile,));
                    continue;

                try:    idTestSet = int(sFile[(iSlash1 + 1):iSlash2]);
                except:
                    self.warning('Bad filename (2): "%s"' % (sFile,));
                    if self.fVerbose:
                        self.dprint('\n'.join(utils.getXcptInfo(4)));
                    continue;

                if idTestSet <= 0:
                    self.warning('Bad filename (3): "%s"' % (sFile,));
                    continue;

                if iSlash2 + 2 >= len(sFile):
                    self.warning('Bad filename (4): "%s"' % (sFile,));
                    continue;
                sName = sFile[(iSlash2 + 1):];

                # Add it.
                if idTestSet not in dTestSets:
                    dTestSets[idTestSet] = [];
                asTestSet = dTestSets[idTestSet];
                asTestSet.append(sName);

        #
        # Test sets.
        #
        fRc = True;
        for idTestSet, oTestSet in dTestSets.items():
            try:
                if self._processTestSet(idTestSet, oTestSet, sCurDir) is not True:
                    fRc = False;
            except:
                self.warning('TestSet %d: Exception in _processTestSet:\n%s' % (idTestSet, '\n'.join(utils.getXcptInfo()),));
                fRc = False;

        #
        # Sub dirs.
        #
        for sSubDir in asSubDirs:
            if self.processDir(os.path.join(sCurDir, sSubDir)) is not True:
                fRc = False;

        #
        # Try Remove the directory iff it's not '.' and it's been unmodified
        # for the last 24h (race protection).
        #
        if sCurDir != '.':
            try:
                fpModTime = float(os.path.getmtime(sCurPath));
                if fpModTime + (24*3600) <= time.time():
                    if utils.noxcptRmDir(sCurPath) is True:
                        self.dprint('Removed "%s".' % (sCurPath,));
            except:
                pass;

        return fRc;

    @staticmethod
    def main():
        """ C-style main(). """
        #
        # Parse options.
        #
        oParser = OptionParser();
        oParser.add_option('-v', '--verbose', dest = 'fVerbose', action = 'store_true',  default = False,
                           help = 'Verbose output.');
        oParser.add_option('-q', '--quiet',   dest = 'fVerbose', action = 'store_false', default = False,
                           help = 'Quiet operation.');
        oParser.add_option('-d', '--dry-run', dest = 'fDryRun',  action = 'store_true',  default = False,
                           help = 'Dry run, do not make any changes.');
        (oOptions, asArgs) = oParser.parse_args()
        if asArgs != []:
            oParser.print_help();
            return 1;

        #
        # Do the work.
        #
        oBatchJob = FileArchiverBatchJob(oOptions);
        fRc = oBatchJob.processDir('.');
        return 0 if fRc is True else 1;

if __name__ == '__main__':
    sys.exit(FileArchiverBatchJob.main());

