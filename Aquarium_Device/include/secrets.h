/**
 * @file secrets.h
 * @brief WiFi 和 IoTDA 固定配置（单机演示）
 */

#ifndef SECRETS_H
#define SECRETS_H

/* WiFi 配置（按现场路由修改） */
#define WIFI_SSID "YourWiFiSSID"
#define WIFI_PASSWORD "YourWiFiPassword"

/* 华为云 IoTDA 配置 */
#define IOTDA_HOST "8cee850016.st1.iotda-device.cn-north-4.myhuaweicloud.com"
#define IOTDA_PORT 1883 /* 纯 MQTT: 1883；如需 MQTTS(8883) 需扩展 aquarium_esp32_mqtt 的 scheme/证书配置 */
#define IOTDA_DEVICE_ID "690237639798273cc4fd09cb_MyAquarium_01"
#define IOTDA_SECRET "z748464wo946"

/* 时间戳（10位，如 2025121400，用于鉴权） */
#define IOTDA_TIMESTAMP "2025121400"

#endif /* SECRETS_H */
