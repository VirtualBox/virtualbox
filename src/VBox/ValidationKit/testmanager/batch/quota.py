#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: quota.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
# pylint: disable=line-too-long

"""
A cronjob that applies quotas to large files in testsets.
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
import shutil
import tempfile;
import zipfile;

# Add Test Manager's modules path
g_ksTestManagerDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.append(g_ksTestManagerDir)

# Test Manager imports
from testmanager                import config;
from testmanager.core.db        import TMDatabaseConnection;
from testmanager.core.testset   import TestSetLogic;


class ArchiveDelFilesBatchJob(object): # pylint: disable=too-few-public-methods
    """
    Log+files comp
    """

    def __init__(self, oOptions):
        """
        Parse command line
        """
        self.fDryRun            = oOptions.fDryRun;
        self.fVerbose           = oOptions.fVerbose;
        self.sTempDir           = tempfile.gettempdir();

        self.dprint('Connecting to DB ...');
        self.oTestSetLogic      = TestSetLogic(TMDatabaseConnection(self.dprint if self.fVerbose else None));

        ## Fetches (and handles) all testsets up to this age (in hours).
        self.uHoursAgeToHandle  = 24;
        ## Always remove files with these extensions.
        self.asRemoveFileExt    = [ 'webm' ];
        ## Always remove files which are bigger than this limit.
        #  Set to 0 to disable.
        self.cbRemoveBiggerThan = 128 * 1024 * 1024;

    def dprint(self, sText):
        """ Verbose output. """
        if self.fVerbose:
            print(sText);
        return True;

    def warning(self, sText):
        """Prints a warning."""
        print(sText);
        return True;

    def _replaceFile(self, sDstFile, sSrcFile, fDryRun = False, fForce = False):
        """
        Replaces / moves a file safely by backing up the existing destination file (if any).

        Returns success indicator.
        """

        fRc = True;

        # Rename the destination file first (if any).
        sDstFileTmp = None;
        if os.path.exists(sDstFile):
            sDstFileTmp = sDstFile + ".bak";
            if os.path.exists(sDstFileTmp):
                if not fForce:
                    print('Replace file: Warning: Temporary destination file "%s" already exists, skipping' % (sDstFileTmp,));
                    fRc = False;
                else:
                    try:
                        os.remove(sDstFileTmp);
                    except Exception as e:
                        print('Replace file: Error deleting old temporary destination file "%s": %s' % (sDstFileTmp, e));
                        fRc = False;
            try:
                if not fDryRun:
                    shutil.move(sDstFile, sDstFileTmp);
            except Exception as e:
                print('Replace file: Error moving old destination file "%s" to temporary file "%s": %s' \
                      % (sDstFile, sDstFileTmp, e));
                fRc = False;

        if not fRc:
            return False;

        try:
            if not fDryRun:
                shutil.move(sSrcFile, sDstFile);
        except Exception as e:
            print('Replace file: Error moving source file "%s" to destination "%s": %s' % (sSrcFile, sDstFile, e,));
            fRc = False;

        if sDstFileTmp:
            if fRc: # Move succeeded, remove backup.
                try:
                    if not fDryRun:
                        os.remove(sDstFileTmp);
                except Exception as e:
                    print('Replace file: Error deleting temporary destination file "%s": %s' % (sDstFileTmp, e));
                    fRc = False;
            else: # Final move failed, roll back.
                try:
                    if not fDryRun:
                        shutil.move(sDstFileTmp, sDstFile);
                except Exception as e:
                    print('Replace file: Error restoring old destination file "%s": %s' % (sDstFile, e));
                    fRc = False;
        return fRc;

    def _processTestSetZip(self, idTestSet, sSrcZipFileAbs):
        """
        Worker for processOneTestSet, which processes the testset's ZIP file.

        Returns success indicator.
        """
        _ = idTestSet

        with tempfile.NamedTemporaryFile(dir=self.sTempDir, delete=False) as tmpfile:
            sDstZipFileAbs = tmpfile.name;

        fRc = True;

        try:
            oSrcZipFile = zipfile.ZipFile(sSrcZipFileAbs, 'r');                             # pylint: disable=consider-using-with
            self.dprint('Processing ZIP archive "%s" ...' % (sSrcZipFileAbs));
            try:
                if not self.fDryRun:
                    oDstZipFile = zipfile.ZipFile(sDstZipFileAbs, 'w');                     # pylint: disable=consider-using-with
                    self.dprint('Using temporary ZIP archive "%s"' % (sDstZipFileAbs));
                try:
                    #
                    # First pass: Gather information if we need to do some re-packing.
                    #
                    fDoRepack = False;
                    aoFilesToRepack = [];
                    for oCurFile in oSrcZipFile.infolist():
                        self.dprint('Handling File "%s" ...' % (oCurFile.filename))
                        sFileExt = os.path.splitext(oCurFile.filename)[1];

                        if  sFileExt \
                        and sFileExt[1:] in self.asRemoveFileExt:
                            self.dprint('\tMatches excluded extensions')
                            fDoRepack = True;
                        elif     self.cbRemoveBiggerThan \
                             and oCurFile.file_size > self.cbRemoveBiggerThan:
                            self.dprint('\tIs bigger than %d bytes (%d bytes)' % (self.cbRemoveBiggerThan, oCurFile.file_size))
                            fDoRepack = True;
                        else:
                            aoFilesToRepack.append(oCurFile);

                    if not fDoRepack:
                        oSrcZipFile.close();
                        self.dprint('No re-packing necessary, skipping ZIP archive');
                        return True;

                    #
                    # Second pass: Re-pack all needed files into our temporary ZIP archive.
                    #
                    for oCurFile in aoFilesToRepack:
                        self.dprint('Re-packing file "%s"' % (oCurFile.filename,))
                        if not self.fDryRun:
                            oBuf = oSrcZipFile.read(oCurFile);
                            oDstZipFile.writestr(oCurFile, oBuf);

                    if not self.fDryRun:
                        oDstZipFile.close();

                except Exception as oXcpt4:
                    print('Error handling file "%s" of archive "%s": %s' % (oCurFile.filename, sSrcZipFileAbs, oXcpt4,));
                    return False;

                oSrcZipFile.close();

                if fRc:
                    self.dprint('Moving file "%s" to "%s"' % (sDstZipFileAbs, sSrcZipFileAbs));
                    fRc = self._replaceFile(sSrcZipFileAbs, sDstZipFileAbs, self.fDryRun);

            except Exception as oXcpt3:
                print('Error creating temporary ZIP archive "%s": %s' % (sDstZipFileAbs, oXcpt3,));
                return False;

        except Exception as oXcpt1:
            # Construct a meaningful error message.
            if os.path.exists(sSrcZipFileAbs):
                print('Error: Opening file "%s" failed: %s' % (sSrcZipFileAbs, oXcpt1));
            else:
                print('Error: File "%s" not found.' % (sSrcZipFileAbs,));
            return False;

        return fRc;


    def processOneTestSet(self, idTestSet, sBasename):
        """
        Processes one single testset.

        Returns success indicator.
        """

        fRc = True;
        self.dprint('Processing testset %d' % (idTestSet,));

        # Construct absolute ZIP file path.
        # ZIP is hardcoded in config, so do here.
        sSrcZipFileAbs = os.path.join(config.g_ksZipFileAreaRootDir, sBasename + '.zip');

        if self._processTestSetZip(idTestSet, sSrcZipFileAbs) is not True:
            fRc = False;

        return fRc;

    def processTestSets(self):
        """
        Processes all testsets according to the set configuration.

        Returns success indicator.
        """

        aoTestSets = self.oTestSetLogic.fetchByAge(cHoursBack = self.uHoursAgeToHandle);
        cTestSets = len(aoTestSets);
        print('Found %d entries in DB' % cTestSets);
        if not cTestSets:
            return True; # Nothing to do (yet).

        fRc = True;
        for oTestSet in aoTestSets:
            fRc = self.processOneTestSet(oTestSet.idTestSet, oTestSet.sBaseFilename) and fRc;
            # Keep going.

        return fRc;

    @staticmethod
    def main():
        """ C-style main(). """
        #
        # Parse options.
        #

        oParser = OptionParser();

        # Generic options.
        oParser.add_option('-v', '--verbose', dest = 'fVerbose', action = 'store_true',  default = False,
                           help = 'Verbose output.');
        oParser.add_option('-q', '--quiet',   dest = 'fVerbose', action = 'store_false', default = False,
                           help = 'Quiet operation.');
        oParser.add_option('-d', '--dry-run', dest = 'fDryRun',  action = 'store_true',  default = False,
                           help = 'Dry run, do not make any changes.');

        (oOptions, asArgs) = oParser.parse_args(sys.argv[1:]);
        if asArgs != []:
            oParser.print_help();
            return 1;

        if oOptions.fDryRun:
            print('***********************************');
            print('*** DRY RUN - NO FILES MODIFIED ***');
            print('***********************************');

        #
        # Do the work.
        #
        fRc = False;

        oBatchJob = ArchiveDelFilesBatchJob(oOptions);
        fRc = oBatchJob.processTestSets();

        if oOptions.fVerbose:
            print('SUCCESS' if fRc else 'FAILURE');

        return 0 if fRc is True else 1;

if __name__ == '__main__':
    sys.exit(ArchiveDelFilesBatchJob.main());
