/*
 * ESP32-S3 MAX98357A喇叭測試程式 (可調速度版)
 * 
 * 硬體接線:
 * === MAX98357A 喇叭 ===
 * DIN  (SD)  -> GPIO7   ⭐ I2S Data
 * BCLK (BCK) -> GPIO15  ⭐ I2S Bit Clock
 * LRC  (WS)  -> GPIO16  ⭐ I2S Left/Right Clock
 * VIN        -> 5V
 * GND        -> GND
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
 * 
 * === UART指令 ===
 * play <freq> <duration>  - 播放指定頻率和時長
 * play <note>             - 播放音符 (C4, D4, E4, F4, G4, A4, B4, C5...)
 * vol <0-100>             - 設定音量 (0-100%)
 * tempo <50-400>          - 設定速度 (50%-400%) ⚡ 新增!
 * eq <on/off>             - 開關等響度補償
 * star                    - 播放小星星 (兩句) ⭐
 * stop                    - 停止播放
 * test                    - 播放測試音階
 * beep                    - 播放提示音
 * help                    - 顯示指令說明
 */

#include <driver/i2s.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_ILI9341.h>
#include <math.h>

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

// ===== I2S喇叭設定 =====
#define SPK_DIN   7
#define SPK_BCLK  15
#define SPK_LRC   16
#define I2S_PORT  I2S_NUM_0

// ===== 音訊參數 =====
#define SAMPLE_RATE 44100
#define BUFFER_SIZE 1024

bool oledOK = false;
bool tftOK = false;
bool speakerOK = false;
bool isPlaying = false;
bool equalizerEnabled = true;
bool skipDisplayUpdate = false;

float volume = 0.3;
float tempo = 1.0;  // 🎵 新增: 速度倍率 (1.0 = 100% = 正常速度)
int currentFreq = 0;
String currentNote = "---";

// 音符頻率對照表
struct Note {
  String name;
  int freq;
};

Note noteTable[] = {
  {"C3", 131}, {"D3", 147}, {"E3", 165}, {"F3", 175}, {"G3", 196}, {"A3", 220}, {"B3", 247},
  {"C4", 262}, {"D4", 294}, {"E4", 330}, {"F4", 349}, {"G4", 392}, {"A4", 440}, {"B4", 494},
  {"C5", 523}, {"D5", 587}, {"E5", 659}, {"F5", 698}, {"G5", 784}, {"A5", 880}, {"B5", 988},
  {"C6", 1047}, {"D6", 1175}, {"E6", 1319}, {"F6", 1397}, {"G6", 1568}, {"A6", 1760}, {"B6", 1976}
};

// 🎵 小星星旋律定義 (基準時長,會根據tempo調整)
struct MusicNote {
  String name;
  int freq;
  int baseDuration;  // 基準時長 (ms)
};

MusicNote twinkleStar[] = {
  {"C4", 262, 500}, {"C4", 262, 500}, {"G4", 392, 500}, {"G4", 392, 500},
  {"A4", 440, 500}, {"A4", 440, 500}, {"G4", 392, 1000},
  {"F4", 349, 500}, {"F4", 349, 500}, {"E4", 330, 500}, {"E4", 330, 500},
  {"D4", 294, 500}, {"D4", 294, 500}, {"C4", 262, 1000}
};

// TFT顏色定義
#define COLOR_BG       0x0000
#define COLOR_TEXT     0xFFFF
#define COLOR_TITLE    0x07FF
#define COLOR_NOTE     0xFFE0
#define COLOR_FREQ     0x07E0
#define COLOR_VOL      0xF81F
#define COLOR_EQ       0x07E0
#define COLOR_TEMPO    0xFDA0
#define COLOR_STAR     0xFFE0
#define COLOR_GRID     0x4208

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║  ESP32-S3 喇叭音樂播放系統 ⭐        ║");
  Serial.println("║      (可調速度版)  🎵⚡              ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  Serial.println("📌 喇叭接線:");
  Serial.println("   DIN  (Data)  -> GPIO7");
  Serial.println("   BCLK (Clock) -> GPIO15");
  Serial.println("   LRC  (WS)    -> GPIO16\n");
  
  // ===== 初始化TFT背光 =====
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  Serial.println("✅ TFT背光已開啟");
  
  // ===== 初始化I2C (OLED) =====
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  
  Serial.print("初始化OLED... ");
  if(oled.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("✅ 成功");
    oledOK = true;
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 0);
    oled.println("Music System");
    oled.println("Variable Tempo!");
    oled.display();
  } else {
    Serial.println("❌ 失敗");
  }
  
  // ===== 初始化TFT LCD =====
  Serial.print("初始化TFT LCD... ");
  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(COLOR_BG);
  tft.setSPISpeed(80000000);
  tftOK = true;
  Serial.println("✅ 成功 (80MHz)");
  
  // TFT歡迎畫面
  tft.setTextColor(COLOR_TITLE);
  tft.setTextSize(3);
  tft.setCursor(50, 40);
  tft.println("Music Box");
  
  tft.setTextColor(COLOR_STAR);
  tft.setTextSize(2);
  tft.setCursor(80, 80);
  tft.println("Twinkle");
  tft.setCursor(60, 110);
  tft.println("Twinkle Star");
  
  tft.setTextColor(COLOR_TEMPO);
  tft.setTextSize(2);
  tft.setCursor(40, 145);
  tft.println("Variable Tempo!");
  
  delay(2000);
  
  // ===== 初始化I2S喇叭 =====
  Serial.print("初始化I2S喇叭... ");
  if(initI2S()) {
    Serial.println("✅ 成功");
    speakerOK = true;
    
    if(oledOK) {
      oled.clearDisplay();
      oled.setTextSize(2);
      oled.setCursor(5, 10);
      oled.println("Speaker");
      oled.setCursor(20, 30);
      oled.println("Ready!");
      oled.setTextSize(1);
      oled.setCursor(5, 50);
      oled.println("Tempo: 100%");
      oled.display();
    }
    
    if(tftOK) {
      tft.fillScreen(COLOR_BG);
      tft.setTextColor(COLOR_TITLE);
      tft.setTextSize(3);
      tft.setCursor(40, 70);
      tft.println("Speaker Ready!");
      tft.setTextColor(COLOR_TEXT);
      tft.setTextSize(2);
      tft.setCursor(20, 120);
      tft.println("Type 'star' to play");
      tft.setCursor(10, 145);
      tft.println("'tempo 200' = 2x speed");
    }
    
    delay(2000);
    
    if(tftOK) {
      drawTFTLayout();
    }
    
  } else {
    Serial.println("❌ 失敗");
    if(tftOK) {
      tft.fillScreen(COLOR_BG);
      tft.setTextColor(0xF800);
      tft.setTextSize(2);
      tft.setCursor(20, 100);
      tft.println("SPEAKER INIT FAILED!");
    }
    while(1) delay(1000);
  }
  
  printHelp();
  updateDisplay();
}

void loop() {
  if(Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    processCommand(cmd);
  }
  
  delay(10);
}

bool initI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };
  
  i2s_pin_config_t pin_config = {
    .bck_io_num = SPK_BCLK,
    .ws_io_num = SPK_LRC,
    .data_out_num = SPK_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  
  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("❌ I2S驅動安裝失敗: %d\n", err);
    return false;
  }
  
  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("❌ I2S腳位設定失敗: %d\n", err);
    return false;
  }
  
  Serial.println("   ✓ BCLK: GPIO15");
  Serial.println("   ✓ LRC:  GPIO16");
  Serial.println("   ✓ DIN:  GPIO7");
  
  i2s_zero_dma_buffer(I2S_PORT);
  
  return true;
}

float getEqualLoudnessGain(int frequency) {
  if(!equalizerEnabled) return 1.0;
  
  float freq = (float)frequency;
  float gain = 1.0;
  
  if(freq < 200) {
    gain = 2.5 - (freq / 200.0) * 1.2;
  }
  else if(freq < 500) {
    gain = 1.3 - (freq - 200) / 300.0 * 0.3;
  }
  else if(freq < 1000) {
    gain = 1.0 + (1000 - freq) / 500.0 * 0.3;
  }
  else if(freq < 4000) {
    gain = 1.0;
  }
  else if(freq < 8000) {
    gain = 1.0 + (freq - 4000) / 4000.0 * 0.2;
  }
  else {
    gain = 1.2 + (freq - 8000) / 8000.0 * 0.3;
    if(gain > 1.8) gain = 1.8;
  }
  
  return gain;
}

void processCommand(String cmd) {
  cmd.toLowerCase();
  
  Serial.println("\n>>> " + cmd);
  
  if(cmd.startsWith("play ")) {
    String param = cmd.substring(5);
    param.trim();
    
    bool isNote = false;
    for(int i = 0; i < sizeof(noteTable)/sizeof(Note); i++) {
      String noteName = noteTable[i].name;
      noteName.toLowerCase();
      if(param == noteName) {
        playTone(noteTable[i].freq, 1000);
        currentNote = noteTable[i].name;
        currentFreq = noteTable[i].freq;
        isNote = true;
        float eqGain = getEqualLoudnessGain(noteTable[i].freq);
        Serial.printf("🔊 播放音符: %s (%d Hz) [EQ: %.2fx]\n", 
                      noteTable[i].name.c_str(), noteTable[i].freq, eqGain);
        break;
      }
    }
    
    if(!isNote) {
      int spaceIndex = param.indexOf(' ');
      if(spaceIndex > 0) {
        int freq = param.substring(0, spaceIndex).toInt();
        int duration = param.substring(spaceIndex + 1).toInt();
        
        if(freq > 0 && duration > 0) {
          playTone(freq, duration);
          currentNote = "---";
          currentFreq = freq;
          float eqGain = getEqualLoudnessGain(freq);
          Serial.printf("🔊 播放: %d Hz, %d ms [EQ: %.2fx]\n", freq, duration, eqGain);
        } else {
          Serial.println("❌ 參數錯誤! 格式: play <freq> <duration>");
        }
      } else {
        Serial.println("❌ 參數錯誤! 格式: play <freq> <duration> 或 play <note>");
      }
    }
    
    updateDisplay();
  }
  else if(cmd.startsWith("vol ")) {
    int vol = cmd.substring(4).toInt();
    if(vol >= 0 && vol <= 100) {
      volume = vol / 100.0;
      Serial.printf("🔊 音量設定為: %d%%\n", vol);
      updateDisplay();
    } else {
      Serial.println("❌ 音量範圍: 0-100");
    }
  }
  else if(cmd.startsWith("tempo ")) {
    int tempoPercent = cmd.substring(6).toInt();
    if(tempoPercent >= 50 && tempoPercent <= 400) {
      tempo = tempoPercent / 100.0;
      Serial.printf("🎵 速度設定為: %d%% (%.2fx)\n", tempoPercent, tempo);
      
      // 顯示預期時長
      float expectedTime = 7.5 / tempo;  // 基準7.5秒
      Serial.printf("   預期播放時長: %.2f 秒\n", expectedTime);
      
      updateDisplay();
    } else {
      Serial.println("❌ 速度範圍: 50-400 (50%~400%)");
      Serial.println("   50  = 0.5x (慢速)");
      Serial.println("   100 = 1.0x (正常)");
      Serial.println("   200 = 2.0x (2倍速)");
      Serial.println("   400 = 4.0x (4倍速)");
    }
  }
  else if(cmd.startsWith("eq ")) {
    String param = cmd.substring(3);
    param.trim();
    if(param == "on") {
      equalizerEnabled = true;
      Serial.println("✅ 等響度補償: 開啟");
    } else if(param == "off") {
      equalizerEnabled = false;
      Serial.println("⚠️  等響度補償: 關閉");
    } else {
      Serial.println("❌ 參數錯誤! 使用: eq on 或 eq off");
    }
    updateDisplay();
  }
  else if(cmd == "star") {
    playTwinkleStar();
  }
  else if(cmd == "stop") {
    i2s_zero_dma_buffer(I2S_PORT);
    isPlaying = false;
    Serial.println("⏹️  停止播放");
    updateDisplay();
  }
  else if(cmd == "test") {
    Serial.println("🎵 播放測試音階...");
    int testFreqs[] = {262, 294, 330, 349, 392, 440, 494, 523};
    String testNotes[] = {"C4", "D4", "E4", "F4", "G4", "A4", "B4", "C5"};
    
    for(int i = 0; i < 8; i++) {
      currentNote = testNotes[i];
      currentFreq = testFreqs[i];
      float eqGain = getEqualLoudnessGain(testFreqs[i]);
      Serial.printf("  %s (%d Hz) [EQ: %.2fx]\n", testNotes[i].c_str(), testFreqs[i], eqGain);
      updateDisplay();
      playTone(testFreqs[i], 400);
      delay(100);
    }
    Serial.println("✅ 測試完成");
  }
  else if(cmd == "beep") {
    Serial.println("🔔 Beep!");
    playTone(1000, 100);
    delay(50);
    playTone(1000, 100);
  }
  else if(cmd == "help") {
    printHelp();
  }
  else {
    Serial.println("❌ 未知指令! 輸入 'help' 查看說明");
  }
  
  Serial.println();
}

// 🎵 播放小星星 (根據tempo調整速度)
void playTwinkleStar() {
  Serial.println("\n⭐ 播放小星星...\n");
  Serial.printf("🎵 當前速度: %.0f%% (%.2fx)\n", tempo * 100, tempo);
  Serial.println("♪ 一閃一閃亮晶晶");
  Serial.println("♪ 滿天都是小星星\n");
  
  if(tftOK) {
    tft.fillRect(0, 40, 320, 145, COLOR_BG);
    tft.setTextColor(COLOR_STAR);
    tft.setTextSize(3);
    tft.setCursor(40, 50);
    tft.println("Playing...");
    
    tft.setTextColor(COLOR_TEMPO);
    tft.setTextSize(2);
    tft.setCursor(60, 100);
    tft.printf("Tempo: %.0f%%", tempo * 100);
    
    tft.setTextColor(COLOR_TEXT);
    tft.setTextSize(1);
    tft.setCursor(80, 130);
    tft.println("Twinkle Star");
  }
  
  skipDisplayUpdate = true;
  
  int noteCount = sizeof(twinkleStar) / sizeof(MusicNote);
  unsigned long startTime = millis();
  
  for(int i = 0; i < noteCount; i++) {
    currentNote = twinkleStar[i].name;
    currentFreq = twinkleStar[i].freq;
    
    // 🎵 根據tempo調整時長
    int adjustedDuration = (int)(twinkleStar[i].baseDuration / tempo);
    
    Serial.printf("  [%2d] %s (%4d Hz) - %d ms (原:%d ms)\n", 
                  i+1, currentNote.c_str(), currentFreq, 
                  adjustedDuration, twinkleStar[i].baseDuration);
    
    playTone(currentFreq, adjustedDuration);
  }
  
  unsigned long endTime = millis();
  float totalTime = (endTime - startTime) / 1000.0;
  
  Serial.printf("\n✅ 播放完成! ⭐ (實際時長: %.2f 秒)\n\n", totalTime);
  
  skipDisplayUpdate = false;
  
  currentNote = "---";
  currentFreq = 0;
  updateDisplay();
}

void playTone(int frequency, int duration_ms) {
  if(!speakerOK) return;
  
  isPlaying = true;
  
  float eqGain = getEqualLoudnessGain(frequency);
  float adjustedVolume = volume * eqGain;
  
  if(adjustedVolume > 1.0) adjustedVolume = 1.0;
  
  int samples = (SAMPLE_RATE * duration_ms) / 1000;
  int16_t *buffer = (int16_t*)malloc(BUFFER_SIZE * sizeof(int16_t) * 2);
  
  if(buffer == NULL) {
    Serial.println("❌ 記憶體分配失敗!");
    isPlaying = false;
    return;
  }
  
  int samplesWritten = 0;
  
  while(samplesWritten < samples) {
    int samplesToWrite = min(BUFFER_SIZE, samples - samplesWritten);
    
    for(int i = 0; i < samplesToWrite; i++) {
      float t = (float)(samplesWritten + i) / SAMPLE_RATE;
      float wave = sin(2.0 * PI * frequency * t);
      
      float envelope = 1.0;
      if(samplesWritten + i < SAMPLE_RATE / 20) {
        envelope = (float)(samplesWritten + i) / (SAMPLE_RATE / 20);
      } else if(samplesWritten + i > samples - SAMPLE_RATE / 20) {
        envelope = (float)(samples - samplesWritten - i) / (SAMPLE_RATE / 20);
      }
      
      int16_t sample = (int16_t)(wave * 32767 * adjustedVolume * envelope);
      
      buffer[i * 2] = sample;
      buffer[i * 2 + 1] = sample;
    }
    
    size_t bytesWritten;
    i2s_write(I2S_PORT, buffer, samplesToWrite * sizeof(int16_t) * 2, &bytesWritten, portMAX_DELAY);
    
    samplesWritten += samplesToWrite;
  }
  
  free(buffer);
  isPlaying = false;
}

void updateDisplay() {
  if(skipDisplayUpdate) return;
  
  // 更新OLED
  if(oledOK) {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 0);
    oled.print("SPK ");
    oled.print(equalizerEnabled ? "[EQ]" : "");
    
    if(isPlaying) {
      oled.setTextSize(2);
      oled.setCursor(30, 15);
      oled.println(currentNote);
      
      oled.setTextSize(1);
      oled.setCursor(20, 38);
      oled.printf("%d Hz", currentFreq);
    } else {
      oled.setTextSize(2);
      oled.setCursor(20, 20);
      oled.println("Ready");
    }
    
    oled.setTextSize(1);
    oled.setCursor(0, 48);
    oled.printf("Vol:%d%%", (int)(volume * 100));
    oled.setCursor(0, 56);
    oled.printf("Tempo:%.0f%%", tempo * 100);
    
    oled.display();
  }
  
  // 更新TFT
  if(tftOK) {
    tft.fillRect(0, 40, 320, 145, COLOR_BG);
    
    if(isPlaying) {
      tft.setTextColor(COLOR_NOTE);
      tft.setTextSize(6);
      tft.setCursor(80, 60);
      tft.println(currentNote);
      
      tft.setTextColor(COLOR_FREQ);
      tft.setTextSize(3);
      tft.setCursor(80, 130);
      tft.printf("%d Hz", currentFreq);
      
      if(equalizerEnabled) {
        float eqGain = getEqualLoudnessGain(currentFreq);
        tft.setTextSize(1);
        tft.setCursor(120, 165);
        tft.printf("EQ:%.2fx", eqGain);
      }
    } else {
      tft.setTextColor(COLOR_TEXT);
      tft.setTextSize(3);
      tft.setCursor(60, 90);
      tft.println("Waiting...");
    }
    
    // 底部資訊
    tft.fillRect(10, 190, 300, 50, COLOR_BG);
    
    // 音量
    tft.setTextColor(COLOR_VOL);
    tft.setTextSize(2);
    tft.setCursor(10, 195);
    tft.printf("Vol:%d%%", (int)(volume * 100));
    
    // Tempo
    tft.setTextColor(COLOR_TEMPO);
    tft.setCursor(140, 195);
    tft.printf("%.0f%%", tempo * 100);
    
    // EQ狀態
    tft.setTextColor(equalizerEnabled ? COLOR_EQ : COLOR_GRID);
    tft.setCursor(250, 195);
    tft.printf("EQ");
    
    // 音量條
    int barWidth = (int)(volume * 280);
    tft.drawRect(10, 220, 280, 15, COLOR_GRID);
    if(barWidth > 0) {
      tft.fillRect(11, 221, barWidth, 13, COLOR_VOL);
    }
  }
}

void drawTFTLayout() {
  tft.fillScreen(COLOR_BG);
  
  tft.setTextColor(COLOR_TITLE);
  tft.setTextSize(2);
  tft.setCursor(20, 10);
  tft.println("Music Box (Tempo Ctrl)");
  
  tft.drawFastHLine(0, 35, 320, COLOR_GRID);
  tft.drawFastHLine(0, 185, 320, COLOR_GRID);
  
  updateDisplay();
}

void printHelp() {
  Serial.println("\n╔══════════════════════════════════════════════╗");
  Serial.println("║      UART 指令說明 (可調速度) 🎵⚡          ║");
  Serial.println("╠══════════════════════════════════════════════╣");
  Serial.println("║ play <freq> <duration>                       ║");
  Serial.println("║   播放指定頻率(Hz)和時長(ms)                 ║");
  Serial.println("║   範例: play 440 1000                        ║");
  Serial.println("║                                              ║");
  Serial.println("║ play <note>                                  ║");
  Serial.println("║   播放音符 (C3~B6)                           ║");
  Serial.println("║   範例: play C4, play A4, play G5            ║");
  Serial.println("║                                              ║");
  Serial.println("║ vol <0-100>                                  ║");
  Serial.println("║   設定音量百分比                             ║");
  Serial.println("║   範例: vol 50                               ║");
  Serial.println("║                                              ║");
  Serial.println("║ tempo <50-400> ⚡ 新增!                     ║");
  Serial.println("║   設定播放速度 (50%-400%)                    ║");
  Serial.println("║   範例:                                      ║");
  Serial.println("║     tempo 50  -> 0.5x (慢速)                ║");
  Serial.println("║     tempo 100 -> 1.0x (正常)                ║");
  Serial.println("║     tempo 200 -> 2.0x (2倍速)               ║");
  Serial.println("║     tempo 400 -> 4.0x (4倍速)               ║");
  Serial.println("║                                              ║");
  Serial.println("║ eq <on/off>                                  ║");
  Serial.println("║   開關等響度補償                             ║");
  Serial.println("║   範例: eq on, eq off                        ║");
  Serial.println("║                                              ║");
  Serial.println("║ star ⭐                                      ║");
  Serial.println("║   播放小星星 (兩句)                          ║");
  Serial.println("║   會根據tempo自動調整速度!                   ║");
  Serial.println("║                                              ║");
  Serial.println("║ stop                                         ║");
  Serial.println("║   停止播放                                   ║");
  Serial.println("║                                              ║");
  Serial.println("║ test                                         ║");
  Serial.println("║   播放測試音階                               ║");
  Serial.println("║                                              ║");
  Serial.println("║ beep                                         ║");
  Serial.println("║   播放提示音                                 ║");
  Serial.println("║                                              ║");
  Serial.println("║ help                                         ║");
  Serial.println("║   顯示此說明                                 ║");
  Serial.println("╚══════════════════════════════════════════════╝");
  Serial.printf("\n當前設定:\n");
  Serial.printf("  音量: %d%%\n", (int)(volume * 100));
  Serial.printf("  速度: %.0f%% (%.2fx)\n", tempo * 100, tempo);
  Serial.printf("  等響度補償: %s\n", equalizerEnabled ? "開啟 ✅" : "關閉 ⚠️");
  Serial.println("\n準備就緒,請輸入指令...\n");
}