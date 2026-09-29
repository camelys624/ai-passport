// Host tests for the Codeloom reducer: navigation, selection, approval locks,
// 409/401 handling, disappearance, and new-approval alerts.
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "codeloom_state.h"

static cl_state_t s;
static cl_overview_t ov;
static cl_actions_t act;

static void reset_actions(void)
{
    memset(&act, 0, sizeof(act));
}

static void make_overview(int approvals, const char *const *ids, int tasks)
{
    memset(&ov, 0, sizeof(ov));
    ov.server_time_valid = true;
    ov.server_time_s = 1000000;
    ov.approvals_total = (uint32_t)approvals;
    ov.approval_count = (uint8_t)approvals;
    for (int i = 0; i < approvals; ++i) {
        strcpy(ov.approvals[i].id, ids[i]);
        snprintf(ov.approvals[i].title, sizeof(ov.approvals[i].title), "title %s", ids[i]);
        ov.approvals[i].kind = CL_KIND_SHELL;
        ov.approvals[i].created_valid = true;
        ov.approvals[i].created_at_s = 1000000 - 120;
    }
    ov.tasks_total = (uint32_t)tasks;
    ov.task_count = (uint8_t)tasks;
    for (int i = 0; i < tasks; ++i) {
        snprintf(ov.tasks[i].id, sizeof(ov.tasks[i].id), "t%d", i);
        strcpy(ov.tasks[i].title, "task");
    }
}

static void apply(uint64_t now)
{
    reset_actions();
    cl_state_overview(&s, &ov, now, &act);
}

static void key(cl_key_t k, uint64_t now)
{
    reset_actions();
    cl_state_key(&s, k, now, &act);
}

static const char *selected_visible_id(void)
{
    uint8_t v[CL_MAX_APPROVALS];
    uint8_t n = cl_state_visible_approvals(&s, v);
    assert(s.approval_sel < n);
    return s.overview.approvals[v[s.approval_sel]].id;
}

static void test_default_page_and_navigation(void)
{
    cl_state_init(&s);
    assert(s.page == CL_PAGE_APPROVALS && s.link == CL_LINK_WIFI_CONNECTING);

    make_overview(0, NULL, 3);
    apply(0);
    assert(s.page == CL_PAGE_TASKS); // no approvals → tasks is the default page
    assert(!act.alert);

    key(CL_KEY_UP_LONG, 1);
    assert(s.page == CL_PAGE_STATUS && act.render);
    key(CL_KEY_UP_LONG, 2);
    assert(s.page == CL_PAGE_APPROVALS);
    key(CL_KEY_UP_LONG, 3);
    assert(s.page == CL_PAGE_TASKS);

    key(CL_KEY_DOWN, 4);
    key(CL_KEY_DOWN, 5);
    key(CL_KEY_DOWN, 6);
    assert(s.task_sel == 2); // clamped at the last task
    key(CL_KEY_UP, 7);
    assert(s.task_sel == 1);

    // Task list shrinks: selection is clamped.
    make_overview(0, NULL, 1);
    apply(8);
    assert(s.task_sel == 0);
    assert(s.page == CL_PAGE_TASKS); // user navigation is kept

    key(CL_KEY_OK_LONG, 9);
    assert(s.page == CL_PAGE_APPROVALS);
}

static void test_first_sync_with_pending_alerts_once(void)
{
    const char *ids[] = {"apr_a", "apr_b"};

    cl_state_init(&s);
    make_overview(2, ids, 0);
    apply(0);
    assert(act.alert && s.page == CL_PAGE_APPROVALS);
    assert(strcmp(selected_visible_id(), "apr_a") == 0);

    apply(5000);
    assert(!act.alert); // same ids → no repeat alert
}

static void test_new_approval_alert_and_jump(void)
{
    const char *one[] = {"apr_a"};
    const char *two[] = {"apr_a", "apr_b"};

    cl_state_init(&s);
    make_overview(1, one, 2);
    apply(0);
    key(CL_KEY_UP_LONG, 1);
    assert(s.page == CL_PAGE_TASKS);

    make_overview(2, two, 2);
    apply(5000);
    assert(act.alert);
    assert(s.page == CL_PAGE_APPROVALS);
    assert(strcmp(selected_visible_id(), "apr_b") == 0); // jumps to the new item

    // In a detail view, a new approval alerts but does not steal focus.
    key(CL_KEY_OK, 5001);
    assert(s.view == CL_VIEW_DETAIL && strcmp(s.detail.id, "apr_b") == 0);
    {
        const char *three[] = {"apr_a", "apr_b", "apr_c"};
        make_overview(3, three, 2);
        apply(10000);
    }
    assert(act.alert && s.view == CL_VIEW_DETAIL && strcmp(s.detail.id, "apr_b") == 0);

    // An id that disappears and comes back is not "new" within this boot.
    make_overview(1, one, 2);
    apply(15000);
    make_overview(2, two, 2);
    apply(20000);
    assert(!act.alert);
}

static void test_selection_follows_id(void)
{
    const char *abc[] = {"apr_a", "apr_b", "apr_c"};
    const char *ac[] = {"apr_a", "apr_c"};
    const char *xac[] = {"apr_x", "apr_a", "apr_c"};

    cl_state_init(&s);
    make_overview(3, abc, 0);
    apply(0);
    key(CL_KEY_DOWN, 1);
    assert(strcmp(selected_visible_id(), "apr_b") == 0);
    key(CL_KEY_DOWN, 2);
    key(CL_KEY_DOWN, 3);
    assert(strcmp(selected_visible_id(), "apr_c") == 0); // clamped
    key(CL_KEY_UP, 4);

    // Selected id disappears → neighbor at the same position.
    make_overview(2, ac, 0);
    apply(5000);
    assert(strcmp(selected_visible_id(), "apr_c") == 0);

    // Insert before the selection: new id alerts and is selected.
    key(CL_KEY_UP, 5001);
    assert(strcmp(selected_visible_id(), "apr_a") == 0);
    make_overview(3, xac, 0);
    apply(10000);
    assert(act.alert && strcmp(selected_visible_id(), "apr_x") == 0);

    // Everything disappears.
    make_overview(0, NULL, 0);
    apply(15000);
    assert(s.approval_sel == 0 && s.selected_approval_id[0] == '\0');
    key(CL_KEY_OK, 15001);
    assert(s.view == CL_VIEW_LIST); // nothing to open
}

static void open_first_detail(void)
{
    const char *ids[] = {"apr_a", "apr_b"};

    cl_state_init(&s);
    make_overview(2, ids, 0);
    apply(0);
    key(CL_KEY_OK, 1);
    assert(s.view == CL_VIEW_DETAIL && strcmp(s.detail.id, "apr_a") == 0);
    assert(s.action_focus == CL_DECISION_ALLOW);
}

static void test_detail_focus_and_submit_lock(void)
{
    open_first_detail();
    key(CL_KEY_UP, 2);
    assert(s.action_focus == CL_DECISION_ALLOW);
    key(CL_KEY_DOWN, 3);
    assert(s.action_focus == CL_DECISION_ALLOW_ALWAYS);
    key(CL_KEY_DOWN, 4);
    key(CL_KEY_DOWN, 5);
    assert(s.action_focus == CL_DECISION_DENY);
    key(CL_KEY_UP_LONG, 6);
    assert(s.view == CL_VIEW_DETAIL && s.page == CL_PAGE_APPROVALS); // modal
    key(CL_KEY_DOWN_LONG, 7);
    assert(s.detail_scroll == 1);

    key(CL_KEY_OK, 8);
    assert(act.resolve && strcmp(act.resolve_id, "apr_a") == 0);
    assert(act.decision == CL_DECISION_DENY);
    assert(s.detail_status == CL_DETAIL_SENDING && cl_state_is_inflight(&s, "apr_a"));

    // Double submit is impossible while in flight, also after reopening.
    key(CL_KEY_OK, 9);
    assert(!act.resolve);
    key(CL_KEY_OK_LONG, 10);
    assert(s.view == CL_VIEW_LIST);
    key(CL_KEY_OK, 11);
    assert(s.view == CL_VIEW_DETAIL && s.detail_status == CL_DETAIL_SENDING);
    key(CL_KEY_OK, 12);
    assert(!act.resolve);

    // Success: shows result, auto closes, and the id is never offered again.
    reset_actions();
    cl_state_resolve_result(&s, "apr_a", CL_RESOLVE_OK, 100, &act);
    assert(s.detail_status == CL_DETAIL_DONE && act.poll_now && act.render);
    assert(!cl_state_is_inflight(&s, "apr_a"));
    reset_actions();
    cl_state_tick(&s, 100 + CL_DETAIL_RESULT_MS - 1, &act);
    assert(s.view == CL_VIEW_DETAIL);
    cl_state_tick(&s, 100 + CL_DETAIL_RESULT_MS, &act);
    assert(s.view == CL_VIEW_LIST && act.render);
    assert(strcmp(selected_visible_id(), "apr_b") == 0);

    // Server still lists apr_a briefly: it stays hidden and does not alert.
    apply(5000);
    {
        uint8_t v[CL_MAX_APPROVALS];
        assert(cl_state_visible_approvals(&s, v) == 1);
        assert(strcmp(s.overview.approvals[v[0]].id, "apr_b") == 0);
    }
    assert(!act.alert);

    // Duplicate/unknown results are ignored.
    reset_actions();
    cl_state_resolve_result(&s, "apr_a", CL_RESOLVE_FAILED, 6000, &act);
    assert(!act.render);
}

static void test_conflict_is_done_elsewhere(void)
{
    open_first_detail();
    key(CL_KEY_OK, 2);
    assert(act.resolve && act.decision == CL_DECISION_ALLOW);
    reset_actions();
    cl_state_resolve_result(&s, "apr_a", CL_RESOLVE_CONFLICT, 50, &act);
    assert(s.detail_status == CL_DETAIL_ELSEWHERE && act.poll_now);
    key(CL_KEY_OK, 51); // acknowledging closes immediately
    assert(s.view == CL_VIEW_LIST);
    assert(strcmp(selected_visible_id(), "apr_b") == 0);
}

static void test_failure_allows_retry(void)
{
    open_first_detail();
    key(CL_KEY_OK, 2);
    reset_actions();
    cl_state_resolve_result(&s, "apr_a", CL_RESOLVE_FAILED, 50, &act);
    assert(s.detail_status == CL_DETAIL_FAILED && !act.poll_now);
    assert(s.view == CL_VIEW_DETAIL);
    key(CL_KEY_OK, 60);
    assert(act.resolve && strcmp(act.resolve_id, "apr_a") == 0);
}

static void test_unauthorized_revokes(void)
{
    open_first_detail();
    key(CL_KEY_OK, 2);
    reset_actions();
    cl_state_resolve_result(&s, "apr_a", CL_RESOLVE_UNAUTHORIZED, 50, &act);
    assert(s.revoked && s.link == CL_LINK_REVOKED && s.detail_status == CL_DETAIL_REVOKED);
    key(CL_KEY_OK, 60);
    assert(!act.resolve); // no submissions while revoked
    // Wi-Fi flaps do not hide the revoked banner.
    reset_actions();
    cl_state_wifi(&s, false, &act);
    assert(s.link == CL_LINK_REVOKED);
    // A successful poll proves the token works again.
    apply(1000);
    assert(!s.revoked && s.link == CL_LINK_OK);
}

static void test_detail_closes_when_id_disappears(void)
{
    const char *only_b[] = {"apr_b"};

    open_first_detail();
    make_overview(1, only_b, 0);
    apply(5000);
    assert(s.view == CL_VIEW_LIST && s.toast == CL_TOAST_HANDLED_ELSEWHERE);
    assert(strcmp(selected_visible_id(), "apr_b") == 0);
    reset_actions();
    cl_state_tick(&s, 5000 + CL_TOAST_MS, &act);
    assert(s.toast == CL_TOAST_NONE && act.render);

    // While in flight, the detail stays open until the result arrives.
    open_first_detail();
    key(CL_KEY_OK, 2);
    make_overview(1, only_b, 0);
    apply(5000);
    assert(s.view == CL_VIEW_DETAIL && s.detail_status == CL_DETAIL_SENDING);
    assert(strcmp(s.detail.title, "title apr_a") == 0); // snapshot kept
    reset_actions();
    cl_state_resolve_result(&s, "apr_a", CL_RESOLVE_OK, 5100, &act);
    assert(s.detail_status == CL_DETAIL_DONE);
    apply(5200); // result is still shown until its timer
    assert(s.view == CL_VIEW_DETAIL);
}

static void test_detail_refreshes_while_present(void)
{
    open_first_detail();
    strcpy(ov.approvals[0].detail, "updated detail");
    apply(5000);
    assert(s.view == CL_VIEW_DETAIL && strcmp(s.detail.detail, "updated detail") == 0);
}

static void test_inflight_capacity(void)
{
    const char *ids[] = {"a1", "a2", "a3", "a4", "a5"};

    cl_state_init(&s);
    make_overview(5, ids, 0);
    apply(0);
    for (int i = 0; i < CL_INFLIGHT_CAP; ++i) {
        key(CL_KEY_OK, 1);
        key(CL_KEY_OK, 2);
        assert(act.resolve);
        key(CL_KEY_OK_LONG, 3);
        key(CL_KEY_DOWN, 4);
    }
    key(CL_KEY_OK, 5);
    key(CL_KEY_OK, 6);
    assert(!act.resolve && s.toast == CL_TOAST_QUEUE_FULL);
    assert(s.detail_status == CL_DETAIL_IDLE);
}

static void test_link_banners(void)
{
    cl_state_init(&s);
    reset_actions();
    cl_state_wifi(&s, true, &act);
    assert(s.link == CL_LINK_OK && act.render);
    reset_actions();
    cl_state_poll_failed(&s, CL_POLL_UNREACHABLE, &act);
    assert(s.link == CL_LINK_OK && !act.render); // single failure: no banner
    cl_state_poll_failed(&s, CL_POLL_BAD_RESPONSE, &act);
    assert(s.link == CL_LINK_UNREACHABLE && act.render);
    reset_actions();
    cl_state_wifi(&s, false, &act);
    assert(s.link == CL_LINK_WIFI_CONNECTING);
    cl_state_poll_failed(&s, CL_POLL_UNREACHABLE, &act);
    cl_state_poll_failed(&s, CL_POLL_UNREACHABLE, &act);
    assert(s.link == CL_LINK_WIFI_CONNECTING); // Wi-Fi banner wins while offline
    cl_state_wifi(&s, true, &act);
    assert(s.link == CL_LINK_OK && s.poll_failures == 0);
    cl_state_poll_failed(&s, CL_POLL_UNAUTHORIZED, &act);
    assert(s.link == CL_LINK_REVOKED && s.revoked);
    cl_state_wifi(&s, true, &act);
    assert(s.link == CL_LINK_REVOKED);
}

static void test_reset_confirmation(void)
{
    cl_state_init(&s);
    make_overview(0, NULL, 0);
    apply(0);
    key(CL_KEY_UP_LONG, 1);
    assert(s.page == CL_PAGE_STATUS);
    key(CL_KEY_DOWN, 2);
    assert(s.status_row == 1);
    key(CL_KEY_OK, 3);
    assert(s.view == CL_VIEW_RESET_CONFIRM && !s.reset_confirm_focus);
    key(CL_KEY_UP_LONG, 4);
    assert(s.view == CL_VIEW_RESET_CONFIRM && s.page == CL_PAGE_STATUS);
    key(CL_KEY_OK, 5); // default is cancel
    assert(s.view == CL_VIEW_LIST && !act.reset);
    key(CL_KEY_OK, 6);
    key(CL_KEY_DOWN, 7);
    assert(s.reset_confirm_focus);
    key(CL_KEY_OK_LONG, 8);
    assert(s.view == CL_VIEW_LIST && !act.reset);
    key(CL_KEY_OK, 9);
    key(CL_KEY_UP, 10);
    key(CL_KEY_OK, 11);
    assert(act.reset);

    // New approvals alert but do not leave the confirmation dialog.
    cl_state_init(&s);
    make_overview(0, NULL, 0);
    apply(0);
    key(CL_KEY_UP_LONG, 1);
    key(CL_KEY_OK, 2);
    {
        const char *ids[] = {"apr_n"};
        make_overview(1, ids, 0);
        apply(10);
    }
    assert(act.alert && s.view == CL_VIEW_RESET_CONFIRM && s.page == CL_PAGE_STATUS);
}

static void test_age(void)
{
    const char *ids[] = {"apr_a"};

    cl_state_init(&s);
    make_overview(1, ids, 0);
    apply(10000);
    assert(cl_state_approval_age_s(&s, &s.overview.approvals[0], 10000) == 120);
    assert(cl_state_approval_age_s(&s, &s.overview.approvals[0], 70000) == 180);
    s.overview.server_time_valid = false;
    assert(cl_state_approval_age_s(&s, &s.overview.approvals[0], 70000) == -1);
}

int main(void)
{
    test_default_page_and_navigation();
    test_first_sync_with_pending_alerts_once();
    test_new_approval_alert_and_jump();
    test_selection_follows_id();
    test_detail_focus_and_submit_lock();
    test_conflict_is_done_elsewhere();
    test_failure_allows_retry();
    test_unauthorized_revokes();
    test_detail_closes_when_id_disappears();
    test_detail_refreshes_while_present();
    test_inflight_capacity();
    test_link_banners();
    test_reset_confirmation();
    test_age();
    return 0;
}
