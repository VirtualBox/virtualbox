/* $Id: tstDragAndDropQt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Drag and drop Qt code test cases.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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

#include <QtWidgets>

class DnDWin : public QWidget
{
    public:

        explicit DnDWin(QWidget *parent = nullptr) : QWidget(parent)
        {
             setMinimumSize(400, 400);
             setWindowTitle("Drag me!");
             setAcceptDrops(true);
        }

        void mouseMoveEvent(QMouseEvent *event)
        {
            if(!(event->buttons() & Qt::LeftButton))
                return DnDWin::mouseMoveEvent(event);

            event->accept();

            QDrag *drag = new QDrag(this);

            QMimeData *mime = new QMimeData();
            mime->setData("text/plain", QString("/tmp/%1").arg("foo.bar").toLatin1());
            mime->setData("text/uri-list", QString("file:///tmp/%1").arg("foo.bar").toLatin1());

            drag->setMimeData(mime);
            drag->exec();
        }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    DnDWin win;
    win.show();

    app.exec();
}
