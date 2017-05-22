from pservlet import pipe_define, PIPE_INPUT, PIPE_OUTPUT
from PyServlet import Pipe
def init(args):
    return (pipe_define("in", PIPE_INPUT),
            pipe_define("out", PIPE_OUTPUT))

def execute(ctx):
    inp = Pipe(ctx[0], line_delimitor = "\r\n")
    oup = Pipe(ctx[1], line_delimitor = "\r\n")
    ua = """<html>
    <head>
        <title>Your User-Agent String</title>
    </head>
    <body>
        <p>Hello World, This is a Plumber-Python Demo</p>
        <p>Your user agent string is: %s</p>
    </body>
</html>"""%inp.readline()
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
