Chapter3: Pipes
---
##Overview
In this chapter we are going to look at the pipe mechanism in Plumber. The pipe mechanism is the 
most important mecahanism in the Plumber framework. It's an high level abstraction of the communication
between tasks(a.k.a Inter-Task Commucation, ITC). With pipes, we are able to assemble the small and 
simple servlet into a large service. 
As we have seen in the previous chapter, in user-space servlet, the pipe is refered by a integer called
pipe descriptor. However, inside the Plumber kernel, a pipe for a task is described by a data strcuture 
called **pipe handle**.
When we mentioned pipe in Plumber term, we may refer two different things:

1. In servlet, pipe is a input/output port from which the execution task is able to read/write data
2. In Plumber kernel, pipe is either input/output pipe handle or a pair of pipe handle with which data can
   be transferred between tasks.

For clearification, we use the term pipe port to refer the pipe in servlet term (the first exlaination).
Use the pipe object to refer the pipe in the kernel term (the second meaning).

There are three kinds of pipe objects, input pipe object, output pipe object and ITC pipe object. The input
pipe object is the pipe object that allows the servlet read the external incoming data from the input port.
Similarly, the output pipe object is the pipe object that allows the servlet write data to external resources.
The input/output pipe object is the basic abstraction of the network connection. The input pipe object and 
output pipe object are always created at the time a new request comes in, and we describe the two related 
pipe object as a companion. A companion basically refers the pipe objects that shares the same resource. 
(For example for TCP input pipe object and output pipe object for a signle request, it actually shares the 
 same TCP connection).

Another type of pipe objects is ITC pipe, an ITC pipe is a pipe that can transfer data between two different
handle. A typical type of ITC pipe is the **mempipe**, which is shared memory based pipe mechanism. When we
refer a companion of a ITC pipe handle, it means the other side of the pipe. 

In order to manipulate different of pipes, the Plumber kernel has an abstract layer called ITC module, which 
is used for pipe manipulations. In this chapter we are going to discuss

1. The pipe manipulation provided by the Plumber kernel to the exeuction task
2. The definition of ITC abstraction layer.

##Pipe Declaration
Each servlet is supposed to declare the pipe it is going to use. As we know, this happens in the initialization task.
The only function involved in the pipe declaration is *pipe_define*, which has the following proto type

```{.c}
	pipe_t pipe_define(const char* name, pipe_flags_t flags);
```

To declare the pipe, you should assign the pipe port an unique name. In later chapter we can see the user-space service 
script is rely on this pipe name to connect different pipe ports. And the pipe flags is the bit flags that describes the 
property of the pipe, like the direction of this pipe, if the pipe is expected to be a persist connection (for instance,
for a TCP pipe for a keep-alived HTTP connection).

The *pipe_define* function will return a pipe desciptor in *pipe_t*, in the execution task we are able to refer pipes using
its pipe descriptor. Because for all the servlet instance shares the same servlet binary the global variables are shared 
between different instances, it's dangerous to use the global variable to pass the pipe descriptor from the initialization
task to execution task. The only safe way to pass the pipe descriptor between initiliaztion task and execution task is
use the servlet instance customized data structure. **The pipe declaration is only allowed in the init task and if pipe_define
has been called from other types of tasks, it will returns a failure result.**

The following example shows how to declare a pipe in a servlet

~~~~{.c}
	#include <pservlet.h>
	/* The servlet instance data */
	typedef struct {
		pipe_t input;
		pipe_t output;
	} servlet_context_t;

	int init(uint32_t argc, char const* const* argv, void* data)
	{
		servlet_context_t* context = (servlet_context_t*)data;

		context->input  = pipe_define("input", PIPE_INPUT);  /* Declare an input pipe named "input" */
		context->output = pipe_define("output", PIPE_OUTPUT);/* Declare an output pipe named "output */
		return 0;
	}

	SERVLET_DEF = {
		.name = "Our test servlet",
		.size = sizeof(servlet_context_t),
		.init = init
	};
~~~~

This example is quite straight forwad, we defined a context data structure include a input pipe and output pipe
descriptor. In the initialization task, we first convert the data pointer to the a servlet context, then assign
the newly created pipes to the context. In next section, we will focus on the second argument called pipe flags 
which is **PIPE_INPUT** and **PIPE_OUTPUT** in this example. 

##Pipe Flags
In the previous section, we demonstrated how to declare a pipe in the initialization task with *pipe_define*. In this
section we will focus on the pipe flags which is the second argument in *pipe_define*. In Plumber, we use pipe flags is a integer value
that represents a set of properties of a pipe. As we already know the direction of the pipe port (input/output) is determined
by the pipe flags. The constant *PIPE_INPUT* and *PIPE_OUTPUT* is actually defined by following code

```{.c}
	#define PIPE_INPUT   ((pipe_flags_t)0x00000u)
	#define PIPE_OUTPUT  ((pipe_flags_t)0x10000u)
```

These two constants are actually changing the same bits which is the bit that indicates the pipe direction. You may realize that
the direction bit do not occupy the low 16 bits in the integer. In fact, the low 16 bits is used as companion pipe descriptor which
we will discuss later.

In general, we have two different types of pipe flag, private flag and shared flag. A private flag is the flag that only affect the 
pipe port it has been set. A shared flag will transmit from the input pipe object to the output pipe object if they are companions.

###Dynamically Access/Modifiy the Pipe Flags
The Plumber API allows an execution task change the pipe flags at runtime using *pipe_cntl* function which used as misc or module specified
functionalities.

```{.c}
	int pipe_cntl(pipe_t pipe, uint32_t opcode, ...);
```

The function can be used to perform an operation defined by the opcode on the given pipe. When we need to access the pipe flags we can use
the opcode called *PIPE_CNTL_GET_FLAFS*. For example, in our example servlet in the last section, we are able to add the code in the execution
task code to access the pipe flags.

```{.c}
	int exec(const void* data)
	{
		const servlet_context_t* context = (const servlet_context_t*)data;
		pipe_flags_t flags;
		if(pipe_cntl(context->input, PIPE_CNTL_GET_FLAFS, &flags) == ERROR_CODE(int)) 
			LOG_ERROR("pipe_cntl failed");
		else
			LOG_DEBUG("The pipe flags is 0x%x", flags);
		return 0;
	}
```

For modifying the pipe flags, we have two related opcodes, *PIPE_CNTL_SET_FLAG* and *PIPE_CNTL_CLR_FLAG*. The behavior of these two opcode is quite
clear, one will set the flag bit to 1 and another will clear the flag which means set the bit to 0. The *pipe_cntl* won't change the pipe declaration,
which means **the dynamically set flag bits won't perserve in the later task**. 

Instead of perserving the modification, the pipe flags change in the execution task, may transmit between different tasks if two different task owns the
pipe ports which are companion. The dynamic access/modification mechanism is very important to modling many service design pattern, like the persist HTTP
connection with the Plumber APIs.

Although the API provide us a way to modify the pipe behavior dynamically, however, there's limits for what a execution task can do with the flag manipulation
APIs. For example, the pipe direction flags is not supposed be changed during the execution task is running. 
In fact, all pipe flags that related to the servlet data dependency in a service are supposed not to be changed during the time. 
Despite the pipe direction flag is not allowed to be changed, there's another flags which is related to the service data dependency, *PIPE_SHAWOD* which means
a pipe port contains exactly the same content of another pipe. (*Note: this flag is in the todo list*) 
This pipe flag is also not allowed to be changed in execution task. As the flag system becomes more and more complicated, there will be a lot of pipe flags is 
actually in this case. 

###Pipe Flag Bits
We will focus on the existing pipe flag bits. 
We currently have only three private flag, *PIPE_SHAWOD*, *PIPE_INPUT* and *PIPE_OUTPUT*. The *PIPE_INPUT*
and *PIPE_OUTPUT* are very straight forward, it's means if we are allowed to use the either read or write API on this pipe. 
Plumber only support the unidirectional pipe, which means each pipe port should be either input or output but not both.
You may realize that currently we have all the private flag not to be changed on execution time, but this is not always true. 
As we add more and more pipe flag bits into the flags, we will have a lot of private bits which can be changed dynamically.

We have only one shared flag currently, which is *PIPE_PERSIST*, this flag will **suggest** the Plumber kernel perserve the 
resource used by this pipe. This is how we make a TCP long connection in Plumber. Not all the types of pipe response to this
flag bit. For example, for the memory pipe, the psersist flag is meaningless and the module will release the memory allocated
for this pipe. In this case, the persist won't change the behavior of the memory pipe. Unlike the memory pipe, the TCP pipe 
manages the TCP connections by a connection pool, and the persist flag will change the dehavior of either close the connection
when he psersist bit is 0, or put the connection to inactive list when the psersist bit is 1.

