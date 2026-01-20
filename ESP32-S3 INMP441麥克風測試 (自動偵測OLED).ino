/*
 * ESP32-S3 INMP441麥克風測試程式 (修正波形顯示)
 * 硬體接線:
 * INMP441 WS  -> GPIO4
 * INMP441 SCK -> GPIO5
 * INMP441 SD  -> GPIO6
 * 
 * OLED SDA -> GPIO41
 * OLED SCK -> GPIO42
 */

#include <driver/i2s.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED設定
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
#define I2C_SDA 41
#define I2C_SCL 42

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// I2S麥克風設定
#define I2S_WS    4
#define I2S_SCK   5
#define I2S_SD    6
#define I2S_PORT  I2S_NUM_0

// 音訊參數
#define SAMPLE_RATE     16000
#define BUFFER_SIZE     1024
#define DISPLAY_SAMPLES 64

int32_t samples[BUFFER_SIZE];
int16_t displayBuffer[DISPLAY_SAMPLES];

float currentVolume = 0;
float maxVolume = 0;
float avgVolume = 0;
int32_t peakValue = 0;
bool micWorking = false;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== ESP32-S3 INMP441麥克風測試 ===");
  
  // 初始化I2C
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  
  // 初始化OLED
  Serial.println("初始化OLED...");
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("❌ OLED初始化失敗!");
  } else {
    Serial.println("✅ OLED初始化成功");
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Initializing...");
    display.display();
  }
  
  // 初始化I2S麥克風
  if(initI2S()) {
    Serial.println("✅ 麥克風初始化成功!");
    micWorking = true;
    
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(10, 10);
    display.println("MIC Ready");
    display.setTextSize(1);
    display.setCursor(10, 35);
    display.println("Speak to test...");
    display.display();
    delay(1500);
  } else {
    Serial.println("❌ 麥克風初始化失敗!");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("MIC INIT FAILED!");
    display.display();
    while(1) delay(1000);
  }
  
  Serial.println("\n開始錄音測試...");
  Serial.println("請對著麥克風說話");
  Serial.println("----------------------------");
}

void loop() {
  if(!micWorking) {
    delay(1000);
    return;
  }
  
  size_t bytesRead = 0;
  esp_err_t result = i2s_read(I2S_PORT, samples, BUFFER_SIZE * sizeof(int32_t), 
                              &bytesRead, portMAX_DELAY);
  
  if (result == ESP_OK && bytesRead > 0) {
    int samplesRead = bytesRead / sizeof(int32_t);
    
    analyzeAudio(samplesRead);
    prepareDisplayData(samplesRead);
    updateDisplay();
    printAudioStats();
  } else {
    Serial.printf("I2S讀取錯誤: %d, 讀取位元組: %d\n", result, bytesRead);
  }
  
  delay(50);
}

bool initI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };
  
  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("I2S驅動安裝失敗: %d\n", err);
    return false;
  }
  
  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("I2S腳位設定失敗: %d\n", err);
    return false;
  }
  
  i2s_zero_dma_buffer(I2S_PORT);
  
  // 等待麥克風穩定
  delay(500);
  
  return true;
}

void analyzeAudio(int samplesRead) {
  int64_t sum = 0;
  int32_t maxSample = 0;
  peakValue = 0;
  
  for(int i = 0; i < samplesRead; i++) {
    // INMP441輸出32位元,取高24位元
    int32_t sample = samples[i] >> 8;
    int32_t absSample = abs(sample);
    sum += absSample;
    
    if(absSample > maxSample) {
      maxSample = absSample;
      peakValue = sample;
    }
  }
  
  avgVolume = (float)sum / samplesRead;
  currentVolume = (float)maxSample / 8388608.0 * 100.0;
  
  if(currentVolume > maxVolume) {
    maxVolume = currentVolume;
  }
}

void prepareDisplayData(int samplesRead) {
  int step = samplesRead / DISPLAY_SAMPLES;
  if(step < 1) step = 1;
  
  for(int i = 0; i < DISPLAY_SAMPLES && i * step < samplesRead; i++) {
    // 取得樣本並轉換
    int32_t sample = samples[i * step] >> 8;
    
    // 縮放到適合顯示的範圍 (-12 to +12 pixels)
    float normalized = (float)sample / 8388608.0;  // 正規化到 -1.0 ~ 1.0
    displayBuffer[i] = (int16_t)(normalized * 12.0);  // 縮放到 ±12 像素
    
    // 限制範圍
    if(displayBuffer[i] > 12) displayBuffer[i] = 12;
    if(displayBuffer[i] < -12) displayBuffer[i] = -12;
  }
}

void updateDisplay() {
  display.clearDisplay();
  
  // 標題
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("=== MIC Monitor ===");
  
  // 音量資訊
  display.setCursor(0, 12);
  display.printf("Volume: %3.0f%%", currentVolume);
  
  display.setCursor(0, 22);
  display.printf("Max:    %3.0f%%", maxVolume);
  
  // 音量條
  display.drawRect(0, 34, 128, 10, SSD1306_WHITE);
  int barWidth = (int)(currentVolume * 1.26);
  if(barWidth > 126) barWidth = 126;
  if(barWidth > 0) {
    display.fillRect(1, 35, barWidth, 8, SSD1306_WHITE);
  }
  
  // === 波形顯示區域 ===
  int waveformTop = 46;     // 波形區域頂部
  int waveformBottom = 63;  // 波形區域底部
  int centerY = 54;         // 中心線位置
  
  // 繪製波形區域邊框
  display.drawRect(0, waveformTop, 128, 18, SSD1306_WHITE);
  
  // 繪製中心參考線 (虛線)
  for(int x = 1; x < 127; x += 3) {
    display.drawPixel(x, centerY, SSD1306_WHITE);
  }
  
  // 繪製波形
  for(int i = 0; i < DISPLAY_SAMPLES - 1; i++) {
    int x1 = 1 + i * 2;
    int x2 = 1 + (i + 1) * 2;
    
    int y1 = centerY - displayBuffer[i];
    int y2 = centerY - displayBuffer[i + 1];
    
    // 確保不超出邊界
    y1 = constrain(y1, waveformTop + 1, waveformBottom - 1);
    y2 = constrain(y2, waveformTop + 1, waveformBottom - 1);
    
    // 繪製線段
    display.drawLine(x1, y1, x2, y2, SSD1306_WHITE);
  }
  
  display.display();
}

void printAudioStats() {
  static unsigned long lastPrint = 0;
  static int printCount = 0;
  
  if(millis() - lastPrint > 500) {
    Serial.printf("[%04d] 音量: %5.1f%% | 最大: %5.1f%% | 平均: %8.0f | 峰值: %10d | 波形[0]=%d\n", 
                  printCount++, currentVolume, maxVolume, avgVolume, peakValue, displayBuffer[0]);
    lastPrint = millis();
  }
}
