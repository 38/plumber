/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The JSON schema library
 * @file jsonschema/include/jsonschema.h
 * @detail The simple JSON schema library. This library uses JSON to describe the JSON schema
 *         The syntax is very easy, for the object, instead of putting the value, we actaully puts
 *         a list of type name to the value. For example. 
 *         <code>
 *         	{
 *         		"x": "number|null",
 *         		"y": "number",
 *         		"message": "string",
 *         		"array":["number*"]   
 *         </code>
 *         So for the list case, ["number*"] means we want to a list of number but don't have the contrain on the
 *         number of element. However, if we use ["number", "number", "number"] it means it would only accept the triple
 *         with 3 numbers.
 **/
#ifndef __JSONSCHEMA_H__
#define __JSONSCHEMA_H__

/**
 * @brief The previous decleartion of the JSON schema object
 **/
typedef struct _jsonschema_t jsonschema_t;

/**
 * @brief Create a new schema from a given json string 
 * @param schema_obj The schema object
 * @return The newly created schema object
 **/
jsonschema_t* jsonschema_new(json_object* schema_obj);

/**
 * @brief Load a JSON schema from a string
 * @param scehma_str The schema string
 * @return the newly created schema or NULL on error case
 **/
jsonschema_t* jsonschema_from_string(const char* schema_str);

/**
 * @brief Load a JSON schema from the schema file
 * @param schema_file the filename of the schema file to load
 * @return The newly created schema object from the file
 **/
jsonschema_t* jsonschema_from_file(const char* schema_file);

/**
 * @brief Dispose a used JSON schema
 * @param schema The json schema to dispose
 * @return status code
 **/
int jsonschema_free(jsonschema_t* schema);

/**
 * @brief Validate if a JSON object is a valid form of the given schema
 * @param schema The schema to validate
 * @param object The object to validate
 * @return The validation result or error code
 **/
int jsonschema_validate(const jsonschema_t* schema, json_object* object);

/**
 * @brief Modify the target object based on the schema, which means only
 *        the schema defined fields will gets updated. This doesn't requires
 *        the patch object is a valid instance of the given schema. But all
 *        the structures that is not belongs to the schema will gets ignored.
 *        However, if the patch contains a key which will break the schema constrain in
 *        target object after the change, the update will failed
 * @param schema The schema definition
 * @param target The target object
 * @param patch The patch object
 * @return the patch result
 **/
int jsonchema_update(const jsonschema_t* schema, json_object* target, const json_object* patch);

#endif /* __JSONSCHEMA_H__ */
