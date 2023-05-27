/* --- Dependencies --- */
// C99 libraries
#include <cstdlib>
#include <cstdarg>
#include <string.h>
#include <time.h>

// For hmac SHA256 encryption
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

// Libraries for MQTT client
#include <mqtt_client.h>

// Azure IoT SDK for C includes
#include <az_core.h>
#include <az_iot.h>
#include <azure_ca.h>

// Additional sample headers
#include "AzureIoT.h"
#include "Azure_IoT_PnP_Template.h"
#include "iot_configs.h"
#include <ArduinoJson.h>

// HX711 Load Cell Amplifier
#include "HX711.h"

/* --- CALIBRATION & CONFIG --- */

/* GSM SIM Card Credentials
 * 
 * Your GPRS credentials (leave empty, if not needed)
 * 
 * APN is required, but username, password and SIM pin
 * may not be needed.
 */
const char apn[] = "gpinternet"; // YOUR CELLULAR APN HERE
const char gprsUser[] = ""; // GPRS User
const char gprsPass[] = ""; // GPRS Password

const char simPIN[] = ""; // SIM card PIN (leave empty, if not defined)

/* WiFi or GPRS Mode
 * 
 * Configure the system to connect via WiFi or GPRS here.
 */
#define TINY_GSM_USE_WIFI false // (true = WiFi, false = GPRS)

/* Load Cell Factors
 * 
 * Please run the script LoadCellCalibration.ino
 * to determine the load cell calibration factors
 * for all 4 load cells, and specify here.
 */
// calibration factors for HX711 load cell amplifiers
float scale1CalibrationFactor = 1000; 
float scale2CalibrationFactor = 1000; 
float scale3CalibrationFactor = 1000; 
float scale4CalibrationFactor = 1000; 

/* --- END OF CALIBRATION --- */

// HX711 circuit wiring
const int LOADCELL_DOUT_PIN1 = 34;
const int LOADCELL_DOUT_PIN2 = 35;
const int LOADCELL_DOUT_PIN3 = 32;
const int LOADCELL_DOUT_PIN4 = 33;

const int LOADCELL_SCK_PIN = 18;

// HX711 library objects
HX711 scale1;
HX711 scale2;
HX711 scale3;
HX711 scale4;

bool takeStock = false;

// Timers
hw_timer_t *timer = NULL;
bool timerEn=false;

/* --- TINY GSM Library --- */

// TTGO T-Call pins
#define MODEM_RST       5
#define MODEM_PWKEY     4
#define MODEM_POWER_ON  23
#define MODEM_TX        27
#define MODEM_RX        26
#define I2C_SDA         21
#define I2C_SCL         22

// Set serialmon for debug console (to SerialMon Monitor, default speed 115200)
#define SerialMon Serial
// Set serial for AT commands (to SIM800 module)
#define SerialAT Serial1

#define TINY_GSM_MODEM_SIM800   // Modem is SIM800
#define TINY_GSM_RX_BUFFER 1024 // Set RX buffer to 1Kb

// enable debug prints of AT commands to console, if needed
#define DUMP_AT_COMMANDS

#include <WiFiClientSecure.h>
#include <Wire.h>
#include <TinyGsmClient.h>

#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, SerialMon);
TinyGsm modem(debugger);
#else
TinyGsm modem(SerialAT);
#endif

#include <WiFi.h>

#define USE_GSM //! Uncomment will use SIM800L for GSM communication

#ifdef USE_GSM
TinyGsmClient client(modem);
#endif

/* --- Product JSON Files ---*/
DynamicJsonDocument Products(512);
DynamicJsonDocument Product1(512);
DynamicJsonDocument Product2(512);
DynamicJsonDocument Product3(512);
DynamicJsonDocument Product4(512);

/* --- I2C for SIM800 (to keep it running when powered from battery) --- */
TwoWire I2CPower = TwoWire(0);
TwoWire I2CComs = TwoWire(1);

/* --- Conversion factor for microseconds to seconds --- */
#define uS_TO_S_FACTOR 1000000UL 

/* --- I2C Addresses --- */
#define I2C_DEV_ADDR        0x55 // I2C address of Motor/IO ESP32 Module
#define IP5306_ADDR         0x75 // I2C address of IP5306 battery power management SoC
#define IP5306_REG_SYS_CTL0 0x00 // Address of SYS_CTL0 register in the IP5306 SoC

/* --- Settings from iot_configs.h --- */
static const char* wifi_ssid = IOT_CONFIG_WIFI_SSID; 
static const char* wifi_password = IOT_CONFIG_WIFI_PASSWORD;

int telemetery_f_seconds = TELEMETRY_FREQUENCY_IN_SECONDS; // stock take intervals in seconds (3600 seconds = 1 stock take per hour)

/* --- Sample-specific Settings --- */
#define SERIAL_LOGGER_BAUD_RATE 115200
#define MQTT_DO_NOT_RETAIN_MSG 0

/* --- Time and NTP Settings --- */
#define NTP_SERVERS "pool.ntp.org", "time.nist.gov"

#define PST_TIME_ZONE -8
#define PST_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF 1

#define GMT_OFFSET_SECS (PST_TIME_ZONE * 3600)
#define GMT_OFFSET_SECS_DST ((PST_TIME_ZONE + PST_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF) * 3600)

#define UNIX_TIME_NOV_13_2017 1510592825
#define UNIX_EPOCH_START_YEAR 1900

/* --- Function Returns --- */
#define RESULT_OK 0
#define RESULT_ERROR __LINE__

/* --- Function Declarations --- */
static void sync_device_clock_with_ntp_server();
static void restart();
static esp_err_t esp_mqtt_event_handler(esp_mqtt_event_handle_t event);

// This is a logging function used by Azure IoT client.
static void logging_function(log_level_t log_level, char const *const format, ...);

/* --- Sample variables --- */
static azure_iot_config_t azure_iot_config;
static azure_iot_t azure_iot;
static esp_mqtt_client_handle_t mqtt_client;
static char mqtt_broker_uri[128];

#define AZ_IOT_DATA_BUFFER_SIZE 1500
static uint8_t az_iot_data_buffer[AZ_IOT_DATA_BUFFER_SIZE];

#define MQTT_PROTOCOL_PREFIX "mqtts://"

static uint32_t properties_request_id = 0;
static bool send_device_info = true;
static bool get_device_props = true;
static bool readyForI2C = false;

String readString;
char Product1Value[50];
char Product2Value[50];
char Product3Value[50];
char Product4Value[50];

String Product1Name;
int Product1Price;
int Product1Time;

uint32_t i = 0;
bool trash_request = false;

/* --- MQTT Interface Functions --- */
/*
 * These functions are used by Azure IoT to interact with whatever MQTT client used by the sample
 * (in this case, Espressif's ESP MQTT). Please see the documentation in AzureIoT.h for more details.
 */

/*
 * See the documentation of `mqtt_client_init_function_t` in AzureIoT.h for details.
 */
static int mqtt_client_init_function(mqtt_client_config_t *mqtt_client_config, mqtt_client_handle_t *mqtt_client_handle)
{
  int result;
  esp_mqtt_client_config_t mqtt_config;
  memset(&mqtt_config, 0, sizeof(mqtt_config));

  az_span mqtt_broker_uri_span = AZ_SPAN_FROM_BUFFER(mqtt_broker_uri);
  mqtt_broker_uri_span = az_span_copy(mqtt_broker_uri_span, AZ_SPAN_FROM_STR(MQTT_PROTOCOL_PREFIX));
  mqtt_broker_uri_span = az_span_copy(mqtt_broker_uri_span, mqtt_client_config->address);
  az_span_copy_u8(mqtt_broker_uri_span, null_terminator);

  mqtt_config.uri = mqtt_broker_uri;
  mqtt_config.port = mqtt_client_config->port;
  mqtt_config.client_id = (const char *)az_span_ptr(mqtt_client_config->client_id);
  mqtt_config.username = (const char *)az_span_ptr(mqtt_client_config->username);

#ifdef IOT_CONFIG_USE_X509_CERT
  LogInfo("MQTT client using X509 Certificate authentication");
  mqtt_config.client_cert_pem = ;
  mqtt_config.client_key_pem = IOT_CONFIG_DEVICE_CERT_PRIVATE_KEY;
#else // Using SAS key
  mqtt_config.password = (const char *)az_span_ptr(mqtt_client_config->password);
#endif

  mqtt_config.keepalive = 30;
  mqtt_config.disable_clean_session = 0;
  mqtt_config.disable_auto_reconnect = false;
  mqtt_config.event_handle = esp_mqtt_event_handler;
  mqtt_config.user_context = NULL;
  mqtt_config.cert_pem = (const char *)ca_pem;

  LogInfo("MQTT client target uri set to '%s'", mqtt_broker_uri);

  mqtt_client = esp_mqtt_client_init(&mqtt_config);

  if (mqtt_client == NULL)
  {
    LogError("esp_mqtt_client_init failed.");
    result = 1;
  }
  else
  {
    esp_err_t start_result = esp_mqtt_client_start(mqtt_client);

    if (start_result != ESP_OK)
    {
      LogError("esp_mqtt_client_start failed (error code: 0x%08x).", start_result);
      result = 1;
    }
    else
    {
      *mqtt_client_handle = mqtt_client;
      result = 0;
    }
  }

  return result;
}

/*
 * See the documentation of `mqtt_client_deinit_function_t` in AzureIoT.h for details.
 */
static int mqtt_client_deinit_function(mqtt_client_handle_t mqtt_client_handle)
{
  int result = 0;
  esp_mqtt_client_handle_t esp_mqtt_client_handle = (esp_mqtt_client_handle_t)mqtt_client_handle;

  LogInfo("MQTT client being disconnected.");

  if (esp_mqtt_client_stop(esp_mqtt_client_handle) != ESP_OK)
  {
    LogError("Failed stopping MQTT client.");
  }

  if (esp_mqtt_client_destroy(esp_mqtt_client_handle) != ESP_OK)
  {
    LogError("Failed destroying MQTT client.");
  }

  if (azure_iot_mqtt_client_disconnected(&azure_iot) != 0)
  {
    LogError("Failed updating azure iot client of MQTT disconnection.");
  }

  return 0;
}

/*
 * See the documentation of `mqtt_client_subscribe_function_t` in AzureIoT.h for details.
 */
static int mqtt_client_subscribe_function(mqtt_client_handle_t mqtt_client_handle, az_span topic, mqtt_qos_t qos)
{
  LogInfo("MQTT client subscribing to '%.*s'", az_span_size(topic), az_span_ptr(topic));

  // As per documentation, `topic` always ends with a null-terminator.
  // esp_mqtt_client_subscribe returns the packet id or negative on error already, so no conversion is needed.
  int packet_id = esp_mqtt_client_subscribe((esp_mqtt_client_handle_t)mqtt_client_handle, (const char *)az_span_ptr(topic), (int)qos);

  return packet_id;
}

/*
 * See the documentation of `mqtt_client_publish_function_t` in AzureIoT.h for details.
 */
static int mqtt_client_publish_function(mqtt_client_handle_t mqtt_client_handle, mqtt_message_t *mqtt_message)
{
  LogInfo("MQTT topic publishing to '%s'", az_span_ptr(mqtt_message->topic));
  LogInfo("MQTT payload subscribing to '%.*s'", az_span_size(mqtt_message->payload), az_span_ptr(mqtt_message->payload));
  int mqtt_result = esp_mqtt_client_publish(
      (esp_mqtt_client_handle_t)mqtt_client_handle,
      (const char *)az_span_ptr(mqtt_message->topic), // topic is always null-terminated.
      (const char *)az_span_ptr(mqtt_message->payload),
      az_span_size(mqtt_message->payload),
      (int)mqtt_message->qos,
      MQTT_DO_NOT_RETAIN_MSG);

  if (mqtt_result == -1)
  {
    return RESULT_ERROR;
  }
  else
  {
    return RESULT_OK;
  }
}

/* --- Other Interface functions required by Azure IoT --- */

/*
 * See the documentation of `hmac_sha256_encryption_function_t` in AzureIoT.h for details.
 */
static int mbedtls_hmac_sha256(const uint8_t *key, size_t key_length, const uint8_t *payload, size_t payload_length, uint8_t *signed_payload, size_t signed_payload_size)
{
  (void)signed_payload_size;
  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
  mbedtls_md_hmac_starts(&ctx, (const unsigned char *)key, key_length);
  mbedtls_md_hmac_update(&ctx, (const unsigned char *)payload, payload_length);
  mbedtls_md_hmac_finish(&ctx, (byte *)signed_payload);
  mbedtls_md_free(&ctx);

  return 0;
}

/*
 * See the documentation of `base64_decode_function_t` in AzureIoT.h for details.
 */
static int base64_decode(uint8_t *data, size_t data_length, uint8_t *decoded, size_t decoded_size, size_t *decoded_length)
{
  return mbedtls_base64_decode(decoded, decoded_size, decoded_length, data, data_length);
}

/*
 * See the documentation of `base64_encode_function_t` in AzureIoT.h for details.
 */
static int base64_encode(uint8_t *data, size_t data_length, uint8_t *encoded, size_t encoded_size, size_t *encoded_length)
{
  return mbedtls_base64_encode(encoded, encoded_size, encoded_length, data, data_length);
}

static void exract_val(String product)
{
  int str_len = product.length() + 1; // convert string to chat
  char char_arr[str_len];
  product.toCharArray(char_arr, str_len);
  char *tokenName = strtok(char_arr, ":");
  char *productVal = strtok(NULL, ":");
  Products[tokenName] = productVal;
}
/*
 * See the documentation of `properties_update_completed_t` in AzureIoT.h for details.
 */
static void on_properties_update_completed(uint32_t request_id, az_iot_status status_code, az_span properties)
{
  // LogInfo("%.*s", az_span_size(properties), az_span_ptr(properties));
  // 999 id for get desired props
  if (request_id == 999)
  {
    char props;
    SerialMon.println("Desired properties");

    // This is defenitly not the best way of doing it but its done
    char s[1024];
    snprintf(s, sizeof(s), "%.*s", az_span_size(properties), az_span_ptr(properties));
    SerialMon.println(s);
    char *desiredProps = strtok(s, "$");
    char *P1Name = "Product1Name";
    char *Product1Val;

    Product1Val = strstr(desiredProps, P1Name);
    String test = Product1Val;
    test.replace('"', ' ');
    test.replace(" ", "");

    int str_len = test.length() + 1; // convert string to chat
    char char_arr[str_len];
    test.toCharArray(char_arr, str_len);
    SerialMon.println(char_arr);
    String values[15];
    int indexVal = 0;
    char *token = strtok(char_arr, ",");

    while (token != NULL)
    {
      values[indexVal] = token;
      indexVal++;
      token = strtok(NULL, ",");
    }

    for (int x = 0; x < indexVal; x++)
    {
      exract_val(values[x]);
    }

    // serializeJson(Products, SerialMon);

    Product1["name"] = Products["Product1Name"];
    Product2["name"] = Products["Product2Name"];
    Product3["name"] = Products["Product3Name"];
    Product4["name"] = Products["Product4Name"];

    Product1["price"] = Products["Product1Price"];
    Product2["price"] = Products["Product2Price"];
    Product3["price"] = Products["Product3Price"];
    Product4["price"] = Products["Product4Price"];

    Product1["time"] = Products["Product1Time"];
    Product2["time"] = Products["Product2Time"];
    Product3["time"] = Products["Product3Time"];
    Product4["time"] = Products["Product4Time"];
    
    serializeJson(Product1, SerialMon);
    serializeJson(Product2, SerialMon);
    serializeJson(Product3, SerialMon);
    serializeJson(Product4, SerialMon);
  }
  LogInfo("Properties update request completed (id=%d, status=%d)", request_id, status_code);
}

/*
 * See the documentation of `properties_received_t` in AzureIoT.h for details.
 */
void on_properties_received(az_span properties)
{
  LogInfo("Properties update received: %.*s", az_span_size(properties), az_span_ptr(properties));
  SerialMon.println("Property Recieved");
  if (azure_pnp_handle_properties_update(&azure_iot, properties, properties_request_id++) != 0)
  {
    LogError("Failed handling properties update.");
  }
}

/*
 * See the documentation of `command_request_received_t` in AzureIoT.h for details.
 */
static void on_command_request_received(command_request_t command)
{
  az_span component_name = az_span_size(command.component_name) == 0 ? AZ_SPAN_FROM_STR("") : command.component_name;

  LogInfo("Command request received (id=%.*s, component=%.*s, name=%.*s)",
          az_span_size(command.request_id), az_span_ptr(command.request_id),
          az_span_size(component_name), az_span_ptr(component_name),
          az_span_size(command.command_name), az_span_ptr(command.command_name));


    if(az_span_is_content_equal(command.command_name, AZ_SPAN_FROM_STR("TakeStock"))){
      takeStock=true;
    }


  // Here the request is being processed within the callback that delivers the command request.
  // However, for production application the recommendation is to save `command` and process it outside
  // this callback, usually inside the main thread/task/loop.
  (void)azure_pnp_handle_command_request(&azure_iot, command);
}

static az_span const twin_document_topic_request_id = AZ_SPAN_LITERAL_FROM_STR("get_twin");
static az_iot_hub_client hub_client;
static void get_device_twin_document(void)
{

  int rc;

  SerialMon.println("Client requesting device twin document from service.");

  // Get the Twin Document topic to publish the twin document request.
  char twin_document_topic_buffer[128];
  rc = az_iot_hub_client_twin_document_get_publish_topic(
      &azure_iot.iot_hub_client,
      twin_document_topic_request_id,
      twin_document_topic_buffer,
      sizeof(twin_document_topic_buffer),
      NULL);
  if (az_result_failed(rc))
  {
    LogError("Failed to get the Twin Document topic: az_result return code 0x%08x.", rc);
    exit(rc);
  }

  // Publish the twin document request.
  rc = esp_mqtt_client_publish(mqtt_client, twin_document_topic_buffer, 0, NULL, 1, 0);
  // int propertyRes = esp_mqtt_client_publish((esp_mqtt_client_handle_t)mqtt_client,twin_document_topic_buffer, 0, NULL, 1, 0);

  SerialMon.println("Client requesting device twin document from service.");
}

static long get_product1_mass()
{
   if (scale1.wait_ready_timeout(5000))
   {  
     long reading= scale1.get_units(10);
     SerialMon.print("Result 1: ");
     SerialMon.println(reading);
     return reading;
   }
   else return 0;
}


static long get_product2_mass()
{ 
   if (scale2.wait_ready_timeout(5000))
   {  
     long reading= scale2.get_units(10);
     SerialMon.print("Result 2: ");
     SerialMon.println(reading);
     return reading;
   }
   else return 0;
}

static long get_product3_mass()
{
   if (scale3.wait_ready_timeout(5000))
   {  
     long reading= scale3.get_units(10);
     SerialMon.print("Result 3: ");
     SerialMon.println(reading);
     return reading;
   }
   else return 0;
}

static long get_product4_mass()
{
   if (scale4.wait_ready_timeout(5000))
   {  
     long reading= scale4.get_units(10);
     SerialMon.print("Result 4: ");
     SerialMon.println(reading);
     return reading;
   }
   else return 0;
}

void onRequest() // Anwsers to Master's "requestFrom"
{
  // SerialMon.println("Request received. Sending buffered data."); //The sending happens on the background
}

void onReceive(int len) // Anwsers to Master's "transmissions"
{
  String requestResponse = ""; // to generate the request reply content
  String masterMessage = "";   // to save Master's message
  
  char msg[len];
  int msgIndex = 0;
  while (Wire.available()) // If there are bytes through I2C
    masterMessage.concat((char)Wire.read()); // make a string out of the bytes

  masterMessage.toCharArray(msg, len);  

  if (masterMessage == "Ready") // Filter Master messages
  {
    String product1String = "No";
    
    if (!send_device_info) product1String = "Yes";
    else product1String = "No";
    
    int str_len = product1String.length() + 1;
    char char_array[str_len];
    product1String.toCharArray(char_array, str_len);
    Wire.slaveWrite((uint8_t *)char_array, str_len); // Adds the string to Slave buffer, sent on Request
  }
  else
  {
    SerialMon.printf("received message[%d]: ", len);
    SerialMon.println(masterMessage);
  }

  if(masterMessage=="Date")
  {
    String DT = getDateTime();
    int str_len = DT.length() + 1;
    char char_array[str_len];
    DT.toCharArray(char_array, str_len);
    Wire.slaveWrite((uint8_t *)char_array, str_len); 
  }
  else if (masterMessage == "Product1Name") // Filter Master messages
  {
    String product1String = Product1["name"];
    SerialMon.println(product1String);
    int str_len = product1String.length() + 1;
    char char_array[str_len];
    product1String.toCharArray(char_array, str_len);
    Wire.slaveWrite((uint8_t *)char_array, str_len); // Adds the string to Slave buffer, sent on Request
  }
  else if (masterMessage == "Product1Time") // Filter Master messages
  {
    
    String product1String = Product1["time"];
    SerialMon.println(product1String);
    int str_len = product1String.length() + 1;
    char char_array[str_len];
    product1String.toCharArray(char_array, str_len);
    Wire.slaveWrite((uint8_t *)char_array, str_len); // Adds the string to Slave buffer, sent on Request
  }
  else if (masterMessage == "Product1Price") // Filter Master messages
  {
    String product1String = Product1["price"];
    SerialMon.println(product1String);
    int str_len = product1String.length() + 1;
    char char_array[str_len];
    product1String.toCharArray(char_array, str_len);
    Wire.slaveWrite((uint8_t *)char_array, str_len); // Adds the string to Slave buffer, sent on Request
  }

  else if (masterMessage == "Product2Name") // Filter Master messages
  {
    String product1String = Product2["name"];
    SerialMon.println(product1String);
    int str_len = product1String.length() + 1;
    char char_array[str_len];
    product1String.toCharArray(char_array, str_len);
    Wire.slaveWrite((uint8_t *)char_array, str_len); // Adds the string to Slave buffer, sent on Request
  }

  else if (masterMessage == "Product2Time") // Filter Master messages
  {
    String product1String = Product2["time"];
    SerialMon.println(product1String);
    int str_len = product1String.length() + 1;
    char char_array[str_len];
    product1String.toCharArray(char_array, str_len);
    Wire.slaveWrite((uint8_t *)char_array, str_len); // Adds the string to Slave buffer, sent on Request
  }

  else if (masterMessage == "Product2Price") // Filter Master messages
  {
    String product1String = Product2["price"];
    SerialMon.println(product1String);
    int str_len = product1String.length() + 1;
    char char_array[str_len];
    product1String.toCharArray(char_array, str_len);
    Wire.slaveWrite((uint8_t *)char_array, str_len); // Adds the string to Slave buffer, sent on Request
  }

  else if (masterMessage == "Product3Name") // Filter Master messages
  {
    String product1String = Product3["name"];
    SerialMon.println(product1String);
    int str_len = product1String.length() + 1;
    char char_array[str_len];
    product1String.toCharArray(char_array, str_len);
    Wire.slaveWrite((uint8_t *)char_array, str_len); // Adds the string to Slave buffer, sent on Request
  }

  else if (masterMessage == "Product3Time") // Filter Master messages
  {
    String product1String = Product3["time"];
    SerialMon.println(product1String); 
    int str_len = product1String.length() + 1;
    char char_array[str_len];
    product1String.toCharArray(char_array, str_len);
    Wire.slaveWrite((uint8_t *)char_array, str_len); // Adds the string to Slave buffer, sent on Request
  }

  else if (masterMessage == "Product3Price") // Filter Master messages
  {
    String product1String = Product3["price"];
    SerialMon.println(product1String);  
    int str_len = product1String.length() + 1;
    char char_array[str_len];
    product1String.toCharArray(char_array, str_len);
    Wire.slaveWrite((uint8_t *)char_array, str_len); // Adds the string to Slave buffer, sent on Request
  }

  else if (masterMessage == "Product4Name") // Filter Master messages
  {
    String product1String = Product4["name"];
    SerialMon.println(product1String);   
    int str_len = product1String.length() + 1;
    char char_array[str_len];
    product1String.toCharArray(char_array, str_len);
    Wire.slaveWrite((uint8_t *)char_array, str_len); // Adds the string to Slave buffer, sent on Request
  }

  else if (masterMessage == "Product4Time") // Filter Master messages
  {
    String product1String = Product4["time"];
    SerialMon.println(product1String);
    int str_len = product1String.length() + 1;
    char char_array[str_len];
    product1String.toCharArray(char_array, str_len);
    Wire.slaveWrite((uint8_t *)char_array, str_len); // Adds the string to Slave buffer, sent on Request
  }

  else if (masterMessage == "Product4Price") // Filter Master messages
  {
    String product1String = Product4["price"];
    SerialMon.println(product1String);
    int str_len = product1String.length() + 1;
    char char_array[str_len];
    product1String.toCharArray(char_array, str_len);
    Wire.slaveWrite((uint8_t *)char_array, str_len); // Adds the string to Slave buffer, sent on Request
  }

  else if(msg[0]!='R' and msg[0]!='P') 
  {
    parse_i2c_message(masterMessage); 
  }
}

void parse_i2c_message(String message) 
{
    String eventMsg;
    String choice;
    String datetime;
    
    int enamount;
    int disamount;
    int price;
    int index;
    
    index = message.indexOf(':'); // Split the message by ':'
    eventMsg = message.substring(0, index);
    message = message.substring(index + 1);
    index = message.indexOf(':');
    choice = message.substring(0, index);
    message = message.substring(index + 1);
    index = message.indexOf(':');
    enamount = message.substring(0, index).toInt();
    message = message.substring(index + 1);
    index = message.indexOf(':');
    disamount = message.substring(0, index).toInt();
    price = message.substring(index + 1).toInt();
    
    datetime = getDateTime();
    
    Serial.println(message);
    Serial.println(eventMsg);
    Serial.println(choice);
    Serial.println(enamount);
    Serial.println(disamount);
    Serial.println(price);
    Serial.println(datetime);

    int str_len = eventMsg.length() + 1;
    char event_array[str_len];
    eventMsg.toCharArray(event_array, str_len);

    int str_len_choice = choice.length() + 1;
    char choice_array[str_len_choice];
    choice.toCharArray(choice_array, str_len_choice);

    int str_len_date = datetime.length() + 1;
    char DT_array[str_len_date];
    datetime.toCharArray(DT_array, str_len_date);

    azure_pnp_send_telemetry(&azure_iot, event_array, choice_array, enamount, disamount,price,DT_array,0,0,0,0);

}
/* --- Arduino setup and loop Functions --- */
void setup()
{
  scale1.begin(LOADCELL_DOUT_PIN1, LOADCELL_SCK_PIN);
  scale2.begin(LOADCELL_DOUT_PIN2, LOADCELL_SCK_PIN);
  scale3.begin(LOADCELL_DOUT_PIN3, LOADCELL_SCK_PIN);
  scale4.begin(LOADCELL_DOUT_PIN4, LOADCELL_SCK_PIN);

  scale1.set_scale(scale1CalibrationFactor);
  scale2.set_scale(scale2CalibrationFactor);
  scale3.set_scale(scale3CalibrationFactor);
  scale4.set_scale(scale4CalibrationFactor);

  Product1["name"] = "Sunsilk Pink";
  Product2["name"] = "Sunsilk Black";
  Product3["name"] = "Lifebuoy Handwash";
  Product4["name"] = "Dove Hairfall Rescue";

  Product1["price"] = 1.3;
  Product2["price"] = 1.3;
  Product3["price"] = 1.3;
  Product4["price"] = 1.3;

  Product1["time"] = 1.2;
  Product2["time"] = 1.2;
  Product3["time"] = 1.2;
  Product4["time"] = 1.2;

  // Set serial monitor debugging window baud rate to 115200
  SerialMon.begin(SERIAL_LOGGER_BAUD_RATE);
  set_logging_function(logging_function);
  
  // Start I2C communication
  Wire.onReceive(onReceive);
  Wire.onRequest(onRequest);
  Wire.begin((uint8_t)I2C_DEV_ADDR); // Starting Wire as Slave
  
  // Set modem reset, enable, power pins for SIM800L
  pinMode(MODEM_PWKEY, OUTPUT);
  pinMode(MODEM_RST, OUTPUT);
  pinMode(MODEM_POWER_ON, OUTPUT);
    
  digitalWrite(MODEM_PWKEY, LOW);
  digitalWrite(MODEM_RST, HIGH);
  digitalWrite(MODEM_POWER_ON, HIGH);
  
  delay(6000);

  // Set SIM800L module baud rate and UART pins
  SerialAT.begin(115200, SERIAL_8N1, 26, 27);
  
  if (TINY_GSM_USE_WIFI)
  {
    while (WiFi.status() != WL_CONNECTED) connect_to_wifi();
    
    sync_device_clock_with_ntp_server();
    azure_pnp_init();
  }
  else if (!TINY_GSM_USE_WIFI)
  {
    // Restart SIM800 module, it takes quite some time
    // To skip it, call init() instead of restart()
    SerialMon.println("Initializing modem...");
    modem.restart();
    delay(3000);
    
    modem.sendAT("+SSLOPT=1,1");
    if (modem.waitResponse() != 1)
    {
      SerialMon.printf("modem +SSLOPT=1,1 failed");
    }

    String modemName = modem.getModemName();
    SerialMon.println("Modem Name: ");
    SerialMon.println(modemName);
    SerialMon.println();
    
    String modemInfo = modem.getModemInfo();
    SerialMon.println("Modem Info: ");
    SerialMon.println(modemInfo);
    SerialMon.println();

    
    Serial.print("Waiting for network... ");
    modem.waitForNetwork(600000L);
    
    if (modem.isNetworkConnected()) SerialMon.println("Network connected!");

    // use modem.init() if you don't need the complete restart
    modem.gprsConnect(apn, gprsUser, gprsPass);
    
    // GPRS connection parameters are usually set after network registration
    int connectAttempts = 0;
    while (!modem.gprsConnect(apn, gprsUser, gprsPass))
    {
      SerialMon.print(F("Connecting to "));
      SerialMon.print(apn);
      SerialMon.println("...");
      delay(10000);
      
      connectAttempts++;
      if (connectAttempts > 3) 
      {
        SerialMon.print("Unable to connect to ");
        SerialMon.print(apn);
        SerialMon.println("! Restarting...");
        delay(750);
        ESP.restart();
      }
    }

    bool res = modem.isGprsConnected();
    SerialMon.print("GPRS status:");
    SerialMon.print(res ? "connected" : "not connected");

    String ccid = modem.getSimCCID();
    SerialMon.print("CCID:"); 
    SerialMon.print(ccid);

    String imei = modem.getIMEI();
    SerialMon.print("IMEI:");
    SerialMon.print(imei);

    String imsi = modem.getIMSI();
    SerialMon.print("IMSI:");
    SerialMon.print(imsi);

    String cop = modem.getOperator();
    SerialMon.print("Operator:");
    SerialMon.print(cop);

    IPAddress local = modem.localIP();
    SerialMon.print("Local IP:");
    SerialMon.print(local);
    
    int csq = modem.getSignalQuality();
    SerialMon.print("Signal quality:");
    SerialMon.print(csq);
    
    if (modem.isGprsConnected())
    { 
      sync_device_clock_with_ntp_server();
      azure_pnp_init();
    } 
  }

  /*
   * The configuration structure used by Azure IoT must remain unchanged (including data buffer)
   * throughout the lifetime of the sample. This variable must also not lose context so other
   * components do not overwrite any information within this structure.
   */
  azure_iot_config.user_agent = AZ_SPAN_FROM_STR(AZURE_SDK_CLIENT_USER_AGENT);
  azure_iot_config.model_id = azure_pnp_get_model_id();
  azure_iot_config.use_device_provisioning = true; // Required for Azure IoT Central.
  azure_iot_config.iot_hub_fqdn = AZ_SPAN_EMPTY;
  azure_iot_config.device_id = AZ_SPAN_EMPTY;

#ifdef IOT_CONFIG_USE_X509_CERT
  azure_iot_config.device_certificate = AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_CERT);
  azure_iot_config.device_certificate_private_key = AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_CERT_PRIVATE_KEY);
  azure_iot_config.device_key = AZ_SPAN_EMPTY;
#else
  azure_iot_config.device_certificate = AZ_SPAN_EMPTY;
  azure_iot_config.device_certificate_private_key = AZ_SPAN_EMPTY;
  azure_iot_config.device_key = AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_KEY);
#endif // IOT_CONFIG_USE_X509_CERT

  azure_iot_config.dps_id_scope = AZ_SPAN_FROM_STR(DPS_ID_SCOPE);
  azure_iot_config.dps_registration_id = AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_ID); // Use Device ID for Azure IoT Central.
  azure_iot_config.data_buffer = AZ_SPAN_FROM_BUFFER(az_iot_data_buffer);
  azure_iot_config.sas_token_lifetime_in_minutes = MQTT_PASSWORD_LIFETIME_IN_MINUTES;
  azure_iot_config.mqtt_client_interface.mqtt_client_init = mqtt_client_init_function;
  azure_iot_config.mqtt_client_interface.mqtt_client_deinit = mqtt_client_deinit_function;
  azure_iot_config.mqtt_client_interface.mqtt_client_subscribe = mqtt_client_subscribe_function;
  azure_iot_config.mqtt_client_interface.mqtt_client_publish = mqtt_client_publish_function;
  azure_iot_config.data_manipulation_functions.hmac_sha256_encrypt = mbedtls_hmac_sha256;
  azure_iot_config.data_manipulation_functions.base64_decode = base64_decode;
  azure_iot_config.data_manipulation_functions.base64_encode = base64_encode;
  azure_iot_config.on_properties_update_completed = on_properties_update_completed;
  azure_iot_config.on_properties_received = on_properties_received;
  azure_iot_config.on_command_request_received = on_command_request_received;
  timer = timerBegin(0, 80, true);
  timerStop(timer);
  azure_iot_init(&azure_iot, &azure_iot_config);
  azure_iot_start(&azure_iot);

  LogInfo("Azure IoT client initialized (state=%d)", azure_iot.state);
}

void loop()
{
  if (TINY_GSM_USE_WIFI)
  {
    if (WiFi.status() != WL_CONNECTED)
    {
      connect_to_wifi();
      azure_iot_start(&azure_iot);
    }
    else
    {
      switch (azure_iot_get_status(&azure_iot))
      {
        case azure_iot_connected:
          if (send_device_info)
          {
            send_device_info = false; // Only need to send once.
            SerialMon.println("I2C Ready!");
          }
          else if (!send_device_info)
          {
            if (!timerEn)
            {
              timerRestart(timer);
              timerStart(timer);
              timerEn=true;
            }
            float timeTaken = timerReadSeconds(timer);
      
            if (timeTaken > telemetery_f_seconds)
            {
              takeStock=true;
            }
            if (takeStock)
            {
              takeStockTel();
              takeStock=false;
              timerEn=false; 
            }
          }
          break;
          
        case azure_iot_error:
          LogError("Azure IoT client is in error state.");
          azure_iot_stop(&azure_iot);
          delay(1000);
          ESP.restart();
          break;
          
        default:
          break;
      }
  
      azure_iot_do_work(&azure_iot);
    }
  }
  else
  {
    if (!modem.gprsConnect(apn, gprsUser, gprsPass))
    {
      restartSimCard();
      azure_iot_start(&azure_iot);
    }
    else
    {
      switch (azure_iot_get_status(&azure_iot))
      {
        case azure_iot_connected:
          if (send_device_info)
          {
            send_device_info = false; // Only need to send once.
            SerialMon.println("I2C Ready!");
          }
          else if (!send_device_info)
          {
            if(!timerEn)
            {
              timerRestart(timer);
              timerStart(timer);
              timerEn=true;
            }
            float timeTaken = timerReadSeconds(timer);
      
            if (timeTaken>telemetery_f_seconds)
            {
              takeStock=true;
            }
            if (takeStock)
            {
              takeStockTel();
              takeStock=false;
              timerEn=false; 
            }
          }
          break;
          
        case azure_iot_error:
          LogError("Azure IoT client is in error state.");
          azure_iot_stop(&azure_iot);
          delay(1000);
          ESP.restart();
          break;
          
        default:
          break;
      }
  
      azure_iot_do_work(&azure_iot);
    }
  }
}

/* === Function Implementations === */

/*
 * These are support functions used by the sample itself to perform its basic tasks
 * of connecting to the internet, syncing the board clock, ESP MQTT client event handler
 * and logging.
 */

void restartSimCard()
{
  modem.restart(); // use modem.init() if you don't need the complete restart
  
  // Unlock your SIM card with a PIN if needed
  if (strlen(simPIN) && modem.getSimStatus() != 3)
  {
    modem.simUnlock(simPIN);
  }
}

static void takeStockTel()
{
  long product1Mass = get_product1_mass();
  long product2Mass = get_product2_mass();
  long product3Mass = get_product3_mass();
  long product4Mass = get_product4_mass();

  SerialMon.println(product1Mass);
  SerialMon.println(product2Mass);
  SerialMon.println(product3Mass);
  SerialMon.println(product4Mass);
  
  char * event = "Stock Take";
  char * product = "Stock Take";
  char * date ="";
  azure_pnp_send_telemetry(&azure_iot, event, product, 999, 999, 999,date,product1Mass,product2Mass,product3Mass,product4Mass);
}

/* --- System and Platform Functions --- */
static void sync_device_clock_with_ntp_server()
{
  LogInfo("Setting time using SNTP");

  configTime(GMT_OFFSET_SECS, GMT_OFFSET_SECS_DST, NTP_SERVERS);
  time_t now = time(NULL);
  while (now < UNIX_TIME_NOV_13_2017)
  {
    delay(500);
    SerialMon.print(".");
    now = time(NULL);
  }
  SerialMon.println("");
  LogInfo("Time initialized!");
}

static void connect_to_gprs()
{
  
}

static void connect_to_wifi()
{
  LogInfo("Connecting to WIFI wifi_ssid %s", wifi_ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");

  LogInfo("WiFi connected, IP address: %s", WiFi.localIP().toString().c_str());
}

static esp_err_t esp_mqtt_event_handler(esp_mqtt_event_handle_t event)
{
  switch (event->event_id)
  {
    int i, r;

  case MQTT_EVENT_ERROR:
    LogError("MQTT client in ERROR state.");
    LogError(
        "esp_tls_stack_err=%d; esp_tls_cert_verify_flags=%d;esp_transport_sock_errno=%d;error_type=%d;connect_return_code=%d",
        event->error_handle->esp_tls_stack_err,
        event->error_handle->esp_tls_cert_verify_flags,
        event->error_handle->esp_transport_sock_errno,
        event->error_handle->error_type,
        event->error_handle->connect_return_code);

    switch (event->error_handle->connect_return_code)
    {
    case MQTT_CONNECTION_ACCEPTED:
      LogError("connect_return_code=MQTT_CONNECTION_ACCEPTED");
      break;
    case MQTT_CONNECTION_REFUSE_PROTOCOL:
      LogError("connect_return_code=MQTT_CONNECTION_REFUSE_PROTOCOL");
      break;
    case MQTT_CONNECTION_REFUSE_ID_REJECTED:
      LogError("connect_return_code=MQTT_CONNECTION_REFUSE_ID_REJECTED");
      break;
    case MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE:
      LogError("connect_return_code=MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE");
      break;
    case MQTT_CONNECTION_REFUSE_BAD_USERNAME:
      LogError("connect_return_code=MQTT_CONNECTION_REFUSE_BAD_USERNAME");
      break;
    case MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED:
      LogError("connect_return_code=MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED");
      break;
    default:
      LogError("connect_return_code=unknown (%d)", event->error_handle->connect_return_code);
      break;
    };

    break;
  case MQTT_EVENT_CONNECTED:
    LogInfo("MQTT client connected (session_present=%d).", event->session_present);

    if (azure_iot_mqtt_client_connected(&azure_iot) != 0)
    {
      LogError("azure_iot_mqtt_client_connected failed.");
    }

    break;
  case MQTT_EVENT_DISCONNECTED:
    LogInfo("MQTT client disconnected.");

    if (azure_iot_mqtt_client_disconnected(&azure_iot) != 0)
    {
      LogError("azure_iot_mqtt_client_disconnected failed.");
    }

    break;
  case MQTT_EVENT_SUBSCRIBED:
    LogInfo("MQTT topic subscribed (message id=%d).", event->msg_id);

    if (azure_iot_mqtt_client_subscribe_completed(&azure_iot, event->msg_id) != 0)
    {
      LogError("azure_iot_mqtt_client_subscribe_completed failed.");
    }

    break;
  case MQTT_EVENT_UNSUBSCRIBED:
    LogInfo("MQTT topic unsubscribed.");
    break;
  case MQTT_EVENT_PUBLISHED:
    LogInfo("MQTT event MQTT_EVENT_PUBLISHED");

    if (azure_iot_mqtt_client_publish_completed(&azure_iot, event->msg_id) != 0)
    {
      LogError("azure_iot_mqtt_client_publish_completed failed (message id=%d).", event->msg_id);
    }

    break;
  case MQTT_EVENT_DATA:
    LogInfo("MQTT message received.");

    mqtt_message_t mqtt_message;
    mqtt_message.topic = az_span_create((uint8_t *)event->topic, event->topic_len);
    mqtt_message.payload = az_span_create((uint8_t *)event->data, event->data_len);
    mqtt_message.qos = mqtt_qos_at_most_once; // QoS is unused by azure_iot_mqtt_client_message_received.

    if (azure_iot_mqtt_client_message_received(&azure_iot, &mqtt_message) != 0)
    {
      LogError("azure_iot_mqtt_client_message_received failed (topic=%.*s).", event->topic_len, event->topic);
    }

    break;
  case MQTT_EVENT_BEFORE_CONNECT:
    LogInfo("MQTT client connecting.");
    break;
  default:
    LogError("MQTT event UNKNOWN.");
    break;
  }

  return ESP_OK;
}

static String getDateTime(){
  String dateTime ="";

  struct tm *ptm;
  time_t now = time(NULL);

  ptm = gmtime(&now);

  String year = String(ptm->tm_year + UNIX_EPOCH_START_YEAR);
  String month = String(ptm->tm_mon + 1);
  String day = String(ptm->tm_mday);
  String hour ="";
  if (ptm->tm_hour<10){
    hour+="0";
  }
  hour +=String(ptm->tm_hour+6);
  String min ="";
  if(ptm->tm_min<10){
    min+="0";
  }
  min+=String(ptm->tm_min);

  String seconds = "";

  if (ptm->tm_sec < 10)
  {
    seconds +="0";
  }
  seconds+=String(ptm->tm_sec);

  dateTime= day+"-"+month+"-"+year+" "+hour+":"+min+":"+seconds;
  
  return dateTime;
}

static void logging_function(log_level_t log_level, char const *const format, ...)
{
  struct tm *ptm;
  time_t now = time(NULL);

  ptm = gmtime(&now);

  SerialMon.print(ptm->tm_year + UNIX_EPOCH_START_YEAR);
  SerialMon.print("/");
  SerialMon.print(ptm->tm_mon + 1);
  SerialMon.print("/");
  SerialMon.print(ptm->tm_mday);
  SerialMon.print(" ");

  if (ptm->tm_hour < 10)
  {
    SerialMon.print(0);
  }

  SerialMon.print(ptm->tm_hour+6);
  SerialMon.print(":");

  if (ptm->tm_min < 10)
  {
    SerialMon.print(0);
  }

  SerialMon.print(ptm->tm_min);
  SerialMon.print(":");

  if (ptm->tm_sec < 10)
  {
    SerialMon.print(0);
  }

  SerialMon.print(ptm->tm_sec);

  SerialMon.print(log_level == log_level_info ? " [INFO] " : " [ERROR] ");

  char message[256];
  va_list ap;
  va_start(ap, format);
  int message_length = vsnprintf(message, 256, format, ap);
  va_end(ap);

  if (message_length < 0)
  {
    SerialMon.println("Failed encoding log message (!)");
  }
  else
  {
    SerialMon.println(message);
  }
}
