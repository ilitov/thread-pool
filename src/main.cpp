#include <iostream>
#include "ThreadPool.h"

int sumArray(ThreadPool &pool, const int *arr, int size) {
	return pool.fold(arr, arr + size, 0, [](int a, int b) {return a + b; });
}

void f() {
	std::cout << "Function f()" << std::endl;
}

int main() {	
	const int numThreads = 4;
	ThreadPool pool(numThreads);

	// Task 1.
	const int size = 100;
	int arr[size];

	for (int i = 0; i < size; ++i) {
		arr[i] = 1;
	}

	auto sum = pool.submitTask(sumArray, std::ref(pool), arr, size);

	std::cout << sum.get() << std::endl;

	// Task 2.
	pool.submitTask(f).wait();

	return 0;
}
