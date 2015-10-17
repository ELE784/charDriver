/*
 * File         : ele784-lab1.c
 * Description  : ELE784 Lab1 source
 *
 * Etudiants:  MASF05089000 (Francis Masse)
 *             LEBA23057609 (Alexandre Leblanc)
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <uapi/asm-generic/errno-base.h>
#include <linux/device.h>
#include <asm/uaccess.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <uapi/asm-generic/ioctl.h>
#include "circularBuffer.h"
#include "charDriver.h"

// Driver constants
#define MAJOR_NUMBER 0
#define NUMBER_OF_DEVICES 1
#define DEVICE_NAME "etsele_cdev"

// Configuration / Defines
#define READWRITE_BUFSIZE 	16
#define CIRCULAR_BUFFER_SIZE 	32
#define MAX_LENGTH 16

// Module Information
MODULE_AUTHOR("Francis Masse, Alexandre Leblanc");
MODULE_LICENSE("Dual BSD/GPL");

dev_t devNumber;

// Driver handled file operations
static struct file_operations charDriver_fops = {
    .owner = THIS_MODULE,
    .open = charDriver_open,
    .release = charDriver_release,
    .read = charDriver_read,
    .write = charDriver_write,
    .unlocked_ioctl = charDriver_ioctl,
};

static struct charDriverDev charDriver;

/*
 *  Init and Release
 */

static int __init charDriver_init(void)
{
  int result;
  //int atomicTest =  0;
  struct device *charDriverDevice;

  result = alloc_chrdev_region(&devNumber, 0, 1, DEVICE_NAME);
  if (result < 0)
    printk(KERN_WARNING"charDriver ERROR IN alloc_chrdev_region (%s:%s:%u)\n", __FILE__, __FUNCTION__, __LINE__);
  else
    printk(KERN_WARNING"charDriver : MAJOR = %u MINOR = %u\n", MAJOR(devNumber), MINOR(devNumber));

  charDriverClass = class_create(THIS_MODULE, "charDriverClass");
  charDriver.class = charDriverClass;
  if(charDriverClass == NULL)
  {
    printk(KERN_WARNING "Can't create class");
    unregister_chrdev_region(devNumber, 1);
    return -EBUSY;
  }

  charDriver.readBuffer = kmalloc(READWRITE_BUFSIZE * sizeof(char), GFP_KERNEL);
  charDriver.writeBuffer = kmalloc(READWRITE_BUFSIZE * sizeof(char), GFP_KERNEL);

  if(charDriver.readBuffer == NULL || charDriver.writeBuffer == NULL)
  {
    kfree(charDriver.readBuffer);
    kfree(charDriver.writeBuffer);
    charDriver.readBuffer = NULL;
    charDriver.writeBuffer = NULL;
    class_destroy(charDriverClass);
    unregister_chrdev_region(devNumber, 1);
  }

  init_waitqueue_head(&charDriver.readWq);
  init_waitqueue_head(&charDriver.writeWq);
  atomic_set(&charDriver.numWriter, 1);
  //atomicTest = atomic_read(&charDriver.numWriter);
  //printk(KERN_WARNING "charDriver numWriter = %d\n", atomicTest);
  charDriver.numReader = 0;
  //printk(KERN_WARNING "charDriver numReader = %d\n", charDriver.numReader);
  charDriver.dev = devNumber;
  //printk(KERN_WARNING "charDriver dev Major = %d Minor = %d\n", MAJOR(charDriver.dev), MINOR(charDriver.dev));

  charDriver.cirBuffer = circularBufferInit(CIRCULAR_BUFFER_SIZE);

  charDriverDevice = device_create(charDriverClass, NULL, devNumber, NULL, DEVICE_NAME);
  charDriver.device = charDriverDevice;

  cdev_init(&charDriver.cdev, &charDriver_fops);
  charDriver.cdev.owner = THIS_MODULE;
  charDriver.cdev.ops = &charDriver_fops;

  sema_init(&charDriver.bufferSem, 1);
  //printk(KERN_WARNING "bufferSem is initialized\n");
  sema_init(&charDriver.countSem, 1);
  //printk(KERN_WARNING "countSem is initialized\n");

  if (cdev_add(&charDriver.cdev, devNumber, 1) < 0)
    printk(KERN_WARNING"charDriver ERROR IN cdev_add (%s:%s:%u)\n", __FILE__, __FUNCTION__, __LINE__);

  printk(KERN_WARNING "====================================================\n");
  printk(KERN_WARNING "====================================================\n");
  printk(KERN_WARNING "                Device initialized\n");
  printk(KERN_WARNING "====================================================\n");
  printk(KERN_WARNING "====================================================\n");

  return 0;
}

static void __exit charDriver_exit(void)
{
  cdev_del(&charDriver.cdev);

  kfree(charDriver.readBuffer);
  kfree(charDriver.writeBuffer);
  charDriver.readBuffer = NULL;
  charDriver.writeBuffer = NULL;
  circularBufferDelete(charDriver.cirBuffer);

  device_destroy (charDriverClass, devNumber);
  class_destroy(charDriverClass);

  unregister_chrdev_region(devNumber, 1);

  printk(KERN_ALERT "charDriver have successfully exited\n");

}

/*
 *  Open and Close
 */

static int charDriver_open(struct inode *inode, struct file *filp)
{
  struct charDriverDev *dev;
  //int atomicTest =  0;

  dev = container_of(inode->i_cdev, struct charDriverDev, cdev);
  filp->private_data = dev;

  //printk(KERN_WARNING "charDriver numReader = %d\n", charDriver.numReader);

  if(filp->f_flags & O_NONBLOCK)
  {
    if(down_trylock(&dev->countSem))
      return -EAGAIN;
  }
  else
  {
    if(down_interruptible(&dev->countSem))
      return -ERESTARTSYS;
  }

  if((filp->f_flags & O_ACCMODE) == O_WRONLY)
  {
    // charDriver try to open as a writer
    if(!atomic_dec_and_test(&dev->numWriter))
      goto fail;
    up(&dev->countSem);
    printk(KERN_ALERT "charDriver is open as a writer\n");
  }
  else if((filp->f_flags & O_ACCMODE) == O_RDONLY)
  {
    // charDriver try to open as a reader
    if(atomic_dec_and_test(&dev->numWriter))
    {
      atomic_inc(&dev->numWriter);
      ++(dev->numReader);
      printk(KERN_ALERT "charDriver is open as a reader\n");
      printk(KERN_ALERT "charDriver have %d reader\n", dev->numReader);
    }
    else
      goto fail;
    up(&dev->countSem);
  }
  else if((filp->f_flags & O_ACCMODE) == O_RDWR)
  {
    // charDriver try to open as a reader/writer
    if(!atomic_dec_and_test(&dev->numWriter))
      goto fail;
    ++(dev->numReader);
    up(&dev->countSem);
    printk(KERN_ALERT "charDriver is open as a reader/writer\n");
  }
  else
  {
    up(&dev->countSem);
    return -ENOTTY;
  }

  //printk(KERN_WARNING "charDriver numReader = %d\n", charDriver.numReader);
  //atomicTest = atomic_read(&charDriver.numWriter);
  //printk(KERN_WARNING "charDriver numWriter = %d\n", atomicTest);

  return 0;

  fail:
  up(&dev->countSem);
  printk(KERN_ALERT "fail to open charDriver\n");
  return -ENOTTY;

}

static int charDriver_release(struct inode *inode, struct file *filp)
{
  struct charDriverDev *dev;
  //int atomicTest =  0;

  dev = container_of(inode->i_cdev, struct charDriverDev, cdev);

  //printk(KERN_WARNING "charDriver numReader = %d\n", charDriver.numReader);

  if(filp->f_flags & O_NONBLOCK)
  {
    if(down_trylock(&dev->countSem))
      return -EAGAIN;
  }
  else
  {
    if(down_interruptible(&dev->countSem))
      return -ERESTARTSYS;
  }

  if((filp->f_flags & O_ACCMODE) == O_WRONLY)
  {
    //printk(KERN_ALERT "charDriver try to release as a writer\n");
    atomic_inc(&dev->numWriter);
    up(&dev->countSem);
    printk(KERN_ALERT "charDriver have release as a writer\n");
  }
  else if((filp->f_flags & O_ACCMODE) == O_RDONLY)
  {
    //printk(KERN_ALERT "charDriver try to release as a reader\n");
    --(dev->numReader);
    up(&dev->countSem);
    printk(KERN_ALERT "charDriver have release as a reader\n");
  }
  else if((filp->f_flags & O_ACCMODE) == O_RDWR)
  {
    //charDriver try to release as a reader/writer
    atomic_inc(&dev->numWriter);
    --(dev->numReader);
    up(&dev->countSem);
    printk(KERN_ALERT "charDriver have release as a reader/writer\n");
  }
  else
  {
    up(&dev->countSem);
    return -ENOTTY;
  }
  //printk(KERN_WARNING "charDriver numReader = %d\n", charDriver.numReader);
  //atomicTest = atomic_read(&charDriver.numWriter);
  //printk(KERN_WARNING "charDriver numWriter = %d\n", atomicTest);

  return 0;
}

/*
 * Data management: read and write
 */

static ssize_t charDriver_read(struct file *filp, char __user *ubuf, size_t count, loff_t *offp)
{
  struct charDriverDev *dev = filp->private_data;
  unsigned int bufferDataCount;
  ssize_t nbBytesRead = 0;
  int i = 0;

  printk(KERN_WARNING "FUNCTION READ\n");

  if((filp->f_flags & O_ACCMODE) == O_RDONLY || (filp->f_flags & O_ACCMODE) == O_RDWR)
  {
    if(count < 0)
      return -EFAULT;

    if(count >= READWRITE_BUFSIZE)
      count = READWRITE_BUFSIZE;

    printk(KERN_WARNING "Number of bytes to read = %zu\n", count);

    if(filp->f_flags & O_NONBLOCK)
    {
      if(down_trylock(&dev->bufferSem))
        return -EAGAIN;
    }
    else
    {
      if(down_interruptible(&dev->bufferSem))
        return -ERESTARTSYS;
    }

    for(i = 0; i < count; ++i)
    {
      if(circularBufferOut(dev->cirBuffer, dev->readBuffer + i) == 0)
      {
        printk(KERN_WARNING "Reading byte %d of value %c\n", i, dev->readBuffer[i]);
        ++nbBytesRead;
        wake_up(&dev->writeWq);
      }
      else
      {
        printk(KERN_WARNING "Buffer Empty\n");
        while(circularBufferDataCount(dev->cirBuffer) == 0)
        {
          up(&dev->bufferSem);
          if(filp->f_flags & O_NONBLOCK)
            return -EAGAIN;
//          if(filp->f_flags & O_RDWR)
//          {
//            if(down_interruptible(&dev->countSem))
//              return -ERESTARTSYS;
//            atomic_inc(&dev->numWriter);
//            up(&dev->countSem);
//            printk(KERN_ALERT "charDriver have release as a writer\n");
//          }
          if(wait_event_interruptible(dev->readWq, circularBufferDataCount(dev->cirBuffer) > 0))
            return -ERESTARTSYS;
//          if(filp->f_flags & O_RDWR)
//          {
//            if(down_interruptible(&dev->countSem))
//              return -ERESTARTSYS;
//            if(!atomic_dec_and_test(&dev->numWriter))
//              goto fail;
//            up(&dev->countSem);
//            printk(KERN_ALERT "charDriver is open as a writer\n");
//          }
          down_interruptible(&dev->bufferSem);
        }
        --i;
      }
    }

    bufferDataCount = circularBufferDataCount(dev->cirBuffer);
    printk(KERN_WARNING "Number of bytes in circular buffer = %u\n", bufferDataCount);

    copy_to_user(ubuf, dev->readBuffer, nbBytesRead);
    up(&dev->bufferSem);
  }
  else
    return -EACCES;

  return nbBytesRead;

//  fail:
//  up(&dev->countSem);
//  printk(KERN_ALERT "fail to open charDriver\n");
//  return -ENOTTY;
}

static ssize_t charDriver_write(struct file *filp, const char __user *ubuf, size_t count, loff_t *offp)
{
  struct charDriverDev *dev = filp->private_data;
  unsigned int bufferDataCount;
  ssize_t nbBytesWrite = 0;
  int i = 0;

  printk(KERN_WARNING "FUNCTION WRITE\n");

  if((filp->f_flags & O_ACCMODE) == O_WRONLY || (filp->f_flags & O_ACCMODE) == O_RDWR)
  {
    if(count < 0)
      return -EFAULT;

    if(count >= READWRITE_BUFSIZE)
      count = READWRITE_BUFSIZE;

    printk(KERN_WARNING "Number of bytes to write = %zu\n", count);

    if(filp->f_flags & O_NONBLOCK)
    {
      if(down_trylock(&dev->bufferSem))
        return -EAGAIN;
    }
    else
    {
      if(down_interruptible(&dev->bufferSem))
        return -ERESTARTSYS;
    }

    copy_from_user(dev->writeBuffer, ubuf, count);

    for(i = 0; i < count; ++i)
    {
      if(circularBufferIn(dev->cirBuffer, dev->writeBuffer[i]) == 0)
      {
        printk(KERN_WARNING "Writing byte %d of value %c\n", i, dev->writeBuffer[i]);
        ++nbBytesWrite;
        wake_up(&dev->readWq);
      }
      else
      {
        printk(KERN_WARNING "Buffer Full\n");
        while(circularBufferDataCount(dev->cirBuffer) == CIRCULAR_BUFFER_SIZE)
        {
          up(&dev->bufferSem);
          if(filp->f_flags & O_NONBLOCK)
            return -EAGAIN;
          if(down_interruptible(&dev->countSem))
            return -ERESTARTSYS;
          atomic_inc(&dev->numWriter);
          up(&dev->countSem);
          printk(KERN_ALERT "charDriver have release as a writer\n");
          if(wait_event_interruptible(dev->writeWq, circularBufferDataCount(dev->cirBuffer) < CIRCULAR_BUFFER_SIZE))
            return -ERESTARTSYS;
          if(down_interruptible(&dev->countSem))
            return -ERESTARTSYS;
          if(!atomic_dec_and_test(&dev->numWriter))
            goto fail;
          up(&dev->countSem);
          printk(KERN_ALERT "charDriver is open as a writer\n");
          down_interruptible(&dev->bufferSem);
        }
        --i;
      }
    }

    bufferDataCount = circularBufferDataCount(dev->cirBuffer);
    printk(KERN_WARNING "Number of bytes in circular buffer = %d\n", bufferDataCount);

    up(&dev->bufferSem);
  }
  else
    return -EACCES;

  return nbBytesWrite;

  fail:
  up(&dev->countSem);
  printk(KERN_ALERT "fail to open charDriver\n");
  return -ENOTTY;
}

/*
 * The ioctl() implementation
 */

static long charDriver_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
  return 0;
}

// Init and Exit functions
module_init(charDriver_init);
module_exit(charDriver_exit);
