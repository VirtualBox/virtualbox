/* $Id: USBIdDatabaseGenerator.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * USB device vendor and product ID database - generator.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <stdio.h>

#include <algorithm>
#include <map>
#include <iprt/sanitized/string>
#include <vector>

#include <VBox/version.h>

#include <iprt/err.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/string.h>
#include <iprt/stream.h>

#include "../include/USBIdDatabase.h"


/*
 * Include the string table generator.
 */
#define BLDPROG_STRTAB_MAX_STRLEN  (USB_ID_DATABASE_MAX_STRING - 1)
#ifdef USB_ID_DATABASE_WITH_COMPRESSION
# define BLDPROG_STRTAB_WITH_COMPRESSION
#else
# undef  BLDPROG_STRTAB_WITH_COMPRESSION
#endif
#define BLDPROG_STRTAB_WITH_CAMEL_WORDS
#undef  BLDPROG_STRTAB_PURE_ASCII
#include <iprt/bldprog-strtab-template.cpp.h>



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** For verbose output.   */
static bool g_fVerbose = false;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
// error codes (complements RTEXITCODE_XXX).
#define ERROR_OPEN_FILE         (12)
#define ERROR_IN_PARSE_LINE     (13)
#define ERROR_DUPLICATE_ENTRY   (14)
#define ERROR_WRONG_FILE_FORMAT (15)
#define ERROR_TOO_MANY_PRODUCTS (16)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
struct VendorRecord
{
    size_t vendorID;
    size_t iProduct;
    size_t cProducts;
    std::string str;
    BLDPROGSTRING StrRef;
};

struct ProductRecord
{
    size_t key;
    size_t vendorID;
    size_t productID;
    std::string str;
    BLDPROGSTRING StrRef;
};

typedef std::vector<ProductRecord> ProductsSet;
typedef std::vector<VendorRecord>  VendorsSet;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static ProductsSet g_products;
static VendorsSet  g_vendors;

/** The size of all the raw strings, including terminators. */
static size_t g_cbRawStrings = 0;



bool operator < (const ProductRecord& lh, const ProductRecord& rh)
{
    return lh.key < rh.key;
}

bool operator < (const VendorRecord& lh, const VendorRecord& rh)
{
    return lh.vendorID < rh.vendorID;
}

bool operator == (const ProductRecord& lh, const ProductRecord& rh)
{
    return lh.key == rh.key;
}

bool operator == (const VendorRecord& lh, const VendorRecord& rh)
{
    return lh.vendorID == rh.vendorID;
}


/*
 * Input file parsing.
 */
static int ParseAlias(char *pszLine, size_t& id, std::string& desc)
{
    /* First there's a hexadeciman number. */
    uint32_t uVal;
    char *pszNext;
    int vrc = RTStrToUInt32Ex(pszLine, &pszNext, 16, &uVal);
    if (   vrc == VWRN_TRAILING_CHARS
        || vrc == VWRN_TRAILING_SPACES
        || vrc == VINF_SUCCESS)
    {
        /* Skip the whipespace following it and at the end of the line. */
        pszNext = RTStrStripL(pszNext);
        if (*pszNext != '\0')
        {
            vrc = RTStrValidateEncoding(pszNext);
            if (RT_SUCCESS(vrc))
            {
                size_t cchDesc = strlen(pszNext);
                if (cchDesc <= USB_ID_DATABASE_MAX_STRING)
                {
                    id   = uVal;
                    desc = pszNext;
                    g_cbRawStrings += cchDesc + 1;
                    return RTEXITCODE_SUCCESS;
                }
                RTMsgError("String to long: %zu", cchDesc);
            }
            else
                RTMsgError("Invalid encoding: '%s' (vrc=%Rrc)", pszNext, vrc);
        }
        else
            RTMsgError("Error parsing '%s'", pszLine);
    }
    else
        RTMsgError("Error converting number at the start of '%s': %Rrc", pszLine, vrc);
    return ERROR_IN_PARSE_LINE;
}

static int ParseUsbIds(PRTSTREAM pInStrm, const char *pszFile)
{
    /*
     * State data.
     */
    VendorRecord vendor = { 0, 0, 0, "" };

    /*
     * Process the file line-by-line.
     *
     * The generic format is that we have top level entries (vendors) starting
     * in position 0 with sub entries starting after one or more, depending on
     * the level, tab characters.
     *
     * Specifically, the list of vendors and their products will always start
     * with a vendor line followed by indented products.  The first character
     * on the vendor line is a hex digit (four in total) that makes up the
     * vendor ID.  The product lines equally starts with a 4 digit hex ID value.
     *
     * Other lists are assumed to have first lines that doesn't start with any
     * lower case hex digit.
     */
    uint32_t iLine = 0;;
    for (;;)
    {
        char szLine[_4K];
        int vrc = RTStrmGetLine(pInStrm, szLine, sizeof(szLine));
        if (RT_SUCCESS(vrc))
        {
            iLine++;

            /* Check for vendor line. */
            char chType = szLine[0];
            if (   RT_C_IS_XDIGIT(chType)
                && RT_C_IS_SPACE(szLine[4])
                && RT_C_IS_XDIGIT(szLine[1])
                && RT_C_IS_XDIGIT(szLine[2])
                && RT_C_IS_XDIGIT(szLine[3]) )
            {
                if (ParseAlias(szLine, vendor.vendorID, vendor.str) == 0)
                    g_vendors.push_back(vendor);
                else
                    return RTMsgErrorExit((RTEXITCODE)ERROR_IN_PARSE_LINE,
                                          "%s(%d): Error in parsing vendor line: '%s'", pszFile, iLine, szLine);
            }
            /* Check for product line. */
            else if (szLine[0] == '\t' && vendor.vendorID != 0)
            {
                ProductRecord product = { 0, vendor.vendorID, 0, "" };
                if (ParseAlias(&szLine[1], product.productID, product.str) == 0)
                {
                    product.key = RT_MAKE_U32(product.productID, product.vendorID);
                    Assert(product.vendorID == vendor.vendorID);
                    g_products.push_back(product);
                }
                else
                    return RTMsgErrorExit((RTEXITCODE)ERROR_IN_PARSE_LINE, "Error in parsing product line: '%s'", szLine);
            }
            /* If not a blank or comment line, it is some other kind of data.
               So, make sure the vendor ID is cleared so we don't try process
               the sub-items of in some other list as products. */
            else if (   chType != '#'
                     && chType != '\0'
                     && *RTStrStripL(szLine) != '\0')
                vendor.vendorID = 0;
        }
        else if (vrc == VERR_EOF)
            return RTEXITCODE_SUCCESS;
        else
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTStrmGetLine failed: %Rrc", vrc);
    }
}

static void WriteSourceFile(FILE *pOut, const char *argv0, PBLDPROGSTRTAB pStrTab)
{
    fprintf(pOut,
            "/** @file\n"
            " * USB device vendor and product ID database - Autogenerated by %s\n"
            " */\n"
            "\n"
            "/*\n"
            " * Copyright (C) 2015-" VBOX_C_YEAR " Oracle and/or its affiliates.\n"
            " *\n"
            " * This file is part of VirtualBox base platform packages, as\n"
            " * available from https://www.virtualbox.org.\n"
            " *\n"
            " * This program is free software; you can redistribute it and/or\n"
            " * modify it under the terms of the GNU General Public License\n"
            " * as published by the Free Software Foundation, in version 3 of the\n"
            " * License.\n"
            " *\n"
            " * This program is distributed in the hope that it will be useful, but\n"
            " * WITHOUT ANY WARRANTY; without even the implied warranty of\n"
            " * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU\n"
            " * General Public License for more details.\n"
            " *\n"
            " * You should have received a copy of the GNU General Public License\n"
            " * along with this program; if not, see <https://www.gnu.org/licenses>.\n"
            " *\n"
            " * SPDX-License-Identifier: GPL-3.0-only\n"
            " */"
            "\n"
            "\n"
            "#include \"USBIdDatabase.h\"\n"
            "\n",
            argv0);

    BldProgStrTab_WriteStringTable(pStrTab, pOut, "", "USBIdDatabase::s_", "StrTab");

    fputs("/**\n"
          " * USB devices aliases array.\n"
          " * Format: VendorId, ProductId, Vendor Name, Product Name\n"
          " * The source of the list is http://www.linux-usb.org/usb.ids\n"
          " */\n"
          "USBIDDBPROD const USBIdDatabase::s_aProducts[] =\n"
          "{\n", pOut);
    for (ProductsSet::iterator itp = g_products.begin(); itp != g_products.end(); ++itp)
        fprintf(pOut, "    { 0x%04x },\n", (unsigned)itp->productID);
    fputs("};\n"
          "\n"
          "\n"
          "const RTBLDPROGSTRREF USBIdDatabase::s_aProductNames[] =\n"
          "{\n", pOut);
    for (ProductsSet::iterator itp = g_products.begin(); itp != g_products.end(); ++itp)
        fprintf(pOut, "{ 0x%06x, 0x%02x },\n", itp->StrRef.offStrTab, (unsigned)itp->StrRef.cchString);
    fputs("};\n"
          "\n"
          "const size_t USBIdDatabase::s_cProducts = RT_ELEMENTS(USBIdDatabase::s_aProducts);\n"
          "\n", pOut);

    fputs("USBIDDBVENDOR const USBIdDatabase::s_aVendors[] =\n"
          "{\n", pOut);
    for (VendorsSet::iterator itv = g_vendors.begin(); itv != g_vendors.end(); ++itv)
        fprintf(pOut, "    { 0x%04x, 0x%04x, 0x%04x },\n", (unsigned)itv->vendorID, (unsigned)itv->iProduct, (unsigned)itv->cProducts);
    fputs("};\n"
          "\n"
          "\n"
          "const RTBLDPROGSTRREF USBIdDatabase::s_aVendorNames[] =\n"
          "{\n", pOut);
    for (VendorsSet::iterator itv = g_vendors.begin(); itv != g_vendors.end(); ++itv)
        fprintf(pOut, "{ 0x%06x, 0x%02x },\n", itv->StrRef.offStrTab, (unsigned)itv->StrRef.cchString);
    fputs("};\n"
          "\n"
          "const size_t USBIdDatabase::s_cVendors = RT_ELEMENTS(USBIdDatabase::s_aVendors);\n"
          "\n", pOut);
}

static int usage(FILE *pOut, const char *argv0)
{
    fprintf(pOut, "Usage: %s [linux.org usb list file] [custom usb list file] [-o output file]\n", argv0);
    return RTEXITCODE_SYNTAX;
}


int main(int argc, char *argv[])
{
    /*
     * Initialize IPRT and convert argv to UTF-8.
     */
    int vrc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(vrc))
        return RTMsgInitFailure(vrc);

    /*
     * Parse arguments and read input files.
     */
    if (argc < 4)
    {
        usage(stderr, argv[0]);
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Insufficient arguments.");
    }
    g_products.reserve(20000);
    g_vendors.reserve(3500);

    const char *pszOutFile = NULL;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-o") == 0)
        {
            pszOutFile = argv[++i];
            continue;
        }
        if (   strcmp(argv[i], "-h") == 0
            || strcmp(argv[i], "-?") == 0
            || strcmp(argv[i], "--help") == 0)
        {
            usage(stdout, argv[0]);
            return RTEXITCODE_SUCCESS;
        }

        PRTSTREAM pInStrm;
        vrc = RTStrmOpen(argv[i], "r", &pInStrm);
        if (RT_FAILURE(vrc))
            return RTMsgErrorExit((RTEXITCODE)ERROR_OPEN_FILE,
                                  "Failed to open file '%s' for reading: %Rrc", argv[i], vrc);

        vrc = ParseUsbIds(pInStrm, argv[i]);
        RTStrmClose(pInStrm);
        if (vrc != 0)
        {
            RTMsgError("Failed parsing USB devices file '%s'", argv[i]);
            return vrc;
        }
    }

    /*
     * Due to USBIDDBVENDOR::iProduct, there is currently a max of 64KB products.
     * (Not a problem as we've only have less that 54K products currently.)
     */
    if (g_products.size() > _64K)
        return RTMsgErrorExit((RTEXITCODE)ERROR_TOO_MANY_PRODUCTS,
                              "More than 64K products is not supported: %u products", g_products.size());

    /*
     * Sort the IDs and fill in the iProduct and cProduct members.
     */
    sort(g_products.begin(), g_products.end());
    sort(g_vendors.begin(), g_vendors.end());

    size_t iProduct = 0;
    for (size_t iVendor = 0; iVendor < g_vendors.size(); iVendor++)
    {
        size_t const idVendor = g_vendors[iVendor].vendorID;
        g_vendors[iVendor].iProduct = iProduct;
        if (   iProduct < g_products.size()
            && g_products[iProduct].vendorID <= idVendor)
        {
            if (g_products[iProduct].vendorID == idVendor)
                do
                    iProduct++;
                while (   iProduct < g_products.size()
                       && g_products[iProduct].vendorID == idVendor);
            else
                return RTMsgErrorExit((RTEXITCODE)ERROR_IN_PARSE_LINE, "product without vendor after sorting. impossible!");
        }
        g_vendors[iVendor].cProducts = iProduct - g_vendors[iVendor].iProduct;
    }

    /*
     * Verify that all IDs are unique.
     */
    ProductsSet::iterator ita = adjacent_find(g_products.begin(), g_products.end());
    if (ita != g_products.end())
        return RTMsgErrorExit((RTEXITCODE)ERROR_DUPLICATE_ENTRY, "Duplicate alias detected: idProduct=%#06x", ita->productID);

    /*
     * Build the string table.
     * Do string compression and create the string table.
     */
    BLDPROGSTRTAB StrTab;
    if (!BldProgStrTab_Init(&StrTab, g_products.size() + g_vendors.size()))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Out of memory!");

    for (ProductsSet::iterator it = g_products.begin(); it != g_products.end(); ++it)
    {
        it->StrRef.pszString = (char *)it->str.c_str();
        BldProgStrTab_AddString(&StrTab, &it->StrRef);
    }
    for (VendorsSet::iterator it = g_vendors.begin(); it != g_vendors.end(); ++it)
    {
        it->StrRef.pszString = (char *)it->str.c_str();
        BldProgStrTab_AddString(&StrTab, &it->StrRef);
    }

    if (!BldProgStrTab_CompileIt(&StrTab, g_fVerbose))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "BldProgStrTab_CompileIt failed!\n");

    /*
     * Print stats.  Making a little extra effort to get it all on one line.
     */
    size_t const cbVendorEntry  = sizeof(USBIdDatabase::s_aVendors[0]) + sizeof(USBIdDatabase::s_aVendorNames[0]);
    size_t const cbProductEntry = sizeof(USBIdDatabase::s_aProducts[0]) + sizeof(USBIdDatabase::s_aProductNames[0]);

    size_t cbOldRaw = (g_products.size() + g_vendors.size()) * sizeof(const char *) * 2 + g_cbRawStrings;
    size_t cbRaw    = g_vendors.size() * cbVendorEntry + g_products.size() * cbProductEntry + g_cbRawStrings;
    size_t cbActual = g_vendors.size() * cbVendorEntry + g_products.size() * cbProductEntry + StrTab.cchStrTab;
#ifdef USB_ID_DATABASE_WITH_COMPRESSION
    cbActual += sizeof(StrTab.aCompDict);
#endif

    char szMsg1[32];
    RTStrPrintf(szMsg1, sizeof(szMsg1),"Total %zu bytes", cbActual);
    char szMsg2[64];
    RTStrPrintf(szMsg2, sizeof(szMsg2)," old version %zu bytes + relocs (%zu%% save)",
                cbOldRaw, (cbOldRaw - cbActual) * 100 / cbOldRaw);
    if (cbActual < cbRaw)
        RTMsgInfo("%s - saving %zu%% (%zu bytes);%s", szMsg1, (cbRaw - cbActual) * 100 / cbRaw, cbRaw - cbActual, szMsg2);
    else
        RTMsgInfo("%s - wasting %zu bytes;%s", szMsg1, cbActual - cbRaw, szMsg2);

    /*
     * Produce the source file.
     */
    if (!pszOutFile)
        return RTMsgErrorExit((RTEXITCODE)ERROR_OPEN_FILE, "Output file is not specified.");

    FILE *pOut = fopen(pszOutFile, "w");
    if (!pOut)
        return RTMsgErrorExit((RTEXITCODE)ERROR_OPEN_FILE, "Error opening '%s' for writing", pszOutFile);

    WriteSourceFile(pOut, argv[0], &StrTab);

    if (ferror(pOut))
        return RTMsgErrorExit((RTEXITCODE)ERROR_OPEN_FILE, "Error writing '%s'!", pszOutFile);
    if (fclose(pOut) != 0)
        return RTMsgErrorExit((RTEXITCODE)ERROR_OPEN_FILE, "Error closing '%s'!", pszOutFile);

    return RTEXITCODE_SUCCESS;
}

