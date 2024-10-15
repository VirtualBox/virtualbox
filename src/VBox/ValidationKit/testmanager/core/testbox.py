# -*- coding: utf-8 -*-
# $Id: testbox.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager - TestBox.
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
import copy;
import sys;
import unittest;

# Validation Kit imports.
from testmanager.core               import db;
from testmanager.core.base          import ModelDataBase, ModelDataBaseTestCase, ModelLogicBase, TMInFligthCollision, \
                                           TMInvalidData, TMTooManyRows, TMRowNotFound, \
                                           ChangeLogEntry, AttributeChangeEntry, AttributeChangeEntryPre;
from testmanager.core.useraccount   import UserAccountLogic;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    xrange = range; # pylint: disable=redefined-builtin,invalid-name


class TestBoxInSchedGroupData(ModelDataBase):
    """
    TestBox in SchedGroup data.
    """

    ksParam_idTestBox           = 'TestBoxInSchedGroup_idTestBox';
    ksParam_idSchedGroup        = 'TestBoxInSchedGroup_idSchedGroup';
    ksParam_tsEffective         = 'TestBoxInSchedGroup_tsEffective';
    ksParam_tsExpire            = 'TestBoxInSchedGroup_tsExpire';
    ksParam_uidAuthor           = 'TestBoxInSchedGroup_uidAuthor';
    ksParam_iSchedPriority      = 'TestBoxInSchedGroup_iSchedPriority';

    kasAllowNullAttributes      = [ 'tsEffective', 'tsExpire', 'uidAuthor', ]

    kiMin_iSchedPriority        = 0;
    kiMax_iSchedPriority        = 32;

    kcDbColumns                 = 6;

    def __init__(self):
        ModelDataBase.__init__(self);
        self.idTestBox          = None;
        self.idSchedGroup       = None;
        self.tsEffective        = None;
        self.tsExpire           = None;
        self.uidAuthor          = None;
        self.iSchedPriority     = 16;

    def initFromDbRow(self, aoRow):
        """
        Expecting the result from a query like this:
            SELECT * FROM TestBoxesInSchedGroups
        """
        if aoRow is None:
            raise TMRowNotFound('TestBox/SchedGroup not found.');

        self.idTestBox          = aoRow[0];
        self.idSchedGroup       = aoRow[1];
        self.tsEffective        = aoRow[2];
        self.tsExpire           = aoRow[3];
        self.uidAuthor          = aoRow[4];
        self.iSchedPriority     = aoRow[5];

        return self;

class TestBoxInSchedGroupDataEx(TestBoxInSchedGroupData):
    """
    Extended version of TestBoxInSchedGroupData that contains the scheduling group.
    """

    def __init__(self):
        TestBoxInSchedGroupData.__init__(self);
        self.oSchedGroup        = None  # type: SchedGroupData

    def initFromDbRowEx(self, aoRow, oDb, tsNow = None, sPeriodBack = None):
        """
        Extended version of initFromDbRow that fills in the rest from the database.
        """
        from testmanager.core.schedgroup import SchedGroupData;
        self.initFromDbRow(aoRow);
        self.oSchedGroup        = SchedGroupData().initFromDbWithId(oDb, self.idSchedGroup, tsNow, sPeriodBack);
        return self;

class TestBoxDataForSchedGroup(TestBoxInSchedGroupData):
    """
    Extended version of TestBoxInSchedGroupData that adds the testbox data (if available).
    Used by TestBoxLogic.fetchForSchedGroup
    """

    def __init__(self):
        TestBoxInSchedGroupData.__init__(self);
        self.oTestBox           = None  # type: TestBoxData

    def initFromDbRow(self, aoRow):
        """
        The row is: TestBoxesInSchedGroups.*, TestBoxesWithStrings.*
        """
        TestBoxInSchedGroupData.initFromDbRow(self, aoRow);
        if aoRow[self.kcDbColumns]:
            self.oTestBox = TestBoxData().initFromDbRow(aoRow[self.kcDbColumns:]);
        else:
            self.oTestBox = None;
        return self;

    def getDataAttributes(self):
        asAttributes = TestBoxInSchedGroupData.getDataAttributes(self);
        asAttributes.remove('oTestBox');
        return asAttributes;

    def _validateAndConvertWorker(self, asAllowNullAttributes, oDb, enmValidateFor = ModelDataBase.ksValidateFor_Other):
        dErrors = TestBoxInSchedGroupData._validateAndConvertWorker(self, asAllowNullAttributes, oDb, enmValidateFor);
        if self.ksParam_idTestBox not in dErrors:
            self.oTestBox = TestBoxData();
            try:
                self.oTestBox.initFromDbWithId(oDb, self.idTestBox);
            except Exception as oXcpt:
                self.oTestBox = TestBoxData()
                dErrors[self.ksParam_idTestBox] = str(oXcpt);
        return dErrors;


# pylint: disable=invalid-name
class TestBoxData(ModelDataBase):  # pylint: disable=too-many-instance-attributes
    """
    TestBox Data.
    """

    ## LomKind_T
    ksLomKind_None                  = 'none';
    ksLomKind_ILOM                  = 'ilom';
    ksLomKind_ELOM                  = 'elom';
    ksLomKind_AppleXserveLom        = 'apple-xserver-lom';
    kasLomKindValues                = [ ksLomKind_None, ksLomKind_ILOM, ksLomKind_ELOM, ksLomKind_AppleXserveLom];
    kaoLomKindDescs                 = \
    [
        ( ksLomKind_None,           'None',             ''),
        ( ksLomKind_ILOM,           'ILOM',             ''),
        ( ksLomKind_ELOM,           'ELOM',             ''),
        ( ksLomKind_AppleXserveLom, 'Apple Xserve LOM', ''),
    ];


    ## TestBoxCmd_T
    ksTestBoxCmd_None               = 'none';
    ksTestBoxCmd_Abort              = 'abort';
    ksTestBoxCmd_Reboot             = 'reboot';
    ksTestBoxCmd_Upgrade            = 'upgrade';
    ksTestBoxCmd_UpgradeAndReboot   = 'upgrade-and-reboot';
    ksTestBoxCmd_Special            = 'special';
    kasTestBoxCmdValues             =  [ ksTestBoxCmd_None, ksTestBoxCmd_Abort, ksTestBoxCmd_Reboot, ksTestBoxCmd_Upgrade,
                                         ksTestBoxCmd_UpgradeAndReboot, ksTestBoxCmd_Special];
    kaoTestBoxCmdDescs              = \
    [
        ( ksTestBoxCmd_None,              'None',                               ''),
        ( ksTestBoxCmd_Abort,             'Abort current test',                 ''),
        ( ksTestBoxCmd_Reboot,            'Reboot TestBox',                     ''),
        ( ksTestBoxCmd_Upgrade,           'Upgrade TestBox Script',             ''),
        ( ksTestBoxCmd_UpgradeAndReboot,  'Upgrade TestBox Script and reboot',  ''),
        ( ksTestBoxCmd_Special,           'Special (reserved)',                 ''),
    ];


    ksIdAttr    = 'idTestBox';
    ksIdGenAttr = 'idGenTestBox';

    ksParam_idTestBox           = 'TestBox_idTestBox';
    ksParam_tsEffective         = 'TestBox_tsEffective';
    ksParam_tsExpire            = 'TestBox_tsExpire';
    ksParam_uidAuthor           = 'TestBox_uidAuthor';
    ksParam_idGenTestBox        = 'TestBox_idGenTestBox';
    ksParam_ip                  = 'TestBox_ip';
    ksParam_uuidSystem          = 'TestBox_uuidSystem';
    ksParam_sName               = 'TestBox_sName';
    ksParam_sDescription        = 'TestBox_sDescription';
    ksParam_fEnabled            = 'TestBox_fEnabled';
    ksParam_enmLomKind          = 'TestBox_enmLomKind';
    ksParam_ipLom               = 'TestBox_ipLom';
    ksParam_pctScaleTimeout     = 'TestBox_pctScaleTimeout';
    ksParam_sComment            = 'TestBox_sComment';
    ksParam_sOs                 = 'TestBox_sOs';
    ksParam_sOsVersion          = 'TestBox_sOsVersion';
    ksParam_sCpuVendor          = 'TestBox_sCpuVendor';
    ksParam_sCpuArch            = 'TestBox_sCpuArch';
    ksParam_sCpuName            = 'TestBox_sCpuName';
    ksParam_lCpuRevision        = 'TestBox_lCpuRevision';
    ksParam_cCpus               = 'TestBox_cCpus';
    ksParam_fCpuHwVirt          = 'TestBox_fCpuHwVirt';
    ksParam_fCpuNestedPaging    = 'TestBox_fCpuNestedPaging';
    ksParam_fCpu64BitGuest      = 'TestBox_fCpu64BitGuest';
    ksParam_fChipsetIoMmu       = 'TestBox_fChipsetIoMmu';
    ksParam_fRawMode            = 'TestBox_fRawMode';
    ksParam_fNativeApi          = 'TestBox_fNativeApi';
    ksParam_cMbMemory           = 'TestBox_cMbMemory';
    ksParam_cMbScratch          = 'TestBox_cMbScratch';
    ksParam_sReport             = 'TestBox_sReport';
    ksParam_iTestBoxScriptRev   = 'TestBox_iTestBoxScriptRev';
    ksParam_iPythonHexVersion   = 'TestBox_iPythonHexVersion';
    ksParam_enmPendingCmd       = 'TestBox_enmPendingCmd';

    kasInternalAttributes       = [ 'idStrDescription', 'idStrComment', 'idStrOs', 'idStrOsVersion', 'idStrCpuVendor',
                                    'idStrCpuArch', 'idStrCpuName', 'idStrReport', ];
    kasMachineSettableOnly      = [ 'sOs', 'sOsVersion', 'sCpuVendor', 'sCpuArch', 'sCpuName', 'lCpuRevision', 'cCpus',
                                    'fCpuHwVirt', 'fCpuNestedPaging', 'fCpu64BitGuest', 'fChipsetIoMmu', 'fRawMode',
                                    'fNativeApi', 'cMbMemory', 'cMbScratch', 'sReport', 'iTestBoxScriptRev',
                                    'iPythonHexVersion', ];
    kasAllowNullAttributes      = ['idTestBox', 'tsEffective', 'tsExpire', 'uidAuthor', 'idGenTestBox', 'sDescription',
                                   'ipLom', 'sComment', ] + kasMachineSettableOnly + kasInternalAttributes;

    kasValidValues_enmLomKind   = kasLomKindValues;
    kasValidValues_enmPendingCmd = kasTestBoxCmdValues;
    kiMin_pctScaleTimeout       =    11;
    kiMax_pctScaleTimeout       = 19999;
    kcchMax_sReport             = 65535;

    kcDbColumns                 = 41; # including the 7 string joins columns


    def __init__(self):
        ModelDataBase.__init__(self);

        #
        # Initialize with defaults.
        # See the database for explanations of each of these fields.
        #
        self.idTestBox           = None;
        self.tsEffective         = None;
        self.tsExpire            = None;
        self.uidAuthor           = None;
        self.idGenTestBox        = None;
        self.ip                  = None;
        self.uuidSystem          = None;
        self.sName               = None;
        self.idStrDescription    = None;
        self.fEnabled            = False;
        self.enmLomKind          = self.ksLomKind_None;
        self.ipLom               = None;
        self.pctScaleTimeout     = 100;
        self.idStrComment        = None;
        self.idStrOs             = None;
        self.idStrOsVersion      = None;
        self.idStrCpuVendor      = None;
        self.idStrCpuArch        = None;
        self.idStrCpuName        = None;
        self.lCpuRevision        = None;
        self.cCpus               = 1;
        self.fCpuHwVirt          = False;
        self.fCpuNestedPaging    = False;
        self.fCpu64BitGuest      = False;
        self.fChipsetIoMmu       = False;
        self.fRawMode            = None;
        self.fNativeApi          = None;
        self.cMbMemory           = 1;
        self.cMbScratch          = 0;
        self.idStrReport         = None;
        self.iTestBoxScriptRev   = 0;
        self.iPythonHexVersion   = 0;
        self.enmPendingCmd       = self.ksTestBoxCmd_None;
        # String table values.
        self.sDescription        = None;
        self.sComment            = None;
        self.sOs                 = None;
        self.sOsVersion          = None;
        self.sCpuVendor          = None;
        self.sCpuArch            = None;
        self.sCpuName            = None;
        self.sReport             = None;

    def initFromDbRow(self, aoRow):
        """
        Internal worker for initFromDbWithId and initFromDbWithGenId as well as
        from TestBoxLogic.  Expecting the result from a query like this:
            SELECT TestBoxesWithStrings.* FROM TestBoxesWithStrings
        """
        if aoRow is None:
            raise TMRowNotFound('TestBox not found.');

        self.idTestBox           = aoRow[0];
        self.tsEffective         = aoRow[1];
        self.tsExpire            = aoRow[2];
        self.uidAuthor           = aoRow[3];
        self.idGenTestBox        = aoRow[4];
        self.ip                  = aoRow[5];
        self.uuidSystem          = aoRow[6];
        self.sName               = aoRow[7];
        self.idStrDescription    = aoRow[8];
        self.fEnabled            = aoRow[9];
        self.enmLomKind          = aoRow[10];
        self.ipLom               = aoRow[11];
        self.pctScaleTimeout     = aoRow[12];
        self.idStrComment        = aoRow[13];
        self.idStrOs             = aoRow[14];
        self.idStrOsVersion      = aoRow[15];
        self.idStrCpuVendor      = aoRow[16];
        self.idStrCpuArch        = aoRow[17];
        self.idStrCpuName        = aoRow[18];
        self.lCpuRevision        = aoRow[19];
        self.cCpus               = aoRow[20];
        self.fCpuHwVirt          = aoRow[21];
        self.fCpuNestedPaging    = aoRow[22];
        self.fCpu64BitGuest      = aoRow[23];
        self.fChipsetIoMmu       = aoRow[24];
        self.fRawMode            = aoRow[25];
        self.fNativeApi          = aoRow[26];
        self.cMbMemory           = aoRow[27];
        self.cMbScratch          = aoRow[28];
        self.idStrReport         = aoRow[29];
        self.iTestBoxScriptRev   = aoRow[30];
        self.iPythonHexVersion   = aoRow[31];
        self.enmPendingCmd       = aoRow[32];

        # String table values.
        if len(aoRow) > 33:
            self.sDescription    = aoRow[33];
            self.sComment        = aoRow[34];
            self.sOs             = aoRow[35];
            self.sOsVersion      = aoRow[36];
            self.sCpuVendor      = aoRow[37];
            self.sCpuArch        = aoRow[38];
            self.sCpuName        = aoRow[39];
            self.sReport         = aoRow[40];

        return self;

    def initFromDbWithId(self, oDb, idTestBox, tsNow = None, sPeriodBack = None):
        """
        Initialize the object from the database.
        """
        oDb.execute(self.formatSimpleNowAndPeriodQuery(oDb,
                                                       'SELECT TestBoxesWithStrings.*\n'
                                                       'FROM   TestBoxesWithStrings\n'
                                                       'WHERE  idTestBox    = %s\n'
                                                       , ( idTestBox, ), tsNow, sPeriodBack));
        aoRow = oDb.fetchOne()
        if aoRow is None:
            raise TMRowNotFound('idTestBox=%s not found (tsNow=%s sPeriodBack=%s)' % (idTestBox, tsNow, sPeriodBack,));
        return self.initFromDbRow(aoRow);

    def initFromDbWithGenId(self, oDb, idGenTestBox, tsNow = None):
        """
        Initialize the object from the database.
        """
        _ = tsNow;                      # Only useful for extended data classes.
        oDb.execute('SELECT TestBoxesWithStrings.*\n'
                    'FROM   TestBoxesWithStrings\n'
                    'WHERE  idGenTestBox = %s\n'
                    , (idGenTestBox, ) );
        return self.initFromDbRow(oDb.fetchOne());

    def _validateAndConvertWorker(self, asAllowNullAttributes, oDb, enmValidateFor = ModelDataBase.ksValidateFor_Other):
        # Override to do extra ipLom checks.
        dErrors = ModelDataBase._validateAndConvertWorker(self, asAllowNullAttributes, oDb, enmValidateFor);
        if    self.ksParam_ipLom      not in dErrors \
          and self.ksParam_enmLomKind not in dErrors \
          and self.enmLomKind != self.ksLomKind_None \
          and self.ipLom is None:
            dErrors[self.ksParam_ipLom] = 'Light-out-management IP is mandatory and a LOM is selected.'
        return dErrors;

    @staticmethod
    def formatPythonVersionEx(iPythonHexVersion):
        """ Unbuttons the version number and formats it as a version string. """
        if iPythonHexVersion is None:
            return 'N/A';
        return 'v%d.%d.%d.%d' \
            % (  iPythonHexVersion >> 24,
                (iPythonHexVersion >> 16) & 0xff,
                (iPythonHexVersion >>  8) & 0xff,
                 iPythonHexVersion        & 0xff);

    def formatPythonVersion(self):
        """ Unbuttons the version number and formats it as a version string. """
        return self.formatPythonVersionEx(self.iPythonHexVersion);


    @staticmethod
    def getCpuFamilyEx(lCpuRevision):
        """ Returns the CPU family for a x86 or amd64 testboxes."""
        if lCpuRevision is None:
            return 0;
        return (lCpuRevision >> 24 & 0xff);

    def getCpuFamily(self):
        """ Returns the CPU family for a x86 or amd64 testboxes."""
        return self.getCpuFamilyEx(self.lCpuRevision);

    @staticmethod
    def getCpuModelEx(lCpuRevision):
        """ Returns the CPU model for a x86 or amd64 testboxes."""
        if lCpuRevision is None:
            return 0;
        return (lCpuRevision >> 8 & 0xffff);

    def getCpuModel(self):
        """ Returns the CPU model for a x86 or amd64 testboxes."""
        return self.getCpuModelEx(self.lCpuRevision);

    @staticmethod
    def getCpuSteppingEx(lCpuRevision):
        """ Returns the CPU stepping for a x86 or amd64 testboxes."""
        if lCpuRevision is None:
            return 0;
        return (lCpuRevision & 0xff);

    def getCpuStepping(self):
        """ Returns the CPU stepping for a x86 or amd64 testboxes."""
        return self.getCpuSteppingEx(self.lCpuRevision);


    # The following is a translation of the g_aenmIntelFamily06 array in CPUMR3CpuId.cpp:
    kdIntelFamily06 = {
        0x00: 'P6',
        0x01: 'P6',
        0x03: 'P6_II',
        0x05: 'P6_II',
        0x06: 'P6_II',
        0x07: 'P6_III',
        0x08: 'P6_III',
        0x09: 'P6_M_Banias',
        0x0a: 'P6_III',
        0x0b: 'P6_III',
        0x0d: 'P6_M_Dothan',
        0x0e: 'Core_Yonah',
        0x0f: 'Core2_Merom',
        0x15: 'P6_M_Dothan',
        0x16: 'Core2_Merom',
        0x17: 'Core2_Penryn',
        0x1a: 'Core7_Nehalem',
        0x1c: 'Atom_Bonnell',
        0x1d: 'Core2_Penryn',
        0x1e: 'Core7_Nehalem',
        0x1f: 'Core7_Nehalem',
        0x25: 'Core7_Westmere',
        0x26: 'Atom_Lincroft',
        0x27: 'Atom_Saltwell',
        0x2a: 'Core7_SandyBridge',
        0x2c: 'Core7_Westmere',
        0x2d: 'Core7_SandyBridge',
        0x2e: 'Core7_Nehalem',
        0x2f: 'Core7_Westmere',
        0x35: 'Atom_Saltwell',
        0x36: 'Atom_Saltwell',
        0x37: 'Atom_Silvermont',
        0x3a: 'Core7_IvyBridge',
        0x3c: 'Core7_Haswell',
        0x3d: 'Core7_Broadwell',
        0x3e: 'Core7_IvyBridge',
        0x3f: 'Core7_Haswell',
        0x45: 'Core7_Haswell',
        0x46: 'Core7_Haswell',
        0x47: 'Core7_Broadwell',
        0x4a: 'Atom_Silvermont',
        0x4c: 'Atom_Airmount',
        0x4d: 'Atom_Silvermont',
        0x4e: 'Core7_Skylake',
        0x4f: 'Core7_Broadwell',
        0x55: 'Core7_Skylake',
        0x56: 'Core7_Broadwell',
        0x5a: 'Atom_Silvermont',
        0x5c: 'Atom_Goldmont',
        0x5d: 'Atom_Silvermont',
        0x5e: 'Core7_Skylake',
        0x66: 'Core7_Cannonlake',
    };
    # Also from CPUMR3CpuId.cpp, but the switch.
    kdIntelFamily15 = {
        0x00: 'NB_Willamette',
        0x01: 'NB_Willamette',
        0x02: 'NB_Northwood',
        0x03: 'NB_Prescott',
        0x04: 'NB_Prescott2M',
        0x05: 'NB_Unknown',
        0x06: 'NB_CedarMill',
        0x07: 'NB_Gallatin',
    };

    @staticmethod
    def queryCpuMicroarchEx(lCpuRevision, sCpuVendor):
        """ Try guess the microarch name for the cpu.  Returns None if we cannot. """
        if lCpuRevision is None or sCpuVendor is None:
            return None;
        uFam = TestBoxData.getCpuFamilyEx(lCpuRevision);
        uMod = TestBoxData.getCpuModelEx(lCpuRevision);
        if sCpuVendor == 'GenuineIntel':
            if uFam == 6:
                return TestBoxData.kdIntelFamily06.get(uMod, None);
            if uFam == 15:
                return TestBoxData.kdIntelFamily15.get(uMod, None);
        elif sCpuVendor == 'AuthenticAMD':
            if uFam == 0xf:
                if uMod < 0x10:                             return 'K8_130nm';
                if 0x60 <= uMod < 0x80:                     return 'K8_65nm';
                if uMod >= 0x40:                            return 'K8_90nm_AMDV';
                if uMod in [0x21, 0x23, 0x2b, 0x37, 0x3f]:  return 'K8_90nm_DualCore';
                return 'AMD_K8_90nm';
            if uFam == 0x10:                                return 'K10';
            if uFam == 0x11:                                return 'K10_Lion';
            if uFam == 0x12:                                return 'K10_Llano';
            if uFam == 0x14:                                return 'Bobcat';
            if uFam == 0x15:
                if uMod <= 0x01:                            return 'Bulldozer';
                if uMod in [0x02, 0x10, 0x13]:              return 'Piledriver';
                return None;
            if uFam == 0x16:
                return 'Jaguar';
        elif sCpuVendor == 'CentaurHauls':
            if uFam == 0x05:
                if uMod == 0x01: return 'Centaur_C6';
                if uMod == 0x04: return 'Centaur_C6';
                if uMod == 0x08: return 'Centaur_C2';
                if uMod == 0x09: return 'Centaur_C3';
            if uFam == 0x06:
                if uMod == 0x05: return 'VIA_C3_M2';
                if uMod == 0x06: return 'VIA_C3_C5A';
                if uMod == 0x07: return 'VIA_C3_C5B' if TestBoxData.getCpuSteppingEx(lCpuRevision) < 8 else 'VIA_C3_C5C';
                if uMod == 0x08: return 'VIA_C3_C5N';
                if uMod == 0x09: return 'VIA_C3_C5XL' if TestBoxData.getCpuSteppingEx(lCpuRevision) < 8 else 'VIA_C3_C5P';
                if uMod == 0x0a: return 'VIA_C7_C5J';
                if uMod == 0x0f: return 'VIA_Isaiah';
        elif sCpuVendor == '  Shanghai  ':
            if uFam == 0x07:
                if uMod == 0x0b: return 'Shanghai_KX-5000';
        return None;

    def queryCpuMicroarch(self):
        """ Try guess the microarch name for the cpu.  Returns None if we cannot. """
        return self.queryCpuMicroarchEx(self.lCpuRevision, self.sCpuVendor);

    @staticmethod
    def getPrettyCpuVersionEx(lCpuRevision, sCpuVendor):
        """ Pretty formatting of the family/model/stepping with microarch optimizations. """
        if lCpuRevision is None or sCpuVendor is None:
            return u'<none>';
        sMarch = TestBoxData.queryCpuMicroarchEx(lCpuRevision, sCpuVendor);
        if sMarch is not None:
            return '%s %02x:%x' \
                 % (sMarch, TestBoxData.getCpuModelEx(lCpuRevision), TestBoxData.getCpuSteppingEx(lCpuRevision));
        return 'fam%02X m%02X s%02X' \
             % ( TestBoxData.getCpuFamilyEx(lCpuRevision), TestBoxData.getCpuModelEx(lCpuRevision),
                 TestBoxData.getCpuSteppingEx(lCpuRevision));

    def getPrettyCpuVersion(self):
        """ Pretty formatting of the family/model/stepping with microarch optimizations. """
        return self.getPrettyCpuVersionEx(self.lCpuRevision, self.sCpuVendor);

    def getArchBitString(self):
        """ Returns 32-bit, 64-bit, <none>, or sCpuArch. """
        if self.sCpuArch is None:
            return '<none>';
        if self.sCpuArch in [ 'x86',]:
            return '32-bit';
        if self.sCpuArch in [ 'amd64',]:
            return '64-bit';
        return self.sCpuArch;

    def getPrettyCpuVendor(self):
        """ Pretty vendor name."""
        if self.sCpuVendor is None:
            return '<none>';
        if self.sCpuVendor == 'GenuineIntel':     return 'Intel';
        if self.sCpuVendor == 'AuthenticAMD':     return 'AMD';
        if self.sCpuVendor == 'CentaurHauls':     return 'VIA';
        if self.sCpuVendor == '  Shanghai  ':     return 'Shanghai';
        return self.sCpuVendor;


class TestBoxDataEx(TestBoxData):
    """
    TestBox data.
    """

    ksParam_aoInSchedGroups = 'TestBox_aoInSchedGroups';

    # Use [] instead of None.
    kasAltArrayNull = [ 'aoInSchedGroups', ];

    ## Helper parameter containing the comma separated list with the IDs of
    #  potential members found in the parameters.
    ksParam_aidSchedGroups = 'TestBoxDataEx_aidSchedGroups';

    def __init__(self):
        TestBoxData.__init__(self);
        self.aoInSchedGroups        = []    # type: list[TestBoxInSchedGroupData]

    def _initExtraMembersFromDb(self, oDb, tsNow = None, sPeriodBack = None):
        """
        Worker shared by the initFromDb* methods.
        Returns self.  Raises exception if no row or database error.
        """
        oDb.execute(self.formatSimpleNowAndPeriodQuery(oDb,
                                                       'SELECT  *\n'
                                                       'FROM    TestBoxesInSchedGroups\n'
                                                       'WHERE   idTestBox = %s\n'
                                                       , (self.idTestBox,), tsNow, sPeriodBack)
                    + 'ORDER BY idSchedGroup\n' );
        self.aoInSchedGroups = [];
        for aoRow in oDb.fetchAll():
            self.aoInSchedGroups.append(TestBoxInSchedGroupDataEx().initFromDbRowEx(aoRow, oDb, tsNow, sPeriodBack));
        return self;

    def initFromDbRowEx(self, aoRow, oDb, tsNow = None):
        """
        Reinitialize from a SELECT * FROM TestBoxesWithStrings row.  Will query the
        necessary additional data from oDb using tsNow.
        Returns self.  Raises exception if no row or database error.
        """
        TestBoxData.initFromDbRow(self, aoRow);
        return self._initExtraMembersFromDb(oDb, tsNow);

    def initFromDbWithId(self, oDb, idTestBox, tsNow = None, sPeriodBack = None):
        """
        Initialize the object from the database.
        """
        TestBoxData.initFromDbWithId(self, oDb, idTestBox, tsNow, sPeriodBack);
        return self._initExtraMembersFromDb(oDb, tsNow, sPeriodBack);

    def initFromDbWithGenId(self, oDb, idGenTestBox, tsNow = None):
        """
        Initialize the object from the database.
        """
        TestBoxData.initFromDbWithGenId(self, oDb, idGenTestBox);
        if tsNow is None and not oDb.isTsInfinity(self.tsExpire):
            tsNow = self.tsEffective;
        return self._initExtraMembersFromDb(oDb, tsNow);

    def getAttributeParamNullValues(self, sAttr): # Necessary?
        if sAttr in ['aoInSchedGroups', ]:
            return [[], ''];
        return TestBoxData.getAttributeParamNullValues(self, sAttr);

    def convertParamToAttribute(self, sAttr, sParam, oValue, oDisp, fStrict):
        """
        For dealing with the in-scheduling-group list.
        """
        if sAttr != 'aoInSchedGroups':
            return TestBoxData.convertParamToAttribute(self, sAttr, sParam, oValue, oDisp, fStrict);

        aoNewValues = [];
        aidSelected = oDisp.getListOfIntParams(sParam, iMin = 1, iMax = 0x7ffffffe, aiDefaults = []);
        asIds       = oDisp.getStringParam(self.ksParam_aidSchedGroups, sDefault = '').split(',');
        for idSchedGroup in asIds:
            try:    idSchedGroup = int(idSchedGroup);
            except: pass;
            oDispWrapper = self.DispWrapper(oDisp, '%s[%s][%%s]' % (TestBoxDataEx.ksParam_aoInSchedGroups, idSchedGroup,))
            oMember = TestBoxInSchedGroupData().initFromParams(oDispWrapper, fStrict = False);
            if idSchedGroup in aidSelected:
                aoNewValues.append(oMember);
        return aoNewValues;

    def _validateAndConvertAttribute(self, sAttr, sParam, oValue, aoNilValues, fAllowNull, oDb): # pylint: disable=too-many-locals
        """
        Validate special arrays and requirement expressions.

        Some special needs for the in-scheduling-group list.
        """
        if sAttr != 'aoInSchedGroups':
            return TestBoxData._validateAndConvertAttribute(self, sAttr, sParam, oValue, aoNilValues, fAllowNull, oDb);

        asErrors = [];
        aoNewValues = [];

        # Note! We'll be returning an error dictionary instead of an string here.
        dErrors = {};

        # HACK ALERT! idTestBox might not have been validated and converted yet, but we need detect
        #             adding so we can ignore idTestBox being NIL when validating group memberships.
        ## @todo make base.py pass us the ksValidateFor_Xxxx value.
        fIsAdding = bool(self.idTestBox in [ None, -1, '-1', 'None', '' ])

        for iInGrp, oInSchedGroup in enumerate(self.aoInSchedGroups):
            oInSchedGroup = copy.copy(oInSchedGroup);
            oInSchedGroup.idTestBox = self.idTestBox;
            if fIsAdding:
                dCurErrors = oInSchedGroup.validateAndConvertEx(['idTestBox',] + oInSchedGroup.kasAllowNullAttributes,
                                                                oDb, ModelDataBase.ksValidateFor_Add);
            else:
                dCurErrors = oInSchedGroup.validateAndConvert(oDb, ModelDataBase.ksValidateFor_Other);
            if not dCurErrors:
                pass; ## @todo figure out the ID?
            else:
                asErrors = [];
                for sKey in dCurErrors:
                    asErrors.append('%s: %s' % (sKey[len('TestBoxInSchedGroup_'):],
                                                dCurErrors[sKey] + ('{%s}' % self.idTestBox)))
                dErrors[iInGrp] = '<br>\n'.join(asErrors)
            aoNewValues.append(oInSchedGroup);

        for iInGrp, oInSchedGroup in enumerate(self.aoInSchedGroups):
            for iInGrp2 in xrange(iInGrp + 1, len(self.aoInSchedGroups)):
                if self.aoInSchedGroups[iInGrp2].idSchedGroup == oInSchedGroup.idSchedGroup:
                    sMsg = 'Duplicate scheduling group #%s".' % (oInSchedGroup.idSchedGroup,);
                    if iInGrp in dErrors:   dErrors[iInGrp]  += '<br>\n' + sMsg;
                    else:                   dErrors[iInGrp]   = sMsg;
                    if iInGrp2 in dErrors:  dErrors[iInGrp2] += '<br>\n' + sMsg;
                    else:                   dErrors[iInGrp2]  = sMsg;
                    break;

        return (aoNewValues, dErrors if dErrors else None);


class TestBoxLogic(ModelLogicBase):
    """
    TestBox logic.
    """

    kiSortColumn_sName              =  1;
    kiSortColumn_sOs                =  2;
    kiSortColumn_sOsVersion         =  3;
    kiSortColumn_sCpuVendor         =  4;
    kiSortColumn_sCpuArch           =  5;
    kiSortColumn_lCpuRevision       =  6;
    kiSortColumn_cCpus              =  7;
    kiSortColumn_cMbMemory          =  8;
    kiSortColumn_cMbScratch         =  9;
    kiSortColumn_fCpuNestedPaging   = 10;
    kiSortColumn_fNativeApi         = 11;
    kiSortColumn_iTestBoxScriptRev  = 12;
    kiSortColumn_iPythonHexVersion  = 13;
    kiSortColumn_enmPendingCmd      = 14;
    kiSortColumn_fEnabled           = 15;
    kiSortColumn_enmState           = 16;
    kiSortColumn_tsUpdated          = 17;
    kcMaxSortColumns                = 18;
    kdSortColumnMap                 = {
        0:                               'TestBoxesWithStrings.sName',
        kiSortColumn_sName:              "regexp_replace(TestBoxesWithStrings.sName,'[0-9]*', '', 'g'), " \
                            "RIGHT(CONCAT(regexp_replace(TestBoxesWithStrings.sName,'[^0-9]*','', 'g'),'0'),8)::int",
        -kiSortColumn_sName:             "regexp_replace(TestBoxesWithStrings.sName,'[0-9]*', '', 'g') DESC, " \
                            "RIGHT(CONCAT(regexp_replace(TestBoxesWithStrings.sName,'[^0-9]*','', 'g'),'0'),8)::int DESC",
        kiSortColumn_sOs:                'TestBoxesWithStrings.sOs',
        -kiSortColumn_sOs:               'TestBoxesWithStrings.sOs DESC',
        kiSortColumn_sOsVersion:         'TestBoxesWithStrings.sOsVersion',
        -kiSortColumn_sOsVersion:        'TestBoxesWithStrings.sOsVersion DESC',
        kiSortColumn_sCpuVendor:         'TestBoxesWithStrings.sCpuVendor',
        -kiSortColumn_sCpuVendor:        'TestBoxesWithStrings.sCpuVendor DESC',
        kiSortColumn_sCpuArch:           'TestBoxesWithStrings.sCpuArch',
        -kiSortColumn_sCpuArch:          'TestBoxesWithStrings.sCpuArch DESC',
        kiSortColumn_lCpuRevision:       'TestBoxesWithStrings.lCpuRevision',
        -kiSortColumn_lCpuRevision:      'TestBoxesWithStrings.lCpuRevision DESC',
        kiSortColumn_cCpus:              'TestBoxesWithStrings.cCpus',
        -kiSortColumn_cCpus:             'TestBoxesWithStrings.cCpus DESC',
        kiSortColumn_cMbMemory:          'TestBoxesWithStrings.cMbMemory',
        -kiSortColumn_cMbMemory:         'TestBoxesWithStrings.cMbMemory DESC',
        kiSortColumn_cMbScratch:         'TestBoxesWithStrings.cMbScratch',
        -kiSortColumn_cMbScratch:        'TestBoxesWithStrings.cMbScratch DESC',
        kiSortColumn_fCpuNestedPaging:   'TestBoxesWithStrings.fCpuNestedPaging',
        -kiSortColumn_fCpuNestedPaging:  'TestBoxesWithStrings.fCpuNestedPaging DESC',
        kiSortColumn_fNativeApi:         'TestBoxesWithStrings.fNativeApi',
        -kiSortColumn_fNativeApi:        'TestBoxesWithStrings.fNativeApi DESC',
        kiSortColumn_iTestBoxScriptRev:  'TestBoxesWithStrings.iTestBoxScriptRev',
        -kiSortColumn_iTestBoxScriptRev: 'TestBoxesWithStrings.iTestBoxScriptRev DESC',
        kiSortColumn_iPythonHexVersion:  'TestBoxesWithStrings.iPythonHexVersion',
        -kiSortColumn_iPythonHexVersion: 'TestBoxesWithStrings.iPythonHexVersion DESC',
        kiSortColumn_enmPendingCmd:      'TestBoxesWithStrings.enmPendingCmd',
        -kiSortColumn_enmPendingCmd:     'TestBoxesWithStrings.enmPendingCmd DESC',
        kiSortColumn_fEnabled:           'TestBoxesWithStrings.fEnabled',
        -kiSortColumn_fEnabled:          'TestBoxesWithStrings.fEnabled DESC',
        kiSortColumn_enmState:           'TestBoxStatuses.enmState',
        -kiSortColumn_enmState:          'TestBoxStatuses.enmState DESC',
        kiSortColumn_tsUpdated:          'TestBoxStatuses.tsUpdated',
        -kiSortColumn_tsUpdated:         'TestBoxStatuses.tsUpdated DESC',
    };

    def __init__(self, oDb):
        ModelLogicBase.__init__(self, oDb);
        self.dCache = None;

    def tryFetchTestBoxByUuid(self, sTestBoxUuid):
        """
        Tries to fetch a testbox by its UUID alone.
        """
        self._oDb.execute('SELECT   TestBoxesWithStrings.*\n'
                          'FROM     TestBoxesWithStrings\n'
                          'WHERE    uuidSystem = %s\n'
                          '     AND tsExpire   = \'infinity\'::timestamp\n'
                          'ORDER BY tsEffective DESC\n',
                          (sTestBoxUuid,));
        if self._oDb.getRowCount() == 0:
            return None;
        if self._oDb.getRowCount() != 1:
            raise TMTooManyRows('Database integrity error: %u hits' % (self._oDb.getRowCount(),));
        oData = TestBoxData();
        oData.initFromDbRow(self._oDb.fetchOne());
        return oData;

    def fetchForListing(self, iStart, cMaxRows, tsNow, aiSortColumns = None):
        """
        Fetches testboxes for listing.

        Returns an array (list) of TestBoxDataForListing items, empty list if none.
        The TestBoxDataForListing instances are just TestBoxData with two extra
        members, an extra oStatus member that is either None or a TestBoxStatusData
        instance, and a member tsCurrent holding CURRENT_TIMESTAMP.

        Raises exception on error.
        """
        class TestBoxDataForListing(TestBoxDataEx):
            """ We add two members for the listing. """
            def __init__(self):
                TestBoxDataEx.__init__(self);
                self.tsCurrent = None;  # CURRENT_TIMESTAMP
                self.oStatus   = None   # type: TestBoxStatusData

        from testmanager.core.testboxstatus import TestBoxStatusData;

        if not aiSortColumns:
            aiSortColumns = [self.kiSortColumn_sName,];

        if tsNow is None:
            self._oDb.execute('SELECT   TestBoxesWithStrings.*,\n'
                              '         TestBoxStatuses.*\n'
                              'FROM     TestBoxesWithStrings\n'
                              '         LEFT OUTER JOIN TestBoxStatuses\n'
                              '                      ON TestBoxStatuses.idTestBox = TestBoxesWithStrings.idTestBox\n'
                              'WHERE    TestBoxesWithStrings.tsExpire = \'infinity\'::TIMESTAMP\n'
                              'ORDER BY ' + (', '.join([self.kdSortColumnMap[i] for i in aiSortColumns])) + '\n'
                              'LIMIT %s OFFSET %s\n'
                              , (cMaxRows, iStart,));
        else:
            self._oDb.execute('SELECT   TestBoxesWithStrings.*,\n'
                              '         TestBoxStatuses.*\n'
                              'FROM     TestBoxesWithStrings\n'
                              '         LEFT OUTER JOIN TestBoxStatuses\n'
                              '                      ON TestBoxStatuses.idTestBox = TestBoxesWithStrings.idTestBox\n'
                              'WHERE    tsExpire     > %s\n'
                              '     AND tsEffective <= %s\n'
                              'ORDER BY ' + (', '.join([self.kdSortColumnMap[i] for i in aiSortColumns])) + '\n'
                              'LIMIT %s OFFSET %s\n'
                              , ( tsNow, tsNow, cMaxRows, iStart,));

        aoRows = [];
        for aoOne in self._oDb.fetchAll():
            oTestBox = TestBoxDataForListing().initFromDbRowEx(aoOne, self._oDb, tsNow);
            oTestBox.tsCurrent = self._oDb.getCurrentTimestamp();
            if aoOne[TestBoxData.kcDbColumns] is not None:
                oTestBox.oStatus = TestBoxStatusData().initFromDbRow(aoOne[TestBoxData.kcDbColumns:]);
            aoRows.append(oTestBox);
        return aoRows;

    def fetchForSchedGroup(self, idSchedGroup, tsNow, aiSortColumns = None):
        """
        Fetches testboxes for listing.

        Returns an array (list) of TestBoxDataForSchedGroup items, empty list if none.

        Raises exception on error.
        """
        if not aiSortColumns:
            aiSortColumns = [self.kiSortColumn_sName,];
        asSortColumns = [self.kdSortColumnMap[i] for i in aiSortColumns];
        asSortColumns.append('TestBoxesInSchedGroups.idTestBox');

        if tsNow is None:
            self._oDb.execute('''
SELECT  TestBoxesInSchedGroups.*,
        TestBoxesWithStrings.*
FROM    TestBoxesInSchedGroups
        LEFT OUTER JOIN TestBoxesWithStrings
                     ON TestBoxesWithStrings.idTestBox = TestBoxesInSchedGroups.idTestBox
                    AND TestBoxesWithStrings.tsExpire  = 'infinity'::TIMESTAMP
WHERE   TestBoxesInSchedGroups.idSchedGroup = %s
    AND TestBoxesInSchedGroups.tsExpire     = 'infinity'::TIMESTAMP
ORDER BY ''' + ', '.join(asSortColumns), (idSchedGroup, ));
        else:
            self._oDb.execute('''
SELECT  TestBoxesInSchedGroups.*,
        TestBoxesWithStrings.*
FROM    TestBoxesInSchedGroups
        LEFT OUTER JOIN TestBoxesWithStrings
                     ON TestBoxesWithStrings.idTestBox    = TestBoxesInSchedGroups.idTestBox
                    AND TestBoxesWithStrings.tsExpire     > %s
                    AND TestBoxesWithStrings.tsEffective <= %s
WHERE   TestBoxesInSchedGroups.idSchedGroup = %s
    AND TestBoxesInSchedGroups.tsExpire     > %s
    AND TestBoxesInSchedGroups.tsEffective <= %s
ORDER BY ''' + ', '.join(asSortColumns), (tsNow, tsNow, idSchedGroup, tsNow, tsNow, ));

        aoRows = [];
        for aoOne in self._oDb.fetchAll():
            aoRows.append(TestBoxDataForSchedGroup().initFromDbRow(aoOne));
        return aoRows;

    def fetchForChangeLog(self, idTestBox, iStart, cMaxRows, tsNow): # pylint: disable=too-many-locals
        """
        Fetches change log entries for a testbox.

        Returns an array of ChangeLogEntry instance and an indicator whether
        there are more entries.
        Raises exception on error.
        """

        ## @todo calc changes to scheduler group!

        if tsNow is None:
            tsNow = self._oDb.getCurrentTimestamp();

        self._oDb.execute('SELECT   TestBoxesWithStrings.*\n'
                          'FROM     TestBoxesWithStrings\n'
                          'WHERE    TestBoxesWithStrings.tsEffective <= %s\n'
                          '     AND TestBoxesWithStrings.idTestBox    = %s\n'
                          'ORDER BY TestBoxesWithStrings.tsExpire DESC\n'
                          'LIMIT %s OFFSET %s\n'
                          , (tsNow, idTestBox, cMaxRows + 1, iStart,));

        aoRows = [];
        for aoDbRow in self._oDb.fetchAll():
            aoRows.append(TestBoxData().initFromDbRow(aoDbRow));

        # Calculate the changes.
        aoEntries = [];
        for i in xrange(0, len(aoRows) - 1):
            oNew      = aoRows[i];
            oOld      = aoRows[i + 1];
            aoChanges = [];
            for sAttr in oNew.getDataAttributes():
                if sAttr not in [ 'tsEffective', 'tsExpire', 'uidAuthor', ]:
                    oOldAttr = getattr(oOld, sAttr);
                    oNewAttr = getattr(oNew, sAttr);
                    if oOldAttr != oNewAttr:
                        if sAttr == 'sReport':
                            aoChanges.append(AttributeChangeEntryPre(sAttr, oNewAttr, oOldAttr, str(oNewAttr), str(oOldAttr)));
                        else:
                            aoChanges.append(AttributeChangeEntry(sAttr, oNewAttr, oOldAttr, str(oNewAttr), str(oOldAttr)));
            aoEntries.append(ChangeLogEntry(oNew.uidAuthor, None, oNew.tsEffective, oNew.tsExpire, oNew, oOld, aoChanges));

        # If we're at the end of the log, add the initial entry.
        if len(aoRows) <= cMaxRows and aoRows:
            oNew = aoRows[-1];
            aoEntries.append(ChangeLogEntry(oNew.uidAuthor, None, oNew.tsEffective, oNew.tsExpire, oNew, None, []));

        UserAccountLogic(self._oDb).resolveChangeLogAuthors(aoEntries);
        return (aoEntries, len(aoRows) > cMaxRows);

    def _validateAndConvertData(self, oData, enmValidateFor):
        # type: (TestBoxDataEx, str) -> None
        """
        Helper for addEntry and editEntry that validates the scheduling group IDs in
        addtion to what's covered by the default validateAndConvert of the data object.

        Raises exception on invalid input.
        """
        dDataErrors = oData.validateAndConvert(self._oDb, enmValidateFor);
        if dDataErrors:
            raise TMInvalidData('TestBoxLogic.addEntry: %s' % (dDataErrors,));
        if isinstance(oData, TestBoxDataEx):
            if oData.aoInSchedGroups:
                sSchedGrps = ', '.join('(%s)' % oCur.idSchedGroup for oCur in oData.aoInSchedGroups);
                self._oDb.execute('SELECT   SchedGroupIDs.idSchedGroup\n'
                                  'FROM     (VALUES ' + sSchedGrps + ' ) AS SchedGroupIDs(idSchedGroup)\n'
                                  '         LEFT OUTER JOIN SchedGroups\n'
                                  '                      ON     SchedGroupIDs.idSchedGroup = SchedGroups.idSchedGroup\n'
                                  '                         AND SchedGroups.tsExpire = \'infinity\'::TIMESTAMP\n'
                                  'WHERE    SchedGroups.idSchedGroup IS NULL\n');
                aaoRows = self._oDb.fetchAll();
                if aaoRows:
                    raise TMInvalidData('TestBoxLogic.addEntry missing scheduling groups: %s'
                                        % (', '.join(str(aoRow[0]) for aoRow in aaoRows),));
        return None;

    def addEntry(self, oData, uidAuthor, fCommit = False):
        # type: (TestBoxDataEx, int, bool) -> (int, int, datetime.datetime)
        """
        Creates a testbox in the database.
        Returns the testbox ID, testbox generation ID and effective timestamp
        of the created testbox on success.  Throws error on failure.
        """

        #
        # Validate. Extra work because of missing foreign key (due to history).
        #
        self._validateAndConvertData(oData, oData.ksValidateFor_Add);

        #
        # Do it.
        #
        self._oDb.callProc('TestBoxLogic_addEntry'
                           , ( uidAuthor,
                               oData.ip,            # Should we allow setting the IP?
                               oData.uuidSystem,
                               oData.sName,
                               oData.sDescription,
                               oData.fEnabled,
                               oData.enmLomKind,
                               oData.ipLom,
                               oData.pctScaleTimeout,
                               oData.sComment,
                               oData.enmPendingCmd, ) );
        (idTestBox, idGenTestBox, tsEffective) = self._oDb.fetchOne();

        for oInSchedGrp in oData.aoInSchedGroups:
            self._oDb.callProc('TestBoxLogic_addGroupEntry',
                               ( uidAuthor, idTestBox, oInSchedGrp.idSchedGroup, oInSchedGrp.iSchedPriority,) );

        self._oDb.maybeCommit(fCommit);
        return (idTestBox, idGenTestBox, tsEffective);


    def editEntry(self, oData, uidAuthor, fCommit = False):
        """
        Data edit update, web UI is the primary user.

        oData is either TestBoxDataEx or TestBoxData.  The latter is for enabling
        Returns the new generation ID and effective date.
        """

        #
        # Validate.
        #
        self._validateAndConvertData(oData, oData.ksValidateFor_Edit);

        #
        # Get current data.
        #
        oOldData = TestBoxDataEx().initFromDbWithId(self._oDb, oData.idTestBox);

        #
        # Do it.
        #
        if not oData.isEqualEx(oOldData, [ 'tsEffective', 'tsExpire', 'uidAuthor', 'aoInSchedGroups', ]
                                         + TestBoxData.kasMachineSettableOnly ):
            self._oDb.callProc('TestBoxLogic_editEntry'
                               , ( uidAuthor,
                                   oData.idTestBox,
                                   oData.ip,            # Should we allow setting the IP?
                                   oData.uuidSystem,
                                   oData.sName,
                                   oData.sDescription,
                                   oData.fEnabled,
                                   oData.enmLomKind,
                                   oData.ipLom,
                                   oData.pctScaleTimeout,
                                   oData.sComment,
                                   oData.enmPendingCmd, ));
            (idGenTestBox, tsEffective) = self._oDb.fetchOne();
        else:
            idGenTestBox = oOldData.idGenTestBox;
            tsEffective  = oOldData.tsEffective;

        if isinstance(oData, TestBoxDataEx):
            # Calc in-group changes.
            aoRemoved = list(oOldData.aoInSchedGroups);
            aoNew     = [];
            aoUpdated = [];
            for oNewInGroup in oData.aoInSchedGroups:
                oOldInGroup = None;
                for iCur, oCur in enumerate(aoRemoved):
                    if oCur.idSchedGroup == oNewInGroup.idSchedGroup:
                        oOldInGroup = aoRemoved.pop(iCur);
                        break;
                if oOldInGroup is None:
                    aoNew.append(oNewInGroup);
                elif oNewInGroup.iSchedPriority != oOldInGroup.iSchedPriority:
                    aoUpdated.append(oNewInGroup);

            # Remove in-groups.
            for oInGroup in aoRemoved:
                self._oDb.callProc('TestBoxLogic_removeGroupEntry', (uidAuthor, oData.idTestBox, oInGroup.idSchedGroup, ));

            # Add new ones.
            for oInGroup in aoNew:
                self._oDb.callProc('TestBoxLogic_addGroupEntry',
                                   ( uidAuthor, oData.idTestBox, oInGroup.idSchedGroup, oInGroup.iSchedPriority, ) );

            # Edit existing ones.
            for oInGroup in aoUpdated:
                self._oDb.callProc('TestBoxLogic_editGroupEntry',
                                   ( uidAuthor, oData.idTestBox, oInGroup.idSchedGroup, oInGroup.iSchedPriority, ) );
        else:
            assert isinstance(oData, TestBoxData);

        self._oDb.maybeCommit(fCommit);
        return (idGenTestBox, tsEffective);


    def removeEntry(self, uidAuthor, idTestBox, fCascade = False, fCommit = False):
        """
        Delete test box and scheduling group associations.
        """
        self._oDb.callProc('TestBoxLogic_removeEntry'
                           , ( uidAuthor, idTestBox, fCascade,));
        self._oDb.maybeCommit(fCommit);
        return True;


    def updateOnSignOn(self, idTestBox, idGenTestBox, sTestBoxAddr, sOs, sOsVersion, # pylint: disable=too-many-arguments,too-many-locals
                       sCpuVendor, sCpuArch, sCpuName, lCpuRevision, cCpus, fCpuHwVirt, fCpuNestedPaging, fCpu64BitGuest,
                       fChipsetIoMmu, fRawMode, fNativeApi, cMbMemory, cMbScratch, sReport, iTestBoxScriptRev, iPythonHexVersion):
        """
        Update the testbox attributes automatically on behalf of the testbox script.
        Returns the new generation id on success, raises an exception on failure.
        """
        _ = idGenTestBox;
        self._oDb.callProc('TestBoxLogic_updateOnSignOn'
                           , ( idTestBox,
                               sTestBoxAddr,
                               sOs,
                               sOsVersion,
                               sCpuVendor,
                               sCpuArch,
                               sCpuName,
                               lCpuRevision,
                               cCpus,
                               fCpuHwVirt,
                               fCpuNestedPaging,
                               fCpu64BitGuest,
                               fChipsetIoMmu,
                               fRawMode,
                               fNativeApi,
                               cMbMemory,
                               cMbScratch,
                               sReport,
                               iTestBoxScriptRev,
                               iPythonHexVersion,));
        return self._oDb.fetchOne()[0];


    def setCommand(self, idTestBox, sOldCommand, sNewCommand, uidAuthor = None, fCommit = False, sComment = None):
        """
        Sets or resets the pending command on a testbox.
        Returns (idGenTestBox, tsEffective) of the new row.
        """
        ## @todo throw TMInFligthCollision again...
        self._oDb.callProc('TestBoxLogic_setCommand'
                           , ( uidAuthor, idTestBox, sOldCommand, sNewCommand, sComment,));
        aoRow = self._oDb.fetchOne();
        self._oDb.maybeCommit(fCommit);
        return (aoRow[0], aoRow[1]);


    def getAll(self):
        """
        Retrieve list of all registered Test Box records from DB.
        """
        self._oDb.execute('SELECT   *\n'
                          'FROM     TestBoxesWithStrings\n'
                          'WHERE    tsExpire=\'infinity\'::timestamp\n'
                          'ORDER BY sName')

        aaoRows = self._oDb.fetchAll()
        aoRet = []
        for aoRow in aaoRows:
            aoRet.append(TestBoxData().initFromDbRow(aoRow))
        return aoRet


    def cachedLookup(self, idTestBox):
        # type: (int) -> TestBoxDataEx
        """
        Looks up the most recent TestBoxData object for idTestBox via
        an object cache.

        Returns a shared TestBoxDataEx object.  None if not found.
        Raises exception on DB error.
        """
        if self.dCache is None:
            self.dCache = self._oDb.getCache('TestBoxData');
        oEntry = self.dCache.get(idTestBox, None);
        if oEntry is None:
            fNeedNow = False;
            self._oDb.execute('SELECT   TestBoxesWithStrings.*\n'
                              'FROM     TestBoxesWithStrings\n'
                              'WHERE    idTestBox  = %s\n'
                              '     AND tsExpire   = \'infinity\'::TIMESTAMP\n'
                              , (idTestBox, ));
            if self._oDb.getRowCount() == 0:
                # Maybe it was deleted, try get the last entry.
                self._oDb.execute('SELECT   TestBoxesWithStrings.*\n'
                                  'FROM     TestBoxesWithStrings\n'
                                  'WHERE    idTestBox = %s\n'
                                  'ORDER BY tsExpire DESC\n'
                                  'LIMIT 1\n'
                                  , (idTestBox, ));
                fNeedNow = True;
            elif self._oDb.getRowCount() > 1:
                raise self._oDb.integrityException('%s infinity rows for %s' % (self._oDb.getRowCount(), idTestBox));

            if self._oDb.getRowCount() == 1:
                aaoRow = self._oDb.fetchOne();
                if not fNeedNow:
                    oEntry = TestBoxDataEx().initFromDbRowEx(aaoRow, self._oDb);
                else:
                    oEntry = TestBoxDataEx().initFromDbRow(aaoRow);
                    oEntry.initFromDbRowEx(aaoRow, self._oDb, tsNow = db.dbTimestampMinusOneTick(oEntry.tsExpire));
                self.dCache[idTestBox] = oEntry;
        return oEntry;



    #
    # The virtual test sheriff interface.
    #

    def hasTestBoxRecentlyBeenRebooted(self, idTestBox, cHoursBack = 2, tsNow = None):
        """
        Checks if the testbox has been rebooted in the specified time period.

        This does not include already pending reboots, though under some
        circumstances it may.  These being the test box entry being edited for
        other reasons.

        Returns True / False.
        """
        if tsNow is None:
            tsNow = self._oDb.getCurrentTimestamp();
        self._oDb.execute('SELECT COUNT(idTestBox)\n'
                          'FROM   TestBoxes\n'
                          'WHERE  idTestBox      = %s\n'
                          '   AND tsExpire       < %s\n'
                          '   AND tsExpire      >= %s - interval \'%s hours\'\n'
                          '   AND enmPendingCmd IN (%s, %s)\n'
                          , ( idTestBox, tsNow, tsNow, cHoursBack,
                              TestBoxData.ksTestBoxCmd_Reboot, TestBoxData.ksTestBoxCmd_UpgradeAndReboot, ));
        return self._oDb.fetchOne()[0] > 0;


    def rebootTestBox(self, idTestBox, uidAuthor, sComment, sOldCommand = TestBoxData.ksTestBoxCmd_None, fCommit = False):
        """
        Issues a reboot command for the given test box.
        Return True on succes, False on in-flight collision.
        May raise DB exception on other trouble.
        """
        try:
            self.setCommand(idTestBox, sOldCommand, TestBoxData.ksTestBoxCmd_Reboot,
                            uidAuthor = uidAuthor, fCommit = fCommit, sComment = sComment);
        except TMInFligthCollision:
            return False;
        return True;


    def disableTestBox(self, idTestBox, uidAuthor, sComment, fCommit = False):
        """
        Disables the given test box.

        Raises exception on trouble, without rollback.
        """
        oTestBox = TestBoxData().initFromDbWithId(self._oDb, idTestBox);
        if oTestBox.fEnabled:
            oTestBox.fEnabled = False;
            if sComment is not None:
                oTestBox.sComment = sComment;
            self.editEntry(oTestBox, uidAuthor = uidAuthor, fCommit = fCommit);
        return None;


#
# Unit testing.
#

# pylint: disable=missing-docstring
class TestBoxDataTestCase(ModelDataBaseTestCase):
    def setUp(self):
        self.aoSamples = [TestBoxData(),];

if __name__ == '__main__':
    unittest.main();
    # not reached.

