from pathlib import Path
import xml.etree.ElementTree as ET
import json
import argparse
from pathlib import Path

def main():
    parser = argparse.ArgumentParser(description='Hardcoded functions list in JSON')
    parser.add_argument('--xidl', type=str, help='Path to VirtualBox XIDL file')
    parser.add_argument('--out-dir', type=str, default="generated", help='Path where the result is stored')
    parser.add_argument('--out-file', type=str, default="hardcoded_function_list.json", help='JSON file name')
    
    args = parser.parse_args()
    xidl_file = Path(args.xidl)
    output_dir = Path(args.out_dir)
    output_dir.mkdir(exist_ok=True)
    output_file = Path(args.out_file)
    
    if not xidl_file.exists():
        print(f'Error: Input XIDL file not found: {xidl_file}')
        exit(1)

    tree = ET.parse(xidl_file)
    root = tree.getroot()

    output = []
    counter = 1

    for interface in root.findall('.//application/interface'):
        iface_name = interface.get('name')
        
        methods = []
        for method in interface.findall('method'):
            method_name = method.get('name')
            rest = method.find('rest')
            has_rest = rest is not None
            fHardcoded = rest.get('stub', 'generate') if has_rest else None

            if fHardcoded and fHardcoded != "generate":
                entry = {
                    'method_name': method_name,
                }
                methods.append(entry)

        if methods:
            output.append({'iface_name': iface_name[1:].lower(), 'methods': methods})


    out_path = Path(f"{output_dir}/{output_file}")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, 'w', encoding='utf-8') as jf:
        json.dump(output, jf, ensure_ascii=False, indent=4)

    print(f"Done. JSON output saved to: {out_path}")

if __name__ == '__main__':
    main()
