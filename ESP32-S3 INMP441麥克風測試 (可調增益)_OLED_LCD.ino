/*
 * ESP32-S3 INMP441麥克風測試程式 (可調整增益)
 * 
 * 硬體接線:
 * === INMP441麥克風 ===
 * WS  -> GPIO4
 * SCK -> GPIO5
 * SD  -> GPIO6
 * VDD -> 3.3V
 * GND -> GND
 * L/R -> GND
 * 
 * === OLED (SSD1306 128x64) ===
 * SDA -> GPIO41
 * SCL -> GPIO42
 * 
 * === TFT LCD (ILI9341 240x320) ===
 * CS   -> GPIO45
 * DC   -> GPIO47
 * RST  -> GPIO21
 * MOSI -> GPIO20
 * SCK  -> GPIO19
 * BL   -> GPIO38
 */

#include <driver/i2s.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_ILI9341.h>

// ===== 🎚️ 音訊增益設定 (在這裡調整!) =====
#define MIC_GAIN 2.0        // 麥克風增益倍率 (1.0 = 原始, 2.0 = 2倍, 4.0 = 4倍)
#define WAVE_GAIN 1.0       // 波形顯示增益 (獨立調整波形靈敏度)
// ==========================================

// ===== OLED設定 =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
#define I2C_SDA 41
#define I2C_SCL 42

Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ===== TFT LCD設定 =====
#define TFT_CS   45
#define TFT_DC   47
#define TFT_RST  21
#define TFT_MOSI 20
#define TFT_SCK  19
#define TFT_BL   38

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCK, TFT_RST);

// ===== I2S麥克風設定 =====
#define I2S_WS    4
#define I2S_SCK   5
#define I2S_SD    6
#define I2S_PORT  I2S_NUM_0

// ===== 音訊參數 =====
#define SAMPLE_RATE     16000
#define BUFFER_SIZE     512
#define DISPLAY_SAMPLES 64
#define TFT_SAMPLES     120

int32_t samples[BUFFER_SIZE];
int16_t displayBuffer[DISPLAY_SAMPLES];
int16_t tftBuffer[TFT_SAMPLES];

float currentVolume = 0;
float maxVolume = 0;
float avgVolume = 0;
int32_t peakValue = 0;
bool oledOK = false;
bool tftOK = false;

// 幀率統計
unsigned long lastFrameTime = 0;
int frameCount = 0;
float fps = 0;

// TFT顏色定義
#define COLOR_BG       0x0000
#define COLOR_TEXT     0xFFFF
#define COLOR_TITLE    0x07FF
#define COLOR_BAR      0x07E0
#define COLOR_BAR_HIGH 0xFFE0
#define COLOR_BAR_MAX  0xF800
#define COLOR_WAVE     0x07FF
#define COLOR_GRID     0x4208

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║  ESP32-S3 INMP441 高速監測系統        ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.printf("\n🎚️  麥克風增益: %.1fx\n", MIC_GAIN);
  Serial.printf("📊 波形增益: %.1fx\n\n", WAVE_GAIN);
  
  // ===== 初始化TFT背光 =====
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  Serial.println("✅ TFT背光已開啟");
  
  // ===== 初始化I2C (OLED) =====
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(800000);
  
  Serial.print("初始化OLED (800kHz)... ");
  if(oled.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("✅ 成功");
    oledOK = true;
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 0);
    oled.printf("Gain: %.1fx", MIC_GAIN);
    oled.setCursor(0, 10);
    oled.println("Initializing...");
    oled.display();
  } else {
    Serial.println("❌ 失敗");
  }
  
  // ===== 初始化SPI (TFT) =====
  SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);
  SPI.setFrequency(80000000);
  
  Serial.print("初始化TFT LCD (80MHz SPI)... ");
  tft.begin(80000000);
  tft.setRotation(3);
  tft.fillScreen(COLOR_BG);
  tftOK = true;
  Serial.println("✅ 成功");
  
  // TFT歡迎畫面
  tft.setTextColor(COLOR_TITLE);
  tft.setTextSize(3);
  tft.setCursor(20, 50);
  tft.println("HIGH SPEED");
  
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(2);
  tft.setCursor(30, 90);
  tft.println("MIC Monitor System");
  
  tft.setTextSize(2);
  tft.setCursor(80, 130);
  tft.printf("Gain: %.1fx", MIC_GAIN);
  
  delay(1500);
  
  // ===== 初始化I2S麥克風 =====
  Serial.print("初始化麥克風... ");
  if(initI2S()) {
    Serial.println("✅ 成功");
    
    if(oledOK) {
      oled.clearDisplay();
      oled.setTextSize(2);
      oled.setCursor(10, 10);
      oled.println("MIC Ready");
      oled.setTextSize(1);
      oled.setCursor(10, 35);
      oled.printf("Gain: %.1fx", MIC_GAIN);
      oled.display();
    }
    
    if(tftOK) {
      tft.fillScreen(COLOR_BG);
      tft.setTextColor(COLOR_TITLE);
      tft.setTextSize(3);
      tft.setCursor(60, 80);
      tft.println("MIC Ready!");
      tft.setTextColor(COLOR_TEXT);
      tft.setTextSize(2);
      tft.setCursor(50, 130);
      tft.println("Speak to test...");
    }
    
    delay(1500);
    
    if(tftOK) {
      drawTFTLayout();
    }
    
  } else {
    Serial.println("❌ 失敗");
    if(tftOK) {
      tft.fillScreen(COLOR_BG);
      tft.setTextColor(COLOR_BAR_MAX);
      tft.setTextSize(2);
      tft.setCursor(40, 100);
      tft.println("MIC INIT FAILED!");
    }
    while(1) delay(1000);
  }
  
  Serial.println("\n開始高速監測...");
  Serial.println("════════════════════════════════════════\n");
  lastFrameTime = millis();
}

void loop() {
  size_t bytesRead = 0;
  
  esp_err_t result = i2s_read(I2S_PORT, samples, BUFFER_SIZE * sizeof(int32_t), 
                              &bytesRead, pdMS_TO_TICKS(10));
  
  if (result == ESP_OK && bytesRead > 0) {
    int samplesRead = bytesRead / sizeof(int32_t);
    
    analyzeAudio(samplesRead);
    prepareDisplayData(samplesRead);
    
    static uint8_t updateCounter = 0;
    updateCounter++;
    
    if(tftOK) {
      updateTFT();
    }
    
    if(oledOK && (updateCounter % 2 == 0)) {
      updateOLED();
    }
    
    frameCount++;
    unsigned long currentTime = millis();
    if(currentTime - lastFrameTime >= 1000) {
      fps = frameCount * 1000.0 / (currentTime - lastFrameTime);
      frameCount = 0;
      lastFrameTime = currentTime;
    }
    
    printAudioStats();
  }
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
    .dma_buf_len = 512,
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
  if (err != ESP_OK) return false;
  
  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) return false;
  
  i2s_zero_dma_buffer(I2S_PORT);
  delay(500);
  
  return true;
}

// ===== 🎚️ 音訊分析 (增益在這裡套用!) =====
void analyzeAudio(int samplesRead) {
  int64_t sum = 0;
  int32_t maxSample = 0;
  peakValue = 0;
  
  for(int i = 0; i < samplesRead; i++) {
    // 取得原始樣本
    int32_t sample = samples[i] >> 8;
    
    // 🎚️ 套用增益
    sample = (int32_t)(sample * MIC_GAIN);
    
    // 防止溢位
    if(sample > 8388607) sample = 8388607;
    if(sample < -8388608) sample = -8388608;
    
    int32_t absSample = abs(sample);
    sum += absSample;
    
    if(absSample > maxSample) {
      maxSample = absSample;
      peakValue = sample;
    }
  }
  
  avgVolume = (float)sum / samplesRead;
  currentVolume = (float)maxSample / 8388608.0 * 100.0;
  
  // 限制音量顯示在100%
  if(currentVolume > 100.0) currentVolume = 100.0;
  
  if(currentVolume > maxVolume) {
    maxVolume = currentVolume;
  }
}

// ===== 📊 波形數據準備 (波形增益在這裡!) =====
void prepareDisplayData(int samplesRead) {
  // OLED波形數據
  int step = samplesRead / DISPLAY_SAMPLES;
  if(step < 1) step = 1;
  
  for(int i = 0; i < DISPLAY_SAMPLES && i * step < samplesRead; i++) {
    int32_t sample = samples[i * step] >> 8;
    
    // 🎚️ 套用增益
    sample = (int32_t)(sample * MIC_GAIN * WAVE_GAIN);
    
    float normalized = (float)sample / 8388608.0;
    displayBuffer[i] = (int16_t)(normalized * 12.0);
    displayBuffer[i] = constrain(displayBuffer[i], -12, 12);
  }
  
  // TFT波形數據
  step = samplesRead / TFT_SAMPLES;
  if(step < 1) step = 1;
  
  for(int i = 0; i < TFT_SAMPLES && i * step < samplesRead; i++) {
    int32_t sample = samples[i * step] >> 8;
    
    // 🎚️ 套用增益
    sample = (int32_t)(sample * MIC_GAIN * WAVE_GAIN);
    
    float normalized = (float)sample / 8388608.0;
    tftBuffer[i] = (int16_t)(normalized * 60.0);
    tftBuffer[i] = constrain(tftBuffer[i], -60, 60);
  }
}

void updateOLED() {
  oled.clearDisplay();
  
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.printf("%.0fFPS x%.1f", fps, MIC_GAIN);
  
  oled.setCursor(0, 12);
  oled.printf("Vol:%3.0f%%", currentVolume);
  
  oled.setCursor(70, 12);
  oled.printf("Max:%3.0f%%", maxVolume);
  
  oled.drawRect(0, 24, 128, 8, SSD1306_WHITE);
  int barWidth = (int)(currentVolume * 1.26);
  if(barWidth > 126) barWidth = 126;
  if(barWidth > 0) {
    oled.fillRect(1, 25, barWidth, 6, SSD1306_WHITE);
  }
  
  int centerY = 48;
  for(int i = 0; i < DISPLAY_SAMPLES - 1; i++) {
    int x1 = i * 2;
    int x2 = (i + 1) * 2;
    int y1 = constrain(centerY - displayBuffer[i], 34, 62);
    int y2 = constrain(centerY - displayBuffer[i + 1], 34, 62);
    oled.drawLine(x1, y1, x2, y2, SSD1306_WHITE);
  }
  
  oled.display();
}

void drawTFTLayout() {
  tft.fillScreen(COLOR_BG);
  
  tft.setTextColor(COLOR_TITLE);
  tft.setTextSize(2);
  tft.setCursor(40, 5);
  tft.printf("MIC x%.1f", MIC_GAIN);
  
  tft.drawFastHLine(0, 25, 320, COLOR_GRID);
  
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(1);
  tft.setCursor(10, 35);
  tft.println("VOLUME:");
  
  tft.setCursor(10, 70);
  tft.println("MAX:");
  
  tft.setCursor(220, 5);
  tft.println("FPS:");
  
  tft.drawRect(5, 105, 310, 130, COLOR_GRID);
  
  for(int y = 120; y <= 220; y += 20) {
    for(int x = 20; x < 310; x += 10) {
      tft.drawPixel(x, y, COLOR_GRID);
    }
  }
  
  tft.drawFastHLine(5, 170, 310, COLOR_GRID);
}

void updateTFT() {
  tft.fillRect(80, 30, 130, 60, COLOR_BG);
  tft.fillRect(250, 5, 60, 15, COLOR_BG);
  
  tft.setTextColor(COLOR_TITLE);
  tft.setTextSize(1);
  tft.setCursor(250, 5);
  tft.printf("%.0f", fps);
  
  tft.setTextSize(3);
  uint16_t volColor = COLOR_BAR;
  if(currentVolume > 80) volColor = COLOR_BAR_MAX;
  else if(currentVolume > 50) volColor = COLOR_BAR_HIGH;
  
  tft.setTextColor(volColor);
  tft.setCursor(80, 30);
  tft.printf("%3.0f%%", currentVolume);
  
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(2);
  tft.setCursor(80, 65);
  tft.printf("%3.0f%%", maxVolume);
  
  int barWidth = (int)(currentVolume * 2.2);
  if(barWidth > 220) barWidth = 220;
  
  tft.fillRect(80, 50, 220, 10, COLOR_BG);
  tft.drawRect(80, 50, 220, 10, COLOR_GRID);
  
  if(barWidth > 0) {
    uint16_t barColor = COLOR_BAR;
    if(currentVolume > 80) barColor = COLOR_BAR_MAX;
    else if(currentVolume > 50) barColor = COLOR_BAR_HIGH;
    
    tft.fillRect(81, 51, barWidth-2, 8, barColor);
  }
  
  tft.fillRect(6, 106, 308, 128, COLOR_BG);
  
  for(int y = 120; y <= 220; y += 20) {
    tft.drawFastHLine(6, y, 308, COLOR_GRID);
  }
  tft.drawFastHLine(6, 170, 308, COLOR_GRID);
  
  int centerY = 170;
  for(int i = 0; i < TFT_SAMPLES - 1; i++) {
    int x1 = 8 + i * 2.5;
    int x2 = 8 + (i + 1) * 2.5;
    int y1 = constrain(centerY - tftBuffer[i], 107, 233);
    int y2 = constrain(centerY - tftBuffer[i + 1], 107, 233);
    
    tft.drawLine(x1, y1, x2, y2, COLOR_WAVE);
  }
}

void printAudioStats() {
  static unsigned long lastPrint = 0;
  if(millis() - lastPrint > 1000) {
    Serial.printf("🎤 FPS:%.1f | Gain:%.1fx | 音量:%5.1f%% | 最大:%5.1f%%\n", 
                  fps, MIC_GAIN, currentVolume, maxVolume);
    lastPrint = millis();
  }
}