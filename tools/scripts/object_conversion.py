import json
from jinja2 import Environment, FileSystemLoader, TemplateNotFound
from pathlib import Path
import argparse
import logging
import re

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

if os.name == 'nt' or platform.system() == 'Windows':
    from pywintypes import com_error as COMException
else:
    from xpcom import COMException
'''


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
    parser = argparse.ArgumentParser(description='Swagger to VirtualBox and vice versa enumeration function Generator')
    parser.add_argument('--in-json-file-path', type=str, default="enumeration_list.json", help='Path to JSON file')
    parser.add_argument('--in-template-file-path', type=str, help='Full path to jinja template')
    parser.add_argument('--out-dir', type=str, default="generated", help='Path where the result is kept')
    parser.add_argument('--out-file', type=str, default="enum_conversion.py", help='File name. Put all functions into one file')
    
    args = parser.parse_args()

    out_file = args.out_file
    output_dir = Path(args.out_dir)
    output_path = Path(output_dir/out_file)
    in_json_file = Path(args.in_json_file_path)
    in_template_file = Path(args.in_template_file_path)

    if not in_template_file.exists():
        print(f'Error: Template file not found: {in_template_file}')
        exit(1)

    if not in_json_file.exists():
        print(f'Error: Template file not found: {in_json_file}')
        exit(1)

    output_dir.mkdir(parents=True, exist_ok=True)
    
    env = Environment(loader=FileSystemLoader(in_template_file.parent), trim_blocks=True, lstrip_blocks=True)
    env.filters['to_snake_case'] = to_snake_case
    try:
        code_template = env.get_template(in_template_file.name)
    except TemplateNotFound:
        print(f'Error: Could not load function template {in_template_file.name}')
        exit(1)

    with open(in_json_file, 'r', encoding='utf-8') as jf:
        data = json.load(jf)
    
    rendered_functions = []

    for entry in data:
        code = code_template.render(**entry)
        rendered_functions.append(code)

    output_content = []
    output_content.append(FILE_HEADER)
    output_content.append('\n')
    output_content.extend(rendered_functions)
 
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(output_content), encoding='utf-8')
    print(f"Generated file {output_path} with {len(rendered_functions)} functions")

if __name__ == "__main__":
    main()