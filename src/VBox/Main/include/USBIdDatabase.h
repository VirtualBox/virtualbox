/* $Id: USBIdDatabase.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * USB device vendor and product ID database.
 */

/*
 * Copyright (C) 2015-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_USBIdDatabase_h
#define MAIN_INCLUDED_USBIdDatabase_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/assert.h>
#include <iprt/stdint.h>
#include <iprt/cpp/ministring.h>
#include <iprt/bldprog-strtab.h>


/** Saves a few bytes (~25%) on strings.  */
#define USB_ID_DATABASE_WITH_COMPRESSION

/** Max string length. */
#define USB_ID_DATABASE_MAX_STRING      _1K


AssertCompileSize(RTBLDPROGSTRREF, sizeof(uint32_t));


/**
 * Elements of product table.
 */
typedef struct USBIDDBPROD
{
    /** Product ID. */
    uint16_t idProduct;
} USBIDDBPROD;
AssertCompileSize(USBIDDBPROD, sizeof(uint16_t));


/**
 * Element of vendor table.
 */
typedef struct USBIDDBVENDOR
{
    /** Vendor ID. */
    uint16_t idVendor;
    /** Index of the first product. */
    uint16_t iProduct;
    /** Number of products. */
    uint16_t cProducts;
} USBIDDBVENDOR;
AssertCompileSize(USBIDDBVENDOR, sizeof(uint16_t) * 3);


/**
 * Wrapper for static array of Aliases.
 */
class USBIdDatabase
{
public: // For assertions and statis in the generator.
    /** The compressed string table.   */
    static RTBLDPROGSTRTAB const s_StrTab;

    /** Number of vendors in the two parallel arrays.   */
    static const size_t          s_cVendors;
    /** Vendor IDs lookup table. */
    static const USBIDDBVENDOR   s_aVendors[];
    /** Vendor names table running parallel to s_aVendors. */
    static const RTBLDPROGSTRREF s_aVendorNames[];

    /** Number of products in the two parallel arrays. */
    static const size_t          s_cProducts;
    /** Vendor+Product keys for lookup purposes. */
    static const USBIDDBPROD     s_aProducts[];
    /** Product names table running parallel to s_aProducts. */
    static const RTBLDPROGSTRREF s_aProductNames[];

public:
    static RTCString returnString(PCRTBLDPROGSTRREF pStr)
    {
        char szTmp[USB_ID_DATABASE_MAX_STRING * 2];
        ssize_t cchTmp = RTBldProgStrTabQueryString(&s_StrTab, pStr->off, pStr->cch, szTmp, sizeof(szTmp));
        return RTCString(szTmp, (size_t)RT_MAX(cchTmp, 0));
    }

private:
    /**
     * Performs a binary lookup of @a idVendor.
     *
     * @returns The index in the vendor tables, UINT32_MAX if not found.
     * @param   idVendor        The vendor ID.
     */
    static uint32_t lookupVendor(uint16_t idVendor)
    {
        size_t iEnd   = s_cVendors;
        if (iEnd)
        {
            size_t iStart = 0;
            for (;;)
            {
                size_t idx = iStart + (iEnd - iStart) / 2;
                if (s_aVendors[idx].idVendor < idVendor)
                {
                    idx++;
                    if (idx < iEnd)
                        iStart = idx;
                    else
                        break;
                }
                else if (s_aVendors[idx].idVendor > idVendor)
                {
                    if (idx != iStart)
                        iEnd = idx;
                    else
                        break;
                }
                else
                    return (uint32_t)idx;
            }
        }
        return UINT32_MAX;
    }

    /**
     * Performs a binary lookup of @a idProduct.
     *
     * @returns The index in the product tables, UINT32_MAX if not found.
     * @param   idProduct       The product ID.
     * @param   iStart          The index of the first entry for the vendor.
     * @param   iEnd            The index of after the last entry.
     */
    static uint32_t lookupProduct(uint16_t idProduct, size_t iStart, size_t iEnd)
    {
        if (iStart < iEnd)
        {
            for (;;)
            {
                size_t idx = iStart + (iEnd - iStart) / 2;
                if (s_aProducts[idx].idProduct < idProduct)
                {
                    idx++;
                    if (idx < iEnd)
                        iStart = idx;
                    else
                        break;
                }
                else if (s_aProducts[idx].idProduct > idProduct)
                {
                    if (idx != iStart)
                        iEnd = idx;
                    else
                        break;
                }
                else
                    return (uint32_t)idx;
            }
        }
        return UINT32_MAX;
    }


public:
    static RTCString findProduct(uint16_t idVendor, uint16_t idProduct)
    {
        uint32_t idxVendor = lookupVendor(idVendor);
        if (idxVendor != UINT32_MAX)
        {
            uint32_t idxProduct = lookupProduct(idProduct, s_aVendors[idxVendor].iProduct,
                                                s_aVendors[idxVendor].iProduct + s_aVendors[idxVendor].cProducts);
            if (idxProduct != UINT32_MAX)
                return returnString(&s_aProductNames[idxProduct]);
        }
        return RTCString();
    }

    static RTCString findVendor(uint16_t idVendor)
    {
        uint32_t idxVendor = lookupVendor(idVendor);
        if (idxVendor != UINT32_MAX)
            return returnString(&s_aVendorNames[idxVendor]);
        return RTCString();
    }

    static RTCString findVendorAndProduct(uint16_t idVendor, uint16_t idProduct, RTCString *pstrProduct)
    {
        uint32_t idxVendor = lookupVendor(idVendor);
        if (idxVendor != UINT32_MAX)
        {
            uint32_t idxProduct = lookupProduct(idProduct, s_aVendors[idxVendor].iProduct,
                                                s_aVendors[idxVendor].iProduct + s_aVendors[idxVendor].cProducts);
            if (idxProduct != UINT32_MAX)
                *pstrProduct = returnString(&s_aProductNames[idxProduct]);
            else
                pstrProduct->setNull();
            return returnString(&s_aVendorNames[idxVendor]);
        }
        pstrProduct->setNull();
        return RTCString();
    }

};


#endif /* !MAIN_INCLUDED_USBIdDatabase_h */

