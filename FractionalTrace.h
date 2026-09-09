#pragma once

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

inline bool FractionalTraceEnabled()
{
	static int enabled = []() {
		const char* value = getenv("HAIWAY_FRACTIONAL_TRACE");
		return value != NULL && value[0] != '\0' && value[0] != '0';
	}();
	return enabled != 0;
}

inline void FractionalTrace(const char* format, ...)
{
	if (!FractionalTraceEnabled())
		return;
	fprintf(stderr, "[haiway-fractional] ");
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputc('\n', stderr);
	fflush(stderr);
}
