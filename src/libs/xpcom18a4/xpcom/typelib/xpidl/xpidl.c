/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: NPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is 
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or 
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the NPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the NPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

/*
 * Main xpidl program entry point.
 */
#include <iprt/initterm.h>

#include "xpidl.h"

static ModeData modes[] = {
    {"header",  "Generate C++ header",         "h",    xpidl_header_dispatch},
    {"typelib", "Generate XPConnect typelib",  "xpt",  xpidl_typelib_dispatch},
    {0,         0,                             0,      0}
};

static ModeData *
FindMode(char *mode)
{
    int i;
    for (i = 0; modes[i].mode; i++) {
        if (!strcmp(modes[i].mode, mode))
            return &modes[i];
    }
    return NULL;
}

bool enable_debug               = false;
bool enable_warnings            = false;
bool verbose_mode               = false;
bool emit_typelib_annotations   = false;
bool explicit_output_filename   = false;

/* The following globals are explained in xpt_struct.h */
PRUint8  major_version              = XPT_MAJOR_VERSION;
PRUint8  minor_version              = XPT_MINOR_VERSION;

static char xpidl_usage_str[] =
"Usage: %s -m mode [-w] [-v] [-t version number]\n"
"          [-I path] [-o basename | -e filename.ext] filename.idl\n"
"       -a emit annotations to typelib\n"
"       -w turn on warnings (recommended)\n"
"       -v verbose mode (NYI)\n"
"       -t create a typelib of a specific version number\n"
"       -I add entry to start of include path for ``#include \"nsIThing.idl\"''\n"
"       -o use basename (e.g. ``/tmp/nsIThing'') for output\n"
"       -e use explicit output filename\n"
"       -m specify output mode:\n";

static void
xpidl_usage(int argc, char *argv[])
{
    RT_NOREF(argc);
    fprintf(stderr, xpidl_usage_str, argv[0]);
    for (int i = 0; modes[i].mode; i++) {
        fprintf(stderr, "          %-12s  %-30s (.%s)\n", modes[i].mode,
                modes[i].modeInfo, modes[i].suffix);
    }
}

int main(int argc, char *argv[])
{
    RTR3InitExeNoArguments(0);

    int i;
    RTLISTANCHOR LstIncludePaths;
    char *file_basename = NULL;
    ModeData *mode = NULL;

    RTListInit(&LstIncludePaths);

    PXPIDLINCLUDEDIR pInc = (PXPIDLINCLUDEDIR)xpidl_malloc(sizeof(*pInc));
    pInc->pszPath = ".";
    RTListAppend(&LstIncludePaths, &pInc->NdIncludes);

    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-')
            break;
        switch (argv[i][1]) {
          case '-':
            argc++;             /* pretend we didn't see this */
            /* fall through */
          case 0:               /* - is a legal input filename (stdin)  */
            goto done_options;
          case 'a':
            emit_typelib_annotations = true;
            break;
          case 'w':
            enable_warnings = true;
            break;
          case 'v':
            verbose_mode = true;
            break;
          case 't':
          {
            /* Parse for "-t version number" and store it into global boolean
             * and string variables.
             */

            /* 
             * If -t is the last argument on the command line, we have a problem
             */

            if (i + 1 == argc) {
                fprintf(stderr, "ERROR: missing version number after -t\n");
                xpidl_usage(argc, argv);
                return 1;
            }

            /*
             * Assume that the argument after "-t" is the version number string
             * and search for it in our internal list of acceptable version
             * numbers.
             */
            switch (XPT_ParseVersionString(argv[++i], &major_version, 
                                           &minor_version)) {
              case XPT_VERSION_CURRENT:
                break; 
              case XPT_VERSION_OLD: 
              case XPT_VERSION_UNSUPPORTED: 
                fprintf(stderr, "ERROR: version \"%s\" not supported.\n", 
                        argv[i]);
                xpidl_usage(argc, argv);
                return 1;          
              case XPT_VERSION_UNKNOWN: 
              default:
                fprintf(stderr, "ERROR: version \"%s\" not recognised.\n", 
                        argv[i]);
                xpidl_usage(argc, argv);
                return 1;          
            }
            break;
          }
          case 'I':
            if (argv[i][2] == '\0' && i == argc) {
                fputs("ERROR: missing path after -I\n", stderr);
                xpidl_usage(argc, argv);
                return 1;
            }
            pInc = (PXPIDLINCLUDEDIR)xpidl_malloc(sizeof(*pInc));
            if (argv[i][2] == '\0') {
                /* is it the -I foo form? */
                pInc->pszPath = argv[++i];
            } else {
                /* must be the -Ifoo form.  Don't preincrement i. */
                pInc->pszPath = argv[i] + 2;
            }

            RTListAppend(&LstIncludePaths, &pInc->NdIncludes);
            break;
          case 'o':
            if (i == argc) {
                fprintf(stderr, "ERROR: missing basename after -o\n");
                xpidl_usage(argc, argv);
                return 1;
            }
            file_basename = argv[++i];
            explicit_output_filename = false;
            break;
          case 'e':
            if (i == argc) {
                fprintf(stderr, "ERROR: missing basename after -e\n");
                xpidl_usage(argc, argv);
                return 1;
            }
            file_basename = argv[++i];
            explicit_output_filename = true;
            break;
          case 'm':
            if (i + 1 == argc) {
                fprintf(stderr, "ERROR: missing modename after -m\n");
                xpidl_usage(argc, argv);
                return 1;
            }
            if (mode) {
                fprintf(stderr,
                        "ERROR: must specify exactly one mode "
                        "(first \"%s\", now \"%s\")\n", mode->mode,
                        argv[i + 1]);
                xpidl_usage(argc, argv);
                return 1;
            }
            mode = FindMode(argv[++i]);
            if (!mode) {
                fprintf(stderr, "ERROR: unknown mode \"%s\"\n", argv[i]);
                xpidl_usage(argc, argv);
                return 1;
            }
            break;                 
          default:
            fprintf(stderr, "unknown option %s\n", argv[i]);
            xpidl_usage(argc, argv);
            return 1;
        }
    }
 done_options:
    if (!mode) {
        fprintf(stderr, "ERROR: must specify output mode\n");
        xpidl_usage(argc, argv);
        return 1;
    }
    if (argc != i + 1) {
        fprintf(stderr, "ERROR: extra arguments after input file\n");
    }

    /*
     * Don't try to process multiple files, given that we don't handle -o
     * multiply.
     */
    int rc = xpidl_process_idl(argv[i], &LstIncludePaths, file_basename, mode);
    if (RT_SUCCESS(rc))
        return 0;

    /** @todo Free include paths. */

    printf("Failed to process IDL file\n");
    return 1;
}
