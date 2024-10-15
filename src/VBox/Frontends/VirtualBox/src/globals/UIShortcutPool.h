/* $Id: UIShortcutPool.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIShortcutPool class declaration.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_globals_UIShortcutPool_h
#define FEQT_INCLUDED_SRC_globals_UIShortcutPool_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QKeySequence>
#include <QObject>
#include <QMap>

/* GUI includes: */
#include "UIDefs.h"
#include "UILibraryDefs.h"

/* Forward declarations: */
class QKeySequence;
class QString;
class UIActionPool;
class UIAction;


/** Shortcut descriptor prototype. */
class SHARED_LIBRARY_STUFF UIShortcut
{
public:

    /** Constructs empty shortcut descriptor. */
    UIShortcut()
        : m_strScope(QString())
        , m_strDescription(QString())
        , m_sequences(QList<QKeySequence>())
        , m_defaultSequence(QKeySequence())
        , m_standardSequence(QKeySequence())
    {}
    /** Constructs shortcut descriptor.
      * @param  strScope          Brings the shortcut scope.
      * @param  strDescription    Brings the shortcut description.
      * @param  sequences         Brings the shortcut sequences.
      * @param  defaultSequence   Brings the default shortcut sequence.
      * @param  standardSequence  Brings the standard shortcut sequence. */
    UIShortcut(const QString &strScope,
               const QString &strDescription,
               const QList<QKeySequence> &sequences,
               const QKeySequence &defaultSequence,
               const QKeySequence &standardSequence)
        : m_strScope(strScope)
        , m_strDescription(strDescription)
        , m_sequences(sequences)
        , m_defaultSequence(defaultSequence)
        , m_standardSequence(standardSequence)
    {}

    /** Defines the shortcut @a strScope. */
    void setScope(const QString &strScope);
    /** Returns the shortcut scope. */
    const QString &scope() const;

    /** Defines the shortcut @a strDescription. */
    void setDescription(const QString &strDescription);
    /** Returns the shortcut description. */
    const QString &description() const;

    /** Defines the shortcut @a sequences. */
    void setSequences(const QList<QKeySequence> &sequences);
    /** Returns the shortcut sequences. */
    const QList<QKeySequence> &sequences() const;

    /** Defines the default shortcut @a sequence. */
    void setDefaultSequence(const QKeySequence &sequence);
    /** Returns the default shortcut sequence. */
    const QKeySequence &defaultSequence() const;

    /** Defines the standard shortcut @a sequence. */
    void setStandardSequence(const QKeySequence &sequence);
    /** Returns the standard shortcut sequence. */
    const QKeySequence &standardSequence() const;

    /** Converts primary shortcut sequence to native text. */
    QString primaryToNativeText() const;
    /** Converts primary shortcut sequence to portable text. */
    QString primaryToPortableText() const;

private:

    /** Holds the shortcut scope. */
    QString              m_strScope;
    /** Holds the shortcut description. */
    QString              m_strDescription;
    /** Holds the shortcut sequences. */
    QList<QKeySequence>  m_sequences;
    /** Holds the default shortcut sequence. */
    QKeySequence         m_defaultSequence;
    /** Holds the standard shortcut sequence. */
    QKeySequence         m_standardSequence;
};


/** QObject extension used as shortcut pool singleton. */
class SHARED_LIBRARY_STUFF UIShortcutPool : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies about Manager UI shortcuts changed. */
    void sigManagerShortcutsReloaded();
    /** Notifies about Runtime UI shortcuts changed. */
    void sigRuntimeShortcutsReloaded();

public:

    /** Returns singleton instance. */
    static UIShortcutPool *instance() { return s_pInstance; }
    /** Creates singleton instance. */
    static void create(UIType enmType);
    /** Destroys singleton instance. */
    static void destroy();

    /** Returns shortcuts of particular @a pActionPool for specified @a pAction. */
    UIShortcut &shortcut(UIActionPool *pActionPool, UIAction *pAction);
    /** Returns shortcuts of action-pool with @a strPoolID for action with @a strActionID. */
    UIShortcut &shortcut(const QString &strPoolID, const QString &strActionID);
    /** Returns all the shortcuts. */
    const QMap<QString, UIShortcut> &shortcuts() const { return m_shortcuts; }
    /** Defines shortcut overrides. */
    void setOverrides(const QMap<QString, QString> &overrides);

    /** Applies shortcuts for specified @a pActionPool. */
    void applyShortcuts(UIActionPool *pActionPool);

    /** Returns standard QKeySequence for passed QKeySequence::StandardKey. */
    static QKeySequence standardSequence(QKeySequence::StandardKey enmKey);

private slots:

    /** Handles translation event. */
    void sltRetranslateUI();
    /** Reloads Selector UI shortcuts. */
    void sltReloadSelectorShortcuts();
    /** Reloads Runtime UI shortcuts. */
    void sltReloadMachineShortcuts();

private:

    /** Constructs shortcut pool. */
    UIShortcutPool(UIType enmType);
    /** Destructs shortcut pool. */
    virtual ~UIShortcutPool() RT_OVERRIDE RT_FINAL;

    /** Prepares all. */
    void prepare();
    /** Prepares connections. */
    void prepareConnections();

    /** Cleanups all. */
    void cleanup() {}

    /** Loads defaults. */
    void loadDefaults();
    /** Loads defaults for pool with specified @a strPoolExtraDataID. */
    void loadDefaultsFor(const QString &strPoolExtraDataID);
    /** Loads overrides. */
    void loadOverrides();
    /** Loads overrides for pool with specified @a strPoolExtraDataID. */
    void loadOverridesFor(const QString &strPoolExtraDataID);
    /** Saves overrides. */
    void saveOverrides();
    /** Saves overrides for pool with specified @a strPoolExtraDataID. */
    void saveOverridesFor(const QString &strPoolExtraDataID);

    /** Returns shortcut with specified @a strShortcutKey. */
    UIShortcut &shortcut(const QString &strShortcutKey);

    /** Holds the singleton instance. */
    static UIShortcutPool *s_pInstance;
    /** Shortcut key template. */
    static const QString   s_strShortcutKeyTemplate;
    /** Shortcut key template for Runtime UI. */
    static const QString   s_strShortcutKeyTemplateRuntime;

    /** Holds the pool type. */
    UIType                     m_enmType;
    /** Holds the pool shortcuts. */
    QMap<QString, UIShortcut>  m_shortcuts;
};

/** Singleton Shortcut Pool 'official' name. */
#define gShortcutPool UIShortcutPool::instance()


#endif /* !FEQT_INCLUDED_SRC_globals_UIShortcutPool_h */
