# EDF-Port-FreeRTOS

Hello! You have arrived at the "clean" repository of the EDF-FreeRTOS porting. I have implemented this as a personal project and hope you find it useful. The goal is to enable the EDF scheduling policy for FreeRTOS. I used PlatformIO and the ESP32 NodeMCU board. However, the port should work for other boards as the changes are mainly introduced in the `task.c` file.

Below, you will find a detailed step-by-step implementation. 



# 🚀 Bringing EDF Scheduling to FreeRTOS 🚀


*FreeRTOS ships with a rock-solid fixed-priority scheduler;  you need a dynamic, optimal policy like Earliest Deadline First (EDF)! In this guide, we'll walk through how to retrofit EDF into FreeRTOS on an ESP32 (NodeMCU) using the ESP-IDF framework in PlatformIO. Sit back, grab a ☕, and let’s dive in!*

*📑 Note: This implementation should also work for other boards, as we keep it abstract from the port. Thus, go ahead and give it a try if you need it.*

---
---



## 🔍 A quick glimpse into FreeRTOS

🚦 If you already deeply understand FreeRTOS, you can directly jump to the next section!🚦

FreeRTOS is designed to work in embedded environments, thus aiming to minimize memory usage while being suitable for low-clock-frequency microcontrollers. The FreeRTOS minimum kernel consists of only three source files, for less than 9,000 lines of code.

At its heart, the kernel splits into:
- A **hardware-dependent layer** (per architecture)
- A **hardware-independent layer** (common core)

The **three** key files we need to understand are:

* **`task.c`** Here, the task function is defined, and its life cycle is managed. Scheduling functions are also defined here. 

* **`queue.c`** In this file, the structures used for task communication and synchronisation are described. In FreeRTOS, tasks and interrupts communicate with each other using queues to exchange messages; semaphores and mutexes are used to synchronize the sharing of critical resources. 

* **`list.c`** The list data structure and its maintaining functions are defined. Lists are used both by task functions and queues.
---

### Tasks in a Nutshell 🎯

Tasks are implemented as C functions. Each task created is essentially a standalone program with an assigned priority. Tasks run within their context, independently of other tasks’ contexts. At any given moment, the OS chooses the task to execute based on its priority. 

In summary:

A FreeRTOS **Task** is just a C function with:

- An assigned **priority** (0 = lowest)
- A **name** for debugging
- Its own **stack** & **Task Control Block (TCB)**


For every task, FreeRTOS links a dedicated data structure known as the Task Control Block (TCB), which includes the following parameters:

```c
typedef struct tskTaskControlBlock       /* The old naming convention is used to prevent breaking kernel aware debuggers. */
{
    volatile StackType_t * pxTopOfStack; /*< Points to the location of the last item placed on the tasks stack.>*/
    ListItem_t xStateListItem;                  /*< The list that the state list item of a task is reference from denotes the state of that task (Ready, Blocked, Suspended ).>*/
    ListItem_t xEventListItem;                  /*< Used to reference a task from an event list.>*/
    UBaseType_t uxPriority;                     /*< The priority of the task.  0 is the lowest priority. >*/
    StackType_t * pxStack;                      /*< Points to the start of the stack. >*/
    char pcTaskName[ configMAX_TASK_NAME_LEN ]; /*< Descriptive name given to the task when created.  Facilitates debugging only. */ 
    BaseType_t xCoreID;        
} tskTCB; 
```
---
#### Task States ♻️

A task can be in one of the following states:


- **Running**: The task referenced by the `*pcCurrentTCB` system variable is in the Running state, meaning it is currently using the processor. Only one task can run at any given time.

- **Ready**: These are tasks prepared for execution but are not currently running because another task with equal or higher priority occupies the processor.

- **Blocked**: A task enters the Blocked state when it is waiting for an external or time-based event. For instance, a task calling `vTaskDelay()` will block itself for a set duration, or a task may block while waiting on queue or semaphore events.

- **Suspended**: A task can be moved to or from the Suspended state only through explicit calls to `vTaskSuspend()` and `vTaskResume()` respectively. Tasks in this state are excluded from the scheduling process.


The TCB in FreeRTOS does not include a dedicated variable to represent a task's state. Instead, FreeRTOS implicitly tracks task states by maintaining separate lists for each state (Ready, Blocked, and Suspended). A task's current state is determined by which list it resides in.


The following lists are important:

**Ready Tasks:**  

`PRIVILEGED_DATA static List_t pxReadyTasksLists[ configMAX_PRIORITIES ];` 
This is an array of lists containing as many lists as the maximum number of priority (e.g., if the maximum number possible to be assigned to the task priority is 24, then FreeRTOS will create 24 lists). Therefore, The i-th position of the array contains the list of the tasks having the i-th priority.


**Blocked Tasks:**  

`PRIVILEGED_DATA static List_t xDelayedTaskList1;` and `PRIVILEGED_DATA static List_t xDelayedTaskList2`. For delayed tasks (two lists are used - one for delays that have overflowed the current tick count.)

`PRIVILEGED_DATA static List_t * volatile pxDelayedTaskList;` Points to the delayed task list currently being used.

`PRIVILEGED_DATA static List_t * volatile pxOverflowDelayedTaskList;` Points to the delayed task list currently being used to hold tasks that have overflowed the current tick count.


**Suspended Tasks:**  

`PRIVILEGED_DATA static List_t xSuspendedTaskList;` /*< Tasks that are currently suspended. */
This is a simple list used to allocate suspended tasks

---

#### Task Creation & Delays ⏳

A task is created invoking the `task.c` method `xTaskCreatePinnedToCore()`:

```c
BaseType_t xTaskCreatePinnedToCore( TaskFunction_t pxTaskCode,
                                        const char * const pcName,
                                        const configSTACK_DEPTH_TYPE usStackDepth,
                                        void * const pvParameters,
                                        UBaseType_t uxPriority,
                                        TaskHandle_t * const pxCreatedTask,
                                        const BaseType_t xCoreID )
```

A task is created following these major steps:

1. Allocate TCB & stack
2. Init TCB fields
3. Init stack frame
4. Add to Ready list

The `vTaskDelayUntil()` function defines a frequency at which the task is periodically executed, so it can be used to implement periodic tasks. FreeRTOS measures time by periodically increasing the tick count variable, which is only available for core 0. `vTaskDelayUntil()` moves the invoking task to the Blocking list, where it waits for a given time interval before being moved to the Ready list again.

---
### Context Switch 🔄

In FreeRTOS, a task does not know when it is going to get suspended or resumed by the system. It only continues executing as long as there is no context switching. When the running task is switched out, the execution context is saved in its stack, ready to be restored when the task is executed again. We will not go into detail on how this works, but it uses registers to point to the running task and the next instruction in the task's code, so the task can be restored afterwards.

The context switch is defined in `vTaskSwitchContext()`.

---


### The Tick System ⏱️

A hardware timer fires at `CONFIG_FREERTOS_HZ` (100 Hz by default → every 10 ms), increments the system tick, and wakes any tasks whose delay has expired. If a higher-priority task unblocks, you’ll get a preemptive switch right in the ISR. In case you want to increase the frequency of the interrupt, you can find the `CONFIG_FREERTOS_HZ` constant in `sdkconfig.h`.



---

## 📂 Setting up the project

If you wish to follow the exact implementation as I did, feel free to thoroughly complete each of the following steps. In case you have your project already, you can simply use this as a guide on how to adapt it.

1. **Create the project:** In PlatformIO go ahead and create an empty project that uses the Espidf framework:

* Name: EDF-Port-FreeRTOS
* Board: NodeMCU-32S
* Framework: Espidf

*📑 Note: We use espidf and not Arduino framework as Arduino comes with a pre-compiled version of FreeRTOS.*

2. **Change project options:**

In `platformio.ini`:
```c
[env:nodemcu-32s]  
platform = espressif32  
board = nodemcu-32s  
framework = espidf  
monitor_speed = 115200  
monitor_filters = esp32_exception_decoder  
```

2. **Get your own customizable FreeRTOS:** ESP-IDF’s CMake build will always look in your project’s components/ folder first, then fall back to the IDF installation in `C:\Users\<user_name>\.platformio\packages\framework-espidf\components\freertos`. You can exploit that to completely replace the FreeRTOS component for this project only.

To do so, let's create the components folder in the platformio project:

```
EDF-PORT-FREERTOS/
├── .pio/
├── .vscode/
├── components/
│   └── freertos/
├── include/
├── lib/
├── src/
├── test/
├── .gitignore
├── CMakeLists.txt
├── platformio.ini
└── sdkconfig.nodemcu-32s
```

Copy the entire contents of the IDF FreeRTOS component into it.

To check you are compiling the custom FreeRTOS, let's add a dummy error flag in the `task.c` file. 

```c
//In task.c
...
/* Add dummy overwrite */
#error OVERRIDE
/* Standard includes. */
#include <stdlib.h>
#include <string.h>

```
---

Clean and compile the project again. You should see the override error popping up in the console window.

```
pio run -t clean
pio run
```


```c
// In the console you will see
components\freertos\FreeRTOS-Kernel\tasks.c:34:2: error: #error OVERRIDE
   34 | #error OVERRIDE
      |  ^~~~~
```

If you see the error, that means you are running the customized FreeRTOS version. Therefore, remove the `#error OVERRIDE` from the `task.c` file to continue.


```
pio run -t clean
pio run
```



## 📅 Earliest Deadline First

Earliest Deadline First (EDF) employs a dynamic priority-based preemptive scheduling policy. This means a task’s priority can change during its execution, and the execution of a task may be interrupted whenever a task with a higher (i.e., earlier deadline) priority becomes ready to run. Hence, the task with the highest priority is the one with the earliest deadline. In case of two or more tasks with the same absolute deadline, the highest priority task among them is chosen at random.


The algorithm is designed for environments where the following assumptions hold:

- **(A1)**: All tasks with hard deadlines are periodic, with a constant interval between successive requests.
- **(A2)**: Deadlines are strictly run-ability constraints. Each task must be completed before the next instance of the same task begins.
- **(A3)**: Tasks are independent; the initiation or completion of one task does not depend on any other task.
- **(A4)**: The run-time for each task is constant and does not change over time. Run-time is defined as the uninterrupted time required by the processor to execute the task.
- **(A5)**: Any non-periodic tasks are exceptional cases, such as initialization or failure-recovery routines. These tasks may preempt periodic tasks during execution, but do not have hard or critical deadlines themselves.

Given these assumptions, it is possible to characterize a task in EDF by two parameters: its period and its runtime.

---

## 🛠️ Implementing EDF on FreeRTOS

The idea is simple: Create a new ready list where tasks are put in priority order. The list should contain tasks ordered by increasing deadline time, where positions in the list represent the tasks' priorities. The task priority (and thus the list) is updated at each system tick. Therefore, the task with the earliest deadline will be the first in the list, so the scheduler gets it and gives it processing time. In case a new task has a closer deadline, the list will be updated, and a context switch should occur.

We’ll keep the vanilla kernel intact and guard EDF changes with:

```c
#if ( configUSE_EDF_SCHEDULER == 1 )
    /*code implementation for EDF*/
#else
    /*optional original code implementation for fixed-priority*/
#endif

```
*⚠️ Limitations: Current implementation has been considered for periodic tasks only and assumes tasks with implicit deadlines (deadline equals task period).*

These are the steps we will follow:



* ➕ Add a new Ready List

* 🔧 Initialize the EDF List

* 🔀 Override the Ready-list Macro

* ⏱️ Add xTaskPeriod to the TCB

* 🚀 New Helper to create EDF tasks

* 💤 Tweak the IDLE Task

* 🔄 Switch Context by Deadline

* ✅ Enable EDF



💥💣 **------------ Let's dive into it! ------------** 💣💥

1. **Add a new Ready List**

Declare `xReadyTasksListEDF` as a simple list structure.

```c
#if ( configUSE_EDF_SCHEDULER == 1 )
/*< Ready tasks ordered by absolute deadline >*/
    PRIVILEGED_DATA static List_t xReadyTasksListEDF;  /*< Ready tasks ordered by deadline. >*/
#endif
```

*📑 Note: You can add this code right before the declaration of the delayed task lists `PRIVILEGED_DATA static List_t xDelayedTaskList1;`*

2. **Initialize the new Ready List**
Then, modify the `prvInitialiseTaskLists()` method, which initializes all the task lists at the creation of the first task. Here we must add the initialization of `xReadyTasksListEDF`


```c
static void prvInitialiseTaskLists( void )
{
    ...

    #if ( configUSE_EDF_SCHEDULER == 1 )
        vListInitialise( &xReadyTasksListEDF );
    #endif
    ...
}
```

*📑 Note: You can add this code right after the declaration of the waiting lists `vListInitialise( &xDelayedTaskList2 );`*

3. **Modify the prvAddTaskToReadyList macro**

We need to modify the macro that adds a task to the Ready List

Find this piece of code on the `task.c`:


```c
/*-----------------------------------------------------------*/

/*
 * Place the task represented by pxTCB into the appropriate ready list for
 * the task.  It is inserted at the end of the list.
 */
#define prvAddTaskToReadyList( pxTCB )                                                                 \
    traceMOVED_TASK_TO_READY_STATE( pxTCB );                                                           \
    taskRECORD_READY_PRIORITY( ( pxTCB )->uxPriority );                                                \
    vListInsertEnd( &( pxReadyTasksLists[ ( pxTCB )->uxPriority ] ), &( ( pxTCB )->xStateListItem ) ); \
    tracePOST_MOVED_TASK_TO_READY_STATE( pxTCB )
/*-----------------------------------------------------------*/
```

Replace it with:


```c
#if ( configUSE_EDF_SCHEDULER == 1 )
    #undef prvAddTaskToReadyList
    #define prvAddTaskToReadyList( pxTCB )                                        \
    do {                                                                          \
        BaseType_t xThisCore   = xPortGetCoreID();                                \
        BaseType_t xTargetCore = ( pxTCB )->xCoreID;                              \
                                                                                  \
        traceMOVED_TASK_TO_READY_STATE( pxTCB );                                  \
                                                                                  \
        /* 1) Tag the absolute deadline */                                        \
        listSET_LIST_ITEM_VALUE(                                                  \
            &( ( pxTCB )->xStateListItem ),                                       \
            xTaskGetTickCount() + ( pxTCB )->xTaskPeriod                          \
        );                                                                        \
                                                                                  \
        /* 2) Must lock around a shared list on SMP */                            \
        taskENTER_CRITICAL( &xKernelLock );                                       \
        vListInsert( &xReadyTasksListEDF, &( ( pxTCB )->xStateListItem ) );       \
        taskEXIT_CRITICAL( &xKernelLock );                                        \
                                                                                  \
        /* 3) Wake the right core: */                                             \
        if( xTargetCore == tskNO_AFFINITY ||                                      \
            xTargetCore == xThisCore )                                            \
        {                                                                         \
            /* Local-core: preempt now */                                         \
            portYIELD_WITHIN_API();                                               \
        }                                                                         \
        else                                                                      \
        {                                                                         \
            /* Other-core: send an IPI */                                         \
            vPortYieldOtherCore( xTargetCore );                                   \
        }                                                                         \
                                                                                  \
        tracePOST_MOVED_TASK_TO_READY_STATE( pxTCB );                             \
    } while( 0 )

#else

    /* Original priority‐based insertion */
    #undef prvAddTaskToReadyList
    #define prvAddTaskToReadyList( pxTCB )                                         \
        traceMOVED_TASK_TO_READY_STATE( pxTCB );                                   \
        taskRECORD_READY_PRIORITY( ( pxTCB )->uxPriority );                        \
        vListInsertEnd(                                                            \
            &( pxReadyTasksLists[ ( pxTCB )->uxPriority ] ),                       \
            &( ( pxTCB )->xStateListItem )                                         \
        );                                                                         \
        tracePOST_MOVED_TASK_TO_READY_STATE( pxTCB )

#endif
```

This is what we are doing here:
* Undefine the macro in case it was defined somewhere else
* Get the core from which the macro is being called (which in ESP32 is always the core 0) and the target core of the task. We read the current core (`xPortGetCoreID()`) and the task’s pinned-to core (`pxTCB->xCoreID`). If they differ, we’ll send an IPI later so the other core reschedules immediately.
* Change the value of the deadline for the task (current tick count + period). By using `listSET_LIST_ITEM_VALUE` the list stays order based on the item value, which in our case is the deadlien. Therefore the list will be in deadline order because we always use the sorted insert API when we put a task into it. 
* Insert the new list element in the `xReadyTasksListEDF` List. `vListInsert()` walks the list from the tail backwards until it finds the first item whose xItemValue (the deadline) is ≤ the new item’s value, and links it immediately after it. That keeps the list always sorted by `xItemValue` (i.e., by deadline) with no extra “rearrange” pass needed.
* Case the task has no affinity, namely can run in any core, or the target core is core 0, then pin the task to core 0. Otherwise, force the task to run on the other core (core 1). Simply adding to the list isn’t enough under EDF. When we have just made a sooner-deadline task ready, the kernel must re-evaluate right now. On the same core we call `portYIELD_WITHIN_API()`, which causes `vTaskSwitchContext()` to run as soon as we exit the critical section. If the task is pinned elsewhere, we send an inter-processor interrupt with `vPortYieldOtherCore()`

*📑 Note: `portYIELD_WITHIN_API();` and `vPortYieldOtherCore( xTargetCore );` functions are the unique port-specific functions we use here. Therefore, its implementation may vary based on which board you are using. The equivalent methods should be found in `portmacro.h` if you are not using the ESP32 but following this tutorial for some other board.*


4. **Add the period variable in the TCB**

As EDF requires the task to have a period (deadline), we need to add that variable in the TCB structure. When a task enters the Ready List, its upcoming deadline must be known to correctly position it in the list. The deadline is computed using `TASKdeadline = tickcur + TASKperiod`. Therefore, each task must store its period value to enable this calculation.


```c
typedef struct tskTaskControlBlock
{
    ...

    #if ( configUSE_EDF_SCHEDULER == 1 )
        TickType_t xTaskPeriod;   /* Period (in ticks) for EDF deadline */
    #endif

} tskTCB;
```

5. **Define a new Task creation**

A new initialization task method for the EDF periodic tasks is created, `xTaskCreateEDF()`


```c
#if ( configUSE_EDF_SCHEDULER == 1 )

    BaseType_t xTaskCreateEDF ( 
        TaskFunction_t    pxTaskCode,
        const char * const pcName,
        const configSTACK_DEPTH_TYPE usStackDepth,
        void * const      pvParameters,
        UBaseType_t       uxPriority,
        TaskHandle_t *    pxCreatedTask,
        TickType_t        xPeriod,
        BaseType_t        xCoreID )
    {
        BaseType_t xReturn;

        /* 1) Create the task pinned to core: */
        xReturn = xTaskCreatePinnedToCore(
            pxTaskCode,
            pcName,
            usStackDepth,
            pvParameters,
            uxPriority,
            pxCreatedTask,
            xCoreID
        );                           
        /* 2) If creation succeeded, tweak for EDF: */
        if( xReturn == pdPASS )
        {
            /* 2.1) Grab its TCB to set up EDF fields */
            TCB_t *pxTCB = ( TCB_t * ) *pxCreatedTask;

            /* 2.2) Remove from whatever list prvAddNewTaskToReadyList put it in */
            uxListRemove( &( pxTCB->xStateListItem ) );

            pxTCB->xTaskPeriod = xPeriod;

            /* 2.3) Set the absolute-deadline = now + period */
            listSET_LIST_ITEM_VALUE( &( pxTCB->xStateListItem ),
                                    xTaskGetTickCount() + xPeriod );

            /* 2.4) Insert it exactly once into the EDF list, in deadline order */
            vListInsert( &xReadyTasksListEDF,
                        &( pxTCB->xStateListItem ) );
            pxTCB->xCoreID = xCoreID; 
        }
        return xReturn;
    }
#endif /* configUSE_EDF_SCHEDULER */
```

*📑 Note: You can add the method right after the block:*

```c
#if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
    ...
#endif /* configSUPPORT_DYNAMIC_ALLOCATION */
```


6. **Modify the behavior of the IDLE task**

An IDLE task with minimum priority should be created, so in case the scheduler has no task to run, it executes the IDLE task. This prevents the system to abort execution and reboot given that there is nothing to run.

The IDLE task is initialized within the `vTaskStartScheduler()` method, which begins the real-time kernel tick processing and sets up all necessary scheduler structures. Since FreeRTOS requires a task to be running at all times, proper handling of the IDLE task is essential.

In the standard FreeRTOS scheduler, the IDLE task is a simple task assigned the lowest priority, ensuring it only runs when no other tasks are Ready. Under the EDF scheduler, this lowest priority behavior is emulated by assigning the IDLE task the farthest possible deadline.


```c
void vTaskStartScheduler( void )
{
    ...
    /* Add the idle task at the lowest priority. */
    #if ( configSUPPORT_STATIC_ALLOCATION == 1 )
        {
            StaticTask_t * pxIdleTaskTCBBuffer = NULL;
            StackType_t * pxIdleTaskStackBuffer = NULL;
            uint32_t ulIdleTaskStackSize;
           /* Ensure ulIdleTaskStackSize is set before use: */
           vApplicationGetIdleTaskMemory( &pxIdleTaskTCBBuffer,
                                              &pxIdleTaskStackBuffer,
                                              &ulIdleTaskStackSize );
            /* The Idle task is created using user-provided RAM - obtain the address of the RAM then create the idle task. */

            /*Create Idle via EDF so it lands in xReadyTasksListEDF */
            #if ( configUSE_EDF_SCHEDULER == 1 )
            {
                   
                BaseType_t xIdleStatus;
    
                /* 1) Create the Idle task, storing its handle via the out‐parameter */
                xIdleStatus = xTaskCreateEDF(
                    prvIdleTask,               /* Task function */
                    configIDLE_TASK_NAME,      /* Name */
                    ulIdleTaskStackSize,       /* Stack depth */
                    NULL,                      /* No parameter */
                    portPRIVILEGE_BIT,         /* Priority */
                    &xIdleTaskHandle[ xCoreID ], /* Out: TaskHandle_t */
                    portMAX_DELAY,              /* Infinite deadline */
                    0
                );
    
                /* 2) Use the return code to set xReturn, not the handle */
                if( xIdleStatus == pdPASS )
                {
                    xReturn = pdPASS;
                }
                else
                {
                    xReturn = pdFAIL;
                }
            }
                       
            #else
                /* Fallback to the original static API if EDF is disabled */
                /* The Idle task is created using user provided RAM - obtain the
                * address of the RAM then create the idle task. */
                vApplicationGetIdleTaskMemory( &pxIdleTaskTCBBuffer, &pxIdleTaskStackBuffer, &ulIdleTaskStackSize );
                xIdleTaskHandle[ xCoreID ] = xTaskCreateStaticPinnedToCore( prvIdleTask,
                                                                            configIDLE_TASK_NAME,
                                                                            ulIdleTaskStackSize,
                                                                            ( void * ) NULL,   
                                                                            portPRIVILEGE_BIT, /* In effect ( tskIDLE_PRIORITY | portPRIVILEGE_BIT ), but tskIDLE_PRIORITY is zero. */
                                                                            pxIdleTaskStackBuffer,
                                                                            pxIdleTaskTCBBuffer,
                                                                            xCoreID ); 
            #endif

            if( xIdleTaskHandle[ xCoreID ] != NULL )
            {
                xReturn = pdPASS;
            }
            else
            {
                xReturn = pdFAIL;
            }
        }
    #else /* if ( configSUPPORT_STATIC_ALLOCATION == 0 ) */
        {
            #if ( configUSE_EDF_SCHEDULER == 1 )
            xReturn = xTaskCreateEDF( prvIdleTask,
                                      configIDLE_TASK_NAME,
                                      configMINIMAL_STACK_SIZE,
                                      NULL,
                                      tskIDLE_PRIORITY | portPRIVILEGE_BIT,
                                      &xIdleTaskHandle[ xCoreID ],
                                      portMAX_DELAY );
            #else
                xReturn = xTaskCreatePinnedToCore( prvIdleTask,
                                                configIDLE_TASK_NAME,
                                                configMINIMAL_STACK_SIZE,
                                                ( void * ) NULL,
                                                portPRIVILEGE_BIT, /* In effect ( tskIDLE_PRIORITY | portPRIVILEGE_BIT ), but tskIDLE_PRIORITY is zero. */
                                                &xIdleTaskHandle[ xCoreID ],
                                                xCoreID );     
            #endif
        
            if( xIdleTaskHandle[ xCoreID ] != NULL )
            {
                xReturn = pdPASS;
            }
            else
            {
                xReturn = pdFAIL;
            }
        }
    #endif /* configSUPPORT_STATIC_ALLOCATION */

    ...

}
```

The IDLE task is initialized with a period of `portMAX_DELAY`. This ensures that when the IDLE task is added to the Ready List, it is placed at the end, as its calculated deadline will be later than that of any other task.

7. **Modify the switch context mechanism**

```c
void vTaskSwitchContext( void )
{

    ...
    /* code block */
        #if( configUSE_EDF_SCHEDULER == 1 )
        {
            const UBaseType_t uxCoreId = xPortGetCoreID();
            ListItem_t *pxItem;
            TCB_t *pxNextTCB = NULL;
        
            /* 1) Scan EDF list for a task pinned to this core or no-affinity */
            for( pxItem = xReadyTasksListEDF.xListEnd.pxNext;
                 pxItem != &xReadyTasksListEDF.xListEnd;
                 pxItem = pxItem->pxNext )
            {
                TCB_t *pxTCB = (TCB_t *) listGET_LIST_ITEM_OWNER( pxItem );
                if( pxTCB->xCoreID == tskNO_AFFINITY || pxTCB->xCoreID == uxCoreId )
                {
                    pxNextTCB = pxTCB;
                    break;
                }
            }
        
            /* 2) If we found one, pick it. Otherwise, fall back to the Idle task. */
            if( pxNextTCB != NULL )
            {
                pxCurrentTCB[ uxCoreId ] = pxNextTCB;
            }
            else
            {
                /* No real tasks ready for this core—run the Idle task. */
                pxCurrentTCB[ uxCoreId ] = xIdleTaskHandle[ uxCoreId ];
            }
        }
        #else
            taskSELECT_HIGHEST_PRIORITY_TASK();
        #endif  
    /* code block */
    ...
}
```

*📑 Note: Replace the following line with the code above:*

```c
taskSELECT_HIGHEST_PRIORITY_TASK(); 
```

8. **Define the task creation function**

In order to be able to use our new `xTaskCreateEDF()` function, we must add it into the `task.h` file.

```c
#if ( configUSE_EDF_SCHEDULER == 1 )

/**
 * Create a new periodic (EDF‐scheduled) task.
 *
 * @param pxTaskCode      The function to execute as the task.
 * @param pcName          A descriptive name for the task.
 * @param usStackDepth    Stack size (in words) to allocate.
 * @param pvParameters    Pointer passed into the task on start.
 * @param uxPriority      Priority (ignored by EDF; you can pass tskIDLE_PRIORITY).
 * @param pxCreatedTask   Optional return handle.
 * @param xPeriod         The task’s period, in RTOS ticks.
 * @return pdPASS if created successfully, otherwise an error.
 */
BaseType_t xTaskCreateEDF( TaskFunction_t    pxTaskCode,
                           const char * const pcName,
                           const configSTACK_DEPTH_TYPE usStackDepth,
                           void * const      pvParameters,
                           UBaseType_t       uxPriority,
                           TaskHandle_t *    pxCreatedTask,
                           TickType_t        xPeriod,
                           BaseType_t        xCoreID );

#endif /* configUSE_EDF_SCHEDULER */
```

*📑 Note: You can add the method rigth after the block:*

```c
#if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
    BaseType_t xTaskCreatePinnedToCore( TaskFunction_t pxTaskCode,
                                        const char * const pcName,
                                        const configSTACK_DEPTH_TYPE usStackDepth,
                                        void * const pvParameters,
                                        UBaseType_t uxPriority,
                                        TaskHandle_t * const pvCreatedTask,
                                        const BaseType_t xCoreID );
#endif
```



9. **Enabling EDF**

To enable the EDF scheduler, it is required to define the macro in `FreeRTOSConfig.h`

```c
#define configUSE_EDF_SCHEDULER 1
```
---
---

✨ That’s it! You’ve now got a fully functional EDF scheduler on top of FreeRTOS, with minimal intrusion into the existing codebase. Time to fire up PlatformIO and watch those deadlines dance! 🎉

Let's create a simple example:

```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
 
/* Task periods, in ticks (e.g. 500ms and 1000ms at a 1 ms tick) */
#define TASK1_PERIOD pdMS_TO_TICKS(500)
#define TASK2_PERIOD pdMS_TO_TICKS(1000)


/*-----------------------------------------------------------*/
static void vTask1( void *pvParameters )
{
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for( ;; )
    {
        printf( "Hello from Task 1 (period %u ticks)\n", (unsigned)TASK1_PERIOD );
        vTaskDelayUntil( &xLastWakeTime, TASK1_PERIOD );
    }
}
/*-----------------------------------------------------------*/
static void vTask2( void *pvParameters )
{
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for( ;; )
    {
        printf( "Hello from Task 2 (period %u ticks)\n", (unsigned)TASK2_PERIOD );
        vTaskDelayUntil( &xLastWakeTime, TASK2_PERIOD );
    }
}
/*-----------------------------------------------------------*/

void app_main( void )
{
    TaskHandle_t xHandle1 = NULL, xHandle2 = NULL;
    BaseType_t   xReturned1, xReturned2;

    xReturned1 = xTaskCreateEDF(
        vTask1,
        "Task1",
        2048,
        NULL,
        1,
        &xHandle1,      
        TASK1_PERIOD,
        1
    );

    xReturned2 = xTaskCreateEDF(
        vTask2,
        "Task2",
        2048,
        NULL,
        1,
        &xHandle2,      
        TASK2_PERIOD,
        1
    );

    if( xReturned1 != pdPASS || xReturned2 != pdPASS )
    {
        printf( "Failed to create EDF tasks\n" );
        for( ;; );
    }

}

```
