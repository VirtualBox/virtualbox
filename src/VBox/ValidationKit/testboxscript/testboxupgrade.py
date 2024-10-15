# -*- coding: utf-8 -*-
# $Id: testboxupgrade.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
TestBox Script - Upgrade from local file ZIP.
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

# Standard python imports.
import os
import shutil
import sys
import subprocess
import threading
import time
import uuid;
import zipfile

# Validation Kit imports.
from common        import utils;
import testboxcommons
from testboxscript import TBS_EXITCODE_SYNTAX;

# Figure where we are.
try:    __file__                            # pylint: disable=used-before-assignment
except: __file__ = sys.argv[0];
g_ksTestScriptDir = os.path.dirname(os.path.abspath(__file__));
g_ksValidationKitDir  = os.path.dirname(g_ksTestScriptDir);


def _doUpgradeThreadProc(oStdOut, asBuf):
    """Thread procedure for the upgrade test drive."""
    asBuf.append(oStdOut.read());
    return True;


def _doUpgradeCheckZip(oZip):
    """
    Check that the essential files are there.
    Returns list of members on success, None on failure.
    """
    asMembers = oZip.namelist();
    if   ('testboxscript/testboxscript/testboxscript.py'      not in asMembers) \
      or ('testboxscript/testboxscript/testboxscript_real.py' not in asMembers):
        testboxcommons.log('Missing one or both testboxscripts (members: %s)' % (asMembers,));
        return None;

    for sMember in asMembers:
        if not sMember.startswith('testboxscript/'):
            testboxcommons.log('zip file contains member outside testboxscript/: "%s"' % (sMember,));
            return None;
        if sMember.find('/../') > 0 or sMember.endswith('/..'):
            testboxcommons.log('zip file contains member with escape sequence: "%s"' % (sMember,));
            return None;

    return asMembers;

def _doUpgradeUnzipAndCheck(oZip, sUpgradeDir, asMembers):
    """
    Unzips the files into sUpdateDir, does chmod(755) on all files and
    checks that there are no symlinks or special files.
    Returns True/False.
    """
    #
    # Extract the files.
    #
    if os.path.exists(sUpgradeDir):
        shutil.rmtree(sUpgradeDir);
    for sMember in asMembers:
        if sMember.endswith('/'):
            os.makedirs(os.path.join(sUpgradeDir, sMember.replace('/', os.path.sep)), 0o775);
        else:
            oZip.extract(sMember, sUpgradeDir);

    #
    # Make all files executable and make sure only owner can write to them.
    # While at it, also check that there are only files and directory, no
    # symbolic links or special stuff.
    #
    for sMember in asMembers:
        sFull = os.path.join(sUpgradeDir, sMember);
        if sMember.endswith('/'):
            if not os.path.isdir(sFull):
                testboxcommons.log('Not directory: "%s"' % sFull);
                return False;
        else:
            if not os.path.isfile(sFull):
                testboxcommons.log('Not regular file: "%s"' % sFull);
                return False;
            try:
                os.chmod(sFull, 0o755);
            except Exception as oXcpt:
                testboxcommons.log('warning chmod error on %s: %s' % (sFull, oXcpt));
    return True;

def _doUpgradeTestRun(sUpgradeDir):
    """
    Do a testrun of the new script, to make sure it doesn't fail with
    to run in any way because of old python, missing import or generally
    busted upgrade.
    Returns True/False.
    """
    asArgs = [os.path.join(sUpgradeDir, 'testboxscript', 'testboxscript', 'testboxscript.py'), '--version' ];
    testboxcommons.log('Testing the new testbox script (%s)...' % (asArgs[0],));
    if sys.executable:
        asArgs.insert(0, sys.executable);
    oChild = subprocess.Popen(asArgs, shell = False,                                        # pylint: disable=consider-using-with
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT);

    asBuf = []
    oThread = threading.Thread(target=_doUpgradeThreadProc, args=(oChild.stdout, asBuf));
    oThread.daemon = True;
    oThread.start();
    oThread.join(30);

    # Give child up to 5 seconds to terminate after producing output.
    if sys.version_info[0] >= 3 and sys.version_info[1] >= 3:
        oChild.wait(5); # pylint: disable=too-many-function-args
    else:
        for _ in range(50):
            iStatus = oChild.poll();
            if iStatus is None:
                break;
            time.sleep(0.1);
    iStatus = oChild.poll();
    if iStatus is None:
        testboxcommons.log('Checking the new testboxscript timed out.');
        oChild.terminate();
        oThread.join(5);
        return False;
    if iStatus is not TBS_EXITCODE_SYNTAX:
        testboxcommons.log('The new testboxscript returned %d instead of %d during check.' \
                           % (iStatus, TBS_EXITCODE_SYNTAX));
        return False;

    sOutput = b''.join(asBuf).decode('utf-8');
    sOutput = sOutput.strip();
    try:
        iNewVersion = int(sOutput);
    except:
        testboxcommons.log('The new testboxscript returned an unparseable version string: "%s"!' % (sOutput,));
        return False;
    testboxcommons.log('New script version: %s' % (iNewVersion,));
    return True;

def _doUpgradeApply(sUpgradeDir, asMembers):
    """
    # Apply the directories and files from the upgrade.
    returns True/False/Exception.
    """

    #
    # Create directories first since that's least intrusive.
    #
    for sMember in asMembers:
        if sMember[-1] == '/':
            sMember = sMember[len('testboxscript/'):];
            if sMember != '':
                sFull = os.path.join(g_ksValidationKitDir, sMember);
                if not os.path.isdir(sFull):
                    os.makedirs(sFull, 0o755);

    #
    # Move the files into place.
    #
    fRc = True;
    asOldFiles = [];
    for sMember in asMembers:
        if sMember[-1] != '/':
            sSrc = os.path.join(sUpgradeDir, sMember);
            sDst = os.path.join(g_ksValidationKitDir, sMember[len('testboxscript/'):]);

            # Move the old file out of the way first.
            sDstRm = None;
            if os.path.exists(sDst):
                testboxcommons.log2('Info: Installing "%s"' % (sDst,));
                sDstRm = '%s-delete-me-%s' % (sDst, uuid.uuid4(),);
                try:
                    os.rename(sDst, sDstRm);
                except Exception as oXcpt:
                    testboxcommons.log('Error: failed to rename (old) "%s" to "%s": %s' % (sDst, sDstRm, oXcpt));
                    try:
                        shutil.copy(sDst, sDstRm);
                    except Exception as oXcpt2:
                        testboxcommons.log('Error: failed to copy (old) "%s" to "%s": %s' % (sDst, sDstRm, oXcpt2));
                        break;
                    try:
                        os.unlink(sDst);
                    except Exception as oXcpt2:
                        testboxcommons.log('Error: failed to unlink (old) "%s": %s' % (sDst, oXcpt2));
                        break;

            # Move/copy the new one into place.
            testboxcommons.log2('Info: Installing "%s"' % (sDst,));
            try:
                os.rename(sSrc, sDst);
            except Exception as oXcpt:
                testboxcommons.log('Warning: failed to rename (new) "%s" to "%s": %s' % (sSrc, sDst, oXcpt));
                try:
                    shutil.copy(sSrc, sDst);
                except Exception as oXcpt2:
                    testboxcommons.log('Error: failed to copy (new) "%s" to "%s": %s' % (sSrc, sDst, oXcpt2));
                    fRc = False;
                    break;

    #
    # Roll back on failure.
    #
    if fRc is not True:
        testboxcommons.log('Attempting to roll back old files...');
        for sDstRm in asOldFiles:
            sDst = sDstRm[:sDstRm.rfind('-delete-me')];
            testboxcommons.log2('Info: Rolling back "%s" (%s)' % (sDst, os.path.basename(sDstRm)));
            try:
                shutil.move(sDstRm, sDst);
            except:
                testboxcommons.log('Error: failed to rollback "%s" onto "%s": %s' % (sDstRm, sDst, oXcpt));
        return False;
    return True;

def _doUpgradeRemoveOldStuff(sUpgradeDir, asMembers):
    """
    Clean up all obsolete files and directories.
    Returns True (shouldn't fail or raise any exceptions).
    """

    try:
        shutil.rmtree(sUpgradeDir, ignore_errors = True);
    except:
        pass;

    asKnownFiles = [];
    asKnownDirs  = [];
    for sMember in asMembers:
        sMember = sMember[len('testboxscript/'):];
        if sMember == '':
            continue;
        if sMember[-1] == '/':
            asKnownDirs.append(os.path.normpath(os.path.join(g_ksValidationKitDir, sMember[:-1])));
        else:
            asKnownFiles.append(os.path.normpath(os.path.join(g_ksValidationKitDir, sMember)));

    for sDirPath, asDirs, asFiles in os.walk(g_ksValidationKitDir, topdown=False):
        for sDir in asDirs:
            sFull = os.path.normpath(os.path.join(sDirPath, sDir));
            if sFull not in asKnownDirs:
                testboxcommons.log2('Info: Removing obsolete directory "%s"' % (sFull,));
                try:
                    os.rmdir(sFull);
                except Exception as oXcpt:
                    testboxcommons.log('Warning: failed to rmdir obsolete dir "%s": %s' % (sFull, oXcpt));

        for sFile in asFiles:
            sFull = os.path.normpath(os.path.join(sDirPath, sFile));
            if sFull not in asKnownFiles:
                testboxcommons.log2('Info: Removing obsolete file "%s"' % (sFull,));
                try:
                    os.unlink(sFull);
                except Exception as oXcpt:
                    testboxcommons.log('Warning: failed to unlink obsolete file "%s": %s' % (sFull, oXcpt));
    return True;

def upgradeFromZip(sZipFile):
    """
    Upgrade the testboxscript install using the specified zip file.
    Returns True/False.
    """

    # A little precaution.
    if utils.isRunningFromCheckout():
        testboxcommons.log('Use "svn up" to "upgrade" your source tree!');
        return False;

    #
    # Prepare.
    #
    # Note! Don't bother cleaning up files and dirs in the error paths,
    #       they'll be restricted to the one zip and the one upgrade dir.
    #       We'll remove them next time we upgrade.
    #
    oZip = zipfile.ZipFile(sZipFile, 'r');                  # No 'with' support in 2.6 class: pylint: disable=consider-using-with
    asMembers = _doUpgradeCheckZip(oZip);
    if asMembers is None:
        return False;

    sUpgradeDir = os.path.join(g_ksTestScriptDir, 'upgrade');
    testboxcommons.log('Unzipping "%s" to "%s"...' % (sZipFile, sUpgradeDir));
    if _doUpgradeUnzipAndCheck(oZip, sUpgradeDir, asMembers) is not True:
        return False;
    oZip.close();

    if _doUpgradeTestRun(sUpgradeDir) is not True:
        return False;

    #
    # Execute.
    #
    if _doUpgradeApply(sUpgradeDir, asMembers) is not True:
        return False;
    _doUpgradeRemoveOldStuff(sUpgradeDir, asMembers);
    return True;


# For testing purposes.
if __name__ == '__main__':
    sys.exit(upgradeFromZip(sys.argv[1]));

