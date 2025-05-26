/* $Id: SelfSizingTable.h 169001 2025-05-26 19:52:08Z klaus.espenlaub@oracle.com $ */
/** @file
 * vboxraw header file
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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

/* SELFSIZINGTABLE
 *
 * An ANSI text-display oriented table, whose column widths conform to width of
 * their contents. The goal is to optimize whitespace usage, so there's neither too
 * much nor too little whitespace (e.g. min. necessary for optimal readability).
 *
 * Contents can only be added to and redisplayed, not manipulated after adding.
 *
 * Simple API (see example below):
 *
 *   1. Create table instance.
 *   2. Add column definitions.
 *   3. Add each row and set data for each column in a row.
 *   4. Invoke the displayTable() method.
 *
 * Each time the table is [re]displayed its contents are [re]evaluated to determine
 * the column sizes and header and data padding.
 *
 * Example:
 *
 *  SELFSIZINGTABLE tbl(2);
 *  void *colPlanet  = tbl.addCol("Planet"          "%s",   1);
 *  void *colInhabit = tbl.addCol("Inhabitability", "%-12s = %s");
 *
 *  // This is an 'unrolled loop' example. More typical would be to iterate,
 *  // providing data content from arrays, indicies, in-place calculations,
 *  // databases, etc... rather than just hardcoded literals.
 *
 *  void *row = tbl.addRow();
 *  tbl.setCell(row, colPlanet,  "Earth");
 *  tbl.setCell(row, colInhabit, "Viability", "Decreasing");
 *  row = tbl.addRow();
 *  tbl.setCell(row, colPlanet,  "Mars");
 *  tbl.setCell(row, colInhabit, "Tolerability", "Miserable");
 *  row = tbl.addRow();
 *  tbl.setCell(row, colPlanet,  "Neptune");
 *  tbl.setCell(row, colInhabit, "Plausibility", "Forget it");
 *
 *  tbl.displayTable();
 *
 *   Planet  Inhabitability
 *    Earth  Viability    = Decreasing
 *     Mars  Tolerability = Miserable
 *  Neptune  Plausibility = Forget it
 *
 *  (note:
 *     Column headers displayed in bold red to distinguish from data)
 *
 */

#ifndef VBOX_INCLUDED_SRC_vboximg_mount_SelfSizingTable_h
#define VBOX_INCLUDED_SRC_vboximg_mount_SelfSizingTable_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/string.h>
#include <iprt/assert.h>
#include <iprt/message.h>
#include <iprt/stream.h>

#define ANSI_BOLD  "\x1b[1m"                        /** ANSI terminal esc. seq [CSI] to switch font to bold */
#define ANSI_BLACK "\x1b[30m"                       /** ANSI terminal esc. seq [CSI] to switch font to black */
#define ANSI_RED   "\x1b[31m"                       /** ANSI terminal esc. seq [CSI] to switch font to red */
#define ANSI_RESET "\x1b[m"                         /** ANSI terminal esc. seq to reset terminal attributes mode */

#define HDRLABEL_MAX                30              /** Maximum column header label length (for RTStrNLen()) */
#define COLUMN_WIDTH_MAX            256             /** Maximum width of a display column */

typedef class SelfSizingTable
{
    public:
        SelfSizingTable(int cbDefaultPadding = 1);
        ~SelfSizingTable();
        void *addCol(const char *pszHdr, const char *pszFmt, int8_t align = LEFT, int8_t padRight = 0);
        void *addRow();
        void setCell(void *row, void *col, ...);
        void displayTable();

   private:
        typedef struct ColDesc {
            struct ColDesc *next;
            char    *pszHdr;
            uint8_t  hdrLen;
            char    *pszFmt;
            int8_t   alignment;
            uint8_t  cbPadRightOpt;
            uint8_t  cbWidestDataInCol;
        } COLDESC;

        typedef struct ColData
        {
            struct ColData *next;
            COLDESC *pColDesc;
            char    *pszData;
            uint8_t  cbData;
        } COLDATA;

        typedef struct Row
        {
            struct Row *next;
            uint32_t id;
            COLDATA colDataListhead;
        } ROW;

        int cbDefaultColPadding;
        COLDESC colDescListhead;
        ROW rowListhead;

    public:
        enum Alignment /* column/cell alignment */
        {
             CENTER = 0, RIGHT = 1, LEFT = -1,
        };

} SELFSIZINGTABLE;

SELFSIZINGTABLE::SelfSizingTable(int cbDefaultPadding)
{
    this->cbDefaultColPadding = cbDefaultPadding;
    colDescListhead.next = NULL;
    rowListhead.next = NULL;
}
SELFSIZINGTABLE::~SelfSizingTable()
{
    COLDESC *pColDesc = colDescListhead.next;
    while (pColDesc)
    {
        COLDESC *pColDescNext = pColDesc->next;
        RTMemFree(pColDesc->pszHdr);
        RTMemFree(pColDesc->pszFmt);
        delete pColDesc;
        pColDesc = pColDescNext;
    }
    ROW *pRow = rowListhead.next;
    while(pRow)
    {
        ROW *pRowNext = pRow->next;
        COLDATA *pColData = pRow->colDataListhead.next;
        while (pColData)
        {
            COLDATA *pColDataNext = pColData->next;
            delete[] pColData->pszData;
            delete pColData;
            pColData = pColDataNext;
        }
        delete pRow;
        pRow = pRowNext;
    }
}

void *SELFSIZINGTABLE::addCol(const char *pszHdr, const char *pszFmt, int8_t align, int8_t padRight)
{
    COLDESC *pColDescNew = new COLDESC();
    if (!pColDescNew)
    {
        RTMsgErrorExitFailure("out of memory");
        return NULL;
    }
    pColDescNew->pszHdr = RTStrDup(pszHdr);
    pColDescNew->hdrLen = RTStrNLen(pszHdr, HDRLABEL_MAX);
    pColDescNew->pszFmt = RTStrDup(pszFmt);
    pColDescNew->alignment = align;
    pColDescNew->cbPadRightOpt = padRight;
    COLDESC *pColDesc = &colDescListhead;

    while (pColDesc->next)
        pColDesc = pColDesc->next;

    pColDesc->next = pColDescNew;
    return (void *)pColDescNew;
}

void *SELFSIZINGTABLE::addRow()
{
    ROW *pNewRow = new Row();
    COLDESC *pColDesc = colDescListhead.next;
    COLDATA *pCurColData = &pNewRow->colDataListhead;
    while (pColDesc)
    {
        COLDATA *pNewColData = new COLDATA();
        pNewColData->pColDesc = pColDesc;
        pCurColData = pCurColData->next = pNewColData;
        pColDesc = pColDesc->next;
    }
    ROW *pRow = &rowListhead;
    while (pRow->next)
        pRow = pRow->next;
    pRow->next = pNewRow;
    return (void *)pNewRow;
}

void SELFSIZINGTABLE::setCell(void *row, void *col, ...)
{
    ROW *pRow = (ROW *)row;
    COLDESC *pColDesc = (COLDESC *)col;
    va_list ap;
    va_start(ap, col);

    char *pszData = new char[COLUMN_WIDTH_MAX];
    int cbData = RTStrPrintfV(pszData, COLUMN_WIDTH_MAX, pColDesc->pszFmt, ap);
    COLDATA *pColData = pRow->colDataListhead.next;
    while (pColData)
    {
        if (pColData->pColDesc == pColDesc)
        {
            pColData->pszData = pszData;
            pColData->cbData = cbData;
            break;
        }
        pColData = pColData->next;
    }

    va_end(ap);
}

void SELFSIZINGTABLE::displayTable()
{
    /* Determine max cell (and column header) length for each column */

    COLDESC *pColDesc = colDescListhead.next;
    while (pColDesc)
    {
        pColDesc->cbWidestDataInCol = pColDesc->hdrLen;
        pColDesc = pColDesc->next;
    }
    ROW *pRow = rowListhead.next;
    while(pRow)
    {
        COLDATA *pColData = pRow->colDataListhead.next;
        while (pColData)
        {
            pColDesc = pColData->pColDesc;
            if (pColData->cbData > pColDesc->cbWidestDataInCol)
                pColDesc->cbWidestDataInCol = pColData->cbData;;
            pColData = pColData->next;
        }
        pRow = pRow->next;
    }

    /* Display col headers based on actual column size w/alignment & padding */
    pColDesc = colDescListhead.next;
    while (pColDesc)
    {
        uint8_t colWidth = pColDesc->cbWidestDataInCol;
        char *pszColHdr = new char[colWidth + 1];
        switch (pColDesc->alignment)
        {
            case RIGHT:
                RTStrPrintf(pszColHdr, colWidth + 1, "%*s", colWidth, pColDesc->pszHdr);
                break;
            case LEFT:
                RTStrPrintf(pszColHdr, colWidth + 1, "%-*s", colWidth, pColDesc->pszHdr);
                break;
            case CENTER:
                int cbPad = (colWidth - pColDesc->hdrLen) / 2;
                RTStrPrintf(pszColHdr, colWidth + 1, "%*s%s%*s", cbPad, "", pColDesc->pszHdr, cbPad, "");
        }
        RTPrintf(ANSI_BOLD ANSI_RED);
        uint8_t cbPad = pColDesc->cbPadRightOpt ? pColDesc->cbPadRightOpt : cbDefaultColPadding;
        RTPrintf("%s%*s", pszColHdr, cbPad, " ");
        RTPrintf(ANSI_RESET);
        pColDesc = pColDesc->next;
        delete[] pszColHdr;
    }
    RTPrintf("\n");
    /*
     * Display each of the column data items for the row
     */
    pRow = rowListhead.next;
    while(pRow)
    {
        COLDATA *pColData = pRow->colDataListhead.next;
        while (pColData)
        {   pColDesc = pColData->pColDesc;
            uint8_t colWidth = pColDesc->cbWidestDataInCol;
            char *aCell = new char[colWidth + 1];
            switch (pColDesc->alignment)
            {
                case RIGHT:
                    RTStrPrintf(aCell, colWidth + 1, "%*s", colWidth, pColData->pszData);
                    break;
                case LEFT:
                    RTStrPrintf(aCell, colWidth + 1, "%-*s", colWidth, pColData->pszData);
                    break;
                case CENTER:
                    int cbPad = (colWidth - pColData->cbData) / 2;
                    RTStrPrintf(aCell, colWidth + 1, "%*s%s%*s", cbPad, "", pColData->pszData, cbPad, "");
            }
            uint8_t cbPad = pColDesc->cbPadRightOpt ? pColDesc->cbPadRightOpt : this->cbDefaultColPadding;
            RTPrintf("%s%*s", aCell, cbPad, " ");
            pColData = pColData->next;
            delete[] aCell;
        }
        RTPrintf("\n");
        pRow = pRow->next;
    }
}
#endif /* !VBOX_INCLUDED_SRC_vboximg_mount_SelfSizingTable_h */
