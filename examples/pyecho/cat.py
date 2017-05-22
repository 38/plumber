import pservlet
class context:
    def __init__(self, n):
        self.inputs = [pservlet.pipe_define("in" + str(i), pservlet.PIPE_INPUT) for i in range(0, n)]
        self.output = pservlet.pipe_define("out", pservlet.PIPE_OUTPUT)
def init(args):
    return context(int(args[1]))
def execute(ctx):
    for pipe in ctx.inputs:
        while not pservlet.pipe_eof(pipe):
            tmp = pservlet.pipe_read(pipe)
            pservlet.pipe_write(ctx.output, tmp)
    return 0
def unload(ctx):
    return 0
