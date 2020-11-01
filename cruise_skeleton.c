#include <stdio.h>
#include "system.h"
#include "includes.h"
#include "altera_avalon_pio_regs.h"
#include "sys/alt_irq.h"
#include "sys/alt_alarm.h"

#define DEBUG 0

#define HW_TIMER_PERIOD 100 /* 100ms */

/*Flag Group*/
OS_FLAG_GRP *EngineStatus;
/* Button Patterns */

#define GAS_PEDAL_FLAG      0x08
#define BRAKE_PEDAL_FLAG    0x04
#define CRUISE_CONTROL_FLAG 0x02
/* Switch Patterns */

#define TOP_GEAR_FLAG       0x00000010
#define ENGINE_FLAG         0x00000001

/* LED Patterns */

#define LED_RED_0 0x00000001 // Engine
#define LED_RED_1 0x00000002 // Top Gear

#define LED_GREEN_0 0x0001 // Cruise Control activated
#define LED_GREEN_2 0x0002 // Cruise Control Button
#define LED_GREEN_4 0x0010 // Brake Pedal
#define LED_GREEN_6 0x0040 // Gas Pedal

/*
 * Definition of Tasks
 */

#define TASK_STACKSIZE 2048

OS_STK StartTask_Stack[TASK_STACKSIZE]; 
OS_STK ControlTask_Stack[TASK_STACKSIZE]; 
OS_STK VehicleTask_Stack[TASK_STACKSIZE];
OS_STK ButtonIOTask_Stack[TASK_STACKSIZE];
OS_STK SwitchIOTask_Stack[TASK_STACKSIZE];
OS_STK ExtraLoadTask_Stack[TASK_STACKSIZE];
OS_STK WatchdogTask_Stack[TASK_STACKSIZE];
OS_STK OverloadTask_Stack[TASK_STACKSIZE];
OS_STK ShowCPUTask_Stack[TASK_STACKSIZE];
OS_STK stat_stk[TASK_STACKSIZE];

// Task Priorities
 
#define STARTTASK_PRIO     5
#define WATCHDOGTASK_PRIO  6
#define ShowCPUTASK_RPIO  7
#define VEHICLETASK_PRIO  10
#define CONTROLTASK_PRIO  12
#define BUTTONTASK_PRIO  14
#define SWITCHTASK_PRIO  15
#define EXTRALOADTASK_PRIO 16
#define OVERLOADTASK_RPIO 17
#define TASK_STAT_PRIORITY 18

// Task Periods

#define CONTROL_PERIOD  300
#define VEHICLE_PERIOD  300

/*
 * Definition of Kernel Objects 
 */

// Mailboxes
OS_EVENT *Mbox_Throttle;
OS_EVENT *Mbox_Velocity;

// Semaphores
OS_EVENT *VehicleSem;
OS_EVENT *ControlSem;
OS_EVENT *OK;
OS_EVENT *ShowCPUSem;

// SW-Timer
OS_TMR *VehicleTmr;
OS_TMR *ControlTmr;
OS_TMR *ShowCPUTmr;

/*
 * Types
 */
enum active {on, off};

enum active gas_pedal = off;
enum active brake_pedal = off;
enum active top_gear = off;
enum active engine = off;
enum active cruise_control = off; 

/*
 * Global variables
 */
int delay; // Delay of HW-timer 
INT16U led_green = 0; // Green LEDs
INT32U led_red = 0;   // Red LEDs

/*
 * Debug
 */
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
void statisticTask(void* pdata)
{
    while(1)
    {
        printStackSize(TASK_STAT_PRIORITY);
        printStackSize(STARTTASK_PRIO);
        printStackSize(WATCHDOGTASK_PRIO);
        printStackSize(ShowCPUTASK_RPIO);
        printStackSize(VEHICLETASK_PRIO);
        printStackSize(CONTROLTASK_PRIO);
        printStackSize(BUTTONTASK_PRIO);
        printStackSize(SWITCHTASK_PRIO);
        printStackSize(EXTRALOADTASK_PRIO);
        printStackSize(OVERLOADTASK_RPIO);
        // OSTimeDlyHMSM(0, 0, 0, 4);
    }
}

void SemPostFunc (void *ptmr, OS_EVENT *semptr)
{
  OSSemPost(semptr);
}

int buttons_pressed(void)
{
  return ~IORD_ALTERA_AVALON_PIO_DATA(DE2_PIO_KEYS4_BASE);    
}

int switches_pressed(void)
{
  return IORD_ALTERA_AVALON_PIO_DATA(DE2_PIO_TOGGLES18_BASE);    
}

/*
 * ISR for HW Timer
 */
alt_u32 alarm_handler(void* context)
{
  OSTmrSignal(); /* Signals a 'tick' to the SW timers */
  
  return delay;
}

static int b2sLUT[] = {0x40, //0
                 0x79, //1
                 0x24, //2
                 0x30, //3
                 0x19, //4
                 0x12, //5
                 0x02, //6
                 0x78, //7
                 0x00, //8
                 0x18, //9
                 0x3F, //-
};

/*
 * convert int to seven segment display format
 */
int int2seven(int inval){
    return b2sLUT[inval];
}

/*
 * output current velocity on the seven segement display
 */
void show_velocity_on_sevenseg(INT8S velocity){
  int tmp = velocity;
  int out;
  INT8U out_high = 0;
  INT8U out_low = 0;
  INT8U out_sign = 0;

  if(velocity < 0){
     out_sign = int2seven(10);
     tmp *= -1;
  }else{
     out_sign = int2seven(0);
  }

  out_high = int2seven(tmp / 10);
  out_low = int2seven(tmp - (tmp/10) * 10);
  
  out = int2seven(0) << 21 |
            out_sign << 14 |
            out_high << 7  |
            out_low;
  IOWR_ALTERA_AVALON_PIO_DATA(DE2_PIO_HEX_LOW28_BASE,out);
}

/*
 * shows the target velocity on the seven segment display (HEX5, HEX4)
 * when the cruise control is activated (0 otherwise)
 */
void show_target_velocity(INT8U target_vel)
{
  int tmp = target_vel;
  int out;
  INT8U out_high = 0;
  INT8U out_low = 0;


  out_high = int2seven(tmp / 10);
  out_low = int2seven(tmp - (tmp/10) * 10);
  
  out = int2seven(0) << 21 |
        int2seven(0) << 14 |
            out_high << 7  |
            out_low;
  IOWR_ALTERA_AVALON_PIO_DATA(DE2_PIO_HEX_HIGH28_BASE,out);
}

/*
 * indicates the position of the vehicle on the track with the four leftmost red LEDs
 * LEDR17: [0m, 400m)
 * LEDR16: [400m, 800m)
 * LEDR15: [800m, 1200m)
 * LEDR14: [1200m, 1600m)
 * LEDR13: [1600m, 2000m)
 * LEDR12: [2000m, 2400m]
 */
void show_position(INT16U position)
{
  INT16U temp=position;
  INT32U out=1,tmp;
  if(temp<4000)
    tmp=out<<17; 
  else if(temp<8000)
    tmp=out<<16;
    else if(temp<12000)
      tmp=out<<15;
      else if(temp<16000)
        tmp=out<<14;
        else if(temp<20000)
          tmp=out<<13;
          else
            tmp=out<<12;
    led_red=(0x3ff&led_red)|(0xffc00&tmp);
    IOWR_ALTERA_AVALON_PIO_DATA(DE2_PIO_REDLED18_BASE,led_red);  
}

/*
 * The function 'adjust_position()' adjusts the position depending on the
 * acceleration and velocity.
 */
 INT16U adjust_position(INT16U position, INT16S velocity,
                        INT8S acceleration, INT16U time_interval)
{
  INT16S new_position = position + velocity * time_interval / 1000
    + acceleration / 2  * (time_interval / 1000) * (time_interval / 1000);

  if (new_position > 24000) {
    new_position -= 24000;
  } else if (new_position < 0){
    new_position += 24000;
  }
  
  show_position(new_position);
  return new_position;
}
 
/*
 * The function 'adjust_velocity()' adjusts the velocity depending on the
 * acceleration.
 */
INT16S adjust_velocity(INT16S velocity, INT8S acceleration,  
		       enum active brake_pedal, INT16U time_interval)
{
  INT16S new_velocity;
  INT8U brake_retardation = 200;

  if (brake_pedal == off)
    new_velocity = velocity  + (float) (acceleration * time_interval) / 1000.0;
  else {
    if (brake_retardation * time_interval / 1000 > velocity)
      new_velocity = 0;
    else
      new_velocity = velocity - brake_retardation * time_interval / 1000;
  }
  
  return new_velocity;
}

/*
 * The task 'VehicleTask' updates the current velocity of the vehicle
 */
void VehicleTask(void* pdata)
{ 
  INT8U err;  
  void* msg;
  INT8U* throttle; 
  INT8S acceleration;  /* Value between 40 and -20 (4.0 m/s^2 and -2.0 m/s^2) */
  INT8S retardation;   /* Value between 20 and -10 (2.0 m/s^2 and -1.0 m/s^2) */
  INT16U position = 0; /* Value between 0 and 20000 (0.0 m and 2000.0 m)  */
  INT16S velocity = 0; /* Value between -200 and 700 (-20.0 m/s amd 70.0 m/s) */
  INT16S wind_factor;   /* Value between -10 and 20 (2.0 m/s^2 and -1.0 m/s^2) */

  printf("Vehicle task created!\n");

  while(1)
    {
      err = OSMboxPost(Mbox_Velocity, (void *) &velocity);

      // OSTimeDlyHMSM(0,0,0,VEHICLE_PERIOD); 
      OSSemPend(VehicleSem,0,&err);

      /* Non-blocking read of mailbox: 
	   - message in mailbox: update throttle
	   - no message:         use old throttle
      */
      msg = OSMboxPend(Mbox_Throttle, 1, &err); 
      if (err == OS_NO_ERR) 
	     throttle = (INT8U*) msg;

      /* Retardation : Factor of Terrain and Wind Resistance */
      if (velocity > 0)
	     wind_factor = velocity * velocity / 10000 + 1;
      else 
	     wind_factor = (-1) * velocity * velocity / 10000 + 1;
         
      if (position < 4000) 
         retardation = wind_factor; // even ground
      else if (position < 8000)
          retardation = wind_factor + 15; // traveling uphill
        else if (position < 12000)
            retardation = wind_factor + 25; // traveling steep uphill
          else if (position < 16000)
              retardation = wind_factor; // even ground
            else if (position < 20000)
                retardation = wind_factor - 10; //traveling downhill
              else
                  retardation = wind_factor - 5 ; // traveling steep downhill
                  
      acceleration = *throttle / 2 - retardation;	  
      position = adjust_position(position, velocity, acceleration, 300); 
      velocity = adjust_velocity(velocity, acceleration, brake_pedal, 300); 
      printf("Position: %dm\n", position / 10);
      printf("Velocity: %4.1fm/s\n", velocity /10.0);
      printf("Throttle: %dV\n", *throttle / 10);
      show_velocity_on_sevenseg((INT8S) (velocity / 10));
    }
} 
 
/*
 *  Incremented PID for speed control
 */
struct _pid{
INT16U SetSpeed;     //定义设定值
INT16U ActualSpeed;    //定义实际值
INT16S err;        //定义偏差值
INT16S err_last;     //定义上一个偏差值
float Kp,Ki,Kd;     //定义比例、积分、微分系数
INT16S voltage;      //定义电压值（控制执行器的变量）
INT16S integral;       //定义积分值
}pid;

//项目中获取到的参数
void PID_init(){
  printf("PID_init begin \n");
  pid.SetSpeed=0;
  // pid.ActualSpeed=0;
  pid.err=0;
  pid.err_last=0;
  pid.voltage=0;
  pid.integral=0;
  pid.Kp=20;       //自己设定
  pid.Ki=0.08;     //自己设定
  pid.Kd=0.2;       //自己设定
  printf("PID_init end \n");
}

INT16S PID_realize(INT16U speed, INT16U velocity){
  pid.SetSpeed=speed;           //设定值
  pid.err=pid.SetSpeed-velocity; //设定值-实际值
  pid.integral+=pid.err;          //积分值，偏差累加
  pid.voltage=pid.Kp*pid.err+pid.Ki*pid.integral+pid.Kd*(pid.err-pid.err_last);
  pid.err_last=pid.err;         //上一个偏差值
  // pid.ActualSpeed=pid.voltage*1.0;    //算出实际值
  return pid.voltage;         //返回
}


/*
 * The task 'ControlTask' is the main task of the application. It reacts
 * on sensors and generates responses.
 */

void ControlTask(void* pdata)
{
  INT8U err;
  INT8U throttle = 0; /* Value between 0 and 80, which is interpreted as between 0.0V and 8.0V */
  void* msg;
  INT16S* current_velocity;
  INT16U target_vel;
  INT16S div=0,count=0;
  INT32S throttleCul,slope;
  INT32U countercruise=0;
  OS_FLAGS value;

  printf("Control Task created!\n");
  PID_init();
  while(1)
    {
     msg = OSMboxPend(Mbox_Velocity, 0, &err);
      current_velocity = (INT16S*) msg;
      /*
       * Engine control
       */
      value = OSFlagPend(EngineStatus,
        ENGINE_FLAG,
        OS_FLAG_WAIT_SET_ALL,
        1,
        &err);
    switch (err) {
      case OS_ERR_NONE:
      engine=on;
      break;
      case OS_ERR_TIMEOUT:
      // engine=off;
      break;
    }
    // printf("Velocity in control: %4.1fm/s\n", *current_velocity /10.0);
    if(*current_velocity==0)
    {
      value = OSFlagPend(EngineStatus,
        ENGINE_FLAG,
        OS_FLAG_WAIT_CLR_ALL,
        1,
        &err);
    switch (err) {
      case OS_ERR_NONE:
      engine=off;
      break;
      case OS_ERR_TIMEOUT:
      break;
    }
    }
    if(engine==on)
    {

      // throttle=40;
      if(gas_pedal==on)
        throttle++;
      else if(throttle!=0&&cruise_control==off)
        throttle--;
      // else
      //   throttle=0;
      
      err = OSMboxPost(Mbox_Throttle, (void *) &throttle);
    /*
     * Cruise control
     */
    if(*current_velocity>=200)
    {
     value = OSFlagPend(EngineStatus,
        CRUISE_CONTROL_FLAG+TOP_GEAR_FLAG,
        OS_FLAG_WAIT_SET_ALL,
        1,
        &err);
    if(err==OS_ERR_NONE)
    {
        value = OSFlagPend(EngineStatus,
        GAS_PEDAL_FLAG+BRAKE_PEDAL_FLAG,
        OS_FLAG_WAIT_CLR_ALL,
        1,
        &err);
      switch (err) {
      case OS_ERR_NONE:
      // printf("cruse control on\n");
      cruise_control=on;
      countercruise++;
      break;
      case OS_ERR_TIMEOUT:
      // printf("gas pedal or brake doesn't release\n");
      cruise_control=off;
      countercruise=0;
      break;
    }
    }
      else
      {
      // printf("cruise or top gear not ready\n");
      cruise_control=off;
      countercruise=0;
      }  
    }
    else
    {
      // printf("velocity not enough\n");
      cruise_control=off;
      countercruise=0;
    }
    if(cruise_control==on&&countercruise==1)
    {
      target_vel=*current_velocity;
    }
    if(cruise_control==on)
    {
      count=PID_realize(target_vel,*current_velocity);
      // printf("Actuator %d\n", count);
      // throttle=throttle+10;
      led_green=(0xfe&led_green)|(0x01);
      show_target_velocity((INT8U)(target_vel/10));
      // switch (led_red&0xffff0) {
      //   case 0x20000:
      //   // throttle=target_vel*target_vel/5000 + 2 + div;
      //   slope=0;
      //   break;
      //   case 0x10000:
      //   // throttle=target_vel*target_vel/5000 + 32 + div;
      //   slope=15;
      //   break;
      //   case 0x08000:
      //   slope=25;
      //   // throttle=target_vel*target_vel/5000 + 52 + div;
      //   break;
      //   case 0x04000:
      //   slope=0;
      //   // throttle=target_vel*target_vel/5000 + 2 + div;
      //   break;
      //   case 0x02000:
      //   slope=-10;
      //   // throttle=0;
      //   break;
      //   case 0x01000:
      //   slope=-5;
      //   // throttle=target_vel*target_vel/5000 - 8 + div;
      //   // if(throttle>80)
      //   //   throttle=0;
      //   break;
      // }
      // if(div>=15)
      // div=15;
      // if(div<=-15)
      // div=-15;
      throttleCul=2*((count*count)/10000+1);
      // printf("throttle needed %d\n", throttleCul);
      if (throttleCul<0)
        throttle=0;
      else if(throttleCul>80)
        throttle=80;
      else
        throttle=throttleCul; 
      // if(*current_velocity<target_vel)
      // {
      //   // count++;
      //   // if(count%3==0)
      //   // div++;
      //   throttle++;
      // }
      // if(*current_velocity>target_vel)
      // {
      //   // count++;
      //   // if(count%3==0)
      //   // div--;
      //   throttle--;
      // }
    }
    else
      led_green=(0xfe&led_green)|(0x00);
    if(cruise_control==off)
      show_target_velocity(0);
    /*
     * Gas Pedal
     */
    value = OSFlagPend(EngineStatus,
        GAS_PEDAL_FLAG,
        OS_FLAG_WAIT_SET_ALL,
        1,
        &err);
    switch (err) {
      case OS_ERR_NONE:
      gas_pedal=on;
      cruise_control=off;
      break;
      case OS_ERR_TIMEOUT:
      gas_pedal=off;
      break;
    }
    /*
     * Brake
     */
    value = OSFlagPend(EngineStatus,
        BRAKE_PEDAL_FLAG,
        OS_FLAG_WAIT_SET_ALL,
        1,
        &err);
    switch (err) {
      case OS_ERR_NONE:
      brake_pedal=on;
      cruise_control=off;
      break;
      case OS_ERR_TIMEOUT:
      brake_pedal=off;
      break;
    }
    /*
     * Gear
     */
    value = OSFlagPend(EngineStatus,
        TOP_GEAR_FLAG,
        OS_FLAG_WAIT_SET_ALL,
        1,
        &err);
    switch (err) {
      case OS_ERR_NONE:
      top_gear=on;
      // cruise_control=off;
      break;
      case OS_ERR_TIMEOUT:
      top_gear=off;
      break;
    }
      // OSTimeDlyHMSM(0,0,0, CONTROL_PERIOD);
    // if(cruise_control==off&&gas_pedal==off)
    //   throttle=0;
    }
    else
    {
      if(cruise_control==off&&gas_pedal==off)
      throttle=0; 
      err = OSMboxPost(Mbox_Throttle, (void *) &throttle); 
    }
    if (throttle>80)
    {
      throttle=80;
    }
    OSSemPend(ControlSem,0,&err);
    }
}
/*
 *  Overload Detection and Watchdog
 */
void ShowCPUUsage(void* pdata)
{
  INT8U err;
  while(1)
  {
    OSSemPend(ShowCPUSem,0,&err);
    // printf("OSIdleCtr: %d\n", OSIdleCtr);
    // printf("OSIdleCtrMax: %d\n", OSIdleCtrMax);
    printf("CPU usage is %ld%%\n", OSCPUUsage);
  }
}
void Watchdog(void* pdata)
{
  INT8U err;
  while(1)
  {
    OSSemPend(OK,300,&err);
    if(err==OS_ERR_TIMEOUT)
      printf("System Overload---------------------------------\n");
  }
}
void OverloadDetection(void* pdata)
{
  INT8U err;
  while(1)
  {
  OSTimeDlyHMSM(0,0,0,290);
  OSSemPost(OK);
  
  }
}
void ExtraLoad(void* pdata)
{
  INT16U usage;
  int x,i,j;
  while(1)
  {
  usage=((led_red&0x3f0)>>4)*2;
  if (usage>100)
  {
    usage=100;
  }
  if(usage>0)
  for (i = 0; i < usage; ++i)
  {
    for (j = 0; j <410; ++j)
    {
      x=j*i;
      // printf("%d\n", OSCPUUsage);
    }
  }
  printf("%d\n", usage);
  OSTimeDlyHMSM(0,0,0,300);
  }
}

/*
 * SwitchIO task reads Switch periodically.
 */
void ButtonIOTask(void* pdata)
{
  INT8U err;
  INT8U temp,temp1,temp2,temp3,temp4;
  OS_FLAGS flags;
  while(1)
  {
    temp=0x0f&buttons_pressed();
    temp1=temp&GAS_PEDAL_FLAG;

    temp2=temp&BRAKE_PEDAL_FLAG;

    temp3=temp&CRUISE_CONTROL_FLAG;
    
    if(temp1==GAS_PEDAL_FLAG)
      err = OSFlagPost(EngineStatus,GAS_PEDAL_FLAG,OS_FLAG_SET,&err);
    else
      err = OSFlagPost(EngineStatus,GAS_PEDAL_FLAG,OS_FLAG_CLR,&err);
    if(temp2==BRAKE_PEDAL_FLAG)
      err = OSFlagPost(EngineStatus,BRAKE_PEDAL_FLAG,OS_FLAG_SET,&err);
    else
      err = OSFlagPost(EngineStatus,BRAKE_PEDAL_FLAG,OS_FLAG_CLR,&err);
    if(temp3==CRUISE_CONTROL_FLAG)
      err = OSFlagPost(EngineStatus,CRUISE_CONTROL_FLAG,OS_FLAG_SET,&err);
    else
      err = OSFlagPost(EngineStatus,CRUISE_CONTROL_FLAG,OS_FLAG_CLR,&err);
    // flags=OSFlagQuery(EngineStatus, &err);
    // temp=(INT8U *)flags;
    temp1=temp1*temp1;
    temp2=temp2*temp2;
    temp3=temp3*temp3;
    // temp=IORD_ALTERA_AVALON_PIO_DATA(DE2_PIO_GREENLED9_BASE); 
    temp=temp1|temp2|temp3;
    // temp=temp*temp;
    // printf("NO KEY?!!\n");
    led_green=(0x01&led_green)|(0xfe&temp);
    IOWR_ALTERA_AVALON_PIO_DATA(DE2_PIO_GREENLED9_BASE,led_green);
    OSTimeDlyHMSM(0,0,0, 100);
  }
}

/*
 * ButtonIO task reads Botton periodically.
 */
void SwitchIOTask(void* pdata)
{
  INT8U err;
  INT32U temp,temp1,temp2;
  while(1)
  {
    temp=switches_pressed();
    temp1=temp&ENGINE_FLAG;
    temp2=temp&0x02;
    if(temp1==ENGINE_FLAG)
    err = OSFlagPost(EngineStatus,ENGINE_FLAG,OS_FLAG_SET,&err); 
    else
    err = OSFlagPost(EngineStatus,ENGINE_FLAG,OS_FLAG_CLR,&err); 
    if(temp2==0x02)
    err = OSFlagPost(EngineStatus,TOP_GEAR_FLAG,OS_FLAG_SET,&err);
    else
    err = OSFlagPost(EngineStatus,TOP_GEAR_FLAG,OS_FLAG_CLR,&err); 
    // switch (err) {
    // case OS_ERR_NONE:
    // // printf("topgear set\n");
    // break;
    // case OS_ERR_FLAG_INVALID_PGRP:
    // printf("OS_ERR_FLAG_INVALID_PGRP\n");
    // break;
    // case OS_ERR_EVENT_TYPE:
    // printf("OS_ERR_EVENT_TYPE\n");
    // break;
    // case OS_ERR_FLAG_INVALID_OPT:
    // printf("OS_ERR_FLAG_INVALID_OPT\n");
    // break;
    // }
    led_red=(0xffc00&led_red)|(0x3ff&temp);
    IOWR_ALTERA_AVALON_PIO_DATA(DE2_PIO_REDLED18_BASE,led_red);
    OSTimeDlyHMSM(0,0,0, 10);
  }
}
/* 
 * The task 'StartTask' creates all other tasks kernel objects and
 * deletes itself afterwards.
 */ 

void StartTask(void* pdata)
{
  INT8U err;
  void* context;
  BOOLEAN status;

  static alt_alarm alarm;     /* Is needed for timer ISR function */
  
  /* Base resolution for SW timer : HW_TIMER_PERIOD ms */
  delay = alt_ticks_per_second() * HW_TIMER_PERIOD / 1000; 
  printf("delay in ticks %d\n", delay);

  /* 
   * Create Hardware Timer with a period of 'delay' 
   */
  if (alt_alarm_start (&alarm,
      delay,
      alarm_handler,
      context) < 0)
      {
          printf("No system clock available!n");
      }
  /*
   * Create Semaphores
   */
      VehicleSem = OSSemCreate(0);
      ControlSem = OSSemCreate(0);
      OK = OSSemCreate(0);
      ShowCPUSem=OSSemCreate(0);

  /* 
   * Create and start Software Timer 
   */
  VehicleTmr = OSTmrCreate(  0,
                           VEHICLE_PERIOD/100,
                           OS_TMR_OPT_PERIODIC,
                           SemPostFunc,
                           VehicleSem,
                           "Release Vehicle",
                           &err);
  ControlTmr = OSTmrCreate(  0,
                           CONTROL_PERIOD/100,
                           OS_TMR_OPT_PERIODIC,
                           SemPostFunc,
                           ControlSem,
                           "Release Vehicle",
                           &err);
  ShowCPUTmr = OSTmrCreate(  0,
                           5,
                           OS_TMR_OPT_PERIODIC,
                           SemPostFunc,
                           ShowCPUSem,
                           "Release ShowCPU",
                           &err);
  status = OSTmrStart(VehicleTmr,
                      &err);
  status = OSTmrStart(ControlTmr,
                      &err);
  status = OSTmrStart(ShowCPUTmr,
                      &err);
  /*
   * Creation of Kernel Objects
   */
  
  // Mailboxes
  Mbox_Throttle = OSMboxCreate((void*) 0); /* Empty Mailbox - Throttle */
  Mbox_Velocity = OSMboxCreate((void*) 0); /* Empty Mailbox - Velocity */
   
  /*
   * Create statistics task
   */
  OSStatInit();

  EngineStatus = OSFlagCreate(0x00, &err);//Create a flag
  printf("%0x\n", err);
  /* 
   * Creating Tasks in the system 
   */


  err = OSTaskCreateExt(
	   ControlTask, // Pointer to task code
	   NULL,        // Pointer to argument that is
	                // passed to task
	   &ControlTask_Stack[TASK_STACKSIZE-1], // Pointer to top
							 // of task stack
	   CONTROLTASK_PRIO,
	   CONTROLTASK_PRIO,
	   (void *)&ControlTask_Stack[0],
	   TASK_STACKSIZE,
	   (void *) 0,
	   OS_TASK_OPT_STK_CHK);

  err = OSTaskCreateExt(
	   VehicleTask, // Pointer to task code
	   NULL,        // Pointer to argument that is
	                // passed to task
	   &VehicleTask_Stack[TASK_STACKSIZE-1], // Pointer to top
							 // of task stack
	   VEHICLETASK_PRIO,
	   VEHICLETASK_PRIO,
	   (void *)&VehicleTask_Stack[0],
	   TASK_STACKSIZE,
	   (void *) 0,
	   OS_TASK_OPT_STK_CHK);

  err = OSTaskCreateExt(
     ButtonIOTask,
     NULL,
     &ButtonIOTask_Stack[TASK_STACKSIZE-1],
     BUTTONTASK_PRIO,
     BUTTONTASK_PRIO,
     (void *)&ButtonIOTask_Stack[0],
     TASK_STACKSIZE,
     (void *)0,
     OS_TASK_OPT_STK_CHK);

  err = OSTaskCreateExt(
     SwitchIOTask,
     NULL,
     &SwitchIOTask_Stack[TASK_STACKSIZE-1],
     SWITCHTASK_PRIO,
     SWITCHTASK_PRIO,
     (void *)&SwitchIOTask_Stack[0],
     TASK_STACKSIZE,
     (void *)0,
     OS_TASK_OPT_STK_CHK);
  err = OSTaskCreateExt(
     ExtraLoad,
     NULL,
     &ExtraLoadTask_Stack[TASK_STACKSIZE-1],
     EXTRALOADTASK_PRIO,
     EXTRALOADTASK_PRIO,
     (void *)&ExtraLoadTask_Stack[0],
     TASK_STACKSIZE,
     (void *)0,
     OS_TASK_OPT_STK_CHK);
  err = OSTaskCreateExt(
     Watchdog,
     NULL,
     &WatchdogTask_Stack[TASK_STACKSIZE-1],
     WATCHDOGTASK_PRIO,
     WATCHDOGTASK_PRIO,
     (void *)&WatchdogTask_Stack[0],
     TASK_STACKSIZE,
     (void *)0,
     OS_TASK_OPT_STK_CHK);
  err = OSTaskCreateExt(
     OverloadDetection,
     NULL,
     &OverloadTask_Stack[TASK_STACKSIZE-1],
     OVERLOADTASK_RPIO,
     OVERLOADTASK_RPIO,
     (void *)&OverloadTask_Stack[0],
     TASK_STACKSIZE,
     (void *)0,
     OS_TASK_OPT_STK_CHK);
  err = OSTaskCreateExt(
     ShowCPUUsage,
     NULL,
     &ShowCPUTask_Stack[TASK_STACKSIZE-1],
     ShowCPUTASK_RPIO,
     ShowCPUTASK_RPIO,
     (void *)&ShowCPUTask_Stack[0],
     TASK_STACKSIZE,
     (void *)0,
     OS_TASK_OPT_STK_CHK);
  if (DEBUG == 1)
  {
    err= OSTaskCreateExt
      (statisticTask,                // Pointer to task code
       NULL,                         // Pointer to argument that is
                                     // passed to task
       &stat_stk[TASK_STACKSIZE-1],  // Pointer to top of task stack
       TASK_STAT_PRIORITY,           // Desired Task priority
       TASK_STAT_PRIORITY,           // Task ID
       &stat_stk[0],                 // Pointer to bottom of task stack
       TASK_STACKSIZE,               // Stacksize
       (void *)0,                         // Pointer to user supplied memory
                                     // (not needed here)
       OS_TASK_OPT_STK_CHK |         // Stack Checking enabled 
       OS_TASK_OPT_STK_CLR           // Stack Cleared                              
      );
      if(err==OS_ERR_NONE)
        printf("Static Task Created\n");
  }  
  

  printf("All Tasks and Kernel Objects generated!\n");

  /* Task deletes itself */

  OSTaskDel(OS_PRIO_SELF);
}

/*
 *
 * The function 'main' creates only a single task 'StartTask' and starts
 * the OS. All other tasks are started from the task 'StartTask'.
 *
 */

int main(void) {

  printf("Lab: Cruise Control\n");
  // OSInit();
  OSTaskCreateExt(
	 StartTask, // Pointer to task code
         NULL,      // Pointer to argument that is
                    // passed to task
         (void *)&StartTask_Stack[TASK_STACKSIZE-1], // Pointer to top
						     // of task stack 
         STARTTASK_PRIO,
         STARTTASK_PRIO,
         (void *)&StartTask_Stack[0],
         TASK_STACKSIZE,
         (void *) 0,  
         OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR);
         
  OSStart();
  
  return 0;
}
