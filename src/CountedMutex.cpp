#include "CountedMutex.h"

CountedMutex::CountedMutex()
	: m_blockedLockCalls(0)
	, m_totalLockCalls(0) {

}
#include <iostream>
void CountedMutex::lock() {
	if (!m_mtx.try_lock()) {
		m_blockedLockCalls += 1;
		m_mtx.lock();
	}

	m_totalLockCalls += 1;
}

void CountedMutex::unlock() {
	m_mtx.unlock();
}

bool CountedMutex::try_lock() {
	return m_mtx.try_lock();
}

size_t CountedMutex::blockedCalls() const {
	return m_blockedLockCalls;
}

size_t CountedMutex::totalCalls() const {
	return m_totalLockCalls;
}

float CountedMutex::contention() const {
	const size_t totalCalls = m_totalLockCalls.load();

	if (totalCalls == 0) {
		return 0.f;
	}

	return 100.f * (m_blockedLockCalls / static_cast<float>(totalCalls));
}

void CountedMutex::clear() {
	m_blockedLockCalls = 0;
	m_totalLockCalls = 0;
}
