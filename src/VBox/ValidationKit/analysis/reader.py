# -*- coding: utf-8 -*-
# $Id: reader.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
XML reader module.

This produces a test result tree that can be processed and passed to
reporting.
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
__all__     = [ 'parseTestResult', ]

# Standard python imports.
import datetime;
import os;
import re;
import sys;
import traceback;

# Only the main script needs to modify the path.
try:    __file__;
except: __file__ = sys.argv[0];
g_ksValidationKitDir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)));
sys.path.append(g_ksValidationKitDir);

# ValidationKit imports.
from common import utils;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    long = int;     # pylint: disable=redefined-builtin,invalid-name

# pylint: disable=missing-docstring


class Value(object):
    """
    Represents a value.  Usually this is benchmark result or parameter.
    """

    kdBestByUnit = {
        "%":                +1, # Difficult to say what's best really.
        "bytes":            +1, # Difficult to say what's best really.
        "bytes/s":          +2,
        "KB":               +1,
        "KB/s":             +2,
        "MB":               +1,
        "MB/s":             +2,
        "packets":          +2,
        "packets/s":        +2,
        "frames":           +2,
        "frames/s":         +2,
        "occurrences":      +1, # Difficult to say what's best really.
        "occurrences/s":    +2,
        "roundtrips":       +2,
        "calls":            +1, # Difficult to say what's best really.
        "calls/s":          +2,
        "s":                -2,
        "ms":               -2,
        "ns":               -2,
        "ns/call":          -2,
        "ns/frame":         -2,
        "ns/occurrence":    -2,
        "ns/packet":        -2,
        "ns/roundtrip":     -2,
        "ins":              +2,
        "ins/sec":          +2,
        "":                 +1, # Difficult to say what's best really.
        "pp1k":             -2,
        "pp10k":            -2,
        "ppm":              -2,
        "ppb":              -2,
        "ticks":            -1, # Difficult to say what's best really.
        "ticks/call":       -2,
        "ticks/occ":        -2,
        "pages":            +1, # Difficult to say what's best really.
        "pages/s":          +2,
        "ticks/page":       -2,
        "ns/page":          -2,
        "ps":               -1, # Difficult to say what's best really.
        "ps/call":          -2,
        "ps/frame":         -2,
        "ps/occurrence":    -2,
        "ps/packet":        -2,
        "ps/roundtrip":     -2,
        "ps/page":          -2,
    };

    def __init__(self, oTest, sName = None, sUnit = None, sTimestamp = None, lValue = None):
        self.oTest      = oTest;
        self.sName      = sName;
        self.sUnit      = sUnit;
        self.sTimestamp = sTimestamp;
        self.lValue     = self.valueToInteger(lValue);
        assert self.lValue is None or isinstance(self.lValue, (int, long)), "lValue=%s %s" % (self.lValue, type(self.lValue),);

    def clone(self, oParentTest):
        """
        Clones the value.
        """
        return Value(oParentTest, self.sName, self.sUnit, self.sTimestamp, self.lValue);

    def matchFilters(self, sPrefix, aoFilters):
        """
        Checks for any substring match between aoFilters (str or re.Pattern)
        and the value name prefixed by sPrefix.

        Returns True if any of the filters matches.
        Returns False if none of the filters matches.
        """
        sFullName = sPrefix + self.sName;
        for oFilter in aoFilters:
            if oFilter.search(sFullName) is not None if isinstance(oFilter, re.Pattern) else sFullName.find(oFilter) >= 0:
                return True;
        return False;

    def canDoBetterCompare(self):
        """
        Checks whether we can do a confident better-than comparsion of the value.
        """
        return self.sUnit is not None  and  self.kdBestByUnit[self.sUnit] not in (-1, 0, 1);

    def getBetterRelation(self):
        """
        Returns +2 if larger values are definintely better.
        Returns +1 if larger values are likely to be better.
        Returns 0 if we have no clue.
        Returns -1 if smaller values are likey to better.
        Returns -2 if smaller values are definitely better.
        """
        if self.sUnit is None:
            return 0;
        return self.kdBestByUnit[self.sUnit];

    @staticmethod
    def valueToInteger(sValue):
        """
        Returns integer (long) represention of lValue.
        Returns None if it cannot be converted to integer.

        Raises an exception if sValue isn't an integer.
        """
        if sValue is None or isinstance(sValue, (int, long)):
            return sValue;
        sValue = sValue.strip();
        if not sValue:
            return None;
        return long(sValue);

    # Manipluation

    def distill(self, aoValues, sMethod):
        """
        Distills the value of the object from values from multiple test runs.
        """
        if not aoValues:
            return self;

        # Everything except the value comes from the first run.
        self.sName      = aoValues[0].sName;
        self.sTimestamp = aoValues[0].sTimestamp;
        self.sUnit      = aoValues[0].sUnit;

        # Find the value to use according to sMethod.
        if len(aoValues) == 1:
            self.lValue = aoValues[0].lValue;
        else:
            alValuesXcptInvalid = [oValue.lValue for oValue in aoValues if oValue.lValue is not None];
            if not alValuesXcptInvalid:
                # No integer result, so just pick the first value whatever it is.
                self.lValue = aoValues[0].lValue;

            elif sMethod == 'best':
                # Pick the best result out of the whole bunch.
                if self.kdBestByUnit[self.sUnit] >= 0:
                    self.lValue = max(alValuesXcptInvalid);
                else:
                    self.lValue = min(alValuesXcptInvalid);

            elif sMethod == 'avg':
                # Calculate the average.
                self.lValue = (sum(alValuesXcptInvalid) + len(alValuesXcptInvalid) // 2) // len(alValuesXcptInvalid);

            else:
                assert False;
                self.lValue = aoValues[0].lValue;

        return self;


    # debug

    def printValue(self, cIndent):
        print('%sValue: name=%s timestamp=%s unit=%s value=%s'
              % (''.ljust(cIndent*2), self.sName, self.sTimestamp, self.sUnit, self.lValue));


class Test(object):
    """
    Nested test result.
    """
    def __init__(self, oParent = None, hsAttrs = None):
        self.aoChildren     = [] # type: list(Test)
        self.aoValues       = [];
        self.oParent        = oParent;
        self.sName          = hsAttrs['name']      if hsAttrs else None;
        self.sStartTS       = hsAttrs['timestamp'] if hsAttrs else None;
        self.sEndTS         = None;
        self.sStatus        = None;
        self.cErrors        = -1;

    def clone(self, oParent = None):
        """
        Returns a deep copy.
        """
        oClone = Test(oParent, {'name': self.sName, 'timestamp': self.sStartTS});

        for oChild in self.aoChildren:
            oClone.aoChildren.append(oChild.clone(oClone));

        for oValue in self.aoValues:
            oClone.aoValues.append(oValue.clone(oClone));

        oClone.sEndTS  = self.sEndTS;
        oClone.sStatus = self.sStatus;
        oClone.cErrors = self.cErrors;
        return oClone;

    # parsing

    def addChild(self, oChild):
        self.aoChildren.append(oChild);
        return oChild;

    def addValue(self, oValue):
        self.aoValues.append(oValue);
        return oValue;

    def __markCompleted(self, sTimestamp):
        """ Sets sEndTS if not already done. """
        if not self.sEndTS:
            self.sEndTS = sTimestamp;

    def markPassed(self, sTimestamp):
        self.__markCompleted(sTimestamp);
        self.sStatus = 'passed';
        self.cErrors = 0;

    def markSkipped(self, sTimestamp):
        self.__markCompleted(sTimestamp);
        self.sStatus = 'skipped';
        self.cErrors = 0;

    def markFailed(self, sTimestamp, cErrors):
        self.__markCompleted(sTimestamp);
        self.sStatus = 'failed';
        self.cErrors = cErrors;

    def markEnd(self, sTimestamp, cErrors):
        self.__markCompleted(sTimestamp);
        if self.sStatus is None:
            self.sStatus = 'failed' if cErrors != 0 else 'end';
            self.cErrors = 0;

    def mergeInIncludedTest(self, oTest):
        """ oTest will be robbed. """
        if oTest is not None:
            for oChild in oTest.aoChildren:
                oChild.oParent = self;
                self.aoChildren.append(oChild);
            for oValue in oTest.aoValues:
                oValue.oTest = self;
                self.aoValues.append(oValue);
            oTest.aoChildren = [];
            oTest.aoValues   = [];

    # debug

    def printTree(self, iLevel = 0):
        print('%sTest: name=%s start=%s end=%s' % (''.ljust(iLevel*2), self.sName, self.sStartTS, self.sEndTS));
        for oChild in self.aoChildren:
            oChild.printTree(iLevel + 1);
        for oValue in self.aoValues:
            oValue.printValue(iLevel + 1);

    # getters / queries

    def getFullNameWorker(self, cSkipUpper):
        if self.oParent is None:
            return (self.sName, 0);
        sName, iLevel = self.oParent.getFullNameWorker(cSkipUpper);
        if iLevel < cSkipUpper:
            sName = self.sName;
        else:
            sName += ', ' + self.sName;
        return (sName, iLevel + 1);

    def getFullName(self, cSkipUpper = 2):
        return self.getFullNameWorker(cSkipUpper)[0];

    def matchFilters(self, aoFilters):
        """
        Checks for any substring match between aoFilters (str or re.Pattern)
        and the full test name.

        Returns True if any of the filters matches.
        Returns False if none of the filters matches.
        """
        sFullName = self.getFullName();
        for oFilter in aoFilters:
            if oFilter.search(sFullName) is not None if isinstance(oFilter, re.Pattern) else sFullName.find(oFilter) >= 0:
                return True;
        return False;

    # manipulation

    def filterTestsWorker(self, asFilters, fReturnOnMatch):
        # depth first
        i = 0;
        while i < len(self.aoChildren):
            if self.aoChildren[i].filterTestsWorker(asFilters, fReturnOnMatch):
                i += 1;
            else:
                self.aoChildren[i].oParent = None;
                del self.aoChildren[i];

        # If we have children, they must've matched up.
        if self.aoChildren:
            return True;
        if self.matchFilters(asFilters):
            return fReturnOnMatch;
        return not fReturnOnMatch;

    def filterTests(self, asFilters):
        """ Keep tests matching asFilters. """
        if asFilters:
            self.filterTestsWorker(asFilters, True);
        return self;

    def filterOutTests(self, asFilters):
        """ Removes tests matching asFilters. """
        if asFilters:
            self.filterTestsWorker(asFilters, False);
        return self;

    def filterValuesWorker(self, asFilters, fKeepWhen):
        # Process children recursively.
        for oChild in self.aoChildren:
            oChild.filterValuesWorker(asFilters, fKeepWhen);

        # Filter our values.
        iValue = len(self.aoValues);
        if iValue > 0:
            sFullname = self.getFullName() + ': ';
            while iValue > 0:
                iValue -= 1;
                if self.aoValues[iValue].matchFilters(sFullname, asFilters) != fKeepWhen:
                    del self.aoValues[iValue];
        return None;

    def filterValues(self, asFilters):
        """ Keep values matching asFilters. """
        if asFilters:
            self.filterValuesWorker(asFilters, True);
        return self;

    def filterOutValues(self, asFilters):
        """ Removes values matching asFilters. """
        if asFilters:
            self.filterValuesWorker(asFilters, False);
        return self;

    def filterOutEmptyLeafTests(self):
        """
        Removes any child tests that has neither values nor sub-tests.
        Returns True if leaf, False if not.
        """
        iChild = len(self.aoChildren);
        while iChild > 0:
            iChild -= 1;
            if self.aoChildren[iChild].filterOutEmptyLeafTests():
                del self.aoChildren[iChild];
        return not self.aoChildren and not self.aoValues;

    @staticmethod
    def calcDurationStatic(sStartTS, sEndTS):
        """
        Returns None the start timestamp is absent or invalid.
        Returns datetime.timedelta otherwise.
        """
        if not sStartTS:
            return None;
        try:
            oStart = utils.parseIsoTimestamp(sStartTS);
        except:
            return None;

        if not sEndTS:
            return datetime.timedelta.max;
        try:
            oEnd   = utils.parseIsoTimestamp(sEndTS);
        except:
            return datetime.timedelta.max;

        return oEnd - oStart;

    def calcDuration(self):
        """
        Returns the duration as a datetime.timedelta object or None if not available.
        """
        return self.calcDurationStatic(self.sStartTS, self.sEndTS);

    def calcDurationAsMicroseconds(self):
        """
        Returns the duration as microseconds or None if not available.
        """
        oDuration = self.calcDuration();
        if not oDuration:
            return None;
        return (oDuration.days * 86400 + oDuration.seconds) * 1000000 + oDuration.microseconds;

    @staticmethod
    def distillTimes(aoTestRuns, sMethod, sStatus):
        """
        Destills the error counts of the tests.
        Returns a (sStartTS, sEndTS) pair.
        """

        #
        # Start by assembling two list of start and end times for all runs that have a start timestamp.
        # Then sort out the special cases where no run has a start timestamp and only a single one has.
        #
        asStartTS = [oRun.sStartTS for oRun in aoTestRuns if oRun.sStartTS];
        if not asStartTS:
            return (None, None);
        asEndTS   = [oRun.sEndTS   for oRun in aoTestRuns if oRun.sStartTS]; # parallel to asStartTS, so we don't check sEndTS.
        if len(asStartTS) == 1:
            return (asStartTS[0], asEndTS[0]);

        #
        # Calculate durations for all runs.
        #
        if sMethod == 'best':
            aoDurations = [Test.calcDurationStatic(oRun.sStartTS, oRun.sEndTS) for oRun in aoTestRuns if oRun.sStatus == sStatus];
            if not aoDurations or aoDurations.count(None) == len(aoDurations):
                aoDurations = [Test.calcDurationStatic(oRun.sStartTS, oRun.sEndTS) for oRun in aoTestRuns];
            if aoDurations.count(None) == len(aoDurations):
                return (asStartTS[0], None);
            oDuration = min([oDuration for oDuration in aoDurations if oDuration is not None]);

        elif sMethod == 'avg':
            print("dbg: 0: sStatus=%s []=%s"
                  % (sStatus, [(Test.calcDurationStatic(oRun.sStartTS, oRun.sEndTS),oRun.sStatus) for oRun in aoTestRuns],));
            aoDurations = [Test.calcDurationStatic(oRun.sStartTS, oRun.sEndTS) for oRun in aoTestRuns if oRun.sStatus == sStatus];
            print("dbg: 1: aoDurations=%s" % (aoDurations,))
            aoDurations = [oDuration for oDuration in aoDurations if oDuration];
            print("dbg: 2: aoDurations=%s" % (aoDurations,))
            if not aoDurations:
                return (asStartTS[0], None);
            aoDurations = [oDuration for oDuration in aoDurations if oDuration < datetime.timedelta.max];
            print("dbg: 3: aoDurations=%s" % (aoDurations,))
            if not aoDurations:
                return (asStartTS[0], None);
            # sum doesn't work on timedelta, so do it manually.
            oDuration = aoDurations[0];
            for i in range(1, len(aoDurations)):
                oDuration += aoDurations[i];
            print("dbg: 5: oDuration=%s" % (aoDurations,))
            oDuration = oDuration / len(aoDurations);
            print("dbg: 6: oDuration=%s" % (aoDurations,))

        else:
            assert False;
            return (asStartTS[0], asEndTS[0]);

        # Check unfinished test.
        if oDuration >= datetime.timedelta.max:
            return (asStartTS[0], None);

        # Calculate and format the end timestamp string.
        oStartTS = utils.parseIsoTimestamp(asStartTS[0]);
        oEndTS   = oStartTS + oDuration;
        return (asStartTS[0], utils.formatIsoTimestamp(oEndTS));

    @staticmethod
    def distillStatus(aoTestRuns, sMethod):
        """
        Destills the status of the tests.
        Returns the status.
        """
        asStatuses = [oRun.sStatus for oRun in aoTestRuns];

        if sMethod == 'best':
            for sStatus in ('passed', 'failed', 'skipped'):
                if sStatus in asStatuses:
                    return sStatus;
            return asStatuses[0];

        if sMethod == 'avg':
            cPassed  = asStatuses.count('passed');
            cFailed  = asStatuses.count('failed');
            cSkipped = asStatuses.count('skipped');
            cEnd     = asStatuses.count('end');
            cNone    = asStatuses.count(None);
            if cPassed >= cFailed and cPassed >= cSkipped and cPassed >= cNone and cPassed >= cEnd:
                return 'passed';
            if cFailed >= cPassed and cFailed >= cSkipped and cFailed >= cNone and cFailed >= cEnd:
                return 'failed';
            if cSkipped >= cPassed and cSkipped >= cFailed and cSkipped >= cNone and cSkipped >= cEnd:
                return 'skipped';
            if cEnd >= cPassed and cEnd >= cFailed and cEnd >= cNone and cEnd >= cSkipped:
                return 'end';
            return None;

        assert False;
        return asStatuses[0];

    @staticmethod
    def distillErrors(aoTestRuns, sMethod):
        """
        Destills the error counts of the tests.
        Returns the status.
        """
        acErrorsXcptNeg = [oRun.cErrors for oRun in aoTestRuns if oRun.cErrors >= 0];

        if sMethod == 'best':
            if acErrorsXcptNeg:
                return min(acErrorsXcptNeg);
        elif sMethod == 'avg':
            if acErrorsXcptNeg:
                return sum(acErrorsXcptNeg) // len(acErrorsXcptNeg);
        else:
            assert False;
        return -1;

    def distill(self, aoTestRuns, sMethod, fDropLoners):
        """
        Distills the test runs into this test.
        """
        #
        # Recurse first (before we create too much state in the stack
        # frame) and do child tests.
        #
        # We copy the child lists of each test run so we can remove tests we've
        # processed from each run and thus make sure we include tests in
        #
        #
        aaoChildren = [list(oRun.aoChildren) for oRun in aoTestRuns];

        # Process the tests for each run.
        for i, _ in enumerate(aaoChildren):
            # Process all tests for the current run.
            while len(aaoChildren[i]) > 0:
                oFirst = aaoChildren[i].pop(0);

                # Build a list of sub-test runs by searching remaining runs by test name.
                aoSameSubTests = [oFirst,];
                for j in range(i + 1, len(aaoChildren)):
                    aoThis = aaoChildren[j];
                    for iThis, oThis in enumerate(aoThis):
                        if oThis.sName == oFirst.sName:
                            del aoThis[iThis];
                            aoSameSubTests.append(oThis);
                            break;

                # Apply fDropLoners.
                if not fDropLoners or len(aoSameSubTests) > 1 or len(aaoChildren) == 1:
                    # Create an empty test and call distill on it with the subtest array, unless
                    # of course that the array only has one member and we can simply clone it.
                    if len(aoSameSubTests) == 1:
                        self.addChild(oFirst.clone(self));
                    else:
                        oSubTest = Test(self);
                        oSubTest.sName = oFirst.sName;
                        oSubTest.distill(aoSameSubTests, sMethod, fDropLoners);
                        self.addChild(oSubTest);
        del aaoChildren;

        #
        # Do values.  Similar approch as for the sub-tests.
        #
        aaoValues = [list(oRun.aoValues) for oRun in aoTestRuns];

        # Process the values for each run.
        for i,_ in enumerate(aaoValues):
            # Process all values for the current run.
            while len(aaoValues[i]) > 0:
                oFirst = aaoValues[i].pop(0);

                # Build a list of values runs by searching remaining runs by value name and unit.
                aoSameValues = [oFirst,];
                for j in range(i + 1, len(aaoValues)):
                    aoThis = aaoValues[j];
                    for iThis, oThis in enumerate(aoThis):
                        if oThis.sName == oFirst.sName and oThis.sUnit == oFirst.sUnit:
                            del aoThis[iThis];
                            aoSameValues.append(oThis);
                            break;

                # Apply fDropLoners.
                if not fDropLoners or len(aoSameValues) > 1 or len(aaoValues) == 1:
                    # Create an empty test and call distill on it with the subtest array, unless
                    # of course that the array only has one member and we can simply clone it.
                    if len(aoSameValues) == 1:
                        self.aoValues.append(oFirst.clone(self));
                    else:
                        oValue = Value(self);
                        oValue.distill(aoSameValues, sMethod);
                        self.aoValues.append(oValue);
        del aaoValues;

        #
        # Distill test properties.
        #
        self.sStatus = self.distillStatus(aoTestRuns, sMethod);
        self.cErrors = self.distillErrors(aoTestRuns, sMethod);
        (self.sStartTS, self.sEndTS) = self.distillTimes(aoTestRuns, sMethod, self.sStatus);
        print("dbg: %s: sStartTS=%s, sEndTS=%s" % (self.sName, self.sStartTS, self.sEndTS));

        return self;


class XmlLogReader(object):
    """
    XML log reader class.
    """

    def __init__(self, sXmlFile):
        self.sXmlFile = sXmlFile;
        self.oRoot    = Test(None, {'name': 'root', 'timestamp': ''});
        self.oTest    = self.oRoot;
        self.iLevel   = 0;
        self.oValue   = None;

    def parse(self):
        try:
            oFile = open(self.sXmlFile, 'rb'); # pylint: disable=consider-using-with
        except:
            traceback.print_exc();
            return False;

        from xml.parsers.expat import ParserCreate
        oParser = ParserCreate();
        oParser.StartElementHandler  = self.handleElementStart;
        oParser.CharacterDataHandler = self.handleElementData;
        oParser.EndElementHandler    = self.handleElementEnd;
        try:
            oParser.ParseFile(oFile);
        except:
            traceback.print_exc();
            oFile.close();
            return False;
        oFile.close();
        return True;

    def handleElementStart(self, sName, hsAttrs):
        #print('%s%s: %s' % (''.ljust(self.iLevel * 2), sName, str(hsAttrs)));
        if sName in ('Test', 'SubTest',):
            self.iLevel += 1;
            self.oTest = self.oTest.addChild(Test(self.oTest, hsAttrs));
        elif sName == 'Value':
            self.oValue = self.oTest.addValue(Value(self.oTest, hsAttrs.get('name'), hsAttrs.get('unit'),
                                                    hsAttrs.get('timestamp'), hsAttrs.get('value')));
        elif sName == 'End':
            self.oTest.markEnd(hsAttrs.get('timestamp'), int(hsAttrs.get('errors', '0')));
        elif sName == 'Passed':
            self.oTest.markPassed(hsAttrs.get('timestamp'));
        elif sName == 'Skipped':
            self.oTest.markSkipped(hsAttrs.get('timestamp'));
        elif sName == 'Failed':
            self.oTest.markFailed(hsAttrs.get('timestamp'), int(hsAttrs['errors']));
        elif sName == 'Include':
            self.handleInclude(hsAttrs);
        else:
            print('Unknown element "%s"' % (sName,));

    def handleElementData(self, sData):
        if self.oValue is not None:
            self.oValue.addData(sData);
        elif sData.strip() != '':
            print('Unexpected data "%s"' % (sData,));
        return True;

    def handleElementEnd(self, sName):
        if sName in ('Test', 'Subtest',):
            self.iLevel -= 1;
            self.oTest = self.oTest.oParent;
        elif sName == 'Value':
            self.oValue = None;
        return True;

    def handleInclude(self, hsAttrs):
        # relative or absolute path.
        sXmlFile = hsAttrs['filename'];
        if not os.path.isabs(sXmlFile):
            sXmlFile = os.path.join(os.path.dirname(self.sXmlFile), sXmlFile);

        # Try parse it.
        oSub = parseTestResult(sXmlFile);
        if oSub is None:
            print('error: failed to parse include "%s"' % (sXmlFile,));
        else:
            # Skip the root and the next level before merging it the subtest and
            # values in to the current test.  The reason for this is that the
            # include is the output of some sub-program we've run and we don't need
            # the extra test level it automatically adds.
            #
            # More benchmark heuristics: Walk down until we find more than one
            # test or values.
            oSub2 = oSub;
            while len(oSub2.aoChildren) == 1 and not oSub2.aoValues:
                oSub2 = oSub2.aoChildren[0];
            if not oSub2.aoValues:
                oSub2 = oSub;
            self.oTest.mergeInIncludedTest(oSub2);
        return True;

def parseTestResult(sXmlFile):
    """
    Parses the test results in the XML.
    Returns result tree.
    Returns None on failure.
    """
    oXlr = XmlLogReader(sXmlFile);
    if oXlr.parse():
        if len(oXlr.oRoot.aoChildren) == 1 and not oXlr.oRoot.aoValues:
            return oXlr.oRoot.aoChildren[0];
        return oXlr.oRoot;
    return None;

