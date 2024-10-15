/* $Id: timezoneinfo.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Time zone mapping info.
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
#define LOG_GROUP RTLOGGROUP_TIME
#include <iprt/time.h>
#include "internal/iprt.h"

#include <iprt/string.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Static time zone mapping info.  Sorted by pszUnixName.
 */
static const RTTIMEZONEINFO g_aTimeZones[] =
{
    { "Africa/Abidjan",                   "Greenwich Standard Time",         14, 23, "CI", "CI", 0x0000005a, 0,                        }, /* 0x000 */
    { "Africa/Accra",                     "Greenwich Standard Time",         12, 23, "GH", "GH", 0x0000005a, 0,                        }, /* 0x001 */
    { "Africa/Addis_Ababa",               "E. Africa Standard Time",         18, 23, "KE", "ET", 0x00000000, 0,                        }, /* 0x002 */
    { "Africa/Algiers",                   "W. Central Africa Standard Time", 14, 31, "DZ", "DZ", 0x00000071, 0,                        }, /* 0x003 */
    { "Africa/Asmara",                    NULL,                              13, 0,  "KE", "",   0x00000000, 0,                        }, /* 0x004 */
    { "Africa/Asmera",                    "E. Africa Standard Time",         13, 23, "KE", "ER", 0x00000000, 0,                        }, /* 0x005 */
    { "Africa/Bamako",                    "Greenwich Standard Time",         13, 23, "CI", "ML", 0x00000000, 0,                        }, /* 0x006 */
    { "Africa/Bangui",                    "W. Central Africa Standard Time", 13, 31, "NG", "CF", 0x00000000, 0,                        }, /* 0x007 */
    { "Africa/Banjul",                    "Greenwich Standard Time",         13, 23, "CI", "GM", 0x00000000, 0,                        }, /* 0x008 */
    { "Africa/Bissau",                    "Greenwich Standard Time",         13, 23, "GW", "GW", 0x0000005a, 0,                        }, /* 0x009 */
    { "Africa/Blantyre",                  "South Africa Standard Time",      15, 26, "MZ", "MW", 0x00000000, 0,                        }, /* 0x00a */
    { "Africa/Brazzaville",               "W. Central Africa Standard Time", 18, 31, "NG", "CG", 0x00000000, 0,                        }, /* 0x00b */
    { "Africa/Bujumbura",                 "South Africa Standard Time",      16, 26, "MZ", "BI", 0x00000000, 0,                        }, /* 0x00c */
    { "Africa/Cairo",                     "Egypt Standard Time",             12, 19, "EG", "EG", 0x00000078, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x00d */
    { "Africa/Casablanca",                "Morocco Standard Time",           17, 21, "MA", "MA", 0x8000004d, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x00e */
    { "Africa/Ceuta",                     "Romance Standard Time",           12, 21, "ES", "ES", 0x00000069, 0,                        }, /* 0x00f */
    { "Africa/Conakry",                   "Greenwich Standard Time",         14, 23, "CI", "GN", 0x00000000, 0,                        }, /* 0x010 */
    { "Africa/Dakar",                     "Greenwich Standard Time",         12, 23, "CI", "SN", 0x00000000, 0,                        }, /* 0x011 */
    { "Africa/Dar_es_Salaam",             "E. Africa Standard Time",         20, 23, "KE", "TZ", 0x00000000, 0,                        }, /* 0x012 */
    { "Africa/Djibouti",                  "E. Africa Standard Time",         15, 23, "KE", "DJ", 0x00000000, 0,                        }, /* 0x013 */
    { "Africa/Douala",                    "W. Central Africa Standard Time", 13, 31, "NG", "CM", 0x00000000, 0,                        }, /* 0x014 */
    { "Africa/El_Aaiun",                  "Morocco Standard Time",           15, 21, "EH", "EH", 0x8000004d, 0,                        }, /* 0x015 */
    { "Africa/Freetown",                  "Greenwich Standard Time",         15, 23, "CI", "SL", 0x00000000, 0,                        }, /* 0x016 */
    { "Africa/Gaborone",                  "South Africa Standard Time",      15, 26, "MZ", "BW", 0x00000000, 0,                        }, /* 0x017 */
    { "Africa/Harare",                    "South Africa Standard Time",      13, 26, "MZ", "ZW", 0x00000000, 0,                        }, /* 0x018 */
    { "Africa/Johannesburg",              "South Africa Standard Time",      19, 26, "ZA", "ZA", 0x0000008c, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x019 */
    { "Africa/Juba",                      "E. Africa Standard Time",         11, 23, "SD", "SS", 0x00000000, 0,                        }, /* 0x01a */
    { "Africa/Kampala",                   "E. Africa Standard Time",         14, 23, "KE", "UG", 0x00000000, 0,                        }, /* 0x01b */
    { "Africa/Khartoum",                  "E. Africa Standard Time",         15, 23, "SD", "SD", 0x0000009b, 0,                        }, /* 0x01c */
    { "Africa/Kigali",                    "South Africa Standard Time",      13, 26, "MZ", "RW", 0x00000000, 0,                        }, /* 0x01d */
    { "Africa/Kinshasa",                  "W. Central Africa Standard Time", 15, 31, "NG", "CD", 0x00000000, 0,                        }, /* 0x01e */
    { "Africa/Lagos",                     "W. Central Africa Standard Time", 12, 31, "NG", "NG", 0x00000071, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x01f */
    { "Africa/Libreville",                "W. Central Africa Standard Time", 17, 31, "NG", "GA", 0x00000000, 0,                        }, /* 0x020 */
    { "Africa/Lome",                      "Greenwich Standard Time",         11, 23, "CI", "TG", 0x00000000, 0,                        }, /* 0x021 */
    { "Africa/Luanda",                    "W. Central Africa Standard Time", 13, 31, "NG", "AO", 0x00000000, 0,                        }, /* 0x022 */
    { "Africa/Lubumbashi",                "South Africa Standard Time",      17, 26, "MZ", "CD", 0x00000000, 0,                        }, /* 0x023 */
    { "Africa/Lusaka",                    "South Africa Standard Time",      13, 26, "MZ", "ZM", 0x00000000, 0,                        }, /* 0x024 */
    { "Africa/Malabo",                    "W. Central Africa Standard Time", 13, 31, "NG", "GQ", 0x00000000, 0,                        }, /* 0x025 */
    { "Africa/Maputo",                    "South Africa Standard Time",      13, 26, "MZ", "MZ", 0x0000008c, 0,                        }, /* 0x026 */
    { "Africa/Maseru",                    "South Africa Standard Time",      13, 26, "ZA", "LS", 0x00000000, 0,                        }, /* 0x027 */
    { "Africa/Mbabane",                   "South Africa Standard Time",      14, 26, "ZA", "SZ", 0x00000000, 0,                        }, /* 0x028 */
    { "Africa/Mogadishu",                 "E. Africa Standard Time",         16, 23, "KE", "SO", 0x00000000, 0,                        }, /* 0x029 */
    { "Africa/Monrovia",                  "Greenwich Standard Time",         15, 23, "LR", "LR", 0x0000005a, 0,                        }, /* 0x02a */
    { "Africa/Nairobi",                   "E. Africa Standard Time",         14, 23, "KE", "KE", 0x0000009b, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x02b */
    { "Africa/Ndjamena",                  "W. Central Africa Standard Time", 15, 31, "TD", "TD", 0x00000071, 0,                        }, /* 0x02c */
    { "Africa/Niamey",                    "W. Central Africa Standard Time", 13, 31, "NG", "NE", 0x00000000, 0,                        }, /* 0x02d */
    { "Africa/Nouakchott",                "Greenwich Standard Time",         17, 23, "CI", "MR", 0x00000000, 0,                        }, /* 0x02e */
    { "Africa/Ouagadougou",               "Greenwich Standard Time",         18, 23, "CI", "BF", 0x00000000, 0,                        }, /* 0x02f */
    { "Africa/Porto-Novo",                "W. Central Africa Standard Time", 17, 31, "NG", "BJ", 0x00000000, 0,                        }, /* 0x030 */
    { "Africa/Sao_Tome",                  "Greenwich Standard Time",         15, 23, "CI", "ST", 0x00000000, 0,                        }, /* 0x031 */
    { "Africa/Timbuktu",                  NULL,                              15, 0,  "CI", "",   0x00000000, 0,                        }, /* 0x032 */
    { "Africa/Tripoli",                   "Libya Standard Time",             14, 19, "LY", "LY", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x033 */
    { "Africa/Tunis",                     "W. Central Africa Standard Time", 12, 31, "TN", "TN", 0x00000071, 0,                        }, /* 0x034 */
    { "Africa/Windhoek",                  "Namibia Standard Time",           15, 21, "NA", "NA", 0x80000046, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x035 */
    { "America/Adak",                     "Aleutian Standard Time",          12, 22, "US", "US", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x036 */
    { "America/Anchorage",                "Alaskan Standard Time",           17, 21, "US", "US", 0x00000003, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x037 */
    { "America/Anguilla",                 "SA Western Standard Time",        16, 24, "TT", "AI", 0x00000000, 0,                        }, /* 0x038 */
    { "America/Antigua",                  "SA Western Standard Time",        15, 24, "TT", "AG", 0x00000000, 0,                        }, /* 0x039 */
    { "America/Araguaina",                "Tocantins Standard Time",         17, 23, "BR", "BR", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x03a */
    { "America/Argentina/Buenos_Aires",   NULL,                              30, 0,  "AR", "",   0x00000000, 0,                        }, /* 0x03b */
    { "America/Argentina/Catamarca",      NULL,                              27, 0,  "AR", "",   0x00000000, 0,                        }, /* 0x03c */
    { "America/Argentina/ComodRivadavia", NULL,                              32, 0,  "AR", "",   0x00000000, 0,                        }, /* 0x03d */
    { "America/Argentina/Cordoba",        NULL,                              25, 0,  "AR", "",   0x00000000, 0,                        }, /* 0x03e */
    { "America/Argentina/Jujuy",          NULL,                              23, 0,  "AR", "",   0x00000000, 0,                        }, /* 0x03f */
    { "America/Argentina/La_Rioja",       "Argentina Standard Time",         26, 23, "AR", "AR", 0x8000004c, 0,                        }, /* 0x040 */
    { "America/Argentina/Mendoza",        NULL,                              25, 0,  "AR", "",   0x00000000, 0,                        }, /* 0x041 */
    { "America/Argentina/Rio_Gallegos",   "Argentina Standard Time",         30, 23, "AR", "AR", 0x8000004c, 0,                        }, /* 0x042 */
    { "America/Argentina/Salta",          "Argentina Standard Time",         23, 23, "AR", "AR", 0x8000004c, 0,                        }, /* 0x043 */
    { "America/Argentina/San_Juan",       "Argentina Standard Time",         26, 23, "AR", "AR", 0x8000004c, 0,                        }, /* 0x044 */
    { "America/Argentina/San_Luis",       "Argentina Standard Time",         26, 23, "AR", "AR", 0x8000004c, 0,                        }, /* 0x045 */
    { "America/Argentina/Tucuman",        "Argentina Standard Time",         25, 23, "AR", "AR", 0x8000004c, 0,                        }, /* 0x046 */
    { "America/Argentina/Ushuaia",        "Argentina Standard Time",         25, 23, "AR", "AR", 0x8000004c, 0,                        }, /* 0x047 */
    { "America/Aruba",                    "SA Western Standard Time",        13, 24, "CW", "AW", 0x00000000, 0,                        }, /* 0x048 */
    { "America/Asuncion",                 "Paraguay Standard Time",          16, 22, "PY", "PY", 0x80000051, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x049 */
    { "America/Atikokan",                 NULL,                              16, 0,  "CA", "",   0x00000000, 0,                        }, /* 0x04a */
    { "America/Atka",                     NULL,                              12, 0,  "US", "",   0x00000000, 0,                        }, /* 0x04b */
    { "America/Bahia",                    "Bahia Standard Time",             13, 19, "BR", "BR", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x04c */
    { "America/Bahia_Banderas",           "Central Standard Time (Mexico)",  22, 30, "MX", "MX", 0x80000043, 0,                        }, /* 0x04d */
    { "America/Barbados",                 "SA Western Standard Time",        16, 24, "BB", "BB", 0x00000037, 0,                        }, /* 0x04e */
    { "America/Belem",                    "SA Eastern Standard Time",        13, 24, "BR", "BR", 0x00000046, 0,                        }, /* 0x04f */
    { "America/Belize",                   "Central America Standard Time",   14, 29, "BZ", "BZ", 0x00000021, 0,                        }, /* 0x050 */
    { "America/Blanc-Sablon",             "SA Western Standard Time",        20, 24, "CA", "CA", 0x00000037, 0,                        }, /* 0x051 */
    { "America/Boa_Vista",                "SA Western Standard Time",        17, 24, "BR", "BR", 0x00000037, 0,                        }, /* 0x052 */
    { "America/Bogota",                   "SA Pacific Standard Time",        14, 24, "CO", "CO", 0x0000002d, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x053 */
    { "America/Boise",                    "Mountain Standard Time",          13, 22, "US", "US", 0x0000000a, 0,                        }, /* 0x054 */
    { "America/Buenos_Aires",             "Argentina Standard Time",         20, 23, "AR", "AR", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x055 */
    { "America/Cambridge_Bay",            "Mountain Standard Time",          21, 22, "CA", "CA", 0x0000000a, 0,                        }, /* 0x056 */
    { "America/Campo_Grande",             "Central Brazilian Standard Time", 20, 31, "BR", "BR", 0x80000048, 0,                        }, /* 0x057 */
    { "America/Cancun",                   "Eastern Standard Time (Mexico)",  14, 30, "MX", "MX", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x058 */
    { "America/Caracas",                  "Venezuela Standard Time",         15, 23, "VE", "VE", 0x8000004b, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x059 */
    { "America/Catamarca",                "Argentina Standard Time",         17, 23, "AR", "AR", 0x00000000, 0,                        }, /* 0x05a */
    { "America/Cayenne",                  "SA Eastern Standard Time",        15, 24, "GF", "GF", 0x00000046, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x05b */
    { "America/Cayman",                   "SA Pacific Standard Time",        14, 24, "PA", "KY", 0x00000000, 0,                        }, /* 0x05c */
    { "America/Chicago",                  "Central Standard Time",           15, 21, "US", "US", 0x00000014, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x05d */
    { "America/Chihuahua",                "Mountain Standard Time (Mexico)", 17, 31, "MX", "MX", 0x80000044, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x05e */
    { "America/Coral_Harbour",            "SA Pacific Standard Time",        21, 24, "CA", "CA", 0x00000000, 0,                        }, /* 0x05f */
    { "America/Cordoba",                  "Argentina Standard Time",         15, 23, "AR", "AR", 0x00000000, 0,                        }, /* 0x060 */
    { "America/Costa_Rica",               "Central America Standard Time",   18, 29, "CR", "CR", 0x00000021, 0,                        }, /* 0x061 */
    { "America/Creston",                  "US Mountain Standard Time",       15, 25, "CA", "CA", 0x0000000f, 0,                        }, /* 0x062 */
    { "America/Cuiaba",                   "Central Brazilian Standard Time", 14, 31, "BR", "BR", 0x80000048, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x063 */
    { "America/Curacao",                  "SA Western Standard Time",        15, 24, "CW", "CW", 0x00000037, 0,                        }, /* 0x064 */
    { "America/Danmarkshavn",             "UTC",                             20, 3,  "GL", "GL", 0x80000050, 0,                        }, /* 0x065 */
    { "America/Dawson",                   "Pacific Standard Time",           14, 21, "CA", "CA", 0x00000004, 0,                        }, /* 0x066 */
    { "America/Dawson_Creek",             "US Mountain Standard Time",       20, 25, "CA", "CA", 0x0000000f, 0,                        }, /* 0x067 */
    { "America/Denver",                   "Mountain Standard Time",          14, 22, "US", "US", 0x0000000a, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x068 */
    { "America/Detroit",                  "Eastern Standard Time",           15, 21, "US", "US", 0x00000023, 0,                        }, /* 0x069 */
    { "America/Dominica",                 "SA Western Standard Time",        16, 24, "TT", "DM", 0x00000000, 0,                        }, /* 0x06a */
    { "America/Edmonton",                 "Mountain Standard Time",          16, 22, "CA", "CA", 0x0000000a, 0,                        }, /* 0x06b */
    { "America/Eirunepe",                 "SA Pacific Standard Time",        16, 24, "BR", "BR", 0x0000002d, 0,                        }, /* 0x06c */
    { "America/El_Salvador",              "Central America Standard Time",   19, 29, "SV", "SV", 0x00000021, 0,                        }, /* 0x06d */
    { "America/Ensenada",                 NULL,                              16, 0,  "MX", "",   0x00000000, 0,                        }, /* 0x06e */
    { "America/Fort_Nelson",              "US Mountain Standard Time",       19, 25, "CA", "CA", 0x0000000f, 0,                        }, /* 0x06f */
    { "America/Fort_Wayne",               NULL,                              18, 0,  "US", "",   0x00000000, 0,                        }, /* 0x070 */
    { "America/Fortaleza",                "SA Eastern Standard Time",        17, 24, "BR", "BR", 0x00000046, 0,                        }, /* 0x071 */
    { "America/Glace_Bay",                "Atlantic Standard Time",          17, 22, "CA", "CA", 0x00000032, 0,                        }, /* 0x072 */
    { "America/Godthab",                  "Greenland Standard Time",         15, 23, "GL", "GL", 0x00000049, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x073 */
    { "America/Goose_Bay",                "Atlantic Standard Time",          17, 22, "CA", "CA", 0x00000032, 0,                        }, /* 0x074 */
    { "America/Grand_Turk",               "Turks And Caicos Standard Time",  18, 30, "TC", "TC", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x075 */
    { "America/Grenada",                  "SA Western Standard Time",        15, 24, "TT", "GD", 0x00000000, 0,                        }, /* 0x076 */
    { "America/Guadeloupe",               "SA Western Standard Time",        18, 24, "TT", "GP", 0x00000000, 0,                        }, /* 0x077 */
    { "America/Guatemala",                "Central America Standard Time",   17, 29, "GT", "GT", 0x00000021, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x078 */
    { "America/Guayaquil",                "SA Pacific Standard Time",        17, 24, "EC", "EC", 0x0000002d, 0,                        }, /* 0x079 */
    { "America/Guyana",                   "SA Western Standard Time",        14, 24, "GY", "GY", 0x00000037, 0,                        }, /* 0x07a */
    { "America/Halifax",                  "Atlantic Standard Time",          15, 22, "CA", "CA", 0x00000032, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x07b */
    { "America/Havana",                   "Cuba Standard Time",              14, 18, "CU", "CU", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x07c */
    { "America/Hermosillo",               "US Mountain Standard Time",       18, 25, "MX", "MX", 0x0000000f, 0,                        }, /* 0x07d */
    { "America/Indiana/Indianapolis",     NULL,                              28, 0,  "US", "",   0x00000000, 0,                        }, /* 0x07e */
    { "America/Indiana/Knox",             "Central Standard Time",           20, 21, "US", "US", 0x00000014, 0,                        }, /* 0x07f */
    { "America/Indiana/Marengo",          "US Eastern Standard Time",        23, 24, "US", "US", 0x00000028, 0,                        }, /* 0x080 */
    { "America/Indiana/Petersburg",       "Eastern Standard Time",           26, 21, "US", "US", 0x00000023, 0,                        }, /* 0x081 */
    { "America/Indiana/Tell_City",        "Central Standard Time",           25, 21, "US", "US", 0x00000014, 0,                        }, /* 0x082 */
    { "America/Indiana/Vevay",            "US Eastern Standard Time",        21, 24, "US", "US", 0x00000028, 0,                        }, /* 0x083 */
    { "America/Indiana/Vincennes",        "Eastern Standard Time",           25, 21, "US", "US", 0x00000023, 0,                        }, /* 0x084 */
    { "America/Indiana/Winamac",          "Eastern Standard Time",           23, 21, "US", "US", 0x00000023, 0,                        }, /* 0x085 */
    { "America/Indianapolis",             "US Eastern Standard Time",        20, 24, "US", "US", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x086 */
    { "America/Inuvik",                   "Mountain Standard Time",          14, 22, "CA", "CA", 0x0000000a, 0,                        }, /* 0x087 */
    { "America/Iqaluit",                  "Eastern Standard Time",           15, 21, "CA", "CA", 0x00000023, 0,                        }, /* 0x088 */
    { "America/Jamaica",                  "SA Pacific Standard Time",        15, 24, "JM", "JM", 0x0000002d, 0,                        }, /* 0x089 */
    { "America/Jujuy",                    "Argentina Standard Time",         13, 23, "AR", "AR", 0x00000000, 0,                        }, /* 0x08a */
    { "America/Juneau",                   "Alaskan Standard Time",           14, 21, "US", "US", 0x00000003, 0,                        }, /* 0x08b */
    { "America/Kentucky/Louisville",      NULL,                              27, 0,  "US", "",   0x00000000, 0,                        }, /* 0x08c */
    { "America/Kentucky/Monticello",      "Eastern Standard Time",           27, 21, "US", "US", 0x00000023, 0,                        }, /* 0x08d */
    { "America/Knox_IN",                  NULL,                              15, 0,  "US", "",   0x00000000, 0,                        }, /* 0x08e */
    { "America/Kralendijk",               "SA Western Standard Time",        18, 24, "CW", "BQ", 0x00000000, 0,                        }, /* 0x08f */
    { "America/La_Paz",                   "SA Western Standard Time",        14, 24, "BO", "BO", 0x00000037, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x090 */
    { "America/Lima",                     "SA Pacific Standard Time",        12, 24, "PE", "PE", 0x0000002d, 0,                        }, /* 0x091 */
    { "America/Los_Angeles",              "Pacific Standard Time",           19, 21, "US", "US", 0x00000004, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x092 */
    { "America/Louisville",               "Eastern Standard Time",           18, 21, "US", "US", 0x00000000, 0,                        }, /* 0x093 */
    { "America/Lower_Princes",            "SA Western Standard Time",        21, 24, "CW", "SX", 0x00000000, 0,                        }, /* 0x094 */
    { "America/Maceio",                   "SA Eastern Standard Time",        14, 24, "BR", "BR", 0x00000046, 0,                        }, /* 0x095 */
    { "America/Managua",                  "Central America Standard Time",   15, 29, "NI", "NI", 0x00000021, 0,                        }, /* 0x096 */
    { "America/Manaus",                   "SA Western Standard Time",        14, 24, "BR", "BR", 0x00000037, 0,                        }, /* 0x097 */
    { "America/Marigot",                  "SA Western Standard Time",        15, 24, "TT", "MF", 0x00000000, 0,                        }, /* 0x098 */
    { "America/Martinique",               "SA Western Standard Time",        18, 24, "MQ", "MQ", 0x00000037, 0,                        }, /* 0x099 */
    { "America/Matamoros",                "Central Standard Time",           17, 21, "MX", "MX", 0x00000014, 0,                        }, /* 0x09a */
    { "America/Mazatlan",                 "Mountain Standard Time (Mexico)", 16, 31, "MX", "MX", 0x80000044, 0,                        }, /* 0x09b */
    { "America/Mendoza",                  "Argentina Standard Time",         15, 23, "AR", "AR", 0x00000000, 0,                        }, /* 0x09c */
    { "America/Menominee",                "Central Standard Time",           17, 21, "US", "US", 0x00000014, 0,                        }, /* 0x09d */
    { "America/Merida",                   "Central Standard Time (Mexico)",  14, 30, "MX", "MX", 0x80000043, 0,                        }, /* 0x09e */
    { "America/Metlakatla",               "Alaskan Standard Time",           18, 21, "US", "US", 0x00000003, 0,                        }, /* 0x09f */
    { "America/Mexico_City",              "Central Standard Time (Mexico)",  19, 30, "MX", "MX", 0x80000043, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x0a0 */
    { "America/Miquelon",                 "Saint Pierre Standard Time",      16, 26, "PM", "PM", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x0a1 */
    { "America/Moncton",                  "Atlantic Standard Time",          15, 22, "CA", "CA", 0x00000032, 0,                        }, /* 0x0a2 */
    { "America/Monterrey",                "Central Standard Time (Mexico)",  17, 30, "MX", "MX", 0x80000043, 0,                        }, /* 0x0a3 */
    { "America/Montevideo",               "Montevideo Standard Time",        18, 24, "UY", "UY", 0x80000049, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x0a4 */
    { "America/Montreal",                 "Eastern Standard Time",           16, 21, "CA", "CA", 0x00000000, 0,                        }, /* 0x0a5 */
    { "America/Montserrat",               "SA Western Standard Time",        18, 24, "TT", "MS", 0x00000000, 0,                        }, /* 0x0a6 */
    { "America/Nassau",                   "Eastern Standard Time",           14, 21, "BS", "BS", 0x00000023, 0,                        }, /* 0x0a7 */
    { "America/New_York",                 "Eastern Standard Time",           16, 21, "US", "US", 0x00000023, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x0a8 */
    { "America/Nipigon",                  "Eastern Standard Time",           15, 21, "CA", "CA", 0x00000023, 0,                        }, /* 0x0a9 */
    { "America/Nome",                     "Alaskan Standard Time",           12, 21, "US", "US", 0x00000003, 0,                        }, /* 0x0aa */
    { "America/Noronha",                  "UTC-02",                          15, 6,  "BR", "BR", 0x00000000, 0,                        }, /* 0x0ab */
    { "America/North_Dakota/Beulah",      "Central Standard Time",           27, 21, "US", "US", 0x00000014, 0,                        }, /* 0x0ac */
    { "America/North_Dakota/Center",      "Central Standard Time",           27, 21, "US", "US", 0x00000014, 0,                        }, /* 0x0ad */
    { "America/North_Dakota/New_Salem",   "Central Standard Time",           30, 21, "US", "US", 0x00000014, 0,                        }, /* 0x0ae */
    { "America/Ojinaga",                  "Mountain Standard Time",          15, 22, "MX", "MX", 0x0000000a, 0,                        }, /* 0x0af */
    { "America/Panama",                   "SA Pacific Standard Time",        14, 24, "PA", "PA", 0x0000002d, 0,                        }, /* 0x0b0 */
    { "America/Pangnirtung",              "Eastern Standard Time",           19, 21, "CA", "CA", 0x00000023, 0,                        }, /* 0x0b1 */
    { "America/Paramaribo",               "SA Eastern Standard Time",        18, 24, "SR", "SR", 0x00000046, 0,                        }, /* 0x0b2 */
    { "America/Phoenix",                  "US Mountain Standard Time",       15, 25, "US", "US", 0x0000000f, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x0b3 */
    { "America/Port-au-Prince",           "Haiti Standard Time",             22, 19, "HT", "HT", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x0b4 */
    { "America/Port_of_Spain",            "SA Western Standard Time",        21, 24, "TT", "TT", 0x00000037, 0,                        }, /* 0x0b5 */
    { "America/Porto_Acre",               NULL,                              18, 0,  "BR", "",   0x00000000, 0,                        }, /* 0x0b6 */
    { "America/Porto_Velho",              "SA Western Standard Time",        19, 24, "BR", "BR", 0x00000037, 0,                        }, /* 0x0b7 */
    { "America/Puerto_Rico",              "SA Western Standard Time",        19, 24, "PR", "PR", 0x00000037, 0,                        }, /* 0x0b8 */
    { "America/Punta_Arenas",             "SA Eastern Standard Time",        20, 24, "CL", "CL", 0x00000046, 0,                        }, /* 0x0b9 */
    { "America/Rainy_River",              "Central Standard Time",           19, 21, "CA", "CA", 0x00000014, 0,                        }, /* 0x0ba */
    { "America/Rankin_Inlet",             "Central Standard Time",           20, 21, "CA", "CA", 0x00000014, 0,                        }, /* 0x0bb */
    { "America/Recife",                   "SA Eastern Standard Time",        14, 24, "BR", "BR", 0x00000046, 0,                        }, /* 0x0bc */
    { "America/Regina",                   "Canada Central Standard Time",    14, 28, "CA", "CA", 0x00000019, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x0bd */
    { "America/Resolute",                 "Central Standard Time",           16, 21, "CA", "CA", 0x00000014, 0,                        }, /* 0x0be */
    { "America/Rio_Branco",               "SA Pacific Standard Time",        18, 24, "BR", "BR", 0x0000002d, 0,                        }, /* 0x0bf */
    { "America/Rosario",                  NULL,                              15, 0,  "AR", "",   0x00000000, 0,                        }, /* 0x0c0 */
    { "America/Santa_Isabel",             "Pacific Standard Time (Mexico)",  20, 30, "MX", "MX", 0x00000000, 0,                        }, /* 0x0c1 */
    { "America/Santarem",                 "SA Eastern Standard Time",        16, 24, "BR", "BR", 0x00000046, 0,                        }, /* 0x0c2 */
    { "America/Santiago",                 "Pacific SA Standard Time",        16, 24, "CL", "CL", 0x00000038, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x0c3 */
    { "America/Santo_Domingo",            "SA Western Standard Time",        21, 24, "DO", "DO", 0x00000037, 0,                        }, /* 0x0c4 */
    { "America/Sao_Paulo",                "E. South America Standard Time",  17, 30, "BR", "BR", 0x00000041, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x0c5 */
    { "America/Scoresbysund",             "Azores Standard Time",            20, 20, "GL", "GL", 0x00000050, 0,                        }, /* 0x0c6 */
    { "America/Shiprock",                 NULL,                              16, 0,  "US", "",   0x00000000, 0,                        }, /* 0x0c7 */
    { "America/Sitka",                    "Alaskan Standard Time",           13, 21, "US", "US", 0x00000003, 0,                        }, /* 0x0c8 */
    { "America/St_Barthelemy",            "SA Western Standard Time",        21, 24, "TT", "BL", 0x00000000, 0,                        }, /* 0x0c9 */
    { "America/St_Johns",                 "Newfoundland Standard Time",      16, 26, "CA", "CA", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x0ca */
    { "America/St_Kitts",                 "SA Western Standard Time",        16, 24, "TT", "KN", 0x00000000, 0,                        }, /* 0x0cb */
    { "America/St_Lucia",                 "SA Western Standard Time",        16, 24, "TT", "LC", 0x00000000, 0,                        }, /* 0x0cc */
    { "America/St_Thomas",                "SA Western Standard Time",        17, 24, "TT", "VI", 0x00000000, 0,                        }, /* 0x0cd */
    { "America/St_Vincent",               "SA Western Standard Time",        18, 24, "TT", "VC", 0x00000000, 0,                        }, /* 0x0ce */
    { "America/Swift_Current",            "Canada Central Standard Time",    21, 28, "CA", "CA", 0x00000019, 0,                        }, /* 0x0cf */
    { "America/Tegucigalpa",              "Central America Standard Time",   19, 29, "HN", "HN", 0x00000021, 0,                        }, /* 0x0d0 */
    { "America/Thule",                    "Atlantic Standard Time",          13, 22, "GL", "GL", 0x00000032, 0,                        }, /* 0x0d1 */
    { "America/Thunder_Bay",              "Eastern Standard Time",           19, 21, "CA", "CA", 0x00000023, 0,                        }, /* 0x0d2 */
    { "America/Tijuana",                  "Pacific Standard Time (Mexico)",  15, 30, "MX", "MX", 0x80000045, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x0d3 */
    { "America/Toronto",                  "Eastern Standard Time",           15, 21, "CA", "CA", 0x00000023, 0,                        }, /* 0x0d4 */
    { "America/Tortola",                  "SA Western Standard Time",        15, 24, "TT", "VG", 0x00000000, 0,                        }, /* 0x0d5 */
    { "America/Vancouver",                "Pacific Standard Time",           17, 21, "CA", "CA", 0x00000004, 0,                        }, /* 0x0d6 */
    { "America/Virgin",                   NULL,                              14, 0,  "TT", "",   0x00000000, 0,                        }, /* 0x0d7 */
    { "America/Whitehorse",               "Pacific Standard Time",           18, 21, "CA", "CA", 0x00000004, 0,                        }, /* 0x0d8 */
    { "America/Winnipeg",                 "Central Standard Time",           16, 21, "CA", "CA", 0x00000014, 0,                        }, /* 0x0d9 */
    { "America/Yakutat",                  "Alaskan Standard Time",           15, 21, "US", "US", 0x00000003, 0,                        }, /* 0x0da */
    { "America/Yellowknife",              "Mountain Standard Time",          19, 22, "CA", "CA", 0x0000000a, 0,                        }, /* 0x0db */
    { "Antarctica/Casey",                 "Central Pacific Standard Time",   16, 29, "AQ", "AQ", 0x00000118, 0,                        }, /* 0x0dc */
    { "Antarctica/Davis",                 "SE Asia Standard Time",           16, 21, "AQ", "AQ", 0x000000cd, 0,                        }, /* 0x0dd */
    { "Antarctica/DumontDUrville",        "West Pacific Standard Time",      25, 26, "AQ", "AQ", 0x00000113, 0,                        }, /* 0x0de */
    { "Antarctica/Macquarie",             "Central Pacific Standard Time",   20, 29, "AU", "AU", 0x00000118, 0,                        }, /* 0x0df */
    { "Antarctica/Mawson",                "West Asia Standard Time",         17, 23, "AQ", "AQ", 0x000000b9, 0,                        }, /* 0x0e0 */
    { "Antarctica/McMurdo",               "New Zealand Standard Time",       18, 25, "NZ", "AQ", 0x00000000, 0,                        }, /* 0x0e1 */
    { "Antarctica/Palmer",                "SA Eastern Standard Time",        17, 24, "AQ", "AQ", 0x00000046, 0,                        }, /* 0x0e2 */
    { "Antarctica/Rothera",               "SA Eastern Standard Time",        18, 24, "AQ", "AQ", 0x00000046, 0,                        }, /* 0x0e3 */
    { "Antarctica/South_Pole",            NULL,                              21, 0,  "NZ", "",   0x00000000, 0,                        }, /* 0x0e4 */
    { "Antarctica/Syowa",                 "E. Africa Standard Time",         16, 23, "AQ", "AQ", 0x0000009b, 0,                        }, /* 0x0e5 */
    { "Antarctica/Troll",                 NULL,                              16, 0,  "AQ", "",   0x00000000, 0,                        }, /* 0x0e6 */
    { "Antarctica/Vostok",                "Central Asia Standard Time",      17, 26, "AQ", "AQ", 0x000000c3, 0,                        }, /* 0x0e7 */
    { "Arctic/Longyearbyen",              "W. Europe Standard Time",         19, 23, "NO", "SJ", 0x00000000, 0,                        }, /* 0x0e8 */
    { "Asia/Aden",                        "Arab Standard Time",              9,  18, "SA", "YE", 0x00000000, 0,                        }, /* 0x0e9 */
    { "Asia/Almaty",                      "Central Asia Standard Time",      11, 26, "KZ", "KZ", 0x000000c3, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x0ea */
    { "Asia/Amman",                       "Jordan Standard Time",            10, 20, "JO", "JO", 0x80000042, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x0eb */
    { "Asia/Anadyr",                      "Russia Time Zone 11",             11, 19, "RU", "RU", 0x00000000, 0,                        }, /* 0x0ec */
    { "Asia/Aqtau",                       "West Asia Standard Time",         10, 23, "KZ", "KZ", 0x000000b9, 0,                        }, /* 0x0ed */
    { "Asia/Aqtobe",                      "West Asia Standard Time",         11, 23, "KZ", "KZ", 0x000000b9, 0,                        }, /* 0x0ee */
    { "Asia/Ashgabat",                    "West Asia Standard Time",         13, 23, "TM", "TM", 0x000000b9, 0,                        }, /* 0x0ef */
    { "Asia/Ashkhabad",                   NULL,                              14, 0,  "TM", "",   0x00000000, 0,                        }, /* 0x0f0 */
    { "Asia/Atyrau",                      "West Asia Standard Time",         11, 23, "KZ", "KZ", 0x000000b9, 0,                        }, /* 0x0f1 */
    { "Asia/Baghdad",                     "Arabic Standard Time",            12, 20, "IQ", "IQ", 0x0000009e, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x0f2 */
    { "Asia/Bahrain",                     "Arab Standard Time",              12, 18, "QA", "BH", 0x00000000, 0,                        }, /* 0x0f3 */
    { "Asia/Baku",                        "Azerbaijan Standard Time",        9,  24, "AZ", "AZ", 0x80000040, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x0f4 */
    { "Asia/Bangkok",                     "SE Asia Standard Time",           12, 21, "TH", "TH", 0x000000cd, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x0f5 */
    { "Asia/Barnaul",                     "Altai Standard Time",             12, 19, "RU", "RU", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x0f6 */
    { "Asia/Beirut",                      "Middle East Standard Time",       11, 25, "LB", "LB", 0x80000041, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x0f7 */
    { "Asia/Bishkek",                     "Central Asia Standard Time",      12, 26, "KG", "KG", 0x000000c3, 0,                        }, /* 0x0f8 */
    { "Asia/Brunei",                      "Singapore Standard Time",         11, 23, "BN", "BN", 0x000000d7, 0,                        }, /* 0x0f9 */
    { "Asia/Calcutta",                    "India Standard Time",             13, 19, "IN", "IN", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x0fa */
    { "Asia/Chita",                       "Transbaikal Standard Time",       10, 25, "RU", "RU", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x0fb */
    { "Asia/Choibalsan",                  "Ulaanbaatar Standard Time",       15, 25, "MN", "MN", 0x00000000, 0,                        }, /* 0x0fc */
    { "Asia/Chongqing",                   NULL,                              14, 0,  "CN", "",   0x00000000, 0,                        }, /* 0x0fd */
    { "Asia/Chungking",                   NULL,                              14, 0,  "CN", "",   0x00000000, 0,                        }, /* 0x0fe */
    { "Asia/Colombo",                     "Sri Lanka Standard Time",         12, 23, "LK", "LK", 0x000000c8, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x0ff */
    { "Asia/Dacca",                       NULL,                              10, 0,  "BD", "",   0x00000000, 0,                        }, /* 0x100 */
    { "Asia/Damascus",                    "Syria Standard Time",             13, 19, "SY", "SY", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x101 */
    { "Asia/Dhaka",                       "Bangladesh Standard Time",        10, 24, "BD", "BD", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x102 */
    { "Asia/Dili",                        "Tokyo Standard Time",             9,  19, "TL", "TL", 0x000000eb, 0,                        }, /* 0x103 */
    { "Asia/Dubai",                       "Arabian Standard Time",           10, 21, "AE", "AE", 0x000000a5, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x104 */
    { "Asia/Dushanbe",                    "West Asia Standard Time",         13, 23, "TJ", "TJ", 0x000000b9, 0,                        }, /* 0x105 */
    { "Asia/Famagusta",                   "Turkey Standard Time",            14, 20, "CY", "CY", 0x00000000, 0,                        }, /* 0x106 */
    { "Asia/Gaza",                        "West Bank Standard Time",         9,  23, "PS", "PS", 0x00000000, 0,                        }, /* 0x107 */
    { "Asia/Harbin",                      NULL,                              11, 0,  "CN", "",   0x00000000, 0,                        }, /* 0x108 */
    { "Asia/Hebron",                      "West Bank Standard Time",         11, 23, "PS", "PS", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x109 */
    { "Asia/Ho_Chi_Minh",                 NULL,                              16, 0,  "VN", "",   0x00000000, 0,                        }, /* 0x10a */
    { "Asia/Hong_Kong",                   "China Standard Time",             14, 19, "HK", "HK", 0x000000d2, 0,                        }, /* 0x10b */
    { "Asia/Hovd",                        "W. Mongolia Standard Time",       9,  25, "MN", "MN", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x10c */
    { "Asia/Irkutsk",                     "North Asia East Standard Time",   12, 29, "RU", "RU", 0x000000e3, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x10d */
    { "Asia/Istanbul",                    NULL,                              13, 0,  "TR", "",   0x00000000, 0,                        }, /* 0x10e */
    { "Asia/Jakarta",                     "SE Asia Standard Time",           12, 21, "ID", "ID", 0x000000cd, 0,                        }, /* 0x10f */
    { "Asia/Jayapura",                    "Tokyo Standard Time",             13, 19, "ID", "ID", 0x000000eb, 0,                        }, /* 0x110 */
    { "Asia/Jerusalem",                   "Israel Standard Time",            14, 20, "IL", "IL", 0x00000087, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x111 */
    { "Asia/Kabul",                       "Afghanistan Standard Time",       10, 25, "AF", "AF", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x112 */
    { "Asia/Kamchatka",                   "Russia Time Zone 11",             14, 19, "RU", "RU", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x113 */
    { "Asia/Karachi",                     "Pakistan Standard Time",          12, 22, "PK", "PK", 0x8000004e, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x114 */
    { "Asia/Kashgar",                     NULL,                              12, 0,  "CN", "",   0x00000000, 0,                        }, /* 0x115 */
    { "Asia/Kathmandu",                   NULL,                              14, 0,  "NP", "",   0x00000000, 0,                        }, /* 0x116 */
    { "Asia/Katmandu",                    "Nepal Standard Time",             13, 19, "NP", "NP", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x117 */
    { "Asia/Khandyga",                    "Yakutsk Standard Time",           13, 21, "RU", "RU", 0x000000f0, 0,                        }, /* 0x118 */
    { "Asia/Kolkata",                     NULL,                              12, 0,  "IN", "",   0x00000000, 0,                        }, /* 0x119 */
    { "Asia/Krasnoyarsk",                 "North Asia Standard Time",        16, 24, "RU", "RU", 0x000000cf, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x11a */
    { "Asia/Kuala_Lumpur",                "Singapore Standard Time",         17, 23, "MY", "MY", 0x000000d7, 0,                        }, /* 0x11b */
    { "Asia/Kuching",                     "Singapore Standard Time",         12, 23, "MY", "MY", 0x000000d7, 0,                        }, /* 0x11c */
    { "Asia/Kuwait",                      "Arab Standard Time",              11, 18, "SA", "KW", 0x00000000, 0,                        }, /* 0x11d */
    { "Asia/Macao",                       NULL,                              10, 0,  "MO", "",   0x00000000, 0,                        }, /* 0x11e */
    { "Asia/Macau",                       "China Standard Time",             10, 19, "MO", "MO", 0x000000d2, 0,                        }, /* 0x11f */
    { "Asia/Magadan",                     "Magadan Standard Time",           12, 21, "RU", "RU", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x120 */
    { "Asia/Makassar",                    "Singapore Standard Time",         13, 23, "ID", "ID", 0x000000d7, 0,                        }, /* 0x121 */
    { "Asia/Manila",                      "Singapore Standard Time",         11, 23, "PH", "PH", 0x000000d7, 0,                        }, /* 0x122 */
    { "Asia/Muscat",                      "Arabian Standard Time",           11, 21, "AE", "OM", 0x00000000, 0,                        }, /* 0x123 */
    { "Asia/Nicosia",                     "GTB Standard Time",               12, 17, "CY", "CY", 0x00000082, 0,                        }, /* 0x124 */
    { "Asia/Novokuznetsk",                "North Asia Standard Time",        17, 24, "RU", "RU", 0x000000cf, 0,                        }, /* 0x125 */
    { "Asia/Novosibirsk",                 "N. Central Asia Standard Time",   16, 29, "RU", "RU", 0x000000c9, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x126 */
    { "Asia/Omsk",                        "Omsk Standard Time",              9,  18, "RU", "RU", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x127 */
    { "Asia/Oral",                        "West Asia Standard Time",         9,  23, "KZ", "KZ", 0x000000b9, 0,                        }, /* 0x128 */
    { "Asia/Phnom_Penh",                  "SE Asia Standard Time",           15, 21, "TH", "KH", 0x00000000, 0,                        }, /* 0x129 */
    { "Asia/Pontianak",                   "SE Asia Standard Time",           14, 21, "ID", "ID", 0x000000cd, 0,                        }, /* 0x12a */
    { "Asia/Pyongyang",                   "North Korea Standard Time",       14, 25, "KP", "KP", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x12b */
    { "Asia/Qatar",                       "Arab Standard Time",              10, 18, "QA", "QA", 0x00000096, 0,                        }, /* 0x12c */
    { "Asia/Qyzylorda",                   "Central Asia Standard Time",      14, 26, "KZ", "KZ", 0x000000c3, 0,                        }, /* 0x12d */
    { "Asia/Rangoon",                     "Myanmar Standard Time",           12, 21, "MM", "MM", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x12e */
    { "Asia/Riyadh",                      "Arab Standard Time",              11, 18, "SA", "SA", 0x00000096, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x12f */
    { "Asia/Saigon",                      "SE Asia Standard Time",           11, 21, "VN", "VN", 0x00000000, 0,                        }, /* 0x130 */
    { "Asia/Sakhalin",                    "Sakhalin Standard Time",          13, 22, "RU", "RU", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x131 */
    { "Asia/Samarkand",                   "West Asia Standard Time",         14, 23, "UZ", "UZ", 0x000000b9, 0,                        }, /* 0x132 */
    { "Asia/Seoul",                       "Korea Standard Time",             10, 19, "KR", "KR", 0x000000e6, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x133 */
    { "Asia/Shanghai",                    "China Standard Time",             13, 19, "CN", "CN", 0x000000d2, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x134 */
    { "Asia/Singapore",                   "Singapore Standard Time",         14, 23, "SG", "SG", 0x000000d7, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x135 */
    { "Asia/Srednekolymsk",               "Russia Time Zone 10",             18, 19, "RU", "RU", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x136 */
    { "Asia/Taipei",                      "Taipei Standard Time",            11, 20, "TW", "TW", 0x000000dc, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x137 */
    { "Asia/Tashkent",                    "West Asia Standard Time",         13, 23, "UZ", "UZ", 0x000000b9, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x138 */
    { "Asia/Tbilisi",                     "Georgian Standard Time",          12, 22, "GE", "GE", 0x80000047, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x139 */
    { "Asia/Tehran",                      "Iran Standard Time",              11, 18, "IR", "IR", 0x000000a0, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x13a */
    { "Asia/Tel_Aviv",                    NULL,                              13, 0,  "IL", "",   0x00000000, 0,                        }, /* 0x13b */
    { "Asia/Thimbu",                      NULL,                              11, 0,  "BT", "",   0x00000000, 0,                        }, /* 0x13c */
    { "Asia/Thimphu",                     "Bangladesh Standard Time",        12, 24, "BT", "BT", 0x00000000, 0,                        }, /* 0x13d */
    { "Asia/Tokyo",                       "Tokyo Standard Time",             10, 19, "JP", "JP", 0x000000eb, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x13e */
    { "Asia/Tomsk",                       "Tomsk Standard Time",             10, 19, "RU", "RU", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x13f */
    { "Asia/Ujung_Pandang",               NULL,                              18, 0,  "ID", "",   0x00000000, 0,                        }, /* 0x140 */
    { "Asia/Ulaanbaatar",                 "Ulaanbaatar Standard Time",       16, 25, "MN", "MN", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x141 */
    { "Asia/Ulan_Bator",                  NULL,                              15, 0,  "MN", "",   0x00000000, 0,                        }, /* 0x142 */
    { "Asia/Urumqi",                      "Central Asia Standard Time",      11, 26, "CN", "CN", 0x000000c3, 0,                        }, /* 0x143 */
    { "Asia/Ust-Nera",                    "Vladivostok Standard Time",       13, 25, "RU", "RU", 0x0000010e, 0,                        }, /* 0x144 */
    { "Asia/Vientiane",                   "SE Asia Standard Time",           14, 21, "TH", "LA", 0x00000000, 0,                        }, /* 0x145 */
    { "Asia/Vladivostok",                 "Vladivostok Standard Time",       16, 25, "RU", "RU", 0x0000010e, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x146 */
    { "Asia/Yakutsk",                     "Yakutsk Standard Time",           12, 21, "RU", "RU", 0x000000f0, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x147 */
    { "Asia/Yangon",                      NULL,                              11, 0,  "MM", "",   0x00000000, 0,                        }, /* 0x148 */
    { "Asia/Yekaterinburg",               "Ekaterinburg Standard Time",      18, 26, "RU", "RU", 0x000000b4, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x149 */
    { "Asia/Yerevan",                     "Caucasus Standard Time",          12, 22, "AM", "AM", 0x000000aa, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x14a */
    { "Atlantic/Azores",                  "Azores Standard Time",            15, 20, "PT", "PT", 0x00000050, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x14b */
    { "Atlantic/Bermuda",                 "Atlantic Standard Time",          16, 22, "BM", "BM", 0x00000032, 0,                        }, /* 0x14c */
    { "Atlantic/Canary",                  "GMT Standard Time",               15, 17, "ES", "ES", 0x00000055, 0,                        }, /* 0x14d */
    { "Atlantic/Cape_Verde",              "Cape Verde Standard Time",        19, 24, "CV", "CV", 0x00000053, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x14e */
    { "Atlantic/Faeroe",                  "GMT Standard Time",               15, 17, "FO", "FO", 0x00000000, 0,                        }, /* 0x14f */
    { "Atlantic/Faroe",                   NULL,                              14, 0,  "FO", "",   0x00000000, 0,                        }, /* 0x150 */
    { "Atlantic/Jan_Mayen",               NULL,                              18, 0,  "NO", "",   0x00000000, 0,                        }, /* 0x151 */
    { "Atlantic/Madeira",                 "GMT Standard Time",               16, 17, "PT", "PT", 0x00000055, 0,                        }, /* 0x152 */
    { "Atlantic/Reykjavik",               "Greenwich Standard Time",         18, 23, "IS", "IS", 0x0000005a, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x153 */
    { "Atlantic/South_Georgia",           "UTC-02",                          22, 6,  "GS", "GS", 0x00000000, 0,                        }, /* 0x154 */
    { "Atlantic/St_Helena",               "Greenwich Standard Time",         18, 23, "CI", "SH", 0x00000000, 0,                        }, /* 0x155 */
    { "Atlantic/Stanley",                 "SA Eastern Standard Time",        16, 24, "FK", "FK", 0x00000046, 0,                        }, /* 0x156 */
    { "Australia/ACT",                    NULL,                              13, 0,  "AU", "",   0x00000000, 0,                        }, /* 0x157 */
    { "Australia/Adelaide",               "Cen. Australia Standard Time",    18, 28, "AU", "AU", 0x000000fa, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x158 */
    { "Australia/Brisbane",               "E. Australia Standard Time",      18, 26, "AU", "AU", 0x00000104, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x159 */
    { "Australia/Broken_Hill",            "Cen. Australia Standard Time",    21, 28, "AU", "AU", 0x000000fa, 0,                        }, /* 0x15a */
    { "Australia/Canberra",               NULL,                              18, 0,  "AU", "",   0x00000000, 0,                        }, /* 0x15b */
    { "Australia/Currie",                 "Tasmania Standard Time",          16, 22, "AU", "AU", 0x00000109, 0,                        }, /* 0x15c */
    { "Australia/Darwin",                 "AUS Central Standard Time",       16, 25, "AU", "AU", 0x000000f5, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x15d */
    { "Australia/Eucla",                  "Aus Central W. Standard Time",    15, 28, "AU", "AU", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x15e */
    { "Australia/Hobart",                 "Tasmania Standard Time",          16, 22, "AU", "AU", 0x00000109, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x15f */
    { "Australia/LHI",                    NULL,                              13, 0,  "AU", "",   0x00000000, 0,                        }, /* 0x160 */
    { "Australia/Lindeman",               "E. Australia Standard Time",      18, 26, "AU", "AU", 0x00000104, 0,                        }, /* 0x161 */
    { "Australia/Lord_Howe",              "Lord Howe Standard Time",         19, 23, "AU", "AU", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x162 */
    { "Australia/Melbourne",              "AUS Eastern Standard Time",       19, 25, "AU", "AU", 0x000000ff, 0,                        }, /* 0x163 */
    { "Australia/NSW",                    NULL,                              13, 0,  "AU", "",   0x00000000, 0,                        }, /* 0x164 */
    { "Australia/North",                  NULL,                              15, 0,  "AU", "",   0x00000000, 0,                        }, /* 0x165 */
    { "Australia/Perth",                  "W. Australia Standard Time",      15, 26, "AU", "AU", 0x000000e1, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x166 */
    { "Australia/Queensland",             NULL,                              20, 0,  "AU", "",   0x00000000, 0,                        }, /* 0x167 */
    { "Australia/South",                  NULL,                              15, 0,  "AU", "",   0x00000000, 0,                        }, /* 0x168 */
    { "Australia/Sydney",                 "AUS Eastern Standard Time",       16, 25, "AU", "AU", 0x000000ff, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x169 */
    { "Australia/Tasmania",               NULL,                              18, 0,  "AU", "",   0x00000000, 0,                        }, /* 0x16a */
    { "Australia/Victoria",               NULL,                              18, 0,  "AU", "",   0x00000000, 0,                        }, /* 0x16b */
    { "Australia/West",                   NULL,                              14, 0,  "AU", "",   0x00000000, 0,                        }, /* 0x16c */
    { "Australia/Yancowinna",             NULL,                              20, 0,  "AU", "",   0x00000000, 0,                        }, /* 0x16d */
    { "Brazil/Acre",                      NULL,                              11, 0,  "BR", "",   0x00000000, 0,                        }, /* 0x16e */
    { "Brazil/DeNoronha",                 NULL,                              16, 0,  "BR", "",   0x00000000, 0,                        }, /* 0x16f */
    { "Brazil/East",                      NULL,                              11, 0,  "BR", "",   0x00000000, 0,                        }, /* 0x170 */
    { "Brazil/West",                      NULL,                              11, 0,  "BR", "",   0x00000000, 0,                        }, /* 0x171 */
    { "CET",                              NULL,                              3,  0,  "ZZ", "",   0x00000000, 0,                        }, /* 0x172 */
    { "CST6CDT",                          "Central Standard Time",           7,  21, "ZZ", "ZZ", 0x00000014, 0,                        }, /* 0x173 */
    { "Canada/Atlantic",                  NULL,                              15, 0,  "CA", "",   0x00000000, 0,                        }, /* 0x174 */
    { "Canada/Central",                   NULL,                              14, 0,  "CA", "",   0x00000000, 0,                        }, /* 0x175 */
    { "Canada/East-Saskatchewan",         NULL,                              24, 0,  "CA", "",   0x00000000, 0,                        }, /* 0x176 */
    { "Canada/Eastern",                   NULL,                              14, 0,  "CA", "",   0x00000000, 0,                        }, /* 0x177 */
    { "Canada/Mountain",                  NULL,                              15, 0,  "CA", "",   0x00000000, 0,                        }, /* 0x178 */
    { "Canada/Newfoundland",              NULL,                              19, 0,  "CA", "",   0x00000000, 0,                        }, /* 0x179 */
    { "Canada/Pacific",                   NULL,                              14, 0,  "CA", "",   0x00000000, 0,                        }, /* 0x17a */
    { "Canada/Saskatchewan",              NULL,                              19, 0,  "CA", "",   0x00000000, 0,                        }, /* 0x17b */
    { "Canada/Yukon",                     NULL,                              12, 0,  "CA", "",   0x00000000, 0,                        }, /* 0x17c */
    { "Chile/Continental",                NULL,                              17, 0,  "CL", "",   0x00000000, 0,                        }, /* 0x17d */
    { "Chile/EasterIsland",               NULL,                              18, 0,  "CL", "",   0x00000000, 0,                        }, /* 0x17e */
    { "Cuba",                             NULL,                              4,  0,  "CU", "",   0x00000000, 0,                        }, /* 0x17f */
    { "EET",                              NULL,                              3,  0,  "ZZ", "",   0x00000000, 0,                        }, /* 0x180 */
    { "EST",                              NULL,                              3,  0,  "ZZ", "",   0x00000000, 0,                        }, /* 0x181 */
    { "EST5EDT",                          "Eastern Standard Time",           7,  21, "ZZ", "ZZ", 0x00000023, 0,                        }, /* 0x182 */
    { "Egypt",                            NULL,                              5,  0,  "EG", "",   0x00000000, 0,                        }, /* 0x183 */
    { "Eire",                             NULL,                              4,  0,  "IE", "",   0x00000000, 0,                        }, /* 0x184 */
    { "Etc/GMT",                          "UTC",                             7,  3,  "ZZ", "ZZ", 0x80000050, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x185 */
    { "Etc/GMT+0",                        NULL,                              9,  0,  "ZZ", "",   0x00000000, 0,                        }, /* 0x186 */
    { "Etc/GMT+1",                        "Cape Verde Standard Time",        9,  24, "ZZ", "ZZ", 0x00000053, 0,                        }, /* 0x187 */
    { "Etc/GMT+10",                       "Hawaiian Standard Time",          10, 22, "ZZ", "ZZ", 0x00000002, 0,                        }, /* 0x188 */
    { "Etc/GMT+11",                       "UTC-11",                          10, 6,  "ZZ", "ZZ", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x189 */
    { "Etc/GMT+12",                       "Dateline Standard Time",          10, 22, "ZZ", "ZZ", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x18a */
    { "Etc/GMT+2",                        "UTC-02",                          9,  6,  "ZZ", "ZZ", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x18b */
    { "Etc/GMT+3",                        "SA Eastern Standard Time",        9,  24, "ZZ", "ZZ", 0x00000046, 0,                        }, /* 0x18c */
    { "Etc/GMT+4",                        "SA Western Standard Time",        9,  24, "ZZ", "ZZ", 0x00000037, 0,                        }, /* 0x18d */
    { "Etc/GMT+5",                        "SA Pacific Standard Time",        9,  24, "ZZ", "ZZ", 0x0000002d, 0,                        }, /* 0x18e */
    { "Etc/GMT+6",                        "Central America Standard Time",   9,  29, "ZZ", "ZZ", 0x00000021, 0,                        }, /* 0x18f */
    { "Etc/GMT+7",                        "US Mountain Standard Time",       9,  25, "ZZ", "ZZ", 0x0000000f, 0,                        }, /* 0x190 */
    { "Etc/GMT+8",                        "UTC-08",                          9,  6,  "ZZ", "ZZ", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x191 */
    { "Etc/GMT+9",                        "UTC-09",                          9,  6,  "ZZ", "ZZ", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x192 */
    { "Etc/GMT-0",                        NULL,                              9,  0,  "ZZ", "",   0x00000000, 0,                        }, /* 0x193 */
    { "Etc/GMT-1",                        "W. Central Africa Standard Time", 9,  31, "ZZ", "ZZ", 0x00000071, 0,                        }, /* 0x194 */
    { "Etc/GMT-10",                       "West Pacific Standard Time",      10, 26, "ZZ", "ZZ", 0x00000113, 0,                        }, /* 0x195 */
    { "Etc/GMT-11",                       "Central Pacific Standard Time",   10, 29, "ZZ", "ZZ", 0x00000118, 0,                        }, /* 0x196 */
    { "Etc/GMT-12",                       "UTC+12",                          10, 6,  "ZZ", "ZZ", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x197 */
    { "Etc/GMT-13",                       "Tonga Standard Time",             10, 19, "ZZ", "ZZ", 0x0000012c, 0,                        }, /* 0x198 */
    { "Etc/GMT-14",                       "Line Islands Standard Time",      10, 26, "ZZ", "ZZ", 0x00000000, 0,                        }, /* 0x199 */
    { "Etc/GMT-2",                        "South Africa Standard Time",      9,  26, "ZZ", "ZZ", 0x0000008c, 0,                        }, /* 0x19a */
    { "Etc/GMT-3",                        "E. Africa Standard Time",         9,  23, "ZZ", "ZZ", 0x0000009b, 0,                        }, /* 0x19b */
    { "Etc/GMT-4",                        "Arabian Standard Time",           9,  21, "ZZ", "ZZ", 0x000000a5, 0,                        }, /* 0x19c */
    { "Etc/GMT-5",                        "West Asia Standard Time",         9,  23, "ZZ", "ZZ", 0x000000b9, 0,                        }, /* 0x19d */
    { "Etc/GMT-6",                        "Central Asia Standard Time",      9,  26, "ZZ", "ZZ", 0x000000c3, 0,                        }, /* 0x19e */
    { "Etc/GMT-7",                        "SE Asia Standard Time",           9,  21, "ZZ", "ZZ", 0x000000cd, 0,                        }, /* 0x19f */
    { "Etc/GMT-8",                        "Singapore Standard Time",         9,  23, "ZZ", "ZZ", 0x000000d7, 0,                        }, /* 0x1a0 */
    { "Etc/GMT-9",                        "Tokyo Standard Time",             9,  19, "ZZ", "ZZ", 0x000000eb, 0,                        }, /* 0x1a1 */
    { "Etc/GMT0",                         NULL,                              8,  0,  "ZZ", "",   0x00000000, 0,                        }, /* 0x1a2 */
    { "Etc/Greenwich",                    NULL,                              13, 0,  "ZZ", "",   0x00000000, 0,                        }, /* 0x1a3 */
    { "Etc/UCT",                          NULL,                              7,  0,  "ZZ", "",   0x00000000, 0,                        }, /* 0x1a4 */
    { "Etc/UTC",                          "UTC",                             7,  3,  "ZZ", "ZZ", 0x80000050, 0,                        }, /* 0x1a5 */
    { "Etc/Universal",                    NULL,                              13, 0,  "ZZ", "",   0x00000000, 0,                        }, /* 0x1a6 */
    { "Etc/Zulu",                         NULL,                              8,  0,  "ZZ", "",   0x00000000, 0,                        }, /* 0x1a7 */
    { "Europe/Amsterdam",                 "W. Europe Standard Time",         16, 23, "NL", "NL", 0x0000006e, 0,                        }, /* 0x1a8 */
    { "Europe/Andorra",                   "W. Europe Standard Time",         14, 23, "AD", "AD", 0x0000006e, 0,                        }, /* 0x1a9 */
    { "Europe/Astrakhan",                 "Astrakhan Standard Time",         16, 23, "RU", "RU", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x1aa */
    { "Europe/Athens",                    "GTB Standard Time",               13, 17, "GR", "GR", 0x00000082, 0,                        }, /* 0x1ab */
    { "Europe/Belfast",                   NULL,                              14, 0,  "GB", "",   0x00000000, 0,                        }, /* 0x1ac */
    { "Europe/Belgrade",                  "Central Europe Standard Time",    15, 28, "RS", "RS", 0x0000005f, 0,                        }, /* 0x1ad */
    { "Europe/Berlin",                    "W. Europe Standard Time",         13, 23, "DE", "DE", 0x0000006e, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x1ae */
    { "Europe/Bratislava",                "Central Europe Standard Time",    17, 28, "CZ", "SK", 0x00000000, 0,                        }, /* 0x1af */
    { "Europe/Brussels",                  "Romance Standard Time",           15, 21, "BE", "BE", 0x00000069, 0,                        }, /* 0x1b0 */
    { "Europe/Bucharest",                 "GTB Standard Time",               16, 17, "RO", "RO", 0x00000082, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x1b1 */
    { "Europe/Budapest",                  "Central Europe Standard Time",    15, 28, "HU", "HU", 0x0000005f, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x1b2 */
    { "Europe/Busingen",                  "W. Europe Standard Time",         15, 23, "CH", "DE", 0x00000000, 0,                        }, /* 0x1b3 */
    { "Europe/Chisinau",                  "E. Europe Standard Time",         15, 23, "MD", "MD", 0x00000073, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x1b4 */
    { "Europe/Copenhagen",                "Romance Standard Time",           17, 21, "DK", "DK", 0x00000069, 0,                        }, /* 0x1b5 */
    { "Europe/Dublin",                    "GMT Standard Time",               13, 17, "IE", "IE", 0x00000055, 0,                        }, /* 0x1b6 */
    { "Europe/Gibraltar",                 "W. Europe Standard Time",         16, 23, "GI", "GI", 0x0000006e, 0,                        }, /* 0x1b7 */
    { "Europe/Guernsey",                  "GMT Standard Time",               15, 17, "GB", "GG", 0x00000000, 0,                        }, /* 0x1b8 */
    { "Europe/Helsinki",                  "FLE Standard Time",               15, 17, "FI", "FI", 0x0000007d, 0,                        }, /* 0x1b9 */
    { "Europe/Isle_of_Man",               "GMT Standard Time",               18, 17, "GB", "IM", 0x00000000, 0,                        }, /* 0x1ba */
    { "Europe/Istanbul",                  "Turkey Standard Time",            15, 20, "TR", "TR", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x1bb */
    { "Europe/Jersey",                    "GMT Standard Time",               13, 17, "GB", "JE", 0x00000000, 0,                        }, /* 0x1bc */
    { "Europe/Kaliningrad",               "Kaliningrad Standard Time",       18, 25, "RU", "RU", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x1bd */
    { "Europe/Kiev",                      "FLE Standard Time",               11, 17, "UA", "UA", 0x0000007d, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x1be */
    { "Europe/Kirov",                     "Russian Standard Time",           12, 21, "RU", "RU", 0x00000091, 0,                        }, /* 0x1bf */
    { "Europe/Lisbon",                    "GMT Standard Time",               13, 17, "PT", "PT", 0x00000055, 0,                        }, /* 0x1c0 */
    { "Europe/Ljubljana",                 "Central Europe Standard Time",    16, 28, "RS", "SI", 0x00000000, 0,                        }, /* 0x1c1 */
    { "Europe/London",                    "GMT Standard Time",               13, 17, "GB", "GB", 0x00000055, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x1c2 */
    { "Europe/Luxembourg",                "W. Europe Standard Time",         17, 23, "LU", "LU", 0x0000006e, 0,                        }, /* 0x1c3 */
    { "Europe/Madrid",                    "Romance Standard Time",           13, 21, "ES", "ES", 0x00000069, 0,                        }, /* 0x1c4 */
    { "Europe/Malta",                     "W. Europe Standard Time",         12, 23, "MT", "MT", 0x0000006e, 0,                        }, /* 0x1c5 */
    { "Europe/Mariehamn",                 "FLE Standard Time",               16, 17, "FI", "AX", 0x00000000, 0,                        }, /* 0x1c6 */
    { "Europe/Minsk",                     "Belarus Standard Time",           12, 21, "BY", "BY", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x1c7 */
    { "Europe/Monaco",                    "W. Europe Standard Time",         13, 23, "MC", "MC", 0x0000006e, 0,                        }, /* 0x1c8 */
    { "Europe/Moscow",                    "Russian Standard Time",           13, 21, "RU", "RU", 0x00000091, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x1c9 */
    { "Europe/Nicosia",                   NULL,                              14, 0,  "CY", "",   0x00000000, 0,                        }, /* 0x1ca */
    { "Europe/Oslo",                      "W. Europe Standard Time",         11, 23, "NO", "NO", 0x0000006e, 0,                        }, /* 0x1cb */
    { "Europe/Paris",                     "Romance Standard Time",           12, 21, "FR", "FR", 0x00000069, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x1cc */
    { "Europe/Podgorica",                 "Central Europe Standard Time",    16, 28, "RS", "ME", 0x00000000, 0,                        }, /* 0x1cd */
    { "Europe/Prague",                    "Central Europe Standard Time",    13, 28, "CZ", "CZ", 0x0000005f, 0,                        }, /* 0x1ce */
    { "Europe/Riga",                      "FLE Standard Time",               11, 17, "LV", "LV", 0x0000007d, 0,                        }, /* 0x1cf */
    { "Europe/Rome",                      "W. Europe Standard Time",         11, 23, "IT", "IT", 0x0000006e, 0,                        }, /* 0x1d0 */
    { "Europe/Samara",                    "Russia Time Zone 3",              13, 18, "RU", "RU", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x1d1 */
    { "Europe/San_Marino",                "W. Europe Standard Time",         17, 23, "IT", "SM", 0x00000000, 0,                        }, /* 0x1d2 */
    { "Europe/Sarajevo",                  "Central European Standard Time",  15, 30, "RS", "BA", 0x00000000, 0,                        }, /* 0x1d3 */
    { "Europe/Saratov",                   "Astrakhan Standard Time",         14, 23, "RU", "RU", 0x00000000, 0,                        }, /* 0x1d4 */
    { "Europe/Simferopol",                "Russian Standard Time",           17, 21, "RU", "UA", 0x00000091, 0,                        }, /* 0x1d5 */
    { "Europe/Skopje",                    "Central European Standard Time",  13, 30, "RS", "MK", 0x00000000, 0,                        }, /* 0x1d6 */
    { "Europe/Sofia",                     "FLE Standard Time",               12, 17, "BG", "BG", 0x0000007d, 0,                        }, /* 0x1d7 */
    { "Europe/Stockholm",                 "W. Europe Standard Time",         16, 23, "SE", "SE", 0x0000006e, 0,                        }, /* 0x1d8 */
    { "Europe/Tallinn",                   "FLE Standard Time",               14, 17, "EE", "EE", 0x0000007d, 0,                        }, /* 0x1d9 */
    { "Europe/Tirane",                    "Central Europe Standard Time",    13, 28, "AL", "AL", 0x0000005f, 0,                        }, /* 0x1da */
    { "Europe/Tiraspol",                  NULL,                              15, 0,  "MD", "",   0x00000000, 0,                        }, /* 0x1db */
    { "Europe/Ulyanovsk",                 "Astrakhan Standard Time",         16, 23, "RU", "RU", 0x00000000, 0,                        }, /* 0x1dc */
    { "Europe/Uzhgorod",                  "FLE Standard Time",               15, 17, "UA", "UA", 0x0000007d, 0,                        }, /* 0x1dd */
    { "Europe/Vaduz",                     "W. Europe Standard Time",         12, 23, "CH", "LI", 0x00000000, 0,                        }, /* 0x1de */
    { "Europe/Vatican",                   "W. Europe Standard Time",         14, 23, "IT", "VA", 0x00000000, 0,                        }, /* 0x1df */
    { "Europe/Vienna",                    "W. Europe Standard Time",         13, 23, "AT", "AT", 0x0000006e, 0,                        }, /* 0x1e0 */
    { "Europe/Vilnius",                   "FLE Standard Time",               14, 17, "LT", "LT", 0x0000007d, 0,                        }, /* 0x1e1 */
    { "Europe/Volgograd",                 "Russian Standard Time",           16, 21, "RU", "RU", 0x00000091, 0,                        }, /* 0x1e2 */
    { "Europe/Warsaw",                    "Central European Standard Time",  13, 30, "PL", "PL", 0x00000064, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x1e3 */
    { "Europe/Zagreb",                    "Central European Standard Time",  13, 30, "RS", "HR", 0x00000000, 0,                        }, /* 0x1e4 */
    { "Europe/Zaporozhye",                "FLE Standard Time",               17, 17, "UA", "UA", 0x0000007d, 0,                        }, /* 0x1e5 */
    { "Europe/Zurich",                    "W. Europe Standard Time",         13, 23, "CH", "CH", 0x0000006e, 0,                        }, /* 0x1e6 */
    { "Factory",                          NULL,                              7,  0,  "ZZ", "",   0x00000000, 0,                        }, /* 0x1e7 */
    { "GB",                               NULL,                              2,  0,  "GB", "",   0x00000000, 0,                        }, /* 0x1e8 */
    { "GB-Eire",                          NULL,                              7,  0,  "GB", "",   0x00000000, 0,                        }, /* 0x1e9 */
    { "GMT",                              NULL,                              3,  0,  "ZZ", "",   0x00000000, 0,                        }, /* 0x1ea */
    { "GMT+0",                            NULL,                              5,  0,  "ZZ", "",   0x00000000, 0,                        }, /* 0x1eb */
    { "GMT-0",                            NULL,                              5,  0,  "ZZ", "",   0x00000000, 0,                        }, /* 0x1ec */
    { "GMT0",                             NULL,                              4,  0,  "ZZ", "",   0x00000000, 0,                        }, /* 0x1ed */
    { "Greenwich",                        NULL,                              9,  0,  "ZZ", "",   0x00000000, 0,                        }, /* 0x1ee */
    { "HST",                              NULL,                              3,  0,  "ZZ", "",   0x00000000, 0,                        }, /* 0x1ef */
    { "Hongkong",                         NULL,                              8,  0,  "HK", "",   0x00000000, 0,                        }, /* 0x1f0 */
    { "Iceland",                          NULL,                              7,  0,  "IS", "",   0x00000000, 0,                        }, /* 0x1f1 */
    { "Indian/Antananarivo",              "E. Africa Standard Time",         19, 23, "KE", "MG", 0x00000000, 0,                        }, /* 0x1f2 */
    { "Indian/Chagos",                    "Central Asia Standard Time",      13, 26, "IO", "IO", 0x000000c3, 0,                        }, /* 0x1f3 */
    { "Indian/Christmas",                 "SE Asia Standard Time",           16, 21, "CX", "CX", 0x000000cd, 0,                        }, /* 0x1f4 */
    { "Indian/Cocos",                     "Myanmar Standard Time",           12, 21, "CC", "CC", 0x000000cb, 0,                        }, /* 0x1f5 */
    { "Indian/Comoro",                    "E. Africa Standard Time",         13, 23, "KE", "KM", 0x00000000, 0,                        }, /* 0x1f6 */
    { "Indian/Kerguelen",                 "West Asia Standard Time",         16, 23, "TF", "TF", 0x000000b9, 0,                        }, /* 0x1f7 */
    { "Indian/Mahe",                      "Mauritius Standard Time",         11, 23, "SC", "SC", 0x8000004f, 0,                        }, /* 0x1f8 */
    { "Indian/Maldives",                  "West Asia Standard Time",         15, 23, "MV", "MV", 0x000000b9, 0,                        }, /* 0x1f9 */
    { "Indian/Mauritius",                 "Mauritius Standard Time",         16, 23, "MU", "MU", 0x8000004f, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x1fa */
    { "Indian/Mayotte",                   "E. Africa Standard Time",         14, 23, "KE", "YT", 0x00000000, 0,                        }, /* 0x1fb */
    { "Indian/Reunion",                   "Mauritius Standard Time",         14, 23, "RE", "RE", 0x8000004f, 0,                        }, /* 0x1fc */
    { "Iran",                             NULL,                              4,  0,  "IR", "",   0x00000000, 0,                        }, /* 0x1fd */
    { "Israel",                           NULL,                              6,  0,  "IL", "",   0x00000000, 0,                        }, /* 0x1fe */
    { "Jamaica",                          NULL,                              7,  0,  "JM", "",   0x00000000, 0,                        }, /* 0x1ff */
    { "Japan",                            NULL,                              5,  0,  "JP", "",   0x00000000, 0,                        }, /* 0x200 */
    { "Kwajalein",                        NULL,                              9,  0,  "MH", "",   0x00000000, 0,                        }, /* 0x201 */
    { "Libya",                            NULL,                              5,  0,  "LY", "",   0x00000000, 0,                        }, /* 0x202 */
    { "MET",                              NULL,                              3,  0,  "ZZ", "",   0x00000000, 0,                        }, /* 0x203 */
    { "MST",                              NULL,                              3,  0,  "ZZ", "",   0x00000000, 0,                        }, /* 0x204 */
    { "MST7MDT",                          "Mountain Standard Time",          7,  22, "ZZ", "ZZ", 0x0000000a, 0,                        }, /* 0x205 */
    { "Mexico/BajaNorte",                 NULL,                              16, 0,  "MX", "",   0x00000000, 0,                        }, /* 0x206 */
    { "Mexico/BajaSur",                   NULL,                              14, 0,  "MX", "",   0x00000000, 0,                        }, /* 0x207 */
    { "Mexico/General",                   NULL,                              14, 0,  "MX", "",   0x00000000, 0,                        }, /* 0x208 */
    { "NZ",                               NULL,                              2,  0,  "NZ", "",   0x00000000, 0,                        }, /* 0x209 */
    { "NZ-CHAT",                          NULL,                              7,  0,  "NZ", "",   0x00000000, 0,                        }, /* 0x20a */
    { "Navajo",                           NULL,                              6,  0,  "US", "",   0x00000000, 0,                        }, /* 0x20b */
    { "PRC",                              NULL,                              3,  0,  "CN", "",   0x00000000, 0,                        }, /* 0x20c */
    { "PST8PDT",                          "Pacific Standard Time",           7,  21, "ZZ", "ZZ", 0x00000004, 0,                        }, /* 0x20d */
    { "Pacific/Apia",                     "Samoa Standard Time",             12, 19, "WS", "WS", 0x00000001, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x20e */
    { "Pacific/Auckland",                 "New Zealand Standard Time",       16, 25, "NZ", "NZ", 0x00000122, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x20f */
    { "Pacific/Bougainville",             "Bougainville Standard Time",      20, 26, "PG", "PG", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x210 */
    { "Pacific/Chatham",                  "Chatham Islands Standard Time",   15, 29, "NZ", "NZ", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x211 */
    { "Pacific/Chuuk",                    NULL,                              13, 0,  "FM", "",   0x00000000, 0,                        }, /* 0x212 */
    { "Pacific/Easter",                   "Easter Island Standard Time",     14, 27, "CL", "CL", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x213 */
    { "Pacific/Efate",                    "Central Pacific Standard Time",   13, 29, "VU", "VU", 0x00000118, 0,                        }, /* 0x214 */
    { "Pacific/Enderbury",                "Tonga Standard Time",             17, 19, "KI", "KI", 0x0000012c, 0,                        }, /* 0x215 */
    { "Pacific/Fakaofo",                  "Tonga Standard Time",             15, 19, "TK", "TK", 0x0000012c, 0,                        }, /* 0x216 */
    { "Pacific/Fiji",                     "Fiji Standard Time",              12, 18, "FJ", "FJ", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x217 */
    { "Pacific/Funafuti",                 "UTC+12",                          16, 6,  "TV", "TV", 0x00000000, 0,                        }, /* 0x218 */
    { "Pacific/Galapagos",                "Central America Standard Time",   17, 29, "EC", "EC", 0x00000021, 0,                        }, /* 0x219 */
    { "Pacific/Gambier",                  "UTC-09",                          15, 6,  "PF", "PF", 0x00000000, 0,                        }, /* 0x21a */
    { "Pacific/Guadalcanal",              "Central Pacific Standard Time",   19, 29, "SB", "SB", 0x00000118, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x21b */
    { "Pacific/Guam",                     "West Pacific Standard Time",      12, 26, "GU", "GU", 0x00000113, 0,                        }, /* 0x21c */
    { "Pacific/Honolulu",                 "Hawaiian Standard Time",          16, 22, "US", "US", 0x00000002, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x21d */
    { "Pacific/Johnston",                 "Hawaiian Standard Time",          16, 22, "US", "UM", 0x00000000, 0,                        }, /* 0x21e */
    { "Pacific/Kiritimati",               "Line Islands Standard Time",      18, 26, "KI", "KI", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x21f */
    { "Pacific/Kosrae",                   "Central Pacific Standard Time",   14, 29, "FM", "FM", 0x00000118, 0,                        }, /* 0x220 */
    { "Pacific/Kwajalein",                "UTC+12",                          17, 6,  "MH", "MH", 0x00000000, 0,                        }, /* 0x221 */
    { "Pacific/Majuro",                   "UTC+12",                          14, 6,  "MH", "MH", 0x00000000, 0,                        }, /* 0x222 */
    { "Pacific/Marquesas",                "Marquesas Standard Time",         17, 23, "PF", "PF", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x223 */
    { "Pacific/Midway",                   "UTC-11",                          14, 6,  "AS", "UM", 0x00000000, 0,                        }, /* 0x224 */
    { "Pacific/Nauru",                    "UTC+12",                          13, 6,  "NR", "NR", 0x00000000, 0,                        }, /* 0x225 */
    { "Pacific/Niue",                     "UTC-11",                          12, 6,  "NU", "NU", 0x00000000, 0,                        }, /* 0x226 */
    { "Pacific/Norfolk",                  "Norfolk Standard Time",           15, 21, "NF", "NF", 0x00000000, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x227 */
    { "Pacific/Noumea",                   "Central Pacific Standard Time",   14, 29, "NC", "NC", 0x00000118, 0,                        }, /* 0x228 */
    { "Pacific/Pago_Pago",                "UTC-11",                          17, 6,  "AS", "AS", 0x00000000, 0,                        }, /* 0x229 */
    { "Pacific/Palau",                    "Tokyo Standard Time",             13, 19, "PW", "PW", 0x000000eb, 0,                        }, /* 0x22a */
    { "Pacific/Pitcairn",                 "UTC-08",                          16, 6,  "PN", "PN", 0x00000000, 0,                        }, /* 0x22b */
    { "Pacific/Pohnpei",                  NULL,                              15, 0,  "FM", "",   0x00000000, 0,                        }, /* 0x22c */
    { "Pacific/Ponape",                   "Central Pacific Standard Time",   14, 29, "FM", "FM", 0x00000000, 0,                        }, /* 0x22d */
    { "Pacific/Port_Moresby",             "West Pacific Standard Time",      20, 26, "PG", "PG", 0x00000113, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x22e */
    { "Pacific/Rarotonga",                "Hawaiian Standard Time",          17, 22, "CK", "CK", 0x00000002, 0,                        }, /* 0x22f */
    { "Pacific/Saipan",                   "West Pacific Standard Time",      14, 26, "GU", "MP", 0x00000000, 0,                        }, /* 0x230 */
    { "Pacific/Samoa",                    NULL,                              13, 0,  "AS", "",   0x00000000, 0,                        }, /* 0x231 */
    { "Pacific/Tahiti",                   "Hawaiian Standard Time",          14, 22, "PF", "PF", 0x00000002, 0,                        }, /* 0x232 */
    { "Pacific/Tarawa",                   "UTC+12",                          14, 6,  "KI", "KI", 0x00000000, 0,                        }, /* 0x233 */
    { "Pacific/Tongatapu",                "Tonga Standard Time",             17, 19, "TO", "TO", 0x0000012c, RTTIMEZONEINFO_F_GOLDEN,  }, /* 0x234 */
    { "Pacific/Truk",                     "West Pacific Standard Time",      12, 26, "FM", "FM", 0x00000000, 0,                        }, /* 0x235 */
    { "Pacific/Wake",                     "UTC+12",                          12, 6,  "UM", "UM", 0x00000000, 0,                        }, /* 0x236 */
    { "Pacific/Wallis",                   "UTC+12",                          14, 6,  "WF", "WF", 0x00000000, 0,                        }, /* 0x237 */
    { "Pacific/Yap",                      NULL,                              11, 0,  "FM", "",   0x00000000, 0,                        }, /* 0x238 */
    { "Poland",                           NULL,                              6,  0,  "PL", "",   0x00000000, 0,                        }, /* 0x239 */
    { "Portugal",                         NULL,                              8,  0,  "PT", "",   0x00000000, 0,                        }, /* 0x23a */
    { "ROC",                              NULL,                              3,  0,  "TW", "",   0x00000000, 0,                        }, /* 0x23b */
    { "ROK",                              NULL,                              3,  0,  "KR", "",   0x00000000, 0,                        }, /* 0x23c */
    { "Singapore",                        NULL,                              9,  0,  "SG", "",   0x00000000, 0,                        }, /* 0x23d */
    { "Turkey",                           NULL,                              6,  0,  "TR", "",   0x00000000, 0,                        }, /* 0x23e */
    { "UCT",                              NULL,                              3,  0,  "ZZ", "",   0x00000000, 0,                        }, /* 0x23f */
    { "US/Alaska",                        NULL,                              9,  0,  "US", "",   0x00000000, 0,                        }, /* 0x240 */
    { "US/Aleutian",                      NULL,                              11, 0,  "US", "",   0x00000000, 0,                        }, /* 0x241 */
    { "US/Arizona",                       NULL,                              10, 0,  "US", "",   0x00000000, 0,                        }, /* 0x242 */
    { "US/Central",                       NULL,                              10, 0,  "US", "",   0x00000000, 0,                        }, /* 0x243 */
    { "US/East-Indiana",                  NULL,                              15, 0,  "US", "",   0x00000000, 0,                        }, /* 0x244 */
    { "US/Eastern",                       NULL,                              10, 0,  "US", "",   0x00000000, 0,                        }, /* 0x245 */
    { "US/Hawaii",                        NULL,                              9,  0,  "US", "",   0x00000000, 0,                        }, /* 0x246 */
    { "US/Indiana-Starke",                NULL,                              17, 0,  "US", "",   0x00000000, 0,                        }, /* 0x247 */
    { "US/Michigan",                      NULL,                              11, 0,  "US", "",   0x00000000, 0,                        }, /* 0x248 */
    { "US/Mountain",                      NULL,                              11, 0,  "US", "",   0x00000000, 0,                        }, /* 0x249 */
    { "US/Pacific",                       NULL,                              10, 0,  "US", "",   0x00000000, 0,                        }, /* 0x24a */
    { "US/Pacific-New",                   NULL,                              14, 0,  "US", "",   0x00000000, 0,                        }, /* 0x24b */
    { "US/Samoa",                         NULL,                              8,  0,  "AS", "",   0x00000000, 0,                        }, /* 0x24c */
    { "UTC",                              NULL,                              3,  0,  "ZZ", "",   0x00000000, 0,                        }, /* 0x24d */
    { "Universal",                        NULL,                              9,  0,  "ZZ", "",   0x00000000, 0,                        }, /* 0x24e */
    { "W-SU",                             NULL,                              4,  0,  "RU", "",   0x00000000, 0,                        }, /* 0x24f */
    { "WET",                              NULL,                              3,  0,  "ZZ", "",   0x00000000, 0,                        }, /* 0x250 */
    { "Zulu",                             NULL,                              4,  0,  "ZZ", "",   0x00000000, 0,                        }, /* 0x251 */
};

/**
 * Windows time zone lookup table.  Sorted by name, golden flag and territory.
 */
static const uint16_t g_aidxWinTimeZones[] =
{
    0x15d, /* AUS Central Standard Time       / AU+ ==>  Australia/Darwin */
    0x169, /* AUS Eastern Standard Time       / AU+ ==>  Australia/Sydney */
    0x163, /* AUS Eastern Standard Time       / AU  ==>  Australia/Melbourne */
    0x112, /* Afghanistan Standard Time       / AF+ ==>  Asia/Kabul */
    0x037, /* Alaskan Standard Time           / US+ ==>  America/Anchorage */
    0x08b, /* Alaskan Standard Time           / US  ==>  America/Juneau */
    0x09f, /* Alaskan Standard Time           / US  ==>  America/Metlakatla */
    0x0aa, /* Alaskan Standard Time           / US  ==>  America/Nome */
    0x0c8, /* Alaskan Standard Time           / US  ==>  America/Sitka */
    0x0da, /* Alaskan Standard Time           / US  ==>  America/Yakutat */
    0x036, /* Aleutian Standard Time          / US+ ==>  America/Adak */
    0x0f6, /* Altai Standard Time             / RU+ ==>  Asia/Barnaul */
    0x12f, /* Arab Standard Time              / SA+ ==>  Asia/Riyadh */
    0x0f3, /* Arab Standard Time              / BH  ==>  Asia/Bahrain */
    0x11d, /* Arab Standard Time              / KW  ==>  Asia/Kuwait */
    0x12c, /* Arab Standard Time              / QA  ==>  Asia/Qatar */
    0x0e9, /* Arab Standard Time              / YE  ==>  Asia/Aden */
    0x104, /* Arabian Standard Time           / AE+ ==>  Asia/Dubai */
    0x123, /* Arabian Standard Time           / OM  ==>  Asia/Muscat */
    0x19c, /* Arabian Standard Time           / ZZ  ==>  Etc/GMT-4 */
    0x0f2, /* Arabic Standard Time            / IQ+ ==>  Asia/Baghdad */
    0x055, /* Argentina Standard Time         / AR+ ==>  America/Buenos_Aires */
    0x040, /* Argentina Standard Time         / AR  ==>  America/Argentina/La_Rioja */
    0x042, /* Argentina Standard Time         / AR  ==>  America/Argentina/Rio_Gallegos */
    0x043, /* Argentina Standard Time         / AR  ==>  America/Argentina/Salta */
    0x044, /* Argentina Standard Time         / AR  ==>  America/Argentina/San_Juan */
    0x045, /* Argentina Standard Time         / AR  ==>  America/Argentina/San_Luis */
    0x046, /* Argentina Standard Time         / AR  ==>  America/Argentina/Tucuman */
    0x047, /* Argentina Standard Time         / AR  ==>  America/Argentina/Ushuaia */
    0x05a, /* Argentina Standard Time         / AR  ==>  America/Catamarca */
    0x060, /* Argentina Standard Time         / AR  ==>  America/Cordoba */
    0x08a, /* Argentina Standard Time         / AR  ==>  America/Jujuy */
    0x09c, /* Argentina Standard Time         / AR  ==>  America/Mendoza */
    0x1aa, /* Astrakhan Standard Time         / RU+ ==>  Europe/Astrakhan */
    0x1d4, /* Astrakhan Standard Time         / RU  ==>  Europe/Saratov */
    0x1dc, /* Astrakhan Standard Time         / RU  ==>  Europe/Ulyanovsk */
    0x07b, /* Atlantic Standard Time          / CA+ ==>  America/Halifax */
    0x14c, /* Atlantic Standard Time          / BM  ==>  Atlantic/Bermuda */
    0x072, /* Atlantic Standard Time          / CA  ==>  America/Glace_Bay */
    0x074, /* Atlantic Standard Time          / CA  ==>  America/Goose_Bay */
    0x0a2, /* Atlantic Standard Time          / CA  ==>  America/Moncton */
    0x0d1, /* Atlantic Standard Time          / GL  ==>  America/Thule */
    0x15e, /* Aus Central W. Standard Time    / AU+ ==>  Australia/Eucla */
    0x0f4, /* Azerbaijan Standard Time        / AZ+ ==>  Asia/Baku */
    0x14b, /* Azores Standard Time            / PT+ ==>  Atlantic/Azores */
    0x0c6, /* Azores Standard Time            / GL  ==>  America/Scoresbysund */
    0x04c, /* Bahia Standard Time             / BR+ ==>  America/Bahia */
    0x102, /* Bangladesh Standard Time        / BD+ ==>  Asia/Dhaka */
    0x13d, /* Bangladesh Standard Time        / BT  ==>  Asia/Thimphu */
    0x1c7, /* Belarus Standard Time           / BY+ ==>  Europe/Minsk */
    0x210, /* Bougainville Standard Time      / PG+ ==>  Pacific/Bougainville */
    0x0bd, /* Canada Central Standard Time    / CA+ ==>  America/Regina */
    0x0cf, /* Canada Central Standard Time    / CA  ==>  America/Swift_Current */
    0x14e, /* Cape Verde Standard Time        / CV+ ==>  Atlantic/Cape_Verde */
    0x187, /* Cape Verde Standard Time        / ZZ  ==>  Etc/GMT+1 */
    0x14a, /* Caucasus Standard Time          / AM+ ==>  Asia/Yerevan */
    0x158, /* Cen. Australia Standard Time    / AU+ ==>  Australia/Adelaide */
    0x15a, /* Cen. Australia Standard Time    / AU  ==>  Australia/Broken_Hill */
    0x078, /* Central America Standard Time   / GT+ ==>  America/Guatemala */
    0x050, /* Central America Standard Time   / BZ  ==>  America/Belize */
    0x061, /* Central America Standard Time   / CR  ==>  America/Costa_Rica */
    0x219, /* Central America Standard Time   / EC  ==>  Pacific/Galapagos */
    0x0d0, /* Central America Standard Time   / HN  ==>  America/Tegucigalpa */
    0x096, /* Central America Standard Time   / NI  ==>  America/Managua */
    0x06d, /* Central America Standard Time   / SV  ==>  America/El_Salvador */
    0x18f, /* Central America Standard Time   / ZZ  ==>  Etc/GMT+6 */
    0x0ea, /* Central Asia Standard Time      / KZ+ ==>  Asia/Almaty */
    0x0e7, /* Central Asia Standard Time      / AQ  ==>  Antarctica/Vostok */
    0x143, /* Central Asia Standard Time      / CN  ==>  Asia/Urumqi */
    0x1f3, /* Central Asia Standard Time      / IO  ==>  Indian/Chagos */
    0x0f8, /* Central Asia Standard Time      / KG  ==>  Asia/Bishkek */
    0x12d, /* Central Asia Standard Time      / KZ  ==>  Asia/Qyzylorda */
    0x19e, /* Central Asia Standard Time      / ZZ  ==>  Etc/GMT-6 */
    0x063, /* Central Brazilian Standard Time / BR+ ==>  America/Cuiaba */
    0x057, /* Central Brazilian Standard Time / BR  ==>  America/Campo_Grande */
    0x1b2, /* Central Europe Standard Time    / HU+ ==>  Europe/Budapest */
    0x1da, /* Central Europe Standard Time    / AL  ==>  Europe/Tirane */
    0x1ce, /* Central Europe Standard Time    / CZ  ==>  Europe/Prague */
    0x1cd, /* Central Europe Standard Time    / ME  ==>  Europe/Podgorica */
    0x1ad, /* Central Europe Standard Time    / RS  ==>  Europe/Belgrade */
    0x1c1, /* Central Europe Standard Time    / SI  ==>  Europe/Ljubljana */
    0x1af, /* Central Europe Standard Time    / SK  ==>  Europe/Bratislava */
    0x1e3, /* Central European Standard Time  / PL+ ==>  Europe/Warsaw */
    0x1d3, /* Central European Standard Time  / BA  ==>  Europe/Sarajevo */
    0x1e4, /* Central European Standard Time  / HR  ==>  Europe/Zagreb */
    0x1d6, /* Central European Standard Time  / MK  ==>  Europe/Skopje */
    0x21b, /* Central Pacific Standard Time   / SB+ ==>  Pacific/Guadalcanal */
    0x0dc, /* Central Pacific Standard Time   / AQ  ==>  Antarctica/Casey */
    0x0df, /* Central Pacific Standard Time   / AU  ==>  Antarctica/Macquarie */
    0x220, /* Central Pacific Standard Time   / FM  ==>  Pacific/Kosrae */
    0x22d, /* Central Pacific Standard Time   / FM  ==>  Pacific/Ponape */
    0x228, /* Central Pacific Standard Time   / NC  ==>  Pacific/Noumea */
    0x214, /* Central Pacific Standard Time   / VU  ==>  Pacific/Efate */
    0x196, /* Central Pacific Standard Time   / ZZ  ==>  Etc/GMT-11 */
    0x0a0, /* Central Standard Time (Mexico)  / MX+ ==>  America/Mexico_City */
    0x04d, /* Central Standard Time (Mexico)  / MX  ==>  America/Bahia_Banderas */
    0x09e, /* Central Standard Time (Mexico)  / MX  ==>  America/Merida */
    0x0a3, /* Central Standard Time (Mexico)  / MX  ==>  America/Monterrey */
    0x05d, /* Central Standard Time           / US+ ==>  America/Chicago */
    0x0ba, /* Central Standard Time           / CA  ==>  America/Rainy_River */
    0x0bb, /* Central Standard Time           / CA  ==>  America/Rankin_Inlet */
    0x0be, /* Central Standard Time           / CA  ==>  America/Resolute */
    0x0d9, /* Central Standard Time           / CA  ==>  America/Winnipeg */
    0x09a, /* Central Standard Time           / MX  ==>  America/Matamoros */
    0x07f, /* Central Standard Time           / US  ==>  America/Indiana/Knox */
    0x082, /* Central Standard Time           / US  ==>  America/Indiana/Tell_City */
    0x09d, /* Central Standard Time           / US  ==>  America/Menominee */
    0x0ac, /* Central Standard Time           / US  ==>  America/North_Dakota/Beulah */
    0x0ad, /* Central Standard Time           / US  ==>  America/North_Dakota/Center */
    0x0ae, /* Central Standard Time           / US  ==>  America/North_Dakota/New_Salem */
    0x173, /* Central Standard Time           / ZZ  ==>  CST6CDT */
    0x211, /* Chatham Islands Standard Time   / NZ+ ==>  Pacific/Chatham */
    0x134, /* China Standard Time             / CN+ ==>  Asia/Shanghai */
    0x10b, /* China Standard Time             / HK  ==>  Asia/Hong_Kong */
    0x11f, /* China Standard Time             / MO  ==>  Asia/Macau */
    0x07c, /* Cuba Standard Time              / CU+ ==>  America/Havana */
    0x18a, /* Dateline Standard Time          / ZZ+ ==>  Etc/GMT+12 */
    0x02b, /* E. Africa Standard Time         / KE+ ==>  Africa/Nairobi */
    0x0e5, /* E. Africa Standard Time         / AQ  ==>  Antarctica/Syowa */
    0x013, /* E. Africa Standard Time         / DJ  ==>  Africa/Djibouti */
    0x005, /* E. Africa Standard Time         / ER  ==>  Africa/Asmera */
    0x002, /* E. Africa Standard Time         / ET  ==>  Africa/Addis_Ababa */
    0x1f6, /* E. Africa Standard Time         / KM  ==>  Indian/Comoro */
    0x1f2, /* E. Africa Standard Time         / MG  ==>  Indian/Antananarivo */
    0x01c, /* E. Africa Standard Time         / SD  ==>  Africa/Khartoum */
    0x029, /* E. Africa Standard Time         / SO  ==>  Africa/Mogadishu */
    0x01a, /* E. Africa Standard Time         / SS  ==>  Africa/Juba */
    0x012, /* E. Africa Standard Time         / TZ  ==>  Africa/Dar_es_Salaam */
    0x01b, /* E. Africa Standard Time         / UG  ==>  Africa/Kampala */
    0x1fb, /* E. Africa Standard Time         / YT  ==>  Indian/Mayotte */
    0x19b, /* E. Africa Standard Time         / ZZ  ==>  Etc/GMT-3 */
    0x159, /* E. Australia Standard Time      / AU+ ==>  Australia/Brisbane */
    0x161, /* E. Australia Standard Time      / AU  ==>  Australia/Lindeman */
    0x1b4, /* E. Europe Standard Time         / MD+ ==>  Europe/Chisinau */
    0x0c5, /* E. South America Standard Time  / BR+ ==>  America/Sao_Paulo */
    0x213, /* Easter Island Standard Time     / CL+ ==>  Pacific/Easter */
    0x058, /* Eastern Standard Time (Mexico)  / MX+ ==>  America/Cancun */
    0x0a8, /* Eastern Standard Time           / US+ ==>  America/New_York */
    0x0a7, /* Eastern Standard Time           / BS  ==>  America/Nassau */
    0x088, /* Eastern Standard Time           / CA  ==>  America/Iqaluit */
    0x0a5, /* Eastern Standard Time           / CA  ==>  America/Montreal */
    0x0a9, /* Eastern Standard Time           / CA  ==>  America/Nipigon */
    0x0b1, /* Eastern Standard Time           / CA  ==>  America/Pangnirtung */
    0x0d2, /* Eastern Standard Time           / CA  ==>  America/Thunder_Bay */
    0x0d4, /* Eastern Standard Time           / CA  ==>  America/Toronto */
    0x069, /* Eastern Standard Time           / US  ==>  America/Detroit */
    0x081, /* Eastern Standard Time           / US  ==>  America/Indiana/Petersburg */
    0x084, /* Eastern Standard Time           / US  ==>  America/Indiana/Vincennes */
    0x085, /* Eastern Standard Time           / US  ==>  America/Indiana/Winamac */
    0x08d, /* Eastern Standard Time           / US  ==>  America/Kentucky/Monticello */
    0x093, /* Eastern Standard Time           / US  ==>  America/Louisville */
    0x182, /* Eastern Standard Time           / ZZ  ==>  EST5EDT */
    0x00d, /* Egypt Standard Time             / EG+ ==>  Africa/Cairo */
    0x149, /* Ekaterinburg Standard Time      / RU+ ==>  Asia/Yekaterinburg */
    0x1be, /* FLE Standard Time               / UA+ ==>  Europe/Kiev */
    0x1c6, /* FLE Standard Time               / AX  ==>  Europe/Mariehamn */
    0x1d7, /* FLE Standard Time               / BG  ==>  Europe/Sofia */
    0x1d9, /* FLE Standard Time               / EE  ==>  Europe/Tallinn */
    0x1b9, /* FLE Standard Time               / FI  ==>  Europe/Helsinki */
    0x1e1, /* FLE Standard Time               / LT  ==>  Europe/Vilnius */
    0x1cf, /* FLE Standard Time               / LV  ==>  Europe/Riga */
    0x1dd, /* FLE Standard Time               / UA  ==>  Europe/Uzhgorod */
    0x1e5, /* FLE Standard Time               / UA  ==>  Europe/Zaporozhye */
    0x217, /* Fiji Standard Time              / FJ+ ==>  Pacific/Fiji */
    0x1c2, /* GMT Standard Time               / GB+ ==>  Europe/London */
    0x14d, /* GMT Standard Time               / ES  ==>  Atlantic/Canary */
    0x14f, /* GMT Standard Time               / FO  ==>  Atlantic/Faeroe */
    0x1b8, /* GMT Standard Time               / GG  ==>  Europe/Guernsey */
    0x1b6, /* GMT Standard Time               / IE  ==>  Europe/Dublin */
    0x1ba, /* GMT Standard Time               / IM  ==>  Europe/Isle_of_Man */
    0x1bc, /* GMT Standard Time               / JE  ==>  Europe/Jersey */
    0x152, /* GMT Standard Time               / PT  ==>  Atlantic/Madeira */
    0x1c0, /* GMT Standard Time               / PT  ==>  Europe/Lisbon */
    0x1b1, /* GTB Standard Time               / RO+ ==>  Europe/Bucharest */
    0x124, /* GTB Standard Time               / CY  ==>  Asia/Nicosia */
    0x1ab, /* GTB Standard Time               / GR  ==>  Europe/Athens */
    0x139, /* Georgian Standard Time          / GE+ ==>  Asia/Tbilisi */
    0x073, /* Greenland Standard Time         / GL+ ==>  America/Godthab */
    0x153, /* Greenwich Standard Time         / IS+ ==>  Atlantic/Reykjavik */
    0x02f, /* Greenwich Standard Time         / BF  ==>  Africa/Ouagadougou */
    0x000, /* Greenwich Standard Time         / CI  ==>  Africa/Abidjan */
    0x001, /* Greenwich Standard Time         / GH  ==>  Africa/Accra */
    0x008, /* Greenwich Standard Time         / GM  ==>  Africa/Banjul */
    0x010, /* Greenwich Standard Time         / GN  ==>  Africa/Conakry */
    0x009, /* Greenwich Standard Time         / GW  ==>  Africa/Bissau */
    0x02a, /* Greenwich Standard Time         / LR  ==>  Africa/Monrovia */
    0x006, /* Greenwich Standard Time         / ML  ==>  Africa/Bamako */
    0x02e, /* Greenwich Standard Time         / MR  ==>  Africa/Nouakchott */
    0x155, /* Greenwich Standard Time         / SH  ==>  Atlantic/St_Helena */
    0x016, /* Greenwich Standard Time         / SL  ==>  Africa/Freetown */
    0x011, /* Greenwich Standard Time         / SN  ==>  Africa/Dakar */
    0x031, /* Greenwich Standard Time         / ST  ==>  Africa/Sao_Tome */
    0x021, /* Greenwich Standard Time         / TG  ==>  Africa/Lome */
    0x0b4, /* Haiti Standard Time             / HT+ ==>  America/Port-au-Prince */
    0x21d, /* Hawaiian Standard Time          / US+ ==>  Pacific/Honolulu */
    0x22f, /* Hawaiian Standard Time          / CK  ==>  Pacific/Rarotonga */
    0x232, /* Hawaiian Standard Time          / PF  ==>  Pacific/Tahiti */
    0x21e, /* Hawaiian Standard Time          / UM  ==>  Pacific/Johnston */
    0x188, /* Hawaiian Standard Time          / ZZ  ==>  Etc/GMT+10 */
    0x0fa, /* India Standard Time             / IN+ ==>  Asia/Calcutta */
    0x13a, /* Iran Standard Time              / IR+ ==>  Asia/Tehran */
    0x111, /* Israel Standard Time            / IL+ ==>  Asia/Jerusalem */
    0x0eb, /* Jordan Standard Time            / JO+ ==>  Asia/Amman */
    0x1bd, /* Kaliningrad Standard Time       / RU+ ==>  Europe/Kaliningrad */
    0x133, /* Korea Standard Time             / KR+ ==>  Asia/Seoul */
    0x033, /* Libya Standard Time             / LY+ ==>  Africa/Tripoli */
    0x21f, /* Line Islands Standard Time      / KI+ ==>  Pacific/Kiritimati */
    0x199, /* Line Islands Standard Time      / ZZ  ==>  Etc/GMT-14 */
    0x162, /* Lord Howe Standard Time         / AU+ ==>  Australia/Lord_Howe */
    0x120, /* Magadan Standard Time           / RU+ ==>  Asia/Magadan */
    0x223, /* Marquesas Standard Time         / PF+ ==>  Pacific/Marquesas */
    0x1fa, /* Mauritius Standard Time         / MU+ ==>  Indian/Mauritius */
    0x1fc, /* Mauritius Standard Time         / RE  ==>  Indian/Reunion */
    0x1f8, /* Mauritius Standard Time         / SC  ==>  Indian/Mahe */
    0x0f7, /* Middle East Standard Time       / LB+ ==>  Asia/Beirut */
    0x0a4, /* Montevideo Standard Time        / UY+ ==>  America/Montevideo */
    0x00e, /* Morocco Standard Time           / MA+ ==>  Africa/Casablanca */
    0x015, /* Morocco Standard Time           / EH  ==>  Africa/El_Aaiun */
    0x05e, /* Mountain Standard Time (Mexico) / MX+ ==>  America/Chihuahua */
    0x09b, /* Mountain Standard Time (Mexico) / MX  ==>  America/Mazatlan */
    0x068, /* Mountain Standard Time          / US+ ==>  America/Denver */
    0x056, /* Mountain Standard Time          / CA  ==>  America/Cambridge_Bay */
    0x06b, /* Mountain Standard Time          / CA  ==>  America/Edmonton */
    0x087, /* Mountain Standard Time          / CA  ==>  America/Inuvik */
    0x0db, /* Mountain Standard Time          / CA  ==>  America/Yellowknife */
    0x0af, /* Mountain Standard Time          / MX  ==>  America/Ojinaga */
    0x054, /* Mountain Standard Time          / US  ==>  America/Boise */
    0x205, /* Mountain Standard Time          / ZZ  ==>  MST7MDT */
    0x12e, /* Myanmar Standard Time           / MM+ ==>  Asia/Rangoon */
    0x1f5, /* Myanmar Standard Time           / CC  ==>  Indian/Cocos */
    0x126, /* N. Central Asia Standard Time   / RU+ ==>  Asia/Novosibirsk */
    0x035, /* Namibia Standard Time           / NA+ ==>  Africa/Windhoek */
    0x117, /* Nepal Standard Time             / NP+ ==>  Asia/Katmandu */
    0x20f, /* New Zealand Standard Time       / NZ+ ==>  Pacific/Auckland */
    0x0e1, /* New Zealand Standard Time       / AQ  ==>  Antarctica/McMurdo */
    0x0ca, /* Newfoundland Standard Time      / CA+ ==>  America/St_Johns */
    0x227, /* Norfolk Standard Time           / NF+ ==>  Pacific/Norfolk */
    0x10d, /* North Asia East Standard Time   / RU+ ==>  Asia/Irkutsk */
    0x11a, /* North Asia Standard Time        / RU+ ==>  Asia/Krasnoyarsk */
    0x125, /* North Asia Standard Time        / RU  ==>  Asia/Novokuznetsk */
    0x12b, /* North Korea Standard Time       / KP+ ==>  Asia/Pyongyang */
    0x127, /* Omsk Standard Time              / RU+ ==>  Asia/Omsk */
    0x0c3, /* Pacific SA Standard Time        / CL+ ==>  America/Santiago */
    0x0d3, /* Pacific Standard Time (Mexico)  / MX+ ==>  America/Tijuana */
    0x0c1, /* Pacific Standard Time (Mexico)  / MX  ==>  America/Santa_Isabel */
    0x092, /* Pacific Standard Time           / US+ ==>  America/Los_Angeles */
    0x066, /* Pacific Standard Time           / CA  ==>  America/Dawson */
    0x0d6, /* Pacific Standard Time           / CA  ==>  America/Vancouver */
    0x0d8, /* Pacific Standard Time           / CA  ==>  America/Whitehorse */
    0x20d, /* Pacific Standard Time           / ZZ  ==>  PST8PDT */
    0x114, /* Pakistan Standard Time          / PK+ ==>  Asia/Karachi */
    0x049, /* Paraguay Standard Time          / PY+ ==>  America/Asuncion */
    0x1cc, /* Romance Standard Time           / FR+ ==>  Europe/Paris */
    0x1b0, /* Romance Standard Time           / BE  ==>  Europe/Brussels */
    0x1b5, /* Romance Standard Time           / DK  ==>  Europe/Copenhagen */
    0x00f, /* Romance Standard Time           / ES  ==>  Africa/Ceuta */
    0x1c4, /* Romance Standard Time           / ES  ==>  Europe/Madrid */
    0x136, /* Russia Time Zone 10             / RU+ ==>  Asia/Srednekolymsk */
    0x113, /* Russia Time Zone 11             / RU+ ==>  Asia/Kamchatka */
    0x0ec, /* Russia Time Zone 11             / RU  ==>  Asia/Anadyr */
    0x1d1, /* Russia Time Zone 3              / RU+ ==>  Europe/Samara */
    0x1c9, /* Russian Standard Time           / RU+ ==>  Europe/Moscow */
    0x1bf, /* Russian Standard Time           / RU  ==>  Europe/Kirov */
    0x1e2, /* Russian Standard Time           / RU  ==>  Europe/Volgograd */
    0x1d5, /* Russian Standard Time           / UA  ==>  Europe/Simferopol */
    0x05b, /* SA Eastern Standard Time        / GF+ ==>  America/Cayenne */
    0x0e2, /* SA Eastern Standard Time        / AQ  ==>  Antarctica/Palmer */
    0x0e3, /* SA Eastern Standard Time        / AQ  ==>  Antarctica/Rothera */
    0x04f, /* SA Eastern Standard Time        / BR  ==>  America/Belem */
    0x071, /* SA Eastern Standard Time        / BR  ==>  America/Fortaleza */
    0x095, /* SA Eastern Standard Time        / BR  ==>  America/Maceio */
    0x0bc, /* SA Eastern Standard Time        / BR  ==>  America/Recife */
    0x0c2, /* SA Eastern Standard Time        / BR  ==>  America/Santarem */
    0x0b9, /* SA Eastern Standard Time        / CL  ==>  America/Punta_Arenas */
    0x156, /* SA Eastern Standard Time        / FK  ==>  Atlantic/Stanley */
    0x0b2, /* SA Eastern Standard Time        / SR  ==>  America/Paramaribo */
    0x18c, /* SA Eastern Standard Time        / ZZ  ==>  Etc/GMT+3 */
    0x053, /* SA Pacific Standard Time        / CO+ ==>  America/Bogota */
    0x06c, /* SA Pacific Standard Time        / BR  ==>  America/Eirunepe */
    0x0bf, /* SA Pacific Standard Time        / BR  ==>  America/Rio_Branco */
    0x05f, /* SA Pacific Standard Time        / CA  ==>  America/Coral_Harbour */
    0x079, /* SA Pacific Standard Time        / EC  ==>  America/Guayaquil */
    0x089, /* SA Pacific Standard Time        / JM  ==>  America/Jamaica */
    0x05c, /* SA Pacific Standard Time        / KY  ==>  America/Cayman */
    0x0b0, /* SA Pacific Standard Time        / PA  ==>  America/Panama */
    0x091, /* SA Pacific Standard Time        / PE  ==>  America/Lima */
    0x18e, /* SA Pacific Standard Time        / ZZ  ==>  Etc/GMT+5 */
    0x090, /* SA Western Standard Time        / BO+ ==>  America/La_Paz */
    0x039, /* SA Western Standard Time        / AG  ==>  America/Antigua */
    0x038, /* SA Western Standard Time        / AI  ==>  America/Anguilla */
    0x048, /* SA Western Standard Time        / AW  ==>  America/Aruba */
    0x04e, /* SA Western Standard Time        / BB  ==>  America/Barbados */
    0x0c9, /* SA Western Standard Time        / BL  ==>  America/St_Barthelemy */
    0x08f, /* SA Western Standard Time        / BQ  ==>  America/Kralendijk */
    0x052, /* SA Western Standard Time        / BR  ==>  America/Boa_Vista */
    0x097, /* SA Western Standard Time        / BR  ==>  America/Manaus */
    0x0b7, /* SA Western Standard Time        / BR  ==>  America/Porto_Velho */
    0x051, /* SA Western Standard Time        / CA  ==>  America/Blanc-Sablon */
    0x064, /* SA Western Standard Time        / CW  ==>  America/Curacao */
    0x06a, /* SA Western Standard Time        / DM  ==>  America/Dominica */
    0x0c4, /* SA Western Standard Time        / DO  ==>  America/Santo_Domingo */
    0x076, /* SA Western Standard Time        / GD  ==>  America/Grenada */
    0x077, /* SA Western Standard Time        / GP  ==>  America/Guadeloupe */
    0x07a, /* SA Western Standard Time        / GY  ==>  America/Guyana */
    0x0cb, /* SA Western Standard Time        / KN  ==>  America/St_Kitts */
    0x0cc, /* SA Western Standard Time        / LC  ==>  America/St_Lucia */
    0x098, /* SA Western Standard Time        / MF  ==>  America/Marigot */
    0x099, /* SA Western Standard Time        / MQ  ==>  America/Martinique */
    0x0a6, /* SA Western Standard Time        / MS  ==>  America/Montserrat */
    0x0b8, /* SA Western Standard Time        / PR  ==>  America/Puerto_Rico */
    0x094, /* SA Western Standard Time        / SX  ==>  America/Lower_Princes */
    0x0b5, /* SA Western Standard Time        / TT  ==>  America/Port_of_Spain */
    0x0ce, /* SA Western Standard Time        / VC  ==>  America/St_Vincent */
    0x0d5, /* SA Western Standard Time        / VG  ==>  America/Tortola */
    0x0cd, /* SA Western Standard Time        / VI  ==>  America/St_Thomas */
    0x18d, /* SA Western Standard Time        / ZZ  ==>  Etc/GMT+4 */
    0x0f5, /* SE Asia Standard Time           / TH+ ==>  Asia/Bangkok */
    0x0dd, /* SE Asia Standard Time           / AQ  ==>  Antarctica/Davis */
    0x1f4, /* SE Asia Standard Time           / CX  ==>  Indian/Christmas */
    0x10f, /* SE Asia Standard Time           / ID  ==>  Asia/Jakarta */
    0x12a, /* SE Asia Standard Time           / ID  ==>  Asia/Pontianak */
    0x129, /* SE Asia Standard Time           / KH  ==>  Asia/Phnom_Penh */
    0x145, /* SE Asia Standard Time           / LA  ==>  Asia/Vientiane */
    0x130, /* SE Asia Standard Time           / VN  ==>  Asia/Saigon */
    0x19f, /* SE Asia Standard Time           / ZZ  ==>  Etc/GMT-7 */
    0x0a1, /* Saint Pierre Standard Time      / PM+ ==>  America/Miquelon */
    0x131, /* Sakhalin Standard Time          / RU+ ==>  Asia/Sakhalin */
    0x20e, /* Samoa Standard Time             / WS+ ==>  Pacific/Apia */
    0x135, /* Singapore Standard Time         / SG+ ==>  Asia/Singapore */
    0x0f9, /* Singapore Standard Time         / BN  ==>  Asia/Brunei */
    0x121, /* Singapore Standard Time         / ID  ==>  Asia/Makassar */
    0x11b, /* Singapore Standard Time         / MY  ==>  Asia/Kuala_Lumpur */
    0x11c, /* Singapore Standard Time         / MY  ==>  Asia/Kuching */
    0x122, /* Singapore Standard Time         / PH  ==>  Asia/Manila */
    0x1a0, /* Singapore Standard Time         / ZZ  ==>  Etc/GMT-8 */
    0x019, /* South Africa Standard Time      / ZA+ ==>  Africa/Johannesburg */
    0x00c, /* South Africa Standard Time      / BI  ==>  Africa/Bujumbura */
    0x017, /* South Africa Standard Time      / BW  ==>  Africa/Gaborone */
    0x023, /* South Africa Standard Time      / CD  ==>  Africa/Lubumbashi */
    0x027, /* South Africa Standard Time      / LS  ==>  Africa/Maseru */
    0x00a, /* South Africa Standard Time      / MW  ==>  Africa/Blantyre */
    0x026, /* South Africa Standard Time      / MZ  ==>  Africa/Maputo */
    0x01d, /* South Africa Standard Time      / RW  ==>  Africa/Kigali */
    0x028, /* South Africa Standard Time      / SZ  ==>  Africa/Mbabane */
    0x024, /* South Africa Standard Time      / ZM  ==>  Africa/Lusaka */
    0x018, /* South Africa Standard Time      / ZW  ==>  Africa/Harare */
    0x19a, /* South Africa Standard Time      / ZZ  ==>  Etc/GMT-2 */
    0x0ff, /* Sri Lanka Standard Time         / LK+ ==>  Asia/Colombo */
    0x101, /* Syria Standard Time             / SY+ ==>  Asia/Damascus */
    0x137, /* Taipei Standard Time            / TW+ ==>  Asia/Taipei */
    0x15f, /* Tasmania Standard Time          / AU+ ==>  Australia/Hobart */
    0x15c, /* Tasmania Standard Time          / AU  ==>  Australia/Currie */
    0x03a, /* Tocantins Standard Time         / BR+ ==>  America/Araguaina */
    0x13e, /* Tokyo Standard Time             / JP+ ==>  Asia/Tokyo */
    0x110, /* Tokyo Standard Time             / ID  ==>  Asia/Jayapura */
    0x22a, /* Tokyo Standard Time             / PW  ==>  Pacific/Palau */
    0x103, /* Tokyo Standard Time             / TL  ==>  Asia/Dili */
    0x1a1, /* Tokyo Standard Time             / ZZ  ==>  Etc/GMT-9 */
    0x13f, /* Tomsk Standard Time             / RU+ ==>  Asia/Tomsk */
    0x234, /* Tonga Standard Time             / TO+ ==>  Pacific/Tongatapu */
    0x215, /* Tonga Standard Time             / KI  ==>  Pacific/Enderbury */
    0x216, /* Tonga Standard Time             / TK  ==>  Pacific/Fakaofo */
    0x198, /* Tonga Standard Time             / ZZ  ==>  Etc/GMT-13 */
    0x0fb, /* Transbaikal Standard Time       / RU+ ==>  Asia/Chita */
    0x1bb, /* Turkey Standard Time            / TR+ ==>  Europe/Istanbul */
    0x106, /* Turkey Standard Time            / CY  ==>  Asia/Famagusta */
    0x075, /* Turks And Caicos Standard Time  / TC+ ==>  America/Grand_Turk */
    0x086, /* US Eastern Standard Time        / US+ ==>  America/Indianapolis */
    0x080, /* US Eastern Standard Time        / US  ==>  America/Indiana/Marengo */
    0x083, /* US Eastern Standard Time        / US  ==>  America/Indiana/Vevay */
    0x0b3, /* US Mountain Standard Time       / US+ ==>  America/Phoenix */
    0x062, /* US Mountain Standard Time       / CA  ==>  America/Creston */
    0x067, /* US Mountain Standard Time       / CA  ==>  America/Dawson_Creek */
    0x06f, /* US Mountain Standard Time       / CA  ==>  America/Fort_Nelson */
    0x07d, /* US Mountain Standard Time       / MX  ==>  America/Hermosillo */
    0x190, /* US Mountain Standard Time       / ZZ  ==>  Etc/GMT+7 */
    0x185, /* UTC                             / ZZ+ ==>  Etc/GMT */
    0x065, /* UTC                             / GL  ==>  America/Danmarkshavn */
    0x1a5, /* UTC                             / ZZ  ==>  Etc/UTC */
    0x197, /* UTC+12                          / ZZ+ ==>  Etc/GMT-12 */
    0x233, /* UTC+12                          / KI  ==>  Pacific/Tarawa */
    0x221, /* UTC+12                          / MH  ==>  Pacific/Kwajalein */
    0x222, /* UTC+12                          / MH  ==>  Pacific/Majuro */
    0x225, /* UTC+12                          / NR  ==>  Pacific/Nauru */
    0x218, /* UTC+12                          / TV  ==>  Pacific/Funafuti */
    0x236, /* UTC+12                          / UM  ==>  Pacific/Wake */
    0x237, /* UTC+12                          / WF  ==>  Pacific/Wallis */
    0x18b, /* UTC-02                          / ZZ+ ==>  Etc/GMT+2 */
    0x0ab, /* UTC-02                          / BR  ==>  America/Noronha */
    0x154, /* UTC-02                          / GS  ==>  Atlantic/South_Georgia */
    0x191, /* UTC-08                          / ZZ+ ==>  Etc/GMT+8 */
    0x22b, /* UTC-08                          / PN  ==>  Pacific/Pitcairn */
    0x192, /* UTC-09                          / ZZ+ ==>  Etc/GMT+9 */
    0x21a, /* UTC-09                          / PF  ==>  Pacific/Gambier */
    0x189, /* UTC-11                          / ZZ+ ==>  Etc/GMT+11 */
    0x229, /* UTC-11                          / AS  ==>  Pacific/Pago_Pago */
    0x226, /* UTC-11                          / NU  ==>  Pacific/Niue */
    0x224, /* UTC-11                          / UM  ==>  Pacific/Midway */
    0x141, /* Ulaanbaatar Standard Time       / MN+ ==>  Asia/Ulaanbaatar */
    0x0fc, /* Ulaanbaatar Standard Time       / MN  ==>  Asia/Choibalsan */
    0x059, /* Venezuela Standard Time         / VE+ ==>  America/Caracas */
    0x146, /* Vladivostok Standard Time       / RU+ ==>  Asia/Vladivostok */
    0x144, /* Vladivostok Standard Time       / RU  ==>  Asia/Ust-Nera */
    0x166, /* W. Australia Standard Time      / AU+ ==>  Australia/Perth */
    0x01f, /* W. Central Africa Standard Time / NG+ ==>  Africa/Lagos */
    0x022, /* W. Central Africa Standard Time / AO  ==>  Africa/Luanda */
    0x030, /* W. Central Africa Standard Time / BJ  ==>  Africa/Porto-Novo */
    0x01e, /* W. Central Africa Standard Time / CD  ==>  Africa/Kinshasa */
    0x007, /* W. Central Africa Standard Time / CF  ==>  Africa/Bangui */
    0x00b, /* W. Central Africa Standard Time / CG  ==>  Africa/Brazzaville */
    0x014, /* W. Central Africa Standard Time / CM  ==>  Africa/Douala */
    0x003, /* W. Central Africa Standard Time / DZ  ==>  Africa/Algiers */
    0x020, /* W. Central Africa Standard Time / GA  ==>  Africa/Libreville */
    0x025, /* W. Central Africa Standard Time / GQ  ==>  Africa/Malabo */
    0x02d, /* W. Central Africa Standard Time / NE  ==>  Africa/Niamey */
    0x02c, /* W. Central Africa Standard Time / TD  ==>  Africa/Ndjamena */
    0x034, /* W. Central Africa Standard Time / TN  ==>  Africa/Tunis */
    0x194, /* W. Central Africa Standard Time / ZZ  ==>  Etc/GMT-1 */
    0x1ae, /* W. Europe Standard Time         / DE+ ==>  Europe/Berlin */
    0x1a9, /* W. Europe Standard Time         / AD  ==>  Europe/Andorra */
    0x1e0, /* W. Europe Standard Time         / AT  ==>  Europe/Vienna */
    0x1e6, /* W. Europe Standard Time         / CH  ==>  Europe/Zurich */
    0x1b3, /* W. Europe Standard Time         / DE  ==>  Europe/Busingen */
    0x1b7, /* W. Europe Standard Time         / GI  ==>  Europe/Gibraltar */
    0x1d0, /* W. Europe Standard Time         / IT  ==>  Europe/Rome */
    0x1de, /* W. Europe Standard Time         / LI  ==>  Europe/Vaduz */
    0x1c3, /* W. Europe Standard Time         / LU  ==>  Europe/Luxembourg */
    0x1c8, /* W. Europe Standard Time         / MC  ==>  Europe/Monaco */
    0x1c5, /* W. Europe Standard Time         / MT  ==>  Europe/Malta */
    0x1a8, /* W. Europe Standard Time         / NL  ==>  Europe/Amsterdam */
    0x1cb, /* W. Europe Standard Time         / NO  ==>  Europe/Oslo */
    0x1d8, /* W. Europe Standard Time         / SE  ==>  Europe/Stockholm */
    0x0e8, /* W. Europe Standard Time         / SJ  ==>  Arctic/Longyearbyen */
    0x1d2, /* W. Europe Standard Time         / SM  ==>  Europe/San_Marino */
    0x1df, /* W. Europe Standard Time         / VA  ==>  Europe/Vatican */
    0x10c, /* W. Mongolia Standard Time       / MN+ ==>  Asia/Hovd */
    0x138, /* West Asia Standard Time         / UZ+ ==>  Asia/Tashkent */
    0x0e0, /* West Asia Standard Time         / AQ  ==>  Antarctica/Mawson */
    0x0ed, /* West Asia Standard Time         / KZ  ==>  Asia/Aqtau */
    0x0ee, /* West Asia Standard Time         / KZ  ==>  Asia/Aqtobe */
    0x0f1, /* West Asia Standard Time         / KZ  ==>  Asia/Atyrau */
    0x128, /* West Asia Standard Time         / KZ  ==>  Asia/Oral */
    0x1f9, /* West Asia Standard Time         / MV  ==>  Indian/Maldives */
    0x1f7, /* West Asia Standard Time         / TF  ==>  Indian/Kerguelen */
    0x105, /* West Asia Standard Time         / TJ  ==>  Asia/Dushanbe */
    0x0ef, /* West Asia Standard Time         / TM  ==>  Asia/Ashgabat */
    0x132, /* West Asia Standard Time         / UZ  ==>  Asia/Samarkand */
    0x19d, /* West Asia Standard Time         / ZZ  ==>  Etc/GMT-5 */
    0x109, /* West Bank Standard Time         / PS+ ==>  Asia/Hebron */
    0x107, /* West Bank Standard Time         / PS  ==>  Asia/Gaza */
    0x22e, /* West Pacific Standard Time      / PG+ ==>  Pacific/Port_Moresby */
    0x0de, /* West Pacific Standard Time      / AQ  ==>  Antarctica/DumontDUrville */
    0x235, /* West Pacific Standard Time      / FM  ==>  Pacific/Truk */
    0x21c, /* West Pacific Standard Time      / GU  ==>  Pacific/Guam */
    0x230, /* West Pacific Standard Time      / MP  ==>  Pacific/Saipan */
    0x195, /* West Pacific Standard Time      / ZZ  ==>  Etc/GMT-10 */
    0x147, /* Yakutsk Standard Time           / RU+ ==>  Asia/Yakutsk */
    0x118, /* Yakutsk Standard Time           / RU  ==>  Asia/Khandyga */
};




RTDECL(PCRTTIMEZONEINFO) RTTimeZoneGetInfoByUnixName(const char *pszName)
{
    /*
     * Try a case sensitive binary search first.
     */
    /** @todo binary searching */

    /*
     * Fallback: Linear case-insensitive search.
     */
    size_t const cchName = strlen(pszName);
    for (size_t i = 0; i < RT_ELEMENTS(g_aTimeZones); i++)
        if (   g_aTimeZones[i].cchUnixName == cchName
            && RTStrICmpAscii(pszName, g_aTimeZones[i].pszUnixName) == 0)
            return &g_aTimeZones[i];
    return NULL;
}


RTDECL(PCRTTIMEZONEINFO) RTTimeZoneGetInfoByWindowsName(const char *pszName)
{
    /*
     * Try a case sensitive binary search first.
     */
    /** @todo binary searching */

    /*
     * Fallback: Linear case-insensitive search.
     */
    size_t const cchName = strlen(pszName);
    for (size_t i = 0; i < RT_ELEMENTS(g_aidxWinTimeZones); i++)
    {
        PCRTTIMEZONEINFO pZone = &g_aTimeZones[g_aidxWinTimeZones[i]];
        if (   pZone->cchWindowsName == cchName
            && RTStrICmpAscii(pszName, pZone->pszWindowsName) == 0)
            return pZone;
    }
    return NULL;
}


RTDECL(PCRTTIMEZONEINFO) RTTimeZoneGetInfoByWindowsIndex(uint32_t idxZone)
{
    for (size_t i = 0; i < RT_ELEMENTS(g_aidxWinTimeZones); i++)
    {
        PCRTTIMEZONEINFO pZone = &g_aTimeZones[g_aidxWinTimeZones[i]];
        if (pZone->idxWindows == idxZone)
            return pZone;
    }
    return NULL;
}

