/* $Id: UISoftKeyboard.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UISoftKeyboard class implementation.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QGroupBox>
#include <QInputDialog>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPicture>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QStackedWidget>
#include <QToolButton>
#include <QXmlStreamReader>
#include <QVBoxLayout>

/* GUI includes: */
#include "UICommon.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIExtraDataManager.h"
#include "UIGlobalSession.h"
#include "UIHelpBrowserDialog.h"
#include "UIIconPool.h"
#include "UILoggingDefs.h"
#include "UIMachine.h"
#include "UIMessageCenter.h"
#include "UIModalWindowManager.h"
#include "UISoftKeyboard.h"
#include "UITranslationEventListener.h"
#ifdef VBOX_WS_MAC
# include "VBoxUtils-darwin.h"
#endif

/* External includes: */
# include <math.h>

/* Forward declarations: */
class UISoftKeyboardColorButton;
class UISoftKeyboardLayout;
class UISoftKeyboardRow;
class UISoftKeyboardWidget;

const int iMessageTimeout = 3000;
/** Key position are used to identify respective keys. */
const int iCapsLockPosition = 30;
const int iNumLockPosition = 90;
const int iScrollLockPosition = 125;

/** Set a generous file size limit. */
const qint64 iFileSizeLimit = _256K;
const QString strSubDirectorName("keyboardLayouts");

/** Name, background color, normal font color, hover color, edited button background color, pressed button font color. */
const char* predefinedColorThemes[][6] = {{"Clear Night","#000000", "#ffffff", "#859900", "#9b6767", "#000000"},
    {"Gobi Dark","#002b36", "#fdf6e3", "#859900", "#cb4b16", "#002b36"},
    {"Gobi Light","#fdf6e3", "#002b36", "#2aa198", "#cb4b16", "#bf4040"},
    {0, 0, 0, 0, 0, 0}};

typedef QPair<QLabel*, UISoftKeyboardColorButton*> ColorSelectLabelButton;

enum KeyState
{
    KeyState_NotPressed,
    KeyState_Pressed,
    KeyState_Locked,
    KeyState_Max
};

enum KeyType
{
    /** Can be in KeyState_NotPressed and KeyState_Pressed states. */
    KeyType_Ordinary,
    /** e.g. CapsLock, NumLock. Can be only in KeyState_NotPressed, KeyState_Locked */
    KeyType_Lock,
    /** e.g. Shift Can be in all 3 states*/
    KeyType_Modifier,
    KeyType_Max
};

enum KeyboardColorType
{
    KeyboardColorType_Background = 0,
    KeyboardColorType_Font,
    KeyboardColorType_Hover,
    KeyboardColorType_Edit,
    KeyboardColorType_Pressed,
    KeyboardColorType_Max
};

enum KeyboardRegion
{
    KeyboardRegion_Main = 0,
    KeyboardRegion_NumPad,
    KeyboardRegion_MultimediaKeys,
    KeyboardRegion_Max
};

struct UIKeyCaptions
{
    UIKeyCaptions(const QString &strBase, const QString &strShift,
                const QString &strAltGr, const QString &strShiftAltGr)
        : m_strBase(strBase)
        , m_strShift(strShift)
        , m_strAltGr(strAltGr)
        , m_strShiftAltGr(strShiftAltGr)
    {
        m_strBase.replace("\\n", "\n");
        m_strShift.replace("\\n", "\n");
        m_strAltGr.replace("\\n", "\n");
        m_strShiftAltGr.replace("\\n", "\n");
    }
    UIKeyCaptions(){}
    bool operator==(const UIKeyCaptions &other) const
    {
        return (m_strBase == other.m_strBase &&
                m_strShift == other.m_strShift &&
                m_strAltGr == other.m_strAltGr &&
                m_strShiftAltGr == other.m_strShiftAltGr);
    }
    QString m_strBase;
    QString m_strShift;
    QString m_strAltGr;
    QString m_strShiftAltGr;
};

/** Returns a QPointF which lies on the line [p0, p1] and with a distance @p fDistance to p0. */
static QPointF pointInBetween(qreal fDistance, const QPointF &p0, const QPointF &p1)
{
    QPointF vectorP0P1 = p1 - p0;
    qreal length = sqrt(vectorP0P1.x() * vectorP0P1.x() + vectorP0P1.y() * vectorP0P1.y());
    if (length == 0)
        return QPointF();
    /* Normalize the vector and add it to starting point: */
    vectorP0P1 = (fDistance / length) * vectorP0P1 + p0;
    return vectorP0P1;
}


/*********************************************************************************************************************************
*   UISoftKeyboardColorButton definition.                                                                                        *
*********************************************************************************************************************************/

class UISoftKeyboardColorButton : public QPushButton
{
    Q_OBJECT;

public:

    UISoftKeyboardColorButton(KeyboardColorType enmColorType, QWidget *pParent = 0);
    KeyboardColorType colorType() const;

public:

    KeyboardColorType m_enmColorType;
};


/*********************************************************************************************************************************
*   UISoftKeyboardPhysicalLayout definition.                                                                                     *
*********************************************************************************************************************************/

/** This class is used to represent the physical layout of a keyboard (in contrast to UISoftKeyboardLayout).
  * Physical layouts are read from an xml file where keys are placed in rows. Each UISoftKeyboardLayout must refer to a
  * refer to a UISoftKeyboardPhysicalLayout instance. An example of an UISoftKeyboardPhysicalLayout instance is 103 key ISO layout.*/
class UISoftKeyboardPhysicalLayout
{

public:
    UISoftKeyboardPhysicalLayout();

    void setName(const QString &strName);
    const QString &name() const;

    void setFileName(const QString &strName);
    const QString &fileName() const;

    void setUid(const QUuid &uid);
    const QUuid &uid() const;

    const QVector<UISoftKeyboardRow> &rows() const;
    QVector<UISoftKeyboardRow> &rows();

    void setLockKey(int iKeyPosition, UISoftKeyboardKey *pKey);
    void updateLockKeyStates(bool fCapsLockState, bool fNumLockState, bool fScrollLockState);
    void reset();

    void setDefaultKeyWidth(int iDefaultKeyWidth);
    int defaultKeyWidth() const;

    /** Returns the sum totalHeight() of all rows(). */
    int totalHeight() const;

private:

    void updateLockKeyState(bool fLockState, UISoftKeyboardKey *pKey);
    QString  m_strFileName;
    QUuid    m_uId;
    QString  m_strName;
    QVector<UISoftKeyboardRow>    m_rows;
    int m_iDefaultKeyWidth;
    /** Scroll, Num, and Caps Lock keys' states are updated thru some API events. Thus we keep their pointers in a containter. */
    QMap<int, UISoftKeyboardKey*> m_lockKeys;
};

/*********************************************************************************************************************************
*   UIKeyboardLayoutEditor definition.                                                                                  *
*********************************************************************************************************************************/

/** A QWidget extension thru which we can edit key captions, the physical layout of the keyboard, name of the layout etc. */
class UIKeyboardLayoutEditor : public QWidget
{
    Q_OBJECT;

signals:

    void sigLayoutEdited();
    void sigUIKeyCaptionsEdited(UISoftKeyboardKey* pKey);
    void sigGoBackButton();

public:

    UIKeyboardLayoutEditor(QWidget *pParent = 0);
    void setKey(UISoftKeyboardKey *pKey);
    void setLayoutToEdit(UISoftKeyboardLayout *pLayout);
    void setPhysicalLayoutList(const QVector<UISoftKeyboardPhysicalLayout> &physicalLayouts);
    void reset();

private slots:

    void sltCaptionsUpdate();
    void sltPhysicalLayoutChanged();
    void sltLayoutNameChanged(const QString &strCaption);
    void sltLayoutNativeNameChanged(const QString &strCaption);
    void sltRetranslateUI();

private:

    void     prepareObjects();
    void     prepareConnections();
    QWidget *prepareKeyCaptionEditWidgets();
    void     resetKeyWidgets();
    QGridLayout *m_pEditorLayout;
    QToolButton *m_pGoBackButton;
    QGroupBox   *m_pSelectedKeyGroupBox;
    QGroupBox   *m_pCaptionEditGroupBox;
    QComboBox   *m_pPhysicalLayoutCombo;
    QLabel      *m_pTitleLabel;
    QLabel      *m_pPhysicalLayoutLabel;
    QLabel      *m_pLayoutNameLabel;
    QLabel      *m_pLayoutNativeNameLabel;
    QLabel      *m_pScanCodeLabel;
    QLabel      *m_pPositionLabel;
    QLabel      *m_pBaseCaptionLabel;
    QLabel      *m_pShiftCaptionLabel;
    QLabel      *m_pAltGrCaptionLabel;
    QLabel      *m_pShiftAltGrCaptionLabel;
    QLineEdit   *m_pLayoutNameEdit;
    QLineEdit   *m_pLayoutNativeNameEdit;
    QLineEdit   *m_pScanCodeEdit;
    QLineEdit   *m_pPositionEdit;
    QLineEdit   *m_pBaseCaptionEdit;
    QLineEdit   *m_pShiftCaptionEdit;
    QLineEdit   *m_pAltGrCaptionEdit;
    QLineEdit   *m_pShiftAltGrCaptionEdit;

    /** The key which is being currently edited. Might be Null. */
    UISoftKeyboardKey  *m_pKey;
    /** The layout which is being currently edited. */
    UISoftKeyboardLayout *m_pLayout;
};

/*********************************************************************************************************************************
*   UILayoutSelector definition.                                                                                  *
*********************************************************************************************************************************/

class UILayoutSelector : public QWidget
{

    Q_OBJECT;

signals:

    void sigSaveLayout();
    void sigCopyLayout();
    void sigDeleteLayout();
    void sigLayoutSelectionChanged(const QUuid &strSelectedLayoutUid);
    void sigShowLayoutEditor();
    void sigCloseLayoutList();

public:

    UILayoutSelector(QWidget *pParent = 0);
    void setLayoutList(const QStringList &layoutNames, QList<QUuid> layoutIdList);
    void setCurrentLayout(const QUuid &layoutUid);
    void setCurrentLayoutIsEditable(bool fEditable);

private slots:

    void sltCurrentItemChanged(QListWidgetItem *pCurrent, QListWidgetItem *pPrevious);
    void sltRetranslateUI();

private:

    void prepareObjects();

    QListWidget *m_pLayoutListWidget;
    QToolButton *m_pApplyLayoutButton;
    QToolButton *m_pEditLayoutButton;
    QToolButton *m_pCopyLayoutButton;
    QToolButton *m_pSaveLayoutButton;
    QToolButton *m_pDeleteLayoutButton;
    QLabel      *m_pTitleLabel;
    QToolButton *m_pCloseButton;
};

/*********************************************************************************************************************************
*   UISoftKeyboardRow definition.                                                                                  *
*********************************************************************************************************************************/

/** UISoftKeyboardRow represents a row in the physical keyboard. The rows are read from a physical layout file and contained
  * keys are added to rows in the order they appear in that file.*/
class UISoftKeyboardRow
{

public:

    UISoftKeyboardRow();

    void setDefaultWidth(int iWidth);
    int defaultWidth() const;

    void setDefaultHeight(int iHeight);
    int defaultHeight() const;

    /* Return the sum of the maximum key height and m_iSpaceHeightAfter */
    int totalHeight() const;

    QVector<UISoftKeyboardKey> &keys();
    const QVector<UISoftKeyboardKey> &keys() const;

    void setSpaceHeightAfter(int iSpace);
    int spaceHeightAfter() const;

    int leftMargin() const;
    void setLeftMargin(int iMargin);

private:

    /** Default width and height might be inherited from the layout and overwritten in row settings. */
    int m_iDefaultWidth;
    int m_iDefaultHeight;

    QVector<UISoftKeyboardKey> m_keys;
    int m_iSpaceHeightAfter;
    /* The width of the empty space before the 1st key. */
    int m_iLeftMargin;
};

/*********************************************************************************************************************************
*   UISoftKeyboardKey definition.                                                                                  *
*********************************************************************************************************************************/

/** UISoftKeyboardKey is a place holder for a keyboard key. Graphical key represantations are drawn according to this class.
  * The position of a key within the physical layout is read from the layout file. Note that UISoftKeyboardKey usually does not have
  * caption field(s). Captions are kept by UISoftKeyboardLayout since same keys may (and usually do) have different captions in
  * different layouts. So called static captions are exceptions. They are defined in physical layout files and kept as member of
  * UISoftKeyboardKey. When a static caption exits captions (if any) from the keyboard layout files are ignored. */
class UISoftKeyboardKey
{
public:

    UISoftKeyboardKey();

    const QRect keyGeometry() const;
    void setKeyGeometry(const QRect &rect);

    void setWidth(int iWidth);
    int width() const;

    void setHeight(int iHeight);
    int height() const;

    void setScanCode(LONG scanCode);
    LONG scanCode() const;

    void addScanCodePrefix(LONG scanCode);

    void setUsageId(LONG usageId);
    void setUsagePage(LONG usagePage);
    QPair<LONG, LONG> usagePageIdPair() const;

    void setSpaceWidthAfter(int iSpace);
    int spaceWidthAfter() const;

    void setPosition(int iPosition);
    int position() const;

    void setType(KeyType enmType);
    KeyType type() const;

    KeyboardRegion keyboardRegion() const;
    void setKeyboardRegion(KeyboardRegion enmRegion);

    void setCutout(int iCorner, int iWidth, int iHeight);

    KeyState state() const;
    void setState(KeyState state);

    void setStaticCaption(const QString &strCaption);
    const QString &staticCaption() const;

    void setImageByName(const QString &strCaption);
    QPixmap image() const;

    void setParentWidget(UISoftKeyboardWidget* pParent);
    QVector<LONG> scanCodeWithPrefix() const;

    void setIsOSMenuKey(bool fFlag);
    bool isOSMenuKey() const;

    void release();
    void press();

    void setPoints(const QVector<QPointF> &points);
    const QVector<QPointF> &points() const;
    const QPainterPath &painterPath() const;


    void setCornerRadius(float fCornerRadius);

    QPolygonF polygonInGlobal() const;

    int cutoutCorner() const;
    int cutoutWidth() const;
    int cutoutHeight() const;

    void updateLockState(bool fLocked);
    void reset();

private:

    void updateState(bool fPressed);
    /** Creates a path out of points m_points with rounded corners. */
    void computePainterPath();

    QRect    m_keyGeometry;
    /** Stores the key points (vertices) in local coordinates. */
    QVector<QPointF> m_points;
    /** We cache the path since re-computing that at each draw is meaningless. */
    QPainterPath m_painterPath;
    KeyType  m_enmType;
    KeyState m_enmState;
    /** Key width as it is read from the xml file. */
    int      m_iWidth;
    /** Key height as it is read from the xml file. */
    int      m_iHeight;
    int      m_iSpaceWidthAfter;
    LONG     m_scanCode;
    QVector<LONG> m_scanCodePrefix;

    /** @name Cutouts are used to create non-rectangle keys polygons.
      * @{ */
        int  m_iCutoutWidth;
        int  m_iCutoutHeight;
        /** -1 is for no cutout. 0 is the topleft, 2 is the top right and so on. */
        int  m_iCutoutCorner;
    /** @} */

    /** Key's position in the layout. */
    int  m_iPosition;
    UISoftKeyboardWidget  *m_pParentWidget;
    LONG m_iUsageId;
    LONG m_iUsagePage;
    KeyboardRegion m_enmKeyboardRegion;
    /** This is used for multimedia keys, OS key etc where we want to have a non-modifiable
      * caption (usually a single char). This caption is defined in the physical layout file
      * and has precedence over the captions defined in keyboard layout files. */
    QString m_strStaticCaption;
    bool    m_fIsOSMenuKey;
    double  m_fCornerRadius;
    QIcon   m_icon;
};


/*********************************************************************************************************************************
*   UISoftKeyboardLayout definition.                                                                                  *
*********************************************************************************************************************************/
/** UISoftKeyboardLayout represents mainly a set of captions for the keys. It refers to a phsical layout which defines the
  * positioning and number of keys (alongside with scan codes etc.). UISoftKeyboardLayout instances are read from xml files. An
  * example for UISoftKeyboardLayout instance is 'US International' keyboard layout. */
class UISoftKeyboardLayout
{

public:

    UISoftKeyboardLayout();

    void setName(const QString &strName);
    const QString &name() const;

    void setNativeName(const QString &strLocaName);
    const QString &nativeName() const;

    /** Combines name and native name and returns the string. */
    QString nameString() const;

    void setSourceFilePath(const QString& strSourceFilePath);
    const QString& sourceFilePath() const;

    void setIsFromResources(bool fIsFromResources);
    bool isFromResources() const;

    void setEditable(bool fEditable);
    bool editable() const;

    void setPhysicalLayoutUuid(const QUuid &uuid);
    const QUuid &physicalLayoutUuid() const;

    void addOrUpdateUIKeyCaptions(int iKeyPosition, const UIKeyCaptions &keyCaptions);
    UIKeyCaptions keyCaptions(int iKeyPosition) const;

    bool operator==(const UISoftKeyboardLayout &otherLayout) const;

    QString baseCaption(int iKeyPosition) const;
    QString shiftCaption(int iKeyPosition) const;

    QString altGrCaption(int iKeyPosition) const;
    QString shiftAltGrCaption(int iKeyPosition) const;

    void setEditedBuNotSaved(bool fEditedButNotsaved);
    bool editedButNotSaved() const;

    void  setUid(const QUuid &uid);
    QUuid uid() const;

    void drawTextInRect(const UISoftKeyboardKey &key, QPainter &painter);
    void drawKeyImageInRect(const UISoftKeyboardKey &key, QPainter &painter);

private:

    QMap<int, UIKeyCaptions> m_keyCaptionsMap;
    /** Caching the font sizes we used for font rendering since it is not a very cheap process to compute these. */
    QMap<int, int>         m_keyCaptionsFontSizeMap;
    /** The UUID of the physical layout used by this layout. */
    QUuid   m_physicalLayoutUuid;
    /** This is the English name of the layout. */
    QString m_strName;
    QString m_strNativeName;
    QString m_strSourceFilePath;
    bool    m_fEditable;
    bool    m_fIsFromResources;
    bool    m_fEditedButNotSaved;
    QUuid   m_uid;
};

/*********************************************************************************************************************************
*   UISoftKeyboardColorTheme definition.                                                                                  *
*********************************************************************************************************************************/

class UISoftKeyboardColorTheme
{

public:

    UISoftKeyboardColorTheme();
    UISoftKeyboardColorTheme(const QString &strName,
                             const QString &strBackgroundColor,
                             const QString &strNormalFontColor,
                             const QString &strHoverColor,
                             const QString &strEditedButtonBackgroundColor,
                             const QString &strPressedButtonFontColor);

    void setColor(KeyboardColorType enmColorType, const QColor &color);
    QColor color(KeyboardColorType enmColorType) const;
    QStringList colorsToStringList() const;
    void colorsFromStringList(const QStringList &colorStringList);

    const QString &name() const;
    void setName(const QString &strName);

    bool isEditable() const;
    void setIsEditable(bool fIsEditable);

private:

    QVector<QColor> m_colors;
    QString m_strName;
    bool    m_fIsEditable;
};

/*********************************************************************************************************************************
*   UISoftKeyboardWidget definition.                                                                                  *
*********************************************************************************************************************************/

/** The container widget for keyboard keys. It also handles all the keyboard related events. paintEvent of this class
  * handles drawing of the soft keyboard. */
class UISoftKeyboardWidget : public QWidget
{
    Q_OBJECT;

    enum Mode
    {
        Mode_LayoutEdit,
        Mode_Keyboard,
        Mode_Max
    };

signals:

    void sigStatusBarMessage(const QString &strMessage);
    void sigCurrentLayoutChange();
    void sigKeyToEdit(UISoftKeyboardKey* pKey);
    void sigCurrentColorThemeChanged();
    void sigOptionsChanged();

public:

    UISoftKeyboardWidget(QWidget *pParent, UIMachine *pMachine);

    virtual QSize minimumSizeHint() const RT_OVERRIDE;
    virtual QSize sizeHint() const  RT_OVERRIDE;
    void keyStateChange(UISoftKeyboardKey* pKey);
    void loadLayouts();

    void setCurrentLayout(const QUuid &layoutUid);
    UISoftKeyboardLayout *currentLayout();

    QStringList layoutNameList() const;
    QList<QUuid> layoutUidList() const;
    const QVector<UISoftKeyboardPhysicalLayout> &physicalLayouts() const;
    void deleteCurrentLayout();
    void toggleEditMode(bool fIsEditMode);

    void saveCurentLayoutToFile();
    void copyCurentLayout();
    float layoutAspectRatio();

    bool hideOSMenuKeys() const;
    void setHideOSMenuKeys(bool fHide);

    bool hideNumPad() const;
    void setHideNumPad(bool fHide);

    bool hideMultimediaKeys() const;
    void setHideMultimediaKeys(bool fHide);

    QColor color(KeyboardColorType enmColorType) const;
    void setColor(KeyboardColorType ennmColorType, const QColor &color);

    QStringList colorsToStringList(const QString &strColorThemeName);
    void colorsFromStringList(const QString &strColorThemeName, const QStringList &colorStringList);

    /** Unlike modifier and ordinary keys we update the state of the Lock keys thru event signals we receieve
      * from the guest OS. Parameter f???State is true if the corresponding key is locked. */
    void updateLockKeyStates(bool fCapsLockState, bool fNumLockState, bool fScrollLockState);
    void reset();

    QStringList colorThemeNames() const;
    QString currentColorThemeName() const;
    void setColorThemeByName(const QString &strColorThemeName);
    void parentDialogDeactivated();
    bool isColorThemeEditable() const;
    /** Returns a list of layout names that have been edited but not yet saved to a file. */
    QStringList unsavedLayoutsNameList() const;

protected:

    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE;
    virtual void mousePressEvent(QMouseEvent *pEvent) RT_OVERRIDE;
    virtual void mouseReleaseEvent(QMouseEvent *pEvent) RT_OVERRIDE;
    virtual void mouseMoveEvent(QMouseEvent *pEvent) RT_OVERRIDE;

private slots:

    void sltKeyboardLedsChange();
    void sltRetranslateUI();

private:

    void    addLayout(const UISoftKeyboardLayout &newLayout);
    void    setNewMinimumSize(const QSize &size);
    void    setInitialSize(int iWidth, int iHeight);
    /** Searches for the key which contains the position of the @p pEvent and returns it if found. */
    UISoftKeyboardKey *keyUnderMouse(QMouseEvent *pEvent);
    UISoftKeyboardKey *keyUnderMouse(const QPoint &point);
    void               handleKeyPress(UISoftKeyboardKey *pKey);
    void               handleKeyRelease(UISoftKeyboardKey *pKey);
    /** Sends usage id/page to API when a modifier key is right clicked. useful for testing and things like
      * Window key press for start menu opening. This works orthogonal to left clicks.*/
    void               modifierKeyPressRelease(UISoftKeyboardKey *pKey, bool fRelease);
    bool               loadPhysicalLayout(const QString &strLayoutFileName, KeyboardRegion keyboardRegion = KeyboardRegion_Main);
    bool               loadKeyboardLayout(const QString &strLayoutName);
    void               prepareObjects();
    void               prepareColorThemes();
    UISoftKeyboardPhysicalLayout *findPhysicalLayout(const QUuid &uuid);
    /** Sets m_pKeyBeingEdited. */
    void               setKeyBeingEdited(UISoftKeyboardKey *pKey);
    bool               layoutByNameExists(const QString &strName) const;

    /** Looks under the default keyboard layout folder and add the file names to the fileList. */
    void               lookAtDefaultLayoutFolder(QStringList &fileList);
    UISoftKeyboardColorTheme *colorTheme(const QString &strColorThemeName);
    void showKeyTooltip(UISoftKeyboardKey *pKey);
    void putKeyboardSequence(QVector<LONG> sequence);
    void putUsageCodesPress(QVector<QPair<LONG, LONG> > sequence);
    void putUsageCodesRelease(QVector<QPair<LONG, LONG> > sequence);

    UIMachine         *m_pMachine;
    UISoftKeyboardKey *m_pKeyUnderMouse;
    UISoftKeyboardKey *m_pKeyBeingEdited;

    UISoftKeyboardKey *m_pKeyPressed;
    UISoftKeyboardColorTheme *m_currentColorTheme;
    QVector<UISoftKeyboardColorTheme> m_colorThemes;
    QVector<UISoftKeyboardKey*> m_pressedModifiers;
    QVector<UISoftKeyboardPhysicalLayout> m_physicalLayouts;
    UISoftKeyboardPhysicalLayout       m_numPadLayout;
    UISoftKeyboardPhysicalLayout       m_multiMediaKeysLayout;
    QMap<QUuid, UISoftKeyboardLayout>  m_layouts;
    QUuid                              m_uCurrentLayoutId;
    /** Key is the key position as read from the layout and value is the message we show as mouse hovers over the key. */
    QMap<int, QString> m_keyTooltips;

    QSize m_minimumSize;
    float m_fScaleFactorX;
    float m_fScaleFactorY;
    int   m_iInitialHeight;
    /** This is the width of the keyboard including the numpad but without m_iInitialWidthNoNumPad */
    int   m_iInitialWidth;
    int   m_iInitialWidthNoNumPad;
    /** This widt is added while drawing the keyboard not to key geometries. */
    int   m_iBeforeNumPadWidth;
    int   m_iXSpacing;
    int   m_iYSpacing;
    int   m_iLeftMargin;
    int   m_iTopMargin;
    int   m_iRightMargin;
    int   m_iBottomMargin;
    Mode  m_enmMode;
    bool  m_fHideOSMenuKeys;
    bool  m_fHideNumPad;
    bool  m_fHideMultimediaKeys;
};

/*********************************************************************************************************************************
*   UIPhysicalLayoutReader definition.                                                                                  *
*********************************************************************************************************************************/

class UIPhysicalLayoutReader
{

public:

    bool parseXMLFile(const QString &strFileName, UISoftKeyboardPhysicalLayout &physicalLayout);
    static QVector<QPointF> computeKeyVertices(const UISoftKeyboardKey &key);

private:

    void  parseKey(UISoftKeyboardRow &row);
    void  parseRow(int iDefaultWidth, int iDefaultHeight, QVector<UISoftKeyboardRow> &rows);
    /** Parses the horizontal space between keys. */
    void  parseKeySpace(UISoftKeyboardRow &row);
    void  parseCutout(UISoftKeyboardKey &key);

    QXmlStreamReader m_xmlReader;
};

/*********************************************************************************************************************************
*   UIKeyboardLayoutReader definition.                                                                                  *
*********************************************************************************************************************************/

class UIKeyboardLayoutReader
{

public:

    bool  parseFile(const QString &strFileName, UISoftKeyboardLayout &layout);

private:

    void  parseKey(UISoftKeyboardLayout &layout);
    QXmlStreamReader m_xmlReader;
    /** Map key is the key position and the value is the captions of the key. */
};


/*********************************************************************************************************************************
*   UISoftKeyboardStatusBarWidget  definition.                                                                                   *
*********************************************************************************************************************************/

class UISoftKeyboardStatusBarWidget : public QWidget
{
    Q_OBJECT;

signals:

    void sigShowHideSidePanel();
    void sigShowSettingWidget();
    void sigResetKeyboard();
    void sigHelpButtonPressed();

public:

    UISoftKeyboardStatusBarWidget(QWidget *pParent = 0);
    void updateLayoutNameInStatusBar(const QString &strMessage);

private slots:

    void sltRetranslateUI();

private:

    void prepareObjects();
    QToolButton  *m_pLayoutListButton;
    QToolButton  *m_pSettingsButton;
    QToolButton  *m_pResetButton;
    QToolButton  *m_pHelpButton;
    QLabel       *m_pMessageLabel;
};


/*********************************************************************************************************************************
*   UISoftKeyboardSettingsWidget  definition.                                                                                    *
*********************************************************************************************************************************/

class UISoftKeyboardSettingsWidget : public QWidget
{
    Q_OBJECT;

signals:

    void sigHideNumPad(bool fHide);
    void sigHideOSMenuKeys(bool fHide);
    void sigHideMultimediaKeys(bool fHide);
    void sigColorCellClicked(int iColorRow);
    void sigCloseSettingsWidget();
    void sigColorThemeSelectionChanged(const QString &strColorThemeName);

public:

    UISoftKeyboardSettingsWidget(QWidget *pParent = 0);
    void setHideOSMenuKeys(bool fHide);
    void setHideNumPad(bool fHide);
    void setHideMultimediaKeys(bool fHide);
    void setColorSelectionButtonBackgroundAndTooltip(KeyboardColorType enmColorType, const QColor &color, bool fIsColorEditable);
    void setColorThemeNames(const QStringList &colorThemeNames);
    void setCurrentColorThemeName(const QString &strColorThemeName);

private slots:

    void sltColorSelectionButtonClicked();
    void sltRetranslateUI();

private:

    void prepareObjects();

    QCheckBox    *m_pHideNumPadCheckBox;
    QCheckBox    *m_pShowOsMenuButtonsCheckBox;
    QCheckBox    *m_pHideMultimediaKeysCheckBox;
    QGroupBox    *m_pColorThemeGroupBox;
    QComboBox    *m_pColorThemeComboBox;
    QLabel       *m_pTitleLabel;
    QToolButton  *m_pCloseButton;
    QVector<ColorSelectLabelButton> m_colorSelectLabelsButtons;
};


/*********************************************************************************************************************************
*   UISoftKeyboardColorButton implementation.                                                                                    *
*********************************************************************************************************************************/


UISoftKeyboardColorButton::UISoftKeyboardColorButton(KeyboardColorType enmColorType, QWidget *pParent /*= 0 */)
    :QPushButton(pParent)
    , m_enmColorType(enmColorType){}

KeyboardColorType UISoftKeyboardColorButton::colorType() const
{
    return m_enmColorType;
}


/*********************************************************************************************************************************
*   UISoftKeyboardPhysicalLayout implementation.                                                                                 *
*********************************************************************************************************************************/

UISoftKeyboardPhysicalLayout::UISoftKeyboardPhysicalLayout()
    :m_iDefaultKeyWidth(50)
{
}

void UISoftKeyboardPhysicalLayout::setName(const QString &strName)
{
    m_strName = strName;
}

const QString &UISoftKeyboardPhysicalLayout::name() const
{
    return m_strName;
}

void UISoftKeyboardPhysicalLayout::setFileName(const QString &strName)
{
    m_strFileName = strName;
}

const QString &UISoftKeyboardPhysicalLayout::fileName() const
{
    return m_strFileName;
}

void UISoftKeyboardPhysicalLayout::setUid(const QUuid &uid)
{
    m_uId = uid;
}

const QUuid &UISoftKeyboardPhysicalLayout::uid() const
{
    return m_uId;
}

const QVector<UISoftKeyboardRow> &UISoftKeyboardPhysicalLayout::rows() const
{
    return m_rows;
}

QVector<UISoftKeyboardRow> &UISoftKeyboardPhysicalLayout::rows()
{
    return m_rows;
}

void UISoftKeyboardPhysicalLayout::setLockKey(int iKeyPosition, UISoftKeyboardKey *pKey)
{
    m_lockKeys[iKeyPosition] = pKey;
}

void UISoftKeyboardPhysicalLayout::updateLockKeyStates(bool fCapsLockState, bool fNumLockState, bool fScrollLockState)
{
    updateLockKeyState(fCapsLockState, m_lockKeys.value(iCapsLockPosition, 0));
    updateLockKeyState(fNumLockState, m_lockKeys.value(iNumLockPosition, 0));
    updateLockKeyState(fScrollLockState, m_lockKeys.value(iScrollLockPosition, 0));
}

void UISoftKeyboardPhysicalLayout::setDefaultKeyWidth(int iDefaultKeyWidth)
{
    m_iDefaultKeyWidth = iDefaultKeyWidth;
}

int UISoftKeyboardPhysicalLayout::defaultKeyWidth() const
{
    return m_iDefaultKeyWidth;
}

void UISoftKeyboardPhysicalLayout::reset()
{
    for (int i = 0; i < m_rows.size(); ++i)
    {
        for (int j = 0; j < m_rows[i].keys().size(); ++j)
        {
            m_rows[i].keys()[j].reset();
        }
    }
}

int UISoftKeyboardPhysicalLayout::totalHeight() const
{
    int iHeight = 0;
    for (int i = 0; i < m_rows.size(); ++i)
        iHeight += m_rows[i].totalHeight();
    return iHeight;
}

void UISoftKeyboardPhysicalLayout::updateLockKeyState(bool fLockState, UISoftKeyboardKey *pKey)
{
    if (!pKey)
        return;
    pKey->updateLockState(fLockState);
}

/*********************************************************************************************************************************
*   UIKeyboardLayoutEditor implementation.                                                                                  *
*********************************************************************************************************************************/

UIKeyboardLayoutEditor::UIKeyboardLayoutEditor(QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_pEditorLayout(0)
    , m_pGoBackButton(0)
    , m_pSelectedKeyGroupBox(0)
    , m_pCaptionEditGroupBox(0)
    , m_pPhysicalLayoutCombo(0)
    , m_pTitleLabel(0)
    , m_pPhysicalLayoutLabel(0)
    , m_pLayoutNameLabel(0)
    , m_pLayoutNativeNameLabel(0)
    , m_pScanCodeLabel(0)
    , m_pPositionLabel(0)
    , m_pBaseCaptionLabel(0)
    , m_pShiftCaptionLabel(0)
    , m_pAltGrCaptionLabel(0)
    , m_pShiftAltGrCaptionLabel(0)
    , m_pLayoutNameEdit(0)
    , m_pLayoutNativeNameEdit(0)
    , m_pScanCodeEdit(0)
    , m_pPositionEdit(0)
    , m_pBaseCaptionEdit(0)
    , m_pShiftCaptionEdit(0)
    , m_pAltGrCaptionEdit(0)
    , m_pShiftAltGrCaptionEdit(0)
    , m_pKey(0)
    , m_pLayout(0)
{
    setAutoFillBackground(true);
    prepareObjects();
}

void UIKeyboardLayoutEditor::setKey(UISoftKeyboardKey *pKey)
{
    if (m_pKey == pKey || !m_pLayout)
        return;
    /* First apply the pending changes to the key that has been edited: */
    if (m_pKey)
    {
        UIKeyCaptions captions = m_pLayout->keyCaptions(m_pKey->position());
        if (captions.m_strBase != m_pBaseCaptionEdit->text() ||
            captions.m_strShift != m_pShiftCaptionEdit->text() ||
            captions.m_strAltGr != m_pAltGrCaptionEdit->text() ||
            captions.m_strShiftAltGr != m_pShiftAltGrCaptionEdit->text())
            m_pLayout->addOrUpdateUIKeyCaptions(m_pKey->position(),
                                              UIKeyCaptions(m_pBaseCaptionEdit->text(),
                                                          m_pShiftCaptionEdit->text(),
                                                          m_pAltGrCaptionEdit->text(),
                                                          m_pShiftAltGrCaptionEdit->text()));
    }
    m_pKey = pKey;
    if (m_pSelectedKeyGroupBox)
        m_pSelectedKeyGroupBox->setEnabled(m_pKey);
    if (!m_pKey)
    {
        resetKeyWidgets();
        return;
    }
    if (m_pScanCodeEdit)
        m_pScanCodeEdit->setText(QString::number(m_pKey->scanCode(), 16));
    if (m_pPositionEdit)
        m_pPositionEdit->setText(QString::number(m_pKey->position()));
    UIKeyCaptions captions = m_pLayout->keyCaptions(m_pKey->position());
    if (m_pBaseCaptionEdit)
        m_pBaseCaptionEdit->setText(captions.m_strBase);
    if (m_pShiftCaptionEdit)
        m_pShiftCaptionEdit->setText(captions.m_strShift);
    if (m_pAltGrCaptionEdit)
        m_pAltGrCaptionEdit->setText(captions.m_strAltGr);
    if (m_pShiftAltGrCaptionEdit)
        m_pShiftAltGrCaptionEdit->setText(captions.m_strShiftAltGr);
    m_pBaseCaptionEdit->setFocus();
}

void UIKeyboardLayoutEditor::setLayoutToEdit(UISoftKeyboardLayout *pLayout)
{
    if (m_pLayout == pLayout)
        return;

    m_pLayout = pLayout;
    if (!m_pLayout)
        reset();

    if (m_pLayoutNameEdit)
        m_pLayoutNameEdit->setText(m_pLayout ? m_pLayout->name() : QString());

    if (m_pLayoutNativeNameEdit)
        m_pLayoutNativeNameEdit->setText(m_pLayout ? m_pLayout->nativeName() : QString());

    if (m_pPhysicalLayoutCombo && m_pLayout)
    {
        int iIndex = m_pPhysicalLayoutCombo->findData(m_pLayout->physicalLayoutUuid());
        if (iIndex != -1)
            m_pPhysicalLayoutCombo->setCurrentIndex(iIndex);
    }
    update();
}

void UIKeyboardLayoutEditor::setPhysicalLayoutList(const QVector<UISoftKeyboardPhysicalLayout> &physicalLayouts)
{
    if (!m_pPhysicalLayoutCombo)
        return;
    m_pPhysicalLayoutCombo->clear();
    foreach (const UISoftKeyboardPhysicalLayout &physicalLayout, physicalLayouts)
        m_pPhysicalLayoutCombo->addItem(physicalLayout.name(), physicalLayout.uid());
}

void UIKeyboardLayoutEditor::sltCaptionsUpdate()
{
    if (!m_pKey || !m_pLayout)
        return;
    m_pLayout->addOrUpdateUIKeyCaptions(m_pKey->position(),
                                      UIKeyCaptions(m_pBaseCaptionEdit->text(),
                                                  m_pShiftCaptionEdit->text(),
                                                  m_pAltGrCaptionEdit->text(),
                                                  m_pShiftAltGrCaptionEdit->text()));
    emit sigUIKeyCaptionsEdited(m_pKey);
}

void UIKeyboardLayoutEditor::sltPhysicalLayoutChanged()
{
    if (!m_pPhysicalLayoutCombo || !m_pLayout)
        return;
    QUuid currentData = m_pPhysicalLayoutCombo->currentData().toUuid();
    if (!currentData.isNull())
        m_pLayout->setPhysicalLayoutUuid(currentData);
    emit sigLayoutEdited();
}

void UIKeyboardLayoutEditor::sltLayoutNameChanged(const QString &strName)
{
    if (!m_pLayout || m_pLayout->name() == strName)
        return;
    m_pLayout->setName(strName);
    emit sigLayoutEdited();
}

void UIKeyboardLayoutEditor::sltLayoutNativeNameChanged(const QString &strNativeName)
{
    if (!m_pLayout || m_pLayout->nativeName() == strNativeName)
        return;
    m_pLayout->setNativeName(strNativeName);
    emit sigLayoutEdited();
}

void UIKeyboardLayoutEditor::sltRetranslateUI()
{
    if (m_pTitleLabel)
        m_pTitleLabel->setText(UISoftKeyboard::tr("Layout Editor"));
    if (m_pGoBackButton)
    {
        m_pGoBackButton->setToolTip(UISoftKeyboard::tr("Return Back to Layout List"));
        m_pGoBackButton->setText(UISoftKeyboard::tr("Back to Layout List"));
    }
    if (m_pPhysicalLayoutLabel)
        m_pPhysicalLayoutLabel->setText(UISoftKeyboard::tr("Physical Layout"));
    if (m_pLayoutNameLabel)
        m_pLayoutNameLabel->setText(UISoftKeyboard::tr("English Name"));
    if (m_pLayoutNameEdit)
        m_pLayoutNameEdit->setToolTip(UISoftKeyboard::tr("Name of the Layout in English"));
    if (m_pLayoutNativeNameLabel)
        m_pLayoutNativeNameLabel->setText(UISoftKeyboard::tr("Native Language Name"));
    if (m_pLayoutNativeNameEdit)
        m_pLayoutNativeNameEdit->setToolTip(UISoftKeyboard::tr("Name of the Layout in the native Language"));
    if (m_pScanCodeLabel)
        m_pScanCodeLabel->setText(UISoftKeyboard::tr("Scan Code"));
    if (m_pScanCodeEdit)
        m_pScanCodeEdit->setToolTip(UISoftKeyboard::tr("The scan code the key produces. Not editable"));
    if (m_pPositionLabel)
        m_pPositionLabel->setText(UISoftKeyboard::tr("Position"));
    if (m_pPositionEdit)
        m_pPositionEdit->setToolTip(UISoftKeyboard::tr("The physical position of the key. Not editable"));
    if (m_pBaseCaptionLabel)
        m_pBaseCaptionLabel->setText(UISoftKeyboard::tr("Base"));
    if (m_pShiftCaptionLabel)
        m_pShiftCaptionLabel->setText(UISoftKeyboard::tr("Shift"));
    if (m_pAltGrCaptionLabel)
        m_pAltGrCaptionLabel->setText(UISoftKeyboard::tr("AltGr"));
   if (m_pShiftAltGrCaptionLabel)
        m_pShiftAltGrCaptionLabel->setText(UISoftKeyboard::tr("ShiftAltGr"));
    if (m_pCaptionEditGroupBox)
        m_pCaptionEditGroupBox->setTitle(UISoftKeyboard::tr("Captions"));
    if (m_pSelectedKeyGroupBox)
        m_pSelectedKeyGroupBox->setTitle(UISoftKeyboard::tr("Selected Key"));
}

void UIKeyboardLayoutEditor::prepareObjects()
{
    m_pEditorLayout = new QGridLayout;
    if (!m_pEditorLayout)
        return;
    setLayout(m_pEditorLayout);

    QHBoxLayout *pTitleLayout = new QHBoxLayout;
    m_pGoBackButton = new QToolButton;
    m_pGoBackButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_pGoBackButton->setIcon(UIIconPool::defaultIcon(UIIconPool::UIDefaultIconType_ArrowBack));
    m_pGoBackButton->setAutoRaise(true);
    m_pEditorLayout->addWidget(m_pGoBackButton, 0, 0, 1, 1);
    connect(m_pGoBackButton, &QToolButton::clicked, this, &UIKeyboardLayoutEditor::sigGoBackButton);
    m_pTitleLabel = new QLabel;
    pTitleLayout->addWidget(m_pTitleLabel);
    pTitleLayout->addStretch(2);
    pTitleLayout->addWidget(m_pGoBackButton);
    m_pEditorLayout->addLayout(pTitleLayout, 0, 0, 1, 2);

    m_pLayoutNativeNameLabel = new QLabel;
    m_pLayoutNativeNameEdit = new QLineEdit;
    m_pLayoutNativeNameLabel->setBuddy(m_pLayoutNativeNameEdit);
    m_pEditorLayout->addWidget(m_pLayoutNativeNameLabel, 2, 0, 1, 1);
    m_pEditorLayout->addWidget(m_pLayoutNativeNameEdit, 2, 1, 1, 1);
    connect(m_pLayoutNativeNameEdit, &QLineEdit::textChanged, this, &UIKeyboardLayoutEditor::sltLayoutNativeNameChanged);

    m_pLayoutNameLabel = new QLabel;
    m_pLayoutNameEdit = new QLineEdit;
    m_pLayoutNameLabel->setBuddy(m_pLayoutNameEdit);
    m_pEditorLayout->addWidget(m_pLayoutNameLabel, 3, 0, 1, 1);
    m_pEditorLayout->addWidget(m_pLayoutNameEdit, 3, 1, 1, 1);
    connect(m_pLayoutNameEdit, &QLineEdit::textChanged, this, &UIKeyboardLayoutEditor::sltLayoutNameChanged);


    m_pPhysicalLayoutLabel = new QLabel;
    m_pPhysicalLayoutCombo = new QComboBox;
    m_pPhysicalLayoutLabel->setBuddy(m_pPhysicalLayoutCombo);
    m_pEditorLayout->addWidget(m_pPhysicalLayoutLabel, 4, 0, 1, 1);
    m_pEditorLayout->addWidget(m_pPhysicalLayoutCombo, 4, 1, 1, 1);
    connect(m_pPhysicalLayoutCombo, &QComboBox::currentIndexChanged, this, &UIKeyboardLayoutEditor::sltPhysicalLayoutChanged);

    m_pSelectedKeyGroupBox = new QGroupBox;
    m_pSelectedKeyGroupBox->setEnabled(false);

    m_pEditorLayout->addWidget(m_pSelectedKeyGroupBox, 5, 0, 1, 2);
    QGridLayout *pSelectedKeyLayout = new QGridLayout(m_pSelectedKeyGroupBox);
    pSelectedKeyLayout->setSpacing(0);
    pSelectedKeyLayout->setContentsMargins(0, 0, 0, 0);

    m_pScanCodeLabel = new QLabel;
    m_pScanCodeEdit = new QLineEdit;
    m_pScanCodeLabel->setBuddy(m_pScanCodeEdit);
    m_pScanCodeEdit->setEnabled(false);
    pSelectedKeyLayout->addWidget(m_pScanCodeLabel, 0, 0);
    pSelectedKeyLayout->addWidget(m_pScanCodeEdit, 0, 1);

    m_pPositionLabel= new QLabel;
    m_pPositionEdit = new QLineEdit;
    m_pPositionEdit->setEnabled(false);
    m_pPositionLabel->setBuddy(m_pPositionEdit);
    pSelectedKeyLayout->addWidget(m_pPositionLabel, 1, 0);
    pSelectedKeyLayout->addWidget(m_pPositionEdit, 1, 1);

    QWidget *pCaptionEditor = prepareKeyCaptionEditWidgets();
    if (pCaptionEditor)
        pSelectedKeyLayout->addWidget(pCaptionEditor, 2, 0, 2, 2);

    QSpacerItem *pSpacer = new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Expanding);
    if (pSpacer)
        pSelectedKeyLayout->addItem(pSpacer, 4, 1);

    sltRetranslateUI();

    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIKeyboardLayoutEditor::sltRetranslateUI);
}

QWidget *UIKeyboardLayoutEditor::prepareKeyCaptionEditWidgets()
{
    m_pCaptionEditGroupBox = new QGroupBox;
    if (!m_pCaptionEditGroupBox)
        return 0;
    m_pCaptionEditGroupBox->setFlat(false);
    QGridLayout *pCaptionEditorLayout = new QGridLayout(m_pCaptionEditGroupBox);
    pCaptionEditorLayout->setSpacing(0);
    pCaptionEditorLayout->setContentsMargins(0, 0, 0, 0);

    if (!pCaptionEditorLayout)
        return 0;

    m_pBaseCaptionLabel = new QLabel;
    m_pBaseCaptionEdit = new QLineEdit;
    m_pBaseCaptionLabel->setBuddy(m_pBaseCaptionEdit);
    pCaptionEditorLayout->addWidget(m_pBaseCaptionLabel, 0, 0);
    pCaptionEditorLayout->addWidget(m_pBaseCaptionEdit, 0, 1);
    connect(m_pBaseCaptionEdit, &QLineEdit::textChanged, this, &UIKeyboardLayoutEditor::sltCaptionsUpdate);

    m_pShiftCaptionLabel = new QLabel;
    m_pShiftCaptionEdit = new QLineEdit;
    m_pShiftCaptionLabel->setBuddy(m_pShiftCaptionEdit);
    pCaptionEditorLayout->addWidget(m_pShiftCaptionLabel, 1, 0);
    pCaptionEditorLayout->addWidget(m_pShiftCaptionEdit, 1, 1);
    connect(m_pShiftCaptionEdit, &QLineEdit::textChanged, this, &UIKeyboardLayoutEditor::sltCaptionsUpdate);

    m_pAltGrCaptionLabel = new QLabel;
    m_pAltGrCaptionEdit = new QLineEdit;
    m_pAltGrCaptionLabel->setBuddy(m_pAltGrCaptionEdit);
    pCaptionEditorLayout->addWidget(m_pAltGrCaptionLabel, 2, 0);
    pCaptionEditorLayout->addWidget(m_pAltGrCaptionEdit, 2, 1);
    connect(m_pAltGrCaptionEdit, &QLineEdit::textChanged, this, &UIKeyboardLayoutEditor::sltCaptionsUpdate);

    m_pShiftAltGrCaptionLabel = new QLabel;
    m_pShiftAltGrCaptionEdit = new QLineEdit;
    m_pShiftAltGrCaptionLabel->setBuddy(m_pShiftAltGrCaptionEdit);
    pCaptionEditorLayout->addWidget(m_pShiftAltGrCaptionLabel, 3, 0);
    pCaptionEditorLayout->addWidget(m_pShiftAltGrCaptionEdit, 3, 1);
    connect(m_pShiftAltGrCaptionEdit, &QLineEdit::textChanged, this, &UIKeyboardLayoutEditor::sltCaptionsUpdate);

    QSpacerItem *pSpacer = new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Expanding);
    if (pSpacer)
        pCaptionEditorLayout->addItem(pSpacer, 4, 1);
    return m_pCaptionEditGroupBox;
}

void UIKeyboardLayoutEditor::reset()
{
    if (m_pLayoutNameEdit)
        m_pLayoutNameEdit->clear();
    resetKeyWidgets();
}

void UIKeyboardLayoutEditor::resetKeyWidgets()
{
    if (m_pScanCodeEdit)
        m_pScanCodeEdit->clear();
    if (m_pPositionEdit)
        m_pPositionEdit->clear();
    if (m_pBaseCaptionEdit)
        m_pBaseCaptionEdit->clear();
    if (m_pShiftCaptionEdit)
        m_pShiftCaptionEdit->clear();
    if (m_pAltGrCaptionEdit)
        m_pAltGrCaptionEdit->clear();
    if (m_pShiftAltGrCaptionEdit)
        m_pShiftAltGrCaptionEdit->clear();
}

/*********************************************************************************************************************************
*   UILayoutSelector implementation.                                                                                  *
*********************************************************************************************************************************/

UILayoutSelector::UILayoutSelector(QWidget *pParent /* = 0 */)
    :QWidget(pParent)
    , m_pLayoutListWidget(0)
    , m_pApplyLayoutButton(0)
    , m_pEditLayoutButton(0)
    , m_pCopyLayoutButton(0)
    , m_pSaveLayoutButton(0)
    , m_pDeleteLayoutButton(0)
    , m_pTitleLabel(0)
    , m_pCloseButton(0)
{
    prepareObjects();
}

void UILayoutSelector::setCurrentLayout(const QUuid &layoutUid)
{
    if (!m_pLayoutListWidget)
        return;
    if (layoutUid.isNull())
    {
        m_pLayoutListWidget->selectionModel()->clear();
        return;
    }
    QListWidgetItem *pFoundItem = 0;
    for (int i = 0; i < m_pLayoutListWidget->count() && !pFoundItem; ++i)
    {
        QListWidgetItem *pItem = m_pLayoutListWidget->item(i);
        if (!pItem)
            continue;
        if (pItem->data(Qt::UserRole).toUuid() == layoutUid)
            pFoundItem = pItem;
    }
    if (!pFoundItem)
        return;
    if (pFoundItem == m_pLayoutListWidget->currentItem())
        return;
    m_pLayoutListWidget->blockSignals(true);
    m_pLayoutListWidget->setCurrentItem(pFoundItem);
    m_pLayoutListWidget->blockSignals(false);
}

void UILayoutSelector::setCurrentLayoutIsEditable(bool fEditable)
{
    if (m_pEditLayoutButton)
        m_pEditLayoutButton->setEnabled(fEditable);
    if (m_pSaveLayoutButton)
        m_pSaveLayoutButton->setEnabled(fEditable);
    if (m_pDeleteLayoutButton)
        m_pDeleteLayoutButton->setEnabled(fEditable);
}

void UILayoutSelector::setLayoutList(const QStringList &layoutNames, QList<QUuid> layoutUidList)
{
    if (!m_pLayoutListWidget || layoutNames.size() != layoutUidList.size())
        return;
    QUuid currentItemUid;
    if (m_pLayoutListWidget->currentItem())
        currentItemUid = m_pLayoutListWidget->currentItem()->data(Qt::UserRole).toUuid();
    m_pLayoutListWidget->blockSignals(true);
    m_pLayoutListWidget->clear();
    for (int i = 0; i < layoutNames.size(); ++i)
    {
        QListWidgetItem *pItem = new QListWidgetItem(layoutNames[i], m_pLayoutListWidget);
        pItem->setData(Qt::UserRole, layoutUidList[i]);
        m_pLayoutListWidget->addItem(pItem);
        if (layoutUidList[i] == currentItemUid)
            m_pLayoutListWidget->setCurrentItem(pItem);
    }
    m_pLayoutListWidget->sortItems();
    m_pLayoutListWidget->blockSignals(false);
}

void UILayoutSelector::prepareObjects()
{
    QVBoxLayout *pLayout = new QVBoxLayout;
    if (!pLayout)
        return;
    pLayout->setSpacing(0);
    setLayout(pLayout);

    QHBoxLayout *pTitleLayout = new QHBoxLayout;
    m_pCloseButton = new QToolButton;
    m_pCloseButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_pCloseButton->setIcon(UIIconPool::defaultIcon(UIIconPool::UIDefaultIconType_DialogCancel));
    m_pCloseButton->setAutoRaise(true);
    connect(m_pCloseButton, &QToolButton::clicked, this, &UILayoutSelector::sigCloseLayoutList);
    m_pTitleLabel = new QLabel;
    pTitleLayout->addWidget(m_pTitleLabel);
    pTitleLayout->addStretch(2);
    pTitleLayout->addWidget(m_pCloseButton);
    pLayout->addLayout(pTitleLayout);

    m_pLayoutListWidget = new QListWidget;
    pLayout->addWidget(m_pLayoutListWidget);
    m_pLayoutListWidget->setSortingEnabled(true);
    connect(m_pLayoutListWidget, &QListWidget::currentItemChanged, this, &UILayoutSelector::sltCurrentItemChanged);

    m_pLayoutListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    QHBoxLayout *pButtonsLayout = new QHBoxLayout;
    pLayout->addLayout(pButtonsLayout);

    m_pEditLayoutButton = new QToolButton;
    m_pEditLayoutButton->setIcon(UIIconPool::iconSet(":/soft_keyboard_layout_edit_16px.png", ":/soft_keyboard_layout_edit_disabled_16px.png"));
    pButtonsLayout->addWidget(m_pEditLayoutButton);
    connect(m_pEditLayoutButton, &QToolButton::clicked, this, &UILayoutSelector::sigShowLayoutEditor);

    m_pCopyLayoutButton = new QToolButton;
    m_pCopyLayoutButton->setIcon(UIIconPool::iconSet(":/soft_keyboard_layout_copy_16px.png", ":/soft_keyboard_layout_copy_disabled_16px.png"));
    pButtonsLayout->addWidget(m_pCopyLayoutButton);
    connect(m_pCopyLayoutButton, &QToolButton::clicked, this, &UILayoutSelector::sigCopyLayout);

    m_pSaveLayoutButton = new QToolButton;
    m_pSaveLayoutButton->setIcon(UIIconPool::iconSet(":/soft_keyboard_layout_save_16px.png", ":/soft_keyboard_layout_save_disabled_16px.png"));
    pButtonsLayout->addWidget(m_pSaveLayoutButton);
    connect(m_pSaveLayoutButton, &QToolButton::clicked, this, &UILayoutSelector::sigSaveLayout);

    m_pDeleteLayoutButton = new QToolButton;
    m_pDeleteLayoutButton->setIcon(UIIconPool::iconSet(":/soft_keyboard_layout_remove_16px.png", ":/soft_keyboard_layout_remove_disabled_16px.png"));
    pButtonsLayout->addWidget(m_pDeleteLayoutButton);
    connect(m_pDeleteLayoutButton, &QToolButton::clicked, this, &UILayoutSelector::sigDeleteLayout);

    pButtonsLayout->addStretch(2);

    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UILayoutSelector::sltRetranslateUI);
}

void UILayoutSelector::sltCurrentItemChanged(QListWidgetItem *pCurrent, QListWidgetItem *pPrevious)
{
    Q_UNUSED(pPrevious);
    if (!pCurrent)
        return;
    emit sigLayoutSelectionChanged(pCurrent->data(Qt::UserRole).toUuid());
}

void UILayoutSelector::sltRetranslateUI()
{
    if (m_pApplyLayoutButton)
        m_pApplyLayoutButton->setToolTip(UISoftKeyboard::tr("Use the selected layout"));
    if (m_pEditLayoutButton)
        m_pEditLayoutButton->setToolTip(UISoftKeyboard::tr("Edit the selected layout"));
    if (m_pDeleteLayoutButton)
        m_pDeleteLayoutButton->setToolTip(UISoftKeyboard::tr("Delete the selected layout"));
    if (m_pCopyLayoutButton)
        m_pCopyLayoutButton->setToolTip(UISoftKeyboard::tr("Copy the selected layout"));
    if (m_pSaveLayoutButton)
        m_pSaveLayoutButton->setToolTip(UISoftKeyboard::tr("Save the selected layout into File"));
    if (m_pTitleLabel)
        m_pTitleLabel->setText(UISoftKeyboard::tr("Layout List"));
    if (m_pCloseButton)
    {
        m_pCloseButton->setToolTip(UISoftKeyboard::tr("Close the layout list"));
        m_pCloseButton->setText(UISoftKeyboard::tr("Close"));
    }
}


/*********************************************************************************************************************************
*   UISoftKeyboardRow implementation.                                                                                  *
*********************************************************************************************************************************/

UISoftKeyboardRow::UISoftKeyboardRow()
    : m_iDefaultWidth(0)
    , m_iDefaultHeight(0)
    , m_iSpaceHeightAfter(0)
    , m_iLeftMargin(0)
{
}

void UISoftKeyboardRow::setDefaultWidth(int iWidth)
{
    m_iDefaultWidth = iWidth;
}

int UISoftKeyboardRow::defaultWidth() const
{
    return m_iDefaultWidth;
}

int UISoftKeyboardRow::totalHeight() const
{
    int iMaxHeight = 0;
    for (int i = 0; i < m_keys.size(); ++i)
        iMaxHeight = qMax(iMaxHeight, m_keys[i].height());
    return iMaxHeight + m_iSpaceHeightAfter;
}

void UISoftKeyboardRow::setDefaultHeight(int iHeight)
{
    m_iDefaultHeight = iHeight;
}

int UISoftKeyboardRow::defaultHeight() const
{
    return m_iDefaultHeight;
}

QVector<UISoftKeyboardKey> &UISoftKeyboardRow::keys()
{
    return m_keys;
}

const QVector<UISoftKeyboardKey> &UISoftKeyboardRow::keys() const
{
    return m_keys;
}

void UISoftKeyboardRow::setSpaceHeightAfter(int iSpace)
{
    m_iSpaceHeightAfter = iSpace;
}

int UISoftKeyboardRow::spaceHeightAfter() const
{
    return m_iSpaceHeightAfter;
}

int UISoftKeyboardRow::leftMargin() const
{
    return m_iLeftMargin;
}

void UISoftKeyboardRow::setLeftMargin(int iMargin)
{
    m_iLeftMargin = iMargin;
}

/*********************************************************************************************************************************
*   UISoftKeyboardKey implementation.                                                                                  *
*********************************************************************************************************************************/

UISoftKeyboardKey::UISoftKeyboardKey()
    : m_enmType(KeyType_Ordinary)
    , m_enmState(KeyState_NotPressed)
    , m_iWidth(0)
    , m_iHeight(0)
    , m_iSpaceWidthAfter(0)
    , m_scanCode(0)
    , m_iCutoutWidth(0)
    , m_iCutoutHeight(0)
    , m_iCutoutCorner(-1)
    , m_iPosition(0)
    , m_pParentWidget(0)
    , m_iUsageId(0)
    , m_iUsagePage(0)
    , m_enmKeyboardRegion(KeyboardRegion_Main)
    , m_fIsOSMenuKey(false)
    , m_fCornerRadius(5.)
{
}

const QRect UISoftKeyboardKey::keyGeometry() const
{
    return m_keyGeometry;
}

void UISoftKeyboardKey::setKeyGeometry(const QRect &rect)
{
    m_keyGeometry = rect;
}


void UISoftKeyboardKey::setWidth(int iWidth)
{
    m_iWidth = iWidth;
}

int UISoftKeyboardKey::width() const
{
    return m_iWidth;
}

void UISoftKeyboardKey::setHeight(int iHeight)
{
    m_iHeight = iHeight;
}

int UISoftKeyboardKey::height() const
{
    return m_iHeight;
}

void UISoftKeyboardKey::setScanCode(LONG scanCode)
{
    m_scanCode = scanCode;
}

LONG UISoftKeyboardKey::scanCode() const
{
    return m_scanCode;
}

void UISoftKeyboardKey::addScanCodePrefix(LONG scanCodePrefix)
{
    m_scanCodePrefix << scanCodePrefix;
}

void UISoftKeyboardKey::setSpaceWidthAfter(int iSpace)
{
    m_iSpaceWidthAfter = iSpace;
}

int UISoftKeyboardKey::spaceWidthAfter() const
{
    return m_iSpaceWidthAfter;
}

void UISoftKeyboardKey::setUsageId(LONG usageId)
{
    m_iUsageId = usageId;
}

void UISoftKeyboardKey::setUsagePage(LONG usagePage)
{
    m_iUsagePage = usagePage;
}

QPair<LONG, LONG> UISoftKeyboardKey::usagePageIdPair() const
{
    return QPair<LONG, LONG>(m_iUsageId, m_iUsagePage);
}

void UISoftKeyboardKey::setPosition(int iPosition)
{
    m_iPosition = iPosition;
}

int UISoftKeyboardKey::position() const
{
    return m_iPosition;
}

void UISoftKeyboardKey::setType(KeyType enmType)
{
    m_enmType = enmType;
}

KeyType UISoftKeyboardKey::type() const
{
    return m_enmType;
}

KeyboardRegion UISoftKeyboardKey::keyboardRegion() const
{
    return m_enmKeyboardRegion;
}

void UISoftKeyboardKey::setKeyboardRegion(KeyboardRegion enmRegion)
{
    m_enmKeyboardRegion = enmRegion;
}

void UISoftKeyboardKey::setCutout(int iCorner, int iWidth, int iHeight)
{
    m_iCutoutCorner = iCorner;
    m_iCutoutWidth = iWidth;
    m_iCutoutHeight = iHeight;
}

KeyState UISoftKeyboardKey::state() const
{
    return m_enmState;
}

void UISoftKeyboardKey::setState(KeyState state)
{
    m_enmState = state;
}

void UISoftKeyboardKey::setStaticCaption(const QString &strCaption)
{
    m_strStaticCaption = strCaption;
}

const QString &UISoftKeyboardKey::staticCaption() const
{
    return m_strStaticCaption;
}

void UISoftKeyboardKey::setImageByName(const QString &strImageFileName)
{
    if (strImageFileName.isEmpty())
        return;
    m_icon = UIIconPool::iconSet(QString(":/%1").arg(strImageFileName));
}

QPixmap UISoftKeyboardKey::image() const
{
    QPixmap pixmap;

    /* Check whether we have valid icon: */
    if (!m_icon.isNull())
    {
        /* Determine desired icon size: */
        const int iIconMetric = QApplication::style()->pixelMetric(QStyle::PM_LargeIconSize);
        const QSize iconSize = QSize(iIconMetric, iIconMetric);
        /* Get pixmap of requested size (take into account the DPI of the main shown window, if possible): */
        const qreal fDevicePixelRatio = windowManager().mainWindowShown() && windowManager().mainWindowShown()->windowHandle()
                                      ? windowManager().mainWindowShown()->windowHandle()->devicePixelRatio() : 1;
        pixmap = m_icon.pixmap(iconSize, fDevicePixelRatio);
    }

    return pixmap;
}

void UISoftKeyboardKey::setParentWidget(UISoftKeyboardWidget* pParent)
{
    m_pParentWidget = pParent;
}

void UISoftKeyboardKey::setIsOSMenuKey(bool fFlag)
{
    m_fIsOSMenuKey = fFlag;
}

bool UISoftKeyboardKey::isOSMenuKey() const
{
    return m_fIsOSMenuKey;
}

void UISoftKeyboardKey::release()
{
    /* Lock key states are controlled by the event signals we get from the guest OS. See updateLockKeyState function: */
    if (m_enmType != KeyType_Lock)
        updateState(false);
}

void UISoftKeyboardKey::press()
{
    /* Lock key states are controlled by the event signals we get from the guest OS. See updateLockKeyState function: */
    if (m_enmType != KeyType_Lock)
        updateState(true);
}

void UISoftKeyboardKey::setPoints(const QVector<QPointF> &points)
{
    m_points = points;
    computePainterPath();
}

const QVector<QPointF> &UISoftKeyboardKey::points() const
{
    return m_points;
}

const QPainterPath &UISoftKeyboardKey::painterPath() const
{
    return m_painterPath;
}

void UISoftKeyboardKey::computePainterPath()
{
    if (m_points.size() < 3)
        return;

    m_painterPath = QPainterPath(pointInBetween(m_fCornerRadius, m_points[0], m_points[1]));
    for (int i = 0; i < m_points.size(); ++i)
    {
        QPointF p0 = pointInBetween(m_fCornerRadius, m_points[(i+1)%m_points.size()], m_points[i]);
        QPointF p1 = pointInBetween(m_fCornerRadius, m_points[(i+1)%m_points.size()], m_points[(i+2)%m_points.size()]);
        m_painterPath.lineTo(p0);
        m_painterPath.quadTo(m_points[(i+1)%m_points.size()], p1);
    }
}

void UISoftKeyboardKey::setCornerRadius(float fCornerRadius)
{
    m_fCornerRadius = fCornerRadius;
}

QPolygonF UISoftKeyboardKey::polygonInGlobal() const
{
    QPolygonF globalPolygon(m_points);
    globalPolygon.translate(m_keyGeometry.x(), m_keyGeometry.y());
    return globalPolygon;
}

int UISoftKeyboardKey::cutoutCorner() const
{
    return m_iCutoutCorner;
}

int UISoftKeyboardKey::cutoutWidth() const
{
    return m_iCutoutWidth;
}

int UISoftKeyboardKey::cutoutHeight() const
{
    return m_iCutoutHeight;
}

void UISoftKeyboardKey::updateState(bool fPressed)
{
    KeyState enmPreviousState = state();
    if (m_enmType == KeyType_Modifier)
    {
        if (fPressed)
        {
            if (m_enmState == KeyState_NotPressed)
                m_enmState = KeyState_Pressed;
            else if(m_enmState == KeyState_Pressed)
                m_enmState = KeyState_Locked;
            else
                m_enmState = KeyState_NotPressed;
        }
        else
        {
            if(m_enmState == KeyState_Pressed)
                m_enmState = KeyState_NotPressed;
        }
    }
    else if (m_enmType == KeyType_Lock)
    {
        m_enmState = fPressed ? KeyState_Locked : KeyState_NotPressed;
    }
    else if (m_enmType == KeyType_Ordinary)
    {
        if (m_enmState == KeyState_NotPressed)
            m_enmState = KeyState_Pressed;
        else
            m_enmState = KeyState_NotPressed;
    }
    if (enmPreviousState != state() && m_pParentWidget)
        m_pParentWidget->keyStateChange(this);
}

void UISoftKeyboardKey::updateLockState(bool fLocked)
{
    if (m_enmType != KeyType_Lock)
        return;
    if (fLocked && m_enmState == KeyState_Locked)
        return;
    if (!fLocked && m_enmState == KeyState_NotPressed)
        return;
    updateState(fLocked);
}

void UISoftKeyboardKey::reset()
{
    m_enmState = KeyState_NotPressed;
}


/*********************************************************************************************************************************
*   UISoftKeyboardLayout implementation.                                                                                  *
*********************************************************************************************************************************/

UISoftKeyboardLayout::UISoftKeyboardLayout()
    : m_fEditable(true)
    , m_fIsFromResources(false)
    , m_fEditedButNotSaved(false)
    , m_uid(QUuid::createUuid())
{
}

QString UISoftKeyboardLayout::nameString() const
{
    QString strCombinedName;
    if (nativeName().isEmpty() && !name().isEmpty())
        strCombinedName = name();
    else if (!nativeName().isEmpty() && name().isEmpty())
        strCombinedName = nativeName();
    else
        strCombinedName = QString("%1 (%2)").arg(nativeName()).arg(name());
    return strCombinedName;
}

void UISoftKeyboardLayout::setSourceFilePath(const QString& strSourceFilePath)
{
    m_strSourceFilePath = strSourceFilePath;
    setEditedBuNotSaved(true);
}

const QString& UISoftKeyboardLayout::sourceFilePath() const
{
    return m_strSourceFilePath;
}

void UISoftKeyboardLayout::setIsFromResources(bool fIsFromResources)
{
    m_fIsFromResources = fIsFromResources;
    setEditedBuNotSaved(true);
}

bool UISoftKeyboardLayout::isFromResources() const
{
    return m_fIsFromResources;
}

void UISoftKeyboardLayout::setName(const QString &strName)
{
    m_strName = strName;
    setEditedBuNotSaved(true);
}

const QString &UISoftKeyboardLayout::name() const
{
    return m_strName;
}

void UISoftKeyboardLayout::setNativeName(const QString &strNativeName)
{
    m_strNativeName = strNativeName;
    setEditedBuNotSaved(true);
}

const QString &UISoftKeyboardLayout::nativeName() const
{
    return m_strNativeName;
}

void UISoftKeyboardLayout::setEditable(bool fEditable)
{
    m_fEditable = fEditable;
    setEditedBuNotSaved(true);
}

bool UISoftKeyboardLayout::editable() const
{
    return m_fEditable;
}

void UISoftKeyboardLayout::setPhysicalLayoutUuid(const QUuid &uuid)
{
    m_physicalLayoutUuid = uuid;
    setEditedBuNotSaved(true);
}

const QUuid &UISoftKeyboardLayout::physicalLayoutUuid() const
{
    return m_physicalLayoutUuid;
}

void UISoftKeyboardLayout::addOrUpdateUIKeyCaptions(int iKeyPosition, const UIKeyCaptions &keyCaptions)
{
    if (m_keyCaptionsMap[iKeyPosition] == keyCaptions)
        return;
    m_keyCaptionsMap[iKeyPosition] = keyCaptions;
    /* Updating the captions invalidates the cached font size. We set it to 0, thereby forcing its recomputaion: */
    m_keyCaptionsFontSizeMap[iKeyPosition] = 0;
    setEditedBuNotSaved(true);
}

UIKeyCaptions UISoftKeyboardLayout::keyCaptions(int iKeyPosition) const
{
    return m_keyCaptionsMap[iKeyPosition];
}

bool UISoftKeyboardLayout::operator==(const UISoftKeyboardLayout &otherLayout) const
{
    if (m_strName != otherLayout.m_strName)
        return false;
    if (m_strNativeName != otherLayout.m_strNativeName)
        return false;
    if (m_physicalLayoutUuid != otherLayout.m_physicalLayoutUuid)
        return false;
    if (m_fEditable != otherLayout.m_fEditable)
        return false;
    if (m_strSourceFilePath != otherLayout.m_strSourceFilePath)
        return false;
    if (m_fIsFromResources != otherLayout.m_fIsFromResources)
        return false;
    return true;
}

QString UISoftKeyboardLayout::baseCaption(int iKeyPosition) const
{
    return m_keyCaptionsMap.value(iKeyPosition, UIKeyCaptions()).m_strBase;
}

QString UISoftKeyboardLayout::shiftCaption(int iKeyPosition) const
{
    if (!m_keyCaptionsMap.contains(iKeyPosition))
        return QString();
    return m_keyCaptionsMap[iKeyPosition].m_strShift;
}

QString UISoftKeyboardLayout::altGrCaption(int iKeyPosition) const
{
    if (!m_keyCaptionsMap.contains(iKeyPosition))
        return QString();
    return m_keyCaptionsMap[iKeyPosition].m_strAltGr;
}

QString UISoftKeyboardLayout::shiftAltGrCaption(int iKeyPosition) const
{
    if (!m_keyCaptionsMap.contains(iKeyPosition))
        return QString();
    return m_keyCaptionsMap[iKeyPosition].m_strShiftAltGr;
}

void UISoftKeyboardLayout::setEditedBuNotSaved(bool fEditedButNotsaved)
{
    m_fEditedButNotSaved = fEditedButNotsaved;
}

bool UISoftKeyboardLayout::editedButNotSaved() const
{
    return m_fEditedButNotSaved;
}

void UISoftKeyboardLayout::setUid(const QUuid &uid)
{
    m_uid = uid;
    setEditedBuNotSaved(true);
}

QUuid UISoftKeyboardLayout::uid() const
{
    return m_uid;
}

void UISoftKeyboardLayout::drawTextInRect(const UISoftKeyboardKey &key, QPainter &painter)
{
     int iKeyPosition = key.position();
     const QRect &keyGeometry = key.keyGeometry();
     QFont painterFont(painter.font());

     QString strBaseCaption;
     QString strShiftCaption;
     QString strShiftAltGrCaption;
     QString strAltGrCaption;

     /* Static captions which are defined in the physical layout files have precedence over
        the one define in the keyboard layouts. In effect they stay the same for all the
        keyboard layouts sharing the same physical layout: */

     if (key.staticCaption().isEmpty())
     {
         strBaseCaption = baseCaption(iKeyPosition);
         strShiftCaption = shiftCaption(iKeyPosition);
         strShiftAltGrCaption = shiftAltGrCaption(iKeyPosition);
         strAltGrCaption = altGrCaption(iKeyPosition);
     }
     else
         strBaseCaption = key.staticCaption();

     const QString &strTopleftString = !strShiftCaption.isEmpty() ? strShiftCaption : strBaseCaption;
     const QString &strBottomleftString = !strShiftCaption.isEmpty() ? strBaseCaption : QString();

     int iFontSize = 30;
     if (!m_keyCaptionsFontSizeMap.contains(iKeyPosition) || m_keyCaptionsFontSizeMap.value(iKeyPosition) == 0)
     {
         do
         {
             painterFont.setPixelSize(iFontSize);
             painterFont.setBold(true);
             painter.setFont(painterFont);
             const QFontMetrics fontMetrics = painter.fontMetrics();
             const int iMargin = 0.25 * fontMetrics.horizontalAdvance('X');

             int iTopWidth = 0;
             /* Some captions are multi line using \n as separator: */
             QStringList strList;
             strList << strTopleftString.split("\n", Qt::SkipEmptyParts)
                     << strShiftAltGrCaption.split("\n", Qt::SkipEmptyParts);
             foreach (const QString &strPart, strList)
                 iTopWidth = qMax(iTopWidth, fontMetrics.horizontalAdvance(strPart));
             strList.clear();
             strList << strBottomleftString.split("\n", Qt::SkipEmptyParts)
                     << strAltGrCaption.split("\n", Qt::SkipEmptyParts);

             int iBottomWidth = 0;
             foreach (const QString &strPart, strList)
                 iBottomWidth = qMax(iBottomWidth, fontMetrics.horizontalAdvance(strPart));
             int iTextWidth =  2 * iMargin + qMax(iTopWidth, iBottomWidth);
             int iTextHeight = 0;

             if (key.keyboardRegion() == KeyboardRegion_MultimediaKeys)
                 iTextHeight = 2 * iMargin + fontMetrics.height();
             else
                 iTextHeight = 2 * iMargin + 2 * fontMetrics.height();

             if (iTextWidth >= keyGeometry.width() || iTextHeight >= keyGeometry.height())
                 --iFontSize;
             else
                 break;

         }while(iFontSize > 1);
         m_keyCaptionsFontSizeMap[iKeyPosition] = iFontSize;
     }
     else
     {
         iFontSize = m_keyCaptionsFontSizeMap[iKeyPosition];
         painterFont.setPixelSize(iFontSize);
         painterFont.setBold(true);
         painter.setFont(painterFont);
     }

     const QFontMetrics fontMetrics = painter.fontMetrics();
     const int iMargin = 0.25 * fontMetrics.horizontalAdvance('X');
     QRect textRect;
     if (key.keyboardRegion() == KeyboardRegion_MultimediaKeys)
         textRect = QRect(2 * iMargin, iMargin,
                          keyGeometry.width() - 2 * iMargin,
                          keyGeometry.height() - 2 * iMargin);
     else
         textRect = QRect(iMargin, iMargin,
                          keyGeometry.width() - 2 * iMargin,
                          keyGeometry.height() - 2 * iMargin);

     if (key.keyboardRegion() == KeyboardRegion_MultimediaKeys)
     {
         painter.drawText(QRect(0, 0, keyGeometry.width(), keyGeometry.height()),
                          Qt::AlignHCenter | Qt::AlignVCenter, strTopleftString);
     }
     else
     {
         painter.drawText(textRect, Qt::AlignLeft | Qt::AlignTop, strTopleftString);
         painter.drawText(textRect, Qt::AlignLeft | Qt::AlignBottom, strBottomleftString);
         painter.drawText(textRect, Qt::AlignRight | Qt::AlignTop, strShiftAltGrCaption);
         painter.drawText(textRect, Qt::AlignRight | Qt::AlignBottom, strAltGrCaption);
     }
}

void UISoftKeyboardLayout::drawKeyImageInRect(const UISoftKeyboardKey &key, QPainter &painter)
{
    if (key.image().isNull())
        return;
    const QRect &keyGeometry = key.keyGeometry();
    int iMargin = 0.1 * qMax(keyGeometry.width(), keyGeometry.height());
    int size = qMin(keyGeometry.width() - 2 * iMargin, keyGeometry.height() - 2 * iMargin);
    painter.drawPixmap(QRect(0.5 * (keyGeometry.width() - size), 0.5 * (keyGeometry.height() - size),
                            size, size), key.image());
}


/*********************************************************************************************************************************
*   UISoftKeyboardColorTheme implementation.                                                                                     *
*********************************************************************************************************************************/

UISoftKeyboardColorTheme::UISoftKeyboardColorTheme()
    : m_colors(QVector<QColor>(KeyboardColorType_Max))
    , m_fIsEditable(false)
{
    m_colors[KeyboardColorType_Background].setNamedColor("#ff878787");
    m_colors[KeyboardColorType_Font].setNamedColor("#ff000000");
    m_colors[KeyboardColorType_Hover].setNamedColor("#ff676767");
    m_colors[KeyboardColorType_Edit].setNamedColor("#ff9b6767");
    m_colors[KeyboardColorType_Pressed].setNamedColor("#fffafafa");
}

UISoftKeyboardColorTheme::UISoftKeyboardColorTheme(const QString &strName,
                                                   const QString &strBackgroundColor,
                                                   const QString &strNormalFontColor,
                                                   const QString &strHoverColor,
                                                   const QString &strEditedButtonBackgroundColor,
                                                   const QString &strPressedButtonFontColor)
    :m_colors(QVector<QColor>(KeyboardColorType_Max))
    ,m_strName(strName)
    , m_fIsEditable(false)
{
    m_colors[KeyboardColorType_Background].setNamedColor(strBackgroundColor);
    m_colors[KeyboardColorType_Font].setNamedColor(strNormalFontColor);
    m_colors[KeyboardColorType_Hover].setNamedColor(strHoverColor);
    m_colors[KeyboardColorType_Edit].setNamedColor(strEditedButtonBackgroundColor);
    m_colors[KeyboardColorType_Pressed].setNamedColor(strPressedButtonFontColor);
}


void UISoftKeyboardColorTheme::setColor(KeyboardColorType enmColorType, const QColor &color)
{
    if ((int) enmColorType >= m_colors.size())
        return;
    m_colors[(int)enmColorType] = color;
}

QColor UISoftKeyboardColorTheme::color(KeyboardColorType enmColorType) const
{
    if ((int) enmColorType >= m_colors.size())
        return QColor();
    return m_colors[(int)enmColorType];
}

QStringList UISoftKeyboardColorTheme::colorsToStringList() const
{
    QStringList colorStringList;
    foreach (const QColor &color, m_colors)
        colorStringList << color.name(QColor::HexArgb);
    return colorStringList;
}

void UISoftKeyboardColorTheme::colorsFromStringList(const QStringList &colorStringList)
{
    for (int i = 0; i < colorStringList.size() && i < m_colors.size(); ++i)
    {
        if (!QColor::isValidColor(colorStringList[i]))
            continue;
        m_colors[i].setNamedColor(colorStringList[i]);
    }
}

const QString &UISoftKeyboardColorTheme::name() const
{
    return m_strName;
}

void UISoftKeyboardColorTheme::setName(const QString &strName)
{
    m_strName = strName;
}

bool UISoftKeyboardColorTheme::isEditable() const
{
    return m_fIsEditable;
}

void UISoftKeyboardColorTheme::setIsEditable(bool fIsEditable)
{
    m_fIsEditable = fIsEditable;
}

/*********************************************************************************************************************************
*   UISoftKeyboardWidget implementation.                                                                                  *
*********************************************************************************************************************************/

UISoftKeyboardWidget::UISoftKeyboardWidget(QWidget *pParent, UIMachine *pMachine)
    : QWidget(pParent)
    , m_pMachine(pMachine)
    , m_pKeyUnderMouse(0)
    , m_pKeyBeingEdited(0)
    , m_pKeyPressed(0)
    , m_currentColorTheme(0)
    , m_iInitialHeight(0)
    , m_iInitialWidth(0)
    , m_iInitialWidthNoNumPad(0)
    , m_iBeforeNumPadWidth(30)
    , m_iXSpacing(5)
    , m_iYSpacing(5)
    , m_iLeftMargin(10)
    , m_iTopMargin(10)
    , m_iRightMargin(10)
    , m_iBottomMargin(10)
    , m_enmMode(Mode_Keyboard)
    , m_fHideOSMenuKeys(false)
    , m_fHideNumPad(false)
    , m_fHideMultimediaKeys(false)
{
    prepareObjects();
    prepareColorThemes();
    sltRetranslateUI();

    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UISoftKeyboardWidget::sltRetranslateUI);
}

QSize UISoftKeyboardWidget::minimumSizeHint() const
{
    float fScale = 0.5f;
    return QSize(fScale * m_minimumSize.width(), fScale * m_minimumSize.height());
}

QSize UISoftKeyboardWidget::sizeHint() const
{
    float fScale = 0.5f;
    return QSize(fScale * m_minimumSize.width(), fScale * m_minimumSize.height());
}

void UISoftKeyboardWidget::paintEvent(QPaintEvent *pEvent) /* RT_OVERRIDE */
{
    Q_UNUSED(pEvent);
    if (!m_layouts.contains(m_uCurrentLayoutId))
        return;

    UISoftKeyboardLayout &currentLayout = m_layouts[m_uCurrentLayoutId];

    if (m_iInitialWidth == 0 || m_iInitialWidthNoNumPad == 0 || m_iInitialHeight == 0)
        return;

    if (!m_fHideNumPad)
        m_fScaleFactorX = width() / (float) (m_iInitialWidth + m_iBeforeNumPadWidth);
    else
        m_fScaleFactorX = width() / (float) m_iInitialWidthNoNumPad;

    if (!m_fHideMultimediaKeys)
        m_fScaleFactorY = height() / (float) m_iInitialHeight;
    else
        m_fScaleFactorY = height() / (float)(m_iInitialHeight - m_multiMediaKeysLayout.totalHeight());

    QPainter painter(this);
    QFont painterFont(font());
    painterFont.setPixelSize(15);
    painterFont.setBold(true);
    painter.setFont(painterFont);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.scale(m_fScaleFactorX, m_fScaleFactorY);
    int unitSize = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin);
    float fLedRadius =  0.8 * unitSize;
    float fLedMargin =  5;//0.6 * unitSize;

    UISoftKeyboardPhysicalLayout *pPhysicalLayout = findPhysicalLayout(currentLayout.physicalLayoutUuid());
    if (!pPhysicalLayout)
        return;

    QVector<UISoftKeyboardRow> &rows = pPhysicalLayout->rows();
    for (int i = 0; i < rows.size(); ++i)
    {
        QVector<UISoftKeyboardKey> &keys = rows[i].keys();
        for (int j = 0; j < keys.size(); ++j)
        {
            UISoftKeyboardKey &key = keys[j];

            if (m_fHideOSMenuKeys && key.isOSMenuKey())
                continue;

            if (m_fHideNumPad && key.keyboardRegion() == KeyboardRegion_NumPad)
                continue;

            if (m_fHideMultimediaKeys && key.keyboardRegion() == KeyboardRegion_MultimediaKeys)
                continue;

            if (m_fHideMultimediaKeys)
                painter.translate(key.keyGeometry().x(), key.keyGeometry().y() - m_multiMediaKeysLayout.totalHeight());
            else
                painter.translate(key.keyGeometry().x(), key.keyGeometry().y());

            if(&key  == m_pKeyBeingEdited)
                painter.setBrush(QBrush(color(KeyboardColorType_Edit)));
            else if (&key  == m_pKeyUnderMouse)
                painter.setBrush(QBrush(color(KeyboardColorType_Hover)));
            else
                painter.setBrush(QBrush(color(KeyboardColorType_Background)));

            if (&key  == m_pKeyPressed)
                painter.setPen(QPen(color(KeyboardColorType_Pressed), 2));
            else
                painter.setPen(QPen(color(KeyboardColorType_Font), 2));

            /* Draw the key shape: */
            painter.drawPath(key.painterPath());

            if (key.keyboardRegion() == KeyboardRegion_MultimediaKeys)
                currentLayout.drawKeyImageInRect(key, painter);
            else
                currentLayout.drawTextInRect(key, painter);
            /* Draw small LED like circles on the modifier/lock keys: */
            if (key.type() != KeyType_Ordinary)
            {
                QColor ledColor;
                if (key.type() == KeyType_Lock)
                {
                    if (key.state() == KeyState_NotPressed)
                        ledColor = color(KeyboardColorType_Font);
                    else
                        ledColor = QColor(0, 255, 0);
                }
                else
                {
                    if (key.state() == KeyState_NotPressed)
                        ledColor = color(KeyboardColorType_Font);
                    else if (key.state() == KeyState_Pressed)
                        ledColor = QColor(0, 191, 204);
                    else
                        ledColor = QColor(255, 50, 50);
                }
                if (m_enmMode == Mode_LayoutEdit)
                    ledColor = color(KeyboardColorType_Font);
                painter.setBrush(ledColor);
                painter.setPen(ledColor);
                QRectF rectangle(key.keyGeometry().width() - 2 * fLedMargin, key.keyGeometry().height() - 2 * fLedMargin,
                                 fLedRadius, fLedRadius);
                painter.drawEllipse(rectangle);
            }
            if (m_fHideMultimediaKeys)
                painter.translate(-key.keyGeometry().x(), -key.keyGeometry().y() + m_multiMediaKeysLayout.totalHeight());
            else
                painter.translate(-key.keyGeometry().x(), -key.keyGeometry().y());
        }
    }
}

void UISoftKeyboardWidget::mousePressEvent(QMouseEvent *pEvent)
{
    QWidget::mousePressEvent(pEvent);
    if (pEvent->button() != Qt::RightButton && pEvent->button() != Qt::LeftButton)
        return;

    m_pKeyPressed = keyUnderMouse(pEvent);
    if (!m_pKeyPressed)
        return;

    /* Handling the right button press: */
    if (pEvent->button() == Qt::RightButton)
        modifierKeyPressRelease(m_pKeyPressed, false);
    else
    {
        /* Handling the left button press: */
        if (m_enmMode == Mode_Keyboard)
            handleKeyPress(m_pKeyPressed);
        else if (m_enmMode == Mode_LayoutEdit)
            setKeyBeingEdited(m_pKeyUnderMouse);
    }
    update();
}

void UISoftKeyboardWidget::mouseReleaseEvent(QMouseEvent *pEvent)
{
    QWidget::mouseReleaseEvent(pEvent);

    if (pEvent->button() != Qt::RightButton && pEvent->button() != Qt::LeftButton)
        return;

    if (!m_pKeyPressed)
        return;
    if (pEvent->button() == Qt::RightButton)
        modifierKeyPressRelease(m_pKeyPressed, true);
    else
    {
        if (m_enmMode == Mode_Keyboard)
            handleKeyRelease(m_pKeyPressed);
    }
    m_pKeyPressed = 0;
    update();
}

void UISoftKeyboardWidget::mouseMoveEvent(QMouseEvent *pEvent)
{
    QWidget::mouseMoveEvent(pEvent);
    UISoftKeyboardKey *pPreviousKeyUnderMouse = m_pKeyUnderMouse;
    keyUnderMouse(pEvent);
    if (pPreviousKeyUnderMouse != m_pKeyUnderMouse)
        showKeyTooltip(m_pKeyUnderMouse);
}

void UISoftKeyboardWidget::sltRetranslateUI()
{
    m_keyTooltips[317] = UISoftKeyboard::tr("Power off");
    m_keyTooltips[300] = UISoftKeyboard::tr("Web browser go back");
    m_keyTooltips[301] = UISoftKeyboard::tr("Web browser go the home page");
    m_keyTooltips[302] = UISoftKeyboard::tr("Web browser go forward");
    m_keyTooltips[315] = UISoftKeyboard::tr("Web browser reload the current page");
    m_keyTooltips[314] = UISoftKeyboard::tr("Web browser stop loading the page");
    m_keyTooltips[313] = UISoftKeyboard::tr("Web browser search");

    m_keyTooltips[307] = UISoftKeyboard::tr("Jump back to previous media track");
    m_keyTooltips[308] = UISoftKeyboard::tr("Jump to next media track");
    m_keyTooltips[309] = UISoftKeyboard::tr("Stop playing");
    m_keyTooltips[310] = UISoftKeyboard::tr("Play or pause playing");

    m_keyTooltips[303] = UISoftKeyboard::tr("Start email application");
    m_keyTooltips[311] = UISoftKeyboard::tr("Start calculator");
    m_keyTooltips[312] = UISoftKeyboard::tr("Show 'My Computer'");
    m_keyTooltips[316] = UISoftKeyboard::tr("Show Media folder");

    m_keyTooltips[304] = UISoftKeyboard::tr("Mute");
    m_keyTooltips[305] = UISoftKeyboard::tr("Volume down");
    m_keyTooltips[306] = UISoftKeyboard::tr("Volume up");
}

void UISoftKeyboardWidget::saveCurentLayoutToFile()
{
    if (!m_layouts.contains(m_uCurrentLayoutId))
        return;
    UISoftKeyboardLayout &currentLayout = m_layouts[m_uCurrentLayoutId];
    QString strHomeFolder = gpGlobalSession->homeFolder();
    QDir dir(strHomeFolder);
    if (!dir.exists(strSubDirectorName))
    {
        if (!dir.mkdir(strSubDirectorName))
        {
            sigStatusBarMessage(QString("%1 %2").arg(UISoftKeyboard::tr("Error! Could not create folder under").arg(strHomeFolder)));
            return;
        }
    }

    strHomeFolder += QString(QDir::separator()) + strSubDirectorName;
    QInputDialog dialog(this);
    dialog.setInputMode(QInputDialog::TextInput);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.setWindowTitle(UISoftKeyboard::tr("Provide a file name"));
    dialog.setTextValue(currentLayout.name());
    dialog.setLabelText(QString("%1 %2").arg(UISoftKeyboard::tr("The file will be saved under:<br>")).arg(strHomeFolder));
    if (dialog.exec() == QDialog::Rejected)
        return;
    QString strFileName(dialog.textValue());
    if (strFileName.isEmpty() || strFileName.contains("..") || strFileName.contains(QDir::separator()))
    {
        sigStatusBarMessage(QString("%1 %2").arg(strFileName).arg(UISoftKeyboard::tr(" is an invalid file name")));
        return;
    }

    UISoftKeyboardPhysicalLayout *pPhysicalLayout = findPhysicalLayout(currentLayout.physicalLayoutUuid());
    if (!pPhysicalLayout)
    {
        sigStatusBarMessage("The layout file could not be saved");
        return;
    }

    QFileInfo fileInfo(strFileName);
    if (fileInfo.suffix().compare("xml", Qt::CaseInsensitive) != 0)
        strFileName += ".xml";
    strFileName = strHomeFolder + QString(QDir::separator()) + strFileName;
    QFile xmlFile(strFileName);
    if (!xmlFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        sigStatusBarMessage("The layout file could not be saved");
        return;
    }

    QXmlStreamWriter xmlWriter;
    xmlWriter.setDevice(&xmlFile);

    xmlWriter.setAutoFormatting(true);
    xmlWriter.writeStartDocument("1.0");
    xmlWriter.writeStartElement("layout");
    xmlWriter.writeTextElement("name", currentLayout.name());
    xmlWriter.writeTextElement("nativename", currentLayout.nativeName());
    xmlWriter.writeTextElement("physicallayoutid", pPhysicalLayout->uid().toString());
    xmlWriter.writeTextElement("id", currentLayout.uid().toString());

    QVector<UISoftKeyboardRow> &rows = pPhysicalLayout->rows();
    for (int i = 0; i < rows.size(); ++i)
    {
        QVector<UISoftKeyboardKey> &keys = rows[i].keys();

       for (int j = 0; j < keys.size(); ++j)
       {
           xmlWriter.writeStartElement("key");

           UISoftKeyboardKey &key = keys[j];
           xmlWriter.writeTextElement("position", QString::number(key.position()));
           xmlWriter.writeTextElement("basecaption", currentLayout.baseCaption(key.position()));
           xmlWriter.writeTextElement("shiftcaption", currentLayout.shiftCaption(key.position()));
           xmlWriter.writeTextElement("altgrcaption", currentLayout.altGrCaption(key.position()));
           xmlWriter.writeTextElement("shiftaltgrcaption", currentLayout.shiftAltGrCaption(key.position()));
           xmlWriter.writeEndElement();
       }
   }
   xmlWriter.writeEndElement();
   xmlWriter.writeEndDocument();

   xmlFile.close();
   currentLayout.setSourceFilePath(strFileName);
   currentLayout.setEditedBuNotSaved(false);
   sigStatusBarMessage(QString("%1 %2").arg(strFileName).arg(UISoftKeyboard::tr(" is saved")));
}

void UISoftKeyboardWidget::copyCurentLayout()
{
    UISoftKeyboardLayout newLayout(m_layouts[m_uCurrentLayoutId]);

    QString strNewName = QString("%1-%2").arg(newLayout.name()).arg(UISoftKeyboard::tr("Copy"));
    int iCount = 1;
    while (layoutByNameExists(strNewName))
    {
        strNewName = QString("%1-%2-%3").arg(newLayout.name()).arg(UISoftKeyboard::tr("Copy")).arg(QString::number(iCount));
        ++iCount;
    }

    newLayout.setName(strNewName);
    newLayout.setEditedBuNotSaved(true);
    newLayout.setEditable(true);
    newLayout.setIsFromResources(false);
    newLayout.setSourceFilePath(QString());
    newLayout.setUid(QUuid::createUuid());
    addLayout(newLayout);
}

float UISoftKeyboardWidget::layoutAspectRatio()
{
    if (m_iInitialWidth == 0)
        return 1.f;
    return  m_iInitialHeight / (float) m_iInitialWidth;
}

bool UISoftKeyboardWidget::hideOSMenuKeys() const
{
    return m_fHideOSMenuKeys;
}

void UISoftKeyboardWidget::setHideOSMenuKeys(bool fHide)
{
    if (m_fHideOSMenuKeys == fHide)
        return;
    m_fHideOSMenuKeys = fHide;
    update();
    emit sigOptionsChanged();
}

bool UISoftKeyboardWidget::hideNumPad() const
{
    return m_fHideNumPad;
}

void UISoftKeyboardWidget::setHideNumPad(bool fHide)
{
    if (m_fHideNumPad == fHide)
        return;
    m_fHideNumPad = fHide;
    update();
    emit sigOptionsChanged();
}

bool UISoftKeyboardWidget::hideMultimediaKeys() const
{
    return m_fHideMultimediaKeys;
}

void UISoftKeyboardWidget::setHideMultimediaKeys(bool fHide)
{
    if (m_fHideMultimediaKeys == fHide)
        return;
    m_fHideMultimediaKeys = fHide;
    update();
    emit sigOptionsChanged();
}

QColor UISoftKeyboardWidget::color(KeyboardColorType enmColorType) const
{
    if (!m_currentColorTheme)
        return QColor();
    return m_currentColorTheme->color(enmColorType);
}

void UISoftKeyboardWidget::setColor(KeyboardColorType enmColorType, const QColor &color)
{
    if (m_currentColorTheme)
        m_currentColorTheme->setColor(enmColorType, color);
    update();
}

QStringList UISoftKeyboardWidget::colorsToStringList(const QString &strColorThemeName)
{
    UISoftKeyboardColorTheme *pTheme = colorTheme(strColorThemeName);
    if (!pTheme)
        return QStringList();
    return pTheme->colorsToStringList();
}

void UISoftKeyboardWidget::colorsFromStringList(const QString &strColorThemeName, const QStringList &colorStringList)
{
    UISoftKeyboardColorTheme *pTheme = colorTheme(strColorThemeName);
    if (!pTheme)
        return;
    pTheme->colorsFromStringList(colorStringList);
}

void UISoftKeyboardWidget::updateLockKeyStates(bool fCapsLockState, bool fNumLockState, bool fScrollLockState)
{
    for (int i = 0; i < m_physicalLayouts.size(); ++i)
        m_physicalLayouts[i].updateLockKeyStates(fCapsLockState, fNumLockState, fScrollLockState);
    update();
}

QStringList UISoftKeyboardWidget::colorThemeNames() const
{
    QStringList nameList;
    foreach (const UISoftKeyboardColorTheme &theme, m_colorThemes)
    {
        nameList << theme.name();
    }
    return nameList;
}

QString UISoftKeyboardWidget::currentColorThemeName() const
{
    if (!m_currentColorTheme)
        return QString();
    return m_currentColorTheme->name();
}

void UISoftKeyboardWidget::setColorThemeByName(const QString &strColorThemeName)
{
    if (strColorThemeName.isEmpty())
        return;
    if (m_currentColorTheme && m_currentColorTheme->name() == strColorThemeName)
        return;
    for (int i = 0; i < m_colorThemes.size(); ++i)
    {
        if (m_colorThemes[i].name() == strColorThemeName)
        {
            m_currentColorTheme = &(m_colorThemes[i]);
            break;
        }
    }
    update();
    emit sigCurrentColorThemeChanged();
}

void UISoftKeyboardWidget::parentDialogDeactivated()
{
    if (!underMouse())
        m_pKeyUnderMouse = 0;
    update();
}

bool UISoftKeyboardWidget::isColorThemeEditable() const
{
    if (!m_currentColorTheme)
        return false;
    return m_currentColorTheme->isEditable();
}

QStringList UISoftKeyboardWidget::unsavedLayoutsNameList() const
{
    QStringList nameList;
    foreach (const UISoftKeyboardLayout &layout, m_layouts)
    {
        if (layout.editedButNotSaved())
            nameList << layout.nameString();
    }
    return nameList;
}

void UISoftKeyboardWidget::deleteCurrentLayout()
{
    if (!m_layouts.contains(m_uCurrentLayoutId))
        return;

    /* Make sure we will have at least one layout remaining. */
    if (m_layouts.size() <= 1)
        return;

    const UISoftKeyboardLayout &layout = m_layouts.value(m_uCurrentLayoutId);
    if (!layout.editable() || layout.isFromResources())
        return;

    QDir fileToDelete;
    QString strFilePath(layout.sourceFilePath());

    bool fFileExists = false;
    if (!strFilePath.isEmpty())
        fFileExists = fileToDelete.exists(strFilePath);
    /* It might be that the layout copied but not yet saved into a file: */
    if (fFileExists)
    {
        if (!msgCenter().questionBinary(this, MessageType_Question,
                                        QString(UISoftKeyboard::tr("This will delete the keyboard layout file as well. Proceed?")),
                                        0 /* auto-confirm id */,
                                        QString("Delete") /* ok button text */,
                                        QString() /* cancel button text */,
                                        false /* ok button by default? */))
            return;

        if (fileToDelete.remove(strFilePath))
            sigStatusBarMessage(UISoftKeyboard::tr("The file %1 has been deleted").arg(strFilePath));
        else
            sigStatusBarMessage(UISoftKeyboard::tr("Deleting the file %1 has failed").arg(strFilePath));
    }

    m_layouts.remove(m_uCurrentLayoutId);
    setCurrentLayout(m_layouts.firstKey());
}

void UISoftKeyboardWidget::toggleEditMode(bool fIsEditMode)
{
    if (fIsEditMode)
        m_enmMode = Mode_LayoutEdit;
    else
    {
        m_enmMode = Mode_Keyboard;
        m_pKeyBeingEdited = 0;
    }
    update();
}

void UISoftKeyboardWidget::addLayout(const UISoftKeyboardLayout &newLayout)
{
    if (m_layouts.contains(newLayout.uid()))
        return;
    m_layouts[newLayout.uid()] = newLayout;
}

void UISoftKeyboardWidget::setNewMinimumSize(const QSize &size)
{
    m_minimumSize = size;
    updateGeometry();
}

void UISoftKeyboardWidget::setInitialSize(int iWidth, int iHeight)
{
    m_iInitialWidth = iWidth;
    m_iInitialHeight = iHeight;
}

UISoftKeyboardKey *UISoftKeyboardWidget::keyUnderMouse(QMouseEvent *pEvent)
{
    const QPoint lPos = pEvent->position().toPoint();
    QPoint eventPosition(lPos.x() / m_fScaleFactorX, lPos.y() / m_fScaleFactorY);
    if (m_fHideMultimediaKeys)
        eventPosition.setY(eventPosition.y() + m_multiMediaKeysLayout.totalHeight());
    return keyUnderMouse(eventPosition);
}

UISoftKeyboardKey *UISoftKeyboardWidget::keyUnderMouse(const QPoint &eventPosition)
{
    const UISoftKeyboardLayout &currentLayout = m_layouts.value(m_uCurrentLayoutId);

    UISoftKeyboardPhysicalLayout *pPhysicalLayout = findPhysicalLayout(currentLayout.physicalLayoutUuid());
    if (!pPhysicalLayout)
        return 0;

    UISoftKeyboardKey *pKey = 0;
    QVector<UISoftKeyboardRow> &rows = pPhysicalLayout->rows();
    for (int i = 0; i < rows.size(); ++i)
    {
        QVector<UISoftKeyboardKey> &keys = rows[i].keys();
        for (int j = 0; j < keys.size(); ++j)
        {
            UISoftKeyboardKey &key = keys[j];
            if (key.polygonInGlobal().containsPoint(eventPosition, Qt::OddEvenFill))
            {
                pKey = &key;
                break;
            }
        }
    }
    if (m_pKeyUnderMouse != pKey)
    {
        m_pKeyUnderMouse = pKey;
        update();
    }
    return pKey;
}

void UISoftKeyboardWidget::handleKeyRelease(UISoftKeyboardKey *pKey)
{
    if (!pKey)
        return;
    if (pKey->type() == KeyType_Ordinary)
        pKey->release();
    /* We only send the scan codes of Ordinary keys: */
    if (pKey->type() == KeyType_Modifier)
        return;

#if 0

    QVector<LONG> sequence;
    if (!pKey->scanCodePrefix().isEmpty())
        sequence <<  pKey->scanCodePrefix();
    sequence << (pKey->scanCode() | 0x80);

    /* Add the pressed modifiers in the reverse order: */
    for (int i = m_pressedModifiers.size() - 1; i >= 0; --i)
    {
        UISoftKeyboardKey *pModifier = m_pressedModifiers[i];
        if (!pModifier->scanCodePrefix().isEmpty())
            sequence << pModifier->scanCodePrefix();
        sequence << (pModifier->scanCode() | 0x80);
        /* Release the pressed modifiers (if there are not locked): */
        pModifier->release();
    }
    putKeyboardSequence(sequence);

#else

    QVector<QPair<LONG, LONG> > sequence;
    sequence << QPair<LONG, LONG>(pKey->usagePageIdPair());
    /* Add the pressed modifiers in the reverse order: */
    for (int i = m_pressedModifiers.size() - 1; i >= 0; --i)
    {
        UISoftKeyboardKey *pModifier = m_pressedModifiers[i];
        sequence << pModifier->usagePageIdPair();
        /* Release the pressed modifiers (if there are not locked): */
        pModifier->release();
    }
    putUsageCodesRelease(sequence);

#endif
}

void UISoftKeyboardWidget::handleKeyPress(UISoftKeyboardKey *pKey)
{
    if (!pKey)
        return;
    pKey->press();

    if (pKey->type() == KeyType_Modifier)
        return;

#if 0
    QVector<LONG> sequence;
     /* Add the pressed modifiers first: */
    for (int i = 0; i < m_pressedModifiers.size(); ++i)
    {
        UISoftKeyboardKey *pModifier = m_pressedModifiers[i];
        if (!pModifier->scanCodePrefix().isEmpty())
            sequence << pModifier->scanCodePrefix();
        sequence << pModifier->scanCode();
    }

    if (!pKey->scanCodePrefix().isEmpty())
        sequence << pKey->scanCodePrefix();
    sequence << pKey->scanCode();
    putKeyboardSequence(sequence);

#else

    QVector<QPair<LONG, LONG> > sequence;

     /* Add the pressed modifiers first: */
    for (int i = 0; i < m_pressedModifiers.size(); ++i)
    {
        UISoftKeyboardKey *pModifier = m_pressedModifiers[i];
        sequence << pModifier->usagePageIdPair();
    }

    sequence << pKey->usagePageIdPair();
    putUsageCodesPress(sequence);

#endif
}

void UISoftKeyboardWidget::modifierKeyPressRelease(UISoftKeyboardKey *pKey, bool fRelease)
{
    if (!pKey || pKey->type() != KeyType_Modifier)
        return;

    pKey->setState(KeyState_NotPressed);

    QVector<QPair<LONG, LONG> > sequence;
    sequence << pKey->usagePageIdPair();
    if (fRelease)
        putUsageCodesRelease(sequence);
    else
        putUsageCodesPress(sequence);
}

void UISoftKeyboardWidget::keyStateChange(UISoftKeyboardKey* pKey)
{
    if (!pKey)
        return;
    if (pKey->type() == KeyType_Modifier)
    {
        if (pKey->state() == KeyState_NotPressed)
            m_pressedModifiers.removeOne(pKey);
        else
            if (!m_pressedModifiers.contains(pKey))
                m_pressedModifiers.append(pKey);
    }
}

void UISoftKeyboardWidget::setCurrentLayout(const QUuid &layoutUid)
{
    if (m_uCurrentLayoutId == layoutUid || !m_layouts.contains(layoutUid))
        return;

    UISoftKeyboardPhysicalLayout *pPhysicalLayout = findPhysicalLayout(m_layouts[layoutUid].physicalLayoutUuid());
    if (!pPhysicalLayout)
        return;

    m_uCurrentLayoutId = layoutUid;
    emit sigCurrentLayoutChange();
    update();
}

UISoftKeyboardLayout *UISoftKeyboardWidget::currentLayout()
{
    if (!m_layouts.contains(m_uCurrentLayoutId))
        return 0;
    return &(m_layouts[m_uCurrentLayoutId]);
}

bool UISoftKeyboardWidget::loadPhysicalLayout(const QString &strLayoutFileName, KeyboardRegion keyboardRegion /* = KeyboardRegion_Main */)
{
    if (strLayoutFileName.isEmpty())
        return false;
    UIPhysicalLayoutReader reader;
    UISoftKeyboardPhysicalLayout *newPhysicalLayout = 0;
    if (keyboardRegion == KeyboardRegion_Main)
    {
        m_physicalLayouts.append(UISoftKeyboardPhysicalLayout());
        newPhysicalLayout = &(m_physicalLayouts.back());
    }
    else if (keyboardRegion == KeyboardRegion_NumPad)
        newPhysicalLayout = &(m_numPadLayout);
    else if (keyboardRegion == KeyboardRegion_MultimediaKeys)
        newPhysicalLayout = &(m_multiMediaKeysLayout);
    else
        return false;

    if (!reader.parseXMLFile(strLayoutFileName, *newPhysicalLayout))
    {
        m_physicalLayouts.removeLast();
        return false;
    }

    for (int i = 0; i < newPhysicalLayout->rows().size(); ++i)
    {
        UISoftKeyboardRow &row = newPhysicalLayout->rows()[i];
        for (int j = 0; j < row.keys().size(); ++j)
            row.keys()[j].setKeyboardRegion(keyboardRegion);
    }

    if (keyboardRegion == KeyboardRegion_NumPad || keyboardRegion == KeyboardRegion_MultimediaKeys)
        return true;

    /* Go thru all the keys row by row and construct their geometries: */
    int iY = m_iTopMargin;
    int iMaxWidth = 0;
    int iMaxWidthNoNumPad = 0;
    const QVector<UISoftKeyboardRow> &numPadRows = m_numPadLayout.rows();
    QVector<UISoftKeyboardRow> &rows = newPhysicalLayout->rows();

    /* Prepend the multimedia rows to the layout */
    const QVector<UISoftKeyboardRow> &multimediaRows = m_multiMediaKeysLayout.rows();
    for (int i = multimediaRows.size() - 1; i >= 0; --i)
        rows.prepend(multimediaRows[i]);

    for (int i = 0; i < rows.size(); ++i)
    {
        UISoftKeyboardRow &row = rows[i];
        /* Insert the numpad rows at the end of keyboard rows starting with appending 0th numpad row to the
           end of (1 + multimediaRows.size())th layout row: */
        if (i > multimediaRows.size())
        {
            int iNumPadRowIndex = i - (1 + multimediaRows.size());
            if (iNumPadRowIndex >= 0 && iNumPadRowIndex < numPadRows.size())
            {
                for (int m = 0; m < numPadRows[iNumPadRowIndex].keys().size(); ++m)
                    row.keys().append(numPadRows[iNumPadRowIndex].keys()[m]);
            }
        }

        int iX = m_iLeftMargin + row.leftMargin();
        int iXNoNumPad = m_iLeftMargin;
        int iRowHeight = row.defaultHeight();
        int iKeyWidth = 0;
        for (int j = 0; j < row.keys().size(); ++j)
        {
            UISoftKeyboardKey &key = (row.keys())[j];
            if (key.position() == iScrollLockPosition ||
                key.position() == iNumLockPosition ||
                key.position() == iCapsLockPosition)
                newPhysicalLayout->setLockKey(key.position(), &key);

            if (key.keyboardRegion() == KeyboardRegion_NumPad)
                key.setKeyGeometry(QRect(iX + m_iBeforeNumPadWidth, iY, key.width(), key.height()));
            else
                key.setKeyGeometry(QRect(iX, iY, key.width(), key.height()));

            key.setCornerRadius(0.1 * newPhysicalLayout->defaultKeyWidth());
            key.setPoints(UIPhysicalLayoutReader::computeKeyVertices(key));
            key.setParentWidget(this);

            iKeyWidth = key.width();
            if (j < row.keys().size() - 1)
                iKeyWidth += m_iXSpacing;
            if (key.spaceWidthAfter() != 0 && j != row.keys().size() - 1)
                iKeyWidth += (m_iXSpacing + key.spaceWidthAfter());

            iX += iKeyWidth;
            if (key.keyboardRegion() != KeyboardRegion_NumPad)
                iXNoNumPad += iKeyWidth;
        }
        if (row.spaceHeightAfter() != 0)
            iY += row.spaceHeightAfter() + m_iYSpacing;
        iMaxWidth = qMax(iMaxWidth, iX);
        iMaxWidthNoNumPad = qMax(iMaxWidthNoNumPad, iXNoNumPad);

        iY += iRowHeight;
        if (i < rows.size() - 1)
            iY += m_iYSpacing;
    }
    int iInitialWidth = iMaxWidth + m_iRightMargin;
    int iInitialWidthNoNumPad = iMaxWidthNoNumPad + m_iRightMargin;
    int iInitialHeight = iY + m_iBottomMargin;
    m_iInitialWidth = qMax(m_iInitialWidth, iInitialWidth);
    m_iInitialWidthNoNumPad = qMax(m_iInitialWidthNoNumPad, iInitialWidthNoNumPad);
    m_iInitialHeight = qMax(m_iInitialHeight, iInitialHeight);
    return true;
}

bool UISoftKeyboardWidget::loadKeyboardLayout(const QString &strLayoutFileName)
{
    if (strLayoutFileName.isEmpty())
        return false;

    UIKeyboardLayoutReader keyboardLayoutReader;

    UISoftKeyboardLayout newLayout;
    if (!keyboardLayoutReader.parseFile(strLayoutFileName, newLayout))
        return false;

    UISoftKeyboardPhysicalLayout *pPhysicalLayout = findPhysicalLayout(newLayout.physicalLayoutUuid());
    /* If no pyhsical layout with the UUID the keyboard layout refers is found then cancel loading the keyboard layout: */
    if (!pPhysicalLayout)
        return false;

    /* Make sure we have unique lay1out UUIDs: */
    int iCount = 0;
    foreach (const UISoftKeyboardLayout &layout, m_layouts)
    {
        if (layout.uid() == newLayout.uid())
            ++iCount;
    }
    if (iCount > 1)
        return false;

    newLayout.setSourceFilePath(strLayoutFileName);
    addLayout(newLayout);
    return true;
}

UISoftKeyboardPhysicalLayout *UISoftKeyboardWidget::findPhysicalLayout(const QUuid &uuid)
{
    for (int i = 0; i < m_physicalLayouts.size(); ++i)
    {
        if (m_physicalLayouts[i].uid() == uuid)
            return &(m_physicalLayouts[i]);
    }
    return 0;
}

void UISoftKeyboardWidget::reset()
{
    m_pressedModifiers.clear();
    m_pKeyUnderMouse = 0;
    m_pKeyBeingEdited = 0;
    m_pKeyPressed = 0;
    m_enmMode = Mode_Keyboard;

    for (int i = 0; i < m_physicalLayouts.size(); ++i)
        m_physicalLayouts[i].reset();
}

void UISoftKeyboardWidget::loadLayouts()
{
    /* Load physical layouts from resources: Numpad and multimedia layout files should be read first
       since we insert these to other layouts: */
    loadPhysicalLayout(":/numpad.xml", KeyboardRegion_NumPad);
    loadPhysicalLayout(":/multimedia_keys.xml", KeyboardRegion_MultimediaKeys);
    QStringList physicalLayoutNames;
    physicalLayoutNames << ":/101_ansi.xml"
                        << ":/102_iso.xml"
                        << ":/106_japanese.xml"
                        << ":/103_iso.xml"
                        << ":/103_ansi.xml";
    foreach (const QString &strName, physicalLayoutNames)
        loadPhysicalLayout(strName);

    setNewMinimumSize(QSize(m_iInitialWidth, m_iInitialHeight));
    setInitialSize(m_iInitialWidth, m_iInitialHeight);

    /* Add keyboard layouts from resources: */
    QStringList keyboardLayoutNames;
    keyboardLayoutNames << ":/us_international.xml"
                        << ":/german.xml"
                        << ":/us.xml"
                        << ":/greek.xml"
                        << ":/japanese.xml"
                        << ":/brazilian.xml"
                        << ":/korean.xml";

    foreach (const QString &strName, keyboardLayoutNames)
        loadKeyboardLayout(strName);
    /* Mark the layouts we load from the resources as non-editable: */
    for (QMap<QUuid, UISoftKeyboardLayout>::iterator iterator = m_layouts.begin(); iterator != m_layouts.end(); ++iterator)
    {
        iterator.value().setEditable(false);
        iterator.value().setIsFromResources(true);
    }
    keyboardLayoutNames.clear();
    /* Add keyboard layouts from the defalt keyboard layout folder: */
    lookAtDefaultLayoutFolder(keyboardLayoutNames);
    foreach (const QString &strName, keyboardLayoutNames)
        loadKeyboardLayout(strName);

    if (m_layouts.isEmpty())
        return;
    for (QMap<QUuid, UISoftKeyboardLayout>::iterator iterator = m_layouts.begin(); iterator != m_layouts.end(); ++iterator)
        iterator.value().setEditedBuNotSaved(false);
    /* Block sigCurrentLayoutChange since it causes saving set layout to exra data: */
    blockSignals(true);
    setCurrentLayout(m_layouts.firstKey());
    blockSignals(false);
}

void UISoftKeyboardWidget::prepareObjects()
{
    setMouseTracking(true);
    connect(m_pMachine, &UIMachine::sigKeyboardLedsChange, this, &UISoftKeyboardWidget::sltKeyboardLedsChange);
}

void UISoftKeyboardWidget::prepareColorThemes()
{
    int iIndex = 0;
    while (predefinedColorThemes[iIndex][0])
    {
        m_colorThemes << UISoftKeyboardColorTheme(predefinedColorThemes[iIndex][0],
                                                  predefinedColorThemes[iIndex][1],
                                                  predefinedColorThemes[iIndex][2],
                                                  predefinedColorThemes[iIndex][3],
                                                  predefinedColorThemes[iIndex][4],
                                                  predefinedColorThemes[iIndex][5]);
        ++iIndex;
    }

    UISoftKeyboardColorTheme customTheme;
    customTheme.setName("Custom");
    customTheme.setIsEditable(true);
    m_colorThemes.append(customTheme);
    m_currentColorTheme = &(m_colorThemes.back());
}

void UISoftKeyboardWidget::setKeyBeingEdited(UISoftKeyboardKey* pKey)
{
    if (m_pKeyBeingEdited == pKey)
        return;
    m_pKeyBeingEdited = pKey;
    emit sigKeyToEdit(pKey);
}

bool UISoftKeyboardWidget::layoutByNameExists(const QString &strName) const
{
    foreach (const UISoftKeyboardLayout &layout, m_layouts)
    {
        if (layout.name() == strName)
            return true;
    }
    return false;
}

void UISoftKeyboardWidget::lookAtDefaultLayoutFolder(QStringList &fileList)
{
    QString strFolder = QString("%1%2%3").arg(gpGlobalSession->homeFolder()).arg(QDir::separator()).arg(strSubDirectorName);
    QDir dir(strFolder);
    if (!dir.exists())
        return;
    QStringList filters;
    filters << "*.xml";
    dir.setNameFilters(filters);
    QFileInfoList fileInfoList = dir.entryInfoList();
    foreach (const QFileInfo &fileInfo, fileInfoList)
        fileList << fileInfo.absoluteFilePath();
}

UISoftKeyboardColorTheme *UISoftKeyboardWidget::colorTheme(const QString &strColorThemeName)
{
    for (int i = 0; i < m_colorThemes.size(); ++i)
    {
        if (m_colorThemes[i].name() == strColorThemeName)
            return &(m_colorThemes[i]);
    }
    return 0;
}

void UISoftKeyboardWidget::showKeyTooltip(UISoftKeyboardKey *pKey)
{
    if (pKey && m_keyTooltips.contains(pKey->position()))
        sigStatusBarMessage(m_keyTooltips[pKey->position()]);
    else
        sigStatusBarMessage(QString());

}

QStringList UISoftKeyboardWidget::layoutNameList() const
{
    QStringList layoutNames;
    foreach (const UISoftKeyboardLayout &layout, m_layouts)
        layoutNames << layout.nameString();
    return layoutNames;
}

QList<QUuid> UISoftKeyboardWidget::layoutUidList() const
{
    QList<QUuid> layoutUids;
    foreach (const UISoftKeyboardLayout &layout, m_layouts)
        layoutUids << layout.uid();
    return layoutUids;
}

const QVector<UISoftKeyboardPhysicalLayout> &UISoftKeyboardWidget::physicalLayouts() const
{
    return m_physicalLayouts;
}

void UISoftKeyboardWidget::sltKeyboardLedsChange()
{
    if (m_pMachine)
    {
        bool fNumLockLed = m_pMachine->isNumLock();
        bool fCapsLockLed = m_pMachine->isCapsLock();
        bool fScrollLockLed = m_pMachine->isScrollLock();
        updateLockKeyStates(fCapsLockLed, fNumLockLed, fScrollLockLed);
    }
}

void UISoftKeyboardWidget::putKeyboardSequence(QVector<LONG> sequence)
{
    if (m_pMachine)
        m_pMachine->putScancodes(sequence);
}

void UISoftKeyboardWidget::putUsageCodesPress(QVector<QPair<LONG, LONG> > sequence)
{
    if (m_pMachine)
    {
        for (int i = 0; i < sequence.size(); ++i)
            m_pMachine->putUsageCode(sequence[i].first, sequence[i].second, false);
    }
}

void UISoftKeyboardWidget::putUsageCodesRelease(QVector<QPair<LONG, LONG> > sequence)
{
    if (m_pMachine)
    {
        for (int i = 0; i < sequence.size(); ++i)
            m_pMachine->putUsageCode(sequence[i].first, sequence[i].second, true);
    }
}


/*********************************************************************************************************************************
*   UIPhysicalLayoutReader implementation.                                                                                  *
*********************************************************************************************************************************/

bool UIPhysicalLayoutReader::parseXMLFile(const QString &strFileName, UISoftKeyboardPhysicalLayout &physicalLayout)
{
    QFile xmlFile(strFileName);
    if (!xmlFile.exists())
        return false;

    if (xmlFile.size() >= iFileSizeLimit)
        return false;

    if (!xmlFile.open(QIODevice::ReadOnly))
        return false;

    m_xmlReader.setDevice(&xmlFile);

    if (!m_xmlReader.readNextStartElement() || m_xmlReader.name() != QLatin1String("physicallayout"))
        return false;
    physicalLayout.setFileName(strFileName);

    QXmlStreamAttributes attributes = m_xmlReader.attributes();
    int iDefaultWidth = attributes.value("defaultWidth").toInt();
    int iDefaultHeight = attributes.value("defaultHeight").toInt();
    QVector<UISoftKeyboardRow> &rows = physicalLayout.rows();
    physicalLayout.setDefaultKeyWidth(iDefaultWidth);

    while (m_xmlReader.readNextStartElement())
    {
        if (m_xmlReader.name() == QLatin1String("row"))
            parseRow(iDefaultWidth, iDefaultHeight, rows);
        else if (m_xmlReader.name() == QLatin1String("name"))
            physicalLayout.setName(m_xmlReader.readElementText());
        else if (m_xmlReader.name() == QLatin1String("id"))
            physicalLayout.setUid(QUuid(m_xmlReader.readElementText()));
        else
            m_xmlReader.skipCurrentElement();
    }

    return true;
}

void UIPhysicalLayoutReader::parseRow(int iDefaultWidth, int iDefaultHeight, QVector<UISoftKeyboardRow> &rows)
{
    rows.append(UISoftKeyboardRow());
    UISoftKeyboardRow &row = rows.back();

    row.setDefaultWidth(iDefaultWidth);
    row.setDefaultHeight(iDefaultHeight);
    row.setSpaceHeightAfter(0);

    /* Override the layout attributes if the row also has them: */
    QXmlStreamAttributes attributes = m_xmlReader.attributes();
    if (attributes.hasAttribute("defaultWidth"))
        row.setDefaultWidth(attributes.value("defaultWidth").toInt());
    if (attributes.hasAttribute("defaultHeight"))
        row.setDefaultHeight(attributes.value("defaultHeight").toInt());
    while (m_xmlReader.readNextStartElement())
    {
        if (m_xmlReader.name() == QLatin1String("key"))
            parseKey(row);
        else if (m_xmlReader.name() == QLatin1String("space"))
            parseKeySpace(row);
        else
            m_xmlReader.skipCurrentElement();
    }
}

void UIPhysicalLayoutReader::parseKey(UISoftKeyboardRow &row)
{
    row.keys().append(UISoftKeyboardKey());
    UISoftKeyboardKey &key = row.keys().back();
    key.setWidth(row.defaultWidth());
    key.setHeight(row.defaultHeight());
    QString strKeyCap;
    while (m_xmlReader.readNextStartElement())
    {
        if (m_xmlReader.name() == QLatin1String("width"))
            key.setWidth(m_xmlReader.readElementText().toInt());
        else if (m_xmlReader.name() == QLatin1String("height"))
            key.setHeight(m_xmlReader.readElementText().toInt());
        else if (m_xmlReader.name() == QLatin1String("scancode"))
        {
            QString strCode = m_xmlReader.readElementText();
            bool fOk = false;
            key.setScanCode(strCode.toInt(&fOk, 16));
        }
        else if (m_xmlReader.name() == QLatin1String("scancodeprefix"))
        {
            const QString strCode = m_xmlReader.readElementText();
            foreach (const QString &strPrefix, strCode.split('-', Qt::SkipEmptyParts))
            {
                bool fOk = false;
                LONG iCode = strPrefix.toInt(&fOk, 16);
                if (fOk)
                    key.addScanCodePrefix(iCode);
            }
        }
        else if (m_xmlReader.name() == QLatin1String("usageid"))
        {
            QString strCode = m_xmlReader.readElementText();
            bool fOk = false;
            key.setUsageId(strCode.toInt(&fOk, 16));
        }
        else if (m_xmlReader.name() == QLatin1String("usagepage"))
        {
            QString strCode = m_xmlReader.readElementText();
            bool fOk = false;
            key.setUsagePage(strCode.toInt(&fOk, 16));
        }
        else if (m_xmlReader.name() == QLatin1String("cutout"))
            parseCutout(key);
        else if (m_xmlReader.name() == QLatin1String("position"))
            key.setPosition(m_xmlReader.readElementText().toInt());
        else if (m_xmlReader.name() == QLatin1String("type"))
        {
            QString strType = m_xmlReader.readElementText();
            if (strType == "modifier")
                key.setType(KeyType_Modifier);
            else if (strType == "lock")
                key.setType(KeyType_Lock);
        }
        else if (m_xmlReader.name() == QLatin1String("osmenukey"))
        {
            if (m_xmlReader.readElementText() == "true")
                key.setIsOSMenuKey(true);
        }
        else if (m_xmlReader.name() == QLatin1String("staticcaption"))
            key.setStaticCaption(m_xmlReader.readElementText());
        else if (m_xmlReader.name() == QLatin1String("image"))
            key.setImageByName(m_xmlReader.readElementText());
        else
            m_xmlReader.skipCurrentElement();
    }
}

void UIPhysicalLayoutReader::parseKeySpace(UISoftKeyboardRow &row)
{
    int iWidth = row.defaultWidth();
    int iHeight = 0;
    while (m_xmlReader.readNextStartElement())
    {
        if (m_xmlReader.name() == QLatin1String("width"))
            iWidth = m_xmlReader.readElementText().toInt();
        else if (m_xmlReader.name() == QLatin1String("height"))
            iHeight = m_xmlReader.readElementText().toInt();
        else
            m_xmlReader.skipCurrentElement();
    }
    row.setSpaceHeightAfter(iHeight);
    /* If we have keys add the parsed space to the last key as the 'space after': */
    if (!row.keys().empty())
        row.keys().back().setSpaceWidthAfter(iWidth);
    /* If we have no keys than this is the initial space left to first key: */
    else
        row.setLeftMargin(iWidth);
}

void UIPhysicalLayoutReader::parseCutout(UISoftKeyboardKey &key)
{
    int iWidth = 0;
    int iHeight = 0;
    int iCorner = 0;
    while (m_xmlReader.readNextStartElement())
    {
        if (m_xmlReader.name() == QLatin1String("width"))
            iWidth = m_xmlReader.readElementText().toInt();
        else if (m_xmlReader.name() == QLatin1String("height"))
            iHeight = m_xmlReader.readElementText().toInt();
        else if (m_xmlReader.name() == QLatin1String("corner"))
        {
            QString strCorner = m_xmlReader.readElementText();
            if (strCorner == "topLeft")
                    iCorner = 0;
            else if(strCorner == "topRight")
                    iCorner = 1;
            else if(strCorner == "bottomRight")
                    iCorner = 2;
            else if(strCorner == "bottomLeft")
                    iCorner = 3;
        }
        else
            m_xmlReader.skipCurrentElement();
    }
    key.setCutout(iCorner, iWidth, iHeight);
}

QVector<QPointF> UIPhysicalLayoutReader::computeKeyVertices(const UISoftKeyboardKey &key)
{
    QVector<QPointF> vertices;

    if (key.cutoutCorner() == -1 || key.width() <= key.cutoutWidth() || key.height() <= key.cutoutHeight())
    {
        vertices.append(QPoint(0, 0));
        vertices.append(QPoint(key.width(), 0));
        vertices.append(QPoint(key.width(), key.height()));
        vertices.append(QPoint(0, key.height()));
        return vertices;
    }
    if (key.cutoutCorner() == 0)
    {
        vertices.append(QPoint(key.cutoutWidth(), 0));
        vertices.append(QPoint(key.width(), 0));
        vertices.append(QPoint(key.width(), key.height()));
        vertices.append(QPoint(0, key.height()));
        vertices.append(QPoint(0, key.cutoutHeight()));
        vertices.append(QPoint(key.cutoutWidth(), key.cutoutHeight()));
    }
    else if (key.cutoutCorner() == 1)
    {
        vertices.append(QPoint(0, 0));
        vertices.append(QPoint(key.width() - key.cutoutWidth(), 0));
        vertices.append(QPoint(key.width() - key.cutoutWidth(), key.cutoutHeight()));
        vertices.append(QPoint(key.width(), key.cutoutHeight()));
        vertices.append(QPoint(key.width(), key.height()));
        vertices.append(QPoint(0, key.height()));
    }
    else if (key.cutoutCorner() == 2)
    {
        vertices.append(QPoint(0, 0));
        vertices.append(QPoint(key.width(), 0));
        vertices.append(QPoint(key.width(), key.cutoutHeight()));
        vertices.append(QPoint(key.width() - key.cutoutWidth(), key.cutoutHeight()));
        vertices.append(QPoint(key.width() - key.cutoutWidth(), key.height()));
        vertices.append(QPoint(0, key.height()));
    }
    else if (key.cutoutCorner() == 3)
    {
        vertices.append(QPoint(0, 0));
        vertices.append(QPoint(key.width(), 0));
        vertices.append(QPoint(key.width(), key.height()));
        vertices.append(QPoint(key.cutoutWidth(), key.height()));
        vertices.append(QPoint(key.cutoutWidth(), key.height() - key.cutoutHeight()));
        vertices.append(QPoint(0, key.height() - key.cutoutHeight()));
    }
    return vertices;
}


/*********************************************************************************************************************************
*   UIKeyboardLayoutReader implementation.                                                                                       *
*********************************************************************************************************************************/

bool UIKeyboardLayoutReader::parseFile(const QString &strFileName, UISoftKeyboardLayout &layout)
{
    QFile xmlFile(strFileName);
    if (!xmlFile.exists())
        return false;

    if (xmlFile.size() >= iFileSizeLimit)
        return false;

    if (!xmlFile.open(QIODevice::ReadOnly))
        return false;

    m_xmlReader.setDevice(&xmlFile);

    if (!m_xmlReader.readNextStartElement() || m_xmlReader.name() != QLatin1String("layout"))
        return false;

    while (m_xmlReader.readNextStartElement())
    {
        if (m_xmlReader.name() == QLatin1String("key"))
            parseKey(layout);
        else if (m_xmlReader.name() == QLatin1String("name"))
            layout.setName(m_xmlReader.readElementText());
        else if (m_xmlReader.name() == QLatin1String("nativename"))
            layout.setNativeName(m_xmlReader.readElementText());
        else if (m_xmlReader.name() == QLatin1String("physicallayoutid"))
            layout.setPhysicalLayoutUuid(QUuid(m_xmlReader.readElementText()));
        else if (m_xmlReader.name() == QLatin1String("id"))
            layout.setUid(QUuid(m_xmlReader.readElementText()));
        else
            m_xmlReader.skipCurrentElement();
    }
    return true;
}

void  UIKeyboardLayoutReader::parseKey(UISoftKeyboardLayout &layout)
{
    UIKeyCaptions keyCaptions;
    int iKeyPosition = 0;
    while (m_xmlReader.readNextStartElement())
    {
        if (m_xmlReader.name() == QLatin1String("basecaption"))
        {
            keyCaptions.m_strBase = m_xmlReader.readElementText();
            keyCaptions.m_strBase.replace("\\n", "\n");
        }
        else if (m_xmlReader.name() == QLatin1String("shiftcaption"))
        {
            keyCaptions.m_strShift = m_xmlReader.readElementText();
            keyCaptions.m_strShift.replace("\\n", "\n");
        }
        else if (m_xmlReader.name() == QLatin1String("altgrcaption"))
        {
            keyCaptions.m_strAltGr = m_xmlReader.readElementText();
            keyCaptions.m_strAltGr.replace("\\n", "\n");
        }
        else if (m_xmlReader.name() == QLatin1String("shiftaltgrcaption"))
        {
            keyCaptions.m_strShiftAltGr = m_xmlReader.readElementText();
            keyCaptions.m_strShiftAltGr.replace("\\n", "\n");
        }
        else if (m_xmlReader.name() == QLatin1String("position"))
            iKeyPosition = m_xmlReader.readElementText().toInt();
        else
            m_xmlReader.skipCurrentElement();
    }
    layout.addOrUpdateUIKeyCaptions(iKeyPosition, keyCaptions);
}


/*********************************************************************************************************************************
*   UISoftKeyboardStatusBarWidget implementation.                                                                                *
*********************************************************************************************************************************/

UISoftKeyboardStatusBarWidget::UISoftKeyboardStatusBarWidget(QWidget *pParent /* = 0*/ )
    : QWidget(pParent)
    , m_pLayoutListButton(0)
    , m_pSettingsButton(0)
    , m_pResetButton(0)
    , m_pHelpButton(0)
    , m_pMessageLabel(0)
{
    prepareObjects();
}

void UISoftKeyboardStatusBarWidget::sltRetranslateUI()
{
    if (m_pLayoutListButton)
        m_pLayoutListButton->setToolTip(UISoftKeyboard::tr("Layout List"));
    if (m_pSettingsButton)
        m_pSettingsButton->setToolTip(UISoftKeyboard::tr("Settings"));
    if (m_pResetButton)
        m_pResetButton->setToolTip(UISoftKeyboard::tr("Reset the keyboard and release all keys"));
    if (m_pHelpButton)
        m_pHelpButton->setToolTip(UISoftKeyboard::tr("Help"));
}

void UISoftKeyboardStatusBarWidget::prepareObjects()
{
    QHBoxLayout *pLayout = new QHBoxLayout;
    if (!pLayout)
        return;
    pLayout->setContentsMargins(0, 0, 0, 0);
    setLayout(pLayout);

    m_pMessageLabel = new QLabel;
    pLayout->addWidget(m_pMessageLabel);

    m_pLayoutListButton = new QToolButton;
    if (m_pLayoutListButton)
    {
        m_pLayoutListButton->setIcon(UIIconPool::iconSet(":/soft_keyboard_layout_list_16px.png", ":/soft_keyboard_layout_list_disabled_16px.png"));
        m_pLayoutListButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        const int iIconMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
        m_pLayoutListButton->resize(QSize(iIconMetric, iIconMetric));
        m_pLayoutListButton->setStyleSheet("QToolButton { border: 0px none black; margin: 0px 0px 0px 0px; } QToolButton::menu-indicator {image: none;}");
        connect(m_pLayoutListButton, &QToolButton::clicked, this, &UISoftKeyboardStatusBarWidget::sigShowHideSidePanel);
        pLayout->addWidget(m_pLayoutListButton);
    }

    m_pSettingsButton = new QToolButton;
    if (m_pSettingsButton)
    {
        m_pSettingsButton->setIcon(UIIconPool::iconSet(":/soft_keyboard_settings_16px.png", ":/soft_keyboard_settings_disabled_16px.png"));
        m_pSettingsButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        const int iIconMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
        m_pSettingsButton->resize(QSize(iIconMetric, iIconMetric));
        m_pSettingsButton->setStyleSheet("QToolButton { border: 0px none black; margin: 0px 0px 0px 0px; } QToolButton::menu-indicator {image: none;}");
        connect(m_pSettingsButton, &QToolButton::clicked, this, &UISoftKeyboardStatusBarWidget::sigShowSettingWidget);
        pLayout->addWidget(m_pSettingsButton);
    }

    m_pResetButton = new QToolButton;
    if (m_pResetButton)
    {
        m_pResetButton->setIcon(UIIconPool::iconSet(":/soft_keyboard_reset_16px.png"));
        m_pResetButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        const int iIconMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
        m_pResetButton->resize(QSize(iIconMetric, iIconMetric));
        m_pResetButton->setStyleSheet("QToolButton { border: 0px none black; margin: 0px 0px 0px 0px; } QToolButton::menu-indicator {image: none;}");
        connect(m_pResetButton, &QToolButton::clicked, this, &UISoftKeyboardStatusBarWidget::sigResetKeyboard);
        pLayout->addWidget(m_pResetButton);
    }

    m_pHelpButton = new QToolButton;
    if (m_pHelpButton)
    {
        m_pHelpButton->setIcon(UIIconPool::iconSet(":/soft_keyboard_help_16px.png"));
        m_pHelpButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        const int iIconMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
        m_pHelpButton->resize(QSize(iIconMetric, iIconMetric));
        m_pHelpButton->setStyleSheet("QToolButton { border: 0px none black; margin: 0px 0px 0px 0px; } QToolButton::menu-indicator {image: none;}");
        connect(m_pHelpButton, &QToolButton::clicked, this, &UISoftKeyboardStatusBarWidget::sigHelpButtonPressed);
        pLayout->addWidget(m_pHelpButton);
    }

    sltRetranslateUI();

    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UISoftKeyboardStatusBarWidget::sltRetranslateUI);
}

void UISoftKeyboardStatusBarWidget::updateLayoutNameInStatusBar(const QString &strMessage)
{
    if (!m_pMessageLabel)
        return;
    m_pMessageLabel->setText(strMessage);
}


/*********************************************************************************************************************************
*   UISoftKeyboardSettingsWidget implementation.                                                                                 *
*********************************************************************************************************************************/

UISoftKeyboardSettingsWidget::UISoftKeyboardSettingsWidget(QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_pHideNumPadCheckBox(0)
    , m_pShowOsMenuButtonsCheckBox(0)
    , m_pHideMultimediaKeysCheckBox(0)
    , m_pColorThemeGroupBox(0)
    , m_pColorThemeComboBox(0)
    , m_pTitleLabel(0)
    , m_pCloseButton(0)

{
    prepareObjects();
}

void UISoftKeyboardSettingsWidget::setHideOSMenuKeys(bool fHide)
{
    if (m_pShowOsMenuButtonsCheckBox)
        m_pShowOsMenuButtonsCheckBox->setChecked(fHide);
}

void UISoftKeyboardSettingsWidget::setHideNumPad(bool fHide)
{
    if (m_pHideNumPadCheckBox)
        m_pHideNumPadCheckBox->setChecked(fHide);
}

void UISoftKeyboardSettingsWidget::setHideMultimediaKeys(bool fHide)
{
    if (m_pHideMultimediaKeysCheckBox)
        m_pHideMultimediaKeysCheckBox->setChecked(fHide);
}

void UISoftKeyboardSettingsWidget::setColorSelectionButtonBackgroundAndTooltip(KeyboardColorType enmColorType,
                                                                               const QColor &color, bool fIsColorEditable)
{
    if (m_colorSelectLabelsButtons.size() > enmColorType && m_colorSelectLabelsButtons[enmColorType].second)
    {
        UISoftKeyboardColorButton *pButton = m_colorSelectLabelsButtons[enmColorType].second;
        QPalette pal = pButton->palette();
        pal.setColor(QPalette::Button, color);
        pButton->setAutoFillBackground(true);
        pButton->setPalette(pal);
        pButton->setToolTip(fIsColorEditable ? UISoftKeyboard::tr("Click to change the color.") : UISoftKeyboard::tr("This color theme is not editable."));
        pButton->update();
    }
}

void UISoftKeyboardSettingsWidget::setColorThemeNames(const QStringList &colorThemeNames)
{
    if (!m_pColorThemeComboBox)
        return;
    m_pColorThemeComboBox->blockSignals(true);
    m_pColorThemeComboBox->clear();
    foreach (const QString &strName, colorThemeNames)
        m_pColorThemeComboBox->addItem(strName);
    m_pColorThemeComboBox->blockSignals(false);
}

void UISoftKeyboardSettingsWidget::setCurrentColorThemeName(const QString &strColorThemeName)
{
    if (!m_pColorThemeComboBox)
        return;
    int iItemIndex = m_pColorThemeComboBox->findText(strColorThemeName, Qt::MatchFixedString);
    if (iItemIndex == -1)
        return;
    m_pColorThemeComboBox->blockSignals(true);
    m_pColorThemeComboBox->setCurrentIndex(iItemIndex);
    m_pColorThemeComboBox->blockSignals(false);
}

void UISoftKeyboardSettingsWidget::sltRetranslateUI()
{
    if (m_pTitleLabel)
        m_pTitleLabel->setText(UISoftKeyboard::tr("Keyboard Settings"));
    if (m_pCloseButton)
    {
        m_pCloseButton->setToolTip(UISoftKeyboard::tr("Close the layout list"));
        m_pCloseButton->setText(UISoftKeyboard::tr("Close"));
    }
    if (m_pHideNumPadCheckBox)
        m_pHideNumPadCheckBox->setText(UISoftKeyboard::tr("Hide NumPad"));
    if (m_pShowOsMenuButtonsCheckBox)
        m_pShowOsMenuButtonsCheckBox->setText(UISoftKeyboard::tr("Hide OS/Menu Keys"));
    if (m_pHideMultimediaKeysCheckBox)
        m_pHideMultimediaKeysCheckBox->setText(UISoftKeyboard::tr("Hide Multimedia Keys"));
    if (m_pColorThemeGroupBox)
        m_pColorThemeGroupBox->setTitle(UISoftKeyboard::tr("Color Themes"));

    if (m_colorSelectLabelsButtons.size() == KeyboardColorType_Max)
    {
        if (m_colorSelectLabelsButtons[KeyboardColorType_Background].first)
            m_colorSelectLabelsButtons[KeyboardColorType_Background].first->setText(UISoftKeyboard::tr("Button Background Color"));
        if (m_colorSelectLabelsButtons[KeyboardColorType_Font].first)
            m_colorSelectLabelsButtons[KeyboardColorType_Font].first->setText(UISoftKeyboard::tr("Button Font Color"));
        if (m_colorSelectLabelsButtons[KeyboardColorType_Hover].first)
            m_colorSelectLabelsButtons[KeyboardColorType_Hover].first->setText(UISoftKeyboard::tr("Button Hover Color"));
        if (m_colorSelectLabelsButtons[KeyboardColorType_Edit].first)
            m_colorSelectLabelsButtons[KeyboardColorType_Edit].first->setText(UISoftKeyboard::tr("Button Edit Color"));
        if (m_colorSelectLabelsButtons[KeyboardColorType_Pressed].first)
            m_colorSelectLabelsButtons[KeyboardColorType_Pressed].first->setText(UISoftKeyboard::tr("Pressed Button Font Color"));
    }
}

void UISoftKeyboardSettingsWidget::prepareObjects()
{
    QGridLayout *pSettingsLayout = new QGridLayout;
    if (!pSettingsLayout)
        return;

    QHBoxLayout *pTitleLayout = new QHBoxLayout;
    m_pCloseButton = new QToolButton;
    m_pCloseButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_pCloseButton->setIcon(UIIconPool::defaultIcon(UIIconPool::UIDefaultIconType_DialogCancel));
    m_pCloseButton->setAutoRaise(true);
    connect(m_pCloseButton, &QToolButton::clicked, this, &UISoftKeyboardSettingsWidget::sigCloseSettingsWidget);
    m_pTitleLabel = new QLabel;
    pTitleLayout->addWidget(m_pTitleLabel);
    pTitleLayout->addStretch(2);
    pTitleLayout->addWidget(m_pCloseButton);
    pSettingsLayout->addLayout(pTitleLayout, 0, 0, 1, 2);

    m_pHideNumPadCheckBox = new QCheckBox;
    m_pShowOsMenuButtonsCheckBox = new QCheckBox;
    m_pHideMultimediaKeysCheckBox = new QCheckBox;
    pSettingsLayout->addWidget(m_pHideNumPadCheckBox, 1, 0, 1, 1);
    pSettingsLayout->addWidget(m_pShowOsMenuButtonsCheckBox, 2, 0, 1, 1);
    pSettingsLayout->addWidget(m_pHideMultimediaKeysCheckBox, 3, 0, 1, 1);
    connect(m_pHideNumPadCheckBox, &QCheckBox::toggled, this, &UISoftKeyboardSettingsWidget::sigHideNumPad);
    connect(m_pShowOsMenuButtonsCheckBox, &QCheckBox::toggled, this, &UISoftKeyboardSettingsWidget::sigHideOSMenuKeys);
    connect(m_pHideMultimediaKeysCheckBox, &QCheckBox::toggled, this, &UISoftKeyboardSettingsWidget::sigHideMultimediaKeys);

    /* A groupbox to host the color selection widgets: */
    m_pColorThemeGroupBox = new QGroupBox;
    QVBoxLayout *pGroupBoxLayout = new QVBoxLayout(m_pColorThemeGroupBox);
    pSettingsLayout->addWidget(m_pColorThemeGroupBox, 4, 0, 1, 1);

    m_pColorThemeComboBox = new QComboBox;
    pGroupBoxLayout->addWidget(m_pColorThemeComboBox);
    connect(m_pColorThemeComboBox, &QComboBox::currentTextChanged, this, &UISoftKeyboardSettingsWidget::sigColorThemeSelectionChanged);

    /* Creating and configuring the color selection buttons: */
    QGridLayout *pColorSelectionLayout = new QGridLayout;
    pColorSelectionLayout->setSpacing(1);
    pGroupBoxLayout->addLayout(pColorSelectionLayout);
    for (int i = KeyboardColorType_Background; i < KeyboardColorType_Max; ++i)
    {
        QLabel *pLabel = new QLabel;
        UISoftKeyboardColorButton *pButton = new UISoftKeyboardColorButton((KeyboardColorType)i);
        pButton->setFlat(true);
        pButton->setMaximumWidth(3 * qApp->style()->pixelMetric(QStyle::PM_LargeIconSize));
        pColorSelectionLayout->addWidget(pLabel, i, 0, 1, 1);
        pColorSelectionLayout->addWidget(pButton, i, 1, 1, 1);
        m_colorSelectLabelsButtons.append(ColorSelectLabelButton(pLabel, pButton));
        connect(pButton, &UISoftKeyboardColorButton::clicked, this, &UISoftKeyboardSettingsWidget::sltColorSelectionButtonClicked);
    }

    QSpacerItem *pSpacer = new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Expanding);
    if (pSpacer)
        pSettingsLayout->addItem(pSpacer, 6, 0);

    setLayout(pSettingsLayout);
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UISoftKeyboardSettingsWidget::sltRetranslateUI);
}

void UISoftKeyboardSettingsWidget::sltColorSelectionButtonClicked()
{
    UISoftKeyboardColorButton *pButton = qobject_cast<UISoftKeyboardColorButton*>(sender());
    if (!pButton)
        return;
    emit sigColorCellClicked((int)pButton->colorType());
}

/*********************************************************************************************************************************
*   UISoftKeyboard implementation.                                                                                  *
*********************************************************************************************************************************/

UISoftKeyboard::UISoftKeyboard(QWidget *pParent, UIMachine *pMachine,
                               QWidget *pCenterWidget, QString strMachineName /* = QString() */)
    : QMainWindowWithRestorableGeometry(pParent)
    , m_pMachine(pMachine)
    , m_pCenterWidget(pCenterWidget)
    , m_pMainLayout(0)
    , m_strMachineName(strMachineName)
    , m_pSplitter(0)
    , m_pSidePanelWidget(0)
    , m_pKeyboardWidget(0)
    , m_pLayoutEditor(0)
    , m_pLayoutSelector(0)
    , m_pSettingsWidget(0)
    , m_pStatusBarWidget(0)
    , m_iGeometrySaveTimerId(-1)
{
    setWindowTitle(QString("%1 - %2").arg(m_strMachineName).arg(tr("Soft Keyboard")));
    prepareObjects();
    prepareConnections();

    if (m_pKeyboardWidget)
    {
        m_pKeyboardWidget->loadLayouts();
        if (m_pLayoutEditor)
            m_pLayoutEditor->setPhysicalLayoutList(m_pKeyboardWidget->physicalLayouts());
    }

    loadSettings();
    configure();
    uiCommon().setHelpKeyword(this, "soft-keyb");
}

bool UISoftKeyboard::shouldBeMaximized() const
{
    return gEDataManager->softKeyboardDialogShouldBeMaximized();
}

void UISoftKeyboard::closeEvent(QCloseEvent *event)
{
    QStringList strNameList = m_pKeyboardWidget->unsavedLayoutsNameList();
    /* Show a warning dialog when there are not saved layouts: */
    if (m_pKeyboardWidget && !strNameList.empty())
    {
        QString strJoinedString = strNameList.join("<br/>");
        if (!msgCenter().questionBinary(this, MessageType_Warning,
                                       tr("<p>Following layouts are edited/copied but not saved:</p>%1"
                                          "<p>Closing this dialog will cause loosing the changes. Proceed?</p>").arg(strJoinedString),
                                       0 /* auto-confirm id */,
                                       "Ok", "Cancel"))
        {
            event->ignore();
            return;
        }
    }
    m_pMachine->releaseKeys();
    emit sigClose();
    event->ignore();
}

bool UISoftKeyboard::event(QEvent *pEvent)
{
    if (pEvent->type() == QEvent::WindowDeactivate)
    {
        if (m_pKeyboardWidget)
            m_pKeyboardWidget->parentDialogDeactivated();
    }
    else if (pEvent->type() == QEvent::KeyPress)
    {
        QKeyEvent *pKeyEvent = dynamic_cast<QKeyEvent*>(pEvent);
        if (pKeyEvent)
        {
            if (QKeySequence(pKeyEvent->key()) == QKeySequence::HelpContents)
                sltHandleHelpRequest();
        }
    }
    else if (pEvent->type() == QEvent::Resize ||
             pEvent->type() == QEvent::Move)
    {
        if (m_iGeometrySaveTimerId != -1)
            killTimer(m_iGeometrySaveTimerId);
        m_iGeometrySaveTimerId = startTimer(300);
    }
    else if (pEvent->type() == QEvent::Timer)
    {
        QTimerEvent *pTimerEvent = static_cast<QTimerEvent*>(pEvent);
        if (pTimerEvent->timerId() == m_iGeometrySaveTimerId)
        {
            killTimer(m_iGeometrySaveTimerId);
            m_iGeometrySaveTimerId = -1;
            saveDialogGeometry();
        }
    }

    return QMainWindowWithRestorableGeometry::event(pEvent);
}

void UISoftKeyboard::sltLayoutSelectionChanged(const QUuid &layoutUid)
{
    if (!m_pKeyboardWidget)
        return;
    m_pKeyboardWidget->setCurrentLayout(layoutUid);
    if (m_pLayoutSelector && m_pKeyboardWidget->currentLayout())
        m_pLayoutSelector->setCurrentLayoutIsEditable(m_pKeyboardWidget->currentLayout()->editable());
}

void UISoftKeyboard::sltCurentLayoutChanged()
{
    if (!m_pKeyboardWidget)
        return;
    UISoftKeyboardLayout *pCurrentLayout = m_pKeyboardWidget->currentLayout();

    /* Update the status bar string: */
    if (!pCurrentLayout)
        return;
    updateStatusBarMessage(pCurrentLayout->nameString());
    saveCurrentLayout();
}

void UISoftKeyboard::sltShowLayoutSelector()
{
    if (m_pSidePanelWidget && m_pLayoutSelector)
        m_pSidePanelWidget->setCurrentWidget(m_pLayoutSelector);
    if (m_pKeyboardWidget)
        m_pKeyboardWidget->toggleEditMode(false);
    if (m_pLayoutEditor)
        m_pLayoutEditor->setKey(0);
}

void UISoftKeyboard::sltShowLayoutEditor()
{
    if (m_pSidePanelWidget && m_pLayoutEditor)
    {
        m_pLayoutEditor->setLayoutToEdit(m_pKeyboardWidget->currentLayout());
        m_pSidePanelWidget->setCurrentWidget(m_pLayoutEditor);
    }
    if (m_pKeyboardWidget)
        m_pKeyboardWidget->toggleEditMode(true);
}

void UISoftKeyboard::sltKeyToEditChanged(UISoftKeyboardKey* pKey)
{
    if (m_pLayoutEditor)
        m_pLayoutEditor->setKey(pKey);
}

void UISoftKeyboard::sltLayoutEdited()
{
    if (!m_pKeyboardWidget)
        return;
    m_pKeyboardWidget->update();
    updateLayoutSelectorList();
    UISoftKeyboardLayout *pCurrentLayout = m_pKeyboardWidget->currentLayout();

    /* Update the status bar string: */
    QString strLayoutName = pCurrentLayout ? pCurrentLayout->name() : QString();
    updateStatusBarMessage(strLayoutName);
}

void UISoftKeyboard::sltKeyCaptionsEdited(UISoftKeyboardKey* pKey)
{
    Q_UNUSED(pKey);
    if (m_pKeyboardWidget)
        m_pKeyboardWidget->update();
}

void UISoftKeyboard::sltShowHideSidePanel()
{
    if (!m_pSidePanelWidget)
        return;
    m_pSidePanelWidget->setVisible(!m_pSidePanelWidget->isVisible());

    if (m_pSidePanelWidget->isVisible() && m_pSettingsWidget->isVisible())
        m_pSettingsWidget->setVisible(false);
}

void UISoftKeyboard::sltShowHideSettingsWidget()
{
    if (!m_pSettingsWidget)
        return;
    m_pSettingsWidget->setVisible(!m_pSettingsWidget->isVisible());
    if (m_pSidePanelWidget->isVisible() && m_pSettingsWidget->isVisible())
        m_pSidePanelWidget->setVisible(false);
}

void UISoftKeyboard::sltHandleColorThemeListSelection(const QString &strColorThemeName)
{
    if (m_pKeyboardWidget)
        m_pKeyboardWidget->setColorThemeByName(strColorThemeName);
    saveSelectedColorThemeName();
}

void UISoftKeyboard::sltHandleKeyboardWidgetColorThemeChange()
{
    for (int i = (int)KeyboardColorType_Background;
         i < (int)KeyboardColorType_Max; ++i)
    {
        KeyboardColorType enmType = (KeyboardColorType)i;
        m_pSettingsWidget->setColorSelectionButtonBackgroundAndTooltip(enmType,
                                                                       m_pKeyboardWidget->color(enmType),
                                                                       m_pKeyboardWidget->isColorThemeEditable());
    }
}

void UISoftKeyboard::sltCopyLayout()
{
    if (!m_pKeyboardWidget)
        return;
    m_pKeyboardWidget->copyCurentLayout();
    updateLayoutSelectorList();
}

void UISoftKeyboard::sltSaveLayout()
{
    if (m_pKeyboardWidget)
        m_pKeyboardWidget->saveCurentLayoutToFile();
}

void UISoftKeyboard::sltDeleteLayout()
{
    if (m_pKeyboardWidget)
        m_pKeyboardWidget->deleteCurrentLayout();
    updateLayoutSelectorList();
    if (m_pKeyboardWidget && m_pKeyboardWidget->currentLayout() && m_pLayoutSelector)
    {
        m_pLayoutSelector->setCurrentLayout(m_pKeyboardWidget->currentLayout()->uid());
        m_pLayoutSelector->setCurrentLayoutIsEditable(m_pKeyboardWidget->currentLayout()->editable());
    }
}

void UISoftKeyboard::sltStatusBarMessage(const QString &strMessage)
{
    statusBar()->showMessage(strMessage, iMessageTimeout);
}

void UISoftKeyboard::sltShowHideOSMenuKeys(bool fHide)
{
    if (m_pKeyboardWidget)
        m_pKeyboardWidget->setHideOSMenuKeys(fHide);
}

void UISoftKeyboard::sltShowHideNumPad(bool fHide)
{
    if (m_pKeyboardWidget)
        m_pKeyboardWidget->setHideNumPad(fHide);
}

void UISoftKeyboard::sltShowHideMultimediaKeys(bool fHide)
{
    if (m_pKeyboardWidget)
        m_pKeyboardWidget->setHideMultimediaKeys(fHide);
}

void UISoftKeyboard::sltHandleColorCellClick(int iColorRow)
{
    if (!m_pKeyboardWidget || iColorRow >= static_cast<int>(KeyboardColorType_Max))
        return;

    if (!m_pKeyboardWidget->isColorThemeEditable())
        return;
    const QColor &currentColor = m_pKeyboardWidget->color(static_cast<KeyboardColorType>(iColorRow));
    QColorDialog colorDialog(currentColor, this);

    if (colorDialog.exec() == QDialog::Rejected)
        return;
    QColor newColor = colorDialog.selectedColor();
    if (currentColor == newColor)
        return;
    m_pKeyboardWidget->setColor(static_cast<KeyboardColorType>(iColorRow), newColor);
    m_pSettingsWidget->setColorSelectionButtonBackgroundAndTooltip(static_cast<KeyboardColorType>(iColorRow),
                                                                   newColor, m_pKeyboardWidget->isColorThemeEditable());
    saveCustomColorTheme();
}

void UISoftKeyboard::sltResetKeyboard()
{
    if (m_pKeyboardWidget)
        m_pKeyboardWidget->reset();
    if (m_pLayoutEditor)
        m_pLayoutEditor->reset();
    m_pMachine->releaseKeys();
    update();
}

void UISoftKeyboard::sltHandleHelpRequest()
{
    UIHelpBrowserDialog::findManualFileAndShow(uiCommon().helpKeyword(this));
}

void UISoftKeyboard::prepareObjects()
{
    m_pSplitter = new QSplitter;
    if (!m_pSplitter)
        return;
    setCentralWidget(m_pSplitter);
    m_pSidePanelWidget = new QStackedWidget;
    if (!m_pSidePanelWidget)
        return;

    m_pSidePanelWidget->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    m_pSidePanelWidget->hide();

    m_pLayoutSelector = new UILayoutSelector;
    if (m_pLayoutSelector)
        m_pSidePanelWidget->addWidget(m_pLayoutSelector);

    m_pLayoutEditor = new UIKeyboardLayoutEditor;
    if (m_pLayoutEditor)
        m_pSidePanelWidget->addWidget(m_pLayoutEditor);

    m_pSettingsWidget = new UISoftKeyboardSettingsWidget;
    if (m_pSettingsWidget)
    {
        m_pSettingsWidget->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
        m_pSettingsWidget->hide();
    }
    m_pKeyboardWidget = new UISoftKeyboardWidget(this, m_pMachine);
    if (!m_pKeyboardWidget)
        return;
    m_pKeyboardWidget->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
    m_pKeyboardWidget->updateGeometry();
    m_pSplitter->addWidget(m_pKeyboardWidget);
    m_pSplitter->addWidget(m_pSidePanelWidget);
    m_pSplitter->addWidget(m_pSettingsWidget);

    m_pSplitter->setCollapsible(0, false);
    m_pSplitter->setCollapsible(1, false);
    m_pSplitter->setCollapsible(2, false);

    statusBar()->setStyleSheet( "QStatusBar::item { border: 0px}" );
    m_pStatusBarWidget = new UISoftKeyboardStatusBarWidget;
    statusBar()->addPermanentWidget(m_pStatusBarWidget);
}

void UISoftKeyboard::prepareConnections()
{
    connect(m_pKeyboardWidget, &UISoftKeyboardWidget::sigCurrentLayoutChange, this, &UISoftKeyboard::sltCurentLayoutChanged);
    connect(m_pKeyboardWidget, &UISoftKeyboardWidget::sigKeyToEdit, this, &UISoftKeyboard::sltKeyToEditChanged);
    connect(m_pKeyboardWidget, &UISoftKeyboardWidget::sigStatusBarMessage, this, &UISoftKeyboard::sltStatusBarMessage);
    connect(m_pKeyboardWidget, &UISoftKeyboardWidget::sigCurrentColorThemeChanged, this, &UISoftKeyboard::sltHandleKeyboardWidgetColorThemeChange);
    connect(m_pKeyboardWidget, &UISoftKeyboardWidget::sigOptionsChanged, this, &UISoftKeyboard::sltSaveSettings);

    connect(m_pLayoutSelector, &UILayoutSelector::sigLayoutSelectionChanged, this, &UISoftKeyboard::sltLayoutSelectionChanged);
    connect(m_pLayoutSelector, &UILayoutSelector::sigShowLayoutEditor, this, &UISoftKeyboard::sltShowLayoutEditor);
    connect(m_pLayoutSelector, &UILayoutSelector::sigCloseLayoutList, this, &UISoftKeyboard::sltShowHideSidePanel);
    connect(m_pLayoutSelector, &UILayoutSelector::sigSaveLayout, this, &UISoftKeyboard::sltSaveLayout);
    connect(m_pLayoutSelector, &UILayoutSelector::sigDeleteLayout, this, &UISoftKeyboard::sltDeleteLayout);
    connect(m_pLayoutSelector, &UILayoutSelector::sigCopyLayout, this, &UISoftKeyboard::sltCopyLayout);
    connect(m_pLayoutEditor, &UIKeyboardLayoutEditor::sigGoBackButton, this, &UISoftKeyboard::sltShowLayoutSelector);
    connect(m_pLayoutEditor, &UIKeyboardLayoutEditor::sigLayoutEdited, this, &UISoftKeyboard::sltLayoutEdited);
    connect(m_pLayoutEditor, &UIKeyboardLayoutEditor::sigUIKeyCaptionsEdited, this, &UISoftKeyboard::sltKeyCaptionsEdited);

    connect(m_pStatusBarWidget, &UISoftKeyboardStatusBarWidget::sigShowHideSidePanel, this, &UISoftKeyboard::sltShowHideSidePanel);
    connect(m_pStatusBarWidget, &UISoftKeyboardStatusBarWidget::sigShowSettingWidget, this, &UISoftKeyboard::sltShowHideSettingsWidget);
    connect(m_pStatusBarWidget, &UISoftKeyboardStatusBarWidget::sigResetKeyboard, this, &UISoftKeyboard::sltResetKeyboard);
    connect(m_pStatusBarWidget, &UISoftKeyboardStatusBarWidget::sigHelpButtonPressed, this, &UISoftKeyboard::sltHandleHelpRequest);

    connect(m_pSettingsWidget, &UISoftKeyboardSettingsWidget::sigHideOSMenuKeys, this, &UISoftKeyboard::sltShowHideOSMenuKeys);
    connect(m_pSettingsWidget, &UISoftKeyboardSettingsWidget::sigHideNumPad, this, &UISoftKeyboard::sltShowHideNumPad);
    connect(m_pSettingsWidget, &UISoftKeyboardSettingsWidget::sigHideMultimediaKeys, this, &UISoftKeyboard::sltShowHideMultimediaKeys);
    connect(m_pSettingsWidget, &UISoftKeyboardSettingsWidget::sigColorCellClicked, this, &UISoftKeyboard::sltHandleColorCellClick);
    connect(m_pSettingsWidget, &UISoftKeyboardSettingsWidget::sigCloseSettingsWidget, this, &UISoftKeyboard::sltShowHideSettingsWidget);
    connect(m_pSettingsWidget, &UISoftKeyboardSettingsWidget::sigColorThemeSelectionChanged, this, &UISoftKeyboard::sltHandleColorThemeListSelection);

    connect(&uiCommon(), &UICommon::sigAskToCommitData, this, &UISoftKeyboard::sltReleaseKeys);
}

void UISoftKeyboard::saveDialogGeometry()
{
    const QRect geo = currentGeometry();
    LogRel2(("GUI: UISoftKeyboard: Saving geometry as: Origin=%dx%d, Size=%dx%d\n",
             geo.x(), geo.y(), geo.width(), geo.height()));
    gEDataManager->setSoftKeyboardDialogGeometry(geo, isCurrentlyMaximized());
}

void UISoftKeyboard::saveCustomColorTheme()
{
    if (!m_pKeyboardWidget)
        return;
    /* Save the changes to the 'Custom' color theme to extra data: */
    QStringList colors = m_pKeyboardWidget->colorsToStringList("Custom");
    colors.prepend("Custom");
    gEDataManager->setSoftKeyboardColorTheme(colors);
}

void UISoftKeyboard::saveSelectedColorThemeName()
{
    if (!m_pKeyboardWidget)
        return;
    gEDataManager->setSoftKeyboardSelectedColorTheme(m_pKeyboardWidget->currentColorThemeName());
}

void UISoftKeyboard::saveCurrentLayout()
{
    if (m_pKeyboardWidget && m_pKeyboardWidget->currentLayout())
        gEDataManager->setSoftKeyboardSelectedLayout(m_pKeyboardWidget->currentLayout()->uid());
}

void UISoftKeyboard::sltSaveSettings()
{
    /* Save other settings: */
    if (m_pKeyboardWidget)
    {
        gEDataManager->setSoftKeyboardOptions(m_pKeyboardWidget->hideNumPad(),
                                              m_pKeyboardWidget->hideOSMenuKeys(),
                                              m_pKeyboardWidget->hideMultimediaKeys());
    }
}

void UISoftKeyboard::sltReleaseKeys()
{
    m_pMachine->releaseKeys();
}

void UISoftKeyboard::loadSettings()
{
    /* Invent default window geometry: */
    float fKeyboardAspectRatio = 1.0f;
    if (m_pKeyboardWidget)
        fKeyboardAspectRatio = m_pKeyboardWidget->layoutAspectRatio();
    const QRect availableGeo = gpDesktop->availableGeometry(this);
    const int iDefaultWidth = availableGeo.width() / 2;
    const int iDefaultHeight = iDefaultWidth * fKeyboardAspectRatio;
    QRect defaultGeo(0, 0, iDefaultWidth, iDefaultHeight);

    /* Load geometry from extradata: */
    const QRect geo = gEDataManager->softKeyboardDialogGeometry(this, m_pCenterWidget, defaultGeo);
    LogRel2(("GUI: UISoftKeyboard: Restoring geometry to: Origin=%dx%d, Size=%dx%d\n",
             geo.x(), geo.y(), geo.width(), geo.height()));
    restoreGeometry(geo);

    /* Load other settings: */
    if (m_pKeyboardWidget)
    {
        QStringList colorTheme = gEDataManager->softKeyboardColorTheme();
        if (!colorTheme.empty())
        {
            /* The fist item is the theme name and the rest are color codes: */
            QString strThemeName = colorTheme[0];
            colorTheme.removeFirst();
            m_pKeyboardWidget->colorsFromStringList(strThemeName, colorTheme);
        }
        m_pKeyboardWidget->setColorThemeByName(gEDataManager->softKeyboardSelectedColorTheme());
        m_pKeyboardWidget->setCurrentLayout(gEDataManager->softKeyboardSelectedLayout());

        /* Load other options from exra data: */
        bool fHideNumPad = false;
        bool fHideOSMenuKeys = false;
        bool fHideMultimediaKeys = false;
        gEDataManager->softKeyboardOptions(fHideNumPad, fHideOSMenuKeys, fHideMultimediaKeys);
        m_pKeyboardWidget->setHideNumPad(fHideNumPad);
        m_pKeyboardWidget->setHideOSMenuKeys(fHideOSMenuKeys);
        m_pKeyboardWidget->setHideMultimediaKeys(fHideMultimediaKeys);
    }
}

void UISoftKeyboard::configure()
{
#ifndef VBOX_WS_MAC
    /* Assign window icon: */
    setWindowIcon(UIIconPool::iconSetFull(":/soft_keyboard_32px.png", ":/soft_keyboard_16px.png"));
#endif

    if (m_pKeyboardWidget && m_pSettingsWidget)
    {
        m_pSettingsWidget->setHideOSMenuKeys(m_pKeyboardWidget->hideOSMenuKeys());
        m_pSettingsWidget->setHideNumPad(m_pKeyboardWidget->hideNumPad());
        m_pSettingsWidget->setHideMultimediaKeys(m_pKeyboardWidget->hideMultimediaKeys());

        m_pSettingsWidget->setColorThemeNames(m_pKeyboardWidget->colorThemeNames());
        m_pSettingsWidget->setCurrentColorThemeName(m_pKeyboardWidget->currentColorThemeName());

        for (int i = (int)KeyboardColorType_Background;
             i < (int)KeyboardColorType_Max; ++i)
        {
            KeyboardColorType enmType = (KeyboardColorType)i;
            m_pSettingsWidget->setColorSelectionButtonBackgroundAndTooltip(enmType,
                                                                           m_pKeyboardWidget->color(enmType),
                                                                           m_pKeyboardWidget->isColorThemeEditable());
        }
    }
    updateLayoutSelectorList();
    if (m_pKeyboardWidget && m_pKeyboardWidget->currentLayout() && m_pLayoutSelector)
    {
        m_pLayoutSelector->setCurrentLayout(m_pKeyboardWidget->currentLayout()->uid());
        m_pLayoutSelector->setCurrentLayoutIsEditable(m_pKeyboardWidget->currentLayout()->editable());
    }
}

void UISoftKeyboard::updateStatusBarMessage(const QString &strName)
{
    if (!m_pStatusBarWidget)
        return;
    QString strMessage;
    if (!strName.isEmpty())
    {
        strMessage += QString("%1: %2").arg(tr("Layout")).arg(strName);
        m_pStatusBarWidget->updateLayoutNameInStatusBar(strMessage);
    }
    else
        m_pStatusBarWidget->updateLayoutNameInStatusBar(QString());
}

void UISoftKeyboard::updateLayoutSelectorList()
{
    if (!m_pKeyboardWidget || !m_pLayoutSelector)
        return;
    m_pLayoutSelector->setLayoutList(m_pKeyboardWidget->layoutNameList(), m_pKeyboardWidget->layoutUidList());
}

#include "UISoftKeyboard.moc"
