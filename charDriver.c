/*
 * File         : charDriver.c
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
#include "circularBuffer.h"
#include "charDriver.h"
#include "cmd.h"

#define __DEBUG__
#define __NONBLOCKWRITER__
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

struct charDriverDev charDriver;

/*
 *  Init and Release
 */

static int __init charDriver_init(void)
{
  int result;
#ifdef __DEBUG__
  int atomicTest =  0;
#endif
  struct device *charDriverDevice;

  result = alloc_chrdev_region(&devNumber, 0, 1, DEVICE_NAME);
#ifdef __DEBUG__
  if (result < 0)
    printk(KERN_WARNING"charDriver ERROR IN alloc_chrdev_region (%s:%s:%u)\n", __FILE__, __FUNCTION__, __LINE__);
  else
    printk(KERN_WARNING"charDriver : MAJOR = %u MINOR = %u\n", MAJOR(devNumber), MINOR(devNumber));
#endif

  charDriverClass = class_create(THIS_MODULE, "charDriverClass");
  charDriver.class = charDriverClass;
  if(charDriverClass == NULL)
  {
#ifdef __DEBUG__
    printk(KERN_WARNING "Can't create class");
#endif
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
#ifdef __DEBUG__
  atomicTest = atomic_read(&charDriver.numWriter);
  printk(KERN_WARNING "charDriver numWriter = %d\n", atomicTest);
#endif
  charDriver.numReader = 0;
#ifdef __DEBUG__
  printk(KERN_WARNING "charDriver numReader = %d\n", charDriver.numReader);
#endif
  charDriver.dev = devNumber;
#ifdef __DEBUG__
  printk(KERN_WARNING "charDriver dev Major = %d Minor = %d\n", MAJOR(charDriver.dev), MINOR(charDriver.dev));
#endif

  charDriver.cirBuffer = circularBufferInit(CIRCULAR_BUFFER_SIZE);

  charDriverDevice = device_create(charDriverClass, NULL, devNumber, NULL, DEVICE_NAME);
  charDriver.device = charDriverDevice;

  cdev_init(&charDriver.cdev, &charDriver_fops);
  charDriver.cdev.owner = THIS_MODULE;
  charDriver.cdev.ops = &charDriver_fops;

  sema_init(&charDriver.bufferSem, 1);
  sema_init(&charDriver.countSem, 1);
  if (cdev_add(&charDriver.cdev, devNumber, 1) < 0)
    printk(KERN_WARNING"charDriver ERROR IN cdev_add (%s:%s:%u)\n", __FILE__, __FUNCTION__, __LINE__);

#ifdef __DEBUG__
  printk(KERN_WARNING "====================================================\n");
  printk(KERN_WARNING "====================================================\n");
  printk(KERN_WARNING "                Device initialized\n");
  printk(KERN_WARNING "====================================================\n");
  printk(KERN_WARNING "====================================================\n");
#endif

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

#ifdef __DEBUG__
  printk(KERN_ALERT "charDriver have successfully exited\n");
#endif
}

/*
 *  Open and Close
 */

static int charDriver_open(struct inode *inode, struct file *filp)
{
  struct charDriverDev *dev;
#ifdef __DEBUG__
  int atomicTest =  0;
#endif

  dev = container_of(inode->i_cdev, struct charDriverDev, cdev);
  filp->private_data = dev;

#ifdef __DEBUG__
  printk(KERN_WARNING "charDriver numReader = %d\n", charDriver.numReader);
#endif

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

#ifdef __DEBUG__
  printk(KERN_WARNING "charDriver numReader = %d\n", charDriver.numReader);
  atomicTest = atomic_read(&charDriver.numWriter);
  printk(KERN_WARNING "charDriver numWriter = %d\n", atomicTest);
#endif

  return 0;

  fail:
  up(&dev->countSem);
  printk(KERN_ALERT "fail to open charDriver\n");
  return -ENOTTY;

}

static int charDriver_release(struct inode *inode, struct file *filp)
{
  struct charDriverDev *dev = filp->private_data;
#ifdef __DEBUG__
  int atomicTest =  0;

  printk(KERN_WARNING "charDriver numReader = %d\n", charDriver.numReader);
#endif

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
#ifdef __DEBUG__
    printk(KERN_ALERT "charDriver try to release as a writer\n");
#endif
    atomic_inc(&dev->numWriter);
    up(&dev->countSem);
    printk(KERN_ALERT "charDriver have release as a writer\n");
  }
  else if((filp->f_flags & O_ACCMODE) == O_RDONLY)
  {
#ifdef __DEBUG__
    printk(KERN_ALERT "charDriver try to release as a reader\n");
#endif
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
#ifdef __DEBUG__
  printk(KERN_WARNING "charDriver numReader = %d\n", charDriver.numReader);
  atomicTest = atomic_read(&charDriver.numWriter);
  printk(KERN_WARNING "charDriver numWriter = %d\n", atomicTest);
#endif

  return 0;
}

/*
 * Data management: read and write
 */

static ssize_t charDriver_read(struct file *filp, char __user *ubuf, size_t count, loff_t *offp)
{
  struct charDriverDev *dev = filp->private_data;
#ifdef __DEBUG__
  unsigned int bufferDataCount;
#endif
  ssize_t nbBytesRead = 0;
  int i = 0;

#ifdef __DEBUG__
  printk(KERN_WARNING "FUNCTION READ\n");
#endif

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
#ifdef __DEBUG__
        printk(KERN_WARNING "Buffer Empty\n");
#endif
        while(circularBufferDataCount(dev->cirBuffer) == 0)
        {
          up(&dev->bufferSem);
          if(filp->f_flags & O_NONBLOCK)
            return -EAGAIN;
          if(wait_event_interruptible(dev->readWq, circularBufferDataCount(dev->cirBuffer) > 0))
            return -ERESTARTSYS;
          down_interruptible(&dev->bufferSem);
        }
        --i;
      }
    }

#ifdef __DEBUG__
    bufferDataCount = circularBufferDataCount(dev->cirBuffer);
    printk(KERN_WARNING "Number of bytes in circular buffer = %u\n", bufferDataCount);
#endif
    copy_to_user(ubuf, dev->readBuffer, nbBytesRead);
    up(&dev->bufferSem);
  }
  else
    return -EACCES;

  return nbBytesRead;
}

static ssize_t charDriver_write(struct file *filp, const char __user *ubuf, size_t count, loff_t *offp)
{
  struct charDriverDev *dev = filp->private_data;
#ifdef __DEBUG__
  unsigned int bufferDataCount;
#endif
  ssize_t nbBytesWrite = 0;
  int i = 0;

#ifdef __DEBUG__
  printk(KERN_WARNING "FUNCTION WRITE\n");
#endif

  if((filp->f_flags & O_ACCMODE) == O_WRONLY || (filp->f_flags & O_ACCMODE) == O_RDWR)
  {
    if(count < 0)
      return -EFAULT;

    if(count >= READWRITE_BUFSIZE)
      count = READWRITE_BUFSIZE;
#ifdef __DEBUG__
    printk(KERN_WARNING "Number of bytes to write = %zu\n", count);
#endif
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
#ifdef __DEBUG__
        printk(KERN_WARNING "Writing byte %d of value %c\n", i, dev->writeBuffer[i]);
#endif
        ++nbBytesWrite;
        wake_up(&dev->readWq);
      }
      else
      {
#ifdef __DEBUG__
        printk(KERN_WARNING "Buffer Full\n");
#endif
        while(circularBufferDataCount(dev->cirBuffer) == CIRCULAR_BUFFER_SIZE)
        {
          up(&dev->bufferSem);
#ifdef __NONBLOCKWRITER__
          if(filp->f_flags & O_NONBLOCK)
            return -EAGAIN;
          if(down_interruptible(&dev->countSem))
            return -ERESTARTSYS;
          atomic_inc(&dev->numWriter);
          up(&dev->countSem);
#ifdef __DEBUG__
          printk(KERN_ALERT "charDriver have release as a writer\n");
#endif
#endif
          if(wait_event_interruptible(dev->writeWq, circularBufferDataCount(dev->cirBuffer) < CIRCULAR_BUFFER_SIZE))
            return -ERESTARTSYS;
#ifdef __NONBLOCKWRITER__
          if(down_interruptible(&dev->countSem))
            return -ERESTARTSYS;
          if(!atomic_dec_and_test(&dev->numWriter))
            goto fail;
          up(&dev->countSem);
          printk(KERN_ALERT "charDriver is open as a writer\n");
#endif
          if(down_interruptible(&dev->bufferSem))
            return -ERESTARTSYS;
        }
        --i;
      }
    }

#ifdef __DEBUG__
    bufferDataCount = circularBufferDataCount(dev->cirBuffer);
    printk(KERN_WARNING "Number of bytes in circular buffer = %d\n", bufferDataCount);
#endif

    up(&dev->bufferSem);
  }
  else
    return -EACCES;

  return nbBytesWrite;
#ifdef __NONBLOCKWRITER__
  fail:
  up(&dev->countSem);
  printk(KERN_ALERT "fail to open charDriver\n");
  return -ENOTTY;
#endif
}

/*
 * The ioctl() implementation
 */

static long charDriver_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
  struct charDriverDev *dev = filp->private_data;
  unsigned short usTemp = 0;
  unsigned int uiTemp = 0;
  int error = 0;
  int retval = 0;

  if(_IOC_TYPE(cmd) != CHARDRIVER_IOC_MAGIC)
    return -ENOTTY;
  if(_IOC_NR(cmd) > CHARDRIVER_IOC_MAXNR)
    return -ENOTTY;

  if(_IOC_DIR(cmd) & _IOC_READ)
    error = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
  else if(_IOC_DIR(cmd) & _IOC_WRITE)
    error =  !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
  if(error)
    return -EFAULT;

  switch(cmd){
  case CHARDRIVER_IOCGETNUMDATA:
    if(down_interruptible(&dev->bufferSem))
      return -ERESTARTSYS;
    uiTemp = circularBufferDataCount(dev->cirBuffer);
    retval = __put_user(uiTemp, (unsigned int __user *)arg);
    up(&charDriver.bufferSem);
    break;
  case CHARDRIVER_IOCGETNUMREADER:
    if(down_interruptible(&dev->countSem))
      return -ERESTARTSYS;
    usTemp = dev->numReader;
    retval = __put_user(usTemp, (unsigned short __user *)arg);
    up(&charDriver.countSem);
    break;
  case CHARDRIVER_IOCGETBUFFERSIZE:
    if(down_interruptible(&dev->bufferSem))
      return -ERESTARTSYS;
    uiTemp = circularBufferDataSize(dev->cirBuffer);
    retval = __put_user(uiTemp, (unsigned int __user *)arg);
    up(&charDriver.bufferSem);
    break;
  case CHARDRIVER_IOCSETBUFFERSIZE:
    if(down_trylock(&dev->bufferSem))
      return -EAGAIN;
    if(!capable(CAP_SYS_ADMIN))
    {
      up(&charDriver.bufferSem);
      return -EPERM;
    }
    retval = __get_user(uiTemp, (unsigned int __user *)arg);
    printk(KERN_WARNING "temp = %u\n", uiTemp);
    retval = circularBufferResize(dev->cirBuffer, uiTemp);
    up(&charDriver.bufferSem);
    break;
  default:
    retval =  -ENOTTY;
    break;
  }

  return retval;
}

// Init and Exit functions
module_init(charDriver_init);
module_exit(charDriver_exit);
