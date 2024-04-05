import functools
import logging
from vbox_server.global_settings import *
from vbox_server.models.error import Error
from vbox_server.utils.vbox_utils import *
from http import HTTPStatus
from flask import jsonify


def session_decorator(func):
    """
    Automatically open a session for VM and close the session in the end.
    The first parameter must be VM uuid always.
    Appends the arguments list by the VirtualBox objects Machine and Session
    """
    @functools.wraps(func)
    def wrapper_decorator(*args, **kwargs):
        args_repr = [a for a in args]        
        vmid = args_repr[0]
        oVM, oError = vbox_utils_find_machine(vmid)
        if oError is None:
            #append the Machine object at the end of the argument's list
            args_repr.append(oVM)
        else:
            logging.info (oError)
            return jsonify(oError), HTTPStatus.NOT_FOUND

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

        #Add the Session object into args list, next create new tuple from the updated list
        #and pass it to the func
        args_repr.append(oSession)
        new_args_repr=[a for a in args_repr]

        #Call the general function with the updated arguments list
        value = func(*new_args_repr, **kwargs)

        #Close the machine session
        if oSession is not None:
            # Try close it.
            try:
                if oSession.state == ctx['const'].SessionState_Locked:
                    oSession.unlockMachine()
                    logging.info ('Unlocked the current machine ' + oVM.id)
                    oSession = None
            except:
                logging.info ('Exception trying unlock machine, close session or set session name')
                try:    fIgnore = oSession.state == ctx['const'].SessionState_Unlocked
                except: fIgnore = False
            
                if fIgnore:
                    oSession  = None # Must prevent a retry during GC.
                else:
                    logging.warning ('ISession::unlockMachine failed on %s' % (oSession))        

        return value

    return wrapper_decorator
