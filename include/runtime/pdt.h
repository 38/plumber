/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief PDT(pipe ID table) is a table that used to define the pipe descriptor
 *        the pipe name table is created for each servlet,
 *        when servlet's init function is called, the create pipe
 *        function will create entry in the pipe name table.
 * @note because this table is only used when interpret the service script
 *       so the performance is not critical
 * @note this file do not have dependency with other files under this directory
 * @file pdt.h
 **/
#ifndef __PLUMBER_RUNTIME_PDT_H__
#define __PLUMBER_RUNTIME_PDT_H__

/** @brief the type used for the pipe descriptor table */
typedef struct _runtime_pdt_t runtime_pdt_t;

/**
 * @brief create a new PD table
 * @return the pipe name table been created, NULL on error
 **/
runtime_pdt_t* runtime_pdt_new(void);

/**
 * @brief free the used PDT table
 * @param table the table to despose
 * @return < 0 on error
 **/
int runtime_pdt_free(runtime_pdt_t* table);

/**
 * @brief insert a new entry to the table
 * @param table the target table
 * @param name the name to be added
 * @param flags the flags of this named pipe
 * @param type_expr the type expression for this pipe
 * @note  The pipe can be strong typed, and the type expression is the description for the pipe typing system. <br>
 *        For each pipe, we actually have a structured data scheme. The typed header comes first, and then a blob data
 *        body section following behind. <br>
 *        For the pipe without strong typing, it can be treated as a strong-typed pipe with a header of 0 length. <br/>
 *        The type expression can be either a concrete type or a type pattern. The concrete type is actually a typename
 *        for the libproto centrialized type system. For example, <br/>
 *        plumber/std/Raw <br/>
 *        The type pattern is the pattern that contains any type variables, for example: <br/>
 *        name/space/to/the/type/compressed $T <br/>
 *        In which \$[a-zA-Z0-9_]+ is a type variable. <br/>
 *        All the type variables in the output pipe's type expression are considered as a reference to the type variable.
 *        All the type variable in an output pipe type expressin are considered as a convertibility equation (See the documentation for sched/type.h for details) <br/>
 *		  We also allow the compound type, for example:
 *        path/to/type-A path/to/type-B
 *        which means the body section contains the enough information to reconstruct a header of type-B from the request. <br/>
 *        This is a good example of how we describe the behavior of a transformer servlet, for example, a compressor. <br/>
 *        So the compressor's input pipe should have a type <br/>
 *        $T <br/>
 *        And the compress's output should have a type <br/>
 *        compressor/compressed $T <br/>
 *        When the service graph is constructed, then we can infer all the type pattern to the concrete type. For example, once we know
 *        what $T stands for, then we know the concrete. <br/>
 *        Also we have the union operator, which means the common ancestor of the type <br/>
 *        For example, if we have a selector servlet, which inputs $A, $B, and may outputs the either first pipe or the second pipe. Then
 *        the output pipe should have the type of $A|$B, which represents the common ancestor of $A and $B. <br/>
 *        Unlike the object oriented language, once the $A or $B have been converted to $A|$B, the additional data distinguishing type $A
 *        and type $B will be droped and the header should have the exactly same size as sizeof($A|$B). This means, once the header has been
 *        shrinked to a smaller size, the additional data will be dropped.
 * @return the newly created pipe id, < 0 on error
 */
runtime_api_pipe_id_t runtime_pdt_insert(runtime_pdt_t* table, const char* name, runtime_api_pipe_flags_t flags, const char* type_expr);

/**
 * @brief get the pipe descriptor by name
 * @param table the target table
 * @param name the target name
 * @return the pipe id, if  < 0 for error
 **/
runtime_api_pipe_id_t runtime_pdt_get_pd_by_name(const runtime_pdt_t* table, const char* name);

/**
 * @brief get the flags that is assocaited with the PD
 * @param table the target PDT
 * @param pd the pipe descriptor
 * @todo  how to pass the flags the actual pipe
 * @return the flags returned or a negative status code indicates an error
 **/
runtime_api_pipe_flags_t runtime_pdt_get_flags_by_pd(const runtime_pdt_t* table, runtime_api_pipe_id_t pd);


/**
 * @brief get the size of the PDT table
 * @param table the target PDT
 * @return the size of the PDT or 0
 **/
runtime_api_pipe_id_t runtime_pdt_get_size(const runtime_pdt_t* table);

/**
 * @brief get the number of input pipes in this table
 * @param table the target PDT
 * @return the number of input pipes or negative status code
 **/
runtime_api_pipe_id_t runtime_pdt_input_count(const runtime_pdt_t* table);

/**
 * @brief get the number of output pipes in this table
 * @param table the target PDT
 * @return the number of output pipes or negative status code
 **/
runtime_api_pipe_id_t runtime_pdt_output_count(const runtime_pdt_t* table);

/**
 * @brief get the name of the pipe for a given table
 * @param table the target PDT
 * @param pid the pipe ID
 * @return the pipe name, NULL on error cases
 **/
const char* runtime_pdt_get_name(const runtime_pdt_t* table, runtime_api_pipe_id_t pid);

/**
 * @brief get the type expression for this PID
 * @param table the target PDT
 * @param pid the pipe ID
 * @return the type expression
 **/
const char* runtime_pdt_type_expr(const runtime_pdt_t* table, runtime_api_pipe_id_t pid);

/**
 * @brief set the type inference callback for the given pipe
 * @param table the target PDT
 * @param pid the pipe ID
 * @param callback the callback function
 * @param data The additional data passed to the hook callback
 * @return status code
 **/
int runtime_pdt_set_type_hook(runtime_pdt_t* table, runtime_api_pipe_id_t pid, runtime_api_pipe_type_callback_t callback, void* data);

/**
 * @brief get the type infernece callback fnction for the given pipe
 * @param table the target PDT
 * @param pid the pipe ID
 * @param result_func the result function buffer
 * @param result_data the result data buffer
 * @return status code
 **/
int runtime_pdt_get_type_hook(const runtime_pdt_t* table, runtime_api_pipe_id_t pid, runtime_api_pipe_type_callback_t* result_func, void** result_data);

#endif /* __PLUMBER_RUNTIME_PDT_H__ */
