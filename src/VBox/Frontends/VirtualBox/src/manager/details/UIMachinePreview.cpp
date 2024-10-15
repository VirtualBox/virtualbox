/* $Id: UIMachinePreview.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachinePreview class implementation.
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
#include <QActionGroup>
#include <QApplication>
#include <QGraphicsSceneContextMenuEvent>
#include <QMenu>
#include <QPainter>
#include <QStyle>
#include <QTimer>

/* GUI includes: */
#include "UIExtraDataManager.h"
#include "UIConverter.h"
#include "UIIconPool.h"
#include "UIMachinePreview.h"
#include "UIImageTools.h"
#include "UITranslationEventListener.h"
#include "UIVirtualBoxEventHandler.h"

/* COM includes: */
#include "CConsole.h"
#include "CDisplay.h"

/* VirtualBox interface declarations: */
#include <VBox/com/VirtualBox.h>


UIMachinePreview::UIMachinePreview(QIGraphicsWidget *pParent)
    : QIGraphicsWidget(pParent)
    , m_pUpdateTimer(new QTimer(this))
    , m_pUpdateTimerMenu(0)
    , m_dRatio((double)QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize) / 16)
    , m_iMargin(0)
    , m_enmPreset(AspectRatioPreset_16x9)
    , m_pPreviewImg(0)
{
    prepare();
}

UIMachinePreview::~UIMachinePreview()
{
    cleanup();
}

void UIMachinePreview::setMachine(const CMachine& comMachine)
{
    /* Pause: */
    stop();

    /* Assign new machine: */
    m_comMachine = comMachine;

    /* Fetch machine data: */
    m_strPreviewName = tr("No preview");
    if (!m_comMachine.isNull())
        m_strPreviewName = m_comMachine.GetAccessible() ? m_comMachine.GetName() :
                           tr("Inaccessible");

    /* Resume: */
    restart();
}

CMachine UIMachinePreview::machine() const
{
    return m_comMachine;
}

void UIMachinePreview::sltRetranslateUI()
{
    /* Translate actions: */
    m_actions.value(PreviewUpdateIntervalType_Disabled)->setText(tr("Update disabled"));
    m_actions.value(PreviewUpdateIntervalType_500ms)->setText(tr("Every 0.5 s"));
    m_actions.value(PreviewUpdateIntervalType_1000ms)->setText(tr("Every 1 s"));
    m_actions.value(PreviewUpdateIntervalType_2000ms)->setText(tr("Every 2 s"));
    m_actions.value(PreviewUpdateIntervalType_5000ms)->setText(tr("Every 5 s"));
    m_actions.value(PreviewUpdateIntervalType_10000ms)->setText(tr("Every 10 s"));
}

void UIMachinePreview::resizeEvent(QGraphicsSceneResizeEvent *pEvent)
{
    recalculatePreviewRectangle();
    sltRecreatePreview();

    /* Call to base-class: */
    QIGraphicsWidget::resizeEvent(pEvent);
}

void UIMachinePreview::showEvent(QShowEvent *pEvent)
{
    restart();

    /* Call to base-class: */
    QIGraphicsWidget::showEvent(pEvent);
}

void UIMachinePreview::hideEvent(QHideEvent *pEvent)
{
    stop();

    /* Call to base-class: */
    QIGraphicsWidget::hideEvent(pEvent);
}

void UIMachinePreview::contextMenuEvent(QGraphicsSceneContextMenuEvent *pEvent)
{
    QAction *pReturn = m_pUpdateTimerMenu->exec(pEvent->screenPos(), 0);
    if (pReturn)
    {
        PreviewUpdateIntervalType enmInterval = static_cast<PreviewUpdateIntervalType>(pReturn->data().toInt());
        setUpdateInterval(enmInterval, true);
        restart();
    }
}

void UIMachinePreview::paint(QPainter *pPainter, const QStyleOptionGraphicsItem*, QWidget*)
{
    /* Where should the content go: */
    QRect cr = contentsRect().toRect();
    if (!cr.isValid())
        return;

    /* If there is a preview image available: */
    if (m_pPreviewImg)
    {
        /* Draw empty monitor frame: */
        pPainter->drawPixmap(cr.x() + m_iMargin, cr.y() + m_iMargin, *m_emptyPixmaps.value(m_enmPreset));

        /* Move image to viewport center: */
        QRect imageRect(QPoint(0, 0), m_pPreviewImg->size());
        imageRect.moveCenter(m_vRect.center());

#ifdef VBOX_WS_MAC
        /* Set composition-mode to opaque: */
        pPainter->setCompositionMode(QPainter::CompositionMode_Source);
        /* Replace translucent background with black one: */
        pPainter->fillRect(imageRect, QColor(Qt::black));
        /* Return default composition-mode back: */
        pPainter->setCompositionMode(QPainter::CompositionMode_SourceAtop);
#endif /* VBOX_WS_MAC */

        /* Draw preview image: */
        pPainter->drawImage(imageRect.topLeft(), *m_pPreviewImg);
    }
    else
    {
        /* Draw full monitor frame: */
        pPainter->drawPixmap(cr.x() + m_iMargin, cr.y() + m_iMargin, *m_fullPixmaps.value(m_enmPreset));

        /* Paint preview name: */
        QFont font = pPainter->font();
        font.setBold(true);
        int fFlags = Qt::AlignCenter | Qt::TextWordWrap;
        float h = m_vRect.size().height() * .2;
        QRect r;
        /* Make a little magic to find out if the given text fits into our rectangle.
         * Decrease the font pixel size as long as it doesn't fit. */
        int cMax = 30;
        do
        {
            h = h * .8;
            font.setPixelSize((int)h);
            pPainter->setFont(font);
            r = pPainter->boundingRect(m_vRect, fFlags, m_strPreviewName);
        }
        while ((r.height() > m_vRect.height() || r.width() > m_vRect.width()) && cMax-- != 0);
        pPainter->setPen(Qt::white);
        pPainter->drawText(m_vRect, fFlags, m_strPreviewName);
    }
}

QSizeF UIMachinePreview::sizeHint(Qt::SizeHint enmWhich, const QSizeF &constraint /* = QSizeF() */) const
{
    if (enmWhich == Qt::MinimumSize)
    {
        AssertReturn(m_emptyPixmaps.contains(m_enmPreset),
                     QIGraphicsWidget::sizeHint(enmWhich, constraint));
        QSize size = m_sizes.value(m_enmPreset);
        if (m_iMargin != 0)
        {
            size.setWidth(size.width() - 2 * m_iMargin);
            size.setHeight(size.height() - 2 * m_iMargin);
        }
        return size;
    }

    /* Call to base-class: */
    return QIGraphicsWidget::sizeHint(enmWhich, constraint);
}

void UIMachinePreview::sltMachineStateChange(const QUuid &uId)
{
    /* Make sure its the event for our machine: */
    if (m_comMachine.isNull() || m_comMachine.GetId() != uId)
        return;

    /* Restart the preview: */
    restart();
}

void UIMachinePreview::sltRecreatePreview()
{
    /* Skip invisible preview: */
    if (!isVisible())
        return;

    /* Cleanup previous image: */
    if (m_pPreviewImg)
    {
        delete m_pPreviewImg;
        m_pPreviewImg = 0;
    }

    /* Fetch actual machine-state: */
    const KMachineState enmMachineState = m_comMachine.isNull() ? KMachineState_Null : m_comMachine.GetState();

    /* We are creating preview only for assigned and accessible VMs: */
    if (!m_comMachine.isNull() && enmMachineState != KMachineState_Null &&
        m_vRect.width() > 0 && m_vRect.height() > 0)
    {
        /* Prepare image: */
        QImage image;

        /* Use 10x9 as the aspect-ratio preset by default: */
        AspectRatioPreset enmPreset = AspectRatioPreset_16x9;

        /* Preview update enabled? */
        if (m_pUpdateTimer->interval() > 0)
        {
            /* Depending on machine state: */
            switch (enmMachineState)
            {
                /* If machine is in SAVED/RESTORING state: */
                case KMachineState_Saved:
                case KMachineState_AbortedSaved:
                case KMachineState_Restoring:
                {
                    /* Use the screenshot from saved-state if possible: */
                    ULONG uGuestWidth = 0, uGuestHeight = 0;
                    QVector<BYTE> screenData = m_comMachine.ReadSavedScreenshotToArray(0, KBitmapFormat_PNG, uGuestWidth, uGuestHeight);

                    /* Make sure screen-data is OK: */
                    if (!m_comMachine.isOk() || screenData.isEmpty())
                        break;

                    if (uGuestWidth > 0 && uGuestHeight > 0)
                    {
                        /* Calculate aspect-ratio: */
                        const double dAspectRatio = (double)uGuestWidth / uGuestHeight;
                        /* Look for the best aspect-ratio preset: */
                        enmPreset = bestAspectRatioPreset(dAspectRatio, m_ratios);
                    }

                    /* Create image based on shallow copy or screenshot data,
                     * scale image down if necessary to the size possible to reflect: */
                    image = QImage::fromData(screenData.data(), screenData.size(), "PNG")
                            .scaled(imageAspectRatioSize(m_vRect.size(), QSize(uGuestWidth, uGuestHeight)),
                                    Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                    /* And detach that copy to make it deep: */
                    image.detach();
                    /* Dim image to give it required look: */
                    dimImage(image);
                    break;
                }
                /* If machine is in RUNNING/PAUSED state: */
                case KMachineState_Running:
                case KMachineState_Paused:
                {
                    /* Make sure session state is Locked: */
                    if (m_comSession.GetState() != KSessionState_Locked)
                        break;

                    /* Make sure console is OK: */
                    CConsole console = m_comSession.GetConsole();
                    if (!m_comSession.isOk() || console.isNull())
                        break;
                    /* Make sure display is OK: */
                    CDisplay display = console.GetDisplay();
                    if (!console.isOk() || display.isNull())
                        break;

                    /* Acquire guest-screen attributes: */
                    LONG iOriginX = 0, iOriginY = 0;
                    ULONG uGuestWidth = 0, uGuestHeight = 0, uBpp = 0;
                    KGuestMonitorStatus monitorStatus = KGuestMonitorStatus_Enabled;
                    display.GetScreenResolution(0, uGuestWidth, uGuestHeight, uBpp, iOriginX, iOriginY, monitorStatus);
                    if (uGuestWidth > 0 && uGuestHeight > 0)
                    {
                        /* Calculate aspect-ratio: */
                        const double dAspectRatio = (double)uGuestWidth / uGuestHeight;
                        /* Look for the best aspect-ratio preset: */
                        enmPreset = bestAspectRatioPreset(dAspectRatio, m_ratios);
                    }

                    /* Calculate size corresponding to aspect-ratio: */
                    const QSize size = imageAspectRatioSize(m_vRect.size(), QSize(uGuestWidth, uGuestHeight));

                    /* Use direct VM content: */
                    QVector<BYTE> screenData = display.TakeScreenShotToArray(0, size.width(), size.height(), KBitmapFormat_BGR0);

                    /* Make sure screen-data is OK: */
                    if (!display.isOk() || screenData.isEmpty())
                        break;

                    /* Make sure screen-data size is valid: */
                    const int iExpectedSize = size.width() * size.height() * 4;
                    const int iActualSize = screenData.size();
                    if (iActualSize != iExpectedSize)
                    {
                        AssertMsgFailed(("Invalid screen-data size '%d', should be '%d'!\n", iActualSize, iExpectedSize));
                        break;
                    }

                    /* Create image based on shallow copy of acquired data: */
                    QImage tempImage(screenData.data(), size.width(), size.height(), QImage::Format_RGB32);
                    image = tempImage;
                    /* And detach that copy to make it deep: */
                    image.detach();
                    /* Dim image to give it required look for PAUSED state: */
                    if (enmMachineState == KMachineState_Paused)
                        dimImage(image);
                    break;
                }
                default:
                    break;
            }
        }

        /* If image initialized: */
        if (!image.isNull())
        {
            /* Shallow copy that image: */
            m_pPreviewImg = new QImage(image);
            /* And detach that copy to make it deep: */
            m_pPreviewImg->detach();
        }

        /* If preset changed: */
        if (m_enmPreset != enmPreset)
        {
            /* Save new preset: */
            m_enmPreset = enmPreset;
            /* And update geometry: */
            updateGeometry();
            emit sigSizeHintChanged();
        }
    }

    /* Redraw preview in any case: */
    update();
}

void UIMachinePreview::prepare()
{
    /* Create session instance: */
    m_comSession.createInstance(CLSID_Session);

    /* Cache aspect-ratio preset settings: */
    const QIcon empty16x10 = UIIconPool::iconSet(":/preview_empty_16to10_242x167px.png");
    const QIcon empty16x9 = UIIconPool::iconSet(":/preview_empty_16to9_242x155px.png");
    const QIcon empty4x3 = UIIconPool::iconSet(":/preview_empty_4to3_242x192px.png");
    const QIcon full16x10 = UIIconPool::iconSet(":/preview_full_16to10_242x167px.png");
    const QIcon full16x9 = UIIconPool::iconSet(":/preview_full_16to9_242x155px.png");
    const QIcon full4x3 = UIIconPool::iconSet(":/preview_full_4to3_242x192px.png");

    // WORKAROUND:
    // Since we don't have x3 and x4 HiDPI icons yet,
    // and we hadn't enabled automatic up-scaling for now,
    // we have to make sure m_dRatio is within possible bounds.
    const QList<QSize> sizes = empty16x10.availableSizes();
    if (sizes.size() >= 2)
        m_dRatio = qMin(m_dRatio, (double)sizes.last().width() / sizes.first().width());

    m_sizes.insert(AspectRatioPreset_16x10, QSize(242 * m_dRatio, 167 * m_dRatio));
    m_sizes.insert(AspectRatioPreset_16x9, QSize(242 * m_dRatio, 155 * m_dRatio));
    m_sizes.insert(AspectRatioPreset_4x3, QSize(242 * m_dRatio, 192 * m_dRatio));
    m_ratios.insert(AspectRatioPreset_16x10, (double)16/10);
    m_ratios.insert(AspectRatioPreset_16x9, (double)16/9);
    m_ratios.insert(AspectRatioPreset_4x3, (double)4/3);
    m_emptyPixmaps.insert(AspectRatioPreset_16x10, new QPixmap(empty16x10.pixmap(m_sizes.value(AspectRatioPreset_16x10))));
    m_emptyPixmaps.insert(AspectRatioPreset_16x9, new QPixmap(empty16x9.pixmap(m_sizes.value(AspectRatioPreset_16x9))));
    m_emptyPixmaps.insert(AspectRatioPreset_4x3, new QPixmap(empty4x3.pixmap(m_sizes.value(AspectRatioPreset_4x3))));
    m_fullPixmaps.insert(AspectRatioPreset_16x10, new QPixmap(full16x10.pixmap(m_sizes.value(AspectRatioPreset_16x10))));
    m_fullPixmaps.insert(AspectRatioPreset_16x9, new QPixmap(full16x9.pixmap(m_sizes.value(AspectRatioPreset_16x9))));
    m_fullPixmaps.insert(AspectRatioPreset_4x3, new QPixmap(full4x3.pixmap(m_sizes.value(AspectRatioPreset_4x3))));

    /* Setup contents (depends on presets above!): */
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    /* Create the context menu: */
    m_pUpdateTimerMenu = new QMenu;
    QActionGroup *pUpdateTimeG = new QActionGroup(this);
    pUpdateTimeG->setExclusive(true);
    for(int i = 0; i < PreviewUpdateIntervalType_Max; ++i)
    {
        QAction *pUpdateTime = new QAction(pUpdateTimeG);
        pUpdateTime->setData(i);
        pUpdateTime->setCheckable(true);
        pUpdateTimeG->addAction(pUpdateTime);
        m_pUpdateTimerMenu->addAction(pUpdateTime);
        m_actions[static_cast<PreviewUpdateIntervalType>(i)] = pUpdateTime;
    }
    m_pUpdateTimerMenu->insertSeparator(m_actions[static_cast<PreviewUpdateIntervalType>(PreviewUpdateIntervalType_500ms)]);

    /* Initialize with the new update interval: */
    setUpdateInterval(gEDataManager->selectorWindowPreviewUpdateInterval(), false);

    /* Setup connections: */
    connect(m_pUpdateTimer, &QTimer::timeout, this, &UIMachinePreview::sltRecreatePreview);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMachineStateChange,
            this, &UIMachinePreview::sltMachineStateChange);

    /* Retranslate the UI */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIMachinePreview::sltRetranslateUI);
}

void UIMachinePreview::cleanup()
{
    /* Close any open session: */
    if (m_comSession.GetState() == KSessionState_Locked)
        m_comSession.UnlockMachine();

    /* Destroy background images: */
    foreach (const AspectRatioPreset &enmPreset, m_emptyPixmaps.keys())
    {
        delete m_emptyPixmaps.value(enmPreset);
        m_emptyPixmaps.remove(enmPreset);
    }
    foreach (const AspectRatioPreset &enmPreset, m_fullPixmaps.keys())
    {
        delete m_fullPixmaps.value(enmPreset);
        m_fullPixmaps.remove(enmPreset);
    }

    /* Destroy preview image: */
    if (m_pPreviewImg)
        delete m_pPreviewImg;

    /* Destroy update timer: */
    if (m_pUpdateTimerMenu)
        delete m_pUpdateTimerMenu;
}

void UIMachinePreview::setUpdateInterval(PreviewUpdateIntervalType interval, bool fSave)
{
    switch (interval)
    {
        case PreviewUpdateIntervalType_Disabled:
        {
            /* Stop the timer: */
            m_pUpdateTimer->stop();
            /* And continue with other cases: */
        }
        RT_FALL_THRU();
        case PreviewUpdateIntervalType_500ms:
        case PreviewUpdateIntervalType_1000ms:
        case PreviewUpdateIntervalType_2000ms:
        case PreviewUpdateIntervalType_5000ms:
        case PreviewUpdateIntervalType_10000ms:
        {
            /* Set the timer interval: */
            m_pUpdateTimer->setInterval(gpConverter->toInternalInteger(interval));
            /* Check corresponding action: */
            m_actions[interval]->setChecked(true);
            break;
        }
        case PreviewUpdateIntervalType_Max:
            break;
    }
    if (fSave)
        gEDataManager->setSelectorWindowPreviewUpdateInterval(interval);
}

void UIMachinePreview::recalculatePreviewRectangle()
{
    /* Contents rectangle: */
    QRect cr = contentsRect().toRect();
    m_vRect = cr.adjusted( 21 * m_dRatio + m_iMargin,  21 * m_dRatio + m_iMargin,
                          -21 * m_dRatio - m_iMargin, -21 * m_dRatio - m_iMargin);
}

void UIMachinePreview::restart()
{
    /* Fetch the latest machine-state: */
    KMachineState enmMachineState = m_comMachine.isNull() ? KMachineState_Null : m_comMachine.GetState();

    /* Reopen session if necessary: */
    if (m_comSession.GetState() == KSessionState_Locked)
        m_comSession.UnlockMachine();
    if (!m_comMachine.isNull())
    {
        /* Lock the session for the current machine: */
        if (enmMachineState == KMachineState_Running || enmMachineState == KMachineState_Paused)
            m_comMachine.LockMachine(m_comSession, KLockType_Shared);
    }

    /* Recreate the preview image: */
    sltRecreatePreview();

    /* Start the timer if necessary: */
    if (!m_comMachine.isNull())
    {
        if (m_pUpdateTimer->interval() > 0 && enmMachineState == KMachineState_Running)
            m_pUpdateTimer->start();
    }
}

void UIMachinePreview::stop()
{
    /* Stop the timer: */
    m_pUpdateTimer->stop();
}

/* static */
UIMachinePreview::AspectRatioPreset UIMachinePreview::bestAspectRatioPreset(const double dAspectRatio,
                                                                            const QMap<AspectRatioPreset, double> &ratios)
{
    /* Use 16x9 preset as the 'best' by 'default': */
    AspectRatioPreset bestPreset = AspectRatioPreset_16x9;
    /* Calculate minimum diff based on 'default' preset: */
    double dMinimumDiff = qAbs(dAspectRatio - ratios.value(bestPreset));
    /* Now look for the 'best' aspect-ratio preset among existing: */
    for (AspectRatioPreset currentPreset = AspectRatioPreset_16x10;
         currentPreset <= AspectRatioPreset_4x3;
         currentPreset = (AspectRatioPreset)(currentPreset + 1))
    {
        /* Calculate current diff based on 'current' preset: */
        const double dDiff = qAbs(dAspectRatio - ratios.value(currentPreset));
        /* If new 'best' preset found: */
        if (dDiff < dMinimumDiff)
        {
            /* Remember new diff: */
            dMinimumDiff = dDiff;
            /* And new preset: */
            bestPreset = currentPreset;
        }
    }
    /* Return 'best' preset: */
    return bestPreset;
}

/* static */
QSize UIMachinePreview::imageAspectRatioSize(const QSize &hostSize, const QSize &guestSize)
{
    /* Make sure host-size and guest-size are valid: */
    AssertReturn(!hostSize.isNull(), QSize());
    if (guestSize.isNull())
        return hostSize;

    /* Calculate host/guest aspect-ratio: */
    const double dHostAspectRatio = (double)hostSize.width() / hostSize.height();
    const double dGuestAspectRatio = (double)guestSize.width() / guestSize.height();
    int iWidth = 0, iHeight = 0;
    /* Guest-screen more thin by vertical than host-screen: */
    if (dGuestAspectRatio >= dHostAspectRatio)
    {
        /* Get host width: */
        iWidth = hostSize.width();
        /* And calculate height based on guest aspect ratio: */
        iHeight = (int)((double)iWidth / dGuestAspectRatio);
        /* But no more than host height: */
        iHeight = qMin(iHeight, hostSize.height());
    }
    /* Host-screen more thin by vertical than guest-screen: */
    else
    {
        /* Get host height: */
        iHeight = hostSize.height();
        /* And calculate width based on guest aspect ratio: */
        iWidth = (int)((double)iHeight * dGuestAspectRatio);
        /* But no more than host width: */
        iWidth = qMin(iWidth, hostSize.width());
    }
    /* Return actual size: */
    return QSize(iWidth, iHeight);
}
