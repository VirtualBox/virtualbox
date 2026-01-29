import json
from jinja2 import Environment, FileSystemLoader, TemplateNotFound
from pathlib import Path
import argparse
import logging
import re
from collections import defaultdict

logging.basicConfig(level=logging.INFO)

FILE_HEADER = '''"""VBox REST API

Copyright (c) 2025 Oracle and/or its affiliates.
Licensed under the Universal Permissive License v 1.0 as shown at https://oss.oracle.com/licenses/upl

SPDX-License-Identifier: UPL-1.0
"""

import os
import platform
import logging
from vbox_server.global_settings import *
from vbox_server.utils.vbox_utils import *
from vbox_server.utils.object_conversion import *
from vbox_server.utils.enum_conversion import *
from vbox_server.utils.decorators import *
from http import HTTPStatus
from flask import jsonify
'''


def group_by_file_name(data):
    grouped = defaultdict(list)
    for entry in data:
        grouped[entry["file_name"]].append(entry)
    return grouped


def fix_uppercase_groups(s):
    def replacer(match):
        group = match.group(1)
        first_letter = group[0]
        rest = group[1:].lower()
        return first_letter + rest
    pattern = r'([A-Z]{2,})(?=[A-Z][a-z]|$)'
    return re.sub(pattern, replacer, s)


def to_snake_case(name):

    fixed_name = fix_uppercase_groups(name)

    result = []
    for i, c in enumerate(fixed_name):
        if i > 0 and c.isupper():
            result.append('_')
            result.append(c.lower())
        else:
            result.append(c.lower())
    return ''.join(result)


def main():
    parser = argparse.ArgumentParser(description='VirtualBox function Generator')
    parser.add_argument('--in-json-file-path', type=str, default="function_list.json", help='Path to JSON file')
    parser.add_argument('--in-template-file-path', type=str, help='Full path to jinja template')
    parser.add_argument('--in-template-header-file-path', type=str, default="function_import_template.j2", help='Path to Jinja template for headers')
    parser.add_argument('--out-dir', type=str, default="generated", help='Path where the result is kept')
    
    args = parser.parse_args()

    output_dir = Path(args.out_dir)
    in_json_file = Path(args.in_json_file_path)
    in_template_file = Path(args.in_template_file_path)
    in_template_header_file = Path(args.in_template_header_file_path)

    if not in_template_file.exists():
        print(f'Error: Template file not found: {in_template_file}')
        exit(1)

    if not in_json_file.exists():
        print(f'Error: Template file not found: {in_json_file}')
        exit(1)

    if not in_template_header_file.exists():
        print(f'Error: Template file not found: {in_template_header_file}')
        exit(1)

    output_dir.mkdir(parents=True, exist_ok=True)
    
    env = Environment(loader=FileSystemLoader(in_template_file.parent), trim_blocks=True, lstrip_blocks=True)
    env.filters['to_snake_case'] = to_snake_case
    try:
        code_template = env.get_template(in_template_file.name)
    except TemplateNotFound:
        print(f'Error: Could not load function template {in_template_file.name}')
        exit(1)

    if in_template_header_file.parent != in_template_file.parent:
        env.loader.searchpath.append(str(in_template_header_file.parent))
    try:
        import_template = env.get_template(in_template_header_file.name)
    except TemplateNotFound:
        print(f'Error: Could not load imports template {in_template_header_file.name}')
        exit(1)
    
    with open(in_json_file, 'r', encoding='utf-8') as jf:
        data = json.load(jf)
    
    groups = group_by_file_name(data)

    import_types = []
    rendered_functions = []

    for file_name, func_data_list in groups.items():
        for entry in func_data_list:
            x_vbox_stub = entry.get('x_vbox_stub')
            if x_vbox_stub is not None and x_vbox_stub != "generate":
                continue

            response_category = entry.get('response_category')
            if response_category != "No Response":
                response_type = entry.get('response_type')
                if response_type and response_type not in import_types:
                    import_types.append(response_type)
                    
            has_request_body = entry.get('has_request_body')
            if has_request_body is True:
                request_type = entry.get('request_type')
                if request_type and request_type not in import_types:
                    import_types.append(request_type)
        
            code = code_template.render(**entry)
            rendered_functions.append(code)

        import_lines = [import_template.render(iface=iface) for iface in import_types]
            
        output_content = []
        output_content.append(FILE_HEADER)
        output_content.extend(import_lines)
        output_content.append('\n')
        output_content.extend(rendered_functions)

        output_path = Path(output_dir/file_name)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text("\n".join(output_content), encoding='utf-8')
        print(f"Generated file {output_path} with {len(rendered_functions)} functions")
        
        import_types.clear()
        import_lines.clear()
        rendered_functions.clear()
        output_content.clear()

if __name__ == "__main__":
    main()