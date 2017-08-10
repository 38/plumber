import json
def parse_out(path, verify, error):
    lines = [line.strip() for line in open(path)]
    buf = None
    label = ""
    for line in lines:
        if line[:8] == ".OUTPUT ":
            buf = []
            label = line[8:].strip()
        elif line == ".END":
            try:
                verify(label, json.loads("".join(buf)))
            except Exception as ex:
                error(label, ex)
            buf = None
        elif buf != None:
            buf.append(line)

def verify(l,o):
    print l, o
parse_out("rest.out", verify, lambda x: None)

