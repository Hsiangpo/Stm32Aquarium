/**
 * @file aquarium_logic.c
 * @brief 智能水族箱核心业务逻辑实现
 */

#include "aquarium_logic.h"
#include <string.h>

/* 静态函数前向声明 */
static AquaError aqua_logic_apply_control(AquariumState *state,
                                          const ControlCommandParams *p);
static AquaError aqua_logic_apply_threshold(AquariumState *state,
                                            const ThresholdCommandParams *p);
static AquaError aqua_logic_apply_config(AquariumState *state,
                                         const ConfigCommandParams *p);
static int32_t aqua_logic_dec_timer(int32_t timer, uint32_t elapsed_seconds);
static int32_t aqua_logic_next_feed_countdown(const AquariumState *state);

/* ============================================================================
 * 初始化
 * ============================================================================
 */

void aqua_logic_init(AquariumState *state) {
  if (!state)
    return;

  memset(state, 0, sizeof(AquariumState));

  /* 默认阈值 */
  state->thresholds.temp_min = DEFAULT_TEMP_MIN;
  state->thresholds.temp_max = DEFAULT_TEMP_MAX;
  state->thresholds.ph_min = DEFAULT_PH_MIN;
  state->thresholds.ph_max = DEFAULT_PH_MAX;
  state->thresholds.tds_warn = DEFAULT_TDS_WARN;
  state->thresholds.tds_critical = DEFAULT_TDS_CRITICAL;
  state->thresholds.turbidity_warn = DEFAULT_TURBIDITY_WARN;
  state->thresholds.turbidity_critical = DEFAULT_TURBIDITY_CRITICAL;
  state->thresholds.level_min = DEFAULT_LEVEL_MIN;
  state->thresholds.level_max = DEFAULT_LEVEL_MAX;
  state->thresholds.feed_interval = DEFAULT_FEED_INTERVAL;
  state->thresholds.feed_amount = DEFAULT_FEED_AMOUNT;

  /* 默认配置 */
  state->config.ph_offset = 0.0f;
  state->config.tds_factor = 1.0f;

  /* 默认运行态 */
  state->target_temp = DEFAULT_TARGET_TEMP;
  state->feed_timer = DEFAULT_FEED_INTERVAL * 3600;
  state->feed_once_timer = FEED_ONCE_TIMER_INACTIVE;
  state->feeding_timer = 0;

  /* 默认属性 */
  state->props.auto_mode = true;
  state->props.feed_countdown = state->feed_timer;
  state->props.feeding_in_progress = false;
  state->props.alarm_level = 0;
  state->props.alarm_muted = false;
}

/* ============================================================================
 * 命令应用
 * ============================================================================
 */

AquaError aqua_logic_apply_command(AquariumState *state,
                                   const ParsedCommand *cmd) {
  if (!state || !cmd) {
    return AQUA_ERR_NULL_PTR;
  }

  switch (cmd->type) {
  case COMMAND_TYPE_CONTROL:
    return aqua_logic_apply_control(state, &cmd->params.control);

  case COMMAND_TYPE_SET_THRESHOLDS:
    return aqua_logic_apply_threshold(state, &cmd->params.threshold);

  case COMMAND_TYPE_SET_CONFIG:
    return aqua_logic_apply_config(state, &cmd->params.config);

  default:
    return AQUA_ERR_INVALID_COMMAND;
  }
}

/* 应用 control 命令 */
static AquaError aqua_logic_apply_control(AquariumState *state,
                                          const ControlCommandParams *p) {      
  if (p->has_feed_once_delay) {
    if (p->feed_once_delay <= 0 ||
        p->feed_once_delay > FEED_ONCE_DELAY_MAX_SECONDS) {
      return AQUA_ERR_INVALID_COMMAND;
    }
  }
  if (p->has_heater) {
    state->props.heater = p->heater;
  }
  if (p->has_pump_in) {
    state->props.pump_in = p->pump_in;
  }
  if (p->has_pump_out) {
    state->props.pump_out = p->pump_out;
  }
  if (p->has_mute) {
    state->props.alarm_muted = p->mute;
  }
  if (p->has_auto_mode) {
    state->props.auto_mode = p->auto_mode;
  }
  if (p->has_feed && p->feed) {
    /* 立即触发投喂 */
    state->feed_timer = 0;
    state->feeding_timer = FEEDING_DURATION_SECONDS;
    state->props.feeding_in_progress = true;
    state->props.feed_countdown = 0;
  }
  if (p->has_feed_once_delay) {
    state->feed_once_timer = p->feed_once_delay;
    state->props.feed_countdown = aqua_logic_next_feed_countdown(state);
  }
  if (p->has_target_temp) {
    state->target_temp = p->target_temp;
  }
  return AQUA_OK;
}

/* 应用 threshold 命令 */
static AquaError aqua_logic_apply_threshold(AquariumState *state,
                                            const ThresholdCommandParams *p) {
  if (p->has_temp_min)
    state->thresholds.temp_min = p->temp_min;
  if (p->has_temp_max)
    state->thresholds.temp_max = p->temp_max;
  if (p->has_ph_min)
    state->thresholds.ph_min = p->ph_min;
  if (p->has_ph_max)
    state->thresholds.ph_max = p->ph_max;
  if (p->has_tds_warn)
    state->thresholds.tds_warn = p->tds_warn;
  if (p->has_tds_critical)
    state->thresholds.tds_critical = p->tds_critical;
  if (p->has_turbidity_warn)
    state->thresholds.turbidity_warn = p->turbidity_warn;
  if (p->has_turbidity_critical)
    state->thresholds.turbidity_critical = p->turbidity_critical;
  if (p->has_level_min)
    state->thresholds.level_min = p->level_min;
  if (p->has_level_max)
    state->thresholds.level_max = p->level_max;

  if (p->has_feed_interval) {
    state->thresholds.feed_interval = p->feed_interval;
    /* 重置投喂计时器 */
    state->feed_timer = p->feed_interval * 3600;
    state->props.feed_countdown = state->feed_timer;
  }
  if (p->has_feed_amount) {
    state->thresholds.feed_amount = p->feed_amount;
  }
  return AQUA_OK;
}

/* 应用 config 命令 */
static AquaError aqua_logic_apply_config(AquariumState *state,
                                         const ConfigCommandParams *p) {
  bool changed = false;

  if (p->has_wifi_ssid) {
    strncpy(state->config.wifi_ssid, p->wifi_ssid, WIFI_SSID_MAX_LEN);
    state->config.wifi_ssid[WIFI_SSID_MAX_LEN] = '\0';
    changed = true;
  }
  if (p->has_wifi_password) {
    strncpy(state->config.wifi_password, p->wifi_password,
            WIFI_PASSWORD_MAX_LEN);
    state->config.wifi_password[WIFI_PASSWORD_MAX_LEN] = '\0';
    changed = true;
  }
  if (p->has_ph_offset) {
    state->config.ph_offset = p->ph_offset;
    changed = true;
  }
  if (p->has_tds_factor) {
    state->config.tds_factor = p->tds_factor;
    changed = true;
  }

  if (changed) {
    state->config_dirty = true;
  }
  return AQUA_OK;
}

/* ============================================================================
 * 时间推进
 * ============================================================================
 */

void aqua_logic_tick(AquariumState *state, uint32_t elapsed_seconds) {
  if (!state || elapsed_seconds == 0)
    return;

  /* 处理投喂进行中 */
  if (state->feeding_timer > 0) {
    if ((int32_t)elapsed_seconds >= state->feeding_timer) {
      /* 投喂结束 */
      state->feeding_timer = 0;
      state->props.feeding_in_progress = false;
      /* 重置投喂倒计时 */
      if (state->feed_timer <= 0) {
        state->feed_timer = state->thresholds.feed_interval * 3600;
      }
    } else {
      state->feeding_timer -= (int32_t)elapsed_seconds;
    }
  } else {
    /* 处理投喂倒计时 */
    state->feed_timer = aqua_logic_dec_timer(state->feed_timer, elapsed_seconds);
    if (state->feed_once_timer > 0) {
      state->feed_once_timer =
          aqua_logic_dec_timer(state->feed_once_timer, elapsed_seconds);
    }

    if (state->feed_once_timer == 0 || state->feed_timer == 0) {
      /* 触发投喂（一次性倒计时优先） */
      state->feeding_timer = FEEDING_DURATION_SECONDS;
      state->props.feeding_in_progress = true;
      if (state->feed_once_timer == 0) {
        state->feed_once_timer = FEED_ONCE_TIMER_INACTIVE;
      }
    }
  }

  /* 更新属性 */
  state->props.feed_countdown = aqua_logic_next_feed_countdown(state);
}

/* ============================================================================
 * 告警等级计算
 * ============================================================================
 */

int32_t aqua_logic_eval_alarm(AquariumState *state) {
  if (!state)
    return 0;

  int32_t level = 0;
  const AquariumProperties *p = &state->props;
  const ThresholdConfig *t = &state->thresholds;

  /* 等级 2（严重）检查 */
  if (p->temperature < t->temp_min || p->temperature > t->temp_max) {
    level = 2;
  } else if (p->ph < t->ph_min || p->ph > t->ph_max) {
    level = 2;
  } else if (p->water_level < (float)t->level_min ||
             p->water_level > (float)t->level_max) {
    level = 2;
  } else if (p->tds >= (float)t->tds_critical) {
    level = 2;
  } else if (p->turbidity >= (float)t->turbidity_critical) {
    level = 2;
  }
  /* 等级 1（警告）检查 */
  else if (p->tds >= (float)t->tds_warn) {
    level = 1;
  } else if (p->turbidity >= (float)t->turbidity_warn) {
    level = 1;
  }

  /* 传感器故障：提升告警等级（但不覆盖严重告警） */
  if (state->sensor_fault_mask != 0 && level < 1) {
    level = 1;
  }

  state->props.alarm_level = level;
  return level;
}

/* ============================================================================
 * 执行器期望状态计算
 * ============================================================================
 */

void aqua_logic_compute_actuators(const AquariumState *state,
                                  ActuatorDesired *desired) {
  if (!state || !desired)
    return;

  memset(desired, 0, sizeof(ActuatorDesired));

  const AquariumProperties *p = &state->props;

  if (p->auto_mode) {
    /* 自动模式：根据传感器值和阈值计算 */

    /* 加热控制（滞回 ±0.5℃） */
    if (p->temperature < state->target_temp - 0.5f) {
      desired->heater = true;
    } else if (p->temperature > state->target_temp + 0.5f) {
      desired->heater = false;
    } else {
      /* 在滞回区间内保持当前状态 */
      desired->heater = p->heater;
    }

    /* 水位控制 */
    bool need_pump_in = (p->water_level < (float)state->thresholds.level_min);
    bool need_pump_out = (p->water_level > (float)state->thresholds.level_max);

    /* 互斥：pump_in 优先 */
    if (need_pump_in) {
      desired->pump_in = true;
      desired->pump_out = false;
    } else if (need_pump_out) {
      desired->pump_in = false;
      desired->pump_out = true;
    } else {
      desired->pump_in = false;
      desired->pump_out = false;
    }

    /* 紧急策略：严重告警时终止换水，并优先保证加热安全 */
    /* Emergency strategy: stop water exchange for critical non-level alarms.
     * Note: water-level anomalies should keep pumps available for correction. */
    if (p->alarm_level >= 2) {
      bool critical_temp = (p->temperature < state->thresholds.temp_min ||
                            p->temperature > state->thresholds.temp_max);
      bool critical_ph = (p->ph < state->thresholds.ph_min ||
                          p->ph > state->thresholds.ph_max);
      bool critical_tds = (p->tds >= (float)state->thresholds.tds_critical);
      bool critical_turb =
          (p->turbidity >= (float)state->thresholds.turbidity_critical);

      bool emergency_stop_pumps =
          (critical_temp || critical_ph || critical_tds || critical_turb);

      if (emergency_stop_pumps) {
        desired->pump_in = false;
        desired->pump_out = false;
        if (p->temperature > state->thresholds.temp_max) {
          desired->heater = false;
        } else {
          desired->heater = true;
        }
      }
    }
  } else {
    /* 手动模式：使用当前属性值 */
    desired->heater = p->heater;
    desired->pump_in = p->pump_in;
    desired->pump_out = p->pump_out;
  }

  /* 告警输出 */
  desired->led = (p->alarm_level > 0);
  desired->buzzer = (p->alarm_level > 0) && !p->alarm_muted;
}

/* ============================================================================ */
/* 内部辅助函数                                                                */
/* ============================================================================ */

static int32_t aqua_logic_dec_timer(int32_t timer, uint32_t elapsed_seconds) {
  if (timer <= 0) {
    return timer;
  }
  if (elapsed_seconds >= (uint32_t)timer) {
    return 0;
  }
  return timer - (int32_t)elapsed_seconds;
}

static int32_t aqua_logic_next_feed_countdown(const AquariumState *state) {
  if (state->feeding_timer > 0 || state->props.feeding_in_progress) {
    return 0;
  }
  int32_t next = state->feed_timer;
  if (state->feed_once_timer >= 0 &&
      (next <= 0 || state->feed_once_timer < next)) {
    next = state->feed_once_timer;
  }
  if (next < 0) {
    next = 0;
  }
  return next;
}
