/**
 * @file test_aquarium_logic.c
 * @brief 智能水族箱业务逻辑模块单元测试
 */

#include "aquarium_logic.h"
#include <string.h>
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

/* ============================================================================
 * 测试：初始化
 * ============================================================================
 */

void test_logic_init_defaults(void) {
  AquariumState state;
  aqua_logic_init(&state);

  /* 验证默认阈值 */
  TEST_ASSERT_FLOAT_WITHIN(0.1f, DEFAULT_TEMP_MIN, state.thresholds.temp_min);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, DEFAULT_TEMP_MAX, state.thresholds.temp_max);
  TEST_ASSERT_EQUAL(DEFAULT_FEED_INTERVAL, state.thresholds.feed_interval);

  /* 验证默认运行态 */
  TEST_ASSERT_FLOAT_WITHIN(0.1f, DEFAULT_TARGET_TEMP, state.target_temp);
  TEST_ASSERT_EQUAL(DEFAULT_FEED_INTERVAL * 3600, state.feed_timer);
  TEST_ASSERT_EQUAL(FEED_ONCE_TIMER_INACTIVE, state.feed_once_timer);
  TEST_ASSERT_EQUAL(state.feed_timer, state.props.feed_countdown);
  TEST_ASSERT_FALSE(state.props.feeding_in_progress);

  /* 验证默认属性 */
  TEST_ASSERT_TRUE(state.props.auto_mode);
  TEST_ASSERT_EQUAL(0, state.props.alarm_level);
  TEST_ASSERT_FALSE(state.props.alarm_muted);
}

/* ============================================================================
 * 测试：告警等级计算
 * ============================================================================
 */

void test_alarm_level_normal(void) {
  AquariumState state;
  aqua_logic_init(&state);

  state.props.temperature = 26.0f;
  state.props.ph = 7.0f;
  state.props.tds = 300.0f;
  state.props.turbidity = 20.0f;
  state.props.water_level = 50.0f;

  int32_t level = aqua_logic_eval_alarm(&state);

  TEST_ASSERT_EQUAL(0, level);
  TEST_ASSERT_EQUAL(0, state.props.alarm_level);
}

void test_alarm_level_warning_tds(void) {
  AquariumState state;
  aqua_logic_init(&state);

  state.props.temperature = 26.0f;
  state.props.ph = 7.0f;
  state.props.tds = 500.0f; /* 等于 tds_warn */
  state.props.turbidity = 20.0f;
  state.props.water_level = 50.0f;

  int32_t level = aqua_logic_eval_alarm(&state);

  TEST_ASSERT_EQUAL(1, level);
}

void test_alarm_level_warning_turbidity(void) {
  AquariumState state;
  aqua_logic_init(&state);

  state.props.temperature = 26.0f;
  state.props.ph = 7.0f;
  state.props.tds = 300.0f;
  state.props.turbidity = 30.0f; /* 等于 turbidity_warn */
  state.props.water_level = 50.0f;

  int32_t level = aqua_logic_eval_alarm(&state);

  TEST_ASSERT_EQUAL(1, level);
}

void test_alarm_level_critical_temp_low(void) {
  AquariumState state;
  aqua_logic_init(&state);

  state.props.temperature = 20.0f; /* 低于 temp_min */
  state.props.water_level = 50.0f;

  int32_t level = aqua_logic_eval_alarm(&state);

  TEST_ASSERT_EQUAL(2, level);
}

void test_alarm_level_critical_temp_high(void) {
  AquariumState state;
  aqua_logic_init(&state);

  state.props.temperature = 30.0f; /* 高于 temp_max */
  state.props.water_level = 50.0f;

  int32_t level = aqua_logic_eval_alarm(&state);

  TEST_ASSERT_EQUAL(2, level);
}

void test_alarm_level_critical_ph_low(void) {
  AquariumState state;
  aqua_logic_init(&state);

  state.props.temperature = 26.0f;
  state.props.ph = 6.0f; /* 低于 ph_min */
  state.props.tds = 300.0f;
  state.props.turbidity = 20.0f;
  state.props.water_level = 50.0f;

  int32_t level = aqua_logic_eval_alarm(&state);

  TEST_ASSERT_EQUAL(2, level);
}

void test_alarm_level_critical_ph_high(void) {
  AquariumState state;
  aqua_logic_init(&state);

  state.props.temperature = 26.0f;
  state.props.ph = 8.0f; /* 高于 ph_max */
  state.props.tds = 300.0f;
  state.props.turbidity = 20.0f;
  state.props.water_level = 50.0f;

  int32_t level = aqua_logic_eval_alarm(&state);

  TEST_ASSERT_EQUAL(2, level);
}

void test_alarm_level_critical_water_low(void) {
  AquariumState state;
  aqua_logic_init(&state);

  state.props.temperature = 26.0f;
  state.props.ph = 7.0f;
  state.props.water_level = 10.0f; /* 低于 level_min */

  int32_t level = aqua_logic_eval_alarm(&state);

  TEST_ASSERT_EQUAL(2, level);
}

void test_alarm_level_critical_tds(void) {
  AquariumState state;
  aqua_logic_init(&state);

  state.props.temperature = 26.0f;
  state.props.ph = 7.0f;
  state.props.tds = 800.0f; /* 等于 tds_critical */
  state.props.water_level = 50.0f;

  int32_t level = aqua_logic_eval_alarm(&state);

  TEST_ASSERT_EQUAL(2, level);
}

/* ============================================================================
 * 测试：静音不影响告警等级
 * ============================================================================
 */

void test_muted_does_not_affect_alarm_level(void) {
  AquariumState state;
  aqua_logic_init(&state);

  state.props.temperature = 20.0f; /* 触发等级 2 */
  state.props.water_level = 50.0f;
  state.props.alarm_muted = true;

  int32_t level = aqua_logic_eval_alarm(&state);

  TEST_ASSERT_EQUAL(2, level);               /* 等级仍为 2 */
  TEST_ASSERT_TRUE(state.props.alarm_muted); /* 静音状态不变 */

  /* 但蜂鸣器应该不响 */
  ActuatorDesired desired;
  aqua_logic_compute_actuators(&state, &desired);

  TEST_ASSERT_TRUE(desired.led);     /* LED 仍亮 */
  TEST_ASSERT_FALSE(desired.buzzer); /* 蜂鸣器静音 */
}

/* ============================================================================
 * 测试：自动模式下泵互斥
 * ============================================================================
 */

void test_pump_mutual_exclusion_auto_mode(void) {
  AquariumState state;
  aqua_logic_init(&state);
  ActuatorDesired desired;

  state.props.auto_mode = true;
  state.props.temperature = 26.0f;

  /* 水位过低，pump_in 应开启 */
  state.props.water_level = 10.0f;
  aqua_logic_compute_actuators(&state, &desired);
  TEST_ASSERT_TRUE(desired.pump_in);
  TEST_ASSERT_FALSE(desired.pump_out);

  /* 水位过高，pump_out 应开启 */
  state.props.water_level = 98.0f;
  aqua_logic_compute_actuators(&state, &desired);
  TEST_ASSERT_FALSE(desired.pump_in);
  TEST_ASSERT_TRUE(desired.pump_out);

  /* 水位正常，两个泵都关闭 */
  state.props.water_level = 50.0f;
  aqua_logic_compute_actuators(&state, &desired);
  TEST_ASSERT_FALSE(desired.pump_in);
  TEST_ASSERT_FALSE(desired.pump_out);
}

/* ============================================================================
 * 测试：手动模式下直接使用属性
 * ============================================================================
 */

void test_manual_mode_uses_props(void) {
  AquariumState state;
  aqua_logic_init(&state);
  ActuatorDesired desired;

  state.props.auto_mode = false;
  state.props.heater = true;
  state.props.pump_in = true;
  state.props.pump_out = true; /* 手动模式允许同时开启 */

  aqua_logic_compute_actuators(&state, &desired);

  TEST_ASSERT_TRUE(desired.heater);
  TEST_ASSERT_TRUE(desired.pump_in);
  TEST_ASSERT_TRUE(desired.pump_out);
}

/* ============================================================================
 * 测试：投喂倒计时触发投喂并复位
 * ============================================================================
 */

void test_feed_countdown_triggers_feeding(void) {
  AquariumState state;
  aqua_logic_init(&state);

  /* 设置较短的倒计时 */
  state.feed_timer = 10;
  state.thresholds.feed_interval = 1; /* 1 小时 = 3600 秒 */

  /* 推进时间触发投喂 */
  aqua_logic_tick(&state, 10);

  TEST_ASSERT_TRUE(state.props.feeding_in_progress);
  TEST_ASSERT_EQUAL(FEEDING_DURATION_SECONDS, state.feeding_timer);
  TEST_ASSERT_EQUAL(0, state.feed_timer);

  /* 投喂结束后复位到 feed_interval * 3600 */
  aqua_logic_tick(&state, FEEDING_DURATION_SECONDS);

  TEST_ASSERT_FALSE(state.props.feeding_in_progress);
  TEST_ASSERT_EQUAL(3600, state.feed_timer); /* 1小时 */
  TEST_ASSERT_EQUAL(3600, state.props.feed_countdown);
}

/* ============================================================================ */
/* 测试：一次性倒计时投喂 */
/* ============================================================================ */

void test_feed_once_delay_triggers_feeding(void) {
  AquariumState state;
  aqua_logic_init(&state);

  /* 设置一次性倒计时 5 秒 */
  ParsedCommand cmd = {0};
  cmd.type = COMMAND_TYPE_CONTROL;
  cmd.params.control.has_feed_once_delay = true;
  cmd.params.control.feed_once_delay = 5;

  AquaError err = aqua_logic_apply_command(&state, &cmd);
  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_EQUAL(5, state.feed_once_timer);
  TEST_ASSERT_EQUAL(5, state.props.feed_countdown);

  /* 推进 5 秒触发投喂 */
  aqua_logic_tick(&state, 5);
  TEST_ASSERT_TRUE(state.props.feeding_in_progress);
  TEST_ASSERT_EQUAL(FEEDING_DURATION_SECONDS, state.feeding_timer);
  TEST_ASSERT_EQUAL(FEED_ONCE_TIMER_INACTIVE, state.feed_once_timer);
}

/* ============================================================================ */
/* 测试：严重告警下的紧急策略 */
/* ============================================================================ */

void test_emergency_strategy_auto_mode(void) {
  AquariumState state;
  aqua_logic_init(&state);
  ActuatorDesired desired;

  state.props.auto_mode = true;
  state.props.temperature = state.thresholds.temp_min - 1.0f; /* 触发严重告警 */
  state.props.water_level = 10.0f;
  (void)aqua_logic_eval_alarm(&state);

  aqua_logic_compute_actuators(&state, &desired);
  TEST_ASSERT_FALSE(desired.pump_in);
  TEST_ASSERT_FALSE(desired.pump_out);
  TEST_ASSERT_TRUE(desired.heater);
}

void test_emergency_strategy_overheat(void) {
  AquariumState state;
  aqua_logic_init(&state);
  ActuatorDesired desired;

  state.props.auto_mode = true;
  state.props.temperature = state.thresholds.temp_max + 1.0f; /* 过热 */
  (void)aqua_logic_eval_alarm(&state);

  aqua_logic_compute_actuators(&state, &desired);
  TEST_ASSERT_FALSE(desired.pump_in);
  TEST_ASSERT_FALSE(desired.pump_out);
  TEST_ASSERT_FALSE(desired.heater);
}

/* ============================================================================
 * 测试：命令应用 - control
 * ============================================================================
 */

void test_apply_control_command(void) {
  AquariumState state;
  aqua_logic_init(&state);

  ParsedCommand cmd = {0};
  cmd.type = COMMAND_TYPE_CONTROL;
  cmd.params.control.has_heater = true;
  cmd.params.control.heater = true;
  cmd.params.control.has_auto_mode = true;
  cmd.params.control.auto_mode = false;
  cmd.params.control.has_target_temp = true;
  cmd.params.control.target_temp = 28.0f;

  AquaError err = aqua_logic_apply_command(&state, &cmd);

  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_TRUE(state.props.heater);
  TEST_ASSERT_FALSE(state.props.auto_mode);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 28.0f, state.target_temp);
}

/* ============================================================================
 * 测试：命令应用 - threshold
 * ============================================================================
 */

void test_apply_threshold_command(void) {
  AquariumState state;
  aqua_logic_init(&state);

  ParsedCommand cmd = {0};
  cmd.type = COMMAND_TYPE_SET_THRESHOLDS;
  cmd.params.threshold.has_temp_min = true;
  cmd.params.threshold.temp_min = 22.0f;
  cmd.params.threshold.has_feed_interval = true;
  cmd.params.threshold.feed_interval = 6;

  AquaError err = aqua_logic_apply_command(&state, &cmd);

  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 22.0f, state.thresholds.temp_min);
  TEST_ASSERT_EQUAL(6, state.thresholds.feed_interval);
  TEST_ASSERT_EQUAL(6 * 3600, state.feed_timer); /* 重置投喂计时器 */
}

/* ============================================================================
 * 测试：命令应用 - config
 * ============================================================================
 */

void test_apply_config_command(void) {
  AquariumState state;
  aqua_logic_init(&state);

  ParsedCommand cmd = {0};
  cmd.type = COMMAND_TYPE_SET_CONFIG;
  cmd.params.config.has_wifi_ssid = true;
  strcpy(cmd.params.config.wifi_ssid, "TestWiFi");
  cmd.params.config.has_ph_offset = true;
  cmd.params.config.ph_offset = 0.5f;

  AquaError err = aqua_logic_apply_command(&state, &cmd);

  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_EQUAL_STRING("TestWiFi", state.config.wifi_ssid);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, state.config.ph_offset);
}

/* ============================================================================
 * 测试：立即投喂命令
 * ============================================================================
 */

void test_immediate_feed_command(void) {
  AquariumState state;
  aqua_logic_init(&state);

  ParsedCommand cmd = {0};
  cmd.type = COMMAND_TYPE_CONTROL;
  cmd.params.control.has_feed = true;
  cmd.params.control.feed = true;

  AquaError err = aqua_logic_apply_command(&state, &cmd);

  TEST_ASSERT_EQUAL(AQUA_OK, err);
  TEST_ASSERT_TRUE(state.props.feeding_in_progress);
  TEST_ASSERT_EQUAL(FEEDING_DURATION_SECONDS, state.feeding_timer);
  TEST_ASSERT_EQUAL(0, state.feed_timer);
  TEST_ASSERT_EQUAL(0, state.props.feed_countdown);
}

/* ============================================================================
 * 主函数
 * ============================================================================
 */

int main(void) {
  UNITY_BEGIN();

  /* 初始化测试 */
  RUN_TEST(test_logic_init_defaults);

  /* 告警等级测试 */
  RUN_TEST(test_alarm_level_normal);
  RUN_TEST(test_alarm_level_warning_tds);
  RUN_TEST(test_alarm_level_warning_turbidity);
  RUN_TEST(test_alarm_level_critical_temp_low);
  RUN_TEST(test_alarm_level_critical_temp_high);
  RUN_TEST(test_alarm_level_critical_ph_low);
  RUN_TEST(test_alarm_level_critical_ph_high);
  RUN_TEST(test_alarm_level_critical_water_low);
  RUN_TEST(test_alarm_level_critical_tds);

  /* 静音测试 */
  RUN_TEST(test_muted_does_not_affect_alarm_level);

  /* 泵互斥测试 */
  RUN_TEST(test_pump_mutual_exclusion_auto_mode);

  /* 手动模式测试 */
  RUN_TEST(test_manual_mode_uses_props);

  /* 投喂倒计时测试 */
  RUN_TEST(test_feed_countdown_triggers_feeding);
  RUN_TEST(test_feed_once_delay_triggers_feeding);

  /* 命令应用测试 */
  RUN_TEST(test_apply_control_command);
  RUN_TEST(test_apply_threshold_command);
  RUN_TEST(test_apply_config_command);
  RUN_TEST(test_immediate_feed_command);

  /* 紧急策略测试 */
  RUN_TEST(test_emergency_strategy_auto_mode);
  RUN_TEST(test_emergency_strategy_overheat);

  return UNITY_END();
}
