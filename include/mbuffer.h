#ifndef MBUFFER_DUCKYDD_H
#define MBUFFER_DUCKYDD_H

#include <stdio.h>

struct managedBuffer {
    void* b;
    size_t size;
    size_t typesize;
};

void m_init(struct managedBuffer* buffer, size_t typesize);

void m_free(struct managedBuffer* buffer);

int m_realloc(struct managedBuffer* buffer, size_t size);

#endif
