/* $Id: tstClipboardQt.cpp 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $ */
/** @file
 * Shared Clipboard guest/host Qt code test cases.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#include <QApplication>
#include <QMainWindow>
#include <QTextEdit>
#include <QHBoxLayout>
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QMainWindow window;
    QWidget *pWidget = new QWidget;
    window.setCentralWidget(pWidget);
    QHBoxLayout *pLayout = new QHBoxLayout(pWidget);

    QTextEdit *pTextEdit = new QTextEdit;
    pLayout->addWidget(pTextEdit);


    window.show();

    app.exec();
}
