"""VBox REST API

Copyright (c) 2025 Oracle and/or its affiliates.
Licensed under the Universal Permissive License v 1.0 as shown at https://oss.oracle.com/licenses/upl

SPDX-License-Identifier: UPL-1.0
"""

#set "FLASK_APP=vbox_server.wsgi:application" to run Flask using the command "python -m flask run"
from vbox_server.__main__ import create_app
from vbox_server.utils.session_observer import SessionObserver

oSO = SessionObserver()
oSO.setDaemon(True)
oSO.start()
app = create_app()
application = app.app