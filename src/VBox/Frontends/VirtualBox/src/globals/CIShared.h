/* $Id: CIShared.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Common VirtualBox classes: CIShared class declaration.
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

#ifndef FEQT_INCLUDED_SRC_globals_CIShared_h
#define FEQT_INCLUDED_SRC_globals_CIShared_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifdef VBOX_CHECK_STATE
#include <stdio.h>
#endif

template< class D >
class CIShared
{
    /** @internal
     *
     *  A class that derives the data structure managed by the CIShared template
     *  (passed as a template parameter) for some internal purposes, such as the
     *  reference count, etc. There is no need to use this class directly.
     */
    class Data : public D
    {
        enum { Orig = 0x01, Null = 0x02 };

        Data() : cnt( 1 ), state( Orig ) {}
        Data( const Data &d ) : D( d ), cnt( 1 ), state( d.state & (~Orig) ) {}
        Data &operator=( const Data &d ) {
            D::operator=( d );
            state &= ~Orig;
            return *this;
        }
        // a special constructor to create a null value
        Data( void* ) : cnt( 1 ), state( Null ) {}
#ifdef VBOX_CHECK_STATE
        virtual ~Data();
        void ref();
        bool deref();
#else
        virtual ~Data() {}
        void ref() { cnt++; }
        bool deref() { return !--cnt; }
#endif // VBOX_CHECK_STATE

        int cnt;
        int state;

        friend class CIShared<D>;
    };

public:
    CIShared( bool null = true ) : d( null ? Null.d->ref(), Null.d : new Data() ) {}
    CIShared( const CIShared &that ) : d( that.d ) { d->ref(); }
    CIShared &operator=( const CIShared &that ) {
        that.d->ref();
        if ( d->deref() ) delete d;
        d = that.d;
        return *this;
    }
    virtual ~CIShared() { if ( d->deref() ) delete d; }

    bool isOriginal() const { return (d->state != 0); }
    bool isNull() const { return ((d->state & Data::Null) != 0); }

    bool detach();
    bool detachOriginal();

    CIShared copy() const {
        return isNull() ? CIShared( Null ) : CIShared( new Data( *d ) );
    }

    const D *data() const { return d; }
    inline D *mData();

    bool operator==( const CIShared &that ) const {
        return (d == that.d) || (*d == *(that.d));
    }

    // convenience operators
    const D *operator->() const { return data(); }
    bool operator!() const { return isNull(); }

private:
    CIShared( Data *aData ) : d( aData ) {}
    Data *d;

    static CIShared Null;
};

/** @class CIShared
 *
 *  This template allows to implement the implicit sharing
 *  semantics for user-defined data structures.
 *
 *  The template argument is a structure (or a class) whose objects
 *  need to be implicitly shared by different pieces of code. A class
 *  generated from this template acts as a wrapper for that structure
 *  and provides a safe access (from the shared usage point of view) to its
 *  members. Note that simple C++ types (such as int) cannot be used as
 *  template arguments.
 *
 *  Implicit sharing means that instances of the generated class point to the
 *  same data object of the managed structure until any one of them tries
 *  to change it. When it happens that instance makes a deep copy of the object
 *  (through its copy constructor) and does the actual change on that copy,
 *  keeping the original data unchanged. This technique is also called
 *  "copy on write". Also, any instance can excplicitly stop sharing the data
 *  it references at any time by calling the detach() method directly, which
 *  makes a copy if the data is referenced by more than one instance.
 *
 *  The read-only access to the managed data can be obtained using the
 *  data() method that returns a pointer to the constant data of the type
 *  used as a template argument. The pointer to the non-constant data
 *  is returned by the mData() method, that automatically detaches the
 *  instance if necessary. This method should be used with care, and only
 *  when it is really necessary to change the data -- if you will use it for
 *  the read-only access the implicit sharing will not work because every
 *  instance will have its data detached.
 *
 *  To be able to be used with the VShared template the structure/class
 *  must have public (or protected) constructors and a destructor. If it
 *  doesn't contain pointers as its members then the two constructors
 *  (the default and the copy constructor) and the destructor automatically
 *  generated by the compiler are enough, there's no need to define them
 *  explicitly. If the destructor is defined explicitly it must be
 *  virtual.
 *
 *  The default constructor implemented by this template (it is actually
 *  a constructor with one bool argument that defaults to false) creates
 *  a null instance (i.e. its isNull() method returns false). All null
 *  instances share the same internal data object (created by the default
 *  constructor of the managed structure) and provide only a read-only access
 *  to its members. This means that the mData() method of such an instance
 *  will always return a null pointer and an attempt to access its members
 *  through that pointer will most likely cause a memory access violation
 *  exception. The template doesn't provide any other constructors (except
 *  the copy constructor) because it doesn't know how to initialize the
 *  object of the managed structure, so the only way to create a non-null
 *  instance is to pass true to the constructor mentioned above.
 *
 *  It's a good practice not to use instantiations of this template directly
 *  but derive them instead. This gives an opportunity to define necessary
 *  constructors with arguments that initialize the managed structure, as
 *  well as to define convenient methods to access structure members (instead
 *  of defining them in the structure itself). For example:
 *
 *  @code
 *
 *  // a data structure
 *  struct ACardData {
 *      string name;
 *      // commented out -- not so convenient:
 *      // void setName( const string &n ) { name = n; }
 *  }
 *
 *  // a wrapper
 *  class ACard : publc CIShared< ACardData > {
 *      ACardData() {}  // the default constructor should be visible
 *      ACardData( const string &name ) :
 *          CIShared< ACardData >( false ) // make non-null
 *      {
 *          mData()->name = name;
 *      }
 *      string name() const { return data()->name; }
 *      void setName( const string &name ) { mData()->name = name; }
 *  }
 *
 *  // ...
 *  ACard c( "John" );
 *  // ...
 *  c.setName( "Ivan" );
 *  // the above is shorter than c.data()->name or c.mData()->setName()
 *
 *  @endcode
 *
 *  If some members of the structure need to be private (and therefore
 *  inaccessible through the pointers returned by data() and vData()) you can
 *  simply declare the wrapper class (the ACard class in the example above)
 *  as a friend of the structure and still use the above approach.
 *
 *  For public members of the original structure it's also possible to use
 *  the overloaded operator->(), which is the equivalent of calling the data()
 *  method, i.e.:
 *
 *  @code
 *  // ...
 *  cout << c->name;
 *  @endcode
 *
 *  The operator!() is overloaded for convenience and is equivalent to the
 *  isNull() method.
 *
 *  The operator==() makes a comparison of two instances.
 *
 *  @todo put the "original" state definition here...
 */

/** @internal
 *
 *  A special null value for internal usage. All null instances created
 *  with the default constructor share the data object it contains.
 */
template< class D > CIShared<D> CIShared<D>::Null = CIShared( new Data( 0 ) );

/** @fn CIShared::CIShared( bool null = true )
 *
 *  Creates a new instance. If the argument is true (which is the default)
 *  a null instance is created. All null instances share the same data
 *  object created using the default constructor of the managed structure
 *  (i.e. specified as template argument when instantiating).
 *
 *  If the argument is false an empty instance is created. The empty instance
 *  differs from the null instance such that the created data object is
 *  initially non-shared and the mData() method returns a valid pointer
 *  suitable for modifying the data.
 *
 *  The instance created by this constructor is initially original.
 *
 *  @see isNull, isOriginal
 */

/** @fn CIShared::CIShared( const CIShared & )
 *
 *  Creates a new instance and initializes it by a reference to the same data
 *  object as managed by the argument. No copies of the data are created.
 *  The created instance becomes null and/or original if the argument is null
 *  and/or original, respectively.
 *
 *  @see isNull, isOriginal
 */

/** @fn CIShared::operator=( const CIShared & )
 *
 *  Assigns a new value to this instance by instructing it to refer to the
 *  same data as managed by the argument. No copies of the data are created.
 *  The previous data is automatically deleted if there are no more references
 *  to it. The instance becomes null and/or original if the argument is null
 *  and/or original, respectively.
 */

/** @fn CIShared::copy() const
 *
 *  Returns a "deep" copy of the instance. The returned instance always
 *  contains its own (not yet shared) copy of the managed data, even if the
 *  data wasn't shared before this call. The new copy becomes not original
 *  if it is not null, otherwise it remains null.
 *
 *  @see isNull, isOriginal
 */

/** @fn CIShared::data() const
 *
 *  Returns a pointer to the object of the managed structure that is suitable
 *  for a read-only access. Does <b>not</b> do an implicit detach(), the
 *  data remains shared.
 *
 *  @see mData()
 */

/** @fn CIShared::operator==( const CIShared & ) const
 *
 *  Compares this instance and the argument. Two instances are considered
 *  to be equal if they share the same data object or if data objects they
 *  share are equal. Data objects are compared using the comparison operator
 *  of the managed structure.
 */

/**
 *  Detaches this instance from other instances it shares the data with by
 *  making the copy of the data. This instance becomes "non-original". The
 *  method does nothing and returns false if this instance is null or its
 *  data is not shared among (referenced by) other instances.
 *
 *  @return true if it does a real detach and false otherwise.
 *
 *  @see isOriginal, isNull
 */
template< class D > bool CIShared<D>::detach() {
    if ( !(d->state & Data::Null) && d->cnt > 1 ) {
        d->deref();
        d = new Data( *d );
        return true;
    }
    return false;
}

/**
 *  Detaches this instance from other instances it shares the data with by
 *  making the copy of the data. This instance becomes "original" (even if
 *  it wasn't original before a detach), all other instances that previously
 *  shared the same data will become "non-original". The method does nothing
 *  and returns false if this instance is null. If its data is not shared
 *  among (referenced by) other instances it marks it as original and
 *  also returns false.
 *
 *  @return true if it does a real detach and false otherwise.
 *
 *  @see isOriginal, isNull
 */
template< class D > bool CIShared<D>::detachOriginal() {
    if ( !(d->state & Data::Null) ) {
        if ( d->cnt > 1 ) {
            d->deref();
            d->state &= ~Data::Orig;
            d = new Data( *d );
            d->state |= Data::Orig;
            return true;
        }
        d->state |= Data::Orig;
    }
    return false;
}

/** @fn CIShared::isOriginal() const
 *
 *  Returns true if the data is the original data and false otherwise.
 *  The data is considered to be original until it is changed through the
 *  mData() member or directly detached by detach(). Also, the data can be
 *  made original at any time using the detachOriginal() method.
 *
 *  Note, that this method always returns true for null instances.
 *
 *  @see detachOriginal, isNull
 */

/** @fn CIShared::isNull() const
 *
 *  Returns true if this instance is a special null value. All null values
 *  share the same data object created by the default constructor of
 *  the managed structure. A null instance gives a read-only access to the
 *  managed data.
 *
 *  @see vData
 */

/**
 *  Returns a pointer to the object of the managed structure that is suitable
 *  for modifying data. Does an implicit detach() if this data object is
 *  referenced by more than one instance, making this instance non-original.
 *
 *  This method should be called only when it's really necessary to change
 *  the data object, read-only access should be obtained using the data()
 *  member. Otherwise there all data objects will be detached and non-shared.
 *
 *  @warning This method returns a null pointer for instances that are
 *  null. Accessing data through that pointer will most likely cause a
 *  memory access violation exception.
 *
 *  @see data, isNull, isOriginal
 */
template< class D > inline D *CIShared<D>::mData() {
    if ( d->state & Data::Null ) {
#ifdef VBOX_CHECK_STATE
        printf( "CIShared::mData(): a null instance, returning a null pointer!" );
#endif
        return 0;
    }
    if ( d->cnt > 1 )
        detach();
    return d;
}

// CIShared<D>::Data debug methods
/////////////////////////////////////////////////////////////////////////////

#ifdef VBOX_CHECK_STATE

template< class D > CIShared<D>::Data::~Data() {
    if ( cnt )
        printf( "~Data(): ref count is %d, but must be zero!\n", cnt );
}

template< class D > void CIShared<D>::Data::ref() {
    if ( cnt <= 0 )
        printf(
            "Data::ref() ref count was %d, "
            "but must be greater than zero!\n",
            cnt
        );
    cnt++;
}

template< class D > bool CIShared<D>::Data::deref() {
    if ( cnt <= 0 )
        printf(
            "Data::ref() ref count was %d, "
            "but must be greater than zero!\n",
            cnt
        );
    return !--cnt;
}

#endif // VBOX_CHECK_STATE

#endif /* !FEQT_INCLUDED_SRC_globals_CIShared_h */

