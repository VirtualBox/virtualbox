/* $Id: vrdp.h 114935 2026-08-10 12:46:46Z vitali.pelenjow@oracle.com $ */
/** @file
 * VBox Remote Desktop Protocol.
 */

/*
 * Copyright (C) 2006-2026 Oracle and/or its affiliates.
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

#ifndef VRDP_INCLUDED_SRC_vrdp_h
#define VRDP_INCLUDED_SRC_vrdp_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "utils.h"
#include "secure.h"
#include "orders.h"
#include "vrdpdata.h"
#include "vrdpstream.h"
#include "regions.h"
#include "textcache.h"
#include "rdpdr.h"
#include <VBox/RemoteDesktop/VRDE.h>

extern "C" {
#include "mixeng/mixeng.h"
}

/* RDP5 disconnect PDU */
#define exDiscReasonNoInfo                              0x0000
#define exDiscReasonAPIInitiatedDisconnect              0x0001
#define exDiscReasonAPIInitiatedLogoff                  0x0002
#define exDiscReasonServerIdleTimeout                   0x0003
#define exDiscReasonServerLogonTimeout                  0x0004
#define exDiscReasonReplacedByOtherConnection           0x0005
#define exDiscReasonOutOfMemory                         0x0006
#define exDiscReasonServerDeniedConnection              0x0007
#define exDiscReasonServerDeniedConnectionFips          0x0008
#define exDiscReasonLicenseInternal                     0x0100
#define exDiscReasonLicenseNoLicenseServer              0x0101
#define exDiscReasonLicenseNoLicense                    0x0102
#define exDiscReasonLicenseErrClientMsg                 0x0103
#define exDiscReasonLicenseHwidDoesntMatchLicense       0x0104
#define exDiscReasonLicenseErrClientLicense             0x0105
#define exDiscReasonLicenseCantFinishProtocol           0x0106
#define exDiscReasonLicenseClientEndedProtocol          0x0107
#define exDiscReasonLicenseErrClientEncryption          0x0108
#define exDiscReasonLicenseCantUpgradeLicense           0x0109
#define exDiscReasonLicenseNoRemoteConnections          0x010a

enum RDP_PDU_TYPE
{
    RDP_PDU_DEMAND_ACTIVE = 1,
    RDP_PDU_CONFIRM_ACTIVE = 3,
    RDP_PDU_REDIRECT = 4,
    RDP_PDU_DEACTIVATE = 6,
    RDP_PDU_DATA = 7
};

/* Actual 16 bit PDU type field is (lsb->msb) 4 bites type + 4 bits versionLow + 8bit version high
 * version Low must be 1, and versioHight must be 0.
 */
#define VRDP_MAKE_PDU_TYPE(a) ((uint16_t)((a) | 0x10))

enum RDP_DATA_PDU_TYPE
{
    RDP_DATA_PDU_UPDATE = 2,
    RDP_DATA_PDU_CONTROL = 20,
    RDP_DATA_PDU_POINTER = 27,
    RDP_DATA_PDU_INPUT = 28,
    RDP_DATA_PDU_SYNCHRONISE = 31,
    RDP_DATA_PDU_REFRESH_RECT = 33, /* MS Client sends this when switching from/to fullscreen. */
    RDP_DATA_PDU_BELL = 34,
    RDP_DATA_PDU_SUPPRESS_OUTPUT = 35,
    RDP_DATA_PDU_CLOSE = 36,
    RDP_DATA_PDU_LOGON = 38,
    RDP_DATA_PDU_FONT2 = 39,
    RDP_DATA_PDU_DISCONNECT = 47,
    RDP_DATA_PDU_MONITOR_LAYOUT_PDU = 55
};

/* RDP_DATA_PDU_SYNCHRONISE messageType */
#define SYNCMSGTYPE_SYNC 1

enum RDP_UPDATE_PDU_TYPE
{
    RDP_UPDATE_ORDERS = 0,
    RDP_UPDATE_BITMAP = 1,
    RDP_UPDATE_PALETTE = 2,
    RDP_UPDATE_SYNCHRONIZE = 3
};

enum RDP_CONTROL_PDU_TYPE
{
    RDP_CTL_REQUEST_CONTROL = 1,
    RDP_CTL_GRANT_CONTROL = 2,
    RDP_CTL_DETACH = 3,
    RDP_CTL_COOPERATE = 4
};

#define ROP2_COPY       0xc
#define ROP2_XOR        0x6
#define ROP2_AND        0x8
#define ROP2_NXOR       0x9
#define ROP2_OR         0xe

#define RDP_ORDER_STANDARD   0x01
#define RDP_ORDER_SECONDARY  0x02
#define RDP_ORDER_BOUNDS     0x04
#define RDP_ORDER_CHANGE     0x08
#define RDP_ORDER_DELTA      0x10
#define RDP_ORDER_LASTBOUNDS 0x20
#define RDP_ORDER_SMALL      0x40
#define RDP_ORDER_TINY       0x80

/* Indexes in the au8SupportedOrders array of ORDERS capability set.
 * Not always equal to the order number.
 */
enum RDP_ORDER_NEG_INDEX
{
    RDP_ORDER_NEG_INDEX_DESTBLT            = 0,
    RDP_ORDER_NEG_INDEX_PATBLT             = 1,
    RDP_ORDER_NEG_INDEX_SCREENBLT          = 2,
    RDP_ORDER_NEG_INDEX_MEMBLT             = 3,
    RDP_ORDER_NEG_INDEX_TRIBLT             = 4,
    RDP_ORDER_NEG_INDEX_5                  = 5, /* not used */
    RDP_ORDER_NEG_INDEX_6                  = 6, /* not used */
    RDP_ORDER_NEG_INDEX_DRAWNINEGRID       = 7,
    RDP_ORDER_NEG_INDEX_LINE               = 8,
    RDP_ORDER_NEG_INDEX_MULTI_DRAWNINEGRID = 9,
    RDP_ORDER_NEG_INDEX_RECT               = 10,
    RDP_ORDER_NEG_INDEX_DESKSAVE           = 11,
    RDP_ORDER_NEG_INDEX_12                 = 12, /* not used */
    RDP_ORDER_NEG_INDEX_13                 = 13, /* not used */
    RDP_ORDER_NEG_INDEX_14                 = 14, /* not used */
    RDP_ORDER_NEG_INDEX_MULTI_DESTBLT      = 15,
    RDP_ORDER_NEG_INDEX_MULTI_PATBLT       = 16,
    RDP_ORDER_NEG_INDEX_MULTI_SCREENBLT    = 17,
    RDP_ORDER_NEG_INDEX_MULTI_RECT         = 18,
    RDP_ORDER_NEG_INDEX_FAST_INDEX         = 19,
    RDP_ORDER_NEG_INDEX_POLYGON            = 20,
    RDP_ORDER_NEG_INDEX_POLYGON2           = 21,
    RDP_ORDER_NEG_INDEX_POLYLINE           = 22,
    RDP_ORDER_NEG_INDEX_23                 = 23, /* not used */
    RDP_ORDER_NEG_INDEX_FAST_GLYPH         = 24,
    RDP_ORDER_NEG_INDEX_ELLIPSE            = 25,
    RDP_ORDER_NEG_INDEX_ELLIPSE2           = 26,
    RDP_ORDER_NEG_INDEX_TEXT2              = 27,
    RDP_ORDER_NEG_INDEX_28                 = 28, /* not used */
    RDP_ORDER_NEG_INDEX_29                 = 29, /* not used */
    RDP_ORDER_NEG_INDEX_30                 = 30, /* not used */
    RDP_ORDER_NEG_INDEX_31                 = 31  /* not used */
};

enum RDP_ORDER_TYPE
{
    RDP_ORDER_TYPE_DESTBLT            = 0,
    RDP_ORDER_TYPE_PATBLT             = 1,
    RDP_ORDER_TYPE_SCREENBLT          = 2,
    RDP_ORDER_TYPE_3                  = 3, /* not used */
    RDP_ORDER_TYPE_4                  = 4, /* not used */
    RDP_ORDER_TYPE_5                  = 5, /* not used */
    RDP_ORDER_TYPE_6                  = 6, /* not used */
    RDP_ORDER_TYPE_DRAWNINEGRID       = 7,
    RDP_ORDER_TYPE_MULTI_DRAWNINEGRID = 8,
    RDP_ORDER_TYPE_LINE               = 9,
    RDP_ORDER_TYPE_RECT               = 10,
    RDP_ORDER_TYPE_DESKSAVE           = 11,
    RDP_ORDER_TYPE_12                 = 12, /* not used */
    RDP_ORDER_TYPE_MEMBLT             = 13,
    RDP_ORDER_TYPE_TRIBLT             = 14,
    RDP_ORDER_TYPE_MULTI_DESTBLT      = 15,
    RDP_ORDER_TYPE_MULTI_PATBLT       = 16,
    RDP_ORDER_TYPE_MULTI_SCREENBLT    = 17,
    RDP_ORDER_TYPE_MULTI_RECT         = 18,
    RDP_ORDER_TYPE_FAST_INDEX         = 19,
    RDP_ORDER_TYPE_POLYGON            = 20,
    RDP_ORDER_TYPE_POLYGON2           = 21,
    RDP_ORDER_TYPE_POLYLINE           = 22,
    RDP_ORDER_TYPE_23                 = 23, /* not used */
    RDP_ORDER_TYPE_FAST_GLYPH         = 24,
    RDP_ORDER_TYPE_ELLIPSE            = 25,
    RDP_ORDER_TYPE_ELLIPSE2           = 26,
    RDP_ORDER_TYPE_TEXT2              = 27,
    RDP_ORDER_TYPE_28                 = 28, /* not used */
    RDP_ORDER_TYPE_29                 = 29, /* not used */
    RDP_ORDER_TYPE_30                 = 30, /* not used */
    RDP_ORDER_TYPE_31                 = 31  /* not used */
};

enum RDP_SECONDARY_ORDER_TYPE
{
    RDP_ORDER_RAW_BMPCACHE = 0,
    RDP_ORDER_COLCACHE = 1,
    RDP_ORDER_BMPCACHE = 2,
    RDP_ORDER_FONTCACHE = 3,
    RDP_ORDER_RAW_BMPCACHE2 = 4,
    RDP_ORDER_BMPCACHE2 = 5,
    RDP_ORDER_BRUSHCACHE = 7
};

#define RDP_CAP_BRUSH_DEFAULT    0x00000000
#define RDP_CAP_BRUSH_COLOR_8x8  0x00000001
#define RDP_CAP_BRUSH_COLOR_FULL 0x00000002

#define RDP_BRUSH_FMT_1BPP 0x01

typedef struct _VRDPBUFFER
{
    const void *pv;
    uint32_t cb;
} VRDPBUFFER;

/* Abstract desktop class */
enum RDP_INPUT_DEVICE
{
    RDP_INPUT_SYNCHRONIZE = 0,
    RDP_INPUT_CODEPOINT = 1,
    RDP_INPUT_VIRTKEY = 2,
    RDP_INPUT_SCANCODE = 4,
    RDP_INPUT_MOUSE = 0x8001
};

/* Device flags */
#define KBD_FLAG_RIGHT          0x0001
#define KBD_FLAG_EXT            0x0100
#define KBD_FLAG_EXT2           0x0200
#define KBD_FLAG_QUIET          0x1000
#define KBD_FLAG_DOWN           0x4000
#define KBD_FLAG_UP             0x8000

/* These are for synchronization; not for keystrokes */
#define KBD_FLAG_SCROLL   0x0001
#define KBD_FLAG_NUMLOCK  0x0002
#define KBD_FLAG_CAPITAL  0x0004

#define MOUSE_FLAG_MOVE         0x0800
#define MOUSE_FLAG_BUTTON1      0x1000
#define MOUSE_FLAG_BUTTON2      0x2000
#define MOUSE_FLAG_BUTTON3      0x4000
#define MOUSE_FLAG_BUTTON4      0x0280
#define MOUSE_FLAG_BUTTON5      0x0380
#define MOUSE_FLAG_DOWN         0x8000

#define MOUSE_FLAG_WHEEL_MASK   0x0300

class VRDPClient;
class VRDPBitmapCompressed;

class VRDPDesktop
{

    public:

    VRDPDesktop () {};
    virtual ~VRDPDesktop () {};

    typedef struct _InputMsg
    {
        uint32_t time;
        uint16_t message_type;
        uint16_t device_flags;
        uint16_t param1;
        uint16_t param2;
    } InputMsg;

    virtual void ProcessInput (unsigned uScreenId, InputMsg *pmsg) = 0;

    virtual int ClientAttach (VRDPClient *pClient, bool fAttached) = 0;
    virtual int ClientLogon (VRDPClient *pClient, const char *pszUser, const char *pszPassword, const char *pszDomain) = 0;
    virtual int ClientLocationInfo (VRDPClient *pClient,const char *pszCName, const char *pszCIPA, const char *pszCLocation, const char *pszCOtherInfo) = 0;
    virtual int InterceptChannel (VRDPClient *pClient, uint32_t fu32Intercept, void **ppvIntercept) = 0;

    virtual void NotifyClientResolution (int w, int h, unsigned uScreenId) = 0;

    virtual void ClientConnect (VRDPClient *pClient) = 0;
    virtual void ClientDisconnect (VRDPClient *pClient) = 0;
};

class VRDPTP;

class VRDPChannel: public VRDPOutputCtx
{
    private:
        uint8_t *m_buf;
        uint32_t m_length;
        uint32_t m_size;

        VRDPLock *m_pChannelLock;

    public:
        VRDPChannel ();
        virtual ~VRDPChannel ();

        virtual int ProcessChannelInput (const uint8_t *pu8Input, uint32_t u32Length) = 0;

        void DefragmentationBegin (uint32_t length);
        void DefragmentationAdd (VRDPInputCtx *pInputCtx);
        const uint8_t *DefragmentationEnd (uint32_t *pu32Length);
        void DefragmentationRelease (void);

        bool ChannelLock (void);
        void ChannelUnlock (void);
};

typedef struct _VRDPClipboardData
{
    uint32_t u32Function;
    uint32_t u32Format;
    void     *pvData;
    uint32_t cbData;
} VRDPClipboardData;


/* The buffer duration (8192/BytesPerSample)/Freq must be less than the audio chunk period */
#define VRDP_AUDIO_OUTPUT_BLOCK_SIZE 8192
#define VRDP_AUDIO_OUTPUT_BLOCKS     16

class VRDPChannelAudio: public VRDPChannel
{
    private:
        VRDPTP *m_pvrdptp;

        uint32_t m_channelOptions;
        uint16_t m_channelId;

        bool m_fOperational;
        bool m_fSetup;
        bool m_fAccumulating;

        /* Which is to be sent, but not sent yet. */
        uint8_t m_u8PacketIdx;
        /* Which was sent as last one. */
        uint8_t m_u8PacketIdxLast;
        /* Last completed. */
        uint8_t m_u8PacketIdxLastCompleted;
        /* Send a close command after the completion of the m_u8PacketIdxLast packet. */
        bool m_fSendClose;

        /* Skip audio packets because the client audio queue increased. */
        int32_t volatile m_cPacketsToSkip;

        /* Client audio queue monitoring. */
        struct AudioClientQueue
        {
            uint32_t aDiffSamples[8];
            int iNextDiffSample;
            int cDiffSamples;
            uint32_t u32Diff;
            uint32_t u32QueueLimit;
        } m_clientQueue;

        /* The output PCM data ring buffer. */
        uint8_t m_au8OutputBuffer[VRDP_AUDIO_OUTPUT_BLOCK_SIZE * VRDP_AUDIO_OUTPUT_BLOCKS];

        /* Where to write new data in the output buffer. */
        uint32_t m_u32OutputTail;

        /* Where data to be sent is in the output buffer. Always on blocks boundary.*/
        uint32_t m_u32OutputHead;

        uint32_t m_u32TimeRefPosition;
        uint64_t m_u64TimeRefNanoTS;

        void *m_pLogCtx;

        int processNegotiateResponse (const uint8_t *pu8Input, uint32_t u32Length);

        void sendWrite (bool fEnd);
        void sendClose (void);

        int sendBuffers (int cBuffers, const VRDPBUFFER *paBuffers);

    public:
        VRDPChannelAudio (VRDPTP *pvrdptp);
        virtual ~VRDPChannelAudio ();

        uint32_t Id (void) { return m_channelId; };

        int Open (uint32_t options, uint16_t id);
        void Setup (void);
        void Close (void);

        void Start (void);

        virtual int ProcessChannelInput (const uint8_t *pu8Input, uint32_t u32Length);

        void Send (const st_sample_t *paSamples, int cSamples, bool fFinished, uint64_t u64SamplesStartNanoTS);

        void SendSetVolume (uint16_t left, uint16_t right);

        void Completion (uint8_t u8PacketIdx);
};

class VRDPServer;

class VRDPChannelClipboard: public VRDPChannel
{
    private:
        VRDPTP *m_pvrdptp;

        uint32_t m_channelOptions;
        uint16_t m_channelId;

        bool m_fOperational;
        bool m_fSetup;

        void *m_pvCallbackUser;

        /* The format the server requested from the client. */
        uint32_t m_u32RequestedFormat;
        /* The client side identifier of the HTML format. */
        uint32_t m_u32FormatHTML;

        uint64_t m_u64AnnounceTime;
        uint32_t m_u32AnnouncedFormats;

        VRDPLock  *m_pLockClipboardData;
        uint32_t  m_u32ClipboardDataVRDPFormat;
        void     *m_pvClipboardData;
        uint32_t  m_cbClipboardData;

        uint64_t m_u64ServerAnnouncedTime;

        void clipboardDataCache (uint32_t u32Format, const void *pvData, uint32_t cbData);

        VRDPServer *Server (void);

    public:
        VRDPChannelClipboard (VRDPTP *pvrdptp);
        virtual ~VRDPChannelClipboard ();

        uint32_t Id (void) { return m_channelId; };

        int Open (uint32_t options, uint16_t id);
        void Setup (void);
        void Close (void);

        void Start (void);

        virtual int ProcessChannelInput (const uint8_t *pu8Input, uint32_t u32Length);

        int SendData (const void *pv, uint32_t cb);
        int SendPacket (const void *pv, uint32_t cb);

        void QueryClipboardFormats (volatile uint32_t *pu32AnnouncedFormats, volatile uint64_t *pu64AnnounceTime);
        bool QueryClipboardData (uint32_t u32Format, void *pvData, uint32_t cbData, uint32_t *pcbActualRead);
        void DiscardClipboardData (void);
};

class VRDPChannelUSB: public VRDPChannel
{
    private:
        VRDPTP *m_pvrdptp;

        uint32_t m_channelOptions;
        uint16_t m_channelId;

        bool m_fOperational;
        bool m_fSetup;

        void *m_pvCallbackUser;

        void initMembers (void);

    public:
        VRDPChannelUSB (VRDPTP *pvrdptp);
        virtual ~VRDPChannelUSB ();

        uint32_t Id (void) { return m_channelId; };

        int Open (uint32_t options, uint16_t id);
        void Setup (void);
        void Close (void);

        void Start (void);

        virtual int ProcessChannelInput (const uint8_t *pu8Input, uint32_t u32Length);

        int Send (void *pvParm, uint32_t cbParm);
};

typedef void FNDVCCALLBACK(void *pvCtx, uint32_t u32Event, const void *pvData, uint32_t cbData);
typedef FNDVCCALLBACK *PFNDVCCALLBACK;

#define DVC_EVENT_CREATE 0
#define DVC_EVENT_DATA   1
#define DVC_EVENT_CLOSE  2

typedef struct DVCData
{
    /* Variables set at the channel registration. */
    PFNDVCCALLBACK pfnDVCCallback;
    void    *pvDVCCallback;
    char    *pszChannelName;
    uint8_t  u8ChannelId;

    /* Channel status. */
    uint32_t volatile u32ChannelStatus;  /* DVC_CH_EMPTY means the channel entry is not used. */

    /* INPUT thread variables. */
    bool fFragmentedInput;

    uint8_t *pu8FragmentedData;
    uint32_t cbFragmentedLength;

    uint8_t *pu8FragmentedDataCurrent;
    uint32_t cbFragmentedRemaining;

} DVCData;

class VRDPChannelDVC: public VRDPChannel
{
    private:
        VRDPTP *m_pvrdptp;

        uint32_t m_channelOptions;
        uint16_t m_channelId;

        bool m_fOperational;
        bool m_fSetup;

        DVCData m_channels[254]; /* 8bit channel id 1..254, 0 and 255 are special values. */

        void initMembers (void);
        int sendBuffers (int cBuffers, const VRDPBUFFER *paBuffers);
        int sendCreateChannel (const DVCData *pData);

        void createChannels (void);

    public:
        VRDPChannelDVC (VRDPTP *pvrdptp);
        virtual ~VRDPChannelDVC ();

        uint32_t Id (void) { return m_channelId; };

        int Open (uint32_t options, uint16_t id);
        void Setup (void);
        void Close (void);

        void Start (void);

        virtual int ProcessChannelInput (const uint8_t *pu8Input, uint32_t u32Length);

        int SendData (uint8_t u8ChannelId, void *pvData, uint32_t cbData);
        int SendDataBuffers(uint8_t u8ChannelId, VRDPBUFFER *paBuffers, int cBuffers);
        int SendCloseChannel (uint8_t u8ChannelId);

        int ProcessOutput (const void *pvData, uint32_t cbData);

        int RegisterChannel (const char *pszChannelName, PFNDVCCALLBACK pfnDVCCallback, void *pvDVCCallback, uint8_t *pu8ChannelId);
};


#define SUNFLSH_EVENT_CREATE 0
#define SUNFLSH_EVENT_DATA   1
#define SUNFLSH_EVENT_CLOSE  2

typedef void FNSUNFLSHCALLBACK(void *pvCtx, uint32_t u32Event, const void *pvData, uint32_t cbData);
typedef FNSUNFLSHCALLBACK *PFNSUNFLSHCALLBACK;

class VRDPChannelSunFlsh: public VRDPChannel
{
    private:
        VRDPTP *m_pvrdptp;

        uint32_t m_channelOptions;
        uint16_t m_channelId;

        bool m_fOperational;
        bool m_fSetup;

        PFNSUNFLSHCALLBACK m_pfnSunFlshCallback;
        void *m_pvSunFlshCallback;

        void initMembers (void);
        int sendBuffers (int cBuffers, const VRDPBUFFER *paBuffers);

    public:
        VRDPChannelSunFlsh (VRDPTP *pvrdptp);
        virtual ~VRDPChannelSunFlsh ();

        uint32_t Id (void) { return m_channelId; };

        int Open (uint32_t options, uint16_t id);
        void Setup (void);
        void Close (void);

        void Start (void);

        virtual int ProcessChannelInput (const uint8_t *pu8Input, uint32_t u32Length);

        int SendData (VRDPBUFFER *paBuffers, int cBuffers);

        int ProcessOutput (const void *pvData, uint32_t cbData);

        int RegisterCallback (PFNSUNFLSHCALLBACK pfnSunFlshCallback, void *pvSunFlshCallback);
};

typedef struct DEVICE_ANNOUNCE DEVICE_ANNOUNCE;

class VRDPChannelRDPDR: public VRDPChannel
{
    private:
        VRDPTP *m_pvrdptp;

        uint32_t m_channelOptions;
        uint16_t m_channelId;

        bool m_fOperational;
        bool m_fSetup;

        uint32_t m_u32ClientId;
        bool m_fSmartCard;

        uint32_t m_u32IoCode1;
        uint32_t m_u32ExtendedPDU;
        uint32_t m_u32ExtraFlags1;

        VRDPLock *m_pLock;

        struct
        {
            uint32_t volatile u32IdSrc;
            uint32_t cIds;
            RTLISTANCHOR ListFree;
            RTLISTANCHOR ListUsed;
        } m_IOCompletion;

        struct
        {
            bool fEnabled;
            uint32_t u32DeviceId;
        } m_smartcard;

        void initMembers (void);
        int sendBuffers (int cBuffers, const VRDPBUFFER *paBuffers);
        int postOutput (RDPDRPKTHDR *pHdr, uint32_t cbData);

        int createIO(uint32_t *pu32CompletionId, RDPDRPKTHDR *pHdr, uint32_t u32MajorFunction, uint32_t u32DeviceId);
        int fetchIO(uint32_t u32CompletionId, RDPDRPKTHDR **ppHdr, uint32_t *pu32MajorFunction, uint32_t *pu32DeviceId);
        void cancelIOForDevice(uint32_t u32DeviceId);

        int rdpdrSendServerCoreCapability(void);
        int rdpdrSendServerClientIdConfirm(void);
        int rdpdrParseCoreClientCapability(const uint8_t *pu8Input, uint32_t u32Length);
        int rdpdrParseClientDeviceListAnnounce(const uint8_t *pu8Input, uint32_t u32Length);
        int rdpdrParseDeviceIOCompletion(const uint8_t *pu8Input, uint32_t u32Length);

        int rdpdrDispatchIOCompletion(RDPDRIOCTL *pIO, uint32_t u32DeviceId, uint32_t u32IoStatus);

        int rdpdrOnDeviceAdd(const DEVICE_ANNOUNCE *pDevHdr,
                             const uint8_t *pu8DeviceData,
                             uint32_t *pu32Status);

    public:
        VRDPChannelRDPDR (VRDPTP *pvrdptp);
        virtual ~VRDPChannelRDPDR ();

        uint32_t Id (void) { return m_channelId; };

        int Open (uint32_t options, uint16_t id);
        void Setup (void);
        void Close (void);

        void Start (void);

        virtual int ProcessChannelInput (const uint8_t *pu8Input, uint32_t u32Length);

        int SendData (VRDPBUFFER *paBuffers, int cBuffers);

        int ProcessOutput (const void *pvData, uint32_t cbData);
};

typedef struct _VRDPCAPABILITIES
{
    struct
    {
        uint16_t u16CacheGranularityX;
        uint16_t u16CacheGranularityY;
        uint16_t u16MaxOrderLevel;
        uint16_t u16NumberOfFonts;
        uint16_t u16CapabilityFlags;
        uint8_t  au8SupportedOrders[32];
        uint16_t u16TextCapabilityFlags;
        uint32_t u32DesktopCacheSize;
    } order;
} VRDPCAPABILITIES;

typedef struct VRDPSAVEDINTERNALORDER
{
    struct VRDPSAVEDINTERNALORDER *next;
    int32_t  i32Op;
    uint32_t cbInternalOrder;
    /* The internal order parameters follow. */
} VRDPSAVEDINTERNALORDER;

/* VRDP can work in different shadowBuffer->clientDesktop mapping modes:
 *
 * 1) Identical - no translation, guest coordinates == client coordinates;
 *    used if the client supports resizing and therefore always has the same size
 *    as the shadow buffer (rdesktop, may be MSFT with MONITOR_LAYOUT PDU).
 *    In this mode the size of the guest determines, which size the client will have.
 *    * Enabled using a VRDE property.
 * 2) SingleMonitor - the client is associated with exactly one guest monitor,
 *    and the size of the client is not changeable. If the shadow buffer is too small, then
 *    it is centered, if it is too large, then either cut or scaled.
 *    * Enabled by default.
 * 3) TSMultipleMonitors - the client will display all shadow buffers.
 *    * The mode is always enabled if client reports multiple monitors and supports MONITOR_LAYOUT
 *
 * The VRDPTP layer selects one of mapping modes. The mode specific logic is encapsulated
 * in the VRDPClientDesktopMap interface.
 */
#define VRDP_CLIENT_DESKTOP_MAP_IDENTICAL      0
#define VRDP_CLIENT_DESKTOP_MAP_SINGLE_MONITOR 1
#define VRDP_CLIENT_DESKTOP_MAP_MULTI_MONITOR  2

class VRDPClientDesktopMap
{
    private:
        VRDPTP *m_pTP;
        RGNRECT m_rectClient;

        int initializeDesktopMap(void);

    public:
        VRDPClientDesktopMap(VRDPTP *pTP);
        virtual ~VRDPClientDesktopMap();

        static int CreateDesktopMap(VRDPClientDesktopMap **ppMap, VRDPTP *pTP, unsigned uScreenId);
        VRDPTP *TP(void) { return m_pTP; }

        /* The screen resolution has been changed and the client may change the resolution as well. */
        void Reinitialize (void);

        /* The resolution of entire client desktop. In case of TS multiple monitor mode the
         * desktop includes all monitors.
         */
        void QueryClientRect (RGNRECT *pClient);
        uint16_t GetClientWidth (void) { return (uint16_t)m_rectClient.w; }
        uint16_t GetClientHeight (void) { return (uint16_t)m_rectClient.h; }

        /* Reinitialize after a resolution change. */
        virtual void OnResolutionChanged(void) = 0;

        /* The client desktop rect as required by the server. */
        virtual void CalculateClientRect(RGNRECT *pRect) = 0;

        /* Query the rectangle of a particular client monitor in the client coords. */
        virtual void QueryClientMonitorRect (unsigned uScreenId, RGNRECT *pRect) = 0;

        virtual bool IsScreenMatched (unsigned uScreenId) = 0;

        /* Returns VRDP_CLIENT_DESKTOP_MAP_* */
        virtual int DesktopMode(void) = 0;

        /* Convert a rectangle in client coordinates to the rectangle which is clipped by the uScreenId monitor.
         * Resulting rectangle is in client coords.
         */
        virtual void Client2Client (unsigned uScreenId, const RGNRECT *pRectClient, RGNRECT *pRectResult) = 0;

        /* Convert a rectangle in client coordinates to the rectangle which is clipped by
         * the uScreenId shadowbuffer. Resulting rectangle is in the shadow buffer coords.
         */
        virtual void Client2Screen (unsigned uScreenId, const RGNRECT *pRectClient, RGNRECT *pRectResult) = 0;

        virtual void Client2ScreenPoint(unsigned *puScreenId, int16_t *pi16x, int16_t *pi16y) = 0;

        virtual void Screen2Client (unsigned uScreenId, const RGNRECT *pRectScreen, RGNRECT *pRectResult) = 0;

        virtual void Screen2ClientPoint (unsigned uScreenId, int16_t *pi16x, int16_t *pi16y) = 0;

        /* Intersect a rectangle in screen coords with the shadow buffer rectangle.
         * Result is in client coords.
         * Returns 'false' if the resulting rectangle is empty.
         */
        virtual bool ClipScreen2Client (unsigned uScreenId, const RGNRECT *pRectScreen, RGNRECT *pRectResult) = 0;
};

class VRDPTP
{
    private:
        SECTP m_sectp;

        VRDPDesktop *m_pdesktop;

        enum VRDP_RDP_Status {
           VRDP_RDP_Status_RecvConnectInitial,
           VRDP_RDP_Status_RecvLogonInfo,
           VRDP_RDP_Status_RDP
        };

        enum VRDP_RDP_Status m_enmStatus;

        VRDPClient *m_pClient;

        uint32_t m_keylayout;
        uint32_t m_clientbuild;

        uint32_t m_flags;

        uint32_t m_numchannels;

        char *m_domain;
        char *m_username;
        char *m_password;
        char *m_program;
        char *m_directory;
        char *m_clientip;
        char *m_dllname;
        char m_client[128];

        int32_t m_timezone;
        uint32_t m_performanceflags;

        uint16_t m_bppcode;
        uint8_t m_bpp;
        unsigned m_white;  /* White pixel value for m_bpp. */
        bool m_fBitmapCompression;
        bool m_fBitmapCaching;

        /* The client will accept the RDP5 short packets. */
        bool m_fRDP5Packets;

        VRDPCAPABILITIES m_caps;

        /* Last sent orders are remembered and only change is sent. */
        RDPSAVEDORDERS m_savedOrders;

        /* Accessed only from OUTPUT thread. */
        bool m_fSeqInProcess;
        VRDPSAVEDINTERNALORDER *m_pSavedInternalOrders;

        uint32_t m_pointerCacheSize;

        uint32_t m_u32ClientClusterFlags;

        uint16_t m_userid;

        bool m_fClientResolution;
        uint32_t m_u32BrushSupportLevel;

        unsigned m_uScreenId;

#ifdef VRDPSTAT
        uint64_t m_bmpSquare;
        uint64_t m_bmpCount;
        uint64_t m_bmpBytes;
        uint64_t m_orderBytes;
        uint64_t m_dataBytes;
        uint16_t m_bmpSizes[65536];
        int m_cCached;
        int m_cCachedTiles;
        int m_cMemblt;
        int m_cMembltSquare;
#endif /* VRDPSTAT */

        VRDPChannelAudio m_audio;
        VRDPChannelUSB m_usb;
        VRDPChannelClipboard m_clipboard;
        VRDPChannelDVC m_dvc;
        VRDPChannelSunFlsh m_sunflsh;
        VRDPChannelRDPDR m_rdpdr;

        /* The RDP connection parameters. */
        VRDPData m_data;

        VRDPClientDesktopMap *m_pDesktopMap;

        int outSaveScreenOrder (VRDPStream *pStream, VRDEORDERAREA *pArea, uint8_t restore, uint32_t offset);
        int outText2 (unsigned uScreenId,
                      VRDPStream *pStream,
                      uint8_t u8RDPFontHandle,
                      uint8_t u8Flags,
                      uint8_t u8CharInc,
                      uint32_t rgbFG,
                      uint32_t rgbBG,
                      const VRDEORDERRECT *pBkGround,
                      const VRDEORDERRECT *pOpaque,
                      const VRDEORDERPOINT *pOrigin,
                      uint8_t u8TextLength,
                      const uint8_t *pu8Text);
        int outMemblt (unsigned uScreenId, VRDPStream *pStream, uint16_t u16CacheId, uint16_t u16Idx,
                       uint16_t u16DstX, uint16_t u16DstY,
                       uint16_t u16Width, uint16_t u16Height,
                       uint16_t u16SrcX, uint16_t u16SrcY, uint8_t u8ROP);

        void ResetSavedOrders (void);

        int RecvConnectInitial (VRDPInputCtx *pInputCtx);
        int SendConnectResponse (VRDPInputCtx *pInputCtx, uint8_t u8Result);
        int RecvLogonInfo (VRDPInputCtx *pInputCtx);
        int RecvRDP5 (VRDPInputCtx *pInputCtx);

        int SendPDU (VRDPInputCtx *pInputCtx, uint16_t u16Type);
        int SendDeactivate (VRDPInputCtx *pInputCtx);
        int SendData (VRDPInputCtx *pInputCtx, uint8_t u8Type);
        int SendControl (VRDPInputCtx *pInputCtx, uint16_t u16Action);

        int ProcessConfirmActivePDU (VRDPInputCtx *pInputCtx);

        int ProcessDataPDUSynchronise (VRDPInputCtx *pInputCtx);
        int ProcessDataPDURefreshRect (VRDPInputCtx *pInputCtx);
        int ProcessDataPDUSuppressOutput (VRDPInputCtx *pInputCtx);
        int ProcessDataPDUControl (VRDPInputCtx *pInputCtx);
        int ProcessDataPDUInput (VRDPInputCtx *pInputCtx);
        int ProcessDataPDUFont2 (VRDPInputCtx *pInputCtx);

        int RegisterChannel (const char *pszName, uint32_t u32Options, uint16_t u16ChannelId);

        int CheckLogon (void);

        void InitMembers (void);
        void DestroyMembers (void);

    public:
        VRDPTP (VRDPDesktop *pdesktop, VRDPClient *pClient);
        ~VRDPTP (void);

        uint8_t QueryClientBpp (void) { return m_bpp; };
        uint32_t PointerCacheSize (void) { return m_pointerCacheSize; };

        VRDPClient *Client (void) { return m_pClient; };

        bool AreRDP5PacketsEnabled (void) { return m_fRDP5Packets; };

        int SendLicense (VRDPInputCtx *pInputCtx);
        int SendDemandActive (VRDPInputCtx *pInputCtx);

        int Recv (VRDPInputCtx *pInputCtx);

        int ProcessChannel (VRDPInputCtx *pInputCtx, uint32_t u32Length, uint32_t u32Flags);

        void Disconnect (VRDPInputCtx *pInputCtx, uint32_t u32Reason);

        void Reset (void);

        void Reconnect (VRDPInputCtx *pInputCtx);
        void Redirect (VRDPInputCtx *pInputCtx,
                       uint32_t u32Flags,
                       const char *pszServer,
                       const char *pszUser,
                       const char *pszDomain,
                       const char *pszPassword,
                       uint32_t u32SessionId,
                       const char *pszCookie);

        int SendToChannel (VRDPOutputCtx *pOutputCtx, uint16_t u16ChannelId, uint32_t u32Options,
                           int cBuffers, const VRDPBUFFER *paBuffers, bool fInsertLengthField);

        int OutputBitmap2 (unsigned uScreenId, VRDPStream *pStream, VRDPBitmapCompressed *pBmp, const int16_t x, const int16_t y);
        int OutputOrder (unsigned uScreenId, VRDPStream *pStream, int32_t i32Op, void *pvOrder);

        bool OutputFontCache (VRDPStream *pStream, const TCFONTTEXT2 *pFontText2, const uint8_t *pau8Indexes, int cIndexes);

        void OutputPalette (VRDPStream *pStream);
        void OutputKeepAlive (VRDPStream *pStream);

        void OutputPointerColor (VRDPServer *pServer, VRDPStream *pStream, uint32_t u32ServerId, uint16_t u16RemoteId);
        void OutputPointerCached (VRDPStream *pStream, uint16_t u16RemoteId);

        bool OutputBitmapCache (VRDPStream *pStream, VRDPBitmapCompressed *pBmp, uint16_t u16TileX, uint16_t u16TileY, uint16_t u16CacheId, uint16_t u16Idx);
        bool OutputBrushCache (VRDPStream *pStream, uint8_t u8CacheEntry, uint8_t u8Width, uint8_t u8Height, uint8_t *au8Pattern, size_t cbPattern);

        bool OutputFrameMarker(VRDPStream *pStream, uint32_t u32Action);

        int Send (bool fRDP5, VRDPCtx *pCtx);
        int SendMonitorLayout (VRDPInputCtx *pInputCtx);

        VRDPChannelAudio *Audio (void) { return &m_audio; };
        VRDPChannelUSB *USB (void) { return &m_usb; };
        VRDPChannelClipboard *Clipboard (void) { return &m_clipboard; };
        VRDPChannelDVC *DVC (void) { return &m_dvc; };
        VRDPChannelSunFlsh *SunFlsh (void) { return &m_sunflsh; };
        VRDPChannelRDPDR *RDPDR (void) { return &m_rdpdr; };

        VRDPDesktop *Desktop (void) { return m_pdesktop; };

        bool IsClientResolution (void) { return m_fClientResolution; };

        /** @todo review usage. TS multimonitor processes all screens. */
        unsigned QueryScreenId (void) { return m_uScreenId; };

        uint32_t GetSupportedOrders (void);

        /* Access to various information about the connection. */
        const char *getUser (void)       { return m_username? m_username: ""; };
        const char *getDomain (void)     { return m_domain?   m_domain:   ""; };
        const char *getClientName (void) { return &m_client[0]; };
        const char *getClientIP (void)   { return m_clientip? m_clientip: ""; };
        uint32_t getClientVersion (void) { return m_clientbuild; };
        void setClientName(const char *pszName);

        bool IsBitmapCompressionSupported (void) { return m_fBitmapCompression; }
        uint32_t BrushSupportLevel(void) { return m_u32BrushSupportLevel; }

        VRDPData *Data(void) { return &m_data; }
        VRDPClientDesktopMap *DesktopMap(void) { return m_pDesktopMap; }
};

#endif /* !VRDP_INCLUDED_SRC_vrdp_h */
