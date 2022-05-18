/* UART Events Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "at_cmd_processing.h"

static const char *TAG = "uart_events";

/**
 * This example shows how to use the UART driver to handle special UART events.
 *
 * It also reads data from UART0 directly, and echoes it to console.
 *
 * - Port: UART0
 * - Receive (Rx) buffer: on
 * - Transmit (Tx) buffer: off
 * - Flow control: off
 * - Event queue: on
 * - Pin assignment: TxD (default), RxD (default)
 */


#define EX_UART_NUM UART_NUM_0
#define PATTERN_CHR_NUM         (3)         /*!< Set the number of consecutive and identical characters received by receiver which defines a UART pattern*/
#define BUF_SIZE                (1024)
#define RD_BUF_SIZE             (BUF_SIZE)

static QueueHandle_t uart0_queue;
QueueHandle_t messageQueue; 
char dtcpy[RD_BUF_SIZE];
volatile char buffer[RD_BUF_SIZE];
volatile unsigned int DATA_IN              = 0;
volatile unsigned int RECEIVING_PACKAGE    = 0;
volatile unsigned int CURRENT_PACKAGE_SIZE = 0;
volatile unsigned int LAST_PACKAGE_SIZE    = 0;
TaskHandle_t process_buffer_Handle         = NULL;
int baud_rate = 115200;

static void process_buffer_task(void *pvParameters)
{
    char message_received[RD_BUF_SIZE]; 
    for(;;)
    {
        /* Wait to receive a notification sent directly to this task from the
        interrupt service routine. */
        if(xQueueReceive(messageQueue, &message_received, (portTickType)portMAX_DELAY)) 
        {
            char *msg = message_received;
            int msg_len = strlen(message_received);
            //printf("message = %s\n", msg);
            int return_index = 0;
            int error = 0;
            enum input_type num_code = checkInput(msg, msg_len, &return_index);
            //printf("enum = %d\n", num_code);
            switch (num_code)
            {
            case Reverse_Cmd:
                printf("AT+REVERSE=\n");
                char* output = calloc(msg_len-11, sizeof(char));
                reverse_str(message_received, return_index, msg_len, output, &error);
                if(error)
                {
                    printf("Erro no input.\n");
                }
                else
                {
                    printf("Entrada invertida = %s.\n", output);
                }
                free(output);
                break;
            case Size_Cmd:
                printf("AT+SIZE=\n");
                size_t input_size = size(message_received, return_index, msg_len, &error);
                if(error)
                {
                    printf("Erro no input.\n");
                }
                else
                {
                    printf("String de tamanho = %zu.\n", input_size);
                }
                break;
            case Mult_Cmd:
                printf("AT+MULT=\n");
                int result = mult(message_received, return_index, msg_len, &error);
                if(error)
                {
                    printf("Erro no input.\n");
                }
                else
                {
                    printf("Resultado da multiplicacao = %d.\n", result);
                }
                break;    
            default:
                printf("Erro no comando.\n");
                break;
            }
            memset(message_received, 0, sizeof(message_received));
        }
    }    
    vTaskDelete(NULL);
}

void vTimerCallback()
{
    BaseType_t xStatus;
    xStatus = xQueueSendToBack(messageQueue, &buffer, 0);
    if(xStatus != pdPASS)
    {
        printf("Nao foi possivel acrescentar item a fila.\n");
    } 
    for(int i = 0; i < LAST_PACKAGE_SIZE; i++)
    {
        buffer[i] = 0;
    }
    RECEIVING_PACKAGE = 0;
}

static void uart_event_task(void *pvParameters)
{
    esp_timer_handle_t oneshot_timer = (esp_timer_handle_t) pvParameters;
    uart_event_t event;
    size_t buffered_size;
    uint8_t* dtmp = (uint8_t*) malloc(RD_BUF_SIZE);
    for(;;) {
        //Waiting for UART event.
        if(xQueueReceive(uart0_queue, (void * )&event, (portTickType)portMAX_DELAY)) {
            bzero(dtmp, RD_BUF_SIZE);
            //ESP_LOGI(TAG, "uart[%d] event:", EX_UART_NUM);
            switch(event.type) {
                //Event of UART receving data
                /*We'd better handler data event fast, there would be much more data events than
                other types of events. If we take too much time on data event, the queue might
                be full.*/
                case UART_DATA:
                    DATA_IN = 1;
                    //ESP_LOGI(TAG, "[UART DATA]: %d", event.size);
                    uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);
                    for(int i = 0; i < event.size; i++)
                    {
                        dtcpy[i] = (char*) dtmp[i];
                    }
                    if(strlen(buffer) < RD_BUF_SIZE)
                        strcat(buffer, dtcpy);
                    memset(dtcpy, 0, event.size); 
                    CURRENT_PACKAGE_SIZE += event.size;  
                    
                    //ESP_LOGI(TAG, "[DATA EVT]:");
                    //uart_write_bytes(EX_UART_NUM, (const char*) dtmp, event.size);
                    break;
                //Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    //ESP_LOGI(TAG, "hw fifo overflow");
                    // If fifo overflow happened, you should consider adding flow control for your application.
                    // The ISR has already reset the rx FIFO,
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(EX_UART_NUM);
                    xQueueReset(uart0_queue);
                    break;
                //Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    //ESP_LOGI(TAG, "ring buffer full");
                    // If buffer full happened, you should consider encreasing your buffer size
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(EX_UART_NUM);
                    xQueueReset(uart0_queue);
                    break;
                //Event of UART RX break detected
                case UART_BREAK:
                    break;
                //Event of UART parity check error
                case UART_PARITY_ERR:
                    break;
                //Event of UART frame error
                case UART_FRAME_ERR:
                    //ESP_LOGI(TAG, "uart frame error");
                    break;
                //UART_PATTERN_DET
                case UART_PATTERN_DET:
                    uart_get_buffered_data_len(EX_UART_NUM, &buffered_size);
                    int pos = uart_pattern_pop_pos(EX_UART_NUM);
                    //ESP_LOGI(TAG, "[UART PATTERN DETECTED] pos: %d, buffered size: %d", pos, buffered_size);
                    if (pos == -1) {
                        // There used to be a UART_PATTERN_DET event, but the pattern position queue is full so that it can not
                        // record the position. We should set a larger queue size.
                        // As an example, we directly flush the rx buffer here.
                        uart_flush_input(EX_UART_NUM);
                    } else {
                        uart_read_bytes(EX_UART_NUM, dtmp, pos, 100 / portTICK_PERIOD_MS);
                        uint8_t pat[PATTERN_CHR_NUM + 1];
                        memset(pat, 0, sizeof(pat));
                        uart_read_bytes(EX_UART_NUM, pat, PATTERN_CHR_NUM, 100 / portTICK_PERIOD_MS);
                        //ESP_LOGI(TAG, "read data: %s", dtmp);
                        //ESP_LOGI(TAG, "read pat : %s", pat);
                    }
                    break;
                default:
                    break;    
            }
            if(DATA_IN == 1 && RECEIVING_PACKAGE == 0)
            {
                RECEIVING_PACKAGE = 1;
                LAST_PACKAGE_SIZE = CURRENT_PACKAGE_SIZE;
                CURRENT_PACKAGE_SIZE = 0;
                ESP_ERROR_CHECK(esp_timer_start_once(oneshot_timer, 80/baud_rate));
            }
        }
    }
    free(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);
}

void app_main(void)
{
    messageQueue = xQueueCreate(5, sizeof(buffer));
    if(messageQueue != NULL)
    {
        esp_log_level_set(TAG, ESP_LOG_INFO);
        static const int baud_rate = 115200;

        /* Configure parameters of an UART driver,
        * communication pins and install the driver */
        uart_config_t uart_config = {
            .baud_rate = baud_rate,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_APB,
        };
        //Install UART driver, and get the queue.
        uart_driver_install(EX_UART_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 20, &uart0_queue, 0);
        uart_param_config(EX_UART_NUM, &uart_config);

        //Set UART log level
        esp_log_level_set(TAG, ESP_LOG_INFO);
        //Set UART pins (using UART0 default pins ie no changes.)
        uart_set_pin(EX_UART_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

        //Set uart pattern detect function.
        uart_enable_pattern_det_baud_intr(EX_UART_NUM, '+', PATTERN_CHR_NUM, 9, 0, 0); //PATTERN_CHR_NUM
        //Reset the pattern queue length to record at most 20 pattern positions.
        uart_pattern_queue_reset(EX_UART_NUM, 20);

        //Create esp timer
        const esp_timer_create_args_t oneshot_timer_args = {
                .callback = &vTimerCallback,
                /* argument specified here will be passed to timer callback function */
                //.arg = (void*) periodic_timer,
                .name = "timer"
        };
        esp_timer_handle_t oneshot_timer;
        ESP_ERROR_CHECK(esp_timer_create(&oneshot_timer_args, &oneshot_timer));
        
        xTaskCreate(process_buffer_task, "process_buffer_task", 6000, NULL, 12, &process_buffer_Handle);
        //Create a task to handler UART event from ISR
        xTaskCreate(uart_event_task, "uart_event_task", 6000, (void*)oneshot_timer, 12, NULL);
    }
    else
    {
        //erro. tratar
    }
}
