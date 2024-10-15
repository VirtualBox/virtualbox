# -*- coding: utf-8 -*-
# $Id: btresolver.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
# pylint: disable=too-many-lines

"""
Backtrace resolver using external debugging symbols and RTLdrFlt.
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


# Standard Python imports.
import os;
import re;
import shutil;
import subprocess;

# Validation Kit imports.
from common import utils;

def getRTLdrFltPath(asPaths):
    """
    Returns the path to the RTLdrFlt tool looking in the provided paths
    or None if not found.
    """

    for sPath in asPaths:
        for sDirPath, _, asFiles in os.walk(sPath):
            if 'RTLdrFlt' in asFiles:
                return os.path.join(sDirPath, 'RTLdrFlt');

    return None;



class BacktraceResolverOs(object):
    """
    Base class for all OS specific resolvers.
    """

    def __init__(self, sScratchPath, sBuildRoot, fnLog = None):
        self.sScratchPath = sScratchPath;
        self.sBuildRoot   = sBuildRoot;
        self.fnLog        = fnLog;

    def log(self, sText):
        """
        Internal logger callback.
        """
        if self.fnLog is not None:
            self.fnLog(sText);



class BacktraceResolverOsLinux(BacktraceResolverOs):
    """
    Linux specific backtrace resolver.
    """

    def __init__(self, sScratchPath, sBuildRoot, fnLog = None):
        """
        Constructs a Linux host specific backtrace resolver.
        """
        BacktraceResolverOs.__init__(self, sScratchPath, sBuildRoot, fnLog);

        self.asDbgFiles  = {};

    def prepareEnv(self):
        """
        Prepares the environment for annotating Linux reports.
        """
        fRc = False;
        try:
            sDbgArchive = os.path.join(self.sBuildRoot, 'bin', 'VirtualBox-dbg.tar.bz2');

            # Extract debug symbol archive if it was found.
            if os.path.exists(sDbgArchive):
                asMembers = utils.unpackFile(sDbgArchive, self.sScratchPath, self.fnLog,
                                             self.fnLog);
                if asMembers:
                    # Populate the list of debug files.
                    for sMember in asMembers:
                        if os.path.isfile(sMember):
                            self.asDbgFiles[os.path.basename(sMember)] = sMember;
                    fRc = True;
        except:
            self.log('Failed to setup debug symbols');

        return fRc;

    def cleanupEnv(self):
        """
        Cleans up the environment.
        """
        fRc = False;
        try:
            shutil.rmtree(self.sScratchPath, True);
            fRc = True;
        except:
            pass;

        return fRc;

    def getDbgSymPathFromBinary(self, sBinary, sArch):
        """
        Returns the path to file containing the debug symbols for the specified binary.
        """
        _ = sArch;
        sDbgFilePath = None;
        try:
            sDbgFilePath = self.asDbgFiles[sBinary];
        except:
            pass;

        return sDbgFilePath;

    def getBinaryListWithLoadAddrFromReport(self, asReport):
        """
        Parses the given VM state report and returns a list of binaries and their
        load address.

        Returns a list if tuples containing the binary and load addres or an empty
        list on failure.
        """
        asListBinaries = [];

        # Look for the line "Mapped address spaces:"
        iLine = 0;
        while iLine < len(asReport):
            if asReport[iLine].startswith('Mapped address spaces:'):
                break;
            iLine += 1;

        for sLine in asReport[iLine:]:
            asCandidate = sLine.split();
            if     len(asCandidate) == 5 \
               and asCandidate[0].startswith('0x') \
               and asCandidate[1].startswith('0x') \
               and asCandidate[2].startswith('0x') \
               and (asCandidate[3] == '0x0' or asCandidate[3] == '0')\
               and 'VirtualBox' in asCandidate[4]:
                asListBinaries.append((asCandidate[0], os.path.basename(asCandidate[4])));

        return asListBinaries;



class BacktraceResolverOsDarwin(BacktraceResolverOs):
    """
    Darwin specific backtrace resolver.
    """

    def __init__(self, sScratchPath, sBuildRoot, fnLog = None):
        """
        Constructs a Linux host specific backtrace resolver.
        """
        BacktraceResolverOs.__init__(self, sScratchPath, sBuildRoot, fnLog);

        self.asDbgFiles  = {};

    def prepareEnv(self):
        """
        Prepares the environment for annotating Darwin reports.
        """
        fRc = False;
        try:
            #
            # Walk the scratch path directory and look for .dSYM directories, building a
            # list of them.
            #
            asDSymPaths = [];

            for sDirPath, asDirs, _ in os.walk(self.sBuildRoot):
                for sDir in asDirs:
                    if sDir.endswith('.dSYM'):
                        asDSymPaths.append(os.path.join(sDirPath, sDir));

            # Expand the dSYM paths to full DWARF debug files in the next step
            # and add them to the debug files dictionary.
            for sDSymPath in asDSymPaths:
                sBinary = os.path.basename(sDSymPath).strip('.dSYM');
                self.asDbgFiles[sBinary] = os.path.join(sDSymPath, 'Contents', 'Resources',
                                                        'DWARF', sBinary);

            fRc = True;
        except:
            self.log('Failed to setup debug symbols');

        return fRc;

    def cleanupEnv(self):
        """
        Cleans up the environment.
        """
        fRc = False;
        try:
            shutil.rmtree(self.sScratchPath, True);
            fRc = True;
        except:
            pass;

        return fRc;

    def getDbgSymPathFromBinary(self, sBinary, sArch):
        """
        Returns the path to file containing the debug symbols for the specified binary.
        """
        # Hack to exclude executables as RTLdrFlt has some problems with it currently.
        _ = sArch;
        sDbgSym = None;
        try:
            sDbgSym = self.asDbgFiles[sBinary];
        except:
            pass;

        if sDbgSym is not None and sDbgSym.endswith('.dylib'):
            return sDbgSym;

        return None;

    def _getReportVersion(self, asReport):
        """
        Returns the version of the darwin report.
        """
        # Find the line starting with "Report Version:"
        iLine = 0;
        iVersion = 0;
        while iLine < len(asReport):
            if asReport[iLine].startswith('Report Version:'):
                break;
            iLine += 1;

        if iLine < len(asReport):
            # Look for the start of the number
            sVersion = asReport[iLine];
            iStartVersion = len('Report Version:');
            iEndVersion = len(sVersion);

            while     iStartVersion < len(sVersion) \
                  and not sVersion[iStartVersion:iStartVersion+1].isdigit():
                iStartVersion += 1;

            while     iEndVersion > 0 \
                  and not sVersion[iEndVersion-1:iEndVersion].isdigit():
                iEndVersion -= 1;

            iVersion = int(sVersion[iStartVersion:iEndVersion]);
        else:
            self.log('Couldn\'t find the report version');

        return iVersion;

    def _getListOfBinariesFromReportPreSierra(self, asReport):
        """
        Returns a list of loaded binaries with their load address obtained from
        a pre Sierra report.
        """
        asListBinaries = [];

        # Find the line starting with "Binary Images:"
        iLine = 0;
        while iLine < len(asReport):
            if asReport[iLine].startswith('Binary Images:'):
                break;
            iLine += 1;

        if iLine < len(asReport):
            # List starts after that
            iLine += 1;

            # A line for a loaded binary looks like the following:
            #     0x100042000 -        0x100095fff +VBoxDDU.dylib (4.3.15) <EB19C44D-F882-0803-DBDD-9995723111B7> /Application...
            # We need the start address and the library name.
            # To distinguish between our own libraries and ones from Apple we check whether the path at the end starts with
            #     /Applications/VirtualBox.app/Contents/MacOS
            oRegExpPath = re.compile(r'/VirtualBox.app/Contents/MacOS');
            oRegExpAddr = re.compile(r'0x\w+');
            oRegExpBinPath = re.compile(r'VirtualBox.app/Contents/MacOS/\S*');
            while iLine < len(asReport):
                asMatches = oRegExpPath.findall(asReport[iLine]);
                if asMatches:
                    # Line contains the path, extract start address and path to binary
                    sAddr = oRegExpAddr.findall(asReport[iLine]);
                    sPath = oRegExpBinPath.findall(asReport[iLine]);

                    if sAddr and sPath:
                        # Construct the path in into the build cache containing the debug symbols
                        oRegExp = re.compile(r'\w+\.{0,1}\w*$');
                        sFilename = oRegExp.findall(sPath[0]);

                        asListBinaries.append((sAddr[0], sFilename[0]));
                    else:
                        break; # End of image list
                iLine += 1;
        else:
            self.log('Couldn\'t find the list of loaded binaries in the given report');

        return asListBinaries;

    def _getListOfBinariesFromReportSierra(self, asReport):
        """
        Returns a list of loaded binaries with their load address obtained from
        a Sierra+ report.
        """
        asListBinaries = [];

        # A line for a loaded binary looks like the following:
        #     4   VBoxXPCOMIPCC.dylib                 0x00000001139f17ea 0x1139e4000 + 55274
        # We need the start address and the library name.
        # To distinguish between our own libraries and ones from Apple we check whether the library
        # name contains VBox or VirtualBox
        iLine = 0;
        while iLine < len(asReport):
            asStackTrace = asReport[iLine].split();

            # Check whether the line is made up of 6 elements separated by whitespace
            # and the first one is a number.
            if     len(asStackTrace) == 6 and asStackTrace[0].isdigit() \
               and (asStackTrace[1].find('VBox') != -1 or asStackTrace[1].find('VirtualBox') != -1) \
               and asStackTrace[3].startswith('0x'):

                # Check whether the library is already in our list an only add new ones
                fFound = False;
                for _, sLibrary in asListBinaries:
                    if asStackTrace[1] == sLibrary:
                        fFound = True;
                        break;

                if not fFound:
                    asListBinaries.append((asStackTrace[3], asStackTrace[1]));
            iLine += 1;

        return asListBinaries;

    def getBinaryListWithLoadAddrFromReport(self, asReport):
        """
        Parses the given VM state report and returns a list of binaries and their
        load address.

        Returns a list if tuples containing the binary and load addres or an empty
        list on failure.
        """
        asListBinaries = [];

        iVersion = self._getReportVersion(asReport);
        if iVersion > 0:
            if iVersion <= 11:
                self.log('Pre Sierra Report');
                asListBinaries = self._getListOfBinariesFromReportPreSierra(asReport);
            elif iVersion == 12:
                self.log('Sierra report');
                asListBinaries = self._getListOfBinariesFromReportSierra(asReport);
            else:
                self.log('Unsupported report version %s' % (iVersion, ));

        return asListBinaries;



class BacktraceResolverOsSolaris(BacktraceResolverOs):
    """
    Solaris specific backtrace resolver.
    """

    def __init__(self, sScratchPath, sBuildRoot, fnLog = None):
        """
        Constructs a Linux host specific backtrace resolver.
        """
        BacktraceResolverOs.__init__(self, sScratchPath, sBuildRoot, fnLog);

        self.asDbgFiles  = {};

    def prepareEnv(self):
        """
        Prepares the environment for annotating Linux reports.
        """
        fRc = False;
        try:
            sDbgArchive = os.path.join(self.sBuildRoot, 'bin', 'VirtualBoxDebug.tar.bz2');

            # Extract debug symbol archive if it was found.
            if os.path.exists(sDbgArchive):
                asMembers = utils.unpackFile(sDbgArchive, self.sScratchPath, self.fnLog,
                                             self.fnLog);
                if asMembers:
                    # Populate the list of debug files.
                    for sMember in asMembers:
                        if os.path.isfile(sMember):
                            sArch = '';
                            if 'amd64' in sMember:
                                sArch = 'amd64';
                            else:
                                sArch = 'x86';
                            self.asDbgFiles[os.path.basename(sMember) + '/' + sArch] = sMember;
                    fRc = True;
                else:
                    self.log('Unpacking the debug archive failed');
        except:
            self.log('Failed to setup debug symbols');

        return fRc;

    def cleanupEnv(self):
        """
        Cleans up the environment.
        """
        fRc = False;
        try:
            shutil.rmtree(self.sScratchPath, True);
            fRc = True;
        except:
            pass;

        return fRc;

    def getDbgSymPathFromBinary(self, sBinary, sArch):
        """
        Returns the path to file containing the debug symbols for the specified binary.
        """
        sDbgFilePath = None;
        try:
            sDbgFilePath = self.asDbgFiles[sBinary + '/' + sArch];
        except:
            pass;

        return sDbgFilePath;

    def getBinaryListWithLoadAddrFromReport(self, asReport):
        """
        Parses the given VM state report and returns a list of binaries and their
        load address.

        Returns a list if tuples containing the binary and load addres or an empty
        list on failure.
        """
        asListBinaries = [];

        # Look for the beginning of the process address space mappings"
        for sLine in asReport:
            asItems = sLine.split();
            if     len(asItems) == 4 \
               and asItems[3].startswith('/opt/VirtualBox') \
               and (   asItems[2] == 'r-x--' \
                    or asItems[2] == 'r-x----'):
                fFound = False;
                sBinaryFile = os.path.basename(asItems[3]);
                for _, sBinary in asListBinaries:
                    if sBinary == sBinaryFile:
                        fFound = True;
                        break;
                if not fFound:
                    asListBinaries.append(('0x' + asItems[0], sBinaryFile));

        return asListBinaries;



class BacktraceResolver(object):
    """
    A backtrace resolving class.
    """

    def __init__(self, sScratchPath, sBuildRoot, sTargetOs, sArch, sRTLdrFltPath = None, fnLog = None):
        """
        Constructs a backtrace resolver object for the given target OS,
        architecture and path to the directory containing the debug symbols and tools
        we need.
        """
        # Initialize all members first.
        self.sScratchPath    = sScratchPath;
        self.sBuildRoot      = sBuildRoot;
        self.sTargetOs       = sTargetOs;
        self.sArch           = sArch;
        self.sRTLdrFltPath   = sRTLdrFltPath;
        self.fnLog           = fnLog;
        self.sDbgSymPath     = None;
        self.oResolverOs     = None;
        self.sScratchDbgPath = os.path.join(self.sScratchPath, 'dbgsymbols');

        if self.fnLog is None:
            self.fnLog = self.logStub;

        if self.sRTLdrFltPath is None:
            self.sRTLdrFltPath = getRTLdrFltPath([self.sScratchPath, self.sBuildRoot]);
            if self.sRTLdrFltPath is not None:
                self.log('Found RTLdrFlt in %s' % (self.sRTLdrFltPath,));
            else:
                self.log('Couldn\'t find RTLdrFlt in either %s or %s' % (self.sScratchPath, self.sBuildRoot));

    def log(self, sText):
        """
        Internal logger callback.
        """
        if self.fnLog is not None:
            self.fnLog(sText);

    def logStub(self, sText):
        """
        Logging stub doing nothing.
        """
        _ = sText;

    def prepareEnv(self):
        """
        Prepares the environment to annotate backtraces, finding the required tools
        and retrieving the debug symbols depending on the host OS.

        Returns True on success and False on error or if not supported.
        """

        # No access to the RTLdrFlt tool means no symbols so no point in trying
        # to set something up.
        if self.sRTLdrFltPath is None:
            return False;

        # Create a directory containing the scratch space for the OS resolver backends.
        fRc = True;
        if not os.path.exists(self.sScratchDbgPath):
            try:
                os.makedirs(self.sScratchDbgPath, 0o750);
            except:
                fRc = False;
                self.log('Failed to create scratch directory for debug symbols');

        if fRc:
            if self.sTargetOs == 'linux':
                self.oResolverOs = BacktraceResolverOsLinux(self.sScratchDbgPath, self.sScratchPath, self.fnLog);
            elif self.sTargetOs == 'darwin':
                self.oResolverOs = BacktraceResolverOsDarwin(self.sScratchDbgPath, self.sScratchPath, self.fnLog); # pylint: disable=redefined-variable-type
            elif self.sTargetOs == 'solaris':
                self.oResolverOs = BacktraceResolverOsSolaris(self.sScratchDbgPath, self.sScratchPath, self.fnLog); # pylint: disable=redefined-variable-type
            else:
                self.log('The backtrace resolver is not supported on %s' % (self.sTargetOs,));
                fRc = False;

            if fRc:
                fRc = self.oResolverOs.prepareEnv();
                if not fRc:
                    self.oResolverOs = None;

            if not fRc:
                shutil.rmtree(self.sScratchDbgPath, True)

        return fRc;

    def cleanupEnv(self):
        """
        Prepares the environment to annotate backtraces, finding the required tools
        and retrieving the debug symbols depending on the host OS.

        Returns True on success and False on error or if not supported.
        """
        fRc = False;
        if self.oResolverOs is not None:
            fRc = self.oResolverOs.cleanupEnv();

        shutil.rmtree(self.sScratchDbgPath, True);
        return fRc;

    def annotateReport(self, sReport):
        """
        Annotates the given report with the previously prepared environment.

        Returns the annotated report on success or None on failure.
        """
        sReportAn = None;

        if self.oResolverOs is not None:
            asListBinaries = self.oResolverOs.getBinaryListWithLoadAddrFromReport(sReport.split('\n'));

            if asListBinaries:
                asArgs = [self.sRTLdrFltPath, ];

                for sLoadAddr, sBinary in asListBinaries:
                    sDbgSymPath = self.oResolverOs.getDbgSymPathFromBinary(sBinary, self.sArch);
                    if sDbgSymPath is not None:
                        asArgs.append(sDbgSymPath);
                        asArgs.append(sLoadAddr);

                oRTLdrFltProc = subprocess.Popen(asArgs, stdin=subprocess.PIPE,         # pylint: disable=consider-using-with
                                                 stdout=subprocess.PIPE, bufsize=0);
                if oRTLdrFltProc is not None:
                    try:
                        sReportAn, _ = oRTLdrFltProc.communicate(sReport);
                    except:
                        self.log('Retrieving annotation report failed (broken pipe / no matching interpreter?)');
                else:
                    self.log('Error spawning RTLdrFlt process');
            else:
                self.log('Getting list of loaded binaries failed');
        else:
            self.log('Backtrace resolver not fully initialized, not possible to annotate');

        return sReportAn;

