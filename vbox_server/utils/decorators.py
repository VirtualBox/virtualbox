"""VBox REST API

Copyright (c) 2025 Oracle and/or its affiliates.
Licensed under the Universal Permissive License v 1.0 as shown at https://oss.oracle.com/licenses/upl

SPDX-License-Identifier: UPL-1.0
"""

import functools
import logging
import os
import inspect
from vbox_server.global_settings import *
from vbox_server.models.error import Error
from vbox_server.utils.vbox_utils import *
from http import HTTPStatus
from flask import jsonify

from vbox_server.utils.object_conversion import *
from vbox_server.utils.enum_conversion import *

def get_default_args(func):
    signature = inspect.signature(func)
    print (signature)
    return {
        k: v.default
        for k, v in signature.parameters.items()
        # if v.default is not inspect.Parameter.empty
    }


def sessionDecorator(func):
    """
    Automatically open a session for VM and close the session in the end.
    The first parameter must be VM uuid always.
    Appends the arguments list by the VirtualBox objects Machine and Session
    """
    @functools.wraps(func)
    def wrapper_decorator(*args, **kwargs):
        args_repr = [a for a in args]
        vmid = args_repr[0]
        oError = None

        if isinstance(vmid, str):
            oVM, oErr = vbox_utils_find_machine(vmid)
            if oVM:
                args_repr[0] = oVM
            else:
                logging.info (oErr)
                return jsonify(oErr), HTTPStatus.NOT_FOUND
        else:
            oVM = vmid

        #Open machine session
        oSession = None
        try:
            oSession = ctx['global'].openMachineSession(oVM)
        except Exception as e:
            logging.info("Session to '%s' not open: %s" % (oVM.name, str(e)))
            oError = Error(HTTPStatus.INTERNAL_SERVER_ERROR, str(e))
            return jsonify(oError), HTTPStatus.INTERNAL_SERVER_ERROR

        if oSession.state != ctx['const'].SessionState_Locked:
            logging.info("Session to '%s' in wrong state: %s" % (oVM.name, oSession.state))
            oSession.unlockMachine()
            oError = Error(HTTPStatus.PRECONDITION_FAILED, "Session to '%s' in wrong state: %s" % (oVM.name, oSession.state))
            return jsonify(oError), HTTPStatus.PRECONDITION_FAILED

        logging.info ('MachineState is ' +  ctx['global'].getEnumValueName('MachineState', oSession.machine.state))
        logging.info ('Session state is ' + ctx['global'].getEnumValueName('SessionState', oSession.state))

        new_args_repr = []
        new_args_repr.append(oSession.machine)
        if len(args_repr) != 0:
            if len(args_repr) > 1:
                if len(args_repr) == 2:
                    for a in args_repr[1:]:
                        new_args_repr.append(a)
                if len(args_repr) == 3:
                    for a in args_repr[2:]:
                        new_args_repr.append(a)

        value = func(*new_args_repr, **kwargs)

        # Always save setting for assurance
        oSession.machine.saveSettings()
        
        #Close the machine session
        if oSession is not None:
            # Try close it.
            try:
                if oSession.state == ctx['const'].SessionState_Locked:
                    oSession.unlockMachine()
                    logging.info ('Unlocked the current machine ' + oVM.id)
                    oSession = None
            except:
                # Kludge to ignore VBoxSVC's closing of our session when the
                # direct session closes / VM process terminates.
                try:    fIgnore = oSession.state == ctx['const'].SessionState_Unlocked
                except: fIgnore = False
            
                if fIgnore:
                    oSession  = None # Must prevent a retry during GC.
                else:
                    logging.warning ('ISession::unlockMachine failed on %s' % (oSession))

        return value

    return wrapper_decorator


def open_exclusive_session(func):
    """
    Automatically open a session for VM.
    Try to get an exclusive access rights to a machine (LockType_Write)
    The first parameter must be VM uuid always.
    Appends the arguments list by the VirtualBox objects Machine and Session
    """
    @functools.wraps(func)
    def wrapper_decorator(*args, **kwargs):
        args_repr = [a for a in args]
        vmid = args_repr[0]
        oError = None

        oVM, oErr = vbox_utils_find_machine(vmid)
        if oVM:
            #append the Machine object at the end of the argument's list
            args_repr.append(oVM)
        else:
            logging.info (oErr)
            return jsonify(oErr), HTTPStatus.NOT_FOUND

        #Open machine session with LockType_Write (call openMachineSession with False)
        oSession = None
        try:
            oSession = ctx['global'].openMachineSession(oVM, False)
        except Exception as e:
            logging.info("Session to '%s' not open: %s" % (oVM.name, str(e)))
            oError = Error(HTTPStatus.INTERNAL_SERVER_ERROR, str(e))
            return jsonify(oError), HTTPStatus.INTERNAL_SERVER_ERROR

        if oSession.state != ctx['const'].SessionState_Locked:
            logging.info("Session to '%s' in wrong state: %s" % (oVM.name, oSession.state))
            oSession.unlockMachine()
            oError = Error(HTTPStatus.PRECONDITION_FAILED, "Session to '%s' in wrong state: %s" % (oVM.name, oSession.state))
            return jsonify(oError), HTTPStatus.PRECONDITION_FAILED

        logging.info ('MachineState is ' +  ctx['global'].getEnumValueName('MachineState', oSession.machine.state))
        logging.info ('Session state is ' + ctx['global'].getEnumValueName('SessionState', oSession.state))
        logging.info ('Session type is ' + ctx['global'].getEnumValueName('SessionType', oSession.type))

        #Add the Session object into args list, next create new tuple from the updated list
        #and pass it to the func
        args_repr.append(oSession)
        new_args_repr=[a for a in args_repr]

        #Call the general function with the updated arguments list
        value = func(*new_args_repr, **kwargs)

        return value

    return wrapper_decorator


def open_session(func):
    """
    Automatically open a session for VM.
    Try to get a shared access rights to a machine (LockType_Shared)
    The first parameter must be VM uuid always.
    Appends the arguments list by the VirtualBox objects Machine and Session
    """
    @functools.wraps(func)
    def wrapper_decorator(*args, **kwargs):
        args_repr = [a for a in args]
        vmid = args_repr[0]
        oError = None

        oVM, oErr = vbox_utils_find_machine(vmid)
        if oVM:
            #append the Machine object at the end of the argument's list
            args_repr.append(oVM)
        else:
            logging.info (oErr)
            return jsonify(oErr), HTTPStatus.NOT_FOUND

        oSession = None
        try:
            oSession = ctx['global'].openMachineSession(oVM)
        except Exception as e:
            logging.info("Session to '%s' not open: %s" % (oVM.name, str(e)))
            oError = Error(HTTPStatus.INTERNAL_SERVER_ERROR, str(e))
            return jsonify(oError), HTTPStatus.INTERNAL_SERVER_ERROR

        if oSession.state != ctx['const'].SessionState_Locked:
            logging.info("Session to '%s' in wrong state: %s" % (oVM.name, oSession.state))
            oSession.unlockMachine()
            oError = Error(HTTPStatus.PRECONDITION_FAILED, "Session to '%s' in wrong state: %s" % (oVM.name, oSession.state))
            return jsonify(oError), HTTPStatus.PRECONDITION_FAILED

        logging.info ('MachineState is ' +  ctx['global'].getEnumValueName('MachineState', oSession.machine.state))
        logging.info ('Session state is ' + ctx['global'].getEnumValueName('SessionState', oSession.state))
        logging.info ('Session type is ' + ctx['global'].getEnumValueName('SessionType', oSession.type))

        #Add the Session object into args list, next create new tuple from the updated list
        #and pass it to the func
        args_repr.append(oSession)
        new_args_repr=[a for a in args_repr]

        #Call the general function with the updated arguments list
        value = func(*new_args_repr, **kwargs)

        return value

    return wrapper_decorator


def machineDecorator(func):
    """
    Find Machine by Id
    """
    @functools.wraps(func)
    def wrapper_decorator(*args, **kwargs):
        args_repr = [a for a in args]
        vmid = args_repr[0]
        oError = None

        oVBoxMachine, oError = vbox_utils_find_machine(vmid)
        
        if oVBoxMachine is not None:
            args_repr[0] = oVBoxMachine #replace the first argument "machineid" by oVBoxMachine
        else:
            if oError:
                return jsonify('The machine with UUID ' + vmid + ' wasn\'t found. Internal error is ' + '"' + oError.message + '"'), oError.code
            else:
                return jsonify("The machine with UUID " + vmid + " wasn't found"), HTTPStatus.NOT_FOUND

        new_args_repr=args_repr
        value = func(*new_args_repr, **kwargs)

        return value

    return wrapper_decorator


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


def mediumDecorator(func):
    """
    Find the medium object using the passed ID
    The first parameter must be medium uuid always.
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


def virtualboxDecorator(func):

    @functools.wraps(func)
    def wrapper_decorator(*args, **kwargs):
        args_repr = [a for a in args]
        oError = None

        try:
            oVBoxObj = ctx['vb']
        except Exception as e:
            logging.info('Exception during getting VirtualBox object')
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))

        new_args_repr = []
        if oError is None and oVBoxObj is not None:
            new_args_repr.append(oVBoxObj)
            if len(args_repr) != 0:
                if len(args_repr) > 1:
                    if len(args_repr) == 2:
                        for a in args_repr[1:]:
                            new_args_repr.append(a)
                    if len(args_repr) == 3:
                        for a in args_repr[2:]:
                            new_args_repr.append(a)
                else:
                    for a in args_repr:
                        new_args_repr.append(a)

        else:
            if oError:
                return jsonify('Exception during getting VirtualBox object. Internal error is ' + '"' + oError.message + '"'), oError.code
            else:
                return jsonify('Exception during getting VirtualBox object'), HTTPStatus.NOT_FOUND

        value = func(*new_args_repr, **kwargs)

        return value

    return wrapper_decorator


def __find_obj_by_id(idname: str):
    return idname


def commonObjDecorator(func):

    @functools.wraps(func)
    def wrapper_decorator(*args, **kwargs):
        args_repr = [a for a in args]
        id = args_repr[0]
        oVBoxObj = None

        oVBoxObj, oError = __find_obj_by_id(id)
        if oVBoxObj is not None:
            args_repr[0] = oVBoxObj
        else:
            if oError:
                return jsonify('The object with ' + id + ' wasn\'t found. Internal error is ' + '"' + oError.message + '"'), oError.code
            else:
                return jsonify('The object with ' + id + ' wasn\'t found'), HTTPStatus.NOT_FOUND

        new_args_repr=args_repr
        value = func(*new_args_repr, **kwargs)

        return value

    return wrapper_decorator


def platformarmDecorator(func):
    """
    Find oVBoxMachine.platform.arm object
    """
    @functools.wraps(func)
    def wrapper_decorator(*args, **kwargs):
        args_repr = [a for a in args]
        vmid = args_repr[0]
        oError = None

        oVBoxMachine, oError = vbox_utils_find_machine(vmid)
        
        if oVBoxMachine is not None:
            strArchtype = vbox_to_swagger_platformarchitecture(oVBoxMachine.platform.architecture)
            if strArchtype == "ARM":
                args_repr[0] = oVBoxMachine.platform.arm #replace the first argument by oVBoxMachine.platform.arm
            else:
                httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
                oError = Error(httpCode, "Virtual CPU isn't ARM architecture CPU")
                return jsonify(oError), httpCode
        else:
            if oError:
                return jsonify('The machine with UUID ' + vmid + ' wasn\'t found. Internal error is ' + '"' + oError.message + '"'), oError.code
            else:
                return jsonify("The machine with UUID " + vmid + " wasn't found"), HTTPStatus.NOT_FOUND

        new_args_repr=args_repr
        value = func(*new_args_repr, **kwargs)

        return value

    return wrapper_decorator


def platformx86Decorator(func):
    """
    Find oVBoxMachine.platform.x86 object
    """
    @functools.wraps(func)
    def wrapper_decorator(*args, **kwargs):
        args_repr = [a for a in args]
        vmid = args_repr[0]
        oError = None

        oVBoxMachine, oError = vbox_utils_find_machine(vmid)
        
        if oVBoxMachine is not None:
            strArchtype = vbox_to_swagger_platformarchitecture(oVBoxMachine.platform.architecture)
            if strArchtype == "X86":
                args_repr[0] = oVBoxMachine.platform.x86 #replace the first argument by oVBoxMachine.platform.x86
            else:
                httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
                oError = Error(httpCode, "Virtual CPU isn't X86 architecture CPU")
                return jsonify(oError), httpCode
        else:
            if oError:
                return jsonify('The machine with UUID ' + vmid + ' wasn\'t found. Internal error is ' + '"' + oError.message + '"'), oError.code
            else:
                return jsonify("The machine with UUID " + vmid + " wasn't found"), HTTPStatus.NOT_FOUND

        new_args_repr=args_repr
        value = func(*new_args_repr, **kwargs)

        return value

    return wrapper_decorator


def consoleDecorator(func):
    """
    Find Console object
    Automatically open a session for VM and close the session in the end.
    The first parameter must be VM uuid always.
    """
    @functools.wraps(func)
    def wrapper_decorator(*args, **kwargs):
        args_repr = [a for a in args]
        vmid = args_repr[0]
        oError = None

        if isinstance(vmid, str):
            oVM, oErr = vbox_utils_find_machine(vmid)
            if oVM:
                args_repr[0] = oVM
            else:
                logging.info (oErr)
                return jsonify(oErr), HTTPStatus.NOT_FOUND
        else:
            oVM = vmid

        #Open machine session
        oSession = None
        try:
            oSession = ctx['global'].openMachineSession(oVM)
        except Exception as e:
            logging.info("Session to '%s' not open: %s" % (oVM.name, str(e)))
            oError = Error(HTTPStatus.INTERNAL_SERVER_ERROR, str(e))
            return jsonify(oError), HTTPStatus.INTERNAL_SERVER_ERROR

        if oSession.state != ctx['const'].SessionState_Locked:
            logging.info("Session to '%s' in wrong state: %s" % (oVM.name, oSession.state))
            oSession.unlockMachine()
            oError = Error(HTTPStatus.PRECONDITION_FAILED, "Session to '%s' in wrong state: %s" % (oVM.name, oSession.state))
            return jsonify(oError), HTTPStatus.PRECONDITION_FAILED

        logging.info ('MachineState is ' +  ctx['global'].getEnumValueName('MachineState', oSession.machine.state))
        logging.info ('Session state is ' + ctx['global'].getEnumValueName('SessionState', oSession.state))

        new_args_repr = []
        new_args_repr.append(oSession.console)
        if len(args_repr) != 0:
            if len(args_repr) > 1:
                if len(args_repr) == 2:
                    for a in args_repr[1:]:
                        new_args_repr.append(a)
                if len(args_repr) == 3:
                    for a in args_repr[2:]:
                        new_args_repr.append(a)
            
        value = func(*new_args_repr, **kwargs)

        # Always save setting for assurance
        oSession.machine.saveSettings()

        #Close the machine session
        if oSession is not None:
            # Try close it.
            try:
                if oSession.state == ctx['const'].SessionState_Locked:
                    oSession.unlockMachine()
                    logging.info ('Unlocked the current machine ' + oVM.id)
                    oSession = None
            except:
                # Kludge to ignore VBoxSVC's closing of our session when the
                # direct session closes / VM process terminates.
                try:    fIgnore = oSession.state == ctx['const'].SessionState_Unlocked
                except: fIgnore = False
            
                if fIgnore:
                    oSession  = None # Must prevent a retry during GC.
                else:
                    logging.warning ('ISession::unlockMachine failed on %s' % (oSession))

        return value

    return wrapper_decorator


def __find_dhcpserver_by_networkname(networkname: str):
    oVBox = ctx['vb']
    oFoundItem = None
    oError = None

    try:
        olDHCPServers = ctx['global'].getArray(oVBox,'DHCPServers')
        for item in olDHCPServers:
            if str(item.networkName) == networkname:
                oFoundItem = item
                break

    except Exception as e:
        logging.info('Error walking through the array of DHCPServers')
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))
        oFoundItem = None

    return oFoundItem, oError


def dhcpserverDecorator(func):
    """
    Searches a DHCP server settings to be used for the given internal network name
    The first parameter must be network name always.
    Appends the arguments list by the flag which indicates whether the DHCP server was found or not
    """
    @functools.wraps(func)
    def wrapper_decorator(*args, **kwargs):
        args_repr = [a for a in args]
        networkname = args_repr[0]
        oVBoxDHCPServer = None

        oVBoxDHCPServer, oError = __find_dhcpserver_by_networkname(networkname)
        if oVBoxDHCPServer is not None:
            args_repr[0] = oVBoxDHCPServer
        else:
            if oError:
                return jsonify(f"The DHCP server with internal network name '{networkname}' wasn't found. Internal error is '{oError.message}'"), oError.code
            else:
                return jsonify(f"The DHCP server with internal network name '{networkname}' wasn't found"), HTTPStatus.NOT_FOUND

        new_args_repr=args_repr

        value = func(*new_args_repr, **kwargs)

        return value

    return wrapper_decorator

def __find_hostonlynetwork_by_name(name: str):
    oVBox = ctx['vb']
    oFoundItem = None
    oError = None

    try:
        olHostOnlyNetworks = ctx['global'].getArray(oVBox,'HostOnlyNetworks')
        for item in olHostOnlyNetworks:
            if str(item.networkName) == name:
                oFoundItem = item
                break

    except Exception as e:
        logging.info('Error walking through the array of hostOnlyNetworks')
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))
        oFoundItem = None

    return oFoundItem, oError


def hostonlyNetworkDecorator(func):
    """
    Find the "hostonly" Network object using the passed network name
    The first parameter must be network name always.
    Appends the arguments list by the flag which indicates whether the network was found or not
    """
    @functools.wraps(func)
    def wrapper_decorator(*args, **kwargs):
        args_repr = [a for a in args]
        networkName = args_repr[0]
        oVBoxHostOnlyNetwork = None

        oVBoxHostOnlyNetwork, oError = __find_hostonlynetwork_by_name(networkName)
        if oVBoxHostOnlyNetwork is not None:
            args_repr[0] = oVBoxHostOnlyNetwork
        else:
            if oError:
                return jsonify(f"The HostOnly Network with name '{networkName}' wasn't found. Internal error is {oError.message}"), oError.code
            else:
                return jsonify(f"The HostOnly Network with name '{networkName}' wasn't found"), HTTPStatus.NOT_FOUND

        new_args_repr=args_repr

        value = func(*new_args_repr, **kwargs)

        return value

    return wrapper_decorator


def __find_natnetwork_by_name(name: str):
    oVBox = ctx['vb']
    oFoundItem = None
    oError = None

    try:
        olNatNetworks = ctx['global'].getArray(oVBox,'NATNetworks')
        for item in olNatNetworks:
            if str(item.networkName) == name:
                oFoundItem = item
                break

    except Exception as e:
        logging.info('Error walking through the array of NATNetworks')
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))
        oFoundItem = None

    return oFoundItem, oError


def natnetworkDecorator(func):
    """
    Find the NAT Network object using the passed ID
    The first parameter must be network name always.
    Appends the arguments list by the flag which indicates whether the network was found or not
    """
    @functools.wraps(func)
    def wrapper_decorator(*args, **kwargs):
        args_repr = [a for a in args]
        networkName = args_repr[0]
        oVBoxNATNetwork = None

        oVBoxNATNetwork, oError = __find_natnetwork_by_name(networkName)
        if oVBoxNATNetwork is not None:
            #append the NAT Network object at the end of the argument's list
            args_repr[0] = oVBoxNATNetwork #replace the first argument "networkid" by oVBoxNATNetwork
        else:
            if oError:
                return jsonify(f"The NAT Network with name '{networkName}' wasn't found. Internal error is {oError.message}"), oError.code
            else:
                return jsonify(f"The NAT Network with name '{networkName}' wasn't found"), HTTPStatus.NOT_FOUND

        # new_args_repr=args_repr[1:] #remove the first argument "networkName" from the argument list because we added oVBoxNATNetwork into the end of one
        new_args_repr=args_repr

        #Call the general function with the updated arguments list
        value = func(*new_args_repr, **kwargs)

        return value

    return wrapper_decorator


dhcpgroupconfigDecorator = commonObjDecorator
dhcpconfigDecorator = commonObjDecorator
dhcpgroupconditionDecorator = commonObjDecorator
certificateDecorator = commonObjDecorator
applianceDecorator = commonObjDecorator
virtualsystemdescriptionDecorator = commonObjDecorator
unattendedDecorator = commonObjDecorator
internalmachinecontrolDecorator = commonObjDecorator
graphicsadapterDecorator = commonObjDecorator
recordingscreensettingsDecorator = commonObjDecorator
recordingsettingsDecorator = commonObjDecorator
pciaddressDecorator = commonObjDecorator
uefivariablestoreDecorator = commonObjDecorator
nvramstoreDecorator = commonObjDecorator
emulatedusbDecorator = commonObjDecorator
hostnetworkinterfaceDecorator = commonObjDecorator
updateagentDecorator = commonObjDecorator
hostDecorator = commonObjDecorator
platformpropertiesDecorator = commonObjDecorator
systempropertiesDecorator = commonObjDecorator
dndbaseDecorator = commonObjDecorator
dndtargetDecorator = commonObjDecorator
guestsessionDecorator = commonObjDecorator
processDecorator = commonObjDecorator
directoryDecorator = commonObjDecorator
fileDecorator = commonObjDecorator
guestDecorator = commonObjDecorator
progressDecorator = commonObjDecorator
internalprogresscontrolDecorator = commonObjDecorator
mediumioDecorator = commonObjDecorator
tokenDecorator = commonObjDecorator
keyboardDecorator = commonObjDecorator
displayDecorator = commonObjDecorator
networkadapterDecorator = commonObjDecorator
machinedebuggerDecorator = commonObjDecorator
usbdevicefiltersDecorator = commonObjDecorator
audioadapterDecorator = commonObjDecorator
hostaudiodeviceDecorator = commonObjDecorator
audiosettingsDecorator = commonObjDecorator
vrdeserverDecorator = commonObjDecorator
internalsessioncontrolDecorator = commonObjDecorator
managedobjectrefDecorator = commonObjDecorator
websessionmanagerDecorator = commonObjDecorator
natengineDecorator = commonObjDecorator
extpackDecorator = commonObjDecorator
extpackmanagerDecorator = commonObjDecorator
bandwidthcontrolDecorator = commonObjDecorator
virtualboxclientDecorator = commonObjDecorator
eventsourceDecorator = commonObjDecorator
eventlistenerDecorator = commonObjDecorator
eventDecorator = commonObjDecorator
reusableeventDecorator = commonObjDecorator
vetoeventDecorator = commonObjDecorator
booleanformvalueDecorator = commonObjDecorator
rangedintegerformvalueDecorator = commonObjDecorator
rangedinteger64formvalueDecorator = commonObjDecorator
stringformvalueDecorator = commonObjDecorator
choiceformvalueDecorator = commonObjDecorator
virtualsystemdescriptionformDecorator = commonObjDecorator
cloudmachineDecorator = commonObjDecorator
cloudclientDecorator = commonObjDecorator
cloudprofileDecorator = commonObjDecorator
cloudproviderDecorator = commonObjDecorator
cloudprovidermanagerDecorator = commonObjDecorator
vboxsvcregistrationDecorator = commonObjDecorator
reusableeventDecorator = commonObjDecorator