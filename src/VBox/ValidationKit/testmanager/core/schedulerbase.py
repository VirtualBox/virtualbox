# -*- coding: utf-8 -*-
# $Id: schedulerbase.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
# pylint: disable=too-many-lines


"""
Test Manager - Base class and utilities for the schedulers.
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
import sys;
import unittest;

# Validation Kit imports.
from common                             import utils, constants;
from testmanager                        import config;
from testmanager.core.build             import BuildDataEx, BuildLogic;
from testmanager.core.base              import ModelDataBase, ModelDataBaseTestCase, TMExceptionBase;
from testmanager.core.buildsource       import BuildSourceData, BuildSourceLogic;
from testmanager.core.globalresource    import GlobalResourceLogic;
from testmanager.core.schedgroup        import SchedGroupData, SchedGroupLogic;
from testmanager.core.systemlog         import SystemLogData, SystemLogLogic;
from testmanager.core.testbox           import TestBoxData, TestBoxDataEx;
from testmanager.core.testboxstatus     import TestBoxStatusData, TestBoxStatusLogic;
from testmanager.core.testcase          import TestCaseLogic;
from testmanager.core.testcaseargs      import TestCaseArgsDataEx, TestCaseArgsLogic;
from testmanager.core.testset           import TestSetData, TestSetLogic;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    xrange = range; # pylint: disable=redefined-builtin,invalid-name



class ReCreateQueueData(object):
    """
    Data object for recreating a scheduling queue.

    It's mostly a storage object, but has a few data checking operation
    associated with it.
    """

    def __init__(self, oDb, idSchedGroup):
        #
        # Load data from the database.
        #
        oSchedGroupLogic = SchedGroupLogic(oDb);
        self.oSchedGroup = oSchedGroupLogic.cachedLookup(idSchedGroup);

        # Will extend the entries with aoTestCases and dTestCases members
        # further down (SchedGroupMemberDataEx).  checkForGroupDepCycles
        # will add aidTestGroupPreReqs.
        self.aoTestGroups       = oSchedGroupLogic.getMembers(idSchedGroup);

        # aoTestCases entries are TestCaseData instance with iSchedPriority
        # and idTestGroup added for our purposes.
        # We will add oTestGroup and aoArgsVariations members to each further down.
        self.aoTestCases        = oSchedGroupLogic.getTestCasesForGroup(idSchedGroup, cMax = 4096);

        # Load dependencies.
        oTestCaseLogic = TestCaseLogic(oDb)
        for oTestCase in self.aoTestCases:
            oTestCase.aidPreReqs = oTestCaseLogic.getTestCasePreReqIds(oTestCase.idTestCase, cMax = 4096);

        # aoTestCases entries are TestCaseArgsData instance with iSchedPriority
        # and idTestGroup added for our purposes.
        # We will add oTestGroup and oTestCase members to each further down.
        self.aoArgsVariations   = oSchedGroupLogic.getTestCaseArgsForGroup(idSchedGroup, cMax = 65536);

        #
        # Generate global lookups.
        #

        # Generate a testcase lookup dictionary for use when working on
        # argument variations.
        self.dTestCases         = {};
        for oTestCase in self.aoTestCases:
            self.dTestCases[oTestCase.idTestCase] = oTestCase;
        assert len(self.dTestCases) <= len(self.aoTestCases); # Note! Can be shorter!

        # Generate a testgroup lookup dictionary.
        self.dTestGroups        = {};
        for oTestGroup in self.aoTestGroups:
            self.dTestGroups[oTestGroup.idTestGroup] = oTestGroup;
        assert len(self.dTestGroups) == len(self.aoTestGroups);

        #
        # Associate extra members with the base data.
        #
        if self.aoTestGroups:
            # Prep the test groups.
            for oTestGroup in self.aoTestGroups:
                oTestGroup.aoTestCases = [];
                oTestGroup.dTestCases  = {};

            # Link testcases to their group, both directions. Prep testcases for
            # argument varation association.
            oTestGroup = self.aoTestGroups[0];
            for oTestCase in self.aoTestCases:
                if oTestGroup.idTestGroup != oTestCase.idTestGroup:
                    oTestGroup = self.dTestGroups[oTestCase.idTestGroup];

                assert oTestCase.idTestCase not in oTestGroup.dTestCases;
                oTestGroup.dTestCases[oTestCase.idTestCase] = oTestCase;
                oTestGroup.aoTestCases.append(oTestCase);
                oTestCase.oTestGroup       = oTestGroup;
                oTestCase.aoArgsVariations = [];

            # Associate testcase argument variations with their testcases (group)
            # in both directions.
            oTestGroup = self.aoTestGroups[0];
            oTestCase  = self.aoTestCases[0] if self.aoTestCases else None;
            for oArgVariation in self.aoArgsVariations:
                if oTestGroup.idTestGroup != oArgVariation.idTestGroup:
                    oTestGroup = self.dTestGroups[oArgVariation.idTestGroup];
                if oTestCase.idTestCase != oArgVariation.idTestCase  or  oTestCase.idTestGroup != oArgVariation.idTestGroup:
                    oTestCase = oTestGroup.dTestCases[oArgVariation.idTestCase];

                oTestCase.aoArgsVariations.append(oArgVariation);
                oArgVariation.oTestCase  = oTestCase;
                oArgVariation.oTestGroup = oTestGroup;

        else:
            assert not self.aoTestCases;
            assert not self.aoArgsVariations;
        # done.

    @staticmethod
    def _addPreReqError(aoErrors, aidChain, oObj, sMsg):
        """ Returns a chain of IDs error entry. """

        sMsg += ' Dependency chain: %s' % (aidChain[0],);
        for i in range(1, len(aidChain)):
            sMsg += ' -> %s' % (aidChain[i],);

        aoErrors.append([sMsg, oObj]);
        return aoErrors;

    def checkForGroupDepCycles(self):
        """
        Checks for testgroup depencency cycles and any missing testgroup
        dependencies.
        Returns array of errors (see SchedulderBase.recreateQueue()).
        """
        aoErrors = [];
        for oTestGroup in self.aoTestGroups:
            idPreReq = oTestGroup.idTestGroupPreReq;
            if idPreReq is None:
                oTestGroup.aidTestGroupPreReqs = [];
                continue;

            aidChain = [oTestGroup.idTestGroup,];
            while idPreReq is not None:
                aidChain.append(idPreReq);
                if len(aidChain) >= 10:
                    self._addPreReqError(aoErrors, aidChain, oTestGroup,
                                         'TestGroup #%s prerequisite chain is too long!'
                                         % (oTestGroup.idTestGroup,));
                    break;

                oDep = self.dTestGroups.get(idPreReq, None);
                if oDep is None:
                    self._addPreReqError(aoErrors, aidChain, oTestGroup,
                                         'TestGroup #%s prerequisite #%s is not in the scheduling group!'
                                         % (oTestGroup.idTestGroup, idPreReq,));
                    break;

                idPreReq = oDep.idTestGroupPreReq;
            oTestGroup.aidTestGroupPreReqs = aidChain[1:];

        return aoErrors;


    def checkForMissingTestCaseDeps(self):
        """
        Checks that testcase dependencies stays within bounds.  We do not allow
        dependencies outside a testgroup, no dependency cycles or even remotely
        long dependency chains.

        Returns array of errors (see SchedulderBase.recreateQueue()).
        """
        aoErrors = [];
        for oTestGroup in self.aoTestGroups:
            for oTestCase in oTestGroup.aoTestCases:
                if not oTestCase.aidPreReqs:
                    continue;

                # Stupid recursion code using special stack(s).
                aiIndexes = [[oTestCase, 0], ];
                aidChain  = [oTestCase.idTestGroup,];
                while aiIndexes:
                    (oCur, i) = aiIndexes[-1];
                    if i >= len(oCur.aidPreReqs):
                        aiIndexes.pop();
                        aidChain.pop();
                    else:
                        aiIndexes[-1][1] = i + 1; # whatever happens, we'll advance on the current level.

                        idPreReq = oTestCase.aidPreReqs[i];
                        oDep = oTestGroup.dTestCases.get(idPreReq, None);
                        if oDep is None:
                            self._addPreReqError(aoErrors, aidChain, oTestCase,
                                                 'TestCase #%s prerequisite #%s is not in the scheduling group!'
                                                 % (oTestCase.idTestCase, idPreReq));
                        elif idPreReq in aidChain:
                            self._addPreReqError(aoErrors, aidChain, oTestCase,
                                                 'TestCase #%s prerequisite #%s creates a cycle!'
                                                 % (oTestCase.idTestCase, idPreReq));
                        elif not oDep.aiPreReqs:
                            pass;
                        elif len(aidChain) >= 10:
                            self._addPreReqError(aoErrors, aidChain, oTestCase,
                                                 'TestCase #%s prerequisite chain is too long!'  % (oTestCase.idTestCase,));
                        else:
                            aiIndexes.append([oDep, 0]);
                            aidChain.append(idPreReq);

        return aoErrors;

    def deepTestGroupSort(self):
        """
        Sorts the testgroups and their testcases by priority and dependencies.
        Note! Don't call this before checking for dependency cycles!
        """
        if not self.aoTestGroups:
            return;

        #
        # ASSUMES groups as well as testcases are sorted by priority by the
        # database.  So we only have to concern ourselves with the dependency
        # sorting.
        #
        iGrpPrio = self.aoTestGroups[0].iSchedPriority;
        for iTestGroup, oTestGroup in enumerate(self.aoTestGroups):
            if oTestGroup.iSchedPriority > iGrpPrio:
                raise TMExceptionBase('Incorrectly sorted testgroups returned by database: iTestGroup=%s prio=%s %s'
                                      % ( iTestGroup, iGrpPrio,
                                          ', '.join(['(%s: %s)' % (oCur.idTestGroup, oCur.iSchedPriority)
                                                     for oCur in self.aoTestGroups]), ) );
            iGrpPrio = oTestGroup.iSchedPriority;

            if oTestGroup.aoTestCases:
                iTstPrio = oTestGroup.aoTestCases[0].iSchedPriority;
                for iTestCase, oTestCase in enumerate(oTestGroup.aoTestCases):
                    if oTestCase.iSchedPriority > iTstPrio:
                        raise TMExceptionBase('Incorrectly sorted testcases returned by database: i=%s prio=%s idGrp=%s %s'
                                              % ( iTestCase, iTstPrio, oTestGroup.idTestGroup,
                                                  ', '.join(['(%s: %s)' % (oCur.idTestCase, oCur.iSchedPriority)
                                                            for oCur in oTestGroup.aoTestCases]),));

        #
        # Sort the testgroups by dependencies.
        #
        i = 0;
        while i < len(self.aoTestGroups):
            oTestGroup = self.aoTestGroups[i];
            if oTestGroup.idTestGroupPreReq is not None:
                iPreReq = self.aoTestGroups.index(self.dTestGroups[oTestGroup.idTestGroupPreReq]);
                if iPreReq > i:
                    # The prerequisite is after the current entry.  Move the
                    # current entry so that it's following it's prereq entry.
                    self.aoTestGroups.insert(iPreReq + 1, oTestGroup);
                    self.aoTestGroups.pop(i);
                    continue;
                assert iPreReq < i;
            i += 1; # Advance.

        #
        # Sort the testcases by dependencies.
        # Same algorithm as above, just more prerequisites.
        #
        for oTestGroup in self.aoTestGroups:
            i = 0;
            while i < len(oTestGroup.aoTestCases):
                oTestCase = oTestGroup.aoTestCases[i];
                if oTestCase.aidPreReqs:
                    for idPreReq in oTestCase.aidPreReqs:
                        iPreReq = oTestGroup.aoTestCases.index(oTestGroup.dTestCases[idPreReq]);
                        if iPreReq > i:
                            # The prerequisite is after the current entry.  Move the
                            # current entry so that it's following it's prereq entry.
                            oTestGroup.aoTestGroups.insert(iPreReq + 1, oTestCase);
                            oTestGroup.aoTestGroups.pop(i);
                            i -= 1; # Don't advance.
                            break;
                        assert iPreReq < i;
                i += 1; # Advance.



class SchedQueueData(ModelDataBase):
    """
    Scheduling queue data item.
    """

    ksIdAttr = 'idSchedGroup';

    ksParam_idSchedGroup            = 'SchedQueueData_idSchedGroup';
    ksParam_idItem                  = 'SchedQueueData_idItem';
    ksParam_offQueue                = 'SchedQueueData_offQueue';
    ksParam_idGenTestCaseArgs       = 'SchedQueueData_idGenTestCaseArgs';
    ksParam_idTestGroup             = 'SchedQueueData_idTestGroup';
    ksParam_aidTestGroupPreReqs     = 'SchedQueueData_aidTestGroupPreReqs';
    ksParam_bmHourlySchedule        = 'SchedQueueData_bmHourlySchedule';
    ksParam_tsConfig                = 'SchedQueueData_tsConfig';
    ksParam_tsLastScheduled         = 'SchedQueueData_tsLastScheduled';
    ksParam_idTestSetGangLeader     = 'SchedQueueData_idTestSetGangLeader';
    ksParam_cMissingGangMembers     = 'SchedQueueData_cMissingGangMembers';

    kasAllowNullAttributes = [ 'idItem', 'offQueue', 'aidTestGroupPreReqs', 'bmHourlySchedule', 'idTestSetGangLeader',
                               'tsConfig', 'tsLastScheduled' ];


    def __init__(self):
        ModelDataBase.__init__(self);

        #
        # Initialize with defaults.
        # See the database for explanations of each of these fields.
        #
        self.idSchedGroup           = None;
        self.idItem                 = None;
        self.offQueue               = None;
        self.idGenTestCaseArgs      = None;
        self.idTestGroup            = None;
        self.aidTestGroupPreReqs    = None;
        self.bmHourlySchedule       = None;
        self.tsConfig               = None;
        self.tsLastScheduled        = None;
        self.idTestSetGangLeader    = None;
        self.cMissingGangMembers    = 1;

    def initFromValues(self, idSchedGroup, idGenTestCaseArgs, idTestGroup, aidTestGroupPreReqs, # pylint: disable=too-many-arguments
                       bmHourlySchedule, cMissingGangMembers,
                       idItem = None, offQueue = None, tsConfig = None, tsLastScheduled = None, idTestSetGangLeader = None):
        """
        Reinitialize with all attributes potentially given as inputs.
        Return self.
        """
        self.idSchedGroup           = idSchedGroup;
        self.idItem                 = idItem;
        self.offQueue               = offQueue;
        self.idGenTestCaseArgs      = idGenTestCaseArgs;
        self.idTestGroup            = idTestGroup;
        self.aidTestGroupPreReqs    = aidTestGroupPreReqs;
        self.bmHourlySchedule       = bmHourlySchedule;
        self.tsConfig               = tsConfig;
        self.tsLastScheduled        = tsLastScheduled;
        self.idTestSetGangLeader    = idTestSetGangLeader;
        self.cMissingGangMembers    = cMissingGangMembers;
        return self;

    def initFromDbRow(self, aoRow):
        """
        Initialize from database row (SELECT * FROM SchedQueues).
        Returns self.
        Raises exception if no row is specfied.
        """
        if aoRow is None:
            raise TMExceptionBase('SchedQueueData not found.');

        self.idSchedGroup           = aoRow[0];
        self.idItem                 = aoRow[1];
        self.offQueue               = aoRow[2];
        self.idGenTestCaseArgs      = aoRow[3];
        self.idTestGroup            = aoRow[4];
        self.aidTestGroupPreReqs    = aoRow[5];
        self.bmHourlySchedule       = aoRow[6];
        self.tsConfig               = aoRow[7];
        self.tsLastScheduled        = aoRow[8];
        self.idTestSetGangLeader    = aoRow[9];
        self.cMissingGangMembers    = aoRow[10];
        return self;






class SchedulerBase(object):
    """
    The scheduler base class.

    The scheduler classes have two functions:
        1. Recreate the scheduling queue.
        2. Pick the next task from the queue.

    The first is scheduler specific, the latter isn't.
    """

    class BuildCache(object):
        """ Build cache. """

        class BuildCacheIterator(object):
            """ Build class iterator. """
            def __init__(self, oCache):
                self.oCache = oCache;
                self.iCur   = 0;

            def __iter__(self):
                """Returns self, required by the language."""
                return self;

            def __next__(self):
                """Returns the next build, raises StopIteration when the end has been reached."""
                while True:
                    if self.iCur >= len(self.oCache.aoEntries):
                        oEntry = self.oCache.fetchFromCursor();
                        if oEntry is None:
                            raise StopIteration;
                    else:
                        oEntry = self.oCache.aoEntries[self.iCur];
                    self.iCur += 1;
                    if not oEntry.fRemoved:
                        return oEntry;
                return None; # not reached, but make pylint happy (for now).

            def next(self):
                """ For python 2.x. """
                return self.__next__();     # pylint: disable=unnecessary-dunder-call

        class BuildCacheEntry(object):
            """ Build cache entry. """

            def __init__(self, oBuild, fMaybeBlacklisted):
                self.oBuild             = oBuild;
                self._fBlacklisted      = None if fMaybeBlacklisted is True else False;
                self.fRemoved           = False;
                self._dPreReqDecisions  = {};

            def remove(self):
                """
                Marks the cache entry as removed.
                This doesn't actually remove it from the cache array, only marks
                it as removed. It has no effect on open iterators.
                """
                self.fRemoved = True;

            def getPreReqDecision(self, sPreReqSet):
                """
                Retrieves a cached prerequisite decision.
                Returns boolean if found, None if not.
                """
                return self._dPreReqDecisions.get(sPreReqSet);

            def setPreReqDecision(self, sPreReqSet, fDecision):
                """
                Caches a prerequistie decision.
                """
                self._dPreReqDecisions[sPreReqSet] = fDecision;
                return fDecision;

            def isBlacklisted(self, oDb):
                """ Checks if the build is blacklisted. """
                if self._fBlacklisted is None:
                    self._fBlacklisted = BuildLogic(oDb).isBuildBlacklisted(self.oBuild);
                return self._fBlacklisted;


        def __init__(self):
            self.aoEntries = [];
            self.oCursor   = None;

        def setupSource(self, oDb, idBuildSrc, sOs, sCpuArch, tsNow):
            """ Configures the build cursor for the cache. """
            if not self.aoEntries and self.oCursor is None:
                oBuildSource = BuildSourceData().initFromDbWithId(oDb, idBuildSrc, tsNow);
                self.oCursor = BuildSourceLogic(oDb).openBuildCursor(oBuildSource, sOs, sCpuArch, tsNow);
            return True;

        def __iter__(self):
            """Return an iterator."""
            return self.BuildCacheIterator(self);

        def fetchFromCursor(self):
            """ Fetches a build from the cursor and adds it to the cache."""
            if self.oCursor is None:
                return None;

            try:
                aoRow = self.oCursor.fetchOne();
            except:
                return None;
            if aoRow is None:
                return None;

            oBuild = BuildDataEx().initFromDbRow(aoRow);
            oEntry = self.BuildCacheEntry(oBuild, aoRow[-1]);
            self.aoEntries.append(oEntry);
            return oEntry;

    def __init__(self, oDb, oSchedGrpData, iVerbosity = 0, tsSecStart = None):
        self._oDb           = oDb;
        self._oSchedGrpData = oSchedGrpData;
        self._iVerbosity    = iVerbosity;
        self._asMessages    = [];
        self._tsSecStart    = tsSecStart if tsSecStart is not None else utils.timestampSecond();
        self.oBuildCache    = self.BuildCache();
        self.dTestGroupMembers = {};

    @staticmethod
    def _instantiate(oDb, oSchedGrpData, iVerbosity = 0, tsSecStart = None):
        """
        Instantiate the scheduler specified by the scheduling group.
        Returns scheduler child class instance.  May raise exception if
        the input is invalid.
        """
        if oSchedGrpData.enmScheduler == SchedGroupData.ksScheduler_BestEffortContinuousIntegration:
            from testmanager.core.schedulerbeci import SchdulerBeci;
            oScheduler = SchdulerBeci(oDb, oSchedGrpData, iVerbosity, tsSecStart);
        else:
            raise oDb.integrityException('Invalid scheduler "%s", idSchedGroup=%d' \
                                         % (oSchedGrpData.enmScheduler, oSchedGrpData.idSchedGroup));
        return oScheduler;


    #
    # Misc.
    #

    def msgDebug(self, sText):
        """Debug printing."""
        if self._iVerbosity > 1:
            self._asMessages.append('debug:' + sText);
        return None;

    def msgInfo(self, sText):
        """Info printing."""
        if self._iVerbosity > 1:
            self._asMessages.append('info: ' + sText);
        return None;

    def dprint(self, sMsg):
        """Prints a debug message to the srv glue log (see config.py). """
        if config.g_kfSrvGlueDebugScheduler:
            self._oDb.dprint(sMsg);
        return None;

    def getElapsedSecs(self):
        """ Returns the number of seconds this scheduling task has been running. """
        tsSecNow = utils.timestampSecond();
        if tsSecNow < self._tsSecStart: # paranoia
            self._tsSecStart = tsSecNow;
        return tsSecNow - self._tsSecStart;


    #
    # Create schedule.
    #

    def _recreateQueueCancelGatherings(self):
        """
        Cancels all pending gang gatherings on the current queue.
        """
        self._oDb.execute('SELECT   idTestSetGangLeader\n'
                          'FROM     SchedQueues\n'
                          'WHERE    idSchedGroup = %s\n'
                          '     AND idTestSetGangLeader is not NULL\n'
                          , (self._oSchedGrpData.idSchedGroup,));
        if self._oDb.getRowCount() > 0:
            oTBStatusLogic = TestBoxStatusLogic(self._oDb);
            for aoRow in self._oDb.fetchAll():
                idTestSetGangLeader = aoRow[0];
                oTBStatusLogic.updateGangStatus(idTestSetGangLeader,
                                                TestBoxStatusData.ksTestBoxState_GangGatheringTimedOut,
                                                fCommit = False);
        return True;

    def _recreateQueueItems(self, oData):
        """
        Returns an array of queue items (SchedQueueData).
        Child classes must override this.
        """
        _ = oData;
        return [];

    def recreateQueueWorker(self):
        """
        Worker for recreateQueue.
        """

        #
        # Collect the necessary data and validate it.
        #
        oData = ReCreateQueueData(self._oDb, self._oSchedGrpData.idSchedGroup);
        aoErrors = oData.checkForGroupDepCycles();
        aoErrors.extend(oData.checkForMissingTestCaseDeps());
        if not aoErrors:
            oData.deepTestGroupSort();

            #
            # The creation of the scheduling queue is done by the child class.
            #
            # We will try guess where in queue we're currently at and rotate
            # the items such that we will resume execution in the approximately
            # same position.  The goal of the scheduler is to provide a 100%
            # deterministic result so that if we regenerate the queue when there
            # are no changes to the testcases, testgroups or scheduling groups
            # involved, test execution will be unchanged (save for maybe just a
            # little for gang gathering).
            #
            aoItems = [];
            if not oData.oSchedGroup.fEnabled:
                self.msgInfo('Disabled.');
            elif not oData.aoArgsVariations:
                self.msgInfo('Found no test case argument variations.');
            else:
                aoItems = self._recreateQueueItems(oData);
                self.msgDebug('len(aoItems)=%s' % (len(aoItems),));
                #for i in range(len(aoItems)):
                #    self.msgDebug('aoItems[%2d]=%s' % (i, aoItems[i]));
            if aoItems:
                self._oDb.execute('SELECT offQueue FROM SchedQueues WHERE idSchedGroup = %s ORDER BY idItem LIMIT 1'
                                  , (self._oSchedGrpData.idSchedGroup,));
                if self._oDb.getRowCount() > 0:
                    offQueue = self._oDb.fetchOne()[0];
                    self._oDb.execute('SELECT COUNT(*) FROM SchedQueues WHERE idSchedGroup = %s'
                                      , (self._oSchedGrpData.idSchedGroup,));
                    cItems  = self._oDb.fetchOne()[0];
                    offQueueNew = (offQueue * cItems) // len(aoItems);
                    if offQueueNew != 0:
                        aoItems = aoItems[offQueueNew:] + aoItems[:offQueueNew];

            #
            # Replace the scheduling queue.
            # Care need to be take to first timeout/abort any gangs in the
            # gathering state since these use the queue to set up the date.
            #
            self._recreateQueueCancelGatherings();
            self._oDb.execute('DELETE FROM SchedQueues WHERE idSchedGroup = %s\n', (self._oSchedGrpData.idSchedGroup,));
            if aoItems:
                self._oDb.insertList('INSERT INTO SchedQueues (\n'
                                      '         idSchedGroup,\n'
                                      '         offQueue,\n'
                                      '         idGenTestCaseArgs,\n'
                                      '         idTestGroup,\n'
                                      '         aidTestGroupPreReqs,\n'
                                      '         bmHourlySchedule,\n'
                                      '         cMissingGangMembers )\n',
                                     aoItems, self._formatItemForInsert);
        return (aoErrors, self._asMessages);

    def _formatItemForInsert(self, oItem):
        """
        Used by recreateQueueWorker together with TMDatabaseConnect::insertList
        """
        return self._oDb.formatBindArgs('(%s,%s,%s,%s,%s,%s,%s)'
                                        , ( oItem.idSchedGroup,
                                            oItem.offQueue,
                                            oItem.idGenTestCaseArgs,
                                            oItem.idTestGroup,
                                            oItem.aidTestGroupPreReqs if oItem.aidTestGroupPreReqs else None,
                                            oItem.bmHourlySchedule,
                                            oItem.cMissingGangMembers
                                        ));

    @staticmethod
    def recreateQueue(oDb, uidAuthor, idSchedGroup, iVerbosity = 1):
        """
        (Re-)creates the scheduling queue for the given group.

        Returns (asMessages, asMessages). On success the array with the error
        will be empty, on failure it will contain (sError, oRelatedObject)
        entries.  The messages is for debugging and are simple strings.

        Raises exception database error.
        """

        aoExtraMsgs = [];
        if oDb.debugIsExplainEnabled():
            aoExtraMsgs += ['Warning! Disabling SQL explain to avoid deadlocking against locked tables.'];
            oDb.debugDisableExplain();

        aoErrors   = [];
        asMessages = [];
        try:
            #
            # To avoid concurrency issues (SchedQueues) and inconsistent data (*),
            # we lock quite a few tables while doing this work.  We access more
            # data than scheduleNewTask so we lock some additional tables.
            #
            oDb.rollback();
            oDb.begin();
            oDb.execute('LOCK TABLE SchedGroups, SchedGroupMembers, TestGroups, TestGroupMembers IN SHARE MODE');
            oDb.execute('LOCK TABLE TestBoxes, TestCaseArgs, TestCases IN SHARE MODE');
            oDb.execute('LOCK TABLE TestBoxStatuses, SchedQueues IN EXCLUSIVE MODE');

            #
            # Instantiate the scheduler and call the worker function.
            #
            oSchedGrpData = SchedGroupData().initFromDbWithId(oDb, idSchedGroup);
            oScheduler = SchedulerBase._instantiate(oDb, oSchedGrpData, iVerbosity);

            (aoErrors, asMessages) = oScheduler.recreateQueueWorker();
            if not aoErrors:
                SystemLogLogic(oDb).addEntry(SystemLogData.ksEvent_SchedQueueRecreate,
                                             'User #%d recreated sched queue #%d.' % (uidAuthor, idSchedGroup,));
                oDb.commit();
            else:
                oDb.rollback();

        except:
            oDb.rollback();
            raise;

        return (aoErrors, aoExtraMsgs + asMessages);


    @staticmethod
    def cleanUpOrphanedQueues(oDb):
        """
        Removes orphan scheduling queues from the SchedQueues table.

        Queues becomes orphaned when the scheduling group they belongs to has been deleted.

        Returns number of orphaned queues.
        Raises exception database error.
        """
        cRet = 0;
        try:
            oDb.rollback();
            oDb.begin();
            oDb.execute('''
SELECT  SchedQueues.idSchedGroup
FROM    SchedQueues
        LEFT OUTER JOIN SchedGroups
                     ON SchedGroups.idSchedGroup = SchedQueues.idSchedGroup
                    AND SchedGroups.tsExpire     = 'infinity'::TIMESTAMP
WHERE   SchedGroups.idSchedGroup is NULL
GROUP BY SchedQueues.idSchedGroup''');
            aaoOrphanRows = oDb.fetchAll();
            cRet = len(aaoOrphanRows);
            if cRet > 0:
                oDb.execute('DELETE FROM SchedQueues WHERE idSchedGroup IN (%s)'
                            % (','.join([str(aoRow[0]) for aoRow in aaoOrphanRows]),));
                oDb.commit();
        except:
            oDb.rollback();
            raise;
        return cRet;


    #
    # Schedule Task.
    #

    def _composeGangArguments(self, idTestSet):
        """
        Composes the gang specific testdriver arguments.
        Returns command line string, including a leading space.
        """

        oTestSet      = TestSetData().initFromDbWithId(self._oDb, idTestSet);
        aoGangMembers = TestSetLogic(self._oDb).getGang(oTestSet.idTestSetGangLeader);

        sArgs = ' --gang-member-no %s --gang-members %s' % (oTestSet.iGangMemberNo, len(aoGangMembers));
        for i, sIp in enumerate(aoGangMembers):
            sArgs = ' --gang-ipv4-%s %s' % (i, sIp); ## @todo IPv6

        return sArgs;


    def composeExecResponseWorker(self, idTestSet, oTestEx, oTestBox, oBuild, oValidationKitBuild, sBaseUrl):
        """
        Given all the bits of data, compose an EXEC command response to the testbox.
        """
        sScriptZips = oTestEx.oTestCase.sValidationKitZips;
        if sScriptZips is None  or  sScriptZips.find('@VALIDATIONKIT_ZIP@') >= 0:
            assert oValidationKitBuild;
            if sScriptZips is None:
                sScriptZips = oValidationKitBuild.sBinaries;
            else:
                sScriptZips = sScriptZips.replace('@VALIDATIONKIT_ZIP@', oValidationKitBuild.sBinaries);
        sScriptZips = sScriptZips.replace('@DOWNLOAD_BASE_URL@', sBaseUrl + config.g_ksTmDownloadBaseUrlRel);

        sCmdLine = oTestEx.oTestCase.sBaseCmd + ' ' + oTestEx.sArgs;
        sCmdLine = sCmdLine.replace('@BUILD_BINARIES@', oBuild.sBinaries);
        sCmdLine = sCmdLine.strip();
        if oTestEx.cGangMembers > 1:
            sCmdLine += ' ' + self._composeGangArguments(idTestSet);

        cSecTimeout = oTestEx.cSecTimeout if oTestEx.cSecTimeout is not None else oTestEx.oTestCase.cSecTimeout;
        cSecTimeout = cSecTimeout * oTestBox.pctScaleTimeout // 100;

        dResponse   = \
        {
            constants.tbresp.ALL_PARAM_RESULT:              constants.tbresp.CMD_EXEC,
            constants.tbresp.EXEC_PARAM_RESULT_ID:          idTestSet,
            constants.tbresp.EXEC_PARAM_SCRIPT_ZIPS:        sScriptZips,
            constants.tbresp.EXEC_PARAM_SCRIPT_CMD_LINE:    sCmdLine,
            constants.tbresp.EXEC_PARAM_TIMEOUT:            cSecTimeout,
        };
        return dResponse;

    @staticmethod
    def composeExecResponse(oDb, idTestSet, sBaseUrl, iVerbosity = 0):
        """
        Composes an EXEC response for a gang member (other than the last).
        Returns a EXEC response or raises an exception (DB/input error).
        """
        #
        # Gather the necessary data.
        #
        oTestSet      = TestSetData().initFromDbWithId(oDb, idTestSet);
        oTestBox      = TestBoxData().initFromDbWithGenId(oDb, oTestSet.idGenTestBox);
        oTestEx       = TestCaseArgsDataEx().initFromDbWithGenIdEx(oDb, oTestSet.idGenTestCaseArgs,
                                                                   tsConfigEff = oTestSet.tsConfig,
                                                                   tsRsrcEff = oTestSet.tsConfig);
        oBuild        = BuildDataEx().initFromDbWithId(oDb, oTestSet.idBuild);
        oValidationKitBuild = None;
        if oTestSet.idBuildTestSuite is not None:
            oValidationKitBuild = BuildDataEx().initFromDbWithId(oDb, oTestSet.idBuildTestSuite);

        #
        # Instantiate the specified scheduler and let it do the rest.
        #
        oSchedGrpData = SchedGroupData().initFromDbWithId(oDb, oTestSet.idSchedGroup, oTestSet.tsCreated);
        assert oSchedGrpData.fEnabled   is True;
        assert oSchedGrpData.idBuildSrc is not None;
        oScheduler = SchedulerBase._instantiate(oDb, oSchedGrpData, iVerbosity);

        return oScheduler.composeExecResponseWorker(idTestSet, oTestEx, oTestBox, oBuild, oValidationKitBuild, sBaseUrl);


    def _updateTask(self, oTask, tsNow):
        """
        Updates a gang schedule task.
        """
        assert oTask.cMissingGangMembers >= 1;
        assert oTask.idTestSetGangLeader is not None;
        assert oTask.idTestSetGangLeader >= 1;
        if tsNow is not None:
            self._oDb.execute('UPDATE SchedQueues\n'
                              '   SET idTestSetGangLeader = %s,\n'
                              '       cMissingGangMembers = %s,\n'
                              '       tsLastScheduled     = %s\n'
                              'WHERE  idItem = %s\n'
                              , (oTask.idTestSetGangLeader, oTask.cMissingGangMembers, tsNow, oTask.idItem,) );
        else:
            self._oDb.execute('UPDATE SchedQueues\n'
                              '   SET cMissingGangMembers = %s\n'
                              'WHERE  idItem = %s\n'
                              , (oTask.cMissingGangMembers, oTask.idItem,) );
        return True;

    def _moveTaskToEndOfQueue(self, oTask, cGangMembers, tsNow):
        """
        The task has been scheduled successfully, reset it's data move it to
        the end of the queue.
        """
        if cGangMembers > 1:
            self._oDb.execute('UPDATE SchedQueues\n'
                              '   SET idItem = NEXTVAL(\'SchedQueueItemIdSeq\'),\n'
                              '       idTestSetGangLeader = NULL,\n'
                              '       cMissingGangMembers = %s\n'
                              'WHERE  idItem = %s\n'
                              , (cGangMembers, oTask.idItem,) );
        else:
            self._oDb.execute('UPDATE SchedQueues\n'
                              '   SET idItem = NEXTVAL(\'SchedQueueItemIdSeq\'),\n'
                              '       idTestSetGangLeader = NULL,\n'
                              '       cMissingGangMembers = 1,\n'
                              '       tsLastScheduled     = %s\n'
                              'WHERE  idItem = %s\n'
                              , (tsNow, oTask.idItem,) );
        return True;




    def _createTestSet(self, oTask, oTestEx, oTestBoxData, oBuild, oValidationKitBuild, tsNow):
        # type: (SchedQueueData, TestCaseArgsDataEx, TestBoxData, BuildDataEx, BuildDataEx, datetime.datetime) -> int
        """
        Creates a test set for using the given data.
        Will not commit, someone up the callstack will that later on.

        Returns the test set ID, may raise an exception on database error.
        """
        # Lazy bird doesn't want to write testset.py and does it all here.

        #
        # We're getting the TestSet ID first in order to include it in the base
        # file name (that way we can directly relate files on the disk to the
        # test set when doing batch work), and also for idTesetSetGangLeader.
        #
        self._oDb.execute('SELECT NEXTVAL(\'TestSetIdSeq\')');
        idTestSet = self._oDb.fetchOne()[0];

        sBaseFilename = '%04d/%02d/%02d/%02d/TestSet-%s' \
                      % (tsNow.year, tsNow.month, tsNow.day, (tsNow.hour // 6) * 6, idTestSet);

        #
        # Gang scheduling parameters.  Changes the oTask data for updating by caller.
        #
        iGangMemberNo = 0;

        if oTestEx.cGangMembers <= 1:
            assert oTask.idTestSetGangLeader is None;
            assert oTask.cMissingGangMembers <= 1;
        elif oTask.idTestSetGangLeader is None:
            assert oTask.cMissingGangMembers == oTestEx.cGangMembers;
            oTask.cMissingGangMembers = oTestEx.cGangMembers - 1;
            oTask.idTestSetGangLeader = idTestSet;
        else:
            assert oTask.cMissingGangMembers > 0 and oTask.cMissingGangMembers < oTestEx.cGangMembers;
            oTask.cMissingGangMembers -= 1;

        #
        # Do the database stuff.
        #
        self._oDb.execute('INSERT INTO  TestSets (\n'
                          '             idTestSet,\n'
                          '             tsConfig,\n'
                          '             tsCreated,\n'
                          '             idBuild,\n'
                          '             idBuildCategory,\n'
                          '             idBuildTestSuite,\n'
                          '             idGenTestBox,\n'
                          '             idTestBox,\n'
                          '             idSchedGroup,\n'
                          '             idTestGroup,\n'
                          '             idGenTestCase,\n'
                          '             idTestCase,\n'
                          '             idGenTestCaseArgs,\n'
                          '             idTestCaseArgs,\n'
                          '             sBaseFilename,\n'
                          '             iGangMemberNo,\n'
                          '             idTestSetGangLeader )\n'
                          'VALUES (     %s,\n'      # idTestSet
                          '             %s,\n'      # tsConfig
                          '             %s,\n'      # tsCreated
                          '             %s,\n'      # idBuild
                          '             %s,\n'      # idBuildCategory
                          '             %s,\n'      # idBuildTestSuite
                          '             %s,\n'      # idGenTestBox
                          '             %s,\n'      # idTestBox
                          '             %s,\n'      # idSchedGroup
                          '             %s,\n'      # idTestGroup
                          '             %s,\n'      # idGenTestCase
                          '             %s,\n'      # idTestCase
                          '             %s,\n'      # idGenTestCaseArgs
                          '             %s,\n'      # idTestCaseArgs
                          '             %s,\n'      # sBaseFilename
                          '             %s,\n'      # iGangMemberNo
                          '             %s)\n'      # idTestSetGangLeader
                          , ( idTestSet,
                              oTask.tsConfig,
                              tsNow,
                              oBuild.idBuild,
                              oBuild.idBuildCategory,
                              oValidationKitBuild.idBuild if oValidationKitBuild is not None else None,
                              oTestBoxData.idGenTestBox,
                              oTestBoxData.idTestBox,
                              oTask.idSchedGroup,
                              oTask.idTestGroup,
                              oTestEx.oTestCase.idGenTestCase,
                              oTestEx.oTestCase.idTestCase,
                              oTestEx.idGenTestCaseArgs,
                              oTestEx.idTestCaseArgs,
                              sBaseFilename,
                              iGangMemberNo,
                              oTask.idTestSetGangLeader,
                          ));

        self._oDb.execute('INSERT INTO  TestResults (\n'
                          '             idTestResultParent,\n'
                          '             idTestSet,\n'
                          '             tsCreated,\n'
                          '             idStrName,\n'
                          '             cErrors,\n'
                          '             enmStatus,\n'
                          '             iNestingDepth)\n'
                          'VALUES (     NULL,\n'    # idTestResultParent
                          '             %s,\n'      # idTestSet
                          '             %s,\n'      # tsCreated
                          '             0,\n'       # idStrName
                          '             0,\n'       # cErrors
                          '             \'running\'::TestStatus_T,\n'
                          '             0)\n'       # iNestingDepth
                          'RETURNING    idTestResult'
                          , ( idTestSet, tsNow, ));
        idTestResult = self._oDb.fetchOne()[0];

        self._oDb.execute('UPDATE   TestSets\n'
                          '     SET idTestResult = %s\n'
                          'WHERE    idTestSet = %s\n'
                          , (idTestResult, idTestSet, ));

        return idTestSet;

    def _tryFindValidationKitBit(self, oTestBoxData, tsNow):
        """
        Tries to find the most recent validation kit build suitable for the given testbox.
        Returns BuildDataEx or None. Raise exception on database error.

        Can be overridden by child classes to change the default build requirements.
        """
        oBuildLogic  = BuildLogic(self._oDb);
        oBuildSource = BuildSourceData().initFromDbWithId(self._oDb, self._oSchedGrpData.idBuildSrcTestSuite, tsNow);
        oCursor = BuildSourceLogic(self._oDb).openBuildCursor(oBuildSource, oTestBoxData.sOs, oTestBoxData.sCpuArch, tsNow);
        for _ in range(oCursor.getRowCount()):
            oBuild = BuildDataEx().initFromDbRow(oCursor.fetchOne());
            if not oBuildLogic.isBuildBlacklisted(oBuild):
                return oBuild;
        return None;

    def _tryFindBuild(self, oTask, oTestEx, oTestBoxData, tsNow):
        """
        Tries to find a fitting build.
        Returns BuildDataEx or None. Raise exception on database error.

        Can be overridden by child classes to change the default build requirements.
        """

        #
        # Gather the set of prerequisites we have and turn them into a value
        # set for use in the loop below.
        #
        # Note! We're scheduling on testcase level and ignoring argument variation
        #       selections in TestGroupMembers is intentional.
        #
        dPreReqs = {};

        # Direct prerequisites. We assume they're all enabled as this can be
        # checked at queue creation time.
        for oPreReq in oTestEx.aoTestCasePreReqs:
            dPreReqs[oPreReq.idTestCase] = 1;

        # Testgroup dependencies from the scheduling group config.
        if oTask.aidTestGroupPreReqs is not None:
            for iTestGroup in oTask.aidTestGroupPreReqs:
                # Make sure the _active_ test group members are in the cache.
                if iTestGroup not in self.dTestGroupMembers:
                    self._oDb.execute('SELECT DISTINCT TestGroupMembers.idTestCase\n'
                                      'FROM     TestGroupMembers, TestCases\n'
                                      'WHERE    TestGroupMembers.idTestGroup  = %s\n'
                                      '     AND TestGroupMembers.tsExpire     > %s\n'
                                      '     AND TestGroupMembers.tsEffective <= %s\n'
                                      '     AND TestCases.idTestCase = TestGroupMembers.idTestCase\n'
                                      '     AND TestCases.tsExpire     > %s\n'
                                      '     AND TestCases.tsEffective <= %s\n'
                                      '     AND TestCases.fEnabled is TRUE\n'
                                      , (iTestGroup, oTask.tsConfig, oTask.tsConfig, oTask.tsConfig, oTask.tsConfig,));
                    aidTestCases = [];
                    for aoRow in self._oDb.fetchAll():
                        aidTestCases.append(aoRow[0]);
                    self.dTestGroupMembers[iTestGroup] = aidTestCases;

                # Add the testgroup members to the prerequisites.
                for idTestCase in self.dTestGroupMembers[iTestGroup]:
                    dPreReqs[idTestCase] = 1;

        # Create a SQL values table out of them.
        sPreReqSet = ''
        if dPreReqs:
            for idPreReq in sorted(dPreReqs):
                sPreReqSet += ', (' + str(idPreReq) + ')';
            sPreReqSet = sPreReqSet[2:]; # drop the leading ', '.

        #
        # Try the builds.
        #
        self.oBuildCache.setupSource(self._oDb, self._oSchedGrpData.idBuildSrc, oTestBoxData.sOs, oTestBoxData.sCpuArch, tsNow);
        for oEntry in self.oBuildCache:
            #
            # Check build requirements set by the test.
            #
            if not oTestEx.matchesBuildProps(oEntry.oBuild):
                continue;

            if oEntry.isBlacklisted(self._oDb):
                oEntry.remove();
                continue;

            #
            # Check prerequisites.  The default scheduler is satisfied if one
            # argument variation has been executed successfully.  It is not
            # satisfied if there are any failure runs.
            #
            if sPreReqSet:
                fDecision = oEntry.getPreReqDecision(sPreReqSet);
                if fDecision is None:
                    # Check for missing prereqs.
                    self._oDb.execute('SELECT   COUNT(*)\n'
                                      'FROM     (VALUES ' + sPreReqSet + ') AS PreReqs(idTestCase)\n'
                                      'LEFT OUTER JOIN (SELECT  idTestSet\n'
                                      '                 FROM    TestSets\n'
                                      '                 WHERE   enmStatus IN (%s, %s)\n'
                                      '                     AND idBuild = %s\n'
                                      '                 ) AS TestSets\n'
                                      '      ON (PreReqs.idTestCase = TestSets.idTestCase)\n'
                                      'WHERE    TestSets.idTestSet is NULL\n'
                                      , ( TestSetData.ksTestStatus_Success, TestSetData.ksTestStatus_Skipped,
                                          oEntry.oBuild.idBuild, ));
                    cMissingPreReqs = self._oDb.fetchOne()[0];
                    if cMissingPreReqs > 0:
                        self.dprint('build %s is missing %u prerequisites (out of %s)'
                                    % (oEntry.oBuild.idBuild, cMissingPreReqs, sPreReqSet,));
                        oEntry.setPreReqDecision(sPreReqSet, False);
                        continue;

                    # Check for failed prereq runs.
                    self._oDb.execute('SELECT   COUNT(*)\n'
                                      'FROM     (VALUES ' + sPreReqSet + ') AS PreReqs(idTestCase),\n'
                                      '         TestSets\n'
                                      'WHERE    PreReqs.idTestCase = TestSets.idTestCase\n'
                                      '     AND TestSets.idBuild   = %s\n'
                                      '     AND TestSets.enmStatus IN (%s, %s, %s)\n'
                                      , ( oEntry.oBuild.idBuild,
                                          TestSetData.ksTestStatus_Failure,
                                          TestSetData.ksTestStatus_TimedOut,
                                          TestSetData.ksTestStatus_Rebooted,
                                        )
                                     );
                    cFailedPreReqs = self._oDb.fetchOne()[0];
                    if cFailedPreReqs > 0:
                        self.dprint('build %s is has %u prerequisite failures (out of %s)'
                                    % (oEntry.oBuild.idBuild, cFailedPreReqs, sPreReqSet,));
                        oEntry.setPreReqDecision(sPreReqSet, False);
                        continue;

                    oEntry.setPreReqDecision(sPreReqSet, True);
                elif not fDecision:
                    continue;

            #
            # If we can, check if the build files still exist.
            #
            if oEntry.oBuild.areFilesStillThere() is False:
                self.dprint('build %s no longer exists' % (oEntry.oBuild.idBuild,));
                oEntry.remove();
                continue;

            self.dprint('found oBuild=%s' % (oEntry.oBuild,));
            return oEntry.oBuild;
        return None;

    def _tryFindMatchingBuild(self, oLeaderBuild, oTestBoxData, idBuildSrc):
        """
        Tries to find a matching build for gang scheduling.
        Returns BuildDataEx or None. Raise exception on database error.

        Can be overridden by child classes to change the default build requirements.
        """
        #
        # Note! Should probably check build prerequisites if we get a different
        #       build back, so that we don't use a build which hasn't passed
        #       the smoke test.
        #
        _ = idBuildSrc;
        return BuildLogic(self._oDb).tryFindSameBuildForOsArch(oLeaderBuild, oTestBoxData.sOs, oTestBoxData.sCpuArch);


    def _tryAsLeader(self, oTask, oTestEx, oTestBoxData, tsNow, sBaseUrl):
        """
        Try schedule the task as a gang leader (can be a gang of one).
        Returns response or None.  May raise exception on DB error.
        """

        # We don't wait for busy resources, we just try the next test.
        oTestArgsLogic = TestCaseArgsLogic(self._oDb);
        if not oTestArgsLogic.areResourcesFree(oTestEx):
            self.dprint('Cannot get global test resources!');
            return None;

        #
        # Find a matching build (this is the difficult bit).
        #
        oBuild = self._tryFindBuild(oTask, oTestEx, oTestBoxData, tsNow);
        if oBuild is None:
            self.dprint('No build!');
            return None;
        if oTestEx.oTestCase.needValidationKitBit():
            oValidationKitBuild = self._tryFindValidationKitBit(oTestBoxData, tsNow);
            if oValidationKitBuild is None:
                self.dprint('No validation kit build!');
                return None;
        else:
            oValidationKitBuild = None;

        #
        # Create a testset, allocate the resources and update the state.
        # Note! Since resource allocation may still fail, we create a nested
        #       transaction so we can roll back. (Heed lock warning in docs!)
        #
        self._oDb.execute('SAVEPOINT tryAsLeader');
        idTestSet = self._createTestSet(oTask, oTestEx, oTestBoxData, oBuild, oValidationKitBuild, tsNow);

        if GlobalResourceLogic(self._oDb).allocateResources(oTestBoxData.idTestBox, oTestEx.aoGlobalRsrc, fCommit = False) \
           is not True:
            self._oDb.execute('ROLLBACK TO SAVEPOINT tryAsLeader');
            self.dprint('Failed to allocate global resources!');
            return False;

        if oTestEx.cGangMembers <= 1:
            # We're alone, put the task back at the end of the queue and issue EXEC cmd.
            self._moveTaskToEndOfQueue(oTask, oTestEx.cGangMembers, tsNow);
            dResponse = self.composeExecResponseWorker(idTestSet, oTestEx, oTestBoxData, oBuild, oValidationKitBuild, sBaseUrl);
            sTBState = TestBoxStatusData.ksTestBoxState_Testing;
        else:
            # We're missing gang members, issue WAIT cmd.
            self._updateTask(oTask, tsNow if idTestSet == oTask.idTestSetGangLeader else None);
            dResponse = { constants.tbresp.ALL_PARAM_RESULT: constants.tbresp.CMD_WAIT, };
            sTBState = TestBoxStatusData.ksTestBoxState_GangGathering;

        TestBoxStatusLogic(self._oDb).updateState(oTestBoxData.idTestBox, sTBState, idTestSet, fCommit = False);
        self._oDb.execute('RELEASE SAVEPOINT tryAsLeader');
        return dResponse;

    def _tryAsGangMember(self, oTask, oTestEx, oTestBoxData, tsNow, sBaseUrl):
        """
        Try schedule the task as a gang member.
        Returns response or None.  May raise exception on DB error.
        """

        #
        # The leader has choosen a build, we need to find a matching one for our platform.
        # (It's up to the scheduler decide upon how strict dependencies are to be enforced
        # upon subordinate group members.)
        #
        oLeaderTestSet = TestSetData().initFromDbWithId(self._oDb, oTestBoxData.idTestSetGangLeader);

        oLeaderBuild = BuildDataEx().initFromDbWithId(self._oDb, oLeaderTestSet.idBuild);
        oBuild = self._tryFindMatchingBuild(oLeaderBuild, oTestBoxData, self._oSchedGrpData.idBuildSrc);
        if oBuild is None:
            return None;

        oValidationKitBuild = None;
        if oLeaderTestSet.idBuildTestSuite is not None:
            oLeaderValidationKitBit = BuildDataEx().initFromDbWithId(self._oDb, oLeaderTestSet.idBuildTestSuite);
            oValidationKitBuild = self._tryFindMatchingBuild(oLeaderValidationKitBit, oTestBoxData,
                                                         self._oSchedGrpData.idBuildSrcTestSuite);

        #
        # Create a testset and update the state(s).
        #
        idTestSet = self._createTestSet(oTask, oTestEx, oTestBoxData, oBuild, oValidationKitBuild, tsNow);

        oTBStatusLogic = TestBoxStatusLogic(self._oDb);
        if oTask.cMissingGangMembers < 1:
            # The whole gang is there, move the task to the end of the queue
            # and update the status on the other gang members.
            self._moveTaskToEndOfQueue(oTask, oTestEx.cGangMembers, tsNow);
            dResponse = self.composeExecResponseWorker(idTestSet, oTestEx, oTestBoxData, oBuild, oValidationKitBuild, sBaseUrl);
            sTBState = TestBoxStatusData.ksTestBoxState_GangTesting;
            oTBStatusLogic.updateGangStatus(oTask.idTestSetGangLeader, sTBState, fCommit = False);
        else:
            # We're still missing some gang members, issue WAIT cmd.
            self._updateTask(oTask, tsNow if idTestSet == oTask.idTestSetGangLeader else None);
            dResponse = { constants.tbresp.ALL_PARAM_RESULT: constants.tbresp.CMD_WAIT, };
            sTBState = TestBoxStatusData.ksTestBoxState_GangGathering;

        oTBStatusLogic.updateState(oTestBoxData.idTestBox, sTBState, idTestSet, fCommit = False);
        return dResponse;


    def scheduleNewTaskWorker(self, oTestBoxData, tsNow, sBaseUrl):
        """
        Worker for schduling a new task.
        """

        #
        # Iterate the scheduler queue (fetch all to avoid having to concurrent
        # queries), trying out each task to see if the testbox can execute it.
        #
        dRejected = {}; # variations we've already checked out and rejected.
        self._oDb.execute('SELECT  *\n'
                          'FROM    SchedQueues\n'
                          'WHERE   idSchedGroup = %s\n'
                          '   AND  (   bmHourlySchedule IS NULL\n'
                          '         OR get_bit(bmHourlySchedule, %s) = 1 )\n'
                          'ORDER BY idItem ASC\n'
                          , (self._oSchedGrpData.idSchedGroup, utils.getLocalHourOfWeek()) );
        aaoRows = self._oDb.fetchAll();
        for aoRow in aaoRows:
            # Don't loop forever.
            if self.getElapsedSecs() >= config.g_kcSecMaxNewTask:
                break;

            # Unpack the data and check if we've rejected the testcasevar/group variation already (they repeat).
            oTask = SchedQueueData().initFromDbRow(aoRow);
            if config.g_kfSrvGlueDebugScheduler:
                self.dprint('** Considering: idItem=%s idGenTestCaseArgs=%s idTestGroup=%s Deps=%s last=%s cfg=%s\n'
                            % ( oTask.idItem, oTask.idGenTestCaseArgs, oTask.idTestGroup, oTask.aidTestGroupPreReqs,
                                oTask.tsLastScheduled, oTask.tsConfig,));

            sRejectNm = '%s:%s' % (oTask.idGenTestCaseArgs, oTask.idTestGroup,);
            if sRejectNm in dRejected:
                self.dprint('Duplicate, already rejected! (%s)' % (sRejectNm,));
                continue;
            dRejected[sRejectNm] = 1;

            # Fetch all the test case info (too much, but who cares right now).
            oTestEx = TestCaseArgsDataEx().initFromDbWithGenIdEx(self._oDb, oTask.idGenTestCaseArgs,
                                                                 tsConfigEff = oTask.tsConfig,
                                                                 tsRsrcEff = oTask.tsConfig);
            if config.g_kfSrvGlueDebugScheduler:
                self.dprint('TestCase "%s": %s %s' % (oTestEx.oTestCase.sName, oTestEx.oTestCase.sBaseCmd, oTestEx.sArgs,));

            # This shouldn't happen, but just in case it does...
            if oTestEx.oTestCase.fEnabled is not True:
                self.dprint('Testcase is not enabled!!');
                continue;

            # Check if the testbox properties matches the test.
            if not oTestEx.matchesTestBoxProps(oTestBoxData):
                self.dprint('Testbox mismatch!');
                continue;

            # Try schedule it.
            if oTask.idTestSetGangLeader is None or oTestEx.cGangMembers <= 1:
                dResponse = self._tryAsLeader(oTask, oTestEx, oTestBoxData, tsNow, sBaseUrl);
            elif oTask.cMissingGangMembers > 1:
                dResponse = self._tryAsGangMember(oTask, oTestEx, oTestBoxData, tsNow, sBaseUrl);
            else:
                dResponse = None; # Shouldn't happen!
            if dResponse is not None:
                self.dprint('Found a task! dResponse=%s' % (dResponse,));
                return dResponse;

        # Found no suitable task.
        return None;

    @staticmethod
    def _pickSchedGroup(oTestBoxDataEx, iWorkItem, dIgnoreSchedGroupIds):
        """
        Picks the next scheduling group for the given testbox.
        """
        if len(oTestBoxDataEx.aoInSchedGroups) == 1:
            oSchedGroup = oTestBoxDataEx.aoInSchedGroups[0].oSchedGroup;
            if    oSchedGroup.fEnabled \
              and oSchedGroup.idBuildSrc is not None \
              and oSchedGroup.idSchedGroup not in dIgnoreSchedGroupIds:
                return (oSchedGroup, 0);
            iWorkItem = 0;

        elif oTestBoxDataEx.aoInSchedGroups:
            # Construct priority table of currently enabled scheduling groups.
            aaoList1 = [];
            for oInGroup in oTestBoxDataEx.aoInSchedGroups:
                oSchedGroup = oInGroup.oSchedGroup;
                if oSchedGroup.fEnabled and oSchedGroup.idBuildSrc is not None:
                    iSchedPriority = oInGroup.iSchedPriority;
                    if iSchedPriority > 31:     # paranoia
                        iSchedPriority = 31;
                    elif iSchedPriority < 0:    # paranoia
                        iSchedPriority = 0;

                    for iSchedPriority in xrange(min(iSchedPriority, len(aaoList1))):
                        aaoList1[iSchedPriority].append(oSchedGroup);
                    while len(aaoList1) <= iSchedPriority:
                        aaoList1.append([oSchedGroup,]);

            # Flatten it into a single list, mixing the priorities a little so it doesn't
            # take forever before low priority stuff is executed.
            aoFlat = [];
            iLo    = 0;
            iHi    = len(aaoList1) - 1;
            while iHi >= iLo:
                aoFlat += aaoList1[iHi];
                if iLo < iHi:
                    aoFlat += aaoList1[iLo];
                iLo += 1;
                iHi -= 1;

            # Pick the next one.
            cLeft = len(aoFlat);
            while cLeft > 0:
                cLeft     -= 1;
                iWorkItem += 1;
                if iWorkItem >= len(aoFlat) or iWorkItem < 0:
                    iWorkItem = 0;
                if aoFlat[iWorkItem].idSchedGroup not in dIgnoreSchedGroupIds:
                    return (aoFlat[iWorkItem], iWorkItem);
        else:
            iWorkItem = 0;

        # No active group.
        return (None, iWorkItem);

    @staticmethod
    def scheduleNewTask(oDb, oTestBoxData, iWorkItem, sBaseUrl, iVerbosity = 0):
        # type: (TMDatabaseConnection, TestBoxData, int, str, int) -> None
        """
        Schedules a new task for a testbox.
        """
        oTBStatusLogic = TestBoxStatusLogic(oDb);

        try:
            #
            # To avoid concurrency issues in SchedQueues we lock all the rows
            # related to our scheduling queue.  Also, since this is a very
            # expensive operation we lock the testbox status row to fend of
            # repeated retires by faulty testbox scripts.
            #
            tsSecStart = utils.timestampSecond();
            oDb.rollback();
            oDb.begin();
            oDb.execute('SELECT idTestBox FROM TestBoxStatuses WHERE idTestBox = %s FOR UPDATE NOWAIT'
                        % (oTestBoxData.idTestBox,));
            oDb.execute('SELECT SchedQueues.idSchedGroup\n'
                        '  FROM SchedQueues, TestBoxesInSchedGroups\n'
                        'WHERE  TestBoxesInSchedGroups.idTestBox    = %s\n'
                        '   AND TestBoxesInSchedGroups.tsExpire     = \'infinity\'::TIMESTAMP\n'
                        '   AND TestBoxesInSchedGroups.idSchedGroup = SchedQueues.idSchedGroup\n'
                        ' FOR UPDATE'
                        % (oTestBoxData.idTestBox,));

            # We need the current timestamp.
            tsNow = oDb.getCurrentTimestamp();

            # Re-read the testbox data with scheduling group relations.
            oTestBoxDataEx = TestBoxDataEx().initFromDbWithId(oDb, oTestBoxData.idTestBox, tsNow);
            if    oTestBoxDataEx.fEnabled \
              and oTestBoxDataEx.idGenTestBox == oTestBoxData.idGenTestBox:

                # We may have to skip scheduling groups that are out of work (e.g. 'No build').
                iInitialWorkItem     = iWorkItem;
                dIgnoreSchedGroupIds = {};
                while True:
                    # Now, pick the scheduling group.
                    (oSchedGroup, iWorkItem) = SchedulerBase._pickSchedGroup(oTestBoxDataEx, iWorkItem, dIgnoreSchedGroupIds);
                    if oSchedGroup is None:
                        break;
                    assert oSchedGroup.fEnabled and oSchedGroup.idBuildSrc is not None;

                    # Instantiate the specified scheduler and let it do the rest.
                    oScheduler = SchedulerBase._instantiate(oDb, oSchedGroup, iVerbosity, tsSecStart);
                    dResponse = oScheduler.scheduleNewTaskWorker(oTestBoxDataEx, tsNow, sBaseUrl);
                    if dResponse is not None:
                        oTBStatusLogic.updateWorkItem(oTestBoxDataEx.idTestBox, iWorkItem);
                        oDb.commit();
                        return dResponse;

                    # Check out the next work item?
                    if oScheduler.getElapsedSecs() > config.g_kcSecMaxNewTask:
                        break;
                    dIgnoreSchedGroupIds[oSchedGroup.idSchedGroup] = oSchedGroup;

                # No luck, but best if we update the work item if we've made progress.
                # Note! In case of a config.g_kcSecMaxNewTask timeout, this may accidentally skip
                #       a work item with actually work to do.  But that's a small price to pay.
                if iWorkItem != iInitialWorkItem:
                    oTBStatusLogic.updateWorkItem(oTestBoxDataEx.idTestBox, iWorkItem);
                    oDb.commit();
                    return None;
        except:
            oDb.rollback();
            raise;

        # Not enabled, rollback and return no task.
        oDb.rollback();
        return None;

    @staticmethod
    def tryCancelGangGathering(oDb, oStatusData):
        """
        Try canceling a gang gathering.

        Returns True if successfully cancelled.
        Returns False if not (someone raced us to the SchedQueue table).

        Note! oStatusData is re-initialized.
        """
        assert oStatusData.enmState == TestBoxStatusData.ksTestBoxState_GangGathering;
        try:
            #
            # Lock the tables we're updating so we don't run into concurrency
            # issues (we're racing both scheduleNewTask and other callers of
            # this method).
            #
            oDb.rollback();
            oDb.begin();
            oDb.execute('LOCK TABLE TestBoxStatuses, SchedQueues IN EXCLUSIVE MODE');

            #
            # Re-read the testbox data and check that we're still in the same state.
            #
            oStatusData.initFromDbWithId(oDb, oStatusData.idTestBox);
            if oStatusData.enmState == TestBoxStatusData.ksTestBoxState_GangGathering:
                #
                # Get the leader thru the test set and change the state of the whole gang.
                #
                oTestSetData = TestSetData().initFromDbWithId(oDb, oStatusData.idTestSet);

                oTBStatusLogic = TestBoxStatusLogic(oDb);
                oTBStatusLogic.updateGangStatus(oTestSetData.idTestSetGangLeader,
                                                TestBoxStatusData.ksTestBoxState_GangGatheringTimedOut,
                                                fCommit = False);

                #
                # Move the scheduling queue item to the end.
                #
                oDb.execute('SELECT *\n'
                            'FROM   SchedQueues\n'
                            'WHERE  idTestSetGangLeader = %s\n'
                            , (oTestSetData.idTestSetGangLeader,) );
                oTask = SchedQueueData().initFromDbRow(oDb.fetchOne());
                oTestEx = TestCaseArgsDataEx().initFromDbWithGenIdEx(oDb, oTask.idGenTestCaseArgs,
                                                                     tsConfigEff = oTask.tsConfig,
                                                                     tsRsrcEff = oTask.tsConfig);
                oDb.execute('UPDATE SchedQueues\n'
                            '   SET idItem = NEXTVAL(\'SchedQueueItemIdSeq\'),\n'
                            '       idTestSetGangLeader = NULL,\n'
                            '       cMissingGangMembers = %s\n'
                            'WHERE  idItem = %s\n'
                            , (oTestEx.cGangMembers, oTask.idItem,) );

                oDb.commit();
                return True;

            if oStatusData.enmState == TestBoxStatusData.ksTestBoxState_GangGatheringTimedOut:
                oDb.rollback();
                return True;
        except:
            oDb.rollback();
            raise;

        # Not enabled, rollback and return no task.
        oDb.rollback();
        return False;


#
# Unit testing.
#

# pylint: disable=missing-docstring
class SchedQueueDataTestCase(ModelDataBaseTestCase):
    def setUp(self):
        self.aoSamples = [SchedQueueData(),];

if __name__ == '__main__':
    unittest.main();
    # not reached.

