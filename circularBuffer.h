/*
 * File         : circularBuffer.h
 * Description  : Circular Buffer source
 *
 */

#ifndef CIRCULAR_BUFFER_H_
#define CIRCULAR_BUFFER_H_

#ifdef __KERNEL__
    #ifndef NULL
        #define NULL ((void *)0)
    #endif
    #include <linux/slab.h> // kmalloc() & kfree()
#else
    #include <stdio.h>
    #include <stdlib.h>
#endif

// Return codes
#define BUFFER_OK     0
#define BUFFER_FULL  -1
#define BUFFER_EMPTY -2
#define BUFFER_ERROR -3

// Buffer Handle type
typedef void* BufferHandle_t;

BufferHandle_t circularBufferInit(unsigned int size);
int circularBufferDelete(BufferHandle_t handle);
int circularBufferResize(BufferHandle_t handle, unsigned int newSize);
int circularBufferIn(BufferHandle_t handle, char data);
int circularBufferOut(BufferHandle_t handle, char *data);
unsigned int circularBufferDataCount(BufferHandle_t handle);
unsigned int circularBufferDataSize(BufferHandle_t handle);
unsigned int circularBufferDataRemaining(BufferHandle_t handle);


#endif /* CIRCULAR_BUFFER_H_ */
