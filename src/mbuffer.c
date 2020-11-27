
#include <stdlib.h>

#include "mbuffer.h"

// initalize a managed buffer
void m_init(struct managedBuffer* buffer, size_t typesize)
{
    buffer->b = NULL;
    buffer->size = 0;
    buffer->typesize = typesize;
}

// free a managed buffer
void m_free(struct managedBuffer* buffer)
{
    free(buffer->b);
    buffer->b = NULL;
    buffer->size = 0;
}

// reallocate the managed buffer to the given size
int m_realloc(struct managedBuffer* buffer, size_t size)
{
    void* temp;
    size_t nextsize;

    nextsize = size;
    temp = realloc(buffer->b, nextsize * buffer->typesize);
    if (temp == NULL && nextsize != 0) {
        return -1;
    }
    buffer->b = temp;
    buffer->size = nextsize;
    return 0;
}
