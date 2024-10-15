# -*- coding: utf-8 -*-
# $Id: tbreq.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager Requests from the TestBox Script.
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


## @name Test Manager actions
# @{
## TestBox sign-on.
SIGNON                  = 'SIGNON'
## TestBox request for a command while busy (EXEC).
REQUEST_COMMAND_BUSY    = 'REQUEST_COMMAND_BUSY'
## TestBox request for a command while idling.
REQUEST_COMMAND_IDLE    = 'REQUEST_COMMAND_IDLE'
## TestBox ACKs a command.
COMMAND_ACK             = 'COMMAND_ACK'
## TestBox NACKs a command.
COMMAND_NACK            = 'COMMAND_NACK'
## TestBox NACKs an unsupported command.
COMMAND_NOTSUP          = 'COMMAND_NOTSUP'
## TestBox adds to the main log.
LOG_MAIN                = 'LOG_MAIN'
## TestBox uploads a file to the current test result.
UPLOAD                  = 'UPLOAD'
## TestBox reports completion of an EXEC command.
EXEC_COMPLETED          = 'EXEC_COMPLETED'
## Push more "XML" results to the server.
XML_RESULTS             = 'XML_RESULTS';
## @}


## @name Parameters for all actions.
# @{
ALL_PARAM_ACTION            = 'ACTION'
ALL_PARAM_TESTBOX_ID        = 'TESTBOX_ID'      ##< Not supplied by SIGNON.
ALL_PARAM_TESTBOX_UUID      = 'TESTBOX_UUID'
## @}

## @name SIGNON parameters.
# @{
SIGNON_PARAM_OS                 = 'OS';
SIGNON_PARAM_OS_VERSION         = 'OS_VERSION';
SIGNON_PARAM_CPU_VENDOR         = 'CPU_VENDOR';
SIGNON_PARAM_CPU_ARCH           = 'CPU_ARCH';
SIGNON_PARAM_CPU_NAME           = 'CPU_NAME';
SIGNON_PARAM_CPU_REVISION       = 'CPU_REVISION';
SIGNON_PARAM_CPU_COUNT          = 'CPU_COUNT';
SIGNON_PARAM_HAS_HW_VIRT        = 'HAS_HW_VIRT';
SIGNON_PARAM_HAS_NESTED_PAGING  = 'HAS_NESTED_PAGING';
SIGNON_PARAM_HAS_64_BIT_GUEST   = 'HAS_64_BIT_GUST';
SIGNON_PARAM_HAS_IOMMU          = 'HAS_IOMMU';
SIGNON_PARAM_WITH_RAW_MODE      = 'WITH_RAW_MODE';
SIGNON_PARAM_HAS_NATIVE_API     = 'HAS_NATIVE_API';
SIGNON_PARAM_MEM_SIZE           = 'MEM_SIZE';
SIGNON_PARAM_SCRATCH_SIZE       = 'SCRATCH_SIZE';
SIGNON_PARAM_REPORT             = 'REPORT';
SIGNON_PARAM_SCRIPT_REV         = 'SCRIPT_REV';
SIGNON_PARAM_PYTHON_VERSION     = 'PYTHON_VERSION';
## @}

## @name Parameters for actions reporting results.
# @{
RESULT_PARAM_TEST_SET_ID    = 'TEST_SET_ID'
## @}

## @name EXEC_COMPLETED parameters.
# @{
EXEC_COMPLETED_PARAM_RESULT = 'EXEC_RESULT'
## @}

## @name COMMAND_ACK, COMMAND_NACK and COMMAND_NOTSUP parameters.
# @{
## The name of the command that's being
COMMAND_ACK_PARAM_CMD_NAME = 'CMD_NAME'
## @}

## @name LOG_MAIN parameters.
## The log body.
LOG_PARAM_BODY             = 'LOG_BODY'
## @}

## @name UPLOAD_FILE parameters.
## The file name.
UPLOAD_PARAM_NAME          = 'UPLOAD_NAME';
## The MIME type of the file.
UPLOAD_PARAM_MIME          = 'UPLOAD_MIME';
## The kind of file.
UPLOAD_PARAM_KIND          = 'UPLOAD_KIND';
## The file description.
UPLOAD_PARAM_DESC          = 'UPLOAD_DESC';
## @}

## @name XML_RESULT parameters.
## The "XML" body.
XML_RESULT_PARAM_BODY      = 'XML_BODY'
## @}

