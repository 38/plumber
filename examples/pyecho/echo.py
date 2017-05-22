import pservlet
def init(args):
    return (pservlet.pipe_define("in", pservlet.PIPE_INPUT), 
            pservlet.pipe_define("out", pservlet.PIPE_OUTPUT))

def execute(s):
    while True:
        tmp = pservlet.pipe_read(s[0])
        if not tmp:
            if not pservlet.pipe_eof(s[0]):
                pservlet.pipe_set_flag(s[0], pservlet.PIPE_PERSIST)
            else:
                pservlet.pipe_clr_flag(s[0], pservlet.PIPE_PERSIST)
            return 0
        else:
            pservlet.pipe_write(s[1], tmp)

def cleanup(s):
    return 0
