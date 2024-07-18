#include "esp_camera.h"
#include <WiFi.h>
#include "PCF8575.h"
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>

#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

PCF8575 pcf8575(0x20);

/* Define the WiFi credentials */
#define WIFI_SSID "IOT"
#define WIFI_PASSWORD "IOT12345"

/* Define the API Key and RTDB URL */
#define API_KEY "AIzaSyA4gSWhZc5e30q5X8YW2UyGlcak6omzw8c"
#define DATABASE_URL "https://home-automation-c8617-default-rtdb.firebaseio.com/"

#define USER_EMAIL "murali.bm22@bitsathy.ac.in"

String room_no = "room1";

// Define the GPIO connected with Relays and switches
#define Relay1 P7
#define Relay2 P6
#define Relay3 P5
#define Relay4 P4
#define Relay5 P3
#define Relay6 P2
#define MOTION_SENSOR_PIN 2

#define Switch1 0
#define Switch2 13
#define Switch3 12
#define Switch4 16

int stateRelay1 = 0, stateRelay2 = 0, stateRelay3 = 0, stateRelay4 = 0, stateRelay5 = 0, stateRelay6 = 0;
#define LED_BUILTIN 4
int motionStateCurrent = LOW;
int motionStatePrevious = LOW;

String stream_path = "";
String event_path = "";
String stream_data = "";
FirebaseJson jsonData;
volatile bool dataChanged = false;
bool signupOK = false;
bool uploadBucket = false;
String bucketData = "", bucketPath = "";

FirebaseData stream;
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

boolean matchFace = false;
boolean activeRelay = false;
long prevMillis = 0;
int interval = 500;

const int Password_Length = 7;
String Data;

String Master1 = "12345";
String Master2 = "45678";
String Master3 = "91011";

int lockOutput = 11;
byte data_count = 0;
char customKey;

const byte ROWS = 4;
const byte COLS = 4;
char hexaKeys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte rowPins[ROWS] = {9, 8, 7, 6};
byte colPins[COLS] = {5, 4, 3, 2};

Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);
LiquidCrystal_I2C lcd(0x3F, 16, 2);

void startCameraServer();
void setupLedFlash(int pin);
void listenSwitches();
void reloadRelayStates();
void clearData();

void setup() {
  Serial.begin(9600);
  Serial.println("Setup started");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback;

  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase signUp ok");
    signupOK = true;
  } else {
    Serial.printf("%s\n", config.signer.signupError.message.c_str());
  }

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  if (!Firebase.RTDB.beginStream(&stream, room_no)) {
    Serial.printf("stream begin error, %s\n\n", stream.errorReason().c_str());
  }

  Firebase.RTDB.setStreamCallback(&stream, streamCallback, streamTimeoutCallback);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG; // for streaming
  config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }
  if (config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, FRAMESIZE_QVGA);
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

#if defined(LED_GPIO_NUM)
  setupLedFlash(LED_GPIO_NUM);
#endif

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  startCameraServer();
  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(MOTION_SENSOR_PIN, INPUT);
  digitalWrite(LED_BUILTIN, LOW);
  lcd.init();
  lcd.backlight();

  pcf8575.pinMode(Relay1, OUTPUT);
  pcf8575.digitalWrite(Relay1, LOW);
  pcf8575.pinMode(Relay2, OUTPUT);
  pcf8575.digitalWrite(Relay2, LOW);
  pcf8575.pinMode(Relay3, OUTPUT);
  pcf8575.digitalWrite(Relay3, LOW);
  pcf8575.pinMode(Relay4, OUTPUT);
  pcf8575.digitalWrite(Relay4, LOW);
  pcf8575.pinMode(Relay5, OUTPUT);
  pcf8575.digitalWrite(Relay5, LOW);
  pcf8575.pinMode(Relay6, OUTPUT);
  pcf8575.digitalWrite(Relay6, LOW);
  pinMode(LED_BUILTIN, OUTPUT);

  pinMode(lockOutput, OUTPUT);

  lcd.print("WELCOME!!!");
  Serial.println("WELCOME!!");
  lcd.setCursor(3, 1);
  lcd.print("HOME AUTOMATION");
  delay(5000);
  lcd.clear();
}

void streamCallback(FirebaseStream data) {
  stream_path = data.streamPath().c_str();
  event_path = data.dataPath().c_str();

  if (String(data.dataType().c_str()) == "json") {
    jsonData = data.to<FirebaseJson>();
    Serial.println("STREAM CALL BACK");
  } else {
    stream_data = data.stringData();
    Serial.println("STREAM DATA CALLBACK");
  }
  dataChanged = true;
}

void streamTimeoutCallback(bool timeout) {
  if (timeout) {
    Serial.println("Stream timeout, resume streaming...");
    Firebase.RTDB.beginStream(&stream, room_no);
  }
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t capture_uri = {
    .uri       = "/capture",
    .method    = HTTP_GET,
    .handler   = capture_handler,
    .user_ctx  = NULL
  };

  ra_filter_init(&ra_filter, 20);
  config.max_uri_handlers = 12;
  httpd_handle_t server = NULL;
  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &capture_uri);
  }
  Serial.printf("Camera Ready! Use 'http://%s' to connect\n", WiFi.localIP().toString().c_str());
}

void setupLedFlash(int pin) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

void loop() {
  if (dataChanged) {
    reloadRelayStates();
    dataChanged = false;
  }
  listenSwitches();
}

void listenSwitches() {
  customKey = customKeypad.getKey();

  if (customKey) {
    Data += customKey;
    data_count++;
  }

  if (customKey == '*') {
    Data = "";
    data_count = 0;
    lcd.clear();
  }

  if (data_count == Password_Length - 1) {
    lcd.clear();
    lcd.setCursor(0, 0);

    if (Data == Master1 || Data == Master2 || Data == Master3) {
      lcd.print("MATCHED");
      digitalWrite(lockOutput, HIGH);
      delay(5000);
      digitalWrite(lockOutput, LOW);
      clearData();
    } else {
      lcd.print("NOT MATCHED");
      delay(1000);
      clearData();
    }
  }
}

void clearData() {
  Data = "";
  data_count = 0;
  lcd.clear();
}

void reloadRelayStates() {
  Firebase.RTDB.getInt(&fbdo, room_no + "/relay1", &stateRelay1);
  Firebase.RTDB.getInt(&fbdo, room_no + "/relay2", &stateRelay2);
  Firebase.RTDB.getInt(&fbdo, room_no + "/relay3", &stateRelay3);
  Firebase.RTDB.getInt(&fbdo, room_no + "/relay4", &stateRelay4);
  Firebase.RTDB.getInt(&fbdo, room_no + "/relay5", &stateRelay5);
  Firebase.RTDB.getInt(&fbdo, room_no + "/relay6", &stateRelay6);

  pcf8575.digitalWrite(Relay1, stateRelay1);
  pcf8575.digitalWrite(Relay2, stateRelay2);
  pcf8575.digitalWrite(Relay3, stateRelay3);
  pcf8575.digitalWrite(Relay4, stateRelay4);
  pcf8575.digitalWrite(Relay5, stateRelay5);
  pcf8575.digitalWrite(Relay6, stateRelay6);
}
