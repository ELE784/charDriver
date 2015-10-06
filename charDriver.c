/*
 * File         : ele784-lab1.c
 * Description  : ELE784 Lab1 source
 *
 * Etudiants:  MASF05089000 (Francis Masse)
 *             LEBA23057609 (Alexandre Leblanc)
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <asm/atomic.h>
#include <asm/uaccess.h>
#include "circularBuffer.h"

// Configuration / Defines
#define READWRITE_BUFSIZE 	16
#define CIRCULAR_BUFFER_SIZE 	256
#define MAX_LENGTH 		16

// Module Information
MODULE_AUTHOR("Francis Masse, Alexandre Leblanc");
MODULE_LICENSE("Dual BSD/GPL");

// Prototypes
static int __init charDriver_init(void);
static void __exit charDriver_exit(void);
static int charDriver_open(struct inode *inode, struct file *flip);
static int charDriver_release(struct inode *inode, struct file *flip);
static ssize_t charDriver_read(struct file *flip, char __user *ubuf, size_t count, loff_t *f_ops);
static ssize_t charDriver_write(struct file *flip, const char __user *ubuf, size_t count, loff_t *f_ops);
static long charDriver_ioctl(struct file *flip, unsigned int cmd, unsigned long arg);

dev_t devNumber;
struct class *charDriver_class;
struct cdev etsele_cdev;
static char buffer[MAX_LENGTH];

// Driver internal management struct
typedef struct charDriverDev {
  char *ReadBuf;
  char *WriteBuf;
  struct semaphore SemBuf;
  unsigned short numWriter;
  unsigned short numReader;
  dev_t dev;
  struct cdev cdev;
};

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

// Init and Exit functions
module_init(charDriver_init);
module_exit(charDriver_exit);

static int __init charDriver_init(void)
{
  int result;

  result = alloc_chrdev_region(&devNumber, 0, 1, "etsele_cdev");
  if (result < 0)
    printk(KERN_WARNING"charDriver ERROR IN alloc_chrdev_region (%s:%s:%u)\n", __FILE__, __FUNCTION__, __LINE__);
  else
    printk(KERN_WARNING"charDriver : MAJOR = %u MINOR = %u\n", MAJOR(devNumber), MINOR(devNumber));

  charDriver = kmalloc(sizeof(struct etsele_cdev), GFP_KERNEL);
  if(!charDriver)
  {
    result = -12;
    goto fail;  /* Make this more graceful */
  }

  charDriver_class = class_create(THIS_MODULE, "charDriverClass");
  device_create(charDriver_class, NULL, devNumber, NULL, "etsele_cdev");
  cdev_init(&etsele_cdev, &charDriver_fops);
  etsele_cdev.owner = THIS_MODULE;
  if (cdev_add(&etsele_cdev, devNumber, 1) < 0)
    printk(KERN_WARNING"charDriver ERROR IN cdev_add (%s:%s:%u)\n", __FILE__, __FUNCTION__, __LINE__);

  return 0;

  fail:
  charDriver_exit();
}


static void __exit charDriver_exit(void)
{
  cdev_del(&etsele_cdev);
  unregister_chrdev_region(devNumber, 1);
  device_destroy (charDriver_class, devNumber);
  class_destroy(charDriver_class);

  printk(KERN_ALERT "charDriver have successfully exited\n");
}


static int charDriver_open(struct inode *inode, struct file *flip)
{
  struct charDriverDev *dev;

  dev = container_of(inode->i_cdev, struct charDriverDev, cdev);
  flip->private_data = dev;

  if(down_interruptible(&charDriver->SemBuf))
    return -ERESTARTSYS;
  myBuffer = circularBufferInit(CIRCULAR_BUFFER_SIZE);

  up(&charDriver->SemBuf);

  printk(KERN_ALERT "charDriver is open\n");
  return 0;
}


static int charDriver_release(struct inode *inode, struct file *flip)
{
  printk(KERN_ALERT "charDriver is release\n");
  return 0;
}


static ssize_t charDriver_read(struct file *flip, char __user *ubuf, size_t count, loff_t *f_ops)
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
    return -28;
  }

  nbBytesLeft = nbBytesToRead - copy_to_user(ubuf, buffer + *f_ops, nbBytesToRead);
  *f_ops += nbBytesLeft;

  printk(KERN_WARNING "%s has read this from the device : %s\n", __FUNCTION__, buffer);

  return nbBytesLeft;
}


static ssize_t charDriver_write(struct file *flip, const char __user *ubuf, size_t count, loff_t *f_ops)
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
    return -28;
  }

  nbBytesLeft = nbBytesToWrite - copy_from_user(buffer + *f_ops, ubuf, nbBytesToWrite);
  *f_ops += nbBytesLeft;

  printk(KERN_WARNING "%s has put this to the device : %s\n", __FUNCTION__, buffer);

  return nbBytesLeft;
}


static long charDriver_ioctl(struct file *flip, unsigned int cmd, unsigned long arg)
{
  return 0;
}
