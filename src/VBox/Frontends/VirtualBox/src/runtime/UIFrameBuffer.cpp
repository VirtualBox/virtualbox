/* $Id: UIFrameBuffer.cpp 109004 2025-03-31 09:38:43Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIFrameBuffer class implementation.
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
#include <QImage>
#include <QRegion>
#include <QPainter>
#include <QPaintEvent>
#include <QTransform>

/* GUI includes: */
#include "UIActionPoolRuntime.h"
#include "UICommon.h"
#include "UIExtraDataManager.h"
#include "UIFrameBuffer.h"
#include "UILoggingDefs.h"
#include "UIMachine.h"
#include "UIMachineLogic.h"
#include "UIMachineView.h"
#include "UIMachineWindow.h"
#include "UINotificationCenter.h"
#include "UISession.h"

/* COM includes: */
#include "CDisplay.h"
#include "CFramebuffer.h"
#include "CDisplaySourceBitmap.h"

/* VirtualBox interface declarations: */
#include <VBox/com/VirtualBox.h>

/* Other VBox includes: */
#include <iprt/critsect.h>
#include <VBoxVideo3D.h>

/* Other includes: */
#include <math.h>
#ifdef VBOX_WS_NIX
# include <X11/Xlib.h>
# undef Bool // Qt5 vs Xlib gift..
#endif /* VBOX_WS_NIX */


/** IFramebuffer implementation used to maintain VM display video memory. */
class ATL_NO_VTABLE UIFrameBufferPrivate : public QObject,
                                           public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                                           VBOX_SCRIPTABLE_IMPL(IFramebuffer)
{
    Q_OBJECT;

signals:

    /** Notifies listener about guest-screen resolution changes. */
    void sigNotifyChange(int iWidth, int iHeight);
    /** Notifies listener about guest-screen updates. */
    void sigNotifyUpdate(int iX, int iY, int iWidth, int iHeight);
    /** Notifies listener about guest-screen visible-region changes. */
    void sigSetVisibleRegion(QRegion region);

public:

    /** Constructs frame-buffer. */
    UIFrameBufferPrivate();
    /** Destructs frame-buffer. */
    virtual ~UIFrameBufferPrivate() RT_OVERRIDE;

    /** Frame-buffer initialization.
      * @param pMachineView defines machine-view this frame-buffer is bounded to. */
    virtual HRESULT init(UIMachineView *pMachineView);

    /** Assigns machine-view frame-buffer will be bounded to.
      * @param pMachineView defines machine-view this frame-buffer is bounded to. */
    virtual void setView(UIMachineView *pMachineView);

    /** Returns the copy of the IDisplay wrapper. */
    CDisplay display() const { return m_comDisplay; }
    /** Attach frame-buffer to IDisplay. */
    void attach();
    /** Detach frame-buffer from IDisplay. */
    void detach();

    /** Returns frame-buffer data address. */
    uchar *address() { return m_image.bits(); }
    /** Returns frame-buffer width. */
    ulong width() const { return m_iWidth; }
    /** Returns frame-buffer height. */
    ulong height() const { return m_iHeight; }
    /** Returns frame-buffer bits-per-pixel value. */
    ulong bitsPerPixel() const { return m_image.depth(); }
    /** Returns frame-buffer bytes-per-line value. */
    ulong bytesPerLine() const { return m_image.bytesPerLine(); }
    /** Returns default frame-buffer pixel-format. */
    ulong pixelFormat() const { return KBitmapFormat_BGR; }

    /** Defines whether frame-buffer is <b>unused</b>.
      * @note Refer to m_fUnused for more information.
      * @note Calls to this and any other EMT callback are synchronized (from GUI side). */
    void setMarkAsUnused(bool fUnused);

    /** Returns the frame-buffer's scaled-size. */
    QSize scaledSize() const { return m_scaledSize; }
    /** Defines host-to-guest scale ratio as @a size. */
    void setScaledSize(const QSize &size = QSize()) { m_scaledSize = size; }
    /** Returns x-origin of the host (scaled) content corresponding to x-origin of guest (actual) content. */
    inline int convertGuestXTo(int x) const { return m_scaledSize.isValid() ? qRound((double)m_scaledSize.width() / m_iWidth * x) : x; }
    /** Returns y-origin of the host (scaled) content corresponding to y-origin of guest (actual) content. */
    inline int convertGuestYTo(int y) const { return m_scaledSize.isValid() ? qRound((double)m_scaledSize.height() / m_iHeight * y) : y; }
    /** Returns x-origin of the guest (actual) content corresponding to x-origin of host (scaled) content. */
    inline int convertHostXTo(int x) const  { return m_scaledSize.isValid() ? qRound((double)m_iWidth / m_scaledSize.width() * x) : x; }
    /** Returns y-origin of the guest (actual) content corresponding to y-origin of host (scaled) content. */
    inline int convertHostYTo(int y) const  { return m_scaledSize.isValid() ? qRound((double)m_iHeight / m_scaledSize.height() * y) : y; }

    /** Returns the scale-factor used by the frame-buffer. */
    double scaleFactor() const { return m_dScaleFactor; }
    /** Define the scale-factor used by the frame-buffer. */
    void setScaleFactor(double dScaleFactor) { m_dScaleFactor = dScaleFactor; }

    /** Returns device-pixel-ratio set for HiDPI frame-buffer. */
    double devicePixelRatio() const { return m_dDevicePixelRatio; }
    /** Defines device-pixel-ratio set for HiDPI frame-buffer. */
    void setDevicePixelRatio(double dDevicePixelRatio);

    /** Returns smallest device-pixel-ratio denominator. */
    int smallestDenominator() const { return m_iSmallestDenominator; }

    /** Returns whether frame-buffer should use unscaled HiDPI output. */
    bool useUnscaledHiDPIOutput() const { return m_fUseUnscaledHiDPIOutput; }
    /** Defines whether frame-buffer should use unscaled HiDPI output. */
    void setUseUnscaledHiDPIOutput(bool fUseUnscaledHiDPIOutput) { m_fUseUnscaledHiDPIOutput = fUseUnscaledHiDPIOutput; }

    /** Returns frame-buffer scaling optimization type. */
    ScalingOptimizationType scalingOptimizationType() const { return m_enmScalingOptimizationType; }
    /** Defines frame-buffer scaling optimization type: */
    void setScalingOptimizationType(ScalingOptimizationType type) { m_enmScalingOptimizationType = type; }

    DECLARE_NOT_AGGREGATABLE(UIFrameBufferPrivate)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(UIFrameBufferPrivate)
        COM_INTERFACE_ENTRY(IFramebuffer)
        COM_INTERFACE_ENTRY2(IDispatch,IFramebuffer)
        COM_INTERFACE_ENTRY_AGGREGATE(IID_IMarshal, m_pUnkMarshaler.m_p)
    END_COM_MAP()

    HRESULT FinalConstruct();
    void FinalRelease();

    STDMETHOD(COMGETTER(Width))(ULONG *puWidth) RT_OVERRIDE;
    STDMETHOD(COMGETTER(Height))(ULONG *puHeight) RT_OVERRIDE;
    STDMETHOD(COMGETTER(BitsPerPixel))(ULONG *puBitsPerPixel) RT_OVERRIDE;
    STDMETHOD(COMGETTER(BytesPerLine))(ULONG *puBytesPerLine) RT_OVERRIDE;
    STDMETHOD(COMGETTER(PixelFormat))(BitmapFormat_T *puPixelFormat) RT_OVERRIDE;
    STDMETHOD(COMGETTER(HeightReduction))(ULONG *puHeightReduction) RT_OVERRIDE;
    STDMETHOD(COMGETTER(Overlay))(IFramebufferOverlay **ppOverlay) RT_OVERRIDE;
    STDMETHOD(COMGETTER(WinId))(LONG64 *pWinId) RT_OVERRIDE;
    STDMETHOD(COMGETTER(Capabilities))(ComSafeArrayOut(FramebufferCapabilities_T, aCapabilities)) RT_OVERRIDE;

    /** EMT callback: Notifies frame-buffer about guest-screen size change.
      * @param        uScreenId Guest screen number.
      * @param        uX        Horizontal origin of the update rectangle, in pixels.
      * @param        uY        Vertical origin of the update rectangle, in pixels.
      * @param        uWidth    Width of the guest display, in pixels.
      * @param        uHeight   Height of the guest display, in pixels.
      * @note         Any EMT callback is subsequent. No any other EMT callback can be called until this one processed.
      * @note         Calls to this and #setMarkAsUnused method are synchronized (from GUI side). */
    STDMETHOD(NotifyChange)(ULONG uScreenId, ULONG uX, ULONG uY, ULONG uWidth, ULONG uHeight) RT_OVERRIDE;

    /** EMT callback: Notifies frame-buffer about guest-screen update.
      * @param        uX      Horizontal origin of the update rectangle, in pixels.
      * @param        uY      Vertical origin of the update rectangle, in pixels.
      * @param        uWidth  Width of the update rectangle, in pixels.
      * @param        uHeight Height of the update rectangle, in pixels.
      * @note         Any EMT callback is subsequent. No any other EMT callback can be called until this one processed.
      * @note         Calls to this and #setMarkAsUnused method are synchronized (from GUI side). */
    STDMETHOD(NotifyUpdate)(ULONG uX, ULONG uY, ULONG uWidth, ULONG uHeight) RT_OVERRIDE;

    /** EMT callback: Notifies frame-buffer about guest-screen update.
      * @param        uX      Horizontal origin of the update rectangle, in pixels.
      * @param        uY      Vertical origin of the update rectangle, in pixels.
      * @param        uWidth  Width of the update rectangle, in pixels.
      * @param        uHeight Height of the update rectangle, in pixels.
      * @param        image   Brings image container which can be used to copy data from.
      * @note         Any EMT callback is subsequent. No any other EMT callback can be called until this one processed.
      * @note         Calls to this and #setMarkAsUnused method are synchronized (from GUI side). */
    STDMETHOD(NotifyUpdateImage)(ULONG uX, ULONG uY, ULONG uWidth, ULONG uHeight, ComSafeArrayIn(BYTE, image)) RT_OVERRIDE;

    /** EMT callback: Returns whether the frame-buffer implementation is willing to support a given video-mode.
      * @param        uWidth      Width of the guest display, in pixels.
      * @param        uHeight     Height of the guest display, in pixels.
      * @param        uBPP        Color depth, bits per pixel.
      * @param        pfSupported Is frame-buffer able/willing to render the video mode or not.
      * @note         Any EMT callback is subsequent. No any other EMT callback can be called until this one processed.
      * @note         Calls to this and #setMarkAsUnused method are synchronized (from GUI side). */
    STDMETHOD(VideoModeSupported)(ULONG uWidth, ULONG uHeight, ULONG uBPP, BOOL *pbSupported) RT_OVERRIDE;

    /** EMT callback which is not used in current implementation. */
    STDMETHOD(GetVisibleRegion)(BYTE *pRectangles, ULONG uCount, ULONG *puCountCopied) RT_OVERRIDE;
    /** EMT callback: Suggests new visible-region to this frame-buffer.
      * @param        pRectangles Pointer to the RTRECT array.
      * @param        uCount      Number of RTRECT elements in the rectangles array.
      * @note         Any EMT callback is subsequent. No any other EMT callback can be called until this one processed.
      * @note         Calls to this and #setMarkAsUnused method are synchronized (from GUI side). */
    STDMETHOD(SetVisibleRegion)(BYTE *pRectangles, ULONG uCount) RT_OVERRIDE;

    /** EMT callback which is not used in current implementation. */
    STDMETHOD(ProcessVHWACommand)(BYTE *pCommand, LONG enmCmd, BOOL fGuestCmd) RT_OVERRIDE;

    /** EMT callback: Notifies frame-buffer about 3D backend event.
      * @param        uType Event type. VBOX3D_NOTIFY_TYPE_*.
      * @param        aData Event-specific data, depends on the supplied event type.
      * @note         Any EMT callback is subsequent. No any other EMT callback can be called until this one processed.
      * @note         Calls to this and #setMarkAsUnused method are synchronized (from GUI side). */
    STDMETHOD(Notify3DEvent)(ULONG uType, ComSafeArrayIn(BYTE, data)) RT_OVERRIDE;

    /** Locks frame-buffer access. */
    void lock() const { RTCritSectEnter(&m_critSect); }
    /** Unlocks frame-buffer access. */
    void unlock() const { RTCritSectLeave(&m_critSect); }

    /** Handles frame-buffer notify-change-event. */
    virtual void handleNotifyChange(int iWidth, int iHeight);
    /** Handles frame-buffer paint-event. */
    virtual void handlePaintEvent(QPaintEvent *pEvent);
    /** Handles frame-buffer set-visible-region-event. */
    virtual void handleSetVisibleRegion(const QRegion &region);

    /** Performs frame-buffer resizing. */
    virtual void performResize(int iWidth, int iHeight);
    /** Performs frame-buffer rescaling. */
    virtual void performRescale();

    /** Handles viewport resize-event. */
    virtual void viewportResized(QResizeEvent*)
    {
    }

protected slots:

    /** Handles guest requests to change mouse pointer shape or position. */
    void sltMousePointerShapeOrPositionChange();

protected:

    /** Prepare connections routine. */
    void prepareConnections();
    /** Cleanup connections routine. */
    void cleanupConnections();

    /** Updates coordinate-system: */
    void updateCoordinateSystem();

    /** Default paint routine. */
    void paintDefault(QPaintEvent *pEvent);

    /** Returns the transformation mode corresponding to the passed @a dScaleFactor and ScalingOptimizationType. */
    static Qt::TransformationMode transformationMode(ScalingOptimizationType type, double dScaleFactor = 0);

    /** Erases corresponding @a rect with @a painter. */
    static void eraseImageRect(QPainter &painter, const QRect &rect,
                               double dDevicePixelRatio);
    /** Draws corresponding @a rect of passed @a image with @a painter. */
    static void drawImageRect(QPainter &painter, const QImage &image, const QRect &rect,
                              int iContentsShiftX, int iContentsShiftY,
                              double dDevicePixelRatio);

    /** Finds greatest common divisor for @a a and @a b. */
    static int greatestCommonDivisor(int a, int b);
    /** Rounds passed iNumber up to @a iNearest @a fUp. */
    static int roundNumberUpToNearest(int iNumber, int iNearest, bool fUp);

    /** Holds the screen-id. */
    ulong m_uScreenId;

    /** Holds the QImage buffer. */
    QImage m_image;
    /** Holds frame-buffer width. */
    int m_iWidth;
    /** Holds frame-buffer height. */
    int m_iHeight;

    /** Holds the copy of the IDisplay wrapper. */
    CDisplay m_comDisplay;
    /** Source bitmap from IDisplay. */
    CDisplaySourceBitmap m_sourceBitmap;
    /** Source bitmap from IDisplay (acquired but not yet applied). */
    CDisplaySourceBitmap m_pendingSourceBitmap;
    /** Holds whether there is a pending source bitmap which must be applied. */
    bool m_fPendingSourceBitmap;

    /** Holds machine-view this frame-buffer is bounded to. */
    UIMachineView *m_pMachineView;
    /** Holds window ID this frame-buffer referring to. */
    int64_t m_iWinId;

    /** Reflects whether screen-updates are allowed. */
    bool m_fUpdatesAllowed;

    /** Defines whether frame-buffer is <b>unused</b>.
      * <b>Unused</b> status determines whether frame-buffer should ignore EMT events or not. */
    bool m_fUnused;

    /** RTCRITSECT object to protect frame-buffer access. */
    mutable RTCRITSECT m_critSect;

    /** @name Scale-factor related variables.
     * @{ */
    /** Holds the scale-factor used by the scaled-size. */
    double m_dScaleFactor;
    /** Holds the scaling optimization type used by the scaling mechanism. */
    ScalingOptimizationType m_enmScalingOptimizationType;
    /** Holds the coordinate-system for the scale-factor above. */
    QTransform m_transform;
    /** Holds the frame-buffer's scaled-size. */
    QSize m_scaledSize;
    /** @} */

    /** @name Seamless mode related variables.
     * @{ */
    /* To avoid a seamless flicker which caused by the latency between
     * the initial visible-region arriving from EMT thread
     * and actual visible-region appliance on GUI thread
     * it was decided to use two visible-region instances: */
    /** Sync visible-region which being updated synchronously by locking EMT thread.
      * Used for immediate manual clipping of the painting operations. */
    QRegion m_syncVisibleRegion;
    /** Async visible-region which being updated asynchronously by posting async-event from EMT to GUI thread,
      * Used to update viewport parts for visible-region changes,
      * because NotifyUpdate doesn't take into account these changes. */
    QRegion m_asyncVisibleRegion;
    /** When the frame-buffer is being resized, visible region is saved here.
      * The saved region is applied when updates are enabled again. */
    QRegion m_pendingSyncVisibleRegion;
    /** @} */

    /** @name HiDPI screens related variables.
     * @{ */
    /** Holds device-pixel-ratio set for HiDPI frame-buffer. */
    double  m_dDevicePixelRatio;
    /** Holds the smallest device-pixel-ratio denominator. */
    int     m_iSmallestDenominator;
    /** Holds whether frame-buffer should use unscaled HiDPI output. */
    bool    m_fUseUnscaledHiDPIOutput;
    /** @} */

private:

#ifdef VBOX_WS_WIN
     ComPtr<IUnknown> m_pUnkMarshaler;
#endif
     /** Identifier returned by AttachFramebuffer. Used in DetachFramebuffer. */
     QUuid m_uFramebufferId;

     /** Holds the last cursor rectangle. */
     QRect  m_cursorRectangle;
};


#ifdef VBOX_WITH_XPCOM
NS_DECL_CLASSINFO(UIFrameBufferPrivate)
NS_IMPL_THREADSAFE_ISUPPORTS1_CI(UIFrameBufferPrivate, IFramebuffer)
#endif /* VBOX_WITH_XPCOM */


UIFrameBufferPrivate::UIFrameBufferPrivate()
    : m_uScreenId(0)
    , m_iWidth(0), m_iHeight(0)
    , m_fPendingSourceBitmap(false)
    , m_pMachineView(NULL)
    , m_iWinId(0)
    , m_fUpdatesAllowed(false)
    , m_fUnused(false)
    , m_dScaleFactor(1.0)
    , m_enmScalingOptimizationType(ScalingOptimizationType_None)
    , m_dDevicePixelRatio(1.0)
    , m_iSmallestDenominator(-1)
    , m_fUseUnscaledHiDPIOutput(false)
{
    LogRel2(("GUI: UIFrameBufferPrivate::UIFrameBufferPrivate %p\n", this));

    /* Update coordinate-system: */
    updateCoordinateSystem();
}

UIFrameBufferPrivate::~UIFrameBufferPrivate()
{
    LogRel2(("GUI: UIFrameBufferPrivate::~UIFrameBufferPrivate %p\n", this));

    /* Disconnect handlers: */
    if (m_pMachineView)
        cleanupConnections();

    /* Deinitialize critical-section: */
    RTCritSectDelete(&m_critSect);
}

HRESULT UIFrameBufferPrivate::init(UIMachineView *pMachineView)
{
    LogRel2(("GUI: UIFrameBufferPrivate::init %p\n", this));

    /* Fetch passed view: */
    setView(pMachineView);

    /* Assign display: */
    m_comDisplay = gpMachine->uisession()->display();

    /* Initialize critical-section: */
    int rc = RTCritSectInit(&m_critSect);
    AssertRC(rc);

    /* Resize/rescale frame-buffer to the default size: */
    performResize(640, 480);
    performRescale();

#ifdef VBOX_WS_WIN
    CoCreateFreeThreadedMarshaler(this, m_pUnkMarshaler.asOutParam());
#endif

    return S_OK;
}

void UIFrameBufferPrivate::setView(UIMachineView *pMachineView)
{
    /* Disconnect old handlers: */
    if (m_pMachineView)
        cleanupConnections();

    /* Reassign machine-view: */
    m_pMachineView = pMachineView;
    /* Reassign index: */
    m_uScreenId = m_pMachineView ? m_pMachineView->screenId() : 0;
    /* Recache window ID: */
    m_iWinId = (m_pMachineView && m_pMachineView->viewport()) ? (LONG64)m_pMachineView->viewport()->winId() : 0;

#ifdef VBOX_WS_NIX
    if (uiCommon().X11ServerAvailable())
        /* Resync Qt and X11 Server (see xTracker #7547). */
        XSync(NativeWindowSubsystem::X11GetDisplay(), false);
#endif

    /* Reconnect new handlers: */
    if (m_pMachineView)
        prepareConnections();
}

void UIFrameBufferPrivate::attach()
{
    m_uFramebufferId = display().AttachFramebuffer(m_uScreenId, CFramebuffer(this));
}

void UIFrameBufferPrivate::detach()
{
    CFramebuffer comFramebuffer = display().QueryFramebuffer(m_uScreenId);
    if (!comFramebuffer.isNull())
    {
        display().DetachFramebuffer(m_uScreenId, m_uFramebufferId);
        m_uFramebufferId = QUuid();
    }
}

void UIFrameBufferPrivate::setMarkAsUnused(bool fUnused)
{
    lock();
    m_fUnused = fUnused;
    unlock();
}

void UIFrameBufferPrivate::setDevicePixelRatio(double dDevicePixelRatio)
{
    /* Make sure something changed: */
    if (devicePixelRatio() == dDevicePixelRatio)
        return;

    /* Cache device-pixel-ratio (dpr): */
    m_dDevicePixelRatio = dDevicePixelRatio;

    /* For 100% host OS scale-factor we don't need denominator: */
    if (devicePixelRatio() == 1.0)
        m_iSmallestDenominator = -1;
    else
    {
        /* Recalculate smallest dpr denominator: */
        const int iDenominator = 100; // precision, cause scale-factor is percentage
        const int iNumerator = static_cast<int>(devicePixelRatio() * iDenominator);
        const int iGreatestCommonDivisor = greatestCommonDivisor(iNumerator, iDenominator);
        /* We need smallest denominator after all: */
        m_iSmallestDenominator = iDenominator / iGreatestCommonDivisor;
    }
    //printf("m_iSmallestDenominator = %d\n", m_iSmallestDenominator);
}

HRESULT UIFrameBufferPrivate::FinalConstruct()
{
    return 0;
}

void UIFrameBufferPrivate::FinalRelease()
{
    return;
}

STDMETHODIMP UIFrameBufferPrivate::COMGETTER(Width)(ULONG *puWidth)
{
    if (!puWidth)
        return E_POINTER;
    *puWidth = (ULONG)width();
    return S_OK;
}

STDMETHODIMP UIFrameBufferPrivate::COMGETTER(Height)(ULONG *puHeight)
{
    if (!puHeight)
        return E_POINTER;
    *puHeight = (ULONG)height();
    return S_OK;
}

STDMETHODIMP UIFrameBufferPrivate::COMGETTER(BitsPerPixel)(ULONG *puBitsPerPixel)
{
    if (!puBitsPerPixel)
        return E_POINTER;
    *puBitsPerPixel = bitsPerPixel();
    return S_OK;
}

STDMETHODIMP UIFrameBufferPrivate::COMGETTER(BytesPerLine)(ULONG *puBytesPerLine)
{
    if (!puBytesPerLine)
        return E_POINTER;
    *puBytesPerLine = bytesPerLine();
    return S_OK;
}

STDMETHODIMP UIFrameBufferPrivate::COMGETTER(PixelFormat)(BitmapFormat_T *puPixelFormat)
{
    if (!puPixelFormat)
        return E_POINTER;
    *puPixelFormat = (BitmapFormat_T)pixelFormat();
    return S_OK;
}

STDMETHODIMP UIFrameBufferPrivate::COMGETTER(HeightReduction)(ULONG *puHeightReduction)
{
    if (!puHeightReduction)
        return E_POINTER;
    *puHeightReduction = 0;
    return S_OK;
}

STDMETHODIMP UIFrameBufferPrivate::COMGETTER(Overlay)(IFramebufferOverlay **ppOverlay)
{
    if (!ppOverlay)
        return E_POINTER;
    *ppOverlay = 0;
    return S_OK;
}

STDMETHODIMP UIFrameBufferPrivate::COMGETTER(WinId)(LONG64 *pWinId)
{
    if (!pWinId)
        return E_POINTER;
    *pWinId = m_iWinId;
    return S_OK;
}

STDMETHODIMP UIFrameBufferPrivate::COMGETTER(Capabilities)(ComSafeArrayOut(FramebufferCapabilities_T, enmCapabilities))
{
    if (ComSafeArrayOutIsNull(enmCapabilities))
        return E_POINTER;

    com::SafeArray<FramebufferCapabilities_T> caps;
    if (uiCommon().isSeparateProcess())
    {
       caps.resize(2);
       caps[0] = FramebufferCapabilities_UpdateImage;
       caps[1] = FramebufferCapabilities_RenderCursor;
    }
    else
    {
       caps.resize(3);
       caps[0] = FramebufferCapabilities_VHWA;
       caps[1] = FramebufferCapabilities_VisibleRegion;
       caps[2] = FramebufferCapabilities_RenderCursor;
    }

    caps.detachTo(ComSafeArrayOutArg(enmCapabilities));
    return S_OK;
}

STDMETHODIMP UIFrameBufferPrivate::NotifyChange(ULONG uScreenId, ULONG uX, ULONG uY, ULONG uWidth, ULONG uHeight)
{
    CDisplaySourceBitmap sourceBitmap;
    if (!uiCommon().isSeparateProcess())
        display().QuerySourceBitmap(uScreenId, sourceBitmap);

    /* Lock access to frame-buffer: */
    lock();

    /* Make sure frame-buffer is used: */
    if (m_fUnused)
    {
        LogRel(("GUI: UIFrameBufferPrivate::NotifyChange: Screen=%lu, Origin=%lux%lu, Size=%lux%lu, Ignored!\n",
                (unsigned long)uScreenId,
                (unsigned long)uX, (unsigned long)uY,
                (unsigned long)uWidth, (unsigned long)uHeight));

        /* Unlock access to frame-buffer: */
        unlock();

        /* Ignore NotifyChange: */
        return E_FAIL;
    }

    /* Disable screen updates: */
    m_fUpdatesAllowed = false;

    /* While updates are disabled, visible region will be saved:  */
    m_pendingSyncVisibleRegion = QRegion();

    if (!uiCommon().isSeparateProcess())
    {
       /* Acquire new pending bitmap: */
       m_pendingSourceBitmap = sourceBitmap;
       m_fPendingSourceBitmap = true;
    }

    /* Widget resize is NOT thread-safe and *probably* never will be,
     * We have to notify machine-view with the async-signal to perform resize operation. */
    LogRel2(("GUI: UIFrameBufferPrivate::NotifyChange: Screen=%lu, Origin=%lux%lu, Size=%lux%lu, Sending to async-handler\n",
             (unsigned long)uScreenId,
             (unsigned long)uX, (unsigned long)uY,
             (unsigned long)uWidth, (unsigned long)uHeight));
    emit sigNotifyChange(uWidth, uHeight);

    /* Unlock access to frame-buffer: */
    unlock();

    /* Give up control token to other thread: */
    RTThreadYield();

    /* Confirm NotifyChange: */
    return S_OK;
}

STDMETHODIMP UIFrameBufferPrivate::NotifyUpdate(ULONG uX, ULONG uY, ULONG uWidth, ULONG uHeight)
{
    /* Lock access to frame-buffer: */
    lock();

    /* Make sure frame-buffer is used: */
    if (m_fUnused)
    {
        LogRel3(("GUI: UIFrameBufferPrivate::NotifyUpdate: Origin=%lux%lu, Size=%lux%lu, Ignored!\n",
                 (unsigned long)uX, (unsigned long)uY,
                 (unsigned long)uWidth, (unsigned long)uHeight));
        /* Unlock access to frame-buffer: */
        unlock();

        /* Ignore NotifyUpdate: */
        return E_FAIL;
    }

    /* Widget update is NOT thread-safe and *seems* never will be,
     * We have to notify machine-view with the async-signal to perform update operation. */
    LogRel3(("GUI: UIFrameBufferPrivate::NotifyUpdate: Origin=%lux%lu, Size=%lux%lu, Sending to async-handler\n",
             (unsigned long)uX, (unsigned long)uY,
             (unsigned long)uWidth, (unsigned long)uHeight));
    emit sigNotifyUpdate(uX, uY, uWidth, uHeight);

    /* Unlock access to frame-buffer: */
    unlock();

    /* Confirm NotifyUpdate: */
    return S_OK;
}

STDMETHODIMP UIFrameBufferPrivate::NotifyUpdateImage(ULONG uX, ULONG uY,
                                                     ULONG uWidth, ULONG uHeight,
                                                     ComSafeArrayIn(BYTE, image))
{
    /* Wrapping received data: */
    com::SafeArray<BYTE> imageData(ComSafeArrayInArg(image));

    /* Lock access to frame-buffer: */
    lock();

    /* Make sure frame-buffer is used: */
    if (m_fUnused)
    {
        LogRel3(("GUI: UIFrameBufferPrivate::NotifyUpdateImage: Origin=%lux%lu, Size=%lux%lu, Ignored!\n",
                 (unsigned long)uX, (unsigned long)uY,
                 (unsigned long)uWidth, (unsigned long)uHeight));

        /* Unlock access to frame-buffer: */
        unlock();

        /* Ignore NotifyUpdate: */
        return E_FAIL;
    }
    /* Directly update m_image if update fits: */
    if (   m_fUpdatesAllowed
        && uX + uWidth <= (ULONG)m_image.width()
        && uY + uHeight <= (ULONG)m_image.height())
    {
        /* Copy to m_image: */
        uchar *pu8Dst = m_image.bits() + uY * m_image.bytesPerLine() + uX * 4;
        uchar *pu8Src = imageData.raw();
        ULONG h;
        for (h = 0; h < uHeight; ++h)
        {
            memcpy(pu8Dst, pu8Src, uWidth * 4);
            pu8Dst += m_image.bytesPerLine();
            pu8Src += uWidth * 4;
        }

        /* Widget update is NOT thread-safe and *seems* never will be,
         * We have to notify machine-view with the async-signal to perform update operation. */
        LogRel3(("GUI: UIFrameBufferPrivate::NotifyUpdateImage: Origin=%lux%lu, Size=%lux%lu, Sending to async-handler\n",
                 (unsigned long)uX, (unsigned long)uY,
                 (unsigned long)uWidth, (unsigned long)uHeight));
        emit sigNotifyUpdate(uX, uY, uWidth, uHeight);
    }

    /* Unlock access to frame-buffer: */
    unlock();

    /* Confirm NotifyUpdateImage: */
    return S_OK;
}

STDMETHODIMP UIFrameBufferPrivate::VideoModeSupported(ULONG uWidth, ULONG uHeight, ULONG uBPP, BOOL *pfSupported)
{
    /* Make sure result pointer is valid: */
    if (!pfSupported)
    {
        LogRel2(("GUI: UIFrameBufferPrivate::IsVideoModeSupported: Mode: BPP=%lu, Size=%lux%lu, Invalid pfSupported pointer!\n",
                 (unsigned long)uBPP, (unsigned long)uWidth, (unsigned long)uHeight));

        return E_POINTER;
    }

    /* Lock access to frame-buffer: */
    lock();

    /* Make sure frame-buffer is used: */
    if (m_fUnused)
    {
        LogRel2(("GUI: UIFrameBufferPrivate::IsVideoModeSupported: Mode: BPP=%lu, Size=%lux%lu, Ignored!\n",
                 (unsigned long)uBPP, (unsigned long)uWidth, (unsigned long)uHeight));

        /* Unlock access to frame-buffer: */
        unlock();

        /* Ignore VideoModeSupported: */
        return E_FAIL;
    }

    /* Determine if supported: */
    *pfSupported = TRUE;
    const QSize screenSize = m_pMachineView->maximumGuestSize();
    if (   (screenSize.width() != 0)
        && (uWidth > (ULONG)screenSize.width())
        && (uWidth > (ULONG)width()))
        *pfSupported = FALSE;
    if (   (screenSize.height() != 0)
        && (uHeight > (ULONG)screenSize.height())
        && (uHeight > (ULONG)height()))
        *pfSupported = FALSE;
    if (*pfSupported)
       LogRel2(("GUI: UIFrameBufferPrivate::IsVideoModeSupported: Mode: BPP=%lu, Size=%lux%lu is supported\n",
                (unsigned long)uBPP, (unsigned long)uWidth, (unsigned long)uHeight));
    else
       LogRel(("GUI: UIFrameBufferPrivate::IsVideoModeSupported: Mode: BPP=%lu, Size=%lux%lu is NOT supported\n",
               (unsigned long)uBPP, (unsigned long)uWidth, (unsigned long)uHeight));

    /* Unlock access to frame-buffer: */
    unlock();

    /* Confirm VideoModeSupported: */
    return S_OK;
}

STDMETHODIMP UIFrameBufferPrivate::GetVisibleRegion(BYTE *pRectangles, ULONG uCount, ULONG *puCountCopied)
{
    PRTRECT rects = (PRTRECT)pRectangles;

    if (!rects)
        return E_POINTER;

    Q_UNUSED(uCount);
    Q_UNUSED(puCountCopied);

    return S_OK;
}

STDMETHODIMP UIFrameBufferPrivate::SetVisibleRegion(BYTE *pRectangles, ULONG uCount)
{
    /* Make sure rectangles were passed: */
    if (!pRectangles)
    {
        LogRel2(("GUI: UIFrameBufferPrivate::SetVisibleRegion: Rectangle count=%lu, Invalid pRectangles pointer!\n",
                 (unsigned long)uCount));

        return E_POINTER;
    }

    /* Lock access to frame-buffer: */
    lock();

    /* Make sure frame-buffer is used: */
    if (m_fUnused)
    {
        LogRel2(("GUI: UIFrameBufferPrivate::SetVisibleRegion: Rectangle count=%lu, Ignored!\n",
                 (unsigned long)uCount));

        /* Unlock access to frame-buffer: */
        unlock();

        /* Ignore SetVisibleRegion: */
        return E_FAIL;
    }

    /* Compose region: */
    QRegion region;
    PRTRECT rects = (PRTRECT)pRectangles;
    for (ULONG uIndex = 0; uIndex < uCount; ++uIndex)
    {
        /* Get current rectangle: */
        QRect rect;
        rect.setLeft(rects->xLeft);
        rect.setTop(rects->yTop);
        /* Which is inclusive: */
        rect.setRight(rects->xRight - 1);
        rect.setBottom(rects->yBottom - 1);
        /* Append region: */
        region += rect;
        ++rects;
    }
    /* Tune according scale-factor: */
    if (scaleFactor() != 1.0 || devicePixelRatio() > 1.0)
        region = m_transform.map(region);

    if (m_fUpdatesAllowed)
    {
        /* We are directly updating synchronous visible-region: */
        m_syncVisibleRegion = region;
        /* And send async-signal to update asynchronous one: */
        LogRel2(("GUI: UIFrameBufferPrivate::SetVisibleRegion: Rectangle count=%lu, Sending to async-handler\n",
                 (unsigned long)uCount));
        emit sigSetVisibleRegion(region);
    }
    else
    {
        /* Save the region. */
        m_pendingSyncVisibleRegion = region;
        LogRel2(("GUI: UIFrameBufferPrivate::SetVisibleRegion: Rectangle count=%lu, Saved\n",
                 (unsigned long)uCount));
    }

    /* Unlock access to frame-buffer: */
    unlock();

    /* Confirm SetVisibleRegion: */
    return S_OK;
}

STDMETHODIMP UIFrameBufferPrivate::ProcessVHWACommand(BYTE *pCommand, LONG enmCmd, BOOL fGuestCmd)
{
    RT_NOREF(pCommand, enmCmd, fGuestCmd);
    return E_NOTIMPL;
}

STDMETHODIMP UIFrameBufferPrivate::Notify3DEvent(ULONG uType, ComSafeArrayIn(BYTE, data))
{
    /* Lock access to frame-buffer: */
    lock();

    /* Make sure frame-buffer is used: */
    if (m_fUnused)
    {
        LogRel2(("GUI: UIFrameBufferPrivate::Notify3DEvent: Ignored!\n"));

        /* Unlock access to frame-buffer: */
        unlock();

        /* Ignore Notify3DEvent: */
        return E_FAIL;
    }

    Q_UNUSED(data);
#ifdef VBOX_WITH_XPCOM
    Q_UNUSED(dataSize);
#endif /* VBOX_WITH_XPCOM */
    // com::SafeArray<BYTE> eventData(ComSafeArrayInArg(data));
    switch (uType)
    {
        case VBOX3D_NOTIFY_TYPE_3DDATA_VISIBLE:
        case VBOX3D_NOTIFY_TYPE_3DDATA_HIDDEN:
        {
            /// @todo wipe out whole case when confirmed
            // We are no more supporting this, am I right?
            AssertFailed();
            /* Confirm Notify3DEvent: */
            return S_OK;
        }

        case VBOX3D_NOTIFY_TYPE_TEST_FUNCTIONAL:
        {
            HRESULT hr = m_fUnused ? E_FAIL : S_OK;
            unlock();
            return hr;
        }

        default:
            break;
    }

    /* Unlock access to frame-buffer: */
    unlock();

    /* Ignore Notify3DEvent: */
    return E_INVALIDARG;
}

void UIFrameBufferPrivate::handleNotifyChange(int iWidth, int iHeight)
{
    LogRel2(("GUI: UIFrameBufferPrivate::handleNotifyChange: Size=%dx%d\n", iWidth, iHeight));

    /* Make sure machine-view is assigned: */
    AssertPtrReturnVoid(m_pMachineView);

    /* Lock access to frame-buffer: */
    lock();

    /* If there is NO pending source-bitmap: */
    if (!uiCommon().isSeparateProcess() && !m_fPendingSourceBitmap)
    {
        /* Do nothing, change-event already processed: */
        LogRel2(("GUI: UIFrameBufferPrivate::handleNotifyChange: Already processed.\n"));
        /* Unlock access to frame-buffer: */
        unlock();
        /* Return immediately: */
        return;
    }

    /* Release the current bitmap and keep the pending one: */
    m_sourceBitmap = m_pendingSourceBitmap;
    m_pendingSourceBitmap = 0;
    m_fPendingSourceBitmap = false;

    /* Unlock access to frame-buffer: */
    unlock();

    /* Perform frame-buffer resize: */
    performResize(iWidth, iHeight);
}

void UIFrameBufferPrivate::handlePaintEvent(QPaintEvent *pEvent)
{
    LogRel3(("GUI: UIFrameBufferPrivate::handlePaintEvent: Origin=%lux%lu, Size=%dx%d\n",
             pEvent->rect().x(), pEvent->rect().y(),
             pEvent->rect().width(), pEvent->rect().height()));

    /* On mode switch the enqueued paint-event may still come
     * while the machine-view is already null (before the new machine-view set),
     * ignore paint-event in that case. */
    if (!m_pMachineView)
        return;

    /* Lock access to frame-buffer: */
    lock();

    /* But if updates disabled: */
    if (!m_fUpdatesAllowed)
    {
        /* Unlock access to frame-buffer: */
        unlock();
        /* And return immediately: */
        return;
    }

    /* Handle paint-event: */
    paintDefault(pEvent);

    /* Unlock access to frame-buffer: */
    unlock();
}

void UIFrameBufferPrivate::handleSetVisibleRegion(const QRegion &region)
{
    /* Make sure async visible-region has changed or wasn't yet applied: */
    if (   m_asyncVisibleRegion == region
#ifdef VBOX_WITH_MASKED_SEAMLESS
        && m_asyncVisibleRegion == m_pMachineView->machineWindow()->mask()
#endif /* VBOX_WITH_MASKED_SEAMLESS */
           )
        return;

    /* We are accounting async visible-regions one-by-one
     * to keep corresponding viewport area always updated! */
    if (!m_asyncVisibleRegion.isEmpty())
        m_pMachineView->viewport()->update(m_asyncVisibleRegion - region);

    /* Remember last visible region: */
    m_asyncVisibleRegion = region;

#ifdef VBOX_WITH_MASKED_SEAMLESS
    /* We have to use async visible-region to apply to [Q]Widget [set]Mask: */
    m_pMachineView->machineWindow()->setMask(m_asyncVisibleRegion);
#endif /* VBOX_WITH_MASKED_SEAMLESS */
}

void UIFrameBufferPrivate::performResize(int iWidth, int iHeight)
{
    /* Make sure machine-view is assigned: */
    AssertReturnVoidStmt(m_pMachineView, LogRel(("GUI: UIFrameBufferPrivate::performResize: Size=%dx%d\n", iWidth, iHeight)));

    /* Invalidate visible-region (if necessary): */
    if (m_pMachineView->machineLogic()->visualStateType() == UIVisualStateType_Seamless &&
        (m_iWidth != iWidth || m_iHeight != iHeight))
    {
        lock();
        m_syncVisibleRegion = QRegion();
        m_asyncVisibleRegion = QRegion();
        unlock();
    }

    /* If source-bitmap invalid: */
    if (m_sourceBitmap.isNull())
    {
        /* Remember new size came from hint: */
        m_iWidth = iWidth;
        m_iHeight = iHeight;
        LogRel(("GUI: UIFrameBufferPrivate::performResize: Size=%dx%d, Using fallback buffer since no source bitmap is provided\n",
                m_iWidth, m_iHeight));

        /* And recreate fallback buffer: */
        m_image = QImage(m_iWidth, m_iHeight, QImage::Format_RGB32);
        m_image.fill(0);
    }
    /* If source-bitmap valid: */
    else
    {
        /* Acquire source-bitmap attributes: */
        BYTE *pAddress = NULL;
        ULONG ulWidth = 0;
        ULONG ulHeight = 0;
        ULONG ulBitsPerPixel = 0;
        ULONG ulBytesPerLine = 0;
        KBitmapFormat bitmapFormat = KBitmapFormat_Opaque;
        m_sourceBitmap.QueryBitmapInfo(pAddress,
                                       ulWidth,
                                       ulHeight,
                                       ulBitsPerPixel,
                                       ulBytesPerLine,
                                       bitmapFormat);
        Assert(ulBitsPerPixel == 32);

        /* Remember new actual size: */
        m_iWidth = (int)ulWidth;
        m_iHeight = (int)ulHeight;
        LogRel2(("GUI: UIFrameBufferPrivate::performResize: Size=%dx%d, Directly using source bitmap content\n",
                 m_iWidth, m_iHeight));

        /* Recreate QImage on the basis of source-bitmap content: */
        m_image = QImage(pAddress, m_iWidth, m_iHeight, ulBytesPerLine, QImage::Format_RGB32);

        /* Check whether guest color depth differs from the bitmap color depth: */
        ULONG ulGuestBitsPerPixel = 0;
        LONG xOrigin = 0;
        LONG yOrigin = 0;
        KGuestMonitorStatus monitorStatus = KGuestMonitorStatus_Enabled;
        display().GetScreenResolution(m_uScreenId, ulWidth, ulHeight, ulGuestBitsPerPixel, xOrigin, yOrigin, monitorStatus);

        /* Remind user if necessary, ignore text and VGA modes: */
        /* This check (supports graphics) is not quite right due to past mistakes
         * in the Guest Additions protocol, but in practice it should be fine. */
        if (   ulGuestBitsPerPixel != ulBitsPerPixel
            && ulGuestBitsPerPixel != 0
            && m_pMachineView->uimachine()->isGuestSupportsGraphics())
            UINotificationMessage::remindAboutWrongColorDepth(ulGuestBitsPerPixel, ulBitsPerPixel);
        else
            UINotificationMessage::forgetAboutWrongColorDepth();
    }

    lock();

    /* Enable screen updates: */
    m_fUpdatesAllowed = true;

    if (!m_pendingSyncVisibleRegion.isEmpty())
    {
        /* Directly update synchronous visible-region: */
        m_syncVisibleRegion = m_pendingSyncVisibleRegion;
        m_pendingSyncVisibleRegion = QRegion();

        /* And send async-signal to update asynchronous one: */
        LogRel2(("GUI: UIFrameBufferPrivate::performResize: Rectangle count=%lu, Sending to async-handler\n",
                 (unsigned long)m_syncVisibleRegion.rectCount()));
        emit sigSetVisibleRegion(m_syncVisibleRegion);
    }

    /* Make sure that the current screen image is immediately displayed: */
    m_pMachineView->viewport()->update();

    unlock();

    /* Make sure action-pool knows frame-buffer size: */
    m_pMachineView->uimachine()->actionPool()->toRuntime()->setGuestScreenSize(m_pMachineView->screenId(),
                                                                               QSize(m_iWidth, m_iHeight));
}

void UIFrameBufferPrivate::performRescale()
{
//    printf("UIFrameBufferPrivate::performRescale\n");

    /* Make sure machine-view is assigned: */
    AssertPtrReturnVoid(m_pMachineView);

    /* Depending on current visual state: */
    switch (m_pMachineView->machineLogic()->visualStateType())
    {
        case UIVisualStateType_Scale:
            m_scaledSize = scaledSize().width() == m_iWidth && scaledSize().height() == m_iHeight ? QSize() : scaledSize();
            break;
        default:
            m_scaledSize = scaleFactor() == 1.0 ? QSize() : QSize((int)(m_iWidth * scaleFactor()), (int)(m_iHeight * scaleFactor()));
            break;
    }

    /* Update coordinate-system: */
    updateCoordinateSystem();

//    printf("UIFrameBufferPrivate::performRescale: Complete: Scale-factor=%f, Scaled-size=%dx%d\n",
//           scaleFactor(), scaledSize().width(), scaledSize().height());
}

void UIFrameBufferPrivate::sltMousePointerShapeOrPositionChange()
{
    /* Do we have view and valid cursor position?
     * Also, please take into account, we are not currently painting
     * framebuffer cursor if mouse integration is supported and enabled. */
    if (   m_pMachineView
        && !m_pMachineView->uimachine()->isHidingHostPointer()
        && m_pMachineView->uimachine()->isValidPointerShapePresent()
        && m_pMachineView->uimachine()->isValidCursorPositionPresent()
        && (   !m_pMachineView->uimachine()->isMouseIntegrated()
            || !m_pMachineView->uimachine()->isMouseSupportsAbsolute()))
    {
        /* Acquire cursor hotspot: */
        QPoint cursorHotspot = m_pMachineView->uimachine()->cursorHotspot();
        /* Apply the scale-factor if necessary: */
        cursorHotspot /= scaleFactor();
        /* Take the device-pixel-ratio into account: */
        if (!useUnscaledHiDPIOutput())
            cursorHotspot /= devicePixelRatio();

        /* Acquire cursor position and size: */
        QPoint cursorPosition = m_pMachineView->uimachine()->cursorPosition() - cursorHotspot;
        QSize cursorSize = m_pMachineView->uimachine()->cursorSize();
        /* Apply the scale-factor if necessary: */
        cursorPosition *= scaleFactor();
        cursorSize *= scaleFactor();
        /* Take the device-pixel-ratio into account: */
        if (useUnscaledHiDPIOutput())
        {
            cursorPosition /= devicePixelRatio();
            cursorSize /= devicePixelRatio();
        }

        /* Call for a viewport update, we need to update cumulative
         * region containing previous and current cursor rectagles. */
        const QRect cursorRectangle = QRect(cursorPosition, cursorSize);
        m_pMachineView->viewport()->update(QRegion(m_cursorRectangle) + cursorRectangle);

        /* Remember current cursor rectangle: */
        m_cursorRectangle = cursorRectangle;
    }
    /* Don't forget to clear the rectangle in opposite case: */
    else if (   m_pMachineView
             && m_cursorRectangle.isValid())
    {
        /* Call for a cursor area update: */
        m_pMachineView->viewport()->update(m_cursorRectangle);
    }
}

void UIFrameBufferPrivate::prepareConnections()
{
    /* Attach EMT connections: */
    connect(this, &UIFrameBufferPrivate::sigNotifyChange,
            m_pMachineView, &UIMachineView::sltHandleNotifyChange,
            Qt::QueuedConnection);
    connect(this, &UIFrameBufferPrivate::sigNotifyUpdate,
            m_pMachineView, &UIMachineView::sltHandleNotifyUpdate,
            Qt::QueuedConnection);
    connect(this, &UIFrameBufferPrivate::sigSetVisibleRegion,
            m_pMachineView, &UIMachineView::sltHandleSetVisibleRegion,
            Qt::QueuedConnection);

    /* Attach GUI connections: */
    connect(m_pMachineView->uimachine(), &UIMachine::sigMousePointerShapeChange,
            this, &UIFrameBufferPrivate::sltMousePointerShapeOrPositionChange);
    connect(m_pMachineView->uimachine(), &UIMachine::sigCursorPositionChange,
            this, &UIFrameBufferPrivate::sltMousePointerShapeOrPositionChange);
}

void UIFrameBufferPrivate::cleanupConnections()
{
    /* Detach EMT connections: */
    disconnect(this, &UIFrameBufferPrivate::sigNotifyChange,
               m_pMachineView, &UIMachineView::sltHandleNotifyChange);
    disconnect(this, &UIFrameBufferPrivate::sigNotifyUpdate,
               m_pMachineView, &UIMachineView::sltHandleNotifyUpdate);
    disconnect(this, &UIFrameBufferPrivate::sigSetVisibleRegion,
               m_pMachineView, &UIMachineView::sltHandleSetVisibleRegion);

    /* Detach GUI connections: */
    disconnect(m_pMachineView->uimachine(), &UIMachine::sigMousePointerShapeChange,
               this, &UIFrameBufferPrivate::sltMousePointerShapeOrPositionChange);
    disconnect(m_pMachineView->uimachine(), &UIMachine::sigCursorPositionChange,
               this, &UIFrameBufferPrivate::sltMousePointerShapeOrPositionChange);
}

void UIFrameBufferPrivate::updateCoordinateSystem()
{
    /* Reset to default: */
    m_transform = QTransform();

    /* Apply the scale-factor if necessary: */
    if (scaleFactor() != 1.0)
        m_transform = m_transform.scale(scaleFactor(), scaleFactor());

    /* Take the device-pixel-ratio into account: */
    if (useUnscaledHiDPIOutput())
        m_transform = m_transform.scale(1.0 / devicePixelRatio(), 1.0 / devicePixelRatio());
}

void UIFrameBufferPrivate::paintDefault(QPaintEvent *pEvent)
{
    /* Make sure cached image is valid: */
    if (m_image.isNull())
        return;

    /* First we take the cached image as the source: */
    QImage *pSourceImage = &m_image;

    /* But if we should scale image by some reason: */
    if (   scaledSize().isValid()
        || (!useUnscaledHiDPIOutput() && devicePixelRatio() != 1.0))
    {
        /* Calculate final scaled size: */
        QSize effectiveSize = !scaledSize().isValid() ? pSourceImage->size() : scaledSize();
        /* Take the device-pixel-ratio into account: */
        if (!useUnscaledHiDPIOutput() && devicePixelRatio() != 1.0)
            effectiveSize *= devicePixelRatio();
        /* We scale the image to requested size and retain it
         * by making heap shallow copy of that temporary object: */
        switch (m_pMachineView->visualStateType())
        {
            case UIVisualStateType_Scale:
                pSourceImage = new QImage(pSourceImage->scaled(effectiveSize, Qt::IgnoreAspectRatio,
                                                               transformationMode(scalingOptimizationType())));
                break;
            default:
                pSourceImage = new QImage(pSourceImage->scaled(effectiveSize, Qt::IgnoreAspectRatio,
                                                               transformationMode(scalingOptimizationType(), scaleFactor())));
                break;
        }
    }

    /* Prepare the base and hidpi paint rectangles: */
    QRect paintRect = pEvent->rect();
    /* Adjust original rectangle the way that it can be fractionally upscaled to int values: */
    if (smallestDenominator() != -1)
    {
        paintRect.setLeft(roundNumberUpToNearest(paintRect.left(), smallestDenominator(), false /* up */));
        paintRect.setTop(roundNumberUpToNearest(paintRect.top(), smallestDenominator(), false /* up */));
        paintRect.setWidth(roundNumberUpToNearest(paintRect.width(), smallestDenominator(), true /* up */));
        paintRect.setHeight(roundNumberUpToNearest(paintRect.height(), smallestDenominator(), true /* up */));
    }
    QRect paintRectHiDPI = paintRect;

    /* Take the device-pixel-ratio into account: */
    paintRectHiDPI.moveTo(paintRectHiDPI.topLeft() * devicePixelRatio());
    paintRectHiDPI.setSize(paintRectHiDPI.size() * devicePixelRatio());

    /* Make sure hidpi paint rectangle is within the image boundary: */
    paintRectHiDPI &= pSourceImage->rect();
    if (paintRectHiDPI.isEmpty())
        return;

    /* Create painter: */
    QPainter painter(m_pMachineView->viewport());

    /* Depending on visual-state type: */
    switch (m_pMachineView->visualStateType())
    {
        case UIVisualStateType_Normal:
        case UIVisualStateType_Fullscreen:
        case UIVisualStateType_Scale:
        {
#ifdef VBOX_WS_MAC
            /* On OSX for Qt5 we need to fill the backing store first: */
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.fillRect(paintRect, QColor(Qt::black));
            painter.setCompositionMode(QPainter::CompositionMode_SourceAtop);
#endif /* VBOX_WS_MAC */

            break;
        }
        case UIVisualStateType_Seamless:
        {
            /* Adjust painter for erasing: */
            lock();
            painter.setClipRegion(QRegion(paintRect) - m_syncVisibleRegion);
            painter.setCompositionMode(QPainter::CompositionMode_Clear);
            unlock();

            /* Erase hidpi rectangle: */
            eraseImageRect(painter, paintRectHiDPI,
                           devicePixelRatio());

            /* Adjust painter for painting: */
            lock();
            painter.setClipRegion(QRegion(paintRect) & m_syncVisibleRegion);
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
            unlock();

#ifdef VBOX_WITH_TRANSLUCENT_SEAMLESS
            /* In case of translucent seamless for Qt5 we need to fill the backing store first: */
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.fillRect(paintRect, QColor(Qt::black));
            painter.setCompositionMode(QPainter::CompositionMode_SourceAtop);
#endif /* VBOX_WITH_TRANSLUCENT_SEAMLESS */

            break;
        }
        default:
            break;
    }

    /* Draw hidpi image rectangle: */
    drawImageRect(painter, *pSourceImage, paintRectHiDPI,
                  m_pMachineView->contentsX(), m_pMachineView->contentsY(),
                  devicePixelRatio());

    /* If we had to scale image for some reason: */
    if (   scaledSize().isValid()
        || (!useUnscaledHiDPIOutput() && devicePixelRatio() != 1.0))
    {
        /* Wipe out copied image: */
        delete pSourceImage;
        pSourceImage = 0;
    }

    /* Paint cursor if it has valid shape and position.
     * Also, please take into account, we are not currently painting
     * framebuffer cursor if mouse integration is supported and enabled. */
    if (   !m_cursorRectangle.isNull()
        && !m_pMachineView->uimachine()->isHidingHostPointer()
        && m_pMachineView->uimachine()->isValidPointerShapePresent()
        && m_pMachineView->uimachine()->isValidCursorPositionPresent()
        && (   !m_pMachineView->uimachine()->isMouseIntegrated()
            || !m_pMachineView->uimachine()->isMouseSupportsAbsolute()))
    {
        /* Acquire session cursor shape pixmap: */
        QPixmap cursorPixmap = m_pMachineView->uimachine()->cursorShapePixmap();

        /* Take the device-pixel-ratio into account: */
        cursorPixmap.setDevicePixelRatio(devicePixelRatio());

        /* Draw sub-pixmap: */
        painter.drawPixmap(m_cursorRectangle, cursorPixmap);
    }
}

/* static */
Qt::TransformationMode UIFrameBufferPrivate::transformationMode(ScalingOptimizationType type, double dScaleFactor /* = 0 */)
{
    switch (type)
    {
        /* Check if optimization type is forced to be 'Performance': */
        case ScalingOptimizationType_Performance: return Qt::FastTransformation;
        default: break;
    }
    /* For integer-scaling we are choosing the 'Performance' optimization type ourselves: */
    return dScaleFactor && floor(dScaleFactor) == dScaleFactor ? Qt::FastTransformation : Qt::SmoothTransformation;;
}

/* static */
void UIFrameBufferPrivate::eraseImageRect(QPainter &painter, const QRect &rect,
                                          double dDevicePixelRatio)
{
    /* Prepare sub-pixmap: */
    QPixmap subPixmap = QPixmap(rect.width(), rect.height());
    /* Take the device-pixel-ratio into account: */
    subPixmap.setDevicePixelRatio(dDevicePixelRatio);

    /* Which point we should draw corresponding sub-pixmap? */
    QPoint paintPoint = rect.topLeft();
    /* Take the device-pixel-ratio into account: */
    paintPoint /= dDevicePixelRatio;

    /* Draw sub-pixmap: */
    painter.drawPixmap(paintPoint, subPixmap);
}

/* static */
void UIFrameBufferPrivate::drawImageRect(QPainter &painter, const QImage &image, const QRect &rect,
                                         int iContentsShiftX, int iContentsShiftY,
                                         double dDevicePixelRatio)
{
    /* Calculate offset: */
    const size_t offset = (rect.x() + iContentsShiftX) * image.depth() / 8 +
                          (rect.y() + iContentsShiftY) * image.bytesPerLine();

    /* Restrain boundaries: */
    const int iSubImageWidth = qMin(rect.width(), image.width() - rect.x() - iContentsShiftX);
    const int iSubImageHeight = qMin(rect.height(), image.height() - rect.y() - iContentsShiftY);

    /* Create sub-image (no copy involved): */
    QImage subImage = QImage(image.bits() + offset,
                             iSubImageWidth, iSubImageHeight,
                             image.bytesPerLine(), image.format());

    /* Create sub-pixmap on the basis of sub-image above (1st copy involved): */
    QPixmap subPixmap = QPixmap::fromImage(subImage);
    /* Take the device-pixel-ratio into account: */
    subPixmap.setDevicePixelRatio(dDevicePixelRatio);

    /* Which point we should draw corresponding sub-pixmap? */
    QPoint paintPoint = rect.topLeft();
    /* Take the device-pixel-ratio into account: */
    paintPoint /= dDevicePixelRatio;

    /* Draw sub-pixmap: */
    painter.drawPixmap(paintPoint, subPixmap);
}

/* static */
int UIFrameBufferPrivate::greatestCommonDivisor(int a, int b)
{
    /* Using simple Euclidean algorithm: */
    while (b)
    {
        a %= b;
        qSwap(a, b);
    }
    return a;
}

/* static */
int UIFrameBufferPrivate::roundNumberUpToNearest(int iNumber, int iNearest, bool fUp)
{
    /* Round passed iNumber up to iNearest: */
    int iResult = (iNumber / iNearest) * iNearest;
    /* Append iNearest if requested: */
    if (fUp && iResult < iNumber)
        iResult += iNearest;
    return iResult;
}


UIFrameBuffer::UIFrameBuffer()
    : m_fInitialized(false)
{
    prepare();
}

UIFrameBuffer::~UIFrameBuffer()
{
    cleanup();
}

HRESULT UIFrameBuffer::init(UIMachineView *pMachineView)
{
    const HRESULT rc = m_pFrameBuffer->init(pMachineView);
    m_fInitialized = true;
    return rc;
}

void UIFrameBuffer::attach()
{
    m_pFrameBuffer->attach();
}

void UIFrameBuffer::detach()
{
    m_pFrameBuffer->detach();
}

uchar *UIFrameBuffer::address()
{
    return m_pFrameBuffer->address();
}

ulong UIFrameBuffer::width() const
{
    return m_pFrameBuffer->width();
}

ulong UIFrameBuffer::height() const
{
    return m_pFrameBuffer->height();
}

ulong UIFrameBuffer::bitsPerPixel() const
{
    return m_pFrameBuffer->bitsPerPixel();
}

ulong UIFrameBuffer::bytesPerLine() const
{
    return m_pFrameBuffer->bytesPerLine();
}

void UIFrameBuffer::setView(UIMachineView *pMachineView)
{
    m_pFrameBuffer->setView(pMachineView);
}

void UIFrameBuffer::setMarkAsUnused(bool fUnused)
{
    m_pFrameBuffer->setMarkAsUnused(fUnused);
}

QSize UIFrameBuffer::scaledSize() const
{
    return m_pFrameBuffer->scaledSize();
}

void UIFrameBuffer::setScaledSize(const QSize &size /* = QSize() */)
{
    m_pFrameBuffer->setScaledSize(size);
}

int UIFrameBuffer::convertHostXTo(int iX) const
{
    return m_pFrameBuffer->convertHostXTo(iX);
}

int UIFrameBuffer::convertHostYTo(int iY) const
{
    return m_pFrameBuffer->convertHostXTo(iY);
}

double UIFrameBuffer::scaleFactor() const
{
    return m_pFrameBuffer->scaleFactor();
}

void UIFrameBuffer::setScaleFactor(double dScaleFactor)
{
    m_pFrameBuffer->setScaleFactor(dScaleFactor);
}

double UIFrameBuffer::devicePixelRatio() const
{
    return m_pFrameBuffer->devicePixelRatio();
}

void UIFrameBuffer::setDevicePixelRatio(double dDevicePixelRatio)
{
    m_pFrameBuffer->setDevicePixelRatio(dDevicePixelRatio);
}

bool UIFrameBuffer::useUnscaledHiDPIOutput() const
{
    return m_pFrameBuffer->useUnscaledHiDPIOutput();
}

void UIFrameBuffer::setUseUnscaledHiDPIOutput(bool fUseUnscaledHiDPIOutput)
{
    m_pFrameBuffer->setUseUnscaledHiDPIOutput(fUseUnscaledHiDPIOutput);
}

ScalingOptimizationType UIFrameBuffer::scalingOptimizationType() const
{
    return m_pFrameBuffer->scalingOptimizationType();
}

void UIFrameBuffer::setScalingOptimizationType(ScalingOptimizationType type)
{
    m_pFrameBuffer->setScalingOptimizationType(type);
}

void UIFrameBuffer::handleNotifyChange(int iWidth, int iHeight)
{
    m_pFrameBuffer->handleNotifyChange(iWidth, iHeight);
}

void UIFrameBuffer::handlePaintEvent(QPaintEvent *pEvent)
{
    m_pFrameBuffer->handlePaintEvent(pEvent);
}

void UIFrameBuffer::handleSetVisibleRegion(const QRegion &region)
{
    m_pFrameBuffer->handleSetVisibleRegion(region);
}

void UIFrameBuffer::performResize(int iWidth, int iHeight)
{
    m_pFrameBuffer->performResize(iWidth, iHeight);
}

void UIFrameBuffer::performRescale()
{
    m_pFrameBuffer->performRescale();
}

void UIFrameBuffer::viewportResized(QResizeEvent *pEvent)
{
    m_pFrameBuffer->viewportResized(pEvent);
}

void UIFrameBuffer::prepare()
{
    /* Creates COM object we are linked to: */
    m_pFrameBuffer.createObject();

    /* Take scaling optimization type into account: */
    setScalingOptimizationType(gEDataManager->scalingOptimizationType(uiCommon().managedVMUuid()));
}

void UIFrameBuffer::cleanup()
{
    /* Detach COM object we are linked to: */
    m_pFrameBuffer.setNull();
}

#include "UIFrameBuffer.moc"
