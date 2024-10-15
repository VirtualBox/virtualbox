# -*- coding: utf-8 -*-
# $Id: wuihlpgraphbase.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager Web-UI - Graph Helpers - Base Class.
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


class WuiHlpGraphBase(object):
    """
    Base class for the Graph helpers.
    """

    ## Set of colors that can be used by child classes to color data series.
    kasColors = \
    [
        '#0000ff', # Blue
        '#00ff00', # Green
        '#ff0000', # Red
        '#000000', # Black

        '#00ffff', # Cyan/Aqua
        '#ff00ff', # Magenta/Fuchsia
        '#ffff00', # Yellow
        '#8b4513', # SaddleBrown

        '#7b68ee', # MediumSlateBlue
        '#ffc0cb', # Pink
        '#bdb76b', # DarkKhaki
        '#008080', # Teal

        '#bc8f8f', # RosyBrown
        '#000080', # Navy(Blue)
        '#dc143c', # Crimson
        '#800080', # Purple

        '#daa520', # Goldenrod
        '#40e0d0', # Turquoise
        '#00bfff', # DeepSkyBlue
        '#c0c0c0', # Silver
    ];


    def __init__(self, sId, oData, oDisp):
        self._sId           = sId;
        self._oData         = oData;
        self._oDisp         = oDisp;
        # Graph output dimensions.
        self._cxGraph       = 1024;
        self._cyGraph       = 448;
        self._cDpiGraph     = 96;
        # Other graph attributes
        self._sTitle        = None;
        self._cPtFont       = 8;

    def headerContent(self):
        """
        Returns content that goes into the HTML header.
        """
        return '';

    def renderGraph(self):
        """
        Renders the graph.
        Returning HTML.
        """
        return '<p>renderGraph needs to be overridden by the child class!</p>';

    def setTitle(self, sTitle):
        """ Sets the graph title. """
        self._sTitle = sTitle;
        return True;

    def setWidth(self, cx):
        """ Sets the graph width. """
        self._cxGraph = cx;
        return True;

    def setHeight(self, cy):
        """ Sets the graph height. """
        self._cyGraph = cy;
        return True;

    def setDpi(self, cDotsPerInch):
        """ Sets the graph DPI. """
        self._cDpiGraph = cDotsPerInch;
        return True;

    def setFontSize(self, cPtFont):
        """ Sets the default font size. """
        self._cPtFont = cPtFont;
        return True;


    @staticmethod
    def calcSeriesColor(iSeries):
        """ Returns a #rrggbb color code for the given series. """
        return WuiHlpGraphBase.kasColors[iSeries % len(WuiHlpGraphBase.kasColors)];
