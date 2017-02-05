#include "thread_safe_queue.h"
#include "function_wrapper.h"
#include <atomic>
#include <future>

class thread_pool
{
	std::atomic_bool done;//不完整的，为初始化，为使用
	
	thread_safe_queue<function_wrapper> pool_work_queue;

	typedef std::queue<function_wrapper> local_queue_type;	//1
	static thread_local std::unique_ptr<local_queue_type> local_work_queue;//2

	void worker_thread()
	{
		local_work_queue.reset(new local_queue_type);//3
		while (!done)
			run_pending_task();
	}
public:
	template<typename FunctionType>
	std::future<typename std::result_of<FunctionType()>::type> 
		submit(FunctionType f)
	{
		typedef typename std::result_of<FunctionType()>::type result_type;

		std::packaged_task<result_type()> task(f);
		std::future<result_type> res(task.get_future());
		if (local_work_queue)//4
			local_work_queue->push(std::move(task));
		else
			pool_work_queue.push(std::move(task));//5
		return res;
	}
	
	void run_pending_task()
	{
		function_wrapper task;
		if (local_work_queue && !local_work_queue->empty())//6
		{
			task = std::move(local_work_queue->front());
			local_work_queue->pop();
			task();
		}
		else if (pool_work_queue.try_pop(task))//7
			task();
		else
			std::this_thread::yield();
	}
	//rest as before
};