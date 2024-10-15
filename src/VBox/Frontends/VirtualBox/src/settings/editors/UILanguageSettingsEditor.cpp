/* $Id: UILanguageSettingsEditor.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UILanguageSettingsEditor class implementation.
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
#include <QDir>
#include <QHeaderView>
#include <QPainter>
#include <QRegularExpression>
#include <QTranslator>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIRichTextLabel.h"
#include "QITreeWidget.h"
#include "UILanguageSettingsEditor.h"
#include "UITranslator.h"

/* Other VBox includes: */
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/path.h>


/** QITreeWidgetItem subclass representing language tree-widget item. */
class UILanguageItem : public QITreeWidgetItem
{
    Q_OBJECT;

public:

    /** Constructs language tree-widget item passing @a pParent to the base-class.
      * @param  translator  Brings the translator this item is related to.
      * @param  strId       Brings the language ID this item is related to.
      * @param  fBuiltIn    Brings whether the language this item related to is built in. */
    UILanguageItem(QITreeWidget *pParent, const QTranslator &translator,
                   const QString &strId, bool fBuiltIn = false);
    /** Constructs language tree-widget item passing @a pParent to the base-class.
      * @param  strId       Brings the language ID this item is related to.
      * @note   This is a constructor for an invalid language ID (i.e. when a
      *         language file is missing or corrupt). */
    UILanguageItem(QITreeWidget *pParent, const QString &strId);
    /** Constructs language tree-widget item passing @a pParent to the base-class.
      * @note   This is a constructor for a default language ID
      *         (column 1 will be set to QString()). */
    UILanguageItem(QITreeWidget *pParent);

    /** Returns whether this item is for built in language. */
    bool isBuiltIn() const { return m_fBuiltIn; }

    /** Returns whether this item is less than @a another one. */
    bool operator<(const QTreeWidgetItem &another) const RT_OVERRIDE RT_FINAL;

private:

    /** Performs translation using passed @a translator for a
      * passed @a pContext, @a pSourceText and @a pComment. */
    QString tratra(const QTranslator &translator, const char *pContext,
                   const char *pSourceText, const char *pComment);

    /** Holds whether this item is for built in language. */
    bool  m_fBuiltIn;
};


/*********************************************************************************************************************************
*   Class UILanguageItem implementation.                                                                                         *
*********************************************************************************************************************************/

UILanguageItem::UILanguageItem(QITreeWidget *pParent, const QTranslator &translator,
                               const QString &strId, bool fBuiltIn /* = false */)
    : QITreeWidgetItem(pParent)
    , m_fBuiltIn(fBuiltIn)
{
    Assert(!strId.isEmpty());

    /* Note: context/source/comment arguments below must match strings used in UITranslator::languageName() and friends
     *       (the latter are the source of information for the lupdate tool that generates translation files). */

    const QString strNativeLanguage = tratra(translator, "@@@", "English", "Native language name");
    const QString strNativeCountry = tratra(translator, "@@@", "--", "Native language country name "
                                                                     "(empty if this language is for all countries)");

    const QString strEnglishLanguage = tratra(translator, "@@@", "English", "Language name, in English");
    const QString strEnglishCountry = tratra(translator, "@@@", "--", "Language country name, in English "
                                                                      "(empty if native country name is empty)");

    const QString strTranslatorsName = tratra(translator, "@@@", "Oracle Corporation", "Comma-separated list of translators");

    QString strItemName = strNativeLanguage;
    QString strLanguageName = strEnglishLanguage;

    if (!m_fBuiltIn)
    {
        if (strNativeCountry != "--")
            strItemName += " (" + strNativeCountry + ")";

        if (strEnglishCountry != "--")
            strLanguageName += " (" + strEnglishCountry + ")";

        if (strItemName != strLanguageName)
            strLanguageName = strItemName + " / " + strLanguageName;
    }
    else
    {
        strItemName += tr(" (built-in)", "Language");
        strLanguageName += tr(" (built-in)", "Language");
    }

    setText(0, strItemName);
    setText(1, strId);
    setText(2, strLanguageName);
    setText(3, strTranslatorsName);

    /* Current language appears in bold: */
    if (text(1) == UITranslator::languageId())
    {
        QFont fnt = font(0);
        fnt.setBold(true);
        setFont(0, fnt);
    }
}

UILanguageItem::UILanguageItem(QITreeWidget *pParent, const QString &strId)
    : QITreeWidgetItem(pParent)
    , m_fBuiltIn(false)
{
    Assert(!strId.isEmpty());

    setText(0, QString("<%1>").arg(strId));
    setText(1, strId);
    setText(2, tr("<unavailable>", "Language"));
    setText(3, tr("<unknown>", "Author(s)"));

    /* Invalid language appears in italic: */
    QFont fnt = font(0);
    fnt.setItalic(true);
    setFont(0, fnt);
}

UILanguageItem::UILanguageItem(QITreeWidget *pParent)
    : QITreeWidgetItem(pParent)
    , m_fBuiltIn(false)
{
    setText(0, tr("Default", "Language"));
    setText(1, QString());
    /* Empty strings of some reasonable length to prevent the info part
     * from being shrinked too much when the list wants to be wider */
    setText(2, "                ");
    setText(3, "                ");

    /* Default language item appears in italic: */
    QFont fnt = font(0);
    fnt.setItalic(true);
    setFont(0, fnt);
}

bool UILanguageItem::operator<(const QTreeWidgetItem &another) const
{
    QString thisId = text(1);
    QString thatId = another.text(1);
    if (thisId.isNull())
        return true;
    if (thatId.isNull())
        return false;
    if (m_fBuiltIn)
        return true;
    if (another.type() == ItemType && ((UILanguageItem*)&another)->m_fBuiltIn)
        return false;
    return QITreeWidgetItem::operator<(another);
}

QString UILanguageItem::tratra(const QTranslator &translator, const char *pContext,
                               const char *pSourceText, const char *pComment)
{
    QString strMsg = translator.translate(pContext, pSourceText, pComment);
    /* Return the source text if no translation is found: */
    if (strMsg.isEmpty())
        strMsg = QString(pSourceText);
    return strMsg;
}


/*********************************************************************************************************************************
*   Class UILanguageSettingsEditor implementation.                                                                               *
*********************************************************************************************************************************/

UILanguageSettingsEditor::UILanguageSettingsEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent, true /* show in basic mode? */)
    , m_fPolished(false)
    , m_pTreeWidget(0)
    , m_pLabelInfo(0)
{
    prepare();
}

void UILanguageSettingsEditor::setValue(const QString &strValue)
{
    /* Update cached value and
     * tree-widget if value has changed: */
    if (m_strValue != strValue)
    {
        m_strValue = strValue;
        if (m_pTreeWidget)
            reloadLanguageTree(m_strValue);
    }
}

QString UILanguageSettingsEditor::value() const
{
    QTreeWidgetItem *pCurrentItem = m_pTreeWidget ? m_pTreeWidget->currentItem() : 0;
    return pCurrentItem ? pCurrentItem->text(1) : m_strValue;
}

void UILanguageSettingsEditor::sltRetranslateUI()
{
    /* Translate tree-widget: */
    if (m_pTreeWidget)
    {
        m_pTreeWidget->setWhatsThis(tr("Lists all available user interface languages. The effective language is written "
                                       "in bold. Select Default to reset to the system default language."));

        /* Translate tree-widget header: */
        QTreeWidgetItem *pTreeWidgetHeaderItem = m_pTreeWidget->headerItem();
        if (pTreeWidgetHeaderItem)
        {
            pTreeWidgetHeaderItem->setText(3, tr("Author"));
            pTreeWidgetHeaderItem->setText(2, tr("Language"));
            pTreeWidgetHeaderItem->setText(1, tr("Id"));
            pTreeWidgetHeaderItem->setText(0, tr("Name"));
        }

        /* Update tree-widget contents finally: */
        reloadLanguageTree(m_strValue);
    }
}

void UILanguageSettingsEditor::showEvent(QShowEvent *pEvent)
{
    /* Call to base-class: */
    UIEditor::showEvent(pEvent);

    /* Polish if necessary: */
    if (!m_fPolished)
    {
        polishEvent(pEvent);
        m_fPolished = true;
    }
}

void UILanguageSettingsEditor::polishEvent(QShowEvent * /* pEvent */)
{
    /* Remember current info-label width: */
    m_pLabelInfo->setMinimumTextWidth(m_pLabelInfo->width());
}

void UILanguageSettingsEditor::sltHandleItemPainting(QTreeWidgetItem *pItem, QPainter *pPainter)
{
    /* We are always expecting an item: */
    AssertPtrReturnVoid(pItem);
    AssertReturnVoid(pItem->type() == QITreeWidgetItem::ItemType);

    /* An item of required type: */
    QITreeWidgetItem *pItemOfRequiredType = QITreeWidgetItem::toItem(pItem);
    AssertPtrReturnVoid(pItemOfRequiredType);

    /* A language item to be honest :) */
    UILanguageItem *pLanguageItem = qobject_cast<UILanguageItem*>(pItemOfRequiredType);
    AssertPtrReturnVoid(pLanguageItem);

    /* For built in language item: */
    if (pLanguageItem->isBuiltIn())
    {
        /* We are drawing a separator line in the tree: */
        const QRect rect = m_pTreeWidget->visualItemRect(pLanguageItem);
        pPainter->setPen(m_pTreeWidget->palette().color(QPalette::Window));
        pPainter->drawLine(rect.x(), rect.y() + rect.height() - 1,
                           rect.x() + rect.width(), rect.y() + rect.height() - 1);
    }
}

void UILanguageSettingsEditor::sltHandleCurrentItemChange(QTreeWidgetItem *pCurrentItem)
{
    /* Make sure item chosen: */
    if (!pCurrentItem)
        return;

    /* Disable labels for the Default language item: */
    const bool fEnabled = !pCurrentItem->text (1).isNull();
    m_pLabelInfo->setEnabled(fEnabled);
    m_pLabelInfo->setText(QString("<table>"
                                  "<tr><td>%1&nbsp;</td><td>%2</td></tr>"
                                  "<tr><td>%3&nbsp;</td><td>%4</td></tr>"
                                  "</table>")
                                  .arg(tr("Language:"))
                                  .arg(pCurrentItem->text(2))
                                  .arg(tr("Author(s):"))
                                  .arg(pCurrentItem->text(3)));
}

void UILanguageSettingsEditor::prepare()
{
    /* Prepare main layout: */
    QVBoxLayout *pLayoutMain = new QVBoxLayout(this);
    if (pLayoutMain)
    {
        pLayoutMain->setContentsMargins(0, 0, 0, 0);

        /* Prepare tree-widget: */
        m_pTreeWidget = new QITreeWidget(this);
        if (m_pTreeWidget)
        {
            m_pTreeWidget->header()->hide();
            m_pTreeWidget->setColumnCount(4);
            m_pTreeWidget->hideColumn(1);
            m_pTreeWidget->hideColumn(2);
            m_pTreeWidget->hideColumn(3);
            m_pTreeWidget->setRootIsDecorated(false);

            pLayoutMain->addWidget(m_pTreeWidget);
        }

        /* Prepare info label: */
        m_pLabelInfo = new QIRichTextLabel(this);
        if (m_pLabelInfo)
        {
            m_pLabelInfo->setWordWrapMode(QTextOption::WordWrap);
            m_pLabelInfo->setMinimumHeight(QFontMetrics(m_pLabelInfo->font(), m_pLabelInfo).height() * 5);

            pLayoutMain->addWidget(m_pLabelInfo);
        }
    }

    /* Prepare connections: */
    connect(m_pTreeWidget, &QITreeWidget::painted, this, &UILanguageSettingsEditor::sltHandleItemPainting);
    connect(m_pTreeWidget, &QITreeWidget::currentItemChanged, this, &UILanguageSettingsEditor::sltHandleCurrentItemChange);

    /* Apply language settings: */
    sltRetranslateUI();
}

void UILanguageSettingsEditor::reloadLanguageTree(const QString &strLanguageId)
{
    /* Clear languages tree: */
    m_pTreeWidget->clear();

    /* Load languages tree: */
    char szNlsPath[RTPATH_MAX];
    const int rc = RTPathAppPrivateNoArch(szNlsPath, sizeof(szNlsPath));
    AssertRC(rc);
    const QString strNlsPath = QString(szNlsPath) + UITranslator::vboxLanguageSubDirectory();
    QDir nlsDir(strNlsPath);
    QStringList files = nlsDir.entryList(QStringList(QString("%1*%2").arg(UITranslator::vboxLanguageFileBase(),
                                                                          UITranslator::vboxLanguageFileExtension())),
                                         QDir::Files);

    QTranslator translator;
    /* Add the default language: */
    new UILanguageItem(m_pTreeWidget);
    /* Add the built-in language: */
    new UILanguageItem(m_pTreeWidget, translator, UITranslator::vboxBuiltInLanguageName(), true /* built-in */);
    /* Add all existing languages */
    for (QStringList::Iterator it = files.begin(); it != files.end(); ++it)
    {
        QString strFileName = *it;
        const QRegularExpression re(UITranslator::vboxLanguageFileBase() + UITranslator::vboxLanguageIdRegExp());
        const QRegularExpressionMatch mt = re.match(strFileName);
        if (!mt.hasMatch())
            continue;

        /* Skip any English version, cause this is extra handled: */
        QString strLanguage = mt.captured(2);
        if (strLanguage.toLower() == "en")
            continue;

        bool fLoadOk = translator.load(strFileName, strNlsPath);
        if (!fLoadOk)
            continue;

        new UILanguageItem(m_pTreeWidget, translator, mt.captured(1));
    }

    /* Adjust selector list: */
    m_pTreeWidget->resizeColumnToContents(0);

    /* Search for necessary language: */
    QList<QTreeWidgetItem*> itemsList = m_pTreeWidget->findItems(strLanguageId, Qt::MatchExactly, 1);
    QTreeWidgetItem *pItem = itemsList.isEmpty() ? 0 : itemsList[0];
    if (!pItem)
    {
        /* Add an pItem for an invalid language to represent it in the list: */
        pItem = new UILanguageItem(m_pTreeWidget, strLanguageId);
        m_pTreeWidget->resizeColumnToContents(0);
    }
    Assert(pItem);
    if (pItem)
        m_pTreeWidget->setCurrentItem(pItem);

    m_pTreeWidget->sortItems(0, Qt::AscendingOrder);
    m_pTreeWidget->scrollToItem(pItem);
}


#include "UILanguageSettingsEditor.moc"
