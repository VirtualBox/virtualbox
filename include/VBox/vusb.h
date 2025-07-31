/** @file
 * VUSB - VirtualBox USB. (DEV,VMM)
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef VBOX_INCLUDED_vusb_h
#define VBOX_INCLUDED_vusb_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <VBox/types.h>
#include <iprt/assert.h>

struct PDMLED;

RT_C_DECLS_BEGIN

/** @defgroup grp_vusb  VBox USB API
 * @{
 */

/** @defgroup grp_vusb_std  Standard Stuff
 * @{ */

/** Frequency of USB bus (from spec). */
#define VUSB_BUS_HZ                 12000000


/** @name USB Descriptor types (from spec)
 * @{ */
#define VUSB_DT_DEVICE                  0x01
#define VUSB_DT_CONFIG                  0x02
#define VUSB_DT_STRING                  0x03
#define VUSB_DT_INTERFACE               0x04
#define VUSB_DT_ENDPOINT                0x05
#define VUSB_DT_DEVICE_QUALIFIER        0x06
#define VUSB_DT_OTHER_SPEED_CFG         0x07
#define VUSB_DT_INTERFACE_POWER         0x08
#define VUSB_DT_INTERFACE_ASSOCIATION   0x0B
#define VUSB_DT_BOS                     0x0F
#define VUSB_DT_DEVICE_CAPABILITY       0x10
#define VUSB_DT_SS_ENDPOINT_COMPANION   0x30
/** @} */

/** @name USB Descriptor minimum sizes (from spec)
 * @{ */
#define VUSB_DT_DEVICE_MIN_LEN          18
#define VUSB_DT_CONFIG_MIN_LEN          9
#define VUSB_DT_CONFIG_STRING_MIN_LEN   2
#define VUSB_DT_INTERFACE_MIN_LEN       9
#define VUSB_DT_ENDPOINT_MIN_LEN        7
#define VUSB_DT_SSEP_COMPANION_MIN_LEN  6
/** @} */

/** @name USB Device Capability Type Codes (from spec)
 * @{ */
#define VUSB_DCT_WIRELESS_USB           0x01
#define VUSB_DCT_USB_20_EXTENSION       0x02
#define VUSB_DCT_SUPERSPEED_USB         0x03
#define VUSB_DCT_CONTAINER_ID           0x04
/** @} */


#pragma pack(1) /* ensure byte packing of the descriptors. */

/**
 * USB language id descriptor (from specs).
 */
typedef struct VUSBDESCLANGID
{
    uint8_t bLength;
    uint8_t bDescriptorType;
} VUSBDESCLANGID;
/** Pointer to a USB language id descriptor. */
typedef VUSBDESCLANGID *PVUSBDESCLANGID;
/** Pointer to a const USB language id descriptor. */
typedef const VUSBDESCLANGID *PCVUSBDESCLANGID;


/**
 * USB string descriptor (from specs).
 */
typedef struct VUSBDESCSTRING
{
    uint8_t bLength;
    uint8_t bDescriptorType;
} VUSBDESCSTRING;
/** Pointer to a USB string descriptor. */
typedef VUSBDESCSTRING *PVUSBDESCSTRING;
/** Pointer to a const USB string descriptor. */
typedef const VUSBDESCSTRING *PCVUSBDESCSTRING;


/**
 * USB device descriptor (from spec)
 */
typedef struct VUSBDESCDEVICE
{
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} VUSBDESCDEVICE;
/** Pointer to a USB device descriptor. */
typedef VUSBDESCDEVICE *PVUSBDESCDEVICE;
/** Pointer to a const USB device descriptor. */
typedef const VUSBDESCDEVICE *PCVUSBDESCDEVICE;

/**
 * USB device qualifier (from spec 9.6.2)
 */
struct VUSBDEVICEQUALIFIER
{
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUsb;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint8_t bNumConfigurations;
    uint8_t bReserved;
};

typedef struct VUSBDEVICEQUALIFIER VUSBDEVICEQUALIFIER;
typedef VUSBDEVICEQUALIFIER *PVUSBDEVICEQUALIFIER;


/**
 * USB configuration descriptor (from spec).
 */
typedef struct VUSBDESCCONFIG
{
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength; /**< recalculated by VUSB when involved in URB. */
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  MaxPower;
} VUSBDESCCONFIG;
/** Pointer to a USB configuration descriptor. */
typedef VUSBDESCCONFIG *PVUSBDESCCONFIG;
/** Pointer to a readonly USB configuration descriptor. */
typedef const VUSBDESCCONFIG *PCVUSBDESCCONFIG;


/**
 * USB interface association descriptor (from USB ECN Interface Association Descriptors)
 */
typedef struct VUSBDESCIAD
{
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bFirstInterface;
    uint8_t bInterfaceCount;
    uint8_t bFunctionClass;
    uint8_t bFunctionSubClass;
    uint8_t bFunctionProtocol;
    uint8_t iFunction;
} VUSBDESCIAD;
/** Pointer to a USB interface association descriptor. */
typedef VUSBDESCIAD *PVUSBDESCIAD;
/** Pointer to a readonly USB interface association descriptor. */
typedef const VUSBDESCIAD *PCVUSBDESCIAD;


/**
 * USB interface descriptor (from spec)
 */
typedef struct VUSBDESCINTERFACE
{
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} VUSBDESCINTERFACE;
/** Pointer to a USB interface descriptor. */
typedef VUSBDESCINTERFACE *PVUSBDESCINTERFACE;
/** Pointer to a const USB interface descriptor. */
typedef const VUSBDESCINTERFACE *PCVUSBDESCINTERFACE;


/**
 * USB endpoint descriptor (from spec)
 */
typedef struct VUSBDESCENDPOINT
{
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} VUSBDESCENDPOINT;
/** Pointer to a USB endpoint descriptor. */
typedef VUSBDESCENDPOINT *PVUSBDESCENDPOINT;
/** Pointer to a const USB endpoint descriptor. */
typedef const VUSBDESCENDPOINT *PCVUSBDESCENDPOINT;


/**
 * USB SuperSpeed endpoint companion descriptor (from USB3 spec)
 */
typedef struct VUSBDESCSSEPCOMPANION
{
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bMaxBurst;
    uint8_t  bmAttributes;
    uint16_t wBytesPerInterval;
} VUSBDESCSSEPCOMPANION;
/** Pointer to a USB endpoint companion descriptor. */
typedef VUSBDESCSSEPCOMPANION *PVUSBDESCSSEPCOMPANION;
/** Pointer to a const USB endpoint companion descriptor. */
typedef const VUSBDESCSSEPCOMPANION *PCVUSBDESCSSEPCOMPANION;


/**
 * USB Binary Device Object Store, aka BOS (from USB3 spec)
 */
typedef struct VUSBDESCBOS
{
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumDeviceCaps;
} VUSBDESCBOS;
/** Pointer to a USB BOS descriptor. */
typedef VUSBDESCBOS *PVUSBDESCBOS;
/** Pointer to a const USB BOS descriptor. */
typedef const VUSBDESCBOS *PCVUSBDESCBOS;


/**
 * Generic USB Device Capability Descriptor within BOS (from USB3 spec)
 */
typedef struct VUSBDESCDEVICECAP
{
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDevCapabilityType;
    uint8_t  aCapSpecific[1];
} VUSBDESCDEVICECAP;
/** Pointer to a USB device capability descriptor. */
typedef VUSBDESCDEVICECAP *PVUSBDESCDEVICECAP;
/** Pointer to a const USB device capability descriptor. */
typedef const VUSBDESCDEVICECAP *PCVUSBDESCDEVICECAP;


/**
 * SuperSpeed USB Device Capability Descriptor within BOS
 */
typedef struct VUSBDESCSSDEVCAP
{
    uint8_t  bLength;
    uint8_t  bDescriptorType;       /* DEVICE CAPABILITY */
    uint8_t  bDevCapabilityType;    /* SUPERSPEED_USB */
    uint8_t  bmAttributes;
    uint16_t wSpeedsSupported;
    uint8_t  bFunctionalitySupport;
    uint8_t  bU1DevExitLat;
    uint16_t wU2DevExitLat;
} VUSBDESCSSDEVCAP;
/** Pointer to an SS USB device capability descriptor. */
typedef VUSBDESCSSDEVCAP *PVUSBDESCSSDEVCAP;
/** Pointer to a const SS USB device capability descriptor. */
typedef const VUSBDESCSSDEVCAP *PCVUSBDESCSSDEVCAP;


/**
 * USB 2.0 Extension Descriptor within BOS
 */
typedef struct VUSBDESCUSB2EXT
{
    uint8_t  bLength;
    uint8_t  bDescriptorType;       /* DEVICE CAPABILITY */
    uint8_t  bDevCapabilityType;    /* USB 2.0 EXTENSION */
    uint8_t  bmAttributes;
} VUSBDESCUSB2EXT;
/** Pointer to a USB 2.0 extension capability descriptor. */
typedef VUSBDESCUSB2EXT *PVUSBDESCUSB2EXT;
/** Pointer to a const USB 2.0 extension capability descriptor. */
typedef const VUSBDESCUSB2EXT *PCVUSBDESCUSB2EXT;


#pragma pack() /* end of the byte packing. */


/**
 * USB configuration descriptor, the parsed variant used by VUSB.
 */
typedef struct VUSBDESCCONFIGEX
{
    /** The USB descriptor data.
     * @remark  The wTotalLength member is recalculated before the data is passed to the guest. */
    VUSBDESCCONFIG Core;
    /** Pointer to additional descriptor bytes following what's covered by VUSBDESCCONFIG. */
    void *pvMore;
    /** Pointer to additional class- or vendor-specific interface descriptors. */
    const void *pvClass;
    /** Size of class- or vendor-specific descriptors. */
    uint16_t cbClass;
    /** Pointer to an array of the interfaces referenced in the configuration.
     * Core.bNumInterfaces in size. */
    const struct VUSBINTERFACE *paIfs;
    /** Pointer to the original descriptor data read from the device. */
    const void *pvOriginal;
} VUSBDESCCONFIGEX;
/** Pointer to a parsed USB configuration descriptor. */
typedef VUSBDESCCONFIGEX *PVUSBDESCCONFIGEX;
/** Pointer to a const parsed USB configuration descriptor. */
typedef const VUSBDESCCONFIGEX *PCVUSBDESCCONFIGEX;


/**
 * For tracking the alternate interface settings of a configuration.
 */
typedef struct VUSBINTERFACE
{
    /** Pointer to an array of interfaces. */
    const struct VUSBDESCINTERFACEEX *paSettings;
    /** The number of entries in the array. */
    uint32_t cSettings;
} VUSBINTERFACE;
/** Pointer to a VUSBINTERFACE. */
typedef VUSBINTERFACE *PVUSBINTERFACE;
/** Pointer to a const VUSBINTERFACE. */
typedef const VUSBINTERFACE *PCVUSBINTERFACE;


/**
 * USB interface descriptor, the parsed variant used by VUSB.
 */
typedef struct VUSBDESCINTERFACEEX
{
    /** The USB descriptor data. */
    VUSBDESCINTERFACE Core;
    /** Pointer to additional descriptor bytes following what's covered by VUSBDESCINTERFACE. */
    const void *pvMore;
    /** Pointer to additional class- or vendor-specific interface descriptors. */
    const void *pvClass;
    /** Size of class- or vendor-specific descriptors. */
    uint16_t cbClass;
    /** Pointer to an array of the endpoints referenced by the interface.
     * Core.bNumEndpoints in size. */
    const struct VUSBDESCENDPOINTEX *paEndpoints;
    /** Interface association descriptor, which prepends a group of interfaces,
     * starting with this interface. */
    PCVUSBDESCIAD pIAD;
    /** Size of interface association descriptor. */
    uint16_t cbIAD;
} VUSBDESCINTERFACEEX;
/** Pointer to an prased USB interface descriptor. */
typedef VUSBDESCINTERFACEEX *PVUSBDESCINTERFACEEX;
/** Pointer to a const parsed USB interface descriptor. */
typedef const VUSBDESCINTERFACEEX *PCVUSBDESCINTERFACEEX;


/**
 * USB endpoint descriptor, the parsed variant used by VUSB.
 */
typedef struct VUSBDESCENDPOINTEX
{
    /** The USB descriptor data.
     * @remark The wMaxPacketSize member is converted to native endian. */
    VUSBDESCENDPOINT Core;
    /** Pointer to additional descriptor bytes following what's covered by VUSBDESCENDPOINT. */
    const void *pvMore;
    /** Pointer to additional class- or vendor-specific endpoint descriptors. */
    const void *pvClass;
    /** Size of class- or vendor-specific descriptors. */
    uint16_t cbClass;
    /** Pointer to SuperSpeed endpoint companion descriptor (SS endpoints only). */
    const void *pvSsepc;
    /** Size of SuperSpeed endpoint companion descriptor.
     * @remark Must be non-zero for SuperSpeed endpoints. */
    uint16_t cbSsepc;
} VUSBDESCENDPOINTEX;
/** Pointer to a parsed USB endpoint descriptor. */
typedef VUSBDESCENDPOINTEX *PVUSBDESCENDPOINTEX;
/** Pointer to a const parsed USB endpoint descriptor. */
typedef const VUSBDESCENDPOINTEX *PCVUSBDESCENDPOINTEX;


/** @name USB Control message recipient codes (from spec)
 * @{ */
#define VUSB_TO_DEVICE          0x0
#define VUSB_TO_INTERFACE       0x1
#define VUSB_TO_ENDPOINT        0x2
#define VUSB_TO_OTHER           0x3
#define VUSB_RECIP_MASK         0x1f
/** @} */

/** @name USB control pipe setup packet structure (from spec)
 * @{ */
#define VUSB_REQ_SHIFT          (5)
#define VUSB_REQ_STANDARD       (0x0 << VUSB_REQ_SHIFT)
#define VUSB_REQ_CLASS          (0x1 << VUSB_REQ_SHIFT)
#define VUSB_REQ_VENDOR         (0x2 << VUSB_REQ_SHIFT)
#define VUSB_REQ_RESERVED       (0x3 << VUSB_REQ_SHIFT)
#define VUSB_REQ_MASK           (0x3 << VUSB_REQ_SHIFT)
/** @} */

#define VUSB_DIR_TO_DEVICE      0x00
#define VUSB_DIR_TO_HOST        0x80
#define VUSB_DIR_MASK           0x80

/**
 * USB Setup request (from spec)
 */
typedef struct vusb_setup
{
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} VUSBSETUP;
/** Pointer to a setup request. */
typedef VUSBSETUP *PVUSBSETUP;
/** Pointer to a const setup request. */
typedef const VUSBSETUP *PCVUSBSETUP;

/** @name USB Standard device requests (from spec)
 * @{ */
#define VUSB_REQ_GET_STATUS         0x00
#define VUSB_REQ_CLEAR_FEATURE      0x01
#define VUSB_REQ_SET_FEATURE        0x03
#define VUSB_REQ_SET_ADDRESS        0x05
#define VUSB_REQ_GET_DESCRIPTOR     0x06
#define VUSB_REQ_SET_DESCRIPTOR     0x07
#define VUSB_REQ_GET_CONFIGURATION  0x08
#define VUSB_REQ_SET_CONFIGURATION  0x09
#define VUSB_REQ_GET_INTERFACE      0x0a
#define VUSB_REQ_SET_INTERFACE      0x0b
#define VUSB_REQ_SYNCH_FRAME        0x0c
#define VUSB_REQ_MAX                0x0d
/** @} */

/** @} */ /* end of grp_vusb_std */



/** @name USB Standard version flags.
 * @{ */
/** Indicates USB 1.1 support. */
#define VUSB_STDVER_11              RT_BIT(1)
/** Indicates USB 2.0 support. */
#define VUSB_STDVER_20              RT_BIT(2)
/** Indicates USB 3.0 support. */
#define VUSB_STDVER_30              RT_BIT(3)
/** @} */

/**
 * USB port/device speeds.
 */
typedef enum VUSBSPEED
{
    /** Undetermined/unknown speed. */
    VUSB_SPEED_UNKNOWN = 0,
    /** Low-speed (LS), 1.5 Mbit/s, USB 1.0. */
    VUSB_SPEED_LOW,
    /** Full-speed (FS), 12 Mbit/s, USB 1.1. */
    VUSB_SPEED_FULL,
    /** High-speed (HS), 480 Mbit/s, USB 2.0. */
    VUSB_SPEED_HIGH,
    /** Variable speed, wireless USB 2.5. */
    VUSB_SPEED_VARIABLE,
    /** SuperSpeed (SS), 5.0 Gbit/s, USB 3.0. */
    VUSB_SPEED_SUPER,
    /** SuperSpeed+ (SS+), 10.0 Gbit/s, USB 3.1. */
    VUSB_SPEED_SUPERPLUS,
    /** The usual 32-bit hack. */
    VUSB_SPEED_32BIT_HACK = 0x7fffffff
} VUSBSPEED;

/**
 * VUSB transfer direction.
 */
typedef enum VUSBDIRECTION
{
    /** Setup */
    VUSBDIRECTION_SETUP = 0,
#define VUSB_DIRECTION_SETUP    VUSBDIRECTION_SETUP
    /** In - Device to host. */
    VUSBDIRECTION_IN = 1,
#define VUSB_DIRECTION_IN       VUSBDIRECTION_IN
    /** Out - Host to device. */
    VUSBDIRECTION_OUT = 2,
#define VUSB_DIRECTION_OUT  VUSBDIRECTION_OUT
    /** Invalid direction */
    VUSBDIRECTION_INVALID = 0x7f
} VUSBDIRECTION;

/**
 * VUSB Transfer types.
 */
typedef enum VUSBXFERTYPE
{
    /** Control message. Used to represent a single control transfer. */
    VUSBXFERTYPE_CTRL = 0,
    /* Isochronous transfer. */
    VUSBXFERTYPE_ISOC,
    /** Bulk transfer. */
    VUSBXFERTYPE_BULK,
    /** Interrupt transfer. */
    VUSBXFERTYPE_INTR,
    /** Complete control message. Used to represent an entire control message. */
    VUSBXFERTYPE_MSG,
    /** Invalid transfer type. */
    VUSBXFERTYPE_INVALID = 0x7f
} VUSBXFERTYPE;

/** Number of valid USB transfer types - KEEP in sync with VUSBXFERTYPE!. */
#define VUSBXFERTYPE_ELEMENTS (5)

/** Pointer to a VBox USB device interface. */
typedef struct VUSBIDEVICE      *PVUSBIDEVICE;

/** Pointer to a VUSB RootHub port interface. */
typedef struct VUSBIROOTHUBPORT *PVUSBIROOTHUBPORT;

/** Pointer to an USB request descriptor. */
typedef struct VUSBURB          *PVUSBURB;


/**
 * VUSB device reset completion callback function.
 * This is called by the reset thread when the reset has been completed.
 *
 * @param   pDevice Pointer to the virtual USB device core.
 * @param   uPort   The port of the device which completed the reset.
 * @param   rc      The VBox status code of the reset operation.
 * @param   pvUser  User specific argument.
 *
 * @thread  The reset thread or EMT.
 */
typedef DECLCALLBACKTYPE(void, FNVUSBRESETDONE,(PVUSBIDEVICE pDevice, uint32_t uPort, int rc, void *pvUser));
/** Pointer to a device reset completion callback function (FNUSBRESETDONE). */
typedef FNVUSBRESETDONE *PFNVUSBRESETDONE;


/**
 * The state of a VUSB Device.
 *
 * @remark  The order of these states is vital.
 */
typedef enum VUSBDEVICESTATE
{
    VUSB_DEVICE_STATE_INVALID = 0,
    VUSB_DEVICE_STATE_DETACHED,
    VUSB_DEVICE_STATE_ATTACHED,
    VUSB_DEVICE_STATE_POWERED,
    VUSB_DEVICE_STATE_DEFAULT,
    VUSB_DEVICE_STATE_ADDRESS,
    VUSB_DEVICE_STATE_CONFIGURED,
    VUSB_DEVICE_STATE_SUSPENDED,
    /** The device is being reset. Don't mess with it.
     * Next states: VUSB_DEVICE_STATE_DEFAULT, VUSB_DEVICE_STATE_DESTROYED
     */
    VUSB_DEVICE_STATE_RESET,
    /** The device has been destroyed. */
    VUSB_DEVICE_STATE_DESTROYED,
    /** The usual 32-bit hack. */
    VUSB_DEVICE_STATE_32BIT_HACK = 0x7fffffff
} VUSBDEVICESTATE;


/** Maximum number of USB devices supported. */
#define VUSB_DEVICES_MAX            128
/** An invalid device port. */
#define VUSB_DEVICE_PORT_INVALID    UINT32_MAX

/**
 * VBox USB port bitmap.
 *
 * Bit 0 == Port 0, ... , Bit 127 == Port 127.
 */
typedef struct VUSBPORTBITMAP
{
    /** 128 bits */
    char ach[VUSB_DEVICES_MAX / 8];
} VUSBPORTBITMAP;
/** Pointer to a VBox USB port bitmap. */
typedef VUSBPORTBITMAP *PVUSBPORTBITMAP;
AssertCompile(sizeof(VUSBPORTBITMAP) * 8 >= VUSB_DEVICES_MAX);

#ifndef RDESKTOP

/**
 * The VUSB RootHub port interface provided by the HCI (down).
 * Pair with VUSBIROOTCONNECTOR
 */
typedef struct VUSBIROOTHUBPORT
{
    /**
     * Get the number of available ports in the hub.
     *
     * @returns The number of ports available.
     * @param   pInterface      Pointer to this structure.
     * @param   pAvailable      Bitmap indicating the available ports. Set bit == available port.
     */
    DECLR3CALLBACKMEMBER(unsigned, pfnGetAvailablePorts,(PVUSBIROOTHUBPORT pInterface, PVUSBPORTBITMAP pAvailable));

    /**
     * Gets the supported USB versions.
     *
     * @returns The mask of supported USB versions.
     * @param   pInterface      Pointer to this structure.
     */
    DECLR3CALLBACKMEMBER(uint32_t, pfnGetUSBVersions,(PVUSBIROOTHUBPORT pInterface));

    /**
     * A device is being attached to a port in the roothub.
     *
     * @param   pInterface      Pointer to this structure.
     * @param   uPort           The port number assigned to the device.
     * @param   enmSpeed        The speed of the device being attached.
     */
    DECLR3CALLBACKMEMBER(int, pfnAttach,(PVUSBIROOTHUBPORT pInterface, uint32_t uPort, VUSBSPEED enmSpeed));

    /**
     * A device is being detached from a port in the roothub.
     *
     * @param   pInterface      Pointer to this structure.
     * @param   uPort           The port number assigned to the device.
     */
    DECLR3CALLBACKMEMBER(void, pfnDetach,(PVUSBIROOTHUBPORT pInterface, uint32_t uPort));

    /**
     * Reset the root hub.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to this structure.
     * @param   fResetOnLinux   Whether or not to do real reset on linux.
     */
    DECLR3CALLBACKMEMBER(int, pfnReset,(PVUSBIROOTHUBPORT pInterface, bool fResetOnLinux));

    /**
     * Transfer completion callback routine.
     *
     * VUSB will call this when a transfer have been completed
     * in a one or another way.
     *
     * @param   pInterface      Pointer to this structure.
     * @param   pUrb            Pointer to the URB in question.
     */
    DECLR3CALLBACKMEMBER(void, pfnXferCompletion,(PVUSBIROOTHUBPORT pInterface, PVUSBURB pUrb));

    /**
     * Handle transfer errors.
     *
     * VUSB calls this when a transfer attempt failed. This function will respond
     * indicating whether to retry or complete the URB with failure.
     *
     * @returns Retry indicator.
     * @param   pInterface      Pointer to this structure.
     * @param   pUrb            Pointer to the URB in question.
     */
    DECLR3CALLBACKMEMBER(bool, pfnXferError,(PVUSBIROOTHUBPORT pInterface, PVUSBURB pUrb));

    /**
     * Processes a new frame if periodic frame processing is enabled.
     *
     * @returns Flag whether there was activity which influences the frame rate.
     * @param   pInterface      Pointer to this structure.
     * @param   u32FrameNo      The frame number.
     */
    DECLR3CALLBACKMEMBER(bool, pfnStartFrame, (PVUSBIROOTHUBPORT pInterface, uint32_t u32FrameNo));

    /**
     * Informs the callee about a change in the frame rate due to too many idle cycles or
     * when seeing activity after some idle time.
     *
     * @param   pInterface      Pointer to this structure.
     * @param   u32FrameRate    The new frame rate.
     */
    DECLR3CALLBACKMEMBER(void, pfnFrameRateChanged, (PVUSBIROOTHUBPORT pInterface, uint32_t u32FrameRate));

    /** Alignment dummy. */
    RTR3PTR Alignment;

} VUSBIROOTHUBPORT;
/** VUSBIROOTHUBPORT interface ID. */
# define VUSBIROOTHUBPORT_IID                   "2ece01c2-4dbf-4bd5-96ca-09fc14164cd4"

/** Pointer to a VUSB RootHub connector interface. */
typedef struct VUSBIROOTHUBCONNECTOR *PVUSBIROOTHUBCONNECTOR;
/**
 * The VUSB RootHub connector interface provided by the VBox USB RootHub driver
 * (up).
 * Pair with VUSBIROOTHUBPORT.
 */
typedef struct VUSBIROOTHUBCONNECTOR
{
    /**
     * Sets the URB parameters for the caller.
     *
     * @returns VBox status code.
     * @param   pInterface  Pointer to this struct.
     * @param   cbHci       Size of the data private to the HCI for each URB when allocated.
     * @param   cbHciTd     Size of one transfer descriptor. The number of transfer descriptors
     *                      is given VUSBIROOTHUBCONNECTOR::pfnNewUrb for each URB to calculate the
     *                      final amount of memory required for the TDs.
     *
     * @note This must be called before starting to allocate any URB or otherwise there will be no
     *       data available for the HCI.
     */
    DECLR3CALLBACKMEMBER(int, pfnSetUrbParams, (PVUSBIROOTHUBCONNECTOR pInterface, size_t cbHci, size_t cbHciTd));

    /**
     * Resets the roothub.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to this struct.
     * @param   fResetOnLinux   Whether or not to do real reset on linux.
     */
    DECLR3CALLBACKMEMBER(int, pfnReset, (PVUSBIROOTHUBCONNECTOR pInterface, bool fResetOnLinux));

    /**
     * Powers on the roothub.
     *
     * @returns VBox status code.
     * @param   pInterface  Pointer to this struct.
     */
    DECLR3CALLBACKMEMBER(int, pfnPowerOn, (PVUSBIROOTHUBCONNECTOR pInterface));

    /**
     * Power off the roothub.
     *
     * @returns VBox status code.
     * @param   pInterface  Pointer to this struct.
     */
    DECLR3CALLBACKMEMBER(int, pfnPowerOff, (PVUSBIROOTHUBCONNECTOR pInterface));

    /**
     * Allocates a new URB for a transfer.
     *
     * Either submit using pfnSubmitUrb or free using VUSBUrbFree().
     *
     * @returns Pointer to a new URB.
     * @returns NULL on failure - try again later.
     *          This will not fail if the device wasn't found. We'll fail it
     *          at submit time, since that makes the usage of this api simpler.
     * @param   pInterface  Pointer to this struct.
     * @param   DstAddress  The destination address of the URB.
     * @param   uPort       Optional port of the device the URB is for, use VUSB_DEVICE_PORT_INVALID to indicate to use the destination address.
     * @param   enmType     Type of the URB.
     * @param   enmDir      Data transfer direction.
     * @param   cbData      The amount of data space required.
     * @param   cTds        The amount of TD space.
     * @param   pszTag      Custom URB tag assigned by the caller, only for
     *                      logged builds and optional.
     *
     * @note pDev should be NULL in most cases. The only useful case is for USB3 where
     *       it is required for the SET_ADDRESS request because USB3 uses unicast traffic.
     */
    DECLR3CALLBACKMEMBER(PVUSBURB, pfnNewUrb,(PVUSBIROOTHUBCONNECTOR pInterface, uint8_t DstAddress, uint32_t uPort,
                                              VUSBXFERTYPE enmType, VUSBDIRECTION enmDir, uint32_t cbData, uint32_t cTds, const char *pszTag));

    /**
     * Free an URB not submitted yet.
     *
     * @returns VBox status code.
     * @param   pInterface  Pointer to this struct.
     * @param   pUrb        Pointer to the URB to free returned by VUSBIROOTHUBCONNECTOR::pfnNewUrb.
     */
    DECLR3CALLBACKMEMBER(int, pfnFreeUrb, (PVUSBIROOTHUBCONNECTOR pInterface, PVUSBURB pUrb));

    /**
     * Submits a URB for transfer.
     * The transfer will do asynchronously if possible.
     *
     * @returns VBox status code.
     * @param   pInterface  Pointer to this struct.
     * @param   pUrb        Pointer to the URB returned by pfnNewUrb.
     *                      The URB will be freed in case of failure.
     * @param   pLed        Pointer to USB Status LED
     */
    DECLR3CALLBACKMEMBER(int, pfnSubmitUrb,(PVUSBIROOTHUBCONNECTOR pInterface, PVUSBURB pUrb, struct PDMLED *pLed));

    /**
     * Call to service asynchronous URB completions in a polling fashion.
     *
     * Reaped URBs will be finished by calling the completion callback,
     * thus there is no return code or input or anything from this function
     * except for potential state changes elsewhere.
     *
     * @param   pInterface  Pointer to this struct.
     * @param   uPort       Port of the device to reap URBs on.
     * @param   cMillies    Number of milliseconds to poll for completion.
     */
    DECLR3CALLBACKMEMBER(void, pfnReapAsyncUrbs,(PVUSBIROOTHUBCONNECTOR pInterface, uint32_t uPort, RTMSINTERVAL cMillies));

    /**
     * Cancels and completes - with CRC failure - all in-flight async URBs.
     * This is typically done before saving a state.
     *
     * @param   pInterface  Pointer to this struct.
     */
    DECLR3CALLBACKMEMBER(void, pfnCancelAllUrbs,(PVUSBIROOTHUBCONNECTOR pInterface));

    /**
     * Cancels and completes - with CRC failure - all URBs queued on an endpoint.
     * This is done in response to a guest endpoint/pipe abort.
     *
     * @returns VBox status code.
     * @param   pInterface  Pointer to this struct.
     * @param   uPort       Port of the device.
     * @param   EndPt       Endpoint number.
     * @param   enmDir      Endpoint direction.
     */
    DECLR3CALLBACKMEMBER(int, pfnAbortEpByPort,(PVUSBIROOTHUBCONNECTOR pInterface, uint32_t uPort, int EndPt, VUSBDIRECTION enmDir));

    /**
     * Cancels and completes - with CRC failure - all URBs queued on an endpoint.
     * This is done in response to a guest endpoint/pipe abort.
     *
     * @returns VBox status code.
     * @param   pInterface  Pointer to this struct.
     * @param   DstAddress  Port of the device.
     * @param   EndPt       Endpoint number.
     * @param   enmDir      Endpoint direction.
     */
    DECLR3CALLBACKMEMBER(int, pfnAbortEpByAddr,(PVUSBIROOTHUBCONNECTOR pInterface, uint8_t DstAddress, int EndPt, VUSBDIRECTION enmDir));

    /**
     * Attach the device to the root hub.
     * The device must not be attached to any hub for this call to succeed.
     *
     * @returns VBox status code.
     * @param   pInterface  Pointer to this struct.
     * @param   uPort       Port of the device to attach.
     */
    DECLR3CALLBACKMEMBER(int, pfnAttachDevice,(PVUSBIROOTHUBCONNECTOR pInterface, uint32_t uPort));

    /**
     * Detach the device from the root hub.
     * The device must already be attached for this call to succeed.
     *
     * @returns VBox status code.
     * @param   pInterface  Pointer to this struct.
     * @param   uPort       Port of the device to detach.
     */
    DECLR3CALLBACKMEMBER(int, pfnDetachDevice,(PVUSBIROOTHUBCONNECTOR pInterface, uint32_t uPort));

    /**
     * Sets periodic frame processing.
     *
     * @returns VBox status code.
     * @param   pInterface  Pointer to this struct.
     * @param   uFrameRate  The target frame rate in Hertz, 0 disables periodic frame processing.
     *                      The real frame rate might be lower if there is no activity for a certain period or
     *                      higher if there is a need for catching up with where the guest expects the device to be.
     */
    DECLR3CALLBACKMEMBER(int, pfnSetPeriodicFrameProcessing, (PVUSBIROOTHUBCONNECTOR pInterface, uint32_t uFrameRate));

    /**
     * Returns the current frame rate for the periodic frame processing.
     *
     * @returns Frame rate for periodic frame processing.
     * @retval  0 if disabled.
     * @param   pInterface  Pointer to this struct.
     */
    DECLR3CALLBACKMEMBER(uint32_t, pfnGetPeriodicFrameRate, (PVUSBIROOTHUBCONNECTOR pInterface));

    /**
     * Updates the internally stored isochronous scheduling frame for a given
     * endpoint and returns the delta between the current and previous frame.
     *
     * @returns Delta between currently and previously scheduled frame.
     * @retval  0 if no previous frame was set.
     * @param   pInterface  Pointer to this struct.
     * @param   uPort       Port of the device.
     * @param   EndPt       Endpoint number.
     * @param   enmDir      Endpoint direction.
     * @param   uNewFrameID The frame ID of a new transfer.
     * @param   uBits       The number of significant bits in frame ID.
     */
    DECLR3CALLBACKMEMBER(uint32_t, pfnUpdateIsocFrameDelta, (PVUSBIROOTHUBCONNECTOR pInterface, uint32_t uPort,
                                                             int EndPt, VUSBDIRECTION enmDir, uint16_t uNewFrameID, uint8_t uBits));

    /**
     * Resets the device.
     *
     * Since a device reset shall take at least 10ms from the guest point of view,
     * it must be performed asynchronously. We create a thread which performs this
     * operation and ensures it will take at least 10ms.
     *
     * At times - like init - a synchronous reset is required, this can be done
     * by passing NULL for pfnDone.
     *
     * -- internal stuff, move it --
     * While the device is being reset it is in the VUSB_DEVICE_STATE_RESET state.
     * On completion it will be in the VUSB_DEVICE_STATE_DEFAULT state if successful,
     * or in the VUSB_DEVICE_STATE_DETACHED state if the rest failed.
     * -- internal stuff, move it --
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to this struct.
     * @param   uPort           Port of the device to reset.
     * @param   fResetOnLinux   Set if we can permit a real reset and a potential logical
     *                          device reconnect on linux hosts.
     * @param   pfnDone         Pointer to the completion routine. If NULL a synchronous
     *                          reset  is performed not respecting the 10ms.
     * @param   pvUser          User argument to the completion routine.
     * @param   pVM             The cross context VM structure.  Required if pfnDone
     *                          is not NULL.
     */
    DECLR3CALLBACKMEMBER(int, pfnDevReset,(PVUSBIROOTHUBCONNECTOR pInterface, uint32_t uPort, bool fResetOnLinux,
                                           PFNVUSBRESETDONE pfnDone, void *pvUser, PVM pVM));

    /**
     * Powers on the device.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to this struct.
     * @param   uPort           Port of the device to power on.
     */
    DECLR3CALLBACKMEMBER(int, pfnDevPowerOn,(PVUSBIROOTHUBCONNECTOR pInterface, uint32_t uPort));

    /**
     * Powers off the device.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to this struct.
     * @param   uPort           Port of the device to power off.
     */
    DECLR3CALLBACKMEMBER(int, pfnDevPowerOff,(PVUSBIROOTHUBCONNECTOR pInterface, uint32_t uPort));

    /**
     * Get the state of the device.
     *
     * @returns Device state.
     * @param   pInterface      Pointer to this struct.
     * @param   uPort           Port of the device to get the state for.
     */
    DECLR3CALLBACKMEMBER(VUSBDEVICESTATE, pfnDevGetState,(PVUSBIROOTHUBCONNECTOR pInterface, uint32_t uPort));

    /**
     * Returns whether the device implements the saved state handlers
     * and doesn't need to get detached.
     *
     * @returns true if the device supports saving the state, false otherwise.
     * @param   pInterface      Pointer to this struct.
     * @param   uPort           Port of the device to query saved state support for.
     */
    DECLR3CALLBACKMEMBER(bool, pfnDevIsSavedStateSupported,(PVUSBIROOTHUBCONNECTOR pInterface, uint32_t uPort));

    /**
     * Get the speed the device is operating at.
     *
     * @returns Device state.
     * @param   pInterface      Pointer to this struct.
     * @param   uPort           Port of the device to query the speed for.
     */
    DECLR3CALLBACKMEMBER(VUSBSPEED, pfnDevGetSpeed,(PVUSBIROOTHUBCONNECTOR pInterface, uint32_t uPort));

} VUSBIROOTHUBCONNECTOR;
AssertCompileSizeAlignment(VUSBIROOTHUBCONNECTOR, 8);
/** VUSBIROOTHUBCONNECTOR interface ID. */
# define VUSBIROOTHUBCONNECTOR_IID              "79aad2da-1628-4f11-be03-ccb8560bb204"


# ifdef IN_RING3
/** @copydoc VUSBIROOTHUBCONNECTOR::pfnSetUrbParams */
DECLINLINE(int) VUSBIRhSetUrbParams(PVUSBIROOTHUBCONNECTOR pInterface, size_t cbHci, size_t cbHciTd)
{
    return pInterface->pfnSetUrbParams(pInterface, cbHci, cbHciTd);
}

/** @copydoc VUSBIROOTHUBCONNECTOR::pfnNewUrb */
DECLINLINE(PVUSBURB) VUSBIRhNewUrb(PVUSBIROOTHUBCONNECTOR pInterface, uint8_t DstAddress, uint32_t uPort,
                                   VUSBXFERTYPE enmType, VUSBDIRECTION enmDir, uint32_t cbData, uint32_t cTds, const char *pszTag)
{
    return pInterface->pfnNewUrb(pInterface, DstAddress, uPort, enmType, enmDir, cbData, cTds, pszTag);
}

/** @copydoc VUSBIROOTHUBCONNECTOR::pfnFreeUrb */
DECLINLINE(int) VUSBIRhFreeUrb(PVUSBIROOTHUBCONNECTOR pInterface, PVUSBURB pUrb)
{
    return pInterface->pfnFreeUrb(pInterface, pUrb);
}

/** @copydoc VUSBIROOTHUBCONNECTOR::pfnSubmitUrb */
DECLINLINE(int) VUSBIRhSubmitUrb(PVUSBIROOTHUBCONNECTOR pInterface, PVUSBURB pUrb, struct PDMLED *pLed)
{
    return pInterface->pfnSubmitUrb(pInterface, pUrb, pLed);
}

/** @copydoc VUSBIROOTHUBCONNECTOR::pfnReapAsyncUrbs */
DECLINLINE(void) VUSBIRhReapAsyncUrbs(PVUSBIROOTHUBCONNECTOR pInterface, uint32_t uPort, RTMSINTERVAL cMillies)
{
    pInterface->pfnReapAsyncUrbs(pInterface, uPort, cMillies);
}

/** @copydoc VUSBIROOTHUBCONNECTOR::pfnCancelAllUrbs */
DECLINLINE(void) VUSBIRhCancelAllUrbs(PVUSBIROOTHUBCONNECTOR pInterface)
{
    pInterface->pfnCancelAllUrbs(pInterface);
}

/** @copydoc VUSBIROOTHUBCONNECTOR::pfnAttachDevice */
DECLINLINE(int) VUSBIRhAttachDevice(PVUSBIROOTHUBCONNECTOR pInterface, uint32_t uPort)
{
    return pInterface->pfnAttachDevice(pInterface, uPort);
}

/** @copydoc VUSBIROOTHUBCONNECTOR::pfnDetachDevice */
DECLINLINE(int) VUSBIRhDetachDevice(PVUSBIROOTHUBCONNECTOR pInterface, uint32_t uPort)
{
    return pInterface->pfnDetachDevice(pInterface, uPort);
}

/** @copydoc VUSBIROOTHUBCONNECTOR::pfnSetPeriodicFrameProcessing */
DECLINLINE(int) VUSBIRhSetPeriodicFrameProcessing(PVUSBIROOTHUBCONNECTOR pInterface, uint32_t uFrameRate)
{
    return pInterface->pfnSetPeriodicFrameProcessing(pInterface, uFrameRate);
}

/** @copydoc VUSBIROOTHUBCONNECTOR::pfnGetPeriodicFrameRate */
DECLINLINE(uint32_t) VUSBIRhGetPeriodicFrameRate(PVUSBIROOTHUBCONNECTOR pInterface)
{
    return pInterface->pfnGetPeriodicFrameRate(pInterface);
}

/** @copydoc VUSBIROOTHUBCONNECTOR::pfnDevReset */
DECLINLINE(int) VUSBIRhDevReset(PVUSBIROOTHUBCONNECTOR pInterface, uint32_t uPort, bool fResetOnLinux,
                                PFNVUSBRESETDONE pfnDone, void *pvUser, PVM pVM)
{
    return pInterface->pfnDevReset(pInterface, uPort, fResetOnLinux, pfnDone, pvUser, pVM);
}

/** @copydoc VUSBIROOTHUBCONNECTOR::pfnDevPowerOn */
DECLINLINE(int) VUSBIRhDevPowerOn(PVUSBIROOTHUBCONNECTOR pInterface, uint32_t uPort)
{
    return pInterface->pfnDevPowerOn(pInterface, uPort);
}

/** @copydoc VUSBIROOTHUBCONNECTOR::pfnDevPowerOff */
DECLINLINE(int) VUSBIRhDevPowerOff(PVUSBIROOTHUBCONNECTOR pInterface, uint32_t uPort)
{
    return pInterface->pfnDevPowerOff(pInterface, uPort);
}

/** @copydoc VUSBIROOTHUBCONNECTOR::pfnDevGetState */
DECLINLINE(VUSBDEVICESTATE) VUSBIRhDevGetState(PVUSBIROOTHUBCONNECTOR pInterface, uint32_t uPort)
{
    return pInterface->pfnDevGetState(pInterface, uPort);
}

/** @copydoc VUSBIROOTHUBCONNECTOR::pfnDevGetState */
DECLINLINE(bool) VUSBIRhDevIsSavedStateSupported(PVUSBIROOTHUBCONNECTOR pInterface, uint32_t uPort)
{
    return pInterface->pfnDevIsSavedStateSupported(pInterface, uPort);
}

/** @copydoc VUSBIROOTHUBCONNECTOR::pfnDevGetSpeed */
DECLINLINE(VUSBSPEED) VUSBIRhDevGetSpeed(PVUSBIROOTHUBCONNECTOR pInterface, uint32_t uPort)
{
    return pInterface->pfnDevGetSpeed(pInterface, uPort);
}
# endif /* IN_RING3 */

#endif /* ! RDESKTOP */


#ifndef RDESKTOP

/**
 * USB Device Interface (up).
 * No interface pair.
 */
typedef struct VUSBIDEVICE
{
    /**
     * Resets the device.
     *
     * Since a device reset shall take at least 10ms from the guest point of view,
     * it must be performed asynchronously. We create a thread which performs this
     * operation and ensures it will take at least 10ms.
     *
     * At times - like init - a synchronous reset is required, this can be done
     * by passing NULL for pfnDone.
     *
     * -- internal stuff, move it --
     * While the device is being reset it is in the VUSB_DEVICE_STATE_RESET state.
     * On completion it will be in the VUSB_DEVICE_STATE_DEFAULT state if successful,
     * or in the VUSB_DEVICE_STATE_DETACHED state if the rest failed.
     * -- internal stuff, move it --
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to this structure.
     * @param   fResetOnLinux   Set if we can permit a real reset and a potential logical
     *                          device reconnect on linux hosts.
     * @param   pfnDone         Pointer to the completion routine. If NULL a synchronous
     *                          reset  is performed not respecting the 10ms.
     * @param   pvUser          User argument to the completion routine.
     * @param   pVM             The cross context VM structure.  Required if pfnDone
     *                          is not NULL.
     */
    DECLR3CALLBACKMEMBER(int, pfnReset,(PVUSBIDEVICE pInterface, bool fResetOnLinux,
                                        PFNVUSBRESETDONE pfnDone, void *pvUser, PVM pVM));

    /**
     * Powers on the device.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to the device interface structure.
     */
    DECLR3CALLBACKMEMBER(int, pfnPowerOn,(PVUSBIDEVICE pInterface));

    /**
     * Powers off the device.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to the device interface structure.
     */
    DECLR3CALLBACKMEMBER(int, pfnPowerOff,(PVUSBIDEVICE pInterface));

    /**
     * Get the state of the device.
     *
     * @returns Device state.
     * @param   pInterface      Pointer to the device interface structure.
     */
    DECLR3CALLBACKMEMBER(VUSBDEVICESTATE, pfnGetState,(PVUSBIDEVICE pInterface));

    /**
     * Returns whether the device implements the saved state handlers
     * and doesn't need to get detached.
     *
     * @returns true if the device supports saving the state, false otherwise.
     * @param   pInterface      Pointer to the device interface structure.
     */
    DECLR3CALLBACKMEMBER(bool, pfnIsSavedStateSupported,(PVUSBIDEVICE pInterface));

    /**
     * Get the speed the device is operating at.
     *
     * @returns Device state.
     * @param   pInterface      Pointer to the device interface structure.
     */
    DECLR3CALLBACKMEMBER(VUSBSPEED, pfnGetSpeed,(PVUSBIDEVICE pInterface));

} VUSBIDEVICE;
/** VUSBIDEVICE interface ID. */
# define VUSBIDEVICE_IID                        "af576b38-e8ca-4db7-810a-2596d8d57ca0"


# ifdef IN_RING3
/**
 * Resets the device.
 *
 * Since a device reset shall take at least 10ms from the guest point of view,
 * it must be performed asynchronously. We create a thread which performs this
 * operation and ensures it will take at least 10ms.
 *
 * At times - like init - a synchronous reset is required, this can be done
 * by passing NULL for pfnDone.
 *
 * -- internal stuff, move it --
 * While the device is being reset it is in the VUSB_DEVICE_STATE_RESET state.
 * On completion it will be in the VUSB_DEVICE_STATE_DEFAULT state if successful,
 * or in the VUSB_DEVICE_STATE_DETACHED state if the rest failed.
 * -- internal stuff, move it --
 *
 * @returns VBox status code.
 * @param   pInterface      Pointer to the device interface structure.
 * @param   fResetOnLinux   Set if we can permit a real reset and a potential logical
 *                          device reconnect on linux hosts.
 * @param   pfnDone         Pointer to the completion routine.  If NULL a
 *                          synchronous reset is performed not respecting the
 *                          10ms.
 * @param   pvUser          User argument to the completion routine.
 * @param   pVM             The cross context VM structure.  Required if pfnDone
 *                          is not NULL.
 *
 * NULL is acceptable Required if callback in EMT is desired, NULL is otherwise
 *                          acceptable.
 */
DECLINLINE(int) VUSBIDevReset(PVUSBIDEVICE pInterface, bool fResetOnLinux, PFNVUSBRESETDONE pfnDone, void *pvUser, PVM pVM)
{
    return pInterface->pfnReset(pInterface, fResetOnLinux, pfnDone, pvUser, pVM);
}

/**
 * Powers on the device.
 *
 * @returns VBox status code.
 * @param   pInterface      Pointer to the device interface structure.
 */
DECLINLINE(int) VUSBIDevPowerOn(PVUSBIDEVICE pInterface)
{
    return pInterface->pfnPowerOn(pInterface);
}

/**
 * Powers off the device.
 *
 * @returns VBox status code.
 * @param   pInterface      Pointer to the device interface structure.
 */
DECLINLINE(int) VUSBIDevPowerOff(PVUSBIDEVICE pInterface)
{
    return pInterface->pfnPowerOff(pInterface);
}

/**
 * Get the state of the device.
 *
 * @returns Device state.
 * @param   pInterface      Pointer to the device interface structure.
 */
DECLINLINE(VUSBDEVICESTATE) VUSBIDevGetState(PVUSBIDEVICE pInterface)
{
    return pInterface->pfnGetState(pInterface);
}

/**
 * @copydoc VUSBIDEVICE::pfnIsSavedStateSupported
 */
DECLINLINE(bool) VUSBIDevIsSavedStateSupported(PVUSBIDEVICE pInterface)
{
    return pInterface->pfnIsSavedStateSupported(pInterface);
}
# endif /* IN_RING3 */

#endif /* ! RDESKTOP */

/** @name URB
 * @{ */

/**
 * VUSB Transfer status codes.
 */
typedef enum VUSBSTATUS
{
    /** Transer was ok. */
    VUSBSTATUS_OK = 0,
    /** Transfer stalled, endpoint halted. */
    VUSBSTATUS_STALL,
    /** Device not responding. */
    VUSBSTATUS_DNR,
    /** CRC error. */
    VUSBSTATUS_CRC,
    /** Data underrun error. */
    VUSBSTATUS_DATA_UNDERRUN,
    /** Data overrun error. */
    VUSBSTATUS_DATA_OVERRUN,
    /** The isochronous buffer hasn't been touched. */
    VUSBSTATUS_NOT_ACCESSED,
    /** Canceled/undone URB (VUSB internal). */
    VUSBSTATUS_UNDO,
    /** Canceled URB. */
    VUSBSTATUS_CANCELED,
    /** Invalid status. */
    VUSBSTATUS_INVALID = 0x7f
} VUSBSTATUS;


/**
 * The URB states
 */
typedef enum VUSBURBSTATE
{
    /** The usual invalid state. */
    VUSBURBSTATE_INVALID = 0,
    /** The URB is free, i.e. not in use.
     * Next state: ALLOCATED */
    VUSBURBSTATE_FREE,
    /** The URB is allocated, i.e. being prepared for submission.
     * Next state: FREE, IN_FLIGHT */
    VUSBURBSTATE_ALLOCATED,
    /** The URB is in flight.
     * Next state: REAPED, CANCELLED */
    VUSBURBSTATE_IN_FLIGHT,
    /** The URB has been reaped and is being completed.
     * Next state: FREE */
    VUSBURBSTATE_REAPED,
    /** The URB has been cancelled and is awaiting reaping and immediate freeing.
     * Next state: FREE */
    VUSBURBSTATE_CANCELLED,
    /** The end of the valid states (exclusive). */
    VUSBURBSTATE_END,
    /** The usual 32-bit blow up. */
    VUSBURBSTATE_32BIT_HACK = 0x7fffffff
} VUSBURBSTATE;


/**
 * Information about a isochronous packet.
 */
typedef struct VUSBURBISOCPKT
{
    /** The size of the packet.
     * IN: The packet size. I.e. the number of bytes to the next packet or end of buffer.
     * OUT: The actual size transferred. */
    uint32_t        cb;
    /** The offset of the packet. (Relative to VUSBURB::abData[0].)
     * OUT: This can be changed by the USB device if it does some kind of buffer squeezing. */
    uint32_t        off;
    /** The status of the transfer.
     * IN: VUSBSTATUS_INVALID
     * OUT: VUSBSTATUS_INVALID if nothing was done, otherwise the correct status. */
    VUSBSTATUS      enmStatus;
} VUSBURBISOCPKT;
/** Pointer to a isochronous packet. */
typedef VUSBURBISOCPKT *PVUSBURBISOCPTK;
/** Pointer to a const isochronous packet. */
typedef const VUSBURBISOCPKT *PCVUSBURBISOCPKT;

/** Private controller emulation specific data for the associated USB request descriptor. */
typedef struct VUSBURBHCIINT *PVUSBURBHCI;
/** Private controller emulation specific TD data. */
typedef struct VUSBURBHCITDINT *PVUSBURBHCITD;
/** Private VUSB/roothub related state for the associated URB. */
typedef struct VUSBURBVUSBINT *PVUSBURBVUSB;

/**
 * Asynchronous USB request descriptor
 */
typedef struct VUSBURB
{
    /** URB magic value. */
    uint32_t        u32Magic;
    /** The USR state. */
    VUSBURBSTATE    enmState;
    /** Flag whether the URB is about to be completed,
     * either by the I/O thread or the cancellation worker.
     */
    volatile bool   fCompleting;
    /** URB description, can be null. intended for logging. */
    char           *pszDesc;

#ifdef RDESKTOP
    /** The next URB in rdesktop-vrdp's linked list */
    PVUSBURB        pNext;
    /** The previous URB in rdesktop-vrdp's linked list */
    PVUSBURB        pPrev;
    /** The vrdp handle for the URB */
    uint32_t        handle;
    /** Pointer used to find the usb proxy device */
    struct VUSBDEV *pDev;
#endif

    /** The VUSB stack private data. */
    PVUSBURBVUSB    pVUsb;
    /** Private host controller data associated with this URB. */
    PVUSBURBHCI     pHci;
    /** Pointer to the host controller transfer descriptor array. */
    PVUSBURBHCITD   paTds;

    /** The device data. */
    struct VUSBURBDEV
    {
        /** Pointer to private device specific data.  */
        void       *pvPrivate;
        /** Used by the device when linking the URB in some list of its own.   */
        PVUSBURB    pNext;
    } Dev;

    /** The device address.
     * This is set at allocation time. */
    uint8_t         DstAddress;

    /** The endpoint.
     * IN: Must be set before submitting the URB.
     * @remark This does not have the high bit (direction) set! */
    uint8_t         EndPt;
    /** The transfer type.
     * IN: Set at allocation time. */
    VUSBXFERTYPE    enmType;
    /** The transfer direction.
     * IN: Set at allocation time. */
    VUSBDIRECTION   enmDir;
    /** Indicates whether it is OK to receive/send less data than requested.
     * IN: Must be initialized before submitting the URB. */
    bool            fShortNotOk;
    /** The transfer status.
     * OUT: This is set when reaping the URB. */
    VUSBSTATUS      enmStatus;

    /** The relative starting frame for isochronous transfers.
     *  Zero indicates "transfer ASAP".
     * This is ignored when enmType isn't VUSBXFERTYPE_ISOC. */
    uint16_t        uStartFrameDelta;
    /** Flag indicating whether the start frame delta is relative
     *  to the previous transfer (false) or now (true).
     * This is ignored when enmType isn't VUSBXFERTYPE_ISOC. */
    bool            fStartRelToNow;
    /** The number of isochronous packets describe in aIsocPkts.
     * This is ignored when enmType isn't VUSBXFERTYPE_ISOC. */
    uint8_t         cIsocPkts;
    /** The iso packets within abData.
     * This is ignored when enmType isn't VUSBXFERTYPE_ISOC. */
    VUSBURBISOCPKT  aIsocPkts[8];

    /** The message length.
     * IN: The amount of data to send / receive - set at allocation time.
     * OUT: The amount of data sent / received. */
    uint32_t        cbData;
    /** The message data.
     * IN: On host to device transfers, the data to send.
     * OUT: On device to host transfers, the data to received.
     * This array has actually a size of VUsb.cbDataAllocated, not 8KB! */
    uint8_t         abData[8*_1K];
} VUSBURB;

/** The magic value of a valid VUSBURB. (Murakami Haruki) */
#define VUSBURB_MAGIC       UINT32_C(0x19490112)

/** @} */


/** @} */

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_vusb_h */
