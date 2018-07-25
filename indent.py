#!/usr/bin/python
#
# Copyright (C) 2015, 2017-2018, Hao Hou
#
# This is the script that correct the indentation, based on the following indentation rule:
#
#   a) There are two types of leading white-spaces charecters: indentation and alignment
#   b) Indentations are the white-spaces that denotes the level of nested blocks
#   c) Alignments are white-spaces for other purpose
#   d) Only use <tab> for indentation and use <space> for alignment
#
# This rule makes the code formatting not related to the editors tab-stop setting. 
# At the same time, for the smaller screen, we can always use a smaller tab-stop to make
# the code compact without any layout change.
#
# Please make sure the code follows the indentation rule before merge by running
# 
#   ./indent.py

import sys
import re
import os
import argparse

class StackToken(object):
    def __init__(self, expect_cond, stop_set, diff, kind):
        self.expect_cond = expect_cond
        self.stop_set = stop_set
        self.diff = diff
        self.kind = kind
    def __repr__(self):
        return "Token(kind = %s, cond = %d, expecting = %s)"%(self.kind, self.expect_cond, self.stop_set)
    def should_pop(self, lit, consume = True):
        stop_lit = lit if consume else "~" + lit
        return stop_lit in self.stop_set

def format(fp, verbose = False, color = False, should_warn = False):
    comment = False
    string  = False
    escape = False
    char = False
    recent = " " * 12
    word = ""
    pp = False
    stack = []
    pc = 0
    idlevel = -1
    result = ""
    changed = False
    was_strict = False
    warned = False
    for line in fp:
        idlevel = -1
        strict = was_strict
        was_strict = False
        pp = True if recent[-2] == "\\" and pp else False
        if re.match("[\t ]*[A-Z0-9_]*:[\t ]*\n",line):
            #a label
            if verbose: sys.stdout.write(" " + line.replace(' ', '_').replace('\t', '    '))
            result += line
            recent = (recent + line)[len(line):]
            continue
        if not pp and re.match("^[\t ]*#[^\n]*\n", line):
            #a Preprocessor directive
            pp = True
        if pp:
            result += line
            if verbose: sys.stdout.write(" " + line.replace(' ', '_').replace('\t', '    '))
            recent = (recent + line)[len(line):]
            continue
        for ch in line:
            just_opened = 0
            recent = (recent + ch)[1:]
            if not comment and not string and not char:
                if recent[-2:] == "//": break
                elif recent[-2:] == "/*": comment = True 
                elif recent[-1] == "\"": string = True
                elif recent[-1] == "'": char = True
            elif comment:
                if recent[-2:] == "*/": comment = False
            elif string:
                if not escape and recent[-1] == "\\": escape = True
                elif escape: escape = False
                elif recent[-1] == '"': string = False
            elif char:
                if not escape and recent[-1] == "\\": escape = True
                elif escape: escape = False
                elif recent[-1] == "'": char = False
            if not comment and not string and not char:
                if re.match("[a-zA-Z0-9_]", ch):
                    word += ch
                else:
                    if word:
                        if not pc:
                            if (stack and 
                                not stack[-1].expect_cond and 
                                not pc and 
                                stack[-1].should_pop(word, True)):
                                stack = stack[:-1]
                            if word == "if":
                                stack.append(StackToken(True, set(["{", "~;", "~}"]), 1, "if"))
                            elif word == "else":
                                stack.append(StackToken(False, set(["{", "~;", "~}", "if", "while", "for", "switch", "do"]), 1, "else"))
                            elif word == "while":
                                stack.append(StackToken(True, set(["{", "~;", "~}"]), 1, "else"))
                            elif word == "for":
                                stack.append(StackToken(True, set(["{", "~;", "~}"]), 1, "for"))
                            elif word == "case" or word == "default":
                                stack.append(StackToken(False, set(["case", "default", "~}"]), 1, "case"))
                            if idlevel == -1:
                                idlevel = len(stack) - (0 if not stack else stack[-1].diff)
                        word = ""
                    if ch not in "\r\n\t " and not pc:
                        was_strict = (ch in ";}")
                    if stack and stack[-1].expect_cond:
                        if pc == 0 and ch == '(':
                            stack[-1].expect_cond = False
                    if(ch == '('): pc += 1
                    if(ch == ')' and pc > 0): pc -= 1
                    if stack and pc == 0 and not stack[-1].expect_cond:
                        stack[-1].diff = 0
                    while (stack and 
                           not stack[-1].expect_cond and 
                           not pc and 
                           stack[-1].should_pop(ch, False)):
                        stack = stack[:-1]
                    if (stack and 
                        not stack[-1].expect_cond and 
                        not pc and 
                        stack[-1].should_pop(ch, True)):
                        stack = stack[:-1]
                    if ch == '{':
                        if stack and stack[-1].kind == "case" and re.match(".*:[\\n\\r\\t ]*{$", recent):
                            stack = stack[:-1]
                        stack.append(StackToken(False, set(["}"]), 1, "block"))
                    if ch not in "\t \n\r" and idlevel == -1:
                        idlevel = len(stack) - (0 if not stack else stack[-1].diff)
                    while (stack and 
                            not stack[-1].expect_cond and 
                            not pc  and
                            "case" != stack[-1].kind and
                            stack[-1].should_pop(ch, False)):
                        stack = stack[:-1]
        if idlevel == -1:
            idlevel = 0 if not line.strip() else len(stack)
        spaces = 0
        for ch in line:
            if ch == ' ': spaces += 1
            elif ch == '\t': spaces += ts
            else: break
        if should_warn and ((spaces - idlevel * ts > 0 and strict and line.strip()) or (spaces - idlevel * ts < 0)):
            changed = True
            warn_line = "// Indent warnning: Suspicious Alignment (stack_size = %d, space_diff = %d)" % (idlevel, spaces - idlevel * ts)
            warned = True
            if verbose: 
                if color: 
                    sys.stdout.write("\033[32m+%s\033[0m\n" % warn_line)
                else:
                    sys.stdout.write("+%s\n" % warn_line)
            result += warn_line + "\n"
        spaces = max(0, spaces - idlevel * 4)
        if not line.strip(): 
            spaces = 0
            was_strict = strict
        new_line = "%s%s%s\n"%('\t' * idlevel, ' ' * spaces, line.strip())
        if new_line != line:
            changed = True
            if color:
                if verbose: sys.stdout.write("\033[31m-" + line.replace(' ', '_').replace('\t', '    ') + "\033[0m")
                if verbose: sys.stdout.write("\033[32m+" + new_line.replace(' ', '_').replace('\t', '    ') + "\033[0m")
            else:
                if verbose: sys.stdout.write("-" + line.replace(' ', '_').replace('\t', '    '))
                if verbose: sys.stdout.write("+" + new_line.replace(' ', '_').replace('\t', '    '))
        else:
            if verbose: sys.stdout.write(" " + line.replace(' ', '_').replace('\t', '    '))
        result += new_line
    return (changed, result, warned)

fn=r'.*\.(c|h|cpp|cxx|hpp)$'
ts = 4
dry_run = False
verbose = False
color = False
strict_mode = True

parser = argparse.ArgumentParser(description = "Correct the indentation of source code under current working directory")
parser.add_argument('--dry-run', action = 'store_true', dest="dry_run", help = 'Do not actually modify the file')
parser.add_argument('--verbose', action = 'store_true', dest="verbose", help = 'Print the modified file to screen')
parser.add_argument('--color',   action = 'store_true', dest="color"  , help = 'Show the colored output')
parser.add_argument('--tab-stop', type = int, dest = "tabstop", metavar = "TABSTOP" ,default = 4, help = 'Set the tab-stop')
parser.add_argument('--include', type = str, dest = "pattern", metavar = "PATTERN", default = fn, help = 'The regex that mathces all the files that should be processed')
parser.add_argument('--no-strict', action = 'store_true', dest='no_strict', help = 'Turn off the strict mode, which detect unexpected indentations/alignments')

options = parser.parse_args(sys.argv[1:])

if options.dry_run: dry_run = True
if options.verbose: verbose = True
if options.color: color = True
fn = options.pattern
ts = options.tabstop
strict_mode = not options.no_strict

for root, _, files in os.walk("."):
    for f in files:
        if root.split("/")[1:][:1] == ["thirdparty"]: continue
        if not re.match(fn,f): continue
        path="%s/%s"%(root,f)
        ch, result, warned = format(file(path), verbose, color, strict_mode)
        if not dry_run:
            if ch and not verbose: 
                sys.stdout.write("Info: file %s has been changed\n"%path)
            f = file(path, "w")
            f.write(result)
            f.close()
        else:
            if ch and not verbose: 
                sys.stdout.write("Info: file %s would be changed\n"%path)
        if warned:
            sys.stderr.write("Warnning: Suspicious Indentation/Alignment has been found in file %s\n" % path)
