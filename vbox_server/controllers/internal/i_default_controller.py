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

from vbox_server.models.error import Error  # noqa: E501
from vbox_server import util
# from vbox_server.models.platform_properties_response import PlatformPropertiesResponse  # noqa: E501


# def i_virtualbox_getplatformproperties(select=None, architecture=None):  # noqa: E501
#     """
#     Call interface method IVirtualBox::getPlatformProperties

#     :param select: The object attributes separated by comma
#     :type select: str
#     :param architecture: For the possible values of enumeration look into #/definitions/PlatformArchitecture
#     :type architecture: str

#     :rtype: PlatformPropertiesResponse
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED