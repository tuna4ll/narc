#include <kernel/arch.h>
#include <kernel/console.h>
#include <kernel/mm.h>
#include <kernel/string.h>
#include <kernel/task.h>
#include <kernel/uaccess.h>
#include <kernel/user.h>
#include <kernel/vfs.h>
#include <stddef.h>
#include <stdint.h>

#define USER_MIN        0x0000000000400000ULL
#define USER_IMAGE_END  0x0000000010000000ULL
#define USER_STACK_TOP  0x0000000080000000ULL
#define USER_STACK_SIZE (64ULL * 1024ULL)
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

static int put_stack(struct address_space *space, uint64_t *sp,
                     const void *src, size_t len, uint64_t *address) {
    if (*sp < USER_STACK_TOP - USER_STACK_SIZE + len) return -1;
    *sp -= len;
    if (uaccess_write(space, *sp, src, len) != 0) return -1;
    *address = *sp;
    return 0;
}

static int build_stack(struct address_space *space, const struct elf64_ehdr *eh,
                       uint64_t phdr, const char *const argv[], size_t argc,
                       const char *const envp[], size_t envc, uint64_t *result) {
    static const uint8_t random[16] = {
        0x6e, 0x61, 0x72, 0x63, 0x4f, 0x73, 0x2d, 0x6c,
        0x69, 0x74, 0x74, 0x6c, 0x65, 0x2d, 0x6f, 0x73,
    };
    if (map_range(space, USER_STACK_TOP - USER_STACK_SIZE, USER_STACK_TOP) != 0) return -1;
    uint64_t sp = USER_STACK_TOP, argp[16], env_addresses[16], randomp;
    if (argc > 16 || envc > 16) return -1;
    for (size_t n = envc; n; n--) {
        size_t len = 0;
        while (envp[n - 1][len]) len++;
        if (put_stack(space, &sp, envp[n - 1], len + 1, &env_addresses[n - 1]) != 0)
            return -1;
    }
    for (size_t n = argc; n; n--) {
        size_t len = 0;
        while (argv[n - 1][len]) len++;
        if (put_stack(space, &sp, argv[n - 1], len + 1, &argp[n - 1]) != 0) return -1;
    }
    if (put_stack(space, &sp, random, sizeof(random), &randomp) != 0) return -1;
    sp &= ~0xfULL;
    uint64_t words[64];
    size_t n = 0;
    words[n++] = argc;
    for (size_t i = 0; i < argc; i++) words[n++] = argp[i];
    words[n++] = 0;
    for (size_t i = 0; i < envc; i++) words[n++] = env_addresses[i];
    words[n++] = 0;
    words[n++] = AT_PHDR; words[n++] = phdr;
    words[n++] = AT_PHENT; words[n++] = eh->phentsize;
    words[n++] = AT_PHNUM; words[n++] = eh->phnum;
    words[n++] = AT_PAGESZ; words[n++] = PAGE_SIZE;
    words[n++] = AT_ENTRY; words[n++] = eh->entry;
    words[n++] = AT_RANDOM; words[n++] = randomp;
    words[n++] = AT_EXECFN; words[n++] = argp[0];
    words[n++] = AT_NULL; words[n++] = 0;
    sp = (sp - n * sizeof(uint64_t)) & ~0xfULL;
    if (uaccess_write(space, sp, words, n * sizeof(uint64_t)) != 0) return -1;
    *result = sp;
    return 0;
}

static int load(struct address_space *space, const char *path, const char *const argv[],
                size_t argc, const char *const envp[], size_t envc,
                uint64_t *entry, uint64_t *stack) {
    struct file executable;
    if (!argc || vfs_open(path, VFS_OPEN_READ, &executable) != VFS_OK) return -1;
    struct vfs_info info;
    vfs_file_info(&executable, &info);
    if (info.type != VFS_REG || info.size > VFS_FILE_MAX) {
        vfs_close(&executable);
        return -1;
    }
    static uint8_t image[VFS_FILE_MAX];
    size_t size = (size_t)info.size;
    if (vfs_read(&executable, image, size) != (long)size) {
        vfs_close(&executable);
        return -1;
    }
    vfs_close(&executable);
    const uint8_t *blob = image;
    if (size < sizeof(struct elf64_ehdr)) return -1;
    const struct elf64_ehdr *eh = (const struct elf64_ehdr *)blob;
    if (eh->ident[0] != 0x7f || eh->ident[1] != 'E' || eh->ident[2] != 'L' ||
        eh->ident[3] != 'F' || eh->ident[4] != 2 || eh->ident[5] != 1 ||
        eh->type != ET_EXEC || eh->machine != ARCH_ELF_MACHINE ||
        eh->phentsize != sizeof(struct elf64_phdr) || !eh->phnum) return -1;
    uint64_t ph_bytes = (uint64_t)eh->phnum * eh->phentsize;
    if (eh->phoff > size || ph_bytes > size - eh->phoff) return -1;
    const struct elf64_phdr *ph = (const struct elf64_phdr *)(blob + eh->phoff);
    uint64_t phdr = find_phdr(eh, ph);
    if (!phdr) return -1;
    for (uint16_t i = 0; i < eh->phnum; i++) {
        if (ph[i].type != PT_LOAD) continue;
        if (ph[i].filesz > ph[i].memsz || ph[i].offset > size ||
            ph[i].filesz > size - ph[i].offset || ph[i].vaddr < USER_MIN ||
            ph[i].vaddr >= USER_IMAGE_END || ph[i].memsz > USER_IMAGE_END - ph[i].vaddr)
            return -1;
        if (!ph[i].memsz) continue;
        if (map_range(space, ph[i].vaddr, ph[i].vaddr + ph[i].memsz) != 0 ||
            uaccess_write(space, ph[i].vaddr, blob + ph[i].offset, (size_t)ph[i].filesz) != 0 ||
            uaccess_zero(space, ph[i].vaddr + ph[i].filesz,
                     (size_t)(ph[i].memsz - ph[i].filesz)) != 0) return -1;
    }
    for (uint16_t i = 0; i < eh->phnum; i++) {
        if (ph[i].type != PT_LOAD || !ph[i].memsz) continue;
        for (uint64_t va = align_down(ph[i].vaddr);
             va < align_up(ph[i].vaddr + ph[i].memsz); va += PAGE_SIZE)
            if (vmm_protect_user(space, va, (ph[i].flags & PF_W) ? VMM_WRITE : 0) != 0) return -1;
    }
    if (build_stack(space, eh, phdr, argv, argc, envp, envc, stack) != 0) return -1;
    *entry = eh->entry;
    return 0;
}

int user_exec(struct task_frame *frame, const char *path, const char *const argv[], size_t argc,
              const char *const envp[], size_t envc) {
    struct address_space space;
    uint64_t entry, stack;
    if (vmm_space_create(&space) != 0) return -1;
    if (load(&space, path, argv, argc, envp, envc, &entry, &stack) != 0) {
        vmm_space_destroy(&space);
        return -1;
    }
    task_exec(frame, &space, entry, stack);
    return 0;
}

void user_start(void) {
    static const char *argv[] = { "/sbin/init" };
    struct task *task = task_create();
    uint64_t entry, stack;
    if (!task || load(task_space(task), argv[0], argv, 1, 0, 0, &entry, &stack) != 0) {
        console_puts("[panic] cannot start init\n");
        arch_halt();
    }
    task_set_entry(task, entry, stack);
    console_puts("[user] entering ring 3\n");
    task_start();
}
