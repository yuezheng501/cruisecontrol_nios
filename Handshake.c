// File: TwoTasks.c 

#include <stdio.h>
#include "includes.h"
#include <string.h>

#define DEBUG 0

/* Definition of Task Stacks */
/* Stack grows from HIGH to LOW memory */
#define   TASK_STACKSIZE       2048
OS_STK    task0_stk[TASK_STACKSIZE];
OS_STK    task1_stk[TASK_STACKSIZE];
OS_STK    stat_stk[TASK_STACKSIZE];

/* Definition of Task Priorities */
#define TASK0_PRIORITY      6  // highest priority
#define TASK1_PRIORITY      7
#define TASK_STAT_PRIORITY 12  // lowest priority 

OS_EVENT *aSemaphore,*bSemaphore;//global statement for semaphore

void printStackSize(INT8U prio)
{
    INT8U err;
    OS_STK_DATA stk_data;
    
    err = OSTaskStkChk(prio, &stk_data);
    if (err == OS_NO_ERR) 
    {
        if (DEBUG == 1)
           printf("Task Priority %d - Used: %d; Free: %d\n", 
                   prio, stk_data.OSFree, stk_data.OSUsed);
    }
    else
    {
        if (DEBUG == 1)
           printf("Stack Check Error!\n");    
    }
}

/* Prints a message and sleeps for given time interval */
void task0(void* pdata)
{
  INT8U state=0;
  INT8U err;
  while (1)
  { 
    // OSSemPend(aSemaphore, 0, &err);
    printf("Task 0 - State %d \n",0x01&state);
    OSSemPost(aSemaphore);
    OSSemPend(bSemaphore, 0, &err);
    state=0x01 & ~state;
    printf("Task 0 - State %d \n",0x01&state);
    state=0x01 & ~state;
    // printf("Task 0 - State %d \n",0x01&state);
    // state=0x01 & ~state;
    // OSSemPend(aSemaphore, 0, &err);
    // OSTimeDlyHMSM(0, 0, 0, 4); // Context Switch to next task

                               // Task will go to the ready state

                               // after the specified delay
  }
}

/* Prints a message and sleeps for given time interval */
void task1(void* pdata)
{
  INT8U state=0;
  INT8U err;
  while (1)
  { 
    // OSSemPost(aSemaphore);
    OSSemPend(aSemaphore, 0, &err);
    printf("Task 1 - State %d \n",0x01&state);
    state=0x01 & ~state;
    printf("Task 1 - State %d \n",0x01&state);
    OSSemPost(bSemaphore);
    state=0x01 & ~state;
    // OSSemPend(bSemaphore, 0, &err);
    // printf("Task 1 - State %d \n",0x01&state);
    // state=0x01 & ~state;
    // OSSemPost(aSemaphore);
    // OSTimeDlyHMSM(0, 0, 0, 4);
  }
}

/* Printing Statistics */
void statisticTask(void* pdata)
{
    while(1)
    {
        printStackSize(TASK0_PRIORITY);
        printStackSize(TASK1_PRIORITY);
        printStackSize(TASK_STAT_PRIORITY);
        // OSTimeDlyHMSM(0, 0, 0, 4);
    }
}

/* The main function creates two task and starts multi-tasking */
int main(void)
{
  // printf("Lab 3 - Two Tasks\n");
  OSInit();
  aSemaphore = OSSemCreate(0);
  bSemaphore = OSSemCreate(0);
  OSTaskCreateExt
    (task0,                        // Pointer to task code
     NULL,                         // Pointer to argument that is
                                   // passed to task
     &task0_stk[TASK_STACKSIZE-1], // Pointer to top of task stack
     TASK0_PRIORITY,               // Desired Task priority
     TASK0_PRIORITY,               // Task ID
     &task0_stk[0],                // Pointer to bottom of task stack
     TASK_STACKSIZE,               // Stacksize
     NULL,                         // Pointer to user supplied memory
                                   // (not needed here)
     OS_TASK_OPT_STK_CHK |         // Stack Checking enabled 
     OS_TASK_OPT_STK_CLR           // Stack Cleared                                 
    );
               
  OSTaskCreateExt
    (task1,                        // Pointer to task code
     NULL,                         // Pointer to argument that is
                                   // passed to task
     &task1_stk[TASK_STACKSIZE-1], // Pointer to top of task stack
     TASK1_PRIORITY,               // Desired Task priority
     TASK1_PRIORITY,               // Task ID
     &task1_stk[0],                // Pointer to bottom of task stack
     TASK_STACKSIZE,               // Stacksize
     NULL,                         // Pointer to user supplied memory
                                   // (not needed here)
     OS_TASK_OPT_STK_CHK |         // Stack Checking enabled 
     OS_TASK_OPT_STK_CLR           // Stack Cleared                       
    );  
    
  if (DEBUG == 1)
  {
    OSTaskCreateExt
      (statisticTask,                // Pointer to task code
       NULL,                         // Pointer to argument that is
                                     // passed to task
       &stat_stk[TASK_STACKSIZE-1],  // Pointer to top of task stack
       TASK_STAT_PRIORITY,           // Desired Task priority
       TASK_STAT_PRIORITY,           // Task ID
       &stat_stk[0],                 // Pointer to bottom of task stack
       TASK_STACKSIZE,               // Stacksize
       NULL,                         // Pointer to user supplied memory
                                     // (not needed here)
       OS_TASK_OPT_STK_CHK |         // Stack Checking enabled 
       OS_TASK_OPT_STK_CLR           // Stack Cleared                              
      );
  }  
  
  OSStart();
  return 0;
}
