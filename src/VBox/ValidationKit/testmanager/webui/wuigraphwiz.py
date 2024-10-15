# -*- coding: utf-8 -*-
# $Id: wuigraphwiz.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager WUI - Graph Wizard
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

# Python imports.
import functools;

# Validation Kit imports.
from testmanager.webui.wuimain          import WuiMain;
from testmanager.webui.wuihlpgraph      import WuiHlpLineGraphErrorbarY, WuiHlpGraphDataTableEx;
from testmanager.webui.wuireport        import WuiReportBase;

from common                             import utils, webutils;
from common                             import constants;


class WuiGraphWiz(WuiReportBase):
    """Construct a graph for analyzing test results (values) across builds and testboxes."""

    ## @name Series name parts.
    ## @{
    kfSeriesName_TestBox        = 1;
    kfSeriesName_Product        = 2;
    kfSeriesName_Branch         = 4;
    kfSeriesName_BuildType      = 8;
    kfSeriesName_OsArchs        = 16;
    kfSeriesName_TestCase       = 32;
    kfSeriesName_TestCaseArgs   = 64;
    kfSeriesName_All            = 127;
    ## @}


    def __init__(self, oModel, dParams, fSubReport = False, fnDPrint = None, oDisp = None):
        WuiReportBase.__init__(self, oModel, dParams, fSubReport = fSubReport, fnDPrint = fnDPrint, oDisp = oDisp);

        # Select graph implementation.
        if dParams[WuiMain.ksParamGraphWizImpl] == 'charts':
            from testmanager.webui.wuihlpgraphgooglechart import WuiHlpLineGraphErrorbarY as MyGraph;
            self.oGraphClass = MyGraph;
        elif dParams[WuiMain.ksParamGraphWizImpl] == 'matplotlib':
            from testmanager.webui.wuihlpgraphmatplotlib  import WuiHlpLineGraphErrorbarY as MyGraph;
            self.oGraphClass = MyGraph;
        else:
            self.oGraphClass = WuiHlpLineGraphErrorbarY;


    #
    def _figureSeriesNameBits(self, aoSeries):
        """ Figures out the method (bitmask) to use when naming series. """
        if len(aoSeries) <= 1:
            return WuiGraphWiz.kfSeriesName_TestBox;

        # Start with all and drop unnecessary specs one-by-one.
        fRet = WuiGraphWiz.kfSeriesName_All;

        if [oSrs.idTestBox for oSrs in aoSeries].count(aoSeries[0].idTestBox) == len(aoSeries):
            fRet &= ~WuiGraphWiz.kfSeriesName_TestBox;

        if [oSrs.idBuildCategory for oSrs in aoSeries].count(aoSeries[0].idBuildCategory) == len(aoSeries):
            fRet &= ~WuiGraphWiz.kfSeriesName_Product;
            fRet &= ~WuiGraphWiz.kfSeriesName_Branch;
            fRet &= ~WuiGraphWiz.kfSeriesName_BuildType;
            fRet &= ~WuiGraphWiz.kfSeriesName_OsArchs;
        else:
            if [oSrs.oBuildCategory.sProduct for oSrs in aoSeries].count(aoSeries[0].oBuildCategory.sProduct) == len(aoSeries):
                fRet &= ~WuiGraphWiz.kfSeriesName_Product;
            if [oSrs.oBuildCategory.sBranch for oSrs in aoSeries].count(aoSeries[0].oBuildCategory.sBranch) == len(aoSeries):
                fRet &= ~WuiGraphWiz.kfSeriesName_Branch;
            if [oSrs.oBuildCategory.sType for oSrs in aoSeries].count(aoSeries[0].oBuildCategory.sType) == len(aoSeries):
                fRet &= ~WuiGraphWiz.kfSeriesName_BuildType;

            # Complicated.
            fRet &= ~WuiGraphWiz.kfSeriesName_OsArchs;
            daTestBoxes = {};
            for oSeries in aoSeries:
                if oSeries.idTestBox in daTestBoxes:
                    daTestBoxes[oSeries.idTestBox].append(oSeries);
                else:
                    daTestBoxes[oSeries.idTestBox] = [oSeries,];
            for aoSeriesPerTestBox in daTestBoxes.values():
                if len(aoSeriesPerTestBox) >= 0:
                    asOsArches = aoSeriesPerTestBox[0].oBuildCategory.asOsArches;
                    for i in range(1, len(aoSeriesPerTestBox)):
                        if aoSeriesPerTestBox[i].oBuildCategory.asOsArches != asOsArches:
                            fRet |= WuiGraphWiz.kfSeriesName_OsArchs;
                            break;

        if aoSeries[0].oTestCaseArgs is None:
            fRet &= ~WuiGraphWiz.kfSeriesName_TestCaseArgs;
            if [oSrs.idTestCase for oSrs in aoSeries].count(aoSeries[0].idTestCase) == len(aoSeries):
                fRet &= ~WuiGraphWiz.kfSeriesName_TestCase;
        else:
            fRet &= ~WuiGraphWiz.kfSeriesName_TestCase;
            if [oSrs.idTestCaseArgs for oSrs in aoSeries].count(aoSeries[0].idTestCaseArgs) == len(aoSeries):
                fRet &= ~WuiGraphWiz.kfSeriesName_TestCaseArgs;

        return fRet;

    def _getSeriesNameFromBits(self, oSeries, fBits):
        """ Creates a series name from bits (kfSeriesName_xxx). """
        assert fBits != 0;
        sName = '';

        if fBits & WuiGraphWiz.kfSeriesName_Product:
            if sName: sName += ' / ';
            sName += oSeries.oBuildCategory.sProduct;

        if fBits & WuiGraphWiz.kfSeriesName_Branch:
            if sName: sName += ' / ';
            sName += oSeries.oBuildCategory.sBranch;

        if fBits & WuiGraphWiz.kfSeriesName_BuildType:
            if sName: sName += ' / ';
            sName += oSeries.oBuildCategory.sType;

        if fBits & WuiGraphWiz.kfSeriesName_OsArchs:
            if sName: sName += ' / ';
            sName += ' & '.join(oSeries.oBuildCategory.asOsArches);

        if fBits & WuiGraphWiz.kfSeriesName_TestCaseArgs:
            if sName: sName += ' / ';
            if oSeries.idTestCaseArgs is not None:
                sName += oSeries.oTestCase.sName + ':#' + str(oSeries.idTestCaseArgs);
            else:
                sName += oSeries.oTestCase.sName;
        elif fBits & WuiGraphWiz.kfSeriesName_TestCase:
            if sName: sName += ' / ';
            sName += oSeries.oTestCase.sName;

        if fBits & WuiGraphWiz.kfSeriesName_TestBox:
            if sName: sName += ' / ';
            sName += oSeries.oTestBox.sName;

        return sName;

    def _calcGraphName(self, oSeries, fSeriesName, sSampleName):
        """ Constructs a name for the graph. """
        fGraphName = ~fSeriesName & (  WuiGraphWiz.kfSeriesName_TestBox
                                     | WuiGraphWiz.kfSeriesName_Product
                                     | WuiGraphWiz.kfSeriesName_Branch
                                     | WuiGraphWiz.kfSeriesName_BuildType
                                    );
        sName = self._getSeriesNameFromBits(oSeries, fGraphName);
        if sName: sName += ' - ';
        sName += sSampleName;
        return sName;

    def _calcSampleName(self, oCollection):
        """ Constructs a name for a sample source (collection). """
        if oCollection.sValue is not None:
            asSampleName = [oCollection.sValue, 'in',];
        elif oCollection.sType == self._oModel.ksTypeElapsed:
            asSampleName = ['Elapsed time', 'for', ];
        elif oCollection.sType == self._oModel.ksTypeResult:
            asSampleName = ['Error count', 'for',];
        else:
            return 'Invalid collection type: "%s"' % (oCollection.sType,);

        sTestName = ', '.join(oCollection.asTests if oCollection.asTests[0] else oCollection.asTests[1:]);
        if sTestName == '':
            # Use the testcase name if there is only one for all series.
            if not oCollection.aoSeries:
                return asSampleName[0];
            if len(oCollection.aoSeries) > 1:
                idTestCase = oCollection.aoSeries[0].idTestCase;
                for oSeries in oCollection.aoSeries:
                    if oSeries.idTestCase != idTestCase:
                        return asSampleName[0];
            sTestName = oCollection.aoSeries[0].oTestCase.sName;
        return ' '.join(asSampleName) + ' ' + sTestName;


    def _splitSeries(self, aoSeries):
        """
        Splits the data series (ReportGraphModel.DataSeries) into one or more graphs.

        Returns an array of data series arrays.
        """
        # Must be at least two series for something to be splittable.
        if len(aoSeries) <= 1:
            if not aoSeries:
                return [];
            return [aoSeries,];

        # Split on unit.
        dUnitSeries = {};
        for oSeries in aoSeries:
            if oSeries.iUnit not in dUnitSeries:
                dUnitSeries[oSeries.iUnit] = [];
            dUnitSeries[oSeries.iUnit].append(oSeries);

        # Sort the per-unit series since the build category was only sorted by ID.
        for iUnit in dUnitSeries:
            def mycmp(oSelf, oOther):
                """ __cmp__ like function. """
                iCmp = utils.stricmp(oSelf.oBuildCategory.sProduct, oOther.oBuildCategory.sProduct);
                if iCmp != 0:
                    return iCmp;
                iCmp = utils.stricmp(oSelf.oBuildCategory.sBranch, oOther.oBuildCategory.sBranch);
                if iCmp != 0:
                    return iCmp;
                iCmp = utils.stricmp(oSelf.oBuildCategory.sType, oOther.oBuildCategory.sType);
                if iCmp != 0:
                    return iCmp;
                iCmp = utils.stricmp(oSelf.oTestBox.sName, oOther.oTestBox.sName);
                if iCmp != 0:
                    return iCmp;
                return 0;
            dUnitSeries[iUnit] = sorted(dUnitSeries[iUnit], key = functools.cmp_to_key(mycmp));

        # Split the per-unit series up if necessary.
        cMaxPerGraph = self._dParams[WuiMain.ksParamGraphWizMaxPerGraph];
        aaoRet = [];
        for aoUnitSeries in dUnitSeries.values():
            while len(aoUnitSeries) > cMaxPerGraph:
                aaoRet.append(aoUnitSeries[:cMaxPerGraph]);
                aoUnitSeries = aoUnitSeries[cMaxPerGraph:];
            if aoUnitSeries:
                aaoRet.append(aoUnitSeries);

        return aaoRet;

    def _configureGraph(self, oGraph):
        """
        Configures oGraph according to user parameters and other config settings.

        Returns oGraph.
        """
        oGraph.setWidth(self._dParams[WuiMain.ksParamGraphWizWidth])
        oGraph.setHeight(self._dParams[WuiMain.ksParamGraphWizHeight])
        oGraph.setDpi(self._dParams[WuiMain.ksParamGraphWizDpi])
        oGraph.setErrorBarY(self._dParams[WuiMain.ksParamGraphWizErrorBarY]);
        oGraph.setFontSize(self._dParams[WuiMain.ksParamGraphWizFontSize]);
        if hasattr(oGraph, 'setXkcdStyle'):
            oGraph.setXkcdStyle(self._dParams[WuiMain.ksParamGraphWizXkcdStyle]);

        return oGraph;

    def _generateInteractiveForm(self):
        """
        Generates the HTML for the interactive form.
        Returns (sTopOfForm, sEndOfForm)
        """

        #
        # The top of the form.
        #
        sTop  = '<form action="#" method="get" id="graphwiz-form">\n' \
                ' <input type="hidden" name="%s" value="%s"/>\n' \
                ' <input type="hidden" name="%s" value="%u"/>\n' \
                % ( WuiMain.ksParamAction,                 WuiMain.ksActionGraphWiz,
                    WuiMain.ksParamGraphWizSrcTestSetId,   self._dParams[WuiMain.ksParamGraphWizSrcTestSetId],
                    );

        sTop  += ' <div id="graphwiz-nav">\n';
        sTop  += '  <script type="text/javascript">\n' \
                 '   window.onresize = function(){ return graphwizOnResizeRecalcWidth("graphwiz-nav", "%s"); }\n' \
                 '   window.onload   = function(){ return graphwizOnLoadRememberWidth("graphwiz-nav"); }\n' \
                 '  </script>\n' \
               % ( WuiMain.ksParamGraphWizWidth, );

        #
        # Top: First row.
        #
        sTop  += '  <div id="graphwiz-top-1">\n';

        # time.
        sNow = self._dParams[WuiMain.ksParamEffectiveDate];
        if sNow is None: sNow = '';
        sTop  += '   <div id="graphwiz-time">\n';
        sTop  += '    <label for="%s">Starting:</label>\n' \
                 '    <input type="text" name="%s" id="%s" value="%s" class="graphwiz-time-input"/>\n' \
               % ( WuiMain.ksParamEffectiveDate,
                   WuiMain.ksParamEffectiveDate, WuiMain.ksParamEffectiveDate, sNow, );

        sTop  += '    <input type="hidden" name="%s" value="%u"/>\n' % ( WuiMain.ksParamReportPeriods, 1, );
        sTop  += '    <label for="%s"> Going back:\n' \
                 '    <input type="text" name="%s" id="%s" value="%s" class="graphwiz-period-input"/>\n' \
               % ( WuiMain.ksParamReportPeriodInHours,
                   WuiMain.ksParamReportPeriodInHours, WuiMain.ksParamReportPeriodInHours,
                   utils.formatIntervalHours(self._dParams[WuiMain.ksParamReportPeriodInHours]) );
        sTop  += '   </div>\n';

        # Graph options top row.
        sTop  += '   <div id="graphwiz-top-options-1">\n';

        # graph type.
        sTop  += '    <label for="%s">Graph:</label>\n' \
                 '    <select name="%s" id="%s">\n' \
               % ( WuiMain.ksParamGraphWizImpl, WuiMain.ksParamGraphWizImpl, WuiMain.ksParamGraphWizImpl, );
        for (sImpl, sDesc) in WuiMain.kaasGraphWizImplCombo:
            sTop  += '     <option value="%s"%s>%s</option>\n' \
                   % (sImpl, ' selected' if sImpl == self._dParams[WuiMain.ksParamGraphWizImpl] else '', sDesc);
        sTop  += '    </select>\n';

        # graph size.
        sTop  += '    <label for="%s">Graph size:</label>\n' \
                 '    <input type="text" name="%s" id="%s" value="%s" class="graphwiz-pixel-input"> x\n' \
                 '    <input type="text" name="%s" id="%s" value="%s" class="graphwiz-pixel-input">\n' \
                 '    <label for="%s">Dpi:</label>'\
                 '    <input type="text" name="%s" id="%s" value="%s" class="graphwiz-dpi-input">\n' \
                 '    <button type="button" onclick="%s">Defaults</button>\n' \
               % ( WuiMain.ksParamGraphWizWidth,
                   WuiMain.ksParamGraphWizWidth,  WuiMain.ksParamGraphWizWidth,  self._dParams[WuiMain.ksParamGraphWizWidth],
                   WuiMain.ksParamGraphWizHeight, WuiMain.ksParamGraphWizHeight, self._dParams[WuiMain.ksParamGraphWizHeight],
                   WuiMain.ksParamGraphWizDpi,
                   WuiMain.ksParamGraphWizDpi,    WuiMain.ksParamGraphWizDpi,    self._dParams[WuiMain.ksParamGraphWizDpi],
                   webutils.escapeAttr('return graphwizSetDefaultSizeValues("graphwiz-nav", "%s", "%s", "%s");'
                                       % ( WuiMain.ksParamGraphWizWidth, WuiMain.ksParamGraphWizHeight,
                                           WuiMain.ksParamGraphWizDpi )),
                 );

        sTop  += '   </div>\n'; # (options row 1)

        sTop  += '  </div>\n'; # (end of row 1)

        #
        # Top: Second row.
        #
        sTop  += '  <div id="graphwiz-top-2">\n';

        # Submit
        sFormButton = '<button type="submit">Refresh</button>\n';
        sTop  += '   <div id="graphwiz-top-submit">' + sFormButton + '</div>\n';


        # Options.
        sTop  += '   <div id="graphwiz-top-options-2">\n';

        sTop  += '    <input type="checkbox" name="%s" id="%s" value="1"%s/>\n' \
                 '    <label for="%s">Tabular data</label>\n' \
               % ( WuiMain.ksParamGraphWizTabular, WuiMain.ksParamGraphWizTabular,
                   ' checked' if self._dParams[WuiMain.ksParamGraphWizTabular] else '',
                   WuiMain.ksParamGraphWizTabular);

        if hasattr(self.oGraphClass, 'setXkcdStyle'):
            sTop  += '    <input type="checkbox" name="%s" id="%s" value="1"%s/>\n' \
                     '    <label for="%s">xkcd-style</label>\n' \
                   % ( WuiMain.ksParamGraphWizXkcdStyle, WuiMain.ksParamGraphWizXkcdStyle,
                       ' checked' if self._dParams[WuiMain.ksParamGraphWizXkcdStyle] else '',
                       WuiMain.ksParamGraphWizXkcdStyle);
        elif self._dParams[WuiMain.ksParamGraphWizXkcdStyle]:
            sTop  += '    <input type="hidden" name="%s" id="%s" value="1"/>\n' \
                   % ( WuiMain.ksParamGraphWizXkcdStyle, WuiMain.ksParamGraphWizXkcdStyle, );

        if not hasattr(self.oGraphClass, 'kfNoErrorBarsSupport'):
            sTop  += '    <input type="checkbox" name="%s" id="%s" value="1"%s title="%s"/>\n' \
                     '    <label for="%s">Error bars,</label>\n' \
                     '    <label for="%s">max: </label>\n' \
                     '    <input type="text" name="%s" id="%s" value="%s" class="graphwiz-maxerrorbar-input" title="%s"/>\n' \
                   % ( WuiMain.ksParamGraphWizErrorBarY, WuiMain.ksParamGraphWizErrorBarY,
                       ' checked' if self._dParams[WuiMain.ksParamGraphWizErrorBarY] else '',
                       'Error bars shows some of the max and min results on the Y-axis.',
                       WuiMain.ksParamGraphWizErrorBarY,
                       WuiMain.ksParamGraphWizMaxErrorBarY,
                       WuiMain.ksParamGraphWizMaxErrorBarY, WuiMain.ksParamGraphWizMaxErrorBarY,
                       self._dParams[WuiMain.ksParamGraphWizMaxErrorBarY],
                       'Maximum number of Y-axis error bar per graph. (Too many makes it unreadable.)'
                       );
        else:
            if self._dParams[WuiMain.ksParamGraphWizErrorBarY]:
                sTop += '<input type="hidden" name="%s" id="%s" value="1">\n' \
                      % ( WuiMain.ksParamGraphWizErrorBarY, WuiMain.ksParamGraphWizErrorBarY, );
            sTop += '<input type="hidden" name="%s" id="%s" value="%u">\n' \
                  % ( WuiMain.ksParamGraphWizMaxErrorBarY, WuiMain.ksParamGraphWizMaxErrorBarY,
                      self._dParams[WuiMain.ksParamGraphWizMaxErrorBarY], );

        sTop  += '    <label for="%s">Font size: </label>\n' \
                 '    <input type="text" name="%s" id="%s" value="%s" class="graphwiz-fontsize-input"/>\n' \
               % ( WuiMain.ksParamGraphWizFontSize,
                   WuiMain.ksParamGraphWizFontSize, WuiMain.ksParamGraphWizFontSize,
                   self._dParams[WuiMain.ksParamGraphWizFontSize], );

        sTop  += '    <label for="%s">Data series: </label>\n' \
                 '    <input type="text" name="%s" id="%s" value="%s" class="graphwiz-maxpergraph-input" title="%s"/>\n' \
               % ( WuiMain.ksParamGraphWizMaxPerGraph,
                   WuiMain.ksParamGraphWizMaxPerGraph, WuiMain.ksParamGraphWizMaxPerGraph,
                   self._dParams[WuiMain.ksParamGraphWizMaxPerGraph],
                   'Max data series per graph.' );

        sTop  += '   </div>\n'; # (options row 2)

        sTop  += '  </div>\n'; # (end of row 2)

        sTop  += ' </div>\n'; # end of top.

        #
        # The end of the page selection.
        #
        sEnd = ' <div id="graphwiz-end-selection">\n';

        #
        # Testbox selection
        #
        aidTestBoxes = list(self._dParams[WuiMain.ksParamGraphWizTestBoxIds]);
        sEnd += '  <div id="graphwiz-testboxes" class="graphwiz-end-selection-group">\n' \
                '   <h3>TestBox Selection:</h3>\n' \
                '   <ol class="tmgraph-testboxes">\n';

        # Get a list of eligible testboxes from the DB.
        for oTestBox in self._oModel.getEligibleTestBoxes():
            try:    aidTestBoxes.remove(oTestBox.idTestBox);
            except: sChecked = '';
            else:   sChecked = ' checked';
            sEnd += '   <li><input type="checkbox" name="%s" value="%s" id="gw-tb-%u"%s/>' \
                    '<label for="gw-tb-%u">%s</label></li>\n' \
                  % ( WuiMain.ksParamGraphWizTestBoxIds, oTestBox.idTestBox, oTestBox.idTestBox, sChecked,
                      oTestBox.idTestBox, oTestBox.sName);

        # List testboxes that have been checked in a different period or something.
        for idTestBox in aidTestBoxes:
            oTestBox = self._oModel.oCache.getTestBox(idTestBox);
            sEnd += '   <li><input type="checkbox" name="%s" value="%s" id="gw-tb-%u" checked/>' \
                    '<label for="gw-tb-%u">%s</label></li>\n' \
                  % ( WuiMain.ksParamGraphWizTestBoxIds, oTestBox.idTestBox, oTestBox.idTestBox,
                      oTestBox.idTestBox, oTestBox.sName);

        sEnd += '   </ol>\n' \
                ' </div>\n';

        #
        # Build category selection.
        #
        aidBuildCategories = list(self._dParams[WuiMain.ksParamGraphWizBuildCatIds]);
        sEnd += '  <div id="graphwiz-buildcategories" class="graphwiz-end-selection-group">\n' \
                '   <h3>Build Category Selection:</h3>\n' \
                '   <ol class="tmgraph-buildcategories">\n';
        for oBuildCat in self._oModel.getEligibleBuildCategories():
            try:    aidBuildCategories.remove(oBuildCat.idBuildCategory);
            except: sChecked = '';
            else:   sChecked = ' checked';
            sEnd += '    <li><input type="checkbox" name="%s" value="%s" id="gw-bc-%u" %s/>' \
                    '<label for="gw-bc-%u">%s / %s / %s / %s</label></li>\n' \
                  % ( WuiMain.ksParamGraphWizBuildCatIds, oBuildCat.idBuildCategory, oBuildCat.idBuildCategory, sChecked,
                      oBuildCat.idBuildCategory,
                      oBuildCat.sProduct, oBuildCat.sBranch, oBuildCat.sType, ' & '.join(oBuildCat.asOsArches) );
        assert not aidBuildCategories; # SQL should return all currently selected.

        sEnd += '   </ol>\n' \
                ' </div>\n';

        #
        # Testcase variations.
        #
        sEnd += '  <div id="graphwiz-testcase-variations" class="graphwiz-end-selection-group">\n' \
                '   <h3>Miscellaneous:</h3>\n' \
                '   <ol>';

        sEnd += '    <li>\n' \
                '     <input type="checkbox" id="%s" name="%s" value="1"%s/>\n' \
                '     <label for="%s">Separate by testcase variation.</label>\n' \
                '    </li>\n' \
              % ( WuiMain.ksParamGraphWizSepTestVars, WuiMain.ksParamGraphWizSepTestVars,
                  ' checked' if self._dParams[WuiMain.ksParamGraphWizSepTestVars] else '',
                  WuiMain.ksParamGraphWizSepTestVars );


        sEnd += '    <li>\n' \
                '     <lable for="%s">Test case ID:</label>\n' \
                '     <input type="text" id="%s" name="%s" value="%s" readonly/>\n' \
                '    </li>\n' \
              % ( WuiMain.ksParamGraphWizTestCaseIds,
                  WuiMain.ksParamGraphWizTestCaseIds, WuiMain.ksParamGraphWizTestCaseIds,
                  ','.join([str(i) for i in self._dParams[WuiMain.ksParamGraphWizTestCaseIds]]), );

        sEnd += '   </ol>\n' \
                '  </div>\n';

        #sEnd += '   <h3>&nbsp;</h3>\n';

        #
        # Finish up the form.
        #
        sEnd += '  <div id="graphwiz-end-submit"><p>' + sFormButton + '</p></div>\n';
        sEnd += ' </div>\n' \
                '</form>\n';

        return (sTop, sEnd);

    def generateReportBody(self):
        fInteractive = not self._fSubReport;

        # Quick mockup.
        self._sTitle = 'Graph Wizzard';

        sHtml = '';
        sHtml += '<h2>Incomplete code - no complaints yet, thank you!!</h2>\n';

        #
        # Create a form for altering the data we're working with.
        #
        if fInteractive:
            (sTopOfForm, sEndOfForm) = self._generateInteractiveForm();
            sHtml += sTopOfForm;
            del sTopOfForm;

        #
        # Emit the graphs.  At least one per sample source.
        #
        sHtml += ' <div id="graphwiz-graphs">\n';
        iGraph = 0;
        aoCollections = self._oModel.fetchGraphData();
        for iCollection, oCollection in enumerate(aoCollections):
            # Name the graph and add a checkbox for removing it.
            sSampleName = self._calcSampleName(oCollection);
            sHtml += '  <div class="graphwiz-collection" id="graphwiz-source-%u">\n' % (iCollection,);
            if fInteractive:
                sHtml += '   <div class="graphwiz-src-select">\n' \
                         '    <input type="checkbox" name="%s" id="%s" value="%s:%s%s" checked class="graphwiz-src-input">\n' \
                         '    <label for="%s">%s</label>\n' \
                         '   </div>\n' \
                       % ( WuiMain.ksParamReportSubjectIds, WuiMain.ksParamReportSubjectIds, oCollection.sType,
                           ':'.join([str(idStr) for idStr in oCollection.aidStrTests]),
                           ':%u' % oCollection.idStrValue if oCollection.idStrValue else '',
                           WuiMain.ksParamReportSubjectIds, sSampleName );

            if oCollection.aoSeries:
                #
                # Split the series into sub-graphs as needed and produce SVGs.
                #
                aaoSeries = self._splitSeries(oCollection.aoSeries);
                for aoSeries in aaoSeries:
                    # Gather the data for this graph. (Most big stuff is passed by
                    # reference, so there shouldn't be any large memory penalty for
                    # repacking the data here.)
                    sYUnit = None;
                    if aoSeries[0].iUnit < len(constants.valueunit.g_asNames) and aoSeries[0].iUnit > 0:
                        sYUnit = constants.valueunit.g_asNames[aoSeries[0].iUnit];
                    oData = WuiHlpGraphDataTableEx(sXUnit = 'Build revision', sYUnit = sYUnit);

                    fSeriesName = self._figureSeriesNameBits(aoSeries);
                    for oSeries in aoSeries:
                        sSeriesName = self._getSeriesNameFromBits(oSeries, fSeriesName);
                        asHtmlTooltips = None;
                        if len(oSeries.aoRevInfo) == len(oSeries.aiRevisions):
                            asHtmlTooltips = [];
                            for i, oRevInfo in enumerate(oSeries.aoRevInfo):
                                sPlusMinus = '';
                                if oSeries.acSamples[i] > 1:
                                    sPlusMinus = ' (+%s/-%s; %u samples)' \
                                               % ( utils.formatNumber(oSeries.aiErrorBarAbove[i]),
                                                   utils.formatNumber(oSeries.aiErrorBarBelow[i]),
                                                   oSeries.acSamples[i])
                                sTooltip = '<table class=\'graphwiz-tt\'><tr><td>%s:</td><td>%s %s %s</td></tr>'\
                                           '<tr><td>Rev:</td><td>r%s</td></tr>' \
                                         % ( sSeriesName,
                                             utils.formatNumber(oSeries.aiValues[i]),
                                             sYUnit, sPlusMinus,
                                             oSeries.aiRevisions[i],
                                             );
                                if oRevInfo.sAuthor is not None:
                                    sMsg = oRevInfo.sMessage[:80].strip();
                                    #if sMsg.find('\n') >= 0:
                                    #    sMsg = sMsg[:sMsg.find('\n')].strip();
                                    sTooltip += '<tr><td>Author:</td><td>%s</td></tr>' \
                                                '<tr><td>Date:</td><td>%s</td><tr>' \
                                                '<tr><td>Message:</td><td>%s%s</td></tr>' \
                                              % ( oRevInfo.sAuthor,
                                                  self.formatTsShort(oRevInfo.tsCreated),
                                                  sMsg, '...' if len(oRevInfo.sMessage) > len(sMsg) else '');
                                sTooltip += '</table>';
                                asHtmlTooltips.append(sTooltip);
                        oData.addDataSeries(sSeriesName, oSeries.aiRevisions, oSeries.aiValues, asHtmlTooltips,
                                            oSeries.aiErrorBarBelow, oSeries.aiErrorBarAbove);
                    # Render the data into a graph.
                    oGraph = self.oGraphClass('tmgraph-%u' % (iGraph,), oData, self._oDisp);
                    self._configureGraph(oGraph);

                    oGraph.setTitle(self._calcGraphName(aoSeries[0], fSeriesName, sSampleName));
                    sHtml += '   <div class="graphwiz-graph" id="graphwiz-graph-%u">\n' % (iGraph,);
                    sHtml += oGraph.renderGraph();
                    sHtml += '\n   </div>\n';
                    iGraph += 1;

                #
                # Emit raw tabular data if requested.
                #
                if self._dParams[WuiMain.ksParamGraphWizTabular]:
                    sHtml += '   <div class="graphwiz-tab-div" id="graphwiz-tab-%u">\n' \
                             '    <table class="tmtable graphwiz-tab">\n' \
                           % (iCollection, );
                    for aoSeries in aaoSeries:
                        if aoSeries[0].iUnit < len(constants.valueunit.g_asNames) and aoSeries[0].iUnit > 0:
                            sUnit = constants.valueunit.g_asNames[aoSeries[0].iUnit];
                        else:
                            sUnit = str(aoSeries[0].iUnit);

                        for iSeries, oSeries in enumerate(aoSeries):
                            sColor = self.oGraphClass.calcSeriesColor(iSeries);

                            sHtml += '<thead class="tmheader">\n' \
                                     ' <tr class="graphwiz-tab graphwiz-tab-new-series-row">\n' \
                                     '  <th colspan="5"><span style="background-color:%s;">&nbsp;&nbsp;</span> %s</th>\n' \
                                     ' </tr>\n' \
                                     ' <tr class="graphwiz-tab graphwiz-tab-col-hdr-row">\n' \
                                     '  <th>Revision</th><th>Value (%s)</th><th>&Delta;max</th><th>&Delta;min</th>' \
                                     '<th>Samples</th>\n' \
                                     ' </tr>\n' \
                                     '</thead>\n' \
                                   % ( sColor,
                                       self._getSeriesNameFromBits(oSeries, self.kfSeriesName_All & ~self.kfSeriesName_OsArchs),
                                       sUnit );

                            for i, iRevision in enumerate(oSeries.aiRevisions):
                                sHtml += '     <tr class="%s"><td>r%s</td><td>%s</td><td>+%s</td><td>-%s</td><td>%s</td></tr>\n' \
                                       % ( 'tmodd' if i & 1 else 'tmeven',
                                           iRevision, oSeries.aiValues[i],
                                           oSeries.aiErrorBarAbove[i], oSeries.aiErrorBarBelow[i],
                                           oSeries.acSamples[i]);
                    sHtml += '    </table>\n' \
                             '   </div>\n';
            else:
                sHtml += '<i>No results.</i>\n';
            sHtml += '  </div>\n'
        sHtml += ' </div>\n';

        #
        # Finish the form.
        #
        if fInteractive:
            sHtml += sEndOfForm;

        return sHtml;

