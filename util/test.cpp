///#include "scheduler.h"
#include "bitset.h"

#include <iostream>
#include <unistd.h>
#include <assert.h>

int main() {
#if 0
	auto f = []() { std::clog << "f()" << std::endl; };
	auto f2 = []() { std::clog << "f2()" << std::endl; };
	auto f3 = []() { std::clog << "f3()" << std::endl; };
	auto c = [c]() { std::clog << "cont" << std::endl; };

	g_sched.addEvent(f, 10);
	g_sched.addEvent(c, 20);
	g_sched.addEvent(f2, 20);
	g_sched.addEvent(f3, 20);
	while (true);
#else
	bitset b(80);
	b.set(1);
	b.set(8);
	b.set(31);

	std::clog << b.count() << std::endl;
	assert(b.test(1));
	assert(b.test(8));
	assert(b.test(31));
	std::clog << b.roundedSize() << std::endl;
#endif
}

