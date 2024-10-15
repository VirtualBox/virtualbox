# -*- coding: utf-8 -*-
# $Id: schedgroup.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager - Scheduling Group.
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
import unittest;

# Validation Kit imports.
from testmanager.core.base          import ModelDataBase, ModelDataBaseTestCase, ModelLogicBase, TMExceptionBase, \
                                           TMRowInUse, TMInvalidData, TMRowAlreadyExists, TMRowNotFound, \
                                           ChangeLogEntry, AttributeChangeEntry, AttributeChangeEntryPre;
from testmanager.core.buildsource   import BuildSourceData;
from testmanager.core               import db;
from testmanager.core.testcase      import TestCaseData;
from testmanager.core.testcaseargs  import TestCaseArgsData;
from testmanager.core.testbox       import TestBoxLogic, TestBoxDataForSchedGroup;
from testmanager.core.testgroup     import TestGroupData;
from testmanager.core.useraccount   import UserAccountLogic;



class SchedGroupMemberData(ModelDataBase):
    """
    SchedGroupMember Data.
    """

    ksIdAttr = 'idSchedGroup';

    ksParam_idSchedGroup        = 'SchedGroupMember_idSchedGroup';
    ksParam_idTestGroup         = 'SchedGroupMember_idTestGroup';
    ksParam_tsEffective         = 'SchedGroupMember_tsEffective';
    ksParam_tsExpire            = 'SchedGroupMember_tsExpire';
    ksParam_uidAuthor           = 'SchedGroupMember_uidAuthor';
    ksParam_iSchedPriority      = 'SchedGroupMember_iSchedPriority';
    ksParam_bmHourlySchedule    = 'SchedGroupMember_bmHourlySchedule';
    ksParam_idTestGroupPreReq   = 'SchedGroupMember_idTestGroupPreReq';

    kasAllowNullAttributes      = [ 'idSchedGroup', 'idTestGroup', 'tsEffective', 'tsExpire',
                                    'uidAuthor', 'bmHourlySchedule', 'idTestGroupPreReq' ];
    kiMin_iSchedPriority        = 0;
    kiMax_iSchedPriority        = 32;

    kcDbColumns                 = 8

    def __init__(self):
        ModelDataBase.__init__(self);

        #
        # Initialize with defaults.
        # See the database for explanations of each of these fields.
        #
        self.idSchedGroup        = None;
        self.idTestGroup         = None;
        self.tsEffective         = None;
        self.tsExpire            = None;
        self.uidAuthor           = None;
        self.iSchedPriority      = 16;
        self.bmHourlySchedule    = None;
        self.idTestGroupPreReq   = None;

    def initFromDbRow(self, aoRow):
        """
        Re-initializes the data with a row from a SELECT * FROM SchedGroupMembers.

        Returns self. Raises exception if the row is None or otherwise invalid.
        """

        if aoRow is None:
            raise TMRowNotFound('SchedGroupMember not found.');

        self.idSchedGroup        = aoRow[0];
        self.idTestGroup         = aoRow[1];
        self.tsEffective         = aoRow[2];
        self.tsExpire            = aoRow[3];
        self.uidAuthor           = aoRow[4];
        self.iSchedPriority      = aoRow[5];
        self.bmHourlySchedule    = aoRow[6]; ## @todo figure out how bitmaps are returned...
        self.idTestGroupPreReq   = aoRow[7];
        return self;


class SchedGroupMemberDataEx(SchedGroupMemberData):
    """
    Extended SchedGroupMember data class.
    This adds the testgroups.
    """

    def __init__(self):
        SchedGroupMemberData.__init__(self);
        self.oTestGroup = None;

    def initFromDbRow(self, aoRow):
        """
        Re-initializes the data with a row from a query like this:

            SELECT  SchedGroupMembers.*, TestGroups.*
            FROM    SchedGroupMembers
            JOIN    TestGroups
                ON  (SchedGroupMembers.idTestGroup = TestGroups.idTestGroup);

        Returns self. Raises exception if the row is None or otherwise invalid.
        """
        SchedGroupMemberData.initFromDbRow(self, aoRow);
        self.oTestGroup = TestGroupData().initFromDbRow(aoRow[SchedGroupMemberData.kcDbColumns:]);
        return self;

    def getDataAttributes(self):
        asAttributes = SchedGroupMemberData.getDataAttributes(self);
        asAttributes.remove('oTestGroup');
        return asAttributes;

    def _validateAndConvertWorker(self, asAllowNullAttributes, oDb, enmValidateFor = ModelDataBase.ksValidateFor_Other):
        dErrors = SchedGroupMemberData._validateAndConvertWorker(self, asAllowNullAttributes, oDb, enmValidateFor);
        if self.ksParam_idTestGroup not in dErrors:
            self.oTestGroup = TestGroupData();
            try:
                self.oTestGroup.initFromDbWithId(oDb, self.idTestGroup);
            except Exception as oXcpt:
                self.oTestGroup = TestGroupData()
                dErrors[self.ksParam_idTestGroup] = str(oXcpt);
        return dErrors;




class SchedGroupData(ModelDataBase):
    """
    SchedGroup Data.
    """

    ## @name TestBoxState_T
    # @{
    ksScheduler_BestEffortContinuousIntegration = 'bestEffortContinousItegration'; # sic*2
    ksScheduler_Reserved                        = 'reserved';
    ## @}


    ksIdAttr = 'idSchedGroup';

    ksParam_idSchedGroup        = 'SchedGroup_idSchedGroup';
    ksParam_tsEffective         = 'SchedGroup_tsEffective';
    ksParam_tsExpire            = 'SchedGroup_tsExpire';
    ksParam_uidAuthor           = 'SchedGroup_uidAuthor';
    ksParam_sName               = 'SchedGroup_sName';
    ksParam_sDescription        = 'SchedGroup_sDescription';
    ksParam_fEnabled            = 'SchedGroup_fEnabled';
    ksParam_enmScheduler        = 'SchedGroup_enmScheduler';
    ksParam_idBuildSrc          = 'SchedGroup_idBuildSrc';
    ksParam_idBuildSrcTestSuite = 'SchedGroup_idBuildSrcTestSuite';
    ksParam_sComment            = 'SchedGroup_sComment';

    kasAllowNullAttributes      = ['idSchedGroup', 'tsEffective', 'tsExpire', 'uidAuthor', 'sDescription',
                                   'idBuildSrc', 'idBuildSrcTestSuite', 'sComment' ];
    kasValidValues_enmScheduler = [ ksScheduler_BestEffortContinuousIntegration, ];

    kcDbColumns                 = 11;

    # Scheduler types
    kasSchedulerDesc            = \
    [
        ( ksScheduler_BestEffortContinuousIntegration,  'Best-Effort-Continuous-Integration (BECI) scheduler.', ''),
    ]

    def __init__(self):
        ModelDataBase.__init__(self);

        #
        # Initialize with defaults.
        # See the database for explanations of each of these fields.
        #
        self.idSchedGroup            = None;
        self.tsEffective             = None;
        self.tsExpire                = None;
        self.uidAuthor               = None;
        self.sName                   = None;
        self.sDescription            = None;
        self.fEnabled                = None;
        self.enmScheduler            = SchedGroupData.ksScheduler_BestEffortContinuousIntegration;
        self.idBuildSrc              = None;
        self.idBuildSrcTestSuite     = None;
        self.sComment                = None;

    def initFromDbRow(self, aoRow):
        """
        Re-initializes the data with a row from a SELECT * FROM SchedGroups.

        Returns self. Raises exception if the row is None or otherwise invalid.
        """

        if aoRow is None:
            raise TMRowNotFound('SchedGroup not found.');

        self.idSchedGroup            = aoRow[0];
        self.tsEffective             = aoRow[1];
        self.tsExpire                = aoRow[2];
        self.uidAuthor               = aoRow[3];
        self.sName                   = aoRow[4];
        self.sDescription            = aoRow[5];
        self.fEnabled                = aoRow[6];
        self.enmScheduler            = aoRow[7];
        self.idBuildSrc              = aoRow[8];
        self.idBuildSrcTestSuite     = aoRow[9];
        self.sComment                = aoRow[10];
        return self;

    def initFromDbWithId(self, oDb, idSchedGroup, tsNow = None, sPeriodBack = None):
        """
        Initialize the object from the database.
        """
        oDb.execute(self.formatSimpleNowAndPeriodQuery(oDb,
                                                       'SELECT *\n'
                                                       'FROM   SchedGroups\n'
                                                       'WHERE  idSchedGroup = %s\n'
                                                       , ( idSchedGroup,), tsNow, sPeriodBack));
        aoRow = oDb.fetchOne()
        if aoRow is None:
            raise TMRowNotFound('idSchedGroup=%s not found (tsNow=%s, sPeriodBack=%s)' % (idSchedGroup, tsNow, sPeriodBack));
        return self.initFromDbRow(aoRow);


class SchedGroupDataEx(SchedGroupData):
    """
    Extended scheduling group data.

    Note! Similar to TestGroupDataEx.
    """

    ksParam_aoMembers    = 'SchedGroup_aoMembers';
    ksParam_aoTestBoxes  = 'SchedGroup_aoTestboxes';
    kasAltArrayNull      = [ 'aoMembers', 'aoTestboxes' ];

    ## Helper parameter containing the comma separated list with the IDs of
    #  potential members found in the parameters.
    ksParam_aidTestGroups = 'TestGroupDataEx_aidTestGroups';
    ## Ditto for testbox meembers.
    ksParam_aidTestBoxes  = 'TestGroupDataEx_aidTestBoxes';


    def __init__(self):
        SchedGroupData.__init__(self);
        self.aoMembers              = []    # type: list[SchedGroupMemberDataEx]
        self.aoTestBoxes            = []    # type: list[TestBoxDataForSchedGroup]

        # The two build sources for the sake of convenience.
        self.oBuildSrc              = None  # type: BuildSourceData
        self.oBuildSrcValidationKit = None  # type: BuildSourceData

    def _initExtraMembersFromDb(self, oDb, tsNow = None, sPeriodBack = None):
        """
        Worker shared by the initFromDb* methods.
        Returns self.  Raises exception if no row or database error.
        """
        #
        # Clear all members upfront so the object has some kind of consistency
        # if anything below raises exceptions.
        #
        self.oBuildSrc              = None;
        self.oBuildSrcValidationKit = None;
        self.aoTestBoxes            = [];
        self.aoMembers              = [];

        #
        # Build source.
        #
        if self.idBuildSrc:
            self.oBuildSrc = BuildSourceData().initFromDbWithId(oDb, self.idBuildSrc, tsNow, sPeriodBack);

        if self.idBuildSrcTestSuite:
            self.oBuildSrcValidationKit = BuildSourceData().initFromDbWithId(oDb, self.idBuildSrcTestSuite,
                                                                             tsNow, sPeriodBack);

        #
        # Test Boxes.
        #
        self.aoTestBoxes = TestBoxLogic(oDb).fetchForSchedGroup(self.idSchedGroup, tsNow);

        #
        # Test groups.
        # The fetchForChangeLog method makes ASSUMPTIONS about sorting!
        #
        oDb.execute('SELECT SchedGroupMembers.*, TestGroups.*\n'
                    'FROM   SchedGroupMembers\n'
                    'LEFT OUTER JOIN TestGroups ON (SchedGroupMembers.idTestGroup = TestGroups.idTestGroup)\n'
                    'WHERE  SchedGroupMembers.idSchedGroup = %s\n'
                    + self.formatSimpleNowAndPeriod(oDb, tsNow, sPeriodBack, sTablePrefix = 'SchedGroupMembers.')
                    + self.formatSimpleNowAndPeriod(oDb, tsNow, sPeriodBack, sTablePrefix = 'TestGroups.') +
                    'ORDER BY SchedGroupMembers.idTestGroupPreReq ASC NULLS FIRST,\n'
                    '         TestGroups.sName,\n'
                    '         SchedGroupMembers.idTestGroup\n'
                    , (self.idSchedGroup,));
        for aoRow in oDb.fetchAll():
            self.aoMembers.append(SchedGroupMemberDataEx().initFromDbRow(aoRow));
        return self;

    def initFromDbRowEx(self, aoRow, oDb, tsNow = None):
        """
        Reinitialize from a SELECT * FROM SchedGroups row.  Will query the
        necessary additional data from oDb using tsNow.
        Returns self.  Raises exception if no row or database error.
        """
        SchedGroupData.initFromDbRow(self, aoRow);
        return self._initExtraMembersFromDb(oDb, tsNow);

    def initFromDbWithId(self, oDb, idSchedGroup, tsNow = None, sPeriodBack = None):
        """
        Initialize the object from the database.
        """
        SchedGroupData.initFromDbWithId(self, oDb, idSchedGroup, tsNow, sPeriodBack);
        return self._initExtraMembersFromDb(oDb, tsNow, sPeriodBack);

    def getDataAttributes(self):
        asAttributes = SchedGroupData.getDataAttributes(self);
        asAttributes.remove('oBuildSrc');
        asAttributes.remove('oBuildSrcValidationKit');
        return asAttributes;

    def getAttributeParamNullValues(self, sAttr):
        if sAttr not in [ 'aoMembers', 'aoTestBoxes' ]:
            return SchedGroupData.getAttributeParamNullValues(self, sAttr);
        return ['', [], None];

    def convertParamToAttribute(self, sAttr, sParam, oValue, oDisp, fStrict):
        aoNewValue  = [];
        if sAttr == 'aoMembers':
            aidSelected = oDisp.getListOfIntParams(sParam, iMin = 1, iMax = 0x7ffffffe, aiDefaults = [])
            sIds        = oDisp.getStringParam(self.ksParam_aidTestGroups, sDefault = '');
            for idTestGroup in sIds.split(','):
                try:    idTestGroup = int(idTestGroup);
                except: pass;
                oDispWrapper = self.DispWrapper(oDisp, '%s[%s][%%s]' % (SchedGroupDataEx.ksParam_aoMembers, idTestGroup,))
                oMember = SchedGroupMemberDataEx().initFromParams(oDispWrapper, fStrict = False);
                if idTestGroup in aidSelected:
                    oMember.idTestGroup = idTestGroup;
                    aoNewValue.append(oMember);
        elif sAttr == 'aoTestBoxes':
            aidSelected = oDisp.getListOfIntParams(sParam, iMin = 1, iMax = 0x7ffffffe, aiDefaults = [])
            sIds        = oDisp.getStringParam(self.ksParam_aidTestBoxes, sDefault = '');
            for idTestBox in sIds.split(','):
                try:    idTestBox = int(idTestBox);
                except: pass;
                oDispWrapper = self.DispWrapper(oDisp, '%s[%s][%%s]' % (SchedGroupDataEx.ksParam_aoTestBoxes, idTestBox,))
                oBoxInGrp = TestBoxDataForSchedGroup().initFromParams(oDispWrapper, fStrict = False);
                if idTestBox in aidSelected:
                    oBoxInGrp.idTestBox = idTestBox;
                    aoNewValue.append(oBoxInGrp);
        else:
            return SchedGroupData.convertParamToAttribute(self, sAttr, sParam, oValue, oDisp, fStrict);
        return aoNewValue;

    def _validateAndConvertAttribute(self, sAttr, sParam, oValue, aoNilValues, fAllowNull, oDb):
        if sAttr not in [ 'aoMembers', 'aoTestBoxes' ]:
            return SchedGroupData._validateAndConvertAttribute(self, sAttr, sParam, oValue, aoNilValues, fAllowNull, oDb);

        if oValue in aoNilValues:
            return ([], None);

        asErrors     = [];
        aoNewMembers = [];
        if sAttr == 'aoMembers':
            asAllowNulls = ['bmHourlySchedule', 'idTestGroupPreReq', 'tsEffective', 'tsExpire', 'uidAuthor', ];
            if self.idSchedGroup in [None, '-1', -1]:
                asAllowNulls.append('idSchedGroup'); # Probably new group, so allow null scheduling group.

            for oOldMember in oValue:
                oNewMember = SchedGroupMemberDataEx().initFromOther(oOldMember);
                aoNewMembers.append(oNewMember);

                dErrors = oNewMember.validateAndConvertEx(asAllowNulls, oDb, ModelDataBase.ksValidateFor_Other);
                if dErrors:
                    asErrors.append(str(dErrors));

            if not asErrors:
                for i, idTestGroup in enumerate(aoNewMembers):
                    for j in range(i + 1, len(aoNewMembers)):
                        if aoNewMembers[j].idTestGroup == idTestGroup:
                            asErrors.append('Duplicate test group #%d!' % (idTestGroup, ));
                            break;
        else:
            asAllowNulls = list(TestBoxDataForSchedGroup.kasAllowNullAttributes);
            if self.idSchedGroup in [None, '-1', -1]:
                asAllowNulls.append('idSchedGroup'); # Probably new group, so allow null scheduling group.

            for oOldMember in oValue:
                oNewMember = TestBoxDataForSchedGroup().initFromOther(oOldMember);
                aoNewMembers.append(oNewMember);

                dErrors = oNewMember.validateAndConvertEx(asAllowNulls, oDb, ModelDataBase.ksValidateFor_Other);
                if dErrors:
                    asErrors.append(str(dErrors));

            if not asErrors:
                for i, idTestBox in enumerate(aoNewMembers):
                    for j in range(i + 1, len(aoNewMembers)):
                        if aoNewMembers[j].idTestBox == idTestBox:
                            asErrors.append('Duplicate test box #%d!' % (idTestBox, ));
                            break;

        return (aoNewMembers, None if not asErrors else '<br>\n'.join(asErrors));

    def _validateAndConvertWorker(self, asAllowNullAttributes, oDb, enmValidateFor = ModelDataBase.ksValidateFor_Other):
        dErrors = SchedGroupData._validateAndConvertWorker(self, asAllowNullAttributes, oDb, enmValidateFor);

        #
        # Fetch the extended build source bits.
        #
        if self.ksParam_idBuildSrc not in dErrors:
            if   self.idBuildSrc in self.getAttributeParamNullValues('idBuildSrc') \
              or self.idBuildSrc is None:
                self.oBuildSrc = None;
            else:
                try:
                    self.oBuildSrc = BuildSourceData().initFromDbWithId(oDb, self.idBuildSrc);
                except Exception as oXcpt:
                    self.oBuildSrc = BuildSourceData();
                    dErrors[self.ksParam_idBuildSrc] = str(oXcpt);

        if self.ksParam_idBuildSrcTestSuite not in dErrors:
            if   self.idBuildSrcTestSuite in self.getAttributeParamNullValues('idBuildSrcTestSuite') \
              or self.idBuildSrcTestSuite is None:
                self.oBuildSrcValidationKit = None;
            else:
                try:
                    self.oBuildSrcValidationKit = BuildSourceData().initFromDbWithId(oDb, self.idBuildSrcTestSuite);
                except Exception as oXcpt:
                    self.oBuildSrcValidationKit = BuildSourceData();
                    dErrors[self.ksParam_idBuildSrcTestSuite] = str(oXcpt);

        return dErrors;



class SchedGroupLogic(ModelLogicBase): # pylint: disable=too-few-public-methods
    """
    SchedGroup logic.
    """

    def __init__(self, oDb):
        ModelLogicBase.__init__(self, oDb);
        self.dCache = None;

    #
    # Standard methods.
    #

    def fetchForListing(self, iStart, cMaxRows, tsNow, aiSortColumns = None):
        """
        Fetches build sources.

        Returns an array (list) of BuildSourceData items, empty list if none.
        Raises exception on error.
        """
        _ = aiSortColumns;

        if tsNow is None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     SchedGroups\n'
                              'WHERE    tsExpire = \'infinity\'::TIMESTAMP\n'
                              'ORDER BY fEnabled DESC, sName DESC\n'
                              'LIMIT %s OFFSET %s\n'
                              , (cMaxRows, iStart,));
        else:
            self._oDb.execute('SELECT   *\n'
                              'FROM     SchedGroups\n'
                              'WHERE    tsExpire     > %s\n'
                              '     AND tsEffective <= %s\n'
                              'ORDER BY fEnabled DESC, sName DESC\n'
                              'LIMIT %s OFFSET %s\n'
                              , (tsNow, tsNow, cMaxRows, iStart,));

        aoRet = [];
        for aoRow in self._oDb.fetchAll():
            aoRet.append(SchedGroupDataEx().initFromDbRowEx(aoRow, self._oDb, tsNow));
        return aoRet;

    def fetchForChangeLog(self, idSchedGroup, iStart, cMaxRows, tsNow): # pylint: disable=too-many-locals,too-many-statements
        """
        Fetches change log entries for a scheduling group.

        Returns an array of ChangeLogEntry instance and an indicator whether
        there are more entries.
        Raises exception on error.
        """

        ## @todo calc changes to scheduler group!

        if tsNow is None:
            tsNow = self._oDb.getCurrentTimestamp();

        #
        # First gather the change log timeline using the effective dates.
        # (ASSUMES that we'll always have a separate delete entry, rather
        # than just setting tsExpire.)
        #
        self._oDb.execute('''
(
SELECT      tsEffective,
            uidAuthor
FROM        SchedGroups
WHERE       idSchedGroup = %s
        AND tsEffective <= %s
ORDER BY    tsEffective DESC
) UNION (
SELECT      CASE WHEN tsEffective + %s::INTERVAL = tsExpire THEN tsExpire ELSE tsEffective END,
            uidAuthor
FROM        SchedGroupMembers
WHERE       idSchedGroup = %s
        AND tsEffective <= %s
ORDER BY    tsEffective DESC
) UNION (
SELECT      CASE WHEN tsEffective + %s::INTERVAL = tsExpire THEN tsExpire ELSE tsEffective END,
            uidAuthor
FROM        TestBoxesInSchedGroups
WHERE       idSchedGroup = %s
        AND tsEffective <= %s
ORDER BY    tsEffective DESC
)
ORDER BY    tsEffective DESC
LIMIT %s OFFSET %s
''', (idSchedGroup, tsNow,
      db.dbOneTickIntervalString(), idSchedGroup, tsNow,
      db.dbOneTickIntervalString(), idSchedGroup, tsNow,
      cMaxRows + 1, iStart, ));

        aoEntries = [] # type: list[ChangeLogEntry]
        tsPrevious = tsNow;
        for aoDbRow in self._oDb.fetchAll():
            (tsEffective, uidAuthor) = aoDbRow;
            aoEntries.append(ChangeLogEntry(uidAuthor, None, tsEffective, tsPrevious, None, None, []));
            tsPrevious = db.dbTimestampPlusOneTick(tsEffective);

        if True: # pylint: disable=using-constant-test
            #
            # Fetch data for each for each change log entry point.
            #
            # We add one tick to the timestamp here to skip past delete records
            # that only there to record the user doing the deletion.
            #
            for iEntry, oEntry in enumerate(aoEntries):
                oEntry.oNewRaw = SchedGroupDataEx().initFromDbWithId(self._oDb, idSchedGroup, oEntry.tsEffective);
                if iEntry > 0:
                    aoEntries[iEntry - 1].oOldRaw = oEntry.oNewRaw;

            # Chop off the +1 entry, if any.
            fMore = len(aoEntries) > cMaxRows;
            if fMore:
                aoEntries = aoEntries[:-1];

            # Figure out the changes.
            for oEntry in aoEntries:
                oOld = oEntry.oOldRaw;
                if not oOld:
                    break;
                oNew      = oEntry.oNewRaw;
                aoChanges = oEntry.aoChanges;
                for sAttr in oNew.getDataAttributes():
                    if sAttr in [ 'tsEffective', 'tsExpire', 'uidAuthor', ]:
                        continue;
                    oOldAttr = getattr(oOld, sAttr);
                    oNewAttr = getattr(oNew, sAttr);
                    if oOldAttr == oNewAttr:
                        continue;
                    if sAttr in [ 'aoMembers', 'aoTestBoxes', ]:
                        iNew = 0;
                        iOld = 0;
                        asNewAttr = [];
                        asOldAttr = [];
                        if sAttr == 'aoMembers':
                            # ASSUMES aoMembers is sorted by idTestGroupPreReq (nulls first), oTestGroup.sName, idTestGroup!
                            while iNew < len(oNewAttr) and iOld < len(oOldAttr):
                                if oNewAttr[iNew].idTestGroup == oOldAttr[iOld].idTestGroup:
                                    if oNewAttr[iNew].idTestGroupPreReq != oOldAttr[iOld].idTestGroupPreReq:
                                        if oNewAttr[iNew].idTestGroupPreReq is None:
                                            asOldAttr.append('Dropped test group #%s (%s) dependency on #%s'
                                                             % (oNewAttr[iNew].idTestGroup, oNewAttr[iNew].oTestGroup.sName,
                                                                oOldAttr[iOld].idTestGroupPreReq));
                                        elif oOldAttr[iOld].idTestGroupPreReq is None:
                                            asNewAttr.append('Added test group #%s (%s) dependency on #%s'
                                                             % (oNewAttr[iNew].idTestGroup, oNewAttr[iNew].oTestGroup.sName,
                                                                oNewAttr[iOld].idTestGroupPreReq));
                                        else:
                                            asNewAttr.append('Test group #%s (%s) dependency on #%s'
                                                             % (oNewAttr[iNew].idTestGroup, oNewAttr[iNew].oTestGroup.sName,
                                                                oNewAttr[iNew].idTestGroupPreReq));
                                            asOldAttr.append('Test group #%s (%s) dependency on #%s'
                                                             % (oNewAttr[iNew].idTestGroup, oNewAttr[iNew].oTestGroup.sName,
                                                                oOldAttr[iOld].idTestGroupPreReq));
                                    if oNewAttr[iNew].iSchedPriority != oOldAttr[iOld].iSchedPriority:
                                        asNewAttr.append('Test group #%s (%s) priority %s'
                                                         % (oNewAttr[iNew].idTestGroup, oNewAttr[iNew].oTestGroup.sName,
                                                            oNewAttr[iNew].iSchedPriority));
                                        asOldAttr.append('Test group #%s (%s) priority %s'
                                                         % (oNewAttr[iNew].idTestGroup, oNewAttr[iNew].oTestGroup.sName,
                                                            oOldAttr[iOld].iSchedPriority));
                                    iNew += 1;
                                    iOld += 1;
                                elif oNewAttr[iNew].oTestGroup.sName      <  oOldAttr[iOld].oTestGroup.sName \
                                  or (    oNewAttr[iNew].oTestGroup.sName == oOldAttr[iOld].oTestGroup.sName
                                      and oNewAttr[iNew].idTestGroup      <  oOldAttr[iOld].idTestGroup):
                                    asNewAttr.append('New test group #%s - %s'
                                                     % (oNewAttr[iNew].idTestGroup, oNewAttr[iNew].oTestGroup.sName));
                                    iNew += 1;
                                else:
                                    asOldAttr.append('Removed test group #%s - %s'
                                                     % (oOldAttr[iOld].idTestGroup, oOldAttr[iOld].oTestGroup.sName));
                                    iOld += 1;
                            while iNew < len(oNewAttr):
                                asNewAttr.append('New test group #%s - %s'
                                                 % (oNewAttr[iNew].idTestGroup, oNewAttr[iNew].oTestGroup.sName));
                                iNew += 1;
                            while iOld < len(oOldAttr):
                                asOldAttr.append('Removed test group #%s - %s'
                                                 % (oOldAttr[iOld].idTestGroup, oOldAttr[iOld].oTestGroup.sName));
                                iOld += 1;
                        else:
                            dNewIds    = { oBoxInGrp.idTestBox: oBoxInGrp for oBoxInGrp in oNewAttr };
                            dOldIds    = { oBoxInGrp.idTestBox: oBoxInGrp for oBoxInGrp in oOldAttr };
                            hCommonIds = set(dNewIds.keys()) & set(dOldIds.keys());
                            for idTestBox in hCommonIds:
                                oNewBoxInGrp = dNewIds[idTestBox];
                                oOldBoxInGrp = dOldIds[idTestBox];
                                if oNewBoxInGrp.iSchedPriority != oOldBoxInGrp.iSchedPriority:
                                    asNewAttr.append('Test box \'%s\' (#%s) priority %s'
                                                     % (getattr(oNewBoxInGrp.oTestBox, 'sName', '[Partial DB]'),
                                                        oNewBoxInGrp.idTestBox, oNewBoxInGrp.iSchedPriority));
                                    asOldAttr.append('Test box \'%s\' (#%s) priority %s'
                                                     % (getattr(oOldBoxInGrp.oTestBox, 'sName', '[Partial DB]'),
                                                        oOldBoxInGrp.idTestBox, oOldBoxInGrp.iSchedPriority));
                            asNewAttr = sorted(asNewAttr);
                            asOldAttr = sorted(asOldAttr);
                            for idTestBox in set(dNewIds.keys()) - hCommonIds:
                                oNewBoxInGrp = dNewIds[idTestBox];
                                asNewAttr.append('New test box \'%s\' (#%s) priority %s'
                                                 % (getattr(oNewBoxInGrp.oTestBox, 'sName', '[Partial DB]'),
                                                    oNewBoxInGrp.idTestBox, oNewBoxInGrp.iSchedPriority));
                            for idTestBox in set(dOldIds.keys()) - hCommonIds:
                                oOldBoxInGrp = dOldIds[idTestBox];
                                asOldAttr.append('Removed test box \'%s\' (#%s) priority %s'
                                                 % (getattr(oOldBoxInGrp.oTestBox, 'sName', '[Partial DB]'),
                                                    oOldBoxInGrp.idTestBox, oOldBoxInGrp.iSchedPriority));

                        if asNewAttr or asOldAttr:
                            aoChanges.append(AttributeChangeEntryPre(sAttr, oNewAttr, oOldAttr,
                                                                     '\n'.join(asNewAttr), '\n'.join(asOldAttr)));
                    else:
                        aoChanges.append(AttributeChangeEntry(sAttr, oNewAttr, oOldAttr, str(oNewAttr), str(oOldAttr)));

        else:
            ##
            ## @todo Incomplete: A more complicate apporach, probably faster though.
            ##
            def findEntry(tsEffective, iPrev = 0):
                """ Find entry with matching effective + expiration time """
                self._oDb.dprint('findEntry: iPrev=%s len(aoEntries)=%s tsEffective=%s' % (iPrev, len(aoEntries), tsEffective));
                while iPrev < len(aoEntries):
                    self._oDb.dprint('%s iPrev=%u' % (aoEntries[iPrev].tsEffective, iPrev, ));
                    if aoEntries[iPrev].tsEffective > tsEffective:
                        iPrev += 1;
                    elif aoEntries[iPrev].tsEffective == tsEffective:
                        self._oDb.dprint('hit %u' % (iPrev,));
                        return iPrev;
                    else:
                        break;
                self._oDb.dprint('%s not found!' % (tsEffective,));
                return -1;

            fMore = True;

            #
            # Track scheduling group changes.  Not terribly efficient for large cMaxRows
            # values, but not in the mood for figure out if there is any way to optimize that.
            #
            self._oDb.execute('''
SELECT      *
FROM        SchedGroups
WHERE       idSchedGroup = %s
        AND tsEffective <= %s
ORDER BY    tsEffective DESC
LIMIT %s''', (idSchedGroup, aoEntries[0].tsEffective, cMaxRows + 1,));

            iEntry  = 0;
            aaoRows = self._oDb.fetchAll();
            for iRow, oRow in enumerate(aaoRows):
                oNew   = SchedGroupDataEx().initFromDbRow(oRow);
                iEntry = findEntry(oNew.tsEffective, iEntry);
                self._oDb.dprint('iRow=%s iEntry=%s' % (iRow, iEntry));
                if iEntry < 0:
                    break;
                oEntry = aoEntries[iEntry];
                aoChanges = oEntry.aoChanges;
                oEntry.oNewRaw = oNew;
                if iRow + 1 < len(aaoRows):
                    oOld = SchedGroupDataEx().initFromDbRow(aaoRows[iRow + 1]);
                    self._oDb.dprint('oOld=%s' % (oOld,));
                    for sAttr in oNew.getDataAttributes():
                        if sAttr not in [ 'tsEffective', 'tsExpire', 'uidAuthor', ]:
                            oOldAttr = getattr(oOld, sAttr);
                            oNewAttr = getattr(oNew, sAttr);
                            if oOldAttr != oNewAttr:
                                aoChanges.append(AttributeChangeEntry(sAttr, oNewAttr, oOldAttr, str(oNewAttr), str(oOldAttr)));
                else:
                    self._oDb.dprint('New');

            #
            # ...
            #

        # FInally
        UserAccountLogic(self._oDb).resolveChangeLogAuthors(aoEntries);
        return (aoEntries, fMore);


    def addEntry(self, oData, uidAuthor, fCommit = False):
        """Add Scheduling Group record"""

        #
        # Validate.
        #
        dDataErrors = oData.validateAndConvert(self._oDb, oData.ksValidateFor_Add);
        if dDataErrors:
            raise TMInvalidData('Invalid data passed to addEntry: %s' % (dDataErrors,));
        if self.exists(oData.sName):
            raise TMRowAlreadyExists('Scheduling group "%s" already exists.' % (oData.sName,));

        #
        # Add it.
        #
        self._oDb.execute('INSERT INTO SchedGroups (\n'
                          '         uidAuthor,\n'
                          '         sName,\n'
                          '         sDescription,\n'
                          '         fEnabled,\n'
                          '         enmScheduler,\n'
                          '         idBuildSrc,\n'
                          '         idBuildSrcTestSuite,\n'
                          '         sComment)\n'
                          'VALUES (%s, %s, %s, %s, %s, %s, %s, %s)\n'
                          'RETURNING idSchedGroup\n'
                          , ( uidAuthor,
                              oData.sName,
                              oData.sDescription,
                              oData.fEnabled,
                              oData.enmScheduler,
                              oData.idBuildSrc,
                              oData.idBuildSrcTestSuite,
                              oData.sComment ));
        idSchedGroup = self._oDb.fetchOne()[0];
        oData.idSchedGroup = idSchedGroup;

        for oBoxInGrp in oData.aoTestBoxes:
            oBoxInGrp.idSchedGroup = idSchedGroup;
            self._addSchedGroupTestBox(uidAuthor, oBoxInGrp);

        for oMember in oData.aoMembers:
            oMember.idSchedGroup = idSchedGroup;
            self._addSchedGroupMember(uidAuthor, oMember);

        self._oDb.maybeCommit(fCommit);
        return True;

    def editEntry(self, oData, uidAuthor, fCommit = False):
        """Edit Scheduling Group record"""

        #
        # Validate input and retrieve the old data.
        #
        dErrors = oData.validateAndConvert(self._oDb, oData.ksValidateFor_Edit);
        if dErrors:
            raise TMInvalidData('editEntry got invalid data: %s' % (dErrors,));
        self._assertUnique(oData.sName, oData.idSchedGroup);
        oOldData = SchedGroupDataEx().initFromDbWithId(self._oDb, oData.idSchedGroup);

        #
        # Make the changes.
        #
        if not oData.isEqualEx(oOldData, [ 'tsEffective', 'tsExpire', 'uidAuthor', 'aoMembers', 'aoTestBoxes',
                                           'oBuildSrc', 'oBuildSrcValidationKit', ]):
            self._historizeEntry(oData.idSchedGroup);
            self._readdEntry(uidAuthor, oData);

        # Remove groups.
        for oOld in oOldData.aoMembers:
            fRemove = True;
            for oNew in oData.aoMembers:
                if oNew.idTestGroup == oOld.idTestGroup:
                    fRemove = False;
                    break;
            if fRemove:
                self._removeSchedGroupMember(uidAuthor, oOld);

        # Add / modify groups.
        for oMember in oData.aoMembers:
            oOldMember = None;
            for oOld in oOldData.aoMembers:
                if oOld.idTestGroup == oMember.idTestGroup:
                    oOldMember = oOld;
                    break;

            oMember.idSchedGroup = oData.idSchedGroup;
            if oOldMember is None:
                self._addSchedGroupMember(uidAuthor, oMember);
            elif not oMember.isEqualEx(oOldMember, ['tsEffective', 'tsExpire', 'uidAuthor', 'oTestGroup']):
                self._historizeSchedGroupMember(oMember);
                self._addSchedGroupMember(uidAuthor, oMember);

        # Remove testboxes.
        for oOld in oOldData.aoTestBoxes:
            fRemove = True;
            for oNew in oData.aoTestBoxes:
                if oNew.idTestBox == oOld.idTestBox:
                    fRemove = False;
                    break;
            if fRemove:
                self._removeSchedGroupTestBox(uidAuthor, oOld);

        # Add / modify testboxes.
        for oBoxInGrp in oData.aoTestBoxes:
            oOldBoxInGrp = None;
            for oOld in oOldData.aoTestBoxes:
                if oOld.idTestBox == oBoxInGrp.idTestBox:
                    oOldBoxInGrp = oOld;
                    break;

            oBoxInGrp.idSchedGroup = oData.idSchedGroup;
            if oOldBoxInGrp is None:
                self._addSchedGroupTestBox(uidAuthor, oBoxInGrp);
            elif not oBoxInGrp.isEqualEx(oOldBoxInGrp, ['tsEffective', 'tsExpire', 'uidAuthor', 'oTestBox']):
                self._historizeSchedGroupTestBox(oBoxInGrp);
                self._addSchedGroupTestBox(uidAuthor, oBoxInGrp);

        self._oDb.maybeCommit(fCommit);
        return True;

    def removeEntry(self, uidAuthor, idSchedGroup, fCascade = False, fCommit = False):
        """
        Deletes a scheduling group.
        """
        _ = fCascade;

        #
        # Input validation and retrival of current data.
        #
        if idSchedGroup == 1:
            raise TMRowInUse('Cannot remove the default scheduling group (id 1).');
        oData = SchedGroupDataEx().initFromDbWithId(self._oDb, idSchedGroup);

        #
        # Remove the test box member records.
        #
        for oBoxInGrp in oData.aoTestBoxes:
            self._removeSchedGroupTestBox(uidAuthor, oBoxInGrp);
        self._oDb.execute('UPDATE   TestBoxesInSchedGroups\n'
                          'SET      tsExpire     = CURRENT_TIMESTAMP\n'
                          'WHERE    idSchedGroup = %s\n'
                          '     AND tsExpire     = \'infinity\'::TIMESTAMP\n'
                          , (idSchedGroup,));

        #
        # Remove the test group member records.
        #
        for oMember in oData.aoMembers:
            self._removeSchedGroupMember(uidAuthor, oMember);
        self._oDb.execute('UPDATE   SchedGroupMembers\n'
                          'SET      tsExpire     = CURRENT_TIMESTAMP\n'
                          'WHERE    idSchedGroup = %s\n'
                          '     AND tsExpire     = \'infinity\'::TIMESTAMP\n'
                          , (idSchedGroup,));

        #
        # Now the SchedGroups entry.
        #
        (tsCur, tsCurMinusOne) = self._oDb.getCurrentTimestamps();
        if oData.tsEffective not in (tsCur, tsCurMinusOne):
            self._historizeEntry(idSchedGroup, tsCurMinusOne);
            self._readdEntry(uidAuthor, oData, tsCurMinusOne);
            self._historizeEntry(idSchedGroup);
        self._oDb.execute('UPDATE   SchedGroups\n'
                          'SET      tsExpire     = CURRENT_TIMESTAMP\n'
                          'WHERE    idSchedGroup = %s\n'
                          '     AND tsExpire     = \'infinity\'::TIMESTAMP\n'
                          , (idSchedGroup,))

        self._oDb.maybeCommit(fCommit)
        return True;


    def cachedLookup(self, idSchedGroup):
        """
        Looks up the most recent SchedGroupData object for idSchedGroup
        via an object cache.

        Returns a shared SchedGroupData object.  None if not found.
        Raises exception on DB error.
        """
        if self.dCache is None:
            self.dCache = self._oDb.getCache('SchedGroup');

        oEntry = self.dCache.get(idSchedGroup, None);
        if oEntry is None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     SchedGroups\n'
                              'WHERE    idSchedGroup = %s\n'
                              '     AND tsExpire = \'infinity\'::TIMESTAMP\n'
                              , (idSchedGroup, ));
            if self._oDb.getRowCount() == 0:
                # Maybe it was deleted, try get the last entry.
                self._oDb.execute('SELECT   *\n'
                                  'FROM     SchedGroups\n'
                                  'WHERE    idSchedGroup = %s\n'
                                  'ORDER BY tsExpire DESC\n'
                                  'LIMIT 1\n'
                                  , (idSchedGroup, ));
            elif self._oDb.getRowCount() > 1:
                raise self._oDb.integrityException('%s infinity rows for %s' % (self._oDb.getRowCount(), idSchedGroup));

            if self._oDb.getRowCount() == 1:
                oEntry = SchedGroupData().initFromDbRow(self._oDb.fetchOne());
                self.dCache[idSchedGroup] = oEntry;
        return oEntry;


    #
    # Other methods.
    #

    def fetchOrderedByName(self, tsNow = None):
        """
        Return list of objects of type SchedGroups ordered by name.
        May raise exception on database error.
        """
        if tsNow is None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     SchedGroups\n'
                              'WHERE    tsExpire = \'infinity\'::TIMESTAMP\n'
                              'ORDER BY sName ASC\n');
        else:
            self._oDb.execute('SELECT   *\n'
                              'FROM     SchedGroups\n'
                              'WHERE    tsExpire     > %s\n'
                              '   AND   tsEffective <= %s\n'
                              'ORDER BY sName ASC\n'
                              , (tsNow, tsNow,));
        aoRet = []
        for _ in range(self._oDb.getRowCount()):
            aoRet.append(SchedGroupData().initFromDbRow(self._oDb.fetchOne()));
        return aoRet;


    def getAll(self, tsEffective = None):
        """
        Gets the list of all scheduling groups.
        Returns an array of SchedGroupData instances.
        """
        if tsEffective is None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     SchedGroups\n'
                              'WHERE    tsExpire = \'infinity\'::TIMESTAMP\n');
        else:
            self._oDb.execute('SELECT   *\n'
                              'FROM     SchedGroups\n'
                              'WHERE    tsExpire     > %s\n'
                              '     AND tsEffective <= %s\n'
                              , (tsEffective, tsEffective));
        aoRet = [];
        for aoRow in self._oDb.fetchAll():
            aoRet.append(SchedGroupData().initFromDbRow(aoRow));
        return aoRet;

    def getSchedGroupsForCombo(self, tsEffective = None):
        """
        Gets the list of active scheduling groups for a combo box.
        Returns an array of (value [idSchedGroup], drop-down-name [sName],
        hover-text [sDescription]) tuples.
        """
        if tsEffective is None:
            self._oDb.execute('SELECT   idSchedGroup, sName, sDescription\n'
                              'FROM     SchedGroups\n'
                              'WHERE    tsExpire = \'infinity\'::TIMESTAMP\n'
                              'ORDER BY sName');
        else:
            self._oDb.execute('SELECT   idSchedGroup, sName, sDescription\n'
                              'FROM     SchedGroups\n'
                              'WHERE    tsExpire     > %s\n'
                              '     AND tsEffective <= %s\n'
                              'ORDER BY sName'
                              , (tsEffective, tsEffective));
        return self._oDb.fetchAll();


    def getMembers(self, idSchedGroup, tsEffective = None):
        """
        Gets the scheduling groups members for the given scheduling group.

        Returns an array of SchedGroupMemberDataEx instances (sorted by
        priority (descending) and idTestGroup).  May raise exception DB error.
        """

        if tsEffective is None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     SchedGroupMembers, TestGroups\n'
                              'WHERE    SchedGroupMembers.idSchedGroup = %s\n'
                              '     AND SchedGroupMembers.tsExpire     = \'infinity\'::TIMESTAMP\n'
                              '     AND TestGroups.idTestGroup         = SchedGroupMembers.idTestGroup\n'
                              '     AND TestGroups.tsExpire            = \'infinity\'::TIMESTAMP\n'
                              'ORDER BY SchedGroupMembers.iSchedPriority DESC, SchedGroupMembers.idTestGroup\n'
                              , (idSchedGroup,));
        else:
            self._oDb.execute('SELECT   *\n'
                              'FROM     SchedGroupMembers, TestGroups\n'
                              'WHERE    SchedGroupMembers.idSchedGroup = %s\n'
                              '     AND SchedGroupMembers.tsExpire     < %s\n'
                              '     AND SchedGroupMembers.tsEffective >= %s\n'
                              '     AND TestGroups.idTestGroup         = SchedGroupMembers.idTestGroup\n'
                              '     AND TestGroups.tsExpire            < %s\n'
                              '     AND TestGroups.tsEffective        >= %s\n'
                              'ORDER BY SchedGroupMembers.iSchedPriority DESC, SchedGroupMembers.idTestGroup\n'
                              , (idSchedGroup, tsEffective, tsEffective, tsEffective, tsEffective, ));
        aaoRows = self._oDb.fetchAll();
        aoRet = [];
        for aoRow in aaoRows:
            aoRet.append(SchedGroupMemberDataEx().initFromDbRow(aoRow));
        return aoRet;

    def getTestCasesForGroup(self, idSchedGroup, cMax = None):
        """
        Gets the enabled testcases w/ testgroup+priority for the given scheduling group.

        Returns an array of TestCaseData instances (ordered by group id, descending
        testcase priority, and testcase IDs) with an extra iSchedPriority member.
        May raise exception on DB error or if the result exceeds cMax.
        """

        self._oDb.execute('SELECT   TestGroupMembers.idTestGroup, TestGroupMembers.iSchedPriority, TestCases.*\n'
                          'FROM     SchedGroupMembers, TestGroups, TestGroupMembers, TestCases\n'
                          'WHERE    SchedGroupMembers.idSchedGroup = %s\n'
                          '     AND SchedGroupMembers.tsExpire     = \'infinity\'::TIMESTAMP\n'
                          '     AND TestGroups.idTestGroup         = SchedGroupMembers.idTestGroup\n'
                          '     AND TestGroups.tsExpire            = \'infinity\'::TIMESTAMP\n'
                          '     AND TestGroupMembers.idTestGroup   = TestGroups.idTestGroup\n'
                          '     AND TestGroupMembers.tsExpire      = \'infinity\'::TIMESTAMP\n'
                          '     AND TestCases.idTestCase           = TestGroupMembers.idTestCase\n'
                          '     AND TestCases.tsExpire             = \'infinity\'::TIMESTAMP\n'
                          '     AND TestCases.fEnabled             = TRUE\n'
                          'ORDER BY TestGroupMembers.idTestGroup, TestGroupMembers.iSchedPriority DESC, TestCases.idTestCase\n'
                          , (idSchedGroup,));

        if cMax is not None  and  self._oDb.getRowCount() > cMax:
            raise TMExceptionBase('Too many testcases for scheduling group %s: %s, max %s'
                                  % (idSchedGroup, cMax, self._oDb.getRowCount(),));

        aoRet = [];
        for aoRow in self._oDb.fetchAll():
            oTestCase = TestCaseData().initFromDbRow(aoRow[2:]);
            oTestCase.idTestGroup    = aoRow[0];
            oTestCase.iSchedPriority = aoRow[1];
            aoRet.append(oTestCase);
        return aoRet;

    def getTestCaseArgsForGroup(self, idSchedGroup, cMax = None):
        """
        Gets the testcase argument variation w/ testgroup+priority for the given scheduling group.

        Returns an array TestCaseArgsData instance (sorted by group and
        variation id) with an extra iSchedPriority member.
        May raise exception on DB error or if the result exceeds cMax.
        """

        self._oDb.execute('SELECT   TestGroupMembers.idTestGroup, TestGroupMembers.iSchedPriority, TestCaseArgs.*\n'
                          'FROM     SchedGroupMembers, TestGroups, TestGroupMembers, TestCaseArgs, TestCases\n'
                          'WHERE    SchedGroupMembers.idSchedGroup = %s\n'
                          '     AND SchedGroupMembers.tsExpire     = \'infinity\'::TIMESTAMP\n'
                          '     AND TestGroups.idTestGroup         = SchedGroupMembers.idTestGroup\n'
                          '     AND TestGroups.tsExpire            = \'infinity\'::TIMESTAMP\n'
                          '     AND TestGroupMembers.idTestGroup   = TestGroups.idTestGroup\n'
                          '     AND TestGroupMembers.tsExpire      = \'infinity\'::TIMESTAMP\n'
                          '     AND TestCaseArgs.idTestCase        = TestGroupMembers.idTestCase\n'
                          '     AND TestCaseArgs.tsExpire          = \'infinity\'::TIMESTAMP\n'
                          '     AND (   TestGroupMembers.aidTestCaseArgs is NULL\n'
                          '          OR TestCaseArgs.idTestCaseArgs = ANY(TestGroupMembers.aidTestCaseArgs) )\n'
                          '     AND TestCases.idTestCase           = TestCaseArgs.idTestCase\n'
                          '     AND TestCases.tsExpire             = \'infinity\'::TIMESTAMP\n'
                          '     AND TestCases.fEnabled             = TRUE\n'
                          'ORDER BY TestGroupMembers.idTestGroup, TestGroupMembers.idTestCase, TestCaseArgs.idTestCaseArgs\n'
                          , (idSchedGroup,));

        if cMax is not None  and  self._oDb.getRowCount() > cMax:
            raise TMExceptionBase('Too many argument variations for scheduling group %s: %s, max %s'
                                  % (idSchedGroup, cMax, self._oDb.getRowCount(),));

        aoRet = [];
        for aoRow in self._oDb.fetchAll():
            oVariation = TestCaseArgsData().initFromDbRow(aoRow[2:]);
            oVariation.idTestGroup    = aoRow[0];
            oVariation.iSchedPriority = aoRow[1];
            aoRet.append(oVariation);
        return aoRet;

    def exists(self, sName):
        """Checks if a group with the given name exists."""
        self._oDb.execute('SELECT idSchedGroup\n'
                          'FROM   SchedGroups\n'
                          'WHERE  tsExpire   = \'infinity\'::TIMESTAMP\n'
                          '   AND sName      = %s\n'
                          'LIMIT 1\n'
                          , (sName,));
        return self._oDb.getRowCount() > 0;

    def getById(self, idSchedGroup):
        """Get Scheduling Group data by idSchedGroup"""
        self._oDb.execute('SELECT   *\n'
                          'FROM     SchedGroups\n'
                          'WHERE    tsExpire     = \'infinity\'::timestamp\n'
                          '  AND    idSchedGroup = %s;', (idSchedGroup,))
        aRows = self._oDb.fetchAll()
        if len(aRows) not in (0, 1):
            raise self._oDb.integrityException(
                'Found more than one scheduling groups with the same credentials. Database structure is corrupted.')
        try:
            return SchedGroupData().initFromDbRow(aRows[0])
        except IndexError:
            return None


    #
    # Internal helpers.
    #

    def _assertUnique(self, sName, idSchedGroupIgnore = None):
        """
        Checks that the scheduling group name is unique.
        Raises exception if the name is already in use.
        """
        if idSchedGroupIgnore is None:
            self._oDb.execute('SELECT   idSchedGroup\n'
                              'FROM     SchedGroups\n'
                              'WHERE    tsExpire = \'infinity\'::TIMESTAMP\n'
                              '   AND   sName    = %s\n'
                              , ( sName, ) );
        else:
            self._oDb.execute('SELECT   idSchedGroup\n'
                              'FROM     SchedGroups\n'
                              'WHERE    tsExpire      = \'infinity\'::TIMESTAMP\n'
                              '   AND   sName         = %s\n'
                              '   AND   idSchedGroup <> %s\n'
                              , ( sName, idSchedGroupIgnore, ) );
        if self._oDb.getRowCount() > 0:
            raise TMRowInUse('Scheduling group name (%s) is already in use.' % (sName,));
        return True;

    def _readdEntry(self, uidAuthor, oData, tsEffective = None):
        """
        Re-adds the SchedGroups entry. Used by editEntry and removeEntry.
        """
        if tsEffective is None:
            tsEffective = self._oDb.getCurrentTimestamp();
        self._oDb.execute('INSERT INTO SchedGroups (\n'
                          '         uidAuthor,\n'
                          '         tsEffective,\n'
                          '         idSchedGroup,\n'
                          '         sName,\n'
                          '         sDescription,\n'
                          '         fEnabled,\n'
                          '         enmScheduler,\n'
                          '         idBuildSrc,\n'
                          '         idBuildSrcTestSuite,\n'
                          '         sComment )\n'
                          'VALUES ( %s, %s, %s, %s, %s, %s, %s, %s, %s, %s )\n'
                          , ( uidAuthor,
                              tsEffective,
                              oData.idSchedGroup,
                              oData.sName,
                              oData.sDescription,
                              oData.fEnabled,
                              oData.enmScheduler,
                              oData.idBuildSrc,
                              oData.idBuildSrcTestSuite,
                              oData.sComment, ));
        return True;

    def _historizeEntry(self, idSchedGroup, tsExpire = None):
        """
        Historizes the current entry for the given scheduling group.
        """
        if tsExpire is None:
            tsExpire = self._oDb.getCurrentTimestamp();
        self._oDb.execute('UPDATE SchedGroups\n'
                          'SET    tsExpire = %s\n'
                          'WHERE  idSchedGroup = %s\n'
                          '   AND tsExpire     = \'infinity\'::TIMESTAMP\n'
                          , ( tsExpire, idSchedGroup, ));
        return True;

    def _addSchedGroupMember(self, uidAuthor, oMember, tsEffective = None):
        """
        addEntry worker for adding a scheduling group member.
        """
        if tsEffective is None:
            tsEffective = self._oDb.getCurrentTimestamp();
        self._oDb.execute('INSERT INTO SchedGroupMembers(\n'
                          '         idSchedGroup,\n'
                          '         idTestGroup,\n'
                          '         tsEffective,\n'
                          '         uidAuthor,\n'
                          '         iSchedPriority,\n'
                          '         bmHourlySchedule,\n'
                          '         idTestGroupPreReq)\n'
                          'VALUES (%s, %s, %s, %s, %s, %s, %s)\n'
                          , ( oMember.idSchedGroup,
                              oMember.idTestGroup,
                              tsEffective,
                              uidAuthor,
                              oMember.iSchedPriority,
                              oMember.bmHourlySchedule,
                              oMember.idTestGroupPreReq, ));
        return True;

    def _removeSchedGroupMember(self, uidAuthor, oMember):
        """
        Removes a scheduling group member.
        """

        # Try record who removed it by adding an dummy entry that expires immediately.
        (tsCur, tsCurMinusOne) = self._oDb.getCurrentTimestamps();
        if oMember.tsEffective not in (tsCur, tsCurMinusOne):
            self._historizeSchedGroupMember(oMember, tsCurMinusOne);
            self._addSchedGroupMember(uidAuthor, oMember, tsCurMinusOne); # lazy bird.
            self._historizeSchedGroupMember(oMember);
        else:
            self._historizeSchedGroupMember(oMember);
        return True;

    def _historizeSchedGroupMember(self, oMember, tsExpire = None):
        """
        Historizes the current entry for the given scheduling group.
        """
        if tsExpire is None:
            tsExpire = self._oDb.getCurrentTimestamp();
        self._oDb.execute('UPDATE SchedGroupMembers\n'
                          'SET    tsExpire = %s\n'
                          'WHERE  idSchedGroup = %s\n'
                          '   AND idTestGroup  = %s\n'
                          '   AND tsExpire     = \'infinity\'::TIMESTAMP\n'
                          , ( tsExpire, oMember.idSchedGroup, oMember.idTestGroup, ));
        return True;

    #
    def _addSchedGroupTestBox(self, uidAuthor, oBoxInGroup, tsEffective = None):
        """
        addEntry worker for adding a test box to a scheduling group.
        """
        if tsEffective is None:
            tsEffective = self._oDb.getCurrentTimestamp();
        self._oDb.execute('INSERT INTO TestBoxesInSchedGroups(\n'
                          '         idSchedGroup,\n'
                          '         idTestBox,\n'
                          '         tsEffective,\n'
                          '         uidAuthor,\n'
                          '         iSchedPriority)\n'
                          'VALUES (%s, %s, %s, %s, %s)\n'
                          , ( oBoxInGroup.idSchedGroup,
                              oBoxInGroup.idTestBox,
                              tsEffective,
                              uidAuthor,
                              oBoxInGroup.iSchedPriority, ));
        return True;

    def _removeSchedGroupTestBox(self, uidAuthor, oBoxInGroup):
        """
        Removes a testbox from a scheduling group.
        """

        # Try record who removed it by adding an dummy entry that expires immediately.
        (tsCur, tsCurMinusOne) = self._oDb.getCurrentTimestamps();
        if oBoxInGroup.tsEffective not in (tsCur, tsCurMinusOne):
            self._historizeSchedGroupTestBox(oBoxInGroup, tsCurMinusOne);
            self._addSchedGroupTestBox(uidAuthor, oBoxInGroup, tsCurMinusOne); # lazy bird.
            self._historizeSchedGroupTestBox(oBoxInGroup);
        else:
            self._historizeSchedGroupTestBox(oBoxInGroup);
        return True;

    def _historizeSchedGroupTestBox(self, oBoxInGroup, tsExpire = None):
        """
        Historizes the current entry for the given scheduling group.
        """
        if tsExpire is None:
            tsExpire = self._oDb.getCurrentTimestamp();
        self._oDb.execute('UPDATE TestBoxesInSchedGroups\n'
                          'SET    tsExpire = %s\n'
                          'WHERE  idSchedGroup = %s\n'
                          '   AND idTestBox    = %s\n'
                          '   AND tsExpire     = \'infinity\'::TIMESTAMP\n'
                          , ( tsExpire, oBoxInGroup.idSchedGroup, oBoxInGroup.idTestBox, ));
        return True;



#
# Unit testing.
#

# pylint: disable=missing-docstring
class SchedGroupMemberDataTestCase(ModelDataBaseTestCase):
    def setUp(self):
        self.aoSamples = [SchedGroupMemberData(),];

class SchedGroupDataTestCase(ModelDataBaseTestCase):
    def setUp(self):
        self.aoSamples = [SchedGroupData(),];

if __name__ == '__main__':
    unittest.main();
    # not reached.

