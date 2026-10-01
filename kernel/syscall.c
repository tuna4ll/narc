#include <kernel/mm.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <kernel/user.h>
#include <kernel/vfs.h>
#include <narc/abi.h>
#include <stddef.h>
#include <stdint.h>

#define USER_MMAP_BASE 0x0000000100000000ULL
#define USER_MMAP_END  0x0000000140000000ULL

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

static uint32_t vfs_error(enum vfs_status status) {
    switch (status) {
    case VFS_NOT_FOUND:     return NARC_NOT_FOUND;
    case VFS_EXISTS:        return NARC_EXISTS;
    case VFS_NOT_DIRECTORY: return NARC_NOT_DIRECTORY;
    case VFS_IS_DIRECTORY:  return NARC_IS_DIRECTORY;
    case VFS_NO_SPACE:      return NARC_NO_SPACE;
    case VFS_NOT_EMPTY:     return NARC_NOT_EMPTY;
    default:                return NARC_INVALID_ARGUMENT;
    }
}

static int copy_from_user(void *dst, uint64_t src, size_t len) {
    uint8_t *out = dst;
    struct address_space *space = vmm_space_current();
    if (!vmm_user_range_ok(space, src, len, 0)) return -1;

    while (len) {
        uint64_t phys = vmm_user_phys(space, src);
        if (!phys) return -1;
        size_t chunk = PAGE_SIZE - (size_t)(src & (PAGE_SIZE - 1));
        if (chunk > len) chunk = len;
        const uint8_t *in = phys_to_virt(phys);
        for (size_t i = 0; i < chunk; i++) out[i] = in[i];
        src += chunk;
        out += chunk;
        len -= chunk;
    }
    return 0;
}

static int copy_to_user(uint64_t dst, const void *src, size_t len) {
    const uint8_t *in = src;
    struct address_space *space = vmm_space_current();
    if (!vmm_user_range_ok(space, dst, len, 1)) return -1;

    while (len) {
        uint64_t phys = vmm_user_phys(space, dst);
        if (!phys) return -1;
        size_t chunk = PAGE_SIZE - (size_t)(dst & (PAGE_SIZE - 1));
        if (chunk > len) chunk = len;
        uint8_t *out = phys_to_virt(phys);
        for (size_t i = 0; i < chunk; i++) out[i] = in[i];
        dst += chunk;
        in += chunk;
        len -= chunk;
    }
    return 0;
}

static int copy_string(char *dst, uint64_t src, size_t capacity) {
    for (size_t i = 0; i < capacity; i++) {
        if (copy_from_user(&dst[i], src + i, 1) != 0) return -1;
        if (!dst[i]) return 0;
    }
    return -1;
}

static uint32_t copy_path(char path[VFS_PATH_MAX], uint64_t address, uint64_t length) {
    if (!length || length >= VFS_PATH_MAX) return NARC_INVALID_ARGUMENT;
    if (copy_from_user(path, address, (size_t)length) != 0) return NARC_BAD_ADDRESS;
    for (uint64_t i = 0; i < length; i++)
        if (!path[i]) return NARC_INVALID_ARGUMENT;
    path[length] = 0;
    return NARC_OK;
}

static int copy_string_list(uint64_t address, char storage[16][VFS_PATH_MAX],
                            const char *values[16], size_t *count) {
    *count = 0;
    if (!address) return 0;
    while (*count < 16) {
        uint64_t item;
        if (copy_from_user(&item, address + *count * sizeof(item), sizeof(item)) != 0)
            return -1;
        if (!item) return 0;
        if (copy_string(storage[*count], item, VFS_PATH_MAX) != 0) return -1;
        values[*count] = storage[*count];
        (*count)++;
    }
    uint64_t terminator;
    if (copy_from_user(&terminator, address + *count * sizeof(terminator),
                       sizeof(terminator)) != 0)
        return -1;
    return terminator ? -1 : 0;
}

static struct kernel_result handle_read(uint64_t fd, uint64_t buffer, uint64_t length) {
    if (!task_fd_valid((int)fd)) return result_error(NARC_BAD_HANDLE);
    struct file *file = task_fd_file((int)fd);
    if (file) {
        struct vfs_info info;
        vfs_file_info(file, &info);
        if (info.type == VFS_DIR) return result_error(NARC_IS_DIRECTORY);
    }
    if (!vmm_user_range_ok(vmm_space_current(), buffer, length, 1))
        return result_error(NARC_BAD_ADDRESS);

    uint8_t chunk[512];
    uint64_t done = 0;
    while (done < length) {
        size_t want = length - done > sizeof(chunk) ? sizeof(chunk) : (size_t)(length - done);
        long got = task_fd_read((int)fd, chunk, want);
        if (got < 0) return done ? result_ok(done) : result_error(NARC_IO_ERROR);
        if (!got) break;
        if (copy_to_user(buffer + done, chunk, (size_t)got) != 0)
            return result_error(NARC_BAD_ADDRESS);
        done += (uint64_t)got;
        if ((size_t)got < want) break;
    }
    return result_ok(done);
}

static struct kernel_result handle_write(uint64_t fd, uint64_t buffer, uint64_t length) {
    if (!task_fd_valid((int)fd)) return result_error(NARC_BAD_HANDLE);
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
        long wrote = task_fd_write((int)fd, phys_to_virt(phys), chunk);
        if (wrote < 0) return done ? result_ok(done) : result_error(NARC_IO_ERROR);
        done += (uint64_t)wrote;
        if ((size_t)wrote < chunk) break;
    }
    return result_ok(done);
}

static struct kernel_result handle_open(uint64_t address, uint64_t length, uint64_t flags) {
    const uint64_t known = NARC_OPEN_READ | NARC_OPEN_WRITE |
                           NARC_OPEN_CREATE | NARC_OPEN_DIRECTORY |
                           NARC_OPEN_TRUNCATE | NARC_OPEN_EXCLUSIVE |
                           NARC_OPEN_APPEND;
    if (!length || length >= VFS_PATH_MAX || (flags & ~known) ||
        !(flags & (NARC_OPEN_READ | NARC_OPEN_WRITE)) ||
        ((flags & NARC_OPEN_TRUNCATE) && !(flags & NARC_OPEN_WRITE)))
        return result_error(NARC_INVALID_ARGUMENT);

    char path[VFS_PATH_MAX];
    uint32_t path_status = copy_path(path, address, length);
    if (path_status != NARC_OK) return result_error(path_status);
    if (path[0] != '/') return result_error(NARC_NOT_FOUND);

    uint32_t native_flags = 0;
    if (flags & NARC_OPEN_READ) native_flags |= VFS_OPEN_READ;
    if (flags & NARC_OPEN_WRITE) native_flags |= VFS_OPEN_WRITE;
    if (flags & NARC_OPEN_CREATE) native_flags |= VFS_OPEN_CREATE;
    if (flags & NARC_OPEN_DIRECTORY) native_flags |= VFS_OPEN_DIRECTORY;
    if (flags & NARC_OPEN_TRUNCATE) native_flags |= VFS_OPEN_TRUNCATE;
    if (flags & NARC_OPEN_EXCLUSIVE) native_flags |= VFS_OPEN_EXCLUSIVE;
    if (flags & NARC_OPEN_APPEND) native_flags |= VFS_OPEN_APPEND;
    int status;
    int fd = task_fd_open(path, native_flags, &status);
    if (fd == -2) return result_error(NARC_TOO_MANY_HANDLES);
    if (fd < 0) return result_error(vfs_error(status));
    return result_ok((uint64_t)fd);
}

static struct kernel_result handle_mkdir(uint64_t address, uint64_t length, uint64_t mode) {
    char path[VFS_PATH_MAX];
    uint32_t status = copy_path(path, address, length);
    if (status != NARC_OK) return result_error(status);
    enum vfs_status result = vfs_mkdir(path, (uint32_t)mode);
    return result == VFS_OK ? result_ok(0) : result_error(vfs_error(result));
}

static struct kernel_result handle_unlink(uint64_t address, uint64_t length,
                                          int remove_directory) {
    char path[VFS_PATH_MAX];
    uint32_t status = copy_path(path, address, length);
    if (status != NARC_OK) return result_error(status);
    enum vfs_status result = vfs_unlink(path, remove_directory);
    return result == VFS_OK ? result_ok(0) : result_error(vfs_error(result));
}

static struct kernel_result handle_rename(uint64_t old_address, uint64_t old_length,
                                          uint64_t new_address, uint64_t new_length) {
    char old_path[VFS_PATH_MAX], new_path[VFS_PATH_MAX];
    uint32_t status = copy_path(old_path, old_address, old_length);
    if (status != NARC_OK) return result_error(status);
    status = copy_path(new_path, new_address, new_length);
    if (status != NARC_OK) return result_error(status);
    enum vfs_status result = vfs_rename(old_path, new_path);
    return result == VFS_OK ? result_ok(0) : result_error(vfs_error(result));
}

static struct kernel_result handle_close(uint64_t fd) {
    if (task_fd_close((int)fd) != 0) return result_error(NARC_BAD_HANDLE);
    return result_ok(0);
}

static struct kernel_result handle_seek(uint64_t fd, int64_t offset, uint64_t origin) {
    if (origin > NARC_SEEK_END) return result_error(NARC_INVALID_ARGUMENT);
    struct file *file = task_fd_file((int)fd);
    if (!file) return result_error(NARC_BAD_HANDLE);
    long value = vfs_seek(file, offset, (int)origin);
    if (value < 0) return result_error(NARC_INVALID_ARGUMENT);
    return result_ok((uint64_t)value);
}

static struct kernel_result handle_file_info(uint64_t fd, uint64_t address) {
    if (!task_fd_valid((int)fd)) return result_error(NARC_BAD_HANDLE);

    narc_file_info_t info = { 0 };
    struct file *file = task_fd_file((int)fd);
    if (file) {
        struct vfs_info source;
        vfs_file_info(file, &source);
        info.inode = source.ino;
        info.size = source.size;
        info.mode = source.mode;
        info.type = source.type == VFS_DIR ? NARC_FILE_DIRECTORY : NARC_FILE_REGULAR;
    } else {
        info.mode = 0020000 | 0666;
        info.type = NARC_FILE_CHARACTER;
    }
    if (copy_to_user(address, &info, sizeof(info)) != 0)
        return result_error(NARC_BAD_ADDRESS);
    return result_ok(0);
}

static struct kernel_result handle_read_dir(uint64_t fd, uint64_t address) {
    struct file *file = task_fd_file((int)fd);
    if (!file) return task_fd_valid((int)fd) ? result_error(NARC_NOT_DIRECTORY) :
                                              result_error(NARC_BAD_HANDLE);
    struct vfs_info info;
    vfs_file_info(file, &info);
    if (info.type != VFS_DIR) return result_error(NARC_NOT_DIRECTORY);

    struct vfs_dirent source;
    int result = vfs_readdir(file, &source);
    if (result < 0) return result_error(NARC_IO_ERROR);
    if (!result) return result_ok(0);

    narc_dir_entry_t entry = { 0 };
    entry.inode = source.ino;
    entry.type = source.type == VFS_DIR ? NARC_FILE_DIRECTORY : NARC_FILE_REGULAR;
    for (size_t i = 0; i < sizeof(entry.name); i++) entry.name[i] = source.name[i];
    if (copy_to_user(address, &entry, sizeof(entry)) != 0)
        return result_error(NARC_BAD_ADDRESS);
    return result_ok(1);
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
    if (!size || base < USER_MMAP_BASE || size > USER_MMAP_END - base)
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
    if (!size || address < USER_MMAP_BASE || size > USER_MMAP_END - address)
        return result_error(NARC_INVALID_ARGUMENT);

    struct address_space *space = vmm_space_current();
    for (uint64_t off = 0; off < size; off += PAGE_SIZE)
        if (!vmm_user_phys(space, address + off))
            return result_error(NARC_INVALID_ARGUMENT);
    for (uint64_t off = 0; off < size; off += PAGE_SIZE)
        vmm_unmap_user(space, address + off);
    return result_ok(0);
}

static uint32_t handle_exec(struct task_frame *frame, uint64_t path_address,
                            uint64_t argv_address, uint64_t envp_address) {
    char path[VFS_PATH_MAX];
    char argument_storage[16][VFS_PATH_MAX];
    char environment_storage[16][VFS_PATH_MAX];
    const char *arguments[16];
    const char *environment[16];
    size_t argument_count, environment_count;
    if (copy_string(path, path_address, sizeof(path)) != 0 ||
        copy_string_list(argv_address, argument_storage, arguments, &argument_count) != 0 ||
        copy_string_list(envp_address, environment_storage, environment,
                         &environment_count) != 0)
        return NARC_BAD_ADDRESS;
    if (!argument_count) {
        arguments[0] = path;
        argument_count = 1;
    }
    return user_exec(frame, path, arguments, argument_count,
                     environment, environment_count) == 0 ? NARC_OK : NARC_NOT_FOUND;
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
        task_exit(frame, (int)a1);
        return;
    case NARC_SYS_GETPID:
        return_result(frame, result_ok((uint64_t)task_pid()));
        return;
    case NARC_SYS_YIELD:
        return_result(frame, result_ok(0));
        task_yield(frame);
        return;
    case NARC_SYS_FORK: {
        int pid = task_fork(frame);
        return_result(frame, pid < 0 ? result_error(NARC_TRY_AGAIN) : result_ok((uint64_t)pid));
        return;
    }
    case NARC_SYS_WAIT: {
        if (a2 && !vmm_user_range_ok(vmm_space_current(), a2, sizeof(int), 1)) {
            return_result(frame, result_error(NARC_BAD_ADDRESS));
            return;
        }
        long value;
        if (task_wait(frame, (int)a1, a2, (int)a3, &value)) return;
        return_result(frame, value < 0 ? result_error(NARC_NO_CHILD) :
                                        result_ok((uint64_t)value));
        return;
    }
    case NARC_SYS_EXEC: {
        uint32_t status = handle_exec(frame, a1, a2, a3);
        if (status != NARC_OK) return_result(frame, result_error(status));
        return;
    }
    case NARC_SYS_OPEN:
        return_result(frame, handle_open(a1, a2, a3));
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
    case NARC_SYS_MAP:
        return_result(frame, handle_map(a1, a2));
        return;
    case NARC_SYS_UNMAP:
        return_result(frame, handle_unmap(a1, a2));
        return;
    default:
        return_result(frame, result_error(NARC_NOT_SUPPORTED));
        return;
    }
}

void syscall_dispatch(struct task_frame *frame) {
    uint64_t number = arch_syscall_number(frame);
    if ((number & NARC_SYSCALL_TAG_MASK) != NARC_SYSCALL_TAG) {
        return_result(frame, result_error(NARC_NOT_SUPPORTED));
        return;
    }
    dispatch(frame, number & NARC_SYSCALL_ID_MASK);
}
