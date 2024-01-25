# pylint: disable=invalid-name
# pylint: disable=consider-using-f-string
# pylint: disable=line-too-long
# pylint: disable=undefined-variable

import sys
import logging

from vbox_server.global_settings import *

from vbox_server.models.error import Error  # noqa: E501
from vboxapi import VirtualBoxManager

# Set logging level for module
logging.getLogger().setLevel(logging.INFO)

# Python 3 hacks:
if sys.version_info[0] >= 3:
    long = int    # pylint: disable=redefined-builtin,invalid-name
    xrange = range # pylint: disable=redefined-builtin,invalid-name

#
# Utility methods.
#
def vbox_utils_logVmInfo(oVM):
    logging.info ("  Name:               %s" % (oVM.name,))
    logging.info ("  ID:                 %s" % (oVM.id,))
    oOsType = ctx['vb'].getGuestOSType(oVM.OSTypeId)
    logging.info ("  OS Type:            %s - %s" % (oVM.OSTypeId, oOsType.description,))
    logging.info ("  Machine state:      %s" % (ctx['global'].getEnumValueName('MachineState', oVM.state),))
    logging.info ("  Session state:      %s" % (ctx['global'].getEnumValueName('SessionState', oVM.sessionState),))
    logging.info ("  Session Name:       %s" % (oVM.sessionName,))
    logging.info ("  CPUs:               %s" % (oVM.CPUCount,))
    logging.info ("  RAM:                %sMB" % (oVM.memorySize,))
    o = ctx['global'].getArray(oVM, 'mediumAttachments')
    logging.info ("  Mediums:            %s" % (len(o),))
    o = ctx['global'].getArray(oVM, 'storageControllers')
    logging.info ("  Storage Controllers:%s" % (len(o),))


def vbox_utils_find_machine(vmid):
    oError = None
    oVM = None
    try:
        oVM = ctx['vb'].findMachine(vmid)
        logging.info ('Found id ' + oVM.id)      
    except Exception as e:
        oError = Error(404, str(e))
    
    return oVM, oError


def vbox_utils_commonChecks():
    logging.info ('global_settings.oVBoxMgr is ' + str(ctx['global']))
    logging.info ('global_settings.oVBox is ' + str(ctx['vb']))

    if (ctx['global'] is None):
        ctx['global'] = VirtualBoxManager(None, None)

    sPyVer = ctx['global'].getPythonApiRevision()

    if (ctx['vb'] is None): 
        ctx['vb'] = ctx['global'].getVirtualBox()

    logging.info ('VirtualBox version is ' + str(ctx['global'].getVirtualBox()))    
    logging.info ('Python binding version is ' + str(sPyVer))

def vbox_utils_detachVmDevice(oVM, mediumid='ALL'):
    olVBoxMediumAttachments = ctx['global'].getArray(oVM, 'mediumAttachments')
    for item in olVBoxMediumAttachments:
        if item.medium:
            if mediumid == "ALL" or item.medium.id == mediumid:
                oVM.detachDevice(item.controller, item.port, item.device)
