' $Id: vboxinfo.vb 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
'' @file
' ???
'

'
' Copyright (C) 2009-2024 Oracle and/or its affiliates.
'
' This file is part of VirtualBox base platform packages, as
' available from https://www.virtualbox.org.
'
' This program is free software; you can redistribute it and/or
' modify it under the terms of the GNU General Public License
' as published by the Free Software Foundation, in version 3 of the
' License.
'
' This program is distributed in the hope that it will be useful, but
' WITHOUT ANY WARRANTY; without even the implied warranty of
' MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
' General Public License for more details.
'
' You should have received a copy of the GNU General Public License
' along with this program; if not, see <https://www.gnu.org/licenses>.
'
' SPDX-License-Identifier: GPL-3.0-only
'

Imports System
Imports System.Drawing
Imports System.Windows.Forms

Module Module1

    Sub Main()
        Dim vb As VirtualBox.IVirtualBox
        Dim listBox As New ListBox()
        Dim form As New Form

        vb = CreateObject("VirtualBox.VirtualBox")

        form.Text = "VirtualBox version " & vb.Version
        form.Size = New System.Drawing.Size(400, 320)
        form.Location = New System.Drawing.Point(10, 10)

        listBox.Size = New System.Drawing.Size(350, 200)
        listBox.Location = New System.Drawing.Point(10, 10)

        For Each m In vb.Machines
            listBox.Items.Add(m.Name & " [" & m.Id & "]")
        Next

        form.Controls.Add(listBox)

        'form.ShowDialog()
        form.Show()
        MsgBox("OK")
    End Sub

End Module

