# -*- coding: utf-8 -*-
# $Id: wuihlpgraph.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager Web-UI - Graph Helpers.
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


class WuiHlpGraphDataTable(object): # pylint: disable=too-few-public-methods
    """
    Data table container.
    """

    class Row(object): # pylint: disable=too-few-public-methods
        """A row."""
        def __init__(self, sGroup, aoValues, asValues = None):
            self.sName    = sGroup;
            self.aoValues = aoValues;
            if asValues is None:
                self.asValues = [str(oVal) for oVal in aoValues];
            else:
                assert len(asValues) == len(aoValues);
                self.asValues = asValues;

    def __init__(self, sGroupLable, asMemberLabels):
        self.aoTable = [ WuiHlpGraphDataTable.Row(sGroupLable, asMemberLabels), ];
        self.fHasStringValues = False;

    def addRow(self, sGroup, aoValues, asValues = None):
        """Adds a row to the data table."""
        if asValues:
            self.fHasStringValues = True;
        self.aoTable.append(WuiHlpGraphDataTable.Row(sGroup, aoValues, asValues));
        return True;

    def getGroupCount(self):
        """Gets the number of data groups (rows)."""
        return len(self.aoTable) - 1;


class WuiHlpGraphDataTableEx(object): # pylint: disable=too-few-public-methods
    """
    Data container for an table/graph with optional error bars on the Y values.
    """

    class DataSeries(object): # pylint: disable=too-few-public-methods
        """
        A data series.

        The aoXValues, aoYValues and aoYErrorBars are parallel arrays, making a
        series of (X,Y,Y-err-above-delta,Y-err-below-delta) points.

        The error bars are optional.
        """
        def __init__(self, sName, aoXValues, aoYValues, asHtmlTooltips = None, aoYErrorBarBelow = None, aoYErrorBarAbove = None):
            self.sName            = sName;
            self.aoXValues        = aoXValues;
            self.aoYValues        = aoYValues;
            self.asHtmlTooltips   = asHtmlTooltips;
            self.aoYErrorBarBelow = aoYErrorBarBelow;
            self.aoYErrorBarAbove = aoYErrorBarAbove;

    def __init__(self, sXUnit, sYUnit):
        self.sXUnit   = sXUnit;
        self.sYUnit   = sYUnit;
        self.aoSeries = [];

    def addDataSeries(self, sName, aoXValues, aoYValues, asHtmlTooltips = None, aoYErrorBarBelow = None, aoYErrorBarAbove = None):
        """Adds an data series to the table."""
        self.aoSeries.append(WuiHlpGraphDataTableEx.DataSeries(sName, aoXValues, aoYValues, asHtmlTooltips,
                                                               aoYErrorBarBelow, aoYErrorBarAbove));
        return True;

    def getDataSeriesCount(self):
        """Gets the number of data series."""
        return len(self.aoSeries);


#
# Dynamically choose implementation.
#
if True: # pylint: disable=using-constant-test
    from testmanager.webui import wuihlpgraphgooglechart        as GraphImplementation;
else:
    try:
        import matplotlib; # pylint: disable=unused-import,import-error,import-error,wrong-import-order
        from testmanager.webui import wuihlpgraphmatplotlib     as GraphImplementation; # pylint: disable=ungrouped-imports
    except:
        from testmanager.webui import wuihlpgraphsimple         as GraphImplementation;

# pylint: disable=invalid-name
WuiHlpBarGraph              = GraphImplementation.WuiHlpBarGraph;
WuiHlpLineGraph             = GraphImplementation.WuiHlpLineGraph;
WuiHlpLineGraphErrorbarY    = GraphImplementation.WuiHlpLineGraphErrorbarY;

