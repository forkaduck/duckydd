#ifndef MBUFFER_DUCKYDD_H
#define MBUFFER_DUCKYDD_H

#include <stdio.h>

struct managedBuffer {
        void *b;
        size_t size;
        size_t typesize;
};

void init_mbuffer ( struct managedBuffer *buffer, size_t typesize );

void free_mbuffer ( struct managedBuffer *buffer );

int realloc_mbuffer ( struct managedBuffer *buffer, size_t size );

#endif
