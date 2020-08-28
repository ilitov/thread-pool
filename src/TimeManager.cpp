#include "TimeManager.h"

TimeManager::TimeManager()
	: m_running(false)
	, m_time(0) {

}

void TimeManager::start() {
	if (m_running) {
		return;
	}

	m_timePoint = clock_t::now();

	m_running = true;
}

void TimeManager::stop() {
	if (!m_running) {
		return;
	}

	tpoint_t currentPoint = clock_t::now();

	m_time += std::chrono::duration_cast<duration_t>(currentPoint - m_timePoint);

	m_running = false;
}

TimeManager::~TimeManager() {
	m_running = false;
	m_time = m_time.zero();
}

bool TimeManager::running() const {
	return m_running;
}

TimeManager::duration_t TimeManager::time() const {
	return m_time;
}

ScopedTimeManager::ScopedTimeManager(TimeManager &timeManager) 
	: m_timeManager(timeManager) {
	m_timeManager.start();
}

ScopedTimeManager::~ScopedTimeManager() {
	m_timeManager.stop();
}
