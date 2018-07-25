#!/usr/bin/python
# The reason why I write such a script is I think the best way for the indentation is 
# mixing spaces with tabs. A lot of people suggest that we should totally use space 
# However, this increase the size of the source code and makes other uncomfortable 
# with the "hard indentation". When I have a smaller screen, I need the TABSTOP become
# smaller, however the space approach fail to do this.
# The good way for indentation is use tabs for indentation, use space for 
# alignment.
# This is the tool that fix the indentation problems
import sys
import re
import os

def format(fp, verbose = False):
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
                                stack.append((1, set(["{", "~;"]), 1))
                            elif word == "else":
                                stack.append((0, set(["{", "~;", "if", "while", "for", "switch", "do"]), 1))
                            elif word == "while":
                                stack.append((1, set(["{", "~;"]), 1))
                            elif word == "for":
                                stack.append((1, set(["{", "~;"]), 1))
                            elif word == "case":
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
                        stack.append((0, set(["}"]), 1))
                    if ch not in "\t \n\r" and idlevel == -1:
                        idlevel = len(stack) - (0 if not stack else stack[-1][2])
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
            if verbose: sys.stdout.write("\033[31m-" + line.replace(' ', '_').replace('\t', '    ') + "\033[0m")
            if verbose: sys.stdout.write("\033[32m+" + new_line.replace(' ', '_').replace('\t', '    ') + "\033[0m")
        else:
            if verbose: sys.stdout.write(" " + line.replace(' ', '_').replace('\t', '    '))
        result += new_line
    return (changed, result)

fn=r'.*\.(c|h|cpp|cxx|hpp|pss)$'
ts = 4

for root, _, files in os.walk("."):
    for f in files:
        if root.split("/")[1:][:1] == ["thirdparty"]: continue
        if not re.match(fn,f): continue
        path="%s/%s"%(root,f)
        ch, result = format(file(path), False)
        if ch: print("Info: file %s has been changed"%path)
        f = file(path, "w")
        f.write(result)
        f.close()

