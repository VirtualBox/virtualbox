import yaml
import json
from pathlib import Path
import argparse
import logging
import re

logging.basicConfig(level=logging.INFO)

def analyze_property(prop_name, prop_schema, definitions):
    is_array = False
    is_enum = False
    is_interface = False
    enum_type = ""
    interface_type = ""
    transform_fn = "item"

    prop_type = prop_schema.get("type")
    ref = prop_schema.get("$ref")
    enum = prop_schema.get("enum", None)

    if prop_type == "array":
        is_array = True
        items = prop_schema.get("items", {})
        ref = items.get("$ref")
        enum = items.get("enum")

    ref_type = None
    if ref:
        ref_type = ref.split("/")[-1]
        ref_schema = definitions.get(ref_type, {})
        ref_enum = ref_schema.get("enum", None)

        if ref_enum is not None:
            is_enum = True
            enum_type = ref_type
            transform_fn = "item"
        else:
            is_interface = True
            interface_type = ref_type
            transform_fn = f"i_fill_{ref_type[1:].lower()}" if ref_type.startswith("I") else f"i_fill_{ref_type.lower()}"

    if enum is not None:
        is_enum = True
        enum_type = prop_name
        transform_fn = "item"

    return {
        "name": prop_name,
        "dest_name": prop_name,
        "is_array": is_array,
        "is_enum": is_enum,
        "is_interface": is_interface,
        "enum_type": enum_type,
        "interface": interface_type,
        "transform_function": transform_fn
    }

def main():
    parser = argparse.ArgumentParser(description='Object data preparation in JSON')
    parser.add_argument('--yaml-api-def', type=str, default='all', help='Full path to YAML VBox API defintion file')
    parser.add_argument('--interface', type=str, default='all', help='Interface name to generate functions for (e.g., machine)')
    parser.add_argument('--out-dir', type=str, default="generated", help='Path where the result is kept')
    parser.add_argument('--out-file', type=str, default="object_list.json", help='JSON file name')

    args = parser.parse_args()
    interface_name = args.interface.lower()
    
    yaml_api_def = Path(args.yaml_api_def)
    out_file = Path(args.out_file)
    output_dir = Path(args.out_dir)
    output_path = Path(output_dir/out_file)

    output_dir.mkdir(parents=True, exist_ok=True) 
    
    with open(yaml_api_def, 'r', encoding='utf-8') as f:
        spec = yaml.safe_load(f)

    definitions = spec.get("definitions", {})
    enum_definitions = [key for key, val in definitions.items() if 'enum' in val]

    f = open(output_path, 'w', encoding='utf-8')

    output = []

    for def_name, def_schema in definitions.items():
        if def_schema.get("type") != "object" or def_name in enum_definitions or "properties" not in def_schema:
            continue

        props = def_schema["properties"]

        if len(interface_name) != 0 and interface_name != 'all':
            if def_name.capitalize() != interface_name.capitalize():
                continue

        swagger_class = def_name
        iface_name = def_name

        attributes = []
        for prop_name, prop_schema in props.items():
            attr = analyze_property(prop_name, prop_schema, definitions)
            attributes.append(attr)

        context = {
            "iface_name": iface_name,
            "swagger_class": swagger_class,
            "attributes": attributes,
        }

        output.append(context)

    f.write(json.dumps(output, indent=4, ensure_ascii=False))

    print(f"Done. JSON output saved to: {output_path}")

if __name__ == '__main__':
    main()
