import pservlet
import PyServlet

def init(args):
    return (pservlet.pipe_define("in", pservlet.PIPE_INPUT),
            pservlet.pipe_define("out", pservlet.PIPE_OUTPUT))

def execute(ctx):
    inp = PyServlet.Pipe(ctx[0], r"\n", persist = True)
    oup = PyServlet.Pipe(ctx[1], r"\n", persist = True)
    line = inp.readline()
    if line:
        oup.write(line)
        if line.strip() == "bye":
            inp.persist = False
    else:
        inp.persist = False
        oup.persist = False
    return 0

def cleanup(ctx):
    return 0
