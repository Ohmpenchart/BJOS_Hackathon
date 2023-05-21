#include <Wire.h>               //IIC
#include <Adafruit_NeoPixel.h>  //RGB LED
#include <Adafruit_BMP280.h>    //IIC pressure sensor
#include <Adafruit_HTS221.h>    //IIC humidity sensor
#include <Adafruit_MPU6050.h>   //IIC gyro sensor
#include "DHT.h"
#include <WiFi.h>  // WiFi
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include <esp_wpa2.h>     // wpa2 library for connections to Enterprise networks
#include <ESPDateTime.h>  // DateTime lib for ESP32/ESP8266
#include <DateTimeTZ.h>   // Timezone definition

#include "mySSID.h"  // define your SSID names, user names & passwords

#include "mbedtls/aes.h"
#include <string.h>
#include <stdint.h>


// define two tasks for Blink & AnalogRead
void TaskWiFiBlink(void *pvParameters);
void TaskShowRGB(void *pvParameters);
void TaskReadBMP280(void *pvParameters);
void TaskReadFireIR(void *pvParameters);
void TaskPreprocessing_Temp1(void *pvParameters);
void TaskPreprocessing_Temp(void *pvParameters);
void TaskPreprocessing_FireIR(void *pvParameters);
void TaskNetPie(void *pvParameters);

WiFiClient espClient;
PubSubClient client(espClient);

// We will pass this class as parameters of tasks
class Lock {
  SemaphoreHandle_t IIC, Serial;
  //constructure
public:
  Lock() {
    // create mutexes
    IIC = xSemaphoreCreateMutex();
    Serial = xSemaphoreCreateMutex();
  }
  // inline function is kind of text replacement
  inline void lockIIC() {
    while (xSemaphoreTake(IIC, 100) == pdFALSE)
      ;
  }
  inline void lockSerial() {
    while (xSemaphoreTake(Serial, 100) == pdFALSE)
      ;
  }
  inline void unlockIIC() {
    xSemaphoreGive(IIC);
  }
  inline void unlockSerial() {
    xSemaphoreGive(Serial);
  }
};
volatile bool rg = false;
// unlike Lock, we will share the handle of FIFO by global variable, not by parameter passing.
QueueHandle_t BMP280_Temp_PreQ{ NULL };   // FIFO between BMP280 and TaskPreprocessing_Temp_ref task
QueueHandle_t DHT11_Temp_PreQ{ NULL };  // FIFO between DHT11 and TaskPreprocessing_Temp_coil task

QueueHandle_t FireIR_PreQ{ NULL };         // FIFO between readFireIR and TaskPreprocessing_FireIR task

//Queue from Preprocessor to Netpie
QueueHandle_t FireIRInfo_NetpieQ{ NULL };  // FIFO between TaskNetpie and TaskPreprocessing_FireIR task
QueueHandle_t TempInfo_NetpieQ{ NULL };  // FIFO between TaskNetpie and TaskPreprocessing_Temp_ref task
QueueHandle_t Temp1Info_NetpieQ{ NULL };   // FIFO between TaskNetpie and TaskPreprocessing_Temp_coil task
// Define structure for data with timestamp (time in seconds since 1970)
struct float_timestamp {
  time_t secs;
  float data;
  float_timestamp(time_t s = 0, float d = 0)
    : secs(s), data(d) {}
};

bool connectWiFi(bool verbose = false);
bool updateNTPTime(bool verbose = false);

// the setup function runs once when you press reset or power the board
void setup() {
  // declare SCA and SCL pin numbers
  const int SDA_PIN{ 41 }, SCL_PIN{ 40 };
  // use the default "Wire" declared somewhere else
  
  extern TwoWire Wire;
  // start a common resource, IIC Bus
  Wire.begin(SDA_PIN, SCL_PIN);
  // start another common resource, Serial Port
  Serial.begin(115200);
  // creat a lock to pass it to all the tasks
  Lock *lock = new Lock;
  // connect to WiFi, status == true if connected
  bool status = connectWiFi(true);
  // update NTP Time if WiFi is connected
  if (status) {
    DateTime.setServer("th.pool.ntp.org");
    DateTime.setTimeZone(TZ_Asia_Bangkok);
    updateNTPTime(true);
  }
  
  // Now set up two tasks to run independently.
  xTaskCreate(
    TaskWiFiBlink,               // function name to run the task
    "Blink if WiFi connected.",  // A name just for humans
    1024,                        // This stack size can be checked & adjusted by reading the Stack Highwater
    NULL,                        // no parameters passed
    2,                           // Priority, with 3 (configMAX_PRIORITIES - 1) being the highest, and 0 being the lowest.
    NULL                         // No handle
  );

  xTaskCreate(
    TaskShowRGB, "TaskShowRGB", 1024,  // Stack size
    NULL, 0,                             // Priority
    NULL);

  xTaskCreate(
    TaskReadBMP280, "Task Read BMP280", 2048,  // Stack size. Note 1024 is not enough, and coz CPU panic!
    (void *)lock,                              // pass lock as a parameter
    1,                                         // Priority
    NULL);

  xTaskCreate(
    TaskReadDHT11, "Task Read DHT11", 2048,  // Stack size
    (void *)lock,                                // pass lock as a parameter
    1,                                           // Priority
    NULL);

  xTaskCreate(
    TaskReadFireIR, "Task Read FireIR", 2048,  // Stack size
    (void *)lock,                              // pass lock as a parameter
    1,                                         // Priority
    NULL);

  xTaskCreate(
    TaskPreprocessing_Temp, "Preprocess Temp_ref Data", 2048,  // Stack size
    (void *)lock,                                          // pass lock as a parameter
    1,                                                     // Priority
    NULL);

  xTaskCreate(
    TaskPreprocessing_FireIR, "Preprocess FireIR Data", 2048,  // Stack size
    (void *)lock,                                          // pass lock as a parameter
    1,                                                     // Priority
    NULL);

  xTaskCreate(
    TaskPreprocessing_Temp1, "Preprocess Temp_coil Data", 2048,  // Stack size
    (void *)lock,                                                     // pass lock as a parameter
    1,                                                                // Priority
    NULL);

  xTaskCreate(
    TaskNetPie, "UpLoad data to Netpie", 16384,  // Stack size 
    (void *)lock,                               // pass lock as a parameter
    0,                                          // Priority
    NULL);
  // Now the task scheduler, which takes over control of scheduling individual tasks, is automatically started.
  // Check if the serial port is ready
  while (!Serial) delay(10);
}



void loop() {
  // Empty. Things are done in Tasks.
}

/*--------------------------------------------------*/
/*---------------------- Tasks ---------------------*/
/*--------------------------------------------------*/

void TaskWiFiBlink(void *pvParameters)  // This is a task.
{

  /*
  Blink
  Turns on an LED on for one second, then off for one second, repeatedly.
    
  If you want to know what pin the on-board LED is connected to on your ESP32 model, check
  the Technical Specs of your board.
*/

  // initialize digital LED_BUILTIN on pin 13 as an output.

  const int YELLOW_LED = 2;
  pinMode(YELLOW_LED, OUTPUT);
  // turn it off (HIGH) first
  digitalWrite(YELLOW_LED, HIGH);
  for (;;)  // A Task shall never return or exit.
  {
    if (WiFi.status() == WL_CONNECTED) {
      // on
      digitalWrite(YELLOW_LED, LOW);  // turn the LED on (HIGH is the voltage level)
      vTaskDelay(500);
      // then off                 // one tick delay (15ms) in between reads for stability
      digitalWrite(YELLOW_LED, HIGH);  // turn the LED off by making the voltage LOW
      vTaskDelay(500);                 // one tick delay (15ms) in between reads for stability
    } else {
      connectWiFi();
      vTaskDelay(1000);
    }
  }
}

void TaskShowRGB(void *pvParameters)  // This is a task.
{

  const int NUMPIXELS = 1;
  const int RGB_PIN = 18;
  /*
  AnalogReadSerial
  Reads an analog input on pin A3, prints the result to the serial monitor.
  Graphical representation is available using serial plotter (Tools > Serial Plotter menu)
  Attach the center pin of a potentiometer to pin A3, and the outside pins to +5V and ground.
  This example code is in the public domain.
*/
  Adafruit_NeoPixel pixels(NUMPIXELS, RGB_PIN, NEO_GRB + NEO_KHZ800);
  pixels.begin();  // INITIALIZE NeoPixel strip object (REQUIRED)
  pixels.clear();  // Set all pixel colors to 'off'
  for (;;) {
    // The first NeoPixel in a strand is #0, second is 1, all the way up
    // to the count of pixels minus one.
    if (rg) {  // For each pixel...
      pixels.setPixelColor(0, pixels.Color(150,0, 0));
      pixels.show();    // Send the updated pixel colors to the hardware.
    }else{
      pixels.setPixelColor(0, pixels.Color(0,150, 0));
      pixels.show();
    }
    rg=!rg;
    vTaskDelay(2000);  // one tick delay (15ms) in between reads for stability  
  }
}

void TaskReadBMP280(void *pvParameters) {
  // ------------- setup() part of this task --------------------
  // get the pointer of the lock from the parameter received
  Lock *lock = (Lock *)pvParameters;
  // Create a pressor sensor using IIC
  Adafruit_BMP280 pressureSensor(&Wire);  // I2C
  // try to lock the shared IIC
  lock->lockIIC();
  // after locked the IIC successfully, start & read the sensor's status from the alternative adr
  bool status = pressureSensor.begin(BMP280_ADDRESS_ALT, BMP280_CHIPID);
  // then give the shared resource back ASAP
  lock->unlockIIC();
  if (!status) {
    // serial port is also another shared resource, take & give before & after use.
    lock->lockSerial();
    Serial.println(F("Could not find a valid BMP280 sensor, check wiring or "
                     "try a different address!"));
    lock->unlockSerial();
    while (1) vTaskDelay(10000000L);  // if BMP280 is not found then this task is doing nothing
  } else {
    lock->lockSerial();
    Serial.printf("\nGreat! found a BMP280 @Address=0x%X, ID:0x%X.\n", BMP280_ADDRESS_ALT, BMP280_CHIPID);
    lock->unlockSerial();
  }
  lock->lockIIC();
  /* Default settings from datasheet. */
  pressureSensor.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode. */
                             Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                             Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                             Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                             Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */
  lock->unlockIIC();
  // create a queue between this task and preprocessor and pass the handle to a global variable
  // queue with 8 spaces for the temperature with timestamp (8 * 16 bytes)
  BMP280_Temp_PreQ = xQueueCreate(8, sizeof(struct float_timestamp));
  // sleep for a while to let other tasks start themselves up
  vTaskDelay(10);

  //---------- loop() part of this task
  for (float temp; 1;) {
    time_t timeStamp;
    float_timestamp Data_Temp;// , Data_AirPressure;
    // lock IIC Bus
    lock->lockIIC();
    // lock Serial Port
    lock->lockSerial();
    temp = pressureSensor.readTemperature();
    Serial.printf("Temperature from BMP280: %3.2f degree C.\n", temp);
    // then give the shared resource back ASAP
    lock->unlockIIC();
    lock->unlockSerial();
    timeStamp = DateTime.now();
    Data_Temp = float_timestamp(timeStamp, temp);
    // send the new Data to the Q, if the Q is full, wait for 100 ticks.
    while (xQueueSend(BMP280_Temp_PreQ, &Data_Temp, (TickType_t)100) != pdTRUE)
      ;  // wait until the preprocessor do something about the data

    // restart the task every second
    vTaskDelay(5000L);
  }
  // end of the task
}


void TaskReadDHT11(void *pvParameters) {
  // ------------- setup() part of this task --------------------
  // get the pointer of the lock from the parameter received
  Lock *lock = (Lock *)pvParameters;
  DHT dht(4, DHT11);
  // declare the sensor
  lock->lockIIC();
   dht.begin();
  lock->unlockIIC();
  // try to lock the shared IIC
  // queue with 8 spaces for the temperature with timestamp (8 * 16 bytes)
  DHT11_Temp_PreQ = xQueueCreate(8, sizeof(struct float_timestamp));
  // sleep for a while to let other tasks start themselves up
  vTaskDelay(10);

  for (;;) {
    time_t timeStamp;
    float_timestamp Data_temp;
    // try to lock the shared IIC
    lock->lockIIC();
    lock->lockSerial();
    float t = dht.readTemperature();
    Serial.printf("Temperature from DHT!!: %3.2f degrees C.\n",t );
    // check if it's time to sample humidity
    // then give the shared resource back ASAP
    lock->unlockIIC();
    lock->unlockSerial();
    // send Humidity data to queue if it's time to sample
    timeStamp = DateTime.now();
    // Send Temp data to queue if it's time to sample
    Data_temp = float_timestamp(timeStamp, t);
    // send the new data to the queue, if the queue is full, wait for 100 ticks.
    while (xQueueSend(DHT11_Temp_PreQ, &Data_temp, (TickType_t)100) != pdTRUE)
        ;  // wait until the preprocessor does something about the data
      //xSemaphoreTake
    vTaskDelay(5000L);
  }
}

void TaskReadFireIR(void *pvParameters) {
  // ------------- setup() part of this task --------------------
  // get the pointer of the lock from the parameter received
  Lock *lock = (Lock *)pvParameters;
  // declare the sensor
  float FireIR;
  
  FireIR_PreQ = xQueueCreate(8, sizeof(struct float_timestamp));
  // sleep for a while to let other tasks start themselves up
  vTaskDelay(10);

  // for (sensors_event_t a, g, temp; 1;) {
  for (;;) {
    time_t timeStamp;
    float_timestamp Data_FireIR;
    // try to lock the shared IIC

    lock->lockIIC();
    lock->unlockIIC();
  
    FireIR= analogRead(3);
    
    lock->unlockSerial();
    lock->unlockIIC();

    
    timeStamp = DateTime.now();
    Data_FireIR = float_timestamp(timeStamp, FireIR);
    while (xQueueSend(FireIR_PreQ, &Data_FireIR, (TickType_t)100) != pdTRUE)
      ;  // wait until the preprocessor do something about the data
    //xSemaphoreTake
    vTaskDelay(5000L);
  }
}



class circularBuffer {
private:
  unsigned bufferLength, next;
  float_timestamp *buffer;
public:
  circularBuffer(unsigned len = 1)
    : bufferLength(len) {
    buffer = new float_timestamp[bufferLength];
    next = 0;
  }
  void newData(float_timestamp *Data) {
    // put info into the new array
    buffer[next].secs = Data->secs;
    buffer[next].data = Data->data;
    // update pointer
    next += 1;
    next %= bufferLength;
  }
  float average() {
    float sum = 0;
    int cnt = 0;
    for (int i = 0; i < bufferLength; i++) {
      if (buffer[i].data != 0) {
        sum += buffer[i].data;
        cnt++;
      }
    }
    return sum / cnt;
  }
};

void TaskPreprocessing_Temp(void *pvParameters) {
  // ------------- setup() part of this task --------------------
  // get the pointer of the lock from the parameter received
  Lock *lock = (Lock *)pvParameters;
  // minimum preprocessing, averaging 5 read data
  circularBuffer BMP280TempBuffer(5);
  // circularBuffer HTS221TempBuffer(5);
  // circularBuffer MPU6050TempBuffer(5);

  float_timestamp *buf1 = new float_timestamp;
  // float_timestamp *buf2 = new float_timestamp;
  // float_timestamp *buf3 = new float_timestamp;

  bool read_Temp_BMP280;//, read_Temp_MPU6050;//, read_Temp_HTS221;
  TempInfo_NetpieQ = xQueueCreate(8, sizeof(struct float_timestamp));
  // sleep for 2 secs.
  vTaskDelay(2000);
  // forever loop
  for (;;) {
    time_t timeStamp;
    float Temp_avg;
    float_timestamp TempInfo;
    // read the shared Q, put the data to buf. if Q is empty, wait for 10 ticks
    read_Temp_BMP280 = xQueueReceive(BMP280_Temp_PreQ, buf1, (TickType_t)10);
    // read_Temp_HTS221 = xQueueReceive(HTS221_Temp_PreQ, buf2, (TickType_t)10);
    // read_Temp_MPU6050 = xQueueReceive(MPU6050_Temp_PreQ, buf3, (TickType_t)10);

    if (read_Temp_BMP280) {
      // if successful, put the read data to the circular buffer
      BMP280TempBuffer.newData(buf1);
      Temp_avg = BMP280TempBuffer.average();
      lock->lockSerial();
      Serial.printf("The average of the last five temp from BMP280 is %3.2f.\n\n", BMP280TempBuffer.average());
      // Serial.printf("The average of the last five temp from HTS221 is %3.2f.\n\n", HTS221TempBuffer.average());
      // Serial.printf("The average of the last five temp from MPU6050 is %3.2f.\n\n", MPU6050TempBuffer.average());
      lock->unlockSerial();

      timeStamp = DateTime.now();
      // form a structure with timestamp and the temperature read recently
      TempInfo = float_timestamp(timeStamp, Temp_avg);
      // send the new Data to the Q, if the Q is full, wait for 100 ticks.
      while (xQueueSend(TempInfo_NetpieQ, &TempInfo, (TickType_t)100) != pdTRUE)
        ;  // wait until the preprocessor do something about the data
    }
    vTaskDelay(1000);
  }
}

void TaskPreprocessing_FireIR(void *pvParameters) {
  // ------------- setup() part of this task --------------------
  // get the pointer of the lock from the parameter received
  Lock *lock = (Lock *)pvParameters;
  // minimum preprocessing, averaging 5 read data
  circularBuffer FireIRBuffer(5);
  float_timestamp *buf = new float_timestamp;
  //Create a queue to tasknetpie
  FireIRInfo_NetpieQ = xQueueCreate(8, sizeof(struct float_timestamp));
  // sleep for 2 secs.
  vTaskDelay(2000);
  // forever loop
  for (;;) {
    time_t timeStamp;
    float FireIR_avg;
    float_timestamp FireIRInfo;
    // read the shared Q, put the data to buf. if Q is empty, wait for 10 ticks
    if (xQueueReceive(FireIR_PreQ, buf, (TickType_t)10)) {
      // if successful, put the read data to the circular buffer
      FireIRBuffer.newData(buf);
      FireIR_avg = FireIRBuffer.average();
      lock->lockSerial();
      Serial.printf("The average of the last five FireIR is %3.2f.\n\n", FireIRBuffer.average());
      // Serial.printf("The average of the last five temp from HTS221 is %3.2f.\n\n", HTS221TempBuffer.average());
      // Serial.printf("The average of the last five temp from MPU6050 is %3.2f.\n\n", MPU6050TempBuffer.average());
      lock->unlockSerial();
      timeStamp = DateTime.now();
      // form a structure with timestamp and the temperature read recently
      FireIRInfo = float_timestamp(timeStamp, FireIR_avg);
      // send the new Data to the Q, if the Q is full, wait for 100 ticks.
      while (xQueueSend(FireIRInfo_NetpieQ, &FireIRInfo, (TickType_t)100) != pdTRUE)
        ;  // wait until the preprocessor do something about the data
    }
    vTaskDelay(1000L);
  }
}
void TaskPreprocessing_Temp1(void *pvParameters) {
  // ------------- setup() part of this task --------------------
  // get the pointer of the lock from the parameter received
  Lock *lock = (Lock *)pvParameters;
  // minimum preprocessing, averaging 5 read data
  circularBuffer Temp1Buffer(5);
  float_timestamp *buf = new float_timestamp;
  //Create a queue to tasknetpie
  Temp1Info_NetpieQ = xQueueCreate(8, sizeof(struct float_timestamp));
  // sleep for 2 secs.
  vTaskDelay(2000);
  // forever loop
  for (;;) {
    time_t timeStamp;
    float Temp1_avg;
    float_timestamp Temp1Info;
    // read the shared Q, put the data to buf. if Q is empty, wait for 10 ticks
    if (xQueueReceive(DHT11_Temp_PreQ, buf, (TickType_t)10)) {
      // if successful, put the read data to the circular buffer
      Temp1Buffer.newData(buf);
      Temp1_avg = Temp1Buffer.average();
      lock->lockSerial();
      Serial.printf("The average of the last five temp from DHT11 is %3.2f.\n\n", Temp1Buffer.average());
      // Serial.printf("The average of the last five temp from HTS221 is %3.2f.\n\n", HTS221TempBuffer.average());
      // Serial.printf("The average of the last five temp from MPU6050 is %3.2f.\n\n", MPU6050TempBuffer.average());
      lock->unlockSerial();

      timeStamp = DateTime.now();
      // form a structure with timestamp and the temperature read recently
      Temp1Info = float_timestamp(timeStamp, Temp1_avg);
      // send the new Data to the Q, if the Q is full, wait for 100 ticks.
      while (xQueueSend(Temp1Info_NetpieQ, &Temp1Info, (TickType_t)100) != pdTRUE)
        ;  // wait until the preprocessor do something about the data
    }
    vTaskDelay(1000L);
  }
}


void TaskNetPie(void *pvParameters) {
  // ------------- setup() part of this task --------------------
  // get the pointer of the lock from the parameter received
  Lock *lock = (Lock *)pvParameters;
  //buffer
  float_timestamp *buf_Temp1 = new float_timestamp;
  // float_timestamp *buf_rain = new float_timestamp;
  float_timestamp *buf_fire = new float_timestamp;
  float_timestamp *buf_Temp = new float_timestamp;
  // float_timestamp *buf_vibr = new float_timestamp;

  float temp,fire,temp1;//,vibr,rain;
  vTaskDelay(2000);

  client.setServer(mqtt_server, mqtt_port);
  client.connect(mqtt_client, mqtt_username, mqtt_password);

  // ------------- loop() part of this task --------------------
  for (;;) {
    if (client.connected()) {
      client.loop();
      String payload;
      // read the shared Temperature Q, put the TempInfo to buf_humi. if Q is empty, wait for 10 ticks
      if (xQueueReceive(TempInfo_NetpieQ, buf_Temp, (TickType_t)10)&&xQueueReceive(FireIRInfo_NetpieQ, buf_fire, (TickType_t)10)&&xQueueReceive(Temp1Info_NetpieQ, buf_Temp1, (TickType_t)10)) {
        temp = buf_Temp->data;
        temp1 = buf_Temp1->data;
        fire= buf_fire->data;
        payload = "{\"data\": {";
        payload.concat("\"environment\":{\"temperature_ref\":" + String(temp)); //publish environment to shadow
        payload.concat(",\"temperature_coil\":" + String(temp1));
        payload.concat(",\"IR_output\":" + String(fire));
        payload.concat("}}}");
        client.publish("@shadow/data/update", payload.c_str());

        payload = "{\"data\": {";
        payload.concat("\"waterdetect\":" + String(int(analogRead(1)<=7500))); //analogRead(1) to detect rain sensor
        payload.concat("}}");
        client.publish("@shadow/data/update", payload.c_str());
        
        payload = "{\"data\": {";
        payload.concat("\"update\":" + String(1));
        payload.concat("}}");
        client.publish("@shadow/data/update", payload.c_str());
        
        lock->lockSerial();
        Serial.printf("shadow environment and waterdetect updated\n\n");
        lock->unlockSerial();
        
      }
    } else {
      lock->lockSerial();
      Serial.printf("failed to connect client \n\n");
      lock->unlockSerial();
      if (WiFi.status() == WL_CONNECTED) {

        client.disconnect();
        client.connect(mqtt_client, mqtt_username, mqtt_password);
      }
    }
    vTaskDelay(1000);
  }
}

bool connectWiFi(bool verbose) {
  // Set WiFi to station mode and disconnect from an AP if it was previously connected.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();

  for (int i = 0; i < n; i++) {
    if (verbose)
      Serial.printf("Found AP: %s with %d RSSI\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i));
    if ((SchoolAP.SSID_Name == WiFi.SSID(i)) && (WiFi.RSSI(i) > -80)) {
      if (verbose)
        Serial.printf("Connecting to School Network.");
      WiFi.begin(WiFi.SSID(i).c_str(), WPA2_AUTH_PEAP, SchoolAP.userName.c_str(), SchoolAP.userName.c_str(), SchoolAP.passPhase.c_str());
      for (int j = 0; j < 20; j++) {
        if (WiFi.status() == WL_CONNECTED) {
          if (verbose) Serial.println(" !connected.");
          return true;
        }
        if (verbose)
          Serial.print(".");
        delay(500);
      }
    }

    if ((HomeAP.SSID_Name == WiFi.SSID(i)) && (WiFi.RSSI(i) > -80)) {
      if (verbose)
        Serial.print("Connecting to Home Network.");
      WiFi.begin(WiFi.SSID(i).c_str(), HomeAP.passPhase.c_str());
      for (int j = 0; j < 20; j++) {
        if (WiFi.status() == WL_CONNECTED) {
          if (verbose) Serial.println(" !connected.");
          return true;
        }
        if (verbose)
          Serial.print(".");
        delay(500);
      }
    }
  }

  if (verbose)
    Serial.printf("Trying to connect to %s.", MobileAP.SSID_Name.c_str());
  WiFi.begin(MobileAP.SSID_Name.c_str(), MobileAP.passPhase.c_str());
  for (int j = 0; j < 20; j++) {
    if (WiFi.status() == WL_CONNECTED) {
      if (verbose) Serial.println(" !connected.");
      return true;
    }
    if (verbose)
      Serial.print(".");
    delay(500);
  }
  return false;
}

bool updateNTPTime(bool verbose) {
  // sync the system time with the NTP server
  DateTime.begin();
  if (!DateTime.isTimeValid()) {
    if (verbose)
      Serial.println("Failed to get time from server.");
    return false;
  } else {
    if (verbose)
      Serial.printf("Date Now is %s\n", DateTime.toISOString().c_str());
    return true;
  }
}
