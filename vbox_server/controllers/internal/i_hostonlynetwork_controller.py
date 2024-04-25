# pylint: disable=invalid-name
# pylint: disable=consider-using-f-string
# pylint: disable=line-too-long
# pylint: disable=undefined-variable
import logging
from http import HTTPStatus
from flask import jsonify

from vbox_server.models.error import Error  # noqa: E501
from vbox_server.models.host_only_network_response import HostOnlyNetworkResponse  # noqa: E501
from vbox_server import util


def i_virtualbox_createhostonlynetwork(networkName=None):  # noqa: E501
    """
    Call interface method IVirtualBox::createHostOnlyNetwork

    :param networkName: 
    :type networkName: str

    :rtype: HostOnlyNetworkResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_virtualbox_findhostonlynetworkbyid(select=None, id=None):  # noqa: E501
    """
    Call interface method IVirtualBox::findHostOnlyNetworkById

    :param select: The object attributes separated by comma
    :type select: str
    :param id: 
    :type id: str

    :rtype: HostOnlyNetworkResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_virtualbox_removehostonlynetwork(network=None):  # noqa: E501
    """
    Call interface method IVirtualBox::removeHostOnlyNetwork

    :param network: Put here an ID of requested IHostOnlyNetwork VirtualBox object
    :type network: str

    :rtype: None
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED