/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is the Python XPCOM language bindings.
 *
 * The Initial Developer of the Original Code is
 * ActiveState Tool Corp.
 * Portions created by the Initial Developer are Copyright (C) 2000
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Mark Hammond <mhammond@skippinet.com.au> (original author)
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

// Py_nsIID.cpp -- IID type for Python/XPCOM
//
// This code is part of the XPCOM extensions for Python.
//
// Written May 2000 by Mark Hammond.
//
// Based heavily on the Python COM support, which is
// (c) Mark Hammond and Greg Stein.
//
// (c) 2000, ActiveState corp.
//
// @doc

#include "PyXPCOM_std.h"
#include <nsIInterfaceInfoManager.h>

nsIID Py_nsIID_NULL = {0,0,0,{0,0,0,0,0,0,0,0}};

// @pymethod <o Py_nsIID>|xpcom|IID|Creates a new IID object
PYXPCOM_EXPORT PyObject *PyXPCOMMethod_IID(PyObject *self, PyObject *args)
{
	PyObject *obIID;
	PyObject *obBuf;
	if ( PyArg_ParseTuple(args, "O", &obBuf)) {
#if PY_MAJOR_VERSION <= 2
		if (PyBuffer_Check(obBuf)) {
			PyBufferProcs *pb = NULL;
			pb = obBuf->ob_type->tp_as_buffer;
			void *buf = NULL;
			int size = (*pb->bf_getreadbuffer)(obBuf, 0, &buf);
#else
		if (PyObject_CheckBuffer(obBuf)) {
# ifndef Py_LIMITED_API
			Py_buffer view;
			if (PyObject_GetBuffer(obBuf, &view, PyBUF_CONTIG_RO) != 0) {
				PyErr_Format(PyExc_ValueError, "Could not get contiguous buffer from object");
				return NULL;
			}
			Py_ssize_t size = view.len;
			const void *buf = view.buf;
# else  /* Py_LIMITED_API - the buffer API is non-existant, from what I can tell */
			const void *buf = NULL;
			Py_ssize_t size = 0;
			if (PyObject_AsReadBuffer(obBuf, &buf, &size) != 0) {
				PyErr_Format(PyExc_ValueError, "Could not get read-only buffer from object");
				return NULL;
			}
# endif /* Py_LIMITED_API */
#endif
			if (size != sizeof(nsIID) || buf==NULL) {
#if PY_MAJOR_VERSION >= 3 && !defined(Py_LIMITED_API)
				PyBuffer_Release(&view);
#endif
#ifdef VBOX
				PyErr_Format(PyExc_ValueError, "A buffer object to be converted to an IID must be exactly %d bytes long", (int)sizeof(nsIID));
#else
				PyErr_Format(PyExc_ValueError, "A buffer object to be converted to an IID must be exactly %d bytes long", sizeof(nsIID));
#endif
				return NULL;
			}
			nsIID iid;
			unsigned char const *ptr = (unsigned char const *)buf;
			iid.m0 = XPT_SWAB32(*((PRUint32 *)ptr));
			ptr = ((unsigned char const *)buf) + offsetof(nsIID, m1);
			iid.m1 = XPT_SWAB16(*((PRUint16 *)ptr));
			ptr = ((unsigned char const *)buf) + offsetof(nsIID, m2);
			iid.m2 = XPT_SWAB16(*((PRUint16 *)ptr));
			ptr = ((unsigned char const *)buf) + offsetof(nsIID, m3);
			for (int i=0;i<8;i++) {
				iid.m3[i] = *((PRUint8 const *)ptr);
				ptr += sizeof(PRUint8);
			}
#if PY_MAJOR_VERSION >= 3 && !defined(Py_LIMITED_API)
			PyBuffer_Release(&view);
#endif
			return new Py_nsIID(iid);
		}
	}
	PyErr_Clear();
	// @pyparm string/Unicode|iidString||A string representation of an IID, or a ContractID.
	if ( !PyArg_ParseTuple(args, "O", &obIID) )
		return NULL;

	nsIID iid;
	if (!Py_nsIID::IIDFromPyObject(obIID, &iid))
		return NULL;
	return new Py_nsIID(iid);
}

/*static*/ PRBool
Py_nsIID::IIDFromPyObject(PyObject *ob, nsIID *pRet) {
	PRBool ok = PR_TRUE;
	nsIID iid;
	if (ob==NULL) {
		PyErr_SetString(PyExc_RuntimeError, "The IID object is invalid!");
		return PR_FALSE;
	}
#if PY_MAJOR_VERSION <= 2
	if (PyString_Check(ob)) {
		ok = iid.Parse(PyString_AsString(ob));
#else
	if (PyUnicode_Check(ob)) {
		ok = iid.Parse(PyUnicode_AsUTF8(ob));
#endif
		if (!ok) {
			PyXPCOM_BuildPyException(NS_ERROR_ILLEGAL_VALUE);
			return PR_FALSE;
		}
#ifndef Py_LIMITED_API
	} else if (ob->ob_type == &type) {
#else
	} else if (ob->ob_type == Py_nsIID::GetTypeObject()) {
#endif
		iid = ((Py_nsIID *)ob)->m_iid;
	} else if (PyObject_HasAttrString(ob, "__class__")) {
		// Get the _iidobj_ attribute
		PyObject *use_ob = PyObject_GetAttrString(ob, "_iidobj_");
		if (use_ob==NULL) {
			PyErr_SetString(PyExc_TypeError, "Only instances with _iidobj_ attributes can be used as IID objects");
			return PR_FALSE;
		}
#ifndef Py_LIMITED_API
		if (use_ob->ob_type != &type) {
#else
		if (use_ob->ob_type != Py_nsIID::GetTypeObject()) {
#endif
			Py_DECREF(use_ob);
			PyErr_SetString(PyExc_TypeError, "instance _iidobj_ attributes must be raw IID object");
			return PR_FALSE;
		}
		iid = ((Py_nsIID *)use_ob)->m_iid;
		Py_DECREF(use_ob);
	} else {
		PyErr_Format(PyExc_TypeError, "Objects of type '%s' can not be converted to an IID", PyXPCOM_ObTypeName(ob));
		ok = PR_FALSE;
	}
	if (ok) *pRet = iid;
	return ok;
}


// @object Py_nsIID|A Python object, representing an IID/CLSID.
// <nl>All pythoncom functions that return a CLSID/IID will return one of these
// objects.  However, in almost all cases, functions that expect a CLSID/IID
// as a param will accept either a string object, or a native Py_nsIID object.
#ifndef Py_LIMITED_API
PyTypeObject Py_nsIID::type =
{
	PyVarObject_HEAD_INIT(&PyType_Type, 0)
	"IID",
	sizeof(Py_nsIID),
	0,
	PyTypeMethod_dealloc,                           /* tp_dealloc */
	0,                                              /* tp_print */
	PyTypeMethod_getattr,                           /* tp_getattr */
	0,                                              /* tp_setattr */
#if PY_MAJOR_VERSION <= 2
	PyTypeMethod_compare,                           /* tp_compare */
#else
	0,                                              /* reserved */
#endif
	PyTypeMethod_repr,                              /* tp_repr */
	0,                                              /* tp_as_number */
	0,                                              /* tp_as_sequence */
	0,                                              /* tp_as_mapping */
	PyTypeMethod_hash,                              /* tp_hash */
	0,                                              /* tp_call */
	PyTypeMethod_str,                               /* tp_str */
	0,                                              /* tp_getattro */
	0,                                              /* tp_setattro */
	0,                                              /* tp_as_buffer */
	0,                                              /* tp_flags */
	0,                                              /* tp_doc */
	0,                                              /* tp_traverse */
	0,                                              /* tp_clear */
	PyTypeMethod_richcompare,                       /* tp_richcompare */
	0,                                              /* tp_weaklistoffset */
	0,                                              /* tp_iter */
	0,                                              /* tp_iternext */
	0,                                              /* tp_methods */
	0,                                              /* tp_members */
	0,                                              /* tp_getset */
	0,                                              /* tp_base */
};
#else  /* Py_LIMITED_API */
NS_EXPORT_STATIC_MEMBER_(PyTypeObject *) Py_nsIID::s_pType = NULL;

PyTypeObject *Py_nsIID::GetTypeObject(void)
{
	PyTypeObject *pTypeObj = Py_nsIID::s_pType;
	if (pTypeObj)
		return pTypeObj;

	PyType_Slot aTypeSlots[] = {
		{ Py_tp_base, 		&PyType_Type },
		{ Py_tp_dealloc,	(void *)(uintptr_t)&Py_nsIID::PyTypeMethod_dealloc },
		{ Py_tp_getattr,	(void *)(uintptr_t)&Py_nsIID::PyTypeMethod_getattr },
		{ Py_tp_repr,   	(void *)(uintptr_t)&Py_nsIID::PyTypeMethod_repr },
		{ Py_tp_hash,   	(void *)(uintptr_t)&Py_nsIID::PyTypeMethod_hash },
		{ Py_tp_str,    	(void *)(uintptr_t)&Py_nsIID::PyTypeMethod_str },
		{ Py_tp_richcompare,    (void *)(uintptr_t)&Py_nsIID::PyTypeMethod_richcompare },
        { Py_tp_is_gc,          (void *)(uintptr_t)&Py_nsIID::PyTypeMethod_is_gc },
		{ 0, NULL } /* terminator */
	};

    /* The parent/base class specified in Py_tp_base (&PyType_Type) here is larger
     * than the Py_nsIID derived class so we need to ensure enough space is allocated
     * for both when calling PyType_FromSpec(), i.e.: PyType_Type.tp_basicsize +
     * sizeof(Py_nsIID).  Starting with Python 3.12 when 'basicsize' is negative "the
     * absolute value specifies how much space instances of the class need in addition
     * to the superclass".  Earlier Python releases require calculating the total size
     * by hand. */
#if PY_VERSION_HEX >= 0x030C0000
    int basicsize = -(int)sizeof(Py_nsIID);
#else
    /* PyObjects are opaque in the Py_LIMITED_API so we must use PyObject_GetAttrString() here. */
    PyObject *sizeObj = PyObject_GetAttrString((PyObject*)&PyType_Type, "__basicsize__");
    int basicsize = (int)PyLong_AsLong(sizeObj) + (int)sizeof(Py_nsIID);
#endif
	PyType_Spec TypeSpec = {
		/* .name: */ 		"IID",
		/* .basicsize: */	basicsize,
		/* .itemsize: */	0,
		/* .flags: */   	Py_TPFLAGS_DEFAULT,
		/* .slots: */		aTypeSlots,
	};

	PyObject *exc_typ = NULL, *exc_val = NULL, *exc_tb = NULL;
	PyErr_Fetch(&exc_typ, &exc_val, &exc_tb); /* goes south in PyType_Ready if we don't clear exceptions first. */

	pTypeObj = (PyTypeObject *)PyType_FromSpec(&TypeSpec);
	assert(pTypeObj);

        PyErr_Restore(exc_typ, exc_val, exc_tb);
	Py_nsIID::s_pType = pTypeObj;
	return pTypeObj;
}
#endif /* Py_LIMITED_API */

Py_nsIID::Py_nsIID(const nsIID &riid)
{
#ifndef Py_LIMITED_API
	ob_type = &type;
#else
	ob_type = GetTypeObject();
#endif
#if 1 /* VBox: Must use for 3.9+, includes _Py_NewReferences. Works for all older versions too. @bugref{10079} */
	PyObject_Init(this, ob_type);
#else
	_Py_NewReference(this);
#endif

	m_iid = riid;
}

/*static*/PyObject *
Py_nsIID::PyTypeMethod_getattr(PyObject *self, char *name)
{
	Py_nsIID *me = (Py_nsIID *)self;
	if (strcmp(name, "name")==0) {
		char *iid_repr = nsnull;
		nsCOMPtr<nsIInterfaceInfoManager> iim(do_GetService(
		                NS_INTERFACEINFOMANAGER_SERVICE_CONTRACTID));
		if (iim!=nsnull)
			iim->GetNameForIID(&me->m_iid, &iid_repr);
		if (iid_repr==nsnull)
			iid_repr = me->m_iid.ToString();
		PyObject *ret;
		if (iid_repr != nsnull) {
#if PY_MAJOR_VERSION <= 2
			ret = PyString_FromString(iid_repr);
#else
			ret = PyUnicode_FromString(iid_repr);
#endif
			nsMemory::Free(iid_repr);
		} else
#if PY_MAJOR_VERSION <= 2
			ret = PyString_FromString("<cant get IID info!>");
#else
			ret = PyUnicode_FromString("<cant get IID info!>");
#endif
		return ret;
	}
	return PyErr_Format(PyExc_AttributeError, "IID objects have no attribute '%s'", name);
}

#if PY_MAJOR_VERSION <= 2
/* static */ int
Py_nsIID::PyTypeMethod_compare(PyObject *self, PyObject *other)
{
	Py_nsIID *s_iid = (Py_nsIID *)self;
	Py_nsIID *o_iid = (Py_nsIID *)other;
	int rc = memcmp(&s_iid->m_iid, &o_iid->m_iid, sizeof(s_iid->m_iid));
	return rc == 0 ? 0 : (rc < 0 ? -1 : 1);
}
#endif

/* static */ PyObject *
Py_nsIID::PyTypeMethod_richcompare(PyObject *self, PyObject *other, int op)
{
    nsIID iid;
    if (!Py_nsIID::IIDFromPyObject(other, &iid))
    {
        /* Can't do comparison besides equality/inequality if the other object does not contain an IID. */
        if (op == Py_EQ)
            Py_RETURN_FALSE;
        else if (op == Py_NE)
            Py_RETURN_TRUE;

        PyErr_SetString(PyExc_TypeError, "Comparison between different object types is not defined");
        return NULL;
    }

    PyObject *result = NULL;
	Py_nsIID *s_iid = (Py_nsIID *)self;
	int rc = memcmp(&s_iid->m_iid, &iid, sizeof(s_iid->m_iid));
    switch (op)
    {
        case Py_LT:
            result = rc < 0 ? Py_True : Py_False;
            break;
        case Py_LE:
            result = rc <= 0 ? Py_True : Py_False;
            break;
        case Py_EQ:
            result = rc == 0 ? Py_True : Py_False;
            break;
        case Py_NE:
            result = rc != 0 ? Py_True : Py_False;
            break;
        case Py_GT:
            result = rc > 0 ? Py_True : Py_False;
            break;
        case Py_GE:
            result = rc >= 0 ? Py_True : Py_False;
            break;
    }
    Py_XINCREF(result);
    return result;
}

/* static */ PyObject *
Py_nsIID::PyTypeMethod_repr(PyObject *self)
{
	Py_nsIID *s_iid = (Py_nsIID *)self;
	char buf[256];
	char *sziid = s_iid->m_iid.ToString();
#ifdef VBOX
	snprintf(buf, sizeof(buf), "_xpcom.ID('%s')", sziid);
#else
	sprintf(buf, "_xpcom.IID('%s')", sziid);
#endif
	nsMemory::Free(sziid);
#if PY_MAJOR_VERSION <= 2
	return PyString_FromString(buf);
#else
	return PyUnicode_FromString(buf);
#endif
}

/* static */ PyObject *
Py_nsIID::PyTypeMethod_str(PyObject *self)
{
	Py_nsIID *s_iid = (Py_nsIID *)self;
	char *sziid = s_iid->m_iid.ToString();
#if PY_MAJOR_VERSION <= 2
	PyObject *ret = PyString_FromString(sziid);
#else
	PyObject *ret = PyUnicode_FromString(sziid);
#endif
	nsMemory::Free(sziid);
	return ret;
}

#if PY_VERSION_HEX >= 0x03020000
/* static */Py_hash_t
Py_nsIID::PyTypeMethod_hash(PyObject *self)
#else
/* static */long
Py_nsIID::PyTypeMethod_hash(PyObject *self)
#endif
{
	const nsIID &iid = ((Py_nsIID *)self)->m_iid;

#if PY_VERSION_HEX >= 0x03020000
	Py_hash_t ret = iid.m0 + iid.m1 + iid.m2;
#else
	long ret = iid.m0 + iid.m1 + iid.m2;
#endif
	for (int i=0;i<7;i++)
		ret += iid.m3[i];
	if ( ret == -1 )
		return -2;
	return ret;
}

/*static*/ void
Py_nsIID::PyTypeMethod_dealloc(PyObject *ob)
{
	delete (Py_nsIID *)ob;
}

#ifdef Py_LIMITED_API
/* static */ int
Py_nsIID::PyTypeMethod_is_gc(PyObject *self)
{
    return 0;
}
#endif
