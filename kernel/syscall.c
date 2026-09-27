#include <kernel/mm.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <kernel/vfs.h>
#include <narcos/abi.h>
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

static struct kernel_result handle_read(uint64_t fd, uint64_t buffer, uint64_t length) {
    if (!task_fd_valid((int)fd)) return result_error(NARC_BAD_HANDLE);
    struct file *file = task_fd_file((int)fd);
    if (file && file->node.type == VFS_DIR) return result_error(NARC_IS_DIRECTORY);
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
                           NARC_OPEN_CREATE | NARC_OPEN_DIRECTORY;
    if (!length || length >= VFS_PATH_MAX || (flags & ~known) ||
        !(flags & NARC_OPEN_READ))
        return result_error(NARC_INVALID_ARGUMENT);
    if (flags & (NARC_OPEN_WRITE | NARC_OPEN_CREATE)) return result_error(NARC_READ_ONLY);

    char path[VFS_PATH_MAX];
    if (copy_from_user(path, address, (size_t)length) != 0)
        return result_error(NARC_BAD_ADDRESS);
    for (uint64_t i = 0; i < length; i++)
        if (!path[i]) return result_error(NARC_INVALID_ARGUMENT);
    path[length] = 0;
    if (path[0] != '/') return result_error(NARC_NOT_FOUND);

    int fd = task_fd_open(path);
    if (fd == -1) return result_error(NARC_NOT_FOUND);
    if (fd == -2) return result_error(NARC_TOO_MANY_HANDLES);
    struct file *file = task_fd_file(fd);
    if ((flags & NARC_OPEN_DIRECTORY) && file->node.type != VFS_DIR) {
        task_fd_close(fd);
        return result_error(NARC_NOT_DIRECTORY);
    }
    return result_ok((uint64_t)fd);
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
        info.inode = file->node.ino;
        info.size = file->node.size;
        info.mode = file->node.mode;
        info.type = file->node.type == VFS_DIR ? NARC_FILE_DIRECTORY : NARC_FILE_REGULAR;
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
    if (file->node.type != VFS_DIR) return result_error(NARC_NOT_DIRECTORY);

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
