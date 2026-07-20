#include "vr_log.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

void vr_log(const char* format, ...) {
		FILE* logfile = fopen("vr_debug.txt", "a");
		if (!logfile) {
				return;
		}

		// Add timestamp
		time_t now;
		time(&now);
		struct tm* timeinfo = localtime(&now);
		fprintf(logfile, "[%02d:%02d:%02d] ",
				timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

		// Write message
		va_list args;
		va_start(args, format);
		vfprintf(logfile, format, args);
		va_end(args);

		fprintf(logfile, "\n");
		fflush(logfile);
		fclose(logfile);
}
