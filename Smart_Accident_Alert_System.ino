#include <Wire.h>
#include <MPU6050.h>
#include <DHT.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <WiFi.h>

/* -------- WIFI -------- */
const char* ssid = "Shyam";
const char* password = "555544444";

/* -------- SERIAL PORTS -------- */
HardwareSerial gsm(1);
HardwareSerial gpsSerial(2);

/* -------- PIN DEFINITIONS -------- */
#define SDA_PIN 21
#define SCL_PIN 22
#define VIB_PIN 27
#define DHTPIN 4
#define DHTTYPE DHT11
#define BUZZER 18
#define LED 19
#define BUTTON 23

/* -------- THRESHOLDS -------- */
#define G_THRESHOLD_MIN 16
#define G_THRESHOLD_MAX 17
#define TEMP_THRESHOLD 29
#define ALERT_TIME 10000

/* -------- OBJECTS -------- */
MPU6050 mpu;
DHT dht(DHTPIN, DHTTYPE);
TinyGPSPlus gps;

/* -------- VARIABLES -------- */
bool accidentDetected = false;
bool powerFailure = false;
bool powerSmsSent = false;
bool smsSent = false;
bool mpuConnected = false;

unsigned long accidentStartTime;
unsigned long bootTime;

void setup() {

  Serial.begin(9600);

  /* Fix floating input */
  pinMode(VIB_PIN, INPUT_PULLDOWN);
  pinMode(BUZZER, OUTPUT);
  pinMode(LED, OUTPUT);
  pinMode(BUTTON, INPUT_PULLUP);

  digitalWrite(BUZZER, LOW);
  digitalWrite(LED, LOW);

  /* GSM & GPS */
  gsm.begin(9600, SERIAL_8N1, 16, 17);
  gpsSerial.begin(9600, SERIAL_8N1, 26, 25);

  /* Sensors */
  Wire.begin(SDA_PIN, SCL_PIN);
  mpu.initialize();
  dht.begin();

  mpuConnected = mpu.testConnection();
  Serial.println(mpuConnected ? "✅ MPU6050 connected" : "❌ MPU6050 NOT connected");

  /* WiFi */
  WiFi.begin(ssid, password);
  Serial.print("📡 Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi Connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  bootTime = millis();
  Serial.println("🚗 ESP32 Accident Alert System Started");
}

void loop() {

  if (millis() - bootTime < 3000) return;

  /* ---------- SAFE DEFAULTS ---------- */
  long gForce = 0;
  bool MPU_THRESHOLD = false;
  bool vibration = false;
  bool fire = false;

  /* ---------- MPU6050 ---------- */
  if (mpuConnected) {
    int16_t ax, ay, az;
    mpu.getAcceleration(&ax, &ay, &az);

    gForce = sqrt((long)ax * ax + (long)ay * ay + (long)az * az);
    int threshold = gForce / 1000;

    MPU_THRESHOLD = ((threshold > G_THRESHOLD_MAX) || (threshold < G_THRESHOLD_MIN))&&(threshold!=0);
  }

  /* ---------- VIBRATION ---------- */
  vibration = vibrationDetected();

  /* ---------- DHT11 ---------- */
  float temp = dht.readTemperature();
  if (!isnan(temp)) {
    fire = (temp >= TEMP_THRESHOLD);
  } 
  else {
    temp = 0;   // prevent garbage
  }

  /* ---------- DEBUG ---------- */
  Serial.print("Raw: "); Serial.print(gForce);
  Serial.print(" | MPU: "); Serial.print(MPU_THRESHOLD);
  Serial.print(" | Vib: "); Serial.print(vibration);
  Serial.print(" | Temp: "); Serial.print(temp);
  Serial.print(" | Fire: "); Serial.println(fire);

  if(gForce==0){
    ESP.restart();
  }

  /* ---------- ACCIDENT LOGIC ---------- */
  if (!accidentDetected && (MPU_THRESHOLD || vibration || fire)) {
    accidentDetected = true;
    accidentStartTime = millis();
    smsSent = false;

    digitalWrite(LED, HIGH);
    digitalWrite(BUZZER, HIGH);

    Serial.println("🚨 ACCIDENT DETECTED");
    Serial.println("⏳ 10 seconds to cancel");
  }

  /* ---------- CANCEL / SEND ---------- */
  if (accidentDetected) {

    if (digitalRead(BUTTON) == LOW) {
      Serial.println("❌ ALERT CANCELLED");
      accidentDetected = false;
      digitalWrite(LED, LOW);
      digitalWrite(BUZZER, LOW);
      delay(5000);
    }

    if (!smsSent && millis() - accidentStartTime >= ALERT_TIME) {
      sendsms();
      smsSent = true;
      accidentDetected = false;
      digitalWrite(LED, LOW);
      digitalWrite(BUZZER, LOW);
    }
  }
  delay(1000);
}

/* -------- VIBRATION FILTER -------- */
bool vibrationDetected() {
  int highCount = 0;
  for (int i = 0; i < 10; i++) {
    if (digitalRead(VIB_PIN) == HIGH) highCount++;
    delay(10);
  }
  return (highCount >= 7);
}

/* -------- LOCATION -------- */
void getLocation(double &lat, double &lng) {

  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }

  if (gps.location.isValid() && gps.location.age() < 5000) {
    lat = gps.location.lat();
    lng = gps.location.lng();
  } else {
    lat = 11.083588033741563;
    lng = 76.99726596842801;
  }
}

/* -------- SMS -------- */
void sendsms() {

  double latitude, longitude;
  getLocation(latitude, longitude);

  gsm.println("AT");
  delay(1000);
  gsm.println("AT+CMGF=1");
  delay(1000);
  gsm.println("AT+CMGS=\"+919791517772\"");
  delay(1000);

  gsm.println("Accident Detected!");
  gsm.print("https://maps.google.com/?q=");
  gsm.print(latitude, 6);
  gsm.print(",");
  gsm.print(longitude, 6);  

  gsm.write(26);
  Serial.println("📩 SMS SENT");
}