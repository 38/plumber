Chapter 2: Servlets
---------------
##Basic Concept
Servlet is one of the core concepts of Plumber. A servlet can be described as a piece of user-space
program (note that at this point the user-space means the code is not the kernel functionality of 
Plumber). A servlet is a basic part of a server, different servlets can be connection to each other
by pipe mechanism. When we talk about the servlet, we might refer to either a **servlet binary** or
a **servlet instance**. 

A servlet binary is a shared object (or a dymanic library) which implements the Plumber servlet protocol.
The servlet binaries are usually stored on the disk, and the Plumber framework will search for the required
servlet from the searching directories. The Plumber binary loader will load the binary and load the **servlet
binary definition** data structure, which defines the callback functions and other metadata, such as name, version,
servlet instance constimized data size, etc.

After the servlet binary is found, a load procedure will proceed, so that
the Plumber kernel creates a servlet instance for this servlet binary. The Plumber kernel accepts arbitrary arguments
for the servlet, and because of the differences of the arguments, the servlet binary may produce multiple
servlet instances.

For example, there's a servlet binary called cat. It's a servlet that reads from serveral different inputs and
concatenate the data from the inputs to the output pipe. (This is very similar to the UNIX command cat)
Unlike the cat program in UNIX operating system, Plumber's cat takes only one argument which is a decimal interger
indicating how many input pipes does this cat instance have. For example, when we use servlet arguments 
```
	cat 3
```
This means we have the servlet instance as following figure shows.

~~~{.graphviz}
	digraph G {
		c3[shape=box, label = "cat 3"];
		i0[shape=circle, label = "input0"];
		i1[shape=circle, label = "input1"];
		i2[shape=circle, label = "input2"];
		o[shape=circle, label = "output"];
		i0 -> c3;
		i1 -> c3;
		i2 -> c3;
		c3 -> o;
	}
~~~

If we instantiate the servlet using a different arguments 

```
	cat 2
```

The input and output layout will get changed to only having input0 and input1 plus the output pipe. 
You can use the tool pstest to see the pipe layout of a servlet intance. 

```
	pstest -s <search_path> -l <servlet_name>
```

For example, we can use the tool to show the pipe layout of *cat 3* 

```
	$ ./pstest -s examples/fileserver/ -l cat 3
	Name    : cat
	Desc    : Concatenate N pipes
	Version : 0x00000000
	Pipes   : [ID]	Name	Flags
	          [ 0]	in0	0x00000000(R)
	          [ 1]	in1	0x00000000(R)
	          [ 2]	in2	0x00000000(R)
	          [ 3]	out	0x00010000(W)
```

## Lifecycle of servlets
In this section, we are going to discuss the lifecycle of a servlet. Although the name of *servlet* comes from the 
word server, but lifecycle of servlet is very different from lifecycle of a service. For the traditional service,
the lifecycle can be simplified as: after the server initialized, the server keep listening from the network interface
and if there's any request, go ahead process the request.

```{.graphviz}
	digraph G{
		Initialization[shape = box];
		Listening[shape = box];
		Processing[shape = box] 
		Initialization -> Listening-> Processing -> Listening;
	}
```

For a servlet, things get very different. First of all, the servlet does not have concept of network interface. Instead of listening to
a network interface, the servlet is a data driven model. After the servlet has been initialized, the servlet will remains deactivated
state until all it's input pipes are gets ready. When it's in an idle state, a servlet doesn't occupy any process or thread and no need
for starting a listening loop. 

```{.graphviz}
	digraph G{
		Init[shape = box];
		Exec[shape = box];
		Idle[shape = box];
		Cleanup[shape = box];
		Init -> Idle -> Exec -> Idle;
		Idle -> Cleanup;
	}
```

## Tasks
We are going to talk about the tasks, in Plumber term, the task is defined as a signle time execution of a piece of servlet code. For example,
when there's an incoming request, a task will be created which execute the *exec* callback of the servlet connected to the TCP pipe. Not only 
*exec*, all the life phrases of a servlet instance are tasks. So we have a three different types of tasks, initialization task, execution task
and cleanup task. We will describe the difference of each type of task later.

### Initialization Task
In this section we are going to talk about the initialization stage of a servlet. The key callback in this phrase is init, which has the prototype

```{.c}
	int (*init)(uint32_t argc, char const* const* argv, void* data);
```

The argument *argc* and *argv* is quite similar to a normal program, the Plumber framework allows user pass arbitrary UNIX-style command line parameter
to the servlet, to change the serlvet's behavior. In addition, you are even able to use the standard *getopt* or C++'s boost argument processor
to handle this command line arguments. The last parameter is the servlet instance owned memory, in which the servlet can store customized data
(typically the data contains the pipe descriptor we are going to talk later). 

When the servlet is being instantiate, the servlet callback init will be invoked and in the init function, the servlet instance is able
to parse the servlet arguments and initialize the pipes and other resources related to this servlet instance. In this init function, the
servlet is allowed to create pipes by calling the framework function **pipe_define**, the function will return a pipe descriptor, which is
a integer number, and we are able to operate the pipe using this pipe descriptor. In the initialization callback, the servlet instance also
have the chance to initialize it's own data (for example keep the pipe descriptor in the servlet instance data).

In Plumber framework, a servlet is not allowed to change the pipe layout, which means define new pipes, change the direction of an existing pipe
or remove an existing pipe. **The servlet must define all the pipe it needed in execution during the initialization phrase**.

Also, since there's actually no pipe avaliable when the initializtion task is invoked, so the invokation to pipe manipulation APIs will always fail.
(The only expection is *pipe_eof* which will always return positive result)

### Execution Task
This is the "main function" of a servlet, for each time the Plumber server gets a request from external source, the framework will convert the request
to a serie of execution tasks. Each task only related to one external request, and after the execution task is done, the servlet will back to idel state,
which doesn't occucpy any processor resource. The basic form of an exuection task code is

```{.c}
	int (*exec)(const void* data);
```

As we can see, the servlet instnace data, in which we carry the pipe descriptor, is accessible as a constant. Since the servlet instances may have 
multiple execution tasks at the same time when the multi-threading enabled, **changing the servlet data in a execution task will introduce undefined
behavior**.

the key functionality of the execution task is process the data from the input pipe, and produce the request to the output pipe. 
The task is allowed to invoke all the pipe manipulation functions, for example, read from an input pipe, write to an output pipe, change a 
pipe's flags, etc. But the limitation of this type of tasks is it's not allowed to change the direction of pipe (although the pipe direction is
described in the pipe flags) and change the number of the pipes by calling *pipe_define*. We do not have a function to open/close a pipe in execution task.
Unlike the file object in UNIX opearting system, all the pipe are opend during the task created and closed automatically after the execution task is done.
The execution tasks plays a core rule in the Plumber Server, since all the incoming requests are handled by a series of tasks. 

### Cleanup Task
This task is the "destructor" of a servlet instance, in this task, the only thing the code supposes to do is to release all the resources this servlet instance 
occupied. For example, if the initialization task allocated some memory for some purpose, the cleanup task is supposed to release the memory purpose when it get
called. It has the form of 

```{.c}
	int (*cleanup)(void* data);
```

## Overview of Plumber's Binary Interface
We have already learned three concepts in the Plumber binary interface, servlet binary, servlet instnace and tasks. First, we load the servlet binary
from the disk, and make it a servlet binary object. Then the servlet binary will bind with initialization arguments provided by user to make a servlet instance.
During this time, the initialization task will be invoked and the servlet instance data will be filled by the initialization task. After that the Plumber
framework will be able to create tasks using the existing servlet instance and pipe handle. Here's the differences between those concepts.

|         | Instance of | Can be inistantiate into | Initialize Shared Data structure | Inistantiation arguments |
|-------|--------------|------------------|------------|-----------|
|Servlet Binary| Shared library on disk | Servlet instance | Servlet binary definition| No inistantionation arguemnts, load from the disk directly |
|Servlet Instance | Servlet Binary | Task | Servlet Instance data  | Servlet instnace arguments(like "cat 2") |
| Task | Servlet Instance | N/A | N/A | Pipe handle table |

## Servlet in other language
Since the Plumber kernel is written in C programming language, the Plumber runtime is only able to load the native binary which follows the Plumber 
Binary Interface protocol. But it's possible to implement an adaptor which can makes user able to write servlet in other scripting or bytecode languages.
The basic idea of this approach is the instead of implementing a normal servlet, a speical type of servlet called loader is implemented. The loader will
launch the runtime enviroment of the target language and responsible for the translation of the Plumber framework funcatinality between the native Plumber 
runtime and the high level program. 

Currently we only have adapator for Python programming language. But we are planning to create more adaptor servlet and make Plumber support more languages.

## Creating Servlet uisng PServlet Library
In the end of this chapter, we introduce the partical way to create a servlet. By linking the PServlet library the target binary is comptible with Plumber
Binary Interface protocol.

A servlet should have a service definition data as following

```{.c}
	SERVLET_DEF = {
		.desc = "Describe your servlet",
		.version = 0x0, /* reserved field, current unused*/
		.size = sizeof(context_t), /* sizeof the context data */
		.init = init, /* init function */
		.exec = exec, /* exec function */
		.unload = unload /* unload function */
	};
```

Of course, you should define all the functions in the servlet definition section. In order to compile the servlet, you should use

```
	gcc servlet.c -o libservlet.so -lpservlet -fPIC
```

Here's the full example of a simple servlet.

```{.c}
	#include <pservlet.h>
	typedef struct {
		pipe_t input, output;
	} context_t;
	// Called after servlet is loaded
	int init(uint32_t argc, char const* const* argv, void* data)
	{
		context_t* context = (context_t*) data;
		context->input = pipe_define("in", PIPE_INPUT);
		context->output = pipe_define("out", PIPE_OUTPUT);
		return 0;
	} 
	// The "main function" of the servlet
	int exec(void* data)
	{
		context_t* context = (context_t*)data;
		int n;
		pipe_read(context->input, &n, sizeof(int)); 
		n ++;
		pipe_write(context->output, &n, sizeof(int)); 
		return 0;
	}
	// Do some cleanup work here
	int unload(void* data)
	{
		return 0;
	}
	SERVLET_DEF = {
		.desc = "Describe your servlet",
		.version = 0x0, /* reserved field, current unused*/
		.size = sizeof(context_t), /* sizeof the context data */
		.init = init, /* init function */
		.exec = exec, /* exec function */
		.unload = unload /* unload function */
	};
```

