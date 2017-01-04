#ifndef UTIL_H
#define UTIL_H

#include <stdbool.h>

extern int istrcmp(const char* a, const char* b);
extern bool is_path_sep(char c);
extern const char* ext_part( const char* path);

#endif

