/* 
*  Author: Talha Duman
*/

#include <stdint.h>
#include <time.h>
#include "driver/uart.h"

//Size of the serial RX circular buffer. Small sizes may cause long responses to be partitially lost. (Default of 2048)
#define A9_UART_RX_BUFFER_SIZE 2048
//Size of the HTTP buffer where responses are stored until a new request is made. (Default of 2048)
#define A9_HTTP_BUFFER_SIZE 2048
//Comment out below the line if logging is unwanted
#define A9_ENABLE_LOGS
//Uncomment below the line to print raw received chars to serial port. 
//#define A9_ENABLE_RAW_PRINT

class A9{
    public:
        A9(uart_port_t uart_num, int tx_pin, int rx_pin, int power_enable_pin, int invert_pwr_en_pin, const char* APN);
        int8_t start();
        int16_t http_get(const char *URL);
        int16_t http_post(const char *URL, const char *body);
        char* read_http_response();
        uint32_t get_gsm_time();
        void stop();
    private:
        uart_port_t uart_num;
        uart_config_t uart_config = {
            .baud_rate = 115200,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .rx_flow_ctrl_thresh = 122,
            .source_clk = UART_SCLK_DEFAULT
        };
        
        int tx_pin;
        int rx_pin;
        int power_enable_pin;
        int invert_pwr_en_pin;

        char APN[32];

        char receive_buffer[A9_UART_RX_BUFFER_SIZE];        
        char http_buffer[A9_HTTP_BUFFER_SIZE];
        char *http_response;

        uint32_t sync_mcu_timestamp;
        uint32_t sync_unix_timestamp;

        void send_to_serial(const char* str);
        void flush_serial();
        uint8_t check_incoming_byte();
        char read_incoming_byte();

        int8_t wait_for_pattern(const char *pattern, uint32_t timeout_ms);
        char*  get_received_line();

        void module_power_on();
        void module_power_off();
        uint32_t millis();
};
