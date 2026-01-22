#ifndef UTILS_HPP
#define UTILS_HPP

#include <iostream>

static uint64_t gettimeus()
{
	timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec*1000000 + t.tv_nsec/1000;
}

#endif