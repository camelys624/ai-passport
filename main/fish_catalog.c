// main/fish_catalog.c — 见 fish_catalog.h。
#include "fish_catalog.h"

const fish_bait_t FISH_BAITS[FISH_BAIT_COUNT] = {
    {.name = "蚯蚓", .hint = "什么鱼都爱，上钩快", .cost = 5, .wait_min_ms = 30000, .wait_max_ms = 90000,
     .bite_ms = 6000},
    {.name = "面团", .hint = "鲤鱼、草鱼的最爱", .cost = 10, .wait_min_ms = 45000, .wait_max_ms = 120000,
     .bite_ms = 6000},
    {.name = "亮片", .hint = "引来凶猛的掠食鱼", .cost = 20, .wait_min_ms = 90000, .wait_max_ms = 180000,
     .bite_ms = 6000},
    // 番茄钟：专注满固定时长才上鱼，不花积分；专注越久，稀有渔获越多。
    {.name = "一个番茄", .hint = "专注 30 分钟后上鱼", .cost = 0, .wait_min_ms = 1800000,
     .wait_max_ms = 1800000, .bite_ms = 60000, .tomatoes = 1},
    {.name = "两个番茄", .hint = "专注 1 小时，大鱼更多", .cost = 0, .wait_min_ms = 3600000,
     .wait_max_ms = 3600000, .bite_ms = 60000, .tomatoes = 2},
};

// weight[] 顺序与 FISH_BAITS 一致：{蚯蚓, 面团, 亮片, 一个番茄, 两个番茄}。
const fish_entry_t FISH_ENTRIES[FISH_ENTRY_COUNT] = {
    {.name = "鲫鱼", .desc = "池塘里最常见的小鱼，\n胃口很好。",
     .kind = FISH_KIND_FISH, .rarity = FISH_RARITY_COMMON,
     .reel_steps = 3, .price = 5, .weight = {40, 30, 5, 15, 2}},
    {.name = "鲤鱼", .desc = "身形厚实，\n偏爱面团的香味。",
     .kind = FISH_KIND_FISH, .rarity = FISH_RARITY_COMMON,
     .reel_steps = 4, .price = 10, .weight = {15, 35, 5, 15, 4}},
    {.name = "草鱼", .desc = "只吃素，\n蚯蚓和亮片都不太理。",
     .kind = FISH_KIND_FISH, .rarity = FISH_RARITY_UNCOMMON,
     .reel_steps = 4, .price = 20, .weight = {5, 20, 0, 12, 8}},
    {.name = "鲈鱼", .desc = "凶猛的掠食者，\n一见亮片就扑。",
     .kind = FISH_KIND_FISH, .rarity = FISH_RARITY_UNCOMMON,
     .reel_steps = 5, .price = 25, .weight = {10, 0, 35, 12, 10}},
    {.name = "鳜鱼", .desc = "藏在石缝里的\n伏击高手。",
     .kind = FISH_KIND_FISH, .rarity = FISH_RARITY_RARE,
     .reel_steps = 6, .price = 60, .weight = {2, 0, 12, 6, 16}},
    {.name = "金色锦鲤", .desc = "传说钓到它，\n一整天都有好运。",
     .kind = FISH_KIND_FISH, .rarity = FISH_RARITY_RARE,
     .reel_steps = 6, .price = 100, .weight = {1, 3, 2, 4, 12}},
    {.name = "水草", .desc = "缠了一钩子，\n下次换个位置。",
     .kind = FISH_KIND_JUNK, .rarity = FISH_RARITY_COMMON,
     .reel_steps = 3, .price = 0, .weight = {10, 8, 8, 3, 0}},
    {.name = "旧靴子", .desc = "不知道是谁的，\n还挺合脚。",
     .kind = FISH_KIND_JUNK, .rarity = FISH_RARITY_COMMON,
     .reel_steps = 3, .price = 0, .weight = {12, 8, 15, 3, 0}},
    {.name = "漂流瓶", .desc = "纸条上写着：\n今天也辛苦了。",
     .kind = FISH_KIND_JUNK, .rarity = FISH_RARITY_UNCOMMON,
     .reel_steps = 4, .price = 10, .weight = {6, 4, 6, 6, 4}},
    {.name = "宝箱", .desc = "锈迹斑斑，\n里面是几枚旧铜钱。",
     .kind = FISH_KIND_JUNK, .rarity = FISH_RARITY_RARE,
     .reel_steps = 5, .price = 50, .weight = {1, 0, 5, 4, 12}},
};

_Static_assert(FISH_ENTRY_COUNT <= FISH_ENTRY_MAX, "存档格式最多记录 FISH_ENTRY_MAX 种渔获");
