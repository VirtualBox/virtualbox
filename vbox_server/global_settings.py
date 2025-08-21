"""VBox REST API

Copyright (c) 2025 Oracle and/or its affiliates.
Licensed under the Universal Permissive License v 1.0 as shown at https://oss.oracle.com/licenses/upl

SPDX-License-Identifier: UPL-1.0
"""

from vboxapi import VirtualBoxManager

count = 0
oVBoxMgr = VirtualBoxManager(None, None)
# oVBox = oVBoxMgr.getVirtualBox()
# Get all constants through the Python manager code
# vboxConstants = oVBoxMgr.constants
ctx = {
    'global':       oVBoxMgr,
    'vb':           oVBoxMgr.getVirtualBox(),
    'const':        oVBoxMgr.constants,
    'remote':       oVBoxMgr.remote,
    'type':         oVBoxMgr.type,
    'tracker':      dict(),
    'vms':          dict(),
}