/* $Id: SUPR3HardenedMain.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Support Library - Hardened main().
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

/** @page pg_hardening      %VirtualBox %VM Process Hardening
 *
 * The %VM process hardening is to prevent malicious software from using
 * %VirtualBox as a vehicle to obtain kernel level access.
 *
 * The %VirtualBox %VMM requires supervisor (kernel) level access to the CPU.
 * For both practical and historical reasons, part of the %VMM is realized in
 * ring-3, with a rich interface to the kernel part.  While the device
 * emulations can be executed exclusively in ring-3, we have performance
 * optimizations that loads device emulation code into ring-0 and our special
 * raw-mode execution context (none VT-x/AMD-V mode) for handling frequent
 * operations a lot more efficiently.  These share data between all three
 * context (ring-3, ring-0 and raw-mode).  All this poses a rather broad attack
 * surface, which the hardening protects.
 *
 * The hardening focuses primarily on restricting access to the support driver,
 * VBoxDrv or vboxdrv depending on the OS, as it is ultimately the link and
 * instigator of the communication between ring-3 and the ring-0 and raw-mode
 * contexts.  A secondary focus is to make sure malicious code cannot be loaded
 * and executed in the %VM process.  Exactly how we go about this depends a lot
 * on the host OS.
 *
 * @section sec_hardening_supdrv    The Support Driver Interfaces
 *
 * The support driver has several interfaces thru which it can be accessed:
 *      - /dev/vboxdrv (win: \\Device\\VBoxDrv) for full unrestricted access.
 *        Offers a rich I/O control interface, which needs protecting.
 *      - /dev/vboxdrvu (win: \\Device\\VBoxDrvU) for restricted access, which
 *        VBoxSVC uses to query VT-x and AMD-V capabilities.  This does not
 *        require protecting, though we limit it to the vboxgroup on some
 *        systems.
 *      - \\Device\\VBoxDrvStub on Windows for protecting the second stub
 *        process and its child, the %VM process.  This is an open+close
 *        interface, only available to partially verified stub processes.
 *      - \\Device\\VBoxDrvErrorInfo on Windows for obtaining detailed error
 *        information on a previous attempt to open \\Device\\VBoxDrv or
 *        \\Device\\VBoxDrvStub.  Open, read and close only interface.
 *
 * The rest of VBox accesses the device interface thru the support library,
 * @ref grp_sup "SUPR3" / sup.h.
 *
 * The support driver also exposes a set of functions and data that other VBox
 * ring-0 modules can import from.  This includes much of the IPRT we need in
 * the ring-0 part of the %VMM and device emulations.
 *
 * The ring-0 part of the %VMM and device emulations are loaded via the
 * #SUPR3LoadModule and #SUPR3LoadServiceModule support library function, which
 * both translates to a sequence of I/O controls against /dev/vboxdrv.  On
 * Windows we use the native kernel loader to load the module, while on the
 * other systems ring-3 prepares the bits with help from the IPRT loader code.
 *
 *
 * @section sec_hardening_unix  Hardening on UNIX-like OSes
 *
 * On UNIX-like systems (Solaris, Linux, darwin, freebsd, ...) we put our trust
 * in root and that root knows what he/she/it is doing.
 *
 * We only allow root to get full unrestricted access to the support driver.
 * The device node corresponding to unrestricted access (/dev/vboxdrv) is own by
 * root and has a 0600 access mode (i.e. only accessible to the owner, root). In
 * addition to this file system level restriction, the support driver also
 * checks that the effective user ID (EUID) is root when it is being opened.
 *
 * The %VM processes temporarily assume root privileges using the set-uid-bit on
 * the executable with root as owner.  In fact, all the files and directories we
 * install are owned by root and the wheel (or equivalent gid = 0) group,
 * including extension pack files.
 *
 * The executable with the set-uid-to-root-bit set is a stub binary that has no
 * unnecessary library dependencies (only libc, pthreads, dynamic linker) and
 * simply calls #SUPR3HardenedMain.  It does the following:
 *      1. Validate the VirtualBox installation (#supR3HardenedVerifyAll):
 *          - Check that the executable file of the process is one of the known
 *            VirtualBox executables.
 *          - Check that all mandatory files are present.
 *          - Check that all installed files and directories (both optional and
 *            mandatory ones) are owned by root:wheel and are not writable by
 *            anyone except root.
 *          - Check that all the parent directories, all the way up to the root
 *            if possible, only permits root (or system admin) to change them.
 *            This is that to rule out unintentional rename races.
 *          - On some systems we may also validate the cryptographic signtures
 *            of executable images.
 *
 *      2. Open a file descriptor for the support device driver
 *         (#supR3HardenedMainOpenDevice).
 *
 *      3. Grab ICMP capabilities for NAT ping support, if required by the OS
 *         (#supR3HardenedMainGrabCapabilites).
 *
 *      4. Correctly drop the root privileges
 *         (#supR3HardenedMainDropPrivileges).
 *
 *      5. Load the VBoxRT dynamic link library and hand over the file
 *         descriptor to the support library code in it
 *         (#supR3HardenedMainInitRuntime).
 *
 *      6. Load the dynamic library containing the actual %VM front end code and
 *         run it (tail of #SUPR3HardenedMain).
 *
 * The set-uid-to-root stub executable is paired with a dynamic link library
 * which export one TrustedMain entry point (see #FNSUPTRUSTEDMAIN) that we
 * call. In case of error reporting, the library may also export a TrustedError
 * function (#FNSUPTRUSTEDERROR).
 *
 * That the set-uid-to-root-bit modifies the dynamic linker behavior on all
 * systems, even after we've dropped back to the real user ID, is something we
 * take advantage of.  The dynamic linkers takes special care to prevent users
 * from using clever tricks to inject their own code into set-uid processes and
 * causing privilege escalation issues.  This is the exact help we need.
 *
 * The VirtualBox installation location is hardcoded, which means the any
 * dynamic linker paths embedded or inferred from the executable and dynamic
 * libraries are also hardcoded.  This helps eliminating search path attack
 * vectors at the cost of being inflexible regarding installation location.
 *
 * In addition to what the dynamic linker does for us, the VirtualBox code will
 * not directly be calling either RTLdrLoad or dlopen to load dynamic link
 * libraries into the process.  Instead it will call #SUPR3HardenedLdrLoad,
 * #SUPR3HardenedLdrLoadAppPriv and #SUPR3HardenedLdrLoadPlugIn to do the
 * loading. These functions will perform the same validations on the file being
 * loaded as #SUPR3HardenedMain did in its validation step.  So, anything we
 * load must be installed with root/wheel as owner/group, the directory we load
 * it from must also be owned by root:wheel and now allow for renaming the file.
 * Similar ownership restrictions applies to all the parent directories (except
 * on darwin).
 *
 * So, we place the responsibility of not installing malicious software on the
 * root user on UNIX-like systems.  Which is fair enough, in our opinion.
 *
 *
 * @section sec_hardening_win   Hardening on Windows
 *
 * On Windows we cannot put the same level or trust in the Administrator user(s)
 * (equivalent of root/wheel on unix) as on the UNIX-like systems, which
 * complicates things greatly.
 *
 * Some of the blame for this can be given to Windows being a descendant /
 * replacement for a set of single user systems: DOS, Windows 1.0-3.11 Windows
 * 95-ME, and OS/2.  Users of NT 3.1 and later was inclined to want to always
 * run it with full root/administrator privileges like they had done on the
 * predecessors, while Microsoft didn't provide much incentive for more secure
 * alternatives.  Bad idea, security wise, but execellent for the security
 * software industry.  For this reason using a set-uid-to-root approach is
 * pointless, even if Windows had one.
 *
 * So, in order to protect access to the support driver and protect the %VM
 * process while it's running we have to do a lot more work.  A keystone in the
 * defences is cryptographic code signing.  Here's the short version of what we
 * do:
 *      - Minimal stub executable, signed with the same certificate as the
 *        kernel driver.
 *
 *      - The stub executable respawns itself twice, hooking the NTDLL init
 *        routine to perform protection tasks as early as possible.  The parent
 *        stub helps keep in the child clean for verification as does the
 *        support driver.
 *
 *      - In order to protect against loading unwanted code into the process,
 *        the stub processes installs DLL load hooks with NTDLL as well as
 *        directly intercepting the LdrLoadDll and NtCreateSection APIs.
 *
 *      - The support driver will verify all but the initial process very
 *        thoroughly before allowing them protection and in the final case full
 *        unrestricted access.
 *
 *
 * @subsection  sec_hardening_win_protsoft      3rd Party "Protection" Software
 *
 * What makes our life REALLY difficult on Windows is this 3rd party "security"
 * software which is more or less required to keep a Windows system safe for
 * normal users and all corporate IT departments rightly insists on installing.
 * After the kernel patching clampdown in Vista, anti-* software has to do a
 * lot more mucking about in user mode to get their job (kind of) done.  So, it
 * is common practice to patch a lot of NTDLL, KERNEL32, the executable import
 * table, load extra DLLs into the process, allocate executable memory in the
 * process (classic code injection) and more.
 *
 * The BIG problem with all this is that it is indistinguishable from what
 * malicious software would be doing in order to intercept process activity
 * (network sniffing, maybe password snooping) or gain a level of kernel access
 * via the support driver.  So, the "protection" software is what is currently
 * forcing us to do the pre-NTDLL initialization.
 *
 *
 * @subsection  sec_hardening_win_1st_stub  The Initial Stub Process
 *
 * We share the stub executable approach with the UNIX-like systems, so there's
 * the #SUPR3HardenedMain calling stub executable with its partner DLL exporting
 * TrustedMain and TrustedError.   However, the stub executable does a lot more,
 * while doing it in a more bare metal fashion:
 *      - It does not use the Microsoft CRT, what we need of CRT functions comes
 *        from IPRT.
 *      - It does not statically import anything.  This is to avoid having an
 *        import table that can be patched to intercept our calls or extended to
 *        load additional DLLs.
 *      - Direct NT system calls.  System calls normally going thru NTDLL, but
 *        since there is so much software out there which wants to patch known
 *        NTDLL entry points to control our software (either for good or
 *        malicious reasons), we do it ourselves.
 *
 * The initial stub process is not really to be trusted, though we try our best
 * to limit potential harm (user mode debugger checks, disable thread creation).
 * So, when it enters #SUPR3HardenedMain we only call #supR3HardenedVerifyAll to
 * verify the installation (known executables and DLLs, checking their code
 * signing signatures, keeping them all open to deny deletion and replacing) and
 * does a respawn via #supR3HardenedWinReSpawn.
 *
 *
 * @subsection  sec_hardening_win_2nd_stub  The Second Stub Process
 *
 * The second stub process will be created in suspended state, i.e. the main
 * thread is suspended before it executes a single instruction.  It is also
 * created with a less generous ACLs, though this doesn't protect us from admin
 * users.  In order for #SUPR3HardenedMain to figure that it is the second stub
 * process, the zeroth command line argument has been replaced by a known magic
 * string (UUID).
 *
 * Now, before the process starts executing, the parent (initial stub) will
 * patch the LdrInitializeThunk entry point in NTDLL to call
 * #supR3HardenedEarlyProcessInit via #supR3HardenedEarlyProcessInitThunk.  The
 * parent will also plant some synchronization stuff via #g_ProcParams (NTDLL
 * location, inherited event handles and associated ping-pong equipment).
 *
 * The LdrInitializeThunk entry point of NTDLL is where the kernel sets up
 * process execution to start executing (via a user alert, so it is not subject
 * to SetThreadContext).  LdrInitializeThunk performs process, NTDLL and
 * sub-system client (kernel32) initialization.  A lot of "protection" software
 * uses triggers in this initialization sequence (like the KERNEL32.DLL load
 * event), so we avoid quite a bit of problems by getting our stuff done early
 * on.
 *
 * However, there are also those that uses events that triggers immediately when
 * the process is created or/and starts executing the first instruction.  But we
 * can easily counter these as we have a known process state we can restore. So,
 * the first thing that #supR3HardenedEarlyProcessInit does is to signal the
 * parent to  perform a child purification, so the potentially evil influences
 * can be exorcised.
 *
 * What the parent does during the purification is very similar to what the
 * kernel driver will do later on when verifying the second stub and the %VM
 * processes, except that instead of failing when encountering an shortcoming it
 * will take corrective actions:
 *      - Executable memory regions not belonging to a DLL mapping will be
 *        attempted freed, and we'll only fail if we can't evict them.
 *      - All pages in the executable images in the process (should be just the
 *        stub executable and NTDLL) will be compared to the pristine fixed-up
 *        copy prepared by the IPRT PE loader code, restoring any bytes which
 *        appears differently in the child.  (#g_ProcParams is exempted,
 *        LdrInitializeThunk is set to call NtTerminateThread.)
 *      - Unwanted DLLs will be unloaded (we have a set of DLLs we like).
 *
 * Before signalling the second stub process that it has been purified and should
 * get on with it, the parent will close all handles with unrestricted access to
 * the process and thread so that the initial stub process no longer can
 * influence the child in any really harmful way.  (The caller of CreateProcess
 * usually receives handles with unrestricted access to the child process and
 * its main thread.  These could in theory be used with DuplicateHandle or
 * WriteProcessMemory to get at the %VM process if we're not careful.)
 *
 * #supR3HardenedEarlyProcessInit will continue with opening the log file
 * (requires command line parsing).  It will continue to initialize a bunch of
 * global variables, system calls and trustworthy/harmless NTDLL imports.
 * #supR3HardenedWinInit is then called to setup image verification, that is:
 *      - Hook the NtCreateSection entry point in NTDLL so we can check all
 *        executable mappings before they're created and can be mapped.  The
 *        NtCreateSection code jumps to #supR3HardenedMonitor_NtCreateSection.
 *      - Hook (ditto) the LdrLoadDll entry point in NTDLL so we can
 *        pre-validate all images that gets loaded the normal way (partly
 *        because the NtCreateSection context is restrictive because the NTDLL
 *        loader lock is usually held, which prevents us from safely calling
 *        WinVerityTrust).  The LdrLoadDll code jumps to
 *        #supR3HardenedMonitor_LdrLoadDll.
 *
 * The image/DLL verification hooks are at this point able to verify DLLs
 * containing embedded code signing signatures, and will restrict the locations
 * from which DLLs will be loaded.  When #SUPR3HardenedMain gets going later on,
 * they will start insisting on everything having valid signatures, either
 * embedded or in a signed installer catalog file.
 *
 * The function also irrevocably disables debug notifications related to the
 * current thread, just to make attaching a debugging that much more difficult
 * and less useful.
 *
 * Now, the second stub process will open the so called stub device
 * (\\Device\\VBoxDrvStub), that is a special support driver device node that
 * tells the support driver to:
 *      - Protect the process against the OpenProcess and OpenThread attack
 *        vectors by stripping risky access rights.
 *      - Check that the process isn't being debugged.
 *      - Check that the process contains exactly one thread.
 *      - Check that the process doesn't have any unknown DLLs loaded into it.
 *      - Check that the process doesn't have any executable memory (other than
 *        DLL sections) in it.
 *      - Check that the process executable is a known VBox executable which may
 *        access the support driver.
 *      - Check that the process executable is signed with the same code signing
 *        certificate as the driver and that the on disk image is valid
 *        according to its embedded signature.
 *      - Check all the signature of all DLLs in the process (NTDLL) if they are
 *        signed, and only accept unsigned ones in versions where they are known
 *        not to be signed.
 *      - Check that the code and readonly parts of the executable and DLLs
 *        mapped into the process matches the on disk content (no patches other
 *        than our own two in NTDLL are allowed).
 *
 * Once granted access to the stub device, #supR3HardenedEarlyProcessInit will
 * restore the LdrInitializeThunk code and let the process perform normal
 * initialization.  Leading us to #SUPR3HardenedMain where we detect that this
 * is the 2nd stub process and does another respawn.
 *
 *
 * @subsection  sec_hardening_win_3rd_stub  The Final Stub / VM Process
 *
 * The third stub process is what becomes the %VM process.  Because the parent
 * has opened \\Device\\VBoxDrvSub, it is protected from malicious OpenProcess &
 * OpenThread calls from the moment of inception, practically speaking.
 *
 * It goes thru the same suspended creation, patching, purification and such as
 * its parent (the second stub process).  However, instead of opening
 * \\Device\\VBoxDrvStub from #supR3HardenedEarlyProcessInit, it opens the
 * support driver for full unrestricted access, i.e. \\Device\\VBoxDrv.
 *
 * The support driver will perform the same checks as it did when
 * \\Device\\VBoxDrvStub was opened, but in addition it will:
 *      - Check that the process is the first child of a process that opened
 *        \\Device\\VBoxDrvStub.
 *      - Check that the parent process is still alive.
 *      - Scan all open handles in the system for potentially harmful ones to
 *        the process or the primary thread.
 *
 * Knowing that the process is genuinly signed with the same certificate as the
 * kernel driver, and the exectuable code in the process is either shipped by us
 * or Microsoft, the support driver will trust it with full access and to keep
 * the handle secure.
 *
 * We also trust the protection the support driver gives the process to keep out
 * malicious ring-3 code, and therefore any code, patching or other mysterious
 * stuff that enteres the process must be from kernel mode and that we can trust
 * it (the alternative interpretation is that the kernel has been breanched
 * already, which isn't our responsibility).  This means that, the anti-software
 * products can do whatever they like from this point on.  However, should they
 * do unrevertable changes to the process before this point, VirtualBox won't
 * work.
 *
 * As in the second stub process, we'll now do normal process initialization and
 * #SUPR3HardenedMain will take control.  It will detect that it is being called
 * by the 3rd stub process because of a different magic string starting the
 * command line, and not respawn itself any more.  #SUPR3HardenedMain will
 * recheck the VirtualBox installation, keeping all known files open just like
 * in two previous stub processes.
 *
 * It will then load the Windows cryptographic API and load the trusted root
 * certificates from the Windows store.  The API enables using installation
 * catalog files for signature checking as well as providing a second
 * verification in addition to our own implementation (IPRT).  The certificates
 * allows our signature validation implementation to validate all embedded
 * signatures, not just the microsoft ones and the one signed by our own
 * certificate.
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#if defined(RT_OS_OS2)
# define INCL_BASE
# define INCL_ERRORS
# include <os2.h>
# include <stdio.h>
# include <stdlib.h>
# include <dlfcn.h>
# include <unistd.h>

#elif RT_OS_WINDOWS
# include <iprt/nt/nt-and-windows.h>

#else /* UNIXes */
# ifdef RT_OS_DARWIN
#  define _POSIX_C_SOURCE 1 /* pick the correct prototype for unsetenv. */
# endif
# include <iprt/types.h> /* stdint fun on darwin. */

# include <stdio.h>
# include <stdlib.h>
# include <dlfcn.h>
# include <limits.h>
# include <errno.h>
# include <unistd.h>
# include <sys/stat.h>
# include <sys/time.h>
# include <sys/types.h>
# if defined(RT_OS_LINUX)
#  undef USE_LIB_PCAP /* don't depend on libcap as we had to depend on either
                         libcap1 or libcap2 */

#  undef _POSIX_SOURCE
#  include <linux/types.h> /* sys/capabilities from uek-headers require this */
#  include <sys/capability.h>
#  include <sys/prctl.h>
#  ifndef CAP_TO_MASK
#   define CAP_TO_MASK(cap) RT_BIT(cap)
#  endif
# elif defined(RT_OS_FREEBSD)
#  include <sys/param.h>
#  include <sys/sysctl.h>
# elif defined(RT_OS_SOLARIS)
#  include <priv.h>
# endif
# include <pwd.h>
# ifdef RT_OS_DARWIN
#  include <mach-o/dyld.h>
# endif

#endif

#include <VBox/sup.h>
#include <VBox/err.h>
#ifdef RT_OS_WINDOWS
# include <VBox/version.h>
# include <iprt/utf16.h>
#endif
#include <iprt/ctype.h>
#include <iprt/string.h>
#include <iprt/initterm.h>
#include <iprt/param.h>
#include <iprt/path.h>

#include "SUPLibInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/* This mess is temporary after eliminating a define duplicated in SUPLibInternal.h. */
#if !defined(RT_OS_OS2) && !defined(RT_OS_WINDOWS) && !defined(RT_OS_L4)
# ifndef SUP_HARDENED_SUID
#  error "SUP_HARDENED_SUID is NOT defined?!?"
# endif
#else
# ifdef SUP_HARDENED_SUID
#  error "SUP_HARDENED_SUID is defined?!?"
# endif
#endif

/** @def SUP_HARDENED_SYM
 * Decorate a symbol that's resolved dynamically.
 */
#ifdef RT_OS_OS2
# define SUP_HARDENED_SYM(sym)  "_" sym
#else
# define SUP_HARDENED_SYM(sym)  sym
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** @see RTR3InitEx */
typedef DECLCALLBACKTYPE(int, FNRTR3INITEX,(uint32_t iVersion, uint32_t fFlags, int cArgs,
                                            char **papszArgs, const char *pszProgramPath));
typedef FNRTR3INITEX *PFNRTR3INITEX;

/** @see RTLogRelPrintf */
typedef DECLCALLBACKTYPE(void, FNRTLOGRELPRINTF,(const char *pszFormat, ...));
typedef FNRTLOGRELPRINTF *PFNRTLOGRELPRINTF;


/**
 * Descriptor of an environment variable to purge.
 */
typedef struct SUPENVPURGEDESC
{
    /** Name of the environment variable to purge. */
    const char         *pszEnv;
    /** The length of the variable name. */
    uint8_t             cchEnv;
    /** Flag whether a failure in purging the variable leads to
     * a fatal error resulting in an process exit. */
    bool                fPurgeErrFatal;
} SUPENVPURGEDESC;
/** Pointer to a environment variable purge descriptor. */
typedef SUPENVPURGEDESC *PSUPENVPURGEDESC;
/** Pointer to a const environment variable purge descriptor. */
typedef const SUPENVPURGEDESC *PCSUPENVPURGEDESC;

/**
 * Descriptor of an command line argument to purge.
 */
typedef struct SUPARGPURGEDESC
{
    /** Name of the argument to purge. */
    const char         *pszArg;
    /** The length of the argument name. */
    uint8_t             cchArg;
    /** Flag whether the argument is followed by an extra argument
     * which must be purged too */
    bool                fTakesValue;
} SUPARGPURGEDESC;
/** Pointer to a environment variable purge descriptor. */
typedef SUPARGPURGEDESC *PSUPARGPURGEDESC;
/** Pointer to a const environment variable purge descriptor. */
typedef const SUPARGPURGEDESC *PCSUPARGPURGEDESC;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The pre-init data we pass on to SUPR3 (residing in VBoxRT). */
static SUPPREINITDATA   g_SupPreInitData;
/** The program executable path. */
#ifndef RT_OS_WINDOWS
static
#endif
char                    g_szSupLibHardenedExePath[RTPATH_MAX];
/** The application bin directory path. */
static char             g_szSupLibHardenedAppBinPath[RTPATH_MAX];
/** The offset into g_szSupLibHardenedExePath of the executable name. */
static size_t           g_offSupLibHardenedExecName;
/** The length of the executable name in g_szSupLibHardenedExePath. */
static size_t           g_cchSupLibHardenedExecName;

/** The program name. */
static const char      *g_pszSupLibHardenedProgName;
/** The flags passed to SUPR3HardenedMain - SUPSECMAIN_FLAGS_XXX. */
static uint32_t         g_fSupHardenedMain;

#ifdef SUP_HARDENED_SUID
/** The real UID at startup. */
static uid_t            g_uid;
/** The real GID at startup. */
static gid_t            g_gid;
# ifdef RT_OS_LINUX
static uint32_t         g_uCaps;
static uint32_t         g_uCapsVersion;
# endif
#endif

/** The startup log file. */
#ifdef RT_OS_WINDOWS
static HANDLE           g_hStartupLog = NULL;
#else
static int              g_hStartupLog = -1;
#endif
/** The number of bytes we've written to the startup log. */
static uint32_t volatile g_cbStartupLog = 0;

/** The current SUPR3HardenedMain state / location. */
SUPR3HARDENEDMAINSTATE  g_enmSupR3HardenedMainState = SUPR3HARDENEDMAINSTATE_NOT_YET_CALLED;
AssertCompileSize(g_enmSupR3HardenedMainState, sizeof(uint32_t));

#ifdef RT_OS_WINDOWS
/** Pointer to VBoxRT's RTLogRelPrintf function so we can write errors to the
 * release log at runtime. */
static PFNRTLOGRELPRINTF g_pfnRTLogRelPrintf = NULL;
/** Log volume name (for attempting volume flush). */
static RTUTF16          g_wszStartupLogVol[16];
#endif

/** Environment variables to purge from the process because
 * they are known to be harmful. */
static const SUPENVPURGEDESC g_aSupEnvPurgeDescs[] =
{
    /* pszEnv                                       fPurgeErrFatal */
    /* Qt related environment variables: */
    { RT_STR_TUPLE("QT_QPA_PLATFORM_PLUGIN_PATH"),  true },
    { RT_STR_TUPLE("QT_PLUGIN_PATH"),               true },
    /* ALSA related environment variables: */
    { RT_STR_TUPLE("ALSA_MIXER_SIMPLE_MODULES"),    true },
    { RT_STR_TUPLE("LADSPA_PATH"),                  true },
};

/** Arguments to purge from the argument vector because
 * they are known to be harmful. */
static const SUPARGPURGEDESC g_aSupArgPurgeDescs[] =
{
    /* pszArg                        fTakesValue */
    /* Qt related environment variables: */
    { RT_STR_TUPLE("-platformpluginpath"),          true },
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#ifdef SUP_HARDENED_SUID
static void supR3HardenedMainDropPrivileges(void);
#endif
static PFNSUPTRUSTEDERROR supR3HardenedMainGetTrustedError(const char *pszProgName);


/**
 * Safely copy one or more strings into the given buffer.
 *
 * @returns VINF_SUCCESS or VERR_BUFFER_OVERFLOW.
 * @param   pszDst              The destionation buffer.
 * @param   cbDst               The size of the destination buffer.
 * @param   ...                 One or more zero terminated strings, ending with
 *                              a NULL.
 */
static int suplibHardenedStrCopyEx(char *pszDst, size_t cbDst, ...)
{
    int rc = VINF_SUCCESS;

    if (cbDst == 0)
        return VERR_BUFFER_OVERFLOW;

    va_list va;
    va_start(va, cbDst);
    for (;;)
    {
        const char *pszSrc = va_arg(va, const char *);
        if (!pszSrc)
            break;

        size_t cchSrc = suplibHardenedStrLen(pszSrc);
        if (cchSrc < cbDst)
        {
            suplibHardenedMemCopy(pszDst, pszSrc, cchSrc);
            pszDst += cchSrc;
            cbDst  -= cchSrc;
        }
        else
        {
            rc = VERR_BUFFER_OVERFLOW;
            if (cbDst > 1)
            {
                suplibHardenedMemCopy(pszDst, pszSrc, cbDst - 1);
                pszDst += cbDst - 1;
                cbDst   = 1;
            }
        }
        *pszDst = '\0';
    }
    va_end(va);

    return rc;
}


/**
 * Exit current process in the quickest possible fashion.
 *
 * @param   rcExit      The exit code.
 */
DECLHIDDEN(DECL_NO_RETURN(void)) suplibHardenedExit(RTEXITCODE rcExit)
{
    for (;;)
    {
#ifdef RT_OS_WINDOWS
        if (g_enmSupR3HardenedMainState >= SUPR3HARDENEDMAINSTATE_WIN_IMPORTS_RESOLVED)
            ExitProcess(rcExit);
        if (RtlExitUserProcess != NULL)
            RtlExitUserProcess(rcExit);
        NtTerminateProcess(NtCurrentProcess(), rcExit);
#else
        _Exit(rcExit);
#endif
    }
}


/**
 * Writes a substring to standard error.
 *
 * @param   pch                 The start of the substring.
 * @param   cch                 The length of the substring.
 */
static void suplibHardenedPrintStrN(const char *pch, size_t cch)
{
#ifdef RT_OS_WINDOWS
    HANDLE hStdOut = NtCurrentPeb()->ProcessParameters->StandardOutput;
    if (hStdOut != NULL)
    {
        if (g_enmSupR3HardenedMainState >= SUPR3HARDENEDMAINSTATE_WIN_IMPORTS_RESOLVED)
        {
            DWORD cbWritten;
            WriteFile(hStdOut, pch, (DWORD)cch, &cbWritten, NULL);
        }
        /* Windows 7 and earlier uses fake handles, with the last two bits set ((hStdOut & 3) == 3). */
        else if (NtWriteFile != NULL && ((uintptr_t)hStdOut & 3) == 0)
        {
            IO_STATUS_BLOCK Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
            NtWriteFile(hStdOut, NULL /*Event*/, NULL /*ApcRoutine*/, NULL /*ApcContext*/,
                        &Ios, (PVOID)pch, (ULONG)cch, NULL /*ByteOffset*/, NULL /*Key*/);
        }
    }
#else
    int res = write(2, pch, cch);
    NOREF(res);
#endif
}


/**
 * Writes a string to standard error.
 *
 * @param   psz                 The string.
 */
static void suplibHardenedPrintStr(const char *psz)
{
    suplibHardenedPrintStrN(psz, suplibHardenedStrLen(psz));
}


/**
 * Writes a char to standard error.
 *
 * @param   ch                  The character value to write.
 */
static void suplibHardenedPrintChr(char ch)
{
    suplibHardenedPrintStrN(&ch, 1);
}

#ifndef IPRT_NO_CRT

/**
 * Writes a decimal number to stdard error.
 *
 * @param   uValue              The value.
 */
static void suplibHardenedPrintDecimal(uint64_t uValue)
{
    char    szBuf[64];
    char   *pszEnd = &szBuf[sizeof(szBuf) - 1];
    char   *psz    = pszEnd;

    *psz-- = '\0';

    do
    {
        *psz-- = '0' + (uValue % 10);
        uValue /= 10;
    } while (uValue > 0);

    psz++;
    suplibHardenedPrintStrN(psz, pszEnd - psz);
}


/**
 * Writes a hexadecimal or octal number to standard error.
 *
 * @param   uValue              The value.
 * @param   uBase               The base (16 or 8).
 * @param   fFlags              Format flags.
 */
static void suplibHardenedPrintHexOctal(uint64_t uValue, unsigned uBase, uint32_t fFlags)
{
    static char const   s_achDigitsLower[17] = "0123456789abcdef";
    static char const   s_achDigitsUpper[17] = "0123456789ABCDEF";
    const char         *pchDigits   = !(fFlags & RTSTR_F_CAPITAL) ? s_achDigitsLower : s_achDigitsUpper;
    unsigned            cShift      = uBase == 16 ?   4 : 3;
    unsigned            fDigitMask  = uBase == 16 ? 0xf : 7;
    char                szBuf[64];
    char               *pszEnd = &szBuf[sizeof(szBuf) - 1];
    char               *psz    = pszEnd;

    *psz-- = '\0';

    do
    {
        *psz-- = pchDigits[uValue & fDigitMask];
        uValue >>= cShift;
    } while (uValue > 0);

    if ((fFlags & RTSTR_F_SPECIAL) && uBase == 16)
    {
        *psz-- = !(fFlags & RTSTR_F_CAPITAL) ? 'x' : 'X';
        *psz-- = '0';
    }

    psz++;
    suplibHardenedPrintStrN(psz, pszEnd - psz);
}


/**
 * Writes a wide character string to standard error.
 *
 * @param   pwsz                The string.
 */
static void suplibHardenedPrintWideStr(PCRTUTF16 pwsz)
{
    for (;;)
    {
        RTUTF16 wc = *pwsz++;
        if (!wc)
            return;
        if (   (wc < 0x7f && wc >= 0x20)
            || wc == '\n'
            || wc == '\r')
            suplibHardenedPrintChr((char)wc);
        else
        {
            suplibHardenedPrintStrN(RT_STR_TUPLE("\\x"));
            suplibHardenedPrintHexOctal(wc, 16, 0);
        }
    }
}

#else /* IPRT_NO_CRT */

/** Buffer structure used by suplibHardenedOutput. */
struct SUPLIBHARDENEDOUTPUTBUF
{
    size_t off;
    char   szBuf[2048];
};

/** Callback for RTStrFormatV, see FNRTSTROUTPUT. */
static DECLCALLBACK(size_t) suplibHardenedOutput(void *pvArg, const char *pachChars, size_t cbChars)
{
    SUPLIBHARDENEDOUTPUTBUF *pBuf = (SUPLIBHARDENEDOUTPUTBUF *)pvArg;
    size_t cbTodo = cbChars;
    for (;;)
    {
        size_t cbSpace = sizeof(pBuf->szBuf) - pBuf->off - 1;

        /* Flush the buffer? */
        if (   cbSpace == 0
            || (cbTodo == 0 && pBuf->off))
        {
            suplibHardenedPrintStrN(pBuf->szBuf, pBuf->off);
# ifdef RT_OS_WINDOWS
            if (g_enmSupR3HardenedMainState >= SUPR3HARDENEDMAINSTATE_WIN_IMPORTS_RESOLVED)
                OutputDebugString(pBuf->szBuf);
# endif
            pBuf->off = 0;
            cbSpace = sizeof(pBuf->szBuf) - 1;
        }

        /* Copy the string into the buffer. */
        if (cbTodo == 1)
        {
            pBuf->szBuf[pBuf->off++] = *pachChars;
            break;
        }
        if (cbSpace >= cbTodo)
        {
            memcpy(&pBuf->szBuf[pBuf->off], pachChars, cbTodo);
            pBuf->off += cbTodo;
            break;
        }
        memcpy(&pBuf->szBuf[pBuf->off], pachChars, cbSpace);
        pBuf->off += cbSpace;
        cbTodo -= cbSpace;
    }
    pBuf->szBuf[pBuf->off] = '\0';

    return cbChars;
}

#endif /* IPRT_NO_CRT */

/**
 * Simple printf to standard error.
 *
 * @param   pszFormat   The format string.
 * @param   va          Arguments to format.
 */
DECLHIDDEN(void) suplibHardenedPrintFV(const char *pszFormat, va_list va)
{
#ifdef IPRT_NO_CRT
    /*
     * Use buffered output here to avoid character mixing on the windows
     * console and to enable us to use OutputDebugString.
     */
    SUPLIBHARDENEDOUTPUTBUF Buf;
    Buf.off = 0;
    Buf.szBuf[0] = '\0';
    RTStrFormatV(suplibHardenedOutput, &Buf, NULL, NULL, pszFormat, va);

#else /* !IPRT_NO_CRT */
    /*
     * Format loop.
     */
    char ch;
    const char *pszLast = pszFormat;
    for (;;)
    {
        ch = *pszFormat;
        if (!ch)
            break;
        pszFormat++;

        if (ch == '%')
        {
            /*
             * Format argument.
             */

            /* Flush unwritten bits. */
            if (pszLast != pszFormat - 1)
                suplibHardenedPrintStrN(pszLast, pszFormat - pszLast - 1);
            pszLast = pszFormat;
            ch = *pszFormat++;

            /* flags. */
            uint32_t fFlags = 0;
            for (;;)
            {
                if (ch == '#')          fFlags |= RTSTR_F_SPECIAL;
                else if (ch == '-')     fFlags |= RTSTR_F_LEFT;
                else if (ch == '+')     fFlags |= RTSTR_F_PLUS;
                else if (ch == ' ')     fFlags |= RTSTR_F_BLANK;
                else if (ch == '0')     fFlags |= RTSTR_F_ZEROPAD;
                else if (ch == '\'')    fFlags |= RTSTR_F_THOUSAND_SEP;
                else                    break;
                ch = *pszFormat++;
            }

            /* Width and precision - ignored. */
            while (RT_C_IS_DIGIT(ch))
                ch = *pszFormat++;
            if (ch == '*')
                va_arg(va, int);
            if (ch == '.')
            {
                do ch = *pszFormat++;
                while (RT_C_IS_DIGIT(ch));
                if (ch == '*')
                    va_arg(va, int);
            }

            /* Size. */
            char chArgSize = 0;
            switch (ch)
            {
                case 'z':
                case 'L':
                case 'j':
                case 't':
                    chArgSize = ch;
                    ch = *pszFormat++;
                    break;

                case 'l':
                    chArgSize = ch;
                    ch = *pszFormat++;
                    if (ch == 'l')
                    {
                        chArgSize = 'L';
                        ch = *pszFormat++;
                    }
                    break;

                case 'h':
                    chArgSize = ch;
                    ch = *pszFormat++;
                    if (ch == 'h')
                    {
                        chArgSize = 'H';
                        ch = *pszFormat++;
                    }
                    break;
            }

            /*
             * Do type specific formatting.
             */
            switch (ch)
            {
                case 'c':
                    ch = (char)va_arg(va, int);
                    suplibHardenedPrintChr(ch);
                    break;

                case 's':
                    if (chArgSize == 'l')
                    {
                        PCRTUTF16 pwszStr = va_arg(va, PCRTUTF16 );
                        if (RT_VALID_PTR(pwszStr))
                            suplibHardenedPrintWideStr(pwszStr);
                        else
                            suplibHardenedPrintStr("<NULL>");
                    }
                    else
                    {
                        const char *pszStr = va_arg(va, const char *);
                        if (!RT_VALID_PTR(pszStr))
                            pszStr = "<NULL>";
                        suplibHardenedPrintStr(pszStr);
                    }
                    break;

                case 'd':
                case 'i':
                {
                    int64_t iValue;
                    if (chArgSize == 'L' || chArgSize == 'j')
                        iValue = va_arg(va, int64_t);
                    else if (chArgSize == 'l')
                        iValue = va_arg(va, signed long);
                    else if (chArgSize == 'z' || chArgSize == 't')
                        iValue = va_arg(va, intptr_t);
                    else
                        iValue = va_arg(va, signed int);
                    if (iValue < 0)
                    {
                        suplibHardenedPrintChr('-');
                        iValue = -iValue;
                    }
                    suplibHardenedPrintDecimal(iValue);
                    break;
                }

                case 'p':
                case 'x':
                case 'X':
                case 'u':
                case 'o':
                {
                    unsigned uBase = 10;
                    uint64_t uValue;

                    switch (ch)
                    {
                        case 'p':
                            fFlags |= RTSTR_F_ZEROPAD; /* Note not standard behaviour (but I like it this way!) */
                            uBase = 16;
                            break;
                        case 'X':
                            fFlags |= RTSTR_F_CAPITAL;
                            RT_FALL_THRU();
                        case 'x':
                            uBase = 16;
                            break;
                        case 'u':
                            uBase = 10;
                            break;
                        case 'o':
                            uBase = 8;
                            break;
                    }

                    if (ch == 'p' || chArgSize == 'z' || chArgSize == 't')
                        uValue = va_arg(va, uintptr_t);
                    else if (chArgSize == 'L' || chArgSize == 'j')
                        uValue = va_arg(va, uint64_t);
                    else if (chArgSize == 'l')
                        uValue = va_arg(va, unsigned long);
                    else
                        uValue = va_arg(va, unsigned int);

                    if (uBase == 10)
                        suplibHardenedPrintDecimal(uValue);
                    else
                        suplibHardenedPrintHexOctal(uValue, uBase, fFlags);
                    break;
                }

                case 'R':
                    if (pszFormat[0] == 'r' && pszFormat[1] == 'c')
                    {
                        int iValue = va_arg(va, int);
                        if (iValue < 0)
                        {
                            suplibHardenedPrintChr('-');
                            iValue = -iValue;
                        }
                        suplibHardenedPrintDecimal(iValue);
                        pszFormat += 2;
                        break;
                    }
                    RT_FALL_THRU();

                /*
                 * Custom format.
                 */
                default:
                    suplibHardenedPrintStr("[bad format: ");
                    suplibHardenedPrintStrN(pszLast, pszFormat - pszLast);
                    suplibHardenedPrintChr(']');
                    break;
            }

            /* continue */
            pszLast = pszFormat;
        }
    }

    /* Flush the last bits of the string. */
    if (pszLast != pszFormat)
        suplibHardenedPrintStrN(pszLast, pszFormat - pszLast);
#endif /* !IPRT_NO_CRT */
}


/**
 * Prints to standard error.
 *
 * @param   pszFormat   The format string.
 * @param   ...         Arguments to format.
 */
DECLHIDDEN(void) suplibHardenedPrintF(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    suplibHardenedPrintFV(pszFormat, va);
    va_end(va);
}


/**
 * @copydoc RTPathStripFilename
 */
static void suplibHardenedPathStripFilename(char *pszPath)
{
    char *psz = pszPath;
    char *pszLastSep = pszPath;

    for (;; psz++)
    {
        switch (*psz)
        {
            /* handle separators. */
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
            case ':':
                pszLastSep = psz + 1;
                break;

            case '\\':
#endif
            case '/':
                pszLastSep = psz;
                break;

            /* the end */
            case '\0':
                if (pszLastSep == pszPath)
                    *pszLastSep++ = '.';
                *pszLastSep = '\0';
                return;
        }
    }
    /* will never get here */
}


DECLHIDDEN(char *) supR3HardenedPathFilename(const char *pszPath)
{
    const char *psz = pszPath;
    const char *pszLastComp = pszPath;

    for (;; psz++)
    {
        switch (*psz)
        {
            /* handle separators. */
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
            case ':':
                pszLastComp = psz + 1;
                break;

            case '\\':
#endif
            case '/':
                pszLastComp = psz + 1;
                break;

            /* the end */
            case '\0':
                if (*pszLastComp)
                    return (char *)(void *)pszLastComp;
                return NULL;
        }
    }

    /* will never get here */
}


DECLHIDDEN(int) supR3HardenedPathAppPrivateNoArch(char *pszPath, size_t cchPath)
{
#if !defined(RT_OS_WINDOWS) && defined(RTPATH_APP_PRIVATE)
    const char *pszSrcPath = RTPATH_APP_PRIVATE;
    size_t cchPathPrivateNoArch = suplibHardenedStrLen(pszSrcPath);
    if (cchPathPrivateNoArch >= cchPath)
        supR3HardenedFatal("supR3HardenedPathAppPrivateNoArch: Buffer overflow, %zu >= %zu\n", cchPathPrivateNoArch, cchPath);
    suplibHardenedMemCopy(pszPath, pszSrcPath, cchPathPrivateNoArch + 1);
    return VINF_SUCCESS;

#else
    return supR3HardenedPathAppBin(pszPath, cchPath);
#endif
}


DECLHIDDEN(int) supR3HardenedPathAppPrivateArch(char *pszPath, size_t cchPath)
{
#if !defined(RT_OS_WINDOWS) && defined(RTPATH_APP_PRIVATE_ARCH)
    const char *pszSrcPath = RTPATH_APP_PRIVATE_ARCH;
    size_t cchPathPrivateArch = suplibHardenedStrLen(pszSrcPath);
    if (cchPathPrivateArch >= cchPath)
        supR3HardenedFatal("supR3HardenedPathAppPrivateArch: Buffer overflow, %zu >= %zu\n", cchPathPrivateArch, cchPath);
    suplibHardenedMemCopy(pszPath, pszSrcPath, cchPathPrivateArch + 1);
    return VINF_SUCCESS;

#else
    return supR3HardenedPathAppBin(pszPath, cchPath);
#endif
}


DECLHIDDEN(int) supR3HardenedPathAppSharedLibs(char *pszPath, size_t cchPath)
{
#if !defined(RT_OS_WINDOWS) && defined(RTPATH_SHARED_LIBS)
    const char *pszSrcPath = RTPATH_SHARED_LIBS;
    size_t cchPathSharedLibs = suplibHardenedStrLen(pszSrcPath);
    if (cchPathSharedLibs >= cchPath)
        supR3HardenedFatal("supR3HardenedPathAppSharedLibs: Buffer overflow, %zu >= %zu\n", cchPathSharedLibs, cchPath);
    suplibHardenedMemCopy(pszPath, pszSrcPath, cchPathSharedLibs + 1);
    return VINF_SUCCESS;

#else
    return supR3HardenedPathAppBin(pszPath, cchPath);
#endif
}


DECLHIDDEN(int) supR3HardenedPathAppDocs(char *pszPath, size_t cchPath)
{
#if !defined(RT_OS_WINDOWS) && defined(RTPATH_APP_DOCS)
    const char *pszSrcPath = RTPATH_APP_DOCS;
    size_t cchPathAppDocs = suplibHardenedStrLen(pszSrcPath);
    if (cchPathAppDocs >= cchPath)
        supR3HardenedFatal("supR3HardenedPathAppDocs: Buffer overflow, %zu >= %zu\n", cchPathAppDocs, cchPath);
    suplibHardenedMemCopy(pszPath, pszSrcPath, cchPathAppDocs + 1);
    return VINF_SUCCESS;

#else
    return supR3HardenedPathAppBin(pszPath, cchPath);
#endif
}


/**
 * Returns the full path to the executable in g_szSupLibHardenedExePath.
 */
static void supR3HardenedGetFullExePath(void)
{
    /*
     * Get the program filename.
     *
     * Most UNIXes have no API for obtaining the executable path, but provides a symbolic
     * link in the proc file system that tells who was exec'ed. The bad thing about this
     * is that we have to use readlink, one of the weirder UNIX APIs.
     *
     * Darwin, OS/2 and Windows all have proper APIs for getting the program file name.
     */
#if defined(RT_OS_LINUX) || defined(RT_OS_FREEBSD) || defined(RT_OS_SOLARIS)
# ifdef RT_OS_LINUX
    int cchLink = readlink("/proc/self/exe", &g_szSupLibHardenedExePath[0], sizeof(g_szSupLibHardenedExePath) - 1);

# elif defined(RT_OS_SOLARIS)
    char szFileBuf[PATH_MAX + 1];
    sprintf(szFileBuf, "/proc/%ld/path/a.out", (long)getpid());
    int cchLink = readlink(szFileBuf, &g_szSupLibHardenedExePath[0], sizeof(g_szSupLibHardenedExePath) - 1);

# else /* RT_OS_FREEBSD */
    int aiName[4];
    aiName[0] = CTL_KERN;
    aiName[1] = KERN_PROC;
    aiName[2] = KERN_PROC_PATHNAME;
    aiName[3] = getpid();

    size_t cbPath = sizeof(g_szSupLibHardenedExePath);
    if (sysctl(aiName, RT_ELEMENTS(aiName), g_szSupLibHardenedExePath, &cbPath, NULL, 0) < 0)
        supR3HardenedFatal("supR3HardenedExecDir: sysctl failed\n");
    g_szSupLibHardenedExePath[sizeof(g_szSupLibHardenedExePath) - 1] = '\0';
    int cchLink = suplibHardenedStrLen(g_szSupLibHardenedExePath); /* paranoid? can't we use cbPath? */

# endif
    if (cchLink < 0 || cchLink == sizeof(g_szSupLibHardenedExePath) - 1)
        supR3HardenedFatal("supR3HardenedExecDir: couldn't read \"%s\", errno=%d cchLink=%d\n",
                            g_szSupLibHardenedExePath, errno, cchLink);
    g_szSupLibHardenedExePath[cchLink] = '\0';

#elif defined(RT_OS_OS2) || defined(RT_OS_L4)
    _execname(g_szSupLibHardenedExePath, sizeof(g_szSupLibHardenedExePath));

#elif defined(RT_OS_DARWIN)
    const char *pszImageName = _dyld_get_image_name(0);
    if (!pszImageName)
        supR3HardenedFatal("supR3HardenedExecDir: _dyld_get_image_name(0) failed\n");
    size_t cchImageName = suplibHardenedStrLen(pszImageName);
    if (!cchImageName || cchImageName >= sizeof(g_szSupLibHardenedExePath))
        supR3HardenedFatal("supR3HardenedExecDir: _dyld_get_image_name(0) failed, cchImageName=%d\n", cchImageName);
    suplibHardenedMemCopy(g_szSupLibHardenedExePath, pszImageName, cchImageName + 1);
    /** @todo abspath the string or this won't work:
     * cd /Applications/VirtualBox.app/Contents/Resources/VirtualBoxVM.app/Contents/MacOS/ && ./VirtualBoxVM --startvm name */

#elif defined(RT_OS_WINDOWS)
    char *pszDst = g_szSupLibHardenedExePath;
    int rc = RTUtf16ToUtf8Ex(g_wszSupLibHardenedExePath, RTSTR_MAX, &pszDst, sizeof(g_szSupLibHardenedExePath), NULL);
    if (RT_FAILURE(rc))
        supR3HardenedFatal("supR3HardenedExecDir: RTUtf16ToUtf8Ex failed, rc=%Rrc\n", rc);
#else
# error needs porting.
#endif

    /*
     * Determine the application binary directory location.
     */
    suplibHardenedStrCopy(g_szSupLibHardenedAppBinPath, g_szSupLibHardenedExePath);
    suplibHardenedPathStripFilename(g_szSupLibHardenedAppBinPath);

    g_offSupLibHardenedExecName = suplibHardenedStrLen(g_szSupLibHardenedAppBinPath);
    while (RTPATH_IS_SEP(g_szSupLibHardenedExePath[g_offSupLibHardenedExecName]))
           g_offSupLibHardenedExecName++;
    g_cchSupLibHardenedExecName = suplibHardenedStrLen(&g_szSupLibHardenedExePath[g_offSupLibHardenedExecName]);

    if (g_enmSupR3HardenedMainState < SUPR3HARDENEDMAINSTATE_HARDENED_MAIN_CALLED)
        supR3HardenedFatal("supR3HardenedExecDir: Called before SUPR3HardenedMain! (%d)\n", g_enmSupR3HardenedMainState);
    switch (g_fSupHardenedMain & SUPSECMAIN_FLAGS_LOC_MASK)
    {
        case SUPSECMAIN_FLAGS_LOC_APP_BIN:
            break;
        case SUPSECMAIN_FLAGS_LOC_TESTCASE:
            suplibHardenedPathStripFilename(g_szSupLibHardenedAppBinPath);
            break;
#ifdef RT_OS_DARWIN
        case SUPSECMAIN_FLAGS_LOC_OSX_HLP_APP:
        {
            /* We must ascend to the parent bundle's Contents directory then decend into its MacOS: */
            static const RTSTRTUPLE s_aComponentsToSkip[] =
            { { RT_STR_TUPLE("MacOS") }, { RT_STR_TUPLE("Contents") }, { NULL /*some.app*/, 0 }, { RT_STR_TUPLE("Resources") } };
            size_t cchPath = suplibHardenedStrLen(g_szSupLibHardenedAppBinPath);
            for (uintptr_t i = 0; i < RT_ELEMENTS(s_aComponentsToSkip); i++)
            {
                while (cchPath > 1 && g_szSupLibHardenedAppBinPath[cchPath - 1] == '/')
                    cchPath--;
                size_t const cchMatch = s_aComponentsToSkip[i].cch;
                if (cchMatch > 0)
                {
                    if (   cchPath >= cchMatch + sizeof("VirtualBox.app/Contents")
                        && g_szSupLibHardenedAppBinPath[cchPath - cchMatch - 1] == '/'
                        && suplibHardenedMemComp(&g_szSupLibHardenedAppBinPath[cchPath - cchMatch],
                                                 s_aComponentsToSkip[i].psz, cchMatch) == 0)
                        cchPath -= cchMatch;
                    else
                        supR3HardenedFatal("supR3HardenedExecDir: Bad helper app path (tail component #%u '%s'): %s\n",
                                           i, s_aComponentsToSkip[i].psz, g_szSupLibHardenedAppBinPath);
                }
                else if (   cchPath > g_cchSupLibHardenedExecName  + sizeof("VirtualBox.app/Contents/Resources/.app")
                         && suplibHardenedMemComp(&g_szSupLibHardenedAppBinPath[cchPath - 4], ".app", 4) == 0
                         && suplibHardenedMemComp(&g_szSupLibHardenedAppBinPath[cchPath - 4 - g_cchSupLibHardenedExecName],
                                                  &g_szSupLibHardenedExePath[g_offSupLibHardenedExecName],
                                                  g_cchSupLibHardenedExecName) == 0)
                    cchPath -= g_cchSupLibHardenedExecName + 4;
                else
                    supR3HardenedFatal("supR3HardenedExecDir: Bad helper app path (tail component #%u '%s.app'): %s\n",
                                       i, &g_szSupLibHardenedExePath[g_offSupLibHardenedExecName], g_szSupLibHardenedAppBinPath);
            }
            suplibHardenedMemCopy(&g_szSupLibHardenedAppBinPath[cchPath], "MacOS", sizeof("MacOS"));
            break;
        }
#endif /* RT_OS_DARWIN */
        default:
            supR3HardenedFatal("supR3HardenedExecDir: Unknown program binary location: %#x\n", g_fSupHardenedMain);
    }

#ifdef RTPATH_APP_PRIVATE_ARCH
    /*
     * If the location is fixed, do not continue if it is not correct. Binaries
     * must not be allowed to be started from anywhere else.  (@bugref{10626})
     */
    if (suplibHardenedStrCmp(g_szSupLibHardenedAppBinPath, RTPATH_APP_PRIVATE_ARCH) != 0)
        supR3HardenedFatal("supR3HardenedExecDir: Invalid program binary location: %s (expected %s)\n",
                           g_szSupLibHardenedAppBinPath, RTPATH_APP_PRIVATE_ARCH);
# ifdef RT_OS_WINDOWS
#  error "Didn't expect RTPATH_APP_PRIVATE_ARCH to be defined on Windows."
# endif
#elif defined(RT_OS_LINUX) || defined(RT_OS_FREEBSD) || defined(RT_OS_SOLARIS) || defined(RT_OS_DARWIN)
# error "Expected RTPATH_APP_PRIVATE_ARCH to be define on this host."
#endif
}


#ifdef RT_OS_LINUX
/**
 * Checks if we can read /proc/self/exe.
 *
 * This is used on linux to see if we have to call init
 * with program path or not.
 *
 * @returns true / false.
 */
static bool supR3HardenedMainIsProcSelfExeAccssible(void)
{
    char szPath[RTPATH_MAX];
    int cchLink = readlink("/proc/self/exe", szPath, sizeof(szPath));
    return cchLink != -1;
}
#endif /* RT_OS_LINUX */



/**
 * @remarks not quite like RTPathExecDir actually...
 */
DECLHIDDEN(int) supR3HardenedPathAppBin(char *pszPath, size_t cchPath)
{
    /*
     * Lazy init (probably not required).
     */
    if (!g_szSupLibHardenedAppBinPath[0])
        supR3HardenedGetFullExePath();

    /*
     * Calc the length and check if there is space before copying.
     */
    size_t cch = suplibHardenedStrLen(g_szSupLibHardenedAppBinPath) + 1;
    if (cch <= cchPath)
    {
        suplibHardenedMemCopy(pszPath, g_szSupLibHardenedAppBinPath, cch + 1);
        return VINF_SUCCESS;
    }

    supR3HardenedFatal("supR3HardenedPathAppBin: Buffer too small (%u < %u)\n", cchPath, cch);
    /* not reached */
}


#ifdef RT_OS_WINDOWS
extern "C" uint32_t g_uNtVerCombined;
#endif

DECLHIDDEN(void) supR3HardenedOpenLog(int *pcArgs, char **papszArgs)
{
    static const char s_szLogOption[] = "--sup-hardening-log=";

    /*
     * Scan the argument vector.
     */
    int cArgs = *pcArgs;
    for (int iArg = 1; iArg < cArgs; iArg++)
        if (strncmp(papszArgs[iArg], s_szLogOption, sizeof(s_szLogOption) - 1) == 0)
        {
#ifdef RT_OS_WINDOWS
            const char *pszLogFile = &papszArgs[iArg][sizeof(s_szLogOption) - 1];
#endif

            /*
             * Drop the argument from the vector (has trailing NULL entry).
             */
//            memmove(&papszArgs[iArg], &papszArgs[iArg + 1], (cArgs - iArg) * sizeof(papszArgs[0]));
            *pcArgs -= 1;
            cArgs   -= 1;

            /*
             * Open the log file, unless we've already opened one.
             * First argument takes precedence
             */
#ifdef RT_OS_WINDOWS
            if (g_hStartupLog == NULL)
            {
                int rc = RTNtPathOpen(pszLogFile,
                                      GENERIC_WRITE | SYNCHRONIZE,
                                      FILE_ATTRIBUTE_NORMAL,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      FILE_OPEN_IF,
                                      FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                                      OBJ_CASE_INSENSITIVE,
                                      &g_hStartupLog,
                                      NULL);
                if (RT_SUCCESS(rc))
                {
//                    SUP_DPRINTF(("Log file opened: " VBOX_VERSION_STRING "r%u g_hStartupLog=%p g_uNtVerCombined=%#x\n",
//                                 VBOX_SVN_REV, g_hStartupLog, g_uNtVerCombined));

                    /*
                     * If the path contains a drive volume, save it so we can
                     * use it to flush the volume containing the log file.
                     */
                    if (RT_C_IS_ALPHA(pszLogFile[0]) && pszLogFile[1] == ':')
                    {
//                        RTUtf16CopyAscii(g_wszStartupLogVol, RT_ELEMENTS(g_wszStartupLogVol), "\\??\\");
                        g_wszStartupLogVol[sizeof("\\??\\") - 1] = RT_C_TO_UPPER(pszLogFile[0]);
                        g_wszStartupLogVol[sizeof("\\??\\") + 0] = ':';
                        g_wszStartupLogVol[sizeof("\\??\\") + 1] = '\0';
                    }
                }
                else
                    g_hStartupLog = NULL;
            }
#else
            /* Just some mumbo jumbo to shut up the compiler. */
            g_hStartupLog  -= 1;
            g_cbStartupLog += 1;
            //g_hStartupLog = open()
#endif
        }
}


DECLHIDDEN(void) supR3HardenedLogV(const char *pszFormat, va_list va)
{
#ifdef RT_OS_WINDOWS
    if (   g_hStartupLog != NULL
        && g_cbStartupLog < 16*_1M)
    {
        char szBuf[5120];
        PCLIENT_ID pSelfId = &((PTEB)NtCurrentTeb())->ClientId;
        size_t cchPrefix = RTStrPrintf(szBuf, sizeof(szBuf), "%x.%x: ", pSelfId->UniqueProcess, pSelfId->UniqueThread);
        size_t cch = RTStrPrintfV(&szBuf[cchPrefix], sizeof(szBuf) - cchPrefix, pszFormat, va) + cchPrefix;

        if ((size_t)cch >= sizeof(szBuf))
            cch = sizeof(szBuf) - 1;

        if (!cch || szBuf[cch - 1] != '\n')
            szBuf[cch++] = '\n';

        ASMAtomicAddU32(&g_cbStartupLog, (uint32_t)cch);

        IO_STATUS_BLOCK Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
        LARGE_INTEGER   Offset;
        Offset.QuadPart = -1; /* Write to end of file. */
        NtWriteFile(g_hStartupLog, NULL /*Event*/, NULL /*ApcRoutine*/, NULL /*ApcContext*/,
                    &Ios, szBuf, (ULONG)cch, &Offset, NULL /*Key*/);
    }
#else
    RT_NOREF(pszFormat, va);
    /* later */
#endif
}


DECLHIDDEN(void) supR3HardenedLog(const char *pszFormat,  ...)
{
    va_list va;
    va_start(va, pszFormat);
    supR3HardenedLogV(pszFormat, va);
    va_end(va);
}


DECLHIDDEN(void) supR3HardenedLogFlush(void)
{
#ifdef RT_OS_WINDOWS
    if (   g_hStartupLog != NULL
        && g_cbStartupLog < 16*_1M)
    {
        IO_STATUS_BLOCK Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
        NTSTATUS rcNt = NtFlushBuffersFile(g_hStartupLog, &Ios);

        /*
         * Try flush the volume containing the log file too.
         */
        if (g_wszStartupLogVol[0])
        {
            HANDLE              hLogVol = RTNT_INVALID_HANDLE_VALUE;
            UNICODE_STRING      NtName;
            NtName.Buffer        = g_wszStartupLogVol;
            NtName.Length        = (USHORT)(RTUtf16Len(g_wszStartupLogVol) * sizeof(RTUTF16));
            NtName.MaximumLength = NtName.Length + 1;
            OBJECT_ATTRIBUTES   ObjAttr;
            InitializeObjectAttributes(&ObjAttr, &NtName, OBJ_CASE_INSENSITIVE, NULL /*hRootDir*/, NULL /*pSecDesc*/);
            RTNT_IO_STATUS_BLOCK_REINIT(&Ios);
            rcNt = NtCreateFile(&hLogVol,
                                GENERIC_WRITE | GENERIC_READ | SYNCHRONIZE | FILE_READ_ATTRIBUTES,
                                &ObjAttr,
                                &Ios,
                                NULL /* Allocation Size*/,
                                0 /*FileAttributes*/,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                FILE_OPEN,
                                FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                                NULL /*EaBuffer*/,
                                0 /*EaLength*/);
            if (NT_SUCCESS(rcNt))
                rcNt = Ios.Status;
            if (NT_SUCCESS(rcNt))
            {
                RTNT_IO_STATUS_BLOCK_REINIT(&Ios);
                rcNt = NtFlushBuffersFile(hLogVol, &Ios);
                NtClose(hLogVol);
            }
            else
            {
                /* This may have sideeffects similar to what we want... */
                hLogVol = RTNT_INVALID_HANDLE_VALUE;
                RTNT_IO_STATUS_BLOCK_REINIT(&Ios);
                rcNt = NtCreateFile(&hLogVol,
                                    GENERIC_READ | SYNCHRONIZE | FILE_READ_ATTRIBUTES,
                                    &ObjAttr,
                                    &Ios,
                                    NULL /* Allocation Size*/,
                                    0 /*FileAttributes*/,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    FILE_OPEN,
                                    FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                                    NULL /*EaBuffer*/,
                                    0 /*EaLength*/);
                if (NT_SUCCESS(rcNt) && NT_SUCCESS(Ios.Status))
                    NtClose(hLogVol);
            }
        }
    }
#else
    /* later */
#endif
}


/**
 * Prints the message prefix.
 */
static void suplibHardenedPrintPrefix(void)
{
    if (g_pszSupLibHardenedProgName)
        suplibHardenedPrintStr(g_pszSupLibHardenedProgName);
    suplibHardenedPrintStr(": ");
}


DECL_NO_RETURN(DECLHIDDEN(void)) supR3HardenedFatalMsgV(const char *pszWhere, SUPINITOP enmWhat, int rc,
                                                        const char *pszMsgFmt, va_list va)
{
    /*
     * First to the log.
     */
    supR3HardenedLog("Error %d in %s! (enmWhat=%d)\n", rc, pszWhere, enmWhat);
    va_list vaCopy;
    va_copy(vaCopy, va);
    supR3HardenedLogV(pszMsgFmt, vaCopy);
    va_end(vaCopy);

#ifdef RT_OS_WINDOWS
    /*
     * The release log.
     */
    if (g_pfnRTLogRelPrintf)
    {
        va_copy(vaCopy, va);
        g_pfnRTLogRelPrintf("supR3HardenedFatalMsgV: %s enmWhat=%d rc=%Rrc (%#x)\n", pszWhere, enmWhat, rc);
        g_pfnRTLogRelPrintf("supR3HardenedFatalMsgV: %N\n", pszMsgFmt, &vaCopy);
        va_end(vaCopy);
    }
#endif

    /*
     * Then to the console.
     */
    suplibHardenedPrintPrefix();
    suplibHardenedPrintF("Error %d in %s!\n", rc, pszWhere);

    suplibHardenedPrintPrefix();
    va_copy(vaCopy, va);
    suplibHardenedPrintFV(pszMsgFmt, vaCopy);
    va_end(vaCopy);
    suplibHardenedPrintChr('\n');

    switch (enmWhat)
    {
        case kSupInitOp_Driver:
            suplibHardenedPrintChr('\n');
            suplibHardenedPrintPrefix();
            suplibHardenedPrintStr("Tip! Make sure the kernel module is loaded. It may also help to reinstall VirtualBox.\n");
            break;

        case kSupInitOp_Misc:
        case kSupInitOp_IPRT:
        case kSupInitOp_Integrity:
        case kSupInitOp_RootCheck:
            suplibHardenedPrintChr('\n');
            suplibHardenedPrintPrefix();
            suplibHardenedPrintStr("Tip! It may help to reinstall VirtualBox.\n");
            break;

        default:
            /* no hints here */
            break;
    }

    /*
     * Finally, TrustedError if appropriate.
     */
    if (g_enmSupR3HardenedMainState >= SUPR3HARDENEDMAINSTATE_WIN_IMPORTS_RESOLVED)
    {
#ifdef SUP_HARDENED_SUID
        /* Drop any root privileges we might be holding, this won't return
           if it fails but end up calling supR3HardenedFatal[V]. */
        supR3HardenedMainDropPrivileges();
#endif
        /* Close the driver, if we succeeded opening it.  Both because
           TrustedError may be untrustworthy and because the driver deosn't
           like us if we fork().  @bugref{8838} */
        suplibOsTerm(&g_SupPreInitData.Data);

        /*
         * Now try resolve and call the TrustedError entry point if we can find it.
         * Note! Loader involved, so we must guard against loader hooks calling us.
         */
        static volatile bool s_fRecursive = false;
        if (!s_fRecursive)
        {
            s_fRecursive = true;

            PFNSUPTRUSTEDERROR pfnTrustedError = supR3HardenedMainGetTrustedError(g_pszSupLibHardenedProgName);
            if (pfnTrustedError)
            {
                /* We'll fork before we make the call because that way the session management
                   in main will see us exiting immediately (if it's involved with us) and possibly
                   get an error back to the API / user. */
#if !defined(RT_OS_WINDOWS) && !defined(RT_OS_OS2) && /* @bugref{10170}: */ !defined(RT_OS_DARWIN)
                int pid = fork();
                if (pid <= 0)
#endif
                {
                    pfnTrustedError(pszWhere, enmWhat, rc, pszMsgFmt, va);
                }
            }

            s_fRecursive = false;
        }
    }
#if defined(RT_OS_WINDOWS)
    /*
     * Report the error to the parent if this happens during early VM init.
     */
    else if (   g_enmSupR3HardenedMainState < SUPR3HARDENEDMAINSTATE_WIN_IMPORTS_RESOLVED
             && g_enmSupR3HardenedMainState != SUPR3HARDENEDMAINSTATE_NOT_YET_CALLED)
        supR3HardenedWinReportErrorToParent(pszWhere, enmWhat, rc, pszMsgFmt, va);
#endif

    /*
     * Quit
     */
    suplibHardenedExit(RTEXITCODE_FAILURE);
}


DECL_NO_RETURN(DECLHIDDEN(void)) supR3HardenedFatalMsg(const char *pszWhere, SUPINITOP enmWhat, int rc,
                                                       const char *pszMsgFmt, ...)
{
    va_list va;
    va_start(va, pszMsgFmt);
    supR3HardenedFatalMsgV(pszWhere, enmWhat, rc, pszMsgFmt, va);
    /* not reached */
}


DECL_NO_RETURN(DECLHIDDEN(void)) supR3HardenedFatalV(const char *pszFormat, va_list va)
{
    supR3HardenedLog("Fatal error:\n");
    va_list vaCopy;
    va_copy(vaCopy, va);
    supR3HardenedLogV(pszFormat, vaCopy);
    va_end(vaCopy);

#if defined(RT_OS_WINDOWS)
    /*
     * Report the error to the parent if this happens during early VM init.
     */
    if (   g_enmSupR3HardenedMainState < SUPR3HARDENEDMAINSTATE_WIN_IMPORTS_RESOLVED
        && g_enmSupR3HardenedMainState != SUPR3HARDENEDMAINSTATE_NOT_YET_CALLED)
        supR3HardenedWinReportErrorToParent(NULL, kSupInitOp_Invalid, VERR_INTERNAL_ERROR, pszFormat, va);
    else
#endif
    {
#ifdef RT_OS_WINDOWS
        if (g_pfnRTLogRelPrintf)
        {
            va_copy(vaCopy, va);
            g_pfnRTLogRelPrintf("supR3HardenedFatalV: %N", pszFormat, &vaCopy);
            va_end(vaCopy);
        }
#endif

        suplibHardenedPrintPrefix();
        suplibHardenedPrintFV(pszFormat, va);
    }

    suplibHardenedExit(RTEXITCODE_FAILURE);
}


DECL_NO_RETURN(DECLHIDDEN(void)) supR3HardenedFatal(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    supR3HardenedFatalV(pszFormat, va);
    /* not reached */
}


DECLHIDDEN(int) supR3HardenedErrorV(int rc, bool fFatal, const char *pszFormat, va_list va)
{
    if (fFatal)
        supR3HardenedFatalV(pszFormat, va);

    supR3HardenedLog("Error (rc=%d):\n", rc);
    va_list vaCopy;
    va_copy(vaCopy, va);
    supR3HardenedLogV(pszFormat, vaCopy);
    va_end(vaCopy);

#ifdef RT_OS_WINDOWS
    if (g_pfnRTLogRelPrintf)
    {
        va_copy(vaCopy, va);
        g_pfnRTLogRelPrintf("supR3HardenedErrorV: %N", pszFormat, &vaCopy);
        va_end(vaCopy);
    }
#endif

    suplibHardenedPrintPrefix();
    suplibHardenedPrintFV(pszFormat, va);

    return rc;
}


DECLHIDDEN(int) supR3HardenedError(int rc, bool fFatal, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    supR3HardenedErrorV(rc, fFatal, pszFormat, va);
    va_end(va);
    return rc;
}



/**
 * Attempts to open /dev/vboxdrv (or equvivalent).
 *
 * @remarks This function will not return on failure.
 */
DECLHIDDEN(void) supR3HardenedMainOpenDevice(void)
{
    RTERRINFOSTATIC ErrInfo;
    SUPINITOP       enmWhat = kSupInitOp_Driver;
    uint32_t        fFlags  = SUPR3INIT_F_UNRESTRICTED;
    if (g_fSupHardenedMain & SUPSECMAIN_FLAGS_DRIVERLESS)
        fFlags |= SUPR3INIT_F_DRIVERLESS;
    if (g_fSupHardenedMain & SUPSECMAIN_FLAGS_DRIVERLESS_IEM_ALLOWED)
        fFlags |= SUPR3INIT_F_DRIVERLESS_IEM_ALLOWED;
#ifdef VBOX_WITH_DRIVERLESS_NEM_FALLBACK
    if (g_fSupHardenedMain & SUPSECMAIN_FLAGS_DRIVERLESS_NEM_FALLBACK)
        fFlags |= SUPR3INIT_F_DRIVERLESS_NEM_FALLBACK;
#endif
    int rc = suplibOsInit(&g_SupPreInitData.Data, false /*fPreInit*/, fFlags, &enmWhat, RTErrInfoInitStatic(&ErrInfo));
    if (RT_SUCCESS(rc))
        return;

    if (RTErrInfoIsSet(&ErrInfo.Core))
        supR3HardenedFatalMsg("suplibOsInit", enmWhat, rc, "%s", ErrInfo.szMsg);

    switch (rc)
    {
        /** @todo better messages! */
        case VERR_VM_DRIVER_NOT_INSTALLED:
            supR3HardenedFatalMsg("suplibOsInit", kSupInitOp_Driver, rc, "Kernel driver not installed");
        case VERR_VM_DRIVER_NOT_ACCESSIBLE:
            supR3HardenedFatalMsg("suplibOsInit", kSupInitOp_Driver, rc, "Kernel driver not accessible");
        case VERR_VM_DRIVER_LOAD_ERROR:
            supR3HardenedFatalMsg("suplibOsInit", kSupInitOp_Driver, rc, "VERR_VM_DRIVER_LOAD_ERROR");
        case VERR_VM_DRIVER_OPEN_ERROR:
            supR3HardenedFatalMsg("suplibOsInit", kSupInitOp_Driver, rc, "VERR_VM_DRIVER_OPEN_ERROR");
        case VERR_VM_DRIVER_VERSION_MISMATCH:
            supR3HardenedFatalMsg("suplibOsInit", kSupInitOp_Driver, rc, "Kernel driver version mismatch");
        case VERR_ACCESS_DENIED:
            supR3HardenedFatalMsg("suplibOsInit", kSupInitOp_Driver, rc, "VERR_ACCESS_DENIED");
        case VERR_NO_MEMORY:
            supR3HardenedFatalMsg("suplibOsInit", kSupInitOp_Driver, rc, "Kernel memory allocation/mapping failed");
        case VERR_SUPDRV_HARDENING_EVIL_HANDLE:
            supR3HardenedFatalMsg("suplibOsInit", kSupInitOp_Integrity, rc, "VERR_SUPDRV_HARDENING_EVIL_HANDLE");
        case VERR_SUPLIB_NT_PROCESS_UNTRUSTED_0:
            supR3HardenedFatalMsg("suplibOsInit", kSupInitOp_Integrity, rc, "VERR_SUPLIB_NT_PROCESS_UNTRUSTED_0");
        case VERR_SUPLIB_NT_PROCESS_UNTRUSTED_1:
            supR3HardenedFatalMsg("suplibOsInit", kSupInitOp_Integrity, rc, "VERR_SUPLIB_NT_PROCESS_UNTRUSTED_1");
        case VERR_SUPLIB_NT_PROCESS_UNTRUSTED_2:
            supR3HardenedFatalMsg("suplibOsInit", kSupInitOp_Integrity, rc, "VERR_SUPLIB_NT_PROCESS_UNTRUSTED_2");
        default:
            supR3HardenedFatalMsg("suplibOsInit", kSupInitOp_Driver, rc, "Unknown rc=%d (%Rrc)", rc, rc);
    }
}


#ifdef SUP_HARDENED_SUID

/**
 * Grabs extra non-root capabilities / privileges that we might require.
 *
 * This is currently only used for being able to do ICMP from the NAT engine
 * and for being able to raise thread scheduling priority
 *
 * @note We still have root privileges at the time of this call.
 */
static void supR3HardenedMainGrabCapabilites(void)
{
# if defined(RT_OS_LINUX)
    /*
     * We are about to drop all our privileges. Remove all capabilities but
     * keep the cap_net_raw capability for ICMP sockets for the NAT stack,
     * also keep cap_sys_nice capability for priority tweaking.
     */
    if (g_uCaps != 0)
    {
#  ifdef USE_LIB_PCAP
        /* XXX cap_net_bind_service */
        if (!cap_set_proc(cap_from_text("all-eip cap_net_raw+ep cap_sys_nice+ep")))
            prctl(PR_SET_KEEPCAPS, 1 /*keep=*/, 0, 0, 0);
        prctl(PR_SET_DUMPABLE, 1 /*dump*/, 0, 0, 0);
#  else
        cap_user_header_t hdr = (cap_user_header_t)alloca(sizeof(*hdr));
        cap_user_data_t   cap = (cap_user_data_t)alloca(2 /*_LINUX_CAPABILITY_U32S_3*/ * sizeof(*cap));
        memset(hdr, 0, sizeof(*hdr));
        capget(hdr, NULL);
        if (   hdr->version != 0x19980330 /* _LINUX_CAPABILITY_VERSION_1, _LINUX_CAPABILITY_U32S_1 = 1 */
            && hdr->version != 0x20071026 /* _LINUX_CAPABILITY_VERSION_2, _LINUX_CAPABILITY_U32S_2 = 2 */
            && hdr->version != 0x20080522 /* _LINUX_CAPABILITY_VERSION_3, _LINUX_CAPABILITY_U32S_3 = 2 */)
            hdr->version = _LINUX_CAPABILITY_VERSION;
        g_uCapsVersion = hdr->version;
        memset(cap, 0, 2 /* _LINUX_CAPABILITY_U32S_3 */ * sizeof(*cap));
        cap->effective = g_uCaps;
        cap->permitted = g_uCaps;
        if (!capset(hdr, cap))
            prctl(PR_SET_KEEPCAPS, 1 /*keep*/, 0, 0, 0);
        prctl(PR_SET_DUMPABLE, 1 /*dump*/, 0, 0, 0);
#  endif /* !USE_LIB_PCAP */
    }

# elif defined(RT_OS_SOLARIS)
    /*
     * Add net_icmpaccess privilege to effective privileges and limit
     * permitted privileges before completely dropping root privileges.
     * This requires dropping root privileges temporarily to get the normal
     * user's privileges.
     */
    seteuid(g_uid);
    priv_set_t *pPrivEffective = priv_allocset();
    priv_set_t *pPrivNew = priv_allocset();
    if (pPrivEffective && pPrivNew)
    {
        int rc = getppriv(PRIV_EFFECTIVE, pPrivEffective);
        seteuid(0);
        if (!rc)
        {
            priv_copyset(pPrivEffective, pPrivNew);
            rc = priv_addset(pPrivNew, PRIV_NET_ICMPACCESS);
            if (!rc)
            {
                /* Order is important, as one can't set a privilege which is
                 * not in the permitted privilege set. */
                rc = setppriv(PRIV_SET, PRIV_EFFECTIVE, pPrivNew);
                if (rc)
                    supR3HardenedError(rc, false, "SUPR3HardenedMain: failed to set effective privilege set.\n");
                rc = setppriv(PRIV_SET, PRIV_PERMITTED, pPrivNew);
                if (rc)
                    supR3HardenedError(rc, false, "SUPR3HardenedMain: failed to set permitted privilege set.\n");
            }
            else
                supR3HardenedError(rc, false, "SUPR3HardenedMain: failed to add NET_ICMPACCESS privilege.\n");
        }
    }
    else
    {
        /* for memory allocation failures just continue */
        seteuid(0);
    }

    if (pPrivEffective)
        priv_freeset(pPrivEffective);
    if (pPrivNew)
        priv_freeset(pPrivNew);
# endif
}

/*
 * Look at the environment for some special options.
 */
static void supR3GrabOptions(void)
{
# ifdef RT_OS_LINUX
    g_uCaps = 0;

    /*
     * Do _not_ perform any capability-related system calls for root processes
     * (leaving g_uCaps at 0).
     * (Hint: getuid gets the real user id, not the effective.)
     */
    if (getuid() != 0)
    {
        /*
         * CAP_NET_RAW.
         * Default: enabled.
         * Can be disabled with 'export VBOX_HARD_CAP_NET_RAW=0'.
         */
        const char *pszOpt = getenv("VBOX_HARD_CAP_NET_RAW");
        if (   !pszOpt
            || memcmp(pszOpt, "0", sizeof("0")) != 0)
            g_uCaps = CAP_TO_MASK(CAP_NET_RAW);

        /*
         * CAP_NET_BIND_SERVICE.
         * Default: disabled.
         * Can be enabled with 'export VBOX_HARD_CAP_NET_BIND_SERVICE=1'.
         */
        pszOpt = getenv("VBOX_HARD_CAP_NET_BIND_SERVICE");
        if (   pszOpt
            && memcmp(pszOpt, "0", sizeof("0")) != 0)
            g_uCaps |= CAP_TO_MASK(CAP_NET_BIND_SERVICE);

        /*
         * CAP_SYS_NICE.
         * Default: enabled.
         * Can be disabled with 'export VBOX_HARD_CAP_SYS_NICE=0'.
         */
        pszOpt = getenv("VBOX_HARD_CAP_SYS_NICE");
        if (   !pszOpt
            || memcmp(pszOpt, "0", sizeof("0")) != 0)
            g_uCaps |= CAP_TO_MASK(CAP_SYS_NICE);
    }
# endif
}

/**
 * Drop any root privileges we might be holding.
 */
static void supR3HardenedMainDropPrivileges(void)
{
    /*
     * Try use setre[ug]id since this will clear the save uid/gid and thus
     * leave fewer traces behind that libs like GTK+ may pick up.
     */
    uid_t euid, ruid, suid;
    gid_t egid, rgid, sgid;
# if defined(RT_OS_DARWIN)
    /* The really great thing here is that setreuid isn't available on
       OS X 10.4, libc emulates it. While 10.4 have a slightly different and
       non-standard setuid implementation compared to 10.5, the following
       works the same way with both version since we're super user (10.5 req).
       The following will set all three variants of the group and user IDs. */
    setgid(g_gid);
    setuid(g_uid);
    euid = geteuid();
    ruid = suid = getuid();
    egid = getegid();
    rgid = sgid = getgid();

# elif defined(RT_OS_SOLARIS)
    /* Solaris doesn't have setresuid, but the setreuid interface is BSD
       compatible and will set the saved uid to euid when we pass it a ruid
       that isn't -1 (which we do). */
    setregid(g_gid, g_gid);
    setreuid(g_uid, g_uid);
    euid = geteuid();
    ruid = suid = getuid();
    egid = getegid();
    rgid = sgid = getgid();

# else
    /* This is the preferred one, full control no questions about semantics.
       PORTME: If this isn't work, try join one of two other gangs above. */
    int res = setresgid(g_gid, g_gid, g_gid);
    NOREF(res);
    res = setresuid(g_uid, g_uid, g_uid);
    NOREF(res);
    if (getresuid(&ruid, &euid, &suid) != 0)
    {
        euid = geteuid();
        ruid = suid = getuid();
    }
    if (getresgid(&rgid, &egid, &sgid) != 0)
    {
        egid = getegid();
        rgid = sgid = getgid();
    }
# endif


    /* Check that it worked out all right. */
    if (    euid != g_uid
        ||  ruid != g_uid
        ||  suid != g_uid
        ||  egid != g_gid
        ||  rgid != g_gid
        ||  sgid != g_gid)
        supR3HardenedFatal("SUPR3HardenedMain: failed to drop root privileges!"
                           " (euid=%d ruid=%d suid=%d  egid=%d rgid=%d sgid=%d; wanted uid=%d and gid=%d)\n",
                           euid, ruid, suid, egid, rgid, sgid, g_uid, g_gid);

# if RT_OS_LINUX
    /*
     * Re-enable the cap_net_raw and cap_sys_nice capabilities which were disabled during setresuid.
     */
    if (g_uCaps != 0)
    {
#  ifdef USE_LIB_PCAP
        /** @todo Warn if that does not work? */
        /* XXX cap_net_bind_service */
        cap_set_proc(cap_from_text("cap_net_raw+ep cap_sys_nice+ep"));
#  else
        cap_user_header_t hdr = (cap_user_header_t)alloca(sizeof(*hdr));
        cap_user_data_t   cap = (cap_user_data_t)alloca(2 /* _LINUX_CAPABILITY_U32S_3 */ * sizeof(*cap));
        memset(hdr, 0, sizeof(*hdr));
        hdr->version = g_uCapsVersion;
        memset(cap, 0, 2 /* _LINUX_CAPABILITY_U32S_3 */ * sizeof(*cap));
        cap->effective = g_uCaps;
        cap->permitted = g_uCaps;
        /** @todo Warn if that does not work? */
        capset(hdr, cap);
#  endif /* !USE_LIB_PCAP */
    }
# endif
}

#endif /* SUP_HARDENED_SUID */

/**
 * Purge the process environment from any environment vairable which can lead
 * to loading untrusted binaries compromising the process address space.
 *
 * @param   envp        The initial environment vector. (Can be NULL.)
 */
static void supR3HardenedMainPurgeEnvironment(char **envp)
{
    for (unsigned i = 0; i < RT_ELEMENTS(g_aSupEnvPurgeDescs); i++)
    {
        /*
         * Update the initial environment vector, just in case someone actually cares about it.
         */
        if (envp)
        {
            const char * const  pszEnv = g_aSupEnvPurgeDescs[i].pszEnv;
            size_t const        cchEnv = g_aSupEnvPurgeDescs[i].cchEnv;
            unsigned            iSrc   = 0;
            unsigned            iDst   = 0;
            char               *pszTmp;

            while ((pszTmp = envp[iSrc]) != NULL)
            {
                if (   memcmp(pszTmp, pszEnv, cchEnv) != 0
                    || (pszTmp[cchEnv] != '=' && pszTmp[cchEnv] != '\0'))
                {
                    if (iDst != iSrc)
                        envp[iDst] = pszTmp;
                    iDst++;
                }
                else
                    SUP_DPRINTF(("supR3HardenedMainPurgeEnvironment: dropping envp[%d]=%s\n", iSrc, pszTmp));
                iSrc++;
            }

            if (iDst != iSrc)
                while (iDst <= iSrc)
                    envp[iDst++] = NULL;
        }

        /*
         * Remove from the process environment if present.
         */
#ifndef RT_OS_WINDOWS
        const char *pszTmp = getenv(g_aSupEnvPurgeDescs[i].pszEnv);
        if (pszTmp != NULL)
        {
            if (unsetenv((char *)g_aSupEnvPurgeDescs[i].pszEnv) == 0)
                SUP_DPRINTF(("supR3HardenedMainPurgeEnvironment: dropped %s\n", pszTmp));
            else
                if (g_aSupEnvPurgeDescs[i].fPurgeErrFatal)
                    supR3HardenedFatal("SUPR3HardenedMain: failed to purge %s environment variable! (errno=%d %s)\n",
                                       g_aSupEnvPurgeDescs[i].pszEnv, errno, strerror(errno));
                else
                    SUP_DPRINTF(("supR3HardenedMainPurgeEnvironment: dropping %s failed! errno=%d\n", pszTmp, errno));
        }
#else
        /** @todo Call NT API to do the same. */
#endif
    }
}


/**
 * Returns the argument purge descriptor of the given argument if available.
 *
 * @retval 0 if it should not be purged.
 * @retval 1 if it only the current argument should be purged.
 * @retval 2 if the argument and the following (if present) should be purged.
 * @param   pszArg           The argument to look for.
 */
static unsigned supR3HardenedMainShouldPurgeArg(const char *pszArg)
{
    for (unsigned i = 0; i < RT_ELEMENTS(g_aSupArgPurgeDescs); i++)
    {
        size_t const cchPurge = g_aSupArgPurgeDescs[i].cchArg;
        if (!memcmp(pszArg, g_aSupArgPurgeDescs[i].pszArg, cchPurge))
        {
            if (pszArg[cchPurge] == '\0')
                return 1 + g_aSupArgPurgeDescs[i].fTakesValue;
            if (   g_aSupArgPurgeDescs[i].fTakesValue
                && (pszArg[cchPurge] == ':' || pszArg[cchPurge] == '='))
                return 1;
        }
    }

    return 0;
}


/**
 * Purges any command line arguments considered harmful.
 *
 * @param   cArgsOrig        The original number of arguments.
 * @param   papszArgsOrig    The original argument vector.
 * @param   pcArgsNew        Where to store the new number of arguments on success.
 * @param   ppapszArgsNew    Where to store the pointer to the purged argument vector.
 */
static void supR3HardenedMainPurgeArgs(int cArgsOrig, char **papszArgsOrig, int *pcArgsNew, char ***ppapszArgsNew)
{
    int    iDst = 0;
#ifdef RT_OS_WINDOWS
    char **papszArgsNew = papszArgsOrig; /* We allocated this, no need to allocate again. */
#else
    char **papszArgsNew = (char **)malloc((cArgsOrig + 1) * sizeof(char *));
#endif
    if (papszArgsNew)
    {
        for (int iSrc = 0; iSrc < cArgsOrig; iSrc++)
        {
            unsigned cPurgedArgs = supR3HardenedMainShouldPurgeArg(papszArgsOrig[iSrc]);
            if (!cPurgedArgs)
                papszArgsNew[iDst++] = papszArgsOrig[iSrc];
            else
                iSrc += cPurgedArgs - 1;
        }

        papszArgsNew[iDst] = NULL; /* The array is NULL terminated, just like envp. */
    }
    else
        supR3HardenedFatal("SUPR3HardenedMain: failed to allocate memory for purged command line!\n");
    *pcArgsNew     = iDst;
    *ppapszArgsNew = papszArgsNew;

#ifdef RT_OS_WINDOWS
    /** @todo Update command line pointers in PEB, wont really work without it. */
#endif
}


/**
 * Loads the VBoxRT DLL/SO/DYLIB, hands it the open driver,
 * and calls RTR3InitEx.
 *
 * @param   fFlags      The SUPR3HardenedMain fFlags argument, passed to supR3PreInit.
 *
 * @remarks VBoxRT contains both IPRT and SUPR3.
 * @remarks This function will not return on failure.
 */
static void supR3HardenedMainInitRuntime(uint32_t fFlags)
{
    /*
     * Construct the name.
     */
    char szPath[RTPATH_MAX];
    supR3HardenedPathAppSharedLibs(szPath, sizeof(szPath) - sizeof("/VBoxRT" SUPLIB_DLL_SUFF));
    suplibHardenedStrCat(szPath, "/VBoxRT" SUPLIB_DLL_SUFF);

    /*
     * Open it and resolve the symbols.
     */
#if defined(RT_OS_WINDOWS)
    HMODULE hMod = (HMODULE)supR3HardenedWinLoadLibrary(szPath, false /*fSystem32Only*/, g_fSupHardenedMain);
    if (!hMod)
        supR3HardenedFatalMsg("supR3HardenedMainInitRuntime", kSupInitOp_IPRT, VERR_MODULE_NOT_FOUND,
                              "LoadLibrary \"%s\" failed (rc=%d)",
                              szPath, RtlGetLastWin32Error());
    PFNRTR3INITEX pfnRTInitEx = (PFNRTR3INITEX)GetProcAddress(hMod, SUP_HARDENED_SYM("RTR3InitEx"));
    if (!pfnRTInitEx)
        supR3HardenedFatalMsg("supR3HardenedMainInitRuntime", kSupInitOp_IPRT, VERR_SYMBOL_NOT_FOUND,
                              "Entrypoint \"RTR3InitEx\" not found in \"%s\" (rc=%d)",
                              szPath, RtlGetLastWin32Error());

    PFNSUPR3PREINIT pfnSUPPreInit = (PFNSUPR3PREINIT)GetProcAddress(hMod, SUP_HARDENED_SYM("supR3PreInit"));
    if (!pfnSUPPreInit)
        supR3HardenedFatalMsg("supR3HardenedMainInitRuntime", kSupInitOp_IPRT, VERR_SYMBOL_NOT_FOUND,
                              "Entrypoint \"supR3PreInit\" not found in \"%s\" (rc=%d)",
                              szPath, RtlGetLastWin32Error());

    g_pfnRTLogRelPrintf = (PFNRTLOGRELPRINTF)GetProcAddress(hMod, SUP_HARDENED_SYM("RTLogRelPrintf"));
    Assert(g_pfnRTLogRelPrintf);  /* Not fatal in non-strict builds. */

#else
    /* the dlopen crowd */
    void *pvMod = dlopen(szPath, RTLD_NOW | RTLD_GLOBAL);
    if (!pvMod)
        supR3HardenedFatalMsg("supR3HardenedMainInitRuntime", kSupInitOp_IPRT, VERR_MODULE_NOT_FOUND,
                              "dlopen(\"%s\",) failed: %s",
                              szPath, dlerror());
    PFNRTR3INITEX pfnRTInitEx = (PFNRTR3INITEX)(uintptr_t)dlsym(pvMod, SUP_HARDENED_SYM("RTR3InitEx"));
    if (!pfnRTInitEx)
        supR3HardenedFatalMsg("supR3HardenedMainInitRuntime", kSupInitOp_IPRT, VERR_SYMBOL_NOT_FOUND,
                              "Entrypoint \"RTR3InitEx\" not found in \"%s\"!\ndlerror: %s",
                              szPath, dlerror());
    PFNSUPR3PREINIT pfnSUPPreInit = (PFNSUPR3PREINIT)(uintptr_t)dlsym(pvMod, SUP_HARDENED_SYM("supR3PreInit"));
    if (!pfnSUPPreInit)
        supR3HardenedFatalMsg("supR3HardenedMainInitRuntime", kSupInitOp_IPRT, VERR_SYMBOL_NOT_FOUND,
                              "Entrypoint \"supR3PreInit\" not found in \"%s\"!\ndlerror: %s",
                              szPath, dlerror());
#endif

    /*
     * Make the calls.
     */
    supR3HardenedGetPreInitData(&g_SupPreInitData);
    int rc = pfnSUPPreInit(&g_SupPreInitData, fFlags);
    if (RT_FAILURE(rc))
        supR3HardenedFatalMsg("supR3HardenedMainInitRuntime", kSupInitOp_IPRT, rc,
                              "supR3PreInit failed with rc=%d", rc);

    /* Get the executable path for the IPRT init on linux if /proc/self/exe isn't accessible. */
    const char *pszExePath = NULL;
#ifdef RT_OS_LINUX
    if (!supR3HardenedMainIsProcSelfExeAccssible())
        pszExePath = g_szSupLibHardenedExePath;
#endif

    /* Assemble the IPRT init flags. We could probably just pass RTR3INIT_FLAGS_TRY_SUPLIB
       here and be done with it, but it's not too much hazzle to convert fFlags 1:1. */
    uint32_t fRtInit = 0;
    if (!(fFlags & SUPSECMAIN_FLAGS_DONT_OPEN_DEV))
    {
        if (fFlags & SUPSECMAIN_FLAGS_DRIVERLESS)
            fRtInit |= (SUPR3INIT_F_DRIVERLESS              << RTR3INIT_FLAGS_SUPLIB_SHIFT) | RTR3INIT_FLAGS_TRY_SUPLIB;
        if (fFlags & SUPSECMAIN_FLAGS_DRIVERLESS_IEM_ALLOWED)
            fRtInit |= (SUPR3INIT_F_DRIVERLESS_IEM_ALLOWED  << RTR3INIT_FLAGS_SUPLIB_SHIFT) | RTR3INIT_FLAGS_TRY_SUPLIB;
#ifdef VBOX_WITH_DRIVERLESS_NEM_FALLBACK
        if (fFlags & SUPSECMAIN_FLAGS_DRIVERLESS_NEM_FALLBACK)
            fRtInit |= (SUPR3INIT_F_DRIVERLESS_NEM_FALLBACK << RTR3INIT_FLAGS_SUPLIB_SHIFT) | RTR3INIT_FLAGS_TRY_SUPLIB;
#endif
        if (!(fRtInit & RTR3INIT_FLAGS_TRY_SUPLIB))
            fRtInit |= RTR3INIT_FLAGS_SUPLIB;
    }

    /* Now do the IPRT init. */
    rc = pfnRTInitEx(RTR3INIT_VER_CUR, fRtInit, 0 /*cArgs*/, NULL /*papszArgs*/, pszExePath);
    if (RT_FAILURE(rc))
        supR3HardenedFatalMsg("supR3HardenedMainInitRuntime", kSupInitOp_IPRT, rc,
                              "RTR3InitEx failed with rc=%d (fRtFlags=%#x)", rc, fRtInit);

#if defined(RT_OS_WINDOWS)
    /*
     * Windows: Create thread that terminates the process when the parent stub
     *          process terminates (VBoxNetDHCP, Ctrl-C, etc).
     */
    if (!(fFlags & SUPSECMAIN_FLAGS_DONT_OPEN_DEV))
        supR3HardenedWinCreateParentWatcherThread(hMod);
#endif
}


/**
 * Construct the path to the DLL/SO/DYLIB containing the actual program.
 *
 * @returns VBox status code.
 * @param   pszProgName     The program name.
 * @param   fMainFlags      The flags passed to SUPR3HardenedMain.
 * @param   pszPath         The output buffer.
 * @param   cbPath          The size of the output buffer, in bytes.  Must be at
 *                          least 128 bytes!
 */
static int supR3HardenedMainGetTrustedLib(const char *pszProgName, uint32_t fMainFlags, char *pszPath, size_t cbPath)
{
    supR3HardenedPathAppPrivateArch(pszPath, sizeof(cbPath) - 10);
    const char *pszSubDirSlash;
    switch (g_fSupHardenedMain & SUPSECMAIN_FLAGS_LOC_MASK)
    {
        case SUPSECMAIN_FLAGS_LOC_APP_BIN:
#ifdef RT_OS_DARWIN
        case SUPSECMAIN_FLAGS_LOC_OSX_HLP_APP:
#endif
            pszSubDirSlash = "/";
            break;
        case SUPSECMAIN_FLAGS_LOC_TESTCASE:
            pszSubDirSlash = "/testcase/";
            break;
        default:
            pszSubDirSlash = "/";
            supR3HardenedFatal("supR3HardenedMainGetTrustedMain: Unknown program binary location: %#x\n", g_fSupHardenedMain);
    }
#ifdef RT_OS_DARWIN
    if (fMainFlags & SUPSECMAIN_FLAGS_OSX_VM_APP)
        pszProgName = "VirtualBox";
#else
    RT_NOREF1(fMainFlags);
#endif
    size_t cch = suplibHardenedStrLen(pszPath);
    return suplibHardenedStrCopyEx(&pszPath[cch], cbPath - cch, pszSubDirSlash, pszProgName, SUPLIB_DLL_SUFF, NULL);
}


/**
 * Loads the DLL/SO/DYLIB containing the actual program and
 * resolves the TrustedError symbol.
 *
 * This is very similar to supR3HardenedMainGetTrustedMain().
 *
 * @returns Pointer to the trusted error symbol if it is exported, NULL
 *          and no error messages otherwise.
 * @param   pszProgName     The program name.
 */
static PFNSUPTRUSTEDERROR supR3HardenedMainGetTrustedError(const char *pszProgName)
{
    /*
     * Don't bother if the main() function didn't advertise any TrustedError
     * export.  It's both a waste of time and may trigger additional problems,
     * confusing or obscuring the original issue.
     */
    if (!(g_fSupHardenedMain & SUPSECMAIN_FLAGS_TRUSTED_ERROR))
        return NULL;

    /*
     * Construct the name.
     */
    char szPath[RTPATH_MAX];
    supR3HardenedMainGetTrustedLib(pszProgName, g_fSupHardenedMain, szPath, sizeof(szPath));

    /*
     * Open it and resolve the symbol.
     */
#if defined(RT_OS_WINDOWS)
    supR3HardenedWinEnableThreadCreation();
    HMODULE hMod = (HMODULE)supR3HardenedWinLoadLibrary(szPath, false /*fSystem32Only*/, 0 /*fMainFlags*/);
    if (!hMod)
        return NULL;
    FARPROC pfn = GetProcAddress(hMod, SUP_HARDENED_SYM("TrustedError"));
    if (!pfn)
        return NULL;
    return (PFNSUPTRUSTEDERROR)pfn;

#else
    /* the dlopen crowd */
    void *pvMod = dlopen(szPath, RTLD_NOW | RTLD_GLOBAL);
    if (!pvMod)
        return NULL;
    void *pvSym = dlsym(pvMod, SUP_HARDENED_SYM("TrustedError"));
    if (!pvSym)
        return NULL;
    return (PFNSUPTRUSTEDERROR)(uintptr_t)pvSym;
#endif
}


/**
 * Loads the DLL/SO/DYLIB containing the actual program and
 * resolves the TrustedMain symbol.
 *
 * @returns Pointer to the trusted main of the actual program.
 * @param   pszProgName     The program name.
 * @param   fMainFlags      The flags passed to SUPR3HardenedMain.
 * @remarks This function will not return on failure.
 */
static PFNSUPTRUSTEDMAIN supR3HardenedMainGetTrustedMain(const char *pszProgName, uint32_t fMainFlags)
{
    /*
     * Construct the name.
     */
    char szPath[RTPATH_MAX];
    supR3HardenedMainGetTrustedLib(pszProgName, fMainFlags, szPath, sizeof(szPath));

    /*
     * Open it and resolve the symbol.
     */
#if defined(RT_OS_WINDOWS)
    HMODULE hMod = (HMODULE)supR3HardenedWinLoadLibrary(szPath, false /*fSystem32Only*/, 0 /*fMainFlags*/);
    if (!hMod)
        supR3HardenedFatal("supR3HardenedMainGetTrustedMain: LoadLibrary \"%s\" failed, rc=%d\n",
                           szPath, RtlGetLastWin32Error());
    FARPROC pfn = GetProcAddress(hMod, SUP_HARDENED_SYM("TrustedMain"));
    if (!pfn)
        supR3HardenedFatal("supR3HardenedMainGetTrustedMain: Entrypoint \"TrustedMain\" not found in \"%s\" (rc=%d)\n",
                           szPath, RtlGetLastWin32Error());
    return (PFNSUPTRUSTEDMAIN)pfn;

#else
    /* the dlopen crowd */
    void *pvMod = dlopen(szPath, RTLD_NOW | RTLD_GLOBAL);
    if (!pvMod)
        supR3HardenedFatal("supR3HardenedMainGetTrustedMain: dlopen(\"%s\",) failed: %s\n",
                           szPath, dlerror());
    void *pvSym = dlsym(pvMod, SUP_HARDENED_SYM("TrustedMain"));
    if (!pvSym)
        supR3HardenedFatal("supR3HardenedMainGetTrustedMain: Entrypoint \"TrustedMain\" not found in \"%s\"!\ndlerror: %s\n",
                           szPath, dlerror());
    return (PFNSUPTRUSTEDMAIN)(uintptr_t)pvSym;
#endif
}


DECLHIDDEN(int) SUPR3HardenedMain(const char *pszProgName, uint32_t fFlags, int argc, char **argv, char **envp)
{
    SUP_DPRINTF(("SUPR3HardenedMain: pszProgName=%s fFlags=%#x\n", pszProgName, fFlags));
    g_enmSupR3HardenedMainState = SUPR3HARDENEDMAINSTATE_HARDENED_MAIN_CALLED;

    /*
     * Note! At this point there is no IPRT, so we will have to stick
     * to basic CRT functions that everyone agree upon.
     */
    g_pszSupLibHardenedProgName   = pszProgName;
    g_fSupHardenedMain            = fFlags;
    g_SupPreInitData.u32Magic     = SUPPREINITDATA_MAGIC;
    g_SupPreInitData.u32EndMagic  = SUPPREINITDATA_MAGIC;
#ifdef RT_OS_WINDOWS
    if (!g_fSupEarlyProcessInit)
#endif
        g_SupPreInitData.Data.hDevice = SUP_HDEVICE_NIL;

    /*
     * Determine the full exe path as we'll be needing it for the verify all
     * call(s) below.  (We have to do this early on Linux because we * *might*
     * not be able to access /proc/self/exe after the seteuid call.)
     */
    supR3HardenedGetFullExePath();
#ifdef RT_OS_WINDOWS
    supR3HardenedWinInitAppBin(fFlags);
#endif

#ifdef SUP_HARDENED_SUID
    /*
     * Grab any options from the environment.
     */
    supR3GrabOptions();

    /*
     * Check that we're root, if we aren't then the installation is butchered.
     */
    g_uid = getuid();
    g_gid = getgid();
    if (geteuid() != 0 /* root */)
        supR3HardenedFatalMsg("SUPR3HardenedMain", kSupInitOp_RootCheck, VERR_PERMISSION_DENIED,
                              "Effective UID is not root (euid=%d egid=%d uid=%d gid=%d)",
                              geteuid(), getegid(), g_uid, g_gid);
#endif /* SUP_HARDENED_SUID */

#ifdef RT_OS_WINDOWS
    /*
     * Windows: First respawn. On Windows we will respawn the process twice to establish
     * something we can put some kind of reliable trust in.  The first respawning aims
     * at dropping compatibility layers and process "security" solutions.
     */
    if (   !g_fSupEarlyProcessInit
        && !(fFlags & SUPSECMAIN_FLAGS_DONT_OPEN_DEV)
        && supR3HardenedWinIsReSpawnNeeded(1 /*iWhich*/, argc, argv))
    {
        SUP_DPRINTF(("SUPR3HardenedMain: Respawn #1\n"));
        supR3HardenedWinInit(SUPSECMAIN_FLAGS_DONT_OPEN_DEV | SUPSECMAIN_FLAGS_FIRST_PROCESS, false /*fAvastKludge*/);
        supR3HardenedVerifyAll(true /* fFatal */, pszProgName, g_szSupLibHardenedExePath, fFlags);
        return supR3HardenedWinReSpawn(1 /*iWhich*/);
    }

    /*
     * Windows: Initialize the image verification global data so we can verify the
     * signature of the process image and hook the core of the DLL loader API so we
     * can check the signature of all DLLs mapped into the process.  (Already done
     * by early VM process init.)
     */
    if (!g_fSupEarlyProcessInit)
        supR3HardenedWinInit(fFlags, true /*fAvastKludge*/);
#endif /* RT_OS_WINDOWS */

    /*
     * Validate the installation.
     */
    supR3HardenedVerifyAll(true /* fFatal */, pszProgName, g_szSupLibHardenedExePath, fFlags);

    /*
     * The next steps are only taken if we actually need to access the support
     * driver.  (Already done by early process init.)
     */
    if (!(fFlags & SUPSECMAIN_FLAGS_DONT_OPEN_DEV))
    {
#ifdef RT_OS_WINDOWS
        /*
         * Windows: Must have done early process init if we get here.
         */
        if (!g_fSupEarlyProcessInit)
            supR3HardenedFatalMsg("SUPR3HardenedMain", kSupInitOp_Integrity, VERR_WRONG_ORDER,
                                  "Early process init was somehow skipped.");

        /*
         * Windows: The second respawn.  This time we make a special arrangement
         * with vboxdrv to monitor access to the new process from its inception.
         */
        if (supR3HardenedWinIsReSpawnNeeded(2 /* iWhich*/, argc, argv))
        {
            SUP_DPRINTF(("SUPR3HardenedMain: Respawn #2\n"));
            return supR3HardenedWinReSpawn(2 /* iWhich*/);
        }
        SUP_DPRINTF(("SUPR3HardenedMain: Final process, opening VBoxDrv...\n"));
        supR3HardenedWinFlushLoaderCache();

#else
        /*
         * Open the vboxdrv device.
         */
        supR3HardenedMainOpenDevice();
#endif /* !RT_OS_WINDOWS */
    }

#ifdef RT_OS_WINDOWS
    /*
     * Windows: Enable the use of windows APIs to verify images at load time.
     */
    supR3HardenedWinEnableThreadCreation();
    supR3HardenedWinFlushLoaderCache();
    supR3HardenedWinResolveVerifyTrustApiAndHookThreadCreation(g_pszSupLibHardenedProgName);
    g_enmSupR3HardenedMainState = SUPR3HARDENEDMAINSTATE_WIN_VERIFY_TRUST_READY;
#else /* !RT_OS_WINDOWS */
# if defined(RT_OS_DARWIN)
    supR3HardenedDarwinInit();
# elif !defined(RT_OS_FREEBSD) /** @todo Portme. */
    /*
     * Posix: Hook the load library interface interface.
     */
    supR3HardenedPosixInit();
# endif
#endif /* !RT_OS_WINDOWS */

#ifdef SUP_HARDENED_SUID
    /*
     * Grab additional capabilities / privileges.
     */
    supR3HardenedMainGrabCapabilites();

    /*
     * Drop any root privileges we might be holding (won't return on failure)
     */
    supR3HardenedMainDropPrivileges();
#endif

    /*
     * Purge any environment variables and command line arguments considered harmful.
     */
    /** @todo May need to move this to a much earlier stage on windows.  */
    supR3HardenedMainPurgeEnvironment(envp);
    supR3HardenedMainPurgeArgs(argc, argv, &argc, &argv);

    /*
     * Load the IPRT, hand the SUPLib part the open driver and
     * call RTR3InitEx.
     */
    SUP_DPRINTF(("SUPR3HardenedMain: Load Runtime...\n"));
    g_enmSupR3HardenedMainState = SUPR3HARDENEDMAINSTATE_INIT_RUNTIME;
    supR3HardenedMainInitRuntime(fFlags);
#ifdef RT_OS_WINDOWS
    supR3HardenedWinModifyDllSearchPath(fFlags, g_szSupLibHardenedAppBinPath);
#endif

    /*
     * Load the DLL/SO/DYLIB containing the actual program
     * and pass control to it.
     */
    SUP_DPRINTF(("SUPR3HardenedMain: Load TrustedMain...\n"));
    g_enmSupR3HardenedMainState = SUPR3HARDENEDMAINSTATE_GET_TRUSTED_MAIN;
    PFNSUPTRUSTEDMAIN pfnTrustedMain = supR3HardenedMainGetTrustedMain(pszProgName, fFlags);

    SUP_DPRINTF(("SUPR3HardenedMain: Calling TrustedMain (%p)...\n", pfnTrustedMain));
    g_enmSupR3HardenedMainState = SUPR3HARDENEDMAINSTATE_CALLED_TRUSTED_MAIN;
    return pfnTrustedMain(argc, argv, envp);
}

