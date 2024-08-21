#define BLYNK_TEMPLATE_ID "TMPL6uJW170NH"
#define BLYNK_TEMPLATE_NAME "BaoChay"
#define BLYNK_AUTH_TOKEN "-7XTSLyMsZqnpWkJQoDFRpIArW0At7eK"

#include <BlynkSimpleEsp32.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <DHT.h>
#include <DHT_U.h>
#include <LiquidCrystal_I2C.h> // LCD DISPLAY
#include <math.h>
#include <string.h>



#define DHTTYPE DHT11   // DHT 11
#define DHTPIN 14
#define MODEM_TX 17
#define MODEM_RX 16
#define SerialMon Serial
#define SerialAT Serial1
#define BUZZER_PIN 13

int Vout;
volatile double ppm;  // Sử dụng volatile để đảm bảo giá trị được cập nhật đồng bộ giữa các task
volatile float t;
volatile float h;
int lcdColumns = 16;
int lcdRows = 2;
int buzzer;
bool smsRequested = false; // Cờ để yêu cầu gửi SMS
bool smsSent = false; // Cờ để đánh dấu đã gửi SMS
bool callRequested = false; // Cờ để yêu cầu gọi điện
bool callMade = false; // Cờ để đánh dấu đã gọi điện
double RL = 30; // Điện trở tải 30K Ohms
double RO = 70; // Điện trở cảm biến trong không khí sạch, cần hiệu chỉnh lại

char auth[] = BLYNK_AUTH_TOKEN;
char ssid[] = "iPhone";
char pass[] = "02022002";

// SIM card settings
const char simPIN[]  = "";  // SIM card PIN (if any)
const char phone_number[] = "+84787413176"; // Phone number to send SMS

DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, lcdColumns, lcdRows);

TaskHandle_t DisplayDataTaskHandle = NULL;
TaskHandle_t SendSMSTaskHandle = NULL;
TaskHandle_t MakeCallTaskHandle = NULL;

void readDHT11() 
{
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  if (!isnan(temp) && !isnan(hum)) 
  {
    t = temp;
    h = hum;
  } 
  else 
  {
    Serial.println("Khong The Doc");
  }
}

void MQ135() 
{
  Vout = analogRead(36);
  double RS;
  double ratio;

  // Chuyển đổi giá trị ADC thành điện áp (ESP32 sử dụng dải 0-5 và độ phân giải 12-bit)
  double V = Vout * (5.0 / 4095.0);
  // Tính điện trở cảm biến (RS) dựa trên điện áp đo được và điện trở tải (RL)
  RS = (5.0 - V) / V * RL;
  // Tính tỷ lệ RS/RO
  ratio = RS / RO;
  // Tính PPM dựa trên tỷ lệ RS/RO. Đường cong đặc tính cụ thể cần điều chỉnh từ datasheet của MQ-135
  ppm = 102.2 * pow(ratio, -2.473);
}

void sendSMS() 
{
  Serial1.println("AT+CMGF=1"); 
  delay(1000);
  Serial1.print("AT+CMGS=\"");
  Serial1.print(phone_number);
  Serial1.println("\"");
  delay(1000);
  Serial1.println("Warning: High PPM detected!");
  delay(1000);
  Serial1.write(26); 
}

void makeCall() 
{
  Serial1.println("ATD" + String(phone_number) + ";"); 
  delay(1000); // 
  Serial1.println("ATH"); 
}

void DisplayData(void *pvParameters) 
{
  char buffer[16];
  while (true) 
  {
    lcd.setCursor(0, 0);  
    if (ppm > 300) 
    {
      lcd.print("CanhBao :       "); // Clear the rest of the line
      digitalWrite(BUZZER_PIN, HIGH);  // Kích hoạt buzzer
      if (!smsSent && WiFi.status() != WL_CONNECTED) 
      {
        smsRequested = true; // Yêu cầu gửi SMS
        vTaskResume(SendSMSTaskHandle); // Kích hoạt task gửi SMS
      }
      else if (!callMade && WiFi.status() != WL_CONNECTED) 
      {
        callRequested = true; // Yêu cầu gọi điện
        vTaskResume(MakeCallTaskHandle); // Kích hoạt task gọi điện
      }
    } 
    else 
    {
      lcd.print("AnToan :        ");
      digitalWrite(BUZZER_PIN, LOW);
      smsRequested = false;
      smsSent = false;
      callRequested = false;
      callMade = false;
    }

    lcd.setCursor(10, 0);
    snprintf(buffer, sizeof(buffer), "%4.1f   ", ppm);
    lcd.print(buffer);

    lcd.setCursor(0, 1);
    snprintf(buffer, sizeof(buffer), "T=%2.1f | H=%2.1f ", t, h);
    lcd.print(buffer);

    // In thông tin ra serial monitor
    Serial.print("\n");
    Serial.print("PPM: ");
    Serial.print(ppm);
    Serial.print("\tT: ");
    Serial.print(t);
    Serial.print("C\tH: ");
    Serial.print(h);
    Serial.println("%");

    vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay for 1 second
  }
}

void SendDataToBlynk(void *pvParameters) 
{
  while (true) 
  {
    if (WiFi.status() == WL_CONNECTED) 
    {
      Blynk.run();
      Blynk.virtualWrite(V0, ppm);
      Blynk.virtualWrite(V1, t);
      Blynk.virtualWrite(V2, h);
      if (ppm > 300) 
      {
          Blynk.logEvent("canhbao");
      }
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS); 
  }
}

void SensorReadTask(void *pvParameters) 
{
  unsigned long warmupEnd = millis() + 8000; // 8 giây thời gian khởi động
  while (true) 
  {
    if (millis() > warmupEnd) 
    {
      MQ135();
      readDHT11();
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void SendSMSTask(void *pvParameters) 
{
  while (true) 
  {
    vTaskSuspend(NULL); // Suspend task until it is needed
    sendSMS();
    smsSent = true;
  }
}

void MakeCallTask(void *pvParameters) 
{
  while (true) 
  {
    vTaskSuspend(NULL); // Suspend task until it is needed
    makeCall();
    callMade = true;
  }
}

void setup() 
{
  lcd.init();
  lcd.backlight();
  pinMode(BUZZER_PIN, OUTPUT);  // Thiết lập chân buzzer là OUTPUT

  SerialMon.begin(115200);
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX); // RX, TX
  dht.begin();
  
  // Khởi động kết nối WiFi không chặn
  WiFi.begin(ssid, pass);
  Blynk.config(auth);

  // Initialize SIM A7680C
  SerialAT.println("AT"); // Test the connection
  delay(1000);
  SerialAT.println("AT+CPIN?"); // Check SIM card status
  delay(1000);
  SerialAT.println("AT+CMGF=1"); // Set SMS mode to text
  delay(1000);

  delay(8000); // Thời gian khởi động cho cảm biến MQ135
  
  xTaskCreatePinnedToCore(SensorReadTask, "SensorReadTask", 2048, NULL, 1, NULL, 1); // Core 1
  xTaskCreatePinnedToCore(DisplayData, "DisplayData", 2048, NULL, 1, NULL, 0); // Core 0
  xTaskCreatePinnedToCore(SendDataToBlynk, "SendDataToBlynk", 4096, NULL, 1, NULL, 0); // Core 0
  xTaskCreatePinnedToCore(SendSMSTask, "SendSMSTask", 2048, NULL, 1, &SendSMSTaskHandle, 0); // Core 0
  xTaskCreatePinnedToCore(MakeCallTask, "MakeCallTask", 2048, NULL, 1, &MakeCallTaskHandle, 0); // Core 0
}

void loop() 
{
}
