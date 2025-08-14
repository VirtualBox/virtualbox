"""VBox REST API

Copyright (c) 2024-2025 Oracle and/or its affiliates.
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

from vbox_server.models.error import Error  # noqa: E501
from vbox_server.models.virtual_box_create_shared_folder_request_body import VirtualBoxCreateSharedFolderRequestBody  # noqa: E501
from vbox_server import util


def i_virtualbox_createsharedfolder(oVirtualBoxCreateSharedFolderRequestBody):  # noqa: E501
    """
    Call interface method IVirtualBox::createSharedFolder

    :param oVirtualBoxCreateSharedFolderRequestBody: 
    :type oVirtualBoxCreateSharedFolderRequestBody: dict | bytes

    :rtype: None
    """
    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_virtualbox_removesharedfolder(name=None):  # noqa: E501
#     """
#     Call interface method IVirtualBox::removeSharedFolder

#     :param name: 
#     :type name: str

#     :rtype: None
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED