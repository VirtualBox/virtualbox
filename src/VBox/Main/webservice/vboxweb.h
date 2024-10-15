/* $Id: vboxweb.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * vboxweb.h - header file for "real" web server code.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MAIN_INCLUDED_SRC_webservice_vboxweb_h
#define MAIN_INCLUDED_SRC_webservice_vboxweb_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#define LOG_GROUP LOG_GROUP_WEBSERVICE
#include <VBox/log.h>
#include <VBox/err.h>

#include <VBox/com/VirtualBox.h>
#include <VBox/com/Guid.h>
#include <VBox/com/AutoLock.h>

#include <iprt/asm.h>

#include <iprt/sanitized/string>

/****************************************************************************
 *
 * debug macro
 *
 ****************************************************************************/

#define WEBDEBUG(a) do { if (g_fVerbose) { LogRel(a); } } while (0)

/****************************************************************************
 *
 * typedefs
 *
 ****************************************************************************/

// type used by gSOAP-generated code
typedef std::string WSDLT_ID;               // combined managed object ref (websession ID plus object ID)
typedef std::string vbox__uuid;

/****************************************************************************
 *
 * global variables
 *
 ****************************************************************************/

extern bool g_fVerbose;

extern util::WriteLockHandle  *g_pWebsessionsLockHandle;

extern const WSDLT_ID          g_EmptyWSDLID;

/****************************************************************************
 *
 * SOAP exceptions
 *
 ****************************************************************************/

extern void RaiseSoapInvalidObjectFault(struct soap *soap, WSDLT_ID obj);

extern void RaiseSoapRuntimeFault(struct soap *soap, const WSDLT_ID &idThis, const char *pcszMethodName, HRESULT apirc, IUnknown *pObj, const com::Guid &iid);

/****************************************************************************
 *
 * conversion helpers
 *
 ****************************************************************************/

extern std::string ConvertComString(const com::Bstr &bstr);

extern std::string ConvertComString(const com::Guid &bstr);

extern std::string Base64EncodeByteArray(ComSafeArrayIn(BYTE, aData));

extern void Base64DecodeByteArray(struct soap *soap, const std::string& aStr, ComSafeArrayOut(BYTE, aData), const WSDLT_ID &idThis, const char *pszMethodName, IUnknown *pObj, const com::Guid &iid);

/****************************************************************************
 *
 * managed object reference classes
 *
 ****************************************************************************/

class WebServiceSessionPrivate;
class ManagedObjectRef;

/**
 *  An instance of this gets created for every client that logs onto the
 *  webservice (via the special IWebsessionManager::logon() SOAP API) and
 *  maintains the managed object references for that websession.
 */
class WebServiceSession
{
    friend class ManagedObjectRef;

    private:
        uint64_t                    _uWebsessionID;
        uint64_t                    _uNextObjectID;
        WebServiceSessionPrivate    *_pp;               // opaque data struct (defined in vboxweb.cpp)
        bool                        _fDestructing;

        uint32_t                    _tLastObjectLookup;

        // hide the copy constructor because we're not copyable
        WebServiceSession(const WebServiceSession &copyFrom);

    public:
        WebServiceSession();

        ~WebServiceSession();

        int authenticate(const char *pcszUsername,
                         const char *pcszPassword,
                         IVirtualBox **ppVirtualBox);

        ManagedObjectRef* findRefFromPtr(const IUnknown *pObject);

        uint64_t getID() const
        {
            return _uWebsessionID;
        }

        uint64_t createObjectID()
        {
            uint64_t id = ASMAtomicIncU64(&_uNextObjectID);
            return id - 1;
        }

        void touch();

        uint32_t getLastObjectLookup() const
        {
            return _tLastObjectLookup;
        }

        static WebServiceSession* findWebsessionFromRef(const WSDLT_ID &id);

        size_t CountRefs();
};

/**
 *  ManagedObjectRef is used to map COM pointers to object IDs
 *  within a websession. Such object IDs are 64-bit integers.
 *
 *  When a webservice method call is invoked on an object, it
 *  has an opaque string called a "managed object reference". Such
 *  a string consists of a websession ID combined with an object ID.
 *
 */
class ManagedObjectRef
{
    protected:
        // owning websession:
        WebServiceSession           &_websession;


        IUnknown                    *_pobjUnknown;          // pointer to IUnknown interface for this MOR

        void                        *_pobjInterface;        // pointer to COM interface represented by _guidInterface, for which this MOR
                                                            // was created; this may be an IUnknown or something more specific
        com::Guid                   _guidInterface;         // the interface which _pvObj represents

        const char                  *_pcszInterface;        // string representation of that interface (e.g. "IMachine")

        // keys:
        uint64_t                    _id;
        uintptr_t                   _ulp;

        // long ID as string
        WSDLT_ID                    _strID;

    public:
        ManagedObjectRef(WebServiceSession &websession,
                         IUnknown *pobjUnknown,
                         void *pobjInterface,
                         const com::Guid &guidInterface,
                         const char *pcszInterface);
        ~ManagedObjectRef();

        uint64_t getID()
        {
            return _id;
        }

        /**
         * Returns the contained COM pointer and the UUID of the COM interface
         * which it supports.
         * @param   ppobjInterface
         * @param   ppobjUnknown
         * @return
         */
        const com::Guid& getPtr(void **ppobjInterface,
                                IUnknown **ppobjUnknown)
        {
            *ppobjInterface = _pobjInterface;
            *ppobjUnknown = _pobjUnknown;
            return _guidInterface;
        }

        /**
         * Returns the ID of this managed object reference to string
         * form, for returning with SOAP data or similar.
         *
         * @return The ID in string form.
         */
        const WSDLT_ID& getWSDLID() const
        {
            return _strID;
        }

        const char* getInterfaceName() const
        {
            return _pcszInterface;
        }

        static int findRefFromId(const WSDLT_ID &id,
                                 ManagedObjectRef **pRef,
                                 bool fNullAllowed);
};

/**
 * Template function that resolves a managed object reference to a COM pointer
 * of the template class T.
 *
 * This gets called only from tons of generated code in methodmaps.cpp to
 * resolve objects in *input* parameters to COM methods (i.e. translate
 * MOR strings to COM objects which should exist already).
 *
 * This is a template function so that we can support ComPtr's for arbitrary
 * interfaces and automatically verify that the managed object reference on
 * the internal stack actually is of the expected interface. We also now avoid
 * calling QueryInterface for the case that the interface desired by the caller
 * is the same as the interface for which the MOR was originally created. In
 * that case, the lookup is very fast.
 *
 * @param soap
 * @param id in: integer managed object reference, as passed in by web service client
 * @param pComPtr out: reference to COM pointer object that receives the com pointer,
 *                if SOAP_OK is returned
 * @param fNullAllowed in: if true, then this func returns a NULL COM pointer if an
 *                empty MOR is passed in (i.e. NULL pointers are allowed). If false,
 *                then this fails; this will be false when called for the "this"
 *                argument of method calls, which really shouldn't be NULL.
 * @return error code or SOAP_OK if no error
 */
template <class T>
int findComPtrFromId(struct soap *soap,
                     const WSDLT_ID &id,
                     ComPtr<T> &pComPtr,
                     bool fNullAllowed)
{
    // findRefFromId requires thelock
    util::AutoWriteLock lock(g_pWebsessionsLockHandle COMMA_LOCKVAL_SRC_POS);

    ManagedObjectRef *pRef;
    int vrc = ManagedObjectRef::findRefFromId(id, &pRef, fNullAllowed);
    if (vrc != VINF_SUCCESS)
        // error:
        RaiseSoapInvalidObjectFault(soap, id);
    else
    {
        if (fNullAllowed && pRef == NULL)
        {
            WEBDEBUG(("   %s(): returning NULL object as permitted\n", __FUNCTION__));
            pComPtr.setNull();
            return VINF_SUCCESS;
        }

        const com::Guid &guidCaller = COM_IIDOF(T);

        // pRef->getPtr returns both a void* for its specific interface pointer as well as a generic IUnknown*
        void *pobjInterface;
        IUnknown *pobjUnknown;
        const com::Guid &guidInterface = pRef->getPtr(&pobjInterface, &pobjUnknown);

        if (guidInterface == guidCaller)
        {
            // same interface: then no QueryInterface needed
            WEBDEBUG(("   %s(): returning original %s*=0x%lX (IUnknown*=0x%lX)\n", __FUNCTION__, pRef->getInterfaceName(), pobjInterface, pobjUnknown));
            pComPtr = (T*)pobjInterface;        // this calls AddRef() once
            return VINF_SUCCESS;
        }

        // QueryInterface tests whether p actually supports the templated T interface desired by caller
        T *pT;
        pobjUnknown->QueryInterface(guidCaller.ref(), (void**)&pT);      // this adds a reference count
        if (pT)
        {
            // assign to caller's ComPtr<T>; use asOutParam() to avoid adding another reference, QueryInterface() already added one
            WEBDEBUG(("   %s(): returning pointer 0x%lX for queried interface %RTuuid (IUnknown*=0x%lX)\n", __FUNCTION__, pT, guidCaller.raw(), pobjUnknown));
            *(pComPtr.asOutParam()) = pT;
            return VINF_SUCCESS;
        }

        WEBDEBUG(("    Interface not supported for object reference %s, which is of class %s\n", id.c_str(), pRef->getInterfaceName()));
        vrc = VERR_WEB_UNSUPPORTED_INTERFACE;
        RaiseSoapInvalidObjectFault(soap, id);      // @todo better message
    }

    return vrc;
}

/**
 * Creates a new managed object reference for the given COM pointer. If one
 * already exists for the given pointer, then that reference's ID is returned.
 *
 * This gets called from tons of generated code in methodmaps.cpp to resolve
 * objects *returned* from COM methods (i.e. create MOR strings from COM
 * objects which might have been newly created).
 *
 * @param idParent managed object reference of calling object; used to extract
 *              websession ID
 * @param pcszInterface
 * @param pc COM object for which to create a reference
 * @return existing or new managed object reference
 */
template <class T>
const WSDLT_ID& createOrFindRefFromComPtr(const WSDLT_ID &idParent,
                                          const char *pcszInterface,
                                          const ComPtr<T> &pc)
{
    // NULL comptr should return NULL MOR
    if (pc.isNull())
    {
        WEBDEBUG(("   createOrFindRefFromComPtr(): returning empty MOR for NULL COM pointer\n"));
        return g_EmptyWSDLID;
    }

    util::AutoWriteLock lock(g_pWebsessionsLockHandle COMMA_LOCKVAL_SRC_POS);
    WebServiceSession *pWebsession;
    if ((pWebsession = WebServiceSession::findWebsessionFromRef(idParent)))
    {
        ManagedObjectRef *pRef;

        // we need an IUnknown pointer for the MOR
        ComPtr<IUnknown> pobjUnknown = pc;

        if (    ((pRef = pWebsession->findRefFromPtr(pobjUnknown)))
             || ((pRef = new ManagedObjectRef(*pWebsession,
                                              pobjUnknown,          // IUnknown *pobjUnknown
                                              pc,                   // void *pobjInterface
                                              COM_IIDOF(T),
                                              pcszInterface)))
           )
            return pRef->getWSDLID();
    }

    // websession has expired, return an empty MOR instead of allocating a
    // new reference which couldn't be used anyway.
    return g_EmptyWSDLID;
}

#endif /* !MAIN_INCLUDED_SRC_webservice_vboxweb_h */

