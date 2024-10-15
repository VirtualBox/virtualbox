/* $Id: VBoxNetAdpCtl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Apps - VBoxAdpCtl, Configuration tool for vboxnetX adapters.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */



/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <list>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <iprt/errcore.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/net.h>
#include <iprt/string.h>
#include <iprt/uint128.h>

#ifdef RT_OS_LINUX
# include <arpa/inet.h>
# include <net/if.h>
# include <linux/types.h>
/* Older versions of ethtool.h rely on these: */
typedef unsigned long long u64;
typedef __uint32_t u32;
typedef __uint16_t u16;
typedef __uint8_t u8;
# include <limits.h> /* for INT_MAX */
# include <linux/ethtool.h>
# include <linux/sockios.h>
#endif
#ifdef RT_OS_SOLARIS
# include <sys/ioccom.h>
#endif

/** @todo Error codes must be moved to some header file */
#define ADPCTLERR_BAD_NAME         2
#define ADPCTLERR_NO_CTL_DEV       3
#define ADPCTLERR_IOCTL_FAILED     4
#define ADPCTLERR_SOCKET_FAILED    5

/** @todo These are duplicates from src/VBox/HostDrivers/VBoxNetAdp/VBoxNetAdpInternal.h */
#define VBOXNETADP_CTL_DEV_NAME    "/dev/vboxnetctl"
#define VBOXNETADP_MAX_INSTANCES   128
#define VBOXNETADP_NAME            "vboxnet"
#define VBOXNETADP_MAX_NAME_LEN    32
#define VBOXNETADP_CTL_ADD   _IOWR('v', 1, VBOXNETADPREQ)
#define VBOXNETADP_CTL_REMOVE _IOW('v', 2, VBOXNETADPREQ)
typedef struct VBoxNetAdpReq
{
    char szName[VBOXNETADP_MAX_NAME_LEN];
} VBOXNETADPREQ;
typedef VBOXNETADPREQ *PVBOXNETADPREQ;

#define VBOXADPCTL_IFCONFIG_PATH1 "/sbin/ifconfig"
#define VBOXADPCTL_IFCONFIG_PATH2 "/bin/ifconfig"

bool verbose;
bool dry_run;


static int usage(void)
{
    fprintf(stderr, "Usage: VBoxNetAdpCtl <adapter> <address> ([netmask <address>] | remove)\n");
    fprintf(stderr, "     | VBoxNetAdpCtl [<adapter>] add\n");
    fprintf(stderr, "     | VBoxNetAdpCtl <adapter> remove\n");
    return EXIT_FAILURE;
}


/*
 * A wrapper on standard list that provides '<<' operator for adding several list members in a single
 * line dynamically. For example: "CmdList(arg1) << arg2 << arg3" produces a list with three members.
 */
class CmdList
{
public:
    /** Creates an empty list. */
    CmdList() {};
    /** Creates a list with a single member. */
    CmdList(const char *pcszCommand) { m_list.push_back(pcszCommand); };
    /** Provides access to the underlying standard list. */
    const std::list<const char *>& getList(void) const { return m_list; };
    /** Adds a member to the list. */
    CmdList& operator<<(const char *pcszArgument);
private:
    std::list<const char *>m_list;
};

CmdList& CmdList::operator<<(const char *pcszArgument)
{
    m_list.push_back(pcszArgument);
    return *this;
}

/** Simple helper to distinguish IPv4 and IPv6 addresses. */
DECLINLINE(bool) isAddrV6(const char *pcszAddress)
{
    return !!(strchr(pcszAddress, ':'));
}


/*********************************************************************************************************************************
*   Generic address commands.                                                                                                    *
*********************************************************************************************************************************/

/**
 * The base class for all address manipulation commands. While being an abstract class,
 * it provides a generic implementation of 'set' and 'remove' methods, which rely on
 * pure virtual methods like 'addV4' and 'removeV4' to perform actual command execution.
 */
class AddressCommand
{
public:
    AddressCommand() : m_pszPath(0) {};
    virtual ~AddressCommand() {};

    /** returns true if underlying command (executable) is present in the system. */
    bool isAvailable(void)
        { struct stat s; return (!stat(m_pszPath, &s) && S_ISREG(s.st_mode)); };

    /*
     * Someday we may want to support several IP addresses per adapter, but for now we
     * have 'set' method only, which replaces all addresses with the one specifed.
     *
     * virtual int add(const char *pcszAdapter, const char *pcszAddress, const char *pcszNetmask = 0) = 0;
     */
    /** replace existing address(es) */
    virtual int set(const char *pcszAdapter, const char *pcszAddress, const char *pcszNetmask = 0);
    /** remove address */
    virtual int remove(const char *pcszAdapter, const char *pcszAddress);
protected:
    /** IPv4-specific handler used by generic implementation of 'set' method if 'setV4' is not supported. */
    virtual int addV4(const char *pcszAdapter, const char *pcszAddress, const char *pcszNetmask = 0) = 0;
    /** IPv6-specific handler used by generic implementation of 'set' method. */
    virtual int addV6(const char *pcszAdapter, const char *pcszAddress, const char *pcszNetmask = 0) = 0;
    /** IPv4-specific handler used by generic implementation of 'set' method. */
    virtual int setV4(const char *pcszAdapter, const char *pcszAddress, const char *pcszNetmask = 0) = 0;
    /** IPv4-specific handler used by generic implementation of 'remove' method. */
    virtual int removeV4(const char *pcszAdapter, const char *pcszAddress) = 0;
    /** IPv6-specific handler used by generic implementation of 'remove' method. */
    virtual int removeV6(const char *pcszAdapter, const char *pcszAddress) = 0;
    /** Composes the argument list of command that obtains all addresses assigned to the adapter. */
    virtual CmdList getShowCommand(const char *pcszAdapter) const = 0;

    /** Prepares an array of C strings needed for 'exec' call. */
    char * const * allocArgv(const CmdList& commandList);
    /** Hides process creation details. To be used in derived classes. */
    int execute(CmdList& commandList);

    /** A path to executable command. */
    const char *m_pszPath;
private:
    /** Removes all previously asssigned addresses of a particular protocol family. */
    int removeAddresses(const char *pcszAdapter, const char *pcszFamily);
};

/*
 * A generic implementation of 'ifconfig' command for all platforms.
 */
class CmdIfconfig : public AddressCommand
{
public:
    CmdIfconfig()
        {
            struct stat s;
            if (   !stat(VBOXADPCTL_IFCONFIG_PATH1, &s)
                && S_ISREG(s.st_mode))
                m_pszPath = (char*)VBOXADPCTL_IFCONFIG_PATH1;
            else
                m_pszPath = (char*)VBOXADPCTL_IFCONFIG_PATH2;
        };

protected:
    /** Returns platform-specific subcommand to add an address. */
    virtual const char *addCmdArg(void) const = 0;
    /** Returns platform-specific subcommand to remove an address. */
    virtual const char *delCmdArg(void) const = 0;
    virtual CmdList getShowCommand(const char *pcszAdapter) const
        { return CmdList(pcszAdapter); };
    virtual int addV4(const char *pcszAdapter, const char *pcszAddress, const char *pcszNetmask = 0)
        { return ENOTSUP; NOREF(pcszAdapter); NOREF(pcszAddress); NOREF(pcszNetmask); };
    virtual int addV6(const char *pcszAdapter, const char *pcszAddress, const char *pcszNetmask = 0)
        {
            return execute(CmdList(pcszAdapter) << "inet6" << addCmdArg() << pcszAddress);
            NOREF(pcszNetmask);
        };
    virtual int setV4(const char *pcszAdapter, const char *pcszAddress, const char *pcszNetmask = 0)
        {
            if (!pcszNetmask)
                return execute(CmdList(pcszAdapter) << pcszAddress);
            return execute(CmdList(pcszAdapter) << pcszAddress << "netmask" << pcszNetmask);
        };
    virtual int removeV4(const char *pcszAdapter, const char *pcszAddress)
        { return execute(CmdList(pcszAdapter) << delCmdArg() << pcszAddress); };
    virtual int removeV6(const char *pcszAdapter, const char *pcszAddress)
        { return execute(CmdList(pcszAdapter) << "inet6" << delCmdArg() << pcszAddress); };
};


/*********************************************************************************************************************************
*   Platform-specific commands                                                                                                   *
*********************************************************************************************************************************/

class CmdIfconfigLinux : public CmdIfconfig
{
protected:
    virtual int removeV4(const char *pcszAdapter, const char *pcszAddress)
        { return execute(CmdList(pcszAdapter) << "0.0.0.0"); NOREF(pcszAddress); };
    virtual const char *addCmdArg(void) const { return "add"; };
    virtual const char *delCmdArg(void) const { return "del"; };
};

class CmdIfconfigDarwin : public CmdIfconfig
{
protected:
    virtual const char *addCmdArg(void) const { return "add"; };
    virtual const char *delCmdArg(void) const { return "delete"; };
};

class CmdIfconfigSolaris : public CmdIfconfig
{
public:
    virtual int set(const char *pcszAdapter, const char *pcszAddress, const char *pcszNetmask = 0)
        {
            const char *pcszFamily = isAddrV6(pcszAddress) ? "inet6" : "inet";
            int status;

            status = execute(CmdList(pcszAdapter) << pcszFamily);
            if (status != EXIT_SUCCESS)
                status = execute(CmdList(pcszAdapter) << pcszFamily << "plumb" << "up");
            if (status != EXIT_SUCCESS)
                return status;

            return CmdIfconfig::set(pcszAdapter, pcszAddress, pcszNetmask);
        };
protected:
    /* We can umplumb IPv4 interfaces only! */
    virtual int removeV4(const char *pcszAdapter, const char *pcszAddress)
        {
            int rc = CmdIfconfig::removeV4(pcszAdapter, pcszAddress);

            /** @todo Do we really need to unplumb inet here? */
            execute(CmdList(pcszAdapter) << "inet" << "unplumb");
            return rc;
        };
    virtual const char *addCmdArg(void) const { return "addif"; };
    virtual const char *delCmdArg(void) const { return "removeif"; };
};


#ifdef RT_OS_LINUX
/*
 * Helper class to incapsulate IPv4 address conversion.
 *
 * Note that this class relies on NetworkAddress to have been used for
 * checking validity of IP addresses prior calling any methods of this
 * class.
 */
class AddressIPv4
{
public:
    AddressIPv4(const char *pcszAddress, const char *pcszNetmask = 0)
        {
            m_Prefix = 0;
            memset(&m_Address, 0, sizeof(m_Address));

            if (pcszNetmask)
                m_Prefix = maskToPrefix(pcszNetmask);
            else
            {
                /*
                 * Since guessing network mask is probably futile we simply use 24,
                 * as it matches our defaults. When non-default values are used
                 * providing a proper netmask is up to the user.
                 */
                m_Prefix = 24;
            }
            int rc = RTNetStrToIPv4Addr(pcszAddress, &m_Address);
            AssertRCReturnVoid(rc);
            snprintf(m_szAddressAndMask, sizeof(m_szAddressAndMask), "%s/%d", pcszAddress, m_Prefix);
            deriveBroadcast(&m_Address, m_Prefix);
        }
    const char *getBroadcast() const { return m_szBroadcast; };
    const char *getAddressAndMask() const { return m_szAddressAndMask; };
private:
    int maskToPrefix(const char *pcszNetmask);
    void deriveBroadcast(PCRTNETADDRIPV4 pcAddress, int uPrefix);

    int           m_Prefix;
    RTNETADDRIPV4 m_Address;
    char m_szAddressAndMask[INET_ADDRSTRLEN + 3]; /* e.g. 192.168.56.101/24 */
    char m_szBroadcast[INET_ADDRSTRLEN];
};

int AddressIPv4::maskToPrefix(const char *pcszNetmask)
{
    RTNETADDRIPV4 mask;
    int prefix = 0;

    int rc = RTNetStrToIPv4Addr(pcszNetmask, &mask);
    AssertRCReturn(rc, 0);
    rc = RTNetMaskToPrefixIPv4(&mask, &prefix);
    AssertRCReturn(rc, 0);

    return prefix;
}

void AddressIPv4::deriveBroadcast(PCRTNETADDRIPV4 pcAddress, int iPrefix)
{
    RTNETADDRIPV4 mask, broadcast;
    int rc = RTNetPrefixToMaskIPv4(iPrefix, &mask);
    AssertRCReturnVoid(rc);
    broadcast.au32[0] = (pcAddress->au32[0] & mask.au32[0]) | ~mask.au32[0];
    inet_ntop(AF_INET, broadcast.au32, m_szBroadcast, sizeof(m_szBroadcast));
}


/*
 * Linux-specific implementation of 'ip' command, as other platforms do not support it.
 */
class CmdIpLinux : public AddressCommand
{
public:
    CmdIpLinux() { m_pszPath = "/sbin/ip"; };
    /**
     * IPv4 and IPv6 syntax is the same, so we override `remove` instead of implementing
     * family-specific commands. It would be easier to use the same body in both
     * 'removeV4' and 'removeV6', so we override 'remove' to illustrate how to do common
     * implementation.
     */
    virtual int remove(const char *pcszAdapter, const char *pcszAddress)
        { return execute(CmdList("addr") << "del" << pcszAddress << "dev" << pcszAdapter); };
protected:
    virtual int addV4(const char *pcszAdapter, const char *pcszAddress, const char *pcszNetmask = 0)
        {
            AddressIPv4 addr(pcszAddress, pcszNetmask);
            bringUp(pcszAdapter);
            return execute(CmdList("addr") << "add" << addr.getAddressAndMask() <<
                           "broadcast" << addr.getBroadcast() << "dev" << pcszAdapter);
        };
    virtual int addV6(const char *pcszAdapter, const char *pcszAddress, const char *pcszNetmask = 0)
        {
            bringUp(pcszAdapter);
            return execute(CmdList("addr") << "add" << pcszAddress << "dev" << pcszAdapter);
            NOREF(pcszNetmask);
        };
    /**
     * Our command does not support 'replacing' addresses. Reporting this fact to generic implementation
     * of 'set' causes it to remove all assigned addresses, then 'add' the new one.
     */
    virtual int setV4(const char *pcszAdapter, const char *pcszAddress, const char *pcszNetmask = 0)
        { return ENOTSUP; NOREF(pcszAdapter); NOREF(pcszAddress); NOREF(pcszNetmask); };
    /** We use family-agnostic command syntax. See 'remove' above. */
    virtual int removeV4(const char *pcszAdapter, const char *pcszAddress)
        { return ENOTSUP; NOREF(pcszAdapter); NOREF(pcszAddress); };
    /** We use family-agnostic command syntax. See 'remove' above. */
    virtual int removeV6(const char *pcszAdapter, const char *pcszAddress)
        { return ENOTSUP; NOREF(pcszAdapter); NOREF(pcszAddress); };
    virtual CmdList getShowCommand(const char *pcszAdapter) const
        { return CmdList("addr") << "show" << "dev" << pcszAdapter; };
private:
    /** Brings up the adapter */
    void bringUp(const char *pcszAdapter)
        { execute(CmdList("link") << "set" << "dev" << pcszAdapter << "up"); };
};
#endif /* RT_OS_LINUX */


/*********************************************************************************************************************************
*   Generic address command implementations                                                                                      *
*********************************************************************************************************************************/

int AddressCommand::set(const char *pcszAdapter, const char *pcszAddress, const char *pcszNetmask)
{
    if (isAddrV6(pcszAddress))
    {
        removeAddresses(pcszAdapter, "inet6");
        return addV6(pcszAdapter, pcszAddress, pcszNetmask);
    }
    int rc = setV4(pcszAdapter, pcszAddress, pcszNetmask);
    if (rc == ENOTSUP)
    {
        removeAddresses(pcszAdapter, "inet");
        rc = addV4(pcszAdapter, pcszAddress, pcszNetmask);
    }
    return rc;
}

int AddressCommand::remove(const char *pcszAdapter, const char *pcszAddress)
{
    if (isAddrV6(pcszAddress))
        return removeV6(pcszAdapter, pcszAddress);
    return removeV4(pcszAdapter, pcszAddress);
}

/*
 * Allocate an array of exec arguments. In addition to arguments provided
 * we need to include the full path to the executable as well as "terminating"
 * null pointer marking the end of the array.
 */
char * const * AddressCommand::allocArgv(const CmdList& list)
{
    int i = 0;
    std::list<const char *>::const_iterator it;
    const char **argv = (const char **)calloc(list.getList().size() + 2, sizeof(const char *));
    if (argv)
    {
        argv[i++] = m_pszPath;
        for (it = list.getList().begin(); it != list.getList().end(); ++it)
            argv[i++] = *it;
        argv[i++] = NULL;
    }
    return (char * const*)argv;
}

int AddressCommand::execute(CmdList& list)
{
    char * const pEnv[] = { (char*)"LC_ALL=C", NULL };
    char * const* argv = allocArgv(list);
    if (argv == NULL)
        return EXIT_FAILURE;

    if (verbose)
    {
        const char *sep = "";
        for (const char * const *pArg = argv; *pArg != NULL; ++pArg)
        {
            printf("%s%s", sep, *pArg);
            sep = " ";
        }
        printf("\n");
    }
    if (dry_run)
    {
        free((void *)argv);
        return EXIT_SUCCESS;
    }

    int rc = EXIT_FAILURE; /* o/~ hope for the best, expect the worst */
    pid_t childPid = fork();
    switch (childPid)
    {
        case -1: /* Something went wrong. */
            perror("fork");
            break;

        case 0: /* Child process. */
            if (execve(argv[0], argv, pEnv) == -1)
            {
                perror("execve");
                exit(EXIT_FAILURE);
                /* NOTREACHED */
            }
            break;

        default: /* Parent process. */
        {
            int status;
            pid_t waited = waitpid(childPid, &status, 0);
            if (waited == childPid) /* likely*/
            {
                if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS)
                    rc = EXIT_SUCCESS;
            }
            else if (waited == (pid_t)-1)
            {
                perror("waitpid");
            }
            else
            {
                /* should never happen */
                fprintf(stderr, "waitpid: unexpected pid %lld\n",
                        (long long int)waited);
            }
            break;
        }
    }

    free((void*)argv);
    return rc;
}

#define MAX_ADDRESSES 128
#define MAX_ADDRLEN   64

int AddressCommand::removeAddresses(const char *pcszAdapter, const char *pcszFamily)
{
    char szBuf[1024];
    char aszAddresses[MAX_ADDRESSES][MAX_ADDRLEN];
    int rc = EXIT_SUCCESS;
    int fds[2];

    memset(aszAddresses, 0, sizeof(aszAddresses));

    rc = pipe(fds);
    if (rc < 0)
        return errno;

    pid_t pid = fork();
    if (pid < 0)
        return errno;

    if (pid == 0)
    {
        /* child */
        close(fds[0]);
        close(STDOUT_FILENO);
        rc = dup2(fds[1], STDOUT_FILENO);
        if (rc >= 0)
        {
            char * const * argv = allocArgv(getShowCommand(pcszAdapter));
            char * const envp[] = { (char*)"LC_ALL=C", NULL };

            if (execve(argv[0], argv, envp) == -1)
            {
                free((void *)argv);
                return errno;
            }

            free((void *)argv);
        }
        return rc;
    }

    /* parent */
    close(fds[1]);
    FILE *fp = fdopen(fds[0], "r");
    if (!fp)
        return false;

    int cAddrs;
    for (cAddrs = 0; cAddrs < MAX_ADDRESSES && fgets(szBuf, sizeof(szBuf), fp);)
    {
        int cbSkipWS = strspn(szBuf, " \t");
        char *pszWord = strtok(szBuf + cbSkipWS, " ");
        /* We are concerned with particular family address lines only. */
        if (!pszWord || strcmp(pszWord, pcszFamily))
            continue;

        pszWord = strtok(NULL, " ");

        /* Skip "addr:" word if present. */
        if (pszWord && !strcmp(pszWord, "addr:"))
            pszWord = strtok(NULL, " ");

        /* Skip link-local address lines. */
        if (!pszWord || !strncmp(pszWord, "fe80", 4))
            continue;
        strncpy(aszAddresses[cAddrs++], pszWord, MAX_ADDRLEN-1);
    }
    fclose(fp);

    for (int i = 0; i < cAddrs && rc == EXIT_SUCCESS; i++)
        rc = remove(pcszAdapter, aszAddresses[i]);

    return rc;
}


/*********************************************************************************************************************************
*   Adapter creation/removal implementations                                                                                     *
*********************************************************************************************************************************/

/*
 * A generic implementation of adapter creation/removal ioctl calls.
 */
class Adapter
{
public:
    int add(char *pszNameInOut);
    int remove(const char *pcszName);
    int checkName(const char *pcszNameIn, char *pszNameOut, size_t cbNameOut);
protected:
    virtual int doIOCtl(unsigned long iCmd, VBOXNETADPREQ *pReq);
};

/*
 * Solaris does not support dynamic creation/removal of adapters.
 */
class AdapterSolaris : public Adapter
{
protected:
    virtual int doIOCtl(unsigned long iCmd, VBOXNETADPREQ *pReq)
        { return 1 /*ENOTSUP*/; NOREF(iCmd); NOREF(pReq); };
};

#if defined(RT_OS_LINUX)
/*
 * Linux implementation provides a 'workaround' to obtain adapter speed.
 */
class AdapterLinux : public Adapter
{
public:
    int getSpeed(const char *pszName, unsigned *puSpeed);
};

int AdapterLinux::getSpeed(const char *pszName, unsigned *puSpeed)
{
    struct ifreq IfReq;
    struct ethtool_value EthToolVal;
    struct ethtool_cmd EthToolReq;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        fprintf(stderr, "VBoxNetAdpCtl: Error while retrieving link "
                "speed for %s: ", pszName);
        perror("VBoxNetAdpCtl: failed to open control socket");
        return ADPCTLERR_SOCKET_FAILED;
    }
    /* Get link status first. */
    memset(&EthToolVal, 0, sizeof(EthToolVal));
    memset(&IfReq, 0, sizeof(IfReq));
    snprintf(IfReq.ifr_name, sizeof(IfReq.ifr_name), "%s", pszName);

    EthToolVal.cmd = ETHTOOL_GLINK;
    IfReq.ifr_data = (caddr_t)&EthToolVal;
    int rc = ioctl(fd, SIOCETHTOOL, &IfReq);
    if (rc == 0)
    {
        if (EthToolVal.data)
        {
            memset(&IfReq, 0, sizeof(IfReq));
            snprintf(IfReq.ifr_name, sizeof(IfReq.ifr_name), "%s", pszName);
            EthToolReq.cmd = ETHTOOL_GSET;
            IfReq.ifr_data = (caddr_t)&EthToolReq;
            rc = ioctl(fd, SIOCETHTOOL, &IfReq);
            if (rc == 0)
            {
                *puSpeed = EthToolReq.speed;
            }
            else
            {
                fprintf(stderr, "VBoxNetAdpCtl: Error while retrieving link "
                        "speed for %s: ", pszName);
                perror("VBoxNetAdpCtl: ioctl failed");
                rc = ADPCTLERR_IOCTL_FAILED;
            }
        }
        else
            *puSpeed = 0;
    }
    else
    {
        fprintf(stderr, "VBoxNetAdpCtl: Error while retrieving link "
                "status for %s: ", pszName);
        perror("VBoxNetAdpCtl: ioctl failed");
        rc = ADPCTLERR_IOCTL_FAILED;
    }

    close(fd);
    return rc;
}
#endif /* defined(RT_OS_LINUX) */

int Adapter::add(char *pszName /* in/out */)
{
    VBOXNETADPREQ Req;
    memset(&Req, '\0', sizeof(Req));
    snprintf(Req.szName, sizeof(Req.szName), "%s", pszName);
    int rc = doIOCtl(VBOXNETADP_CTL_ADD, &Req);
    if (rc == 0)
        strncpy(pszName, Req.szName, VBOXNETADP_MAX_NAME_LEN);
    return rc;
}

int Adapter::remove(const char *pcszName)
{
    VBOXNETADPREQ Req;
    memset(&Req, '\0', sizeof(Req));
    snprintf(Req.szName, sizeof(Req.szName), "%s", pcszName);
    return doIOCtl(VBOXNETADP_CTL_REMOVE, &Req);
}

int Adapter::checkName(const char *pcszNameIn, char *pszNameOut, size_t cbNameOut)
{
    int iAdapterIndex = -1;

    if (   strlen(pcszNameIn) >= VBOXNETADP_MAX_NAME_LEN
        || sscanf(pcszNameIn, "vboxnet%d", &iAdapterIndex) != 1
        || iAdapterIndex < 0 || iAdapterIndex >= VBOXNETADP_MAX_INSTANCES )
    {
        fprintf(stderr, "VBoxNetAdpCtl: Setting configuration for '%s' is not supported.\n", pcszNameIn);
        return ADPCTLERR_BAD_NAME;
    }
    snprintf(pszNameOut, cbNameOut, "vboxnet%d", iAdapterIndex);
    if (strcmp(pszNameOut, pcszNameIn))
    {
        fprintf(stderr, "VBoxNetAdpCtl: Invalid adapter name '%s'.\n", pcszNameIn);
        return ADPCTLERR_BAD_NAME;
    }

    return 0;
}

int Adapter::doIOCtl(unsigned long iCmd, VBOXNETADPREQ *pReq)
{
    int fd = open(VBOXNETADP_CTL_DEV_NAME, O_RDWR);
    if (fd == -1)
    {
        fprintf(stderr, "VBoxNetAdpCtl: Error while %s %s: ",
                iCmd == VBOXNETADP_CTL_REMOVE ? "removing" : "adding",
                pReq->szName[0] ? pReq->szName : "new interface");
        perror("failed to open " VBOXNETADP_CTL_DEV_NAME);
        return ADPCTLERR_NO_CTL_DEV;
    }

    int rc = ioctl(fd, iCmd, pReq);
    if (rc == -1)
    {
        fprintf(stderr, "VBoxNetAdpCtl: Error while %s %s: ",
                iCmd == VBOXNETADP_CTL_REMOVE ? "removing" : "adding",
                pReq->szName[0] ? pReq->szName : "new interface");
        perror("VBoxNetAdpCtl: ioctl failed for " VBOXNETADP_CTL_DEV_NAME);
        rc = ADPCTLERR_IOCTL_FAILED;
    }

    close(fd);

    return rc;
}


/*********************************************************************************************************************************
*   Global config file implementation                                                                                            *
*********************************************************************************************************************************/

#define VBOX_GLOBAL_NETWORK_CONFIG_PATH "/etc/vbox/networks.conf"
#define VBOXNET_DEFAULT_IPV4MASK "255.255.255.0"

class NetworkAddress
{
    public:
        bool isValidString(const char *pcszNetwork);
        bool isValid() { return m_fValid; };
        virtual bool matches(const char *pcszNetwork) = 0;
        virtual const char *defaultNetwork() = 0;
    protected:
        bool m_fValid;
};

bool NetworkAddress::isValidString(const char *pcszNetwork)
{
    RTNETADDRIPV4 addrv4;
    RTNETADDRIPV6 addrv6;
    int prefix;
    int rc = RTNetStrToIPv4Cidr(pcszNetwork, &addrv4, &prefix);
    if (RT_SUCCESS(rc))
        return true;
    rc = RTNetStrToIPv6Cidr(pcszNetwork, &addrv6, &prefix);
    return RT_SUCCESS(rc);
}

class NetworkAddressIPv4 : public NetworkAddress
{
    public:
        NetworkAddressIPv4(const char *pcszIpAddress, const char *pcszNetMask = VBOXNET_DEFAULT_IPV4MASK);
        virtual bool matches(const char *pcszNetwork);
        virtual const char *defaultNetwork() { return "192.168.56.1/21"; }; /* Matches defaults in VBox/Main/include/netif.h, see @bugref{10077}. */

    protected:
        bool isValidUnicastAddress(PCRTNETADDRIPV4 address);

    private:
        RTNETADDRIPV4 m_address;
        int m_prefix;
};

NetworkAddressIPv4::NetworkAddressIPv4(const char *pcszIpAddress, const char *pcszNetMask)
{
    int rc = RTNetStrToIPv4Addr(pcszIpAddress, &m_address);
    if (RT_SUCCESS(rc))
    {
        RTNETADDRIPV4 mask;
        rc = RTNetStrToIPv4Addr(pcszNetMask, &mask);
        if (RT_FAILURE(rc))
            m_fValid = false;
        else
            rc = RTNetMaskToPrefixIPv4(&mask, &m_prefix);
    }
#if 0 /* cmd.set() does not support CIDR syntax */
    else
        rc = RTNetStrToIPv4Cidr(pcszIpAddress, &m_address, &m_prefix);
#endif
    m_fValid = RT_SUCCESS(rc) && isValidUnicastAddress(&m_address);
}

bool NetworkAddressIPv4::isValidUnicastAddress(PCRTNETADDRIPV4 address)
{
    /* Multicast addresses are not allowed. */
    if ((address->au8[0] & 0xF0) == 0xE0)
        return false;

    /* Broadcast address is not allowed. */
    if (address->au32[0] == 0xFFFFFFFF) /* Endianess does not matter in this particual case. */
        return false;

    /* Loopback addresses are not allowed. */
    if ((address->au8[0] & 0xFF) == 0x7F)
        return false;

    return true;
}

bool NetworkAddressIPv4::matches(const char *pcszNetwork)
{
    RTNETADDRIPV4 allowedNet, allowedMask;
    int allowedPrefix;
    int rc = RTNetStrToIPv4Cidr(pcszNetwork, &allowedNet, &allowedPrefix);
    if (RT_SUCCESS(rc))
        rc = RTNetPrefixToMaskIPv4(allowedPrefix, &allowedMask);
    if (RT_FAILURE(rc))
        return false;
    return m_prefix >= allowedPrefix && (m_address.au32[0] & allowedMask.au32[0]) == (allowedNet.au32[0] & allowedMask.au32[0]);
}

class NetworkAddressIPv6 : public NetworkAddress
{
    public:
        NetworkAddressIPv6(const char *pcszIpAddress);
        virtual bool matches(const char *pcszNetwork);
        virtual const char *defaultNetwork() { return "FE80::/10"; };
    private:
        RTNETADDRIPV6 m_address;
        int m_prefix;
};

NetworkAddressIPv6::NetworkAddressIPv6(const char *pcszIpAddress)
{
    int rc = RTNetStrToIPv6Cidr(pcszIpAddress, &m_address, &m_prefix);
    m_fValid = RT_SUCCESS(rc);
}

bool NetworkAddressIPv6::matches(const char *pcszNetwork)
{
    RTNETADDRIPV6 allowedNet, allowedMask;
    int allowedPrefix;
    int rc = RTNetStrToIPv6Cidr(pcszNetwork, &allowedNet, &allowedPrefix);
    if (RT_SUCCESS(rc))
        rc = RTNetPrefixToMaskIPv6(allowedPrefix, &allowedMask);
    if (RT_FAILURE(rc))
        return false;
    RTUINT128U u128Provided, u128Allowed;
    return m_prefix >= allowedPrefix
        && RTUInt128Compare(RTUInt128And(&u128Provided, &m_address, &allowedMask), RTUInt128And(&u128Allowed, &allowedNet, &allowedMask)) == 0;
}


class GlobalNetworkPermissionsConfig
{
    public:
        bool forbids(const char *pcszIpAddress); /* address or address with mask in cidr */
        bool forbids(const char *pcszIpAddress, const char *pcszNetMask);

    private:
        bool forbids(NetworkAddress& address);
};

bool GlobalNetworkPermissionsConfig::forbids(const char *pcszIpAddress)
{
    NetworkAddressIPv6 addrv6(pcszIpAddress);

    if (addrv6.isValid())
        return forbids(addrv6);

    NetworkAddressIPv4 addrv4(pcszIpAddress);

    if (addrv4.isValid())
        return forbids(addrv4);

    fprintf(stderr, "Error: invalid address '%s'\n", pcszIpAddress);
    return true;
}

bool GlobalNetworkPermissionsConfig::forbids(const char *pcszIpAddress, const char *pcszNetMask)
{
    NetworkAddressIPv4 addrv4(pcszIpAddress, pcszNetMask);

    if (addrv4.isValid())
        return forbids(addrv4);

    fprintf(stderr, "Error: invalid address '%s' with mask '%s'\n", pcszIpAddress, pcszNetMask);
    return true;
}

bool GlobalNetworkPermissionsConfig::forbids(NetworkAddress& address)
{
    FILE *fp = fopen(VBOX_GLOBAL_NETWORK_CONFIG_PATH, "r");
    if (!fp)
    {
        if (verbose)
            fprintf(stderr, "Info: matching against default '%s' => %s\n", address.defaultNetwork(),
                address.matches(address.defaultNetwork()) ? "MATCH" : "no match");
        return !address.matches(address.defaultNetwork());
    }

    char *pszToken, szLine[1024];
    for (int line = 1; fgets(szLine, sizeof(szLine), fp); ++line)
    {
        pszToken = strtok(szLine, " \t\n");
        /* Skip anything except '*' lines */
        if (pszToken == NULL || strcmp("*", pszToken))
            continue;
        /* Match the specified address against each network */
        while ((pszToken = strtok(NULL, " \t\n")) != NULL)
        {
            if (!address.isValidString(pszToken))
            {
                fprintf(stderr, "Warning: %s(%d) invalid network '%s'\n", VBOX_GLOBAL_NETWORK_CONFIG_PATH, line, pszToken);
                continue;
            }
            if (verbose)
                fprintf(stderr, "Info: %s(%d) matching against '%s' => %s\n", VBOX_GLOBAL_NETWORK_CONFIG_PATH, line, pszToken,
                    address.matches(pszToken) ? "MATCH" : "no match");
            if (address.matches(pszToken))
            {
                fclose(fp);
                return false;
            }
        }
    }
    fclose(fp);
    return true;
}


/*********************************************************************************************************************************
*   Main logic, argument parsing, etc.                                                                                           *
*********************************************************************************************************************************/

#if defined(RT_OS_LINUX)
static CmdIfconfigLinux g_ifconfig;
static AdapterLinux g_adapter;
#elif defined(RT_OS_SOLARIS)
static CmdIfconfigSolaris g_ifconfig;
static AdapterSolaris g_adapter;
#else
static CmdIfconfigDarwin g_ifconfig;
static Adapter g_adapter;
#endif

static AddressCommand& chooseAddressCommand()
{
#if defined(RT_OS_LINUX)
    static CmdIpLinux g_ip;
    if (g_ip.isAvailable())
        return g_ip;
#endif
    return g_ifconfig;
}

int main(int argc, char *argv[])
{
    char szAdapterName[VBOXNETADP_MAX_NAME_LEN];
    int rc = RTR3InitExe(argc, &argv, 0 /*fFlags*/);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);


    AddressCommand& cmd = chooseAddressCommand();


    static const struct option options[] = {
        { "dry-run", no_argument, NULL, 'n' },
        { "verbose", no_argument, NULL, 'v' },
        { NULL, 0, NULL, 0 }
    };

    int ch;
    while ((ch = getopt_long(argc, argv, "nv", options, NULL)) != -1)
    {
        switch (ch)
        {
            case 'n':
                dry_run = true;
                verbose = true;
                break;

            case 'v':
                verbose = true;
                break;

            case '?':
            default:
                return usage();
        }
    }
    argc -= optind;
    argv += optind;

    if (argc == 0)
        return usage();


    /*
     * VBoxNetAdpCtl add
     */
    if (strcmp(argv[0], "add") == 0)
    {
        if (argc > 1)           /* extraneous args */
            return usage();

        /* Create a new interface, print its name. */
        *szAdapterName = '\0';
        rc = g_adapter.add(szAdapterName);
        if (rc == EXIT_SUCCESS)
            puts(szAdapterName);

        return rc;
    }


    /*
     * All other variants are of the form:
     *   VBoxNetAdpCtl if0 ...action...
     */
    const char * const ifname = argv[0];
    const char * const action = argv[1];
    if (argc < 2)
        return usage();


#ifdef RT_OS_LINUX
    /*
     * VBoxNetAdpCtl iface42 speed
     *
     * This ugly hack is needed for retrieving the link speed on
     * pre-2.6.33 kernels (see @bugref{6345}).
     *
     * This variant is used with any interface, not just host-only.
     */
    if (strcmp(action, "speed") == 0)
    {
        if (argc > 2)           /* extraneous args */
            return usage();

        if (strlen(ifname) >= IFNAMSIZ)
        {
            fprintf(stderr, "Interface name too long\n");
            return EXIT_FAILURE;
        }

        unsigned uSpeed = 0;
        rc = g_adapter.getSpeed(ifname, &uSpeed);
        if (rc == EXIT_SUCCESS)
            printf("%u", uSpeed);

        return rc;
    }
#endif  /* RT_OS_LINUX */


    /*
     * The rest of the actions only operate on host-only interfaces.
     */
    /** @todo Why the code below uses both ifname and szAdapterName? */
    rc = g_adapter.checkName(ifname, szAdapterName, sizeof(szAdapterName));
    if (rc != EXIT_SUCCESS)
        return rc;


    /*
     * VBoxNetAdpCtl vboxnetN remove
     */
    if (strcmp(action, "remove") == 0)
    {
        if (argc > 2)           /* extraneous args */
            return usage();

        /* Remove an existing interface */
        return g_adapter.remove(ifname);
    }

    /*
     * VBoxNetAdpCtl vboxnetN add
     */
    if (strcmp(action, "add") == 0)
    {
        if (argc > 2)           /* extraneous args */
            return usage();

        /* Create an interface with the given name, print its name. */
        rc = g_adapter.add(szAdapterName);
        if (rc == EXIT_SUCCESS)
            puts(szAdapterName);

        return rc;
    }


    /*
     * The rest of the actions are of the form
     *     VBoxNetAdpCtl vboxnetN $addr [...]
     *
     * Use the argument after the address to select the action.
     */
    /** @todo Do early verification of addr format here? */
    const char * const addr = argv[1];
    const char * const keyword = argv[2];

    GlobalNetworkPermissionsConfig config;

    /*
     * VBoxNetAdpCtl vboxnetN 1.2.3.4
     */
    if (keyword == NULL)
    {
        if (config.forbids(addr))
        {
            fprintf(stderr, "Error: permission denied\n");
            return -VERR_ACCESS_DENIED;
        }

        return cmd.set(ifname, addr);
    }

    /*
     * VBoxNetAdpCtl vboxnetN 1.2.3.4 netmask 255.255.255.0
     */
    if (strcmp(keyword, "netmask") == 0)
    {
        if (argc != 4)          /* too few or too many args */
            return usage();

        const char * const mask = argv[3];
        if (config.forbids(addr, mask))
        {
            fprintf(stderr, "Error: permission denied\n");
            return -VERR_ACCESS_DENIED;
        }

        return cmd.set(ifname, addr, mask);
    }

    /*
     * VBoxNetAdpCtl vboxnetN 1.2.3.4 remove
     */
    if (strcmp(keyword, "remove") == 0)
    {
        if (argc > 3)           /* extraneous args */
            return usage();

        return cmd.remove(ifname, addr);
    }

    return usage();
}
