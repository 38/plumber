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

def format(fp):
	comment = False
	string  = False
	escape = False
	char = False
	idlevel = 0
	recent = "  "
	result = ""
	changed = False
	for line in fp:
		spaces = 0
		pp = False
		for ch in line:
			if ch == ' ': spaces += 1
			elif ch == '\t': spaces += ts
			else: break
		if re.match("[\t ]*[A-Z0-9_]*:[\t ]*\n",line):
			#a label
			result += line.strip() + "\n"
			if line != line.strip() + "\n": changed = True
			continue
		if re.match("^[\t ]*#[^\n]*\n", line):
			#a Preprocessor directive
			result += line.strip() + "\n"
			if line != line.strip() + "\n": changed = True
			pp = True
		level = idlevel - (len(line.strip()) != 0 and line.strip()[0] == '}')
		spaces -= level * ts
		if spaces < 0: spaces = 0
		header = '\t'*level + ' '*spaces  
		for ch in line:
			recent = (recent + ch)[1:]
			if not comment and not string and not char:
				if recent == "//": break
				elif recent == "/*": comment = True 
				elif recent[1] == "\"": string = True
				elif recent[1] == "'": char = True
			elif comment:
				if recent == "*/": comment = False
			elif string:
				if not escape and recent[1] == "\\": escape = True
				elif escape: escape = False
				elif recent[1] == '"': string = False
			elif char:
				if not escape and recent[1] == "\\": escape = True
				elif escape: escape = False
				elif recent[1] == "'": char = False
			if not comment and not string and not char:
				if(ch == '{'): idlevel += 1
				if(ch == '}'): idlevel -= 1
		if not pp:
			newline = header + line.strip() + "\n" if line.strip() else "\n"
			result += newline
			if newline != line: changed = True
	fp.close()
	return changed,result

fn=r'.*\.(c|h|cpp|cxx|hpp)$'
ts = 4

for root, _, files in os.walk("."):
	for f in files:
		if root.split("/")[1:][:1] == ["thirdparty"]: continue
		if not re.match(fn,f): continue
		path="%s/%s"%(root,f)
		ch, result = format(file(path))
		if ch: print "Info: file %s has been changed"%path
		f = file(path, "w")
		f.write(result)
		f.close()


