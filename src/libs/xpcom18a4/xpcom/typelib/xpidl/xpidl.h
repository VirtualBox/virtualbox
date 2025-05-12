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
 * Intramodule declarations.
 */

#ifndef __xpidl_h
#define __xpidl_h

#include <iprt/errcore.h>
#include <iprt/list.h>
#include <iprt/script.h>

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xpt_struct.h>


/**
 * An include path.
 */
typedef struct XPIDLINCLUDEDIR
{
    /** Node for the list of include paths. */
    RTLISTNODE          NdIncludes;
    /** The zero terminated include path. */
    const char          *pszPath;
} XPIDLINCLUDEDIR;
/** Pointer to an include path. */
typedef XPIDLINCLUDEDIR *PXPIDLINCLUDEDIR;
/** Pointer to a const include path. */
typedef const XPIDLINCLUDEDIR *PCXPIDLINCLUDEDIR;


/**
 * The input stream.
 */
typedef struct XPIDLINPUT
{
    /** Node for the list of inputs. */
    RTLISTNODE          NdInput;
    /** Node for the list of include. */
    RTLISTNODE          NdInclude;
    /** The list of includes this input generated. */
    RTLISTANCHOR        LstIncludes;
    /** The basename for this input. */
    char                *pszBasename;
    /** The filename for this input. */
    char                *pszFilename;
    /** The lexer instance for this input. */
    RTSCRIPTLEX         hIdlLex;
} XPIDLINPUT;
/** Pointer to an input stream. */
typedef XPIDLINPUT *PXPIDLINPUT;
/** Pointer to a const input stream. */
typedef const XPIDLINPUT *PCXPIDLINPUT;


/**
 * IDL node type.
 */
typedef enum XPIDLNDTYPE
{
    kXpidlNdType_Invalid = 0,
    kXpidlNdType_RawBlock,
    kXpidlNdType_Typedef,
    kXpidlNdType_BaseType,
    kXpidlNdType_Identifier,
    kXpidlNdType_Native,
    kXpidlNdType_Interface_Forward_Decl,
    kXpidlNdType_Interface_Def,
    kXpidlNdType_Attribute,
    kXpidlNdType_Method,
    kXpidlNdType_Parameter,
    kXpidlNdType_Const
} XPIDLNDTYPE;


/**
 * IDL base type.
 */
typedef enum XPIDLTYPE
{
    kXpidlType_Invalid = 0,
    kXpidlType_Void,
    kXpidlType_Boolean,
    kXpidlType_Octet,
    kXpidlType_Char,
    kXpidlType_Wide_Char,
    kXpidlType_Short,
    kXpidlType_Long,
    kXpidlType_Long_Long,
    kXpidlType_Unsigned_Short,
    kXpidlType_Unsigned_Long,
    kXpidlType_Unsigned_Long_Long,
    kXpidlType_String,
    kXpidlType_Wide_String,
    kXpidlType_Double,
    kXpidlType_Float,
} XPIDLTYPE;


/**
 * IDL direction.
 */
typedef enum XPIDLDIRECTION
{
    kXpidlDirection_Invalid = 0,
    kXpidlDirection_In,
    kXpidlDirection_InOut,
    kXpidlDirection_Out
} XPIDLDIRECTION;


/**
 * A node attribute.
 */
typedef struct XPIDLATTR
{
    /** The attribute name. */
    const char          *pszName;
    /** The value assigned if any. */
    const char          *pszVal;
} XPIDLATTR;
/** Pointer to an attribute. */
typedef XPIDLATTR *PXPIDLATTR;
/** Pointer to a const attribute. */
typedef const XPIDLATTR *PCXPIDLATTR;


/** Pointer to an IDL node. */
typedef struct XPIDLNODE *PXPIDLNODE;
/** Pointer to a const IDL node. */
typedef const struct XPIDLNODE *PCXPIDLNODE;

/**
 * IDL node.
 */
typedef struct XPIDLNODE
{
    /** Node for the list this node is in. */
    RTLISTNODE          NdLst;
    /** The parent node (if any). */
    PCXPIDLNODE         pParent;
    /** The input stream this node was generated from (via #include's). */
    PCXPIDLINPUT        pInput;
    /** The type this node references (for identifiers and the inheritance for interfaces only). */
    PCXPIDLNODE         pNdTypeRef;
    /** The node type. */
    XPIDLNDTYPE         enmType;
    /** Node type dependent data. */
    union
    {
        struct
        {
            const char *pszRaw;
            size_t     cchRaw;
        } RawBlock;
        struct
        {
            PCXPIDLNODE pNodeTypeSpec;
            const char  *pszName;
        } Typedef;
        XPIDLTYPE       enmBaseType;
        const char      *pszIde;
        struct
        {
            const char  *pszName;
            const char  *pszNative;
        } Native;
        const char      *pszIfFwdName;
        struct
        {
            const char  *pszIfName;
            const char  *pszIfInherit;
            RTLISTANCHOR LstBody;
        } If;
        struct
        {
            bool        fReadonly;
            PCXPIDLNODE pNdTypeSpec;
            const char  *pszName;
        } Attribute;
        struct
        {
            PCXPIDLNODE pNdTypeSpecRet;
            const char  *pszName;
            RTLISTANCHOR LstParams;
        } Method;
        struct
        {
            PCXPIDLNODE pNdTypeSpec;
            const char  *pszName;
            XPIDLDIRECTION enmDir;
        } Param;
        struct
        {
            PCXPIDLNODE pNdTypeSpec;
            const char  *pszName;
            uint64_t    u64Const; /* Only allowing numbers for now. */
        } Const;
    } u;
    /** Number of entries in the attribute array. */
    uint32_t            cAttrs;
    /** Node attributes array - variable in size. */
    XPIDLATTR           aAttrs[1];
} XPIDLNODE;


/**
 * The IDL parsing state.
 */
typedef struct XPIDLPARSE
{
    /** List of input files. */
    RTLISTANCHOR        LstInputs;
    /** The list of XPIDL nodes from the root. */
    RTLISTANCHOR        LstNodes;
    /** Extended error info. */
    RTERRINFOSTATIC     ErrInfo;
    /** Current attributes parsed. */
    XPIDLATTR           aAttrs[32];
    /** Number of entries in the attribute array. */
    uint32_t            cAttrs;
} XPIDLPARSE;
/** Pointer to an IDL parsing state. */
typedef XPIDLPARSE *PXPIDLPARSE;
/** Pointer to a const IDL parsing state. */
typedef const XPIDLPARSE *PCXPIDLPARSE;


/*
 * Internal operation flags.
 */
extern bool enable_debug;
extern bool enable_warnings;
extern bool verbose_mode;
extern bool emit_typelib_annotations;
extern bool explicit_output_filename;

extern PRUint8  major_version;
extern PRUint8  minor_version;


/**
 * Dispatch callback.
 *
 * @returns IPRT status code.
 * @param   pFile       The file to output to.
 * @param   pInput      The original input file to generate for.
 * @param   pParse      The parsing state.
 * @param   pErrInfo    Error information.
 */
typedef DECLCALLBACKTYPE(int, FNXPIDLDISPATCH,(FILE *pFile, PCXPIDLINPUT pInput, PCXPIDLPARSE pParse, PRTERRINFO pErrInfo));
/** Pointer to a dispatch callback. */
typedef FNXPIDLDISPATCH *PFNXPIDLDISPATCH;


DECL_HIDDEN_CALLBACK(int) xpidl_header_dispatch(FILE *pFile, PCXPIDLINPUT pInput, PCXPIDLPARSE pParse, PRTERRINFO pErrInfo);
DECL_HIDDEN_CALLBACK(int) xpidl_typelib_dispatch(FILE *pFile, PCXPIDLINPUT pInput, PCXPIDLPARSE pParse, PRTERRINFO pErrInfo);

typedef struct ModeData {
    char               *mode;
    char               *modeInfo;
    char               *suffix;
    PFNXPIDLDISPATCH    dispatch;
} ModeData;


/*
 * Process an IDL file, generating InterfaceInfo, documentation and headers as
 * appropriate.
 */
int
xpidl_process_idl(char *filename, PRTLISTANCHOR pLstIncludePaths,
                  char *file_basename, ModeData *mode);

/*
 * Wrapper whines to stderr then exits after null return from malloc or strdup.
 */
void *
xpidl_malloc(size_t nbytes);

char *
xpidl_strdup(const char *s);

/*
 * Return a newly allocated string to the start of the base filename of path.
 * Free with g_free().
 */
char *
xpidl_basename(const char * path);


/*
 * Functions for parsing and printing UUIDs.
 */

/*
 * How large should the buffer supplied to xpidl_sprint_IID be?
 */
#define UUID_LENGTH 37

/*
 * Print an iid to into a supplied buffer; the buffer should be at least
 * UUID_LENGTH bytes.
 */
bool
xpidl_sprint_iid(nsID *iid, char iidbuf[]);

/*
 * Parse a uuid string into an nsID struct.  We cannot link against libxpcom,
 * so we re-implement nsID::Parse here.
 */
bool
xpidl_parse_iid(nsID *id, const char *str);


DECLHIDDEN(PCXPIDLATTR) xpidlNodeAttrFind(PCXPIDLNODE pNd, const char *pszAttr);


DECLHIDDEN(int) xpidlIdlError(PRTERRINFO pErrInfo, PCXPIDLNODE pNd, int rc, const char *pszFmt, ...);

/* Try to common a little node-handling stuff. */


DECLINLINE(bool) xpidlNdIsStringType(PCXPIDLNODE pNd)
{
    return    pNd->enmType == kXpidlNdType_BaseType
           && (   pNd->u.enmBaseType == kXpidlType_String
               || pNd->u.enmBaseType == kXpidlType_Wide_String);
}


/* is this node from an aggregate type (interface)? */
#define UP_IS_AGGREGATE(a_pNd) \
    (   a_pNd->pNdTypeRef \
     && (   a_pNd->pNdTypeRef->enmType == kXpidlNdType_Interface_Forward_Decl \
         || a_pNd->pNdTypeRef->enmType == kXpidlNdType_Interface_Def))

#define UP_IS_NATIVE(a_pNd) \
    (   a_pNd->pNdTypeRef \
     && a_pNd->pNdTypeRef->enmType == kXpidlNdType_Native)

/* is this type output in the form "<foo> *"? */
#define STARRED_TYPE(a_pNd) (xpidlNdIsStringType(a_pNd) ||   \
                            (a_pNd->enmType == kXpidlNdType_Identifier &&     \
                             UP_IS_AGGREGATE(a_pNd)))

#define DIPPER_TYPE(a_pNd)                                                     \
    (xpidlNodeAttrFind(a_pNd, "domstring") != NULL  ||                     \
     xpidlNodeAttrFind(a_pNd, "utf8string") != NULL ||                     \
     xpidlNodeAttrFind(a_pNd, "cstring") != NULL    ||                     \
     xpidlNodeAttrFind(a_pNd, "astring") != NULL)


/*
 * Find the underlying type of an identifier typedef.  Returns NULL
 * (and doesn't complain) on failure.
 */
DECLHIDDEN(PCXPIDLNODE) find_underlying_type(PCXPIDLNODE pNd);

/*
 * Verifies the interface declaration
 */
DECLHIDDEN(int) verify_interface_declaration(PCXPIDLNODE pNd, PRTERRINFO pErrInfo);

/*
 * Check that const declarations match their stated sign and are of the
 * appropriate types.
 */
DECLHIDDEN(int) verify_const_declaration(PCXPIDLNODE pNd, PRTERRINFO pErrInfo);

/*
 * Check that scriptable attributes in scriptable interfaces actually are.
 */
DECLHIDDEN(int) verify_attribute_declaration(PCXPIDLNODE pNd, PRTERRINFO pErrInfo);

/*
 * Perform various validation checks on methods.
 */
DECLHIDDEN(int) verify_method_declaration(PCXPIDLNODE pNd, PRTERRINFO pErrInfo);

/*
 * Verify that a native declaration has an associated C++ expression, i.e. that
 * it's of the form native <idl-name>(<c++-name>)
 */
DECLHIDDEN(int) check_native(PCXPIDLNODE pNd, PRTERRINFO pErrInfo);

#endif /* __xpidl_h */
