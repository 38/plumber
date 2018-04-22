# Plumber - The General-Purpose Cross-Language Dataflow Programming
----
[![Build Status](https://plumberserver.com/jenkins/job/Plumber/badge/icon)](https://plumberserver.com/jenkins/job/Plumber/) [![Build Status Travis](https://travis-ci.org/38/plumber.svg?branch=master)](https://travis-ci.org/38/plumber)

# What is Plumber?

Plumber is middleware for high-performance, general-purpose, cross-language dataflow programming.
Plumber allows developer design dataflow based system easily, and provides many features, such 
as type-checking, generic-typing, metaprogramming, in a language-neutral way.
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

## Explore the Framework with the Sandbox(Linux/MacOS/Windows WSL)

You can also use the sandbox environment and try the examples with [the Plumber examples repository](https://github.com/38/plumber_examples). 

## Tutorial 

Now we have a tutorial repository in which we demonstrate how we build a simple server software setp by step. 
In this tutorial we are be able to go through most of the key concepts of Plumber software infrastructure.
Follow the [link](https://github.com/38/plumber-tutorial) for the tutorial repository.

# Useful Links

- [Plumber Project Site](https://plumberserver.com)
- [Plumber Main Project](https://github.com/38/plumber)
- [Plumber Examples Repository](https://github.com/38/plumber_example)
- [Introduction to Plumber (Slides)](https://plumberserver.com/slides/index.html?slideshow=plumber-intro)
- [Explaination for the Plumber Based Static Content Server](https://plumberserver.com/fileserver_example/explained_fileserver_pss.html)
- [A Tutorial of Plumber](https://github.com/38/plumber-tutorial)


