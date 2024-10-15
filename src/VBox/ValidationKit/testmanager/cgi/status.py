#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: status.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
CGI - Administrator Web-UI.
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
import sys

# Only the main script needs to modify the path.
g_ksValidationKitDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))));
sys.path.append(g_ksValidationKitDir);

# Validation Kit imports.
from testmanager                        import config;
from testmanager.core.webservergluecgi  import WebServerGlueCgi;

from common                             import constants;
from testmanager.core.base              import TMExceptionBase;
from testmanager.core.db                import TMDatabaseConnection;



def timeDeltaToHours(oTimeDelta):
    return oTimeDelta.days * 24 + oTimeDelta.seconds // 3600


def testbox_data_processing(oDb):
    dTestBoxes = {}
    while True:
        # Fetch the next row and unpack it.
        aoRow = oDb.fetchOne();
        if aoRow is None:
            break;
        sTextBoxName           = aoRow[0];
        enmStatus              = aoRow[1];
        oTimeDeltaSinceStarted = aoRow[2];
        sTestBoxOs             = aoRow[3]
        sSchedGroupNames       = aoRow[4];

        # Idle testboxes will not have an assigned test set, so enmStatus
        # will be None.  Skip these.
        if enmStatus:
            dict_update(dTestBoxes, sTextBoxName, enmStatus)

            if "testbox_os" not in dTestBoxes[sTextBoxName]:
                dTestBoxes[sTextBoxName].update({"testbox_os": sTestBoxOs})

            if "sched_group" not in dTestBoxes[sTextBoxName]:
                dTestBoxes[sTextBoxName].update({"sched_group": sSchedGroupNames})
            elif sSchedGroupNames not in dTestBoxes[sTextBoxName]["sched_group"]:
                dTestBoxes[sTextBoxName]["sched_group"] += "," + sSchedGroupNames

            if enmStatus == "running":
                dTestBoxes[sTextBoxName].update({"hours_running": timeDeltaToHours(oTimeDeltaSinceStarted)})

    return dTestBoxes;


def os_results_separating(dResult, sTestName, sTestBoxOs, sTestBoxCpuArch, enmStatus):
    if sTestBoxOs == 'win':
        sTestBoxOs = 'windows'
    elif sTestBoxOs not in ('linux', 'darwin', 'solaris'):
        sTestBoxOs = 'other'
    dict_update(dResult, '%s / %s.%s' % (sTestName, sTestBoxOs, sTestBoxCpuArch), enmStatus)


## Template dictionary for new dTarget[] entries in dict_update.
# This _MUST_ include all values of the TestStatus_T SQL enum type.
g_kdTestStatuses = {
    'running': 0,
    'success': 0,
    'skipped': 0,
    'bad-testbox': 0,
    'aborted': 0,
    'failure': 0,
    'timed-out': 0,
    'rebooted': 0,
}

def dict_update(dTarget, sKeyName, enmStatus):
    if sKeyName not in dTarget:
        dTarget.update({sKeyName: g_kdTestStatuses.copy()})
    dTarget[sKeyName][enmStatus] += 1


def formatDataEntry(sKey, dEntry):
    # There are variations in the first and second "columns".
    if "hours_running" in dEntry:
        sRet = "%s;%s;%s | running: %s;%s" \
             % (sKey, dEntry["testbox_os"], dEntry["sched_group"], dEntry["running"], dEntry["hours_running"]);
    else:
        if "testbox_os" in dEntry:
            sRet = "%s;%s;%s" % (sKey, dEntry["testbox_os"], dEntry["sched_group"],);
        else:
            sRet = sKey;
        sRet += " | running: %s" % (dEntry["running"],)

    # The rest is currently identical:
    sRet += " | success: %s | skipped: %s | bad-testbox: %s | aborted: %s | failure: %s | timed-out: %s | rebooted: %s | \n" \
          % (dEntry["success"], dEntry["skipped"], dEntry["bad-testbox"], dEntry["aborted"],
             dEntry["failure"], dEntry["timed-out"], dEntry["rebooted"],);
    return sRet;


def format_data(dData, fSorted):
    sRet = "";
    if not fSorted:
        for sKey in dData:
            sRet += formatDataEntry(sKey, dData[sKey]);
    else:
        for sKey in sorted(dData.keys()):
            sRet += formatDataEntry(sKey, dData[sKey]);
    return sRet;

######

class StatusDispatcherException(TMExceptionBase):
    """
    Exception class for TestBoxController.
    """
    pass;                               # pylint: disable=unnecessary-pass


class StatusDispatcher(object): # pylint: disable=too-few-public-methods
    """
    Status dispatcher class.
    """


    def __init__(self, oSrvGlue):
        """
        Won't raise exceptions.
        """
        self._oSrvGlue          = oSrvGlue;
        self._sAction           = None; # _getStandardParams / dispatchRequest sets this later on.
        self._dParams           = None; # _getStandardParams / dispatchRequest sets this later on.
        self._asCheckedParams   = [];
        self._dActions          = \
        {
            'MagicMirrorTestResults': self._actionMagicMirrorTestResults,
            'MagicMirrorTestBoxes':   self._actionMagicMirrorTestBoxes,
        };

    def _getStringParam(self, sName, asValidValues = None, fStrip = False, sDefValue = None):
        """
        Gets a string parameter (stripped).

        Raises exception if not found and no default is provided, or if the
        value isn't found in asValidValues.
        """
        if sName not in self._dParams:
            if sDefValue is None:
                raise StatusDispatcherException('%s parameter %s is missing' % (self._sAction, sName));
            return sDefValue;
        sValue = self._dParams[sName];
        if fStrip:
            sValue = sValue.strip();

        if sName not in self._asCheckedParams:
            self._asCheckedParams.append(sName);

        if asValidValues is not None and sValue not in asValidValues:
            raise StatusDispatcherException('%s parameter %s value "%s" not in %s '
                                            % (self._sAction, sName, sValue, asValidValues));
        return sValue;

    def _getIntParam(self, sName, iMin = None, iMax = None, iDefValue = None):
        """
        Gets a string parameter.
        Raises exception if not found, not a valid integer, or if the value
        isn't in the range defined by iMin and iMax.
        """
        if sName not in self._dParams:
            if iDefValue is None:
                raise StatusDispatcherException('%s parameter %s is missing' % (self._sAction, sName));
            return iDefValue;
        sValue = self._dParams[sName];
        try:
            iValue = int(sValue, 0);
        except:
            raise StatusDispatcherException('%s parameter %s value "%s" cannot be convert to an integer'
                                            % (self._sAction, sName, sValue));
        if sName not in self._asCheckedParams:
            self._asCheckedParams.append(sName);

        if   (iMin is not None and iValue < iMin) \
          or (iMax is not None and iValue > iMax):
            raise StatusDispatcherException('%s parameter %s value %d is out of range [%s..%s]'
                                            % (self._sAction, sName, iValue, iMin, iMax));
        return iValue;

    def _getBoolParam(self, sName, fDefValue = None):
        """
        Gets a boolean parameter.

        Raises exception if not found and no default is provided, or if not a
        valid boolean.
        """
        sValue = self._getStringParam(sName, [ 'True', 'true', '1', 'False', 'false', '0'], sDefValue = str(fDefValue));
        return sValue in ('True', 'true', '1',);

    def _checkForUnknownParameters(self):
        """
        Check if we've handled all parameters, raises exception if anything
        unknown was found.
        """

        if len(self._asCheckedParams) != len(self._dParams):
            sUnknownParams = '';
            for sKey in self._dParams:
                if sKey not in self._asCheckedParams:
                    sUnknownParams += ' ' + sKey + '=' + self._dParams[sKey];
            raise StatusDispatcherException('Unknown parameters: ' + sUnknownParams);

        return True;

    def _connectToDb(self):
        """
        Connects to the database.

        Returns (TMDatabaseConnection, (more later perhaps) ) on success.
        Returns (None, ) on failure after sending the box an appropriate response.
        May raise exception on DB error.
        """
        return (TMDatabaseConnection(self._oSrvGlue.dprint),);

    def _actionMagicMirrorTestBoxes(self):
        """
        Produces test result status for the magic mirror dashboard
        """

        #
        # Parse arguments and connect to the database.
        #
        cHoursBack = self._getIntParam('cHours', 1, 24*14, 12);
        fSorted   = self._getBoolParam('fSorted', False);
        self._checkForUnknownParameters();

        #
        # Get the data.
        #
        # - The first part of the select is about fetching all finished tests
        #   for last cHoursBack hours
        #
        # - The second part is fetching all tests which isn't done. (Both old
        #   (running more than cHoursBack) and fresh (less than cHoursBack) ones
        #   because we want to know if there's a hanging tests together with
        #   currently running).
        #
        # - There are also testsets without status at all, likely because disabled
        #   testboxes still have an assigned testsets.
        #
        # Note! We're not joining on TestBoxesWithStrings.idTestBox = TestSets.idGenTestBox
        #       here because of indexes.  This is also more consistent with the
        #       rest of the query.
        #
        # Note! The original SQL is slow because of the 'OR TestSets.tsDone'
        #       part, using AND and UNION is significatly faster because
        #       it matches the TestSetsGraphBoxIdx (index).
        #
        (oDb,) = self._connectToDb();
        if oDb is None:
            return False;

        oDb.execute('''
(   SELECT  TestBoxesWithStrings.sName,
            TestSets.enmStatus,
            CURRENT_TIMESTAMP - TestSets.tsCreated,
            TestBoxesWithStrings.sOS,
            SchedGroupNames.sSchedGroupNames
    FROM    (
            SELECT TestBoxesInSchedGroups.idTestBox AS idTestBox,
            STRING_AGG(SchedGroups.sName, ',') AS sSchedGroupNames
            FROM   TestBoxesInSchedGroups
            INNER JOIN SchedGroups
                    ON SchedGroups.idSchedGroup = TestBoxesInSchedGroups.idSchedGroup
            WHERE   TestBoxesInSchedGroups.tsExpire = 'infinity'::TIMESTAMP
                AND SchedGroups.tsExpire            = 'infinity'::TIMESTAMP
            GROUP BY TestBoxesInSchedGroups.idTestBox
            ) AS SchedGroupNames,
            TestBoxesWithStrings
    LEFT OUTER JOIN TestSets
                 ON TestSets.idTestBox  = TestBoxesWithStrings.idTestBox
                AND TestSets.tsCreated >= (CURRENT_TIMESTAMP - '%s hours'::interval)
                AND TestSets.tsDone IS NOT NULL
    WHERE   TestBoxesWithStrings.tsExpire = 'infinity'::TIMESTAMP
      AND   SchedGroupNames.idTestBox = TestBoxesWithStrings.idTestBox
) UNION (
    SELECT  TestBoxesWithStrings.sName,
            TestSets.enmStatus,
            CURRENT_TIMESTAMP - TestSets.tsCreated,
            TestBoxesWithStrings.sOS,
            SchedGroupNames.sSchedGroupNames
    FROM    (
            SELECT TestBoxesInSchedGroups.idTestBox AS idTestBox,
            STRING_AGG(SchedGroups.sName, ',') AS sSchedGroupNames
            FROM   TestBoxesInSchedGroups
            INNER JOIN SchedGroups
                    ON SchedGroups.idSchedGroup = TestBoxesInSchedGroups.idSchedGroup
            WHERE   TestBoxesInSchedGroups.tsExpire = 'infinity'::TIMESTAMP
                AND SchedGroups.tsExpire            = 'infinity'::TIMESTAMP
            GROUP BY TestBoxesInSchedGroups.idTestBox
            ) AS SchedGroupNames,
            TestBoxesWithStrings
    LEFT OUTER JOIN TestSets
                 ON TestSets.idTestBox  = TestBoxesWithStrings.idTestBox
                AND TestSets.tsDone IS NULL
    WHERE   TestBoxesWithStrings.tsExpire = 'infinity'::TIMESTAMP
      AND   SchedGroupNames.idTestBox = TestBoxesWithStrings.idTestBox
)
''', (cHoursBack, ));


        #
        # Process, format and output data.
        #
        dResult = testbox_data_processing(oDb);
        self._oSrvGlue.setContentType('text/plain');
        self._oSrvGlue.write(format_data(dResult, fSorted));

        return True;

    def _actionMagicMirrorTestResults(self):
        """
        Produces test result status for the magic mirror dashboard
        """

        #
        # Parse arguments and connect to the database.
        #
        sBranch = self._getStringParam('sBranch');
        cHoursBack = self._getIntParam('cHours', 1, 24*14, 6); ## @todo why 6 hours here and 12 for test boxes?
        fSorted   = self._getBoolParam('fSorted', False);
        self._checkForUnknownParameters();

        #
        # Get the data.
        #
        # Note! These queries should be joining TestBoxesWithStrings and TestSets
        #       on idGenTestBox rather than on idTestBox and tsExpire=inf, but
        #       we don't have any index matching those.  So, we'll ignore tests
        #       performed by deleted testboxes for the present as that doesn't
        #       happen often and we want the ~1000x speedup.
        #
        (oDb,) = self._connectToDb();
        if oDb is None:
            return False;

        if sBranch == 'all':
            oDb.execute('''
SELECT  TestSets.enmStatus,
        TestCases.sName,
        TestBoxesWithStrings.sOS,
        TestBoxesWithStrings.sCpuArch
FROM    TestSets
INNER JOIN TestCases
        ON TestCases.idGenTestCase         = TestSets.idGenTestCase
INNER JOIN TestBoxesWithStrings
        ON TestBoxesWithStrings.idTestBox  = TestSets.idTestBox
       AND TestBoxesWithStrings.tsExpire   = 'infinity'::TIMESTAMP
WHERE   TestSets.tsCreated                >= (CURRENT_TIMESTAMP - '%s hours'::interval)
''', (cHoursBack,));
        else:
            oDb.execute('''
SELECT  TestSets.enmStatus,
        TestCases.sName,
        TestBoxesWithStrings.sOS,
        TestBoxesWithStrings.sCpuArch
FROM    TestSets
INNER JOIN BuildCategories
        ON BuildCategories.idBuildCategory = TestSets.idBuildCategory
       AND BuildCategories.sBranch         = %s
INNER JOIN TestCases
        ON TestCases.idGenTestCase         = TestSets.idGenTestCase
INNER JOIN TestBoxesWithStrings
        ON TestBoxesWithStrings.idTestBox  = TestSets.idTestBox
       AND TestBoxesWithStrings.tsExpire   = 'infinity'::TIMESTAMP
WHERE   TestSets.tsCreated                >= (CURRENT_TIMESTAMP - '%s hours'::interval)
''', (sBranch, cHoursBack,));

        # Process the data
        dResult = {};
        while True:
            aoRow = oDb.fetchOne();
            if aoRow is None:
                break;
            os_results_separating(dResult, aoRow[1], aoRow[2], aoRow[3], aoRow[0])  # save all test results

        # Format and output it.
        self._oSrvGlue.setContentType('text/plain');
        self._oSrvGlue.write(format_data(dResult, fSorted));

        return True;

    def _getStandardParams(self, dParams):
        """
        Gets the standard parameters and validates them.

        The parameters are returned as a tuple: sAction, (more later, maybe)
        Note! the sTextBoxId can be None if it's a SIGNON request.

        Raises StatusDispatcherException on invalid input.
        """
        #
        # Get the action parameter and validate it.
        #
        if constants.tbreq.ALL_PARAM_ACTION not in dParams:
            raise StatusDispatcherException('No "%s" parameter in request (params: %s)'
                                            % (constants.tbreq.ALL_PARAM_ACTION, dParams,));
        sAction = dParams[constants.tbreq.ALL_PARAM_ACTION];

        if sAction not in self._dActions:
            raise StatusDispatcherException('Unknown action "%s" in request (params: %s; action: %s)'
                                            % (sAction, dParams, self._dActions));
        #
        # Update the list of checked parameters.
        #
        self._asCheckedParams.extend([constants.tbreq.ALL_PARAM_ACTION,]);

        return (sAction,);

    def dispatchRequest(self):
        """
        Dispatches the incoming request.

        Will raise StatusDispatcherException on failure.
        """

        #
        # Must be a GET request.
        #
        try:
            sMethod = self._oSrvGlue.getMethod();
        except Exception as oXcpt:
            raise StatusDispatcherException('Error retriving request method: %s' % (oXcpt,));
        if sMethod != 'GET':
            raise StatusDispatcherException('Error expected POST request not "%s"' % (sMethod,));

        #
        # Get the parameters and checks for duplicates.
        #
        try:
            dParams = self._oSrvGlue.getParameters();
        except Exception as oXcpt:
            raise StatusDispatcherException('Error retriving parameters: %s' % (oXcpt,));
        for sKey in dParams.keys():
            if len(dParams[sKey]) > 1:
                raise StatusDispatcherException('Parameter "%s" is given multiple times: %s' % (sKey, dParams[sKey]));
            dParams[sKey] = dParams[sKey][0];
        self._dParams = dParams;

        #
        # Get+validate the standard action parameters and dispatch the request.
        #
        (self._sAction, ) = self._getStandardParams(dParams);
        return self._dActions[self._sAction]();


def main():
    """
    Main function a la C/C++. Returns exit code.
    """

    oSrvGlue = WebServerGlueCgi(g_ksValidationKitDir, fHtmlOutput = False);
    try:
        oDisp = StatusDispatcher(oSrvGlue);
        oDisp.dispatchRequest();
        oSrvGlue.flush();
    except Exception as oXcpt:
        return oSrvGlue.errorPage('Internal error: %s' % (str(oXcpt),), sys.exc_info());

    return 0;

if __name__ == '__main__':
    if config.g_kfProfileAdmin:
        from testmanager.debug import cgiprofiling;
        sys.exit(cgiprofiling.profileIt(main));
    else:
        sys.exit(main());

