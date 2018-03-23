# Plumber [![Build Status](https://plumberserver.com/jenkins/job/Plumber/badge/icon)](https://plumberserver.com/jenkins/job/Plumber/) [![Build Status Travis](https://travis-ci.org/38/plumber.svg?branch=master)](https://travis-ci.org/38/plumber)

----

# Quick start links

- [Plumber Project Site](https://plumberserver.com)
- [Plumber Main Project](https://github.com/38/plumber)
- [Plumber Examples Repository](https://github.com/38/plumber_example)
- [Introduction to Plumber (Slides)](https://plumberserver.com/slides/index.html?slideshow=plumber-intro)
- [Explaination for the Plumber Based Static Content Server](https://plumberserver.com/fileserver_example/explained_fileserver_pss.html)
- [A Tutorial of Plumber](https://github.com/38/plumber-tutorial)



# What is Plumber?

Plumber is a general purpose, language independent and high performance [Flow-Based Programming](https://en.wikipedia.org/wiki/Flow-based_programming) software framework. 

> In computer programming, flow-based programming (FBP) is a programming paradigm that defines applications as networks of "black box" processes, which exchange data across predefined connections by message passing, where the connections are specified externally to the processes. These black box processes can be reconnected endlessly to form different applications without having to be changed internally. FBP is thus naturally component-oriented.

The basic idea of FPB and Plumber is very similar to [UNIX Philosophy](https://en.wikipedia.org/wiki/Unix_philosophy#Do_One_Thing_and_Do_It_Well): Do One Thing and Do It Well. 

With Plumber, small, independent and composable components written in different programming languages can be connected together as a system. Plumber also brings many modern technologies and innovative ideas which enables fully flow-based development practical in modern use cases.

# Why Plumber?

Different from traditional OOP method, Plumber makes organization of a large application easier. 

* **Modular and Composable Desgin** All components are highly composable modules. Developer can implement complicated software logic by connecting different components without touching the actual code.
* **Code Isolation** All components have on code coupling. Developer can maintain different components with seperate code base without any conflicts and compatibility issues.
* **Testablility** All components can be tested seperately without any mocking.
* **Language Independent** developer can choose programming language fits their need and connects components in differently languages easily.

Plumber makes high-performance application easier.

* **Event-Driven** Application are fully event-driven and can fully use computer hardware
* **Naturally Asynchronous** There's no need for passing callback funcions and developers are completely free from callback hell
* **Multithreaded** Unlike javascript based event-driven framework, Plumber applications utilize modern multicore CPU effeciently.

Plumber enables practical Flow-Based development. Unlike other FBP environment, a Plumber application's workflow can be:

* **Strongly Typed** Plumber protect developer from the errors that introduced by connecting wrong components and eliminating depenency hell issue.
* **Generically Typed** Components can be used for different types of input without any modification of the source code.
* **Hierarchically Described** The workflow can be described hierarchically, thus the complexity of the workflow can be hidden by the hierarchical description.
* **Dynamically Generated** Plumber provides a Turing-Complete workflow generator, which application can be generated automatically by need.  

# Try Plumber

## Try the Application - The PINS Web Server

The Plumber Project homepage is actually powered by this web server. 
The code lives in [the Plumber examples repository](https://github.com/38/plumber_examples/tree/master/src/fileserver). You can try the server in following ways.

* **Try with Docker(Linux Only)**

```bash
docker run --rm -t -i --network=host haohou/plumber-fileserver-example --port=8080
```

To serve the files other than the default page

```bash
docker run --rm --network=host -ti -v /path/to/serve:/www haohou/plumber-fileserver-example --root=/www
```
## Explore the Framework(Linux/MacOS/Windows WSL)

You can also use the sandbox environment and try the examples with [the Plumber examples repository](https://github.com/38/plumber_examples). 

```bash
git clone --recursive https://github.com/38/plumber_examples.git
```

The minimal required dependencies are 

	- Python 2 (Python 2.7 Recommended)
	- CMake 2.6 or later (CMake 3+ Recommended)
	- libreadline 
	- GCC and G++ (GCC-5 Recommended)
	- GNU Make
	
for Ubuntu users, use command

```bash
sudo apt-get install python-2.7 cmake libreadline-dev gcc g++ pkg-config make
```

for MacOS users, use command

```bash
sudo brew install cmake openssl@1.0 ossp-uuid pkg-config  pkgconfig   readline
```

After installed all the dependencies, use the following command to enter the environment.

```bash
cd plumber_examples
./init 
```

In the environment, you should be able to build and run the examples under `src` directory.

## Tutorial 

Now we have a tutorial repository in which we demonstrate how we build a simple server software setp by step. 
In this tutorial we are be able to go through most of the key concepts of Plumber software infrastructure.
Follow the [link](https://github.com/38/plumber-tutorial) for the tutorial repository.

# Needs Your Help

*If you think this project is interesting, please help us to promote the project.*

You probably know this, the Porject is still in a very early stage. There are too many development target needs to be done. But we don't have too enough people work on the project. **Any one who wants to contribute to the project are warmly welcomed.**

If you are interested regarding the project, feel free to email the [contributors](https://github.com/38/plumber/blob/master/CONTRIBUTORS).

# How to compile?
To compile this project you need CMake 2.6 or later. 

```bash
cmake . && make 
```

You can set environment variable to change the compile options. To learn all the configuration, use 

```bash
make show-flags 
```

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

```bash
cmake -DPLUMBER_V8_PREFIX=<your-plumber-v8-install-prefix> -Dbuild_javascript=yes .
```

# Profiling
The service infrastructure provide two ways for profiling, one is use the Plumber built-in servlet profiler, 
which is just accumulate the execution time of this servlet. This is useful when you are looking at why the
service runs slow, because it provides a good way to discover the bottleneck. **This feature is disabled by 
default, because it will have some impact on the performance.** To enable this feature, you need to compile 
the code with predefined macro *ENABLE_PROFILER*. After the build, you can use 
	
```
profiler.enabled = 1 
```

to enable the profiling feature and use
	
```
profiler.output = "<filename>"
```

to make it output to a file or alernatively

```
profiler.output = ""
```

will make the profiling data dumped as a **notice** log. 

Another way to for profiling is more focus on the framework itself. Using the gpprof tool we can get the call
graph and the time based on the stack stampling.
To enable this, you need to configure the project in this way:
	
```bash
LIBS=-lprofiler CFLAGS=-DGPROFTOOLS cmake .
```

After compilation, the pscript binary will generate profiling data file for each run.

