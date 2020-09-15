
#include <stdlib.h>

#include "mbuffer.h"

void m_init(struct managedBuffer* buffer, size_t typesize)
{
    buffer->b = NULL;
    buffer->size = 0;
    buffer->typesize = typesize;
}

void m_free(struct managedBuffer* buffer)
{
    free(buffer->b);
    buffer->b = NULL;
    buffer->size = 0;
}

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
