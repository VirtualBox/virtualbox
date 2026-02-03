"""VBox REST API

Copyright (c) 2025 Oracle and/or its affiliates.
Licensed under the Universal Permissive License v 1.0 as shown at https://oss.oracle.com/licenses/upl

SPDX-License-Identifier: UPL-1.0
"""

# pylint: disable=invalid-name
# pylint: disable=consider-using-f-string
# pylint: disable=line-too-long
# pylint: disable=undefined-variable
import logging
from http import HTTPStatus
from flask import jsonify

from vbox_server.utils.decorators import *
from vbox_server.models.error import Error  # noqa: E501
from vbox_server.models.virtual_box_create_shared_folder_request_body import VirtualBoxCreateSharedFolderRequestBody  # noqa: E501
from vbox_server import util


# @virtualboxDecorator
# def i_virtualbox_createsharedfolder(oVBoxObj, oVirtualBoxCreateSharedFolderRequestBody):  # noqa: E501
#     """
#     Call interface method IVirtualBox::createSharedFolder

#     :param oVirtualBoxCreateSharedFolderRequestBody: 
#     :type oVirtualBoxCreateSharedFolderRequestBody: dict | bytes

#     :rtype: None
#     """

#     vbox_utils_commonChecks()
#     httpCode = HTTPStatus.OK
#     oError = None

#     oCurrVirtualBox = oVBoxObj
    
#     o = oVirtualBoxCreateSharedFolderRequestBody
#     name = o.name
#     hostPath = o.hostPath
#     fWritable = o.writable
#     fAutomount = o.automount
#     autoMountPoint = o.autoMountPoint

#     logging.info(f'Try to create the shared folder {name}')

#     oError = None
#     httpCode = HTTPStatus.OK

#     if oError is None:
#         try:
#             # No return result check
#             oCurrVirtualBox.createSharedFolder(name, hostPath, fWritable, fAutomount, autoMountPoint)
#             logging.info(f'Created the shared folder {name}')

#         except Exception as e:
#             logging.info(f'Exception during creation the shared folder {name}')
#             httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#             oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else f'Successfully created the shared folder {name}')
#     return response, httpCode


# def i_virtualbox_removesharedfolder(name=None):  # noqa: E501
#     """
#     Call interface method IVirtualBox::removeSharedFolder

#     :param name: 
#     :type name: str

#     :rtype: None
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED