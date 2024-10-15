# $Id: win-vbox-net-uninstall.ps1 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
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

Function DeleteUnmatchingKeys ($title_text, $reg_key)
{
   $ghostcon = @(Get-ChildItem ($reg_key) | Where-Object { !$connections.ContainsKey($_.PSChildName) } )
   if ($ghostcon.count -eq 0) {
      Write-Host "`nNo ghost connections has been found -- nothing to do"
   } else {
         Write-Host "`nParameter keys for the following connections will be removed:"
         Write-Host ($ghostcon | Out-String)

      $result = AskForConfirmation $title_text `
          "Do you want to delete the keys listed above?" `
          "Deletes all ghost connection keys from the registry." `
          "No modifications to the registry will be made."

      switch ($result)
         {
            0 {$ghostcon.GetEnumerator() | ForEach-Object { Remove-Item -Path $_ -Recurse }}
            1 {"Removal cancelled."}
         }
   }
}


Push-Location
cd "Registry::"
Write-Host "Retrieving valid connections:"
$iftypes = @{}
$connections = @{}
$ghostcon_names = @{}
Get-Item ".\HKLM\SYSTEM\CurrentControlSet\Control\Class\{4D36E972-E325-11CE-BFC1-08002BE10318}\0*" | `
   ForEach-Object {
      $prop = (Get-ItemProperty $_.PSPath)
      $conn = $null
      if (Test-Path ("HKLM\SYSTEM\CurrentControlSet\Control\Network\{4D36E972-E325-11CE-BFC1-08002BE10318}\" + $prop.NetCfgInstanceId + "\Connection")) {
         $conn = (Get-ItemProperty ("HKLM\SYSTEM\CurrentControlSet\Control\Network\{4D36E972-E325-11CE-BFC1-08002BE10318}\" + $prop.NetCfgInstanceId + "\Connection"))
      }
      $iftype = $prop."*IfType"
      if ($iftypes.ContainsKey($iftype)) {
         $iftypes[$iftype] = $iftypes[$iftype] + [Math]::pow(2,$prop.NetLuidIndex)
      } else {
         $iftypes[$iftype] = [Math]::pow(2,$prop.NetLuidIndex)
      }
      if ($conn -ne $null) {
         $connections[$prop.NetCfgInstanceId] = $conn.Name
         Write-Host $prop.NetCfgInstanceId $conn.Name "|" $prop."*IfType" $prop.NetLuidIndex $prop.DriverDesc
      } else {
         Write-Host $prop.NetCfgInstanceId [MISSING] "|" $prop."*IfType" $prop.NetLuidIndex $prop.DriverDesc
      }
   }

# Someday we may want to process other types than Ethernet as well: $iftypes.GetEnumerator() | ForEach-Object {
if ($iftypes[6] -gt 9223372036854775808) {
   Write-Host "Found more than 63 interfaces (mask=" $iftypes[6] ") -- bailing out"
   exit
}
Write-Host "`nChecking if the used LUID index mask is correct:"
$correctmask = [BitConverter]::GetBytes([int64]($iftypes[6]))
$actualmask = (Get-ItemProperty -Path "HKLM\SYSTEM\CurrentControlSet\Services\NDIS\IfTypes\6" -Name "IfUsedNetLuidIndices").IfUsedNetLuidIndices
$needcorrection = $FALSE
$ai = 0
$lastnonzero = 0
for ($ci = 0; $ci -lt $correctmask.Length; $ci++) {
   if ($ai -lt $actualmask.Length) {
      $aval = $actualmask[$ai++]
   } else {
      $aval = 0
   }
   if ($correctmask[$ci] -ne 0) {
      $lastnonzero = $ci
   }
   if ($correctmask[$ci] -eq $aval) {
      Write-Host "DEBUG: " $correctmask[$ci].ToString("X2") " == " $aval.ToString("X2")
   } else {
      Write-Host "DEBUG: " $correctmask[$ci].ToString("X2") " != " $aval.ToString("X2")
      $needcorrection = $TRUE
   }
}
if ($ai -lt $actualmask.Length) {
   for (; $ai -lt $actualmask.Length; $ai++) {
      if ($actualmask[$ai] -eq 0) {
         Write-Host "DEBUG: 0 == 0"
      } else {
         Write-Host "DEBUG: " $actualmask[$ai].ToString("X2") " != 0"
         $needcorrection = $TRUE
      }
   }
}
if ($needcorrection) {
   Write-Host "Current mask is " ($actualmask|foreach {$_.ToString("X2")}) ", while it should be" ($correctmask|foreach {$_.ToString("X2")})
   if ($confirm) {
      Set-ItemProperty -Path "HKLM\SYSTEM\CurrentControlSet\Services\NDIS\IfTypes\6" -Name "IfUsedNetLuidIndices" -Value $correctmask -Type Binary -Confirm
   } else {
      Set-ItemProperty -Path "HKLM\SYSTEM\CurrentControlSet\Services\NDIS\IfTypes\6" -Name "IfUsedNetLuidIndices" -Value $correctmask -Type Binary
   }
} else {
   Write-Host "The used LUID index mask is correct -- nothing to do"
}

#Write-Host ($connections | Out-String)
$ghostcon = @(Get-ChildItem ("HKLM\SYSTEM\CurrentControlSet\Control\Network\{4D36E972-E325-11CE-BFC1-08002BE10318}") | Where-Object { !$connections.ContainsKey($_.PSChildName) -and $_.PSChildName -ne "Descriptions" } )
if ($ghostcon -eq $null) {
   Write-Host "`nNo ghost connections has been found -- nothing to do"
} else {
   Write-Host "`nThe following connections will be removed:"
   #Write-Host ($ghostcon | Out-String)

   $ghostcon.GetEnumerator() | ForEach-Object {
      $prop = (Get-ItemProperty "$_\Connection")
      if ($prop.PnPInstanceId -eq $null) {
         Write-Host "WARNING! PnPInstanceId does not exist for" $_.PSChildName
      } elseif (!($prop.PnPInstanceId.ToString() -match "SUN_VBOXNETFLTMP")) {
         Write-Host "WARNING! PnPInstanceId (" $prop.PnPInstanceId.ToString() ") does not match ROOT\SUN_VBOXNETFLTMP for" $_.PSChildName
      }
      if ($prop.Name -eq $null) {
         Write-Host "WARNING! Name does not exist for" $_.PSChildName
      } else {
         $ghostcon_names.Add($_.PSChildName, $prop.Name)
         Write-Host $_.PSChildName -nonewline
         Write-Host "  " -nonewline
         Write-Host $prop.Name
      }
   }

   $result = AskForConfirmation "Delete Registry Keys" `
          "Do you want to delete the keys listed above?" `
          "Deletes all ghost connection keys from the registry." `
          "No modifications to the registry will be made."

   switch ($result)
       {
           0 {$ghostcon.GetEnumerator() | ForEach-Object { Remove-Item -Path $_.PSPath -Recurse }}
           1 {"Removal cancelled."}
       }
}

# Delete WFPLWFS parameter keys
DeleteUnmatchingKeys "Delete WFPLWFS Parameter Keys (Adapter subkey)" "HKLM\SYSTEM\CurrentControlSet\Services\WFPLWFS\Parameters\Adapters"
DeleteUnmatchingKeys "Delete WFPLWFS Parameter Keys (NdisAdapter subkey)" "HKLM\SYSTEM\CurrentControlSet\Services\WFPLWFS\Parameters\NdisAdapters"
# Delete Psched parameter keys
DeleteUnmatchingKeys "Delete Psched Parameter Keys (Adapter subkey)" "HKLM\SYSTEM\CurrentControlSet\Services\Psched\Parameters\Adapters"
DeleteUnmatchingKeys "Delete Psched Parameter Keys (NdisAdapter subkey)" "HKLM\SYSTEM\CurrentControlSet\Services\Psched\Parameters\NdisAdapters"

# Clean up NSI entries
$nsi_obsolete = New-Object System.Collections.ArrayList
$nsi_path = "HKLM\SYSTEM\CurrentControlSet\Control\Nsi\{EB004A11-9B1A-11D4-9123-0050047759BC}\10"
$nsi = (Get-Item $nsi_path) | Select-Object -ExpandProperty property
$nsi | ForEach-Object {
   $value = (Get-ItemProperty -Path $nsi_path -Name $_).$_
   [byte[]]$guid_bytes = $value[1040..1055]
   $guid = New-Object -TypeName System.Guid -ArgumentList (,$guid_bytes)
   $guid_string = $guid.ToString("B").ToUpper()
   $nsi_conn_name_last = 6 + $value[4] + $value[5]*256
   $nsi_conn_name = [Text.Encoding]::Unicode.GetString($value[6..$nsi_conn_name_last])
   $nsi_if_name_last = 522 + $value[520] + $value[521]*256
   $nsi_if_name = [Text.Encoding]::Unicode.GetString($value[522..$nsi_if_name_last])
   Write-Host $_ -nonewline
   Write-Host "  " -nonewline
   Write-Host $guid_string -nonewline
   Write-Host "  " -nonewline
   if ($connections.ContainsKey($guid_string)) {
      Write-Host $nsi_if_name
   } else {
      [void] $nsi_obsolete.Add($_)
      Write-Host "[OBSOLETE] " $nsi_if_name -foregroundcolor red
   }
}

$result = AskForConfirmation "Delete NSI Entries" `
        "Do you want to delete the entries marked in red above?" `
        "Deletes all marked entries from the NSI registry key." `
        "No modifications to the registry will be made."

switch ($result)
    {
        0 {$nsi_obsolete.GetEnumerator() | ForEach-Object { Remove-ItemProperty -Path $nsi_path -Name $_ }}
        1 {"Removal cancelled."}
    }

# Clean up uninstalled connections
if ( (Get-ChildItem "HKLM\SYSTEM\CurrentControlSet\Control\Network\Uninstalled" | Measure-Object).Count -gt 10 ) {
   $result = AskForConfirmation "Delete Uninstalled Network Connection Registry Keys" `
          "There are over 10 uninstalled network connections accumulated in the registry. Do you want to delete them?" `
          "Deletes uninstalled connection keys from the registry." `
          "No modifications to the registry will be made."

   switch ($result)
       {
           0 {Remove-Item -Path "HKLM\SYSTEM\CurrentControlSet\Control\Network\Uninstalled\*" -Recurse}
           1 {"Removal cancelled."}
       }
} else {
   Write-Host "Less than 10 uninstalled connections -- no action yet required."
}

Pop-Location
