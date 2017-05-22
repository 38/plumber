Chapter 1: Introduction to Plumber
----
##Pipelining vs C/S

When we talk about a web service, we almost imply the architecture that the client send a 
request to the computer called "server" and wait for the server to response. This is very
similar to the senario that you (the client) ask someone else (the server) a question. This
is a very common design pattern not only on Internet but also everywhere in our life.

However, the nature of this architecture is synchronized, which means the cosumer has
to wait until the provider complete the job. This nature makes the client has to be idle until 
the server gets ready, but no one actually wants to wait. The client have 3 different states:

1. Busy: Client needs to do something
2. Idle: Client has nothing to do
3. Waiting: Client is busy, but it has to wait, because the server has not yet resnonded 

But nobody likes waiting, so we developed a very common used technique to avoid its synchronized 
nature called callbacks. The concept of callback
is not only used in Computer Science area. In real life, we use the same design pattern to improve costomer
exprience. Imagine we are sending our car to a dealship for maintainance, after you initialize your maintainance service
and provide your phone number, you are able to leave.  Because when the maintainance is done, the dealership 
will call you and you will be able to pickup you car after the call. By doing this, you don't need to 
wait for your car getting maintained. You are free to do anyother things until you get the call from them.
This works perfectly in most of the cases, but we still have to do something other than only provide the 
service to make the entire procedure asynchornized.

Here's alernative way to make people work together, pipe-lining. Most of the mordern industral 
manufacture use the technique called pipe-line. Imagine you were working in a cell phone manufacturer,
and your job is to install the screen to the phone. What you suppose to do is quite easy, whenever a
cell phone without the screen comes from the pipe line, you take it and install the screen then put it
back to the pipe line. After that the pipe line will send the unfinished phone to other workers for 
further process. During the time, you only have two states:

1. Busy: You have nothing to do
2. Idle: You get something to do 

Unlike the C/S architecture, in the pipe-lining architecture, nobody is going to wait for the others 
gets their job done. This pattern has been proven a very effecient way in industry. The nature of 
pipelining is asynchornized, which means after you put your request on the pipe line, you are free to 
take another thing, rather than wait until others get their jobs done. During the time, no one needs 
to make more rules (like the callback pattern in the C/S architecture) to make it asynchornized. 
**The nature of pipelining is asyncrhonized.**

In fact the asynchronized nature of pipelining has been widely used on modern processor,
by using pipelining technique, the processor is able to reduce the **average** execution time of each instruction
to almost one clock cycle, no matter how many clock cycles does the instruction **actually** need.
No one really enjoys waiting, that is why today we have number of asynchronize infrastructure, libraries,
languages, etc. for that. But the asynchronized nature of pipe-lining has rarely been realized by the
server developer.

The Project Plumber is an attempt of how to take the advantage of pipelining in our web services. By 
doing pipe lining, we also get another advantages.

##The Overview of Plumber
We are introducing a new service infrastructure which is compatible with the C/S world. We are not getting
some new network architecture that makes the Internet a huge pipeline.
By using Plumber the developer will still get traditional web service, which is based on C/S desgin pattern,
but Plumber makes the internal of the service to be a pipelining structure. The basic idea of Plumber is by breaking
down the entire service to many tiny parts and combining all those small parts together to get a fully functioning service.
In plumber term, eacho of those small part is called a **servlet** which means a tiny service. 

The term of servlet is a little confusing, because it's neither a traditional service, nor a process or a thread.
It takes one or more input and produce one or more output. It's a little bit similar to the concept of pipe in UNIX shell, 
when we write
```shell
	ls | wc -l
```
It means we want *ls* to read the file list under current file directory and write it to the pipe. At the same time, we want *wc* to 
consume the data from other side of the pipe and count how many lines are in the input. It will output the standard output. By doing this, 
we combine different program together, and the upstream program (or servlet) doesn't need to wait for any other parts response. The only 
job of this program is to read from *stdin* and write to *stdout*. Also, in this example, each program a simple functionality. With pipelining 
mechanism, it makes the functionality more complicated than each of them.

The senario of Plumber servlet is quite similar. For example, if we want to implement a simple server, which always outputs your user-agent
string back, you can use the following servelt layout.

~~~{.graphviz}
	digraph G{
		subgraph cluster_0
		{
			node[shape=box];
			label="Plumber Server"
			style=filled;
			color=lightgrey;
			RequestParser -> ResponseRender [len = 3, label = "User Agent String"];
		}
		Client [shape = circle];
		Client -> RequestParser [len = 3];
		ResponseRender -> Client [len = 3];
	}
~~~

The client sends a request to the Plumber server, the **Request Parser** gets the request and extracts the user agent string and produces it's 
output. The **Response Render** takes the output from the **Request Parser** and produces the user agent string echo page to the user.
Neither servlets is aware of the existance of the other servlet, the request, etc. The job of the servlet is **read input from a pipe and
produce output to another one**. 
The things are little bit different than the UNIX case, in which the input pipe is always *stdin* and the output pipes are always *stdout*
and *stderr*. Plumber actually allows the servlet to have arbitary number of pipes, each pipe has it's own property, for example the direction
of the pipe (input or output), and can be determined by the servlet. Each pipe is named, which means the pipe has a uniqe name to identify 
what pipe you are referring. 

As we can see each servlet is programtically isolated from another servlet, which means, how a servlet is implemented is totally unrelated to
how another part is implemented or designed. The only relationship between two servlet is the two connected pipe and the data format transferred
in the pipeline should be the same. This is actually a really good thing, by having this kind of isolation, we are able to use different lanaguges
in different parts and no need to worry about how to combine different parts together. At the same time, we are able to reimplement any part of 
the server without touching any other parts of the server. 

The pipelining design pattern allows us to pratice the UNIX philosophy in server software development, which is, each program only finishes a simple
and well defined job, and we have a way to combine the simple component together to make a complicated system. We will have many standard servlet which
finish a simple and well defined functionality. By having those standard servlet, we will be able to make our own service without writing even a single
line of code.

## Architecture and pipelining
Many modern web applications have to use multiple server, each server is acting as different roles. For example, we can imagine an online advertising server
has three different parts: front-end server, ranking server and advertisement indexing server.

~~~{.graphviz}
	digraph G{
		subgraph cluster_dc {
			label = "Data Center";
			color = blue;
			FE[shape = box3d, label = "Frontend"];
			R[shape = box3d, label = "Ranking"];
			I[shape = box3d, label = "Indexing"];
			R->I [label = "Keywords"];
			I->R [label = "AD List"];
			R->FE [label = "Ranked AD List"];
			FE->R [label = "Queries"];
		}
		
		Client;
		FE->Client [label = "AD Page"];
		Client -> FE [label = "request"];
	}
~~~

In most cases, the architecture of a system is hard coded to the server software, which means we are not able to change the system layout unless we change the 
code. The nature of C/S architecture do not allow us to change the system layout, without changing code, because the interface between two server are hard coded
network connections.(In the figure above, we have to hard code the link between frontend server and ranking server, etc.) 
If we look at the alernative way to implement this system by pipelining, for example, we can break it down to 5 parts, request processor, query processor, advertisement
library, ranker and pange render. 

* **Request Processor** Parse the request and extract the query from the request
* **Query Processor** Process the query, extract the advertisement related keywords 
* **AD Library** Indexing all the Advertisement and based on keyword return a AD List
* **Ranker** Adjust the order of the returned AD list
* **Pange Render** Render the page

~~~{.graphviz}
	digraph G{
		subgraph cluster_dc {
			label = "Data Center";
			color = blue;
			subgraph cluster_fe {
				label = "Frontend";
				color = lightgrey;
				style=filled;
				Req[shape = box, label = "Request\nProcessor"];
				PR[shape = box, label = "PageRender"];
			}

			subgraph cluster_r{
				label = "Ranking Server";
				color = lightgrey;
				style=filled;
				Q[shape = box, label = "Query\nProcessor"];
				Rank[shape = box, label = "Ranker"];
			}

			subgraph cluster_i {
				label = "Indexing Server";
				color = lightgrey;
				style=filled;
				I[shape = box, label = "AD Library"];
			}
			Req->Q [label = "Query"];
			Q->I [label = "Keywords"];
			I->Rank [label = "AD List"];
			Rank->PR [label = "Ranked List"];
		}
		Client;
		PR->Client [label = "AD Page"];
		Client -> Req [label = "request"];
	}
~~~

As we can see, the role of the Frontend server is actually request processor plus page render. Ranking server is actually query processor plus ranker, and Indexing server is actually
the advertisement library. 

In plumber term, pipe is an abstraction of inter-servlet communication mechanism. Because it's an abstracted concept, **the pipe interface do not
related to how it's implemented.** In this case, we have a type of pipe, which is used to abstract network communication, if we use this type of 
pipe, even thoguh the servlet code never changed, we actually change the layout of the server. This provides us the flexibility, so that we do not
need to change anything, but deploy the servlet on different machines, the architecture will be changed automatically. In this way, **the architecture
is not related to the actual code anymore.**

For instance, if we don't want 3 different types of server, instead, we want to have only frontend server and backend server, we can simple deploy
ranker, query processor and advertisement library to the same machine, so that the system layout will be change to as following figure shows.

~~~{.graphviz}
	digraph G{
		subgraph cluster_dc {
			label = "Data Center";
			color = blue;
			subgraph cluster_fe {
				label = "Frontend";
				color = lightgrey;
				style=filled;
				Req[shape = box, label = "Request\nProcessor"];
				PR[shape = box, label = "PageRender"];
			}

			subgraph cluster_r{
				label = "Backend Server";
				color = lightgrey;
				style=filled;
				Q[shape = box, label = "Query\nProcessor"];
				Rank[shape = box, label = "Ranker"];
				I[shape = box, label = "AD Library"];
			}
			Req->Q [label = "Query"];
			Q->I [label = "Keywords"];
			I->Rank [label = "AD List"];
			Rank->PR [label = "Ranked List"];
		}
		Client;
		PR->Client [label = "AD Page"];
		Client -> Req [label = "request"];
	}
~~~

By using pipelining based architectures, we do not need to hard code the architecture anymore. And the system layout becomes
configurable, and in the Plumber long term plan, there's also an idea about self adjusting architecture, which automatically
make adjusting based on the traffic pattern.
