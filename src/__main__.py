#!/usr/bin/env python3

import connexion

from vbox_server import encoder

import gc
import logging

from vbox_server import global_settings
from flask_swagger_ui import get_swaggerui_blueprint
from flask_cors import CORS

FORMAT = "[%(filename)s:%(lineno)s - %(funcName)20s() ] %(message)s"
logging.basicConfig(format=FORMAT)

def create_app():
    app = connexion.FlaskApp(__name__, specification_dir='static/')
    app.app.json_encoder = encoder.JSONEncoder
    app.add_api('virtualbox.yaml', arguments={'title': 'VirtualBox REST API'})

    # add CORS support
    CORS(app.app)

    ### setup API url for Swagger UI ###
    SWAGGER_URL = '/virtualbox/0.0.1/api'
    API_URL = '/static/virtualbox.yaml'
    SWAGGERUI_BLUEPRINT = get_swaggerui_blueprint(
        SWAGGER_URL,
        API_URL,
        config={
            'app_name': "VirtualBox REST API"
        }
    )
    app.app.register_blueprint(SWAGGERUI_BLUEPRINT, url_prefix=SWAGGER_URL)

    return app

if __name__ == '__main__':
    application = create_app()
    application.run()

    for sKey in list(global_settings.ctx.keys()):
        del global_settings.ctx[sKey]
    global_settings.ctx = None
    
    gc.collect()

    global_settings.oVBoxMgr.deinit()
    del global_settings.oVBoxMgr