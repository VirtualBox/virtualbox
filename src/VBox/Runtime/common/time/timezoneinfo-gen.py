# -*- coding: utf-8 -*-
# $Id: timezoneinfo-gen.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Generates timezone mapping info from public domain tz data and
simple windows tables.
"""
from __future__ import print_function;

__copyright__ = \
"""
Copyright (C) 2017-2024 Oracle and/or its affiliates.

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

import os;
import sys;
import xml.etree.ElementTree as ElementTree;


class TzWinZoneEntry(object):
    def __init__(self):
        self.sWinName      = None;
        self.sWinTerritory = None;
        self.fWinGolden    = False;
        self.idxWin        = 0;

class TzLinkEntry(TzWinZoneEntry):
    def __init__(self, sLinkNm, sTarget):
        TzWinZoneEntry.__init__(self);
        self.sLinkNm = sLinkNm;
        self.sTarget = sTarget;

class TzZoneOffset(object):
    def __init__(self, asFields):
        self.sOffset = asFields[0]; # GMT offset expression
        self.sRules  = asFields[1] if len(asFields) > 1 and asFields[1] not in [ '-', '' ] else None;
        self.sFormat = asFields[2] if len(asFields) > 2 and asFields[2] not in [ '-', '' ] else None;
        self.sUntil  = asFields[3] if len(asFields) > 3 and asFields[3] not in [ '-', '' ] else None;

class TzZoneEntry(TzWinZoneEntry):
    def __init__(self, sName):
        TzWinZoneEntry.__init__(self);
        self.sName         = sName;
        self.sTerritory    = 'ZZ';
        self.aOffsets      = []; # type: list(TzZoneOffset)

class TzZoneRule(object):
    def __init__(self, sName, sFrom, sTo, sType, sIn, sOn, sAt, sSave, sLetter):
        self.sName   = sName;
        self.sFrom   = sFrom   if sFrom   not in [ '-', '' ] else None;
        self.sTo     = sTo     if sFrom   not in [ '-', '' ] else None;
        self.sType   = sType   if sType   not in [ '-', '' ] else None;
        self.sIn     = sIn     if sIn     not in [ '-', '' ] else None;
        self.sAt     = sAt     if sAt     not in [ '-', '' ] else None;
        self.sSave   = sSave   if sSave   not in [ '-', '' ] else None;
        self.sLetter = sLetter if sLetter not in [ '-', '' ] else None;

def info(sMsg):
    """
    Outputs an informational message to stderr.
    """
    print('info: ' + sMsg, file=sys.stderr);

def warning(sMsg):
    """
    Outputs a warning (to stderr).
    """
    print('warning: ' + sMsg, file=sys.stderr);

def error(sMsg):
    """
    Outputs a warning (to stderr).
    """
    print('error: ' + sMsg, file=sys.stderr);

def readTzDataFile(sFile):
    """ Reads the given data file into memory, stripping comments. """
    oInFile = open(sFile, 'r');
    asLines = oInFile.readlines();
    oInFile.close();
    iLine = 0;
    while iLine < len(asLines):
        offHash = asLines[iLine].find('#');
        if offHash >= 0:
            asLines[iLine] = asLines[iLine][:offHash].rstrip();
        else:
            asLines[iLine] = asLines[iLine].rstrip();
        iLine += 1;
    return asLines;

#
# tzdata structures.
#
g_dZones = {};
g_dRules = {};
g_dLinks = {};

def readTzData(sTzDataDir):
    """
    Reads in the bits we want from tz data.  Assumes 2017b edition.
    """

    #
    # Parse the tzdata files.
    #
    for sFile in [ 'africa', 'antarctica', 'asia', 'australasia', 'europe', 'northamerica', 'southamerica',
                   'pacificnew', 'etcetera', 'backward', 'systemv', 'factory', #'backzone'
                   ]:
        sIn = 'none';
        asLines = readTzDataFile(os.path.join(sTzDataDir, sFile));
        iLine = 0;
        while iLine < len(asLines):
            sLine = asLines[iLine];
            sStrippedLine = sLine.strip(); # Fully stripped version.
            if sStrippedLine:
                asFields = sLine.split();
                try:
                    if sLine.startswith('Zone'): # 'Rule' NAME FROM TO TYPE IN ON AT SAVE LETTER/S
                        sIn = 'Zone';
                        oZone = TzZoneEntry(asFields[1]);
                        if oZone.sName in g_dZones: raise Exception('duplicate: %s' % (oZone.sName,));
                        g_dZones[oZone.sName] = oZone;
                        oZone.aOffsets.append(TzZoneOffset(asFields[2:]));
                    elif sLine.startswith('Rule'): # 'Rule' NAME FROM TO TYPE IN ON AT SAVE LETTER/S
                        oRule = TzZoneRule(asFields[1], asFields[2], asFields[3], asFields[4], asFields[5],
                                           asFields[6], asFields[7], asFields[8], asFields[9]);
                        if oRule.sName not in g_dRules:
                            g_dRules[oRule] = [oRule,];
                        else:
                            g_dRules[oRule].append(oRule);
                    elif sLine.startswith('Link'):
                        if len(asFields) != 3: raise Exception("malformed link: len(asFields) = %d" % (len(asFields)));
                        oLink = TzLinkEntry(asFields[2].strip(), asFields[1].strip());
                        if oLink.sLinkNm not in g_dLinks:
                            g_dLinks[oLink.sLinkNm] = oLink;
                        elif g_dLinks[oLink.sLinkNm].sTarget != oLink.sTarget:
                            warning('duplicate link for %s: new target %s, previous %s'
                                    % (oLink.sLinkNm, oLink.sTarget, g_dLinks[oLink.sLinkNm].sTarget,));
                    elif sIn == 'Zone':
                        oZone.aOffsets.append(TzZoneEntry(asFields[3:]));
                    else:
                        raise Exception('what is this?')
                except Exception as oXcpt:
                    error("line %u in %s: '%s'" % (iLine + 1, sFile, type(oXcpt) if not str(oXcpt) else str(oXcpt),));
                    info("'%s'" % (asLines[iLine],));
                    return 1;
            iLine += 1;

    #
    # Process the country <-> zone mapping file.
    #
    asLines = readTzDataFile(os.path.join(sTzDataDir, 'zone.tab'));
    iLine = 0;
    while iLine < len(asLines):
        sLine = asLines[iLine];
        if sLine and sLine[0] != ' ':
            asFields = sLine.split('\t');
            try:
                sTerritory = asFields[0];
                if len(sTerritory) != 2: raise Exception('malformed country: %s' % (sTerritory,));
                sZone = asFields[2];
                oZone = g_dZones.get(sZone);
                if oZone:
                    if oZone.sTerritory and oZone.sTerritory != 'ZZ':
                        raise Exception('zone %s already have country %s associated with it (setting %s)'
                                        % (sZone, oZone.sTerritory, sTerritory));
                    oZone.sTerritory = sTerritory;
                else:
                    oLink = g_dLinks.get(sZone);
                    if oLink:
                        pass; # ignore country<->link associations for now.
                    else: raise Exception('country zone not found: %s' % (sZone,));

            except Exception as oXcpt:
                error("line %u in %s: '%s'" % (iLine + 1, 'zone.tab', type(oXcpt) if not str(oXcpt) else str(oXcpt),));
                info("'%s'" % (asLines[iLine],));
                return 1;
        iLine += 1;
    return 0


def readWindowsToTzMap(sMapXml):
    """
    Reads the 'common/supplemental/windowsZones.xml' file from http://cldr.unicode.org/.
    """
    oXmlDoc = ElementTree.parse(sMapXml);
    oMap = oXmlDoc.getroot().find('windowsZones').find('mapTimezones');
    # <mapZone other="Line Islands Standard Time" territory="001" type="Pacific/Kiritimati"/>
    for oChild in oMap.findall('mapZone'):
        sTerritory  = oChild.attrib['territory'];
        sWinZone    = oChild.attrib['other'];
        asUnixZones = oChild.attrib['type'].split();
        for sZone in asUnixZones:
            oZone = g_dZones.get(sZone);
            if oZone:
                if oZone.sWinName is None or (oZone.sWinTerritory == '001' and oZone.sWinName == sWinZone):
                    oZone.sWinName      = sWinZone;
                    oZone.sWinTerritory = sTerritory;
                    if sTerritory == '001':
                        oZone.fWinGolden = True;
                else:
                    warning('zone "%s" have more than one windows mapping: %s (%s) and now %s (%s)'
                            % (sZone, oZone.sWinName, oZone.sWinTerritory, sWinZone, sTerritory));
            else:
                oLink = g_dLinks.get(sZone);
                if oLink:
                    if oLink.sWinName is None or (oLink.sWinTerritory == '001' and oLink.sWinName == sWinZone):
                        oLink.sWinName      = sWinZone;
                        oLink.sWinTerritory = sTerritory;
                        if sTerritory == '001':
                            oLink.fWinGolden = True;
                    else:
                        warning('zone-link "%s" have more than one windows mapping: %s (%s) and now %s (%s)'
                                % (sZone, oLink.sWinName, oLink.sWinTerritory, sWinZone, sTerritory));
                else:
                    warning('could not find zone "%s" (for mapping win zone "%s" to) - got the right data sets?'
                            % (sZone, sWinZone));
    return 0;


def readWindowsIndexes(sFile):
    """
    Reads the windows time zone index from the table in the given file and sets idxWin.

    Assumes format: index{tab}name{tab}(GMT{offset}){space}{cities}

    For instance: https://support.microsoft.com/en-gb/help/973627/microsoft-time-zone-index-values
    """
    # Read the file.
    oInFile = open(sFile, "r");
    asLines = oInFile.readlines();
    oInFile.close();

    # Check the header.
    if not asLines[0].startswith('Index'):
        error('expected first line of "%s" to start with "Index"' % (sFile,));
        return 1;
    fHexIndex = asLines[0].find('hex') > 0;
    iLine     = 1;
    while iLine < len(asLines):
        # Parse.
        asFields = asLines[iLine].split('\t');
        try:
            idxWin     = int(asFields[0].strip(), 16 if fHexIndex else 10);
            sWinName   = asFields[1].strip();
            sLocations = ' '.join(asFields[2].split());
            if sWinName.find('(GMT') >= 0:          raise Exception("oops #1");
            if not sLocations.startswith('(GMT'):   raise Exception("oops #2");
            sStdOffset = sLocations[sLocations.find('(') + 1 : sLocations.find(')')].strip().replace(' ','');
            sLocations = sLocations[sLocations.find(')') + 1 : ].strip();
        except Exception as oXcpt:
            error("line %u in %s: '%s'" % (iLine + 1, sFile, type(oXcpt) if not str(oXcpt) else str(oXcpt),));
            info("'%s'" % (asLines[iLine],));
            return 1;

        # Some name adjustments.
        sWinName = sWinName.lower();
        if sWinName.startswith('a.u.s.'):
            sWinName = 'aus' + sWinName[6:];
        elif sWinName.startswith('u.s. '):
            sWinName = 'us ' + sWinName[5:];
        elif sWinName.startswith('s.a. '):
            sWinName = 'sa ' + sWinName[5:];
        elif sWinName.startswith('s.e. '):
            sWinName = 'se ' + sWinName[5:];
        elif sWinName.startswith('pacific s.a. '):
            sWinName = 'pacific sa ' + sWinName[13:];

        # Update zone entries with matching windows names.
        cUpdates = 0;
        for sZone in g_dZones:
            oZone = g_dZones[sZone];
            if oZone.sWinName and oZone.sWinName.lower() == sWinName:
                oZone.idxWin = idxWin;
                cUpdates += 1;
                #info('idxWin=%#x - %s / %s' % (idxWin, oZone.sName, oZone.sWinName,));
        if cUpdates == 0:
            warning('No matching zone found for index zone "%s" (%#x, %s)' % (sWinName, idxWin, sLocations));

        # Advance.
        iLine += 1;
    return 0;

def getPadding(sField, cchWidth):
    """ Returns space padding for the given field string. """
    if len(sField) < cchWidth:
        return ' ' * (cchWidth - len(sField));
    return '';

def formatFields(sName, oZone, oWinZone):
    """ Formats the table fields. """

    # RTTIMEZONEINFO:
    #    const char     *pszUnixName;
    #    const char     *pszWindowsName;
    #    uint8_t         cchUnixName;
    #    uint8_t         cchWindowsName;
    #    char            szCountry[3];
    #    char            szWindowsCountry[3];
    #    uint32_t        idxWindows;
    #    uint32_t        uReserved;

    asFields = [ '"%s"' % sName, ];
    if oWinZone.sWinName:
        asFields.append('"%s"' % oWinZone.sWinName);
    else:
        asFields.append('NULL');

    asFields.append('%u' % (len(sName),));
    if oWinZone.sWinName:
        asFields.append('%u' % (len(oWinZone.sWinName),));
    else:
        asFields.append('0');

    asFields.append('"%s"' % (oZone.sTerritory,));
    if oWinZone.sWinTerritory:
        asFields.append('"%s"' % (oWinZone.sWinTerritory,));
    else:
        asFields.append('""');
    asFields.append('%#010x' % (oWinZone.idxWin,));

    asFlags = [];
    if oWinZone.fWinGolden:
        asFlags.append('RTTIMEZONEINFO_F_GOLDEN');
    if asFlags:
        asFields.append(' | '.join(asFlags));
    else:
        asFields.append('0');
    return asFields;

def produceCode(oDst):
    """
    Produces the tables.
    """

    #
    # Produce the info table.
    #
    aasEntries = [];

    # The straight zones.
    for sZone in g_dZones:
        asFields = formatFields(sZone, g_dZones[sZone], g_dZones[sZone]);
        aasEntries.append(asFields);

    # The links.
    for sZone in g_dLinks:
        oLink = g_dLinks[sZone];
        asFields = formatFields(sZone, g_dZones[oLink.sTarget], oLink);
        aasEntries.append(asFields);

    # Figure field lengths.
    acchFields = [ 2, 2, 2, 2, 4, 4, 10, 1 ];
    for asFields in aasEntries:
        assert len(asFields) == len(acchFields);
        for iField, sField in enumerate(asFields):
            if len(sField) > acchFields[iField]:
                acchFields[iField] = len(sField);

    # Sort the data on zone name.
    aasEntries.sort();

    # Do the formatting.
    oDst.write('/**\n'
               ' * Static time zone mapping info.  Sorted by pszUnixName.\n'
               ' */\n'
               'static const RTTIMEZONEINFO g_aTimeZones[] =\n'
               '{\n');
    for iEntry, asFields in enumerate(aasEntries):
        sLine = '    { ';
        for iField, sField in enumerate(asFields):
            sLine += sField;
            sLine += ', ';
            sLine += getPadding(sField, acchFields[iField]);
        sLine += ' }, /* %#05x */\n' % (iEntry,);
        oDst.write(sLine);
    oDst.write('};\n'
               '\n');

    #
    # Now produce a lookup table for windows time zone names, with indexes into
    # the g_aTimeZone table.
    #
    aasLookup = [];
    for iEntry, asFields in enumerate(aasEntries):
        if asFields[1] != 'NULL':
            aasLookup.append([ asFields[1],   # sWinName
                               -1 if asFields[7].find('RTTIMEZONEINFO_F_GOLDEN') >= 0 else 1,
                               asFields[5],   # sWinTerritory
                               iEntry,
                               asFields[0]]); # sZone
    aasLookup.sort();

    oDst.write('/**\n'
               ' * Windows time zone lookup table.  Sorted by name, golden flag and territory.\n'
               ' */\n'
               'static const uint16_t g_aidxWinTimeZones[] = \n'
               '{\n');
    for asFields in aasLookup:
        sLine  = '    %#05x, /* %s' % (asFields[3], asFields[0][1:-1]);
        sLine += getPadding(asFields[0], acchFields[1]);
        sLine += ' / %s%s' % (asFields[2][1:-1], '+' if asFields[1] < 0 else ' ');
        if len(asFields[2]) == 2:
            sLine += '  ';
        sLine += ' ==>  %s */\n' % (asFields[4][1:-1],)
        oDst.write(sLine);

    oDst.write('};\n'
               '\n');

    return 0;


def main(asArgs):
    """
    C-like main function.
    """
    if len(asArgs) != 4:
        error("Takes exacty three arguments: <ms-index-file> <ms-key-file> <tz-data-dir>");
        return 1;
    sTzDataDir     = asArgs[1];
    sWinToTzMap    = asArgs[2];
    sWinIndexTable = asArgs[3];

    #
    # Read in the data first.
    #
    iRc = readTzData(sTzDataDir);
    if iRc == 0:
        iRc = readWindowsToTzMap(sWinToTzMap);
    if iRc == 0:
        iRc = readWindowsIndexes(sWinIndexTable);
    if iRc == 0:
        #
        # Produce the C table.
        #
        iRc = produceCode(sys.stdout);
    return iRc;

if __name__ == '__main__':
    sys.exit(main(sys.argv));

