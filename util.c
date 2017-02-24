#include "impy.h"
#include "private.h"

#include <string.h>
#include <ctype.h>


int istricmp(const char* a, const char* b)
{
    while(1) {
        if(*a < *b) {
            return -1;
        }
        if(*a > *b) {
            return 1;
        }
        if(*a == '\0') {
            break;
        }
        ++a;
        ++b;
    }
    return 0;
}



bool is_path_sep(char c) {
    if (c=='/') {
        return true;
    }
#ifdef __WIN32
    if (c=='\\' || c==':') {
        return true;
    }
#endif
    return false;
}


// examples:
// "wibble.gif" => ".gif"
// "foo/bar.wibble/blah" => ""
// "foo/bar.wibble/blah.png" => ".png"
// ".config" => ".config"
// "foo.tar.gz" => ".gz"
const char* ext_part( const char* path)
{
    int n;
    for (n=(int)strlen(path)-1; n>=0; --n) {
        if (is_path_sep(path[n])) {
            break;
        }
        if(path[n]=='.') {
            return &path[n];
        }
    }

    return "";
}

