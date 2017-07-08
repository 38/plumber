from pservlet import pipe_define, PIPE_INPUT, PIPE_OUTPUT, SCOPE_TYPE_STRING, RLS_Object
from PyServlet import Pipe, Type
def init(args):
    input_pipe  = pipe_define("in", PIPE_INPUT)
    output_pipe = pipe_define("out", PIPE_OUTPUT, "plumber/std/request_local/String")
    type_context = Type.TypeContext()
    @type_context.model_class(name = "output", pipe = output_pipe)
    class _OutputModel(Type.ModelBase):
        token = Type.ScopeToken()
    return (input_pipe, type_context)

def execute(ctx):
    inp = Pipe(ctx[0], line_delimitor = r"\r\n")
    type_instance = ctx[1].init_instance()
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
    if user_agent == None: 
        user_agent = "Unknown"
    ua_object = RLS_Object(SCOPE_TYPE_STRING, -1, user_agent)
    type_instance.output.token = ua_object.get_token()
    inp.setpersist(persist)
    inp.setstate(None)
    return 0

def cleanup(ctx):
    return 0
