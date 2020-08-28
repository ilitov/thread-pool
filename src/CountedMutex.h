#ifndef _COUNTED_MUTEX_HEADER_
#define _COUNTED_MUTEX_HEADER_

#include <mutex>
#include <atomic>

class CountedMutex {
public:
	CountedMutex();
	CountedMutex(const CountedMutex &r) = delete;
	CountedMutex& operator=(const CountedMutex &rhs) = delete;
	~CountedMutex() = default;

	void lock();
	void unlock();
	bool try_lock();

	size_t blockedCalls() const;
	size_t totalCalls() const;
	float contention() const;

	void clear();

private:
	std::atomic<size_t> m_blockedLockCalls;
	std::atomic<size_t> m_totalLockCalls;
	mutable std::mutex m_mtx;
};

#endif // !_COUNTED_MUTEX_HEADER_
