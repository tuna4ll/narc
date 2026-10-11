#pragma once

#include <stddef.h>
#include <stdint.h>

#define NARC_SYSCALL_TAG       UINT64_C(0x4e41524300000000)
#define NARC_SYSCALL_TAG_MASK  UINT64_C(0xffffffff00000000)
#define NARC_SYSCALL_ID_MASK   UINT64_C(0x00000000ffffffff)

#define NARC_ABI_VERSION UINT64_C(2)

enum narc_syscall {
    NARC_SYS_ABI_QUERY = 0x0000,

    NARC_SYS_EXIT      = 0x0001,
    NARC_SYS_GETPID    = 0x0002,
    NARC_SYS_YIELD     = 0x0003,
    NARC_SYS_FORK      = 0x0004,
    NARC_SYS_WAIT      = 0x0005,
    NARC_SYS_EXEC      = 0x0006,
    NARC_SYS_CHDIR     = 0x0007,
    NARC_SYS_FCHDIR    = 0x0008,
    NARC_SYS_GETCWD    = 0x0009,

    NARC_SYS_OPEN      = 0x0100,
    NARC_SYS_CLOSE     = 0x0101,
    NARC_SYS_READ      = 0x0102,
    NARC_SYS_WRITE     = 0x0103,
    NARC_SYS_SEEK      = 0x0104,
    NARC_SYS_FILE_INFO = 0x0105,
    NARC_SYS_READ_DIR  = 0x0106,
    NARC_SYS_MKDIR     = 0x0107,
    NARC_SYS_UNLINK    = 0x0108,
    NARC_SYS_RMDIR     = 0x0109,
    NARC_SYS_RENAME    = 0x010a,
    NARC_SYS_PIPE      = 0x010b,
    NARC_SYS_DUP       = 0x010c,
    NARC_SYS_DUP2      = 0x010d,

    NARC_SYS_MAP       = 0x0200,
    NARC_SYS_UNMAP     = 0x0201,
};

enum narc_status {
    NARC_OK               = 0,
    NARC_INVALID_ARGUMENT = 1,
    NARC_BAD_HANDLE       = 2,
    NARC_NOT_FOUND        = 3,
    NARC_IO_ERROR         = 4,
    NARC_BAD_ADDRESS      = 5,
    NARC_NO_MEMORY        = 6,
    NARC_TOO_MANY_HANDLES = 7,
    NARC_NOT_A_TTY        = 8,
    NARC_IS_DIRECTORY     = 9,
    NARC_NOT_DIRECTORY    = 10,
    NARC_READ_ONLY        = 11,
    NARC_NOT_SUPPORTED    = 12,
    NARC_NO_CHILD         = 13,
    NARC_TRY_AGAIN        = 14,
    NARC_EXISTS           = 15,
    NARC_NO_SPACE         = 16,
    NARC_NOT_EMPTY        = 17,
    NARC_BROKEN_PIPE      = 18,
    NARC_NAME_TOO_LONG    = 19,
    NARC_CROSS_DEVICE     = 20,
    NARC_ILLEGAL_SEEK     = 21,
    NARC_OUT_OF_RANGE     = 22,
};

enum narc_signal {
    NARC_SIGHUP    = 1,
    NARC_SIGINT    = 2,
    NARC_SIGQUIT   = 3,
    NARC_SIGILL    = 4,
    NARC_SIGTRAP   = 5,
    NARC_SIGABRT   = 6,
    NARC_SIGBUS    = 7,
    NARC_SIGFPE    = 8,
    NARC_SIGKILL   = 9,
    NARC_SIGUSR1   = 10,
    NARC_SIGSEGV   = 11,
    NARC_SIGUSR2   = 12,
    NARC_SIGPIPE   = 13,
    NARC_SIGALRM   = 14,
    NARC_SIGTERM   = 15,
    NARC_SIGSTKFLT = 16,
    NARC_SIGCHLD   = 17,
    NARC_SIGCONT   = 18,
    NARC_SIGSTOP   = 19,
    NARC_SIGTSTP   = 20,
    NARC_SIGTTIN   = 21,
    NARC_SIGTTOU   = 22,
    NARC_SIGURG    = 23,
    NARC_SIGXCPU   = 24,
    NARC_SIGXFSZ   = 25,
    NARC_SIGVTALRM = 26,
    NARC_SIGPROF   = 27,
    NARC_SIGWINCH  = 28,
    NARC_SIGIO     = 29,
    NARC_SIGPWR    = 30,
    NARC_SIGSYS    = 31,
    NARC_NSIG      = 32,
};

enum narc_open_flags {
    NARC_OPEN_READ      = 1u << 0,
    NARC_OPEN_WRITE     = 1u << 1,
    NARC_OPEN_CREATE    = 1u << 2,
    NARC_OPEN_DIRECTORY = 1u << 3,
    NARC_OPEN_TRUNCATE  = 1u << 4,
    NARC_OPEN_EXCLUSIVE = 1u << 5,
    NARC_OPEN_APPEND    = 1u << 6,
};

enum narc_seek_origin {
    NARC_SEEK_BEGIN   = 0,
    NARC_SEEK_CURRENT = 1,
    NARC_SEEK_END     = 2,
};

enum narc_map_flags {
    NARC_MAP_READ  = 1u << 0,
    NARC_MAP_WRITE = 1u << 1,
};

enum narc_file_type {
    NARC_FILE_REGULAR   = 1,
    NARC_FILE_DIRECTORY = 2,
    NARC_FILE_CHARACTER = 3,
    NARC_FILE_BLOCK     = 4,
    NARC_FILE_SYMLINK   = 5,
};

#define NARC_NAME_MAX 255

typedef struct narc_file_info {
    uint64_t inode;
    uint64_t size;
    uint32_t mode;
    uint8_t type;
    uint8_t reserved[3];
} narc_file_info_t;

typedef struct narc_dir_entry {
    uint64_t inode;
    uint8_t type;
    char name[NARC_NAME_MAX + 1];
    uint8_t reserved[7];
} narc_dir_entry_t;

typedef struct narc_result {
    int64_t value;
    uint32_t status;
    uint32_t reserved;
} narc_result_t;
