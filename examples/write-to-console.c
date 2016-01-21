#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include <stdarg.h>

int main(void) {
  char *sayHi = "Hello user, it now works to print text to the console.\n";
  char *longText = "Lets see if it can handle long texts, so this is gonna be alittle bit longer then the last.\nWhat do you think about parrots? I like carrots, they are usually flying around all over the place like crazy little birds. Caw caw bitches\n";

  write(STDOUT_FILENO, sayHi, strlen(sayHi));
  write(STDOUT_FILENO, longText, strlen(longText));

  printf("Test passed like a baws\n");
  halt();
}
