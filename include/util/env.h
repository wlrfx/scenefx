#ifndef UTIL_ENV_H
#define UTIL_ENV_H

#include <stdbool.h>
#include <unistd.h>

bool env_parse_bool(const char *option);

ssize_t env_parse_switch(const char *option, const char **switches);

#endif
