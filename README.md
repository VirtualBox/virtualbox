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

## Preparation

### Downloading VirtualBox SDK
- Download an appropriate VirtualBox SDK as a standalone package.
- Unpack SDK.
    > **NOTE!**
    >  Below the folder with SDK is called "VBox SDK folder".

    > **NOTE!** 
    > If you have a development environment you might install Python binding from there. There is no need to download SDK in this case.
### Cloning the project
```git clone https://linux-git.oraclecorp.com/vportnya/vboxrestapi.git```
> **NOTE!**
> Below the folder with the cloned project is called "project folder" or "vboxrestapi".
### VirtualBox API IDL description
Go into the project folder\
```cd vboxrestapi```\
Choose one of the next cases:
1. GitLab (runner)\
Set the environment variable VBOX_XIDL_FILE globally for all users on the machine where the\ GitLab runner lives to use your own variant of VirtualBox.xidl. Or specially for gitlab-runner user.\
gitlab-runner user must have an access to this file.\
Othertwise VirtualBox.xidl from the local "idl" folder will be used as a fallback.
2. Your own VirtualBox.xidl\
Copy your VirtualBox.xidl into the folder "idl".
3. Default\
Default VirtualBox.xidl from the folder "idl" is used.
### Check Java on the machine
```
java --version
javac --version
```
## Build the project
Run ```make```\
The artifacts will be placed into the "out" folder.\
The folder "out/vbox_server" will contain the server code.
## Destination server folder and Python virtual environment
Go to a folder where the server will live.
> **NOTE!**
> Below it's called "destination folder".
### Create a python virtual environment
Run ```python -m venv ./```
### Activate the environment
- Windows:
    ```Scripts\activate```
- Linux:
    ```source bin/activate```
### Install VirtualBox python binding
Go to the VBox SDK installation folder (inside VirtualBox SDK folder)\
```cd /path/to/<virualbox-sdk-folder>/sdk/installer```\
and run\
```python vboxapisetup.py install```
## Copy the server code into the destination server folder
- Go to the destination folder\
```cd <destination folder>```
- Copy the code from the `<project folder>/out/vbox_server` into the destination folder\
```cp -r <project folder>/out/vbox_server ./```
- Copy requirements.txt file into the destination folder\
```cp <project folder>/out/vbox_server/requirements.txt ./```
## Install the requirements
- Run ```pip install -r requirements.txt```
- Check the packages versions
  - Run `flask --version`. The version mustn't be higher than 2.3.

    Example of output:

    _Python 3.8.0\
    Flask 2.2.\
    Werkzeug 2.3.8_

  - Run `connexion --version`. The version mustn't be higher than 2.14.1.
    Example of output:

    _Connexion 2.14.1_
## Set the environment variable FLASK_APP
- Windows:\
    ```set FLASK_APP=vbox_server.wsgi:application```
- Linux:\
    ```export FLASK_APP=vbox_server.wsgi:application```
## Start server
Run\
```python -m flask run --port=8080```\
or\
```flask --app=vbox_server.wsgi run --port=8080```\
or\
```flask run --port=8080```

If all is correct you will see the output like:
```
    Serving Flask app vbox_server.wsgi:application
    Debug mode: off
    [internal.py:187 -  _log() ] WARNING: This is a development server. Do not use it in a
    production deployment. Use a production WSGI server instead.
    Running on all addresses (0.0.0.0)
    Running on http://127.0.0.1:8080
```

If you want a server to be visible across the network you can add "--host=0.0.0.0" to the command line. 

## Using a web browser
open a web browser on the page http://localhost:8080/virtualbox/{apiversion}/api

[Current API version](#current-api-version)

Example:\
    _http://localhost:8080/virtualbox/0.0.1/api_

If all is correct you will see the home page with Swagger UI displaying VBox REST API home page

![Example: VirtualBox Swagger UI page](assets/home_page_example.png)
## Roadmap

## Authors and acknowledgment
Valery Portnyagin - valery.portnyagin@oracle.com

## License
[MIT licence](https://opensource.org/license/mit)

## Project status
Active