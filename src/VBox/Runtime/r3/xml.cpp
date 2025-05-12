/* $Id: xml.cpp 109500 2025-05-12 12:30:04Z vadim.galitsyn@oracle.com $ */
/** @file
 * IPRT - XML Manipulation API.
 *
 * @note Not available in no-CRT mode because it relies too heavily on
 *       exceptions, as well as using std::list and std::map.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/cpp/xml.h>

#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/cpp/lock.h>

#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/globals.h>
#include <libxml/xmlIO.h>
#include <libxml/xmlsave.h>
#include <libxml/uri.h>

#include <libxml/xmlschemas.h>

#include <map>


/*********************************************************************************************************************************
*   Strict / debug helpers                                                                                                       *
*********************************************************************************************************************************/
#ifdef DEBUG_andy
/** Enable the following to get strict assertions if the XML got some invalid values for attributes. */
# define VBOX_XML_STRICT_ASSERT_FAILED { AssertMsgFailed(("Invalid value '%s' for %s/@%s\n", pcsz, m_pcszName, pcszMatch)); }
#else
# define VBOX_XML_STRICT_ASSERT_FAILED void(0)
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Global module initialization structure. This is to wrap non-reentrant bits
 * of libxml, among other things.
 *
 * The constructor and destructor of this structure are used to perform global
 * module initialization and cleanup. There must be only one global variable of
 * this structure.
 */
static class Global
{
public:

    Global()
    {
        /* Check the parser version. The docs say it will kill the app if
         * there is a serious version mismatch, but I couldn't find it in the
         * source code (it only prints the error/warning message to the console) so
         * let's leave it as is for informational purposes. */
        LIBXML_TEST_VERSION

        /* Init libxml */
        xmlInitParser();

        /* Save the default entity resolver before someone has replaced it */
        sxml.defaultEntityLoader = xmlGetExternalEntityLoader();
    }

    ~Global()
    {
        /* Shutdown libxml */
        xmlCleanupParser();
    }

    struct
    {
        xmlExternalEntityLoader defaultEntityLoader;

        /** Used to provide some thread safety missing in libxml2 (see e.g.
         *  XmlTreeBackend::read()) */
        RTCLockMtx lock;
    }
    sxml;  /* XXX naming this xml will break with gcc-3.3 */
} gGlobal;



namespace xml
{

////////////////////////////////////////////////////////////////////////////////
//
// Exceptions
//
////////////////////////////////////////////////////////////////////////////////

LogicError::LogicError(RT_SRC_POS_DECL)
    : RTCError(NULL)
{
    char *msg = NULL;
    RTStrAPrintf(&msg, "In '%s', '%s' at #%d",
                 pszFunction, pszFile, iLine);
    setWhat(msg);
    RTStrFree(msg);
}

XmlError::XmlError(xmlErrorPtr aErr)
{
    if (!aErr)
        throw EInvalidArg(RT_SRC_POS);

    char *msg = Format(aErr);
    setWhat(msg);
    RTStrFree(msg);
}

/**
 * Composes a single message for the given error. The caller must free the
 * returned string using RTStrFree() when no more necessary.
 */
/* static */ char *XmlError::Format(xmlErrorPtr aErr)
{
    const char *msg = aErr->message ? aErr->message : "<none>";
    size_t msgLen = strlen(msg);
    /* strip spaces, trailing EOLs and dot-like char */
    while (msgLen && strchr(" \n.?!", msg [msgLen - 1]))
        --msgLen;

    char *finalMsg = NULL;
    RTStrAPrintf(&finalMsg, "%.*s.\nLocation: '%s', line %d (%d), column %d",
                 msgLen, msg, aErr->file, aErr->line, aErr->int1, aErr->int2);

    return finalMsg;
}

EIPRTFailure::EIPRTFailure(int aRC, const char *pszContextFmt, ...)
    : RuntimeError(NULL)
    , mRC(aRC)
{
    va_list va;
    va_start(va, pszContextFmt);
    m_strMsg.printfVNoThrow(pszContextFmt, va);
    va_end(va);
    m_strMsg.appendPrintfNoThrow(" %Rrc (%Rrs)", aRC, aRC);
}

////////////////////////////////////////////////////////////////////////////////
//
// File Class
//
//////////////////////////////////////////////////////////////////////////////

struct File::Data
{
    Data(RTFILE a_hHandle, const char *a_pszFilename, bool a_fFlushOnClose)
        : strFileName(a_pszFilename)
        , handle(a_hHandle)
        , opened(a_hHandle != NIL_RTFILE)
        , flushOnClose(a_fFlushOnClose)
    { }

    ~Data()
    {
        if (flushOnClose)
        {
            RTFileFlush(handle);
            if (!strFileName.isEmpty())
                RTDirFlushParent(strFileName.c_str());
        }

        if (opened)
        {
            RTFileClose(handle);
            handle = NIL_RTFILE;
            opened = false;
        }
    }

    RTCString strFileName;
    RTFILE handle;
    bool opened : 1;
    bool flushOnClose : 1;
};

File::File(Mode aMode, const char *aFileName, bool aFlushIt /* = false */)
    : m(NULL)
{
    /* Try open the file first, as the destructor will not be invoked if we throw anything from here. For details see:
       https://stackoverflow.com/questions/9971782/destructor-not-invoked-when-an-exception-is-thrown-in-the-constructor */
    uint32_t flags = 0;
    const char *pcszMode = "???";
    switch (aMode)
    {
        /** @todo change to RTFILE_O_DENY_WRITE where appropriate. */
        case Mode_Read:
            flags = RTFILE_O_READ      | RTFILE_O_OPEN           | RTFILE_O_DENY_NONE;
            pcszMode = "reading";
            break;
        case Mode_WriteCreate:      // fail if file exists
            flags = RTFILE_O_WRITE     | RTFILE_O_CREATE         | RTFILE_O_DENY_NONE;
            pcszMode = "writing";
            break;
        case Mode_Overwrite:        // overwrite if file exists
            flags = RTFILE_O_WRITE     | RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_NONE;
            pcszMode = "overwriting";
            break;
        case Mode_ReadWrite:
            flags = RTFILE_O_READWRITE | RTFILE_O_OPEN           | RTFILE_O_DENY_NONE;
            pcszMode = "reading/writing";
            break;
    }
    RTFILE hFile = NIL_RTFILE;
    int vrc = RTFileOpen(&hFile, aFileName, flags);
    if (RT_FAILURE(vrc))
        throw EIPRTFailure(vrc, "Runtime error opening '%s' for %s", aFileName, pcszMode);

    /* Now we can create the data and stuff: */
    try
    {
        m = new Data(hFile, aFileName, aFlushIt && (flags & RTFILE_O_ACCESS_MASK) != RTFILE_O_READ);
    }
    catch (std::bad_alloc &)
    {
        RTFileClose(hFile);
        throw;
    }
}

File::File(RTFILE aHandle, const char *aFileName /* = NULL */, bool aFlushIt /* = false */)
    : m(NULL)
{
    if (aHandle == NIL_RTFILE)
        throw EInvalidArg(RT_SRC_POS);

    m = new Data(aHandle, aFileName, aFlushIt);

    setPos(0);
}

File::~File()
{
    if (m)
    {
        delete m;
        m = NULL;
    }
}

const char *File::uri() const
{
    return m->strFileName.c_str();
}

uint64_t File::pos() const
{
    uint64_t p = 0;
    int vrc = RTFileSeek(m->handle, 0, RTFILE_SEEK_CURRENT, &p);
    if (RT_SUCCESS(vrc))
        return p;

    throw EIPRTFailure(vrc, "Runtime error seeking in file '%s'", m->strFileName.c_str());
}

void File::setPos(uint64_t aPos)
{
    uint64_t p = 0;
    unsigned method = RTFILE_SEEK_BEGIN;
    int vrc = VINF_SUCCESS;

    /* check if we overflow int64_t and move to INT64_MAX first */
    if ((int64_t)aPos < 0)
    {
        vrc = RTFileSeek(m->handle, INT64_MAX, method, &p);
        aPos -= (uint64_t)INT64_MAX;
        method = RTFILE_SEEK_CURRENT;
    }
    /* seek the rest */
    if (RT_SUCCESS(vrc))
        vrc = RTFileSeek(m->handle, (int64_t) aPos, method, &p);
    if (RT_SUCCESS(vrc))
        return;

    throw EIPRTFailure(vrc, "Runtime error seeking in file '%s'", m->strFileName.c_str());
}

int File::read(char *aBuf, int aLen)
{
    size_t len = aLen;
    int vrc = RTFileRead(m->handle, aBuf, len, &len);
    if (RT_SUCCESS(vrc))
        return (int)len;

    throw EIPRTFailure(vrc, "Runtime error reading from file '%s'", m->strFileName.c_str());
}

int File::write(const char *aBuf, int aLen)
{
    size_t len = aLen;
    int vrc = RTFileWrite(m->handle, aBuf, len, &len);
    if (RT_SUCCESS(vrc))
        return (int)len;

    throw EIPRTFailure(vrc, "Runtime error writing to file '%s'", m->strFileName.c_str());
}

void File::truncate()
{
    int vrc = RTFileSetSize(m->handle, pos());
    if (RT_SUCCESS(vrc))
        return;

    throw EIPRTFailure(vrc, "Runtime error truncating file '%s'", m->strFileName.c_str());
}

////////////////////////////////////////////////////////////////////////////////
//
// MemoryBuf Class
//
//////////////////////////////////////////////////////////////////////////////

struct MemoryBuf::Data
{
    Data()
        : buf(NULL), len(0), uri(NULL), pos(0) {}

    const char *buf;
    size_t len;
    char *uri;

    size_t pos;
};

MemoryBuf::MemoryBuf(const char *aBuf, size_t aLen, const char *aURI /* = NULL */)
    : m(new Data())
{
    if (aBuf == NULL)
        throw EInvalidArg(RT_SRC_POS);

    m->buf = aBuf;
    m->len = aLen;
    m->uri = RTStrDup(aURI);
}

MemoryBuf::~MemoryBuf()
{
    RTStrFree(m->uri);
}

const char *MemoryBuf::uri() const
{
    return m->uri;
}

uint64_t MemoryBuf::pos() const
{
    return m->pos;
}

void MemoryBuf::setPos(uint64_t aPos)
{
    size_t off = (size_t)aPos;
    if ((uint64_t) off != aPos)
        throw EInvalidArg();

    if (off > m->len)
        throw EInvalidArg();

    m->pos = off;
}

int MemoryBuf::read(char *aBuf, int aLen)
{
    if (m->pos >= m->len)
        return 0 /* nothing to read */;

    size_t len = m->pos + aLen < m->len ? aLen : m->len - m->pos;
    memcpy(aBuf, m->buf + m->pos, len);
    m->pos += len;

    return (int)len;
}

////////////////////////////////////////////////////////////////////////////////
//
// GlobalLock class
//
////////////////////////////////////////////////////////////////////////////////

struct GlobalLock::Data
{
    PFNEXTERNALENTITYLOADER pfnOldLoader;
    RTCLock lock;

    Data()
        : pfnOldLoader(NULL)
        , lock(gGlobal.sxml.lock)
    {
    }
};

GlobalLock::GlobalLock()
    : m(new Data())
{
}

GlobalLock::~GlobalLock()
{
    if (m->pfnOldLoader)
        xmlSetExternalEntityLoader(m->pfnOldLoader);
    delete m;
    m = NULL;
}

void GlobalLock::setExternalEntityLoader(PFNEXTERNALENTITYLOADER pfnLoader)
{
    m->pfnOldLoader = (PFNEXTERNALENTITYLOADER)xmlGetExternalEntityLoader();
    xmlSetExternalEntityLoader(pfnLoader);
}

// static
xmlParserInput* GlobalLock::callDefaultLoader(const char *aURI,
                                              const char *aID,
                                              xmlParserCtxt *aCtxt)
{
    return gGlobal.sxml.defaultEntityLoader(aURI, aID, aCtxt);
}



////////////////////////////////////////////////////////////////////////////////
//
// Node class
//
////////////////////////////////////////////////////////////////////////////////

Node::Node(EnumType type,
           Node *pParent,
           PRTLISTANCHOR pListAnchor,
           xmlNode *pLibNode,
           xmlAttr *pLibAttr)
    : m_Type(type)
    , m_pParent(pParent)
    , m_pLibNode(pLibNode)
    , m_pLibAttr(pLibAttr)
    , m_pcszNamespacePrefix(NULL)
    , m_pcszNamespaceHref(NULL)
    , m_pcszName(NULL)
    , m_pParentListAnchor(pListAnchor)
{
    RTListInit(&m_listEntry);
}

Node::~Node()
{
}

/**
 * Returns the name of the node, which is either the element name or
 * the attribute name. For other node types it probably returns NULL.
 * @return
 */
const char *Node::getName() const
{
    return m_pcszName;
}

/**
 * Returns the name of the node, which is either the element name or
 * the attribute name. For other node types it probably returns NULL.
 * @return
 */
const char *Node::getPrefix() const
{
    return m_pcszNamespacePrefix;
}

/**
 * Returns the XML namespace URI, which is the attribute name. For other node types it probably
 * returns NULL.
 * @return
 */
const char *Node::getNamespaceURI() const
{
    return m_pcszNamespaceHref;
}

/**
 * Variant of nameEquals that checks the namespace as well.
 * @param pcszNamespace
 * @param pcsz
 * @return
 */
bool Node::nameEqualsNS(const char *pcszNamespace, const char *pcsz) const
{
    if (m_pcszName == pcsz)
        return true;
    if (m_pcszName == NULL)
        return false;
    if (pcsz == NULL)
        return false;
    if (strcmp(m_pcszName, pcsz))
        return false;

    // name matches: then check namespaces as well
    if (!pcszNamespace)
        return true;
    // caller wants namespace:
    if (!m_pcszNamespacePrefix)
        // but node has no namespace:
        return false;
    return !strcmp(m_pcszNamespacePrefix, pcszNamespace);
}

/**
 * Variant of nameEquals that checks the namespace as well.
 *
 * @returns true if equal, false if not.
 * @param   pcsz            The element name.
 * @param   cchMax          The maximum number of character from @a pcsz to
 *                          match.
 * @param   pcszNamespace   The name space prefix or NULL (default).
 */
bool Node::nameEqualsN(const char *pcsz, size_t cchMax, const char *pcszNamespace /* = NULL*/) const
{
    /* Match the name. */
    if (!m_pcszName)
        return false;
    if (!pcsz || cchMax == 0)
        return false;
    if (strncmp(m_pcszName, pcsz, cchMax))
        return false;
    if (strlen(m_pcszName) > cchMax)
        return false;

    /* Match name space. */
    if (!pcszNamespace)
        return true;    /* NULL, anything goes. */
    if (!m_pcszNamespacePrefix)
        return false;   /* Element has no namespace. */
    return !strcmp(m_pcszNamespacePrefix, pcszNamespace);
}

/**
 * Returns the value of a node. If this node is an attribute, returns
 * the attribute value; if this node is an element, then this returns
 * the element text content.
 * @return
 */
const char *Node::getValue() const
{
    if (   m_pLibAttr
        && m_pLibAttr->children
        )
        // libxml hides attribute values in another node created as a
        // single child of the attribute node, and it's in the content field
        return (const char *)m_pLibAttr->children->content;

    if (   m_pLibNode
        && m_pLibNode->children)
        return (const char *)m_pLibNode->children->content;

    return NULL;
}

/**
 * Returns the value of a node. If this node is an attribute, returns
 * the attribute value; if this node is an element, then this returns
 * the element text content.
 * @return
 * @param   cchValueLimit   If the length of the returned value exceeds this
 *                          limit a EIPRTFailure exception will be thrown.
 */
const char *Node::getValueN(size_t cchValueLimit) const
{
    if (   m_pLibAttr
        && m_pLibAttr->children
        )
    {
        // libxml hides attribute values in another node created as a
        // single child of the attribute node, and it's in the content field
        AssertStmt(strlen((const char *)m_pLibAttr->children->content) <= cchValueLimit, throw EIPRTFailure(VERR_BUFFER_OVERFLOW, "Attribute '%s' exceeds limit of %zu bytes", m_pcszName, cchValueLimit));
        return (const char *)m_pLibAttr->children->content;
    }

    if (   m_pLibNode
        && m_pLibNode->children)
    {
        AssertStmt(strlen((const char *)m_pLibNode->children->content) <= cchValueLimit, throw EIPRTFailure(VERR_BUFFER_OVERFLOW, "Element '%s' exceeds limit of %zu bytes", m_pcszName, cchValueLimit));
        return (const char *)m_pLibNode->children->content;
    }

    return NULL;
}

/**
 * Copies the value of a node into the given integer variable.
 * Returns TRUE only if a value was found and was actually an
 * integer of the given type.
 * @return
 */
bool Node::copyValue(int32_t &i) const
{
    const char *pcsz;
    if (    ((pcsz = getValue()))
         && (VINF_SUCCESS == RTStrToInt32Ex(pcsz, NULL, 10, &i))
       )
        return true;

    return false;
}

/**
 * Copies the value of a node into the given integer variable.
 * Returns TRUE only if a value was found and was actually an
 * integer of the given type.
 * @return
 */
bool Node::copyValue(uint32_t &i) const
{
    const char *pcsz;
    if (    ((pcsz = getValue()))
         && (VINF_SUCCESS == RTStrToUInt32Ex(pcsz, NULL, 10, &i))
       )
        return true;

    return false;
}

/**
 * Copies the value of a node into the given integer variable.
 * Returns TRUE only if a value was found and was actually an
 * integer of the given type.
 * @return
 */
bool Node::copyValue(int64_t &i) const
{
    const char *pcsz;
    if (    ((pcsz = getValue()))
         && (VINF_SUCCESS == RTStrToInt64Ex(pcsz, NULL, 10, &i))
       )
        return true;

    return false;
}

/**
 * Copies the value of a node into the given integer variable.
 * Returns TRUE only if a value was found and was actually an
 * integer of the given type.
 * @return
 */
bool Node::copyValue(uint64_t &i) const
{
    const char *pcsz;
    if (    ((pcsz = getValue()))
         && (VINF_SUCCESS == RTStrToUInt64Ex(pcsz, NULL, 10, &i))
       )
        return true;

    return false;
}

/**
 * Returns the line number of the current node in the source XML file.
 * Useful for error messages.
 * @return
 */
int Node::getLineNumber() const
{
    if (m_pLibAttr)
        return m_pParent->m_pLibNode->line;

    return m_pLibNode->line;
}

/**
 * Private element constructor.
 *
 * @param   pElmRoot    Pointer to the root element.
 * @param   pParent     Pointer to the parent element (always an ElementNode,
 *                      despite the type).  NULL for the root node.
 * @param   pListAnchor Pointer to the m_children member of the parent.  NULL
 *                      for the root node.
 * @param   pLibNode    Pointer to the libxml2 node structure.
 */
ElementNode::ElementNode(const ElementNode *pElmRoot,
                         Node *pParent,
                         PRTLISTANCHOR pListAnchor,
                         xmlNode *pLibNode)
    : Node(IsElement,
           pParent,
           pListAnchor,
           pLibNode,
           NULL)
{
    m_pElmRoot = pElmRoot ? pElmRoot : this; // If NULL is passed, then this is the root element.
    m_pcszName = (const char *)pLibNode->name;

    if (pLibNode->ns)
    {
        m_pcszNamespacePrefix = (const char *)m_pLibNode->ns->prefix;
        m_pcszNamespaceHref = (const char *)m_pLibNode->ns->href;
    }

    RTListInit(&m_children);
    RTListInit(&m_attributes);
}

ElementNode::~ElementNode()
{
    Node *pCur, *pNext;
    RTListForEachSafeCpp(&m_children, pCur, pNext, Node, m_listEntry)
    {
        delete pCur;
    }
    RTListInit(&m_children);

    RTListForEachSafeCpp(&m_attributes, pCur, pNext, Node, m_listEntry)
    {
        delete pCur;
    }
    RTListInit(&m_attributes);
}


ElementNode const *ElementNode::getNextTreeElement(ElementNode const *pElmRoot /*= NULL */) const
{
    /*
     * Consider children first.
     */
    ElementNode const *pChild = getFirstChildElement();
    if (pChild)
        return pChild;

    /*
     * Then siblings, aunts and uncles.
     */
    ElementNode const *pCur = this;
    do
    {
        ElementNode const *pSibling = pCur->getNextSibilingElement();
        if (pSibling != NULL)
            return pSibling;

        pCur = static_cast<const xml::ElementNode *>(pCur->m_pParent);
        Assert(pCur || pCur == pElmRoot);
    } while (pCur != pElmRoot);

    return NULL;
}


/**
 * Private implementation.
 *
 * @param   pElmRoot        The root element.
 */
/*static*/ void ElementNode::buildChildren(ElementNode *pElmRoot)       // protected
{
    for (ElementNode *pCur = pElmRoot; pCur; pCur = pCur->getNextTreeElement(pElmRoot))
    {
        /*
         * Go thru this element's attributes creating AttributeNodes for them.
         */
        for (xmlAttr *pLibAttr = pCur->m_pLibNode->properties; pLibAttr; pLibAttr = pLibAttr->next)
        {
            AttributeNode *pNew = new AttributeNode(pElmRoot, pCur, &pCur->m_attributes, pLibAttr);
            RTListAppend(&pCur->m_attributes, &pNew->m_listEntry);
        }

        /*
         * Go thru this element's child elements (element and text nodes).
         */
        for (xmlNodePtr pLibNode = pCur->m_pLibNode->children; pLibNode; pLibNode = pLibNode->next)
        {
            Node *pNew;
            if (pLibNode->type == XML_ELEMENT_NODE)
                pNew = new ElementNode(pElmRoot, pCur, &pCur->m_children, pLibNode);
            else if (pLibNode->type == XML_TEXT_NODE)
                pNew = new ContentNode(pCur, &pCur->m_children, pLibNode);
            else
                continue;
            RTListAppend(&pCur->m_children, &pNew->m_listEntry);
        }
    }
}


/**
 * Builds a list of direct child elements of the current element that
 * match the given string; if pcszMatch is NULL, all direct child
 * elements are returned.
 * @param children out: list of nodes to which children will be appended.
 * @param pcszMatch in: match string, or NULL to return all children.
 * @return Number of items appended to the list (0 if none).
 */
int ElementNode::getChildElements(ElementNodesList &children,
                                  const char *pcszMatch /*= NULL*/)
    const
{
    int i = 0;
    Node *p;
    RTListForEachCpp(&m_children, p, Node, m_listEntry)
    {
        // export this child node if ...
        if (p->isElement())
            if (   !pcszMatch                       // ... the caller wants all nodes or ...
                || !strcmp(pcszMatch, p->getName()) // ... the element name matches.
               )
            {
                children.push_back(static_cast<ElementNode *>(p));
                ++i;
            }
    }
    return i;
}

/**
 * Returns the first child element whose name matches pcszMatch.
 *
 * @param pcszNamespace Namespace prefix (e.g. "vbox") or NULL to match any namespace.
 * @param pcszMatch Element name to match.
 * @return
 */
const ElementNode *ElementNode::findChildElementNS(const char *pcszNamespace, const char *pcszMatch) const
{
    Node *p;
    RTListForEachCpp(&m_children, p, Node, m_listEntry)
    {
        if (p->isElement())
        {
            ElementNode *pelm = static_cast<ElementNode*>(p);
            if (pelm->nameEqualsNS(pcszNamespace, pcszMatch))
                return pelm;
        }
    }
    return NULL;
}

/**
 * Returns the first child element whose "id" attribute matches pcszId.
 * @param pcszId identifier to look for.
 * @return child element or NULL if not found.
 */
const ElementNode *ElementNode::findChildElementFromId(const char *pcszId) const
{
    const Node *p;
    RTListForEachCpp(&m_children, p, Node, m_listEntry)
    {
        if (p->isElement())
        {
            const ElementNode   *pElm  = static_cast<const ElementNode *>(p);
            const AttributeNode *pAttr = pElm->findAttribute("id");
            if (pAttr && !strcmp(pAttr->getValue(), pcszId))
                return pElm;
        }
    }
    return NULL;
}

const ElementNode *ElementNode::findChildElementP(const char *pcszPath, const char *pcszNamespace /*= NULL*/) const
{
    const char *pszElm   = strchr(pcszPath, '/');
    size_t const cchThis = pszElm ? pszElm - pcszPath : 0;
    if (!cchThis)
        return findChildElementNS(pcszNamespace, pcszPath);

    /** @todo Can be done without recursion as we have both sibling lists and parent
     *        pointers in this variant.  */
    const Node *p;
    RTListForEachCpp(&m_children, p, Node, m_listEntry)
    {
        if (p->isElement())
        {
            const ElementNode *pElm = static_cast<const ElementNode *>(p);
            if (pElm->nameEqualsN(pcszPath, cchThis, pcszNamespace))
            {
                pElm = findChildElementP(pcszPath + cchThis, pcszNamespace);
                if (pElm)
                    return pElm;
            }
        }
    }

    return NULL;
}

const ElementNode *ElementNode::getFirstChildElement() const
{
    const Node *p;
    RTListForEachCpp(&m_children, p, Node, m_listEntry)
    {
        if (p->isElement())
            return static_cast<const ElementNode *>(p);
    }
    return NULL;
}

const ElementNode *ElementNode::getLastChildElement() const
{
    const Node *p;
    RTListForEachReverseCpp(&m_children, p, Node, m_listEntry)
    {
        if (p->isElement())
            return static_cast<const ElementNode *>(p);
    }
    return NULL;
}

const ElementNode *ElementNode::getPrevSibilingElement() const
{
    if (!m_pParent)
        return NULL;
    const Node *pSibling = this;
    for (;;)
    {
        pSibling = RTListGetPrevCpp(m_pParentListAnchor, pSibling, const Node, m_listEntry);
        if (!pSibling)
            return NULL;
        if (pSibling->isElement())
            return static_cast<const ElementNode *>(pSibling);
    }
}

const ElementNode *ElementNode::getNextSibilingElement() const
{
    if (!m_pParent)
        return NULL;
    const Node *pSibling = this;
    for (;;)
    {
        pSibling = RTListGetNextCpp(m_pParentListAnchor, pSibling, const Node, m_listEntry);
        if (!pSibling)
            return NULL;
        if (pSibling->isElement())
            return static_cast<const ElementNode *>(pSibling);
    }
}

const ElementNode *ElementNode::findPrevSibilingElement(const char *pcszMatch, const char *pcszNamespace /*= NULL*/) const
{
    if (!m_pParent)
        return NULL;
    const Node *pSibling = this;
    for (;;)
    {
        pSibling = RTListGetPrevCpp(m_pParentListAnchor, pSibling, const Node, m_listEntry);
        if (!pSibling)
            return NULL;
        if (pSibling->isElement())
        {
            const ElementNode *pElem = static_cast<const ElementNode *>(pSibling);
            if (pElem->nameEqualsNS(pcszNamespace, pcszMatch))
                return pElem;
        }
    }
}

const ElementNode *ElementNode::findNextSibilingElement(const char *pcszMatch, const char *pcszNamespace /*= NULL*/) const
{
    if (!m_pParent)
        return NULL;
    const Node *pSibling = this;
    for (;;)
    {
        pSibling = RTListGetNextCpp(m_pParentListAnchor, pSibling, const Node, m_listEntry);
        if (!pSibling)
            return NULL;
        if (pSibling->isElement())
        {
            const ElementNode *pElem = static_cast<const ElementNode *>(pSibling);
            if (pElem->nameEqualsNS(pcszNamespace, pcszMatch))
                return pElem;
        }
    }
}


/**
 * Looks up the given attribute node in this element's attribute map.
 *
 * @param   pcszMatch       The name of the attribute to find.
 * @param   pcszNamespace   The attribute name space prefix or NULL.
 */
const AttributeNode *ElementNode::findAttribute(const char *pcszMatch, const char *pcszNamespace /*= NULL*/) const
{
    AttributeNode *p;
    RTListForEachCpp(&m_attributes, p, AttributeNode, m_listEntry)
    {
        if (p->nameEqualsNS(pcszNamespace, pcszMatch))
            return p;
    }
    return NULL;
}

/**
 * Convenience method which attempts to find the attribute with the given
 * name and returns its value as a string.
 *
 * @param   pcszMatch       Name of attribute to find.
 * @param   ppcsz           Where to return the attribute.
 * @param   pcszNamespace   The attribute name space prefix or NULL.
 * @returns Boolean success indicator.
 */
bool ElementNode::getAttributeValue(const char *pcszMatch, const char **ppcsz, const char *pcszNamespace /*= NULL*/) const
{
    const AttributeNode *pAttr = findAttribute(pcszMatch, pcszNamespace);
    if (pAttr)
    {
        *ppcsz = pAttr->getValue();
        return true;
    }
    return false;
}

/**
 * Convenience method which attempts to find the attribute with the given
 * name and returns its value as a string.
 *
 * @param   pcszMatch       Name of attribute to find.
 * @param   pStr            Pointer to the string object that should receive the
 *                          attribute value.
 * @param   pcszNamespace   The attribute name space prefix or NULL.
 * @returns Boolean success indicator.
 *
 * @throws  Whatever the string class may throw on assignment.
 */
bool ElementNode::getAttributeValue(const char *pcszMatch, RTCString *pStr, const char *pcszNamespace /*= NULL*/) const
{
    const AttributeNode *pAttr = findAttribute(pcszMatch, pcszNamespace);
    if (pAttr)
    {
        *pStr = pAttr->getValue();
        return true;
    }

    return false;
}

/**
 * Like getAttributeValue (ministring variant), but makes sure that all backslashes
 * are converted to forward slashes.
 *
 * @param   pcszMatch       Name of attribute to find.
 * @param   pStr            Pointer to the string object that should
 *                          receive the attribute path value.
 * @param   pcszNamespace   The attribute name space prefix or NULL.
 * @returns Boolean success indicator.
 */
bool ElementNode::getAttributeValuePath(const char *pcszMatch, RTCString *pStr, const char *pcszNamespace /*= NULL*/) const
{
    if (getAttributeValue(pcszMatch, pStr, pcszNamespace))
    {
        pStr->findReplace('\\', '/');
        return true;
    }

    return false;
}

/**
 * Convenience method which attempts to find the attribute with the given
 * name and returns its value as a signed 32-bit integer.
 *
 * @param   pcszMatch       Name of attribute to find.
 * @param   piValue         Where to return the value.
 * @param   pcszNamespace   The attribute name space prefix or NULL.
 * @returns Boolean success indicator.
 */
bool ElementNode::getAttributeValue(const char *pcszMatch, int32_t *piValue, const char *pcszNamespace /*= NULL*/) const
{
    const char *pcsz = findAttributeValue(pcszMatch, pcszNamespace);
    if (pcsz)
    {
        int rc = RTStrToInt32Ex(pcsz, NULL, 0, piValue);
        if (rc == VINF_SUCCESS)
            return true;
        VBOX_XML_STRICT_ASSERT_FAILED;
    }
    return false;
}

/**
 * Convenience method which attempts to find the attribute with the given
 * name and returns its value as an unsigned 32-bit integer.
 *
 * @param   pcszMatch       Name of attribute to find.
 * @param   puValue         Where to return the value.
 * @param   pcszNamespace   The attribute name space prefix or NULL.
 * @returns Boolean success indicator.
 */
bool ElementNode::getAttributeValue(const char *pcszMatch, uint32_t *puValue, const char *pcszNamespace /*= NULL*/) const
{
    const char *pcsz = findAttributeValue(pcszMatch, pcszNamespace);
    if (pcsz)
    {
        int rc = RTStrToUInt32Ex(pcsz, NULL, 0, puValue);
        if (rc == VINF_SUCCESS)
            return true;
       VBOX_XML_STRICT_ASSERT_FAILED;
    }
    return false;
}

/**
 * Convenience method which attempts to find the attribute with the given
 * name and returns its value as a signed 64-bit integer.
 *
 * @param   pcszMatch       Name of attribute to find.
 * @param   piValue         Where to return the value.
 * @param   pcszNamespace   The attribute name space prefix or NULL.
 * @returns Boolean success indicator.
 */
bool ElementNode::getAttributeValue(const char *pcszMatch, int64_t *piValue, const char *pcszNamespace /*= NULL*/) const
{
    const char *pcsz = findAttributeValue(pcszMatch, pcszNamespace);
    if (pcsz)
    {
        int rc = RTStrToInt64Ex(pcsz, NULL, 0, piValue);
        if (rc == VINF_SUCCESS)
            return true;
        VBOX_XML_STRICT_ASSERT_FAILED;
    }
    return false;
}

/**
 * Convenience method which attempts to find the attribute with the given
 * name and returns its value as an unsigned 64-bit integer.
 *
 * @param   pcszMatch       Name of attribute to find.
 * @param   puValue         Where to return the value.
 * @param   pcszNamespace   The attribute name space prefix or NULL.
 * @returns Boolean success indicator.
 */
bool ElementNode::getAttributeValue(const char *pcszMatch, uint64_t *puValue, const char *pcszNamespace /*= NULL*/) const
{
    const char *pcsz = findAttributeValue(pcszMatch, pcszNamespace);
    if (pcsz)
    {
        int rc = RTStrToUInt64Ex(pcsz, NULL, 0, puValue);
        if (rc == VINF_SUCCESS)
            return true;
        VBOX_XML_STRICT_ASSERT_FAILED;
    }
    return false;
}

/**
 * Convenience method which attempts to find the attribute with the given
 * name and returns its value as a boolean. This accepts "true", "false",
 * "yes", "no", "1" or "0" as valid values.
 *
 * @param   pcszMatch       Name of attribute to find.
 * @param   pfValue         Where to return the value.
 * @param   pcszNamespace   The attribute name space prefix or NULL.
 * @returns Boolean success indicator.
 */
bool ElementNode::getAttributeValue(const char *pcszMatch, bool *pfValue, const char *pcszNamespace /*= NULL*/) const
{
    const char *pcsz = findAttributeValue(pcszMatch, pcszNamespace);
    if (pcsz)
    {
        if (   !strcmp(pcsz, "true")
            || !strcmp(pcsz, "yes")
            || !strcmp(pcsz, "1")
           )
        {
            *pfValue = true;
            return true;
        }
        if (   !strcmp(pcsz, "false")
            || !strcmp(pcsz, "no")
            || !strcmp(pcsz, "0")
           )
        {
            *pfValue = false;
            return true;
        }
        VBOX_XML_STRICT_ASSERT_FAILED;
    }
    return false;
}

/**
 * Convenience method which attempts to find the attribute with the given
 * name and returns its value as a string.
 *
 * @param   pcszMatch       Name of attribute to find.
 * @param   ppcsz           Where to return the attribute.
 * @param   cchValueLimit   If the length of the returned value exceeds this
 *                          limit a EIPRTFailure exception will be thrown.
 * @param   pcszNamespace   The attribute name space prefix or NULL.
 * @returns Boolean success indicator.
 */
bool ElementNode::getAttributeValueN(const char *pcszMatch, const char **ppcsz, size_t cchValueLimit, const char *pcszNamespace /*= NULL*/) const
{
    const AttributeNode *pAttr = findAttribute(pcszMatch, pcszNamespace);
    if (pAttr)
    {
        *ppcsz = pAttr->getValueN(cchValueLimit);
        return true;
    }
    return false;
}

/**
 * Convenience method which attempts to find the attribute with the given
 * name and returns its value as a string.
 *
 * @param   pcszMatch       Name of attribute to find.
 * @param   pStr            Pointer to the string object that should receive the
 *                          attribute value.
 * @param   cchValueLimit   If the length of the returned value exceeds this
 *                          limit a EIPRTFailure exception will be thrown.
 * @param   pcszNamespace   The attribute name space prefix or NULL.
 * @returns Boolean success indicator.
 *
 * @throws  Whatever the string class may throw on assignment.
 */
bool ElementNode::getAttributeValueN(const char *pcszMatch, RTCString *pStr, size_t cchValueLimit, const char *pcszNamespace /*= NULL*/) const
{
    const AttributeNode *pAttr = findAttribute(pcszMatch, pcszNamespace);
    if (pAttr)
    {
        *pStr = pAttr->getValueN(cchValueLimit);
        return true;
    }

    return false;
}

/**
 * Like getAttributeValue (ministring variant), but makes sure that all backslashes
 * are converted to forward slashes.
 *
 * @param   pcszMatch       Name of attribute to find.
 * @param   pStr            Pointer to the string object that should
 *                          receive the attribute path value.
 * @param   cchValueLimit   If the length of the returned value exceeds this
 *                          limit a EIPRTFailure exception will be thrown.
 * @param   pcszNamespace   The attribute name space prefix or NULL.
 * @returns Boolean success indicator.
 */
bool ElementNode::getAttributeValuePathN(const char *pcszMatch, RTCString *pStr, size_t cchValueLimit, const char *pcszNamespace /*= NULL*/) const
{
    if (getAttributeValueN(pcszMatch, pStr, cchValueLimit, pcszNamespace))
    {
        pStr->findReplace('\\', '/');
        return true;
    }

    return false;
}


bool ElementNode::getElementValue(int32_t *piValue) const
{
    const char *pszValue = getValue();
    if (pszValue)
    {
        int rc = RTStrToInt32Ex(pszValue, NULL, 0, piValue);
        if (rc == VINF_SUCCESS)
            return true;
    }
    return false;
}

bool ElementNode::getElementValue(uint32_t *puValue) const
{
    const char *pszValue = getValue();
    if (pszValue)
    {
        int rc = RTStrToUInt32Ex(pszValue, NULL, 0, puValue);
        if (rc == VINF_SUCCESS)
            return true;
    }
    return false;
}

bool ElementNode::getElementValue(int64_t *piValue) const
{
    const char *pszValue = getValue();
    if (pszValue)
    {
        int rc = RTStrToInt64Ex(pszValue, NULL, 0, piValue);
        if (rc == VINF_SUCCESS)
            return true;
    }
    return false;
}

bool ElementNode::getElementValue(uint64_t *puValue) const
{
    const char *pszValue = getValue();
    if (pszValue)
    {
        int rc = RTStrToUInt64Ex(pszValue, NULL, 0, puValue);
        if (rc == VINF_SUCCESS)
            return true;
    }
    return false;
}

bool ElementNode::getElementValue(bool *pfValue) const
{
    const char *pszValue = getValue();
    if (pszValue)
    {
        if (   !strcmp(pszValue, "true")
            || !strcmp(pszValue, "yes")
            || !strcmp(pszValue, "1")
           )
        {
            *pfValue = true;
            return true;
        }
        if (   !strcmp(pszValue, "false")
            || !strcmp(pszValue, "no")
            || !strcmp(pszValue, "0")
           )
        {
            *pfValue = true;
            return true;
        }
    }
    return false;
}


/**
 * Creates a new child element node and appends it to the list
 * of children in "this".
 *
 * @param pcszElementName
 * @return
 */
ElementNode *ElementNode::createChild(const char *pcszElementName)
{
    // we must be an element, not an attribute
    if (!m_pLibNode)
        throw ENodeIsNotElement(RT_SRC_POS);

    // libxml side: create new node
    xmlNode *pLibNode;
    if (!(pLibNode = xmlNewNode(NULL,        // namespace
                                (const xmlChar*)pcszElementName)))
        throw std::bad_alloc();
    xmlAddChild(m_pLibNode, pLibNode);

    // now wrap this in C++
    ElementNode *p = new ElementNode(m_pElmRoot, this, &m_children, pLibNode);
    RTListAppend(&m_children, &p->m_listEntry);

    return p;
}


/**
 * Creates a content node and appends it to the list of children
 * in "this".
 *
 * @param pcszContent
 * @return
 */
ContentNode *ElementNode::addContent(const char *pcszContent)
{
    // libxml side: create new node
    xmlNode *pLibNode = xmlNewText((const xmlChar*)pcszContent);
    if (!pLibNode)
        throw std::bad_alloc();
    xmlAddChild(m_pLibNode, pLibNode);

    // now wrap this in C++
    ContentNode *p = new ContentNode(this, &m_children, pLibNode);
    RTListAppend(&m_children, &p->m_listEntry);

    return p;
}

/**
 * Changes the contents of node and appends it to the list of
 * children
 *
 * @param pcszContent
 * @return
 */
ContentNode *ElementNode::setContent(const char *pcszContent)
{
//  1. Update content
    xmlNodeSetContent(m_pLibNode, (const xmlChar*)pcszContent);

//  2. Remove Content node from the list
    /* Check that the order is right. */
    xml::Node * pNode;
    RTListForEachCpp(&m_children, pNode, xml::Node, m_listEntry)
    {
        bool fLast = RTListNodeIsLast(&m_children, &pNode->m_listEntry);

        if (pNode->isContent())
        {
            RTListNodeRemove(&pNode->m_listEntry);
        }

        if (fLast)
            break;
    }

//  3. Create a new node and append to the list
    // now wrap this in C++
    ContentNode *pCNode = new ContentNode(this, &m_children, m_pLibNode);
    RTListAppend(&m_children, &pCNode->m_listEntry);

    return pCNode;
}

/**
 * Sets the given attribute; overloaded version for const char *.
 *
 * If an attribute with the given name exists, it is overwritten,
 * otherwise a new attribute is created. Returns the attribute node
 * that was either created or changed.
 *
 * @param   pcszName        The attribute name.
 * @param   pcszValue       The attribute value.
 * @return  Pointer to the attribute node that was created or modified.
 */
AttributeNode *ElementNode::setAttribute(const char *pcszName, const char *pcszValue)
{
    /*
     * Do we already have an attribute and should we just update it?
     */
    AttributeNode *pAttr;
    RTListForEachCpp(&m_attributes, pAttr, AttributeNode, m_listEntry)
    {
        if (pAttr->nameEquals(pcszName))
        {
            /* Overwrite existing libxml attribute node ... */
            xmlAttrPtr pLibAttr = xmlSetProp(m_pLibNode, (xmlChar *)pcszName, (xmlChar *)pcszValue);

            /* ... and update our C++ wrapper in case the attrib pointer changed. */
            pAttr->m_pLibAttr = pLibAttr;
            return pAttr;
        }
    }

    /*
     * No existing attribute, create a new one.
     */
    /* libxml side: xmlNewProp creates an attribute. */
    xmlAttr *pLibAttr = xmlNewProp(m_pLibNode, (xmlChar *)pcszName, (xmlChar *)pcszValue);

    /* C++ side: Create an attribute node around it. */
    pAttr = new AttributeNode(m_pElmRoot, this, &m_attributes, pLibAttr);
    RTListAppend(&m_attributes, &pAttr->m_listEntry);

    return pAttr;
}

/**
 * Like setAttribute (ministring variant), but replaces all backslashes with forward slashes
 * before calling that one.
 * @param pcszName
 * @param strValue
 * @return
 */
AttributeNode* ElementNode::setAttributePath(const char *pcszName, const RTCString &strValue)
{
    RTCString strTemp(strValue);
    strTemp.findReplace('\\', '/');
    return setAttribute(pcszName, strTemp.c_str());
}

/**
 * Sets the given attribute; overloaded version for int32_t.
 *
 * If an attribute with the given name exists, it is overwritten,
 * otherwise a new attribute is created. Returns the attribute node
 * that was either created or changed.
 *
 * @param pcszName
 * @param i
 * @return
 */
AttributeNode* ElementNode::setAttribute(const char *pcszName, int32_t i)
{
    char szValue[12];  // negative sign + 10 digits + \0
    RTStrPrintf(szValue, sizeof(szValue), "%RI32", i);
    AttributeNode *p = setAttribute(pcszName, szValue);
    return p;
}

/**
 * Sets the given attribute; overloaded version for uint32_t.
 *
 * If an attribute with the given name exists, it is overwritten,
 * otherwise a new attribute is created. Returns the attribute node
 * that was either created or changed.
 *
 * @param pcszName
 * @param u
 * @return
 */
AttributeNode* ElementNode::setAttribute(const char *pcszName, uint32_t u)
{
    char szValue[11];  // 10 digits + \0
    RTStrPrintf(szValue, sizeof(szValue), "%RU32", u);
    AttributeNode *p = setAttribute(pcszName, szValue);
    return p;
}

/**
 * Sets the given attribute; overloaded version for int64_t.
 *
 * If an attribute with the given name exists, it is overwritten,
 * otherwise a new attribute is created. Returns the attribute node
 * that was either created or changed.
 *
 * @param pcszName
 * @param i
 * @return
 */
AttributeNode* ElementNode::setAttribute(const char *pcszName, int64_t i)
{
    char szValue[21];  // negative sign + 19 digits + \0
    RTStrPrintf(szValue, sizeof(szValue), "%RI64", i);
    AttributeNode *p = setAttribute(pcszName, szValue);
    return p;
}

/**
 * Sets the given attribute; overloaded version for uint64_t.
 *
 * If an attribute with the given name exists, it is overwritten,
 * otherwise a new attribute is created. Returns the attribute node
 * that was either created or changed.
 *
 * @param pcszName
 * @param u
 * @return
 */
AttributeNode* ElementNode::setAttribute(const char *pcszName, uint64_t u)
{
    char szValue[21];  // 20 digits + \0
    RTStrPrintf(szValue, sizeof(szValue), "%RU64", u);
    AttributeNode *p = setAttribute(pcszName, szValue);
    return p;
}

/**
 * Sets the given attribute to the given uint32_t, outputs a hexadecimal string.
 *
 * If an attribute with the given name exists, it is overwritten,
 * otherwise a new attribute is created. Returns the attribute node
 * that was either created or changed.
 *
 * @param pcszName
 * @param u
 * @return
 */
AttributeNode* ElementNode::setAttributeHex(const char *pcszName, uint32_t u)
{
    char szValue[11];  // "0x" + 8 digits + \0
    RTStrPrintf(szValue, sizeof(szValue), "0x%RX32", u);
    AttributeNode *p = setAttribute(pcszName, szValue);
    return p;
}

/**
 * Sets the given attribute; overloaded version for bool.
 *
 * If an attribute with the given name exists, it is overwritten,
 * otherwise a new attribute is created. Returns the attribute node
 * that was either created or changed.
 *
 * @param   pcszName    The attribute name.
 * @param   f           The attribute value.
 * @return
 */
AttributeNode* ElementNode::setAttribute(const char *pcszName, bool f)
{
    return setAttribute(pcszName, (f) ? "true" : "false");
}

/**
 * Private constructor for a new attribute node.
 *
 * @param   pElmRoot    Pointer to the root element.  Needed for getting the
 *                      default name space.
 * @param   pParent     Pointer to the parent element (always an ElementNode,
 *                      despite the type).  NULL for the root node.
 * @param   pListAnchor Pointer to the m_children member of the parent.  NULL
 *                      for the root node.
 * @param   pLibAttr    Pointer to the libxml2 attribute structure.
 */
AttributeNode::AttributeNode(const ElementNode *pElmRoot,
                             Node *pParent,
                             PRTLISTANCHOR pListAnchor,
                             xmlAttr *pLibAttr)
    : Node(IsAttribute,
           pParent,
           pListAnchor,
           NULL,
           pLibAttr)
{
    m_pcszName = (const char *)pLibAttr->name;
    RT_NOREF_PV(pElmRoot);

    if (   pLibAttr->ns
        && pLibAttr->ns->prefix)
    {
        m_pcszNamespacePrefix = (const char *)pLibAttr->ns->prefix;
        m_pcszNamespaceHref   = (const char *)pLibAttr->ns->href;
    }
}

ContentNode::ContentNode(Node *pParent, PRTLISTANCHOR pListAnchor, xmlNode *pLibNode)
    : Node(IsContent,
           pParent,
           pListAnchor,
           pLibNode,
           NULL)
{
}

/*
 * NodesLoop
 *
 */

struct NodesLoop::Data
{
    ElementNodesList listElements;
    ElementNodesList::const_iterator it;
};

NodesLoop::NodesLoop(const ElementNode &node, const char *pcszMatch /* = NULL */)
{
    m = new Data;
    node.getChildElements(m->listElements, pcszMatch);
    m->it = m->listElements.begin();
}

NodesLoop::~NodesLoop()
{
    delete m;
}


/**
 * Handy convenience helper for looping over all child elements. Create an
 * instance of NodesLoop on the stack and call this method until it returns
 * NULL, like this:
 * <code>
 *      xml::ElementNode node;               // should point to an element
 *      xml::NodesLoop loop(node, "child");  // find all "child" elements under node
 *      const xml::ElementNode *pChild = NULL;
 *      while (pChild = loop.forAllNodes())
 *          ...;
 * </code>
 * @return
 */
const ElementNode* NodesLoop::forAllNodes() const
{
    const ElementNode *pNode = NULL;

    if (m->it != m->listElements.end())
    {
        pNode = *(m->it);
        ++(m->it);
    }

    return pNode;
}

////////////////////////////////////////////////////////////////////////////////
//
// Document class
//
////////////////////////////////////////////////////////////////////////////////

struct Document::Data
{
    xmlDocPtr   plibDocument;
    ElementNode *pRootElement;
    ElementNode *pComment;

    Data()
    {
        plibDocument = NULL;
        pRootElement = NULL;
        pComment = NULL;
    }

    ~Data()
    {
        reset();
    }

    void reset()
    {
        if (plibDocument)
        {
            xmlFreeDoc(plibDocument);
            plibDocument = NULL;
        }
        if (pRootElement)
        {
            delete pRootElement;
            pRootElement = NULL;
        }
        if (pComment)
        {
            delete pComment;
            pComment = NULL;
        }
    }

    void copyFrom(const Document::Data *p)
    {
        if (p->plibDocument)
        {
            plibDocument = xmlCopyDoc(p->plibDocument,
                                      1);      // recursive == copy all
        }
    }
};

Document::Document()
    : m(new Data)
{
}

Document::Document(const Document &x)
    : m(new Data)
{
    m->copyFrom(x.m);
}

Document& Document::operator=(const Document &x)
{
    m->reset();
    m->copyFrom(x.m);
    return *this;
}

Document::~Document()
{
    delete m;
}

/**
 * private method to refresh all internal structures after the internal pDocument
 * has changed. Called from XmlFileParser::read(). m->reset() must have been
 * called before to make sure all members except the internal pDocument are clean.
 */
void Document::refreshInternals() // private
{
    m->pRootElement = new ElementNode(NULL, NULL, NULL, xmlDocGetRootElement(m->plibDocument));

    ElementNode::buildChildren(m->pRootElement);
}

/**
 * Returns the root element of the document, or NULL if the document is empty.
 * Const variant.
 * @return
 */
const ElementNode *Document::getRootElement() const
{
    return m->pRootElement;
}

/**
 * Returns the root element of the document, or NULL if the document is empty.
 * Non-const variant.
 * @return
 */
ElementNode *Document::getRootElement()
{
    return m->pRootElement;
}

/**
 * Creates a new element node and sets it as the root element.
 *
 * This will only work if the document is empty; otherwise EDocumentNotEmpty is
 * thrown.
 */
ElementNode *Document::createRootElement(const char *pcszRootElementName,
                                         const char *pcszComment /* = NULL */)
{
    if (m->pRootElement || m->plibDocument)
        throw EDocumentNotEmpty(RT_SRC_POS);

    // libxml side: create document, create root node
    m->plibDocument = xmlNewDoc((const xmlChar *)"1.0");
    xmlNode *plibRootNode = xmlNewNode(NULL /*namespace*/ , (const xmlChar *)pcszRootElementName);
    if (!plibRootNode)
        throw std::bad_alloc();
    xmlDocSetRootElement(m->plibDocument, plibRootNode);

    // now wrap this in C++
    m->pRootElement = new ElementNode(NULL, NULL, NULL, plibRootNode);

    // add document global comment if specified
    if (pcszComment != NULL)
    {
        xmlNode *pComment = xmlNewDocComment(m->plibDocument, (const xmlChar *)pcszComment);
        if (!pComment)
            throw std::bad_alloc();
        xmlAddPrevSibling(plibRootNode, pComment);

        // now wrap this in C++
        m->pComment = new ElementNode(NULL, NULL, NULL, pComment);
    }

    return m->pRootElement;
}

////////////////////////////////////////////////////////////////////////////////
//
// XmlParserBase class
//
////////////////////////////////////////////////////////////////////////////////

static void xmlParserBaseGenericError(void *pCtx, const char *pszMsg, ...) RT_NOTHROW_DEF
{
    NOREF(pCtx);
    va_list args;
    va_start(args, pszMsg);
    RTLogRelPrintfV(pszMsg, args);
    va_end(args);
}

#if LIBXML_VERSION >= 21205
static void xmlStructuredErrorFunc(void *userData, const xmlError *error)  RT_NOTHROW_DEF
{
    NOREF(userData);
    NOREF(error);
}
#else
static void xmlParserBaseStructuredError(void *pCtx, xmlErrorPtr error) RT_NOTHROW_DEF
{
    NOREF(pCtx);
    /* we expect that there is always a trailing NL */
    LogRel(("XML error at '%s' line %d: %s", error->file, error->line, error->message));
}
#endif

XmlParserBase::XmlParserBase()
{
    m_ctxt = xmlNewParserCtxt();
    if (m_ctxt == NULL)
        throw std::bad_alloc();
    /* per-thread so it must be here */
    xmlSetGenericErrorFunc(NULL, xmlParserBaseGenericError);
#if LIBXML_VERSION >= 21205
    xmlSetStructuredErrorFunc(NULL, xmlStructuredErrorFunc);
#else
    xmlSetStructuredErrorFunc(NULL, xmlParserBaseStructuredError);
#endif
}

XmlParserBase::~XmlParserBase()
{
    xmlSetStructuredErrorFunc(NULL, NULL);
    xmlSetGenericErrorFunc(NULL, NULL);
    xmlFreeParserCtxt (m_ctxt);
    m_ctxt = NULL;
}

////////////////////////////////////////////////////////////////////////////////
//
// XmlMemParser class
//
////////////////////////////////////////////////////////////////////////////////

XmlMemParser::XmlMemParser()
    : XmlParserBase()
{
}

XmlMemParser::~XmlMemParser()
{
}

/**
 * Parse the given buffer and fills the given Document object with its contents.
 * Throws XmlError on parsing errors.
 *
 * The document that is passed in will be reset before being filled if not empty.
 *
 * @param pvBuf         Memory buffer to parse.
 * @param cbSize        Size of the memory buffer.
 * @param strFilename   Refernece to the name of the file we're parsing.
 * @param doc           Reference to the output document.  This will be reset
 *                      and filled with data according to file contents.
 */
void XmlMemParser::read(const void *pvBuf, size_t cbSize,
                        const RTCString &strFilename,
                        Document &doc)
{
    GlobalLock lock;
//     global.setExternalEntityLoader(ExternalEntityLoader);

    const char *pcszFilename = strFilename.c_str();

    doc.m->reset();
    const int options = XML_PARSE_NOBLANKS /* remove blank nodes */
                      | XML_PARSE_NONET    /* forbit any network access */
#if LIBXML_VERSION >= 20700
                      | XML_PARSE_HUGE     /* don't restrict the node depth
                                              to 256 (bad for snapshots!) */
#endif
                ;
    if (!(doc.m->plibDocument = xmlCtxtReadMemory(m_ctxt,
                                                  (const char*)pvBuf,
                                                  (int)cbSize,
                                                  pcszFilename,
                                                  NULL,       // encoding = auto
                                                  options)))
        throw XmlError((xmlErrorPtr)xmlCtxtGetLastError(m_ctxt));

    doc.refreshInternals();
}

////////////////////////////////////////////////////////////////////////////////
//
// XmlMemWriter class
//
////////////////////////////////////////////////////////////////////////////////

XmlMemWriter::XmlMemWriter()
  : m_pBuf(0)
{
}

XmlMemWriter::~XmlMemWriter()
{
    if (m_pBuf)
        xmlFree(m_pBuf);
}

void XmlMemWriter::write(const Document &doc, void **ppvBuf, size_t *pcbSize)
{
    if (m_pBuf)
    {
        xmlFree(m_pBuf);
        m_pBuf = 0;
    }
    int size;
    xmlDocDumpFormatMemory(doc.m->plibDocument, (xmlChar**)&m_pBuf, &size, 1);
    *ppvBuf = m_pBuf;
    *pcbSize = size;
}


////////////////////////////////////////////////////////////////////////////////
//
// XmlStringWriter class
//
////////////////////////////////////////////////////////////////////////////////

XmlStringWriter::XmlStringWriter()
  : m_pStrDst(NULL), m_fOutOfMemory(false)
{
}

int XmlStringWriter::write(const Document &rDoc, RTCString *pStrDst)
{
    /*
     * Clear the output string and take the global libxml2 lock so we can
     * safely configure the output formatting.
     */
    pStrDst->setNull();

    GlobalLock lock;

    xmlIndentTreeOutput = 1;
    xmlTreeIndentString = "  ";
    xmlSaveNoEmptyTags  = 0;

    /*
     * Do a pass to calculate the size.
     */
    size_t cbOutput = 1; /* zero term */

    xmlSaveCtxtPtr pSaveCtx= xmlSaveToIO(WriteCallbackForSize, CloseCallback, &cbOutput, NULL /*pszEncoding*/, XML_SAVE_FORMAT);
    if (!pSaveCtx)
        return VERR_NO_MEMORY;

    long rcXml = xmlSaveDoc(pSaveCtx, rDoc.m->plibDocument);
    xmlSaveClose(pSaveCtx);
    if (rcXml == -1)
        return VERR_GENERAL_FAILURE;

    /*
     * Try resize the string.
     */
    int rc = pStrDst->reserveNoThrow(cbOutput);
    if (RT_SUCCESS(rc))
    {
        /*
         * Do the real run where we feed output to the string.
         */
        m_pStrDst      = pStrDst;
        m_fOutOfMemory = false;
        pSaveCtx = xmlSaveToIO(WriteCallbackForReal, CloseCallback, this, NULL /*pszEncoding*/, XML_SAVE_FORMAT);
        if (pSaveCtx)
        {
            rcXml = xmlSaveDoc(pSaveCtx, rDoc.m->plibDocument);
            xmlSaveClose(pSaveCtx);
            m_pStrDst = NULL;
            if (rcXml != -1)
            {
                if (!m_fOutOfMemory)
                    return VINF_SUCCESS;

                rc = VERR_NO_STR_MEMORY;
            }
            else
                rc = VERR_GENERAL_FAILURE;
        }
        else
            rc = VERR_NO_MEMORY;
        pStrDst->setNull();
        m_pStrDst = NULL;
    }
    return rc;
}

/*static*/ int XmlStringWriter::WriteCallbackForSize(void *pvUser, const char *pachBuf, int cbToWrite) RT_NOTHROW_DEF
{
    if (cbToWrite > 0)
        *(size_t *)pvUser += (unsigned)cbToWrite;
    RT_NOREF(pachBuf);
    return cbToWrite;
}

/*static*/ int XmlStringWriter::WriteCallbackForReal(void *pvUser, const char *pachBuf, int cbToWrite) RT_NOTHROW_DEF
{
    XmlStringWriter *pThis = static_cast<XmlStringWriter*>(pvUser);
    if (!pThis->m_fOutOfMemory)
    {
        if (cbToWrite > 0)
        {
            try
            {
                pThis->m_pStrDst->append(pachBuf, (size_t)cbToWrite);
            }
            catch (std::bad_alloc &)
            {
                pThis->m_fOutOfMemory = true;
                return -1;
            }
        }
        return cbToWrite;
    }
    return -1; /* failure */
}

/*static*/ int XmlStringWriter::CloseCallback(void *pvUser) RT_NOTHROW_DEF
{
    /* Nothing to do here. */
    RT_NOREF(pvUser);
    return 0;
}



////////////////////////////////////////////////////////////////////////////////
//
// XmlFileParser class
//
////////////////////////////////////////////////////////////////////////////////

struct XmlFileParser::Data
{
    RTCString strXmlFilename;

    Data()
    {
    }

    ~Data()
    {
    }
};

XmlFileParser::XmlFileParser()
    : XmlParserBase(),
      m(new Data())
{
}

XmlFileParser::~XmlFileParser()
{
    delete m;
    m = NULL;
}

struct IOContext
{
    File file;
    RTCString error;

    IOContext(const char *pcszFilename, File::Mode mode, bool fFlush = false)
        : file(mode, pcszFilename, fFlush)
    {
    }

    void setError(const RTCError &x)
    {
        error = x.what();
    }

    void setError(const std::exception &x)
    {
        error = x.what();
    }

private:
    DECLARE_CLS_COPY_CTOR_ASSIGN_NOOP(IOContext); /* (shuts up C4626 and C4625 MSC warnings) */
};

struct ReadContext : IOContext
{
    ReadContext(const char *pcszFilename)
        : IOContext(pcszFilename, File::Mode_Read)
    {
    }

private:
    DECLARE_CLS_COPY_CTOR_ASSIGN_NOOP(ReadContext); /* (shuts up C4626 and C4625 MSC warnings) */
};

struct WriteContext : IOContext
{
    WriteContext(const char *pcszFilename, bool fFlush)
        : IOContext(pcszFilename, File::Mode_Overwrite, fFlush)
    {
    }

private:
    DECLARE_CLS_COPY_CTOR_ASSIGN_NOOP(WriteContext); /* (shuts up C4626 and C4625 MSC warnings) */
};

/**
 * Reads the given file and fills the given Document object with its contents.
 * Throws XmlError on parsing errors.
 *
 * The document that is passed in will be reset before being filled if not empty.
 *
 * @param strFilename in: name fo file to parse.
 * @param doc out: document to be reset and filled with data according to file contents.
 */
void XmlFileParser::read(const RTCString &strFilename,
                         Document &doc)
{
    GlobalLock lock;
//     global.setExternalEntityLoader(ExternalEntityLoader);

    m->strXmlFilename = strFilename;
    const char *pcszFilename = strFilename.c_str();

    ReadContext context(pcszFilename);
    doc.m->reset();
    const int options = XML_PARSE_NOBLANKS /* remove blank nodes */
                      | XML_PARSE_NONET    /* forbit any network access */
#if LIBXML_VERSION >= 20700
                      | XML_PARSE_HUGE     /* don't restrict the node depth
                                              to 256 (bad for snapshots!) */
#endif
                ;
    if (!(doc.m->plibDocument = xmlCtxtReadIO(m_ctxt,
                                              ReadCallback,
                                              CloseCallback,
                                              &context,
                                              pcszFilename,
                                              NULL,       // encoding = auto
                                              options)))
        throw XmlError((xmlErrorPtr)xmlCtxtGetLastError(m_ctxt));

    doc.refreshInternals();
}

/*static*/ int XmlFileParser::ReadCallback(void *aCtxt, char *aBuf, int aLen) RT_NOTHROW_DEF
{
    ReadContext *pContext = static_cast<ReadContext*>(aCtxt);

    /* To prevent throwing exceptions while inside libxml2 code, we catch
     * them and forward to our level using a couple of variables. */

    try
    {
        return pContext->file.read(aBuf, aLen);
    }
    catch (const xml::EIPRTFailure &err) { pContext->setError(err); }
    catch (const RTCError &err) { pContext->setError(err); }
    catch (const std::exception &err) { pContext->setError(err); }
    catch (...) { pContext->setError(xml::LogicError(RT_SRC_POS)); }

    return -1 /* failure */;
}

/*static*/ int XmlFileParser::CloseCallback(void *aCtxt) RT_NOTHROW_DEF
{
    /// @todo to be written
    NOREF(aCtxt);

    return -1;
}

////////////////////////////////////////////////////////////////////////////////
//
// XmlFileWriter class
//
////////////////////////////////////////////////////////////////////////////////

struct XmlFileWriter::Data
{
    Document *pDoc;
};

XmlFileWriter::XmlFileWriter(Document &doc)
{
    m = new Data();
    m->pDoc = &doc;
}

XmlFileWriter::~XmlFileWriter()
{
    delete m;
}

void XmlFileWriter::writeInternal(const char *pcszFilename, bool fSafe)
{
    WriteContext context(pcszFilename, fSafe);

    GlobalLock lock;

    /* serialize to the stream */
    xmlIndentTreeOutput = 1;
    xmlTreeIndentString = "  ";
    xmlSaveNoEmptyTags = 0;

    xmlSaveCtxtPtr saveCtxt;
    if (!(saveCtxt = xmlSaveToIO(WriteCallback,
                                 CloseCallback,
                                 &context,
                                 NULL,
                                 XML_SAVE_FORMAT)))
        throw xml::LogicError(RT_SRC_POS);

    long rc = xmlSaveDoc(saveCtxt, m->pDoc->m->plibDocument);
    if (rc == -1)
    {
        /* look if there was a forwarded exception from the lower level */
//         if (m->trappedErr.get() != NULL)
//             m->trappedErr->rethrow();

        /* there must be an exception from the Output implementation,
         * otherwise the save operation must always succeed. */
        throw xml::LogicError(RT_SRC_POS);
    }

    xmlSaveClose(saveCtxt);
}

void XmlFileWriter::write(const char *pcszFilename, bool fSafe)
{
    if (!fSafe)
        writeInternal(pcszFilename, fSafe);
    else
    {
        /* Empty string and directory spec must be avoid. */
        if (RTPathFilename(pcszFilename) == NULL)
            throw xml::LogicError(RT_SRC_POS);

        /* Construct both filenames first to ease error handling.  */
        size_t const cchFilename = strlen(pcszFilename);
        size_t const cchTmpSuff  = strlen(s_pszTmpSuff);
        size_t const cchPrevSuff = strlen(s_pszPrevSuff);
        AssertStmt(cchFilename + RT_MAX(cchTmpSuff, cchPrevSuff) < RTPATH_MAX,
                   throw EIPRTFailure(VERR_BUFFER_OVERFLOW, "filename to long (%zu)", cchFilename));

        char szTmpFilename[RTPATH_MAX];
        memcpy(mempcpy(szTmpFilename, pcszFilename, cchFilename), s_pszTmpSuff, cchTmpSuff + 1);

        char szPrevFilename[RTPATH_MAX];
        memcpy(mempcpy(szPrevFilename, pcszFilename, cchFilename), s_pszPrevSuff, cchPrevSuff + 1);

        /* Write the XML document to the temporary file.  */
        writeInternal(szTmpFilename, fSafe);

        /* Make a backup of any existing file (ignore failure). */
        uint64_t cbPrevFile;
        int rc = RTFileQuerySizeByPath(pcszFilename, &cbPrevFile);
        if (RT_SUCCESS(rc) && cbPrevFile >= 16)
            RTFileRename(pcszFilename, szPrevFilename, RTPATHRENAME_FLAGS_REPLACE);

        /* Commit the temporary file. Just leave the tmp file behind on failure. */
        rc = RTFileRename(szTmpFilename, pcszFilename, RTPATHRENAME_FLAGS_REPLACE);
        if (RT_FAILURE(rc))
            throw EIPRTFailure(rc, "Failed to replace '%s' with '%s'", pcszFilename, szTmpFilename);

        /* Flush the directory changes (required on linux at least). */
        RTPathStripFilename(szTmpFilename);
        rc = RTDirFlush(szTmpFilename);
        AssertMsg(RT_SUCCESS(rc) || rc == VERR_NOT_SUPPORTED || rc == VERR_NOT_IMPLEMENTED, ("%Rrc\n", rc));
    }
}

/*static*/ int XmlFileWriter::WriteCallback(void *aCtxt, const char *aBuf, int aLen) RT_NOTHROW_DEF
{
    WriteContext *pContext = static_cast<WriteContext*>(aCtxt);

    /* To prevent throwing exceptions while inside libxml2 code, we catch
     * them and forward to our level using a couple of variables. */
    try
    {
        return pContext->file.write(aBuf, aLen);
    }
    catch (const xml::EIPRTFailure &err) { pContext->setError(err); }
    catch (const RTCError &err) { pContext->setError(err); }
    catch (const std::exception &err) { pContext->setError(err); }
    catch (...) { pContext->setError(xml::LogicError(RT_SRC_POS)); }

    return -1 /* failure */;
}

/*static*/ int XmlFileWriter::CloseCallback(void *aCtxt) RT_NOTHROW_DEF
{
    /// @todo to be written
    NOREF(aCtxt);

    return -1;
}

/*static*/ const char * const XmlFileWriter::s_pszTmpSuff  = "-tmp";
/*static*/ const char * const XmlFileWriter::s_pszPrevSuff = "-prev";


} // end namespace xml

