/*
 * ESP32-S3 Edge Impulse Data Forwarder (Streaming Mode)
 * * 修改說明:
 * 1. 為了支援 Data Forwarder，改為「連續串流」模式。
 * 2. Baud Rate 提升至 921600 (傳送 16kHz 音訊必須)。
 * 3. 執行 edge-impulse-data-forwarder 時，頻率請手動輸入 16000。
 * * 硬體接線:
 * === INMP441麥克風 ===
 * WS  -> GPIO 4
 * SCK -> GPIO 5
 * SD  -> GPIO 6
 * VDD -> 3.3V
 * GND -> GND
 * L/R -> GND
 * * === OLED / TFT === (維持原腳位不變)
 */

#include <driver/i2s.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_ILI9341.h>

// ===== Edge Impulse 設定 =====
// 注意: Data Forwarder 透過 Serial 傳送 16kHz 會有極限
// 建議 Baud Rate 設為 921600 或 2000000
#define SERIAL_BAUD_RATE    921600 
#define EI_SAMPLE_RATE      16000   

// ===== 硬體定義 (維持不變) =====
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

// ===== I2S 設定 =====
#define I2S_MIC_PORT   I2S_NUM_1
#define DMA_BUF_LEN    256      // 縮小 Buffer 以降低延遲
#define DMA_BUF_COUNT  4

// TFT 顏色
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_RED     0xF800
#define C_GREEN   0x07E0
#define C_CYAN    0x07FF

// ===== 全域物件 =====
Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCK, TFT_RST);

// ===== 系統狀態 =====
struct SystemState {
  bool oledOK = false;
  bool tftOK = false;
  bool micOK = false;
  float micGain = 5.0; // 預設增益調高，方便觀察
} sys;

// ===================================
// 初始化
// ===================================

void setup() {
  // 1. 提升通訊速度
  Serial.begin(SERIAL_BAUD_RATE);
  
  // 顯示器與周邊初始化 (維持您的原邏輯)
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  
  if(oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    sys.oledOK = true;
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0,0); oled.println("Edge Impulse");
    oled.setCursor(0,10); oled.println("Data Forwarder");
    oled.display();
  }
  
  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(C_BLACK);
  sys.tftOK = true;
  
  // 顯示歡迎畫面
  if(sys.tftOK) {
    tft.setTextColor(C_CYAN); tft.setTextSize(2);
    tft.setCursor(10, 50); tft.println("Data Forwarder");
    tft.setTextColor(C_GREEN); tft.setTextSize(1);
    tft.setCursor(10, 80); tft.println("Mode: Continuous Stream");
    tft.setCursor(10, 100); tft.printf("Baud: %d", SERIAL_BAUD_RATE);
  }

  // 初始化麥克風
  sys.micOK = initMic();
  if(!sys.micOK) {
    if(sys.oledOK) { oled.setCursor(0,30); oled.println("MIC ERROR"); oled.display(); }
    while(1) delay(100);
  }
}

// ===================================
// 主迴圈 (串流核心)
// ===================================

void loop() {
  // Data Forwarder 需要持續收到數據
  // 格式: value1\n (單聲道)
  
  int32_t buf[DMA_BUF_LEN];
  size_t bytesRead;
  
  // 從 I2S 讀取一塊數據
  i2s_read(I2S_MIC_PORT, buf, DMA_BUF_LEN * sizeof(int32_t), &bytesRead, 0); // 使用 0 不阻塞太久
  
  int samples = bytesRead / sizeof(int32_t);
  
  if (samples > 0) {
    for(int i = 0; i < samples; i++) {
      int32_t raw = buf[i] >> 8; // 24-bit 修正
      
      // 應用增益並轉為 int16 範圍
      int16_t val = (int16_t)((raw * sys.micGain) / 256.0);
      
      // === 關鍵輸出 ===
      // 直接輸出數值與換行，最精簡格式以節省頻寬
      Serial.println(val);
    }
  }
  
  // 更新 UI (降低頻率以免拖慢 Serial 傳輸)
  // 每處理約 100 次更新一次 UI，避免影響採樣率
  static int uiCounter = 0;
  if(++uiCounter > 200) {
    updateUI();
    uiCounter = 0;
  }
}

// ===================================
// 輔助函式
// ===================================

bool initMic() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = EI_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = DMA_BUF_COUNT,
    .dma_buf_len = DMA_BUF_LEN,
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
  return true;
}

void updateUI() {
  if(sys.oledOK) {
    oled.fillRect(0, 50, 128, 14, SSD1306_BLACK);
    oled.setCursor(0, 50);
    oled.print("Streaming...");
    // 做一個簡單的閃爍效果證明還活著
    static bool blink = false;
    if(blink) oled.print(".");
    blink = !blink;
    oled.display();
  }
}