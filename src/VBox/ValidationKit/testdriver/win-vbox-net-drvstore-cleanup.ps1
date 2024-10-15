# $Id: win-vbox-net-drvstore-cleanup.ps1 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# VirtualBox Validation Kit - network cleanup script (powershell).
#

#
# Copyright (C) 2006-2024 Oracle and/or its affiliates.
#
# This file is part of VirtualBox base platform packages, as
# available from https://www.virtualbox.org.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation, in version 3 of the
# License.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <https://www.gnu.org/licenses>.
#
# The contents of this file may alternatively be used under the terms
# of the Common Development and Distribution License Version 1.0
# (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
# in the VirtualBox distribution, in which case the provisions of the
# CDDL are applicable instead of those of the GPL.
#
# You may elect to license modified versions of this file under the
# terms and conditions of either the GPL or the CDDL or both.
#
# SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
#

param([switch]$confirm)

Function AskForConfirmation ($title_text, $message_text, $yes_text, $no_text)
{
   if ($confirm) {
      $title = $title_text
      $message = $message_text

      $yes = New-Object System.Management.Automation.Host.ChoiceDescription "&Yes", $yes_text

      $no = New-Object System.Management.Automation.Host.ChoiceDescription "&No", $no_text

      $options = [System.Management.Automation.Host.ChoiceDescription[]]($yes, $no)

      $result = $host.ui.PromptForChoice($title, $message, $options, 0)
   } else {
      $result = 0
   }

   return $result
}

pnputil -e | ForEach-Object { if ($_ -match "Published name :.*(oem\d+\.inf)") {$inf=$matches[1]} elseif ($_ -match "Driver package provider :.*Oracle") {$inf + " " + $_} }

$result = AskForConfirmation "Clean up the driver store" `
        "Do you want to delete all VirtualBox drivers from the driver store?" `
        "Deletes all VirtualBox drivers from the driver store." `
        "No modifications to the driver store will be made."

switch ($result)
    {
        0 {pnputil -e | ForEach-Object { if ($_ -match "Published name :.*(oem\d+\.inf)") {$inf=$matches[1]} elseif ($_ -match "Driver package provider :.*Oracle") {$inf} } | ForEach-Object { pnputil -d $inf } }
        1 {"Removal cancelled."}
    }

