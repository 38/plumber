/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief The service graph type checker
 * @detials This is the implementation of the strong type service graph
 * @note <h1>Notes on the pipe type equations</h1>
 *       We abstract the type inference problem of a service graph to a group of type equations. <br/>
 *       <h2>Basic concepts</h2>
 *       <ol>
 *       	<li>Simple Type Constant   : A type name in the ptype system, see libproto documentation for the details </li>
 *          <li>Simple Type Variable   : A variable used to capture a single type name in the type equation </li>
 *          <li>Compound Type Constant : A series of simple type name, which can be formally defined as
 *                                       <code>CompoundType := CompoundType SimpleType | SimpleType SimpleType </code> </li>
 *          <li>Compound Type Variable : A variable used to capture a compund type constant in the type equation </li>
 *       </ol>
 *       <h2>Type Convertiblity</h2>
 *       The equation allows the simple type to be converted from one to another. And convertibiliy relationship is denoted by
 *       the notation "-&gt;" <br/>
 *       We can say "a -&gt; b" if either of following statement is true
 *		 <ol>
 *       	<li>The type b is the generalization of type a. </li>
 *       	<li>The type b is literally the same as type a. </li>
 *       </ol>
 *       We also defined a compound type convertibility, which means two compound type constants are in the same length,
 *       <code>
 *       	A -> B <=>
 *       	length_of(A) = length_of(B) &&
 *       	A[k] -> B[k] for all k in [0, length_of(A))
 *       </code>
 *       <h2>Type Convertibility Equation</h2>
 *       We can add type variable to the convertiblity expressions, which makes a type convertibility equation.
 *       However, we add some limit on what kind of convertibility equation are valid. <br/>
 *       For a type expression, a compund type variable can not be in the middle of the type expression.
 *       For example, we have a type expression:
 *       <code>
 *       	encrypted compressed myActualData -> T myActualData
 *       </code>
 *       without the limit, T will be able to capure the compound type "encrypted compressed". However, this dosn't
 *       really make sense in our case. Because a servlet can either handle a encrypted data or compressed data, but
 *       can't do both. If there's a servlet can do the both thing, it's reansonble to have a simple type called
 *       encrypted_and_compressed.  <br/>
 *       So all the variables in the middle of the expression are considered to be simple type variable, if the last
 *       variable is also the end of the expression then the type variable are considered to be a compound <br/>
 *       <h2>Modeling the Servlet Type System</h2>
 *       For each servlet, we define the type using the type expression. And in the service graph, for each incoming pipe
 *       we have an equation of
 *       <code>
 *       	[source_type] -> [input_type_expr]
 *       </code>
 *       All these tyhe convertibility equation forms a euqation system, and the type inference is actually solving all the type
 *       variables in the equation system. If there's no solution for the quation system, it means we have some type error. <br/>
 *       We in general do not inherit from the type plumber.base.Raw, which is reserved for the untyped type.
 * @file sched/type.h
 **/

#ifndef __PLUMBER_SCHED_TYPE_H__
#define __PLUMBER_SCHED_TYPE_H__

/**
 * @brief check the type of the service graph, also update the type information stored in the service graph
 * @param service the service to check
 * @note this will change the service graph and fill the inferred concrete type
 *       for each node.
 * @return status code
 **/
int sched_type_check(sched_service_t* service);

#endif /* __PLUMBER_SCHED_TYPE_H__ */
