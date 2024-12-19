/* $Id: nt.h 107437 2024-12-19 14:03:57Z knut.osmundsen@oracle.com $ */
/** @file
 * IPRT - Header for code using the Native NT API.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef IPRT_INCLUDED_nt_nt_h
#define IPRT_INCLUDED_nt_nt_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/** @def IPRT_NT_MAP_TO_ZW
 * Map Nt calls to Zw calls.  In ring-0 the Zw calls let you pass kernel memory
 * to the APIs (takes care of the previous context checks).
 */
#ifdef DOXYGEN_RUNNING
# define IPRT_NT_MAP_TO_ZW
#endif

#ifdef IPRT_NT_MAP_TO_ZW
# define NtQueryDirectoryFile           ZwQueryDirectoryFile
# define NtQueryInformationFile         ZwQueryInformationFile
# define NtQueryInformationProcess      ZwQueryInformationProcess
# define NtQueryInformationThread       ZwQueryInformationThread
# define NtQueryFullAttributesFile      ZwQueryFullAttributesFile
# define NtQuerySystemInformation       ZwQuerySystemInformation
# define NtQuerySecurityObject          ZwQuerySecurityObject
# define NtSetInformationFile           ZwSetInformationFile
# define NtClose                        ZwClose
# define NtCreateFile                   ZwCreateFile
# define NtReadFile                     ZwReadFile
# define NtWriteFile                    ZwWriteFile
# define NtFlushBuffersFile             ZwFlushBuffersFile
/** @todo this is very incomplete! */
#endif

#include <ntstatus.h>

/*
 * Hacks common to both base header sets.
 */
#define RtlFreeUnicodeString       WrongLinkage_RtlFreeUnicodeString
#define NtQueryObject              Incomplete_NtQueryObject
#define ZwQueryObject              Incomplete_ZwQueryObject
#define NtSetInformationObject     Incomplete_NtSetInformationObject
#define _OBJECT_INFORMATION_CLASS  Incomplete_OBJECT_INFORMATION_CLASS
#define OBJECT_INFORMATION_CLASS   Incomplete_OBJECT_INFORMATION_CLASS
#define ObjectBasicInformation     Incomplete_ObjectBasicInformation
#define ObjectTypeInformation      Incomplete_ObjectTypeInformation
#define _PEB                       Incomplete__PEB
#define PEB                        Incomplete_PEB
#define PPEB                       Incomplete_PPEB
#define _TEB                       Incomplete__TEB
#define TEB                        Incomplete_TEB
#define PTEB                       Incomplete_PTEB
#define _PEB_LDR_DATA              Incomplete__PEB_LDR_DATA
#define PEB_LDR_DATA               Incomplete_PEB_LDR_DATA
#define PPEB_LDR_DATA              Incomplete_PPEB_LDR_DATA
#define _KUSER_SHARED_DATA         Incomplete__KUSER_SHARED_DATA
#define KUSER_SHARED_DATA          Incomplete_KUSER_SHARED_DATA
#define PKUSER_SHARED_DATA         Incomplete_PKUSER_SHARED_DATA



#ifdef IPRT_NT_USE_WINTERNL
/*
 * Use Winternl.h.
 */
# define _FILE_INFORMATION_CLASS                IncompleteWinternl_FILE_INFORMATION_CLASS
# define FILE_INFORMATION_CLASS                 IncompleteWinternl_FILE_INFORMATION_CLASS
# define FileDirectoryInformation               IncompleteWinternl_FileDirectoryInformation

# define NtQueryInformationProcess              IncompleteWinternl_NtQueryInformationProcess
# define NtSetInformationProcess                IncompleteWinternl_NtSetInformationProcess
# define PROCESSINFOCLASS                       IncompleteWinternl_PROCESSINFOCLASS
# define _PROCESSINFOCLASS                      IncompleteWinternl_PROCESSINFOCLASS
# define PROCESS_BASIC_INFORMATION              IncompleteWinternl_PROCESS_BASIC_INFORMATION
# define PPROCESS_BASIC_INFORMATION             IncompleteWinternl_PPROCESS_BASIC_INFORMATION
# define _PROCESS_BASIC_INFORMATION             IncompleteWinternl_PROCESS_BASIC_INFORMATION
# define ProcessBasicInformation                IncompleteWinternl_ProcessBasicInformation
# define ProcessDebugPort                       IncompleteWinternl_ProcessDebugPort
# define ProcessWow64Information                IncompleteWinternl_ProcessWow64Information
# define ProcessImageFileName                   IncompleteWinternl_ProcessImageFileName
# define ProcessBreakOnTermination              IncompleteWinternl_ProcessBreakOnTermination

# define RTL_USER_PROCESS_PARAMETERS            IncompleteWinternl_RTL_USER_PROCESS_PARAMETERS
# define PRTL_USER_PROCESS_PARAMETERS           IncompleteWinternl_PRTL_USER_PROCESS_PARAMETERS
# define _RTL_USER_PROCESS_PARAMETERS           IncompleteWinternl__RTL_USER_PROCESS_PARAMETERS

# define NtQueryInformationThread               IncompleteWinternl_NtQueryInformationThread
# define NtSetInformationThread                 IncompleteWinternl_NtSetInformationThread
# define THREADINFOCLASS                        IncompleteWinternl_THREADINFOCLASS
# define _THREADINFOCLASS                       IncompleteWinternl_THREADINFOCLASS
# define ThreadIsIoPending                      IncompleteWinternl_ThreadIsIoPending

# define NtQuerySystemInformation               IncompleteWinternl_NtQuerySystemInformation
# define NtSetSystemInformation                 IncompleteWinternl_NtSetSystemInformation
# define NtQueryTimerResolution                 AddedRecentlyUseOwnPrototype_NtQueryTimerResolution
# define SYSTEM_INFORMATION_CLASS               IncompleteWinternl_SYSTEM_INFORMATION_CLASS
# define _SYSTEM_INFORMATION_CLASS              IncompleteWinternl_SYSTEM_INFORMATION_CLASS
# define SystemBasicInformation                 IncompleteWinternl_SystemBasicInformation
# define SystemPerformanceInformation           IncompleteWinternl_SystemPerformanceInformation
# define SystemTimeOfDayInformation             IncompleteWinternl_SystemTimeOfDayInformation
# define SystemProcessInformation               IncompleteWinternl_SystemProcessInformation
# define SystemProcessorPerformanceInformation  IncompleteWinternl_SystemProcessorPerformanceInformation
# define SystemInterruptInformation             IncompleteWinternl_SystemInterruptInformation
# define SystemExceptionInformation             IncompleteWinternl_SystemExceptionInformation
# define SystemRegistryQuotaInformation         IncompleteWinternl_SystemRegistryQuotaInformation
# define SystemLookasideInformation             IncompleteWinternl_SystemLookasideInformation
# define SystemPolicyInformation                IncompleteWinternl_SystemPolicyInformation


# pragma warning(push)
# pragma warning(disable: 4668)
# define WIN32_NO_STATUS
# include <windef.h>
# include <winnt.h>
# include <winternl.h>
# undef WIN32_NO_STATUS
# include <ntstatus.h>
# pragma warning(pop)

# ifndef OBJ_DONT_REPARSE
#  define RTNT_NEED_CLIENT_ID
# endif

# undef _FILE_INFORMATION_CLASS
# undef FILE_INFORMATION_CLASS
# undef FileDirectoryInformation

# undef NtQueryInformationProcess
# undef NtSetInformationProcess
# undef PROCESSINFOCLASS
# undef _PROCESSINFOCLASS
# undef PROCESS_BASIC_INFORMATION
# undef PPROCESS_BASIC_INFORMATION
# undef _PROCESS_BASIC_INFORMATION
# undef ProcessBasicInformation
# undef ProcessDebugPort
# undef ProcessWow64Information
# undef ProcessImageFileName
# undef ProcessBreakOnTermination

# undef RTL_USER_PROCESS_PARAMETERS
# undef PRTL_USER_PROCESS_PARAMETERS
# undef _RTL_USER_PROCESS_PARAMETERS

# undef NtQueryInformationThread
# undef NtSetInformationThread
# undef THREADINFOCLASS
# undef _THREADINFOCLASS
# undef ThreadIsIoPending

# undef NtQuerySystemInformation
# undef NtSetSystemInformation
# undef NtQueryTimerResolution
# undef SYSTEM_INFORMATION_CLASS
# undef _SYSTEM_INFORMATION_CLASS
# undef SystemBasicInformation
# undef SystemPerformanceInformation
# undef SystemTimeOfDayInformation
# undef SystemProcessInformation
# undef SystemProcessorPerformanceInformation
# undef SystemInterruptInformation
# undef SystemExceptionInformation
# undef SystemRegistryQuotaInformation
# undef SystemLookasideInformation
# undef SystemPolicyInformation

# define RTNT_NEED_NT_GET_PRODUCT_TYPE

#else
/*
 * Use ntifs.h and wdm.h.
 */
# if _MSC_VER >= 1200 /* Fix/workaround for KeInitializeSpinLock visibility issue on AMD64. */
#  define FORCEINLINE static __forceinline
# else
#  define FORCEINLINE static __inline
# endif

# define _FSINFOCLASS                   OutdatedWdm_FSINFOCLASS
# define FS_INFORMATION_CLASS           OutdatedWdm_FS_INFORMATION_CLASS
# define PFS_INFORMATION_CLASS          OutdatedWdm_PFS_INFORMATION_CLASS
# define FileFsVolumeInformation        OutdatedWdm_FileFsVolumeInformation
# define FileFsLabelInformation         OutdatedWdm_FileFsLabelInformation
# define FileFsSizeInformation          OutdatedWdm_FileFsSizeInformation
# define FileFsDeviceInformation        OutdatedWdm_FileFsDeviceInformation
# define FileFsAttributeInformation     OutdatedWdm_FileFsAttributeInformation
# define FileFsControlInformation       OutdatedWdm_FileFsControlInformation
# define FileFsFullSizeInformation      OutdatedWdm_FileFsFullSizeInformation
# define FileFsObjectIdInformation      OutdatedWdm_FileFsObjectIdInformation
# define FileFsDriverPathInformation    OutdatedWdm_FileFsDriverPathInformation
# define FileFsVolumeFlagsInformation   OutdatedWdm_FileFsVolumeFlagsInformation
# define FileFsSectorSizeInformation    OutdatedWdm_FileFsSectorSizeInformation
# define FileFsDataCopyInformation      OutdatedWdm_FileFsDataCopyInformation
# define FileFsMetadataSizeInformation  OutdatedWdm_FileFsMetadataSizeInformation
# define FileFsFullSizeInformationEx    OutdatedWdm_FileFsFullSizeInformationEx
# define FileFsMaximumInformation       OutdatedWdm_FileFsMaximumInformation
# define NtQueryVolumeInformationFile   OutdatedWdm_NtQueryVolumeInformationFile
# define NtSetVolumeInformationFile     OutdatedWdm_NtSetVolumeInformationFile
# define _MEMORY_INFORMATION_CLASS      OutdatedWdm__MEMORY_INFORMATION_CLASS
# define MEMORY_INFORMATION_CLASS       OutdatedWdm_MEMORY_INFORMATION_CLASS
# define MemoryBasicInformation         OutdatedWdm_MemoryBasicInformation
# define NtQueryVirtualMemory           OutdatedWdm_NtQueryVirtualMemory

# pragma warning(push)
# ifdef RT_ARCH_X86
#  define _InterlockedAddLargeStatistic  _InterlockedAddLargeStatistic_StupidDDKVsCompilerCrap
#  pragma warning(disable: 4163)
# endif
# pragma warning(disable: 4668)
# pragma warning(disable: 4255) /* warning C4255: 'ObGetFilterVersion' : no function prototype given: converting '()' to '(void)' */
# if _MSC_VER >= 1800 /*RT_MSC_VER_VC120*/
#  pragma warning(disable:4005) /* sdk/v7.1/include/sal_supp.h(57) : warning C4005: '__useHeader' : macro redefinition */
#  pragma warning(disable:4471) /* wdm.h(11057) : warning C4471: '_POOL_TYPE' : a forward declaration of an unscoped enumeration must have an underlying type (int assumed) */
# endif
# if _MSC_VER >= 1900 /*RT_MSC_VER_VC140*/
#  ifdef __cplusplus
#   pragma warning(disable:5039) /* warning C5039: 'KeInitializeDpc': pointer or reference to potentially throwing function passed to 'extern "C"' function under -EHc. Undefined behavior may occur if this function throws an exception. */
#  endif
# endif

# include <ntifs.h>
# include <wdm.h>

# ifdef RT_ARCH_X86
#  undef _InterlockedAddLargeStatistic
# endif
# pragma warning(pop)

# undef _FSINFOCLASS
# undef FS_INFORMATION_CLASS
# undef PFS_INFORMATION_CLASS
# undef FileFsVolumeInformation
# undef FileFsLabelInformation
# undef FileFsSizeInformation
# undef FileFsDeviceInformation
# undef FileFsAttributeInformation
# undef FileFsControlInformation
# undef FileFsFullSizeInformation
# undef FileFsObjectIdInformation
# undef FileFsDriverPathInformation
# undef FileFsVolumeFlagsInformation
# undef FileFsSectorSizeInformation
# undef FileFsDataCopyInformation
# undef FileFsMetadataSizeInformation
# undef FileFsFullSizeInformationEx
# undef FileFsMaximumInformation
# undef NtQueryVolumeInformationFile
# undef NtSetVolumeInformationFile
# undef _MEMORY_INFORMATION_CLASS
# undef MEMORY_INFORMATION_CLASS
# undef MemoryBasicInformation
# undef NtQueryVirtualMemory

# define IPRT_NT_NEED_API_GROUP_NTIFS
# ifndef NTDDI_WIN10_RS1
#  define RTNT_NEED_NT_GET_PRODUCT_TYPE
# elif NTDDI_VERSION < NTDDI_WIN10_RS1
#  define RTNT_NEED_NT_GET_PRODUCT_TYPE
# endif

#endif

#undef RtlFreeUnicodeString
#undef NtQueryObject
#undef ZwQueryObject
#undef NtSetInformationObject
#undef _OBJECT_INFORMATION_CLASS
#undef OBJECT_INFORMATION_CLASS
#undef ObjectBasicInformation
#undef ObjectTypeInformation
#undef _PEB
#undef PEB
#undef PPEB
#undef _TEB
#undef TEB
#undef PTEB
#undef _PEB_LDR_DATA
#undef PEB_LDR_DATA
#undef PPEB_LDR_DATA
#undef _KUSER_SHARED_DATA
#undef KUSER_SHARED_DATA
#undef PKUSER_SHARED_DATA


#include <iprt/types.h>
#include <iprt/assert.h>


/** @name Useful macros
 * @{ */
/** Indicates that we're targeting native NT in the current source. */
#define RTNT_USE_NATIVE_NT              1
/** Initializes a IO_STATUS_BLOCK. */
#define RTNT_IO_STATUS_BLOCK_INITIALIZER  { STATUS_FAILED_DRIVER_ENTRY, ~(uintptr_t)42 }
/** Reinitializes a IO_STATUS_BLOCK. */
#define RTNT_IO_STATUS_BLOCK_REINIT(a_pIos) \
    do { (a_pIos)->Status = STATUS_FAILED_DRIVER_ENTRY; (a_pIos)->Information = ~(uintptr_t)42; } while (0)
/** Similar to INVALID_HANDLE_VALUE in the Windows environment. */
#define RTNT_INVALID_HANDLE_VALUE         ( (HANDLE)~(uintptr_t)0 )
/** Constant UNICODE_STRING initializer. */
#define RTNT_CONSTANT_UNISTR(a_String)   { sizeof(a_String) - sizeof(WCHAR), sizeof(a_String), (WCHAR *)a_String }
/** Null UNICODE_STRING initializer. */
#define RTNT_NULL_UNISTR()               { 0, 0, NULL }

/** Declaration wrapper for NT apis.
 * Adds nothrow.  Don't use with callbacks. */
#define RT_DECL_NTAPI(type) DECL_NOTHROW(NTSYSAPI type NTAPI)
/** @} */


/** @name IPRT helper functions for NT
 * @{ */
RT_C_DECLS_BEGIN

RTDECL(int) RTNtPathOpen(const char *pszPath, ACCESS_MASK fDesiredAccess, ULONG fFileAttribs, ULONG fShareAccess,
                          ULONG fCreateDisposition, ULONG fCreateOptions, ULONG fObjAttribs,
                          PHANDLE phHandle, PULONG_PTR puDisposition);
RTDECL(int) RTNtPathOpenDir(const char *pszPath, ACCESS_MASK fDesiredAccess, ULONG fShareAccess, ULONG fCreateOptions,
                            ULONG fObjAttribs, PHANDLE phHandle, bool *pfObjDir);
RTDECL(int) RTNtPathOpenDirEx(HANDLE hRootDir, struct _UNICODE_STRING *pNtName, ACCESS_MASK fDesiredAccess,
                              ULONG fShareAccess, ULONG fCreateOptions, ULONG fObjAttribs, PHANDLE phHandle, bool *pfObjDir);
RTDECL(int) RTNtPathClose(HANDLE hHandle);

/**
 * Converts a windows-style path to NT format and encoding.
 *
 * @returns IPRT status code.
 * @param   pNtName             Where to return the NT name.  Free using
 *                              RTNtPathFree.
 * @param   phRootDir           Where to return the root handle, if applicable.
 * @param   pszPath             The UTF-8 path.
 */
RTDECL(int) RTNtPathFromWinUtf8(struct _UNICODE_STRING *pNtName, PHANDLE phRootDir, const char *pszPath);

/**
 * Converts a UTF-16 windows-style path to NT format.
 *
 * @returns IPRT status code.
 * @param   pNtName             Where to return the NT name.  Free using
 *                              RTNtPathFree.
 * @param   phRootDir           Where to return the root handle, if applicable.
 * @param   pwszPath            The UTF-16 windows-style path.
 * @param   cwcPath             The max length of the windows-style path in
 *                              RTUTF16 units.  Use RTSTR_MAX if unknown and @a
 *                              pwszPath is correctly terminated.
 */
RTDECL(int) RTNtPathFromWinUtf16Ex(struct _UNICODE_STRING *pNtName, HANDLE *phRootDir, PCRTUTF16 pwszPath, size_t cwcPath);

/**
 * How to handle ascent ('..' relative to a root handle).
 */
typedef enum RTNTPATHRELATIVEASCENT
{
    kRTNtPathRelativeAscent_Invalid = 0,
    kRTNtPathRelativeAscent_Allow,
    kRTNtPathRelativeAscent_Fail,
    kRTNtPathRelativeAscent_Ignore,
    kRTNtPathRelativeAscent_End,
    kRTNtPathRelativeAscent_32BitHack = 0x7fffffff
} RTNTPATHRELATIVEASCENT;

/**
 * Converts a relative windows-style path to relative NT format and encoding.
 *
 * @returns IPRT status code.
 * @param   pNtName             Where to return the NT name.  Free using
 *                              rtTNtPathToNative with phRootDir set to NULL.
 * @param   phRootDir           On input, the handle to the directory the path
 *                              is relative to.  On output, the handle to
 *                              specify as root directory in the object
 *                              attributes when accessing the path.  If
 *                              enmAscent is kRTNtPathRelativeAscent_Allow, it
 *                              may have been set to NULL.
 * @param   pszPath             The relative UTF-8 path.
 * @param   enmAscent           How to handle ascent.
 * @param   fMustReturnAbsolute Must convert to an absolute path.  This
 *                              is necessary if the root dir is a NT directory
 *                              object (e.g. /Devices) since they cannot parse
 *                              relative paths it seems.
 */
RTDECL(int) RTNtPathRelativeFromUtf8(struct _UNICODE_STRING *pNtName, PHANDLE phRootDir, const char *pszPath,
                                     RTNTPATHRELATIVEASCENT enmAscent, bool fMustReturnAbsolute);

/**
 * Ensures that the NT string has sufficient storage to hold @a cwcMin RTUTF16
 * chars plus a terminator.
 *
 * The NT string must have been returned by RTNtPathFromWinUtf8 or
 * RTNtPathFromWinUtf16Ex.
 *
 * @returns IPRT status code.
 * @param   pNtName             The NT path string.
 * @param   cwcMin              The minimum number of RTUTF16 chars. Max 32767.
 * @sa      RTNtPathFree
 */
RTDECL(int) RTNtPathEnsureSpace(struct _UNICODE_STRING *pNtName, size_t cwcMin);

/**
 * Gets the NT path to the object represented by the given handle.
 *
 * @returns IPRT status code.
 * @param   pNtName             Where to return the NT path.  Free using
 *                              RTNtPathFree.
 * @param   hHandle             The handle.
 * @param   cwcExtra            How much extra space is needed.
 */
RTDECL(int) RTNtPathFromHandle(struct _UNICODE_STRING *pNtName, HANDLE hHandle, size_t cwcExtra);

/**
 * Frees the native path and root handle.
 *
 * @param   pNtName             The NT path after a successful rtNtPathToNative
 *                              call or RTNtPathRelativeFromUtf8.
 * @param   phRootDir           The root handle variable from rtNtPathToNative,
 */
RTDECL(void) RTNtPathFree(struct _UNICODE_STRING *pNtName, HANDLE *phRootDir);


/**
 * Checks whether the path could be containing alternative 8.3 names generated
 * by NTFS, FAT, or other similar file systems.
 *
 * @returns Pointer to the first component that might be an 8.3 name, NULL if
 *          not 8.3 path.
 * @param   pwszPath        The path to check.
 *
 * @remarks This is making bad ASSUMPTION wrt to the naming scheme of 8.3 names,
 *          however, non-tilde 8.3 aliases are probably rare enough to not be
 *          worth all the extra code necessary to open each path component and
 *          check if we've got the short name or not.
 */
RTDECL(PRTUTF16) RTNtPathFindPossible8dot3Name(PCRTUTF16 pwszPath);

/**
 * Fixes up a path possibly containing one or more alternative 8-dot-3 style
 * components.
 *
 * The path is fixed up in place.  Errors are ignored.
 *
 * @returns VINF_SUCCESS if it all went smoothly, informational status codes
 *          indicating the nature of last problem we ran into.
 *
 * @param   pUniStr     The path to fix up. MaximumLength is the max buffer
 *                      length.
 * @param   fPathOnly   Whether to only process the path and leave the filename
 *                      as passed in.
 */
RTDECL(int) RTNtPathExpand8dot3Path(struct _UNICODE_STRING *pUniStr, bool fPathOnly);

/**
 * Wrapper around RTNtPathExpand8dot3Path that allocates a buffer instead of
 * working on the input buffer.
 *
 * @returns IPRT status code, see RTNtPathExpand8dot3Path().
 * @param   pUniStrSrc  The path to fix up. MaximumLength is the max buffer
 *                      length.
 * @param   fPathOnly   Whether to only process the path and leave the filename
 *                      as passed in.
 * @param   pUniStrDst  Output string.  On success, the caller must use
 *                      RTUtf16Free to free what the Buffer member points to.
 *                      This is all zeros and NULL on failure.
 */
RTDECL(int) RTNtPathExpand8dot3PathA(struct _UNICODE_STRING const *pUniStrSrc, bool fPathOnly, struct _UNICODE_STRING *pUniStrDst);


RT_C_DECLS_END
/** @} */


/** @name NT API delcarations.
 * @{ */
RT_C_DECLS_BEGIN

/** @name Process access rights missing in ntddk headers
 * @{ */
#ifndef  PROCESS_TERMINATE
# define PROCESS_TERMINATE                  UINT32_C(0x00000001)
#endif
#ifndef  PROCESS_CREATE_THREAD
# define PROCESS_CREATE_THREAD              UINT32_C(0x00000002)
#endif
#ifndef  PROCESS_SET_SESSIONID
# define PROCESS_SET_SESSIONID              UINT32_C(0x00000004)
#endif
#ifndef  PROCESS_VM_OPERATION
# define PROCESS_VM_OPERATION               UINT32_C(0x00000008)
#endif
#ifndef  PROCESS_VM_READ
# define PROCESS_VM_READ                    UINT32_C(0x00000010)
#endif
#ifndef  PROCESS_VM_WRITE
# define PROCESS_VM_WRITE                   UINT32_C(0x00000020)
#endif
#ifndef  PROCESS_DUP_HANDLE
# define PROCESS_DUP_HANDLE                 UINT32_C(0x00000040)
#endif
#ifndef  PROCESS_CREATE_PROCESS
# define PROCESS_CREATE_PROCESS             UINT32_C(0x00000080)
#endif
#ifndef  PROCESS_SET_QUOTA
# define PROCESS_SET_QUOTA                  UINT32_C(0x00000100)
#endif
#ifndef  PROCESS_SET_INFORMATION
# define PROCESS_SET_INFORMATION            UINT32_C(0x00000200)
#endif
#ifndef  PROCESS_QUERY_INFORMATION
# define PROCESS_QUERY_INFORMATION          UINT32_C(0x00000400)
#endif
#ifndef  PROCESS_SUSPEND_RESUME
# define PROCESS_SUSPEND_RESUME             UINT32_C(0x00000800)
#endif
#ifndef  PROCESS_QUERY_LIMITED_INFORMATION
# define PROCESS_QUERY_LIMITED_INFORMATION  UINT32_C(0x00001000)
#endif
#ifndef  PROCESS_SET_LIMITED_INFORMATION
# define PROCESS_SET_LIMITED_INFORMATION    UINT32_C(0x00002000)
#endif
#define PROCESS_UNKNOWN_4000                UINT32_C(0x00004000)
#define PROCESS_UNKNOWN_6000                UINT32_C(0x00008000)
#ifndef PROCESS_ALL_ACCESS
# define PROCESS_ALL_ACCESS                 ( STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | UINT32_C(0x0000ffff) )
#endif
/** @} */

/** @name Thread access rights missing in ntddk headers
 * @{ */
#ifndef THREAD_QUERY_INFORMATION
# define THREAD_QUERY_INFORMATION           UINT32_C(0x00000040)
#endif
#ifndef THREAD_SET_THREAD_TOKEN
# define THREAD_SET_THREAD_TOKEN            UINT32_C(0x00000080)
#endif
#ifndef THREAD_IMPERSONATE
# define THREAD_IMPERSONATE                 UINT32_C(0x00000100)
#endif
#ifndef THREAD_DIRECT_IMPERSONATION
# define THREAD_DIRECT_IMPERSONATION        UINT32_C(0x00000200)
#endif
#ifndef THREAD_RESUME
# define THREAD_RESUME                      UINT32_C(0x00001000)
#endif
#define THREAD_UNKNOWN_2000                 UINT32_C(0x00002000)
#define THREAD_UNKNOWN_4000                 UINT32_C(0x00004000)
#define THREAD_UNKNOWN_8000                 UINT32_C(0x00008000)
/** @} */

/** @name Special handle values.
 * @{ */
#ifndef NtCurrentProcess
# define NtCurrentProcess()                 ( (HANDLE)-(intptr_t)1 )
#endif
#ifndef NtCurrentThread
# define NtCurrentThread()                  ( (HANDLE)-(intptr_t)2 )
#endif
#ifndef ZwCurrentProcess
# define ZwCurrentProcess()                 NtCurrentProcess()
#endif
#ifndef ZwCurrentThread
# define ZwCurrentThread()                  NtCurrentThread()
#endif
/** @} */


/** @name Directory object access rights.
 * @{ */
#ifndef DIRECTORY_QUERY
# define DIRECTORY_QUERY                    UINT32_C(0x00000001)
#endif
#ifndef DIRECTORY_TRAVERSE
# define DIRECTORY_TRAVERSE                 UINT32_C(0x00000002)
#endif
#ifndef DIRECTORY_CREATE_OBJECT
# define DIRECTORY_CREATE_OBJECT            UINT32_C(0x00000004)
#endif
#ifndef DIRECTORY_CREATE_SUBDIRECTORY
# define DIRECTORY_CREATE_SUBDIRECTORY      UINT32_C(0x00000008)
#endif
#ifndef DIRECTORY_ALL_ACCESS
# define DIRECTORY_ALL_ACCESS               ( STANDARD_RIGHTS_REQUIRED | UINT32_C(0x0000000f) )
#endif
/** @} */



#ifdef RTNT_NEED_CLIENT_ID
typedef struct _CLIENT_ID
{
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
} CLIENT_ID;
#endif
#ifdef IPRT_NT_USE_WINTERNL
typedef CLIENT_ID *PCLIENT_ID;
#endif

/** Extended affinity type, introduced in Windows 7 (?). */
typedef struct _KAFFINITY_EX
{
    /** Count of valid bitmap entries. */
    uint16_t                Count;
    /** Count of allocated bitmap entries. */
    uint16_t                Size;
    /** Reserved / aligmment padding. */
    uint32_t                Reserved;
    /** Bitmap where one bit corresponds to a CPU.
     * @note Started at 20 entries.  W10 20H2 increased it to 32.  Must be
     *       probed by passing a big buffer to KeInitializeAffinityEx and check
     *       the Size afterwards. */
    uintptr_t               Bitmap[RT_FLEXIBLE_ARRAY_IN_NESTED_UNION];
} KAFFINITY_EX;
typedef KAFFINITY_EX *PKAFFINITY_EX;
typedef KAFFINITY_EX const *PCKAFFINITY_EX;

/** @name User Shared Data
 * @{ */

#ifdef IPRT_NT_USE_WINTERNL
typedef struct _KSYSTEM_TIME
{
    ULONG                   LowPart;
    LONG                    High1Time;
    LONG                    High2Time;
} KSYSTEM_TIME;
typedef KSYSTEM_TIME *PKSYSTEM_TIME;

typedef enum _NT_PRODUCT_TYPE
{
    NtProductWinNt = 1,
    NtProductLanManNt,
    NtProductServer
} NT_PRODUCT_TYPE;

#define PROCESSOR_FEATURE_MAX       64

typedef enum _ALTERNATIVE_ARCHITECTURE_TYPE
{
    StandardDesign = 0,
    NEC98x86,
    EndAlternatives
} ALTERNATIVE_ARCHITECTURE_TYPE;

# if 0
typedef struct _XSTATE_FEATURE
{
    ULONG                   Offset;
    ULONG                   Size;
} XSTATE_FEATURE;
typedef XSTATE_FEATURE *PXSTATE_FEATURE;

#define MAXIMUM_XSTATE_FEATURES     64

typedef struct _XSTATE_CONFIGURATION
{
    ULONG64                 EnabledFeatures;
    ULONG                   Size;
    ULONG                   OptimizedSave  : 1;
    XSTATE_FEATURE          Features[MAXIMUM_XSTATE_FEATURES];
} XSTATE_CONFIGURATION;
typedef XSTATE_CONFIGURATION *PXSTATE_CONFIGURATION;
# endif
#endif /* IPRT_NT_USE_WINTERNL */

typedef struct _KUSER_SHARED_DATA
{
    ULONG                   TickCountLowDeprecated;                     /**< 0x000 */
    ULONG                   TickCountMultiplier;                        /**< 0x004 */
    KSYSTEM_TIME volatile   InterruptTime;                              /**< 0x008 */
    KSYSTEM_TIME volatile   SystemTime;                                 /**< 0x014 */
    KSYSTEM_TIME volatile   TimeZoneBias;                               /**< 0x020 */
    USHORT                  ImageNumberLow;                             /**< 0x02c */
    USHORT                  ImageNumberHigh;                            /**< 0x02e */
    WCHAR                   NtSystemRoot[260];                          /**< 0x030 - Seems to be last member in NT 3.51. */
    ULONG                   MaxStackTraceDepth;                         /**< 0x238 */
    ULONG                   CryptoExponent;                             /**< 0x23c */
    ULONG                   TimeZoneId;                                 /**< 0x240 */
    ULONG                   LargePageMinimum;                           /**< 0x244 */
    ULONG                   AitSamplingValue;                           /**< 0x248 */
    ULONG                   AppCompatFlag;                              /**< 0x24c */
    ULONGLONG               RNGSeedVersion;                             /**< 0x250 */
    ULONG                   GlobalValidationRunlevel;                   /**< 0x258 */
    LONG volatile           TimeZoneBiasStamp;                          /**< 0x25c*/
    ULONG                   Reserved2;                                  /**< 0x260 */
    NT_PRODUCT_TYPE         NtProductType;                              /**< 0x264 */
    BOOLEAN                 ProductTypeIsValid;                         /**< 0x268 */
    BOOLEAN                 Reserved0[1];                               /**< 0x269 */
    USHORT                  NativeProcessorArchitecture;                /**< 0x26a */
    ULONG                   NtMajorVersion;                             /**< 0x26c */
    ULONG                   NtMinorVersion;                             /**< 0x270 */
    BOOLEAN                 ProcessorFeatures[PROCESSOR_FEATURE_MAX];   /**< 0x274 */
    ULONG                   Reserved1;                                  /**< 0x2b4 */
    ULONG                   Reserved3;                                  /**< 0x2b8 */
    ULONG volatile          TimeSlip;                                   /**< 0x2bc */
    ALTERNATIVE_ARCHITECTURE_TYPE AlternativeArchitecture;              /**< 0x2c0 */
    ULONG                   AltArchitecturePad[1];                      /**< 0x2c4 */
    LARGE_INTEGER           SystemExpirationDate;                       /**< 0x2c8 */
    ULONG                   SuiteMask;                                  /**< 0x2d0 */
    BOOLEAN                 KdDebuggerEnabled;                          /**< 0x2d4 */
    union                                                               /**< 0x2d5 */
    {
        UCHAR               MitigationPolicies;                         /**< 0x2d5 */
        struct
        {
            UCHAR           NXSupportPolicy  : 2;
            UCHAR           SEHValidationPolicy  : 2;
            UCHAR           CurDirDevicesSkippedForDlls  : 2;
            UCHAR           Reserved  : 2;
        };
    };
    UCHAR                   Reserved6[2];                               /**< 0x2d6 */
    ULONG volatile          ActiveConsoleId;                            /**< 0x2d8 */
    ULONG volatile          DismountCount;                              /**< 0x2dc */
    ULONG                   ComPlusPackage;                             /**< 0x2e0 */
    ULONG                   LastSystemRITEventTickCount;                /**< 0x2e4 */
    ULONG                   NumberOfPhysicalPages;                      /**< 0x2e8 */
    BOOLEAN                 SafeBootMode;                               /**< 0x2ec */
    UCHAR                   Reserved12[3];                              /**< 0x2ed */
    union                                                               /**< 0x2f0 */
    {
        ULONG               SharedDataFlags;                            /**< 0x2f0 */
        struct
        {
            ULONG           DbgErrorPortPresent  : 1;
            ULONG           DbgElevationEnabled  : 1;
            ULONG           DbgVirtEnabled  : 1;
            ULONG           DbgInstallerDetectEnabled  : 1;
            ULONG           DbgLkgEnabled  : 1;
            ULONG           DbgDynProcessorEnabled  : 1;
            ULONG           DbgConsoleBrokerEnabled  : 1;
            ULONG           DbgSecureBootEnabled  : 1;
            ULONG           SpareBits  : 24;
        };
    };
    ULONG                   DataFlagsPad[1];                            /**< 0x2f4 */
    ULONGLONG               TestRetInstruction;                         /**< 0x2f8 */
    LONGLONG                QpcFrequency;                               /**< 0x300 */
    ULONGLONG               SystemCallPad[3];                           /**< 0x308 */
    union                                                               /**< 0x320 */
    {
        ULONG64 volatile    TickCountQuad;                              /**< 0x320 */
        KSYSTEM_TIME volatile TickCount;                                /**< 0x320 */
        struct                                                          /**< 0x320 */
        {
            ULONG           ReservedTickCountOverlay[3];                /**< 0x320 */
            ULONG           TickCountPad[1];                            /**< 0x32c */
        };
    };
    ULONG                   Cookie;                                     /**< 0x330 */
    ULONG                   CookiePad[1];                               /**< 0x334 */
    LONGLONG                ConsoleSessionForegroundProcessId;          /**< 0x338 */
    ULONGLONG               TimeUpdateLock;                             /**< 0x340 */
    ULONGLONG               BaselineSystemTimeQpc;                      /**< 0x348 */
    ULONGLONG               BaselineInterruptTimeQpc;                   /**< 0x350 */
    ULONGLONG               QpcSystemTimeIncrement;                     /**< 0x358 */
    ULONGLONG               QpcInterruptTimeIncrement;                  /**< 0x360 */
    ULONG                   QpcSystemTimeIncrement32;                   /**< 0x368 */
    ULONG                   QpcInterruptTimeIncrement32;                /**< 0x36c */
    UCHAR                   QpcSystemTimeIncrementShift;                /**< 0x370 */
    UCHAR                   QpcInterruptTimeIncrementShift;             /**< 0x371 */
    UCHAR                   Reserved8[14];                              /**< 0x372 */
    USHORT                  UserModeGlobalLogger[16];                   /**< 0x380 */
    ULONG                   ImageFileExecutionOptions;                  /**< 0x3a0 */
    ULONG                   LangGenerationCount;                        /**< 0x3a4 */
    ULONGLONG               Reserved4;                                  /**< 0x3a8 */
    ULONGLONG volatile      InterruptTimeBias;                          /**< 0x3b0 - What QueryUnbiasedInterruptTimePrecise
                                                                         * subtracts from interrupt time. */
    ULONGLONG volatile      QpcBias;                                    /**< 0x3b8 */
    ULONG volatile          ActiveProcessorCount;                       /**< 0x3c0 */
    UCHAR volatile          ActiveGroupCount;                           /**< 0x3c4 */
    UCHAR                   Reserved9;                                  /**< 0x3c5 */
    union                                                               /**< 0x3c6 */
    {
        USHORT              QpcData;                                    /**< 0x3c6 */
        struct                                                          /**< 0x3c6 */
        {
            BOOLEAN volatile QpcBypassEnabled;                          /**< 0x3c6 */
            UCHAR           QpcShift;                                   /**< 0x3c7 */
        };
    };
    LARGE_INTEGER           TimeZoneBiasEffectiveStart;                 /**< 0x3c8 */
    LARGE_INTEGER           TimeZoneBiasEffectiveEnd;                   /**< 0x3d0 */
    XSTATE_CONFIGURATION    XState;                                     /**< 0x3d8 */
} KUSER_SHARED_DATA;
typedef KUSER_SHARED_DATA *PKUSER_SHARED_DATA;
AssertCompileMemberOffset(KUSER_SHARED_DATA, InterruptTime,             0x008);
AssertCompileMemberOffset(KUSER_SHARED_DATA, SystemTime,                0x014);
AssertCompileMemberOffset(KUSER_SHARED_DATA, NtSystemRoot,              0x030);
AssertCompileMemberOffset(KUSER_SHARED_DATA, LargePageMinimum,          0x244);
AssertCompileMemberOffset(KUSER_SHARED_DATA, Reserved1,                 0x2b4);
AssertCompileMemberOffset(KUSER_SHARED_DATA, TestRetInstruction,        0x2f8);
AssertCompileMemberOffset(KUSER_SHARED_DATA, Cookie,                    0x330);
AssertCompileMemberOffset(KUSER_SHARED_DATA, ImageFileExecutionOptions, 0x3a0);
AssertCompileMemberOffset(KUSER_SHARED_DATA, XState,                    0x3d8);
/** @def MM_SHARED_USER_DATA_VA
 * Read only userland mapping of KUSER_SHARED_DATA. */
#ifndef MM_SHARED_USER_DATA_VA
# if ARCH_BITS == 32
#  define MM_SHARED_USER_DATA_VA        UINT32_C(0x7ffe0000)
# elif ARCH_BITS == 64
#  define MM_SHARED_USER_DATA_VA        UINT64_C(0x7ffe0000)
# else
#  error "Unsupported/undefined ARCH_BITS value."
# endif
#endif
/** @def KI_USER_SHARED_DATA
 * Read write kernel mapping of KUSER_SHARED_DATA. */
#ifndef KI_USER_SHARED_DATA
# ifdef RT_ARCH_X86
#  define KI_USER_SHARED_DATA           UINT32_C(0xffdf0000)
# elif defined(RT_ARCH_AMD64)
#  define KI_USER_SHARED_DATA           UINT64_C(0xfffff78000000000)
# else
#  error "PORT ME - KI_USER_SHARED_DATA"
# endif
#endif
/** @} */


/** @name Process And Thread Environment Blocks
 * @{ */

typedef struct _PEB_LDR_DATA
{
    uint32_t Length;
    BOOLEAN Initialized;
    BOOLEAN Padding[3];
    HANDLE SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
    /* End NT4 */
    LIST_ENTRY *EntryInProgress;
    BOOLEAN ShutdownInProgress;
    HANDLE ShutdownThreadId;
} PEB_LDR_DATA;
typedef PEB_LDR_DATA *PPEB_LDR_DATA;

typedef struct _PEB_COMMON
{
    BOOLEAN InheritedAddressSpace;                                          /**< 0x000 / 0x000 */
    BOOLEAN ReadImageFileExecOptions;                                       /**< 0x001 / 0x001 */
    BOOLEAN BeingDebugged;                                                  /**< 0x002 / 0x002 */
    union
    {
        uint8_t BitField;                                                   /**< 0x003 / 0x003 */
        struct
        {
            uint8_t ImageUsesLargePages : 1;                                /**< 0x003 / 0x003 : Pos 0, 1 Bit */
        } Common;
        struct
        {
            uint8_t ImageUsesLargePages : 1;                                /**< 0x003 / 0x003 : Pos 0, 1 Bit */
            uint8_t IsProtectedProcess : 1;                                 /**< 0x003 / 0x003 : Pos 1, 1 Bit */
            uint8_t IsImageDynamicallyRelocated : 1;                        /**< 0x003 / 0x003 : Pos 2, 1 Bit - Differs from W80 */
            uint8_t SkipPatchingUser32Forwarders : 1;                       /**< 0x003 / 0x003 : Pos 3, 1 Bit - Differs from W80 */
            uint8_t IsPackagedProcess : 1;                                  /**< 0x003 / 0x003 : Pos 4, 1 Bit - Differs from W80 */
            uint8_t IsAppContainer : 1;                                     /**< 0x003 / 0x003 : Pos 5, 1 Bit - Differs from W80 */
            uint8_t IsProtectedProcessLight : 1;                            /**< 0x003 / 0x003 : Pos 6, 1 Bit - Differs from W80 */
            uint8_t SpareBits : 1;                                          /**< 0x003 / 0x003 : Pos 7, 1 Bit */
        } W81;
        struct
        {
            uint8_t ImageUsesLargePages : 1;                                /**< 0x003 / 0x003 : Pos 0, 1 Bit */
            uint8_t IsProtectedProcess : 1;                                 /**< 0x003 / 0x003 : Pos 1, 1 Bit */
            uint8_t IsLegacyProcess : 1;                                    /**< 0x003 / 0x003 : Pos 2, 1 Bit - Differs from W81 */
            uint8_t IsImageDynamicallyRelocated : 1;                        /**< 0x003 / 0x003 : Pos 3, 1 Bit - Differs from W81 */
            uint8_t SkipPatchingUser32Forwarders : 1;                       /**< 0x003 / 0x003 : Pos 4, 1 Bit - Differs from W81 */
            uint8_t IsPackagedProcess : 1;                                  /**< 0x003 / 0x003 : Pos 5, 1 Bit - Differs from W81 */
            uint8_t IsAppContainer : 1;                                     /**< 0x003 / 0x003 : Pos 6, 1 Bit - Differs from W81 */
            uint8_t SpareBits : 1;                                          /**< 0x003 / 0x003 : Pos 7, 1 Bit */
        } W80;
        struct
        {
            uint8_t ImageUsesLargePages : 1;                                /**< 0x003 / 0x003 : Pos 0, 1 Bit */
            uint8_t IsProtectedProcess : 1;                                 /**< 0x003 / 0x003 : Pos 1, 1 Bit */
            uint8_t IsLegacyProcess : 1;                                    /**< 0x003 / 0x003 : Pos 2, 1 Bit - Differs from W81, same as W80 & W6. */
            uint8_t IsImageDynamicallyRelocated : 1;                        /**< 0x003 / 0x003 : Pos 3, 1 Bit - Differs from W81, same as W80 & W6. */
            uint8_t SkipPatchingUser32Forwarders : 1;                       /**< 0x003 / 0x003 : Pos 4, 1 Bit - Added in W7; Differs from W81, same as W80. */
            uint8_t SpareBits : 3;                                          /**< 0x003 / 0x003 : Pos 5, 3 Bit - Differs from W81 & W80, more spare bits. */
        } W7;
        struct
        {
            uint8_t ImageUsesLargePages : 1;                                /**< 0x003 / 0x003 : Pos 0, 1 Bit */
            uint8_t IsProtectedProcess : 1;                                 /**< 0x003 / 0x003 : Pos 1, 1 Bit */
            uint8_t IsLegacyProcess : 1;                                    /**< 0x003 / 0x003 : Pos 2, 1 Bit - Differs from W81, same as W80 & W7. */
            uint8_t IsImageDynamicallyRelocated : 1;                        /**< 0x003 / 0x003 : Pos 3, 1 Bit - Differs from W81, same as W80 & W7. */
            uint8_t SpareBits : 4;                                          /**< 0x003 / 0x003 : Pos 4, 4 Bit - Differs from W81, W80, & W7, more spare bits. */
        } W6;
        struct
        {
            uint8_t ImageUsesLargePages : 1;                                /**< 0x003 / 0x003 : Pos 0, 1 Bit */
            uint8_t SpareBits : 7;                                          /**< 0x003 / 0x003 : Pos 1, 7 Bit - Differs from W81, W80, & W7, more spare bits. */
        } W52;
        struct
        {
            BOOLEAN SpareBool;
        } W51;
    } Diff0;
#if ARCH_BITS == 64
    uint32_t Padding0;                                                      /**< 0x004 / NA */
#endif
    HANDLE Mutant;                                                          /**< 0x008 / 0x004 */
    PVOID ImageBaseAddress;                                                 /**< 0x010 / 0x008 */
    PPEB_LDR_DATA Ldr;                                                      /**< 0x018 / 0x00c */
    struct _RTL_USER_PROCESS_PARAMETERS *ProcessParameters;                 /**< 0x020 / 0x010 */
    PVOID SubSystemData;                                                    /**< 0x028 / 0x014 */
    HANDLE ProcessHeap;                                                     /**< 0x030 / 0x018 */
    struct _RTL_CRITICAL_SECTION *FastPebLock;                              /**< 0x038 / 0x01c */
    union
    {
        struct
        {
            PVOID AtlThunkSListPtr;                                         /**< 0x040 / 0x020 */
            PVOID IFEOKey;                                                  /**< 0x048 / 0x024 */
            union
            {
                ULONG CrossProcessFlags;                                    /**< 0x050 / 0x028 */
                struct
                {
                    uint32_t ProcessInJob : 1;                              /**< 0x050 / 0x028: Pos 0, 1 Bit */
                    uint32_t ProcessInitializing : 1;                       /**< 0x050 / 0x028: Pos 1, 1 Bit */
                    uint32_t ProcessUsingVEH : 1;                           /**< 0x050 / 0x028: Pos 2, 1 Bit */
                    uint32_t ProcessUsingVCH : 1;                           /**< 0x050 / 0x028: Pos 3, 1 Bit */
                    uint32_t ProcessUsingFTH : 1;                           /**< 0x050 / 0x028: Pos 4, 1 Bit */
                    uint32_t ReservedBits0 : 1;                             /**< 0x050 / 0x028: Pos 5, 27 Bits */
                } W7, W8, W80, W81;
                struct
                {
                    uint32_t ProcessInJob : 1;                              /**< 0x050 / 0x028: Pos 0, 1 Bit */
                    uint32_t ProcessInitializing : 1;                       /**< 0x050 / 0x028: Pos 1, 1 Bit */
                    uint32_t ReservedBits0 : 30;                            /**< 0x050 / 0x028: Pos 2, 30 Bits */
                } W6;
            };
#if ARCH_BITS == 64
            uint32_t Padding1;                                              /**< 0x054 / */
#endif
        } W6, W7, W8, W80, W81;
        struct
        {
            PVOID AtlThunkSListPtr;                                         /**< 0x040 / 0x020 */
            PVOID SparePtr2;                                                /**< 0x048 / 0x024 */
            uint32_t EnvironmentUpdateCount;                                /**< 0x050 / 0x028 */
#if ARCH_BITS == 64
            uint32_t Padding1;                                              /**< 0x054 / */
#endif
        } W52;
        struct
        {
            PVOID FastPebLockRoutine;                                       /**< NA / 0x020 */
            PVOID FastPebUnlockRoutine;                                     /**< NA / 0x024 */
            uint32_t EnvironmentUpdateCount;                                /**< NA / 0x028 */
        } W51;
    } Diff1;
    union
    {
        PVOID KernelCallbackTable;                                          /**< 0x058 / 0x02c */
        PVOID UserSharedInfoPtr;                                            /**< 0x058 / 0x02c - Alternative use in W6.*/
    };
    uint32_t SystemReserved;                                                /**< 0x060 / 0x030 */
    union
    {
        struct
        {
            uint32_t AtlThunkSListPtr32;                                    /**< 0x064 / 0x034 */
        } W7, W8, W80, W81;
        struct
        {
            uint32_t SpareUlong;                                            /**< 0x064 / 0x034 */
        } W52, W6;
        struct
        {
            uint32_t ExecuteOptions : 2;                                    /**< NA / 0x034: Pos 0, 2 Bits */
            uint32_t SpareBits : 30;                                        /**< NA / 0x034: Pos 2, 30 Bits */
        } W51;
    } Diff2;
    union
    {
        struct
        {
            PVOID ApiSetMap;                                                /**< 0x068 / 0x038 */
        } W7, W8, W80, W81;
        struct
        {
            struct _PEB_FREE_BLOCK *FreeList;                               /**< 0x068 / 0x038 */
        } W52, W6;
        struct
        {
            struct _PEB_FREE_BLOCK *FreeList;                               /**< NA / 0x038 */
        } W51;
    } Diff3;
    uint32_t TlsExpansionCounter;                                           /**< 0x070 / 0x03c */
#if ARCH_BITS == 64
    uint32_t Padding2;                                                      /**< 0x074 / NA */
#endif
    struct _RTL_BITMAP *TlsBitmap;                                          /**< 0x078 / 0x040 */
    uint32_t TlsBitmapBits[2];                                              /**< 0x080 / 0x044 */
    PVOID ReadOnlySharedMemoryBase;                                         /**< 0x088 / 0x04c */
    union
    {
        struct
        {
            PVOID SparePvoid0;                                              /**< 0x090 / 0x050 - HotpatchInformation before W81. */
        } W81;
        struct
        {
            PVOID HotpatchInformation;                                      /**< 0x090 / 0x050 - Retired in W81. */
        } W6, W7, W80;
        struct
        {
            PVOID ReadOnlySharedMemoryHeap;
        } W52;
    } Diff4;
    PVOID *ReadOnlyStaticServerData;                                        /**< 0x098 / 0x054 */
    PVOID AnsiCodePageData;                                                 /**< 0x0a0 / 0x058 */
    PVOID OemCodePageData;                                                  /**< 0x0a8 / 0x05c */
    PVOID UnicodeCaseTableData;                                             /**< 0x0b0 / 0x060 */
    uint32_t NumberOfProcessors;                                            /**< 0x0b8 / 0x064 */
    uint32_t NtGlobalFlag;                                                  /**< 0x0bc / 0x068 */
#if ARCH_BITS == 32
    uint32_t Padding2b;
#endif
    LARGE_INTEGER CriticalSectionTimeout;                                   /**< 0x0c0 / 0x070 */
    SIZE_T HeapSegmentReserve;                                              /**< 0x0c8 / 0x078 */
    SIZE_T HeapSegmentCommit;                                               /**< 0x0d0 / 0x07c */
    SIZE_T HeapDeCommitTotalFreeThreshold;                                  /**< 0x0d8 / 0x080 */
    SIZE_T HeapDeCommitFreeBlockThreshold;                                  /**< 0x0e0 / 0x084 */
    uint32_t NumberOfHeaps;                                                 /**< 0x0e8 / 0x088 */
    uint32_t MaximumNumberOfHeaps;                                          /**< 0x0ec / 0x08c */
    PVOID *ProcessHeaps;                                                    /**< 0x0f0 / 0x090 - Last NT 3.51 member. */
    PVOID GdiSharedHandleTable;                                             /**< 0x0f8 / 0x094  */
    PVOID ProcessStarterHelper;                                             /**< 0x100 / 0x098 */
    uint32_t GdiDCAttributeList;                                            /**< 0x108 / 0x09c */
#if ARCH_BITS == 64
    uint32_t Padding3;                                                      /**< 0x10c / NA */
#endif
    struct _RTL_CRITICAL_SECTION *LoaderLock;                               /**< 0x110 / 0x0a0 */
    uint32_t OSMajorVersion;                                                /**< 0x118 / 0x0a4 */
    uint32_t OSMinorVersion;                                                /**< 0x11c / 0x0a8 */
    uint16_t OSBuildNumber;                                                 /**< 0x120 / 0x0ac */
    uint16_t OSCSDVersion;                                                  /**< 0x122 / 0x0ae */
    uint32_t OSPlatformId;                                                  /**< 0x124 / 0x0b0 */
    uint32_t ImageSubsystem;                                                /**< 0x128 / 0x0b4 */
    uint32_t ImageSubsystemMajorVersion;                                    /**< 0x12c / 0x0b8 */
    uint32_t ImageSubsystemMinorVersion;                                    /**< 0x130 / 0x0bc */
#if ARCH_BITS == 64
    uint32_t Padding4;                                                      /**< 0x134 / NA */
#endif
    union
    {
        struct
        {
            SIZE_T ActiveProcessAffinityMask;                               /**< 0x138 / 0x0c0 */
        } W7, W8, W80, W81;
        struct
        {
            SIZE_T ImageProcessAffinityMask;                                /**< 0x138 / 0x0c0 */
        } W52, W6;
    } Diff5;
    uint32_t GdiHandleBuffer[ARCH_BITS == 64 ? 60 : 34];                    /**< 0x140 / 0x0c4 */
    PVOID PostProcessInitRoutine;                                           /**< 0x230 / 0x14c */
    PVOID TlsExpansionBitmap;                                               /**< 0x238 / 0x150 */
    uint32_t TlsExpansionBitmapBits[32];                                    /**< 0x240 / 0x154 */
    uint32_t SessionId;                                                     /**< 0x2c0 / 0x1d4 */
#if ARCH_BITS == 64
    uint32_t Padding5;                                                      /**< 0x2c4 / NA */
#endif
    ULARGE_INTEGER AppCompatFlags;                                          /**< 0x2c8 / 0x1d8 */
    ULARGE_INTEGER AppCompatFlagsUser;                                      /**< 0x2d0 / 0x1e0 */
    PVOID pShimData;                                                        /**< 0x2d8 / 0x1e8 */
    PVOID AppCompatInfo;                                                    /**< 0x2e0 / 0x1ec */
    UNICODE_STRING CSDVersion;                                              /**< 0x2e8 / 0x1f0 */
    struct _ACTIVATION_CONTEXT_DATA *ActivationContextData;                 /**< 0x2f8 / 0x1f8 */
    struct _ASSEMBLY_STORAGE_MAP *ProcessAssemblyStorageMap;                /**< 0x300 / 0x1fc */
    struct _ACTIVATION_CONTEXT_DATA *SystemDefaultActivationContextData;    /**< 0x308 / 0x200 */
    struct _ASSEMBLY_STORAGE_MAP *SystemAssemblyStorageMap;                 /**< 0x310 / 0x204 */
    SIZE_T MinimumStackCommit;                                              /**< 0x318 / 0x208 */
    /* End of PEB in W52 (Windows XP (RTM))! */
    struct _FLS_CALLBACK_INFO *FlsCallback;                                 /**< 0x320 / 0x20c */
    LIST_ENTRY FlsListHead;                                                 /**< 0x328 / 0x210 */
    PVOID FlsBitmap;                                                        /**< 0x338 / 0x218 */
    uint32_t FlsBitmapBits[4];                                              /**< 0x340 / 0x21c */
    uint32_t FlsHighIndex;                                                  /**< 0x350 / 0x22c */
    /* End of PEB in W52 (Windows Server 2003)! */
    PVOID WerRegistrationData;                                              /**< 0x358 / 0x230 */
    PVOID WerShipAssertPtr;                                                 /**< 0x360 / 0x234 */
    /* End of PEB in W6 (windows Vista)! */
    union
    {
        struct
        {
            PVOID pUnused;                                                  /**< 0x368 / 0x238 - Was pContextData in W7. */
        } W8, W80, W81;
        struct
        {
            PVOID pContextData;                                             /**< 0x368 / 0x238 - Retired in W80. */
        } W7;
    } Diff6;
    PVOID pImageHeaderHash;                                                 /**< 0x370 / 0x23c */
    union
    {
        uint32_t TracingFlags;                                              /**< 0x378 / 0x240 */
        struct
        {
            uint32_t HeapTracingEnabled : 1;                                /**< 0x378 / 0x240 : Pos 0, 1 Bit */
            uint32_t CritSecTracingEnabled : 1;                             /**< 0x378 / 0x240 : Pos 1, 1 Bit */
            uint32_t LibLoaderTracingEnabled : 1;                           /**< 0x378 / 0x240 : Pos 2, 1 Bit */
            uint32_t SpareTracingBits : 29;                                 /**< 0x378 / 0x240 : Pos 3, 29 Bits */
        } W8, W80, W81;
        struct
        {
            uint32_t HeapTracingEnabled : 1;                                /**< 0x378 / 0x240 : Pos 0, 1 Bit */
            uint32_t CritSecTracingEnabled : 1;                             /**< 0x378 / 0x240 : Pos 1, 1 Bit */
            uint32_t SpareTracingBits : 30;                                 /**< 0x378 / 0x240 : Pos 3, 30 Bits - One bit more than W80 */
        } W7;
    } Diff7;
#if ARCH_BITS == 64
    uint32_t Padding6;                                                      /**< 0x37c / NA */
#endif
    uint64_t CsrServerReadOnlySharedMemoryBase;                             /**< 0x380 / 0x248 */
    /* End of PEB in W8, W81. */
    uintptr_t TppWorkerpListLock;                                           /**< 0x388 / 0x250 */
    LIST_ENTRY TppWorkerpList;                                              /**< 0x390 / 0x254 */
    PVOID WaitOnAddressHashTable[128];                                      /**< 0x3a0 / 0x25c */
#if ARCH_BITS == 32
    uint32_t ExplicitPadding7;                                              /**< NA NA / 0x45c */
#endif
} PEB_COMMON;
typedef PEB_COMMON *PPEB_COMMON;

AssertCompileMemberOffset(PEB_COMMON, ProcessHeap,    ARCH_BITS == 64 ?  0x30 :  0x18);
AssertCompileMemberOffset(PEB_COMMON, SystemReserved, ARCH_BITS == 64 ?  0x60 :  0x30);
AssertCompileMemberOffset(PEB_COMMON, TlsExpansionCounter,   ARCH_BITS == 64 ?  0x70 :  0x3c);
AssertCompileMemberOffset(PEB_COMMON, NtGlobalFlag,   ARCH_BITS == 64 ?  0xbc :  0x68);
AssertCompileMemberOffset(PEB_COMMON, LoaderLock,     ARCH_BITS == 64 ? 0x110 :  0xa0);
AssertCompileMemberOffset(PEB_COMMON, Diff5.W52.ImageProcessAffinityMask, ARCH_BITS == 64 ? 0x138 :  0xc0);
AssertCompileMemberOffset(PEB_COMMON, PostProcessInitRoutine,    ARCH_BITS == 64 ? 0x230 : 0x14c);
AssertCompileMemberOffset(PEB_COMMON, AppCompatFlags, ARCH_BITS == 64 ? 0x2c8 : 0x1d8);
AssertCompileSize(PEB_COMMON, ARCH_BITS == 64 ? 0x7a0 : 0x460);

/** The size of the windows 10 (build 14393) PEB structure. */
#define PEB_SIZE_W10    sizeof(PEB_COMMON)
/** The size of the windows 8.1 PEB structure.  */
#define PEB_SIZE_W81    RT_UOFFSETOF(PEB_COMMON, TppWorkerpListLock)
/** The size of the windows 8.0 PEB structure.  */
#define PEB_SIZE_W80    RT_UOFFSETOF(PEB_COMMON, TppWorkerpListLock)
/** The size of the windows 7 PEB structure.  */
#define PEB_SIZE_W7     RT_UOFFSETOF(PEB_COMMON, CsrServerReadOnlySharedMemoryBase)
/** The size of the windows vista PEB structure.  */
#define PEB_SIZE_W6     RT_UOFFSETOF(PEB_COMMON, Diff3)
/** The size of the windows server 2003 PEB structure.  */
#define PEB_SIZE_W52    RT_UOFFSETOF(PEB_COMMON, WerRegistrationData)
/** The size of the windows XP PEB structure.  */
#define PEB_SIZE_W51    RT_UOFFSETOF(PEB_COMMON, FlsCallback)

#if 0
typedef struct _NT_TIB
{
    struct _EXCEPTION_REGISTRATION_RECORD *ExceptionList;
    PVOID StackBase;
    PVOID StackLimit;
    PVOID SubSystemTib;
    union
    {
        PVOID FiberData;
        ULONG Version;
    };
    PVOID ArbitraryUserPointer;
    struct _NT_TIB *Self;
} NT_TIB;
typedef NT_TIB *PNT_TIB;
#endif

typedef struct _ACTIVATION_CONTEXT_STACK
{
   uint32_t Flags;
   uint32_t NextCookieSequenceNumber;
   PVOID ActiveFrame;
   LIST_ENTRY FrameListCache;
} ACTIVATION_CONTEXT_STACK;

/* Common TEB. */
typedef struct _TEB_COMMON
{
    NT_TIB NtTib;                                                           /**< 0x000 / 0x000 */
    PVOID EnvironmentPointer;                                               /**< 0x038 / 0x01c */
    CLIENT_ID ClientId;                                                     /**< 0x040 / 0x020 */
    PVOID ActiveRpcHandle;                                                  /**< 0x050 / 0x028 */
    PVOID ThreadLocalStoragePointer;                                        /**< 0x058 / 0x02c */
    PPEB_COMMON ProcessEnvironmentBlock;                                    /**< 0x060 / 0x030 */
    uint32_t LastErrorValue;                                                /**< 0x068 / 0x034 */
    uint32_t CountOfOwnedCriticalSections;                                  /**< 0x06c / 0x038 */
    PVOID CsrClientThread;                                                  /**< 0x070 / 0x03c */
    PVOID Win32ThreadInfo;                                                  /**< 0x078 / 0x040 */
    uint32_t User32Reserved[26];                                            /**< 0x080 / 0x044 */
    uint32_t UserReserved[5];                                               /**< 0x0e8 / 0x0ac */
    PVOID WOW32Reserved;                                                    /**< 0x100 / 0x0c0 */
    uint32_t CurrentLocale;                                                 /**< 0x108 / 0x0c4 */
    uint32_t FpSoftwareStatusRegister;                                      /**< 0x10c / 0x0c8 */
    PVOID SystemReserved1[54];                                              /**< 0x110 / 0x0cc */
    uint32_t ExceptionCode;                                                 /**< 0x2c0 / 0x1a4 */
#if ARCH_BITS == 64
    uint32_t Padding0;                                                      /**< 0x2c4 / NA */
#endif
    union
    {
        struct
        {
            struct _ACTIVATION_CONTEXT_STACK *ActivationContextStackPointer;/**< 0x2c8 / 0x1a8 */
            uint8_t SpareBytes[ARCH_BITS == 64 ? 24 : 36];                  /**< 0x2d0 / 0x1ac */
        } W52, W6, W7, W8, W80, W81;
#if ARCH_BITS == 32
        struct
        {
            ACTIVATION_CONTEXT_STACK ActivationContextStack;                /**< NA / 0x1a8 */
            uint8_t SpareBytes[20];                                         /**< NA / 0x1bc */
        } W51;
#endif
    } Diff0;
    union
    {
        struct
        {
            uint32_t TxFsContext;                                           /**< 0x2e8 / 0x1d0 */
        } W6, W7, W8, W80, W81;
        struct
        {
            uint32_t SpareBytesContinues;                                   /**< 0x2e8 / 0x1d0 */
        } W52;
    } Diff1;
#if ARCH_BITS == 64
    uint32_t Padding1;                                                      /**< 0x2ec / NA */
#endif
    /*_GDI_TEB_BATCH*/ uint8_t GdiTebBatch[ARCH_BITS == 64 ? 0x4e8 :0x4e0]; /**< 0x2f0 / 0x1d4 */
    CLIENT_ID RealClientId;                                                 /**< 0x7d8 / 0x6b4 */
    HANDLE GdiCachedProcessHandle;                                          /**< 0x7e8 / 0x6bc */
    uint32_t GdiClientPID;                                                  /**< 0x7f0 / 0x6c0 */
    uint32_t GdiClientTID;                                                  /**< 0x7f4 / 0x6c4 */
    PVOID GdiThreadLocalInfo;                                               /**< 0x7f8 / 0x6c8 */
    SIZE_T Win32ClientInfo[62];                                             /**< 0x800 / 0x6cc */
    PVOID glDispatchTable[233];                                             /**< 0x9f0 / 0x7c4 */
    SIZE_T glReserved1[29];                                                 /**< 0x1138 / 0xb68 */
    PVOID glReserved2;                                                      /**< 0x1220 / 0xbdc */
    PVOID glSectionInfo;                                                    /**< 0x1228 / 0xbe0 */
    PVOID glSection;                                                        /**< 0x1230 / 0xbe4 */
    PVOID glTable;                                                          /**< 0x1238 / 0xbe8 */
    PVOID glCurrentRC;                                                      /**< 0x1240 / 0xbec */
    PVOID glContext;                                                        /**< 0x1248 / 0xbf0 */
    NTSTATUS LastStatusValue;                                               /**< 0x1250 / 0xbf4 */
#if ARCH_BITS == 64
    uint32_t Padding2;                                                      /**< 0x1254 / NA */
#endif
    UNICODE_STRING StaticUnicodeString;                                     /**< 0x1258 / 0xbf8 */
    WCHAR StaticUnicodeBuffer[261];                                         /**< 0x1268 / 0xc00 */
#if ARCH_BITS == 64
    WCHAR Padding3[3];                                                      /**< 0x1472 / NA */
#endif
    PVOID DeallocationStack;                                                /**< 0x1478 / 0xe0c */
    PVOID TlsSlots[64];                                                     /**< 0x1480 / 0xe10 */
    LIST_ENTRY TlsLinks;                                                    /**< 0x1680 / 0xf10 */
    PVOID Vdm;                                                              /**< 0x1690 / 0xf18 */
    PVOID ReservedForNtRpc;                                                 /**< 0x1698 / 0xf1c */
    PVOID DbgSsReserved[2];                                                 /**< 0x16a0 / 0xf20 */
    uint32_t HardErrorMode;                                                 /**< 0x16b0 / 0xf28 - Called HardErrorsAreDisabled in W51. */
#if ARCH_BITS == 64
    uint32_t Padding4;                                                      /**< 0x16b4 / NA */
#endif
    PVOID Instrumentation[ARCH_BITS == 64 ? 11 : 9];                        /**< 0x16b8 / 0xf2c */
    union
    {
        struct
        {
            GUID ActivityId;                                                /**< 0x1710 / 0xf50 */
            PVOID SubProcessTag;                                            /**< 0x1720 / 0xf60 */
        } W6, W7, W8, W80, W81;
        struct
        {
            PVOID InstrumentationContinues[ARCH_BITS == 64 ? 3 : 5];        /**< 0x1710 / 0xf50 */
        } W52;
    } Diff2;
    union                                                                   /**< 0x1728 / 0xf64 */
    {
        struct
        {
            PVOID PerflibData;                                              /**< 0x1728 / 0xf64 */
        } W8, W80, W81;
        struct
        {
            PVOID EtwLocalData;                                             /**< 0x1728 / 0xf64 */
        } W7, W6;
        struct
        {
            PVOID SubProcessTag;                                            /**< 0x1728 / 0xf64 */
        } W52;
        struct
        {
            PVOID InstrumentationContinues[1];                              /**< 0x1728 / 0xf64 */
        } W51;
    } Diff3;
    union
    {
        struct
        {
            PVOID EtwTraceData;                                             /**< 0x1730 / 0xf68 */
        } W52, W6, W7, W8, W80, W81;
        struct
        {
            PVOID InstrumentationContinues[1];                              /**< 0x1730 / 0xf68 */
        } W51;
    } Diff4;
    PVOID WinSockData;                                                      /**< 0x1738 / 0xf6c */
    uint32_t GdiBatchCount;                                                 /**< 0x1740 / 0xf70 */
    union
    {
        union
        {
            PROCESSOR_NUMBER CurrentIdealProcessor;                         /**< 0x1744 / 0xf74 - W7+ */
            uint32_t IdealProcessorValue;                                   /**< 0x1744 / 0xf74 - W7+ */
            struct
            {
                uint8_t ReservedPad1;                                       /**< 0x1744 / 0xf74 - Called SpareBool0 in W6 */
                uint8_t ReservedPad2;                                       /**< 0x1745 / 0xf75 - Called SpareBool0 in W6 */
                uint8_t ReservedPad3;                                       /**< 0x1746 / 0xf76 - Called SpareBool0 in W6 */
                uint8_t IdealProcessor;                                     /**< 0x1747 / 0xf77 */
            };
        } W6, W7, W8, W80, W81;
        struct
        {
            BOOLEAN InDbgPrint;                                             /**< 0x1744 / 0xf74 */
            BOOLEAN FreeStackOnTermination;                                 /**< 0x1745 / 0xf75 */
            BOOLEAN HasFiberData;                                           /**< 0x1746 / 0xf76 */
            uint8_t IdealProcessor;                                         /**< 0x1747 / 0xf77 */
        } W51, W52;
    } Diff5;
    uint32_t GuaranteedStackBytes;                                          /**< 0x1748 / 0xf78 */
#if ARCH_BITS == 64
    uint32_t Padding5;                                                      /**< 0x174c / NA */
#endif
    PVOID ReservedForPerf;                                                  /**< 0x1750 / 0xf7c */
    PVOID ReservedForOle;                                                   /**< 0x1758 / 0xf80 */
    uint32_t WaitingOnLoaderLock;                                           /**< 0x1760 / 0xf84 */
#if ARCH_BITS == 64
    uint32_t Padding6;                                                      /**< 0x1764 / NA */
#endif
    union                                                                   /**< 0x1770 / 0xf8c */
    {
        struct
        {
            PVOID SavedPriorityState;                                       /**< 0x1768 / 0xf88 */
            SIZE_T ReservedForCodeCoverage;                                 /**< 0x1770 / 0xf8c */
            PVOID ThreadPoolData;                                           /**< 0x1778 / 0xf90 */
        } W8, W80, W81;
        struct
        {
            PVOID SavedPriorityState;                                       /**< 0x1768 / 0xf88 */
            SIZE_T SoftPatchPtr1;                                           /**< 0x1770 / 0xf8c */
            PVOID ThreadPoolData;                                           /**< 0x1778 / 0xf90 */
        } W6, W7;
        struct
        {
            PVOID SparePointer1;                                            /**< 0x1768 / 0xf88 */
            SIZE_T SoftPatchPtr1;                                           /**< 0x1770 / 0xf8c */
            PVOID SoftPatchPtr2;                                            /**< 0x1778 / 0xf90 */
        } W52;
#if ARCH_BITS == 32
        struct _Wx86ThreadState
        {
            PVOID CallBx86Eip;                                            /**< NA / 0xf88 */
            PVOID DeallocationCpu;                                        /**< NA / 0xf8c */
            BOOLEAN UseKnownWx86Dll;                                      /**< NA / 0xf90 */
            int8_t OleStubInvoked;                                        /**< NA / 0xf91 */
        } W51;
#endif
    } Diff6;
    PVOID TlsExpansionSlots;                                                /**< 0x1780 / 0xf94 */
#if ARCH_BITS == 64
    PVOID DallocationBStore;                                                /**< 0x1788 / NA */
    PVOID BStoreLimit;                                                      /**< 0x1790 / NA */
#endif
    union
    {
        struct
        {
            uint32_t MuiGeneration;                                                 /**< 0x1798 / 0xf98 */
        } W7, W8, W80, W81;
        struct
        {
            uint32_t ImpersonationLocale;
        } W6;
    } Diff7;
    uint32_t IsImpersonating;                                               /**< 0x179c / 0xf9c */
    PVOID NlsCache;                                                         /**< 0x17a0 / 0xfa0 */
    PVOID pShimData;                                                        /**< 0x17a8 / 0xfa4 */
    union                                                                   /**< 0x17b0 / 0xfa8 */
    {
        struct
        {
            uint16_t HeapVirtualAffinity;                                   /**< 0x17b0 / 0xfa8 */
            uint16_t LowFragHeapDataSlot;                                   /**< 0x17b2 / 0xfaa */
        } W8, W80, W81;
        struct
        {
            uint32_t HeapVirtualAffinity;                                   /**< 0x17b0 / 0xfa8 */
        } W7;
    } Diff8;
#if ARCH_BITS == 64
    uint32_t Padding7;                                                      /**< 0x17b4 / NA */
#endif
    HANDLE CurrentTransactionHandle;                                        /**< 0x17b8 / 0xfac */
    struct _TEB_ACTIVE_FRAME *ActiveFrame;                                  /**< 0x17c0 / 0xfb0 */
    /* End of TEB in W51 (Windows XP)! */
    PVOID FlsData;                                                          /**< 0x17c8 / 0xfb4 */
    union
    {
        struct
        {
            PVOID PreferredLanguages;                                       /**< 0x17d0 / 0xfb8 */
        } W6, W7, W8, W80, W81;
        struct
        {
            BOOLEAN SafeThunkCall;                                          /**< 0x17d0 / 0xfb8 */
            uint8_t BooleanSpare[3];                                        /**< 0x17d1 / 0xfb9 */
            /* End of TEB in W52 (Windows server 2003)! */
        } W52;
    } Diff9;
    PVOID UserPrefLanguages;                                                /**< 0x17d8 / 0xfbc */
    PVOID MergedPrefLanguages;                                              /**< 0x17e0 / 0xfc0 */
    uint32_t MuiImpersonation;                                              /**< 0x17e8 / 0xfc4 */
    union
    {
        uint16_t CrossTebFlags;                                             /**< 0x17ec / 0xfc8 */
        struct
        {
            uint16_t SpareCrossTebBits : 16;                                /**< 0x17ec / 0xfc8 : Pos 0, 16 Bits */
        };
    };
    union
    {
        uint16_t SameTebFlags;                                              /**< 0x17ee / 0xfca */
        struct
        {
            uint16_t SafeThunkCall : 1;                                     /**< 0x17ee / 0xfca : Pos 0, 1 Bit */
            uint16_t InDebugPrint : 1;                                      /**< 0x17ee / 0xfca : Pos 1, 1 Bit */
            uint16_t HasFiberData : 1;                                      /**< 0x17ee / 0xfca : Pos 2, 1 Bit */
            uint16_t SkipThreadAttach : 1;                                  /**< 0x17ee / 0xfca : Pos 3, 1 Bit */
            uint16_t WerInShipAssertCode : 1;                               /**< 0x17ee / 0xfca : Pos 4, 1 Bit */
            uint16_t RanProcessInit : 1;                                    /**< 0x17ee / 0xfca : Pos 5, 1 Bit */
            uint16_t ClonedThread : 1;                                      /**< 0x17ee / 0xfca : Pos 6, 1 Bit */
            uint16_t SuppressDebugMsg : 1;                                  /**< 0x17ee / 0xfca : Pos 7, 1 Bit */
        } Common;
        struct
        {
            uint16_t SafeThunkCall : 1;                                     /**< 0x17ee / 0xfca : Pos 0, 1 Bit */
            uint16_t InDebugPrint : 1;                                      /**< 0x17ee / 0xfca : Pos 1, 1 Bit */
            uint16_t HasFiberData : 1;                                      /**< 0x17ee / 0xfca : Pos 2, 1 Bit */
            uint16_t SkipThreadAttach : 1;                                  /**< 0x17ee / 0xfca : Pos 3, 1 Bit */
            uint16_t WerInShipAssertCode : 1;                               /**< 0x17ee / 0xfca : Pos 4, 1 Bit */
            uint16_t RanProcessInit : 1;                                    /**< 0x17ee / 0xfca : Pos 5, 1 Bit */
            uint16_t ClonedThread : 1;                                      /**< 0x17ee / 0xfca : Pos 6, 1 Bit */
            uint16_t SuppressDebugMsg : 1;                                  /**< 0x17ee / 0xfca : Pos 7, 1 Bit */
            uint16_t DisableUserStackWalk : 1;                              /**< 0x17ee / 0xfca : Pos 8, 1 Bit */
            uint16_t RtlExceptionAttached : 1;                              /**< 0x17ee / 0xfca : Pos 9, 1 Bit */
            uint16_t InitialThread : 1;                                     /**< 0x17ee / 0xfca : Pos 10, 1 Bit */
            uint16_t SessionAware : 1;                                      /**< 0x17ee / 0xfca : Pos 11, 1 Bit - New Since W7. */
            uint16_t SpareSameTebBits : 4;                                  /**< 0x17ee / 0xfca : Pos 12, 4 Bits */
        } W8, W80, W81;
        struct
        {
            uint16_t SafeThunkCall : 1;                                     /**< 0x17ee / 0xfca : Pos 0, 1 Bit */
            uint16_t InDebugPrint : 1;                                      /**< 0x17ee / 0xfca : Pos 1, 1 Bit */
            uint16_t HasFiberData : 1;                                      /**< 0x17ee / 0xfca : Pos 2, 1 Bit */
            uint16_t SkipThreadAttach : 1;                                  /**< 0x17ee / 0xfca : Pos 3, 1 Bit */
            uint16_t WerInShipAssertCode : 1;                               /**< 0x17ee / 0xfca : Pos 4, 1 Bit */
            uint16_t RanProcessInit : 1;                                    /**< 0x17ee / 0xfca : Pos 5, 1 Bit */
            uint16_t ClonedThread : 1;                                      /**< 0x17ee / 0xfca : Pos 6, 1 Bit */
            uint16_t SuppressDebugMsg : 1;                                  /**< 0x17ee / 0xfca : Pos 7, 1 Bit */
            uint16_t DisableUserStackWalk : 1;                              /**< 0x17ee / 0xfca : Pos 8, 1 Bit */
            uint16_t RtlExceptionAttached : 1;                              /**< 0x17ee / 0xfca : Pos 9, 1 Bit */
            uint16_t InitialThread : 1;                                     /**< 0x17ee / 0xfca : Pos 10, 1 Bit */
            uint16_t SpareSameTebBits : 5;                                  /**< 0x17ee / 0xfca : Pos 12, 4 Bits */
        } W7;
        struct
        {
            uint16_t DbgSafeThunkCall : 1;                                  /**< 0x17ee / 0xfca : Pos 0, 1 Bit */
            uint16_t DbgInDebugPrint : 1;                                   /**< 0x17ee / 0xfca : Pos 1, 1 Bit */
            uint16_t DbgHasFiberData : 1;                                   /**< 0x17ee / 0xfca : Pos 2, 1 Bit */
            uint16_t DbgSkipThreadAttach : 1;                               /**< 0x17ee / 0xfca : Pos 3, 1 Bit */
            uint16_t DbgWerInShipAssertCode : 1;                            /**< 0x17ee / 0xfca : Pos 4, 1 Bit */
            uint16_t DbgRanProcessInit : 1;                                 /**< 0x17ee / 0xfca : Pos 5, 1 Bit */
            uint16_t DbgClonedThread : 1;                                   /**< 0x17ee / 0xfca : Pos 6, 1 Bit */
            uint16_t DbgSuppressDebugMsg : 1;                               /**< 0x17ee / 0xfca : Pos 7, 1 Bit */
            uint16_t SpareSameTebBits : 8;                                  /**< 0x17ee / 0xfca : Pos 8, 8 Bits */
        } W6;
    } Diff10;
    PVOID TxnScopeEnterCallback;                                            /**< 0x17f0 / 0xfcc */
    PVOID TxnScopeExitCallback;                                             /**< 0x17f8 / 0xfd0 */
    PVOID TxnScopeContext;                                                  /**< 0x1800 / 0xfd4 */
    uint32_t LockCount;                                                     /**< 0x1808 / 0xfd8 */
    union
    {
        struct
        {
            uint32_t SpareUlong0;                                           /**< 0x180c / 0xfdc */
        } W7, W8, W80, W81;
        struct
        {
            uint32_t ProcessRundown;
        } W6;
    } Diff11;
    union
    {
        struct
        {
            PVOID ResourceRetValue;                                        /**< 0x1810 / 0xfe0 */
            /* End of TEB in W7 (windows 7)! */
            PVOID ReservedForWdf;                                          /**< 0x1818 / 0xfe4 - New Since W7. */
            /* End of TEB in W8 (windows 8.0 & 8.1)! */
            PVOID ReservedForCrt;                                          /**< 0x1820 / 0xfe8 - New Since W10.  */
            RTUUID EffectiveContainerId;                                   /**< 0x1828 / 0xfec - New Since W10.  */
            /* End of TEB in W10 14393! */
        } W8, W80, W81, W10;
        struct
        {
            PVOID ResourceRetValue;                                        /**< 0x1810 / 0xfe0 */
        } W7;
        struct
        {
            uint64_t LastSwitchTime;                                       /**< 0x1810 / 0xfe0 */
            uint64_t TotalSwitchOutTime;                                   /**< 0x1818 / 0xfe8 */
            LARGE_INTEGER WaitReasonBitMap;                                /**< 0x1820 / 0xff0 */
            /* End of TEB in W6 (windows Vista)! */
        } W6;
    } Diff12;
} TEB_COMMON;
typedef TEB_COMMON *PTEB_COMMON;
AssertCompileMemberOffset(TEB_COMMON, ExceptionCode,        ARCH_BITS == 64 ?  0x2c0 : 0x1a4);
AssertCompileMemberOffset(TEB_COMMON, LastStatusValue,      ARCH_BITS == 64 ? 0x1250 : 0xbf4);
AssertCompileMemberOffset(TEB_COMMON, DeallocationStack,    ARCH_BITS == 64 ? 0x1478 : 0xe0c);
AssertCompileMemberOffset(TEB_COMMON, ReservedForNtRpc,     ARCH_BITS == 64 ? 0x1698 : 0xf1c);
AssertCompileMemberOffset(TEB_COMMON, Instrumentation,      ARCH_BITS == 64 ? 0x16b8 : 0xf2c);
AssertCompileMemberOffset(TEB_COMMON, Diff2,                ARCH_BITS == 64 ? 0x1710 : 0xf50);
AssertCompileMemberOffset(TEB_COMMON, Diff3,                ARCH_BITS == 64 ? 0x1728 : 0xf64);
AssertCompileMemberOffset(TEB_COMMON, Diff4,                ARCH_BITS == 64 ? 0x1730 : 0xf68);
AssertCompileMemberOffset(TEB_COMMON, WinSockData,          ARCH_BITS == 64 ? 0x1738 : 0xf6c);
AssertCompileMemberOffset(TEB_COMMON, GuaranteedStackBytes, ARCH_BITS == 64 ? 0x1748 : 0xf78);
AssertCompileMemberOffset(TEB_COMMON, MuiImpersonation,     ARCH_BITS == 64 ? 0x17e8 : 0xfc4);
AssertCompileMemberOffset(TEB_COMMON, LockCount,            ARCH_BITS == 64 ? 0x1808 : 0xfd8);
AssertCompileSize(TEB_COMMON, ARCH_BITS == 64 ? 0x1838 : 0x1000);


/** The size of the windows 8.1 PEB structure.  */
#define TEB_SIZE_W10    ( RT_UOFFSETOF(TEB_COMMON, Diff12.W10.EffectiveContainerId) + sizeof(RTUUID) )
/** The size of the windows 8.1 PEB structure.  */
#define TEB_SIZE_W81    ( RT_UOFFSETOF(TEB_COMMON, Diff12.W8.ReservedForWdf) + sizeof(PVOID) )
/** The size of the windows 8.0 PEB structure.  */
#define TEB_SIZE_W80    ( RT_UOFFSETOF(TEB_COMMON, Diff12.W8.ReservedForWdf) + sizeof(PVOID) )
/** The size of the windows 7 PEB structure.  */
#define TEB_SIZE_W7     RT_UOFFSETOF(TEB_COMMON, Diff12.W8.ReservedForWdf)
/** The size of the windows vista PEB structure.  */
#define TEB_SIZE_W6     ( RT_UOFFSETOF(TEB_COMMON, Diff12.W6.WaitReasonBitMap) + sizeof(LARGE_INTEGER) )
/** The size of the windows server 2003 PEB structure.  */
#define TEB_SIZE_W52    RT_ALIGN_Z(RT_UOFFSETOF(TEB_COMMON, Diff9.W52.BooleanSpare), sizeof(PVOID))
/** The size of the windows XP PEB structure.  */
#define TEB_SIZE_W51    RT_UOFFSETOF(TEB_COMMON, FlsData)



#define _PEB        _PEB_COMMON
typedef PEB_COMMON  PEB;
typedef PPEB_COMMON PPEB;

#define _TEB        _TEB_COMMON
typedef TEB_COMMON  TEB;
typedef PTEB_COMMON PTEB;

#if !defined(NtCurrentTeb) && !defined(IPRT_NT_HAVE_CURRENT_TEB_MACRO)
# ifdef RT_ARCH_X86
DECL_FORCE_INLINE(PTEB)     RTNtCurrentTeb(void) { return (PTEB)__readfsdword(RT_UOFFSETOF(TEB_COMMON, NtTib.Self)); }
DECL_FORCE_INLINE(PPEB)     RTNtCurrentPeb(void) { return (PPEB)__readfsdword(RT_UOFFSETOF(TEB_COMMON, ProcessEnvironmentBlock)); }
DECL_FORCE_INLINE(uint32_t) RTNtCurrentThreadId(void) { return __readfsdword(RT_UOFFSETOF(TEB_COMMON, ClientId.UniqueThread)); }
DECL_FORCE_INLINE(NTSTATUS) RTNtLastStatusValue(void) { return (NTSTATUS)__readfsdword(RT_UOFFSETOF(TEB_COMMON, LastStatusValue)); }
DECL_FORCE_INLINE(uint32_t) RTNtLastErrorValue(void)  { return __readfsdword(RT_UOFFSETOF(TEB_COMMON, LastErrorValue)); }
# elif defined(RT_ARCH_AMD64)
DECL_FORCE_INLINE(PTEB)     RTNtCurrentTeb(void) { return (PTEB)__readgsqword(RT_UOFFSETOF(TEB_COMMON, NtTib.Self)); }
DECL_FORCE_INLINE(PPEB)     RTNtCurrentPeb(void) { return (PPEB)__readgsqword(RT_UOFFSETOF(TEB_COMMON, ProcessEnvironmentBlock)); }
DECL_FORCE_INLINE(uint32_t) RTNtCurrentThreadId(void) { return __readgsdword(RT_UOFFSETOF(TEB_COMMON, ClientId.UniqueThread)); }
DECL_FORCE_INLINE(NTSTATUS) RTNtLastStatusValue(void) { return (NTSTATUS)__readgsdword(RT_UOFFSETOF(TEB_COMMON, LastStatusValue)); }
DECL_FORCE_INLINE(uint32_t) RTNtLastErrorValue(void)  { return __readgsdword(RT_UOFFSETOF(TEB_COMMON, LastErrorValue)); }
# else
#  error "Port me"
# endif
#else
# define RTNtCurrentTeb()        ((PTEB)NtCurrentTeb())
# define RTNtCurrentPeb()        (RTNtCurrentTeb()->ProcessEnvironmentBlock)
# define RTNtCurrentThreadId()   ((uint32_t)(uintptr_t)RTNtCurrentTeb()->ClientId.UniqueThread)
# define RTNtLastStatusValue()   (RTNtCurrentTeb()->LastStatusValue)
# define RTNtLastErrorValue()    (RTNtCurrentTeb()->LastErrorValue)
#endif
#define NtCurrentPeb()           RTNtCurrentPeb()

#ifdef IN_RING3
RT_DECL_NTAPI(void) RtlAcquirePebLock(void);
RT_DECL_NTAPI(void) RtlReleasePebLock(void);
#endif

/** @} */


#ifdef IPRT_NT_USE_WINTERNL
RT_DECL_NTAPI(NTSTATUS) NtCreateSection(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PLARGE_INTEGER, ULONG, ULONG, HANDLE);
typedef enum _SECTION_INHERIT
{
    ViewShare = 1,
    ViewUnmap
} SECTION_INHERIT;
#endif
RT_DECL_NTAPI(NTSTATUS) NtMapViewOfSection(HANDLE, HANDLE, PVOID *, ULONG, SIZE_T, PLARGE_INTEGER, PSIZE_T, SECTION_INHERIT,
                                           ULONG, ULONG);
RT_DECL_NTAPI(NTSTATUS) NtFlushVirtualMemory(HANDLE, PVOID *, PSIZE_T, PIO_STATUS_BLOCK);
RT_DECL_NTAPI(NTSTATUS) NtUnmapViewOfSection(HANDLE, PVOID);

#ifdef IPRT_NT_USE_WINTERNL
RT_DECL_NTAPI(NTSTATUS) NtOpenProcess(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PCLIENT_ID);
RT_DECL_NTAPI(NTSTATUS) ZwOpenProcess(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PCLIENT_ID);
#endif
RT_DECL_NTAPI(NTSTATUS) NtOpenThread(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PCLIENT_ID);
RT_DECL_NTAPI(NTSTATUS) ZwOpenThread(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PCLIENT_ID);
RT_DECL_NTAPI(NTSTATUS) NtAlertThread(HANDLE hThread);
#ifdef IPRT_NT_USE_WINTERNL
RT_DECL_NTAPI(NTSTATUS) ZwAlertThread(HANDLE hThread);
#endif
RT_DECL_NTAPI(NTSTATUS) NtTestAlert(void);

#ifdef IPRT_NT_USE_WINTERNL
RT_DECL_NTAPI(NTSTATUS) NtOpenProcessToken(HANDLE, ACCESS_MASK, PHANDLE);
RT_DECL_NTAPI(NTSTATUS) NtOpenThreadToken(HANDLE, ACCESS_MASK, BOOLEAN, PHANDLE);
#endif
RT_DECL_NTAPI(NTSTATUS) ZwOpenProcessToken(HANDLE, ACCESS_MASK, PHANDLE);
RT_DECL_NTAPI(NTSTATUS) ZwOpenThreadToken(HANDLE, ACCESS_MASK, BOOLEAN, PHANDLE);

#ifdef IPRT_NT_USE_WINTERNL
typedef struct _FILE_FS_VOLUME_INFORMATION
{
    LARGE_INTEGER   VolumeCreationTime;
    ULONG           VolumeSerialNumber;
    ULONG           VolumeLabelLength;
    BOOLEAN         SupportsObjects;
    WCHAR           VolumeLabel[1];
} FILE_FS_VOLUME_INFORMATION;
typedef FILE_FS_VOLUME_INFORMATION *PFILE_FS_VOLUME_INFORMATION;
typedef struct _FILE_FS_LABEL_INFORMATION
{
    ULONG           VolumeLabelLength;
    WCHAR           VolumeLabel[1];
} FILE_FS_LABEL_INFORMATION;
typedef FILE_FS_LABEL_INFORMATION *PFILE_FS_LABEL_INFORMATION;
typedef struct _FILE_FS_SIZE_INFORMATION
{
    LARGE_INTEGER   TotalAllocationUnits;
    LARGE_INTEGER   AvailableAllocationUnits;
    ULONG           SectorsPerAllocationUnit;
    ULONG           BytesPerSector;
} FILE_FS_SIZE_INFORMATION;
typedef FILE_FS_SIZE_INFORMATION *PFILE_FS_SIZE_INFORMATION;
typedef struct _FILE_FS_DEVICE_INFORMATION
{
    DEVICE_TYPE     DeviceType;
    ULONG           Characteristics;
} FILE_FS_DEVICE_INFORMATION;
typedef FILE_FS_DEVICE_INFORMATION *PFILE_FS_DEVICE_INFORMATION;
typedef struct _FILE_FS_ATTRIBUTE_INFORMATION
{
    ULONG           FileSystemAttributes;
    LONG            MaximumComponentNameLength;
    ULONG           FileSystemNameLength;
    WCHAR           FileSystemName[1];
} FILE_FS_ATTRIBUTE_INFORMATION;
typedef FILE_FS_ATTRIBUTE_INFORMATION *PFILE_FS_ATTRIBUTE_INFORMATION;
typedef struct _FILE_FS_CONTROL_INFORMATION
{
    LARGE_INTEGER   FreeSpaceStartFiltering;
    LARGE_INTEGER   FreeSpaceThreshold;
    LARGE_INTEGER   FreeSpaceStopFiltering;
    LARGE_INTEGER   DefaultQuotaThreshold;
    LARGE_INTEGER   DefaultQuotaLimit;
    ULONG           FileSystemControlFlags;
} FILE_FS_CONTROL_INFORMATION;
typedef FILE_FS_CONTROL_INFORMATION *PFILE_FS_CONTROL_INFORMATION;
typedef struct _FILE_FS_FULL_SIZE_INFORMATION
{
    LARGE_INTEGER   TotalAllocationUnits;
    LARGE_INTEGER   CallerAvailableAllocationUnits;
    LARGE_INTEGER   ActualAvailableAllocationUnits;
    ULONG           SectorsPerAllocationUnit;
    ULONG           BytesPerSector;
} FILE_FS_FULL_SIZE_INFORMATION;
typedef FILE_FS_FULL_SIZE_INFORMATION *PFILE_FS_FULL_SIZE_INFORMATION;
typedef struct _FILE_FS_OBJECTID_INFORMATION
{
    UCHAR           ObjectId[16];
    UCHAR           ExtendedInfo[48];
} FILE_FS_OBJECTID_INFORMATION;
typedef FILE_FS_OBJECTID_INFORMATION *PFILE_FS_OBJECTID_INFORMATION;
typedef struct _FILE_FS_DRIVER_PATH_INFORMATION
{
    BOOLEAN         DriverInPath;
    ULONG           DriverNameLength;
    WCHAR           DriverName[1];
} FILE_FS_DRIVER_PATH_INFORMATION;
typedef FILE_FS_DRIVER_PATH_INFORMATION *PFILE_FS_DRIVER_PATH_INFORMATION;
typedef struct _FILE_FS_VOLUME_FLAGS_INFORMATION
{
    ULONG           Flags;
} FILE_FS_VOLUME_FLAGS_INFORMATION;
typedef FILE_FS_VOLUME_FLAGS_INFORMATION *PFILE_FS_VOLUME_FLAGS_INFORMATION;
#endif
#if !defined(SSINFO_OFFSET_UNKNOWN) || defined(IPRT_NT_USE_WINTERNL)
typedef struct _FILE_FS_SECTOR_SIZE_INFORMATION
{
    ULONG           LogicalBytesPerSector;
    ULONG           PhysicalBytesPerSectorForAtomicity;
    ULONG           PhysicalBytesPerSectorForPerformance;
    ULONG           FileSystemEffectivePhysicalBytesPerSectorForAtomicity;
    ULONG           Flags;
    ULONG           ByteOffsetForSectorAlignment;
    ULONG           ByteOffsetForPartitionAlignment;
} FILE_FS_SECTOR_SIZE_INFORMATION;
typedef FILE_FS_SECTOR_SIZE_INFORMATION *PFILE_FS_SECTOR_SIZE_INFORMATION;
# ifndef SSINFO_OFFSET_UNKNOWN
#  define SSINFO_OFFSET_UNKNOWN                     0xffffffffUL
#  define SSINFO_FLAGS_ALIGNED_DEVICE               1UL
#  define SSINFO_FLAGS_PARTITION_ALIGNED_ON_DEVICE  2UL
#  define SSINFO_FLAGS_NO_SEEK_PENALTY              4UL
#  define SSINFO_FLAGS_TRIM_ENABLED                 8UL
#  define SSINFO_FLAGS_BYTE_ADDRESSABLE             16UL
# endif
#endif
#ifdef IPRT_NT_USE_WINTERNL
typedef struct _FILE_FS_DATA_COPY_INFORMATION
{
    ULONG           NumberOfCopies;
} FILE_FS_DATA_COPY_INFORMATION;
typedef FILE_FS_DATA_COPY_INFORMATION *PFILE_FS_DATA_COPY_INFORMATION;
typedef struct _FILE_FS_METADATA_SIZE_INFORMATION
{
    LARGE_INTEGER   TotalMetadataAllocationUnits;
    ULONG           SectorsPerAllocationUnit;
    ULONG           BytesPerSector;
} FILE_FS_METADATA_SIZE_INFORMATION;
typedef FILE_FS_METADATA_SIZE_INFORMATION *PFILE_FS_METADATA_SIZE_INFORMATION;
typedef struct _FILE_FS_FULL_SIZE_INFORMATION_EX
{
    ULONGLONG       ActualTotalAllocationUnits;
    ULONGLONG       ActualAvailableAllocationUnits;
    ULONGLONG       ActualPoolUnavailableAllocationUnits;
    ULONGLONG       CallerTotalAllocationUnits;
    ULONGLONG       CallerAvailableAllocationUnits;
    ULONGLONG       CallerPoolUnavailableAllocationUnits;
    ULONGLONG       UsedAllocationUnits;
    ULONGLONG       TotalReservedAllocationUnits;
    ULONGLONG       VolumeStorageReserveAllocationUnits;
    ULONGLONG       AvailableCommittedAllocationUnits;
    ULONGLONG       PoolAvailableAllocationUnits;
    ULONG           SectorsPerAllocationUnit;
    ULONG           BytesPerSector;
} FILE_FS_FULL_SIZE_INFORMATION_EX;
typedef FILE_FS_FULL_SIZE_INFORMATION_EX *PFILE_FS_FULL_SIZE_INFORMATION_EX;
#endif /* IPRT_NT_USE_WINTERNL */

typedef enum _FSINFOCLASS
{
    FileFsVolumeInformation = 1,
    FileFsLabelInformation,
    FileFsSizeInformation,          /**< FILE_FS_SIZE_INFORMATION */
    FileFsDeviceInformation,
    FileFsAttributeInformation,
    FileFsControlInformation,
    FileFsFullSizeInformation,
    FileFsObjectIdInformation,
    FileFsDriverPathInformation,
    FileFsVolumeFlagsInformation,
    FileFsSectorSizeInformation,
    FileFsDataCopyInformation,
    FileFsMetadataSizeInformation,
    FileFsFullSizeInformationEx,
    FileFsMaximumInformation
} FS_INFORMATION_CLASS;
typedef FS_INFORMATION_CLASS *PFS_INFORMATION_CLASS;
RT_DECL_NTAPI(NTSTATUS) NtQueryVolumeInformationFile(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FS_INFORMATION_CLASS);
RT_DECL_NTAPI(NTSTATUS) NtSetVolumeInformationFile(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FS_INFORMATION_CLASS);

#ifdef IPRT_NT_USE_WINTERNL
typedef struct _FILE_DIRECTORY_INFORMATION
{
    ULONG           NextEntryOffset;
    ULONG           FileIndex;
    LARGE_INTEGER   CreationTime;
    LARGE_INTEGER   LastAccessTime;
    LARGE_INTEGER   LastWriteTime;
    LARGE_INTEGER   ChangeTime;
    LARGE_INTEGER   EndOfFile;
    LARGE_INTEGER   AllocationSize;
    ULONG           FileAttributes;
    ULONG           FileNameLength;
    WCHAR           FileName[1];
} FILE_DIRECTORY_INFORMATION;
typedef FILE_DIRECTORY_INFORMATION *PFILE_DIRECTORY_INFORMATION;
typedef struct _FILE_FULL_DIR_INFORMATION
{
    ULONG           NextEntryOffset;
    ULONG           FileIndex;
    LARGE_INTEGER   CreationTime;
    LARGE_INTEGER   LastAccessTime;
    LARGE_INTEGER   LastWriteTime;
    LARGE_INTEGER   ChangeTime;
    LARGE_INTEGER   EndOfFile;
    LARGE_INTEGER   AllocationSize;
    ULONG           FileAttributes;
    ULONG           FileNameLength;
    ULONG           EaSize;
    WCHAR           FileName[1];
} FILE_FULL_DIR_INFORMATION;
typedef FILE_FULL_DIR_INFORMATION *PFILE_FULL_DIR_INFORMATION;
typedef struct _FILE_BOTH_DIR_INFORMATION
{
    ULONG           NextEntryOffset;    /**< 0x00: */
    ULONG           FileIndex;          /**< 0x04: */
    LARGE_INTEGER   CreationTime;       /**< 0x08: */
    LARGE_INTEGER   LastAccessTime;     /**< 0x10: */
    LARGE_INTEGER   LastWriteTime;      /**< 0x18: */
    LARGE_INTEGER   ChangeTime;         /**< 0x20: */
    LARGE_INTEGER   EndOfFile;          /**< 0x28: */
    LARGE_INTEGER   AllocationSize;     /**< 0x30: */
    ULONG           FileAttributes;     /**< 0x38: */
    ULONG           FileNameLength;     /**< 0x3c: */
    ULONG           EaSize;             /**< 0x40: */
    CCHAR           ShortNameLength;    /**< 0x44: */
    WCHAR           ShortName[12];      /**< 0x46: */
    WCHAR           FileName[1];        /**< 0x5e: */
} FILE_BOTH_DIR_INFORMATION;
typedef FILE_BOTH_DIR_INFORMATION *PFILE_BOTH_DIR_INFORMATION;
typedef struct _FILE_BASIC_INFORMATION
{
    LARGE_INTEGER   CreationTime;
    LARGE_INTEGER   LastAccessTime;
    LARGE_INTEGER   LastWriteTime;
    LARGE_INTEGER   ChangeTime;
    ULONG           FileAttributes;
} FILE_BASIC_INFORMATION;
typedef FILE_BASIC_INFORMATION *PFILE_BASIC_INFORMATION;
typedef struct _FILE_STANDARD_INFORMATION
{
    LARGE_INTEGER   AllocationSize;
    LARGE_INTEGER   EndOfFile;
    ULONG           NumberOfLinks;
    BOOLEAN         DeletePending;
    BOOLEAN         Directory;
} FILE_STANDARD_INFORMATION;
typedef FILE_STANDARD_INFORMATION *PFILE_STANDARD_INFORMATION;
typedef struct _FILE_NAME_INFORMATION
{
    ULONG           FileNameLength;
    WCHAR           FileName[1];
} FILE_NAME_INFORMATION;
typedef FILE_NAME_INFORMATION *PFILE_NAME_INFORMATION;
typedef FILE_NAME_INFORMATION FILE_NETWORK_PHYSICAL_NAME_INFORMATION;
typedef FILE_NETWORK_PHYSICAL_NAME_INFORMATION *PFILE_NETWORK_PHYSICAL_NAME_INFORMATION;
typedef struct _FILE_INTERNAL_INFORMATION
{
    LARGE_INTEGER   IndexNumber;
} FILE_INTERNAL_INFORMATION;
typedef FILE_INTERNAL_INFORMATION *PFILE_INTERNAL_INFORMATION;
typedef struct _FILE_EA_INFORMATION
{
    ULONG           EaSize;
} FILE_EA_INFORMATION;
typedef FILE_EA_INFORMATION *PFILE_EA_INFORMATION;
typedef struct _FILE_ACCESS_INFORMATION
{
    ACCESS_MASK     AccessFlags;
} FILE_ACCESS_INFORMATION;
typedef FILE_ACCESS_INFORMATION *PFILE_ACCESS_INFORMATION;
typedef struct _FILE_RENAME_INFORMATION
{
    union
    {
        BOOLEAN     ReplaceIfExists;
        ULONG       Flags;
    };
    HANDLE          RootDirectory;
    ULONG           FileNameLength;
    WCHAR           FileName[1];
} FILE_RENAME_INFORMATION;
typedef FILE_RENAME_INFORMATION *PFILE_RENAME_INFORMATION;
typedef struct _FILE_LINK_INFORMATION
{
    union
    {
        BOOLEAN     ReplaceIfExists;
        ULONG       Flags;
    };
    HANDLE          RootDirectory;
    ULONG           FileNameLength;
    WCHAR           FileName[1];
} FILE_LINK_INFORMATION;
typedef FILE_LINK_INFORMATION *PFILE_LINK_INFORMATION;
typedef struct _FILE_NAMES_INFORMATION
{
    ULONG           NextEntryOffset;
    ULONG           FileIndex;
    ULONG           FileNameLength;
    WCHAR           FileName[1];
} FILE_NAMES_INFORMATION;
typedef FILE_NAMES_INFORMATION *PFILE_NAMES_INFORMATION;
typedef struct _FILE_DISPOSITION_INFORMATION
{
    BOOLEAN         DeleteFile;
} FILE_DISPOSITION_INFORMATION;
typedef FILE_DISPOSITION_INFORMATION *PFILE_DISPOSITION_INFORMATION;
typedef struct _FILE_POSITION_INFORMATION
{
    LARGE_INTEGER   CurrentByteOffset;
} FILE_POSITION_INFORMATION;
typedef FILE_POSITION_INFORMATION *PFILE_POSITION_INFORMATION;
typedef struct _FILE_FULL_EA_INFORMATION
{
    ULONG           NextEntryOffset;
    UCHAR           Flags;
    UCHAR           EaNameLength;
    USHORT          EaValueLength;
    CHAR            EaName[1];
} FILE_FULL_EA_INFORMATION;
typedef FILE_FULL_EA_INFORMATION *PFILE_FULL_EA_INFORMATION;
typedef struct _FILE_MODE_INFORMATION
{
    ULONG           Mode;
} FILE_MODE_INFORMATION;
typedef FILE_MODE_INFORMATION *PFILE_MODE_INFORMATION;
typedef struct _FILE_ALIGNMENT_INFORMATION
{
    ULONG           AlignmentRequirement;
} FILE_ALIGNMENT_INFORMATION;
typedef FILE_ALIGNMENT_INFORMATION *PFILE_ALIGNMENT_INFORMATION;
typedef struct _FILE_ALL_INFORMATION
{
    FILE_BASIC_INFORMATION      BasicInformation;
    FILE_STANDARD_INFORMATION   StandardInformation;
    FILE_INTERNAL_INFORMATION   InternalInformation;
    FILE_EA_INFORMATION         EaInformation;
    FILE_ACCESS_INFORMATION     AccessInformation;
    FILE_POSITION_INFORMATION   PositionInformation;
    FILE_MODE_INFORMATION       ModeInformation;
    FILE_ALIGNMENT_INFORMATION  AlignmentInformation;
    FILE_NAME_INFORMATION       NameInformation;
} FILE_ALL_INFORMATION;
typedef FILE_ALL_INFORMATION *PFILE_ALL_INFORMATION;
typedef struct _FILE_ALLOCATION_INFORMATION
{
    LARGE_INTEGER   AllocationSize;
} FILE_ALLOCATION_INFORMATION;
typedef FILE_ALLOCATION_INFORMATION *PFILE_ALLOCATION_INFORMATION;
typedef struct _FILE_END_OF_FILE_INFORMATION
{
    LARGE_INTEGER   EndOfFile;
} FILE_END_OF_FILE_INFORMATION;
typedef FILE_END_OF_FILE_INFORMATION *PFILE_END_OF_FILE_INFORMATION;
typedef struct _FILE_STREAM_INFORMATION
{
    ULONG           NextEntryOffset;
    ULONG           StreamNameLength;
    LARGE_INTEGER   StreamSize;
    LARGE_INTEGER   StreamAllocationSize;
    WCHAR           StreamName[1];
} FILE_STREAM_INFORMATION;
typedef FILE_STREAM_INFORMATION *PFILE_STREAM_INFORMATION;

typedef struct _FILE_PIPE_INFORMATION
{
    ULONG           ReadMode;
    ULONG           CompletionMode;
} FILE_PIPE_INFORMATION;
typedef FILE_PIPE_INFORMATION *PFILE_PIPE_INFORMATION;

typedef struct _FILE_PIPE_LOCAL_INFORMATION
{
    ULONG           NamedPipeType;
    ULONG           NamedPipeConfiguration;
    ULONG           MaximumInstances;
    ULONG           CurrentInstances;
    ULONG           InboundQuota;
    ULONG           ReadDataAvailable;
    ULONG           OutboundQuota;
    ULONG           WriteQuotaAvailable;
    ULONG           NamedPipeState;
    ULONG           NamedPipeEnd;
} FILE_PIPE_LOCAL_INFORMATION;
typedef FILE_PIPE_LOCAL_INFORMATION *PFILE_PIPE_LOCAL_INFORMATION;

/** @name Pipe state (FILE_PIPE_LOCAL_INFORMATION::NamedPipeState)
 * @{  */
#if !defined(FILE_PIPE_DISCONNECTED_STATE) || defined(DOXYGEN_RUNNING)
# define FILE_PIPE_DISCONNECTED_STATE   0x00000001U
# define FILE_PIPE_LISTENING_STATE      0x00000002U
# define FILE_PIPE_CONNECTED_STATE      0x00000003U
# define FILE_PIPE_CLOSING_STATE        0x00000004U
#endif
/** @} */

/** @name Pipe config (FILE_PIPE_LOCAL_INFORMATION::NamedPipeConfiguration)
 * @{ */
#if !defined(FILE_PIPE_INBOUND) || defined(DOXYGEN_RUNNING)
# define FILE_PIPE_INBOUND              0x00000000U
# define FILE_PIPE_OUTBOUND             0x00000001U
# define FILE_PIPE_FULL_DUPLEX          0x00000002U
#endif
/** @} */

/** @name Pipe end (FILE_PIPE_LOCAL_INFORMATION::NamedPipeEnd)
 * @{ */
#if !defined(FILE_PIPE_CLIENT_END) || defined(DOXYGEN_RUNNING)
# define FILE_PIPE_CLIENT_END           0x00000000U
# define FILE_PIPE_SERVER_END           0x00000001U
#endif
/** @} */

typedef struct _FILE_PIPE_REMOTE_INFORMATION
{
    LARGE_INTEGER   CollectDataTime;
    ULONG           MaximumCollectionCount;
} FILE_PIPE_REMOTE_INFORMATION;
typedef FILE_PIPE_REMOTE_INFORMATION *PFILE_PIPE_REMOTE_INFORMATION;
typedef struct _FILE_MAILSLOT_QUERY_INFORMATION
{
    ULONG           MaximumMessageSize;
    ULONG           MailslotQuota;
    ULONG           NextMessageSize;
    ULONG           MessagesAvailable;
    LARGE_INTEGER   ReadTimeout;
} FILE_MAILSLOT_QUERY_INFORMATION;
typedef FILE_MAILSLOT_QUERY_INFORMATION *PFILE_MAILSLOT_QUERY_INFORMATION;
typedef struct _FILE_MAILSLOT_SET_INFORMATION
{
    PLARGE_INTEGER  ReadTimeout;
} FILE_MAILSLOT_SET_INFORMATION;
typedef FILE_MAILSLOT_SET_INFORMATION *PFILE_MAILSLOT_SET_INFORMATION;
typedef struct _FILE_COMPRESSION_INFORMATION
{
    LARGE_INTEGER   CompressedFileSize;
    USHORT          CompressionFormat;
    UCHAR           CompressionUnitShift;
    UCHAR           ChunkShift;
    UCHAR           ClusterShift;
    UCHAR           Reserved[3];
} FILE_COMPRESSION_INFORMATION;
typedef FILE_COMPRESSION_INFORMATION *PFILE_COMPRESSION_INFORMATION;
typedef struct _FILE_OBJECTID_INFORMATION
{
    LONGLONG        FileReference;
    UCHAR           ObjectId[16];
    union
    {
        struct
        {
            UCHAR   BirthVolumeId[16];
            UCHAR   BirthObjectId[16];
            UCHAR   DomainId[16];
        };
        UCHAR       ExtendedInfo[48];
    };
} FILE_OBJECTID_INFORMATION;
typedef FILE_OBJECTID_INFORMATION *PFILE_OBJECTID_INFORMATION;
typedef struct _FILE_COMPLETION_INFORMATION
{
    HANDLE          Port;
    PVOID           Key;
} FILE_COMPLETION_INFORMATION;
typedef FILE_COMPLETION_INFORMATION *PFILE_COMPLETION_INFORMATION;
typedef struct _FILE_MOVE_CLUSTER_INFORMATION
{
    ULONG           ClusterCount;
    HANDLE          RootDirectory;
    ULONG           FileNameLength;
    WCHAR           FileName[1];
} FILE_MOVE_CLUSTER_INFORMATION;
typedef FILE_MOVE_CLUSTER_INFORMATION *PFILE_MOVE_CLUSTER_INFORMATION;
typedef struct _FILE_QUOTA_INFORMATION
{
    ULONG           NextEntryOffset;
    ULONG           SidLength;
    LARGE_INTEGER   ChangeTime;
    LARGE_INTEGER   QuotaUsed;
    LARGE_INTEGER   QuotaThreshold;
    LARGE_INTEGER   QuotaLimit;
    SID             Sid;
} FILE_QUOTA_INFORMATION;
typedef FILE_QUOTA_INFORMATION *PFILE_QUOTA_INFORMATION;
typedef struct _FILE_REPARSE_POINT_INFORMATION
{
    LONGLONG        FileReference;
    ULONG           Tag;
} FILE_REPARSE_POINT_INFORMATION;
typedef FILE_REPARSE_POINT_INFORMATION *PFILE_REPARSE_POINT_INFORMATION;
typedef struct _FILE_NETWORK_OPEN_INFORMATION
{
    LARGE_INTEGER   CreationTime;
    LARGE_INTEGER   LastAccessTime;
    LARGE_INTEGER   LastWriteTime;
    LARGE_INTEGER   ChangeTime;
    LARGE_INTEGER   AllocationSize;
    LARGE_INTEGER   EndOfFile;
    ULONG           FileAttributes;
} FILE_NETWORK_OPEN_INFORMATION;
typedef FILE_NETWORK_OPEN_INFORMATION *PFILE_NETWORK_OPEN_INFORMATION;
typedef struct _FILE_ATTRIBUTE_TAG_INFORMATION
{
    ULONG           FileAttributes;
    ULONG           ReparseTag;
} FILE_ATTRIBUTE_TAG_INFORMATION;
typedef FILE_ATTRIBUTE_TAG_INFORMATION *PFILE_ATTRIBUTE_TAG_INFORMATION;
typedef struct _FILE_TRACKING_INFORMATION
{
    HANDLE          DestinationFile;
    ULONG           ObjectInformationLength;
    CHAR            ObjectInformation[1];
} FILE_TRACKING_INFORMATION;
typedef FILE_TRACKING_INFORMATION *PFILE_TRACKING_INFORMATION;
typedef struct _FILE_ID_BOTH_DIR_INFORMATION
{
    ULONG           NextEntryOffset;
    ULONG           FileIndex;
    LARGE_INTEGER   CreationTime;
    LARGE_INTEGER   LastAccessTime;
    LARGE_INTEGER   LastWriteTime;
    LARGE_INTEGER   ChangeTime;
    LARGE_INTEGER   EndOfFile;
    LARGE_INTEGER   AllocationSize;
    ULONG           FileAttributes;
    ULONG           FileNameLength;
    ULONG           EaSize;
    CCHAR           ShortNameLength;
    WCHAR           ShortName[12];
    LARGE_INTEGER   FileId;
    WCHAR           FileName[1];
} FILE_ID_BOTH_DIR_INFORMATION;
typedef FILE_ID_BOTH_DIR_INFORMATION *PFILE_ID_BOTH_DIR_INFORMATION;
typedef struct _FILE_ID_FULL_DIR_INFORMATION
{
    ULONG           NextEntryOffset;
    ULONG           FileIndex;
    LARGE_INTEGER   CreationTime;
    LARGE_INTEGER   LastAccessTime;
    LARGE_INTEGER   LastWriteTime;
    LARGE_INTEGER   ChangeTime;
    LARGE_INTEGER   EndOfFile;
    LARGE_INTEGER   AllocationSize;
    ULONG           FileAttributes;
    ULONG           FileNameLength;
    ULONG           EaSize;
    LARGE_INTEGER   FileId;
    WCHAR           FileName[1];
} FILE_ID_FULL_DIR_INFORMATION;
typedef FILE_ID_FULL_DIR_INFORMATION *PFILE_ID_FULL_DIR_INFORMATION;
typedef struct _FILE_VALID_DATA_LENGTH_INFORMATION
{
    LARGE_INTEGER   ValidDataLength;
} FILE_VALID_DATA_LENGTH_INFORMATION;
typedef FILE_VALID_DATA_LENGTH_INFORMATION *PFILE_VALID_DATA_LENGTH_INFORMATION;
typedef struct _FILE_IO_COMPLETION_NOTIFICATION_INFORMATION
{
    ULONG           Flags;
} FILE_IO_COMPLETION_NOTIFICATION_INFORMATION;
typedef FILE_IO_COMPLETION_NOTIFICATION_INFORMATION *PFILE_IO_COMPLETION_NOTIFICATION_INFORMATION;
typedef enum _IO_PRIORITY_HINT
{
    IoPriorityVeryLow = 0,
    IoPriorityLow,
    IoPriorityNormal,
    IoPriorityHigh,
    IoPriorityCritical,
    MaxIoPriorityTypes
} IO_PRIORITY_HINT;
AssertCompileSize(IO_PRIORITY_HINT, sizeof(int));
typedef struct _FILE_IO_PRIORITY_HINT_INFORMATION
{
    IO_PRIORITY_HINT    PriorityHint;
} FILE_IO_PRIORITY_HINT_INFORMATION;
typedef FILE_IO_PRIORITY_HINT_INFORMATION *PFILE_IO_PRIORITY_HINT_INFORMATION;
typedef struct _FILE_SFIO_RESERVE_INFORMATION
{
    ULONG           RequestsPerPeriod;
    ULONG           Period;
    BOOLEAN         RetryFailures;
    BOOLEAN         Discardable;
    ULONG           RequestSize;
    ULONG           NumOutstandingRequests;
} FILE_SFIO_RESERVE_INFORMATION;
typedef FILE_SFIO_RESERVE_INFORMATION *PFILE_SFIO_RESERVE_INFORMATION;
typedef struct _FILE_SFIO_VOLUME_INFORMATION
{
    ULONG           MaximumRequestsPerPeriod;
    ULONG           MinimumPeriod;
    ULONG           MinimumTransferSize;
} FILE_SFIO_VOLUME_INFORMATION;
typedef FILE_SFIO_VOLUME_INFORMATION *PFILE_SFIO_VOLUME_INFORMATION;
typedef struct _FILE_LINK_ENTRY_INFORMATION
{
    ULONG           NextEntryOffset;
    LONGLONG        ParentFileId;
    ULONG           FileNameLength;
    WCHAR           FileName[1];
} FILE_LINK_ENTRY_INFORMATION;
typedef FILE_LINK_ENTRY_INFORMATION *PFILE_LINK_ENTRY_INFORMATION;
typedef struct _FILE_LINKS_INFORMATION
{
    ULONG                       BytesNeeded;
    ULONG                       EntriesReturned;
    FILE_LINK_ENTRY_INFORMATION Entry;
} FILE_LINKS_INFORMATION;
typedef FILE_LINKS_INFORMATION *PFILE_LINKS_INFORMATION;
typedef struct _FILE_PROCESS_IDS_USING_FILE_INFORMATION
{
    ULONG           NumberOfProcessIdsInList;
    ULONG_PTR       ProcessIdList[1];
} FILE_PROCESS_IDS_USING_FILE_INFORMATION;
typedef FILE_PROCESS_IDS_USING_FILE_INFORMATION *PFILE_PROCESS_IDS_USING_FILE_INFORMATION;
typedef struct _FILE_ID_GLOBAL_TX_DIR_INFORMATION
{
    ULONG           NextEntryOffset;
    ULONG           FileIndex;
    LARGE_INTEGER   CreationTime;
    LARGE_INTEGER   LastAccessTime;
    LARGE_INTEGER   LastWriteTime;
    LARGE_INTEGER   ChangeTime;
    LARGE_INTEGER   EndOfFile;
    LARGE_INTEGER   AllocationSize;
    ULONG           FileAttributes;
    ULONG           FileNameLength;
    LARGE_INTEGER   FileId;
    GUID            LockingTransactionId;
    ULONG           TxInfoFlags;
    WCHAR           FileName[1];
} FILE_ID_GLOBAL_TX_DIR_INFORMATION;
typedef FILE_ID_GLOBAL_TX_DIR_INFORMATION *PFILE_ID_GLOBAL_TX_DIR_INFORMATION;
typedef struct _FILE_IS_REMOTE_DEVICE_INFORMATION
{
    BOOLEAN         IsRemote;
} FILE_IS_REMOTE_DEVICE_INFORMATION;
typedef FILE_IS_REMOTE_DEVICE_INFORMATION *PFILE_IS_REMOTE_DEVICE_INFORMATION;
typedef struct _FILE_NUMA_NODE_INFORMATION
{
    USHORT          NodeNumber;
} FILE_NUMA_NODE_INFORMATION;
typedef FILE_NUMA_NODE_INFORMATION *PFILE_NUMA_NODE_INFORMATION;
typedef struct _FILE_STANDARD_LINK_INFORMATION
{
    ULONG           NumberOfAccessibleLinks;
    ULONG           TotalNumberOfLinks;
    BOOLEAN         DeletePending;
    BOOLEAN         Directory;
} FILE_STANDARD_LINK_INFORMATION;
typedef FILE_STANDARD_LINK_INFORMATION *PFILE_STANDARD_LINK_INFORMATION;
typedef struct _FILE_REMOTE_PROTOCOL_INFORMATION
{
    USHORT          StructureVersion;
    USHORT          StructureSize;
    ULONG           Protocol;
    USHORT          ProtocolMajorVersion;
    USHORT          ProtocolMinorVersion;
    USHORT          ProtocolRevision;
    USHORT          Reserved;
    ULONG           Flags;
    struct
    {
        ULONG       Reserved[8];
    }               GenericReserved;
    struct
    {
        ULONG       Reserved[16];
    }               ProtocolSpecificReserved;
} FILE_REMOTE_PROTOCOL_INFORMATION;
typedef FILE_REMOTE_PROTOCOL_INFORMATION *PFILE_REMOTE_PROTOCOL_INFORMATION;
typedef struct _FILE_VOLUME_NAME_INFORMATION
{
    ULONG           DeviceNameLength;
    WCHAR           DeviceName[1];
} FILE_VOLUME_NAME_INFORMATION;
typedef FILE_VOLUME_NAME_INFORMATION *PFILE_VOLUME_NAME_INFORMATION;
# ifndef FILE_INVALID_FILE_ID
typedef struct _FILE_ID_128
{
    BYTE            Identifier[16];
} FILE_ID_128;
typedef FILE_ID_128 *PFILE_ID_128;
# endif
typedef struct _FILE_ID_EXTD_DIR_INFORMATION
{
    ULONG           NextEntryOffset;
    ULONG           FileIndex;
    LARGE_INTEGER   CreationTime;
    LARGE_INTEGER   LastAccessTime;
    LARGE_INTEGER   LastWriteTime;
    LARGE_INTEGER   ChangeTime;
    LARGE_INTEGER   EndOfFile;
    LARGE_INTEGER   AllocationSize;
    ULONG           FileAttributes;
    ULONG           FileNameLength;
    ULONG           EaSize;
    ULONG           ReparsePointTag;
    FILE_ID_128     FileId;
    WCHAR           FileName[1];
} FILE_ID_EXTD_DIR_INFORMATION;
typedef FILE_ID_EXTD_DIR_INFORMATION *PFILE_ID_EXTD_DIR_INFORMATION;
typedef struct _FILE_ID_EXTD_BOTH_DIR_INFORMATION
{
    ULONG           NextEntryOffset;
    ULONG           FileIndex;
    LARGE_INTEGER   CreationTime;
    LARGE_INTEGER   LastAccessTime;
    LARGE_INTEGER   LastWriteTime;
    LARGE_INTEGER   ChangeTime;
    LARGE_INTEGER   EndOfFile;
    LARGE_INTEGER   AllocationSize;
    ULONG           FileAttributes;
    ULONG           FileNameLength;
    ULONG           EaSize;
    ULONG           ReparsePointTag;
    FILE_ID_128     FileId;
    CCHAR           ShortNameLength;
    WCHAR           ShortName[12];
    WCHAR           FileName[1];
} FILE_ID_EXTD_BOTH_DIR_INFORMATION;
typedef FILE_ID_EXTD_BOTH_DIR_INFORMATION *PFILE_ID_EXTD_BOTH_DIR_INFORMATION;
typedef struct _FILE_ID_INFORMATION
{
    ULONGLONG       VolumeSerialNumber;
    FILE_ID_128     FileId;
} FILE_ID_INFORMATION;
typedef FILE_ID_INFORMATION *PFILE_ID_INFORMATION;
typedef struct _FILE_LINK_ENTRY_FULL_ID_INFORMATION
{
    ULONG           NextEntryOffset;
    FILE_ID_128     ParentFileId;
    ULONG           FileNameLength;
    WCHAR           FileName[1];
} FILE_LINK_ENTRY_FULL_ID_INFORMATION;
typedef FILE_LINK_ENTRY_FULL_ID_INFORMATION *PFILE_LINK_ENTRY_FULL_ID_INFORMATION;
typedef struct _FILE_LINKS_FULL_ID_INFORMATION {
    ULONG                                   BytesNeeded;
    ULONG                                   EntriesReturned;
    FILE_LINK_ENTRY_FULL_ID_INFORMATION     Entry;
} FILE_LINKS_FULL_ID_INFORMATION;
typedef FILE_LINKS_FULL_ID_INFORMATION *PFILE_LINKS_FULL_ID_INFORMATION;
typedef struct _FILE_DISPOSITION_INFORMATION_EX
{
    ULONG           Flags;
} FILE_DISPOSITION_INFORMATION_EX;
typedef FILE_DISPOSITION_INFORMATION_EX *PFILE_DISPOSITION_INFORMATION_EX;
# ifndef QUERY_STORAGE_CLASSES_FLAGS_MEASURE_WRITE
typedef struct _FILE_DESIRED_STORAGE_CLASS_INFORMATION
{
    /*FILE_STORAGE_TIER_CLASS*/ ULONG   Class;
    ULONG                               Flags;
} FILE_DESIRED_STORAGE_CLASS_INFORMATION;
typedef FILE_DESIRED_STORAGE_CLASS_INFORMATION *PFILE_DESIRED_STORAGE_CLASS_INFORMATION;
# endif
typedef struct _FILE_STAT_INFORMATION
{
    LARGE_INTEGER   FileId;
    LARGE_INTEGER   CreationTime;
    LARGE_INTEGER   LastAccessTime;
    LARGE_INTEGER   LastWriteTime;
    LARGE_INTEGER   ChangeTime;
    LARGE_INTEGER   AllocationSize;
    LARGE_INTEGER   EndOfFile;
    ULONG           FileAttributes;
    ULONG           ReparseTag;
    ULONG           NumberOfLinks;
    ACCESS_MASK     EffectiveAccess;
} FILE_STAT_INFORMATION;
typedef FILE_STAT_INFORMATION *PFILE_STAT_INFORMATION;
typedef struct _FILE_STAT_LX_INFORMATION
{
    LARGE_INTEGER   FileId;
    LARGE_INTEGER   CreationTime;
    LARGE_INTEGER   LastAccessTime;
    LARGE_INTEGER   LastWriteTime;
    LARGE_INTEGER   ChangeTime;
    LARGE_INTEGER   AllocationSize;
    LARGE_INTEGER   EndOfFile;
    ULONG           FileAttributes;
    ULONG           ReparseTag;
    ULONG           NumberOfLinks;
    ACCESS_MASK     EffectiveAccess;
    ULONG           LxFlags;
    ULONG           LxUid;
    ULONG           LxGid;
    ULONG           LxMode;
    ULONG           LxDeviceIdMajor;
    ULONG           LxDeviceIdMinor;
} FILE_STAT_LX_INFORMATION;
typedef FILE_STAT_LX_INFORMATION *PFILE_STAT_LX_INFORMATION;
typedef struct _FILE_CASE_SENSITIVE_INFORMATION
{
    ULONG           Flags;
} FILE_CASE_SENSITIVE_INFORMATION;
typedef FILE_CASE_SENSITIVE_INFORMATION *PFILE_CASE_SENSITIVE_INFORMATION;

typedef enum _FILE_INFORMATION_CLASS
{
    FileDirectoryInformation = 1,
    FileFullDirectoryInformation,
    FileBothDirectoryInformation,
    FileBasicInformation,
    FileStandardInformation,
    FileInternalInformation,
    FileEaInformation,
    FileAccessInformation,
    FileNameInformation,
    FileRenameInformation,
    FileLinkInformation,
    FileNamesInformation,
    FileDispositionInformation,
    FilePositionInformation,
    FileFullEaInformation,
    FileModeInformation,
    FileAlignmentInformation,
    FileAllInformation,
    FileAllocationInformation,
    FileEndOfFileInformation,
    FileAlternateNameInformation,
    FileStreamInformation,
    FilePipeInformation,
    FilePipeLocalInformation,
    FilePipeRemoteInformation,
    FileMailslotQueryInformation,
    FileMailslotSetInformation,
    FileCompressionInformation,
    /* NT351 and NT4 have different stuff assigned from here on. */
    FileObjectIdInformation,        /**< NT4 and earlier has FileCopyOnWriteInformation here - only for setting, not querying. */
    FileCompletionInformation,
    FileMoveClusterInformation,
    FileQuotaInformation,           /**< NT35 has FileStorageInformation here (query+set) and NT4 FileOleClassIdInformation (set only). Last for NT35. */
    FileReparsePointInformation,    /**< NT4 has FileOleStateBitsInformation here (set only). */
    FileNetworkOpenInformation,
    FileAttributeTagInformation,    /**< NT4 has FileObjectIdInformation here (set only). */
    FileTrackingInformation,        /**< NT4 has FileOleAllInformation here (query only). Last for W2K. */
    FileIdBothDirectoryInformation, /**< NT4 has FileOleDirectoryInformation here (dir query only). */
    FileIdFullDirectoryInformation, /**< NT4 has FileContentIndexInformation here (set only). */
    FileValidDataLengthInformation, /**< NT4 has FileInheritContentIndexInformation here (set only). */
    FileShortNameInformation,       /**< NT4 has FileOleInformation here (query + set). Last for NT4. */
    FileIoCompletionNotificationInformation,
    FileIoStatusBlockRangeInformation,
    FileIoPriorityHintInformation,
    FileSfioReserveInformation,
    FileSfioVolumeInformation,
    FileHardLinkInformation,
    FileProcessIdsUsingFileInformation,
    FileNormalizedNameInformation,
    FileNetworkPhysicalNameInformation,
    FileIdGlobalTxDirectoryInformation,
    FileIsRemoteDeviceInformation,
    FileUnusedInformation,
    FileNumaNodeInformation,
    FileStandardLinkInformation,
    FileRemoteProtocolInformation,
    /* Defined with Windows 10: */
    FileRenameInformationBypassAccessCheck,
    FileLinkInformationBypassAccessCheck,
    FileVolumeNameInformation,
    FileIdInformation,
    FileIdExtdDirectoryInformation,
    FileReplaceCompletionInformation,
    FileHardLinkFullIdInformation,
    FileIdExtdBothDirectoryInformation,
    FileDispositionInformationEx,
    FileRenameInformationEx,
    FileRenameInformationExBypassAccessCheck,
    FileDesiredStorageClassInformation,
    FileStatInformation,
    FileMemoryPartitionInformation,
    FileStatLxInformation,
    FileCaseSensitiveInformation,
    FileLinkInformationEx,
    FileLinkInformationExBypassAccessCheck,
    FileStorageReserveIdInformation,
    FileCaseSensitiveInformationForceAccessCheck,
    FileMaximumInformation
} FILE_INFORMATION_CLASS;
typedef FILE_INFORMATION_CLASS *PFILE_INFORMATION_CLASS;
RT_DECL_NTAPI(NTSTATUS) NtQueryInformationFile(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);
RT_DECL_NTAPI(NTSTATUS) NtQueryDirectoryFile(HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID, PIO_STATUS_BLOCK, PVOID, ULONG,
                                             FILE_INFORMATION_CLASS, BOOLEAN, PUNICODE_STRING, BOOLEAN);
RT_DECL_NTAPI(NTSTATUS) NtSetInformationFile(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);
#endif /* IPRT_NT_USE_WINTERNL */
RT_DECL_NTAPI(NTSTATUS) NtQueryAttributesFile(POBJECT_ATTRIBUTES, PFILE_BASIC_INFORMATION);
RT_DECL_NTAPI(NTSTATUS) NtQueryFullAttributesFile(POBJECT_ATTRIBUTES, PFILE_NETWORK_OPEN_INFORMATION);


/** @name SE_GROUP_XXX - Attributes returned with TokenGroup and others.
 * @{  */
#ifndef SE_GROUP_MANDATORY
# define SE_GROUP_MANDATORY             UINT32_C(0x01)
#endif
#ifndef SE_GROUP_ENABLED_BY_DEFAULT
# define SE_GROUP_ENABLED_BY_DEFAULT    UINT32_C(0x02)
#endif
#ifndef SE_GROUP_ENABLED
# define SE_GROUP_ENABLED               UINT32_C(0x04)
#endif
#ifndef SE_GROUP_OWNER
# define SE_GROUP_OWNER                 UINT32_C(0x08)
#endif
#ifndef SE_GROUP_USE_FOR_DENY_ONLY
# define SE_GROUP_USE_FOR_DENY_ONLY     UINT32_C(0x10)
#endif
#ifndef SE_GROUP_INTEGRITY
# define SE_GROUP_INTEGRITY             UINT32_C(0x20)
#endif
#ifndef SE_GROUP_INTEGRITY_ENABLED
# define SE_GROUP_INTEGRITY_ENABLED     UINT32_C(0x40)
#endif
#ifndef SE_GROUP_RESOURCE
# define SE_GROUP_RESOURCE              UINT32_C(0x20000000)
#endif
#ifndef SE_GROUP_LOGON_ID
# define SE_GROUP_LOGON_ID              UINT32_C(0xc0000000)
#endif
/** @} */


#ifdef IPRT_NT_USE_WINTERNL

/** For use with KeyBasicInformation. */
typedef struct _KEY_BASIC_INFORMATION
{
    LARGE_INTEGER   LastWriteTime;
    ULONG           TitleIndex;
    ULONG           NameLength;
    WCHAR           Name[1];
} KEY_BASIC_INFORMATION;
typedef KEY_BASIC_INFORMATION *PKEY_BASIC_INFORMATION;

/** For use with KeyNodeInformation. */
typedef struct _KEY_NODE_INFORMATION
{
    LARGE_INTEGER   LastWriteTime;
    ULONG           TitleIndex;
    ULONG           ClassOffset; /**< Offset from the start of the structure. */
    ULONG           ClassLength;
    ULONG           NameLength;
    WCHAR           Name[1];
} KEY_NODE_INFORMATION;
typedef KEY_NODE_INFORMATION *PKEY_NODE_INFORMATION;

/** For use with KeyFullInformation. */
typedef struct _KEY_FULL_INFORMATION
{
    LARGE_INTEGER   LastWriteTime;
    ULONG           TitleIndex;
    ULONG           ClassOffset; /**< Offset of the Class member. */
    ULONG           ClassLength;
    ULONG           SubKeys;
    ULONG           MaxNameLen;
    ULONG           MaxClassLen;
    ULONG           Values;
    ULONG           MaxValueNameLen;
    ULONG           MaxValueDataLen;
    WCHAR           Class[1];
} KEY_FULL_INFORMATION;
typedef KEY_FULL_INFORMATION *PKEY_FULL_INFORMATION;

/** For use with KeyNameInformation. */
typedef struct _KEY_NAME_INFORMATION
{
    ULONG           NameLength;
    WCHAR           Name[1];
} KEY_NAME_INFORMATION;
typedef KEY_NAME_INFORMATION *PKEY_NAME_INFORMATION;

/** For use with KeyCachedInformation. */
typedef struct _KEY_CACHED_INFORMATION
{
    LARGE_INTEGER   LastWriteTime;
    ULONG           TitleIndex;
    ULONG           SubKeys;
    ULONG           MaxNameLen;
    ULONG           Values;
    ULONG           MaxValueNameLen;
    ULONG           MaxValueDataLen;
    ULONG           NameLength;
} KEY_CACHED_INFORMATION;
typedef KEY_CACHED_INFORMATION *PKEY_CACHED_INFORMATION;

/** For use with KeyVirtualizationInformation. */
typedef struct _KEY_VIRTUALIZATION_INFORMATION
{
    ULONG           VirtualizationCandidate : 1;
    ULONG           VirtualizationEnabled   : 1;
    ULONG           VirtualTarget           : 1;
    ULONG           VirtualStore            : 1;
    ULONG           VirtualSource           : 1;
    ULONG           Reserved                : 27;
} KEY_VIRTUALIZATION_INFORMATION;
typedef KEY_VIRTUALIZATION_INFORMATION *PKEY_VIRTUALIZATION_INFORMATION;

typedef enum _KEY_INFORMATION_CLASS
{
    KeyBasicInformation = 0,
    KeyNodeInformation,
    KeyFullInformation,
    KeyNameInformation,
    KeyCachedInformation,
    KeyFlagsInformation,
    KeyVirtualizationInformation,
    KeyHandleTagsInformation,
    MaxKeyInfoClass
} KEY_INFORMATION_CLASS;
RT_DECL_NTAPI(NTSTATUS) NtQueryKey(HANDLE, KEY_INFORMATION_CLASS, PVOID, ULONG, PULONG);
RT_DECL_NTAPI(NTSTATUS) NtEnumerateKey(HANDLE, ULONG, KEY_INFORMATION_CLASS, PVOID, ULONG, PULONG);

typedef struct _MEMORY_SECTION_NAME
{
    UNICODE_STRING  SectionFileName;
    WCHAR           NameBuffer[1];
} MEMORY_SECTION_NAME;

#ifdef IPRT_NT_USE_WINTERNL
typedef struct _PROCESS_BASIC_INFORMATION
{
    NTSTATUS ExitStatus;
    PPEB PebBaseAddress;
    ULONG_PTR AffinityMask;
    int32_t BasePriority;
    ULONG_PTR UniqueProcessId;
    ULONG_PTR InheritedFromUniqueProcessId;
} PROCESS_BASIC_INFORMATION;
typedef PROCESS_BASIC_INFORMATION *PPROCESS_BASIC_INFORMATION;
#endif

typedef enum _PROCESSINFOCLASS
{
    ProcessBasicInformation = 0,                /**<  0 / 0x00 */
    ProcessQuotaLimits,                         /**<  1 / 0x01 */
    ProcessIoCounters,                          /**<  2 / 0x02 */
    ProcessVmCounters,                          /**<  3 / 0x03 */
    ProcessTimes,                               /**<  4 / 0x04 */
    ProcessBasePriority,                        /**<  5 / 0x05 */
    ProcessRaisePriority,                       /**<  6 / 0x06 */
    ProcessDebugPort,                           /**<  7 / 0x07 */
    ProcessExceptionPort,                       /**<  8 / 0x08 */
    ProcessAccessToken,                         /**<  9 / 0x09 */
    ProcessLdtInformation,                      /**< 10 / 0x0a */
    ProcessLdtSize,                             /**< 11 / 0x0b */
    ProcessDefaultHardErrorMode,                /**< 12 / 0x0c */
    ProcessIoPortHandlers,                      /**< 13 / 0x0d */
    ProcessPooledUsageAndLimits,                /**< 14 / 0x0e */
    ProcessWorkingSetWatch,                     /**< 15 / 0x0f */
    ProcessUserModeIOPL,                        /**< 16 / 0x10 */
    ProcessEnableAlignmentFaultFixup,           /**< 17 / 0x11 */
    ProcessPriorityClass,                       /**< 18 / 0x12 */
    ProcessWx86Information,                     /**< 19 / 0x13 */
    ProcessHandleCount,                         /**< 20 / 0x14 */
    ProcessAffinityMask,                        /**< 21 / 0x15 */
    ProcessPriorityBoost,                       /**< 22 / 0x16 */
    ProcessDeviceMap,                           /**< 23 / 0x17 */
    ProcessSessionInformation,                  /**< 24 / 0x18 */
    ProcessForegroundInformation,               /**< 25 / 0x19 */
    ProcessWow64Information,                    /**< 26 / 0x1a */
    ProcessImageFileName,                       /**< 27 / 0x1b */
    ProcessLUIDDeviceMapsEnabled,               /**< 28 / 0x1c */
    ProcessBreakOnTermination,                  /**< 29 / 0x1d */
    ProcessDebugObjectHandle,                   /**< 30 / 0x1e */
    ProcessDebugFlags,                          /**< 31 / 0x1f */
    ProcessHandleTracing,                       /**< 32 / 0x20 */
    ProcessIoPriority,                          /**< 33 / 0x21 */
    ProcessExecuteFlags,                        /**< 34 / 0x22 */
    ProcessTlsInformation,                      /**< 35 / 0x23 */
    ProcessCookie,                              /**< 36 / 0x24 */
    ProcessImageInformation,                    /**< 37 / 0x25 */
    ProcessCycleTime,                           /**< 38 / 0x26 */
    ProcessPagePriority,                        /**< 39 / 0x27 */
    ProcessInstrumentationCallbak,              /**< 40 / 0x28 */
    ProcessThreadStackAllocation,               /**< 41 / 0x29 */
    ProcessWorkingSetWatchEx,                   /**< 42 / 0x2a */
    ProcessImageFileNameWin32,                  /**< 43 / 0x2b */
    ProcessImageFileMapping,                    /**< 44 / 0x2c */
    ProcessAffinityUpdateMode,                  /**< 45 / 0x2d */
    ProcessMemoryAllocationMode,                /**< 46 / 0x2e */
    ProcessGroupInformation,                    /**< 47 / 0x2f */
    ProcessTokenVirtualizationEnabled,          /**< 48 / 0x30 */
    ProcessOwnerInformation,                    /**< 49 / 0x31 */
    ProcessWindowInformation,                   /**< 50 / 0x32 */
    ProcessHandleInformation,                   /**< 51 / 0x33 */
    ProcessMitigationPolicy,                    /**< 52 / 0x34 */
    ProcessDynamicFunctionTableInformation,     /**< 53 / 0x35 */
    ProcessHandleCheckingMode,                  /**< 54 / 0x36 */
    ProcessKeepAliveCount,                      /**< 55 / 0x37 */
    ProcessRevokeFileHandles,                   /**< 56 / 0x38 */
    ProcessWorkingSetControl,                   /**< 57 / 0x39 */
    ProcessHandleTable,                         /**< 58 / 0x3a */
    ProcessCheckStackExtentsMode,               /**< 59 / 0x3b */
    ProcessCommandLineInformation,              /**< 60 / 0x3c */
    ProcessProtectionInformation,               /**< 61 / 0x3d */
    ProcessMemoryExhaustion,                    /**< 62 / 0x3e */
    ProcessFaultInformation,                    /**< 63 / 0x3f */
    ProcessTelemetryIdInformation,              /**< 64 / 0x40 */
    ProcessCommitReleaseInformation,            /**< 65 / 0x41 */
    ProcessDefaultCpuSetsInformation,           /**< 66 / 0x42 - aka ProcessReserved1Information */
    ProcessAllowedCpuSetsInformation,           /**< 67 / 0x43 - aka ProcessReserved2Information; PROCESS_SET_LIMITED_INFORMATION & audiog.exe; W10 */
    ProcessSubsystemProcess,                    /**< 68 / 0x44 */
    ProcessJobMemoryInformation,                /**< 69 / 0x45 */
    ProcessInPrivate,                           /**< 70 / 0x46 */
    ProcessRaiseUMExceptionOnInvalidHandleClose,/**< 71 / 0x47 */
    ProcessIumChallengeResponse,                /**< 72 / 0x48 */
    ProcessChildProcessInformation,             /**< 73 / 0x49 */
    ProcessHighGraphicsPriorityInformation,     /**< 74 / 0x4a */
    ProcessSubsystemInformation,                /**< 75 / 0x4b */
    ProcessEnergyValues,                        /**< 76 / 0x4c */
    ProcessPowerThrottlingState,                /**< 77 / 0x4d */
    ProcessReserved3Information,                /**< 78 / 0x4e */
    ProcessWin32kSyscallFilterInformation,      /**< 79 / 0x4f */
    ProcessDisableSystemAllowedCpuSets,         /**< 80 / 0x50 */
    ProcessWakeInformation,                     /**< 81 / 0x51 */
    ProcessEnergyTrackingState,                 /**< 82 / 0x52 */
    ProcessManageWritesToExecutableMemory,      /**< 83 / 0x53 */
    ProcessCaptureTrustletLiveDump,             /**< 84 / 0x54 */
    ProcessTelemetryCoverage,                   /**< 85 / 0x55 */
    ProcessEnclaveInformation,                  /**< 86 / 0x56 */
    ProcessEnableReadWriteVmLogging,            /**< 87 / 0x57 */
    ProcessUptimeInformation,                   /**< 88 / 0x58 */
    ProcessImageSection,                        /**< 89 / 0x59 */
    ProcessDebugAuthInformation,                /**< 90 / 0x5a */
    ProcessSystemResourceManagement,            /**< 92 / 0x5b */
    ProcessSequenceNumber,                      /**< 93 / 0x5c */
    MaxProcessInfoClass
} PROCESSINFOCLASS;
AssertCompile(ProcessSequenceNumber == 0x5c);
#endif
#if defined(IPRT_NT_USE_WINTERNL) || defined(WDK_NTDDI_VERSION) /* Present in ntddk.h from 7600.16385.1, but not in W10. */
RT_DECL_NTAPI(NTSTATUS) NtQueryInformationProcess(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
#endif
#ifdef IPRT_NT_USE_WINTERNL
#if ARCH_BITS == 32
/** 64-bit API pass thru to WOW64 processes. */
RT_DECL_NTAPI(NTSTATUS) NtWow64QueryInformationProcess64(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
#endif

typedef enum _THREADINFOCLASS
{
    ThreadBasicInformation = 0,
    ThreadTimes,
    ThreadPriority,
    ThreadBasePriority,
    ThreadAffinityMask,
    ThreadImpersonationToken,
    ThreadDescriptorTableEntry,
    ThreadEnableAlignmentFaultFixup,
    ThreadEventPair_Reusable,
    ThreadQuerySetWin32StartAddress,
    ThreadZeroTlsCell,
    ThreadPerformanceCount,
    ThreadAmILastThread,
    ThreadIdealProcessor,
    ThreadPriorityBoost,
    ThreadSetTlsArrayAddress,
    ThreadIsIoPending,
    ThreadHideFromDebugger,
    ThreadBreakOnTermination,
    ThreadSwitchLegacyState,
    ThreadIsTerminated,
    ThreadLastSystemCall,
    ThreadIoPriority,
    ThreadCycleTime,
    ThreadPagePriority,
    ThreadActualBasePriority,
    ThreadTebInformation,
    ThreadCSwitchMon,
    ThreadCSwitchPmu,
    ThreadWow64Context,
    ThreadGroupInformation,
    ThreadUmsInformation,
    ThreadCounterProfiling,
    ThreadIdealProcessorEx,
    ThreadCpuAccountingInformation,
    MaxThreadInfoClass
} THREADINFOCLASS;
RT_DECL_NTAPI(NTSTATUS) NtSetInformationThread(HANDLE, THREADINFOCLASS, LPCVOID, ULONG);

RT_DECL_NTAPI(NTSTATUS) NtQueryInformationToken(HANDLE, TOKEN_INFORMATION_CLASS, PVOID, ULONG, PULONG);
RT_DECL_NTAPI(NTSTATUS) ZwQueryInformationToken(HANDLE, TOKEN_INFORMATION_CLASS, PVOID, ULONG, PULONG);

RT_DECL_NTAPI(NTSTATUS) NtReadFile(HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID, PIO_STATUS_BLOCK, PVOID, ULONG, PLARGE_INTEGER, PULONG);
RT_DECL_NTAPI(NTSTATUS) NtWriteFile(HANDLE, HANDLE, PIO_APC_ROUTINE, void const *, PIO_STATUS_BLOCK, PVOID, ULONG, PLARGE_INTEGER, PULONG);
RT_DECL_NTAPI(NTSTATUS) NtFlushBuffersFile(HANDLE, PIO_STATUS_BLOCK);
RT_DECL_NTAPI(NTSTATUS) NtCancelIoFile(HANDLE, PIO_STATUS_BLOCK);

RT_DECL_NTAPI(NTSTATUS) NtReadVirtualMemory(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);
RT_DECL_NTAPI(NTSTATUS) NtWriteVirtualMemory(HANDLE, PVOID, void const *, SIZE_T, PSIZE_T);

RT_DECL_NTAPI(NTSTATUS) RtlAddAccessAllowedAce(PACL, ULONG, ULONG, PSID);
RT_DECL_NTAPI(NTSTATUS) RtlCopySid(ULONG, PSID, PSID);
RT_DECL_NTAPI(NTSTATUS) RtlCreateAcl(PACL, ULONG, ULONG);
RT_DECL_NTAPI(NTSTATUS) RtlCreateSecurityDescriptor(PSECURITY_DESCRIPTOR, ULONG);
RT_DECL_NTAPI(BOOLEAN)  RtlEqualSid(PSID, PSID);
RT_DECL_NTAPI(NTSTATUS) RtlGetVersion(PRTL_OSVERSIONINFOW);
RT_DECL_NTAPI(NTSTATUS) RtlInitializeSid(PSID, PSID_IDENTIFIER_AUTHORITY, UCHAR);
RT_DECL_NTAPI(NTSTATUS) RtlSetDaclSecurityDescriptor(PSECURITY_DESCRIPTOR, BOOLEAN, PACL, BOOLEAN);
RT_DECL_NTAPI(PULONG)   RtlSubAuthoritySid(PSID, ULONG);

#endif /* IPRT_NT_USE_WINTERNL */

#ifdef RTNT_NEED_NT_GET_PRODUCT_TYPE
RT_DECL_NTAPI(BOOLEAN)  RtlGetNtProductType(enum _NT_PRODUCT_TYPE *); /**< @since NT 3.1 */
#endif

/** For use with ObjectBasicInformation.
 * A watered down version of this struct appears under the name
 * PUBLIC_OBJECT_BASIC_INFORMATION in ntifs.h.  It only defines
 * the first four members, so don't trust the rest.  */
typedef struct _OBJECT_BASIC_INFORMATION
{
    ULONG Attributes;
    ACCESS_MASK GrantedAccess;
    ULONG HandleCount;
    ULONG PointerCount;
    /* Not in ntifs.h: */
    ULONG PagedPoolCharge;
    ULONG NonPagedPoolCharge;
    ULONG Reserved[3];
    ULONG NameInfoSize;
    ULONG TypeInfoSize;
    ULONG SecurityDescriptorSize;
    LARGE_INTEGER CreationTime;
} OBJECT_BASIC_INFORMATION;
typedef OBJECT_BASIC_INFORMATION *POBJECT_BASIC_INFORMATION;

/** For use with ObjectHandleFlagInformation. */
typedef struct _OBJECT_HANDLE_FLAG_INFORMATION
{
    BOOLEAN Inherit;
    BOOLEAN ProtectFromClose;
} OBJECT_HANDLE_FLAG_INFORMATION;
typedef OBJECT_HANDLE_FLAG_INFORMATION *POBJECT_HANDLE_FLAG_INFORMATION;

/**
 * Returned via ObjectTypesInformation, see also OBJECT_TYPES_INFORMATION.
 * The next structure address is calculate:
 *   (uintptr_t)Name.Buffer + RT_ALIGN_32(Name.MaximumLength, sizeof(uintptr_t))
 */
typedef struct _OBJECT_TYPE_INFORMATION
{                                           /*   64-bit offset */
    UNICODE_STRING TypeName;                /**< 0x00 */
    ULONG TotalNumberOfObjects;             /**< 0x10 */
    ULONG TotalNumberOfHandles;             /**< 0x14 */
    ULONG TotalPagedPoolUsage;              /**< 0x18 - not set by W10 19044 */
    ULONG TotalNonPagedPoolUsage;           /**< 0x1c - not set by W10 19044 */
    ULONG TotalNamePoolUsage;               /**< 0x20 - not set by W10 19044 */
    ULONG TotalHandleTableUsage;            /**< 0x24 - not set by W10 19044  */
    ULONG HighWaterNumberOfObjects;         /**< 0x28  */
    ULONG HighWaterNumberOfHandles;         /**< 0x2c  */
    ULONG HighWaterPagedPoolUsage;          /**< 0x30 - not set by W10 19044 */
    ULONG HighWaterNonPagedPoolUsage;       /**< 0x34 - not set by W10 19044 */
    ULONG HighWaterNamePoolUsage;           /**< 0x38 - not set by W10 19044 */
    ULONG HighWaterHandleTableUsage;        /**< 0x3c - not set by W10 19044  */
    ULONG InvalidAttributes;                /**< 0x40 */
    GENERIC_MAPPING GenericMapping;         /**< 0x44 */
    ULONG ValidAccessMask;                  /**< 0x54 */
    BOOLEAN SecurityRequired;               /**< 0x58 */
    BOOLEAN MaintainHandleCount;            /**< 0x59 */
    UCHAR TypeIndex;                        /**< 0x5a */
    UCHAR ReservedZero;                     /**< 0x5b */
    ULONG PoolType;                         /**< 0x5c */
    ULONG DefaultPagedPoolCharge;           /**< 0x60 - not set by W10 19044 */
    ULONG DefaultNonPagedPoolCharge;        /**< 0x64 - not set by W10 19044 */
    /* The name string follows after the structure. */
} OBJECT_TYPE_INFORMATION;
AssertCompileSize(OBJECT_TYPE_INFORMATION, sizeof(UNICODE_STRING) + 0x58);
typedef OBJECT_TYPE_INFORMATION *POBJECT_TYPE_INFORMATION;

/** Returned via ObjectTypesInformation. */
typedef struct _OBJECT_TYPES_INFORMATION
{
    ULONG NumberOfTypes;
    OBJECT_TYPE_INFORMATION FirstType;
} OBJECT_TYPES_INFORMATION;
typedef OBJECT_TYPES_INFORMATION *POBJECT_TYPES_INFORMATION;

typedef enum _OBJECT_INFORMATION_CLASS
{
    ObjectBasicInformation = 0,
    ObjectNameInformation,
    ObjectTypeInformation,
    ObjectTypesInformation,
    ObjectHandleFlagInformation,
    ObjectSessionInformation,
    MaxObjectInfoClass
} OBJECT_INFORMATION_CLASS;
typedef OBJECT_INFORMATION_CLASS *POBJECT_INFORMATION_CLASS;
#ifdef IN_RING0
# define NtQueryObject ZwQueryObject
#endif
RT_DECL_NTAPI(NTSTATUS) NtQueryObject(HANDLE, OBJECT_INFORMATION_CLASS, PVOID, ULONG, PULONG);
RT_DECL_NTAPI(NTSTATUS) NtSetInformationObject(HANDLE, OBJECT_INFORMATION_CLASS, PVOID, ULONG);
RT_DECL_NTAPI(NTSTATUS) NtDuplicateObject(HANDLE, HANDLE, HANDLE, PHANDLE, ACCESS_MASK, ULONG, ULONG);

RT_DECL_NTAPI(NTSTATUS) NtOpenDirectoryObject(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);

typedef struct _OBJECT_DIRECTORY_INFORMATION
{
    UNICODE_STRING Name;
    UNICODE_STRING TypeName;
} OBJECT_DIRECTORY_INFORMATION;
typedef OBJECT_DIRECTORY_INFORMATION *POBJECT_DIRECTORY_INFORMATION;
RT_DECL_NTAPI(NTSTATUS) NtQueryDirectoryObject(HANDLE, PVOID, ULONG, BOOLEAN, BOOLEAN, PULONG, PULONG);

RT_DECL_NTAPI(NTSTATUS) NtSuspendProcess(HANDLE);
RT_DECL_NTAPI(NTSTATUS) NtResumeProcess(HANDLE);
/** @name ProcessDefaultHardErrorMode bit definitions.
 * @{ */
#define PROCESS_HARDERR_CRITICAL_ERROR              UINT32_C(0x00000001) /**< Inverted from the win32 define. */
#define PROCESS_HARDERR_NO_GP_FAULT_ERROR           UINT32_C(0x00000002)
#define PROCESS_HARDERR_NO_ALIGNMENT_FAULT_ERROR    UINT32_C(0x00000004)
#define PROCESS_HARDERR_NO_OPEN_FILE_ERROR          UINT32_C(0x00008000)
/** @} */
RT_DECL_NTAPI(NTSTATUS) NtSetInformationProcess(HANDLE, PROCESSINFOCLASS, PVOID, ULONG);
RT_DECL_NTAPI(NTSTATUS) NtTerminateProcess(HANDLE, LONG);

/** Returned by NtQUerySection with SectionBasicInformation. */
typedef struct _SECTION_BASIC_INFORMATION
{
    PVOID            BaseAddress;
    ULONG            AllocationAttributes;
    LARGE_INTEGER    MaximumSize;
} SECTION_BASIC_INFORMATION;
typedef SECTION_BASIC_INFORMATION *PSECTION_BASIC_INFORMATION;

/** Retured by ProcessImageInformation as well as NtQuerySection. */
typedef struct _SECTION_IMAGE_INFORMATION
{
    PVOID TransferAddress;
    ULONG ZeroBits;
    SIZE_T MaximumStackSize;
    SIZE_T CommittedStackSize;
    ULONG SubSystemType;
    union
    {
        struct
        {
            USHORT SubSystemMinorVersion;
            USHORT SubSystemMajorVersion;
        };
        ULONG SubSystemVersion;
    };
    ULONG GpValue;
    USHORT ImageCharacteristics;
    USHORT DllCharacteristics;
    USHORT Machine;
    BOOLEAN ImageContainsCode;
    union /**< Since Vista, used to be a spare BOOLEAN. */
    {
        struct
        {
            UCHAR ComPlusNativeRead : 1;
            UCHAR ComPlusILOnly : 1;
            UCHAR ImageDynamicallyRelocated : 1;
            UCHAR ImageMAppedFlat : 1;
            UCHAR Reserved : 4;
        };
        UCHAR ImageFlags;
    };
    ULONG LoaderFlags;
    ULONG ImageFileSize; /**< Since XP? */
    ULONG CheckSum; /**< Since Vista, Used to be a reserved/spare ULONG. */
} SECTION_IMAGE_INFORMATION;
typedef SECTION_IMAGE_INFORMATION *PSECTION_IMAGE_INFORMATION;

typedef enum _SECTION_INFORMATION_CLASS
{
    SectionBasicInformation = 0,
    SectionImageInformation,
    MaxSectionInfoClass
} SECTION_INFORMATION_CLASS;
RT_DECL_NTAPI(NTSTATUS) NtQuerySection(HANDLE, SECTION_INFORMATION_CLASS, PVOID, SIZE_T, PSIZE_T);

RT_DECL_NTAPI(NTSTATUS) NtCreateSymbolicLinkObject(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PUNICODE_STRING pTarget);
RT_DECL_NTAPI(NTSTATUS) NtOpenSymbolicLinkObject(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);
RT_DECL_NTAPI(NTSTATUS) NtQuerySymbolicLinkObject(HANDLE, PUNICODE_STRING, PULONG);
#ifndef SYMBOLIC_LINK_QUERY
# define SYMBOLIC_LINK_QUERY        UINT32_C(0x00000001)
#endif
#ifndef SYMBOLIC_LINK_ALL_ACCESS
# define SYMBOLIC_LINK_ALL_ACCESS   (STANDARD_RIGHTS_REQUIRED | SYMBOLIC_LINK_QUERY)
#endif

RT_DECL_NTAPI(NTSTATUS) NtQueryInformationThread(HANDLE, THREADINFOCLASS, PVOID, ULONG, PULONG);
RT_DECL_NTAPI(NTSTATUS) NtResumeThread(HANDLE, PULONG);
RT_DECL_NTAPI(NTSTATUS) NtSuspendThread(HANDLE, PULONG);
RT_DECL_NTAPI(NTSTATUS) NtTerminateThread(HANDLE, LONG);
RT_DECL_NTAPI(NTSTATUS) NtGetContextThread(HANDLE, PCONTEXT);
RT_DECL_NTAPI(NTSTATUS) NtSetContextThread(HANDLE, PCONTEXT);
RT_DECL_NTAPI(NTSTATUS) ZwYieldExecution(void);


#ifndef SEC_FILE
# define SEC_FILE               UINT32_C(0x00800000)
#endif
#ifndef SEC_IMAGE
# define SEC_IMAGE              UINT32_C(0x01000000)
#endif
#ifndef SEC_PROTECTED_IMAGE
# define SEC_PROTECTED_IMAGE    UINT32_C(0x02000000)
#endif
#ifndef SEC_NOCACHE
# define SEC_NOCACHE            UINT32_C(0x10000000)
#endif
#ifndef MEM_ROTATE
# define MEM_ROTATE             UINT32_C(0x00800000)
#endif
typedef enum _MEMORY_INFORMATION_CLASS
{
    MemoryBasicInformation = 0,
    MemoryWorkingSetList,
    MemorySectionName,
    MemoryBasicVlmInformation
} MEMORY_INFORMATION_CLASS;
#ifndef IPRT_NT_USE_WINTERNL
# ifndef WDK_NTDDI_VERSION /* W10 ntifs.h has it, 7600.16385.1 didn't. */
typedef struct _MEMORY_BASIC_INFORMATION
{
    PVOID BaseAddress;
    PVOID AllocationBase;
    ULONG AllocationProtect;
#  if ARCH_BITS == 64
    USHORT PartitionId;
#  endif
    SIZE_T RegionSize;
    ULONG State;
    ULONG Protect;
    ULONG Type;
} MEMORY_BASIC_INFORMATION;
typedef MEMORY_BASIC_INFORMATION *PMEMORY_BASIC_INFORMATION;
# endif
# define NtQueryVirtualMemory ZwQueryVirtualMemory
#endif
#if defined(IPRT_NT_USE_WINTERNL) || !defined(WDK_NTDDI_VERSION) /* W10 ntifs.h has it, 7600.16385.1 didn't. */
RT_DECL_NTAPI(NTSTATUS) NtQueryVirtualMemory(HANDLE, void const *, MEMORY_INFORMATION_CLASS, PVOID, SIZE_T, PSIZE_T);
#endif
#ifdef IPRT_NT_USE_WINTERNL
RT_DECL_NTAPI(NTSTATUS) NtAllocateVirtualMemory(HANDLE, PVOID *, ULONG, PSIZE_T, ULONG, ULONG);
RT_DECL_NTAPI(NTSTATUS) NtFreeVirtualMemory(HANDLE, PVOID *, PSIZE_T, ULONG);
#endif
RT_DECL_NTAPI(NTSTATUS) NtProtectVirtualMemory(HANDLE, PVOID *, PSIZE_T, ULONG, PULONG);

typedef enum _SYSTEM_INFORMATION_CLASS
{
    SystemBasicInformation = 0,
    SystemCpuInformation,
    SystemPerformanceInformation,
    SystemTimeOfDayInformation,
    SystemInformation_Unknown_4,
    SystemProcessInformation,
    SystemInformation_Unknown_6,
    SystemInformation_Unknown_7,
    SystemProcessorPerformanceInformation,
    SystemInformation_Unknown_9,
    SystemInformation_Unknown_10,
    SystemModuleInformation,
    SystemInformation_Unknown_12,
    SystemInformation_Unknown_13,
    SystemInformation_Unknown_14,
    SystemInformation_Unknown_15,
    SystemHandleInformation,
    SystemInformation_Unknown_17,
    SystemPageFileInformation,
    SystemInformation_Unknown_19,
    SystemInformation_Unknown_20,
    SystemCacheInformation,
    SystemInformation_Unknown_22,
    SystemInterruptInformation,
    SystemDpcBehaviourInformation,
    SystemFullMemoryInformation,
    SystemLoadGdiDriverInformation, /* 26 */
    SystemUnloadGdiDriverInformation, /* 27 */
    SystemTimeAdjustmentInformation,
    SystemSummaryMemoryInformation,
    SystemInformation_Unknown_30,
    SystemInformation_Unknown_31,
    SystemInformation_Unknown_32,
    SystemExceptionInformation,
    SystemCrashDumpStateInformation,
    SystemKernelDebuggerInformation,
    SystemContextSwitchInformation,
    SystemRegistryQuotaInformation,
    SystemInformation_Unknown_38,
    SystemInformation_Unknown_39,
    SystemInformation_Unknown_40,
    SystemInformation_Unknown_41,
    SystemInformation_Unknown_42,
    SystemInformation_Unknown_43,
    SystemCurrentTimeZoneInformation,
    SystemLookasideInformation,
    SystemSetTimeSlipEvent,
    SystemCreateSession,
    SystemDeleteSession,
    SystemInformation_Unknown_49,
    SystemRangeStartInformation,
    SystemVerifierInformation,
    SystemInformation_Unknown_52,
    SystemSessionProcessInformation,
    SystemLoadGdiDriverInSystemSpaceInformation, /* 54 */
    SystemInformation_Unknown_55,
    SystemInformation_Unknown_56,
    SystemExtendedProcessInformation,
    SystemInformation_Unknown_58,
    SystemInformation_Unknown_59,
    SystemInformation_Unknown_60,
    SystemInformation_Unknown_61,
    SystemInformation_Unknown_62,
    SystemInformation_Unknown_63,
    SystemExtendedHandleInformation, /* 64 */
    SystemInformation_Unknown_65,
    SystemInformation_Unknown_66,
    SystemInformation_Unknown_67, /**< See https://www.geoffchappell.com/studies/windows/km/ntoskrnl/api/ex/sysinfo/codeintegrity.htm */
    SystemInformation_Unknown_68,
    SystemInformation_HotPatchInfo, /* 69 */
    SystemInformation_Unknown_70,
    SystemInformation_Unknown_71,
    SystemInformation_Unknown_72,
    SystemInformation_Unknown_73,
    SystemInformation_Unknown_74,
    SystemInformation_Unknown_75,
    SystemInformation_Unknown_76,
    SystemInformation_Unknown_77,
    SystemInformation_Unknown_78,
    SystemInformation_Unknown_79,
    SystemInformation_Unknown_80,
    SystemInformation_Unknown_81,
    SystemInformation_Unknown_82,
    SystemInformation_Unknown_83,
    SystemInformation_Unknown_84,
    SystemInformation_Unknown_85,
    SystemInformation_Unknown_86,
    SystemInformation_Unknown_87,
    SystemInformation_Unknown_88,
    SystemInformation_Unknown_89,
    SystemInformation_Unknown_90,
    SystemInformation_Unknown_91,
    SystemInformation_Unknown_92,
    SystemInformation_Unknown_93,
    SystemInformation_Unknown_94,
    SystemInformation_Unknown_95,
    SystemInformation_KiOpPrefetchPatchCount, /* 96 */
    SystemInformation_Unknown_97,
    SystemInformation_Unknown_98,
    SystemInformation_Unknown_99,
    SystemInformation_Unknown_100,
    SystemInformation_Unknown_101,
    SystemInformation_Unknown_102,
    SystemInformation_Unknown_103,
    SystemInformation_Unknown_104,
    SystemInformation_Unknown_105,
    SystemInformation_Unknown_107,
    SystemInformation_GetLogicalProcessorInformationEx, /* 107 */

    /** @todo fill gap. they've added a whole bunch of things  */
    SystemPolicyInformation = 134,
    SystemInformationClassMax
} SYSTEM_INFORMATION_CLASS;

#ifdef IPRT_NT_USE_WINTERNL
typedef struct _VM_COUNTERS
{
    SIZE_T PeakVirtualSize;
    SIZE_T VirtualSize;
    ULONG PageFaultCount;
    SIZE_T PeakWorkingSetSize;
    SIZE_T WorkingSetSize;
    SIZE_T QuotaPeakPagedPoolUsage;
    SIZE_T QuotaPagedPoolUsage;
    SIZE_T QuotaPeakNonPagedPoolUsage;
    SIZE_T QuotaNonPagedPoolUsage;
    SIZE_T PagefileUsage;
    SIZE_T PeakPagefileUsage;
} VM_COUNTERS;
typedef VM_COUNTERS *PVM_COUNTERS;
#endif

#if 0
typedef struct _IO_COUNTERS
{
    ULONGLONG ReadOperationCount;
    ULONGLONG WriteOperationCount;
    ULONGLONG OtherOperationCount;
    ULONGLONG ReadTransferCount;
    ULONGLONG WriteTransferCount;
    ULONGLONG OtherTransferCount;
} IO_COUNTERS;
typedef IO_COUNTERS *PIO_COUNTERS;
#endif

typedef struct _RTNT_SYSTEM_PROCESS_INFORMATION
{
    ULONG NextEntryOffset;          /**< 0x00 / 0x00 */
    ULONG NumberOfThreads;          /**< 0x04 / 0x04 */
    LARGE_INTEGER Reserved1[3];     /**< 0x08 / 0x08 */
    LARGE_INTEGER CreationTime;     /**< 0x20 / 0x20 */
    LARGE_INTEGER UserTime;         /**< 0x28 / 0x28 */
    LARGE_INTEGER KernelTime;       /**< 0x30 / 0x30 */
    UNICODE_STRING ProcessName;     /**< 0x38 / 0x38 Clean unicode encoding? */
    int32_t BasePriority;           /**< 0x40 / 0x48 */
    HANDLE UniqueProcessId;         /**< 0x44 / 0x50 */
    HANDLE ParentProcessId;         /**< 0x48 / 0x58 */
    ULONG HandleCount;              /**< 0x4c / 0x60 */
    ULONG Reserved2;                /**< 0x50 / 0x64 Session ID? */
    ULONG_PTR Reserved3;            /**< 0x54 / 0x68 */
    VM_COUNTERS VmCounters;         /**< 0x58 / 0x70 */
    IO_COUNTERS IoCounters;         /**< 0x88 / 0xd0 Might not be present in earlier windows versions. */
    /* After this follows the threads, then the ProcessName.Buffer. */
} RTNT_SYSTEM_PROCESS_INFORMATION;
typedef RTNT_SYSTEM_PROCESS_INFORMATION *PRTNT_SYSTEM_PROCESS_INFORMATION;
#ifndef IPRT_NT_USE_WINTERNL
typedef RTNT_SYSTEM_PROCESS_INFORMATION SYSTEM_PROCESS_INFORMATION;
typedef SYSTEM_PROCESS_INFORMATION *PSYSTEM_PROCESS_INFORMATION;
#endif

typedef struct _SYSTEM_HANDLE_ENTRY_INFO
{
    USHORT UniqueProcessId;
    USHORT CreatorBackTraceIndex;
    UCHAR ObjectTypeIndex;
    UCHAR HandleAttributes;
    USHORT HandleValue;
    PVOID Object;
    ULONG GrantedAccess;
} SYSTEM_HANDLE_ENTRY_INFO;
typedef SYSTEM_HANDLE_ENTRY_INFO *PSYSTEM_HANDLE_ENTRY_INFO;

/** Returned by SystemHandleInformation  */
typedef struct _SYSTEM_HANDLE_INFORMATION
{
    ULONG NumberOfHandles;
    SYSTEM_HANDLE_ENTRY_INFO Handles[1];
} SYSTEM_HANDLE_INFORMATION;
typedef SYSTEM_HANDLE_INFORMATION *PSYSTEM_HANDLE_INFORMATION;

/** Extended handle information entry.
 * @remarks 3 x PVOID + 4 x ULONG = 28 bytes on 32-bit / 40 bytes on 64-bit  */
typedef struct _SYSTEM_HANDLE_ENTRY_INFO_EX
{
    PVOID Object;
    HANDLE UniqueProcessId;
    HANDLE HandleValue;
    ACCESS_MASK GrantedAccess;
    USHORT CreatorBackTraceIndex;
    USHORT ObjectTypeIndex;
    ULONG HandleAttributes;
    ULONG Reserved;
} SYSTEM_HANDLE_ENTRY_INFO_EX;
typedef SYSTEM_HANDLE_ENTRY_INFO_EX *PSYSTEM_HANDLE_ENTRY_INFO_EX;

/** Returned by SystemExtendedHandleInformation.  */
typedef struct _SYSTEM_HANDLE_INFORMATION_EX
{
    ULONG_PTR NumberOfHandles;
    ULONG_PTR Reserved;
    SYSTEM_HANDLE_ENTRY_INFO_EX Handles[1];
} SYSTEM_HANDLE_INFORMATION_EX;
typedef SYSTEM_HANDLE_INFORMATION_EX *PSYSTEM_HANDLE_INFORMATION_EX;

/** Returned by SystemSessionProcessInformation. */
typedef struct _SYSTEM_SESSION_PROCESS_INFORMATION
{
    ULONG SessionId;
    ULONG BufferLength;
    /** Return buffer, SYSTEM_PROCESS_INFORMATION entries. */
    PVOID Buffer;
} SYSTEM_SESSION_PROCESS_INFORMATION;
typedef SYSTEM_SESSION_PROCESS_INFORMATION *PSYSTEM_SESSION_PROCESS_INFORMATION;

typedef struct _RTL_PROCESS_MODULE_INFORMATION
{
    HANDLE Section;                 /**< 0x00 / 0x00 */
    PVOID MappedBase;               /**< 0x04 / 0x08 */
    PVOID ImageBase;                /**< 0x08 / 0x10 */
    ULONG ImageSize;                /**< 0x0c / 0x18 */
    ULONG Flags;                    /**< 0x10 / 0x1c */
    USHORT LoadOrderIndex;          /**< 0x14 / 0x20 */
    USHORT InitOrderIndex;          /**< 0x16 / 0x22 */
    USHORT LoadCount;               /**< 0x18 / 0x24 */
    USHORT OffsetToFileName;        /**< 0x1a / 0x26 */
    UCHAR  FullPathName[256];       /**< 0x1c / 0x28 */
} RTL_PROCESS_MODULE_INFORMATION;
typedef RTL_PROCESS_MODULE_INFORMATION *PRTL_PROCESS_MODULE_INFORMATION;

/** Returned by SystemModuleInformation. */
typedef struct _RTL_PROCESS_MODULES
{
    ULONG NumberOfModules;
    RTL_PROCESS_MODULE_INFORMATION Modules[1];  /**< 0x04 / 0x08 */
} RTL_PROCESS_MODULES;
typedef RTL_PROCESS_MODULES *PRTL_PROCESS_MODULES;

RT_DECL_NTAPI(NTSTATUS) NtQuerySystemInformation(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);
#ifndef IPRT_NT_MAP_TO_ZW
RT_DECL_NTAPI(NTSTATUS) ZwQuerySystemInformation(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);
#endif

RT_DECL_NTAPI(NTSTATUS) NtSetTimerResolution(ULONG cNtTicksWanted, BOOLEAN fSetResolution, PULONG pcNtTicksCur);
RT_DECL_NTAPI(NTSTATUS) NtQueryTimerResolution(PULONG pcNtTicksMin, PULONG pcNtTicksMax, PULONG pcNtTicksCur);

RT_DECL_NTAPI(NTSTATUS) NtDelayExecution(BOOLEAN, PLARGE_INTEGER);
RT_DECL_NTAPI(NTSTATUS) NtYieldExecution(void);
#ifndef IPRT_NT_USE_WINTERNL
RT_DECL_NTAPI(NTSTATUS) NtWaitForSingleObject(HANDLE, BOOLEAN, PLARGE_INTEGER);
#endif
typedef NTSYSAPI NTSTATUS (NTAPI *PFNNTWAITFORSINGLEOBJECT)(HANDLE, BOOLEAN, PLARGE_INTEGER);
typedef enum _OBJECT_WAIT_TYPE { WaitAllObjects = 0, WaitAnyObject = 1, ObjectWaitTypeHack = 0x7fffffff } OBJECT_WAIT_TYPE;
RT_DECL_NTAPI(NTSTATUS) NtWaitForMultipleObjects(ULONG, PHANDLE, OBJECT_WAIT_TYPE, BOOLEAN, PLARGE_INTEGER);

#ifdef IPRT_NT_USE_WINTERNL
RT_DECL_NTAPI(NTSTATUS) NtQuerySecurityObject(HANDLE, ULONG, PSECURITY_DESCRIPTOR, ULONG, PULONG);
#endif

#ifdef IPRT_NT_USE_WINTERNL
typedef enum _EVENT_TYPE
{
    /* Manual reset event. */
    NotificationEvent = 0,
    /* Automaitc reset event. */
    SynchronizationEvent
} EVENT_TYPE;
#endif
RT_DECL_NTAPI(NTSTATUS) NtCreateEvent(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, EVENT_TYPE, BOOLEAN);
RT_DECL_NTAPI(NTSTATUS) NtOpenEvent(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);
typedef NTSYSAPI NTSTATUS (NTAPI *PFNNTCLEAREVENT)(HANDLE);
RT_DECL_NTAPI(NTSTATUS) NtClearEvent(HANDLE);
RT_DECL_NTAPI(NTSTATUS) NtResetEvent(HANDLE, PULONG);
RT_DECL_NTAPI(NTSTATUS) NtSetEvent(HANDLE, PULONG);
typedef NTSYSAPI NTSTATUS (NTAPI *PFNNTSETEVENT)(HANDLE, PULONG);
typedef enum _EVENT_INFORMATION_CLASS
{
    EventBasicInformation = 0
} EVENT_INFORMATION_CLASS;
/** Data returned by NtQueryEvent + EventBasicInformation. */
typedef struct EVENT_BASIC_INFORMATION
{
    EVENT_TYPE  EventType;
    ULONG       EventState;
} EVENT_BASIC_INFORMATION;
typedef EVENT_BASIC_INFORMATION *PEVENT_BASIC_INFORMATION;
RT_DECL_NTAPI(NTSTATUS) NtQueryEvent(HANDLE, EVENT_INFORMATION_CLASS, PVOID, ULONG, PULONG);

#ifdef IPRT_NT_USE_WINTERNL
/** For NtQueryValueKey. */
typedef enum _KEY_VALUE_INFORMATION_CLASS
{
    KeyValueBasicInformation = 0,
    KeyValueFullInformation,
    KeyValuePartialInformation,
    KeyValueFullInformationAlign64,
    KeyValuePartialInformationAlign64
} KEY_VALUE_INFORMATION_CLASS;

/** KeyValuePartialInformation and KeyValuePartialInformationAlign64 struct. */
typedef struct _KEY_VALUE_PARTIAL_INFORMATION
{
    ULONG TitleIndex;
    ULONG Type;
    ULONG DataLength;
    UCHAR Data[1];
} KEY_VALUE_PARTIAL_INFORMATION;
typedef KEY_VALUE_PARTIAL_INFORMATION *PKEY_VALUE_PARTIAL_INFORMATION;
#endif
RT_DECL_NTAPI(NTSTATUS) NtOpenKey(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);
RT_DECL_NTAPI(NTSTATUS) NtQueryValueKey(HANDLE, PUNICODE_STRING, KEY_VALUE_INFORMATION_CLASS, PVOID, ULONG, PULONG);


RT_DECL_NTAPI(NTSTATUS) RtlAddAccessDeniedAce(PACL, ULONG, ULONG, PSID);


typedef struct _CURDIR
{
    UNICODE_STRING  DosPath;
    HANDLE          Handle;     /**< 0x10 / 0x08 */
} CURDIR;
AssertCompileSize(CURDIR, ARCH_BITS == 32 ? 0x0c : 0x18);
typedef CURDIR *PCURDIR;

typedef struct _RTL_DRIVE_LETTER_CURDIR
{
    USHORT          Flags;
    USHORT          Length;
    ULONG           TimeStamp;
    STRING          DosPath; /**< Yeah, it's STRING according to dt ntdll!_RTL_DRIVE_LETTER_CURDIR. */
} RTL_DRIVE_LETTER_CURDIR;
typedef RTL_DRIVE_LETTER_CURDIR *PRTL_DRIVE_LETTER_CURDIR;

typedef struct _RTL_USER_PROCESS_PARAMETERS
{
    ULONG           MaximumLength;                      /**< 0x000 / 0x000 */
    ULONG           Length;                             /**< 0x004 / 0x004 */
    ULONG           Flags;                              /**< 0x008 / 0x008 */
    ULONG           DebugFlags;                         /**< 0x00c / 0x00c */
    HANDLE          ConsoleHandle;                      /**< 0x010 / 0x010 */
    ULONG           ConsoleFlags;                       /**< 0x018 / 0x014 */
    HANDLE          StandardInput;                      /**< 0x020 / 0x018 */
    HANDLE          StandardOutput;                     /**< 0x028 / 0x01c */
    HANDLE          StandardError;                      /**< 0x030 / 0x020 */
    CURDIR          CurrentDirectory;                   /**< 0x038 / 0x024 */
    UNICODE_STRING  DllPath;                            /**< 0x050 / 0x030 */
    UNICODE_STRING  ImagePathName;                      /**< 0x060 / 0x038 */
    UNICODE_STRING  CommandLine;                        /**< 0x070 / 0x040 */
    PWSTR           Environment;                        /**< 0x080 / 0x048 */
    ULONG           StartingX;                          /**< 0x088 / 0x04c */
    ULONG           StartingY;                          /**< 0x090 / 0x050 */
    ULONG           CountX;                             /**< 0x094 / 0x054 */
    ULONG           CountY;                             /**< 0x098 / 0x058 */
    ULONG           CountCharsX;                        /**< 0x09c / 0x05c */
    ULONG           CountCharsY;                        /**< 0x0a0 / 0x060 */
    ULONG           FillAttribute;                      /**< 0x0a4 / 0x064 */
    ULONG           WindowFlags;                        /**< 0x0a8 / 0x068 */
    ULONG           ShowWindowFlags;                    /**< 0x0ac / 0x06c */
    UNICODE_STRING  WindowTitle;                        /**< 0x0b0 / 0x070 */
    UNICODE_STRING  DesktopInfo;                        /**< 0x0c0 / 0x078 */
    UNICODE_STRING  ShellInfo;                          /**< 0x0d0 / 0x080 */
    UNICODE_STRING  RuntimeInfo;                        /**< 0x0e0 / 0x088 */
    RTL_DRIVE_LETTER_CURDIR  CurrentDirectories[0x20];  /**< 0x0f0 / 0x090 */
    SIZE_T          EnvironmentSize;                    /**< 0x3f0 / 0x - Added in Vista */
    SIZE_T          EnvironmentVersion;                 /**< 0x3f8 / 0x - Added in Windows 7. */
    PVOID           PackageDependencyData;              /**< 0x400 / 0x - Added Windows 8? */
    ULONG           ProcessGroupId;                     /**< 0x408 / 0x - Added Windows 8? */
    ULONG           LoaderThreads;                      /**< 0x40c / 0x - Added Windows 10? */
} RTL_USER_PROCESS_PARAMETERS;
typedef RTL_USER_PROCESS_PARAMETERS *PRTL_USER_PROCESS_PARAMETERS;
#define RTL_USER_PROCESS_PARAMS_FLAG_NORMALIZED     1

typedef struct _RTL_USER_PROCESS_INFORMATION
{
    ULONG           Size;
    HANDLE          ProcessHandle;
    HANDLE          ThreadHandle;
    CLIENT_ID       ClientId;
    SECTION_IMAGE_INFORMATION  ImageInformation;
} RTL_USER_PROCESS_INFORMATION;
typedef RTL_USER_PROCESS_INFORMATION *PRTL_USER_PROCESS_INFORMATION;


RT_DECL_NTAPI(NTSTATUS) RtlCreateUserProcess(PUNICODE_STRING, ULONG, PRTL_USER_PROCESS_PARAMETERS, PSECURITY_DESCRIPTOR,
                                             PSECURITY_DESCRIPTOR, HANDLE, BOOLEAN, HANDLE, HANDLE, PRTL_USER_PROCESS_INFORMATION);
RT_DECL_NTAPI(NTSTATUS) RtlCreateProcessParameters(PRTL_USER_PROCESS_PARAMETERS *, PUNICODE_STRING ImagePathName,
                                                   PUNICODE_STRING DllPath, PUNICODE_STRING CurrentDirectory,
                                                   PUNICODE_STRING CommandLine, PUNICODE_STRING Environment,
                                                   PUNICODE_STRING WindowTitle, PUNICODE_STRING DesktopInfo,
                                                   PUNICODE_STRING ShellInfo, PUNICODE_STRING RuntimeInfo);
RT_DECL_NTAPI(VOID)     RtlDestroyProcessParameters(PRTL_USER_PROCESS_PARAMETERS);
RT_DECL_NTAPI(NTSTATUS) RtlCreateUserThread(HANDLE, PSECURITY_DESCRIPTOR, BOOLEAN, ULONG, SIZE_T, SIZE_T,
                                            PFNRT, PVOID, PHANDLE, PCLIENT_ID);

#ifndef RTL_CRITICAL_SECTION_FLAG_NO_DEBUG_INFO
typedef struct _RTL_CRITICAL_SECTION
{
    struct _RTL_CRITICAL_SECTION_DEBUG *DebugInfo;
    LONG            LockCount;
    LONG            Recursioncount;
    HANDLE          OwningThread;
    HANDLE          LockSemaphore;
    ULONG_PTR       SpinCount;
} RTL_CRITICAL_SECTION;
typedef RTL_CRITICAL_SECTION *PRTL_CRITICAL_SECTION;
#endif

/*RT_DECL_NTAPI(ULONG) RtlNtStatusToDosError(NTSTATUS rcNt);*/

/** @def RTL_QUERY_REGISTRY_TYPECHECK
 * WDK 8.1+, backported in updates, ignored in older. */
#if !defined(RTL_QUERY_REGISTRY_TYPECHECK) || defined(DOXYGEN_RUNNING)
# define RTL_QUERY_REGISTRY_TYPECHECK       UINT32_C(0x00000100)
#endif
/** @def RTL_QUERY_REGISTRY_TYPECHECK_SHIFT
 * WDK 8.1+, backported in updates, ignored in older. */
#if !defined(RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) || defined(DOXYGEN_RUNNING)
# define RTL_QUERY_REGISTRY_TYPECHECK_SHIFT 24
#endif

RT_DECL_NTAPI(VOID)         RtlFreeUnicodeString(PUNICODE_STRING);

RT_C_DECLS_END
/** @} */


#if defined(IN_RING0) || defined(DOXYGEN_RUNNING)
/** @name NT Kernel APIs
 * @{ */
RT_C_DECLS_BEGIN

typedef ULONG KEPROCESSORINDEX; /**< Bitmap indexes != process numbers, apparently. */

RT_DECL_NTAPI(VOID)          KeInitializeAffinityEx(PKAFFINITY_EX pAffinity);
typedef  VOID    (NTAPI *PFNKEINITIALIZEAFFINITYEX)(PKAFFINITY_EX pAffinity);
RT_DECL_NTAPI(VOID)          KeAddProcessorAffinityEx(PKAFFINITY_EX pAffinity, KEPROCESSORINDEX idxProcessor);
typedef  VOID    (NTAPI *PFNKEADDPROCESSORAFFINITYEX)(PKAFFINITY_EX pAffinity, KEPROCESSORINDEX idxProcessor);
RT_DECL_NTAPI(VOID)          KeRemoveProcessorAffinityEx(PKAFFINITY_EX pAffinity, KEPROCESSORINDEX idxProcessor);
typedef  VOID    (NTAPI *PFNKEREMOVEPROCESSORAFFINITYEX)(PKAFFINITY_EX pAffinity, KEPROCESSORINDEX idxProcessor);
RT_DECL_NTAPI(BOOLEAN)       KeInterlockedSetProcessorAffinityEx(PKAFFINITY_EX pAffinity, KEPROCESSORINDEX idxProcessor);
typedef  BOOLEAN (NTAPI *PFNKEINTERLOCKEDSETPROCESSORAFFINITYEX)(PKAFFINITY_EX pAffinity, KEPROCESSORINDEX idxProcessor);
RT_DECL_NTAPI(BOOLEAN)       KeInterlockedClearProcessorAffinityEx(PKAFFINITY_EX pAffinity, KEPROCESSORINDEX idxProcessor);
typedef  BOOLEAN (NTAPI *PFNKEINTERLOCKEDCLEARPROCESSORAFFINITYEX)(PKAFFINITY_EX pAffinity, KEPROCESSORINDEX idxProcessor);
RT_DECL_NTAPI(BOOLEAN)       KeCheckProcessorAffinityEx(PCKAFFINITY_EX pAffinity, KEPROCESSORINDEX idxProcessor);
typedef  BOOLEAN (NTAPI *PFNKECHECKPROCESSORAFFINITYEX)(PCKAFFINITY_EX pAffinity, KEPROCESSORINDEX idxProcessor);
RT_DECL_NTAPI(VOID)          KeCopyAffinityEx(PKAFFINITY_EX pDst, PCKAFFINITY_EX pSrc);
typedef  VOID    (NTAPI *PFNKECOPYAFFINITYEX)(PKAFFINITY_EX pDst, PCKAFFINITY_EX pSrc);
RT_DECL_NTAPI(VOID)          KeComplementAffinityEx(PKAFFINITY_EX pResult, PCKAFFINITY_EX pIn);
typedef  VOID    (NTAPI *PFNKECOMPLEMENTAFFINITYEX)(PKAFFINITY_EX pResult, PCKAFFINITY_EX pIn);
RT_DECL_NTAPI(BOOLEAN)       KeAndAffinityEx(PCKAFFINITY_EX pIn1, PCKAFFINITY_EX pIn2, PKAFFINITY_EX pResult OPTIONAL);
typedef  BOOLEAN (NTAPI *PFNKEANDAFFINITYEX)(PCKAFFINITY_EX pIn1, PCKAFFINITY_EX pIn2, PKAFFINITY_EX pResult OPTIONAL);
RT_DECL_NTAPI(BOOLEAN)       KeOrAffinityEx(PCKAFFINITY_EX pIn1, PCKAFFINITY_EX pIn2, PKAFFINITY_EX pResult OPTIONAL);
typedef  BOOLEAN (NTAPI *PFNKEORAFFINITYEX)(PCKAFFINITY_EX pIn1, PCKAFFINITY_EX pIn2, PKAFFINITY_EX pResult OPTIONAL);
/** Works like anding the complemented subtrahend with the minuend. */
RT_DECL_NTAPI(BOOLEAN)       KeSubtractAffinityEx(PCKAFFINITY_EX pMinuend, PCKAFFINITY_EX pSubtrahend, PKAFFINITY_EX pResult OPTIONAL);
typedef  BOOLEAN (NTAPI *PFNKESUBTRACTAFFINITYEX)(PCKAFFINITY_EX pMinuend, PCKAFFINITY_EX pSubtrahend, PKAFFINITY_EX pResult OPTIONAL);
RT_DECL_NTAPI(BOOLEAN)       KeIsEqualAffinityEx(PCKAFFINITY_EX pLeft, PCKAFFINITY_EX pRight);
typedef  BOOLEAN (NTAPI *PFNKEISEQUALAFFINITYEX)(PCKAFFINITY_EX pLeft, PCKAFFINITY_EX pRight);
RT_DECL_NTAPI(BOOLEAN)       KeIsEmptyAffinityEx(PCKAFFINITY_EX pAffinity);
typedef  BOOLEAN (NTAPI *PFNKEISEMPTYAFFINITYEX)(PCKAFFINITY_EX pAffinity);
RT_DECL_NTAPI(BOOLEAN)       KeIsSubsetAffinityEx(PCKAFFINITY_EX pSubset, PCKAFFINITY_EX pSuperSet);
typedef  BOOLEAN (NTAPI *PFNKEISSUBSETAFFINITYEX)(PCKAFFINITY_EX pSubset, PCKAFFINITY_EX pSuperSet);
RT_DECL_NTAPI(ULONG)     KeCountSetBitsAffinityEx(PCKAFFINITY_EX pAffinity);
typedef  ULONG   (NTAPI *PFNKECOUNTSETAFFINITYEX)(PCKAFFINITY_EX pAffinity);
RT_DECL_NTAPI(KEPROCESSORINDEX)       KeFindFirstSetLeftAffinityEx(PCKAFFINITY_EX pAffinity);
typedef  KEPROCESSORINDEX (NTAPI *PFNKEFINDFIRSTSETLEFTAFFINITYEX)(PCKAFFINITY_EX pAffinity);
typedef  NTSTATUS (NTAPI *PFNKEGETPROCESSORNUMBERFROMINDEX)(KEPROCESSORINDEX idxProcessor, PPROCESSOR_NUMBER pProcNumber);
typedef  KEPROCESSORINDEX (NTAPI *PFNKEGETPROCESSORINDEXFROMNUMBER)(const PROCESSOR_NUMBER *pProcNumber);
typedef  NTSTATUS (NTAPI *PFNKEGETPROCESSORNUMBERFROMINDEX)(KEPROCESSORINDEX ProcIndex, PROCESSOR_NUMBER *pProcNumber);
typedef  KEPROCESSORINDEX (NTAPI *PFNKEGETCURRENTPROCESSORNUMBEREX)(const PROCESSOR_NUMBER *pProcNumber);
typedef  KAFFINITY (NTAPI *PFNKEQUERYACTIVEPROCESSORS)(VOID);
typedef  ULONG   (NTAPI *PFNKEQUERYMAXIMUMPROCESSORCOUNT)(VOID);
typedef  ULONG   (NTAPI *PFNKEQUERYMAXIMUMPROCESSORCOUNTEX)(USHORT GroupNumber);
typedef  USHORT  (NTAPI *PFNKEQUERYMAXIMUMGROUPCOUNT)(VOID);
typedef  ULONG   (NTAPI *PFNKEQUERYACTIVEPROCESSORCOUNT)(KAFFINITY *pfActiveProcessors);
typedef  ULONG   (NTAPI *PFNKEQUERYACTIVEPROCESSORCOUNTEX)(USHORT GroupNumber);
typedef  NTSTATUS (NTAPI *PFNKEQUERYLOGICALPROCESSORRELATIONSHIP)(PROCESSOR_NUMBER *pProcNumber,
                                                                  LOGICAL_PROCESSOR_RELATIONSHIP RelationShipType,
                                                                  SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *pInfo, PULONG pcbInfo);
typedef  PVOID   (NTAPI *PFNKEREGISTERPROCESSORCHANGECALLBACK)(PPROCESSOR_CALLBACK_FUNCTION pfnCallback, void *pvUser, ULONG fFlags);
typedef  VOID    (NTAPI *PFNKEDEREGISTERPROCESSORCHANGECALLBACK)(PVOID pvCallback);
typedef  NTSTATUS (NTAPI *PFNKESETTARGETPROCESSORDPCEX)(KDPC *pDpc, PROCESSOR_NUMBER *pProcNumber);
typedef  LOGICAL  (NTAPI *PFNKESHOULDYIELDPROCESSOR)(void);

RT_DECL_NTAPI(BOOLEAN)  ObFindHandleForObject(PEPROCESS pProcess, PVOID pvObject, POBJECT_TYPE pObjectType,
                                              PVOID pvOptionalConditions, PHANDLE phFound);
RT_DECL_NTAPI(NTSTATUS) ObReferenceObjectByName(PUNICODE_STRING pObjectPath, ULONG fAttributes, PACCESS_STATE pAccessState,
                                                ACCESS_MASK fDesiredAccess, POBJECT_TYPE pObjectType,
                                                KPROCESSOR_MODE enmAccessMode, PVOID pvParseContext, PVOID *ppvObject);
RT_DECL_NTAPI(HANDLE)   PsGetProcessInheritedFromUniqueProcessId(PEPROCESS);
RT_DECL_NTAPI(UCHAR *)  PsGetProcessImageFileName(PEPROCESS);
RT_DECL_NTAPI(BOOLEAN)  PsIsProcessBeingDebugged(PEPROCESS);
RT_DECL_NTAPI(ULONG)    PsGetProcessSessionId(PEPROCESS);
extern DECLIMPORT(POBJECT_TYPE *) LpcPortObjectType;            /**< In vista+ this is the ALPC port object type. */
extern DECLIMPORT(POBJECT_TYPE *) LpcWaitablePortObjectType;    /**< In vista+ this is the ALPC port object type. */

typedef VOID (NTAPI *PFNHALREQUESTIPI_PRE_W7)(KAFFINITY TargetSet);
typedef VOID (NTAPI *PFNHALREQUESTIPI_W7PLUS)(ULONG uUsuallyZero, PCKAFFINITY_EX pTargetSet);

RT_C_DECLS_END
/** @ */
#endif /* IN_RING0 */


#if defined(IN_RING3) || defined(DOXYGEN_RUNNING)
/** @name NT Userland APIs
 * @{ */
RT_C_DECLS_BEGIN

#if 0 /** @todo figure this out some time... */
typedef struct CSR_MSG_DATA_CREATED_PROCESS
{
    HANDLE hProcess;
    HANDLE hThread;
    CLIENT_ID
    DWORD idProcess;
    DWORD idThread;
    DWORD fCreate;

} CSR_MSG_DATA_CREATED_PROCESS;

#define CSR_MSG_NO_CREATED_PROCESS    UINT32_C(0x10000)
#define CSR_MSG_NO_CREATED_THREAD     UINT32_C(0x10001)
RT_DECL_NTAPI(NTSTATUS) CsrClientCallServer(PVOID, PVOID, ULONG, SIZE_T);
#endif

RT_DECL_NTAPI(VOID)     LdrInitializeThunk(PVOID, PVOID, PVOID);

typedef struct _LDR_DLL_LOADED_NOTIFICATION_DATA
{
    ULONG               Flags;
    PCUNICODE_STRING    FullDllName;
    PCUNICODE_STRING    BaseDllName;
    PVOID               DllBase;
    ULONG               SizeOfImage;
} LDR_DLL_LOADED_NOTIFICATION_DATA, LDR_DLL_UNLOADED_NOTIFICATION_DATA;
typedef LDR_DLL_LOADED_NOTIFICATION_DATA *PLDR_DLL_LOADED_NOTIFICATION_DATA, *PLDR_DLL_UNLOADED_NOTIFICATION_DATA;
typedef LDR_DLL_LOADED_NOTIFICATION_DATA const *PCLDR_DLL_LOADED_NOTIFICATION_DATA, *PCLDR_DLL_UNLOADED_NOTIFICATION_DATA;

typedef union _LDR_DLL_NOTIFICATION_DATA
{
    LDR_DLL_LOADED_NOTIFICATION_DATA    Loaded;
    LDR_DLL_UNLOADED_NOTIFICATION_DATA  Unloaded;
} LDR_DLL_NOTIFICATION_DATA;
typedef LDR_DLL_NOTIFICATION_DATA *PLDR_DLL_NOTIFICATION_DATA;
typedef LDR_DLL_NOTIFICATION_DATA const *PCLDR_DLL_NOTIFICATION_DATA;

typedef VOID (NTAPI *PLDR_DLL_NOTIFICATION_FUNCTION)(ULONG ulReason, PCLDR_DLL_NOTIFICATION_DATA pData, PVOID pvUser);

#define LDR_DLL_NOTIFICATION_REASON_LOADED      UINT32_C(1)
#define LDR_DLL_NOTIFICATION_REASON_UNLOADED    UINT32_C(2)
RT_DECL_NTAPI(NTSTATUS) LdrRegisterDllNotification(ULONG fFlags, PLDR_DLL_NOTIFICATION_FUNCTION pfnCallback, PVOID pvUser,
                                                   PVOID *pvCookie);
typedef NTSTATUS (NTAPI *PFNLDRREGISTERDLLNOTIFICATION)(ULONG, PLDR_DLL_NOTIFICATION_FUNCTION, PVOID, PVOID *);
RT_DECL_NTAPI(NTSTATUS) LdrUnregisterDllNotification(PVOID pvCookie);
typedef NTSTATUS (NTAPI *PFNLDRUNREGISTERDLLNOTIFICATION)(PVOID);

RT_DECL_NTAPI(NTSTATUS) LdrLoadDll(IN PWSTR pwszSearchPathOrFlags OPTIONAL, IN PULONG pfFlags OPTIONAL,
                                   IN PCUNICODE_STRING pName, OUT PHANDLE phMod);
typedef NTSTATUS (NTAPI *PFNLDRLOADDLL)(IN PWSTR pwszSearchPathOrFlags OPTIONAL, IN PULONG pfFlags OPTIONAL,
                                        IN PCUNICODE_STRING pName, OUT PHANDLE phMod);
RT_DECL_NTAPI(NTSTATUS) LdrUnloadDll(IN HANDLE hMod);
typedef NTSTATUS (NTAPI *PFNLDRUNLOADDLL)(IN HANDLE hMod);
RT_DECL_NTAPI(NTSTATUS) LdrGetDllHandle(IN PCWSTR pwszDllPath OPTIONAL, IN PULONG pfFlags OPTIONAL,
                                        IN PCUNICODE_STRING pName, OUT PHANDLE phDll);
typedef NTSTATUS (NTAPI *PFNLDRGETDLLHANDLE)(IN PCWSTR pwszDllPath OPTIONAL, IN PULONG pfFlags OPTIONAL,
                                             IN PCUNICODE_STRING pName, OUT PHANDLE phDll);
#define LDRGETDLLHANDLEEX_F_UNCHANGED_REFCOUNT  RT_BIT_32(0)
#define LDRGETDLLHANDLEEX_F_PIN                 RT_BIT_32(1)
/** @since Windows XP. */
RT_DECL_NTAPI(NTSTATUS) LdrGetDllHandleEx(IN ULONG fFlags, IN PCWSTR pwszDllPath OPTIONAL, IN PULONG pfFlags OPTIONAL,
                                          IN PCUNICODE_STRING pName, OUT PHANDLE phDll);
/** @since Windows XP. */
typedef NTSTATUS (NTAPI *PFNLDRGETDLLHANDLEEX)(IN ULONG fFlags, IN PCWSTR pwszDllPath OPTIONAL, IN PULONG pfFlags OPTIONAL,
                                               IN PCUNICODE_STRING pName, OUT PHANDLE phDll);
/** @since Windows 7. */
RT_DECL_NTAPI(NTSTATUS) LdrGetDllHandleByMapping(IN PVOID pvBase, OUT PHANDLE phDll);
/** @since Windows 7. */
typedef NTSTATUS (NTAPI *PFNLDRGETDLLHANDLEBYMAPPING)(IN PVOID pvBase, OUT PHANDLE phDll);
/** @since Windows 7. */
RT_DECL_NTAPI(NTSTATUS) LdrGetDllHandleByName(IN PCUNICODE_STRING pName OPTIONAL, IN PCUNICODE_STRING pFullName OPTIONAL,
                                              OUT PHANDLE phDll);
/** @since Windows 7. */
typedef NTSTATUS (NTAPI *PFNLDRGETDLLHANDLEBYNAME)(IN PCUNICODE_STRING pName OPTIONAL, IN PCUNICODE_STRING pFullName OPTIONAL,
                                                   OUT PHANDLE phDll);
#define LDRADDREFDLL_F_PIN                      RT_BIT_32(0)
RT_DECL_NTAPI(NTSTATUS) LdrAddRefDll(IN ULONG fFlags, IN HANDLE hDll);
typedef NTSTATUS (NTAPI *PFNLDRADDREFDLL)(IN ULONG fFlags, IN HANDLE hDll);
RT_DECL_NTAPI(NTSTATUS) LdrGetProcedureAddress(IN HANDLE hDll, IN ANSI_STRING const *pSymbol OPTIONAL,
                                               IN ULONG uOrdinal OPTIONAL, OUT PVOID *ppvSymbol);
typedef NTSTATUS (NTAPI *PFNLDRGETPROCEDUREADDRESS)(IN HANDLE hDll, IN PCANSI_STRING pSymbol OPTIONAL,
                                                    IN ULONG uOrdinal OPTIONAL, OUT PVOID *ppvSymbol);
#define LDRGETPROCEDUREADDRESSEX_F_DONT_RECORD_FORWARDER RT_BIT_32(0)
/** @since Windows Vista. */
RT_DECL_NTAPI(NTSTATUS) LdrGetProcedureAddressEx(IN HANDLE hDll, IN ANSI_STRING const *pSymbol OPTIONAL,
                                                 IN ULONG uOrdinal OPTIONAL, OUT PVOID *ppvSymbol, ULONG fFlags);
/** @since Windows Vista. */
typedef NTSTATUS (NTAPI *PFNLDRGETPROCEDUREADDRESSEX)(IN HANDLE hDll, IN ANSI_STRING const *pSymbol OPTIONAL,
                                                      IN ULONG uOrdinal OPTIONAL, OUT PVOID *ppvSymbol, ULONG fFlags);
#define LDRLOCKLOADERLOCK_F_RAISE_ERRORS    RT_BIT_32(0)
#define LDRLOCKLOADERLOCK_F_NO_WAIT         RT_BIT_32(1)
#define LDRLOCKLOADERLOCK_DISP_INVALID      UINT32_C(0)
#define LDRLOCKLOADERLOCK_DISP_ACQUIRED     UINT32_C(1)
#define LDRLOCKLOADERLOCK_DISP_NOT_ACQUIRED UINT32_C(2)
/** @since Windows XP. */
RT_DECL_NTAPI(NTSTATUS) LdrLockLoaderLock(IN ULONG fFlags, OUT PULONG puDisposition OPTIONAL, OUT PVOID *ppvCookie);
/** @since Windows XP. */
typedef NTSTATUS (NTAPI *PFNLDRLOCKLOADERLOCK)(IN ULONG fFlags, OUT PULONG puDisposition OPTIONAL, OUT PVOID *ppvCookie);
#define LDRUNLOCKLOADERLOCK_F_RAISE_ERRORS  RT_BIT_32(0)
/** @since Windows XP. */
RT_DECL_NTAPI(NTSTATUS) LdrUnlockLoaderLock(IN ULONG fFlags, OUT PVOID pvCookie);
/** @since Windows XP. */
typedef NTSTATUS (NTAPI *PFNLDRUNLOCKLOADERLOCK)(IN ULONG fFlags, OUT PVOID pvCookie);

RT_DECL_NTAPI(NTSTATUS) RtlExpandEnvironmentStrings_U(PVOID, PUNICODE_STRING, PUNICODE_STRING, PULONG);
RT_DECL_NTAPI(VOID)     RtlExitUserProcess(NTSTATUS rcExitCode); /**< Vista and later. */
RT_DECL_NTAPI(VOID)     RtlExitUserThread(NTSTATUS rcExitCode);
RT_DECL_NTAPI(NTSTATUS) RtlDosApplyFileIsolationRedirection_Ustr(IN ULONG fFlags,
                                                                 IN PCUNICODE_STRING pOrgName,
                                                                 IN PUNICODE_STRING pDefaultSuffix,
                                                                 IN OUT PUNICODE_STRING pStaticString,
                                                                 IN OUT PUNICODE_STRING pDynamicString,
                                                                 IN OUT PUNICODE_STRING *ppResultString,
                                                                 IN PULONG pfNewFlags OPTIONAL,
                                                                 IN PSIZE_T pcbFilename OPTIONAL,
                                                                 IN PSIZE_T pcbNeeded OPTIONAL);
/** @since Windows 8.
 * @note Status code is always zero in windows 10 build 14393. */
RT_DECL_NTAPI(NTSTATUS) ApiSetQueryApiSetPresence(IN PCUNICODE_STRING pAllegedApiSetDll, OUT PBOOLEAN pfPresent);
/** @copydoc ApiSetQueryApiSetPresence */
typedef NTSTATUS (NTAPI *PFNAPISETQUERYAPISETPRESENCE)(IN PCUNICODE_STRING pAllegedApiSetDll, OUT PBOOLEAN pfPresent);


# ifdef IPRT_NT_USE_WINTERNL
typedef NTSTATUS NTAPI RTL_HEAP_COMMIT_ROUTINE(PVOID, PVOID *, PSIZE_T);
typedef RTL_HEAP_COMMIT_ROUTINE *PRTL_HEAP_COMMIT_ROUTINE;
typedef struct _RTL_HEAP_PARAMETERS
{
    ULONG   Length;
    SIZE_T  SegmentReserve;
    SIZE_T  SegmentCommit;
    SIZE_T  DeCommitFreeBlockThreshold;
    SIZE_T  DeCommitTotalFreeThreshold;
    SIZE_T  MaximumAllocationSize;
    SIZE_T  VirtualMemoryThreshold;
    SIZE_T  InitialCommit;
    SIZE_T  InitialReserve;
    PRTL_HEAP_COMMIT_ROUTINE  CommitRoutine;
    SIZE_T  Reserved[2];
} RTL_HEAP_PARAMETERS;
typedef RTL_HEAP_PARAMETERS *PRTL_HEAP_PARAMETERS;
RT_DECL_NTAPI(PVOID) RtlCreateHeap(ULONG fFlags, PVOID pvHeapBase, SIZE_T cbReserve, SIZE_T cbCommit, PVOID pvLock,
                                   PRTL_HEAP_PARAMETERS pParameters);
/** @name Heap flags (for RtlCreateHeap).
 * @{ */
/*#  define HEAP_NO_SERIALIZE             UINT32_C(0x00000001)
#  define HEAP_GROWABLE                 UINT32_C(0x00000002)
#  define HEAP_GENERATE_EXCEPTIONS      UINT32_C(0x00000004)
#  define HEAP_ZERO_MEMORY              UINT32_C(0x00000008)
#  define HEAP_REALLOC_IN_PLACE_ONLY    UINT32_C(0x00000010)
#  define HEAP_TAIL_CHECKING_ENABLED    UINT32_C(0x00000020)
#  define HEAP_FREE_CHECKING_ENABLED    UINT32_C(0x00000040)
#  define HEAP_DISABLE_COALESCE_ON_FREE UINT32_C(0x00000080)*/
#  define HEAP_SETTABLE_USER_VALUE      UINT32_C(0x00000100)
#  define HEAP_SETTABLE_USER_FLAG1      UINT32_C(0x00000200)
#  define HEAP_SETTABLE_USER_FLAG2      UINT32_C(0x00000400)
#  define HEAP_SETTABLE_USER_FLAG3      UINT32_C(0x00000800)
#  define HEAP_SETTABLE_USER_FLAGS      UINT32_C(0x00000e00)
#  define HEAP_CLASS_0                  UINT32_C(0x00000000)
#  define HEAP_CLASS_1                  UINT32_C(0x00001000)
#  define HEAP_CLASS_2                  UINT32_C(0x00002000)
#  define HEAP_CLASS_3                  UINT32_C(0x00003000)
#  define HEAP_CLASS_4                  UINT32_C(0x00004000)
#  define HEAP_CLASS_5                  UINT32_C(0x00005000)
#  define HEAP_CLASS_6                  UINT32_C(0x00006000)
#  define HEAP_CLASS_7                  UINT32_C(0x00007000)
#  define HEAP_CLASS_8                  UINT32_C(0x00008000)
#  define HEAP_CLASS_MASK               UINT32_C(0x0000f000)
# endif
# define HEAP_CLASS_PROCESS             HEAP_CLASS_0
# define HEAP_CLASS_PRIVATE             HEAP_CLASS_1
# define HEAP_CLASS_KERNEL              HEAP_CLASS_2
# define HEAP_CLASS_GDI                 HEAP_CLASS_3
# define HEAP_CLASS_USER                HEAP_CLASS_4
# define HEAP_CLASS_CONSOLE             HEAP_CLASS_5
# define HEAP_CLASS_USER_DESKTOP        HEAP_CLASS_6
# define HEAP_CLASS_CSRSS_SHARED        HEAP_CLASS_7
# define HEAP_CLASS_CSRSS_PORT          HEAP_CLASS_8
# ifdef IPRT_NT_USE_WINTERNL
/*#  define HEAP_CREATE_ALIGN_16          UINT32_C(0x00010000)
#  define HEAP_CREATE_ENABLE_TRACING    UINT32_C(0x00020000)
#  define HEAP_CREATE_ENABLE_EXECUTE    UINT32_C(0x00040000)*/
#  define HEAP_CREATE_VALID_MASK        UINT32_C(0x0007f0ff)
# endif /* IPRT_NT_USE_WINTERNL */
/** @} */
# ifdef IPRT_NT_USE_WINTERNL
/** @name Heap tagging constants
 * @{ */
#  define HEAP_GLOBAL_TAG               UINT32_C(0x00000800)
/*#  define HEAP_MAXIMUM_TAG              UINT32_C(0x00000fff)
#  define HEAP_PSEUDO_TAG_FLAG          UINT32_C(0x00008000)
#  define HEAP_TAG_SHIFT                18 */
#  define HEAP_TAG_MASK                 (HEAP_MAXIMUM_TAG << HEAP_TAG_SHIFT)
/** @}  */
RT_DECL_NTAPI(PVOID)        RtlAllocateHeap(HANDLE hHeap, ULONG fFlags, SIZE_T cb);
RT_DECL_NTAPI(PVOID)        RtlReAllocateHeap(HANDLE hHeap, ULONG fFlags, PVOID pvOld, SIZE_T cbNew);
RT_DECL_NTAPI(BOOLEAN)      RtlFreeHeap(HANDLE hHeap, ULONG fFlags, PVOID pvMem);
# endif /* IPRT_NT_USE_WINTERNL */
RT_DECL_NTAPI(SIZE_T)       RtlCompactHeap(HANDLE hHeap, ULONG fFlags);
RT_DECL_NTAPI(SIZE_T)       RtlSizeHeap(HANDLE hHeap, ULONG fFlags, PVOID pvMem);
RT_DECL_NTAPI(NTSTATUS)     RtlGetLastNtStatus(VOID);
RT_DECL_NTAPI(ULONG)        RtlGetLastWin32Error(VOID);
RT_DECL_NTAPI(VOID)         RtlSetLastWin32Error(ULONG uError);
RT_DECL_NTAPI(VOID)         RtlSetLastWin32ErrorAndNtStatusFromNtStatus(NTSTATUS rcNt);
RT_DECL_NTAPI(VOID)         RtlRestoreLastWin32Error(ULONG uError);
RT_DECL_NTAPI(BOOLEAN)      RtlQueryPerformanceCounter(PLARGE_INTEGER);
RT_DECL_NTAPI(uint64_t)     RtlGetSystemTimePrecise(VOID);
typedef uint64_t (NTAPI * PFNRTLGETSYSTEMTIMEPRECISE)(VOID);
RT_DECL_NTAPI(uint64_t)     RtlGetInterruptTimePrecise(uint64_t *puPerfTime);
typedef uint64_t (NTAPI * PFNRTLGETINTERRUPTTIMEPRECISE)(uint64_t *);
RT_DECL_NTAPI(BOOLEAN)      RtlQueryUnbiasedInterruptTime(uint64_t *puInterruptTime);
typedef BOOLEAN (NTAPI * PFNRTLQUERYUNBIASEDINTERRUPTTIME)(uint64_t *);

RT_C_DECLS_END
/** @} */
#endif /* IN_RING3 */

#endif /* !IPRT_INCLUDED_nt_nt_h */

