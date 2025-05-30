##
#    SPDX-License-Identifier: BSD-2-Clause-Patent
##

#
# Built shell can be useful even when not included here, and is used in CI:
# https://github.com/tianocore/edk2/commit/bc982869dd3e69ffd374fd968d378b5d954f66e8
#
!if $(BUILD_SHELL) == TRUE && $(SECURE_BOOT_ENABLE) == FALSE

!if $(TOOL_CHAIN_TAG) != "XCODE5"
!if $(NETWORK_ENABLE) == TRUE
INF  ShellPkg/DynamicCommand/TftpDynamicCommand/TftpDynamicCommand.inf
INF  ShellPkg/DynamicCommand/HttpDynamicCommand/HttpDynamicCommand.inf
!endif
INF  ShellPkg/DynamicCommand/VariablePolicyDynamicCommand/VariablePolicyDynamicCommand.inf
INF  OvmfPkg/LinuxInitrdDynamicShellCommand/LinuxInitrdDynamicShellCommand.inf
!endif

INF  ShellPkg/Application/Shell/Shell.inf
!endif
