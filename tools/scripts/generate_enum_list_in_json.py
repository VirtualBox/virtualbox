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
    parser = argparse.ArgumentParser(description='Enumeration data preparation in JSON')
    parser.add_argument('--xidl', type=str, help='Path to VirtualBox XIDL file')
    parser.add_argument('--out-dir', type=str, default="generated", help='Path where the result is stored')
    parser.add_argument('--out-file', type=str, default="enumeration_list.json", help='JSON file name')
    
    args = parser.parse_args()
    xidl_file = Path(args.xidl)
    output_dir = Path(args.out_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    output_file = Path(args.out_file)
    
    if not xidl_file.exists():
        print(f'Error: Input JSON file not found: {xidl_file}')
        exit(1)

    tree = ET.parse(xidl_file)
    root = tree.getroot()

    output = []

    for enum in root.findall('.//application/enum'):
        enum_name = enum.get('name')
        enum_values_number = enum.findall('const')
        enum_values = [val.get('name') for val in enum_values_number if val.get('name')]
            
        output.append({'enum': enum_name, 'values': enum_values})

    out_path = Path(f"{output_dir}/{output_file}")
    with open(out_path, 'w', encoding='utf-8') as jf:
        json.dump(output, jf, ensure_ascii=False, indent=4)

    print(f"Done. JSON output saved to: {out_path}")

if __name__ == '__main__':
    main()
