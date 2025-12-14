# iotfleetwise-ros2

AWS IoT FleetWise Edge Agent と micro-ROS を組み合わせて、センサーデータ（例: 速度、カメラ画像）を ROS 2 トピックとして扱い、RPi 側で FleetWise に橋渡しするためのサンプルです。

## 構成

- **ESP32-S3（PlatformIO / Arduino + micro-ROS）**: `src/`
  - micro-ROS（Wi-Fi transport）で ROS 2 トピックを Publish
  - カメラ（`esp32-camera`）で JPEG の `sensor_msgs/CompressedImage` を Publish
- **Raspberry Pi（Docker Compose）**: `rpi/`
  - micro-ROS Agent（XRCE-DDS Agent）と FleetWise Edge Agent を起動
  - `rpi/run_agents.sh` で `up/down/logs/ps` をまとめて操作

## 使い始め方（最小）

1. **ESP32 側の Wi-Fi / Agent 接続先を設定**

```bash
cp src/secret.h.example src/secret.h
```

1. **ESP32-S3 をビルドして書き込み（PlatformIO）**

```bash
pio run -e esp32s3box -t upload
```

1. **RPi 側で Agent を起動**

```bash
cp rpi/agents.env.example rpi/agents.env
./rpi/run_agents.sh up
```

## 主なトピック

- **`/ego_vehicle/speedometer`**: `std_msgs/Float32`
- **`/ego_vehicle/rgb_front/image_compressed`**: `sensor_msgs/CompressedImage`
- **`/fwe_topic`**: `std_msgs/Int32`（動作確認用）

## 注意事項

- **秘密情報はコミットしない**: `src/secret.h` や `rpi/agents.env`、`rpi/cert/private.key` などはローカル専用です。
- **Agent の接続先**: ESP32 側の `MICRO_ROS_AGENT_IP/PORT` と、RPi 側の `MICRO_ROS_AGENT_PORT` を一致させてください。

## 参考

- **ROS 2 Humble のインストール**: `https://docs.ros.org/en/humble/Installation/Ubuntu-Install-Debians.html`
- **PlatformIO**: `https://docs.platformio.org/`
