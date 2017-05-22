#!/usr/bin/env python

"""
Pandoc filter to process code blocks with class "graphviz" into
graphviz-generated images.
"""

from subprocess import Popen, PIPE
import hashlib
import os
import sys
from pandocfilters import toJSONFilter, Str, Para, Image


dot_param = ["dot"] + sys.argv[1:]

def sha1(x):
    return hashlib.sha1(x.encode(sys.getfilesystemencoding())).hexdigest()

imagedir = "graphviz-images"


def graphviz(key, value, format, meta):
    if key == 'CodeBlock':
        [[ident, classes, keyvals], code] = value
        caption = "caption"
        if "graphviz" in classes:
            filename = sha1(code)
            if format == "html":
                filetype = "png"
            elif format == "latex":
                filetype = "pdf"
            else:
                filetype = "png"
            alt = Str(caption)
            src = imagedir + '/' + filename + '.' + filetype
            if not os.path.isfile(src):
                try:
                    os.mkdir(imagedir)
                    sys.stderr.write('Created directory ' + imagedir + '\n')
                except OSError:
                    pass
                p = Popen(dot_param + ['-o%s'%src], stdin = PIPE)
                p.communicate(code)
                sys.stderr.write('Created image ' + src + '\n')
            tit = ""
            return Para([Image(['', [], []], [alt], [src, tit])])

if __name__ == "__main__":
    toJSONFilter(graphviz)
