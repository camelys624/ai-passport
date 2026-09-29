// main/codeloom_state.c — 见 codeloom_state.h。
#include "codeloom_state.h"

#include <stddef.h>
#include <string.h>

static void copy_id(char *dst, const char *src)
{
    size_t length = 0;

    while (length + 1 < CL_ID_MAX && src[length] != '\0') {
        ++length;
    }
    memcpy(dst, src, length);
    dst[length] = '\0';
}

static void set_init(cl_id_set_t *set, char (*storage)[CL_ID_MAX], uint8_t capacity)
{
    memset(set, 0, sizeof(*set));
    set->ids = storage;
    set->capacity = capacity;
}

static int set_find(const cl_id_set_t *set, const char *id)
{
    for (uint8_t i = 0; i < set->count; ++i) {
        if (strncmp(set->ids[i], id, CL_ID_MAX) == 0) {
            return i;
        }
    }
    return -1;
}

static bool set_contains(const cl_id_set_t *set, const char *id)
{
    return id[0] != '\0' && set_find(set, id) >= 0;
}

// 满时覆盖最早加入的 ID（seen/resolved 只需记住近期条目）。
static void set_add_fifo(cl_id_set_t *set, const char *id)
{
    if (id[0] == '\0' || set_contains(set, id)) {
        return;
    }
    if (set->count < set->capacity) {
        copy_id(set->ids[set->count++], id);
        return;
    }
    copy_id(set->ids[set->next], id);
    set->next = (uint8_t)((set->next + 1U) % set->capacity);
}

// 满时拒绝（inflight 不能丢失正在进行的锁）。
static bool set_add_bounded(cl_id_set_t *set, const char *id)
{
    if (id[0] == '\0' || set_contains(set, id) || set->count >= set->capacity) {
        return false;
    }
    copy_id(set->ids[set->count++], id);
    return true;
}

static void set_remove(cl_id_set_t *set, const char *id)
{
    int index = set_find(set, id);

    if (index < 0) {
        return;
    }
    set->count--;
    if ((uint8_t)index != set->count) {
        memcpy(set->ids[index], set->ids[set->count], CL_ID_MAX);
    }
    memset(set->ids[set->count], 0, CL_ID_MAX);
    set->next = 0;
}

void cl_state_init(cl_state_t *state)
{
    memset(state, 0, sizeof(*state));
    state->page = CL_PAGE_APPROVALS;
    state->view = CL_VIEW_LIST;
    state->link = CL_LINK_WIFI_CONNECTING;
    set_init(&state->seen, state->seen_ids, CL_SEEN_CAP);
    set_init(&state->resolved, state->resolved_ids, CL_RESOLVED_CAP);
    set_init(&state->inflight, state->inflight_ids, CL_INFLIGHT_CAP);
}

uint8_t cl_state_visible_approvals(const cl_state_t *state, uint8_t indices[CL_MAX_APPROVALS])
{
    uint8_t count = 0;

    for (uint8_t i = 0; i < state->overview.approval_count && i < CL_MAX_APPROVALS; ++i) {
        if (!set_contains(&state->resolved, state->overview.approvals[i].id)) {
            indices[count++] = i;
        }
    }
    return count;
}

bool cl_state_is_inflight(const cl_state_t *state, const char *id)
{
    return id != NULL && set_contains(&state->inflight, id);
}

int64_t cl_state_approval_age_s(const cl_state_t *state, const cl_approval_t *approval,
                                uint64_t now_ms)
{
    int64_t age;

    if (!state->has_overview || !state->overview.server_time_valid || !approval->created_valid) {
        return -1;
    }
    age = state->overview.server_time_s - approval->created_at_s;
    if (now_ms > state->synced_at_ms) {
        age += (int64_t)((now_ms - state->synced_at_ms) / 1000U);
    }
    return age < 0 ? 0 : age;
}

static const cl_approval_t *find_approval(const cl_state_t *state, const char *id)
{
    for (uint8_t i = 0; i < state->overview.approval_count; ++i) {
        if (strncmp(state->overview.approvals[i].id, id, CL_ID_MAX) == 0) {
            return &state->overview.approvals[i];
        }
    }
    return NULL;
}

// 保持选中 ID；若它已不可见，则选中原位置附近的项。
static void fix_approval_selection(cl_state_t *state)
{
    uint8_t visible[CL_MAX_APPROVALS];
    uint8_t count = cl_state_visible_approvals(state, visible);

    if (count == 0) {
        state->approval_sel = 0;
        state->selected_approval_id[0] = '\0';
        return;
    }
    for (uint8_t i = 0; i < count; ++i) {
        if (strncmp(state->overview.approvals[visible[i]].id, state->selected_approval_id,
                    CL_ID_MAX) == 0) {
            state->approval_sel = i;
            return;
        }
    }
    if (state->approval_sel >= count) {
        state->approval_sel = (uint8_t)(count - 1U);
    }
    copy_id(state->selected_approval_id, state->overview.approvals[visible[state->approval_sel]].id);
}

static void select_approval_id(cl_state_t *state, const char *id)
{
    copy_id(state->selected_approval_id, id);
    fix_approval_selection(state);
}

static void set_toast(cl_state_t *state, cl_toast_t toast, uint64_t now_ms)
{
    state->toast = toast;
    state->toast_until_ms = now_ms + CL_TOAST_MS;
}

static void open_detail(cl_state_t *state)
{
    uint8_t visible[CL_MAX_APPROVALS];
    uint8_t count = cl_state_visible_approvals(state, visible);
    const cl_approval_t *approval;

    if (count == 0 || state->approval_sel >= count) {
        return;
    }
    approval = &state->overview.approvals[visible[state->approval_sel]];
    state->detail = *approval;
    state->view = CL_VIEW_DETAIL;
    state->action_focus = CL_DECISION_ALLOW;
    state->detail_scroll = 0;
    state->detail_close_at_ms = 0;
    state->detail_status = cl_state_is_inflight(state, approval->id) ? CL_DETAIL_SENDING
                                                                     : CL_DETAIL_IDLE;
}

static void close_detail(cl_state_t *state)
{
    state->view = CL_VIEW_LIST;
    state->detail_close_at_ms = 0;
    fix_approval_selection(state);
}

static void submit_decision(cl_state_t *state, uint64_t now_ms, cl_actions_t *actions)
{
    cl_decision_t decision = (cl_decision_t)state->action_focus;

    if (state->detail_status != CL_DETAIL_IDLE && state->detail_status != CL_DETAIL_FAILED &&
        state->detail_status != CL_DETAIL_REVOKED) {
        return;
    }
    if (state->revoked) {
        state->detail_status = CL_DETAIL_REVOKED;
        return;
    }
    if (set_contains(&state->resolved, state->detail.id) ||
        cl_state_is_inflight(state, state->detail.id)) {
        return;
    }
    if (!set_add_bounded(&state->inflight, state->detail.id)) {
        set_toast(state, CL_TOAST_QUEUE_FULL, now_ms);
        return;
    }
    state->detail_status = CL_DETAIL_SENDING;
    state->detail_decision = decision;
    actions->resolve = true;
    copy_id(actions->resolve_id, state->detail.id);
    actions->decision = decision;
}

static void key_detail(cl_state_t *state, cl_key_t key, uint64_t now_ms, cl_actions_t *actions)
{
    switch (key) {
    case CL_KEY_UP:
        if (state->action_focus > 0) {
            state->action_focus--;
        }
        break;
    case CL_KEY_DOWN:
        if (state->action_focus + 1U < CL_DECISION_COUNT) {
            state->action_focus++;
        }
        break;
    case CL_KEY_OK:
        if (state->detail_status == CL_DETAIL_DONE ||
            state->detail_status == CL_DETAIL_ELSEWHERE) {
            close_detail(state);
        } else {
            submit_decision(state, now_ms, actions);
        }
        break;
    case CL_KEY_OK_LONG:
        close_detail(state);
        break;
    case CL_KEY_DOWN_LONG:
        state->detail_scroll++;
        break;
    case CL_KEY_UP_LONG:
        return; // 详情页是模态视图：不切页
    }
    actions->render = true;
}

static void key_reset_confirm(cl_state_t *state, cl_key_t key, cl_actions_t *actions)
{
    switch (key) {
    case CL_KEY_UP:
    case CL_KEY_DOWN:
        state->reset_confirm_focus = !state->reset_confirm_focus;
        break;
    case CL_KEY_OK:
        if (state->reset_confirm_focus) {
            actions->reset = true;
        } else {
            state->view = CL_VIEW_LIST;
        }
        break;
    case CL_KEY_OK_LONG:
        state->view = CL_VIEW_LIST;
        break;
    default:
        return;
    }
    actions->render = true;
}

static void move_index(uint8_t *index, uint8_t count, bool down)
{
    if (count == 0) {
        *index = 0;
    } else if (down) {
        if (*index + 1U < count) {
            (*index)++;
        }
    } else if (*index > 0) {
        (*index)--;
    }
}

static void key_list(cl_state_t *state, cl_key_t key, cl_actions_t *actions)
{
    uint8_t visible[CL_MAX_APPROVALS];
    uint8_t count;

    if (key == CL_KEY_UP_LONG) {
        state->page = (cl_page_t)((state->page + 1) % CL_PAGE_COUNT);
        state->user_navigated = true;
        actions->render = true;
        return;
    }
    if (key == CL_KEY_OK_LONG) {
        if (state->page != CL_PAGE_APPROVALS) {
            state->page = CL_PAGE_APPROVALS;
            state->user_navigated = true;
            actions->render = true;
        }
        return;
    }
    if (key == CL_KEY_DOWN_LONG) {
        return;
    }
    switch (state->page) {
    case CL_PAGE_APPROVALS:
        count = cl_state_visible_approvals(state, visible);
        if (key == CL_KEY_OK) {
            open_detail(state);
        } else {
            move_index(&state->approval_sel, count, key == CL_KEY_DOWN);
            if (count > 0) {
                copy_id(state->selected_approval_id,
                        state->overview.approvals[visible[state->approval_sel]].id);
            }
        }
        break;
    case CL_PAGE_TASKS:
        if (key != CL_KEY_OK) {
            move_index(&state->task_sel, state->overview.task_count, key == CL_KEY_DOWN);
        }
        break;
    case CL_PAGE_STATUS:
        if (key == CL_KEY_OK) {
            state->view = CL_VIEW_RESET_CONFIRM;
            state->reset_confirm_focus = false;
        } else {
            move_index(&state->status_row, CL_STATUS_ROWS, key == CL_KEY_DOWN);
        }
        break;
    default:
        break;
    }
    actions->render = true;
}

void cl_state_key(cl_state_t *state, cl_key_t key, uint64_t now_ms, cl_actions_t *actions)
{
    switch (state->view) {
    case CL_VIEW_DETAIL:
        key_detail(state, key, now_ms, actions);
        break;
    case CL_VIEW_RESET_CONFIRM:
        key_reset_confirm(state, key, actions);
        break;
    default:
        key_list(state, key, actions);
        break;
    }
}

static bool overview_content_changed(const cl_overview_t *a, const cl_overview_t *b)
{
    return a->approvals_total != b->approvals_total || a->tasks_total != b->tasks_total ||
           a->approval_count != b->approval_count || a->task_count != b->task_count ||
           strcmp(a->workspace_name, b->workspace_name) != 0 ||
           memcmp(a->approvals, b->approvals, sizeof(a->approvals[0]) * a->approval_count) != 0 ||
           memcmp(a->tasks, b->tasks, sizeof(a->tasks[0]) * a->task_count) != 0;
}

void cl_state_overview(cl_state_t *state, const cl_overview_t *overview, uint64_t now_ms,
                       cl_actions_t *actions)
{
    bool first = !state->has_overview;
    uint8_t visible[CL_MAX_APPROVALS];
    uint8_t count;
    int first_new = -1;

    if (first || overview_content_changed(&state->overview, overview)) {
        state->overview_gen++;
    }
    state->overview = *overview;
    state->has_overview = true;
    state->synced_at_ms = now_ms;
    state->poll_failures = 0;
    state->revoked = false;
    state->link = CL_LINK_OK;
    state->wifi_connected = true;

    if (first && !state->user_navigated) {
        state->page = overview->approvals_total > 0 ? CL_PAGE_APPROVALS : CL_PAGE_TASKS;
    }

    count = cl_state_visible_approvals(state, visible);
    for (uint8_t i = 0; i < count; ++i) {
        const char *id = state->overview.approvals[visible[i]].id;
        if (!set_contains(&state->seen, id)) {
            set_add_fifo(&state->seen, id);
            if (first_new < 0) {
                first_new = i;
            }
        }
    }
    if (first_new >= 0) {
        actions->alert = true;
        if (state->view == CL_VIEW_LIST) {
            state->page = CL_PAGE_APPROVALS;
            select_approval_id(state, state->overview.approvals[visible[first_new]].id);
        }
    }
    fix_approval_selection(state);
    if (state->overview.task_count == 0) {
        state->task_sel = 0;
    } else if (state->task_sel >= state->overview.task_count) {
        state->task_sel = (uint8_t)(state->overview.task_count - 1U);
    }

    if (state->view == CL_VIEW_DETAIL) {
        const cl_approval_t *current = find_approval(state, state->detail.id);
        bool showing_result = state->detail_status == CL_DETAIL_DONE ||
                              state->detail_status == CL_DETAIL_ELSEWHERE ||
                              state->detail_status == CL_DETAIL_SENDING;
        if (current != NULL && !set_contains(&state->resolved, current->id)) {
            state->detail = *current;
        } else if (current == NULL && !showing_result &&
                   !cl_state_is_inflight(state, state->detail.id)) {
            close_detail(state);
            set_toast(state, CL_TOAST_HANDLED_ELSEWHERE, now_ms);
        }
    }
    actions->render = true;
}

void cl_state_poll_failed(cl_state_t *state, cl_poll_error_t error, cl_actions_t *actions)
{
    cl_link_t before = state->link;

    if (error == CL_POLL_UNAUTHORIZED) {
        state->revoked = true;
        state->link = CL_LINK_REVOKED;
    } else {
        if (state->poll_failures < 1000U) {
            state->poll_failures++;
        }
        if (!state->revoked && state->wifi_connected &&
            state->poll_failures >= CL_UNREACHABLE_AFTER_FAILURES) {
            state->link = CL_LINK_UNREACHABLE;
        }
    }
    if (state->link != before) {
        actions->render = true;
    }
}

void cl_state_wifi(cl_state_t *state, bool connected, cl_actions_t *actions)
{
    cl_link_t before = state->link;

    state->wifi_connected = connected;
    if (!state->revoked) {
        if (!connected) {
            state->link = CL_LINK_WIFI_CONNECTING;
        } else if (state->link == CL_LINK_WIFI_CONNECTING) {
            state->link = CL_LINK_OK;
            state->poll_failures = 0;
        }
    }
    if (state->link != before) {
        actions->render = true;
    }
}

void cl_state_resolve_result(cl_state_t *state, const char *id, cl_resolve_outcome_t outcome,
                             uint64_t now_ms, cl_actions_t *actions)
{
    bool is_detail;
    cl_detail_status_t status;

    if (id == NULL || !set_contains(&state->inflight, id)) {
        return; // 未知或重复的结果：锁只由自己发出的提交释放
    }
    set_remove(&state->inflight, id);
    switch (outcome) {
    case CL_RESOLVE_OK:
        set_add_fifo(&state->resolved, id);
        status = CL_DETAIL_DONE;
        actions->poll_now = true;
        break;
    case CL_RESOLVE_CONFLICT:
    case CL_RESOLVE_NOT_FOUND:
        set_add_fifo(&state->resolved, id);
        status = CL_DETAIL_ELSEWHERE;
        actions->poll_now = true;
        break;
    case CL_RESOLVE_UNAUTHORIZED:
        state->revoked = true;
        state->link = CL_LINK_REVOKED;
        status = CL_DETAIL_REVOKED;
        break;
    default:
        status = CL_DETAIL_FAILED;
        break;
    }
    is_detail = state->view == CL_VIEW_DETAIL && strncmp(state->detail.id, id, CL_ID_MAX) == 0;
    if (is_detail) {
        state->detail_status = status;
        if (status == CL_DETAIL_DONE || status == CL_DETAIL_ELSEWHERE) {
            state->detail_close_at_ms = now_ms + CL_DETAIL_RESULT_MS;
        }
    }
    fix_approval_selection(state);
    actions->render = true;
}

void cl_state_tick(cl_state_t *state, uint64_t now_ms, cl_actions_t *actions)
{
    if (state->view == CL_VIEW_DETAIL && state->detail_close_at_ms != 0 &&
        now_ms >= state->detail_close_at_ms) {
        close_detail(state);
        actions->render = true;
    }
    if (state->toast != CL_TOAST_NONE && now_ms >= state->toast_until_ms) {
        state->toast = CL_TOAST_NONE;
        actions->render = true;
    }
}
