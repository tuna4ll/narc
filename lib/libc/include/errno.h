#pragma once

#define ENOENT  2
#define EIO     5
#define EINTR   4
#define EBADF   9
#define ECHILD 10
#define EAGAIN 11
#define ENOMEM 12
#define EFAULT 14
#define EINVAL 22
#define EMFILE 24
#define ENOTTY 25
#define EISDIR 21
#define ENOTDIR 20
#define EROFS  30
#define ENOSYS 38
#define EOVERFLOW 75

int *__errno_location(void);

#define errno (*__errno_location())
