#!/usr/bin/env python3

import subprocess
import sys
import os
import tempfile

if __name__ == '__main__':
    if len(sys.argv) != 4:
        print("Usage: process-gperf.py <compiler> <input_file> <output_file>")
        sys.exit(1)

    compiler = sys.argv[1]
    input_file = sys.argv[2]
    output_file = sys.argv[3]

    # Get the directory of the input file for include path
    include_dir = os.path.dirname(input_file)
    # Also need the source directory where fcobjs.h is located
    src_dir = os.path.dirname(os.path.abspath(__file__))

    # Create a temporary .c file for preprocessing
    with tempfile.NamedTemporaryFile(mode='w', suffix='.c', delete=False) as tmp:
        with open(input_file, 'r') as f:
            tmp.write(f.read())
        tmp_name = tmp.name

    try:
        # Run the C preprocessor
        result = subprocess.run([
            compiler, '-E', f'-I{include_dir}', f'-I{src_dir}', tmp_name
        ], capture_output=True, text=True)

        if result.returncode != 0:
            print(f"Preprocessor failed: {result.stderr}")
            sys.exit(1)

        # Filter out preprocessor directives and empty lines
        lines = []
        for line in result.stdout.split('\n'):
            if not line.startswith('#') and line.strip():
                lines.append(line)

        # Write the output
        with open(output_file, 'w') as f:
            f.write('\n'.join(lines))
            f.write('\n')

    finally:
        # Clean up temporary file
        os.unlink(tmp_name)
