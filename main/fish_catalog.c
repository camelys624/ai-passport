// main/fish_catalog.c — 见 fish_catalog.h。
#include "fish_catalog.h"

const fish_bait_t FISH_BAITS[FISH_BAIT_COUNT] = {
    {.name = "蚯蚓", .hint = "什么鱼都爱，上钩快", .wait_min_ms = 30000, .wait_max_ms = 90000},
    {.name = "面团", .hint = "鲤鱼、草鱼的最爱", .wait_min_ms = 45000, .wait_max_ms = 120000},
    {.name = "亮片", .hint = "引来凶猛的掠食鱼", .wait_min_ms = 90000, .wait_max_ms = 180000},
};

// weight[] 顺序与 FISH_BAITS 一致：{蚯蚓, 面团, 亮片}。
const fish_entry_t FISH_ENTRIES[FISH_ENTRY_COUNT] = {
    {.name = "鲫鱼", .desc = "池塘里最常见的小鱼，\n胃口很好。",
     .kind = FISH_KIND_FISH, .rarity = FISH_RARITY_COMMON,
     .reel_steps = 3, .weight = {40, 30, 5}},
    {.name = "鲤鱼", .desc = "身形厚实，\n偏爱面团的香味。",
     .kind = FISH_KIND_FISH, .rarity = FISH_RARITY_COMMON,
     .reel_steps = 4, .weight = {15, 35, 5}},
    {.name = "草鱼", .desc = "只吃素，\n蚯蚓和亮片都不太理。",
     .kind = FISH_KIND_FISH, .rarity = FISH_RARITY_UNCOMMON,
     .reel_steps = 4, .weight = {5, 20, 0}},
    {.name = "鲈鱼", .desc = "凶猛的掠食者，\n一见亮片就扑。",
     .kind = FISH_KIND_FISH, .rarity = FISH_RARITY_UNCOMMON,
     .reel_steps = 5, .weight = {10, 0, 35}},
    {.name = "鳜鱼", .desc = "藏在石缝里的\n伏击高手。",
     .kind = FISH_KIND_FISH, .rarity = FISH_RARITY_RARE,
     .reel_steps = 6, .weight = {2, 0, 12}},
    {.name = "金色锦鲤", .desc = "传说钓到它，\n一整天都有好运。",
     .kind = FISH_KIND_FISH, .rarity = FISH_RARITY_RARE,
     .reel_steps = 6, .weight = {1, 3, 2}},
    {.name = "水草", .desc = "缠了一钩子，\n下次换个位置。",
     .kind = FISH_KIND_JUNK, .rarity = FISH_RARITY_COMMON,
     .reel_steps = 3, .weight = {10, 8, 8}},
    {.name = "旧靴子", .desc = "不知道是谁的，\n还挺合脚。",
     .kind = FISH_KIND_JUNK, .rarity = FISH_RARITY_COMMON,
     .reel_steps = 3, .weight = {12, 8, 15}},
    {.name = "漂流瓶", .desc = "纸条上写着：\n今天也辛苦了。",
     .kind = FISH_KIND_JUNK, .rarity = FISH_RARITY_UNCOMMON,
     .reel_steps = 4, .weight = {6, 4, 6}},
    {.name = "宝箱", .desc = "锈迹斑斑，\n里面是几枚旧铜钱。",
     .kind = FISH_KIND_JUNK, .rarity = FISH_RARITY_RARE,
     .reel_steps = 5, .weight = {1, 0, 5}},
};

_Static_assert(FISH_ENTRY_COUNT <= FISH_ENTRY_MAX, "存档格式最多记录 FISH_ENTRY_MAX 种渔获");
