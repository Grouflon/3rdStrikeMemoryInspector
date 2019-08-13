#pragma once


#include <cstdio>
#include <cstring>

#ifndef __FILENAME__
#define __FILENAME__ \
	(strrchr(__FILE__,'/') \
	? strrchr(__FILE__,'/')+1 \
	: (strrchr(__FILE__,'\\') \
	? strrchr(__FILE__,'\\')+1 \
	: __FILE__ \
	))
#endif


#define LOG(...)			{ fprintf(stdout, "[%s:%d] ", __FILENAME__, __LINE__); fprintf(stdout, __VA_ARGS__); fprintf(stdout, "\n"); }
#define LOG_WARNING(...)	{ fprintf(stdout, "[WARNING:%s:%d] ", __FILENAME__, __LINE__); fprintf(stdout, __VA_ARGS__); fprintf(stdout, "\n"); }
#define LOG_ERROR(...)		{ fprintf(stderr, "[ERROR:%s:%d] ", __FILENAME__, __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); }