#pragma once

#include <sys/types.h>

const char *getuser(uid_t uid);
const char *getgroup(gid_t gid);
uid_t *getuidbyname(const char *user);
gid_t *getgidbyname(const char *group);
int xgetgroups(const char *username, gid_t gid, gid_t **groups);
