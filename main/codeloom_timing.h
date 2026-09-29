// main/codeloom_timing.h — 背光空闲策略、轮询间隔和重连退避（纯 C，时间由调用方传入）。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CL_BACKLIGHT_DIM_AFTER_MS 30000U
#define CL_BACKLIGHT_OFF_AFTER_MS 60000U
#define CL_BACKLIGHT_ON_PERCENT 80U
#define CL_BACKLIGHT_DIM_PERCENT 15U
// 唤醒按键之后的手势（单击/双击/长按）在此窗口内会被吞掉；超时后不再吞，避免误伤下一次操作。
#define CL_WAKE_SWALLOW_MS 3000U

#define CL_POLL_SCREEN_ON_MS 5000U
#define CL_POLL_SCREEN_OFF_MS 20000U
#define CL_POLL_MAX_MS 60000U
#define CL_WIFI_RETRY_BASE_MS 1000U
#define CL_WIFI_RETRY_MAX_MS 60000U

typedef enum {
    CL_SCREEN_ON = 0,
    CL_SCREEN_DIM,
    CL_SCREEN_OFF,
} cl_screen_t;

typedef struct {
    cl_screen_t screen;
    uint64_t last_activity_ms;
    int swallow_button; // -1 表示无
    uint64_t swallow_until_ms;
} cl_backlight_t;

void cl_backlight_init(cl_backlight_t *bl, uint64_t now_ms);

// 按键按下（BSP_BTN_PRESS）。记录活动；屏幕变暗/熄灭时唤醒，并标记该键随后的手势只用于唤醒。
// 屏幕状态改变时返回 true。
bool cl_backlight_press(cl_backlight_t *bl, int button, uint64_t now_ms);

// 手势事件（单击/双击/长按）到达时调用。若它属于唤醒按键，消费标记并返回 true（应丢弃）。
// 同时记录活动；若此时屏幕不亮（未收到 PRESS 的情况），也会唤醒并返回 true。
bool cl_backlight_gesture_swallowed(cl_backlight_t *bl, int button, uint64_t now_ms,
                                    bool *screen_changed);

// 新审批提醒等非按键唤醒：点亮屏幕并重置空闲计时。状态改变时返回 true。
bool cl_backlight_wake(cl_backlight_t *bl, uint64_t now_ms);

// 周期调用，按空闲时长进入 DIM / OFF。状态改变时返回 true。
bool cl_backlight_tick(cl_backlight_t *bl, uint64_t now_ms);

uint8_t cl_backlight_percent(cl_screen_t screen);

// 下次轮询前的等待：屏幕亮（含变暗）5 s、熄灭 20 s；连续失败按 2^n 退避，上限 60 s；
// 设备被吊销后固定 60 s。
uint32_t cl_poll_delay_ms(bool screen_off, unsigned failures, bool revoked);

// Wi-Fi 重连：第 attempt 次（从 0 开始）等待 1 s、2 s、4 s … 上限 60 s。
uint32_t cl_wifi_retry_delay_ms(unsigned attempt);
