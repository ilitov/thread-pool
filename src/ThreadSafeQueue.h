#ifndef _THREAD_SAFE_QUEUE_HEADER_
#define _THREAD_SAFE_QUEUE_HEADER_

#include <mutex>				// Mutex locks for the head and the tail.
#include <memory>				// Smart pointers.
#include <limits>				// The maximum value of the capacity.
#include <atomic>				// Number of elements in the queue.
#include <utility>				// std::forward<>
#include <stdexcept>			// Exceptions.
#include <type_traits>			// Basic type traits.
#include "CountedMutex.h"		// Mutex statistics.

template <typename T>
class ThreadSafeQueue {
	struct Node {
		std::unique_ptr<T> m_data;
		Node *m_next;

		Node(std::unique_ptr<T> &&data = std::unique_ptr<T>(), Node *next = nullptr);
	};

public:
	static const size_t MAX_CAPACITY = std::numeric_limits<size_t>::max();
	using mtx_t = CountedMutex;

public:
	explicit ThreadSafeQueue(size_t capacity = MAX_CAPACITY);
	ThreadSafeQueue(const ThreadSafeQueue &r) = delete;
	ThreadSafeQueue& operator=(const ThreadSafeQueue &rhs) = delete;
	ThreadSafeQueue(ThreadSafeQueue &&r);
	ThreadSafeQueue& operator=(ThreadSafeQueue &&rhs) = delete;
	~ThreadSafeQueue();

public:
	bool tryPush(const T &value);
	bool tryPush(T &&value);

	void waitPush(const T &value);
	void waitPush(T &&value);

	std::unique_ptr<T> tryPop();
	bool tryPop(T &value);

	bool empty() const;
	size_t size() const;
	size_t maxCapacity() const;

	float headContention() const;
	float tailContention() const;

	void clearMutexes();

private:
	template <typename U>
	bool pushImpl(U &&value, bool wait);

	Node* tryPopHead();

	const Node* getTail() const;
	static void freeMemory(Node *node);

private:
	Node *m_head;
	Node *m_tail;

	std::atomic<size_t> m_size;
	std::atomic<size_t> m_maxCapacity;

	mutable mtx_t m_headMtx;
	mutable mtx_t m_tailMtx;

	std::condition_variable_any m_pushCV;
};

template <typename T>
inline ThreadSafeQueue<T>::Node::Node(std::unique_ptr<T> &&data, Node *next)
	: m_data(std::move(data))
	, m_next(next) {

}

template <typename T>
inline ThreadSafeQueue<T>::ThreadSafeQueue(size_t capacity)
	: m_head(new Node)
	, m_tail(m_head)
	, m_size(0)
	, m_maxCapacity(capacity) {

	// We use a dummy node in order to access only m_head(in pop())
	// or m_tail(in push()) and never both of them.

	if (capacity == 0) {
		throw std::logic_error("Queue capacity must be greater than 0.");
	}
}

template <typename T>
inline ThreadSafeQueue<T>::ThreadSafeQueue(ThreadSafeQueue<T> &&r) {
	std::lock_guard<mtx_t> rHeadLock(r.m_headMtx);
	std::lock_guard<mtx_t> rTailLock(r.m_tailMtx);

	m_head = r.m_head;
	m_tail = r.m_tail;

	m_size.store(r.m_size.load());
	m_maxCapacity.store(r.m_maxCapacity.load());

	r.m_head = nullptr;
	r.m_tail = nullptr;

	r.m_size.store(0);
	r.m_maxCapacity.store(0);
}

template <typename T>
inline ThreadSafeQueue<T>::~ThreadSafeQueue() {
	std::lock_guard<mtx_t> headLock(m_headMtx);
	std::lock_guard<mtx_t> tailLock(m_tailMtx);

	// Delete the list of nodes.
	freeMemory(m_head);

	// Clear everything.
	m_head = nullptr;
	m_tail = nullptr;

	m_size.store(0);
}

template <typename T>
inline bool ThreadSafeQueue<T>::tryPush(const T &value) {
	return pushImpl(value, false);
}

template <typename T>
inline bool ThreadSafeQueue<T>::tryPush(T &&value) {
	return pushImpl(std::move(value), false);
}

template <typename T>
inline void ThreadSafeQueue<T>::waitPush(const T &value) {
	pushImpl(value, true);
}

template <typename T>
inline void ThreadSafeQueue<T>::waitPush(T &&value) {
	pushImpl(std::move(value), true);
}

template <typename T>
inline std::unique_ptr<T> ThreadSafeQueue<T>::tryPop() {
	Node *oldHead = tryPopHead();

	// If old_head == nullptr, then the queue is empty.
	if (!oldHead) {
		return std::unique_ptr<T>();
	}

	// Steal the data from the old head.
	std::unique_ptr<T> result(std::move(oldHead->m_data));

	// Free the old head.
	delete oldHead;

	return result;
}

template <typename T>
inline bool ThreadSafeQueue<T>::tryPop(T &value) {
	// Get the stored value.
	std::unique_ptr<T> headValue = tryPop();

	if (!headValue) {
		return false;
	}

	// Update the given value if the pointer is valid.
	value = std::move(*headValue);

	return true;
}

template <typename T>
inline bool ThreadSafeQueue<T>::empty() const {
	std::lock_guard<mtx_t> headLock(m_headMtx);
	return m_head == getTail();
}

template <typename T>
inline size_t ThreadSafeQueue<T>::size() const {
	return m_size.load();
}

template <typename T>
inline size_t ThreadSafeQueue<T>::maxCapacity() const {
	return m_maxCapacity.load();
}

template <typename T>
inline float ThreadSafeQueue<T>::headContention() const {
	return m_headMtx.contention();
}

template <typename T>
inline float ThreadSafeQueue<T>::tailContention() const {
	return m_tailMtx.contention();
}

template <typename T>
inline void ThreadSafeQueue<T>::clearMutexes() {
	m_headMtx.clear();
	m_tailMtx.clear();
}

template <typename T>
template <typename U>
inline bool ThreadSafeQueue<T>::pushImpl(U &&value, bool wait) {
	// Create a new dummy node.
	std::unique_ptr<Node> newNode(new Node);

	// Prepare the data for the new node(the current dummy node).
	std::unique_ptr<T> newData(new T(std::forward<U>(value)));

	{
		// Lock the mutex for the tail and update the tail.
		std::unique_lock<mtx_t> tailLock(m_tailMtx);

		// If we aren't going to wait for pop() operations and the queue is full.
		if (!wait && m_size >= m_maxCapacity) {
			return false;
		}

		// Wait until there is enough space.
		m_pushCV.wait(tailLock, [this]() { return m_size < m_maxCapacity; });

		// Update the data of the current dummy node.
		m_tail->m_data = std::move(newData);

		// Set the next pointer to the new dummy node.
		m_tail->m_next = newNode.release();

		// Update the tail.
		m_tail = m_tail->m_next;

		// Increase the number of elements in the queue.
		m_size += 1;

		return true;
	}

	return false;
}

template <typename T>
inline typename ThreadSafeQueue<T>::Node* ThreadSafeQueue<T>::tryPopHead() {
	std::unique_lock<mtx_t> headLock(m_headMtx);

	// If the queue is empty.
	if (m_head == getTail()) {
		return nullptr;
	}

	// Get a pointer to the current head.
	Node *oldHead = m_head;

	// Update the head to the next node in the queue.
	m_head = m_head->m_next;

	// Unlock the mutex, because the head pointer has been updated.
	headLock.unlock();

	{
		// m_size is atomic, but there is a condition variable in the push() method,
		// which whecks m_size and needs to be synchronized through a mutex lock.
		std::lock_guard<mtx_t> tailLock(m_tailMtx);

		// Decrease the number of elements in the queue.
		m_size -= 1;
	}

	// Notify any waiting threads that there could be enough space to push their element.
	m_pushCV.notify_one();

	return oldHead;
}

template <typename T>
inline const typename ThreadSafeQueue<T>::Node* ThreadSafeQueue<T>::getTail() const {
	std::lock_guard<mtx_t> lock(m_tailMtx);
	return m_tail;
}

template <typename T>
inline void ThreadSafeQueue<T>::freeMemory(Node *node) {
	while (node) {
		Node *toDelete = node;
		node = node->m_next;
		delete toDelete;
	}
}

#endif // !_THREAD_SAFE_QUEUE_HEADER_
