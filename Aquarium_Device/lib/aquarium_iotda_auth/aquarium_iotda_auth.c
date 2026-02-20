/**
 * @file aquarium_iotda_auth.c
 * @brief 华为云 IoTDA MQTT 鉴权参数生成实现
 */

#include "aquarium_iotda_auth.h"
#include "hmac.h"
#include <stdio.h>
#include <string.h>

int aqua_iotda_build_client_id(const char *device_id, IoTDASignType sign_type,
                               const char *timestamp, char *out,
                               size_t out_size) {
  if (!device_id || !timestamp || !out || out_size == 0) {
    return -1;
  }

  /* 格式：{device_id}_0_{sign_type}_{timestamp} */
  int len = snprintf(out, out_size, "%s_0_%d_%s", device_id, (int)sign_type,
                     timestamp);

  if (len < 0 || (size_t)len >= out_size) {
    return -1;
  }

  return len;
}

void aqua_iotda_build_password(const char *secret, const char *timestamp,
                               char out_hex[IOTDA_PASSWORD_LEN]) {
  if (!secret || !timestamp || !out_hex) {
    if (out_hex)
      out_hex[0] = '\0';
    return;
  }

  /*
   * 华为云文档说明：HMACSHA256(secret, timestamp)
   * 表示"以 secret 为消息内容、timestamp 为密钥"
   * 即：HMAC_SHA256(key=timestamp, msg=secret)
   */
  aqua_hmac_sha256_hex(timestamp, secret, out_hex);
}
