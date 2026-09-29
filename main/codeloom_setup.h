// main/codeloom_setup.h — 配网模式：SoftAP + 本地网页表单 + STA 验证 + 设备配对。
//
// 只在“没有有效配置”时运行。成功后把配置写入 NVS 并重启进入正常模式；任何失败都
// 显示原因并回到热点页面，不写入任何配置（保留“先验证、后保存”的顺序）。
#pragma once

// 配网主循环，作为独立任务运行，永不返回（成功时 esp_restart()）。
void codeloom_setup_task(void *arg);
