#include <kernel/file.h>
#include <kernel/heap.h>
#include <kernel/mm.h>
#include <kernel/pipe.h>
#include <kernel/sched.h>
#include <kernel/signal.h>
#include <kernel/string.h>
#include <kernel/syscall.h>
#include <kernel/system.h>
#include <kernel/task.h>
#include <kernel/time.h>
#include <kernel/uaccess.h>
#include <kernel/user.h>
#include <kernel/vfs.h>
#include <narc/abi.h>
#include <stddef.h>
#include <stdint.h>


struct kernel_result {
    uint64_t value;
    uint32_t status;
};

static struct kernel_result result_ok(uint64_t value) {
    return (struct kernel_result) { value, NARC_OK };
}

static struct kernel_result result_error(uint32_t status) {
    return (struct kernel_result) { 0, status };
}

static struct kernel_result result_status(long value) {
    return value < 0 ? result_error((uint32_t)-value) : result_ok((uint64_t)value);
}

static int copy_from_user(void *dst, uint64_t src, size_t len) {
    struct address_space *space = vmm_space_current();
    if (!vmm_user_range_ok(space, src, len, 0)) return -1;
    return uaccess_read(space, dst, src, len);
}

static int copy_to_user(uint64_t dst, const void *src, size_t len) {
    struct address_space *space = vmm_space_current();
    if (!vmm_user_range_ok(space, dst, len, 1)) return -1;
    return uaccess_write(space, dst, src, len);
}

static char *copy_user_string(uint64_t address) {
    struct address_space *space = vmm_space_current();
    long length = uaccess_strlen(space, address);
    if (length < 0) return 0;
    char *string = kmalloc((size_t)length + 1);
    if (!string) return 0;
    if (uaccess_read(space, string, address, (size_t)length + 1) != 0) {
        kfree(string);
        return 0;
    }
    return string;
}

static uint32_t copy_path(char **path, uint64_t address, uint64_t length) {
    *path = 0;
    if (!length || length >= SIZE_MAX) return NARC_INVALID_ARGUMENT;
    if (!vmm_user_range_ok(vmm_space_current(), address, length, 0)) return NARC_BAD_ADDRESS;
    char *copy = kmalloc((size_t)length + 1);
    if (!copy) return NARC_NO_MEMORY;
    if (copy_from_user(copy, address, (size_t)length) != 0) {
        kfree(copy);
        return NARC_BAD_ADDRESS;
    }
    for (uint64_t i = 0; i < length; i++) {
        if (copy[i]) continue;
        kfree(copy);
        return NARC_INVALID_ARGUMENT;
    }
    copy[length] = 0;
    *path = copy;
    return NARC_OK;
}

static struct kernel_result read_file(struct open_file *open, uint64_t buffer,
                                      uint64_t length) {
    if (open->node && open->node->type == VFS_DIR) return result_error(NARC_IS_DIRECTORY);
    if (!vmm_user_range_ok(vmm_space_current(), buffer, length, 1))
        return result_error(NARC_BAD_ADDRESS);

    uint8_t chunk[512];
    uint64_t done = 0;
    while (done < length) {
        size_t want = length - done > sizeof(chunk) ? sizeof(chunk) : (size_t)(length - done);
        long got = file_read(open, chunk, want);
        if (got < 0) return done ? result_ok(done) : result_error((uint32_t)-got);
        if (!got) break;
        if (copy_to_user(buffer + done, chunk, (size_t)got) != 0)
            return result_error(NARC_BAD_ADDRESS);
        done += (uint64_t)got;
        if (!open->node || (size_t)got < want) break;
    }
    return result_ok(done);
}

static struct kernel_result handle_read(uint64_t fd, uint64_t buffer, uint64_t length) {
    struct open_file *open = fd_get(task_files(), (int)fd);
    if (!open) return result_error(NARC_BAD_HANDLE);
    struct kernel_result result = read_file(open, buffer, length);
    file_put(open);
    return result;
}

static struct kernel_result write_file(struct open_file *open, uint64_t buffer,
                                       uint64_t length) {
    struct address_space *space = vmm_space_current();
    if (!vmm_user_range_ok(space, buffer, length, 0))
        return result_error(NARC_BAD_ADDRESS);

    uint64_t done = 0;
    while (done < length) {
        uint64_t ptr = buffer + done;
        uint64_t phys = vmm_user_phys(space, ptr);
        if (!phys) return result_error(NARC_BAD_ADDRESS);
        size_t chunk = PAGE_SIZE - (size_t)(ptr & (PAGE_SIZE - 1));
        if ((uint64_t)chunk > length - done) chunk = (size_t)(length - done);
        long wrote = file_write(open, phys_to_virt(phys), chunk);
        if (wrote < 0) return done ? result_ok(done) : result_error((uint32_t)-wrote);
        done += (uint64_t)wrote;
        if ((size_t)wrote < chunk) break;
    }
    return result_ok(done);
}

static struct kernel_result handle_write(uint64_t fd, uint64_t buffer, uint64_t length) {
    struct open_file *open = fd_get(task_files(), (int)fd);
    if (!open) return result_error(NARC_BAD_HANDLE);
    struct kernel_result result = write_file(open, buffer, length);
    file_put(open);
    return result;
}

static struct kernel_result handle_open(uint64_t address, uint64_t length, uint64_t flags,
                                        uint64_t mode) {
    const uint64_t known = NARC_OPEN_READ | NARC_OPEN_WRITE |
                           NARC_OPEN_CREATE | NARC_OPEN_DIRECTORY |
                           NARC_OPEN_TRUNCATE | NARC_OPEN_EXCLUSIVE |
                           NARC_OPEN_APPEND | NARC_OPEN_NONBLOCK |
                           NARC_OPEN_CLOEXEC | NARC_OPEN_NOCTTY;
    if (!length || (flags & ~known) ||
        !(flags & (NARC_OPEN_READ | NARC_OPEN_WRITE)) ||
        ((flags & NARC_OPEN_TRUNCATE) && !(flags & NARC_OPEN_WRITE)))
        return result_error(NARC_INVALID_ARGUMENT);

    char *path;
    uint32_t path_status = copy_path(&path, address, length);
    if (path_status != NARC_OK) return result_error(path_status);

    uint32_t native_flags = 0;
    if (flags & NARC_OPEN_READ) native_flags |= VFS_OPEN_READ;
    if (flags & NARC_OPEN_WRITE) native_flags |= VFS_OPEN_WRITE;
    if (flags & NARC_OPEN_CREATE) native_flags |= VFS_OPEN_CREATE;
    if (flags & NARC_OPEN_DIRECTORY) native_flags |= VFS_OPEN_DIRECTORY;
    if (flags & NARC_OPEN_TRUNCATE) native_flags |= VFS_OPEN_TRUNCATE;
    if (flags & NARC_OPEN_EXCLUSIVE) native_flags |= VFS_OPEN_EXCLUSIVE;
    if (flags & NARC_OPEN_APPEND) native_flags |= VFS_OPEN_APPEND;
    if (flags & NARC_OPEN_NONBLOCK) native_flags |= VFS_OPEN_NONBLOCK;
    struct open_file *open;
    uint32_t create_mode = (uint32_t)mode & 07777 & ~task_umask(0, 0);
    int status = file_open(task_cwd(), path, native_flags, create_mode, &open);
    kfree(path);
    if (status) return result_error((uint32_t)-status);
    int fd = fd_install(task_files(), open, 0, (flags & NARC_OPEN_CLOEXEC) ? FD_CLOEXEC : 0);
    if (fd < 0) {
        file_put(open);
        return result_error(NARC_NO_MEMORY);
    }
    return result_ok((uint64_t)fd);
}

static struct kernel_result handle_mkdir(uint64_t address, uint64_t length, uint64_t mode) {
    char *path;
    uint32_t status = copy_path(&path, address, length);
    if (status != NARC_OK) return result_error(status);
    int result = vfs_mkdir(task_cwd(), path, (uint32_t)mode & 07777 & ~task_umask(0, 0));
    kfree(path);
    return result_status(result);
}

static struct kernel_result handle_unlink(uint64_t address, uint64_t length,
                                          int remove_directory) {
    char *path;
    uint32_t status = copy_path(&path, address, length);
    if (status != NARC_OK) return result_error(status);
    int result = vfs_unlink(task_cwd(), path, remove_directory);
    kfree(path);
    return result_status(result);
}

static struct kernel_result handle_rename(uint64_t old_address, uint64_t old_length,
                                          uint64_t new_address, uint64_t new_length) {
    char *old_path, *new_path;
    uint32_t status = copy_path(&old_path, old_address, old_length);
    if (status != NARC_OK) return result_error(status);
    status = copy_path(&new_path, new_address, new_length);
    if (status != NARC_OK) {
        kfree(old_path);
        return result_error(status);
    }
    int result = vfs_rename(task_cwd(), old_path, new_path);
    kfree(old_path);
    kfree(new_path);
    return result_status(result);
}

static struct kernel_result handle_chdir(uint64_t address, uint64_t length) {
    char *path;
    uint32_t status = copy_path(&path, address, length);
    if (status != NARC_OK) return result_error(status);
    struct vnode *node;
    int result = vfs_lookup(task_cwd(), path, &node);
    kfree(path);
    if (result) return result_status(result);
    if (node->type != VFS_DIR) {
        vnode_put(node);
        return result_error(NARC_NOT_DIRECTORY);
    }
    task_set_cwd(node);
    return result_ok(0);
}

static struct kernel_result handle_fchdir(uint64_t fd) {
    struct open_file *open = fd_get(task_files(), (int)fd);
    if (!open) return result_error(NARC_BAD_HANDLE);
    struct vnode *node = open->node;
    int directory = node && node->type == VFS_DIR;
    if (directory) task_set_cwd(vnode_get(node));
    file_put(open);
    return directory ? result_ok(0) : result_error(NARC_NOT_DIRECTORY);
}

static struct kernel_result handle_getcwd(uint64_t address, uint64_t size) {
    if (!size) return result_error(NARC_INVALID_ARGUMENT);
    if (size > VFS_PATH_MAX) size = VFS_PATH_MAX;
    char *buffer = kmalloc((size_t)size);
    if (!buffer) return result_error(NARC_NO_MEMORY);
    long length = vfs_getcwd(task_cwd(), buffer, (size_t)size);
    if (length >= 0 && copy_to_user(address, buffer, (size_t)length + 1) != 0)
        length = -NARC_BAD_ADDRESS;
    kfree(buffer);
    return result_status(length);
}

static struct kernel_result handle_close(uint64_t fd) {
    if (fd_close(task_files(), (int)fd) != 0) return result_error(NARC_BAD_HANDLE);
    return result_ok(0);
}

static struct kernel_result handle_seek(uint64_t fd, int64_t offset, uint64_t origin) {
    if (origin > NARC_SEEK_END) return result_error(NARC_INVALID_ARGUMENT);
    struct open_file *open = fd_get(task_files(), (int)fd);
    if (!open) return result_error(NARC_BAD_HANDLE);
    long value = file_seek(open, offset, (int)origin);
    file_put(open);
    return result_status(value);
}

static uint8_t native_type(uint8_t type) {
    switch (type) {
    case VFS_DIR: return NARC_FILE_DIRECTORY;
    case VFS_CHR: return NARC_FILE_CHARACTER;
    case VFS_BLK: return NARC_FILE_BLOCK;
    case VFS_LNK: return NARC_FILE_SYMLINK;
    default:      return NARC_FILE_REGULAR;
    }
}

static struct kernel_result handle_file_info(uint64_t fd, uint64_t address) {
    struct open_file *open = fd_get(task_files(), (int)fd);
    if (!open) return result_error(NARC_BAD_HANDLE);

    narc_file_info_t info = { 0 };
    struct vnode *node = open->node;
    if (node) {
        info.inode = node->ino;
        info.size = node->size;
        info.mode = node->mode;
        info.type = native_type(node->type);
    } else {
        info.mode = 0020000 | 0666;
        info.type = NARC_FILE_CHARACTER;
    }
    file_put(open);
    if (copy_to_user(address, &info, sizeof(info)) != 0)
        return result_error(NARC_BAD_ADDRESS);
    return result_ok(0);
}

static struct kernel_result handle_read_dir(uint64_t fd, uint64_t address) {
    struct open_file *open = fd_get(task_files(), (int)fd);
    if (!open) return result_error(NARC_BAD_HANDLE);
    struct vfs_dirent source;
    int result = file_readdir(open, &source);
    file_put(open);
    if (result <= 0) return result_status(result);

    narc_dir_entry_t entry = { 0 };
    entry.inode = source.ino;
    entry.type = native_type(source.type);
    memcpy(entry.name, source.name, sizeof(entry.name));
    if (copy_to_user(address, &entry, sizeof(entry)) != 0)
        return result_error(NARC_BAD_ADDRESS);
    return result_ok(1);
}

static struct kernel_result handle_pipe(uint64_t address, uint64_t flags) {
    if (flags & ~(uint64_t)(NARC_OPEN_CLOEXEC | NARC_OPEN_NONBLOCK))
        return result_error(NARC_INVALID_ARGUMENT);
    struct open_file *read_end, *write_end;
    if (!vmm_user_range_ok(vmm_space_current(), address, 2 * sizeof(int32_t), 1))
        return result_error(NARC_BAD_ADDRESS);
    if (pipe_create(&read_end, &write_end) != 0) return result_error(NARC_NO_MEMORY);
    if (flags & NARC_OPEN_NONBLOCK) {
        read_end->flags |= VFS_OPEN_NONBLOCK;
        write_end->flags |= VFS_OPEN_NONBLOCK;
    }
    uint32_t fd_flags = (flags & NARC_OPEN_CLOEXEC) ? FD_CLOEXEC : 0;
    struct fd_table *table = task_files();
    int32_t fds[2] = { fd_install(table, read_end, 0, fd_flags), -1 };
    if (fds[0] >= 0) fds[1] = fd_install(table, write_end, 0, fd_flags);
    if (fds[1] < 0) {
        if (fds[0] >= 0) fd_close(table, fds[0]);
        else file_put(read_end);
        file_put(write_end);
        return result_error(NARC_TOO_MANY_HANDLES);
    }
    if (copy_to_user(address, fds, sizeof(fds)) != 0) {
        fd_close(table, fds[0]);
        fd_close(table, fds[1]);
        return result_error(NARC_BAD_ADDRESS);
    }
    return result_ok(0);
}

static struct kernel_result handle_dup(uint64_t fd, uint64_t minimum) {
    if (minimum > INT32_MAX) return result_error(NARC_INVALID_ARGUMENT);
    int result = fd_dup(task_files(), (int)fd, (int)minimum, 0);
    return result < 0 ? result_error(NARC_BAD_HANDLE) : result_ok((uint64_t)result);
}

static struct kernel_result handle_dup2(uint64_t fd, uint64_t target) {
    if (target >= FD_LIMIT) return result_error(NARC_BAD_HANDLE);
    int result = fd_dup2(task_files(), (int)fd, (int)target);
    return result < 0 ? result_error(NARC_BAD_HANDLE) : result_ok((uint64_t)result);
}

static uint32_t native_status_flags(uint32_t flags) {
    uint32_t result = 0;
    if (flags & VFS_OPEN_READ) result |= NARC_OPEN_READ;
    if (flags & VFS_OPEN_WRITE) result |= NARC_OPEN_WRITE;
    if (flags & VFS_OPEN_APPEND) result |= NARC_OPEN_APPEND;
    if (flags & VFS_OPEN_NONBLOCK) result |= NARC_OPEN_NONBLOCK;
    return result;
}

static struct kernel_result handle_fcntl(uint64_t fd, uint64_t command, uint64_t argument) {
    struct fd_table *table = task_files();
    switch (command) {
    case NARC_FCNTL_DUPFD:
    case NARC_FCNTL_DUPFD_CLOEXEC: {
        if (argument >= FD_LIMIT) return result_error(NARC_INVALID_ARGUMENT);
        int result = fd_dup(table, (int)fd, (int)argument,
                            command == NARC_FCNTL_DUPFD_CLOEXEC ? FD_CLOEXEC : 0);
        return result < 0 ? result_error(fd_get_flags(table, (int)fd) < 0 ? NARC_BAD_HANDLE :
                                                                             NARC_TOO_MANY_HANDLES)
                          : result_ok((uint64_t)result);
    }
    case NARC_FCNTL_GETFD: {
        int flags = fd_get_flags(table, (int)fd);
        return flags < 0 ? result_error(NARC_BAD_HANDLE) : result_ok((uint64_t)flags);
    }
    case NARC_FCNTL_SETFD:
        return fd_set_flags(table, (int)fd, (uint32_t)argument) ? result_error(NARC_BAD_HANDLE)
                                                                : result_ok(0);
    case NARC_FCNTL_GETFL:
    case NARC_FCNTL_SETFL: {
        struct open_file *open = fd_get(table, (int)fd);
        if (!open) return result_error(NARC_BAD_HANDLE);
        uint32_t flags = native_status_flags(open->flags);
        if (command == NARC_FCNTL_SETFL) {
            uint32_t mutable = VFS_OPEN_APPEND | VFS_OPEN_NONBLOCK;
            uint32_t next = 0;
            if (argument & NARC_OPEN_APPEND) next |= VFS_OPEN_APPEND;
            if (argument & NARC_OPEN_NONBLOCK) next |= VFS_OPEN_NONBLOCK;
            __atomic_store_n(&open->flags, (open->flags & ~mutable) | next, __ATOMIC_RELAXED);
            flags = 0;
        }
        file_put(open);
        return result_ok(flags);
    }
    default:
        return result_error(NARC_INVALID_ARGUMENT);
    }
}

static struct kernel_result handle_ioctl(uint64_t fd, uint64_t request, uint64_t argument) {
    struct open_file *open = fd_get(task_files(), (int)fd);
    if (!open) return result_error(NARC_BAD_HANDLE);
    long result = file_ioctl(open, (uint32_t)request, argument);
    file_put(open);
    return result_status(result);
}

static uint64_t page_align(uint64_t value) {
    if (value > UINT64_MAX - (PAGE_SIZE - 1)) return 0;
    return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static struct kernel_result handle_map(uint64_t length, uint64_t flags) {
    const uint64_t known = NARC_MAP_READ | NARC_MAP_WRITE;
    if (!length || (flags & ~known) || !(flags & NARC_MAP_READ))
        return result_error(NARC_INVALID_ARGUMENT);

    uint64_t size = page_align(length);
    uint64_t base = page_align(task_mmap_next());
    if (!size || base < USER_MMAP_BASE || base > vmm_user_end() || size > vmm_user_end() - base)
        return result_error(NARC_NO_MEMORY);

    struct address_space *space = vmm_space_current();
    uint64_t mapped = 0;
    while (mapped < size) {
        uint64_t phys = pmm_alloc_page();
        if (!phys) break;
        if (vmm_map_user(space, base + mapped, phys,
                         (flags & NARC_MAP_WRITE) ? VMM_WRITE : 0) != 0) {
            pmm_free_page(phys);
            break;
        }
        mapped += PAGE_SIZE;
    }
    if (mapped != size) {
        for (uint64_t off = 0; off < mapped; off += PAGE_SIZE)
            vmm_unmap_user(space, base + off);
        return result_error(NARC_NO_MEMORY);
    }

    task_set_mmap_next(base + size);
    return result_ok(base);
}

static struct kernel_result handle_unmap(uint64_t address, uint64_t length) {
    if (!length || (address & (PAGE_SIZE - 1)))
        return result_error(NARC_INVALID_ARGUMENT);

    uint64_t size = page_align(length);
    if (!size || address < USER_MMAP_BASE || address > vmm_user_end() ||
        size > vmm_user_end() - address)
        return result_error(NARC_INVALID_ARGUMENT);

    struct address_space *space = vmm_space_current();
    for (uint64_t off = 0; off < size; off += PAGE_SIZE)
        if (!vmm_user_phys(space, address + off))
            return result_error(NARC_INVALID_ARGUMENT);
    for (uint64_t off = 0; off < size; off += PAGE_SIZE)
        vmm_unmap_user(space, address + off);
    return result_ok(0);
}

static struct kernel_result handle_clock_get(uint64_t clock) {
    if (clock == NARC_CLOCK_REALTIME) return result_ok((uint64_t)time_realtime_ns());
    if (clock == NARC_CLOCK_MONOTONIC) return result_ok(time_monotonic_ns());
    return result_error(NARC_INVALID_ARGUMENT);
}

static struct kernel_result handle_sleep(uint64_t nanoseconds) {
    uint64_t now = time_monotonic_ns();
    uint64_t deadline = nanoseconds > UINT64_MAX - now ? UINT64_MAX : now + nanoseconds;
    if (!time_sleep_until(deadline)) return result_ok(0);
    now = time_monotonic_ns();
    return (struct kernel_result) { deadline > now ? deadline - now : 0, NARC_INTERRUPTED };
}

static uint32_t handle_exec(struct task_frame *frame, uint64_t path_address,
                            uint64_t argv_address, uint64_t envp_address) {
    char *path = copy_user_string(path_address);
    if (!path) return NARC_BAD_ADDRESS;
    int status = user_exec(frame, path, argv_address, envp_address);
    kfree(path);
    return (uint32_t)-status;
}

static void return_result(struct task_frame *frame, struct kernel_result result) {
    arch_syscall_return2(frame, result.value, result.status);
}

static void dispatch(struct task_frame *frame, uint64_t id) {
    uint64_t a1 = arch_syscall_arg(frame, 0);
    uint64_t a2 = arch_syscall_arg(frame, 1);
    uint64_t a3 = arch_syscall_arg(frame, 2);

    switch (id) {
    case NARC_SYS_ABI_QUERY:
        return_result(frame, result_ok(NARC_ABI_VERSION));
        return;
    case NARC_SYS_EXIT:
        task_exit((int)a1);
    case NARC_SYS_GETPID:
        return_result(frame, result_ok((uint64_t)task_pid()));
        return;
    case NARC_SYS_YIELD:
        return_result(frame, result_ok(0));
        schedule();
        return;
    case NARC_SYS_FORK: {
        int pid = task_fork(frame);
        return_result(frame, pid < 0 ? result_error(NARC_TRY_AGAIN) : result_ok((uint64_t)pid));
        return;
    }
    case NARC_SYS_WAIT:
        return_result(frame, result_status(task_wait((int)a1, a2, (int)a3)));
        return;
    case NARC_SYS_EXEC: {
        uint32_t status = handle_exec(frame, a1, a2, a3);
        if (status != NARC_OK) return_result(frame, result_error(status));
        return;
    }
    case NARC_SYS_GETPPID:
        return_result(frame, result_ok((uint64_t)task_ppid()));
        return;
    case NARC_SYS_SETPGID:
        return_result(frame, result_status(task_setpgid((int)a1, (int)a2)));
        return;
    case NARC_SYS_GETPGID:
        return_result(frame, result_status(task_getpgid((int)a1)));
        return;
    case NARC_SYS_SETSID:
        return_result(frame, result_status(task_setsid()));
        return;
    case NARC_SYS_GETSID:
        return_result(frame, result_status(task_getsid((int)a1)));
        return;
    case NARC_SYS_CHDIR:
        return_result(frame, handle_chdir(a1, a2));
        return;
    case NARC_SYS_FCHDIR:
        return_result(frame, handle_fchdir(a1));
        return;
    case NARC_SYS_GETCWD:
        return_result(frame, handle_getcwd(a1, a2));
        return;
    case NARC_SYS_OPEN:
        return_result(frame, handle_open(a1, a2, a3, arch_syscall_arg(frame, 3)));
        return;
    case NARC_SYS_UNAME:
        return_result(frame, result_status(system_uname(a1)));
        return;
    case NARC_SYS_SETHOSTNAME:
        return_result(frame, result_status(system_sethostname(a1, a2)));
        return;
    case NARC_SYS_UMASK:
        return_result(frame, result_ok(task_umask((uint32_t)a1, 1)));
        return;
    case NARC_SYS_CLOSE:
        return_result(frame, handle_close(a1));
        return;
    case NARC_SYS_READ:
        return_result(frame, handle_read(a1, a2, a3));
        return;
    case NARC_SYS_WRITE:
        return_result(frame, handle_write(a1, a2, a3));
        return;
    case NARC_SYS_SEEK:
        return_result(frame, handle_seek(a1, (int64_t)a2, a3));
        return;
    case NARC_SYS_FILE_INFO:
        return_result(frame, handle_file_info(a1, a2));
        return;
    case NARC_SYS_READ_DIR:
        return_result(frame, handle_read_dir(a1, a2));
        return;
    case NARC_SYS_MKDIR:
        return_result(frame, handle_mkdir(a1, a2, a3));
        return;
    case NARC_SYS_UNLINK:
        return_result(frame, handle_unlink(a1, a2, 0));
        return;
    case NARC_SYS_RMDIR:
        return_result(frame, handle_unlink(a1, a2, 1));
        return;
    case NARC_SYS_RENAME: {
        uint64_t a4 = arch_syscall_arg(frame, 3);
        return_result(frame, handle_rename(a1, a2, a3, a4));
        return;
    }
    case NARC_SYS_PIPE:
        return_result(frame, handle_pipe(a1, a2));
        return;
    case NARC_SYS_DUP:
        return_result(frame, handle_dup(a1, a2));
        return;
    case NARC_SYS_DUP2:
        return_result(frame, handle_dup2(a1, a2));
        return;
    case NARC_SYS_FCNTL:
        return_result(frame, handle_fcntl(a1, a2, a3));
        return;
    case NARC_SYS_IOCTL:
        return_result(frame, handle_ioctl(a1, a2, a3));
        return;
    case NARC_SYS_MAP:
        return_result(frame, handle_map(a1, a2));
        return;
    case NARC_SYS_UNMAP:
        return_result(frame, handle_unmap(a1, a2));
        return;
    case NARC_SYS_CLOCK_GET:
        return_result(frame, handle_clock_get(a1));
        return;
    case NARC_SYS_SLEEP:
        return_result(frame, handle_sleep(a1));
        return;
    case NARC_SYS_KILL:
        return_result(frame, result_status(task_kill((int)a1, (int)a2)));
        return;
    case NARC_SYS_SIGACTION:
        return_result(frame, result_status(signal_action((int)a1, a2, a3)));
        return;
    case NARC_SYS_SIGMASK:
        return_result(frame, result_status(signal_mask((int)a1, a2)));
        return;
    case NARC_SYS_SIGPENDING:
        return_result(frame, result_ok(task_current()->signals.pending));
        return;
    case NARC_SYS_SIGSUSPEND:
        return_result(frame, result_status(signal_suspend(a1)));
        return;
    case NARC_SYS_SIGRETURN:
        signal_return(frame);
        return;
    default:
        return_result(frame, result_error(NARC_NOT_SUPPORTED));
        return;
    }
}

static int restartable(uint64_t id) {
    return id == NARC_SYS_READ || id == NARC_SYS_WRITE || id == NARC_SYS_WAIT ||
           id == NARC_SYS_IOCTL;
}

void syscall_dispatch(struct task_frame *frame) {
    struct task *self = task_current();
    uint64_t number = arch_syscall_number(frame);
    self->syscall_saved[0] = number;
    for (unsigned i = 0; i < 3; i++) self->syscall_saved[i + 1] = arch_syscall_arg(frame, i);
    arch_irq_enable();
    if ((number & NARC_SYSCALL_TAG_MASK) != NARC_SYSCALL_TAG) {
        return_result(frame, result_error(NARC_NOT_SUPPORTED));
    } else {
        uint64_t id = number & NARC_SYSCALL_ID_MASK;
        dispatch(frame, id);
        self->syscall_restart = restartable(id) && arch_syscall_status(frame) == NARC_INTERRUPTED;
    }
    arch_irq_disable();
    user_return(frame);
}
