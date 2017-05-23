/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The binary data wrapper for Javascript code
 * @file javascript/include/blobdata.hpp
 **/

#ifndef __JAVASCRIPT_BLOBDATA_HPP__
#define __JAVASCRIPT_BLOBDATA_HPP__

namespace Servlet {
	/**
	 * @brief a binary blob
	 **/
	class Blob {
		size_t _capacity;    /*!< the capacity of the struct */
		size_t _size;        /*!< the actual size */
		char*  _data;        /*!< the data pointer */
		public:
		/**
		 * @brief make a new blob data
		 **/
		Blob();
		/**
		 * @brief dispose a used blob data
		 **/
		~Blob();

		/**
		 * @brief initialize the blob buffer with given capacity
		 * @param capacity the capacity in bytes
		 * @return status code
		 **/
		int init(size_t capacity);

		/**
		 * @brief get the n-th bytes from the blob data
		 * @param idx the subscript
		 * @return the data
		 **/
		char& operator [](size_t idx);

		/**
		 * @biref get the string representation of the blob data
		 * @return the string
		 **/
		operator const char*() const;

		/**
		 * @biref get the size of the buffer
		 * @return the size or error code
		 **/
		size_t size() const;

		/**
		 * @brief append new data to the blob
		 * @param data the pointer to data
		 * @param count the size of the data
		 * @return status code
		 **/
		int append(const char* data, size_t count);

		/**
		 * @brief append new data to the blob(assume data is already copied to buffer)
		 * @param count the size of the data
		 * @return status code
		 **/
		int append(size_t count);

		/**
		 * @brief ensure the buffer has enough space
		 * @param count the number of additional bytes is needed
		 * @return status code
		 **/
		int ensure_space(size_t count);

		/**
		 * @brief get the maximum number of bytes can be put into the buffer which won't trigger the buffer resize
		 * @return the size
		 **/
		size_t space_available_without_resize();
	};
}

#endif /* __JAVASCRIPT_BLOBDATA_HPP__ */
