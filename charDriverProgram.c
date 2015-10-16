#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>


int main()
{
  int charDriver;
  char bufferIn[32];
  char bufferOut[32] = "Hello WorldHello WorldHello Worl";
  int ret;
  
  charDriver = open("/dev/etsele_cdev", O_RDWR);

  ret = write(charDriver, bufferOut, 5);
  if(ret < 0)
    return ret; 
  ret = read(charDriver, bufferIn, 4);
  if(ret < 0)
    return ret; 
  ret = read(charDriver, bufferIn, 1);
  if(ret < 0)
    return ret; 
  ret = write(charDriver, bufferOut, 32);  
  if(ret < 0)
    return ret;  
  ret = read(charDriver, bufferIn, 11);
  if(ret < 0)
    return ret; 
  ret = read(charDriver, bufferIn, 11);
  if(ret < 0)
    return ret; 
  ret = read(charDriver, bufferIn, 11);
  if(ret < 0)
    return ret; 

  close(charDriver);

	return ret;
}
