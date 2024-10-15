/* $Id: EBML_MKV.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * EbmlMkvIDs.h - Matroska EBML Class IDs.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_EBML_MKV_h
#define MAIN_INCLUDED_EBML_MKV_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/** Matroska EBML Class IDs supported by WebM.
 *
 *  Keep the structure clean and group elements where it makes sense
 *  for easier reading / lookup. */
enum MkvElem
{
    MkvElem_EBML                    = 0x1A45DFA3,
    MkvElem_EBMLVersion             = 0x4286,
    MkvElem_EBMLReadVersion         = 0x42F7,
    MkvElem_EBMLMaxIDLength         = 0x42F2,
    MkvElem_EBMLMaxSizeLength       = 0x42F3,

    MkvElem_DocType                 = 0x4282,
    MkvElem_DocTypeVersion          = 0x4287,
    MkvElem_DocTypeReadVersion      = 0x4285,

    MkvElem_Segment                 = 0x18538067,
    MkvElem_Segment_Duration        = 0x4489,

    MkvElem_SeekHead                = 0x114D9B74,
    MkvElem_Seek                    = 0x4DBB,
    MkvElem_SeekID                  = 0x53AB,
    MkvElem_SeekPosition            = 0x53AC,

    MkvElem_Info                    = 0x1549A966,
    MkvElem_TimecodeScale           = 0x2AD7B1,
    MkvElem_MuxingApp               = 0x4D80,
    MkvElem_WritingApp              = 0x5741,

    MkvElem_Tracks                  = 0x1654AE6B,
    MkvElem_TrackEntry              = 0xAE,
    MkvElem_TrackNumber             = 0xD7,
    MkvElem_TrackUID                = 0x73C5,
    MkvElem_TrackType               = 0x83,

    MkvElem_Language                = 0x22B59C,

    MkvElem_FlagLacing              = 0x9C,

    MkvElem_Cluster                 = 0x1F43B675,
    MkvElem_Timecode                = 0xE7,

    MkvElem_SimpleBlock             = 0xA3,

    MkvElem_SeekPreRoll             = 0x56BB,

    MkvElem_CodecID                 = 0x86,
    MkvElem_CodecDelay              = 0x56AA,
    MkvElem_CodecPrivate            = 0x63A2,
    MkvElem_CodecName               = 0x258688,

    MkvElem_Video                   = 0xE0,
    MkvElem_PixelWidth              = 0xB0,
    MkvElem_PixelHeight             = 0xBA,
    MkvElem_FrameRate               = 0x2383E3,

    MkvElem_Audio                   = 0xE1,
    MkvElem_SamplingFrequency       = 0xB5,
    MkvElem_OutputSamplingFrequency = 0x78B5,
    MkvElem_Channels                = 0x9F,
    MkvElem_BitDepth                = 0x6264,

    MkvElem_Cues                    = 0x1C53BB6B,
    MkvElem_CuePoint                = 0xBB,
    MkvElem_CueTime                 = 0xB3,
    MkvElem_CueTrackPositions       = 0xB7,
    MkvElem_CueTrack                = 0xF7,
    MkvElem_CueClusterPosition      = 0xF1
};

#endif /* !MAIN_INCLUDED_EBML_MKV_h */

