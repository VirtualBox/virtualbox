# -*- coding: utf-8 -*-
# $Id: wuihlpgraphmatplotlib.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager Web-UI - Graph Helpers - Implemented using matplotlib.
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

# Standard Python Import and extensions installed on the system.
import re;
import sys;
if sys.version_info[0] >= 3:
    from io       import StringIO as StringIO;  # pylint: disable=import-error,no-name-in-module,useless-import-alias
else:
    from StringIO import StringIO as StringIO;  # pylint: disable=import-error,no-name-in-module,useless-import-alias

import matplotlib;                              # pylint: disable=import-error
matplotlib.use('Agg'); # Force backend.
import matplotlib.pyplot;                       # pylint: disable=import-error
from numpy import arange as numpy_arange;       # pylint: disable=no-name-in-module,import-error,wrong-import-order

# Validation Kit imports.
from testmanager.webui.wuihlpgraphbase  import WuiHlpGraphBase;


class WuiHlpGraphMatplotlibBase(WuiHlpGraphBase):
    """ Base class for the matplotlib graphs. """

    def __init__(self, sId, oData, oDisp):
        WuiHlpGraphBase.__init__(self, sId, oData, oDisp);
        self._fXkcdStyle    = True;

    def setXkcdStyle(self, fEnabled = True):
        """ Enables xkcd style graphs for implementations that supports it. """
        self._fXkcdStyle = fEnabled;
        return True;

    def _createFigure(self):
        """
        Wrapper around matplotlib.pyplot.figure that feeds the figure the
        basic graph configuration.
        """
        if self._fXkcdStyle and matplotlib.__version__ > '1.2.9':
            matplotlib.pyplot.xkcd();           # pylint: disable=no-member
        matplotlib.rcParams.update({'font.size': self._cPtFont});

        oFigure = matplotlib.pyplot.figure(figsize = (float(self._cxGraph) / self._cDpiGraph,
                                                      float(self._cyGraph) / self._cDpiGraph),
                                           dpi = self._cDpiGraph);
        return oFigure;

    def _produceSvg(self, oFigure, fTightLayout = True):
        """ Creates an SVG string from the given figure. """
        oOutput = StringIO();
        if fTightLayout:
            oFigure.tight_layout();
        oFigure.savefig(oOutput, format = 'svg');

        if self._oDisp and self._oDisp.isBrowserGecko('20100101'):
            # This browser will stretch images to fit if no size or width is given.
            sSubstitute = r'\1 \3 reserveAspectRatio="xMidYMin meet"';
        else:
            # Chrome and IE likes to have the sizes as well as the viewBox.
            sSubstitute = r'\1 \3 reserveAspectRatio="xMidYMin meet" \2 \4';
        return re.sub(r'(<svg) (height="\d+pt") (version="\d+.\d+" viewBox="\d+ \d+ \d+ \d+") (width="\d+pt")',
                      sSubstitute,
                      oOutput.getvalue().decode('utf8'),
                      count = 1);

class WuiHlpBarGraph(WuiHlpGraphMatplotlibBase):
    """
    Bar graph.
    """

    def __init__(self, sId, oData, oDisp = None):
        WuiHlpGraphMatplotlibBase.__init__(self, sId, oData, oDisp);
        self.fpMax      = None;
        self.fpMin      = 0.0;
        self.cxBarWidth = None;

    def setRangeMax(self, fpMax):
        """ Sets the max range."""
        self.fpMax = float(fpMax);
        return None;

    def invertYDirection(self):
        """ Inverts the direction of the Y-axis direction. """
        ## @todo self.fYInverted = True;
        return None;

    def renderGraph(self): # pylint: disable=too-many-locals
        aoTable  = self._oData.aoTable;

        #
        # Extract/structure the required data.
        #
        aoSeries = [];
        for j in range(len(aoTable[1].aoValues)):
            aoSeries.append([]);
        asNames  = [];
        oXRange  = numpy_arange(self._oData.getGroupCount());
        fpMin = self.fpMin;
        fpMax = self.fpMax;
        if self.fpMax is None:
            fpMax = float(aoTable[1].aoValues[0]);

        for i in range(1, len(aoTable)):
            asNames.append(aoTable[i].sName);
            for j, oValue in enumerate(aoTable[i].aoValues):
                fpValue = float(oValue);
                aoSeries[j].append(fpValue);
                if fpValue < fpMin:
                    fpMin = fpValue;
                if fpValue > fpMax:
                    fpMax = fpValue;

        fpMid = fpMin + (fpMax - fpMin) / 2.0;

        if self.cxBarWidth is None:
            self.cxBarWidth = 1.0 / (len(aoTable[0].asValues) + 1.1);

        # Render the PNG.
        oFigure = self._createFigure();
        oSubPlot = oFigure.add_subplot(1, 1, 1);

        aoBars = [];
        for i, oValue in enumerate(aoSeries):
            sColor = self.calcSeriesColor(i);
            aoBars.append(oSubPlot.bar(oXRange + self.cxBarWidth * i,
                                       oValue,
                                       self.cxBarWidth,
                                       color = sColor,
                                       align = 'edge'));

        #oSubPlot.set_title('Title')
        #oSubPlot.set_xlabel('X-axis')
        #oSubPlot.set_xticks(oXRange + self.cxBarWidth);
        oSubPlot.set_xticks(oXRange);
        oLegend = oSubPlot.legend(aoTable[0].asValues, loc = 'best', fancybox = True);
        oLegend.get_frame().set_alpha(0.5);
        oSubPlot.set_xticklabels(asNames, ha = "left");
        #oSubPlot.set_ylabel('Y-axis')
        oSubPlot.set_yticks(numpy_arange(fpMin, fpMax + (fpMax - fpMin) / 10 * 0, fpMax / 10));
        oSubPlot.grid(True);
        fpPadding = (fpMax - fpMin) * 0.02;
        for i, aoRects in enumerate(aoBars):
            for j, oRect in enumerate(aoRects):
                fpValue = float(aoTable[j + 1].aoValues[i]);
                if fpValue <= fpMid:
                    oSubPlot.text(oRect.get_x() + oRect.get_width() / 2.0,
                                  oRect.get_height() + fpPadding,
                                  aoTable[j + 1].asValues[i],
                                  ha = 'center', va = 'bottom', rotation = 'vertical', alpha = 0.6, fontsize = 'small');
                else:
                    oSubPlot.text(oRect.get_x() + oRect.get_width() / 2.0,
                                  oRect.get_height() - fpPadding,
                                  aoTable[j + 1].asValues[i],
                                  ha = 'center', va = 'top', rotation = 'vertical', alpha = 0.6, fontsize = 'small');

        return self._produceSvg(oFigure);




class WuiHlpLineGraph(WuiHlpGraphMatplotlibBase):
    """
    Line graph.
    """

    def __init__(self, sId, oData, oDisp = None, fErrorBarY = False):
        # oData must be a WuiHlpGraphDataTableEx like object.
        WuiHlpGraphMatplotlibBase.__init__(self, sId, oData, oDisp);
        self._cMaxErrorBars = 12;
        self._fErrorBarY    = fErrorBarY;

    def setErrorBarY(self, fEnable):
        """ Enables or Disables error bars, making this work like a line graph. """
        self._fErrorBarY = fEnable;
        return True;

    def renderGraph(self): # pylint: disable=too-many-locals
        aoSeries = self._oData.aoSeries;

        oFigure = self._createFigure();
        oSubPlot = oFigure.add_subplot(1, 1, 1);
        if self._oData.sYUnit is not None:
            oSubPlot.set_ylabel(self._oData.sYUnit);
        if self._oData.sXUnit is not None:
            oSubPlot.set_xlabel(self._oData.sXUnit);

        cSeriesNames = 0;
        cYMin = 1000;
        cYMax = 0;
        for iSeries, oSeries in enumerate(aoSeries):
            sColor = self.calcSeriesColor(iSeries);
            cYMin  = min(cYMin, min(oSeries.aoYValues));
            cYMax  = max(cYMax, max(oSeries.aoYValues));
            if not self._fErrorBarY:
                oSubPlot.errorbar(oSeries.aoXValues, oSeries.aoYValues, color = sColor);
            elif len(oSeries.aoXValues) > self._cMaxErrorBars:
                if matplotlib.__version__ < '1.3.0':
                    oSubPlot.errorbar(oSeries.aoXValues, oSeries.aoYValues, color = sColor);
                else:
                    oSubPlot.errorbar(oSeries.aoXValues, oSeries.aoYValues,
                                      yerr = [oSeries.aoYErrorBarBelow, oSeries.aoYErrorBarAbove],
                                      errorevery = len(oSeries.aoXValues) / self._cMaxErrorBars,
                                      color = sColor );
            else:
                oSubPlot.errorbar(oSeries.aoXValues, oSeries.aoYValues,
                                  yerr = [oSeries.aoYErrorBarBelow, oSeries.aoYErrorBarAbove],
                                  color = sColor);
            cSeriesNames += oSeries.sName is not None;

        if cYMin != 0 or cYMax != 0:
            oSubPlot.set_ylim(bottom = 0);

        if cSeriesNames > 0:
            oLegend = oSubPlot.legend([oSeries.sName for oSeries in aoSeries], loc = 'best', fancybox = True);
            oLegend.get_frame().set_alpha(0.5);

        if self._sTitle is not None:
            oSubPlot.set_title(self._sTitle);

        if self._cxGraph >= 256:
            oSubPlot.minorticks_on();
            oSubPlot.grid(True, 'major', axis = 'both');
            oSubPlot.grid(True, 'both', axis = 'x');

        if True: # pylint: disable=using-constant-test
            #    oSubPlot.axis('off');
            #oSubPlot.grid(True, 'major', axis = 'none');
            #oSubPlot.grid(True, 'both', axis = 'none');
            matplotlib.pyplot.setp(oSubPlot, xticks = [], yticks = []);

        return self._produceSvg(oFigure);


class WuiHlpLineGraphErrorbarY(WuiHlpLineGraph):
    """
    Line graph with an errorbar for the Y axis.
    """

    def __init__(self, sId, oData, oDisp = None):
        WuiHlpLineGraph.__init__(self, sId, oData, fErrorBarY = True);


class WuiHlpMiniSuccessRateGraph(WuiHlpGraphMatplotlibBase):
    """
    Mini rate graph.
    """

    def __init__(self, sId, oData, oDisp = None):
        """
        oData must be a WuiHlpGraphDataTableEx like object, but only aoSeries,
        aoSeries[].aoXValues, and aoSeries[].aoYValues will be used.  The
        values are expected to be a percentage, i.e. values between 0 and 100.
        """
        WuiHlpGraphMatplotlibBase.__init__(self, sId, oData, oDisp);
        self.setFontSize(6);

    def renderGraph(self): # pylint: disable=too-many-locals
        assert len(self._oData.aoSeries) == 1;
        oSeries = self._oData.aoSeries[0];

        # hacking
        #self.setWidth(512);
        #self.setHeight(128);
        # end

        oFigure = self._createFigure();
        from mpl_toolkits.axes_grid.axislines import SubplotZero; # pylint: disable=import-error
        oAxis = SubplotZero(oFigure, 111);
        oFigure.add_subplot(oAxis);

        # Disable all the normal axis.
        oAxis.axis['right'].set_visible(False)
        oAxis.axis['top'].set_visible(False)
        oAxis.axis['bottom'].set_visible(False)
        oAxis.axis['left'].set_visible(False)

        # Use the zero axis instead.
        oAxis.axis['yzero'].set_axisline_style('-|>');
        oAxis.axis['yzero'].set_visible(True);
        oAxis.axis['xzero'].set_axisline_style('-|>');
        oAxis.axis['xzero'].set_visible(True);

        if oSeries.aoYValues[-1] == 100:
            sColor = 'green';
        elif oSeries.aoYValues[-1] > 75:
            sColor = 'yellow';
        else:
            sColor = 'red';
        oAxis.plot(oSeries.aoXValues, oSeries.aoYValues, '.-', color = sColor, linewidth = 3);
        oAxis.fill_between(oSeries.aoXValues, oSeries.aoYValues, facecolor = sColor, alpha = 0.5)

        oAxis.set_xlim(left = -0.01);
        oAxis.set_xticklabels([]);
        oAxis.set_xmargin(1);

        oAxis.set_ylim(bottom = 0, top = 100);
        oAxis.set_yticks([0, 50, 100]);
        oAxis.set_ylabel('%');
        #oAxis.set_yticklabels([]);
        oAxis.set_yticklabels(['', '%', '']);

        return self._produceSvg(oFigure, False);

