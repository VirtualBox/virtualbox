/* $Id: UIWizardDiskEditors.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardDiskEditors class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_editors_UIWizardDiskEditors_h
#define FEQT_INCLUDED_SRC_wizards_editors_UIWizardDiskEditors_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QComboBox>
#include <QIcon>
#include <QGroupBox>
#include <QVector>

/* Forward declarations: */
class CMediumFormat;
class QButtonGroup;
class QCheckBox;
class QGridLayout;
class QLabel;
class QVBoxLayout;
class QIRichTextLabel;
class QILineEdit;
class QIToolButton;
class UIFilePathSelector;
class UIHostnameDomainNameEditor;
class UIPasswordLineEdit;
class UIUserNamePasswordEditor;
class UIMediumSizeEditor;

/* Other VBox includes: */
#include "CMediumFormat.h"

/** A set of static utility functions used by several wizard in the context of virtual media. */
namespace UIWizardDiskEditors
{
    /** Makes sure that @strFileName is suffixed witht the @strExtension. */
    SHARED_LIBRARY_STUFF QString appendExtension(const QString &strName, const QString &strExtension);
    SHARED_LIBRARY_STUFF QString constructMediumFilePath(const QString &strFileName, const QString &strPath);
    SHARED_LIBRARY_STUFF bool checkFATSizeLimitation(const qulonglong uVariant, const QString &strMediumPath, const qulonglong uSize);
    SHARED_LIBRARY_STUFF QString openFileDialogForDiskFile(const QString &strInitialPath, const CMediumFormat &comMediumFormat,
                                             KDeviceType enmDeviceType, QWidget *pParent);
    /** Attempts to find a file extention for the device type @p enmDeviceType within the extensions
      * returned by CMediumFormat::DescribeFileExtensions(..). */
    SHARED_LIBRARY_STUFF  QString defaultExtension(const CMediumFormat &mediumFormatRef, KDeviceType enmDeviceType);
    /** Removes the file extension from @strFileName if @p formatExtensions contains it. */
    SHARED_LIBRARY_STUFF QString stripFormatExtension(const QString &strFileName,
                                                      const QStringList &formatExtensions);
};

class SHARED_LIBRARY_STUFF UIDiskVariantWidget : public QWidget
{
    Q_OBJECT;

signals:

    void sigMediumVariantChanged(qulonglong uVariant);

public:

    UIDiskVariantWidget(QWidget *pParent = 0);
    /** Enable/disable medium variant radio button depending on the capabilities of the medium format. */
    void updateMediumVariantWidgetsAfterFormatChange(const CMediumFormat &mediumFormat);
    qulonglong mediumVariant() const;
    void setMediumVariant(qulonglong uMediumVariant);
    bool isComplete() const;

    bool isCreateDynamicPossible() const;
    bool isCreateFixedPossible() const;
    bool isCreateSplitPossible() const;

private slots:

    void sltVariantChanged();
    void sltRetranslateUI();

private:

    void prepare();

    QCheckBox *m_pFixedCheckBox;
    QCheckBox *m_pSplitBox;
    bool m_fIsCreateDynamicPossible;
    bool m_fIsCreateFixedPossible;
    bool m_fIsCreateSplitPossible;
};


class SHARED_LIBRARY_STUFF UIMediumSizeAndPathGroupBox : public QGroupBox
{
    Q_OBJECT;

signals:

    void sigMediumSizeChanged(qulonglong uSize);
    void sigMediumPathChanged(const QString &strPath);
    void sigMediumLocationButtonClicked();

public:

    UIMediumSizeAndPathGroupBox(bool fExpertMode, QWidget *pParent, qulonglong uMinimumMediumSize);
    /** Returns name of the medium file without extension and path. */
    QString mediumName() const;
    /** Returns the file pat of the medium file including file name and extension. */
    QString mediumFilePath() const;
    void setMediumFilePath(const QString &strMediumPath);
    /** Returns path of the medium file without the file name. */
    QString mediumPath() const;
    /** Checks if the file extension is correct. Fixs it if necessary. */
    void updateMediumPath(const CMediumFormat &mediumFormat, const QStringList &formatExtensions, KDeviceType enmDeviceType);
    qulonglong mediumSize() const;
    void setMediumSize(qulonglong uSize);

    bool filePathUnique() const;
    bool pathExists() const;

private slots:

    void sltRetranslateUI();

private:

    void prepare(qulonglong uMinimumMediumSize);

    QILineEdit *m_pLocationEditor;
    QIToolButton *m_pLocationOpenButton;
    UIMediumSizeEditor *m_pMediumSizeEditor;
    QIRichTextLabel *m_pLocationLabel;
    QIRichTextLabel *m_pSizeLabel;
    bool m_fExpertMode;
};

/** Base class for the widgets used to select virtual medium format. It implements mutual functioanlity
  * like finding name, extension etc for a CMediumFormat and device type. */
class SHARED_LIBRARY_STUFF UIDiskFormatBase
{
public:

    UIDiskFormatBase(KDeviceType enmDeviceType, bool fExpertMode);
    virtual ~UIDiskFormatBase();
    virtual CMediumFormat mediumFormat() const = 0;
    virtual void setMediumFormat(const CMediumFormat &mediumFormat) = 0;

    const CMediumFormat &VDIMediumFormat() const;
    QStringList formatExtensions() const;

protected:

    struct Format
    {
        CMediumFormat m_comFormat;
        QString       m_strExtension;
        bool          m_fPreferred;
        Format(const CMediumFormat &comFormat,
               const QString &strExtension, bool fPreferred)
               : m_comFormat(comFormat)
               , m_strExtension(strExtension)
               , m_fPreferred(fPreferred){}
        Format(){}
    };

    void addFormat(CMediumFormat medFormat, bool fPreferred = false);
    void populateFormats();
    bool isExpertMode() const;
    QVector<Format> m_formatList;

private:

    CMediumFormat m_comVDIMediumFormat;
    KDeviceType m_enmDeviceType;
    bool m_fExpertMode;
};

class SHARED_LIBRARY_STUFF UIDiskFormatsGroupBox : public QWidget, public UIDiskFormatBase
{
    Q_OBJECT;

signals:

    void sigMediumFormatChanged();

public:

    UIDiskFormatsGroupBox(bool fExpertMode, KDeviceType enmDeviceType, QWidget *pParent = 0);
    virtual CMediumFormat mediumFormat() const RT_OVERRIDE RT_FINAL;
    virtual void setMediumFormat(const CMediumFormat &mediumFormat) RT_OVERRIDE RT_FINAL;

private slots:

    void sltRetranslateUI();

private:

    void prepare();
    void createFormatWidgets();

    QButtonGroup *m_pFormatButtonGroup;
    QVBoxLayout *m_pMainLayout;
};

class SHARED_LIBRARY_STUFF UIDiskFormatsComboBox : public QComboBox, public UIDiskFormatBase
{
    Q_OBJECT;

signals:

    void sigMediumFormatChanged();

public:

    UIDiskFormatsComboBox(bool fExpertMode, KDeviceType enmDeviceType, QWidget *pParent = 0);
    virtual CMediumFormat mediumFormat() const RT_OVERRIDE RT_FINAL;
    virtual void setMediumFormat(const CMediumFormat &mediumFormat) RT_OVERRIDE RT_FINAL;

private slots:

    void sltRetranslateUI();

private:

    void prepare();
};

#endif /* !FEQT_INCLUDED_SRC_wizards_editors_UIWizardDiskEditors_h */
