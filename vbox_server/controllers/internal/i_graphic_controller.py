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
import connexion
import six

from vbox_server.models.graphics_adapter_set_feature_request_body import GraphicsAdapterSetFeatureRequestBody  # noqa: E501
from vbox_server.models.graphics_adapter_is_feature_enabled_response import GraphicsAdapterIsFeatureEnabledResponse  # noqa: E501


# def i_graphicsadapter_isfeatureenabled(feature, feature2=None):  # noqa: E501
#     """
#     Call interface method IGraphicsAdapter::isFeatureEnabled

#     :param feature: The Id of featu
#     :type feature: str
#     :param feature2: For the possible values of enumeration look into #/definitions/GraphicsFeature
#     :type feature2: str

#     :rtype: GraphicsadapterIsfeatureenabledResponse
#     """
#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_graphicsadapter_setfeature(feature, oGraphicsAdapterSetFeatureRequestBody):  # noqa: E501
    """
    Call interface method IGraphicsAdapter::setFeature

    :param feature: The Id of featu
    :type feature: str
    :param oGraphicsAdapterSetFeatureRequestBody: 
    :type oGraphicsAdapterSetFeatureRequestBody: dict | bytes

    :rtype: None
    """
    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED
