#!/usr/bin/env kmk_ash
# $Id: gen-vscode-workspace.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# Script for generating a Visual Studio Code (vscode) workspace.
#
# This is derived from gen-slickedit-workspace.sh, so fixes may apply to both.
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
# Include code we share with gen-slickedit-workspace.sh
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
MY_VSCODE_DIR=".vscode"
MY_VSCODE_FILE_DOT_EXT=".json"
MY_OUT_DIR=${MY_VSCODE_DIR}
MY_PRJ_PRF="VBox-"
MY_WS_NAME="virtualbox.code-workspace"
MY_DBG=""
MY_WINDOWS_HOST=""
MY_OPT_MINIMAL=""
MY_OPT_USE_WILDCARDS="yes"


##
# Function generating an (intermediate) project task.
#
# @param    $1      The project file name.
# @param    $2      Build config name.
# @param    $3      Task group to assign task to.
# @param    $4      kBuild extra args.
# @param    $5      kBuild working directory to set.
#                   If empty, the file's directory will be used.
# @param    $6      kBuild per-task options.
#                   Leave empty if not being used.
my_generate_project_task()
{
    MY_FILE="${1}";
    MY_CFG_NAME="${2}";
    MY_TASK_GROUP="${3}";
    MY_KMK_EXTRAS="${4}";
    MY_KMK_CWD="${5}";
    MY_KMK_TASK_ARGS="${6}";
    shift; shift; shift; shift; shift; shift;

    if [ -z "$MY_KMK_CWD" ]; then
        MY_KMK_CWD='${fileDirname}'
    fi

    MY_TASK_LABEL="${MY_TASK_GROUP}: ${MY_CFG_NAME}"

    echo '        {'                                                            >> "${MY_FILE}"
    echo '            "type": "shell",'                                         >> "${MY_FILE}"
    echo '            "label": "'${MY_TASK_LABEL}'",'                           >> "${MY_FILE}"
    echo '            "command": "'${MY_KMK_CMD}'",'                            >> "${MY_FILE}"
    echo '            "args": ['                                                >> "${MY_FILE}"
    echo '                ' ${MY_KMK_ARGS} ','                                  >> "${MY_FILE}"
    if [ -n "${MY_KMK_EXTRAS}" ]; then
        echo '                '${MY_KMK_EXTRAS}' ,'                             >> "${MY_FILE}"
    fi
    echo '                "-C",'                                                >> "${MY_FILE}"
    echo '                "'${MY_KMK_CWD}'"'                                    >> "${MY_FILE}"
    if [ -n "${MY_KMK_TASK_ARGS}" ]; then
    echo '              ', ${MY_KMK_TASK_ARGS}                                  >> "${MY_FILE}"
    fi
    echo '            ],'                                                       >> "${MY_FILE}"
    echo '            "options": {'                                             >> "${MY_FILE}"
    echo '                "cwd": "'${MY_KMK_CWD}'"'                             >> "${MY_FILE}"
    echo '            },'                                                       >> "${MY_FILE}"
    echo '            "problemMatcher": ['                                      >> "${MY_FILE}"
    echo '                "$gcc"'                                               >> "${MY_FILE}"
    echo '            ],'                                                       >> "${MY_FILE}"
    echo '            "presentation": {'                                        >> "${MY_FILE}"
    echo '                "reveal": "always",'                                  >> "${MY_FILE}"
    echo '                "clear": true,'                                       >> "${MY_FILE}"
    echo '                "panel": "dedicated"'                                 >> "${MY_FILE}"
    echo '            },'                                                       >> "${MY_FILE}"
    echo '            "detail": "compiler: /bin/clang++-9"'                     >> "${MY_FILE}"
    echo '        },'                                                           >> "${MY_FILE}"
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

    ## @todo Process includes.
    while test $# -ge 1  -a  "${1}" != "--end-includes";
    do
        for f in $1;
        do
            my_abs_dir ${f}
            #echo "Includes: ${MY_ABS_DIR}/"
        done
        shift
    done
    shift

    #
    # Build tasks.
    #
    MY_TASK_CWD='${fileDirname}'
    MY_TASK_ARGS='"-o", "${file}"'
    my_generate_project_task "${MY_FILE}" "${MY_CFG_NAME}" "Compile1" "${MY_KMK_EXTRAS1}" \
                             "${MY_TASK_CWD}" "${MY_TASK_ARGS}"
    if test -n "${MY_KMK_EXTRAS2}"; then
        my_generate_project_task "${MY_FILE}" "${MY_CFG_NAME}" "Compile2" "${MY_KMK_EXTRAS2}" \
                                 "${MY_TASK_CWD}" "${MY_TASK_ARGS}"
    fi
    if test -n "${MY_KMK_EXTRAS3}"; then
        my_generate_project_task "${MY_FILE}" "${MY_CFG_NAME}" "Compile3" "${MY_KMK_EXTRAS3}" \
                                 "${MY_TASK_CWD}" "${MY_TASK_ARGS}"
    fi
    if test -n "${MY_KMK_EXTRAS4}"; then
        my_generate_project_task "${MY_FILE}" "${MY_CFG_NAME}" "Compile4" "${MY_KMK_EXTRAS4}" \
                                 "${MY_TASK_CWD}" "${MY_TASK_ARGS}"
    fi
    MY_TASK_CWD='${workspaceFolder}'
    MY_TASK_ARGS=""
    my_generate_project_task "${MY_FILE}" "${MY_CFG_NAME}" "Build1" "${MY_KMK_EXTRAS1}" \
                             "${MY_TASK_CWD}" "${MY_TASK_ARGS}"
    if test -n "${MY_KMK_EXTRAS2}"; then
        my_generate_project_task "${MY_FILE}" "${MY_CFG_NAME}" "Build2" "${MY_KMK_EXTRAS2}" \
                                 "${MY_TASK_CWD}" "${MY_TASK_ARGS}"
    fi
    if test -n "${MY_KMK_EXTRAS3}"; then
        my_generate_project_task "${MY_FILE}" "${MY_CFG_NAME}" "Build3" "${MY_KMK_EXTRAS3}" \
                                 "${MY_TASK_CWD}" "${MY_TASK_ARGS}"
    fi
    if test -n "${MY_KMK_EXTRAS4}"; then
        my_generate_project_task "${MY_FILE}" "${MY_CFG_NAME}" "Build4" "${MY_KMK_EXTRAS4}" \
                                 "${MY_TASK_CWD}" "${MY_TASK_ARGS}"
    fi

    MY_TASK_CWD='${workspaceFolder}'
    MY_TASK_ARGS='"rebuild"'
    my_generate_project_task "${MY_FILE}" "${MY_CFG_NAME}" "Rebuild1" "${MY_KMK_EXTRAS1}" \
                             "${MY_TASK_CWD}" "${MY_TASK_ARGS}"
    if test -n "${MY_KMK_EXTRAS2}"; then
        my_generate_project_task "${MY_FILE}" "${MY_CFG_NAME}" "Rebuild2" "${MY_KMK_EXTRAS2}" \
                                 "${MY_TASK_CWD}" "${MY_TASK_ARGS}"
    fi
    if test -n "${MY_KMK_EXTRAS3}"; then
        my_generate_project_task "${MY_FILE}" "${MY_CFG_NAME}" "Rebuild3" "${MY_KMK_EXTRAS3}" \
                                 "${MY_TASK_CWD}" "${MY_TASK_ARGS}"
    fi
    if test -n "${MY_KMK_EXTRAS4}"; then
        my_generate_project_task "${MY_FILE}" "${MY_CFG_NAME}" "Rebuild4" "${MY_KMK_EXTRAS4}" \
                                 "${MY_TASK_CWD}" "${MY_TASK_ARGS}"
    fi

    #
    # Generate compound tasks that invokes all needed sub tasks.
    #
    # Note: We include "VBox" in the label so that the command is easier to find
    #       in the command pallette.
    #
    echo '              {'                                                      >> "${MY_FILE}"
    echo '                  "label": "VBox Compile: '${MY_CFG_NAME}'",'         >> "${MY_FILE}"
    echo '                  "dependsOrder": "sequence",'                        >> "${MY_FILE}"
    echo '                  "dependsOn": [ "Compile1: '${MY_CFG_NAME}'"'        >> "${MY_FILE}"
    if test -n "${MY_KMK_EXTRAS2}"; then
        echo '                        , "Compile2: '${MY_CFG_NAME}'"'           >> "${MY_FILE}"
    fi
    if test -n "${MY_KMK_EXTRAS3}"; then
       echo '                         , "Compile3: '${MY_CFG_NAME}'"'           >> "${MY_FILE}"
    fi
    if test -n "${MY_KMK_EXTRAS4}"; then
       echo '                         , "Compile4: '${MY_CFG_NAME}'"'           >> "${MY_FILE}"
    fi
    echo '                       ]'                                             >> "${MY_FILE}"
    echo '              },'                                                     >> "${MY_FILE}"
    echo '              {'                                                      >> "${MY_FILE}"
    echo '                      "label": "VBox Build: '${MY_CFG_NAME}'",'       >> "${MY_FILE}"
    echo '                      "dependsOrder": "sequence",'                    >> "${MY_FILE}"
    echo '                      "dependsOn": [ "Build1: '${MY_CFG_NAME}'"'      >> "${MY_FILE}"
    if test -n "${MY_KMK_EXTRAS2}"; then
        echo '                         , "Build2: '${MY_CFG_NAME}'"'            >> "${MY_FILE}"
    fi
    if test -n "${MY_KMK_EXTRAS3}"; then
        echo '                         , "Build3: '${MY_CFG_NAME}'"'            >> "${MY_FILE}"
    fi
    if test -n "${MY_KMK_EXTRAS4}"; then
        echo '                         , "Build4: '${MY_CFG_NAME}'"'            >> "${MY_FILE}"
    fi
    echo '                      ],'                                             >> "${MY_FILE}"
    echo '                      "group": {'                                     >> "${MY_FILE}"
    echo '                          "kind": "build",'                           >> "${MY_FILE}"
    echo '                          "isDefault": true'                          >> "${MY_FILE}"
    echo '                      }'                                              >> "${MY_FILE}"
    echo '              },'                                                     >> "${MY_FILE}"
    echo '              {'                                                      >> "${MY_FILE}"
    echo '                      "label": "VBox Rebuild: '${MY_CFG_NAME}'",'     >> "${MY_FILE}"
    echo '                      "dependsOrder": "sequence",'                    >> "${MY_FILE}"
    echo '                      "dependsOn": [ "Rebuild1: '${MY_CFG_NAME}'"'    >> "${MY_FILE}"
    if test -n "${MY_KMK_EXTRAS2}"; then
        echo '                         , "Rebuild2: '${MY_CFG_NAME}'"'          >> "${MY_FILE}"
    fi
    if test -n "${MY_KMK_EXTRAS3}"; then
        echo '                         , "Rebuild3: '${MY_CFG_NAME}'"'          >> "${MY_FILE}"
    fi
    if test -n "${MY_KMK_EXTRAS4}"; then
        echo '                         , "Rebuild4: '${MY_CFG_NAME}'"'          >> "${MY_FILE}"
    fi
    echo '                       ]'                                             >> "${MY_FILE}"
    echo '              },'                                                     >> "${MY_FILE}"
}


##
# Function generating a project.
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
    MY_PRJ_NAME=${1}
    MY_WRK_DIR="${MY_FILE_ROOT_DIR}/${2}"
    MY_FILE_PATH="${MY_WRK_DIR}/.vscode"
    shift
    shift
    shift

    # Make sure that the .vscode project dir exists. But do *NOT* create the
    # parent dir, it must already exist. Duh!
    test -d "${MY_FILE_PATH}" || ${MY_MKDIR} "${MY_FILE_PATH}"

    MY_FILE="${MY_FILE_PATH}/c_cpp_properties${MY_VSCODE_FILE_DOT_EXT}";
    echo "Generating ${MY_FILE}..."

    # Add it to the project list for workspace construction later on.
    MY_PROJECT_FILES="${MY_PROJECT_FILES} ${MY_PRJ_NAME}:${MY_WRK_DIR}"

    #
    # Generate the C/C++ bits.
    ## @todo Might needs tweaking a bit more as stuff evolves.
    #
    echo '{'                                                                    >  "${MY_FILE}"
    echo '    "configurations": ['                                              >> "${MY_FILE}"
    echo '        {'                                                            >> "${MY_FILE}"
    echo '            "name": "Linux",'                                         >> "${MY_FILE}"
    echo '            "includePath": ['                                         >> "${MY_FILE}"
    echo '                "${workspaceFolder}/**"'                              >> "${MY_FILE}"
    echo '            ],'                                                       >> "${MY_FILE}"
    echo '            "defines": [],'                                           >> "${MY_FILE}"
    echo '            "cStandard": "c17",'                                      >> "${MY_FILE}"
    echo '            "cppStandard": "c++14",'                                  >> "${MY_FILE}"
    echo '            "intelliSenseMode": "linux-gcc-x64",'                     >> "${MY_FILE}"
    echo '            "compilerPath": "/usr/bin/gcc"'                           >> "${MY_FILE}"
    echo '        }'                                                            >> "${MY_FILE}"
    echo '    ],'                                                               >> "${MY_FILE}"
    echo '    "version": 4'                                                     >> "${MY_FILE}"
    echo '}'                                                                    >> "${MY_FILE}"

    MY_FILE="${MY_FILE_PATH}/tasks${MY_VSCODE_FILE_DOT_EXT}";
    echo "Generating ${MY_FILE}..."

    #
    # Tasks header.
    #
    echo '{'                                                                    >  "${MY_FILE}"
    echo '    "version": "2.0.0",'                                              >> "${MY_FILE}"
    echo '    "tasks": ['                                                       >> "${MY_FILE}"

    my_generate_project_config "${MY_FILE}" "Default" "" "" "" "" $*
    my_generate_project_config "${MY_FILE}" "Debug + hardening" \
        '"KBUILD_TYPE=debug", "VBOX_WITH_HARDENING=1"' \
        "" "" "" $*
    my_generate_project_config "${MY_FILE}" "Release + hardening" \
        '"KBUILD_TYPE=release", "VBOX_WITH_HARDENING=1"' \
        "" "" "" $*
    my_generate_project_config "${MY_FILE}" "Debug+Release + hardening" \
        '"KBUILD_TYPE=debug", "VBOX_WITH_HARDENING=1"' \
        '"KBUILD_TYPE=release", "VBOX_WITH_HARDENING=1"' \
        "" "" $*
    my_generate_project_config "${MY_FILE}" "Debug w/o hardening" \
        '"KBUILD_TYPE=debug", "VBOX_WITHOUT_HARDENING=1"' \
        "" "" "" $*
    my_generate_project_config "${MY_FILE}" "Release w/o hardening" \
        '"KBUILD_TYPE=release", "VBOX_WITHOUT_HARDENING=1"' \
        "" "" "" $*
    my_generate_project_config "${MY_FILE}" "Debug+Release w/o hardening" \
        '"KBUILD_TYPE=debug", "VBOX_WITHOUT_HARDENING=1"' \
        '"KBUILD_TYPE=release", "VBOX_WITHOUT_HARDENING=1"' \
        "" "" $*
    my_generate_project_config "${MY_FILE}" "Debug+Release with and without hardening" \
        '"KBUILD_TYPE=debug", "VBOX_WITH_HARDENING=1"' \
        '"KBUILD_TYPE=release", "VBOX_WITH_HARDENING=1"' \
        '"KBUILD_TYPE=debug", "VBOX_WITHOUT_HARDENING=1"' \
        '"KBUILD_TYPE=release", "VBOX_WITHOUT_HARDENING=1"' \
        $*

    #
    # Tasks footer.
    #
    echo '    ]'                                                                >> "${MY_FILE}"
    echo '}'                                                                    >> "${MY_FILE}"

    while test $# -ge 1  -a  "${1}" != "--end-includes";
    do
        shift;
    done;
    shift;

    return 0
}


##
# Generate the workspace
#
my_generate_workspace()
{
    MY_FILE="${MY_FILE_ROOT_DIR}/${MY_WS_NAME}"
    echo "Generating ${MY_FILE}..."
    echo '{'                                                                    >  "${MY_FILE}"
    echo '    "folders": ['                                                     >> "${MY_FILE}"
    for i in ${MY_PROJECT_FILES};
    do
        MY_PRJ_NAME=$(echo $i | ${MY_SED} -e 's/:.*$//')
        MY_PRJ_PATH=$(echo $i | ${MY_SED} -e 's/^.*://')
        echo '        {'                                                        >> "${MY_FILE}"
        echo '            "name": "'"${MY_PRJ_NAME}"'",'                        >> "${MY_FILE}"
        echo '            "path": "'"${MY_PRJ_PATH}"'",'                        >> "${MY_FILE}"
        echo '        },'                                                       >> "${MY_FILE}"
    done
    echo '    ],'                                                               >> "${MY_FILE}"
    echo '    "settings": {'                                                    >> "${MY_FILE}"
    echo '        "breadcrumbs.enabled": true,'                                 >> "${MY_FILE}"
    echo '        "diffEditor.renderSideBySide": false,'                        >> "${MY_FILE}"
    echo '        "editor.renderWhitespace": "boundary",'                       >> "${MY_FILE}"
    echo '        "editor.cursorStyle": "block-outline",'                       >> "${MY_FILE}"
    echo '        "editor.minimap.showSlider": "always",'                       >> "${MY_FILE}"
    echo '        "editor.wordWrapColumn": 130,'                                >> "${MY_FILE}"
    echo '        "editor.rulers": [ 80, 130],'                                 >> "${MY_FILE}"
    echo '        "files.associations": {'                                      >> "${MY_FILE}"
    echo '            "*.kmk": "makefile",'                                     >> "${MY_FILE}"
    echo '            "*.wxi": "xml",'                                          >> "${MY_FILE}"
    echo '            "*.wxs": "xml"'                                           >> "${MY_FILE}"
    echo '        },'                                                           >> "${MY_FILE}"
    echo '        "files.trimFinalNewlines": true,'                             >> "${MY_FILE}"
    echo '        "files.trimTrailingWhitespace": true,'                        >> "${MY_FILE}"
    echo '        "multiclip.bufferSize": 999,'                                 >> "${MY_FILE}"
    echo '        "telemetry.telemetryLevel": "off",'                           >> "${MY_FILE}"
    echo '        "python.linting.pylintEnabled": true,'                        >> "${MY_FILE}"
    echo '        "python.linting.enabled": false,'                             >> "${MY_FILE}"
    echo '        "python.linting.pylintUseMinimalCheckers": false,'            >> "${MY_FILE}"
    echo '        "window.restoreWindows": "all",'                              >> "${MY_FILE}"
    echo '        "workbench.editor.highlightModifiedTabs": true,'              >> "${MY_FILE}"
    echo '        "workbench.colorCustomizations": {'                           >> "${MY_FILE}"
    echo '            "editorRuler.foreground": "#660000"'                      >> "${MY_FILE}"
    echo '        },'                                                           >> "${MY_FILE}"
    echo '        "xmlTools.enableXmlTreeViewCursorSync": true,'                >> "${MY_FILE}"
    echo '    },'                                                               >> "${MY_FILE}"
    echo '    "extensions": {'                                                  >> "${MY_FILE}"
    echo '        "recommendations": ['                                         >> "${MY_FILE}"
    echo '            "ms-vscode.cpptools",'                                    >> "${MY_FILE}"
    echo '            "ms-vscode.cpptools-extension-pack",'                     >> "${MY_FILE}"
    echo '            "ms-python.python",'                                      >> "${MY_FILE}"
    echo '            "ms-python.vscode-pylance",'                              >> "${MY_FILE}"
    echo '            "johnstoncode.svn-scm"'                                   >> "${MY_FILE}"
    echo '        ]'                                                            >> "${MY_FILE}"
    echo '    }'                                                                >> "${MY_FILE}"
    echo '}'                                                                    >> "${MY_FILE}"
    return 0
}


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

        # usage
        --h*|-h*|-?|--?)
            echo "usage: $0 [--rootdir <rootdir>] [--outdir <outdir>] [--project-base <prefix>] [--workspace <name>] [--minimal]"
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
## @todo r=bird: The above statement is *extremely* misleading. This script will
#  create .vscode subdirs all over the place,  and the root one doesn't seem to
#  have any special purpose compared to the rest.  Guess this is just fluff
#  inherited from gen-slickedit-workspace.sh.
${MY_RM} -f \
    "${MY_OUT_DIR}"*.json \
    "${MY_OUT_DIR}/${MY_ROOT_DIR}/${MY_WS_NAME}"
${MY_MKDIR} -p "${MY_OUT_DIR}"

# Enter the directory (or ${MY_ROOT_DIR} + my_abs_dir won't work) and save the absolute root path.
cd "${MY_OUT_DIR}"
my_abs_dir "."
MY_FILE_ROOT_DIR=${MY_ABS_DIR} ## @todo r=bird: 'FILE' or 'DIR'? ;-)


#
# Determine the invocation to conjure up kmk.
#
my_abs_dir "tools"
if test -n "${MY_WINDOWS_HOST}"; then
    MY_KMK_CMD="cscript.exe"
    MY_KMK_ARGS='"/Nologo", "'${MY_ABS_DIR}/envSub.vbs'", "--quiet", "--", "kmk.exe"'
else
    MY_KMK_CMD="/usr/bin/env"
    MY_KMK_ARGS='"LANG=C", "'${MY_ABS_DIR}/env.sh'", "--quiet", "--no-wine", "kmk"'
fi


#
# Generate the projects (common code) and workspace.
#
my_generate_all_projects # in common-gen-workspace-projects.inc.sh
my_generate_workspace


echo "done"
