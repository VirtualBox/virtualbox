# -*- coding: utf-8 -*-
# $Id: vboxtestfileset.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
# pylint: disable=too-many-lines

"""
Test File Set
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
__version__ = "$Revision: 106320 $"


# Standard Python imports.
import os;
import sys;

# Validation Kit imports.
from common     import utils;
from testdriver import reporter;
from testdriver import testfileset;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    xrange = range; # pylint: disable=redefined-builtin,invalid-name


class TestFileSet(testfileset.TestFileSet):
    """
    A generated set of files and directories for uploading to a VM.

    The file and directory names are compatible with the host, so it is
    possible to copy them to the host without changing any names.

    Uploaded as a tarball and expanded via TXS (if new enough) or uploaded vts_tar
    utility from the validation kit.
    """

    def __init__(self, oTestVm, sBasePath, sSubDir, # pylint: disable=too-many-arguments
                 oRngFileSizes = xrange(0, 16384),
                 oRngManyFiles = xrange(128, 512),
                 oRngTreeFiles = xrange(128, 384),
                 oRngTreeDepth = xrange(92, 256),
                 oRngTreeDirs  = xrange(2, 16),
                 cchMaxPath    = 230,
                 cchMaxName    = 230,
                 asCompatibleWith = None,
                 uSeed         = None):

        asCompOses = [oTestVm.getGuestOs(), ];
        sHostOs = utils.getHostOs();
        if sHostOs not in asCompOses:
            asCompOses.append(sHostOs);

        testfileset.TestFileSet.__init__(self,
                                         fDosStyle        = oTestVm.isWindows() or oTestVm.isOS2(),
                                         asCompatibleWith = asCompOses,
                                         sBasePath        = sBasePath,
                                         sSubDir          = sSubDir,
                                         oRngFileSizes    = oRngFileSizes,
                                         oRngManyFiles    = oRngManyFiles,
                                         oRngTreeFiles    = oRngTreeFiles,
                                         oRngTreeDepth    = oRngTreeDepth,
                                         oRngTreeDirs     = oRngTreeDirs,
                                         cchMaxPath       = cchMaxPath,
                                         cchMaxName       = cchMaxName,
                                         uSeed            = uSeed);
        self.oTestVm = oTestVm;

    def __uploadFallback(self, oTxsSession, sTarFileGst, oTstDrv):
        """
        Fallback upload method.
        """
        sVtsTarExe = 'vts_tar' + self.oTestVm.getGuestExeSuff();
        sVtsTarHst = os.path.join(oTstDrv.sVBoxValidationKit, self.oTestVm.getGuestOs(),
                                  self.oTestVm.getGuestArch(), sVtsTarExe);
        sVtsTarGst = self.oTestVm.pathJoin(self.sBasePath, sVtsTarExe);

        if oTxsSession.syncUploadFile(sVtsTarHst, sVtsTarGst) is not True:
            return reporter.error('Failed to upload "%s" to the guest as "%s"!' % (sVtsTarHst, sVtsTarGst,));

        fRc = oTxsSession.syncExec(sVtsTarGst, [sVtsTarGst, '-xzf', sTarFileGst, '-C', self.sBasePath,], fWithTestPipe = False);
        if fRc is not True:
            return reporter.error('vts_tar failed!');
        return True;

    def upload(self, oTxsSession, oTstDrv):
        """
        Uploads the files into the guest via the given TXS session.

        Returns True / False.
        """

        #
        # Create a tarball.
        #
        sTarFileHst = os.path.join(oTstDrv.sScratchPath, 'tdAddGuestCtrl-1-Stuff.tar.gz');
        sTarFileGst = self.oTestVm.pathJoin(self.sBasePath, 'tdAddGuestCtrl-1-Stuff.tar.gz');
        if self.createTarball(sTarFileHst) is not True:
            return False;

        #
        # Upload it.
        #
        reporter.log('Uploading tarball "%s" to the guest as "%s"...' % (sTarFileHst, sTarFileGst));
        if oTxsSession.syncUploadFile(sTarFileHst, sTarFileGst) is not True:
            return reporter.error('Failed upload tarball "%s" as "%s"!' % (sTarFileHst, sTarFileGst,));

        #
        # Try unpack it.
        #
        reporter.log('Unpacking "%s" into "%s"...' % (sTarFileGst, self.sBasePath));
        if oTxsSession.syncUnpackFile(sTarFileGst, self.sBasePath, fIgnoreErrors = True) is not True:
            reporter.log('Failed to expand tarball "%s" into "%s", falling back on individual directory and file creation...'
                         % (sTarFileGst, self.sBasePath,));
            if self.__uploadFallback(oTxsSession, sTarFileGst, oTstDrv) is not True:
                return False;
        reporter.log('Successfully placed test files and directories in the VM.');
        return True;

