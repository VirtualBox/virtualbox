
#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
VirtualBox Validation Kit - VMDK raw disk tests.
"""

__copyright__ = \
"""
Copyright (C) 2013-2024 Oracle and/or its affiliates.

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
__version__ = "$Id: tdStorageRawDrive1.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $"

# Standard Python imports.
import os;
import re;
import sys;

# Only the main script needs to modify the path.
try:    __file__                            # pylint: disable=used-before-assignment
except: __file__ = sys.argv[0];
g_ksValidationKitDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))));
sys.path.append(g_ksValidationKitDir);

# Validation Kit imports.
from common     import utils;
from testdriver import reporter;
from testdriver import base;
from testdriver import vbox;
from testdriver import vboxcon;
from testdriver import vboxtestvms;
from testdriver import vboxwrappers;


class tdStorageRawDriveOs(vboxtestvms.BaseTestVm):
    """
    Base autostart helper class to provide common methods.
    """
    # pylint: disable=too-many-arguments
    def __init__(self, oSet, oTstDrv, sVmName, sKind, sHdd, eNic0Type = None, cMbRam = None,  \
                 cCpus = 1, fPae = None, sGuestAdditionsIso = None, sBootSector = None):
        vboxtestvms.BaseTestVm.__init__(self, sVmName, oSet = oSet, sKind = sKind);
        self.oTstDrv = oTstDrv;
        self.sHdd = sHdd;
        self.eNic0Type = eNic0Type;
        self.cMbRam = cMbRam;
        self.cCpus = cCpus;
        self.fPae = fPae;
        self.sGuestAdditionsIso = sGuestAdditionsIso;
        self.asTestBuildDirs = oTstDrv.asTestBuildDirs;
        self.sVBoxInstaller = "";
        self.sVMDKPath='/home/vbox/vmdk';
        self.asVirtModesSup = ['hwvirt-np',];
        self.asParavirtModesSup = ['default',];
        self.sBootSector = sBootSector;
        self.sPathDelimiter = '/';

        # Had to move it here from oTestDrv because the output is platform-dependent
        self.asHdds = \
            { '6.1/storage/t-mbr.vdi' :
                {
                    'Header' :
                        {
                            #Drive:       /dev/sdb
                            'Model'       : '"ATA VBOX HARDDISK"',
                            'UUID'        : '62d4f394-0000-0000-0000-000000000000',
                            'Size'        : '2.0GiB',
                            'Sector Size' : '512 bytes',
                            'Scheme'      : 'MBR',
                        },
                    'Partitions' :
                        {
                            'Partitions'  :
                                [
                                    '$(1)   07    10.0MiB    1.0MiB     0/ 32/33      1/102/37   no   IFS',
                                    '$(2)   83    10.0MiB   11.0MiB     5/ 93/33     11/ 29/14   no   Linux',
                                    '$(3)   07    10.0MiB   21.0MiB     2/172/43      3/242/47   no   IFS',
                                    '$(4)   07    10.0MiB   32.0MiB     4/ 20/17      5/ 90/21   no   IFS',
                                    '$(5)   83    10.0MiB   43.0MiB     5/122/54      6/192/58   no   Linux',
                                    '$(6)   07    10.0MiB   54.0MiB     6/225/28      8/ 40/32   no   IFS',
                                    '$(7)   83    10.0MiB   65.0MiB     8/ 73/ 2      9/143/ 6   no   Linux',
                                    '$(8)   07     1.9GiB   76.0MiB     9/175/39    260/243/47   no   IFS',
                                ],
                            'PartitionNumbers' : [1, 2, 3, 5, 6, 7, 8, 9],
                        },
                } ,
              '6.1/storage/t-gpt.vdi' :
                {
                    'Header' :
                        {
                            #Drive:       /dev/sdc
                            'Model'       : '"ATA VBOX HARDDISK"',
                            'UUID'        : '7b642ab1-9d44-b844-a860-ce71e0686274',
                            'Size'        : '2.0GiB',
                            'Sector Size' : '512 bytes',
                            'Scheme'      : 'GPT',
                        },
                    'Partitions'  :
                        {
                            'Partitions'  :
                                [
                                    '$(1)  WindowsBasicData  560b261d-081f-fb4a-8df8-c64fffcb2bd1   10.0MiB    1.0MiB  off',
                                    '$(2)  LinuxData         629f66be-0254-7c4f-a328-cc033e4de124   10.0MiB   11.0MiB  off',
                                    '$(3)  WindowsBasicData  d3f56c96-3b28-7f44-a53d-85b8bc93bd91   10.0MiB   21.0MiB  off',
                                    '$(4)  LinuxData         27c0f5ad-74c8-d54f-835f-06e51b3f10ef   10.0MiB   31.0MiB  off',
                                    '$(5)  WindowsBasicData  6cf1fdf0-b2ae-3849-9cfa-c056f9d8b722   10.0MiB   41.0MiB  off',
                                    '$(6)  LinuxData         017bcbed-8b96-be4d-925a-2f872194fbe6   10.0MiB   51.0MiB  off',
                                    '$(7)  WindowsBasicData  af6c4f89-8fc3-5049-9d98-3e2e98061073   10.0MiB   61.0MiB  off',
                                    '$(8)  LinuxData         9704d7cd-810f-4d44-ac78-432ebc16143f   10.0MiB   71.0MiB  off',
                                    '$(9)  WindowsBasicData  a05f8e09-f9e7-5b4e-bb4e-e9f8fde3110e    1.9GiB   81.0MiB  off',
                                ],
                            'PartitionNumbers' : [1, 2, 3, 4, 5, 6, 7, 8, 9],
                        },

                }
            };
        self.asActions = \
            [
                {
                    'action'     : 'whole drive',
                    'options'    : [],
                    'data-crc'   : {},
                    'createType' : 'fullDevice',
                    'extents'    : { '6.1/storage/t-mbr.vdi' : ['RW 0 FLAT "$(disk)" 0',],
                                     '6.1/storage/t-gpt.vdi' : ['RW 0 FLAT "$(disk)" 0',],
                                   },
                },
                {
                    'action'     : '1 partition',
                    'options'    : ['--property', 'Partitions=1'],
                    'data-crc'   : {'6.1/storage/t-mbr.vdi' : 2681429243,
                                    '6.1/storage/t-gpt.vdi' : 1391394051,
                                   },
                    'createType' : 'partitionedDevice',
                    'extents'    : { '6.1/storage/t-mbr.vdi' :
                                        ['RW 2048 FLAT "vmdktest-pt.vmdk" 0',
                                         'RW 20480 FLAT "$(disk)" 2048',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 2048',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 4096',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 6144',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 8192',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 10240',
                                         'RW 4036608 ZERO',
                                         'RW 36028797014771712 ZERO',
                                        ],
                                     '6.1/storage/t-gpt.vdi' :
                                        ['RW 1 FLAT "vmdktest-pt.vmdk" 0',
                                         'RW 2047 FLAT "vmdktest-pt.vmdk" 1',
                                         'RW 20480 FLAT "$(disk)" 2048',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 4026368 ZERO',
                                         'RW 36028797014771712 ZERO',
                                        ],
                                   },
                },
                {
                    'action'     : '2 partitions',
                    'options'    : ['--property', 'Partitions=1,$(4)'],
                    'data-crc'   : {'6.1/storage/t-mbr.vdi' : 2681429243,
                                    '6.1/storage/t-gpt.vdi' : 1391394051,
                                   },
                    'createType' : 'partitionedDevice',
                    'extents'    : { '6.1/storage/t-mbr.vdi' :
                                        ['RW 2048 FLAT "vmdktest-pt.vmdk" 0',
                                         'RW 20480 FLAT "$(disk)" 2048',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 2048',
                                         'RW 20480 FLAT "$(disk)" 65536',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 4096',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 6144',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 8192',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 10240',
                                         'RW 4036608 ZERO',
                                         'RW 36028797014771712 ZERO',
                                        ],
                                     '6.1/storage/t-gpt.vdi' :
                                        ['RW 1 FLAT "vmdktest-pt.vmdk" 0',
                                         'RW 2047 FLAT "vmdktest-pt.vmdk" 1',
                                         'RW 20480 FLAT "$(disk)" 2048',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 20480 FLAT "$(disk)" 63488',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 4026368 ZERO',
                                         'RW 36028797014771712 ZERO',
                                        ],
                                   },
                },
                {
                    'action'     : '1 partition with boot sector',
                    'options'    : ['--property', 'Partitions=1',
                                    '--property-file', 'BootSector=$(bootsector)'],
                    'data-crc'   : {'6.1/storage/t-mbr.vdi' : 3980784439,
                                    '6.1/storage/t-gpt.vdi' : 1152317131,
                                   },
                    'createType' : 'partitionedDevice',
                    'extents'    : { '6.1/storage/t-mbr.vdi' :
                                        ['RW 2048 FLAT "vmdktest-pt.vmdk" 0',
                                         'RW 20480 FLAT "$(disk)" 2048',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 2048',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 4096',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 6144',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 8192',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 10240',
                                         'RW 4036608 ZERO',
                                         'RW 36028797014771712 ZERO',
                                        ],
                                     '6.1/storage/t-gpt.vdi' :
                                        ['RW 1 FLAT "vmdktest-pt.vmdk" 0',
                                         'RW 2047 FLAT "vmdktest-pt.vmdk" 1',
                                         'RW 20480 FLAT "$(disk)" 2048',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 4026368 ZERO',
                                         'RW 36028797014771712 ZERO',
                                        ],
                                   },
                },
                {
                    'action'     : '2 partitions with boot sector',
                    'options'    : ['--property', 'Partitions=1,$(4)',
                                    '--property-file', 'BootSector=$(bootsector)'],
                    'data-crc'   : {'6.1/storage/t-mbr.vdi' : 3980784439,
                                    '6.1/storage/t-gpt.vdi' : 1152317131,
                                   },
                    'createType' : 'partitionedDevice',
                    'extents'    : { '6.1/storage/t-mbr.vdi' :
                                        ['RW 2048 FLAT "vmdktest-pt.vmdk" 0',
                                         'RW 20480 FLAT "$(disk)" 2048',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 2048',
                                         'RW 20480 FLAT "$(disk)" 65536',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 4096',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 6144',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 8192',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 10240',
                                         'RW 4036608 ZERO',
                                         'RW 36028797014771712 ZERO',
                                        ],
                                     '6.1/storage/t-gpt.vdi' :
                                        ['RW 1 FLAT "vmdktest-pt.vmdk" 0',
                                         'RW 2047 FLAT "vmdktest-pt.vmdk" 1',
                                         'RW 20480 FLAT "$(disk)" 2048',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 20480 FLAT "$(disk)" 63488',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 4026368 ZERO',
                                         'RW 36028797014771712 ZERO',
                                        ],
                                   },
                },
                {
                    'action'     : '1 partition with relative names',
                    'options'    : ['--property', 'Partitions=1', '--property', 'Relative=1'],
                    'data-crc'   : {'6.1/storage/t-mbr.vdi' : 2681429243,
                                    '6.1/storage/t-gpt.vdi' : 1391394051,
                                   },
                    'createType' : 'partitionedDevice',
                    'extents'    : { '6.1/storage/t-mbr.vdi' :
                                        ['RW 2048 FLAT "vmdktest-pt.vmdk" 0',
                                         'RW 20480 FLAT "$(part)1" 0',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 2048',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 4096',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 6144',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 8192',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 10240',
                                         'RW 4036608 ZERO',
                                         'RW 36028797014771712 ZERO',
                                        ],
                                     '6.1/storage/t-gpt.vdi' :
                                        ['RW 1 FLAT "vmdktest-pt.vmdk" 0',
                                         'RW 2047 FLAT "vmdktest-pt.vmdk" 1',
                                         'RW 20480 FLAT "$(part)1" 0',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 4026368 ZERO',
                                         'RW 36028797014771712 ZERO',
                                        ],
                                   },
                },
                {
                    'action'     : '2 partitions with relative names',
                    'options'    : ['--property', 'Partitions=1,$(4)', '--property', 'Relative=1'],
                    'data-crc'   : {'6.1/storage/t-mbr.vdi' : 2681429243,
                                    '6.1/storage/t-gpt.vdi' : 1391394051,
                                   },
                    'createType' : 'partitionedDevice',
                    'extents'    : { '6.1/storage/t-mbr.vdi' :
                                        ['RW 2048 FLAT "vmdktest-pt.vmdk" 0',
                                         'RW 20480 FLAT "$(part)1" 0',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 2048',
                                         'RW 20480 FLAT "$(part)$(4)" 0',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 4096',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 6144',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 8192',
                                         'RW 20480 ZERO',
                                         'RW 2048 FLAT "vmdktest-pt.vmdk" 10240',
                                         'RW 4036608 ZERO',
                                         'RW 36028797014771712 ZERO',
                                        ],
                                     '6.1/storage/t-gpt.vdi' :
                                        ['RW 1 FLAT "vmdktest-pt.vmdk" 0',
                                         'RW 2047 FLAT "vmdktest-pt.vmdk" 1',
                                         'RW 20480 FLAT "$(part)1" 0',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 20480 FLAT "$(part)$(4)" 0',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 20480 ZERO',
                                         'RW 4026368 ZERO',
                                         'RW 36028797014771712 ZERO',
                                        ],
                                   },
                },
            ];


    def _findFile(self, sRegExp, asTestBuildDirs):
        """
        Returns a filepath based on the given regex and paths to look into
        or None if no matching file is found.
        """
        oRegExp = re.compile(sRegExp);
        for sTestBuildDir in asTestBuildDirs:
            try:
                #return most recent file if there are several ones matching the pattern
                asFiles = [s for s in os.listdir(sTestBuildDir)
                           if os.path.isfile(os.path.join(sTestBuildDir, s))];
                asFiles = (s for s in asFiles
                           if oRegExp.match(os.path.basename(s))
                           and os.path.exists(sTestBuildDir + '/' + s));
                asFiles = sorted(asFiles, reverse = True,
                                 key = lambda s, sTstBuildDir = sTestBuildDir: os.path.getmtime(os.path.join(sTstBuildDir, s)));
                if asFiles:
                    return sTestBuildDir + '/' + asFiles[0];
            except:
                pass;
        reporter.error('Failed to find a file matching "%s" in %s.' % (sRegExp, ','.join(asTestBuildDirs)));
        return None;

    def _waitAdditionsIsRunning(self, oGuest, fWaitTrayControl):
        """
        Check is the additions running
        """
        cAttempt = 0;
        fRc = False;
        while cAttempt < 30:
            fRc = oGuest.additionsRunLevel in [vboxcon.AdditionsRunLevelType_Userland,
                                               vboxcon.AdditionsRunLevelType_Desktop];
            if fRc:
                eServiceStatus, _ = oGuest.getFacilityStatus(vboxcon.AdditionsFacilityType_VBoxService);
                fRc = eServiceStatus == vboxcon.AdditionsFacilityStatus_Active;
                if fRc and not fWaitTrayControl:
                    break;
                if fRc:
                    eServiceStatus, _ = oGuest.getFacilityStatus(vboxcon.AdditionsFacilityType_VBoxTrayClient);
                    fRc = eServiceStatus == vboxcon.AdditionsFacilityStatus_Active;
                    if fRc:
                        break;
            self.oTstDrv.sleep(10);
            cAttempt += 1;
        return fRc;

    def createSession(self, oSession, sName, sUser, sPassword, cMsTimeout = 10 * 1000, fIsError = True):
        """
        Creates (opens) a guest session.
        Returns (True, IGuestSession) on success or (False, None) on failure.
        """
        oGuest = oSession.o.console.guest;
        if sName is None:
            sName = "<untitled>";
        reporter.log('Creating session "%s" ...' % (sName,));
        try:
            oGuestSession = oGuest.createSession(sUser, sPassword, '', sName);
        except:
            # Just log, don't assume an error here (will be done in the main loop then).
            reporter.maybeErrXcpt(fIsError, 'Creating a guest session "%s" failed; sUser="%s", pw="%s"'
                                  % (sName, sUser, sPassword));
            return (False, None);
        reporter.log('Waiting for session "%s" to start within %dms...' % (sName, cMsTimeout));
        aeWaitFor = [ vboxcon.GuestSessionWaitForFlag_Start, ];
        try:
            waitResult = oGuestSession.waitForArray(aeWaitFor, cMsTimeout);
            #
            # Be nice to Guest Additions < 4.3: They don't support session handling and
            # therefore return WaitFlagNotSupported.
            #
            if waitResult not in (vboxcon.GuestSessionWaitResult_Start, vboxcon.GuestSessionWaitResult_WaitFlagNotSupported):
                # Just log, don't assume an error here (will be done in the main loop then).
                reporter.maybeErr(fIsError, 'Session did not start successfully, returned wait result: %d' % (waitResult,));
                return (False, None);
            reporter.log('Session "%s" successfully started' % (sName,));
        except:
            # Just log, don't assume an error here (will be done in the main loop then).
            reporter.maybeErrXcpt(fIsError, 'Waiting for guest session "%s" (usr=%s;pw=%s) to start failed:'
                                  % (sName, sUser, sPassword,));
            return (False, None);
        return (True, oGuestSession);

    def closeSession(self, oGuestSession, fIsError = True):
        """
        Closes the guest session.
        """
        if oGuestSession is not None:
            try:
                sName = oGuestSession.name;
            except:
                return reporter.errorXcpt();
            reporter.log('Closing session "%s" ...' % (sName,));
            try:
                oGuestSession.close();
                oGuestSession = None;
            except:
                # Just log, don't assume an error here (will be done in the main loop then).
                reporter.maybeErrXcpt(fIsError, 'Closing guest session "%s" failed:' % (sName,));
                return False;
        return True;

    def guestProcessExecute(self, oGuestSession, sTestName, cMsTimeout, sExecName, asArgs = (),
                            fGetStdOut = True, fIsError = True):
        """
        Helper function to execute a program on a guest, specified in the current test.
        Returns (True, ProcessStatus, ProcessExitCode, ProcessStdOutBuffer) on success or (False, 0, 0, None) on failure.
        """
        _ = sTestName;
        fRc = True; # Be optimistic.
        reporter.log2('Using session user=%s, name=%s, timeout=%d'
                      % (oGuestSession.user, oGuestSession.name, oGuestSession.timeout,));
        #
        # Start the process:
        #
        reporter.log2('Executing sCmd=%s, timeoutMS=%d, asArgs=%s'
                      % (sExecName, cMsTimeout, asArgs, ));
        fTaskFlags = [];
        if fGetStdOut:
            fTaskFlags = [vboxcon.ProcessCreateFlag_WaitForStdOut,
                          vboxcon.ProcessCreateFlag_WaitForStdErr];
        try:
            oProcess = oGuestSession.processCreate(sExecName,
                                                   asArgs if self.oTstDrv.fpApiVer >= 5.0 else asArgs[1:],
                                                   [], fTaskFlags, cMsTimeout);
        except:
            reporter.maybeErrXcpt(fIsError, 'asArgs=%s' % (asArgs,));
            return (False, 0, 0, None);
        if oProcess is None:
            return (reporter.error('oProcess is None! (%s)' % (asArgs,)), 0, 0, None);
        #time.sleep(5); # try this if you want to see races here.
        # Wait for the process to start properly:
        reporter.log2('Process start requested, waiting for start (%dms) ...' % (cMsTimeout,));
        iPid = -1;
        aeWaitFor = [ vboxcon.ProcessWaitForFlag_Start, ];
        aBuf = None;
        try:
            eWaitResult = oProcess.waitForArray(aeWaitFor, cMsTimeout);
        except:
            reporter.maybeErrXcpt(fIsError, 'waitforArray failed for asArgs=%s' % (asArgs,));
            fRc = False;
        else:
            try:
                eStatus = oProcess.status;
                iPid    = oProcess.PID;
            except:
                fRc = reporter.errorXcpt('asArgs=%s' % (asArgs,));
            else:
                reporter.log2('Wait result returned: %d, current process status is: %d' % (eWaitResult, eStatus,));
                #
                # Wait for the process to run to completion if necessary.
                #
                # Note! The above eWaitResult return value can be ignored as it will
                #       (mostly) reflect the process status anyway.
                #
                if eStatus == vboxcon.ProcessStatus_Started:
                    # What to wait for:
                    aeWaitFor = [ vboxcon.ProcessWaitForFlag_Terminate,
                                  vboxcon.ProcessWaitForFlag_StdOut,
                                  vboxcon.ProcessWaitForFlag_StdErr];
                    reporter.log2('Process (PID %d) started, waiting for termination (%dms), aeWaitFor=%s ...'
                                  % (iPid, cMsTimeout, aeWaitFor));
                    acbFdOut = [0,0,0];
                    while True:
                        try:
                            eWaitResult = oProcess.waitForArray(aeWaitFor, cMsTimeout);
                        except KeyboardInterrupt: # Not sure how helpful this is, but whatever.
                            reporter.error('Process (PID %d) execution interrupted' % (iPid,));
                            try: oProcess.close();
                            except: pass;
                            break;
                        except:
                            fRc = reporter.errorXcpt('asArgs=%s' % (asArgs,));
                            break;
                        reporter.log2('Wait returned: %d' % (eWaitResult,));
                        # Process output:
                        for eFdResult, iFd, sFdNm in [ (vboxcon.ProcessWaitResult_StdOut, 1, 'stdout'),
                                                       (vboxcon.ProcessWaitResult_StdErr, 2, 'stderr'), ]:
                            if eWaitResult in (eFdResult, vboxcon.ProcessWaitResult_WaitFlagNotSupported):
                                reporter.log2('Reading %s ...' % (sFdNm,));
                                try:
                                    abBuf = oProcess.read(iFd, 64 * 1024, cMsTimeout);
                                except KeyboardInterrupt: # Not sure how helpful this is, but whatever.
                                    reporter.error('Process (PID %d) execution interrupted' % (iPid,));
                                    try: oProcess.close();
                                    except: pass;
                                except:
                                    pass; ## @todo test for timeouts and fail on anything else!
                                else:
                                    if abBuf:
                                        reporter.log2('Process (PID %d) got %d bytes of %s data' % (iPid, len(abBuf), sFdNm,));
                                        acbFdOut[iFd] += len(abBuf);
                                        ## @todo Figure out how to uniform + append!
                                        sBuf = '';
                                        if sys.version_info >= (2, 7) and isinstance(abBuf, memoryview):
                                            abBuf = abBuf.tobytes();
                                            sBuf  = abBuf.decode("utf-8");
                                        else:
                                            sBuf = str(abBuf);
                                        if aBuf:
                                            aBuf += sBuf;
                                        else:
                                            aBuf = sBuf;
                        ## Process input (todo):
                        #if eWaitResult in (vboxcon.ProcessWaitResult_StdIn, vboxcon.ProcessWaitResult_WaitFlagNotSupported):
                        #    reporter.log2('Process (PID %d) needs stdin data' % (iPid,));
                        # Termination or error?
                        if eWaitResult in (vboxcon.ProcessWaitResult_Terminate,
                                           vboxcon.ProcessWaitResult_Error,
                                           vboxcon.ProcessWaitResult_Timeout,):
                            try:    eStatus = oProcess.status;
                            except: fRc = reporter.errorXcpt('asArgs=%s' % (asArgs,));
                            reporter.log2('Process (PID %d) reported terminate/error/timeout: %d, status: %d'
                                          % (iPid, eWaitResult, eStatus,));
                            break;
                    # End of the wait loop.
                    _, cbStdOut, cbStdErr = acbFdOut;
                    try:    eStatus = oProcess.status;
                    except: fRc = reporter.errorXcpt('asArgs=%s' % (asArgs,));
                    reporter.log2('Final process status (PID %d) is: %d' % (iPid, eStatus));
                    reporter.log2('Process (PID %d) %d stdout, %d stderr' % (iPid, cbStdOut, cbStdErr));
        #
        # Get the final status and exit code of the process.
        #
        try:
            uExitStatus = oProcess.status;
            iExitCode   = oProcess.exitCode;
        except:
            fRc = reporter.errorXcpt('asArgs=%s' % (asArgs,));
        reporter.log2('Process (PID %d) has exit code: %d; status: %d ' % (iPid, iExitCode, uExitStatus));
        return (fRc, uExitStatus, iExitCode, aBuf);

    def uploadString(self, oGuestSession, sSrcString, sDst):
        """
        Upload the string into guest.
        """
        fRc = True;
        try:
            oFile = oGuestSession.fileOpenEx(sDst, vboxcon.FileAccessMode_ReadWrite, vboxcon.FileOpenAction_CreateOrReplace,
                                             vboxcon.FileSharingMode_All, 0, []);
        except:
            fRc = reporter.errorXcpt('Upload string failed. Could not create and open the file %s' % sDst);
        else:
            try:
                oFile.write(bytearray(sSrcString), 60*1000);
            except:
                fRc = reporter.errorXcpt('Upload string failed. Could not write the string into the file %s' % sDst);
        try:
            oFile.close();
        except:
            fRc = reporter.errorXcpt('Upload string failed. Could not close the file %s' % sDst);
        return fRc;

    def uploadFile(self, oGuestSession, sSrc, sDst):
        """
        Upload the string into guest.
        """
        fRc = True;
        try:
            if self.oTstDrv.fpApiVer >= 5.0:
                oCurProgress = oGuestSession.fileCopyToGuest(sSrc, sDst, [0]);
            else:
                oCurProgress = oGuestSession.copyTo(sSrc, sDst, [0]);
        except:
            reporter.maybeErrXcpt(True, 'Upload file exception for sSrc="%s":'
                                  % (self.sGuestAdditionsIso,));
            fRc = False;
        else:
            if oCurProgress is not None:
                oWrapperProgress = vboxwrappers.ProgressWrapper(oCurProgress, self.oTstDrv.oVBoxMgr, self.oTstDrv, "uploadFile");
                oWrapperProgress.wait();
                if not oWrapperProgress.isSuccess():
                    oWrapperProgress.logResult(fIgnoreErrors = False);
                    fRc = False;
            else:
                fRc = reporter.error('No progress object returned');
        return fRc;

    def downloadFile(self, oGuestSession, sSrc, sDst, fIgnoreErrors = False):
        """
        Get a file (sSrc) from the guest storing it on the host (sDst).
        """
        fRc = True;
        try:
            if self.oTstDrv.fpApiVer >= 5.0:
                oCurProgress = oGuestSession.fileCopyFromGuest(sSrc, sDst, [0]);
            else:
                oCurProgress = oGuestSession.copyFrom(sSrc, sDst, [0]);
        except:
            if not fIgnoreErrors:
                reporter.errorXcpt('Download file exception for sSrc="%s":' % (sSrc,));
            else:
                reporter.log('warning: Download file exception for sSrc="%s":' % (sSrc,));
            fRc = False;
        else:
            if oCurProgress is not None:
                oWrapperProgress = vboxwrappers.ProgressWrapper(oCurProgress, self.oTstDrv.oVBoxMgr,
                                                                self.oTstDrv, "downloadFile");
                oWrapperProgress.wait();
                if not oWrapperProgress.isSuccess():
                    oWrapperProgress.logResult(fIgnoreErrors);
                    fRc = False;
            else:
                if not fIgnoreErrors:
                    reporter.error('No progress object returned');
                else:
                    reporter.log('warning: No progress object returned');
                fRc = False;
        return fRc;

    def downloadFiles(self, oGuestSession, asFiles, fIgnoreErrors = False):
        """
        Convenience function to get files from the guest and stores it
        into the scratch directory for later (manual) review.
        Returns True on success.
        Returns False on failure, logged.
        """
        fRc = True;
        for sGstFile in asFiles:
            ## @todo r=bird: You need to use the guest specific path functions here.
            ##       Best would be to add basenameEx to common/pathutils.py.  See how joinEx
            ##       is used by BaseTestVm::pathJoin and such.
            sTmpFile = os.path.join(self.oTstDrv.sScratchPath, 'tmp-' + os.path.basename(sGstFile));
            reporter.log2('Downloading file "%s" to "%s" ...' % (sGstFile, sTmpFile));
            # First try to remove (unlink) an existing temporary file, as we don't truncate the file.
            try:    os.unlink(sTmpFile);
            except: pass;
            ## @todo Check for already existing files on the host and create a new
            #        name for the current file to download.
            fRc = self.downloadFile(oGuestSession, sGstFile, sTmpFile, fIgnoreErrors);
            if fRc:
                reporter.addLogFile(sTmpFile, 'misc/other', 'guest - ' + sGstFile);
            else:
                if fIgnoreErrors is not True:
                    reporter.error('error downloading file "%s" to "%s"' % (sGstFile, sTmpFile));
                    return fRc;
                reporter.log('warning: file "%s" was not downloaded, ignoring.' % (sGstFile,));
        return True;

    def _checkVmIsReady(self, oGuestSession):
        (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Start a guest process',
                                                  30 * 1000, '/sbin/ifconfig',
                                                  ['ifconfig',],
                                                  False, False);
        return fRc;

    def waitVmIsReady(self, oSession, fWaitTrayControl):
        """
        Waits the VM is ready after start or reboot.
        Returns result (true or false) and guest session obtained
        """
        _ = fWaitTrayControl;
        # Give the VM a time to reboot
        self.oTstDrv.sleep(30);
        # Waiting the VM is ready.
        # To do it, one will try to open the guest session and start the guest process in loop
        if not self._waitAdditionsIsRunning(oSession.o.console.guest, False):
            return (False, None);
        cAttempt = 0;
        oGuestSession = None;
        fRc = False;
        while cAttempt < 30:
            fRc, oGuestSession = self.createSession(oSession, 'Session for user: vbox',
                                                    'vbox', 'password', 10 * 1000, False);
            if fRc:
                fRc = self._checkVmIsReady(oGuestSession);
                if fRc:
                    break;
                self.closeSession(oGuestSession, False);
            self.oTstDrv.sleep(10);
            cAttempt += 1;
        return (fRc, oGuestSession);

    def _rebootVM(self, oGuestSession):
        (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Reboot the VM',
                                                  30 * 1000, '/usr/bin/sudo',
                                                  ['sudo', 'reboot'],
                                                  False, True);
        if not fRc:
            reporter.error('Calling the reboot utility failed');
        return fRc;

    def rebootVMAndCheckReady(self, oSession, oGuestSession):
        """
        Reboot the VM and wait the VM is ready.
        Returns result and guest session obtained after reboot
        """
        reporter.testStart('Reboot VM and wait for readiness');
        fRc = self._rebootVM(oGuestSession);
        fRc = self.closeSession(oGuestSession, True) and fRc and True; # pychecker hack.
        if fRc:
            (fRc, oGuestSession) = self.waitVmIsReady(oSession, False);
        if not fRc:
            reporter.error('VM is not ready after reboot');
        reporter.testDone();
        return (fRc, oGuestSession);

    def _powerDownVM(self, oGuestSession):
        (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Power down the VM',
                                                  30 * 1000, '/usr/bin/sudo',
                                                  ['sudo', 'poweroff'],
                                                  False, True);
        if not fRc:
            reporter.error('Calling the poweroff utility failed');
        return fRc;

    def powerDownVM(self, oGuestSession):
        """
        Power down the VM by calling guest process without wating
        the VM is really powered off. Also, closes the guest session.
        It helps the terminateBySession to stop the VM without aborting.
        """
        if oGuestSession is None:
            return False;
        reporter.testStart('Power down the VM');
        fRc = self._powerDownVM(oGuestSession);
        fRc = self.closeSession(oGuestSession, True) and fRc and True; # pychecker hack.
        if not fRc:
            reporter.error('Power down the VM failed');
        reporter.testDone();
        return fRc;

    def installAdditions(self, oSession, oGuestSession, oVM):
        """
        Installs the Windows guest additions using the test execution service.
        """
        _ = oSession;
        _ = oGuestSession;
        _ = oVM;
        reporter.error('Not implemented');
        return False;

    def installVirtualBox(self, oGuestSession):
        """
        Install VirtualBox in the guest.
        """
        _ = oGuestSession;
        reporter.error('Not implemented');
        return False;

    def getResourceSet(self):
        asRet = [];
        if not os.path.isabs(self.sHdd):
            asRet.append(self.sHdd);
        return asRet;

    def _createVmDoIt(self, oTestDrv, eNic0AttachType, sDvdImage):
        """
        Creates the VM.
        Returns Wrapped VM object on success, None on failure.
        """
        _ = eNic0AttachType;
        _ = sDvdImage;
        return oTestDrv.createTestVM(self.sVmName, self.iGroup, self.sHdd, sKind = self.sKind, \
                                     fIoApic = True, eNic0AttachType = vboxcon.NetworkAttachmentType_NAT, \
                                     eNic0Type = self.eNic0Type, cMbRam = self.cMbRam, \
                                     sHddControllerType = "SATA Controller", fPae = self.fPae, \
                                     cCpus = self.cCpus, sDvdImage = self.sGuestAdditionsIso);

    def _createVmPost(self, oTestDrv, oVM, eNic0AttachType, sDvdImage):
        _ = eNic0AttachType;
        _ = sDvdImage;
        fRc = True;
        oSession = oTestDrv.openSession(oVM);
        if oSession is not None:
            fRc = fRc and oSession.enableVirtExX86(True);
            # nested paging doesn't need for the test
            #fRc = fRc and oSession.enableNestedPagingX86(True);
            #fRc = fRc and oSession.enableNestedHwVirtX86(True);
            # disable 3D until the error is fixed.
            fRc = fRc and oSession.setAccelerate3DEnabled(False);
            fRc = fRc and oSession.setVRamSize(256);
            fRc = fRc and oSession.setVideoControllerType(vboxcon.GraphicsControllerType_VBoxSVGA);
            fRc = fRc and oSession.enableUsbOhci(True);
            fRc = fRc and oSession.enableUsbHid(True);
            fRc = fRc and oSession.saveSettings();
            fRc = oSession.close() and fRc and True; # pychecker hack.
            oSession = None;
        else:
            fRc = False;
        return oVM if fRc else None;

    def getReconfiguredVm(self, oTestDrv, cCpus, sVirtMode, sParavirtMode = None):
        #
        # Current test uses precofigured VMs. This override disables any changes in the machine.
        #
        _ = cCpus;
        _ = sVirtMode;
        _ = sParavirtMode;
        oVM = oTestDrv.getVmByName(self.sVmName);
        if oVM is None:
            return (False, None);
        return (True, oVM);

    def reattachHdd(self, oVM, sHdd, asHdds):
        """
        Attach required hdd and remove all others from asHdds list.
        """
        reporter.testStart("Reattach hdd");
        oSession = self.oTstDrv.openSession(oVM);
        fRc = False;
        if oSession is not None:
            # for simplicity and because we are using VMs having "SATA controller"
            # we will add the hdds to only "SATA controller"
            iPortNew = 0;
            fFound = False;
            try:
                aoAttachments = self.oTstDrv.oVBox.oVBoxMgr.getArray(oVM, 'mediumAttachments');
            except:
                fRc = reporter.errorXcpt();
            else:
                for oAtt in aoAttachments:
                    try:
                        sCtrl = oAtt.controller
                        iPort = oAtt.port;
                        iDev  = oAtt.device;
                        eType = oAtt.type;
                    except:
                        fRc = reporter.errorXcpt();
                        break;

                    fDetached = False;
                    if eType == vboxcon.DeviceType_HardDisk:
                        oMedium = oVM.getMedium(sCtrl, iPort, iDev);
                        if oMedium.location.endswith(sHdd):
                            fRc = True;
                            fFound = True;
                            break;
                        for sHddVar in asHdds:
                            if    oMedium.location.endswith(sHddVar) \
                               or oMedium.parent is not None and oMedium.parent.location.endswith(sHddVar) :
                                (fRc, oOldHd) = oSession.detachHd(sCtrl, iPort, iDev);
                                if fRc and oOldHd is not None:
                                    fRc = oSession.saveSettings();
                                    if oMedium.parent is not None:
                                        fRc = fRc and self.oTstDrv.oVBox.deleteHdByMedium(oOldHd);
                                    else:
                                        fRc = fRc and oOldHd.close();
                                    fRc = fRc and oSession.saveSettings();
                                fDetached = True;
                    if not fDetached and sCtrl == 'SATA Controller' and iPort + 1 > iPortNew:
                        iPortNew = iPort + 1;
            if not fFound:
                fRc = oSession.attachHd(sHdd, 'SATA Controller', iPortNew, 0);
                if fRc:
                    fRc = oSession.saveSettings();
                else:
                    oSession.discadSettings();
            fRc = oSession.close() and fRc and True; # pychecker hack
        else:
            reporter.error("Open session for '%s' failed" % self.sVmName);
            fRc = False;
        reporter.testDone();
        return fRc;

    def _callVBoxManage(self, oGuestSession, sTestName, cMsTimeout, asArgs = (),
                            fGetStdOut = True, fIsError = True):
        return self.guestProcessExecute(oGuestSession, sTestName,
                                        cMsTimeout, '/usr/bin/sudo',
                                        ['/usr/bin/sudo', '/opt/VirtualBox/VBoxManage'] + asArgs, fGetStdOut, fIsError);

    def listHostDrives(self, oGuestSession, sHdd):
        """
        Define path of the specified drive using 'VBoxManage list hostdrives'.
        """
        reporter.testStart("List host drives");
        sDrive = None;
        (fRc, _, _, aBuf) = self._callVBoxManage(oGuestSession, 'List host drives', 60 * 1000,
                                                 ['list', 'hostdrives'], True, True);
        if not fRc:
            reporter.error('List host drives in the VM %s failed' % (self.sVmName, ));
        else:
            if aBuf is None:
                fRc = reporter.error('"List host drives" output is empty for the VM %s' % (self.sVmName, ));
            else:
                asHddData = self.asHdds[sHdd];

                try:    aBuf = str(aBuf); # pylint: disable=redefined-variable-type
                except: pass;
                asLines = aBuf.splitlines();
                oRegExp = re.compile(r'^\s*([^:]+)\s*:\s*(.+)\s*$');

                # pylint: disable=no-init
                class ParseState(object):
                    kiNothing   = 0;
                    kiDrive     = 1;
                    kiPartition = 2;

                iParseState = ParseState.kiNothing;
                asKeysNotFound = asHddData['Header'].keys();
                idxPartition = 0;
                for sLine in asLines:
                    if not sLine or sLine.startswith('#') or sLine.startswith("\n"):
                        continue;
                    oMatch = oRegExp.match(sLine);
                    if oMatch is not None:
                        sKey = oMatch.group(1);
                        sValue = oMatch.group(2);
                        if sKey is not None and sKey == 'Drive':
                            # we found required disk if we found all required disk info and partitions
                            if sDrive and not asKeysNotFound and idxPartition >= len(asHddData['Partitions']['Partitions']):
                                break;
                            sDrive = sValue;
                            iParseState = ParseState.kiDrive;
                            asKeysNotFound = asKeysNotFound = asHddData['Header'].keys();
                            idxPartition = 0;
                            continue;
                    if iParseState == ParseState.kiDrive:
                        if sLine.strip().startswith('Partitions:'):
                            iParseState = ParseState.kiPartition;
                            continue;
                        if oMatch is None or sKey is None:
                            continue;
                        if sKey in asHddData['Header'].keys() and asHddData['Header'][sKey] == sValue:
                            asKeysNotFound.remove(sKey);
                        continue;
                    if iParseState == ParseState.kiPartition:
                        if idxPartition < len(asHddData['Partitions']['Partitions']):
                            sPart = asHddData['Partitions']['Partitions'][idxPartition];
                            sPart = sPart.replace('$(' + str(idxPartition + 1) + ')',
                                                  str(asHddData['Partitions']['PartitionNumbers'][idxPartition]));
                            if sLine.strip() == sPart:
                                idxPartition += 1;
                        continue;
                fRc = sDrive and not asKeysNotFound and idxPartition >= len(asHddData['Partitions']['Partitions']);
                if fRc:
                    reporter.log("Path to the drive '%s' in the VM '%s': %s " % (sHdd, self.sVmName, sDrive));
                else:
                    reporter.error("Path to drive '%s' not found in the VM '%s'" % (sHdd, self.sVmName));
        reporter.testDone();
        return (fRc, sDrive);

    def convertDiskToPartitionPrefix(self, sDisk):
        return sDisk;

    def checkVMDKDescriptor(self, asDescriptor, sHdd, sRawDrive, asAction):
        """
        Check VMDK descriptor of the disk created
        """
        if     asDescriptor is None \
            or asDescriptor[0] != '# Disk DescriptorFile'  \
           and asDescriptor[0] != '# Disk Descriptor File' \
           and asDescriptor[0] != '#Disk Descriptor File'  \
           and asDescriptor[0] != '#Disk DescriptorFile':
            return reporter.error("VMDK descriptor has invalid format");

        # pylint: disable=no-init
        class DescriptorParseState(object):
            kiHeader   = 1;
            kiExtent   = 2;
            kiDatabase = 3;

        asHddData = self.asHdds[sHdd];
        iParseState = DescriptorParseState.kiHeader;

        asHeader = { 'version'    : '1',
                     'CID'        : '*',
                     'parentCID'  : 'ffffffff',
                     'createType' : '$'
                   };

        asDatabase = { 'ddb.virtualHWVersion'        : '4',
                       'ddb.adapterType'             : 'ide',
                       'ddb.uuid.image'              : '*',
                       'ddb.uuid.parent'             : '00000000-0000-0000-0000-000000000000',
                       'ddb.uuid.modification'       : '00000000-0000-0000-0000-000000000000',
                       'ddb.uuid.parentmodification' : '00000000-0000-0000-0000-000000000000'
                     };

        oRegExp = re.compile(r'^\s*([^=]+)\s*=\s*\"*([^\"]+)\"*\s*$');
        iExtentIdx = 0;

        for sLine in asDescriptor:
            if not sLine or sLine.startswith('#') or sLine.startswith("\n"):
                continue;

            if iParseState == DescriptorParseState.kiHeader:
                if sLine.startswith('ddb.'):
                    return reporter.error("VMDK descriptor has invalid order of sections");
                if    sLine.startswith("RW")     \
                   or sLine.startswith("RDONLY") \
                   or sLine.startswith("NOACCESS"):
                    iParseState = DescriptorParseState.kiExtent;
                else:
                    oMatch = oRegExp.match(sLine);
                    if oMatch is None:
                        return reporter.error("VMDK descriptor contains lines in invalid form");
                    sKey = oMatch.group(1).strip();
                    sValue = oMatch.group(2).strip();
                    if sKey not in asHeader:
                        return reporter.error("VMDK descriptor has invalid format");
                    sDictValue = asHeader[sKey];
                    if sDictValue == '$':
                        sDictValue = asAction[sKey];
                    if sDictValue not in ('*', sValue):
                        return reporter.error("VMDK descriptor has value which was not expected");
                    continue;

            if iParseState == DescriptorParseState.kiExtent:
                if sLine.startswith('ddb.'):
                    iParseState = DescriptorParseState.kiDatabase;
                else:
                    if     not sLine.startswith("RW")     \
                       and not sLine.startswith("RDONLY") \
                       and not sLine.startswith("NOACCESS"):
                        return reporter.error("VMDK descriptor has invalid order of sections");
                    sExtent = asAction['extents'][sHdd][iExtentIdx];
                    sExtent = sExtent.replace('$(disk)', sRawDrive);
                    sExtent = sExtent.replace('$(part)', self.convertDiskToPartitionPrefix(sRawDrive));
                    sExtent = re.sub(r'\$\((\d+)\)',
                                      lambda oMatch: str(asHddData['Partitions']['PartitionNumbers'][int(oMatch.group(1)) - 1]),
                                      sExtent);
                    if sExtent != sLine.strip():
                        return reporter.error("VMDK descriptor has invalid order of sections");
                    iExtentIdx += 1;
                    continue;

            if iParseState == DescriptorParseState.kiDatabase:
                if not sLine.startswith('ddb.'):
                    return reporter.error("VMDK descriptor has invalid order of sections");
                oMatch = oRegExp.match(sLine);
                if oMatch is None:
                    return reporter.error("VMDK descriptor contains lines in invalid form");
                sKey = oMatch.group(1).strip();
                sValue = oMatch.group(2).strip();
                if sKey not in asDatabase:
                    return reporter.error("VMDK descriptor has invalid format");
                sDictValue = asDatabase[sKey];
                if sDictValue not in ('*', sValue):
                    return reporter.error("VMDK descriptor has value which was not expected");
                continue;
        return iParseState == DescriptorParseState.kiDatabase;

    def _setPermissionsToVmdkFiles(self, oGuestSession):
        """
            Sets 0644 permissions to all files in the self.sVMDKPath allowing reading them by 'vbox' user.
        """
        (fRc, _, _, _) = self.guestProcessExecute(oGuestSession,
                                                      'Allowing reading of the VMDK content by vbox user',
                                                      30 * 1000, '/usr/bin/sudo',
                                                      ['/usr/bin/sudo', '/bin/chmod', '644',
                                                       self.sVMDKPath + '/vmdktest.vmdk', self.sVMDKPath + '/vmdktest-pt.vmdk'],
                                                      False, True);
        return fRc;

    def createDrives(self, oGuestSession, sHdd, sRawDrive):
        """
        Creates VMDK Raw file and check correctness
        """
        reporter.testStart("Create VMDK disks");
        asHddData = self.asHdds[sHdd];
        fRc = True;
        try:    oGuestSession.directoryCreate(self.sVMDKPath, 0o777, (vboxcon.DirectoryCreateFlag_Parents,));
        except: fRc = reporter.errorXcpt('Create directory for VMDK files failed in the VM %s' % (self.sVmName));
        if fRc:
            sBootSectorGuestPath = self.sVMDKPath + self.sPathDelimiter + 't-bootsector.bin';
            try:    fExists = oGuestSession.fileExists(sBootSectorGuestPath, False);
            except: fExists = False;
            if not fExists:
                sBootSectorPath = self.oTstDrv.getFullResourceName(self.sBootSector);
                fRc = self.uploadFile(oGuestSession, sBootSectorPath, sBootSectorGuestPath);

            for action in self.asActions:
                reporter.testStart("Create VMDK disk: %s" % action["action"]);
                asOptions = action['options'];
                asOptions = [option.replace('$(bootsector)', sBootSectorGuestPath) for option in asOptions];
                asOptions = [re.sub(r'\$\((\d+)\)',
                                    lambda oMatch: str(asHddData['Partitions']['PartitionNumbers'][int(oMatch.group(1)) - 1]),
                                    option)
                             for option in asOptions];
                (fRc, _, _, _) = self._callVBoxManage(oGuestSession, 'Create VMDK disk', 60 * 1000,
                                                      ['createmedium', '--filename',
                                                       self.sVMDKPath + self.sPathDelimiter + 'vmdktest.vmdk',
                                                       '--format', 'VMDK', '--variant', 'RawDisk',
                                                       '--property', 'RawDrive=%s' % (sRawDrive,) ] + asOptions,
                                                      False, True);
                if not fRc:
                    reporter.error('Create VMDK raw drive variant "%s" failed in the VM %s' % (action["action"], self.sVmName));
                else:
                    fRc = self._setPermissionsToVmdkFiles(oGuestSession);
                    if not fRc:
                        reporter.error('Setting permissions to VMDK files failed');
                    else:
                        sSrcFile = self.sVMDKPath + self.sPathDelimiter + 'vmdktest.vmdk';
                        sDstFile = os.path.join(self.oTstDrv.sScratchPath, 'guest-vmdktest.vmdk');
                        reporter.log2('Downloading file "%s" to "%s" ...' % (sSrcFile, sDstFile));
                        # First try to remove (unlink) an existing temporary file, as we don't truncate the file.
                        try:    os.unlink(sDstFile);
                        except: pass;
                        fRc = self.downloadFile(oGuestSession, sSrcFile, sDstFile, False);
                        if not fRc:
                            reporter.error('Download vmdktest.vmdk from guest to host failed');
                        else:
                            with open(sDstFile) as oFile: # pylint: disable=unspecified-encoding
                                asDescriptor = [row.strip() for row in oFile];
                            if not asDescriptor:
                                fRc = reporter.error('Reading vmdktest.vmdk from guest filed');
                            else:
                                fRc = self.checkVMDKDescriptor(asDescriptor, sHdd, sRawDrive, action);
                                if not fRc:
                                    reporter.error('Cheking vmdktest.vmdk from guest filed');
                                elif action['data-crc']:
                                    sSrcFile = self.sVMDKPath + self.sPathDelimiter + 'vmdktest-pt.vmdk';
                                    sDstFile = os.path.join(self.oTstDrv.sScratchPath, 'guest-vmdktest-pt.vmdk');
                                    reporter.log2('Downloading file "%s" to "%s" ...' % (sSrcFile, sDstFile));
                                    # First try to remove (unlink) an existing temporary file, as we don't truncate the file.
                                    try:    os.unlink(sDstFile);
                                    except: pass;
                                    fRc = self.downloadFile(oGuestSession, sSrcFile, sDstFile, False);
                                    if not fRc:
                                        reporter.error('Download vmdktest-pt.vmdk from guest to host failed');
                                    else:
                                        uResCrc32 = utils.calcCrc32OfFile(sDstFile);
                                        if uResCrc32 != action['data-crc'][sHdd]:
                                            fRc = reporter.error('vmdktest-pt.vmdk does not match what was expected');
                    (fRc1, _, _, _) = self._callVBoxManage(oGuestSession, 'Delete VMDK disk', 60 * 1000,
                                                           ['closemedium',
                                                            self.sVMDKPath + self.sPathDelimiter + 'vmdktest.vmdk',
                                                            '--delete'],
                                                           False, True);
                    if not fRc1:
                        reporter.error('Delete VMDK raw drive variant "%s" failed in the VM %s' %
                                       (action["action"], self.sVmName));
                    fRc = fRc and fRc1;
                reporter.testDone();
                if not fRc:
                    break;
        else:
            reporter.error('Create %s dir failed in the VM %s' % (self.sVMDKPath, self.sVmName));

        reporter.testDone();
        return fRc;


class tdStorageRawDriveOsLinux(tdStorageRawDriveOs):
    """
    Autostart support methods for Linux guests.
    """
    # pylint: disable=too-many-arguments
    def __init__(self, oSet, oTstDrv, sVmName, sKind, sHdd, eNic0Type = None, cMbRam = None,  \
                 cCpus = 1, fPae = None, sGuestAdditionsIso = None, sBootSector = None):
        tdStorageRawDriveOs.__init__(self, oSet, oTstDrv, sVmName, sKind, sHdd, eNic0Type, cMbRam, \
                               cCpus, fPae, sGuestAdditionsIso, sBootSector);
        self.sVBoxInstaller = '^VirtualBox-.*\\.run$';
        return;

    def installAdditions(self, oSession, oGuestSession, oVM):
        """
        Install guest additions in the guest.
        """
        reporter.testStart('Install Guest Additions');
        fRc = False;
        # Install Kernel headers, which are required for actually installing the Linux Additions.
        if   oVM.OSTypeId.startswith('Debian') \
          or oVM.OSTypeId.startswith('Ubuntu'):
            (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Installing Kernel headers',
                                                  5 * 60 *1000, '/usr/bin/apt-get',
                                                  ['/usr/bin/apt-get', 'install', '-y',
                                                   'linux-headers-generic'],
                                                  False, True);
            if not fRc:
                reporter.error('Error installing Kernel headers');
            else:
                (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Installing Guest Additions depdendencies',
                                                          5 * 60 *1000, '/usr/bin/apt-get',
                                                          ['/usr/bin/apt-get', 'install', '-y', 'build-essential',
                                                           'perl'], False, True);
                if not fRc:
                    reporter.error('Error installing additional installer dependencies');
        elif oVM.OSTypeId.startswith('OL') \
          or oVM.OSTypeId.startswith('Oracle') \
          or oVM.OSTypeId.startswith('RHEL') \
          or oVM.OSTypeId.startswith('Redhat') \
          or oVM.OSTypeId.startswith('Cent'):
            (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Installing Kernel headers',
                                                  5 * 60 *1000, '/usr/bin/yum',
                                                  ['/usr/bin/yum', '-y', 'install', 'kernel-headers'],
                                                  False, True);
            if not fRc:
                reporter.error('Error installing Kernel headers');
            else:
                (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Installing Guest Additions depdendencies',
                                                          5 * 60 *1000, '/usr/bin/yum',
                                                          ['/usr/bin/yum', '-y', 'install', 'make', 'automake', 'gcc',
                                                           'kernel-devel', 'dkms', 'bzip2', 'perl'], False, True);
                if not fRc:
                    reporter.error('Error installing additional installer dependencies');
        else:
            reporter.error('Installing Linux Additions for the "%s" is not supported yet' % oVM.OSTypeId);
            fRc = False;
        if fRc:
            #
            # The actual install.
            # Also tell the installer to produce the appropriate log files.
            #
            (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Installing guest additions',
                                                      10 * 60 *1000, '/usr/bin/sudo',
                                                      ['/usr/bin/sudo', '/bin/sh',
                                                       '/media/cdrom/VBoxLinuxAdditions.run'],
                                                      False, True);
            if fRc:
                # Due to the GA updates as separate process the above function returns before
                # the actual installation finished. So just wait until the GA installed
                fRc = self.closeSession(oGuestSession);
                if fRc:
                    (fRc, oGuestSession) = self.waitVmIsReady(oSession, False);
                # Download log files.
                # Ignore errors as all files above might not be present for whatever reason.
                #
                if fRc:
                    asLogFile = [];
                    asLogFile.append('/var/log/vboxadd-install.log');
                    self.downloadFiles(oGuestSession, asLogFile, fIgnoreErrors = True);
            else:
                reporter.error('Installing guest additions failed: Error occured during vbox installer execution')
        if fRc:
            (fRc, oGuestSession) = self.rebootVMAndCheckReady(oSession, oGuestSession);
            if not fRc:
                reporter.error('Reboot after installing GuestAdditions failed');
        reporter.testDone();
        return (fRc, oGuestSession);

    def installVirtualBox(self, oGuestSession):
        """
        Install VirtualBox in the guest.
        """
        reporter.testStart('Install Virtualbox into the guest VM');
        sTestBuild = self._findFile(self.sVBoxInstaller, self.asTestBuildDirs);
        reporter.log("Virtualbox install file: %s" % os.path.basename(sTestBuild));
        fRc = sTestBuild is not None;
        if fRc:
            fRc = self.uploadFile(oGuestSession, sTestBuild,
                                  '/tmp/' + os.path.basename(sTestBuild));
        else:
            reporter.error("VirtualBox install package is not defined");

        if not fRc:
            reporter.error('Upload the vbox installer into guest VM failed');
        else:
            (fRc, _, _, _) = self.guestProcessExecute(oGuestSession,
                                                      'Allowing execution for the vbox installer',
                                                      30 * 1000, '/usr/bin/sudo',
                                                      ['/usr/bin/sudo', '/bin/chmod', '755',
                                                       '/tmp/' + os.path.basename(sTestBuild)],
                                                      False, True);
            if not fRc:
                reporter.error('Allowing execution for the vbox installer failed');
        if fRc:
            (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Installing VBox',
                                                      240 * 1000, '/usr/bin/sudo',
                                                      ['/usr/bin/sudo',
                                                       '/tmp/' + os.path.basename(sTestBuild),],
                                                      False, True);
            if not fRc:
                reporter.error('Installing VBox failed');
        reporter.testDone();
        return fRc;

class tdStorageRawDriveOsDarwin(tdStorageRawDriveOs):
    """
    Autostart support methods for Darwin guests.
    """
    # pylint: disable=too-many-arguments
    def __init__(self, oSet, oTstDrv, sVmName, sKind, sHdd, eNic0Type = None, cMbRam = None,  \
                 cCpus = 1, fPae = None, sGuestAdditionsIso = None, sBootSector = None):
        tdStorageRawDriveOs.__init__(self, oSet, oTstDrv, sVmName, sKind, sHdd, eNic0Type, cMbRam, \
                               cCpus, fPae, sGuestAdditionsIso, sBootSector);
        raise base.GenError('Testing the autostart functionality for Darwin is not implemented');

class tdStorageRawDriveOsSolaris(tdStorageRawDriveOs):
    """
    Autostart support methods for Solaris guests.
    """
    # pylint: disable=too-many-arguments
    def __init__(self, oSet, oTstDrv, sVmName, sKind, sHdd, eNic0Type = None, cMbRam = None,  \
                 cCpus = 1, fPae = None, sGuestAdditionsIso = None, sBootSector = None):
        tdStorageRawDriveOs.__init__(self, oSet, oTstDrv, sVmName, sKind, sHdd, eNic0Type, cMbRam, \
                               cCpus, fPae, sGuestAdditionsIso, sBootSector);
        raise base.GenError('Testing the autostart functionality for Solaris is not implemented');

class tdStorageRawDriveOsWin(tdStorageRawDriveOs):
    """
    Autostart support methods for Windows guests.
    """
    # pylint: disable=too-many-arguments
    def __init__(self, oSet, oTstDrv, sVmName, sKind, sHdd, eNic0Type = None, cMbRam = None,  \
                 cCpus = 1, fPae = None, sGuestAdditionsIso = None, sBootSector = None):
        tdStorageRawDriveOs.__init__(self, oSet, oTstDrv, sVmName, sKind, sHdd, eNic0Type, cMbRam, \
                               cCpus, fPae, sGuestAdditionsIso, sBootSector);
        self.sVBoxInstaller = r'^VirtualBox-.*\.(exe|msi)$';
        self.sVMDKPath=r'C:\Temp\vmdk';
        self.sPathDelimiter = '\\';
        self.asHdds['6.1/storage/t-mbr.vdi']['Header']['Model'] = '"VBOX HARDDISK"';
        self.asHdds['6.1/storage/t-gpt.vdi']['Header']['Model'] = '"VBOX HARDDISK"';
        self.asHdds['6.1/storage/t-mbr.vdi']['Partitions']['PartitionNumbers'] = [1, 2, 3, 4, 5, 6, 7, 8];
        return;

    def _checkVmIsReady(self, oGuestSession):
        (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Start a guest process',
                                                  30 * 1000, 'C:\\Windows\\System32\\ipconfig.exe',
                                                  ['C:\\Windows\\System32\\ipconfig.exe',],
                                                  False, False);
        return fRc;

    def _rebootVM(self, oGuestSession):
        (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Reboot the VM',
                                                  30 * 1000, 'C:\\Windows\\System32\\shutdown.exe',
                                                  ['C:\\Windows\\System32\\shutdown.exe', '/f',
                                                   '/r', '/t', '0'],
                                                  False, True);
        if not fRc:
            reporter.error('Calling the shutdown utility failed');
        return fRc;

    def _powerDownVM(self, oGuestSession):
        (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Power down the VM',
                                                  30 * 1000, 'C:\\Windows\\System32\\shutdown.exe',
                                                  ['C:\\Windows\\System32\\shutdown.exe', '/f',
                                                   '/s', '/t', '0'],
                                                  False, True);
        if not fRc:
            reporter.error('Calling the shutdown utility failed');
        return fRc;

    def _callVBoxManage(self, oGuestSession, sTestName, cMsTimeout, asArgs = (),
                            fGetStdOut = True, fIsError = True):
        return self.guestProcessExecute(oGuestSession, sTestName,
                                        cMsTimeout, r'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe',
                                        [r'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe',] + asArgs, fGetStdOut, fIsError);

    def _setPermissionsToVmdkFiles(self, oGuestSession):
        """
            Sets 0644 permissions to all files in the self.sVMDKPath allowing reading them by 'vbox' user.
        """
        _ = oGuestSession;
        # It is not required in case of Windows
        return True;

    def installAdditions(self, oSession, oGuestSession, oVM):
        """
        Installs the Windows guest additions using the test execution service.
        """
        _ = oVM;
        reporter.testStart('Install Guest Additions');
        asLogFiles = [];
        fRc = self.closeSession(oGuestSession, True); # pychecker hack.
        try:
            oCurProgress = oSession.o.console.guest.updateGuestAdditions(self.sGuestAdditionsIso, ['/l',], None);
        except:
            reporter.maybeErrXcpt(True, 'Updating Guest Additions exception for sSrc="%s":'
                                  % (self.sGuestAdditionsIso,));
            fRc = False;
        else:
            if oCurProgress is not None:
                oWrapperProgress = vboxwrappers.ProgressWrapper(oCurProgress, self.oTstDrv.oVBoxMgr,
                                                                self.oTstDrv, "installAdditions");
                oWrapperProgress.wait(cMsTimeout = 10 * 60 * 1000);
                if not oWrapperProgress.isSuccess():
                    oWrapperProgress.logResult(fIgnoreErrors = False);
                    fRc = False;
            else:
                fRc = reporter.error('No progress object returned');

        # Store the result and try download logs anyway.
        fGaRc = fRc;
        fRc, oGuestSession = self.createSession(oSession, 'Session for user: vbox',
                                        'vbox', 'password', 10 * 1000, True);
        if fRc is True:
            (fRc, oGuestSession) = self.rebootVMAndCheckReady(oSession, oGuestSession);
            if fRc is True:
                # Add the Windows Guest Additions installer files to the files we want to download
                # from the guest.
                sGuestAddsDir = 'C:/Program Files/Oracle/VirtualBox Guest Additions/';
                asLogFiles.append(sGuestAddsDir + 'install.log');
                # Note: There won't be a install_ui.log because of the silent installation.
                asLogFiles.append(sGuestAddsDir + 'install_drivers.log');
                # Download log files.
                # Ignore errors as all files above might not be present (or in different locations)
                # on different Windows guests.
                #
                self.downloadFiles(oGuestSession, asLogFiles, fIgnoreErrors = True);
            else:
                reporter.error('Reboot after installing GuestAdditions failed');
        else:
            reporter.error('Create session for user vbox after GA updating failed');
        reporter.testDone();
        return (fRc and fGaRc, oGuestSession);

    def installVirtualBox(self, oGuestSession):
        """
        Install VirtualBox in the guest.
        """
        reporter.testStart('Install Virtualbox into the guest VM');
        # Used windows image already contains the C:\Temp
        sTestBuild = self._findFile(self.sVBoxInstaller, self.asTestBuildDirs);
        reporter.log("Virtualbox install file: %s" % os.path.basename(sTestBuild));
        fRc = sTestBuild is not None;
        if fRc:
            fRc = self.uploadFile(oGuestSession, sTestBuild,
                              'C:\\Temp\\' + os.path.basename(sTestBuild));
        else:
            reporter.error("VirtualBox install package is not defined");

        if not fRc:
            reporter.error('Upload the installing into guest VM failed');
        else:
            if sTestBuild.endswith('.msi'):
                sLogFile = 'C:/Temp/VBoxInstallLog.txt';
                (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Installing VBox',
                                                        600 * 1000, 'C:\\Windows\\System32\\msiexec.exe',
                                                        ['msiexec', '/quiet', '/norestart', '/i',
                                                         'C:\\Temp\\' + os.path.basename(sTestBuild),
                                                        '/lv', sLogFile],
                                                        False, True);
                if not fRc:
                    reporter.error('Installing the VBox from msi installer failed');
            else:
                sLogFile = 'C:/Temp/Virtualbox/VBoxInstallLog.txt';
                (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Installing VBox',
                                                        600 * 1000, 'C:\\Temp\\' + os.path.basename(sTestBuild),
                                                        ['C:\\Temp\\' + os.path.basename(sTestBuild), '-vvvv',
                                                         '--silent', '--logging',
                                                         '--msiparams', 'REBOOT=ReallySuppress'],
                                                        False, True);
                if not fRc:
                    reporter.error('Installing the VBox failed');
                else:
                    (_, _, _, aBuf) = self.guestProcessExecute(oGuestSession, 'Check installation',
                                                               240 * 1000, 'C:\\Windows\\System32\\cmd.exe',
                                                               ['c:\\Windows\\System32\\cmd.exe', '/c',
                                                                'dir', 'C:\\Program Files\\Oracle\\VirtualBox\\*.*'],
                                                               True, True);
                    reporter.log('Content of  VirtualBxox folder:');
                    reporter.log(str(aBuf));
            asLogFiles = [sLogFile,];
            self.downloadFiles(oGuestSession, asLogFiles, fIgnoreErrors = True);
        reporter.testDone();
        return fRc;

    def convertDiskToPartitionPrefix(self, sDisk):
        # Convert \\.\PhysicalDriveX into \\.\HarddiskXPartition
        oMatch = re.match(r'^\\\\.\\PhysicalDrive(\d+)$', sDisk);
        if oMatch is None:
            return None;
        return r'\\.\Harddisk' + oMatch.group(1) + 'Partition';

class tdStorageRawDrive(vbox.TestDriver):                                      # pylint: disable=too-many-instance-attributes
    """
    Autostart testcase.
    """
    ksOsLinux   = 'tst-linux';
    ksOsWindows = 'tst-win';
    ksOsDarwin  = 'tst-darwin';
    ksOsSolaris = 'tst-solaris';
    ksOsFreeBSD = 'tst-freebsd';
    ksBootSectorPath = '6.1/storage/t-bootsector.bin';
    kasHdds = ['6.1/storage/t-gpt.vdi', '6.1/storage/t-mbr.vdi'];

    def __init__(self):
        vbox.TestDriver.__init__(self);
        self.asRsrcs            = None;
        self.asSkipVMs          = [];
        ## @todo r=bird: The --test-build-dirs option as primary way to get the installation files to test
        ## is not an acceptable test practice as we don't know wtf you're testing.  See defect for more.
        self.asTestBuildDirs    = [os.path.join(self.sScratchPath, 'bin'),];
        self.sGuestAdditionsIso = None; #'D:/AlexD/TestBox/TestAdditionalFiles/VBoxGuestAdditions_6.1.2.iso';
        oSet = vboxtestvms.TestVmSet(self.oTestVmManager, acCpus = [2], asVirtModes = ['hwvirt-np',], fIgnoreSkippedVm = True);
        # pylint: disable=line-too-long
        self.asTestVmClasses = {
            'win'     : None, #tdStorageRawDriveOsWin(oSet, self, self.ksOsWindows, 'Windows7_64', \
                             #'6.0/windows7piglit/windows7piglit.vdi', eNic0Type = None, cMbRam = 2048,  \
                             #cCpus = 2, fPae = True, sGuestAdditionsIso = self.getGuestAdditionsIso(),
                             #sBootSector = self.ksBootSectorPath),
            'linux'   : tdStorageRawDriveOsLinux(oSet, self, self.ksOsLinux, 'Ubuntu_64', \
                               '6.0/ub1804piglit/ub1804piglit.vdi', eNic0Type = None, \
                               cMbRam = 2048, cCpus = 2, fPae = None, sGuestAdditionsIso = self.getGuestAdditionsIso(),
                               sBootSector = self.ksBootSectorPath),
            'solaris' : None, #'tdAutostartOsSolaris',
            'darwin'  : None  #'tdAutostartOsDarwin'
        };
        oSet.aoTestVms.extend([oTestVm for oTestVm in self.asTestVmClasses.values() if oTestVm is not None]);
        sOs = self.getBuildOs();
        if sOs in self.asTestVmClasses:
            for oTestVM in oSet.aoTestVms:
                if oTestVM is not None:
                    oTestVM.fSkip = oTestVM != self.asTestVmClasses[sOs];
        # pylint: enable=line-too-long
        self.oTestVmSet = oSet;

    #
    # Overridden methods.
    #

    def showUsage(self):
        rc = vbox.TestDriver.showUsage(self);
        reporter.log('');
        reporter.log('tdAutostart Options:');
        reporter.log('  --test-build-dirs <path1[,path2[,...]]>');
        reporter.log('      The list of directories with VirtualBox distros. Overrides default path.');
        reporter.log('      Default path is $TESTBOX_SCRATCH_PATH/bin.');
        reporter.log('  --vbox-<os>-build <path>');
        reporter.log('      The path to vbox build for the specified OS.');
        reporter.log('      The OS can be one of "win", "linux", "solaris" and "darwin".');
        reporter.log('      This option alse enables corresponding VM for testing.');
        reporter.log('      (Default behaviour is testing only VM having host-like OS.)');
        return rc;

    def parseOption(self, asArgs, iArg): # pylint: disable=too-many-branches,too-many-statements
        if asArgs[iArg] == '--test-build-dirs':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--test-build-dirs" takes a path argument');
            self.asTestBuildDirs = asArgs[iArg].split(',');
            for oTestVm in self.oTestVmSet.aoTestVms:
                oTestVm.asTestBuildDirs = self.asTestBuildDirs;
        elif asArgs[iArg] in [ '--vbox-%s-build' % sKey for sKey in self.asTestVmClasses]:
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "%s" take a path argument' % (asArgs[iArg - 1],));
            oMatch = re.match("--vbox-([^-]+)-build", asArgs[iArg - 1]);
            if oMatch is not None:
                sOs = oMatch.group(1);
                oTestVm = self.asTestVmClasses.get(sOs);
                if oTestVm is not None:
                    oTestVm.sTestBuild = asArgs[iArg];
                    oTestVm.fSkip = False;
        else:
            return vbox.TestDriver.parseOption(self, asArgs, iArg);
        return iArg + 1;

    def getResourceSet(self):
        asRsrcs = self.kasHdds[:];
        asRsrcs.extend([self.ksBootSectorPath,]);
        asRsrcs.extend(vbox.TestDriver.getResourceSet(self));
        return asRsrcs;

    def actionConfig(self):
        if not self.importVBoxApi(): # So we can use the constant below.
            return False;
        return self.oTestVmSet.actionConfig(self);

    def actionExecute(self):
        """
        Execute the testcase.
        """
        return self.oTestVmSet.actionExecute(self, self.testAutostartOneVfg)

    #
    # Test execution helpers.
    #
    def testAutostartOneVfg(self, oVM, oTestVm):
        fRc = True;
        self.logVmInfo(oVM);

        for sHdd in self.kasHdds:
            reporter.testStart('%s with %s disk' % ( oTestVm.sVmName, sHdd))
            fRc = oTestVm.reattachHdd(oVM, sHdd, self.kasHdds);
            if fRc:
                oSession = self.startVmByName(oTestVm.sVmName);
                if oSession is not None:
                    (fRc, oGuestSession) = oTestVm.waitVmIsReady(oSession, True);
                    if fRc:
                        if fRc:
                            (fRc, oGuestSession) = oTestVm.installAdditions(oSession, oGuestSession, oVM);
                        if fRc:
                            fRc = oTestVm.installVirtualBox(oGuestSession);
                            if fRc:
                                (fRc, sRawDrive) = oTestVm.listHostDrives(oGuestSession, sHdd);
                                if fRc:
                                    fRc = oTestVm.createDrives(oGuestSession, sHdd, sRawDrive);
                                    if not fRc:
                                        reporter.error('Create VMDK raw drives failed');
                                else:
                                    reporter.error('List host drives failed');
                            else:
                                reporter.error('Installing VirtualBox in the guest failed');
                        else:
                            reporter.error('Creating Guest Additions failed');
                    else:
                        reporter.error('Waiting for start VM failed');
                    if oGuestSession is not None:
                        try:    oTestVm.powerDownVM(oGuestSession);
                        except: pass;
                    try:    self.terminateVmBySession(oSession);
                    except: pass;
                    fRc = oSession.close() and fRc and True; # pychecker hack.
                    oSession = None;
                else:
                    fRc = False;
            else:
                reporter.error('Attaching %s to %s failed' % (sHdd, oTestVm.sVmName));
            reporter.testDone();
        return fRc;

if __name__ == '__main__':
    sys.exit(tdStorageRawDrive().main(sys.argv));
