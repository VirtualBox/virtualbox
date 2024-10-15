/* $Id: vector.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * STL-inspired vector implementation in C
 * @note  functions in this file are inline to prevent warnings about
 *        unused static functions.  I assume that in this day and age a
 *        compiler makes its own decisions about whether to actually
 *        inline a function.
 * @note  as this header is included in rdesktop-vrdp, we do not include other
 *        required header files here (to wit assert.h, err.h, mem.h and
 *        types.h).  These must be included first.  If this moves to iprt, we
 *        should write a wrapper around it doing that.
 * @todo  can we do more of the type checking at compile time somehow?
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_vector_h
#define MAIN_INCLUDED_vector_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <stdlib.h>


/*********************************************************************************************************************************
*   Helper macros and definitions                                                                                                *
*********************************************************************************************************************************/

/** The unit by which the vector capacity is increased */
#define VECTOR_ALLOC_UNIT 16

/** Calculate a hash of a string of tokens for sanity type checking */
#define VECTOR_TOKEN_HASH(token) \
    ((unsigned) \
     (  VECTOR_TOKEN_HASH4(token, 0) \
      ^ VECTOR_TOKEN_HASH4(token, 4) \
      ^ VECTOR_TOKEN_HASH4(token, 8) \
      ^ VECTOR_TOKEN_HASH4(token, 12)))

/** Helper macro for @a VECTOR_TOKEN_HASH */
#define VECTOR_TOKEN_HASH_VALUE(token, place, mul) \
    (sizeof(#token) > place ? #token[place] * mul : 0)

/** Helper macro for @a VECTOR_TOKEN_HASH */
#define VECTOR_TOKEN_HASH4(token, place) \
      VECTOR_TOKEN_HASH_VALUE(token, place,     0x1) \
    ^ VECTOR_TOKEN_HASH_VALUE(token, place + 1, 0x100) \
    ^ VECTOR_TOKEN_HASH_VALUE(token, place + 2, 0x10000) \
    ^ VECTOR_TOKEN_HASH_VALUE(token, place + 3, 0x1000000)

/** Generic vector structure, used by @a VECTOR_OBJ and @a VECTOR_PTR */
#define VECTOR_STRUCT \
{ \
    /** The number of elements in the vector */ \
    size_t mcElements; \
    /** The current capacity of the vector */ \
    size_t mcCapacity; \
    /** The size of an element */ \
    size_t mcbElement; \
    /** Hash value of the element type */ \
    unsigned muTypeHash; \
    /** The elements themselves */ \
    void *mpvaElements; \
    /** Destructor for elements - takes a pointer to an element. */ \
    void (*mpfnCleanup)(void *); \
}

/*** Structure definitions ***/

/** A vector of values or objects */
typedef struct VECTOR_OBJ VECTOR_STRUCT VECTOR_OBJ;

/** A vector of pointer values.  (A handy special case.) */
typedef struct VECTOR_PTR VECTOR_STRUCT VECTOR_PTR;

/** Convenience macro for annotating the type of the vector.  Unfortunately the
 * type name is only cosmetic. */
/** @todo can we do something useful with the type? */
#define VECTOR_OBJ(type) VECTOR_OBJ

/** Convenience macro for annotating the type of the vector.  Unfortunately the
 * type name is only cosmetic. */
#define VECTOR_PTR(type) VECTOR_PTR

/*** Private helper functions and macros ***/

#define VEC_GET_ELEMENT_OBJ(pvaElements, cbElement, cElement) \
    ((void *)((char *)(pvaElements) + (cElement) * (cbElement)))

#define VEC_GET_ELEMENT_PTR(pvaElements, cElement) \
    (*(void **)VEC_GET_ELEMENT_OBJ(pvaElements, sizeof(void *), cElement))

/** Default cleanup function that does nothing. */
DECLINLINE(void) vecNoCleanup(void *pvElement)
{
    (void) pvElement;
}

/** Expand an existing vector, implementation */
DECLINLINE(int) vecExpand(size_t *pcCapacity, void **ppvaElements,
                            size_t cbElement)
{
    size_t cOldCap, cNewCap;
    void *pRealloc;

    cOldCap = *pcCapacity;
    cNewCap = cOldCap + VECTOR_ALLOC_UNIT;
    pRealloc = RTMemRealloc(*ppvaElements, cNewCap * cbElement);
    if (!pRealloc)
        return VERR_NO_MEMORY;
    *pcCapacity = cNewCap;
    *ppvaElements = pRealloc;
    return VINF_SUCCESS;
}

/** Expand an existing vector */
#define VEC_EXPAND(pvec) vecExpand(&(pvec)->mcCapacity, &(pvec)->mpvaElements, \
                                   (pvec)->mcbElement)

/** Reset a vector, cleaning up all its elements. */
DECLINLINE(void) vecClearObj(VECTOR_OBJ *pvec)
{
    unsigned i;

    for (i = 0; i < pvec->mcElements; ++i)
        pvec->mpfnCleanup(VEC_GET_ELEMENT_OBJ(pvec->mpvaElements,
                                              pvec->mcbElement, i));
    pvec->mcElements = 0;
}

/** Reset a pointer vector, cleaning up all its elements. */
DECLINLINE(void) vecClearPtr(VECTOR_PTR *pvec)
{
    unsigned i;

    for (i = 0; i < pvec->mcElements; ++i)
        pvec->mpfnCleanup(VEC_GET_ELEMENT_PTR(pvec->mpvaElements, i));
    pvec->mcElements = 0;
}

/** Clean up a vector */
DECLINLINE(void) vecCleanupObj(VECTOR_OBJ *pvec)
{
    vecClearObj(pvec);
    RTMemFree(pvec->mpvaElements);
    pvec->mpvaElements = NULL;
}

/** Clean up a pointer vector */
DECLINLINE(void) vecCleanupPtr(VECTOR_PTR *pvec)
{
    vecClearPtr(pvec);
    RTMemFree(pvec->mpvaElements);
    pvec->mpvaElements = NULL;
}

/** Initialise a vector structure, implementation */
#define VEC_INIT(pvec, cbElement, uTypeHash, pfnCleanup) \
    pvec->mcElements = pvec->mcCapacity = 0; \
    pvec->mcbElement = cbElement; \
    pvec->muTypeHash = uTypeHash; \
    pvec->mpfnCleanup = pfnCleanup ? pfnCleanup : vecNoCleanup; \
    pvec->mpvaElements = NULL;

/** Initialise a vector. */
DECLINLINE(void) vecInitObj(VECTOR_OBJ *pvec, size_t cbElement,
                            unsigned uTypeHash, void (*pfnCleanup)(void *))
{
    VEC_INIT(pvec, cbElement, uTypeHash, pfnCleanup)
}

/** Initialise a pointer vector. */
DECLINLINE(void) vecInitPtr(VECTOR_PTR *pvec, size_t cbElement,
                            unsigned uTypeHash, void (*pfnCleanup)(void *))
{
    VEC_INIT(pvec, cbElement, uTypeHash, pfnCleanup)
}

/** Add an element onto the end of a vector */
DECLINLINE(int) vecPushBackObj(VECTOR_OBJ *pvec, unsigned uTypeHash,
                                 void *pvElement)
{
    AssertReturn(pvec->muTypeHash == uTypeHash, VERR_INVALID_PARAMETER);
    if (pvec->mcElements == pvec->mcCapacity)
    {
        int vrc2 = VEC_EXPAND(pvec);
        if (RT_FAILURE(vrc2))
            return vrc2;
    }
    memcpy(VEC_GET_ELEMENT_OBJ(pvec->mpvaElements, pvec->mcbElement,
                               pvec->mcElements), pvElement, pvec->mcbElement);
    ++pvec->mcElements;
    return VINF_SUCCESS;
}

/** Add a pointer onto the end of a pointer vector */
DECLINLINE(int) vecPushBackPtr(VECTOR_PTR *pvec, unsigned uTypeHash,
                                 void *pv)
{
    AssertReturn(pvec->muTypeHash == uTypeHash, VERR_INVALID_PARAMETER);
    if (pvec->mcElements == pvec->mcCapacity)
    {
        int vrc2 = VEC_EXPAND(pvec);
        if (RT_FAILURE(vrc2))
            return vrc2;
    }
    VEC_GET_ELEMENT_PTR(pvec->mpvaElements, pvec->mcElements) = pv;
    ++pvec->mcElements;
    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   Public interface macros                                                                                                      *
*********************************************************************************************************************************/

/**
 * Initialise a vector structure.  Always succeeds.
 * @param   pvec        pointer to an uninitialised vector structure
 * @param   type        the type of the objects in the vector.  As this is
 *                      hashed by the preprocessor use of space etc is
 *                      important.
 * @param   pfnCleanup  cleanup function (void (*pfn)(void *)) that is called
 *                      on a pointer to a vector element when that element is
 *                      dropped
 */
#define VEC_INIT_OBJ(pvec, type, pfnCleanup) \
    vecInitObj(pvec, sizeof(type), VECTOR_TOKEN_HASH(type), \
               (void (*)(void*)) pfnCleanup)

/**
 * Initialise a vector-of-pointers structure.  Always succeeds.
 * @param   pvec        pointer to an uninitialised vector structure
 * @param   type        the type of the pointers in the vector, including the
 *                      final "*".  As this is hashed by the preprocessor use
 *                      of space etc is important.
 * @param   pfnCleanup  cleanup function (void (*pfn)(void *)) that is called
 *                      directly on a vector element when that element is
 *                      dropped
 */
#define VEC_INIT_PTR(pvec, type, pfnCleanup) \
    vecInitPtr(pvec, sizeof(type), VECTOR_TOKEN_HASH(type), \
               (void (*)(void*)) pfnCleanup)

/**
 * Clean up a vector.
 * @param pvec  pointer to the vector to clean up.  The clean up function
 *              specified at initialisation (if any) is called for each element
 *              in the vector.  After clean up, the vector structure is invalid
 *              until it is re-initialised
 */
#define VEC_CLEANUP_OBJ vecCleanupObj

/**
 * Clean up a vector-of-pointers.
 * @param pvec  pointer to the vector to clean up.  The clean up function
 *              specified at initialisation (if any) is called for each element
 *              in the vector.  After clean up, the vector structure is invalid
 *              until it is re-initialised
 */
#define VEC_CLEANUP_PTR vecCleanupPtr

/**
 * Reinitialises a vector structure to empty.
 * @param  pvec  pointer to the vector to re-initialise.  The clean up function
 *               specified at initialisation (if any) is called for each element
 *               in the vector.
 */
#define VEC_CLEAR_OBJ vecClearObj

/**
 * Reinitialises a vector-of-pointers structure to empty.
 * @param  pvec  pointer to the vector to re-initialise.  The clean up function
 *               specified at initialisation (if any) is called for each element
 *               in the vector.
 */
#define VEC_CLEAR_PTR vecClearPtr

/**
 * Adds an element to the back of a vector.  The element will be byte-copied
 * and become owned by the vector, to be cleaned up by the vector's clean-up
 * routine when the element is dropped.
 * @returns iprt status code (VINF_SUCCESS or VERR_NO_MEMORY)
 * @returns VERR_INVALID_PARAMETER if the type does not match the type given
 *          when the vector was initialised (asserted)
 * @param pvec       pointer to the vector on to which the element should be
 *                   added
 * @param type       the type of the vector as specified at initialisation.
 *                   Spacing etc is important.
 * @param pvElement  void pointer to the element to be added
 */
#define VEC_PUSH_BACK_OBJ(pvec, type, pvElement) \
    vecPushBackObj(pvec, VECTOR_TOKEN_HASH(type), \
                   (pvElement) + ((pvElement) - (type *)(pvElement)))

/**
 * Adds a pointer to the back of a vector-of-pointers.  The pointer will become
 * owned by the vector, to be cleaned up by the vector's clean-up routine when
 * it is dropped.
 * @returns iprt status code (VINF_SUCCESS or VERR_NO_MEMORY)
 * @returns VERR_INVALID_PARAMETER if the type does not match the type given
 *          when the vector was initialised (asserted)
 * @param pvec       pointer to the vector on to which the element should be
 *                   added
 * @param type       the type of the vector as specified at initialisation.
 *                   Spacing etc is important.
 * @param pvElement  the pointer to be added, typecast to pointer-to-void
 */
#define VEC_PUSH_BACK_PTR(pvec, type, pvElement) \
    vecPushBackPtr(pvec, VECTOR_TOKEN_HASH(type), \
                   (pvElement) + ((pvElement) - (type)(pvElement)))

/**
 * Returns the count of elements in a vector.
 * @param  pvec  pointer to the vector.
 */
#define VEC_SIZE_OBJ(pvec) (pvec)->mcElements

/**
 * Returns the count of elements in a vector-of-pointers.
 * @param  pvec  pointer to the vector.
 */
#define VEC_SIZE_PTR VEC_SIZE_OBJ

/**
 * Iterates over a vector.
 *
 * Iterates over the vector elements from first to last and execute the
 * following instruction or block on each iteration with @a pIterator set to
 * point to the current element (that is, a pointer to the pointer element for
 * a vector-of-pointers).  Use in the same way as a "for" statement.
 *
 * @param pvec       Pointer to the vector to be iterated over.
 * @param type       The type of the vector, must match the type specified at
 *                   vector initialisation (including whitespace etc).
 * @param pIterator  A pointer to @a type which will be set to point to the
 *                   current vector element on each iteration.
 *
 * @todo  can we assert the correctness of the type in some way?
 */
#define VEC_FOR_EACH(pvec, type, pIterator) \
    for (pIterator = (type *) (pvec)->mpvaElements; \
            (pvec)->muTypeHash == VECTOR_TOKEN_HASH(type) \
         && pIterator < (type *) (pvec)->mpvaElements + (pvec)->mcElements; \
         ++pIterator)

#endif /* !MAIN_INCLUDED_vector_h */
