#include <kernel/arch.h>
#include <kernel/console.h>
#include <kernel/heap.h>
#include <kernel/mm.h>
#include <kernel/string.h>
#include <kernel/sched.h>
#include <kernel/task.h>
#include <kernel/uaccess.h>
#include <kernel/user.h>
#include <kernel/vfs.h>
#include <stddef.h>
#include <stdint.h>

#define USER_MIN           0x0000000000400000ULL
#define USER_STACK_TOP     0x0000000080000000ULL
#define USER_STACK_RESERVE 0x0000000010000000ULL
#define USER_STACK_SIZE    (64ULL * 1024ULL)
#define USER_IMAGE_END     (USER_STACK_TOP - USER_STACK_RESERVE)
#define EI_NIDENT 16
#define ET_EXEC 2
#define PT_LOAD 1
#define PT_PHDR 6
#define PF_W 2
#define AT_NULL 0
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define AT_PAGESZ 6
#define AT_ENTRY 9
#define AT_RANDOM 25
#define AT_EXECFN 31

struct __attribute__((packed)) elf64_ehdr {
    unsigned char ident[EI_NIDENT];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, phoff, shoff;
    uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};

struct __attribute__((packed)) elf64_phdr {
    uint32_t type, flags;
    uint64_t offset, vaddr, paddr, filesz, memsz, align;
};

static uint64_t align_down(uint64_t x) { return x & ~(PAGE_SIZE - 1); }
static uint64_t align_up(uint64_t x) { return (x + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); }

static int map_range(struct address_space *space, uint64_t start, uint64_t end) {
    for (uint64_t va = align_down(start); va < align_up(end); va += PAGE_SIZE) {
        if (vmm_user_phys(space, va)) continue;
        uint64_t phys = pmm_alloc_page();
        if (!phys) return -1;
        if (vmm_map_user(space, va, phys, VMM_WRITE) != 0) {
            pmm_free_page(phys);
            return -1;
        }
    }
    return 0;
}

static uint64_t find_phdr(const struct elf64_ehdr *eh, const struct elf64_phdr *ph) {
    for (uint16_t i = 0; i < eh->phnum; i++) if (ph[i].type == PT_PHDR) return ph[i].vaddr;
    uint64_t bytes = (uint64_t)eh->phnum * eh->phentsize;
    for (uint16_t i = 0; i < eh->phnum; i++) {
        if (ph[i].type == PT_LOAD && eh->phoff >= ph[i].offset &&
            eh->phoff + bytes <= ph[i].offset + ph[i].filesz)
            return ph[i].vaddr + eh->phoff - ph[i].offset;
    }
    return 0;
}

struct strings {
    struct address_space *space;
    uint64_t list;
    size_t count;
    uint64_t bytes;
};

static int string_address(const struct strings *strings, size_t index, uint64_t *address) {
    if (!strings->list) {
        *address = 0;
        return 0;
    }
    if (!strings->space) {
        *address = (uint64_t)(uintptr_t)((const char *const *)(uintptr_t)strings->list)[index];
        return 0;
    }
    uint64_t slot = strings->list + index * sizeof(uint64_t);
    if (!vmm_user_range_ok(strings->space, slot, sizeof(uint64_t), 0)) return -1;
    return uaccess_read(strings->space, address, slot, sizeof(uint64_t));
}

static long string_length(const struct strings *strings, uint64_t address) {
    if (strings->space) return uaccess_strlen(strings->space, address);
    long length = 0;
    while (((const char *)(uintptr_t)address)[length]) length++;
    return length;
}

static int measure(struct strings *strings) {
    strings->count = 0;
    strings->bytes = 0;
    for (;;) {
        uint64_t address;
        if (string_address(strings, strings->count, &address) != 0) return -1;
        if (!address) return 0;
        long length = string_length(strings, address);
        if (length < 0) return -1;
        strings->bytes += (uint64_t)length + 1;
        strings->count++;
        if (strings->bytes > USER_STACK_RESERVE) return -1;
    }
}

static int copy_string(struct address_space *space, uint64_t dst,
                       const struct strings *strings, uint64_t src, uint64_t length) {
    if (!strings->space)
        return uaccess_write(space, dst, (const void *)(uintptr_t)src, (size_t)length);
    uint8_t buffer[256];
    while (length) {
        size_t chunk = length > sizeof(buffer) ? sizeof(buffer) : (size_t)length;
        if (uaccess_read(strings->space, buffer, src, chunk) != 0 ||
            uaccess_write(space, dst, buffer, chunk) != 0)
            return -1;
        src += chunk;
        dst += chunk;
        length -= chunk;
    }
    return 0;
}

static int push_strings(struct address_space *space, const struct strings *strings,
                        uint64_t *cursor, uint64_t end, uint64_t *words, size_t *n) {
    for (size_t i = 0; i < strings->count; i++) {
        uint64_t address;
        if (string_address(strings, i, &address) != 0 || !address) return -1;
        long length = string_length(strings, address);
        if (length < 0 || (uint64_t)length + 1 > end - *cursor) return -1;
        if (copy_string(space, *cursor, strings, address, (uint64_t)length) != 0 ||
            uaccess_zero(space, *cursor + (uint64_t)length, 1) != 0)
            return -1;
        words[(*n)++] = *cursor;
        *cursor += (uint64_t)length + 1;
    }
    words[(*n)++] = 0;
    return 0;
}

static int build_stack(struct address_space *space, const struct elf64_ehdr *eh,
                       uint64_t phdr, const struct strings *argv,
                       const struct strings *envp, uint64_t *result) {
    static const uint8_t random[16] = {
        0x6e, 0x61, 0x72, 0x63, 0x2d, 0x72, 0x61, 0x6e,
        0x64, 0x6f, 0x6d, 0x2d, 0x73, 0x65, 0x65, 0x64,
    };
    uint64_t strings = sizeof(random) + argv->bytes + envp->bytes;
    size_t word_count = 1 + argv->count + 1 + envp->count + 1 + 16;
    uint64_t need = strings + word_count * sizeof(uint64_t) + 32;
    if (need > USER_STACK_RESERVE - USER_STACK_SIZE) return -1;
    uint64_t size = align_up(need) + USER_STACK_SIZE;
    if (map_range(space, USER_STACK_TOP - size, USER_STACK_TOP) != 0) return -1;

    uint64_t *words = kmalloc(word_count * sizeof(uint64_t));
    if (!words) return -1;
    uint64_t cursor = USER_STACK_TOP - strings, randomp = cursor;
    size_t n = 0;
    words[n++] = argv->count;
    int status = uaccess_write(space, randomp, random, sizeof(random));
    cursor += sizeof(random);
    if (status == 0) status = push_strings(space, argv, &cursor, USER_STACK_TOP, words, &n);
    uint64_t execfn = words[1];
    if (status == 0) status = push_strings(space, envp, &cursor, USER_STACK_TOP, words, &n);
    if (status == 0) {
        words[n++] = AT_PHDR; words[n++] = phdr;
        words[n++] = AT_PHENT; words[n++] = eh->phentsize;
        words[n++] = AT_PHNUM; words[n++] = eh->phnum;
        words[n++] = AT_PAGESZ; words[n++] = PAGE_SIZE;
        words[n++] = AT_ENTRY; words[n++] = eh->entry;
        words[n++] = AT_RANDOM; words[n++] = randomp;
        words[n++] = AT_EXECFN; words[n++] = execfn;
        words[n++] = AT_NULL; words[n++] = 0;
        uint64_t sp = ((USER_STACK_TOP - strings) & ~0xfULL) - n * sizeof(uint64_t);
        sp &= ~0xfULL;
        status = uaccess_write(space, sp, words, n * sizeof(uint64_t));
        *result = sp;
    }
    kfree(words);
    return status;
}

static int read_at(struct file *file, uint64_t offset, void *buffer, size_t length) {
    if (vfs_seek(file, (int64_t)offset, 0) < 0) return -1;
    return vfs_read(file, buffer, length) == (long)length ? 0 : -1;
}

static int load_segment(struct address_space *space, struct file *file,
                        const struct elf64_phdr *ph) {
    if (map_range(space, ph->vaddr, ph->vaddr + ph->memsz) != 0 ||
        vfs_seek(file, (int64_t)ph->offset, 0) < 0)
        return -1;
    uint64_t dst = ph->vaddr, left = ph->filesz;
    while (left) {
        uint64_t phys = vmm_user_phys(space, dst);
        if (!phys) return -1;
        size_t chunk = PAGE_SIZE - (size_t)(dst & (PAGE_SIZE - 1));
        if (chunk > left) chunk = (size_t)left;
        if (vfs_read(file, phys_to_virt(phys), chunk) != (long)chunk) return -1;
        dst += chunk;
        left -= chunk;
    }
    return uaccess_zero(space, ph->vaddr + ph->filesz, (size_t)(ph->memsz - ph->filesz));
}

static int load_image(struct address_space *space, struct file *file, uint64_t size,
                      struct elf64_ehdr *eh, const struct elf64_phdr *ph, uint64_t *phdr) {
    *phdr = find_phdr(eh, ph);
    if (!*phdr) return -1;
    for (uint16_t i = 0; i < eh->phnum; i++) {
        if (ph[i].type != PT_LOAD) continue;
        if (ph[i].filesz > ph[i].memsz || ph[i].offset > size ||
            ph[i].filesz > size - ph[i].offset || ph[i].vaddr < USER_MIN ||
            ph[i].vaddr >= USER_IMAGE_END || ph[i].memsz > USER_IMAGE_END - ph[i].vaddr)
            return -1;
        if (ph[i].memsz && load_segment(space, file, &ph[i]) != 0) return -1;
    }
    for (uint16_t i = 0; i < eh->phnum; i++) {
        if (ph[i].type != PT_LOAD || !ph[i].memsz) continue;
        for (uint64_t va = align_down(ph[i].vaddr);
             va < align_up(ph[i].vaddr + ph[i].memsz); va += PAGE_SIZE)
            if (vmm_protect_user(space, va, (ph[i].flags & PF_W) ? VMM_WRITE : 0) != 0) return -1;
    }
    return 0;
}

static int load(struct address_space *space, const char *path, const struct strings *argv,
                const struct strings *envp, uint64_t *entry, uint64_t *stack) {
    struct file file;
    if (vfs_open(path, VFS_OPEN_READ, &file) != VFS_OK) return -1;
    struct vfs_info info;
    vfs_file_info(&file, &info);
    struct elf64_ehdr eh;
    struct elf64_phdr *ph = 0;
    uint64_t phdr;
    int status = -1;
    if (info.type != VFS_REG || info.size < sizeof(eh) || read_at(&file, 0, &eh, sizeof(eh)) != 0)
        goto done;
    if (eh.ident[0] != 0x7f || eh.ident[1] != 'E' || eh.ident[2] != 'L' ||
        eh.ident[3] != 'F' || eh.ident[4] != 2 || eh.ident[5] != 1 ||
        eh.type != ET_EXEC || eh.machine != ARCH_ELF_MACHINE ||
        eh.phentsize != sizeof(struct elf64_phdr) || !eh.phnum)
        goto done;
    uint64_t ph_bytes = (uint64_t)eh.phnum * eh.phentsize;
    if (eh.phoff > info.size || ph_bytes > info.size - eh.phoff) goto done;
    ph = kmalloc((size_t)ph_bytes);
    if (!ph || read_at(&file, eh.phoff, ph, (size_t)ph_bytes) != 0 ||
        load_image(space, &file, info.size, &eh, ph, &phdr) != 0 ||
        build_stack(space, &eh, phdr, argv, envp, stack) != 0)
        goto done;
    *entry = eh.entry;
    status = 0;
done:
    kfree(ph);
    vfs_close(&file);
    return status;
}

int user_exec(struct task_frame *frame, const char *path, uint64_t argv, uint64_t envp) {
    struct address_space *current = vmm_space_current();
    const char *fallback[] = { path, 0 };
    struct strings arguments = { current, argv, 0, 0 };
    struct strings environment = { current, envp, 0, 0 };
    if (measure(&arguments) != 0 || measure(&environment) != 0) return -1;
    if (!arguments.count) {
        arguments = (struct strings) { 0, (uint64_t)(uintptr_t)fallback, 0, 0 };
        if (measure(&arguments) != 0) return -1;
    }
    struct address_space space;
    uint64_t entry, stack;
    if (vmm_space_create(&space) != 0) return -1;
    if (load(&space, path, &arguments, &environment, &entry, &stack) != 0) {
        vmm_space_destroy(&space);
        return -1;
    }
    task_exec(frame, &space, entry, stack);
    return 0;
}

void user_start(void) {
    static const char *const argv[] = { "/sbin/init", 0 };
    struct strings arguments = { 0, (uint64_t)(uintptr_t)argv, 0, 0 };
    struct strings environment = { 0, 0, 0, 0 };
    struct task *task = task_create();
    uint64_t entry, stack;
    if (!task || measure(&arguments) != 0 ||
        load(&task->space, argv[0], &arguments, &environment, &entry, &stack) != 0) {
        console_puts("[panic] cannot start init\n");
        arch_halt();
    }
    task_set_entry(task, entry, stack);
    console_puts("[user] entering ring 3\n");
    sched_start(task);
}
