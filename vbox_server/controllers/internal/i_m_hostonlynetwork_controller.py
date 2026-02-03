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

from vbox_server.models.host_only_network_obj_wrapper_response import HostOnlyNetworkObjWrapperResponse  # noqa: E501


# def i_virtualbox_createhostonlynetwork(networkName=None):  # noqa: E501
#     """
#     Call interface method IVirtualBox::createHostOnlyNetwork

#     :param networkName: 
#     :type networkName: str

#     :rtype: HostOnlyNetworkObjWrapperResponse
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_virtualbox_findhostonlynetworkbyid(select=None, id=None):  # noqa: E501
#     """
#     Call interface method IVirtualBox::findHostOnlyNetworkById

#     :param select: The object attributes separated by comma
#     :type select: str
#     :param id: 
#     :type id: str

#     :rtype: HostOnlyNetworkObjWrapperResponse
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_virtualbox_removehostonlynetwork(network=None):  # noqa: E501
#     """
#     Call interface method IVirtualBox::removeHostOnlyNetwork

#     :param network: Put here an ID of requested IHostOnlyNetwork VirtualBox object
#     :type network: str

#     :rtype: None
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED