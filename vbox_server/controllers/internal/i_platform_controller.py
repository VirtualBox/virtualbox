# pylint: disable=invalid-name
# pylint: disable=consider-using-f-string
# pylint: disable=line-too-long
# pylint: disable=undefined-variable

from http import HTTPStatus

from vbox_server.global_settings import *
from vbox_server.utils.vbox_utils import *
from vbox_server.utils.restapi_objects_functions import *
from vbox_server.utils.enum_conversion import *

from vbox_server.models.error import Error  # noqa: E501
from vbox_server.models.platform_properties_response import PlatformPropertiesResponse  # noqa: E501
from vbox_server.controllers.internal.i_platform_controller import *


def i_virtualbox_getplatformproperties(select=None, architecture=None):  # noqa: E501
    """
    Call interface method IVirtualBox::getPlatformProperties

    :param select: The object attributes separated by comma
    :type select: str
    :param architecture: For the possible values of enumeration look into #/definitions/PlatformArchitecture
    :type architecture: str

    :rtype: PlatformPropertiesResponse
    """

    oError = None
    httpCode = HTTPStatus.OK

    vbox_utils_commonChecks()

    oPlatformPropertiesResponse = PlatformPropertiesResponse()
    try:
        oVBox = ctx['vb']
        vBoxPlatformArchitecture = swagger_to_vbox_platform_architecture(architecture)
        oPlatformProperties = oVBox.getPlatformProperties(vBoxPlatformArchitecture)
        oPlatformPropertiesResponse.properties = i_fill_platform_properties(oPlatformProperties, select)
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oPlatformPropertiesResponse)
    return response, httpCode
