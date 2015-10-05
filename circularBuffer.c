/*
 * File         : circularBuffer.c
 * Description  : Circular Buffer source
 *
 */
#include "circularBuffer.h"

// Buffer handle internal structure
typedef struct {
    unsigned int   inIndex;
    unsigned int   outIndex;
    unsigned char  full;
    unsigned char  empty;
    unsigned int   size;
    char* bufferData;
} Buffer_t;


BufferHandle_t circularBufferInit(unsigned int size) {
    Buffer_t* newBuffer = NULL;
    // Allocate memory for main structure
#ifdef __KERNEL__
    newBuffer = (Buffer_t*) kmalloc(sizeof(Buffer_t), GFP_KERNEL);
#else
    newBuffer = (Buffer_t*) malloc(sizeof(Buffer_t));
#endif
    if(newBuffer != NULL) {
        // Allocate memory for internal memory
#ifdef __KERNEL__
        newBuffer->bufferData = (char*) kmalloc(sizeof(char) * size, GFP_KERNEL);
#else
        newBuffer->bufferData = (char*) malloc(sizeof(char) * size);
#endif
        if(newBuffer->bufferData != NULL) {
            // Initialize internal variables
            newBuffer->inIndex = 0;
            newBuffer->outIndex = 0;
            newBuffer->full = 0;
            newBuffer->empty = 1;
            newBuffer->size = size;
        } else {
#ifdef __KERNEL__
            kfree(newBuffer);
#else
            free(newBuffer);
#endif
            newBuffer = NULL;
        }
    }
    return (BufferHandle_t)newBuffer;
}


int circularBufferDelete(BufferHandle_t handle) {
    Buffer_t* buffer = (Buffer_t*) handle;
    int retval = BUFFER_OK;
    // Check buffer structure
    if(buffer == NULL) {
        retval = BUFFER_ERROR;
    } else {
        // Check internal data
        if(buffer->bufferData == NULL) {
            retval = BUFFER_ERROR;
        } else {
#ifdef __KERNEL__
            kfree(buffer->bufferData);
            kfree(buffer);
#else
            free(buffer->bufferData);
            free(buffer);
#endif
        }
    }
    return retval;
}


int circularBufferResize(BufferHandle_t handle, unsigned int newSize) {
    // TODO
    return BUFFER_OK;
}


int circularBufferIn(BufferHandle_t handle, char data) {
    Buffer_t* buffer = (Buffer_t*) handle;
    if(buffer->full) {
        return BUFFER_FULL;
    }
    buffer->empty = 0;
    buffer->bufferData[buffer->inIndex] = data;
    buffer->inIndex = (buffer->inIndex + 1) % buffer->size;
    if (buffer->inIndex == buffer->outIndex) {
        buffer->full = 1;
    }
    return BUFFER_OK;
}


int circularBufferOut(BufferHandle_t handle, char *data) {
    Buffer_t* buffer = (Buffer_t*) handle;
    if (buffer->empty) {
        return BUFFER_EMPTY;
    }
    buffer->full = 0;
    *data = buffer->bufferData[buffer->outIndex];
    buffer->outIndex = (buffer->outIndex + 1) % buffer->size;
    if (buffer->outIndex == buffer->inIndex) {
        buffer->empty = 1;
    }
    return BUFFER_OK;
}


unsigned int circularBufferDataCount(BufferHandle_t handle) {
    Buffer_t* buffer = (Buffer_t*) handle;
    int retval = 0;
    if(buffer->inIndex > buffer->outIndex) {
        retval = buffer->inIndex - buffer->outIndex;
    } else if(buffer->inIndex < buffer->outIndex) {
        retval = buffer->inIndex - buffer->outIndex + buffer->size;
    } else if(buffer->full) {
        retval = buffer->size;
    }
    return retval;
}
