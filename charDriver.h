/*
 * charDriver.h
 *
 *  Created on: Oct 8, 2015
 *      Author: bullshark
 */

#ifndef CHARDRIVER_H_
#define CHARDRIVER_H_

#include <linux/kernel.h>
#include <asm/atomic.h>
#include <linux/types.h>

int charDriverMajor;
int charDriverMinor;
int numDevices;
struct class *charDriverClass;

// Driver internal management struct
struct charDriverDev {
  // Read & Write intermediate buffers
  char *readBuffer;
  char *writeBuffer;
  struct semaphore bufferSem;

  // User count
  struct semaphore countSem;
  atomic_t numWriter;
  unsigned short numReader;

  // Circular buffer
  BufferHandle_t cirBuffer;
  wait_queue_head_t readWq;
  wait_queue_head_t writeWq;

  // Linux module
  struct device* device;
  struct class* class;
  dev_t dev;
  struct cdev cdev;
} charDriver;

// Prototypes
static int __init charDriver_init(void);
static void __exit charDriver_exit(void);
static int charDriver_open(struct inode *inode, struct file *filp);
static int charDriver_release(struct inode *inode, struct file *filp);
static ssize_t charDriver_read(struct file *filp, char __user *ubuf, size_t count, loff_t *f_ops);
static ssize_t charDriver_write(struct file *filp, const char __user *ubuf, size_t count, loff_t *f_ops);
static long charDriver_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

#endif /* CHARDRIVER_H_ */
