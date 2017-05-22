from pservlet import pipe_define, PIPE_INPUT, PIPE_OUTPUT
from PyServlet import Pipe
def init(args):
    return (pipe_define("in", PIPE_INPUT),
            pipe_define("out", PIPE_OUTPUT))
def execute(ctx):
    inp = Pipe(ctx[0], line_delimitor = r"\r\n")
    oup = Pipe(ctx[1], line_delimitor = r"\r\n")
    state = inp.getstate()
    user_agent, persist = None, False
    if None != state:
        user_agent, persist = state
    done = False
    for line in inp:
        line = line.strip()
        if not line: 
            done = True
            break
        key = line.split(":")[0].lower()
        val = line[len(key) + 1:].strip()
        if key == "user-agent":
            user_agent = val
        if key == "connection" and val.lower() == "keep-alive":
            persist = True
    if not done:
        if inp.eof():
            inp.setpersist(False)
        else:
            inp.setstate((user_agent, persist))
            inp.setpersist(True)
            return 0
    if user_agent != None:
        oup.write(user_agent)
    else:
        oup.write("Unknown")
    inp.setpersist(persist)
    inp.setstate(None)
    return 0

def cleanup(ctx):
    return 0
