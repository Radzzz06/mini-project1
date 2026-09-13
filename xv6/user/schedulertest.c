#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// spawn several children with different CPU-burst / yield patterns to exercise the MLFQ scheduler
//   child 0 : CPU-bound(one long burst, never yields) -> sinks to q3, then gets pulled back to q0 by the 48-tick boost
//   child 1 : mixed(a few ticks of CPU, then a short voluntary yield)
//   child 2+: I/O-bound(tiny CPU burst, then a longer voluntary yield) -> keeps giving up the CPU so it stays in a high queue

static void busy_ticks(int nticks)
{
  int start = uptime();
  volatile int x = 0;
  while (uptime() - start < nticks) 
  {
    for (int j = 0; j < 40000; j++)
      x += j;
  }
}

int
main(int argc, char *argv[])
{
  int nproc = 3;
  if (argc > 1)
    nproc = atoi(argv[1]);
  if (nproc < 1)
    nproc = 1;

  printf("schedulertest: launching %d children at tick %d\n", nproc, uptime());

  for (int i = 0; i < nproc; i++) 
  {
    int pid = fork();
    if (pid < 0) {
      printf("schedulertest: fork failed\n");
      exit(1);
    }
    if (pid == 0) 
    {
      if (i == 0) 
      {
        busy_ticks(60); // CPU-bound: one long burst
      } else if (i == 1) 
      {
        for (int k = 0; k < 12; k++) // mixed
        {  
          busy_ticks(4);
          pause(2);
        }
      } else {
        for (int k = 0; k < 15; k++) // I/O-bound-like
        {  
          busy_ticks(1);
          pause(5);
        }
      }
      exit(0);
    }
  }

  for (int i = 0; i < nproc; i++)
    wait(0);

  printf("schedulertest: all children done at tick %d\n", uptime());
  exit(0);
}