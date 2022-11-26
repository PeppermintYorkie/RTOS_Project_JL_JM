// RTOS Framework - Fall 2022
// J Losh

// Student Name: Jackson Liller & Joanne Mathew
// TO DO: Add your name(s) on this line.
//        Do not include your ID number(s) in the file.

// Please do not change any function name in this code or the thread priorities

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: EK-TM4C123GXL Evaluation Board
// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz

// Hardware configuration:
// 6 Pushbuttons and 5 LEDs, UART
// LEDs on these pins:
// Blue:   PF2 (on-board)
// Red:    PE0 (lengthy and general)
// Orange: PA2 (idle)
// Yellow: PA3 (oneshot and general)
// Green:  PA4 (flash4hz)
// PBs on these pins
// PB0:    PD6 (set red, toggle yellow)
// PB1:    PD7 (clear red, post flash_request semaphore)
// PB2:    PC4 (restart flash4hz)
// PB3:    PC5 (stop flash4hz, uncoop)
// PB4:    PC6 (lengthy priority increase)
// PB5:    PC7 (errant)
// UART Interface:
//   U0TX (PA1) and U0RX (PA0) are connected to the 2nd controller
//   The USB on the 2nd controller enumerates to an ICDI interface and a virtual COM port
//   Configured to 115,200 baud, 8N1
// Memory Protection Unit (MPU):
//   Region to allow peripheral access (RW) or a general all memory access (RW)
//   Region to allow flash access (XRW)
//   Regions to allow 32 1KiB SRAM access (RW or none)

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#include <stdint.h>
#include <stdbool.h>
#include "tm4c123gh6pm.h"
#include "clock.h"
#include "gpio.h"
#include "uart0.h"
#include "wait.h"
#include "shell.h"
#include "kernel.h"
// TODO: Add header files here for your strings functions, ... - DONE JL & JM, 10/31

#define BLUE_LED   PORTF,2          // on-board blue LED
#define RED_LED    PORTE,0          // off-board red LED
#define ORANGE_LED PORTA,2          // off-board orange LED
#define YELLOW_LED PORTA,3          // off-board yellow LED
#define GREEN_LED  PORTA,4          // off-board green LED

#define PB0     PORTD,6             // Off-board pushbutton 0
#define PB1     PORTD,7             // Off-board pushbutton 1
#define PB2     PORTC,4             // Off-board pushbutton 2
#define PB3     PORTC,5             // Off-board pushbutton 3
#define PB4     PORTC,6             // Off-board pushbutton 4
#define PB5     PORTC,7             // Off-board pushbutton 5

#define YIELD 0
#define SLEEP 1
#define WAIT  2
#define POST  3

#define TOP_OF_SRAM 0x20008000      // A macro for the top of SRAM memory

extern void setTMPLbit(void);
extern void setASPbit(void);
extern void setPSP(uint32_t *stack);
extern uint32_t* getPSP(void);
extern uint32_t* getMSP(void);
extern uint32_t* pushContext(uint32_t *stack);
extern uint32_t* popContext(uint32_t *stack);
extern void fabricateContext(void *pid);
extern uint32_t extractSVC(uint32_t *stack);

//-----------------------------------------------------------------------------
// RTOS Defines and Kernel Variables
//-----------------------------------------------------------------------------

// function pointer
typedef void (*_fn)();

// semaphore
#define MAX_SEMAPHORES 5
#define MAX_QUEUE_SIZE 5
typedef struct _semaphore
{
    uint16_t count;
    uint16_t queueSize;
    uint32_t processQueue[MAX_QUEUE_SIZE]; // store task index here
} semaphore;

semaphore semaphores[MAX_SEMAPHORES];
#define keyPressed 1
#define keyReleased 2
#define flashReq 3
#define resource 4

// task
#define STATE_INVALID    0 // no task
#define STATE_UNRUN      1 // task has never been run
#define STATE_READY      2 // has run, can resume at any time
#define STATE_DELAYED    3 // has run, but now awaiting timer
#define STATE_BLOCKED    4 // has run, but now blocked by semaphore

#define MAX_TASKS 12       // maximum number of valid tasks
uint8_t taskCurrent = 0;   // index of last dispatched task
uint8_t taskCount = 0;     // total number of valid tasks

// REQUIRED: add store and management for the memory used by the thread stacks
//           thread stacks must start on 1 kiB boundaries so mpu can work correctly

struct _tcb
{
    uint8_t state;                 // see STATE_ values above
    void *pid;                     // used to uniquely identify thread
    void *spInit;                  // original top of stack
    void *sp;                      // current stack pointer
    int8_t priority;               // 0=highest to 7=lowest
    uint32_t ticks;                // ticks until sleep complete
    uint32_t srd;                  // MPU subregion disable bits (one per 1 KiB)
    char name[16];                 // name of task used in ps command
    void *semaphore;               // pointer to the semaphore that is blocking the thread
} tcb[MAX_TASKS];


//-----------------------------------------------------------------------------
// Memory Manager and MPU Funcitons
//-----------------------------------------------------------------------------

// TODO: add your malloc code here and update the SRD bits for the current thread 
// mallocFromHeap() completed JL 11/13
void * mallocFromHeap(uint32_t size_in_bytes)
{
    void * ptr = 0;
    static uint32_t * heap = (uint32_t *)0x20001800;                        // A statically instantiated heap location, is given memory and never dies, but only touchable in this scope (Losh advisory) -JL
    size_in_bytes = (((size_in_bytes-1)/1024)+1)*1024;
    heap += (size_in_bytes / 4);
    if(heap >= (uint32_t *)0x20008000)
    {
        ptr = 0;
    }
    else
    {
        ptr = heap;
    }

    uint32_t baseAddr = (uint32_t) ptr;
    uint32_t srdBits = 0x00000000;                                       // We'll need this later
    uint8_t subRegion0 = ((baseAddr - 0x20000000) / 0x400) - 1;       // This is the first sub region we will enable.
    uint8_t numSubregions = size_in_bytes / 0x400;              // This is the number of subregions we will need to cover the request
    if(size_in_bytes % 0x400 != 0)
        numSubregions++;                                        // Users don't care about sub region size. Make sure they get all the memory they need, even if it's more than they need. Hell, give them all of the memory, see if I care.
    uint8_t i = 0;
    for(i = 0; i < numSubregions; i++)
    {
        srdBits |= (1 << (subRegion0 + i));                     // This SHOULD fill srdBits with 1s where we want to disable subregion protection, i.e. give access to a program calling this function
    }

    tcb[taskCurrent].srd |= srdBits;

    srdBits = tcb[taskCurrent].srd;

    NVIC_MPU_NUMBER_R &= ~(0b111);                          // Setting SRD bits for region 3 (first SRAM region)
    NVIC_MPU_NUMBER_R |= 0b011;                              // Let's look at region 3
    NVIC_MPU_ATTR_R &= ~(0x000000FF << 8);                        //Disable unneeded subregions
    NVIC_MPU_ATTR_R |= ((0xFF & srdBits) << 8);             // Grabs the SRD bits for region 3, shifts them into position, and writes to attribute register.

    srdBits = srdBits >> 8;                                 // Banish the bits we are done with!

    NVIC_MPU_NUMBER_R &= ~(0b111);                          // Setting SRD bits for region 4 (second SRAM region)
    NVIC_MPU_NUMBER_R |= 0b100;                              // Let's look at region 4
    NVIC_MPU_ATTR_R &= ~(0x000000FF << 8);                        //Disable unneeded subregions
    NVIC_MPU_ATTR_R |= ((0xFF & srdBits) << 8);             // Grabs the SRD bits for region 4, shifts them into position, and writes to attribute register.

    srdBits = srdBits >> 8;                                 // Banish the bits we are done with!

    NVIC_MPU_NUMBER_R &= ~(0b111);                          // Setting SRD bits for region 5 (third SRAM region)
    NVIC_MPU_NUMBER_R |= 0b101;                              // Let's look at region 5
    NVIC_MPU_ATTR_R &= ~(0x000000FF << 8);                        //Disable unneeded subregions
    NVIC_MPU_ATTR_R |= ((0xFF & srdBits) << 8);             // Grabs the SRD bits for region 5, shifts them into position, and writes to attribute register.

    srdBits = srdBits >> 8;                                 // Banish the bits we are done with!

    NVIC_MPU_NUMBER_R &= ~(0b111);                          // Setting SRD bits for region 6 (fourth SRAM region)
    NVIC_MPU_NUMBER_R |= 0b110;                              // Let's look at region 6
    NVIC_MPU_ATTR_R &= ~(0x000000FF << 8);                        //Disable unneeded subregions
    NVIC_MPU_ATTR_R |= ((0xFF & srdBits) << 8);             // Grabs the SRD bits for region 6, shifts them into position, and writes to attribute register.

    return (void *)ptr;
}

// REQUIRED: add your MPU functions here
// allowBackgroundAccess() added JL 10/29
// allowFlashAcces() added JL 10/29
// allowPeripheralAccess() added JL 10/29
// setupSramAccess() added JL 10/29

void allowBackgroundAccess(void)
{
    NVIC_MPU_BASE_R &= ~(NVIC_MPU_BASE_ADDR_M);
    NVIC_MPU_BASE_R |= NVIC_MPU_BASE_VALID;
    NVIC_MPU_ATTR_R |=  (0b0 << 28) | (0b011 << 24) | (0x1F << 1) | NVIC_MPU_ATTR_ENABLE;
}

void allowFlashAccess(void)
{
    NVIC_MPU_NUMBER_R &= ~(0b111);
    NVIC_MPU_NUMBER_R |= 0b1;                           // Let's look at region 1
    NVIC_MPU_BASE_R &= ~(NVIC_MPU_BASE_ADDR_M);         // Sets Address field to zeros first.
    //NVIC_MPU_BASE_R &= ~(0b00000000000000 << 18);        // Our address field for 256 KiB is 31-18, and our address is starting at 0x0000.0000. Previous line does this for me.
    NVIC_MPU_ATTR_R |= (0b10001 << 1);                  // Set the size to 256 KiB (Full flash)
    NVIC_MPU_ATTR_R |= (0b011 << 24);                   // Sets full permissions for all users
    NVIC_MPU_ATTR_R |= (0b0 << 28);                     // Set executable access.
    NVIC_MPU_ATTR_R |= NVIC_MPU_ATTR_ENABLE;
}

void allowPeripheralAccess(void)            // Not technically needed, but since we have it... why not.
{
    NVIC_MPU_NUMBER_R &= ~(0b111);
    NVIC_MPU_NUMBER_R |= 0b10;                          // Let's look at region 2
    NVIC_MPU_BASE_R &= ~(NVIC_MPU_BASE_ADDR_M);         // Sets Address field to zeros first.
    NVIC_MPU_BASE_R |= (0x40000000);                 // Our address field for 64 MiB is bits 31-26, and our address is starting at 0x4000.0000
    NVIC_MPU_ATTR_R |= (0b11001 << 1);                  // Set the size to 64 MiB (Full peripheral 0x4400.0000 - 0x4000.0000)
    NVIC_MPU_ATTR_R |= (0b011 << 24);                   // Sets full permissions for all users
    NVIC_MPU_ATTR_R |= (0b1 << 28);                     // Remove executable access.
    NVIC_MPU_ATTR_R |= NVIC_MPU_ATTR_ENABLE;
}

void setupSramAccess(void)
{
    NVIC_MPU_NUMBER_R &= ~(0b111);
    NVIC_MPU_NUMBER_R |= 0b11;                            // Let's look at region 3
    NVIC_MPU_BASE_R &= ~(NVIC_MPU_BASE_ADDR_M);         // Sets Address field to zeros first.
    NVIC_MPU_BASE_R |= (0x20000000);                   // Our address field for 8 KiB is bits 31-13, and our address is starting at 0x2000.0000
    NVIC_MPU_ATTR_R |= (0b01100 << 1);                    // Set the size to 8 KiB (First SRAM region, 0-7 KiB)
    NVIC_MPU_ATTR_R |= (0b001 << 24);                     // Sets full permissions for privileged users, none for other
    NVIC_MPU_ATTR_R |= (0b1 << 28);                       // Remove executable access.
    NVIC_MPU_ATTR_R &= ~(NVIC_MPU_ATTR_SRD_M);          // Enable them tasty subregions
    NVIC_MPU_ATTR_R |= NVIC_MPU_ATTR_ENABLE;

    NVIC_MPU_NUMBER_R &= ~(0b111);
    NVIC_MPU_NUMBER_R |= 0b100;                          // Let's look at region 4
    NVIC_MPU_BASE_R &= ~(NVIC_MPU_BASE_ADDR_M);         // Sets Address field to zeros first.
    NVIC_MPU_BASE_R |= (0x20002000);                 // Our address field for 8 KiB is bits 31-13, and our address is starting at 0x2000.2000
    NVIC_MPU_ATTR_R |= (0b01100 << 1);                  // Set the size to 8 KiB (Second SRAM region, 8-15 KiB)
    NVIC_MPU_ATTR_R |= (0b001 << 24);                     // Sets full permissions for privileged users, none for other
    NVIC_MPU_ATTR_R |= (0b1 << 28);                   // Remove executable access.
    NVIC_MPU_ATTR_R &= ~(NVIC_MPU_ATTR_SRD_M);          // Enable them tasty subregions
    NVIC_MPU_ATTR_R |= NVIC_MPU_ATTR_ENABLE;

    NVIC_MPU_NUMBER_R &= ~(0b111);
    NVIC_MPU_NUMBER_R |= 0b101;                          // Let's look at region 5
    NVIC_MPU_BASE_R &= ~(NVIC_MPU_BASE_ADDR_M);         // Sets Address field to zeros first.
    NVIC_MPU_BASE_R |= (0x20004000);                  // Our address field for 8 KiB is bits 31-13, and our address is starting at 0x2000.4000
    NVIC_MPU_ATTR_R |= (0b01100 << 1);                   // Set the size to 8 KiB (Third SRAM region, 16-23 KiB)
    NVIC_MPU_ATTR_R |= (0b001 << 24);                     // Sets full permissions for privileged users, none for other
    NVIC_MPU_ATTR_R |= (0b1 << 28);                    // Remove executable access.
    NVIC_MPU_ATTR_R &= ~(NVIC_MPU_ATTR_SRD_M);          // Enable them tasty subregions
    NVIC_MPU_ATTR_R |= NVIC_MPU_ATTR_ENABLE;

    NVIC_MPU_NUMBER_R &= ~(0b111);
    NVIC_MPU_NUMBER_R |= 0b110;                          // Let's look at region 6
    NVIC_MPU_BASE_R &= ~(NVIC_MPU_BASE_ADDR_M);         // Sets Address field to zeros first.
    NVIC_MPU_BASE_R |= (0x20006000);                  // Our address field for 8 kiB is bits 31-13, and our address is starting at 0x2000.8000
    NVIC_MPU_ATTR_R |= (0b01100 << 1);                   // Set the size to 8 kiB (Fourth SRAM region, 24-31KiB)
    NVIC_MPU_ATTR_R |= (0b001 << 24);                     // Sets full permissions for privileged users, none for other
    NVIC_MPU_ATTR_R |= (0b1 << 28);                    // Remove executable access.
    NVIC_MPU_ATTR_R &= ~(NVIC_MPU_ATTR_SRD_M);          // Enable them tasty subregions
    NVIC_MPU_ATTR_R |= NVIC_MPU_ATTR_ENABLE;
}


// REQUIRED: initialize MPU here
void initMpu(void)
{
    // REQUIRED: call your MPU functions here
    allowBackgroundAccess();                        // Region 0 will be the background region
    setupSramAccess();                              // Setup SRAM memory regions 3-6    
    setPSP((uint32_t *)TOP_OF_SRAM);                // Set temporary PSP at the top of SRAM
    NVIC_MPU_NUMBER_R &= ~(0b111);                  // Clearing MPU targeting register
    NVIC_MPU_NUMBER_R |= 0b11;                      // Targetting MPU Region 6
    NVIC_MPU_ATTR_R &= ~(0x11 << 8);                // Disable unneeded subregions
    NVIC_MPU_ATTR_R |= ((0xFF & 0x80) << 8);        // Enabling top kB of memory for access.
    NVIC_MPU_CTRL_R |= NVIC_MPU_CTRL_ENABLE;        // Enable the MPU
}

//-----------------------------------------------------------------------------
// RTOS Kernel Functions
//-----------------------------------------------------------------------------

// REQUIRED: initialize systick for 1ms system timer - DONE JM, 11/15
void initRtos()
{
    uint8_t i;
    // no tasks running
    taskCount = 0;
    // clear out tcb records
    for (i = 0; i < MAX_TASKS; i++)
    {
        tcb[i].state = STATE_INVALID;
        tcb[i].pid = 0;
    }

    // initialize systick for 1ms system timer
    NVIC_ST_CTRL_R |= NVIC_ST_CTRL_CLK_SRC | NVIC_ST_CTRL_INTEN | NVIC_ST_CTRL_ENABLE;
    NVIC_ST_RELOAD_R = 39999; // 1ms reload value, ***look at this again***
                              // probably because we're using the 40MHz clock, so since we want 1ms, 
                              // we need 40,000 ticks because 40,000,000/1000 = 40,000
                              // we use 40000-1 = 39999 because the reload value needs to be
                              // 1 less than the number of ticks (datasheet pg 140)
}

// REQUIRED: Implement prioritization to 8 levels
int rtosScheduler()
{
    uint8_t highestPriority = 8, currentPriority = 8, i = 0, task = 0;
    for(i = 0; i < MAX_TASKS; i++)
    {
        if(tcb[i].state == STATE_READY || tcb[i].state == STATE_UNRUN)
            currentPriority = tcb[i].priority;
        
        if(currentPriority < highestPriority)
        {
            highestPriority = currentPriority;
            task = i;
        }
    }
    return task;
}

bool createThread(_fn fn, const char name[], uint8_t priority, uint32_t stackBytes)
{
    bool ok = false;
    uint8_t i = 0;
    bool found = false;
    // REQUIRED:
    // store the thread name
    // allocate stack space and store top of stack in sp and spInit
    void * ptr = mallocFromHeap(stackBytes) - 1;
    
    // add task if room in task list
    if (taskCount < MAX_TASKS)
    {
        // make sure fn not already in list (prevent reentrancy)
        while (!found && (i < MAX_TASKS))
        {
            found = (tcb[i++].pid == fn);
        }
        if (!found)
        {
            // find first available tcb record
            i = 0;
            while (tcb[i].state != STATE_INVALID) {i++;}
            tcb[i].state = STATE_UNRUN;
            tcb[i].pid = fn;
            tcb[i].sp = ptr;                        // set tcb[i].sp = ptr 11/13 JL
            tcb[i].spInit = ptr;                    // set tcb[i].spInit = ptr 11/13 JL
            tcb[i].priority = priority;
            // tcb[i].srd = 0;
            strCopy(tcb[i].name, name);
            // increment task count
            taskCount++;
            ok = true;
        }
    }
    // REQUIRED: allow tasks switches again
    return ok;
}

// REQUIRED: modify this function to restart a thread
void restartThread(_fn fn)
{
}

// REQUIRED: modify this function to stop a thread
// REQUIRED: remove any pending semaphore waiting
// NOTE: see notes in class for strategies on whether stack is freed or not
void stopThread(_fn fn)
{
}

// REQUIRED: modify this function to set a thread priority
void setThreadPriority(_fn fn, uint8_t priority)
{
}

bool createSemaphore(uint8_t semaphore, uint8_t count)
{
    bool ok = (semaphore < MAX_SEMAPHORES);
    {
        semaphores[semaphore].count = count;
    }
    return ok;
}

void makeRtosGreatAgain()
{
    // This is where we enable the memory windows and shit. Step 10 or w/e, later.
    _fn funk = (_fn)tcb[taskCurrent].pid;
    setTMPLbit();
    (*funk)();
}

// REQUIRED: modify this function to start the operating system
// by calling scheduler, setting PSP, ASP bit, TMPL bit, and PC
void startRtos()
{
    taskCurrent = rtosScheduler();
    tcb[taskCurrent].state = STATE_READY;
    setPSP(tcb[taskCurrent].sp);
    setASPbit();
    makeRtosGreatAgain();
}

// REQUIRED: modify this function to yield execution back to scheduler using pendsv - JM, 11/14
void yield()
{
    __asm("  SVC #0");
}

// REQUIRED: modify this function to support 1ms system timer
// execution yielded back to scheduler until time elapses using pendsv - JM, 11/14
void sleep(uint32_t tick)
{
    __asm("  SVC #1");
}

// REQUIRED: modify this function to wait a semaphore using pendsv - JM, 11/14
void wait(int8_t semaphore)
{
    __asm("  SVC #2");
}

// REQUIRED: modify this function to signal a semaphore is available using pendsv - JM, 11/14
void post(int8_t semaphore)
{
    __asm("  SVC #3");
}

// REQUIRED: modify this function to add support for the system timer
// REQUIRED: in preemptive code, add code to request task switch
void systickIsr()
{
    //cycle through tasks to find tasks that have been delayed
    //sysTickIsr() needs to decrement tcb[taskCurrent].ticks for delayed tasks every time it triggers, set to trigger every 1ms
    //if tcb[taskCurrent].ticks == 0, set state to ready

    uint8_t i = 0;
    for(i = 0; i < MAX_TASKS; i++)
    {
        if(tcb[i].state == STATE_DELAYED)
        {
            tcb[i].ticks--;
            if(tcb[i].ticks == 0)
            {
                tcb[i].state = STATE_READY;
            }
        }
    }
}

// REQUIRED: in coop and preemptive, modify this function to add support for task switching - JM, 11/14
// REQUIRED: process UNRUN and READY tasks differently
void pendSvIsr()
{
    // printf("PendSV called in process %d \r\n", taskCurrent); //just taskCurrent instead of tcb[taskCurrent].pid?? //((uint32_t)tcb[taskCurrent].pid)
    if(NVIC_FAULT_STAT_R & NVIC_FAULT_STAT_DERR)
    {
        NVIC_FAULT_STAT_R &= ~NVIC_FAULT_STAT_DERR;
        putsUart0("Called from MPU \r\n");
    }

    if(NVIC_FAULT_STAT_R & NVIC_FAULT_STAT_IERR)
    {
        NVIC_FAULT_STAT_R &= ~NVIC_FAULT_STAT_IERR;
        putsUart0("Called from MPU \r\n");
    }

    //get PSP (in C) (send to pushContext)
    //store R4 in PSP, decrement PSP by 4 bytes (-4) (in asm, part of pushContext)
    //repeat for R5-R11 (in asm, part of pushContext)
    //save PSP in tcb[taskCurrent].sp (in C)
    //call rtosScheduler to get next task (idle) (in C)
    //load tcb[taskCurrent].sp into PSP (where taskCurrent has already been updated to be the next task by rtosScheduler) (in C)
    //check if task is UNRUN (in C) or READY (in C)
        //if UNRUN, fabricate context (in asm)
    //load R4 from PSP, increment PSP by 4 bytes (+4) (in asm, part of popContext)
    //repeat for R5-R11 (in asm, part of popContext)
    //set PSP to return value of popContext (in C)

    tcb[taskCurrent].sp = pushContext(getPSP());
    taskCurrent = rtosScheduler();
    uint32_t srdBits = tcb[taskCurrent].srd;

    if(tcb[taskCurrent].state == STATE_READY)       
    {
        setPSP(tcb[taskCurrent].sp);
        setPSP(popContext(getPSP()));
    }
    else if(tcb[taskCurrent].state == STATE_UNRUN)  // Doesn't need R4-R11 because unrun task has no context for those registers yet.
    {
        setPSP(tcb[taskCurrent].spInit);
        fabricateContext(tcb[taskCurrent].pid);
        tcb[taskCurrent].state = STATE_READY;
    }
}

// REQUIRED: modify this function to add support for the service call - JM, 11/14
// REQUIRED: in preemptive code, add code to handle synchronization primitives
void svCallIsr()
{
    // get stack dump and get PC value, get address of PC, dec by 2 bytes
    // get value at that address, and that is the SVC number
    // if SVC number is 0, yield
    // if SVC number is 1, sleep
    // if SVC number is 2, wait
    // if SVC number is 3, post

    uint32_t svcNum = extractSVC(getPSP());

    switch (svcNum)
    {
        case YIELD:
            // set pendsv bit
            NVIC_INT_CTRL_R |= NVIC_INT_CTRL_PEND_SV;
            break;
        case SLEEP:
            //set state of task to delayed
            //set tcb[taskCurrent].ticks to value passed in
            //set pendsv bit
            tcb[taskCurrent].ticks = *(getPSP());
            tcb[taskCurrent].state = STATE_DELAYED;
            NVIC_INT_CTRL_R |= NVIC_INT_CTRL_PEND_SV;
            break;
        case WAIT:
            if(semaphores[*(getPSP())].count > 0)
            {
                semaphores[*(getPSP())].count--;
            }
            else
            {
                semaphores[*(getPSP())].processQueue[semaphores[*(getPSP())].queueSize] = taskCurrent;
                semaphores[*(getPSP())].queueSize++;
                //tcb[taskCurrent].semaphore = getPSP();
                tcb[taskCurrent].state = STATE_BLOCKED;
                NVIC_INT_CTRL_R |= NVIC_INT_CTRL_PEND_SV;
            }
            break;
        case POST:
            semaphores[*(getPSP())].count++;
            if(semaphores[*(getPSP())].queueSize > 0)
            {
                tcb[semaphores[*(getPSP())].processQueue[0]].state = STATE_READY;
                //tcb[semaphores[*(getPSP())].processQueue[0]].semaphore = 0; //?
                semaphores[*(getPSP())].queueSize--;
                uint8_t i = 0;
                for(i = 0; i < semaphores[*(getPSP())].queueSize; i++)
                {
                    semaphores[*(getPSP())].processQueue[i] = semaphores[*(getPSP())].processQueue[i+1];
                }
                semaphores[*(getPSP())].count--;
            }
            NVIC_INT_CTRL_R |= NVIC_INT_CTRL_PEND_SV;
            break;
        default:
            putsUart0("Invalid SVC number \r\n");
            break;
    }
}

// REQUIRED: code this function - JL & JM, 11/14
void mpuFaultIsr()
{
    printf("MPU Fault in process %d \r\n", taskCurrent); //*((uint32_t*)tcb[taskCurrent].pid)
    printf("Value of PSP: %h \r\n", (uint32_t)getPSP);
    printf("Value of MSP: %h \r\n", (uint32_t)getMSP);
    printf("Value of NVIC_FAULT_STAT_R: %h \r\n", NVIC_FAULT_STAT_R);
    printf("Address of offending instruction: %h \r\n", *(getPSP()+5));

    if(NVIC_FAULT_STAT_R & NVIC_FAULT_STAT_MMARV)
        printf("Address of offending data: %h \r\n", NVIC_MM_ADDR_R);

    putsUart0("Process Stack Dump: \r\n");
    printf("R0: %h \r\n", *(getPSP()));
    printf("R1: %h \r\n", *(getPSP()+1));
    printf("R2: %h \r\n", *(getPSP()+2));
    printf("R3: %h \r\n", *(getPSP()+3));
    printf("R12: %h \r\n", *(getPSP()+4));
    printf("LR: %h \r\n", *(getPSP()+5));
    printf("PC: %h \r\n", *(getPSP()+6));
    printf("xPSR: %h \r\n", *(getPSP()+7));

    NVIC_SYS_HND_CTRL_R &= ~NVIC_SYS_HND_CTRL_MEMP;
    NVIC_INT_CTRL_R |= NVIC_INT_CTRL_PEND_SV;
}

// REQUIRED: code this function - JL & JM, 11/14
void hardFaultIsr()
{
    printf("Hard Fault in process %d \r\n", taskCurrent);
    printf("Value of PSP: %h \r\n", (uint32_t)getPSP);
    printf("Value of MSP: %h \r\n", (uint32_t)getMSP);
    printf("Value of NVIC_HFAULT_STAT_R: %h \r\n", NVIC_HFAULT_STAT_R);

    if(NVIC_HFAULT_STAT_R & NVIC_HFAULT_STAT_DBG)
        putsUart0("Hard Fault triggered by Debug Event \n");
    if(NVIC_HFAULT_STAT_R & NVIC_HFAULT_STAT_FORCED)
        putsUart0("Hard Fault triggered by Forced Hard Fault \n");
    if(NVIC_HFAULT_STAT_R & NVIC_HFAULT_STAT_VECT)
        putsUart0("Hard Fault triggered by Vector Table Read Fault \n");
}

// REQUIRED: code this function - JL & JM, 11/14
void busFaultIsr()
{
    printf("Bus Fault in process %d \r\n", taskCurrent);
}

// REQUIRED: code this function - JL & JM, 11/14
void usageFaultIsr()
{
    printf("Usage Fault in process %d \r\n", taskCurrent);
}

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

// Initialize Hardware
// REQUIRED: Add initialization for blue, orange, red, green, and yellow LEDs
//           6 pushbuttons - DONE JL 10/29
void initHw()
{
    // Initialize system clock to 40 MHz
    initSystemClockTo40Mhz();

    // Enable GPIO Ports A,C,D,E,F
    enablePort(PORTF);
    enablePort(PORTE);
    enablePort(PORTA);
    enablePort(PORTC);
    enablePort(PORTD);

    // Configure LED Pins as Push-Pull Outputs
    selectPinPushPullOutput(BLUE_LED);
    selectPinPushPullOutput(RED_LED);
    selectPinPushPullOutput(ORANGE_LED);
    selectPinPushPullOutput(YELLOW_LED);
    selectPinPushPullOutput(GREEN_LED);

    // Initiate blink test: Dance Battle!
    setPinValue(ORANGE_LED,1);
    waitMicrosecond(100000);
    setPinValue(YELLOW_LED,1);
    waitMicrosecond(100000);
    setPinValue(GREEN_LED,1);
    waitMicrosecond(100000);
    setPinValue(RED_LED,1);
    waitMicrosecond(100000);
    setPinValue(BLUE_LED,1);
    waitMicrosecond(100000);
    setPinValue(ORANGE_LED,0);
    waitMicrosecond(100000);
    setPinValue(YELLOW_LED,0);
    waitMicrosecond(100000);
    setPinValue(GREEN_LED,0);
    waitMicrosecond(100000);
    setPinValue(RED_LED,0);
    waitMicrosecond(100000);
    setPinValue(BLUE_LED,0);
    waitMicrosecond(100000);

    // Configure Pushbutton pins as digital inputs
    selectPinDigitalInput(PB0);
    setPinCommitControl(PB1);
    selectPinDigitalInput(PB1);
    selectPinDigitalInput(PB2);
    selectPinDigitalInput(PB3);
    selectPinDigitalInput(PB4);
    selectPinDigitalInput(PB5);
    enablePinPullup(PB0);
    enablePinPullup(PB1);
    enablePinPullup(PB2);
    enablePinPullup(PB3);
    enablePinPullup(PB4);
    enablePinPullup(PB5);

}

// REQUIRED: add code to return a value from 0-63 indicating which of 6 PBs are pressed - DONE JL 10/29
uint8_t readPbs()
{
    uint8_t pbStatus = 0;
    if(!getPinValue(PB0))
        pbStatus += 1;
    if(!getPinValue(PB1))
        pbStatus += 2;
    if(!getPinValue(PB2))
        pbStatus += 4;
    if(!getPinValue(PB3))
        pbStatus += 8;
    if(!getPinValue(PB4))
        pbStatus += 16;
    if(!getPinValue(PB5))
        pbStatus += 32;
    return pbStatus;
}

//-----------------------------------------------------------------------------
// YOUR UNIQUE CODE
// REQUIRED: add any custom code in this space
//-----------------------------------------------------------------------------

// ------------------------------------------------------------------------------
//  Task functions
// ------------------------------------------------------------------------------

// one task must be ready at all times or the scheduler will fail
// the idle task is implemented for this purpose
void idle()
{
    while(true)
    {
        setPinValue(ORANGE_LED, 1);
        waitMicrosecond(1000);
        setPinValue(ORANGE_LED, 0);
        yield();
    }
}

void idle2()
{
    while(true)
    {
        setPinValue(RED_LED, 1);
        waitMicrosecond(1000);
        setPinValue(RED_LED, 0);
        yield();
    }
}

void flash4Hz()
{
    while(true)
    {
        setPinValue(GREEN_LED, !getPinValue(GREEN_LED));
        sleep(125);
    }
}

void oneshot()
{
    while(true)
    {
        wait(flashReq);
        setPinValue(YELLOW_LED, 1);
        sleep(1000);
        setPinValue(YELLOW_LED, 0);
    }
}

void partOfLengthyFn()
{
    // represent some lengthy operation
    waitMicrosecond(990);
    // give another process a chance to run
    yield();
}

void lengthyFn()
{
    uint16_t i;
    uint8_t *p;

    // Example of allocating memory from stack
    // This will show up in the pmap command for this thread
    p = mallocFromHeap(1024);
    *p = 0;

    while(true)
    {
        wait(resource);
        for (i = 0; i < 5000; i++)
        {
            partOfLengthyFn();
        }
        setPinValue(RED_LED, !getPinValue(RED_LED));
        post(resource);
    }
}

void readKeys()
{
    uint8_t buttons;
    while(true)
    {
        wait(keyReleased);
        buttons = 0;
        while (buttons == 0)
        {
            buttons = readPbs();
            yield();
        }
        post(keyPressed);
        if ((buttons & 1) != 0)
        {
            setPinValue(YELLOW_LED, !getPinValue(YELLOW_LED));
            setPinValue(RED_LED, 1);
        }
        if ((buttons & 2) != 0)
        {
            post(flashReq);
            setPinValue(RED_LED, 0);
        }
        if ((buttons & 4) != 0)
        {
            restartThread(flash4Hz);
        }
        if ((buttons & 8) != 0)
        {
            stopThread(flash4Hz);
        }
        if ((buttons & 16) != 0)
        {
            setThreadPriority(lengthyFn, 4);
        }
        yield();
    }
}

void debounce()
{
    uint8_t count;
    while(true)
    {
        wait(keyPressed);
        count = 10;
        while (count != 0)
        {
            sleep(10);
            if (readPbs() == 0)
                count--;
            else
                count = 10;
        }
        post(keyReleased);
    }
}

void uncooperative()
{
    while(true)
    {
        while (readPbs() == 8)
        {
        }
        yield();
    }
}

void errant()
{
    uint32_t* p = (uint32_t*)0x20000000;
    while(true)
    {
        while (readPbs() == 32)
        {
            *p = 0;
        }
        yield();
    }
}

void important()
{
    while(true)
    {
        wait(resource);
        setPinValue(BLUE_LED, 1);
        sleep(1000);
        setPinValue(BLUE_LED, 0);
        post(resource);
    }
}

// REQUIRED: add processing for the shell commands through the UART here
void shell()
{
    while (true)
    {
    }
}

//-----------------------------------------------------------------------------
// Main
//-----------------------------------------------------------------------------

int main(void)
{
    bool ok;

    // Initialize hardware
    initHw();
    initUart0();
    initMpu();
    initRtos();
    enableFaultExceptions();

    // Setup UART0 baud rate 
    setUart0BaudRate(115200, 40e6);

    // Power-up flash
    setPinValue(GREEN_LED, 1);
    waitMicrosecond(250000);
    setPinValue(GREEN_LED, 0);
    waitMicrosecond(250000);

    // Initialize semaphores
    createSemaphore(keyPressed, 1);
    createSemaphore(keyReleased, 0);
    createSemaphore(flashReq, 5);
    createSemaphore(resource, 1);

    // Add required idle process at lowest priority
    ok =  createThread(idle, "Idle", 7, 1024);
//    ok &=  createThread(idle2, "Idle2", 7, 1024);

    // Add other processes
//    ok &= createThread(lengthyFn, "LengthyFn", 6, 1024);
    ok &= createThread(flash4Hz, "Flash4Hz", 4, 1024);
    ok &= createThread(oneshot, "OneShot", 2, 1024);
//    ok &= createThread(readKeys, "ReadKeys", 6, 1024);
//    ok &= createThread(debounce, "Debounce", 6, 1024);
    ok &= createThread(important, "Important", 0, 1024);
//    ok &= createThread(uncooperative, "Uncoop", 6, 1024);
//    ok &= createThread(errant, "Errant", 6, 1024);
//    ok &= createThread(shell, "Shell", 6, 2048);

    // Start up RTOS
    if (ok)
        startRtos(); // never returns
    else
        setPinValue(RED_LED, 1);

    return 0;
}
