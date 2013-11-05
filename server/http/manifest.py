# Manifest of files to include in the simple HTTP server.
# Run this script to generate the httpdocs.cpp source file.

manifest = [
    ('/', 'index.html', 'text/html'),
    ('/fc-96x96.png', None, 'image/png'),

    # 404 error document must be last.
    (None, '404.html', 'text/html'),
]

import json, sys, os

sys.stdout.write("""/*
 * HTTP Document data.
 * Automatically generated by manifest.py
 */

#include "netserver.h"

NetServer::HTTPDocument NetServer::httpDocumentList[] = {
""")

def quote(str):
    # Encode a byte buffer as a C++ string, and octal-escape any funny characters

    if str is None:
        return 'NULL'
    output = ['"']

    allowedChars = [chr(c) for c in range(ord(' '), ord('~') + 1) if chr(c) not in '"\\?']
    for c in str:
        if c in allowedChars:
            output.append(c)
        else:
            byte = ord(c)
            output.append('\\%d%d%d' % (byte >> 6, (byte >> 3) & 7, byte & 7))

    output.append('"')
    return ''.join(output)

for path, filename, contentType in manifest:
    if filename is None:
        filename = '.' + path

    if contentType.startswith('text/'):
        mode = 'r'
    else:
        mode = 'rb'

    data = open(filename, mode).read()
    sys.stdout.write("{ %s, %s, %s, %d },\n" %
        (quote(path), quote(data), quote(contentType), len(data)))

sys.stdout.write("};\n")