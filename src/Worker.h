#ifndef _WORKER_HEADER_
#define _WORKER_HEADER_

#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>

#include "TimeManager.h"

class Worker {
public:
	using thread_t = std::thread;

public:
	Worker();
	Worker(std::thread &&thread);
	Worker(const Worker &r) = delete;
	Worker& operator=(const Worker &rhs) = delete;
	Worker(Worker &&r) = delete;
	Worker& operator=(Worker &&rhs);
	~Worker() = default;

	void join();
	bool joinable() const;

	size_t completedTasks() const;
	TimeManager::duration_t workTime() const;
	TimeManager::duration_t sleepTime() const;
	TimeManager::duration_t totalTime() const;

	template <typename Func>
	void operator()(Func &&func) {
		{
			ScopedTimeManager wtm(m_workTime);
			func();
		}

		++m_workDone;
	}

	template <typename Mutex, typename Predicate>
	void sleep(Mutex &workMtx, std::condition_variable_any &workCV, Predicate predicate) {
		ScopedTimeManager stm(m_sleepTime);

		std::unique_lock<Mutex> workLock(workMtx);
		workCV.wait(workLock, predicate);

		// The current thread holds the mutex, but will release it when it goes out of the scope.
	}

private:
	std::atomic<size_t> m_workDone;
	TimeManager m_workTime;
	TimeManager m_sleepTime;
	TimeManager m_totalTime;

	thread_t m_thread;
};

#endif // !_WORKER_HEADER_
