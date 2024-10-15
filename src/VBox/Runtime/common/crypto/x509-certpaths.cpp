/* $Id: x509-certpaths.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - X.509, Simple Certificate Path Builder & Validator.
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
#define LOG_GROUP RTLOGGROUP_CRYPTO
#include "internal/iprt.h"
#include <iprt/crypto/x509.h>

#include <iprt/asm.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/list.h>
#include <iprt/log.h>
#include <iprt/time.h>
#include <iprt/crypto/applecodesign.h> /* critical extension OIDs */
#include <iprt/crypto/pkcs7.h> /* PCRTCRPKCS7SETOFCERTS */
#include <iprt/crypto/store.h>

#include "x509-internal.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * X.509 certificate path node.
 */
typedef struct RTCRX509CERTPATHNODE
{
    /** Sibling list entry. */
    RTLISTNODE                      SiblingEntry;
    /** List of children or leaf list entry. */
    RTLISTANCHOR                    ChildListOrLeafEntry;
    /** Pointer to the parent node.  NULL for root. */
    struct RTCRX509CERTPATHNODE    *pParent;

    /** The distance between this node and the target.  */
    uint32_t                        uDepth : 8;
    /** Indicates the source of this certificate.  */
    uint32_t                        uSrc : 3;
    /** Set if this is a leaf node. */
    uint32_t                        fLeaf : 1;
    /** Makes sure it's a 32-bit bitfield. */
    uint32_t                        uReserved : 20;

    /** Leaf only: The result of the last path vertification. */
    int                             rcVerify;

    /** Pointer to the certificate.  This can be NULL only for trust anchors. */
    PCRTCRX509CERTIFICATE           pCert;

    /** If the certificate or trust anchor was obtained from a store, this is the
     * associated certificate context (referenced of course).  This is used to
     * access the trust anchor information, if present.
     *
     * (If this is NULL it's from a certificate array or some such given directly to
     * the path building code.  It's assumed the caller doesn't free these until the
     * path validation/whatever is done with and the paths destroyed.) */
    PCRTCRCERTCTX                   pCertCtx;
} RTCRX509CERTPATHNODE;
/** Pointer to a X.509 path node. */
typedef RTCRX509CERTPATHNODE *PRTCRX509CERTPATHNODE;

/** @name RTCRX509CERTPATHNODE::uSrc values.
 * The trusted and untrusted sources ordered in priority order, where higher
 * number means high priority in case of duplicates.
 * @{ */
#define RTCRX509CERTPATHNODE_SRC_NONE               0
#define RTCRX509CERTPATHNODE_SRC_TARGET             1
#define RTCRX509CERTPATHNODE_SRC_UNTRUSTED_SET      2
#define RTCRX509CERTPATHNODE_SRC_UNTRUSTED_ARRAY    3
#define RTCRX509CERTPATHNODE_SRC_UNTRUSTED_STORE    4
#define RTCRX509CERTPATHNODE_SRC_TRUSTED_STORE      5
#define RTCRX509CERTPATHNODE_SRC_TRUSTED_CERT       6
#define RTCRX509CERTPATHNODE_SRC_IS_TRUSTED(uSrc)   ((uSrc) >= RTCRX509CERTPATHNODE_SRC_TRUSTED_STORE)
/** @} */


/**
 * Policy tree node.
 */
typedef struct RTCRX509CERTPATHSPOLICYNODE
{
    /** Sibling list entry. */
    RTLISTNODE                      SiblingEntry;
    /** Tree depth list entry. */
    RTLISTNODE                      DepthEntry;
    /** List of children or leaf list entry. */
    RTLISTANCHOR                    ChildList;
    /** Pointer to the parent. */
    struct RTCRX509CERTPATHSPOLICYNODE *pParent;

    /** The policy object ID. */
    PCRTASN1OBJID                   pValidPolicy;

    /** Optional sequence of policy qualifiers. */
    PCRTCRX509POLICYQUALIFIERINFOS  pPolicyQualifiers;

    /** The first policy ID in the exepcted policy set. */
    PCRTASN1OBJID                   pExpectedPolicyFirst;
    /** Set if we've already mapped pExpectedPolicyFirst. */
    bool                            fAlreadyMapped;
    /** Number of additional items in the expected policy set. */
    uint32_t                        cMoreExpectedPolicySet;
    /** Additional items in the expected policy set.  */
    PCRTASN1OBJID                  *papMoreExpectedPolicySet;
} RTCRX509CERTPATHSPOLICYNODE;
/** Pointer to a policy tree node. */
typedef RTCRX509CERTPATHSPOLICYNODE *PRTCRX509CERTPATHSPOLICYNODE;


/**
 * Path builder and validator instance.
 *
 * The path builder creates a tree of certificates by forward searching from the
 * end-entity towards a trusted source.  The leaf nodes are inserted into list
 * ordered by the source of the leaf certificate and the path length (i.e. tree
 * depth).
 *
 * The path validator works the tree from the leaf end and validates each
 * potential path found by the builder.  It is generally happy with one working
 * path, but may be told to verify all of them.
 */
typedef struct RTCRX509CERTPATHSINT
{
    /** Magic number. */
    uint32_t                        u32Magic;
    /** Reference counter. */
    uint32_t volatile               cRefs;

    /** @name Input
     * @{ */
    /** The target certificate (end entity) to build a trusted path for. */
    PCRTCRX509CERTIFICATE           pTarget;

    /** Lone trusted certificate.  */
    PCRTCRX509CERTIFICATE           pTrustedCert;
    /** Store of trusted certificates. */
    RTCRSTORE                       hTrustedStore;

    /** Store of untrusted certificates. */
    RTCRSTORE                       hUntrustedStore;
    /** Array of untrusted certificates, typically from the protocol. */
    PCRTCRX509CERTIFICATE           paUntrustedCerts;
    /** Number of entries in paUntrusted. */
    uint32_t                        cUntrustedCerts;
    /** Set of untrusted PKCS \#7 / CMS certificatess. */
    PCRTCRPKCS7SETOFCERTS           pUntrustedCertsSet;

    /** UTC time we're going to validate the path at, requires
     *  RTCRX509CERTPATHSINT_F_VALID_TIME to be set. */
    RTTIMESPEC                      ValidTime;
    /** Number of policy OIDs in the user initial policy set, 0 means anyPolicy. */
    uint32_t                        cInitialUserPolicySet;
    /** The user initial policy set.  As with all other user provided data, we
     * assume it's immutable and remains valid for the usage period of the path
     * builder & validator. */
    PCRTASN1OBJID                  *papInitialUserPolicySet;
    /** Number of certificates before the user wants an explicit policy result.
     * Set to UINT32_MAX no explicit policy restriction required by the user. */
    uint32_t                        cInitialExplicitPolicy;
    /** Number of certificates before the user wants policy mapping to be
     * inhibited.  Set to UINT32_MAX if no initial policy mapping inhibition
     * desired by the user. */
    uint32_t                        cInitialPolicyMappingInhibit;
    /** Number of certificates before the user wants the anyPolicy to be rejected.
     * Set to UINT32_MAX no explicit policy restriction required by the user. */
    uint32_t                        cInitialInhibitAnyPolicy;
    /** Initial name restriction: Permitted subtrees.  */
    PCRTCRX509GENERALSUBTREES       pInitialPermittedSubtrees;
    /** Initial name restriction: Excluded subtrees.  */
    PCRTCRX509GENERALSUBTREES       pInitialExcludedSubtrees;

    /** Flags RTCRX509CERTPATHSINT_F_XXX. */
    uint32_t                        fFlags;
    /** @} */

    /** Sticky status for remembering allocation errors and the like.  */
    int32_t                         rc;
    /** Where to store extended error info (optional). */
    PRTERRINFO                      pErrInfo;

    /** @name Path Builder Output
     * @{  */
    /** Pointer to the root of the tree.  This will always be non-NULL after path
     *  building and thus can be reliably used to tell if path building has taken
     *  place or not. */
    PRTCRX509CERTPATHNODE           pRoot;
    /** List of working leaf tree nodes. */
    RTLISTANCHOR                    LeafList;
    /** The number of paths (leafs). */
    uint32_t                        cPaths;
    /** @} */

    /** Path Validator State. */
    struct
    {
        /** Number of nodes in the certificate path we're validating (aka 'n'). */
        uint32_t                        cNodes;
        /** The current node (0 being the trust anchor). */
        uint32_t                        iNode;

        /** The root node of the valid policy tree. */
        PRTCRX509CERTPATHSPOLICYNODE    pValidPolicyTree;
        /** An array of length cNodes + 1 which tracks all nodes at the given (index)
         *  tree depth via the RTCRX509CERTPATHSPOLICYNODE::DepthEntry member. */
        PRTLISTANCHOR                   paValidPolicyDepthLists;

        /** Number of entries in paPermittedSubtrees (name constraints).
         * If zero, no permitted name constrains currently in effect.  */
        uint32_t                        cPermittedSubtrees;
        /** The allocated size of papExcludedSubtrees */
        uint32_t                        cPermittedSubtreesAlloc;
        /** Array of permitted subtrees we've collected so far (name constraints). */
        PCRTCRX509GENERALSUBTREE       *papPermittedSubtrees;
        /** Set if we end up with an empty set after calculating a name constraints
         * union.  */
        bool                            fNoPermittedSubtrees;

        /** Number of entries in paExcludedSubtrees (name constraints).
         * If zero, no excluded name constrains currently in effect.  */
        uint32_t                        cExcludedSubtrees;
        /** Array of excluded subtrees we've collected so far (name constraints). */
        PCRTCRX509GENERALSUBTREES      *papExcludedSubtrees;

        /** Number of non-self-issued certificates to be processed before a non-NULL
         * paValidPolicyTree is required. */
        uint32_t                        cExplicitPolicy;
        /** Number of non-self-issued certificates to be processed we stop processing
         * policy mapping extensions. */
        uint32_t                        cInhibitPolicyMapping;
        /** Number of non-self-issued certificates to be processed before a the
         * anyPolicy is rejected. */
        uint32_t                        cInhibitAnyPolicy;
        /** Number of non-self-issued certificates we're allowed to process. */
        uint32_t                        cMaxPathLength;

        /** The working issuer name. */
        PCRTCRX509NAME                  pWorkingIssuer;
        /** The working public key algorithm ID. */
        PCRTASN1OBJID                   pWorkingPublicKeyAlgorithm;
        /** The working public key algorithm parameters. */
        PCRTASN1DYNTYPE                 pWorkingPublicKeyParameters;
        /** A bit string containing the public key. */
        PCRTASN1BITSTRING               pWorkingPublicKey;
    } v;

    /** An object identifier initialized to anyPolicy. */
    RTASN1OBJID                         AnyPolicyObjId;

    /** Temporary scratch space. */
    char                                szTmp[1024];
} RTCRX509CERTPATHSINT;
typedef RTCRX509CERTPATHSINT *PRTCRX509CERTPATHSINT;

/** Magic value for RTCRX509CERTPATHSINT::u32Magic (Bruce Schneier). */
#define RTCRX509CERTPATHSINT_MAGIC                          UINT32_C(0x19630115)

/** @name RTCRX509CERTPATHSINT_F_XXX - Certificate path build flags.
 * @{ */
#define RTCRX509CERTPATHSINT_F_VALID_TIME                   RT_BIT_32(0)
#define RTCRX509CERTPATHSINT_F_ELIMINATE_UNTRUSTED_PATHS    RT_BIT_32(1)
/** Whether checking the trust anchor signature (if self signed) and
 * that it is valid at the verification time, also require it to be a CA if not
 * leaf node. */
#define RTCRX509CERTPATHSINT_F_CHECK_TRUST_ANCHOR           RT_BIT_32(2)
#define RTCRX509CERTPATHSINT_F_VALID_MASK                   UINT32_C(0x00000007)
/** @} */


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void rtCrX509CertPathsDestroyTree(PRTCRX509CERTPATHSINT pThis);
static void rtCrX509CpvCleanup(PRTCRX509CERTPATHSINT pThis);


/** @name Path Builder and Validator Config APIs
 * @{
 */

RTDECL(int) RTCrX509CertPathsCreate(PRTCRX509CERTPATHS phCertPaths, PCRTCRX509CERTIFICATE pTarget)
{
    AssertPtrReturn(phCertPaths, VERR_INVALID_POINTER);

    PRTCRX509CERTPATHSINT pThis = (PRTCRX509CERTPATHSINT)RTMemAllocZ(sizeof(*pThis));
    if (pThis)
    {
        int rc = RTAsn1ObjId_InitFromString(&pThis->AnyPolicyObjId, RTCRX509_ID_CE_CP_ANY_POLICY_OID, &g_RTAsn1DefaultAllocator);
        if (RT_SUCCESS(rc))
        {
            pThis->u32Magic                     = RTCRX509CERTPATHSINT_MAGIC;
            pThis->cRefs                        = 1;
            pThis->pTarget                      = pTarget;
            pThis->hTrustedStore                = NIL_RTCRSTORE;
            pThis->hUntrustedStore              = NIL_RTCRSTORE;
            pThis->cInitialExplicitPolicy       = UINT32_MAX;
            pThis->cInitialPolicyMappingInhibit = UINT32_MAX;
            pThis->cInitialInhibitAnyPolicy     = UINT32_MAX;
            pThis->rc                           = VINF_SUCCESS;
            RTListInit(&pThis->LeafList);
            *phCertPaths = pThis;
            return VINF_SUCCESS;
        }
        return rc;
    }
    return VERR_NO_MEMORY;
}


RTDECL(uint32_t) RTCrX509CertPathsRetain(RTCRX509CERTPATHS hCertPaths)
{
    PRTCRX509CERTPATHSINT pThis = hCertPaths;
    AssertPtrReturn(pThis, UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs > 0 && cRefs < 64);
    return cRefs;
}


RTDECL(uint32_t) RTCrX509CertPathsRelease(RTCRX509CERTPATHS hCertPaths)
{
    uint32_t cRefs;
    if (hCertPaths != NIL_RTCRX509CERTPATHS)
    {
        PRTCRX509CERTPATHSINT pThis = hCertPaths;
        AssertPtrReturn(pThis, UINT32_MAX);
        AssertReturn(pThis->u32Magic == RTCRX509CERTPATHSINT_MAGIC, UINT32_MAX);

        cRefs = ASMAtomicDecU32(&pThis->cRefs);
        Assert(cRefs < 64);
        if (!cRefs)
        {
            /*
             * No more references, destroy the whole thing.
             */
            ASMAtomicWriteU32(&pThis->u32Magic, ~RTCRX509CERTPATHSINT_MAGIC);

            /* config */
            pThis->pTarget                      = NULL; /* Referencing user memory. */
            pThis->pTrustedCert                 = NULL; /* Referencing user memory. */
            RTCrStoreRelease(pThis->hTrustedStore);
            pThis->hTrustedStore                = NIL_RTCRSTORE;
            RTCrStoreRelease(pThis->hUntrustedStore);
            pThis->hUntrustedStore              = NIL_RTCRSTORE;
            pThis->paUntrustedCerts             = NULL; /* Referencing user memory. */
            pThis->pUntrustedCertsSet           = NULL; /* Referencing user memory. */
            pThis->papInitialUserPolicySet      = NULL; /* Referencing user memory. */
            pThis->pInitialPermittedSubtrees    = NULL; /* Referencing user memory. */
            pThis->pInitialExcludedSubtrees     = NULL; /* Referencing user memory. */

            /* builder */
            rtCrX509CertPathsDestroyTree(pThis);

            /* validator */
            rtCrX509CpvCleanup(pThis);

            /* misc */
            RTAsn1VtDelete(&pThis->AnyPolicyObjId.Asn1Core);

            /* Finally, the instance itself. */
            RTMemFree(pThis);
        }
    }
    else
        cRefs = 0;
    return cRefs;
}



RTDECL(int) RTCrX509CertPathsSetTrustedStore(RTCRX509CERTPATHS hCertPaths, RTCRSTORE hTrustedStore)
{
    PRTCRX509CERTPATHSINT pThis = hCertPaths;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTCRX509CERTPATHSINT_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pThis->pRoot == NULL, VERR_WRONG_ORDER);

    if (pThis->hTrustedStore != NIL_RTCRSTORE)
    {
        RTCrStoreRelease(pThis->hTrustedStore);
        pThis->hTrustedStore = NIL_RTCRSTORE;
    }
    if (hTrustedStore != NIL_RTCRSTORE)
    {
        AssertReturn(RTCrStoreRetain(hTrustedStore) != UINT32_MAX, VERR_INVALID_HANDLE);
        pThis->hTrustedStore = hTrustedStore;
    }
    return VINF_SUCCESS;
}


RTDECL(int) RTCrX509CertPathsSetUntrustedStore(RTCRX509CERTPATHS hCertPaths, RTCRSTORE hUntrustedStore)
{
    PRTCRX509CERTPATHSINT pThis = hCertPaths;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTCRX509CERTPATHSINT_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pThis->pRoot == NULL, VERR_WRONG_ORDER);

    if (pThis->hUntrustedStore != NIL_RTCRSTORE)
    {
        RTCrStoreRelease(pThis->hUntrustedStore);
        pThis->hUntrustedStore = NIL_RTCRSTORE;
    }
    if (hUntrustedStore != NIL_RTCRSTORE)
    {
        AssertReturn(RTCrStoreRetain(hUntrustedStore) != UINT32_MAX, VERR_INVALID_HANDLE);
        pThis->hUntrustedStore = hUntrustedStore;
    }
    return VINF_SUCCESS;
}


RTDECL(int) RTCrX509CertPathsSetUntrustedArray(RTCRX509CERTPATHS hCertPaths, PCRTCRX509CERTIFICATE paCerts, uint32_t cCerts)
{
    PRTCRX509CERTPATHSINT pThis = hCertPaths;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTCRX509CERTPATHSINT_MAGIC, VERR_INVALID_HANDLE);

    pThis->paUntrustedCerts = paCerts;
    pThis->cUntrustedCerts  = cCerts;
    return VINF_SUCCESS;
}


RTDECL(int) RTCrX509CertPathsSetUntrustedSet(RTCRX509CERTPATHS hCertPaths, PCRTCRPKCS7SETOFCERTS pSetOfCerts)
{
    PRTCRX509CERTPATHSINT pThis = hCertPaths;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTCRX509CERTPATHSINT_MAGIC, VERR_INVALID_HANDLE);

    pThis->pUntrustedCertsSet = pSetOfCerts;
    return VINF_SUCCESS;
}


RTDECL(int) RTCrX509CertPathsSetValidTime(RTCRX509CERTPATHS hCertPaths, PCRTTIME pTime)
{
    PRTCRX509CERTPATHSINT pThis = hCertPaths;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTCRX509CERTPATHSINT_MAGIC, VERR_INVALID_HANDLE);

    /* Allow this after building paths, as it's only used during verification. */

    if (pTime)
    {
        if (RTTimeImplode(&pThis->ValidTime, pTime))
            return VERR_INVALID_PARAMETER;
        pThis->fFlags |= RTCRX509CERTPATHSINT_F_VALID_TIME;
    }
    else
        pThis->fFlags &= ~RTCRX509CERTPATHSINT_F_VALID_TIME;
    return VINF_SUCCESS;
}


RTDECL(int) RTCrX509CertPathsSetValidTimeSpec(RTCRX509CERTPATHS hCertPaths, PCRTTIMESPEC pTimeSpec)
{
    PRTCRX509CERTPATHSINT pThis = hCertPaths;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTCRX509CERTPATHSINT_MAGIC, VERR_INVALID_HANDLE);

    /* Allow this after building paths, as it's only used during verification. */

    if (pTimeSpec)
    {
        pThis->ValidTime = *pTimeSpec;
        pThis->fFlags |= RTCRX509CERTPATHSINT_F_VALID_TIME;
    }
    else
        pThis->fFlags &= ~RTCRX509CERTPATHSINT_F_VALID_TIME;
    return VINF_SUCCESS;
}


RTDECL(int) RTCrX509CertPathsSetTrustAnchorChecks(RTCRX509CERTPATHS hCertPaths, bool fEnable)
{
    PRTCRX509CERTPATHSINT pThis = hCertPaths;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTCRX509CERTPATHSINT_MAGIC, VERR_INVALID_HANDLE);

    if (fEnable)
        pThis->fFlags |= RTCRX509CERTPATHSINT_F_CHECK_TRUST_ANCHOR;
    else
        pThis->fFlags &= ~RTCRX509CERTPATHSINT_F_CHECK_TRUST_ANCHOR;
    return VINF_SUCCESS;
}


RTDECL(int) RTCrX509CertPathsCreateEx(PRTCRX509CERTPATHS phCertPaths, PCRTCRX509CERTIFICATE pTarget, RTCRSTORE hTrustedStore,
                                      RTCRSTORE hUntrustedStore, PCRTCRX509CERTIFICATE paUntrustedCerts, uint32_t cUntrustedCerts,
                                      PCRTTIMESPEC pValidTime)
{
    int rc = RTCrX509CertPathsCreate(phCertPaths, pTarget);
    if (RT_SUCCESS(rc))
    {
        PRTCRX509CERTPATHSINT pThis = *phCertPaths;

        rc = RTCrX509CertPathsSetTrustedStore(pThis, hTrustedStore);
        if (RT_SUCCESS(rc))
        {
            rc = RTCrX509CertPathsSetUntrustedStore(pThis, hUntrustedStore);
            if (RT_SUCCESS(rc))
            {
                rc = RTCrX509CertPathsSetUntrustedArray(pThis, paUntrustedCerts, cUntrustedCerts);
                if (RT_SUCCESS(rc))
                {
                    rc = RTCrX509CertPathsSetValidTimeSpec(pThis, pValidTime);
                    if (RT_SUCCESS(rc))
                    {
                        return VINF_SUCCESS;
                    }
                }
                RTCrStoreRelease(pThis->hUntrustedStore);
            }
            RTCrStoreRelease(pThis->hTrustedStore);
        }
        RTMemFree(pThis);
        *phCertPaths = NIL_RTCRX509CERTPATHS;
    }
    return rc;
}

/** @} */



/** @name Path Builder and Validator Common Utility Functions.
 * @{
 */

/**
 * Checks if the certificate is self-issued.
 *
 * @returns true / false.
 * @param   pNode               The path node to check..
 */
static bool rtCrX509CertPathsIsSelfIssued(PRTCRX509CERTPATHNODE pNode)
{
    return pNode->pCert
        && RTCrX509Name_MatchByRfc5280(&pNode->pCert->TbsCertificate.Subject, &pNode->pCert->TbsCertificate.Issuer);
}

/**
 * Helper for checking whether a certificate is in the trusted store or not.
 */
static bool rtCrX509CertPathsIsCertInStore(PRTCRX509CERTPATHNODE pNode, RTCRSTORE hStore)
{
    bool fRc = false;
    PCRTCRCERTCTX pCertCtx = RTCrStoreCertByIssuerAndSerialNo(hStore, &pNode->pCert->TbsCertificate.Issuer,
                                                              &pNode->pCert->TbsCertificate.SerialNumber);
    if (pCertCtx)
    {
        if (pCertCtx->pCert)
            fRc = RTCrX509Certificate_Compare(pCertCtx->pCert, pNode->pCert) == 0;
        RTCrCertCtxRelease(pCertCtx);
    }
    return fRc;
}

/** @}  */



/** @name Path Builder Functions.
 * @{
 */

static PRTCRX509CERTPATHNODE rtCrX509CertPathsNewNode(PRTCRX509CERTPATHSINT pThis)
{
    PRTCRX509CERTPATHNODE pNode = (PRTCRX509CERTPATHNODE)RTMemAllocZ(sizeof(*pNode));
    if (RT_LIKELY(pNode))
    {
        RTListInit(&pNode->SiblingEntry);
        RTListInit(&pNode->ChildListOrLeafEntry);
        pNode->rcVerify = VERR_CR_X509_NOT_VERIFIED;

        return pNode;
    }

    pThis->rc = RTErrInfoSet(pThis->pErrInfo, VERR_NO_MEMORY, "No memory for path node");
    return NULL;
}


static void rtCrX509CertPathsDestroyNode(PRTCRX509CERTPATHNODE pNode)
{
    if (pNode->pCertCtx)
    {
        RTCrCertCtxRelease(pNode->pCertCtx);
        pNode->pCertCtx = NULL;
    }
    RT_ZERO(*pNode);
    RTMemFree(pNode);
}


static void rtCrX509CertPathsAddIssuer(PRTCRX509CERTPATHSINT pThis, PRTCRX509CERTPATHNODE pParent,
                                       PCRTCRX509CERTIFICATE pCert, PCRTCRCERTCTX pCertCtx, uint8_t uSrc)
{
    /*
     * Check if we've seen this certificate already in the current path or
     * among the already gathered issuers.
     */
    if (pCert)
    {
        /* No duplicate certificates in the path. */
        PRTCRX509CERTPATHNODE pTmpNode = pParent;
        while (pTmpNode)
        {
            Assert(pTmpNode->pCert);
            if (   pTmpNode->pCert == pCert
                || RTCrX509Certificate_Compare(pTmpNode->pCert, pCert) == 0)
            {
                /* If target and the source it trusted, upgrade the source so we can successfully verify single node 'paths'. */
                if (   RTCRX509CERTPATHNODE_SRC_IS_TRUSTED(uSrc)
                    && pTmpNode == pParent
                    && pTmpNode->uSrc == RTCRX509CERTPATHNODE_SRC_TARGET)
                {
                    AssertReturnVoid(!pTmpNode->pParent);
                    pTmpNode->uSrc = uSrc;
                }
                return;
            }
            pTmpNode = pTmpNode->pParent;
        }

        /* No duplicate tree branches. */
        RTListForEach(&pParent->ChildListOrLeafEntry, pTmpNode, RTCRX509CERTPATHNODE, SiblingEntry)
        {
            if (RTCrX509Certificate_Compare(pTmpNode->pCert, pCert) == 0)
                return;
        }
    }
    else
        Assert(pCertCtx);

    /*
     * Reference the context core before making the allocation.
     */
    if (pCertCtx)
        AssertReturnVoidStmt(RTCrCertCtxRetain(pCertCtx) != UINT32_MAX,
                             pThis->rc = RTErrInfoSetF(pThis->pErrInfo, VERR_CR_X509_CPB_BAD_CERT_CTX,
                                                       "Bad pCertCtx=%p", pCertCtx));

    /*
     * We haven't see it, append it as a child.
     */
    PRTCRX509CERTPATHNODE pNew = rtCrX509CertPathsNewNode(pThis);
    if (pNew)
    {
        pNew->pParent  = pParent;
        pNew->pCert    = pCert;
        pNew->pCertCtx = pCertCtx;
        pNew->uSrc     = uSrc;
        pNew->uDepth   = pParent->uDepth + 1;
        RTListAppend(&pParent->ChildListOrLeafEntry, &pNew->SiblingEntry);
        Log2Func(("pNew=%p uSrc=%u uDepth=%u\n", pNew, uSrc, pNew->uDepth));
    }
    else
        RTCrCertCtxRelease(pCertCtx);
}


static void rtCrX509CertPathsGetIssuersFromStore(PRTCRX509CERTPATHSINT pThis, PRTCRX509CERTPATHNODE pNode,
                                                 PCRTCRX509NAME pIssuer, RTCRSTORE hStore, uint8_t uSrc)
{
    RTCRSTORECERTSEARCH Search;
    int rc = RTCrStoreCertFindBySubjectOrAltSubjectByRfc5280(hStore, pIssuer, &Search);
    if (RT_SUCCESS(rc))
    {
        PCRTCRCERTCTX pCertCtx;
        while ((pCertCtx = RTCrStoreCertSearchNext(hStore, &Search)) != NULL)
        {
            if (   pCertCtx->pCert
                || (   RTCRX509CERTPATHNODE_SRC_IS_TRUSTED(uSrc)
                    && pCertCtx->pTaInfo) )
                rtCrX509CertPathsAddIssuer(pThis, pNode, pCertCtx->pCert, pCertCtx, uSrc);
            RTCrCertCtxRelease(pCertCtx);
        }
        RTCrStoreCertSearchDestroy(hStore, &Search);
    }
}


static void rtCrX509CertPathsGetIssuers(PRTCRX509CERTPATHSINT pThis, PRTCRX509CERTPATHNODE pNode)
{
    Assert(RTListIsEmpty(&pNode->ChildListOrLeafEntry));
    Assert(!pNode->fLeaf);
    Assert(pNode->pCert);

    /*
     * Don't recurse infintely.
     */
    if (RT_UNLIKELY(pNode->uDepth >= 50))
        return;

    PCRTCRX509NAME const pIssuer = &pNode->pCert->TbsCertificate.Issuer;
#if defined(LOG_ENABLED) && defined(IN_RING3)
    if (LogIs2Enabled())
    {
        char szIssuer[128] = {0};
        RTCrX509Name_FormatAsString(pIssuer, szIssuer, sizeof(szIssuer), NULL);
        char szSubject[128] = {0};
        RTCrX509Name_FormatAsString(&pNode->pCert->TbsCertificate.Subject, szSubject, sizeof(szSubject), NULL);
        Log2Func(("pNode=%p uSrc=%u uDepth=%u Issuer='%s' (Subject='%s')\n", pNode, pNode->uSrc, pNode->uDepth, szIssuer, szSubject));
    }
#endif

    /*
     * Trusted certificate.
     */
    if (   pThis->pTrustedCert
        && RTCrX509Certificate_MatchSubjectOrAltSubjectByRfc5280(pThis->pTrustedCert, pIssuer))
        rtCrX509CertPathsAddIssuer(pThis, pNode, pThis->pTrustedCert, NULL, RTCRX509CERTPATHNODE_SRC_TRUSTED_CERT);

    /*
     * Trusted certificate store.
     */
    if (pThis->hTrustedStore != NIL_RTCRSTORE)
        rtCrX509CertPathsGetIssuersFromStore(pThis, pNode, pIssuer, pThis->hTrustedStore,
                                             RTCRX509CERTPATHNODE_SRC_TRUSTED_STORE);

    /*
     * Untrusted store.
     */
    if (pThis->hUntrustedStore != NIL_RTCRSTORE)
        rtCrX509CertPathsGetIssuersFromStore(pThis, pNode, pIssuer, pThis->hUntrustedStore,
                                             RTCRX509CERTPATHNODE_SRC_UNTRUSTED_STORE);

    /*
     * Untrusted array.
     */
    if (pThis->paUntrustedCerts)
        for (uint32_t i = 0; i < pThis->cUntrustedCerts; i++)
            if (RTCrX509Certificate_MatchSubjectOrAltSubjectByRfc5280(&pThis->paUntrustedCerts[i], pIssuer))
                rtCrX509CertPathsAddIssuer(pThis, pNode, &pThis->paUntrustedCerts[i], NULL,
                                           RTCRX509CERTPATHNODE_SRC_UNTRUSTED_ARRAY);

    /** @todo Rainy day: Should abstract the untrusted array and set so we don't get
     *        unnecessary PKCS7/CMS header dependencies. */

    /*
     * Untrusted set.
     */
    if (pThis->pUntrustedCertsSet)
    {
        uint32_t const        cCerts   = pThis->pUntrustedCertsSet->cItems;
        PRTCRPKCS7CERT const *papCerts = pThis->pUntrustedCertsSet->papItems;
        for (uint32_t i = 0; i < cCerts; i++)
        {
            PCRTCRPKCS7CERT pCert = papCerts[i];
            if (   pCert->enmChoice == RTCRPKCS7CERTCHOICE_X509
                && RTCrX509Certificate_MatchSubjectOrAltSubjectByRfc5280(pCert->u.pX509Cert, pIssuer))
                rtCrX509CertPathsAddIssuer(pThis, pNode, pCert->u.pX509Cert, NULL, RTCRX509CERTPATHNODE_SRC_UNTRUSTED_SET);
        }
    }
}


static PRTCRX509CERTPATHNODE rtCrX509CertPathsGetNextRightUp(PRTCRX509CERTPATHSINT pThis, PRTCRX509CERTPATHNODE pNode)
{
    for (;;)
    {
        /* The root node has no siblings. */
        PRTCRX509CERTPATHNODE pParent = pNode->pParent;
        if (!pNode->pParent)
            return NULL;

        /* Try go to the right. */
        PRTCRX509CERTPATHNODE pNext = RTListGetNext(&pParent->ChildListOrLeafEntry, pNode, RTCRX509CERTPATHNODE, SiblingEntry);
        if (pNext)
            return pNext;

        /* Up. */
        pNode = pParent;
    }

    RT_NOREF_PV(pThis);
}


static PRTCRX509CERTPATHNODE rtCrX509CertPathsEliminatePath(PRTCRX509CERTPATHSINT pThis, PRTCRX509CERTPATHNODE pNode)
{
    for (;;)
    {
        Assert(RTListIsEmpty(&pNode->ChildListOrLeafEntry));

        /* Don't remove the root node. */
        PRTCRX509CERTPATHNODE pParent = pNode->pParent;
        if (!pParent)
            return NULL;

        /* Before removing and deleting the node check if there is sibling
           right to it that we should continue processing from. */
        PRTCRX509CERTPATHNODE pNext = RTListGetNext(&pParent->ChildListOrLeafEntry, pNode, RTCRX509CERTPATHNODE, SiblingEntry);
        RTListNodeRemove(&pNode->SiblingEntry);
        rtCrX509CertPathsDestroyNode(pNode);

        if (pNext)
            return pNext;

        /* If the parent node cannot be removed, do a normal get-next-rigth-up
           to find the continuation point for the tree loop. */
        if (!RTListIsEmpty(&pParent->ChildListOrLeafEntry))
            return rtCrX509CertPathsGetNextRightUp(pThis, pParent);

        pNode = pParent;
    }
}


/**
 * Destroys the whole path tree.
 *
 * @param   pThis       The path builder and verifier instance.
 */
static void rtCrX509CertPathsDestroyTree(PRTCRX509CERTPATHSINT pThis)
{
    PRTCRX509CERTPATHNODE pNode, pNextLeaf;
    RTListForEachSafe(&pThis->LeafList, pNode, pNextLeaf, RTCRX509CERTPATHNODE, ChildListOrLeafEntry)
    {
        RTListNodeRemove(&pNode->ChildListOrLeafEntry);
        RTListInit(&pNode->ChildListOrLeafEntry);

        for (;;)
        {
            PRTCRX509CERTPATHNODE pParent = pNode->pParent;

            RTListNodeRemove(&pNode->SiblingEntry);
            rtCrX509CertPathsDestroyNode(pNode);

            if (!pParent)
            {
                pThis->pRoot = NULL;
                break;
            }

            if (!RTListIsEmpty(&pParent->ChildListOrLeafEntry))
                break;

            pNode = pParent;
        }
    }
    Assert(!pThis->pRoot);
}


/**
 * Adds a leaf node.
 *
 * This should normally be a trusted certificate, but the caller can also
 * request the incomplete paths, in which case this will be an untrusted
 * certificate.
 *
 * @returns Pointer to the next node in the tree to process.
 * @param   pThis               The path builder instance.
 * @param   pNode               The leaf node.
 */
static PRTCRX509CERTPATHNODE rtCrX509CertPathsAddLeaf(PRTCRX509CERTPATHSINT pThis, PRTCRX509CERTPATHNODE pNode)
{
    pNode->fLeaf = true;

    /*
     * Priority insert by source and depth.
     */
    PRTCRX509CERTPATHNODE pCurLeaf;
    RTListForEach(&pThis->LeafList, pCurLeaf, RTCRX509CERTPATHNODE, ChildListOrLeafEntry)
    {
        if (   pNode->uSrc > pCurLeaf->uSrc
            || (   pNode->uSrc == pCurLeaf->uSrc
                && pNode->uDepth < pCurLeaf->uDepth) )
        {
            RTListNodeInsertBefore(&pCurLeaf->ChildListOrLeafEntry, &pNode->ChildListOrLeafEntry);
            pThis->cPaths++;
            return rtCrX509CertPathsGetNextRightUp(pThis, pNode);
        }
    }

    RTListAppend(&pThis->LeafList, &pNode->ChildListOrLeafEntry);
    pThis->cPaths++;
    return rtCrX509CertPathsGetNextRightUp(pThis, pNode);
}



RTDECL(int) RTCrX509CertPathsBuild(RTCRX509CERTPATHS hCertPaths, PRTERRINFO pErrInfo)
{
    /*
     * Validate the input.
     */
    PRTCRX509CERTPATHSINT pThis = hCertPaths;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTCRX509CERTPATHSINT_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(!(pThis->fFlags & ~RTCRX509CERTPATHSINT_F_VALID_MASK), VERR_INVALID_PARAMETER);
    AssertReturn(   (pThis->paUntrustedCerts == NULL && pThis->cUntrustedCerts == 0)
                 || (pThis->paUntrustedCerts != NULL && pThis->cUntrustedCerts > 0),
                 VERR_INVALID_PARAMETER);
    AssertReturn(RTListIsEmpty(&pThis->LeafList), VERR_INVALID_PARAMETER);
    AssertReturn(pThis->pRoot == NULL, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->rc == VINF_SUCCESS, pThis->rc);
    AssertPtrReturn(pThis->pTarget, VERR_INVALID_PARAMETER);
    Assert(RT_SUCCESS(RTCrX509Certificate_CheckSanity(pThis->pTarget, 0, NULL, NULL)));

    /*
     * Set up the target.
     */
    PRTCRX509CERTPATHNODE pCur;
    pThis->pRoot = pCur = rtCrX509CertPathsNewNode(pThis);
    if (pThis->pRoot)
    {
        pCur->pCert  = pThis->pTarget;
        pCur->uDepth = 0;
        pCur->uSrc   = RTCRX509CERTPATHNODE_SRC_TARGET;

        /* Check if the target is trusted and do the upgrade (this is outside the RFC,
           but this simplifies the path validator usage a lot (less work for the caller)). */
        if (   pThis->pTrustedCert
            && RTCrX509Certificate_Compare(pThis->pTrustedCert, pCur->pCert) == 0)
            pCur->uSrc = RTCRX509CERTPATHNODE_SRC_TRUSTED_CERT;
        else if (   pThis->hTrustedStore != NIL_RTCRSTORE
                 && rtCrX509CertPathsIsCertInStore(pCur, pThis->hTrustedStore))
            pCur->uSrc = RTCRX509CERTPATHNODE_SRC_TRUSTED_STORE;

        pThis->pErrInfo = pErrInfo;

        /*
         * The tree construction loop.
         * Walks down, up, and right as the tree is constructed.
         */
        do
        {
            /*
             * Check for the two leaf cases first.
             */
            if (RTCRX509CERTPATHNODE_SRC_IS_TRUSTED(pCur->uSrc))
                pCur = rtCrX509CertPathsAddLeaf(pThis, pCur);
#if 0 /* This isn't right.*/
            else if (rtCrX509CertPathsIsSelfIssued(pCur))
            {
                if (pThis->fFlags & RTCRX509CERTPATHSINT_F_ELIMINATE_UNTRUSTED_PATHS)
                    pCur = rtCrX509CertPathsEliminatePath(pThis, pCur);
                else
                    pCur = rtCrX509CertPathsAddLeaf(pThis, pCur);
            }
#endif
            /*
             * Not a leaf, find all potential issuers and decend into these.
             */
            else
            {
                rtCrX509CertPathsGetIssuers(pThis, pCur);
                if (RT_FAILURE(pThis->rc))
                    break;

                if (!RTListIsEmpty(&pCur->ChildListOrLeafEntry))
                    pCur = RTListGetFirst(&pCur->ChildListOrLeafEntry, RTCRX509CERTPATHNODE, SiblingEntry);
                else if (pThis->fFlags & RTCRX509CERTPATHSINT_F_ELIMINATE_UNTRUSTED_PATHS)
                    pCur = rtCrX509CertPathsEliminatePath(pThis, pCur);
                else
                    pCur = rtCrX509CertPathsAddLeaf(pThis, pCur);
            }
            if (pCur)
                Log2(("RTCrX509CertPathsBuild: pCur=%p fLeaf=%d pParent=%p pNext=%p pPrev=%p\n",
                      pCur, pCur->fLeaf, pCur->pParent,
                      pCur->pParent ? RTListGetNext(&pCur->pParent->ChildListOrLeafEntry, pCur, RTCRX509CERTPATHNODE, SiblingEntry) : NULL,
                      pCur->pParent ? RTListGetPrev(&pCur->pParent->ChildListOrLeafEntry, pCur, RTCRX509CERTPATHNODE, SiblingEntry) : NULL));
        } while (pCur);

        pThis->pErrInfo = NULL;
        if (RT_SUCCESS(pThis->rc))
            return VINF_SUCCESS;
    }
    else
        Assert(RT_FAILURE_NP(pThis->rc));
    return pThis->rc;
}


/**
 * Looks up path by leaf/path index.
 *
 * @returns Pointer to the leaf node of the path.
 * @param   pThis           The path builder & validator instance.
 * @param   iPath           The oridnal of the path to get.
 */
static PRTCRX509CERTPATHNODE rtCrX509CertPathsGetLeafByIndex(PRTCRX509CERTPATHSINT pThis, uint32_t iPath)
{
    Assert(iPath < pThis->cPaths);

    uint32_t                iCurPath = 0;
    PRTCRX509CERTPATHNODE   pCurLeaf;
    RTListForEach(&pThis->LeafList, pCurLeaf, RTCRX509CERTPATHNODE, ChildListOrLeafEntry)
    {
        if (iCurPath == iPath)
            return pCurLeaf;
        iCurPath++;
    }

    AssertFailedReturn(NULL);
}


static void rtDumpPrintf(PFNRTDUMPPRINTFV pfnPrintfV, void *pvUser, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    pfnPrintfV(pvUser, pszFormat, va);
    va_end(va);
}


static void rtDumpIndent(PFNRTDUMPPRINTFV pfnPrintfV, void *pvUser, uint32_t cchSpaces, const char *pszFormat, ...)
{
    static const char s_szSpaces[] = "                          ";
    while (cchSpaces > 0)
    {
        uint32_t cchBurst = RT_MIN(sizeof(s_szSpaces) - 1, cchSpaces);
        rtDumpPrintf(pfnPrintfV, pvUser, &s_szSpaces[sizeof(s_szSpaces) - cchBurst - 1]);
        cchSpaces -= cchBurst;
    }

    va_list va;
    va_start(va, pszFormat);
    pfnPrintfV(pvUser, pszFormat, va);
    va_end(va);
}

/** @name X.500 attribute types
 * See RFC-4519 among others.
 * @{ */
#define RTCRX500_ID_AT_OBJECT_CLASS_OID                     "2.5.4.0"
#define RTCRX500_ID_AT_ALIASED_ENTRY_NAME_OID               "2.5.4.1"
#define RTCRX500_ID_AT_KNOWLDGEINFORMATION_OID              "2.5.4.2"
#define RTCRX500_ID_AT_COMMON_NAME_OID                      "2.5.4.3"
#define RTCRX500_ID_AT_SURNAME_OID                          "2.5.4.4"
#define RTCRX500_ID_AT_SERIAL_NUMBER_OID                    "2.5.4.5"
#define RTCRX500_ID_AT_COUNTRY_NAME_OID                     "2.5.4.6"
#define RTCRX500_ID_AT_LOCALITY_NAME_OID                    "2.5.4.7"
#define RTCRX500_ID_AT_STATE_OR_PROVINCE_NAME_OID           "2.5.4.8"
#define RTCRX500_ID_AT_STREET_ADDRESS_OID                   "2.5.4.9"
#define RTCRX500_ID_AT_ORGANIZATION_NAME_OID                "2.5.4.10"
#define RTCRX500_ID_AT_ORGANIZATION_UNIT_NAME_OID           "2.5.4.11"
#define RTCRX500_ID_AT_TITLE_OID                            "2.5.4.12"
#define RTCRX500_ID_AT_DESCRIPTION_OID                      "2.5.4.13"
#define RTCRX500_ID_AT_SEARCH_GUIDE_OID                     "2.5.4.14"
#define RTCRX500_ID_AT_BUSINESS_CATEGORY_OID                "2.5.4.15"
#define RTCRX500_ID_AT_POSTAL_ADDRESS_OID                   "2.5.4.16"
#define RTCRX500_ID_AT_POSTAL_CODE_OID                      "2.5.4.17"
#define RTCRX500_ID_AT_POST_OFFICE_BOX_OID                  "2.5.4.18"
#define RTCRX500_ID_AT_PHYSICAL_DELIVERY_OFFICE_NAME_OID    "2.5.4.19"
#define RTCRX500_ID_AT_TELEPHONE_NUMBER_OID                 "2.5.4.20"
#define RTCRX500_ID_AT_TELEX_NUMBER_OID                     "2.5.4.21"
#define RTCRX500_ID_AT_TELETEX_TERMINAL_IDENTIFIER_OID      "2.5.4.22"
#define RTCRX500_ID_AT_FACIMILE_TELEPHONE_NUMBER_OID        "2.5.4.23"
#define RTCRX500_ID_AT_X121_ADDRESS_OID                     "2.5.4.24"
#define RTCRX500_ID_AT_INTERNATIONAL_ISDN_NUMBER_OID        "2.5.4.25"
#define RTCRX500_ID_AT_REGISTERED_ADDRESS_OID               "2.5.4.26"
#define RTCRX500_ID_AT_DESTINATION_INDICATOR_OID            "2.5.4.27"
#define RTCRX500_ID_AT_PREFERRED_DELIVERY_METHOD_OID        "2.5.4.28"
#define RTCRX500_ID_AT_PRESENTATION_ADDRESS_OID             "2.5.4.29"
#define RTCRX500_ID_AT_SUPPORTED_APPLICATION_CONTEXT_OID    "2.5.4.30"
#define RTCRX500_ID_AT_MEMBER_OID                           "2.5.4.31"
#define RTCRX500_ID_AT_OWNER_OID                            "2.5.4.32"
#define RTCRX500_ID_AT_ROLE_OCCUPANT_OID                    "2.5.4.33"
#define RTCRX500_ID_AT_SEE_ALSO_OID                         "2.5.4.34"
#define RTCRX500_ID_AT_USER_PASSWORD_OID                    "2.5.4.35"
#define RTCRX500_ID_AT_USER_CERTIFICATE_OID                 "2.5.4.36"
#define RTCRX500_ID_AT_CA_CERTIFICATE_OID                   "2.5.4.37"
#define RTCRX500_ID_AT_AUTHORITY_REVOCATION_LIST_OID        "2.5.4.38"
#define RTCRX500_ID_AT_CERTIFICATE_REVOCATION_LIST_OID      "2.5.4.39"
#define RTCRX500_ID_AT_CROSS_CERTIFICATE_PAIR_OID           "2.5.4.40"
#define RTCRX500_ID_AT_NAME_OID                             "2.5.4.41"
#define RTCRX500_ID_AT_GIVEN_NAME_OID                       "2.5.4.42"
#define RTCRX500_ID_AT_INITIALS_OID                         "2.5.4.43"
#define RTCRX500_ID_AT_GENERATION_QUALIFIER_OID             "2.5.4.44"
#define RTCRX500_ID_AT_UNIQUE_IDENTIFIER_OID                "2.5.4.45"
#define RTCRX500_ID_AT_DN_QUALIFIER_OID                     "2.5.4.46"
#define RTCRX500_ID_AT_ENHANCHED_SEARCH_GUIDE_OID           "2.5.4.47"
#define RTCRX500_ID_AT_PROTOCOL_INFORMATION_OID             "2.5.4.48"
#define RTCRX500_ID_AT_DISTINGUISHED_NAME_OID               "2.5.4.49"
#define RTCRX500_ID_AT_UNIQUE_MEMBER_OID                    "2.5.4.50"
#define RTCRX500_ID_AT_HOUSE_IDENTIFIER_OID                 "2.5.4.51"
#define RTCRX500_ID_AT_SUPPORTED_ALGORITHMS_OID             "2.5.4.52"
#define RTCRX500_ID_AT_DELTA_REVOCATION_LIST_OID            "2.5.4.53"
#define RTCRX500_ID_AT_ATTRIBUTE_CERTIFICATE_OID            "2.5.4.58"
#define RTCRX500_ID_AT_PSEUDONYM_OID                        "2.5.4.65"
/** @} */


static void rtCrX509NameDump(PCRTCRX509NAME pName, PFNRTDUMPPRINTFV pfnPrintfV, void *pvUser)
{
    for (uint32_t i = 0; i < pName->cItems; i++)
    {
        PCRTCRX509RELATIVEDISTINGUISHEDNAME const pRdn = pName->papItems[i];
        for (uint32_t j = 0; j < pRdn->cItems; j++)
        {
            PRTCRX509ATTRIBUTETYPEANDVALUE pAttrib = pRdn->papItems[j];

            const char *pszType = RTCrX509Name_GetShortRdn(&pAttrib->Type);
            if (!pszType)
                pszType = pAttrib->Type.szObjId;
            rtDumpPrintf(pfnPrintfV, pvUser, "/%s=", pszType);
            if (pAttrib->Value.enmType == RTASN1TYPE_STRING)
            {
                if (pAttrib->Value.u.String.pszUtf8)
                    rtDumpPrintf(pfnPrintfV, pvUser, "%s", pAttrib->Value.u.String.pszUtf8);
                else
                {
                    const char *pch = pAttrib->Value.u.String.Asn1Core.uData.pch;
                    uint32_t    cch = pAttrib->Value.u.String.Asn1Core.cb;
                    int rc = RTStrValidateEncodingEx(pch, cch, 0);
                    if (RT_SUCCESS(rc) && cch)
                        rtDumpPrintf(pfnPrintfV, pvUser, "%.*s", (size_t)cch, pch);
                    else
                        while (cch > 0)
                        {
                            if (RT_C_IS_PRINT(*pch))
                                rtDumpPrintf(pfnPrintfV, pvUser, "%c", *pch);
                            else
                                rtDumpPrintf(pfnPrintfV, pvUser, "\\x%02x", *pch);
                            cch--;
                            pch++;
                        }
                }
            }
            else
                rtDumpPrintf(pfnPrintfV, pvUser, "<not-string: uTag=%#x>", pAttrib->Value.u.Core.uTag);
        }
    }
}


static const char *rtCrX509CertPathsNodeGetSourceName(PRTCRX509CERTPATHNODE pNode)
{
    switch (pNode->uSrc)
    {
        case RTCRX509CERTPATHNODE_SRC_TARGET:           return "target";
        case RTCRX509CERTPATHNODE_SRC_UNTRUSTED_SET:    return "untrusted_set";
        case RTCRX509CERTPATHNODE_SRC_UNTRUSTED_ARRAY:  return "untrusted_array";
        case RTCRX509CERTPATHNODE_SRC_UNTRUSTED_STORE:  return "untrusted_store";
        case RTCRX509CERTPATHNODE_SRC_TRUSTED_STORE:    return "trusted_store";
        case RTCRX509CERTPATHNODE_SRC_TRUSTED_CERT:     return "trusted_cert";
        default:                                        return "invalid";
    }
}


static void rtCrX509CertPathsDumpOneWorker(PRTCRX509CERTPATHSINT pThis, uint32_t iPath, PRTCRX509CERTPATHNODE pCurLeaf,
                                           uint32_t uVerbosity, PFNRTDUMPPRINTFV pfnPrintfV, void *pvUser)
{
    RT_NOREF_PV(pThis);
    rtDumpPrintf(pfnPrintfV, pvUser, "Path #%u: %s, %u deep, rcVerify=%Rrc\n",
                 iPath, RTCRX509CERTPATHNODE_SRC_IS_TRUSTED(pCurLeaf->uSrc) ? "trusted" : "untrusted", pCurLeaf->uDepth,
                 pCurLeaf->rcVerify);

    for (uint32_t iIndent = 2; pCurLeaf; iIndent += 2, pCurLeaf = pCurLeaf->pParent)
    {
        if (pCurLeaf->pCert)
        {
            rtDumpIndent(pfnPrintfV, pvUser, iIndent, "Issuer : ");
            rtCrX509NameDump(&pCurLeaf->pCert->TbsCertificate.Issuer, pfnPrintfV, pvUser);
            rtDumpPrintf(pfnPrintfV, pvUser, "\n");

            rtDumpIndent(pfnPrintfV, pvUser, iIndent, "Subject: ");
            rtCrX509NameDump(&pCurLeaf->pCert->TbsCertificate.Subject, pfnPrintfV, pvUser);
            rtDumpPrintf(pfnPrintfV, pvUser, "\n");

            if (uVerbosity >= 4)
                RTAsn1Dump(&pCurLeaf->pCert->SeqCore.Asn1Core, 0, iIndent, pfnPrintfV, pvUser);
            else if (uVerbosity >= 3)
                RTAsn1Dump(&pCurLeaf->pCert->TbsCertificate.T3.Extensions.SeqCore.Asn1Core, 0, iIndent, pfnPrintfV, pvUser);

            rtDumpIndent(pfnPrintfV, pvUser, iIndent, "Valid  : %s thru %s\n",
                         RTTimeToString(&pCurLeaf->pCert->TbsCertificate.Validity.NotBefore.Time,
                                        pThis->szTmp, sizeof(pThis->szTmp) / 2),
                         RTTimeToString(&pCurLeaf->pCert->TbsCertificate.Validity.NotAfter.Time,
                                        &pThis->szTmp[sizeof(pThis->szTmp) / 2], sizeof(pThis->szTmp) / 2) );
        }
        else
        {
            Assert(pCurLeaf->pCertCtx); Assert(pCurLeaf->pCertCtx->pTaInfo);
            rtDumpIndent(pfnPrintfV, pvUser, iIndent, "Subject: ");
            rtCrX509NameDump(&pCurLeaf->pCertCtx->pTaInfo->CertPath.TaName, pfnPrintfV, pvUser);

            if (uVerbosity >= 4)
                RTAsn1Dump(&pCurLeaf->pCertCtx->pTaInfo->SeqCore.Asn1Core, 0, iIndent, pfnPrintfV, pvUser);
        }

        const char *pszSrc = rtCrX509CertPathsNodeGetSourceName(pCurLeaf);
        rtDumpIndent(pfnPrintfV, pvUser, iIndent, "Source : %s\n", pszSrc);
    }
}


RTDECL(int) RTCrX509CertPathsDumpOne(RTCRX509CERTPATHS hCertPaths, uint32_t iPath, uint32_t uVerbosity,
                                     PFNRTDUMPPRINTFV pfnPrintfV, void *pvUser)
{
    /*
     * Validate the input.
     */
    PRTCRX509CERTPATHSINT pThis = hCertPaths;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTCRX509CERTPATHSINT_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pfnPrintfV, VERR_INVALID_POINTER);
    int rc;
    if (iPath < pThis->cPaths)
    {
        PRTCRX509CERTPATHNODE pLeaf = rtCrX509CertPathsGetLeafByIndex(pThis, iPath);
        if (pLeaf)
        {
            rtCrX509CertPathsDumpOneWorker(pThis, iPath, pLeaf, uVerbosity, pfnPrintfV, pvUser);
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_CR_X509_CERTPATHS_INTERNAL_ERROR;
    }
    else
        rc = VERR_NOT_FOUND;
    return rc;
}


RTDECL(int) RTCrX509CertPathsDumpAll(RTCRX509CERTPATHS hCertPaths, uint32_t uVerbosity, PFNRTDUMPPRINTFV pfnPrintfV, void *pvUser)
{
    /*
     * Validate the input.
     */
    PRTCRX509CERTPATHSINT pThis = hCertPaths;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTCRX509CERTPATHSINT_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pfnPrintfV, VERR_INVALID_POINTER);

    /*
     * Dump all the paths.
     */
    rtDumpPrintf(pfnPrintfV, pvUser, "%u paths, rc=%Rrc\n", pThis->cPaths, pThis->rc);
    uint32_t iPath = 0;
    PRTCRX509CERTPATHNODE pCurLeaf, pNextLeaf;
    RTListForEachSafe(&pThis->LeafList, pCurLeaf, pNextLeaf, RTCRX509CERTPATHNODE, ChildListOrLeafEntry)
    {
        rtCrX509CertPathsDumpOneWorker(pThis, iPath, pCurLeaf, uVerbosity, pfnPrintfV, pvUser);
        iPath++;
    }

    return VINF_SUCCESS;
}


/** @} */


/** @name Path Validator Functions.
 * @{
 */


static void *rtCrX509CpvAllocZ(PRTCRX509CERTPATHSINT pThis, size_t cb, const char *pszWhat)
{
    void *pv = RTMemAllocZ(cb);
    if (!pv)
        pThis->rc = RTErrInfoSetF(pThis->pErrInfo, VERR_NO_MEMORY, "Failed to allocate %zu bytes for %s", cb, pszWhat);
    return pv;
}


DECL_NO_INLINE(static, bool) rtCrX509CpvFailed(PRTCRX509CERTPATHSINT pThis, int rc, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    pThis->rc = RTErrInfoSetV(pThis->pErrInfo, rc, pszFormat, va);
    va_end(va);
    return false;
}


/**
 * Adds a sequence of excluded sub-trees.
 *
 * Don't waste time optimizing the output if this is supposed to be a union.
 * Unless the path is very long, it's a lot more work to optimize and the result
 * will be the same anyway.
 *
 * @returns success indicator.
 * @param   pThis               The validator instance.
 * @param   pSubtrees           The sequence of sub-trees to add.
 */
static bool rtCrX509CpvAddExcludedSubtrees(PRTCRX509CERTPATHSINT pThis, PCRTCRX509GENERALSUBTREES pSubtrees)
{
    if (((pThis->v.cExcludedSubtrees + 1) & 0xf) == 0)
    {
        void *pvNew = RTMemRealloc(pThis->v.papExcludedSubtrees,
                                   (pThis->v.cExcludedSubtrees + 16) * sizeof(pThis->v.papExcludedSubtrees[0]));
        if (RT_UNLIKELY(!pvNew))
            return rtCrX509CpvFailed(pThis, VERR_NO_MEMORY, "Error growing subtrees pointer array to %u elements",
                                     pThis->v.cExcludedSubtrees + 16);
        pThis->v.papExcludedSubtrees = (PCRTCRX509GENERALSUBTREES *)pvNew;
    }
    pThis->v.papExcludedSubtrees[pThis->v.cExcludedSubtrees] = pSubtrees;
    pThis->v.cExcludedSubtrees++;
    return true;
}


/**
 * Checks if a sub-tree is according to RFC-5280.
 *
 * @returns Success indiciator.
 * @param   pThis               The validator instance.
 * @param   pSubtree            The subtree to check.
 */
static bool rtCrX509CpvCheckSubtreeValidity(PRTCRX509CERTPATHSINT pThis, PCRTCRX509GENERALSUBTREE pSubtree)
{
    if (   pSubtree->Base.enmChoice <= RTCRX509GENERALNAMECHOICE_INVALID
        || pSubtree->Base.enmChoice >= RTCRX509GENERALNAMECHOICE_END)
        return rtCrX509CpvFailed(pThis, VERR_CR_X509_CPV_UNEXP_GENERAL_SUBTREE_CHOICE,
                                 "Unexpected GeneralSubtree choice %#x", pSubtree->Base.enmChoice);

    if (RTAsn1Integer_UnsignedCompareWithU32(&pSubtree->Minimum, 0) != 0)
        return rtCrX509CpvFailed(pThis, VERR_CR_X509_CPV_UNEXP_GENERAL_SUBTREE_MIN,
                                 "Unexpected GeneralSubtree Minimum value: %#llx",
                                 pSubtree->Minimum.uValue);

    if (RTAsn1Integer_IsPresent(&pSubtree->Maximum))
        return rtCrX509CpvFailed(pThis, VERR_CR_X509_CPV_UNEXP_GENERAL_SUBTREE_MAX,
                                 "Unexpected GeneralSubtree Maximum value: %#llx",
                                 pSubtree->Maximum.uValue);

    return true;
}


/**
 * Grows the array of permitted sub-trees.
 *
 * @returns success indiciator.
 * @param   pThis               The validator instance.
 * @param   cAdding             The number of subtrees we should grow by
 *                              (relative to the current number of valid
 *                              entries).
 */
static bool rtCrX509CpvGrowPermittedSubtrees(PRTCRX509CERTPATHSINT pThis, uint32_t cAdding)
{
    uint32_t cNew = RT_ALIGN_32(pThis->v.cPermittedSubtrees + cAdding, 16);
    if (cNew > pThis->v.cPermittedSubtreesAlloc)
    {
        if (cNew >= _4K)
            return rtCrX509CpvFailed(pThis, VERR_NO_MEMORY, "Too many permitted subtrees: %u (cur %u)",
                                     cNew, pThis->v.cPermittedSubtrees);
        void *pvNew = RTMemRealloc(pThis->v.papPermittedSubtrees, cNew * sizeof(pThis->v.papPermittedSubtrees[0]));
        if (RT_UNLIKELY(!pvNew))
            return rtCrX509CpvFailed(pThis, VERR_NO_MEMORY, "Error growing subtrees pointer array from %u to %u elements",
                                     pThis->v.cPermittedSubtreesAlloc, cNew);
        pThis->v.papPermittedSubtrees = (PCRTCRX509GENERALSUBTREE *)pvNew;
    }
    return true;
}


/**
 * Adds a sequence of permitted sub-trees.
 *
 * We store reference to each individual sub-tree because we must support
 * intersection calculation.
 *
 * @returns success indiciator.
 * @param   pThis               The validator instance.
 * @param   cSubtrees           The number of sub-trees to add.
 * @param   papSubtrees         Array of sub-trees to add.
 */
static bool rtCrX509CpvAddPermittedSubtrees(PRTCRX509CERTPATHSINT pThis, uint32_t cSubtrees,
                                            PRTCRX509GENERALSUBTREE const *papSubtrees)
{
    /*
     * If the array is empty, assume no permitted names.
     */
    if (!cSubtrees)
    {
        pThis->v.fNoPermittedSubtrees = true;
        return true;
    }

    /*
     * Grow the array if necessary.
     */
    if (!rtCrX509CpvGrowPermittedSubtrees(pThis, cSubtrees))
        return false;

    /*
     * Append each subtree to the array.
     */
    uint32_t iDst = pThis->v.cPermittedSubtrees;
    for (uint32_t iSrc = 0; iSrc < cSubtrees; iSrc++)
    {
        if (!rtCrX509CpvCheckSubtreeValidity(pThis, papSubtrees[iSrc]))
            return false;
        pThis->v.papPermittedSubtrees[iDst] = papSubtrees[iSrc];
        iDst++;
    }
    pThis->v.cPermittedSubtrees = iDst;

    return true;
}


/**
 * Adds a one permitted sub-tree.
 *
 * We store reference to each individual sub-tree because we must support
 * intersection calculation.
 *
 * @returns success indiciator.
 * @param   pThis               The validator instance.
 * @param   pSubtree            Array of sub-trees to add.
 */
static bool rtCrX509CpvAddPermittedSubtree(PRTCRX509CERTPATHSINT pThis, PCRTCRX509GENERALSUBTREE pSubtree)
{
    return rtCrX509CpvAddPermittedSubtrees(pThis, 1, (PRTCRX509GENERALSUBTREE const *)&pSubtree);
}


/**
 * Calculates the intersection between @a pSubtrees and the current permitted
 * sub-trees.
 *
 * @returns Success indicator.
 * @param   pThis               The validator instance.
 * @param   pSubtrees           The sub-tree sequence to intersect with.
 */
static bool rtCrX509CpvIntersectionPermittedSubtrees(PRTCRX509CERTPATHSINT pThis, PCRTCRX509GENERALSUBTREES pSubtrees)
{
    /*
     * Deal with special cases first.
     */
    if (pThis->v.fNoPermittedSubtrees)
    {
        Assert(pThis->v.cPermittedSubtrees == 0);
        return true;
    }

    uint32_t                       cRight   = pSubtrees->cItems;
    PRTCRX509GENERALSUBTREE const *papRight = pSubtrees->papItems;
    if (cRight == 0)
    {
        pThis->v.cPermittedSubtrees = 0;
        pThis->v.fNoPermittedSubtrees = true;
        return true;
    }

    uint32_t                    cLeft   = pThis->v.cPermittedSubtrees;
    PCRTCRX509GENERALSUBTREE   *papLeft = pThis->v.papPermittedSubtrees;
    if (!cLeft) /* first name constraint, no initial constraint */
        return rtCrX509CpvAddPermittedSubtrees(pThis, cRight, papRight);

    /*
     * Create a new array with the intersection, freeing the old (left) array
     * once we're done.
     */
    bool afRightTags[RTCRX509GENERALNAMECHOICE_END] = { 0, 0, 0, 0,  0, 0, 0, 0,  0 };

    pThis->v.cPermittedSubtrees      = 0;
    pThis->v.cPermittedSubtreesAlloc = 0;
    pThis->v.papPermittedSubtrees    = NULL;

    for (uint32_t iRight = 0; iRight < cRight; iRight++)
    {
        if (!rtCrX509CpvCheckSubtreeValidity(pThis, papRight[iRight]))
            return false;

        RTCRX509GENERALNAMECHOICE const enmRightChoice = papRight[iRight]->Base.enmChoice;
        afRightTags[enmRightChoice] = true;

        bool fHaveRight = false;
        for (uint32_t iLeft = 0; iLeft < cLeft; iLeft++)
            if (papLeft[iLeft]->Base.enmChoice == enmRightChoice)
            {
                if (RTCrX509GeneralSubtree_Compare(papLeft[iLeft], papRight[iRight]) == 0)
                {
                    if (!fHaveRight)
                    {
                        fHaveRight = true;
                        rtCrX509CpvAddPermittedSubtree(pThis, papLeft[iLeft]);
                    }
                }
                else if (RTCrX509GeneralSubtree_ConstraintMatch(papLeft[iLeft], papRight[iRight]))
                {
                    if (!fHaveRight)
                    {
                        fHaveRight = true;
                        rtCrX509CpvAddPermittedSubtree(pThis, papRight[iRight]);
                    }
                }
                else if (RTCrX509GeneralSubtree_ConstraintMatch(papRight[iRight], papLeft[iLeft]))
                    rtCrX509CpvAddPermittedSubtree(pThis, papLeft[iLeft]);
            }
    }

    /*
     * Add missing types not specified in the right set.
     */
    for (uint32_t iLeft = 0; iLeft < cLeft; iLeft++)
        if (!afRightTags[papLeft[iLeft]->Base.enmChoice])
            rtCrX509CpvAddPermittedSubtree(pThis, papLeft[iLeft]);

    /*
     * If we ended up with an empty set, no names are permitted any more.
     */
    if (pThis->v.cPermittedSubtrees == 0)
        pThis->v.fNoPermittedSubtrees = true;

    RTMemFree(papLeft);
    return RT_SUCCESS(pThis->rc);
}


/**
 * Check if the given X.509 name is permitted by current name constraints.
 *
 * @returns true is permitteded, false if not (caller set error info).
 * @param   pThis       The validator instance.
 * @param   pName       The name to match.
 */
static bool rtCrX509CpvIsNamePermitted(PRTCRX509CERTPATHSINT pThis, PCRTCRX509NAME pName)
{
    uint32_t i = pThis->v.cPermittedSubtrees;
    if (i == 0)
        return !pThis->v.fNoPermittedSubtrees;

    while (i-- > 0)
    {
        PCRTCRX509GENERALSUBTREE pConstraint = pThis->v.papPermittedSubtrees[i];
        if (   RTCRX509GENERALNAME_IS_DIRECTORY_NAME(&pConstraint->Base)
            && RTCrX509Name_ConstraintMatch(&pConstraint->Base.u.pT4->DirectoryName, pName))
            return true;
    }
    return false;
}


/**
 * Check if the given X.509 general name is permitted by current name
 * constraints.
 *
 * @returns true is permitteded, false if not (caller sets error info).
 * @param   pThis           The validator instance.
 * @param   pGeneralName    The name to match.
 */
static bool rtCrX509CpvIsGeneralNamePermitted(PRTCRX509CERTPATHSINT pThis, PCRTCRX509GENERALNAME pGeneralName)
{
    uint32_t i = pThis->v.cPermittedSubtrees;
    if (i == 0)
        return !pThis->v.fNoPermittedSubtrees;

    while (i-- > 0)
        if (RTCrX509GeneralName_ConstraintMatch(&pThis->v.papPermittedSubtrees[i]->Base, pGeneralName))
            return true;
    return false;
}


/**
 * Check if the given X.509 name is excluded by current name constraints.
 *
 * @returns true if excluded (caller sets error info), false if not explicitly
 *          excluded.
 * @param   pThis       The validator instance.
 * @param   pName       The name to match.
 */
static bool rtCrX509CpvIsNameExcluded(PRTCRX509CERTPATHSINT pThis, PCRTCRX509NAME pName)
{
    uint32_t i = pThis->v.cExcludedSubtrees;
    while (i-- > 0)
    {
        PCRTCRX509GENERALSUBTREES pSubTrees = pThis->v.papExcludedSubtrees[i];
        uint32_t j = pSubTrees->cItems;
        while (j-- > 0)
        {
            PCRTCRX509GENERALSUBTREE const pSubTree = pSubTrees->papItems[j];
            if (   RTCRX509GENERALNAME_IS_DIRECTORY_NAME(&pSubTree->Base)
                && RTCrX509Name_ConstraintMatch(&pSubTree->Base.u.pT4->DirectoryName, pName))
                return true;
        }
    }
    return false;
}


/**
 * Check if the given X.509 general name is excluded by current name
 * constraints.
 *
 * @returns true if excluded (caller sets error info), false if not explicitly
 *          excluded.
 * @param   pThis           The validator instance.
 * @param   pGeneralName    The name to match.
 */
static bool rtCrX509CpvIsGeneralNameExcluded(PRTCRX509CERTPATHSINT pThis, PCRTCRX509GENERALNAME pGeneralName)
{
    uint32_t i = pThis->v.cExcludedSubtrees;
    while (i-- > 0)
    {
        PCRTCRX509GENERALSUBTREES pSubTrees = pThis->v.papExcludedSubtrees[i];
        uint32_t j = pSubTrees->cItems;
        while (j-- > 0)
            if (RTCrX509GeneralName_ConstraintMatch(&pSubTrees->papItems[j]->Base, pGeneralName))
                return true;
    }
    return false;
}


/**
 * Creates a new node and inserts it.
 *
 * @param   pThis           The path builder & validator instance.
 * @param   pParent         The parent node. NULL for the root node.
 * @param   iDepth          The tree depth to insert at.
 * @param   pValidPolicy    The valid policy of the new node.
 * @param   pQualifiers     The qualifiers of the new node.
 * @param   pExpectedPolicy The (first) expected polcy of the new node.
 */
static bool rtCrX509CpvPolicyTreeInsertNew(PRTCRX509CERTPATHSINT pThis, PRTCRX509CERTPATHSPOLICYNODE pParent, uint32_t iDepth,
                                           PCRTASN1OBJID pValidPolicy, PCRTCRX509POLICYQUALIFIERINFOS pQualifiers,
                                           PCRTASN1OBJID pExpectedPolicy)
{
    Assert(iDepth <= pThis->v.cNodes);

    PRTCRX509CERTPATHSPOLICYNODE pNode;
    pNode = (PRTCRX509CERTPATHSPOLICYNODE)rtCrX509CpvAllocZ(pThis, sizeof(*pNode), "policy tree node");
    if (pNode)
    {
        pNode->pParent = pParent;
        if (pParent)
            RTListAppend(&pParent->ChildList, &pNode->SiblingEntry);
        else
        {
            Assert(pThis->v.pValidPolicyTree == NULL);
            pThis->v.pValidPolicyTree = pNode;
            RTListInit(&pNode->SiblingEntry);
        }
        RTListInit(&pNode->ChildList);
        RTListAppend(&pThis->v.paValidPolicyDepthLists[iDepth], &pNode->DepthEntry);

        pNode->pValidPolicy = pValidPolicy;
        pNode->pPolicyQualifiers = pQualifiers;
        pNode->pExpectedPolicyFirst = pExpectedPolicy;
        pNode->cMoreExpectedPolicySet = 0;
        pNode->papMoreExpectedPolicySet = NULL;
        return true;
    }
    return false;
}


/**
 * Unlinks and frees a node in the valid policy tree.
 *
 * @param   pThis           The path builder & validator instance.
 * @param   pNode           The node to destroy.
 */
static void rtCrX509CpvPolicyTreeDestroyNode(PRTCRX509CERTPATHSINT pThis, PRTCRX509CERTPATHSPOLICYNODE pNode)
{
    Assert(RTListIsEmpty(&pNode->ChildList));
    if (pNode->pParent)
        RTListNodeRemove(&pNode->SiblingEntry);
    else
        pThis->v.pValidPolicyTree = NULL;
    RTListNodeRemove(&pNode->DepthEntry);
    pNode->pParent = NULL;

    if (pNode->papMoreExpectedPolicySet)
    {
        RTMemFree(pNode->papMoreExpectedPolicySet);
        pNode->papMoreExpectedPolicySet = NULL;
    }
    RTMemFree(pNode);
}


/**
 * Unlinks and frees a sub-tree in the valid policy tree.
 *
 * @param   pThis           The path builder & validator instance.
 * @param   pNode           The node that is the root of the subtree.
 */
static void rtCrX509CpvPolicyTreeDestroySubtree(PRTCRX509CERTPATHSINT pThis, PRTCRX509CERTPATHSPOLICYNODE pNode)
{
    if (!RTListIsEmpty(&pNode->ChildList))
    {
        PRTCRX509CERTPATHSPOLICYNODE pCur = pNode;
        do
        {
            Assert(!RTListIsEmpty(&pCur->ChildList));

            /* Decend until we find a leaf. */
            do
                pCur = RTListGetFirst(&pCur->ChildList, RTCRX509CERTPATHSPOLICYNODE, SiblingEntry);
            while (!RTListIsEmpty(&pCur->ChildList));

            /* Remove it and all leafy siblings. */
            PRTCRX509CERTPATHSPOLICYNODE pParent = pCur->pParent;
            do
            {
                Assert(pCur != pNode);
                rtCrX509CpvPolicyTreeDestroyNode(pThis, pCur);
                pCur = RTListGetFirst(&pParent->ChildList, RTCRX509CERTPATHSPOLICYNODE, SiblingEntry);
                if (!pCur)
                {
                    pCur = pParent;
                    pParent = pParent->pParent;
                }
            } while (RTListIsEmpty(&pCur->ChildList) && pCur != pNode);
        } while (pCur != pNode);
    }

    rtCrX509CpvPolicyTreeDestroyNode(pThis, pNode);
}



/**
 * Destroys the entire policy tree.
 *
 * @param   pThis           The path builder & validator instance.
 */
static void rtCrX509CpvPolicyTreeDestroy(PRTCRX509CERTPATHSINT pThis)
{
    uint32_t i = pThis->v.cNodes + 1;
    while (i-- > 0)
    {
        PRTCRX509CERTPATHSPOLICYNODE pCur, pNext;
        RTListForEachSafe(&pThis->v.paValidPolicyDepthLists[i], pCur, pNext, RTCRX509CERTPATHSPOLICYNODE, DepthEntry)
        {
            rtCrX509CpvPolicyTreeDestroyNode(pThis, pCur);
        }
    }
}


/**
 * Removes all leaf nodes at level @a iDepth and above.
 *
 * @param   pThis           The path builder & validator instance.
 * @param   iDepth          The depth to start pruning at.
 */
static void rtCrX509CpvPolicyTreePrune(PRTCRX509CERTPATHSINT pThis, uint32_t iDepth)
{
    do
    {
        PRTLISTANCHOR pList = &pThis->v.paValidPolicyDepthLists[iDepth];
        PRTCRX509CERTPATHSPOLICYNODE pCur, pNext;
        RTListForEachSafe(pList, pCur, pNext, RTCRX509CERTPATHSPOLICYNODE, DepthEntry)
        {
            if (RTListIsEmpty(&pCur->ChildList))
                rtCrX509CpvPolicyTreeDestroyNode(pThis, pCur);
        }

    } while (iDepth-- > 0);
}


/**
 * Checks if @a pPolicy is the valid policy of a child of @a pNode.
 *
 * @returns true if in child node, false if not.
 * @param   pNode           The node which children to check.
 * @param   pPolicy         The valid policy to look for among the children.
 */
static bool rtCrX509CpvPolicyTreeIsChild(PRTCRX509CERTPATHSPOLICYNODE pNode, PCRTASN1OBJID pPolicy)
{
    PRTCRX509CERTPATHSPOLICYNODE pChild;
    RTListForEach(&pNode->ChildList, pChild, RTCRX509CERTPATHSPOLICYNODE, SiblingEntry)
    {
        if (RTAsn1ObjId_Compare(pChild->pValidPolicy, pPolicy) == 0)
            return true;
    }
    return true;
}


/**
 * Prunes the valid policy tree according to the specified user policy set.
 *
 * @returns Pointer to the policy object from @a papPolicies if found, NULL if
 *          no match.
 * @param   pObjId          The object ID to locate at match in the set.
 * @param   cPolicies       The number of policies in @a papPolicies.
 * @param   papPolicies     The policy set to search.
 */
static PCRTASN1OBJID rtCrX509CpvFindObjIdInPolicySet(PCRTASN1OBJID pObjId, uint32_t cPolicies, PCRTASN1OBJID *papPolicies)
{
    uint32_t i = cPolicies;
    while (i-- > 0)
        if (RTAsn1ObjId_Compare(pObjId, papPolicies[i]) == 0)
            return papPolicies[i];
    return NULL;
}


/**
 * Prunes the valid policy tree according to the specified user policy set.
 *
 * @returns success indicator (allocates memory)
 * @param   pThis           The path builder & validator instance.
 * @param   cPolicies       The number of policies in @a papPolicies.
 * @param   papPolicies     The user initial policies.
 */
static bool rtCrX509CpvPolicyTreeIntersect(PRTCRX509CERTPATHSINT pThis, uint32_t cPolicies, PCRTASN1OBJID *papPolicies)
{
    /*
     * 4.1.6.g.i - NULL tree remains NULL.
     */
    if (!pThis->v.pValidPolicyTree)
        return true;

    /*
     * 4.1.6.g.ii - If the user set includes anyPolicy, the whole tree is the
     *              result of the intersection.
     */
    uint32_t i = cPolicies;
    while (i-- > 0)
        if (RTAsn1ObjId_CompareWithString(papPolicies[i], RTCRX509_ID_CE_CP_ANY_POLICY_OID) == 0)
            return true;

    /*
     * 4.1.6.g.iii - Complicated.
     */
    PRTCRX509CERTPATHSPOLICYNODE pCur, pNext;
    PRTLISTANCHOR pList;

    /* 1 & 2: Delete nodes which parent has valid policy == anyPolicy and which
              valid policy is neither anyPolicy nor a member of papszPolicies.
              While doing so, construct a set of unused user policies that
              we'll replace anyPolicy nodes with in step 3. */
    uint32_t        cPoliciesLeft   = 0;
    PCRTASN1OBJID  *papPoliciesLeft = NULL;
    if (cPolicies)
    {
        papPoliciesLeft = (PCRTASN1OBJID *)rtCrX509CpvAllocZ(pThis, cPolicies * sizeof(papPoliciesLeft[0]), "papPoliciesLeft");
        if (!papPoliciesLeft)
            return false;
        for (i = 0; i < cPolicies; i++)
            papPoliciesLeft[i] = papPolicies[i];
    }

    for (uint32_t iDepth = 1; iDepth <= pThis->v.cNodes; iDepth++)
    {
        pList = &pThis->v.paValidPolicyDepthLists[iDepth];
        RTListForEachSafe(pList, pCur, pNext, RTCRX509CERTPATHSPOLICYNODE, DepthEntry)
        {
            Assert(pCur->pParent);
            if (   RTAsn1ObjId_CompareWithString(pCur->pParent->pValidPolicy, RTCRX509_ID_CE_CP_ANY_POLICY_OID) == 0
                && RTAsn1ObjId_CompareWithString(pCur->pValidPolicy, RTCRX509_ID_CE_CP_ANY_POLICY_OID) != 0)
            {
                PCRTASN1OBJID pFound = rtCrX509CpvFindObjIdInPolicySet(pCur->pValidPolicy, cPolicies, papPolicies);
                if (!pFound)
                    rtCrX509CpvPolicyTreeDestroySubtree(pThis, pCur);
                else
                    for (i = 0; i < cPoliciesLeft; i++)
                        if (papPoliciesLeft[i] == pFound)
                        {
                            cPoliciesLeft--;
                            if (i < cPoliciesLeft)
                                papPoliciesLeft[i] = papPoliciesLeft[cPoliciesLeft];
                            papPoliciesLeft[cPoliciesLeft] = NULL;
                            break;
                        }
            }
        }
    }

    /*
     * 4.1.5.g.iii.3 - Replace anyPolicy nodes on the final tree depth with
     *                 the policies in papPoliciesLeft.
     */
    pList = &pThis->v.paValidPolicyDepthLists[pThis->v.cNodes];
    RTListForEachSafe(pList, pCur, pNext, RTCRX509CERTPATHSPOLICYNODE, DepthEntry)
    {
        if (RTAsn1ObjId_CompareWithString(pCur->pValidPolicy, RTCRX509_ID_CE_CP_ANY_POLICY_OID) == 0)
        {
            for (i = 0; i < cPoliciesLeft; i++)
                rtCrX509CpvPolicyTreeInsertNew(pThis, pCur->pParent, pThis->v.cNodes - 1,
                                               papPoliciesLeft[i], pCur->pPolicyQualifiers, papPoliciesLeft[i]);
            rtCrX509CpvPolicyTreeDestroyNode(pThis, pCur);
        }
    }

    RTMemFree(papPoliciesLeft);

    /*
     * 4.1.5.g.iii.4 - Prune the tree
     */
    rtCrX509CpvPolicyTreePrune(pThis, pThis->v.cNodes - 1);

    return RT_SUCCESS(pThis->rc);
}



/**
 * Frees the path validator state.
 *
 * @param   pThis           The path builder & validator instance.
 */
static void rtCrX509CpvCleanup(PRTCRX509CERTPATHSINT pThis)
{
    /*
     * Destroy the policy tree and all its nodes.  We do this from the bottom
     * up via the depth lists, saving annoying tree traversal.
     */
    if (pThis->v.paValidPolicyDepthLists)
    {
        rtCrX509CpvPolicyTreeDestroy(pThis);

        RTMemFree(pThis->v.paValidPolicyDepthLists);
        pThis->v.paValidPolicyDepthLists = NULL;
    }

    Assert(pThis->v.pValidPolicyTree == NULL);
    pThis->v.pValidPolicyTree = NULL;

    /*
     * Destroy the name constraint arrays.
     */
    if (pThis->v.papPermittedSubtrees)
    {
        RTMemFree(pThis->v.papPermittedSubtrees);
        pThis->v.papPermittedSubtrees = NULL;
    }
    pThis->v.cPermittedSubtrees = 0;
    pThis->v.cPermittedSubtreesAlloc = 0;
    pThis->v.fNoPermittedSubtrees = false;

    if (pThis->v.papExcludedSubtrees)
    {
        RTMemFree(pThis->v.papExcludedSubtrees);
        pThis->v.papExcludedSubtrees = NULL;
    }
    pThis->v.cExcludedSubtrees = 0;

    /*
     * Clear other pointers.
     */
    pThis->v.pWorkingIssuer              = NULL;
    pThis->v.pWorkingPublicKey           = NULL;
    pThis->v.pWorkingPublicKeyAlgorithm  = NULL;
    pThis->v.pWorkingPublicKeyParameters = NULL;
}


/**
 * Initializes the state.
 *
 * Caller must check pThis->rc.
 *
 * @param   pThis           The path builder & validator instance.
 * @param   pTrustAnchor    The trust anchor node for the path that we're about
 *                          to validate.
 */
static void rtCrX509CpvInit(PRTCRX509CERTPATHSINT pThis, PRTCRX509CERTPATHNODE pTrustAnchor)
{
    rtCrX509CpvCleanup(pThis);

    /*
     * The node count does not include the trust anchor.
     */
    pThis->v.cNodes = pTrustAnchor->uDepth;

    /*
     * Valid policy tree starts with an anyPolicy node.
     */
    uint32_t i = pThis->v.cNodes + 1;
    pThis->v.paValidPolicyDepthLists = (PRTLISTANCHOR)rtCrX509CpvAllocZ(pThis, i * sizeof(RTLISTANCHOR),
                                                                        "paValidPolicyDepthLists");
    if (RT_UNLIKELY(!pThis->v.paValidPolicyDepthLists))
        return;
    while (i-- > 0)
        RTListInit(&pThis->v.paValidPolicyDepthLists[i]);

    if (!rtCrX509CpvPolicyTreeInsertNew(pThis, NULL, 0 /* iDepth*/, &pThis->AnyPolicyObjId, NULL, &pThis->AnyPolicyObjId))
        return;
    Assert(!RTListIsEmpty(&pThis->v.paValidPolicyDepthLists[0])); Assert(pThis->v.pValidPolicyTree);

    /*
     * Name constrains.
     */
    if (pThis->pInitialPermittedSubtrees)
        rtCrX509CpvAddPermittedSubtrees(pThis, pThis->pInitialPermittedSubtrees->cItems,
                                        pThis->pInitialPermittedSubtrees->papItems);
    if (pThis->pInitialExcludedSubtrees)
        rtCrX509CpvAddExcludedSubtrees(pThis, pThis->pInitialExcludedSubtrees);

    /*
     * Counters.
     */
    pThis->v.cExplicitPolicy        = pThis->cInitialExplicitPolicy;
    pThis->v.cInhibitPolicyMapping  = pThis->cInitialPolicyMappingInhibit;
    pThis->v.cInhibitAnyPolicy      = pThis->cInitialInhibitAnyPolicy;
    pThis->v.cMaxPathLength         = pThis->v.cNodes;

    /*
     * Certificate info from the trust anchor.
     */
    if (pTrustAnchor->pCert)
    {
        PCRTCRX509TBSCERTIFICATE const pTbsCert = &pTrustAnchor->pCert->TbsCertificate;
        pThis->v.pWorkingIssuer                 = &pTbsCert->Subject;
        pThis->v.pWorkingPublicKey              = &pTbsCert->SubjectPublicKeyInfo.SubjectPublicKey;
        pThis->v.pWorkingPublicKeyAlgorithm     = &pTbsCert->SubjectPublicKeyInfo.Algorithm.Algorithm;
        pThis->v.pWorkingPublicKeyParameters    = &pTbsCert->SubjectPublicKeyInfo.Algorithm.Parameters;
    }
    else
    {
        Assert(pTrustAnchor->pCertCtx); Assert(pTrustAnchor->pCertCtx->pTaInfo);

        PCRTCRTAFTRUSTANCHORINFO const  pTaInfo = pTrustAnchor->pCertCtx->pTaInfo;
        pThis->v.pWorkingIssuer                 = &pTaInfo->CertPath.TaName;
        pThis->v.pWorkingPublicKey              = &pTaInfo->PubKey.SubjectPublicKey;
        pThis->v.pWorkingPublicKeyAlgorithm     = &pTaInfo->PubKey.Algorithm.Algorithm;
        pThis->v.pWorkingPublicKeyParameters    = &pTaInfo->PubKey.Algorithm.Parameters;
    }
    if (   !RTASN1CORE_IS_PRESENT(&pThis->v.pWorkingPublicKeyParameters->u.Core)
        || pThis->v.pWorkingPublicKeyParameters->enmType == RTASN1TYPE_NULL)
        pThis->v.pWorkingPublicKeyParameters = NULL;
}


/**
 * This does basic trust anchor checks (similar to 6.1.3.a) before starting on
 * the RFC-5280 algorithm.
 */
static bool rtCrX509CpvMaybeCheckTrustAnchor(PRTCRX509CERTPATHSINT pThis, PRTCRX509CERTPATHNODE pTrustAnchor)
{
    /*
     * This is optional (not part of RFC-5280) and we need a full certificate
     * structure to do it.
     */
    if (!(pThis->fFlags & RTCRX509CERTPATHSINT_F_CHECK_TRUST_ANCHOR))
        return true;

    PCRTCRX509CERTIFICATE const pCert = pTrustAnchor->pCert;
    if (!pCert)
        return true;

    /*
     * Verify the certificate signature if self-signed.
     */
    if (RTCrX509Certificate_IsSelfSigned(pCert))
    {
        int rc = RTCrX509Certificate_VerifySignature(pCert, pThis->v.pWorkingPublicKeyAlgorithm,
                                                     pThis->v.pWorkingPublicKeyParameters, pThis->v.pWorkingPublicKey,
                                                     pThis->pErrInfo);
        if (RT_FAILURE(rc))
        {
            pThis->rc = rc;
            return false;
        }
    }

    /*
     * Verify that the certificate is valid at the specified time.
     */
    AssertCompile(sizeof(pThis->szTmp) >= 36 * 3);
    if (   (pThis->fFlags & RTCRX509CERTPATHSINT_F_VALID_TIME)
        && !RTCrX509Validity_IsValidAtTimeSpec(&pCert->TbsCertificate.Validity, &pThis->ValidTime))
        return rtCrX509CpvFailed(pThis, VERR_CR_X509_CPV_NOT_VALID_AT_TIME,
                                 "Certificate is not valid (ValidTime=%s Validity=[%s...%s])",
                                 RTTimeSpecToString(&pThis->ValidTime, &pThis->szTmp[0], 36),
                                 RTTimeToString(&pCert->TbsCertificate.Validity.NotBefore.Time, &pThis->szTmp[36], 36),
                                 RTTimeToString(&pCert->TbsCertificate.Validity.NotAfter.Time,  &pThis->szTmp[2*36], 36) );

    /*
     * Verified that the certficiate is not revoked.
     */
    /** @todo rainy day. */

    /*
     * If non-leaf certificate CA must be set, if basic constraints are present.
     */
    if (pTrustAnchor->pParent)
    {
        if (RTAsn1Integer_UnsignedCompareWithU32(&pTrustAnchor->pCert->TbsCertificate.T0.Version, RTCRX509TBSCERTIFICATE_V3) != 0)
            return rtCrX509CpvFailed(pThis, VERR_CR_X509_CPV_NOT_V3_CERT,
                                     "Only version 3 TA certificates are supported (Version=%llu)",
                                     pTrustAnchor->pCert->TbsCertificate.T0.Version.uValue);
        PCRTCRX509BASICCONSTRAINTS pBasicConstraints = pTrustAnchor->pCert->TbsCertificate.T3.pBasicConstraints;
        if (pBasicConstraints && !pBasicConstraints->CA.fValue)
            return rtCrX509CpvFailed(pThis, VERR_CR_X509_CPV_NOT_CA_CERT,
                                     "Trust anchor certificate is not marked as a CA");
    }

    return true;
}


/**
 * Step 6.1.3.a.
 */
static bool rtCrX509CpvCheckBasicCertInfo(PRTCRX509CERTPATHSINT pThis, PRTCRX509CERTPATHNODE pNode)
{
    /*
     * 6.1.3.a.1 - Verify the certificate signature.
     */
    int rc = RTCrX509Certificate_VerifySignature(pNode->pCert, pThis->v.pWorkingPublicKeyAlgorithm,
                                                 pThis->v.pWorkingPublicKeyParameters, pThis->v.pWorkingPublicKey,
                                                 pThis->pErrInfo);
    if (RT_FAILURE(rc))
    {
        pThis->rc = rc;
        return false;
    }

    /*
     * 6.1.3.a.2 - Verify that the certificate is valid at the specified time.
     */
    AssertCompile(sizeof(pThis->szTmp) >= 36 * 3);
    if (   (pThis->fFlags & RTCRX509CERTPATHSINT_F_VALID_TIME)
        && !RTCrX509Validity_IsValidAtTimeSpec(&pNode->pCert->TbsCertificate.Validity, &pThis->ValidTime))
        return rtCrX509CpvFailed(pThis, VERR_CR_X509_CPV_NOT_VALID_AT_TIME,
                                 "Certificate is not valid (ValidTime=%s Validity=[%s...%s])",
                                 RTTimeSpecToString(&pThis->ValidTime, &pThis->szTmp[0], 36),
                                 RTTimeToString(&pNode->pCert->TbsCertificate.Validity.NotBefore.Time, &pThis->szTmp[36], 36),
                                 RTTimeToString(&pNode->pCert->TbsCertificate.Validity.NotAfter.Time,  &pThis->szTmp[2*36], 36) );

    /*
     * 6.1.3.a.3 - Verified that the certficiate is not revoked.
     */
    /** @todo rainy day. */

    /*
     * 6.1.3.a.4 - Check the issuer name.
     */
    if (!RTCrX509Name_MatchByRfc5280(&pNode->pCert->TbsCertificate.Issuer, pThis->v.pWorkingIssuer))
        return rtCrX509CpvFailed(pThis, VERR_CR_X509_CPV_ISSUER_MISMATCH, "Issuer mismatch");

    return true;
}


/**
 * Step 6.1.3.b-c.
 */
static bool rtCrX509CpvCheckNameConstraints(PRTCRX509CERTPATHSINT pThis, PRTCRX509CERTPATHNODE pNode)
{
    if (pThis->v.fNoPermittedSubtrees)
        return rtCrX509CpvFailed(pThis, VERR_CR_X509_CPV_NO_PERMITTED_NAMES, "No permitted subtrees");

    if (   pNode->pCert->TbsCertificate.Subject.cItems > 0
        && (   !rtCrX509CpvIsNamePermitted(pThis, &pNode->pCert->TbsCertificate.Subject)
            || rtCrX509CpvIsNameExcluded(pThis, &pNode->pCert->TbsCertificate.Subject)) )
        return rtCrX509CpvFailed(pThis, VERR_CR_X509_CPV_NAME_NOT_PERMITTED,
                                 "Subject name is not permitted by current name constraints");

    PCRTCRX509GENERALNAMES pAltSubjectName = pNode->pCert->TbsCertificate.T3.pAltSubjectName;
    if (pAltSubjectName)
    {
        uint32_t i = pAltSubjectName->cItems;
        while (i-- > 0)
            if (   !rtCrX509CpvIsGeneralNamePermitted(pThis, pAltSubjectName->papItems[i])
                || rtCrX509CpvIsGeneralNameExcluded(pThis, pAltSubjectName->papItems[i]))
                return rtCrX509CpvFailed(pThis, VERR_CR_X509_CPV_ALT_NAME_NOT_PERMITTED,
                                         "Alternative name #%u is is not permitted by current name constraints", i);
    }

    return true;
}


/**
 * Step 6.1.3.d-f.
 */
static bool rtCrX509CpvWorkValidPolicyTree(PRTCRX509CERTPATHSINT pThis, uint32_t iDepth, PRTCRX509CERTPATHNODE pNode,
                                           bool fSelfIssued)
{
    PCRTCRX509CERTIFICATEPOLICIES pPolicies = pNode->pCert->TbsCertificate.T3.pCertificatePolicies;
    if (pPolicies)
    {
        /*
         * 6.1.3.d.1 - Work the certiciate policies into the tree.
         */
        PRTCRX509CERTPATHSPOLICYNODE    pCur;
        PRTLISTANCHOR                   pListAbove  = &pThis->v.paValidPolicyDepthLists[iDepth - 1];
        uint32_t                        iAnyPolicy  = UINT32_MAX;
        uint32_t                        i           = pPolicies->cItems;
        while (i-- > 0)
        {
            PCRTCRX509POLICYQUALIFIERINFOS const    pQualifiers = &pPolicies->papItems[i]->PolicyQualifiers;
            PCRTASN1OBJID const                     pIdP        = &pPolicies->papItems[i]->PolicyIdentifier;
            if (RTAsn1ObjId_CompareWithString(pIdP, RTCRX509_ID_CE_CP_ANY_POLICY_OID) == 0)
            {
                iAnyPolicy++;
                continue;
            }

            /*
             * 6.1.3.d.1.i - Create children for matching policies.
             */
            uint32_t cMatches = 0;
            RTListForEach(pListAbove, pCur, RTCRX509CERTPATHSPOLICYNODE, DepthEntry)
            {
                bool fMatch = RTAsn1ObjId_Compare(pCur->pExpectedPolicyFirst, pIdP) == 0;
                if (!fMatch && pCur->cMoreExpectedPolicySet)
                    for (uint32_t j = 0; !fMatch && j < pCur->cMoreExpectedPolicySet; j++)
                        fMatch = RTAsn1ObjId_Compare(pCur->papMoreExpectedPolicySet[j], pIdP) == 0;
                if (fMatch)
                {
                    if (!rtCrX509CpvPolicyTreeInsertNew(pThis, pCur, iDepth, pIdP, pQualifiers, pIdP))
                        return false;
                    cMatches++;
                }
            }

            /*
             * 6.1.3.d.1.ii - If no matches above do the same for anyPolicy
             *                nodes, only match with valid policy this time.
             */
            if (cMatches == 0)
            {
                RTListForEach(pListAbove, pCur, RTCRX509CERTPATHSPOLICYNODE, DepthEntry)
                {
                    if (RTAsn1ObjId_CompareWithString(pCur->pExpectedPolicyFirst, RTCRX509_ID_CE_CP_ANY_POLICY_OID) == 0)
                    {
                        if (!rtCrX509CpvPolicyTreeInsertNew(pThis, pCur, iDepth, pIdP, pQualifiers, pIdP))
                            return false;
                    }
                }
            }
        }

        /*
         * 6.1.3.d.2 - If anyPolicy present, make sure all expected policies
         *             are propagated to the current depth.
         */
        if (   iAnyPolicy < pPolicies->cItems
            && (   pThis->v.cInhibitAnyPolicy > 0
                || (pNode->pParent && fSelfIssued) ) )
        {
            PCRTCRX509POLICYQUALIFIERINFOS pApQ = &pPolicies->papItems[iAnyPolicy]->PolicyQualifiers;
            RTListForEach(pListAbove, pCur, RTCRX509CERTPATHSPOLICYNODE, DepthEntry)
            {
                if (!rtCrX509CpvPolicyTreeIsChild(pCur, pCur->pExpectedPolicyFirst))
                    rtCrX509CpvPolicyTreeInsertNew(pThis, pCur, iDepth, pCur->pExpectedPolicyFirst, pApQ,
                                                   pCur->pExpectedPolicyFirst);
                for (uint32_t j = 0; j < pCur->cMoreExpectedPolicySet; j++)
                    if (!rtCrX509CpvPolicyTreeIsChild(pCur, pCur->papMoreExpectedPolicySet[j]))
                        rtCrX509CpvPolicyTreeInsertNew(pThis, pCur, iDepth, pCur->papMoreExpectedPolicySet[j], pApQ,
                                                       pCur->papMoreExpectedPolicySet[j]);
            }
        }
        /*
         * 6.1.3.d.3 - Prune the tree.
         */
        else
            rtCrX509CpvPolicyTreePrune(pThis, iDepth - 1);
    }
    else
    {
        /*
         * 6.1.3.e - No policy extension present, set tree to NULL.
         */
        rtCrX509CpvPolicyTreeDestroy(pThis);
    }

    /*
     * 6.1.3.f - NULL tree check.
     */
    if (   pThis->v.pValidPolicyTree == NULL
        && pThis->v.cExplicitPolicy == 0)
        return rtCrX509CpvFailed(pThis, VERR_CR_X509_CPV_NO_VALID_POLICY,
                                 "An explicit policy is called for but the valid policy tree is NULL.");
    return RT_SUCCESS(pThis->rc);
}


/**
 * Step 6.1.4.a-b.
 */
static bool rtCrX509CpvSoakUpPolicyMappings(PRTCRX509CERTPATHSINT pThis, uint32_t iDepth,
                                            PCRTCRX509POLICYMAPPINGS pPolicyMappings)
{
    /*
     * 6.1.4.a - The anyPolicy is not allowed in policy mappings as it would
     *           allow an evil intermediate certificate to expand the policy
     *           scope of a certiciate chain without regard to upstream.
     */
    uint32_t i = pPolicyMappings->cItems;
    while (i-- > 0)
    {
        PCRTCRX509POLICYMAPPING const pOne = pPolicyMappings->papItems[i];
        if (RTAsn1ObjId_CompareWithString(&pOne->IssuerDomainPolicy, RTCRX509_ID_CE_CP_ANY_POLICY_OID) == 0)
            return rtCrX509CpvFailed(pThis, VERR_CR_X509_CPV_INVALID_POLICY_MAPPING,
                                     "Invalid policy mapping %#u: IssuerDomainPolicy is anyPolicy.", i);

        if (RTAsn1ObjId_CompareWithString(&pOne->SubjectDomainPolicy, RTCRX509_ID_CE_CP_ANY_POLICY_OID) == 0)
            return rtCrX509CpvFailed(pThis, VERR_CR_X509_CPV_INVALID_POLICY_MAPPING,
                                     "Invalid policy mapping %#u: SubjectDomainPolicy is anyPolicy.", i);
    }

    PRTCRX509CERTPATHSPOLICYNODE pCur, pNext;
    if (pThis->v.cInhibitPolicyMapping > 0)
    {
        /*
         * 6.1.4.b.1 - Do the policy mapping.
         */
        i = pPolicyMappings->cItems;
        while (i-- > 0)
        {
            PCRTCRX509POLICYMAPPING const pOne = pPolicyMappings->papItems[i];

            uint32_t cFound = 0;
            RTListForEach(&pThis->v.paValidPolicyDepthLists[iDepth], pCur, RTCRX509CERTPATHSPOLICYNODE, DepthEntry)
            {
                if (RTAsn1ObjId_Compare(pCur->pValidPolicy, &pOne->IssuerDomainPolicy))
                {
                    if (!pCur->fAlreadyMapped)
                    {
                        pCur->fAlreadyMapped = true;
                        pCur->pExpectedPolicyFirst = &pOne->SubjectDomainPolicy;
                    }
                    else
                    {
                        uint32_t iExpected = pCur->cMoreExpectedPolicySet;
                        void *pvNew = RTMemRealloc(pCur->papMoreExpectedPolicySet,
                                                   sizeof(pCur->papMoreExpectedPolicySet[0]) * (iExpected + 1));
                        if (!pvNew)
                            return rtCrX509CpvFailed(pThis, VERR_NO_MEMORY,
                                                     "Error growing papMoreExpectedPolicySet array (cur %u, depth %u)",
                                                     pCur->cMoreExpectedPolicySet, iDepth);
                        pCur->papMoreExpectedPolicySet = (PCRTASN1OBJID *)pvNew;
                        pCur->papMoreExpectedPolicySet[iExpected] = &pOne->SubjectDomainPolicy;
                        pCur->cMoreExpectedPolicySet = iExpected  + 1;
                    }
                    cFound++;
                }
            }

            /*
             * If no mapping took place, look for an anyPolicy node.
             */
            if (!cFound)
            {
                RTListForEach(&pThis->v.paValidPolicyDepthLists[iDepth], pCur, RTCRX509CERTPATHSPOLICYNODE, DepthEntry)
                {
                    if (RTAsn1ObjId_CompareWithString(pCur->pValidPolicy, RTCRX509_ID_CE_CP_ANY_POLICY_OID) == 0)
                    {
                        if (!rtCrX509CpvPolicyTreeInsertNew(pThis, pCur->pParent, iDepth,
                                                            &pOne->IssuerDomainPolicy,
                                                            pCur->pPolicyQualifiers,
                                                            &pOne->SubjectDomainPolicy))
                            return false;
                        break;
                    }
                }
            }
        }
    }
    else
    {
        /*
         * 6.1.4.b.2 - Remove matching policies from the tree if mapping is
         *             inhibited and prune the tree.
         */
        uint32_t cRemoved = 0;
        i = pPolicyMappings->cItems;
        while (i-- > 0)
        {
            PCRTCRX509POLICYMAPPING const pOne = pPolicyMappings->papItems[i];
            RTListForEachSafe(&pThis->v.paValidPolicyDepthLists[iDepth], pCur, pNext, RTCRX509CERTPATHSPOLICYNODE, DepthEntry)
            {
                if (RTAsn1ObjId_Compare(pCur->pValidPolicy, &pOne->IssuerDomainPolicy))
                {
                    rtCrX509CpvPolicyTreeDestroyNode(pThis, pCur);
                    cRemoved++;
                }
            }
        }
        if (cRemoved)
            rtCrX509CpvPolicyTreePrune(pThis, iDepth - 1);
    }

    return true;
}


/**
 * Step 6.1.4.d-f & 6.1.5.c-e.
 */
static void rtCrX509CpvSetWorkingPublicKeyInfo(PRTCRX509CERTPATHSINT pThis, PRTCRX509CERTPATHNODE pNode)
{
    PCRTCRX509TBSCERTIFICATE const pTbsCert = &pNode->pCert->TbsCertificate;

    /*
     * 6.1.4.d - The public key.
     */
    pThis->v.pWorkingPublicKey = &pTbsCert->SubjectPublicKeyInfo.SubjectPublicKey;

    /*
     * 6.1.4.e - The public key parameters.  Use new ones if present, keep old
     *           if the algorithm remains the same.
     */
    if (   RTASN1CORE_IS_PRESENT(&pTbsCert->SubjectPublicKeyInfo.Algorithm.Parameters.u.Core)
        && pTbsCert->SubjectPublicKeyInfo.Algorithm.Parameters.enmType != RTASN1TYPE_NULL)
        pThis->v.pWorkingPublicKeyParameters = &pTbsCert->SubjectPublicKeyInfo.Algorithm.Parameters;
    else if (   pThis->v.pWorkingPublicKeyParameters
             && RTAsn1ObjId_Compare(pThis->v.pWorkingPublicKeyAlgorithm, &pTbsCert->SubjectPublicKeyInfo.Algorithm.Algorithm) != 0)
        pThis->v.pWorkingPublicKeyParameters = NULL;

    /*
     * 6.1.4.f - The public algorithm.
     */
    pThis->v.pWorkingPublicKeyAlgorithm = &pTbsCert->SubjectPublicKeyInfo.Algorithm.Algorithm;
}


/**
 * Step 6.1.4.g.
 */
static bool rtCrX509CpvSoakUpNameConstraints(PRTCRX509CERTPATHSINT pThis, PCRTCRX509NAMECONSTRAINTS pNameConstraints)
{
    if (pNameConstraints->T0.PermittedSubtrees.cItems > 0)
        if (!rtCrX509CpvIntersectionPermittedSubtrees(pThis, &pNameConstraints->T0.PermittedSubtrees))
            return false;

    if (pNameConstraints->T1.ExcludedSubtrees.cItems > 0)
        if (!rtCrX509CpvAddExcludedSubtrees(pThis, &pNameConstraints->T1.ExcludedSubtrees))
            return false;

    return true;
}


/**
 * Step 6.1.4.i.
 */
static bool rtCrX509CpvSoakUpPolicyConstraints(PRTCRX509CERTPATHSINT pThis, PCRTCRX509POLICYCONSTRAINTS pPolicyConstraints)
{
    if (RTAsn1Integer_IsPresent(&pPolicyConstraints->RequireExplicitPolicy))
    {
        if (RTAsn1Integer_UnsignedCompareWithU32(&pPolicyConstraints->RequireExplicitPolicy, pThis->v.cExplicitPolicy) < 0)
            pThis->v.cExplicitPolicy = pPolicyConstraints->RequireExplicitPolicy.uValue.s.Lo;
    }

    if (RTAsn1Integer_IsPresent(&pPolicyConstraints->InhibitPolicyMapping))
    {
        if (RTAsn1Integer_UnsignedCompareWithU32(&pPolicyConstraints->InhibitPolicyMapping, pThis->v.cInhibitPolicyMapping) < 0)
            pThis->v.cInhibitPolicyMapping = pPolicyConstraints->InhibitPolicyMapping.uValue.s.Lo;
    }
    return true;
}


/**
 * Step 6.1.4.j.
 */
static bool rtCrX509CpvSoakUpInhibitAnyPolicy(PRTCRX509CERTPATHSINT pThis, PCRTASN1INTEGER pInhibitAnyPolicy)
{
    if (RTAsn1Integer_UnsignedCompareWithU32(pInhibitAnyPolicy, pThis->v.cInhibitAnyPolicy) < 0)
        pThis->v.cInhibitAnyPolicy = pInhibitAnyPolicy->uValue.s.Lo;
    return true;
}


/**
 * Steps 6.1.4.k, 6.1.4.l, 6.1.4.m, and 6.1.4.n.
 */
static bool rtCrX509CpvCheckAndSoakUpBasicConstraintsAndKeyUsage(PRTCRX509CERTPATHSINT pThis, PRTCRX509CERTPATHNODE pNode,
                                                                 bool fSelfIssued)
{
    /* 6.1.4.k - If basic constraints present, CA must be set. */
    if (RTAsn1Integer_UnsignedCompareWithU32(&pNode->pCert->TbsCertificate.T0.Version, RTCRX509TBSCERTIFICATE_V3) != 0)
    {
        /* Note! Add flags if support for older certificates is needed later. */
        return rtCrX509CpvFailed(pThis, VERR_CR_X509_CPV_NOT_V3_CERT,
                                 "Only version 3 certificates are supported (Version=%llu)",
                                 pNode->pCert->TbsCertificate.T0.Version.uValue);
    }
    PCRTCRX509BASICCONSTRAINTS pBasicConstraints = pNode->pCert->TbsCertificate.T3.pBasicConstraints;
    if (pBasicConstraints)
    {
        if (!pBasicConstraints->CA.fValue)
            return rtCrX509CpvFailed(pThis, VERR_CR_X509_CPV_NOT_CA_CERT,
                                     "Intermediate certificate (#%u) is not marked as a CA", pThis->v.iNode);
    }

    /* 6.1.4.l - Work cMaxPathLength. */
    if (!fSelfIssued)
    {
        if (pThis->v.cMaxPathLength > 0)
            pThis->v.cMaxPathLength--;
        else
            return rtCrX509CpvFailed(pThis, VERR_CR_X509_CPV_MAX_PATH_LENGTH,
                                     "Hit max path length at node #%u", pThis->v.iNode);
    }

    /* 6.1.4.m - Update cMaxPathLength if basic constrain field is present and smaller. */
    if (pBasicConstraints)
    {
        if (RTAsn1Integer_IsPresent(&pBasicConstraints->PathLenConstraint))
            if (RTAsn1Integer_UnsignedCompareWithU32(&pBasicConstraints->PathLenConstraint, pThis->v.cMaxPathLength) < 0)
                pThis->v.cMaxPathLength = pBasicConstraints->PathLenConstraint.uValue.s.Lo;
    }

    /* 6.1.4.n - Require keyCertSign in key usage if the extension is present. */
    PCRTCRX509TBSCERTIFICATE const pTbsCert = &pNode->pCert->TbsCertificate;
    if (   (pTbsCert->T3.fFlags     & RTCRX509TBSCERTIFICATE_F_PRESENT_KEY_USAGE)
        && !(pTbsCert->T3.fKeyUsage & RTCRX509CERT_KEY_USAGE_F_KEY_CERT_SIGN))
        return rtCrX509CpvFailed(pThis, VERR_CR_X509_CPV_MISSING_KEY_CERT_SIGN,
                                 "Node #%u does not have KeyCertSign set (keyUsage=%#x)",
                                 pThis->v.iNode, pTbsCert->T3.fKeyUsage);

    return true;
}


/**
 * Step 6.1.4.o - check out critical extensions.
 */
static bool rtCrX509CpvCheckCriticalExtensions(PRTCRX509CERTPATHSINT pThis, PRTCRX509CERTPATHNODE pNode)
{
    uint32_t                  cLeft = pNode->pCert->TbsCertificate.T3.Extensions.cItems;
    PRTCRX509EXTENSION const *ppCur = pNode->pCert->TbsCertificate.T3.Extensions.papItems;
    while (cLeft-- > 0)
    {
        PCRTCRX509EXTENSION const pCur = *ppCur;
        if (pCur->Critical.fValue)
        {
            if (   RTAsn1ObjId_CompareWithString(&pCur->ExtnId, RTCRX509_ID_CE_KEY_USAGE_OID) != 0
                && RTAsn1ObjId_CompareWithString(&pCur->ExtnId, RTCRX509_ID_CE_SUBJECT_ALT_NAME_OID) != 0
                && RTAsn1ObjId_CompareWithString(&pCur->ExtnId, RTCRX509_ID_CE_ISSUER_ALT_NAME_OID) != 0
                && RTAsn1ObjId_CompareWithString(&pCur->ExtnId, RTCRX509_ID_CE_BASIC_CONSTRAINTS_OID) != 0
                && RTAsn1ObjId_CompareWithString(&pCur->ExtnId, RTCRX509_ID_CE_NAME_CONSTRAINTS_OID) != 0
                && RTAsn1ObjId_CompareWithString(&pCur->ExtnId, RTCRX509_ID_CE_CERTIFICATE_POLICIES_OID) != 0
                && RTAsn1ObjId_CompareWithString(&pCur->ExtnId, RTCRX509_ID_CE_POLICY_MAPPINGS_OID) != 0
                && RTAsn1ObjId_CompareWithString(&pCur->ExtnId, RTCRX509_ID_CE_POLICY_CONSTRAINTS_OID) != 0
                && RTAsn1ObjId_CompareWithString(&pCur->ExtnId, RTCRX509_ID_CE_EXT_KEY_USAGE_OID) != 0
                && RTAsn1ObjId_CompareWithString(&pCur->ExtnId, RTCRX509_ID_CE_INHIBIT_ANY_POLICY_OID) != 0
                && RTAsn1ObjId_CompareWithString(&pCur->ExtnId, RTCR_APPLE_CS_DEVID_APPLICATION_OID) != 0
                && RTAsn1ObjId_CompareWithString(&pCur->ExtnId, RTCR_APPLE_CS_DEVID_INSTALLER_OID) != 0
                && RTAsn1ObjId_CompareWithString(&pCur->ExtnId, RTCR_APPLE_CS_DEVID_KEXT_OID) != 0
                && RTAsn1ObjId_CompareWithString(&pCur->ExtnId, RTCR_APPLE_CS_DEVID_IPHONE_SW_DEV_OID) != 0
                && RTAsn1ObjId_CompareWithString(&pCur->ExtnId, RTCR_APPLE_CS_DEVID_MAC_SW_DEV_OID) != 0
               )
            {
                /* @bugref{10130}: An IntelGraphicsPE2021 cert issued by iKG_AZSKGFDCS has a critical subjectKeyIdentifier
                                   which we quietly ignore here. RFC-5280 conforming CAs should not mark this as critical.
                                   On an end entity this extension can have relevance to path construction. */
                if (   pNode->uSrc == RTCRX509CERTPATHNODE_SRC_TARGET
                    && RTAsn1ObjId_CompareWithString(&pCur->ExtnId, RTCRX509_ID_CE_SUBJECT_KEY_IDENTIFIER_OID) == 0)
                    LogFunc(("Ignoring non-standard subjectKeyIdentifier on target certificate.\n"));
                else
                    return rtCrX509CpvFailed(pThis, VERR_CR_X509_CPV_UNKNOWN_CRITICAL_EXTENSION,
                                             "Node #%u has an unknown critical extension: %s",
                                             pThis->v.iNode, pCur->ExtnId.szObjId);
            }
        }

        ppCur++;
    }

    return true;
}


/**
 * Step 6.1.5 - The wrapping up.
 */
static bool rtCrX509CpvWrapUp(PRTCRX509CERTPATHSINT pThis, PRTCRX509CERTPATHNODE pNode)
{
    Assert(!pNode->pParent); Assert(pThis->pTarget == pNode->pCert);

    /*
     * 6.1.5.a - Decrement explicit policy.
     */
    if (pThis->v.cExplicitPolicy > 0)
        pThis->v.cExplicitPolicy--;

    /*
     * 6.1.5.b - Policy constraints and explicit policy.
     */
    PCRTCRX509POLICYCONSTRAINTS pPolicyConstraints = pNode->pCert->TbsCertificate.T3.pPolicyConstraints;
    if (   pPolicyConstraints
        && RTAsn1Integer_IsPresent(&pPolicyConstraints->RequireExplicitPolicy)
        && RTAsn1Integer_UnsignedCompareWithU32(&pPolicyConstraints->RequireExplicitPolicy, 0) == 0)
        pThis->v.cExplicitPolicy = 0;

    /*
     * 6.1.5.c-e - Update working public key info.
     */
    rtCrX509CpvSetWorkingPublicKeyInfo(pThis, pNode);

    /*
     * 6.1.5.f - Critical extensions.
     */
    if (!rtCrX509CpvCheckCriticalExtensions(pThis, pNode))
        return false;

    /*
     * 6.1.5.g - Calculate the intersection between the user initial policy set
     *           and the valid policy tree.
     */
    rtCrX509CpvPolicyTreeIntersect(pThis, pThis->cInitialUserPolicySet, pThis->papInitialUserPolicySet);

    if (   pThis->v.cExplicitPolicy == 0
        && pThis->v.pValidPolicyTree == NULL)
        return rtCrX509CpvFailed(pThis, VERR_CR_X509_CPV_NO_VALID_POLICY, "No valid policy (wrap-up).");

    return true;
}


/**
 * Worker that validates one path.
 *
 * This implements the algorithm in RFC-5280, section 6.1, with exception of
 * the CRL checks in 6.1.3.a.3.
 *
 * @returns success indicator.
 * @param   pThis           The path builder & validator instance.
 * @param   pTrustAnchor    The trust anchor node.
 */
static bool rtCrX509CpvOneWorker(PRTCRX509CERTPATHSINT pThis, PRTCRX509CERTPATHNODE pTrustAnchor)
{
    /*
     * Init.
     */
    rtCrX509CpvInit(pThis, pTrustAnchor);
    if (RT_SUCCESS(pThis->rc))
    {
        /*
         * Maybe do some trust anchor checks.
         */
        if (!rtCrX509CpvMaybeCheckTrustAnchor(pThis, pTrustAnchor))
        {
            AssertStmt(RT_FAILURE_NP(pThis->rc), pThis->rc = VERR_CR_X509_CERTPATHS_INTERNAL_ERROR);
            return false;
        }

        /*
         * Special case, target certificate is trusted.
         */
        if (!pTrustAnchor->pParent)
            return true; /* rtCrX509CpvWrapUp should not be needed here. */

        /*
         * Normal processing.
         */
        PRTCRX509CERTPATHNODE   pNode = pTrustAnchor->pParent;
        uint32_t                iNode = pThis->v.iNode = 1; /* We count to cNode (inclusive).  Same a validation tree depth. */
        while (pNode && RT_SUCCESS(pThis->rc))
        {
            /*
             * Basic certificate processing.
             */
            if (!rtCrX509CpvCheckBasicCertInfo(pThis, pNode))                                           /* Step 6.1.3.a */
                break;

            bool const fSelfIssued = rtCrX509CertPathsIsSelfIssued(pNode);
            if (!fSelfIssued || !pNode->pParent)                                                        /* Step 6.1.3.b-c */
                if (!rtCrX509CpvCheckNameConstraints(pThis, pNode))
                    break;

            if (!rtCrX509CpvWorkValidPolicyTree(pThis, iNode, pNode, fSelfIssued))                      /* Step 6.1.3.d-f */
                break;

            /*
             * If it's the last certificate in the path, do wrap-ups.
             */
            if (!pNode->pParent)                                                                         /* Step 6.1.5 */
            {
                Assert(iNode == pThis->v.cNodes);
                if (!rtCrX509CpvWrapUp(pThis, pNode))
                    break;
                AssertRCBreak(pThis->rc);
                return true;
            }

            /*
             * Preparations for the next certificate.
             */
            PCRTCRX509TBSCERTIFICATE const pTbsCert = &pNode->pCert->TbsCertificate;
            if (   pTbsCert->T3.pPolicyMappings
                && !rtCrX509CpvSoakUpPolicyMappings(pThis, iNode, pTbsCert->T3.pPolicyMappings))        /* Step 6.1.4.a-b */
                break;

            pThis->v.pWorkingIssuer = &pTbsCert->Subject;                                               /* Step 6.1.4.c */

            rtCrX509CpvSetWorkingPublicKeyInfo(pThis, pNode);                                           /* Step 6.1.4.d-f */

            if (   pTbsCert->T3.pNameConstraints                                                        /* Step 6.1.4.g */
                && !rtCrX509CpvSoakUpNameConstraints(pThis, pTbsCert->T3.pNameConstraints))
                break;

            if (!fSelfIssued)                                                                           /* Step 6.1.4.h */
            {
                if (pThis->v.cExplicitPolicy > 0)
                    pThis->v.cExplicitPolicy--;
                if (pThis->v.cInhibitPolicyMapping > 0)
                    pThis->v.cInhibitPolicyMapping--;
                if (pThis->v.cInhibitAnyPolicy > 0)
                    pThis->v.cInhibitAnyPolicy--;
            }

            if (   pTbsCert->T3.pPolicyConstraints                                                      /* Step 6.1.4.j */
                && !rtCrX509CpvSoakUpPolicyConstraints(pThis, pTbsCert->T3.pPolicyConstraints))
                break;

            if (   pTbsCert->T3.pInhibitAnyPolicy                                                       /* Step 6.1.4.j */
                && !rtCrX509CpvSoakUpInhibitAnyPolicy(pThis, pTbsCert->T3.pInhibitAnyPolicy))
                break;

            if (!rtCrX509CpvCheckAndSoakUpBasicConstraintsAndKeyUsage(pThis, pNode, fSelfIssued))       /* Step 6.1.4.k-n */
                break;

            if (!rtCrX509CpvCheckCriticalExtensions(pThis, pNode))                                      /* Step 6.1.4.o */
                break;

            /*
             * Advance to the next certificate.
             */
            pNode = pNode->pParent;
            pThis->v.iNode = ++iNode;
        }
        AssertStmt(RT_FAILURE_NP(pThis->rc), pThis->rc = VERR_CR_X509_CERTPATHS_INTERNAL_ERROR);
    }
    return false;
}


RTDECL(int) RTCrX509CertPathsValidateOne(RTCRX509CERTPATHS hCertPaths, uint32_t iPath, PRTERRINFO pErrInfo)
{
    /*
     * Validate the input.
     */
    PRTCRX509CERTPATHSINT pThis = hCertPaths;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTCRX509CERTPATHSINT_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(!(pThis->fFlags & ~RTCRX509CERTPATHSINT_F_VALID_MASK), VERR_INVALID_PARAMETER);
    AssertPtrReturn(pThis->pTarget, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pThis->pRoot, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->rc == VINF_SUCCESS, VERR_INVALID_PARAMETER);

    /*
     * Locate the path and validate it.
     */
    int rc;
    if (iPath < pThis->cPaths)
    {
        PRTCRX509CERTPATHNODE pLeaf = rtCrX509CertPathsGetLeafByIndex(pThis, iPath);
        if (pLeaf)
        {
            if (RTCRX509CERTPATHNODE_SRC_IS_TRUSTED(pLeaf->uSrc))
            {
                pThis->pErrInfo = pErrInfo;
                rtCrX509CpvOneWorker(pThis, pLeaf);
                pThis->pErrInfo = NULL;
                rc = pThis->rc;
                pThis->rc = VINF_SUCCESS;
            }
            else
                rc = RTErrInfoSetF(pErrInfo, VERR_CR_X509_NO_TRUST_ANCHOR, "Path #%u is does not have a trust anchor: uSrc=%s",
                                   iPath, rtCrX509CertPathsNodeGetSourceName(pLeaf));
            pLeaf->rcVerify = rc;
        }
        else
            rc = VERR_CR_X509_CERTPATHS_INTERNAL_ERROR;
    }
    else
        rc = VERR_NOT_FOUND;
    return rc;
}


RTDECL(int) RTCrX509CertPathsValidateAll(RTCRX509CERTPATHS hCertPaths, uint32_t *pcValidPaths, PRTERRINFO pErrInfo)
{
    /*
     * Validate the input.
     */
    PRTCRX509CERTPATHSINT pThis = hCertPaths;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTCRX509CERTPATHSINT_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(!(pThis->fFlags & ~RTCRX509CERTPATHSINT_F_VALID_MASK), VERR_INVALID_PARAMETER);
    AssertPtrReturn(pThis->pTarget, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pThis->pRoot, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->rc == VINF_SUCCESS, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pcValidPaths, VERR_INVALID_POINTER);

    /*
     * Validate the paths.
     */
    pThis->pErrInfo = pErrInfo;

    int      rcLastFailure = VINF_SUCCESS;
    uint32_t cValidPaths   = 0;
    PRTCRX509CERTPATHNODE pCurLeaf;
    RTListForEach(&pThis->LeafList, pCurLeaf, RTCRX509CERTPATHNODE, ChildListOrLeafEntry)
    {
        if (RTCRX509CERTPATHNODE_SRC_IS_TRUSTED(pCurLeaf->uSrc))
        {
            rtCrX509CpvOneWorker(hCertPaths, pCurLeaf);
            if (RT_SUCCESS(pThis->rc))
                cValidPaths++;
            else
                rcLastFailure = pThis->rc;
            pCurLeaf->rcVerify = pThis->rc;
            pThis->rc = VINF_SUCCESS;
        }
        else
            pCurLeaf->rcVerify = VERR_CR_X509_NO_TRUST_ANCHOR;
    }

    pThis->pErrInfo = NULL;

    if (pcValidPaths)
        *pcValidPaths = cValidPaths;
    if (cValidPaths > 0)
        return VINF_SUCCESS;
    if (RT_SUCCESS_NP(rcLastFailure))
        return RTErrInfoSetF(pErrInfo, VERR_CR_X509_CPV_NO_TRUSTED_PATHS,
                             "None of the %u path(s) have a trust anchor.", pThis->cPaths);
    return rcLastFailure;
}


RTDECL(uint32_t) RTCrX509CertPathsGetPathCount(RTCRX509CERTPATHS hCertPaths)
{
    /*
     * Validate the input.
     */
    PRTCRX509CERTPATHSINT pThis = hCertPaths;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTCRX509CERTPATHSINT_MAGIC, UINT32_MAX);
    AssertPtrReturn(pThis->pRoot, UINT32_MAX);

    /*
     * Return data.
     */
    return pThis->cPaths;
}


RTDECL(int)  RTCrX509CertPathsQueryPathInfo(RTCRX509CERTPATHS hCertPaths, uint32_t iPath,
                                            bool *pfTrusted, uint32_t *pcNodes, PCRTCRX509NAME *ppSubject,
                                            PCRTCRX509SUBJECTPUBLICKEYINFO *ppPublicKeyInfo,
                                            PCRTCRX509CERTIFICATE *ppCert, PCRTCRCERTCTX *ppCertCtx,
                                            int *prcVerify)
{
    /*
     * Validate the input.
     */
    PRTCRX509CERTPATHSINT pThis = hCertPaths;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTCRX509CERTPATHSINT_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pThis->pRoot, VERR_WRONG_ORDER);
    AssertReturn(iPath < pThis->cPaths, VERR_NOT_FOUND);

    /*
     * Get the data.
     */
    PRTCRX509CERTPATHNODE pLeaf = rtCrX509CertPathsGetLeafByIndex(pThis, iPath);
    AssertReturn(pLeaf, VERR_CR_X509_INTERNAL_ERROR);

    if (pfTrusted)
        *pfTrusted = RTCRX509CERTPATHNODE_SRC_IS_TRUSTED(pLeaf->uSrc);

    if (pcNodes)
        *pcNodes = pLeaf->uDepth + 1; /* Includes both trust anchor and target. */

    if (ppSubject)
        *ppSubject = pLeaf->pCert ? &pLeaf->pCert->TbsCertificate.Subject : &pLeaf->pCertCtx->pTaInfo->CertPath.TaName;

    if (ppPublicKeyInfo)
        *ppPublicKeyInfo = pLeaf->pCert ? &pLeaf->pCert->TbsCertificate.SubjectPublicKeyInfo : &pLeaf->pCertCtx->pTaInfo->PubKey;

    if (ppCert)
        *ppCert = pLeaf->pCert;

    if (ppCertCtx)
    {
        if (pLeaf->pCertCtx)
        {
            uint32_t cRefs = RTCrCertCtxRetain(pLeaf->pCertCtx);
            AssertReturn(cRefs != UINT32_MAX, VERR_CR_X509_INTERNAL_ERROR);
        }
        *ppCertCtx = pLeaf->pCertCtx;
    }

    if (prcVerify)
        *prcVerify = pLeaf->rcVerify;

    return VINF_SUCCESS;
}


RTDECL(uint32_t) RTCrX509CertPathsGetPathLength(RTCRX509CERTPATHS hCertPaths, uint32_t iPath)
{
    /*
     * Validate the input.
     */
    PRTCRX509CERTPATHSINT pThis = hCertPaths;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTCRX509CERTPATHSINT_MAGIC, UINT32_MAX);
    AssertPtrReturn(pThis->pRoot, UINT32_MAX);
    AssertReturn(iPath < pThis->cPaths, UINT32_MAX);

    /*
     * Get the data.
     */
    PRTCRX509CERTPATHNODE pLeaf = rtCrX509CertPathsGetLeafByIndex(pThis, iPath);
    AssertReturn(pLeaf, UINT32_MAX);
    return pLeaf->uDepth + 1;
}


RTDECL(int) RTCrX509CertPathsGetPathVerifyResult(RTCRX509CERTPATHS hCertPaths, uint32_t iPath)
{
    /*
     * Validate the input.
     */
    PRTCRX509CERTPATHSINT pThis = hCertPaths;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTCRX509CERTPATHSINT_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pThis->pRoot, VERR_WRONG_ORDER);
    AssertReturn(iPath < pThis->cPaths, VERR_NOT_FOUND);

    /*
     * Get the data.
     */
    PRTCRX509CERTPATHNODE pLeaf = rtCrX509CertPathsGetLeafByIndex(pThis, iPath);
    AssertReturn(pLeaf, VERR_CR_X509_INTERNAL_ERROR);

    return pLeaf->rcVerify;
}


static PRTCRX509CERTPATHNODE rtCrX509CertPathsGetPathNodeByIndexes(PRTCRX509CERTPATHSINT pThis, uint32_t iPath, uint32_t iNode)
{
    PRTCRX509CERTPATHNODE pNode = rtCrX509CertPathsGetLeafByIndex(pThis, iPath);
    Assert(pNode);
    if (pNode)
    {
        if (iNode <= pNode->uDepth)
        {
            uint32_t uCertDepth = pNode->uDepth - iNode;
            while (pNode->uDepth > uCertDepth)
                pNode = pNode->pParent;
            Assert(pNode);
            Assert(pNode && pNode->uDepth == uCertDepth);
            return pNode;
        }
    }

    return NULL;
}


RTDECL(PCRTCRX509CERTIFICATE) RTCrX509CertPathsGetPathNodeCert(RTCRX509CERTPATHS hCertPaths, uint32_t iPath, uint32_t iNode)
{
    /*
     * Validate the input.
     */
    PRTCRX509CERTPATHSINT pThis = hCertPaths;
    AssertPtrReturn(pThis, NULL);
    AssertReturn(pThis->u32Magic == RTCRX509CERTPATHSINT_MAGIC, NULL);
    AssertPtrReturn(pThis->pRoot, NULL);
    AssertReturn(iPath < pThis->cPaths, NULL);

    /*
     * Get the data.
     */
    PRTCRX509CERTPATHNODE pNode = rtCrX509CertPathsGetPathNodeByIndexes(pThis, iPath, iNode);
    if (pNode)
        return pNode->pCert;
    return NULL;
}


/** @} */

