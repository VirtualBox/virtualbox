# GitLab build (runner) case:
# Set the environment variable VBOX_XIDL_FILE globally for all users on the machine where the GitLab runner lives
# to use your own variant of VirtualBox.xidl. Or specially for gitlab-runner user.
# gitlab-runner user must have the access to this file.
# Othertwise VirtualBox.xidl from the local "idl" folder will be used as fallback.
#
# Default case:
# Copy your VirtualBox.xidl into the folder "idl".
ifndef VBOX_XIDL_FILE
	VBOX_XIDL_FILE=./idl/VirtualBox.xidl
endif

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

# Hack for java on Windows. Java doesn't recognize a path in Cygwin form. See the comments below.
# But when the '.' is used as the path to the current directory it helps on Windows.
ifeq ($(OS),Windows_NT)
	CURDIR='.'
endif

# Possible values are:
# 'swagger20' - for Swagger2.0
GENERATOR_TYPE=swagger20

# Support Connexion 2 and 3. Possible values are: 2,3
CONNEXION_VERSION=3

# Possible values are:
# 'jinja', 'xslt'
INTERNAL_GENERATOR_TYPE=jinja

# Used as name convention to point out which case style is used for the attributes\parameters names
CASE_STYLE=camel #available values are 'snake' and 'camel'
CASE_STYLE_SWAGGER=modelPropertyNaming=camelCase

SRC_DIR=$(CURDIR)/vbox_server
TOOLS_DIR=$(CURDIR)/tools

SRC_XSL_DIR=$(CURDIR)/xslt/$(GENERATOR_TYPE)
SRC_YAML_DIR=$(CURDIR)/yaml/$(GENERATOR_TYPE)
SRC_JINJA_DIR=$(CURDIR)/jinja/$(GENERATOR_TYPE)

SRC_MODEL_DIR=$(SRC_DIR)/models
SRC_CONTROLLER_DIR=$(SRC_DIR)/controllers
SRC_INT_MODEL_DIR=$(SRC_MODEL_DIR)/internal
SRC_INT_CONTROLLER_DIR=$(SRC_CONTROLLER_DIR)/internal

DEST_DIR=$(CURDIR)/out
DEST_CODE_DIR=$(DEST_DIR)/$(PYTHON_PACKAGE_NAME)

DEST_YAML_DIR=$(DEST_DIR)/yaml

DEST_DOCS_DIR=$(DEST_DIR)/html

DEST_UTILS_DIR=$(DEST_CODE_DIR)/utils

DEST_INTERMEDIATE_DIR=$(DEST_DIR)/intermediate

DEST_MODEL_DIR=$(DEST_CODE_DIR)/models
DEST_CONTROLLER_DIR=$(DEST_CODE_DIR)/controllers
DEST_INT_MODEL_DIR=$(DEST_MODEL_DIR)/internal
DEST_INT_CONTROLLER_DIR=$(DEST_CONTROLLER_DIR)/internal

SRC_REST_API_DIR=$(DEST_YAML_DIR)

GENERATOR_CONFIG_FILE = $(CURDIR)/config.json

ifeq ($(CONNEXION_VERSION), 3)
	GENERATOR=swagger-codegen-cli.jar
# 	GENERATOR=swagger-codegen-cli_experimental.jar
#	GENERATOR=swagger-codegen-cli_no-snake-case-rule.jar
else
	GENERATOR=swagger-codegen-cli_connexion2.jar
endif

GENERATOR_FLAG = -l
OPTIONS=$(GENERATOR_FLAG) python-flask -c $(GENERATOR_CONFIG_FILE)

COMMON_XSLT_OPTIONS += -param case-style "$(CASE_STYLE)"

export CLASSPATH=$(TOOLS_DIR)/bin/xalan-2.4.1.jar:$(TOOLS_DIR)/bin/xercesImpl-2.2.1.jar:$(TOOLS_DIR)/bin/xml-apis.jar

all: preparations api-generation code-generation docs copy

preparations:
	mkdir -p $(DEST_DIR)
	mkdir -p $(DEST_CODE_DIR)
	mkdir -p $(DEST_YAML_DIR)
	mkdir -p $(DEST_DOCS_DIR)
	mkdir -p $(DEST_UTILS_DIR)
	mkdir -p $(DEST_INT_MODEL_DIR)
	mkdir -p $(DEST_INT_CONTROLLER_DIR)
	mkdir -p $(DEST_INTERMEDIATE_DIR)

	@echo ""
	@echo CLASSPATH is $$CLASSPATH
	@echo CURDIR is $$CURDIR
	@echo VBOX_XIDL_FILE is $(VBOX_XIDL_FILE)
	@echo ""

api-generation: fullapi

code-generation: flask-connexion enum-conversion-code object-conversion-code function-json

# The variable CLASSPATH isn't picked up properly on Windows
# That's why the path to Xalan is added directly via the parameter "-classpath"
# Apart from that, the error occurs when "-classpath" has the Cygwin form of path, for example:
# java -classpath /cygdrive/d/workspace/git_workspace/vbox-api-xlst-transform/tools/bin/xalan-2.4.1.jar \
# org.apache.xalan.xslt.Process -in D:\workspace\git_workspace\vbox-api-xlst-transform\src\idl/VirtualBox.xidl -xsl \
# /cygdrive/d/workspace/git_workspace/vbox-api-xlst-transform/src/xlst/restapi-enumerations.xsl -out \
# /cygdrive/d/workspace/git_workspace/vbox-api-xlst-transform/out/yaml/restapi-enumerations.yml
#
# Error: Could not find or load main class org.apache.xalan.xslt.Process
# Caused by: java.lang.ClassNotFoundException: org.apache.xalan.xslt.Process

enums:
	java -classpath $(TOOLS_DIR)/bin/xalan-2.4.1.jar org.apache.xalan.xslt.Process \
	$(COMMON_XSLT_OPTIONS) \
	-in $(VBOX_XIDL_FILE) \
	-xsl $(SRC_XSL_DIR)/restapi-enumerations.xsl \
	-out $(DEST_YAML_DIR)/restapi-enumerations.yaml

objects:
	java -classpath $(TOOLS_DIR)/bin/xalan-2.4.1.jar org.apache.xalan.xslt.Process \
	$(COMMON_XSLT_OPTIONS) \
	-in $(VBOX_XIDL_FILE) \
	-xsl $(SRC_XSL_DIR)/restapi-objects.xsl \
	-out $(DEST_YAML_DIR)/restapi-objects.yaml

methods:
	java -classpath $(TOOLS_DIR)/bin/xalan-2.4.1.jar org.apache.xalan.xslt.Process \
	$(COMMON_XSLT_OPTIONS) \
	-in $(VBOX_XIDL_FILE) \
	-xsl $(SRC_XSL_DIR)/restapi-methods.xsl \
	-out $(DEST_YAML_DIR)/restapi-methods.yaml

requestbody:
	java -classpath $(TOOLS_DIR)/bin/xalan-2.4.1.jar org.apache.xalan.xslt.Process \
	$(COMMON_XSLT_OPTIONS) \
	-in $(VBOX_XIDL_FILE) \
	-xsl $(SRC_XSL_DIR)/restapi-request-body-definitions.xsl \
	-out $(DEST_YAML_DIR)/restapi-request-body-definitions.yaml

fullapi: requestbody methods objects enums
	cat $(SRC_YAML_DIR)/restapi-header.yaml \
	$(DEST_YAML_DIR)/restapi-enumerations.yaml \
	$(DEST_YAML_DIR)/restapi-request-body-definitions.yaml \
	$(DEST_YAML_DIR)/restapi-objects.yaml \
	$(DEST_YAML_DIR)/restapi-methods.yaml \
	$(SRC_YAML_DIR)/restapi-footer.yaml \
	> $(DEST_YAML_DIR)/restapi.yaml

flask-connexion:
	@echo "@@@@@@@@@@@@@@@@ $(GENERATOR) @@@@@@@@@@@@@@@@"
	java -jar $(TOOLS_DIR)/bin/$(GENERATOR) generate $(OPTIONS) -i $(DEST_YAML_DIR)/restapi.yaml -o $(DEST_DIR) \
	-D$(CASE_STYLE_SWAGGER)
	cp $(DEST_CODE_DIR)/swagger/swagger.yaml $(DEST_INTERMEDIATE_DIR)/

ENUM_GENERATED_FILE = enum_conversion.py

ifeq ($(INTERNAL_GENERATOR_TYPE), jinja)
	ENUM_JSON_FILE = enumeration_list.json
	JINJA_ENUM_CODE_TEMPLATE = enum_conversion.j2
endif

enum-json:
	python $(TOOLS_DIR)/scripts/generate_enum_list_in_json.py \
	--xidl $(VBOX_XIDL_FILE) \
	--out-dir $(DEST_INTERMEDIATE_DIR) \
	--out-file $(ENUM_JSON_FILE)

enum-conversion-code: $(if $(filter jinja,$(INTERNAL_GENERATOR_TYPE)),enum-json)

enum-conversion-code:
ifeq ($(INTERNAL_GENERATOR_TYPE), jinja)
	python $(TOOLS_DIR)/scripts/enum_conversion.py \
	--in-json-file-path $(DEST_INTERMEDIATE_DIR)/$(ENUM_JSON_FILE) \
	--in-template-file-path $(SRC_JINJA_DIR)/$(JINJA_ENUM_CODE_TEMPLATE) \
	--out-dir $(DEST_UTILS_DIR) \
	--out-file $(ENUM_GENERATED_FILE)
else
	java -classpath $(TOOLS_DIR)/bin/xalan-2.4.1.jar org.apache.xalan.xslt.Process \
	$(COMMON_XSLT_OPTIONS) \
	-in $(VBOX_XIDL_FILE) \
	-xsl $(SRC_XSL_DIR)/code-enum-conversion-functions.xsl \
	-out $(DEST_UTILS_DIR)/$(ENUM_GENERATED_FILE)
endif

OBJECT_GENERATED_FILE = object_conversion.py

ifeq ($(INTERNAL_GENERATOR_TYPE), jinja)
	OBJECT_JSON_FILE = object_list.json
	JINJA_OBJECT_CODE_TEMPLATE = object_conversion.j2
endif

object-json:
	python $(TOOLS_DIR)/scripts/generate_obj_list_in_json.py \
	--yaml-api-def $(DEST_CODE_DIR)/swagger/swagger.yaml \
	--interface all \
	--out-dir $(DEST_INTERMEDIATE_DIR) \
	--out-file $(OBJECT_JSON_FILE)

object-conversion-code: $(if $(filter jinja,$(INTERNAL_GENERATOR_TYPE)),object-json)

object-conversion-code:
ifeq ($(INTERNAL_GENERATOR_TYPE), jinja)
	python $(TOOLS_DIR)/scripts/object_conversion.py \
	--in-json-file-path $(DEST_INTERMEDIATE_DIR)/$(OBJECT_JSON_FILE) \
	--in-template-file-path $(SRC_JINJA_DIR)/$(JINJA_OBJECT_CODE_TEMPLATE) \
	--out-dir $(DEST_UTILS_DIR) \
	--out-file $(OBJECT_GENERATED_FILE)
else
	java -classpath $(TOOLS_DIR)/bin/xalan-2.4.1.jar org.apache.xalan.xslt.Process \
	$(COMMON_XSLT_OPTIONS) \
	-in $(VBOX_XIDL_FILE) \
	-xsl $(SRC_XSL_DIR)/code-restapi-objects-functions.xsl \
	-out $(DEST_UTILS_DIR)/$(OBJECT_GENERATED_FILE)
endif

FUNCTION_JSON_FILE = function_list.json

function-json:
ifeq ($(INTERNAL_GENERATOR_TYPE), jinja)
	python $(TOOLS_DIR)/scripts/generate_function_list_in_json.py \
	--yaml-api-def $(DEST_CODE_DIR)/swagger/swagger.yaml \
	--interface all \
	--out-dir $(DEST_INTERMEDIATE_DIR) \
	--out-file $(FUNCTION_JSON_FILE)
endif

docs: html-docs

html-docs:
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

# Delete unused folder
	rm -rf $(DEST_CODE_DIR)/test

# Copy the rest to the output folder
	cp -r --update $(SRC_DIR)/utils $(DEST_CODE_DIR)
	cp $(SRC_DIR)/__init__.py $(SRC_DIR)/global_settings.py $(DEST_CODE_DIR)

ifeq ($(CONNEXION_VERSION), 3)
		cp $(SRC_DIR)/asgi.py $(DEST_CODE_DIR)
else
		cp $(SRC_DIR)/wsgi.py $(DEST_CODE_DIR)
endif

	cp -r $(SRC_DIR)/controllers/internal/* $(DEST_INT_CONTROLLER_DIR)

clean:
	rm -rf out
