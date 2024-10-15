# -*- coding: utf-8 -*-
# $Id: fix_stale_refs.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
This module must be used interactively!
Use with caution as it will delete some values from the regisry!

It tries to locate client references to products that no longer exist.
"""

__copyright__ = \
"""
Copyright (C) 2012-2024 Oracle and/or its affiliates.

This file is part of VirtualBox base platform packages, as
available from https://www.virtualbox.org.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation, in version 3 of the
License.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <https://www.gnu.org/licenses>.

The contents of this file may alternatively be used under the terms
of the Common Development and Distribution License Version 1.0
(CDDL), a copy of it is provided in the "COPYING.CDDL" file included
in the VirtualBox distribution, in which case the provisions of the
CDDL are applicable instead of those of the GPL.

You may elect to license modified versions of this file under the
terms and conditions of either the GPL or the CDDL or both.

SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
"""
__version__ = "$Revision: 106320 $"


from _winreg import HKEY_LOCAL_MACHINE, KEY_ALL_ACCESS
from _winreg import OpenKey, CloseKey, EnumKey, QueryInfoKey, EnumValue, DeleteValue, QueryValueEx
from distutils.util import strtobool

def reverse_bytes(hex_string):
    """
    This function reverses the order of bytes in the provided string.
    Each byte is represented by two characters which are reversed as well.
    """
    #print 'reverse_bytes(' + hex_string + ')'
    chars = len(hex_string)
    if chars > 2:
        return reverse_bytes(hex_string[chars/2:]) + reverse_bytes(hex_string[:chars/2])
    else:
        return hex_string[1] + hex_string[0]

def transpose_guid(guid):
    """
    Windows Installer uses different way to present GUID string. This function converts GUID
    from installer's presentation to more conventional form.
    """
    return '{' + reverse_bytes(guid[0:8]) + '-' + reverse_bytes(guid[8:12]) + \
        '-' + reverse_bytes(guid[12:16]) + \
        '-' + reverse_bytes(guid[16:18]) + reverse_bytes(guid[18:20]) + \
        '-' + ''.join([reverse_bytes(guid[i:i+2]) for i in range(20, 32, 2)]) + '}'

PRODUCTS_KEY   = r'SOFTWARE\Microsoft\Windows\CurrentVersion\Installer\UserData\S-1-5-18\Products'
COMPONENTS_KEY = r'SOFTWARE\Microsoft\Windows\CurrentVersion\Installer\UserData\S-1-5-18\Components'

def get_installed_products():
    """
    Enumerate all installed products.
    """
    products = {}
    hkey_products = OpenKey(HKEY_LOCAL_MACHINE, PRODUCTS_KEY, 0, KEY_ALL_ACCESS)

    try:
        product_index = 0
        while True:
            product_guid = EnumKey(hkey_products, product_index)
            hkey_product_properties = OpenKey(hkey_products, product_guid + r'\InstallProperties', 0, KEY_ALL_ACCESS)
            try:
                value = QueryValueEx(hkey_product_properties, 'DisplayName')[0]
            except WindowsError as oXcpt:
                if oXcpt.winerror != 2:
                    raise
                value = '<unknown>'
            CloseKey(hkey_product_properties)
            products[product_guid] = value
            product_index += 1
    except WindowsError as oXcpt:
        if oXcpt.winerror != 259:
            print(oXcpt.strerror + '.', 'error', oXcpt.winerror)
    CloseKey(hkey_products)

    print('Installed products:')
    for product_key in sorted(products.keys()):
        print(transpose_guid(product_key), '=', products[product_key])

    print()
    return products

def get_missing_products(hkey_components):
    """
    Detect references to missing products.
    """
    products = get_installed_products()

    missing_products = {}

    for component_index in xrange(0, QueryInfoKey(hkey_components)[0]):
        component_guid = EnumKey(hkey_components, component_index)
        hkey_component = OpenKey(hkey_components, component_guid, 0, KEY_ALL_ACCESS)
        clients = []
        for value_index in xrange(0, QueryInfoKey(hkey_component)[1]):
            client_guid, client_path = EnumValue(hkey_component, value_index)[:2]
            clients.append((client_guid, client_path))
            if not client_guid in products:
                if client_guid in missing_products:
                    missing_products[client_guid].append((component_guid, client_path))
                else:
                    missing_products[client_guid] = [(component_guid, client_path)]
        CloseKey(hkey_component)
    return missing_products

def main():
    """
    Enumerate all installed products, go through all components and check if client refences
    point to valid products. Remove references to non-existing products if the user allowed it.
    """
    hkey_components = OpenKey(HKEY_LOCAL_MACHINE, COMPONENTS_KEY, 0, KEY_ALL_ACCESS)

    missing_products = get_missing_products(hkey_components)

    print('Missing products refer the following components:')
    for product_guid in sorted(missing_products.keys()):
        if product_guid[1:] == '0'*31:
            continue
        print('Product', transpose_guid(product_guid) + ':')
        for component_guid, component_file in missing_products[product_guid]:
            print(' ' + transpose_guid(component_guid), '=', component_file)

        print('Remove all references to product', transpose_guid(product_guid) + '? [y/n]')
        if strtobool(raw_input().lower()):
            for component_guid, component_file in missing_products[product_guid]:
                hkey_component = OpenKey(hkey_components, component_guid, 0, KEY_ALL_ACCESS)
                print('Removing reference in ' + transpose_guid(component_guid), '=', component_file)
                DeleteValue(hkey_component, product_guid)
                CloseKey(hkey_component)
        else:
            print('Cancelled removal of product', transpose_guid(product_guid))

    CloseKey(hkey_components)

if __name__ == "__main__":
    main()
