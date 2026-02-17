import yaml
import json
from pathlib import Path
import argparse
import logging
import re
from collections import defaultdict

logging.basicConfig(level=logging.INFO)

from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Tuple


@dataclass
class FieldNode:
    name: str
    kind: str
    ref: Optional[str] = None
    is_array: bool = False
    item_kind: Optional[str] = None
    item_ref: Optional[str] = None


@dataclass
class TypeNode:
    kind: str = "unknown"

    ref: Optional[str] = None

    swagger_type: Optional[str] = None
    swagger_format: Optional[str] = None

    is_array: bool = False
    item_ref: Optional[str] = None
    item_kind: Optional[str] = None

    is_wrapper: bool = False
    is_wrapper_multi: bool = False
    inner_ref: Optional[str] = None
    inner_kind: Optional[str] = None
    wrapper_field: Optional[str] = None
    wrapper_multi_items: List[dict] = field(default_factory=list)


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


def parse_response(responses: dict, definitions: dict):
    code, resp = pick_success_response(responses or {})

    if not isinstance(resp, dict):
        return code, TypeNode(kind="no_body"), True

    schema = resp.get("schema")
    if not isinstance(schema, dict):
        return code, TypeNode(kind="no_body"), True

    node = parse_schema_to_typenode_shallow(schema, definitions)
    return code, node, False


def typenode_to_dict(n: TypeNode) -> dict:
    if n is None:
        return {}
    d = {
        "kind": n.kind,
        "ref": n.ref,
        "is_array": n.is_array,
        "item_ref": n.item_ref,
        "item_kind": n.item_kind,
        "is_wrapper": n.is_wrapper,
        "is_wrapper_multi": n.is_wrapper_multi,
        "inner_ref": n.inner_ref,
        "inner_kind": n.inner_kind,
        "wrapper_field": n.wrapper_field,
        "wrapper_multi_items": n.wrapper_multi_items,
        "swagger_type": n.swagger_type,
        "swagger_format": n.swagger_format,
    }
    return {k: v for k, v in d.items() if v not in (None, False, "", [])}


def build_response_type_node(node: TypeNode) -> dict:
    """
    Unified response_type_node structure:
      - Non-wrapper:
          { "type": <simple|enum|interface|object>, "ref"?: <str>, "is_array"?: true }
      - Swagger wrapper (top-level only):
          { "type": "object", "ref": <WrapperName>, "items": [ { "name": <field>, "is_array"?: true, "node": {...} }, ... ] }

    NOTE: Per invariant: Swagger wrapper can be only top-level; below it are only simple / native interface / native enum.
    """

    def _simple_node_dict(t: str, ref: str = None) -> dict:
        d = {"type": t}
        if ref:
            d["ref"] = ref
        return d

    def _from_typenode(n: TypeNode) -> dict:
        if n.kind == "array":
            base_kind = n.item_kind or "object"
            if base_kind == "simple":
                base_type = n.swagger_type or "string"
                d = _simple_node_dict(base_type)
            elif base_kind in ("enum", "interface", "object"):
                d = _simple_node_dict(base_kind, n.item_ref)
            else:
                d = _simple_node_dict("object")
            d["is_array"] = True
            return d

        if n.kind == "simple":
            return _simple_node_dict(n.swagger_type or "string")

        if n.kind in ("enum", "interface", "object"):
            return _simple_node_dict(n.kind, n.ref)

        return _simple_node_dict("object", n.ref)

    def _from_typenode_dict(d: dict) -> dict:
        kind = d.get("kind")
        if kind == "array":
            base_kind = d.get("item_kind") or "object"
            if base_kind == "simple":
                base_type = d.get("swagger_type") or "string"
                out = {"type": base_type, "is_array": True}
            elif base_kind in ("enum", "interface", "object"):
                out = {"type": base_kind, "ref": d.get("item_ref"), "is_array": True}
            else:
                out = {"type": "object", "is_array": True}
            return out

        if kind == "simple":
            return {"type": d.get("swagger_type") or "string"}

        if kind in ("enum", "interface", "object"):
            out = {"type": kind}
            if d.get("ref"):
                out["ref"] = d["ref"]
            return out

        # fallback
        out = {"type": "object"}
        if d.get("ref"):
            out["ref"] = d["ref"]
        return out

    if not node or node.kind in ("unknown", "no_body"):
        return {}

    if node.kind == "wrapper_multi":
        items = []
        for it in (node.wrapper_multi_items or []):
            fname = it.get("field") or "returnValue"
            nd = it.get("node") or {}
            items.append({
                "name": fname,
                **({"is_array": True} if (nd.get("kind") == "array") else {}),
                "node": _from_typenode_dict(nd),
            })

        return {
            "type": "object",
            "ref": node.ref,
            "items": items,
        }

    if node.kind == "wrapper":
        field_name = node.wrapper_field or "returnValue"

        if node.is_array:
            base_kind = node.item_kind or "object"
            if base_kind == "simple":
                inner_node = {"type": node.swagger_type or "string"}
            elif base_kind in ("enum", "interface", "object"):
                inner_node = {"type": base_kind, "ref": node.item_ref}
            else:
                inner_node = {"type": "object"}

            return {
                "type": "object",
                "ref": node.ref,
                "items": [{
                    "name": field_name,
                    "is_array": True,
                    "node": inner_node,
                }]
            }

        if node.inner_kind == "simple":
            inner_node = {"type": node.swagger_type or "string"}
        elif node.inner_kind in ("enum", "interface", "object"):
            inner_node = {"type": node.inner_kind, "ref": node.inner_ref}
        else:
            inner_node = {"type": "object"}

        return {
            "type": "object",
            "ref": node.ref,
            "items": [{
                "name": field_name,
                "node": inner_node,
            }]
        }

    return _from_typenode(node)


def extract_ref_name(sref: str) -> str:
    return sref.split("/")[-1] if isinstance(sref, str) else ""


def get_def(definitions: dict, name: str) -> dict:
    d = definitions.get(name, {})
    return d if isinstance(d, dict) else {}


def is_enum_ref(definitions: dict, name: str) -> bool:
    d = get_def(definitions, name)
    if bool(d.get("x-vbox-type") == 'enum'):
        return True
    return False


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


def unwrap_wrapper_one_level(definitions: dict, wrapper_ref: str):
    """
    Returns:
      (wrapper_kind, field_name, inner_schema_dict)
    wrapper_kind: 'single'|'multi'|None
    """
    d = get_def(definitions, wrapper_ref)

    if is_enum_ref(definitions, wrapper_ref) or is_interface_ref(definitions, wrapper_ref):
        return (None, None, None)

    props = d.get("properties")
    if not isinstance(props, dict) or len(props) == 0:
        return (None, None, None)

    if len(props) > 1:
        return ("multi", list(props.items()), None)

    field_name, field_schema = next(iter(props.items()))
    if isinstance(field_schema, dict):
        return ("single", field_name, field_schema)

    return (None, None, None)


def parse_schema_to_typenode_shallow(schema: dict, definitions: dict) -> TypeNode:
    n = TypeNode(kind="unknown")

    if not isinstance(schema, dict):
        return n

    if schema.get("type") == "array":
        n.kind = "array"
        n.is_array = True
        items = schema.get("items", {})
        if isinstance(items, dict):
            iref = items.get("$ref")
            if iref:
                n.item_ref = extract_ref_name(iref)
                n.item_kind = classify_ref(definitions, n.item_ref)
            else:
                itype = items.get("type")
                if itype in ("string", "integer", "number", "boolean"):
                    n.item_kind = "simple"
                    n.swagger_type = itype
        return n

    stype = schema.get("type")
    if stype in ("string", "integer", "number", "boolean"):
        n.kind = "simple"
        n.swagger_type = stype
        n.swagger_format = schema.get("format")
        return n

    sref = schema.get("$ref")
    if sref:
        ref_name = extract_ref_name(sref)
        n.ref = ref_name

        direct_kind = classify_ref(definitions, ref_name)
        if direct_kind in ("enum", "interface"):
            n.kind = direct_kind
            return n

        wkind, field_name, field_schema = unwrap_wrapper_one_level(definitions, ref_name)

        if wkind == "multi":
            n.kind = "wrapper_multi"
            n.is_wrapper = True
            n.is_wrapper_multi = True

            # collect per-field type info (order is preserved from YAML definitions)
            props_items = field_name if isinstance(field_name, list) else []
            for fname, fschema in props_items:
                if not isinstance(fschema, dict):
                    n.wrapper_multi_items.append({"field": fname, "node": {"kind": "simple", "swagger_type": "string"}})
                    continue
                fnode = parse_schema_to_typenode_shallow(fschema, definitions)
                n.wrapper_multi_items.append({"field": fname, "node": typenode_to_dict(fnode)})
            return n

        if wkind == "single" and isinstance(field_schema, dict):
            n.kind = "wrapper"
            n.is_wrapper = True
            n.wrapper_field = field_name

            if "$ref" in field_schema:
                inner_ref = extract_ref_name(field_schema["$ref"])
                n.inner_ref = inner_ref
                n.inner_kind = classify_ref(definitions, inner_ref)
                return n

            if field_schema.get("type") == "array":
                n.is_array = True
                n.inner_kind = "array"
                items = field_schema.get("items", {})
                if isinstance(items, dict) and "$ref" in items:
                    inner_ref = extract_ref_name(items["$ref"])
                    n.inner_ref = inner_ref
                    n.item_ref = inner_ref
                    n.item_kind = classify_ref(definitions, inner_ref)
                else:
                    itype = (items or {}).get("type") if isinstance(items, dict) else None
                    if itype in ("string", "integer", "number", "boolean"):
                        n.item_kind = "simple"
                        n.swagger_type = itype
                return n

            ftype = field_schema.get("type")
            if ftype in ("string", "integer", "number", "boolean"):
                n.inner_kind = "simple"
                n.swagger_type = ftype
                n.swagger_format = field_schema.get("format")
                return n

            n.inner_kind = "unknown"
            return n

        n.kind = "object"
        return n

    if stype == "object" or "properties" in schema:
        n.kind = "object"
        return n

    return n


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

    in_param_names = [p['name'] for p in in_main_param_list]

    responses = operation_data.get('responses', {})

    success_code, resp_node, no_body = parse_response(responses, definitions)
    response_success_code = success_code
    response_type_node = build_response_type_node(resp_node)
    response_has_body = (not no_body)

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
        'x_vbox_stub': operation_data.get('x-vbox-stub', 'generate'),
        'tags': operation_data.get('tags', ''),
        'response_success_code': response_success_code,
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
