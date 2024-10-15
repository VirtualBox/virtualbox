/* $Id: README.txt 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
Introduction:

    This is a simple tracing driver which hooks into a driver chain and logs all executed callbacks for a particular interface
    with their parameters.
    It uses the RTTraceLog API to write the data into a compact binary log file which can be examined using RTTraceLogTool
    (which is still very basic at this point but will be enhanced when certain features are required).

    Currently implemented interfaces are (implement more on a need basis and add them here):
        - PDMISERIALPORT and PDMISERIALCONNECTOR

Benefits:

    This driver allows to gather more detailed information about the interaction between devices and attached drivers without
    the need to add additional debug/logging code. This is especially useful for debugging user reported issues as it can avoid
    having to send a special testbuil to the user (provided the interfaces are implemented by the trace driver),
    the user just requires instructions on how to enable the tracing for her particular setup.

    Later on this might also come in handy for a driver testbench because it allows recording the interaction from a complicated setup and
    allows replaying it in a more controlled/compact environment for unit testing or fuzzing.

Setup:

    To inject the driver into an existing driver chain the PDM driver transformations feature is used.
    The following example injects the trace driver right before every instance of the "Host Serial" driver:

        VBoxManage setextradata <VM name> "VBoxInternal/PDM/DriverTransformations/SerialTrace/AboveDriver"                         "Host Serial"
        VBoxManage setextradata <VM name> "VBoxInternal/PDM/DriverTransformations/SerialTrace/AttachedDriver/Config/TraceFilePath" "<trace/log/file/path>"
        VBoxManage setextradata <VM name> "VBoxInternal/PDM/DriverTransformations/SerialTrace/AttachedDriver/Driver"               "IfTrace"/>



