# -*- coding: utf-8 -*-
# $Id: tbresp.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager Responses to the TestBox Script.
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


## All test manager actions responses to the testbox include a RESULT field.
ALL_PARAM_RESULT = 'RESULT'

## @name Statuses (returned in ALL_PARAM_RESULT).
## Acknowledgement.
STATUS_ACK  = 'ACK'
## Negative acknowledgement.
STATUS_NACK = 'NACK'
## The testbox is dead, i.e. it no longer exists with the test manager.
# @note Not used by the SIGNON action, but all the rest uses it.
STATUS_DEAD = 'DEAD'
## @}

## @name Command names (returned in ALL_PARAM_RESULT).
# @{
CMD_IDLE               = 'IDLE'
CMD_WAIT               = 'WAIT'
CMD_EXEC               = 'EXEC'
CMD_ABORT              = 'ABORT'
CMD_REBOOT             = 'REBOOT'
CMD_UPGRADE            = 'UPGRADE'
CMD_UPGRADE_AND_REBOOT = 'UPGRADE_AND_REBOOT'
CMD_SPECIAL            = 'SPECIAL'
## @ }

## @name SIGNON parameter names.
# @{
## The TestBox ID.
SIGNON_PARAM_ID     = 'TESTBOX_ID'
## The TestBox name.
SIGNON_PARAM_NAME   = 'TESTBOX_NAME'
## @}


## @name EXEC parameter names
# @{
## The test set id, used for reporting results.
EXEC_PARAM_RESULT_ID        = 'TEST_SET_ID'
## The file to download/copy and unpack into TESTBOX_SCRIPT.
EXEC_PARAM_SCRIPT_ZIPS      = 'SCRIPT_ZIPS'
## The testcase invocation command line (bourne shell style).
EXEC_PARAM_SCRIPT_CMD_LINE  = 'SCRIPT_CMD_LINE'
## The testcase timeout in seconds.
EXEC_PARAM_TIMEOUT          = 'TIMEOUT'
## @}

## @name UPGRADE and @name UPGRADE_AND_REBOOT parameter names.
# @{
## A URL for downloading new version of Test Box Script archive
UPGRADE_PARAM_URL       = 'DOWNLOAD_URL'
## @}

