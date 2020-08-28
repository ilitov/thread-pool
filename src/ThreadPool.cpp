#include <iostream>
#include <stdexcept>
#include "ThreadPool.h"

DataParallelOptions::DataParallelOptions(size_t blockSize, IterationType iterType)
	: m_blockSize(blockSize)
	, m_iterType(iterType) {

	if (blockSize == 0) {
		throw std::logic_error("Invalid block size.");
	}
}

thread_local int ThreadPool::m_threadId = -1;
thread_local const ThreadPool* ThreadPool::m_threadPool = nullptr;

ThreadPool::ThreadPool(size_t numThreads)
	: ThreadPool(numThreads, ThreadSafeQueue<ThreadPool::task_t>::MAX_CAPACITY, ThreadSafeQueue<ThreadPool::task_t>::MAX_CAPACITY) {

}

ThreadPool::ThreadPool(size_t numThreads, size_t workQueueMaxSize, size_t localQueueMaxSize)
	: m_active(false)
	, m_running(false)
	, m_workQueue(workQueueMaxSize)
	, m_localQueues()
	, m_threads(numThreads) {

	if (numThreads == 0) {
		throw std::logic_error("The number of threads must be greater than 0.");
	}

	// Random seed, needed when we steal work.
	std::srand(42);

	// Create enough local queues with specified maximum size.
	m_localQueues.reserve(numThreads);

	for (size_t i = 0; i < numThreads; ++i) {
		m_localQueues.emplace_back(localQueueMaxSize);
	}

	// Create and start the threads.
	startThreads();
}

void ThreadPool::joinThreads() {
	// We expect that this function will be called while holding the pool lock.

	for (size_t i = 0, size = m_threads.size(); i < size; ++i) {
		if (m_threads[i].joinable()) {
			m_threads[i].join();
		}
	}
}

void ThreadPool::threadLoop(int initialId) {
	// Initialize the local static variables for each thread from the pool.

	// Every pool thread has a unique id and it is used for indexing.
	m_threadId = initialId;

	// Every thread has a single parent pool. This helps us decide whether a
	// given thread with a positive id is truly from the given pool.
	m_threadPool = this;

	// Wait until all of the threads are signaled to run.
	// This allows us to tell whether the pool is running or not. Otherwise,
	// some threads might be running, and some might not.
	{
		std::unique_lock<ThreadPool::mutex_t> waitLock(m_workMtx);
		m_workCV.wait(waitLock, [this]() { return m_running.load() || !m_active.load(); });
	}

	// Loop while the pool is active.
	while (m_active.load()) {
		executeTask();
	}
}

bool ThreadPool::getLocalTask(task_t &task) {
	//If the current thread is from the pool and its local queue is not empty, we return true.
	if (m_threadPool == this && m_localQueues[m_threadId].tryPop(task)) {
		return true;
	}

	return false;
}

bool ThreadPool::stealLocalTask(task_t &task) {
	// If the current thread is not from this pool.
	if (m_threadPool != this) {
		return false;
	}

	const size_t queuesNum = m_localQueues.size();
	const size_t idx = std::rand() % queuesNum;

	// Try to steal a task from any pool thread.
	size_t i = idx;
	do {
		// If we successfully popped a task.
		if (i != m_threadId && m_localQueues[i].tryPop(task)) {
			return true;
		}

		i = (i + 1) % queuesNum;
	} while (i != idx);

	return false;
}

bool ThreadPool::stealGlobalTask(task_t &task) {
	return m_workQueue.tryPop(task);
}

bool ThreadPool::executeTask() {
	const int id = m_threadId;
	const bool fromThisPool = (m_threadPool == this);

	task_t currentTask;

	if (getLocalTask(currentTask) || stealGlobalTask(currentTask) || stealLocalTask(currentTask)) {
		// Decrease the number of pending tasks.
		m_pendingTasks -= 1;

		if (!fromThisPool) {
			currentTask();
		}
		else {
			// Measure the time of execution of the current thread(in its operator()).
			m_threads[id](currentTask);
		}

		return true;
	}
	else if(fromThisPool) {
		// If there are no more tasks, then sleep.
		m_threads[id].sleep(m_workMtx, m_workCV, [this]() { return !m_active || m_pendingTasks > 0; });
	}

	return false;
}

size_t ThreadPool::numThreads() const {
	return m_threads.size();
}

size_t ThreadPool::pendingTasks() const {
	return m_pendingTasks.load();
}

bool ThreadPool::running() const {
	return m_running.load();
}

bool ThreadPool::statistics(std::ostream &ostream) const {
	// Ensure that the pool won't be modified while printing the statistics.
	std::lock_guard<ThreadPool::mutex_t> poolLock(m_poolMtx);
	
	// The pool must not be running. We do not want a blocking behavior.
	if (running()) {
		return false;
	}

	ostream << "Mutex 1(work) contention = " << m_workMtx.contention() << "%.\n";
	ostream << "Mutex 2(pool) contention = " << m_poolMtx.contention() << "%.\n";
	ostream << "Global work queue: head contention = " << m_workQueue.headContention() << "%.\n";
	ostream << "Global work queue: tail contention = " << m_workQueue.tailContention() << "%.\n";

	std::cout << "\n\t##### Threads information #####\n\n";

	for (size_t i = 0, size = m_threads.size(); i < size; ++i) {
		TimeManager::duration_t workTime = m_threads[i].workTime();
		TimeManager::duration_t sleepTime = m_threads[i].sleepTime();
		TimeManager::duration_t totalTime = m_threads[i].totalTime();

		ostream << "### Thread " << i << ":\n";
		ostream << "\t@ Work time: " << workTime.count() << "s.\n";
		ostream << "\t@ Sleep time: " << sleepTime.count() << "s.\n";
		ostream << "\t@ Other time(pool maintenance): " << (totalTime - workTime - sleepTime).count() << "s.\n";
		ostream << "\t@ Completed tasks: " << m_threads[i].completedTasks() << '\n';
		ostream << "\t@ Local work queue: head contention = " << m_localQueues[i].headContention() << "%.\n";
		ostream << "\t@ Local work queue: tail contention = " << m_localQueues[i].tailContention() << "%.\n";
		ostream << '\n';
	}

	return true;
}

bool ThreadPool::startThreads() {
	// Lock the whole pool.
	std::unique_lock<ThreadPool::mutex_t> poolLock(m_poolMtx);

	// Do nothing if the pool is running.
	if (running()) {
		return false;
	}

	// m_running is false here.

	// Clear the mutexes.
	m_workMtx.clear();
	m_poolMtx.clear();

	// Clear the global work queue.
	m_workQueue.clearMutexes();

	const size_t threadNumber = m_threads.size();

	for (size_t i = 0; i < threadNumber; ++i) {
		m_localQueues[i].clearMutexes();
	}

	// Indicate that the threads can run their loop.
	m_active.store(true);

	// Initialize each thread with its thread loop.
	try {
		for (size_t i = 0; i < threadNumber; ++i) {
			m_threads[i] = Worker(std::thread(&ThreadPool::threadLoop, this, static_cast<int>(i)));
		}
	}
	catch (...) {
		{
			// Threads sleep on m_workMtx.
			std::lock_guard<mutex_t> lock(m_workMtx);

			// The pool is no more active.
			m_active.store(false);
		}

		// Wake up all sleeping threads.
		m_workCV.notify_all();

		// Join all threads and re-throw.
		joinThreads();

		throw;
	}

	{
		// Threads sleep on m_workMtx.
		std::lock_guard<mutex_t> lock(m_workMtx);

		// Thre threads can run now.
		m_running.store(true);
	}

	// Unlock the mutex.
	poolLock.unlock();

	// Signal the threads to start running.
	m_workCV.notify_all();

	return true;
}

void ThreadPool::waitStop(bool waitTasks) {
	// Execute all waiting tasks.
	if (waitTasks) {

		// While there are working threads and more tasks to complete...
		while (running() && m_pendingTasks.load() > 0) {
			// Help with the execution...
			executeTask();
		}

	}

	// Lock the whole pool and join the threads.
	std::lock_guard<ThreadPool::mutex_t> poolLock(m_poolMtx);

	// Indicate that the threads must stop.
	m_active.store(false);

	// Wake up all waiting threads.
	m_workCV.notify_all();

	// Join all threads.
	joinThreads();

	// The pool isn't running now.
	m_running.store(false);
}

ThreadPool::~ThreadPool() {
	waitStop();
}
