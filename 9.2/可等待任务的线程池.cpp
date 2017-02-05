#include <thread>
#include <memory>
#include <future>
#include "thread_safe_queue.h"
#include <vector>
#include <numeric>
#include <list>

class function_wrapper
{
	struct impl_base {
		virtual void call() = 0;
		virtual ~impl_base(){}
	};
	std::unique_ptr<impl_base> impl;
	template<typename F>
	struct impl_type :impl_base
	{
		F f;
		impl_type(F&& f_):f(std::move(f_)){}
		void call() { f(); }
	};
public:
	template<typename F>
	function_wrapper(F&& f):impl(new impl_type<F>(std::move(f)))
	{}

	void operator()() { impl->call(); }

	function_wrapper() = default;

	function_wrapper(function_wrapper&& other):
		impl(std::move(other.impl))
	{}

	function_wrapper& operator=(function_wrapper&& other)
	{
		impl = std::move(other.impl);
		return *this;
	}

	function_wrapper(const function_wrapper&) = delete;
	//function_wrapper(function_wrapper&) = delete;
	function_wrapper& operator=(const function_wrapper&) = delete;
};

class thread_pool
{
	std::atomic_bool done;

	thread_safe_queue<function_wrapper> work_queue;//使用function_wrapper，而非使用std::function

	void worker_thread()
	{
		while (!done)
		{
			function_wrapper task;
			if (work_queue.try_pop(task))
				task();
			else
				std::this_thread::yield();

		}
	}

	void run_pending_task()
	{
		function_wrapper task;
		if (work_queue.try_pop(task))
			task();
		else
			std::this_thread::yield();
	}
public:
	template<typename FunctionType>
	std::function<typename std::result_of<FunctionType()>::type>	//1
		submit(FunctionType f)
	{
		typedef typename std::result_of<FunctionType()>::type result_type;//2

		std::packaged_task<result_type()> task(std::move(f));//3
		std::future<result_type> res(task.get_future());//4
		work_queue.push(std::move(task));//5
		return res;	//6
	}
};

template<typename Iterator,typename T>
T paralel_accumulate(Iterator first, Iterator last, T init)
{
	unsigned long const length = std::distance(first, last);
	if (!length)
		return init;
	unsigned long const block_size = 25;
	unsigned long const num_blocks = (length + block_size - 1) / block_size;//1

	std::vector<std::future<T>> futures(num_blocks - 1);
	thread_pool pool;

	Iterator block_start = first;
	for (unsigned long i = 0; i < (num_block - 1); ++i)
	{
		Iterator block_end = block_start;
		std::advance(block_end, block_size);
		futures[i] = pool.submit(accumulate_block<Iterator, T>());//2
		block_start = block_end;
	}
	T last_result = accumulate_block<Iterator, T>()(block_start, last);
	T result = init;
	for (unsigned long i = 0; i < (num_blocks - ); ++i)
		result += futures[i].get();
	result += last_result;
	return result;
}

template<typename Iterator, typename T>
struct accumulate_block
{
	T operator()(Iterator first, Iterator last)//1
	{
		return std::accumulate(first, last, T());//2
	}
};

template<typename T>
struct sorter	//1
{
	thread_pool pool;	//2
	std::list<T> do_sort(std::list<T>& chunk_data)
	{
		if (chunk_data.empty())
			return chunk_data;
		std::list<T> result;
		result.splice(result.begin(), chunk_data, chunk_data.begin());
		T const& partition_val = *result.begin();

		typename std::list<T>::iterator divide_point =
			std::partition(chunk_data.begin(), chunk_data.end(),
				[&](T const& val) {return val < partition_val};);

		std::list<T> new_lower_chunk;
		new_lower_chunk.splice(new_lower_chunk.end(),
			chunk_data, chunk_data.begin(), divide_point);

		std::future<std::list<T>> new_lower =	//3
			pool.submit(std::bind(&sorter::do_sort, this, std::move(new_lower_chunk)));

		std::list<T> new_higher(do_sort(chunk_data));

		result.splice(result.end(), new_higher);
		while (!new_lower.wait_for(std::chrono::seconds(0)) ==
			std::future_status::timeout)
			pool.run_pending_task();	//4

		result.splice(result.begin(), new_lower.get());
		return result;
	}
};

template<typename T>
std::list<T> parallel_quick_sort(std::list<T> input)
{
	if (input.empty())
		return input;
	sorter<T> s;
	return s.do_sort(input);
}