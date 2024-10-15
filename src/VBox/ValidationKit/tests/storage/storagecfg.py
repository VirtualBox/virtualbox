# -*- coding: utf-8 -*-
# $Id: storagecfg.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
VirtualBox Validation Kit - Storage test configuration API.
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
from testdriver import reporter;


class StorageDisk(object):
    """
    Class representing a disk for testing.
    """

    def __init__(self, sPath, fRamDisk = False):
        self.sPath    = sPath;
        self.fUsed    = False;
        self.fRamDisk = fRamDisk;

    def getPath(self):
        """
        Return the disk path.
        """
        return self.sPath;

    def isUsed(self):
        """
        Returns whether the disk is currently in use.
        """
        return self.fUsed;

    def isRamDisk(self):
        """
        Returns whether the disk objecthas a RAM backing.
        """
        return self.fRamDisk;

    def setUsed(self, fUsed):
        """
        Sets the used flag for the disk.
        """
        if fUsed:
            if self.fUsed:
                return False;

            self.fUsed = True;
        else:
            self.fUsed = fUsed;

        return True;

class StorageConfigOs(object):
    """
    Base class for a single hosts OS storage configuration.
    """

    def _getDisksMatchingRegExpWithPath(self, sPath, sRegExp):
        """
        Adds new disks to the config matching the given regular expression.
        """

        lstDisks = [];
        oRegExp = re.compile(sRegExp);
        asFiles = os.listdir(sPath);
        for sFile in asFiles:
            if oRegExp.match(os.path.basename(sFile)) and os.path.exists(sPath + '/' + sFile):
                lstDisks.append(StorageDisk(sPath + '/' + sFile));

        return lstDisks;

class StorageConfigOsSolaris(StorageConfigOs):
    """
    Class implementing the Solaris specifics for a storage configuration.
    """

    def __init__(self):
        StorageConfigOs.__init__(self);
        self.idxRamDisk = 0;

    def _getActivePoolsStartingWith(self, oExec, sPoolIdStart):
        """
        Returns a list of pools starting with the given ID or None on failure.
        """
        lstPools = None;
        fRc, sOutput, _ = oExec.execBinary('zpool', ('list', '-H'));
        if fRc:
            lstPools = [];
            asPools = re.sub("b'|'", "", sOutput).rstrip("\\n").split("\\n")  # as sOutput could look like "b'blabla'"
            reporter.log('asPools: %s' % asPools)                             # plus delete excessive end-of-line
            for sPool in asPools:
                oMatchResult = re.match("%s[0-9]?[0-9]?" % sPoolIdStart, sPool)  # either re.Match obj or None
                reporter.log('sPoolIdStart: %s, sPool: %s, oMatchResult: %s' % (sPoolIdStart, sPool, oMatchResult))
                if oMatchResult:
                    lstPools.append(oMatchResult.group(0));
        return lstPools;

    def _getActiveVolumesInPoolStartingWith(self, oExec, sPool, sVolumeIdStart):
        """
        Returns a list of active volumes for the given pool starting with the given
        identifier or None on failure.
        """
        lstVolumes = None;
        fRc, sOutput, _ = oExec.execBinary('zfs', ('list', '-H'));
        if fRc:
            lstVolumes = [];
            asVolumes = re.sub("b'|'", "", sOutput).rstrip("\\n").split("\\n")  # as sOutput could look like "b'blabla'"
            reporter.log('asVolumes: %s' % asVolumes)                           # plus delete excessive end-of-line
            for sVolume in asVolumes:
                oMatchResult = re.match("%s/%s" % (sPool, sVolumeIdStart), sVolume)  # either re.Match obj or None
                reporter.log('sPool: %s, sVolumeIdStart: %s, sVolume: %s, OMatchResult: %s' % (sPool, sVolumeIdStart,
                                                                                               sVolume, oMatchResult))
                if oMatchResult:
                    lstVolumes.append(oMatchResult.group(0));
        return lstVolumes;

    def getDisksMatchingRegExp(self, sRegExp):
        """
        Returns a list of disks matching the regular expression.
        """
        return self._getDisksMatchingRegExpWithPath('/dev/dsk', sRegExp);

    def getMntBase(self):
        """
        Returns the mountpoint base for the host.
        """
        return '/pools';

    def createStoragePool(self, oExec, sPool, asDisks, sRaidLvl):
        """
        Creates a new storage pool with the given disks and the given RAID level.
        """
        sZPoolRaid = None;
        if len(asDisks) > 1 and (sRaidLvl == 'raid5' or sRaidLvl is None):
            sZPoolRaid = 'raidz';

        fRc = True;
        if sZPoolRaid is not None:
            fRc = oExec.execBinaryNoStdOut('zpool', ('create', '-f', sPool, sZPoolRaid,) + tuple(asDisks));
        else:
            fRc = oExec.execBinaryNoStdOut('zpool', ('create', '-f', sPool,) + tuple(asDisks));

        return fRc;

    def createVolume(self, oExec, sPool, sVol, sMountPoint, cbVol = None):
        """
        Creates and mounts a filesystem at the given mountpoint using the
        given pool and volume IDs.
        """
        fRc = True;
        if cbVol is not None:
            fRc = oExec.execBinaryNoStdOut('zfs', ('create', '-o', 'mountpoint='+sMountPoint, '-V', cbVol, sPool + '/' + sVol));
        else:
            fRc = oExec.execBinaryNoStdOut('zfs', ('create', '-o', 'mountpoint='+sMountPoint, sPool + '/' + sVol));

        # @todo Add proper parameters to set proper owner:group ownership, the testcase broke in r133060 for Solaris
        #       because ceating directories is now done using the python mkdir API instead of calling 'sudo mkdir...'.
        #       No one noticed though because testboxstor1 went out of action before...
        #       Will get fixed as soon as I'm back home.
        if fRc:
            fRc = oExec.execBinaryNoStdOut('chmod', ('777', sMountPoint));

        return fRc;

    def destroyVolume(self, oExec, sPool, sVol):
        """
        Destroys the given volume.
        """
        fRc = oExec.execBinaryNoStdOut('zfs', ('destroy', sPool + '/' + sVol));
        return fRc;

    def destroyPool(self, oExec, sPool):
        """
        Destroys the given storage pool.
        """
        fRc = oExec.execBinary('zpool', ('destroy', sPool));
        return fRc;

    def cleanupPoolsAndVolumes(self, oExec, sPoolIdStart, sVolIdStart):
        """
        Cleans up any pools and volumes starting with the name in the given
        parameters.
        """
        reporter.log('cleanupPoolsAndVolumes starts');
        fRc = True;
        lstPools = self._getActivePoolsStartingWith(oExec, sPoolIdStart);
        reporter.log('lstPools: %s' % lstPools);
        if lstPools is not None:
            for sPool in lstPools:
                fRc2 = fRc3 = False  # flags for volumes and pools destruction results
                lstVolumes = self._getActiveVolumesInPoolStartingWith(oExec, sPool, sVolIdStart);
                reporter.log('lstVolumes: %s' % lstVolumes)
                if lstVolumes is not None:
                    # Destroy all the volumes first
                    reporter.log('destroying volumes starts');
                    for sVolume in lstVolumes:
                        fRc2 = oExec.execBinary('zfs', ('destroy', sVolume));
                    reporter.log('destroying volumes ends');

                # Destroy the pool
                reporter.log('destroying pools starts');
                fRc3 = self.destroyPool(oExec, sPool);
                reporter.log('destroying pools ends');
                if not (fRc2 or fRc3):
                    fRc = False;
        else:
            fRc = False;
        reporter.log('cleanupPoolsAndVolumes is finished');
        return fRc;

    def createRamDisk(self, oExec, cbRamDisk):
        """
        Creates a RAM backed disk with the given size.
        """
        oDisk = None;
        sRamDiskName = 'ramdisk%u' % (self.idxRamDisk,);
        fRc, _ , _ = oExec.execBinary('ramdiskadm', ('-a', sRamDiskName, str(cbRamDisk)));
        if fRc:
            self.idxRamDisk += 1;
            oDisk = StorageDisk('/dev/ramdisk/%s' % (sRamDiskName, ), True);

        return oDisk;

    def destroyRamDisk(self, oExec, oDisk):
        """
        Destroys the given ramdisk object.
        """
        sRamDiskName = os.path.basename(oDisk.getPath());
        return oExec.execBinaryNoStdOut('ramdiskadm', ('-d', sRamDiskName));

class StorageConfigOsLinux(StorageConfigOs):
    """
    Class implementing the Linux specifics for a storage configuration.
    """

    def __init__(self):
        StorageConfigOs.__init__(self);
        self.dSimplePools = { }; # Simple storage pools which don't use lvm (just one partition)
        self.dMounts      = { }; # Pool/Volume to mountpoint mapping.

    def _getDmRaidLevelFromLvl(self, sRaidLvl):
        """
        Converts our raid level indicators to something mdadm can understand.
        """
        if sRaidLvl is None or sRaidLvl == 'raid0':
            return 'stripe';
        if sRaidLvl == 'raid5':
            return '5';
        if sRaidLvl == 'raid1':
            return 'mirror';
        return 'stripe';

    def getDisksMatchingRegExp(self, sRegExp):
        """
        Returns a list of disks matching the regular expression.
        """
        return self._getDisksMatchingRegExpWithPath('/dev/', sRegExp);

    def getMntBase(self):
        """
        Returns the mountpoint base for the host.
        """
        return '/mnt';

    def createStoragePool(self, oExec, sPool, asDisks, sRaidLvl):
        """
        Creates a new storage pool with the given disks and the given RAID level.
        """
        fRc = True;
        if len(asDisks) == 1 and sRaidLvl is None:
            # Doesn't require LVM, put into the simple pools dictionary so we can
            # use it when creating a volume later.
            self.dSimplePools[sPool] = asDisks[0];
        else:
            # If a RAID is required use dm-raid first to create one.
            asLvmPvDisks = asDisks;
            fRc = oExec.execBinaryNoStdOut('mdadm', ('--create', '/dev/md0', '--assume-clean',
                                                     '--level=' + self._getDmRaidLevelFromLvl(sRaidLvl),
                                                     '--raid-devices=' + str(len(asDisks))) + tuple(asDisks));
            if fRc:
                # /dev/md0 is the only block device to use for our volume group.
                asLvmPvDisks = [ '/dev/md0' ];

            # Create a physical volume on every disk first.
            for sLvmPvDisk in asLvmPvDisks:
                fRc = oExec.execBinaryNoStdOut('pvcreate', (sLvmPvDisk, ));
                if not fRc:
                    break;

            if fRc:
                # Create volume group with all physical volumes included
                fRc = oExec.execBinaryNoStdOut('vgcreate', (sPool, ) + tuple(asLvmPvDisks));
        return fRc;

    def createVolume(self, oExec, sPool, sVol, sMountPoint, cbVol = None):
        """
        Creates and mounts a filesystem at the given mountpoint using the
        given pool and volume IDs.
        """
        fRc = True;
        sBlkDev = None;
        if sPool in self.dSimplePools:
            sDiskPath = self.dSimplePools.get(sPool);
            if sDiskPath.find('zram') != -1:
                sBlkDev = sDiskPath;
            else:
                # Create a partition with the requested size
                sFdiskScript = ';\n'; # Single partition filling everything
                if cbVol is not None:
                    sFdiskScript = ',' + str(cbVol // 512) + '\n'; # Get number of sectors
                fRc, _, _ = oExec.execBinary('sfdisk', ('--no-reread', '--wipe', 'always', '-q', '-f', sDiskPath), \
                                             sFdiskScript);
                if fRc:
                    if sDiskPath.find('nvme') != -1:
                        sBlkDev = sDiskPath + 'p1';
                    else:
                        sBlkDev = sDiskPath + '1';
        else:
            if cbVol is None:
                fRc = oExec.execBinaryNoStdOut('lvcreate', ('-l', '100%FREE', '-n', sVol, sPool));
            else:
                fRc = oExec.execBinaryNoStdOut('lvcreate', ('-L', str(cbVol), '-n', sVol, sPool));
            if fRc:
                sBlkDev = '/dev/mapper' + sPool + '-' + sVol;

        if fRc is True and sBlkDev is not None:
            # Create a filesystem and mount it
            fRc = oExec.execBinaryNoStdOut('mkfs.ext4', ('-F', '-F', sBlkDev,));
            fRc = fRc and oExec.mkDir(sMountPoint);
            fRc = fRc and oExec.execBinaryNoStdOut('mount', (sBlkDev, sMountPoint));
            if fRc:
                self.dMounts[sPool + '/' + sVol] = sMountPoint;
        return fRc;

    def destroyVolume(self, oExec, sPool, sVol):
        """
        Destroys the given volume.
        """
        # Unmount first
        sMountPoint = self.dMounts[sPool + '/' + sVol];
        fRc = oExec.execBinaryNoStdOut('umount', (sMountPoint,));
        self.dMounts.pop(sPool + '/' + sVol);
        oExec.rmDir(sMountPoint);
        if sPool in self.dSimplePools:
            # Wipe partition table
            sDiskPath = self.dSimplePools.get(sPool);
            if sDiskPath.find('zram') == -1:
                fRc = oExec.execBinaryNoStdOut('sfdisk', ('--no-reread', '--wipe', 'always', '-q', '-f', '--delete', \
                                               sDiskPath));
        else:
            fRc = oExec.execBinaryNoStdOut('lvremove', (sPool + '/' + sVol,));
        return fRc;

    def destroyPool(self, oExec, sPool):
        """
        Destroys the given storage pool.
        """
        fRc = True;
        if sPool in self.dSimplePools:
            self.dSimplePools.pop(sPool);
        else:
            fRc = oExec.execBinaryNoStdOut('vgremove', (sPool,));
        return fRc;

    def cleanupPoolsAndVolumes(self, oExec, sPoolIdStart, sVolIdStart):
        """
        Cleans up any pools and volumes starting with the name in the given
        parameters.
        """
        # @todo: Needs implementation, for LVM based configs a similar approach can be used
        #        as for Solaris.
        _ = oExec;
        _ = sPoolIdStart;
        _ = sVolIdStart;
        return True;

    def createRamDisk(self, oExec, cbRamDisk):
        """
        Creates a RAM backed disk with the given size.
        """
        # Make sure the ZRAM module is loaded.
        oDisk = None;
        fRc = oExec.execBinaryNoStdOut('modprobe', ('zram',));
        if fRc:
            fRc, sOut, _ = oExec.execBinary('zramctl', ('--raw', '-f', '-s', str(cbRamDisk)));
            if fRc:
                oDisk = StorageDisk(sOut.rstrip(), True);

        return oDisk;

    def destroyRamDisk(self, oExec, oDisk):
        """
        Destroys the given ramdisk object.
        """
        return oExec.execBinaryNoStdOut('zramctl', ('-r', oDisk.getPath()));

## @name Host disk config types.
## @{
g_ksDiskCfgStatic = 'StaticDir';
g_ksDiskCfgRegExp = 'RegExp';
g_ksDiskCfgList   = 'DiskList';
## @}

class DiskCfg(object):
    """
    Host disk configuration.
    """

    def __init__(self, sTargetOs, sCfgType, oDisks):
        self.sTargetOs = sTargetOs;
        self.sCfgType  = sCfgType;
        self.oDisks    = oDisks;

    def getTargetOs(self):
        return self.sTargetOs;

    def getCfgType(self):
        return self.sCfgType;

    def isCfgStaticDir(self):
        return self.sCfgType == g_ksDiskCfgStatic;

    def isCfgRegExp(self):
        return self.sCfgType == g_ksDiskCfgRegExp;

    def isCfgList(self):
        return self.sCfgType == g_ksDiskCfgList;

    def getDisks(self):
        return self.oDisks;

class StorageCfg(object):
    """
    Storage configuration helper class taking care of the different host OS.
    """

    def __init__(self, oExec, oDiskCfg):
        self.oExec    = oExec;
        self.lstDisks = [ ]; # List of disks present in the system.
        self.dPools   = { }; # Dictionary of storage pools.
        self.dVols    = { }; # Dictionary of volumes.
        self.iPoolId  = 0;
        self.iVolId   = 0;
        self.oDiskCfg = oDiskCfg;

        fRc = True;
        oStorOs = None;
        if oDiskCfg.getTargetOs() == 'solaris':
            oStorOs = StorageConfigOsSolaris();
        elif oDiskCfg.getTargetOs() == 'linux':
            oStorOs = StorageConfigOsLinux(); # pylint: disable=redefined-variable-type
        elif not oDiskCfg.isCfgStaticDir():
             # For unknown hosts only allow a static testing directory we don't care about setting up
            fRc = False;

        if fRc:
            self.oStorOs = oStorOs;
            if oDiskCfg.isCfgRegExp():
                self.lstDisks = oStorOs.getDisksMatchingRegExp(oDiskCfg.getDisks());
            elif oDiskCfg.isCfgList():
                # Assume a list of disks and add.
                for sDisk in oDiskCfg.getDisks():
                    self.lstDisks.append(StorageDisk(sDisk));
            elif oDiskCfg.isCfgStaticDir():
                if not os.path.exists(oDiskCfg.getDisks()):
                    self.oExec.mkDir(oDiskCfg.getDisks(), 0o700);

    def __del__(self):
        self.cleanup();
        self.oDiskCfg = None;

    def cleanup(self):
        """
        Cleans up any created storage configs.
        """

        if not self.oDiskCfg.isCfgStaticDir():
            # Destroy all volumes first.
            for sMountPoint in list(self.dVols.keys()): # pylint: disable=consider-iterating-dictionary
                self.destroyVolume(sMountPoint);

            # Destroy all pools.
            for sPool in list(self.dPools.keys()): # pylint: disable=consider-iterating-dictionary
                self.destroyStoragePool(sPool);

        self.dVols.clear();
        self.dPools.clear();
        self.iPoolId  = 0;
        self.iVolId   = 0;

    def getRawDisk(self):
        """
        Returns a raw disk device from the list of free devices for use.
        """

        for oDisk in self.lstDisks:
            if oDisk.isUsed() is False:
                oDisk.setUsed(True);
                return oDisk.getPath();

        return None;

    def getUnusedDiskCount(self):
        """
        Returns the number of unused disks.
        """

        cDisksUnused = 0;
        for oDisk in self.lstDisks:
            if not oDisk.isUsed():
                cDisksUnused += 1;

        return cDisksUnused;

    def createStoragePool(self, cDisks = 0, sRaidLvl = None,
                          cbPool = None, fRamDisk = False):
        """
        Create a new storage pool
        """
        lstDisks = [ ];
        fRc = True;
        sPool = None;

        if not self.oDiskCfg.isCfgStaticDir():
            if fRamDisk:
                oDisk = self.oStorOs.createRamDisk(self.oExec, cbPool);
                if oDisk is not None:
                    lstDisks.append(oDisk);
                    cDisks = 1;
            else:
                if cDisks == 0:
                    cDisks = self.getUnusedDiskCount();

                for oDisk in self.lstDisks:
                    if not oDisk.isUsed():
                        oDisk.setUsed(True);
                        lstDisks.append(oDisk);
                        if len(lstDisks) == cDisks:
                            break;

            # Enough drives to satisfy the request?
            if len(lstDisks) == cDisks:
                # Create a list of all device paths
                lstDiskPaths = [ ];
                for oDisk in lstDisks:
                    lstDiskPaths.append(oDisk.getPath());

                # Find a name for the pool
                sPool = 'pool' + str(self.iPoolId);
                self.iPoolId += 1;

                fRc = self.oStorOs.createStoragePool(self.oExec, sPool, lstDiskPaths, sRaidLvl);
                if fRc:
                    self.dPools[sPool] = lstDisks;
                else:
                    self.iPoolId -= 1;
            else:
                fRc = False;

            # Cleanup in case of error.
            if not fRc:
                for oDisk in lstDisks:
                    oDisk.setUsed(False);
                    if oDisk.isRamDisk():
                        self.oStorOs.destroyRamDisk(self.oExec, oDisk);
        else:
            sPool = 'StaticDummy';

        return fRc, sPool;

    def destroyStoragePool(self, sPool):
        """
        Destroys the storage pool with the given ID.
        """

        fRc = True;

        if not self.oDiskCfg.isCfgStaticDir():
            lstDisks = self.dPools.get(sPool);
            if lstDisks is not None:
                fRc = self.oStorOs.destroyPool(self.oExec, sPool);
                if fRc:
                    # Mark disks as unused
                    self.dPools.pop(sPool);
                    for oDisk in lstDisks:
                        oDisk.setUsed(False);
                        if oDisk.isRamDisk():
                            self.oStorOs.destroyRamDisk(self.oExec, oDisk);
            else:
                fRc = False;

        return fRc;

    def createVolume(self, sPool, cbVol = None):
        """
        Creates a new volume from the given pool returning the mountpoint.
        """

        fRc = True;
        sMountPoint = None;
        if not self.oDiskCfg.isCfgStaticDir():
            if sPool in self.dPools:
                sVol = 'vol' + str(self.iVolId);
                sMountPoint = self.oStorOs.getMntBase() + '/' + sVol;
                self.iVolId += 1;
                fRc = self.oStorOs.createVolume(self.oExec, sPool, sVol, sMountPoint, cbVol);
                if fRc:
                    self.dVols[sMountPoint] = (sVol, sPool);
                else:
                    self.iVolId -= 1;
            else:
                fRc = False;
        else:
            sMountPoint = self.oDiskCfg.getDisks();

        return fRc, sMountPoint;

    def destroyVolume(self, sMountPoint):
        """
        Destroy the volume at the given mount point.
        """

        fRc = True;
        if not self.oDiskCfg.isCfgStaticDir():
            sVol, sPool = self.dVols.get(sMountPoint);
            if sVol is not None:
                fRc = self.oStorOs.destroyVolume(self.oExec, sPool, sVol);
                if fRc:
                    self.dVols.pop(sMountPoint);
            else:
                fRc = False;

        return fRc;

    def mkDirOnVolume(self, sMountPoint, sDir, fMode = 0o700):
        """
        Creates a new directory on the volume pointed to by the given mount point.

        Returns success status.
        """
        return self.oExec.mkDir(os.path.join(sMountPoint, sDir), fMode);

    def cleanupLeftovers(self):
        """
        Tries to clean up any leftover pools and volumes from a failed previous run.
        """
        reporter.log('cleanupLeftovers starts');
        if not self.oDiskCfg.isCfgStaticDir():
            return self.oStorOs.cleanupPoolsAndVolumes(self.oExec, 'pool', 'vol');

        fRc = True;
        asDisks = self.oDiskCfg.getDisks();
        reporter.log("oDiskCfg.getDisks: %s" % asDisks);
        if os.path.exists(asDisks):
            reporter.log('os.listdir(asDisks): %s' % asDisks)
            for sEntry in os.listdir(asDisks):
                sPath = os.path.join(self.oDiskCfg.getDisks(), sEntry);
                reporter.log('path to sEntry: %s' % sPath)
                fRc = fRc and self.oExec.rmTree(sPath);
        reporter.log('cleanupLeftovers ends');
        return fRc;
