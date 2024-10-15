/* $Id: vbox-tpm.dsl 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox ACPI - TPM ACPI device.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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

DefinitionBlock ("SSDT.aml", "SSDT", 1, "VBOX  ", "VBOXTPMT", 2)
{
    External(DBG, MethodObj, ,)

    Scope (\_SB)
    {
        Device (TPM)
        {
            Method (_HID, 0, NotSerialized)
            {
                If (LEqual(IFID, One))
                {
                    Return ("PNP0C31")
                }
                Else
                {
                    Return ("MSFT0101")
                }
            }

            Method (_CID, 0, NotSerialized)
            {
                If (LEqual(IFID, One))
                {
                    Return ("PNP0C31")
                }
                Else
                {
                    Return ("MSFT0101")
                }
            }

            Method (_STR, 0, NotSerialized)
            {
                If (LEqual(IFID, One))
                {
                    Return (Unicode ("TPM 1.2 Device"))
                }
                Else
                {
                    Return (Unicode ("TPM 2.0 Device"))
                }
            }

            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            OperationRegion (TPMR, SystemMemory, 0xFED40000, 0x5000)
            Field(TPMR, AnyAcc, NoLock, Preserve)
            {
                Offset(0x30),
                IFID,       1,
            }

            Name(RES, ResourceTemplate()
            {
                Memory32Fixed (ReadWrite, 0xfed40000, 0x5000, REG1)
            })

            Method (_CRS, 0, Serialized)
            {
               Return (RES)
            }

            Method (TPFS, 1, Serialized)
            {
                If (LGreaterEqual(Arg0, 0x100))
                {
                    Return (Zero)
                }

                OperationRegion (TPP1, SystemMemory, Add(0xFED45000, Arg0), One)
                Field (TPP1, ByteAcc, NoLock, Preserve)
                {
                    TPPF,   8
                }

                Return (TPPF)
            }

            /**
             *  Device-Specific Method
             */
            Method (_DSM, 4, Serialized)
            {
                /**
                 * The TPM Physical Presence Interface MMIO region.
                 */
                OperationRegion (TPMP, SystemMemory, 0xFED45100, 0x5A)
                Field (TPMP, AnyAcc, NoLock, Preserve)
                {
                    PPIN,   8,
                    PPIP,   32,
                    PPRP,   32,
                    PPRQ,   32,
                    PPRM,   32,
                    LPPR,   32
                }

                Name (TPB2, Package (0x02)
                {
                    Zero,
                    Zero
                })

                Name (TPB3, Package (0x03)
                {
                    Zero,
                    Zero,
                    Zero
                })

                /**
                 * Physical Presence Interface Specification PPI.
                 */
                If (LEqual (Arg0, ToUUID("3dddfaa6-361b-4eb4-a424-8d10089d1653")))
                {
                    /**
                     * Standard _DSM query function.
                     */
                    If (LEqual (Arg2, Zero))
                    {
                        DBG("_DSM: Query\n")
                        Return (Buffer (0x02) { 0xFF, 0x01 })
                    }

                    /**
                     * Query supported PPI revision.
                     *
                     * Result:
                     *    1.3 (string).
                     */
                    If (LEqual (Arg2, One))
                    {
                        DBG("_DSM: PPI Revision\n")
                        Return ("1.3")
                    }

                    /**
                     * Submit TPM Operation Request to pre-OS environment.
                     *
                     * Input:
                     *     Package[0] - Operation value of the request
                     * Result:
                     *     - 0: Success
                     *     - 1: Operation value of the request not supported.
                     *     - 2: General failure
                     */
                    If (LEqual (Arg2, 0x02))
                    {
                        DBG("_DSM: Submit TPM Operation Request\n")

                        Store(DerefOf(Index(Arg3, Zero)), Local0)
                        Store(TPFS(Local0), Local1)
                        If (LEqual(And(Local1, 0x07), Zero))
                        {
                            Return (One)
                        }

                        Store(Local0, PPRQ)
                        Store(Zero, PPRM)
                        Return (Zero)
                    }

                    /**
                     * Get Pending TPM Operation Requested by the OS.
                     *
                     * Result:
                     *     Package[0] - Function Return Code:
                     *         - 0: Success
                     *         - 1: General Failure
                     *     Package[1] - Pending operation requested by the OS:
                     *         -  0: None
                     *         - >0: Operation value of the pending request
                     *     Package[2] - Optional argument to pending operation requested by the OS:
                     *         -  0: None
                     *         - >0: Argument value of the pending request
                     */
                    If (LEqual (Arg2, 0x03))
                    {
                        DBG("_DSM: Get Pending TPM Operation Request\n")

                        if (LEqual(Arg1, One))
                        {
                            Store(PPRQ, Index(TPB2, One))
                            Return (TPB2)
                        }

                        if (LEqual(Arg1, 0x02))
                        {
                            Store(PPRQ, Index(TPB3, One))
                            Store(PPRM, Index(TPB3, 0x02))
                            Return (TPB3)
                        }

                        Return (TPB3)
                    }

                    /**
                     * Get Platform-Specific Action to Transition to Pre-OS Environment.
                     *
                     * Result:
                     *     - 0: None
                     *     - 1: Shutdown
                     *     - 2: Reboot
                     *     - 3: OS vendor specific
                     */
                    If (LEqual (Arg2, 0x04))
                    {
                        DBG("_DSM: Get Platform-Specific Action to Transition to Pre-OS Environment\n")

                        Return (0x02)
                    }

                    /**
                     * Return TPM Operation Response to OS Environment.
                     */
                    If (LEqual (Arg2, 0x05))
                    {
                        DBG("_DSM: Return TPM Operation Response to OS Environment\n")

                        Store (LPPR, Index (TPB3, One))
                        Store (PPRP, Index (TPB3, 0x02))
                        Return (TPB3)
                    }

                    /**
                     * Submit preferred user language - deprecated
                     *
                     * Result:
                     *     - 3: Not implemented
                     */
                    If (LEqual (Arg2, 0x06))
                    {
                        DBG("_DSM: Submit preferred user language\n")

                        Return (0x03)
                    }

                    /**
                     * Submit TPM Operation Request to Pre-OS Environment 2
                     */
                    If (LEqual (Arg2, 0x07))
                    {
                        DBG("_DSM: Submit TPM Operation Request 2\n")

                        Store(DerefOf(Index(Arg3, Zero)), Local0)   /* Local0 = *Arg3[0] (Arg3 is a Package) */
                        Store(TPFS(Local0), Local1)                 /* Local1 = TPFS(Local0) */
                        Store(And(Local1, 0x07), Local1)            /* Local1 &= 0x7 */
                        If (LEqual(Local1, Zero))
                        {
                            Return (One)                            /* Operation not implemented */
                        }

                        If (LEqual(Local1, 0x02))
                        {
                            Return (0x03)                           /* Operation blocked by current firmware settings */
                        }

                        If (LEqual(Arg1, One))
                        {
                            Store(Local0, PPRQ)
                            Store(Zero,   PPRM)
                        }

                        If (LEqual(Arg1, 0x02))
                        {
                            Store(DerefOf(Index(Arg3, One)), Local2) /* Local2 = *Arg3[1] (Arg3 is a Package) */

                            Store(Local0, PPRQ)
                            Store(Local2, PPRM)
                        }

                        Return (Zero)
                    }

                    /**
                     * Get User Confirmation Status for Operation.
                     *
                     * Input is the operation value maybe needing user confirmation
                     * Result:
                     *     - 0: Not implemented
                     *     - 1: Firmware only
                     *     - 2: Blocked for OS by firmware configuration.
                     *     - 3: Allowed and physically present user required
                     *     - 4: Allowed and physically present user not required.
                     */
                    If (LEqual (Arg2, 0x08))
                    {
                        DBG("_DSM: Get user confirmation status for operation\n")

                        Store(DerefOf(Index(Arg3, Zero)), Local0)
                        Store(TPFS(Local0), Local1)

                        Return (And(Local1, 0x7))
                    }

                    DBG("TPM_DSM: Unknown function\n")
                    Return (Buffer (One) { 0x00 })
                }

                /**
                 * TCG Platform Reset Attack Mitigation Specification interface.
                 */
                If (LEqual (Arg0, ToUUID("376054ed-cc13-4675-901c-4756d7f2d45d")))
                {
                    /**
                     * Standard _DSM query function.
                     */
                    If (LEqual (Arg2, Zero))
                    {
                        Return (Buffer (One) { 0x03 })
                    }

                    /**
                     * Set Memory Overwrite Request (MOR) bit to specified value.
                     */
                    If (LEqual (Arg2, One))
                    {
                        /* Memory is always zeroed on reset. */
                        Return (Zero)
                    }

                    Return (Buffer (One) { 0x00 })
                }

                Return (Buffer (One) { 0x00 })
            }
        }
    }
}

/*
 * Local Variables:
 * comment-start: "//"
 * End:
 */

