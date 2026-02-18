import yaml
import json
from pathlib import Path
import argparse
import logging
import re

logging.basicConfig(level=logging.INFO)

from typing import Optional, List, Tuple, Dict, Any


def pick_success_response(responses: dict):
    for code in ('200', '201', '202', '204'):
        if code in responses:
            v = responses.get(code)
            return code, (v if isinstance(v, dict) else {})
    if 'default' in responses:
        v = responses.get('default')
        return 'default', (v if isinstance(v, dict) else {})
    for k, v in (responses or {}).items():
        return k, (v if isinstance(v, dict) else {})
    return '', {}


def parse_schema_to_response_node(schema: dict, definitions: dict) -> dict:
    if not isinstance(schema, dict):
        return {}

    if schema.get("type") == "array":
        items = schema.get("items") or {}
        if isinstance(items, dict) and "$ref" in items:
            ref = extract_ref_name(items["$ref"])
            kind = classify_ref(definitions, ref)
            return {"type": kind, "ref": ref, "is_array": True}
        itype = items.get("type") if isinstance(items, dict) else None
        if itype in ("string", "integer", "number", "boolean"):
            return {"type": itype, "is_array": True}
        return {"type": "object", "is_array": True}

    stype = schema.get("type")
    if stype in ("string", "integer", "number", "boolean"):
        return {"type": stype}

    sref = schema.get("$ref")
    if sref:
        ref_name = extract_ref_name(sref)
        direct_kind = classify_ref(definitions, ref_name)
        if direct_kind in ("enum", "interface"):
            return {"type": direct_kind, "ref": ref_name}

        props_items = unwrap_wrapper_one_level(definitions, ref_name)
        if props_items is not None:
            items_out = []
            for fname, fschema in props_items:
                node = parse_schema_to_response_node(fschema, definitions)
                item = {"name": fname, "node": {k: v for k, v in node.items() if k != "is_array"}}
                if node.get("is_array"):
                    item["is_array"] = True
                items_out.append(item)
            return {"type":"object", "ref": ref_name, "items": items_out}

        return {"type":"object", "ref": ref_name}

    if stype == "object" or "properties" in schema:
        return {"type": "object"}

    return {}


def parse_response(responses: dict, definitions: dict):
    code, resp = pick_success_response(responses or {})

    if not isinstance(resp, dict):
        return code, {}, False

    schema = resp.get("schema")
    if not isinstance(schema, dict):
        return code, {}, False

    response_type_node = parse_schema_to_response_node(schema, definitions)
    return code, response_type_node, bool(response_type_node)


def extract_ref_name(sref: str) -> str:
    return sref.split("/")[-1] if isinstance(sref, str) else ""


def get_def(definitions: dict, name: str) -> dict:
    d = definitions.get(name, {})
    return d if isinstance(d, dict) else {}


def is_enum_ref(definitions: dict, name: str) -> bool:
    d = get_def(definitions, name)
    if bool(d.get("x-vbox-type") == 'enum'):
        return True

    return isinstance(d.get("enum"), list) and len(d["enum"]) > 0


def is_interface_ref(definitions: dict, name: str) -> bool:
    d = get_def(definitions, name)
    if bool(d.get("x-vbox-type") == 'interface'):
        return True
    return False


def classify_ref(definitions: dict, ref_name: str) -> str:
    if is_enum_ref(definitions, ref_name):
        return "enum"
    if is_interface_ref(definitions, ref_name):
        return "interface"
    return "object"


def unwrap_wrapper_one_level(definitions: dict, wrapper_ref: str) -> Optional[List[Tuple[str, Dict[str, Any]]]]:
    """
    Returns:
      - None, if wrapper_ref NOT a Swagger wrapper
      - list (field_name, field_schema_dict) if it's wrapper
        (always list 1+ items; single wrapper => list contains only 1 item)
    """

    if is_enum_ref(definitions, wrapper_ref) or is_interface_ref(definitions, wrapper_ref):
        return None

    d = get_def(definitions, wrapper_ref)
    props = d.get("properties")

    if not isinstance(props, dict) or not props:
        return None

    items: List[Tuple[str, Dict[str, Any]]] = []
    for fname, fschema in props.items():
        if isinstance(fschema, dict):
            items.append((fname, fschema))
        else:
            items.append((fname, {"type": "string"}))

    return items


def parse_request_body(body_param: dict, definitions: dict):
    """
    Returns:
      has_request_body: bool
      request_body_type: str|None
      request_body_fields: list[dict]  # [{'name':..., 'type':...}, ...]
    """
    if not isinstance(body_param, dict):
        return False, None, []

    schema = body_param.get("schema", {})
    if not isinstance(schema, dict):
        return True, None, []

    sref = schema.get("$ref", "")
    if not isinstance(sref, str) or not sref:
        return True, None, []

    request_body_type = sref.split("/")[-1]
    request_body_fields = extract_body_fields(request_body_type, definitions)
        
    return True, request_body_type, request_body_fields


def extract_body_fields(body_type: str, definitions: dict):
    props = definitions.get(body_type, {}).get("properties", {})
    if not isinstance(props, dict):
        return []

    def is_enum_ref(ref_name: str) -> bool:
        d = definitions.get(ref_name, {})
        return isinstance(d, dict) and isinstance(d.get("enum"), list) and len(d["enum"]) > 0

    fields = []
    for fname, finfo in props.items():
        if not isinstance(finfo, dict):
            fields.append({"name": fname, "type": "string"})
            continue

        if "$ref" in finfo:
            ref_name = finfo["$ref"].split("/")[-1]
            if is_enum_ref(ref_name):
                fields.append({"name": fname, "type": "enum", "enum": ref_name})
            else:
                fields.append({"name": fname, "type": "ref", "ref": ref_name})
            continue

        ftype = finfo.get("type", "string")

        if ftype == "array":
            item = finfo.get("items", {})
            item_type = "string"
            item_ref = None
            item_enum = None

            if isinstance(item, dict):
                if "$ref" in item:
                    item_ref = item["$ref"].split("/")[-1]
                    if is_enum_ref(item_ref):
                        item_enum = item_ref
                        item_type = "enum"
                    else:
                        item_type = "ref"
                else:
                    item_type = item.get("type", "string")

            out = {"name": fname, "type": "array", "items_type": item_type}
            if item_ref:
                out["items_ref"] = item_ref
            if item_enum:
                out["items_enum"] = item_enum
            fields.append(out)
            continue

        out = {"name": fname, "type": ftype}
        fmt = finfo.get("format")
        if fmt:
            out["format"] = fmt
        fields.append(out)

    return fields


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
    enum_in_params = []
    has_request_body, request_body_type = False, None
    request_body_fields = []

    params = operation_data.get('parameters', []) or []

    body_param = None
    for param in params:
        param_in = param.get('in')
        if param_in == 'body':
            body_param = param
        elif param_in == 'path':
            in_path_param_list.append(param)
        elif param_in == 'query':
            in_query_param_list.append(param)
            
    has_request_body, request_body_type, request_body_fields = parse_request_body(body_param, definitions)

    for f in request_body_fields:
        if f.get("type") == "enum":
            enum_in_params.append({"name": f["name"], "type": f["enum"]})
        if f.get("type") == "array" and f.get("items_type") == "enum":
            enum_in_params.append({"name": f["name"], "type": f["items_enum"]})
 
    in_main_param_list = []

    x_vbox_stub = operation_data.get('x-vbox-stub', 'generate')
    for p in in_path_param_list:
        if x_vbox_stub != 'generate':
            in_main_param_list.append({"name": p.get("name"), "type": "string"})

    for p in in_query_param_list:
        pname = p.get("name")
        in_main_param_list.append({"name": pname, "type": "string"})

    if has_request_body:
        in_main_param_list.append({"name": "oRequest", "type": request_body_type})

    iface_decorator_name = '@' + iface_name + 'Decorator'

    curr_vbox_obj = 'oCurr' + iface_name[0].upper() + iface_name[1:]

    responses = operation_data.get('responses', {})
    success_code, response_type_node, response_has_body = parse_response(responses, definitions)

    return {
        'iface_name': iface_name,
        'iface_decorator_name': iface_decorator_name,
        'method_name': method_name,
        'function_name': function_name,
        'file_name': file_name,
        'is_session_req': session,
        'in_params': in_main_param_list,
        'enum_in_params': enum_in_params,
        'curr_vbox_obj': curr_vbox_obj,
        'has_request_body': has_request_body,
        'request_type': request_body_type,
        'request_body_fields': request_body_fields,
        'x_vbox_stub': x_vbox_stub,
        'tags': operation_data.get('tags', ''),
        'response_success_code': success_code,
        'response_has_body': response_has_body,
        'response_type_node': response_type_node,
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
