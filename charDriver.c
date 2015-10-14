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
#define MINOR_NUMBER 0
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
struct class *charDriver_class;
struct cdev etsele_cdev;
char buffer[MAX_LENGTH];

// Driver handled file operations
static struct file_operations charDriver_fops = {
    .owner = THIS_MODULE,
    .open = charDriver_open,
    .release = charDriver_release,
    .read = charDriver_read,
    .write = charDriver_write,
    .unlocked_ioctl = charDriver_ioctl,
};

struct charDriverDev *charDriver;
struct Buffer_t *myBuffer;

/*
 *  Init and Release
 */

static int __init charDriver_init(void)
{
  int result;

  result = alloc_chrdev_region(&devNumber, 0, 1, "etsele_cdev");
  if (result < 0)
    printk(KERN_WARNING"charDriver ERROR IN alloc_chrdev_region (%s:%s:%u)\n", __FILE__, __FUNCTION__, __LINE__);
  else
    printk(KERN_WARNING"charDriver : MAJOR = %u MINOR = %u\n", MAJOR(devNumber), MINOR(devNumber));

  printk(KERN_WARNING "Init 1\n");
  printk(KERN_WARNING "Init 2\n");

  charDriver_class = class_create(THIS_MODULE, "charDriverClass");
  device_create(charDriver_class, NULL, devNumber, NULL, "etsele_cdev");
  cdev_init(&etsele_cdev, &charDriver_fops);
  etsele_cdev.owner = THIS_MODULE;
  etsele_cdev.ops = &charDriver_fops;
  if (cdev_add(&etsele_cdev, devNumber, 1) < 0)
    printk(KERN_WARNING"charDriver ERROR IN cdev_add (%s:%s:%u)\n", __FILE__, __FUNCTION__, __LINE__);

  return 0;
}

static void __exit charDriver_exit(void)
{

  cdev_del(&etsele_cdev);
  device_destroy (charDriver_class, devNumber);
  class_destroy(charDriver_class);

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

//  if((filp->f_flags & O_ACCMODE) == O_WRONLY || O_RDWR)
//  {
//    printk(KERN_ALERT "charDriver try to open as a writer\n");
//    if(down_interruptible(&dev->bufferSem))
//      return -ERESTARTSYS;
//    if(!atomic_dec_and_test(&dev->numWriter))
//      goto fail;
//    up(&dev->bufferSem);
//    printk(KERN_ALERT "charDriver is open as a writer\n");
//  }
//
//  if((filp->f_flags & O_ACCMODE) == O_RDONLY)
//  {
//    printk(KERN_ALERT "charDriver try to open as a reader\n");
//    if(down_interruptible(&dev->bufferSem))
//      return -ERESTARTSYS;
//    if(atomic_dec_and_test(&dev->numWriter))
//    {
//      atomic_inc(&dev->numWriter);
//      ++(dev->numReader);
//      printk(KERN_ALERT "charDriver is open as a reader\n");
//    }
//    else
//      goto fail;
//    up(&dev->bufferSem);
//  }

  return 0;

//  fail:
//  up(&dev->bufferSem);
//  printk(KERN_ALERT "fail to open charDriver\n");
//  return -ENOTTY;

}

static int charDriver_release(struct inode *inode, struct file *filp)
{
  struct charDriverDev *dev;

  dev = container_of(inode->i_cdev, struct charDriverDev, cdev);

//  if((filp->f_flags & O_ACCMODE) == O_WRONLY || O_RDWR)
//  {
//    printk(KERN_ALERT "charDriver try to release as a writer\n");
//    if(down_interruptible(&dev->bufferSem))
//      return -ERESTARTSYS;
//    atomic_inc(&dev->numWriter);
//    up(&dev->bufferSem);
//    printk(KERN_ALERT "charDriver have release as a writer\n");
//  }
//
//  if((filp->f_flags & O_ACCMODE) == O_RDONLY)
//  {
//    printk(KERN_ALERT "charDriver try to release as a reader\n");
//    if(down_interruptible(&dev->bufferSem))
//      return -ERESTARTSYS;
//    --(dev->numReader);
//    up(&dev->bufferSem);
//    printk(KERN_ALERT "charDriver have release as a reader\n");
//  }

  return 0;
}

/*
 * Data management: read and write
 */

static ssize_t charDriver_read(struct file *filp, char __user *ubuf, size_t count, loff_t *f_ops)
{
  //struct etsele_cdev *dev = flip->private_data;

  int maxBytes;
  int nbBytesToRead;
  int nbBytesLeft;

  maxBytes = MAX_LENGTH - *f_ops;

  if(maxBytes > count)
    nbBytesToRead = count;
  else
    nbBytesToRead = maxBytes;

  if(nbBytesToRead == 0)
  {
    printk(KERN_ALERT "Reached end of device\n");
    return -ENOSPC;
  }

  nbBytesLeft = nbBytesToRead - copy_to_user(ubuf, buffer + *f_ops, nbBytesToRead);
  *f_ops += nbBytesLeft;

  printk(KERN_WARNING "%s has read this from the device : %s\n", __FUNCTION__, buffer);

  return nbBytesLeft;
}

static ssize_t charDriver_write(struct file *filp, const char __user *ubuf, size_t count, loff_t *f_ops)
{
  int nbBytesLeft;
  int nbBytesToWrite;
  int maxBytes;

  maxBytes = MAX_LENGTH - *f_ops;

  if(maxBytes > count)
    nbBytesToWrite = count;
  else
    nbBytesToWrite = maxBytes;


  if(nbBytesToWrite == 0)
  {
    printk(KERN_ALERT "Reached end of device\n");
    return -ENOSPC;
  }

  nbBytesLeft = nbBytesToWrite - copy_from_user(buffer + *f_ops, ubuf, nbBytesToWrite);
  *f_ops += nbBytesLeft;

  printk(KERN_WARNING "%s has put this to the device : %s\n", __FUNCTION__, buffer);

  return nbBytesLeft;
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
