# libA9
Ai-Thinker A9 (A9G) GSM module library for microcontrollers (initially for ESP32 family on ESP-IDF framework)

✅Current features:
  *  No AT commands needed
  *  Simple methods for HTTP GET/POST requests (A9/A9G does not support HTTPS)
  *  Module gets ready to make requests with only one method ".start()"
  *  Supports hard reset with power enable pin, if the power of the module is controlled via switch (ex. MOSFET)
  *  Error and info logging

🔶Planned features:
  *  Surrounding cell stations' information
  
❌Not supported: 
  * SMS
  * Calls

# How to use
  1. ```#include "libA9.h" // include the header file``` 
  2. ```A9 gsm = A9(..)    // create an A9 object with desired parameters (uart_num, tx_pin, rx_pin, power_enable_pin, invert_pwr_en_pin, APN)```
  3. ```gsm.start()        // gets the module ready for HTTP requests, returns 1 if succeeded```
  4. ```gsm.http_post("http://webhook.site/d9781ab3-6cae-4f62-90df-a875d9b3e1f7","Hello world!") //making the request, returns HTTP status code```
  5. ```gsm.stop()         // shuts the module down```

# Contact me
Feel free to contact me: talhadduman[at]gmail[dot]com
