/* $Id: UIAddDiskEncryptionPasswordDialog.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIAddDiskEncryptionPasswordDialog class implementation.
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
#include <QAbstractTableModel>
#include <QHeaderView>
#include <QItemEditorFactory>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardItemEditorCreator>
#include <QTableView>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "QIStyledItemDelegate.h"
#include "UIAddDiskEncryptionPasswordDialog.h"
#include "UIIconPool.h"
#include "UIMedium.h"
#include "UIMediumEnumerator.h"
#include "UINotificationCenter.h"
#include "UITranslationEventListener.h"

/* Other VBox includes: */
#include <iprt/assert.h>


/** UIEncryptionDataTable field indexes. */
enum UIEncryptionDataTableSection
{
    UIEncryptionDataTableSection_Id,
    UIEncryptionDataTableSection_Password,
    UIEncryptionDataTableSection_Max
};

template<class T>
static QStringList toStringList(const QList<T> &list)
{
    QStringList l;
    foreach(const T &t, list)
        l << t.toString();
    return l;
}

/** QLineEdit extension used as
  * the embedded password editor for the UIEncryptionDataTable. */
class UIPasswordEditor : public QLineEdit
{
    Q_OBJECT;

    /** Holds the current password of the editor. */
    Q_PROPERTY(QString password READ password WRITE setPassword USER true);

signals:

    /** Notifies listeners about data should be committed. */
    void sigCommitData(QWidget *pThis);

    /** Notifies listeners about Enter/Return key triggering. */
    void sigEnterKeyTriggered();

public:

    /** Constructs password editor passing @a pParent to the base-class. */
    UIPasswordEditor(QWidget *pParent);

protected:

    /** Handles key-press @a pEvent. */
    virtual void keyPressEvent(QKeyEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Commits data to the listeners. */
    void sltCommitData() { emit sigCommitData(this); }

private:

    /** Prepares all. */
    void prepare();

    /** Property: Returns the current password of the editor. */
    QString password() const { return QLineEdit::text(); }
    /** Property: Defines the current @a strPassword of the editor. */
    void setPassword(const QString &strPassword) { QLineEdit::setText(strPassword); }
};


/** QAbstractTableModel extension used as
  * the data representation model for the UIEncryptionDataTable. */
class UIEncryptionDataModel : public QAbstractTableModel
{
    Q_OBJECT;

public:

    /** Constructs model passing @a pParent to the base-class.
      * @param  encryptedMedia  Brings the lists of medium ids (values) encrypted with passwords with ids (keys). */
    UIEncryptionDataModel(QObject *pParent, const EncryptedMediumMap &encryptedMedia);

    /** Returns the shallow copy of the encryption password map instance. */
    EncryptionPasswordMap encryptionPasswords() const { return m_encryptionPasswords; }

    /** Returns the row count, taking optional @a parent instead of root if necessary. */
    virtual int rowCount(const QModelIndex &parent = QModelIndex()) const RT_OVERRIDE RT_FINAL;
    /** Returns the column count, taking optional @a parent instead of root if necessary. */
    virtual int columnCount(const QModelIndex &parent = QModelIndex()) const RT_OVERRIDE RT_FINAL;

    /** Returns the @a index flags. */
    virtual Qt::ItemFlags flags(const QModelIndex &index) const RT_OVERRIDE RT_FINAL;

    /** Returns the header data for the @a iSection, @a orientation and @a iRole. */
    virtual QVariant headerData(int iSection, Qt::Orientation orientation, int iRole) const RT_OVERRIDE RT_FINAL;

    /** Returns the @a index data for the @a iRole. */
    virtual QVariant data(const QModelIndex &index, int iRole = Qt::DisplayRole) const RT_OVERRIDE RT_FINAL;
    /** Defines the @a index data for the @a iRole as @a value. */
    virtual bool setData(const QModelIndex &index, const QVariant &value, int iRole = Qt::EditRole) RT_OVERRIDE RT_FINAL;

private:

    /** Prepares all. */
    void prepare();

    /** Holds the encrypted medium map reference. */
    const EncryptedMediumMap &m_encryptedMedia;

    /** Holds the encryption password map instance. */
    EncryptionPasswordMap  m_encryptionPasswords;
};


/** QTableView extension used to
  * allow the UIAddDiskEncryptionPasswordDialog to enter
  * disk encryption passwords for particular password ids. */
class UIEncryptionDataTable : public QTableView
{
    Q_OBJECT;

signals:

    /** Notifies listeners about editor's Enter/Return key triggering. */
    void sigEditorEnterKeyTriggered();

public:

    /** Constructs table.
      * @param  encryptedMedia  Brings the lists of medium ids (values) encrypted with passwords with ids (keys). */
    UIEncryptionDataTable(const EncryptedMediumMap &encryptedMedia);
    /** Destructs table. */
    virtual ~UIEncryptionDataTable() RT_OVERRIDE;

    /** Returns the shallow copy of the encryption password map
      * acquired from the UIEncryptionDataModel instance. */
    EncryptionPasswordMap encryptionPasswords() const;

    /** Initiates the editor for the first index available. */
    void editFirstIndex();

private:

    /** Prepares all. */
    void prepare();
    /** Cleanups all. */
    void cleanup();

    /** Holds the encrypted medium map reference. */
    const EncryptedMediumMap &m_encryptedMedia;

    /** Holds the encryption-data model instance. */
    UIEncryptionDataModel *m_pModelEncryptionData;

    /** Holds the item editor factory instance. */
    QItemEditorFactory *m_pItemEditorFactory;
};


/*********************************************************************************************************************************
*   Class UIPasswordEditor implementation.                                                                                       *
*********************************************************************************************************************************/

UIPasswordEditor::UIPasswordEditor(QWidget *pParent)
    : QLineEdit(pParent)
{
    /* Prepare: */
    prepare();
}

void UIPasswordEditor::keyPressEvent(QKeyEvent *pEvent)
{
    /* Call to base-class: */
    QLineEdit::keyPressEvent(pEvent);

    /* Broadcast Enter/Return key press: */
    switch (pEvent->key())
    {
        case Qt::Key_Enter:
        case Qt::Key_Return:
            emit sigEnterKeyTriggered();
            pEvent->accept();
            break;
        default:
            break;
    }
}

void UIPasswordEditor::prepare()
{
    /* Make sure QIStyledDelegate aware of us: */
    setProperty("has_sigCommitData", true);
    setProperty("has_sigEnterKeyTriggered", true);
    /* Set echo mode: */
    setEchoMode(QLineEdit::Password);
    /* Listen for the text changes: */
    connect(this, &UIPasswordEditor::textChanged,
            this, &UIPasswordEditor::sltCommitData);
}


/*********************************************************************************************************************************
*   Class UIEncryptionDataModel implementation.                                                                                  *
*********************************************************************************************************************************/

UIEncryptionDataModel::UIEncryptionDataModel(QObject *pParent, const EncryptedMediumMap &encryptedMedia)
    : QAbstractTableModel(pParent)
    , m_encryptedMedia(encryptedMedia)
{
    /* Prepare: */
    prepare();
}

int UIEncryptionDataModel::rowCount(const QModelIndex &parent /* = QModelIndex() */) const
{
    Q_UNUSED(parent);
    return m_encryptionPasswords.size();
}

int UIEncryptionDataModel::columnCount(const QModelIndex &parent /* = QModelIndex() */) const
{
    Q_UNUSED(parent);
    return UIEncryptionDataTableSection_Max;
}

Qt::ItemFlags UIEncryptionDataModel::flags(const QModelIndex &index) const
{
    /* Check index validness: */
    if (!index.isValid())
        return Qt::NoItemFlags;
    /* Depending on column index: */
    switch (index.column())
    {
        case UIEncryptionDataTableSection_Id:       return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        case UIEncryptionDataTableSection_Password: return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
        default: break;
    }
    /* No flags by default: */
    return Qt::NoItemFlags;
}

QVariant UIEncryptionDataModel::headerData(int iSection, Qt::Orientation orientation, int iRole) const
{
    /* Check argument validness: */
    if (iRole != Qt::DisplayRole || orientation != Qt::Horizontal)
        return QVariant();
    /* Depending on column index: */
    switch (iSection)
    {
        case UIEncryptionDataTableSection_Id:       return UIAddDiskEncryptionPasswordDialog::tr("ID", "password table field");
        case UIEncryptionDataTableSection_Password: return UIAddDiskEncryptionPasswordDialog::tr("Password", "password table field");
        default: break;
    }
    /* Null value by default: */
    return QVariant();
}

QVariant UIEncryptionDataModel::data(const QModelIndex &index, int iRole /* = Qt::DisplayRole */) const
{
    /* Check argument validness: */
    if (!index.isValid())
        return QVariant();
    /* Depending on role: */
    switch (iRole)
    {
        case Qt::DisplayRole:
        {
            /* Depending on column index: */
            switch (index.column())
            {
                case UIEncryptionDataTableSection_Id:
                    return m_encryptionPasswords.keys().at(index.row());
                case UIEncryptionDataTableSection_Password:
                    return QString().fill('*', m_encryptionPasswords.value(m_encryptionPasswords.keys().at(index.row())).size());
                default:
                    return QVariant();
            }
            break;
        }
        case Qt::EditRole:
        {
            /* Depending on column index: */
            switch (index.column())
            {
                case UIEncryptionDataTableSection_Password:
                    return m_encryptionPasswords.value(m_encryptionPasswords.keys().at(index.row()));
                default:
                    return QVariant();
            }
            break;
        }
        case Qt::ToolTipRole:
        {
            /* We are generating tool-tip here and not in sltRetranslateUI() because of the tricky plural form handling,
             * but be quiet, it's safe enough because the tool-tip being re-acquired every time on mouse-hovering. */
            const QList<QUuid> encryptedMedia = m_encryptedMedia.values(m_encryptionPasswords.keys().at(index.row()));
            return UIAddDiskEncryptionPasswordDialog::tr("<nobr>Used by the following %n hard disk(s):</nobr><br>%1",
                                                         "This text is never used with n == 0. "
                                                         "Feel free to drop the %n where possible, "
                                                         "we only included it because of problems with Qt Linguist "
                                                         "(but the user can see how many hard drives are in the tool-tip "
                                                         "and doesn't need to be told).",
                                                         encryptedMedia.size())
                                                         .arg(toStringList(encryptedMedia).join("<br>"));
        }
        default:
            break;
    }
    /* Null value by default: */
    return QVariant();
}

bool UIEncryptionDataModel::setData(const QModelIndex &index, const QVariant &value, int iRole /* = Qt::EditRole */)
{
    /* Check argument validness: */
    if (!index.isValid() || iRole != Qt::EditRole)
        return false;
    /* Depending on column index: */
    switch (index.column())
    {
        case UIEncryptionDataTableSection_Password:
        {
            /* Update password: */
            const int iRow = index.row();
            const QString strPassword = value.toString();
            const QString strKey = m_encryptionPasswords.keys().at(iRow);
            m_encryptionPasswords[strKey] = strPassword;
            break;
        }
        default:
            break;
    }
    /* Nothing to set by default: */
    return false;
}

void UIEncryptionDataModel::prepare()
{
    /* Populate the map of passwords and statuses: */
    foreach (const QString &strPasswordId, m_encryptedMedia.keys())
        m_encryptionPasswords.insert(strPasswordId, QString());
}


/*********************************************************************************************************************************
*   Class UIEncryptionDataTable implementation.                                                                                  *
*********************************************************************************************************************************/

UIEncryptionDataTable::UIEncryptionDataTable(const EncryptedMediumMap &encryptedMedia)
    : m_encryptedMedia(encryptedMedia)
    , m_pModelEncryptionData(0)
    , m_pItemEditorFactory(0)
{
    prepare();
}

UIEncryptionDataTable::~UIEncryptionDataTable()
{
    cleanup();
}

EncryptionPasswordMap UIEncryptionDataTable::encryptionPasswords() const
{
    AssertPtrReturn(m_pModelEncryptionData, EncryptionPasswordMap());
    return m_pModelEncryptionData->encryptionPasswords();
}

void UIEncryptionDataTable::editFirstIndex()
{
    AssertPtrReturnVoid(m_pModelEncryptionData);
    /* Compose the password field index of the first available table record: */
    const QModelIndex index = m_pModelEncryptionData->index(0, UIEncryptionDataTableSection_Password);
    /* Navigate table to the corresponding index: */
    setCurrentIndex(index);
    /* Compose the fake mouse-event which will trigger the embedded editor: */
    QMouseEvent event(QEvent::MouseButtonPress, QPoint(), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    /* Initiate the embedded editor for the corresponding index: */
    edit(index, QAbstractItemView::SelectedClicked, &event);
}

void UIEncryptionDataTable::prepare()
{
    /* Create encryption-data model: */
    m_pModelEncryptionData = new UIEncryptionDataModel(this, m_encryptedMedia);
    if (m_pModelEncryptionData)
    {
        /* Assign configured model to table: */
        setModel(m_pModelEncryptionData);
    }

    /* Create item delegate: */
    QIStyledItemDelegate *pStyledItemDelegate = new QIStyledItemDelegate(this);
    if (pStyledItemDelegate)
    {
        /* Create new item editor factory: */
        m_pItemEditorFactory = new QItemEditorFactory;
        if (m_pItemEditorFactory)
        {
            /* Register UIPasswordEditor as the QString editor: */
            QStandardItemEditorCreator<UIPasswordEditor> *pQStringItemEditorCreator = new QStandardItemEditorCreator<UIPasswordEditor>();
            if (pQStringItemEditorCreator)
            m_pItemEditorFactory->registerEditor(QMetaType::QString, pQStringItemEditorCreator);

            /* Assign configured item editor factory to table delegate: */
            pStyledItemDelegate->setItemEditorFactory(m_pItemEditorFactory);
        }

        /* Assign configured item delegate to table: */
        delete itemDelegate();
        setItemDelegate(pStyledItemDelegate);

        /* Configure item delegate: */
        pStyledItemDelegate->setWatchForEditorDataCommits(true);
        pStyledItemDelegate->setWatchForEditorEnterKeyTriggering(true);
        connect(pStyledItemDelegate, &QIStyledItemDelegate::sigEditorEnterKeyTriggered,
                this, &UIEncryptionDataTable::sigEditorEnterKeyTriggered);
    }

    /* Configure table: */
    setTabKeyNavigation(false);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::CurrentChanged | QAbstractItemView::SelectedClicked);

    /* Configure headers: */
    verticalHeader()->hide();
    verticalHeader()->setDefaultSectionSize((int)(verticalHeader()->minimumSectionSize() * 1.33));
    horizontalHeader()->setStretchLastSection(false);
    horizontalHeader()->setSectionResizeMode(UIEncryptionDataTableSection_Id, QHeaderView::Interactive);
    horizontalHeader()->setSectionResizeMode(UIEncryptionDataTableSection_Password, QHeaderView::Stretch);
}

void UIEncryptionDataTable::cleanup()
{
    /* Cleanup item editor factory: */
    delete m_pItemEditorFactory;
    m_pItemEditorFactory = 0;
}


/*********************************************************************************************************************************
*   Class UIAddDiskEncryptionPasswordDialog implementation.                                                                      *
*********************************************************************************************************************************/

UIAddDiskEncryptionPasswordDialog::UIAddDiskEncryptionPasswordDialog(QWidget *pParent,
                                                                     const QString &strMachineName,
                                                                     const EncryptedMediumMap &encryptedMedia)
    : QDialog(pParent)
    , m_strMachineName(strMachineName)
    , m_encryptedMedia(encryptedMedia)
    , m_pLabelDescription(0)
    , m_pTableEncryptionData(0)
    , m_pButtonBox(0)
{
    /* Prepare: */
    prepare();
    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIAddDiskEncryptionPasswordDialog::sltRetranslateUI);

}

EncryptionPasswordMap UIAddDiskEncryptionPasswordDialog::encryptionPasswords() const
{
    AssertPtrReturn(m_pTableEncryptionData, EncryptionPasswordMap());
    return m_pTableEncryptionData->encryptionPasswords();
}

void UIAddDiskEncryptionPasswordDialog::sltRetranslateUI()
{
    /* Translate the dialog title: */
    setWindowTitle(tr("%1 - Disk Encryption").arg(m_strMachineName));

    /* Translate the description label: */
    AssertPtrReturnVoid(m_pLabelDescription);
    m_pLabelDescription->setText(tr("This virtual machine is password protected. "
                                    "Please enter the %n encryption password(s) below.",
                                    "This text is never used with n == 0. "
                                    "Feel free to drop the %n where possible, "
                                    "we only included it because of problems with Qt Linguist "
                                    "(but the user can see how many passwords are in the list "
                                    "and doesn't need to be told).",
                                    m_encryptedMedia.uniqueKeys().size()));
}

void UIAddDiskEncryptionPasswordDialog::accept()
{
    /* Validate passwords status: */
    foreach (const QString &strPasswordId, m_encryptedMedia.uniqueKeys())
    {
        const QUuid uMediumId = m_encryptedMedia.values(strPasswordId).first();
        const QString strPassword = m_pTableEncryptionData->encryptionPasswords().value(strPasswordId);
        if (!isPasswordValid(uMediumId, strPassword))
        {
            UINotificationMessage::warnAboutInvalidEncryptionPassword(strPasswordId);
            AssertPtrReturnVoid(m_pTableEncryptionData);
            m_pTableEncryptionData->setFocus();
            m_pTableEncryptionData->editFirstIndex();
            return;
        }
    }
    /* Call to base-class: */
    QDialog::accept();
}

void UIAddDiskEncryptionPasswordDialog::prepare()
{
    /* Configure self: */
    setWindowModality(Qt::WindowModal);

    /* Create main-layout: */
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    if (pMainLayout)
    {
        /* Create input-layout: */
        QVBoxLayout *pInputLayout = new QVBoxLayout;
        if (pInputLayout)
        {
            /* Create description label: */
            m_pLabelDescription = new QLabel;
            if (m_pLabelDescription)
            {
                /* Add label into layout: */
                pInputLayout->addWidget(m_pLabelDescription);
            }

            /* Create encryption-data table: */
            m_pTableEncryptionData = new UIEncryptionDataTable(m_encryptedMedia);
            if (m_pTableEncryptionData)
            {
                /* Configure encryption-data table: */
                connect(m_pTableEncryptionData, &UIEncryptionDataTable::sigEditorEnterKeyTriggered,
                        this, &UIAddDiskEncryptionPasswordDialog::sltEditorEnterKeyTriggered);
                m_pTableEncryptionData->setFocus();
                m_pTableEncryptionData->editFirstIndex();
                /* Add label into layout: */
                pInputLayout->addWidget(m_pTableEncryptionData);
            }

            /* Add layout into parent: */
            pMainLayout->addLayout(pInputLayout);
        }

        /* Create button-box: */
        m_pButtonBox = new QIDialogButtonBox;
        if (m_pButtonBox)
        {
            /* Configure button-box: */
            m_pButtonBox->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            connect(m_pButtonBox, &QIDialogButtonBox::accepted, this, &UIAddDiskEncryptionPasswordDialog::accept);
            connect(m_pButtonBox, &QIDialogButtonBox::rejected, this, &UIAddDiskEncryptionPasswordDialog::reject);

            /* Add button-box into layout: */
            pMainLayout->addWidget(m_pButtonBox);
        }
    }
}

/* static */
bool UIAddDiskEncryptionPasswordDialog::isPasswordValid(const QUuid &uMediumId, const QString strPassword)
{
    /* Look for the medium with passed ID: */
    const UIMedium uimedium = gpMediumEnumerator->medium(uMediumId);
    if (!uimedium.isNull())
    {
        /* Check wrapped medium for validity: */
        const CMedium medium = uimedium.medium();
        if (!medium.isNull())
        {
            /* Check whether the password is suitable for that medium: */
            medium.CheckEncryptionPassword(strPassword);
            return medium.isOk();
        }
    }
    /* False by default: */
    return false;
}


#include "UIAddDiskEncryptionPasswordDialog.moc"
