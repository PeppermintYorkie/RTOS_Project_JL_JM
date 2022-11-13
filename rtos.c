// RTOS Framework - Fall 2022
// J Losh

// Student Name:
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
// TODO: Add header files here for your strings functions, ... - DONE JL JM 10/31

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

#define TOP_OF_SRAM 0x20008000      // A macro for the top of SRAM memory

extern void setTMPLbit(void);
extern void setASPbit(void);
extern void setPSP(uint32_t* stack);
extern uint32_t* getPSP(void);
extern uint32_t* getMSP(void);


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
void * mallocFromHeap(uint32_t size_in_bytes)
{
    static uint32_t * heap = 0x20001800;
        size_in_bytes = (((size_in_bytes-1)/1024)+1)*1024;
        heap += size_in_bytes;
        if(heap >= 0x20008000)
        {
            p = 0;
        }
        else
        {
            p = (uint32_t *)TOP_OF_SRAM - (size_in_bytes/4);
        }
        return p;
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
    setPSP(TOP_OF_SRAM);                            // Set temporary PSP at the top of SRAM
    NVIC_MPU_NUMBER_R &= ~(0b111);                  // Clearing MPU targeting register
    NVIC_MPU_NUMBER_R |= 0b110;                     // Targetting MPU Region 6
    NVIC_MPU_ATTR_R &= ~(0x11 << 8);                // Disable unneeded subregions
    NVIC_MPU_ATTR_R |= ((0xFF & 0x80) << 8);        // Enabling top kB of memory for access.
    NVIC_MPU_CTRL_R |= NVIC_MPU_CTRL_ENABLE;        // Enable the MPU
    setASPbit();
}

//-----------------------------------------------------------------------------
// RTOS Kernel Functions
//-----------------------------------------------------------------------------

// REQUIRED: initialize systick for 1ms system timer
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
}

// REQUIRED: Implement prioritization to 8 levels
int rtosScheduler()
{
    bool ok;
    static uint8_t task = 0xFF;
    ok = false;
    while (!ok)
    {
        task++;
        if (task >= MAX_TASKS)
            task = 0;
        ok = (tcb[task].state == STATE_READY || tcb[task].state == STATE_UNRUN);
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
    strCopy(tcb.name, name);
    // allocate stack space and store top of stack in sp and spInit
    
    // add task if room in task list
    if (taskCount < MAX_TASKS)
    {
        // make sure fn not already in list (prevent reentrancy)
        while (!found && (i < MAX_TASKS))
        {
            found = (tcb[i++].pid ==  fn);
        }
        if (!found)
        {
            // find first available tcb record
            i = 0;
            while (tcb[i].state != STATE_INVALID) {i++;}
            tcb[i].state = STATE_UNRUN;
            tcb[i].pid = fn;
            tcb[i].sp = 0;
            tcb[i].spInit = 0;
            tcb[i].priority = priority;
            tcb[i].srd = 0;
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

// REQUIRED: modify this function to start the operating system
// by calling scheduler, setting PSP, ASP bit, TMPL bit, and PC
void startRtos()
{
    
}

// REQUIRED: modify this function to yield execution back to scheduler using pendsv
void yield()
{
}

// REQUIRED: modify this function to support 1ms system timer
// execution yielded back to scheduler until time elapses using pendsv
void sleep(uint32_t tick)
{
}

// REQUIRED: modify this function to wait a semaphore using pendsv
void wait(int8_t semaphore)
{
}

// REQUIRED: modify this function to signal a semaphore is available using pendsv
void post(int8_t semaphore)
{
}

// REQUIRED: modify this function to add support for the system timer
// REQUIRED: in preemptive code, add code to request task switch
void systickIsr()
{
}

// REQUIRED: in coop and preemptive, modify this function to add support for task switching
// REQUIRED: process UNRUN and READY tasks differently
void pendSvIsr()
{
}

// REQUIRED: modify this function to add support for the service call
// REQUIRED: in preemptive code, add code to handle synchronization primitives
void svCallIsr()
{
}

// REQUIRED: code this function - Needs process name printing JL 10/28
void mpuFaultIsr()
{
    putsUart0("MPU fault in process ");
    // something that converts int to string
    // something that prints that string, and newlines
    putcUart0('\n');
    char str[11];
    putsUart0("Process Stack Pointer: ");
    regToHex((uint32_t)getPSP(),str);
    putsUart0(str);
    putsUart0("\n");
    putsUart0("Main Stack Pointer: ");
    regToHex((uint32_t)getMSP(),str);
    putsUart0(str);
    putsUart0("\n");
    putsUart0("Offending instruction address: ");
    regToHex((uint32_t)(*(getPSP()+5)),str);
    putsUart0(str);
    putsUart0("\n");
    uint32_t dataAddr = NVIC_MM_ADDR_R;
    if(NVIC_FAULT_STAT_R & NVIC_FAULT_STAT_MMARV)      // If we have a valid address for faulting instruction
    {
        putsUart0("Offending data address: ");
        regToHex(dataAddr,str);
        putsUart0(str);
        putsUart0("\n");
    }
    putsUart0("Process Stack Dump: \n");
    putsUart0("R0: ");
    regToHex((uint32_t)(*(getPSP()+0)),str);
    putsUart0(str);
    putsUart0("\n");
    putsUart0("R1: ");
    regToHex((uint32_t)(*(getPSP()+1)),str);
    putsUart0(str);
    putsUart0("\n");
    putsUart0("R2: ");
    regToHex((uint32_t)(*(getPSP()+2)),str);
    putsUart0(str);
    putsUart0("\n");
    putsUart0("R3: ");
    regToHex((uint32_t)(*(getPSP()+3)),str);
    putsUart0(str);
    putsUart0("\n");
    putsUart0("R12: ");
    regToHex((uint32_t)(*(getPSP()+4)),str);
    putsUart0(str);
    putsUart0("\n");
    putsUart0("LR: ");
    regToHex((uint32_t)(*(getPSP()+5)),str);
    putsUart0(str);
    putsUart0("\n");
    putsUart0("PC: ");
    regToHex((uint32_t)(*(getPSP()+6)),str);
    putsUart0(str);
    putsUart0("\n");
    putsUart0("xPSR: ");
    regToHex((uint32_t)(*(getPSP()+7)),str);
    putsUart0(str);
    putsUart0("\n");

    putsUart0("\n");
    NVIC_SYS_HND_CTRL_R &= ~(NVIC_SYS_HND_CTRL_MEMP);
    NVIC_INT_CTRL_R |= NVIC_INT_CTRL_PEND_SV;
}

// REQUIRED: code this function - Needs process name printing JL 10/28
void hardFaultIsr()
{
    putsUart0("Hard fault in process ");
     // something that converts int to string
     // something that prints that string, and newlines
     putcUart0('\n');
     char str[11];
     putsUart0("Process Stack Pointer: ");
     regToHex((uint32_t)getPSP(),str);
     putsUart0(str);
     putsUart0("\n");
     putsUart0("Main Stack Pointer: ");
     regToHex((uint32_t)getMSP(),str);
     putsUart0(str);
     putsUart0("\n");
     putsUart0("Hard Fault Flags: ");
     regToHex(NVIC_HFAULT_STAT_R,str);
     putsUart0(str);
     putsUart0("\n");
     if(NVIC_HFAULT_STAT_R & NVIC_HFAULT_STAT_DBG)
         putsUart0("Hard fault by debug event has occurred.\n");
     if(NVIC_HFAULT_STAT_R & NVIC_HFAULT_STAT_FORCED)
         putsUart0("Forced hard fault has occurred.\n");
     if(NVIC_HFAULT_STAT_R & NVIC_HFAULT_STAT_VECT)
         putsUart0("A bus fault occurred on a vector table read.\n");
}

// REQUIRED: code this function - Needs process name printing JL 10/28
void busFaultIsr()
{
    putsUart0("Bus fault in process ");
    // something that converts int to string
    // something that prints that string, and newlines
    putcUart0('\n');
}

// REQUIRED: code this function - Needs process name printing JL 10/28
void usageFaultIsr()
{
    putsUart0("Usage fault in process ");
    // something that converts int to string
    // something that prints that string, and newlines
    putcUart0('\n');
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

    // Add other processes
//    ok &= createThread(lengthyFn, "LengthyFn", 6, 1024);
//    ok &= createThread(flash4Hz, "Flash4Hz", 4, 1024);
//    ok &= createThread(oneshot, "OneShot", 2, 1024);
//    ok &= createThread(readKeys, "ReadKeys", 6, 1024);
//    ok &= createThread(debounce, "Debounce", 6, 1024);
//    ok &= createThread(important, "Important", 0, 1024);
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
