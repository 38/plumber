/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief the Module Addressing Table, the table we used to assign a module instance ID
 *        To each initialized module. One module binary can be initialized with different
 *        args to different module instances
 * @details There are two different ways for referring a module instance, module path and
 *         module instance id/module type code. <br/>
 *         The module path is used in the non-plumber-kernel code, such as service script,
 *         servlet code or servlet libraries. <br/>
 *         A sample module path: pipe.tcp.port_80, this point to the tcp module that listening
 *         to the 80 port. A module instance path have two parts, module binary name and the
 *         instance name, it should looks like &lt;module_binary_name&gt;.&lt;module_instance_name;&gt
 *         In the example of TCP module on 80 port, the module binary name is "pipe.tcp" and
 *         the module instance name is "port80" <br/>
 *         The other way to represent a module is use the module type id, which is used within
 *         the plumber kernel code internally. For example, instantiate a new event loop for
 *         a given module, at this time we do not need to use the human readable path for the
 *         module, instead a integer (actually itc_module_type_t) will be used to identify the
 *         module instance.
 * @file itc/modtab.h
 **/
#include <utils/mempool/objpool.h>
#ifndef __PLUMBER_ITC_MODTAB_H__
#define __PLUMBER_ITC_MODTAB_H__

/**
 * @brief the type used to represents a module instance
 * @details a module instance is a instance of a module binary which has been initialized by a module initialization string
 *         The same module binary could have multiple instances, for example, the TCP module binary is having a instance which
 *         listening to port 80 and another instance listening to port 8080. <br/>
 *         The limit of adding new module instance is *it can not use the same module path*.
 *         So if a module is designed to allow multiple instantiation, it should use the different module path for each time. <br/>
 *         The module like Memory Pipe, always try to register itself at "pipe.mem". So it can not instantiate twice. <br/>
 *         However, the TCP module register its instances at "pipe.tcp.port_&lt;port number&gt;", that is why the module have more than
 *         1 instances. <br/>
 *         The instances that instantiated from the same module binary, shares the module code, but use different context.
 **/
typedef struct {
	itc_module_type_t   module_id; /*!< the module id for this instance */
	const itc_module_t* module;    /*!< the module binary for this instance */
	void*               context;   /*!< the module context */
	const char*         path;      /*!< the path for the module */
	mempool_objpool_t*  handle_pool; /*!< the memory pool use to allocate the pipe handle for this module */
} itc_modtab_instance_t;

/**
 * @brief the iterator used to enumberate the directory entries
 * @details The module path can be used as a real directory, for example, you can look for all the TCP modules by
 *         looking for all the modules under "pipe.tcp". <br/>
 *         This is the iterator data structure that is used to enumerate all the module start with the same prefix <br/>
 **/
typedef struct {
	uint8_t begin;   /*!< the start point of the iterator */
	uint8_t end;     /*!< the end point of the iterator */
} itc_modtab_dir_iter_t;

/**
 * @brief initialize the module addressing table subsystem
 * @return status code
 **/
int itc_modtab_init();

/**
 * @brief finalize the module addressing table subsystem
 * @return status code
 **/
int itc_modtab_finalize();

/**
 * @brief The function that is used to set the size of the header of each pipe handle <br/>
 * @details The module runtime (itc/module.c) is where we manage all the pipe objects. And
 *         all the pipe instance has an header, which is used for the module runtime to keep
 *         some private data in it. <br/>
 *         That is why we need this function, this function will set how many bytes of data needs
 *         to be reserved for the module runtime internal use. <br/>
 *         The reason why we have this function is, because the module internal data is private, and
 *         we don't want to expose them to other files which may cause problems. <br/>
 * @param size the size of the header of pipe handle
 * @note this is used by the memory pool
 * @return nothing
 **/
void itc_modtab_set_handle_header_size(size_t size);

/**
 * @brief initialize a new module instance under a name alias, this is similar to exec functions of POSIX API,
 *        you can pass the module name and it's initialize param
 * @param module the module definition data
 * @param argc the number of module initialization arguments
 * @param argv the module initialization arguments
 * @return the status code
 **/
int itc_modtab_insmod(const itc_module_t* module, uint32_t argc, char const* const* argv);

/**
 * @brief get the module instance from the module path
 * @param path the path of the module
 * @return the result module instance, NULL on error
 **/
const itc_modtab_instance_t* itc_modtab_get_from_path(const char* path);

#if 0
/**
 * @brief link a module to another path, this is a planned feature
 * @details The module path link is similar to the symbolic link/name alias of a
 *         module instance. <br/>
 *         This is used when we want to setup the default modules, etc.
 * @param sour the source path of the module
 * @param dest the destination path of the module
 * @return status code
 **/
int itc_modtab_link(const char* sour, const char* dest);
#endif

/**
 * @brief open a dir to enumerate its entry, this means get all the module instances
 *        which has the shared prefix in module instance path <br/>
 *        This is used to enumerate all the modules that has the same property. <br/>
 *        For example all the communication modules has the shared prefix pipe.
 *        So we can use itc_modtab_open_dir("pipe.", &iter) to get an iterator that can
 *        enumerate all the module instances that is used for communication. <br/>
 *        Another example is use itc_modtab_open_dir("pipe.tcp.") for all the TCP modules.
 * @param path the path to open
 * @param iter the buffer used as the result buffer
 * @note the dir is not actually the concept of directory, but a list of
 *       pathes that have the common prefix
 * @return status code
 **/
int itc_modtab_open_dir(const char* path, itc_modtab_dir_iter_t* iter);

/**
 * @brief get the next entry under this directory
 * @param iter the iterator used for enumerate
 * @return the next instnace in the dir, NULL if there's no more items
 **/
const itc_modtab_instance_t* itc_modtab_dir_iter_next(itc_modtab_dir_iter_t* iter);

/**
 * @brief get a module instance from the module type ID(the lagacy name for the module instance id)
 * @param type the module type ID
 * @return the result instance or NULL when error
 **/
const itc_modtab_instance_t* itc_modtab_get_from_module_type(itc_module_type_t type);

/**
 * @brief get the module instance id (aka module instance type) from the module instance path
 * @param path the path to search
 * @return the result module tab or error code
 **/
itc_module_type_t itc_modtab_get_module_type_from_path(const char* path);

/**
 * @brief call all the on exit module call for all the module instance added to the modtab.
 * @details this is necessary because the module may have something to clean up before the actual cleanup code is called.
 *         For example, the PSSM allows the user code register on exit callbacks for some cleanup purposes. And this
 *         cleanup callback may depends on some part of the infrastrcture provided by the framework. <br/>
 *         So the actual safe way for us to run the callback is the time point that we do not start finalizing the
 *         framework, but we are going to do that. <br/>
 *         This is the function that should be called before the actual cleanup process begins, and this give the user space
 *         cleanup code have a chance to do the normal things.
 * @return status code
 **/
int itc_modtab_on_exit();

#endif /** __PLUMBER_ITC_MODTAB_H__ */
