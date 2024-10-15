# -*- coding: utf-8 -*-
# $Id: report.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager - Report models.
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


# Standard Python imports.
import sys;

# Validation Kit imports.
from testmanager.core.base          import ModelLogicBase, TMExceptionBase;
from testmanager.core.build         import BuildCategoryData;
from testmanager.core.dbobjcache    import DatabaseObjCache;
from testmanager.core.failurereason import FailureReasonLogic;
from testmanager.core.testbox       import TestBoxLogic, TestBoxData;
from testmanager.core.testcase      import TestCaseLogic;
from testmanager.core.testcaseargs  import TestCaseArgsLogic;
from testmanager.core.testresults   import TestResultLogic, TestResultFilter;
from common                         import constants;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    xrange = range; # pylint: disable=redefined-builtin,invalid-name



class ReportFilter(TestResultFilter):
    """
    Same as TestResultFilter for now.
    """

    def __init__(self):
        TestResultFilter.__init__(self);



class ReportModelBase(ModelLogicBase): # pylint: disable=too-few-public-methods
    """
    Something all report logic(/miner) classes inherit from.
    """

    ## @name Report subjects
    ## @{
    ksSubEverything       = 'Everything';
    ksSubSchedGroup       = 'SchedGroup';
    ksSubTestGroup        = 'TestGroup';
    ksSubTestCase         = 'TestCase';
    ksSubTestCaseArgs     = 'TestCaseArgs';
    ksSubTestBox          = 'TestBox';
    ksSubBuild            = 'Build';
    ## @}
    kasSubjects           = [ ksSubEverything, ksSubSchedGroup, ksSubTestGroup, ksSubTestCase, ksSubTestBox, ksSubBuild, ];


    ## @name TestStatus_T
    # @{
    ksTestStatus_Running    = 'running';
    ksTestStatus_Success    = 'success';
    ksTestStatus_Skipped    = 'skipped';
    ksTestStatus_BadTestBox = 'bad-testbox';
    ksTestStatus_Aborted    = 'aborted';
    ksTestStatus_Failure    = 'failure';
    ksTestStatus_TimedOut   = 'timed-out';
    ksTestStatus_Rebooted   = 'rebooted';
    ## @}


    def __init__(self, oDb, tsNow, cPeriods, cHoursPerPeriod, sSubject, aidSubjects, oFilter):
        ModelLogicBase.__init__(self, oDb);
        # Public so the report generator can easily access them.
        self.tsNow           = tsNow;               # (Can be None.)
        self.__tsNowDateTime = None;
        self.cPeriods        = cPeriods;
        self.cHoursPerPeriod = cHoursPerPeriod;
        self.sSubject        = sSubject;
        self.aidSubjects     = aidSubjects;
        self.oFilter         = oFilter;
        if self.oFilter is None:
            class DummyFilter(object):
                """ Dummy """
                def getTableJoins(self, sExtraIndent = '', iOmit = -1, dOmitTables = None):
                    """ Dummy """
                    _ = sExtraIndent; _ = iOmit; _ = dOmitTables; # pylint: disable=redefined-variable-type
                    return '';
                def getWhereConditions(self, sExtraIndent = '', iOmit = -1):
                    """ Dummy """
                    _ = sExtraIndent; _ = iOmit; # pylint: disable=redefined-variable-type
                    return '';
                def isJoiningWithTable(self, sTable):
                    """ Dummy """;
                    _ = sTable;
                    return False;
            self.oFilter = DummyFilter();

    def getExtraSubjectTables(self):
        """
        Returns a list of additional tables needed by the subject.
        """
        return [];

    def getExtraSubjectWhereExpr(self):
        """
        Returns additional WHERE expression relating to the report subject.  It starts
        with an AND so that it can simply be appended to the WHERE clause.
        """
        if self.sSubject == self.ksSubEverything:
            return '';

        if self.sSubject == self.ksSubSchedGroup:
            sWhere = '     AND TestSets.idSchedGroup';
        elif self.sSubject == self.ksSubTestGroup:
            sWhere = '     AND TestSets.idTestGroup';
        elif self.sSubject == self.ksSubTestCase:
            sWhere = '     AND TestSets.idTestCase';
        elif self.sSubject == self.ksSubTestCaseArgs:
            sWhere = '     AND TestSets.idTestCaseArgs';
        elif self.sSubject == self.ksSubTestBox:
            sWhere = '     AND TestSets.idTestBox';
        elif self.sSubject == self.ksSubBuild:
            sWhere = '     AND TestSets.idBuild';
        else:
            raise TMExceptionBase(self.sSubject);

        if len(self.aidSubjects) == 1:
            sWhere += self._oDb.formatBindArgs(' = %s\n', (self.aidSubjects[0],));
        else:
            assert self.aidSubjects;
            sWhere += self._oDb.formatBindArgs(' IN (%s', (self.aidSubjects[0],));
            for i in range(1, len(self.aidSubjects)):
                sWhere += self._oDb.formatBindArgs(', %s', (self.aidSubjects[i],));
            sWhere += ')\n';

        return sWhere;

    def getNowAsDateTime(self):
        """ Returns a datetime instance corresponding to tsNow. """
        if self.__tsNowDateTime is None:
            if self.tsNow is None:
                self.__tsNowDateTime = self._oDb.getCurrentTimestamp();
            else:
                self._oDb.execute('SELECT %s::TIMESTAMP WITH TIME ZONE', (self.tsNow,));
                self.__tsNowDateTime = self._oDb.fetchOne()[0];
        return self.__tsNowDateTime;

    def getPeriodStart(self, iPeriod):
        """ Gets the python timestamp for the start of the given period. """
        from datetime import timedelta;
        cHoursStart = (self.cPeriods - iPeriod    ) * self.cHoursPerPeriod;
        return self.getNowAsDateTime() - timedelta(hours = cHoursStart);

    def getPeriodEnd(self, iPeriod):
        """ Gets the python timestamp for the end of the given period. """
        from datetime import timedelta;
        cHoursEnd   = (self.cPeriods - iPeriod - 1) * self.cHoursPerPeriod;
        return self.getNowAsDateTime() - timedelta(hours = cHoursEnd);

    def getExtraWhereExprForPeriod(self, iPeriod):
        """
        Returns additional WHERE expression for getting test sets for the
        specified period.  It starts with an AND so that it can simply be
        appended to the WHERE clause.
        """
        if self.tsNow is None:
            sNow = 'CURRENT_TIMESTAMP';
        else:
            sNow = self._oDb.formatBindArgs('%s::TIMESTAMP', (self.tsNow,));

        cHoursStart = (self.cPeriods - iPeriod    ) * self.cHoursPerPeriod;
        cHoursEnd   = (self.cPeriods - iPeriod - 1) * self.cHoursPerPeriod;
        if cHoursEnd == 0:
            return '     AND TestSets.tsDone >= (%s - interval \'%u hours\')\n' \
                   '     AND TestSets.tsDone <  %s\n' \
                 % (sNow, cHoursStart, sNow);
        return '     AND TestSets.tsDone >= (%s - interval \'%u hours\')\n' \
               '     AND TestSets.tsDone <  (%s - interval \'%u hours\')\n' \
             % (sNow, cHoursStart, sNow, cHoursEnd);

    def getPeriodDesc(self, iPeriod):
        """
        Returns the period description, usually for graph data.
        """
        if iPeriod == 0:
            return 'now' if self.tsNow is None else 'then';
        sTerm = 'ago' if self.tsNow is None else 'earlier';
        if self.cHoursPerPeriod == 24:
            return '%dd %s' % (iPeriod, sTerm, );
        if (iPeriod * self.cHoursPerPeriod) % 24 == 0:
            return '%dd %s' % (iPeriod * self.cHoursPerPeriod / 24, sTerm, );
        return '%dh %s' % (iPeriod * self.cHoursPerPeriod, sTerm);

    def getStraightPeriodDesc(self, iPeriod):
        """
        Returns the period description, usually for graph data.
        """
        iWickedPeriod = self.cPeriods - iPeriod - 1;
        return self.getPeriodDesc(iWickedPeriod);


#
# Data structures produced and returned by the ReportLazyModel.
#

class ReportTransientBase(object):
    """ Details on the test where a problem was first/last seen.  """
    def __init__(self, idBuild, iRevision, sRepository, idTestSet, idTestResult, tsDone, # pylint: disable=too-many-arguments
                 iPeriod, fEnter, idSubject, oSubject):
        self.idBuild            = idBuild;      # Build ID.
        self.iRevision          = iRevision;    # SVN revision for build.
        self.sRepository        = sRepository;  # SVN repository for build.
        self.idTestSet          = idTestSet;    # Test set.
        self.idTestResult       = idTestResult; # Test result.
        self.tsDone             = tsDone;       # When the test set was done.
        self.iPeriod            = iPeriod;      # Data set period.
        self.fEnter             = fEnter;       # True if enter event, False if leave event.
        self.idSubject          = idSubject;
        self.oSubject           = oSubject;

class ReportFailureReasonTransient(ReportTransientBase):
    """ Details on the test where a failure reason was first/last seen.  """
    def __init__(self, idBuild, iRevision, sRepository, idTestSet, idTestResult, tsDone,  # pylint: disable=too-many-arguments
                 iPeriod, fEnter, oReason):
        ReportTransientBase.__init__(self, idBuild, iRevision, sRepository, idTestSet, idTestResult, tsDone, iPeriod, fEnter,
                                     oReason.idFailureReason, oReason);
        self.oReason            = oReason;      # FailureReasonDataEx


class ReportHitRowBase(object):
    """ A row in a period. """
    def __init__(self, idSubject, oSubject, cHits, tsMin = None, tsMax = None):
        self.idSubject          = idSubject;
        self.oSubject           = oSubject;
        self.cHits              = cHits;
        self.tsMin              = tsMin;
        self.tsMax              = tsMax;

class ReportHitRowWithTotalBase(ReportHitRowBase):
    """ A row in a period. """
    def __init__(self, idSubject, oSubject, cHits, cTotal, tsMin = None, tsMax = None):
        ReportHitRowBase.__init__(self, idSubject, oSubject, cHits, tsMin, tsMax)
        self.cTotal             = cTotal;
        self.uPct               = cHits * 100 / cTotal;

class ReportFailureReasonRow(ReportHitRowBase):
    """ The account of one failure reason for a period. """
    def __init__(self, aoRow, oReason):
        ReportHitRowBase.__init__(self, aoRow[0], oReason, aoRow[1], aoRow[2], aoRow[3]);
        self.idFailureReason    = aoRow[0];
        self.oReason            = oReason;      # FailureReasonDataEx


class ReportPeriodBase(object):
    """ A period in ReportFailureReasonSet. """
    def __init__(self, oSet, iPeriod, sDesc, tsFrom, tsTo):
        self.oSet               = oSet          # Reference to the parent ReportSetBase derived object.
        self.iPeriod            = iPeriod;      # Period number in the set.
        self.sDesc              = sDesc;        # Short period description.
        self.tsStart            = tsFrom;       # Start of the period.
        self.tsEnd              = tsTo;         # End of the period (exclusive).
        self.tsMin              = tsTo;         # The earlierst hit of the period (only valid for cHits > 0).
        self.tsMax              = tsFrom;       # The latest hit of the period (only valid for cHits > 0).
        self.aoRows             = [];           # Rows in order the database returned them (ReportHitRowBase descendant).
        self.dRowsById          = {};           # Same as aoRows but indexed by object ID (see ReportSetBase::sIdAttr).
        self.dFirst             = {};           # The subjects seen for the first time - data object, keyed by ID.
        self.dLast              = {};           # The subjects seen for the last  time - data object, keyed by ID.
        self.cHits              = 0;            # Total number of hits in this period.
        self.cMaxHits           = 0;            # Max hits in a row.
        self.cMinHits           = 99999999;     # Min hits in a row (only valid for cHits > 0).

    def appendRow(self, oRow, idRow, oData):
        """ Adds a row. """
        assert isinstance(oRow, ReportHitRowBase);
        self.aoRows.append(oRow);
        self.dRowsById[idRow] = oRow;
        if idRow not in self.oSet.dSubjects:
            self.oSet.dSubjects[idRow] = oData;
        self._doStatsForRow(oRow, idRow, oData);

    def _doStatsForRow(self, oRow, idRow, oData):
        """ Does the statistics for a row. Helper for appendRow as well as helpRecalcStats. """
        if oRow.tsMin is not None and oRow.tsMin < self.tsMin:
            self.tsMin = oRow.tsMin;
        if oRow.tsMax is not None and oRow.tsMax < self.tsMax:
            self.tsMax = oRow.tsMax;

        self.cHits += oRow.cHits;
        if oRow.cHits > self.cMaxHits:
            self.cMaxHits = oRow.cHits;
        if oRow.cHits < self.cMinHits:
            self.cMinHits = oRow.cHits;

        if idRow in self.oSet.dcHitsPerId:
            self.oSet.dcHitsPerId[idRow] += oRow.cHits;
        else:
            self.oSet.dcHitsPerId[idRow]  = oRow.cHits;

        if oRow.cHits > 0:
            if idRow not in self.oSet.diPeriodFirst:
                self.dFirst[idRow]              = oData;
                self.oSet.diPeriodFirst[idRow]  = self.iPeriod;
            self.oSet.diPeriodLast[idRow]       = self.iPeriod;

    def helperSetRecalcStats(self):
        """ Recalc the statistics (do resetStats first on set). """
        for idRow, oRow in self.dRowsById.items():
            self._doStatsForRow(oRow, idRow, self.oSet.dSubjects[idRow]);

    def helperSetResetStats(self):
        """ Resets the statistics. """
        self.tsMin      = self.tsEnd;
        self.tsMax      = self.tsStart;
        self.cHits      = 0;
        self.cMaxHits   = 0;
        self.cMinHits   = 99999999;
        self.dFirst     = {};
        self.dLast      = {};

    def helperSetDeleteKeyFromSet(self, idKey):
        """ Helper for ReportPeriodSetBase::deleteKey """
        if idKey in self.dRowsById:
            oRow = self.dRowsById[idKey];
            self.aoRows.remove(oRow);
            del self.dRowsById[idKey]
            self.cHits -= oRow.cHits;
            if idKey in self.dFirst:
                del self.dFirst[idKey];
            if idKey in self.dLast:
                del self.dLast[idKey];

class ReportPeriodWithTotalBase(ReportPeriodBase):
    """ In addition to the cHits, we also have a total to relate it too. """
    def __init__(self, oSet, iPeriod, sDesc, tsFrom, tsTo):
        ReportPeriodBase.__init__(self, oSet, iPeriod, sDesc, tsFrom, tsTo);
        self.cTotal             = 0;
        self.cMaxTotal          = 0;
        self.cMinTotal          = 99999999;
        self.uMaxPct            = 0;            # Max percentage in a row (100 = 100%).

    def _doStatsForRow(self, oRow, idRow, oData):
        assert isinstance(oRow, ReportHitRowWithTotalBase);
        super(ReportPeriodWithTotalBase, self)._doStatsForRow(oRow, idRow, oData);
        self.cTotal += oRow.cTotal;
        if oRow.cTotal > self.cMaxTotal:
            self.cMaxTotal = oRow.cTotal;
        if oRow.cTotal < self.cMinTotal:
            self.cMinTotal = oRow.cTotal;

        if oRow.uPct > self.uMaxPct:
            self.uMaxPct = oRow.uPct;

        if idRow in self.oSet.dcTotalPerId:
            self.oSet.dcTotalPerId[idRow] += oRow.cTotal;
        else:
            self.oSet.dcTotalPerId[idRow]  = oRow.cTotal;

    def helperSetResetStats(self):
        super(ReportPeriodWithTotalBase, self).helperSetResetStats();
        self.cTotal             = 0;
        self.cMaxTotal          = 0;
        self.cMinTotal          = 99999999;
        self.uMaxPct            = 0;

class ReportFailureReasonPeriod(ReportPeriodBase):
    """ A period in ReportFailureReasonSet. """
    def __init__(self, oSet, iPeriod, sDesc, tsFrom, tsTo):
        ReportPeriodBase.__init__(self, oSet, iPeriod, sDesc, tsFrom, tsTo);
        self.cWithoutReason     = 0;            # Number of failed test sets without any assigned reason.



class ReportPeriodSetBase(object):
    """ Period data set base class. """
    def __init__(self, sIdAttr):
        self.sIdAttr            = sIdAttr;      # The name of the key attribute.  Mainly for documentation purposes.
        self.aoPeriods          = [];           # Periods (ReportPeriodBase descendant) in ascending order (time wise).
        self.dSubjects          = {};           # The subject data objects, keyed by the subject ID.
        self.dcHitsPerId        = {};           # Sum hits per subject ID (key).
        self.cHits              = 0;            # Sum number of hits in all periods and all reasons.
        self.cMaxHits           = 0;            # Max hits in a row.
        self.cMinHits           = 99999999;     # Min hits in a row.
        self.cMaxRows           = 0;            # Max number of rows in a period.
        self.cMinRows           = 99999999;     # Min number of rows in a period.
        self.diPeriodFirst      = {};           # The period number a reason was first seen (keyed by subject ID).
        self.diPeriodLast       = {};           # The period number a reason was last seen (keyed by subject ID).
        self.aoEnterInfo        = [];           # Array of ReportTransientBase children order by iRevision.  Excludes
                                                # the first period of course.  (Child class populates this.)
        self.aoLeaveInfo        = [];           # Array of ReportTransientBase children order in descending order by
                                                # iRevision. Excludes the last priod.  (Child class populates this.)

    def appendPeriod(self, oPeriod):
        """ Appends a period to the set. """
        assert isinstance(oPeriod, ReportPeriodBase);
        self.aoPeriods.append(oPeriod);
        self._doStatsForPeriod(oPeriod);

    def _doStatsForPeriod(self, oPeriod):
        """ Worker for appendPeriod and recalcStats. """
        self.cHits += oPeriod.cHits;
        if oPeriod.cMaxHits > self.cMaxHits:
            self.cMaxHits = oPeriod.cMaxHits;
        if oPeriod.cMinHits < self.cMinHits:
            self.cMinHits = oPeriod.cMinHits;

        if len(oPeriod.aoRows) > self.cMaxRows:
            self.cMaxRows = len(oPeriod.aoRows);
        if len(oPeriod.aoRows) < self.cMinRows:
            self.cMinRows = len(oPeriod.aoRows);

    def recalcStats(self):
        """ Recalculates the statistics. ASSUMES finalizePass1 hasn't been done yet. """
        self.cHits          = 0;
        self.cMaxHits       = 0;
        self.cMinHits       = 99999999;
        self.cMaxRows       = 0;
        self.cMinRows       = 99999999;
        self.diPeriodFirst  = {};
        self.diPeriodLast   = {};
        self.dcHitsPerId    = {};
        for oPeriod in self.aoPeriods:
            oPeriod.helperSetResetStats();

        for oPeriod in self.aoPeriods:
            oPeriod.helperSetRecalcStats();
            self._doStatsForPeriod(oPeriod);

    def deleteKey(self, idKey):
        """ Deletes a key from the set.  May leave cMaxHits and cMinHits with outdated values. """
        self.cHits -= self.dcHitsPerId[idKey];
        del self.dcHitsPerId[idKey];
        if idKey in self.diPeriodFirst:
            del self.diPeriodFirst[idKey];
        if idKey in self.diPeriodLast:
            del self.diPeriodLast[idKey];
        if idKey in self.aoEnterInfo:
            del self.aoEnterInfo[idKey];
        if idKey in self.aoLeaveInfo:
            del self.aoLeaveInfo[idKey];
        del self.dSubjects[idKey];
        for oPeriod in self.aoPeriods:
            oPeriod.helperSetDeleteKeyFromSet(idKey);

    def pruneRowsWithZeroSumHits(self):
        """ Discards rows with zero sum hits across all periods.  Works around lazy selects counting both totals and hits. """
        cDeleted = 0;
        aidKeys  = list(self.dcHitsPerId);
        for idKey in aidKeys:
            if self.dcHitsPerId[idKey] == 0:
                self.deleteKey(idKey);
                cDeleted += 1;
        if cDeleted > 0:
            self.recalcStats();
        return cDeleted;

    def finalizePass1(self):
        """ Finished all but aoEnterInfo and aoLeaveInfo. """
        # All we need to do here is to populate the dLast members.
        for idKey, iPeriod in self.diPeriodLast.items():
            self.aoPeriods[iPeriod].dLast[idKey] = self.dSubjects[idKey];
        return self;

    def finalizePass2(self):
        """ Called after aoEnterInfo and aoLeaveInfo has been populated to sort them. """
        self.aoEnterInfo = sorted(self.aoEnterInfo, key = lambda oTrans: oTrans.iRevision);
        self.aoLeaveInfo = sorted(self.aoLeaveInfo, key = lambda oTrans: oTrans.iRevision, reverse = True);
        return self;

class ReportPeriodSetWithTotalBase(ReportPeriodSetBase):
    """ In addition to the cHits, we also have a total to relate it too. """
    def __init__(self, sIdAttr):
        ReportPeriodSetBase.__init__(self, sIdAttr);
        self.dcTotalPerId       = {};           # Sum total per subject ID (key).
        self.cTotal             = 0;            # Sum number of total in all periods and all reasons.
        self.cMaxTotal          = 0;            # Max total in a row.
        self.cMinTotal          = 0;            # Min total in a row.
        self.uMaxPct            = 0;            # Max percentage in a row (100 = 100%).

    def _doStatsForPeriod(self, oPeriod):
        assert isinstance(oPeriod, ReportPeriodWithTotalBase);
        super(ReportPeriodSetWithTotalBase, self)._doStatsForPeriod(oPeriod);
        self.cTotal += oPeriod.cTotal;
        if oPeriod.cMaxTotal > self.cMaxTotal:
            self.cMaxTotal = oPeriod.cMaxTotal;
        if oPeriod.cMinTotal < self.cMinTotal:
            self.cMinTotal = oPeriod.cMinTotal;

        if oPeriod.uMaxPct > self.uMaxPct:
            self.uMaxPct = oPeriod.uMaxPct;

    def recalcStats(self):
        self.dcTotalPerId       = {};
        self.cTotal             = 0;
        self.cMaxTotal          = 0;
        self.cMinTotal          = 0;
        self.uMaxPct            = 0;
        super(ReportPeriodSetWithTotalBase, self).recalcStats();

    def deleteKey(self, idKey):
        self.cTotal -= self.dcTotalPerId[idKey];
        del self.dcTotalPerId[idKey];
        super(ReportPeriodSetWithTotalBase, self).deleteKey(idKey);

class ReportFailureReasonSet(ReportPeriodSetBase):
    """ What ReportLazyModel.getFailureReasons returns. """
    def __init__(self):
        ReportPeriodSetBase.__init__(self, 'idFailureReason');



class ReportLazyModel(ReportModelBase): # pylint: disable=too-few-public-methods
    """
    The 'lazy bird' report model class.

    We may want to have several classes, maybe one for each report even. But,
    I'm thinking that's a bit overkill so we'll start with this and split it
    if/when it becomes necessary.
    """

    kdsStatusSimplificationMap = {
        ReportModelBase.ksTestStatus_Running:       ReportModelBase.ksTestStatus_Running,
        ReportModelBase.ksTestStatus_Success:       ReportModelBase.ksTestStatus_Success,
        ReportModelBase.ksTestStatus_Skipped:       ReportModelBase.ksTestStatus_Skipped,
        ReportModelBase.ksTestStatus_BadTestBox:    ReportModelBase.ksTestStatus_Skipped,
        ReportModelBase.ksTestStatus_Aborted:       ReportModelBase.ksTestStatus_Skipped,
        ReportModelBase.ksTestStatus_Failure:       ReportModelBase.ksTestStatus_Failure,
        ReportModelBase.ksTestStatus_TimedOut:      ReportModelBase.ksTestStatus_Failure,
        ReportModelBase.ksTestStatus_Rebooted:      ReportModelBase.ksTestStatus_Failure,
    };

    def getSuccessRates(self):
        """
        Gets the success rates of the subject in the specified period.

        Returns an array of data per period (0 is the oldes, self.cPeriods-1 is
        the latest) where each entry is a status (TestStatus_T) dictionary with
        the number of occurences of each final status (i.e. not running).
        """

        sBaseQuery  = 'SELECT   TestSets.enmStatus, COUNT(TestSets.idTestSet)\n' \
                      'FROM     TestSets\n' \
                    + self.oFilter.getTableJoins();
        for sTable in self.getExtraSubjectTables():
            sBaseQuery = sBaseQuery[:-1] + ',\n         ' + sTable + '\n';
        sBaseQuery += 'WHERE    enmStatus <> \'running\'\n' \
                    + self.oFilter.getWhereConditions() \
                    + self.getExtraSubjectWhereExpr();

        adPeriods = [];
        for iPeriod in xrange(self.cPeriods):
            self._oDb.execute(sBaseQuery + self.getExtraWhereExprForPeriod(iPeriod) + 'GROUP BY enmStatus\n');

            dRet = \
            {
                self.ksTestStatus_Skipped: 0,
                self.ksTestStatus_Failure: 0,
                self.ksTestStatus_Success: 0,
            };

            for aoRow in self._oDb.fetchAll():
                sKey = self.kdsStatusSimplificationMap[aoRow[0]]
                if sKey in dRet:
                    dRet[sKey] += aoRow[1];
                else:
                    dRet[sKey]  = aoRow[1];

            assert len(dRet) == 3;

            adPeriods.insert(0, dRet);

        return adPeriods;


    def getFailureReasons(self):
        """
        Gets the failure reasons of the subject in the specified period.

        Returns a ReportFailureReasonSet instance.
        """

        oFailureReasonLogic = FailureReasonLogic(self._oDb);

        #
        # Create a temporary table
        #
        sTsNow   = 'CURRENT_TIMESTAMP' if self.tsNow is None else self._oDb.formatBindArgs('%s::TIMESTAMP', (self.tsNow,));
        sTsFirst = '(%s - interval \'%s hours\')' \
                 % (sTsNow, self.cHoursPerPeriod * self.cPeriods,);
        sQuery   = 'CREATE TEMPORARY TABLE TmpReasons ON COMMIT DROP AS\n' \
                   'SELECT   TestResultFailures.idFailureReason AS idFailureReason,\n' \
                   '         TestResultFailures.idTestResult    AS idTestResult,\n' \
                   '         TestSets.idTestSet                 AS idTestSet,\n' \
                   '         TestSets.tsDone                    AS tsDone,\n' \
                   '         TestSets.tsCreated                 AS tsCreated,\n' \
                   '         TestSets.idBuild                   AS idBuild\n' \
                   'FROM     TestResultFailures,\n' \
                   '         TestResults,\n' \
                   '         TestSets\n' \
                   + self.oFilter.getTableJoins(dOmitTables = {'TestResults': True, 'TestResultFailures': True});
        for sTable in self.getExtraSubjectTables():
            if sTable not in [ 'TestResults', 'TestResultFailures' ] and not self.oFilter.isJoiningWithTable(sTable):
                sQuery = sQuery[:-1] + ',\n         ' + sTable + '\n';
        sQuery  += 'WHERE    TestResultFailures.idTestResult = TestResults.idTestResult\n' \
                   '     AND TestResultFailures.tsExpire     = \'infinity\'::TIMESTAMP\n' \
                   '     AND TestResultFailures.tsEffective >= ' + sTsFirst + '\n' \
                   '     AND TestResults.enmStatus          <> \'running\'\n' \
                   '     AND TestResults.enmStatus          <> \'success\'\n' \
                   '     AND TestResults.tsCreated          >= ' + sTsFirst + '\n' \
                   '     AND TestResults.tsCreated          <  ' + sTsNow + '\n' \
                   '     AND TestResults.idTestSet           = TestSets.idTestSet\n' \
                   '     AND TestSets.tsDone                >= ' + sTsFirst + '\n' \
                   '     AND TestSets.tsDone                <  ' + sTsNow + '\n' \
                   + self.oFilter.getWhereConditions() \
                   + self.getExtraSubjectWhereExpr();
        self._oDb.execute(sQuery);
        self._oDb.execute('SELECT idFailureReason FROM TmpReasons;');

        #
        # Retrieve the period results.
        #
        oSet = ReportFailureReasonSet();
        for iPeriod in xrange(self.cPeriods):
            self._oDb.execute('SELECT   idFailureReason,\n'
                              '         COUNT(idTestResult),\n'
                              '         MIN(tsDone),\n'
                              '         MAX(tsDone)\n'
                              'FROM     TmpReasons\n'
                              'WHERE    TRUE\n'
                              + self.getExtraWhereExprForPeriod(iPeriod).replace('TestSets.', '') +
                              'GROUP BY idFailureReason\n');
            aaoRows = self._oDb.fetchAll()

            oPeriod = ReportFailureReasonPeriod(oSet, iPeriod, self.getStraightPeriodDesc(iPeriod),
                                                self.getPeriodStart(iPeriod), self.getPeriodEnd(iPeriod));

            for aoRow in aaoRows:
                oReason = oFailureReasonLogic.cachedLookup(aoRow[0]);
                oPeriodRow = ReportFailureReasonRow(aoRow, oReason);
                oPeriod.appendRow(oPeriodRow, oReason.idFailureReason, oReason);

            # Count how many test sets we've got without any reason associated with them.
            self._oDb.execute('SELECT   COUNT(TestSets.idTestSet)\n'
                              'FROM     TestSets\n'
                              '         LEFT OUTER JOIN TestResultFailures\n'
                              '                      ON     TestSets.idTestSet             = TestResultFailures.idTestSet\n'
                              '                         AND TestResultFailures.tsEffective = \'infinity\'::TIMESTAMP\n'
                              'WHERE    TestSets.enmStatus          <> \'running\'\n'
                              '     AND TestSets.enmStatus          <> \'success\'\n'
                              + self.getExtraWhereExprForPeriod(iPeriod) +
                              '     AND TestResultFailures.idTestSet IS NULL\n');
            oPeriod.cWithoutReason = self._oDb.fetchOne()[0];

            oSet.appendPeriod(oPeriod);


        #
        # For reasons entering after the first period, look up the build and
        # test set it first occured with.
        #
        oSet.finalizePass1();

        for iPeriod in xrange(1, self.cPeriods):
            oPeriod = oSet.aoPeriods[iPeriod];
            for oReason in oPeriod.dFirst.values():
                oSet.aoEnterInfo.append(self._getEdgeFailureReasonOccurence(oReason, iPeriod, fEnter = True));

        # Ditto for reasons leaving before the last.
        for iPeriod in xrange(self.cPeriods - 1):
            oPeriod = oSet.aoPeriods[iPeriod];
            for oReason in oPeriod.dLast.values():
                oSet.aoLeaveInfo.append(self._getEdgeFailureReasonOccurence(oReason, iPeriod, fEnter = False));

        oSet.finalizePass2();

        self._oDb.execute('DROP TABLE TmpReasons\n');
        return oSet;


    def _getEdgeFailureReasonOccurence(self, oReason, iPeriod, fEnter = True):
        """
        Helper for the failure reason report that finds the oldest or newest build
        (SVN rev) and test set (start time) it occured with.

        If fEnter is set the oldest occurence is return, if fEnter clear the newest
        is is returned.

        Returns ReportFailureReasonTransient instant.

        """


        sSorting = 'ASC' if fEnter else 'DESC';
        self._oDb.execute('SELECT   TmpReasons.idTestResult,\n'
                          '         TmpReasons.idTestSet,\n'
                          '         TmpReasons.tsDone,\n'
                          '         TmpReasons.idBuild,\n'
                          '         Builds.iRevision,\n'
                          '         BuildCategories.sRepository\n'
                          'FROM     TmpReasons,\n'
                          '         Builds,\n'
                          '         BuildCategories\n'
                          'WHERE    TmpReasons.idFailureReason  = %s\n'
                          '     AND TmpReasons.idBuild          = Builds.idBuild\n'
                          '     AND Builds.tsExpire             > TmpReasons.tsCreated\n'
                          '     AND Builds.tsEffective         <= TmpReasons.tsCreated\n'
                          '     AND Builds.idBuildCategory      = BuildCategories.idBuildCategory\n'
                          'ORDER BY Builds.iRevision ' + sSorting + ',\n'
                          '         TmpReasons.tsCreated ' + sSorting + '\n'
                          'LIMIT 1\n'
                          , ( oReason.idFailureReason, ));
        aoRow = self._oDb.fetchOne();
        if aoRow is None:
            return ReportFailureReasonTransient(-1, -1, 'internal-error', -1, -1, self._oDb.getCurrentTimestamp(),
                                                iPeriod, fEnter, oReason);
        return ReportFailureReasonTransient(idBuild = aoRow[3], iRevision = aoRow[4], sRepository = aoRow[5],
                                            idTestSet = aoRow[1], idTestResult = aoRow[0], tsDone = aoRow[2],
                                            iPeriod = iPeriod, fEnter = fEnter, oReason = oReason);


    def getTestCaseFailures(self):
        """
        Gets the test case failures of the subject in the specified period.

        Returns a ReportPeriodSetWithTotalBase instance.

        """
        return self._getSimpleFailures('idTestCase', TestCaseLogic);


    def getTestCaseVariationFailures(self):
        """
        Gets the test case failures of the subject in the specified period.

        Returns a ReportPeriodSetWithTotalBase instance.

        """
        return self._getSimpleFailures('idTestCaseArgs', TestCaseArgsLogic);


    def getTestBoxFailures(self):
        """
        Gets the test box failures of the subject in the specified period.

        Returns a ReportPeriodSetWithTotalBase instance.

        """
        return self._getSimpleFailures('idTestBox', TestBoxLogic);


    def _getSimpleFailures(self, sIdColumn, oCacheLogicType, sIdAttr = None):
        """
        Gets the test box failures of the subject in the specified period.

        Returns a ReportPeriodSetWithTotalBase instance.

        """

        oLogic = oCacheLogicType(self._oDb);
        oSet = ReportPeriodSetWithTotalBase(sIdColumn if sIdAttr is None else sIdAttr);

        # Construct base query.
        sBaseQuery  = 'SELECT   TestSets.' + sIdColumn + ',\n' \
                      '         COUNT(CASE WHEN TestSets.enmStatus >= \'failure\' THEN 1 END),\n' \
                      '         MIN(TestSets.tsDone),\n' \
                      '         MAX(TestSets.tsDone),\n' \
                      '         COUNT(TestSets.idTestResult)\n' \
                      'FROM     TestSets\n' \
                      + self.oFilter.getTableJoins();
        for sTable in self.getExtraSubjectTables():
            sBaseQuery = sBaseQuery[:-1] + ',\n         ' + sTable + '\n';
        sBaseQuery += 'WHERE    TRUE\n' \
                    + self.oFilter.getWhereConditions() \
                    + self.getExtraSubjectWhereExpr() + '\n';

        # Retrieve the period results.
        for iPeriod in xrange(self.cPeriods):
            self._oDb.execute(sBaseQuery + self.getExtraWhereExprForPeriod(iPeriod) + 'GROUP BY TestSets.' + sIdColumn + '\n');
            aaoRows = self._oDb.fetchAll()

            oPeriod = ReportPeriodWithTotalBase(oSet, iPeriod, self.getStraightPeriodDesc(iPeriod),
                                                self.getPeriodStart(iPeriod), self.getPeriodEnd(iPeriod));

            for aoRow in aaoRows:
                oSubject = oLogic.cachedLookup(aoRow[0]);
                oPeriodRow = ReportHitRowWithTotalBase(aoRow[0], oSubject, aoRow[1], aoRow[4], aoRow[2], aoRow[3]);
                oPeriod.appendRow(oPeriodRow, aoRow[0], oSubject);

            oSet.appendPeriod(oPeriod);
        oSet.pruneRowsWithZeroSumHits();



        #
        # For reasons entering after the first period, look up the build and
        # test set it first occured with.
        #
        oSet.finalizePass1();

        for iPeriod in xrange(1, self.cPeriods):
            oPeriod = oSet.aoPeriods[iPeriod];
            for idSubject, oSubject in oPeriod.dFirst.items():
                oSet.aoEnterInfo.append(self._getEdgeSimpleFailureOccurence(idSubject, sIdColumn, oSubject,
                                                                            iPeriod, fEnter = True));

        # Ditto for reasons leaving before the last.
        for iPeriod in xrange(self.cPeriods - 1):
            oPeriod = oSet.aoPeriods[iPeriod];
            for idSubject, oSubject in oPeriod.dLast.items():
                oSet.aoLeaveInfo.append(self._getEdgeSimpleFailureOccurence(idSubject, sIdColumn, oSubject,
                                                                            iPeriod, fEnter = False));

        oSet.finalizePass2();

        return oSet;

    def _getEdgeSimpleFailureOccurence(self, idSubject, sIdColumn, oSubject, iPeriod, fEnter = True):
        """
        Helper for the failure reason report that finds the oldest or newest build
        (SVN rev) and test set (start time) it occured with.

        If fEnter is set the oldest occurence is return, if fEnter clear the newest
        is is returned.

        Returns ReportTransientBase instant.

        """
        sSorting = 'ASC' if fEnter else 'DESC';
        sQuery   = 'SELECT   TestSets.idTestResult,\n' \
                   '         TestSets.idTestSet,\n' \
                   '         TestSets.tsDone,\n' \
                   '         TestSets.idBuild,\n' \
                   '         Builds.iRevision,\n' \
                   '         BuildCategories.sRepository\n' \
                   'FROM     TestSets\n' \
                   + self.oFilter.getTableJoins(dOmitTables = {'Builds': True, 'BuildCategories': True});
        sQuery   = sQuery[:-1] + ',\n' \
                   '         Builds,\n' \
                   '         BuildCategories\n';
        for sTable in self.getExtraSubjectTables():
            if sTable not in [ 'Builds', 'BuildCategories' ] and not self.oFilter.isJoiningWithTable(sTable):
                sQuery = sQuery[:-1] + ',\n         ' + sTable + '\n';
        sQuery  += 'WHERE    TestSets.' + sIdColumn + '      = ' + str(idSubject) + '\n' \
                   '     AND TestSets.idBuild          = Builds.idBuild\n' \
                   '     AND TestSets.enmStatus       >= \'failure\'\n' \
                   + self.getExtraWhereExprForPeriod(iPeriod) + \
                   '     AND Builds.tsExpire           > TestSets.tsCreated\n' \
                   '     AND Builds.tsEffective       <= TestSets.tsCreated\n' \
                   '     AND Builds.idBuildCategory    = BuildCategories.idBuildCategory\n' \
                   + self.oFilter.getWhereConditions() \
                   + self.getExtraSubjectWhereExpr() + '\n' \
                   'ORDER BY Builds.iRevision ' + sSorting + ',\n' \
                   '         TestSets.tsCreated ' + sSorting + '\n' \
                   'LIMIT 1\n';
        self._oDb.execute(sQuery);
        aoRow = self._oDb.fetchOne();
        if aoRow is None:
            return ReportTransientBase(-1, -1, 'internal-error', -1, -1, self._oDb.getCurrentTimestamp(),
                                       iPeriod, fEnter, idSubject, oSubject);
        return ReportTransientBase(idBuild = aoRow[3], iRevision = aoRow[4], sRepository = aoRow[5],
                                   idTestSet = aoRow[1], idTestResult = aoRow[0], tsDone = aoRow[2],
                                   iPeriod = iPeriod, fEnter = fEnter, idSubject = idSubject, oSubject = oSubject);

    def fetchPossibleFilterOptions(self, oFilter, tsNow, sPeriod):
        """
        Fetches possible filtering options.
        """
        return TestResultLogic(self._oDb).fetchPossibleFilterOptions(oFilter, tsNow, sPeriod, oReportModel = self);



class ReportGraphModel(ReportModelBase): # pylint: disable=too-few-public-methods
    """
    Extended report model used when generating the more complicated graphs
    detailing results, time elapsed and values over time.
    """

    ## @name Subject ID types.
    ## These prefix the values in the aidSubjects array.  The prefix is
    ## followed by a colon and then a list of string IDs.  Following the prefix
    ## is one or more string table IDs separated by colons.  These are used to
    ## drill down the exact test result we're looking for, by matching against
    ## TestResult::idStrName (in the db).
    ## @{
    ksTypeResult  = 'result';
    ksTypeElapsed = 'elapsed';
    ## The last string table ID gives the name of the value.
    ksTypeValue   = 'value';
    ## List of types.
    kasTypes = (ksTypeResult, ksTypeElapsed, ksTypeValue);
    ## @}

    class SampleSource(object):
        """ A sample source. """
        def __init__(self, sType, aidStrTests, idStrValue):
            self.sType       = sType;
            self.aidStrTests = aidStrTests;
            self.idStrValue  = idStrValue;

        def getTestResultTables(self):
            """ Retrieves the list of TestResults tables to join with."""
            sRet = '';
            for i in range(len(self.aidStrTests)):
                sRet += '         TestResults TR%u,\n' % (i,);
            return sRet;

        def getTestResultConditions(self):
            """ Retrieves the join conditions for the TestResults tables."""
            sRet = '';
            cItems = len(self.aidStrTests);
            for i in range(cItems - 1):
                sRet += '     AND TR%u.idStrName = %u\n' \
                        '     AND TR%u.idTestResultParent = TR%u.idTestResult\n' \
                      % ( i, self.aidStrTests[cItems - i - 1], i, i + 1 );
            sRet += '     AND TR%u.idStrName = %u\n' % (cItems - 1, self.aidStrTests[0]);
            return sRet;

    class DataSeries(object):
        """ A data series. """
        def __init__(self, oCache, idBuildCategory, idTestBox, idTestCase, idTestCaseArgs, iUnit):
            _ = oCache;
            self.idBuildCategory    = idBuildCategory;
            self.oBuildCategory     = oCache.getBuildCategory(idBuildCategory);
            self.idTestBox          = idTestBox;
            self.oTestBox           = oCache.getTestBox(idTestBox);
            self.idTestCase         = idTestCase;
            self.idTestCaseArgs     = idTestCaseArgs;
            if idTestCase is not None:
                self.oTestCase      = oCache.getTestCase(idTestCase);
                self.oTestCaseArgs  = None;
            else:
                self.oTestCaseArgs  = oCache.getTestCaseArgs(idTestCaseArgs);
                self.oTestCase      = oCache.getTestCase(self.oTestCaseArgs.idTestCase);
            self.iUnit              = iUnit;
            # Six parallel arrays.
            self.aiRevisions        = []; # The X values.
            self.aiValues           = []; # The Y values.
            self.aiErrorBarBelow    = []; # The Y value minimum errorbars, relative to the Y value (positive).
            self.aiErrorBarAbove    = []; # The Y value maximum errorbars, relative to the Y value (positive).
            self.acSamples          = []; # The number of samples at this X value.
            self.aoRevInfo          = []; # VcsRevisionData objects for each revision. Empty/SQL-NULL objects if no info.

    class DataSeriesCollection(object):
        """ A collection of data series corresponding to one input sample source. """
        def __init__(self, oSampleSrc, asTests, sValue = None):
            self.sType       = oSampleSrc.sType;
            self.aidStrTests = oSampleSrc.aidStrTests;
            self.asTests     = list(asTests);
            self.idStrValue  = oSampleSrc.idStrValue;
            self.sValue      = sValue;
            self.aoSeries    = [];

        def addDataSeries(self, oDataSeries):
            """ Appends a data series to the collection. """
            self.aoSeries.append(oDataSeries);
            return oDataSeries;


    def __init__(self, oDb, tsNow, cPeriods, cHoursPerPeriod, sSubject, aidSubjects, # pylint: disable=too-many-arguments
                 aidTestBoxes, aidBuildCats, aidTestCases, fSepTestVars):
        assert(sSubject == self.ksSubEverything); # dummy
        ReportModelBase.__init__(self, oDb, tsNow, cPeriods, cHoursPerPeriod, sSubject, aidSubjects, oFilter = None);
        self.aidTestBoxes = aidTestBoxes;
        self.aidBuildCats = aidBuildCats;
        self.aidTestCases = aidTestCases;
        self.fOnTestCase  = not fSepTestVars; # (Separates testcase variations into separate data series.)
        self.oCache       = DatabaseObjCache(self._oDb, self.tsNow, None, self.cPeriods * self.cHoursPerPeriod);


        # Quickly validate and convert the subject "IDs".
        self.aoLookups       = [];
        for sCur in self.aidSubjects:
            asParts = sCur.split(':');
            if len(asParts) < 2:
                raise TMExceptionBase('Invalid graph value "%s"' % (sCur,));

            sType = asParts[0];
            if sType not in ReportGraphModel.kasTypes:
                raise TMExceptionBase('Invalid graph value type "%s" (full: "%s")' % (sType, sCur,));

            aidStrTests = [];
            for sIdStr in asParts[1:]:
                try:    idStr = int(sIdStr);
                except: raise TMExceptionBase('Invalid graph value id "%s" (full: "%s")' % (sIdStr, sCur,));
                if idStr < 0:
                    raise TMExceptionBase('Invalid graph value id "%u" (full: "%s")' % (idStr, sCur,));
                aidStrTests.append(idStr);

            idStrValue = None;
            if sType == ReportGraphModel.ksTypeValue:
                idStrValue = aidStrTests.pop();
            self.aoLookups.append(ReportGraphModel.SampleSource(sType, aidStrTests, idStrValue));

        # done


    def getExtraWhereExprForTotalPeriod(self, sTimestampField):
        """
        Returns additional WHERE expression for getting test sets for the
        specified period.  It starts with an AND so that it can simply be
        appended to the WHERE clause.
        """
        return self.getExtraWhereExprForTotalPeriodEx(sTimestampField, sTimestampField, True);

    def getExtraWhereExprForTotalPeriodEx(self, sStartField = 'tsCreated', sEndField = 'tsDone', fLeadingAnd = True):
        """
        Returns additional WHERE expression for getting test sets for the
        specified period.
        """
        if self.tsNow is None:
            sNow = 'CURRENT_TIMESTAMP';
        else:
            sNow = self._oDb.formatBindArgs('%s::TIMESTAMP', (self.tsNow,));

        sRet = '     AND %s >= (%s - interval \'%u hours\')\n' \
               '     AND %s <=  %s\n' \
             % ( sStartField, sNow, self.cPeriods * self.cHoursPerPeriod,
                 sEndField, sNow);

        if not fLeadingAnd:
            assert sRet[8] == ' ' and sRet[7] == 'D';
            return sRet[9:];
        return sRet;

    def _getEligibleTestSetPeriod(self, sPrefix = 'TestSets.', fLeadingAnd = False):
        """
        Returns additional WHERE expression for getting TestSets rows
        potentially relevant for the selected period.
        """
        if self.tsNow is None:
            sNow = 'CURRENT_TIMESTAMP';
        else:
            sNow = self._oDb.formatBindArgs('%s::TIMESTAMP', (self.tsNow,));

        # The 2nd line is a performance hack on TestSets.  It nudges postgresql
        # into useing the TestSetsCreatedDoneIdx index instead of doing a table
        # scan when we look for eligible bits there.
        # ASSUMES no relevant test runs longer than 7 days!
        sRet = '     AND %stsCreated <= %s\n' \
               '     AND %stsCreated >= (%s - interval \'%u hours\' - interval \'%u days\')\n' \
               '     AND %stsDone    >= (%s - interval \'%u hours\')\n' \
             % ( sPrefix, sNow,
                 sPrefix, sNow,  self.cPeriods * self.cHoursPerPeriod, 7,
                 sPrefix, sNow, self.cPeriods * self.cHoursPerPeriod, );

        if not fLeadingAnd:
            assert sRet[8] == ' ' and sRet[7] == 'D';
            return sRet[9:];
        return sRet;


    def _getNameStrings(self, aidStrTests):
        """ Returns an array of names corresponding to the array of string table entries. """
        return [self.oCache.getTestResultString(idStr) for idStr in aidStrTests];

    def fetchGraphData(self):
        """ returns data """
        sWantedTestCaseId = 'idTestCase' if self.fOnTestCase else 'idTestCaseArgs';

        aoRet = [];
        for oLookup in self.aoLookups:
            #
            # Set up the result collection.
            #
            if oLookup.sType == self.ksTypeValue:
                oCollection = self.DataSeriesCollection(oLookup, self._getNameStrings(oLookup.aidStrTests),
                                                        self.oCache.getTestResultString(oLookup.idStrValue));
            else:
                oCollection = self.DataSeriesCollection(oLookup, self._getNameStrings(oLookup.aidStrTests));

            #
            # Construct the query.
            #
            sQuery  = 'SELECT   Builds.iRevision,\n' \
                      '         TestSets.idBuildCategory,\n' \
                      '         TestSets.idTestBox,\n' \
                      '         TestSets.' + sWantedTestCaseId + ',\n';
            if oLookup.sType == self.ksTypeValue:
                sQuery += '         TestResultValues.iUnit as iUnit,\n' \
                          '         MIN(TestResultValues.lValue),\n' \
                          '         CAST(ROUND(AVG(TestResultValues.lValue)) AS BIGINT),\n' \
                          '         MAX(TestResultValues.lValue),\n' \
                          '         COUNT(TestResultValues.lValue)\n';
            elif oLookup.sType == self.ksTypeElapsed:
                sQuery += '         %u as iUnit,\n' \
                          '         CAST((EXTRACT(EPOCH FROM MIN(TR0.tsElapsed)) * 1000) AS INTEGER),\n' \
                          '         CAST((EXTRACT(EPOCH FROM AVG(TR0.tsElapsed)) * 1000) AS INTEGER),\n' \
                          '         CAST((EXTRACT(EPOCH FROM MAX(TR0.tsElapsed)) * 1000) AS INTEGER),\n' \
                          '         COUNT(TR0.tsElapsed)\n' \
                        % (constants.valueunit.MS,);
            else:
                sQuery += '         %u as iUnit,\n'\
                          '         MIN(TR0.cErrors),\n' \
                          '         CAST(ROUND(AVG(TR0.cErrors)) AS INTEGER),\n' \
                          '         MAX(TR0.cErrors),\n' \
                          '         COUNT(TR0.cErrors)\n' \
                        % (constants.valueunit.OCCURRENCES,);

            if oLookup.sType == self.ksTypeValue:
                sQuery += 'FROM     TestResultValues,\n';
                sQuery += '         TestSets,\n'
                sQuery += oLookup.getTestResultTables();
            else:
                sQuery += 'FROM     ' + oLookup.getTestResultTables().lstrip();
                sQuery += '         TestSets,\n';
            sQuery += '         Builds\n';

            if oLookup.sType == self.ksTypeValue:
                sQuery += 'WHERE    TestResultValues.idStrName = %u\n' % ( oLookup.idStrValue, );
                sQuery += self.getExtraWhereExprForTotalPeriod('TestResultValues.tsCreated');
                sQuery += '     AND TestResultValues.idTestSet = TestSets.idTestSet\n';
                sQuery += self._getEligibleTestSetPeriod(fLeadingAnd = True);
            else:
                sQuery += 'WHERE    ' + (self.getExtraWhereExprForTotalPeriod('TR0.tsCreated').lstrip()[4:]).lstrip();
                sQuery += '     AND TR0.idTestSet = TestSets.idTestSet\n';

            if len(self.aidTestBoxes) == 1:
                sQuery += '     AND TestSets.idTestBox = %u\n' % (self.aidTestBoxes[0],);
            elif self.aidTestBoxes:
                sQuery += '     AND TestSets.idTestBox IN (' + ','.join([str(i) for i in self.aidTestBoxes]) + ')\n';

            if len(self.aidBuildCats) == 1:
                sQuery += '     AND TestSets.idBuildCategory = %u\n' % (self.aidBuildCats[0],);
            elif self.aidBuildCats:
                sQuery += '     AND TestSets.idBuildCategory IN (' + ','.join([str(i) for i in self.aidBuildCats]) + ')\n';

            if len(self.aidTestCases) == 1:
                sQuery += '     AND TestSets.idTestCase = %u\n' % (self.aidTestCases[0],);
            elif self.aidTestCases:
                sQuery += '     AND TestSets.idTestCase IN (' + ','.join([str(i) for i in self.aidTestCases]) + ')\n';

            if oLookup.sType == self.ksTypeElapsed:
                sQuery += '     AND TestSets.enmStatus = \'%s\'::TestStatus_T\n' % (self.ksTestStatus_Success,);

            if oLookup.sType == self.ksTypeValue:
                sQuery += '     AND TestResultValues.idTestResult = TR0.idTestResult\n'
                sQuery += self.getExtraWhereExprForTotalPeriod('TR0.tsCreated'); # For better index matching in some cases.

            if oLookup.sType != self.ksTypeResult:
                sQuery += '     AND TR0.enmStatus = \'%s\'::TestStatus_T\n' % (self.ksTestStatus_Success,);

            sQuery += oLookup.getTestResultConditions();
            sQuery += '     AND TestSets.idBuild = Builds.idBuild\n';

            sQuery += 'GROUP BY TestSets.idBuildCategory,\n' \
                      '         TestSets.idTestBox,\n' \
                      '         TestSets.' + sWantedTestCaseId + ',\n' \
                      '         iUnit,\n' \
                      '         Builds.iRevision\n';
            sQuery += 'ORDER BY TestSets.idBuildCategory,\n' \
                      '         TestSets.idTestBox,\n' \
                      '         TestSets.' + sWantedTestCaseId + ',\n' \
                      '         iUnit,\n' \
                      '         Builds.iRevision\n';

            #
            # Execute it and collect the result.
            #
            sCurRepository   = None;
            dRevisions       = {};
            oLastSeries      = None;
            idLastBuildCat   = -1;
            idLastTestBox    = -1;
            idLastTestCase   = -1;
            iLastUnit        = -1;
            self._oDb.execute(sQuery);
            for aoRow in self._oDb.fetchAll(): # Fetching all here so we can make cache queries below.
                if  aoRow[1] != idLastBuildCat \
                 or aoRow[2] != idLastTestBox \
                 or aoRow[3] != idLastTestCase \
                 or aoRow[4] != iLastUnit:
                    idLastBuildCat = aoRow[1];
                    idLastTestBox  = aoRow[2];
                    idLastTestCase = aoRow[3];
                    iLastUnit      = aoRow[4];
                    if self.fOnTestCase:
                        oLastSeries = self.DataSeries(self.oCache, idLastBuildCat, idLastTestBox,
                                                      idLastTestCase, None, iLastUnit);
                    else:
                        oLastSeries = self.DataSeries(self.oCache, idLastBuildCat, idLastTestBox,
                                                      None, idLastTestCase, iLastUnit);
                    oCollection.addDataSeries(oLastSeries);
                    if oLastSeries.oBuildCategory.sRepository != sCurRepository:
                        if sCurRepository is not None:
                            self.oCache.preloadVcsRevInfo(sCurRepository, dRevisions.keys());
                        sCurRepository = oLastSeries.oBuildCategory.sRepository
                        dRevisions = {};
                oLastSeries.aiRevisions.append(aoRow[0]);
                oLastSeries.aiValues.append(aoRow[6]);
                oLastSeries.aiErrorBarBelow.append(aoRow[6] - aoRow[5]);
                oLastSeries.aiErrorBarAbove.append(aoRow[7] - aoRow[6]);
                oLastSeries.acSamples.append(aoRow[8]);
                dRevisions[aoRow[0]] = 1;

            if sCurRepository is not None:
                self.oCache.preloadVcsRevInfo(sCurRepository, dRevisions.keys());
                del dRevisions;

            #
            # Look up the VCS revision details.
            #
            for oSeries in oCollection.aoSeries:
                for iRevision in oSeries.aiRevisions:
                    oSeries.aoRevInfo.append(self.oCache.getVcsRevInfo(sCurRepository, iRevision));
            aoRet.append(oCollection);

        return aoRet;

    def getEligibleTestBoxes(self):
        """
        Returns a list of TestBoxData objects with eligible testboxes for
        the total period of time defined for this graph.
        """

        # Taking the simple way out now, getting all active testboxes at the
        # time without filtering out on sample sources.

        # 1. Collect the relevant testbox generation IDs.
        self._oDb.execute('SELECT   DISTINCT idTestBox, idGenTestBox\n'
                          'FROM     TestSets\n'
                          'WHERE    ' + self._getEligibleTestSetPeriod(fLeadingAnd = False) +
                          'ORDER BY idTestBox, idGenTestBox DESC');
        idPrevTestBox    = -1;
        asIdGenTestBoxes = [];
        for _ in range(self._oDb.getRowCount()):
            aoRow = self._oDb.fetchOne();
            if aoRow[0] != idPrevTestBox:
                idPrevTestBox = aoRow[0];
                asIdGenTestBoxes.append(str(aoRow[1]));

        # 2. Query all the testbox data in one go.
        aoRet = [];
        if asIdGenTestBoxes:
            self._oDb.execute('SELECT   *\n'
                              'FROM     TestBoxesWithStrings\n'
                              'WHERE    idGenTestBox IN (' + ','.join(asIdGenTestBoxes) + ')\n'
                              'ORDER BY sName');
            for _ in range(self._oDb.getRowCount()):
                aoRet.append(TestBoxData().initFromDbRow(self._oDb.fetchOne()));

        return aoRet;

    def getEligibleBuildCategories(self):
        """
        Returns a list of BuildCategoryData objects with eligible build
        categories for the total period of time defined for this graph.  In
        addition it will add any currently selected categories that aren't
        really relevant to the period, just to simplify the WUI code.

        """

        # Taking the simple way out now, getting all used build cat without
        # any testbox or testcase filtering.

        sSelectedBuildCats = '';
        if self.aidBuildCats:
            sSelectedBuildCats = '   OR idBuildCategory IN (' + ','.join([str(i) for i in self.aidBuildCats]) + ')\n';

        self._oDb.execute('SELECT   DISTINCT *\n'
                          'FROM     BuildCategories\n'
                          'WHERE    idBuildCategory IN (\n'
                          '   SELECT DISTINCT idBuildCategory\n'
                          '   FROM  TestSets\n'
                          '   WHERE ' + self._getEligibleTestSetPeriod(fLeadingAnd = False) +
                          ')\n'
                          + sSelectedBuildCats +
                          'ORDER BY sProduct,\n'
                          '         sBranch,\n'
                          '         asOsArches,\n'
                          '         sType\n');
        aoRet = [];
        for _ in range(self._oDb.getRowCount()):
            aoRet.append(BuildCategoryData().initFromDbRow(self._oDb.fetchOne()));

        return aoRet;

