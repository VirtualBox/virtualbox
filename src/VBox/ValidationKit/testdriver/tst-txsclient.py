# -*- coding: utf-8 -*-
# $Id: tst-txsclient.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Simple testcase for txsclient.py.
"""

from __future__ import print_function;

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

# Standard python imports.
import os
import sys

# Validation Kit imports.
sys.path.insert(0, '.');
sys.path.insert(0, '..');
from common     import utils;
from testdriver import txsclient;
from testdriver import reporter;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    long = int;     # pylint: disable=redefined-builtin,invalid-name

g_cTests = 0;
g_cFailures = 0

def boolRes(rc, fExpect = True):
    """Checks a boolean result."""
    global g_cTests, g_cFailures;
    g_cTests = g_cTests + 1;
    if isinstance(rc, bool):
        if rc == fExpect:
            return 'PASSED';
    g_cFailures = g_cFailures + 1;
    return 'FAILED';

def stringRes(rc, sExpect):
    """Checks a string result."""
    global g_cTests, g_cFailures;
    g_cTests = g_cTests + 1;
    if utils.isString(rc):
        if rc == sExpect:
            return 'PASSED';
    g_cFailures = g_cFailures + 1;
    return 'FAILED';

def main(asArgs): # pylint: disable=missing-docstring,too-many-locals,too-many-statements
    cMsTimeout      = long(30*1000);
    sAddress        = 'localhost';
    uPort           = None;
    fReversedSetup  = False;
    fReboot         = False;
    fShutdown       = False;
    fStdTests       = True;

    i = 1;
    while i < len(asArgs):
        if asArgs[i] == '--hostname':
            sAddress = asArgs[i + 1];
            i = i + 2;
        elif asArgs[i] == '--port':
            uPort = int(asArgs[i + 1]);
            i = i + 2;
        elif asArgs[i] == '--reversed-setup':
            fReversedSetup = True;
            i = i + 1;
        elif asArgs[i] == '--timeout':
            cMsTimeout = long(asArgs[i + 1]);
            i = i + 2;
        elif asArgs[i] == '--reboot':
            fReboot   = True;
            fShutdown = False;
            fStdTests = False;
            i = i + 1;
        elif asArgs[i] == '--shutdown':
            fShutdown = True;
            fReboot   = False;
            fStdTests = False;
            i = i + 1;
        elif asArgs[i] == '--help':
            print('tst-txsclient.py [--hostname <addr|name>] [--port <num>] [--timeout <cMS>] '
                  '[--reboot|--shutdown] [--reversed-setup]');
            return 0;
        else:
            print('Unknown argument: %s' % (asArgs[i]));
            return 2;

    if uPort is None:
        oSession = txsclient.openTcpSession(cMsTimeout, sAddress, fReversedSetup = fReversedSetup);
    else:
        oSession = txsclient.openTcpSession(cMsTimeout, sAddress, uPort = uPort, fReversedSetup = fReversedSetup);
    if oSession is None:
        print('openTcpSession failed');
        return 1;

    fDone = oSession.waitForTask(30*1000);
    print('connect: waitForTask -> %s, result %s' % (fDone, oSession.getResult()));
    if fDone is True and oSession.isSuccess():
        if fStdTests:
            # Get the UUID of the remote instance.
            sUuid = oSession.syncUuid();
            if sUuid is not False:
                print('%s: UUID = %s' % (boolRes(True), sUuid));
            else:
                print('%s: UUID' % (boolRes(False),));

            # Create and remove a directory on the scratch area.
            rc = oSession.syncMkDir('${SCRATCH}/testdir1');
            print('%s: MKDIR(${SCRATCH}/testdir1) -> %s' % (boolRes(rc), rc));

            rc = oSession.syncIsDir('${SCRATCH}/testdir1');
            print('%s: ISDIR(${SCRATCH}/testdir1) -> %s' % (boolRes(rc), rc));

            rc = oSession.syncRmDir('${SCRATCH}/testdir1');
            print('%s: RMDIR(${SCRATCH}/testdir1) -> %s' % (boolRes(rc), rc));

            # Create a two-level subdir.
            rc = oSession.syncMkDirPath('${SCRATCH}/testdir2/subdir1');
            print('%s: MKDRPATH(${SCRATCH}/testdir2/subdir1) -> %s' % (boolRes(rc), rc));

            rc = oSession.syncIsDir('${SCRATCH}/testdir2');
            print('%s: ISDIR(${SCRATCH}/testdir2) -> %s' % (boolRes(rc), rc));
            rc = oSession.syncIsDir('${SCRATCH}/testdir2/');
            print('%s: ISDIR(${SCRATCH}/testdir2/) -> %s' % (boolRes(rc), rc));
            rc = oSession.syncIsDir('${SCRATCH}/testdir2/subdir1');
            print('%s: ISDIR(${SCRATCH}/testdir2/subdir1) -> %s' % (boolRes(rc), rc));

            rc = oSession.syncRmTree('${SCRATCH}/testdir2');
            print('%s: RMTREE(${SCRATCH}/testdir2) -> %s' % (boolRes(rc), rc));

            # Check out a simple file.
            rc = oSession.syncUploadString('howdy', '${SCRATCH}/howdyfile');
            print('%s: PUT FILE(${SCRATCH}/howdyfile) -> %s' % (boolRes(rc), rc));

            rc = oSession.syncUploadString('howdy-replaced', '${SCRATCH}/howdyfile');
            print('%s: PUT FILE(${SCRATCH}/howdyfile) -> %s' % (boolRes(rc), rc));

            rc = oSession.syncDownloadString('${SCRATCH}/howdyfile');
            print('%s: GET FILE(${SCRATCH}/howdyfile) -> "%s" expected "howdy-replaced"' % (stringRes(rc, 'howdy-replaced'), rc));

            rc = oSession.syncIsFile('${SCRATCH}/howdyfile');
            print('%s: ISFILE(${SCRATCH}/howdyfile) -> %s' % (boolRes(rc), rc));
            rc = oSession.syncIsDir('${SCRATCH}/howdyfile');
            print('%s: ISDIR(${SCRATCH}/howdyfile) -> %s' % (boolRes(rc, False), rc));
            rc = oSession.syncIsSymlink('${SCRATCH}/howdyfile');
            print('%s: ISSYMLNK(${SCRATCH}/howdyfile) -> %s' % (boolRes(rc, False), rc));

            rc = oSession.syncRmFile('${SCRATCH}/howdyfile');
            print('%s: RMFILE(${SCRATCH}/howdyfile) -> %s' % (boolRes(rc), rc));

            # Unicode filename (may or may not work, LANG/LC_TYPE dependent on some hosts).
            rc = oSession.syncUploadString('howdy', u'${SCRATCH}/Schröder');
            print((u'%s: PUT FILE(${SCRATCH}/Schröder) -> %s' % (boolRes(rc), rc)).encode('ascii', 'replace'));

            rc = oSession.syncIsFile(u'${SCRATCH}/Schröder');
            print((u'%s: ISFILE(${SCRATCH}/Schröder) -> %s' % (boolRes(rc), rc)).encode('ascii', 'replace'));

            rc = oSession.syncRmFile(u'${SCRATCH}/Schröder');
            print((u'%s: RMFILE(${SCRATCH}/Schröder) -> %s' % (boolRes(rc), rc)).encode('ascii', 'replace'));

            # Finally, some file uploading and downloading with unicode filenames.
            strUpFile  = 'tst-txsclient-upload.bin';
            strDwnFile = 'tst-txsclient-download.bin';
            try:
                abRandFile = os.urandom(257897);
            except:
                print('INFO: no urandom... falling back on a simple string.');
                abRandFile = 'asdflkjasdlfkjasdlfkjq023942relwjgkna9epr865u2nm345;hndafgoukhasre5kb2453km';
                for i in range(1, 64):
                    abRandFile += abRandFile;
            try:
                oLocalFile = utils.openNoInherit(strUpFile, 'w+b');
                oLocalFile.write(abRandFile);
                oLocalFile.close();
                rc = True;
            except:
                rc = False;
                print('%s: creating file (%s) to upload failed....' % (boolRes(rc), strUpFile));

            if rc is True:
                rc = oSession.syncUploadFile(strUpFile, '${SCRATCH}/tst-txsclient-uploaded.bin')
                print('%s: PUT FILE(%s, ${SCRATCH}/tst-txsclient-uploaded.bin) -> %s' % (boolRes(rc), strUpFile, rc));

                rc = oSession.syncDownloadFile('${SCRATCH}/tst-txsclient-uploaded.bin', strDwnFile)
                print('%s: GET FILE(${SCRATCH}/tst-txsclient-uploaded.bin, tst-txsclient-downloaded.txt) -> %s'
                      % (boolRes(rc), rc));

                try:
                    oLocalFile = utils.openNoInherit(strDwnFile, "rb");
                    abDwnFile = oLocalFile.read();
                    oLocalFile.close();
                    if abRandFile == abDwnFile:
                        print('%s: downloaded file matches the uploaded file' % (boolRes(True),));
                    else:
                        print('%s: downloaded file does not match the uploaded file' % (boolRes(False),));
                        print('abRandFile=%s' % (abRandFile,));
                        print('abDwnFile =%s' % (abRandFile,));
                except:
                    print('%s: reading downloaded file (%s) failed....' % (boolRes(False), strDwnFile));

                rc = oSession.syncRmFile(u'${SCRATCH}/tst-txsclient-uploaded.bin');
                print('%s: RMFILE(${SCRATCH}/tst-txsclient-uploaded.bin) -> %s' % (boolRes(rc), rc));

            try:    os.remove(strUpFile);
            except: pass;
            try:    os.remove(strDwnFile);
            except: pass;

            # Execute some simple thing, if available.
            # Intentionally skip this test if file is not available due to
            # another inserted CD-ROM (e.g. not TestSuite.iso).
            sProg = '${CDROM}/${OS/ARCH}/NetPerf${EXESUFF}';
            rc = oSession.syncIsFile(sProg, 30 * 1000, True);
            if rc is True:
                rc = oSession.syncExecEx(sProg, (sProg, '--help'));
                print('%s: EXEC(%s ${SCRATCH}) -> %s' % (boolRes(rc), sProg, rc));

                rc = oSession.syncExecEx(sProg, (sProg, 'there', 'is no such', 'parameter'), \
                                         oStdOut='${SCRATCH}/stdout', \
                                         oStdErr='${SCRATCH}/stderr');
                print('%s: EXEC(%s there is not such parameter > ${SCRATCH}/stdout 2> ${SCRATCH}/stderr) -> %s'
                      % (boolRes(rc, False), sProg, rc));

                rc = oSession.syncDownloadString('${SCRATCH}/stdout');
                print('INFO:   GET FILE(${SCRATCH}/stdout) -> "%s"' % (rc));
                rc = oSession.syncDownloadString('${SCRATCH}/stderr');
                print('INFO:   GET FILE(${SCRATCH}/stderr) -> "%s"' % (rc));

                print('TESTING: syncExec...');
                rc = oSession.syncExec(sProg, (sProg, '--version'));
                print('%s: EXEC(%s --version) -> %s' % (boolRes(rc), sProg, rc));

                print('TESTING: syncExec...');
                rc = oSession.syncExec(sProg, (sProg, '--help'));
                print('%s: EXEC(%s --help) -> %s' % (boolRes(rc), sProg, rc));

                #print('TESTING: syncExec sleep 30...'
                #rc = oSession.syncExec('/usr/bin/sleep', ('/usr/bin/sleep', '30')));
                #print('%s: EXEC(/bin/sleep 30) -> %s' % (boolRes(rc), rc));
            else:
                print('SKIP:   Execution of %s skipped, does not exist on CD-ROM' % (sProg,));

            # Execute a non-existing file on CD-ROM.
            sProg = '${CDROM}/${OS/ARCH}/NonExisting${EXESUFF}';
            rc = oSession.syncExecEx(sProg, (sProg,), oStdIn = '/dev/null', oStdOut = '/dev/null', \
                                     oStdErr = '/dev/null', oTestPipe = '/dev/null', \
                                     sAsUser = '', cMsTimeout = 3600000, fIgnoreErrors = True);
            if rc is None:
                rc = True;
            else:
                reporter.error('Unexpected value \"%s\" while executing non-existent file "%s"' % (rc, sProg));
            print('%s: EXEC(%s ${SCRATCH}) -> %s' % (boolRes(rc), sProg, rc));

            # Done
            rc = oSession.syncDisconnect();
            print('%s: disconnect() -> %s' % (boolRes(rc), rc));

        elif fReboot:
            print('TESTING: syncReboot...');
            rc = oSession.syncReboot();
            print('%s: REBOOT() -> %s' % (boolRes(rc), rc));
        elif fShutdown:
            print('TESTING: syncShutdown...');
            rc = oSession.syncShutdown();
            print('%s: SHUTDOWN() -> %s' % (boolRes(rc), rc));


    if g_cFailures != 0:
        print('tst-txsclient.py: %u out of %u test failed' % (g_cFailures, g_cTests));
        return 1;
    print('tst-txsclient.py: all %u tests passed!' % (g_cTests));
    return 0;


if __name__ == '__main__':
    reporter.incVerbosity();
    reporter.incVerbosity();
    reporter.incVerbosity();
    reporter.incVerbosity();
    sys.exit(main(sys.argv));

