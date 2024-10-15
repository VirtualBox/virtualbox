# -*- coding: utf-8 -*-
# $Id: vboxcon.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
VirtualBox Constants.

See VBoxConstantWrappingHack for details.
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


# Standard Python imports.
import sys


class VBoxConstantWrappingHack(object):                                         # pylint: disable=too-few-public-methods
    """
    This is a hack to avoid the self.oVBoxMgr.constants.MachineState_Running
    ugliness that forces one into the right margin...  Anyone using this module
    can get to the constants easily by:

        from testdriver import vboxcon
        if self.o.machine.state == vboxcon.MachineState_Running:
            do stuff;

    For our own convenience there's a vboxcon attribute set up in vbox.py,
    class TestDriver which is the basis for the VirtualBox testcases. It takes
    care of setting things up properly through the global variable
    'goHackModuleClass' that refers to the instance of this class(if we didn't
    we'd have to use testdriver.vboxcon.MachineState_Running).
    """
    def __init__(self, oWrapped):
        self.oWrapped = oWrapped;
        self.oVBoxMgr = None;
        self.fpApiVer = 99.0;

    def __getattr__(self, sName):
        # Our self.
        try:
            return getattr(self.oWrapped, sName)
        except AttributeError:
            # The VBox constants.
            if self.oVBoxMgr is None:
                raise;
            try:
                return getattr(self.oVBoxMgr.constants, sName);
            except AttributeError:
                # Do some compatability mappings to keep it working with
                # older versions.
                if self.fpApiVer < 3.3:
                    if sName == 'SessionState_Locked':
                        return getattr(self.oVBoxMgr.constants, 'SessionState_Open');
                    if sName == 'SessionState_Unlocked':
                        return getattr(self.oVBoxMgr.constants, 'SessionState_Closed');
                    if sName == 'SessionState_Unlocking':
                        return getattr(self.oVBoxMgr.constants, 'SessionState_Closing');
                raise;


goHackModuleClass = VBoxConstantWrappingHack(sys.modules[__name__]);                         # pylint: disable=invalid-name
sys.modules[__name__] = goHackModuleClass;

