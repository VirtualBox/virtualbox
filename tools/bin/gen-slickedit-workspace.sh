#!/usr/bin/env kmk_ash
# $Id: gen-slickedit-workspace.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# Script for generating a SlickEdit workspace.
#
# The gen-vscode-workspace.sh script is derived from this one, so fixes here
# may apply there too.
#

#
# Copyright (C) 2009-2024 Oracle and/or its affiliates.
#
# This file is part of VirtualBox base platform packages, as
# available from https://www.virtualbox.org.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation, in version 3 of the
# License.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <https://www.gnu.org/licenses>.
#
# SPDX-License-Identifier: GPL-3.0-only
#

#
# Include code we share with gen-vscode-workspace.sh
#
MY_SCRIPT_DIR=.
case "$0" in
    */*|*\\*)
        MY_SCRIPT_DIR=$(echo "$0" | kmk_sed -e 's,[/\][^/\][^/\]*$,,')
        ;;
esac
. "${MY_SCRIPT_DIR}/common-gen-workspace.inc.sh"


#
# Globals.
#
MY_PROJECT_FILES=""


#
# Parameters w/ defaults.
#
MY_OUT_DIR="SlickEdit"
MY_PRJ_PRF="VBox-"
MY_WS_NAME="VirtualBox.vpw"
MY_DBG=""
MY_WINDOWS_HOST=""
MY_OPT_MINIMAL=""
MY_OPT_USE_WILDCARDS="yes"
MY_OPT_ONLY_USERCPP=""


##
# Generate folders for the specified directories and files.
#
# @param    $1      The output (base) file name.
# @param    $2+     The files and directories to traverse.
my_generate_folder()
{
    MY_FILE="$1"
    shift

    # Zap existing file collections.
    > "${MY_FILE}-All.lst"
    > "${MY_FILE}-Sources.lst"
    > "${MY_FILE}-Headers.lst"
    > "${MY_FILE}-Assembly.lst"
    > "${MY_FILE}-Testcases.lst"
    > "${MY_FILE}-Others.lst"

    # Traverse the directories and files.
    while test $# -ge 1  -a  -n "${1}";
    do
        for f in ${MY_ROOT_DIR}/$1;
        do
            if test -d "${f}";
            then
                if test -z "${MY_OPT_USE_WILDCARDS}";
                then
                    my_sub_tree "${MY_FILE}" "${f}"
                else
                    case "${f}" in
                        ${MY_ROOT_DIR}/include*)
                            #my_sub_tree "${MY_FILE}" "${f}" "Headers"
                            my_wildcard "${MY_FILE}" "${f}" "Headers"
                            ;;
                        *)  my_wildcard "${MY_FILE}" "${f}"
                            ;;
                    esac
                fi
            else
                my_file "${MY_FILE}" "${f}"
            fi
        done
        shift
    done

    # Generate the folders.
    if test -s "${MY_FILE}-All.lst";
    then
        ${MY_SORT} "${MY_FILE}-All.lst"   | ${MY_SED} -e 's/<!-- sortkey: [^>]*>/          /' >> "${MY_FILE}"
    fi
    if test -s "${MY_FILE}-Sources.lst";
    then
        echo '        <Folder Name="Sources"  Filters="*.c;*.cpp;*.cpp.h;*.c.h;*.m;*.mm;*.pl;*.py;*.as">' >> "${MY_FILE}"
        ${MY_SORT} "${MY_FILE}-Sources.lst"   | ${MY_SED} -e 's/<!-- sortkey: [^>]*>/          /' >> "${MY_FILE}"
        echo '        </Folder>' >> "${MY_FILE}"
    fi
    if test -s "${MY_FILE}-Headers.lst";
    then
        if test -z "${MY_OPT_USE_WILDCARDS}";
        then
            echo '        <Folder Name="Headers"  Filters="*.h;*.hpp">' >> "${MY_FILE}"
        else
            echo '        <Folder Name="Headers"  Filters="">' >> "${MY_FILE}"
        fi
        ${MY_SORT} "${MY_FILE}-Headers.lst"   | ${MY_SED} -e 's/<!-- sortkey: [^>]*>/          /' >> "${MY_FILE}"
        echo '        </Folder>' >> "${MY_FILE}"
    fi
    if test -s "${MY_FILE}-Assembly.lst";
    then
        echo '        <Folder Name="Assembly" Filters="*.asm;*.s;*.S;*.inc;*.mac">' >> "${MY_FILE}"
        ${MY_SORT} "${MY_FILE}-Assembly.lst"  | ${MY_SED} -e 's/<!-- sortkey: [^>]*>/          /' >> "${MY_FILE}"
        echo '        </Folder>' >> "${MY_FILE}"
    fi
    if test -s "${MY_FILE}-Testcases.lst";
    then
        echo '        <Folder Name="Testcases" Filters="tst*;">' >> "${MY_FILE}"
        ${MY_SORT} "${MY_FILE}-Testcases.lst" | ${MY_SED} -e 's/<!-- sortkey: [^>]*>/          /' >> "${MY_FILE}"
        echo '        </Folder>' >> "${MY_FILE}"
    fi
    if test -s "${MY_FILE}-Others.lst";
    then
        echo '        <Folder Name="Others"   Filters="">' >> "${MY_FILE}"
        ${MY_SORT} "${MY_FILE}-Others.lst"    | ${MY_SED} -e 's/<!-- sortkey: [^>]*>/          /' >> "${MY_FILE}"
        echo '        </Folder>' >> "${MY_FILE}"
    fi

    # Cleanup
    ${MY_RM}  "${MY_FILE}-All.lst" "${MY_FILE}-Sources.lst" "${MY_FILE}-Headers.lst" "${MY_FILE}-Assembly.lst" \
        "${MY_FILE}-Testcases.lst" "${MY_FILE}-Others.lst"
}

##
# Function generating a project build config.
#
# @param    $1      The project file name.
# @param    $2      Build config name.
# @param    $3      Extra kBuild command line options, variant 1.
# @param    $4      Extra kBuild command line options, variant 2.
# @param    $4+     Include directories.
# @param    $N      --end-includes
my_generate_project_config()
{
    MY_FILE="${1}";
    MY_CFG_NAME="${2}";
    MY_KMK_EXTRAS1="${3}";
    MY_KMK_EXTRAS2="${4}";
    MY_KMK_EXTRAS3="${5}";
    MY_KMK_EXTRAS4="${6}";
    shift; shift; shift; shift; shift; shift;

    echo '    <Config Name="'"${MY_CFG_NAME}"'" OutputFile="" CompilerConfigName="Latest Version">'             >> "${MY_FILE}"
    echo '        <Menu>'                                                                                       >> "${MY_FILE}"

    echo '            <Target Name="Compile" MenuCaption="&amp;Compile" CaptureOutputWith="ProcessBuffer"'      >> "${MY_FILE}"
    echo '                    SaveOption="SaveCurrent" RunFromDir="%p" ClearProcessBuffer="1">'                 >> "${MY_FILE}"
    echo -n '                <Exec CmdLine="'"${MY_KMK_INVOCATION} ${MY_KMK_EXTRAS1}"' -C %p %n.o'              >> "${MY_FILE}"
    if test -n "${MY_KMK_EXTRAS2}"; then
        echo -n " && ${MY_KMK_INVOCATION} ${MY_KMK_EXTRAS2} -C %p %n.o" >> "${MY_FILE}"
    fi
    if test -n "${MY_KMK_EXTRAS3}"; then
        echo -n " && ${MY_KMK_INVOCATION} ${MY_KMK_EXTRAS3} -C %p %n.o" >> "${MY_FILE}"
    fi
    if test -n "${MY_KMK_EXTRAS4}"; then
        echo -n " && ${MY_KMK_INVOCATION} ${MY_KMK_EXTRAS4} -C %p %n.o" >> "${MY_FILE}"
    fi
    echo  '"/>' >> "${MY_FILE}"
    echo '            </Target>'                                                                                >> "${MY_FILE}"

    echo '            <Target Name="Build" MenuCaption="&amp;Build"CaptureOutputWith="ProcessBuffer"'           >> "${MY_FILE}"
    echo '                    SaveOption="SaveWorkspaceFiles" RunFromDir="%rw" ClearProcessBuffer="1">'         >> "${MY_FILE}"
    echo -n '                <Exec CmdLine="'"${MY_KMK_INVOCATION} ${MY_KMK_EXTRAS1}"' -C %rw'                  >> "${MY_FILE}"
    if test -n "${MY_KMK_EXTRAS2}"; then
        echo -n " && ${MY_KMK_INVOCATION} ${MY_KMK_EXTRAS2} -C %rw" >> "${MY_FILE}"
    fi
    if test -n "${MY_KMK_EXTRAS3}"; then
        echo -n " && ${MY_KMK_INVOCATION} ${MY_KMK_EXTRAS3} -C %rw" >> "${MY_FILE}"
    fi
    if test -n "${MY_KMK_EXTRAS4}"; then
        echo -n " && ${MY_KMK_INVOCATION} ${MY_KMK_EXTRAS4} -C %rw" >> "${MY_FILE}"
    fi
    echo  '"/>' >> "${MY_FILE}"
    echo '            </Target>'                                                                                >> "${MY_FILE}"

    echo '            <Target Name="Rebuild" MenuCaption="&amp;Rebuild" CaptureOutputWith="ProcessBuffer"'      >> "${MY_FILE}"
    echo '                    SaveOption="SaveWorkspaceFiles" RunFromDir="%rw" ClearProcessBuffer="1">'         >> "${MY_FILE}"
    echo -n '                <Exec CmdLine="'"${MY_KMK_INVOCATION} ${MY_KMK_EXTRAS1}"' -C %rw'                  >> "${MY_FILE}"
    if test -n "${MY_KMK_EXTRAS2}"; then
        echo -n " && ${MY_KMK_INVOCATION} ${MY_KMK_EXTRAS2} -C %rw rebuild" >> "${MY_FILE}"
    fi
    if test -n "${MY_KMK_EXTRAS3}"; then
        echo -n " && ${MY_KMK_INVOCATION} ${MY_KMK_EXTRAS3} -C %rw rebuild" >> "${MY_FILE}"
    fi
    if test -n "${MY_KMK_EXTRAS4}"; then
        echo -n " && ${MY_KMK_INVOCATION} ${MY_KMK_EXTRAS4} -C %rw rebuild" >> "${MY_FILE}"
    fi
    echo  '"/>' >> "${MY_FILE}"
    echo '            </Target>'                                                                                >> "${MY_FILE}"

    #echo '            <Target Name="Debug" MenuCaption="&amp;Debug" SaveOption="SaveNone" RunFromDir="%rw">'    >> "${MY_FILE}"
    #echo '                <Exec/>'                                                                              >> "${MY_FILE}"
    #echo '            </Target>'                                                                                >> "${MY_FILE}"
    #echo '            <Target Name="Execute" MenuCaption="E&amp;xecute" SaveOption="SaveNone" RunFromDir="%rw">'>> "${MY_FILE}"
    #echo '                <Exec/>'                                                                              >> "${MY_FILE}"
    #echo '            </Target>'                                                                                >> "${MY_FILE}"
    echo '        </Menu>'                                                                                      >> "${MY_FILE}"

    #
    # Include directories.
    #
    echo '        <Includes>'                                                                                   >> "${MY_FILE}"
    while test $# -ge 1  -a  "${1}" != "--end-includes";
    do
        for f in $1;
        do
            my_abs_dir ${f}
            echo '            <Include Dir="'"${MY_ABS_DIR}/"'"/>'                                              >> "${MY_FILE}"
        done
        shift
    done
    shift
    echo '        </Includes>'                                                                                  >> "${MY_FILE}"
    echo '    </Config>'                                                                                        >> "${MY_FILE}"
}


##
# Function generating a project.
#
# This is called from my_generate_all_projects() in the common code.
#
# @param    $1      The project file name.
# @param    $2      The project working directory.
# @param    $3      Dummy separator.
# @param    $4+     Include directories.
# @param    $N      --end-includes
# @param    $N+1    Directory sub-trees and files to include in the project.
#
my_generate_project()
{
    MY_FILE="${MY_PRJ_PRF}${1}.vpj";
    echo "Generating ${MY_FILE}..."
    MY_WRK_DIR="${2}"
    shift
    shift
    shift

    # Add it to the project list for workspace construction later on.
    MY_PROJECT_FILES="${MY_PROJECT_FILES} ${MY_FILE}"


    #
    # Generate the head bits.
    #
    echo '<!DOCTYPE Project SYSTEM "http://www.slickedit.com/dtd/vse/10.0/vpj.dtd">'                                    >  "${MY_FILE}"
    echo '<Project'                                                                                                     >> "${MY_FILE}"
    echo '        Version="10.0"'                                                                                       >> "${MY_FILE}"
    echo '        VendorName="SlickEdit"'                                                                               >> "${MY_FILE}"
    echo '        VCSProject="Subversion:"'                                                                             >> "${MY_FILE}"
#    echo '        Customized="1"'                                                                                       >> "${MY_FILE}"
#    echo '        WorkingDir="."'                                                                                       >> "${MY_FILE}"
    my_abs_dir "${MY_WRK_DIR}"                                                                                          >> "${MY_FILE}"
    echo '        WorkingDir="'"${MY_ABS_DIR}"'"'                                                                       >> "${MY_FILE}"
    echo '        >'                                                                                                    >> "${MY_FILE}"
    my_generate_project_config "${MY_FILE}" "Default" "" "" "" "" $*
    my_generate_project_config "${MY_FILE}" "Debug + hardening"     "KBUILD_TYPE=debug VBOX_WITH_HARDENING=1"   "" "" "" $*
    my_generate_project_config "${MY_FILE}" "Release + hardening"   "KBUILD_TYPE=release VBOX_WITH_HARDENING=1" "" "" "" $*
    my_generate_project_config "${MY_FILE}" "Debug+Release + hardening" \
        "KBUILD_TYPE=debug VBOX_WITH_HARDENING=1" \
        "KBUILD_TYPE=release VBOX_WITH_HARDENING=1" \
        "" "" $*
    my_generate_project_config "${MY_FILE}" "Debug w/o hardening"   "KBUILD_TYPE=debug VBOX_WITHOUT_HARDENING=1"   "" "" $*
    my_generate_project_config "${MY_FILE}" "Release w/o hardening" "KBUILD_TYPE=release VBOX_WITHOUT_HARDENING=1" "" "" $*
    my_generate_project_config "${MY_FILE}" "Debug+Release w/o hardening" \
        "KBUILD_TYPE=debug VBOX_WITHOUT_HARDENING=1" \
        "KBUILD_TYPE=release VBOX_WITHOUT_HARDENING=1" \
        "" "" $*
    my_generate_project_config "${MY_FILE}" "Debug+Release with and without hardening" \
        "KBUILD_TYPE=debug VBOX_WITH_HARDENING=1" \
        "KBUILD_TYPE=release VBOX_WITH_HARDENING=1" \
        "KBUILD_TYPE=debug VBOX_WITHOUT_HARDENING=1" \
        "KBUILD_TYPE=release VBOX_WITHOUT_HARDENING=1" \
        $*

    while test $# -ge 1  -a  "${1}" != "--end-includes";
    do
        shift;
    done;
    shift;

    #
    # Process directories+files and create folders.
    #
    echo '    <Files>'                                                                                                  >> "${MY_FILE}"
    my_generate_folder "${MY_FILE}" $*
    echo '    </Files>'                                                                                                 >> "${MY_FILE}"

    #
    # The tail.
    #
    echo '</Project>'                                                                                                   >> "${MY_FILE}"

    return 0
}


##
# Generate the workspace
#
my_generate_workspace()
{
    MY_FILE="${MY_WS_NAME}"
    echo "Generating ${MY_FILE}..."
    echo '<!DOCTYPE Workspace SYSTEM "http://www.slickedit.com/dtd/vse/10.0/vpw.dtd">'   > "${MY_FILE}"
    echo '<Workspace Version="10.0" VendorName="SlickEdit">'                            >> "${MY_FILE}"
    echo '    <Projects>'                                                               >> "${MY_FILE}"
    for i in ${MY_PROJECT_FILES};
    do
        echo '        <Project File="'"${i}"'" />'                                      >> "${MY_FILE}"
    done
    echo '    </Projects>'                                                              >> "${MY_FILE}"
    echo '</Workspace>'                                                                 >> "${MY_FILE}"
    return 0;
}


##
# Generate stuff
#
my_generate_usercpp_h()
{
    #
    # Probe the slickedit user config, picking the most recent version.
    #
    MY_VSLICK_DB_OLD=
    if test -z "${MY_SLICK_CONFIG}"; then
        if test -d "${HOME}/Library/Application Support/SlickEdit"; then
            MY_SLICKDIR_="${HOME}/Library/Application Support/SlickEdit"
            MY_USERCPP_H="unxcpp.h"
            MY_VSLICK_DB="vslick.sta" # was .stu earlier, 24 is using .sta.
            MY_VSLICK_DB_OLD="vslick.stu"
        elif test -d "${HOMEDRIVE}${HOMEPATH}/AppData/Local/SlickEdit"; then
            MY_SLICKDIR_="${HOMEDRIVE}${HOMEPATH}/AppData/Local/SlickEdit"
            MY_USERCPP_H="usercpp.h"
            MY_VSLICK_DB="vslick.sta"
        elif test -d "${HOMEDRIVE}${HOMEPATH}/Documents/My SlickEdit Config"; then
            MY_SLICKDIR_="${HOMEDRIVE}${HOMEPATH}/Documents/My SlickEdit Config"
            MY_USERCPP_H="usercpp.h"
            MY_VSLICK_DB="vslick.sta"
        else
            MY_SLICKDIR_="${HOME}/.slickedit"
            MY_USERCPP_H="unxcpp.h"
            MY_VSLICK_DB="vslick.sta"
            MY_VSLICK_DB_OLD="vslick.stu"
        fi
    else
        MY_SLICKDIR_="${MY_SLICK_CONFIG}"
        if test -n "${MY_WINDOWS_HOST}"; then
            MY_USERCPP_H="usercpp.h"
            MY_VSLICK_DB="vslick.sta"
        else
            MY_USERCPP_H="unxcpp.h"
            MY_VSLICK_DB="vslick.sta"
            MY_VSLICK_DB_OLD="vslick.stu"
        fi
        # MacOS: Implement me!
    fi

    MY_VER_NUM="0"
    MY_VER="0.0.0"
    for subdir in "${MY_SLICKDIR_}/"*;
    do
        if test    -f "${subdir}/${MY_USERCPP_H}"  \
                -o -f "${subdir}/${MY_VSLICK_DB}" \
                -o '(' -n "${MY_VSLICK_DB_OLD}" -a -f "${subdir}/${MY_VSLICK_DB_OLD}" ')'; then
            MY_CUR_VER_NUM=0
            MY_CUR_VER=`echo "${subdir}" | ${MY_SED} -e 's,^.*/,,g'`

            # Convert the dotted version number to an integer, checking that
            # it is all numbers in the process.
            set `echo "${MY_CUR_VER}" | ${MY_SED} 's/\./ /g' `
            i=24010000   # == 70*70*70*70; max major version 89.
            while test $# -gt 0;
            do
                if ! ${MY_EXPR} "$1" + 1 > /dev/null 2> /dev/null; then
                    MY_CUR_VER_NUM=0;
                    break
                fi
                if test "$i" -gt 0; then
                    MY_CUR_VER_NUM=$((${MY_CUR_VER_NUM} + $1 * $i))
                    i=$(($i / 70))
                fi
                shift
            done

            # More recent that what we have?
            if test "${MY_CUR_VER_NUM}" -gt "${MY_VER_NUM}"; then
                MY_VER_NUM="${MY_CUR_VER_NUM}"
                MY_VER="${MY_CUR_VER}"
            fi
        fi
    done

    MY_SLICKDIR="${MY_SLICKDIR_}/${MY_VER}"
    MY_USERCPP_H_FULL="${MY_SLICKDIR}/${MY_USERCPP_H}"
    if test -d "${MY_SLICKDIR}"; then
        echo "Found SlickEdit v${MY_VER} preprocessor file: ${MY_USERCPP_H_FULL}"
    else
        echo "Failed to locate SlickEdit preprocessor file. You need to manually merge ${MY_USERCPP_H}."
        echo "dbg: MY_SLICKDIR=${MY_SLICKDIR}  MY_USERCPP_H_FULL=${MY_USERCPP_H_FULL}"
        MY_USERCPP_H_FULL=""
    fi

    # Generate our stuff.
    MY_FILE="${MY_USERCPP_H}"
    ${MY_CAT} > ${MY_FILE} <<EOF
#define IN_SLICKEDIT
#define RT_C_DECLS_BEGIN
#define RT_C_DECLS_END
#define RT_NOTHROW_PROTO
#define RT_NOTHROW_DEF
#define RT_NO_THROW_PROTO
#define RT_NO_THROW_DEF
#define RT_NOEXCEPT
#define RT_OVERRIDE
#define RT_THROW(type)                  throw(type)
#define RT_GCC_EXTENSION
#define RT_COMPILER_GROKS_64BIT_BITFIELDS
#define RT_COMPILER_WITH_80BIT_LONG_DOUBLE
#define RT_DECL_NTAPI(type)             type
#define RT_CONCAT(a,b)                  a##b
#define RT_CONCAT3(a,b,c)               a##b##c
#define RT_CONCAT4(a,b,c,d)             a##b##c##d

#define ATL_NO_VTABLE
#define BEGIN_COM_MAP(a)
#define COM_INTERFACE_ENTRY(a)
#define COM_INTERFACE_ENTRY2(a,b)
#define COM_INTERFACE_ENTRY3(a,b,c)
#define COM_INTERFACE_ENTRY4(a,b,c,d)
#define END_COM_MAP(a)

#define COM_DECL_READONLY_ENUM_AND_COLLECTION(a)
#define COMGETTER(n)                    n
#define COMSETTER(n)                    n
#define ComSafeArrayIn(t,a)             t a[]
#define ComSafeArrayOut(t,a)            t * a[]
#define DECLARE_CLASSFACTORY(a)
#define DECLARE_CLASSFACTORY_SINGLETON(a)
#define DECLARE_REGISTRY_RESOURCEID(a)
#define DECLARE_NOT_AGGREGATABLE(a)
#define DECLARE_PROTECT_FINAL_CONSTRUCT(a)
#define DECLARE_EMPTY_CTOR_DTOR(a)      a(); ~a();
#define DEFINE_EMPTY_CTOR_DTOR(a)       a::a() {}   a::~a() {}
#define DECLARE_TRANSLATE_METHODS(cls) \
    static inline const char *tr(const char *aSourceText, \
                                 const char *aComment = NULL, \
                                 const int   aNum = -1) \
    { \
        return VirtualBoxTranslator::translate(#cls, aSourceText, aComment, aNum); \
    }
#define DECLARE_COMMON_CLASS_METHODS(cls) \
    DECLARE_EMPTY_CTOR_DTOR(cls) \
    DECLARE_TRANSLATE_METHODS(cls)
#define NS_DECL_ISUPPORTS
#define NS_IMETHOD                      virtual nsresult
#define NS_IMETHOD_(type)               virtual type
#define NS_IMETHODIMP                   nsresult
#define NS_IMETHODIMP_(type)            type
#define PARSERS_EXPORT
EOF
    if test -n "${MY_WINDOWS_HOST}"; then
        ${MY_CAT} >> ${MY_FILE} <<EOF
#define COM_STRUCT_OR_CLASS(I)          struct I
#define STDMETHOD(m)                    virtual HRESULT m
#define STDMETHOD_(type,m)              virtual type m
#define STDMETHODIMP                    HRESULT
#define STDMETHODIMP_(type)             type
EOF
    else
        ${MY_CAT} >> ${MY_FILE} <<EOF
#define COM_STRUCT_OR_CLASS(I)          class I
#define STDMETHOD(m)                    virtual nsresult m
#define STDMETHOD_(type,m)              virtual type m
#define STDMETHODIMP                    nsresult
#define STDMETHODIMP_(type)             type
EOF
    fi
    ${MY_CAT} >> ${MY_FILE} <<EOF
#define VBOX_SCRIPTABLE(a)              public a
#define VBOX_SCRIPTABLE_IMPL(a)
#define VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT(a)

#define CTX_SUFF(var)                   var##R3
#define CTXAllSUFF(var)                 var##R3
#define CTXSUFF(var)                    var##HC
#define OTHERCTXSUFF(var)               var##GC
#define CTXALLMID(first, last)          first##R3##last
#define CTXMID(first, last)             first##HC##last
#define OTHERCTXMID(first, last)        first##GC##last
#define CTXTYPE(GCType, R3Type, R0Type) R3Type
#define RCTYPE(RCType, HCType)          RCType
#define GCTYPE(GCType, HCType)          GCType
#define RCPTRTYPE(RCType)               RCType
#define GCPTRTYPE(GCType)               GCType
#define HCPTRTYPE(HCType)               HCType
#define R3R0PTRTYPE(HCType)             HCType
#define R0PTRTYPE(R3Type)               R3Type
#define R3PTRTYPE(R0Type)               R0Type
#define RT_SRC_POS                      __FILE__, __LINE__, __PRETTY_FUNCTION__
#define RT_SRC_POS_DECL                 const char *pszFile, unsigned iLine, const char *pszFunction
#define RT_SRC_POS_ARGS                 pszFile, iLine, pszFunction
#define RTCALL
#define RT_IPRT_FORMAT_ATTR(a_iFmt, a_iArgs)
#define RT_IPRT_FORMAT_ATTR_MAYBE_NULL(a_iFmt, a_iArgs)
#define RT_IPRT_CALLREQ_ATTR(a_iFn, a_iArgc, a_iArgs)
#define RT_NOCRT(a_Name)                a_Name
#define DECLASM(type)                   type
#define DECLINLINE(type)                inline type
#define DECL_INLINE_THROW(type)         inline type
#define DECL_FORCE_INLINE(type)         inline type
#define DECL_FORCE_INLINE_THROW(type)   inline type
#define DECL_INVALID(type)              type
#define DECL_NO_RETURN(a_RetType)       a_RetType
#define DECLCALLBACK(a_RetType)         a_RetType
#define DECLCALLBACKMEMBER(a_RetType, a_Name, a_Args)                   a_RetType (*a_Name) a_Args
#define DECLCALLBACKMEMBER_EX(a_RetType, a_CallConv, a_Name, a_Args)    a_RetType (*a_Name) a_Args
#define DECLR0CALLBACKMEMBER(a_RetType, a_CallConv, a_Name, a_Args)     a_RetType (*a_Name) a_Args
#define DECLR3CALLBACKMEMBER(a_RetType, a_CallConv, a_Name, a_Args)     a_RetType (*a_Name) a_Args
#define DECLRCCALLBACKMEMBER(a_RetType, a_CallConv, a_Name, a_Args)     a_RetType (*a_Name) a_Args
#define DECLRGCALLBACKMEMBER(a_RetType, a_CallConv, a_Name, a_Args)     a_RetType (*a_Name) a_Args
#define DECLCALLBACKPTR(a_RetType, a_Name, a_Args)                      a_RetType (*a_Name) a_Args
#define DECLCALLBACKPTR_EX(a_RetType, a_CallConv, a_Name, a_Args)       a_RetType (*a_Name) a_Args
#define DECLCALLBACKTYPE(a_RetType, a_Name, a_Args)                     a_RetType   a_Name  a_Args
#define DECLCALLBACKTYPE_EX(a_RetType, a_CallConv, a_Name, a_Args)      a_RetType   a_Name  a_Args
#define DECLEXPORT(a_RetType)           a_RetType
#define DECL_EXPORT_NOTHROW(a_RetType)  a_RetType
#define DECLHIDDEN(a_RetType)           a_RetType
#define DECL_HIDDEN_CALLBACK(a_RetType) a_RetType
#define DECL_HIDDEN_NOTHROW(a_RetType)  a_RetType
#define DECL_HIDDEN_THROW(a_RetType)    a_RetType
#define DECLIMPORT(a_RetType)           a_RetType
#define DECL_IMPORT_NOTHROW(a_RetType)  a_RetType

#define DECLVBGL(a_RetType)             a_RetType

#define PDMDEVINSINT_DECLARED           1
#define VBOX_WITH_HGCM                  1
#define VBOXCALL

#define HM_NAMELESS_UNION_TAG(a_Tag)
#define HM_UNION_NM(a_Nm)
#define HM_STRUCT_NM(a_Nm)

#define PGM_ALL_CB_DECL(type)           type
#define PGM_ALL_CB2_DECL(type)          type
#define PGM_CTX(a,b)                    b
#define PGM_CTX3(a,b,c)                 c
#define PGM_GST_NAME(name)              PGM_GST_NAME_AMD64(name)
#define PGM_GST_NAME_REAL(name)         pgmGstReal##name
#define PGM_GST_NAME_PROT(name)         pgmGstProt##name
#define PGM_GST_NAME_32BIT(name)        pgmGst32Bit##name
#define PGM_GST_NAME_PAE(name)          pgmGstPAE##name
#define PGM_GST_NAME_AMD64(name)        pgmGstAMD64##name
#define PGM_GST_DECL(type, name)        type PGM_GST_NAME(name)
#define PGM_SHW_NAME(name)              PGM_GST_NAME_AMD64(name)
#define PGM_SHW_NAME_32BIT(name)        pgmShw32Bit##name
#define PGM_SHW_NAME_PAE(name)          pgmShwPAE##name
#define PGM_SHW_NAME_AMD64(name)        pgmShwAMD64##name
#define PGM_SHW_NAME_NESTED(name)       pgmShwNested##name
#define PGM_SHW_NAME_EPT(name)          pgmShwEPT##name
#define PGM_SHW_DECL(type, name)        type PGM_SHW_NAME(name)
#define PGM_BTH_NAME(name)              PGM_BTH_NAME_NESTED_AMD64(name)
#define PGM_BTH_NAME_32BIT_REAL(name)   pgmBth##name
#define PGM_BTH_NAME_32BIT_PROT(name)   pgmBth##name
#define PGM_BTH_NAME_32BIT_32BIT(name)  pgmBth##name
#define PGM_BTH_NAME_PAE_REAL(name)     pgmBth##name
#define PGM_BTH_NAME_PAE_PROT(name)     pgmBth##name
#define PGM_BTH_NAME_PAE_32BIT(name)    pgmBth##name
#define PGM_BTH_NAME_PAE_PAE(name)      pgmBth##name
#define PGM_BTH_NAME_AMD64_PROT(name)   pgmBth##name
#define PGM_BTH_NAME_AMD64_AMD64(name)  pgmBth##name
#define PGM_BTH_NAME_NESTED_REAL(name)  pgmBth##name
#define PGM_BTH_NAME_NESTED_PROT(name)  pgmBth##name
#define PGM_BTH_NAME_NESTED_32BIT(name) pgmBth##name
#define PGM_BTH_NAME_NESTED_PAE(name)   pgmBth##name
#define PGM_BTH_NAME_NESTED_AMD64(name) pgmBth##name
#define PGM_BTH_NAME_EPT_REAL(name)     pgmBth##name
#define PGM_BTH_NAME_EPT_PROT(name)     pgmBth##name
#define PGM_BTH_NAME_EPT_32BIT(name)    pgmBth##name
#define PGM_BTH_NAME_EPT_PAE(name)      pgmBth##name
#define PGM_BTH_NAME_EPT_AMD64(name)    pgmBth##name
#define PGM_BTH_DECL(type, name)        type PGM_BTH_NAME(name)

#define FNIEMOP_STUB(a_Name)            VBOXSTRICTRC a_Name(PIEMCPU pIemCpu) { return VERR_NOT_IMPLEMENTED; }
#define FNIEMOP_DEF(a_Name)             VBOXSTRICTRC a_Name(PIEMCPU pIemCpu)
#define FNIEMOP_DEF_1(a_Name, a_Type0, a_Name0) static VBOXSTRICTRC a_Name(PIEMCPU pIemCpu, a_Type0 a_Name0)
#define FNIEMOP_DEF_2(a_Name, a_Type0, a_Name0, a_Type1, a_Name1) static VBOXSTRICTRC a_Name(PIEMCPU pIemCpu, a_Type0 a_Name0, a_Type1 a_Name1)
#define FNIEMOPRM_DEF(a_Name)           static VBOXSTRICTRC a_Name(PIEMCPU pIemCpu, uint8_t bBm)
#define IEM_CIMPL_DEF_0(a_Name)         static VBOXSTRICTRC a_Name(PIEMCPU pIemCpu)
#define IEM_CIMPL_DEF_1(a_Name, a_Type0, a_Name0) static VBOXSTRICTRC a_Name(PIEMCPU pIemCpu, , a_Type0 a_Name0)
#define IEM_CIMPL_DEF_2(a_Name, a_Type0, a_Name0, a_Type1, a_Name1) static VBOXSTRICTRC a_Name(PIEMCPU pIemCpu, a_Type0 a_Name0, a_Type1 a_Name1)
#define IEM_CIMPL_DEF_3(a_Name, a_Type0, a_Name0, a_Type1, a_Name1, a_Type2, a_Name2)  static VBOXSTRICTRC a_Name(PIEMCPU pIemCpu, a_Type0 a_Name0, a_Type1 a_Name1, a_Type2 a_Name2)
#define IEM_CIMPL_DEF_4(a_Name, a_Type0, a_Name0, a_Type1, a_Name1, a_Type2, a_Name2, a_Type3, a_Name3)  static VBOXSTRICTRC a_Name(PIEMCPU pIemCpu, a_Type0 a_Name0, a_Type1 a_Name1, a_Type2 a_Name2, a_Type3 a_Name3)
#define IEM_DECL_IMPL_DEF(a_RetType, a_Name, a_ArgList)   a_RetType a_Name a_ArgList
#define IEM_DECL_IEMTHREADEDFUNC_DEF(a_Name)   VBOXSTRICTRC a_Name(PVMCPU pVCpu, uint64_t uParam0, uint64_t uParam1, uint64_t uParam2)
#define IEM_DECL_IEMTHREADEDFUNC_PROTO(a_Name) VBOXSTRICTRC a_Name(PVMCPU pVCpu, uint64_t uParam0, uint64_t uParam1, uint64_t uParam2)
#define IEM_DECL_NATIVE_HLP_DEF(a_RetType, a_Name, a_ArgList)   a_RetType a_Name a_ArgList
#define IEM_DECL_NATIVE_HLP_PROTO(a_RetType, a_Name, a_ArgList) a_RetType a_Name a_ArgList
#define IEM_DECL_IEMNATIVERECOMPFUNC_DEF(a_Name) uint32_t a_Name(PIEMRECOMPILERSTATE pReNative, uint32_t off, PCIEMTHRDEDCALLENTRY pCallEntry)
#define IEM_DECL_IEMNATIVERECOMPFUNC_PROTO(a_Name) FNIEMNATIVERECOMPFUNC a_Name
#define IEM_MC_LOCAL(a_Type, a_Name)                       a_Type a_Name
#define IEM_MC_ARG(a_Type, a_Name, a_iArg)                 a_Type a_Name
#define IEM_MC_ARG_CONST(a_Type, a_Name, a_Value, a_iArg)  a_Type const a_Name = a_Value
#define IEM_STATIC

#define RTASN1_IMPL_GEN_SEQ_OF_TYPEDEFS_AND_PROTOS(a_SeqOfType, a_ItemType, a_DeclMacro, a_ImplExtNm) typedef struct a_SeqOfType { RTASN1SEQUENCECORE SeqCore; RTASN1ALLOCATION Allocation; uint32_t cItems; RT_CONCAT(P,a_ItemType) paItems; } a_SeqOfType; typedef a_SeqOfType *P##a_SeqOfType, const *PC##a_SeqOfType; int a_ImplExtNm##_DecodeAsn1(struct RTASN1CURSOR *pCursor, uint32_t fFlags, P##a_SeqOfType pThis, const char *pszErrorTag); int a_ImplExtNm##_Compare(PC##a_SeqOfType pLeft, PC##a_SeqOfType pRight)
#define RTASN1_IMPL_GEN_SET_OF_TYPEDEFS_AND_PROTOS(a_SetOfType, a_ItemType, a_DeclMacro, a_ImplExtNm) typedef struct a_SetOfType { RTASN1SETCORE SetCore; RTASN1ALLOCATION Allocation; uint32_t cItems; RT_CONCAT(P,a_ItemType) paItems; } a_SetOfType; typedef a_SetOfType *P##a_SetOfType, const *PC##a_SetOfType; int a_ImplExtNm##_DecodeAsn1(struct RTASN1CURSOR *pCursor, uint32_t fFlags, P##a_SetOfType pThis, const char *pszErrorTag); int a_ImplExtNm##_Compare(PC##a_SetOfType pLeft, PC##a_SetOfType pRight)
#define RTASN1TYPE_STANDARD_PROTOTYPES_NO_GET_CORE(a_TypeNm, a_DeclMacro, a_ImplExtNm) int  a_ImplExtNm##_Init(P##a_TypeNm pThis, PCRTASN1ALLOCATORVTABLE pAllocator); int  a_ImplExtNm##_Clone(P##a_TypeNm pThis, PC##a_TypeNm) pSrc, PCRTASN1ALLOCATORVTABLE pAllocator); void a_ImplExtNm##_Delete(P##a_TypeNm pThis); int  a_ImplExtNm##_Enum(P##a_TypeNm pThis, PFNRTASN1ENUMCALLBACK pfnCallback, uint32_t uDepth, void *pvUser); int  a_ImplExtNm##_Compare(PC##a_TypeNm) pLeft, PC##a_TypeNm pRight); int  a_ImplExtNm##_DecodeAsn1(PRTASN1CURSOR pCursor, uint32_t fFlags, P##a_TypeNm pThis, const char *pszErrorTag); int  a_ImplExtNm##_CheckSanity(PC##a_TypeNm pThis, uint32_t fFlags, PRTERRINFO pErrInfo, const char *pszErrorTag)
#define RTASN1TYPE_STANDARD_PROTOTYPES(a_TypeNm, a_DeclMacro, a_ImplExtNm, a_Asn1CoreNm) inline PRTASN1CORE a_ImplExtNm##_GetAsn1Core(PC##a_TypeNm pThis) { return (PRTASN1CORE)&pThis->a_Asn1CoreNm; } inline bool a_ImplExtNm##_IsPresent(PC##a_TypeNm pThis) { return pThis && RTASN1CORE_IS_PRESENT(&pThis->a_Asn1CoreNm); } RTASN1TYPE_STANDARD_PROTOTYPES_NO_GET_CORE(a_TypeNm, a_DeclMacro, a_ImplExtNm)

#define RTLDRELF_NAME(name)             rtldrELF64##name
#define RTLDRELF_SUFF(name)             name##64
#define RTLDRELF_MID(pre,suff)          pre##64##suff

#define BS3_DECL(type)                  type
#define BS3_DECL_CALLBACK(type)         type
#define TMPL_NM(name)                   name##_mmm
#define TMPL_FAR_NM(name)               name##_mmm_far
#define BS3_CMN_NM(name)                name
#define BS3_CMN_FAR_NM(name)            name
#define BS3_CMN_FN_NM(name)             name
#define BS3_DATA_NM(name)               name
#define BS3_FAR
#define BS3_FAR_CODE
#define BS3_FAR_DATA
#define BS3_NEAR
#define BS3_NEAR_CODE
#define BS3_CMN_PROTO_STUB(a_RetType, a_Name, a_Params) a_RetType a_Name a_Params
#define BS3_CMN_PROTO_NOSB(a_RetType, a_Name, a_Params) a_RetType a_Name a_Params
#define BS3_CMN_DEF(       a_RetType, a_Name, a_Params) a_RetType a_Name a_Params
#define BS3_MODE_PROTO_STUB(a_RetType, a_Name, a_Params) \
    a_RetType a_Name##_mmm           a_Params; \
    a_RetType a_Name##_mmm_far       a_Params; \
    a_RetType a_Name##_rm            a_Params; \
    a_RetType a_Name##_pe16          a_Params; \
    a_RetType a_Name##_pe16_32       a_Params; \
    a_RetType a_Name##_pe16_v86      a_Params; \
    a_RetType a_Name##_pe32          a_Params; \
    a_RetType a_Name##_pe32_16       a_Params; \
    a_RetType a_Name##_pev86         a_Params; \
    a_RetType a_Name##_pp16          a_Params; \
    a_RetType a_Name##_pp16_32       a_Params; \
    a_RetType a_Name##_pp16_v86      a_Params; \
    a_RetType a_Name##_pp32          a_Params; \
    a_RetType a_Name##_pp32_16       a_Params; \
    a_RetType a_Name##_ppv86         a_Params; \
    a_RetType a_Name##_pae16         a_Params; \
    a_RetType a_Name##_pae16_32      a_Params; \
    a_RetType a_Name##_pae16_v86     a_Params; \
    a_RetType a_Name##_pae32         a_Params; \
    a_RetType a_Name##_pae32_16      a_Params; \
    a_RetType a_Name##_paev86        a_Params; \
    a_RetType a_Name##_lm16          a_Params; \
    a_RetType a_Name##_lm32          a_Params; \
    a_RetType a_Name##_lm64          a_Params; \
    a_RetType a_Name##_rm_far        a_Params; \
    a_RetType a_Name##_pe16_far      a_Params; \
    a_RetType a_Name##_pe16_v86_far  a_Params; \
    a_RetType a_Name##_pe32_16_far   a_Params; \
    a_RetType a_Name##_pev86_far     a_Params; \
    a_RetType a_Name##_pp16_far      a_Params; \
    a_RetType a_Name##_pp16_v86_far  a_Params; \
    a_RetType a_Name##_pp32_16_far   a_Params; \
    a_RetType a_Name##_ppv86_far     a_Params; \
    a_RetType a_Name##_pae16_far     a_Params; \
    a_RetType a_Name##_pae16_v86_far a_Params; \
    a_RetType a_Name##_pae32_16_far  a_Params; \
    a_RetType a_Name##_paev86_far    a_Params; \
    a_RetType a_Name##_lm16_far      a_Params
#define BS3_MODE_PROTO_NOSB(a_RetType, a_Name, a_Params) \
    a_RetType a_Name##_mmm           a_Params; \
    a_RetType a_Name##_mmm_far       a_Params; \
    a_RetType a_Name##_rm            a_Params; \
    a_RetType a_Name##_pe16          a_Params; \
    a_RetType a_Name##_pe16_32       a_Params; \
    a_RetType a_Name##_pe16_v86      a_Params; \
    a_RetType a_Name##_pe32          a_Params; \
    a_RetType a_Name##_pe32_16       a_Params; \
    a_RetType a_Name##_pev86         a_Params; \
    a_RetType a_Name##_pp16          a_Params; \
    a_RetType a_Name##_pp16_32       a_Params; \
    a_RetType a_Name##_pp16_v86      a_Params; \
    a_RetType a_Name##_pp32          a_Params; \
    a_RetType a_Name##_pp32_16       a_Params; \
    a_RetType a_Name##_ppv86         a_Params; \
    a_RetType a_Name##_pae16         a_Params; \
    a_RetType a_Name##_pae16_32      a_Params; \
    a_RetType a_Name##_pae16_v86     a_Params; \
    a_RetType a_Name##_pae32         a_Params; \
    a_RetType a_Name##_pae32_16      a_Params; \
    a_RetType a_Name##_paev86        a_Params; \
    a_RetType a_Name##_lm16          a_Params; \
    a_RetType a_Name##_lm32          a_Params; \
    a_RetType a_Name##_lm64          a_Params; \
    a_RetType a_Name##_rm_far        a_Params; \
    a_RetType a_Name##_pe16_far      a_Params; \
    a_RetType a_Name##_pe16_v86_far  a_Params; \
    a_RetType a_Name##_pe32_16_far   a_Params; \
    a_RetType a_Name##_pev86_far     a_Params; \
    a_RetType a_Name##_pp16_far      a_Params; \
    a_RetType a_Name##_pp16_v86_far  a_Params; \
    a_RetType a_Name##_pp32_16_far   a_Params; \
    a_RetType a_Name##_ppv86_far     a_Params; \
    a_RetType a_Name##_pae16_far     a_Params; \
    a_RetType a_Name##_pae16_v86_far a_Params; \
    a_RetType a_Name##_pae32_16_far  a_Params; \
    a_RetType a_Name##_paev86_far    a_Params; \
    a_RetType a_Name##_lm16_far      a_Params
#define BS3_MODE_EXPAND_EXTERN_DATA16(a_VarType, a_VarName, a_Suffix) \
    extern a_VarType a_VarName##_rm        a_Suffix; \
    extern a_VarType a_VarName##_pe16      a_Suffix; \
    extern a_VarType a_VarName##_pe16_32   a_Suffix; \
    extern a_VarType a_VarName##_pe16_v86  a_Suffix; \
    extern a_VarType a_VarName##_pe32      a_Suffix; \
    extern a_VarType a_VarName##_pe32_16   a_Suffix; \
    extern a_VarType a_VarName##_pev86     a_Suffix; \
    extern a_VarType a_VarName##_pp16      a_Suffix; \
    extern a_VarType a_VarName##_pp16_32   a_Suffix; \
    extern a_VarType a_VarName##_pp16_v86  a_Suffix; \
    extern a_VarType a_VarName##_pp32      a_Suffix; \
    extern a_VarType a_VarName##_pp32_16   a_Suffix; \
    extern a_VarType a_VarName##_ppv86     a_Suffix; \
    extern a_VarType a_VarName##_pae16     a_Suffix; \
    extern a_VarType a_VarName##_pae16_32  a_Suffix; \
    extern a_VarType a_VarName##_pae16_v86 a_Suffix; \
    extern a_VarType a_VarName##_pae32     a_Suffix; \
    extern a_VarType a_VarName##_pae32_16  a_Suffix; \
    extern a_VarType a_VarName##_paev86    a_Suffix; \
    extern a_VarType a_VarName##_lm16      a_Suffix; \
    extern a_VarType a_VarName##_lm32      a_Suffix; \
    extern a_VarType a_VarName##_lm64      a_Suffix

EOF

    MY_HDR_FILES=`  echo ${MY_ROOT_DIR}/include/VBox/*.h ${MY_ROOT_DIR}/include/VBox/vmm/*.h \
                  | ${MY_SED} -e 's,${MY_ROOT_DIR}/include/VBox/err.h,,' `
    MY_HDR_FILES="${MY_HDR_FILES} ${MY_ROOT_DIR}/include/iprt/cdefs.h"
    ${MY_SED} \
        -e '/__cdecl/d' \
        -e '/^ *# *define.*DECL/!d' \
        -e '/DECLS/d' \
        -e '/DECLINED/d' \
        -e '/DECLARE_CLS_/d' \
        -e '/_SRC_POS_DECL/d' \
        -e '/declspec/d' \
        -e '/__attribute__/d' \
        -e 's/#  */#/g' \
        -e 's/   */ /g' \
        -e '/ DECLEXPORT_CLASS/d' \
        -e 's/ *VBOXCALL//' \
        -e 's/ *RTCALL//' \
        -e '/define  *DECLASM(/d' \
        -e '/define  *DECL..CALLBACKMEMBER([^)]*) *RT/d' \
        -e '/define  *DECLINLINE(/d' \
        -e '/define  *DECL_INLINE_THROW(/d' \
        -e '/define  *DECL_FORCE_INLINE(/d' \
        -e '/define  *DECL_FORCE_INLINE_THROW(/d' \
        -e '/define  *DECLCALLBACK(/d' \
        -e '/define  *DECLCALLBACKMEMBER(/d' \
        -e '/define  *DECLCALLBACKMEMBER_EX(/d' \
        -e '/define  *DECLR[03CG]CALLBACKMEMBER(/d' \
        -e '/define  *DECLCALLBACKPTR(/d' \
        -e '/define  *DECLCALLBACKPTR_EX(/d' \
        -e '/define  *DECLCALLBACKTYPE(/d' \
        -e '/define  *DECLCALLBACKTYPE_EX(/d' \
        -e '/define  *DECLEXPORT(/d' \
        -e '/define  *DECL_EXPORT_THROW(/d' \
        -e '/define  *DECLHIDDEN(/d' \
        -e '/define  *DECL_HIDDEN_CALLBACK(/d' \
        -e '/define  *DECL_HIDDEN_NOTHROW(/d' \
        -e '/define  *DECL_HIDDEN_THROW(/d' \
        -e '/define  *DECLIMPORT(/d' \
        -e '/define  *DECL_IMPORT_NOTHROW(/d' \
        -e '/ DECL_INVALID(/d' \
        \
        -e 's/(\([a-zA-Z_]*\)) DECL_NOTHROW(\1)/(\1) \1/' \
        -e 's/(\([a-zA-Z_]*\)) DECLHIDDEN(DECL_NOTHROW(\1))/(\1) \1/' \
        -e 's/(\([a-zA-Z_]*\)) DECLHIDDEN(\1)/(\1) \1/' \
        -e 's/(\([a-zA-Z_]*\)) DECLEXPORT(\([a-zA-Z_]*\))/(\1) \1/' \
        -e 's/(\([a-zA-Z_]*\)) DECLIMPORT(\([a-zA-Z_]*\))/(\1) \1/' \
        -e 's/(\([a-zA-Z_]*\)) DECL_HIDDEN_NOTHROW(\([a-zA-Z_]*\))/(\1) \1/' \
        -e 's/(\([a-zA-Z_]*\)) DECL_EXPORT_NOTHROW(\([a-zA-Z_]*\))/(\1) \1/' \
        -e 's/(\([a-zA-Z_]*\)) DECL_IMPORT_NOTHROW(\([a-zA-Z_]*\))/(\1) \1/' \
        \
        --append "${MY_FILE}" \
        ${MY_HDR_FILES}

    ${MY_CAT} "${MY_FILE}" \
        | ${MY_SED} -e 's/_/\x1F/g' -e 's/(/\x1E/g' -e 's/[[:space:]][[:space:]]*/\x1C/g' \
        | ${MY_SED} -e 's/\x1F/_/g' -e 's/\x1E/(/g' -e 's/\x1C/ /g' \
        | ${MY_SORT} \
        | ${MY_SED} -e '/#define/s/$/ \/\/ vbox/' --output "${MY_FILE}.2"

    # Eliminate duplicates.
    > "${MY_FILE}.3"
    exec < "${MY_FILE}.2"
    MY_PREV_DEFINE=""
    while read MY_LINE;
    do
        MY_DEFINE=`echo "${MY_LINE}" | ${MY_SED} -e 's/^#define \([^ ()]*\).*$/\1/' `
        if test "${MY_DEFINE}" != "${MY_PREV_DEFINE}"; then
            MY_PREV_DEFINE=${MY_DEFINE}
            echo "${MY_LINE}" >> "${MY_FILE}.3"
        fi
    done

    # Append non-vbox bits from the current user config.
    if test -n "${MY_USERCPP_H_FULL}"  -a  -f "${MY_USERCPP_H_FULL}"; then
        ${MY_SED} -e '/ \/\/ vbox$/d' -e '/^[[:space:]]*$/d' --append "${MY_FILE}.3" "${MY_USERCPP_H_FULL}"
    fi

    # Finalize the file (sort + blank lines).
    ${MY_CAT} "${MY_FILE}.3" \
        | ${MY_SED} -e 's/$/\n/' --output "${MY_FILE}"
    ${MY_RM} -f "${MY_FILE}.2" "${MY_FILE}.3"

    # Install it.
    if test -n "${MY_USERCPP_H_FULL}"  -a  -d "${MY_SLICKDIR}"; then
        if test -f "${MY_USERCPP_H_FULL}"; then
            ${MY_MV} -vf "${MY_USERCPP_H_FULL}" "${MY_USERCPP_H_FULL}.bak"
            ${MY_CP} "${MY_FILE}" "${MY_USERCPP_H_FULL}"
            echo "Updated the SlickEdit preprocessor file. (Previous version renamed to .bak.)"
        else
            ${MY_CP} "${MY_FILE}" "${MY_USERCPP_H_FULL}"
            echo "Created the SlickEdit preprocessor file."
        fi
    fi
}

###### end of functions ####


#
# Parse arguments.
#
while test $# -ge 1;
do
    ARG=$1
    shift
    case "$ARG" in

        --rootdir)
            if test $# -eq 0; then
                echo "error: missing --rootdir argument." 1>&2
                exit 1;
            fi
            MY_ROOT_DIR="$1"
            shift
            ;;

        --outdir)
            if test $# -eq 0; then
                echo "error: missing --outdir argument." 1>&2
                exit 1;
            fi
            MY_OUT_DIR="$1"
            shift
            ;;

        --project-base)
            if test $# -eq 0; then
                echo "error: missing --project-base argument." 1>&2
                exit 1;
            fi
            MY_PRJ_PRF="$1"
            shift
            ;;

        --workspace)
            if test $# -eq 0; then
                echo "error: missing --workspace argument." 1>&2
                exit 1;
            fi
            MY_WS_NAME="$1"
            shift
            ;;

        --windows-host)
            MY_WINDOWS_HOST=1
            ;;

        --minimal)
            MY_OPT_MINIMAL=1
            ;;

        --slickedit-config)
            MY_SLICK_CONFIG="$1"
            shift
            ;;

        --only-usercpp)
            MY_OPT_ONLY_USERCPP=1
            ;;

        # usage
        --h*|-h*|-?|--?)
            echo "usage: $0 [--rootdir <rootdir>] [--outdir <outdir>] [--project-base <prefix>] [--workspace <name>] [--minimal] [--slickedit-config <DIR>]"
            echo ""
            echo "If --outdir is specified, you must specify a --rootdir relative to it as well."
            exit 1;
            ;;

        # default
        *)
            echo "error: Invalid parameter '$ARG'" 1>&2
            exit 1;

    esac
done


#
# From now on everything *MUST* succeed.
#
set -e

#
# Make sure the output directory exists, is valid and clean.
#
if test -z "${MY_OPT_ONLY_USERCPP}"; then
    ${MY_RM} -f \
        "${MY_OUT_DIR}/${MY_PRJ_PRF}"*.vpj \
        "${MY_OUT_DIR}/${MY_WS_NAME}" \
        "${MY_OUT_DIR}/`echo ${MY_WS_NAME} | ${MY_SED} -e 's/\.vpw$/.vtg/'`"
fi
${MY_MKDIR} -p "${MY_OUT_DIR}"
cd "${MY_OUT_DIR}"


#
# Determine the invocation to conjure up kmk.
#
my_abs_dir "tools"
if test -n "${MY_WINDOWS_HOST}"; then
    MY_KMK_INVOCATION="cscript.exe /Nologo ${MY_ABS_DIR}/envSub.vbs --quiet -- kmk.exe"
else
    MY_KMK_INVOCATION="/usr/bin/env LANG=C ${MY_ABS_DIR}/env.sh --quiet --no-wine kmk"
fi


#
# Generate the projects (common code) and workspace.
#
if test -z "${MY_OPT_ONLY_USERCPP}"; then
    my_generate_all_projects # in common-gen-workspace-projects.inc.sh
    my_generate_workspace
fi

#
# Update the history file if present.
#
if test -z "${MY_OPT_ONLY_USERCPP}"; then
    MY_FILE="${MY_WS_NAME}histu"
    if test -f "${MY_FILE}"; then
        echo "Updating ${MY_FILE}..."
        ${MY_MV} -f "${MY_FILE}" "${MY_FILE}.old"
        ${MY_SED} -n \
            -e '/PROJECT_CACHE/d' \
            -e '/\[TreeExpansion2\]/d' \
            -e '/^\[/p' \
            -e '/: /p' \
            -e '/^CurrentProject/p' \
            "${MY_FILE}.old" > "${MY_FILE}"
    fi
fi

#
# Generate and update the usercpp.h/unxcpp.h file.
#
my_generate_usercpp_h


echo "done"
