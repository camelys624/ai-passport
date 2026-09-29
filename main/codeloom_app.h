// main/codeloom_app.h — 正常模式应用任务：事件循环、状态机、背光、提醒和界面刷新。
#pragma once

#include "codeloom_types.h"

// 作为独立任务运行（arg 指向 main 中静态存储的 cl_config_t），永不返回。
void codeloom_app_task(void *arg);
