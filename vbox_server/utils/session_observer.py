"""VBox REST API

Copyright (c) 2025 Oracle and/or its affiliates.
Licensed under the Universal Permissive License v 1.0 as shown at https://oss.oracle.com/licenses/upl

SPDX-License-Identifier: UPL-1.0
"""

import logging
from time import sleep
from threading import Thread, Semaphore
from vbox_server.global_settings import *
from vbox_server.utils.vbox_utils import vbox_utils_unlockSession as tryUnlockSession
from vbox_server.utils.vbox_utils import vbox_utils_find_machine as findMachine

FORMAT = "%(asctime)s, %(levelname)-8s | %(filename)-23s:%(lineno)-4s | %(threadName)15s: %(message)s"
logger = logging.getLogger("Session")

class SessionObserver(Thread):
    def __init__(self):
        self.bStop = False

        logger.setLevel(logging.INFO)
        formatter = logging.Formatter(
            fmt=FORMAT,
            datefmt="%Y-%m-%d:%H:%M:%S")
        fh = logging.FileHandler(f"{self.__class__.__name__}.log")
        fh.setFormatter(formatter)
        logger.addHandler(fh)

        logger.info(f"self.bStop is {self.bStop}")
        super(SessionObserver, self).__init__()

    def run(self):
        while self.bStop == False:
            try:
                obsoleteSessions  = list()
                for progressId, oSession in ctx['tracker'].items():
                    oVBoxProgress = ctx['vb'].findProgressById(progressId)
                    if oVBoxProgress is not None:
                        logger.info ("Progress with Id %s was found" % progressId)
                        if oVBoxProgress.completed or oVBoxProgress.canceled:
                            logger.info ("  state: finished")
                            logger.info (f"  description: {oVBoxProgress.description}")

                            fTryUnlock = False
                            fExclusiveLock = False
                            for k, v in ctx['vms'].items():
                                if progressId in v:
                                    machineId = k
                                    logger.info ('Found machine Id ' + machineId)
                                    oVM, oError = findMachine(machineId)
                                    if oVM is None:
                                        logger.info ('Couldn\'t find machine with Id ' + machineId)
                                        break
                                    
                                    try:
                                        if oSession.state == ctx['const'].SessionState_Unlocked:
                                            logger.info ('Nobody locks the machine. Try to get an exclusive write lock for ' + oVM.id)
                                            # Nobody locks the machine. Try to get an exclusive write lock
                                            # Not sure about getting LockType_Write. It seems that VirtualBox stores the Session lock
                                            oVM.lockMachine(oSession, ctx['const'].LockType_Shared)
                                            fExclusiveLock = True
                                            logger.info (" AFTER LockType_Write Session state: %s" % (ctx['global'].getEnumValueName('SessionState', oSession.state),))
                                        else:
                                            logger.info ('The machine ' + oVM.id + ' is locked (Maybe it\'s our own lock)')
                                            # Somebody has already hold the lock. Maybe it's our own lock
                                            # If it's our lock we can unlock machine
                                            fTryUnlock = True
                                    except Exception as e:
                                        logger.info(f"SessionObserver exception: {e}")

                                    break

                            try:
                                if fExclusiveLock or fTryUnlock:
                                    # todo: save settings before trying to unlock the session?
                                    # oSession.machine.saveSettings()
                                    res = tryUnlockSession(oSession)
                                    if res:
                                        obsoleteSessions.append(progressId)
                                        del ctx['vms'][machineId]
                            except Exception as e:
                                logger.info(f"SessionObserver exception: {e}")

                for k in obsoleteSessions:
                    oObsoleteSession = ctx['tracker'].pop(k)
                    logger.info ('Obsolete session ' + str(oObsoleteSession))
                    oObsoleteSession = None

            except Exception as e:
                logger.info(f"SessionObserver exception: {e}")

            sleep(1)
        logger.info("SessionObserver: done")

    def myStop(self):
        self.bStop = True
        logger.info(f"self.bStop is {self.bStop}")
