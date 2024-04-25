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
from vbox_server.models.progress_response import ProgressResponse  # noqa: E501
from vbox_server.models.progress_wait_for_operation_completion_request_body import ProgressWaitForOperationCompletionRequestBody  # noqa: E501
from vbox_server import util


def i_progress_cancel(progressid):  # noqa: E501
    """
    Call interface method IProgress::cancel

    :param progressid: The Id of progress
    :type progressid: str

    :rtype: None
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_progress_waitforcompletion(progressid, timeout=None):  # noqa: E501
    """
    Call interface method IProgress::waitForCompletion

    :param progressid: The Id of progress
    :type progressid: str
    :param timeout: 
    :type timeout: int

    :rtype: None
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_progress_waitforoperationcompletion(progressid, oProgressWaitForOperationCompletionRequestBody):  # noqa: E501
    """
    Call interface method IProgress::waitForOperationCompletion

    :param progressid: The Id of progress
    :type progressid: str
    :param oProgressWaitForOperationCompletionRequestBody: 
    :type oProgressWaitForOperationCompletionRequestBody: dict | bytes

    :rtype: None
    """
    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_virtualbox_findprogressbyid(progressid, select=None, id=None):  # noqa: E501
    """
    Call interface method IVirtualBox::findProgressById

    :param progressid: The Id of progress
    :type progressid: str
    :param select: The object attributes separated by comma
    :type select: str
    :param id: 
    :type id: str

    :rtype: ProgressResponse
    """
    oError = None
    httpCode = 200 #(OK)

    vbox_utils_commonChecks()

    try:
        oVBox = ctx['vb']
    except Exception as e:
        logging.info ('couldn\'t get the VirtualBox object')
        oError = Error(500, str(e))
        return jsonify(oError), 500

    oProgressResponse = ProgressResponse()
    try:
        oVBoxProgress = oVBox.findProgressById(progressid)
        oProgressResponse.progress = i_fill_progress(oVBoxProgress)
    except Exception as e:
        httpCode = 500
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oProgressResponse)
    return response, httpCode

