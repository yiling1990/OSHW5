// Added by Jingyue

#include "types.h"
#include "user.h"
#include "hw5-common.h"

int main() {

  int pid;
  int ppid = getpid();

  pid = fork();
  printf(stdout, "pid %d\n", pid);
  if (pid < 0)
    panic("fork");
  if (pid == 0) {
    char *a = sbrk(0);
    printf(stdout, "oops could read1 %x = %x\n", a, *a);
    kill(ppid);
    exit();
  }
  if (wait() < 0)
    panic("wait");

  pid = fork();
  if (pid < 0)
    panic("fork");
  if (pid == 0) {
    char *a = sbrk(0);
    *a = '\0';
    printf(stdout, "oops could write1 %x = %x\n", a, *a);
    kill(ppid);
    exit();
  }
  if (wait() < 0)
    panic("wait");

  pid = fork();
  if (pid < 0)
    panic("fork");
  if (pid == 0) {
    char *a = (char *)(640 * 1024);
    printf(stdout, "oops could read2 %x = %x\n", a, *a);
    kill(ppid);
    exit();
  }
  if (wait() < 0)
    panic("wait");

  pid = fork();
  if (pid < 0)
    panic("fork");
  if (pid == 0) {
    char *a = (char *)(640 * 1024);
    *a = '\0';
    printf(stdout, "oops could write2 %x = %x\n", a, *a);
    kill(ppid);
    exit();
  }
  if (wait() < 0)
    panic("wait");

  printf(stdout, "hw5-test-error succeeded\n");
  exit();
}

