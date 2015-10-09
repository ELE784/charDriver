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
#include <uapi/asm-generic/fcntl.h>
#include <linux/fcntl.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/device.h>
#include <asm/uaccess.h>
#include "circularBuffer.h"
#include "charDriver.h"

// Configuration / Defines
#define READWRITE_BUFSIZE 	16
#define CIRCULAR_BUFFER_SIZE 	256
#define MAX_LENGTH 		16

// Module Information
MODULE_AUTHOR("Francis Masse, Alexandre Leblanc");
MODULE_LICENSE("Dual BSD/GPL");

dev_t devNumber;
struct class *charDriver_class;
struct cdev etsele_cdev;
static char buffer[MAX_LENGTH];

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

  charDriver = kmalloc(sizeof(struct charDriverDev), GFP_KERNEL);
  if (!charDriver)
  {
    result = -ENOMEM;
    goto fail;
  }

  memset(charDriver, 0, sizeof(struct charDriverDev));

  charDriver->ReadBuf[READWRITE_BUFSIZE] = 0;
  charDriver->WriteBuf[READWRITE_BUFSIZE] = 0;
  charDriver->numReader = 0;
  atomic_set(&charDriver->numWriter, 1);
  charDriver->dev = devNumber;
  sema_init(&charDriver->SemBuf, 1);

  myBuffer = circularBufferInit(CIRCULAR_BUFFER_SIZE);

  charDriver_class = class_create(THIS_MODULE, "charDriverClass");
  device_create(charDriver_class, NULL, devNumber, NULL, "etsele_cdev");
  cdev_init(&etsele_cdev, &charDriver_fops);
  etsele_cdev.owner = THIS_MODULE;
  if (cdev_add(&etsele_cdev, devNumber, 1) < 0)
    printk(KERN_WARNING"charDriver ERROR IN cdev_add (%s:%s:%u)\n", __FILE__, __FUNCTION__, __LINE__);

  return 0;

  fail:
  charDriver_exit();
  return result;
}

static void __exit charDriver_exit(void)
{
  if(charDriver)
  {
  cdev_del(&etsele_cdev);
  device_destroy (charDriver_class, devNumber);
  class_destroy(charDriver_class);
  charDriver->ReadBuf = 0;
  charDriver->WriteBuf = 0;
  charDriver->numReader = 0;
  kfree(charDriver);
  }

  circularBufferDelete(myBuffer);

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

  if((filp->f_flags & O_ACCMODE) == O_WRONLY || O_RDWR)
  {
    printk(KERN_ALERT "charDriver try to open as a writer\n");
    if(down_interruptible(&dev->SemBuf))
      return -ERESTARTSYS;
    if(!atomic_dec_and_test(&dev->numWriter))
      goto fail;
    up(&dev->SemBuf);
    printk(KERN_ALERT "charDriver is open as a writer\n");
  }

  if((filp->f_flags & O_ACCMODE) == O_RDONLY)
  {
    printk(KERN_ALERT "charDriver try to open as a reader\n");
    if(down_interruptible(&dev->SemBuf))
      return -ERESTARTSYS;
    if(atomic_dec_and_test(&dev->numWriter))
    {
      atomic_inc(&dev->numWriter);
      ++dev->numReader;
      printk(KERN_ALERT "charDriver is open as a reader\n");
    }
    else
      goto fail;
    up(&dev->SemBuf);
  }

  return 0;

  fail:
  up(&dev->SemBuf);
  printk(KERN_ALERT "fail to open charDriver\n");
  return -ENOTTY;

}

static int charDriver_release(struct inode *inode, struct file *filp)
{
  struct charDriverDev *dev;

  dev = container_of(inode->i_cdev, struct charDriverDev, cdev);
  filp->private_data = dev;

  if((filp->f_flags & O_ACCMODE) == O_WRONLY || O_RDWR)
  {
    printk(KERN_ALERT "charDriver try to release as a writer\n");
    if(down_interruptible(&dev->SemBuf))
      return -ERESTARTSYS;
    atomic_inc(&dev->numWriter);
    up(&dev->SemBuf);
    printk(KERN_ALERT "charDriver have release as a writer\n");
  }

  if((filp->f_flags & O_ACCMODE) == O_RDONLY)
  {
    printk(KERN_ALERT "charDriver try to release as a reader\n");
    if(down_interruptible(&dev->SemBuf))
      return -ERESTARTSYS;
    --dev->numReader;
    up(&dev->SemBuf);
    printk(KERN_ALERT "charDriver have release as a reader\n");
  }

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
