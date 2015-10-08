/*
 * charDriver.h
 *
 *  Created on: Oct 8, 2015
 *      Author: bullshark
 */

#ifndef CHARDRIVER_H_
#define CHARDRIVER_H_

#include <linux/kernel.h>

// Driver internal management struct
typedef struct {
  char *ReadBuf;
  char *WriteBuf;
  struct semaphore SemBuf;
  unsigned short numWriter;
  unsigned short numReader;
  dev_t dev;
  struct cdev cdev;
} charDriverDev;



#endif /* CHARDRIVER_H_ */
