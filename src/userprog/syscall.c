#include <stdio.h>
#include <syscall-nr.h>
#include "syscall.h"
#include "pagedir.h"
#include "process.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "lib/kernel/stdio.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/page.h"

struct lock filesys_lock;

static void syscall_handler(struct intr_frame *);

struct vm_entry *check_vaddr(const void *);
void check_valid_buffer(void *buffer, unsigned size, void *esp, bool to_write);
void check_valid_string(const void *str, void *esp);

static void syscall_halt(void);
static pid_t syscall_exec(const char *);
static int syscall_wait(pid_t);
static bool syscall_create(const char *, unsigned);
static bool syscall_remove(const char *);
static int syscall_open(const char *);
static int syscall_filesize(int);
static int syscall_read(int, void *, unsigned);
static int syscall_write(int, const void *, unsigned);
static void syscall_seek(int, unsigned);
static unsigned syscall_tell(int);

/* Registers the system call interrupt handler. */
void syscall_init(void)
{
    intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
    lock_init(&filesys_lock);
}

/* Pops the system call number and handles system call
   according to it. */
static void
syscall_handler(struct intr_frame *f)
{
    void *esp = f->esp;
    int syscall_num;

    check_vaddr(esp);
    check_vaddr(esp + sizeof(uintptr_t) - 1);
    syscall_num = *(int *)esp;

    switch (syscall_num)
    {
    case SYS_HALT:
    {
        syscall_halt();
        NOT_REACHED();
    }
    case SYS_EXIT:
    {
        int status;

        check_vaddr(esp + sizeof(uintptr_t));
        check_vaddr(esp + 2 * sizeof(uintptr_t) - 1);
        status = *(int *)(esp + sizeof(uintptr_t));

        syscall_exit(status);
        NOT_REACHED();
    }
    case SYS_EXEC:
    {
        char *cmd_line;

        check_vaddr(esp + sizeof(uintptr_t));
        check_vaddr(esp + 2 * sizeof(uintptr_t) - 1);
        cmd_line = *(char **)(esp + sizeof(uintptr_t));

        if (!check_vaddr(cmd_line))
            syscall_exit(-1);

        f->eax = (uint32_t)syscall_exec(cmd_line);
        break;
    }
    case SYS_WAIT:
    {
        pid_t pid;

        check_vaddr(esp + sizeof(uintptr_t));
        check_vaddr(esp + 2 * sizeof(uintptr_t) - 1);
        pid = *(pid_t *)(esp + sizeof(uintptr_t));

        f->eax = (uint32_t)syscall_wait(pid);
        break;
    }
    case SYS_CREATE:
    {
        char *file;
        unsigned initial_size;

        check_vaddr(esp + sizeof(uintptr_t));
        check_vaddr(esp + 3 * sizeof(uintptr_t) - 1);
        file = *(char **)(esp + sizeof(uintptr_t));
        initial_size = *(unsigned *)(esp + 2 * sizeof(uintptr_t));

        f->eax = (uint32_t)syscall_create(file, initial_size);
        break;
    }
    case SYS_REMOVE:
    {
        char *file;

        check_vaddr(esp + sizeof(uintptr_t));
        check_vaddr(esp + 2 * sizeof(uintptr_t) - 1);
        file = *(char **)(esp + sizeof(uintptr_t));

        f->eax = (uint32_t)syscall_remove(file);
        break;
    }
    case SYS_OPEN:
    {
        char *file;

        check_vaddr(esp + sizeof(uintptr_t));
        check_vaddr(esp + 2 * sizeof(uintptr_t) - 1);
        file = *(char **)(esp + sizeof(uintptr_t));

        if (!check_vaddr(file))
            syscall_exit(-1);

        f->eax = (uint32_t)syscall_open(file);
        break;
    }
    case SYS_FILESIZE:
    {
        int fd;

        check_vaddr(esp + sizeof(uintptr_t));
        check_vaddr(esp + 2 * sizeof(uintptr_t) - 1);
        fd = *(int *)(esp + sizeof(uintptr_t));

        f->eax = (uint32_t)syscall_filesize(fd);
        break;
    }
    case SYS_READ:
    {
        int fd;
        void *buffer;
        unsigned size;

        fd = *(int *)(esp + sizeof(uintptr_t));
        buffer = *(void **)(esp + 2 * sizeof(uintptr_t));
        size = *(unsigned *)(esp + 3 * sizeof(uintptr_t));

        //check_vaddr(esp + sizeof(uintptr_t));
        check_valid_buffer(buffer, size, esp + sizeof(uintptr_t), false);
        //check_vaddr(esp + 4 * sizeof(uintptr_t) - 1);
        check_valid_buffer(buffer, size, esp + 4 * sizeof(uintptr_t) - 1, true);

        f->eax = (uint32_t)syscall_read(fd, buffer, size);
        break;
    }
    case SYS_WRITE:
    {
        int fd;
        void *buffer;
        unsigned size;

        //check_vaddr(esp + sizeof(uintptr_t));
        //check_vaddr(esp + 4 * sizeof(uintptr_t) - 1);
        fd = *(int *)(esp + sizeof(uintptr_t));
        buffer = *(void **)(esp + 2 * sizeof(uintptr_t));
        size = *(unsigned *)(esp + 3 * sizeof(uintptr_t));

        f->eax = (uint32_t)syscall_write(fd, buffer, size);
        break;
    }
    case SYS_SEEK:
    {
        int fd;
        unsigned position;

        check_vaddr(esp + sizeof(uintptr_t));
        check_vaddr(esp + 3 * sizeof(uintptr_t) - 1);
        fd = *(int *)(esp + sizeof(uintptr_t));
        position = *(unsigned *)(esp + 2 * sizeof(uintptr_t));

        syscall_seek(fd, position);
        break;
    }
    case SYS_TELL:
    {
        int fd;

        check_vaddr(esp + sizeof(uintptr_t));
        check_vaddr(esp + 2 * sizeof(uintptr_t) - 1);
        fd = *(int *)(esp + sizeof(uintptr_t));

        f->eax = (uint32_t)syscall_tell(fd);
        break;
    }
    case SYS_CLOSE:
    {
        int fd;

        check_vaddr(esp + sizeof(uintptr_t));
        check_vaddr(esp + 2 * sizeof(uintptr_t) - 1);
        fd = *(int *)(esp + sizeof(uintptr_t));

        syscall_close(fd);
        break;
    }
    case SYS_MMAP:
    {
		int fd = *(int *)(esp + sizeof(uintptr_t));
        void* addr = *(void **)(esp + 2 * sizeof(uintptr_t));
		f->eax = mmap(fd, addr);
		break;
    }
	case SYS_MUNMAP:
    {
        int map = *(int *)(esp + sizeof(uintptr_t));
		munmap(map);
		break;
    }
    default:
        syscall_exit(-1);
    }
}

/* Checks user-provided virtual address. If it is
   invalid, terminates the current process. */
struct vm_entry *
check_vaddr(const void *vaddr)
{
    if (!vaddr || !is_user_vaddr(vaddr) ||
        !pagedir_get_page(thread_get_pagedir(), vaddr))
        syscall_exit(-1);
    struct vm_entry *vme = find_vme(vaddr);
    return vme;
}

void check_valid_buffer(void *buffer, unsigned size, void *esp, bool to_write)
{
    int i;
    for (i = 0; i < size; i++)
    {
        struct vm_entry *vme = check_vaddr(buffer + i);
        if (vme == NULL || (to_write == true && !(vme->writable)))
        {
            syscall_exit(-1);
        }
    }
}

void check_valid_string(const void *str, void *esp)
{
    struct vm_entry *vme = check_vaddr(str);
    if (vme == NULL)
        syscall_exit(-1);
}

struct lock *syscall_get_filesys_lock(void)
{
    return &filesys_lock;
}

/* Handles halt() system call. */
static void syscall_halt(void)
{
    shutdown_power_off();
}

/* Handles exit() system call. */
void syscall_exit(int status)
{
    struct process *pcb = thread_get_pcb();

    pcb->exit_status = status;
    printf("%s: exit(%d)\n", thread_name(), status);
    thread_exit();
}

/* Handles exec() system call. */
static pid_t syscall_exec(const char *cmd_line)
{
    pid_t pid;
    struct process *child;
    int i;

    check_vaddr(cmd_line);
    for (i = 0; *(cmd_line + i); i++)
        check_vaddr(cmd_line + i + 1);

    pid = process_execute(cmd_line);
    child = process_get_child(pid);

    if (!child || !child->is_loaded)
        return PID_ERROR;

    return pid;
}

/* Handles wait() system call. */
static int syscall_wait(pid_t pid)
{
    return process_wait(pid);
}

/* Handles create() system call. */
static bool syscall_create(const char *file, unsigned initial_size)
{
    bool success;
    int i;

    check_vaddr(file);
    for (i = 0; *(file + i); i++)
        check_vaddr(file + i + 1);

    lock_acquire(&filesys_lock);
    success = filesys_create(file, (off_t)initial_size);
    lock_release(&filesys_lock);

    return success;
}

/* Handles remove() system call. */
static bool syscall_remove(const char *file)
{
    bool success;
    int i;

    check_vaddr(file);
    for (i = 0; *(file + i); i++)
        check_vaddr(file + i + 1);

    lock_acquire(&filesys_lock);
    success = filesys_remove(file);
    lock_release(&filesys_lock);

    return success;
}

/* Handles open() system call. */
static int syscall_open(const char *file)
{
    struct file_descriptor_entry *fde;
    struct file *new_file;
    int i;

    check_vaddr(file);
    for (i = 0; *(file + i); i++)
        check_vaddr(file + i + 1);

    fde = palloc_get_page(0);
    if (!fde)
        return -1;

    lock_acquire(&filesys_lock);

    new_file = filesys_open(file);
    if (!new_file)
    {
        palloc_free_page(fde);
        lock_release(&filesys_lock);

        return -1;
    }

    fde->fd = thread_get_next_fd();
    fde->file = new_file;
    list_push_back(thread_get_fdt(), &fde->fdtelem);

    lock_release(&filesys_lock);

    return fde->fd;
}

/* Handles filesize() system call. */
static int syscall_filesize(int fd)
{
    struct file_descriptor_entry *fde = process_get_fde(fd);
    int filesize;

    if (!fde)
        return -1;

    lock_acquire(&filesys_lock);
    filesize = file_length(fde->file);
    lock_release(&filesys_lock);

    return filesize;
}

/* Handles read() system call. */
static int syscall_read(int fd, void *buffer, unsigned size)
{
    struct file_descriptor_entry *fde;
    int bytes_read, i;

    for (i = 0; i < size; i++)
        check_vaddr(buffer + i);

    if (fd == 0)
    {
        unsigned i;

        for (i = 0; i < size; i++)
            *(uint8_t *)(buffer + i) = input_getc();

        return size;
    }

    fde = process_get_fde(fd);
    if (!fde)
        return -1;

    lock_acquire(&filesys_lock);
    bytes_read = (int)file_read(fde->file, buffer, (off_t)size);
    lock_release(&filesys_lock);

    return bytes_read;
}

/* Handles write() system call. */
static int syscall_write(int fd, const void *buffer, unsigned size)
{
    struct file_descriptor_entry *fde;
    int bytes_written, i;

    for (i = 0; i < size; i++)
        check_vaddr(buffer + i);

    if (fd == 1)
    {
        putbuf((const char *)buffer, (size_t)size);

        return size;
    }

    fde = process_get_fde(fd);
    if (!fde)
        return -1;

    lock_acquire(&filesys_lock);
    bytes_written = (int)file_write(fde->file, buffer, (off_t)size);
    lock_release(&filesys_lock);

    return bytes_written;
}

/* Handles seek() system call. */
static void syscall_seek(int fd, unsigned position)
{
    struct file_descriptor_entry *fde = process_get_fde(fd);

    if (!fde)
        return;

    lock_acquire(&filesys_lock);
    file_seek(fde->file, (off_t)position);
    lock_release(&filesys_lock);
}

/* Handles tell() system call. */
static unsigned syscall_tell(int fd)
{
    struct file_descriptor_entry *fde = process_get_fde(fd);
    unsigned pos;

    if (!fde)
        return -1;

    lock_acquire(&filesys_lock);
    pos = (unsigned)file_tell(fde->file);
    lock_release(&filesys_lock);

    return pos;
}

/* Handles close() system call. */
void syscall_close(int fd)
{
    struct file_descriptor_entry *fde = process_get_fde(fd);

    if (!fde)
        return;

    lock_acquire(&filesys_lock);
    file_close(fde->file);
    list_remove(&fde->fdtelem);
    palloc_free_page(fde);
    lock_release(&filesys_lock);
}

int mmap(int fd, void* addr){
    struct mmap_file* map_entry = malloc(sizeof(struct mmap_file));
    if((!addr) || ((int)addr&PGSIZE) != 0 || (!map_entry)){
        return -1;
    }

    struct file* map_file = file_reopen(process_get_fde(fd));
    if(!map_file){
        return -1;
    }

    list_init(&map_entry->vme_list);
    thread_current()->map_id++;
    map_entry->mapid = thread_current()->map_id;
    map_entry->file = map_file;

    void* vaddr = addr;
    struct vm_entry* vme;
    int offset = 0;

    int k = file_length(map_file)/PGSIZE;
    for(int i = 0; i < k; i++){
        vme = malloc(sizeof(struct vm_entry));
        if(!vme){
            return -1;
        }

        vme->type = VM_FILE;
		vme->vaddr = vaddr;
		vme->writable = true;
		vme->is_loaded = false;
		vme->file = map_file;
		vme->offset = offset;
		vme->read_bytes = PGSIZE;
		vme->zero_bytes = 0;
		vme->addi = false;

        bool check = insert_vme(&thread_current()->vm, vme);
        if(!check){
            return -1;
        }
        list_push_back(&(map_entry->vme_list), &(vme->mmap_elem));
        vaddr += PGSIZE;
        offset += PGSIZE;
    }

    vme = malloc(sizeof(struct vm_entry));
    if(!vme){
        return -1;
    }
    k = file_length(map_file) % PGSIZE;
    vme->type = VM_FILE;
	vme->vaddr = vaddr;
	vme->writable = true;
	vme->is_loaded = false;
	vme->file = map_file;
	vme->offset = offset;
	vme->read_bytes = k;
	vme->zero_bytes = PGSIZE - k;;
	vme->addi = false;

    bool check = insert_vme(&thread_current()->vm, vme);
    if(!check){
        return -1;
    }
    list_push_back(&(map_entry->vme_list), &(vme->mmap_elem));
    vaddr += PGSIZE;

    list_push_back(&thread_current()->mappingList, &map_entry->elem);
    return thread_current()->map_id;
}

void munmap(int mapping){
    struct mmap_file* file;
    for(struct list_elem* elem = list_begin(&thread_current()->mappingList);
            elem != list_end(&thread_current()->mappingList); elem = list_next(elem)){
        file = list_entry(elem, struct mmap_file, elem);

        if(file->mapid == mapping){
            do_munmap(file);
            file_close(file->file);
            struct list_elem* temp = list_prev(elem);
            list_remove(elem);
            elem = temp;
            free(file);
            break;
        }
    }
}

void do_munmap(struct mmap_file* mmap_file){
    void* paddr;
    struct vm_entry* vme;
    struct list_elem* temp;
    int* pagedir = thread_current()->pagedir;

    for(struct list_elem* elem = list_begin(&mmap_file->vme_list);
            elem != list_end(&mmap_file->vme_list); elem = list_next(elem)){
        vme = list_entry(elem, struct vm_entry, mmap_elem);
        if(vme->is_loaded){
            paddr = pagedir_get_page(pagedir, vme->vaddr);
            if(pagedir_is_dirty(pagedir, vme->vaddr)){
                file_write_at(vme->file, vme->vaddr, vme->read_bytes, vme->offset);
            }
            pagedir_clear_page(pagedir, vme->vaddr);
            free_page(paddr);
        }
        delete_vme(&thread_current()->vm, vme);

        temp = list_prev(elem);
        list_remove(elem);
        elem = temp;
    }
}