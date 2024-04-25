# File ".env" contains the next variables:
# IDL_DIR - Folder where VirtualBox API XIDL file lives (VirtualBox.xidl)
# DEST_REMOTE_PYTHON_DIR - Folder where you flask server will live
include .env
export

# The PYTHON_PACKAGE_NAME must be equal to the string "packageName" in the file config.json.
# Example:
# {
#  "apiPackage" : "VBoxRestAPI",
#  "serverPort" : 8080,
#  "packageVersion": "0.0.1",
#  "packageName": "vbox_server"
# }
#
# The file restapi-header.yml contains the header template for Swagger API.
# The section "info" contains the API version. Check it and fix if it's needed.
# Example:
#  info:
#    version: 0.0.1
#
# The root attribute "basePath" contains the API version too. Check it and fix if it's needed.
# Example:
#  basePath: /virtualbox/0.0.1
#
# All these atributes\variables\paths\lines must contain the same packageName and packageVersion.
#
PYTHON_PACKAGE_NAME=vbox_server

#Hack for java on Windows. Java doesn't recognize a path in Cygwin form. See the comments below.
#But when the '.' is used as the path to the current directory it helps on Windows.
ifeq ($(OS),Windows_NT)
	CURDIR='.'
endif

#Possible values are:
#'swagger20' - for Swagger2.0
GENERATOR_TYPE=swagger20

#Used as name convention to point out which case style is used for the attributes\parameters names
CASE_STYLE=camel #available values are 'snake' and 'camel'

SRC_DIR=$(CURDIR)/vbox_server
TOOLS_DIR=$(CURDIR)/tools

SRC_XSL_DIR=$(CURDIR)/xlst/$(GENERATOR_TYPE)
SRC_YAML_DIR=$(CURDIR)/yaml/$(GENERATOR_TYPE)

SRC_MODEL_DIR=$(SRC_DIR)/models
SRC_CONTROLLER_DIR=$(SRC_DIR)/controllers
SRC_INT_MODEL_DIR=$(SRC_MODEL_DIR)/internal
SRC_INT_CONTROLLER_DIR=$(SRC_CONTROLLER_DIR)/internal

DEST_DIR=$(CURDIR)/out
DEST_CODE_DIR=$(DEST_DIR)/$(PYTHON_PACKAGE_NAME)

DEST_YAML_DIR=$(DEST_DIR)/yaml

DEST_DOCS_DIR=$(DEST_DIR)/html

DEST_UTILS_DIR=$(DEST_CODE_DIR)/utils

DEST_MODEL_DIR=$(DEST_CODE_DIR)/models
DEST_CONTROLLER_DIR=$(DEST_CODE_DIR)/controllers
DEST_INT_MODEL_DIR=$(DEST_MODEL_DIR)/internal
DEST_INT_CONTROLLER_DIR=$(DEST_CONTROLLER_DIR)/internal

SRC_REST_API_DIR=$(DEST_YAML_DIR)

GENERATOR_CONFIG_FILE = $(CURDIR)/config.json

CASE_STYLE=camel

GENERATOR=swagger-codegen-cli.jar
GENERATOR_FLAG = -l
OPTIONS=$(GENERATOR_FLAG) python-flask -c $(GENERATOR_CONFIG_FILE)

COMMON_XSLT_OPTIONS += -param case_style $(CASE_STYLE)

export CLASSPATH=$(TOOLS_DIR)/bin/xalan-2.4.1.jar:$(TOOLS_DIR)/bin/xercesImpl-2.2.1.jar:$(TOOLS_DIR)/bin/xml-apis.jar

all: preparations api_generation code_generation docs

preparations:
	mkdir -p $(DEST_DIR)
	mkdir -p $(DEST_CODE_DIR)
	mkdir -p $(DEST_YAML_DIR)
	mkdir -p $(DEST_DOCS_DIR)
	mkdir -p $(DEST_UTILS_DIR)
	mkdir -p $(DEST_INT_MODEL_DIR)
	mkdir -p $(DEST_INT_CONTROLLER_DIR)

	echo $$CLASSPATH
	echo $$CURDIR

api_generation: enums objects methods requestbody fullapi

code_generation: code

test: test_schemas

final: copy

# The variable CLASSPATH isn't picked up properly on Windows
# That's why the path to Xalan is added directly via the parameter "-classpath"
# Apart from that, the error occurs when "-classpath" has the Cygwin form of path, for example:
# java -classpath /cygdrive/d/workspace/git_workspace/vbox-api-xlst-transform/tools/bin/xalan-2.4.1.jar
# org.apache.xalan.xslt.Process -in D:\workspace\git_workspace\vbox-api-xlst-transform\src\idl/VirtualBox.xidl -xsl
# /cygdrive/d/workspace/git_workspace/vbox-api-xlst-transform/src/xlst/restapi-enumerations.xsl -out
# /cygdrive/d/workspace/git_workspace/vbox-api-xlst-transform/out/yaml/restapi-enumerations.yml
#
# Error: Could not find or load main class org.apache.xalan.xslt.Process
# Caused by: java.lang.ClassNotFoundException: org.apache.xalan.xslt.Process

enums:
	java -classpath $(TOOLS_DIR)/bin/xalan-2.4.1.jar org.apache.xalan.xslt.Process \
	$(COMMON_XSLT_OPTIONS) \
	-in $(IDL_DIR)/VirtualBox.xidl \
	-xsl $(SRC_XSL_DIR)/restapi-enumerations.xsl \
	-out $(DEST_YAML_DIR)/restapi-enumerations.yaml

objects:
	java -classpath $(TOOLS_DIR)/bin/xalan-2.4.1.jar org.apache.xalan.xslt.Process \
	$(COMMON_XSLT_OPTIONS) \
	-in $(IDL_DIR)/VirtualBox.xidl \
	-xsl $(SRC_XSL_DIR)/restapi-objects.xsl \
	-out $(DEST_YAML_DIR)/restapi-objects.yaml

methods:
	java -classpath $(TOOLS_DIR)/bin/xalan-2.4.1.jar org.apache.xalan.xslt.Process \
	$(COMMON_XSLT_OPTIONS) \
	-in $(IDL_DIR)/VirtualBox.xidl \
	-xsl $(SRC_XSL_DIR)/restapi-methods.xsl \
	-out $(DEST_YAML_DIR)/restapi-methods.yaml

requestbody:
	java -classpath $(TOOLS_DIR)/bin/xalan-2.4.1.jar org.apache.xalan.xslt.Process \
	$(COMMON_XSLT_OPTIONS) \
	-in $(IDL_DIR)/VirtualBox.xidl \
	-xsl $(SRC_XSL_DIR)/restapi-request-body-definitions.xsl \
	-out $(DEST_YAML_DIR)/restapi-request-body-definitions.yaml

fullapi:
	cat $(SRC_YAML_DIR)/restapi-header.yaml \
	$(DEST_YAML_DIR)/restapi-enumerations.yaml \
	$(DEST_YAML_DIR)/restapi-request-body-definitions.yaml \
	$(DEST_YAML_DIR)/restapi-objects.yaml \
	$(DEST_YAML_DIR)/restapi-methods.yaml \
	$(SRC_YAML_DIR)/restapi-footer.yaml \
	> $(DEST_YAML_DIR)/restapi.yaml

code:
	@echo "@@@@@@@@@@@@@@@@ Used $(GENERATOR) @@@@@@@@@@@@@@@@"
	java -jar $(TOOLS_DIR)/bin/$(GENERATOR) generate $(OPTIONS) -i $(DEST_YAML_DIR)/restapi.yaml -o $(DEST_DIR)

docs: html_generation

html_generation:
	java -jar $(TOOLS_DIR)/bin/$(GENERATOR) generate \
	-i $(DEST_YAML_DIR)/restapi.yaml \
	-c $(GENERATOR_CONFIG_FILE) $(GENERATOR_FLAG) html \
	-o $(DEST_DOCS_DIR)

copy:
# Generated file contains a recursive import:
# from vbox_server.models.virtual_box_error_info import VirtualBoxErrorInfo
# As temporarily workaround the same file is used but without such import
	cp $(SRC_INT_MODEL_DIR)/i_virtual_box_error_info.py $(DEST_MODEL_DIR)/virtual_box_error_info.py

# Found an error in swagger-codegen. The version started from 2.4.31 contains the following error:
# The generated file "__init__.py" in the output folder "controllers" isn't empty as should be, moreover one contains
# some binary characters. It leads to the error "ValueError: source code string cannot contain null bytes" during
# launching Python code.
# As a temporarily workaround the corrupted file is replaced by our local version.
	cp -f $(SRC_INT_CONTROLLER_DIR)/__init__.py $(DEST_CONTROLLER_DIR)

# Creating the folder 'static' and put swagger.yaml into it under the name virtualbox.yaml
# this step is needed for a correct work with Flask Swagger UI. Swagger UI works properly only when the
# the API definition sits in this folder.
	mkdir -p $(DEST_CODE_DIR)/static
	mv $(DEST_CODE_DIR)/swagger/swagger.yaml $(DEST_CODE_DIR)/static/virtualbox.yaml
	rmdir $(DEST_CODE_DIR)/swagger

# Copy the rest to the output folder
	cp -r $(SRC_DIR)/utils $(DEST_CODE_DIR)
	cp $(SRC_DIR)/__init__.py $(SRC_DIR)/global_settings.py $(SRC_DIR)/wsgi.py $(DEST_CODE_DIR)
	cp -r $(SRC_DIR)/controllers/internal/* $(DEST_INT_CONTROLLER_DIR)

clean:
	rm -rf out

