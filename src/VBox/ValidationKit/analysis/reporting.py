# -*- coding: utf-8 -*-
# $Id: reporting.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Result Report Writer.

This takes a processed test result tree and creates a HTML, re-structured text,
or normal text report from it.
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

# Standard python imports.
import os;
import sys;

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


##################################################################################################################################
#   Run Table                                                                                                                    #
##################################################################################################################################

def alignTextLeft(sText, cchWidth):
    """ Left aligns text and pads it to cchWidth characters length. """
    return sText + ' ' * (cchWidth - min(len(sText), cchWidth));


def alignTextRight(sText, cchWidth):
    """ Right aligns text and pads it to cchWidth characters length. """
    return ' ' * (cchWidth - min(len(sText), cchWidth)) + sText;


def alignTextCenter(sText, cchWidth):
    """ Pads the text equally on both sides to cchWidth characters length. """
    return alignTextLeft(' ' * ((cchWidth - min(len(sText), cchWidth)) // 2) + sText, cchWidth);


g_kiAlignLeft   = -1;
g_kiAlignRight  = 1;
g_kiAlignCenter = 0;
def alignText(sText, cchWidth, iAlignType):
    """
    General alignment method.

    Negative iAlignType for left aligning, zero for entered, and positive for
    right aligning the text.
    """
    if iAlignType < 0:
        return alignTextLeft(sText, cchWidth);
    if iAlignType > 0:
        return alignTextRight(sText, cchWidth);
    return alignTextCenter(sText, cchWidth);


class TextColumnWidth(object):
    """
    Tracking the width of a column, dealing with sub-columns and such.
    """

    def __init__(self):
        self.cch      = 0;
        self.dacchSub = {};

    def update(self, oWidth, cchSubColSpacing = 1):
        """
        Updates the column width tracking with oWidth, which is either
        an int or an array of ints (sub columns).
        """
        if isinstance(oWidth, int):
            self.cch = max(self.cch, oWidth);
        else:
            cSubCols = len(oWidth);
            if cSubCols not in self.dacchSub:
                self.dacchSub[cSubCols] = list(oWidth);
                self.cch = max(self.cch, sum(oWidth) + cchSubColSpacing * (cSubCols - 1));
            else:
                acchSubCols = self.dacchSub[cSubCols];
                for iSub in range(cSubCols):
                    acchSubCols[iSub] = max(acchSubCols[iSub], oWidth[iSub]);
                self.cch = max(self.cch, sum(acchSubCols) + cchSubColSpacing * (cSubCols - 1));

    def finalize(self):
        """ Finalizes sub-column sizes. """
        ## @todo maybe do something here, maybe not...
        return self;

    def hasSubColumns(self):
        """ Checks if there are sub-columns for this column. """
        return not self.dacchSub;

class TextWidths(object):
    """
    Tracks the column widths for text rending of the table.
    """
    def __init__(self, cchSubColSpacing = 1, ):
        self.cchName          = 1;
        self.aoColumns        = [] # type: TextColumnWidth
        self.cchSubColSpacing = cchSubColSpacing;
        self.fFinalized       = False;

    def update(self, aoWidths):
        """ Updates the tracker with the returns of calcColumnWidthsForText. """
        if not aoWidths[0]:
            self.cchName = max(self.cchName, aoWidths[1]);

            for iCol, oWidth in enumerate(aoWidths[2]):
                if iCol >= len(self.aoColumns):
                    self.aoColumns.append(TextColumnWidth());
                self.aoColumns[iCol].update(oWidth, self.cchSubColSpacing);

        return self;

    def finalize(self):
        """ Finalizes sub-column sizes. """
        for oColumnWidth in self.aoColumns:
            oColumnWidth.finalize();
        self.fFinalized = True;
        return self;

    def getColumnWidth(self, iColumn, cSubs = None, iSub = None):
        """ Returns the width of the specified column. """
        if not self.fFinalized:
            return 0;
        assert iColumn < len(self.aoColumns), "iColumn=%s vs %s" % (iColumn, len(self.aoColumns),);
        oColumn = self.aoColumns[iColumn];
        if cSubs is not None:
            assert iSub < cSubs;
            if cSubs != 1:
                assert cSubs in oColumn.dacchSub, \
                       "iColumn=%s cSubs=%s iSub=%s; dacchSub=%s" % (iColumn, cSubs, iSub, oColumn.dacchSub);
                return oColumn.dacchSub[cSubs][iSub];
        return oColumn.cch;


class TextElement(object):
    """
    A text element (cell/sub-cell in a table).
    """

    def __init__(self, sText = '', iAlign = g_kiAlignRight): # type: (str, int) -> None
        self.sText  = sText;
        self.iAlign = iAlign;

    def asText(self, cchWidth): # type: (int) -> str
        """ Pads the text to width of cchWidth characters. """
        return alignText(self.sText, cchWidth, self.iAlign);


class RunRow(object):
    """
    Run table row.
    """

    def __init__(self, iLevel, sName, iRun = 0): # type: (int, str, int) -> None
        self.iLevel     = iLevel;
        self.sName      = sName;
        self.iFirstRun  = iRun;

        # Fields used while formatting (set during construction or calcColumnWidthsForText/Html).
        self.cColumns   = 0;                    ##< Number of columns.
        self.fSkip      = False                 ##< Whether or not to skip this row in the output.

    # Format as Text:

    def formatNameAsText(self, cchWidth): # (int) -> TextElement
        """ Format the row as text. """
        _ = cchWidth;
        return TextElement(' ' * (self.iLevel * 2) + self.sName, g_kiAlignLeft);

    def getColumnCountAsText(self, oTable):
        """
        Called by calcColumnWidthsForText for getting an up-to-date self.cColumns value.
        Override this to update cColumns after construction.
        """
        _ = oTable;
        return self.cColumns;

    def formatColumnAsText(self, iColumn, oTable): # type: (int, RunTable) -> [TextElement]
        """ Returns an array of TextElements for the given column in this row. """
        _ = iColumn; _ = oTable;
        return [ TextElement(),];

    def calcColumnWidthsForText(self, oTable): # type: (RunTable) -> (bool, int, [])
        """
        Calculates the column widths for text rendering.

        Returns a tuple consisting of the fSkip, the formatted name width, and an
        array of column widths.  The entries in the latter are either integer
        widths or arrays of subcolumn integer widths.
        """
        aoRetCols  = [];
        cColumns   = self.getColumnCountAsText(oTable);
        for iColumn in range(cColumns):
            aoSubColumns = self.formatColumnAsText(iColumn, oTable);
            if len(aoSubColumns) == 1:
                aoRetCols.append(len(aoSubColumns[0].sText));
            else:
                aoRetCols.append([len(oSubColumn.sText) for oSubColumn in aoSubColumns]);
        return (False, len(self.formatNameAsText(0).sText), aoRetCols);

    def renderAsText(self, oWidths, oTable): # type: (TextWidths, RunTable) -> str
        """
        Renders the row as text.

        Returns string.
        """
        sRow = self.formatNameAsText(oWidths.cchName).asText(oWidths.cchName);
        sRow = sRow + ' ' * (oWidths.cchName - min(len(sRow), oWidths.cchName)) + ' : ';

        for iColumn in range(self.cColumns):
            aoSubCols = self.formatColumnAsText(iColumn, oTable);
            sCell = '';
            for iSub, oText in enumerate(aoSubCols):
                cchWidth = oWidths.getColumnWidth(iColumn, len(aoSubCols), iSub);
                if iSub > 0:
                    sCell += ' ' * oWidths.cchSubColSpacing;
                sCell += oText.asText(cchWidth);
            cchWidth = oWidths.getColumnWidth(iColumn);
            sRow  += (' | ' if iColumn > 0 else '') + ' ' * (cchWidth - min(cchWidth, len(sCell))) + sCell;

        return sRow;

    @staticmethod
    def formatDiffAsText(lNumber, lBaseline):
        """ Formats the difference between lNumber and lBaseline as text. """
        if lNumber is not None:
            if lBaseline is not None:
                if lNumber < lBaseline:
                    return '-' + utils.formatNumber(lBaseline - lNumber); ## @todo formatter is busted for negative nums.
                if lNumber > lBaseline:
                    return '+' + utils.formatNumber(lNumber - lBaseline);
                return '0';
        return '';

    @staticmethod
    def formatPctAsText(chSign, rdPct, cPctPrecision):
        """ Formats percentage value as text. """
        if rdPct >= 100:
            return '%s%s%%' % (chSign, utils.formatNumber(int(rdPct + 0.5)),);
        if round(rdPct, cPctPrecision) != 0:
            return '%s%.*f%%' % (chSign, cPctPrecision, rdPct,); # %.*f rounds.
        return '~' + chSign + '0.' + '0' * cPctPrecision + '%';

    @staticmethod
    def formatDiffInPctAsText(lNumber, lBaseline, cPctPrecision):
        """ Formats the difference between lNumber and lBaseline in precent as text. """
        if lNumber is not None:
            if lBaseline is not None:
                ## @todo implement cPctPrecision
                if lNumber == lBaseline:
                    return '0.' + '0'*cPctPrecision + '%';

                lDiff  = lNumber - lBaseline;
                chSign = '+';
                if lDiff < 0:
                    lDiff  = -lDiff;
                    chSign = '-';
                return RunRow.formatPctAsText(chSign, lDiff / float(lBaseline) * 100, cPctPrecision);
        return '';


class RunHeaderRow(RunRow):
    """
    Run table header row.
    """
    def __init__(self, sName, asColumns): # type: (str, [str]) -> None
        RunRow.__init__(self, 0, sName);
        self.asColumns = asColumns
        self.cColumns  = len(asColumns);

    def formatColumnAsText(self, iColumn, oTable): # type: (int, RunTable) -> [TextElement]
        return [TextElement(self.asColumns[iColumn], g_kiAlignCenter),];


class RunFooterRow(RunHeaderRow):
    """
    Run table footer row.
    """
    def __init__(self, sName, asColumns):
        RunHeaderRow.__init__(self, sName, asColumns);


class RunSeparatorRow(RunRow):
    """
    Base class for separator rows.
    """
    def __init__(self):
        RunRow.__init__(self, 0, '');

    def calcTableWidthAsText(self, oWidths):
        """ Returns the table width for when rendered as text. """
        cchWidth = oWidths.cchName;
        for oCol in oWidths.aoColumns:
            cchWidth += 3 + oCol.cch;
        return cchWidth;


class RunHeaderSeparatorRow(RunSeparatorRow):
    """
    Run table header separator row.
    """
    def __init__(self):
        RunSeparatorRow.__init__(self);

    def renderAsText(self, oWidths, oTable):
        _ = oTable;
        return '=' * self.calcTableWidthAsText(oWidths);


class RunFooterSeparatorRow(RunHeaderSeparatorRow):
    """
    Run table footer separator row.
    """
    def __init__(self):
        RunHeaderSeparatorRow.__init__(self);


class RunTestRow(RunRow):
    """
    Run table test row.
    """

    def __init__(self, iLevel, oTest, iRun, aoTests = None): # type: (int, reader.Test, int, [reader.Test]) -> None
        RunRow.__init__(self, iLevel, oTest.sName, iRun);
        assert oTest;
        self.oTest = oTest;
        if aoTests is None:
            aoTests = [None for i in range(iRun)];
            aoTests.append(oTest);
        else:
            aoTests= list(aoTests);
        self.aoTests = aoTests

    def isSameTest(self, oTest):
        """ Checks if oTest belongs to this row or not. """
        return oTest.sName == self.oTest.sName;

    def getBaseTest(self, oTable):
        """ Returns the baseline test. """
        oBaseTest = self.aoTests[oTable.iBaseline];
        if not oBaseTest:
            oBaseTest = self.aoTests[self.iFirstRun];
        return oBaseTest;


class RunTestStartRow(RunTestRow):
    """
    Run table start of test row.
    """

    def __init__(self, iLevel, oTest, iRun): # type: (int, reader.Test, int) -> None
        RunTestRow.__init__(self, iLevel, oTest, iRun);

    def renderAsText(self, oWidths, oTable):
        _ = oTable;
        sRet  = self.formatNameAsText(oWidths.cchName).asText(oWidths.cchName);
        sRet += ' : ';
        sRet += ' | '.join(['-' * oCol.cch for oCol in oWidths.aoColumns]);
        return sRet;


class RunTestEndRow(RunTestRow):
    """
    Run table end of test row.
    """

    def __init__(self, oStartRow): # type: (RunTestStartRow) -> None
        RunTestRow.__init__(self, oStartRow.iLevel, oStartRow.oTest, oStartRow.iFirstRun, oStartRow.aoTests);
        self.oStartRow = oStartRow # type: RunTestStartRow

    def getColumnCountAsText(self, oTable):
        self.cColumns = len(self.aoTests);
        return self.cColumns;

    def formatColumnAsText(self, iColumn, oTable):
        oTest = self.aoTests[iColumn];
        if oTest and oTest.sStatus:
            if oTest.cErrors > 0:
                return [ TextElement(oTest.sStatus, g_kiAlignCenter),
                         TextElement(utils.formatNumber(oTest.cErrors) + 'errors') ];
            return [ TextElement(oTest.sStatus, g_kiAlignCenter) ];
        return [ TextElement(), ];


class RunTestEndRow2(RunTestRow):
    """
    Run table 2nd end of test row, this shows the times.
    """

    def __init__(self, oStartRow): # type: (RunTestStartRow) -> None
        RunTestRow.__init__(self, oStartRow.iLevel, oStartRow.oTest, oStartRow.iFirstRun, oStartRow.aoTests);
        self.oStartRow = oStartRow # type: RunTestStartRow

    def formatNameAsText(self, cchWidth):
        _ = cchWidth;
        return TextElement('runtime', g_kiAlignRight);

    def getColumnCountAsText(self, oTable):
        self.cColumns = len(self.aoTests);
        return self.cColumns;

    def formatColumnAsText(self, iColumn, oTable):
        oTest = self.aoTests[iColumn];
        if oTest:
            cUsElapsed = oTest.calcDurationAsMicroseconds();
            if cUsElapsed:
                oBaseTest = self.getBaseTest(oTable);
                if oTest is oBaseTest:
                    return [ TextElement(utils.formatNumber(cUsElapsed)), TextElement('us', g_kiAlignLeft), ];
                cUsElapsedBase = oBaseTest.calcDurationAsMicroseconds();
                aoRet = [
                    TextElement(utils.formatNumber(cUsElapsed)),
                    TextElement(self.formatDiffAsText(cUsElapsed, cUsElapsedBase)),
                    TextElement(self.formatDiffInPctAsText(cUsElapsed, cUsElapsedBase, oTable.cPctPrecision)),
                ];
                return aoRet[1:] if oTable.fBrief else aoRet;
        return [ TextElement(), ];


class RunTestValueAnalysisRow(RunTestRow):
    """
    Run table row with value analysis for a test, see if we have an improvement or not.
    """
    def __init__(self, oStartRow): # type: (RunTestStartRow) -> None
        RunTestRow.__init__(self, oStartRow.iLevel, oStartRow.oTest, oStartRow.iFirstRun, oStartRow.aoTests);
        self.oStartRow = oStartRow # type: RunTestStartRow
        self.cColumns  = len(self.aoTests);

    def formatNameAsText(self, cchWidth):
        _ = cchWidth;
        return TextElement('value analysis', g_kiAlignRight);

    def formatColumnAsText(self, iColumn, oTable):
        oBaseline = self.getBaseTest(oTable);
        oTest     = self.aoTests[iColumn];
        if not oTest or oTest is oBaseline:
            return [TextElement(),];

        #
        # This is a bit ugly, but it means we don't have to re-merge the values.
        #
        cTotal     = 0;
        cBetter    = 0;
        cWorse     = 0;
        cSame      = 0;
        cUncertain = 0;
        rdPctTotal = 0.0;

        iRow = oTable.aoRows.index(self.oStartRow); # ugly
        while iRow < len(oTable.aoRows):
            oRow = oTable.aoRows[iRow];
            if oRow is self:
                break;
            if isinstance(oRow, RunValueRow):
                oValue     = oRow.aoValues[iColumn];
                oBaseValue = oRow.getBaseValue(oTable);
                if oValue is not None and oValue is not oBaseValue:
                    iBetter = oValue.getBetterRelation();
                    if iBetter != 0:
                        lDiff   = oValue.lValue - oBaseValue.lValue;
                        rdPct   = abs(lDiff / float(oBaseValue.lValue) * 100);
                        if rdPct < oTable.rdPctSameValue:
                            cSame      += 1;
                        else:
                            if lDiff > 0 if iBetter > 0 else lDiff < 0:
                                cBetter    += 1;
                                rdPctTotal += rdPct;
                            else:
                                cWorse     += 1;
                                rdPctTotal += -rdPct;
                            cUncertain += 1 if iBetter in (1, -1) else 0;
                        cTotal     += 1;
            iRow += 1;

        #
        # Format the result.
        #
        aoRet = [];
        if not oTable.fBrief:
            sText = u' \u2193%u' % (cWorse,);
            sText = u' \u2248%u' % (cSame,)   + alignTextRight(sText, 4);
            sText =  u'\u2191%u' % (cBetter,) + alignTextRight(sText, 8);
            aoRet = [TextElement(sText),];

        if cSame >= cWorse and cSame >= cBetter:
            sVerdict = 'same';
        elif cWorse >= cSame and cWorse >= cBetter:
            sVerdict = 'worse';
        else:
            sVerdict = 'better';
        if cUncertain > 0:
            sVerdict = 'probably ' + sVerdict;
        aoRet.append(TextElement(sVerdict));

        rdPctAvg = abs(rdPctTotal / cTotal); # Yes, average of the percentages!
        aoRet.append(TextElement(self.formatPctAsText('+' if rdPctTotal >= 0 else '-', rdPctAvg, oTable.cPctPrecision)));

        return aoRet;


class RunValueRow(RunRow):
    """
    Run table value row.
    """

    def __init__(self, iLevel, oValue, iRun): # type: (int, reader.Value, int) -> None
        RunRow.__init__(self, iLevel, oValue.sName, iRun);
        self.oValue   = oValue;
        self.aoValues = [None for i in range(iRun)];
        self.aoValues.append(oValue);

    def isSameValue(self, oValue):
        """ Checks if oValue belongs to this row or not. """
        return oValue.sName == self.oValue.sName and oValue.sUnit == self.oValue.sUnit;

    # Formatting as Text.

    @staticmethod
    def formatOneValueAsText(oValue): # type: (reader.Value) -> str
        """ Formats a value. """
        if not oValue:
            return "N/A";
        return utils.formatNumber(oValue.lValue);

    def getBaseValue(self, oTable):
        """ Returns the base value instance. """
        oBaseValue = self.aoValues[oTable.iBaseline];
        if not oBaseValue:
            oBaseValue = self.aoValues[self.iFirstRun];
        return oBaseValue;

    def getColumnCountAsText(self, oTable):
        self.cColumns = len(self.aoValues);
        return self.cColumns;

    def formatColumnAsText(self, iColumn, oTable):
        oValue     = self.aoValues[iColumn];
        oBaseValue = self.getBaseValue(oTable);
        if oValue is oBaseValue:
            return [ TextElement(self.formatOneValueAsText(oValue)),
                     TextElement(oValue.sUnit, g_kiAlignLeft), ];
        aoRet = [
            TextElement(self.formatOneValueAsText(oValue)),
            TextElement(self.formatDiffAsText(oValue.lValue if oValue else None, oBaseValue.lValue)),
            TextElement(self.formatDiffInPctAsText(oValue.lValue if oValue else None, oBaseValue.lValue, oTable.cPctPrecision))
        ];
        return aoRet[1:] if oTable.fBrief else aoRet;


class RunTable(object):
    """
    Result table.

    This contains one or more test runs as columns.
    """

    def __init__(self, iBaseline = 0, fBrief = True, cPctPrecision = 2, rdPctSameValue = 0.10): # (int, bool, int, float) -> None
        self.asColumns      = []            # type: [str]       ##< Column names.
        self.aoRows         = []            # type: [RunRow]    ##< The table rows.
        self.iBaseline      = iBaseline     # type: int         ##< Which column is the baseline when diffing things.
        self.fBrief         = fBrief        # type: bool        ##< Whether to exclude the numerical values of non-baseline runs.
        self.cPctPrecision  = cPctPrecision # type: int         ##< Number of decimal points in diff percentage value.
        self.rdPctSameValue = rdPctSameValue # type: float      ##< The percent value at which a value difference is considered
                                                                ##  to be the same during value analysis.
    def __populateFromValues(self, aaoValueRuns, iLevel): # type: ([reader.Value]) -> None
        """
        Internal worker for __populateFromRuns()

        This will modify the sub-lists inside aaoValueRuns, returning with them all empty.

        Returns True if an value analysis row should be added, False if not.
        """
        # Same as for __populateFromRuns, only no recursion.
        fAnalysisRow = False;
        for iValueRun, aoValuesForRun in enumerate(aaoValueRuns):
            while aoValuesForRun:
                oRow = RunValueRow(iLevel, aoValuesForRun.pop(0), iValueRun);
                self.aoRows.append(oRow);

                # Pop matching values from the other runs of this test.
                for iOtherRun in range(iValueRun + 1, len(aaoValueRuns)):
                    aoValuesForOtherRun = aaoValueRuns[iOtherRun];
                    for iValueToPop, oOtherValue in enumerate(aoValuesForOtherRun):
                        if oRow.isSameValue(oOtherValue):
                            oRow.aoValues.append(aoValuesForOtherRun.pop(iValueToPop));
                            break;
                    if len(oRow.aoValues) <= iOtherRun:
                        oRow.aoValues.append(None);

                fAnalysisRow = fAnalysisRow or oRow.oValue.canDoBetterCompare();
        return fAnalysisRow;

    def __populateFromRuns(self, aaoTestRuns, iLevel): # type: ([reader.Test]) -> None
        """
        Internal worker for populateFromRuns()

        This will modify the sub-lists inside aaoTestRuns, returning with them all empty.
        """

        #
        # Currently doing depth first, so values are always at the end.
        # Nominally, we should inject values according to the timestamp.
        # However, that's too much work right now and can be done later if needed.
        #
        for iRun, aoTestForRun in enumerate(aaoTestRuns):
            while aoTestForRun:
                # Pop the next test and create a start-test row for it.
                oStartRow = RunTestStartRow(iLevel, aoTestForRun.pop(0), iRun);
                self.aoRows.append(oStartRow);

                # Pop matching tests from the other runs.
                for iOtherRun in range(iRun + 1, len(aaoTestRuns)):
                    aoOtherTestRun = aaoTestRuns[iOtherRun];
                    for iTestToPop, oOtherTest in enumerate(aoOtherTestRun):
                        if oStartRow.isSameTest(oOtherTest):
                            oStartRow.aoTests.append(aoOtherTestRun.pop(iTestToPop));
                            break;
                    if len(oStartRow.aoTests) <= iOtherRun:
                        oStartRow.aoTests.append(None);

                # Now recursively do the subtests for it and then do the values.
                self.__populateFromRuns(  [list(oTest.aoChildren) if oTest else list() for oTest in oStartRow.aoTests], iLevel+1);
                fValueAnalysisRow = self.__populateFromValues([list(oTest.aoValues)
                                                                  if oTest else list() for oTest in oStartRow.aoTests], iLevel+1);

                # Add the end-test row for it.
                self.aoRows.append(RunTestEndRow(oStartRow));
                self.aoRows.append(RunTestEndRow2(oStartRow));
                if fValueAnalysisRow:
                    self.aoRows.append(RunTestValueAnalysisRow(oStartRow));

        return self;

    def populateFromRuns(self, aoTestRuns, asRunNames = None): # type: ([reader.Test], [str]) -> RunTable
        """
        Populates the table from the series of runs.

        The aoTestRuns and asRunNames run in parallel.  If the latter isn't
        given, the names will just be ordinals starting with #0 for the
        first column.

        Returns self.
        """
        #
        # Deal with the column names first.
        #
        if asRunNames:
            self.asColumns = list(asRunNames);
        else:
            self.asColumns = [];
        iCol = len(self.asColumns);
        while iCol < len(aoTestRuns):
            self.asColumns.append('#%u%s' % (iCol, ' (baseline)' if iCol == self.iBaseline else '',));

        self.aoRows = [
            RunHeaderSeparatorRow(),
            RunHeaderRow('Test / Value', self.asColumns),
            RunHeaderSeparatorRow(),
        ];

        #
        # Now flatten the test trees into a table.
        #
        self.__populateFromRuns([[oTestRun,] for oTestRun in aoTestRuns], 0);

        #
        # Add a footer if there are a lot of rows.
        #
        if len(self.aoRows) - 2 > 40:
            self.aoRows.extend([RunFooterSeparatorRow(), RunFooterRow('', self.asColumns),]);

        return self;

    #
    # Text formatting.
    #

    def formatAsText(self):
        """
        Formats the table as text.

        Returns a string array of the output lines.
        """

        #
        # Pass 1: Calculate column widths.
        #
        oWidths = TextWidths(1);
        for oRow in self.aoRows:
            oWidths.update(oRow.calcColumnWidthsForText(self));
        oWidths.finalize();

        #
        # Pass 2: Generate the output strings.
        #
        asRet = [];
        for oRow in self.aoRows:
            if not oRow.fSkip:
                asRet.append(oRow.renderAsText(oWidths, self));

        return asRet;

