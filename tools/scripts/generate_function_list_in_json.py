import yaml
import json
from pathlib import Path
import argparse
import logging
import re
from collections import defaultdict

logging.basicConfig(level=logging.INFO)

def create_operation_id_list(data):
    id_list = set()
    for entry in data:
        iface_name = str(entry["iface_name"])
        for item in entry.get('methods'):
            id_list.add(str(iface_name + '_' + item.get('method_name')).lower())
    return id_list

def to_snake_case_simple(name):
    result = []
    for i, c in enumerate(name):
        if i > 0 and c.isupper():
            result.append('_')
            result.append(c.lower())
        else:
            result.append(c.lower())
    return ''.join(result)


def extract_enum_from_description(description, definitions):
    match = re.search(r'#/definitions/(\w+)', description)
    if match:
        enum_name = match.group(1)
        if enum_name in definitions and 'enum' in definitions[enum_name]:
            return enum_name
    return None


def prepare_endpoint_data(path, method, operation_data, definitions):
    operation_id = operation_data.get('operationId', 'unknown_operation')
    method_upper = method.upper()

    parts = operation_id.split('_', 1)
    iface_name = parts[0] if len(parts) == 2 else 'unknown'
    method_name = parts[1] if len(parts) == 2 else parts[0]

    summary = operation_data.get('summary', '')
    summary_match = re.search(r'::(\w+)', summary)
    if summary_match:
        summary_method = summary_match.group(1)
        if summary_method.lower() == method_name.lower():
            method_name = summary_method

    file_name = f"i_g_{iface_name}_controller.py"
    function_name = f"i_{operation_id}"

    session = None
    if path.startswith('/vm'):
        tags = operation_data.get('tags', '')
        if tags.count("console") !=0 or \
            method_upper == 'GET' or \
            method_name.lower().startswith(('get', 'check', 'test', 'find', 'query', 'read', 'enumerate')):
            session = False
        else:
            session = True

    in_path_param_list, in_query_param_list = [], []
    in_main_param_list, in_help_param_list = [], []
    enum_in_params = []
    has_request_body, request_body_type = False, None
    request_body_fields = []

    for param in operation_data.get('parameters', []):
        param_in = param.get('in')
        pname = param.get('name')

        if param_in == 'body':
            has_request_body = True
            schema_ref = param.get('schema', {}).get('$ref', '')
            request_body_type = schema_ref.split('/')[-1]
            request_body_fields = list(definitions.get(request_body_type, {}).get('properties', {}).keys())

            request_body_properties = definitions.get(request_body_type, {}).get('properties', {})
            for field_name, field_info in request_body_properties.items():
                ref = field_info.get('$ref')
                if ref:
                    ref_name = ref.split('/')[-1]
                    if 'enum' in definitions.get(ref_name, {}):
                        enum_in_params.append({'name': field_name, 'type': ref_name})
        elif param_in == 'path':
            in_path_param_list.append(param)
        elif param_in == 'query':
            in_query_param_list.append(param)

    iface_decorator_name = '@' + iface_name + 'Decorator'

    curr_vbox_obj = 'oCurr' + iface_name[0].upper() + iface_name[1:]

    if has_request_body:
        in_help_param_list = in_path_param_list + in_query_param_list
    else:
        for param in in_query_param_list:
            pname = param.get('name')
            if pname == 'select':
                in_help_param_list.append(param)
            else:
                enum = extract_enum_from_description(param.get('description', ''), definitions)
                if enum:
                    enum_in_params.append({'name': pname, 'type': enum})
                    in_main_param_list.append({'name': pname, 'type': enum})
                else:
                    lattr = {'name': pname, 'type': param.get('type', 'string')}
                    lextra_attr ={}
                    if lattr['type'] == 'array':
                        lextra_attr = {'is_array': True}
                        if (
                            (items := param.get('items')) and
                            (fmt := items.get('format')) and
                            (x_vbox_type := items.get('x-vbox-type'))
                        ):
                            lextra_attr.update({
                                'format': fmt,
                                'x_vbox_type': x_vbox_type,
                            })
                    else:
                        lextra_attr = {'format': param.get('format'), 'x_vbox_type': param.get('x-vbox-type')}

                    lattr.update({k: v for k, v in lextra_attr.items() if v not in (None, 0)})
                    in_main_param_list.append(lattr)

    in_param_names = [p['name'] for p in in_main_param_list]
    param_names_map = {name: to_snake_case_simple(name) for name in in_param_names}

    responses = operation_data.get('responses', {})

    response_ref, response_category, response_type, response_var, returned_param_list = None, 'No Response', None, None, []
    response_item_type = None

    response_200 = responses.get('200', {})

    if 'schema' in response_200:
        ref = response_200['schema'].get('$ref')
        if ref:
            response_ref = ref.split('/')[-1]
            response_type = response_ref
            response_var = f"o{response_type}"

            prop = definitions.get(response_ref, {}).get('properties', {})
            returned_param_list = [{'name': key, 'value': val} for key, val in prop.items()] if prop else []

            if 'ObjArrayWrapper' in response_ref or 'EnumArrayWrapper' in response_ref:
                response_category = 'Array'
                for key, value in prop.items():
                    if value.get('type') == 'array':
                        items_info = value.get('items', {})
                        if '$ref' in items_info:
                            response_item_type = items_info['$ref'].split('/')[-1]
                        elif 'type' in items_info:
                            response_item_type = items_info['type']
                        break

            elif 'ObjWrapper' in response_ref:
                response_category = 'Interface'
                for value in prop.values():
                    if '$ref' in value:
                        response_item_type = value['$ref'].split('/')[-1]
                        break

            elif 'EnumWrapper' in response_ref:
                response_category = 'Enum'
                for value in prop.values():
                    if '$ref' in value:
                        response_item_type = value['$ref'].split('/')[-1]
                        break
            else:
                response_category = 'Standard'
                standard_param_list = []

                for field_name, field_info in prop.items():
                    is_array = False
                    is_enum = False
                    field_type = None

                    if field_info.get('type') == 'array':
                        is_array = True
                        items = field_info.get('items', {})

                        if '$ref' in items:
                            ref_name = items['$ref'].split('/')[-1]
                            field_type = ref_name
                            if 'enum' in definitions.get(ref_name, {}):
                                is_enum = True
                        elif 'type' in items:
                            field_type = items['type']

                    elif '$ref' in field_info:
                        ref_name = field_info['$ref'].split('/')[-1]
                        field_type = ref_name
                        if 'enum' in definitions.get(ref_name, {}):
                            is_enum = True

                    elif 'type' in field_info:
                        field_type = field_info['type']

                    standard_param_list.append({
                        'name': field_name,
                        'is_array': is_array,
                        'type': field_type,
                        'is_enum': is_enum
                    })

                returned_param_list = standard_param_list

    return {
        'iface_name': iface_name,
        'iface_decorator_name': iface_decorator_name,
        'method_name': method_name,
        'function_name': function_name,
        'file_name': file_name,
        'is_session_req': session,
        'in_params': in_main_param_list,
        'in_param_list': ', '.join(in_param_names),
        'enum_in_params': enum_in_params,
        'in_help_param_list': in_help_param_list,
        'curr_vbox_obj': curr_vbox_obj,
        'has_request_body': has_request_body,
        'request_type': request_body_type,
        'request_body_fields': request_body_fields,
        'response_ref': response_ref,
        'response_category': response_category,
        'response_type': response_type,
        'response_var': response_var,
        'returned_param_list': returned_param_list,
        'param_names_map': param_names_map,
        'response_item_type': response_item_type,
        'x_vbox_stub': operation_data.get('x-vbox-stub', 'generate'),
        'tags': operation_data.get('tags', '')
    }


def main():
    parser = argparse.ArgumentParser(description='Function list data preparation in JSON')
    parser.add_argument('--yaml-api-def', type=str, default='all', help='Full path to YAML VBox API defintion file')
    parser.add_argument('--interface', type=str, default='all', help='Interface name to generate functions for (e.g., machine)')
    parser.add_argument('--out-dir', type=str, default="generated", help='Path where the result is kept')
    parser.add_argument('--out-file', type=str, default="function_list.json", help='JSON file name')

    args = parser.parse_args()
    interface_name = args.interface.lower()
    
    yaml_api_def = Path(args.yaml_api_def)
    out_file = Path(args.out_file)
    output_dir = Path(args.out_dir)
    output_path = Path(output_dir/out_file)

    if not yaml_api_def.exists():
        print(f'Error: YAML file not found: {yaml_api_def}')
        exit(1)

    output_dir.mkdir(parents=True, exist_ok=True) 
    
    with open(yaml_api_def, 'r', encoding='utf-8') as f:
        spec = yaml.safe_load(f)

    definitions = spec.get("definitions", {})
    paths = spec.get('paths', {})

    f = open(output_path, 'w', encoding='utf-8')

    output = []

    target_operations = set()
    for methods in paths.values():
        for d in methods.values():
            op_id = d.get('operationId', '')
            iface_prefix = op_id.split('_')[0].lower() if '_' in op_id else ''

            if interface_name == 'all':
                target_operations.add(op_id)
            else:
                if iface_prefix == interface_name:
                    target_operations.add(op_id)
                    break

    filtered = {(p, m): d for p, methods in paths.items() for m, d in methods.items() if d.get('operationId') in target_operations}
            
    for (path, method), details in filtered.items():
        data = prepare_endpoint_data(path, method, details, definitions)
        output.append(data)

    f.write(json.dumps(output, indent=4, ensure_ascii=False))

    print(f"Done. JSON output saved to: {output_path}")

if __name__ == '__main__':
    main()
