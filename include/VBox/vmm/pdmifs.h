/** @file
 * PDM - Pluggable Device Manager, Interfaces.
 */

/*
 * Copyright (C) 2006-2025 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_vmm_pdmifs_h
#define VBOX_INCLUDED_vmm_pdmifs_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/sg.h>
#include <VBox/types.h>


RT_C_DECLS_BEGIN

/** @defgroup grp_pdm_interfaces    The PDM Interface Definitions
 * @ingroup grp_pdm
 *
 * For historical reasons (the PDMINTERFACE enum) a lot of interface was stuffed
 * together in this group instead, dragging stuff into global space that didn't
 * need to be there and making this file huge (>2500 lines).  Since we're using
 * UUIDs as interface identifiers (IIDs) now, no only generic PDM interface will
 * be added to this file.  Component specific interface should be defined in the
 * header file of that component.
 *
 * Interfaces consists of a method table (typedef'ed struct) and an interface
 * ID.  The typename of the method table should have an 'I' in it, be all
 * capitals and according to the rules, no underscores.  The interface ID is a
 * \#define constructed by appending '_IID' to the typename. The IID value is a
 * UUID string on the form "a2299c0d-b709-4551-aa5a-73f59ffbed74".  If you stick
 * to these rules, you can make use of the PDMIBASE_QUERY_INTERFACE and
 * PDMIBASE_RETURN_INTERFACE when querying interface and implementing
 * PDMIBASE::pfnQueryInterface respectively.
 *
 * In most interface descriptions the orientation of the interface is given as
 * 'down' or 'up'.  This refers to a model with the device on the top and the
 * drivers stacked below it.  Sometimes there is mention of 'main' or 'external'
 * which normally means the same, i.e. the Main or VBoxBFE API.  Picture the
 * orientation of 'main' as horizontal.
 *
 * @{
 */


/** @name PDMIBASE
 * @{
 */

/**
 * PDM Base Interface.
 *
 * Everyone implements this.
 */
typedef struct PDMIBASE
{
    /**
     * Queries an interface to the driver.
     *
     * @returns Pointer to interface.
     * @returns NULL if the interface was not supported by the driver.
     * @param   pInterface          Pointer to this interface structure.
     * @param   pszIID              The interface ID, a UUID string.
     * @thread  Any thread.
     */
    DECLR3CALLBACKMEMBER(void *, pfnQueryInterface,(struct PDMIBASE *pInterface, const char *pszIID));
} PDMIBASE;
/** PDMIBASE interface ID. */
#define PDMIBASE_IID                            "a2299c0d-b709-4551-aa5a-73f59ffbed74"

/**
 * Helper macro for querying an interface from PDMIBASE.
 *
 * @returns Correctly typed PDMIBASE::pfnQueryInterface return value.
 *
 * @param   pIBase          Pointer to the base interface.
 * @param   InterfaceType   The interface type name.  The interface ID is
 *                          derived from this by appending _IID.
 */
#define PDMIBASE_QUERY_INTERFACE(pIBase, InterfaceType)  \
    ( (InterfaceType *)(pIBase)->pfnQueryInterface(pIBase, InterfaceType##_IID ) )

/**
 * Helper macro for implementing PDMIBASE::pfnQueryInterface.
 *
 * Return @a pInterface if @a pszIID matches the @a InterfaceType.  This will
 * perform basic type checking.
 *
 * @param   pszIID          The ID of the interface that is being queried.
 * @param   InterfaceType   The interface type name.  The interface ID is
 *                          derived from this by appending _IID.
 * @param   pInterface      The interface address expression.
 */
#define PDMIBASE_RETURN_INTERFACE(pszIID, InterfaceType, pInterface)  \
    do { \
        if (RTUuidCompare2Strs((pszIID), InterfaceType##_IID) == 0) \
        { \
            P##InterfaceType pReturnInterfaceTypeCheck = (pInterface); \
            return pReturnInterfaceTypeCheck; \
        } \
    } while (0)

/** @} */


/** @name PDMIBASERC
 * @{
 */

/**
 * PDM Base Interface for querying ring-mode context interfaces in
 * ring-3.
 *
 * This is mandatory for drivers present in raw-mode context.
 */
typedef struct PDMIBASERC
{
    /**
     * Queries an ring-mode context interface to the driver.
     *
     * @returns Pointer to interface.
     * @returns NULL if the interface was not supported by the driver.
     * @param   pInterface          Pointer to this interface structure.
     * @param   pszIID              The interface ID, a UUID string.
     * @thread  Any thread.
     */
    DECLR3CALLBACKMEMBER(RTRCPTR, pfnQueryInterface,(struct PDMIBASERC *pInterface, const char *pszIID));
} PDMIBASERC;
/** Pointer to a PDM Base Interface for query ring-mode context interfaces. */
typedef PDMIBASERC *PPDMIBASERC;
/** PDMIBASERC interface ID. */
#define PDMIBASERC_IID                          "f6a6c649-6cb3-493f-9737-4653f221aeca"

/**
 * Helper macro for querying an interface from PDMIBASERC.
 *
 * @returns PDMIBASERC::pfnQueryInterface return value.
 *
 * @param   pIBaseRC        Pointer to the base raw-mode context interface.  Can
 *                          be NULL.
 * @param   InterfaceType   The interface type base name, no trailing RC.  The
 *                          interface ID is derived from this by appending _IID.
 *
 * @remarks Unlike PDMIBASE_QUERY_INTERFACE, this macro is not able to do any
 *          implicit type checking for you.
 */
#define PDMIBASERC_QUERY_INTERFACE(pIBaseRC, InterfaceType)  \
    ( (P##InterfaceType##RC)((pIBaseRC) ? (pIBaseRC)->pfnQueryInterface(pIBaseRC, InterfaceType##_IID) : NIL_RTRCPTR) )

/**
 * Helper macro for implementing PDMIBASERC::pfnQueryInterface.
 *
 * Return @a pInterface if @a pszIID matches the @a InterfaceType.  This will
 * perform basic type checking.
 *
 * @param   pIns            Pointer to the instance data.
 * @param   pszIID          The ID of the interface that is being queried.
 * @param   InterfaceType   The interface type base name, no trailing RC.  The
 *                          interface ID is derived from this by appending _IID.
 * @param   pInterface      The interface address expression.  This must resolve
 *                          to some address within the instance data.
 * @remarks Don't use with PDMIBASE.
 */
#define PDMIBASERC_RETURN_INTERFACE(pIns, pszIID, InterfaceType, pInterface)  \
    do { \
        Assert((uintptr_t)pInterface - PDMINS_2_DATA(pIns, uintptr_t) < _4M); \
        if (RTUuidCompare2Strs((pszIID), InterfaceType##_IID) == 0) \
        { \
            InterfaceType##RC *pReturnInterfaceTypeCheck = (pInterface); \
            return (uintptr_t)pReturnInterfaceTypeCheck \
                 - PDMINS_2_DATA(pIns, uintptr_t) \
                 + PDMINS_2_DATA_RCPTR(pIns); \
        } \
    } while (0)

/** @} */


/** @name PDMIBASER0
 * @{
 */

/**
 * PDM Base Interface for querying ring-0 interfaces in ring-3.
 *
 * This is mandatory for drivers present in ring-0 context.
 */
typedef struct PDMIBASER0
{
    /**
     * Queries an ring-0 interface to the driver.
     *
     * @returns Pointer to interface.
     * @returns NULL if the interface was not supported by the driver.
     * @param   pInterface          Pointer to this interface structure.
     * @param   pszIID              The interface ID, a UUID string.
     * @thread  Any thread.
     */
    DECLR3CALLBACKMEMBER(RTR0PTR, pfnQueryInterface,(struct PDMIBASER0 *pInterface, const char *pszIID));
} PDMIBASER0;
/** Pointer to a PDM Base Interface for query ring-0 context interfaces. */
typedef PDMIBASER0 *PPDMIBASER0;
/** PDMIBASER0 interface ID. */
#define PDMIBASER0_IID                          "9c9b99b8-7f53-4f59-a3c2-5bc9659c7944"

/**
 * Helper macro for querying an interface from PDMIBASER0.
 *
 * @returns PDMIBASER0::pfnQueryInterface return value.
 *
 * @param   pIBaseR0        Pointer to the base ring-0 interface.  Can be NULL.
 * @param   InterfaceType   The interface type base name, no trailing R0.  The
 *                          interface ID is derived from this by appending _IID.
 *
 * @remarks Unlike PDMIBASE_QUERY_INTERFACE, this macro is not able to do any
 *          implicit type checking for you.
 */
#define PDMIBASER0_QUERY_INTERFACE(pIBaseR0, InterfaceType)  \
    ( (P##InterfaceType##R0)((pIBaseR0) ? (pIBaseR0)->pfnQueryInterface(pIBaseR0, InterfaceType##_IID) : NIL_RTR0PTR) )

/**
 * Helper macro for implementing PDMIBASER0::pfnQueryInterface.
 *
 * Return @a pInterface if @a pszIID matches the @a InterfaceType.  This will
 * perform basic type checking.
 *
 * @param   pIns            Pointer to the instance data.
 * @param   pszIID          The ID of the interface that is being queried.
 * @param   InterfaceType   The interface type base name, no trailing R0.  The
 *                          interface ID is derived from this by appending _IID.
 * @param   pInterface      The interface address expression.  This must resolve
 *                          to some address within the instance data.
 * @remarks Don't use with PDMIBASE.
 */
#define PDMIBASER0_RETURN_INTERFACE(pIns, pszIID, InterfaceType, pInterface)  \
    do { \
        Assert((uintptr_t)pInterface - PDMINS_2_DATA(pIns, uintptr_t) < _4M); \
        if (RTUuidCompare2Strs((pszIID), InterfaceType##_IID) == 0) \
        { \
            InterfaceType##R0 *pReturnInterfaceTypeCheck = (pInterface); \
            return (uintptr_t)pReturnInterfaceTypeCheck \
                 - PDMINS_2_DATA(pIns, uintptr_t) \
                 + PDMINS_2_DATA_R0PTR(pIns); \
        } \
    } while (0)

/** @} */


/**
 * Dummy interface.
 *
 * This is used to typedef other dummy interfaces. The purpose of a dummy
 * interface is to validate the logical function of a driver/device and
 * full a natural interface pair.
 */
typedef struct PDMIDUMMY
{
    RTHCPTR pvDummy;
} PDMIDUMMY;


/** Pointer to a mouse port interface. */
typedef struct PDMIMOUSEPORT *PPDMIMOUSEPORT;
/**
 * Mouse port interface (down).
 * Pair with PDMIMOUSECONNECTOR.
 */
typedef struct PDMIMOUSEPORT
{
    /**
     * Puts a mouse event.
     *
     * This is called by the source of mouse events.  The event will be passed up
     * until the topmost driver, which then calls the registered event handler.
     *
     * @returns VBox status code.  Return VERR_TRY_AGAIN if you cannot process the
     *          event now and want it to be repeated at a later point.
     *
     * @param   pInterface     Pointer to this interface structure.
     * @param   dx             The X delta.
     * @param   dy             The Y delta.
     * @param   dz             The Z delta.
     * @param   dw             The W (horizontal scroll button) delta.
     * @param   fButtons       The button states, see the PDMIMOUSEPORT_BUTTON_* \#defines.
     */
    DECLR3CALLBACKMEMBER(int, pfnPutEvent,(PPDMIMOUSEPORT pInterface,
                                           int32_t dx, int32_t dy, int32_t dz,
                                           int32_t dw, uint32_t fButtons));
    /**
     * Puts an absolute mouse event.
     *
     * This is called by the source of mouse events.  The event will be passed up
     * until the topmost driver, which then calls the registered event handler.
     *
     * @returns VBox status code.  Return VERR_TRY_AGAIN if you cannot process the
     *          event now and want it to be repeated at a later point.
     *
     * @param   pInterface     Pointer to this interface structure.
     * @param   x              The X value, in the range 0 to 0xffff.
     * @param   y              The Y value, in the range 0 to 0xffff.
     * @param   dz             The Z delta.
     * @param   dw             The W (horizontal scroll button) delta.
     * @param   fButtons       The button states, see the PDMIMOUSEPORT_BUTTON_* \#defines.
     */
    DECLR3CALLBACKMEMBER(int, pfnPutEventAbs,(PPDMIMOUSEPORT pInterface,
                                              uint32_t x, uint32_t y,
                                              int32_t dz, int32_t dw,
                                              uint32_t fButtons));
    /**
     * Puts a multi-touch absolute (touchscreen) event.
     *
     * @returns VBox status code. Return VERR_TRY_AGAIN if you cannot process the
     *          event now and want it to be repeated at a later point.
     *
     * @param   pInterface          Pointer to this interface structure.
     * @param   cContacts           How many touch contacts in this event.
     * @param   pau64Contacts       Pointer to array of packed contact information.
     *                              Each 64bit element contains:
     *                              Bits 0..15:  X coordinate in pixels (signed).
     *                              Bits 16..31: Y coordinate in pixels (signed).
     *                              Bits 32..39: contact identifier.
     *                              Bit 40:      "in contact" flag, which indicates that
     *                                           there is a contact with the touch surface.
     *                              Bit 41:      "in range" flag, the contact is close enough
     *                                           to the touch surface.
     *                              All other bits are reserved for future use and must be set to 0.
     * @param   u32ScanTime         Timestamp of this event in milliseconds. Only relative
     *                              time between event is important.
     */
    DECLR3CALLBACKMEMBER(int, pfnPutEventTouchScreen,(PPDMIMOUSEPORT pInterface,
                                                      uint8_t cContacts,
                                                      const uint64_t *pau64Contacts,
                                                      uint32_t u32ScanTime));

    /**
     * Puts a multi-touch relative (touchpad) event.
     *
     * @returns VBox status code. Return VERR_TRY_AGAIN if you cannot process the
     *          event now and want it to be repeated at a later point.
     *
     * @param   pInterface          Pointer to this interface structure.
     * @param   cContacts           How many touch contacts in this event.
     * @param   pau64Contacts       Pointer to array of packed contact information.
     *                              Each 64bit element contains:
     *                              Bits 0..15:  Normalized X coordinate (range: 0 - 0xffff).
     *                              Bits 16..31: Normalized Y coordinate (range: 0 - 0xffff).
     *                              Bits 32..39: contact identifier.
     *                              Bit 40:      "in contact" flag, which indicates that
     *                                           there is a contact with the touch surface.
     *                              All other bits are reserved for future use and must be set to 0.
     * @param   u32ScanTime         Timestamp of this event in milliseconds. Only relative
     *                              time between event is important.
     */

    DECLR3CALLBACKMEMBER(int, pfnPutEventTouchPad,(PPDMIMOUSEPORT pInterface,
                                                   uint8_t cContacts,
                                                   const uint64_t *pau64Contacts,
                                                   uint32_t u32ScanTime));
} PDMIMOUSEPORT;
/** PDMIMOUSEPORT interface ID. */
#define PDMIMOUSEPORT_IID                       "d2bb54b7-d877-441b-9d25-d2d3329465c2"

/** Mouse button defines for PDMIMOUSEPORT::pfnPutEvent.
 * @{ */
#define PDMIMOUSEPORT_BUTTON_LEFT   RT_BIT(0)
#define PDMIMOUSEPORT_BUTTON_RIGHT  RT_BIT(1)
#define PDMIMOUSEPORT_BUTTON_MIDDLE RT_BIT(2)
#define PDMIMOUSEPORT_BUTTON_X1     RT_BIT(3)
#define PDMIMOUSEPORT_BUTTON_X2     RT_BIT(4)
/** @} */


/** Pointer to a mouse connector interface. */
typedef struct PDMIMOUSECONNECTOR *PPDMIMOUSECONNECTOR;
/**
 * Mouse connector interface (up).
 * Pair with PDMIMOUSEPORT.
 */
typedef struct PDMIMOUSECONNECTOR
{
    /**
     * Notifies the the downstream driver of changes to the reporting modes
     * supported by the driver
     *
     * @param   pInterface      Pointer to this interface structure.
     * @param   fRelative       Whether relative mode is currently supported.
     * @param   fAbsolute       Whether absolute mode is currently supported.
     * @param   fMTAbsolute     Whether absolute multi-touch mode is currently supported.
     * @param   fMTRelative     Whether relative multi-touch mode is currently supported.
     */
    DECLR3CALLBACKMEMBER(void, pfnReportModes,(PPDMIMOUSECONNECTOR pInterface, bool fRelative, bool fAbsolute, bool fMTAbsolute, bool fMTRelative));

    /**
     * Flushes the mouse queue if it contains pending events.
     *
     * @param   pInterface      Pointer to this interface structure.
     */
    DECLR3CALLBACKMEMBER(void, pfnFlushQueue,(PPDMIMOUSECONNECTOR pInterface));

} PDMIMOUSECONNECTOR;
/** PDMIMOUSECONNECTOR interface ID.  */
#define PDMIMOUSECONNECTOR_IID                  "ce64d7bd-fa8f-41d1-a6fb-d102a2d6bffe"


/** Flags for PDMIKEYBOARDPORT::pfnPutEventHid.
 * @{ */
#define PDMIKBDPORT_KEY_UP          RT_BIT(31)  /** Key release event if set. */
#define PDMIKBDPORT_RELEASE_KEYS    RT_BIT(30)  /** Force all keys to be released. */
/** @} */

/** USB HID usage pages understood by PDMIKEYBOARDPORT::pfnPutEventHid.
 * @{ */
#define USB_HID_DC_PAGE             1       /** USB HID Generic Desktop Control Usage Page. */
#define USB_HID_KB_PAGE             7       /** USB HID Keyboard Usage Page. */
#define USB_HID_CC_PAGE             12      /** USB HID Consumer Control Usage Page. */
/** @} */


/** Pointer to a keyboard port interface. */
typedef struct PDMIKEYBOARDPORT *PPDMIKEYBOARDPORT;
/**
 * Keyboard port interface (down).
 * Pair with PDMIKEYBOARDCONNECTOR.
 */
typedef struct PDMIKEYBOARDPORT
{
    /**
     * Puts a scan code based keyboard event.
     *
     * This is called by the source of keyboard events. The event will be passed up
     * until the topmost driver, which then calls the registered event handler.
     *
     * @returns VBox status code.  Return VERR_TRY_AGAIN if you cannot process the
     *          event now and want it to be repeated at a later point.
     *
     * @param   pInterface          Pointer to this interface structure.
     * @param   u8ScanCode          The scan code to queue.
     */
    DECLR3CALLBACKMEMBER(int, pfnPutEventScan,(PPDMIKEYBOARDPORT pInterface, uint8_t u8KeyCode));

    /**
     * Puts a USB HID usage ID based keyboard event.
     *
     * This is called by the source of keyboard events. The event will be passed up
     * until the topmost driver, which then calls the registered event handler.
     *
     * @returns VBox status code.  Return VERR_TRY_AGAIN if you cannot process the
     *          event now and want it to be repeated at a later point.
     *
     * @param   pInterface          Pointer to this interface structure.
     * @param   idUsage             The HID usage code event to queue.
     */
    DECLR3CALLBACKMEMBER(int, pfnPutEventHid,(PPDMIKEYBOARDPORT pInterface, uint32_t idUsage));

    /**
     * Forcibly releases any pressed keys.
     *
     * This is called by the source of keyboard events in situations when a full
     * release of all currently pressed keys must be forced, e.g. when activating
     * a different keyboard, or when key-up events may have been lost.
     *
     * @returns VBox status code.
     *
     * @param   pInterface          Pointer to this interface structure.
     */
    DECLR3CALLBACKMEMBER(int, pfnReleaseKeys,(PPDMIKEYBOARDPORT pInterface));
} PDMIKEYBOARDPORT;
/** PDMIKEYBOARDPORT interface ID. */
#define PDMIKEYBOARDPORT_IID                    "2a0844f0-410b-40ab-a6ed-6575f3aa3e29"


/**
 * Keyboard LEDs.
 */
typedef enum PDMKEYBLEDS
{
    /** No leds. */
    PDMKEYBLEDS_NONE             = 0x0000,
    /** Num Lock */
    PDMKEYBLEDS_NUMLOCK          = 0x0001,
    /** Caps Lock */
    PDMKEYBLEDS_CAPSLOCK         = 0x0002,
    /** Scroll Lock */
    PDMKEYBLEDS_SCROLLLOCK       = 0x0004
} PDMKEYBLEDS;

/** Pointer to keyboard connector interface. */
typedef struct PDMIKEYBOARDCONNECTOR *PPDMIKEYBOARDCONNECTOR;
/**
 * Keyboard connector interface (up).
 * Pair with PDMIKEYBOARDPORT
 */
typedef struct PDMIKEYBOARDCONNECTOR
{
    /**
     * Notifies the the downstream driver about an LED change initiated by the guest.
     *
     * @param   pInterface      Pointer to this interface structure.
     * @param   enmLeds         The new led mask.
     */
    DECLR3CALLBACKMEMBER(void, pfnLedStatusChange,(PPDMIKEYBOARDCONNECTOR pInterface, PDMKEYBLEDS enmLeds));

    /**
     * Notifies the the downstream driver of changes in driver state.
     *
     * @param   pInterface      Pointer to this interface structure.
     * @param   fActive         Whether interface wishes to get "focus".
     */
    DECLR3CALLBACKMEMBER(void, pfnSetActive,(PPDMIKEYBOARDCONNECTOR pInterface, bool fActive));

    /**
     * Flushes the keyboard queue if it contains pending events.
     *
     * @param   pInterface      Pointer to this interface structure.
     */
    DECLR3CALLBACKMEMBER(void, pfnFlushQueue,(PPDMIKEYBOARDCONNECTOR pInterface));

} PDMIKEYBOARDCONNECTOR;
/** PDMIKEYBOARDCONNECTOR interface ID. */
#define PDMIKEYBOARDCONNECTOR_IID               "db3f7bd5-953e-436f-9f8e-077905a92d82"


/**
 * 'enmFormat' parameter for PDMIDISPLAYPORT::pfnCreateOutputTargetAsync.
 */
typedef enum PDMDISPLAYOUTPUTTARGETFORMAT
{
    /** Invalid 0 entry. */
    PDMDISPLAYOUTPUTTARGETFORMAT_INVALID = 0,
    /** 32 bits per pixel with the same size as the guest screen. */
    PDMDISPLAYOUTPUTTARGETFORMAT_B8G8R8X8_I,
    /** Y plane followed by U and V planes. */
    PDMDISPLAYOUTPUTTARGETFORMAT_YUVI420,
    /** The usual 32-bit paranoia. */
    PDMDISPLAYOUTPUTTARGETFORMAT_32BIT_HACK = 0x7fffffff
} PDMDISPLAYOUTPUTTARGETFORMAT;

/**
 * Description of an display output target.
 * Output targets contain guest screen image.
 * Example of an output targets is an YUV scaled image for video encoding.
 */
typedef struct PDMDISPLAYOUTPUTTARGETDESC
{
    /** Screen index the target was created for. */
    uint32_t idScreen;
    /** Format of the screen image. */
    PDMDISPLAYOUTPUTTARGETFORMAT enmFormat;
    /** Width of the target. */
    uint32_t cWidth;
    /** Height of the target. */
    uint32_t cHeight;
    /** Pointer to a counter which is incremented each time the target is updated.
     * If the target was never updated then the counter value is 0.
     */
    uint64_t volatile const *pu64UpdateSequenceNumber;
    /** Size of memory buffer which contains the guest image in the specified format. */
    uint32_t cbOutputBuffer;
    /** Memory buffer which contains the guest image in the specified format. */
    void *pvOutputBuffer;
} PDMDISPLAYOUTPUTTARGETDESC;

/** @name Flags for PDMIDISPLAYPORT::pfnCreateOutputTargetAsync.
 * @{ */
/** The output target image dimensions are specified by cWidth and cHeight parameters.
 * Otherwise the size of the image is equal to the guest resolution.
 */
#define PDM_DISPLAY_OUTPUT_TARGET_FIXED_SIZE                  RT_BIT(0)
/** @} */

/** Pointer to a display port interface. */
typedef struct PDMIDISPLAYPORT *PPDMIDISPLAYPORT;
/**
 * Display port interface (down).
 * Pair with PDMIDISPLAYCONNECTOR.
 */
typedef struct PDMIDISPLAYPORT
{
    /**
     * Update the display with any changed regions.
     *
     * Flushes any display changes to the memory pointed to by the
     * PDMIDISPLAYCONNECTOR interface and calles PDMIDISPLAYCONNECTOR::pfnUpdateRect()
     * while doing so.
     *
     * @returns VBox status code.
     * @param   pInterface          Pointer to this interface.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnUpdateDisplay,(PPDMIDISPLAYPORT pInterface));

    /**
     * Update the entire display.
     *
     * Flushes the entire display content to the memory pointed to by the
     * PDMIDISPLAYCONNECTOR interface and calles PDMIDISPLAYCONNECTOR::pfnUpdateRect().
     *
     * @returns VBox status code.
     * @param   pInterface          Pointer to this interface.
     * @param   fFailOnResize       Fail is a resize is pending.
     * @thread  The emulation thread - bird sees no need for EMT here!
     */
    DECLR3CALLBACKMEMBER(int, pfnUpdateDisplayAll,(PPDMIDISPLAYPORT pInterface, bool fFailOnResize));

    /**
     * Return the current guest resolution and color depth in bits per pixel (bpp).
     *
     * As the graphics card is able to provide display updates with the bpp
     * requested by the host, this method can be used to query the actual
     * guest color depth.
     *
     * @returns VBox status code.
     * @param   pInterface         Pointer to this interface.
     * @param   pcBits             Where to store the current guest color depth.
     * @param   pcx                Where to store the horizontal resolution.
     * @param   pcy                Where to store the vertical resolution.
     * @thread  Any thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnQueryVideoMode,(PPDMIDISPLAYPORT pInterface, uint32_t *pcBits, uint32_t *pcx, uint32_t *pcy));

    /**
     * Sets the refresh rate and restart the timer.
     * The rate is defined as the minimum interval between the return of
     * one PDMIDISPLAYPORT::pfnRefresh() call to the next one.
     *
     * The interval timer will be restarted by this call. So at VM startup
     * this function must be called to start the refresh cycle. The refresh
     * rate is not saved, but have to be when resuming a loaded VM state.
     *
     * @returns VBox status code.
     * @param   pInterface          Pointer to this interface.
     * @param   cMilliesInterval    Number of millis between two refreshes.
     * @thread  Any thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnSetRefreshRate,(PPDMIDISPLAYPORT pInterface, uint32_t cMilliesInterval));

    /**
     * Create a 32-bbp screenshot of the display.
     *
     * This will allocate and return a 32-bbp bitmap. Size of the bitmap scanline in bytes is 4*width.
     *
     * The allocated bitmap buffer must be freed with pfnFreeScreenshot.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   ppbData             Where to store the pointer to the allocated
     *                              buffer.
     * @param   pcbData             Where to store the actual size of the bitmap.
     * @param   pcx                 Where to store the width of the bitmap.
     * @param   pcy                 Where to store the height of the bitmap.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnTakeScreenshot,(PPDMIDISPLAYPORT pInterface, uint8_t **ppbData, size_t *pcbData, uint32_t *pcx, uint32_t *pcy));

    /**
     * Free screenshot buffer.
     *
     * This will free the memory buffer allocated by pfnTakeScreenshot.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   pbData              Pointer to the buffer returned by
     *                              pfnTakeScreenshot.
     * @thread  Any.
     */
    DECLR3CALLBACKMEMBER(void, pfnFreeScreenshot,(PPDMIDISPLAYPORT pInterface, uint8_t *pbData));

    /**
     * Copy bitmap to the display.
     *
     * This will convert and copy a 32-bbp bitmap (with dword aligned scanline length) to
     * the memory pointed to by the PDMIDISPLAYCONNECTOR interface.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   pvData              Pointer to the bitmap bits.
     * @param   x                   The upper left corner x coordinate of the destination rectangle.
     * @param   y                   The upper left corner y coordinate of the destination rectangle.
     * @param   cx                  The width of the source and destination rectangles.
     * @param   cy                  The height of the source and destination rectangles.
     * @thread  The emulation thread.
     * @remark  This is just a convenience for using the bitmap conversions of the
     *          graphics device.
     */
    DECLR3CALLBACKMEMBER(int, pfnDisplayBlt,(PPDMIDISPLAYPORT pInterface, const void *pvData, uint32_t x, uint32_t y, uint32_t cx, uint32_t cy));

    /**
     * Render a rectangle from guest VRAM to Framebuffer.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   x                   The upper left corner x coordinate of the rectangle to be updated.
     * @param   y                   The upper left corner y coordinate of the rectangle to be updated.
     * @param   cx                  The width of the rectangle to be updated.
     * @param   cy                  The height of the rectangle to be updated.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnUpdateDisplayRect,(PPDMIDISPLAYPORT pInterface, int32_t x, int32_t y, uint32_t cx, uint32_t cy));

    /**
     * Inform the VGA device whether the Display is directly using the guest VRAM and there is no need
     * to render the VRAM to the framebuffer memory.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   fRender             Whether the VRAM content must be rendered to the framebuffer.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnSetRenderVRAM,(PPDMIDISPLAYPORT pInterface, bool fRender));

    /**
     * Render a bitmap rectangle from source to target buffer.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   cx                  The width of the rectangle to be copied.
     * @param   cy                  The height of the rectangle to be copied.
     * @param   pbSrc               Source frame buffer 0,0.
     * @param   xSrc                The upper left corner x coordinate of the source rectangle.
     * @param   ySrc                The upper left corner y coordinate of the source rectangle.
     * @param   cxSrc               The width of the source frame buffer.
     * @param   cySrc               The height of the source frame buffer.
     * @param   cbSrcLine           The line length of the source frame buffer.
     * @param   cSrcBitsPerPixel    The pixel depth of the source.
     * @param   pbDst               Destination frame buffer 0,0.
     * @param   xDst                The upper left corner x coordinate of the destination rectangle.
     * @param   yDst                The upper left corner y coordinate of the destination rectangle.
     * @param   cxDst               The width of the destination frame buffer.
     * @param   cyDst               The height of the destination frame buffer.
     * @param   cbDstLine           The line length of the destination frame buffer.
     * @param   cDstBitsPerPixel    The pixel depth of the destination.
     * @thread  The emulation thread - bird sees no need for EMT here!
     */
    DECLR3CALLBACKMEMBER(int, pfnCopyRect,(PPDMIDISPLAYPORT pInterface, uint32_t cx, uint32_t cy,
        const uint8_t *pbSrc, int32_t xSrc, int32_t ySrc, uint32_t cxSrc, uint32_t cySrc, uint32_t cbSrcLine, uint32_t cSrcBitsPerPixel,
        uint8_t       *pbDst, int32_t xDst, int32_t yDst, uint32_t cxDst, uint32_t cyDst, uint32_t cbDstLine, uint32_t cDstBitsPerPixel));

    /**
     * Inform the VGA device of viewport changes (as a result of e.g. scrolling).
     *
     * @param   pInterface          Pointer to this interface.
     * @param   idScreen            The screen updates are for.
     * @param   x                   The upper left corner x coordinate of the new viewport rectangle
     * @param   y                   The upper left corner y coordinate of the new viewport rectangle
     * @param   cx                  The width of the new viewport rectangle
     * @param   cy                  The height of the new viewport rectangle
     * @thread  GUI thread?
     *
     * @remarks Is allowed to be NULL.
     */
    DECLR3CALLBACKMEMBER(void, pfnSetViewport,(PPDMIDISPLAYPORT pInterface,
                                               uint32_t idScreen, uint32_t x, uint32_t y, uint32_t cx, uint32_t cy));

    /**
     * Send a video mode hint to the VGA device.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   cx                  The X resolution.
     * @param   cy                  The Y resolution.
     * @param   cBPP                The bit count.
     * @param   iDisplay            The screen number.
     * @param   dx                  X offset into the virtual framebuffer or ~0.
     * @param   dy                  Y offset into the virtual framebuffer or ~0.
     * @param   fEnabled            Is this screen currently enabled?
     * @param   fNotifyGuest        Should the device send the guest an IRQ?
     *                              Set for the last hint of a series.
     * @thread  Schedules on the emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnSendModeHint, (PPDMIDISPLAYPORT pInterface, uint32_t cx, uint32_t cy,
                                                uint32_t cBPP, uint32_t iDisplay, uint32_t dx,
                                                uint32_t dy, uint32_t fEnabled, uint32_t fNotifyGuest));

    /**
     * Send the guest a notification about host cursor capabilities changes.
     *
     * @param   pInterface             Pointer to this interface.
     * @param   fSupportsRenderCursor  Whether the host can draw the guest cursor
     *                                 using the host one provided the location matches.
     * @param   fSupportsMoveCursor    Whether the host can draw the guest cursor
     *                                 itself at any position.  Implies RenderCursor.
     * @thread  Any.
     */
    DECLR3CALLBACKMEMBER(void, pfnReportHostCursorCapabilities, (PPDMIDISPLAYPORT pInterface, bool fSupportsRenderCursor, bool fSupportsMoveCursor));

    /**
     * Tell the graphics device about the host cursor position.
     *
     * @param   pInterface   Pointer to this interface.
     * @param   x            X offset into the cursor range.
     * @param   y            Y offset into the cursor range.
     * @param   fOutOfRange  The host pointer is out of all guest windows, so
     *                       X and Y do not currently have meaningful value.
     * @thread  Any.
     */
    DECLR3CALLBACKMEMBER(void, pfnReportHostCursorPosition, (PPDMIDISPLAYPORT pInterface, uint32_t x, uint32_t y, bool fOutOfRange));

    /**
     * Notify the graphics device about the monitor positions since the ones we get
     * from vmwgfx FIFO are not correct.
     *
     * In an ideal universe this method should not be here.
     *
     * @param   pInterface      Pointer to this interface.
     * @param   cPositions      Number of monitor positions.
     * @param   paPositions     Monitor positions (offsets/origins) array.
     * @thread  Any (EMT).
     * @sa      PDMIVMMDEVCONNECTOR::pfnUpdateMonitorPositions
     */
    DECLR3CALLBACKMEMBER(void, pfnReportMonitorPositions, (PPDMIDISPLAYPORT pInterface, uint32_t cPositions,
                                                           PCRTPOINT paPositions));

    /**
     * Create an unique output target token which will be used for creating a new target.
     *
     * @returns VBox status code.
     * @param   pInterface            Pointer to this interface.
     * @param   pu64OutputTargetToken Pointer to returned token.
     * @thread  Any thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnGetUniqueOutputTargetToken, (PPDMIDISPLAYPORT pInterface,
                                                              uint64_t *pu64OutputTargetToken));

    /**
     * Get the output target token of the default output target which is used by the device
     * as the screen bitmap.
     *
     * This should be called only as part of screen resize processing (pfnVBVAResize handler).
     * This method will return an error code if called not on the graphics device thread.
     *
     * The reference count of the returned token is incremented.
     *
     * @returns VBox status code.
     * @param   pInterface            Pointer to this interface.
     * @param   idScreen              The output target token is requested for this screen.
     * @param   pu64OutputTargetToken Pointer to returned token.
     * @thread  The graphics device thread: FIFO for the VMSVGA device.
     */
    DECLR3CALLBACKMEMBER(int, pfnQueryDefaultOutputTargetToken, (PPDMIDISPLAYPORT pInterface,
                                                                 uint32_t idScreen,
                                                                 uint64_t *pu64OutputTargetToken));

    /**
     * Create a new output target.
     *
     * This should be called only as part of screen resize processing (pfnVBVAResize handler).
     * This method will return an error code if called not on the graphics device thread.
     *
     * Once created the target has reference count equal to 1.
     *
     * @returns VBox status code.
     * @param   pInterface            Pointer to this interface.
     * @param   idScreen              The output target is being created for this screen.
     * @param   enmFormat             Format of the output target (PDMDISPLAYOUTPUTTARGETFORMAT).
     * @param   cWidth                Width of the target image (can be different from the guest screen resolution).
     * @param   cHeight               Height of the target image (can be different from the guest screen resolution).
     * @param   uFlags                Option for the target (PDM_DISPLAY_OUTPUT_TARGET_*).
     * @param   u64OutputTargetToken  Unique token previously generated by pfnGetUniqueOutputTargetToken.
     * @thread  The graphics device thread: FIFO for the VMSVGA device.
     */
    DECLR3CALLBACKMEMBER(int, pfnCreateOutputTarget, (PPDMIDISPLAYPORT pInterface,
                                                      uint32_t idScreen,
                                                      PDMDISPLAYOUTPUTTARGETFORMAT enmFormat,
                                                      uint32_t cWidth, uint32_t cHeight,
                                                      uint32_t uFlags, uint64_t u64OutputTargetToken));

    /**
     * Create a new output target asynchronously.
     *
     * The target is created asynchronously by a graphics device thread.
     * PDMIDISPLAYCONNECTOR::pfnOnOutputTargetCreated will be called when the target creation is completed.
     * Once created the target has reference counter equal to 1.
     *
     * @returns VBox status code.
     * @param   pInterface            Pointer to this interface.
     * @param   idScreen              The output target is being created for this screen.
     * @param   enmFormat             Format of the output target (PDMDISPLAYOUTPUTTARGETFORMAT).
     * @param   cWidth                Width of the target image (can be different from the guest screen resolution).
     * @param   cHeight               Height of the target image (can be different from the guest screen resolution).
     * @param   uFlags                Option for the target (PDM_DISPLAY_OUTPUT_TARGET_*).
     * @param   u64OutputTargetToken  Unique token previously generated by pfnGetUniqueOutputTargetToken.
     * @thread  Any thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnCreateOutputTargetAsync, (PPDMIDISPLAYPORT pInterface,
                                                           uint32_t idScreen,
                                                           PDMDISPLAYOUTPUTTARGETFORMAT enmFormat,
                                                           uint32_t cWidth, uint32_t cHeight,
                                                           uint32_t uFlags, uint64_t u64OutputTargetToken));

    /**
     * Get description of an output target.
     *
     * @returns VBox status code.
     * @param   pInterface            Pointer to this interface.
     * @param   u64OutputTargetToken  Unique token previously used for creating a target.
     * @param   pDescOut              Where to store the description. The description is valid as long as
     *                                the reference of the target is greater than 0.
     * @thread  Any thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnOutputTargetDesc, (PPDMIDISPLAYPORT pInterface,
                                                    uint64_t u64OutputTargetToken,
                                                    PDMDISPLAYOUTPUTTARGETDESC *pDescOut));

    /**
     * Increment reference counter of an output target.
     *
     * @param   pInterface            Pointer to this interface.
     * @param   u64OutputTargetToken  Unique token previously used for creating a target.
     * @thread  Any thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnRetainOutputTarget, (PPDMIDISPLAYPORT pInterface,
                                                       uint64_t u64OutputTargetToken));

    /**
     * Decrement reference counter of an output target.
     *
     * @param   pInterface            Pointer to this interface.
     * @param   u64OutputTargetToken  Unique token previously used for creating a target.
     * @thread  Any thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnReleaseOutputTarget, (PPDMIDISPLAYPORT pInterface,
                                                        uint64_t u64OutputTargetToken));

} PDMIDISPLAYPORT;
/** PDMIDISPLAYPORT interface ID. */
#define PDMIDISPLAYPORT_IID                     "ddf9d469-96f0-4bc2-a287-cf1ac12d9227"

/** @name Flags for PDMIDISPLAYCONNECTOR::pfnVBVAReportCursorPosition.
 * @{ */
/** Is the data in the report valid? */
#define VBVA_CURSOR_VALID_DATA                              RT_BIT(0)
/** Is the cursor position reported relative to a particular guest screen? */
#define VBVA_CURSOR_SCREEN_RELATIVE                         RT_BIT(1)
/** @} */

/** Pointer to a 3D graphics notification. */
typedef struct VBOX3DNOTIFY VBOX3DNOTIFY;
/** Pointer to a VBVA command header. */
typedef struct VBVACMDHDR *PVBVACMDHDR;
/** Pointer to a const VBVA command header. */
typedef const struct VBVACMDHDR *PCVBVACMDHDR;
/** Pointer to a VBVA screen information. */
typedef struct VBVAINFOSCREEN *PVBVAINFOSCREEN;
/** Pointer to a const VBVA screen information. */
typedef const struct VBVAINFOSCREEN *PCVBVAINFOSCREEN;
/** Pointer to a VBVA guest VRAM area information. */
typedef struct VBVAINFOVIEW *PVBVAINFOVIEW;
/** Pointer to a const VBVA guest VRAM area information. */
typedef const struct VBVAINFOVIEW *PCVBVAINFOVIEW;
typedef struct VBVAHOSTFLAGS *PVBVAHOSTFLAGS;

/** Pointer to a display connector interface. */
typedef struct PDMIDISPLAYCONNECTOR *PPDMIDISPLAYCONNECTOR;

/**
 * Display connector interface (up).
 * Pair with PDMIDISPLAYPORT.
 */
typedef struct PDMIDISPLAYCONNECTOR
{
    /**
     * Resize the display.
     * This is called when the resolution changes. This usually happens on
     * request from the guest os, but may also happen as the result of a reset.
     * If the callback returns VINF_VGA_RESIZE_IN_PROGRESS, the caller (VGA device)
     * must not access the connector and return.
     *
     * @returns VINF_SUCCESS if the framebuffer resize was completed,
     *          VINF_VGA_RESIZE_IN_PROGRESS if resize takes time and not yet finished.
     * @param   pInterface          Pointer to this interface.
     * @param   cBits               Color depth (bits per pixel) of the new video mode.
     * @param   pvVRAM              Address of the guest VRAM.
     * @param   cbLine              Size in bytes of a single scan line.
     * @param   cx                  New display width.
     * @param   cy                  New display height.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnResize,(PPDMIDISPLAYCONNECTOR pInterface, uint32_t cBits, void *pvVRAM, uint32_t cbLine,
                                         uint32_t cx, uint32_t cy));

    /**
     * Update a rectangle of the display.
     * PDMIDISPLAYPORT::pfnUpdateDisplay is the caller.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   x                   The upper left corner x coordinate of the rectangle.
     * @param   y                   The upper left corner y coordinate of the rectangle.
     * @param   cx                  The width of the rectangle.
     * @param   cy                  The height of the rectangle.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnUpdateRect,(PPDMIDISPLAYCONNECTOR pInterface, uint32_t x, uint32_t y, uint32_t cx, uint32_t cy));

    /**
     * Refresh the display.
     *
     * The interval between these calls is set by
     * PDMIDISPLAYPORT::pfnSetRefreshRate(). The driver should call
     * PDMIDISPLAYPORT::pfnUpdateDisplay() if it wishes to refresh the
     * display. PDMIDISPLAYPORT::pfnUpdateDisplay calls pfnUpdateRect with
     * the changed rectangles.
     *
     * @param   pInterface          Pointer to this interface.
     * @thread  The emulation thread or timer queue thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnRefresh,(PPDMIDISPLAYCONNECTOR pInterface));

    /**
     * Reset the display.
     *
     * Notification message when the graphics card has been reset.
     *
     * @param   pInterface          Pointer to this interface.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnReset,(PPDMIDISPLAYCONNECTOR pInterface));

    /**
     * LFB video mode enter/exit.
     *
     * Notification message when LinearFrameBuffer video mode is enabled/disabled.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   fEnabled            false - LFB mode was disabled,
     *                              true -  an LFB mode was disabled
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnLFBModeChange,(PPDMIDISPLAYCONNECTOR pInterface, bool fEnabled));

    /**
     * Process the guest graphics adapter information.
     *
     * Direct notification from guest to the display connector.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   pvVRAM              Address of the guest VRAM.
     * @param   u32VRAMSize         Size of the guest VRAM.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnProcessAdapterData,(PPDMIDISPLAYCONNECTOR pInterface, void *pvVRAM, uint32_t u32VRAMSize));

    /**
     * Process the guest display information.
     *
     * Direct notification from guest to the display connector.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   pvVRAM              Address of the guest VRAM.
     * @param   uScreenId           The index of the guest display to be processed.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnProcessDisplayData,(PPDMIDISPLAYCONNECTOR pInterface, void *pvVRAM, unsigned uScreenId));

    /**
     * The specified screen enters VBVA mode.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   uScreenId           The screen updates are for.
     * @param   pHostFlags          Undocumented!
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnVBVAEnable,(PPDMIDISPLAYCONNECTOR pInterface, unsigned uScreenId,
                                             struct VBVAHOSTFLAGS RT_UNTRUSTED_VOLATILE_GUEST *pHostFlags));

    /**
     * The specified screen leaves VBVA mode.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   uScreenId           The screen updates are for.
     * @thread  if render thread mode is on (fRenderThreadMode that was passed to pfnVBVAEnable is TRUE) - the render thread pfnVBVAEnable was called in,
     *          otherwise - the emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnVBVADisable,(PPDMIDISPLAYCONNECTOR pInterface, unsigned uScreenId));

    /**
     * A sequence of pfnVBVAUpdateProcess calls begins.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   uScreenId           The screen updates are for.
     * @thread  if render thread mode is on (fRenderThreadMode that was passed to pfnVBVAEnable is TRUE) - the render thread pfnVBVAEnable was called in,
     *          otherwise - the emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnVBVAUpdateBegin,(PPDMIDISPLAYCONNECTOR pInterface, unsigned uScreenId));

    /**
     * Process the guest VBVA command.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   uScreenId           The screen updates are for.
     * @param   pCmd                Video HW Acceleration Command to be processed.
     * @param   cbCmd               Undocumented!
     * @thread  if render thread mode is on (fRenderThreadMode that was passed to pfnVBVAEnable is TRUE) - the render thread pfnVBVAEnable was called in,
     *          otherwise - the emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnVBVAUpdateProcess,(PPDMIDISPLAYCONNECTOR pInterface, unsigned uScreenId,
                                                     struct VBVACMDHDR const RT_UNTRUSTED_VOLATILE_GUEST *pCmd, size_t cbCmd));

    /**
     * A sequence of pfnVBVAUpdateProcess calls ends.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   uScreenId           The screen updates are for.
     * @param   x                   The upper left corner x coordinate of the combined rectangle of all VBVA updates.
     * @param   y                   The upper left corner y coordinate of the rectangle.
     * @param   cx                  The width of the rectangle.
     * @param   cy                  The height of the rectangle.
     * @thread  if render thread mode is on (fRenderThreadMode that was passed to pfnVBVAEnable is TRUE) - the render thread pfnVBVAEnable was called in,
     *          otherwise - the emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnVBVAUpdateEnd,(PPDMIDISPLAYCONNECTOR pInterface, unsigned uScreenId, int32_t x, int32_t y,
                                                 uint32_t cx, uint32_t cy));

    /**
     * Resize the display.
     * This is called when the resolution changes. This usually happens on
     * request from the guest os, but may also happen as the result of a reset.
     * If the callback returns VINF_VGA_RESIZE_IN_PROGRESS, the caller (VGA device)
     * must not access the connector and return.
     *
     * @todo Merge with pfnResize.
     *
     * @returns VINF_SUCCESS if the framebuffer resize was completed,
     *          VINF_VGA_RESIZE_IN_PROGRESS if resize takes time and not yet finished.
     * @param   pInterface          Pointer to this interface.
     * @param   pView               The description of VRAM block for this screen.
     * @param   pScreen             The data of screen being resized.
     * @param   pvVRAM              Address of the guest VRAM.
     * @param   fResetInputMapping  Whether to reset the absolute pointing device to screen position co-ordinate
     *                              mapping.  Needed for real resizes, as the caller on the guest may not know how
     *                              to set the mapping.  Not wanted when we restore a saved state and are resetting
     *                              the mode.
     * @thread  if render thread mode is on (fRenderThreadMode that was passed to pfnVBVAEnable is TRUE) - the render thread pfnVBVAEnable was called in,
     *          otherwise - the emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnVBVAResize,(PPDMIDISPLAYCONNECTOR pInterface, PCVBVAINFOVIEW pView, PCVBVAINFOSCREEN pScreen,
                                             void *pvVRAM, bool fResetInputMapping));

    /**
     * Update the pointer shape.
     * This is called when the mouse pointer shape changes. The new shape
     * is passed as a caller allocated buffer that will be freed after returning
     *
     * @param   pInterface          Pointer to this interface.
     * @param   fVisible            Visibility indicator (if false, the other parameters are undefined).
     * @param   fAlpha              Flag whether alpha channel is being passed.
     * @param   xHot                Pointer hot spot x coordinate.
     * @param   yHot                Pointer hot spot y coordinate.
     * @param   cx                  Pointer width in pixels.
     * @param   cy                  Pointer height in pixels.
     * @param   pvShape             New shape buffer.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnVBVAMousePointerShape,(PPDMIDISPLAYCONNECTOR pInterface, bool fVisible, bool fAlpha,
                                                        uint32_t xHot, uint32_t yHot, uint32_t cx, uint32_t cy,
                                                        const void *pvShape));

    /**
     * The guest capabilities were updated.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   fCapabilities       The new capability flag state.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnVBVAGuestCapabilityUpdate,(PPDMIDISPLAYCONNECTOR pInterface, uint32_t fCapabilities));

    /** Read-only attributes.
     * For preformance reasons some readonly attributes are kept in the interface.
     * We trust the interface users to respect the readonlyness of these.
     * @{
     */
    /** Pointer to the display data buffer. */
    uint8_t        *pbData;
    /** Size of a scanline in the data buffer. */
    uint32_t        cbScanline;
    /** The color depth (in bits) the graphics card is supposed to provide. */
    uint32_t        cBits;
    /** The display width. */
    uint32_t        cx;
    /** The display height. */
    uint32_t        cy;
    /** @} */

    /**
     * The guest display input mapping rectangle was updated.
     *
     * @param   pInterface  Pointer to this interface.
     * @param   xOrigin     Upper left X co-ordinate relative to the first screen.
     * @param   yOrigin     Upper left Y co-ordinate relative to the first screen.
     * @param   cx          Rectangle width.
     * @param   cy          Rectangle height.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnVBVAInputMappingUpdate,(PPDMIDISPLAYCONNECTOR pInterface, int32_t xOrigin, int32_t yOrigin, uint32_t cx, uint32_t cy));

    /**
     * The guest is reporting the requested location of the host pointer.
     *
     * @param   pInterface  Pointer to this interface.
     * @param   fFlags      VBVA_CURSOR_*
     * @param   uScreenId   The screen to which X and Y are relative if VBVA_CURSOR_SCREEN_RELATIVE is set.
     * @param   x           Cursor X offset.
     * @param   y           Cursor Y offset.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnVBVAReportCursorPosition,(PPDMIDISPLAYCONNECTOR pInterface, uint32_t fFlags, uint32_t uScreen, uint32_t x, uint32_t y));

    /**
     * Process the graphics device HW Acceleration command.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   p3DNotify           Acceleration Command to be processed.
     * @thread  The graphics device thread: FIFO for the VMSVGA device.
     */
    DECLR3CALLBACKMEMBER(int, pfn3DNotifyProcess,(PPDMIDISPLAYCONNECTOR pInterface,
                                                  VBOX3DNOTIFY *p3DNotify));

    /**
     * The output target has been created.
     *
     * This callback is invoked only for output targets created with PDMIDISPLAYPORT::pfnCreateOutputTargetAsync.
     *
     * @param   pInterface            Pointer to this interface.
     * @param   uScreenId             The screen the target was created for.
     * @param   u64OutputTargetToken  Unique token previously used for creating a target.
     * @param   vrcCreated            VBox status code. If not VINF_SUCCESS then the target creation has failed.
     * @thread  The graphics device thread: FIFO for the VMSVGA device.
     */
    DECLR3CALLBACKMEMBER(void, pfnOnOutputTargetCreated, (PPDMIDISPLAYCONNECTOR pInterface,
                                                          uint32_t uScreenId,
                                                          uint64_t u64OutputTargetToken,
                                                          int vrcCreated));

    /**
     * Notifies that the output target is deprecated and will not be updated any longer.
     * The consumer is must release all references to the target.
     *
     * This can happen for example when the guest resizes its screen, in which case the target
     * has to be recreated.
     *
     * This is called by the graphics device if the reference counter of a target is greater than 0.
     * Due to asynchronous execution of the graphics thread relative to a consumer's thread,
     * the call may happen even if the consumer has just released all references to the token.
     * In this case the consumer must ignore the call.
     *
     * This callback, unlike PDMIDISPLAYCONNECTOR::pfnOnOutputTargetCreated, is invoked for output targets
     * created with both PDMIDISPLAYPORT::pfnCreateOutputTargetAsync and PDMIDISPLAYPORT::pfnCreateOutputTarget.
     *
     * @param   pInterface            Pointer to this interface.
     * @param   uScreenId             The screen the target was created for.
     * @param   u64OutputTargetToken  Unique token previously used for creating a target.
     * @thread  The graphics device thread: FIFO for the VMSVGA device.
     */
    DECLR3CALLBACKMEMBER(void, pfnOnOutputTargetRetired, (PPDMIDISPLAYCONNECTOR pInterface,
                                                          uint32_t uScreenId,
                                                          uint64_t u64OutputTargetToken));

} PDMIDISPLAYCONNECTOR;
/** PDMIDISPLAYCONNECTOR interface ID. */
#define PDMIDISPLAYCONNECTOR_IID                "0ca21275-42f9-4964-a73a-9973bf12c63d"


/** Pointer to a secret key interface. */
typedef struct PDMISECKEY *PPDMISECKEY;

/**
 * Secret key interface to retrieve secret keys.
 */
typedef struct PDMISECKEY
{
    /**
     * Retains a key identified by the ID. The caller will only hold a reference
     * to the key and must not modify the key buffer in any way.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to this interface.
     * @param   pszId           The alias/id for the key to retrieve.
     * @param   ppbKey          Where to store the pointer to the key buffer on success.
     * @param   pcbKey          Where to store the size of the key in bytes on success.
     */
    DECLR3CALLBACKMEMBER(int, pfnKeyRetain, (PPDMISECKEY pInterface, const char *pszId,
                                             const uint8_t **pbKey, size_t *pcbKey));

    /**
     * Releases one reference of the key identified by the given identifier.
     * The caller must not access the key buffer after calling this operation.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to this interface.
     * @param   pszId          The alias/id for the key to release.
     *
     * @note: It is advised to release the key whenever it is not used anymore so the entity
     *        storing the key can do anything to make retrieving the key from memory more
     *        difficult like scrambling the memory buffer for instance.
     */
    DECLR3CALLBACKMEMBER(int, pfnKeyRelease, (PPDMISECKEY pInterface, const char *pszId));

    /**
     * Retains a password identified by the ID. The caller will only hold a reference
     * to the password and must not modify the buffer in any way.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to this interface.
     * @param   pszId           The alias/id for the password to retrieve.
     * @param   ppszPassword    Where to store the pointer to the password on success.
     */
    DECLR3CALLBACKMEMBER(int, pfnPasswordRetain, (PPDMISECKEY pInterface, const char *pszId,
                                                  const char **ppszPassword));

    /**
     * Releases one reference of the password identified by the given identifier.
     * The caller must not access the password after calling this operation.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to this interface.
     * @param   pszId           The alias/id for the password to release.
     *
     * @note: It is advised to release the password whenever it is not used anymore so the entity
     *        storing the password can do anything to make retrieving the password from memory more
     *        difficult like scrambling the memory buffer for instance.
     */
    DECLR3CALLBACKMEMBER(int, pfnPasswordRelease, (PPDMISECKEY pInterface, const char *pszId));
} PDMISECKEY;
/** PDMISECKEY interface ID. */
#define PDMISECKEY_IID                           "3d698355-d995-453d-960f-31566a891df2"

/** Pointer to a secret key helper interface. */
typedef struct PDMISECKEYHLP *PPDMISECKEYHLP;

/**
 * Secret key helper interface for non critical functionality.
 */
typedef struct PDMISECKEYHLP
{
    /**
     * Notifies the interface provider that a key couldn't be retrieved from the key store.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to this interface.
     */
    DECLR3CALLBACKMEMBER(int, pfnKeyMissingNotify, (PPDMISECKEYHLP pInterface));

} PDMISECKEYHLP;
/** PDMISECKEY interface ID. */
#define PDMISECKEYHLP_IID                        "7be96168-4156-40ac-86d2-3073bf8b318e"


/** Pointer to a stream interface. */
typedef struct PDMISTREAM *PPDMISTREAM;
/**
 * Stream interface (up).
 * Makes up the foundation for PDMICHARCONNECTOR.  No pair interface.
 */
typedef struct PDMISTREAM
{
    /**
     * Polls for the specified events.
     *
     * @returns VBox status code.
     * @retval  VERR_INTERRUPTED if the poll was interrupted.
     * @retval  VERR_TIMEOUT     if the maximum waiting time was reached.
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   fEvts           The events to poll for, see RTPOLL_EVT_XXX.
     * @param   pfEvts          Where to return details about the events that occurred.
     * @param   cMillies        Number of milliseconds to wait.  Use
     *                          RT_INDEFINITE_WAIT to wait for ever.
     */
    DECLR3CALLBACKMEMBER(int, pfnPoll,(PPDMISTREAM pInterface, uint32_t fEvts, uint32_t *pfEvts, RTMSINTERVAL cMillies));

    /**
     * Interrupts the current poll call.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     */
    DECLR3CALLBACKMEMBER(int, pfnPollInterrupt,(PPDMISTREAM pInterface));

    /**
     * Read bits.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   pvBuf           Where to store the read bits.
     * @param   pcbRead         Number of bytes to read/bytes actually read.
     * @thread  Any thread.
     *
     * @note: This is non blocking, use the poll callback to block when there is nothing to read.
     */
    DECLR3CALLBACKMEMBER(int, pfnRead,(PPDMISTREAM pInterface, void *pvBuf, size_t *pcbRead));

    /**
     * Write bits.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   pvBuf           Where to store the write bits.
     * @param   pcbWrite        Number of bytes to write/bytes actually written.
     * @thread  Any thread.
     *
     * @note: This is non blocking, use the poll callback to block until there is room to write.
     */
    DECLR3CALLBACKMEMBER(int, pfnWrite,(PPDMISTREAM pInterface, const void *pvBuf, size_t *pcbWrite));
} PDMISTREAM;
/** PDMISTREAM interface ID. */
#define PDMISTREAM_IID                          "f9bd1ba6-c134-44cc-8259-febe14393952"


/** Mode of the parallel port */
typedef enum PDMPARALLELPORTMODE
{
    /** First invalid mode. */
    PDM_PARALLEL_PORT_MODE_INVALID = 0,
    /** SPP (Compatibility mode). */
    PDM_PARALLEL_PORT_MODE_SPP,
    /** EPP Data mode. */
    PDM_PARALLEL_PORT_MODE_EPP_DATA,
    /** EPP Address mode. */
    PDM_PARALLEL_PORT_MODE_EPP_ADDR,
    /** ECP mode (not implemented yet). */
    PDM_PARALLEL_PORT_MODE_ECP,
    /** 32bit hack. */
    PDM_PARALLEL_PORT_MODE_32BIT_HACK = 0x7fffffff
} PDMPARALLELPORTMODE;

/** Pointer to a host parallel port interface. */
typedef struct PDMIHOSTPARALLELPORT *PPDMIHOSTPARALLELPORT;
/**
 * Host parallel port interface (down).
 * Pair with PDMIHOSTPARALLELCONNECTOR.
 */
typedef struct PDMIHOSTPARALLELPORT
{
    /**
     * Notify device/driver that an interrupt has occurred.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @thread  Any thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnNotifyInterrupt,(PPDMIHOSTPARALLELPORT pInterface));
} PDMIHOSTPARALLELPORT;
/** PDMIHOSTPARALLELPORT interface ID. */
#define PDMIHOSTPARALLELPORT_IID                "f24b8668-e7f6-4eaa-a14c-4aa2a5f7048e"



/** Pointer to a Host Parallel connector interface. */
typedef struct PDMIHOSTPARALLELCONNECTOR *PPDMIHOSTPARALLELCONNECTOR;
/**
 * Host parallel connector interface (up).
 * Pair with PDMIHOSTPARALLELPORT.
 */
typedef struct PDMIHOSTPARALLELCONNECTOR
{
    /**
     * Write bits.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   pvBuf           Where to store the write bits.
     * @param   cbWrite         Number of bytes to write.
     * @param   enmMode         Mode to write the data.
     * @thread  Any thread.
     * @todo r=klaus cbWrite only defines buffer length, method needs a way top return actually written amount of data.
     */
    DECLR3CALLBACKMEMBER(int, pfnWrite,(PPDMIHOSTPARALLELCONNECTOR pInterface, const void *pvBuf,
                                        size_t cbWrite, PDMPARALLELPORTMODE enmMode));

    /**
     * Read bits.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   pvBuf           Where to store the read bits.
     * @param   cbRead          Number of bytes to read.
     * @param   enmMode         Mode to read the data.
     * @thread  Any thread.
     * @todo r=klaus cbRead only defines buffer length, method needs a way top return actually read amount of data.
     */
    DECLR3CALLBACKMEMBER(int, pfnRead,(PPDMIHOSTPARALLELCONNECTOR pInterface, void *pvBuf,
                                       size_t cbRead, PDMPARALLELPORTMODE enmMode));

    /**
     * Set data direction of the port (forward/reverse).
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   fForward        Flag whether to indicate whether the port is operated in forward or reverse mode.
     * @thread  Any thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnSetPortDirection,(PPDMIHOSTPARALLELCONNECTOR pInterface, bool fForward));

    /**
     * Write control register bits.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   fReg            The new control register value.
     * @thread  Any thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnWriteControl,(PPDMIHOSTPARALLELCONNECTOR pInterface, uint8_t fReg));

    /**
     * Read control register bits.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   pfReg           Where to store the control register bits.
     * @thread  Any thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnReadControl,(PPDMIHOSTPARALLELCONNECTOR pInterface, uint8_t *pfReg));

    /**
     * Read status register bits.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   pfReg           Where to store the status register bits.
     * @thread  Any thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnReadStatus,(PPDMIHOSTPARALLELCONNECTOR pInterface, uint8_t *pfReg));

} PDMIHOSTPARALLELCONNECTOR;
/** PDMIHOSTPARALLELCONNECTOR interface ID. */
#define PDMIHOSTPARALLELCONNECTOR_IID           "7c532602-7438-4fbc-9265-349d9f0415f9"


/** ACPI power source identifier */
typedef enum PDMACPIPOWERSOURCE
{
    PDM_ACPI_POWER_SOURCE_UNKNOWN  =   0,
    PDM_ACPI_POWER_SOURCE_OUTLET,
    PDM_ACPI_POWER_SOURCE_BATTERY
} PDMACPIPOWERSOURCE;
/** Pointer to ACPI battery state. */
typedef PDMACPIPOWERSOURCE *PPDMACPIPOWERSOURCE;

/** ACPI battey capacity */
typedef enum PDMACPIBATCAPACITY
{
    PDM_ACPI_BAT_CAPACITY_MIN      =   0,
    PDM_ACPI_BAT_CAPACITY_MAX      = 100,
    PDM_ACPI_BAT_CAPACITY_UNKNOWN  = 255
} PDMACPIBATCAPACITY;
/** Pointer to ACPI battery capacity. */
typedef PDMACPIBATCAPACITY *PPDMACPIBATCAPACITY;

/** ACPI battery state. See ACPI 3.0 spec '_BST (Battery Status)' */
typedef enum PDMACPIBATSTATE
{
    PDM_ACPI_BAT_STATE_CHARGED     = 0x00,
    PDM_ACPI_BAT_STATE_DISCHARGING = 0x01,
    PDM_ACPI_BAT_STATE_CHARGING    = 0x02,
    PDM_ACPI_BAT_STATE_CRITICAL    = 0x04
} PDMACPIBATSTATE;
/** Pointer to ACPI battery state. */
typedef PDMACPIBATSTATE *PPDMACPIBATSTATE;

/** Pointer to an ACPI port interface. */
typedef struct PDMIACPIPORT *PPDMIACPIPORT;
/**
 * ACPI port interface (down). Used by both the ACPI driver and (grumble) main.
 * Pair with PDMIACPICONNECTOR.
 */
typedef struct PDMIACPIPORT
{
    /**
     * Check if the guest entered the ACPI mode.
     *
     * @returns VBox status code
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   pfEntered       Is set to true if the guest entered the ACPI mode, false otherwise.
     */
    DECLR3CALLBACKMEMBER(int, pfnGetGuestEnteredACPIMode,(PPDMIACPIPORT pInterface, bool *pfEntered));

    /**
     * Check if the given CPU is still locked by the guest.
     *
     * @returns VBox status code
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   uCpu            The CPU to check for.
     * @param   pfLocked        Is set to true if the CPU is still locked by the guest, false otherwise.
     */
    DECLR3CALLBACKMEMBER(int, pfnGetCpuStatus,(PPDMIACPIPORT pInterface, unsigned uCpu, bool *pfLocked));

    /**
     * Send an ACPI monitor hot-plug event.
     *
     * @returns VBox status code
     * @param   pInterface      Pointer to the interface structure containing
     *                          the called function pointer.
     */
    DECLR3CALLBACKMEMBER(int, pfnMonitorHotPlugEvent,(PPDMIACPIPORT pInterface));

    /**
     * Send a battery status change event.
     *
     * @returns VBox status code
     * @param   pInterface      Pointer to the interface structure containing
     *                          the called function pointer.
     */
    DECLR3CALLBACKMEMBER(int, pfnBatteryStatusChangeEvent,(PPDMIACPIPORT pInterface));
} PDMIACPIPORT;
/** PDMIACPIPORT interface ID. */
#define PDMIACPIPORT_IID                        "974cb8fb-7fda-408c-f9b4-7ff4e3b2a699"


/** Pointer to an ACPI connector interface. */
typedef struct PDMIACPICONNECTOR *PPDMIACPICONNECTOR;
/**
 * ACPI connector interface (up).
 * Pair with PDMIACPIPORT.
 */
typedef struct PDMIACPICONNECTOR
{
    /**
     * Get the current power source of the host system.
     *
     * @returns VBox status code
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   penmPowerSource Pointer to the power source result variable.
     */
    DECLR3CALLBACKMEMBER(int, pfnQueryPowerSource,(PPDMIACPICONNECTOR, PPDMACPIPOWERSOURCE penmPowerSource));

    /**
     * Query the current battery status of the host system.
     *
     * @returns VBox status code?
     * @param   pInterface              Pointer to the interface structure containing the called function pointer.
     * @param   pfPresent               Is set to true if battery is present, false otherwise.
     * @param   penmRemainingCapacity   Pointer to the battery remaining capacity (0 - 100 or 255 for unknown).
     * @param   penmBatteryState        Pointer to the battery status.
     * @param   pu32PresentRate         Pointer to the present rate (0..1000 of the total capacity).
     */
    DECLR3CALLBACKMEMBER(int, pfnQueryBatteryStatus,(PPDMIACPICONNECTOR, bool *pfPresent, PPDMACPIBATCAPACITY penmRemainingCapacity,
                                                     PPDMACPIBATSTATE penmBatteryState, uint32_t *pu32PresentRate));
} PDMIACPICONNECTOR;
/** PDMIACPICONNECTOR interface ID. */
#define PDMIACPICONNECTOR_IID                   "5f14bf8d-1edf-4e3a-a1e1-cca9fd08e359"


/** Pointer to an event button port interface. */
typedef struct PDMIEVENTBUTTONPORT *PPDMIEVENTBUTTONPORT;
/**
 * Event button port interface (down). Used by both the ACPI/GPIO driver and (grumble) main.
 * Pair with PDMIEVENTBUTTONCONNECTOR.
 */
typedef struct PDMIEVENTBUTTONPORT
{
    /**
     * Check if the guest is able to handle button events.
     *
     * @returns VBox status code
     * @param   pInterface                      Pointer to the interface structure containing the called function pointer.
     * @param   pfCanHandleButtonEvents         Is set to true if the guest is able to handle button events, false otherwise.
     */
    DECLR3CALLBACKMEMBER(int, pfnQueryGuestCanHandleButtonEvents,(PPDMIEVENTBUTTONPORT pInterface, bool *pfCanHandleButtonEvents));

    /**
     * Send an power off button event.
     *
     * @returns VBox status code
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     */
    DECLR3CALLBACKMEMBER(int, pfnPowerButtonPress,(PPDMIEVENTBUTTONPORT pInterface));

    /**
     * Send an sleep button event.
     *
     * @returns VBox status code
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     */
    DECLR3CALLBACKMEMBER(int, pfnSleepButtonPress,(PPDMIEVENTBUTTONPORT pInterface));

    /**
     * Check if the last power button event was handled by the guest.
     *
     * @returns VBox status code
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   pfHandled       Is set to true if the last power button event was handled, false otherwise.
     */
    DECLR3CALLBACKMEMBER(int, pfnQueryPowerButtonHandled,(PPDMIEVENTBUTTONPORT pInterface, bool *pfHandled));

} PDMIEVENTBUTTONPORT;
/** PDMIEVENTBUTTONPORT interface ID. */
#define PDMIEVENTBUTTONPORT_IID                 "7aa5ada2-35c7-45be-a757-0398427af7aa"


/** Pointer to an event button connector interface. */
typedef struct PDMIEVENTBUTTONCONNECTOR *PPDMIEVENTBUTTONCONNECTOR;
/**
 * Event button connector interface (up).
 * Pair with PDMIEVENTBUTTONPORT.
 */
typedef struct PDMIEVENTBUTTONCONNECTOR
{
    /** Currently empty, so just a dummy value. */
    uint32_t                uDummy;

} PDMIEVENTBUTTONCONNECTOR;
/** PDMIACPICONNECTOR interface ID. */
#define PDMIEVENTBUTTONCONNECTOR_IID                   "46774d25-a55e-4e63-b70b-8946bcedd4a7"


struct VMMDevDisplayDef;

/** Pointer to a VMMDevice port interface. */
typedef struct PDMIVMMDEVPORT *PPDMIVMMDEVPORT;
/**
 * VMMDevice port interface (down).
 * Pair with PDMIVMMDEVCONNECTOR.
 */
typedef struct PDMIVMMDEVPORT
{
    /**
     * Return the current absolute mouse position in pixels
     *
     * @returns VBox status code
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   pxAbs           Pointer of result value, can be NULL
     * @param   pyAbs           Pointer of result value, can be NULL
     */
    DECLR3CALLBACKMEMBER(int, pfnQueryAbsoluteMouse,(PPDMIVMMDEVPORT pInterface, int32_t *pxAbs, int32_t *pyAbs));

    /**
     * Set the new absolute mouse position in pixels
     *
     * @returns VBox status code
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   xAbs            New absolute X position
     * @param   yAbs            New absolute Y position
     * @param   dz              New mouse wheel vertical movement offset
     * @param   dw              New mouse wheel horizontal movement offset
     * @param   fButtons        New buttons state
     */
    DECLR3CALLBACKMEMBER(int, pfnSetAbsoluteMouse,(PPDMIVMMDEVPORT pInterface, int32_t xAbs, int32_t yAbs,
                                                   int32_t dz, int32_t dw, uint32_t fButtons));

    /**
     * Return the current mouse capability flags
     *
     * @returns VBox status code
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   pfCapabilities  Pointer of result value
     */
    DECLR3CALLBACKMEMBER(int, pfnQueryMouseCapabilities,(PPDMIVMMDEVPORT pInterface, uint32_t *pfCapabilities));

    /**
     * Set the current mouse capability flag (host side)
     *
     * @returns VBox status code
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   fCapsAdded      Mask of capabilities to add to the flag
     * @param   fCapsRemoved    Mask of capabilities to remove from the flag
     */
    DECLR3CALLBACKMEMBER(int, pfnUpdateMouseCapabilities,(PPDMIVMMDEVPORT pInterface, uint32_t fCapsAdded, uint32_t fCapsRemoved));

    /**
     * Issue a display resolution change request.
     *
     * Note that there can only one request in the queue and that in case the guest does
     * not process it, issuing another request will overwrite the previous.
     *
     * @returns VBox status code
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   cDisplays       Number of displays. Can be either 1 or the number of VM virtual monitors.
     * @param   paDisplays      Definitions of guest screens to be applied. See VMMDev.h
     * @param   fForce          Whether to deliver the request to the guest even if the guest has
     *                          the requested resolution already.
     * @param   fMayNotify      Whether to send a hotplug notification to the guest if appropriate.
     */
    DECLR3CALLBACKMEMBER(int, pfnRequestDisplayChange,(PPDMIVMMDEVPORT pInterface, uint32_t cDisplays,
                                                       struct VMMDevDisplayDef const *paDisplays, bool fForce, bool fMayNotify));

    /**
     * Pass credentials to guest.
     *
     * Note that there can only be one set of credentials and the guest may or may not
     * query them and may do whatever it wants with them.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   pszUsername     User name, may be empty (UTF-8).
     * @param   pszPassword     Password, may be empty (UTF-8).
     * @param   pszDomain       Domain name, may be empty (UTF-8).
     * @param   fFlags          VMMDEV_SETCREDENTIALS_*.
     */
    DECLR3CALLBACKMEMBER(int, pfnSetCredentials,(PPDMIVMMDEVPORT pInterface, const char *pszUsername,
                                                 const char *pszPassword, const char *pszDomain,
                                                 uint32_t fFlags));

    /**
     * Notify the driver about a VBVA status change.
     *
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   fEnabled        Current VBVA status.
     */
    DECLR3CALLBACKMEMBER(void, pfnVBVAChange, (PPDMIVMMDEVPORT pInterface, bool fEnabled));

    /**
     * Issue a seamless mode change request.
     *
     * Note that there can only one request in the queue and that in case the guest does
     * not process it, issuing another request will overwrite the previous.
     *
     * @returns VBox status code
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   fEnabled        Seamless mode enabled or not
     */
    DECLR3CALLBACKMEMBER(int, pfnRequestSeamlessChange,(PPDMIVMMDEVPORT pInterface, bool fEnabled));

    /**
     * Issue a memory balloon change request.
     *
     * Note that there can only one request in the queue and that in case the guest does
     * not process it, issuing another request will overwrite the previous.
     *
     * @returns VBox status code
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   cMbBalloon      Balloon size in megabytes
     */
    DECLR3CALLBACKMEMBER(int, pfnSetMemoryBalloon,(PPDMIVMMDEVPORT pInterface, uint32_t cMbBalloon));

    /**
     * Issue a statistcs interval change request.
     *
     * Note that there can only one request in the queue and that in case the guest does
     * not process it, issuing another request will overwrite the previous.
     *
     * @returns VBox status code
     * @param   pInterface          Pointer to the interface structure containing the called function pointer.
     * @param   cSecsStatInterval   Statistics query interval in seconds
     *                              (0=disable).
     */
    DECLR3CALLBACKMEMBER(int, pfnSetStatisticsInterval,(PPDMIVMMDEVPORT pInterface, uint32_t cSecsStatInterval));

    /**
     * Notify the guest about a VRDP status change.
     *
     * @returns VBox status code
     * @param   pInterface              Pointer to the interface structure containing the called function pointer.
     * @param   fVRDPEnabled            Current VRDP status.
     * @param   uVRDPExperienceLevel    Which visual effects to be disabled in
     *                                  the guest.
     */
    DECLR3CALLBACKMEMBER(int, pfnVRDPChange, (PPDMIVMMDEVPORT pInterface, bool fVRDPEnabled, uint32_t uVRDPExperienceLevel));

    /**
     * Notify the guest of CPU hot-unplug event.
     *
     * @returns VBox status code
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   idCpuCore       The core id of the CPU to remove.
     * @param   idCpuPackage    The package id of the CPU to remove.
     */
    DECLR3CALLBACKMEMBER(int, pfnCpuHotUnplug, (PPDMIVMMDEVPORT pInterface, uint32_t idCpuCore, uint32_t idCpuPackage));

    /**
     * Notify the guest of CPU hot-plug event.
     *
     * @returns VBox status code
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   idCpuCore       The core id of the CPU to add.
     * @param   idCpuPackage    The package id of the CPU to add.
     */
    DECLR3CALLBACKMEMBER(int, pfnCpuHotPlug, (PPDMIVMMDEVPORT pInterface, uint32_t idCpuCore, uint32_t idCpuPackage));

} PDMIVMMDEVPORT;
/** PDMIVMMDEVPORT interface ID. */
#define PDMIVMMDEVPORT_IID                      "9e004f1a-875d-11e9-a673-c77c30f53623"


/** Pointer to a HPET legacy notification interface. */
typedef struct PDMIHPETLEGACYNOTIFY *PPDMIHPETLEGACYNOTIFY;
/**
 * HPET legacy notification interface.
 */
typedef struct PDMIHPETLEGACYNOTIFY
{
    /**
     * Notify about change of HPET legacy mode.
     *
     * @param   pInterface      Pointer to the interface structure containing the
     *                          called function pointer.
     * @param   fActivated      If HPET legacy mode is activated (@c true) or
     *                          deactivated (@c false).
     */
    DECLR3CALLBACKMEMBER(void, pfnModeChanged,(PPDMIHPETLEGACYNOTIFY pInterface, bool fActivated));
} PDMIHPETLEGACYNOTIFY;
/** PDMIHPETLEGACYNOTIFY interface ID. */
#define PDMIHPETLEGACYNOTIFY_IID                "c9ada595-4b65-4311-8b21-b10498997774"


/** @name Flags for PDMIVMMDEVPORT::pfnSetCredentials.
 * @{ */
/** The guest should perform a logon with the credentials. */
#define VMMDEV_SETCREDENTIALS_GUESTLOGON                    RT_BIT(0)
/** The guest should prevent local logons. */
#define VMMDEV_SETCREDENTIALS_NOLOCALLOGON                  RT_BIT(1)
/** The guest should verify the credentials. */
#define VMMDEV_SETCREDENTIALS_JUDGE                         RT_BIT(15)
/** @} */

/** Forward declaration of the guest information structure. */
struct VBoxGuestInfo;
/** Forward declaration of the guest information-2 structure. */
struct VBoxGuestInfo2;
/** Forward declaration of the guest statistics structure */
struct VBoxGuestStatistics;
/** Forward declaration of the guest status structure */
struct VBoxGuestStatus;

/** Forward declaration of the video accelerator command memory. */
struct VBVAMEMORY;
/** Pointer to video accelerator command memory. */
typedef struct VBVAMEMORY *PVBVAMEMORY;

/** Pointer to a VMMDev connector interface. */
typedef struct PDMIVMMDEVCONNECTOR *PPDMIVMMDEVCONNECTOR;
/**
 * VMMDev connector interface (up).
 * Pair with PDMIVMMDEVPORT.
 */
typedef struct PDMIVMMDEVCONNECTOR
{
    /**
     * Update guest facility status.
     *
     * Called in response to VMMDevReq_ReportGuestStatus, reset or state restore.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   uFacility           The facility.
     * @param   uStatus             The status.
     * @param   fFlags              Flags assoicated with the update. Currently
     *                              reserved and should be ignored.
     * @param   pTimeSpecTS         Pointer to the timestamp of this report.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnUpdateGuestStatus,(PPDMIVMMDEVCONNECTOR pInterface, uint32_t uFacility, uint16_t uStatus,
                                                     uint32_t fFlags, PCRTTIMESPEC pTimeSpecTS));

    /**
     * Updates a guest user state.
     *
     * Called in response to VMMDevReq_ReportGuestUserState.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   pszUser             Guest user name to update status for.
     * @param   pszDomain           Domain the guest user is bound to. Optional.
     * @param   uState              New guest user state to notify host about.
     * @param   pabDetails          Pointer to optional state data.
     * @param   cbDetails           Size (in bytes) of optional state data.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnUpdateGuestUserState,(PPDMIVMMDEVCONNECTOR pInterface, const char *pszUser,
                                                        const char *pszDomain, uint32_t uState,
                                                        const uint8_t *pabDetails, uint32_t cbDetails));

    /**
     * Reports the guest API and OS version.
     * Called whenever the Additions issue a guest info report request.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   pGuestInfo          Pointer to guest information structure
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnUpdateGuestInfo,(PPDMIVMMDEVCONNECTOR pInterface, const struct VBoxGuestInfo *pGuestInfo));

    /**
     * Reports the detailed Guest Additions version.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   uFullVersion        The guest additions version as a full version.
     *                              Use VBOX_FULL_VERSION_GET_MAJOR,
     *                              VBOX_FULL_VERSION_GET_MINOR and
     *                              VBOX_FULL_VERSION_GET_BUILD to access it.
     *                              (This will not be zero, so turn down the
     *                              paranoia level a notch.)
     * @param   pszName             Pointer to the sanitized version name.  This can
     *                              be empty, but will not be NULL.  If not empty,
     *                              it will contain a build type tag and/or a
     *                              publisher tag.  If both, then they are separated
     *                              by an underscore (VBOX_VERSION_STRING fashion).
     * @param   uRevision           The SVN revision.  Can be 0.
     * @param   fFeatures           Feature mask, currently none are defined.
     *
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnUpdateGuestInfo2,(PPDMIVMMDEVCONNECTOR pInterface, uint32_t uFullVersion,
                                                    const char *pszName, uint32_t uRevision, uint32_t fFeatures));

    /**
     * Update the guest additions capabilities.
     * This is called when the guest additions capabilities change. The new capabilities
     * are given and the connector should update its internal state.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   newCapabilities     New capabilities.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnUpdateGuestCapabilities,(PPDMIVMMDEVCONNECTOR pInterface, uint32_t newCapabilities));

    /**
     * Update the mouse capabilities.
     * This is called when the mouse capabilities change. The new capabilities
     * are given and the connector should update its internal state.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   newCapabilities     New capabilities.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnUpdateMouseCapabilities,(PPDMIVMMDEVCONNECTOR pInterface, uint32_t newCapabilities));

    /**
     * Update the pointer shape.
     * This is called when the mouse pointer shape changes. The new shape
     * is passed as a caller allocated buffer that will be freed after returning
     *
     * @param   pInterface          Pointer to this interface.
     * @param   fVisible            Visibility indicator (if false, the other parameters are undefined).
     * @param   fAlpha              Flag whether alpha channel is being passed.
     * @param   xHot                Pointer hot spot x coordinate.
     * @param   yHot                Pointer hot spot y coordinate.
     * @param   x                   Pointer new x coordinate on screen.
     * @param   y                   Pointer new y coordinate on screen.
     * @param   cx                  Pointer width in pixels.
     * @param   cy                  Pointer height in pixels.
     * @param   cbScanline          Size of one scanline in bytes.
     * @param   pvShape             New shape buffer.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnUpdatePointerShape,(PPDMIVMMDEVCONNECTOR pInterface, bool fVisible, bool fAlpha,
                                                      uint32_t xHot, uint32_t yHot,
                                                      uint32_t cx, uint32_t cy,
                                                      void *pvShape));

    /**
     * Enable or disable video acceleration on behalf of guest.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   fEnable             Whether to enable acceleration.
     * @param   pVbvaMemory         Video accelerator memory.

     * @return  VBox rc. VINF_SUCCESS if VBVA was enabled.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnVideoAccelEnable,(PPDMIVMMDEVCONNECTOR pInterface, bool fEnable, PVBVAMEMORY pVbvaMemory));

    /**
     * Force video queue processing.
     *
     * @param   pInterface          Pointer to this interface.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnVideoAccelFlush,(PPDMIVMMDEVCONNECTOR pInterface));

    /**
     * Return whether the given video mode is supported/wanted by the host.
     *
     * @returns VBox status code
     * @param   pInterface      Pointer to this interface.
     * @param   display         The guest monitor, 0 for primary.
     * @param   cy              Video mode horizontal resolution in pixels.
     * @param   cx              Video mode vertical resolution in pixels.
     * @param   cBits           Video mode bits per pixel.
     * @param   pfSupported     Where to put the indicator for whether this mode is supported. (output)
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnVideoModeSupported,(PPDMIVMMDEVCONNECTOR pInterface, uint32_t display, uint32_t cx, uint32_t cy, uint32_t cBits, bool *pfSupported));

    /**
     * Queries by how many pixels the height should be reduced when calculating video modes
     *
     * @returns VBox status code
     * @param   pInterface          Pointer to this interface.
     * @param   pcyReduction        Pointer to the result value.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnGetHeightReduction,(PPDMIVMMDEVCONNECTOR pInterface, uint32_t *pcyReduction));

    /**
     * Informs about a credentials judgement result from the guest.
     *
     * @returns VBox status code
     * @param   pInterface          Pointer to this interface.
     * @param   fFlags              Judgement result flags.
     * @thread  The emulation thread.
     */
     DECLR3CALLBACKMEMBER(int, pfnSetCredentialsJudgementResult,(PPDMIVMMDEVCONNECTOR pInterface, uint32_t fFlags));

    /**
     * Set the visible region of the display
     *
     * @returns VBox status code.
     * @param   pInterface          Pointer to this interface.
     * @param   cRect               Number of rectangles in pRect
     * @param   pRect               Rectangle array
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnSetVisibleRegion,(PPDMIVMMDEVCONNECTOR pInterface, uint32_t cRect, PRTRECT pRect));

    /**
     * Update monitor positions (offsets).
     *
     * Passing monitor positions from the guest to host exclusively since vmwgfx
     * (linux driver) fails to do so thru the FIFO.
     *
     * @returns VBox status code.
     * @param   pInterface          Pointer to this interface.
     * @param   cPositions          Number of monitor positions
     * @param   paPositions         Positions array
     * @remarks Is allowed to be NULL.
     * @thread  The emulation thread.
     * @sa      PDMIDISPLAYPORT::pfnReportMonitorPositions
     */
    DECLR3CALLBACKMEMBER(int, pfnUpdateMonitorPositions,(PPDMIVMMDEVCONNECTOR pInterface,
                                                         uint32_t cPositions, PCRTPOINT paPositions));

    /**
     * Query the visible region of the display
     *
     * @returns VBox status code.
     * @param   pInterface          Pointer to this interface.
     * @param   pcRects             Where to return the number of rectangles in
     *                              paRects.
     * @param   paRects             Rectangle array (set to NULL to query the number
     *                              of rectangles)
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnQueryVisibleRegion,(PPDMIVMMDEVCONNECTOR pInterface, uint32_t *pcRects, PRTRECT paRects));

    /**
     * Request the statistics interval
     *
     * @returns VBox status code.
     * @param   pInterface          Pointer to this interface.
     * @param   pulInterval         Pointer to interval in seconds
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnQueryStatisticsInterval,(PPDMIVMMDEVCONNECTOR pInterface, uint32_t *pulInterval));

    /**
     * Report new guest statistics
     *
     * @returns VBox status code.
     * @param   pInterface          Pointer to this interface.
     * @param   pGuestStats         Guest statistics
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnReportStatistics,(PPDMIVMMDEVCONNECTOR pInterface, struct VBoxGuestStatistics *pGuestStats));

    /**
     * Query the current balloon size
     *
     * @returns VBox status code.
     * @param   pInterface          Pointer to this interface.
     * @param   pcbBalloon          Balloon size
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnQueryBalloonSize,(PPDMIVMMDEVCONNECTOR pInterface, uint32_t *pcbBalloon));

    /**
     * Query the current page fusion setting
     *
     * @returns VBox status code.
     * @param   pInterface          Pointer to this interface.
     * @param   pfPageFusionEnabled Pointer to boolean
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnIsPageFusionEnabled,(PPDMIVMMDEVCONNECTOR pInterface, bool *pfPageFusionEnabled));

    /**
     * Queries a capability of virtual graphics device.
     *
     * @returns VBox status code
     * @param   pInterface          Pointer to this interface.
     * @param   capIndex            Capability index.
     * @param   pCapValue           Pointer to the result value.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnGetHostGraphicsCapability,(PPDMIVMMDEVCONNECTOR pInterface, uint32_t capIndex, uint32_t *pCapValue));

} PDMIVMMDEVCONNECTOR;
/** PDMIVMMDEVCONNECTOR interface ID. */
#define PDMIVMMDEVCONNECTOR_IID                 "eedbe8d9-eeec-4f03-8da4-a83d24870c71"


/**
 * Generic status LED core.
 * Note that a unit doesn't have to support all the indicators.
 */
typedef union PDMLEDCORE
{
    /** 32-bit view. */
    uint32_t volatile u32;
    /** Bit view. */
    struct
    {
        /** Reading/Receiving indicator. */
        uint32_t    fReading : 1;
        /** Writing/Sending indicator. */
        uint32_t    fWriting : 1;
        /** Busy indicator. */
        uint32_t    fBusy : 1;
        /** Error indicator. */
        uint32_t    fError : 1;
    }           s;
} PDMLEDCORE;

/** LED bit masks for the u32 view.
 * @{ */
/** Reading/Receiving indicator. */
#define PDMLED_READING  RT_BIT(0)
/** Writing/Sending indicator. */
#define PDMLED_WRITING  RT_BIT(1)
/** Busy indicator. */
#define PDMLED_BUSY     RT_BIT(2)
/** Error indicator. */
#define PDMLED_ERROR    RT_BIT(3)
/** @} */


/**
 * Generic status LED.
 * Note that a unit doesn't have to support all the indicators.
 */
typedef struct PDMLED
{
    /** Just a magic for sanity checking. */
    uint32_t    u32Magic;
    uint32_t    u32Alignment;           /**< structure size alignment. */
    /** The actual LED status.
     * Only the device is allowed to change this. */
    PDMLEDCORE  Actual;
    /** The asserted LED status which is cleared by the reader.
     * The device will assert the bits but never clear them.
     * The driver clears them as it sees fit. */
    PDMLEDCORE  Asserted;
} PDMLED;

/** Pointer to an LED. */
typedef PDMLED *PPDMLED;
/** Pointer to a const LED. */
typedef const PDMLED *PCPDMLED;

/** Magic value for PDMLED::u32Magic. */
#define PDMLED_MAGIC    UINT32_C(0x11335577)

/** Pointer to an LED ports interface. */
typedef struct PDMILEDPORTS      *PPDMILEDPORTS;
/**
 * Interface for exporting LEDs (down).
 * Pair with PDMILEDCONNECTORS.
 */
typedef struct PDMILEDPORTS
{
    /**
     * Gets the pointer to the status LED of a unit.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   iLUN            The unit which status LED we desire.
     * @param   ppLed           Where to store the LED pointer.
     */
    DECLR3CALLBACKMEMBER(int, pfnQueryStatusLed,(PPDMILEDPORTS pInterface, unsigned iLUN, PPDMLED *ppLed));

} PDMILEDPORTS;
/** PDMILEDPORTS interface ID. */
#define PDMILEDPORTS_IID                        "435e0cec-8549-4ca0-8c0d-98e52f1dc038"


/** Pointer to an LED connectors interface. */
typedef struct PDMILEDCONNECTORS *PPDMILEDCONNECTORS;
/**
 * Interface for reading LEDs (up).
 * Pair with PDMILEDPORTS.
 */
typedef struct PDMILEDCONNECTORS
{
    /**
     * Notification about a unit which have been changed.
     *
     * The driver must discard any pointers to data owned by
     * the unit and requery it.
     *
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   iLUN            The unit number.
     */
    DECLR3CALLBACKMEMBER(void, pfnUnitChanged,(PPDMILEDCONNECTORS pInterface, unsigned iLUN));
} PDMILEDCONNECTORS;
/** PDMILEDCONNECTORS interface ID. */
#define PDMILEDCONNECTORS_IID                   "8ed63568-82a7-4193-b57b-db8085ac4495"


/** Pointer to a Media Notification interface. */
typedef struct PDMIMEDIANOTIFY  *PPDMIMEDIANOTIFY;
/**
 * Interface for exporting Medium eject information (up).  No interface pair.
 */
typedef struct PDMIMEDIANOTIFY
{
    /**
     * Signals that the medium was ejected.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   iLUN            The unit which had the medium ejected.
     */
    DECLR3CALLBACKMEMBER(int, pfnEjected,(PPDMIMEDIANOTIFY pInterface, unsigned iLUN));

} PDMIMEDIANOTIFY;
/** PDMIMEDIANOTIFY interface ID. */
#define PDMIMEDIANOTIFY_IID                     "fc22d53e-feb1-4a9c-b9fb-0a990a6ab288"


/** The special status unit number */
#define PDM_STATUS_LUN      999


#ifdef VBOX_WITH_HGCM

/** Abstract HGCM command structure. Used only to define a typed pointer. */
struct VBOXHGCMCMD;

/** Pointer to HGCM command structure. This pointer is unique and identifies
 *  the command being processed. The pointer is passed to HGCM connector methods,
 *  and must be passed back to HGCM port when command is completed.
 */
typedef struct VBOXHGCMCMD *PVBOXHGCMCMD;

/** Pointer to a HGCM port interface. */
typedef struct PDMIHGCMPORT *PPDMIHGCMPORT;
/**
 * Host-Guest communication manager port interface (down). Normally implemented
 * by VMMDev.
 * Pair with PDMIHGCMCONNECTOR.
 */
typedef struct PDMIHGCMPORT
{
    /**
     * Notify the guest on a command completion.
     *
     * @returns VINF_SUCCESS or VERR_CANCELLED if the guest canceled the call.
     * @param   pInterface          Pointer to this interface.
     * @param   rc                  The return code (VBox error code).
     * @param   pCmd                A pointer that identifies the completed command.
     */
    DECLR3CALLBACKMEMBER(int, pfnCompleted,(PPDMIHGCMPORT pInterface, int32_t rc, PVBOXHGCMCMD pCmd));

    /**
     * Checks if @a pCmd was restored & resubmitted from saved state.
     *
     * @returns true if restored, false if not.
     * @param   pInterface          Pointer to this interface.
     * @param   pCmd                The command we're checking on.
     */
    DECLR3CALLBACKMEMBER(bool, pfnIsCmdRestored,(PPDMIHGCMPORT pInterface, PVBOXHGCMCMD pCmd));

    /**
     * Checks if @a pCmd was cancelled.
     *
     * @returns true if cancelled, false if not.
     * @param   pInterface          Pointer to this interface.
     * @param   pCmd                The command we're checking on.
     */
    DECLR3CALLBACKMEMBER(bool, pfnIsCmdCancelled,(PPDMIHGCMPORT pInterface, PVBOXHGCMCMD pCmd));

    /**
     * Gets the VMMDevRequestHeader::fRequestor value for @a pCmd.
     *
     * @returns The fRequestor value, VMMDEV_REQUESTOR_LEGACY if guest does not
     *          support it, VMMDEV_REQUESTOR_LOWEST if invalid parameters.
     * @param   pInterface          Pointer to this interface.
     * @param   pCmd                The command we're in checking on.
     */
    DECLR3CALLBACKMEMBER(uint32_t, pfnGetRequestor,(PPDMIHGCMPORT pInterface, PVBOXHGCMCMD pCmd));

    /**
     * Gets the VMMDevState::idSession value.
     *
     * @returns VMMDevState::idSession.
     * @param   pInterface          Pointer to this interface.
     */
    DECLR3CALLBACKMEMBER(uint64_t, pfnGetVMMDevSessionId,(PPDMIHGCMPORT pInterface));

} PDMIHGCMPORT;
/** PDMIHGCMPORT interface ID. */
# define PDMIHGCMPORT_IID                       "28c0a201-68cd-4752-9404-bb42a0c09eb7"

/* forward decl to hgvmsvc.h. */
struct VBOXHGCMSVCPARM;
/** Pointer to a HGCM service location structure. */
typedef struct HGCMSERVICELOCATION *PHGCMSERVICELOCATION;
/** Pointer to a HGCM connector interface. */
typedef struct PDMIHGCMCONNECTOR *PPDMIHGCMCONNECTOR;
/**
 * The Host-Guest communication manager connector interface (up). Normally
 * implemented by Main::VMMDevInterface.
 * Pair with PDMIHGCMPORT.
 */
typedef struct PDMIHGCMCONNECTOR
{
    /**
     * Locate a service and inform it about a client connection.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   pCmd                A pointer that identifies the command.
     * @param   pServiceLocation    Pointer to the service location structure.
     * @param   pu32ClientID        Where to store the client id for the connection.
     * @return  VBox status code.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnConnect,(PPDMIHGCMCONNECTOR pInterface, PVBOXHGCMCMD pCmd, PHGCMSERVICELOCATION pServiceLocation, uint32_t *pu32ClientID));

    /**
     * Disconnect from service.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   pCmd                A pointer that identifies the command.
     * @param   u32ClientID         The client id returned by the pfnConnect call.
     * @return  VBox status code.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnDisconnect,(PPDMIHGCMCONNECTOR pInterface, PVBOXHGCMCMD pCmd, uint32_t u32ClientID));

    /**
     * Process a guest issued command.
     *
     * @param   pInterface  Pointer to this interface.
     * @param   pCmd        A pointer that identifies the command.
     * @param   u32ClientID The client id returned by the pfnConnect call.
     * @param   u32Function Function to be performed by the service.
     * @param   cParms      Number of parameters in the array pointed to by paParams.
     * @param   paParms     Pointer to an array of parameters.
     * @param   tsArrival   The STAM_GET_TS() value when the request arrived.
     * @return  VBox status code.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnCall,(PPDMIHGCMCONNECTOR pInterface, PVBOXHGCMCMD pCmd, uint32_t u32ClientID, uint32_t u32Function,
                                       uint32_t cParms, struct VBOXHGCMSVCPARM *paParms, uint64_t tsArrival));

    /**
     * Notification about the guest cancelling a pending request.
     * @param   pInterface  Pointer to this interface.
     * @param   pCmd        A pointer that identifies the command.
     * @param   idclient    The client id returned by the pfnConnect call.
     */
    DECLR3CALLBACKMEMBER(void, pfnCancelled,(PPDMIHGCMCONNECTOR pInterface, PVBOXHGCMCMD pCmd, uint32_t idClient));

} PDMIHGCMCONNECTOR;
/** PDMIHGCMCONNECTOR interface ID. */
# define PDMIHGCMCONNECTOR_IID                  "33cb5c91-6a4a-4ad9-3fec-d1f7d413c4a5"

#endif /* VBOX_WITH_HGCM */

/** Pointer to a PCI raw connector interface. */
typedef struct PDMIPCIRAWCONNECTOR *PPDMIPCIRAWCONNECTOR;
/**
 * PCI raw connector interface (up).
 */
typedef struct PDMIPCIRAWCONNECTOR
{

    /**
     *
     */
    DECLR3CALLBACKMEMBER(int, pfnDeviceConstructComplete, (PPDMIPCIRAWCONNECTOR pInterface, const char *pcszName,
                                                           uint32_t uHostPciAddress, uint32_t uGuestPciAddress,
                                                           int vrc));

} PDMIPCIRAWCONNECTOR;
/** PDMIPCIRAWCONNECTOR interface ID. */
#define PDMIPCIRAWCONNECTOR_IID                 "14aa9c6c-8869-4782-9dfc-910071a6aebf"


/** Pointer to a VFS connector interface. */
typedef struct PDMIVFSCONNECTOR *PPDMIVFSCONNECTOR;
/**
 * VFS connector interface (up).
 */
typedef struct PDMIVFSCONNECTOR
{
    /**
     * Queries the size of the given path.
     *
     * @returns VBox status code.
     * @retval  VERR_NOT_FOUND if the path is not available.
     * @param   pInterface          Pointer to this interface.
     * @param   pszNamespace        The namespace for the path (usually driver/device name) or NULL for default namespace.
     * @param   pszPath             The path to query the size for.
     * @param   pcb                 Where to store the size of the path in bytes on success.
     */
    DECLR3CALLBACKMEMBER(int, pfnQuerySize, (PPDMIVFSCONNECTOR pInterface, const char *pszNamespace, const char *pszPath,
                                             uint64_t *pcb));

    /**
     * Reads everything from the given path and stores the data into the supplied buffer.
     *
     * @returns VBox status code.
     * @retval  VERR_NOT_FOUND if the path is not available.
     * @retval  VERR_BUFFER_OVERFLOW if the supplied buffer is too small to read everything.
     * @retval  VINF_BUFFER_UNDERFLOW if the supplied buffer is too large.
     * @param   pInterface          Pointer to this interface.
     * @param   pszNamespace        The namespace for the path (usually driver/device name) or NULL for default namespace.
     * @param   pszPath             The path to read everything for.
     * @param   pvBuf               Where to store the data.
     * @param   cbRead              How much to read.
     */
    DECLR3CALLBACKMEMBER(int, pfnReadAll, (PPDMIVFSCONNECTOR pInterface, const char *pszNamespace, const char *pszPath,
                                           void *pvBuf, size_t cbRead));

    /**
     * Writes the supplied data to the given path, overwriting any previously existing data.
     *
     * @returns VBox status code.
     * @param   pInterface          Pointer to this interface.
     * @param   pszNamespace        The namespace for the path (usually driver/device name) or NULL for default namespace.
     * @param   pszPath             The path to write everything to.
     * @param   pvBuf               The data to store.
     * @param   cbWrite             How many bytes to write.
     */
    DECLR3CALLBACKMEMBER(int, pfnWriteAll, (PPDMIVFSCONNECTOR pInterface, const char *pszNamespace, const char *pszPath,
                                            const void *pvBuf, size_t cbWrite));

    /**
     * Deletes the given path.
     *
     * @returns VBox status code.
     * @retval  VERR_NOT_FOUND if the path is not available.
     * @param   pszNamespace        The namespace for the path (usually driver/device name) or NULL for default namespace.
     * @param   pszPath             The path to delete.
     */
    DECLR3CALLBACKMEMBER(int, pfnDelete, (PPDMIVFSCONNECTOR pInterface, const char *pszNamespace, const char *pszPath));

    /** @todo Add standard open/read/write/close callbacks when the need arises. */

} PDMIVFSCONNECTOR;
/** PDMIVFSCONNECTOR interface ID. */
#define PDMIVFSCONNECTOR_IID               "a1fc51e0-414a-4e78-8388-8053b9dc6521"


/** Pointer to an GPIO port interface. */
typedef struct PDMIGPIOPORT       *PPDMIGPIOPORT;
/**
 * Interface for GPIO ports (down).
 * Pair with PDMIGPIOCONNECTORS.
 */
typedef struct PDMIGPIOPORT
{

    /**
     * Changes the state of the indicated GPIO line to the given value.
     *
     * @returns VBox status code.
     * @param   pInterface          Pointer to this interface.
     * @param   idGpio              The GPIO line ID to change.
     * @param   fVal                The value to change the GPIO line to.
     */
    DECLR3CALLBACKMEMBER(int, pfnGpioLineChange, (PPDMIGPIOPORT pInterface, uint32_t idGpio, bool fVal));

    /**
     * Returns whether the given GPIO line is configured as an input.
     *
     * @returns true if the line is configured as an input, false if output.
     * @param   pInterface          Pointer to this interface.
     * @param   idGpio              The GPIO line ID to check.
     */
    DECLR3CALLBACKMEMBER(bool, pfnGpioLineIsInput, (PPDMIGPIOPORT pInterface, uint32_t idGpio));

} PDMIGPIOPORT;
/** PDMIGPIOPORT interface ID. */
#define PDMIGPIOPORT_IID                        "75e0017c-4cda-47a4-8160-f4cc436025c4"


/** Pointer to an GPIO connectors interface. */
typedef struct PDMIGPIOCONNECTOR *PPDMIGPIOCONNECTOR;
/**
 * GPIO connector interface (up).
 * Pair with PDMIGPIOPORT.
 */
typedef struct PDMIGPIOCONNECTOR
{
    uint32_t uDummy;
} PDMIGPIOCONNECTOR;
/** PDMIGPIOCONNECTOR interface ID. */
#define PDMIGPIOCONNECTOR_IID                   "504bff7e-489f-4829-8cc3-f9b080d39133"

/** @} */

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_vmm_pdmifs_h */
