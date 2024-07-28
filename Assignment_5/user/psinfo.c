#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/psinfo.h"

int main(void) {
  struct psinfo pinfo[MAX_PROCS];
  int counter = 0;

  counter = psinfo(pinfo);

  // Display the name board
  printf("pid\t name\t state\t\t parent\n");
  printf("----------------------------------------\n");

  for (int i = 0; i < counter; i++) 
  {
    // SLEEPING state
    if (pinfo[i].state == 2)
    {
      printf("%d\t %s\t SLEEPING\t %s\n", pinfo[i].pid, pinfo[i].name, pinfo[i].parent_name);
    }
    // RUNNING state
    else if (pinfo[i].state == 4)
    {      
      printf("%d\t %s\t RUNNING\t %s\n", pinfo[i].pid, pinfo[i].name, pinfo[i].parent_name);
    }
    // RUNNABLE state
    else if (pinfo[i].state == 3)
    {
      printf("%d\t %s\t RUNNABLE\t %s\n", pinfo[i].pid, pinfo[i].name, pinfo[i].parent_name);
    }    
  }

  // Total number of processes
  printf ("Total: %d\n", counter);

  // // Error on the CPU part, Abandon All
  // for (int i = 0; i < 8; i++) 
  // {
  //   printf("cpu%d: %s\n", i, pinfo[i].cpu_process_name);  
  // }
  
  exit(0);
}

