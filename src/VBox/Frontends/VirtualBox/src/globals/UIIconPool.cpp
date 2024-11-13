/* $Id: UIIconPool.cpp 107002 2024-11-13 12:21:13Z brent.paulson@oracle.com $ */
/** @file
 * VBox Qt GUI - UIIconPool class implementation.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include <QFile>
#include <QPainter>
#include <QStyle>
#include <QWidget>
#include <QWindow>

/* GUI includes: */
#include "UIIconPool.h"
#include "UIExtraDataManager.h"
#include "UIGlobalSession.h"
#include "UIGuestOSType.h"
#include "UIModalWindowManager.h"

/* COM includes: */
#include "CMachine.h"
#include <VBox/com/VirtualBox.h> /* Need GUEST_OS_ID_STR_X86 and friends. */

/* Other VBox includes: */
#include <iprt/assert.h>


/*********************************************************************************************************************************
*   Class UIIconPool implementation.                                                                                             *
*********************************************************************************************************************************/

/* static */
QPixmap UIIconPool::pixmap(const QString &strName)
{
    /* Reuse iconSet API: */
    QIcon icon = iconSet(strName);

    /* Return pixmap of first available size: */
    const int iHint = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
    return icon.pixmap(icon.availableSizes().value(0, QSize(iHint, iHint)));
}

/* static */
QIcon UIIconPool::iconSet(const QString &strNormal,
                          const QString &strDisabled /* = QString() */,
                          const QString &strActive /* = QString() */)
{
    /* Prepare fallback icon: */
    static QIcon nullIcon;

    /* Prepare icon: */
    QIcon icon;

    /* Add 'normal' pixmap: */
    AssertReturn(!strNormal.isEmpty(), nullIcon);
    addName(icon, strNormal, QIcon::Normal);

    /* Add 'disabled' pixmap (if any): */
    if (!strDisabled.isEmpty())
        addName(icon, strDisabled, QIcon::Disabled);

    /* Add 'active' pixmap (if any): */
    if (!strActive.isEmpty())
        addName(icon, strActive, QIcon::Active);

    /* Return icon: */
    return icon;
}

/* static */
QIcon UIIconPool::iconSetOnOff(const QString &strNormal, const QString strNormalOff,
                               const QString &strDisabled /* = QString() */, const QString &strDisabledOff /* = QString() */,
                               const QString &strActive /* = QString() */, const QString &strActiveOff /* = QString() */)
{
    /* Prepare fallback icon: */
    static QIcon nullIcon;

    /* Prepare icon: */
    QIcon icon;

    /* Add 'normal' on/off pixmaps: */
    AssertReturn(!strNormal.isEmpty(), nullIcon);
    addName(icon, strNormal, QIcon::Normal, QIcon::On);
    AssertReturn(!strNormalOff.isEmpty(), nullIcon);
    addName(icon, strNormalOff, QIcon::Normal, QIcon::Off);

    /* Add 'disabled' on/off pixmaps (if any): */
    if (!strDisabled.isEmpty())
        addName(icon, strDisabled, QIcon::Disabled, QIcon::On);
    if (!strDisabledOff.isEmpty())
        addName(icon, strDisabledOff, QIcon::Disabled, QIcon::Off);

    /* Add 'active' on/off pixmaps (if any): */
    if (!strActive.isEmpty())
        addName(icon, strActive, QIcon::Active, QIcon::On);
    if (!strActiveOff.isEmpty())
        addName(icon, strActiveOff, QIcon::Active, QIcon::Off);

    /* Return icon: */
    return icon;
}

/* static */
QIcon UIIconPool::iconSetFull(const QString &strNormal, const QString &strSmall,
                              const QString &strNormalDisabled /* = QString() */, const QString &strSmallDisabled /* = QString() */,
                              const QString &strNormalActive /* = QString() */, const QString &strSmallActive /* = QString() */)
{
    /* Prepare fallback icon: */
    static QIcon nullIcon;

    /* Prepare icon: */
    QIcon icon;

    /* Add 'normal' & 'small normal' pixmaps: */
    AssertReturn(!strNormal.isEmpty(), nullIcon);
    addName(icon, strNormal, QIcon::Normal);
    AssertReturn(!strSmall.isEmpty(), nullIcon);
    addName(icon, strSmall, QIcon::Normal);

    /* Add 'disabled' & 'small disabled' pixmaps (if any): */
    if (!strNormalDisabled.isEmpty())
        addName(icon, strNormalDisabled, QIcon::Disabled);
    if (!strSmallDisabled.isEmpty())
        addName(icon, strSmallDisabled, QIcon::Disabled);

    /* Add 'active' & 'small active' pixmaps (if any): */
    if (!strNormalActive.isEmpty())
        addName(icon, strNormalActive, QIcon::Active);
    if (!strSmallActive.isEmpty())
        addName(icon, strSmallActive, QIcon::Active);

    /* Return icon: */
    return icon;
}

/* static */
QIcon UIIconPool::iconSet(const QPixmap &normal,
                          const QPixmap &disabled /* = QPixmap() */,
                          const QPixmap &active /* = QPixmap() */)
{
    QIcon iconSet;

    Assert(!normal.isNull());
    iconSet.addPixmap(normal, QIcon::Normal);

    if (!disabled.isNull())
        iconSet.addPixmap(disabled, QIcon::Disabled);

    if (!active.isNull())
        iconSet.addPixmap(active, QIcon::Active);

    return iconSet;
}

/* static */
QIcon UIIconPool::defaultIcon(UIDefaultIconType defaultIconType, const QWidget *pWidget /* = 0 */)
{
    QIcon icon;
    QStyle *pStyle = pWidget ? pWidget->style() : QApplication::style();
    switch (defaultIconType)
    {
        case UIDefaultIconType_MessageBoxInformation:
        {
            icon = pStyle->standardIcon(QStyle::SP_MessageBoxInformation, 0, pWidget);
            break;
        }
        case UIDefaultIconType_MessageBoxQuestion:
        {
            icon = pStyle->standardIcon(QStyle::SP_MessageBoxQuestion, 0, pWidget);
            break;
        }
        case UIDefaultIconType_MessageBoxWarning:
        {
#ifdef VBOX_WS_MAC
            /* At least in Qt 4.3.4/4.4 RC1 SP_MessageBoxWarning is the application
             * icon. So change this to the critical icon. (Maybe this would be
             * fixed in a later Qt version) */
            icon = pStyle->standardIcon(QStyle::SP_MessageBoxCritical, 0, pWidget);
#else /* VBOX_WS_MAC */
            icon = pStyle->standardIcon(QStyle::SP_MessageBoxWarning, 0, pWidget);
#endif /* !VBOX_WS_MAC */
            break;
        }
        case UIDefaultIconType_MessageBoxCritical:
        {
            icon = pStyle->standardIcon(QStyle::SP_MessageBoxCritical, 0, pWidget);
            break;
        }
        case UIDefaultIconType_DialogCancel:
        {
            icon = pStyle->standardIcon(QStyle::SP_DialogCancelButton, 0, pWidget);
            if (icon.isNull())
                icon = iconSet(":/cancel_16px.png");
            break;
        }
        case UIDefaultIconType_DialogHelp:
        {
            icon = pStyle->standardIcon(QStyle::SP_DialogHelpButton, 0, pWidget);
            if (icon.isNull())
                icon = iconSet(":/help_16px.png");
            break;
        }
        case UIDefaultIconType_ArrowBack:
        {
            icon = pStyle->standardIcon(QStyle::SP_ArrowBack, 0, pWidget);
            if (icon.isNull())
                icon = iconSet(":/list_moveup_16px.png",
                               ":/list_moveup_disabled_16px.png");
            break;
        }
        case UIDefaultIconType_ArrowForward:
        {
            icon = pStyle->standardIcon(QStyle::SP_ArrowForward, 0, pWidget);
            if (icon.isNull())
                icon = iconSet(":/list_movedown_16px.png",
                               ":/list_movedown_disabled_16px.png");
            break;
        }
        default:
        {
            AssertMsgFailed(("Unknown default icon type!"));
            break;
        }
    }
    return icon;
}

/* static */
QPixmap UIIconPool::joinPixmaps(const QPixmap &pixmap1, const QPixmap &pixmap2)
{
    if (pixmap1.isNull())
        return pixmap2;
    if (pixmap2.isNull())
        return pixmap1;

    QPixmap result(pixmap1.width() + pixmap2.width() + 2,
                   qMax(pixmap1.height(), pixmap2.height()));
    result.fill(Qt::transparent);

    QPainter painter(&result);
    painter.drawPixmap(0, 0, pixmap1);
    painter.drawPixmap(pixmap1.width() + 2, result.height() - pixmap2.height(), pixmap2);
    painter.end();

    return result;
}

/* static */
void UIIconPool::addName(QIcon &icon, const QString &strName,
                         QIcon::Mode mode /* = QIcon::Normal */, QIcon::State state /* = QIcon::Off */)
{
    /* Prepare pixmap on the basis of passed value: */
    QPixmap pixmap(strName);
    /* Add pixmap: */
    icon.addPixmap(pixmap, mode, state);

    /* Parse name to prefix and suffix: */
    QString strPrefix = strName.section('.', 0, -2);
    QString strSuffix = strName.section('.', -1, -1);
    /* Prepare HiDPI pixmaps: */
    const QStringList aPixmapNames = QStringList() << (strPrefix + "_x2." + strSuffix)
                                                   << (strPrefix + "_x3." + strSuffix)
                                                   << (strPrefix + "_x4." + strSuffix);
    foreach (const QString &strPixmapName, aPixmapNames)
    {
        QPixmap pixmapHiDPI(strPixmapName);
        if (!pixmapHiDPI.isNull())
            icon.addPixmap(pixmapHiDPI, mode, state);
    }
}


/*********************************************************************************************************************************
*   Class UIIconPoolGeneral implementation.                                                                                      *
*********************************************************************************************************************************/

/* static */
UIIconPoolGeneral *UIIconPoolGeneral::s_pInstance = 0;

/* static */
void UIIconPoolGeneral::create()
{
    AssertReturnVoid(!s_pInstance);
    new UIIconPoolGeneral;
}

/* static */
void UIIconPoolGeneral::destroy()
{
    AssertPtrReturnVoid(s_pInstance);
    delete s_pInstance;
}

/* static */
UIIconPoolGeneral *UIIconPoolGeneral::instance()
{
    return s_pInstance;
}

UIIconPoolGeneral::UIIconPoolGeneral()
{
    /* Init instance: */
    s_pInstance = this;

    /* Prepare OS type icon-name hash: */
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Other"),                 ":/os_other.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Other"),                 ":/os_other.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_A64("Other"),                 ":/os_other.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("DOS"),                   ":/os_dos.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Netware"),               ":/os_netware.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("L4"),                    ":/os_l4.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Windows31"),             ":/os_win31.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Windows95"),             ":/os_win95.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Windows98"),             ":/os_win98.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("WindowsMe"),             ":/os_winme.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("WindowsNT3x"),           ":/os_winnt4.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("WindowsNT4"),            ":/os_winnt4.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Windows2000"),           ":/os_win2k.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("WindowsXP"),             ":/os_winxp.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("WindowsXP"),             ":/os_winxp.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Windows2003"),           ":/os_win2k3.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Windows2003"),           ":/os_win2k3.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("WindowsVista"),          ":/os_winvista.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("WindowsVista"),          ":/os_winvista.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Windows2008"),           ":/os_win2k8.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Windows2008"),           ":/os_win2k8.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Windows7"),              ":/os_win7.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Windows7"),              ":/os_win7.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Windows8"),              ":/os_win8.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Windows8"),              ":/os_win8.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Windows81"),             ":/os_win81.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Windows81"),             ":/os_win81.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Windows2012"),           ":/os_win2k12.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Windows10"),             ":/os_win10.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Windows10"),             ":/os_win10.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Windows11"),             ":/os_win11.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Windows2016"),           ":/os_win2k16.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Windows2019"),           ":/os_win2k19.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Windows2022"),           ":/os_win2k22.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Windows2025"),           ":/os_win2k25.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("WindowsNT"),             ":/os_win_other.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("WindowsNT"),             ":/os_win_other.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("OS2Warp3"),              ":/os_os2warp3.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("OS2Warp4"),              ":/os_os2warp4.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("OS2Warp45"),             ":/os_os2warp45.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("OS2eCS"),                ":/os_os2ecs.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("OS2ArcaOS"),             ":/os_os2_other.png"); /** @todo icon? */
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("OS21x"),                 ":/os_os2_other.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("OS2"),                   ":/os_os2_other.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Linux22"),               ":/os_linux22.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Linux24"),               ":/os_linux24.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Linux24"),               ":/os_linux24.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Linux26"),               ":/os_linux26.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Linux26"),               ":/os_linux26.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("ArchLinux"),             ":/os_archlinux.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("ArchLinux"),             ":/os_archlinux.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_A64("ArchLinux"),             ":/os_archlinux.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Debian"),                ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Debian"),                ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_A64("Debian"),                ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Debian31"),              ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Debian4"),               ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Debian4"),               ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Debian5"),               ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Debian5"),               ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Debian5"),               ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Debian5"),               ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Debian6"),               ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Debian6"),               ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Debian7"),               ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Debian7"),               ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Debian8"),               ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Debian8"),               ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Debian9"),               ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Debian9"),               ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_A64("Debian9"),               ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Debian10"),              ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Debian10"),              ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_A64("Debian10"),              ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Debian11"),              ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Debian11"),              ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_A64("Debian11"),              ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Debian12"),              ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Debian12"),              ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_A64("Debian12"),              ":/os_debian.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("OpenSUSE"),              ":/os_opensuse.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("OpenSUSE"),              ":/os_opensuse.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("OpenSUSE_Leap"),         ":/os_opensuse.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_A64("OpenSUSE_Leap"),         ":/os_opensuse.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("OpenSUSE_Tumbleweed"),   ":/os_opensuse.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("OpenSUSE_Tumbleweed"),   ":/os_opensuse.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_A64("OpenSUSE_Tumbleweed"),   ":/os_opensuse.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("SUSE_LE"),               ":/os_opensuse.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("SUSE_LE"),               ":/os_opensuse.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Fedora"),                ":/os_fedora.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Fedora"),                ":/os_fedora.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_A64("Fedora"),                ":/os_fedora.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Gentoo"),                ":/os_gentoo.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Gentoo"),                ":/os_gentoo.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Mandriva"),              ":/os_mandriva.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Mandriva"),              ":/os_mandriva.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("OpenMandriva_Lx"),       ":/os_mandriva.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("OpenMandriva_Lx"),       ":/os_mandriva.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("PCLinuxOS"),             ":/os_mandriva.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("PCLinuxOS"),             ":/os_mandriva.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Mageia"),                ":/os_mandriva.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Mageia"),                ":/os_mandriva.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("RedHat"),                ":/os_redhat.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("RedHat"),                ":/os_redhat.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("RedHat3"),               ":/os_redhat.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("RedHat3"),               ":/os_redhat.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("RedHat4"),               ":/os_redhat.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("RedHat4"),               ":/os_redhat.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("RedHat5"),               ":/os_redhat.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("RedHat5"),               ":/os_redhat.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("RedHat6"),               ":/os_redhat.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("RedHat6"),               ":/os_redhat.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("RedHat7"),               ":/os_redhat.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("RedHat8"),               ":/os_redhat.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("RedHat9"),               ":/os_redhat.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Turbolinux"),            ":/os_turbolinux.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Turbolinux"),            ":/os_turbolinux.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Ubuntu"),                ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Ubuntu"),                ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_A64("Ubuntu"),                ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Ubuntu10_LTS"),          ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Ubuntu10_LTS"),          ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Ubuntu10"),              ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Ubuntu10"),              ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Ubuntu11"),              ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Ubuntu11"),              ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Ubuntu12_LTS"),          ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Ubuntu12_LTS"),          ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Ubuntu12"),              ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Ubuntu12"),              ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Ubuntu13"),              ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Ubuntu13"),              ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Ubuntu14_LTS"),          ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Ubuntu14_LTS"),          ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Ubuntu14"),              ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Ubuntu14"),              ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Ubuntu15"),              ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Ubuntu15"),              ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Ubuntu16_LTS"),          ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Ubuntu16_LTS"),          ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Ubuntu16"),              ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Ubuntu16"),              ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Ubuntu17"),              ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Ubuntu17"),              ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Ubuntu18_LTS"),          ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Ubuntu18_LTS"),          ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Ubuntu18"),              ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Ubuntu18"),              ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Ubuntu19"),              ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Ubuntu19"),              ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Ubuntu20_LTS"),          ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Ubuntu20"),              ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Ubuntu21_LTS"),          ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Ubuntu21"),              ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Ubuntu22_LTS"),          ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Ubuntu22"),              ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_A64("Ubuntu22"),              ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Ubuntu23"),              ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_A64("Ubuntu23"),              ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Ubuntu24_LTS"),          ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_A64("Ubuntu24_LTS"),          ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Lubuntu"),               ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Lubuntu"),               ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Xubuntu"),               ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Xubuntu"),               ":/os_ubuntu.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Xandros"),               ":/os_xandros.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Xandros"),               ":/os_xandros.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Oracle"),                ":/os_oracle.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Oracle"),                ":/os_oracle.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Oracle3"),               ":/os_oracle.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Oracle3"),               ":/os_oracle.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Oracle4"),               ":/os_oracle.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Oracle4"),               ":/os_oracle.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Oracle5"),               ":/os_oracle.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Oracle5"),               ":/os_oracle.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Oracle6"),               ":/os_oracle.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Oracle6"),               ":/os_oracle.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Oracle7"),               ":/os_oracle.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Oracle8"),               ":/os_oracle.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Oracle9"),               ":/os_oracle.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_A64("Oracle9"),               ":/os_oracle.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Oracle"),                ":/os_oracle.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Oracle"),                ":/os_oracle.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_A64("Oracle"),                ":/os_oracle.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Linux"),                 ":/os_linux.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Linux"),                 ":/os_linux.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("FreeBSD"),               ":/os_freebsd.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("FreeBSD"),               ":/os_freebsd.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_A64("FreeBSD"),               ":/os_freebsd.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("OpenBSD"),               ":/os_openbsd.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("OpenBSD"),               ":/os_openbsd.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_A64("OpenBSD"),               ":/os_openbsd.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("NetBSD"),                ":/os_netbsd.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("NetBSD"),                ":/os_netbsd.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_A64("NetBSD"),                ":/os_netbsd.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Solaris"),               ":/os_solaris.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Solaris"),               ":/os_solaris.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Solaris10U8_or_later"),  ":/os_solaris.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Solaris10U8_or_later"),  ":/os_solaris.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("OpenSolaris"),           ":/os_oraclesolaris.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("OpenSolaris"),           ":/os_oraclesolaris.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("Solaris11"),             ":/os_oraclesolaris.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("QNX"),                   ":/os_qnx.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("MacOS"),                 ":/os_macosx.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("MacOS"),                 ":/os_macosx.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("MacOS106"),              ":/os_macosx.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("MacOS106"),              ":/os_macosx.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("MacOS107"),              ":/os_macosx.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("MacOS108"),              ":/os_macosx.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("MacOS109"),              ":/os_macosx.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("MacOS1010"),             ":/os_macosx.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("MacOS1011"),             ":/os_macosx.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("MacOS1012"),             ":/os_macosx.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("MacOS1013"),             ":/os_macosx.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("JRockitVE"),             ":/os_jrockitve.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X64("VBoxBS"),                ":/os_other.png");
    m_guestOSTypeIconNames.insert(GUEST_OS_ID_STR_X86("Cloud"),                 ":/os_cloud.png");

    /* Prepare warning/error icons: */
    m_pixWarning = defaultIcon(UIDefaultIconType_MessageBoxWarning).pixmap(16, 16);
    Assert(!m_pixWarning.isNull());
    m_pixError = defaultIcon(UIDefaultIconType_MessageBoxCritical).pixmap(16, 16);
    Assert(!m_pixError.isNull());
}

UIIconPoolGeneral::~UIIconPoolGeneral()
{
    /* Deinit instance: */
    s_pInstance = 0;
}

/* static */
QIcon UIIconPoolGeneral::overlayedIconSet(const QString &strGuestOSTypeId,
                                          const QString &strNormal,
                                          const QString &strDisabled /* = QString() */,
                                          const QString &strActive /* = QString() */)
{
    /* Prepare fallback icon: */
    static QIcon nullIcon;

    /* Prepare icon: */
    QIcon icon;

    /* Add 'normal' pixmap: */
    AssertReturn(!strNormal.isEmpty(), nullIcon);
    addNameAndOverlay(icon, strNormal, strGuestOSTypeId, QIcon::Normal);

    /* Add 'disabled' pixmap (if any): */
    if (!strDisabled.isEmpty())
        addNameAndOverlay(icon, strDisabled, strGuestOSTypeId, QIcon::Disabled);

    /* Add 'active' pixmap (if any): */
    if (!strActive.isEmpty())
        addNameAndOverlay(icon, strActive, strGuestOSTypeId, QIcon::Active);

    /* Return icon: */
    return icon;
}

/* static */
void UIIconPoolGeneral::addNameAndOverlay(QIcon &icon, const QString &strName, const QString &strGuestOSTypeId,
                                QIcon::Mode mode /* = QIcon::Normal */, QIcon::State state /* = QIcon::Off */ )
{
    /* Prepare pixmap on the basis of passed value: */
    QPixmap pixmap(strName);
    overlayArchitectureTextOnPixmap(determineOSArchString(strGuestOSTypeId), pixmap);
    /* Add pixmap: */
    icon.addPixmap(pixmap, mode, state);

    /* Parse name to prefix and suffix: */
    QString strPrefix = strName.section('.', 0, -2);
    QString strSuffix = strName.section('.', -1, -1);
    /* Prepare HiDPI pixmaps: */
    const QStringList aPixmapNames = QStringList() << (strPrefix + "_x2." + strSuffix)
                                                   << (strPrefix + "_x3." + strSuffix)
                                                   << (strPrefix + "_x4." + strSuffix);
    foreach (const QString &strPixmapName, aPixmapNames)
    {
        QPixmap pixmapHiDPI(strPixmapName);
        if (!pixmapHiDPI.isNull())
        {
            overlayArchitectureTextOnPixmap(determineOSArchString(strGuestOSTypeId), pixmapHiDPI);
            icon.addPixmap(pixmapHiDPI, mode, state);
        }
    }
}

QIcon UIIconPoolGeneral::userMachineIcon(const CMachine &comMachine) const
{
    /* Make sure machine is not NULL: */
    AssertReturn(comMachine.isNotNull(), QIcon());

    /* Get machine ID: */
    const QUuid uMachineId = comMachine.GetId();
    AssertReturn(comMachine.isOk(), QIcon());

    /* Prepare icon: */
    QIcon icon;

    /* 1. First, load icon from IMachine extra-data: */
    if (icon.isNull())
    {
        foreach (const QString &strIconName, gEDataManager->machineWindowIconNames(uMachineId))
            if (!strIconName.isEmpty() && QFile::exists(strIconName))
                icon.addFile(strIconName);
    }

    /* 2. Otherwise, load icon from IMachine interface itself: */
    if (icon.isNull())
    {
        const QVector<BYTE> byteVector = comMachine.GetIcon();
        AssertReturn(comMachine.isOk(), QPixmap());
        const QByteArray byteArray = QByteArray::fromRawData(reinterpret_cast<const char*>(byteVector.constData()), byteVector.size());
        const QImage image = QImage::fromData(byteArray);
        if (!image.isNull())
        {
            QPixmap pixmap = QPixmap::fromImage(image);
            const int iMinimumLength = qMin(pixmap.width(), pixmap.height());
            if (pixmap.width() != iMinimumLength || pixmap.height() != iMinimumLength)
                pixmap = pixmap.scaled(QSize(iMinimumLength, iMinimumLength), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            icon.addPixmap(pixmap);
        }
    }

    /* Return icon: */
    return icon;
}

QPixmap UIIconPoolGeneral::userMachinePixmap(const CMachine &comMachine, const QSize &size) const
{
    /* Acquire icon: */
    const QIcon icon = userMachineIcon(comMachine);

    /* Prepare pixmap: */
    QPixmap pixmap;

    /* Check whether we have valid icon: */
    if (!icon.isNull())
    {
        /* Get pixmap of requested size: */
        pixmap = icon.pixmap(size);
        /* And even scale it if size is not valid: */
        if (pixmap.size() != size)
            pixmap = pixmap.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    /* Return pixmap: */
    return pixmap;
}

QPixmap UIIconPoolGeneral::userMachinePixmapDefault(const CMachine &comMachine, QSize *pLogicalSize /* = 0 */) const
{
    /* Acquire icon: */
    const QIcon icon = userMachineIcon(comMachine);

    /* Prepare pixmap: */
    QPixmap pixmap;

    /* Check whether we have valid icon: */
    if (!icon.isNull())
    {
        /* Determine desired icon size: */
        const int iIconMetric = QApplication::style()->pixelMetric(QStyle::PM_LargeIconSize);
        const QSize iconSize = QSize(iIconMetric, iIconMetric);

        /* Pass up logical size if necessary: */
        if (pLogicalSize)
            *pLogicalSize = iconSize;

        /* Get pixmap of requested size: */
        pixmap = icon.pixmap(iconSize);
    }

    /* Return pixmap: */
    return pixmap;
}

QIcon UIIconPoolGeneral::guestOSTypeIcon(const QString &strOSTypeID) const
{
    /* Prepare fallback icon: */
    static QPixmap nullIcon(":/os_other.png");

    /* If we do NOT have that 'guest OS type' icon cached already: */
    if (!m_guestOSTypeIcons.contains(strOSTypeID))
    {
        /* Compose proper icon if we have that 'guest OS type' known: */
        if (m_guestOSTypeIconNames.contains(strOSTypeID))
            m_guestOSTypeIcons[strOSTypeID] = overlayedIconSet(strOSTypeID, m_guestOSTypeIconNames[strOSTypeID]);
        /* Assign fallback icon if we do NOT have that 'guest OS type' registered: */
        else if (!strOSTypeID.isNull())
            m_guestOSTypeIcons[strOSTypeID] = overlayedIconSet(strOSTypeID, m_guestOSTypeIconNames[GUEST_OS_ID_STR_X86("Other")]);
        /* Assign fallback icon if we do NOT have that 'guest OS type' known: */
        else
            m_guestOSTypeIcons[strOSTypeID] = iconSet(nullIcon);
    }

    /* Retrieve corresponding icon: */
    const QIcon &icon = m_guestOSTypeIcons[strOSTypeID];
    AssertMsgReturn(!icon.isNull(),
                    ("Undefined icon for type '%s'.", strOSTypeID.toLatin1().constData()),
                    nullIcon);

    /* Return icon: */
    return icon;
}

QPixmap UIIconPoolGeneral::guestOSTypePixmap(const QString &strOSTypeID, const QSize &size) const
{
    /* Acquire icon: */
    const QIcon icon = guestOSTypeIcon(strOSTypeID);

    /* Prepare pixmap: */
    QPixmap pixmap;

    /* Check whether we have valid icon: */
    if (!icon.isNull())
    {
        /* Get pixmap of requested size: */
        pixmap = icon.pixmap(size);
        /* And even scale it if size is not valid: */
        if (pixmap.size() != size)
            pixmap = pixmap.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    /* Return pixmap: */
    return pixmap;
}

QPixmap UIIconPoolGeneral::guestOSTypePixmapDefault(const QString &strOSTypeID, QSize *pLogicalSize /* = 0 */) const
{
    /* Acquire icon: */
    const QIcon icon = guestOSTypeIcon(strOSTypeID);

    /* Prepare pixmap: */
    QPixmap pixmap;

    /* Check whether we have valid icon: */
    if (!icon.isNull())
    {
        /* Determine desired icon size: */
        const int iIconMetric = QApplication::style()->pixelMetric(QStyle::PM_LargeIconSize);
        const QSize iconSize = QSize(iIconMetric, iIconMetric);

        /* Pass up logical size if necessary: */
        if (pLogicalSize)
            *pLogicalSize = iconSize;

        /* Get pixmap of requested size (take into account the DPI of the main shown window, if possible): */
        const qreal fDevicePixelRatio = windowManager().mainWindowShown() && windowManager().mainWindowShown()->windowHandle()
                                      ? windowManager().mainWindowShown()->windowHandle()->devicePixelRatio() : 1;
        pixmap = icon.pixmap(iconSize, fDevicePixelRatio);
    }

    /* Return pixmap: */
    return pixmap;
}

/* static */
QString UIIconPoolGeneral::determineOSArchString(const QString &strOSTypeId)
{
    bool fIs64Bit = gpGlobalSession->guestOSTypeManager().is64Bit(strOSTypeId);
    KPlatformArchitecture enmPlatformArchitecture = gpGlobalSession->guestOSTypeManager().getPlatformArchitecture(strOSTypeId);

    if (enmPlatformArchitecture == KPlatformArchitecture_ARM)
    {
        if (fIs64Bit)
            return QString("A64");
        else
            return QString("A32");
    }
    else if (enmPlatformArchitecture == KPlatformArchitecture_x86)
    {
        if (fIs64Bit)
            return QString("x64");
        else
            return QString("32");
    }
    return QString();
}

/* static */
void UIIconPoolGeneral::overlayArchitectureTextOnPixmap(const QString &strArch, QPixmap &pixmap)
{
#if 0
    /* First create a pixmap and draw a background and text on it: */
    QFontMetrics fontMetrics = qApp->fontMetrics();
    QRect rect(0, 0, fontMetrics.boundingRect(strArch).width(), fontMetrics.boundingRect(strArch).height());
    QPixmap textPixmap(rect.size());

    QPainter painter(&textPixmap);
    QFont font = qApp->font();
    font.setBold(true);
    painter.setFont(font);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setPen(QPen(Qt::black, 1));
    painter.setBrush(QColor(255, 255, 255, 80));
    painter.drawRect(rect);
    painter.drawText(rect, Qt::AlignHCenter | Qt::AlignVCenter, strArch);

    /* Now draw that^^ pixmap onto our original pixmap:*/
    QPainter originalPixmapPainter(&pixmap);
    originalPixmapPainter.setRenderHint(QPainter::Antialiasing);
    originalPixmapPainter.setRenderHint(QPainter::SmoothPixmapTransform);
    QRect targetRect(QPoint(0, 0), QSize(0.34 * pixmap.rect().width(), 0.3 * pixmap.rect().height()));
    originalPixmapPainter.drawPixmap(targetRect, textPixmap, textPixmap.rect());
#endif
#if 1
    QFont font = qApp->font();
    /* Set font' size wrt. @p pixmap height/dpr: */
    font.setPixelSize(0.31 * pixmap.height() / pixmap.devicePixelRatio());
    font.setBold(true);
    QPainter painter(&pixmap);
    painter.setFont(font);
    /* Make a rectangle just a bit larger than text's bounding rect. to have some spacing around the text: */
    int w = 1.2 * painter.fontMetrics().boundingRect(strArch).size().width();
    int h = 1.0 * painter.fontMetrics().boundingRect(strArch).size().height();
    QRect textRect(QPoint(0, 0), QSize(w, h));
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setPen(QPen(Qt::black, 1));
    painter.setBrush(QColor(255, 255, 255, 200));
    /* x and y radii are relative to rectangle's width and height and should be in [0, 100]: */
    painter.drawRoundedRect(textRect, 50 /* xRadius */, 50 /* yRadius*/, Qt::RelativeSize);
    painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignVCenter, strArch);
#endif
}
