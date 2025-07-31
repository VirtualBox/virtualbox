/* $Id: UIWizardNewVMNameOSTypePage.cpp 110477 2025-07-30 12:07:17Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewVMPageBasicNameOSStype class implementation.
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

/* Qt includes: */
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QStyle>
#include <QRegularExpression>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIRichTextLabel.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIGlobalSession.h"
#include "UIIconPool.h"
#include "UIMediumEnumerator.h"
#include "UINameAndSystemEditor.h"
#include "UINotificationCenter.h"
#include "UIWizardNewVMNameOSTypePage.h"
#include "UIWizardNewVM.h"

/* COM includes: */
#include "CHost.h"
#include "CUnattended.h"
#include <VBox/com/VirtualBox.h> /* Need GUEST_OS_ID_STR_X86 and friends. */

/* Defines some patterns to guess the right OS type. Should be in sync with
 * VirtualBox-settings-common.xsd in Main. The list is sorted by priority. The
 * first matching string found, will be used. */
struct osTypePattern
{
    QRegularExpression pattern;
    const char *pcstId;
};

static const osTypePattern gs_OSTypePattern[] =
{
    /* DOS: */
    { QRegularExpression("DOS", QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("DOS") },

    /* Windows: */
    { QRegularExpression(  "Wi.*98",                         QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Windows98") },
    { QRegularExpression(  "Wi.*95",                         QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Windows95") },
    { QRegularExpression(  "Wi.*Me",                         QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("WindowsMe") },
    { QRegularExpression( "(Wi.*NT)|(NT[-._v]*4)",           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("WindowsNT4") },
    { QRegularExpression( "NT[-._v]*3[.,]*[51x]",            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("WindowsNT3x") },
    { QRegularExpression("(Wi.*XP.*64)|(XP.*64)",            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("WindowsXP") },
    { QRegularExpression("(XP)",                             QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("WindowsXP") },
    { QRegularExpression("((Wi.*2003)|(W2K3)|(Win2K3)).*64", QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Windows2003") },
    { QRegularExpression("((Wi.*2003)|(W2K3)|(Win2K3)).*32", QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Windows2003") },
    { QRegularExpression("((Wi.*Vis)|(Vista)).*64",          QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("WindowsVista") },
    { QRegularExpression("((Wi.*Vis)|(Vista)).*32",          QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("WindowsVista") },
    { QRegularExpression( "(Wi.*2025)|(W2K25)|(Win2K25)",    QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Windows2025") },
    { QRegularExpression( "(Wi.*2022)|(W2K22)|(Win2K22)",    QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Windows2022") },
    { QRegularExpression( "(Wi.*2016)|(W2K16)|(Win2K16)",    QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Windows2016") },
    { QRegularExpression( "(Wi.*2012)|(W2K12)|(Win2K12)",    QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Windows2012") },
    { QRegularExpression("((Wi.*2008)|(W2K8)|(Win2k8)).*64", QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Windows2008") },
    { QRegularExpression("((Wi.*2008)|(W2K8)|(Win2K8)).*32", QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Windows2008") },
    { QRegularExpression( "(Wi.*2000)|(W2K)|(Win2K)",        QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Windows2000") },
    { QRegularExpression( "(Wi.*7.*64)|(W7.*64)",            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Windows7") },
    { QRegularExpression( "(Wi.*7.*32)|(W7.*32)",            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Windows7") },
    { QRegularExpression( "(Wi.*8.*1.*64)|(W8.*64)",         QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Windows81") },
    { QRegularExpression( "(Wi.*8.*1.*32)|(W8.*32)",         QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Windows81") },
    { QRegularExpression( "(Wi.*8.*64)|(W8.*64)",            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Windows8") },
    { QRegularExpression( "(Wi.*8.*32)|(W8.*32)",            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Windows8") },
    { QRegularExpression( "(Wi.*10.*64)|(W10.*64)",          QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Windows10") },
    { QRegularExpression( "(Wi.*10.*32)|(W10.*32)",          QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Windows10") },
    { QRegularExpression( "(Wi.*11)|(W11)",                  QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Windows11") },
    { QRegularExpression( "(Wi.*11)|(W11)",                  QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("Windows11") },
    { QRegularExpression(  "Wi.*3.*1",                       QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Windows31") },
    /* Set Windows 10 as default for "Windows". */
    { QRegularExpression(  "Wi.*64",                         QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Windows10") },
    { QRegularExpression(  "Wi.*32",                         QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Windows10") },
    /* Set Windows 11 as default for "Windows" on ARM. */
    { QRegularExpression(  "Wi.*",                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("Windows11") },
    /* ReactOS wants to be considered as Windows 2003 */
    { QRegularExpression(  "Reac.*",                         QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Windows2003") },

    /* Solaris: */
    { QRegularExpression("((Op.*Sol)|(os20[01][0-9])|(India)|(Illum)|(Neva)).*64",   QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("OpenSolaris") },
    { QRegularExpression("((Op.*Sol)|(os20[01][0-9])|(India)|(Illum)|(Neva)).*32",   QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("OpenSolaris") },
    { QRegularExpression("(Sol.*10.*(10/09)|(9/10)|(8/11)|(1/13)).*64",              QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Solaris10U8_or_later") },
    { QRegularExpression("(Sol.*10.*(10/09)|(9/10)|(8/11)|(1/13)).*32",              QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Solaris10U8_or_later") },
    { QRegularExpression("(Sol.*10.*(U[89])|(U1[01])).*64",                          QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Solaris10U8_or_later") },
    { QRegularExpression("(Sol.*10.*(U[89])|(U1[01])).*32",                          QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Solaris10U8_or_later") },
    { QRegularExpression("(Sol.*10.*(1/06)|(6/06)|(11/06)|(8/07)|(5/08)|(10/08)|(5/09)).*64",  QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Solaris") }, // Solaris 10U7 (5/09) or earlier
    { QRegularExpression("(Sol.*10.*(1/06)|(6/06)|(11/06)|(8/07)|(5/08)|(10/08)|(5/09)).*32",  QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Solaris") }, // Solaris 10U7 (5/09) or earlier
    { QRegularExpression("((Sol.*10.*U[1-7])|(Sol.*10)).*64",                        QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Solaris") }, // Solaris 10U7 (5/09) or earlier
    { QRegularExpression("((Sol.*10.*U[1-7])|(Sol.*10)).*32",                        QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Solaris") }, // Solaris 10U7 (5/09) or earlier
    { QRegularExpression("((Sol.*11)|(Sol.*)).*64",                                  QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Solaris11") },

    /* OS/2: */
    { QRegularExpression("OS[/|!-]{,1}2.*W.*4.?5", QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("OS2Warp45") },
    { QRegularExpression("OS[/|!-]{,1}2.*W.*4",    QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("OS2Warp4") },
    { QRegularExpression("OS[/|!-]{,1}2.*W",       QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("OS2Warp3") },
    { QRegularExpression("OS[/|!-]{,1}2.*e",       QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("OS2eCS") },
    { QRegularExpression("OS[/|!-]{,1}2.*Ar.*",    QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("OS2ArcaOS") },
    { QRegularExpression("OS[/|!-]{,1}2",          QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("OS2") },
    { QRegularExpression("(eComS.*|eCS.*)",        QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("OS2eCS") },
    { QRegularExpression("Arca.*",                 QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("OS2ArcaOS") },

    /* Other: Must come before Ubuntu/Maverick and before Linux??? */
    { QRegularExpression("QN", QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("QNX") },

    /* Mac OS X: Must come before Ubuntu/Maverick and before Linux: */
    { QRegularExpression("((mac.*10[.,]{0,1}4)|(os.*x.*10[.,]{0,1}4)|(mac.*ti)|(os.*x.*ti)|(Tig)).64",     QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("MacOS") },
    { QRegularExpression("((mac.*10[.,]{0,1}4)|(os.*x.*10[.,]{0,1}4)|(mac.*ti)|(os.*x.*ti)|(Tig)).32",     QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("MacOS") },
    { QRegularExpression("((mac.*10[.,]{0,1}5)|(os.*x.*10[.,]{0,1}5)|(mac.*leo)|(os.*x.*leo)|(Leop)).*64", QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("MacOS") },
    { QRegularExpression("((mac.*10[.,]{0,1}5)|(os.*x.*10[.,]{0,1}5)|(mac.*leo)|(os.*x.*leo)|(Leop)).*32", QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("MacOS") },
    { QRegularExpression("((mac.*10[.,]{0,1}6)|(os.*x.*10[.,]{0,1}6)|(mac.*SL)|(os.*x.*SL)|(Snow L)).*64", QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("MacOS106") },
    { QRegularExpression("((mac.*10[.,]{0,1}6)|(os.*x.*10[.,]{0,1}6)|(mac.*SL)|(os.*x.*SL)|(Snow L)).*32", QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("MacOS106") },
    { QRegularExpression( "(mac.*10[.,]{0,1}7)|(os.*x.*10[.,]{0,1}7)|(mac.*ML)|(os.*x.*ML)|(Mount)",       QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("MacOS107") },
    { QRegularExpression( "(mac.*10[.,]{0,1}8)|(os.*x.*10[.,]{0,1}8)|(Lion)",                              QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("MacOS108") },
    { QRegularExpression( "(mac.*10[.,]{0,1}9)|(os.*x.*10[.,]{0,1}9)|(mac.*mav)|(os.*x.*mav)|(Mavericks)", QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("MacOS109") },
    { QRegularExpression( "(mac.*yos)|(os.*x.*yos)|(Yosemite)",                                            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("MacOS1010") },
    { QRegularExpression( "(mac.*cap)|(os.*x.*capit)|(Capitan)",                                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("MacOS1011") },
    { QRegularExpression( "(mac.*hig)|(os.*x.*high.*sierr)|(High Sierra)",                                 QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("MacOS1013") },
    { QRegularExpression( "(mac.*sie)|(os.*x.*sierr)|(Sierra)",                                            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("MacOS1012") },
    { QRegularExpression("((Mac)|(Tig)|(Leop)|(Yose)|(os[ ]*x)).*64",                                      QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("MacOS") },
    { QRegularExpression("((Mac)|(Tig)|(Leop)|(Yose)|(os[ ]*x)).*32",                                      QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("MacOS") },

    /* Code names for Linux distributions: */
    { QRegularExpression("((bianca)|(cassandra)|(celena)|(daryna)|(elyssa)|(felicia)|(gloria)|(helena)|(isadora)|(julia)|(katya)|(lisa)|(maya)|(nadia)|(olivia)|(petra)|(qiana)|(rebecca)|(rafaela)|(rosa)).*64", QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu") },
    { QRegularExpression("((bianca)|(cassandra)|(celena)|(daryna)|(elyssa)|(felicia)|(gloria)|(helena)|(isadora)|(julia)|(katya)|(lisa)|(maya)|(nadia)|(olivia)|(petra)|(qiana)|(rebecca)|(rafaela)|(rosa)).*32", QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Ubuntu") },
    { QRegularExpression("((edgy)|(feisty)|(gutsy)|(hardy)|(intrepid)|(jaunty)|(karmic)).*64",  QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu") },
    { QRegularExpression("((edgy)|(feisty)|(gutsy)|(hardy)|(intrepid)|(jaunty)|(karmic)).*32",  QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Ubuntu") },
    { QRegularExpression("((eft)|(fawn)|(gibbon)|(heron)|(ibex)|(jackalope)|(koala)).*64",      QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu") },
    { QRegularExpression("((eft)|(fawn)|(gibbon)|(heron)|(ibex)|(jackalope)|(koala)).*32",      QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Ubuntu") },
    { QRegularExpression("((lucid)|(lynx)).*64",                                                QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu10_LTS") },
    { QRegularExpression("((lucid)|(lynx)).*32",                                                QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Ubuntu10_LTS") },
    { QRegularExpression("((maverick)|(meerkat)).*64",                                          QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu10") },
    { QRegularExpression("((maverick)|(meerkat)).*32",                                          QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Ubuntu10") },
    { QRegularExpression("((natty)|(narwhal)|(oneiric)|(ocelot)).*64",                          QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu11") },
    { QRegularExpression("((natty)|(narwhal)|(oneiric)|(ocelot)).*32",                          QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Ubuntu11") },
    { QRegularExpression("((precise)|(pangolin)).*64",                                          QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu12_LTS") },
    { QRegularExpression("((precise)|(pangolin)).*32",                                          QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Ubuntu12_LTS") },
    { QRegularExpression("((quantal)|(quetzal)).*64",                                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu12") },
    { QRegularExpression("((quantal)|(quetzal)).*32",                                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Ubuntu12") },
    { QRegularExpression("((raring)|(ringtail)|(saucy)|(salamander)).*64",                      QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu13") },
    { QRegularExpression("((raring)|(ringtail)|(saucy)|(salamander)).*32",                      QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Ubuntu13") },
    { QRegularExpression("((trusty)|(tahr)).*64",                                               QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu14_LTS") },
    { QRegularExpression("((trusty)|(tahr)).*32",                                               QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Ubuntu14_LTS") },
    { QRegularExpression("((utopic)|(unicorn)).*64",                                            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu14") },
    { QRegularExpression("((utopic)|(unicorn)).*32",                                            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Ubuntu14") },
    { QRegularExpression("((vivid)|(vervet)|(wily)|(werewolf)).*64",                            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu15") },
    { QRegularExpression("((vivid)|(vervet)|(wily)|(werewolf)).*32",                            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Ubuntu15") },
    { QRegularExpression("((xenial)|(xerus)).*64",                                              QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu16_LTS") },
    { QRegularExpression("((xenial)|(xerus)).*32",                                              QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Ubuntu16_LTS") },
    { QRegularExpression("((yakkety)|(yak)).*64",                                               QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu16") },
    { QRegularExpression("((yakkety)|(yak)).*32",                                               QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Ubuntu16") },
    { QRegularExpression("((zesty)|(zapus)|(artful)|(aardvark)).*64",                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu17") },
    { QRegularExpression("((zesty)|(zapus)|(artful)|(aardvark)).*32",                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Ubuntu17") },
    { QRegularExpression("((bionic)|(beaver)).*64",                                             QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu18_LTS") },
    { QRegularExpression("((bionic)|(beaver)).*32",                                             QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Ubuntu18_LTS") },
    { QRegularExpression("((cosmic)|(cuttlefish)).*64",                                         QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu18") },
    { QRegularExpression("((cosmic)|(cuttlefish)).*32",                                         QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Ubuntu18") },
    { QRegularExpression("((disco)|(dingo)|(eoan)|(ermine)).*64",                               QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu19") },
    { QRegularExpression("((disco)|(dingo)|(eoan)|(ermine)).*32",                               QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Ubuntu19") },
    { QRegularExpression("((focal)|(fossa)).*64",                                               QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu20_LTS") },
    { QRegularExpression("((groovy)|(gorilla)).*64",                                            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu20") },
    { QRegularExpression("((hirsute)|(hippo)|(impish)|(indri)).*64",                            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu21") },
    { QRegularExpression("((jammy)|(jellyfish)).*64",                                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu22_LTS") },
    { QRegularExpression("((jammy)|(jellyfish)).*64",                                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("Ubuntu22_LTS") },
    { QRegularExpression("((kinetic)|(kudu)).*64",                                              QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu22") },
    { QRegularExpression("((kinetic)|(kudu)).*64",                                              QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("Ubuntu22") },
    { QRegularExpression("((lunar)|(lobster)).*64",                                             QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu23") },
    { QRegularExpression("((lunar)|(lobster)).*64",                                             QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("Ubuntu23") },
    { QRegularExpression("((mantic)|(minotaur)).*64",                                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu231") },
    { QRegularExpression("((mantic)|(minotaur)).*64",                                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("Ubuntu231") },
    { QRegularExpression("((noble)|(numbat)).*64",                                              QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu24_LTS") },
    { QRegularExpression("((noble)|(numbat)).*64",                                              QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("Ubuntu24_LTS") },
    { QRegularExpression("((oracular)|(oriole)).*64",                                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu24") },
    { QRegularExpression("((oracular)|(oriole)).*64",                                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("Ubuntu24") },
    { QRegularExpression("sarge.*32",                         QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Debian31") },
    { QRegularExpression("^etch.*64",                         QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Debian4") },
    { QRegularExpression("debian.*4.*64",                     QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Debian4") },
    { QRegularExpression("^etch.*32",                         QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Debian4") },
    { QRegularExpression("debian.*4.*32",                     QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Debian4") },
    { QRegularExpression("lenny.*64",                         QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Debian5") },
    { QRegularExpression("lenny.*32",                         QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Debian5") },
    { QRegularExpression("squeeze.*64",                       QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Debian6") },
    { QRegularExpression("debian.*6.*64",                     QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Debian6") },
    { QRegularExpression("squeeze.*32",                       QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Debian6") },
    { QRegularExpression("debian.*6.*32",                     QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Debian6") },
    { QRegularExpression("wheezy.*64",                        QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Debian7") },
    { QRegularExpression("debian.*7.*64",                     QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Debian7") },
    { QRegularExpression("wheezy.*32",                        QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Debian7") },
    { QRegularExpression("debian.*7.*32",                     QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Debian7") },
    { QRegularExpression("jessie.*64",                        QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Debian8") },
    { QRegularExpression("debian.*8.*64",                     QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Debian8") },
    { QRegularExpression("jessie.*32",                        QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Debian8") },
    { QRegularExpression("debian.*8*32",                      QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Debian8") },
    { QRegularExpression("stretch.*64",                       QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Debian9") },
    { QRegularExpression("debian.*9.*64",                     QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Debian9") },
    { QRegularExpression("debian.*9.*32",                     QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Debian9") },
    { QRegularExpression("stretch.*32",                       QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Debian9") },
    { QRegularExpression("stretch.*64",                       QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("Debian9") },
    { QRegularExpression("debian.*9.*64",                     QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("Debian9") },
    { QRegularExpression("debian.*9.*32",                     QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM32("Debian9") },
    { QRegularExpression("stretch.*32",                       QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM32("Debian9") },
    { QRegularExpression("buster.*64",                        QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Debian10") },
    { QRegularExpression("debian.*10.*64",                    QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Debian10") },
    { QRegularExpression("buster.*32",                        QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Debian10") },
    { QRegularExpression("debian.*10.*32",                    QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Debian10") },
    { QRegularExpression("buster.*64",                        QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("Debian10") },
    { QRegularExpression("debian.*10.*64",                    QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("Debian10") },
    { QRegularExpression("buster.*32",                        QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM32("Debian10") },
    { QRegularExpression("debian.*10.*32",                    QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM32("Debian10") },
    { QRegularExpression("bullseye.*64",                      QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Debian11") },
    { QRegularExpression("debian.*11.*64",                    QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Debian11") },
    { QRegularExpression("bullseye.*32",                      QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Debian11") },
    { QRegularExpression("debian.*11.*32",                    QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Debian11") },
    { QRegularExpression("bullseye.*64",                      QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("Debian11") },
    { QRegularExpression("debian.*11.*64",                    QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("Debian11") },
    { QRegularExpression("bullseye.*32",                      QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM32("Debian11") },
    { QRegularExpression("debian.*11.*32",                    QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM32("Debian11") },
    { QRegularExpression("bookworm.*64",                      QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Debian12") },
    { QRegularExpression("debian.*12.*64",                    QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Debian12") },
    { QRegularExpression("debian.*12",                        QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Debian12") },
    { QRegularExpression("bookworm.*32",                      QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Debian12") },
    { QRegularExpression("bookworm.*64",                      QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("Debian12") },
    { QRegularExpression("debian.*12.*64",                    QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("Debian12") },
    { QRegularExpression("debian.*12",                        QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM32("Debian12") },
    { QRegularExpression("bookworm.*32",                      QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM32("Debian12") },
    { QRegularExpression("((trixie)|(sid)).*64",              QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Debian") },
    { QRegularExpression("((trixie)|(sid)).*32",              QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Debian") },
    { QRegularExpression("((moonshine)|(werewolf)|(sulphur)|(cambridge)|(leonidas)|(constantine)|(goddard)|(laughlin)|(lovelock)|(verne)|(beefy)|(spherical)|(schrodinger)|(heisenberg)).*64", QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Fedora") },
    { QRegularExpression("((moonshine)|(werewolf)|(sulphur)|(cambridge)|(leonidas)|(constantine)|(goddard)|(laughlin)|(lovelock)|(verne)|(beefy)|(spherical)|(schrodinger)|(heisenberg)).*32", QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Fedora") },
    { QRegularExpression("((basilisk)|(emerald)|(teal)|(celadon)|(asparagus)|(mantis)|(dartmouth)|(bottle)|(harlequin)).*64", QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("OpenSUSE") },
    { QRegularExpression("((basilisk)|(emerald)|(teal)|(celadon)|(asparagus)|(mantis)|(dartmouth)|(bottle)|(harlequin)).*32", QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("OpenSUSE") },

    /* Regular names of Linux distributions: */
    { QRegularExpression("Arc.*64",                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("ArchLinux") },
    { QRegularExpression("Arc.*32",                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("ArchLinux") },
    { QRegularExpression("Arc.*64",                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("ArchLinux") },
    { QRegularExpression("Arc.*32",                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM32("ArchLinux") },
    { QRegularExpression("Deb.*64",                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Debian") },
    { QRegularExpression("Deb.*32",                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Debian") },
    { QRegularExpression("Deb.*64",                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("Debian") },
    { QRegularExpression("Deb.*32",                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM32("Debian") },
    { QRegularExpression("SU.*Leap.*64",                      QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("OpenSUSE_Leap") },
    { QRegularExpression("SU.*Leap.*64",                      QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("OpenSUSE_Leap") },
    { QRegularExpression("SU.*Tumble.*64",                    QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("OpenSUSE_Tumbleweed") },
    { QRegularExpression("SU.*Tumble.*32",                    QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("OpenSUSE_Tumbleweed") },
    { QRegularExpression("SU.*Tumble.*64",                    QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("OpenSUSE_Tumbleweed") },
    { QRegularExpression("SU.*Tumble.*32",                    QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM32("OpenSUSE_Tumbleweed") },
    { QRegularExpression("((SU)|(Nov)|(SLE)).*64",            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("OpenSUSE") },
    { QRegularExpression("((SU)|(Nov)|(SLE)).*32",            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("OpenSUSE") },
    { QRegularExpression("Fe.*64",                            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Fedora") },
    { QRegularExpression("Fe.*32",                            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Fedora") },
    { QRegularExpression("Fe.*64",                            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("Fedora") },
    { QRegularExpression("Fe.*32",                            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM32("Fedora") },
    { QRegularExpression("((Gen)|(Sab)).*64",                 QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Gentoo") },
    { QRegularExpression("((Gen)|(Sab)).*32",                 QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Gentoo") },
    { QRegularExpression("^Man.*64",                          QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Mandriva") },
    { QRegularExpression("^Man.*32",                          QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Mandriva") },
    { QRegularExpression("Op.*Man.*Lx.*64",                   QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("OpenMandriva_Lx") },
    { QRegularExpression("Op.*Man.*Lx.*32",                   QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("OpenMandriva_Lx") },
    { QRegularExpression("PCL.*OS.*64",                       QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("PCLinuxOS") },
    { QRegularExpression("PCL.*OS.*32",                       QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("PCLinuxOS") },
    { QRegularExpression("Mageia.*64",                        QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Mageia") },
    { QRegularExpression("Mageia.*32",                        QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Mageia") },
    { QRegularExpression("((Red)|(rhel)|(cen)).*64",          QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("RedHat") },
    { QRegularExpression("((Red)|(rhel)|(cen)).*32",          QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("RedHat") },
    { QRegularExpression("Tur.*64",                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Turbolinux") },
    { QRegularExpression("Tur.*32",                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Turbolinux") },
    { QRegularExpression("Lub.*64",                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Lubuntu") },
    { QRegularExpression("Lub.*32",                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Lubuntu") },
    { QRegularExpression("Xub.*64",                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Xubuntu") },
    { QRegularExpression("Xub.*32",                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Xubuntu") },
    { QRegularExpression("((Ub)|(Mint)).*64",                 QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Ubuntu") },
    { QRegularExpression("((Ub)|(Mint)).*32",                 QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Ubuntu") },
    { QRegularExpression("((Ub)|(Mint)).*64",                 QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("Ubuntu") },
    { QRegularExpression("((Ub)|(Mint)).*32",                 QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM32("Ubuntu") },
    { QRegularExpression("Xa.*64",                            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Xandros") },
    { QRegularExpression("Xa.*32",                            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Xandros") },
    { QRegularExpression("((Or)|(oel)|(^ol)).*64",            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Oracle") },
    { QRegularExpression("((Or)|(oel)|(^ol)).*32",            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Oracle") },
    { QRegularExpression("((Or)|(oel)|(^ol)).*64",            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("Oracle") },
    { QRegularExpression("Knoppix",                           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Linux26") },
    { QRegularExpression("Dsl",                               QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Linux24") },
    { QRegularExpression("((Lin)|(lnx)).*2.?2",               QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Linux22") },
    { QRegularExpression("((Lin)|(lnx)).*2.?4.*64",           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Linux24") },
    { QRegularExpression("((Lin)|(lnx)).*2.?4.*32",           QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Linux24") },
    { QRegularExpression("((((Lin)|(lnx)).*2.?6)|(LFS)).*64", QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Linux26") },
    { QRegularExpression("((((Lin)|(lnx)).*2.?6)|(LFS)).*32", QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Linux26") },
    { QRegularExpression("((Lin)|(lnx)).*64",                 QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("Linux26") },
    { QRegularExpression("((Lin)|(lnx)).*32",                 QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Linux26") },

    /* Other: */
    { QRegularExpression("L4",                   QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("L4") },
    { QRegularExpression("((Fr.*B)|(fbsd)).*64", QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("FreeBSD") },
    { QRegularExpression("((Fr.*B)|(fbsd)).*32", QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("FreeBSD") },
    { QRegularExpression("((Fr.*B)|(fbsd)).*64", QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("FreeBSD") },
    { QRegularExpression("((Fr.*B)|(fbsd)).*32", QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM32("FreeBSD") },
    { QRegularExpression("Op.*B.*64",            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("OpenBSD") },
    { QRegularExpression("Op.*B.*32",            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("OpenBSD") },
    { QRegularExpression("Op.*B.*64",            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("OpenBSD") },
    { QRegularExpression("Op.*B.*32",            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM32("OpenBSD") },
    { QRegularExpression("Ne.*B.*64",            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("NetBSD") },
    { QRegularExpression("Ne.*B.*32",            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("NetBSD") },
    { QRegularExpression("Ne.*B.*64",            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("NetBSD") },
    { QRegularExpression("Ne.*B.*32",            QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM32("NetBSD") },
    { QRegularExpression("Net",                  QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Netware") },
    { QRegularExpression("Rocki",                QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("JRockitVE") },
    { QRegularExpression("bs[23]{0,1}-",         QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X64("VBoxBS") }, /* bootsector tests */
    { QRegularExpression("Ot",                   QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_X86("Other") },
    { QRegularExpression("Ot",                   QRegularExpression::CaseInsensitiveOption), GUEST_OS_ID_STR_ARM64("Other") },
};

static const QRegularExpression gs_Prefer32BitNamePatterns = QRegularExpression("(XP)", QRegularExpression::CaseInsensitiveOption);


bool UIWizardNewVMNameOSTypeCommon::guessOSTypeFromName(UINameAndSystemEditor *pNameAndSystemEditor, QString strNewName)
{
    AssertReturn(pNameAndSystemEditor, false);

    /* Append default architecture bit-count (64/32) if not already in the name, unless
       it's XP or similar which is predominantly 32-bit: */
    if (!strNewName.contains("32") && !strNewName.contains("64") && !strNewName.contains(gs_Prefer32BitNamePatterns))
        strNewName += ARCH_BITS == 64 ? "64" : "32";

    /* Search for a matching OS type based on the string the user typed already. */
    for (size_t i = 0; i < RT_ELEMENTS(gs_OSTypePattern); ++i)
    {
        if (   strNewName.contains(gs_OSTypePattern[i].pattern)
            && gpGlobalSession->guestOSTypeManager().isGuestOSTypeIDSupported(gs_OSTypePattern[i].pcstId))
        {

            return pNameAndSystemEditor->setGuestOSTypeByTypeId(gs_OSTypePattern[i].pcstId);
        }
    }
    return false;
}

bool UIWizardNewVMNameOSTypeCommon::guessOSTypeDetectedOSTypeString(UINameAndSystemEditor *pNameAndSystemEditor, QString strDetectedOSType)
{
    AssertReturn(pNameAndSystemEditor, false);
    if (!strDetectedOSType.isEmpty())
    {
        if (!pNameAndSystemEditor->setGuestOSTypeByTypeId(strDetectedOSType))
        {
            pNameAndSystemEditor->setGuestOSTypeByTypeId(GUEST_OS_ID_STR_X86("Other"));
            /* Return false to allow OS type guessing from name. See caller code: */
            return false;
        }
        return true;
    }
    pNameAndSystemEditor->setGuestOSTypeByTypeId(GUEST_OS_ID_STR_X86("Other"));
    return false;
}

void UIWizardNewVMNameOSTypeCommon::composeMachineFilePath(UINameAndSystemEditor *pNameAndSystemEditor,
                                                           UIWizardNewVM *pWizard)
{
    if (!pNameAndSystemEditor || !pWizard)
        return;
    if (pNameAndSystemEditor->name().isEmpty() || pNameAndSystemEditor->path().isEmpty())
        return;
    /* Get VBox: */
    CVirtualBox vbox = gpGlobalSession->virtualBox();

    /* Compose machine filename: */
    pWizard->setMachineFilePath(vbox.ComposeMachineFilename(pNameAndSystemEditor->name(),
                                                            pWizard->machineGroup(),
                                                            QString(),
                                                            pNameAndSystemEditor->path()));
    /* Compose machine folder/basename: */
    const QFileInfo fileInfo(pWizard->machineFilePath());
    pWizard->setMachineFolder(fileInfo.absolutePath());
    pWizard->setMachineBaseName(pNameAndSystemEditor->name());
}

bool UIWizardNewVMNameOSTypeCommon::createMachineFolder(UINameAndSystemEditor *pNameAndSystemEditor,
                                                        UIWizardNewVM *pWizard)
{
    if (!pNameAndSystemEditor || !pWizard)
        return false;
    const QString &strMachineFolder = pWizard->machineFolder();
    const QString &strCreatedFolder = pWizard->createdMachineFolder();

    /* Cleanup previosly created folder if any: */
    if (!cleanupMachineFolder(pWizard))
    {
        UINotificationMessage::cannotRemoveMachineFolder(strCreatedFolder, pWizard->notificationCenter());
        return false;
    }

    /* Check if the folder already exists and check if it has been created by this wizard */
    if (QDir(strMachineFolder).exists())
    {
        /* Looks like we have already created this folder for this run of the wizard. Just return */
        if (strCreatedFolder == strMachineFolder)
            return true;
        /* The folder is there but not because of this wizard. Avoid overwriting a existing machine's folder */
        else
        {
            UINotificationMessage::cannotOverwriteMachineFolder(strMachineFolder, pWizard->notificationCenter());
            return false;
        }
    }

    /* Try to create new folder (and it's predecessors): */
    bool fMachineFolderCreated = QDir().mkpath(strMachineFolder);
    if (!fMachineFolderCreated)
    {
        UINotificationMessage::cannotCreateMachineFolder(strMachineFolder, pWizard->notificationCenter());
        return false;
    }
    pWizard->setCreatedMachineFolder(strMachineFolder);
    return true;
}

bool UIWizardNewVMNameOSTypeCommon::cleanupMachineFolder(UIWizardNewVM *pWizard, bool fWizardCancel /* = false */)
{
    if (!pWizard)
        return false;
    const QString &strMachineFolder = pWizard->machineFolder();
    const QString &strCreatedFolder = pWizard->createdMachineFolder();
    /* Make sure folder was previosly created: */
    if (strCreatedFolder.isEmpty())
        return true;
    /* Clean this folder if the machine folder has been changed by the user or we are cancelling the wizard: */
    if (strCreatedFolder != strMachineFolder || fWizardCancel)
    {
        /* Try to cleanup folder (and it's predecessors): */
        bool fMachineFolderRemoved = QDir(strCreatedFolder).removeRecursively();
        /* Reset machine folder value: */
        if (fMachineFolderRemoved)
            pWizard->setCreatedMachineFolder(QString());
        /* Return cleanup result: */
        return fMachineFolderRemoved;
    }
    return true;
}

bool UIWizardNewVMNameOSTypeCommon::checkISOFile(const QString &strPath)
{
    if (strPath.isNull() || strPath.isEmpty())
        return true;
    QFileInfo fileInfo(strPath);
    if (!fileInfo.exists() || !fileInfo.isReadable())
        return false;
    return true;
}

void UIWizardNewVMNameOSTypeCommon::setUnattendedCheckBoxEnable(QCheckBox *pUnattendedCheckBox,
                                                                const QString &strISOPath, bool fIsUnattendedInstallSupported)
{
    AssertReturnVoid(pUnattendedCheckBox);

    if (!fIsUnattendedInstallSupported)
    {
        pUnattendedCheckBox->setEnabled(false);
        pUnattendedCheckBox->setChecked(false);
    }
    else
    {
        pUnattendedCheckBox->setEnabled(UIWizardNewVMNameOSTypeCommon::checkISOFile(strISOPath));
        pUnattendedCheckBox->setChecked(true);
    }
}

UIWizardNewVMNameOSTypePage::UIWizardNewVMNameOSTypePage(const QString strHelpKeyword /* = QString() */)
    : UINativeWizardPage(strHelpKeyword)
    , m_pNameAndSystemLayout(0)
    , m_pNameAndSystemEditor(0)
    , m_pUnattendedCheckBox(0)
    , m_pNameOSTypeLabel(0)
    , m_pInfoLabel(0)
{
    prepare();
}

void UIWizardNewVMNameOSTypePage::setISOFilePath(const QString &strISOFilePath)
{
    QFileInfo isoFileInfo(strISOFilePath);
    if (isoFileInfo.exists() && m_pNameAndSystemEditor)
        m_pNameAndSystemEditor->setISOImagePath(strISOFilePath);
}

void UIWizardNewVMNameOSTypePage::prepare()
{
    QVBoxLayout *pPageLayout = new QVBoxLayout(this);
    if (pPageLayout)
    {
        m_pNameOSTypeLabel = new QIRichTextLabel(this);
        if (m_pNameOSTypeLabel)
            pPageLayout->addWidget(m_pNameOSTypeLabel);

        /* Prepare Name and OS Type editor: */
        pPageLayout->addWidget(createNameOSTypeWidgets());

        pPageLayout->addStretch();
    }

    createConnections();
}

void UIWizardNewVMNameOSTypePage::createConnections()
{
    if (m_pNameAndSystemEditor)
    {
        connect(m_pNameAndSystemEditor, &UINameAndSystemEditor::sigNameChanged, this, &UIWizardNewVMNameOSTypePage::sltNameChanged);
        connect(m_pNameAndSystemEditor, &UINameAndSystemEditor::sigPathChanged, this, &UIWizardNewVMNameOSTypePage::sltPathChanged);
        connect(m_pNameAndSystemEditor, &UINameAndSystemEditor::sigOsTypeChanged, this, &UIWizardNewVMNameOSTypePage::sltOsTypeChanged);
        connect(m_pNameAndSystemEditor, &UINameAndSystemEditor::sigImageChanged, this, &UIWizardNewVMNameOSTypePage::sltISOPathChanged);
        connect(m_pNameAndSystemEditor, &UINameAndSystemEditor::sigOSFamilyChanged, this, &UIWizardNewVMNameOSTypePage::sltGuestOSFamilyChanged);
        connect(m_pNameAndSystemEditor, &UINameAndSystemEditor::sigEditionChanged, this, &UIWizardNewVMNameOSTypePage::sltSelectedEditionChanged);
    }
    if (m_pUnattendedCheckBox)
        connect(m_pUnattendedCheckBox, &QCheckBox::toggled, this, &UIWizardNewVMNameOSTypePage::sltUnattendedInstallEnableChanged);
}

bool UIWizardNewVMNameOSTypePage::isComplete() const
{
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturn(pWizard, false);
    markWidgets();
    if (m_pNameAndSystemEditor->name().isEmpty())
        return false;
    if (!isMachineFolderUnique())
        return false;
    return UIWizardNewVMNameOSTypeCommon::checkISOFile(m_pNameAndSystemEditor->ISOImagePath());
}

void UIWizardNewVMNameOSTypePage::sltNameChanged(const QString &strNewName)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    if (!m_userModifiedParameters.contains("GuestOSTypeFromISO"))
    {
        m_pNameAndSystemEditor->blockSignals(true);
        if (UIWizardNewVMNameOSTypeCommon::guessOSTypeFromName(m_pNameAndSystemEditor, strNewName))
        {
            wizardWindow<UIWizardNewVM>()->setGuestOSTypeId(m_pNameAndSystemEditor->typeId());
            m_userModifiedParameters << "GuestOSTypeFromName";
        }
        m_pNameAndSystemEditor->blockSignals(false);
    }
    UIWizardNewVMNameOSTypeCommon::composeMachineFilePath(m_pNameAndSystemEditor, wizardWindow<UIWizardNewVM>());
    emit completeChanged();
}

void UIWizardNewVMNameOSTypePage::sltPathChanged(const QString &strNewPath)
{
    Q_UNUSED(strNewPath);
    emit completeChanged();
    UIWizardNewVMNameOSTypeCommon::composeMachineFilePath(m_pNameAndSystemEditor, wizardWindow<UIWizardNewVM>());
}

void UIWizardNewVMNameOSTypePage::sltOsTypeChanged()
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    //m_userModifiedParameters << "GuestOSType";
    if (m_pNameAndSystemEditor)
        wizardWindow<UIWizardNewVM>()->setGuestOSTypeId(m_pNameAndSystemEditor->typeId());
}

void UIWizardNewVMNameOSTypePage::sltRetranslateUI()
{
    setTitle(UIWizardNewVM::tr("Virtual machine name and operating system"));

    if (m_pNameOSTypeLabel)
        m_pNameOSTypeLabel->setText(UIWizardNewVM::tr("The ISO image is used to install the operating system on the VM."));

    if (m_pUnattendedCheckBox)
    {
        m_pUnattendedCheckBox->setText(UIWizardNewVM::tr("&Proceed with Unattended Installation"));
        m_pUnattendedCheckBox->setToolTip(UIWizardNewVM::tr("The ISO is attached to the VM, so you can install the OS automatically"));
    }

    if (m_pNameAndSystemLayout && m_pNameAndSystemEditor)
        m_pNameAndSystemLayout->setColumnMinimumWidth(0, m_pNameAndSystemEditor->firstColumnWidth());

    updateInfoLabel();
}

void UIWizardNewVMNameOSTypePage::updateInfoLabel()
{
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturnVoid(pWizard);

    if (!m_pInfoLabel)
        return;

    /*
     * Here are the scenarios we consider while updating the info label:
     * - No ISO selected,
     * - Unattended cannot determine OS type from the ISO,
     * - Unattended can determine the OS type from the ISO but cannot install it,
     * - User has disabled unattended explicitly,
     * - Unattended install will kick off.
     */
    QString strMessage;
    if (m_pNameAndSystemEditor->ISOImagePath().isEmpty())
        strMessage = UIWizardNewVM::tr("No ISO image is selected, the guest OS will need to be installed manually.");
    else if (pWizard->detectedOSTypeId().isEmpty())
        strMessage = UIWizardNewVM::tr("VirtualBox can't install an OS from the selected ISO. OS cannot be determined, the guest OS will need to be installed manually.");
    else if (!pWizard->detectedOSTypeId().isEmpty())
    {
        QString strType = gpGlobalSession->guestOSTypeManager().getDescription(pWizard->detectedOSTypeId());
        if (!pWizard->isUnattendedInstallSupported())
            strMessage = UIWizardNewVM::tr("Detected OS type: %1. %2")
                                           .arg(strType)
                .arg(UIWizardNewVM::tr("This OS can't be installed using Unattended Installation. "
                                       "The installation needs to be done manually."));

        else if (pWizard->skipUnattendedInstall())
            strMessage = UIWizardNewVM::tr("You have selected to skip unattended guest OS install, "
                                           "the guest OS will need to be installed manually.");
        else
            strMessage = UIWizardNewVM::tr("Detected OS type: %1. %2")
                                           .arg(strType)
                                           .arg(UIWizardNewVM::tr("VirtualBox will install the OS using an unattended installation when the VM is created. "
                                                                  "Supply the required information in the following steps."));
    }

    const QIcon icon = UIIconPool::iconSet(":/session_info_16px.png");
    const int iIconMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
    const qreal fDevicePixelRatio = gpDesktop->devicePixelRatio(m_pInfoLabel);
    m_pInfoLabel->registerPixmap(icon.pixmap(QSize(iIconMetric, iIconMetric), fDevicePixelRatio), "wizard://info");
    m_pInfoLabel->setText(QString("<img src='wizard://info' style=\"vertical-align:top\"> %1").arg(strMessage));
}

void UIWizardNewVMNameOSTypePage::initializePage()
{
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturnVoid(pWizard);
    AssertReturnVoid(m_pNameAndSystemEditor);

    sltRetranslateUI();

    /* Initialize this page's widgets etc: */
    m_pNameAndSystemEditor->setFocus();
    setEditionAndOSTypeSelectorsEnabled();
    UIWizardNewVMNameOSTypeCommon::setUnattendedCheckBoxEnable(m_pUnattendedCheckBox,
                                                               m_pNameAndSystemEditor->ISOImagePath(), isUnattendedInstallSupported());

    /* Initialize some of the wizard's parameters: */
    pWizard->setGuestOSFamilyId(m_pNameAndSystemEditor->familyId());
    pWizard->setGuestOSTypeId(m_pNameAndSystemEditor->typeId());
}

bool UIWizardNewVMNameOSTypePage::validatePage()
{
    /* Try to create machine folder: */
    return UIWizardNewVMNameOSTypeCommon::createMachineFolder(m_pNameAndSystemEditor, wizardWindow<UIWizardNewVM>());
}

void UIWizardNewVMNameOSTypePage::sltISOPathChanged(const QString &strPath)
{
    UIWizardNewVM *pWizard = qobject_cast<UIWizardNewVM*>(this->wizard());
    AssertReturnVoid(pWizard);

    pWizard->setISOFilePath(strPath);

    bool const fOsTypeFixed = UIWizardNewVMNameOSTypeCommon::guessOSTypeDetectedOSTypeString(m_pNameAndSystemEditor,
                                                                                             pWizard->detectedOSTypeId());
    if (fOsTypeFixed)
        m_userModifiedParameters << "GuestOSTypeFromISO";
    else /* Remove GuestOSTypeFromISO from the set if it is there: */
        m_userModifiedParameters.remove("GuestOSTypeFromISO");

    /* Update the global recent ISO path: */
    QFileInfo fileInfo(strPath);
    if (fileInfo.exists() && fileInfo.isReadable())
        gpMediumEnumerator->updateRecentlyUsedMediumListAndFolder(UIMediumDeviceType_DVD, strPath);

    /* Populate the editions selector: */
    if (m_pNameAndSystemEditor)
         m_pNameAndSystemEditor->setEditionNameAndIndices(pWizard->detectedWindowsImageNames(),
                                                          pWizard->detectedWindowsImageIndices());

    UIWizardNewVMNameOSTypeCommon::setUnattendedCheckBoxEnable(m_pUnattendedCheckBox,
                                                               m_pNameAndSystemEditor->ISOImagePath(),
                                                               isUnattendedInstallSupported());
    setEditionAndOSTypeSelectorsEnabled();
    updateInfoLabel();

    /* Disable OS type selector(s) to prevent user from changing guest OS type manually: */
    if (m_pNameAndSystemEditor)
    {
        /* Redetect the OS type using the name if detection or the step above failed: */
        if (!fOsTypeFixed)
            sltNameChanged(m_pNameAndSystemEditor->name());
    }

    emit completeChanged();
}

void UIWizardNewVMNameOSTypePage::sltGuestOSFamilyChanged(const QString &strGuestOSFamilyId)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    wizardWindow<UIWizardNewVM>()->setGuestOSFamilyId(strGuestOSFamilyId);
}

void UIWizardNewVMNameOSTypePage::sltSelectedEditionChanged(ulong uEditionIndex)
{
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturnVoid(pWizard);
    pWizard->setSelectedWindowImageIndex(uEditionIndex);
    /* Update the OS type since IUnattended updates the detected OS type after edition (image index) changes: */
    UIWizardNewVMNameOSTypeCommon::guessOSTypeDetectedOSTypeString(m_pNameAndSystemEditor, pWizard->detectedOSTypeId());
}

void UIWizardNewVMNameOSTypePage::sltUnattendedInstallEnableChanged(bool fEnable)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    m_userModifiedParameters << "SkipUnattendedInstall";
    wizardWindow<UIWizardNewVM>()->setSkipUnattendedInstall(!fEnable);
    /* Override OS type selectors when unattended is enabled: */
    if (fEnable)
    {
        UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
        UIWizardNewVMNameOSTypeCommon::guessOSTypeDetectedOSTypeString(m_pNameAndSystemEditor,
                                                                       pWizard->detectedOSTypeId());
    }
    setEditionAndOSTypeSelectorsEnabled();
    updateInfoLabel();
}

QWidget *UIWizardNewVMNameOSTypePage::createNameOSTypeWidgets()
{
    /* Prepare container widget: */
    QWidget *pContainerWidget = new QWidget;
    if (pContainerWidget)
    {
        /* Prepare layout: */
        m_pNameAndSystemLayout = new QGridLayout(pContainerWidget);
        if (m_pNameAndSystemLayout)
        {
            m_pNameAndSystemLayout->setContentsMargins(0, 0, 0, 0);

            /* Prepare Name and OS Type editor: */
            m_pNameAndSystemEditor = new UINameAndSystemEditor(0,
                                                               true /* fChooseName? */,
                                                               true /* fChoosePath? */,
                                                               true /* fChooseImage? */,
                                                               true /* fChooseEdition? */,
                                                               true /* fChooseType? */);
            if (m_pNameAndSystemEditor)
                m_pNameAndSystemLayout->addWidget(m_pNameAndSystemEditor, 0, 0, 1, 2);

            /* Prepare Unattended checkbox: */
            m_pUnattendedCheckBox = new QCheckBox;
            if (m_pUnattendedCheckBox)
            {
                m_pNameAndSystemLayout->addWidget(m_pUnattendedCheckBox, 1, 1);
                m_pUnattendedCheckBox->setChecked(false);
            }
            m_pInfoLabel = new QIRichTextLabel(pContainerWidget);
            if (m_pInfoLabel)
                m_pNameAndSystemLayout->addWidget(m_pInfoLabel, 2, 1);
        }
    }

    /* Return container widget: */
    return pContainerWidget;
}

void UIWizardNewVMNameOSTypePage::markWidgets() const
{
    if (m_pNameAndSystemEditor)
    {
        if (m_pNameAndSystemEditor->name().isEmpty())
            m_pNameAndSystemEditor->markNameEditor(m_pNameAndSystemEditor->name().isEmpty(),
                                                   UIWizardNewVM::tr("Virtual machine name cannot be empty"),
                                                   UIWizardNewVM::tr("Virtual machine name is valid"));
        else
            m_pNameAndSystemEditor->markNameEditor(!isMachineFolderUnique(),
                                                   UIWizardNewVM::tr("Virtual machine path is not unique"),
                                                   UIWizardNewVM::tr("Virtual machine name is valid"));

        m_pNameAndSystemEditor->markImageEditor(!UIWizardNewVMNameOSTypeCommon::checkISOFile(m_pNameAndSystemEditor->ISOImagePath()),
                                                UIWizardNewVM::tr("Invalid file path or unreadable file"),
                                                UIWizardNewVM::tr("File path is valid"));
    }
}

bool UIWizardNewVMNameOSTypePage::isUnattendedEnabled() const
{
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturn(pWizard, false);
    return pWizard->isUnattendedEnabled();
}

bool UIWizardNewVMNameOSTypePage::isUnattendedInstallSupported() const
{
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturn(pWizard, false);
    return pWizard->isUnattendedInstallSupported();
}

void  UIWizardNewVMNameOSTypePage::setEditionAndOSTypeSelectorsEnabled()
{
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturnVoid(pWizard);
    if (!m_pNameAndSystemEditor || !m_pUnattendedCheckBox)
        return;
    m_pNameAndSystemEditor->setEditionSelectorEnabled(   !m_pNameAndSystemEditor->isEditionsSelectorEmpty()
                                                      && m_pUnattendedCheckBox->isChecked());
    /* Disable OS type, version, subtype selectors if unattended is enabled: */
    if (pWizard->isUnattendedEnabled())
        m_pNameAndSystemEditor->setOSTypeStuffEnabled(false);
    else
        m_pNameAndSystemEditor->setOSTypeStuffEnabled(true);

}

bool UIWizardNewVMNameOSTypePage::isMachineFolderUnique() const
{
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturn(pWizard, false);

    if (QDir(m_pNameAndSystemEditor->fullPath()).exists())
    {
        /* Make sure that existing machine folder has not been created during this intantiation of the wizard.
         * This can happen, for example, when we come back to this page (via Back button): */
        if (pWizard->createdMachineFolder() != m_pNameAndSystemEditor->fullPath())
            return false;
    }
    return true;
}
