#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>


int main()
{
  int charDriver;
  int charDriver2;
  char bufferOut[8] = "Francis";
  char bufferIn[8];
  int ret;
  
  charDriver = open("/dev/etsele_cdev", O_WRONLY);
  
  ret = write(charDriver, bufferOut, 8); 
  
  close(charDriver);

	return ret;
}
