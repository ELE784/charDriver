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
#include <linux/sched.h>
#include <uapi/asm-generic/fcntl.h>
#include <linux/fcntl.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/device.h>
#include <asm/uaccess.h>
#include <uapi/asm-generic/ioctl.h>
#include "circularBuffer.h"
#include "charDriver.h"

// Driver constants
#define MAJOR_NUMBER 0
#define NUMBER_OF_DEVICES 1
#define DEVICE_NAME "etsele_cdev"

// Configuration / Defines
#define READWRITE_BUFSIZE 	16
#define CIRCULAR_BUFFER_SIZE 	256
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
  int atomicTest =  0;
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

  atomic_set(&charDriver.numWriter, 1);
  atomicTest = atomic_read(&charDriver.numWriter);
  printk(KERN_WARNING "charDriver numWriter = %d\n", atomicTest);
  charDriver.numReader = 0;
  printk(KERN_WARNING "charDriver numReader = %d\n", charDriver.numReader);
  charDriver.dev = devNumber;
  printk(KERN_WARNING "charDriver dev Major = %d Minor = %d\n", MAJOR(charDriver.dev), MINOR(charDriver.dev));

  charDriver.cirBuffer = circularBufferInit(CIRCULAR_BUFFER_SIZE);

  charDriverDevice = device_create(charDriverClass, NULL, devNumber, NULL, DEVICE_NAME);
  charDriver.device = charDriverDevice;

  cdev_init(&charDriver.cdev, &charDriver_fops);
  charDriver.cdev.owner = THIS_MODULE;
  charDriver.cdev.ops = &charDriver_fops;

  sema_init(&charDriver.bufferSem, 1);
  printk(KERN_WARNING "bufferSem is initialized\n");
  sema_init(&charDriver.countSem, 1);
  printk(KERN_WARNING "countSem is initialized\n");

  if (cdev_add(&charDriver.cdev, devNumber, 1) < 0)
    printk(KERN_WARNING"charDriver ERROR IN cdev_add (%s:%s:%u)\n", __FILE__, __FUNCTION__, __LINE__);

  printk(KERN_WARNING "Device initialized\n");

  return 0;
}

static void __exit charDriver_exit(void)
{
  kfree(charDriver.readBuffer);
  kfree(charDriver.writeBuffer);
  charDriver.readBuffer = NULL;
  charDriver.writeBuffer = NULL;
  circularBufferDelete(charDriver.cirBuffer);

  cdev_del(&charDriver.cdev);
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

  dev = container_of(inode->i_cdev, struct charDriverDev, cdev);
  filp->private_data = dev;

  printk(KERN_WARNING "charDriver numReader = %d\n", charDriver.numReader);

  if((filp->f_flags & O_ACCMODE) == O_WRONLY)
  {
    printk(KERN_ALERT "charDriver try to open as a writer\n");
    if(down_interruptible(&dev->countSem))
      return -ERESTARTSYS;
    if(!atomic_dec_and_test(&dev->numWriter))
      goto fail;
    up(&dev->countSem);
    printk(KERN_ALERT "charDriver is open as a writer\n");
  }

  if((filp->f_flags & O_ACCMODE) == O_RDONLY)
  {
    printk(KERN_ALERT "charDriver try to open as a reader\n");
    if(down_interruptible(&dev->countSem))
      return -ERESTARTSYS;
    if(atomic_dec_and_test(&dev->numWriter))
    {
      atomic_inc(&dev->numWriter);
      ++(dev->numReader);
      printk(KERN_ALERT "charDriver is open as a reader\n");
    }
    else
      goto fail;
    up(&dev->countSem);
  }

  if((filp->f_flags & O_ACCMODE) == O_RDWR)
  {
    printk(KERN_ALERT "charDriver try to open as a reader/writer\n");
    if(down_interruptible(&dev->countSem))
      return -ERESTARTSYS;
    if(!atomic_dec_and_test(&dev->numWriter))
      goto fail;
    ++(dev->numReader);
    up(&dev->countSem);
    printk(KERN_ALERT "charDriver is open as a writer\n");
  }

  printk(KERN_WARNING "charDriver numReader = %d\n", charDriver.numReader);

  return 0;

  fail:
  up(&dev->bufferSem);
  printk(KERN_ALERT "fail to open charDriver\n");
  return -ENOTTY;

}

static int charDriver_release(struct inode *inode, struct file *filp)
{
  struct charDriverDev *dev;

  dev = container_of(inode->i_cdev, struct charDriverDev, cdev);

  printk(KERN_WARNING "charDriver numReader = %d\n", charDriver.numReader);

  if((filp->f_flags & O_ACCMODE) == O_WRONLY)
  {
    printk(KERN_ALERT "charDriver try to release as a writer\n");
    if(down_interruptible(&dev->countSem))
      return -ERESTARTSYS;
    atomic_inc(&dev->numWriter);
    up(&dev->countSem);
    printk(KERN_ALERT "charDriver have release as a writer\n");
  }

  if((filp->f_flags & O_ACCMODE) == O_RDONLY)
  {
    printk(KERN_ALERT "charDriver try to release as a reader\n");
    if(down_interruptible(&dev->countSem))
      return -ERESTARTSYS;
    --(dev->numReader);
    up(&dev->countSem);
    printk(KERN_ALERT "charDriver have release as a reader\n");
  }

  if((filp->f_flags & O_ACCMODE) == O_RDWR)
  {
    printk(KERN_ALERT "charDriver try to release as a reader/writer\n");
    if(down_interruptible(&dev->countSem))
      return -ERESTARTSYS;
    atomic_inc(&dev->numWriter);
    --(dev->numReader);
    up(&dev->countSem);
    printk(KERN_ALERT "charDriver have release as a writer\n");
  }

  printk(KERN_WARNING "charDriver numReader = %d\n", charDriver.numReader);

  return 0;
}

/*
 * Data management: read and write
 */

static ssize_t charDriver_read(struct file *filp, char __user *ubuf, size_t count, loff_t *offp)
{
  struct charDriverDev *dev = filp->private_data;

  size_t maxBytes = READWRITE_BUFSIZE;
  size_t nbBytesToRead = 0;
  size_t nbBytesRead = 0;
  int i = 0;

  printk(KERN_WARNING "(%s:%s:%u)\n", __FILE__, __FUNCTION__, __LINE__);

  maxBytes = maxBytes - *offp;

  if((filp->f_flags & O_ACCMODE) == O_RDONLY || (filp->f_flags & O_ACCMODE) == O_RDWR)
  {
    if(count > maxBytes)
      nbBytesToRead = maxBytes;
    else
      nbBytesToRead = count;

    if(count < 0)
    {
      printk(KERN_ALERT "count < 0\n");
      count = 0;
    }

    for(i = 0; i < nbBytesToRead; ++i)
    {
      if(down_interruptible(&dev->bufferSem))
        return -ERESTARTSYS;
      if(circularBufferOut(dev->cirBuffer, dev->readBuffer + i) == 0)
        ++nbBytesRead;
      else
      {
        count = 0;
        i = nbBytesToRead;
      }
      up(&dev->bufferSem);

      printk(KERN_WARNING "Reading byte %d of value %d\n", i, dev->readBuffer[i]);
    }

    copy_to_user(ubuf, dev->readBuffer, nbBytesRead);
    *offp += nbBytesRead;

    printk(KERN_WARNING "%s has read this from the device : %s\n", __FUNCTION__, dev->readBuffer);
  }
  else
    return -EACCES;

  return nbBytesRead;
}

static ssize_t charDriver_write(struct file *filp, const char __user *ubuf, size_t count, loff_t *offp)
{
  struct charDriverDev *dev = filp->private_data;

  size_t maxBytes = READWRITE_BUFSIZE;
  size_t nbBytesToWrite = 0;
  size_t nbBytesWrite = 0;
  int i = 0;

  if((filp->f_flags & O_ACCMODE) == O_WRONLY || (filp->f_flags & O_ACCMODE) == O_RDWR)
  {
    maxBytes = READWRITE_BUFSIZE - *offp;

    if(count > maxBytes)
      nbBytesToWrite = maxBytes;
    else
      nbBytesToWrite = count;

    if(count < 0)
    {
      printk(KERN_ALERT "count < 0\n");
      count = 0;
    }
    if(down_interruptible(&dev->bufferSem))
      return -ERESTARTSYS;
    copy_from_user(dev->writeBuffer + i, ubuf, nbBytesToWrite);
    up(&dev->bufferSem);
    for(i = 0; i < nbBytesToWrite; ++i)
        {
          if(down_interruptible(&dev->bufferSem))
            return -ERESTARTSYS;
          if(circularBufferIn(dev->cirBuffer, dev->writeBuffer[i]) == 0)
            ++nbBytesWrite;
          else
          {
            count = 0;
            i = nbBytesToWrite;
          }
          up(&dev->bufferSem);

          printk(KERN_WARNING "Writing byte %d of value %d\n", i, dev->writeBuffer[i]);
        }

    *offp += nbBytesWrite;
  }
  else
    return -EACCES;

  printk(KERN_WARNING "%s has put this to the device : %s\n", __FUNCTION__, dev->writeBuffer);

  return nbBytesWrite;
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
