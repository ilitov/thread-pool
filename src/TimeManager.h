#ifndef _TIME_MANAGER_HEADER_
#define _TIME_MANAGER_HEADER_

#include <chrono>

class TimeManager {
public:
	using duration_t = std::chrono::duration<double>;
	using clock_t = std::chrono::steady_clock;
	using tpoint_t = std::chrono::time_point<clock_t>;

public:
	TimeManager();
	TimeManager(const TimeManager &r) = default;
	TimeManager& operator=(const TimeManager &rhs) = default;
	~TimeManager();

	void start();
	void stop();

	bool running() const;
	duration_t time() const;

private:
	bool m_running;
	duration_t m_time;
	tpoint_t m_timePoint;
};

class ScopedTimeManager {
public:
	ScopedTimeManager(TimeManager &timeManager);
	ScopedTimeManager(const ScopedTimeManager &r) = delete;
	ScopedTimeManager& operator=(const ScopedTimeManager &rhs) = delete;
	~ScopedTimeManager();

private:
	TimeManager &m_timeManager;
};

#endif // !_TIME_MANAGER_HEADER_
