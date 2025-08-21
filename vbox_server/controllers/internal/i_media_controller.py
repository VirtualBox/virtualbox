"""VBox REST API

Copyright (c) 2025 Oracle and/or its affiliates.
Licensed under the Universal Permissive License v 1.0 as shown at https://oss.oracle.com/licenses/upl

SPDX-License-Identifier: UPL-1.0
"""

# pylint: disable=invalid-name
# pylint: disable=consider-using-f-string
# pylint: disable=line-too-long
# pylint: disable=undefined-variable
import functools
import uuid
import logging
from http import HTTPStatus
from flask import jsonify

from typing import List
from ctypes import c_uint32

from vbox_server.global_settings import *
from vbox_server.utils.vbox_utils import *
from vbox_server.utils.object_conversion import *
from vbox_server.utils.enum_conversion import *
from vbox_server.utils.decorators import *

from vbox_server.models.error import Error  # noqa: E501
from vbox_server.models.medium_clone_to_base_request_body import MediumCloneToBaseRequestBody  # noqa: E501
from vbox_server.models.medium_clone_to_request_body import MediumCloneToRequestBody  # noqa: E501
from vbox_server.models.medium_create_base_storage_request_body import MediumCreateBaseStorageRequestBody  # noqa: E501
from vbox_server.models.medium_create_diff_storage_request_body import MediumCreateDiffStorageRequestBody  # noqa: E501
from vbox_server.models.medium_get_encryption_settings_response import MediumGetEncryptionSettingsResponse  # noqa: E501
from vbox_server.models.medium_get_properties_response import MediumGetPropertiesResponse  # noqa: E501
from vbox_server.models.medium_get_property_response import MediumGetPropertyResponse  # noqa: E501
from vbox_server.models.medium_get_snapshot_ids_response import MediumGetSnapshotIdsResponse  # noqa: E501
from vbox_server.models.medium_obj_wrapper_response import MediumObjWrapperResponse  # noqa: E501
from vbox_server.models.medium_resize_and_clone_to_request_body import MediumResizeAndCloneToRequestBody  # noqa: E501
from vbox_server.models.medium_set_ids_request_body import MediumSetIdsRequestBody  # noqa: E501
from vbox_server.models.medium_set_properties_request_body import MediumSetPropertiesRequestBody  # noqa: E501
from vbox_server.models.medium_set_property_request_body import MediumSetPropertyRequestBody  # noqa: E501
from vbox_server.models.progress_obj_wrapper_response import ProgressObjWrapperResponse  # noqa: E501
from vbox_server.models.virtual_box_create_medium_request_body import VirtualBoxCreateMediumRequestBody  # noqa: E501


############################ Helpers ############################
# local list to keep a newly created mediums that wait to be registered in VirtualBox
lNewAndNotRegisteredStorage = dict()

def __find_medium_by_id(id: str):
    lDiskType = ['hardDisks', 'DVDImages', 'floppyImages']
    oVBox = ctx['vb']
    fFound = False
    oFoundMedium = None
    oError = None

    for diskType in lDiskType:
        try:
            olDisks = ctx['global'].getArray(oVBox, diskType)
            for item in olDisks:
                if str(item.id) == id:
                    oFoundMedium = item
                    fFound = True
                    break
        except Exception as e:
            logging.info('Error walking through the array of ' + diskType)
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))
            oFoundMedium = None

        if fFound is True:
            break

    return oFoundMedium, oError


def findMedium_decorator(func):
    """
    Find the medium object using the passed ID
    The first parameter must be medium uuid always.
    Appends the arguments list by the flag which indicates whether the medium was found or not
    """
    @functools.wraps(func)
    def wrapper_decorator(*args, **kwargs):
        args_repr = [a for a in args]
        mediumid = args_repr[0]
        oVBoxMedium = None

        oVBoxMedium, oError = __find_medium_by_id(mediumid)
        if oVBoxMedium is not None:
            args_repr[0] = oVBoxMedium #replace the first argument "mediumid" by oVBoxMedium
        else:
            if oError:
                return jsonify('The medium with UUID ' + mediumid + ' wasn\'t found. Internal error is ' + '"' + oError.message + '"'), oError.code
            else:
                return jsonify("The medium with UUID " + mediumid + " wasn't found"), HTTPStatus.NOT_FOUND

        new_args_repr=args_repr

        #Call the general function with the updated arguments list
        value = func(*new_args_repr, **kwargs)

        return value

    return wrapper_decorator


def __testLocation(sLocation: str):
    fRes = True
    if not os.path.exists(sLocation):
        fRes = False
    return fRes


############################ Implemented or used ############################
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
    oMediumResponse = MediumObjWrapperResponse()
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


# Problem! IVirtualBox::createMedium must be called together with IMedium::createBaseStorage inside one action
# Because medium registration is done only inside IMedium::createBaseStorage.
# User can't find a new medium after returning from IVirtualBox::createMedium.
# Workaround is the using the dictionary lNewAndNotRegisteredStorage as the temporary storage for a new VirtualBox Medium object
def i_virtualbox_createmedium(oVirtualBoxCreateMediumRequestBody: VirtualBoxCreateMediumRequestBody):  # noqa: E501
    """
    Call interface method IVirtualBox::createMedium

    :param oVirtualBoxCreateMediumRequestBody: 
    :type oVirtualBoxCreateMediumRequestBody: dict | bytes

    :rtype: MediumResponse
    """

    vbox_utils_commonChecks()

    format = oVirtualBoxCreateMediumRequestBody.format
    location = oVirtualBoxCreateMediumRequestBody.location
    accessMode = swagger_to_vbox_accessmode(oVirtualBoxCreateMediumRequestBody.accessMode)
    deviceType = swagger_to_vbox_devicetype(oVirtualBoxCreateMediumRequestBody.aDeviceTypeType)

    logging.info(f"Creating medium in location {location}")

    oVBox = ctx['vb']
    oError = None
    httpCode = HTTPStatus.OK
    oMediumResponse = MediumObjWrapperResponse()

    try:
        oHdd = oVBox.createMedium(format, location, accessMode, deviceType)
        if oHdd is not None:
            tempUuid = uuid.uuid4()
            strUuid = str(tempUuid)
            lNewAndNotRegisteredStorage[strUuid] = oHdd
            oMediumResponse.medium = i_fill_medium(oHdd)
            oMediumResponse.medium.id = strUuid
            logging.info('The medium creation has been done successfully')
        else:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, f"Something wrong with medium creation in location {location}")

    except Exception as e:
        logging.info(f"Exception during medium creation in location {location}")
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oMediumResponse)
    return response, httpCode


@findMedium_decorator
def i_medium_compact(oVBoxMedium):  # noqa: E501
    """
    Call interface method IMedium::compact

    :param mediumid: The Id of medium
    :type mediumid: str

    :rtype: ProgressResponse
    """

    vbox_utils_commonChecks()

    logging.info(f"Compacting the medium {oVBoxMedium.id}")

    oError = None
    httpCode = HTTPStatus.OK
    oProgressResponse = ProgressObjWrapperResponse()

    try:
        oVBoxProgress = oVBoxMedium.compact()
        if oVBoxProgress is not None:
            oProgressResponse.progress = i_fill_progress(oVBoxProgress)
            logging.info('The compacting medium has been successfully started')

            # Add Progress Id object into the tracking lists
            ctx['tracker'][oProgressResponse.progress.id] = None
        else:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, "Something wrong with the Progress object")

    except Exception as e:
        logging.info(f"Exception during compacting the medium {oVBoxMedium.id}")
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oProgressResponse)
    return response, httpCode


@findMedium_decorator
def i_medium_resize(oVBoxMedium, logicalSize=None):  # noqa: E501
    """
    Call interface method IMedium::resize

    :param mediumid: The Id of medium
    :type mediumid: str
    :param logicalSize: 
    :type logicalSize: int

    :rtype: ProgressResponse
    """

    vbox_utils_commonChecks()

    logging.info(f"Resizing the medium {oVBoxMedium.id}")

    oError = None
    httpCode = HTTPStatus.OK
    oProgressResponse = ProgressObjWrapperResponse()

    if logicalSize is None or logicalSize == 0:
        httpCode = HTTPStatus.PRECONDITION_FAILED
        oError = Error(httpCode, "The passed logical size is zero")
        return jsonify(oError), httpCode

    try:
        oVBoxProgress = oVBoxMedium.resize(logicalSize)
        if oVBoxProgress is not None:
            oProgressResponse.progress = i_fill_progress(oVBoxProgress)
            logging.info('The compacting medium has been successfully started')

            # Add Progress Id object into the tracking lists
            ctx['tracker'][oProgressResponse.progress.id] = None
        else:
            httpCode = HTTPStatus.OK
            oError = Error(httpCode, f"The medium {id} has been successfully resized without using Progress object")

    except Exception as e:
        logging.info(f"Exception during compacting the medium {oVBoxMedium.id}")
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oProgressResponse)
    return response, httpCode


@findMedium_decorator
def i_medium_deletestorage(oVBoxMedium):  # noqa: E501
    """
    Call interface method IMedium::deleteStorage

    :param mediumid: The Id of medium
    :type mediumid: str

    :rtype: ProgressResponse
    """

    vbox_utils_commonChecks()

    logging.info(f"Deletion the medium {oVBoxMedium.id}")

    oError = None
    httpCode = HTTPStatus.OK
    oProgressResponse = ProgressObjWrapperResponse()

    try:
        oVBoxProgress = oVBoxMedium.deleteStorage()
        if oVBoxProgress is not None:
            oProgressResponse.progress = i_fill_progress(oVBoxProgress)
            logging.info('The deletion of medium has been successfully started')

            # Add Progress Id object into the tracking lists
            ctx['tracker'][oProgressResponse.progress.id] = None
        else:
            httpCode = HTTPStatus.OK
            oError = Error(httpCode, f"The medium {id} has been successfully deleted without using Progress object")

    except Exception as e:
        logging.info(f"Exception during deletion the medium {oVBoxMedium.id}")
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oProgressResponse)
    return response, httpCode


@findMedium_decorator
def i_medium_close(oVBoxMedium):  # noqa: E501
    """
    Call interface method IMedium::close

    :param mediumid: The Id of medium
    :type mediumid: str

    :rtype: None
    """

    vbox_utils_commonChecks()

    logging.info(f"Closing the medium {oVBoxMedium.id}")

    oError = None
    httpCode = HTTPStatus.OK
    oProgressResponse = ProgressObjWrapperResponse()

    try:
        # Note that after this method successfully returns, the given medium object becomes uninitialized.
        # This means that any attempt to call any of its methods or attributes will fail with the "Object not ready" (E_ACCESSDENIED) error.
        # save oVBoxMedium.id in the temporary variable
        id = oVBoxMedium.id
        oVBoxProgress = oVBoxMedium.close()
        if oVBoxProgress is not None:
            oProgressResponse.progress = i_fill_progress(oVBoxProgress)
            logging.info('The closing of medium has been successfully started')

            # Add Progress Id object into the tracking lists
            ctx['tracker'][oProgressResponse.progress.id] = None
        else:
            httpCode = HTTPStatus.OK
            oError = Error(httpCode, f"The medium {id} has been successfully closed without using Progress object")

    except Exception as e:
        logging.info(f"Exception during closing the medium {id}")
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oProgressResponse)
    return response, httpCode


@findMedium_decorator
def i_medium_reset(oVBoxMedium):  # noqa: E501
    """
    Call interface method IMedium::reset

    :param mediumid: The Id of medium
    :type mediumid: str

    :rtype: ProgressResponse
    """

    vbox_utils_commonChecks()

    logging.info(f"Closing the medium {oVBoxMedium.id}")

    oError = None
    httpCode = HTTPStatus.OK
    oProgressResponse = ProgressObjWrapperResponse()
    # oProgressResponse = ProgressResponse()

    try:
        oVBoxProgress = oVBoxMedium.reset()
        if oVBoxProgress is not None:
            oProgressResponse.progress = i_fill_progress(oVBoxProgress)
            logging.info('The closing of medium has been successfully started')

            # Add Progress Id object into the tracking lists
            ctx['tracker'][oProgressResponse.progress.id] = None
        else:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, "Something wrong with the Progress object")

    except Exception as e:
        logging.info(f"Exception during closing the medium {oVBoxMedium.id}")
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oProgressResponse)
    return response, httpCode


@findMedium_decorator
def i_medium_cloneto(oVBoxMedium, oMediumCloneToRequestBody: MediumCloneToRequestBody):  # noqa: E501
    """
    Call interface method IMedium::cloneTo

    :param mediumid: The Id of medium
    :type mediumid: str
    :param oMediumCloneToRequestBody:
    :type oMediumCloneToRequestBody: dict | bytes

    :rtype: ProgressResponse
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK
    oProgressResponse = ProgressObjWrapperResponse()

    strParentId = oMediumCloneToRequestBody.parent
    strTargetId = oMediumCloneToRequestBody.target
    lMediumVariant = list()
    for item in oMediumCloneToRequestBody.variant:
        variant = swagger_to_vbox_mediumvariant(item)
        lMediumVariant.append(variant)

    if len(lMediumVariant) == 0:#try to use kind of standard medium variant
        lMediumVariant.append(ctx['const'].MediumVariant_Standard)

    logging.info(f"Cloning the medium {oVBoxMedium.id} to the medium {strTargetId}")

    oTargetMedium, oError = __find_medium_by_id(strTargetId)
    if oError or oTargetMedium is None:
        logging.info(f"The medium passed as 'target' with Id {strTargetId} wasn't found")
        oError = Error(HTTPStatus.NOT_FOUND, f"The medium passed as 'target' with Id {strTargetId} wasn't found")
        return jsonify(oError), HTTPStatus.NOT_FOUND

    oParentMedium = None
    if strParentId is not None and strParentId != '':
        oParentMedium, oError = __find_medium_by_id(strParentId)
        if oError or oParentMedium is None:
            logging.info(f"The medium passed as 'parent' with Id {strParentId} wasn't found")
            oError = Error(HTTPStatus.NOT_FOUND, f"The medium passed as 'parent' with Id {strParentId} wasn't found")
            return jsonify(oError), HTTPStatus.NOT_FOUND

    try:
        oVBoxProgress = oVBoxMedium.cloneTo(oTargetMedium, lMediumVariant, oParentMedium)
        if oVBoxProgress is not None:
            oProgressResponse.progress = i_fill_progress(oVBoxProgress)
            logging.info('The cloning medium has been successfully started')

            # Add Progress Id object into the tracking lists
            ctx['tracker'][oProgressResponse.progress.id] = None
        else:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, "Something wrong with the Progress object")

    except Exception as e:
        logging.info(f"Exception during cloning the medium {oVBoxMedium.id}")
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oProgressResponse)
    return response, httpCode


def i_medium_clonetobase(mediumid, oMediumCloneToBaseRequestBody: MediumCloneToBaseRequestBody):  # noqa: E501
    """
    Call interface method IMedium::cloneToBase

    :param mediumid: The Id of medium
    :type mediumid: str
    :param oMediumCloneToBaseRequestBody: 
    :type oMediumCloneToBaseRequestBody: dict | bytes

    :rtype: ProgressResponse
    """

    oMediumCloneToRequestBody = MediumCloneToRequestBody()
    oMediumCloneToRequestBody.parent = None
    oMediumCloneToRequestBody.target = oMediumCloneToBaseRequestBody.target
    oMediumCloneToRequestBody.variant = oMediumCloneToBaseRequestBody.variant

    return i_medium_cloneto(mediumid, oMediumCloneToRequestBody)


@findMedium_decorator
def i_medium_mergeto(oVBoxMedium, target=None):  # noqa: E501
    """
    Call interface method IMedium::mergeTo

    :param mediumid: The Id of medium
    :type mediumid: str
    :param target: Put here an ID of requested IMedium VirtualBox object
    :type target: str

    :rtype: ProgressResponse
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK
    oProgressResponse = ProgressObjWrapperResponse()
    
    strTargetId = target
    oTargetMedium, oError = __find_medium_by_id(strTargetId)
    if oError or oTargetMedium is None:
        logging.info(f"The medium passed as 'target' with Id {strTargetId} wasn't found")
        oError = Error(HTTPStatus.NOT_FOUND, f"The medium passed as 'target' with Id {strTargetId} wasn't found")
        return jsonify(oError), HTTPStatus.NOT_FOUND

    try:
        oVBoxProgress = oVBoxMedium.mergeTo(oTargetMedium)

        if oVBoxProgress is not None:
            oProgressResponse.progress = i_fill_progress(oVBoxProgress)
            logging.info('The medium merging has been successfully started')

            # Add Progress Id object into the tracking lists
            ctx['tracker'][oProgressResponse.progress.id] = None
        else:
            httpCode = HTTPStatus.OK
            oError = Error(httpCode, f"The medium merging has been done without using Progress object")

    except Exception as e:
        logging.info(f"Exception during merging the source medium {oVBoxMedium.id} with the target {strTargetId}")
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oProgressResponse)
    return response, httpCode


@findMedium_decorator
def i_medium_moveto(oVBoxMedium, location=None):
    """
    Call interface method IMedium::moveTo

    :param mediumid: The Id of medium
    :type mediumid: str
    :param location: 
    :type location: str

    :rtype: ProgressResponse
    """

    vbox_utils_commonChecks()

    logging.info(f"Moving the medium {oVBoxMedium.id} to the new location {location}")

    oError = None
    httpCode = HTTPStatus.OK
    oProgressResponse = ProgressObjWrapperResponse()

    if __testLocation(location) is False:
        logging.info("The passed new location %s isn't a fully qualified path or hasn't existed" % (location))
    try:
        oVBoxProgress = oVBoxMedium.moveTo(location)
        if oVBoxProgress is not None:
            oProgressResponse.progress = i_fill_progress(oVBoxProgress)
            logging.info('The moving medium has been successfully started')

            # Add Progress Id object into the tracking lists
            ctx['tracker'][oProgressResponse.progress.id] = None
        else:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, "Something wrong with the Progress object")

    except Exception as e:
        logging.info("Exception during moving the medium %s to the new location %s" % (oVBoxMedium.id, location))
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oProgressResponse)
    return response, httpCode


@findMedium_decorator
def i_medium_resizeandcloneto(oVBoxMedium, oMediumResizeAndCloneToRequestBody: MediumResizeAndCloneToRequestBody):  # noqa: E501
    """
    Call interface method IMedium::resizeAndCloneTo

    :param mediumid: The Id of medium
    :type mediumid: str
    :param oMediumResizeAndCloneToRequestBody:
    :type oMediumResizeAndCloneToRequestBody: dict | bytes

    :rtype: ProgressResponse
    """

    oError = None
    httpCode = HTTPStatus.OK
    oProgressResponse = ProgressObjWrapperResponse()

    strParentId = oMediumResizeAndCloneToRequestBody.parent
    strTargetId = oMediumResizeAndCloneToRequestBody.target
    nLogicalSize = oMediumResizeAndCloneToRequestBody.logicalSize
    lMediumVariant = list()
    for item in oMediumResizeAndCloneToRequestBody.variant:
        variant = swagger_to_vbox_mediumvariant(item)
        lMediumVariant.append(variant)

    if len(lMediumVariant) == 0:#try to use kind of standard medium variant
        lMediumVariant.append(ctx['const'].MediumVariant_Standard)

    logging.info(f"Resizing and cloning the medium {oVBoxMedium.id} to the medium {strTargetId} with the size {nLogicalSize}")

    oTargetMedium, oError = __find_medium_by_id(strTargetId)
    if oError or oTargetMedium is None:
        logging.info(f"The medium passed as 'target' with Id {strTargetId} wasn't found")
        oError = Error(HTTPStatus.NOT_FOUND, f"The medium passed as 'target' with Id {strTargetId} wasn't found")
        return jsonify(oError), HTTPStatus.NOT_FOUND

    oParentMedium = None
    if strParentId is not None and strParentId != '':
        oParentMedium, oError = __find_medium_by_id(strParentId)
        if oError or oParentMedium is None:
            logging.info(f"The medium passed as 'parent' with Id {strParentId} wasn't found")
            oError = Error(HTTPStatus.NOT_FOUND, f"The medium passed as 'parent' with Id {strParentId} wasn't found")
            return jsonify(oError), HTTPStatus.NOT_FOUND

    try:
        oVBoxProgress = oVBoxMedium.resizeAndCloneTo(oTargetMedium, nLogicalSize, lMediumVariant, oParentMedium)
        if oVBoxProgress is not None:
            oProgressResponse.progress = i_fill_progress(oVBoxProgress)
            logging.info('The cloning medium has been successfully started')

            # Add Progress Id object into the tracking lists
            ctx['tracker'][oProgressResponse.progress.id] = None
        else:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, "Something wrong with the Progress object")

    except Exception as e:
        logging.info(f"Exception during resizing and cloning the medium {oVBoxMedium.id}")
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oProgressResponse)
    return response, httpCode


def i_medium_createbasestorage(mediumid, oMediumCreateBaseStorageRequestBody: MediumCreateBaseStorageRequestBody):  # noqa: E501
    """
    Call interface method IMedium::createBaseStorage

    :param mediumid: The Id of medium
    :type mediumid: str
    :param oMediumCreateBaseStorageRequestBody:
    :type oMediumCreateBaseStorageRequestBody: dict | bytes

    :rtype: ProgressResponse
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK
    oProgressResponse = ProgressObjWrapperResponse()

    # Trying to get VirtualBox Medium object that had been created earlier
    # mediumid, in this case, is a value returned in oMediumResponse.medium.id (see the function i_virtualbox_createmedium())
    try:
        oVBoxMedium = lNewAndNotRegisteredStorage[mediumid]
    except KeyError as e:
        logging.info(f"The medium Id {mediumid} wasn't found")
        httpCode = HTTPStatus.NOT_FOUND
        oError = Error(httpCode, str(e))
        return jsonify(oError), httpCode

    nLogicalSize = oMediumCreateBaseStorageRequestBody.logicalSize
    variant = swagger_to_vbox_mediumvariant(oMediumCreateBaseStorageRequestBody.variant)

    try:
        oVBoxProgress = oVBoxMedium.createBaseStorage(nLogicalSize, variant)
        # Don't forget to remove the Id from the Map
        del lNewAndNotRegisteredStorage[mediumid]

        if oVBoxProgress is not None:
            oProgressResponse.progress = i_fill_progress(oVBoxProgress)
            logging.info('The medium base storage creation has been successfully started')

            # Add Progress Id object into the tracking lists
            ctx['tracker'][oProgressResponse.progress.id] = None
        else:
            httpCode = HTTPStatus.OK
            oError = Error(httpCode, f"The medium base storage creation has been done without using Progress object")

    except Exception as e:
        logging.info(f"Exception during creation of the base storage of the medium {oVBoxMedium.id}")
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oProgressResponse)
    return response, httpCode


def i_medium_creatediffstorage(mediumid, oMediumCreateDiffStorageRequestBody: MediumCreateDiffStorageRequestBody):  # noqa: E501
    """
    Call interface method IMedium::createDiffStorage

    :param mediumid: The Id of medium
    :type mediumid: str
    :param oMediumCreateDiffStorageRequestBody: 
    :type oMediumCreateDiffStorageRequestBody: dict | bytes

    :rtype: ProgressResponse
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK
    oProgressResponse = ProgressObjWrapperResponse()

    # Trying to get VirtualBox Medium object that had been created earlier
    # mediumid, in this case, is a value returned in oMediumResponse.medium.id (see the function i_virtualbox_createmedium())
    try:
        oVBoxMedium = lNewAndNotRegisteredStorage[mediumid]
    except KeyError as e:
        logging.info(f"The medium Id {mediumid} wasn't found")
        httpCode = HTTPStatus.NOT_FOUND
        oError = Error(httpCode, str(e))
        return jsonify(oError), httpCode

    strTargetId = oMediumCreateDiffStorageRequestBody.target
    oTargetMedium, oError = __find_medium_by_id(strTargetId)
    if oError or oTargetMedium is None:
        logging.info(f"The medium passed as 'target' with Id {strTargetId} wasn't found")
        oError = Error(HTTPStatus.NOT_FOUND, f"The medium passed as 'target' with Id {strTargetId} wasn't found")
        return jsonify(oError), HTTPStatus.NOT_FOUND

    variant = swagger_to_vbox_mediumvariant(oMediumCreateDiffStorageRequestBody.variant)

    try:
        oVBoxProgress = oVBoxMedium.createDiffStorage(strTargetId, variant)
        # Don't forget to remove the Id from the Map
        del lNewAndNotRegisteredStorage[mediumid]

        if oVBoxProgress is not None:
            oProgressResponse.progress = i_fill_progress(oVBoxProgress)
            logging.info('The medium diff base storage creation has been successfully started')

            # Add Progress Id object into the tracking lists
            ctx['tracker'][oProgressResponse.progress.id] = None
        else:
            httpCode = HTTPStatus.OK
            oError = Error(httpCode, f"The medium diff storage creation has been done without using Progress object")

    except Exception as e:
        logging.info(f"Exception during creation of the diff storage of the medium {oVBoxMedium.id}")
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oProgressResponse)
    return response, httpCode


@findMedium_decorator
def i_medium_getencryptionsettings(oVBoxMedium):  # noqa: E501
    """
    Call interface method IMedium::getEncryptionSettings

    :param mediumid: The Id of medium
    :type mediumid: str

    :rtype: MediumGetencryptionsettingsResponse
    """

    oError = None
    httpCode = HTTPStatus.OK
    oMediumGetencryptionsettingsResponse = MediumGetEncryptionSettingsResponse()
    oMediumGetencryptionsettingsResponse.cipher = ""
    oMediumGetencryptionsettingsResponse.passwordId = ""

    try:
        strCipher, strPassId = oVBoxMedium.getEncryptionSettings()
        logging.info('Getting the list of encription settings has been done successfully')
        if strCipher and len(strCipher) !=0:
            oMediumGetencryptionsettingsResponse.cipher = strCipher    
        if strPassId and len(strCipher) !=0:
            oMediumGetencryptionsettingsResponse.passwordId = strPassId

    except Exception as e:
        logging.info(f"Exception during getting the list encription settings")
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oMediumGetencryptionsettingsResponse)
    return response, httpCode


@findMedium_decorator
def i_medium_getproperties(oVBoxMedium, names=None):  # noqa: E501
    """
    Call interface method IMedium::getProperties

    :param mediumid: The Id of medium
    :type mediumid: str
    :param names: 
    :type names: str

    :rtype: MediumGetpropertiesResponse
    """

    oError = None
    httpCode = HTTPStatus.OK
    oMediumGetpropertiesResponse = MediumGetPropertiesResponse(list(), list())

    try:
        lValue, lName = oVBoxMedium.getProperties(names)
        if lName and len(lName) > 0:
            for count, item in enumerate(lName):
                oMediumGetpropertiesResponse.returnNames.append(item)
                if lValue[count] and len(lValue[count])!=0:
                    oMediumGetpropertiesResponse.returnValues.append(lValue[count])
                else:
                    oMediumGetpropertiesResponse.returnValues.append("")
        else:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, f"Something wrong with getting the list of properties {names}")

    except Exception as e:
        logging.info(f"Exception during getting the list of properties {names}")
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oMediumGetpropertiesResponse)
    return response, httpCode


@findMedium_decorator
def i_medium_getproperty(oVBoxMedium, name=None):  # noqa: E501
    """
    Call interface method IMedium::getProperty

    :param mediumid: The Id of medium
    :type mediumid: str
    :param name: 
    :type name: str

    :rtype: MediumGetpropertyResponse
    """

    oError = None
    httpCode = HTTPStatus.OK
    oMediumGetpropertyResponse = MediumGetPropertyResponse()

    try:
        oMediumGetpropertyResponse.value = oVBoxMedium.getProperty(name)

    except Exception as e:
        logging.info(f"Exception during getting the value of property {name}")
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oMediumGetpropertyResponse)
    return response, httpCode


@findMedium_decorator
def i_medium_getsnapshotids(oVBoxMedium, machineId=None):  # noqa: E501
    """
    Call interface method IMedium::getSnapshotIds

    :param mediumid: The Id of medium
    :type mediumid: str
    :param machineId: 
    :type machineId: str

    :rtype: MediumGetsnapshotidsResponse
    """

    oError = None
    httpCode = HTTPStatus.OK
    oMediumGetsnapshotidsResponse = MediumGetSnapshotIdsResponse([])

    try:
        lIds = oVBoxMedium.getSnapshotIds(machineId)
        if lIds is not None and len(lIds) > 0:
            logging.info('Getting the list of snapshots Ids has been done successfully')
            for item in lIds:
                oMediumGetsnapshotidsResponse.snapshotIds.append(item)
        else:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, f"Something wrong with getting the list of snapshots Ids for machine {machineId}")

    except Exception as e:
        logging.info(f"Exception during getting the list of snapshots Ids for machine {machineId}")
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oMediumGetsnapshotidsResponse)
    return response, httpCode


@findMedium_decorator
def i_medium_setids(oVBoxMedium, oMediumSetIdsRequestBody: MediumSetIdsRequestBody):  # noqa: E501
    """
    Call interface method IMedium::setIds

    :param mediumid: The Id of medium
    :type mediumid: str
    :param oMediumSetIdsRequestBody: 
    :type oMediumSetIdsRequestBody: dict | bytes

    :rtype: None
    """

    oError = None
    httpCode = HTTPStatus.OK
    imageId = oMediumSetIdsRequestBody.imageId
    parentId = oMediumSetIdsRequestBody.parentId
    fSetImageId = oMediumSetIdsRequestBody.setImageId
    fSetParentId = oMediumSetIdsRequestBody.setParentId

    try:
        res = oVBoxMedium.setIds(fSetImageId, imageId, fSetParentId, parentId)
        if res != 0:
            nErrorHex = c_uint32(res).value
            strError = f"Error {nErrorHex} during changing the UUID and parent UUID for a hard disk medium"
            logging.info(strError)
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, strError)
    except Exception as e:
        logging.info(f"Exception during changing the UUID and parent UUID for a hard disk medium")
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else "Changing the UUID and parent UUID for a hard disk medium has been done successfully")
    return response, httpCode


@findMedium_decorator
def i_medium_setproperties(oVBoxMedium, oMediumSetPropertiesRequestBody: MediumSetPropertiesRequestBody):  # noqa: E501
    """
    Call interface method IMedium::setProperties

    :param mediumid: The Id of medium
    :type mediumid: str
    :param oMediumSetPropertiesRequestBody: 
    :type oMediumSetPropertiesRequestBody: dict | bytes

    :rtype: None
    """

    oError = None
    httpCode = HTTPStatus.OK
    lNames = oMediumSetPropertiesRequestBody.names
    lValues = oMediumSetPropertiesRequestBody.values

    try:
        # Always returns S_OK (0)
        oVBoxMedium.setProperties(lNames, lValues)

    except Exception as e:
        logging.info(f"Exception during setting the values of the properties {lNames}")
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else f"All properties {lNames} have been set successfully")
    return response, httpCode


@findMedium_decorator
def i_medium_setproperty(oVBoxMedium, oMediumSetPropertyRequestBody: MediumSetPropertyRequestBody):  # noqa: E501
    """
    Call interface method IMedium::setProperty

    :param mediumid: The Id of medium
    :type mediumid: str
    :param oMediumSetPropertyRequestBody: 
    :type oMediumSetPropertyRequestBody: dict | bytes

    :rtype: None
    """

    oError = None
    httpCode = HTTPStatus.OK
    name = oMediumSetPropertyRequestBody.name
    value = oMediumSetPropertyRequestBody.value

    try:
        # Always returns S_OK (0)
        oVBoxMedium.setProperty(name, value)
    except Exception as e:
        logging.info(f"Exception during setting the value of the property {name}")
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else f"Setting the value of the property {name} has been done successfully")
    return response, httpCode


############################# Not implemented yet or not used #############################
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


# def i_medium_checkencryptionpassword(mediumid, password=None):  # noqa: E501
#     """
#     Call interface method IMedium::checkEncryptionPassword

#     :param mediumid: The Id of medium
#     :type mediumid: str
#     :param password: 
#     :type password: str

#     :rtype: None
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_medium_close(mediumid):  # noqa: E501
#     """
#     Call interface method IMedium::close

#     :param mediumid: The Id of medium
#     :type mediumid: str

#     :rtype: None
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_medium_compact(mediumid):  # noqa: E501
    """
    Call interface method IMedium::compact

    :param mediumid: The Id of medium
    :type mediumid: str

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


# def i_medium_lockread(mediumid):  # noqa: E501
#     """
#     Call interface method IMedium::lockRead

#     :param mediumid: The Id of medium
#     :type mediumid: str

#     :rtype: TokenResponse
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_medium_lockwrite(mediumid):  # noqa: E501
#     """
#     Call interface method IMedium::lockWrite

#     :param mediumid: The Id of medium
#     :type mediumid: str

#     :rtype: TokenResponse
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_medium_refreshstate(mediumid):  # noqa: E501
#     """
#     Call interface method IMedium::refreshState

#     :param mediumid: The Id of medium
#     :type mediumid: str

#     :rtype: MediumStateResponse
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


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
