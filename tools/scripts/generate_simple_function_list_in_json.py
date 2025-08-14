from pathlib import Path
import xml.etree.ElementTree as ET
import json
import argparse
from pathlib import Path

simple_types = ['boolean', 
                'wstring', 
                'long', 
                'long long', 
                'uuid', 
                'float', 
                'unsigned long',
                'unsigned short',
                ]

def main():
    parser = argparse.ArgumentParser(description='Simple function data preparation in JSON')
    parser.add_argument('--xidl', type=str, help='Path to VirtualBox XIDL file')
    parser.add_argument('--out-dir', type=str, default="generated", help='Path where the result is stored')
    parser.add_argument('--out-file', type=str, default="simple_function_list.json", help='JSON file name')
    
    args = parser.parse_args()
    xidl_file = Path(args.xidl)
    output_dir = Path(args.out_dir)
    output_dir.mkdir(exist_ok=True)
    output_file = Path(args.out_file)
    
    if not xidl_file.exists():
        print(f'Error: Input JSON file not found: {xidl_file}')
        exit(1)

    tree = ET.parse(xidl_file)
    root = tree.getroot()

    output = []
    counter = 1

    for interface in root.findall('.//application/interface'):
        iface_name = interface.get('name')
        attr_number = interface.findall('attribute')
        
        methods = []
        for method in interface.findall('method'):
            method_name = method.get('name')
            rest = method.find('rest')
            has_rest = rest is not None
            rest_method = rest.get('request', '').upper() if has_rest else None

            params = method.findall('param') or []

            if any('progress' in (p.get('type') or '').lower() for p in params):
                continue

            if any(p.get('safearray') == 'yes' for p in params):
                continue

            in_params = [p for p in params if p.get('dir') in (None, 'in')]
            out_params = [p for p in params if p.get('dir') in ('out', 'return')]

            if len(in_params) > 1 or len(out_params) > 1:
                continue

            entry = {
                'index': counter,
                'interface': iface_name,
                'method': method_name,
                'rest': rest_method if has_rest else None,
                'in': {
                    'name': in_params[0].get('name') if in_params else None,
                    'type': in_params[0].get('type') if in_params else None
                },
                'out': {
                    'name': out_params[0].get('name') if out_params else None,
                    'type': out_params[0].get('type') if out_params else None
                }
            }
            methods.append(entry)
            counter += 1

        if methods:
            output.append({'interface': iface_name, 'attribute_number': len(attr_number), 'methods': methods})


    out_path = Path(f"{output_dir}/{output_file}")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, 'w', encoding='utf-8') as jf:
        json.dump(output, jf, ensure_ascii=False, indent=4)

    print(f"Done. JSON output saved to: {out_path}")

if __name__ == '__main__':
    main()
