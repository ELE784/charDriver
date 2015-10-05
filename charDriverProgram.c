#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>


int main()
{
  char bufferOut[16] = "My charDriver";
  char bufferIn[16];
  int charDriver;
  int ret;
  
  charDriver = open("/dev/charDriver_Node", O_RDWR);

  ret = write(charDriver, bufferOut, 16);

  if(ret == 16)
  {    
    printf("Write is completed and ret = %d\n", ret);
    ret = read(charDriver, bufferIn, 1);
    printf("Read ret = %d\n", ret);
  }
  else
    printf("Error ret = %d\n", ret);

  close(charDriver);

	return ret;
}
