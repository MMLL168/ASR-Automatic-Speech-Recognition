/*
 * ESP32-S3 OLED顯示測試程式 (128x32)
 * 硬體接線:
 * OLED SDA -> GPIO41
 * OLED SCK -> GPIO42
 * OLED VCC -> 3.3V
 * OLED GND -> GND
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED參數設定 (128x32)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1  // 沒有使用Reset腳位
#define SCREEN_ADDRESS 0x3C  // 常見地址為0x3C或0x3D

// I2C腳位定義
#define I2C_SDA 41
#define I2C_SCL 42

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== ESP32-S3 OLED測試程式 (128x32) ===");
  
  // 初始化I2C
  Wire.begin(I2C_SDA, I2C_SCL);
  Serial.println("I2C初始化完成");
  
  // 掃描I2C設備
  Serial.println("掃描I2C設備...");
  scanI2C();
  
  // 初始化OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("❌ OLED初始化失敗!");
    Serial.println("請檢查:");
    Serial.println("1. 接線是否正確");
    Serial.println("2. OLED地址是否為0x3C");
    Serial.println("3. 電源供應是否正常");
    while(1) delay(1000);
  }
  
  Serial.println("✅ OLED初始化成功!");
  
  // 清除畫面
  display.clearDisplay();
  display.display();
  delay(500);
  
  // 執行測試
  testOLED();
}

void loop() {
  // 循環顯示測試
  displayInfo();
  delay(3000);
  
  displayPattern();
  delay(3000);
  
  displayScrollText();
  delay(3000);
  
  displayAnimation();
  delay(3000);
}

// I2C設備掃描
void scanI2C() {
  byte error, address;
  int deviceCount = 0;
  
  Serial.println("開始掃描...");
  for(address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    
    if (error == 0) {
      Serial.print("✅ 發現I2C設備於地址: 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
      deviceCount++;
    }
  }
  
  if (deviceCount == 0) {
    Serial.println("❌ 未發現任何I2C設備!");
  } else {
    Serial.printf("總共發現 %d 個I2C設備\n", deviceCount);
  }
  Serial.println("掃描完成\n");
}

// OLED基本測試
void testOLED() {
  // 測試1: 全螢幕填充
  Serial.println("測試1: 全螢幕填充");
  display.fillScreen(SSD1306_WHITE);
  display.display();
  delay(1000);
  
  display.clearDisplay();
  display.display();
  delay(500);
  
  // 測試2: 繪製矩形
  Serial.println("測試2: 繪製矩形");
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.drawRect(5, 5, SCREEN_WIDTH-10, SCREEN_HEIGHT-10, SSD1306_WHITE);
  display.display();
  delay(1000);
  
  // 測試3: 繪製圓形
  Serial.println("測試3: 繪製圓形");
  display.clearDisplay();
  display.drawCircle(64, 16, 12, SSD1306_WHITE);
  display.fillCircle(64, 16, 6, SSD1306_WHITE);
  display.display();
  delay(1000);
  
  // 測試4: 文字顯示
  Serial.println("測試4: 文字顯示");
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(5, 0);
  display.println("ESP32-S3");
  display.setTextSize(1);
  display.setCursor(15, 20);
  display.println("OLED TEST OK!");
  display.display();
  delay(2000);
  
  Serial.println("✅ 所有測試完成!\n");
}

// 顯示系統資訊 (針對32高度優化)
void displayInfo() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(0, 0);
  display.println("ESP32-S3 N16R8");
  display.println("----------------");
  display.printf("Heap:%dKB Time:%lus", 
                 ESP.getFreeHeap()/1024, 
                 millis()/1000);
  
  display.display();
}

// 顯示圖案 (針對32高度優化)
void displayPattern() {
  display.clearDisplay();
  
  // 繪製網格
  for(int i=0; i<SCREEN_WIDTH; i+=8) {
    display.drawLine(i, 0, i, SCREEN_HEIGHT, SSD1306_WHITE);
  }
  for(int i=0; i<SCREEN_HEIGHT; i+=8) {
    display.drawLine(0, i, SCREEN_WIDTH, i, SSD1306_WHITE);
  }
  
  // 中心圓
  display.fillCircle(64, 16, 8, SSD1306_WHITE);
  
  display.display();
}

// 滾動文字測試
void displayScrollText() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 0);
  display.println("VOICE AI");
  display.setTextSize(1);
  display.setCursor(35, 20);
  display.println("Ready...");
  display.display();
  
  // 啟動滾動
  display.startscrollright(0x00, 0x0F);
  delay(2000);
  display.stopscroll();
}

// 動畫效果 (進度條)
void displayAnimation() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(25, 0);
  display.println("Loading...");
  
  // 進度條動畫
  for(int i=0; i<=100; i+=5) {
    // 進度條外框
    display.drawRect(10, 15, 108, 12, SSD1306_WHITE);
    
    // 進度條填充
    int barWidth = (i * 104) / 100;
    display.fillRect(12, 17, barWidth, 8, SSD1306_WHITE);
    
    // 顯示百分比
    display.fillRect(45, 15, 38, 12, SSD1306_BLACK);
    display.setCursor(50, 18);
    display.setTextColor(SSD1306_WHITE);
    display.printf("%d%%", i);
    
    display.display();
    delay(50);
  }
  
  delay(500);
}