#include <cassert>
#include "Worker.h"

Worker::Worker()
	: m_workDone(0) {

}

Worker::Worker(std::thread &&thread)
	: m_workDone(0)
	, m_thread(std::move(thread)) {
	
	// Capture the time of construction.
	m_totalTime.start();
}

Worker& Worker::operator=(Worker &&rhs) {
	if (this != &rhs) {
		m_workDone.store(rhs.m_workDone.load());
		m_workTime = std::move(rhs.m_workTime);
		m_sleepTime = std::move(rhs.m_sleepTime);
		m_totalTime = std::move(rhs.m_totalTime);
		m_thread = std::move(rhs.m_thread);

		rhs.m_workDone.store(0);
	}

	return *this;
}

void Worker::join() {
	m_thread.join();
	m_totalTime.stop();

	// If the thread member was joined, then these two timers must be stopped.
	assert(m_workTime.running() == false && "Work time isn't stopped!");
	assert(m_sleepTime.running() == false && "Sleep time isn't stopped!");
}

bool Worker::joinable() const {
	return m_thread.joinable();
}

size_t Worker::completedTasks() const {
	return m_workDone.load();
}

TimeManager::duration_t Worker::workTime() const {
	assert(m_workTime.running() == false && "Work time isn't stopped!");
	
	return m_workTime.time();
}

TimeManager::duration_t Worker::sleepTime() const {
	assert(m_sleepTime.running() == false && "Sleep time isn't stopped!");
	
	return m_sleepTime.time();
}

TimeManager::duration_t Worker::totalTime() const {
	assert(m_totalTime.running() == false && "Total time isn't stopped!");
	
	return m_totalTime.time();
}
