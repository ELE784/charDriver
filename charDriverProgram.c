#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>


int main()
{
  int charDriver;
  int charDriver2;
  int ret;
  
  charDriver = open("/dev/etsele_cdev", O_RDONLY);
  charDriver2 = open("/dev/etsele_cdev", O_RDONLY);

  close(charDriver);
  close(charDriver2);

	return ret;
}
