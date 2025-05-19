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
