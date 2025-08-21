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

from vbox_server.models.cloud_network_obj_wrapper_response import CloudNetworkObjWrapperResponse  # noqa: E501
from vbox_server.models.error import Error  # noqa: E501
from vbox_server import util


# def i_virtualbox_createcloudnetwork(networkName=None):  # noqa: E501
#     """
#     Call interface method IVirtualBox::createCloudNetwork

#     :param networkName: 
#     :type networkName: str

#     :rtype: CloudNetworkResponse
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_virtualbox_findcloudnetworkbyname(select=None, networkName=None):  # noqa: E501
#     """
#     Call interface method IVirtualBox::findCloudNetworkByName

#     :param select: The object attributes separated by comma
#     :type select: str
#     :param networkName: 
#     :type networkName: str

#     :rtype: CloudNetworkResponse
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_virtualbox_removecloudnetwork(network=None):  # noqa: E501
#     """
#     Call interface method IVirtualBox::removeCloudNetwork

#     :param network: Put here an ID of requested ICloudNetwork VirtualBox object
#     :type network: str

#     :rtype: None
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED
