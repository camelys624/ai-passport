// main/fish_catalog.h — 饵料与渔获数据表（纯数据，不依赖 ESP-IDF/LVGL）。
//
// 扩展方式：
//   加饵料：FISH_BAIT_COUNT + 1，在 FISH_BAITS 追加一项，并给每个渔获的 weight[] 补一列。
//   加渔获：FISH_ENTRY_COUNT + 1，在 FISH_ENTRIES 末尾追加一行（不要插到中间：
//           存档按下标记录收集数量）。上限 FISH_ENTRY_MAX 由存档格式决定。
//   像素图：tools/gen_fish_sprites.py 按 name 查找画法（CATCH_ART / BAIT_ART），
//           新名字没有对应画法时构建失败。
// 概率 = 该项在当前饵料下的权重 / 当前饵料下所有项的权重之和；权重 0 = 这种饵料钓不到它。
// 积分：抛竿时扣除饵料的 cost；渔获在鱼册里按 price 卖出换积分（水草、旧靴子为 0，不能卖）。
// desc 显示在 176 px 宽的卡片里，16 px 字每行最多 11 个汉字：用 "\n" 手动断成两行，
// 避免标点被挤到单独一行。新增的文字需要重新运行 tools/gen_fish_fonts.py。
#pragma once

#include <stdint.h>

#define FISH_BAIT_COUNT 3
#define FISH_ENTRY_COUNT 10
#define FISH_ENTRY_MAX 32

typedef enum {
    FISH_KIND_FISH = 0,
    FISH_KIND_JUNK,
} fish_kind_t;

typedef enum {
    FISH_RARITY_COMMON = 0,
    FISH_RARITY_UNCOMMON,
    FISH_RARITY_RARE,
} fish_rarity_t;

typedef struct {
    const char *name;
    const char *hint;      // 准备页上的一句提示，不直接显示概率
    uint16_t cost;         // 每抛一竿消耗的积分
    uint32_t wait_min_ms;  // 抛竿到咬钩的随机等待区间（含两端）
    uint32_t wait_max_ms;
} fish_bait_t;

typedef struct {
    const char *name;
    const char *desc;
    uint8_t kind;          // fish_kind_t
    uint8_t rarity;        // fish_rarity_t
    uint8_t reel_steps;    // 收线提示步数，越稀有越多
    uint16_t price;        // 在鱼册卖出一条换得的积分；0 = 不能卖
    uint16_t weight[FISH_BAIT_COUNT];
} fish_entry_t;

extern const fish_bait_t FISH_BAITS[FISH_BAIT_COUNT];
extern const fish_entry_t FISH_ENTRIES[FISH_ENTRY_COUNT];
