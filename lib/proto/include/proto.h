/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The protocol database system
 * @details <h1>General Idea</h1>
 *         This library is the centralized protocol typing system for inter-servlet-communication. <br/>
 *         The library is a user-space library over the transportation layer implemented by the Plumber infrastructure's
 *         pipe mechamisim. <br/>
 *         This is the <em>recommended</em> representation layer, when the servlet is implemented. <br/>
 *         The system is desgined to address the problem such as, if servlet A defineds a output data type, and servlet B
 *         comsumes it. However, when the servlet A changed the data definition, servlet B must change the code to fit the
 *         new protocol. When the system is growing larger and larger, this is a big problem and may lead the entire system
 *         fail. <br/>
 *         JSON is used to avoid this, because when the upstream change the protocol, there's no need for downstream servlet
 *         change the code, unless the protocol deleted or renamed the required field. <br/>
 *         The idea of the protocol database is similar to the JSON approach. It's a system that the servlet will be able to
 *         address the field by name rather than offset. <br/>
 *         However, JSON approach is reasonable in the Web API scenario, because the network communication is the major overhead,
 *         and serializing and deserializing the data from/to JSON format is not a huge cost compare to the network latency. <br/>
 *         However, in the Plumber infrastructure, all the parts are most likely communicate with shared memory, which means
 *         serializing and deserializing is a major overhead if we use this approach. <br/>
 *         The protocol database is a different approach, when the servlet is initializing, the servlet should query the database
 *         about the protocol type the servlet interested. And will be able to get the field offset from the query. <br/>
 *         When the servlet handles a request, it will use the offset previously get from protocol database query to get the value.
 *         In this way, we have a typing system which is addressing by name at the same time, we avoid the parsing overhead. <br/>
 *         Also, by making the protocol centrialized, different servlet will be able to share the data communication protocol. <br/>
 *         <h1>Managing Protocol Types</h1>
 *         The protocol types can be managed by the <em>protoman</em> tool. <br/>
 *         <code>protoman apply typedesc.ptype</code>
 *         The code above will apply the protocol type defition in the file to the database. Which means if the type is not exists,
 *         we will define a new type, otherwise, we will override the previous type.
 *         <code>protoman remove graphics.Point2D</code>
 *         This command will remove the type named Point2D under namespace grpahics and <em>all the type references this type</em>
 *         <br/>
 *         Note: All the operation should validate the protocol database in a good state, which means no undefined references <br/>
 *         <h1>How the protocol data is organized</h1>
 *         It will use the filesystem as database, just like Java. Namespaces are implemented by directory. <br/>
 *         When the database modification is going on, all the operations will be validated and then posted <br/>
 *         <h1> How a servlet can use the database </h1>
 *         Using the query API.
 * @file proto/include/proto.h
 **/
#ifndef __PROTO_H__
#define __PROTO_H__

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include <error.h>

#include <proto/ref.h>
#include <proto/type.h>
#include <proto/err.h>
#include <proto/db.h>
#include <proto/cache.h>

/**
 * @brief initialize the libproto
 * @return status code
 **/
int proto_init();

/**
 * @brief finalize the libproto
 * @return status code
 **/
int proto_finalize();

#endif /* __PROTO_H__ */
