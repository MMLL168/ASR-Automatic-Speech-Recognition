/*
 * ESP32-S3 Audio Recording System (提示音音量調整版)
 * 
 * 硬體接線:
 * === INMP441麥克風 ===
 * WS  -> GPIO 4
 * SCK -> GPIO 5
 * SD  -> GPIO 6
 * VDD -> 3.3V
 * GND -> GND
 * L/R -> GND
 * 
 * === MAX98357A 喇叭 ===
 * DIN  -> GPIO 7
 * BCLK -> GPIO 15
 * LRC  -> GPIO 16
 * 
 * === OLED (SSD1306) ===
 * SDA -> GPIO 41
 * SCL -> GPIO 42
 * 
 * === TFT (ILI9341) ===
 * CS   -> GPIO 45
 * DC   -> GPIO 47
 * RST  -> GPIO 21
 * MOSI -> GPIO 20
 * SCK  -> GPIO 19
 * BL   -> GPIO 38
 */

#include <driver/i2s.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_ILI9341.h>
#include <math.h>

// ===== 硬體定義 =====
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C
#define I2C_SDA 41
#define I2C_SCL 42

#define TFT_CS   45
#define TFT_DC   47
#define TFT_RST  21
#define TFT_MOSI 20
#define TFT_SCK  19
#define TFT_BL   38

// ===== 麥克風腳位 =====
#define MIC_WS   4
#define MIC_SCK  5
#define MIC_SD   6

// ===== 喇叭腳位 =====
#define SPK_DIN   7
#define SPK_BCLK  15
#define SPK_LRC   16

// ===== 音訊設定 =====
#define I2S_MIC_PORT   I2S_NUM_1
#define I2S_SPK_PORT   I2S_NUM_0
#define SAMPLE_RATE    16000
#define BUFFER_SIZE    512
#define MAX_REC_SEC    10

// ===== 音量設定 =====
#define BEEP_VOLUME    0.1   // 提示音音量 (0.1 = 10%, 原本是用 sys.volume)
#define MUSIC_VOLUME   0.3   // 音樂音量

// TFT 顏色
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_RED     0xF800
#define C_GREEN   0x07E0
#define C_CYAN    0x07FF
#define C_YELLOW  0xFFE0
#define C_GRAY    0x4208

// ===== 全域物件 =====
Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCK, TFT_RST);

// ===== 系統狀態 =====
struct SystemState {
  bool oledOK = false;
  bool tftOK = false;
  bool micOK = false;
  bool spkOK = false;
  bool psramOK = false;
  bool isRecording = false;
  bool isPlaying = false;
  bool hasRecording = false;
  float volume = 0.3;      // 播放音量
  float beepVolume = 0.1;  // 提示音音量 (可調整)
  float tempo = 1.0;
  float micGain = 2.0;
} sys;

// ===== 錄音緩衝 =====
int16_t *recBuffer = NULL;
int recSamples = 0;

// ===== 音樂 =====
struct MusicNote { String name; int freq; int dur; };
MusicNote star[] = {
  {"C4", 262, 500}, {"C4", 262, 500}, {"G4", 392, 500}, {"G4", 392, 500},
  {"A4", 440, 500}, {"A4", 440, 500}, {"G4", 392, 1000},
  {"F4", 349, 500}, {"F4", 349, 500}, {"E4", 330, 500}, {"E4", 330, 500},
  {"D4", 294, 500}, {"D4", 294, 500}, {"C4", 262, 1000}
};

// ===================================
// 初始化
// ===================================

void setup() {
  Serial.begin(115200);
  delay(1500);
  
  Serial.println("\n+===================================+");
  Serial.println("|  ESP32-S3 Audio System            |");
  Serial.println("+===================================+\n");
  
  Serial.println("[PIN] Wiring:");
  Serial.println("  MIC: WS=4, SCK=5, SD=6");
  Serial.println("  SPK: DIN=7, BCLK=15, LRC=16\n");
  
  sys.psramOK = psramFound();
  if(sys.psramOK) {
    Serial.printf("[OK] PSRAM: %d MB\n\n", ESP.getPsramSize()/(1024*1024));
  }
  
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(800000);
  
  Serial.print("OLED... ");
  sys.oledOK = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  Serial.println(sys.oledOK ? "OK" : "FAIL");
  
  if(sys.oledOK) {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 0);
    oled.println("Audio System");
    oled.setCursor(0, 15);
    oled.println("Initializing...");
    oled.display();
  }
  
  Serial.print("TFT... ");
  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(C_BLACK);
  tft.setSPISpeed(80000000);
  sys.tftOK = true;
  Serial.println("OK (80MHz)");
  
  showWelcome();
  delay(2000);
  
  Serial.print("MIC... ");
  sys.micOK = initMic();
  Serial.println(sys.micOK ? "OK" : "FAIL");
  
  Serial.print("SPK... ");
  sys.spkOK = initSpk();
  Serial.println(sys.spkOK ? "OK" : "FAIL");
  
  if(sys.micOK) {
    Serial.println("\n✅ Microphone ready!");
    Serial.println("   Try: test mic\n");
  } else {
    Serial.println("\n❌ Microphone failed!\n");
  }
  
  Serial.printf("[VOLUME] Beep: %d%% | Playback: %d%%\n\n", 
                (int)(sys.beepVolume*100), (int)(sys.volume*100));
  
  delay(1000);
  printHelp();
  showIdle();
}

void loop() {
  if(Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    processCmd(cmd);
  }
  delay(10);
}

// ===================================
// 硬體初始化
// ===================================

bool initMic() {
  i2s_config_t cfg = {
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
  
  i2s_pin_config_t pin = {
    .bck_io_num = MIC_SCK,
    .ws_io_num = MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = MIC_SD
  };
  
  if(i2s_driver_install(I2S_MIC_PORT, &cfg, 0, NULL) != ESP_OK) return false;
  if(i2s_set_pin(I2S_MIC_PORT, &pin) != ESP_OK) return false;
  
  i2s_zero_dma_buffer(I2S_MIC_PORT);
  delay(500);
  
  return true;
}

bool initSpk() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };
  
  i2s_pin_config_t pin = {
    .bck_io_num = SPK_BCLK,
    .ws_io_num = SPK_LRC,
    .data_out_num = SPK_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  
  if(i2s_driver_install(I2S_SPK_PORT, &cfg, 0, NULL) != ESP_OK) return false;
  if(i2s_set_pin(I2S_SPK_PORT, &pin) != ESP_OK) return false;
  i2s_zero_dma_buffer(I2S_SPK_PORT);
  return true;
}

// ===================================
// 麥克風測試
// ===================================

void testMicrophone(int duration = 3) {
  if(!sys.micOK) {
    Serial.println("[ERROR] Mic not ready!");
    return;
  }
  
  Serial.println("\n+===================================+");
  Serial.println("|   Microphone Test                 |");
  Serial.println("+===================================+");
  Serial.printf("\nGain: %.1fx | Duration: %d sec\n\n", sys.micGain, duration);
  
  if(sys.tftOK) {
    tft.fillScreen(C_BLACK);
    tft.setTextColor(C_CYAN);
    tft.setTextSize(2);
    tft.setCursor(50, 10);
    tft.println("MIC TEST");
    tft.drawFastHLine(0, 40, 320, C_WHITE);
  }
  
  int32_t buf[BUFFER_SIZE];
  size_t bytesRead;
  int totalSamples = SAMPLE_RATE * duration;
  int processed = 0;
  
  float rmsSum = 0;
  int rmsCount = 0;
  
  while(processed < totalSamples) {
    i2s_read(I2S_MIC_PORT, buf, BUFFER_SIZE * sizeof(int32_t), &bytesRead, portMAX_DELAY);
    int samples = bytesRead / sizeof(int32_t);
    
    long long sumSquares = 0;
    int16_t minVal = 32767;
    int16_t maxVal = -32768;
    
    for(int i = 0; i < samples && processed < totalSamples; i++) {
      int32_t raw = buf[i] >> 8;
      float scaled = raw * sys.micGain;
      int16_t val = (int16_t)(scaled / 256.0);
      
      if(val < minVal) minVal = val;
      if(val > maxVal) maxVal = val;
      sumSquares += (long long)val * val;
      processed++;
    }
    
    float bufRMS = sqrt((float)sumSquares / samples);
    rmsSum += bufRMS;
    rmsCount++;
    
    int elapsed = processed / SAMPLE_RATE;
    int level = map(bufRMS, 0, 10000, 0, 100);
    if(level > 100) level = 100;
    
    Serial.printf("[MIC] %d/%d sec | Level: %3d%% | RMS: %.0f | Range: %d~%d\n",
                  elapsed, duration, level, bufRMS, minVal, maxVal);
    
    // TFT 視覺化
    if(sys.tftOK) {
      static int lastY = 50;
      int y = 50 + elapsed * 30;
      if(y != lastY) {
        lastY = y;
        tft.setCursor(10, y);
        tft.setTextSize(1);
        tft.setTextColor(C_WHITE);
        tft.printf("Sec %d:", elapsed);
      }
      
      int barWidth = map(level, 0, 100, 0, 200);
      uint16_t color = C_GREEN;
      if(level > 80) color = C_RED;
      else if(level > 60) color = C_YELLOW;
      
      tft.fillRect(80, y, 200, 15, C_BLACK);
      if(barWidth > 0) {
        tft.fillRect(80, y, barWidth, 15, color);
      }
      tft.drawRect(80, y, 200, 15, C_GRAY);
    }
    
    // OLED
    if(sys.oledOK) {
      oled.clearDisplay();
      oled.setTextSize(1);
      oled.setCursor(0, 0);
      oled.println("MIC TEST");
      oled.setCursor(0, 15);
      oled.printf("Level: %d%%", level);
      oled.setCursor(0, 30);
      oled.printf("RMS: %.0f", bufRMS);
      
      int barLen = map(level, 0, 100, 0, 120);
      oled.drawRect(0, 50, 128, 12, SSD1306_WHITE);
      if(barLen > 0) {
        oled.fillRect(2, 52, barLen, 8, SSD1306_WHITE);
      }
      oled.display();
    }
  }
  
  float avgRMS = rmsSum / rmsCount;
  
  Serial.println("\n+===================================+");
  Serial.println("|   Test Results                    |");
  Serial.println("+===================================+");
  Serial.printf("| Avg RMS:  %6.1f                |\n", avgRMS);
  Serial.printf("| Gain:     %.1fx                   |\n", sys.micGain);
  Serial.println("+===================================+");
  
  if(avgRMS < 50) {
    Serial.println("\n⚠️  Signal weak, try: gain 5\n");
  } else if(avgRMS > 20000) {
    Serial.println("\n⚠️  Too loud, try: gain 0.5\n");
  } else {
    Serial.println("\n✅ Signal good!\n");
  }
  
  delay(2000);
  showIdle();
}

// ===================================
// 錄音功能
// ===================================

void recordAudio(int duration) {
  if(!sys.micOK) {
    Serial.println("[ERROR] Mic not ready!");
    return;
  }
  
  int maxDuration = sys.psramOK ? MAX_REC_SEC : 3;
  if(duration > maxDuration) {
    Serial.printf("[ERROR] Max %d seconds\n", maxDuration);
    return;
  }
  
  Serial.println("\n+===================================+");
  Serial.println("|   Recording                       |");
  Serial.println("+===================================+");
  Serial.printf("\nDuration: %d sec | Gain: %.1fx\n\n", duration, sys.micGain);
  
  recSamples = SAMPLE_RATE * duration;
  int bytesNeeded = recSamples * sizeof(int16_t);
  
  if(recBuffer) {
    free(recBuffer);
    recBuffer = NULL;
  }
  
  if(sys.psramOK) {
    recBuffer = (int16_t*)ps_malloc(bytesNeeded);
  }
  
  if(!recBuffer) {
    recBuffer = (int16_t*)malloc(bytesNeeded);
  }
  
  if(!recBuffer) {
    Serial.println("[ERROR] Memory allocation failed!");
    return;
  }
  
  Serial.println("[MEM] Buffer allocated");
  
  // 倒數
  for(int i = 3; i > 0; i--) {
    Serial.printf("[COUNTDOWN] %d...\n", i);
    if(sys.tftOK) {
      tft.fillScreen(C_BLACK);
      tft.setTextColor(C_RED);
      tft.setTextSize(10);
      tft.setCursor(120, 100);
      tft.println(i);
    }
    delay(1000);
  }
  
  Serial.println("\n[REC] Recording started!\n");
  
  // 開始提示音 (降低音量)
  if(sys.spkOK) {
    playBeep(1000, 100);
    delay(50);
    playBeep(1000, 100);
    delay(200);
  }
  
  // 錄音
  sys.isRecording = true;
  int32_t buf[BUFFER_SIZE];
  size_t bytesRead;
  int recorded = 0;
  
  while(recorded < recSamples) {
    i2s_read(I2S_MIC_PORT, buf, BUFFER_SIZE * sizeof(int32_t), &bytesRead, portMAX_DELAY);
    int samples = bytesRead / sizeof(int32_t);
    
    for(int i = 0; i < samples && recorded < recSamples; i++) {
      int32_t raw = buf[i] >> 8;
      float scaled = raw * sys.micGain;
      recBuffer[recorded++] = (int16_t)(scaled / 256.0);
    }
    
    int elapsed = recorded / SAMPLE_RATE;
    static int lastSec = -1;
    if(elapsed != lastSec) {
      lastSec = elapsed;
      Serial.printf("[REC] %d/%d sec\n", elapsed, duration);
    }
  }
  
  sys.isRecording = false;
  sys.hasRecording = true;
  
  // 結束提示音 (降低音量)
  if(sys.spkOK) {
    playBeep(800, 100);
    delay(50);
    playBeep(1200, 100);
  }
  
  Serial.println("\n[OK] Recording complete!");
  Serial.println("Type 'playback' to play\n");
  
  showComplete("Recording");
  showIdle();
}

void playbackRecording() {
  if(!sys.spkOK || !sys.hasRecording || !recBuffer) {
    Serial.println("[ERROR] No recording or speaker unavailable!");
    return;
  }
  
  Serial.println("\n+===================================+");
  Serial.println("|   Playback                        |");
  Serial.println("+===================================+\n");
  
  sys.isPlaying = true;
  int16_t stereo[BUFFER_SIZE * 2];
  size_t written;
  int played = 0;
  int total = recSamples / SAMPLE_RATE;
  
  while(played < recSamples) {
    int toPlay = min(BUFFER_SIZE, recSamples - played);
    
    for(int i = 0; i < toPlay; i++) {
      int16_t s = (int16_t)(recBuffer[played + i] * sys.volume * 2.0);
      stereo[i * 2] = s;
      stereo[i * 2 + 1] = s;
    }
    
    i2s_write(I2S_SPK_PORT, stereo, toPlay * 4, &written, portMAX_DELAY);
    played += toPlay;
    
    int elapsed = played / SAMPLE_RATE;
    static int lastSec = -1;
    if(elapsed != lastSec) {
      lastSec = elapsed;
      Serial.printf("[PLAY] %d/%d sec\n", elapsed, total);
    }
  }
  
  sys.isPlaying = false;
  Serial.println("\n[OK] Playback complete!\n");
  
  showComplete("Playback");
  showIdle();
}

// ===================================
// 顯示函數
// ===================================

void showWelcome() {
  if(!sys.tftOK) return;
  tft.fillScreen(C_BLACK);
  tft.setTextColor(C_CYAN);
  tft.setTextSize(2);
  tft.setCursor(30, 50);
  tft.println("Audio System");
  tft.setTextColor(C_GREEN);
  tft.setCursor(80, 90);
  tft.println("Ready!");
  tft.setTextColor(C_YELLOW);
  tft.setTextSize(1);
  tft.setCursor(60, 130);
  tft.println("WS=4 SCK=5 SD=6");
}

void showIdle() {
  if(sys.oledOK) {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.printf("Gain:%.1fx", sys.micGain);
    oled.setCursor(0, 15);
    oled.printf("Vol:%d%% B:%d%%", (int)(sys.volume*100), (int)(sys.beepVolume*100));
    oled.setCursor(0, 30);
    if(sys.hasRecording) {
      oled.printf("REC:%.1fs", (float)recSamples/SAMPLE_RATE);
    } else {
      oled.println("Ready");
    }
    oled.display();
  }
  
  if(sys.tftOK) {
    tft.fillScreen(C_BLACK);
    tft.setTextColor(C_CYAN);
    tft.setTextSize(2);
    tft.setCursor(50, 10);
    tft.println("System Ready");
    
    tft.setTextColor(sys.micOK ? C_GREEN : C_RED);
    tft.setCursor(40, 60);
    tft.print("MIC");
    
    tft.setTextColor(sys.spkOK ? C_GREEN : C_RED);
    tft.setCursor(140, 60);
    tft.print("SPK");
    
    tft.setTextColor(C_WHITE);
    tft.setTextSize(1);
    tft.setCursor(30, 100);
    tft.printf("Beep Vol: %d%%", (int)(sys.beepVolume*100));
    
    tft.setCursor(30, 120);
    tft.println("Commands:");
    tft.setCursor(30, 140);
    tft.println("test mic / record 5");
    tft.setCursor(30, 160);
    tft.println("playback / star");
  }
}

void showComplete(String msg) {
  if(sys.tftOK) {
    tft.fillScreen(C_BLACK);
    tft.setTextColor(C_GREEN);
    tft.setTextSize(3);
    tft.setCursor(60, 100);
    tft.println(msg);
    tft.setCursor(60, 140);
    tft.println("Complete!");
  }
  delay(1500);
}

// ===================================
// 音效函數 (使用獨立的提示音音量)
// ===================================

void playBeep(int freq, int ms) {
  if(!sys.spkOK) return;
  int samples = (SAMPLE_RATE * ms) / 1000;
  int16_t *buf = (int16_t*)malloc(BUFFER_SIZE * 4);
  if(!buf) return;
  int written = 0;
  while(written < samples) {
    int toWrite = min(BUFFER_SIZE, samples - written);
    for(int i = 0; i < toWrite; i++) {
      float t = (float)(written + i) / SAMPLE_RATE;
      // 使用 sys.beepVolume 而不是 sys.volume
      int16_t s = (int16_t)(sin(2 * PI * freq * t) * 32767 * sys.beepVolume);
      buf[i * 2] = s;
      buf[i * 2 + 1] = s;
    }
    size_t w;
    i2s_write(I2S_SPK_PORT, buf, toWrite * 4, &w, portMAX_DELAY);
    written += toWrite;
  }
  free(buf);
}

void playStar() {
  if(!sys.spkOK) return;
  Serial.println("\n[MUSIC] Twinkle Star...\n");
  
  // 音樂使用正常音量
  float oldBeepVol = sys.beepVolume;
  sys.beepVolume = MUSIC_VOLUME;  // 音樂用較大音量
  
  int count = sizeof(star) / sizeof(MusicNote);
  for(int i = 0; i < count; i++) {
    playBeep(star[i].freq, star[i].dur);
  }
  
  sys.beepVolume = oldBeepVol;  // 恢復提示音音量
  
  Serial.println("\n[OK] Complete!\n");
  showIdle();
}

// ===================================
// 命令處理
// ===================================

void processCmd(String cmd) {
  cmd.toLowerCase();
  cmd.trim();
  Serial.println("\n>>> " + cmd);
  
  if(cmd == "test mic" || cmd == "testmic") {
    testMicrophone(3);
  }
  else if(cmd.startsWith("gain ")) {
    float g = cmd.substring(5).toFloat();
    if(g >= 0.1 && g <= 10.0) {
      sys.micGain = g;
      Serial.printf("[OK] Mic Gain: %.1fx\n", sys.micGain);
      showIdle();
    }
  }
  else if(cmd.startsWith("record ")) {
    int dur = cmd.substring(7).toInt();
    int maxDur = sys.psramOK ? MAX_REC_SEC : 3;
    if(dur >= 1 && dur <= maxDur) {
      recordAudio(dur);
    }
  }
  else if(cmd == "playback") {
    playbackRecording();
  }
  else if(cmd == "star") {
    playStar();
  }
  else if(cmd.startsWith("vol ")) {
    int v = cmd.substring(4).toInt();
    if(v >= 0 && v <= 100) {
      sys.volume = v / 100.0;
      Serial.printf("[OK] Playback Volume: %d%%\n", v);
      showIdle();
    }
  }
  else if(cmd.startsWith("beep ")) {
    int v = cmd.substring(5).toInt();
    if(v >= 0 && v <= 100) {
      sys.beepVolume = v / 100.0;
      Serial.printf("[OK] Beep Volume: %d%%\n", v);
      showIdle();
      // 測試新音量
      if(sys.spkOK) {
        playBeep(1000, 100);
      }
    }
  }
  else if(cmd == "help") {
    printHelp();
  }
  else {
    Serial.println("[ERROR] Unknown command!");
  }
  Serial.println();
}

void printHelp() {
  Serial.println("\n+===================================+");
  Serial.println("|        Commands                   |");
  Serial.println("+===================================+");
  Serial.println("| test mic       - Test microphone  |");
  int maxRec = sys.psramOK ? MAX_REC_SEC : 3;
  Serial.printf("| record <1-%d>  - Record audio     |\n", maxRec);
  Serial.println("| playback       - Play recording   |");
  Serial.println("| star           - Test speaker     |");
  Serial.println("|                                   |");
  Serial.println("| === Volume Control ===            |");
  Serial.println("| gain <0.1-10>  - Mic gain         |");
  Serial.println("| vol <0-100>    - Playback volume  |");
  Serial.println("| beep <0-100>   - Beep volume      |");
  Serial.println("|                                   |");
  Serial.println("| help           - This help        |");
  Serial.println("+===================================+");
  Serial.printf("\n[CURRENT] Beep: %d%% | Play: %d%% | Gain: %.1fx\n\n",
                (int)(sys.beepVolume*100), (int)(sys.volume*100), sys.micGain);
}