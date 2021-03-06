#ifndef _LOG_H
#define _LOG_H

#include <stdlib.h>

#ifdef DEBUGMODE
#define DEBUG(...) log_print('D', __FUNCTION__, __VA_ARGS__ )
#define PERROR(...) log_print('E', __FUNCTION__, __VA_ARGS__ )
#define INFO(...) log_print('I', __FUNCTION__, __VA_ARGS__ )
#else
#define DEBUG(...)
#define PERROR(...) log_print('E', NULL, __VA_ARGS__ )
#define INFO(...) log_print('I', NULL, __VA_ARGS__ )
#endif

void log_print(char mode, const char* fn, const char *fmt, ...);
int log_init(const char *filename);
void log_close();

#endif // _LOG_H
