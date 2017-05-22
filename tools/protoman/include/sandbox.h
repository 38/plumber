/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief the sandbox utilities, a sandbox is set of pending operations,
 *        within the sandbox the system will be able to check if the operation
 *        causes problems
 * @file  protoman/include/sandbox.h
 **/
/**
 * @brief the database sandbox
 * @note the servlet should not use the modification utilities to change the database, so if you want to use
 *       this function, you must define the marco PROTO_DB_SANDBOX_MODIFICATION.
 * @details the sandbox is the temporary buffer which the database management system should
 *         use when modifying the database. Before the sandbox content commit to the database
 *         no actual opeartion will be performed.
 *         Use proto_db_sandbox_validate to validate the unlerlying peding operations in a sandbox
 *         will result a database in valid state. (Means no undefined references, no field reference error, etc.)
 **/

#ifndef __SANDBOX_H__

/**
 * @brief the type of a pending protocol type database operation
 **/
typedef enum {
	SANDBOX_CREATE,    /*!< Create a new type to the database */
	SANDBOX_DELETE,    /*!< Delete a existing type from the database */
	SANDBOX_UPDATE,    /*!< Update existing type in the database */
	SANDBOX_NOMORE     /*!< No more operations */
} sandbox_opcode_t;

/**
 * @brief represent a pending operation
 **/
typedef struct {
	sandbox_opcode_t opcode;  /*!< the type of the operation */
	const char*      target;  /*!< target the target typename */
} sandbox_op_t;

/**
 * @brief the type for a sandbox
 **/
typedef struct _sandbox_t sandbox_t;

/**
 * @brief the insersion flags, indicates how we handle the conflict when we inserting elements
 **/
typedef enum {
	SANDBOX_INSERT_ONLY,    /*!< indicates the only operation allowd is insert, which means if there's
	                                  *   already a type named that, the operation will fail to commit */
	SANDBOX_ALLOW_UPDATE,   /*!< indicates we are allowed to update the type if there's another type in
	                         *   the database have the same name. But if the new type will introduce
	                         *   some type errors in other types (For example, other type references the field
	                         *   that is not exist in the new type), the operation will fail to commit */
	SANDBOX_FORCE_UPDATE    /*!< indicates we are in the force update mode, which means if the updated type introduce
	                         *   errors in another types, we should remove the affected types and do the update anyway */
} sandbox_insert_flags_t;

/**
 * @brief create a new sandbox
 * @param flags the insertion flags, see the enum documenation for details
 * @return the newly created sandbox
 **/
sandbox_t* sandbox_new(sandbox_insert_flags_t flags);

/**
 * @brief dispose a used sandbox
 * @param sandbox the sandbox to dispose
 * @return status code
 **/
int sandbox_free(sandbox_t* sandbox);

/**
 * @brief insert a new protocol type description to the sandbox
 * @note owership of the protocol type passed in will be transferred to the sandnbox, so do not dispose the protocol type description object
 *       once the insertion is successfully completed.
 * @param sandbox the sandbox
 * @param type the protocol type description object
 * @param type_name the name of the type
 * @return status code
 **/
int sandbox_insert_type(sandbox_t* sandbox, const char* type_name, proto_type_t* type);

/**
 * @brief delete a existing protocol type and all the types it depends
 * @param sandbox the sandbox
 * @param type_name the type name to delete
 * @return status code
 **/
int sandbox_delete_type(sandbox_t* sandbox, const char* type_name);

/**
 * @brief perform dry run, which means we simulate what change to commit
 * @param sandbox the sandbox
 * @param buf the buffer for all the pending operations
 * @param size the buffer size
 * @return status code
 **/
int sandbox_dry_run(sandbox_t* sandbox, sandbox_op_t* buf, size_t size);

/**
 * @brief actually commit the content of the sandbox to the database
 * @param sandbox the sandbox
 * @return status code
 **/
int sandbox_commit(sandbox_t* sandbox);

#endif /* __SANDBOX_H__ */
