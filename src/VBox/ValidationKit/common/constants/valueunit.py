# -*- coding: utf-8 -*-
# $Id: valueunit.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Value Unit Definititions.

This must correspond 1:1 with include/iprt/test.h and
include/VBox/VMMDevTesting.h.
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



## @name Unit constants.
## Used everywhere.
## @note Using upper case here so we can copy, past and chop from the other
#        headers.
## @{
PCT                     = 0x01;
BYTES                   = 0x02;
BYTES_PER_SEC           = 0x03;
KILOBYTES               = 0x04;
KILOBYTES_PER_SEC       = 0x05;
MEGABYTES               = 0x06;
MEGABYTES_PER_SEC       = 0x07;
PACKETS                 = 0x08;
PACKETS_PER_SEC         = 0x09;
FRAMES                  = 0x0a;
FRAMES_PER_SEC          = 0x0b;
OCCURRENCES             = 0x0c;
OCCURRENCES_PER_SEC     = 0x0d;
CALLS                   = 0x0e;
CALLS_PER_SEC           = 0x0f;
ROUND_TRIP              = 0x10;
SECS                    = 0x11;
MS                      = 0x12;
NS                      = 0x13;
NS_PER_CALL             = 0x14;
NS_PER_FRAME            = 0x15;
NS_PER_OCCURRENCE       = 0x16;
NS_PER_PACKET           = 0x17;
NS_PER_ROUND_TRIP       = 0x18;
INSTRS                  = 0x19;
INSTRS_PER_SEC          = 0x1a;
NONE                    = 0x1b;
PP1K                    = 0x1c;
PP10K                   = 0x1d;
PPM                     = 0x1e;
PPB                     = 0x1f;
TICKS                   = 0x20;
TICKS_PER_CALL          = 0x21;
TICKS_PER_OCCURENCE     = 0x22;
PAGES                   = 0x23;
PAGES_PER_SEC           = 0x24;
TICKS_PER_PAGE          = 0x25;
NS_PER_PAGE             = 0x26;
PS                      = 0x27;
PS_PER_CALL             = 0x28;
PS_PER_FRAME            = 0x29;
PS_PER_OCCURRENCE       = 0x2a;
PS_PER_PACKET           = 0x2b;
PS_PER_ROUND_TRIP       = 0x2c;
PS_PER_PAGE             = 0x2d;
END                     = 0x2e;
## @}


## Translate constant to string.
g_asNames = \
[
    'invalid',          # 0
    '%',
    'bytes',
    'bytes/s',
    'KiB',
    'KiB/s',
    'MiB',
    'MiB/s',
    'packets',
    'packets/s',
    'frames',
    'frames/s',
    'occurrences',
    'occurrences/s',
    'calls',
    'calls/s',
    'roundtrips',
    's',
    'ms',
    'ns',
    'ns/call',
    'ns/frame',
    'ns/occurrences',
    'ns/packet',
    'ns/roundtrips',
    'ins',
    'ins/s',
    '',                 # none
    'pp1k',
    'pp10k',
    'ppm',
    'ppb',
    'ticks',
    'ticks/call',
    'ticks/occ',
    'pages',
    'pages/s',
    'ticks/page',
    'ns/page',
    'ps',
    'ps/call',
    'ps/frame',
    'ps/occurrences',
    'ps/packet',
    'ps/roundtrips',
    'ps/page',
];
assert g_asNames[PP1K] == 'pp1k';
assert g_asNames[NS_PER_PAGE] == 'ns/page';
assert g_asNames[PS_PER_PAGE] == 'ps/page';


## Translation table for XML -> number.
g_kdNameToConst = \
{
    'KB':               KILOBYTES,
    'KB/s':             KILOBYTES_PER_SEC,
    'MB':               MEGABYTES,
    'MB/s':             MEGABYTES_PER_SEC,
    'occurrences':      OCCURRENCES,
    'occurrences/s':    OCCURRENCES_PER_SEC,

};
for i in range(1, len(g_asNames)):
    g_kdNameToConst[g_asNames[i]] = i;

