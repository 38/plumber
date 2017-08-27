/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @note I have thought about if the async task queue should be lock-free and do we really care about the
 *       lock overhead of posting a async task to the queue. The answer seems not. <br/>
 *       For the enttire processing procedure, it's hard to believe that most of the time the worker thread 
 *       is initializing the async task - Most of the operation should be done by the worker thread, unless
 *       the slow operations. <br/>
 *       Although the global mutex will serialize all the worker threads if every worker is starting a async
 *       task, but this situation is almost impossible. This is because the assumption that the time for each
 *       request being processed, the async initiazation time is tiny. It's not likely that multiple worker thread 
 *       trying to start the async task at the same time.
 *       Which means the benefit we gain from lock-free desgin seems really small. <br/>
 *       On the other hand, even if this serialization happens, the time we post a message to the queue should be much
 *       faster than the actual IO or other slow operation. <br/>
 *       In addtion, without the lock-free desgin, the structure of the async task queue should be much simplier and 
 *       should have less chance to make mistake. <br/>
 *       So we need to use the mutex base, multiple writer, multiple reader, blocking queue desgin. Which means we have
 *       two condition variable, one used to block the writers, another used to block readers. 
 **/
#include <runtime/api.h>
#include <sched/async.h>

int sched_async_init()
{
	return 0;
}

int sched_async_finalize()
{
	return 0;
}
