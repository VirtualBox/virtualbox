# pylint: disable=invalid-name
# pylint: disable=consider-using-f-string
# pylint: disable=line-too-long
# pylint: disable=undefined-variable
import logging
from http import HTTPStatus
from flask import jsonify

from vbox_server.models.cloud_network_response import CloudNetworkResponse  # noqa: E501
from vbox_server.models.error import Error  # noqa: E501
from vbox_server import util


def i_virtualbox_createcloudnetwork(networkName=None):  # noqa: E501
    """
    Call interface method IVirtualBox::createCloudNetwork

    :param networkName: 
    :type networkName: str

    :rtype: CloudNetworkResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_virtualbox_findcloudnetworkbyname(select=None, networkName=None):  # noqa: E501
    """
    Call interface method IVirtualBox::findCloudNetworkByName

    :param select: The object attributes separated by comma
    :type select: str
    :param networkName: 
    :type networkName: str

    :rtype: CloudNetworkResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_virtualbox_removecloudnetwork(network=None):  # noqa: E501
    """
    Call interface method IVirtualBox::removeCloudNetwork

    :param network: Put here an ID of requested ICloudNetwork VirtualBox object
    :type network: str

    :rtype: None
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED
