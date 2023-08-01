# libA9
Ai-Thinker A9 / A9G is a fairly low priced GSM module with capability of GPRS and GPS (GPS only exists in A9G).

This library implements several features of the module for some microcontroller families (initially for ESP32 family on ESP-IDF framework).  
No AT commands needed, simple to make HTTP requests, module gets ready within a single method.

✅Current features:
  *  HTTP GET / POST requests (A9 / A9G does not support HTTPS)
  *  Fetch GSM network time in UNIX timestamp
  *  Get & set IMEI number
  *  Fetch surrounding cell tower information
  *  Optional hard reset with power enable pin (when the module is powered through switch or a MOSFET)
  *  Optional error and info logging

⏳Planned features:
  *  ...

⛔Not supported: 
  *  SMS
  *  Calls
  *  TCP/IP socket connections

## How to use
  1. include the header file  
     ```#include "libA9.h"```  
  2. create an A9 instance with your parameters (uart_num, tx_pin, rx_pin, power_enable_pin, invert_pwr_en_pin, APN)  
     ```A9 gsm = A9(..);```  
  3. set the power on and get the module ready to do HTTP requests, returns 1 on success  
     ```gsm.start();```  
  4. make the HTTP request, returns HTTP status code  
     ```gsm.http_post("http://webhook.site/d9781ab3-6cae-4f62-90df-a875d9b3e1f7","Hello world!");```  
  5. shuts the module down  
     ```gsm.stop();```  

---
[![](https://visitcount.itsvg.in/api?id=libA9&label=Repo%20views&icon=8)](https://visitcount.itsvg.in)

## Contact me
- email: talhadduman[at]gmail[dot]com
