Plumber [![Build Status](http://plumberserver.com:8123/job/Plumber/badge/icon)](http://plumberserver.com:8123/job/Plumber/) 
[![Build Status Travis](https://travis-ci.org/38/plumber.svg?branch=master)](https://travis-ci.org/38/plumber.svg?branch=master)
----

# Quick start

- [Plumber Project Site](http://plumberserver.com)
- [Plumber Main Project](https://github.com/38/plumber)
- [Plumber Examples Repository](https://github.com/38/plumber_example)
- [Introduction to Plumber (Slides)](http://plumberserver.com/slides/index.html?slideshow=plumber-intro)
- [Explaination for the Plumber Based Static Content Server](http://plumberserver.com/fileserver_example/explained_fileserver_pss.html)

# What is Plumber

Plumber is a runtime environment which makes nanoservice feasible,
it is a infrastructure based on the concept of "pipe". It provides a runtime environment 
for pipe based, asynchronized, ultra lightweight micro-service we called servlet and a high-level domain specific 
language to describe the high-level software architecture. The Plumber framework has multiple language bindings,
and user should be able to develop each part of their software in the language that fits the task most. Currently
we support C, C++, Javascript and Python, and new language support is coming.

# Try Plumber

## Docker (Linux)

Thanks to the container techenoloy, you can explorer the Plumber environment without installing any dependencies

- You can also try the file server example with docker and open [http://localhost:8080/](http://localhost:8080/) in browser

```
docker run --rm -t -i --network=host haohou/plumber-fileserver-example --port=8080
```

- To make the static content server serves the content you provided using command

```
docker run --rm -ti -v <path-to-serve>:/www haohou/plumber-fileserver-example --root=/www
```

- To play with the precompiled Plumber interactive REPL shell use command

```
docker run --rm -t -i haohou/plumber-minimal -c pscript
```

## Try Plumber with the example sandbox (Linux/MacOS/Windows WSL)

Currently we have a automated tool to build an isolated sandbox environment. To get the sandbox environment, 
using the following command

	git clone --recursive https://github.com/38/plumber_examples.git

The minimal required dependencies are 

	- Python 2 (Python 2.7 Recommended)
	- CMake 2.6 or later (CMake 3+ Recommended)
	- libreadline 
	- GCC and G++ (GCC-5 Recommended)
	- GNU Make

for Ubuntu users, use command

	sudo apt-get install python-2.7 cmake libreadline-dev gcc g++ pkg-config make

for MacOS users, use command

	sudo brew install cmake openssl@1.0 ossp-uuid pkg-config  pkgconfig   readline

After you get the code, use 

	cd plumber_examples
	./init 

To initialize the Plumber isolated environment. Then you can go to src/ directory, compile and run the examples 
there. 

To start the static content server based on the Plumber framework

	cd src/fileserver
	make
	./fileserver.pss

After the server starts, you will be able to access [http://localhost:8080/](http://localhost:8080)

# General Idea of Plumber
Unlike traditional service framework, which modelling the component as a request-response based "service", 
by which means for each request, the caller is responsible for handling the response. Plumber use an innovative
pipelining model. Each component do not need to return any value to the caller, but provide the result 
to its downstream component via a mechanism called "pipe".

The pipe API provides good abstraction for the communication between different servlets. The details about inter-component
communication are transparent to servlet using pipe API. And a pipe can be either in the same machine or cross the machine
boarder or even a load balancer. The goal of Plumber software infrastructure is the software shouldn't aware if itself is
running on a single machine or a large cluster.

The servlet is a small piece of code runs under the Plumber runtime environment. 
It's an ultra ligthweight nanoservice, by which means multiple servlet can share a same worker thread and occupy minmum
amount of resources. The isolation provided by the traditional micro-service architecture still applies, between servlets it only
has data dependencies and the servlet has to access data via the pipe APIs. The programming model of a servlet is quite simple, 
it takes one or more input, and produces one or more output. For each time the exec function is called, the servlet reads data 
from input pipes and produces result and write it to output. Plumber has a few APIs provided to servlet. Each servlet is a really 
strighforward program, which reads from the input pipes and writes the result ot the output pipes. The Plumber runtime environment 
provides a simple UNIX-like abstraction API for pipe manipulations. And the details of the network and inter-servlet communication 
are completely hidden by the environment. 

Plumber has a centralized protocol database which manages all the communication protocols between different components, the pipes
are optional structured and strong typed. The mechanism of the protocol database also makes the upstream and downstream *always*
compatibile. 

# How to compile?
To compile this project you need CMake 2.6 or later. 

	cmake . && make 

You can set environment variable to change the compile options. To learn all the configuration, use 

	make show-flags 

to see the full configuration parameters. 

# Language Support
## C/C++ support
Plumber supports all the language that can produce native binary code and supports dynamic link. 
Currently we only have the header definition for C and C++. 

## Python support
If you want to compile PyServlet you need python development file installed on you computer. 
To compile this project with default configuration.

## For javascript support:
You need the plumberv8 project built and installed in your computer and configure Plumber with the following 
configure parameter, to build plumberv8, look at the repository page at [https://github.com/38/plumberv8](https://github.com/38/plumberv8)

	cmake -DPLUMBER_V8_PREFIX=<your-plumber-v8-install-prefix> -Dbuild_javascript=yes .

## How to build native servlet
To build you own user-space program, you need to compile your code with libpservlet which is a part of this repository. The libpservlet provides your code some functions.

	#include <pservlet.h>
	// The context of the servlet
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

To compile this code, use command

	gcc servlet.c -o libservlet.so -lpservlet -fPIC

Note that you may need to give the compiler include path and library search path depends on where libpservlet located.

If you have Plumber installed, you will be able to use the servlet.mk to build your own servlet. You can create build.mk 
on your source code directory

	SRC=servlet.c
	TARGET=servlet

And then make the servlet using command

	make -f <your-plumber-install-prefix>/lib/plumber/servlet.mk

# How to use scripting language for servlet

For the scripting language, instead of loading the servlet bindary itself, you need to load the script loader servlet.
For python, you need use pyservlet as the script loader, for example, if we have myservlet.py as the servlet, we can use

	myservlet := "pyservlet myservlet"

To load the servlet. 

For javascript, we need to use the servlet called javascript.

# PSS Language: Plumber Service Script
The PSS language is the scripting language used to define the software layout from high level, it actaully defines the
application you want to build with Plumber. It combines all the components with the pipe using the language. Also it's 
a javascript-like, turing completed scripting language in which software is a first-class-citizen. Which means you will
be able to create an software generator with the PSS language!

- The simple example of how to create a static content server with plumber

```
	import("service");
	//define the file server
	fileserver = {
		/* Define the servlet node */
		parse_req := "getpath";
		dup_path  := "dup 2";
		mime_type := "mime mime.types";
		read_file := "read doc/doxygen/html";
		error     := "cat 2";
		gen_res   := "response";
		/* Define the pipes */	
		() -> "request" parse_req "path" -> "in" dup_path {
				"out0" -> "path" mime_type "mime" -> "mime";
				"out1" -> "path" read_file {
					"content" -> "content";
					"size"    -> "size";
					"status"  -> "status";
				}
		} gen_res "response" -> ();
		{
			parse_req "error" -> "in0";
			read_file "error" -> "in1";
		} error "out" -> "error" gen_res;
	};
	Service.start(fileserver);
```

# Profiling
The service infrastructure provide two ways for profiling, one is use the Plumber built-in servlet profiler, 
which is just accumulate the execution time of this servlet. This is useful when you are looking at why the
service runs slow, because it provides a good way to discover the bottleneck. **This feature is disabled by 
default, because it will have some impact on the performance.** To enable this feature, you need to compile 
the code with predefined macro *ENABLE_PROFILER*. After the build, you can use 
	
	profiler.enabled = 1 

to enable the profiling feature and use
	
	profiler.output = "<filename>"

to make it output to a file or alernatively

	profiler.output = ""

will make the profiling data dumped as a **notice** log. 

Another way to for profiling is more focus on the framework itself. Using the gpprof tool we can get the call
graph and the time based on the stack stampling.
To enable this, you need to configure the project in this way:
	
	LIBS=-lprofiler CFLAGS=-DGPROFTOOLS cmake .

After compilation, the pscript binary will generate profiling data file for each run.

