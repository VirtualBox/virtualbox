#set "FLASK_APP=vbox_server.wsgi:application" to run Flask using the command "python -m flask run"
from vbox_server.__main__ import create_app
app = create_app()
application = app.app