/*
 * ESP32-S3 AI Monitor (Debug Monitor Version)
 * 用途: 抓出為什麼 Device 跑的和 Web 不一樣
 */

#include <ESP32S3_MIC_Add_Error_inferencing.h> // ★ 確認 Library 名稱

#include <driver/i2s.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_ILI9341.h>
#include <DHT.h> 

// ===== 參數設定 =====
#define EI_SAMPLE_RATE       16000 
#define BEEP_VOLUME          0.05 
#define MIC_GAIN             5.0   

// 使用我們推測的 Data Forwarder 設定
#define AUDIO_DIVISOR        256.0f 

// 硬體腳位 (維持不變)
#define DHTPIN  1
#define DHTTYPE DHT11
#define PIN_RED   10
#define PIN_GREEN 11
#define PIN_BLUE  12
#define MIC_WS 4
#define MIC_SCK 5
#define MIC_SD 6
#define I2S_MIC_PORT I2S_NUM_1
#define SPK_DIN 7
#define SPK_BCLK 15
#define SPK_LRC 16
#define I2S_SPK_PORT I2S_NUM_0
#define OLED_ADDR 0x3C
#define I2C_SDA 41
#define I2C_SCL 42
#define TFT_CS 45
#define TFT_DC 47
#define TFT_RST 21
#define TFT_MOSI 20
#define TFT_SCK 19
#define TFT_BL 38

// 顏色定義
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_RED     0xF800
#define C_GREEN   0x07E0
#define C_BLUE    0x001F
#define C_CYAN    0x07FF

Adafruit_SSD1306 oled(128, 64, &Wire, -1);
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCK, TFT_RST);
DHT dht(DHTPIN, DHTTYPE);

enum DetectionState { STATE_UNKNOWN, STATE_BACKGROUND, STATE_FAN_ON, STATE_ERROR };
struct SystemState {
  DetectionState currentState = STATE_UNKNOWN;
  DetectionState lastState = STATE_UNKNOWN;
  float temp = 0.0; float hum = 0.0;
  float score_fan = 0.0; float score_background = 0.0; float score_error = 0.0; 
} sys;

float *ai_buffer = NULL;
int16_t *audio_out_buffer = NULL;

// 函式原型
void updateSensors();
bool initMic();
bool initSpk();
void record_audio_for_ai(float *out_buf);
void updateNumbers();
void setRGB(bool r, bool g, bool b);

void setup() {
  Serial.begin(115200);
  delay(1000); 
  Serial.println("\n=== DEBUG MODE STARTED ===");

  pinMode(PIN_RED, OUTPUT); pinMode(PIN_GREEN, OUTPUT); pinMode(PIN_BLUE, OUTPUT);
  dht.begin();

  audio_out_buffer = (int16_t*)malloc(512 * 4); 
  if(psramFound()) ai_buffer = (float*)ps_malloc(EI_CLASSIFIER_RAW_SAMPLE_COUNT * sizeof(float));
  else ai_buffer = (float*)malloc(EI_CLASSIFIER_RAW_SAMPLE_COUNT * sizeof(float));
  
  pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH);
  Wire.begin(I2C_SDA, I2C_SCL);
  oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  oled.clearDisplay(); oled.display();
  
  tft.begin(); tft.setRotation(2); tft.fillScreen(C_BLACK);

  initMic(); initSpk();
}

void loop() {
  updateSensors();
  
  // 1. 錄音
  record_audio_for_ai(ai_buffer);

  // 🔥🔥🔥【數據監控核心】🔥🔥🔥
  // 我們要看這一秒鐘錄到的聲音，最大聲到底是多少？
  float max_val = 0.0;
  float min_val = 0.0;
  for(int i=0; i<EI_CLASSIFIER_RAW_SAMPLE_COUNT; i++){
    if(ai_buffer[i] > max_val) max_val = ai_buffer[i];
    if(ai_buffer[i] < min_val) min_val = ai_buffer[i];
  }

  // 2. AI 推論
  signal_t signal;
  numpy::signal_from_buffer(ai_buffer, EI_CLASSIFIER_RAW_SAMPLE_COUNT, &signal);
  ei_impulse_result_t result = { 0 };
  run_classifier(&signal, &result, false);

  sys.score_fan = 0; sys.score_background = 0; sys.score_error = 0;
  for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
    String label = String(result.classification[ix].label);
    float value = result.classification[ix].value;
    if (label.equalsIgnoreCase("Fan")) sys.score_fan = value;
    else if (label.equalsIgnoreCase("Error") || label.equalsIgnoreCase("anomaly")) sys.score_error = value;
    else sys.score_background = value;
  }

  // 🔥 印出診斷資訊 (請截圖或複製這行給我看)
  Serial.printf("[DEBUG] Peak: %.0f / %.0f | AI -> Fan:%.2f Back:%.2f Err:%.2f\n", 
                min_val, max_val, sys.score_fan, sys.score_background, sys.score_error);

  updateNumbers();
}

// ===================================
// 模擬 Data Forwarder 的錄音函式
// ===================================
void record_audio_for_ai(float *out_buf) {
  size_t bytes_read; 
  int32_t i2s_buffer[512]; 
  int samples_read = 0;
  float gain = 5.0f;
  float divisor = 256.0f; 

  while (samples_read < EI_CLASSIFIER_RAW_SAMPLE_COUNT) {
    i2s_read(I2S_MIC_PORT, i2s_buffer, sizeof(i2s_buffer), &bytes_read, portMAX_DELAY);
    if (bytes_read == 0) continue;
    int samples_in_chunk = bytes_read / 4; 
    
    for (int i = 0; i < samples_in_chunk; i++) {
      if (samples_read < EI_CLASSIFIER_RAW_SAMPLE_COUNT) {
        int32_t raw = i2s_buffer[i] >> 8; 
        
        // 模擬 Data Forwarder 的 int16 強制轉型
        int32_t temp_calc = (int32_t)((raw * gain) / divisor);
        int16_t simulated_val = (int16_t)temp_calc; 
        
        out_buf[samples_read] = (float)simulated_val; 
        samples_read++;
      }
    }
  }
}

// (以下為簡化的硬體驅動，維持不變)
void updateSensors() { /* 簡化省略 */ }
bool initMic() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX), .sample_rate = EI_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT, .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S, .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4, .dma_buf_len = 512, .use_apll = false, .tx_desc_auto_clear = false, .fixed_mclk = 0
  };
  i2s_pin_config_t pin = { .bck_io_num = MIC_SCK, .ws_io_num = MIC_WS, .data_out_num = -1, .data_in_num = MIC_SD };
  i2s_driver_install(I2S_MIC_PORT, &cfg, 0, NULL); i2s_set_pin(I2S_MIC_PORT, &pin);
  return true;
}
bool initSpk() { return true; } // 暫時省略
void updateNumbers() { /* 暫時省略 */ }
void setRGB(bool r, bool g, bool b) { digitalWrite(PIN_RED, r); digitalWrite(PIN_GREEN, g); digitalWrite(PIN_BLUE, b); }
