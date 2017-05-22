import pservlet
import PyServlet

def init(args):
    return (pservlet.pipe_define("in", pservlet.PIPE_INPUT),
            pservlet.pipe_define("out", pservlet.PIPE_OUTPUT))

def execute(ctx):
    inp = PyServlet.Pipe(ctx[0], r"\n")
    oup = PyServlet.Pipe(ctx[1], r"\n")
    inp.setpersist(True)
    line = inp.readline()
    if line:
        prev = inp.getstate()
        if prev == None: 
            prev = "None\n"
        oup.write("Current Line: %s"%line)
        oup.write("Prevoius Line: %s"%prev)
        inp.setstate(line)
        if line.strip() == "bye":
            inp.setpersist(False)
    else:
        inp.setpersist(False)
    return 0

def cleanup(ctx):
    return 0
