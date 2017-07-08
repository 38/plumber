from pservlet import pipe_define, PIPE_INPUT, PIPE_OUTPUT, SCOPE_TYPE_STRING, RLS_Object, RLS_String
from PyServlet import Pipe, Type
def init(args):
    input_pipe  = pipe_define("in", PIPE_INPUT, "plumber/std/request_local/String")
    output_pipe = pipe_define("out", PIPE_OUTPUT)
    type_context = Type.TypeContext()
    @type_context.model_class(name = "input", pipe = input_pipe)
    class _InputModel(Type.ModelBase):
        token = Type.ScopeToken()
    return (type_context, output_pipe)

def execute(ctx):
    type_instance = ctx[0].init_instance()
    oup = Pipe(ctx[1], line_delimitor = "\r\n")
    user_agent = RLS_String.get_value(RLS_Object(SCOPE_TYPE_STRING, type_instance.input.token))
    ua = """<html>
    <head>
        <title>Your User-Agent String</title>
    </head>
    <body>
        <p>Hello World, This is a Plumber-Python Demo</p>
        <p>Your user agent string is: %s</p>
    </body>
</html>"""%user_agent
    try:
        oup.write("HTTP/1.1 200 OK\r\n")
        oup.write("Content-Type: text/html\r\n")
        oup.write("Content-Length: %d\r\n"%len(ua))
        oup.write("Connection: %s\r\n"%("keep-alive" if oup.ispersist() else "close"))
        oup.write("\r\n")
        oup.write(ua)
    except IOError:
        pass
    return 0

def cleanup(ctx):
    return 0
