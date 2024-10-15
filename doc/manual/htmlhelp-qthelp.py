#!/usr/bin/python3
# -*- coding: utf-8 -*-
# $Id: htmlhelp-qthelp.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
A python script to create a .qhp file out of a given htmlhelp
folder. Lots of things about the said folder is assumed. Please
see the code and inlined comments.
"""

__copyright__ = \
"""
Copyright (C) 2006-2024 Oracle and/or its affiliates.

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

SPDX-License-Identifier: GPL-3.0-only
"""

import getopt
import logging
import os.path
import re
import sys

if sys.version_info[0] >= 3:
    from html.parser import HTMLParser
else:
    from HTMLParser import HTMLParser

# number of opened and not yet closed section tags of toc section
open_section_tags = 0

html_files = []

def create_keywords_section(folder):
    """
    use html_parser stuff to collect <a name ...> tags
    """
    keywords_section_lines = ['<keywords>']
    for html_file_name in html_files:
        # dita-ot creates htmlhelp output for en-us language in iso-8859-1 encoding, not utf-8
        full_html_path = os.path.join(folder, html_file_name)
        file_content = open(full_html_path, encoding='iso-8859-1').read()

        class html_parser(HTMLParser):
            def __init__(self):
                HTMLParser.__init__(self)
                self.a_tag = []
            def handle_starttag(self, tag, attrs):
                if tag != 'div' and tag != 'a':
                    return
                if tag == 'a':
                    for a in attrs:
                        if a[0] == 'name':
                            self.a_tag.append(a[1])

        parser = html_parser()
        parser.feed(file_content)
        for k in parser.a_tag:
            line = '<keyword name="' + k + '" id="' + k + '" ref="' + html_file_name + '#' + k + '"/>'
            keywords_section_lines.append(line)
    keywords_section_lines.append('</keywords>')
    return keywords_section_lines

def create_image_list(folder):
    """
    find the png files under topics/images folder and create a part of the
    qhelp project file with <file> tags
    """
    sFullImageFolderPath = os.path.join(folder, 'topics', 'images');
    if not os.path.isdir(sFullImageFolderPath):
        logging.error('Image subfolder "topics/images" is not found under "%s"!', folder)
        sys.exit(1);
    return ['<file>topics/images/%s</file>' % sFile for sFile in os.listdir(sFullImageFolderPath)];

def create_html_list(folder, list_file):
    """
    open files list and read the list of html files from there
    """
    global html_files
    html_file_lines = []
    if not list_file in os.listdir(folder):
        logging.error('Could not find the file "%s" in "%s"', list_file, folder)
        return html_file_lines
    full_path = os.path.join(folder, list_file)
    with open(full_path, encoding='utf-8') as file:
        lines = file.readlines()

    # first search for the [FILES] marker then collect .html lines
    marker_found = 0
    for line in lines:
        if '[FILES]' in line:
            marker_found = 1
            continue
        if marker_found == 0:
            continue
        if '.html' in line:
            html_file_lines.append('<file>' + line.strip('\n') + '</file>')
            html_files.append(line.strip('\n'))
    return html_file_lines


def create_files_section(folder, list_file):
    files_section_lines = ['<files>']
    files_section_lines += create_image_list(folder)
    files_section_lines += create_html_list(folder, list_file)
    files_section_lines.append('</files>')
    return files_section_lines

def parse_param_tag(line):
    label = 'value="'
    start = line.find(label)
    if start == -1:
        return ''
    start += len(label)
    end = line.find('"', start)
    if end == -1:
        return ''
    return line[start:end]

def parse_object_tag(lines, index):
    """
    look at next two lines. they are supposed to look like the following
         <param name="Name" value="Oracle VirtualBox">
         <param name="Local" value="index.html">
    parse out value fields and return
    title="Oracle VirtualBox" ref="index.html
    """
    result = ''
    if index + 2 > len(lines):
        logging.warning('Not enough tags after this one "%s"', lines[index])
        return result
    if    not re.match(r'^\s*<param', lines[index + 1], re.IGNORECASE) \
       or not re.match(r'^\s*<param', lines[index + 2], re.IGNORECASE):
        logging.warning('Skipping the line "%s" since next two tags are supposed to be param tags', lines[index])
        return result
    title = parse_param_tag(lines[index + 1])
    ref = parse_param_tag(lines[index + 2])
    global open_section_tags
    if title and ref:
        open_section_tags += 1
        result = '<section title="' + title + '" ref="' + ref + '">'
    else:
        logging.warning('Title or ref part is empty for the tag "%s"', lines[index])
    return result

def parse_non_object_tag(lines, index):
    """
    parse any string other than staring with <OBJECT
    decide if <section> tag should be closed
    """

    if index + 1 > len(lines):
        return ''
    global open_section_tags
    if open_section_tags <= 0:
        return ''
    # replace </OBJECT with </section only if the next tag is not <UL
    if re.match(r'^\s*</OBJECT', lines[index], re.IGNORECASE):
        if not re.match(r'^\s*<UL', lines[index + 1], re.IGNORECASE):
            open_section_tags -= 1
            return '</section>'
    elif re.match(r'^\s*</UL', lines[index], re.IGNORECASE):
        open_section_tags -= 1
        return '</section>'
    return ''

def parse_line(lines, index):
    result = ''

    # if the line starts with <OBJECT
    if re.match(r'^\s*<OBJECT', lines[index], re.IGNORECASE):
        result = parse_object_tag(lines, index)
    else:
        result = parse_non_object_tag(lines, index)
    return result

def create_toc(folder, toc_file):
    """
    parse TOC file. assuming all the relevant information
    is stored in tags and attributes. whatever is outside of
    <... > pairs is filtered out. we also assume < ..> are not nested
    and each < matches to a >
    """
    toc_string_list = []
    content = [x[2] for x in os.walk(folder)]
    if toc_file not in content[0]:
        logging.error('Could not find toc file "%s" under "%s"', toc_file, folder)
        return toc_string_list
    full_path = os.path.join(folder, toc_file)
    with open(full_path, encoding='utf-8') as file:
        content = file.read()

    # convert the file string into a list of tags there by eliminating whatever
    # char reside outside of tags.
    char_pos = 0
    tag_list = []
    while char_pos < len(content):
        start = content.find('<', char_pos)
        if start == -1:
            break
        end = content.find('>', start)
        if end == -1 or end >= len(content) - 1:
            break
        char_pos = end
        tag_list.append(content[start:end +1])

    # # insert new line chars. to make sure each line includes at most one tag
    # content = re.sub(r'>.*?<', r'>\n<', content)
    # lines = content.split('\n')
    toc_string_list.append('<toc>')
    for index, _ in enumerate(tag_list):
        str = parse_line(tag_list, index)
        if str:
            toc_string_list.append(str)
    toc_string_list.append('</toc>')
    toc_string = '\n'.join(toc_string_list)

    return toc_string_list

def usage(iExitCode):
    print('htmlhelp-qthelp.py -d <helphtmlfolder> -o <outputfilename>')
    return iExitCode

def main(argv):
    # Parse arguments.
    helphtmlfolder = ''
    output_filename = ''
    list_file = ''
    toc_file = ''
    try:
        opts, _ = getopt.getopt(argv[1:], "hd:o:f:t:")
    except getopt.GetoptError as err:
        logging.error(str(err))
        return usage(2)
    for opt, arg in opts:
        if opt == '-h':
            return usage(0)
        if opt == "-d":
            helphtmlfolder = arg
            print(helphtmlfolder)
        elif opt == "-f":
            list_file = arg
        elif opt == "-t":
            toc_file = arg
            print(toc_file)
        elif opt == "-o":
            output_filename = arg
    # check supplied helphtml folder argument
    if not helphtmlfolder:
        logging.error('No helphtml folder is provided. Exiting')
        return usage(2)
    if not os.path.exists(helphtmlfolder):
        logging.error('folder "%s" does not exist. Exiting', helphtmlfolder)
        return usage(2)
    helphtmlfolder = os.path.normpath(helphtmlfolder)

    # check supplied output file name
    if not output_filename:
        logging.error('No filename for output is given. Exiting')
        return usage(2)

    out_xml_lines = ['<?xml version="1.0" encoding="UTF-8"?>',
                     '<QtHelpProject version="1.0">',
                     '<namespace>org.virtualbox</namespace>',
                     '<virtualFolder>doc</virtualFolder>',
                     '<filterSection>']
    out_xml_lines += create_toc(helphtmlfolder, toc_file)
    out_xml_lines += create_files_section(helphtmlfolder, list_file)
    out_xml_lines += create_keywords_section(helphtmlfolder)
    out_xml_lines += ['</filterSection>', '</QtHelpProject>']

    with open(output_filename, 'wb') as out_file:
        out_file.write('\n'.join(out_xml_lines).encode('utf8'))
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))
