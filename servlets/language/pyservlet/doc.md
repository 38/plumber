# language/pyservlet

## Description

The Python servlet loader. 
This is the servlet which manage Python interpeter to run Python written guest code.

Because the Python has GIL, so there's no way for a python servlet runs concurrently. 
There may be a performance impact of using Python in the dataflow graph.

```python
import pyservlet

def init(args):
	pass

def exec(ctx):
	pass

def unload(ctx):
	pass
```

## Ports

Any ports and type trait this servlet can have. It depends on how the servlet has been initialized and how the guest code looks like.

## Options

```
language/pyservlet <py-script-file> <python-param1> ... <python-paramN>
```

## Note

This is just a overview of Python support component of Plumber. 
