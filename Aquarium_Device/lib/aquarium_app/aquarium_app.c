/**
 * @file aquarium_app.c
 * @brief 智能水族箱应用层编排器实现
 */

#include "aquarium_app.h"
#include <string.h>

/* ============================================================================
 * 传感器容错：连续失败/异常 -> 安全值兜底 + 告警
 * ============================================================================
 */

#define AQUA_SENSOR_FAULT_TEMP (1u << 0)
#define AQUA_SENSOR_FAULT_PH (1u << 1)
#define AQUA_SENSOR_FAULT_TDS (1u << 2)
#define AQUA_SENSOR_FAULT_TURBIDITY (1u << 3)
#define AQUA_SENSOR_FAULT_WATER_LEVEL (1u << 4)

#define AQUA_TEMP_PHYS_MIN (-55.0f)
#define AQUA_TEMP_PHYS_MAX (125.0f)
#define AQUA_PH_PHYS_MIN (0.0f)
#define AQUA_PH_PHYS_MAX (14.0f)
#define AQUA_TDS_PHYS_MIN (0.0f)
#define AQUA_TDS_PHYS_MAX (5000.0f)
#define AQUA_TURB_PHYS_MIN (0.0f)
#define AQUA_TURB_PHYS_MAX (3000.0f)
#define AQUA_LEVEL_PHYS_MIN (0.0f)
#define AQUA_LEVEL_PHYS_MAX (100.0f)

static bool aqua_is_finitef(float v) { return (v == v) && ((v - v) == 0.0f); }

static float aqua_clampf(float v, float min_val, float max_val) {
  if (v < min_val)
    return min_val;
  if (v > max_val)
    return max_val;
  return v;
}

static void aqua_normalize_min_max(float *min_val, float *max_val,
                                   float default_min, float default_max) {
  if (!min_val || !max_val)
    return;

  float min_v = *min_val;
  float max_v = *max_val;

  if (!aqua_is_finitef(min_v) || !aqua_is_finitef(max_v)) {
    min_v = default_min;
    max_v = default_max;
  }
  if (min_v > max_v) {
    float tmp = min_v;
    min_v = max_v;
    max_v = tmp;
  }

  *min_val = min_v;
  *max_val = max_v;
}

typedef struct {
  float temperature;
  float ph;
  float tds;
  float turbidity;
  float water_level;
} AquaSafeSensorValues;

static AquaSafeSensorValues
aqua_app_compute_safe_sensor_values(const AquariumState *state) {
  AquaSafeSensorValues safe = {0};
  if (!state) {
    return safe;
  }

  float temp_min = state->thresholds.temp_min;
  float temp_max = state->thresholds.temp_max;
  aqua_normalize_min_max(&temp_min, &temp_max, DEFAULT_TEMP_MIN,
                         DEFAULT_TEMP_MAX);

  float safe_temp = state->target_temp;
  if (!aqua_is_finitef(safe_temp)) {
    safe_temp = (temp_min + temp_max) * 0.5f;
  }
  safe.temperature = aqua_clampf(safe_temp, temp_min, temp_max);

  float ph_min = state->thresholds.ph_min;
  float ph_max = state->thresholds.ph_max;
  aqua_normalize_min_max(&ph_min, &ph_max, DEFAULT_PH_MIN, DEFAULT_PH_MAX);
  safe.ph = aqua_clampf((ph_min + ph_max) * 0.5f, AQUA_PH_PHYS_MIN,
                        AQUA_PH_PHYS_MAX);

  safe.tds = 0.0f;
  safe.turbidity = 0.0f;

  float level_min = (float)state->thresholds.level_min;
  float level_max = (float)state->thresholds.level_max;
  if (level_min > level_max) {
    float tmp = level_min;
    level_min = level_max;
    level_max = tmp;
  }
  level_min = aqua_clampf(level_min, AQUA_LEVEL_PHYS_MIN, AQUA_LEVEL_PHYS_MAX);
  level_max = aqua_clampf(level_max, AQUA_LEVEL_PHYS_MIN, AQUA_LEVEL_PHYS_MAX);
  if (level_min > level_max) {
    level_min = DEFAULT_LEVEL_MIN;
    level_max = DEFAULT_LEVEL_MAX;
  }
  safe.water_level =
      aqua_clampf((level_min + level_max) * 0.5f, AQUA_LEVEL_PHYS_MIN,
                  AQUA_LEVEL_PHYS_MAX);

  return safe;
}

static void aqua_app_update_sensor_with_tolerance(AquariumState *state,
                                                  float *out_value,
                                                  uint8_t *fail_count,
                                                  uint32_t fault_bit,
                                                  bool value_valid,
                                                  float value,
                                                  float safe_default) {
  if (!state || !out_value || !fail_count) {
    return;
  }

  bool ok = value_valid && aqua_is_finitef(value);

  if (ok) {
    *out_value = value;
    *fail_count = 0;
    state->sensor_fault_mask &= ~fault_bit;
    return;
  }

  if (*fail_count < AQUA_APP_SENSOR_FAIL_THRESHOLD) {
    (*fail_count)++;
  }

  if (*fail_count >= AQUA_APP_SENSOR_FAIL_THRESHOLD) {
    *out_value = safe_default;
    state->sensor_fault_mask |= fault_bit;
  } else {
    /* 未达阈值：保留上次值，但确保不会把 NaN/Inf 带入状态 */
    if (!aqua_is_finitef(*out_value)) {
      *out_value = safe_default;
    }
  }
}

/* ============================================================================
 * 初始化
 * ============================================================================
 */

void aqua_app_init(AquariumApp *app, const char *device_id) {
  if (!app)
    return;

  memset(app, 0, sizeof(AquariumApp));

  /* 设置设备 ID */
  if (device_id) {
    strncpy(app->device_id, device_id, DEVICE_ID_MAX_LEN);
    app->device_id[DEVICE_ID_MAX_LEN] = '\0';
  }

  /* 初始化设备状态 */
  aqua_logic_init(&app->state);

  /* 传感器安全默认值：避免启动早期/采集异常导致 NaN/Inf 或误触发阈值告警 */
  AquaSafeSensorValues safe = aqua_app_compute_safe_sensor_values(&app->state);
  app->state.props.temperature = safe.temperature;
  app->state.props.ph = safe.ph;
  app->state.props.tds = safe.tds;
  app->state.props.turbidity = safe.turbidity;
  app->state.props.water_level = safe.water_level;
  app->state.sensor_fault_mask = 0;

  /* 默认上报配置 */
  app->report_interval = DEFAULT_REPORT_INTERVAL_SECONDS;
  app->report_timer = DEFAULT_REPORT_INTERVAL_SECONDS;
}

/* ============================================================================
 * 配置
 * ============================================================================
 */

void aqua_app_set_report_interval(AquariumApp *app, uint32_t interval_seconds) {
  if (!app || interval_seconds == 0)
    return;

  app->report_interval = interval_seconds;
  /* 重置上报计时器 */
  app->report_timer = interval_seconds;
}

/* ============================================================================
 * 传感器数据更新
 * ============================================================================
 */

void aqua_app_update_sensors(AquariumApp *app, float temperature, float ph,
                             float tds, float turbidity, float water_level) {
  if (!app)
    return;

  AquariumState *state = &app->state;
  const AquaSafeSensorValues safe = aqua_app_compute_safe_sensor_values(state);

  /* 温度：物理范围校验（避免 NaN/Inf 或明显异常值） */
  bool temp_ok = aqua_is_finitef(temperature) && temperature >= AQUA_TEMP_PHYS_MIN &&
                 temperature <= AQUA_TEMP_PHYS_MAX;
  aqua_app_update_sensor_with_tolerance(
      state, &state->props.temperature, &app->sensor_fail_count_temp,
      AQUA_SENSOR_FAULT_TEMP, temp_ok, temperature, safe.temperature);

  /* pH：先校验原始值，再应用偏移校准（校准后 clamp 到物理范围） */
  bool ph_raw_ok = aqua_is_finitef(ph) && ph >= AQUA_PH_PHYS_MIN &&
                   ph <= AQUA_PH_PHYS_MAX;
  float ph_cal = ph + state->config.ph_offset;
  bool ph_ok = ph_raw_ok && aqua_is_finitef(ph_cal);
  if (ph_ok) {
    ph_cal = aqua_clampf(ph_cal, AQUA_PH_PHYS_MIN, AQUA_PH_PHYS_MAX);
  }
  aqua_app_update_sensor_with_tolerance(
      state, &state->props.ph, &app->sensor_fail_count_ph, AQUA_SENSOR_FAULT_PH,
      ph_ok, ph_cal, safe.ph);

  /* TDS：先校验原始值，再应用系数校准（校准后 clamp 到物理范围） */
  bool tds_raw_ok = aqua_is_finitef(tds) && tds >= AQUA_TDS_PHYS_MIN &&
                    tds <= AQUA_TDS_PHYS_MAX;
  float tds_cal = tds * state->config.tds_factor;
  bool tds_ok = tds_raw_ok && aqua_is_finitef(tds_cal);
  if (tds_ok) {
    tds_cal = aqua_clampf(tds_cal, AQUA_TDS_PHYS_MIN, AQUA_TDS_PHYS_MAX);
  }
  aqua_app_update_sensor_with_tolerance(
      state, &state->props.tds, &app->sensor_fail_count_tds,
      AQUA_SENSOR_FAULT_TDS, tds_ok, tds_cal, safe.tds);

  /* 浊度：物理范围校验 */
  bool turb_ok = aqua_is_finitef(turbidity) && turbidity >= AQUA_TURB_PHYS_MIN &&
                 turbidity <= AQUA_TURB_PHYS_MAX;
  aqua_app_update_sensor_with_tolerance(
      state, &state->props.turbidity, &app->sensor_fail_count_turbidity,
      AQUA_SENSOR_FAULT_TURBIDITY, turb_ok, turbidity, safe.turbidity);

  /* 水位：物理范围校验 */
  bool level_ok = aqua_is_finitef(water_level) &&
                  water_level >= AQUA_LEVEL_PHYS_MIN &&
                  water_level <= AQUA_LEVEL_PHYS_MAX;
  aqua_app_update_sensor_with_tolerance(
      state, &state->props.water_level, &app->sensor_fail_count_water_level,
      AQUA_SENSOR_FAULT_WATER_LEVEL, level_ok, water_level, safe.water_level);
}

/* ============================================================================
 * 主循环步进
 * ============================================================================
 */

AquaError aqua_app_step(AquariumApp *app, uint32_t elapsed_seconds,
                        ActuatorDesired *out_actuators, bool *out_has_publish,
                        char *out_topic, size_t topic_size, char *out_payload,
                        size_t payload_size) {
  if (!app || !out_actuators || !out_has_publish || !out_topic ||
      !out_payload) {
    return AQUA_ERR_NULL_PTR;
  }

  *out_has_publish = false;

  /* 1. 推进投喂倒计时 */
  aqua_logic_tick(&app->state, elapsed_seconds);

  /* 2. 计算告警等级 */
  aqua_logic_eval_alarm(&app->state);

  /* 3. 计算期望执行器状态 */
  aqua_logic_compute_actuators(&app->state, out_actuators);

  /* 4. 自动模式下将期望执行器状态回写到 props（保证上报一致） */
  if (app->state.props.auto_mode) {
    app->state.props.heater = out_actuators->heater;
    app->state.props.pump_in = out_actuators->pump_in;
    app->state.props.pump_out = out_actuators->pump_out;
  }

  /* 5. 检查上报周期 */
  if (elapsed_seconds >= app->report_timer) {
    /* 触发上报 */
    size_t topic_len, payload_len;
    AquaError err = aqua_iotda_build_report(
        app->device_id, &app->state.props, out_topic, topic_size, out_payload,
        payload_size, &topic_len, &payload_len);
    if (err != AQUA_OK) {
      return err;
    }

    *out_has_publish = true;
    /* 重置上报计时器 */
    app->report_timer = app->report_interval;
  } else {
    app->report_timer -= elapsed_seconds;
  }

  return AQUA_OK;
}

/* ============================================================================
 * MQTT 命令处理
 * ============================================================================
 */

AquaError aqua_app_on_mqtt_command(AquariumApp *app, const char *in_topic,
                                   const char *in_payload, size_t payload_len,
                                   bool *out_has_response, char *out_topic,
                                   size_t topic_size, char *out_payload,
                                   size_t payload_size) {
  if (!app || !in_topic || !in_payload || !out_has_response || !out_topic ||
      !out_payload) {
    return AQUA_ERR_NULL_PTR;
  }

  IoTDACommandResult result;
  AquaError err = aqua_iotda_handle_command(
      app->device_id, in_topic, in_payload, payload_len, &app->state, &result);

  if (err != AQUA_OK && !result.has_response) {
    *out_has_response = false;
    return err;
  }

  *out_has_response = result.has_response;

  if (result.has_response) {
    /* 复制响应 Topic */
    if (result.response_topic_len >= topic_size) {
      return AQUA_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(out_topic, result.response_topic, result.response_topic_len + 1);

    /* 复制响应 Payload */
    if (result.response_payload_len >= payload_size) {
      return AQUA_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(out_payload, result.response_payload,
           result.response_payload_len + 1);
  }

  return AQUA_OK;
}

/* ============================================================================
 * 状态访问
 * ============================================================================
 */

const AquariumState *aqua_app_get_state(const AquariumApp *app) {
  if (!app)
    return NULL;
  return &app->state;
}
