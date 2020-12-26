#include <string.h>

#include "modbus.h"
#include "main.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp8266/uart_struct.h"
#include "esp8266/uart_register.h"

#include "driver/uart.h"
#include "driver/gpio.h"

#define MODBUS_GPIO_DE_ID 15
#define MODBUS_GPIO_DE_BIT (1<<MODBUS_GPIO_DE_ID)

#define UART_EMPTY_THRESH_DEFAULT  (10)
#define UART_FULL_THRESH_DEFAULT  (120)
#define UART_TOUT_THRESH_DEFAULT   (22)

#define MODBUS_BUF_SIZE (512)

#define RX_OVFL_NONE 0
#define RX_OVFL_BUF  1
#define RX_OVFL_FIFO 2

typedef struct uart_modbus_obj {
    uart_port_t uart_num;               /*!< UART port number*/
    uart_dev_t* uart_dev;               /*!< UART peripheral (Address)*/
    uint32_t char_duration_us;

    SemaphoreHandle_t tx_done_sem;
    uint8_t* tx_buffer;    // The tx buffer, allocated at UART init
    uint32_t tx_len;       // The size of data in bytes in the buffer to be sent
    // The pointer to the first data to be sent
    // Non-null indicates a Tx is going on
    uint8_t* tx_ptr;

    SemaphoreHandle_t rx_done_sem;
    uint8_t* rx_buffer;
    uint32_t rx_len;
    uint8_t rx_data_ready;
    uint8_t rx_overflow;
} uart_modbus_obj_t;

static uart_modbus_obj_t* p_uart_obj = {0};

void modbus_gpio_rxen_set(uint32_t val) {
    gpio_set_level(MODBUS_GPIO_DE_ID, val);
}

static void uart_modbus_intr_handler(void *param) {
    uart_modbus_obj_t* p_uart = (uart_modbus_obj_t*)param;
    BaseType_t task_woken = 0;

    uint32_t uart_intr_status = p_uart->uart_dev->int_st.val;
    while (uart_intr_status != 0x0) {
        if (uart_intr_status & UART_TXFIFO_EMPTY_INT_ST_M) {
            uart_clear_intr_status(p_uart->uart_num, UART_TXFIFO_EMPTY_INT_CLR_M);
            uart_disable_intr_mask(p_uart->uart_num, UART_TXFIFO_EMPTY_INT_ENA_M);

            int tx_fifo_rem = UART_FIFO_LEN - p_uart->uart_dev->status.txfifo_cnt;

            if (p_uart->tx_ptr == NULL) {
                if (p_uart->tx_len == 0) {
                    // No data to send, abort
                    break;
                } else {
                    // The first interrupt, set DE high to enable tx
                    p_uart->tx_ptr = p_uart->tx_buffer;
                }
            } else {    // p_uart->tx_ptr != NULL
                if (p_uart->tx_len == 0) {
                    // We have sent all data and the buffer is now completely empty
                    p_uart->tx_ptr = NULL;

                    xSemaphoreGiveFromISR(p_uart->tx_done_sem, &task_woken);
                    if (task_woken == pdTRUE)
                        portYIELD_FROM_ISR();
                    break;
                }
            }

            int send_len = p_uart->tx_len > tx_fifo_rem ? tx_fifo_rem : p_uart->tx_len;
            for (int buf_idx = 0; buf_idx < send_len; buf_idx++) {
                p_uart->uart_dev->fifo.rw_byte = *(p_uart->tx_ptr++) & 0xff;
            }
            p_uart->tx_len -= send_len;
            tx_fifo_rem -= send_len;

            // tx_fifo_rem  p_uart->tx_len
            // == 0         > 0                 UART FIFO is full, more data to be pushed into the FIFO
            // == 0         == 0                UART FIFO is full, no more data to be pushed, UART is still sending
            // > 0          == 0                UART FIFO is not full, no more data to be pushed, UART is still sending
            if (p_uart->tx_len == 0) {
                // No more data need to be pushed into the UART FIFO
                // But the UART may still need sometime to send all data in its FIFO
                // We want to have another interrupt once the Tx FIFO becomes empty again
                p_uart->uart_dev->conf1.txfifo_empty_thrhd = 0;
            }

            // Enable the interrupt again
            uart_clear_intr_status(p_uart->uart_num, UART_TXFIFO_EMPTY_INT_CLR_M);
            uart_enable_intr_mask(p_uart->uart_num, UART_TXFIFO_EMPTY_INT_ENA_M);

        } else if ((uart_intr_status & UART_RXFIFO_TOUT_INT_ST_M)
                || (uart_intr_status & UART_RXFIFO_FULL_INT_ST_M)
               ) {
            int rx_fifo_len = p_uart->uart_dev->status.rxfifo_cnt;
            int rx_buf_vacant = MODBUS_BUF_SIZE - p_uart->rx_len;
            if (rx_fifo_len > rx_buf_vacant) {
                // Too much data in the Rx FIFO, rx_buffer overflow
                // We will not copy the remaining data since the request must be malformed.
                p_uart->rx_overflow |= RX_OVFL_BUF;

                // Discard whatever is in the Rx FIFO
                p_uart->uart_dev->conf0.rxfifo_rst = 0x1;
                p_uart->uart_dev->conf0.rxfifo_rst = 0x0;
            } else {
                // All data in the Rx FIFO can be pushed into rx_buffer
                uint8_t* rx_data_buf = p_uart->rx_buffer + p_uart->rx_len;
                for (int buf_idx = 0; buf_idx < rx_fifo_len; buf_idx++) {
                    rx_data_buf[buf_idx] = p_uart->uart_dev->fifo.rw_byte;
                }
                p_uart->rx_len += rx_fifo_len;
            }

            // After Copying the Data From FIFO ,Clear intr_status
            uart_clear_intr_status(p_uart->uart_num, UART_RXFIFO_TOUT_INT_CLR_M | UART_RXFIFO_FULL_INT_CLR_M);

            if (uart_intr_status & UART_RXFIFO_TOUT_INT_ST_M) {
                // Silent interval detected!
                uart_disable_intr_mask(p_uart->uart_num, UART_RXFIFO_TOUT_INT_CLR_M | UART_RXFIFO_FULL_INT_CLR_M);
                p_uart->rx_data_ready = 1;

                xSemaphoreGiveFromISR(p_uart->rx_done_sem, &task_woken);
                if (task_woken == pdTRUE)
                    portYIELD_FROM_ISR();
            }

        } else if (uart_intr_status & UART_RXFIFO_OVF_INT_ST_M) {
            // When fifo overflows, we reset the fifo.
            p_uart->uart_dev->conf0.rxfifo_rst = 0x1;
            p_uart->uart_dev->conf0.rxfifo_rst = 0x0;
            p_uart->uart_dev->int_clr.rxfifo_ovf = 1;
            p_uart->rx_overflow |= RX_OVFL_FIFO;
        } else if (uart_intr_status & UART_FRM_ERR_INT_ST_M) {
            p_uart->uart_dev->int_clr.frm_err = 1;
        } else if (uart_intr_status & UART_PARITY_ERR_INT_ST_M) {
            p_uart->uart_dev->int_clr.parity_err = 1;
        } else {
            p_uart->uart_dev->int_clr.val = uart_intr_status; // simply clear all other intr status
        }

        uart_intr_status = p_uart->uart_dev->int_st.val;
    } // while (uart_intr_status != 0x0);
}

void uart_modbus_tx_all() {
    modbus_gpio_rxen_set(1);
    ets_delay_us(p_uart_obj->char_duration_us);
    p_uart_obj->tx_ptr = NULL;
    p_uart_obj->tx_len = p_uart_obj->rx_len;
    // Enter UART_TXFIFO_EMPTY_INT immediately
    // Tx FIFO is populated by the ISR
    uart_enable_tx_intr(p_uart_obj->uart_num, 1, UART_EMPTY_THRESH_DEFAULT);

    xSemaphoreTake(p_uart_obj->tx_done_sem, portMAX_DELAY );
    // Although we have pushed the last byte into the buffer, but the UART will take sometime to send it
    ets_delay_us(p_uart_obj->char_duration_us << 1);
    modbus_gpio_rxen_set(0);
}

static void echo_task() {
    while (1) {
        // Read data from the UART
        //while (!p_uart_obj->rx_data_ready) vTaskDelay(10);
        if (xSemaphoreTake(p_uart_obj->rx_done_sem, 100) == pdTRUE) {
            ESP_LOGI("MODBUS", "Received %d bytes, overflow state %d", p_uart_obj->rx_len, p_uart_obj->rx_overflow);

            memcpy(p_uart_obj->tx_buffer, p_uart_obj->rx_buffer, p_uart_obj->rx_len);

            uart_modbus_tx_all();

            p_uart_obj->rx_len = 0;
            p_uart_obj->rx_data_ready = 0;
            p_uart_obj->rx_overflow = RX_OVFL_NONE;
            uart_enable_intr_mask(p_uart_obj->uart_num, UART_RXFIFO_TOUT_INT_CLR_M | UART_RXFIFO_FULL_INT_CLR_M);
        }
    }
}

void modbus_uart_init() {
    p_uart_obj = malloc(sizeof(uart_modbus_obj_t));
    p_uart_obj->uart_num = UART_NUM_0;
    p_uart_obj->uart_dev = &uart0;
    p_uart_obj->char_duration_us = 10*1000000/115200;

    p_uart_obj->tx_done_sem = xSemaphoreCreateBinary();
    p_uart_obj->tx_buffer = malloc(MODBUS_BUF_SIZE);
    p_uart_obj->tx_len = 0;
    p_uart_obj->tx_ptr = NULL;

    p_uart_obj->rx_done_sem = xSemaphoreCreateBinary();
    p_uart_obj->rx_buffer = malloc(MODBUS_BUF_SIZE);
    p_uart_obj->rx_len = 0;
    p_uart_obj->rx_data_ready = 0;
    p_uart_obj->rx_overflow = RX_OVFL_NONE;


    // Configure parameters of an UART driver,
    // communication pins and install the driver
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    ESP_ERROR_CHECK(uart_param_config(p_uart_obj->uart_num, &uart_config));

    ESP_ERROR_CHECK(uart_isr_register(p_uart_obj->uart_num, uart_modbus_intr_handler, p_uart_obj));
    // Set the thresholds and enable interrupts
    uart_intr_config_t uart_intr = {
        .intr_enable_mask = UART_RXFIFO_FULL_INT_ENA_M
        | UART_RXFIFO_TOUT_INT_ENA_M
        | UART_FRM_ERR_INT_ENA_M
        | UART_RXFIFO_OVF_INT_ENA_M,
        .rxfifo_full_thresh = UART_FULL_THRESH_DEFAULT,
        .rx_timeout_thresh = UART_TOUT_THRESH_DEFAULT,
        .txfifo_empty_intr_thresh = UART_EMPTY_THRESH_DEFAULT
    };
    ESP_ERROR_CHECK(uart_intr_config(p_uart_obj->uart_num, &uart_intr));
    ESP_LOGI("MODBUS", "UART INIT");

    gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO15/16
    io_conf.pin_bit_mask = MODBUS_GPIO_DE_BIT;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);
    gpio_set_level(MODBUS_GPIO_DE_ID, 0);

    xTaskCreate(echo_task, "uart_echo_task", 1024, NULL, 10, NULL);
}

void modbus_uart_deinit() {
    if (p_uart_obj != NULL) {
        vSemaphoreDelete(p_uart_obj->tx_done_sem);
        free(p_uart_obj->tx_buffer);
        vSemaphoreDelete(p_uart_obj->rx_done_sem);
        free(p_uart_obj->rx_buffer);
    }
}