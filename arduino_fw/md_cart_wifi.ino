/* Megadrive Wifi cart
 *  
 *  USB UPLOAD - Via bootloader!!! do not use auto-reboot via serial!!!
 */


#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiAP.h>
#include <soc/periph_defs.h>

//definititon of a root html page
#include "html_root.h"

//#define ISR_TRACE

#define APP_VERSION "ver. 0.3"

const char* ssid = "md_cart";
const char* password = "genesisflash";

WiFiServer server(80);


#if ARDUINO_USB_CDC_ON_BOOT == 0
#error USB CDC ON BOOT NOT DEFINED!
#endif

#if CONFIG_IDF_TARGET_ESP32S2 == 0
#error This code works only on ESP32-S2
#endif

#include <soc_log.h>

// pin config for WEMOS S2 mini board

#define PIN_LED 15
#define PIN_DATA 13
#define PIN_SHIFT 14
#define PIN_STORE 12

#define PIN_CEU1 18
#define PIN_CEU2 21

//#port B

//Data bus
// 33
#define PIN_D0  1
// 34
#define PIN_D1  2
// 35
#define PIN_D2  3
// 36
#define PIN_D3  4
// 37
#define PIN_D4  5
// 38
#define PIN_D5  6
// 39
#define PIN_D6  7
// 40
#define PIN_D7  8


#define M_D0 ((uint32_t) 1 << PIN_D0)
#define M_D1 ((uint32_t) 1 << PIN_D1)
#define M_D2 ((uint32_t) 1 << PIN_D2)
#define M_D3 ((uint32_t) 1 << PIN_D3)
#define M_D4 ((uint32_t) 1 << PIN_D4)
#define M_D5 ((uint32_t) 1 << PIN_D5)
#define M_D6 ((uint32_t) 1 << PIN_D6)
#define M_D7 ((uint32_t) 1 << PIN_D7)

//Mask for data bus
#define M_D (M_D0 | M_D1 | M_D2 | M_D3 | M_D4 | M_D5 | M_D6 | M_D7)

// Address bus port A - low 11 bits
#define PIN_A0 1
#define PIN_A1 2
#define PIN_A2 3
#define PIN_A3 4
#define PIN_A4 5
#define PIN_A5 6
#define PIN_A6 7
#define PIN_A7 8
#define PIN_A8 9
#define PIN_A9 10
#define PIN_A10 11

#define M_A0 ((uint32_t) 1 << PIN_A0)
#define M_A1 ((uint32_t) 1 << PIN_A1)
#define M_A2 ((uint32_t) 1 << PIN_A2)
#define M_A3 ((uint32_t) 1 << PIN_A3)
#define M_A4 ((uint32_t) 1 << PIN_A4)
#define M_A5 ((uint32_t) 1 << PIN_A5)
#define M_A6 ((uint32_t) 1 << PIN_A6)
#define M_A7 ((uint32_t) 1 << PIN_A7)
#define M_A8 ((uint32_t) 1 << PIN_A8)
#define M_A9 ((uint32_t) 1 << PIN_A9)
#define M_A10 ((uint32_t) 1 << PIN_A10)

//Mask for address bus
#define M_A (M_A0 | M_A1 | M_A2 | M_A3 | M_A4 | M_A5 | M_A6 | M_A7 | M_A8 | M_A9 | M_A10 )


// Data direction for level shifters
#define PIN_DDIR 15
#define M_DDIR (uint32_t) 1 << PIN_DDIR

#define PIN_OE 16
#define M_OE (uint32_t) 1 << PIN_OE

#define PIN_WE 17
#define M_WE (uint32_t) 1 << PIN_WE


//#define D1 delayMicroseconds(1)
// a small delay dor shifting the 595 clock at ~ 5.3MHz
#define D1 {uint8_t d1t = 5; while (d1t--) __asm ("nop"); }


#define M_LED (uint32_t) 1 << PIN_LED
#define M_DATA (uint32_t) 1 << PIN_DATA
#define M_SHIFT (uint32_t) 1 << PIN_SHIFT
#define M_STORE (uint32_t) 1 << PIN_STORE

#define M_CEU1 (uint32_t) 1 << PIN_CEU1
#define M_CEU2 (uint32_t) 1 << PIN_CEU2

// quick GPIO functions for ESP32-S2

// X = Pins 0 - 31
#define IO_PA(X,V) if (V) { GPIO.out_w1ts = (X);} else {GPIO.out_w1tc = (X);}
#define SET_IO_PA(X) GPIO.out_w1ts = (X)
#define CLR_IO_PA(X) GPIO.out_w1tc = (X)
#define READ_IO_PA(MASK)  ((GPIO.in) & (MASK))

// X = Pins 0 - 31, but mapped to GPIO 32 - 40
#define SET_IO_PB(X) GPIO.out1_w1ts.val = (X)
#define CLR_IO_PB(X) GPIO.out1_w1tc.val = (X)
#define READ_IO_PB(MASK)  ((GPIO.in1.val) & (MASK))
#define WRITE_IO_PB(X) GPIO.out1.val = (X)

#define INP_IO_PB(X) GPIO.enable1_w1tc.val = (X)
#define OUT_IO_PB(X) GPIO.enable1_w1ts.val = (X)

#define STATE_IDLE 0
#define STATE_SETUP_WRITE 1
#define STATE_ERASE 2
#define STATE_WRITE 3
#define STATE_VERIFY 4
#define STATE_START_GAME 5

#define STATE_ERROR 13

void setShiftRegister(uint8_t v);

uint8_t state = STATE_IDLE;
uint8_t stateErase = 0;
char errorMessage[64];

uint8_t value = 0;
uint8_t umode = 0;


#define MAX_ROM_SIZE (1024*1024)
//rom buffer
uint8_t* romBuf;
uint32_t romLen = 0;
uint32_t romPos = 0;

static void setAddress(uint32_t address);

//the total number of written bytes since the start of transaction
volatile uint32_t  weCount = 0;

//current index to isrRam
volatile uint32_t  weAddr = 0;

//total data length of the transaction
volatile uint32_t  weLen = 0;

// indicates the WE was asserted recently
volatile bool  pinWeAsserted = false;

// handle to the ISR  - used for masking the interrupts coming from the Console
intr_handle_t intrHandle = 0;

//The first 4 kbytes is a scratch area for receiving data
//slow ram starts from offset of 4 kbytes
volatile uint8_t isrRam[(4 + 64) * 1024];

volatile int intCounter = 0;

WiFiUDP udp;

// These 2 macros allow to quickly flip the Data bus direction to input or output
// output - keep Data Dir pin low
#define SET_DATA_DIR_OUT OUT_IO_PB(M_D); CLR_IO_PA(M_DDIR)
// input - keep Data Dir pin high
#define SET_DATA_DIR_INP INP_IO_PB(M_D); SET_IO_PA(M_DDIR);

// this function fully reconfigures the Data bus pins
static void setDataBusDirection(uint8_t m)
{
  pinMode(32 + PIN_D0, m);
  pinMode(32 + PIN_D1, m);
  pinMode(32 + PIN_D2, m);
  pinMode(32 + PIN_D3, m);
  pinMode(32 + PIN_D4, m);
  pinMode(32 + PIN_D5, m);
  pinMode(32 + PIN_D6, m);
  pinMode(32 + PIN_D7, m);

  if (OUTPUT == m) {
    CLR_IO_PA(M_DDIR); // output - keep Data Dir pin low
  } else {
    SET_IO_PA(M_DDIR); // input - keep Data Dir pin high
  }
}



static void writeByte(uint32_t addr, uint8_t d) {
  uint32_t v = d << PIN_D0;
  
  setAddress(addr);
  CLR_IO_PB(M_D);
  SET_IO_PB(v);

  // #WE -> L
  CLR_IO_PA(M_WE);
  D1;
  // #WE -> H
  SET_IO_PA(M_WE);
}

static uint8_t readByte(uint32_t addr) {
  uint32_t r;
  
  setAddress(addr);

  // #OE -> L
  CLR_IO_PA(M_OE);
  D1;
  r = READ_IO_PB(0b111111110);
  // #OE -> H
  SET_IO_PA(M_OE);
  return (uint8_t)(r >> PIN_D0);
}

static void readId(void) {
  uint8_t b0, b1;

// read id 
  SET_DATA_DIR_OUT;
  writeByte(0x5555, 0xAA);
  writeByte(0x2AAA, 0x55);
  writeByte(0x5555, 0x90);

  SET_DATA_DIR_INP;
  b0 = readByte(0);
  b1 = readByte(1);

// exit id
  SET_DATA_DIR_OUT;
  writeByte(0x5555, 0xAA);
  writeByte(0x2AAA, 0x55);
  writeByte(0x5555, 0xF0);

  Serial.printf("ID 0x%02x 0x%02x  %s\r\n", b0, b1, (b0 == 0xbf && b1 == 0xb7) ? "OK: 4MBit" : "Unknown ID");
}

static int chipErase(void) {
  uint8_t r;
  uint8_t cnt = 0;

  //Serial.printf("Erasing chip\r\n");
  SET_DATA_DIR_OUT;
  delay(10);
  writeByte(0x5555, 0xAA);
  writeByte(0x2AAA, 0x55);
  writeByte(0x5555, 0x80);
  writeByte(0x5555, 0xAA);
  writeByte(0x2AAA, 0x55);
  writeByte(0x5555, 0x10);

  delay(5);
  SET_DATA_DIR_INP;
  delay(5);
  r = readByte(0);  

  // poll until the top bit is 1
  while (!(r & 0b10000000) && cnt < 200) {
    delay(10);
    r = readByte(0);
    cnt++;
  }
  if (cnt >= 200) {
    Serial.printf("Erase failed!\r\n", cnt);
    return 1;
  }
  Serial.printf("Erase done cnt=%i\r\n", cnt);
  return 0;
}

static int writeAddress(uint32_t addr, uint8_t* buf, uint16_t len) {
  uint32_t i;
  uint32_t v, r1, r2;
  uint32_t bufPos = 0;
  uint32_t cnt;
  
  SET_DATA_DIR_OUT;


  for (i = 0; i < len; i++) {
      //magic write sequence 
      setAddress(0x5555);
      CLR_IO_PB(M_D);
      SET_IO_PB(0xAA << PIN_D0);
      // #WE -> L
      CLR_IO_PA(M_WE);
      D1;
      // #WE -> H
      SET_IO_PA(M_WE);   


      setAddress(0x2AAA);
      CLR_IO_PB(M_D);
      SET_IO_PB(0x55 << PIN_D0);
      // #WE -> L
      CLR_IO_PA(M_WE);
      D1;
      // #WE -> H
      SET_IO_PA(M_WE);   

      setAddress(0x5555);
      CLR_IO_PB(M_D);
      SET_IO_PB(0xA0 << PIN_D0);
      // #WE -> L
      CLR_IO_PA(M_WE);
      D1;
      // #WE -> H
      SET_IO_PA(M_WE);   
      

      // now write the actual data
      setAddress(addr + i);
      //set data
      v = buf[bufPos];
      bufPos += 2; //skip over a byte
      CLR_IO_PB(M_D);
      SET_IO_PB(v << PIN_D0);

      // #WE -> L
      CLR_IO_PA(M_WE);
      D1;
      // #WE -> H
      SET_IO_PA(M_WE);


      //check for completion of the write operation

      SET_DATA_DIR_INP;
      // check top 7th bit
      r1 = v & 0b10000000; 
      r2 = 1;

      cnt = 0;
      //when writing is finished the values of the top 7th bit should match
      while (r1 != r2 && cnt < 1000) {
        delayMicroseconds(1);
  
        // read the value
        // #OE -> L
        CLR_IO_PA(M_OE);
        D1;
        r2= READ_IO_PB(M_D7); //read only the top 7th bit     
        // #OE -> H
        SET_IO_PA(M_OE);
  
        r2 >>= PIN_D0;
        cnt++;  
      }
      if (cnt >= 1000) {
        return 1;
      }
      //Serial.printf("wait=%i\r\n", cnt);
      SET_DATA_DIR_OUT;
  }
  return 0;
}

static void readAddress(uint32_t addr, uint8_t* buf, uint16_t len) {
  uint16_t a;
  uint32_t r;

  // ensure the pins and transceivers are set up for reading
  SET_DATA_DIR_INP;
  
  //ensure the address bottom 11 bits are 0 (ie. multiply of 2048)
  addr &= ~(0b11111111111);
  setAddress(addr);

  // assume CE1 or CE2 is L - chip 1 or chip 2 is already enabled
  // assume len is less or equal 2048!
  for (a = 0; a < len; a++) {
    // bottom 11 bits of 19 bit address
    CLR_IO_PA(M_A);  //clear all
    SET_IO_PA(a << PIN_A0); //set 1s to the botom 11 bits

    // #OE -> L
    CLR_IO_PA(M_OE);


    // read the data bus
    r = READ_IO_PB(0b111111110);
    // #OE -> H
    SET_IO_PA(M_OE);

    // store the data
    buf[a] = (uint8_t)(r >> PIN_D0);
  }
}

static void dumpAddress(uint32_t addr) {

  uint16_t a;
  uint8_t buf[2048] = {0};

  readAddress(addr, buf, 40);

  for (a = 0; a < 40; a++) {
      Serial.printf(" b 0x%05x  %02x\r\n", addr + a, buf[a]);
  }
}



static void setupWifi() {
  WiFi.softAP(ssid, password);
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  server.begin();

  Serial.println("Server started");
  Serial.printf("%s / %s\r\n", ssid, password);
}

static void handleHttpOk(WiFiClient* client, const char* body, uint32_t len) {
    char cl[32] = {0};
    // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
    // and a content-type so the client knows what's coming, then a blank line:
    client->println("HTTP/1.1 200 OK");
    client->println("Content-type: text/html");
    if (body != NULL) {
        if (0 == len) {
            len = strlen(body);
        }
        sprintf(cl, "Content-Length: %i", len);
        client->println(cl);
    }

    client->println();
    
    // the content of the HTTP response follows the header:
    if (body != NULL) {
        client->write(body, len);
    }
}

static void getStatusText(char* response) {
    uint32_t proc;

    switch (state) {
        case STATE_ERASE:
        case STATE_SETUP_WRITE:
            sprintf(response, "E:0");
            return;
        case STATE_WRITE:
            proc = (romPos * 100) / romLen;
            sprintf(response, "F:%i", proc);
            return;
        case STATE_VERIFY:
            proc = (romPos * 100) / romLen;
            sprintf(response, "V:%i", proc);
            return;
        case STATE_IDLE:
        case STATE_START_GAME:
            sprintf(response, "finishedOK");
            return;
        case STATE_ERROR:
            sprintf(response,"X:%s", errorMessage);  
            return;
    }
    sprintf(response, "not implemented");
}

static void charCopy(char* dst, const char* src, uint16_t len) {
    dst[len] = 0;
    while (len--) {
        *dst = *src;
        dst++;
        src++;
    }
}

static void handleWifi() {
    bool isPost = false;
    bool downloading = false;
    bool statusRequest;
    uint16_t avCounter;
    uint32_t romLenDl = 0;
    bool romDl = false;
    bool retryWrite = false;

    delay(10);
    WiFiClient client = server.available();   // listen for incoming clients
    if (client) {                             // if you get a client,
        //Serial.println("New Client.");           // print a message out the serial port
        String currentLine = "";                // make a String to hold incoming data from the client
        statusRequest = false;
        while (client.connected()) {            // loop while the client's connected
            int avBytes = client.available();
            if (avBytes > 0) {             // if there's bytes to read from the client,
                // when downloading content data just store it to the rom buffer
                if (downloading) {
                    avCounter = 100;
                    if (romPos + avBytes <= romLen) {
                        //size_t r = client.readBytes(romBuf + romPos, avBytes);
                        size_t r = client.readBytes(romBuf + romPos, romLen - romPos);
                        romPos += r;
                        romLenDl = romPos;
                        if (romPos == romLen) {
                            avCounter = 1; //early break
                            state = STATE_SETUP_WRITE; //start the erase/write/verify sequence
                            stateErase = 0;
                            romPos = 0;
                        }
                    } else {
                        client.read();
                        romPos++;
                    }
                } else {
                    char c = client.read();             // read a byte, then           
                    if (c == '\n') {                    // if the byte is a newline character
                        // if the current line is blank, you got two newline characters in a row.
                        // that's the end of the client HTTP request, so send a response:
                        if (currentLine.length() == 0) {
                            // POST method was detected - ROM upload ?
                            if (isPost) {
                                if (romLen > 0 && romLen <= 1024 * 1024) {
                                    handleHttpOk(&client, "Downloading...", 0);
                                    downloading = true;
                                    //carry on reading data...                    
                                } else {
                                    handleHttpOk(&client, "X:File size too big", 0);
                                    break;
                                }
                            } else 
                            if (romDl) {
                                handleHttpOk(&client, (const char*)romBuf, 512 * 1024);
                                break;
                            } else
                            if (retryWrite) {
                                if (romLen > 0) {
                                    state = STATE_SETUP_WRITE; //start the erase/write/verify sequence
                                    stateErase = 0;
                                    romPos = 0;
                                    retryWrite = false;
                                    handleHttpOk(&client, "OK", 0);
                                } else {
                                    handleHttpOk(&client, "Error: No data to write", 0);
                                }
                                break;
                            }
                            else {
                                // status update requested
                                if (statusRequest) {
                                    char response[64] = {0};
                                    getStatusText(response);
                                    handleHttpOk(&client, response, 0);
                                    // when error is reported back to the http client then switch to idle
                                    if (STATE_ERROR == state) {
                                        state = STATE_IDLE;
                                    }
                                }
                                //anything else: send the main page
                                else {
                                    // check and update the version string
                                    char* appVersion = strstr(html_root, "ver. X.YZ");
                                    if (appVersion != NULL) {
                                        uint32_t spos = appVersion - html_root;
                                        uint32_t verLen = strlen(APP_VERSION);
                                        uint32_t htmlLen = strlen(html_root);

                                        charCopy((char*)romBuf, html_root, spos);
                                        charCopy((char*)romBuf + spos, APP_VERSION, verLen);
                                        charCopy((char*)romBuf + spos + verLen, html_root + spos + 9, htmlLen - (spos + 9));
                                        handleHttpOk(&client, (const char*)romBuf, 0);                                      
                                    } else
                                    // no version string found in the http page
                                    {
                                        handleHttpOk(&client, html_root, 0);
                                    }
                                }
                                // break out of the while loop:
                                break;
                            }
                        }
                        // got a new-line character while the currentLine contains some text
                        else 
                        {   
                            //Serial.println(currentLine.c_str());  // print the line to the serial monitor
    
                            // if you got a newline, then clear currentLine:
                            if (currentLine.startsWith("GET /status")) {
                                Serial.println(">> Detected Status request");
                                statusRequest = true;
                            } else
                            if (currentLine.startsWith("POST /rom.bin ")) {
                                Serial.println(">> Detected Rom upload");
                                isPost = true;
                                romPos = 0;
                                romLen = 0;
                            } else
                            if (currentLine.startsWith("GET /l.bin ")) {
                                uint32_t address;
                                // chip 1
                                CLR_IO_PA(M_CEU1); // ON
                                SET_IO_PA(M_CEU2); // OFF
                                for (address = 0; address < 512 * 1024; address += 2048) {
                                    readAddress(address, romBuf + address, 2048);
                                }
                                romDl = true;
                            } else
                            if (currentLine.startsWith("GET /h.bin ")) {
                                uint32_t address;
                                // chip 2
                                CLR_IO_PA(M_CEU2); // ON
                                SET_IO_PA(M_CEU1); // OFF
                                for (address = 0; address < 512 * 1024; address += 2048) {
                                    readAddress(address, romBuf + address, 2048);
                                }
                                romDl = true;
                            } else
                            if (currentLine.startsWith("GET /retrywrite ")) {
                                retryWrite = true;
                            }
                            if (currentLine.startsWith("Content-Length:")) {
                                romLen = atoi(currentLine.c_str() + 15);
                                Serial.printf(">> Detected content length=%i\r\n", romLen);
                            }
                            
                            currentLine = "";
                        }
                    }
                    else if (c != '\r') {  // if you got anything else but a carriage return character,
                        currentLine += c;      // add it to the end of the currentLine
                    }
                }
            } // client available (avBytes > 0)
            else {
                avCounter --;
                if (avCounter == 0) {
                    Serial.println("No more data available");
                    break;
                } else {
                    delay(1);                   
                }
            }
        } // client connected
        if (isPost) {
            Serial.printf(">> Downloaded %i / %i bytes (%02x %02x %02x %02x)\r\n", romLenDl, romLen, romBuf[0], romBuf[1], romBuf[2], romBuf[3]);
            if (romLenDl != romLen) {
                state = STATE_ERROR;
                sprintf(errorMessage, "Upload failed. Please try again.");
            } else
            // the size is not multiply 16kB - pad the rom buffer
            if (romLen & 0x3fff) {
                uint32_t maxLen = romLen + 16384;
                maxLen &= ~(0x3fff);
                memset(romBuf + romLen, 0xFF, maxLen - romLen);
                romLen = maxLen;
                Serial.printf("size adjusted to %i\n", romLen);
            }
        }
        // close the connection:
        client.flush();
        client.stop();

        //Serial.println("Client Disconnected.");
    }
}

static void setupPins(bool listenForIntr) {
    if (listenForIntr) {
        pinMode(PIN_WE, INPUT_PULLUP);
        GPIO.pin[PIN_WE].int_type = FALLING;
        GPIO.pin[PIN_WE].int_ena = 1;

        pinMode(PIN_OE, INPUT_PULLUP);
        GPIO.pin[PIN_OE].int_type = FALLING;
        GPIO.pin[PIN_OE].int_ena = 1;

        esp_intr_enable(intrHandle);
    } else {
        esp_intr_disable(intrHandle);
        pinMode(PIN_WE, OUTPUT);
        pinMode(PIN_OE, OUTPUT);
    }
}

// the setup function runs once when you press reset or power the board
void setup() {

  // Disable access to flash chips -> console released from reset -> game starts ASAP
  // Chip enable - outputs
  pinMode(PIN_CEU1, OUTPUT);
  pinMode(PIN_CEU2, OUTPUT);
  SET_IO_PA(M_CEU2); // OFF
  SET_IO_PA(M_CEU1); // OFF

  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_DATA, OUTPUT);
  pinMode(PIN_SHIFT, OUTPUT);
  pinMode(PIN_STORE, OUTPUT);

  // Adress buss - outputs
  pinMode(PIN_A0, OUTPUT);
  pinMode(PIN_A1, OUTPUT);
  pinMode(PIN_A2, OUTPUT);
  pinMode(PIN_A3, OUTPUT);
  pinMode(PIN_A4, OUTPUT);
  pinMode(PIN_A5, OUTPUT);
  pinMode(PIN_A6, OUTPUT);
  pinMode(PIN_A7, OUTPUT);
  pinMode(PIN_A8, OUTPUT);
  pinMode(PIN_A9, OUTPUT);
  pinMode(PIN_A10, OUTPUT);

  pinMode(PIN_DDIR, OUTPUT);

  //register the interrupt handler - required to detect WE and OE lines
  //the ISR routine is written in highint5.S
  esp_intr_alloc(ETS_GPIO_INTR_SOURCE, ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL5, NULL, NULL,  &intrHandle);

  setupPins(true);
 
  pinMode(0, OUTPUT);
  
  setDataBusDirection(INPUT);
  SET_IO_PA(1);

  SET_IO_PA(M_WE); // keep WE pin high (no write)
  SET_IO_PA(M_OE); // keep OE pin high (no read)

  SET_IO_PA(M_SHIFT); // keep Shift pin high
  SET_IO_PA(M_STORE); // keep Store pin high

  isrRam[0] = 0x5E;
  isrRam[1] = 0x6A;


  // Ensure USB CDC On Boot is Enabled (in Arduino IDE, Menu->Tools->Board_options list)  to view serial log on /dev/ttyACM0
  Serial.begin (115200);

  delay(1500);
  CLR_IO_PA(1);

  romBuf = (uint8_t*) malloc(MAX_ROM_SIZE);
  if (NULL == romBuf) {
    Serial.println("Rom buffer alloction failed");
  } else {
    Serial.printf("Rom buffer allocated st=%p stw1tc=%p \r \n", &GPIO.status, &GPIO.status_w1tc);
  }
  setupWifi();

}

void setShiftRegister(uint8_t v)
{
  CLR_IO_PA(M_STORE); // Store pin Low
  D1;
  CLR_IO_PA(M_SHIFT); D1; IO_PA(M_DATA, v & 0b10000000); D1; SET_IO_PA(M_SHIFT); D1;
  CLR_IO_PA(M_SHIFT); D1; IO_PA(M_DATA, v & 0b01000000); D1; SET_IO_PA(M_SHIFT); D1;
  CLR_IO_PA(M_SHIFT); D1; IO_PA(M_DATA, v & 0b00100000); D1; SET_IO_PA(M_SHIFT); D1;
  CLR_IO_PA(M_SHIFT); D1; IO_PA(M_DATA, v & 0b00010000); D1; SET_IO_PA(M_SHIFT); D1;
  CLR_IO_PA(M_SHIFT); D1; IO_PA(M_DATA, v & 0b00001000); D1; SET_IO_PA(M_SHIFT); D1;
  CLR_IO_PA(M_SHIFT); D1; IO_PA(M_DATA, v & 0b00000100); D1; SET_IO_PA(M_SHIFT); D1;
  CLR_IO_PA(M_SHIFT); D1; IO_PA(M_DATA, v & 0b00000010); D1; SET_IO_PA(M_SHIFT); D1;
  CLR_IO_PA(M_SHIFT); D1; IO_PA(M_DATA, v & 0b00000001); D1; SET_IO_PA(M_SHIFT); D1;

  SET_IO_PA(M_STORE); // Store pin High
  D1;
}

static char hexVal[16] = { '0', '1', '2', '3','4','5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };

void printHex(uint8_t v) {
  char b[3];
  b[0] = hexVal[v >> 4];
  b[1] = hexVal[v & 0xF];
  b[2] = 0;
  Serial.write(b);
  
}

static void setAddress(uint32_t address) {
  uint16_t low = (address & 0b11111111111) << PIN_A0;

  // set top 8 bits of 19 bit address
  setShiftRegister((uint8_t)(address >> 11));

  // bottom 11 bits of 19 bit address
  CLR_IO_PA(M_A);
  SET_IO_PA(low);
}

static void releaseConsoleReset(int toggleTime) {
    SET_DATA_DIR_INP;
    // keep access to both chip disabled -> releases the console being in reset
    SET_IO_PA(M_CEU1); // keep CEU1 high (disabled)
    SET_IO_PA(M_CEU2); // keep CEU2 high (disabled)  
    if (toggleTime) {
        //game runs for the length of 'toggleTime'
        delay(toggleTime);
        CLR_IO_PA(M_CEU1); // keep CEU1 low (enabled) ->puts the console into reset again
    }
}

static void eraseChips() {
  stateErase++;

  if (1 == stateErase) {
    Serial.printf("Erasing Chip 1\r\n");
    CLR_IO_PA(M_CEU1); // ON
    SET_IO_PA(M_CEU2); // OFF
    //  readId();
    if (chipErase()) {
        goto error_erase;
    }
  }
  else if (2 == stateErase) {
    Serial.printf("Erasing Chip 2\r\n");
    CLR_IO_PA(M_CEU2); // ON
    SET_IO_PA(M_CEU1); // OFF
    // readId();
    if (chipErase()) {
        goto error_erase;
    }
  } else {
    state = STATE_WRITE;
    romPos = 0;
    stateErase = 0;
    //release reset, wait then reset again
    releaseConsoleReset(500); 
    
  }
  return;
error_erase:
  state = STATE_ERROR;
  sprintf(errorMessage, "Chip erase failed. Please try again.");
  romPos = 0;
  stateErase = 0;
}

static void writeChips() {
    uint8_t i;
    uint32_t address;

    // check the writing has finished
    if (romPos >= romLen) {
        state = STATE_VERIFY;
        romPos = 0;
        return;
    }

    // write 16 kbytes at a time, that is 8kb for each chip
    for (i = 0; i < 4; i++) {
        address = romPos >> 1; //writing chip address is half of the romPos
    
        // chip 1 (L bytes - at offset +1 because the data are big endian )
        CLR_IO_PA(M_CEU1); // ON
        SET_IO_PA(M_CEU2); // OFF
        if (writeAddress(address, romBuf + romPos +1, 2048)) {
            goto error_write;
        }

        // chip 2 (H bytes - at offset + 0 because the data are big endian )
        CLR_IO_PA(M_CEU2); // ON
        SET_IO_PA(M_CEU1); // OFF
        if (writeAddress(address, romBuf + romPos, 2048)) {
            goto error_write;
        }

        romPos += 4096;
    }
    return;
error_write:
    state = STATE_ERROR;
    sprintf(errorMessage, "Chip write failed.");
    romPos = 0;
}

static void verifyChips() {
    uint8_t i;
    uint32_t address, j;
    volatile uint32_t romAddr;
    uint8_t readBuf[2048];

    // check the verification has finished
    if (romPos >= romLen) {
        state = STATE_START_GAME;
        romPos = 0;
        romLen = 0;
        return;
    }
    //Serial.printf("verify addr=0x%06x\r\n", romPos);
    // read 32 kbytes at a time, that is 16kb for each chip
    for (i = 0; i < 4; i++) {
        address = romPos >> 1; //writing chip address is half of the romPos
    
        // chip 1
        CLR_IO_PA(M_CEU1); // ON
        SET_IO_PA(M_CEU2); // OFF
        readAddress(address, readBuf, 2048);

        //verify odd bytes (L offset +1 because data are big endian)
        romAddr = romPos + 1;
        for (j = 0; j < 2048; j++) {
            if (romBuf[romAddr] != readBuf[j]) {
                state = STATE_ERROR;
                sprintf(errorMessage, "verify failed on CHIP1, chip addr: 0x%06x", address + j);
                return;
            }
            romAddr+=2;
        }

        // chip 2;
        CLR_IO_PA(M_CEU2); // ON
        SET_IO_PA(M_CEU1); // OFF
        readAddress(address, readBuf, 2048);

        //verify even bytes (H offset +0 because data are big endian)
        romAddr = romPos;
        for (j = 0; j < 2048; j++) {
            if (romBuf[romAddr] != readBuf[j]) {
                state = STATE_ERROR;
                sprintf(errorMessage, "verify failed on CHIP 2, chip addr: 0x%06x", address + j);
                return;
            }
            romAddr+=2;
        }

        romPos += 4096;
    }
}


// the loop function runs over and over again forever
void loop() {

    switch (state) {
    case STATE_SETUP_WRITE:
        setupPins(false); // setup WE and OE for Control
        state = STATE_ERASE;
        break;
    case STATE_ERASE:
        eraseChips();
        break;
    case STATE_WRITE:
        writeChips();        
        break;
    case STATE_VERIFY:
        verifyChips();
        break;
    case STATE_START_GAME:
        state = STATE_IDLE;
        setupPins(true); // setup WE and OE for external Iterrupt reception
        releaseConsoleReset(0);
    }
    handleWifi();
    // Write from console, read by MCU
    if (pinWeAsserted) {
        char t[256];
        pinWeAsserted = false;       
        t[0] = 0;
        if (isrRam[0] == 0x5E && isrRam[1] == 3) {
            sprintf((char*)t, "%s\r\n", isrRam + 2);   
        }
#ifdef ISR_TRACE
        else {
            sprintf((char*)t, "WE intr: %i (0x%02x 0x%02x 0x%02x 0x%02x)\r\n", weCount, isrRam[0], isrRam[1], isrRam[2], isrRam[3]);
        }
        Serial.printf(t);

#endif
        //send non-empty strings only
        if (t[0]) {
            udp.beginPacket("192.168.4.2", 0x5E6A);
            udp.write((const uint8_t *)t, strlen(t) + 1);
            udp.endPacket();
        }
    }
   
}
