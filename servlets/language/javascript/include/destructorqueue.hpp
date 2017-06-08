/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief The callback function queue
 * @details Currently, V8 may crash if we call a javascript function during GC. 
 *          So we need a queue put all the destructor in the queue and run it later
 * @file javascript/include/destructorqueue.hpp
 **/

#ifndef __DESCTRUCTORQUEUE_HPP__
#define __DESCTRUCTORQUEUE_HPP__
namespace Servlet {
	/**
	 * @brief The callback function qeueue
	 * @param InvokeFunc The functor for how we invoke the callback
	 **/
	class DestructorQueue {
		/**
		 * @brief Represent one element in the queue
		 **/
		struct _node_t {
			v8::Persistent<v8::Function>* callback;  /*!< The callback function we need to call */
			_node_t* next;    /*!< The next pointer for the linked list */
			/**
			 * @brief Create a new node
			 * @param _cb The callback function object
			 **/
			_node_t(v8::Persistent<v8::Function>* _cb) : callback(_cb), next(NULL) {}
			/**
			 * @brief Dipsose the node, at this time the destructor will be triggered
			 **/
			~_node_t();
		};

		_node_t* _queue;    /*!< The actual queue */

		public:

		/**
		 * @brief Create a new destructor queue
		 **/
		DestructorQueue();

		/**
		 * @brief dispose a used queue 
		 **/
		~DestructorQueue();

		/**
		 * @brief Add a new destructor function to the list
		 * @param desc The desctructor
		 * @return status code
		 **/
		int add(v8::Persistent<v8::Function>* desc); 

		/**
		 * @brief Flush the queue, execute and dispose all the registered callbacks 
		 * @return status code
		 **/
		int flush();
	};
}

#endif /* __DESCTRUCTORQUEUE_HPP__ */
