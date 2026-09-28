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
    default:                    return EIO;
    }
}

long __libc_result(narc_result_t result) {
    if (result.status == NARC_OK) return (long)result.value;
    errno = status_errno(result.status);
    return -1;
}
