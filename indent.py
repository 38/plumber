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

def format(fp, verbose = False, color = False):
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
    for line in fp:
        idlevel = -1
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
                            if stack and not stack[-1][0] and not pc and word in stack[-1][1]:
                                stack = stack[:-1]
                            if word == "if":
                                stack.append((1, set(["{", "~;", "~}"]), 1))
                            elif word == "else":
                                stack.append((0, set(["{", "~;", "~}", "if", "while", "for", "switch", "do"]), 1))
                            elif word == "while":
                                stack.append((1, set(["{", "~;", "~}"]), 1))
                            elif word == "for":
                                stack.append((1, set(["{", "~;", "~}"]), 1))
                            elif word == "case" or word == "default":
                                stack.append((0, set(["case", "default", "~}"]), 1))
                            if idlevel == -1:
                                idlevel = len(stack) - (0 if not stack else stack[-1][2])
                        word = ""
                    if stack and stack[-1][0]:
                        if pc == 0 and ch == '(':
                            stack[-1] = (0, stack[-1][1], stack[-1][2])
                            pc = 1
                    elif stack and (stack[-1][0] or pc > 0):
                        if(ch == '('): pc += 1
                        if(ch == ')'): pc -= 1
                    if stack and pc == 0 and stack[-1][0] == 0:
                        stack[-1] = (stack[-1][0], stack[-1][1], 0)
                    while stack and not stack[-1][0] and not pc and ("~" + ch) in stack[-1][1]:
                        stack = stack[:-1]
                    if stack and not stack[-1][0] and not pc and ch in stack[-1][1]:
                        stack = stack[:-1]
                    if ch == '{':
                        if stack and "case" in stack[-1][1] and re.match(".*:[\\n\\r\\t ]*{$", recent):
                            stack = stack[:-1]
                        stack.append((0, set(["}"]), 1))
                    if ch not in "\t \n\r" and idlevel == -1:
                        idlevel = len(stack) - (0 if not stack else stack[-1][2])
                    while (stack and 
                            not stack[-1][0] and 
                            not pc 
                            and ("~" + ch) in stack[-1][1] and 
                            "case" not in stack[-1][1]):
                        stack = stack[:-1]
        if idlevel == -1:
            idlevel = 0 if not line.strip() else len(stack)
        spaces = 0
        for ch in line:
            if ch == ' ': spaces += 1
            elif ch == '\t': spaces += ts
            else: break
        spaces = max(0, spaces - idlevel * 4)
        if not line.strip(): 
            spaces = 0
        new_line = "%s%s%s\n"%('\t' * idlevel, ' ' * spaces, line.strip())
        if new_line != line:
            changed = True
            if color:
                if verbose: sys.stdout.write("\033[31m-" + line.replace(' ', '_').replace('\t', '    ') + "\033[0m")
                if verbose: sys.stdout.write("\033[32m+" + new_line.replace(' ', '_').replace('\t', '    ') + "\033[0m")
            else:
                if verbose: sys.stdout.write(line.replace(' ', '_').replace('\t', '    '))
                if verbose: sys.stdout.write(new_line.replace(' ', '_').replace('\t', '    '))
        else:
            if verbose: sys.stdout.write(" " + line.replace(' ', '_').replace('\t', '    '))
        result += new_line
    return (changed, result)

fn=r'.*\.(c|h|cpp|cxx|hpp|pss)$'
ts = 4
dry_run = False
verbose = False
color = False
    
parser = argparse.ArgumentParser(description = "Correct the indentation of source code under current working directory")
parser.add_argument('--dry-run', action = 'store_true', dest="dry_run", help = 'Do not actually modify the file')
parser.add_argument('--verbose', action = 'store_true', dest="verbose", help = 'Print the modified file to screen')
parser.add_argument('--color',   action = 'store_true', dest="color"  , help = 'Show the colored output')
parser.add_argument('--tab-stop', type = int, dest = "tabstop", metavar = "TABSTOP" ,default = 4, help = 'Set the tab-stop')
parser.add_argument('--include', type = str, dest = "pattern", metavar = "PATTERN", default = fn, help = 'The regex that mathces all the files that should be processed')

options = parser.parse_args(sys.argv[1:])

if options.dry_run: dry_run = True
if options.verbose: verbose = True
if options.color: color = True
fn = options.pattern
ts = options.tabstop

for root, _, files in os.walk("."):
    for f in files:
        if root.split("/")[1:][:1] == ["thirdparty"]: continue
        if not re.match(fn,f): continue
        path="%s/%s"%(root,f)
        ch, result = format(file(path), verbose, color)
        if not dry_run:
            if ch: print("Info: file %s has been changed"%path)
            f = file(path, "w")
            f.write(result)
            f.close()
        else:
            if ch: print("Info: file %s would be changed"%path)

