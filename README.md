# VirtualBox Swagger/OpenAPI server

## Name
VirtualBox Swagger/OpenAPI server

## Description
The project is intended to help Virtualbox's users interact with VirtualBox server remotely. The standard example is web browser where user opens the home page of VirtualBox server and does some actions with VMs or get\set some information related to VMs.

## Prerequisities
- Python 3.8 and above
- Connexion below 2.14.1
- Flask below 2.3.0

## Current API version
0.0.1

## Installation

All platforms
1. Download an appropriate VirtualBox SDK as a standalone package and unpack it.

    > **NOTE!** 
    > If you have a development environment you might install Python binding from there. There is no need to download SDK in this case.

8. Clone the project
    ```
    git clone https://linux-git.oraclecorp.com/vportnya/vboxrestapi.git
    ```

2. Go to a folder "vboxrestapi" where the project has been cloned

3. Create a python virtual environment in this folder
	```
    python -m venv ./
    ```

5. Activate the environment 

    - Windows:
    ```Scripts\activate```
	- Linux:
    ```source bin/activate```


6. Install VirtualBox python binding inside the project python environment
    ```
    cd /path/to/<virualbox-sdk-folder>/sdk/installer
    ```
	and run 
    ```
    python vboxapisetup.py install
    ```

7. Return to the project folder "vboxrestapi"
	```
    cd /path/to/vboxrestapi
    ```

9. Install the requirements
    ```
    pip3 install -r requirements.txt
    ```

10. Check the packages versions

    10.1. Run `flask --version`. The version mustn't be higher than 2.3.

    Example of output:

    _Python 3.8.0\
    Flask 2.2.\
    Werkzeug 2.3.8_


    10.2 Run `connexion --version`. The version mustn't be higher than 2.14.1.

    Example of output:

    _Connexion 2.14.1_


11. Set the environment variable FLASK_APP

	- Windows:
    ```set FLASK_APP=vbox_server.wsgi:application```
    - Linux:
    ```export FLASK_APP=vbox_server.wsgi:application```

12. Start server

    ```python -m flask run --port=8080 --host=0.0.0.0```

    If all is correct you will see the output like:
    <em>
    * Serving Flask app 'vbox_server.wsgi:application'
    * Debug mode: off
    [_internal.py:187 -  _log() ] WARNING: This is a development server. Do not use it in a production deployment. Use a production WSGI server instead.←[0m
    * Running on all addresses (0.0.0.0)
    * Running on http://127.0.0.1:8080
    * Running on http://10.172.69.124:8080
    </em>

13. open web browser on the page http://localhost:8080/virtualbox/{apiversion}/api

    [Current API version](#current-api-version)

    If all is correct you will see the home page with Swagger UI displaying VBox REST API home page

    ![Example: VirtualBox Swagger UI page](https://linux-git.oraclecorp.com/vportnya/vboxrestapi/-/blob/170b3a9b0666464358b2d8588bc3d62dfaed5081/home_page_example.png)
## Roadmap

## Authors and acknowledgment
Valery Portnyagin - valery.portnyagin@oracle.com

## License
[MIT licence](https://opensource.org/license/mit)

## Project status
Active