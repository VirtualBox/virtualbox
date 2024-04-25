# pylint: disable=invalid-name
# pylint: disable=consider-using-f-string
# pylint: disable=line-too-long
# pylint: disable=undefined-variable
import logging
from http import HTTPStatus
from flask import jsonify

from vbox_server.global_settings import *
from vbox_server.utils.vbox_utils import *
from vbox_server.utils.restapi_objects_functions import *

from vbox_server.models.error import Error  # noqa: E501
from vbox_server.models.medium_change_encryption_request_body import MediumChangeEncryptionRequestBody  # noqa: E501
from vbox_server.models.medium_clone_to_base_request_body import MediumCloneToBaseRequestBody  # noqa: E501
from vbox_server.models.medium_clone_to_request_body import MediumCloneToRequestBody  # noqa: E501
from vbox_server.models.medium_create_base_storage_request_body import MediumCreateBaseStorageRequestBody  # noqa: E501
from vbox_server.models.medium_create_diff_storage_request_body import MediumCreateDiffStorageRequestBody  # noqa: E501
from vbox_server.models.medium_getencryptionsettings_response import MediumGetencryptionsettingsResponse  # noqa: E501
from vbox_server.models.medium_getproperties_response import MediumGetpropertiesResponse  # noqa: E501
from vbox_server.models.medium_getproperty_response import MediumGetpropertyResponse  # noqa: E501
from vbox_server.models.medium_getsnapshotids_response import MediumGetsnapshotidsResponse  # noqa: E501
from vbox_server.models.medium_resize_and_clone_to_request_body import MediumResizeAndCloneToRequestBody  # noqa: E501
from vbox_server.models.medium_response import MediumResponse  # noqa: E501
from vbox_server.models.medium_set_ids_request_body import MediumSetIdsRequestBody  # noqa: E501
from vbox_server.models.medium_set_properties_request_body import MediumSetPropertiesRequestBody  # noqa: E501
from vbox_server.models.medium_set_property_request_body import MediumSetPropertyRequestBody  # noqa: E501
from vbox_server.models.medium_state_response import MediumStateResponse  # noqa: E501
from vbox_server.models.progress_response import ProgressResponse  # noqa: E501
from vbox_server.models.token_response import TokenResponse  # noqa: E501
from vbox_server.models.virtual_box_create_medium_request_body import VirtualBoxCreateMediumRequestBody  # noqa: E501
from vbox_server import util


def i_synthetic_getmedium(mediumid, select=None):  # noqa: E501
    """
    Call interface method ISynthetic::getMedium

    :param select: The object attributes separated by comma
    :type select: str

    :rtype: MediumResponse
    """

    oError = None
    httpCode = 200 #(OK)

    vbox_utils_commonChecks()

    errorMessage='Error trying to find and get the medium with UUID '

    try:
        oVBox = ctx['vb']
    except Exception as e:
        logging.info ('couldn\'t get the VirtualBox object')
        oError = Error(500, str(e))
        return jsonify(oError), 500

    fFound = False
    oMediumResponse = MediumResponse()
    try:
        olDisks = ctx['global'].getArray(oVBox,'hardDisks')
        for count, item in enumerate(olDisks):
            if item.id == mediumid:
                oMediumResponse.medium = i_fill_medium(item)
                fFound = True
                break
        if fFound == False:
            olDisks = ctx['global'].getArray(oVBox,'DVDImages')
            for count, item in enumerate(olDisks):
                if item.id == mediumid:
                    oMediumResponse.medium = i_fill_medium(item)
                    fFound = True
                    break
        if fFound == False:
            olDisks = ctx['global'].getArray(oVBox,'floppyImages')
            for count, item in enumerate(olDisks):
                if item.id == mediumid:
                    oMediumResponse.medium = i_fill_medium(item)
                    fFound = True
                    break

    except Exception as e:
        errorMessage = str(e) + 'Additional: ' + errorMessage + ' %s' % (mediumid,)
        logging.info(errorMessage)
        httpCode = 500
        oError = Error(httpCode, str(e))

    if fFound == False:
        httpCode = 404 
        oError = Error(404, str("The Medium with UUID %s wasn't found" % (mediumid,)))
        
    response = jsonify(oError if oError is not None else oMediumResponse)
    return response, httpCode


def i_medium_changeencryption(mediumid, oMediumChangeEncryptionRequestBody):  # noqa: E501
    """
    Call interface method IMedium::changeEncryption

    :param mediumid: The Id of medium
    :type mediumid: str
    :param oMediumChangeEncryptionRequestBody: 
    :type oMediumChangeEncryptionRequestBody: dict | bytes

    :rtype: ProgressResponse
    """
    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_medium_checkencryptionpassword(mediumid, password=None):  # noqa: E501
    """
    Call interface method IMedium::checkEncryptionPassword

    :param mediumid: The Id of medium
    :type mediumid: str
    :param password: 
    :type password: str

    :rtype: None
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_medium_cloneto(mediumid, oMediumCloneToRequestBody):  # noqa: E501
    """
    Call interface method IMedium::cloneTo

    :param mediumid: The Id of medium
    :type mediumid: str
    :param oMediumCloneToRequestBody: 
    :type oMediumCloneToRequestBody: dict | bytes

    :rtype: ProgressResponse
    """
    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_medium_clonetobase(mediumid, oMediumCloneToBaseRequestBody):  # noqa: E501
    """
    Call interface method IMedium::cloneToBase

    :param mediumid: The Id of medium
    :type mediumid: str
    :param oMediumCloneToBaseRequestBody: 
    :type oMediumCloneToBaseRequestBody: dict | bytes

    :rtype: ProgressResponse
    """
    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_medium_close(mediumid):  # noqa: E501
    """
    Call interface method IMedium::close

    :param mediumid: The Id of medium
    :type mediumid: str

    :rtype: None
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_medium_compact(mediumid):  # noqa: E501
    """
    Call interface method IMedium::compact

    :param mediumid: The Id of medium
    :type mediumid: str

    :rtype: ProgressResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_medium_createbasestorage(mediumid, oMediumCreateBaseStorageRequestBody):  # noqa: E501
    """
    Call interface method IMedium::createBaseStorage

    :param mediumid: The Id of medium
    :type mediumid: str
    :param oMediumCreateBaseStorageRequestBody: 
    :type oMediumCreateBaseStorageRequestBody: dict | bytes

    :rtype: ProgressResponse
    """
    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_medium_creatediffstorage(mediumid, oMediumCreateDiffStorageRequestBody):  # noqa: E501
    """
    Call interface method IMedium::createDiffStorage

    :param mediumid: The Id of medium
    :type mediumid: str
    :param oMediumCreateDiffStorageRequestBody: 
    :type oMediumCreateDiffStorageRequestBody: dict | bytes

    :rtype: ProgressResponse
    """
    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_medium_deletestorage(mediumid):  # noqa: E501
    """
    Call interface method IMedium::deleteStorage

    :param mediumid: The Id of medium
    :type mediumid: str

    :rtype: ProgressResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_medium_getencryptionsettings(mediumid):  # noqa: E501
    """
    Call interface method IMedium::getEncryptionSettings

    :param mediumid: The Id of medium
    :type mediumid: str

    :rtype: MediumGetencryptionsettingsResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_medium_getproperties(mediumid, names=None):  # noqa: E501
    """
    Call interface method IMedium::getProperties

    :param mediumid: The Id of medium
    :type mediumid: str
    :param names: 
    :type names: str

    :rtype: MediumGetpropertiesResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_medium_getproperty(mediumid, name=None):  # noqa: E501
    """
    Call interface method IMedium::getProperty

    :param mediumid: The Id of medium
    :type mediumid: str
    :param name: 
    :type name: str

    :rtype: MediumGetpropertyResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_medium_getsnapshotids(mediumid, machineId=None):  # noqa: E501
    """
    Call interface method IMedium::getSnapshotIds

    :param mediumid: The Id of medium
    :type mediumid: str
    :param machineId: 
    :type machineId: str

    :rtype: MediumGetsnapshotidsResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_medium_lockread(mediumid):  # noqa: E501
    """
    Call interface method IMedium::lockRead

    :param mediumid: The Id of medium
    :type mediumid: str

    :rtype: TokenResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_medium_lockwrite(mediumid):  # noqa: E501
    """
    Call interface method IMedium::lockWrite

    :param mediumid: The Id of medium
    :type mediumid: str

    :rtype: TokenResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_medium_mergeto(mediumid, target=None):  # noqa: E501
    """
    Call interface method IMedium::mergeTo

    :param mediumid: The Id of medium
    :type mediumid: str
    :param target: Put here an ID of requested IMedium VirtualBox object
    :type target: str

    :rtype: ProgressResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_medium_moveto(mediumid, location=None):  # noqa: E501
    """
    Call interface method IMedium::moveTo

    :param mediumid: The Id of medium
    :type mediumid: str
    :param location: 
    :type location: str

    :rtype: ProgressResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_medium_refreshstate(mediumid):  # noqa: E501
    """
    Call interface method IMedium::refreshState

    :param mediumid: The Id of medium
    :type mediumid: str

    :rtype: MediumStateResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_medium_reset(mediumid):  # noqa: E501
    """
    Call interface method IMedium::reset

    :param mediumid: The Id of medium
    :type mediumid: str

    :rtype: ProgressResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_medium_resize(mediumid, logicalSize=None):  # noqa: E501
    """
    Call interface method IMedium::resize

    :param mediumid: The Id of medium
    :type mediumid: str
    :param logicalSize: 
    :type logicalSize: int

    :rtype: ProgressResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED

def medium_resizeandcloneto(mediumid, oMediumResizeAndCloneToRequestBody):  # noqa: E501
    """
    Call interface method IMedium::resizeAndCloneTo

    :param mediumid: The Id of medium
    :type mediumid: str
    :param oMediumResizeAndCloneToRequestBody: 
    :type oMediumResizeAndCloneToRequestBody: dict | bytes

    :rtype: ProgressResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED

def i_medium_setids(mediumid, oMediumSetIdsRequestBody):  # noqa: E501
    """
    Call interface method IMedium::setIds

    :param mediumid: The Id of medium
    :type mediumid: str
    :param oMediumSetIdsRequestBody: 
    :type oMediumSetIdsRequestBody: dict | bytes

    :rtype: None
    """
    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_medium_setproperties(mediumid, oMediumSetPropertiesRequestBody):  # noqa: E501
    """
    Call interface method IMedium::setProperties

    :param mediumid: The Id of medium
    :type mediumid: str
    :param oMediumSetPropertiesRequestBody: 
    :type oMediumSetPropertiesRequestBody: dict | bytes

    :rtype: None
    """
    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_medium_setproperty(mediumid, oMediumSetPropertyRequestBody):  # noqa: E501
    """
    Call interface method IMedium::setProperty

    :param mediumid: The Id of medium
    :type mediumid: str
    :param oMediumSetPropertyRequestBody: 
    :type oMediumSetPropertyRequestBody: dict | bytes

    :rtype: None
    """
    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_virtualbox_createmedium(oVirtualBoxCreateMediumRequestBody):  # noqa: E501
    """
    Call interface method IVirtualBox::createMedium

    :param oVirtualBoxCreateMediumRequestBody: 
    :type oVirtualBoxCreateMediumRequestBody: dict | bytes

    :rtype: MediumResponse
    """
    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED