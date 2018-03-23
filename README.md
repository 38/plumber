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

Plumber is a general purpose, language independent and high performance [Flow-Based Programming](https://en.wikipedia.org/wiki/Flow-based_programming) framework. 
With Plumber, applications are built with small, independent and composable components written in different programming languages. Plumber brings many modern technologies and innovation that enables fully flow-based development experience in modern use cases.

For more details please visit the project [home page](https://plumberserver.com).

# Try Plumber

## Try the Application - The PINS Web Server

The Plumber Project home page is actually powered by this web server. 
The code lives in [the Plumber examples repository](https://github.com/38/plumber_examples/tree/master/src/fileserver). You can play with the server in following ways.

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


