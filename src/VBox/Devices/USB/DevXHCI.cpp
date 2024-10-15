/* $Id: DevXHCI.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevXHCI - eXtensible Host Controller Interface for USB.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

/** @page pg_dev_xhci   xHCI - eXtensible Host Controller Interface Emulation.
 *
 * This component implements an xHCI USB controller.
 *
 * The xHCI device is significantly different from the EHCI and OHCI
 * controllers in that it is not timer driven. A worker thread is responsible
 * for transferring data between xHCI and VUSB.
 *
 * Since there can be dozens or even hundreds of USB devices, and because USB
 * transfers must share the same bus, only one worker thread is created (per
 * host controller).
 *
 *
 * The xHCI operational model is heavily based around a producer/consumer
 * model utilizing rings -- Command, Event, and Transfer rings. The Event ring
 * is only written by the xHC and is read-only for the HCD (Host Controller
 * Driver). The Command/Transfer rings are only written by the HCD and are
 * read-only for the xHC.
 *
 * The rings contain TRBs (Transfer Request Blocks). The TRBs represent not
 * only data transfers but also commands and status information. Each type of
 * ring only produces/consumes specific TRB types.
 *
 * When processing a ring, the xHC simply keeps advancing an internal pointer.
 * For the Command/Transfer rings, the HCD uses Link TRBs to manage the ring
 * storage in a fairly arbitrary manner. Since the HCD cannot write to the
 * Event ring, the Event Ring Segment Table (ERST) is used to manage the ring
 * storage instead.
 *
 * The Cycle bit is used to manage the ring buffer full/empty condition. The
 * Producer and Consumer both have their own Cycle State (PCS/CCS). The Cycle
 * bit of each TRB determines who owns it. The consumer only processes TRBs
 * whose Cycle bit matches the CCS. HCD software typically toggles the Cycle
 * bit on each pass through the ring. The Link TRB can be used to toggle the
 * CCS accordingly.
 *
 * Multiple Transfer TRBs can be chained together (via the Chain bit) into a
 * single Transfer Descriptor (TD). This provides a convenient capability for
 * the HCD to turn a URB into a single TD regardless of how the URB is laid
 * out in physical memory. If a transfer encounters an error or is terminated
 * by a short packet, the entire TD (i.e. chain of TRBs) is retired.
 *
 * Note that the xHC detects and handles short packets on its own. Backends
 * are always asked not to consider a short packet to be an error condition.
 *
 * Command and Event TRBs cannot be chained, thus an ED (Event Descriptor)
 * or a Command Descriptor (CD) always consists of a single TRB.
 *
 * There is one Command ring per xHC, one Event ring per interrupter (one or
 * more), and a potentially very large number of Transfer rings. There is a
 * 1:1 mapping between Transfer Rings and USB pipes, hence each USB device
 * uses 1-31 Transfer rings (at least one for the default control endpoint,
 * up to 31 if all IN/OUT endpoints are used). USB 3.0 devices may also use
 * up to 64K streams per endpoint, each with its Transfer ring, massively
 * increasing the potential number of Transfer rings in use.
 *
 * When building a Transfer ring, it's possible to queue up a large number
 * of TDs and as soon as the oldest ones are retired, queue up new TDs. The
 * Transfer ring might thus never be empty.
 *
 * For tracking ring buffer position, the TRDP and TREP fields in an endpoint
 * context are used. The TRDP is the 'TR Dequeue Pointer', i.e. the position
 * of the next TRB to be completed. This field is visible by the HCD when the
 * endpoint isn't running. It reflects TRBs completely processed by the xHC
 * and hence no longer owned by the xHC.
 *
 * The TREP field is the 'TR Enqueue Pointer' and tracks the position of the
 * next TRB to start processing (submit). This is purely internal to the
 * xHC. The TREP can potentially get far ahead of the TRDP, but only in the
 * part of the ring owned by the xHC (i.e. with matching DCS bit).
 *
 * Unlike most other xHCI data structures, transfer TRBs may describe memory
 * buffers with no alignment restrictions (both starting position and size).
 * In addition, there is no relationship between TRB boundaries and USB
 * packet boundaries.
 *
 *
 * Typically an event would be generated via the IOC bit (Interrupt On
 * Completion) when the last TRB of a TD is completed. However, multiple IOC
 * bits may be set per TD. This may be required when a TD equal or larger
 * than 16MB is used, since transfer events utilize a 24-bit length field.
 *
 * There is also the option of using Transfer Event TRBs to report TRB
 * completion. Transfer Event TRBs may be freely intermixed with transfer
 * TRBs. Note that an event TRB will produce an event reporting the size of
 * data transferred since the last event TRB or since the beginning of a TD.
 * The xHC submits URBs such that they either comprise the entire TD or end
 * at a Transfer Event TRB, thus there is no need to track the EDTLA
 * separately.
 *
 * Transfer errors always generate events, irrespective of IOC settings. The
 * xHC has always the option to generate events at implementation-specific
 * points so that the HCD does not fall too far behind.
 *
 * Control transfers use special TDs. A Setup Stage TD consists of only a
 * single Setup Stage TRB (there's no Chain bit). The optional Data Stage
 * TD consists of a Data Stage TRB chained to zero or more Normal TRBs
 * and/or Event Data TRBs. The Status Stage TD then consists of a Status
 * Stage TRB optionally chained to an Event Data TRB. The HCD is responsible
 * for building the TDs correctly.
 *
 * For isochronous transfers, only the first TRB of a TD is actually an
 * isochronous TRB. If the TD is chained, it will contain Normal TRBs (and
 * possibly Event Data TRBs).
 *
 *
 * Isochronous transfers require multiple TDs/URBs to be in flight at a
 * time. This complicates dealing with non-data TRBs (such as link or event
 * data TRBs). These TRBs cannot be completed while a previous TRB is still
 * in flight. They are completed either: a) when submitting URBs and there
 * are no in-flight URBs, or b) just prior to completing an URB.
 *
 * This approach works because URBs must be completed strictly in-order. The
 * TRDP and TREP determine whether there are in-flight TRBs (TREP equals
 * TRDP if and only if there are no in-flight TRBs).
 *
 * When submitting TRBs and there is in-flight traffic, non-data TRBs must
 * be examined and skipped over. Link TRBs need to be taken into account.
 *
 * Unfortunately, certain HCDs (looking at you, Microsoft!) violate the xHCI
 * specification and make assumptions about how far ahead of the TRDP the
 * xHC can get. We have to artificially limit the number of in-flight TDs
 * for this reason.
 *
 * Non-isochronous TRBs do not require this treatment for correct function
 * but are likely to benefit performance-wise from the pipelining.
 *
 * With high-speed and faster transfers, there is an added complication for
 * endpoints with more than one transfer per frame, i.e. short intervals. At
 * least some host USB stacks require URBs to cover an entire frame, which
 * means we may have to glue together several TDs into a single URB.
 *
 *
 * A buggy or malicious guest can create a transfer or command ring that
 * loops in on itself (in the simplest case using a sequence of one or more
 * link TRBs where the last TRB points to the beginning of the sequence).
 * Such a loop would effectively hang the processing thread. Since we cannot
 * easily detect a generic loop, and because even non-looped TRB/command
 * rings might contain extremely large number of items, we limit the number
 * of entries that we are willing to process at once. If the limit is
 * crossed, the xHC reports a host controller error and shuts itself down
 * until it's reset.
 *
 * Note that for TRB lists, both URB submission and completion must protect
 * against loops because the lists in guest memory are not guaranteed to stay
 * unchanged between submitting and completing URBs.
 *
 * The event ring is not susceptible to loops because the xHC is the producer,
 * not consumer. The event ring can run out of space but that is not a fatal
 * problem.
 *
 *
 * The interrupt logic uses an internal IPE (Interrupt Pending Enable) bit
 * which controls whether the register-visible IP (Interrupt Pending) bit
 * can be set. The IPE bit is set when a non-blocking event (BEI bit clear)
 * is enqueued. The IPE bit is cleared when the event ring is initialized or
 * transitions to empty (i.e. ERDP == EREP). When IPE transtitions to set,
 * it will set IP unless the EHB (Event Handler Busy) bit is set or IMODC
 * (Interrupt Moderation Counter) is non-zero. When IMODC counts down to
 * zero, it sets the IP bit if IPE is set and EHB is not. Setting the IP bit
 * triggers interrupt delivery. Note that clearing the IPE bit does not
 * change the IP bit state.
 *
 * Interrupt delivery depends on whether MSI/MSI-X is in use or not. With MSI,
 * an interrupter's IP (Interrupt Pending) bit is cleared as soon as the MSI
 * message is written; with classic PCI interrupt delivery, the HCD must clear
 * the IP bit. However, the EHB (Event Handler Busy) bit is always set, which
 * causes further interrupts to be blocked on the interrupter until the HCD
 * processes pending events and clears the EHB bit.
 *
 * Note that clearing the EHB bit may immediately trigger an interrupt if
 * additional event TRBs were queued up while the HCD was processing previous
 * ones.
 *
 *
 * Each enabled USB device has a corresponding slot ID, a doorbell, as well as
 * a device context which can be accessed through the DCBAA (Device Context
 * Base Address Array). Valid slot IDs are in the 1-255 range; the first entry
 * (i.e. index 0) in the DCBAA may optionally point to the Scratchpad Buffer
 * Array, while doorbell 0 is associated with the Command Ring.
 *
 * While 255 valid slot IDs is an xHCI architectural limit, existing xHC
 * implementations usually set a considerably lower limit, such as 32. See
 * the XHCI_NDS constant.
 *
 * It would be tempting to use the DCBAA to determine which slots are free.
 * Unfortunately the xHC is not allowed to access DCBAA entries which map to
 * disabled slots (see section 6.1). A parallel aSlotState array is hence used
 * to internally track the slot state and find available slots. Once a slot
 * is enabled, the slot context entry in the DCBAA is used to track the
 * slot state.
 *
 *
 * Unlike OHCI/UHCI/EHCI, the xHC much more closely tracks USB device state.
 * HCDs are not allowed to issue SET_ADDRESS requests at all and must use
 * the Address Device xHCI command instead.
 *
 * HCDs can use SET_CONFIGURATION and SET_INTERFACE requests normally, but
 * must inform the xHC of the changes via Configure Endpoint and Evaluate
 * Context commands. Similarly there are Reset Endpoint and Stop Endpoint
 * commands to manage endpoint state.
 *
 * A corollary of the above is that unlike OHCI/UHCI/EHCI, with xHCI there
 * are very clear rules and a straightforward protocol for managing
 * ownership of structures in physical memory. During normal operation, the
 * xHC owns all device context memory and the HCD must explicitly ask the xHC
 * to relinquish the ownership.
 *
 * The xHCI architecture offers an interesting feature in that it reserves
 * opaque fields for xHCI use in certain data structures (slot and endpoint
 * contexts) and gives the xHC an option to request scratchpad buffers that
 * a HCD must provide. The xHC may use the opaque storage and/or scratchpad
 * buffers for saving internal state.
 *
 * For implementation reasons, the xHCI device creates two root hubs on the
 * VUSB level; one for USB2 devices (USB 1.x and 2.0), one for USB3. The
 * behavior of USB2 vs. USB3 ports is different, and a device can only be
 * attached to either one or the other hub. However, there is a single array
 * of ports to avoid overly complicating the code, given that port numbering
 * is linear and encompasses both USB2 and USB3 ports.
 *
 *
 * The default emulated device is an Intel 7-Series xHC aka Panther Point.
 * This was Intel's first xHC and is widely supported. It is also possible
 * to select an Intel 8-Series xHC aka Lynx Point; this is only useful for
 * debugging and requires the 'other' set of Windows 7 drivers.
 *
 * For Windows XP guest support, it is possible to emulate a Renesas
 * (formerly NEC) uPD720201 xHC. It would be possible to emulate the earlier
 * NEC chips but those a) only support xHCI 0.96, and b) their drivers
 * require a reboot during installation. Renesas' drivers also support
 * Windows Vista and 7.
 *
 *
 * NB: Endpoints are addressed differently in xHCI and USB. In USB,
 * endpoint addresses are 8-bit values with the low four bits identifying
 * the endpoint number and the high bit indicating the direction (0=OUT,
 * 1=IN); see e.g. 9.3.4 in USB 2.0 spec. In xHCI, endpoint addresses are
 * used as DCIs (Device Context Index) and for that reason, they're
 * compressed into 5 bits where the lowest bit(!) indicates direction (again
 * 1=IN) and bits 1-4 designate the endpoint number. Endpoint 0 is somewhat
 * special and uses DCI 1. See 4.8.1 in xHCI spec.
 *
 *
 * NB: A variable named iPort is a zero-based index into the port array.
 * On the other hand, a variable named uPort is a one-based port number!
 * The implementation (obviously) uses zero-based indexing, but USB ports
 * are numbered starting with 1. The same is true of xHCI slot numbering.
 * The macros IDX_TO_ID() and ID_TO_IDX(a) should be used to convert between
 * the two numbering conventions to make the intent clear.
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_XHCI
#include <VBox/pci.h>
#include <VBox/msi.h>
#include <VBox/vmm/pdm.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#ifdef IN_RING3
# include <iprt/uuid.h>
# include <iprt/critsect.h>
#endif
#include <VBox/vusb.h>
#ifdef VBOX_IN_EXTPACK_R3
# include <VBox/version.h>
#endif
#ifndef VBOX_IN_EXTPACK
# include "VBoxDD.h"
#endif


/*********************************************************************************************************************************
*   (Most of the) Defined Constants, Macros and Structures                                                                       *
*********************************************************************************************************************************/

/* Optional error injection support via DBGF. */
//#define XHCI_ERROR_INJECTION

/** The saved state version. */
#define XHCI_SAVED_STATE_VERSION        1


/** Convert a zero-based index to a 1-based ID. */
#define IDX_TO_ID(a)    (a + 1)
/** Convert a 1-based ID to a zero-based index. */
#define ID_TO_IDX(a)    (a - 1)

/** PCI device related constants. */
#define XHCI_PCI_MSI_CAP_OFS    0x80

/** Number of LUNs/root hubs. One each for USB2/USB3. */
#define XHCI_NUM_LUNS   2

/** @name The following two constants were determined experimentally.
 * They determine the maximum number of TDs allowed to be in flight.
 * NB: For isochronous TDs, the number *must* be limited because
 * Windows 8+ violates the xHCI specification and does not keep
 * the transfer rings consistent.
 * @{
 */
//#define XHCI_MAX_ISOC_IN_FLIGHT     3   /* Scarlett needs 3; was 12 */
#define XHCI_MAX_ISOC_IN_FLIGHT     12
#define XHCI_MAX_BULK_IN_FLIGHT     8
/** @} */

/** @name Implementation limit on the number of TRBs and commands
 * the xHC is willing to process at once. A larger number is taken
 * to indicate a broken or malicious guest, and causes a HC error.
 * @{
 */
#define XHCI_MAX_NUM_CMDS   128
#define XHCI_MAX_NUM_TRBS   1024
/** @} */

/** Implementation TD size limit. Prevents EDTLA wrap-around. */
#define XHCI_MAX_TD_SIZE            (16 * _1M - 1)

/** Special value to prevent further queuing. */
#define XHCI_NO_QUEUING_IN_FLIGHT   (XHCI_MAX_BULK_IN_FLIGHT * 2)

/* Structural Parameters #1 (HCSPARAMS1) values. */

/** Maximum allowed Number of Downstream Ports on the root hub. Careful
 *  when changing -- other structures may need adjusting!
 */
#define XHCI_NDP_MAX                32

/** Default number of USB 2.0 ports.
 *
 * @note AppleUSBXHCI does not handle more than 15 ports. At least OS X
 *       10.8.2 crashes if we report more than 15 ports! Hence the default
 *       is 8 USB2 + 6 USB3 ports for a total of 14 so that OS X is happy.
 */
#define XHCI_NDP_20_DEFAULT         8

/** Default number of USB 3.0 ports. */
#define XHCI_NDP_30_DEFAULT         6

/** Number of interrupters. */
#define XHCI_NINTR                  8

/** Mask for interrupter indexing. */
#define XHCI_INTR_MASK              (XHCI_NINTR - 1)

/* The following is only true if XHCI_NINTR is a (non-zero) power of two. */
AssertCompile((XHCI_NINTR & XHCI_INTR_MASK) == 0);

/** Number of Device Slots. Determines the number of doorbell
 * registers and device slots, among other things. */
#define XHCI_NDS                    32

/* Enforce xHCI architectural limits on HCSPARAMS1. */
AssertCompile(XHCI_NDP_MAX < 255 && XHCI_NINTR < 1024 && XHCI_NDS < 255);
AssertCompile(XHCI_NDP_20_DEFAULT + XHCI_NDP_30_DEFAULT <= XHCI_NDP_MAX);
AssertCompile(XHCI_NDP_MAX <= XHCI_NDS);

/* Structural Parameters #2 (HCSPARAMS2) values. */

/** Isochronous Scheduling Threshold. */
#define XHCI_IST                    (RT_BIT(3) | 1)   /* One frame. */

/** Max number of Event Ring Segment Table entries as a power of two. */
#define XHCI_ERSTMAX_LOG2           5
/** Max number of Event Ring Segment Table entries. */
#define XHCI_ERSTMAX                RT_BIT(XHCI_ERSTMAX_LOG2)

/* Enforce xHCI architectural limits on HCSPARAMS2. */
AssertCompile(XHCI_ERSTMAX_LOG2 < 16);


/** Size of the xHCI memory-mapped I/O region. */
#define XHCI_MMIO_SIZE              _64K

/** Size of the capability part of the MMIO region.  */
#define XHCI_CAPS_REG_SIZE          0x80

/** Offset of the port registers in operational register space. */
#define XHCI_PORT_REG_OFFSET        0x400

/** Offset of xHCI extended capabilities in MMIO region.  */
#define XHCI_XECP_OFFSET            0x1000

/** Offset of the run-time registers in MMIO region.  */
#define XHCI_RTREG_OFFSET           0x2000

/** Offset of the doorbell registers in MMIO region.  */
#define XHCI_DOORBELL_OFFSET        0x3000

/** Size of the extended capability area. */
#define XHCI_EXT_CAP_SIZE           1024

/* Make sure we can identify MMIO register accesses properly. */
AssertCompile(XHCI_DOORBELL_OFFSET > XHCI_RTREG_OFFSET);
AssertCompile(XHCI_XECP_OFFSET > XHCI_PORT_REG_OFFSET + XHCI_CAPS_REG_SIZE);
AssertCompile(XHCI_RTREG_OFFSET > XHCI_XECP_OFFSET + XHCI_EXT_CAP_SIZE);


/** Maximum size of a single extended capability. */
#define MAX_XCAP_SIZE           256

/** @name xHCI Extended capability types.
 * @{ */
#define XHCI_XCP_USB_LEGACY     1   /**< USB legacy support. */
#define XHCI_XCP_PROTOCOL       2   /**< Protocols supported by ports. */
#define XHCI_XCP_EXT_PM         3   /**< Extended power management (non-PCI). */
#define XHCI_XCP_IOVIRT         4   /**< Hardware xHCI virtualization support. */
#define XHCI_XCP_MSI            5   /**< Message interrupts (non-PCI). */
#define XHCI_XCP_LOCAL_MEM      6   /**< Local memory (for debug support). */
#define XHCI_XCP_USB_DEBUG      10  /**< USB debug capability. */
#define XHCI_XCP_EXT_MSI        17  /**< MSI-X (non-PCI). */
/** @} */


/* xHCI Register Bits. */


/** @name Capability Parameters (HCCPARAMS) bits
 * @{ */
#define XHCI_HCC_AC64           RT_BIT(0)   /**< RO */
#define XHCI_HCC_BNC            RT_BIT(1)   /**< RO */
#define XHCI_HCC_CSZ            RT_BIT(2)   /**< RO */
#define XHCI_HCC_PPC            RT_BIT(3)   /**< RO */
#define XHCI_HCC_PIND           RT_BIT(4)   /**< RO */
#define XHCI_HCC_LHRC           RT_BIT(5)   /**< RO */
#define XHCI_HCC_LTC            RT_BIT(6)   /**< RO */
#define XHCI_HCC_NSS            RT_BIT(7)   /**< RO */
#define XHCI_HCC_MAXPSA_MASK    (RT_BIT(12)|RT_BIT(13)|RT_BIT(14)| RT_BIT(15))  /**< RO */
#define XHCI_HCC_MAXPSA_SHIFT   12
#define XHCI_HCC_XECP_MASK      0xFFFF0000  /**< RO */
#define XHCI_HCC_XECP_SHIFT     16
/** @} */


/** @name Command Register (USBCMD) bits
 * @{ */
#define XHCI_CMD_RS             RT_BIT(0)   /**< RW - Run/Stop */
#define XHCI_CMD_HCRST          RT_BIT(1)   /**< RW - Host Controller Reset */
#define XHCI_CMD_INTE           RT_BIT(2)   /**< RW - Interrupter Enable */
#define XHCI_CMD_HSEE           RT_BIT(3)   /**< RW - Host System Error Enable */
#define XHCI_CMD_LCRST          RT_BIT(7)   /**< RW - Light HC Reset */
#define XHCI_CMD_CSS            RT_BIT(8)   /**< RW - Controller Save State */
#define XHCI_CMD_CRS            RT_BIT(9)   /**< RW - Controller Restore State */
#define XHCI_CMD_EWE            RT_BIT(10)  /**< RW - Enable Wrap Event */
#define XHCI_CMD_EU3S           RT_BIT(11)  /**< RW - Enable U3 MFINDEX Stop */

#define XHCI_CMD_MASK           (  XHCI_CMD_RS  | XHCI_CMD_HCRST | XHCI_CMD_INTE | XHCI_CMD_HSEE | XHCI_CMD_LCRST  \
                                 | XHCI_CMD_CSS | XHCI_CMD_CRS   | XHCI_CMD_EWE  | XHCI_CMD_EU3S)
/** @} */


/** @name Status Register (USBSTS) bits
 * @{ */
#define XHCI_STATUS_HCH         RT_BIT(0)   /**< RO   - HC Halted */
#define XHCI_STATUS_HSE         RT_BIT(2)   /**< RW1C - Host System Error */
#define XHCI_STATUS_EINT        RT_BIT(3)   /**< RW1C - Event Interrupt */
#define XHCI_STATUS_PCD         RT_BIT(4)   /**< RW1C - Port Change Detect */
#define XHCI_STATUS_SSS         RT_BIT(8)   /**< RO   - Save State Status */
#define XHCI_STATUS_RSS         RT_BIT(9)   /**< RO   - Resture State Status */
#define XHCI_STATUS_SRE         RT_BIT(10)  /**< RW1C - Save/Restore Error */
#define XHCI_STATUS_CNR         RT_BIT(11)  /**< RO   - Controller Not Ready */
#define XHCI_STATUS_HCE         RT_BIT(12)  /**< RO   - Host Controller Error */

#define XHCI_STATUS_WRMASK      (XHCI_STATUS_HSE | XHCI_STATUS_EINT | XHCI_STATUS_PCD | XHCI_STATUS_SRE)
/** @} */


/** @name Default xHCI speed definitions (7.2.2.1.1)
 * @{ */
#define XHCI_SPD_FULL           1
#define XHCI_SPD_LOW            2
#define XHCI_SPD_HIGH           3
#define XHCI_SPD_SUPER          4
/** @} */

/** @name Port Status and Control Register bits (PORTSCUSB2/PORTSCUSB3)
 * @{ */
#define XHCI_PORT_CCS           RT_BIT(0)   /**< ROS   - Current Connection Status */
#define XHCI_PORT_PED           RT_BIT(1)   /**< RW1S  - Port Enabled/Disabled */
#define XHCI_PORT_OCA           RT_BIT(3)   /**< RO    - Over-current Active */
#define XHCI_PORT_PR            RT_BIT(4)   /**< RW1S  - Port Reset */
#define XHCI_PORT_PLS_MASK      (RT_BIT(5) | RT_BIT(6) | RT_BIT(7) | RT_BIT(8))      /**< RWS */
#define XHCI_PORT_PLS_SHIFT     5
#define XHCI_PORT_PP            RT_BIT(9)   /**< RWS   - Port Power */
#define XHCI_PORT_SPD_MASK      (RT_BIT(10) | RT_BIT(11) | RT_BIT(12) | RT_BIT(13))  /**< ROS */
#define XHCI_PORT_SPD_SHIFT     10
#define XHCI_PORT_LWS           RT_BIT(16)  /**< RW    - Link State Write Strobe */
#define XHCI_PORT_CSC           RT_BIT(17)  /**< RW1CS - Connect Status Change */
#define XHCI_PORT_PEC           RT_BIT(18)  /**< RW1CS - Port Enabled/Disabled Change */
#define XHCI_PORT_WRC           RT_BIT(19)  /**< RW1CS - Warm Port Reset Change */
#define XHCI_PORT_OCC           RT_BIT(20)  /**< RW1CS - Over-current Change */
#define XHCI_PORT_PRC           RT_BIT(21)  /**< RW1CS - Port Reset Change */
#define XHCI_PORT_PLC           RT_BIT(22)  /**< RW1CS - Port Link State Change */
#define XHCI_PORT_CEC           RT_BIT(23)  /**< RW1CS - Port Config Error Change */
#define XHCI_PORT_CAS           RT_BIT(24)  /**< RO    - Cold Attach Status */
#define XHCI_PORT_WCE           RT_BIT(25)  /**< RWS   - Wake on Connect Enable */
#define XHCI_PORT_WDE           RT_BIT(26)  /**< RWS   - Wake on Disconnect Enable */
#define XHCI_PORT_WOE           RT_BIT(27)  /**< RWS   - Wake on Over-current Enable */
#define XHCI_PORT_DR            RT_BIT(30)  /**< RO    - Device (Not) Removable */
#define XHCI_PORT_WPR           RT_BIT(31)  /**< RW1S  - Warm Port Reset */

#define XHCI_PORT_RESERVED      (RT_BIT(2) | RT_BIT(14) | RT_BIT(15) | RT_BIT(28) | RT_BIT(29))

#define XHCI_PORT_WAKE_MASK     (XHCI_PORT_WCE|XHCI_PORT_WDE|XHCI_PORT_WOE)
#define XHCI_PORT_CHANGE_MASK   (XHCI_PORT_CSC|XHCI_PORT_PEC|XHCI_PORT_WRC|XHCI_PORT_OCC|XHCI_PORT_PRC|XHCI_PORT_PLC|XHCI_PORT_CEC)
#define XHCI_PORT_CTL_RW_MASK   (XHCI_PORT_PP|XHCI_PORT_LWS)
#define XHCI_PORT_CTL_W1_MASK   (XHCI_PORT_PED|XHCI_PORT_PR|XHCI_PORT_WPR)
#define XHCI_PORT_RO_MASK       (XHCI_PORT_CCS|XHCI_PORT_OCA|XHCI_PORT_SPD_MASK|XHCI_PORT_CAS|XHCI_PORT_DR)
/** @} */

/** @name Port Link State values
 * @{ */
#define XHCI_PLS_U0        0  /**< U0 State. */
#define XHCI_PLS_U1        1  /**< U1 State. */
#define XHCI_PLS_U2        2  /**< U2 State. */
#define XHCI_PLS_U3        3  /**< U3 State (Suspended). */
#define XHCI_PLS_DISABLED  4  /**< Disabled. */
#define XHCI_PLS_RXDETECT  5  /**< RxDetect. */
#define XHCI_PLS_INACTIVE  6  /**< Inactive. */
#define XHCI_PLS_POLLING   7  /**< Polling. */
#define XHCI_PLS_RECOVERY  8  /**< Recovery. */
#define XHCI_PLS_HOTRST    9  /**< Hot Reset. */
#define XHCI_PLS_CMPLMODE 10  /**< Compliance Mode. */
#define XHCI_PLS_TSTMODE  11  /**< Test Mode. */
/* Values 12-14 are reserved. */
#define XHCI_PLS_RESUME   15  /**< Resume. */
/** @} */


/** @name Command Ring Control Register (CRCR) bits
 * @{ */
#define XHCI_CRCR_RCS           RT_BIT(0)   /**< RW   - Ring Cycle State */
#define XHCI_CRCR_CS            RT_BIT(1)   /**< RW1S - Command Stop */
#define XHCI_CRCR_CA            RT_BIT(2)   /**< RW1S - Command Abort */
#define XHCI_CRCR_CRR           RT_BIT(3)   /**< RO   - Command Ring Running */

#define XHCI_CRCR_RD_MASK       UINT64_C(0xFFFFFFFFFFFFFFF8)    /* Mask off bits always read as zero. */
#define XHCI_CRCR_ADDR_MASK     UINT64_C(0xFFFFFFFFFFFFFFC0)
#define XHCI_CRCR_UPD_MASK      (XHCI_CRCR_ADDR_MASK | XHCI_CRCR_RCS)
/** @} */


/** @name Interrupter Management Register (IMAN) bits
 * @{ */
#define XHCI_IMAN_IP            RT_BIT(0)   /**< RW1C - Interrupt Pending */
#define XHCI_IMAN_IE            RT_BIT(1)   /**< RW   - Interrupt Enable */

#define XHCI_IMAN_VALID_MASK    (XHCI_IMAN_IP | XHCI_IMAN_IE)
/** @} */


/** @name Interrupter Moderation Register (IMOD) bits
 * @{ */
#define XHCI_IMOD_IMODC_MASK    0xFFFF0000  /**< RW */
#define XHCI_IMOD_IMODC_SHIFT   16
#define XHCI_IMOD_IMODI_MASK    0x0000FFFF  /**< RW */
/** @} */


/** @name Event Ring Segment Table Size Register (ERSTSZ) bits
 * @{ */
#define XHCI_ERSTSZ_MASK        0x0000FFFF  /**< RW */
/** @} */

/** @name Event Ring Segment Table Base Address Register (ERSTBA) bits
 * @{ */
#define XHCI_ERST_ADDR_MASK     UINT64_C(0xFFFFFFFFFFFFFFC0)
/** @} */

/** For reasons that are not obvious, NEC/Renesas xHCs only require 16-bit
 *  alignment for the ERST base. This is not in line with the xHCI spec
 *  (which requires 64-bit alignment) but is clearly documented by NEC.
 */
#define NEC_ERST_ADDR_MASK      UINT64_C(0xFFFFFFFFFFFFFFF0)

/** Firmware revision reported in NEC/Renesas mode. Value chosen based on
 *  OS X driver check (OS X supports these chips since they're commonly
 *  found in ExpressCards).
 */
#define NEC_FW_REV              0x3028

/** @name Event Ring Deqeue Pointer Register (ERDP) bits
 * @{ */
#define XHCI_ERDP_DESI_MASK     0x00000007  /**< RW   - Dequeue ERST Segment Index */
#define XHCI_ERDP_EHB           RT_BIT(3)   /**< RW1C - Event Handler Busy */
#define XHCI_ERDP_ADDR_MASK     UINT64_C(0xFFFFFFFFFFFFFFF0)    /**< RW - ERDP address mask */
/** @} */

/** @name Device Context Base Address Array (DCBAA) definitions
 * @{ */
#define XHCI_DCBAA_ADDR_MASK    UINT64_C(0xFFFFFFFFFFFFFFC0)    /**< Applies to DCBAAP and its entries. */
/** @} */

/** @name Doorbell Register bits
 * @{ */
#define XHCI_DB_TGT_MASK        0x000000FF  /**< DB Target mask. */
#define XHCI_DB_STRMID_SHIFT    16          /**< DB Stream ID shift. */
#define XHCI_DB_STRMID_MASK     0xFFFF0000  /**< DB Stream ID mask. */
/** @} */

/** Address mask for device/endpoint/input contexts. */
#define XHCI_CTX_ADDR_MASK      UINT64_C(0xFFFFFFFFFFFFFFF0)

/** @name TRB Completion Codes
 * @{ */
#define XHCI_TCC_INVALID        0   /**< CC field not updated. */
#define XHCI_TCC_SUCCESS        1   /**< Successful TRB completion. */
#define XHCI_TCC_DATA_BUF_ERR   2   /**< Overrun/underrun. */
#define XHCI_TCC_BABBLE         3   /**< Babble detected. */
#define XHCI_TCC_USB_XACT_ERR   4   /**< USB transaction error. */
#define XHCI_TCC_TRB_ERR        5   /**< TRB error detected. */
#define XHCI_TCC_STALL          6   /**< USB Stall detected. */
#define XHCI_TCC_RSRC_ERR       7   /**< Inadequate xHC resources. */
#define XHCI_TCC_BWIDTH_ERR     8   /**< Unable to allocate bandwidth. */
#define XHCI_TCC_NO_SLOTS       9   /**< MaxSlots (NDS) exceeded. */
#define XHCI_TCC_INV_STRM_TYP   10  /**< Invalid stream context type. */
#define XHCI_TCC_SLOT_NOT_ENB   11  /**< Slot not enabled. */
#define XHCI_TCC_EP_NOT_ENB     12  /**< Endpoint not enabled. */
#define XHCI_TCC_SHORT_PKT      13  /**< Short packet detected. */
#define XHCI_TCC_RING_UNDERRUN  14  /**< Transfer ring underrun. */
#define XHCI_TCC_RING_OVERRUN   15  /**< Transfer ring overrun. */
#define XHCI_TCC_VF_RING_FULL   16  /**< VF event ring full. */
#define XHCI_TCC_PARM_ERR       17  /**< Invalid context parameter. */
#define XHCI_TCC_BWIDTH_OVER    18  /**< Isoc bandwidth overrun. */
#define XHCI_TCC_CTX_STATE_ERR  19  /**< Transition from illegal context state. */
#define XHCI_TCC_NO_PING        20  /**< No ping response in time. */
#define XHCI_TCC_EVT_RING_FULL  21  /**< Event Ring full. */
#define XHCI_TCC_DEVICE_COMPAT  22  /**< Incompatible device detected. */
#define XHCI_TCC_MISS_SVC       23  /**< Missed isoc service. */
#define XHCI_TCC_CMDR_STOPPED   24  /**< Command ring stopped. */
#define XHCI_TCC_CMD_ABORTED    25  /**< Command aborted. */
#define XHCI_TCC_STOPPED        26  /**< Endpoint stopped. */
#define XHCI_TCC_STP_INV_LEN    27  /**< EP stopped, invalid transfer length. */
                            /*  28       Reserved. */
#define XHCI_TCC_MAX_EXIT_LAT   29  /**< Max exit latency too large. */
                            /*  30       Reserved. */
#define XHCI_TCC_ISOC_OVERRUN   31  /**< Isochronous buffer overrun. */
#define XHCI_TCC_EVT_LOST       32  /**< Event lost due to overrun. */
#define XHCI_TCC_ERR_OTHER      33  /**< Implementation specific error. */
#define XHCI_TCC_INV_STRM_ID    34  /**< Invalid stream ID. */
#define XHCI_TCC_SEC_BWIDTH_ERR 35  /**< Secondary bandwidth error. */
#define XHCI_TCC_SPLIT_ERR      36  /**< Split transaction error. */
/** @} */

#if defined(IN_RING3) && defined(LOG_ENABLED)
/** Human-readable completion code descriptions for debugging. */
static const char * const g_apszCmplCodes[] = {
    "CC field not updated", "Successful TRB completion", "Overrun/underrun", "Babble detected",                     /* 0-3 */
    "USB transaction error", "TRB error detected", "USB Stall detected", "Inadequate xHC resources",                /* 4-7 */
    "Unable to allocate bandwidth", "MaxSlots (NDS) exceeded", "Invalid stream context type", "Slot not enabled",   /* 8-11 */
    "Endpoint not enabled", "Short packet detected", "Transfer ring underrun", "Transfer ring overrun",             /* 12-15 */
    "VF event ring full", "Invalid context param", "Isoc bandwidth overrun", "Transition from illegal ctx state",   /* 16-19 */
    "No ping response in time", "Event Ring full", "Incompatible device detected", "Missed isoc service",           /* 20-23 */
    "Command ring stopped", "Command aborted", "Endpoint stopped", "EP stopped, invalid transfer length",           /* 24-27 */
    "Reserved", "Max exit latency too large", "Reserved", "Isochronous buffer overrun",                             /* 28-31 */
    "Event lost due to overrun", "Implementation specific error", "Invalid stream ID", "Secondary bandwidth error", /* 32-35 */
    "Split transaction error"                                                                                       /* 36 */
};
#endif


/* TRBs marked as 'TRB' are only valid in the transfer ring. TRBs marked
 * as 'Command' are only valid in the command ring. TRBs marked as 'Event'
 * are the only ones generated in the event ring. The Link TRB is valid
 * in both the transfer and command rings.
 */

/** @name TRB Types
 * @{ */
#define XHCI_TRB_INVALID        0   /**< Reserved/unused TRB type. */
#define XHCI_TRB_NORMAL         1   /**< Normal TRB. */
#define XHCI_TRB_SETUP_STG      2   /**< Setup Stage TRB. */
#define XHCI_TRB_DATA_STG       3   /**< Data Stage TRB. */
#define XHCI_TRB_STATUS_STG     4   /**< Status Stage TRB. */
#define XHCI_TRB_ISOCH          5   /**< Isochronous TRB. */
#define XHCI_TRB_LINK           6   /**< Link. */
#define XHCI_TRB_EVT_DATA       7   /**< Event Data TRB. */
#define XHCI_TRB_NOOP_XFER      8   /**< No-op transfer TRB. */
#define XHCI_TRB_ENB_SLOT       9   /**< Enable Slot Command. */
#define XHCI_TRB_DIS_SLOT       10  /**< Disable Slot Command. */
#define XHCI_TRB_ADDR_DEV       11  /**< Address Device Command. */
#define XHCI_TRB_CFG_EP         12  /**< Configure Endpoint Command. */
#define XHCI_TRB_EVAL_CTX       13  /**< Evaluate Context Command. */
#define XHCI_TRB_RESET_EP       14  /**< Reset Endpoint Command. */
#define XHCI_TRB_STOP_EP        15  /**< Stop Endpoint Command. */
#define XHCI_TRB_SET_DEQ_PTR    16  /**< Set TR Dequeue Pointer Command. */
#define XHCI_TRB_RESET_DEV      17  /**< Reset Device Command. */
#define XHCI_TRB_FORCE_EVT      18  /**< Force Event Command. */
#define XHCI_TRB_NEG_BWIDTH     19  /**< Negotiate Bandwidth Command. */
#define XHCI_TRB_SET_LTV        20  /**< Set Latency Tolerate Value Command. */
#define XHCI_TRB_GET_PORT_BW    21  /**< Get Port Bandwidth Command. */
#define XHCI_TRB_FORCE_HDR      22  /**< Force Header Command. */
#define XHCI_TRB_NOOP_CMD       23  /**< No-op Command. */
                            /*  24-31    Reserved. */
#define XHCI_TRB_XFER           32  /**< Transfer Event. */
#define XHCI_TRB_CMD_CMPL       33  /**< Command Completion Event. */
#define XHCI_TRB_PORT_SC        34  /**< Port Status Change Event. */
#define XHCI_TRB_BW_REQ         35  /**< Bandwidth Request Event. */
#define XHCI_TRB_DBELL          36  /**< Doorbell Event. */
#define XHCI_TRB_HC_EVT         37  /**< Host Controller Event. */
#define XHCI_TRB_DEV_NOTIFY     38  /**< Device Notification Event. */
#define XHCI_TRB_MFIDX_WRAP     39  /**< MFINDEX Wrap Event. */
                            /*  40-47    Reserved. */
#define NEC_TRB_CMD_CMPL        48  /**< Command Completion Event, NEC specific. */
#define NEC_TRB_GET_FW_VER      49  /**< Get Firmware Version Command, NEC specific. */
#define NEC_TRB_AUTHENTICATE    50  /**< Authenticate Command, NEC specific. */
/** @} */

#if defined(IN_RING3) && defined(LOG_ENABLED)
/** Human-readable TRB names for debugging. */
static const char * const g_apszTrbNames[] = {
    "Reserved/unused TRB!!", "Normal TRB", "Setup Stage TRB", "Data Stage TRB",         /* 0-3 */
    "Status Stage TRB", "Isochronous TRB", "Link", "Event Data TRB",                    /* 4-7 */
    "No-op transfer TRB", "Enable Slot", "Disable Slot", "Address Device",              /* 8-11 */
    "Configure Endpoint", "Evaluate Context", "Reset Endpoint", "Stop Endpoint",        /* 12-15 */
    "Set TR Dequeue Pointer", "Reset Device", "Force Event", "Negotiate Bandwidth",     /* 16-19 */
    "Set Latency Tolerate Value", "Get Port Bandwidth", "Force Header", "No-op",        /* 20-23 */
    "UNDEF", "UNDEF", "UNDEF", "UNDEF", "UNDEF", "UNDEF", "UNDEF", "UNDEF",             /* 24-31 */
    "Transfer", "Command Completion", "Port Status Change", "BW Request",               /* 32-35 */
    "Doorbell", "Host Controller", "Device Notification", "MFINDEX Wrap",               /* 36-39 */
    "UNDEF", "UNDEF", "UNDEF", "UNDEF", "UNDEF", "UNDEF", "UNDEF", "UNDEF",             /* 40-47 */
    "NEC FW Version Completion", "NEC Get FW Version", "NEC Authenticate"               /* 48-50 */
};
#endif

/** Generic TRB template. */
typedef struct sXHCI_TRB_G {
    uint32_t    resvd0;
    uint32_t    resvd1;
    uint32_t    resvd2  : 24;
    uint32_t    cc      :  8;   /**< Completion Code. */
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    resvd3  :  9;
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    resvd4  : 16;
} XHCI_TRB_G;
AssertCompile(sizeof(XHCI_TRB_G) == 0x10);

/** Generic transfer TRB template. */
typedef struct sXHCI_TRB_GX {
    uint32_t    resvd0;
    uint32_t    resvd1;
    uint32_t    xfr_len : 17;   /**< Transfer length. */
    uint32_t    resvd2  :  5;
    uint32_t    int_tgt : 10;   /**< Interrupter target. */
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    ent     :  1;   /**< Evaluate Next TRB. */
    uint32_t    isp     :  1;   /**< Interrupt on Short Packet. */
    uint32_t    ns      :  1;   /**< No Snoop. */
    uint32_t    ch      :  1;   /**< Chain bit. */
    uint32_t    ioc     :  1;   /**< Interrupt On Completion. */
    uint32_t    idt     :  1;   /**< Immediate Data. */
    uint32_t    resvd3  :  3;
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    resvd4  : 16;
} XHCI_TRB_GX;
AssertCompile(sizeof(XHCI_TRB_GX) == 0x10);


/* -= Transfer TRB types =- */


/** Normal Transfer TRB. */
typedef struct sXHCI_TRB_NORM {
    uint64_t    data_ptr;       /**< Pointer or data. */
    uint32_t    xfr_len : 17;   /**< Transfer length. */
    uint32_t    td_size :  5;   /**< Remaining packets. */
    uint32_t    int_tgt : 10;   /**< Interrupter target. */
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    ent     :  1;   /**< Evaluate Next TRB. */
    uint32_t    isp     :  1;   /**< Interrupt on Short Packet. */
    uint32_t    ns      :  1;   /**< No Snoop. */
    uint32_t    ch      :  1;   /**< Chain bit. */
    uint32_t    ioc     :  1;   /**< Interrupt On Completion. */
    uint32_t    idt     :  1;   /**< Immediate Data. */
    uint32_t    resvd0  :  2;
    uint32_t    bei     :  1;   /**< Block Event Interrupt. */
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    resvd1  : 16;
} XHCI_TRB_NORM;
AssertCompile(sizeof(XHCI_TRB_NORM) == 0x10);

/** Control Transfer - Setup Stage TRB. */
typedef struct sXHCI_TRB_CTSP {
    uint8_t     bmRequestType;  /**< See the USB spec. */
    uint8_t     bRequest;
    uint16_t    wValue;
    uint16_t    wIndex;
    uint16_t    wLength;
    uint32_t    xfr_len : 17;   /**< Transfer length (8). */
    uint32_t    resvd0  :  5;
    uint32_t    int_tgt : 10;   /**< Interrupter target. */
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    resvd1  :  4;
    uint32_t    ioc     :  1;   /**< Interrupt On Completion. */
    uint32_t    idt     :  1;   /**< Immediate Data. */
    uint32_t    resvd2  :  2;
    uint32_t    bei     :  1;   /**< Block Event Interrupt. */
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    trt     :  2;   /**< Transfer Type. */
    uint32_t    resvd3  : 14;
} XHCI_TRB_CTSP;
AssertCompile(sizeof(XHCI_TRB_CTSP) == 0x10);

/** Control Transfer - Data Stage TRB. */
typedef struct sXHCI_TRB_CTDT {
    uint64_t    data_ptr;       /**< Pointer or data. */
    uint32_t    xfr_len : 17;   /**< Transfer length. */
    uint32_t    td_size :  5;   /**< Remaining packets. */
    uint32_t    int_tgt : 10;   /**< Interrupter target. */
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    ent     :  1;   /**< Evaluate Next TRB. */
    uint32_t    isp     :  1;   /**< Interrupt on Short Packet. */
    uint32_t    ns      :  1;   /**< No Snoop. */
    uint32_t    ch      :  1;   /**< Chain bit. */
    uint32_t    ioc     :  1;   /**< Interrupt On Completion. */
    uint32_t    idt     :  1;   /**< Immediate Data. */
    uint32_t    resvd0  :  3;
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    dir     :  1;   /**< Direction (1=IN). */
    uint32_t    resvd1  : 15;
} XHCI_TRB_CTDT;
AssertCompile(sizeof(XHCI_TRB_CTDT) == 0x10);

/** Control Transfer - Status Stage TRB. */
typedef struct sXHCI_TRB_CTSS {
    uint64_t    resvd0;
    uint32_t    resvd1  : 22;
    uint32_t    int_tgt : 10;   /**< Interrupter target. */
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    ent     :  1;   /**< Evaluate Next TRB. */
    uint32_t    resvd2  :  2;
    uint32_t    ch      :  1;   /**< Chain bit. */
    uint32_t    ioc     :  1;   /**< Interrupt On Completion. */
    uint32_t    resvd3  :  4;
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    dir     :  1;   /**< Direction (1=IN). */
    uint32_t    resvd4  : 15;
} XHCI_TRB_CTSS;
AssertCompile(sizeof(XHCI_TRB_CTSS) == 0x10);

/** Isochronous Transfer TRB. */
typedef struct sXHCI_TRB_ISOC {
    uint64_t    data_ptr;       /**< Pointer or data. */
    uint32_t    xfr_len : 17;   /**< Transfer length. */
    uint32_t    td_size :  5;   /**< Remaining packets. */
    uint32_t    int_tgt : 10;   /**< Interrupter target. */
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    ent     :  1;   /**< Evaluate Next TRB. */
    uint32_t    isp     :  1;   /**< Interrupt on Short Packet. */
    uint32_t    ns      :  1;   /**< No Snoop. */
    uint32_t    ch      :  1;   /**< Chain bit. */
    uint32_t    ioc     :  1;   /**< Interrupt On Completion. */
    uint32_t    idt     :  1;   /**< Immediate Data. */
    uint32_t    tbc     :  2;   /**< Transfer Burst Count. */
    uint32_t    bei     :  1;   /**< Block Event Interrupt. */
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    tlbpc   :  4;   /**< Transfer Last Burst Packet Count. */
    uint32_t    frm_id  : 11;   /**< Frame ID. */
    uint32_t    sia     :  1;   /**< Start Isoch ASAP. */
} XHCI_TRB_ISOC;
AssertCompile(sizeof(XHCI_TRB_ISOC) == 0x10);

/* Number of bits in the frame ID. */
#define XHCI_FRAME_ID_BITS  11

/** No Op Transfer TRB. */
typedef struct sXHCI_TRB_NOPT {
    uint64_t    resvd0;
    uint32_t    resvd1  : 22;
    uint32_t    int_tgt : 10;   /**< Interrupter target. */
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    ent     :  1;   /**< Evaluate Next TRB. */
    uint32_t    resvd2  :  2;
    uint32_t    ch      :  1;   /**< Chain bit. */
    uint32_t    ioc     :  1;   /**< Interrupt On Completion. */
    uint32_t    resvd3  :  4;
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    resvd4  : 16;
} XHCI_TRB_NOPT;
AssertCompile(sizeof(XHCI_TRB_NOPT) == 0x10);


/* -= Event TRB types =- */


/** Transfer Event TRB. */
typedef struct sXHCI_TRB_TE {
    uint64_t    trb_ptr;        /**< TRB pointer. */
    uint32_t    xfr_len : 24;   /**< Transfer length. */
    uint32_t    cc      :  8;   /**< Completion Code. */
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    resvd0  :  1;
    uint32_t    ed      :  1;   /**< Event Data flag. */
    uint32_t    resvd1  :  7;
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    ep_id   :  5;   /**< Endpoint ID. */
    uint32_t    resvd2  :  3;
    uint32_t    slot_id :  8;   /**< Slot ID. */
} XHCI_TRB_TE;
AssertCompile(sizeof(XHCI_TRB_TE) == 0x10);

/** Command Completion Event TRB. */
typedef struct sXHCI_TRB_CCE {
    uint64_t    trb_ptr;        /**< Command TRB pointer. */
    uint32_t    resvd0  : 24;
    uint32_t    cc      :  8;   /**< Completion Code. */
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    resvd1  :  9;
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    vf_id   :  8;   /**< Virtual Function ID. */
    uint32_t    slot_id :  8;   /**< Slot ID. */
} XHCI_TRB_CCE;
AssertCompile(sizeof(XHCI_TRB_CCE) == 0x10);

/** Port Staus Change Event TRB. */
typedef struct sXHCI_TRB_PSCE {
    uint32_t    resvd0  : 24;
    uint32_t    port_id :  8;   /**< Port ID. */
    uint32_t    resvd1;
    uint32_t    resvd2  : 24;
    uint32_t    cc      :  8;   /**< Completion Code. */
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    resvd3  :  9;
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    resvd4  : 16;
} XHCI_TRB_PSCE;
AssertCompile(sizeof(XHCI_TRB_PSCE) == 0x10);

/** Bandwidth Request Event TRB. */
typedef struct sXHCI_TRB_BRE {
    uint32_t    resvd0;
    uint32_t    resvd1;
    uint32_t    resvd2  : 24;
    uint32_t    cc      :  8;   /**< Completion Code. */
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    resvd3  :  9;
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    resvd4  :  8;
    uint32_t    slot_id :  8;   /**< Slot ID. */
} XHCI_TRB_BRE;
AssertCompile(sizeof(XHCI_TRB_BRE) == 0x10);

/** Doorbell Event TRB. */
typedef struct sXHCI_TRB_DBE {
    uint32_t    reason  :  5;   /**< DB Reason/target. */
    uint32_t    resvd0  : 27;
    uint32_t    resvd1;
    uint32_t    resvd2  : 24;
    uint32_t    cc      :  8;   /**< Completion Code. */
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    resvd3  :  9;
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    vf_id   :  8;   /**< Virtual Function ID. */
    uint32_t    slot_id :  8;   /**< Slot ID. */
} XHCI_TRB_DBE;
AssertCompile(sizeof(XHCI_TRB_DBE) == 0x10);

/** Host Controller Event TRB. */
typedef struct sXHCI_TRB_HCE {
    uint32_t    resvd0;
    uint32_t    resvd1;
    uint32_t    resvd2  : 24;
    uint32_t    cc      :  8;   /**< Completion Code. */
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    resvd3  :  9;
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    resvd4  : 16;
} XHCI_TRB_HCE;
AssertCompile(sizeof(XHCI_TRB_HCE) == 0x10);

/** Device Notification Event TRB. */
typedef struct sXHCI_TRB_DNE {
    uint32_t    resvd0  :  4;
    uint32_t    dn_type :  4;   /**< Device Notification Type. */
    uint32_t    dnd_lo  :  5;   /**< Device Notification Data Lo. */
    uint32_t    dnd_hi;         /**< Device Notification Data Hi. */
    uint32_t    resvd1  : 24;
    uint32_t    cc      :  8;   /**< Completion Code. */
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    resvd2  :  9;
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    resvd3  :  8;
    uint32_t    slot_id :  8;   /**< Slot ID. */
} XHCI_TRB_DNE;
AssertCompile(sizeof(XHCI_TRB_DNE) == 0x10);

/** MFINDEX Wrap Event TRB. */
typedef struct sXHCI_TRB_MWE {
    uint32_t    resvd0;
    uint32_t    resvd1;
    uint32_t    resvd2  : 24;
    uint32_t    cc      :  8;   /**< Completion Code. */
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    resvd3  :  9;
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    resvd4  : 16;
} XHCI_TRB_MWE;
AssertCompile(sizeof(XHCI_TRB_MWE) == 0x10);

/** NEC Specific Command Completion Event TRB. */
typedef struct sXHCI_TRB_NCE {
    uint64_t    trb_ptr;        /**< Command TRB pointer. */
    uint32_t    word1   : 16;   /**< First result word. */
    uint32_t    resvd0  :  8;
    uint32_t    cc      :  8;   /**< Completion Code. */
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    resvd1  :  9;
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    word2   : 16;   /**< Second result word. */
} XHCI_TRB_NCE;
AssertCompile(sizeof(XHCI_TRB_NCE) == 0x10);



/* -= Command TRB types =- */


/** No Op Command TRB. */
typedef struct sXHCI_TRB_NOPC {
    uint32_t    resvd0;
    uint32_t    resvd1;
    uint32_t    resvd2;
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    resvd3  :  9;
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    resvd4  : 16;
} XHCI_TRB_NOPC;
AssertCompile(sizeof(XHCI_TRB_NOPC) == 0x10);

/** Enable Slot Command TRB. */
typedef struct sXHCI_TRB_ESL {
    uint32_t    resvd0;
    uint32_t    resvd1;
    uint32_t    resvd2;
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    resvd3  :  9;
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    resvd4  : 16;
} XHCI_TRB_ESL;
AssertCompile(sizeof(XHCI_TRB_ESL) == 0x10);

/** Disable Slot Command TRB. */
typedef struct sXHCI_TRB_DSL {
    uint32_t    resvd0;
    uint32_t    resvd1;
    uint32_t    resvd2;
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    resvd3  :  9;
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    resvd4  :  8;
    uint32_t    slot_id :  8;   /**< Slot ID. */
} XHCI_TRB_DSL;
AssertCompile(sizeof(XHCI_TRB_DSL) == 0x10);

/** Address Device Command TRB. */
typedef struct sXHCI_TRB_ADR {
    uint64_t    ctx_ptr;        /**< Input Context pointer. */
    uint32_t    resvd0;
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    resvd1  :  8;
    uint32_t    bsr     :  1;   /**< Block Set Address Request. */
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    resvd2  :  8;
    uint32_t    slot_id :  8;   /**< Slot ID. */
} XHCI_TRB_ADR;
AssertCompile(sizeof(XHCI_TRB_ADR) == 0x10);

/** Configure Endpoint Command TRB. */
typedef struct sXHCI_TRB_CFG {
    uint64_t    ctx_ptr;        /**< Input Context pointer. */
    uint32_t    resvd0;
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    resvd1  :  8;
    uint32_t    dc      :  1;   /**< Deconfigure. */
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    resvd2  :  8;
    uint32_t    slot_id :  8;   /**< Slot ID. */
} XHCI_TRB_CFG;
AssertCompile(sizeof(XHCI_TRB_CFG) == 0x10);

/** Evaluate Context Command TRB. */
typedef struct sXHCI_TRB_EVC {
    uint64_t    ctx_ptr;        /**< Input Context pointer. */
    uint32_t    resvd0;
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    resvd1  :  9;
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    resvd2  :  8;
    uint32_t    slot_id :  8;   /**< Slot ID. */
} XHCI_TRB_EVC;
AssertCompile(sizeof(XHCI_TRB_EVC) == 0x10);

/** Reset Endpoint Command TRB. */
typedef struct sXHCI_TRB_RSE {
    uint32_t    resvd0;
    uint32_t    resvd1;
    uint32_t    resvd2;
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    resvd3  :  8;
    uint32_t    tsp     :  1;   /**< Transfer State Preserve. */
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    ep_id   :  5;   /**< Endpoint ID. */
    uint32_t    resvd4  :  3;
    uint32_t    slot_id :  8;   /**< Slot ID. */
} XHCI_TRB_RSE;
AssertCompile(sizeof(XHCI_TRB_RSE) == 0x10);

/** Stop Endpoint Command TRB. */
typedef struct sXHCI_TRB_STP {
    uint32_t    resvd0;
    uint32_t    resvd1;
    uint32_t    resvd2;
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    resvd3  :  9;
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    ep_id   :  5;   /**< Endpoint ID. */
    uint32_t    resvd4  :  2;
    uint32_t    sp      :  1;   /**< Suspend. */
    uint32_t    slot_id :  8;   /**< Slot ID. */
} XHCI_TRB_STP;
AssertCompile(sizeof(XHCI_TRB_STP) == 0x10);

/** Set TR Dequeue Pointer Command TRB. */
typedef struct sXHCI_TRB_STDP {
#if 0
    uint64_t    dcs     :  1;   /**< Dequeue Cycle State. */
    uint64_t    sct     :  3;   /**< Stream Context Type. */
    uint64_t    tr_dqp  : 60;   /**< New TR Dequeue Pointer (63:4). */
#else
    uint64_t    tr_dqp;
#endif
    uint16_t    resvd0;
    uint16_t    strm_id;        /**< Stream ID. */
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    resvd1  :  9;
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    ep_id   :  5;   /**< Endpoint ID. */
    uint32_t    resvd2  :  3;
    uint32_t    slot_id :  8;   /**< Slot ID. */
} XHCI_TRB_STDP;
AssertCompile(sizeof(XHCI_TRB_STDP) == 0x10);

/** Reset Device Command TRB. */
typedef struct sXHCI_TRB_RSD {
    uint32_t    resvd0;
    uint32_t    resvd1;
    uint32_t    resvd2;
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    resvd3  :  9;
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    resvd4  :  8;
    uint32_t    slot_id :  8;   /**< Slot ID. */
} XHCI_TRB_RSD;
AssertCompile(sizeof(XHCI_TRB_RSD) == 0x10);

/** Get Port Bandwidth Command TRB. */
typedef struct sXHCI_TRB_GPBW {
    uint64_t    pbctx_ptr;      /**< Port Bandwidth Context pointer. */
    uint32_t    resvd0;
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    resvd1  :  9;
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    spd     :  4;   /**< Dev Speed. */
    uint32_t    resvd2  :  4;
    uint32_t    slot_id :  8;   /**< Slot ID. */
} XHCI_TRB_GPBW;
AssertCompile(sizeof(XHCI_TRB_GPBW) == 0x10);

/** Force Header Command TRB. */
typedef struct sXHCI_TRB_FHD {
    uint32_t    pkt_typ :  5;   /**< Packet Type. */
    uint32_t    hdr_lo  : 27;   /**< Header Info Lo. */
    uint32_t    hdr_mid;        /**< Header Info Mid. */
    uint32_t    hdr_hi;         /**< Header Info Hi. */
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    resvd0  :  9;
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    resvd1  :  8;
    uint32_t    slot_id :  8;   /**< Slot ID. */
} XHCI_TRB_FHD;
AssertCompile(sizeof(XHCI_TRB_FHD) == 0x10);

/** NEC Specific Authenticate Command TRB. */
typedef struct sXHCI_TRB_NAC {
    uint64_t    cookie;         /**< Cookie to munge. */
    uint32_t    resvd0;
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    resvd1  :  9;
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    resvd2  :  8;
    uint32_t    slot_id :  8;   /**< Slot ID. */
} XHCI_TRB_NAC;
AssertCompile(sizeof(XHCI_TRB_NAC) == 0x10);


/* -= Other TRB types =- */


/** Link TRB. */
typedef struct sXHCI_TRB_LNK {
    uint64_t    rseg_ptr;       /**< Ring Segment Pointer. */
    uint32_t    resvd0  : 22;
    uint32_t    int_tgt : 10;   /**< Interrupter target. */
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    toggle  :  1;   /**< Toggle Cycle flag. */
    uint32_t    resvd1  :  2;
    uint32_t    chain   :  1;   /**< Chain flag. */
    uint32_t    ioc     :  1;   /**< Interrupt On Completion flag. */
    uint32_t    resvd2  :  4;
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    resvd3  : 16;
} XHCI_TRB_LNK;
AssertCompile(sizeof(XHCI_TRB_LNK) == 0x10);

/** Event Data TRB. */
typedef struct sXHCI_TRB_EVTD {
    uint64_t    evt_data;       /**< Event Data. */
    uint32_t    resvd0  : 22;
    uint32_t    int_tgt : 10;   /**< Interrupter target. */
    uint32_t    cycle   :  1;   /**< Cycle bit. */
    uint32_t    ent     :  1;   /**< Evaluate Next Target flag. */
    uint32_t    resvd1  :  2;
    uint32_t    chain   :  1;   /**< Chain flag. */
    uint32_t    ioc     :  1;   /**< Interrupt On Completion flag. */
    uint32_t    resvd2  :  3;
    uint32_t    bei     :  1;   /**< Block Event Interrupt flag. */
    uint32_t    type    :  6;   /**< TRB Type. */
    uint32_t    resvd3  : 16;
} XHCI_TRB_EVTD;
AssertCompile(sizeof(XHCI_TRB_EVTD) == 0x10);


/* -= Union TRB types for the three rings =- */


typedef union sXHCI_XFER_TRB {
    XHCI_TRB_NORM   norm;
    XHCI_TRB_CTSP   setup;
    XHCI_TRB_CTDT   data;
    XHCI_TRB_CTSS   status;
    XHCI_TRB_ISOC   isoc;
    XHCI_TRB_EVTD   evtd;
    XHCI_TRB_NOPT   nop;
    XHCI_TRB_LNK    link;
    XHCI_TRB_GX     gen;
} XHCI_XFER_TRB;
AssertCompile(sizeof(XHCI_XFER_TRB) == 0x10);

typedef union sXHCI_COMMAND_TRB {
    XHCI_TRB_ESL    esl;
    XHCI_TRB_DSL    dsl;
    XHCI_TRB_ADR    adr;
    XHCI_TRB_CFG    cfg;
    XHCI_TRB_EVC    evc;
    XHCI_TRB_RSE    rse;
    XHCI_TRB_STP    stp;
    XHCI_TRB_STDP   stdp;
    XHCI_TRB_RSD    rsd;
    XHCI_TRB_GPBW   gpbw;
    XHCI_TRB_FHD    fhd;
    XHCI_TRB_NAC    nac;
    XHCI_TRB_NOPC   nopc;
    XHCI_TRB_LNK    link;
    XHCI_TRB_G      gen;
} XHCI_COMMAND_TRB;
AssertCompile(sizeof(XHCI_COMMAND_TRB) == 0x10);

typedef union sXHCI_EVENT_TRB {
    XHCI_TRB_TE     te;
    XHCI_TRB_CCE    cce;
    XHCI_TRB_PSCE   psce;
    XHCI_TRB_BRE    bre;
    XHCI_TRB_DBE    dbe;
    XHCI_TRB_HCE    hce;
    XHCI_TRB_DNE    dne;
    XHCI_TRB_MWE    mwe;
    XHCI_TRB_NCE    nce;
    XHCI_TRB_G      gen;
} XHCI_EVENT_TRB;
AssertCompile(sizeof(XHCI_EVENT_TRB) == 0x10);



/* -=-=-= Contexts =-=-=- */

/** Slot Context. */
typedef struct sXHCI_SLOT_CTX {
    uint32_t    route_str   : 20;   /**< Route String. */
    uint32_t    speed       :  4;   /**< Device speed. */
    uint32_t    resvd0      :  1;
    uint32_t    mtt         :  1;   /**< Multi-TT flag. */
    uint32_t    hub         :  1;   /**< Hub flag. */
    uint32_t    ctx_ent     :  5;   /**< Context entries. */
    uint32_t    max_lat     : 16;   /**< Max exit latency in usec. */
    uint32_t    rh_port     :  8;   /**< Root hub port number (1-based). */
    uint32_t    n_ports     :  8;   /**< No. of ports for hubs. */
    uint32_t    tt_slot     :  8;   /**< TT hub slot ID. */
    uint32_t    tt_port     :  8;   /**< TT port number. */
    uint32_t    ttt         :  2;   /**< TT Think Time. */
    uint32_t    resvd1      :  4;
    uint32_t    intr_tgt    : 10;   /**< Interrupter Target. */
    uint32_t    dev_addr    :  8;   /**< Device Address. */
    uint32_t    resvd2      : 19;
    uint32_t    slot_state  :  5;   /**< Slot State. */
    uint32_t    opaque[4];          /**< For xHC (i.e. our own) use. */
} XHCI_SLOT_CTX;
AssertCompile(sizeof(XHCI_SLOT_CTX) == 0x20);

/** @name Slot Context states
 * @{ */
#define XHCI_SLTST_ENDIS        0   /**< Enabled/Disabled. */
#define XHCI_SLTST_DEFAULT      1   /**< Default. */
#define XHCI_SLTST_ADDRESSED    2   /**< Addressed. */
#define XHCI_SLTST_CONFIGURED   3   /**< Configured. */
/** @} */

#ifdef IN_RING3
/** Human-readable slot state descriptions for debugging. */
static const char * const g_apszSltStates[] = {
    "Enabled/Disabled", "Default", "Addressed", "Configured"   /* 0-3 */
};
#endif

/** Endpoint Context. */
typedef struct sXHCI_EP_CTX {
    uint32_t    ep_state    :  3;   /**< Endpoint state. */
    uint32_t    resvd0      :  5;
    uint32_t    mult        :  2;   /**< SS isoc burst count. */
    uint32_t    maxps       :  5;   /**< Max Primary Streams. */
    uint32_t    lsa         :  1;   /**< Linear Stream Array. */
    uint32_t    interval    :  8;   /**< USB request interval. */
    uint32_t    resvd1      :  8;
    uint32_t    resvd2      :  1;
    uint32_t    c_err       :  2;   /**< Error count. */
    uint32_t    ep_type     :  3;   /**< Endpoint type. */
    uint32_t    resvd3      :  1;
    uint32_t    hid         :  1;   /**< Host Initiate Disable. */
    uint32_t    max_brs_sz  :  8;   /**< Max Burst Size. */
    uint32_t    max_pkt_sz  : 16;   /**< Max Packet Size. */
    uint64_t    trdp;               /**< TR Dequeue Pointer. */
    uint32_t    avg_trb_len : 16;   /**< Average TRB Length. */
    uint32_t    max_esit    : 16;   /**< Max EP Service Interval Time Payload. */
                                    /**< The rest for xHC (i.e. our own) use. */
    uint32_t    last_frm    : 16;   /**< Last isochronous frame used (opaque). */
    uint32_t    ifc         :  8;   /**< isoch in-flight TD count (opaque). */
    uint32_t    last_cc     :  8;   /**< Last TRB completion code (opaque). */
    uint64_t    trep;               /**< TR Enqueue Pointer (opaque). */
} XHCI_EP_CTX;
AssertCompile(sizeof(XHCI_EP_CTX) == 0x20);

/** @name Endpoint Context states
 * @{ */
#define XHCI_EPST_DISABLED      0   /**< Disabled. */
#define XHCI_EPST_RUNNING       1   /**< Running. */
#define XHCI_EPST_HALTED        2   /**< Halted. */
#define XHCI_EPST_STOPPED       3   /**< Not running/stopped. */
#define XHCI_EPST_ERROR         4   /**< Not running/error. */
/** @} */

/** @name Endpoint Type values
 * @{ */
#define XHCI_EPTYPE_INVALID     0   /**< Not valid. */
#define XHCI_EPTYPE_ISOCH_OUT   1   /**< Isochronous Out. */
#define XHCI_EPTYPE_BULK_OUT    2   /**< Bulk Out. */
#define XHCI_EPTYPE_INTR_OUT    3   /**< Interrupt Out. */
#define XHCI_EPTYPE_CONTROL     4   /**< Control Bidi. */
#define XHCI_EPTYPE_ISOCH_IN    5   /**< Isochronous In. */
#define XHCI_EPTYPE_BULK_IN     6   /**< Bulk In. */
#define XHCI_EPTYPE_INTR_IN     7   /**< Interrupt In. */
/** @} */

/* Pick out transfer type from endpoint. */
#define XHCI_EP_XTYPE(a)        (a & 3)

/* Endpoint transfer types. */
#define XHCI_XFTYPE_CONTROL     0
#define XHCI_XFTYPE_ISOCH       XHCI_EPTYPE_ISOCH_OUT
#define XHCI_XFTYPE_BULK        XHCI_EPTYPE_BULK_OUT
#define XHCI_XFTYPE_INTR        XHCI_EPTYPE_INTR_OUT

/* Transfer Ring Dequeue Pointer address mask. */
#define XHCI_TRDP_ADDR_MASK     UINT64_C(0xFFFFFFFFFFFFFFF0)
#define XHCI_TRDP_DCS_MASK      RT_BIT(0)   /* Dequeue Cycle State bit. */


#ifdef IN_RING3

/* Human-readable endpoint state descriptions for debugging. */
static const char * const g_apszEpStates[] = {
    "Disabled", "Running", "Halted", "Stopped", "Error"     /* 0-4 */
};

/* Human-readable endpoint type descriptions for debugging. */
static const char * const g_apszEpTypes[] = {
    "Not Valid", "Isoch Out", "Bulk Out", "Interrupt Out",  /* 0-3 */
    "Control", "Isoch In", "Bulk In", "Interrupt In"        /* 4-7 */
};

#endif /* IN_RING3 */

/* Input Control Context. */
typedef struct sXHCI_INPC_CTX {
    uint32_t    drop_flags;         /* Drop Context flags (2-31). */
    uint32_t    add_flags;          /* Add Context flags (0-31). */
    uint32_t    resvd[6];
} XHCI_INPC_CTX;
AssertCompile(sizeof(XHCI_INPC_CTX) == 0x20);

/* Make sure all contexts are the same size. */
AssertCompile(sizeof(XHCI_EP_CTX) == sizeof(XHCI_SLOT_CTX));
AssertCompile(sizeof(XHCI_EP_CTX) == sizeof(XHCI_INPC_CTX));

/* -= Event Ring Segment Table =- */

/** Event Ring Segment Table Entry. */
typedef struct sXHCI_ERSTE {
    uint64_t    addr;
    uint16_t    size;
    uint16_t    resvd0;
    uint32_t    resvd1;
} XHCI_ERSTE;
AssertCompile(sizeof(XHCI_ERSTE) == 0x10);


/* -=-= Internal data structures not defined by xHCI =-=- */


/** Device slot entry -- either slot context or endpoint context. */
typedef union sXHCI_DS_ENTRY {
    XHCI_SLOT_CTX   sc;     /**< Slot context. */
    XHCI_EP_CTX     ep;     /**< Endpoint context. */
} XHCI_DS_ENTRY;

/** Full device context (slot context + 31 endpoint contexts). */
typedef struct sXHCI_DEV_CTX {
    XHCI_DS_ENTRY   entry[32];
} XHCI_DEV_CTX;
AssertCompile(sizeof(XHCI_DEV_CTX) == 32 * sizeof(XHCI_EP_CTX));
AssertCompile(sizeof(XHCI_DEV_CTX) == 32 * sizeof(XHCI_SLOT_CTX));

/** Pointer to the xHCI device state. */
typedef struct XHCI *PXHCI;

#ifndef VBOX_DEVICE_STRUCT_TESTCASE
/**
 * The xHCI controller data associated with each URB.
 */
typedef struct VUSBURBHCIINT
{
    /** The slot index. */
    uint8_t         uSlotID;
    /** Number of Tds in the array. */
    uint32_t        cTRB;
} VUSBURBHCIINT;
#endif

/**
 * An xHCI root hub port, shared.
 */
typedef struct XHCIHUBPORT
{
    /** PORTSC: Port status/control register (R/W). */
    uint32_t                portsc;
    /** PORTPM: Power management status/control register (R/W). */
    uint32_t                portpm;
    /** PORTLI: USB3 port link information (R/O). */
    uint32_t                portli;
} XHCIHUBPORT;
/** Pointer to a shared xHCI root hub port. */
typedef XHCIHUBPORT *PXHCIHUBPORT;

/**
 * An xHCI root hub port, ring-3.
 */
typedef struct XHCIHUBPORTR3
{
    /** Flag whether there is a device attached to the port. */
    bool                                fAttached;
} XHCIHUBPORTR3;
/** Pointer to a ring-3 xHCI root hub port. */
typedef XHCIHUBPORTR3 *PXHCIHUBPORTR3;

/**
 * The xHCI root hub, ring-3 only.
 *
 * @implements  PDMIBASE
 * @implements  VUSBIROOTHUBPORT
 */
typedef struct XHCIROOTHUBR3
{
    /** Pointer to the parent xHC. */
    R3PTRTYPE(struct XHCIR3 *)          pXhciR3;
    /** Pointer to the base interface of the VUSB RootHub. */
    R3PTRTYPE(PPDMIBASE)                pIBase;
    /** Pointer to the connector interface of the VUSB RootHub. */
    R3PTRTYPE(PVUSBIROOTHUBCONNECTOR)   pIRhConn;
    /** The base interface exposed to the roothub driver. */
    PDMIBASE                            IBase;
    /** The roothub port interface exposed to the roothub driver. */
    VUSBIROOTHUBPORT                    IRhPort;

    /** The LED for this hub. */
    PDMLED                              Led;

    /** Number of actually implemented ports. */
    uint8_t                             cPortsImpl;
    /** Index of first port for this hub. */
    uint8_t                             uPortBase;

    uint16_t                            Alignment0; /**< Force alignment. */
#if HC_ARCH_BITS == 64
    uint32_t                            Alignment1;
#endif
} XHCIROOTHUBR3;
/** Pointer to a xHCI root hub (ring-3 only). */
typedef XHCIROOTHUBR3 *PXHCIROOTHUBR3;

/**
 * An xHCI interrupter.
 */
typedef struct sXHCIINTRPTR
{
    /* Registers defined by xHCI. */
    /** IMAN: Interrupt Management Register (R/W). */
    uint32_t                iman;
    /** IMOD: Interrupt Moderation Register (R/W). */
    uint32_t                imod;
    /** ERSTSZ: Event Ring Segment Table Size (R/W). */
    uint32_t                erstsz;
    /* Reserved/padding. */
    uint32_t                reserved;
    /** ERSTBA: Event Ring Segment Table Base Address (R/W). */
    uint64_t                erstba;
    /** ERDP: Event Ring Dequeue Pointer (R/W). */
    uint64_t                erdp;
    /* Interrupter lock. */
    PDMCRITSECT             lock;
    /* Internal xHCI non-register state. */
    /** Internal Event Ring enqueue pointer. */
    uint64_t                erep;
    /** Internal ERDP re-write counter. */
    uint32_t                erdp_rewrites;
    /** This interrupter's index (for logging). */
    uint32_t                index;
    /** Internal index into Event Ring Segment Table. */
    uint16_t                erst_idx;
    /** Internal index into Event Ring Segment. */
    uint16_t                trb_count;
    /** Internal Event Ring Producer Cycle State. */
    bool                    evtr_pcs;
    /** Internal Interrupt Pending Enable flag. */
    bool                    ipe;
} XHCIINTRPTR, *PXHCIINTRPTR;

/**
 * xHCI device state.
 * @implements  PDMILEDPORTS
 */
typedef struct XHCI
{
    /** MFINDEX wraparound timer. */
    TMTIMERHANDLE                   hWrapTimer;

#ifdef XHCI_ERROR_INJECTION
    bool                            fDropIntrHw;
    bool                            fDropIntrIpe;
    bool                            fDropUrb;
    uint8_t                         Alignment00[1];
#else
    uint32_t                        Alignment00;    /**< Force alignment. */
#endif

    /** Flag indicating a pending worker thread notification. */
    volatile bool                   fNotificationSent;
    volatile bool                   afPadding[3];

    /** The event semaphore the worker thread waits on. */
    SUPSEMEVENT                     hEvtProcess;

    /** Bitmap for finished tasks (R3 -> Guest). */
    volatile uint32_t               u32TasksFinished;
    /** Bitmap for finished queued tasks (R3 -> Guest). */
    volatile uint32_t               u32QueuedTasksFinished;
    /** Bitmap for new queued tasks (Guest -> R3). */
    volatile uint32_t               u32TasksNew;

    /** Copy of XHCIR3::RootHub2::cPortsImpl. */
    uint8_t                         cUsb2Ports;
    /** Copy of XHCIR3::RootHub3::cPortsImpl. */
    uint8_t                         cUsb3Ports;
    /** Sum of cUsb2Ports and cUsb3Ports. */
    uint8_t                         cTotalPorts;
    /** Explicit padding. */
    uint8_t                         bPadding;

    /** Start of current frame. */
    uint64_t                        SofTime;
    /** State of the individual ports. */
    XHCIHUBPORT                     aPorts[XHCI_NDP_MAX];
    /** Interrupters array. */
    XHCIINTRPTR                     aInterrupters[XHCI_NINTR];

    /** @name Host Controller Capability Registers
     * @{ */
    /** CAPLENGTH: base + CAPLENGTH = operational register start (R/O). */
    uint32_t                        cap_length;
    /** HCIVERSION: host controller interface version (R/O). */
    uint32_t                        hci_version;
    /** HCSPARAMS: Structural parameters 1 (R/O). */
    uint32_t                        hcs_params1;
    /** HCSPARAMS: Structural parameters 2 (R/O). */
    uint32_t                        hcs_params2;
    /** HCSPARAMS: Structural parameters 3 (R/O). */
    uint32_t                        hcs_params3;
    /** HCCPARAMS: Capability parameters (R/O). */
    uint32_t                        hcc_params;
    /** DBOFF: Doorbell offset (R/O). */
    uint32_t                        dbell_off;
    /** RTSOFF: Run-time register space offset (R/O). */
    uint32_t                        rts_off;
    /** @} */

    /** @name Host Controller Operational Registers
     * @{ */
    /** USB command register - USBCMD (R/W). */
    uint32_t                        cmd;
    /** USB status register - USBSTS (R/W).*/
    uint32_t                        status;
    /** Device Control Notification register - DNCTRL (R/W). */
    uint32_t                        dnctrl;
    /** Configure Register (R/W). */
    uint32_t                        config;
    /** Command Ring Control Register - CRCR (R/W). */
    uint64_t                        crcr;
    /** Device Context Base Address Array Pointer (R/W). */
    uint64_t                        dcbaap;
    /** @} */

    /** Extended Capabilities storage. */
    uint8_t                         abExtCap[XHCI_EXT_CAP_SIZE];
    /** Size of valid extended capabilities. */
    uint32_t                        cbExtCap;

    uint32_t                        Alignment1;     /**< Align cmdr_dqp. */

    /** @name Internal xHCI non-register state
     * @{ */
    /** Internal Command Ring dequeue pointer. */
    uint64_t                        cmdr_dqp;
    /** Internal Command Ring Consumer Cycle State. */
    bool                            cmdr_ccs;
    uint8_t                         aAlignment2[7];   /**< Force alignment. */
    /** Internal Device Slot states. */
    uint8_t                         aSlotState[XHCI_NDS];
    /** Internal doorbell states. Each bit corresponds to an endpoint. */
    uint32_t                        aBellsRung[XHCI_NDS];
    /** @} */

    /** @name Model specific configuration
     * @{ */
    /** ERST address mask. */
    uint64_t                        erst_addr_mask;
    /** @} */

    /** The MMIO region. */
    IOMMMIOHANDLE                   hMmio;

    /** Detected isochronous URBs completed with error. */
    STAMCOUNTER                     StatErrorIsocUrbs;
    /** Detected isochronous packets (not URBs!) with error. */
    STAMCOUNTER                     StatErrorIsocPkts;

    /** Event TRBs written to event ring(s). */
    STAMCOUNTER                     StatEventsWritten;
    /** Event TRBs not written to event ring(s) due to HC being stopped. */
    STAMCOUNTER                     StatEventsDropped;
    /** Requests to set the IP bit. */
    STAMCOUNTER                     StatIntrsPending;
    /** Actual interrupt deliveries. */
    STAMCOUNTER                     StatIntrsSet;
    /** Interrupts not raised because they were disabled. */
    STAMCOUNTER                     StatIntrsNotSet;
    /** A pending interrupt was cleared. */
    STAMCOUNTER                     StatIntrsCleared;
    /** Number of TRBs that formed a single control URB. */
    STAMCOUNTER                     StatTRBsPerCtlUrb;
    /** Number of TRBs that formed a single data (bulk/interrupt) URB. */
    STAMCOUNTER                     StatTRBsPerDtaUrb;
    /** Number of TRBs that formed a single isochronous URB. */
    STAMCOUNTER                     StatTRBsPerIsoUrb;
    /** Size of a control URB in bytes. */
    STAMCOUNTER                     StatUrbSizeCtrl;
    /** Size of a data URB in bytes. */
    STAMCOUNTER                     StatUrbSizeData;
    /** Size of an isochronous URB in bytes. */
    STAMCOUNTER                     StatUrbSizeIsoc;

#ifdef VBOX_WITH_STATISTICS
    /** @name Register access counters.
     * @{ */
    STAMCOUNTER                     StatRdCaps;
    STAMCOUNTER                     StatRdCmdRingCtlHi;
    STAMCOUNTER                     StatRdCmdRingCtlLo;
    STAMCOUNTER                     StatRdConfig;
    STAMCOUNTER                     StatRdDevCtxBaapHi;
    STAMCOUNTER                     StatRdDevCtxBaapLo;
    STAMCOUNTER                     StatRdDevNotifyCtrl;
    STAMCOUNTER                     StatRdDoorBell;
    STAMCOUNTER                     StatRdEvtRingDeqPtrHi;
    STAMCOUNTER                     StatRdEvtRingDeqPtrLo;
    STAMCOUNTER                     StatRdEvtRsTblBaseHi;
    STAMCOUNTER                     StatRdEvtRsTblBaseLo;
    STAMCOUNTER                     StatRdEvtRstblSize;
    STAMCOUNTER                     StatRdEvtRsvd;
    STAMCOUNTER                     StatRdIntrMgmt;
    STAMCOUNTER                     StatRdIntrMod;
    STAMCOUNTER                     StatRdMfIndex;
    STAMCOUNTER                     StatRdPageSize;
    STAMCOUNTER                     StatRdPortLinkInfo;
    STAMCOUNTER                     StatRdPortPowerMgmt;
    STAMCOUNTER                     StatRdPortRsvd;
    STAMCOUNTER                     StatRdPortStatusCtrl;
    STAMCOUNTER                     StatRdUsbCmd;
    STAMCOUNTER                     StatRdUsbSts;
    STAMCOUNTER                     StatRdUnknown;

    STAMCOUNTER                     StatWrCmdRingCtlHi;
    STAMCOUNTER                     StatWrCmdRingCtlLo;
    STAMCOUNTER                     StatWrConfig;
    STAMCOUNTER                     StatWrDevCtxBaapHi;
    STAMCOUNTER                     StatWrDevCtxBaapLo;
    STAMCOUNTER                     StatWrDevNotifyCtrl;
    STAMCOUNTER                     StatWrDoorBell0;
    STAMCOUNTER                     StatWrDoorBellN;
    STAMCOUNTER                     StatWrEvtRingDeqPtrHi;
    STAMCOUNTER                     StatWrEvtRingDeqPtrLo;
    STAMCOUNTER                     StatWrEvtRsTblBaseHi;
    STAMCOUNTER                     StatWrEvtRsTblBaseLo;
    STAMCOUNTER                     StatWrEvtRstblSize;
    STAMCOUNTER                     StatWrIntrMgmt;
    STAMCOUNTER                     StatWrIntrMod;
    STAMCOUNTER                     StatWrPortPowerMgmt;
    STAMCOUNTER                     StatWrPortStatusCtrl;
    STAMCOUNTER                     StatWrUsbCmd;
    STAMCOUNTER                     StatWrUsbSts;
    STAMCOUNTER                     StatWrUnknown;
    /** @} */
#endif
} XHCI;

/**
 * xHCI device state, ring-3 edition.
 * @implements  PDMILEDPORTS
 */
typedef struct XHCIR3
{
    /** The async worker thread. */
    R3PTRTYPE(PPDMTHREAD)           pWorkerThread;
    /** The device instance.
     * @note This is only so interface functions can get their bearings.  */
    PPDMDEVINSR3                    pDevIns;

    /** Status LUN: The base interface. */
    PDMIBASE                        IBase;
    /** Status LUN: Leds interface. */
    PDMILEDPORTS                    ILeds;
    /** Status LUN: Partner of ILeds. */
    R3PTRTYPE(PPDMILEDCONNECTORS)   pLedsConnector;

    /** USB 2.0 Root hub device. */
    XHCIROOTHUBR3                   RootHub2;
    /** USB 3.0 Root hub device. */
    XHCIROOTHUBR3                   RootHub3;

    /** State of the individual ports. */
    XHCIHUBPORTR3                   aPorts[XHCI_NDP_MAX];

    /** Critsect to synchronize worker and I/O completion threads. */
    RTCRITSECT                      CritSectThrd;
} XHCIR3;
/** Pointer to ring-3 xHCI device state. */
typedef XHCIR3 *PXHCIR3;

/**
 * xHCI device data, ring-0 edition.
 */
typedef struct XHCIR0
{
    uint32_t uUnused;
} XHCIR0;
/** Pointer to ring-0 xHCI device data. */
typedef struct XHCIR0 *PXHCIR0;


/**
 * xHCI device data, raw-mode edition.
 */
typedef struct XHCIRC
{
    uint32_t uUnused;
} XHCIRC;
/** Pointer to raw-mode xHCI device data. */
typedef struct XHCIRC *PXHCIRC;


/** @typedef XHCICC
 * The xHCI device data for the current context. */
typedef CTX_SUFF(XHCI) XHCICC;
/** @typedef PXHCICC
 * Pointer to the xHCI device for the current context. */
typedef CTX_SUFF(PXHCI) PXHCICC;


/* -=-= Local implementation details =-=- */

typedef enum sXHCI_JOB {
    XHCI_JOB_PROCESS_CMDRING,   /**< Process the command ring. */
    XHCI_JOB_DOORBELL,          /**< A doorbell (other than DB0) was rung. */
    XHCI_JOB_XFER_DONE,         /**< Transfer completed, look for more work. */
    XHCI_JOB_MAX
} XHCI_JOB;

/* -=-=- Local xHCI definitions -=-=- */

/** @name USB states.
 * @{ */
#define XHCI_USB_RESET              0x00
#define XHCI_USB_RESUME             0x40
#define XHCI_USB_OPERATIONAL        0x80
#define XHCI_USB_SUSPEND            0xc0
/** @} */

/* Primary interrupter (for readability). */
#define XHCI_PRIMARY_INTERRUPTER    0

/** @name Device Slot states.
 * @{ */
#define XHCI_DEVSLOT_EMPTY          0
#define XHCI_DEVSLOT_ENABLED        1
#define XHCI_DEVSLOT_DEFAULT        2
#define XHCI_DEVSLOT_ADDRESSED      3
#define XHCI_DEVSLOT_CONFIGURED     4
/** @} */

/** Get the pointer to a root hub corresponding to given port index. */
#define GET_PORT_PRH(a_pThisCC, a_uPort) \
    ((a_uPort) >= (a_pThisCC)->RootHub2.cPortsImpl ? &(a_pThisCC)->RootHub3 : &(a_pThisCC)->RootHub2)
#define GET_VUSB_PORT_FROM_XHCI_PORT(a_pRh, a_iPort) \
    (((a_iPort) - (a_pRh)->uPortBase) + 1)
#define GET_XHCI_PORT_FROM_VUSB_PORT(a_pRh, a_uPort) \
    ((a_pRh)->uPortBase + (a_uPort) - 1)

/** Check if port corresponding to index is USB3, using shared data. */
#define IS_USB3_PORT_IDX_SHR(a_pThis, a_uPort)  ((a_uPort) >= (a_pThis)->cUsb2Ports)

/** Check if port corresponding to index is USB3, using ring-3 data. */
#define IS_USB3_PORT_IDX_R3(a_pThisCC, a_uPort) ((a_uPort) >= (a_pThisCC)->RootHub2.cPortsImpl)

/** Query the number of configured USB2 ports. */
#define XHCI_NDP_USB2(a_pThisCC)        ((unsigned)(a_pThisCC)->RootHub2.cPortsImpl)

/** Query the number of configured USB3 ports. */
#define XHCI_NDP_USB3(a_pThisCC)        ((unsigned)(a_pThisCC)->RootHub3.cPortsImpl)

/** Query the total number of configured ports. */
#define XHCI_NDP_CFG(a_pThis)           ((unsigned)RT_MIN((a_pThis)->cTotalPorts, XHCI_NDP_MAX))


#ifndef VBOX_DEVICE_STRUCT_TESTCASE


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

#ifdef IN_RING3

/** Build a Protocol extended capability. */
static uint32_t xhciR3BuildProtocolCaps(uint8_t *pbCap, uint32_t cbMax, int cPorts, int nPortOfs, int ver)
{
    uint32_t    *pu32Cap = (uint32_t *)pbCap;
    unsigned    cPsi;

    Assert(nPortOfs + cPorts < 255);
    Assert(ver == 2 || ver == 3);

    cPsi = 0;   /* Currently only implied port speed IDs. */

    /* Make sure there's enough room. */
    if (cPsi * 4 + 16 > cbMax)
        return 0;

    /* Header - includes (USB) specification version. */
    *pu32Cap++ = (ver << 24) | (0 << 16) | XHCI_XCP_PROTOCOL;
    /* Specification - 'USB ' */
    *pu32Cap++ = 0x20425355;
    /* Port offsets and counts. 1-based! */
    *pu32Cap++ = (cPsi << 28) | (cPorts << 8) | (nPortOfs + 1);
    /* Reserved dword. */
    *pu32Cap++ = 0;

    return (uint8_t *)pu32Cap - pbCap;
}


/** Add an extended capability and link it into the chain. */
static int xhciR3AddExtCap(PXHCI pThis, const uint8_t *pCap, uint32_t cbCap, uint32_t *puPrevOfs)
{
    Assert(*puPrevOfs <= pThis->cbExtCap);
    Assert(!(cbCap & 3));

    /* Check that the extended capability is sane. */
    if (cbCap == 0)
        return VERR_BUFFER_UNDERFLOW;
    if (pThis->cbExtCap + cbCap > XHCI_EXT_CAP_SIZE)
        return VERR_BUFFER_OVERFLOW;
    if (cbCap > 255 * 4)    /* Size must fit into 8-bit dword count. */
        return VERR_BUFFER_OVERFLOW;

    /* Copy over the capability data and update offsets. */
    memcpy(pThis->abExtCap + pThis->cbExtCap, pCap, cbCap);
    pThis->abExtCap[*puPrevOfs + 1] = cbCap >> 2;
    pThis->abExtCap[pThis->cbExtCap + 1] = 0;
    *puPrevOfs = pThis->cbExtCap;
    pThis->cbExtCap += cbCap;
    return VINF_SUCCESS;
}

/** Build the xHCI Extended Capabilities region. */
static int xhciR3BuildExtCaps(PXHCI pThis, PXHCICC pThisCC)
{
    int         rc;
    uint8_t     abXcp[MAX_XCAP_SIZE];
    uint32_t    cbXcp;
    uint32_t    uPrevOfs = 0;

    Assert(XHCI_NDP_USB2(pThisCC));
    Assert(XHCI_NDP_USB3(pThisCC));

    /* Most of the extended capabilities are optional or not relevant for PCI
     * implementations. However, the Supported Protocol caps are required.
     */
    cbXcp = xhciR3BuildProtocolCaps(abXcp, sizeof(abXcp), XHCI_NDP_USB2(pThisCC), 0, 2);
    rc = xhciR3AddExtCap(pThis, abXcp, cbXcp, &uPrevOfs);
    AssertReturn(RT_SUCCESS(rc), rc);

    cbXcp = xhciR3BuildProtocolCaps(abXcp, sizeof(abXcp), XHCI_NDP_USB3(pThisCC), XHCI_NDP_USB2(pThisCC), 3);
    rc = xhciR3AddExtCap(pThis, abXcp, cbXcp, &uPrevOfs);
    AssertReturn(RT_SUCCESS(rc), rc);

    return VINF_SUCCESS;
}


/**
 * Select an unused device address. Note that this may fail in the unlikely
 * case where all possible addresses are exhausted.
 */
static uint8_t xhciR3SelectNewAddress(PXHCI pThis, uint8_t uSlotID)
{
    RT_NOREF(pThis, uSlotID);

    /*
     * Since there is a 1:1 mapping between USB devices and device slots, we
     * should be able to assign a USB address which equals slot ID to any USB
     * device. However, the address selection algorithm could be completely
     * different (it is not defined by the xHCI spec).
     */
    return uSlotID;
}


/**
 * Read the address of a device context for a slot from the DCBAA.
 *
 * @returns Given slot's device context base address.
 * @param   pDevIns         The device instance.
 * @param   pThis           Pointer to the xHCI state.
 * @param   uSlotID         Slot ID to get the context address of.
 */
static uint64_t xhciR3FetchDevCtxAddr(PPDMDEVINS pDevIns, PXHCI pThis, uint8_t uSlotID)
{
    uint64_t        uCtxAddr;
    RTGCPHYS        GCPhysDCBAAE;

    Assert(uSlotID > 0);
    Assert(uSlotID < XHCI_NDS);

    /* Fetch the address of the output slot context from the DCBAA. */
    GCPhysDCBAAE = pThis->dcbaap + uSlotID * sizeof(uint64_t);
    PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysDCBAAE, &uCtxAddr, sizeof(uCtxAddr));
    LogFlowFunc(("Slot ID %u, device context @ %RGp\n", uSlotID, uCtxAddr));
    Assert(uCtxAddr);

    return uCtxAddr & XHCI_CTX_ADDR_MASK;
}


/**
 * Fetch a device's slot or endpoint context from memory.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The xHCI device state.
 * @param   uSlotID     Slot ID to access.
 * @param   uDCI        Device Context Index.
 * @param   pCtx        Pointer to storage for the context.
 */
static int xhciR3FetchDevCtx(PPDMDEVINS pDevIns, PXHCI pThis, uint8_t uSlotID, uint8_t uDCI, void *pCtx)
{
    RTGCPHYS    GCPhysCtx;

    GCPhysCtx = xhciR3FetchDevCtxAddr(pDevIns, pThis, uSlotID);
    LogFlowFunc(("Reading device context @ %RGp, DCI %u\n", GCPhysCtx, uDCI));
    GCPhysCtx += uDCI * sizeof(XHCI_SLOT_CTX);
    PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysCtx, pCtx, sizeof(XHCI_SLOT_CTX));
    return VINF_SUCCESS;
}


/**
 * Fetch a device's slot and endpoint contexts from guest memory.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The xHCI device state.
 * @param   uSlotID     Slot ID to access.
 * @param   uDCI        Endpoint Device Context Index.
 * @param   pSlot       Pointer to storage for the slot context.
 * @param   pEp         Pointer to storage for the endpoint context.
 */
static int xhciR3FetchCtxAndEp(PPDMDEVINS pDevIns, PXHCI pThis, uint8_t uSlotID, uint8_t uDCI, XHCI_SLOT_CTX *pSlot, XHCI_EP_CTX *pEp)
{
    AssertPtr(pSlot);
    AssertPtr(pEp);
    Assert(uDCI);   /* Can't be 0 -- that's the device context. */

    /* Load the slot context. */
    xhciR3FetchDevCtx(pDevIns, pThis, uSlotID, 0, pSlot);
    /// @todo sanity check the slot context here?
    Assert(pSlot->ctx_ent >= uDCI);

    /* Load the endpoint context. */
    xhciR3FetchDevCtx(pDevIns, pThis, uSlotID, uDCI, pEp);
    /// @todo sanity check the endpoint context here?

    return VINF_SUCCESS;
}


/**
 * Update an endpoint context in guest memory.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The xHCI device state.
 * @param   uSlotID     Slot ID to access.
 * @param   uDCI        Endpoint Device Context Index.
 * @param   pEp         Pointer to storage of the endpoint context.
 */
static int xhciR3WriteBackEp(PPDMDEVINS pDevIns, PXHCI pThis, uint8_t uSlotID, uint8_t uDCI, XHCI_EP_CTX *pEp)
{
    RTGCPHYS    GCPhysCtx;

    AssertPtr(pEp);
    Assert(uDCI);   /* Can't be 0 -- that's the device context. */

    /// @todo sanity check the endpoint context here?
    /* Find the physical address. */
    GCPhysCtx = xhciR3FetchDevCtxAddr(pDevIns, pThis, uSlotID);
    LogFlowFunc(("Writing device context @ %RGp, DCI %u\n", GCPhysCtx, uDCI));
    GCPhysCtx += uDCI * sizeof(XHCI_SLOT_CTX);
    /* Write the updated context. */
    PDMDevHlpPCIPhysWriteMeta(pDevIns, GCPhysCtx, pEp, sizeof(XHCI_EP_CTX));

    return VINF_SUCCESS;
}


/**
 * Modify an endpoint context such that it enters the running state.
 *
 * @param   pEpCtx          Pointer to the endpoint context.
 */
static void xhciR3EnableEP(XHCI_EP_CTX *pEpCtx)
{
    LogFlow(("Enabling EP, TRDP @ %RGp, DCS=%u\n", pEpCtx->trdp & XHCI_TRDP_ADDR_MASK, pEpCtx->trdp & XHCI_TRDP_DCS_MASK));
    pEpCtx->ep_state = XHCI_EPST_RUNNING;
    pEpCtx->trep     = pEpCtx->trdp;
}

#endif /* IN_RING3 */

#define MFIND_PERIOD_NS     (UINT64_C(2048) * 1000000)

/**
 * Set up the MFINDEX wrap timer.
 */
static void xhciSetWrapTimer(PPDMDEVINS pDevIns, PXHCI pThis)
{
    uint64_t        u64Now;
    uint64_t        u64LastWrap;
    uint64_t        u64Expire;
    int             rc;

    /* Try to avoid drift. */
    u64Now      = PDMDevHlpTimerGet(pDevIns, pThis->hWrapTimer);
//    u64LastWrap = u64Now - (u64Now % (0x3FFF * 125000));
    u64LastWrap = u64Now / MFIND_PERIOD_NS * MFIND_PERIOD_NS;
    /* The MFINDEX counter wraps around every 2048 milliseconds. */
    u64Expire   = u64LastWrap + (uint64_t)2048 * 1000000;
    rc = PDMDevHlpTimerSet(pDevIns, pThis->hWrapTimer, u64Expire);
    AssertRC(rc);
}

/**
 * Determine whether MSI/MSI-X is enabled for this PCI device.
 *
 * This influences interrupt handling in xHCI. NB: There should be a PCIDevXxx
 * function for this.
 */
static bool xhciIsMSIEnabled(PPDMPCIDEV pDevIns)
{
    uint16_t    uMsgCtl;

    uMsgCtl = PDMPciDevGetWord(pDevIns, XHCI_PCI_MSI_CAP_OFS + VBOX_MSI_CAP_MESSAGE_CONTROL);
    return !!(uMsgCtl & VBOX_PCI_MSI_FLAGS_ENABLE);
}

/**
 * Get the worker thread going -- there's something to do.
 */
static void xhciKickWorker(PPDMDEVINS pDevIns, PXHCI pThis, XHCI_JOB enmJob, uint32_t uWorkDesc)
{
    RT_NOREF(enmJob, uWorkDesc);

    /* Tell the worker thread there's something to do. */
    if (!ASMAtomicXchgBool(&pThis->fNotificationSent, true))
    {
        LogFlowFunc(("Signal event semaphore\n"));
        int rc = PDMDevHlpSUPSemEventSignal(pDevIns, pThis->hEvtProcess);
        AssertRC(rc);
    }
}

/**
 * Fetch the current ERST entry from guest memory.
 */
static void xhciFetchErstEntry(PPDMDEVINS pDevIns, PXHCI pThis, PXHCIINTRPTR ip)
{
    RTGCPHYS        GCPhysErste;
    XHCI_ERSTE      entry;

    Assert(ip->erst_idx < ip->erstsz);
    GCPhysErste = ip->erstba + ip->erst_idx * sizeof(XHCI_ERSTE);
    PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysErste, &entry, sizeof(entry));

    /*
     * 6.5 claims values in 16-4096 range are valid, but does not say what
     * happens for values outside of that range...
     */
    Assert((pThis->status & XHCI_STATUS_HCH) || (entry.size >= 16 && entry.size <= 4096));

    /* Cache the entry data internally. */
    ip->erep      = entry.addr & pThis->erst_addr_mask;
    ip->trb_count = entry.size;
    Log(("Fetched ERST Entry at %RGp: %u entries at %RGp\n", GCPhysErste, ip->trb_count, ip->erep));
}

/**
 * Set the interrupter's IP and EHB bits and trigger an interrupt if required.
 *
 * @param   pDevIns         The PDM device instance.
 * @param   pThis           Pointer to the xHCI state.
 * @param   ip              Pointer to the interrupter structure.
 *
 */
static void xhciSetIntr(PPDMDEVINS pDevIns, PXHCI pThis, PXHCIINTRPTR ip)
{
    Assert(pThis && ip);
    LogFlowFunc(("old IP: %u\n", !!(ip->iman & XHCI_IMAN_IP)));

    if (!(ip->iman & XHCI_IMAN_IP))
    {
        /// @todo assert that we own the interrupter lock
        ASMAtomicOrU32(&pThis->status, XHCI_STATUS_EINT);
        ASMAtomicOrU64(&ip->erdp, XHCI_ERDP_EHB);
        ASMAtomicOrU32(&ip->iman, XHCI_IMAN_IP);
        if ((ip->iman & XHCI_IMAN_IE) && (pThis->cmd & XHCI_CMD_INTE))
        {
#ifdef XHCI_ERROR_INJECTION
            if (pThis->fDropIntrHw)
            {
                pThis->fDropIntrHw = false;
                ASMAtomicAndU32(&ip->iman, ~XHCI_IMAN_IP);
            }
            else
#endif
            {
                Log2(("Triggering interrupt on interrupter %u\n", ip->index));
                PDMDevHlpPCISetIrq(pDevIns, 0, PDM_IRQ_LEVEL_HIGH);
                STAM_COUNTER_INC(&pThis->StatIntrsSet);
            }
        }
        else
        {
            Log2(("Not triggering interrupt on interrupter %u (interrupts disabled)\n", ip->index));
            STAM_COUNTER_INC(&pThis->StatIntrsNotSet);
        }

        /* If MSI/MSI-X is in use, the IP bit is immediately cleared again. */
        if (xhciIsMSIEnabled(pDevIns->apPciDevs[0]))
            ASMAtomicAndU32(&ip->iman, ~XHCI_IMAN_IP);
    }
}

#ifdef IN_RING3

/**
 * Set the interrupter's IPE bit. If this causes a 0->1 transition, an
 * interrupt may be triggered.
 *
 * @param   pDevIns         The PDM device instance.
 * @param   pThis           Pointer to the xHCI state.
 * @param   ip              Pointer to the interrupter structure.
 */
static void xhciR3SetIntrPending(PPDMDEVINS pDevIns, PXHCI pThis, PXHCIINTRPTR ip)
{
    uint16_t        imodc = (ip->imod >> XHCI_IMOD_IMODC_SHIFT) & XHCI_IMOD_IMODC_MASK;

    Assert(pThis && ip);
    LogFlowFunc(("old IPE: %u, IMODC: %u, EREP: %RGp, EHB: %u\n", ip->ipe, imodc, (RTGCPHYS)ip->erep, !!(ip->erdp & XHCI_ERDP_EHB)));
    STAM_COUNTER_INC(&pThis->StatIntrsPending);

    if (!ip->ipe)
    {
#ifdef XHCI_ERROR_INJECTION
        if (pThis->fDropIntrIpe)
        {
            pThis->fDropIntrIpe = false;
        }
        else
#endif
        {
            ip->ipe = true;
            if (!(ip->erdp & XHCI_ERDP_EHB) && (imodc == 0))
                xhciSetIntr(pDevIns, pThis, ip);
        }
    }
}


/**
 * Check if there is space available for writing at least two events on the
 * event ring. See 4.9.4 for the state machine (right hand side of diagram).
 * If there's only room for one event, the Event Ring Full TRB will need to
 * be written out, hence the ring is considered full.
 *
 * @returns True if space is available, false otherwise.
 * @param   pDevIns         The PDM device instance.
 * @param   pThis           Pointer to the xHCI state.
 * @param   pIntr           Pointer to the interrupter structure.
 */
static bool xhciR3IsEvtRingFull(PPDMDEVINS pDevIns, PXHCI pThis, PXHCIINTRPTR pIntr)
{
    uint64_t    next_ptr;
    uint64_t    erdp = pIntr->erdp & XHCI_ERDP_ADDR_MASK;

    if (pIntr->trb_count > 1)
    {
        /* Check the current segment. */
        next_ptr = pIntr->erep + sizeof(XHCI_EVENT_TRB);
    }
    else
    {
        uint16_t    erst_idx;
        XHCI_ERSTE  entry;
        RTGCPHYS    GCPhysErste;

        /* Need to check the next segment. */
        erst_idx = pIntr->erst_idx + 1;
        if (erst_idx == pIntr->erstsz)
            erst_idx = 0;
        GCPhysErste = pIntr->erstba + erst_idx * sizeof(XHCI_ERSTE);
        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysErste, &entry, sizeof(entry));
        next_ptr = entry.addr & pThis->erst_addr_mask;
    }

    /// @todo We'll have to remember somewhere that the ring is full
    return erdp == next_ptr;
}

/**
 * Write an event to the given Event Ring. This implements a good chunk of
 * the event ring state machine in section 4.9.4 of the xHCI spec.
 *
 * @returns VBox status code. Error if event could not be enqueued.
 * @param   pDevIns         The PDM device instance.
 * @param   pThis           Pointer to the xHCI state.
 * @param   pEvent          Pointer to the Event TRB to be enqueued.
 * @param   iIntr           Index of the interrupter to write to.
 * @param   fBlockInt       Set if interrupt should be blocked (BEI bit).
 */
static int xhciR3WriteEvent(PPDMDEVINS pDevIns, PXHCI pThis, XHCI_EVENT_TRB *pEvent, unsigned iIntr, bool fBlockInt)
{
    PXHCIINTRPTR    pIntr;
    int             rc = VINF_SUCCESS;

    LogFlowFunc(("Interrupter: %u\n", iIntr));

    /* If the HC isn't running, events can not be generated. However,
     * especially port change events can be triggered at any time. We just
     * drop them here -- it's often not an error condition.
     */
    if (pThis->cmd & XHCI_CMD_RS)
    {
        STAM_COUNTER_INC(&pThis->StatEventsWritten);
        Assert(iIntr < XHCI_NINTR); /* Supplied by guest, potentially invalid. */
        pIntr = &pThis->aInterrupters[iIntr & XHCI_INTR_MASK];

        /*
         * If the interrupter/event ring isn't in a sane state, just
         * give up and report Host Controller Error (HCE).
         */
    //    pIntr->erst_idx

        int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pIntr->lock, VERR_IGNORED);   /* R3 only, no rcBusy. */
        PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pIntr->lock, rcLock); /* eventually, most call chains ignore the status. */

        if (xhciR3IsEvtRingFull(pDevIns, pThis, pIntr))
        {
            LogRel(("xHCI: Event ring full!\n"));
        }

        /* Set the TRB's Cycle bit as appropriate. */
        pEvent->gen.cycle = pIntr->evtr_pcs;

        /* Write out the TRB and advance the EREP. */
        /// @todo This either has to be atomic from the guest's POV or the cycle bit needs to be toggled last!!
        PDMDevHlpPCIPhysWriteMeta(pDevIns, pIntr->erep, pEvent, sizeof(*pEvent));
        pIntr->erep += sizeof(*pEvent);
        --pIntr->trb_count;

        /* Advance to the next ERST entry if necessary. */
        if (pIntr->trb_count == 0)
        {
            ++pIntr->erst_idx;
            /* If necessary, roll over back to the beginning. */
            if (pIntr->erst_idx == pIntr->erstsz)
            {
                pIntr->erst_idx = 0;
                pIntr->evtr_pcs = !pIntr->evtr_pcs;
            }
            xhciFetchErstEntry(pDevIns, pThis, pIntr);
        }

        /* Set the IPE bit unless interrupts are blocked. */
        if (!fBlockInt)
            xhciR3SetIntrPending(pDevIns, pThis, pIntr);

        PDMDevHlpCritSectLeave(pDevIns, &pIntr->lock);
    }
    else
    {
        STAM_COUNTER_INC(&pThis->StatEventsDropped);
        Log(("Event dropped because HC is not running.\n"));
    }

    return rc;
}


/**
 * Post a port change TRB to an Event Ring.
 */
static int xhciR3GenPortChgEvent(PPDMDEVINS pDevIns, PXHCI pThis, uint8_t uPort)
{
    XHCI_EVENT_TRB  ed; /* Event Descriptor */
    LogFlowFunc(("Port ID: %u\n", uPort));

    /*
     * Devices can be "physically" attached/detached regardless of whether
     * the HC is running or not, but the port status change events can only
     * be generated when R/S is set; xhciR3WriteEvent() takes care of that.
     */
    RT_ZERO(ed);
    ed.psce.cc      = XHCI_TCC_SUCCESS;
    ed.psce.port_id = uPort;
    ed.psce.type    = XHCI_TRB_PORT_SC;
    return xhciR3WriteEvent(pDevIns, pThis, &ed, XHCI_PRIMARY_INTERRUPTER, false);
}


/**
 * Post a command completion TRB to an Event Ring.
 */
static int xhciR3PostCmdCompletion(PPDMDEVINS pDevIns, PXHCI pThis, unsigned cc, unsigned uSlotID)
{
    XHCI_EVENT_TRB      ed; /* Event Descriptor */
    LogFlowFunc(("Cmd @ %RGp, Completion Code: %u (%s), Slot ID: %u\n", (RTGCPHYS)pThis->cmdr_dqp, cc,
                 cc < RT_ELEMENTS(g_apszCmplCodes) ? g_apszCmplCodes[cc] : "WHAT?!!", uSlotID));

    /* The Command Ring dequeue pointer still holds the address of the current
     * command TRB. It is written to the completion event TRB as the command
     * TRB pointer.
     */
    RT_ZERO(ed);
    ed.cce.trb_ptr = pThis->cmdr_dqp;
    ed.cce.cc      = cc;
    ed.cce.type    = XHCI_TRB_CMD_CMPL;
    ed.cce.slot_id = uSlotID;
    return xhciR3WriteEvent(pDevIns, pThis, &ed, XHCI_PRIMARY_INTERRUPTER, false);
}


/**
 * Post a transfer event TRB to an Event Ring.
 */
static int xhciR3PostXferEvent(PPDMDEVINS pDevIns, PXHCI pThis, unsigned uIntTgt, unsigned uXferLen, unsigned cc,
                               unsigned uSlotID, unsigned uEpDCI, uint64_t uEvtData, bool fBlockInt, bool fEvent)
{
    XHCI_EVENT_TRB      ed; /* Event Descriptor */
    LogFlowFunc(("Xfer @ %RGp, Completion Code: %u (%s), Slot ID=%u DCI=%u Target=%u EvtData=%RX64 XfrLen=%u BEI=%u ED=%u\n",
                 (RTGCPHYS)pThis->cmdr_dqp, cc, cc < RT_ELEMENTS(g_apszCmplCodes) ? g_apszCmplCodes[cc] : "WHAT?!!",
                 uSlotID, uEpDCI, uIntTgt, uEvtData, uXferLen, fBlockInt, fEvent));

    /* A transfer event may be either generated by TRB completion (in case
     * fEvent=false) or by a special transfer event TRB (fEvent=true). In
     * either case, interrupts may be suppressed.
     */
    RT_ZERO(ed);
    ed.te.trb_ptr = uEvtData;
    ed.te.xfr_len = uXferLen;
    ed.te.cc      = cc;
    ed.te.ed      = fEvent;
    ed.te.type    = XHCI_TRB_XFER;
    ed.te.ep_id   = uEpDCI;
    ed.te.slot_id = uSlotID;
    return xhciR3WriteEvent(pDevIns, pThis, &ed, uIntTgt, fBlockInt);   /* Sets the cycle bit, too. */
}


static int xhciR3FindRhDevBySlot(PPDMDEVINS pDevIns, PXHCI pThis, PXHCICC pThisCC, uint8_t uSlotID, PXHCIROOTHUBR3 *ppRh, uint32_t *puPort)
{
    XHCI_SLOT_CTX   slot_ctx;
    PXHCIROOTHUBR3  pRh;
    unsigned        iPort;
    int             rc;

    /// @todo Do any of these need to be release assertions?
    Assert(uSlotID <= RT_ELEMENTS(pThis->aSlotState));
    Assert(pThis->aSlotState[ID_TO_IDX(uSlotID)] > XHCI_DEVSLOT_EMPTY);

    /* Load the slot context. */
    xhciR3FetchDevCtx(pDevIns, pThis, uSlotID, 0, &slot_ctx);

    /* The port ID is stored in the slot context. */
    iPort = ID_TO_IDX(slot_ctx.rh_port);
    if (iPort < XHCI_NDP_CFG(pThis))
    {
        /* Find the corresponding root hub. */
        pRh = GET_PORT_PRH(pThisCC, iPort);
        Assert(pRh);

        /* And the device; if the device was ripped out fAttached will be false. */
        if (pThisCC->aPorts[iPort].fAttached)
        {
            /* Provide the information the caller asked for. */
            if (ppRh)
                *ppRh  = pRh;
            if (puPort)
                *puPort = GET_VUSB_PORT_FROM_XHCI_PORT(pRh, iPort);
            rc = VINF_SUCCESS;
        }
        else
        {
            LogFunc(("No device attached (port index %u)!\n", iPort));
            rc = VERR_VUSB_DEVICE_NOT_ATTACHED;
        }
    }
    else
    {
        LogFunc(("Port out of range (index %u)!\n", iPort));
        rc = VERR_INVALID_PARAMETER;
    }
    return rc;
}


static void xhciR3EndlessTrbError(PPDMDEVINS pDevIns, PXHCI pThis)
{
    /* Clear the R/S bit and indicate controller error. */
    ASMAtomicAndU32(&pThis->cmd, ~XHCI_CMD_RS);
    ASMAtomicOrU32(&pThis->status, XHCI_STATUS_HCE);

    /* Ensure that XHCI_STATUS_HCH gets set by the worker thread. */
    xhciKickWorker(pDevIns, pThis, XHCI_JOB_XFER_DONE, 0);

    LogRelMax(10, ("xHCI: Attempted to process too many TRBs, stopping xHC!\n"));
}

/**
 * TRB walker callback prototype.
 *
 * @returns true if walking should continue.
 * @returns false if walking should be terminated.
 * @param   pDevIns         The device instance.
 * @param   pThis           The xHCI device state.
 * @param   pXferTRB        Pointer to the transfer TRB to handle.
 * @param   GCPhysXfrTRB    Physical address of the TRB.
 * @param   pvContext       User-defined walk context.
 * @remarks We don't need to use DECLCALLBACKPTR here, since all users are in
 *          the same source file, but having the functions marked with
 *          DECLCALLBACK helps readability.
 */
typedef DECLCALLBACKPTR(bool, PFNTRBWALKCB,(PPDMDEVINS pDevIns, PXHCI pThis, const XHCI_XFER_TRB *pXferTRB,
                                            RTGCPHYS GCPhysXfrTRB, void *pvContext));


/**
 * Walk a chain of TRBs which comprise a single TD.
 *
 * This is something we need to do potentially more than once when submitting a
 * URB and then often again when completing the URB. Note that the walker does
 * not update the endpoint state (TRDP/TREP/DCS) so that it can be re-run
 * multiple times.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The xHCI device state.
 * @param   uTRP        Initial TR pointer and DCS.
 * @param   pfnCbk      Callback routine.
 * @param   pvContext   User-defined walk context.
 * @param   pTREP       Pointer to storage for final TR Enqueue Pointer/DCS.
 */
static int xhciR3WalkXferTrbChain(PPDMDEVINS pDevIns, PXHCI pThis, uint64_t uTRP,
                                  PFNTRBWALKCB pfnCbk, void *pvContext, uint64_t *pTREP)
{
    RTGCPHYS        GCPhysXfrTRB;
    uint64_t        uTREP;
    XHCI_XFER_TRB   XferTRB;
    bool            fContinue = true;
    bool            dcs;
    int             rc = VINF_SUCCESS;
    unsigned        cTrbs = 0;

    AssertPtr(pvContext);
    AssertPtr(pTREP);
    Assert(uTRP);

    /* Find the transfer TRB address and the DCS. */
    GCPhysXfrTRB = uTRP & XHCI_TRDP_ADDR_MASK;
    dcs = !!(uTRP & XHCI_TRDP_DCS_MASK); /* MSC upgrades bool to signed something when comparing with a uint8_t:1. */
    LogFlowFunc(("Walking Transfer Ring, TREP:%RGp DCS=%u\n", GCPhysXfrTRB, dcs));

    do {
        /* Fetch the transfer TRB. */
        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysXfrTRB, &XferTRB, sizeof(XferTRB));

        if ((bool)XferTRB.gen.cycle == dcs)
        {
            Log2(("Walking TRB@%RGp, type %u (%s) %u bytes ENT=%u ISP=%u NS=%u CH=%u IOC=%u IDT=%u\n", GCPhysXfrTRB, XferTRB.gen.type,
                  XferTRB.gen.type < RT_ELEMENTS(g_apszTrbNames) ? g_apszTrbNames[XferTRB.gen.type] : "WHAT?!!",
                  XferTRB.gen.xfr_len, XferTRB.gen.ent, XferTRB.gen.isp, XferTRB.gen.ns, XferTRB.gen.ch, XferTRB.gen.ioc, XferTRB.gen.idt));

            /* DCS matches, the TRB is ours to process. */
            switch (XferTRB.gen.type) {
            case XHCI_TRB_LINK:
                Log2(("Link intra-TD: Ptr=%RGp IOC=%u TC=%u CH=%u\n", XferTRB.link.rseg_ptr, XferTRB.link.ioc, XferTRB.link.toggle, XferTRB.link.chain));
                Assert(XferTRB.link.chain);
                /* Do not update the actual TRDP/TREP and DCS yet, just the temporary images. */
                GCPhysXfrTRB = XferTRB.link.rseg_ptr & XHCI_TRDP_ADDR_MASK;
                if (XferTRB.link.toggle)
                    dcs = !dcs;
                Assert(!XferTRB.link.ioc);  /// @todo Needs to be reported.
                break;
            case XHCI_TRB_NORMAL:
            case XHCI_TRB_ISOCH:
            case XHCI_TRB_SETUP_STG:
            case XHCI_TRB_DATA_STG:
            case XHCI_TRB_STATUS_STG:
            case XHCI_TRB_EVT_DATA:
                fContinue = pfnCbk(pDevIns, pThis, &XferTRB, GCPhysXfrTRB, pvContext);
                GCPhysXfrTRB += sizeof(XferTRB);
                break;
            default:
                /* NB: No-op TRBs are not allowed within TDs (4.11.7). */
                Log(("Bad TRB type %u found within TD!!\n", XferTRB.gen.type));
                fContinue = false;
                /// @todo Stop EP etc.?
            }
        }
        else
        {
            /* We don't have a complete TD. Interesting times. */
            Log2(("DCS mismatch, no more TRBs available.\n"));
            fContinue = false;
            rc = VERR_TRY_AGAIN;
        }

        /* Kill the xHC if the TRB list has no end in sight. */
        if (++cTrbs > XHCI_MAX_NUM_TRBS)
        {
            /* Stop the xHC with an error. */
            xhciR3EndlessTrbError(pDevIns, pThis);

            /* Get out of the loop. */
            fContinue = false;
            rc = VERR_NOT_SUPPORTED;    /* No good error code really... */
        }
    } while (fContinue);

    /* Inform caller of the new TR Enqueue Pointer/DCS (not necessarily changed). */
    Assert(!(GCPhysXfrTRB & ~XHCI_TRDP_ADDR_MASK));
    uTREP  = GCPhysXfrTRB | (unsigned)dcs;
    Log2(("Final TRP after walk: %RGp\n", uTREP));
    *pTREP = uTREP;

    return rc;
}


/** Context for probing TD size. */
typedef struct {
    uint32_t    uXferLen;
    uint32_t    cTRB;
    uint32_t    uXfrLenLastED;
    uint32_t    cTRBLastED;
} XHCI_CTX_XFER_PROBE;


/** Context for submitting 'out' TDs. */
typedef struct {
    PVUSBURB    pUrb;
    uint32_t    uXferPos;
    unsigned    cTRB;
} XHCI_CTX_XFER_SUBMIT;


/** Context for completing TDs. */
typedef struct {
    PVUSBURB    pUrb;
    uint32_t    uXferPos;
    uint32_t    uXferLeft;
    unsigned    cTRB;
    uint32_t    uEDTLA  : 24;
    uint32_t    uLastCC :  8;
    uint8_t     uSlotID;
    uint8_t     uEpDCI;
    bool        fMaxCount;
} XHCI_CTX_XFER_COMPLETE;


/** Context for building isochronous URBs. */
typedef struct {
    PVUSBURB        pUrb;
    unsigned        iPkt;
    uint32_t        offCur;
    uint64_t        uInitTREP;
    bool            fSubmitFailed;
} XHCI_CTX_ISOCH;


/**
 * @callback_method_impl{PFNTRBWALKCB,
 *      Probe a TD and figure out how big it is so that a URB can be allocated to back it.}
 */
static DECLCALLBACK(bool)
xhciR3WalkDataTRBsProbe(PPDMDEVINS pDevIns, PXHCI pThis, const XHCI_XFER_TRB *pXferTRB, RTGCPHYS GCPhysXfrTRB, void *pvContext)
{
    RT_NOREF(pDevIns, pThis, GCPhysXfrTRB);
    XHCI_CTX_XFER_PROBE *pCtx = (XHCI_CTX_XFER_PROBE *)pvContext;

    pCtx->cTRB++;

    /* Only consider TRBs which transfer data. */
    switch (pXferTRB->gen.type)
    {
    case XHCI_TRB_NORMAL:
    case XHCI_TRB_ISOCH:
    case XHCI_TRB_SETUP_STG:
    case XHCI_TRB_DATA_STG:
    case XHCI_TRB_STATUS_STG:
        pCtx->uXferLen += pXferTRB->norm.xfr_len;
        if (RT_UNLIKELY(pCtx->uXferLen > XHCI_MAX_TD_SIZE))
        {
            /* NB: We let the TD size get a bit past the max so that we don't lose anything,
             * but the EDTLA will wrap around.
             */
            LogRelMax(10, ("xHCI: TD size (%u) too big, not continuing!\n", pCtx->uXferLen));
            return false;
        }
        break;
    case XHCI_TRB_EVT_DATA:
        /* Remember where the last seen Event Data TRB was. */
        pCtx->cTRBLastED    = pCtx->cTRB;
        pCtx->uXfrLenLastED = pCtx->uXferLen;
        break;
    default:    /* Could be a link TRB, too. */
        break;
    }

    return pXferTRB->gen.ch;
}


/**
 * @callback_method_impl{PFNTRBWALKCB,
 *      Copy data from a TD (TRB chain) into the corresponding TD. OUT direction only.}
 */
static DECLCALLBACK(bool)
xhciR3WalkDataTRBsSubmit(PPDMDEVINS pDevIns, PXHCI pThis, const XHCI_XFER_TRB *pXferTRB, RTGCPHYS GCPhysXfrTRB, void *pvContext)
{
    RT_NOREF(pThis, GCPhysXfrTRB);
    XHCI_CTX_XFER_SUBMIT    *pCtx    = (XHCI_CTX_XFER_SUBMIT *)pvContext;
    uint32_t                uXferLen = pXferTRB->norm.xfr_len;


    /* Only consider TRBs which transfer data. */
    switch (pXferTRB->gen.type)
    {
    case XHCI_TRB_NORMAL:
    case XHCI_TRB_ISOCH:
    case XHCI_TRB_SETUP_STG:
    case XHCI_TRB_DATA_STG:
    case XHCI_TRB_STATUS_STG:
        /* NB: Transfer length may be zero! */
        /// @todo explain/verify abuse of various TRB types here (data stage mapped to normal etc.).
        if (uXferLen)
        {
            /* Sanity check for broken guests (TRBs may have changed since probing). */
            if (pCtx->uXferPos + uXferLen <= pCtx->pUrb->cbData)
            {
                /* Data might be immediate or elsewhere in memory. */
                if (pXferTRB->norm.idt)
                {
                    /* If an immediate data TRB claims there's more than 8 bytes, we have a problem. */
                    if (uXferLen > 8)
                    {
                        LogRelMax(10, ("xHCI: Immediate data TRB length %u bytes, ignoring!\n", uXferLen));
                        return false;   /* Stop walking the chain immediately. */
                    }

                    Assert(uXferLen >= 1 && uXferLen <= 8);
                    Log2(("Copying %u bytes to URB offset %u (immediate data)\n", uXferLen, pCtx->uXferPos));
                    memcpy(pCtx->pUrb->abData + pCtx->uXferPos, pXferTRB, uXferLen);
                }
                else
                {
                    PDMDevHlpPCIPhysReadUser(pDevIns, pXferTRB->norm.data_ptr, pCtx->pUrb->abData + pCtx->uXferPos, uXferLen);
                    Log2(("Copying %u bytes to URB offset %u (from %RGp)\n", uXferLen, pCtx->uXferPos, pXferTRB->norm.data_ptr));
                }
                pCtx->uXferPos += uXferLen;
            }
            else
            {
                LogRelMax(10, ("xHCI: Attempted to submit too much data, ignoring!\n"));
                return false;   /* Stop walking the chain immediately. */
            }

        }
        break;
    default:    /* Could be an event or status stage TRB, too. */
        break;
    }
    pCtx->cTRB++;

    /// @todo Maybe have to make certain that the number of probed TRBs matches? Potentially
    /// by the time TRBs get submitted, there might be more of them available if the TD was
    /// initially not fully written by HCD.

    return pXferTRB->gen.ch;
}


/**
 * Perform URB completion processing.
 *
 * Figure out how much data was really transferred, post events if required, and
 * for IN transfers, copy data from the URB.
 *
 * @callback_method_impl{PFNTRBWALKCB}
 */
static DECLCALLBACK(bool)
xhciR3WalkDataTRBsComplete(PPDMDEVINS pDevIns, PXHCI pThis, const XHCI_XFER_TRB *pXferTRB, RTGCPHYS GCPhysXfrTRB, void *pvContext)
{
    XHCI_CTX_XFER_COMPLETE  *pCtx = (XHCI_CTX_XFER_COMPLETE *)pvContext;
    int                     rc;
    unsigned                uXferLen;
    unsigned                uResidue;
    uint8_t                 cc;
    bool                    fKeepGoing = true;

    switch (pXferTRB->gen.type)
    {
    case XHCI_TRB_NORMAL:
    case XHCI_TRB_ISOCH:
    case XHCI_TRB_SETUP_STG:
    case XHCI_TRB_DATA_STG:   /// @todo document abuse; esp. check BEI bit
    case XHCI_TRB_STATUS_STG:
        /* Assume successful transfer. */
        uXferLen = pXferTRB->norm.xfr_len;
        cc       = XHCI_TCC_SUCCESS;

        /* If there was a short packet, handle it accordingly. */
        if (pCtx->uXferLeft < uXferLen)
        {
            /* The completion code is set regardless of IOC/ISP. It may be
             * reported later via an Event Data TRB (4.10.1.1)
             */
            uXferLen = pCtx->uXferLeft;
            cc       = XHCI_TCC_SHORT_PKT;
        }

        if (pCtx->pUrb->enmDir == VUSBDIRECTION_IN)
        {
            Assert(!pXferTRB->norm.idt);

            /* NB: Transfer length may be zero! */
            if (uXferLen)
            {
                if (uXferLen <= pCtx->uXferLeft)
                {
                    Log2(("Writing %u bytes to %RGp from URB offset %u (TRB@%RGp)\n", uXferLen, pXferTRB->norm.data_ptr, pCtx->uXferPos, GCPhysXfrTRB));
                    PDMDevHlpPCIPhysWriteUser(pDevIns, pXferTRB->norm.data_ptr, pCtx->pUrb->abData + pCtx->uXferPos, uXferLen);
                }
                else
                {
                    LogRelMax(10, ("xHCI: Attempted to read too much data, ignoring!\n"));
                }
            }
        }

        /* Update position within TD. */
        pCtx->uXferLeft -= uXferLen;
        pCtx->uXferPos  += uXferLen;
        Log2(("Current uXferLeft=%u, uXferPos=%u (length was %u)\n", pCtx->uXferLeft, pCtx->uXferPos, uXferLen));

        /* Keep track of the EDTLA and last completion status. */
        pCtx->uEDTLA += uXferLen;   /* May wrap around! */
        pCtx->uLastCC = cc;

        /* Report events as required. */
        uResidue = pXferTRB->norm.xfr_len - uXferLen;
        if (pXferTRB->norm.ioc || (pXferTRB->norm.isp && uResidue))
        {
            rc = xhciR3PostXferEvent(pDevIns, pThis, pXferTRB->norm.int_tgt, uResidue, cc,
                                     pCtx->uSlotID, pCtx->uEpDCI, GCPhysXfrTRB, pXferTRB->norm.bei, false);
        }
        break;
    case XHCI_TRB_EVT_DATA:
        if (pXferTRB->evtd.ioc)
        {
            rc = xhciR3PostXferEvent(pDevIns, pThis, pXferTRB->evtd.int_tgt, pCtx->uEDTLA, pCtx->uLastCC,
                                     pCtx->uSlotID, pCtx->uEpDCI, pXferTRB->evtd.evt_data, pXferTRB->evtd.bei, true);
        }
        /* Clear the EDTLA. */
        pCtx->uEDTLA = 0;
        break;
    default:
        AssertMsgFailed(("%#x\n", pXferTRB->gen.type));
        break;
    }

    pCtx->cTRB--;
    /* For TD fragments, enforce the maximum count, but only as long as the transfer
     * is successful. In case of error we have to complete the entire TD! */
    if (!pCtx->cTRB && pCtx->fMaxCount && pCtx->uLastCC == XHCI_TCC_SUCCESS)
    {
        Log2(("Stopping at the end of TD Fragment.\n"));
        fKeepGoing = false;
    }

    /* NB: We currently do not enforce that the number of TRBs can't change between
     * submission and completion. If we do, we'll have to store it somewhere for
     * isochronous URBs.
     */
    return pXferTRB->gen.ch && fKeepGoing;
}

/**
 * Process (consume) non-data TRBs on a transfer ring. This function
 * completes TRBs which do not have any URB associated with them. Only
 * used with running endpoints. Usable regardless of whether there are
 * in-flight TRBs or not. Returns the next TRB and its address to the
 * caller. May modify the endpoint context!
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The xHCI device state.
 * @param   uSlotID     The slot corresponding to this USB device.
 * @param   uEpDCI      The DCI of this endpoint.
 * @param   pEpCtx      Endpoint context. May be modified.
 * @param   pXfer       Storage for returning the next TRB to caller.
 * @param   pGCPhys     Storage for returning the physical address of TRB.
 */
static int xhciR3ConsumeNonXferTRBs(PPDMDEVINS pDevIns, PXHCI pThis, uint8_t uSlotID, uint8_t uEpDCI,
                                    XHCI_EP_CTX *pEpCtx, XHCI_XFER_TRB *pXfer, RTGCPHYS *pGCPhys)
{
    XHCI_XFER_TRB   xfer;
    RTGCPHYS        GCPhysXfrTRB = 0;
    bool            dcs;
    bool            fInFlight;
    bool            fContinue = true;
    int             rc;
    unsigned        cTrbs = 0;

    LogFlowFunc(("Slot ID: %u, EP DCI %u\n", uSlotID, uEpDCI));
    Assert(uSlotID > 0);
    Assert(uSlotID <= XHCI_NDS);

    Assert(pEpCtx->ep_state == XHCI_EPST_RUNNING);
    do
    {
        /* Find the transfer TRB address. */
        GCPhysXfrTRB = pEpCtx->trdp & XHCI_TRDP_ADDR_MASK;
        dcs = !!(pEpCtx->trdp & XHCI_TRDP_DCS_MASK);

        /* Determine whether there are any in-flight TRBs or not. This affects TREP
         * processing -- when nothing is in flight, we have to move both TREP and TRDP;
         * otherwise only the TRDP must be updated.
         */
        fInFlight = pEpCtx->trep != pEpCtx->trdp;
        LogFlowFunc(("Skipping non-data TRBs, TREP:%RGp, TRDP:%RGp, in-flight: %RTbool\n", pEpCtx->trep, pEpCtx->trdp, fInFlight));

        /* Fetch the transfer TRB. */
        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysXfrTRB, &xfer, sizeof(xfer));

        /* Make sure the Cycle State matches. */
        if ((bool)xfer.gen.cycle == dcs)
        {
            Log2(("TRB @ %RGp, type %u (%s) %u bytes ENT=%u ISP=%u NS=%u CH=%u IOC=%u IDT=%u\n", GCPhysXfrTRB, xfer.gen.type,
                  xfer.gen.type < RT_ELEMENTS(g_apszTrbNames) ? g_apszTrbNames[xfer.gen.type] : "WHAT?!!",
                  xfer.gen.xfr_len, xfer.gen.ent, xfer.gen.isp, xfer.gen.ns, xfer.gen.ch, xfer.gen.ioc, xfer.gen.idt));

            switch (xfer.gen.type) {
            case XHCI_TRB_LINK:
                Log2(("Link extra-TD: Ptr=%RGp IOC=%u TC=%u CH=%u\n", xfer.link.rseg_ptr, xfer.link.ioc, xfer.link.toggle, xfer.link.chain));
                Assert(!xfer.link.chain);
                AssertCompile(XHCI_TRDP_DCS_MASK == 1); /* link.toggle is 0 or 1, can be used as XHCI_TRDP_DCS_MASK */
                /* Set new TRDP but leave DCS bit alone... */
                pEpCtx->trdp = (xfer.link.rseg_ptr & XHCI_TRDP_ADDR_MASK) | (pEpCtx->trdp & XHCI_TRDP_DCS_MASK);
                /* ...and flip the DCS bit if required. Then update the TREP. */
                pEpCtx->trdp = pEpCtx->trdp ^ xfer.link.toggle;
                if (!fInFlight)
                    pEpCtx->trep = pEpCtx->trdp;
                if (xfer.link.ioc)
                    rc = xhciR3PostXferEvent(pDevIns, pThis, xfer.link.int_tgt, 0, XHCI_TCC_SUCCESS, uSlotID, uEpDCI,
                                             GCPhysXfrTRB, false, false);
                break;
            case XHCI_TRB_NOOP_XFER:
                Log2(("No op xfer: IOC=%u CH=%u ENT=%u\n", xfer.nop.ioc, xfer.nop.ch, xfer.nop.ent));
                /* A no-op transfer TRB must not be part of a chain. See 4.11.7. */
                Assert(!xfer.link.chain);
                /* Update enqueue/dequeue pointers. */
                pEpCtx->trdp += sizeof(XHCI_XFER_TRB);
                if (!fInFlight)
                    pEpCtx->trep += sizeof(XHCI_XFER_TRB);
                if (xfer.nop.ioc)
                    rc = xhciR3PostXferEvent(pDevIns, pThis, xfer.nop.int_tgt, 0, XHCI_TCC_SUCCESS, uSlotID, uEpDCI,
                                             GCPhysXfrTRB, false, false);
                break;
            default:
                fContinue = false;
                break;
            }
        }
        else
        {
            LogFunc(("Transfer Ring empty\n"));
            fContinue = false;
        }

        /* Kill the xHC if the TRB list has no end in sight. */
        /* NB: The limit here could perhaps be much lower because a sequence of Link
         * and No-op TRBs with no real work to be done would be highly suspect.
         */
        if (++cTrbs > XHCI_MAX_NUM_TRBS)
        {
            /* Stop the xHC with an error. */
            xhciR3EndlessTrbError(pDevIns, pThis);

            /* Get out of the loop. */
            fContinue = false;
            rc = VERR_NOT_SUPPORTED;    /* No good error code really... */
        }
    } while (fContinue);

    /* The caller will need the next TRB. Hand it over. */
    Assert(GCPhysXfrTRB);
    *pGCPhys = GCPhysXfrTRB;
    *pXfer   = xfer;
    LogFlowFunc(("Final TREP:%RGp, TRDP:%RGp GCPhysXfrTRB:%RGp\n", pEpCtx->trep, pEpCtx->trdp, GCPhysXfrTRB));

    return VINF_SUCCESS;
}

/**
 * Transfer completion callback routine.
 *
 * VUSB will call this when a transfer have been completed
 * in a one or another way.
 *
 * @param   pInterface      Pointer to XHCI::ROOTHUB::IRhPort.
 * @param   pUrb            Pointer to the URB in question.
 */
static DECLCALLBACK(void) xhciR3RhXferCompletion(PVUSBIROOTHUBPORT pInterface, PVUSBURB pUrb)
{
    PXHCIROOTHUBR3  pRh = RT_FROM_MEMBER(pInterface, XHCIROOTHUBR3, IRhPort);
    PXHCICC         pThisCC = pRh->pXhciR3;
    PPDMDEVINS      pDevIns = pThisCC->pDevIns;
    PXHCI           pThis   = PDMDEVINS_2_DATA(pDevIns, PXHCI);
    XHCI_SLOT_CTX   slot_ctx;
    XHCI_EP_CTX     ep_ctx;
    XHCI_XFER_TRB   xfer;
    RTGCPHYS        GCPhysXfrTRB;
    int             rc;
    unsigned        uResidue = 0;
    uint8_t         uSlotID  = pUrb->pHci->uSlotID;
    uint8_t         cc       = XHCI_TCC_SUCCESS;
    uint8_t         uEpDCI;

    /* Check for URBs completed synchronously as part of xHCI command execution.
     * The URB will have zero cTRB as it's not associated with a TD.
     */
    if (!pUrb->pHci->cTRB)
    {
        LogFlow(("%s: xhciR3RhXferCompletion: uSlotID=%u EP=%u cTRB=%d cbData=%u status=%u\n",
                 pUrb->pszDesc, uSlotID, pUrb->EndPt, pUrb->pHci->cTRB, pUrb->cbData, pUrb->enmStatus));
        LogFlow(("%s: xhciR3RhXferCompletion: Completing xHCI-generated request\n", pUrb->pszDesc));
        return;
    }

    /* If the xHC isn't running, just drop the URB right here. */
    if (pThis->status & XHCI_STATUS_HCH)
    {
        LogFlow(("%s: xhciR3RhXferCompletion: uSlotID=%u EP=%u cTRB=%d cbData=%u status=%u\n",
                 pUrb->pszDesc, uSlotID, pUrb->EndPt, pUrb->pHci->cTRB, pUrb->cbData, pUrb->enmStatus));
        LogFlow(("%s: xhciR3RhXferCompletion: xHC halted, skipping URB completion\n", pUrb->pszDesc));
        return;
    }

#ifdef XHCI_ERROR_INJECTION
    if (pThis->fDropUrb)
    {
        LogFlow(("%s: xhciR3RhXferCompletion: Error injection, dropping URB!\n", pUrb->pszDesc));
        pThis->fDropUrb = false;
        return;
    }
#endif

    RTCritSectEnter(&pThisCC->CritSectThrd);

    /* Convert USB endpoint address to xHCI format. */
    if (pUrb->EndPt)
        uEpDCI = pUrb->EndPt * 2 + (pUrb->enmDir == VUSBDIRECTION_IN ? 1 : 0);
    else
        uEpDCI = 1;    /* EP 0 */

    LogFlow(("%s: xhciR3RhXferCompletion: uSlotID=%u EP=%u cTRB=%d\n",
             pUrb->pszDesc, uSlotID, pUrb->EndPt, pUrb->pHci->cTRB));
    LogFlow(("%s: xhciR3RhXferCompletion: EP DCI=%u, cbData=%u status=%u\n", pUrb->pszDesc, uEpDCI, pUrb->cbData, pUrb->enmStatus));

    /* Load the slot/endpoint contexts from guest memory. */
    xhciR3FetchCtxAndEp(pDevIns, pThis, uSlotID, uEpDCI, &slot_ctx, &ep_ctx);

    /* If the EP is disabled, we don't own it so we can't complete the URB.
     * Leave this EP alone and drop the URB.
     */
    if (ep_ctx.ep_state != XHCI_EPST_RUNNING)
    {
        Log(("EP DCI %u not running (state %u), skipping URB completion\n", uEpDCI, ep_ctx.ep_state));
        RTCritSectLeave(&pThisCC->CritSectThrd);
        return;
    }

    /* Now complete any non-transfer TRBs that might be on the transfer ring before
     * the TRB(s) corresponding to this URB. Preloads the TRB as a side effect.
     * Endpoint state now must be written back in case it was modified!
     */
    xhciR3ConsumeNonXferTRBs(pDevIns, pThis, uSlotID, uEpDCI, &ep_ctx, &xfer, &GCPhysXfrTRB);

    /* Deal with failures which halt the EP first. */
    if (RT_UNLIKELY(pUrb->enmStatus != VUSBSTATUS_OK))
    {
        switch(pUrb->enmStatus)
        {
        case VUSBSTATUS_STALL:
            /* Halt the endpoint and inform the HCD.
             * NB: The TRDP is NOT advanced in case of error.
             */
            ep_ctx.ep_state = XHCI_EPST_HALTED;
            cc = XHCI_TCC_STALL;
            rc = xhciR3PostXferEvent(pDevIns, pThis, xfer.gen.int_tgt, uResidue, cc,
                                     uSlotID, uEpDCI, GCPhysXfrTRB, false, false);
            break;
        case VUSBSTATUS_DNR:
            /* Halt the endpoint and inform the HCD.
             * NB: The TRDP is NOT advanced in case of error.
             */
            ep_ctx.ep_state = XHCI_EPST_HALTED;
            cc = XHCI_TCC_USB_XACT_ERR;
            rc = xhciR3PostXferEvent(pDevIns, pThis, xfer.gen.int_tgt, uResidue, cc,
                                     uSlotID, uEpDCI, GCPhysXfrTRB, false, false);
            break;
        case VUSBSTATUS_CRC:    /// @todo Separate status for canceling?!
            ep_ctx.ep_state = XHCI_EPST_HALTED;
            cc = XHCI_TCC_USB_XACT_ERR;
            rc = xhciR3PostXferEvent(pDevIns, pThis, xfer.gen.int_tgt, uResidue, cc,
                                     uSlotID, uEpDCI, GCPhysXfrTRB, false, false);

            /* NB: The TRDP is *not* advanced and TREP is reset. */
            ep_ctx.trep = ep_ctx.trdp;
            break;
        case VUSBSTATUS_DATA_OVERRUN:
        case VUSBSTATUS_DATA_UNDERRUN:
            /* Halt the endpoint and inform the HCD.
             * NB: The TRDP is NOT advanced in case of error.
             */
            ep_ctx.ep_state = XHCI_EPST_HALTED;
            cc = XHCI_TCC_DATA_BUF_ERR;
            rc = xhciR3PostXferEvent(pDevIns, pThis, xfer.gen.int_tgt, uResidue, cc,
                                     uSlotID, uEpDCI, GCPhysXfrTRB, false, false);
            break;
        default:
            AssertMsgFailed(("Unexpected URB status %u\n", pUrb->enmStatus));
        }

        if (pUrb->enmType == VUSBXFERTYPE_ISOC)
            STAM_COUNTER_INC(&pThis->StatErrorIsocUrbs);
    }
    else if (xfer.gen.type == XHCI_TRB_NORMAL)
    {
        XHCI_CTX_XFER_COMPLETE  ctxComplete;
        uint64_t                uTRDP;

        ctxComplete.pUrb      = pUrb;
        ctxComplete.uXferPos  = 0;
        ctxComplete.uXferLeft = pUrb->cbData;
        ctxComplete.cTRB      = pUrb->pHci->cTRB;
        ctxComplete.uSlotID   = uSlotID;
        ctxComplete.uEpDCI    = uEpDCI;
        ctxComplete.uEDTLA    = 0;  // Always zero at the beginning of a new TD.
        ctxComplete.uLastCC   = cc;
        ctxComplete.fMaxCount = ep_ctx.ifc >= XHCI_NO_QUEUING_IN_FLIGHT;
        xhciR3WalkXferTrbChain(pDevIns, pThis, ep_ctx.trdp, xhciR3WalkDataTRBsComplete, &ctxComplete, &uTRDP);
        ep_ctx.last_cc = ctxComplete.uLastCC;
        ep_ctx.trdp    = uTRDP;

        if (ep_ctx.ifc >= XHCI_NO_QUEUING_IN_FLIGHT)
            ep_ctx.ifc -= XHCI_NO_QUEUING_IN_FLIGHT;    /* TD fragment done, allow further queuing. */
        else
            ep_ctx.ifc--;                               /* TD done, decrement in-flight counter. */
    }
    else if (xfer.gen.type == XHCI_TRB_ISOCH)
    {
        XHCI_CTX_XFER_COMPLETE  ctxComplete;
        uint64_t                uTRDP;
        unsigned                iPkt;

        ctxComplete.pUrb      = pUrb;
        ctxComplete.uSlotID   = uSlotID;
        ctxComplete.uEpDCI    = uEpDCI;

        for (iPkt = 0; iPkt < pUrb->cIsocPkts; ++iPkt) {
            ctxComplete.uXferPos  = pUrb->aIsocPkts[iPkt].off;
            ctxComplete.uXferLeft = pUrb->aIsocPkts[iPkt].cb;
            ctxComplete.cTRB      = pUrb->pHci->cTRB;
            ctxComplete.uEDTLA    = 0;  // Zero at TD start.
            ctxComplete.uLastCC   = cc;
            ctxComplete.fMaxCount = false;
            if (pUrb->aIsocPkts[iPkt].enmStatus != VUSBSTATUS_OK)
                STAM_COUNTER_INC(&pThis->StatErrorIsocPkts);
            xhciR3WalkXferTrbChain(pDevIns, pThis, ep_ctx.trdp, xhciR3WalkDataTRBsComplete, &ctxComplete, &uTRDP);
            ep_ctx.last_cc = ctxComplete.uLastCC;
            ep_ctx.trdp    = uTRDP;
            xhciR3ConsumeNonXferTRBs(pDevIns, pThis, uSlotID, uEpDCI, &ep_ctx, &xfer, &GCPhysXfrTRB);
        }
        ep_ctx.ifc--;   /* TD done, decrement in-flight counter. */
    }
    else if (xfer.gen.type == XHCI_TRB_SETUP_STG || xfer.gen.type == XHCI_TRB_DATA_STG || xfer.gen.type == XHCI_TRB_STATUS_STG)
    {
        XHCI_CTX_XFER_COMPLETE  ctxComplete;
        uint64_t                uTRDP;

        ctxComplete.pUrb      = pUrb;
        ctxComplete.uXferPos  = 0;
        ctxComplete.uXferLeft = pUrb->cbData;
        ctxComplete.cTRB      = pUrb->pHci->cTRB;
        ctxComplete.uSlotID   = uSlotID;
        ctxComplete.uEpDCI    = uEpDCI;
        ctxComplete.uEDTLA    = 0;  // Always zero at the beginning of a new TD.
        ctxComplete.uLastCC   = cc;
        ctxComplete.fMaxCount = ep_ctx.ifc >= XHCI_NO_QUEUING_IN_FLIGHT;
        xhciR3WalkXferTrbChain(pDevIns, pThis, ep_ctx.trdp, xhciR3WalkDataTRBsComplete, &ctxComplete, &uTRDP);
        ep_ctx.last_cc = ctxComplete.uLastCC;
        ep_ctx.trdp    = uTRDP;
    }
    else
    {
        AssertMsgFailed(("Unexpected TRB type %u\n", xfer.gen.type));
        Log2(("TRB @ %RGp, type %u unexpected!\n", GCPhysXfrTRB, xfer.gen.type));
        /* Advance the TRDP anyway so that the endpoint isn't completely stuck. */
        ep_ctx.trdp += sizeof(XHCI_XFER_TRB);
    }

    /* Update the endpoint state. */
    xhciR3WriteBackEp(pDevIns, pThis, uSlotID, uEpDCI, &ep_ctx);

    RTCritSectLeave(&pThisCC->CritSectThrd);

    if (pUrb->enmStatus == VUSBSTATUS_OK)
    {
        /* Completion callback usually runs on a separate thread. Let the worker do more. */
        Log2(("Ring bell for slot %u, DCI %u\n", uSlotID, uEpDCI));
        ASMAtomicOrU32(&pThis->aBellsRung[ID_TO_IDX(uSlotID)], 1 << uEpDCI);
        xhciKickWorker(pDevIns, pThis, XHCI_JOB_XFER_DONE, 0);
    }
}


/**
 * Handle transfer errors.
 *
 * VUSB calls this when a transfer attempt failed. This function will respond
 * indicating whether to retry or complete the URB with failure.
 *
 * @returns true if the URB should be retired.
 * @returns false if the URB should be re-tried.
 * @param   pInterface      Pointer to XHCI::ROOTHUB::IRhPort.
 * @param   pUrb            Pointer to the URB in question.
 */
static DECLCALLBACK(bool) xhciR3RhXferError(PVUSBIROOTHUBPORT pInterface, PVUSBURB pUrb)
{
    PXHCIROOTHUBR3  pRh = RT_FROM_MEMBER(pInterface, XHCIROOTHUBR3, IRhPort);
    PXHCICC         pThisCC = pRh->pXhciR3;
    PXHCI           pThis = PDMDEVINS_2_DATA(pThisCC->pDevIns, PXHCI);
    bool            fRetire = true;

    /* If the xHC isn't running, get out of here immediately. */
    if (pThis->status & XHCI_STATUS_HCH)
    {
        Log(("xHC halted, skipping URB error handling\n"));
        return fRetire;
    }

    RTCritSectEnter(&pThisCC->CritSectThrd);

    Assert(pUrb->pHci->cTRB); /* xHCI-generated URBs should not fail! */
    if (!pUrb->pHci->cTRB)
    {
        Log(("%s: Failing xHCI-generated request!\n", pUrb->pszDesc));
    }
    else if (pUrb->enmStatus == VUSBSTATUS_STALL)
    {
        /* Don't retry on stall. */
        Log2(("%s: xhciR3RhXferError: STALL, giving up.\n", pUrb->pszDesc));
    } else if (pUrb->enmStatus == VUSBSTATUS_CRC) {
        /* Don't retry on CRC errors either. These indicate canceled URBs, among others. */
        Log2(("%s: xhciR3RhXferError: CRC, giving up.\n", pUrb->pszDesc));
    } else if (pUrb->enmStatus == VUSBSTATUS_DNR) {
        /* Don't retry on DNR errors. These indicate the device vanished. */
        Log2(("%s: xhciR3RhXferError: DNR, giving up.\n", pUrb->pszDesc));
    } else if (pUrb->enmStatus == VUSBSTATUS_DATA_OVERRUN) {
        /* Don't retry on OVERRUN errors. These indicate a fatal error. */
        Log2(("%s: xhciR3RhXferError: OVERRUN, giving up.\n", pUrb->pszDesc));
    } else if (pUrb->enmStatus == VUSBSTATUS_DATA_UNDERRUN) {
        /* Don't retry on UNDERRUN errors. These indicate a fatal error. */
        Log2(("%s: xhciR3RhXferError: UNDERRUN, giving up.\n", pUrb->pszDesc));
    } else {
        /// @todo
        AssertMsgFailed(("%#x\n", pUrb->enmStatus));
    }

    RTCritSectLeave(&pThisCC->CritSectThrd);
    return fRetire;
}


/**
 * Queue a TD composed of normal TRBs, event data TRBs, and suchlike.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The xHCI device state, shared edition.
 * @param   pThisCC     The xHCI device state, ring-3 edition.
 * @param   pRh         Root hub for the device.
 * @param   GCPhysTRB   Physical gues address of the TRB.
 * @param   pTrb        Pointer to the contents of the first TRB.
 * @param   pEpCtx      Pointer to the cached EP context.
 * @param   uSlotID     ID of the associated slot context.
 * @param   uAddr       The device address.
 * @param   uEpDCI      The DCI(!) of the endpoint.
 */
static int xhciR3QueueDataTD(PPDMDEVINS pDevIns, PXHCI pThis, PXHCICC pThisCC, PXHCIROOTHUBR3 pRh, RTGCPHYS GCPhysTRB,
                             XHCI_XFER_TRB *pTrb, XHCI_EP_CTX *pEpCtx, uint8_t uSlotID, uint8_t uAddr, uint8_t uEpDCI)
{
    RT_NOREF(GCPhysTRB);
    XHCI_CTX_XFER_PROBE     ctxProbe;
    XHCI_CTX_XFER_SUBMIT    ctxSubmit;
    uint64_t                uTREP;
    bool                    fFragOnly = false;
    int                     rc;
    VUSBXFERTYPE            enmType;
    VUSBDIRECTION           enmDir;

    /* Discover how big this TD is. */
    RT_ZERO(ctxProbe);
    rc = xhciR3WalkXferTrbChain(pDevIns, pThis, pEpCtx->trep, xhciR3WalkDataTRBsProbe, &ctxProbe, &uTREP);
    if (RT_SUCCESS(rc))
        LogFlowFunc(("Probed %u TRBs, %u bytes total, TREP@%RX64\n", ctxProbe.cTRB, ctxProbe.uXferLen, uTREP));
    else
    {
        LogFlowFunc(("Probing failed after %u TRBs, %u bytes total (last ED after %u TRBs and %u bytes), TREP@%RX64\n", ctxProbe.cTRB, ctxProbe.uXferLen, ctxProbe.cTRBLastED, ctxProbe.uXfrLenLastED, uTREP));
        if (rc == VERR_TRY_AGAIN && pTrb->gen.type == XHCI_TRB_NORMAL && ctxProbe.cTRBLastED)
        {
            /* The TD is incomplete, but we have at least one TD fragment. We can create a URB for
             * what we have but we can't safely queue any more because if any error occurs, the
             * TD needs to fail as a whole.
             * OS X Mavericks and Yosemite tend to trigger this case when reading from USB 3.0
             * MSDs (transfers up to 1MB).
             */
            fFragOnly = true;

            /* Because we currently do not maintain the EDTLA across URBs, we have to only submit
             * TD fragments up to where we last saw an Event Data TRB. If there was no Event Data
             * TRB, we'll just try waiting a bit longer for the TD to be complete or an Event Data
             * TRB to show up. The guest is extremely likely to do one or the other, since otherwise
             * it has no way to tell when the transfer completed.
             */
            ctxProbe.cTRB     = ctxProbe.cTRBLastED;
            ctxProbe.uXferLen = ctxProbe.uXfrLenLastED;
        }
        else
            return rc;
    }

    /* Determine the transfer kind based on endpoint type. */
    switch (pEpCtx->ep_type)
    {
    case XHCI_EPTYPE_BULK_IN:
    case XHCI_EPTYPE_BULK_OUT:
        enmType = VUSBXFERTYPE_BULK;
        break;
    case XHCI_EPTYPE_INTR_IN:
    case XHCI_EPTYPE_INTR_OUT:
        enmType = VUSBXFERTYPE_INTR;
        break;
    case XHCI_EPTYPE_CONTROL:
        enmType = VUSBXFERTYPE_CTRL;
        break;
    case XHCI_EPTYPE_ISOCH_IN:
    case XHCI_EPTYPE_ISOCH_OUT:
    default:
        enmType = VUSBXFERTYPE_INVALID;
        AssertMsgFailed(("%#x\n", pEpCtx->ep_type));
    }

    /* Determine the direction based on endpoint type. */
    switch (pEpCtx->ep_type)
    {
    case XHCI_EPTYPE_BULK_IN:
    case XHCI_EPTYPE_INTR_IN:
        enmDir = VUSBDIRECTION_IN;
        break;
    case XHCI_EPTYPE_BULK_OUT:
    case XHCI_EPTYPE_INTR_OUT:
        enmDir = VUSBDIRECTION_OUT;
        break;
    default:
        enmDir = VUSBDIRECTION_INVALID;
        AssertMsgFailed(("%#x\n", pEpCtx->ep_type));
    }

    /* Allocate and initialize a URB. */
    PVUSBURB pUrb = VUSBIRhNewUrb(pRh->pIRhConn, uAddr, VUSB_DEVICE_PORT_INVALID, enmType, enmDir, ctxProbe.uXferLen, ctxProbe.cTRB, NULL);
    if (!pUrb)
        return VERR_OUT_OF_RESOURCES;   /// @todo handle error!

    STAM_COUNTER_ADD(&pThis->StatTRBsPerDtaUrb, ctxProbe.cTRB);

    /* See 4.5.1 about xHCI vs. USB endpoint addressing. */
    Assert(uEpDCI);

    pUrb->EndPt          = uEpDCI / 2;  /* DCI = EP * 2 + direction */
    pUrb->fShortNotOk    = false;       /* We detect short packets ourselves. */
    pUrb->enmStatus      = VUSBSTATUS_OK;

    /// @todo Cross check that the EP type corresponds to direction. Probably
    //should check when configuring device?
    pUrb->pHci->uSlotID  = uSlotID;

    /* For OUT transfers, copy the TD data into the URB. */
    if (pUrb->enmDir == VUSBDIRECTION_OUT)
    {
        ctxSubmit.pUrb     = pUrb;
        ctxSubmit.uXferPos = 0;
        ctxSubmit.cTRB     = 0;
        xhciR3WalkXferTrbChain(pDevIns, pThis, pEpCtx->trep, xhciR3WalkDataTRBsSubmit, &ctxSubmit, &uTREP);
        Assert(ctxProbe.cTRB == ctxSubmit.cTRB);
        ctxProbe.cTRB = ctxSubmit.cTRB;
    }

    /* If only completing a fragment, remember the TRB count and increase
     * the in-flight count past the limit so we won't queue any more.
     */
    pUrb->pHci->cTRB = ctxProbe.cTRB;
    if (fFragOnly)
        /* Bit of a hack -- prevent further queuing. */
        pEpCtx->ifc += XHCI_NO_QUEUING_IN_FLIGHT;
    else
        /* Increment the in-flight counter before queuing more. */
        pEpCtx->ifc++;

    /* Commit the updated TREP; submitting the URB may already invoke completion callbacks. */
    pEpCtx->trep = uTREP;
    xhciR3WriteBackEp(pDevIns, pThis, uSlotID, uEpDCI, pEpCtx);

    /*
     * Submit the URB.
     */
    STAM_COUNTER_ADD(&pThis->StatUrbSizeData, pUrb->cbData);
    Log(("%s: xhciR3QueueDataTD: Addr=%u, EndPt=%u, enmDir=%u cbData=%u\n",
         pUrb->pszDesc, pUrb->DstAddress, pUrb->EndPt, pUrb->enmDir, pUrb->cbData));
    RTCritSectLeave(&pThisCC->CritSectThrd);
    rc = VUSBIRhSubmitUrb(pRh->pIRhConn, pUrb, &pRh->Led);
    RTCritSectEnter(&pThisCC->CritSectThrd);
    if (RT_SUCCESS(rc))
        return VINF_SUCCESS;

    /* Failure cleanup. Can happen if we're still resetting the device or out of resources,
     * or the user just ripped out the device.
     */
    /// @todo Mark the EP as halted and inactive and write back the changes.

    return VERR_OUT_OF_RESOURCES;
}


/**
 * Queue an isochronous TD composed of isochronous and normal TRBs, event
 * data TRBs, and suchlike. This TD may either correspond to a single URB or
 * form one packet of an isochronous URB.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The xHCI device state, shared edition.
 * @param   pThisCC     The xHCI device state, ring-3 edition.
 * @param   pRh         Root hub for the device.
 * @param   GCPhysTRB   Physical guest address of the TRB.
 * @param   pTrb        Pointer to the contents of the first TRB.
 * @param   pEpCtx      Pointer to the cached EP context.
 * @param   uSlotID     ID of the associated slot context.
 * @param   uAddr       The device address.
 * @param   uEpDCI      The DCI(!) of the endpoint.
 * @param   pCtxIso     Additional isochronous URB context.
 */
static int xhciR3QueueIsochTD(PPDMDEVINS pDevIns, PXHCI pThis, PXHCICC pThisCC, PXHCIROOTHUBR3 pRh, RTGCPHYS GCPhysTRB,
                              XHCI_XFER_TRB *pTrb, XHCI_EP_CTX *pEpCtx, uint8_t uSlotID, uint8_t uAddr, uint8_t uEpDCI,
                              XHCI_CTX_ISOCH *pCtxIso)
{
    RT_NOREF(GCPhysTRB, pTrb);
    XHCI_CTX_XFER_PROBE     ctxProbe;
    XHCI_CTX_XFER_SUBMIT    ctxSubmit;
    uint64_t                uTREP;
    PVUSBURB                pUrb;
    unsigned                cIsoPackets;
    uint32_t                cbPktMax;

    /* Discover how big this TD is. */
    RT_ZERO(ctxProbe);
    xhciR3WalkXferTrbChain(pDevIns, pThis, pEpCtx->trep, xhciR3WalkDataTRBsProbe, &ctxProbe, &uTREP);
    LogFlowFunc(("Probed %u TRBs, %u bytes total, TREP@%RX64\n", ctxProbe.cTRB, ctxProbe.uXferLen, uTREP));

    /* See 4.5.1 about xHCI vs. USB endpoint addressing. */
    Assert(uEpDCI);

    /* For isochronous transfers, there's a bit of extra work to do. The interval
     * is key and determines whether the TD will directly correspond to a URB or
     * if it will only form part of a larger URB. In any case, one TD equals one
     * 'packet' of an isochronous URB.
     */
    switch (pEpCtx->interval)
    {
    case 0: /* Every 2^0 * 125us, i.e. 8 per frame. */
        cIsoPackets = 8;
        break;
    case 1: /* Every 2^1 * 125us, i.e. 4 per frame. */
        cIsoPackets = 4;
        break;
    case 2: /* Every 2^2 * 125us, i.e. 2 per frame. */
        cIsoPackets = 2;
        break;
    case 3: /* Every 2^3 * 125us, i.e. 1 per frame. */
    default:/* Or any larger interval (every n frames).*/
        cIsoPackets = 1;
        break;
    }

    /* We do not know exactly how much data might be transferred until we
     * look at all TDs/packets that constitute the URB. However, we do know
     * the maximum possible size even without probing any TDs at all.
     * The actual size is expected to be the same or at most slightly smaller,
     * hence it makes sense to allocate the URB right away and copy data into
     * it as we go, rather than doing complicated probing first.
     * The Max Endpoint Service Interval Time (ESIT) Payload defines the
     * maximum number of bytes that can be transferred per interval (4.14.2).
     * Unfortunately Apple was lazy and their driver leaves the Max ESIT
     * Payload as zero, so we have to do the math ourselves.
     */

    /* Calculate the maximum transfer size per (micro)frame. */
    /// @todo This ought to be stored within the URB somewhere.
    cbPktMax = pEpCtx->max_pkt_sz * (pEpCtx->max_brs_sz + 1) * (pEpCtx->mult + 1);
    if (!pCtxIso->pUrb)
    {
        uint32_t    cbUrbMax = cIsoPackets * cbPktMax;

        /* Validate endpoint type. */
        AssertMsg(pEpCtx->ep_type == XHCI_EPTYPE_ISOCH_IN || pEpCtx->ep_type == XHCI_EPTYPE_ISOCH_OUT,
                  ("%#x\n", pEpCtx->ep_type));

        /* Allocate and initialize a new URB. */
        pUrb = VUSBIRhNewUrb(pRh->pIRhConn, uAddr, VUSB_DEVICE_PORT_INVALID, VUSBXFERTYPE_ISOC,
                             (pEpCtx->ep_type == XHCI_EPTYPE_ISOCH_IN) ? VUSBDIRECTION_IN : VUSBDIRECTION_OUT,
                             cbUrbMax, ctxProbe.cTRB, NULL);
        if (!pUrb)
            return VERR_OUT_OF_RESOURCES;   /// @todo handle error!

        STAM_COUNTER_ADD(&pThis->StatTRBsPerIsoUrb, ctxProbe.cTRB);

        LogFlowFunc(("Allocated URB with %u packets, %u bytes total (ESIT payload %u)\n", cIsoPackets, cbUrbMax, cbPktMax));

        pUrb->EndPt          = uEpDCI / 2;  /* DCI = EP * 2 + direction */
        pUrb->fShortNotOk    = false;       /* We detect short packets ourselves. */
        pUrb->enmStatus      = VUSBSTATUS_OK;
        pUrb->cIsocPkts      = cIsoPackets;
        pUrb->pHci->uSlotID  = uSlotID;
        pUrb->pHci->cTRB     = ctxProbe.cTRB;

        /* If TRB says so or if there are multiple packets per interval, don't even
         * bother with frame counting and schedule everything ASAP.
         */
        if (pTrb->isoc.sia || cIsoPackets != 1)
            pUrb->uStartFrameDelta = 0;
        else
        {
            uint16_t        uFrameDelta;
            uint32_t        uPort;

            /* Abort the endpoint, i.e. cancel any outstanding URBs. This needs to be done after
             * writing back the EP state so that the completion callback can operate.
             */
            if (RT_SUCCESS(xhciR3FindRhDevBySlot(pDevIns, pThis, pThisCC, uSlotID, NULL, &uPort)))
            {

                uFrameDelta = pRh->pIRhConn->pfnUpdateIsocFrameDelta(pRh->pIRhConn, uPort, uEpDCI / 2,
                                                                     uEpDCI & 1 ? VUSBDIRECTION_IN : VUSBDIRECTION_OUT,
                                                                     pTrb->isoc.frm_id, XHCI_FRAME_ID_BITS);
                pUrb->uStartFrameDelta = uFrameDelta;
                Log(("%s: Isoch frame delta set to %u\n", pUrb->pszDesc, uFrameDelta));
            }
            else
            {
                Log(("%s: Failed to find device for slot! Setting frame delta to zero.\n", pUrb->pszDesc));
                pUrb->uStartFrameDelta = 0;
            }
        }

        Log(("%s: Addr=%u, EndPt=%u, enmDir=%u cIsocPkts=%u cbData=%u FrmID=%u Isoch URB created\n",
             pUrb->pszDesc, pUrb->DstAddress, pUrb->EndPt, pUrb->enmDir, pUrb->cIsocPkts, pUrb->cbData, pTrb->isoc.frm_id));

        /* Set up the context for later use. */
        pCtxIso->pUrb      = pUrb;
        /* Save the current TREP in case we need to rewind. */
        pCtxIso->uInitTREP = pEpCtx->trep;
    }
    else
    {
        Assert(cIsoPackets > 1);
        /* Grab the URB we initialized earlier. */
        pUrb = pCtxIso->pUrb;
    }

    /* Set up the packet corresponding to this TD. */
    pUrb->aIsocPkts[pCtxIso->iPkt].cb        = RT_MIN(ctxProbe.uXferLen, cbPktMax);
    pUrb->aIsocPkts[pCtxIso->iPkt].off       = pCtxIso->offCur;
    pUrb->aIsocPkts[pCtxIso->iPkt].enmStatus = VUSBSTATUS_NOT_ACCESSED;

    /* For OUT transfers, copy the TD data into the URB. */
    if (pUrb->enmDir == VUSBDIRECTION_OUT)
    {
        ctxSubmit.pUrb     = pUrb;
        ctxSubmit.uXferPos = pCtxIso->offCur;
        ctxSubmit.cTRB     = 0;
        xhciR3WalkXferTrbChain(pDevIns, pThis, pEpCtx->trep, xhciR3WalkDataTRBsSubmit, &ctxSubmit, &uTREP);
        Assert(ctxProbe.cTRB == ctxSubmit.cTRB);
    }

    /* Done preparing this packet. */
    Assert(pCtxIso->iPkt < 8);
    pCtxIso->iPkt++;
    pCtxIso->offCur += ctxProbe.uXferLen;
    Assert(pCtxIso->offCur <= pUrb->cbData);

    /* Increment the in-flight counter before queuing more. */
    if (pCtxIso->iPkt == pUrb->cIsocPkts)
        pEpCtx->ifc++;

    /* Commit the updated TREP; submitting the URB may already invoke completion callbacks. */
    pEpCtx->trep = uTREP;
    xhciR3WriteBackEp(pDevIns, pThis, uSlotID, uEpDCI, pEpCtx);

    /* If the URB is complete, submit it. */
    if (pCtxIso->iPkt == pUrb->cIsocPkts)
    {
        /* Change cbData to reflect how much data should be transferred. This can differ
         * from how much data was allocated for the URB.
         */
        pUrb->cbData = pCtxIso->offCur;
        STAM_COUNTER_ADD(&pThis->StatUrbSizeIsoc, pUrb->cbData);
        Log(("%s: Addr=%u, EndPt=%u, enmDir=%u cIsocPkts=%u cbData=%u Isoch URB being submitted\n",
             pUrb->pszDesc, pUrb->DstAddress, pUrb->EndPt, pUrb->enmDir, pUrb->cIsocPkts, pUrb->cbData));
        RTCritSectLeave(&pThisCC->CritSectThrd);
        int rc = VUSBIRhSubmitUrb(pRh->pIRhConn, pUrb, &pRh->Led);
        RTCritSectEnter(&pThisCC->CritSectThrd);
        if (RT_FAILURE(rc))
        {
            /* Failure cleanup. Can happen if we're still resetting the device or out of resources,
             * or the user just ripped out the device.
             */
            pCtxIso->fSubmitFailed = true;
            /// @todo Mark the EP as halted and inactive and write back the changes.
            return VERR_OUT_OF_RESOURCES;
        }
        /* Clear the isochronous URB context. */
        RT_ZERO(*pCtxIso);
    }

    return VINF_SUCCESS;
}


/**
 * Queue a control TD composed of setup/data/status stage TRBs, event data
 * TRBs, and suchlike.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The xHCI device state, shared edition.
 * @param   pThisCC     The xHCI device state, ring-3 edition.
 * @param   pRh         Root hub for the device.
 * @param   GCPhysTRB   Physical guest address of th TRB.
 * @param   pTrb        Pointer to the contents of the first TRB.
 * @param   pEpCtx      Pointer to the cached EP context.
 * @param   uSlotID     ID of the associated slot context.
 * @param   uAddr       The device address.
 * @param   uEpDCI      The DCI(!) of the endpoint.
 */
static int xhciR3QueueControlTD(PPDMDEVINS pDevIns, PXHCI pThis, PXHCICC pThisCC, PXHCIROOTHUBR3 pRh, RTGCPHYS GCPhysTRB,
                                XHCI_XFER_TRB *pTrb, XHCI_EP_CTX *pEpCtx, uint8_t uSlotID, uint8_t uAddr, uint8_t uEpDCI)
{
    RT_NOREF(GCPhysTRB);
    XHCI_CTX_XFER_PROBE     ctxProbe;
    XHCI_CTX_XFER_SUBMIT    ctxSubmit;
    uint64_t                uTREP;
    int                     rc;
    VUSBDIRECTION           enmDir;

    /* Discover how big this TD is. */
    RT_ZERO(ctxProbe);
    rc = xhciR3WalkXferTrbChain(pDevIns, pThis, pEpCtx->trep, xhciR3WalkDataTRBsProbe, &ctxProbe, &uTREP);
    if (RT_SUCCESS(rc))
        LogFlowFunc(("Probed %u TRBs, %u bytes total, TREP@%RX64\n", ctxProbe.cTRB, ctxProbe.uXferLen, uTREP));
    else
    {
        LogFlowFunc(("Probing failed after %u TRBs, %u bytes total (last ED after %u TRBs and %u bytes), TREP@%RX64\n", ctxProbe.cTRB, ctxProbe.uXferLen, ctxProbe.cTRBLastED, ctxProbe.uXfrLenLastED, uTREP));
        return rc;
    }

    /* Determine the transfer direction. */
    switch (pTrb->gen.type)
    {
    case XHCI_TRB_SETUP_STG:
        enmDir = VUSBDIRECTION_SETUP;
        /* For setup TRBs, there is always 8 bytes of immediate data. */
        Assert(sizeof(VUSBSETUP) == 8);
        Assert(ctxProbe.uXferLen == 8);
        Log2(("bmRequestType:%02X bRequest:%02X wValue:%04X wIndex:%04X wLength:%04X\n", pTrb->setup.bmRequestType,
              pTrb->setup.bRequest, pTrb->setup.wValue, pTrb->setup.wIndex, pTrb->setup.wLength));
        break;
    case XHCI_TRB_STATUS_STG:
        enmDir = pTrb->status.dir ? VUSBDIRECTION_IN : VUSBDIRECTION_OUT;
        break;
    case XHCI_TRB_DATA_STG:
        enmDir = pTrb->data.dir ? VUSBDIRECTION_IN : VUSBDIRECTION_OUT;
        break;
    default:
        AssertMsgFailed(("%#x\n", pTrb->gen.type)); /* Can't happen unless caller messed up. */
        return VERR_INTERNAL_ERROR;
    }

    /* Allocate and initialize a URB. */
    PVUSBURB pUrb = VUSBIRhNewUrb(pRh->pIRhConn, uAddr, VUSB_DEVICE_PORT_INVALID, VUSBXFERTYPE_CTRL, enmDir, ctxProbe.uXferLen, ctxProbe.cTRB,
                                  NULL);
    if (!pUrb)
        return VERR_OUT_OF_RESOURCES;   /// @todo handle error!

    STAM_COUNTER_ADD(&pThis->StatTRBsPerCtlUrb, ctxProbe.cTRB);

    /* See 4.5.1 about xHCI vs. USB endpoint addressing. */
    Assert(uEpDCI);

    /* This had better be a control endpoint. */
    AssertMsg(pEpCtx->ep_type == XHCI_EPTYPE_CONTROL, ("%#x\n", pEpCtx->ep_type));

    pUrb->EndPt          = uEpDCI / 2;  /* DCI = EP * 2 + direction */
    pUrb->fShortNotOk    = false;       /* We detect short packets ourselves. */
    pUrb->enmStatus      = VUSBSTATUS_OK;
    pUrb->pHci->uSlotID  = uSlotID;

    /* For OUT/SETUP transfers, copy the TD data into the URB. */
    if (pUrb->enmDir == VUSBDIRECTION_OUT || pUrb->enmDir == VUSBDIRECTION_SETUP)
    {
        ctxSubmit.pUrb     = pUrb;
        ctxSubmit.uXferPos = 0;
        ctxSubmit.cTRB     = 0;
        xhciR3WalkXferTrbChain(pDevIns, pThis, pEpCtx->trep, xhciR3WalkDataTRBsSubmit, &ctxSubmit, &uTREP);
        Assert(ctxProbe.cTRB == ctxSubmit.cTRB);
        ctxProbe.cTRB = ctxSubmit.cTRB;
    }

    pUrb->pHci->cTRB = ctxProbe.cTRB;

    /* Commit the updated TREP; submitting the URB may already invoke completion callbacks. */
    pEpCtx->trep = uTREP;
    xhciR3WriteBackEp(pDevIns, pThis, uSlotID, uEpDCI, pEpCtx);

    /*
     * Submit the URB.
     */
    STAM_COUNTER_ADD(&pThis->StatUrbSizeCtrl, pUrb->cbData);
    Log(("%s: xhciR3QueueControlTD: Addr=%u, EndPt=%u, enmDir=%u cbData=%u\n",
         pUrb->pszDesc, pUrb->DstAddress, pUrb->EndPt, pUrb->enmDir, pUrb->cbData));
    RTCritSectLeave(&pThisCC->CritSectThrd);
    rc = VUSBIRhSubmitUrb(pRh->pIRhConn, pUrb, &pRh->Led);
    RTCritSectEnter(&pThisCC->CritSectThrd);
    if (RT_SUCCESS(rc))
        return VINF_SUCCESS;

    /* Failure cleanup. Can happen if we're still resetting the device or out of resources,
     * or the user just ripped out the device.
     */
    /// @todo Mark the EP as halted and inactive and write back the changes.

    return VERR_OUT_OF_RESOURCES;
}


/**
 * Process a device context (transfer data).
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The xHCI device state, shared edition.
 * @param   pThisCC     The xHCI device state, ring-3 edition.
 * @param   uSlotID     Slot/doorbell which had been rung.
 * @param   uDBVal      Value written to the doorbell.
 */
static int xhciR3ProcessDevCtx(PPDMDEVINS pDevIns, PXHCI pThis, PXHCICC pThisCC, uint8_t uSlotID, uint32_t uDBVal)
{
    uint8_t         uDBTarget = uDBVal & XHCI_DB_TGT_MASK;
    XHCI_CTX_ISOCH  ctxIsoch = {0};
    XHCI_SLOT_CTX   slot_ctx;
    XHCI_EP_CTX     ep_ctx;
    XHCI_XFER_TRB   xfer;
    RTGCPHYS        GCPhysXfrTRB;
    PXHCIROOTHUBR3  pRh;
    bool            dcs;
    bool            fContinue = true;
    int             rc;
    unsigned        cTrbs = 0;

    LogFlowFunc(("Slot ID: %u, DB target %u, DB stream ID %u\n", uSlotID, uDBTarget, (uDBVal & XHCI_DB_STRMID_MASK) >> XHCI_DB_STRMID_SHIFT));
    Assert(uSlotID > 0);
    Assert(uSlotID <= XHCI_NDS);
    /// @todo report errors for bogus DB targets
    Assert(uDBTarget > 0);
    Assert(uDBTarget < 32);

    /// @todo Check for aborts and the like?

    /* Load the slot and endpoint contexts. */
    xhciR3FetchCtxAndEp(pDevIns, pThis, uSlotID, uDBTarget, &slot_ctx, &ep_ctx);
    /// @todo sanity check the context in here?

    /* Select the root hub corresponding to the port. */
    pRh = GET_PORT_PRH(pThisCC, ID_TO_IDX(slot_ctx.rh_port));

    /* Stopped endpoints automatically transition to running state. */
    if (RT_UNLIKELY(ep_ctx.ep_state == XHCI_EPST_STOPPED))
    {
        Log(("EP DCI %u stopped -> running\n", uDBTarget));
        ep_ctx.ep_state = XHCI_EPST_RUNNING;
        /* Update EP right here. Theoretically could be postponed, but we
         * must ensure that the EP does get written back even if there is
         * no other work to do.
         */
        xhciR3WriteBackEp(pDevIns, pThis, uSlotID, uDBTarget, &ep_ctx);
    }

    /* If the EP isn't running, get outta here. */
    if (RT_UNLIKELY(ep_ctx.ep_state != XHCI_EPST_RUNNING))
    {
        Log2(("EP DCI %u not running (state %u), bail!\n", uDBTarget, ep_ctx.ep_state));
        return VINF_SUCCESS;
    }

    /* Get any non-transfer TRBs out of the way. */
    xhciR3ConsumeNonXferTRBs(pDevIns, pThis, uSlotID, uDBTarget, &ep_ctx, &xfer, &GCPhysXfrTRB);
    /// @todo This is inefficient.
    xhciR3WriteBackEp(pDevIns, pThis, uSlotID, uDBTarget, &ep_ctx);

    do
    {
        /* Fetch the contexts again and find the TRB address at enqueue point. */
        xhciR3FetchCtxAndEp(pDevIns, pThis, uSlotID, uDBTarget, &slot_ctx, &ep_ctx);
        GCPhysXfrTRB = ep_ctx.trep & XHCI_TRDP_ADDR_MASK;
        dcs = !!(ep_ctx.trep & XHCI_TRDP_DCS_MASK);
        LogFlowFunc(("Processing Transfer Ring, TREP: %RGp\n", GCPhysXfrTRB));

        /* Fetch the transfer TRB. */
        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysXfrTRB, &xfer, sizeof(xfer));

        /* Make sure the Cycle State matches. */
        if ((bool)xfer.gen.cycle == dcs)
        {
            Log2(("TRB @ %RGp, type %u (%s) %u bytes ENT=%u ISP=%u NS=%u CH=%u IOC=%u IDT=%u\n", GCPhysXfrTRB, xfer.gen.type,
                  xfer.gen.type < RT_ELEMENTS(g_apszTrbNames) ? g_apszTrbNames[xfer.gen.type] : "WHAT?!!",
                  xfer.gen.xfr_len, xfer.gen.ent, xfer.gen.isp, xfer.gen.ns, xfer.gen.ch, xfer.gen.ioc, xfer.gen.idt));

            /* If there is an "in-flight" TRDP, check if we need to wait until the transfer completes. */
            if ((ep_ctx.trdp & XHCI_TRDP_ADDR_MASK) != GCPhysXfrTRB)
            {
                switch (xfer.gen.type) {
                case XHCI_TRB_ISOCH:
                    if (ep_ctx.ifc >= XHCI_MAX_ISOC_IN_FLIGHT)
                    {
                        Log(("%u isoch URBs in flight, backing off\n", ep_ctx.ifc));
                        fContinue = false;
                        break;
                    }
                    RT_FALL_THRU();
                case XHCI_TRB_LINK:
                    Log2(("TRB OK, continuing @ %RX64\n", GCPhysXfrTRB));
                    break;
                case XHCI_TRB_NORMAL:
                    if (XHCI_EP_XTYPE(ep_ctx.ep_type) != XHCI_XFTYPE_BULK)
                    {
                        Log2(("Normal TRB not bulk, not continuing @ %RX64\n", GCPhysXfrTRB));
                        fContinue = false;
                        break;
                    }
                    if (ep_ctx.ifc >= XHCI_MAX_BULK_IN_FLIGHT)
                    {
                        Log(("%u normal URBs in flight, backing off\n", ep_ctx.ifc));
                        fContinue = false;
                        break;
                    }
                    Log2(("Bulk TRB OK, continuing @ %RX64\n", GCPhysXfrTRB));
                    break;
                case XHCI_TRB_EVT_DATA:
                case XHCI_TRB_NOOP_XFER:
                    Log2(("TRB not OK, not continuing @ %RX64\n", GCPhysXfrTRB));
                    fContinue = false;
                    break;
                default:
                    Log2(("Some other TRB (type %u), not continuing @ %RX64\n", xfer.gen.type, GCPhysXfrTRB));
                    fContinue = false;
                    break;
                }
            }
            if (!fContinue)
                break;

            switch (xfer.gen.type) {
            case XHCI_TRB_NORMAL:
                Log(("Normal TRB: Ptr=%RGp IOC=%u CH=%u\n", xfer.norm.data_ptr, xfer.norm.ioc, xfer.norm.ch));
                rc = xhciR3QueueDataTD(pDevIns, pThis, pThisCC, pRh, GCPhysXfrTRB, &xfer, &ep_ctx, uSlotID,
                                       slot_ctx.dev_addr, uDBTarget);
                break;
            case XHCI_TRB_SETUP_STG:
                Log(("Setup stage TRB: IOC=%u IDT=%u\n", xfer.setup.ioc, xfer.setup.idt));
                rc = xhciR3QueueControlTD(pDevIns, pThis, pThisCC, pRh, GCPhysXfrTRB, &xfer, &ep_ctx, uSlotID,
                                          slot_ctx.dev_addr, uDBTarget);
                break;
            case XHCI_TRB_DATA_STG:
                Log(("Data stage TRB: Ptr=%RGp IOC=%u CH=%u DIR=%u\n", xfer.data.data_ptr, xfer.data.ioc, xfer.data.ch, xfer.data.dir));
                rc = xhciR3QueueControlTD(pDevIns, pThis, pThisCC, pRh, GCPhysXfrTRB, &xfer, &ep_ctx, uSlotID,
                                          slot_ctx.dev_addr, uDBTarget);
                break;
            case XHCI_TRB_STATUS_STG:
                Log(("Status stage TRB: IOC=%u CH=%u DIR=%u\n", xfer.status.ioc, xfer.status.ch, xfer.status.dir));
                rc = xhciR3QueueControlTD(pDevIns, pThis, pThisCC, pRh, GCPhysXfrTRB, &xfer, &ep_ctx, uSlotID,
                                          slot_ctx.dev_addr, uDBTarget);
                break;
            case XHCI_TRB_ISOCH:
                Log(("Isoch TRB: Ptr=%RGp IOC=%u CH=%u TLBPC=%u TBC=%u SIA=%u FrmID=%u\n", xfer.isoc.data_ptr, xfer.isoc.ioc, xfer.isoc.ch, xfer.isoc.tlbpc, xfer.isoc.tbc, xfer.isoc.sia, xfer.isoc.frm_id));
                rc = xhciR3QueueIsochTD(pDevIns, pThis, pThisCC, pRh, GCPhysXfrTRB, &xfer, &ep_ctx, uSlotID,
                                        slot_ctx.dev_addr, uDBTarget, &ctxIsoch);
                break;
            case XHCI_TRB_LINK:
                Log2(("Link extra-TD: Ptr=%RGp IOC=%u TC=%u CH=%u\n", xfer.link.rseg_ptr, xfer.link.ioc, xfer.link.toggle, xfer.link.chain));
                Assert(!xfer.link.chain);
                /* Set new TREP but leave DCS bit alone... */
                ep_ctx.trep = (xfer.link.rseg_ptr & XHCI_TRDP_ADDR_MASK) | (ep_ctx.trep & XHCI_TRDP_DCS_MASK);
                /* ...and flip the DCS bit if required. Then update the TREP. */
                ep_ctx.trep = ep_ctx.trep ^ xfer.link.toggle;
                rc = xhciR3WriteBackEp(pDevIns, pThis, uSlotID, uDBTarget, &ep_ctx);
                break;
            case XHCI_TRB_NOOP_XFER:
                Log2(("No op xfer: IOC=%u CH=%u ENT=%u\n", xfer.nop.ioc, xfer.nop.ch, xfer.nop.ent));
                /* A no-op transfer TRB must not be part of a chain. See 4.11.7. */
                Assert(!xfer.link.chain);
                /* Update enqueue pointer (TRB was not yet completed). */
                ep_ctx.trep += sizeof(XHCI_XFER_TRB);
                rc = xhciR3WriteBackEp(pDevIns, pThis, uSlotID, uDBTarget, &ep_ctx);
                break;
            default:
                Log(("Unsupported TRB!!\n"));
                rc = VERR_NOT_SUPPORTED;
                break;
            }
            /* If queuing failed, stop right here. */
            if (RT_FAILURE(rc))
                fContinue = false;
        }
        else
        {
            LogFunc(("Transfer Ring empty\n"));
            fContinue = false;

            /* If an isochronous ring is empty, this is an overrun/underrun. At this point
             * the ring will no longer be scheduled (until the doorbell is rung again)
             * but it remains in the Running state. This error is only reported if someone
             * rang the doorbell and there are no TDs available or in-flight.
             */
            if (    (ep_ctx.trep == ep_ctx.trdp)    /* Nothing in-flight? */
                 && (ep_ctx.ep_type == XHCI_EPTYPE_ISOCH_IN || ep_ctx.ep_type == XHCI_EPTYPE_ISOCH_OUT))
            {
                /* There is no TRB associated with this error; the slot context
                 * determines the interrupter.
                 */
                Log(("Isochronous ring %s, TRDP:%RGp\n", ep_ctx.ep_type == XHCI_EPTYPE_ISOCH_IN ? "overrun" : "underrun", ep_ctx.trdp & XHCI_TRDP_ADDR_MASK));
                rc = xhciR3PostXferEvent(pDevIns, pThis, slot_ctx.intr_tgt, 0,
                                         ep_ctx.ep_type == XHCI_EPTYPE_ISOCH_IN ? XHCI_TCC_RING_OVERRUN : XHCI_TCC_RING_UNDERRUN,
                                         uSlotID, uDBTarget, 0, false, false);
            }

        }

        /* Kill the xHC if the TRB list has no end in sight. */
        if (++cTrbs > XHCI_MAX_NUM_TRBS)
        {
            /* Stop the xHC with an error. */
            xhciR3EndlessTrbError(pDevIns, pThis);

            /* Get out of the loop. */
            fContinue = false;
            rc = VERR_NOT_SUPPORTED;    /* No good error code really... */
        }
    } while (fContinue);

    /* It can unfortunately happen that for endpoints with more than one
     * transfer per USB frame, there won't be a complete multi-packet URB ready
     * when we go looking for it. If that happens, we'll "rewind" the TREP and
     * try again later. Since the URB construction is done under a lock, this
     * is safe as we won't be accessing the endpoint concurrently.
     */
    if (ctxIsoch.pUrb)
    {
        Log(("Unfinished ISOC URB (%u packets out of %u)!\n", ctxIsoch.iPkt, ctxIsoch.pUrb->cIsocPkts));
        /* If submitting failed, the URB is already freed. */
        if (!ctxIsoch.fSubmitFailed)
            VUSBIRhFreeUrb(pRh->pIRhConn, ctxIsoch.pUrb);
        ep_ctx.trep = ctxIsoch.uInitTREP;
        xhciR3WriteBackEp(pDevIns, pThis, uSlotID, uDBTarget, &ep_ctx);
    }
    return VINF_SUCCESS;
}


/**
 * A worker routine for Address Device command. Builds a URB containing
 * a SET_ADDRESS requests and (synchronously) submits it to VUSB, then
 * follows up with a status stage URB.
 *
 * @returns true on success.
 * @returns false on failure to submit.
 * @param   pThisCC     The xHCI device state, ring-3 edition.
 * @param   uSlotID     Slot ID to assign address to.
 * @param   uDevAddr    New device address.
 * @param   iPort       The xHCI root hub port index.
 */
static bool xhciR3IssueSetAddress(PXHCICC pThisCC, uint8_t uSlotID, uint8_t uDevAddr, unsigned iPort)
{
    PXHCIROOTHUBR3  pRh  = GET_PORT_PRH(pThisCC, iPort);

    Assert(uSlotID);
    LogFlowFunc(("Slot %u port idx %u: new address is %u\n", uSlotID, iPort, uDevAddr));

    /* For USB3 devices, force the port number. This simulates the fact that USB3 uses directed (unicast) traffic. */
    if (!IS_USB3_PORT_IDX_R3(pThisCC, iPort))
        iPort = VUSB_DEVICE_PORT_INVALID;
    else
        iPort = GET_VUSB_PORT_FROM_XHCI_PORT(pRh, iPort);

    /* Allocate and initialize a URB. NB: Zero cTds indicates a URB not submitted by guest. */
    PVUSBURB pUrb = VUSBIRhNewUrb(pRh->pIRhConn, 0 /* address */, iPort, VUSBXFERTYPE_CTRL, VUSBDIRECTION_SETUP,
                                  sizeof(VUSBSETUP), 0 /* cTds */, NULL);
    if (!pUrb)
        return false;

    pUrb->EndPt           = 0;
    pUrb->fShortNotOk     = true;
    pUrb->enmStatus       = VUSBSTATUS_OK;
    pUrb->pHci->uSlotID   = uSlotID;
    pUrb->pHci->cTRB      = 0;

    /* Build the request. */
    PVUSBSETUP  pSetup    = (PVUSBSETUP)pUrb->abData;
    pSetup->bmRequestType = VUSB_DIR_TO_DEVICE | VUSB_REQ_STANDARD | VUSB_TO_DEVICE;
    pSetup->bRequest      = VUSB_REQ_SET_ADDRESS;
    pSetup->wValue        = uDevAddr;
    pSetup->wIndex        = 0;
    pSetup->wLength       = 0;

    /* NB: We assume the address assignment is a synchronous operation. */

    /* Submit the setup URB. */
    Log(("%s: xhciSetAddress setup: cbData=%u\n", pUrb->pszDesc, pUrb->cbData));
    RTCritSectLeave(&pThisCC->CritSectThrd);
    int rc = VUSBIRhSubmitUrb(pRh->pIRhConn, pUrb, &pRh->Led);
    RTCritSectEnter(&pThisCC->CritSectThrd);
    if (RT_FAILURE(rc))
    {
        Log(("xhciSetAddress: setup stage failed pUrb=%p!!\n", pUrb));
        return false;
    }

    /* To complete the SET_ADDRESS request, the status stage must succeed. */
    pUrb = VUSBIRhNewUrb(pRh->pIRhConn, 0 /* address */, iPort, VUSBXFERTYPE_CTRL, VUSBDIRECTION_IN, 0 /* cbData */, 0 /* cTds */,
                         NULL);
    if (!pUrb)
        return false;

    pUrb->EndPt           = 0;
    pUrb->fShortNotOk     = true;
    pUrb->enmStatus       = VUSBSTATUS_OK;
    pUrb->pHci->uSlotID   = uSlotID;
    pUrb->pHci->cTRB      = 0;

    /* Submit the setup URB. */
    Log(("%s: xhciSetAddress status: cbData=%u\n", pUrb->pszDesc, pUrb->cbData));
    RTCritSectLeave(&pThisCC->CritSectThrd);
    rc = VUSBIRhSubmitUrb(pRh->pIRhConn, pUrb, &pRh->Led);
    RTCritSectEnter(&pThisCC->CritSectThrd);
    if (RT_FAILURE(rc))
    {
        Log(("xhciSetAddress: status stage failed pUrb=%p!!\n", pUrb));
        return false;
    }

    Log(("xhciSetAddress: set address succeeded\n"));
    return true;
}


/**
 * Address a device.
 *
 * @returns TRB completion code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The xHCI device state, shared edition.
 * @param   pThisCC     The xHCI device state, ring-3 edition.
 * @param   uInpCtxAddr Address of the input context.
 * @param   uSlotID     Slot ID to assign address to.
 * @param   fBSR        Block Set address Request flag.
 */
static unsigned xhciR3AddressDevice(PPDMDEVINS pDevIns, PXHCI pThis, PXHCICC pThisCC, uint64_t uInpCtxAddr,
                                    uint8_t uSlotID, bool fBSR)
{
    RTGCPHYS        GCPhysInpCtx = uInpCtxAddr & XHCI_CTX_ADDR_MASK;
    RTGCPHYS        GCPhysInpSlot;
    RTGCPHYS        GCPhysOutSlot;
    XHCI_INPC_CTX   icc;            /* Input Control Context (ICI=0). */
    XHCI_SLOT_CTX   inp_slot_ctx;   /* Input Slot Context (ICI=1). */
    XHCI_EP_CTX     ep_ctx;         /* Endpoint Context (ICI=2+). */
    XHCI_SLOT_CTX   out_slot_ctx;   /* Output Slot Context. */
    uint8_t         dev_addr;
    unsigned        cc = XHCI_TCC_SUCCESS;

    Assert(GCPhysInpCtx);
    Assert(uSlotID);
    LogFlowFunc(("Slot ID %u, input control context @ %RGp\n", uSlotID, GCPhysInpCtx));

    /* Determine the address of the output slot context. */
    GCPhysOutSlot = xhciR3FetchDevCtxAddr(pDevIns, pThis, uSlotID);

    /* Fetch the output slot context. */
    PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysOutSlot, &out_slot_ctx, sizeof(out_slot_ctx));

    /// @todo Check for valid context (6.2.2.1, 6.2.3.1)

    /* See 4.6.5 */
    do {
        /* Parameter validation depends on whether the BSR flag is set or not. */
        if (fBSR)
        {
            /* Check that the output slot context state is in Enabled state. */
            if (out_slot_ctx.slot_state >= XHCI_SLTST_DEFAULT)
            {
                Log(("Output slot context state (%u) wrong (BSR)!\n", out_slot_ctx.slot_state));
                cc = XHCI_TCC_CTX_STATE_ERR;
                break;
            }
            dev_addr = 0;
        }
        else
        {
            /* Check that the output slot context state is in Enabled or Default state. */
            if (out_slot_ctx.slot_state > XHCI_SLTST_DEFAULT)
            {
                Log(("Output slot context state (%u) wrong (no-BSR)!\n", out_slot_ctx.slot_state));
                cc = XHCI_TCC_CTX_STATE_ERR;
                break;
            }
            dev_addr = xhciR3SelectNewAddress(pThis, uSlotID);
        }

        /* Fetch the input control context. */
        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysInpCtx, &icc, sizeof(icc));
        Assert(icc.add_flags == (RT_BIT(0) | RT_BIT(1)));   /* Should have been already checked. */
        Assert(!icc.drop_flags);

        /* Calculate the address of the input slot context (ICI=1/DCI=0). */
        GCPhysInpSlot = GCPhysInpCtx + sizeof(XHCI_INPC_CTX);

        /* Read the input slot context. */
        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysInpSlot, &inp_slot_ctx, sizeof(inp_slot_ctx));

        /* If BSR isn't set, issue the actual SET_ADDRESS request. */
        if (!fBSR) {
            unsigned    iPort;

            /* We have to dig out the port number/index to determine which virtual root hub to use. */
            iPort = ID_TO_IDX(inp_slot_ctx.rh_port);
            if (iPort >= XHCI_NDP_CFG(pThis))
            {
                Log(("Port out of range (index %u)!\n", iPort));
                cc = XHCI_TCC_USB_XACT_ERR;
                break;
            }
            if (!xhciR3IssueSetAddress(pThisCC, uSlotID, dev_addr, iPort))
            {
                Log(("SET_ADDRESS failed!\n"));
                cc = XHCI_TCC_USB_XACT_ERR;
                break;
            }
        }

        /* Copy the slot context with appropriate modifications. */
        out_slot_ctx = inp_slot_ctx;
        if (fBSR)
            out_slot_ctx.slot_state = XHCI_SLTST_DEFAULT;
        else
            out_slot_ctx.slot_state = XHCI_SLTST_ADDRESSED;
        out_slot_ctx.dev_addr = dev_addr;
        PDMDevHlpPCIPhysWriteMeta(pDevIns, GCPhysOutSlot, &out_slot_ctx, sizeof(out_slot_ctx));

        /* Point at the EP0 contexts. */
        GCPhysInpSlot += sizeof(inp_slot_ctx);
        GCPhysOutSlot += sizeof(out_slot_ctx);

        /* Copy EP0 context with appropriate modifications. */
        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysInpSlot, &ep_ctx, sizeof(ep_ctx));
        xhciR3EnableEP(&ep_ctx);
        PDMDevHlpPCIPhysWriteMeta(pDevIns, GCPhysOutSlot, &ep_ctx, sizeof(ep_ctx));
    } while (0);

    return cc;
}


/**
 * Reset a halted endpoint.
 *
 * @returns TRB completion code.
 * @param   pDevIns         The device instance.
 * @param   pThis           Pointer to the xHCI state.
 * @param   uSlotID         Slot ID to work with.
 * @param   uDCI            DCI of the endpoint to reset.
 * @param   fTSP            The Transfer State Preserve flag.
 */
static unsigned xhciR3ResetEndpoint(PPDMDEVINS pDevIns, PXHCI pThis, uint8_t uSlotID, uint8_t uDCI, bool fTSP)
{
    RT_NOREF(fTSP);
    RTGCPHYS        GCPhysSlot;
    RTGCPHYS        GCPhysEndp;
    XHCI_SLOT_CTX   slot_ctx;
    XHCI_EP_CTX     endp_ctx;
    unsigned        cc = XHCI_TCC_SUCCESS;

    Assert(uSlotID);

    /* Determine the addresses of the contexts. */
    GCPhysSlot = xhciR3FetchDevCtxAddr(pDevIns, pThis, uSlotID);
    GCPhysEndp = GCPhysSlot + uDCI * sizeof(XHCI_EP_CTX);

    /* Fetch the slot context. */
    PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysSlot, &slot_ctx, sizeof(slot_ctx));

    /* See 4.6.8 */
    do {
        /* Check that the slot context state is Default, Addressed, or Configured. */
        if (slot_ctx.slot_state < XHCI_SLTST_DEFAULT)
        {
            Log(("Slot context state wrong (%u)!\n", slot_ctx.slot_state));
            cc = XHCI_TCC_CTX_STATE_ERR;
            break;
        }

        /* Fetch the endpoint context. */
        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysEndp, &endp_ctx, sizeof(endp_ctx));

        /* Check that the endpoint context state is Halted. */
        if (endp_ctx.ep_state != XHCI_EPST_HALTED)
        {
            Log(("Endpoint context state wrong (%u)!\n", endp_ctx.ep_state));
            cc = XHCI_TCC_CTX_STATE_ERR;
            break;
        }

        /* Transition EP state. */
        endp_ctx.ep_state = XHCI_EPST_STOPPED;

        /// @todo What can we do with the TSP flag?
        /// @todo Anything to do WRT enabling the corresponding doorbell register?

        /* Write back the updated endpoint context. */
        PDMDevHlpPCIPhysWriteMeta(pDevIns, GCPhysEndp, &endp_ctx, sizeof(endp_ctx));
    } while (0);

    return cc;
}


/**
 * Stop a running endpoint.
 *
 * @returns TRB completion code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The xHCI device state, shared edition.
 * @param   pThisCC     The xHCI device state, ring-3 edition.
 * @param   uSlotID     Slot ID to work with.
 * @param   uDCI        DCI of the endpoint to stop.
 * @param   fTSP        The Suspend flag.
 */
static unsigned xhciR3StopEndpoint(PPDMDEVINS pDevIns, PXHCI pThis, PXHCICC pThisCC, uint8_t uSlotID, uint8_t uDCI, bool fTSP)
{
    RT_NOREF(fTSP);
    RTGCPHYS        GCPhysSlot;
    RTGCPHYS        GCPhysEndp;
    XHCI_SLOT_CTX   slot_ctx;
    XHCI_EP_CTX     endp_ctx;
    unsigned        cc = XHCI_TCC_SUCCESS;

    Assert(uSlotID);

    /* Determine the addresses of the contexts. */
    GCPhysSlot = xhciR3FetchDevCtxAddr(pDevIns, pThis, uSlotID);
    GCPhysEndp = GCPhysSlot + uDCI * sizeof(XHCI_EP_CTX);

    /* Fetch the slot context. */
    PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysSlot, &slot_ctx, sizeof(slot_ctx));

    /* See 4.6.9 */
    do {
        /* Check that the slot context state is Default, Addressed, or Configured. */
        if (slot_ctx.slot_state < XHCI_SLTST_DEFAULT)
        {
            Log(("Slot context state wrong (%u)!\n", slot_ctx.slot_state));
            cc = XHCI_TCC_CTX_STATE_ERR;
            break;
        }

        /* The doorbell could be ringing; stop it if so. */
        if (pThis->aBellsRung[ID_TO_IDX(uSlotID)] & (1 << uDCI))
        {
            Log(("Unring bell for slot ID %u, DCI %u\n", uSlotID, uDCI));
            ASMAtomicAndU32(&pThis->aBellsRung[ID_TO_IDX(uSlotID)], ~(1 << uDCI));
        }

        /* Fetch the endpoint context. */
        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysEndp, &endp_ctx, sizeof(endp_ctx));

        /* Check that the endpoint context state is Running. */
        if (endp_ctx.ep_state != XHCI_EPST_RUNNING)
        {
            Log(("Endpoint context state wrong (%u)!\n", endp_ctx.ep_state));
            cc = XHCI_TCC_CTX_STATE_ERR;
            break;
        }

        /* Transition EP state. */
        endp_ctx.ep_state = XHCI_EPST_STOPPED;

        /* Write back the updated endpoint context *now*, before actually canceling anyhing. */
        PDMDevHlpPCIPhysWriteMeta(pDevIns, GCPhysEndp, &endp_ctx, sizeof(endp_ctx));

        /// @todo What can we do with the SP flag?

        PXHCIROOTHUBR3  pRh;
        uint32_t        uPort;

        /* Abort the endpoint, i.e. cancel any outstanding URBs. This needs to be done after
         * writing back the EP state so that the completion callback can operate. Note that
         * the completion callback will not modify the TR when it sees that the EP is not in
         * the 'running' state.
         * NB: If a URB is canceled before it completed, we have no way to tell if any data
         * was already (partially) transferred.
         */
        if (RT_SUCCESS(xhciR3FindRhDevBySlot(pDevIns, pThis, pThisCC, uSlotID, &pRh, &uPort)))
        {
            /* Temporarily give up the lock so that the completion callbacks can run. */
            RTCritSectLeave(&pThisCC->CritSectThrd);
            Log(("Aborting DCI %u -> ep=%u d=%u\n", uDCI, uDCI / 2, uDCI & 1 ? VUSBDIRECTION_IN : VUSBDIRECTION_OUT));
            pRh->pIRhConn->pfnAbortEpByPort(pRh->pIRhConn, uPort, uDCI / 2, uDCI & 1 ? VUSBDIRECTION_IN : VUSBDIRECTION_OUT);
            RTCritSectEnter(&pThisCC->CritSectThrd);
        }

        /* Once the completion callbacks had a chance to run, we have to adjust
         * the endpoint state.
         * NB: The guest may just ring the doorbell to continue and not execute
         * 'Set TRDP' after stopping the endpoint.
         */
        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysEndp, &endp_ctx, sizeof(endp_ctx));

        /* If the enqueue and dequeue pointers are different, a transfer was
         * in progress.
         */
        bool fXferWasInProgress = endp_ctx.trep != endp_ctx.trdp;

        /* Reset the TREP, but the EDTLA should be left alone. */
        endp_ctx.trep = endp_ctx.trdp;

        if (fXferWasInProgress)
        {
            /* Fetch the transfer TRB to see the length. */
            RTGCPHYS        GCPhysXfrTRB = endp_ctx.trdp & XHCI_TRDP_ADDR_MASK;
            XHCI_XFER_TRB   XferTRB;
            PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysXfrTRB, &XferTRB, sizeof(XferTRB));

            xhciR3PostXferEvent(pDevIns, pThis, slot_ctx.intr_tgt, XferTRB.gen.xfr_len, XHCI_TCC_STOPPED, uSlotID, uDCI,
                                GCPhysXfrTRB, false, false);
        }
        else
        {
            /* We need to generate a Force Stopped Event or FSE. Note that FSEs were optional
             * in xHCI 0.96 but aren't in 1.0.
             */
            xhciR3PostXferEvent(pDevIns, pThis, slot_ctx.intr_tgt, 0, XHCI_TCC_STP_INV_LEN, uSlotID, uDCI,
                                endp_ctx.trdp & XHCI_TRDP_ADDR_MASK, false, false);
        }

        /* Write back the updated endpoint context again. */
        PDMDevHlpPCIPhysWriteMeta(pDevIns, GCPhysEndp, &endp_ctx, sizeof(endp_ctx));

    } while (0);

    return cc;
}


/**
 * Set a new TR Dequeue Pointer for an endpoint.
 *
 * @returns TRB completion code.
 * @param   pDevIns         The device instance.
 * @param   pThis           Pointer to the xHCI state.
 * @param   uSlotID         Slot ID to work with.
 * @param   uDCI            DCI of the endpoint to reset.
 * @param   uTRDP           The TRDP including DCS/ flag.
 */
static unsigned xhciR3SetTRDP(PPDMDEVINS pDevIns, PXHCI pThis, uint8_t uSlotID, uint8_t uDCI, uint64_t uTRDP)
{
    RTGCPHYS        GCPhysSlot;
    RTGCPHYS        GCPhysEndp;
    XHCI_SLOT_CTX   slot_ctx;
    XHCI_EP_CTX     endp_ctx;
    unsigned        cc = XHCI_TCC_SUCCESS;

    Assert(uSlotID);

    /* Determine the addresses of the contexts. */
    GCPhysSlot = xhciR3FetchDevCtxAddr(pDevIns, pThis, uSlotID);
    GCPhysEndp = GCPhysSlot + uDCI * sizeof(XHCI_EP_CTX);

    /* Fetch the slot context. */
    PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysSlot, &slot_ctx, sizeof(slot_ctx));

    /* See 4.6.10 */
    do {
        /* Check that the slot context state is Default, Addressed, or Configured. */
        if (slot_ctx.slot_state < XHCI_SLTST_DEFAULT)
        {
            Log(("Slot context state wrong (%u)!\n", slot_ctx.slot_state));
            cc = XHCI_TCC_CTX_STATE_ERR;
            break;
        }

        /* Fetch the endpoint context. */
        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysEndp, &endp_ctx, sizeof(endp_ctx));

        /* Check that the endpoint context state is Stopped or Error. */
        if (endp_ctx.ep_state != XHCI_EPST_STOPPED && endp_ctx.ep_state != XHCI_EPST_ERROR)
        {
            Log(("Endpoint context state wrong (%u)!\n", endp_ctx.ep_state));
            cc = XHCI_TCC_CTX_STATE_ERR;
            break;
        }

        /* Update the TRDP/TREP and DCS. */
        endp_ctx.trdp = uTRDP;
        endp_ctx.trep = uTRDP;

        /* Also clear the in-flight counter! */
        endp_ctx.ifc = 0;

        /// @todo Handle streams

        /* Write back the updated endpoint context. */
        PDMDevHlpPCIPhysWriteMeta(pDevIns, GCPhysEndp, &endp_ctx, sizeof(endp_ctx));
    } while (0);

    return cc;
}


/**
 * Prepare for a device reset.
 *
 * @returns TRB completion code.
 * @param   pDevIns         The device instance.
 * @param   pThis           Pointer to the xHCI state.
 * @param   uSlotID         Slot ID to work with.
 */
static unsigned xhciR3ResetDevice(PPDMDEVINS pDevIns, PXHCI pThis, uint8_t uSlotID)
{
    RTGCPHYS        GCPhysSlot;
    XHCI_SLOT_CTX   slot_ctx;
    XHCI_DEV_CTX    dc;
    unsigned        num_ctx;
    unsigned        i;
    unsigned        cc = XHCI_TCC_SUCCESS;

    Assert(uSlotID);

    /* Determine the address of the slot/device context. */
    GCPhysSlot = xhciR3FetchDevCtxAddr(pDevIns, pThis, uSlotID);

    /* Fetch the slot context. */
    PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysSlot, &slot_ctx, sizeof(slot_ctx));

    /* See 4.6.11. */
    do {
        /* Check that the slot context state is Addressed or Configured. */
        if (slot_ctx.slot_state < XHCI_SLTST_ADDRESSED)
        {
            Log(("Slot context state wrong (%u)!\n", slot_ctx.slot_state));
            cc = XHCI_TCC_CTX_STATE_ERR;
            break;
        }

        /* Read the entire Device Context. */
        num_ctx = slot_ctx.ctx_ent + 1; /* Slot context plus EPs. */
        Assert(num_ctx);
        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysSlot, &dc, num_ctx * sizeof(XHCI_SLOT_CTX));

        /// @todo Abort any outstanding transfers!

        /* Set slot state to Default and reset the USB device address. */
        dc.entry[0].sc.slot_state = XHCI_SLTST_DEFAULT;
        dc.entry[0].sc.dev_addr   = 0;

        /* Disable all endpoints except for EP 0 (aka DCI 1). */
        for (i = 2; i < num_ctx; ++i)
            dc.entry[i].ep.ep_state = XHCI_EPST_DISABLED;

        /* Write back the updated device context. */
        PDMDevHlpPCIPhysWriteMeta(pDevIns, GCPhysSlot, &dc, num_ctx * sizeof(XHCI_SLOT_CTX));
    } while (0);

    return cc;
}


/**
 * Configure a device (even though the relevant command is called 'Configure
 * Endpoint'. This includes adding/dropping endpoint contexts as directed by
 * the input control context bits.
 *
 * @returns TRB completion code.
 * @param   pDevIns         The device instance.
 * @param   pThis           Pointer to the xHCI state.
 * @param   uInpCtxAddr     Address of the input context.
 * @param   uSlotID         Slot ID associated with the context.
 * @param   fDC             Deconfigure flag set (input context unused).
 */
static unsigned xhciR3ConfigureDevice(PPDMDEVINS pDevIns, PXHCI pThis, uint64_t uInpCtxAddr, uint8_t uSlotID, bool fDC)
{
    RTGCPHYS        GCPhysInpCtx = uInpCtxAddr & XHCI_CTX_ADDR_MASK;
    RTGCPHYS        GCPhysInpSlot;
    RTGCPHYS        GCPhysOutSlot;
    RTGCPHYS        GCPhysOutEndp;
    XHCI_INPC_CTX   icc;            /* Input Control Context (ICI=0). */
    XHCI_SLOT_CTX   out_slot_ctx;   /* Slot context (DCI=0). */
    XHCI_EP_CTX     out_endp_ctx;   /* Endpoint Context (DCI=1). */
    unsigned        cc = XHCI_TCC_SUCCESS;
    uint32_t        uAddFlags;
    uint32_t        uDropFlags;
    unsigned        num_inp_ctx;
    unsigned        num_out_ctx;
    XHCI_DEV_CTX    dc_inp;
    XHCI_DEV_CTX    dc_out;
    unsigned        uDCI;

    RT_ZERO(dc_inp);

    Assert(uSlotID);
    LogFlowFunc(("Slot ID %u, input control context @ %RGp\n", uSlotID, GCPhysInpCtx));

    /* Determine the address of the output slot context. */
    GCPhysOutSlot = xhciR3FetchDevCtxAddr(pDevIns, pThis, uSlotID);
    Assert(GCPhysOutSlot);

    /* Fetch the output slot context. */
    PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysOutSlot, &out_slot_ctx, sizeof(out_slot_ctx));

    /* See 4.6.6 */
    do {
        /* Check that the output slot context state is Addressed, or Configured. */
        if (out_slot_ctx.slot_state < XHCI_SLTST_ADDRESSED)
        {
            Log(("Output slot context state wrong (%u)!\n", out_slot_ctx.slot_state));
            cc = XHCI_TCC_CTX_STATE_ERR;
            break;
        }

        /* Check for deconfiguration request. */
        if (fDC) {
            if (out_slot_ctx.slot_state == XHCI_SLTST_CONFIGURED) {
                /* Disable all enabled endpoints. */
                uDropFlags = 0xFFFFFFFC; /** @todo r=bird: Why do you set uDropFlags and uAddFlags in a code path that doesn't use
                                          * them?  This is a _very_ difficult function to get the hang of the way it's written.
                                          * Stuff like this looks like there's a control flow flaw (as to the do-break-while-false
                                          * loop which doesn't do any clean up or logging at the end and seems only sever the very
                                          * dubious purpose of making sure ther's only one return statement).   The insistance on
                                          * C-style variable declarations (top of function), makes checking state harder, which is
                                          * why it's discouraged. */
                uAddFlags  = 0;

                /* Start with EP1. */
                GCPhysOutEndp = GCPhysOutSlot + sizeof(XHCI_SLOT_CTX) + sizeof(XHCI_EP_CTX);

                PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysOutEndp, &out_endp_ctx, sizeof(out_endp_ctx));
                out_endp_ctx.ep_state = XHCI_EPST_DISABLED;
                PDMDevHlpPCIPhysWriteMeta(pDevIns, GCPhysOutEndp, &out_endp_ctx, sizeof(out_endp_ctx));
                GCPhysOutEndp += sizeof(XHCI_EP_CTX);   /* Point to the next EP context. */

                /* Finally update the output slot context. */
                out_slot_ctx.ctx_ent    = 1;    /* Only EP0 left. */
                out_slot_ctx.slot_state = XHCI_SLTST_ADDRESSED;
                PDMDevHlpPCIPhysWriteMeta(pDevIns, GCPhysOutSlot, &out_slot_ctx, sizeof(out_slot_ctx));
                LogFlow(("Setting Output Slot State to Addressed, Context Entries = %u\n", out_slot_ctx.ctx_ent));
            }
            else
                /* NB: Attempts to deconfigure a slot in Addressed state are ignored. */
                Log(("Ignoring attempt to deconfigure slot in Addressed state!\n"));
            break;
        }

        /* Fetch the input control context. */
        Assert(GCPhysInpCtx);
        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysInpCtx, &icc, sizeof(icc));
        Assert(icc.add_flags || icc.drop_flags);    /* Make sure there's something to do. */

        uAddFlags  = icc.add_flags;
        uDropFlags = icc.drop_flags;
        LogFlowFunc(("Add Flags=%08X, Drop Flags=%08X\n", uAddFlags, uDropFlags));

        /* If and only if any 'add context' flag is set, fetch the corresponding
         * input device context.
         */
        if (uAddFlags) {
            /* Calculate the address of the input slot context (ICI=1/DCI=0). */
            GCPhysInpSlot = GCPhysInpCtx + sizeof(XHCI_INPC_CTX);

            /* Read the input Slot Context plus all Endpoint Contexts up to and
             * including the one with the highest 'add' bit set.
             */
            num_inp_ctx = ASMBitLastSetU32(uAddFlags);
            Assert(num_inp_ctx);
            PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysInpSlot, &dc_inp, num_inp_ctx * sizeof(XHCI_DS_ENTRY));

            /// @todo Check that the highest set add flag isn't beyond input slot Context Entries

            /// @todo Check input slot context according to 6.2.2.2
            /// @todo Check input EP contexts according to 6.2.3.2
        }
        /* Read the output Slot Context plus all Endpoint Contexts up to and
         * including the one with the highest 'add' or 'drop' bit set.
         */
        num_out_ctx = ASMBitLastSetU32(uAddFlags | uDropFlags);
        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysOutSlot, &dc_out, num_out_ctx * sizeof(XHCI_DS_ENTRY));

        /* Drop contexts as directed by flags. */
        for (uDCI = 2; uDCI < 32; ++uDCI)
        {
            if (!((1 << uDCI) & uDropFlags))
                continue;

            Log2(("Dropping EP DCI %u\n", uDCI));
            dc_out.entry[uDCI].ep.ep_state = XHCI_EPST_DISABLED;
            /// @todo Do we need to bother tracking resources/bandwidth?
        }

        /* Now add contexts as directed by flags. */
        for (uDCI = 2; uDCI < 32; ++uDCI)
        {
            if (!((1 << uDCI) & uAddFlags))
                continue;

            Assert(!fDC);
            /* Copy over EP context, set to running. */
            Log2(("Adding EP DCI %u\n", uDCI));
            dc_out.entry[uDCI].ep = dc_inp.entry[uDCI].ep;
            xhciR3EnableEP(&dc_out.entry[uDCI].ep);
            /// @todo Do we need to bother tracking resources/bandwidth?
        }

        /* Finally update the device context. */
        if (fDC || dc_inp.entry[0].sc.ctx_ent == 1)
        {
            dc_out.entry[0].sc.slot_state = XHCI_SLTST_ADDRESSED;
            dc_out.entry[0].sc.ctx_ent    = 1;
            LogFlow(("Setting Output Slot State to Addressed\n"));
        }
        else
        {
            uint32_t    uKillFlags = uDropFlags & ~uAddFlags;   /* Endpoints going away. */

            /* At least one EP enabled. Update Context Entries and state. */
            Assert(dc_inp.entry[0].sc.ctx_ent);
            dc_out.entry[0].sc.slot_state = XHCI_SLTST_CONFIGURED;
            if (ID_TO_IDX(ASMBitLastSetU32(uAddFlags)) > dc_out.entry[0].sc.ctx_ent)
            {
                /* Adding new endpoints. */
                dc_out.entry[0].sc.ctx_ent = ID_TO_IDX(ASMBitLastSetU32(uAddFlags));
            }
            else if (ID_TO_IDX(ASMBitLastSetU32(uKillFlags)) == dc_out.entry[0].sc.ctx_ent)
            {
                /* Removing the last endpoint, find the last non-disabled one. */
                unsigned    num_ctx_ent;

                Assert(dc_out.entry[0].sc.ctx_ent + 1u == num_out_ctx);
                for (num_ctx_ent = dc_out.entry[0].sc.ctx_ent; num_ctx_ent > 1; --num_ctx_ent)
                    if (dc_out.entry[num_ctx_ent].ep.ep_state != XHCI_EPST_DISABLED)
                        break;
                dc_out.entry[0].sc.ctx_ent = num_ctx_ent;   /* Last valid index to be precise. */
            }
            LogFlow(("Setting Output Slot State to Configured, Context Entries = %u\n", dc_out.entry[0].sc.ctx_ent));
        }

        /* If there were no errors, write back the updated output context. */
        LogFlow(("Success, updating Output Context @ %RGp\n", GCPhysOutSlot));
        PDMDevHlpPCIPhysWriteMeta(pDevIns, GCPhysOutSlot, &dc_out, num_out_ctx * sizeof(XHCI_DS_ENTRY));
    } while (0);

    return cc;
}


/**
 * Evaluate an input context. This involves modifying device and endpoint
 * contexts as directed by the input control context add bits.
 *
 * @returns TRB completion code.
 * @param   pDevIns         The device instance.
 * @param   pThis           Pointer to the xHCI state.
 * @param   uInpCtxAddr     Address of the input context.
 * @param   uSlotID         Slot ID associated with the context.
 */
static unsigned xhciR3EvalContext(PPDMDEVINS pDevIns, PXHCI pThis, uint64_t uInpCtxAddr, uint8_t uSlotID)
{
    RTGCPHYS        GCPhysInpCtx = uInpCtxAddr & XHCI_CTX_ADDR_MASK;
    RTGCPHYS        GCPhysInpSlot;
    RTGCPHYS        GCPhysOutSlot;
    XHCI_INPC_CTX   icc;            /* Input Control Context (ICI=0). */
    XHCI_SLOT_CTX   out_slot_ctx;   /* Slot context (DCI=0). */
    unsigned        cc = XHCI_TCC_SUCCESS;
    uint32_t        uAddFlags;
    uint32_t        uDropFlags;
    unsigned        num_inp_ctx;
    unsigned        num_out_ctx;
    XHCI_DEV_CTX    dc_inp;
    XHCI_DEV_CTX    dc_out;
    unsigned        uDCI;

    Assert(GCPhysInpCtx);
    Assert(uSlotID);
    LogFlowFunc(("Slot ID %u, input control context @ %RGp\n", uSlotID, GCPhysInpCtx));

    /* Determine the address of the output slot context. */
    GCPhysOutSlot = xhciR3FetchDevCtxAddr(pDevIns, pThis, uSlotID);
    Assert(GCPhysOutSlot);

    /* Fetch the output slot context. */
    PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysOutSlot, &out_slot_ctx, sizeof(out_slot_ctx));

    /* See 4.6.7 */
    do {
        /* Check that the output slot context state is Default, Addressed, or Configured. */
        if (out_slot_ctx.slot_state < XHCI_SLTST_DEFAULT)
        {
            Log(("Output slot context state wrong (%u)!\n", out_slot_ctx.slot_state));
            cc = XHCI_TCC_CTX_STATE_ERR;
            break;
        }

        /* Fetch the input control context. */
        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysInpCtx, &icc, sizeof(icc));
        uAddFlags  = icc.add_flags;
        uDropFlags = icc.drop_flags;
        LogFlowFunc(("Add Flags=%08X, Drop Flags=%08X\n", uAddFlags, uDropFlags));

        /* Drop flags "shall be cleared to 0" but also "do not apply" (4.6.7). Log & ignore. */
        if (uDropFlags)
            Log(("Drop flags set (%X) for evaluating context!\n", uDropFlags));

        /* If no add flags are set, nothing will be done but an error is not reported
         * according to the logic flow in 4.6.7.
         */
        if (!uAddFlags)
        {
            Log(("Warning: no add flags set for evaluating context!\n"));
            break;
        }

        /* Calculate the address of the input slot context (ICI=1/DCI=0). */
        GCPhysInpSlot = GCPhysInpCtx + sizeof(XHCI_INPC_CTX);

        /* Read the output Slot Context plus all Endpoint Contexts up to and
         * including the one with the highest 'add' bit set.
         */
        num_inp_ctx = ASMBitLastSetU32(uAddFlags);
        Assert(num_inp_ctx);
        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysInpSlot, &dc_inp, num_inp_ctx * sizeof(XHCI_DS_ENTRY));

        /* Read the output Slot Context plus all Endpoint Contexts up to and
         * including the one with the highest 'add' bit set.
         */
        num_out_ctx = ASMBitLastSetU32(uAddFlags);
        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysOutSlot, &dc_out, num_out_ctx * sizeof(XHCI_DS_ENTRY));

        /// @todo Check input slot context according to 6.2.2.3
        /// @todo Check input EP contexts according to 6.2.3.3
        /// @todo Check that the highest set add flag isn't beyond input slot Context Entries

        /* Evaluate endpoint contexts as directed by add flags. */
        /// @todo 6.2.3.3 suggests only the A1 bit matters? Anything besides A0/A1 is ignored??
        for (uDCI = 1; uDCI < 32; ++uDCI)
        {
            if (!((1 << uDCI) & uAddFlags))
                continue;

            /* Evaluate Max Packet Size. */
            LogFunc(("DCI %u: Max Packet Size: %u -> %u\n", uDCI, dc_out.entry[uDCI].ep.max_pkt_sz, dc_inp.entry[uDCI].ep.max_pkt_sz));
            dc_out.entry[uDCI].ep.max_pkt_sz = dc_inp.entry[uDCI].ep.max_pkt_sz;
        }

        /* Finally update the device context if directed to do so (A0 flag set). */
        if (uAddFlags & RT_BIT(0))
        {
            /* 6.2.2.3 - evaluate Interrupter Target and Max Exit Latency. */
            Log(("Interrupter Target: %u -> %u\n", dc_out.entry[0].sc.intr_tgt, dc_inp.entry[0].sc.intr_tgt));
            Log(("Max Exit Latency  : %u -> %u\n", dc_out.entry[0].sc.max_lat, dc_inp.entry[0].sc.max_lat));

            /// @todo Non-zero Max Exit Latency (see 4.6.7)
            dc_out.entry[0].sc.intr_tgt = dc_inp.entry[0].sc.intr_tgt;
            dc_out.entry[0].sc.max_lat  = dc_inp.entry[0].sc.max_lat;
        }

        /* If there were no errors, write back the updated output context. */
        LogFlow(("Success, updating Output Context @ %RGp\n", GCPhysOutSlot));
        PDMDevHlpPCIPhysWriteMeta(pDevIns, GCPhysOutSlot, &dc_out, num_out_ctx * sizeof(XHCI_DS_ENTRY));
    } while (0);

    return cc;
}


/**
 * Query available port bandwidth.
 *
 * @returns TRB completion code.
 * @param   pDevIns     The device instance.
 * @param   pThis       Pointer to the xHCI state.
 * @param   uDevSpd     Speed of not yet attached devices.
 * @param   uHubSlotID  Hub Slot ID to query (unsupported).
 * @param   uBwCtx      Bandwidth context physical address.
 */
static unsigned xhciR3GetPortBandwidth(PPDMDEVINS pDevIns, PXHCI pThis, uint8_t uDevSpd, uint8_t uHubSlotID, uint64_t uBwCtx)
{
    RT_NOREF(uHubSlotID);
    RTGCPHYS        GCPhysBwCtx;
    unsigned        cc = XHCI_TCC_SUCCESS;
    unsigned        ctx_size;
    unsigned        iPort;
    uint8_t         bw_ctx[RT_ALIGN_32(XHCI_NDP_MAX + 1, 4)] = {0};
    uint8_t         dev_spd;
    uint8_t         avail_bw;

    Assert(!uHubSlotID);
    Assert(uBwCtx);

    /* See 4.6.15. */

    /* Hubs are not supported because guests will never see them. The
     * reported values are more or less dummy because we have no real
     * information about the bandwidth available on the host. The reported
     * values are optimistic, as if each port had its own separate Bus
     * Instance aka BI.
     */

    GCPhysBwCtx = uBwCtx & XHCI_CTX_ADDR_MASK;

    /* Number of ports + 1, rounded up to DWORDs. */
    ctx_size = RT_ALIGN_32(XHCI_NDP_CFG(pThis) + 1, 4);
    LogFlowFunc(("BW Context at %RGp, size %u\n", GCPhysBwCtx, ctx_size));
    Assert(ctx_size <= sizeof(bw_ctx));

    /* Go over all the ports. */
    for (iPort = 0; iPort < XHCI_NDP_CFG(pThis); ++iPort)
    {
        /* Get the device speed from the port... */
        dev_spd = (pThis->aPorts[iPort].portsc & XHCI_PORT_PLS_MASK) >> XHCI_PORT_PLS_SHIFT;
        /* ...and if nothing is attached, use the provided default. */
        if (!dev_spd)
            dev_spd = uDevSpd;

        /* For USB3 ports, report 90% available for SS devices (see 6.2.6). */
        if (IS_USB3_PORT_IDX_SHR(pThis, iPort))
            avail_bw = dev_spd == XHCI_SPD_SUPER ? 90 : 0;
        else
            /* For USB2 ports, report 80% available for HS and 90% for FS/LS. */
            switch (dev_spd)
            {
            case XHCI_SPD_HIGH:
                avail_bw = 80;
                break;
            case XHCI_SPD_FULL:
            case XHCI_SPD_LOW:
                avail_bw = 90;
                break;
            default:
                avail_bw = 0;
            }

        /* The first entry in the context is reserved. */
        bw_ctx[iPort + 1] = avail_bw;
    }

    /* Write back the bandwidth context. */
    PDMDevHlpPCIPhysWriteMeta(pDevIns, GCPhysBwCtx, &bw_ctx, ctx_size);

    return cc;
}

#define NEC_MAGIC   ('x' | ('H' << 8) | ('C' << 16) | ('I' << 24))

/**
 * Take a 64-bit input, shake well, produce 32-bit token. This mechanism
 * prevents NEC/Renesas drivers from running on 3rd party hardware. Mirrors
 * code found in vendor's drivers.
 */
static uint32_t xhciR3NecAuthenticate(uint64_t cookie)
{
    uint32_t    cookie_lo = RT_LODWORD(cookie);
    uint32_t    cookie_hi = RT_HIDWORD(cookie);
    uint32_t    shift_cnt;
    uint32_t    token;

    shift_cnt = (cookie_hi >> 8) & 31;
    token     = ASMRotateRightU32(cookie_lo - NEC_MAGIC, shift_cnt);
    shift_cnt = cookie_hi & 31;
    token    += ASMRotateLeftU32(cookie_lo + NEC_MAGIC, shift_cnt);
    shift_cnt = (cookie_lo >> 16) & 31;
    token    -= ASMRotateLeftU32(cookie_hi ^ NEC_MAGIC, shift_cnt);

    return ~token;
}

/**
 * Process a single command TRB and post completion information.
 */
static int xhciR3ExecuteCommand(PPDMDEVINS pDevIns, PXHCI pThis, PXHCICC pThisCC, XHCI_COMMAND_TRB *pCmd)
{
    XHCI_EVENT_TRB  ed;
    uint32_t        token;
    unsigned        slot;
    unsigned        cc;
    int             rc = VINF_SUCCESS;
    LogFlowFunc(("Executing command %u (%s) @ %RGp\n", pCmd->gen.type,
                 pCmd->gen.type < RT_ELEMENTS(g_apszTrbNames) ? g_apszTrbNames[pCmd->gen.type] : "WHAT?!!",
                 (RTGCPHYS)pThis->cmdr_dqp));

    switch (pCmd->gen.type)
    {
    case XHCI_TRB_NOOP_CMD:
        /* No-op, slot ID is always zero. */
        rc = xhciR3PostCmdCompletion(pDevIns, pThis, XHCI_TCC_SUCCESS, 0);
        pThis->cmdr_dqp += sizeof(XHCI_COMMAND_TRB);
        break;

    case XHCI_TRB_LINK:
        /* Link; set the dequeue pointer. CH bit is ignored. */
        Log(("Link: Ptr=%RGp IOC=%u TC=%u\n", pCmd->link.rseg_ptr, pCmd->link.ioc, pCmd->link.toggle));
        if (pCmd->link.ioc)     /* Command completion event is optional! */
            rc = xhciR3PostCmdCompletion(pDevIns, pThis, XHCI_TCC_SUCCESS, 0);
        /* Update the dequeue pointer and flip DCS if required. */
        pThis->cmdr_dqp = pCmd->link.rseg_ptr & XHCI_TRDP_ADDR_MASK;
        pThis->cmdr_ccs = pThis->cmdr_ccs ^ pCmd->link.toggle;
        break;

    case XHCI_TRB_ENB_SLOT:
        /* Look for an empty device slot. */
        for (slot = 0; slot < RT_ELEMENTS(pThis->aSlotState); ++slot)
        {
            if (pThis->aSlotState[slot] == XHCI_DEVSLOT_EMPTY)
            {
                /* Found a slot - transition to enabled state. */
                pThis->aSlotState[slot] = XHCI_DEVSLOT_ENABLED;
                break;
            }
        }
        Log(("Enable Slot: found slot ID %u\n", IDX_TO_ID(slot)));

        /* Post command completion event. */
        if (slot == RT_ELEMENTS(pThis->aSlotState))
            xhciR3PostCmdCompletion(pDevIns, pThis, XHCI_TCC_NO_SLOTS, 0);
        else
            xhciR3PostCmdCompletion(pDevIns, pThis, XHCI_TCC_SUCCESS, IDX_TO_ID(slot));

        pThis->cmdr_dqp += sizeof(XHCI_COMMAND_TRB);
        break;

    case XHCI_TRB_DIS_SLOT:
        /* Disable the given device slot. */
        Log(("Disable Slot: slot ID %u\n", pCmd->dsl.slot_id));
        cc = XHCI_TCC_SUCCESS;
        slot = ID_TO_IDX(pCmd->dsl.slot_id);
        if ((slot >= RT_ELEMENTS(pThis->aSlotState)) || (pThis->aSlotState[slot] == XHCI_DEVSLOT_EMPTY))
            cc = XHCI_TCC_SLOT_NOT_ENB;
        else
        {
            /// @todo set slot state of assoc. context to disabled
            pThis->aSlotState[slot] = XHCI_DEVSLOT_EMPTY;
        }
        xhciR3PostCmdCompletion(pDevIns, pThis, cc, pCmd->dsl.slot_id);
        pThis->cmdr_dqp += sizeof(XHCI_COMMAND_TRB);
        break;

    case XHCI_TRB_ADDR_DEV:
        /* Address a device. */
        Log(("Address Device: slot ID %u, BSR=%u\n", pCmd->adr.slot_id, pCmd->adr.bsr));
        slot = ID_TO_IDX(pCmd->cfg.slot_id);
        if ((slot >= RT_ELEMENTS(pThis->aSlotState)) || (pThis->aSlotState[slot] == XHCI_DEVSLOT_EMPTY))
            cc = XHCI_TCC_SLOT_NOT_ENB;
        else
            cc = xhciR3AddressDevice(pDevIns, pThis, pThisCC, pCmd->adr.ctx_ptr, pCmd->adr.slot_id, pCmd->adr.bsr);
        xhciR3PostCmdCompletion(pDevIns, pThis, cc, pCmd->adr.slot_id);
        pThis->cmdr_dqp += sizeof(XHCI_COMMAND_TRB);
        break;

    case XHCI_TRB_CFG_EP:
        /* Configure endpoint. */
        Log(("Configure endpoint: slot ID %u, DC=%u, Ctx @ %RGp\n", pCmd->cfg.slot_id, pCmd->cfg.dc, pCmd->cfg.ctx_ptr));
        slot = ID_TO_IDX(pCmd->cfg.slot_id);
        if ((slot >= RT_ELEMENTS(pThis->aSlotState)) || (pThis->aSlotState[slot] == XHCI_DEVSLOT_EMPTY))
            cc = XHCI_TCC_SLOT_NOT_ENB;
        else
            cc = xhciR3ConfigureDevice(pDevIns, pThis, pCmd->cfg.ctx_ptr, pCmd->cfg.slot_id, pCmd->cfg.dc);
        xhciR3PostCmdCompletion(pDevIns, pThis, cc, pCmd->cfg.slot_id);
        pThis->cmdr_dqp += sizeof(XHCI_COMMAND_TRB);
        break;

    case XHCI_TRB_EVAL_CTX:
        /* Evaluate context. */
        Log(("Evaluate context: slot ID %u, Ctx @ %RGp\n", pCmd->evc.slot_id, pCmd->evc.ctx_ptr));
        slot = ID_TO_IDX(pCmd->evc.slot_id);
        if ((slot >= RT_ELEMENTS(pThis->aSlotState)) || (pThis->aSlotState[slot] == XHCI_DEVSLOT_EMPTY))
            cc = XHCI_TCC_SLOT_NOT_ENB;
        else
            cc = xhciR3EvalContext(pDevIns, pThis, pCmd->evc.ctx_ptr, pCmd->evc.slot_id);
        xhciR3PostCmdCompletion(pDevIns, pThis, cc, pCmd->evc.slot_id);
        pThis->cmdr_dqp += sizeof(XHCI_COMMAND_TRB);
        break;

    case XHCI_TRB_RESET_EP:
        /* Reset the given endpoint. */
        Log(("Reset Endpoint: slot ID %u, EP ID %u, TSP=%u\n", pCmd->rse.slot_id, pCmd->rse.ep_id, pCmd->rse.tsp));
        cc = XHCI_TCC_SUCCESS;
        slot = ID_TO_IDX(pCmd->rse.slot_id);
        if ((slot >= RT_ELEMENTS(pThis->aSlotState)) || (pThis->aSlotState[slot] == XHCI_DEVSLOT_EMPTY))
            cc = XHCI_TCC_SLOT_NOT_ENB;
        else
            cc = xhciR3ResetEndpoint(pDevIns, pThis, pCmd->rse.slot_id, pCmd->rse.ep_id, pCmd->rse.tsp);
        xhciR3PostCmdCompletion(pDevIns, pThis, cc, pCmd->stp.slot_id);
        pThis->cmdr_dqp += sizeof(XHCI_COMMAND_TRB);
        break;

    case XHCI_TRB_STOP_EP:
        /* Stop the given endpoint. */
        Log(("Stop Endpoint: slot ID %u, EP ID %u, SP=%u\n", pCmd->stp.slot_id, pCmd->stp.ep_id, pCmd->stp.sp));
        cc = XHCI_TCC_SUCCESS;
        slot = ID_TO_IDX(pCmd->stp.slot_id);
        if ((slot >= RT_ELEMENTS(pThis->aSlotState)) || (pThis->aSlotState[slot] == XHCI_DEVSLOT_EMPTY))
            cc = XHCI_TCC_SLOT_NOT_ENB;
        else
            cc = xhciR3StopEndpoint(pDevIns, pThis, pThisCC, pCmd->stp.slot_id, pCmd->stp.ep_id, pCmd->stp.sp);
        xhciR3PostCmdCompletion(pDevIns, pThis, cc, pCmd->stp.slot_id);
        pThis->cmdr_dqp += sizeof(XHCI_COMMAND_TRB);
        break;

    case XHCI_TRB_SET_DEQ_PTR:
        /* Set TR Dequeue Pointer. */
        Log(("Set TRDP: slot ID %u, EP ID %u, TRDP=%RX64\n", pCmd->stdp.slot_id, pCmd->stdp.ep_id, pCmd->stdp.tr_dqp));
        cc = XHCI_TCC_SUCCESS;
        slot = ID_TO_IDX(pCmd->stdp.slot_id);
        if ((slot >= RT_ELEMENTS(pThis->aSlotState)) || (pThis->aSlotState[slot] == XHCI_DEVSLOT_EMPTY))
            cc = XHCI_TCC_SLOT_NOT_ENB;
        else
            cc = xhciR3SetTRDP(pDevIns, pThis, pCmd->stdp.slot_id, pCmd->stdp.ep_id, pCmd->stdp.tr_dqp);
        xhciR3PostCmdCompletion(pDevIns, pThis, cc, pCmd->stdp.slot_id);
        pThis->cmdr_dqp += sizeof(XHCI_COMMAND_TRB);
        break;

    case XHCI_TRB_RESET_DEV:
        /* Reset a device. */
        Log(("Reset Device: slot ID %u\n", pCmd->rsd.slot_id));
        cc = XHCI_TCC_SUCCESS;
        slot = ID_TO_IDX(pCmd->rsd.slot_id);
        if ((slot >= RT_ELEMENTS(pThis->aSlotState)) || (pThis->aSlotState[slot] == XHCI_DEVSLOT_EMPTY))
            cc = XHCI_TCC_SLOT_NOT_ENB;
        else
            cc = xhciR3ResetDevice(pDevIns, pThis, pCmd->rsd.slot_id);
        xhciR3PostCmdCompletion(pDevIns, pThis, cc, pCmd->rsd.slot_id);
        pThis->cmdr_dqp += sizeof(XHCI_COMMAND_TRB);
        break;

    case XHCI_TRB_GET_PORT_BW:
        /* Get port bandwidth. */
        Log(("Get Port Bandwidth: Dev Speed %u, Hub Slot ID %u, Context=%RX64\n", pCmd->gpbw.spd, pCmd->gpbw.slot_id, pCmd->gpbw.pbctx_ptr));
        cc = XHCI_TCC_SUCCESS;
        if (pCmd->gpbw.slot_id)
            cc = XHCI_TCC_PARM_ERR; /* Potential undefined behavior, see 4.6.15. */
        else
            cc = xhciR3GetPortBandwidth(pDevIns, pThis, pCmd->gpbw.spd, pCmd->gpbw.slot_id, pCmd->gpbw.pbctx_ptr);
        xhciR3PostCmdCompletion(pDevIns, pThis, cc, 0);
        pThis->cmdr_dqp += sizeof(XHCI_COMMAND_TRB);
        break;

    case NEC_TRB_GET_FW_VER:
        /* Get NEC firmware version. */
        Log(("Get NEC firmware version\n"));
        cc = XHCI_TCC_SUCCESS;

        RT_ZERO(ed);
        ed.nce.word1   = NEC_FW_REV;
        ed.nce.trb_ptr = pThis->cmdr_dqp;
        ed.nce.cc      = cc;
        ed.nce.type    = NEC_TRB_CMD_CMPL;

        xhciR3WriteEvent(pDevIns, pThis, &ed, XHCI_PRIMARY_INTERRUPTER, false);

        pThis->cmdr_dqp += sizeof(XHCI_COMMAND_TRB);
        break;

    case NEC_TRB_AUTHENTICATE:
        /* NEC authentication. */
        Log(("NEC authentication, cookie %RX64\n", pCmd->nac.cookie));
        cc = XHCI_TCC_SUCCESS;

        token = xhciR3NecAuthenticate(pCmd->nac.cookie);
        RT_ZERO(ed);
        ed.nce.word1   = RT_LOWORD(token);
        ed.nce.word2   = RT_HIWORD(token);
        ed.nce.trb_ptr = pThis->cmdr_dqp;
        ed.nce.cc      = cc;
        ed.nce.type    = NEC_TRB_CMD_CMPL;

        xhciR3WriteEvent(pDevIns, pThis, &ed, XHCI_PRIMARY_INTERRUPTER, false);

        pThis->cmdr_dqp += sizeof(XHCI_COMMAND_TRB);
        break;

    default:
        Log(("Unsupported command!\n"));
        pThis->cmdr_dqp += sizeof(XHCI_COMMAND_TRB);
        break;
    }

    return rc;
}


/**
 * Stop the Command Ring.
 */
static int xhciR3StopCommandRing(PPDMDEVINS pDevIns, PXHCI pThis)
{
    LogFlowFunc(("Command Ring stopping\n"));

    Assert(pThis->crcr & (XHCI_CRCR_CA | XHCI_CRCR_CS));
    Assert(pThis->crcr & XHCI_CRCR_CRR);
    ASMAtomicAndU64(&pThis->crcr, ~(XHCI_CRCR_CRR | XHCI_CRCR_CA | XHCI_CRCR_CS));
    return xhciR3PostCmdCompletion(pDevIns, pThis, XHCI_TCC_CMDR_STOPPED, 0);
}


/**
 * Process the xHCI command ring.
 */
static int xhciR3ProcessCommandRing(PPDMDEVINS pDevIns, PXHCI pThis, PXHCICC pThisCC)
{
    RTGCPHYS            GCPhysCmdTRB;
    XHCI_COMMAND_TRB    cmd;    /* Command Descriptor */
    unsigned            cCmds;

    Assert(pThis->crcr & XHCI_CRCR_CRR);
    LogFlowFunc(("Processing commands...\n"));

    for (cCmds = 0;; cCmds++)
    {
        /* First check if the xHC is running at all. */
        if (!(pThis->cmd & XHCI_CMD_RS))
        {
            /* Note that this will call xhciR3PostCmdCompletion() which will
             * end up doing nothing because R/S is clear.
             */
            xhciR3StopCommandRing(pDevIns, pThis);
            break;
        }

        /* Check if Command Ring was stopped in the meantime. */
        if (pThis->crcr & (XHCI_CRCR_CS | XHCI_CRCR_CA))
        {
            /* NB: We currently do not abort commands. If we did, we would
             * abort the currently running command and complete it with
             * the XHCI_TCC_CMD_ABORTED status.
             */
            xhciR3StopCommandRing(pDevIns, pThis);
            break;
        }

        /* Fetch the command TRB. */
        GCPhysCmdTRB = pThis->cmdr_dqp;
        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysCmdTRB, &cmd, sizeof(cmd));

        /* Make sure the Cycle State matches. */
        if ((bool)cmd.gen.cycle == pThis->cmdr_ccs)
            xhciR3ExecuteCommand(pDevIns, pThis, pThisCC, &cmd);
        else
        {
            Log(("Command Ring empty\n"));
            break;
        }

        /* Check if we're being fed suspiciously many commands. */
        if (cCmds > XHCI_MAX_NUM_CMDS)
        {
            /* Clear the R/S bit and any command ring running bits.
             * Note that the caller (xhciR3WorkerLoop) will set XHCI_STATUS_HCH.
             */
            ASMAtomicAndU32(&pThis->cmd, ~XHCI_CMD_RS);
            ASMAtomicAndU64(&pThis->crcr, ~(XHCI_CRCR_CRR | XHCI_CRCR_CA | XHCI_CRCR_CS));
            ASMAtomicOrU32(&pThis->status, XHCI_STATUS_HCE);
            LogRelMax(10, ("xHCI: Attempted to execute too many commands, stopping xHC!\n"));
            break;
        }
    }
    return VINF_SUCCESS;
}


/**
 * The xHCI asynchronous worker thread.
 *
 * @returns VBox status code.
 * @param   pDevIns     The xHCI device instance.
 * @param   pThread     The worker thread.
 */
static DECLCALLBACK(int) xhciR3WorkerLoop(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    PXHCI   pThis = PDMDEVINS_2_DATA(pDevIns, PXHCI);
    PXHCICC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PXHCICC);
    int     rc;

    LogFlow(("xHCI entering worker thread loop.\n"));
    if (pThread->enmState == PDMTHREADSTATE_INITIALIZING)
        return VINF_SUCCESS;

    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        uint8_t     uSlotID;

        bool fNotificationSent = ASMAtomicXchgBool(&pThis->fNotificationSent, false);
        if (!fNotificationSent)
        {
            rc = PDMDevHlpSUPSemEventWaitNoResume(pDevIns, pThis->hEvtProcess, RT_INDEFINITE_WAIT);
            AssertLogRelMsgReturn(RT_SUCCESS(rc) || rc == VERR_INTERRUPTED, ("%Rrc\n", rc), rc);
            if (RT_UNLIKELY(pThread->enmState != PDMTHREADSTATE_RUNNING))
                break;
            LogFlowFunc(("Woken up with rc=%Rrc\n", rc));
            ASMAtomicWriteBool(&pThis->fNotificationSent, false);
        }

        RTCritSectEnter(&pThisCC->CritSectThrd);

        if (pThis->crcr & XHCI_CRCR_CRR)
            xhciR3ProcessCommandRing(pDevIns, pThis, pThisCC);

        /* Run down the list of doorbells that are ringing. */
        for (uSlotID = 1; uSlotID < XHCI_NDS; ++uSlotID)
        {
            if (pThis->aSlotState[ID_TO_IDX(uSlotID)] >= XHCI_DEVSLOT_ENABLED)
            {
                while (pThis->aBellsRung[ID_TO_IDX(uSlotID)])
                {
                    uint8_t     bit;
                    uint32_t    uDBVal = 0;

                    for (bit = 0; bit < 32; ++bit)
                        if (pThis->aBellsRung[ID_TO_IDX(uSlotID)] & (1 << bit))
                        {
                            uDBVal = bit;
                            break;
                        }

                    Log2(("Stop ringing bell for slot %u, DCI %u\n", uSlotID, uDBVal));
                    ASMAtomicAndU32(&pThis->aBellsRung[ID_TO_IDX(uSlotID)], ~(1 << uDBVal));
                    xhciR3ProcessDevCtx(pDevIns, pThis, pThisCC, uSlotID, uDBVal);
                }
            }
        }

        /* If the R/S bit is no longer set, halt the xHC. */
        if (!(pThis->cmd & XHCI_CMD_RS))
        {
            Log(("R/S clear, halting the xHC.\n"));
            ASMAtomicOrU32(&pThis->status, XHCI_STATUS_HCH);
        }

        RTCritSectLeave(&pThisCC->CritSectThrd);
    } /* While running */

    LogFlow(("xHCI worker thread exiting.\n"));
    return VINF_SUCCESS;
}


/**
 * Unblock the worker thread so it can respond to a state change.
 *
 * @returns VBox status code.
 * @param   pDevIns     The xHCI device instance.
 * @param   pThread     The worker thread.
 */
static DECLCALLBACK(int) xhciR3WorkerWakeUp(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    NOREF(pThread);
    PXHCI pThis = PDMDEVINS_2_DATA(pDevIns, PXHCI);

    return PDMDevHlpSUPSemEventSignal(pDevIns, pThis->hEvtProcess);
}


/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) xhciR3RhQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PXHCIROOTHUBR3 pRh = RT_FROM_MEMBER(pInterface, XHCIROOTHUBR3, IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pRh->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, VUSBIROOTHUBPORT, &pRh->IRhPort);
    return NULL;
}

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) xhciR3QueryStatusInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PXHCIR3 pThisCC = RT_FROM_MEMBER(pInterface, XHCIR3, IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThisCC->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMILEDPORTS, &pThisCC->ILeds);
    return NULL;
}

/**
 * Gets the pointer to the status LED of a unit.
 *
 * @returns VBox status code.
 * @param   pInterface      Pointer to the interface structure containing the called function pointer.
 * @param   iLUN            The unit which status LED we desire.
 * @param   ppLed           Where to store the LED pointer.
 */
static DECLCALLBACK(int) xhciR3QueryStatusLed(PPDMILEDPORTS pInterface, unsigned iLUN, PPDMLED *ppLed)
{
    PXHCICC pThisCC = RT_FROM_MEMBER(pInterface, XHCIR3, ILeds);

    if (iLUN < XHCI_NUM_LUNS)
    {
        *ppLed = iLUN ? &pThisCC->RootHub3.Led : &pThisCC->RootHub2.Led;
        Assert((*ppLed)->u32Magic == PDMLED_MAGIC);
        return VINF_SUCCESS;
    }
    return VERR_PDM_LUN_NOT_FOUND;
}


/**
 * Get the number of ports available in the hub.
 *
 * @returns The number of ports available.
 * @param   pInterface      Pointer to this structure.
 * @param   pAvailable      Bitmap indicating the available ports. Set bit == available port.
 */
static DECLCALLBACK(unsigned) xhciR3RhGetAvailablePorts(PVUSBIROOTHUBPORT pInterface, PVUSBPORTBITMAP pAvailable)
{
    PXHCIROOTHUBR3  pRh = RT_FROM_MEMBER(pInterface, XHCIROOTHUBR3, IRhPort);
    PXHCICC         pThisCC = pRh->pXhciR3;
    PPDMDEVINS      pDevIns = pThisCC->pDevIns;
    unsigned        iPort;
    unsigned        cPorts = 0;
    LogFlow(("xhciR3RhGetAvailablePorts\n"));

    memset(pAvailable, 0, sizeof(*pAvailable));

    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->pCritSectRoR3, rcLock);

    for (iPort = pRh->uPortBase; iPort < (unsigned)pRh->uPortBase + pRh->cPortsImpl; iPort++)
    {
        Assert(iPort < XHCI_NDP_CFG(PDMDEVINS_2_DATA(pDevIns, PXHCI)));
        if (!pThisCC->aPorts[iPort].fAttached)
        {
            cPorts++;
            ASMBitSet(pAvailable, IDX_TO_ID(iPort - pRh->uPortBase));
        }
    }

    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);
    return cPorts;
}


/**
 * Get the supported USB versions for USB2 hubs.
 *
 * @returns The mask of supported USB versions.
 * @param   pInterface      Pointer to this structure.
 */
static DECLCALLBACK(uint32_t) xhciR3RhGetUSBVersions2(PVUSBIROOTHUBPORT pInterface)
{
    RT_NOREF(pInterface);
    return VUSB_STDVER_11 | VUSB_STDVER_20;
}


/**
 * Get the supported USB versions for USB2 hubs.
 *
 * @returns The mask of supported USB versions.
 * @param   pInterface      Pointer to this structure.
 */
static DECLCALLBACK(uint32_t) xhciR3RhGetUSBVersions3(PVUSBIROOTHUBPORT pInterface)
{
    RT_NOREF(pInterface);
    return VUSB_STDVER_30;
}


/**
 * Start sending SOF tokens across the USB bus, lists are processed in the
 * next frame.
 */
static void xhciR3BusStart(PPDMDEVINS pDevIns, PXHCI pThis, PXHCICC pThisCC)
{
    unsigned    iPort;

    pThisCC->RootHub2.pIRhConn->pfnPowerOn(pThisCC->RootHub2.pIRhConn);
    pThisCC->RootHub3.pIRhConn->pfnPowerOn(pThisCC->RootHub3.pIRhConn);
//    xhciR3BumpFrameNumber(pThis);

    Log(("xHCI: Bus started\n"));

    Assert(pThis->status & XHCI_STATUS_HCH);
    ASMAtomicAndU32(&pThis->status, ~XHCI_STATUS_HCH);

    /* HCH gates PSCEG (4.19.2). When clearing HCH, re-evaluate port changes. */
    for (iPort = 0; iPort < XHCI_NDP_CFG(pThis); ++iPort)
    {
        if (pThis->aPorts[iPort].portsc & XHCI_PORT_CHANGE_MASK)
            xhciR3GenPortChgEvent(pDevIns, pThis, IDX_TO_ID(iPort));
    }

    /// @todo record the starting time?
//    pThis->SofTime = TMTimerGet(pThis->CTX_SUFF(pEndOfFrameTimer)) - pThis->cTicksPerFrame;
}

/**
 * Stop sending SOF tokens on the bus and processing the data.
 */
static void xhciR3BusStop(PPDMDEVINS pDevIns, PXHCI pThis, PXHCICC pThisCC)
{
    LogFlow(("xhciR3BusStop\n"));

    /* Stop the controller and Command Ring. */
    pThis->cmd  &= ~XHCI_CMD_RS;
    pThis->crcr |= XHCI_CRCR_CS;

    /* Power off the root hubs. */
    pThisCC->RootHub2.pIRhConn->pfnPowerOff(pThisCC->RootHub2.pIRhConn);
    pThisCC->RootHub3.pIRhConn->pfnPowerOff(pThisCC->RootHub3.pIRhConn);

    /* The worker thread will halt the HC (set HCH) when done. */
    xhciKickWorker(pDevIns, pThis, XHCI_JOB_PROCESS_CMDRING, 0);
}


/**
 * Power a port up or down
 */
static void xhciR3PortPower(PXHCI pThis, PXHCICC pThisCC, unsigned iPort, bool fPowerUp)
{
    PXHCIHUBPORT    pPort = &pThis->aPorts[iPort];
    PXHCIHUBPORTR3  pPortR3 = &pThisCC->aPorts[iPort];
    PXHCIROOTHUBR3  pRh = GET_PORT_PRH(pThisCC, iPort);

    bool            fOldPPS = !!(pPort->portsc & XHCI_PORT_PP);
    LogFlow(("xhciR3PortPower (port %u) %s\n", IDX_TO_ID(iPort), fPowerUp ? "UP" : "DOWN"));

    if (fPowerUp)
    {
        /* Power up a port. */
        if (pPortR3->fAttached)
            ASMAtomicOrU32(&pPort->portsc, XHCI_PORT_CCS);
        if (pPort->portsc & XHCI_PORT_CCS)
            ASMAtomicOrU32(&pPort->portsc, XHCI_PORT_PP);
        if (pPortR3->fAttached && !fOldPPS)
            VUSBIRhDevPowerOn(pRh->pIRhConn, GET_VUSB_PORT_FROM_XHCI_PORT(pRh, iPort));
    }
    else
    {
        /* Power down. */
        ASMAtomicAndU32(&pPort->portsc, ~(XHCI_PORT_PP | XHCI_PORT_CCS));
        if (pPortR3->fAttached && fOldPPS)
            VUSBIRhDevPowerOff(pRh->pIRhConn, GET_VUSB_PORT_FROM_XHCI_PORT(pRh, iPort));
    }
}


/**
 * Port reset done callback.
 *
 * @param   pDevIns             The device instance data.
 * @param   iPort               The XHCI port index of the port being resetted.
 */
static void xhciR3PortResetDone(PPDMDEVINS pDevIns, unsigned iPort)
{
    PXHCI       pThis   = PDMDEVINS_2_DATA(pDevIns, PXHCI);

    Log2(("xhciR3PortResetDone\n"));

    AssertReturnVoid(iPort < XHCI_NDP_CFG(pThis));

    /*
     * Successful reset.
     */
    Log2(("xhciR3PortResetDone: Reset completed.\n"));

    uint32_t fChangeMask = XHCI_PORT_PED | XHCI_PORT_PRC;
    /* For USB2 ports, transition the link state. */
    if (!IS_USB3_PORT_IDX_SHR(pThis, iPort))
    {
        pThis->aPorts[iPort].portsc &= ~XHCI_PORT_PLS_MASK;
        pThis->aPorts[iPort].portsc |= XHCI_PLS_U0 << XHCI_PORT_PLS_SHIFT;
    }
    else
    {
        if (pThis->aPorts[iPort].portsc & XHCI_PORT_WPR)
            fChangeMask |= XHCI_PORT_WRC;
    }

    ASMAtomicAndU32(&pThis->aPorts[iPort].portsc, ~(XHCI_PORT_PR | XHCI_PORT_WPR));
    ASMAtomicOrU32(&pThis->aPorts[iPort].portsc, fChangeMask);
    /// @todo Set USBSTS.PCD and manage PSCEG correctly!
    /// @todo just guessing?!
//        ASMAtomicOrU32(&pThis->aPorts[iPort].portsc, XHCI_PORT_CSC | XHCI_PORT_PLC);

    /// @todo Is this the right place?
    xhciR3GenPortChgEvent(pDevIns, pThis, IDX_TO_ID(iPort));
}


/**
 * Sets a flag in a port status register, but only if a device is connected;
 * if not, set ConnectStatusChange flag to force HCD to reevaluate connect status.
 *
 * @returns true if device was connected and the flag was cleared.
 */
static bool xhciR3RhPortSetIfConnected(PXHCI pThis, int iPort, uint32_t fValue)
{
    /*
     * Writing a 0 has no effect
     */
    if (fValue == 0)
        return false;

    /*
     * The port might be still/already disconnected.
     */
    if (!(pThis->aPorts[iPort].portsc & XHCI_PORT_CCS))
        return false;

    bool fRc = !(pThis->aPorts[iPort].portsc & fValue);

    /* Set the bit. */
    ASMAtomicOrU32(&pThis->aPorts[iPort].portsc, fValue);

    return fRc;
}


/** Translate VUSB speed enum to xHCI definition. */
static unsigned xhciR3UsbSpd2XhciSpd(VUSBSPEED enmSpeed)
{
    unsigned    uSpd;

    switch (enmSpeed)
    {
    default:                AssertMsgFailed(("%d\n", enmSpeed));
        RT_FALL_THRU();
    case VUSB_SPEED_LOW:    uSpd = XHCI_SPD_LOW;    break;
    case VUSB_SPEED_FULL:   uSpd = XHCI_SPD_FULL;   break;
    case VUSB_SPEED_HIGH:   uSpd = XHCI_SPD_HIGH;   break;
    case VUSB_SPEED_SUPER:  uSpd = XHCI_SPD_SUPER;  break;
    }
    return uSpd;
}

/** @interface_method_impl{VUSBIROOTHUBPORT,pfnAttach} */
static DECLCALLBACK(int) xhciR3RhAttach(PVUSBIROOTHUBPORT pInterface, unsigned uPort, VUSBSPEED enmSpeed)
{
    PXHCIROOTHUBR3  pRh = RT_FROM_MEMBER(pInterface, XHCIROOTHUBR3, IRhPort);
    PXHCICC         pThisCC = pRh->pXhciR3;
    PPDMDEVINS      pDevIns = pThisCC->pDevIns;
    PXHCI           pThis = PDMDEVINS_2_DATA(pDevIns, PXHCI);
    PXHCIHUBPORT    pPort;
    unsigned        iPort;
    LogFlow(("xhciR3RhAttach: uPort=%u (iPort=%u)\n", uPort, ID_TO_IDX(uPort) + pRh->uPortBase));

    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
    AssertRCReturn(rcLock, rcLock);

    /*
     * Validate and adjust input.
     */
    Assert(uPort >= 1 && uPort <= pRh->cPortsImpl);
    iPort = ID_TO_IDX(uPort) + pRh->uPortBase;
    Assert(iPort < XHCI_NDP_CFG(pThis));
    pPort = &pThis->aPorts[iPort];
    Assert(!pThisCC->aPorts[iPort].fAttached);
    Assert(enmSpeed != VUSB_SPEED_UNKNOWN);

    /*
     * Attach it.
     */
    ASMAtomicOrU32(&pPort->portsc, XHCI_PORT_CCS | XHCI_PORT_CSC);
    pThisCC->aPorts[iPort].fAttached = true;
    xhciR3PortPower(pThis, pThisCC, iPort, 1 /* power on */);

    /* USB3 ports automatically transition to Enabled state. */
    if (IS_USB3_PORT_IDX_R3(pThisCC, iPort))
    {
        Assert(enmSpeed == VUSB_SPEED_SUPER);
        pPort->portsc |= XHCI_PORT_PED;
        pPort->portsc &= ~XHCI_PORT_PLS_MASK;
        pPort->portsc |= XHCI_PLS_U0 << XHCI_PORT_PLS_SHIFT;
        pPort->portsc &= ~XHCI_PORT_SPD_MASK;
        pPort->portsc |= XHCI_SPD_SUPER << XHCI_PORT_SPD_SHIFT;
        VUSBIRhDevReset(pRh->pIRhConn, GET_VUSB_PORT_FROM_XHCI_PORT(pRh, iPort),
                        false, NULL /* sync */, NULL, PDMDevHlpGetVM(pDevIns));
    }
    else
    {
        Assert(enmSpeed == VUSB_SPEED_LOW || enmSpeed == VUSB_SPEED_FULL || enmSpeed == VUSB_SPEED_HIGH);
        pPort->portsc &= ~XHCI_PORT_SPD_MASK;
        pPort->portsc |= xhciR3UsbSpd2XhciSpd(enmSpeed) << XHCI_PORT_SPD_SHIFT;
    }

    xhciR3GenPortChgEvent(pDevIns, pThis, IDX_TO_ID(iPort));

    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);
    return VINF_SUCCESS;
}


/**
 * A device is being detached from a port in the root hub.
 *
 * @param   pInterface      Pointer to this structure.
 * @param   uPort           The 1-based port number assigned to the device.
 */
static DECLCALLBACK(void) xhciR3RhDetach(PVUSBIROOTHUBPORT pInterface, unsigned uPort)
{
    PXHCIROOTHUBR3  pRh = RT_FROM_MEMBER(pInterface, XHCIROOTHUBR3, IRhPort);
    PXHCICC         pThisCC = pRh->pXhciR3;
    PPDMDEVINS      pDevIns = pThisCC->pDevIns;
    PXHCI           pThis = PDMDEVINS_2_DATA(pDevIns, PXHCI);
    PXHCIHUBPORT    pPort;
    unsigned        iPort;
    LogFlow(("xhciR3RhDetach: uPort=%u iPort=%u\n", uPort, ID_TO_IDX(uPort) + pRh->uPortBase));
    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->pCritSectRoR3, rcLock);

    /*
     * Validate and adjust input.
     */
    Assert(uPort >= 1 && uPort <= pRh->cPortsImpl);
    iPort = ID_TO_IDX(uPort) + pRh->uPortBase;
    Assert(iPort < XHCI_NDP_CFG(pThis));
    pPort = &pThis->aPorts[iPort];
    Assert(pThisCC->aPorts[iPort].fAttached);

    /*
     * Detach it.
     */
    pThisCC->aPorts[iPort].fAttached = false;
    ASMAtomicAndU32(&pPort->portsc, ~(XHCI_PORT_CCS | XHCI_PORT_SPD_MASK | XHCI_PORT_PLS_MASK));
    ASMAtomicOrU32(&pPort->portsc, XHCI_PORT_CSC);
    /* Link state goes to RxDetect. */
    ASMAtomicOrU32(&pPort->portsc, XHCI_PLS_RXDETECT << XHCI_PORT_PLS_SHIFT);
    /* Disconnect clears the port enable bit. */
    if (pPort->portsc & XHCI_PORT_PED)
        ASMAtomicAndU32(&pPort->portsc, ~XHCI_PORT_PED);

    xhciR3GenPortChgEvent(pDevIns, pThis, IDX_TO_ID(iPort));

    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);
}


/**
 * One of the root hub devices has completed its reset
 * operation.
 *
 * Currently, we don't think anything is required to be done here
 * so it's just a stub for forcing async resetting of the devices
 * during a root hub reset.
 *
 * @param pDev      The root hub device.
 * @param rc        The result of the operation.
 * @param uPort     The port number of the device on the roothub being resetted.
 * @param pvUser    Pointer to the controller.
 */
static DECLCALLBACK(void) xhciR3RhResetDoneOneDev(PVUSBIDEVICE pDev, uint32_t uPort, int rc, void *pvUser)
{
    LogRel(("xHCI: Root hub-attached device reset completed with %Rrc\n", rc));
    RT_NOREF(pDev, uPort, rc, pvUser);
}


/**
 * Does a software or hardware reset of the controller.
 *
 * This is called in response to setting HcCommandStatus.HCR, hardware reset,
 * and device construction.
 *
 * @param   pThis        The shared XHCI instance data
 * @param   pThisCC      The ring-3 XHCI instance data
 * @param   fNewMode     The new mode of operation. This is UsbSuspend if
 *                       it's a software reset, and UsbReset if it's a
 *                       hardware reset / cold boot.
 * @param   fTrueReset   Set if we can do a real reset of the devices
 *                       attached to the root hub. This is really a just a
 *                       hack for the non-working linux device reset. Linux
 *                       has this feature called 'logical disconnect' if
 *                       device reset fails which prevents us from doing
 *                       resets when the guest asks for it - the guest will
 *                       get confused when the device seems to be
 *                       reconnected everytime it tries to reset it. But if
 *                       we're at hardware reset time, we can allow a device
 *                       to be 'reconnected' without upsetting the guest.
 *
 * @remark  This has nothing to do with software setting the
 *          mode to UsbReset.
 */
static void xhciR3DoReset(PXHCI pThis, PXHCICC pThisCC, uint32_t fNewMode, bool fTrueReset)
{
    LogFunc(("%s reset%s\n", fNewMode == XHCI_USB_RESET ? "Hardware" : "Software",
             fTrueReset ? " (really reset devices)" : ""));

    /*
     * Cancel all outstanding URBs.
     *
     * We can't, and won't, deal with URBs until we're moved out of the
     * suspend/reset state. Also, a real HC isn't going to send anything
     * any more when a reset has been signaled.
     */
    pThisCC->RootHub2.pIRhConn->pfnCancelAllUrbs(pThisCC->RootHub2.pIRhConn);
    pThisCC->RootHub3.pIRhConn->pfnCancelAllUrbs(pThisCC->RootHub3.pIRhConn);

    /*
     * Reset the hardware registers.
     */
    /** @todo other differences between hardware reset and VM reset? */

    pThis->cmd       = 0;
    pThis->status    = XHCI_STATUS_HCH;
    pThis->dnctrl    = 0;
    pThis->crcr      = 0;
    pThis->dcbaap    = 0;
    pThis->config    = 0;

    /*
     * Reset the internal state.
     */
    pThis->cmdr_dqp  = 0;
    pThis->cmdr_ccs  = 0;

    RT_ZERO(pThis->aSlotState);
    RT_ZERO(pThis->aBellsRung);

    /* Zap everything but the lock. */
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aInterrupters); ++i)
    {
        pThis->aInterrupters[i].iman      = 0;
        pThis->aInterrupters[i].imod      = 0;
        pThis->aInterrupters[i].erstsz    = 0;
        pThis->aInterrupters[i].erstba    = 0;
        pThis->aInterrupters[i].erdp      = 0;
        pThis->aInterrupters[i].erep      = 0;
        pThis->aInterrupters[i].erst_idx  = 0;
        pThis->aInterrupters[i].trb_count = 0;
        pThis->aInterrupters[i].evtr_pcs  = false;
        pThis->aInterrupters[i].ipe       = false;
    }

    if (fNewMode == XHCI_USB_RESET)
    {
        /* Only a hardware reset reinits the port registers. */
        for (unsigned i = 0; i < XHCI_NDP_CFG(pThis); i++)
        {
            /* Need to preserve the speed of attached devices. */
            pThis->aPorts[i].portsc &= XHCI_PORT_SPD_MASK;
            pThis->aPorts[i].portsc |= XHCI_PLS_RXDETECT << XHCI_PORT_PLS_SHIFT;
            /* If Port Power Control is not supported, ports are always powered on. */
            if (!(pThis->hcc_params & XHCI_HCC_PPC))
                pThis->aPorts[i].portsc |= XHCI_PORT_PP;
        }
    }

    /*
     * If this is a hardware reset, we will initialize the root hub too.
     * Software resets doesn't do this according to the specs.
     * (It's not possible to have a device connected at the time of the
     * device construction, so nothing to worry about there.)
     */
    if (fNewMode == XHCI_USB_RESET)
    {
        pThisCC->RootHub2.pIRhConn->pfnReset(pThisCC->RootHub2.pIRhConn, fTrueReset);
        pThisCC->RootHub3.pIRhConn->pfnReset(pThisCC->RootHub3.pIRhConn, fTrueReset);

        /*
         * Reattach the devices.
         */
        for (unsigned i = 0; i < XHCI_NDP_CFG(pThis); i++)
        {
            bool fAttached = pThisCC->aPorts[i].fAttached;
            PXHCIROOTHUBR3 pRh = GET_PORT_PRH(pThisCC, i);
            pThisCC->aPorts[i].fAttached = false;

            if (fAttached)
            {
                VUSBSPEED enmSpeed = VUSBIRhDevGetSpeed(pRh->pIRhConn, GET_VUSB_PORT_FROM_XHCI_PORT(pRh, i));
                xhciR3RhAttach(&pRh->IRhPort, GET_VUSB_PORT_FROM_XHCI_PORT(pRh, i), enmSpeed);
            }
        }
    }
}

/**
 * Reset the root hub.
 *
 * @returns VBox status code.
 * @param   pInterface  Pointer to this structure.
 * @param   fTrueReset  This is used to indicate whether we're at VM reset
 *                      time and can do real resets or if we're at any other
 *                      time where that isn't such a good idea.
 * @remark  Do NOT call VUSBIDevReset on the root hub in an async fashion!
 * @thread  EMT
 */
static DECLCALLBACK(int) xhciR3RhReset(PVUSBIROOTHUBPORT pInterface, bool fTrueReset)
{
    PXHCIROOTHUBR3  pRh = RT_FROM_MEMBER(pInterface, XHCIROOTHUBR3, IRhPort);
    PXHCICC         pThisCC = pRh->pXhciR3;
    PPDMDEVINS      pDevIns = pThisCC->pDevIns;
    PXHCI           pThis = PDMDEVINS_2_DATA(pDevIns, PXHCI);

    Log(("xhciR3RhReset fTrueReset=%d\n", fTrueReset));
    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
    AssertRCReturn(rcLock, rcLock);

    /* Soft reset first */
    xhciR3DoReset(pThis, pThisCC, XHCI_USB_SUSPEND, false /* N/A */);

    /*
     * We're pretending to _reattach_ the devices without resetting them.
     * Except, during VM reset where we use the opportunity to do a proper
     * reset before the guest comes along and expects things.
     *
     * However, it's very very likely that we're not doing the right thing
     * here when end up here on request from the guest (USB Reset state).
     * The docs talk about root hub resetting, however what exact behaviour
     * in terms of root hub status and changed bits, and HC interrupts aren't
     * stated clearly. IF we get trouble and see the guest doing "USB Resets"
     * we will have to look into this. For the time being we stick with simple.
     */
    for (unsigned iPort = pRh->uPortBase; iPort < XHCI_NDP_CFG(pThis); iPort++)
    {
        if (pThisCC->aPorts[iPort].fAttached)
        {
            ASMAtomicOrU32(&pThis->aPorts[iPort].portsc, XHCI_PORT_CCS | XHCI_PORT_CSC);
            if (fTrueReset)
                VUSBIRhDevReset(pRh->pIRhConn, GET_VUSB_PORT_FROM_XHCI_PORT(pRh, iPort), fTrueReset,
                                xhciR3RhResetDoneOneDev, pDevIns, PDMDevHlpGetVM(pDevIns));
        }
    }

    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);
    return VINF_SUCCESS;
}

#endif /* IN_RING3 */



/* -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- */
/*                xHCI Operational Register access routines                    */
/* -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- */



/**
 * Read the USBCMD register of the host controller.
 */
static VBOXSTRICTRC HcUsbcmd_r(PPDMDEVINS pDevIns, PXHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, iReg);
    STAM_COUNTER_INC(&pThis->StatRdUsbCmd);
    *pu32Value = pThis->cmd;
    return VINF_SUCCESS;
}

/**
 * Write to the USBCMD register of the host controller.
 */
static VBOXSTRICTRC HcUsbcmd_w(PPDMDEVINS pDevIns, PXHCI pThis, uint32_t iReg, uint32_t val)
{
#ifdef IN_RING3
    PXHCICC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PXHCICC);
#endif
    RT_NOREF(iReg);
    STAM_COUNTER_INC(&pThis->StatWrUsbCmd);
#ifdef LOG_ENABLED
    Log(("HcUsbcmd_w old=%x new=%x\n", pThis->cmd, val));
    if (val & XHCI_CMD_RS)
        Log(("    XHCI_CMD_RS\n"));
    if (val & XHCI_CMD_HCRST)
        Log(("    XHCI_CMD_HCRST\n"));
    if (val & XHCI_CMD_INTE )
        Log(("    XHCI_CMD_INTE\n"));
    if (val & XHCI_CMD_HSEE)
        Log(("    XHCI_CMD_HSEE\n"));
    if (val & XHCI_CMD_LCRST)
        Log(("    XHCI_CMD_LCRST\n"));
    if (val & XHCI_CMD_CSS)
        Log(("    XHCI_CMD_CSS\n"));
    if (val & XHCI_CMD_CRS)
        Log(("    XHCI_CMD_CRS\n"));
    if (val & XHCI_CMD_EWE)
        Log(("    XHCI_CMD_EWE\n"));
    if (val & XHCI_CMD_EU3S)
        Log(("    XHCI_CMD_EU3S\n"));
#endif

    if (val & ~XHCI_CMD_MASK)
        Log(("Unknown USBCMD bits %#x are set!\n", val & ~XHCI_CMD_MASK));

    uint32_t old_cmd = pThis->cmd;
#ifdef IN_RING3
    pThis->cmd = val;
#endif

    if (val & XHCI_CMD_HCRST)
    {
#ifdef IN_RING3
        LogRel(("xHCI: Hardware reset\n"));
        xhciR3DoReset(pThis, pThisCC, XHCI_USB_RESET, true /* reset devices */);
#else
        return VINF_IOM_R3_MMIO_WRITE;
#endif
    }
    else if (val & XHCI_CMD_LCRST)
    {
#ifdef IN_RING3
        LogRel(("xHCI: Software reset\n"));
        xhciR3DoReset(pThis, pThisCC, XHCI_USB_SUSPEND, false /* N/A */);
#else
        return VINF_IOM_R3_MMIO_WRITE;
#endif
    }
    else if (pThis->status & XHCI_STATUS_HCE)
    {
        /* If HCE is set, don't restart the controller. Only a reset
         * will clear the HCE bit.
         */
        Log(("xHCI: HCE bit set, ignoring USBCMD register changes!\n"));
        pThis->cmd = old_cmd;
        return VINF_SUCCESS;
    }
    else
    {
        /* See what changed and take action on that. First the R/S bit. */
        uint32_t old_state = old_cmd & XHCI_CMD_RS;
        uint32_t new_state = val     & XHCI_CMD_RS;

        if (old_state != new_state)
        {
#ifdef IN_RING3
            switch (new_state)
            {
                case XHCI_CMD_RS:
                    LogRel(("xHCI: USB Operational\n"));
                    xhciR3BusStart(pDevIns, pThis, pThisCC);
                    break;
                case 0:
                    xhciR3BusStop(pDevIns, pThis, pThisCC);
                    LogRel(("xHCI: USB Suspended\n"));
                    break;
            }
#else
            return VINF_IOM_R3_MMIO_WRITE;
#endif
        }

        /* Check EWE (Enable MFINDEX Wraparound Event) changes. */
        old_state = old_cmd & XHCI_CMD_EWE;
        new_state = val     & XHCI_CMD_EWE;

        if (old_state != new_state)
        {
            switch (new_state)
            {
                case XHCI_CMD_EWE:
                    Log(("xHCI: MFINDEX Wrap timer started\n"));
                    xhciSetWrapTimer(pDevIns, pThis);
                    break;
                case 0:
                    PDMDevHlpTimerStop(pDevIns, pThis->hWrapTimer);
                    Log(("xHCI: MFINDEX Wrap timer stopped\n"));
                    break;
            }
        }

        /* INTE transitions need to twiddle interrupts. */
        old_state = old_cmd & XHCI_CMD_INTE;
        new_state = val     & XHCI_CMD_INTE;
        if (old_state != new_state)
        {
            switch (new_state)
            {
                case XHCI_CMD_INTE:
                    /* Check whether the event interrupt bit is set and trigger an interrupt. */
                    if (pThis->status & XHCI_STATUS_EINT)
                        PDMDevHlpPCISetIrq(pDevIns, 0, PDM_IRQ_LEVEL_HIGH);
                    break;
                case 0:
                    PDMDevHlpPCISetIrq(pDevIns, 0, PDM_IRQ_LEVEL_LOW);
                    break;
            }
        }

        /* We currently do nothing for state save/restore. If we did, the CSS/CRS command bits
         * would set the SSS/RSS status bits until the operation is done. The CSS/CRS bits are
         * never read as one.
         */
        /// @todo 4.9.4 describes internal state that needs to be saved/restored:
        /// ERSTE, ERST Count, EREP, and TRB Count
        /// Command Ring Dequeue Pointer?
        if (val & XHCI_CMD_CSS)
        {
            Log(("xHCI: Save State requested\n"));
            val &= ~XHCI_CMD_CSS;
        }

        if (val & XHCI_CMD_CRS)
        {
            Log(("xHCI: Restore State requested\n"));
            val &= ~XHCI_CMD_CRS;
        }
    }
#ifndef IN_RING3
    pThis->cmd = val;
#endif
    return VINF_SUCCESS;
}

#ifdef LOG_ENABLED
static void HcUsbstsLogBits(uint32_t val)
{
    if (val & XHCI_STATUS_HCH)
        Log(("    XHCI_STATUS_HCH (HC Halted)\n"));
    if (val & XHCI_STATUS_HSE)
        Log(("    XHCI_STATUS_HSE (Host System Error)\n"));
    if (val & XHCI_STATUS_EINT)
        Log(("    XHCI_STATUS_EINT (Event Interrupt)\n"));
    if (val & XHCI_STATUS_PCD)
        Log(("    XHCI_STATUS_PCD (Port Change Detect)\n"));
    if (val & XHCI_STATUS_SSS)
        Log(("    XHCI_STATUS_SSS (Save State Status)\n"));
    if (val & XHCI_STATUS_RSS)
        Log(("    XHCI_STATUS_RSS (Restore State Status)\n"));
    if (val & XHCI_STATUS_SRE)
        Log(("    XHCI_STATUS_SRE (Save/Restore Error)\n"));
    if (val & XHCI_STATUS_CNR)
        Log(("    XHCI_STATUS_CNR (Controller Not Ready)\n"));
    if (val & XHCI_STATUS_HCE)
        Log(("    XHCI_STATUS_HCE (Host Controller Error)\n"));
}
#endif

/**
 * Read the USBSTS register of the host controller.
 */
static VBOXSTRICTRC HcUsbsts_r(PPDMDEVINS pDevIns, PXHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
#ifdef LOG_ENABLED
    Log(("HcUsbsts_r current value %x\n", pThis->status));
    HcUsbstsLogBits(pThis->status);
#endif
    RT_NOREF(pDevIns, iReg);
    STAM_COUNTER_INC(&pThis->StatRdUsbSts);

    *pu32Value = pThis->status;
    return VINF_SUCCESS;
}

/**
 * Write to the USBSTS register of the host controller.
 */
static VBOXSTRICTRC HcUsbsts_w(PPDMDEVINS pDevIns, PXHCI pThis, uint32_t iReg, uint32_t val)
{
#ifdef LOG_ENABLED
    Log(("HcUsbsts_w current value %x; new %x\n", pThis->status, val));
    HcUsbstsLogBits(val);
#endif
    RT_NOREF(pDevIns, iReg);
    STAM_COUNTER_INC(&pThis->StatWrUsbSts);

    if (    (val & ~XHCI_STATUS_WRMASK)
        &&  val != 0xffffffff   /* Ignore clear-all-like requests. */)
        Log(("Unknown USBSTS bits %#x are set!\n", val & ~XHCI_STATUS_WRMASK));

    /* Most bits are read-only. */
    val &= XHCI_STATUS_WRMASK;

    /* "The Host Controller Driver may clear specific bits in this
     * register by writing '1' to bit positions to be cleared"
     */
    ASMAtomicAndU32(&pThis->status, ~val);

    return VINF_SUCCESS;
}

/**
 * Read the PAGESIZE register of the host controller.
 */
static VBOXSTRICTRC HcPagesize_r(PPDMDEVINS pDevIns, PXHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis, iReg);
    STAM_COUNTER_INC(&pThis->StatRdPageSize);
    *pu32Value = 1; /* 2^(bit n + 12) -> 4K page size only. */
    return VINF_SUCCESS;
}

/**
 * Read the DNCTRL (Device Notification Control) register.
 */
static VBOXSTRICTRC HcDevNotifyCtrl_r(PPDMDEVINS pDevIns, PXHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, iReg);
    STAM_COUNTER_INC(&pThis->StatRdDevNotifyCtrl);
    *pu32Value = pThis->dnctrl;
    return VINF_SUCCESS;
}

/**
 * Write the DNCTRL (Device Notification Control) register.
 */
static VBOXSTRICTRC HcDevNotifyCtrl_w(PPDMDEVINS pDevIns, PXHCI pThis, uint32_t iReg, uint32_t val)
{
    RT_NOREF(pDevIns, iReg);
    STAM_COUNTER_INC(&pThis->StatWrDevNotifyCtrl);
    pThis->dnctrl = val;
    return VINF_SUCCESS;
}

/**
 * Read the low dword of CRCR (Command Ring Control) register.
 */
static VBOXSTRICTRC HcCmdRingCtlLo_r(PPDMDEVINS pDevIns, PXHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, iReg);
    STAM_COUNTER_INC(&pThis->StatRdCmdRingCtlLo);
    *pu32Value = (uint32_t)(pThis->crcr & XHCI_CRCR_RD_MASK);
    return VINF_SUCCESS;
}

/**
 * Write the low dword of CRCR (Command Ring Control) register.
 */
static VBOXSTRICTRC HcCmdRingCtlLo_w(PPDMDEVINS pDevIns, PXHCI pThis, uint32_t iReg, uint32_t val)
{
    RT_NOREF(iReg);
    STAM_COUNTER_INC(&pThis->StatWrCmdRingCtlLo);
    /* NB: A dword write to the low half clears the high half. */

    /* Sticky Abort/Stop bits - update register and kick the worker thread. */
    if (val & (XHCI_CRCR_CA | XHCI_CRCR_CS))
    {
        pThis->crcr |= val & (XHCI_CRCR_CA | XHCI_CRCR_CS);
        xhciKickWorker(pDevIns, pThis, XHCI_JOB_PROCESS_CMDRING, 0);
    }

    /*
     * If the command ring is not running, the internal dequeue pointer
     * and the cycle state is updated. Otherwise the update is ignored.
     */
    if (!(pThis->crcr & XHCI_CRCR_CRR))
    {
        pThis->crcr     = (pThis->crcr & ~XHCI_CRCR_UPD_MASK) | (val & XHCI_CRCR_UPD_MASK);
        /// @todo cmdr_dqp: atomic? volatile?
        pThis->cmdr_dqp = pThis->crcr & XHCI_CRCR_ADDR_MASK;
        pThis->cmdr_ccs = pThis->crcr & XHCI_CRCR_RCS;
    }

    return VINF_SUCCESS;
}

/**
 * Read the high dword of CRCR (Command Ring Control) register.
 */
static VBOXSTRICTRC HcCmdRingCtlHi_r(PPDMDEVINS pDevIns, PXHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, iReg);
    STAM_COUNTER_INC(&pThis->StatRdCmdRingCtlHi);
    *pu32Value = pThis->crcr >> 32;
    return VINF_SUCCESS;
}

/**
 * Write the high dword of CRCR (Command Ring Control) register.
 */
static VBOXSTRICTRC HcCmdRingCtlHi_w(PPDMDEVINS pDevIns, PXHCI pThis, uint32_t iReg, uint32_t val)
{
    RT_NOREF(pDevIns, iReg);
    STAM_COUNTER_INC(&pThis->StatWrCmdRingCtlHi);
    if (!(pThis->crcr & XHCI_CRCR_CRR))
    {
        pThis->crcr     = ((uint64_t)val << 32) | (uint32_t)pThis->crcr;
        pThis->cmdr_dqp = pThis->crcr & XHCI_CRCR_ADDR_MASK;
    }
    return VINF_SUCCESS;
}

/**
 * Read the low dword of the DCBAAP register.
 */
static VBOXSTRICTRC HcDevCtxBAAPLo_r(PPDMDEVINS pDevIns, PXHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, iReg);
    STAM_COUNTER_INC(&pThis->StatRdDevCtxBaapLo);
    *pu32Value = (uint32_t)pThis->dcbaap;
    return VINF_SUCCESS;
}

/**
 * Write the low dword of the DCBAAP register.
 */
static VBOXSTRICTRC HcDevCtxBAAPLo_w(PPDMDEVINS pDevIns, PXHCI pThis, uint32_t iReg, uint32_t val)
{
    RT_NOREF(pDevIns, iReg);
    STAM_COUNTER_INC(&pThis->StatWrDevCtxBaapLo);
    /* NB: A dword write to the low half clears the high half. */
    /// @todo Should this mask off the reserved bits?
    pThis->dcbaap = val;
    return VINF_SUCCESS;
}

/**
 * Read the high dword of the DCBAAP register.
 */
static VBOXSTRICTRC HcDevCtxBAAPHi_r(PPDMDEVINS pDevIns, PXHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, iReg);
    STAM_COUNTER_INC(&pThis->StatRdDevCtxBaapHi);
    *pu32Value = pThis->dcbaap >> 32;
    return VINF_SUCCESS;
}

/**
 * Write the high dword of the DCBAAP register.
 */
static VBOXSTRICTRC HcDevCtxBAAPHi_w(PPDMDEVINS pDevIns, PXHCI pThis, uint32_t iReg, uint32_t val)
{
    RT_NOREF(pDevIns, iReg);
    STAM_COUNTER_INC(&pThis->StatWrDevCtxBaapHi);
    pThis->dcbaap = ((uint64_t)val << 32) | (uint32_t)pThis->dcbaap;
    return VINF_SUCCESS;
}

/**
 * Read the CONFIG register.
 */
static VBOXSTRICTRC HcConfig_r(PPDMDEVINS pDevIns, PXHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, iReg);
    STAM_COUNTER_INC(&pThis->StatRdConfig);
    *pu32Value = pThis->config;
    return VINF_SUCCESS;
}

/**
 * Write the CONFIG register.
 */
static VBOXSTRICTRC HcConfig_w(PPDMDEVINS pDevIns, PXHCI pThis, uint32_t iReg, uint32_t val)
{
    RT_NOREF(pDevIns, iReg);
    STAM_COUNTER_INC(&pThis->StatWrConfig);
    /// @todo  side effects?
    pThis->config = val;
    return VINF_SUCCESS;
}



/* -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- */
/*                    xHCI Port Register access routines                       */
/* -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- */



/**
 * Read the PORTSC register.
 */
static VBOXSTRICTRC HcPortStatusCtrl_r(PPDMDEVINS pDevIns, PXHCI pThis, uint32_t iPort, uint32_t *pu32Value)
{
    PXHCIHUBPORT    p = &pThis->aPorts[iPort];
    RT_NOREF(pDevIns);
    STAM_COUNTER_INC(&pThis->StatRdPortStatusCtrl);

    Assert(!(pThis->hcc_params & XHCI_HCC_PPC));

    if (p->portsc & XHCI_PORT_PR)
    {
/// @todo Probably not needed?
#ifdef IN_RING3
        Log2(("HcPortStatusCtrl_r(): port %u: Impatient guest!\n", IDX_TO_ID(iPort)));
        RTThreadYield();
#else
        Log2(("HcPortStatusCtrl_r: yield -> VINF_IOM_R3_MMIO_READ\n"));
        return VINF_IOM_R3_MMIO_READ;
#endif
    }

    /* The WPR bit is always read as zero. */
    *pu32Value = p->portsc & ~XHCI_PORT_WPR;
    return VINF_SUCCESS;
}

/**
 * Write the PORTSC register.
 */
static VBOXSTRICTRC HcPortStatusCtrl_w(PPDMDEVINS pDevIns, PXHCI pThis, uint32_t iPort, uint32_t val)
{
    PXHCIHUBPORT    p = &pThis->aPorts[iPort];
#ifdef IN_RING3
    PXHCICC         pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PXHCICC);
#endif
    STAM_COUNTER_INC(&pThis->StatWrPortStatusCtrl);

    /* If no register change results, we're done. */
    if (    p->portsc == val
        &&  !(val & XHCI_PORT_CHANGE_MASK))
        return VINF_SUCCESS;

    /* If port state is not changing (status bits are being cleared etc.), we can do it in any context.
     * This case occurs when the R/W control bits are not changing and the W1C bits are not being set.
     */
    if (   (p->portsc & XHCI_PORT_CTL_RW_MASK) == (val & XHCI_PORT_CTL_RW_MASK)
        && !(val & XHCI_PORT_CTL_W1_MASK))
    {
        Log(("HcPortStatusCtrl_w port %u (status only): old=%x new=%x\n", IDX_TO_ID(iPort), p->portsc, val));

        if (val & XHCI_PORT_RESERVED)
            Log(("Reserved bits set %x!\n", val & XHCI_PORT_RESERVED));

        /* A write to clear any of the change notification bits. */
        if (val & XHCI_PORT_CHANGE_MASK)
            p->portsc &= ~(val & XHCI_PORT_CHANGE_MASK);

        /* Update the wake mask. */
        p->portsc &= ~XHCI_PORT_WAKE_MASK;
        p->portsc |= val & XHCI_PORT_WAKE_MASK;

        /* There may still be differences between 'portsc' and 'val' in
         * the R/O bits; that does not count as a register change and is fine.
         * The RW1x control bits are not considered either since those only matter
         * if set in 'val'. Since the LWS bit was not set, the PLS bits should not
         * be compared. The port change bits may differ as well since the guest
         * could be clearing only some or none of them.
         */
        AssertMsg(!(val & XHCI_PORT_CTL_W1_MASK), ("val=%X\n", val));
        AssertMsg(!(val & XHCI_PORT_LWS), ("val=%X\n", val));
        AssertMsg((val & ~(XHCI_PORT_RO_MASK|XHCI_PORT_CTL_W1_MASK|XHCI_PORT_PLS_MASK|XHCI_PORT_CHANGE_MASK)) == (p->portsc & ~(XHCI_PORT_RO_MASK|XHCI_PORT_CTL_W1_MASK|XHCI_PORT_PLS_MASK|XHCI_PORT_CHANGE_MASK)), ("val=%X vs. portsc=%X\n", val, p->portsc));
        return VINF_SUCCESS;
    }

    /* Actual USB port state changes need to be done in R3. */
#ifdef IN_RING3
    Log(("HcPortStatusCtrl_w port %u: old=%x new=%x\n", IDX_TO_ID(iPort), p->portsc, val));
    Assert(!(pThis->hcc_params & XHCI_HCC_PPC));
    Assert(p->portsc & XHCI_PORT_PP);

    if (val & XHCI_PORT_RESERVED)
        Log(("Reserved bits set %x!\n", val & XHCI_PORT_RESERVED));

    /* A write to clear any of the change notification bits. */
    if (val & XHCI_PORT_CHANGE_MASK)
        p->portsc &= ~(val & XHCI_PORT_CHANGE_MASK);

    /* Writing the Port Enable/Disable bit as 1 disables a port; it cannot be
     * enabled that way. Writing the bit as zero does does nothing.
     */
    if ((val & XHCI_PORT_PED) && (p->portsc & XHCI_PORT_PED))
    {
        p->portsc &= ~XHCI_PORT_PED;
        Log(("HcPortStatusCtrl_w(): port %u: DISABLE\n", IDX_TO_ID(iPort)));
    }

    if (!(val & XHCI_PORT_PP) && (p->portsc & XHCI_PORT_PP))
    {
        p->portsc &= ~XHCI_PORT_PP;
        Log(("HcPortStatusCtrl_w(): port %u: POWER OFF\n", IDX_TO_ID(iPort)));
    }

    /* Warm Port Reset - USB3 only; see 4.19.5.1. */
    if ((val & XHCI_PORT_WPR) && IS_USB3_PORT_IDX_SHR(pThis, iPort))
    {
        Log(("HcPortStatusCtrl_w(): port %u: WARM RESET\n", IDX_TO_ID(iPort)));
        if (xhciR3RhPortSetIfConnected(pThis, iPort, XHCI_PORT_PR | XHCI_PORT_WPR))
        {
            PXHCIROOTHUBR3 pRh = GET_PORT_PRH(pThisCC, iPort);

            VUSBIRhDevReset(pRh->pIRhConn, GET_VUSB_PORT_FROM_XHCI_PORT(pRh, iPort), false /* don't reset on linux */, NULL /* sync */, NULL, PDMDevHlpGetVM(pDevIns));
            xhciR3PortResetDone(pDevIns, iPort);
        }
    }

    if (val & XHCI_PORT_PR)
    {
        Log(("HcPortStatusCtrl_w(): port %u: RESET\n", IDX_TO_ID(iPort)));
        if (xhciR3RhPortSetIfConnected(pThis, iPort, XHCI_PORT_PR))
        {
            PXHCIROOTHUBR3 pRh = GET_PORT_PRH(pThisCC, iPort);

            VUSBIRhDevReset(pRh->pIRhConn, GET_VUSB_PORT_FROM_XHCI_PORT(pRh, iPort), false /* don't reset on linux */, NULL /* sync */, NULL, PDMDevHlpGetVM(pDevIns));
            xhciR3PortResetDone(pDevIns, iPort);
        }
        else if (p->portsc & XHCI_PORT_PR)
        {
            /* the guest is getting impatient. */
            Log2(("HcPortStatusCtrl_w(): port %u: Impatient guest!\n", IDX_TO_ID(iPort)));
            RTThreadYield();
        }
    }

    /// @todo Do some sanity checking on the new link state?
    /* Update the link state if requested. */
    if (val & XHCI_PORT_LWS)
    {
        unsigned    old_pls;
        unsigned    new_pls;

        old_pls = (p->portsc & XHCI_PORT_PLS_MASK) >> XHCI_PORT_PLS_SHIFT;
        new_pls = (val       & XHCI_PORT_PLS_MASK) >> XHCI_PORT_PLS_SHIFT;

        p->portsc &= ~XHCI_PORT_PLS_MASK;
        p->portsc |= new_pls << XHCI_PORT_PLS_SHIFT;
        Log2(("HcPortStatusCtrl_w(): port %u: Updating link state from %u to %u\n", IDX_TO_ID(iPort), old_pls, new_pls));
        /* U3->U0 (USB3) and Resume->U0 transitions set the PLC flag. See 4.15.2.2 */
        if (new_pls == XHCI_PLS_U0)
            if (old_pls == XHCI_PLS_U3 || old_pls == XHCI_PLS_RESUME)
            {
                p->portsc |= XHCI_PORT_PLC;
                xhciR3GenPortChgEvent(pDevIns, pThis, IDX_TO_ID(iPort));
            }
    }

    /// @todo which other bits can we safely ignore?

    /* Update the wake mask. */
    p->portsc &= ~XHCI_PORT_WAKE_MASK;
    p->portsc |= val & XHCI_PORT_WAKE_MASK;

    return VINF_SUCCESS;
#else  /* !IN_RING3 */
    RT_NOREF(pDevIns);
    return VINF_IOM_R3_MMIO_WRITE;
#endif /* !IN_RING3 */
}


/**
 * Read the PORTPMSC register.
 */
static VBOXSTRICTRC HcPortPowerMgmt_r(PPDMDEVINS pDevIns, PXHCI pThis, uint32_t iPort, uint32_t *pu32Value)
{
    PXHCIHUBPORT    p = &pThis->aPorts[iPort];
    RT_NOREF(pDevIns);
    STAM_COUNTER_INC(&pThis->StatRdPortPowerMgmt);

    *pu32Value = p->portpm;
    return VINF_SUCCESS;
}


/**
 * Write the PORTPMSC register.
 */
static VBOXSTRICTRC HcPortPowerMgmt_w(PPDMDEVINS pDevIns, PXHCI pThis, uint32_t iPort, uint32_t val)
{
    PXHCIHUBPORT    p = &pThis->aPorts[iPort];
    RT_NOREF(pDevIns);
    STAM_COUNTER_INC(&pThis->StatWrPortPowerMgmt);

    /// @todo anything to do here?
    p->portpm = val;
    return VINF_SUCCESS;
}


/**
 * Read the PORTLI register.
 */
static VBOXSTRICTRC HcPortLinkInfo_r(PPDMDEVINS pDevIns, PXHCI pThis, uint32_t iPort, uint32_t *pu32Value)
{
    PXHCIHUBPORT    p = &pThis->aPorts[iPort];
    RT_NOREF(pDevIns);
    STAM_COUNTER_INC(&pThis->StatRdPortLinkInfo);

    /* The link information is R/O; we probably can't get it at all. If we
     * do maintain it for USB3 ports, we also have to reset it (5.4.10).
     */
    *pu32Value = p->portli;
    return VINF_SUCCESS;
}

/**
 * Read the reserved register. Linux likes to do this.
 */
static VBOXSTRICTRC HcPortRsvd_r(PPDMDEVINS pDevIns, PXHCI pThis, uint32_t iPort, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis, iPort);
    STAM_COUNTER_INC(&pThis->StatRdPortRsvd);
    *pu32Value = 0;
    return VINF_SUCCESS;
}



/* -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- */
/*                 xHCI Interrupter Register access routines                   */
/* -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- */



/**
 * Read the IMAN register.
 */
static VBOXSTRICTRC HcIntrMgmt_r(PPDMDEVINS pDevIns, PXHCI pThis, PXHCIINTRPTR ip, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis);
    STAM_COUNTER_INC(&pThis->StatRdIntrMgmt);

    *pu32Value = ip->iman;
    return VINF_SUCCESS;
}

/**
 * Write the IMAN register.
 */
static VBOXSTRICTRC HcIntrMgmt_w(PPDMDEVINS pDevIns, PXHCI pThis, PXHCIINTRPTR ip, uint32_t val)
{
    uint32_t        uNew = val & XHCI_IMAN_VALID_MASK;
    STAM_COUNTER_INC(&pThis->StatWrIntrMgmt);

    if (val & ~XHCI_IMAN_VALID_MASK)
        Log(("Reserved bits set %x!\n", val & ~XHCI_IMAN_VALID_MASK));

    /* If the Interrupt Pending (IP) bit is set, writing one clears it.
     * Note that when MSIs are enabled, the bit auto-clears almost immediately.
     */
    if (val & ip->iman & XHCI_IMAN_IP)
    {
        Log2(("clearing interrupt on interrupter %u\n", ip->index));
        PDMDevHlpPCISetIrq(pDevIns, 0, PDM_IRQ_LEVEL_LOW);
        STAM_COUNTER_INC(&pThis->StatIntrsCleared);
        uNew &= ~XHCI_IMAN_IP;
    }
    else
    {
        /* Preserve the current IP bit. */
        uNew = (uNew & ~XHCI_IMAN_IP) | (ip->iman & XHCI_IMAN_IP);
    }

    /* Trigger an interrupt if the IP bit is set and IE transitions from 0 to 1. */
    if (   (uNew & XHCI_IMAN_IE)
        && !(ip->iman & XHCI_IMAN_IE)
        && (ip->iman & XHCI_IMAN_IP)
        && (pThis->cmd & XHCI_CMD_INTE))
        PDMDevHlpPCISetIrq(pDevIns, 0, PDM_IRQ_LEVEL_HIGH);

    ip->iman = uNew;
    return VINF_SUCCESS;
}

/**
 * Read the IMOD register.
 */
static VBOXSTRICTRC HcIntrMod_r(PPDMDEVINS pDevIns, PXHCI pThis, PXHCIINTRPTR ip, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis);
    STAM_COUNTER_INC(&pThis->StatRdIntrMod);

    *pu32Value = ip->imod;
    return VINF_SUCCESS;
}

/**
 * Write the IMOD register.
 */
static VBOXSTRICTRC HcIntrMod_w(PPDMDEVINS pDevIns, PXHCI pThis, PXHCIINTRPTR ip, uint32_t val)
{
    RT_NOREF(pDevIns, pThis);
    STAM_COUNTER_INC(&pThis->StatWrIntrMod);

    /// @todo Does writing a zero to IMODC/IMODI potentially trigger
    /// an interrupt?
    ip->imod = val;
    return VINF_SUCCESS;
}

/**
 * Read the ERSTSZ register.
 */
static VBOXSTRICTRC HcEvtRSTblSize_r(PPDMDEVINS pDevIns, PXHCI pThis, PXHCIINTRPTR ip, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis);
    STAM_COUNTER_INC(&pThis->StatRdEvtRstblSize);

    *pu32Value = ip->erstsz;
    return VINF_SUCCESS;
}

/**
 * Write the ERSTSZ register.
 */
static VBOXSTRICTRC HcEvtRSTblSize_w(PPDMDEVINS pDevIns, PXHCI pThis, PXHCIINTRPTR ip, uint32_t val)
{
    RT_NOREF(pDevIns, pThis);
    STAM_COUNTER_INC(&pThis->StatWrEvtRstblSize);

    if (val & ~XHCI_ERSTSZ_MASK)
        Log(("Reserved bits set %x!\n", val & ~XHCI_ERSTSZ_MASK));
    if (val > XHCI_ERSTMAX)
        Log(("ERSTSZ (%u) > ERSTMAX (%u)!\n", val, XHCI_ERSTMAX));

    /* Enforce the maximum size. */
    ip->erstsz = RT_MIN(val, XHCI_ERSTMAX);

    if (!ip->index && !ip->erstsz)  /* Windows 8 does this temporarily. Thanks guys... */
        Log(("ERSTSZ is zero for primary interrupter: undefined behavior!\n"));

    return VINF_SUCCESS;
}

/**
 * Read the reserved register. Linux likes to do this.
 */
static VBOXSTRICTRC HcEvtRsvd_r(PPDMDEVINS pDevIns, PXHCI pThis, PXHCIINTRPTR ip, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis, ip);
    STAM_COUNTER_INC(&pThis->StatRdEvtRsvd);
    *pu32Value = 0;
    return VINF_SUCCESS;
}

/**
 * Read the low dword of the ERSTBA register.
 */
static VBOXSTRICTRC HcEvtRSTblBaseLo_r(PPDMDEVINS pDevIns, PXHCI pThis, PXHCIINTRPTR ip, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis);
    STAM_COUNTER_INC(&pThis->StatRdEvtRsTblBaseLo);

    *pu32Value = (uint32_t)ip->erstba;
    return VINF_SUCCESS;
}


/**
 * Write the low dword of the ERSTBA register.
 */
static VBOXSTRICTRC HcEvtRSTblBaseLo_w(PPDMDEVINS pDevIns, PXHCI pThis, PXHCIINTRPTR ip, uint32_t val)
{
    STAM_COUNTER_INC(&pThis->StatWrEvtRsTblBaseLo);

    if (val & ~pThis->erst_addr_mask)
        Log(("Reserved bits set %x!\n", val & ~pThis->erst_addr_mask));

    /* NB: A dword write to the low half clears the high half. */
    ip->erstba = val & pThis->erst_addr_mask;

    /* Initialize the internal event ring state. */
    ip->evtr_pcs = 1;
    ip->erst_idx = 0;
    ip->ipe      = false;

    /* Fetch the first ERST entry now. Not later! That "sets the Event Ring
     * State Machine:EREP Advancement to the Start state"
     */
    xhciFetchErstEntry(pDevIns, pThis, ip);

    return VINF_SUCCESS;
}

/**
 * Read the high dword of the ERSTBA register.
 */
static VBOXSTRICTRC HcEvtRSTblBaseHi_r(PPDMDEVINS pDevIns, PXHCI pThis, PXHCIINTRPTR ip, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis);
    STAM_COUNTER_INC(&pThis->StatRdEvtRsTblBaseHi);

    *pu32Value = (uint32_t)(ip->erstba >> 32);
    return VINF_SUCCESS;
}

/**
 * Write the high dword of the ERSTBA register.
 */
static VBOXSTRICTRC HcEvtRSTblBaseHi_w(PPDMDEVINS pDevIns, PXHCI pThis, PXHCIINTRPTR ip, uint32_t val)
{
    STAM_COUNTER_INC(&pThis->StatWrEvtRsTblBaseHi);

    /* Update the high dword while preserving the low one. */
    ip->erstba = ((uint64_t)val << 32) | (uint32_t)ip->erstba;

    /* We shouldn't be doing this when AC64 is set. But High Sierra
     * ignores that because it "knows" the xHC handles 64-bit addressing,
     * so we're going to assume that OSes are not going to write junk into
     * ERSTBAH when they don't see AC64 set.
     */
    xhciFetchErstEntry(pDevIns, pThis, ip);

    return VINF_SUCCESS;
}


/**
 * Read the low dword of the ERDP register.
 */
static VBOXSTRICTRC HcEvtRingDeqPtrLo_r(PPDMDEVINS pDevIns, PXHCI pThis, PXHCIINTRPTR ip, uint32_t *pu32Value)
{
    RT_NOREF(pThis);
    STAM_COUNTER_INC(&pThis->StatRdEvtRingDeqPtrLo);

    /* Lock to avoid incomplete update being seen. */
    int rc = PDMDevHlpCritSectEnter(pDevIns, &ip->lock, VINF_IOM_R3_MMIO_READ);
    if (rc != VINF_SUCCESS)
        return rc;

    *pu32Value = (uint32_t)ip->erdp;

    PDMDevHlpCritSectLeave(pDevIns, &ip->lock);

    return VINF_SUCCESS;
}

/**
 * Write the low dword of the ERDP register.
 */
static VBOXSTRICTRC HcEvtRingDeqPtrLo_w(PPDMDEVINS pDevIns, PXHCI pThis, PXHCIINTRPTR ip, uint32_t val)
{
    uint64_t        old_erdp;
    uint64_t        new_erdp;
    STAM_COUNTER_INC(&pThis->StatWrEvtRingDeqPtrLo);

    /* NB: A dword write to the low half clears the high half.
     * The high dword should be ignored when AC64=0, but High Sierra
     * does not care what we report. Therefore a write to the low dword
     * handles all the control bits and a write to the high dword still
     * updates the ERDP address. On a 64-bit host, there must be a
     * back-to-back low dword + high dword access. We are going to boldly
     * assume that the guest will not place the event ring across the 4G
     * boundary (i.e. storing the bottom part in the firmware ROM).
     */
    int rc = PDMDevHlpCritSectEnter(pDevIns, &ip->lock, VINF_IOM_R3_MMIO_WRITE);
    if (rc != VINF_SUCCESS)
        return rc;

    old_erdp = ip->erdp & XHCI_ERDP_ADDR_MASK;  /* Remember old ERDP address. */
    new_erdp = ip->erdp & XHCI_ERDP_EHB;        /* Preserve EHB */

    /* If the Event Handler Busy (EHB) bit is set, writing a one clears it. */
    if (val & ip->erdp & XHCI_ERDP_EHB)
    {
        Log2(("clearing EHB on interrupter %p\n", ip));
        new_erdp &= ~XHCI_ERDP_EHB;
    }
    /// @todo Check if this might inadvertently set EHB!

    new_erdp |= val & ~XHCI_ERDP_EHB;
    ip->erdp  = new_erdp;

    /* Check if the ERDP changed. See workaround below. */
    if (old_erdp != (new_erdp & XHCI_ERDP_ADDR_MASK))
        ip->erdp_rewrites = 0;
    else
        ++ip->erdp_rewrites;

    LogFlowFunc(("ERDP: %RGp, EREP: %RGp\n", (RTGCPHYS)(ip->erdp & XHCI_ERDP_ADDR_MASK), (RTGCPHYS)ip->erep));

    if ((ip->erdp & XHCI_ERDP_ADDR_MASK) == ip->erep)
    {
        Log2(("Event Ring empty, clearing IPE\n"));
        ip->ipe = false;
    }
    else if (ip->ipe && (val & XHCI_ERDP_EHB))
    {
        /* EHB is being cleared but the ring isn't empty and IPE is still set. */
        if (RT_UNLIKELY(old_erdp == (new_erdp & XHCI_ERDP_ADDR_MASK) && ip->erdp_rewrites > 2))
        {
            /* If guest does not advance the ERDP, do not trigger an interrupt
             * again. Workaround for buggy xHCI initialization in Linux 4.6 which
             * enables interrupts before setting up internal driver state. That
             * leads to the guest IRQ handler not actually handling events and
             * infinitely re-triggering interrupts. However, only do this if the
             * guest has already written the same ERDP value a few times. The Intel
             * xHCI driver always writes the same ERDP twice and we must still
             * re-trigger interrupts in that case.
             * See @bugref{8546}.
             */
            Log2(("Event Ring not empty, ERDP not advanced, not re-triggering interrupt!\n"));
            ip->ipe = false;
        }
        else
        {
            Log2(("Event Ring not empty, re-triggering interrupt\n"));
            xhciSetIntr(pDevIns, pThis, ip);
        }
    }

    PDMDevHlpCritSectLeave(pDevIns, &ip->lock);

    return VINF_SUCCESS;
}

/**
 * Read the high dword of the ERDP register.
 */
static VBOXSTRICTRC HcEvtRingDeqPtrHi_r(PPDMDEVINS pDevIns, PXHCI pThis, PXHCIINTRPTR ip, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis);
    STAM_COUNTER_INC(&pThis->StatRdEvtRingDeqPtrHi);

    *pu32Value = (uint32_t)(ip->erdp >> 32);
    return VINF_SUCCESS;
}

/**
 * Write the high dword of the ERDP register.
 */
static VBOXSTRICTRC HcEvtRingDeqPtrHi_w(PPDMDEVINS pDevIns, PXHCI pThis, PXHCIINTRPTR ip, uint32_t val)
{
    RT_NOREF(pThis);
    STAM_COUNTER_INC(&pThis->StatWrEvtRingDeqPtrHi);

    /* See HcEvtRingDeqPtrLo_w for semantics. */
    int rc = PDMDevHlpCritSectEnter(pDevIns, &ip->lock, VINF_IOM_R3_MMIO_WRITE);
    if (rc != VINF_SUCCESS)
        return rc;

    /* Update the high dword while preserving the low one. */
    ip->erdp = ((uint64_t)val << 32) | (uint32_t)ip->erdp;

    PDMDevHlpCritSectLeave(pDevIns, &ip->lock);

    return VINF_SUCCESS;
}


/**
 * xHCI register access routines.
 */
typedef struct
{
    const char *pszName;
    VBOXSTRICTRC (*pfnRead )(PPDMDEVINS pDevIns, PXHCI pThis, uint32_t iReg, uint32_t *pu32Value);
    VBOXSTRICTRC (*pfnWrite)(PPDMDEVINS pDevIns, PXHCI pThis, uint32_t iReg, uint32_t u32Value);
} XHCIREGACC;

/**
 * xHCI interrupter register access routines.
 */
typedef struct
{
    const char *pszName;
    VBOXSTRICTRC (*pfnIntrRead )(PPDMDEVINS pDevIns, PXHCI pThis, PXHCIINTRPTR pIntr, uint32_t *pu32Value);
    VBOXSTRICTRC (*pfnIntrWrite)(PPDMDEVINS pDevIns, PXHCI pThis, PXHCIINTRPTR pIntr, uint32_t u32Value);
} XHCIINTRREGACC;

/**
 * Operational registers descriptor table.
 */
static const XHCIREGACC g_aOpRegs[] =
{
    {"USBCMD" ,             HcUsbcmd_r,             HcUsbcmd_w          },
    {"USBSTS",              HcUsbsts_r,             HcUsbsts_w          },
    {"PAGESIZE",            HcPagesize_r,           NULL                },
    {"Unused",              NULL,                   NULL                },
    {"Unused",              NULL,                   NULL                },
    {"DNCTRL",              HcDevNotifyCtrl_r,      HcDevNotifyCtrl_w   },
    {"CRCRL",               HcCmdRingCtlLo_r,       HcCmdRingCtlLo_w    },
    {"CRCRH",               HcCmdRingCtlHi_r,       HcCmdRingCtlHi_w    },
    {"Unused",              NULL,                   NULL                },
    {"Unused",              NULL,                   NULL                },
    {"Unused",              NULL,                   NULL                },
    {"Unused",              NULL,                   NULL                },
    {"DCBAAPL",             HcDevCtxBAAPLo_r,       HcDevCtxBAAPLo_w    },
    {"DCBAAPH",             HcDevCtxBAAPHi_r,       HcDevCtxBAAPHi_w    },
    {"CONFIG",              HcConfig_r,             HcConfig_w          }
};


/**
 * Port registers descriptor table (for a single port). The number of ports
 * and their associated registers depends on the NDP value.
 */
static const XHCIREGACC g_aPortRegs[] =
{
    /*
     */
    {"PORTSC",              HcPortStatusCtrl_r,     HcPortStatusCtrl_w  },
    {"PORTPMSC",            HcPortPowerMgmt_r,      HcPortPowerMgmt_w   },
    {"PORTLI",              HcPortLinkInfo_r,       NULL                },
    {"Reserved",            HcPortRsvd_r,           NULL                }
};
AssertCompile(RT_ELEMENTS(g_aPortRegs) * sizeof(uint32_t) == 0x10);


/**
 * Interrupter runtime registers descriptor table (for a single interrupter).
 * The number of interrupters depends on the XHCI_NINTR value.
 */
static const XHCIINTRREGACC g_aIntrRegs[] =
{
    {"IMAN",                HcIntrMgmt_r,           HcIntrMgmt_w        },
    {"IMOD",                HcIntrMod_r,            HcIntrMod_w         },
    {"ERSTSZ",              HcEvtRSTblSize_r,       HcEvtRSTblSize_w    },
    {"Reserved",            HcEvtRsvd_r,            NULL                },
    {"ERSTBAL",             HcEvtRSTblBaseLo_r,     HcEvtRSTblBaseLo_w  },
    {"ERSTBAH",             HcEvtRSTblBaseHi_r,     HcEvtRSTblBaseHi_w  },
    {"ERDPL",               HcEvtRingDeqPtrLo_r,    HcEvtRingDeqPtrLo_w },
    {"ERDPH",               HcEvtRingDeqPtrHi_r,    HcEvtRingDeqPtrHi_w }
};
AssertCompile(RT_ELEMENTS(g_aIntrRegs) * sizeof(uint32_t) == 0x20);


/**
 * Read the MFINDEX register.
 */
static int HcMfIndex_r(PPDMDEVINS pDevIns, PXHCI pThis, uint32_t *pu32Value)
{
    uint64_t    uNanoTime;
    uint64_t    uMfTime;
    STAM_COUNTER_INC(&pThis->StatRdMfIndex);

    /* MFINDEX increments once per micro-frame, i.e. 8 times per millisecond
     * or every 125us. The resolution is only 14 bits, meaning that MFINDEX
     * wraps around after it reaches 0x3FFF (16383) or every 2048 milliseconds.
     */
    /// @todo MFINDEX should only be running when R/S is set. May not matter.
    uNanoTime = PDMDevHlpTimerGet(pDevIns, pThis->hWrapTimer);
    uMfTime   = uNanoTime / 125000;

    *pu32Value = uMfTime & 0x3FFF;
    Log2(("MFINDEX read: %u\n", *pu32Value));
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNIOMMMIONEWREAD, Read a MMIO register.}
 *
 * @note We only accept 32-bit writes that are 32-bit aligned.
 */
static DECLCALLBACK(VBOXSTRICTRC) xhciMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    PXHCI               pThis  = PDMDEVINS_2_DATA(pDevIns, PXHCI);
    const uint32_t      offReg = (uint32_t)off;
    uint32_t * const    pu32   = (uint32_t *)pv;
    uint32_t            iReg;
    RT_NOREF(pvUser);

    Log2(("xhciRead %RGp (offset %04X) size=%d\n", off, offReg, cb));

    if (offReg < XHCI_CAPS_REG_SIZE)
    {
        switch (offReg)
        {
            case 0x0:   /* CAPLENGTH + HCIVERSION */
                *pu32 = (pThis->hci_version << 16) | pThis->cap_length;
                break;

            case 0x4:   /* HCSPARAMS1 (structural) */
                Log2(("HCSPARAMS1 read\n"));
                *pu32 = pThis->hcs_params1;
                break;

            case 0x8:   /* HCSPARAMS2 (structural) */
                Log2(("HCSPARAMS2 read\n"));
                *pu32 = pThis->hcs_params2;
                break;

            case 0xC:   /* HCSPARAMS3 (structural) */
                Log2(("HCSPARAMS3 read\n"));
                *pu32 = pThis->hcs_params3;
                break;

            case 0x10:   /* HCCPARAMS1 (caps) */
                Log2(("HCCPARAMS1 read\n"));
                *pu32 = pThis->hcc_params;
                break;

            case 0x14:   /* DBOFF (doorbell offset) */
                Log2(("DBOFF read\n"));
                *pu32 = pThis->dbell_off;
                break;

            case 0x18:   /* RTSOFF (run-time register offset) */
                Log2(("RTSOFF read\n"));
                *pu32 = pThis->rts_off;
                break;

            case 0x1C:   /* HCCPARAMS2 (caps) */
                Log2(("HCCPARAMS2 read\n"));
                *pu32 = 0;  /* xHCI 1.1 only */
                break;

            default:
                Log(("xHCI: Trying to read unknown capability register %u!\n", offReg));
                STAM_COUNTER_INC(&pThis->StatRdUnknown);
                return VINF_IOM_MMIO_UNUSED_FF;
        }
        STAM_COUNTER_INC(&pThis->StatRdCaps);
        Log2(("xhciRead %RGp size=%d -> val=%x\n", off, cb, *pu32));
        return VINF_SUCCESS;
    }

    /*
     * Validate the access (in case of IOM bugs or incorrect MMIO registration).
     */
    AssertMsgReturn(cb == sizeof(uint32_t), ("IOM bug? %RGp LB %d\n", off, cb),
                    VINF_IOM_MMIO_UNUSED_FF /* No idea what really would happen... */);
    /** r=bird: If you don't have an idea what would happen for non-dword reads,
     * then the flags passed to IOM when creating the MMIO region are doubtful, right? */
    AssertMsgReturn(!(off & 0x3),    ("IOM bug? %RGp LB %d\n", off, cb), VINF_IOM_MMIO_UNUSED_FF);

    /*
     * Validate the register and call the read operator.
     */
    VBOXSTRICTRC rcStrict = VINF_IOM_MMIO_UNUSED_FF;
    if (offReg >= XHCI_DOORBELL_OFFSET)
    {
        /* The doorbell registers are effectively write-only and return 0 when read. */
        iReg = (offReg - XHCI_DOORBELL_OFFSET) >> 2;
        if (iReg < XHCI_NDS)
        {
            STAM_COUNTER_INC(&pThis->StatRdDoorBell);
            *pu32 = 0;
            rcStrict = VINF_SUCCESS;
            Log2(("xhciRead: DBellReg (DB %u) %RGp size=%d -> val=%x (rc=%d)\n", iReg, off, cb, *pu32, VBOXSTRICTRC_VAL(rcStrict)));
        }
    }
    else if (offReg >= XHCI_RTREG_OFFSET)
    {
        /* Run-time registers. */
        Assert(offReg < XHCI_DOORBELL_OFFSET);
        /* The MFINDEX register would be interrupter -1... */
        if (offReg < XHCI_RTREG_OFFSET + RT_ELEMENTS(g_aIntrRegs) * sizeof(uint32_t))
        {
            if (offReg == XHCI_RTREG_OFFSET)
                rcStrict = HcMfIndex_r(pDevIns, pThis, pu32);
            else
            {
                /* The silly Linux xHCI driver reads the reserved registers. */
                STAM_COUNTER_INC(&pThis->StatRdUnknown);
                *pu32 = 0;
                rcStrict = VINF_SUCCESS;
            }
        }
        else
        {
            Assert((offReg - XHCI_RTREG_OFFSET) / (RT_ELEMENTS(g_aIntrRegs) * sizeof(uint32_t)) > 0);
            const uint32_t iIntr = (offReg - XHCI_RTREG_OFFSET) / (RT_ELEMENTS(g_aIntrRegs) * sizeof(uint32_t)) - 1;

            if (iIntr < XHCI_NINTR)
            {
                iReg = (offReg >> 2) & (RT_ELEMENTS(g_aIntrRegs) - 1);
                const XHCIINTRREGACC *pReg = &g_aIntrRegs[iReg];
                if (pReg->pfnIntrRead)
                {
                    PXHCIINTRPTR pIntr = &pThis->aInterrupters[iIntr];
                    rcStrict = pReg->pfnIntrRead(pDevIns, pThis, pIntr, pu32);
                    Log2(("xhciRead: IntrReg (intr %u): %RGp (%s) size=%d -> val=%x (rc=%d)\n", iIntr, off, pReg->pszName, cb, *pu32, VBOXSTRICTRC_VAL(rcStrict)));
                }
            }
        }
    }
    else if (offReg >= XHCI_XECP_OFFSET)
    {
        /* Extended Capability registers. */
        Assert(offReg < XHCI_RTREG_OFFSET);
        uint32_t    offXcp = offReg - XHCI_XECP_OFFSET;

        if (offXcp + cb <= RT_MIN(pThis->cbExtCap, sizeof(pThis->abExtCap))) /* can't trust cbExtCap in ring-0. */
        {
            *pu32 = *(uint32_t *)&pThis->abExtCap[offXcp];
            rcStrict = VINF_SUCCESS;
        }
        Log2(("xhciRead: ExtCapReg %RGp size=%d -> val=%x (rc=%d)\n", off, cb, *pu32, VBOXSTRICTRC_VAL(rcStrict)));
    }
    else
    {
        /* Operational registers (incl. port registers). */
        Assert(offReg < XHCI_XECP_OFFSET);
        iReg = (offReg - XHCI_CAPS_REG_SIZE) >> 2;
        if (iReg < RT_ELEMENTS(g_aOpRegs))
        {
            const XHCIREGACC *pReg = &g_aOpRegs[iReg];
            if (pReg->pfnRead)
            {
                rcStrict = pReg->pfnRead(pDevIns, pThis, iReg, pu32);
                Log2(("xhciRead: OpReg %RGp (%s) size=%d -> val=%x (rc=%d)\n", off, pReg->pszName, cb, *pu32, VBOXSTRICTRC_VAL(rcStrict)));
            }
        }
        else if (iReg >= (XHCI_PORT_REG_OFFSET >> 2))
        {
            iReg -= (XHCI_PORT_REG_OFFSET >> 2);
            const uint32_t iPort = iReg / RT_ELEMENTS(g_aPortRegs);
            if (iPort < XHCI_NDP_CFG(pThis))
            {
                iReg = (offReg >> 2) & (RT_ELEMENTS(g_aPortRegs) - 1);
                Assert(iReg < RT_ELEMENTS(g_aPortRegs));
                const XHCIREGACC *pReg = &g_aPortRegs[iReg];
                if (pReg->pfnRead)
                {
                    rcStrict = pReg->pfnRead(pDevIns, pThis, iPort, pu32);
                    Log2(("xhciRead: PortReg (port %u): %RGp (%s) size=%d -> val=%x (rc=%d)\n", IDX_TO_ID(iPort), off, pReg->pszName, cb, *pu32, VBOXSTRICTRC_VAL(rcStrict)));
                }
            }
        }
    }

    if (rcStrict != VINF_IOM_MMIO_UNUSED_FF)
    { /* likely */ }
    else
    {
        STAM_COUNTER_INC(&pThis->StatRdUnknown);
        Log(("xHCI: Trying to read unimplemented register at offset %04X!\n", offReg));
    }

    return rcStrict;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE, Write to a MMIO register.}
 *
 * @note We only accept 32-bit writes that are 32-bit aligned.
 */
static DECLCALLBACK(VBOXSTRICTRC) xhciMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PXHCI               pThis  = PDMDEVINS_2_DATA(pDevIns, PXHCI);
    const uint32_t      offReg = (uint32_t)off;
    uint32_t * const    pu32   = (uint32_t *)pv;
    uint32_t            iReg;
    RT_NOREF(pvUser);

    Log2(("xhciWrite %RGp (offset %04X) %x size=%d\n", off, offReg, *(uint32_t *)pv, cb));

    if (offReg < XHCI_CAPS_REG_SIZE)
    {
        /* These are read-only */
        Log(("xHCI: Trying to write to register %u!\n", offReg));
        STAM_COUNTER_INC(&pThis->StatWrUnknown);
        return VINF_SUCCESS;
    }

    /*
     * Validate the access (in case of IOM bug or incorrect MMIO registration).
     */
    AssertMsgReturn(cb == sizeof(uint32_t), ("IOM bug? %RGp LB %d\n", off, cb), VINF_SUCCESS);
    AssertMsgReturn(!(off & 0x3),    ("IOM bug? %RGp LB %d\n", off, cb), VINF_SUCCESS);

    /*
     * Validate the register and call the write operator.
     */
    VBOXSTRICTRC rcStrict = VINF_IOM_MMIO_UNUSED_FF;
    if (offReg >= XHCI_DOORBELL_OFFSET)
    {
        /* Let's spring into action... as long as the xHC is running. */
        iReg = (offReg - XHCI_DOORBELL_OFFSET) >> 2;
        if ((pThis->cmd & XHCI_CMD_RS) && iReg < XHCI_NDS)
        {
            if (iReg == 0)
            {
                /* DB0 aka Command Ring. */
                STAM_COUNTER_INC(&pThis->StatWrDoorBell0);
                if (*pu32 == 0)
                {
                    /* Set the Command Ring state to Running if not already set. */
                    if (!(pThis->crcr & XHCI_CRCR_CRR))
                    {
                        Log(("Command ring entered Running state\n"));
                        ASMAtomicOrU64(&pThis->crcr, XHCI_CRCR_CRR);
                    }
                    xhciKickWorker(pDevIns, pThis, XHCI_JOB_PROCESS_CMDRING, 0);
                }
                else
                    Log2(("Ignoring DB0 write with value %X!\n", *pu32));
            }
            else
            {
                /* Device context doorbell. Do basic parameter checking to avoid
                 * waking up the worker thread needlessly.
                 */
                STAM_COUNTER_INC(&pThis->StatWrDoorBellN);
                uint8_t uDBTarget = *pu32 & XHCI_DB_TGT_MASK;
                Assert(uDBTarget < 32); /// @todo Report an error? Or just ignore?
                if (uDBTarget < 32)
                {
                    Log2(("Ring bell for slot %u, DCI %u\n", iReg, uDBTarget));
                    ASMAtomicOrU32(&pThis->aBellsRung[ID_TO_IDX(iReg)], 1 << uDBTarget);
                    xhciKickWorker(pDevIns, pThis, XHCI_JOB_DOORBELL, *pu32);
                }
                else
                    Log2(("Ignoring DB%u write with bad target %u!\n", iReg, uDBTarget));
            }
            rcStrict = VINF_SUCCESS;
            Log2(("xhciWrite: DBellReg (DB %u) %RGp size=%d <- val=%x (rc=%d)\n", iReg, off, cb, *(uint32_t *)pv, VBOXSTRICTRC_VAL(rcStrict)));
        }
    }
    else if (offReg >= XHCI_RTREG_OFFSET)
    {
        /* Run-time registers. */
        Assert(offReg < XHCI_DOORBELL_OFFSET);
        /* NB: The MFINDEX register is R/O. */
        if (offReg >= XHCI_RTREG_OFFSET + (RT_ELEMENTS(g_aIntrRegs) * sizeof(uint32_t)))
        {
            Assert((offReg - XHCI_RTREG_OFFSET) / (RT_ELEMENTS(g_aIntrRegs) * sizeof(uint32_t)) > 0);
            const uint32_t iIntr = (offReg - XHCI_RTREG_OFFSET) / (RT_ELEMENTS(g_aIntrRegs) * sizeof(uint32_t)) - 1;

            if (iIntr < XHCI_NINTR)
            {
                iReg = (offReg >> 2) & (RT_ELEMENTS(g_aIntrRegs) - 1);
                const XHCIINTRREGACC *pReg = &g_aIntrRegs[iReg];
                if (pReg->pfnIntrWrite)
                {
                    PXHCIINTRPTR pIntr = &pThis->aInterrupters[iIntr];
                    rcStrict = pReg->pfnIntrWrite(pDevIns, pThis, pIntr, *pu32);
                    Log2(("xhciWrite: IntrReg (intr %u): %RGp (%s) size=%d <- val=%x (rc=%d)\n", iIntr, off, pReg->pszName, cb, *pu32, VBOXSTRICTRC_VAL(rcStrict)));
                }
            }
        }
    }
    else
    {
        /* Operational registers (incl. port registers). */
        Assert(offReg < XHCI_RTREG_OFFSET);
        iReg = (offReg - pThis->cap_length) >> 2;
        if (iReg < RT_ELEMENTS(g_aOpRegs))
        {
            const XHCIREGACC *pReg = &g_aOpRegs[iReg];
            if (pReg->pfnWrite)
            {
                rcStrict = pReg->pfnWrite(pDevIns, pThis, iReg, *(uint32_t *)pv);
                Log2(("xhciWrite: OpReg %RGp (%s) size=%d <- val=%x (rc=%d)\n", off, pReg->pszName, cb, *(uint32_t *)pv, VBOXSTRICTRC_VAL(rcStrict)));
            }
        }
        else if (iReg >= (XHCI_PORT_REG_OFFSET >> 2))
        {
            iReg -= (XHCI_PORT_REG_OFFSET >> 2);
            const uint32_t iPort = iReg / RT_ELEMENTS(g_aPortRegs);
            if (iPort < XHCI_NDP_CFG(pThis))
            {
                iReg = (offReg >> 2) & (RT_ELEMENTS(g_aPortRegs) - 1);
                Assert(iReg < RT_ELEMENTS(g_aPortRegs));
                const XHCIREGACC *pReg = &g_aPortRegs[iReg];
                if (pReg->pfnWrite)
                {
                    rcStrict = pReg->pfnWrite(pDevIns, pThis, iPort, *pu32);
                    Log2(("xhciWrite: PortReg (port %u): %RGp (%s) size=%d <- val=%x (rc=%d)\n", IDX_TO_ID(iPort), off, pReg->pszName, cb, *pu32, VBOXSTRICTRC_VAL(rcStrict)));
                }
            }
        }
    }

    if (rcStrict != VINF_IOM_MMIO_UNUSED_FF)
    { /* likely */ }
    else
    {
        /* Ignore writes to unimplemented or read-only registers. */
        STAM_COUNTER_INC(&pThis->StatWrUnknown);
        Log(("xHCI: Trying to write unimplemented or R/O register at offset %04X!\n", offReg));
        rcStrict = VINF_SUCCESS;
    }

    return rcStrict;
}


#ifdef IN_RING3

/**
 * @callback_method_impl{FNTMTIMERDEV,
 *      Provides periodic MFINDEX wrap events. See 4.14.2.}
 */
static DECLCALLBACK(void) xhciR3WrapTimer(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PXHCI           pThis = (PXHCI)pvUser;
    XHCI_EVENT_TRB  ed;
    LogFlow(("xhciR3WrapTimer:\n"));
    RT_NOREF(hTimer);

    /*
     * Post the MFINDEX Wrap event and rearm the timer. Only called
     * when the EWE bit is set in command register.
     */
    RT_ZERO(ed);
    ed.mwe.cc       = XHCI_TCC_SUCCESS;
    ed.mwe.type     = XHCI_TRB_MFIDX_WRAP;
    xhciR3WriteEvent(pDevIns, pThis, &ed, XHCI_PRIMARY_INTERRUPTER, false);

    xhciSetWrapTimer(pDevIns, pThis);
}


/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) xhciR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PXHCI           pThis = PDMDEVINS_2_DATA(pDevIns, PXHCI);
    PCPDMDEVHLPR3   pHlp = pDevIns->pHlpR3;
    uint32_t        iPort;
    uint32_t        iSlot;
    uint32_t        iIntr;

    LogFlow(("xhciR3SaveExec: \n"));

    /* Save HC operational registers. */
    pHlp->pfnSSMPutU32(pSSM, pThis->cmd);
    pHlp->pfnSSMPutU32(pSSM, pThis->status);
    pHlp->pfnSSMPutU32(pSSM, pThis->dnctrl);
    pHlp->pfnSSMPutU64(pSSM, pThis->crcr);
    pHlp->pfnSSMPutU64(pSSM, pThis->dcbaap);
    pHlp->pfnSSMPutU32(pSSM, pThis->config);

    /* Save HC non-register state. */
    pHlp->pfnSSMPutU64(pSSM, pThis->cmdr_dqp);
    pHlp->pfnSSMPutBool(pSSM, pThis->cmdr_ccs);

    /* Save per-slot state. */
    pHlp->pfnSSMPutU32(pSSM, XHCI_NDS);
    for (iSlot = 0; iSlot < XHCI_NDS; ++iSlot)
    {
        pHlp->pfnSSMPutU8 (pSSM, pThis->aSlotState[iSlot]);
        pHlp->pfnSSMPutU32(pSSM, pThis->aBellsRung[iSlot]);
    }

    /* Save root hub (port) state. */
    pHlp->pfnSSMPutU32(pSSM, XHCI_NDP_CFG(pThis));
    for (iPort = 0; iPort < XHCI_NDP_CFG(pThis); ++iPort)
    {
        pHlp->pfnSSMPutU32(pSSM, pThis->aPorts[iPort].portsc);
        pHlp->pfnSSMPutU32(pSSM, pThis->aPorts[iPort].portpm);
    }

    /* Save interrupter state. */
    pHlp->pfnSSMPutU32(pSSM, XHCI_NINTR);
    for (iIntr = 0; iIntr < XHCI_NINTR; ++iIntr)
    {
        pHlp->pfnSSMPutU32(pSSM, pThis->aInterrupters[iIntr].iman);
        pHlp->pfnSSMPutU32(pSSM, pThis->aInterrupters[iIntr].imod);
        pHlp->pfnSSMPutU32(pSSM, pThis->aInterrupters[iIntr].erstsz);
        pHlp->pfnSSMPutU64(pSSM, pThis->aInterrupters[iIntr].erstba);
        pHlp->pfnSSMPutU64(pSSM, pThis->aInterrupters[iIntr].erdp);
        pHlp->pfnSSMPutU64(pSSM, pThis->aInterrupters[iIntr].erep);
        pHlp->pfnSSMPutU16(pSSM, pThis->aInterrupters[iIntr].erst_idx);
        pHlp->pfnSSMPutU16(pSSM, pThis->aInterrupters[iIntr].trb_count);
        pHlp->pfnSSMPutBool(pSSM, pThis->aInterrupters[iIntr].evtr_pcs);
        pHlp->pfnSSMPutBool(pSSM, pThis->aInterrupters[iIntr].ipe);
    }

    /* Terminator marker. */
    pHlp->pfnSSMPutU32(pSSM, UINT32_MAX);

    /* If not continuing after save, force HC into non-running state to avoid trouble later. */
    if (pHlp->pfnSSMHandleGetAfter(pSSM) != SSMAFTER_CONTINUE)
        pThis->cmd &= ~XHCI_CMD_RS;

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) xhciR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PXHCI           pThis = PDMDEVINS_2_DATA(pDevIns, PXHCI);
    PCPDMDEVHLPR3   pHlp = pDevIns->pHlpR3;
    int             rc;
    uint32_t        cPorts;
    uint32_t        iPort;
    uint32_t        cSlots;
    uint32_t        iSlot;
    uint32_t        cIntrs;
    uint32_t        iIntr;
    uint64_t        u64Dummy;
    uint32_t        u32Dummy;
    uint16_t        u16Dummy;
    uint8_t         u8Dummy;
    bool            fDummy;

    LogFlow(("xhciR3LoadExec:\n"));

    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);
    if (uVersion != XHCI_SAVED_STATE_VERSION)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    /* Load HC operational registers. */
    pHlp->pfnSSMGetU32(pSSM, &pThis->cmd);
    pHlp->pfnSSMGetU32(pSSM, &pThis->status);
    pHlp->pfnSSMGetU32(pSSM, &pThis->dnctrl);
    pHlp->pfnSSMGetU64(pSSM, &pThis->crcr);
    pHlp->pfnSSMGetU64(pSSM, &pThis->dcbaap);
    pHlp->pfnSSMGetU32(pSSM, &pThis->config);

    /* Load HC non-register state. */
    pHlp->pfnSSMGetU64(pSSM, &pThis->cmdr_dqp);
    pHlp->pfnSSMGetBool(pSSM, &pThis->cmdr_ccs);

    /* Load per-slot state. */
    rc = pHlp->pfnSSMGetU32(pSSM, &cSlots);
    AssertRCReturn(rc, rc);
    if (cSlots > 256)   /* Sanity check. */
        return VERR_SSM_INVALID_STATE;
    for (iSlot = 0; iSlot < cSlots; ++iSlot)
    {
        /* Load only as many slots as we have; discard any extras. */
        if (iSlot < XHCI_NDS)
        {
            pHlp->pfnSSMGetU8 (pSSM, &pThis->aSlotState[iSlot]);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aBellsRung[iSlot]);
        }
        else
        {
            pHlp->pfnSSMGetU8 (pSSM, &u8Dummy);
            pHlp->pfnSSMGetU32(pSSM, &u32Dummy);
        }
    }

    /* Load root hub (port) state. */
    rc = pHlp->pfnSSMGetU32(pSSM, &cPorts);
    AssertRCReturn(rc, rc);
    if (cPorts > 256)   /* Sanity check. */
        return VERR_SSM_INVALID_STATE;

    for (iPort = 0; iPort < cPorts; ++iPort)
    {
        /* Load only as many ports as we have; discard any extras. */
        if (iPort < XHCI_NDP_CFG(pThis))
        {
            pHlp->pfnSSMGetU32(pSSM, &pThis->aPorts[iPort].portsc);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aPorts[iPort].portpm);
        }
        else
        {
            pHlp->pfnSSMGetU32(pSSM, &u32Dummy);
            pHlp->pfnSSMGetU32(pSSM, &u32Dummy);
        }
    }

    /* Load interrupter state. */
    rc = pHlp->pfnSSMGetU32(pSSM, &cIntrs);
    AssertRCReturn(rc, rc);
    if (cIntrs > 256)   /* Sanity check. */
        return VERR_SSM_INVALID_STATE;
    for (iIntr = 0; iIntr < cIntrs; ++iIntr)
    {
        /* Load only as many interrupters as we have; discard any extras. */
        if (iIntr < XHCI_NINTR)
        {
            pHlp->pfnSSMGetU32(pSSM, &pThis->aInterrupters[iIntr].iman);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aInterrupters[iIntr].imod);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aInterrupters[iIntr].erstsz);
            pHlp->pfnSSMGetU64(pSSM, &pThis->aInterrupters[iIntr].erstba);
            pHlp->pfnSSMGetU64(pSSM, &pThis->aInterrupters[iIntr].erdp);
            pHlp->pfnSSMGetU64(pSSM, &pThis->aInterrupters[iIntr].erep);
            pHlp->pfnSSMGetU16(pSSM, &pThis->aInterrupters[iIntr].erst_idx);
            pHlp->pfnSSMGetU16(pSSM, &pThis->aInterrupters[iIntr].trb_count);
            pHlp->pfnSSMGetBool(pSSM, &pThis->aInterrupters[iIntr].evtr_pcs);
            pHlp->pfnSSMGetBool(pSSM, &pThis->aInterrupters[iIntr].ipe);
        }
        else
        {
            pHlp->pfnSSMGetU32(pSSM, &u32Dummy);
            pHlp->pfnSSMGetU32(pSSM, &u32Dummy);
            pHlp->pfnSSMGetU32(pSSM, &u32Dummy);
            pHlp->pfnSSMGetU64(pSSM, &u64Dummy);
            pHlp->pfnSSMGetU64(pSSM, &u64Dummy);
            pHlp->pfnSSMGetU64(pSSM, &u64Dummy);
            pHlp->pfnSSMGetU16(pSSM, &u16Dummy);
            pHlp->pfnSSMGetU16(pSSM, &u16Dummy);
            pHlp->pfnSSMGetBool(pSSM, &fDummy);
            pHlp->pfnSSMGetBool(pSSM, &fDummy);
        }
    }

    /* Terminator marker. */
    rc = pHlp->pfnSSMGetU32(pSSM, &u32Dummy);
    AssertRCReturn(rc, rc);
    AssertReturn(u32Dummy == UINT32_MAX, VERR_SSM_DATA_UNIT_FORMAT_CHANGED);

    return rc;
}


/* -=-=-=-=- DBGF -=-=-=-=- */

/**
 * @callback_method_impl{FNDBGFHANDLERDEV, Dumps xHCI state.}
 */
static DECLCALLBACK(void) xhciR3Info(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PXHCI       pThis = PDMDEVINS_2_DATA(pDevIns, PXHCI);
    RTGCPHYS    GPAddr;
    bool        fVerbose = false;
    unsigned    i, j;
    uint64_t    u64Val;

    /* Parse arguments. */
    if (pszArgs)
        fVerbose = strstr(pszArgs, "verbose") != NULL;

#ifdef XHCI_ERROR_INJECTION
    if (pszArgs && strstr(pszArgs, "dropintrhw"))
    {
        pHlp->pfnPrintf(pHlp, "Dropping the next interrupt (external)!\n");
        pThis->fDropIntrHw = true;
        return;
    }

    if (pszArgs && strstr(pszArgs, "dropintrint"))
    {
        pHlp->pfnPrintf(pHlp, "Dropping the next interrupt (internal)!\n");
        pThis->fDropIntrIpe = true;
        return;
    }

    if (pszArgs && strstr(pszArgs, "dropurb"))
    {
        pHlp->pfnPrintf(pHlp, "Dropping the next URB!\n");
        pThis->fDropUrb = true;
        return;
    }

    if (pszArgs && strstr(pszArgs, "genintrhw"))
    {
        pHlp->pfnPrintf(pHlp, "Generating hardware interrupt (external)...\n");
        int iIntr = 0;
        PXHCIINTRPTR pIntr = &pThis->aInterrupters[iIntr & XHCI_INTR_MASK];
        xhciSetIntr(pDevIns, pThis, pIntr);
        return;
    }

    if (pszArgs && strstr(pszArgs, "genintrint"))
    {
        pHlp->pfnPrintf(pHlp, "Generating hardware interrupt (internal)...\n");
        int iIntr = 0;
        PXHCIINTRPTR pIntr = &pThis->aInterrupters[iIntr & XHCI_INTR_MASK];
        xhciR3SetIntrPending(pDevIns, pThis, pIntr);
        return;
    }

    if (pszArgs && strstr(pszArgs, "genintrhw"))
    {
        pHlp->pfnPrintf(pHlp, "Generating hardware interrupt (external)...\n");
        int iIntr = 0;
        PXHCIINTRPTR pIntr = &pThis->aInterrupters[iIntr & XHCI_INTR_MASK];
        xhciSetIntr(pDevIns, pThis, pIntr);
        return;
    }

    if (pszArgs && strstr(pszArgs, "genintrint"))
    {
        pHlp->pfnPrintf(pHlp, "Generating hardware interrupt (internal)...\n");
        int iIntr = 0;
        PXHCIINTRPTR pIntr = &pThis->aInterrupters[iIntr & XHCI_INTR_MASK];
        xhciR3SetIntrPending(pDevIns, pThis, pIntr);
        return;
    }

    if (pszArgs && strstr(pszArgs, "genportchgevt"))
    {
        pHlp->pfnPrintf(pHlp, "Generating port change event...\n");
        int iPort = 0;
        xhciR3GenPortChgEvent(pDevIns, pThis, IDX_TO_ID(iPort));
        return;
    }

    if (pszArgs && strstr(pszArgs, "genmfiwrapevt"))
    {
        pHlp->pfnPrintf(pHlp, "Generating MF Index wrap event...\n");
        XHCI_EVENT_TRB  ed;
        RT_ZERO(ed);
        ed.mwe.cc   = XHCI_TCC_SUCCESS;
        ed.mwe.type = XHCI_TRB_MFIDX_WRAP;
        xhciR3WriteEvent(pDevIns, pThis, &ed, XHCI_PRIMARY_INTERRUPTER, false);
        return;
    }

    if (pszArgs && strstr(pszArgs, "gendoorbell"))
    {
        pHlp->pfnPrintf(pHlp, "Generating doorbell ring..\n");
        xhciKickWorker(pDevIns, pThis, XHCI_JOB_DOORBELL, 0);
        return;
    }
#endif

    /* Show basic information. */
    pHlp->pfnPrintf(pHlp,
                    "%s#%d: PCI MMIO=%RGp IRQ=%u MSI=%s R0=%RTbool RC=%RTbool\n",
                    pDevIns->pReg->szName,
                    pDevIns->iInstance,
                    PDMDevHlpMmioGetMappingAddress(pDevIns, pThis->hMmio),
                    PCIDevGetInterruptLine(pDevIns->apPciDevs[0]),
#ifdef VBOX_WITH_MSI_DEVICES
                    xhciIsMSIEnabled(pDevIns->apPciDevs[0]) ? "on" : "off",
#else
                    "none",
#endif
                    pDevIns->fR0Enabled, pDevIns->fRCEnabled);

    /* Command register. */
    pHlp->pfnPrintf(pHlp, "USBCMD: %X:", pThis->cmd);
    if (pThis->cmd & XHCI_CMD_EU3S)  pHlp->pfnPrintf(pHlp, " EU3S"  );
    if (pThis->cmd & XHCI_CMD_EWE)   pHlp->pfnPrintf(pHlp, " EWE"   );
    if (pThis->cmd & XHCI_CMD_CRS)   pHlp->pfnPrintf(pHlp, " CRS"   );
    if (pThis->cmd & XHCI_CMD_CSS)   pHlp->pfnPrintf(pHlp, " CSS"   );
    if (pThis->cmd & XHCI_CMD_LCRST) pHlp->pfnPrintf(pHlp, " LCRST" );
    if (pThis->cmd & XHCI_CMD_HSEE)  pHlp->pfnPrintf(pHlp, " HSEE"  );
    if (pThis->cmd & XHCI_CMD_INTE)  pHlp->pfnPrintf(pHlp, " INTE"  );
    if (pThis->cmd & XHCI_CMD_HCRST) pHlp->pfnPrintf(pHlp, " HCRST" );
    if (pThis->cmd & XHCI_CMD_RS)    pHlp->pfnPrintf(pHlp, " RS"    );
    pHlp->pfnPrintf(pHlp, "\n");

    /* Status register. */
    pHlp->pfnPrintf(pHlp, "USBSTS: %X:", pThis->status);
    if (pThis->status & XHCI_STATUS_HCH)  pHlp->pfnPrintf(pHlp, " HCH"  );
    if (pThis->status & XHCI_STATUS_HSE)  pHlp->pfnPrintf(pHlp, " HSE"  );
    if (pThis->status & XHCI_STATUS_EINT) pHlp->pfnPrintf(pHlp, " EINT" );
    if (pThis->status & XHCI_STATUS_PCD)  pHlp->pfnPrintf(pHlp, " PCD"  );
    if (pThis->status & XHCI_STATUS_SSS)  pHlp->pfnPrintf(pHlp, " SSS"  );
    if (pThis->status & XHCI_STATUS_RSS)  pHlp->pfnPrintf(pHlp, " RSS"  );
    if (pThis->status & XHCI_STATUS_SRE)  pHlp->pfnPrintf(pHlp, " SRE"  );
    if (pThis->status & XHCI_STATUS_CNR)  pHlp->pfnPrintf(pHlp, " CNR"  );
    if (pThis->status & XHCI_STATUS_HCE)  pHlp->pfnPrintf(pHlp, " HCE"  );
    pHlp->pfnPrintf(pHlp, "\n");

    /* Device Notification Control and Configure registers. */
    pHlp->pfnPrintf(pHlp, "DNCTRL: %X   CONFIG: %X (%u slots)\n", pThis->dnctrl, pThis->config, pThis->config);

    /* Device Context Base Address Array. */
    GPAddr = pThis->dcbaap & XHCI_DCBAA_ADDR_MASK;
    pHlp->pfnPrintf(pHlp, "DCBAA ptr: %RGp\n", GPAddr);
    /* The DCBAA must be valid in 'run' state. */
    if (fVerbose && (pThis->cmd & XHCI_CMD_RS))
    {
        PDMDevHlpPCIPhysRead(pDevIns, GPAddr, &u64Val, sizeof(u64Val));
        pHlp->pfnPrintf(pHlp, "  Scratchpad buffer: %RX64\n", u64Val);
    }

    /* Command Ring Control Register. */
    pHlp->pfnPrintf(pHlp, "CRCR: %X:", pThis->crcr & ~XHCI_CRCR_ADDR_MASK);
    if (pThis->crcr & XHCI_CRCR_RCS) pHlp->pfnPrintf(pHlp, " RCS");
    if (pThis->crcr & XHCI_CRCR_CS)  pHlp->pfnPrintf(pHlp, " CS" );
    if (pThis->crcr & XHCI_CRCR_CA)  pHlp->pfnPrintf(pHlp, " CA" );
    if (pThis->crcr & XHCI_CRCR_CRR) pHlp->pfnPrintf(pHlp, " CRR");
    pHlp->pfnPrintf(pHlp, "\n");
    GPAddr = pThis->crcr & XHCI_CRCR_ADDR_MASK;
    pHlp->pfnPrintf(pHlp, "CRCR ptr : %RGp\n", GPAddr);

    /* Interrupters. */
    if (fVerbose)
    {
        for (i = 0; i < RT_ELEMENTS(pThis->aInterrupters); ++i)
        {
            if (pThis->aInterrupters[i].erstsz)
            {
                XHCIINTRPTR     *ir = &pThis->aInterrupters[i];

                pHlp->pfnPrintf(pHlp, "Interrupter %d (IPE=%u)\n", i, ir->ipe);

                /* The Interrupt Management Register. */
                pHlp->pfnPrintf(pHlp, "  IMAN  : %X:", ir->iman);
                if (ir->iman & XHCI_IMAN_IP)   pHlp->pfnPrintf(pHlp, " IP");
                if (ir->iman & XHCI_IMAN_IE)   pHlp->pfnPrintf(pHlp, " IE");
                pHlp->pfnPrintf(pHlp, "\n");

                /* The Interrupt Moderation Register. */
                pHlp->pfnPrintf(pHlp, "  IMOD  : %X:", ir->imod);
                pHlp->pfnPrintf(pHlp, " IMODI=%u", ir->imod & XHCI_IMOD_IMODI_MASK);
                pHlp->pfnPrintf(pHlp, " IMODC=%u", (ir->imod & XHCI_IMOD_IMODC_MASK) >> XHCI_IMOD_IMODC_SHIFT);
                pHlp->pfnPrintf(pHlp, "\n");

                pHlp->pfnPrintf(pHlp, "  ERSTSZ: %X\n", ir->erstsz);
                pHlp->pfnPrintf(pHlp, "  ERSTBA: %RGp\n", (RTGCPHYS)ir->erstba);

                pHlp->pfnPrintf(pHlp, "  ERDP  : %RGp:", (RTGCPHYS)ir->erdp);
                pHlp->pfnPrintf(pHlp, " EHB=%u", !!(ir->erdp & XHCI_ERDP_EHB));
                pHlp->pfnPrintf(pHlp, " DESI=%u", ir->erdp & XHCI_ERDP_DESI_MASK);
                pHlp->pfnPrintf(pHlp, " ptr=%RGp", ir->erdp & XHCI_ERDP_ADDR_MASK);
                pHlp->pfnPrintf(pHlp, "\n");

                pHlp->pfnPrintf(pHlp, "  EREP  : %RGp", ir->erep);
                pHlp->pfnPrintf(pHlp, " Free TRBs in seg=%u", ir->trb_count);
                pHlp->pfnPrintf(pHlp, "\n");
            }
        }
    }

    /* Port control/status. */
    for (i = 0; i < XHCI_NDP_CFG(pThis); ++i)
    {
        PXHCIHUBPORT    p = &pThis->aPorts[i];

        pHlp->pfnPrintf(pHlp, "Port %02u (USB%c): ", IDX_TO_ID(i), IS_USB3_PORT_IDX_SHR(pThis, i) ? '3' : '2');

        /* Port Status register. */
        pHlp->pfnPrintf(pHlp, "PORTSC: %8X:", p->portsc);
        if (p->portsc & XHCI_PORT_CCS)   pHlp->pfnPrintf(pHlp, " CCS"  );
        if (p->portsc & XHCI_PORT_PED)   pHlp->pfnPrintf(pHlp, " PED"  );
        if (p->portsc & XHCI_PORT_OCA)   pHlp->pfnPrintf(pHlp, " OCA"  );
        if (p->portsc & XHCI_PORT_PR )   pHlp->pfnPrintf(pHlp, " PR"   );
        pHlp->pfnPrintf(pHlp, " PLS=%u", (p->portsc & XHCI_PORT_PLS_MASK) >> XHCI_PORT_PLS_SHIFT);
        if (p->portsc & XHCI_PORT_PP )   pHlp->pfnPrintf(pHlp, " PP"   );
        pHlp->pfnPrintf(pHlp, " SPD=%u", (p->portsc & XHCI_PORT_SPD_MASK) >> XHCI_PORT_SPD_SHIFT);
        if (p->portsc & XHCI_PORT_LWS)   pHlp->pfnPrintf(pHlp, " LWS"  );
        if (p->portsc & XHCI_PORT_CSC)   pHlp->pfnPrintf(pHlp, " CSC"  );
        if (p->portsc & XHCI_PORT_PEC)   pHlp->pfnPrintf(pHlp, " PEC"  );
        if (p->portsc & XHCI_PORT_WRC)   pHlp->pfnPrintf(pHlp, " WRC"  );
        if (p->portsc & XHCI_PORT_OCC)   pHlp->pfnPrintf(pHlp, " OCC"  );
        if (p->portsc & XHCI_PORT_PRC)   pHlp->pfnPrintf(pHlp, " PRC"  );
        if (p->portsc & XHCI_PORT_PLC)   pHlp->pfnPrintf(pHlp, " PLC"  );
        if (p->portsc & XHCI_PORT_CEC)   pHlp->pfnPrintf(pHlp, " CEC"  );
        if (p->portsc & XHCI_PORT_CAS)   pHlp->pfnPrintf(pHlp, " CAS"  );
        if (p->portsc & XHCI_PORT_WCE)   pHlp->pfnPrintf(pHlp, " WCE"  );
        if (p->portsc & XHCI_PORT_WDE)   pHlp->pfnPrintf(pHlp, " WDE"  );
        if (p->portsc & XHCI_PORT_WOE)   pHlp->pfnPrintf(pHlp, " WOE"  );
        if (p->portsc & XHCI_PORT_DR )   pHlp->pfnPrintf(pHlp, " DR"   );
        if (p->portsc & XHCI_PORT_WPR)   pHlp->pfnPrintf(pHlp, " WPR"  );
        pHlp->pfnPrintf(pHlp, "\n");
    }

    /* Device contexts. */
    if (fVerbose && (pThis->cmd & XHCI_CMD_RS))
    {
        for (i = 0; i < XHCI_NDS; ++i)
        {
            if (pThis->aSlotState[i] > XHCI_DEVSLOT_EMPTY)
            {
                RTGCPHYS         GCPhysSlot;
                XHCI_DEV_CTX     ctxDevice;
                XHCI_SLOT_CTX    ctxSlot;
                const char       *pcszDesc;
                uint8_t          uSlotID = IDX_TO_ID(i);

                /* Find the slot address/ */
                GCPhysSlot = xhciR3FetchDevCtxAddr(pDevIns, pThis, uSlotID);
                pHlp->pfnPrintf(pHlp, "Slot %d (device context @ %RGp)\n", uSlotID, GCPhysSlot);
                if (!GCPhysSlot)
                {
                    pHlp->pfnPrintf(pHlp, "Bad context address, skipping!\n");
                    continue;
                }

                /* Just read in the whole lot and sort in which contexts are valid later. */
                PDMDevHlpPCIPhysRead(pDevIns, GCPhysSlot, &ctxDevice, sizeof(ctxDevice));

                ctxSlot  = ctxDevice.entry[0].sc;
                pcszDesc = ctxSlot.slot_state < RT_ELEMENTS(g_apszSltStates) ? g_apszSltStates[ctxSlot.slot_state] : "BAD!!!";
                pHlp->pfnPrintf(pHlp, "  Speed:%u Entries:%u RhPort:%u", ctxSlot.speed, ctxSlot.ctx_ent, ctxSlot.rh_port);
                pHlp->pfnPrintf(pHlp, " Address:%u State:%s \n", ctxSlot.dev_addr, pcszDesc);
                pHlp->pfnPrintf(pHlp, "  Doorbells:%08X\n", pThis->aBellsRung[ID_TO_IDX(uSlotID)]);

                /* Endpoint contexts. */
                for (j = 1; j <= ctxSlot.ctx_ent; ++j)
                {
                    XHCI_EP_CTX     ctxEP = ctxDevice.entry[j].ep;

                    /* Skip disabled endpoints -- they may be unused and do not
                      * contain valid data in any case.
                      */
                    if (ctxEP.ep_state == XHCI_EPST_DISABLED)
                        continue;

                    pcszDesc = ctxEP.ep_state < RT_ELEMENTS(g_apszEpStates) ? g_apszEpStates[ctxEP.ep_state] : "BAD!!!";
                    pHlp->pfnPrintf(pHlp, "  Endpoint DCI %u State:%s", j, pcszDesc);
                    pcszDesc = ctxEP.ep_type < RT_ELEMENTS(g_apszEpTypes) ? g_apszEpTypes[ctxEP.ep_type] : "BAD!!!";
                    pHlp->pfnPrintf(pHlp, " Type:%s\n",pcszDesc);

                    pHlp->pfnPrintf(pHlp, "    Mult:%u MaxPStreams:%u LSA:%u Interval:%u\n",
                                    ctxEP.mult, ctxEP.maxps, ctxEP.lsa, ctxEP.interval);
                    pHlp->pfnPrintf(pHlp, "    CErr:%u HID:%u MaxPS:%u MaxBS:%u",
                                    ctxEP.c_err, ctxEP.hid, ctxEP.max_pkt_sz, ctxEP.max_brs_sz);
                    pHlp->pfnPrintf(pHlp, " AvgTRBLen:%u MaxESIT:%u",
                                    ctxEP.avg_trb_len, ctxEP.max_esit);
                    pHlp->pfnPrintf(pHlp, " LastFrm:%u IFC:%u LastCC:%u\n",
                                    ctxEP.last_frm, ctxEP.ifc, ctxEP.last_cc);
                    pHlp->pfnPrintf(pHlp, "    TRDP:%RGp DCS:%u\n", (RTGCPHYS)(ctxEP.trdp & XHCI_TRDP_ADDR_MASK),
                                    ctxEP.trdp & XHCI_TRDP_DCS_MASK);
                    pHlp->pfnPrintf(pHlp, "    TREP:%RGp DCS:%u\n", (RTGCPHYS)(ctxEP.trep & XHCI_TRDP_ADDR_MASK),
                                    ctxEP.trep & XHCI_TRDP_DCS_MASK);
                }
            }
        }
    }
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) xhciR3Reset(PPDMDEVINS pDevIns)
{
    PXHCI pThis = PDMDEVINS_2_DATA(pDevIns, PXHCI);
    PXHCICC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PXHCICC);
    LogFlow(("xhciR3Reset:\n"));

    /*
     * There is no distinction between cold boot, warm reboot and software reboots,
     * all of these are treated as cold boots. We are also doing the initialization
     * job of a BIOS or SMM driver.
     *
     * Important: Don't confuse UsbReset with hardware reset. Hardware reset is
     *            just one way of getting into the UsbReset state.
     */

    /* Set the HC Halted bit now to prevent completion callbacks from running
     *(there is really no point when resetting).
     */
    ASMAtomicOrU32(&pThis->status, XHCI_STATUS_HCH);

    xhciR3BusStop(pDevIns, pThis, pThisCC);
    xhciR3DoReset(pThis, pThisCC, XHCI_USB_RESET, true /* reset devices */);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) xhciR3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);
    PXHCI   pThis = PDMDEVINS_2_DATA(pDevIns, PXHCI);
    PXHCICC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PXHCICC);
    LogFlow(("xhciR3Destruct:\n"));

    /*
     * Destroy interrupter locks.
     */
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aInterrupters); ++i)
    {
        if (PDMDevHlpCritSectIsInitialized(pDevIns, &pThis->aInterrupters[i].lock))
            PDMDevHlpCritSectDelete(pDevIns, &pThis->aInterrupters[i].lock);
    }

    /*
     * Clean up the worker thread and associated machinery.
     */
    if (pThis->hEvtProcess != NIL_SUPSEMEVENT)
    {
        PDMDevHlpSUPSemEventClose(pDevIns, pThis->hEvtProcess);
        pThis->hEvtProcess = NIL_SUPSEMEVENT;
    }
    if (RTCritSectIsInitialized(&pThisCC->CritSectThrd))
        RTCritSectDelete(&pThisCC->CritSectThrd);

    return VINF_SUCCESS;
}


/**
 * Worker for xhciR3Construct that registers a LUN (USB root hub).
 */
static int xhciR3RegisterHub(PPDMDEVINS pDevIns, PXHCIROOTHUBR3 pRh, int iLun, const char *pszDesc)
{
    int rc = PDMDevHlpDriverAttach(pDevIns, iLun, &pRh->IBase, &pRh->pIBase, pszDesc);
    AssertMsgRCReturn(rc, ("Configuration error: Failed to attach root hub driver to LUN #%d! (%Rrc)\n", iLun, rc), rc);

    pRh->pIRhConn = PDMIBASE_QUERY_INTERFACE(pRh->pIBase, VUSBIROOTHUBCONNECTOR);
    AssertMsgReturn(pRh->pIRhConn,
                    ("Configuration error: The driver doesn't provide the VUSBIROOTHUBCONNECTOR interface!\n"),
                    VERR_PDM_MISSING_INTERFACE);

    /* Set URB parameters. */
    rc = VUSBIRhSetUrbParams(pRh->pIRhConn, sizeof(VUSBURBHCIINT), 0);
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS, N_("OHCI: Failed to set URB parameters"));

    return rc;
}

/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct,XHCI
 *                       constructor}
 */
static DECLCALLBACK(int) xhciR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PXHCI           pThis   = PDMDEVINS_2_DATA(pDevIns, PXHCI);
    PXHCICC         pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PXHCICC);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    uint32_t        cUsb2Ports;
    uint32_t        cUsb3Ports;
    int             rc;
    LogFlow(("xhciR3Construct:\n"));
    RT_NOREF(iInstance);

    /*
     * Initialize data so the destructor runs smoothly.
     */
    pThis->hEvtProcess = NIL_SUPSEMEVENT;

    /*
     * Validate and read configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "USB2Ports|USB3Ports|ChipType", "");

    /* Number of USB2 ports option. */
    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "USB2Ports", &cUsb2Ports, XHCI_NDP_20_DEFAULT);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("xHCI configuration error: failed to read USB2Ports as integer"));

    if (cUsb2Ports == 0 || cUsb2Ports > XHCI_NDP_MAX)
        return PDMDevHlpVMSetError(pDevIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                   N_("xHCI configuration error: USB2Ports must be in range [%u,%u]"),
                                   1, XHCI_NDP_MAX);

    /* Number of USB3 ports option. */
    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "USB3Ports", &cUsb3Ports, XHCI_NDP_30_DEFAULT);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("xHCI configuration error: failed to read USB3Ports as integer"));

    if (cUsb3Ports == 0 || cUsb3Ports > XHCI_NDP_MAX)
        return PDMDevHlpVMSetError(pDevIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                   N_("xHCI configuration error: USB3Ports must be in range [%u,%u]"),
                                   1, XHCI_NDP_MAX);

    /* Check that the total number of ports is within limits.*/
    if (cUsb2Ports + cUsb3Ports > XHCI_NDP_MAX)
        return PDMDevHlpVMSetError(pDevIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                   N_("xHCI configuration error: USB2Ports + USB3Ports must be in range [%u,%u]"),
                                   1, XHCI_NDP_MAX);

    /* Determine the model. */
    char szChipType[16];
    rc = pHlp->pfnCFGMQueryStringDef(pCfg, "ChipType", &szChipType[0], sizeof(szChipType), "PantherPoint");
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, VERR_PDM_DEVINS_UNKNOWN_CFG_VALUES,
                                N_("xHCI configuration error: Querying \"ChipType\" as string failed"));

    /*
     * The default model is Panther Point (8086:1E31), Intel's first and most widely
     * supported xHCI implementation. For debugging, the Lynx Point (8086:8C31) model
     * can be selected. These two models work with the 7 Series and 8 Series Intel xHCI
     * drivers for Windows 7, respectively. There is no functional difference.
     * For Windows XP support, it's also possible to present a Renesas uPD720201 xHC;
     * this is an evolution of the original NEC xHCI chip.
     */
    bool fChipLynxPoint = false;
    bool fChipRenesas   = false;
    if (!strcmp(szChipType, "PantherPoint"))
        fChipLynxPoint = false;
    else if (!strcmp(szChipType, "LynxPoint"))
        fChipLynxPoint = true;
    else if (!strcmp(szChipType, "uPD720201"))
        fChipRenesas = true;
    else
        return PDMDevHlpVMSetError(pDevIns, VERR_PDM_DEVINS_UNKNOWN_CFG_VALUES, RT_SRC_POS,
                                   N_("xHCI configuration error: The \"ChipType\" value \"%s\" is unsupported"), szChipType);

    LogFunc(("cUsb2Ports=%u cUsb3Ports=%u szChipType=%s (%d,%d) fR0Enabled=%d fRCEnabled=%d\n", cUsb2Ports, cUsb3Ports,
             szChipType, fChipLynxPoint, fChipRenesas, pDevIns->fR0Enabled, pDevIns->fRCEnabled));

    /* Set up interrupter locks. */
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aInterrupters); ++i)
    {
        rc = PDMDevHlpCritSectInit(pDevIns, &pThis->aInterrupters[i].lock, RT_SRC_POS, "xHCIIntr#%u", i);
        if (RT_FAILURE(rc))
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                       N_("xHCI: Failed to create critical section for interrupter %u"), i);
        pThis->aInterrupters[i].index = i;  /* Stash away index, mostly for logging/debugging. */
    }


    /*
     * Init instance data.
     */
    pThisCC->pDevIns = pDevIns;

    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    if (fChipRenesas)
    {
        pThis->erst_addr_mask = NEC_ERST_ADDR_MASK;
        PCIDevSetVendorId(pPciDev,      0x1912);
        PCIDevSetDeviceId(pPciDev,      0x0014);
        PCIDevSetByte(pPciDev, VBOX_PCI_REVISION_ID, 0x02);
    }
    else
    {
        pThis->erst_addr_mask = XHCI_ERST_ADDR_MASK;
        PCIDevSetVendorId(pPciDev,      0x8086);
        if (fChipLynxPoint)
            PCIDevSetDeviceId(pPciDev,  0x8C31); /* Lynx Point / 8 Series */
        else
            PCIDevSetDeviceId(pPciDev,  0x1E31); /* Panther Point / 7 Series */
    }

    PCIDevSetClassProg(pPciDev,         0x30);   /* xHCI */
    PCIDevSetClassSub(pPciDev,          0x03);   /* USB 3.0 */
    PCIDevSetClassBase(pPciDev,         0x0C);
    PCIDevSetInterruptPin(pPciDev,      0x01);
#ifdef VBOX_WITH_MSI_DEVICES
    PCIDevSetStatus(pPciDev,            VBOX_PCI_STATUS_CAP_LIST);
    PCIDevSetCapabilityList(pPciDev,    0x80);
#endif
    PDMPciDevSetByte(pPciDev, 0x60,     0x20);   /* serial bus release number register; 0x20 = USB 2.0 */
    /** @todo USBLEGSUP & USBLEGCTLSTS? Legacy interface for the BIOS (0xEECP+0 & 0xEECP+4) */

    pThis->cTotalPorts                             = (uint8_t)(cUsb2Ports + cUsb3Ports);

    /* Set up the USB2 root hub interface. */
    pThis->cUsb2Ports                              = (uint8_t)cUsb2Ports;
    pThisCC->RootHub2.pXhciR3                      = pThisCC;
    pThisCC->RootHub2.cPortsImpl                   = cUsb2Ports;
    pThisCC->RootHub2.uPortBase                    = 0;
    pThisCC->RootHub2.IBase.pfnQueryInterface      = xhciR3RhQueryInterface;
    pThisCC->RootHub2.IRhPort.pfnGetAvailablePorts = xhciR3RhGetAvailablePorts;
    pThisCC->RootHub2.IRhPort.pfnGetUSBVersions    = xhciR3RhGetUSBVersions2;
    pThisCC->RootHub2.IRhPort.pfnAttach            = xhciR3RhAttach;
    pThisCC->RootHub2.IRhPort.pfnDetach            = xhciR3RhDetach;
    pThisCC->RootHub2.IRhPort.pfnReset             = xhciR3RhReset;
    pThisCC->RootHub2.IRhPort.pfnXferCompletion    = xhciR3RhXferCompletion;
    pThisCC->RootHub2.IRhPort.pfnXferError         = xhciR3RhXferError;

    /* Now the USB3 root hub interface. */
    pThis->cUsb3Ports                              = (uint8_t)cUsb3Ports;
    pThisCC->RootHub3.pXhciR3                      = pThisCC;
    pThisCC->RootHub3.cPortsImpl                   = cUsb3Ports;
    pThisCC->RootHub3.uPortBase                    = XHCI_NDP_USB2(pThisCC);
    pThisCC->RootHub3.IBase.pfnQueryInterface      = xhciR3RhQueryInterface;
    pThisCC->RootHub3.IRhPort.pfnGetAvailablePorts = xhciR3RhGetAvailablePorts;
    pThisCC->RootHub3.IRhPort.pfnGetUSBVersions    = xhciR3RhGetUSBVersions3;
    pThisCC->RootHub3.IRhPort.pfnAttach            = xhciR3RhAttach;
    pThisCC->RootHub3.IRhPort.pfnDetach            = xhciR3RhDetach;
    pThisCC->RootHub3.IRhPort.pfnReset             = xhciR3RhReset;
    pThisCC->RootHub3.IRhPort.pfnXferCompletion    = xhciR3RhXferCompletion;
    pThisCC->RootHub3.IRhPort.pfnXferError         = xhciR3RhXferError;

    /* USB LED */
    pThisCC->RootHub2.Led.u32Magic        = PDMLED_MAGIC;
    pThisCC->RootHub3.Led.u32Magic        = PDMLED_MAGIC;
    pThisCC->IBase.pfnQueryInterface      = xhciR3QueryStatusInterface;
    pThisCC->ILeds.pfnQueryStatusLed      = xhciR3QueryStatusLed;

    /* Initialize the capability registers */
    pThis->cap_length   = XHCI_CAPS_REG_SIZE;
    pThis->hci_version  = 0x100;    /* Version 1.0 */
    pThis->hcs_params1  = (XHCI_NDP_CFG(pThis) << 24) | (XHCI_NINTR << 8) | XHCI_NDS;
    pThis->hcs_params2  = (XHCI_ERSTMAX_LOG2 << 4) | XHCI_IST;
    pThis->hcs_params3  = (4 << 16) | 1;  /* Matches Intel 7 Series xHCI. */
    /* Note: The Intel 7 Series xHCI does not have port power control (XHCI_HCC_PPC). */
    pThis->hcc_params   = ((XHCI_XECP_OFFSET >> 2) << XHCI_HCC_XECP_SHIFT); /// @todo other fields
    pThis->dbell_off    = XHCI_DOORBELL_OFFSET;
    pThis->rts_off      = XHCI_RTREG_OFFSET;

    /*
     * Set up extended capabilities.
     */
    rc = xhciR3BuildExtCaps(pThis, pThisCC);
    AssertRCReturn(rc, rc);

    /*
     * Register PCI device and I/O region.
     */
    rc = PDMDevHlpPCIRegister(pDevIns, pPciDev);
    AssertRCReturn(rc, rc);

#ifdef VBOX_WITH_MSI_DEVICES
    PDMMSIREG MsiReg;
    RT_ZERO(MsiReg);
    MsiReg.cMsiVectors    = 1;
    MsiReg.iMsiCapOffset  = XHCI_PCI_MSI_CAP_OFS;
    MsiReg.iMsiNextOffset = 0x00;
    rc = PDMDevHlpPCIRegisterMsi(pDevIns, &MsiReg);
    if (RT_FAILURE (rc))
    {
        PCIDevSetCapabilityList(pPciDev, 0x0);
        /* That's OK, we can work without MSI */
    }
#endif

    rc = PDMDevHlpPCIIORegionCreateMmio(pDevIns, 0, XHCI_MMIO_SIZE, PCI_ADDRESS_SPACE_MEM,
                                        xhciMmioWrite, xhciMmioRead, NULL,
                                          IOMMMIO_FLAGS_READ_DWORD | IOMMMIO_FLAGS_WRITE_DWORD_ZEROED
                                        /*| IOMMMIO_FLAGS_DBGSTOP_ON_COMPLICATED_WRITE*/,
                                        "USB xHCI", &pThis->hMmio);
    AssertRCReturn(rc, rc);

    /*
     * Register the saved state data unit.
     */
    rc = PDMDevHlpSSMRegisterEx(pDevIns, XHCI_SAVED_STATE_VERSION, sizeof(*pThis), NULL,
                                NULL, NULL, NULL,
                                NULL, xhciR3SaveExec, NULL,
                                NULL, xhciR3LoadExec, NULL);
    AssertRCReturn(rc, rc);

    /*
     * Attach to the VBox USB RootHub Driver on LUN #0 (USB3 root hub).
     * NB: USB3 must come first so that emulated devices which support both USB2
     * and USB3 are attached to the USB3 hub.
     */
    rc = xhciR3RegisterHub(pDevIns, &pThisCC->RootHub3, 0, "RootHubUSB3");
    AssertRCReturn(rc, rc);

    /*
     * Attach to the VBox USB RootHub Driver on LUN #1 (USB2 root hub).
     */
    rc = xhciR3RegisterHub(pDevIns, &pThisCC->RootHub2, 1, "RootHubUSB2");
    AssertRCReturn(rc, rc);

    /*
     * Attach the status LED (optional).
     */
    PPDMIBASE pBase;
    rc = PDMDevHlpDriverAttach(pDevIns, PDM_STATUS_LUN, &pThisCC->IBase, &pBase, "Status Port");
    if (RT_SUCCESS(rc))
        pThisCC->pLedsConnector = PDMIBASE_QUERY_INTERFACE(pBase, PDMILEDCONNECTORS);
    else if (rc != VERR_PDM_NO_ATTACHED_DRIVER)
    {
        AssertMsgFailed(("xHCI: Failed to attach to status driver. rc=%Rrc\n", rc));
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("xHCI cannot attach to status driver"));
    }

    /*
     * Create the MFINDEX wrap event timer.
     */
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL, xhciR3WrapTimer, pThis,
                              TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_RING0, "xHCI MFINDEX Wrap", &pThis->hWrapTimer);
    AssertRCReturn(rc, rc);

    /*
     * Set up the worker thread.
     */
    rc = PDMDevHlpSUPSemEventCreate(pDevIns, &pThis->hEvtProcess);
    AssertLogRelRCReturn(rc, rc);

    rc = RTCritSectInit(&pThisCC->CritSectThrd);
    AssertLogRelRCReturn(rc, rc);

    rc = PDMDevHlpThreadCreate(pDevIns, &pThisCC->pWorkerThread, pThis, xhciR3WorkerLoop, xhciR3WorkerWakeUp,
                               0, RTTHREADTYPE_IO, "xHCI");
    AssertLogRelRCReturn(rc, rc);

    /*
     * Do a hardware reset.
     */
    xhciR3DoReset(pThis, pThisCC, XHCI_USB_RESET, false /* don't reset devices */);

# ifdef VBOX_WITH_STATISTICS
    /*
     * Register statistics.
     */
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatErrorIsocUrbs, STAMTYPE_COUNTER, "IsocUrbsErr",   STAMUNIT_OCCURENCES, "Isoch URBs completed w/error.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatErrorIsocPkts, STAMTYPE_COUNTER, "IsocPktsErr",   STAMUNIT_OCCURENCES, "Isoch packets completed w/error.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatEventsWritten, STAMTYPE_COUNTER, "EventsWritten", STAMUNIT_OCCURENCES, "Event TRBs delivered.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatEventsDropped, STAMTYPE_COUNTER, "EventsDropped", STAMUNIT_OCCURENCES, "Event TRBs dropped (HC stopped).");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatIntrsPending,  STAMTYPE_COUNTER, "IntrsPending",  STAMUNIT_OCCURENCES, "Requests to set the IP bit.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatIntrsSet,      STAMTYPE_COUNTER, "IntrsSet",      STAMUNIT_OCCURENCES, "Actual interrupts delivered.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatIntrsNotSet,   STAMTYPE_COUNTER, "IntrsNotSet",   STAMUNIT_OCCURENCES, "Interrupts not delivered/disabled.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatIntrsCleared,  STAMTYPE_COUNTER, "IntrsCleared",  STAMUNIT_OCCURENCES, "Interrupts cleared by guest.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTRBsPerCtlUrb, STAMTYPE_COUNTER, "UrbTrbsCtl",    STAMUNIT_COUNT,      "TRBs per one control URB.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTRBsPerDtaUrb, STAMTYPE_COUNTER, "UrbTrbsDta",    STAMUNIT_COUNT,      "TRBs per one data (bulk/intr) URB.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTRBsPerIsoUrb, STAMTYPE_COUNTER, "UrbTrbsIso",    STAMUNIT_COUNT,      "TRBs per one isochronous URB.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatUrbSizeCtrl,   STAMTYPE_COUNTER, "UrbSizeCtl",    STAMUNIT_COUNT,      "Size of a control URB in bytes.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatUrbSizeData,   STAMTYPE_COUNTER, "UrbSizeDta",    STAMUNIT_COUNT,      "Size of a data (bulk/intr) URB in bytes.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatUrbSizeIsoc,   STAMTYPE_COUNTER, "UrbSizeIso",    STAMUNIT_COUNT,      "Size of an isochronous URB in bytes.");

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdCaps,              STAMTYPE_COUNTER, "Regs/RdCaps",            STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdCmdRingCtlHi,      STAMTYPE_COUNTER, "Regs/RdCmdRingCtlHi",    STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdCmdRingCtlLo,      STAMTYPE_COUNTER, "Regs/RdCmdRingCtlLo",    STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdConfig,            STAMTYPE_COUNTER, "Regs/RdConfig",          STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdDevCtxBaapHi,      STAMTYPE_COUNTER, "Regs/RdDevCtxBaapHi",    STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdDevCtxBaapLo,      STAMTYPE_COUNTER, "Regs/RdDevCtxBaapLo",    STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdDevNotifyCtrl,     STAMTYPE_COUNTER, "Regs/RdDevNotifyCtrl",   STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdDoorBell,          STAMTYPE_COUNTER, "Regs/RdDoorBell",        STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdEvtRingDeqPtrHi,   STAMTYPE_COUNTER, "Regs/RdEvtRingDeqPtrHi", STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdEvtRingDeqPtrLo,   STAMTYPE_COUNTER, "Regs/RdEvtRingDeqPtrLo", STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdEvtRsTblBaseHi,    STAMTYPE_COUNTER, "Regs/RdEvtRsTblBaseHi",  STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdEvtRsTblBaseLo,    STAMTYPE_COUNTER, "Regs/RdEvtRsTblBaseLo",  STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdEvtRstblSize,      STAMTYPE_COUNTER, "Regs/RdEvtRstblSize",    STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdEvtRsvd,           STAMTYPE_COUNTER, "Regs/RdEvtRsvd",         STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdIntrMgmt,          STAMTYPE_COUNTER, "Regs/RdIntrMgmt",        STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdIntrMod,           STAMTYPE_COUNTER, "Regs/RdIntrMod",         STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdMfIndex,           STAMTYPE_COUNTER, "Regs/RdMfIndex",         STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdPageSize,          STAMTYPE_COUNTER, "Regs/RdPageSize",        STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdPortLinkInfo,      STAMTYPE_COUNTER, "Regs/RdPortLinkInfo",    STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdPortPowerMgmt,     STAMTYPE_COUNTER, "Regs/RdPortPowerMgmt",   STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdPortRsvd,          STAMTYPE_COUNTER, "Regs/RdPortRsvd",        STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdPortStatusCtrl,    STAMTYPE_COUNTER, "Regs/RdPortStatusCtrl",  STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdUsbCmd,            STAMTYPE_COUNTER, "Regs/RdUsbCmd",          STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdUsbSts,            STAMTYPE_COUNTER, "Regs/RdUsbSts",          STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRdUnknown,           STAMTYPE_COUNTER, "Regs/RdUnknown",         STAMUNIT_COUNT, "");

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatWrCmdRingCtlHi,      STAMTYPE_COUNTER, "Regs/WrCmdRingCtlHi",    STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatWrCmdRingCtlLo,      STAMTYPE_COUNTER, "Regs/WrCmdRingCtlLo",    STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatWrConfig,            STAMTYPE_COUNTER, "Regs/WrConfig",          STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatWrDevCtxBaapHi,      STAMTYPE_COUNTER, "Regs/WrDevCtxBaapHi",    STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatWrDevCtxBaapLo,      STAMTYPE_COUNTER, "Regs/WrDevCtxBaapLo",    STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatWrDevNotifyCtrl,     STAMTYPE_COUNTER, "Regs/WrDevNotifyCtrl",   STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatWrDoorBell0,         STAMTYPE_COUNTER, "Regs/WrDoorBell0",       STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatWrDoorBellN,         STAMTYPE_COUNTER, "Regs/WrDoorBellN",       STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatWrEvtRingDeqPtrHi,   STAMTYPE_COUNTER, "Regs/WrEvtRingDeqPtrHi", STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatWrEvtRingDeqPtrLo,   STAMTYPE_COUNTER, "Regs/WrEvtRingDeqPtrLo", STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatWrEvtRsTblBaseHi,    STAMTYPE_COUNTER, "Regs/WrEvtRsTblBaseHi",  STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatWrEvtRsTblBaseLo,    STAMTYPE_COUNTER, "Regs/WrEvtRsTblBaseLo",  STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatWrEvtRstblSize,      STAMTYPE_COUNTER, "Regs/WrEvtRstblSize",    STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatWrIntrMgmt,          STAMTYPE_COUNTER, "Regs/WrIntrMgmt",        STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatWrIntrMod,           STAMTYPE_COUNTER, "Regs/WrIntrMod",         STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatWrPortPowerMgmt,     STAMTYPE_COUNTER, "Regs/WrPortPowerMgmt",   STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatWrPortStatusCtrl,    STAMTYPE_COUNTER, "Regs/WrPortStatusCtrl",  STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatWrUsbCmd,            STAMTYPE_COUNTER, "Regs/WrUsbCmd",          STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatWrUsbSts,            STAMTYPE_COUNTER, "Regs/WrUsbSts",          STAMUNIT_COUNT, "");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatWrUnknown,           STAMTYPE_COUNTER, "Regs/WrUnknown",         STAMUNIT_COUNT, "");
# endif /* VBOX_WITH_STATISTICS */

    /*
     * Register debugger info callbacks.
     */
    PDMDevHlpDBGFInfoRegister(pDevIns, "xhci", "xHCI registers.", xhciR3Info);

    return VINF_SUCCESS;
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) xhciRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PXHCI pThis = PDMDEVINS_2_DATA(pDevIns, PXHCI);

    int rc = PDMDevHlpMmioSetUpContext(pDevIns, pThis->hMmio, xhciMmioWrite, xhciMmioRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */

/* Without this, g_DeviceXHCI won't be visible outside this module! */
extern "C" const PDMDEVREG g_DeviceXHCI;

const PDMDEVREG g_DeviceXHCI =
{
    /* .u32version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "usb-xhci",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_BUS_USB,
    /* .cMaxInstances = */          ~0U,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(XHCI),
    /* .cbInstanceCC = */           sizeof(XHCICC),
    /* .cbInstanceRC = */           sizeof(XHCIRC),
    /* .cMaxPciDevices = */         1,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "xHCI USB controller.\n",
#if defined(IN_RING3)
# ifdef VBOX_IN_EXTPACK
    /* .pszRCMod = */               "VBoxEhciRC.rc",
    /* .pszR0Mod = */               "VBoxEhciR0.r0",
# else
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
# endif
    /* .pfnConstruct = */           xhciR3Construct,
    /* .pfnDestruct = */            xhciR3Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               xhciR3Reset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              NULL,
    /* .pfnDetach = */              NULL,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        NULL,
    /* .pfnPowerOff = */            NULL,
    /* .pfnSoftReset = */           NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RING0)
    /* .pfnEarlyConstruct = */      NULL,
    /* .pfnConstruct = */           xhciRZConstruct,
    /* .pfnDestruct = */            NULL,
    /* .pfnFinalDestruct = */       NULL,
    /* .pfnRequest = */             NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RC)
    /* .pfnConstruct = */           xhciRZConstruct,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#else
# error "Not in IN_RING3, IN_RING0 or IN_RC!"
#endif
    /* .u32VersionEnd = */          PDM_DEVREG_VERSION
};

#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */
