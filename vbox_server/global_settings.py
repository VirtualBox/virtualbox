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
}

