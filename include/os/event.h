/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The kernel event driven interface wrapper
 * @file os/event.h
 **/
#ifndef __OS_EVENT_H__

/**
 * @brief The type of the event 
 **/
typedef enum {
	OS_EVENT_TYPE_KERNEL,    /*!< The event comes from kernel, typically FD events */
	OS_EVENT_TYPE_USER       /*!< The event comes from user space code, this is the abstraction for Linux eventfd */
} os_event_type_t;

typedef enum {
	OS_EVENT_KERNEL_EVENT_IN,      /*!< The kernel event that indicates a FD is current readable */
	OS_EVENT_KERNEL_EVENT_OUT,     /*!< The kernel event that indicates a FD is current writeable */
	OS_EVENT_KERNEL_EVENT_CONNECT  /*!< The kernel event for establishing a scoket */
} os_event_kernel_type_t;

/**
 * @brief The data structure used to describe a kernel event 
 **/
typedef struct {
	int                     fd;    /*!< The target file descriptor */
	os_event_kernel_type_t  event; /*!< The event we care about */
	void*                   data;  /*!< The additional data we get from it */
} os_event_kernel_event_desc_t;

/**
 * @brief The data structure used to describe a user space event
 **/
typedef struct {
	void*                  data;  /*!< The additional data for it */
} os_event_user_event_desc_t;

/**
 * @brief The event descriptor 
 **/
typedef struct {
	os_event_type_t type;   /*!< The desired event type */
	union {
		os_event_kernel_event_desc_t kernel;  /*!< The kernel event type */
		os_event_user_event_desc_t   user;    /*!< The user defined event type */
	};
} os_event_desc_t;

/**
 * @brief The event poll object
 **/
typedef struct _os_event_poll_t os_event_poll_t;

/**
 * @brief Create a new event poll object 
 * @return the newly created OS poll event or NULL on error
 **/
os_event_poll_t* os_event_poll_new();

/**
 * @brief Dispose a used OS event poll object
 * @param poll The poll object to dispose
 * @return status code
 **/
int os_event_poll_free(os_event_poll_t* poll);

/**
 * @brief Add a new event from the event poll object
 * @param poll The target poll object
 * @param desc The event descriptor
 * @return The FD to listen, if the event is a kernel event, it will return what FD
 *         it gets from the desc. If it's a user event, it will return a FD which the program
 *         can trigger the event by writting to it. On error case, it should return error code
 **/
int os_event_poll_add(os_event_poll_t* poll, os_event_desc_t* desc);

/**
 * @brief remove the event from the poll object
 * @param poll the target poll
 * @param fd The FD returned from the add (For the kernel event, it should be the target event, for user event, it should be os_event_poll_add return value)
 * @param read indicates if this is a read FD, otherwise it's a write FD
 * @note The read param is not actually used by Linux epoll, but this hint is ncessary for the KQueue
 * @return status code
 **/
int os_event_poll_del(os_event_poll_t* poll, int fd, int read);

/**
 * @brief Wait for the event defined in the poll object
 * @param poll The poll object
 * @param data_buf The buffer used to return the data object that has been activated
 * @param max_events The maximum events we are accepting
 * @param timeout The time limit for waiting
 * @return The number of events has been returned, or error code
 * @note For the timeout case, it should return 0. Also, The API is edge-triggerred, which means
 *       a event can be poped up only once
 **/
int os_event_poll_wait(os_event_poll_t* poll, size_t max_events, int timeout);

/**
 * @brief Take the idx-th result from the last poll result
 * @note This function do not check if the idx out of bound, so beware of the buffer overflow
 * @param poll The poll to use
 * @param idx The idex
 * @return The event data pointer
 **/
void* os_event_poll_take_result(os_event_poll_t* poll, size_t idx);

/**
 * @brief Consume a user space event
 * @param fd The user event FD to consume
 * @parap poll The poll object owns this event
 * @return status code
 **/
int os_event_user_event_consume(os_event_poll_t* poll, int fd);

#endif /* __OS_EVENT_H__ */
