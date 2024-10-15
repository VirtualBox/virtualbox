#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: analyze.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Analyzer CLI.
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
import re;
import os;
import textwrap;
import sys;

# Only the main script needs to modify the path.
try:    __file__                            # pylint: disable=used-before-assignment
except: __file__ = sys.argv[0];
g_ksValidationKitDir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)));
sys.path.append(g_ksValidationKitDir);

# Validation Kit imports.
from analysis import reader
from analysis import reporting


def usage():
    """
    Display usage.
    """
    # Set up the output wrapper.
    try:    cCols = os.get_terminal_size()[0] # since 3.3
    except: cCols = 79;
    oWrapper = textwrap.TextWrapper(width = cCols);

    # Do the outputting.
    print('Tool for comparing test results.');
    print('');
    oWrapper.subsequent_indent = ' ' * (len('usage: ') + 4);
    print(oWrapper.fill('usage: analyze.py [options] [collection-1] -- [collection-2] [-- [collection3] [..]])'))
    oWrapper.subsequent_indent = '';
    print('');
    print(oWrapper.fill('This tool compares two or more result collections, using one as a baseline (first by default) '
                        'and showing how the results in others differs from it.'));
    print('');
    print(oWrapper.fill('The results (XML file) from one or more test runs makes up a collection.  A collection can be '
                        'named using the --name <name> option, or will get a sequential name automatically.  The baseline '
                        'collection will have "(baseline)" appended to its name.'));
    print('');
    print(oWrapper.fill('A test run produces one XML file, either via the testdriver/reporter.py machinery or via the IPRT '
                        'test.cpp code. In the latter case it can be enabled and controlled via IPRT_TEST_FILE.  A collection '
                        'consists of one or more of test runs (i.e. XML result files).  These are combined (aka distilled) '
                        'into a single set of results before comparing them with the others.  The --best and --avg options '
                        'controls how this combining is done.  The need for this is mainly to try counteract some of the '
                        'instability typically found in the restuls.  Just because one test run produces a better result '
                        'after a change does not necessarily mean this will always be the case and that the change was to '
                        'the better, it might just have been regular fluctuations in the test results.'));

    oWrapper.initial_indent    = '      ';
    oWrapper.subsequent_indent = '      ';
    print('');
    print('Options governing combining (distillation):');
    print('  --avg, --average');
    print(oWrapper.fill('Picks the best result by calculating the average values across all the runs.'));
    print('');
    print('  --best');
    print(oWrapper.fill('Picks the best result from all the runs.  For values, this means making guessing what result is '
                        'better based on the unit.  This may not always lead to the right choices.'));
    print(oWrapper.initial_indent + 'Default: --best');

    print('');
    print('Options relating to collections:');
    print('  --name <name>');
    print(oWrapper.fill('Sets the name of the current collection.  By default a collection gets a sequential number.'));
    print('');
    print('  --baseline <num>');
    print(oWrapper.fill('Sets collection given by <num> (0-based) as the baseline collection.'));
    print(oWrapper.initial_indent + 'Default: --baseline 0')

    print('');
    print('Filtering options:');
    print('  --filter-test <substring>');
    print(oWrapper.fill('Exclude tests not containing any of the substrings given via the --filter-test option.  The '
                        'matching is done with full test name, i.e. all parent names are prepended with ", " as separator '
                        '(for example "tstIOInstr, CPUID EAX=1").'));
    print('');
    print('  --filter-test-out <substring>');
    print(oWrapper.fill('Exclude tests containing the given substring.  As with --filter-test, the matching is done against '
                        'the full test name.'));
    print('');
    print('  --filter-value <substring>');
    print(oWrapper.fill('Exclude values not containing any of the substrings given via the --filter-value option.  The '
                        'matching is done against the value name prefixed by the full test name and ": " '
                        '(for example "tstIOInstr, CPUID EAX=1: real mode, CPUID").'));
    print('');
    print('  --filter-value-out <substring>');
    print(oWrapper.fill('Exclude value containing the given substring.  As with --filter-value, the matching is done against '
                        'the value name prefixed by the full test name.'));

    print('');
    print('  --regex-test <expr>');
    print(oWrapper.fill('Same as --filter-test except the substring matching is done via a regular expression.'));
    print('');
    print('  --regex-test-out <expr>');
    print(oWrapper.fill('Same as --filter-test-out except the substring matching is done via a regular expression.'));
    print('');
    print('  --regex-value <expr>');
    print(oWrapper.fill('Same as --filter-value except the substring matching is done via a regular expression.'));
    print('');
    print('  --regex-value-out <expr>');
    print(oWrapper.fill('Same as --filter-value-out except the substring matching is done via a regular expression.'));
    print('');
    print('  --filter-out-empty-leaf-tests');
    print(oWrapper.fill('Removes any leaf tests that are without any values or sub-tests.  This is useful when '
                        'only considering values, especially when doing additional value filtering.'));

    print('');
    print('Analysis options:');
    print('  --pct-same-value <float>');
    print(oWrapper.fill('The threshold at which the percent difference between two values are considered the same '
                        'during analysis.'));
    print(oWrapper.initial_indent + 'Default: --pct-same-value 0.10');

    print('');
    print('Output options:');
    print('  --brief, --verbose');
    print(oWrapper.fill('Whether to omit (--brief) the value for non-baseline runs and just get along with the difference.'));
    print(oWrapper.initial_indent + 'Default: --brief');
    print('');
    print('  --pct <num>, --pct-precision <num>');
    print(oWrapper.fill('Specifies the number of decimal place to use when formatting the difference as percent.'));
    print(oWrapper.initial_indent + 'Default: --pct 2');
    return 1;


class ResultCollection(object):
    """
    One or more test runs that should be merged before comparison.
    """

    def __init__(self, sName):
        self.sName       = sName;
        self.aoTestTrees = []   # type: [Test]
        self.asTestFiles = []   # type: [str] - runs parallel to aoTestTrees
        self.oDistilled  = None # type: Test

    def append(self, sFilename):
        """
        Loads sFilename and appends the result.
        Returns True on success, False on failure.
        """
        oTestTree = reader.parseTestResult(sFilename);
        if oTestTree:
            self.aoTestTrees.append(oTestTree);
            self.asTestFiles.append(sFilename);
            return True;
        return False;

    def isEmpty(self):
        """ Checks if the result is empty. """
        return len(self.aoTestTrees) == 0;

    def filterTests(self, asFilters):
        """
        Keeps all the tests in the test trees sub-string matching asFilters (str or re).
        """
        for oTestTree in self.aoTestTrees:
            oTestTree.filterTests(asFilters);
        return self;

    def filterOutTests(self, asFilters):
        """
        Removes all the tests in the test trees sub-string matching asFilters (str or re).
        """
        for oTestTree in self.aoTestTrees:
            oTestTree.filterOutTests(asFilters);
        return self;

    def filterValues(self, asFilters):
        """
        Keeps all the tests in the test trees sub-string matching asFilters (str or re).
        """
        for oTestTree in self.aoTestTrees:
            oTestTree.filterValues(asFilters);
        return self;

    def filterOutValues(self, asFilters):
        """
        Removes all the tests in the test trees sub-string matching asFilters (str or re).
        """
        for oTestTree in self.aoTestTrees:
            oTestTree.filterOutValues(asFilters);
        return self;

    def filterOutEmptyLeafTests(self):
        """
        Removes all the tests in the test trees that have neither child tests nor values.
        """
        for oTestTree in self.aoTestTrees:
            oTestTree.filterOutEmptyLeafTests();
        return self;

    def distill(self, sMethod, fDropLoners = False):
        """
        Distills the set of test results into a single one by the given method.

        Valid sMethod values:
            - 'best': Pick the best result for each test and value among all the test runs.
            - 'avg':  Calculate the average value among all the test runs.

        When fDropLoners is True, tests and values that only appear in a single test run
        will be discarded.  When False (the default), the lone result will be used.
        """
        assert sMethod in ['best', 'avg'];
        assert not self.oDistilled;

        # If empty, nothing to do.
        if self.isEmpty():
            return None;

        # If there is only a single tree, make a deep copy of it.
        if len(self.aoTestTrees) == 1:
            oDistilled = self.aoTestTrees[0].clone();
        else:

            # Since we don't know if the test runs are all from the same test, we create
            # dummy root tests for each run and use these are the start for the distillation.
            aoDummyInputTests = [];
            for oRun in self.aoTestTrees:
                oDummy = reader.Test();
                oDummy.aoChildren = [oRun,];
                aoDummyInputTests.append(oDummy);

            # Similarly, we end up with a "dummy" root test for the result.
            oDistilled = reader.Test();
            oDistilled.distill(aoDummyInputTests, sMethod, fDropLoners);

            # We can drop this if there is only a single child, i.e. if all runs are for
            # the same test.
            if len(oDistilled.aoChildren) == 1:
                oDistilled = oDistilled.aoChildren[0];

        self.oDistilled = oDistilled;
        return oDistilled;



# matchWithValue hacks.
g_asOptions = [];
g_iOptInd   = 1;
g_sOptArg   = '';

def matchWithValue(sOption):
    """ Matches an option with a value, placing the value in g_sOptArg if it matches. """
    global g_asOptions, g_iOptInd, g_sOptArg;
    sArg = g_asOptions[g_iOptInd];
    if sArg.startswith(sOption):
        if len(sArg) == len(sOption):
            if g_iOptInd + 1 < len(g_asOptions):
                g_iOptInd += 1;
                g_sOptArg  = g_asOptions[g_iOptInd];
                return True;

            print('syntax error: Option %s takes a value!' % (sOption,));
            raise Exception('syntax error: Option %s takes a value!' % (sOption,));

        if sArg[len(sOption)] in ('=', ':'):
            g_sOptArg = sArg[len(sOption) + 1:];
            return True;
    return False;


def main(asArgs):
    """ C style main(). """
    #
    # Parse arguments
    #
    oCurCollection          = ResultCollection('#0');
    aoCollections           = [ oCurCollection, ];
    iBaseline               = 0;
    sDistillationMethod     = 'best';
    fBrief                  = True;
    cPctPrecision           = 2;
    rdPctSameValue          = 0.1;
    asTestFilters           = [];
    asTestOutFilters        = [];
    asValueFilters          = [];
    asValueOutFilters       = [];
    fFilterOutEmptyLeafTest = True;

    global g_asOptions, g_iOptInd, g_sOptArg;
    g_asOptions = asArgs;
    g_iOptInd   = 1;
    while g_iOptInd < len(asArgs):
        sArg      = asArgs[g_iOptInd];
        g_sOptArg = '';
        #print("dbg: g_iOptInd=%s '%s'" % (g_iOptInd, sArg,));

        if sArg.startswith('--help'):
            return usage();

        if matchWithValue('--filter-test'):
            asTestFilters.append(g_sOptArg);
        elif matchWithValue('--filter-test-out'):
            asTestOutFilters.append(g_sOptArg);
        elif matchWithValue('--filter-value'):
            asValueFilters.append(g_sOptArg);
        elif matchWithValue('--filter-value-out'):
            asValueOutFilters.append(g_sOptArg);

        elif matchWithValue('--regex-test'):
            asTestFilters.append(re.compile(g_sOptArg));
        elif matchWithValue('--regex-test-out'):
            asTestOutFilters.append(re.compile(g_sOptArg));
        elif matchWithValue('--regex-value'):
            asValueFilters.append(re.compile(g_sOptArg));
        elif matchWithValue('--regex-value-out'):
            asValueOutFilters.append(re.compile(g_sOptArg));

        elif sArg == '--filter-out-empty-leaf-tests':
            fFilterOutEmptyLeafTest = True;
        elif sArg == '--no-filter-out-empty-leaf-tests':
            fFilterOutEmptyLeafTest = False;

        elif sArg == '--best':
            sDistillationMethod = 'best';
        elif sArg in ('--avg', '--average'):
            sDistillationMethod = 'avg';

        elif sArg == '--brief':
            fBrief = True;
        elif sArg == '--verbose':
            fBrief = False;

        elif matchWithValue('--pct') or matchWithValue('--pct-precision'):
            cPctPrecision = int(g_sOptArg);
        elif matchWithValue('--base') or matchWithValue('--baseline'):
            iBaseline = int(g_sOptArg);

        elif matchWithValue('--pct-same-value'):
            rdPctSameValue = float(g_sOptArg);

        # '--' starts a new collection.  If current one is empty, drop it.
        elif sArg == '--':
            print("dbg: new collection");
            #if oCurCollection.isEmpty():
            #    del aoCollections[-1];
            oCurCollection = ResultCollection("#%s" % (len(aoCollections),));
            aoCollections.append(oCurCollection);

        # Name the current result collection.
        elif matchWithValue('--name'):
            oCurCollection.sName = g_sOptArg;

        # Read in a file and add it to the current data set.
        else:
            if not oCurCollection.append(sArg):
                return 1;
        g_iOptInd += 1;

    #
    # Post argument parsing processing.
    #

    # Drop the last collection if empty.
    if oCurCollection.isEmpty():
        del aoCollections[-1];
    if not aoCollections:
        print("error: No input files given!");
        return 1;

    # Check the baseline value and mark the column as such.
    if iBaseline < 0 or iBaseline > len(aoCollections):
        print("error: specified baseline is out of range: %s, valid range 0 <= baseline < %s"
              % (iBaseline, len(aoCollections),));
        return 1;
    aoCollections[iBaseline].sName += ' (baseline)';

    #
    # Apply filtering before distilling each collection into a single result tree.
    #
    if asTestFilters:
        for oCollection in aoCollections:
            oCollection.filterTests(asTestFilters);
    if asTestOutFilters:
        for oCollection in aoCollections:
            oCollection.filterOutTests(asTestOutFilters);

    if asValueFilters:
        for oCollection in aoCollections:
            oCollection.filterValues(asValueFilters);
    if asValueOutFilters:
        for oCollection in aoCollections:
            oCollection.filterOutValues(asValueOutFilters);

    if fFilterOutEmptyLeafTest:
        for oCollection in aoCollections:
            oCollection.filterOutEmptyLeafTests();

    # Distillation.
    for oCollection in aoCollections:
        oCollection.distill(sDistillationMethod);

    #
    # Produce the report.
    #
    oTable = reporting.RunTable(iBaseline, fBrief, cPctPrecision, rdPctSameValue);
    oTable.populateFromRuns([oCollection.oDistilled for oCollection in aoCollections],
                            [oCollection.sName      for oCollection in aoCollections]);
    print('\n'.join(oTable.formatAsText()));
    return 0;

if __name__ == '__main__':
    sys.exit(main(sys.argv));

