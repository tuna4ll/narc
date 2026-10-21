#pragma once

#include <stdbool.h>
#include <sys/types.h>

bool print_group(gid_t gid, bool use_name);
bool print_group_list(const char *username, uid_t ruid, gid_t rgid, gid_t egid, bool use_names,
                      char delimiter);
