# -*- coding: utf-8 -*-
# $Id: coreconsts.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager - Test Manager Constants (without a more appropriate home).
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

## OS agnostic.
g_ksOsAgnostic      = 'os-agnostic';
## All known OSes, except the agnostic one.
# See KBUILD_OSES in kBuild/header.kmk for reference.
g_kasOses           = ['darwin', 'dos', 'dragonfly', 'freebsd', 'haiku', 'l4', 'linux', 'netbsd', 'nt', 'openbsd', 'os2',
                       'solaris', 'win'];
## All known OSes, including the agnostic one.
# See KBUILD_OSES in kBuild/header.kmk for reference.
g_kasOsesAll        = g_kasOses + [g_ksOsAgnostic,];


## Architecture agnostic.
g_ksCpuArchAgnostic = 'noarch';
## All known CPU architectures, except the agnostic one.
# See KBUILD_ARCHES in kBuild/header.kmk for reference.
g_kasCpuArches      = ['amd64', 'x86', 'sparc32', 'sparc64', 's390', 's390x', 'ppc32', 'ppc64', 'mips32', 'mips64', 'ia64',
                       'hppa32', 'hppa64', 'arm', 'arm64', 'alpha'];
## All known CPU architectures, except the agnostic one.
# See KBUILD_ARCHES in kBuild/header.kmk for reference.
g_kasCpuArchesAll   = g_kasCpuArches + [g_ksCpuArchAgnostic,];

## All known build types
# See KBUILD_TYPE in kBuild/header.kmk for reference.
# @note 'blessed' is a special type used for release builds that has been notarized
#        or attestation signed by the OS vendor.
g_kasBuildTypesAll  = [ 'release', 'strict', 'profile', 'debug', 'asan', 'blessed' ];

## OS and CPU architecture agnostic.
g_ksOsDotArchAgnostic = 'os-agnostic.noarch';
## Combinations of all OSes and CPU architectures, except the two agnostic ones.
# We do some of them by hand to avoid offering too many choices.
g_kasOsDotCpus = \
[
    'darwin.amd64', 'darwin.x86', 'darwin.ppc32', 'darwin.ppc64', 'darwin.arm', 'darwin.arm64',
    'dos.x86',
    'dragonfly.amd64', 'dragonfly.x86',
    'freebsd.amd64', 'freebsd.x86', 'freebsd.sparc64', 'freebsd.ia64', 'freebsd.ppc32', 'freebsd.ppc64',
    'freebsd.arm', 'freebsd.arm64', 'freebsd.mips32', 'freebsd.mips64',
    'haiku.amd64', 'haiku.x86',
    'l4.amd64', 'l4.x86', 'l4.ppc32', 'l4.ppc64', 'l4.arm',
    'nt.amd64',  'nt.x86',  'nt.arm',  'nt.arm64',  'nt.ia64',  'nt.mips32',  'nt.ppc32',  'nt.alpha',
    'win.amd64', 'win.x86', 'win.arm', 'win.arm64', 'win.ia64', 'win.mips32', 'win.ppc32', 'win.alpha',
    'os2.x86',
    'solaris.amd64', 'solaris.x86', 'solaris.sparc32', 'solaris.sparc64',
];
for sOs in g_kasOses:
    if sOs not in ['darwin', 'dos', 'dragonfly', 'freebsd', 'haiku', 'l4', 'nt', 'win', 'os2', 'solaris']:
        for sArch in g_kasCpuArches:
            g_kasOsDotCpus.append(sOs + '.' + sArch);
g_kasOsDotCpus.sort();

## Combinations of all OSes and CPU architectures, including the two agnostic ones.
g_kasOsDotCpusAll = [g_ksOsDotArchAgnostic]
g_kasOsDotCpusAll.extend(g_kasOsDotCpus);
for sOs in g_kasOsesAll:
    g_kasOsDotCpusAll.append(sOs + '.' + g_ksCpuArchAgnostic);
for sArch in g_kasCpuArchesAll:
    g_kasOsDotCpusAll.append(g_ksOsAgnostic + '.' + sArch);
g_kasOsDotCpusAll.sort();

