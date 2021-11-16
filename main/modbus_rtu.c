#include <string.h>

#include "modbus.h"
#include "main.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"

#include "esp8266/uart_struct.h"
#include "esp8266/uart_register.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp8266/gpio_struct.h"

#define MODBUS_GPIO_DE_BIT (1<<MODBUS_GPIO_DE_ID)
#if MODBUS_GPIO_DE_INV == 1
#define MODBUS_GPIO_DE_SET() (GPIO.out_w1tc |= MODBUS_GPIO_DE_BIT)
#define MODBUS_GPIO_DE_CLR() (GPIO.out_w1ts |= MODBUS_GPIO_DE_BIT)
#else
#define MODBUS_GPIO_DE_SET() (GPIO.out_w1ts |= MODBUS_GPIO_DE_BIT)
#define MODBUS_GPIO_DE_CLR() (GPIO.out_w1tc |= MODBUS_GPIO_DE_BIT)
#endif


#define UART_EMPTY_THRESH_DEFAULT  (10)
#define UART_FULL_THRESH_DEFAULT  (120)
#define UART_TOUT_THRESH_DEFAULT   (22)

#define MODBUS_BUF_SIZE (MODBUS_RTU_FRAME_MAXLEN+MODBUS_TCP_PAYLOAD_OFFSET)

#define RX_OVFL_NONE 0
#define RX_OVFL_BUF  1
#define RX_OVFL_FIFO 2

typedef struct uart_modbus_obj {
    uart_port_t uart_num;               /*!< UART port number*/
    uart_dev_t* uart_dev;               /*!< UART peripheral (Address)*/
    uint32_t char_duration_us;
    uint32_t tx_delay_us;

    SemaphoreHandle_t tx_done_sem;
    uint8_t tx_frame_buffer[sizeof(rtu_session_t) + MODBUS_BUF_SIZE];
    uint8_t* tx_buffer;    // The tx buffer, points to tx_frame_buffer+sizeof(rtu_session_t)
    uint32_t tx_len;       // The size of data in bytes in the buffer to be sent
    // The pointer to the first data to be sent
    // Non-null indicates a Tx is going on
    uint8_t* tx_ptr;

    SemaphoreHandle_t rx_done_sem;
    uint8_t rx_buffer[MODBUS_BUF_SIZE];
    uint32_t rx_len;
    uint8_t rx_overflow;

    RingbufHandle_t tx_fifo;
    SemaphoreHandle_t tx_fifo_mux;

    SemaphoreHandle_t cfg_mux;
} uart_modbus_obj_t;

static uart_modbus_obj_t p_uart_obj = {0};

static void uart_modbus_intr_handler(void *param) {
    BaseType_t task_woken = 0;

    uint32_t uart_intr_status = p_uart_obj.uart_dev->int_st.val;
    while (uart_intr_status != 0x0) {
        if (uart_intr_status & UART_TXFIFO_EMPTY_INT_ST_M) {
            uart_clear_intr_status(p_uart_obj.uart_num, UART_TXFIFO_EMPTY_INT_CLR_M);
            uart_disable_intr_mask(p_uart_obj.uart_num, UART_TXFIFO_EMPTY_INT_ENA_M);

            int tx_fifo_rem = UART_FIFO_LEN - p_uart_obj.uart_dev->status.txfifo_cnt;

            if (p_uart_obj.tx_ptr == NULL) {
                if (p_uart_obj.tx_len == 0) {
                    // No data to send, abort
                    break;
                } else {
                    // The first interrupt, set DE high to enable tx
                    MODBUS_GPIO_DE_SET();
                    ets_delay_us(p_uart_obj.tx_delay_us);
                    p_uart_obj.tx_ptr = p_uart_obj.tx_buffer;
                }
            } else {    // p_uart_obj.tx_ptr != NULL
                if (p_uart_obj.tx_len == 0) {
                    // We have sent all data and the buffer is now completely empty
                    p_uart_obj.tx_ptr = NULL;

                    // Although we have pushed the last byte into the buffer, but the UART will take sometime to send it
                    ets_delay_us(p_uart_obj.char_duration_us);
                    MODBUS_GPIO_DE_CLR();

                    xSemaphoreGiveFromISR(p_uart_obj.tx_done_sem, &task_woken);
                    if (task_woken == pdTRUE)
                        portYIELD_FROM_ISR();
                    break;
                }
            }

            int send_len = p_uart_obj.tx_len > tx_fifo_rem ? tx_fifo_rem : p_uart_obj.tx_len;
            for (int buf_idx = 0; buf_idx < send_len; buf_idx++) {
                p_uart_obj.uart_dev->fifo.rw_byte = *(p_uart_obj.tx_ptr++) & 0xff;
            }
            p_uart_obj.tx_len -= send_len;
            tx_fifo_rem -= send_len;

            // tx_fifo_rem  p_uart_obj.tx_len
            // == 0         > 0                 UART FIFO is full, more data to be pushed into the FIFO
            // == 0         == 0                UART FIFO is full, no more data to be pushed, UART is still sending
            // > 0          == 0                UART FIFO is not full, no more data to be pushed, UART is still sending
            if (p_uart_obj.tx_len == 0) {
                // No more data need to be pushed into the UART FIFO
                // But the UART may still need sometime to send all data in its FIFO
                // We want to have another interrupt once the Tx FIFO becomes empty again
                p_uart_obj.uart_dev->conf1.txfifo_empty_thrhd = 0;
            }

            // Enable the interrupt again
            uart_clear_intr_status(p_uart_obj.uart_num, UART_TXFIFO_EMPTY_INT_CLR_M);
            uart_enable_intr_mask(p_uart_obj.uart_num, UART_TXFIFO_EMPTY_INT_ENA_M);

        } else if ((uart_intr_status & UART_RXFIFO_TOUT_INT_ST_M)
                || (uart_intr_status & UART_RXFIFO_FULL_INT_ST_M)
               ) {
            int rx_fifo_len = p_uart_obj.uart_dev->status.rxfifo_cnt;
            int rx_buf_vacant = MODBUS_BUF_SIZE - p_uart_obj.rx_len;
            if (rx_fifo_len > rx_buf_vacant) {
                // Too much data in the Rx FIFO, rx_buffer overflow
                // We will not copy the remaining data since the request must be malformed.
                p_uart_obj.rx_overflow |= RX_OVFL_BUF;

                // Discard whatever is in the Rx FIFO
                p_uart_obj.uart_dev->conf0.rxfifo_rst = 0x1;
                p_uart_obj.uart_dev->conf0.rxfifo_rst = 0x0;
            } else {
                // All data in the Rx FIFO can be pushed into rx_buffer
                uint8_t* rx_data_buf = p_uart_obj.rx_buffer + p_uart_obj.rx_len;
                for (int buf_idx = 0; buf_idx < rx_fifo_len; buf_idx++) {
                    rx_data_buf[buf_idx] = p_uart_obj.uart_dev->fifo.rw_byte;
                }
                p_uart_obj.rx_len += rx_fifo_len;
            }

            // After Copying the Data From FIFO ,Clear intr_status
            uart_clear_intr_status(p_uart_obj.uart_num, UART_RXFIFO_TOUT_INT_CLR_M | UART_RXFIFO_FULL_INT_CLR_M);

            if (uart_intr_status & UART_RXFIFO_TOUT_INT_ST_M) {
                // Silent interval detected!
                uart_disable_intr_mask(p_uart_obj.uart_num, UART_RXFIFO_TOUT_INT_CLR_M | UART_RXFIFO_FULL_INT_CLR_M);

                xSemaphoreGiveFromISR(p_uart_obj.rx_done_sem, &task_woken);
                if (task_woken == pdTRUE)
                    portYIELD_FROM_ISR();
            }

        } else if (uart_intr_status & UART_RXFIFO_OVF_INT_ST_M) {
            // When fifo overflows, we reset the fifo.
            p_uart_obj.uart_dev->conf0.rxfifo_rst = 0x1;
            p_uart_obj.uart_dev->conf0.rxfifo_rst = 0x0;
            p_uart_obj.uart_dev->int_clr.rxfifo_ovf = 1;
            p_uart_obj.rx_overflow |= RX_OVFL_FIFO;
        } else if (uart_intr_status & UART_FRM_ERR_INT_ST_M) {
            p_uart_obj.uart_dev->int_clr.frm_err = 1;
        } else if (uart_intr_status & UART_PARITY_ERR_INT_ST_M) {
            p_uart_obj.uart_dev->int_clr.parity_err = 1;
        } else {
            p_uart_obj.uart_dev->int_clr.val = uart_intr_status; // simply clear all other intr status
        }

        uart_intr_status = p_uart_obj.uart_dev->int_st.val;
    } // while (uart_intr_status != 0x0);
}

static void modbus_rtu_task(void* param) {
    while (1) {
        rtu_session_t* session_header;
        uint8_t* buf1 = NULL;
        uint8_t* buf2 = NULL;
        size_t buf1_len, buf2_len;
        if (xRingbufferReceiveSplit(p_uart_obj.tx_fifo, (void**)(&buf1), (void**)(&buf2), &buf1_len, &buf2_len, portMAX_DELAY)) {
            memcpy(p_uart_obj.tx_frame_buffer, buf1, buf1_len);
            p_uart_obj.tx_len = buf1_len;
            vRingbufferReturnItem(p_uart_obj.tx_fifo, buf1);

            if (buf2 != NULL) {
                memcpy(p_uart_obj.tx_frame_buffer + buf1_len, buf2, buf2_len);
                p_uart_obj.tx_len += buf2_len;
                vRingbufferReturnItem(p_uart_obj.tx_fifo, buf2);
            }
        }
        session_header = (rtu_session_t*)p_uart_obj.tx_frame_buffer;
        p_uart_obj.tx_len -= sizeof(rtu_session_t);

        xSemaphoreTake(p_uart_obj.cfg_mux, portMAX_DELAY);

        uint16_t crc16 = modbus_rtu_crc16(p_uart_obj.tx_buffer, p_uart_obj.tx_len);
        p_uart_obj.tx_buffer[p_uart_obj.tx_len++] = crc16 & 0xFF; // Lower Byte
        p_uart_obj.tx_buffer[p_uart_obj.tx_len++] = (crc16>>8) & 0xFF; // Higher Byte
        //ets_delay_us(p_uart_obj.char_duration_us);
        p_uart_obj.tx_ptr = NULL;
        // Enter UART_TXFIFO_EMPTY_INT immediately
        // Tx FIFO is populated by the ISR
        // ESP_LOGI("MODBUS", "Tx[%04X@%d]:", session_header->transaction_id, xTaskGetTickCount());
        // hexdump(p_uart_obj.tx_buffer, p_uart_obj.tx_len);
        uart_enable_tx_intr(p_uart_obj.uart_num, 1, UART_EMPTY_THRESH_DEFAULT);

        xSemaphoreTake(p_uart_obj.tx_done_sem, portMAX_DELAY);

        if (xSemaphoreTake(p_uart_obj.rx_done_sem, 300/portTICK_RATE_MS) == pdTRUE) {
            // ESP_LOGI("MODBUS", "Rx[%04X@%d] %d bytes, overflow state %d", session_header->transaction_id, xTaskGetTickCount(), p_uart_obj.rx_len, p_uart_obj.rx_overflow);
            // hexdump(p_uart_obj.rx_buffer, p_uart_obj.rx_len);

            buf1_len = p_uart_obj.rx_len-2;
            buf2_len = modbus_rtu_crc16(p_uart_obj.rx_buffer+MODBUS_TCP_PAYLOAD_OFFSET, buf1_len-MODBUS_TCP_PAYLOAD_OFFSET);
            if (    (p_uart_obj.rx_buffer[buf1_len] == (buf2_len & 0xFF)) &&
                    (p_uart_obj.rx_buffer[buf1_len+1] == ((buf2_len>>8) & 0xFF)) &&
                    p_uart_obj.rx_buffer[MODBUS_TCP_PAYLOAD_OFFSET] == session_header->uid) {
                tcp_server_send_response(session_header, p_uart_obj.rx_buffer, buf1_len);
            } else {
                ESP_LOGW("Modbus_Rx", "Bad CRC");
            }

            p_uart_obj.rx_len = MODBUS_TCP_PAYLOAD_OFFSET;
            p_uart_obj.rx_overflow = RX_OVFL_NONE;
            uart_enable_intr_mask(p_uart_obj.uart_num, UART_RXFIFO_TOUT_INT_CLR_M | UART_RXFIFO_FULL_INT_CLR_M);
        } else {
            ESP_LOGW("Modbus_Rx", "Rx timeout");
        }

        xSemaphoreGive(p_uart_obj.cfg_mux);

        // Inter-frame gap
        vTaskDelay(p_uart_obj.char_duration_us * 4 * configTICK_RATE_HZ / 1000000 );
    }
}

static uart_parity_t parity_from_u8(uint8_t val) {
    uart_parity_t parity = UART_PARITY_DISABLE;
    if (val == 1 || val == 3)
        parity = UART_PARITY_ODD;
    else if (val == 2)
        parity = UART_PARITY_EVEN;
    return parity;
}

static uint32_t calc_char_us(uint32_t baudrate, uint8_t parity) {
    uint8_t bits = parity == 0 ? 10 : 11;
    return bits * 1000000 / baudrate;
}

void modbus_uart_init(uint32_t baudrate, uint8_t parity, uint32_t tx_delay) {
    p_uart_obj.uart_num = UART_NUM_0;
    p_uart_obj.uart_dev = &uart0;
    p_uart_obj.char_duration_us = calc_char_us(baudrate, parity);
    p_uart_obj.tx_delay_us = tx_delay;

    p_uart_obj.tx_done_sem = xSemaphoreCreateBinary();
    p_uart_obj.tx_buffer = p_uart_obj.tx_frame_buffer + sizeof(rtu_session_t);
    p_uart_obj.tx_len = 0;
    p_uart_obj.tx_ptr = NULL;

    p_uart_obj.rx_done_sem = xSemaphoreCreateBinary();
    p_uart_obj.rx_len = MODBUS_TCP_PAYLOAD_OFFSET;
    p_uart_obj.rx_overflow = RX_OVFL_NONE;

    p_uart_obj.tx_fifo = xRingbufferCreate(MODBUS_RTU_TX_FIFO_LEN, RINGBUF_TYPE_ALLOWSPLIT);
    p_uart_obj.tx_fifo_mux = xSemaphoreCreateMutex();

    p_uart_obj.cfg_mux = xSemaphoreCreateMutex();

    // Configure parameters of an UART driver,
    // communication pins and install the driver
    uart_config_t uart_config = {
        .baud_rate = baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = parity_from_u8(parity),
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    ESP_ERROR_CHECK(uart_param_config(p_uart_obj.uart_num, &uart_config));

    ESP_ERROR_CHECK(uart_isr_register(p_uart_obj.uart_num, uart_modbus_intr_handler, NULL));
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
    ESP_ERROR_CHECK(uart_intr_config(p_uart_obj.uart_num, &uart_intr));
    ESP_LOGI("Modbus RTU", "UART Init, baudrate = %d, parity = %d", baudrate, parity);

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
    gpio_set_level(MODBUS_GPIO_DE_ID, MODBUS_GPIO_DE_INV);

    xTaskCreate(modbus_rtu_task, "uart_task", 2048, NULL, 7, NULL);
}

void modbus_uart_deinit() {
    vSemaphoreDelete(p_uart_obj.tx_done_sem);
    vSemaphoreDelete(p_uart_obj.rx_done_sem);
    vRingbufferDelete(p_uart_obj.tx_fifo);
    vSemaphoreDelete(p_uart_obj.tx_fifo_mux);
    vSemaphoreDelete(p_uart_obj.cfg_mux);
}

void modbus_uart_queue_send(const void* buf, size_t len) {
	xRingbufferSend(p_uart_obj.tx_fifo, buf, len, portMAX_DELAY);
	portYIELD();
}

void modbus_uart_set_baudrate(uint32_t baudrate) {
    uart_parity_t parity;
    xSemaphoreTake(p_uart_obj.cfg_mux, portMAX_DELAY);
    uart_set_baudrate(p_uart_obj.uart_num, baudrate);
    uart_get_parity(p_uart_obj.uart_num, &parity);
    p_uart_obj.char_duration_us = calc_char_us(baudrate, parity);
    xSemaphoreGive(p_uart_obj.cfg_mux);
}

void modbus_uart_set_parity(uint8_t parity) {
    uint32_t baudrate;
    xSemaphoreTake(p_uart_obj.cfg_mux, portMAX_DELAY);
    uart_set_parity(p_uart_obj.uart_num, parity_from_u8(parity));
    uart_get_baudrate(p_uart_obj.uart_num, &baudrate);
    p_uart_obj.char_duration_us = calc_char_us(baudrate, parity);
    xSemaphoreGive(p_uart_obj.cfg_mux);
}

void modbus_uart_set_tx_delay(uint32_t tx_delay) {
    xSemaphoreTake(p_uart_obj.cfg_mux, portMAX_DELAY);
    p_uart_obj.tx_delay_us = tx_delay;
    xSemaphoreGive(p_uart_obj.cfg_mux);
}
