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
    NARC_SYS_GETPPID   = 0x000a,
    NARC_SYS_SETPGID   = 0x000b,
    NARC_SYS_GETPGID   = 0x000c,
    NARC_SYS_SETSID    = 0x000d,
    NARC_SYS_GETSID    = 0x000e,
    NARC_SYS_UMASK     = 0x000f,
    NARC_SYS_UNAME     = 0x0010,
    NARC_SYS_SETHOSTNAME = 0x0011,

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
    NARC_SYS_FCNTL     = 0x010e,
    NARC_SYS_IOCTL     = 0x010f,
    NARC_SYS_MOUNT     = 0x0110,
    NARC_SYS_UMOUNT    = 0x0111,
    NARC_SYS_STAT      = 0x0112,
    NARC_SYS_SYMLINK   = 0x0113,
    NARC_SYS_READLINK  = 0x0114,
    NARC_SYS_LINK      = 0x0115,
    NARC_SYS_SET_ATTR  = 0x0116,
    NARC_SYS_FILE_SET_ATTR = 0x0117,
    NARC_SYS_SYNC      = 0x0118,
    NARC_SYS_FS_INFO   = 0x0119,
    NARC_SYS_FILE_FS_INFO = 0x011a,

    NARC_SYS_MAP       = 0x0200,
    NARC_SYS_UNMAP     = 0x0201,

    NARC_SYS_CLOCK_GET = 0x0300,
    NARC_SYS_SLEEP     = 0x0301,

    NARC_SYS_KILL        = 0x0400,
    NARC_SYS_SIGACTION   = 0x0401,
    NARC_SYS_SIGMASK     = 0x0402,
    NARC_SYS_SIGPENDING  = 0x0403,
    NARC_SYS_SIGSUSPEND  = 0x0404,
    NARC_SYS_SIGRETURN   = 0x0405,
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
    NARC_INTERRUPTED      = 23,
    NARC_NO_PROCESS       = 24,
    NARC_NOT_PERMITTED    = 25,
    NARC_EXEC_FORMAT      = 26,
    NARC_ACCESS_DENIED    = 27,
    NARC_TOO_BIG          = 28,
    NARC_BUSY             = 29,
    NARC_NO_DEVICE        = 30,
    NARC_READ_ONLY_FS     = 31,
    NARC_LOOP             = 32,
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

#define NARC_SIG_DFL UINT64_C(0)
#define NARC_SIG_IGN UINT64_C(1)

enum narc_signal_flags {
    NARC_SA_NOCLDSTOP = 0x00000001u,
    NARC_SA_NOCLDWAIT = 0x00000002u,
    NARC_SA_SIGINFO   = 0x00000004u,
    NARC_SA_RESTART   = 0x10000000u,
    NARC_SA_NODEFER   = 0x40000000u,
    NARC_SA_RESETHAND = 0x80000000u,
};

enum narc_signal_mask_how {
    NARC_SIG_BLOCK   = 0,
    NARC_SIG_UNBLOCK = 1,
    NARC_SIG_SETMASK = 2,
};

enum narc_signal_code {
    NARC_SI_USER    = 0,
    NARC_SI_KERNEL  = 0x80,
    NARC_CLD_EXITED = 1,
    NARC_CLD_KILLED = 2,
    NARC_CLD_DUMPED = 3,
    NARC_CLD_TRAPPED = 4,
    NARC_CLD_STOPPED = 5,
    NARC_CLD_CONTINUED = 6,
};

typedef struct narc_sigaction {
    uint64_t handler;
    uint64_t flags;
    uint64_t restorer;
    uint64_t mask;
} narc_sigaction_t;

typedef struct narc_siginfo {
    int32_t signo;
    int32_t code;
    int32_t pid;
    int32_t status;
    uint64_t address;
    uint64_t reserved;
} narc_siginfo_t;

enum narc_wait_flags {
    NARC_WAIT_NOHANG    = 1u << 0,
    NARC_WAIT_UNTRACED  = 1u << 1,
    NARC_WAIT_CONTINUED = 1u << 3,
};

enum narc_open_flags {
    NARC_OPEN_READ      = 1u << 0,
    NARC_OPEN_WRITE     = 1u << 1,
    NARC_OPEN_CREATE    = 1u << 2,
    NARC_OPEN_DIRECTORY = 1u << 3,
    NARC_OPEN_TRUNCATE  = 1u << 4,
    NARC_OPEN_EXCLUSIVE = 1u << 5,
    NARC_OPEN_APPEND    = 1u << 6,
    NARC_OPEN_NONBLOCK  = 1u << 7,
    NARC_OPEN_CLOEXEC   = 1u << 8,
    NARC_OPEN_NOCTTY    = 1u << 9,
};

enum narc_mount_flags {
    NARC_MOUNT_READONLY = 1u << 0,
};

enum narc_fcntl_command {
    NARC_FCNTL_DUPFD         = 0,
    NARC_FCNTL_DUPFD_CLOEXEC = 1,
    NARC_FCNTL_GETFD         = 2,
    NARC_FCNTL_SETFD         = 3,
    NARC_FCNTL_GETFL         = 4,
    NARC_FCNTL_SETFL         = 5,
};

#define NARC_FD_CLOEXEC 1u

enum narc_seek_origin {
    NARC_SEEK_BEGIN   = 0,
    NARC_SEEK_CURRENT = 1,
    NARC_SEEK_END     = 2,
};

enum narc_map_flags {
    NARC_MAP_READ  = 1u << 0,
    NARC_MAP_WRITE = 1u << 1,
};

enum narc_clock {
    NARC_CLOCK_REALTIME  = 0,
    NARC_CLOCK_MONOTONIC = 1,
};

enum narc_file_type {
    NARC_FILE_REGULAR   = 1,
    NARC_FILE_DIRECTORY = 2,
    NARC_FILE_CHARACTER = 3,
    NARC_FILE_BLOCK     = 4,
    NARC_FILE_SYMLINK   = 5,
};

#define NARC_NCCS 32

enum narc_tty_request {
    NARC_TCGETS     = 0x5401,
    NARC_TCSETS     = 0x5402,
    NARC_TCSETSW    = 0x5403,
    NARC_TCSETSF    = 0x5404,
    NARC_TCFLSH     = 0x540b,
    NARC_TIOCSCTTY  = 0x540e,
    NARC_TIOCGPGRP  = 0x540f,
    NARC_TIOCSPGRP  = 0x5410,
    NARC_TIOCOUTQ   = 0x5411,
    NARC_TIOCGWINSZ = 0x5413,
    NARC_TIOCSWINSZ = 0x5414,
    NARC_FIONREAD   = 0x541b,
    NARC_TIOCNOTTY  = 0x5422,
    NARC_TIOCGSID   = 0x5429,
};

typedef struct narc_termios {
    uint32_t iflag;
    uint32_t oflag;
    uint32_t cflag;
    uint32_t lflag;
    uint8_t line;
    uint8_t cc[NARC_NCCS];
    uint8_t reserved[3];
    uint32_t ispeed;
    uint32_t ospeed;
} narc_termios_t;

typedef struct narc_winsize {
    uint16_t rows;
    uint16_t cols;
    uint16_t xpixels;
    uint16_t ypixels;
} narc_winsize_t;

#define NARC_UTS_LENGTH 65

typedef struct narc_utsname {
    char sysname[NARC_UTS_LENGTH];
    char nodename[NARC_UTS_LENGTH];
    char release[NARC_UTS_LENGTH];
    char version[NARC_UTS_LENGTH];
    char machine[NARC_UTS_LENGTH];
} narc_utsname_t;

#define NARC_NAME_MAX 255

typedef struct narc_file_info {
    uint64_t inode;
    uint64_t size;
    uint32_t mode;
    uint8_t type;
    uint8_t reserved[3];
    uint32_t nlink;
    uint32_t uid;
    uint32_t gid;
    uint32_t rdev;
    uint64_t device;
    uint64_t blocks;
    int64_t atime;
    int64_t mtime;
    int64_t ctime;
} narc_file_info_t;

#define NARC_STAT_NOFOLLOW 1u

enum narc_attr_mask {
    NARC_ATTR_MODE      = 1u << 0,
    NARC_ATTR_UID       = 1u << 1,
    NARC_ATTR_GID       = 1u << 2,
    NARC_ATTR_SIZE      = 1u << 3,
    NARC_ATTR_ATIME     = 1u << 4,
    NARC_ATTR_MTIME     = 1u << 5,
    NARC_ATTR_ATIME_NOW = 1u << 6,
    NARC_ATTR_MTIME_NOW = 1u << 7,
};

typedef struct narc_file_attr {
    uint32_t mask;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint64_t size;
    int64_t atime;
    int64_t mtime;
} narc_file_attr_t;

#define NARC_FS_TYPE_LENGTH 16

typedef struct narc_fs_info {
    uint64_t block_size;
    uint64_t blocks;
    uint64_t free_blocks;
    uint64_t available_blocks;
    uint64_t files;
    uint64_t free_files;
    uint64_t device;
    uint32_t name_max;
    uint32_t flags;
    char type[NARC_FS_TYPE_LENGTH];
} narc_fs_info_t;

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
