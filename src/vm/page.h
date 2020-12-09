#ifndef VM_PAGE_H
#define VM_PAGE_H

#define VM_BIN 0
#define VM_FILE 1
#define VM_ANON 2

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include <hash.h>
#include "threads/vaddr.h"
#include "threads/palloc.h"

struct vm_entry
{
    uint8_t type;
    void *vaddr;
    bool writable;

    bool is_loaded;
    struct file *file;

    struct list_elem mmap_elem;

    size_t offset;
    size_t read_bytes;
    size_t zero_bytes;

    size_t swap_slot;

    struct hash_elem elem;
};

bool insert_vme(struct hash *vm, struct vm_entry *vme);
bool delete_vme(struct hash *vm, struct vm_entry *vme);

#endif