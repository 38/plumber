/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The exotic object data type
 **/
#ifndef __PSS_EXOTIC_H__
#define __PSS_EXOTIC_H__
/**
 * @brief The data structure used for an exotic object
 **/
typedef struct _pss_exotic_t pss_exotic_t;

/**
 * @brief The data used to create an exotic object
 **/
typedef struct {
	uint32_t  magic_num;  /*!< The magic number of the object */
	const char* type_name;/*!< The name of the type */
	/**
	 * @brief Dispose a use exotic object
	 * @param mem The memomry to dispose
	 * @return status code
	 **/
	int (*dispose)(void* mem);

	void* data; /*!< The actual data */
} pss_exotic_creation_param_t;

/**
 * @brief Initialize the callbacks for the exotic objects
 * @return status code
 **/
int pss_exotic_init(void);

/**
 * @brief FInalize the type callbacks
 * @return status code
 **/
int pss_exotic_finalize(void);

/**
 * @brief Get the actual data from the exotic object
 * @param obj The exotic object
 * @param magic_num The expected magic number
 * @return The pointer to the actual data
 **/
void* pss_exotic_get_data(pss_exotic_t* obj, uint32_t magic_num);

#endif
