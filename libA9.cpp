/*
*  Author: Talha Duman
*/

#include <string.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "libA9.h"

#define A9_LOGGING_TAG "libA9"

/*
*   uart_num : the UART port number of which A9 module is connected to. (Ex. UART_NUM_1).
*   tx_pin : Tx pin of ESP32, Rx pin of A9. Set -1 to use default UART Tx pin. 
*   rx_pin : Rx pin of ESP32, Tx pin of A9. Set -1 to use default UART Rx pin.
*   power_enable_pin : The IO pin that enables the power of A9 module. Set -1 if not used.
*   invert_pwr_en_pin : Whether the power enable pin should be logically inverted or not. Set 1 to invert, 0 to not. Set -1 if not used.
*/
A9::A9(uart_port_t uart_num, int tx_pin, int rx_pin, int power_enable_pin, int invert_pwr_en_pin, const char* APN) : uart_num(uart_num), power_enable_pin(power_enable_pin), invert_pwr_en_pin(invert_pwr_en_pin){
    strcpy(this->APN,APN);
    
    #ifdef A9_ENABLE_RAW_PRINT
        uart_driver_install(UART_NUM_0,256,0,0,NULL,0); //Raw print only
    #endif

    //Install UART driver
    ESP_ERROR_CHECK(uart_driver_install(uart_num, A9_UART_RX_BUFFER_SIZE, 0, 0, NULL, 0));
    //Configure UART parameters
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));

    //Assign pins
    int _rx_pin = (rx_pin == -1) ? UART_PIN_NO_CHANGE : rx_pin;
    int _tx_pin = (tx_pin == -1) ? UART_PIN_NO_CHANGE : tx_pin;
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, _tx_pin, _rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    //Configure GPIO for power enable pin if configured
    if(power_enable_pin != -1){
        gpio_config_t pin_config = {
            .pin_bit_mask = (1ULL<<power_enable_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = (invert_pwr_en_pin == 1) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
            .pull_down_en = (invert_pwr_en_pin != 1) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };

        ESP_ERROR_CHECK(gpio_config(&pin_config));
        module_power_off();
    }

    sync_mcu_timestamp = 0;
    sync_unix_timestamp = 0;
}

/*
*   Enable power of module and get it ready for HTTP connections
*/
int8_t A9::start(){

    module_power_off();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    flush_serial();
    module_power_on();

    int8_t stat;
    char* received_line;

    send_to_serial("AT+RST=1\r");
    stat = wait_for_pattern("READY",30000);
    if (stat == -1)
    {
        #ifdef A9_ENABLE_LOGS
            ESP_LOGW(A9_LOGGING_TAG,"AT+RST=1 timeout");
        #endif
        return -1; //Timeout when resetting module
    }

    vTaskDelay(500 / portTICK_PERIOD_MS);
    flush_serial();
    send_to_serial("AT+CREG?\r");
    stat = wait_for_pattern("+CREG:", 1000);
    received_line = get_received_line();
    if (stat == -1)
    {
        #ifdef A9_ENABLE_LOGS
            ESP_LOGW(A9_LOGGING_TAG,"Network reg. status timeout");
        #endif
        return -2; //Timeout when getting network reg. status
    }

    sscanf(received_line,"+CREG: %*d,%d",(int*)&stat);
    if((!(stat == 1)||(stat == 5))){
        #ifdef A9_ENABLE_LOGS
            ESP_LOGW(A9_LOGGING_TAG,"Network not registered");
        #endif
        return -3; //Network not registered.
    }

    vTaskDelay(250 / portTICK_PERIOD_MS);
    flush_serial();
    send_to_serial("AT+CGATT=1\r");
    stat = wait_for_pattern("+CGATT:1", 30000);
    if (stat == -1)
    {
        #ifdef A9_ENABLE_LOGS
            ESP_LOGW(A9_LOGGING_TAG,"Network attach timeout");
        #endif
        return -4; //Timeout when attaching network
    }

    vTaskDelay(250 / portTICK_PERIOD_MS);
    flush_serial();
    char apn_command[128];
    sprintf(apn_command,"AT+CGDCONT=1,\"IP\",\"%s\"\r",this->APN);
    send_to_serial(apn_command);
    stat = wait_for_pattern("OK", 30000);
    if (stat == -1)
    {
        #ifdef A9_ENABLE_LOGS
            ESP_LOGW(A9_LOGGING_TAG,"APN timeout");
        #endif
        return -5; //APN timeout.
    }
    
    //Later add cell info here.

    vTaskDelay(250 / portTICK_PERIOD_MS);
    flush_serial();
    send_to_serial("AT+CGACT=1,1\r");
    stat = wait_for_pattern("OK", 45000);
    if (stat == -1)
    {
        #ifdef A9_ENABLE_LOGS
            ESP_LOGW(A9_LOGGING_TAG,"Network activate PDP timeout");
        #endif
        return -6; //Timeout when getting network reg. status
    }

    return 1; //Success
}

/*
*   Makes an HTTP GET request to the given URL. (URL length should not be bigger than ~1000)
*   Return value is the status code of request. (Ex. 200: HTTP OK)
*/
int16_t A9::http_get(const char* URL){
    
    sprintf(http_buffer,"AT+HTTPGET=\"%s\"\r",URL); //Using http_buffer for storing AT commands to save RAM
    flush_serial();
    send_to_serial(http_buffer);
    int8_t stat = wait_for_pattern("OK",45000);

    if(stat == -1){
        #ifdef A9_ENABLE_LOGS
            ESP_LOGI(A9_LOGGING_TAG,"HTTP timeout");
        #endif
        return -1; //HTTP timeout
    }

    vTaskDelay(250 / portTICK_PERIOD_MS);
    http_buffer[A9_HTTP_BUFFER_SIZE - 1] = '\0';
    int16_t byte_count = uart_read_bytes(uart_num, http_buffer, A9_HTTP_BUFFER_SIZE - 1, 500 / portTICK_PERIOD_MS);
    http_buffer[byte_count] = '\0';

    if (byte_count >= (A9_HTTP_BUFFER_SIZE - 1))
    {
        #ifdef A9_ENABLE_LOGS
            ESP_LOGI(A9_LOGGING_TAG,"HTTP buffer is full, data may be partitially lost. Try increasing A9_HTTP_BUFFER_SIZE");
        #endif
        return -2; //HTTP buffer is full
    }else if(byte_count <= 0){
        return -3; //No response
    }

    uint16_t response_code;
    char* response_code_index = strstr(http_buffer,"HTTP");
    if(response_code_index == NULL)
    {
        #ifdef A9_ENABLE_LOGS
            ESP_LOGI(A9_LOGGING_TAG,"Response code parsing error.");
        #endif
        return -4; //Cannot parse response code
    }

    sscanf(response_code_index,"HTTP/%*d.%*d %d OK %*s",(int*)&response_code);
    if(response_code != 200)
    {
        #ifdef A9_ENABLE_LOGS
            ESP_LOGI(A9_LOGGING_TAG,"HTTP bad request:%d",response_code);
        #endif
        return response_code;
    }

    char* index = strstr(http_buffer,"Content-Length");
    if (index == NULL)
    {
        return -5; //Cannot parse Content-Length header
    }
    
    uint16_t body_byte_count;
    sscanf(index,"Content-Length: %d %*s",(int*)&body_byte_count);

    http_response = (char*) (http_buffer + byte_count - body_byte_count - 2);
    
    #ifdef A9_ENABLE_LOGS
        ESP_LOGI(A9_LOGGING_TAG,"HTTP 200 OK:%s",http_response);
    #endif

    return 200; //HTTP success    
}

/*
*   Makes an HTTP POST request to the given URL. (URL + BODY length should not be bigger than ~1000)
*   Return value is the status code of request. (Ex. 200: HTTP OK)
*/
int16_t A9::http_post(const char* URL, const char* body){
    
    sprintf(http_buffer,"AT+HTTPPOST=\"%s\",\"text/plain\",\"%s\"\r",URL,body); //Using http_buffer for storing AT commands to save RAM
    flush_serial();
    send_to_serial(http_buffer);
    int8_t stat = wait_for_pattern("OK",45000);

    if(stat == -1){
        #ifdef A9_ENABLE_LOGS
            ESP_LOGI(A9_LOGGING_TAG,"HTTP timeout");
        #endif
        return -1; //HTTP timeout
    }

    vTaskDelay(250 / portTICK_PERIOD_MS);
    http_buffer[A9_HTTP_BUFFER_SIZE - 1] = '\0';
    int16_t byte_count = uart_read_bytes(uart_num, http_buffer, A9_HTTP_BUFFER_SIZE - 1, 500 / portTICK_PERIOD_MS);
    http_buffer[byte_count] = '\0';

    if (byte_count >= (A9_HTTP_BUFFER_SIZE - 1))
    {
        #ifdef A9_ENABLE_LOGS
            ESP_LOGI(A9_LOGGING_TAG,"HTTP buffer is full, data may be partitially lost. Try increasing A9_HTTP_BUFFER_SIZE");
        #endif
        return -2; //HTTP buffer is full
    }else if(byte_count <= 0){
        return -3; //No response
    }

    uint16_t response_code;
    char* response_code_index = strstr(http_buffer,"HTTP");
    if(response_code_index == NULL)
    {
        #ifdef A9_ENABLE_LOGS
            ESP_LOGI(A9_LOGGING_TAG,"Response code parsing error.");
        #endif
        return -4; //Cannot parse response code
    }

    sscanf(response_code_index,"HTTP/%*d.%*d %d OK %*s",(int*)&response_code);
    if(response_code != 200)
    {
        #ifdef A9_ENABLE_LOGS
            ESP_LOGI(A9_LOGGING_TAG,"HTTP bad request:%d",response_code);
        #endif
        return response_code;
    }

    char* index = strstr(http_buffer,"Content-Length");
    if (index == NULL)
    {
        return -5; //Cannot parse Content-Length header
    }
    
    uint16_t body_byte_count;
    sscanf(index,"Content-Length: %d %*s",(int*)&body_byte_count);

    http_response = (char*) (http_buffer + byte_count - body_byte_count - 2);
    
    #ifdef A9_ENABLE_LOGS
        ESP_LOGI(A9_LOGGING_TAG,"HTTP 200 OK:%s",http_response);
    #endif

    return 200; //HTTP success    
}

/*
*   Returns a pointer to the response body of the recent request.
*/
char* A9::read_http_response(){
    return http_response;
}

/*
*   Get GSM network time in Unix seconds timestamp.
*/
uint32_t A9::get_gsm_time(){

    if(sync_unix_timestamp != 0){ //Already syncronised, calculate & return current timestamp
    return ((millis() - sync_mcu_timestamp)/1000) + sync_unix_timestamp;
    }

    flush_serial();
    send_to_serial("AT+CCLK?\r");
    int32_t command_time = millis(); //in MCU milliseconds

    int8_t stat;
    stat = wait_for_pattern("+CCLK:",5000);
    if (stat == -1)
    {
        return 0; //Cannot get time.
    }

    uint8_t years_since_2000;
    uint8_t month;
    uint8_t day;
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
    stat = sscanf(get_received_line(), "+CCLK: \"%hhd/%hhd/%hhd,%hhd:%hhd:%hhd%*s\"", &years_since_2000, &month, &day, &hours, &minutes, &seconds);

    if(stat != 6){
        #ifdef A9_ENABLE_LOGS
            ESP_LOGI(A9_LOGGING_TAG,"GSM time parsing error");
        #endif
        return 0; //Time parsing error
    }
    
    struct tm gmt_time = {
        .tm_sec = seconds,
        .tm_min = minutes,
        .tm_hour = hours,
        .tm_mday = day,
        .tm_mon = (month - 1),
        .tm_year = (years_since_2000 + 100),
        .tm_wday = 0,
        .tm_yday = 0,
        .tm_isdst = 0};

    uint32_t gsm_time = (mktime(&gmt_time)); //in Unix seconds

    //Syncronising time
    sync_mcu_timestamp = command_time;
    sync_unix_timestamp = gsm_time;

    //Calculate & return current timestamp
    return ((millis() - sync_mcu_timestamp)/1000) + sync_unix_timestamp;
}

/*
*   Disable the power of module and flush incoming serial buffer.
*/
void A9::stop(){
    module_power_off();
    flush_serial();
}

/** PRIVATE METHODS**/

//Sends given string to A9 serial port
void A9::send_to_serial(const char* str){
    uint16_t len = strlen(str);
    uart_write_bytes(uart_num,str,len);
}

//Flushes internal uart rx buffer
void A9::flush_serial(){
    //Cleans UART rx buffer.
    uart_flush_input(uart_num);
}

//Waits until given pattern is received. Returns 1 when success and returns -1 when timeout
int8_t A9::wait_for_pattern(const char *pattern, uint32_t timeout_ms){
    
    receive_buffer[0] = '\0';
    uint16_t cursor = 0;

    uint32_t start_time = millis();
    while ((millis() - start_time) < timeout_ms){

        if (check_incoming_byte())
        {
            char tmp = read_incoming_byte();

            #ifdef A9_ENABLE_RAW_PRINT
                uart_write_bytes(UART_NUM_0,&tmp,1);//Debug only
            #endif

            receive_buffer[cursor] = tmp;
            if (cursor<(A9_UART_RX_BUFFER_SIZE - 2))
            {
                cursor++;
            }
            else
            {
                #ifdef A9_ENABLE_LOGS
                    ESP_LOGW(A9_LOGGING_TAG,"receive_buffer is full, data may be corrupted. Try increasing A9_UART_RX_BUFFER_SIZE");
                #endif
            }
            
            if (tmp == 10 /*LF*/) 
            {
                receive_buffer[cursor] = '\0';
                cursor = 0;

                char* sub = strstr(receive_buffer,pattern);
                if (sub != NULL) {
                return 0; //Pattern found.
                }
            }
            continue;
        }
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
    return -1; //Timeout       
};

//Returns the recently received string which is located in received_buffer, terminated by LF char
char* A9::get_received_line(){
    return receive_buffer;
}

//Checks if there exists an incoming byte
uint8_t A9::check_incoming_byte(){
    int16_t length = 0;
    uart_get_buffered_data_len(uart_num, (size_t*)&length);
    return (length>0);
}

//Reads an incoming byte.
char A9::read_incoming_byte(){
    char byte = 0;
    uart_read_bytes(UART_NUM_1, &byte, 1, 10);
    return byte;
}

//Gets mcu time in milliseconds
uint32_t A9::millis(){
    return (esp_timer_get_time()/1000);
}

//Power on
void A9::module_power_on(){
    if(power_enable_pin != -1){
    gpio_set_level((gpio_num_t) power_enable_pin, invert_pwr_en_pin != 1);
    }
}

//Power off
void A9::module_power_off(){
    if(power_enable_pin != -1){ 
    gpio_set_level((gpio_num_t) power_enable_pin, invert_pwr_en_pin == 1);
    }
}
