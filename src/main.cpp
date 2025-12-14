#include <Arduino.h>
#include <micro_ros_platformio.h>
#include "secret.h"
#include <rcl/rcl.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>
#include <esp_log.h>
#include <rosidl_runtime_c/string_functions.h>

#include <std_msgs/msg/int32.h>
#include <std_msgs/msg/float32.h>
#include <sensor_msgs/msg/compressed_image.h>

#include "esp_camera.h"
#define CAMERA_MODEL_M5STACK_CAMS3_UNIT
#include "camera_pins.h"

#if !defined(MICRO_ROS_TRANSPORT_ARDUINO_WIFI)
#error This example is only avaliable for Arduino framework with wifi transport.
#endif

// 定数定義
static const char* TAG = "FWE_NODE";
const char* NODE_NAME = "fwe_node";
const char* INT32_TOPIC = "/fwe_topic";
const char* SPEED_TOPIC = "/ego_vehicle/speedometer";
const char* IMAGE_TOPIC = "/ego_vehicle/rgb_front/image_compressed";
const unsigned int TIMER_TIMEOUT_MS = 1000;

// グローバル変数
rcl_publisher_t publisher;
std_msgs__msg__Int32 msg;
rcl_publisher_t speed_publisher;
std_msgs__msg__Float32 speed_msg;
rcl_publisher_t image_publisher;
sensor_msgs__msg__CompressedImage image_msg;
rclc_executor_t executor;
rclc_support_t support;
rcl_allocator_t allocator;
rcl_node_t node;
rcl_timer_t timer;

// エラーハンドルループ
void error_loop(rcl_ret_t error_code, const char* function_name) {
  while(1) {
    ESP_LOGE(TAG, "エラーが発生しました。関数: %s, エラーコード: %d, リカバリーのためループしています。", function_name, error_code);
    delay(100);
  }
}

#define RCCHECK(fn) { rcl_ret_t temp_rc = fn; if((temp_rc != RCL_RET_OK)){error_loop(temp_rc, #fn);}}
#define RCSOFTCHECK(fn) { rcl_ret_t temp_rc = fn; if((temp_rc != RCL_RET_OK)){} }

// Int32タイマーコールバック
void timer_callback(rcl_timer_t * timer, int64_t last_call_time) {
  RCLC_UNUSED(last_call_time);
  if (timer != NULL) {
    // Int32メッセージをパブリッシュ
    RCSOFTCHECK(rcl_publish(&publisher, &msg, NULL));
    ESP_LOGD(TAG, "Int32メッセージをパブリッシュしました: %d", msg.data);
    msg.data++;
    
    // 速度データをパブリッシュ（テストデータ：0-120km/hの範囲で変動）
    static float speed_counter = 0.0;
    speed_msg.data = 30.0 + 20.0 * sin(speed_counter * 0.1); // 10-50km/hの範囲で変動
    speed_counter += 1.0;
    if (speed_counter > 62.8) speed_counter = 0.0; // 2π*10でリセット
    
    RCSOFTCHECK(rcl_publish(&speed_publisher, &speed_msg, NULL));
    ESP_LOGD(TAG, "速度メッセージをパブリッシュしました: %.2f km/h", speed_msg.data);
  }
}

// LEDフラッシュ初期化（必要な場合のみ）
void setupLedFlash(int pin) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

// カメラ初期化処理
bool init_camera() {
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
#ifdef CAMERA_MODEL_M5STACK_CAMS3_UNIT
  config.frame_size = FRAMESIZE_HD;  // HD画質 (1280x720) に変更
#else
  config.frame_size = FRAMESIZE_HD;  // HD画質 (1280x720) に変更
#endif
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 15;  // HD画質に合わせて品質を調整
  config.fb_count = 1;

  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 12;  // HD画質用に品質を調整
#ifdef CAMERA_MODEL_M5STACK_CAMS3_UNIT
      config.fb_count = 2;
      config.frame_size = FRAMESIZE_HD;  // HD画質に設定
#else
      config.fb_count = 2;
      config.frame_size = FRAMESIZE_HD;  // HD画質に設定
#endif
      config.grab_mode = CAMERA_GRAB_LATEST;
      ESP_LOGI(TAG, "PSRAMあり: JPEG品質12, HD画質 (1280x720)");
    } else {
      config.frame_size = FRAMESIZE_VGA;  // PSRAMなしの場合はVGA画質
      config.fb_location = CAMERA_FB_IN_DRAM;
      ESP_LOGI(TAG, "PSRAMなし: VGA画質 (640x480) - メモリ制限のため");
    }
  } else {
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
    ESP_LOGI(TAG, "RGB/顔認識用設定");
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "カメラの初期化に失敗しました。エラーコード: 0x%x", err);
    return false;
  }
  ESP_LOGI(TAG, "カメラの初期化が完了しました");

  sensor_t *s = esp_camera_sensor_get();
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
    ESP_LOGI(TAG, "OV3660センサーの設定を調整しました: vflip=1, brightness=1, saturation=-2");
  }
  if (config.pixel_format == PIXFORMAT_JPEG) {
#ifdef CAMERA_MODEL_M5STACK_CAMS3_UNIT
    s->set_framesize(s, FRAMESIZE_HD);  // 1280x720
    ESP_LOGI(TAG, "JPEGフォーマット: フレームサイズをHDに設定しました (1280x720)");
#else
    s->set_framesize(s, FRAMESIZE_HD);   // 1280x720
    ESP_LOGI(TAG, "JPEGフォーマット: フレームサイズをHDに設定しました (1280x720)");
#endif
  }
  return true;
}

// 画像publish処理
void publish_camera_image() {
  static uint32_t image_counter = 0;
  
  // 画像送信頻度を下げる（5秒に1回）
  if (image_counter % 5 != 0) {
    image_counter++;
    return;
  }
  image_counter++;
  
  camera_fb_t *fb = esp_camera_fb_get();
  if (fb && fb->format == PIXFORMAT_JPEG) {
    ESP_LOGI(TAG, "画像を取得しました: %dバイト", fb->len);
    
    // サイズチェック（HD画質用に100KB以下に制限）
    if (fb->len > 102400) {
      ESP_LOGW(TAG, "画像サイズが大きすぎます: %dバイト > 100KB、スキップします", fb->len);
      esp_camera_fb_return(fb);
      return;
    }
    
    image_msg.header.stamp.sec = (int32_t)(millis() / 1000);
    image_msg.header.stamp.nanosec = (int32_t)((millis() % 1000) * 1000000);
    image_msg.header.frame_id.data = (char*)"camera";
    image_msg.header.frame_id.size = strlen("camera");
    image_msg.header.frame_id.capacity = strlen("camera") + 1;
    image_msg.format.data = (char*)"jpeg";
    image_msg.format.size = strlen("jpeg");
    image_msg.format.capacity = strlen("jpeg") + 1;
    if (image_msg.data.capacity < fb->len) {
      if (image_msg.data.data) free(image_msg.data.data);
      image_msg.data.data = (uint8_t*)malloc(fb->len);
      image_msg.data.capacity = fb->len;
      ESP_LOGD(TAG, "画像バッファを再確保しました: %dバイト", fb->len);
    }
    memcpy(image_msg.data.data, fb->buf, fb->len);
    image_msg.data.size = fb->len;
    rcl_ret_t pub_ret = rcl_publish(&image_publisher, &image_msg, NULL);
    if (pub_ret == RCL_RET_OK) {
      ESP_LOGI(TAG, "画像をパブリッシュしました: %dバイト", fb->len);
    } else {
      ESP_LOGW(TAG, "画像のパブリッシュに失敗しました: %d", pub_ret);
    }
    esp_camera_fb_return(fb);
  } else {
    ESP_LOGW(TAG, "画像の取得に失敗したか、JPEGフォーマットではありません");
  }
}

void setup() {
  delay(2000);
  Serial.begin(115200);
  esp_log_level_set(TAG, ESP_LOG_DEBUG);
  ESP_LOGI(TAG, "セットアップを開始します");

  // micro-ROS WiFi設定
  IPAddress agent_ip;
  agent_ip.fromString(MICRO_ROS_AGENT_IP);
  uint16_t agent_port = MICRO_ROS_AGENT_PORT;
  char ssid[] = WIFI_SSID;
  char password[] = WIFI_PASSWORD;
  set_microros_wifi_transports(ssid, password, agent_ip, agent_port);
  ESP_LOGI(TAG, "micro-ROS WiFiトランスポートを設定しました (SSID: %s, Agent IP: %s:%d)", ssid, MICRO_ROS_AGENT_IP, agent_port);
  
  // WiFi接続とmicro-ROSエージェント接続の確立を待つ
  ESP_LOGI(TAG, "micro-ROSエージェントとの接続を確立中...");
  delay(5000);  // WiFi接続の安定化を待つ
  
  allocator = rcl_get_default_allocator();

  // rclcサポート初期化（リトライ機能付き）
  int retry_count = 0;
  const int max_retries = 10;
  rcl_ret_t ret;
  
  while (retry_count < max_retries) {
    ret = rclc_support_init(&support, 0, NULL, &allocator);
    if (ret == RCL_RET_OK) {
      ESP_LOGI(TAG, "rclc_support_initが完了しました（試行回数: %d）", retry_count + 1);
      break;
    } else {
      retry_count++;
      ESP_LOGW(TAG, "rclc_support_init失敗（試行 %d/%d）、エラーコード: %d", retry_count, max_retries, ret);
      if (retry_count < max_retries) {
        delay(2000);  // 2秒待ってリトライ
      }
    }
  }
  
  if (ret != RCL_RET_OK) {
    ESP_LOGE(TAG, "rclc_support_initが最大試行回数後も失敗しました");
    error_loop(ret, "rclc_support_init");
  }

  // ノード作成
  RCCHECK(rclc_node_init_default(&node, NODE_NAME, "", &support));
  ESP_LOGI(TAG, "ノードを作成しました: %s", NODE_NAME);

  // Int32パブリッシャ作成
  RCCHECK(rclc_publisher_init_default(&publisher, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32), INT32_TOPIC));
  ESP_LOGI(TAG, "Int32パブリッシャを作成しました: %s", INT32_TOPIC);

  // 速度パブリッシャ作成
  RCCHECK(rclc_publisher_init_default(&speed_publisher, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Float32), SPEED_TOPIC));
  ESP_LOGI(TAG, "速度パブリッシャを作成しました: %s", SPEED_TOPIC);

  // タイマー作成
  RCCHECK(rclc_timer_init_default(&timer, &support, RCL_MS_TO_NS(TIMER_TIMEOUT_MS), timer_callback));
  ESP_LOGI(TAG, "タイマーを作成しました: %dms", TIMER_TIMEOUT_MS);

  // エグゼキュータ作成
  RCCHECK(rclc_executor_init(&executor, &support.context, 1, &allocator));
  RCCHECK(rclc_executor_add_timer(&executor, &timer));
  ESP_LOGI(TAG, "エグゼキュータを作成しました");

  msg.data = 0;
  speed_msg.data = 0.0;

  // 画像パブリッシャ作成
  RCCHECK(rclc_publisher_init_default(&image_publisher, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, CompressedImage), IMAGE_TOPIC));
  ESP_LOGI(TAG, "CompressedImageパブリッシャを作成しました: %s", IMAGE_TOPIC);

  // カメラ初期化
  if (!init_camera()) {
    ESP_LOGE(TAG, "カメラ初期化に失敗しました");
    error_loop(-1, "init_camera");
  }

#if defined(LED_GPIO_NUM)
  setupLedFlash(LED_GPIO_NUM);
  ESP_LOGI(TAG, "LEDフラッシュ初期化");
#endif
}

void loop() {
  delay(1000);
  RCSOFTCHECK(rclc_executor_spin_some(&executor, RCL_MS_TO_NS(100)));
  publish_camera_image();
}
