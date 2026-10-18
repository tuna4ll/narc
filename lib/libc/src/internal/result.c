#include "narc.h"
#include <errno.h>

static int status_errno(uint32_t status) {
    switch (status) {
    case NARC_INVALID_ARGUMENT: return EINVAL;
    case NARC_BAD_HANDLE:       return EBADF;
    case NARC_NOT_FOUND:        return ENOENT;
    case NARC_IO_ERROR:         return EIO;
    case NARC_BAD_ADDRESS:      return EFAULT;
    case NARC_NO_MEMORY:        return ENOMEM;
    case NARC_TOO_MANY_HANDLES: return EMFILE;
    case NARC_NOT_A_TTY:        return ENOTTY;
    case NARC_IS_DIRECTORY:     return EISDIR;
    case NARC_NOT_DIRECTORY:    return ENOTDIR;
    case NARC_READ_ONLY:        return EROFS;
    case NARC_NOT_SUPPORTED:    return ENOSYS;
    case NARC_NO_CHILD:         return ECHILD;
    case NARC_TRY_AGAIN:        return EAGAIN;
    case NARC_EXISTS:           return EEXIST;
    case NARC_NO_SPACE:         return ENOSPC;
    case NARC_NOT_EMPTY:        return ENOTEMPTY;
    case NARC_BROKEN_PIPE:      return EPIPE;
    case NARC_NAME_TOO_LONG:    return ENAMETOOLONG;
    case NARC_CROSS_DEVICE:     return EXDEV;
    case NARC_ILLEGAL_SEEK:     return ESPIPE;
    case NARC_OUT_OF_RANGE:     return ERANGE;
    case NARC_INTERRUPTED:      return EINTR;
    case NARC_NO_PROCESS:       return ESRCH;
    case NARC_NOT_PERMITTED:    return EPERM;
    case NARC_EXEC_FORMAT:      return ENOEXEC;
    case NARC_ACCESS_DENIED:    return EACCES;
    case NARC_TOO_BIG:          return E2BIG;
    case NARC_BUSY:             return EBUSY;
    case NARC_NO_DEVICE:        return ENODEV;
    case NARC_READ_ONLY_FS:     return EROFS;
    case NARC_LOOP:             return ELOOP;
    default:                    return EIO;
    }
}

long __libc_result(narc_result_t result) {
    if (result.status == NARC_OK) return (long)result.value;
    errno = status_errno(result.status);
    return -1;
}
