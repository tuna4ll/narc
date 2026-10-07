#pragma once
#include <kernel/file.h>

int pipe_create(struct open_file **read_end, struct open_file **write_end);
