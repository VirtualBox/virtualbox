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
 * Generate typelib files for use with InterfaceInfo.
 * http://www.mozilla.org/scriptable/typelib_file.html
 */
#include <iprt/assert.h>
#include <iprt/string.h>

#include "xpidl.h"
#include <xpt_xdr.h>
#include <xpt_struct.h>
#include <time.h>               /* XXX XP? */

typedef struct XPIDLTYPELIBSTATE {
    XPTHeader *header;
    uint16 ifaces;
    RTLISTANCHOR LstInterfaces;
    XPTInterfaceDescriptor *current;
    XPTArena *arena;
    uint16 next_method;
    uint16 next_const;
    uint16 next_type;   /* used for 'additional_types' for idl arrays */
    PRTERRINFO pErrInfo;
} XPIDLTYPELIBSTATE;
typedef XPIDLTYPELIBSTATE *PXPIDLTYPELIBSTATE;

#define HEADER(state)     (state->header)
#define IFACES(state)     (state->ifaces)
#define IFACE_MAP(state)  (state->LstInterfaces)
#define CURRENT(state)    (state->current)
#define ARENA(state)      (state->arena)
#define NEXT_METH(state)  (state->next_method)
#define NEXT_CONST(state) (state->next_const)
#define NEXT_TYPE(state)  (state->next_type)

#ifdef DEBUG_shaver
/* #define DEBUG_shaver_sort */
#endif

typedef struct {
    RTLISTNODE NdInterfaces;
    char     *full_name;
    char     *name;
    char     *name_space;
    char     *iid;
    bool     is_forward_dcl;
} NewInterfaceHolder;

static NewInterfaceHolder*
CreateNewInterfaceHolder(const char *name, char *name_space, char *iid, 
                         bool is_forward_dcl)
{
    NewInterfaceHolder *holder = calloc(1, sizeof(NewInterfaceHolder));
    if (holder) {
        holder->is_forward_dcl = is_forward_dcl;
        if (name)
            holder->name = xpidl_strdup(name);
        if (name_space)
            holder->name_space = xpidl_strdup(name_space);
        if (holder->name && holder->name_space) {
            holder->full_name = calloc(1, strlen(holder->name) +
                                          strlen(holder->name_space) + 2);
        }
        if (holder->full_name) {
            strcpy(holder->full_name, holder->name_space);
            strcat(holder->full_name, ".");
            strcat(holder->full_name, holder->name);
        }
        else
            holder->full_name = holder->name;
        if (iid)
            holder->iid = xpidl_strdup(iid);
    }
    return holder;
}

static void
DeleteNewInterfaceHolder(NewInterfaceHolder *holder)
{
    if (holder) {
        if (holder->full_name && holder->full_name != holder->name)
            free(holder->full_name);
        if (holder->name)
            free(holder->name);
        if (holder->name_space)
            free(holder->name_space);
        if (holder->iid)
            free(holder->iid);
        free(holder);
    }
}


static XPTInterfaceDirectoryEntry *FindInterfaceByName(XPTInterfaceDirectoryEntry *ides, uint16 num_interfaces,
                                                       const char *name, uint16_t *pu16Id)
{
    uint16_t i;
    for (i = 0; i < num_interfaces; i++) {
        if (!strcmp(ides[i].name, name))
        {
            if (pu16Id)
                *pu16Id = i + 1;
            return &ides[i];
        }
    }
    return NULL;
}


/*
 * If p is an ident for an interface, and we don't have an entry in the
 * interface map yet, add one.
 */
static bool add_interface_maybe(PXPIDLTYPELIBSTATE pThis, PCXPIDLNODE pNd)
{
    if (pNd->enmType == kXpidlNdType_Identifier)
    {
        pNd = pNd->pNdTypeRef;
        AssertPtr(pNd);
    }

    if (   pNd->enmType == kXpidlNdType_Interface_Forward_Decl
        || pNd->enmType == kXpidlNdType_Interface_Def)
    {

        /* We only want to add a new entry if there is no entry by this 
         * name or if the previously found entry was just a forward 
         * declaration and the new entry is not.
         */

        const char *pszIfName =   pNd->enmType == kXpidlNdType_Interface_Forward_Decl
                                ? pNd->u.pszIfFwdName
                                : pNd->u.If.pszIfName;
        NewInterfaceHolder *old_holder = NULL;

        NewInterfaceHolder *pIt;
        RTListForEach(&IFACE_MAP(pThis), pIt, NewInterfaceHolder, NdInterfaces)
        {
            if (!strcmp(pIt->name, pszIfName))
            {
                old_holder = pIt;
                break;
            }
        }

        if (   old_holder
            && old_holder->is_forward_dcl
            && pNd->enmType != kXpidlNdType_Interface_Forward_Decl)
        {
            RTListNodeRemove(&old_holder->NdInterfaces);
            DeleteNewInterfaceHolder(old_holder);
            IFACES(pThis)--;
            old_holder = NULL;
        }

        if (!old_holder)
        {
            /* XXX should we parse here and store a struct nsID *? */
            PCXPIDLATTR pAttr = xpidlNodeAttrFind(pNd, "uuid");
            char *iid = NULL;
            if (pAttr)
            {
                if (pAttr->pszVal)
                    iid = (char *)pAttr->pszVal;
                else
                    return false; /* uuid requires an argument. */
            }

            pAttr = xpidlNodeAttrFind(pNd, "namespace");
            char *name_space = NULL;
            if (pAttr)
            {
                if (pAttr->pszVal)
                    name_space = (char *)pAttr->pszVal;
                else
                    return false; /* uuid requires an argument. */
            }

            NewInterfaceHolder *holder = CreateNewInterfaceHolder(pszIfName, name_space, iid, pNd->enmType == kXpidlNdType_Interface_Forward_Decl);
            if (!holder)
                return false;
            RTListAppend(&pThis->LstInterfaces, &holder->NdInterfaces);                

            IFACES(pThis)++;
#ifdef DEBUG_shaver_ifaces
            fprintf(stderr, "adding interface #%d: %s/%s\n", IFACES(pThis),
                    pszIfName, iid ? iid : "<unresolved>");
#endif
        }
    }

    return true;
}

/* Find all the interfaces referenced in the tree (uses add_interface_maybe) */
static bool find_interfaces(PXPIDLTYPELIBSTATE pThis, PCXPIDLINPUT pInput, PCRTLISTANCHOR pLstNodes)
{
    PCXPIDLNODE pIt;
    RTListForEach(pLstNodes, pIt, XPIDLNODE, NdLst)
    {
        switch (pIt->enmType)
        {
            case kXpidlNdType_Identifier:
            {
                if (pIt->u.Attribute.pNdTypeSpec)
                    add_interface_maybe(pThis, pIt->u.Attribute.pNdTypeSpec);
                break;
            }
            case kXpidlNdType_Interface_Forward_Decl:
                add_interface_maybe(pThis, pIt);
                break;
            case kXpidlNdType_Interface_Def:
                if (pIt->pInput != pInput) /* Skip anything not top level. */
                    continue;

                if (pIt->pNdTypeRef)
                    add_interface_maybe(pThis, pIt->pNdTypeRef);
                add_interface_maybe(pThis, pIt);
                if (!find_interfaces(pThis, pInput, &pIt->u.If.LstBody))
                    return false;
                break;
            case kXpidlNdType_Attribute:
                add_interface_maybe(pThis, pIt->u.Attribute.pNdTypeSpec);
                break;
            case kXpidlNdType_Method:
                add_interface_maybe(pThis, pIt->u.Method.pNdTypeSpecRet);
                if (!find_interfaces(pThis, pInput, &pIt->u.Method.LstParams))
                    return false;
                break;
            case kXpidlNdType_Parameter:
                add_interface_maybe(pThis, pIt->u.Param.pNdTypeSpec);
                break;
            default:
                break;
        }
    }

    return true;
}

#ifdef DEBUG_shaver
/* for calling from gdb */
static void
print_IID(struct nsID *iid, FILE *file)
{
    char iid_buf[UUID_LENGTH];

    xpidl_sprint_iid(iid, iid_buf);
    fprintf(file, "%s\n", iid_buf);
}
#endif

/* fill the interface_directory IDE table from the interface_map */
static int fill_ide_table(PXPIDLTYPELIBSTATE pThis)
{
    NewInterfaceHolder *pIt, *pItNext;
    RTListForEachSafe(&pThis->LstInterfaces, pIt, pItNext, NewInterfaceHolder, NdInterfaces)
    {
        struct nsID id;
        XPTInterfaceDirectoryEntry *ide;

        XPT_ASSERT(pIt);

#ifdef DEBUG_shaver_ifaces
        fprintf(stderr, "filling %s\n", pIt->full_name);
#endif

        if (pIt->iid)
        {
            if (strlen(pIt->iid) != 36)
                return xpidlIdlError(pThis->pErrInfo, NULL /*pNd*/, VERR_INVALID_STATE,
                                     "IID %s is the wrong length", pIt->iid);

            if (!xpidl_parse_iid(&id, pIt->iid))
                return xpidlIdlError(pThis->pErrInfo, NULL /*pNd*/, VERR_INVALID_STATE,
                                     "cannot parse IID %s\n", pIt->iid);
        }
        else
            memset(&id, 0, sizeof(id));

        ide = &(HEADER(pThis)->interface_directory[IFACES(pThis)]);
        if (!XPT_FillInterfaceDirectoryEntry(ARENA(pThis), ide, &id, pIt->name,
                                             pIt->name_space, NULL))
            return xpidlIdlError(pThis->pErrInfo, NULL /*pNd*/, VERR_INVALID_STATE,
                                 "INTERNAL: XPT_FillIDE failed for %s\n", pIt->full_name);

        IFACES(pThis)++;
        RTListNodeRemove(&pIt->NdInterfaces);
        DeleteNewInterfaceHolder(pIt);
    }
    return VINF_SUCCESS;
}

static int
compare_IDEs(const void *ap, const void *bp)
{
    const XPTInterfaceDirectoryEntry *a = ap, *b = bp;
    const nsID *aid = &a->iid, *bid = &b->iid;

    int i;
#define COMPARE(field) if (aid->field > bid->field) return 1; \
                       if (bid->field > aid->field) return -1;
    COMPARE(m0);
    COMPARE(m1);
    COMPARE(m2);
    for (i = 0; i < 8; i++) {
        COMPARE(m3[i]);
    }

    if (a->name_space && b->name_space) {
        if ((i = strcmp(a->name_space, b->name_space)))
            return i;
    } else {
        if (a->name_space || b->name_space) {
            if (a->name_space)
                return -1;
            return 1;
        }
    }
    /* these had better not be NULL... */
    return strcmp(a->name, b->name);
#undef COMPARE
}

/* sort the IDE block as per the typelib spec: IID order, unresolved first */
static void
sort_ide_block(PXPIDLTYPELIBSTATE pThis)
{
    XPTInterfaceDirectoryEntry *ide;

    /* boy, I sure hope qsort works correctly everywhere */
#ifdef DEBUG_shaver_sort
    fputs("before sort:\n", stderr);
    for (uint16_t i = 0; i < IFACES(pThis); i++) {
        fputs("  ", stderr);
        print_IID(&HEADER(pThis)->interface_directory[i].iid, stderr);
        fputc('\n', stderr);
    }
#endif
    qsort(HEADER(pThis)->interface_directory, IFACES(pThis),
          sizeof(*ide), compare_IDEs);
#ifdef DEBUG_shaver_sort
    fputs("after sort:\n", stderr);
    for (uint16_t i = 0; i < IFACES(pThis); i++) {
        fputs("  ", stderr);
        print_IID(&HEADER(pThis)->interface_directory[i].iid, stderr);
        fputc('\n', stderr);
    }
#endif
}


static int typelib_prolog(PXPIDLTYPELIBSTATE pThis, PCXPIDLINPUT pInput, PCXPIDLPARSE pParse)
{
    IFACES(pThis) = 0;
    RTListInit(&IFACE_MAP(pThis));

    /* find all interfaces, top-level and referenced by others */
    if (!find_interfaces(pThis, pInput, &pParse->LstNodes))
        return VERR_BUFFER_OVERFLOW;

    ARENA(pThis) = XPT_NewArena(1024, sizeof(double), "main xpidl arena");
    HEADER(pThis) = XPT_NewHeader(ARENA(pThis), IFACES(pThis), 
                                  major_version, minor_version);

    /* fill IDEs from hash table */
    IFACES(pThis) = 0;
    int rc = fill_ide_table(pThis);
    if (RT_FAILURE(rc))
        return rc;

    /* if any are left then we must have failed in fill_ide_table */
    if (!RTListIsEmpty(&IFACE_MAP(pThis)))
        return VERR_BUFFER_OVERFLOW;

    /* sort the IDEs by IID order and store indices in the interface map */
    sort_ide_block(pThis);

    return VINF_SUCCESS;
}

static int typelib_epilog(PXPIDLTYPELIBSTATE pThis, FILE *pFile, PCXPIDLINPUT pInput)
{
    XPTState *xstate = XPT_NewXDRState(XPT_ENCODE, NULL, 0);
    XPTCursor curs, *cursor = &curs;
    PRUint32 i, len, header_sz;
    PRUint32 oldOffset;
    PRUint32 newOffset;
    char *data;

    /* Write any annotations */
    if (emit_typelib_annotations) {
        PRUint32 annotation_len, written_so_far;
        char *annotate_val, *timestr;
        time_t now;
        static char *annotation_format = 
            "Created from %s.idl\nCreation date: %sInterfaces:";

        /* fill in the annotations, listing resolved interfaces in order */

        (void)time(&now);
        timestr = ctime(&now);

        /* Avoid dependence on nspr; no PR_smprintf and friends. */

        /* How large should the annotation string be? */
        annotation_len = strlen(annotation_format) + strlen(pInput->pszBasename) +
            strlen(timestr);
#ifdef VBOX
        /* note that '%s' is contained two times in annotation_format and both
         * format specifiers are replaced by a string. So in fact we reserve 4
         * bytes minus one byte (for the terminating '\0') more than necessary. */
#endif
        for (i = 0; i < HEADER(pThis)->num_interfaces; i++) {
            XPTInterfaceDirectoryEntry *ide;
            ide = &HEADER(pThis)->interface_directory[i];
            if (ide->interface_descriptor) {
                annotation_len += strlen(ide->name) + 1;
            }
        }

        annotate_val = (char *) malloc(annotation_len);
        written_so_far = sprintf(annotate_val, annotation_format,
                                 pInput->pszBasename, timestr);
        
        for (i = 0; i < HEADER(pThis)->num_interfaces; i++) {
            XPTInterfaceDirectoryEntry *ide;
            ide = &HEADER(pThis)->interface_directory[i];
            if (ide->interface_descriptor) {
                written_so_far += sprintf(annotate_val + written_so_far, " %s",
                                          ide->name);
            }
        }

        HEADER(pThis)->annotations =
            XPT_NewAnnotation(ARENA(pThis), 
                              XPT_ANN_LAST | XPT_ANN_PRIVATE,
                              XPT_NewStringZ(ARENA(pThis), "xpidl 0.99.9"),
                              XPT_NewStringZ(ARENA(pThis), annotate_val));
        free(annotate_val);
    } else {
        HEADER(pThis)->annotations =
            XPT_NewAnnotation(ARENA(pThis), XPT_ANN_LAST, NULL, NULL);
    }

    if (!HEADER(pThis)->annotations) {
        /* XXX report out of memory error */
        return false;
    }

    /* Write the typelib */
    header_sz = XPT_SizeOfHeaderBlock(HEADER(pThis));

    if (!xstate ||
        !XPT_MakeCursor(xstate, XPT_HEADER, header_sz, cursor))
        goto destroy_header;
    oldOffset = cursor->offset;
    if (!XPT_DoHeader(ARENA(pThis), cursor, &HEADER(pThis)))
        goto destroy;
    newOffset = cursor->offset;
    XPT_GetXDRDataLength(xstate, XPT_HEADER, &len);
    HEADER(pThis)->file_length = len;
    XPT_GetXDRDataLength(xstate, XPT_DATA, &len);
    HEADER(pThis)->file_length += len;
    XPT_SeekTo(cursor, oldOffset);
    if (!XPT_DoHeaderPrologue(ARENA(pThis), cursor, &HEADER(pThis), NULL))
        goto destroy;
    XPT_SeekTo(cursor, newOffset);
    XPT_GetXDRData(xstate, XPT_HEADER, &data, &len);
    fwrite(data, len, 1, pFile);
    XPT_GetXDRData(xstate, XPT_DATA, &data, &len);
    fwrite(data, len, 1, pFile);

 destroy:
    XPT_DestroyXDRState(xstate);
 destroy_header:
    /* XXX XPT_DestroyHeader(HEADER(pThis)) */

    XPT_FreeHeader(ARENA(pThis), HEADER(pThis));
    XPT_DestroyArena(ARENA(pThis));
    return VINF_SUCCESS;
}

static bool find_arg_with_name(PCXPIDLNODE pNd, const char *name, int16 *argnum)
{
    XPT_ASSERT(name);
    XPT_ASSERT(argnum);

    Assert(pNd->enmType == kXpidlNdType_Parameter);
    pNd = pNd->pParent;
    Assert(pNd->enmType == kXpidlNdType_Method);

    uint16_t idxArgNum = 0;
    PCXPIDLNODE pIt;
    RTListForEach(&pNd->u.Method.LstParams, pIt, XPIDLNODE, NdLst)
    {
        Assert(pIt->enmType == kXpidlNdType_Parameter);
        if (!strcmp(pIt->u.Param.pszName, name))
        {
            /* XXX ought to verify that this is the right type here */
            /* XXX for iid_is this must be an iid */
            /* XXX for size_is and length_is this must be a uint32 */
            *argnum = idxArgNum;
            return true;
        }
        idxArgNum++;
    }
    return false;
}


/* return value is for success or failure */
static int get_size_and_length(PXPIDLTYPELIBSTATE pThis, PCXPIDLNODE pNdType,
                               int16 *size_is_argnum, int16 *length_is_argnum,
                               bool *has_size_is, bool *has_length_is)
{
    *has_size_is = false;
    *has_length_is = false;

    if (pNdType->enmType == kXpidlNdType_Parameter)
    {
        /* only if size_is is found does any of this matter */
        PCXPIDLATTR pAttr = xpidlNodeAttrFind(pNdType, "size_is");
        if (!pAttr)
            return true;
        if (!pAttr->pszVal) /* Attribute needs a value. */
            return false;

        if (!find_arg_with_name(pNdType, pAttr->pszVal, size_is_argnum))
            return xpidlIdlError(pThis->pErrInfo, NULL /*pNd*/, VERR_INVALID_STATE,
                                 "can't find matching argument for [size_is(%s)]", pAttr->pszVal);
        *has_size_is = true;

        /* length_is is optional */
        pAttr = xpidlNodeAttrFind(pNdType, "length_is");
        if (!pAttr)
            return true;
        if (!pAttr->pszVal) /* Attribute needs a value. */
            return false;

        if (!find_arg_with_name(pNdType, pAttr->pszVal, length_is_argnum))
            return xpidlIdlError(pThis->pErrInfo, NULL /*pNd*/, VERR_INVALID_STATE,
                                 "can't find matching argument for [length_is(%s)]\n", pAttr->pszVal);
        *has_length_is = true;
    }

    return VINF_SUCCESS;
}


static int fill_td_from_type(PXPIDLTYPELIBSTATE pThis, XPTTypeDescriptor *td, PCXPIDLNODE pNdType)
{
    int16 size_is_argnum;
    int16 length_is_argnum;
    bool has_size_is;
    bool has_length_is;
    bool is_array = false;

    if (   pNdType->enmType == kXpidlNdType_BaseType
        && pNdType->u.enmBaseType == kXpidlType_Void)
        td->prefix.flags = TD_VOID;
    else
    {
        PCXPIDLNODE pNdParam = NULL;
        if (   pNdType->pParent
            && pNdType->pParent->enmType == kXpidlNdType_Parameter)
            pNdParam = pNdType->pParent;

        if (   pNdParam
            && xpidlNodeAttrFind(pNdParam, "array"))
        {
            is_array = true;

            /* size_is is required! */
            int rc = get_size_and_length(pThis, pNdParam, 
                                         &size_is_argnum, &length_is_argnum,
                                         &has_size_is, &has_length_is);
            if (RT_FAILURE(rc))
                return rc; /* error was reported by helper function */

            if (!has_size_is)
                return xpidlIdlError(pThis->pErrInfo, NULL /*pNd*/, VERR_NOT_FOUND, 
                                     "[array] requires [size_is()]\n");

            td->prefix.flags = TD_ARRAY | XPT_TDP_POINTER;
            td->argnum = size_is_argnum;

            if (has_length_is)
                td->argnum2 = length_is_argnum;
            else
                td->argnum2 = size_is_argnum;

            /* 
            * XXX - NOTE - this will be broken for multidimensional 
            * arrays because of the realloc XPT_InterfaceDescriptorAddTypes
            * uses. The underlying 'td' can change as we recurse in to get
            * additional dimensions. Luckily, we don't yet support more
            * than on dimension in the arrays
            */
            /* setup the additional_type */                
            if (!XPT_InterfaceDescriptorAddTypes(ARENA(pThis), CURRENT(pThis), 1))
                return xpidlIdlError(pThis->pErrInfo, pNdType, VERR_NO_MEMORY, 
                                     "Failed to add types to interface descriptor\n");

            td->type.additional_type = NEXT_TYPE(pThis);
            td = &CURRENT(pThis)->additional_types[NEXT_TYPE(pThis)];
            NEXT_TYPE(pThis)++ ;
        }

handle_typedef:
        if (pNdType->enmType == kXpidlNdType_BaseType)
        {
            switch (pNdType->u.enmBaseType)
            {
                case kXpidlType_Boolean:
                    td->prefix.flags = TD_BOOL;
                    break;
                case kXpidlType_Octet:
                    td->prefix.flags = TD_UINT8;
                    break;
                case kXpidlType_Char:
                    td->prefix.flags = TD_CHAR;
                    break;
                case kXpidlType_Wide_Char:
                    td->prefix.flags = TD_WCHAR;
                    break;
                case kXpidlType_Short:
                    td->prefix.flags = TD_INT16;
                    break;
                case kXpidlType_Long:
                    td->prefix.flags = TD_INT32;
                    break;
                case kXpidlType_Long_Long:
                    td->prefix.flags = TD_INT64;
                    break;
                case kXpidlType_Unsigned_Short:
                    td->prefix.flags = TD_UINT16;
                    break;
                case kXpidlType_Unsigned_Long:
                    td->prefix.flags = TD_UINT32;
                    break;
                case kXpidlType_Unsigned_Long_Long:
                    td->prefix.flags = TD_UINT64;
                    break;
                case kXpidlType_String:
                    if (is_array)
                        td->prefix.flags = TD_PSTRING | XPT_TDP_POINTER;
                    else
                    {
                        int rc = get_size_and_length(pThis, pNdType, 
                                                     &size_is_argnum, &length_is_argnum,
                                                     &has_size_is, &has_length_is);
                        if (RT_FAILURE(rc))
                            return rc; /* error was reported by helper function */

                        if (has_size_is)
                        {
                            td->prefix.flags = TD_PSTRING_SIZE_IS | XPT_TDP_POINTER;
                            td->argnum = size_is_argnum;
                            if (has_length_is)
                                td->argnum2 = length_is_argnum;
                            else
                                td->argnum2 = size_is_argnum;
                        }
                        else
                            td->prefix.flags = TD_PSTRING | XPT_TDP_POINTER;
                    }
                    break;
                case kXpidlType_Wide_String:
                    if (is_array)
                        td->prefix.flags = TD_PWSTRING | XPT_TDP_POINTER;
                    else
                    {
                        int rc = get_size_and_length(pThis, pNdType, 
                                                     &size_is_argnum, &length_is_argnum,
                                                     &has_size_is, &has_length_is);
                        if (RT_FAILURE(rc))
                            return rc; /* error was reported by helper function */

                        if (has_size_is)
                        {
                            td->prefix.flags = TD_PWSTRING_SIZE_IS | XPT_TDP_POINTER;
                            td->argnum = size_is_argnum;
                            if (has_length_is)
                                td->argnum2 = length_is_argnum;
                            else
                                td->argnum2 = size_is_argnum;
                        }
                        else
                            td->prefix.flags = TD_PWSTRING | XPT_TDP_POINTER;
                    }
                    break;
                case kXpidlType_Double:
                    td->prefix.flags = TD_DOUBLE;
                    break;
                case kXpidlType_Float:
                    td->prefix.flags = TD_FLOAT;
                    break;
                default:
                    AssertReleaseFailed();
            }
        }
        else if (pNdType->enmType == kXpidlNdType_Identifier)
        {
            if (!pNdType->pNdTypeRef)
                return xpidlIdlError(pThis->pErrInfo, pNdType, VERR_NOT_FOUND,
                                     "ERROR: orphan ident %s in param list\n", pNdType->u.pszIde);

            /* This whole section is abominably ugly */
            PCXPIDLNODE pNdTypeRef = pNdType->pNdTypeRef;
            switch (pNdTypeRef->enmType)
            {
                case kXpidlNdType_Interface_Forward_Decl:
                case kXpidlNdType_Interface_Def:
                {
                    XPTInterfaceDirectoryEntry *ide, *ides;
                    uint16 num_ifaces;
                    const char *className;
                    const char *iid_is;
handle_iid_is:
                    className = NULL;
                    ides = HEADER(pThis)->interface_directory;
                    num_ifaces = HEADER(pThis)->num_interfaces;
                    /* might get here via the goto, so re-check type */
                    if (pNdTypeRef->enmType == kXpidlNdType_Interface_Def)
                        className = pNdTypeRef->u.If.pszIfName;
                    else if (pNdTypeRef->enmType == kXpidlNdType_Interface_Forward_Decl)
                        className = pNdTypeRef->u.pszIfFwdName;
                    else
                        Assert(   pNdParam
                               && xpidlNodeAttrFind(pNdParam, "iid_is")); //className = IDL_IDENT(IDL_NATIVE(up).ident).str;
                    iid_is = NULL;

                    if (pNdParam)
                    {
                        PCXPIDLATTR pAttr = xpidlNodeAttrFind(pNdParam, "iid_is");
                        if (pAttr)
                        {
                            if (!pAttr->pszVal) /* iid_is requires a value */
                                return false;
                            iid_is = pAttr->pszVal;
                        }
                    }

                    if (iid_is) {
                        int16 argnum;
                        if (!find_arg_with_name(pNdParam, iid_is, &argnum))
                            return xpidlIdlError(pThis->pErrInfo, pNdParam, VERR_NOT_FOUND,
                                                 "can't find matching argument for [iid_is(%s)]", iid_is);

                        td->prefix.flags = TD_INTERFACE_IS_TYPE | XPT_TDP_POINTER;
                        td->argnum = argnum;
                    } else {
                        td->prefix.flags = TD_INTERFACE_TYPE | XPT_TDP_POINTER;
                        ide = FindInterfaceByName(ides, num_ifaces, className, NULL);
                        if (!ide || ide < ides || ide > ides + num_ifaces)
                            return xpidlIdlError(pThis->pErrInfo, pNdParam, VERR_NOT_FOUND,
                                                 "unknown iface %s in param\n", className);

                        td->type.iface = ide - ides + 1;
#ifdef DEBUG_shaver_index
                        fprintf(stderr, "DBG: index %d for %s\n",
                                td->type.iface, className);
#endif
                    }
                    break;
                }
                case kXpidlNdType_Native:
                {
                    /* jband - adding goto for iid_is when type is native */
                    if (   pNdParam
                        && xpidlNodeAttrFind(pNdParam, "iid_is"))
                        goto handle_iid_is;

                    if (xpidlNodeAttrFind(pNdTypeRef, "nsid")) {
                        td->prefix.flags = TD_PNSIID;
                        if (xpidlNodeAttrFind(pNdTypeRef, "ref"))
                            td->prefix.flags |= XPT_TDP_POINTER | XPT_TDP_REFERENCE;
                        else if (xpidlNodeAttrFind(pNdTypeRef,"ptr"))
                            td->prefix.flags |= XPT_TDP_POINTER;
                    } else if (xpidlNodeAttrFind(pNdTypeRef, "domstring")) {
                        td->prefix.flags = TD_DOMSTRING | XPT_TDP_POINTER;
                        if (xpidlNodeAttrFind(pNdTypeRef, "ref"))
                            td->prefix.flags |= XPT_TDP_REFERENCE;
                    } else if (xpidlNodeAttrFind(pNdTypeRef, "astring")) {
                        td->prefix.flags = TD_ASTRING | XPT_TDP_POINTER;
                        if (xpidlNodeAttrFind(pNdTypeRef, "ref"))
                            td->prefix.flags |= XPT_TDP_REFERENCE;
                    } else if (xpidlNodeAttrFind(pNdTypeRef, "utf8string")) {
                        td->prefix.flags = TD_UTF8STRING | XPT_TDP_POINTER;
                        if (xpidlNodeAttrFind(pNdTypeRef, "ref"))
                            td->prefix.flags |= XPT_TDP_REFERENCE;
                    } else if (xpidlNodeAttrFind(pNdTypeRef, "cstring")) {
                        td->prefix.flags = TD_CSTRING | XPT_TDP_POINTER;
                        if (xpidlNodeAttrFind(pNdTypeRef, "ref"))
                            td->prefix.flags |= XPT_TDP_REFERENCE;
                    } else {
                        td->prefix.flags = TD_VOID | XPT_TDP_POINTER;
                    }
                    break;
                }
                case kXpidlNdType_Typedef:
                {
                    /* restart with the underlying type */
                    
#ifdef DEBUG_shaver_misc
                    fprintf(stderr, "following %s typedef to %u\n",
                            pNdType->u.Typedef.pszName, pNdType->u.Typedef.pNodeTypeSpec);
#endif
                    /* 
                    *  Do a nice messy goto rather than recursion so that
                    *  we can avoid screwing up the *array* information.
                    */
                    if (pNdTypeRef->u.Typedef.pNodeTypeSpec)
                    {
                        pNdType = pNdTypeRef->u.Typedef.pNodeTypeSpec;
                        goto handle_typedef;
                    }
                    else
                    {
                        /* do what we would do in recursion if !type */
                        td->prefix.flags = TD_VOID;
                        return VINF_SUCCESS;
                    }
                }
                default:
                    xpidlIdlError(pThis->pErrInfo, pNdType, VERR_INTERNAL_ERROR,
                                  "can't handle %s ident in param list\n",
                                  pNdType->u.pszIde);
                    AssertFailedReturn(VERR_INTERNAL_ERROR);
            }
        }
        else
            AssertFailedReturn(VERR_INTERNAL_ERROR);
    }

    return VINF_SUCCESS;
}


static int fill_pd_from_type(PXPIDLTYPELIBSTATE pThis, XPTParamDescriptor *pd, uint8 flags, PCXPIDLNODE pNd)
{
    pd->flags = flags;
    return fill_td_from_type(pThis, &pd->type, pNd);
}

static int fill_pd_from_param(PXPIDLTYPELIBSTATE pThis, XPTParamDescriptor *pd, PCXPIDLNODE pNd)
{
    uint8 flags = 0;
    bool is_dipper_type = DIPPER_TYPE(pNd->u.Param.pNdTypeSpec);

    switch (pNd->u.Param.enmDir)
    {
        case kXpidlDirection_In:
            flags = XPT_PD_IN;
            break;
        case kXpidlDirection_Out:
            flags = XPT_PD_OUT;
            break;
        case kXpidlDirection_InOut:
            flags = XPT_PD_IN | XPT_PD_OUT;
            break;
        default:
            AssertReleaseFailed();
    }

    if (xpidlNodeAttrFind(pNd, "retval"))
    {
        if (flags != XPT_PD_OUT)
            return xpidlIdlError(pThis->pErrInfo, NULL /*pNd*/, VERR_INVALID_STATE,
                                 "can't have [retval] with in%s param (only out)",
                                 flags & XPT_PD_OUT ? "out" : "");

        flags |= XPT_PD_RETVAL;
    }

    if (is_dipper_type && (flags & XPT_PD_OUT))
    {
        flags &= ~XPT_PD_OUT; 
        flags |= XPT_PD_IN | XPT_PD_DIPPER;
    }

    if (xpidlNodeAttrFind(pNd, "shared"))
    {
        if (flags & XPT_PD_IN)
            return xpidlIdlError(pThis->pErrInfo, NULL /*pNd*/, VERR_INVALID_STATE,
                                 "can't have [shared] with in%s param (only out)",
                                 flags & XPT_PD_OUT ? "out" : "");

        flags |= XPT_PD_SHARED;
    }

    return fill_pd_from_type(pThis, pd, flags, pNd->u.Param.pNdTypeSpec);
}


static int fill_pd_as_nsresult(XPTParamDescriptor *pd)
{
    pd->type.prefix.flags = TD_UINT32; /* TD_NSRESULT */
    return VINF_SUCCESS;
}

static int typelib_attr_accessor(PXPIDLTYPELIBSTATE pThis, PCXPIDLNODE pNd,
                                 XPTMethodDescriptor *meth, bool getter, bool hidden)
{
    uint8 methflags = 0;
    uint8 pdflags = 0;

    methflags |= getter ? XPT_MD_GETTER : XPT_MD_SETTER;
    methflags |= hidden ? XPT_MD_HIDDEN : 0;
    if (!XPT_FillMethodDescriptor(ARENA(pThis), meth, methflags,
                                  (char *)pNd->u.Attribute.pszName, 1))
        return xpidlIdlError(pThis->pErrInfo, pNd, VERR_NO_MEMORY,
                             "Failed to fill method descriptor for attribute '%s'",
                             pNd->u.Attribute.pszName);

    if (getter)
    {
        if (DIPPER_TYPE(pNd->u.Attribute.pNdTypeSpec))
            pdflags |= (XPT_PD_RETVAL | XPT_PD_IN | XPT_PD_DIPPER);
        else
            pdflags |= (XPT_PD_RETVAL | XPT_PD_OUT);
        
    }
    else
        pdflags |= XPT_PD_IN;

    int rc = fill_pd_from_type(pThis, meth->params, pdflags, pNd->u.Attribute.pNdTypeSpec);
    if (RT_FAILURE(rc))
        return rc; /* error info already set. */

    fill_pd_as_nsresult(meth->result);
    NEXT_METH(pThis)++;
    return VINF_SUCCESS;
}


static int xpidlTypelibProcessAttr(PXPIDLTYPELIBSTATE pThis, PCXPIDLNODE pNd)
{
    XPTInterfaceDescriptor *id = CURRENT(pThis);
    XPTMethodDescriptor *meth;

    /* If it's marked [noscript], mark it as hidden in the typelib. */
    bool hidden = (xpidlNodeAttrFind(pNd, "noscript") != NULL);

    int rc = verify_attribute_declaration(pNd, pThis->pErrInfo);
    if (RT_FAILURE(rc))
        return rc;

    if (!XPT_InterfaceDescriptorAddMethods(ARENA(pThis), id, 
                                           (PRUint16) (pNd->u.Attribute.fReadonly ? 1 : 2)))
        return VERR_NO_MEMORY;

    meth = &id->method_descriptors[NEXT_METH(pThis)];

    rc = typelib_attr_accessor(pThis, pNd, meth, true, hidden);
    if (RT_FAILURE(rc))
        return rc;

    if (!pNd->u.Attribute.fReadonly)
    {
        rc = typelib_attr_accessor(pThis, pNd, meth + 1, false, hidden);
        if (RT_FAILURE(rc))
            return rc;
    }

    return VINF_SUCCESS;
}


static int xpidlTypelibProcessMethod(PXPIDLTYPELIBSTATE pThis, PCXPIDLNODE pNd)
{
    XPTInterfaceDescriptor *id = CURRENT(pThis);
    XPTMethodDescriptor *meth;
    uint16 num_args = 0;
    uint8 op_flags = 0;
    bool op_notxpcom = (xpidlNodeAttrFind(pNd, "notxpcom") != NULL);
    bool op_noscript = (xpidlNodeAttrFind(pNd, "noscript") != NULL);

    int rc = verify_method_declaration(pNd, pThis->pErrInfo);
    if (RT_FAILURE(rc))
        return rc;

    if (!XPT_InterfaceDescriptorAddMethods(ARENA(pThis), id, 1))
        return VERR_NO_MEMORY;

    meth = &id->method_descriptors[NEXT_METH(pThis)];

    /* count params */
    PCXPIDLNODE pIt;
    RTListForEach(&pNd->u.Method.LstParams, pIt, XPIDLNODE, NdLst)
    {
        num_args++;
    }
    if (   !op_notxpcom
        && pNd->u.Method.pNdTypeSpecRet
        && (   pNd->u.Method.pNdTypeSpecRet->enmType != kXpidlNdType_BaseType
            || pNd->u.Method.pNdTypeSpecRet->u.enmBaseType != kXpidlType_Void))
        num_args++;             /* fake param for _retval */

    if (op_noscript)
        op_flags |= XPT_MD_HIDDEN;
    if (op_notxpcom)
        op_flags |= XPT_MD_NOTXPCOM;

    /* XXXshaver constructor? */

#ifdef DEBUG_shaver_method
    fprintf(stdout, "DBG: adding method %s (nargs %d)\n",
            pNd->u.Method.pszName, num_args);
#endif
    if (!XPT_FillMethodDescriptor(ARENA(pThis), meth, op_flags, 
                                  (char *)pNd->u.Method.pszName,
                                  (uint8) num_args))
        return VERR_INVALID_PARAMETER;

    num_args = 0;
    RTListForEach(&pNd->u.Method.LstParams, pIt, XPIDLNODE, NdLst)
    {
        XPTParamDescriptor *pd = &meth->params[num_args++];
        rc = fill_pd_from_param(pThis, pd, pIt);
        if (RT_FAILURE(rc))
            return rc;
    }

    /* XXX unless [notxpcom] */
    if (!op_notxpcom)
    {
        if (   pNd->u.Method.pNdTypeSpecRet
            && (   pNd->u.Method.pNdTypeSpecRet->enmType != kXpidlNdType_BaseType
                || pNd->u.Method.pNdTypeSpecRet->u.enmBaseType != kXpidlType_Void))
        {
            uint8 pdflags = DIPPER_TYPE(pNd) ?
                                (XPT_PD_RETVAL | XPT_PD_IN | XPT_PD_DIPPER) :
                                (XPT_PD_RETVAL | XPT_PD_OUT);
    
            rc = fill_pd_from_type(pThis, &meth->params[num_args], pdflags, pNd->u.Method.pNdTypeSpecRet);
            if (RT_FAILURE(rc))
                return rc;
        }

        rc = fill_pd_as_nsresult(meth->result);
        if (RT_FAILURE(rc))
            return rc;
    }
    else
    {
#ifdef DEBUG_shaver_notxpcom
        fprintf(stderr, "%s is notxpcom\n", pNd->u.Method.pszName);
#endif
        rc = fill_pd_from_type(pThis, meth->result, XPT_PD_RETVAL,
                               pNd->u.Method.pNdTypeSpecRet);
        if (RT_FAILURE(rc))
            return rc;
    }
    NEXT_METH(pThis)++;
    return VINF_SUCCESS;
}


static int xpidlTypelibProcessConst(PXPIDLTYPELIBSTATE pThis, PCXPIDLNODE pNd)
{
    bool is_long;
    XPTInterfaceDescriptor *id;
    XPTConstDescriptor *cd;

    int rc = verify_const_declaration(pNd, pThis->pErrInfo);
    if (RT_FAILURE(rc))
        return rc;

    /* Could be a typedef; try to map it to the real type. */
    PCXPIDLNODE pNdRealType = find_underlying_type(pNd->u.Const.pNdTypeSpec);
    Assert(pNdRealType->enmType == kXpidlNdType_BaseType);
    pNdRealType = pNdRealType ? pNdRealType : pNd->u.Const.pNdTypeSpec;
    Assert(   pNdRealType->u.enmBaseType == kXpidlType_Long
           || pNdRealType->u.enmBaseType == kXpidlType_Short
           || pNdRealType->u.enmBaseType == kXpidlType_Unsigned_Long
           || pNdRealType->u.enmBaseType == kXpidlType_Unsigned_Short);
    is_long =    pNdRealType->u.enmBaseType == kXpidlType_Long
              || pNdRealType->u.enmBaseType == kXpidlType_Unsigned_Long;

    id = CURRENT(pThis);
    if (!XPT_InterfaceDescriptorAddConsts(ARENA(pThis), id, 1))
        return VERR_NO_MEMORY;
    cd = &id->const_descriptors[NEXT_CONST(pThis)];
    
    cd->name = (char *)pNd->u.Const.pszName;
#ifdef DEBUG_shaver_const
    fprintf(stderr, "DBG: adding const %s\n", cd->name);
#endif
    rc = fill_td_from_type(pThis, &cd->type, pNd->u.Const.pNdTypeSpec);
    if (RT_FAILURE(rc))
        return rc;
    
    if (is_long)
        cd->value.ui32 = (uint32_t)pNd->u.Const.u64Const;
    else
        cd->value.ui16 = (uint16_t)pNd->u.Const.u64Const;

    NEXT_CONST(pThis)++;
    return VINF_SUCCESS;
}


static int xpidlTypelibProcessIf(PXPIDLTYPELIBSTATE pThis, PCXPIDLNODE pNd)
{
    char *name = (char *)pNd->u.If.pszIfName;
    XPTInterfaceDirectoryEntry *ide;
    XPTInterfaceDescriptor *id;
    uint16_t parent_id = 0;
    PRUint8 interface_flags = 0;

    int rc = verify_interface_declaration(pNd, pThis->pErrInfo);
    if (RT_FAILURE(rc))
        return rc;

    if (xpidlNodeAttrFind(pNd, "scriptable"))
        interface_flags |= XPT_ID_SCRIPTABLE;

    if (xpidlNodeAttrFind(pNd, "function"))
        interface_flags |= XPT_ID_FUNCTION;

    ide = FindInterfaceByName(HEADER(pThis)->interface_directory,
                              HEADER(pThis)->num_interfaces, name,
                              NULL);
    if (!ide)
        return xpidlIdlError(pThis->pErrInfo, NULL /*pNd*/, VERR_NOT_FOUND,
                             "ERROR: didn't find interface %s in IDE block. Giving up.\n",
                             name);

    if (pNd->u.If.pszIfInherit)
    {
        if (!FindInterfaceByName(HEADER(pThis)->interface_directory,
                                 HEADER(pThis)->num_interfaces, pNd->u.If.pszIfInherit,
                                 &parent_id))
            return xpidlIdlError(pThis->pErrInfo, NULL /*pNd*/, VERR_NOT_FOUND,
                                 "ERROR: no index found for %s. Giving up.\n",
                                 pNd->u.If.pszIfInherit);
    }

    id = XPT_NewInterfaceDescriptor(ARENA(pThis), parent_id, 0, 0, 
                                    interface_flags);
    if (!id)
        return VERR_NO_MEMORY;

    CURRENT(pThis) = ide->interface_descriptor = id;
#ifdef DEBUG_shaver_ifaces
    fprintf(stderr, "DBG: starting interface %s @ %p\n", name, id);
#endif

    NEXT_METH(pThis) = 0;
    NEXT_CONST(pThis) = 0;
    NEXT_TYPE(pThis) = 0;

    /* Walk the children and process. */
    PCXPIDLNODE pIt;
    RTListForEach(&pNd->u.If.LstBody, pIt, XPIDLNODE, NdLst)
    {
        switch (pIt->enmType)
        {
            case kXpidlNdType_Attribute:
                rc = xpidlTypelibProcessAttr(pThis, pIt);
                break;
            case kXpidlNdType_Method:
                rc = xpidlTypelibProcessMethod(pThis, pIt);
                break;
            case kXpidlNdType_Const:
                rc = xpidlTypelibProcessConst(pThis, pIt);
                break;
            case kXpidlNdType_RawBlock:
                break;
            default:
                AssertReleaseFailed();
                break;
        }
        if (RT_FAILURE(rc))
            return rc;
    }

#ifdef DEBUG_shaver_ifaces
    fprintf(stderr, "DBG: ending interface %s\n", name);
#endif
    return VINF_SUCCESS;
}


DECL_HIDDEN_CALLBACK(int) xpidl_typelib_dispatch(FILE *pFile, PCXPIDLINPUT pInput, PCXPIDLPARSE pParse, PRTERRINFO pErrInfo)
{
    XPIDLTYPELIBSTATE This; RT_ZERO(This);
    This.pErrInfo = pErrInfo;
    int rc = typelib_prolog(&This, pInput, pParse);
    if (RT_SUCCESS(rc))
    {
        PCXPIDLNODE pIt;
        RTListForEach(&pParse->LstNodes, pIt, XPIDLNODE, NdLst)
        {
            if (pIt->pInput != pInput)
                continue;

            switch (pIt->enmType)
            {
                case kXpidlNdType_Native:
                    rc = check_native(pIt, pErrInfo);
                    break;
                case kXpidlNdType_Interface_Def:
                    rc = xpidlTypelibProcessIf(&This, pIt);
                    break;
                case kXpidlNdType_Interface_Forward_Decl: /* Ignored */
                case kXpidlNdType_Typedef:
                case kXpidlNdType_RawBlock:
                    break;
                default:
                    AssertReleaseFailed();
                    break;
            }
            if (RT_FAILURE(rc))
                break;
        }

        if (RT_SUCCESS(rc))
            rc = typelib_epilog(&This, pFile, pInput);
    }

    return rc;
}
