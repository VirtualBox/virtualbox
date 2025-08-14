#!/usr/bin/env python3
import argparse
import json
from pathlib import Path
from jinja2 import Environment, FileSystemLoader, TemplateNotFound
import re

INPUT_DIR = "generated"
INPUT_FILE = "simple_function_list.json"
OUTPUT_DIR = "generated"
OUTPUT_PYTHON_FILE = "generated_simple_functions.py"
TEMPLATE_DIR = "templates"
TEMPLATE_FILE = "simple_function_template.j2"

static_imports = 'from http import HTTPStatus\n\
from flask import jsonify\n\
from vbox_server.global_settings import *\n\
from vbox_server.utils.vbox_utils import *\n\
from vbox_server.utils.object_conversion import *\n\
from vbox_server.utils.enum_conversion import *\n\
from vbox_server.utils.decorators import *\n\n'

def to_snake_case_simple(name):
    result = []
    for i, c in enumerate(name):
        if i > 0 and c.isupper():
            result.append('_')
            result.append(c.lower())
        else:
            result.append(c.lower())
    return ''.join(result)


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
    parser = argparse.ArgumentParser(description='Generator of the set of the simple functions from a prepared JSON file')
    parser.add_argument('--in-json-file-path', type=str, default="generated/simple_function_list.json", help='Path to JSON file')
    parser.add_argument('--in-template-file-path', type=str, default="templates/simple_function_template.j2", help='Path to Jinja template file')
    parser.add_argument('--in-template-header-file-path', type=str, default="templates/simple_class_import_template.j2", help='Path to Jinja template file with the imported classes')
    parser.add_argument('--out-dir', type=str, default="generated", help='Path where the result is stored')
    parser.add_argument('--out-file', type=str, default="simple_functions.py", help='Output file name')
    
    args = parser.parse_args()
    in_json_file = Path(args.in_json_file_path)
    in_template_file = Path(args.in_template_file_path)
    in_template_header_file = Path(args.in_template_header_file_path)
    
    output_dir = Path(args.out_dir)
    output_dir.mkdir(exist_ok=True)
    output_file = Path(args.out_file)
    output_path = Path(output_dir/output_file)

    if not in_json_file.exists():
        print(f'Error: Input JSON file not found: {in_json_file}')
        exit(1)
    if not in_template_file.exists():
        print(f'Error: Template file not found: {in_template_file}')
        exit(1)
    if not in_template_header_file.exists():
        print(f'Error: Template file not found: {in_template_header_file}')
        exit(1)

    env = Environment(
        loader=FileSystemLoader(str(in_template_file.parent)),
        trim_blocks=True,
        lstrip_blocks=True
    )

    env.filters['to_snake_case'] = to_snake_case
    env.globals.update({'to_snake_case': to_snake_case_simple})

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

    excluded_iface_list = ['IFile', 'ManagedObjectRef', 'WebsessionManager', 'VBoxSVCRegistration', 'IInternalSessionControl']

    # Collect unique interfaces for imports
    unique_ifaces = []
    for entry in data:
        iface = entry.get('interface')
        if iface and iface not in unique_ifaces:
            unique_ifaces.append(iface)

    unique_ifaces = [item for item in unique_ifaces if item not in excluded_iface_list]

    # Render import lines
    import_lines = [import_template.render(iface=iface) for iface in unique_ifaces]
    
    rendered_functions = []

    for iface_entry in data:
        iface = iface_entry.get('interface')
        if iface in excluded_iface_list:
            continue

        methods = iface_entry.get('methods', [])
        for method_entry in methods:
            method_name = method_entry.get('method')
            input_param = method_entry.get('in', {}).get('name')
            output_param = method_entry.get('out', {}).get('name')
            input_type = method_entry.get('in', {}).get('type')
            output_type = method_entry.get('out', {}).get('type')
            rest = method_entry.get('rest')

            code = code_template.render(
                iface=iface,
                method_name=method_name,
                input_param=input_param,
                output_param=output_param,
                input_type=input_type,
                output_type=output_type,
                rest=rest
            )

            iface_snake = iface[1:].lower() if iface.startswith('I') else iface.lower()
            func_snake = method_name.lower()
            header = ('#' * 70 + f"\n# Function: i_{iface_snake}_{func_snake}\n" + '#' * 70 + '\n')
            rendered_functions.append(header + code)

    # Combine imports and functions
    output_content = []
    output_content.append(static_imports)
    # output_content.extend(import_lines) # not used at moment
    output_content.append('\n')
    output_content.extend(rendered_functions)

    # Write combined output
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(output_content), encoding='utf-8')
    print(f"Generated file with {len(import_lines)} imports and {len(rendered_functions)} functions into {output_path}")

if __name__ == '__main__':
    main()
