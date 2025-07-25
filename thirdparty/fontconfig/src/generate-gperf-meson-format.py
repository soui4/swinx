#!/usr/bin/env python3

import sys
import re
import os

def parse_fcobjs_h(fcobjs_path):
    """Parse fcobjs.h to extract object names and create string mappings"""
    objects = []

    # Mapping from macro names to their actual string values as defined in fontconfig.h
    macro_to_string = {
        'FAMILY': 'family',
        'FAMILYLANG': 'familylang',
        'STYLE': 'style',
        'STYLELANG': 'stylelang',
        'FULLNAME': 'fullname',
        'FULLNAMELANG': 'fullnamelang',
        'SLANT': 'slant',
        'WEIGHT': 'weight',
        'WIDTH': 'width',
        'SIZE': 'size',
        'ASPECT': 'aspect',
        'PIXEL_SIZE': 'pixelsize',
        'SPACING': 'spacing',
        'FOUNDRY': 'foundry',
        'ANTIALIAS': 'antialias',
        'HINT_STYLE': 'hintstyle',
        'HINTING': 'hinting',
        'VERTICAL_LAYOUT': 'verticallayout',
        'AUTOHINT': 'autohint',
        'GLOBAL_ADVANCE': 'globaladvance',
        'FILE': 'file',
        'INDEX': 'index',
        'RASTERIZER': 'rasterizer',
        'OUTLINE': 'outline',
        'SCALABLE': 'scalable',
        'DPI': 'dpi',
        'RGBA': 'rgba',
        'SCALE': 'scale',
        'MINSPACE': 'minspace',
        'CHARWIDTH': 'charwidth',
        'CHAR_HEIGHT': 'charheight',
        'MATRIX': 'matrix',
        'CHARSET': 'charset',
        'LANG': 'lang',
        'FONTVERSION': 'fontversion',
        'CAPABILITY': 'capability',
        'FONTFORMAT': 'fontformat',
        'EMBOLDEN': 'embolden',
        'EMBEDDED_BITMAP': 'embeddedbitmap',
        'DECORATIVE': 'decorative',
        'LCD_FILTER': 'lcdfilter',
        'NAMELANG': 'namelang',
        'FONT_FEATURES': 'fontfeatures',
        'PRGNAME': 'prgname',
        'HASH': 'hash',
        'POSTSCRIPT_NAME': 'postscriptname',
        'COLOR': 'color',
        'SYMBOL': 'symbol',
        'FONT_VARIATIONS': 'fontvariations',
        'VARIABLE': 'variable',
        'FONT_HAS_HINT': 'fonthashint',
        'ORDER': 'order',
        'DESKTOP_NAME': 'desktop',
        'NAMED_INSTANCE': 'namedinstance',
        'FONT_WRAPPER': 'fontwrapper'
    }

    with open(fcobjs_path, 'r') as f:
        content = f.read()

    # Find FC_OBJECT() macro definitions like: FC_OBJECT (FAMILY, FcTypeString, FcCompareFamily)
    fc_pattern = r'FC_OBJECT\s*\(\s*([A-Z_]+)\s*,\s*[^,]+\s*,\s*[^)]+\)'
    matches = re.findall(fc_pattern, content)

    # Create list of (string_literal, object_constant) pairs
    for macro_name in matches:
        if macro_name in macro_to_string:
            string_val = macro_to_string[macro_name]
            object_const = f"FC_{macro_name}_OBJECT"
            objects.append((f'"{string_val}"', object_const))
        else:
            print(f"Warning: Unknown macro {macro_name}")

    return objects

def generate_meson_format_gperf(template_path, fcobjs_path, output_path):
    """Generate gperf file in Meson-compatible format (using string literals)"""

    # Parse fcobjs.h to get object mappings
    objects = parse_fcobjs_h(fcobjs_path)

    # Write the output file with Meson-compatible minimal header
    with open(output_path, 'w') as f:
        # Write minimal header like Meson version
        f.write('%{\n')
        f.write('%}\n')
        f.write('%struct-type\n')
        f.write('%language=ANSI-C\n')
        f.write('%includes\n')
        f.write('%enum\n')
        f.write('%readonly-tables\n')
        f.write('%define slot-name name\n')
        f.write('%define hash-function-name FcObjectTypeHash\n')
        f.write('%define lookup-function-name FcObjectTypeLookup\n')
        f.write('%pic\n')
        f.write('%define string-pool-name FcObjectTypeNamePool\n')
        f.write('struct FcObjectTypeInfo {\n')
        f.write('int name;\n')
        f.write('int id;\n')
        f.write('};\n')
        f.write('%%\n')

        # Write object data in Meson format
        for string_literal, object_const in objects:
            f.write(f"{string_literal},{object_const}\n")

if __name__ == '__main__':
    if len(sys.argv) != 4:
        print("Usage: generate-gperf-meson-format.py <template_file> <fcobjs_file> <output_file>")
        sys.exit(1)

    template_path = sys.argv[1]
    fcobjs_path = sys.argv[2]
    output_path = sys.argv[3]

    generate_meson_format_gperf(template_path, fcobjs_path, output_path)