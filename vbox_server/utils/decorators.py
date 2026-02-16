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


def _make_resolving_decorator(not_found_label: str, resolver):
    """
    not_found_label: the line for the messages ("DHCP server", "DHCP config" и т.п.)
    resolver(first_arg) -> (resolved_obj or None, oError or None)
    """
    def _decorator(func):
        @functools.wraps(func)
        def _wrapper(*args, **kwargs):
            if not args:
                return jsonify(f"{not_found_label} lookup requires the first positional argument"), HTTPStatus.BAD_REQUEST

            args_list = list(args)
            original_first = args_list[0]

            try:
                resolved_obj, oError = resolver(original_first)
            except Exception as e:
                return jsonify(f"Failed to resolve {not_found_label} for '{original_first}': {e}"), HTTPStatus.INTERNAL_SERVER_ERROR

            if resolved_obj is None:
                if oError:
                    return jsonify(
                        f"The {not_found_label} for '{original_first}' wasn't found. Internal error is '{oError.message}'"
                    ), oError.code
                else:
                    return jsonify(
                        f"The {not_found_label} for '{original_first}' wasn't found"
                    ), HTTPStatus.NOT_FOUND

            args_list[0] = resolved_obj
            return func(*tuple(args_list), **kwargs)
        return _wrapper
    return _decorator


def __find_server():
    oError = None
    oVBoxObj = None
    try:
        oVBoxObj = ctx['vb']
    except Exception as e:
        logging.info('Exception during getting VirtualBox object')
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    return oVBoxObj, oError


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

        args_repr[0] = oSession
        new_args_repr = args_repr

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

        value = func(*new_args_repr, **kwargs)

        return value

    return wrapper_decorator


def openSessionDecorator(func):
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
        if oErr:
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

        args_repr[0] = oSession
        new_args_repr = args_repr

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

        if isinstance(vmid, str):
            oVBoxMachine, oError = vbox_utils_find_machine(vmid)
            if oVBoxMachine:
                args_repr[0] = oVBoxMachine
            else:
                logging.info (oError)
                return jsonify(oError), HTTPStatus.NOT_FOUND
        else:
            oVBoxMachine = vmid
        
        if oVBoxMachine is not None:
            args_repr[0] = oVBoxMachine
        else:
            if oError:
                return jsonify('The machine with UUID ' + vmid + ' wasn\'t found. Internal error is ' + '"' + oError.message + '"'), oError.code
            else:
                return jsonify("The machine with UUID " + vmid + " wasn't found"), HTTPStatus.NOT_FOUND

        new_args_repr=args_repr
        value = func(*new_args_repr, **kwargs)

        return value

    return wrapper_decorator


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

        oVBoxMedium, oError = vbox_utils_find_medium(mediumid)
        if oVBoxMedium is not None:
            args_repr[0] = oVBoxMedium
        else:
            if oError:
                return jsonify('The medium with UUID ' + mediumid + ' wasn\'t found. Internal error is ' + '"' + oError.message + '"'), oError.code
            else:
                return jsonify("The medium with UUID " + mediumid + " wasn't found"), HTTPStatus.NOT_FOUND

        new_args_repr=args_repr

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

        if isinstance(vmid, str):
            oVBoxMachine, oErr = vbox_utils_find_machine(vmid)
            if oVBoxMachine:
                args_repr[0] = oVBoxMachine
            else:
                logging.info (oErr)
                return jsonify(oErr), HTTPStatus.NOT_FOUND
        else:
            oVBoxMachine = vmid

        vmid = oVBoxMachine.id
        
        if oVBoxMachine is not None:
            strArchtype = vbox_to_swagger_platformarchitecture(oVBoxMachine.platform.architecture)
            if strArchtype == "ARM":
                args_repr[0] = oVBoxMachine.platform.arm
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

        if isinstance(vmid, str):
            oVBoxMachine, oErr = vbox_utils_find_machine(vmid)
            if oVBoxMachine:
                args_repr[0] = oVBoxMachine
            else:
                logging.info (oErr)
                return jsonify(oErr), HTTPStatus.NOT_FOUND
        else:
            oVBoxMachine = vmid

        vmid = oVBoxMachine.id
        
        if oVBoxMachine is not None:
            strArchtype = vbox_to_swagger_platformarchitecture(oVBoxMachine.platform.architecture)
            if strArchtype == "X86":
                args_repr[0] = oVBoxMachine.platform.x86
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
        for a in args_repr[1:]:
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
            args_repr[0] = oVBoxNATNetwork
        else:
            if oError:
                return jsonify(f"The NAT Network with name '{networkName}' wasn't found. Internal error is {oError.message}"), oError.code
            else:
                return jsonify(f"The NAT Network with name '{networkName}' wasn't found"), HTTPStatus.NOT_FOUND

        new_args_repr=args_repr

        value = func(*new_args_repr, **kwargs)

        return value

    return wrapper_decorator


def _resolve_dhcpserver_by_networkname(networkname):
    oReqObj, oError = __find_dhcpserver_by_networkname(networkname)
    return oReqObj, oError

dhcpserverDecorator = _make_resolving_decorator("DHCP server", _resolve_dhcpserver_by_networkname)


def _resolve_dhcpconfig_by_networkname(networkname):
    oVBoxDHCPServer, oError = __find_dhcpserver_by_networkname(networkname)
    if oVBoxDHCPServer is None:
        return None, oError
    try:
        oReqObj = oVBoxDHCPServer.getConfig()
        return oReqObj, None
    except Exception as e:
        return None, Error(HTTPStatus.INTERNAL_SERVER_ERROR, str(e))

dhcpconfigDecorator = _make_resolving_decorator("DHCP config", _resolve_dhcpconfig_by_networkname)


def _resolve_host():
    oVBoxObj, oError = __find_server()
    if oVBoxObj is None:
        return None, oError
    try:
        oReqObj = oVBoxObj.host
        return oReqObj, None
    except Exception as e:
        return None, Error(HTTPStatus.INTERNAL_SERVER_ERROR, str(e))

def hostDecorator(func):
    """
    Find the "host" object
    Appends the arguments list by the flag which indicates whether the host was found or not
    """
    @functools.wraps(func)
    def wrapper_decorator(*args, **kwargs):
        args_repr = [a for a in args]
        new_args_repr =[]
        oVBoxObj, oError = __find_server()
        if oVBoxObj is not None:
            new_args_repr.append(oVBoxObj.host)
        else:
            if oError:
                return jsonify(f"The Host wasn't found. Internal error is {oError.message}"), oError.code
            else:
                return jsonify(f"The Host wasn't found"), HTTPStatus.NOT_FOUND
        
        for a in args_repr:
            new_args_repr.append(a)
            
        value = func(*new_args_repr, **kwargs)

        return value

    return wrapper_decorator


def _resolve_systemproperties():
    oVBoxObj, oError = __find_server()
    if oVBoxObj is None:
        return None, oError
    try:
        oReqObj = oVBoxObj.systemProperties
        return oReqObj, None
    except Exception as e:
        return None, Error(HTTPStatus.INTERNAL_SERVER_ERROR, str(e))
    
systempropertiesDecorator = _make_resolving_decorator("System properties", _resolve_systemproperties)


def _resolve_platformproperties():
    oVBoxObj, oError = _resolve_systemproperties()
    if oVBoxObj is None:
        return None, oError
    try:
        oReqObj = oVBoxObj.platform
        return oReqObj, None
    except Exception as e:
        return None, Error(HTTPStatus.INTERNAL_SERVER_ERROR, str(e))

platformpropertiesDecorator = _make_resolving_decorator("Platform", _resolve_platformproperties)


def _resolve_progress(progressId: str):
    oVBoxObj, oError = __find_server()
    if oVBoxObj is None:
        return None, oError
    try:
        oReqObj = oVBoxObj.findPfindProgressById(progressId)
        return oReqObj, None
    except Exception as e:
        return None, Error(HTTPStatus.INTERNAL_SERVER_ERROR, str(e))

progressDecorator = _make_resolving_decorator("Progress", _resolve_progress)

syntheticDecorator = virtualboxDecorator
mediumformatDecorator = virtualboxDecorator

dhcpgroupconfigDecorator = commonObjDecorator
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
dndbaseDecorator = commonObjDecorator
dndtargetDecorator = commonObjDecorator
guestsessionDecorator = commonObjDecorator
processDecorator = commonObjDecorator
directoryDecorator = commonObjDecorator
fileDecorator = commonObjDecorator
guestDecorator = commonObjDecorator
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
