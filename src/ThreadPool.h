#ifndef _THREAD_POOL_HEADER_
#define _THREAD_POOL_HEADER_

#include "ThreadSafeQueue.h"	// Work queues.
#include "FunctionWrapper.h"	// Task wrapper.
#include "Worker.h"				// Thread wrapper.
#include "CountedMutex.h"		// Mutex wrapper.

#include <vector>
#include <atomic>
#include <thread>
#include <future>
#include <iostream>
#include <condition_variable>

struct DataParallelOptions {
	static const size_t DEFAULT_BLOCK_SIZE = 4096u;

	enum IterationType {
		BLOCK_ITER,
		STRIPE_ITER,
		DEFAULT_ITER = BLOCK_ITER
	};

	DataParallelOptions(size_t blockSize = DEFAULT_BLOCK_SIZE, IterationType iterType = IterationType::DEFAULT_ITER);

	size_t m_blockSize;
	IterationType m_iterType;
};

class ThreadPool {
public:
	using task_t = FunctionWrapper;
	using worker_t = Worker;
	using mutex_t = CountedMutex;

public:
	explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency());
	ThreadPool(size_t numThreads, size_t globalQueueMaxSize, size_t localQueuesMaxSize);
	ThreadPool(const ThreadPool &r) = delete;
	ThreadPool& operator=(const ThreadPool &rhs) = delete;
	~ThreadPool();

private:
	void joinThreads();
	void threadLoop(int initialId);
	bool getLocalTask(task_t &task);
	bool stealLocalTask(task_t &task);
	bool stealGlobalTask(task_t &task);

public:
	bool executeTask();

	size_t numThreads() const;
	size_t pendingTasks() const;

	bool running() const;
	bool statistics(std::ostream &ostream = std::cout) const;

	bool startThreads();
	void waitStop(bool waitTasks = false);

	template <typename Func, typename ...Args>
	std::future<typename std::result_of<Func(Args...)>::type> submitTask(Func &&f, Args &&...args) {
		return submitTaskImpl(false, std::forward<Func>(f), std::forward<Args>(args)...);
	}

	template <typename Func, typename ...Args>
	std::future<typename std::result_of<Func(Args...)>::type> submitTaskWait(Func &&f, Args &&...args) {
		return submitTaskImpl(true, std::forward<Func>(f), std::forward<Args>(args)...);
	}

	template <typename Iterator, typename ElementFunction>
	void forEach(Iterator begin, Iterator end, const ElementFunction &func, const DataParallelOptions &options = DataParallelOptions()) {
		static_assert(std::is_same<void, typename std::result_of<ElementFunction(decltype(*begin))>::type>::value, "parallelFor accepts functions with return type 'void'");

		// Calculate the length of the sequence.
		const size_t length = std::distance(begin, end);
		if (length == 0) {
			return;
		}

		// Calculate the number of different blocks/tasks.
		// Note: (length >= 1) -> (numBlocks >= 1)
		const size_t numTasks = (length + options.m_blockSize - 1) / options.m_blockSize;

		// A vector of futures which we get for each submitted task.
		std::vector<std::future<void>> tasksCompletion(numTasks - 1);

		if (options.m_iterType == DataParallelOptions::BLOCK_ITER) {
			forEachBlock(begin, end, numTasks, options.m_blockSize, func, tasksCompletion);
		}
		else {
			forEachStripe(begin, numTasks, length, func, tasksCompletion);
		}

		// Wait for all tasks to complete.
		for (size_t i = 0; i < numTasks - 1; ++i) {
			tasksCompletion[i].get();
		}
	}

	template <typename Iterator, typename DataType, typename Operator>
	DataType fold(Iterator begin, Iterator end, DataType initValue, const Operator &oper, const DataParallelOptions &options = DataParallelOptions()) {
		auto foldFunction = [&oper, &initValue](Iterator from, Iterator to) {
			DataType result = initValue;

			for (Iterator iter = from; iter != to; ++iter) {
				result = oper(result, *iter);
			}

			return result;
		};

		DataType result = initValue;

		// Calculate the length of the sequence.
		const size_t length = std::distance(begin, end);
		if (length == 0) {
			return result;
		}

		// Calculate the number of different blocks.
		const size_t numBlocks = (length + options.m_blockSize - 1) / options.m_blockSize;

		// A vector of futures which we get for each submitted task.
		std::vector<std::future<DataType>> results(numBlocks - 1);
		
		Iterator from = begin;
		for (size_t i = 0; i < numBlocks - 1; ++i) {
			Iterator to = from;
			std::advance(to, options.m_blockSize);

			// Send a new task to the pool.
			results[i] = submitTaskWait(foldFunction, from, to);

			from = to;
		}

		// Calculate the last block from the current thread.
		result = oper(result, foldFunction(from, end));

		for (size_t i = 0; i < numBlocks - 1; ++i) {
			result = oper(result, results[i].get());
		}

		return result;
	}

private:	
	template <typename Func, typename ...Args>
	std::future<typename std::result_of<Func(Args...)>::type> submitTaskImpl(bool wait, Func &&f, Args &&...args) {
		using result_t = typename std::result_of<Func(Args...)>::type;

		std::packaged_task<result_t()> newTask(std::bind(std::forward<Func>(f), std::forward<Args>(args)...));
		std::future<result_t> resultFuture = newTask.get_future();

		// The current thread is from the pool. True value implies that m_threadId is >= 0.
		const bool poolThread = (m_threadPool == this);

		if (wait) {
			waitPush(poolThread, FunctionWrapper(std::move(newTask)));
		}
		else if (!tryPush(poolThread, FunctionWrapper(std::move(newTask)))) {
			// Return an empty future if the push has failed.
			return std::future<result_t>();
		}

		{
			// Synchronize m_pendingTasks increase with the sleeping(on m_workMtx) threads.
			std::lock_guard<mutex_t> lock(m_workMtx);

			// Increase the number of pending tasks.
			m_pendingTasks += 1;
		}

		// Notify any sleeping threads, which are waiting for more work.
		m_workCV.notify_one();

		return resultFuture;
	}

	bool tryPush(bool poolThread, ThreadPool::task_t &&newTask) {
		// (1) If the current thread is from the pool and its local queue is full, try to push the task
		// in the global queue.
		// (2) If the current thread isn't from the pool, try to push to the global work queue of the pool.

		if (poolThread && m_localQueues[m_threadId].size() < m_localQueues[m_threadId].maxCapacity()) {
			return m_localQueues[m_threadId].tryPush(std::move(newTask));
		}
		
		return m_workQueue.tryPush(std::move(newTask));
	}

	void waitPush(bool poolThread, ThreadPool::task_t &&newTask) {
		ThreadSafeQueue<ThreadPool::task_t> &queue = poolThread ? m_localQueues[m_threadId] : m_workQueue;

		// Block until there is enough space in the queue.
		queue.waitPush(std::move(newTask));
	}

	template <typename Iterator, typename Func>
	void forEachBlock(Iterator begin, Iterator end, size_t numTasks, size_t blockSize, const Func &func, std::vector<std::future<void>> &tasksCompletion) {
		auto blockTask = [&func](Iterator from, Iterator to) {
			for (Iterator iter = from; iter != to; ++iter) {
				func(*iter);
			}
		};
		
		// 'from' and 'to' form the interval for every task.
		Iterator from = begin;

		for (size_t i = 0; i < numTasks - 1; ++i) {
			Iterator to = from;
			std::advance(to, blockSize);

			// Send a new task to the pool.
			tasksCompletion[i] = submitTaskWait(blockTask, from, to);

			from = to;
		}

		// Execute tha last part on the current thread.
		blockTask(from, end);
	}

	template <typename Iterator, typename Func>
	void forEachStripe(Iterator begin, size_t numTasks, size_t length, const Func &func, std::vector<std::future<void>> &tasksCompletion) {
		auto stripeTask = [&func, numTasks, length](Iterator from, size_t currentIndex) {
			const size_t step = numTasks;

			Iterator iter = from;
			for (size_t i = currentIndex; i < length; i += step) {
				func(*iter);

				if (i + step < length) {
					std::advance(iter, step);
				}
			}
		};

		Iterator from = begin;

		for (size_t i = 0; i < numTasks - 1; ++i) {
			// Send a new task to the pool.
			tasksCompletion[i] = submitTaskWait(stripeTask, from, i);

			from = from + 1;
		}

		// Execute tha last part on the current thread.
		stripeTask(from, numTasks - 1);
	}

private:
	static thread_local int m_threadId;
	static thread_local const ThreadPool* m_threadPool;

	std::atomic<bool> m_active;
	std::atomic<bool> m_running;
	std::atomic<size_t> m_pendingTasks;

	mutable mutex_t m_workMtx;
	mutable mutex_t m_poolMtx;

	std::condition_variable_any m_workCV;

	ThreadSafeQueue<task_t> m_workQueue;
	std::vector<ThreadSafeQueue<task_t>> m_localQueues;
	std::vector<worker_t> m_threads;
};

#endif // !_THREAD_POOL_HEADER_
