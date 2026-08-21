/*----------------------
 | test_menu_state.cxx
 | Description: Host unit tests for the menu screen state machine. It is pure
 |   logic by design, so every transition the player can reach is covered here
 |   rather than on hardware.
 | Author: suinevere
 | Dependencies: menu_state.h
 ----------------------*/
#include <cstdint>
#include "menu_state.h"

/* Neither <cstdio> nor <cstring> is included here: menu_state.h pulls in
   savedata.h and intern.h, which already declare printf (via saturn_compat.h)
   and strcmp/memset (via its own extern "C" <string.h>) -- including the C++
   wrappers on top collides with saturn_compat.h's FILE typedef. See
   savedata.h and intern.h for the full story. */

static int g_fail = 0;

#define CHECK_EQ(actual, expected)                                            \
    do {                                                                      \
        long long a_ = (long long)(actual);                                   \
        long long e_ = (long long)(expected);                                 \
        if (a_ != e_) {                                                       \
            g_fail++;                                                         \
            printf("FAIL %s:%d  %s\n  actual   = %lld\n  expected = %lld\n",  \
                   __FILE__, __LINE__, #actual, a_, e_);                      \
        }                                                                     \
    } while (0)

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            g_fail++;                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                     \
    } while (0)

static MenuInput NONE;

static MenuInput press(const char *what)
{
    MenuInput in;
    memset(&in, 0, sizeof(in));
    if (strcmp(what, "up") == 0) in.up = true;
    if (strcmp(what, "down") == 0) in.down = true;
    if (strcmp(what, "left") == 0) in.left = true;
    if (strcmp(what, "right") == 0) in.right = true;
    if (strcmp(what, "confirm") == 0) in.confirm = true;
    if (strcmp(what, "cancel") == 0) in.cancel = true;
    if (strcmp(what, "pause") == 0) in.pause = true;
    return in;
}

static void freshTitle(MenuState *st)
{
    memset(st, 0, sizeof(*st));
    menuStateEnterTitle(st);
}

static void test_title_starts_on_start_game(void)
{
    MenuState st;
    freshTitle(&st);
    CHECK_EQ(st.screen, MENU_TITLE);
    CHECK_EQ(st.cursor, 0);
}

static void test_title_confirm_starts_game(void)
{
    MenuState st;
    freshTitle(&st);
    MenuInput in = press("confirm");
    CHECK_EQ(menuStateStep(&st, &in), MENU_ACT_START_GAME);
}

static void test_title_cursor_wraps(void)
{
    MenuState st;
    freshTitle(&st);
    MenuInput up = press("up");
    menuStateStep(&st, &up);
    CHECK_EQ(st.cursor, 1);
    MenuInput down = press("down");
    menuStateStep(&st, &down);
    CHECK_EQ(st.cursor, 0);
}

static void test_title_load_opens_slots_in_load_mode(void)
{
    MenuState st;
    freshTitle(&st);
    MenuInput down = press("down");
    menuStateStep(&st, &down);
    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_RESCAN_SLOTS);
    CHECK_EQ(st.screen, MENU_SLOTS);
    CHECK(!st.saving);
}

static void test_pause_starts_on_resume(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    CHECK_EQ(st.screen, MENU_PAUSE);
    CHECK_EQ(st.cursor, 0);
    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_RESUME);
}

static void test_pause_button_resumes(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    MenuInput in = press("pause");
    CHECK_EQ(menuStateStep(&st, &in), MENU_ACT_RESUME);
}

static void test_pause_cancel_resumes(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    MenuInput in = press("cancel");
    CHECK_EQ(menuStateStep(&st, &in), MENU_ACT_RESUME);
}

static void test_return_to_menu_asks_for_confirmation(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    MenuInput down = press("down");
    for (int i = 0; i < 4; ++i) {
        menuStateStep(&st, &down);
    }
    CHECK_EQ(st.cursor, 4);
    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_NONE);
    CHECK_EQ(st.screen, MENU_CONFIRM);
    CHECK(!st.confirmYes);
}

static void test_confirm_defaults_to_no(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    MenuInput down = press("down");
    for (int i = 0; i < 4; ++i) {
        menuStateStep(&st, &down);
    }
    MenuInput confirm = press("confirm");
    menuStateStep(&st, &confirm);
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_NONE);
    CHECK_EQ(st.screen, MENU_PAUSE);
}

static void test_confirm_yes_returns_to_title(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    MenuInput down = press("down");
    for (int i = 0; i < 4; ++i) {
        menuStateStep(&st, &down);
    }
    MenuInput confirm = press("confirm");
    menuStateStep(&st, &confirm);
    MenuInput left = press("left");
    menuStateStep(&st, &left);
    CHECK(st.confirmYes);
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_RETURN_TO_TITLE);
}

static void test_save_over_empty_slot_needs_no_confirmation(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    st.screen = MENU_SLOTS;
    st.saving = true;
    st.slots[0].state = SLOT_EMPTY;
    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_SAVE_SLOT);
}

static void test_save_over_used_slot_asks_first(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    st.screen = MENU_SLOTS;
    st.saving = true;
    st.slots[0].state = SLOT_OK;
    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_NONE);
    CHECK_EQ(st.screen, MENU_CONFIRM);
    MenuInput left = press("left");
    menuStateStep(&st, &left);
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_SAVE_SLOT);
}

static void test_confirmed_overwrite_stays_on_the_slot_list(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    st.screen = MENU_SLOTS;
    st.saving = true;
    st.slots[0].state = SLOT_OK;

    MenuInput confirm = press("confirm");
    menuStateStep(&st, &confirm);
    CHECK_EQ(st.screen, MENU_CONFIRM);

    MenuInput left = press("left");
    menuStateStep(&st, &left);
    CHECK(st.confirmYes);

    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_SAVE_SLOT);
    CHECK_EQ(st.screen, MENU_SLOTS);
}

static void test_declined_overwrite_also_stays_on_the_slot_list(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    st.screen = MENU_SLOTS;
    st.saving = true;
    st.slots[0].state = SLOT_OK;

    MenuInput confirm = press("confirm");
    menuStateStep(&st, &confirm);
    CHECK_EQ(st.screen, MENU_CONFIRM);

    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_NONE);
    CHECK_EQ(st.screen, MENU_SLOTS);
}

static void test_load_refuses_empty_and_damaged_slots(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterTitle(&st);
    st.screen = MENU_SLOTS;
    st.saving = false;

    st.slots[0].state = SLOT_EMPTY;
    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_NONE);
    CHECK_EQ(st.screen, MENU_SLOTS);

    st.slots[0].state = SLOT_DAMAGED;
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_NONE);

    st.slots[0].state = SLOT_OLD_VERSION;
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_NONE);

    st.slots[0].state = SLOT_OK;
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_LOAD_SLOT);
}

static void test_save_over_damaged_slot_is_allowed(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    st.screen = MENU_SLOTS;
    st.saving = true;
    st.slots[0].state = SLOT_DAMAGED;
    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_NONE);
    CHECK_EQ(st.screen, MENU_CONFIRM);
}

static void test_device_toggle_only_when_cart_present(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterTitle(&st);
    st.screen = MENU_SLOTS;
    st.device = 1;
    st.cartPresent = false;

    MenuInput right = press("right");
    CHECK_EQ(menuStateStep(&st, &right), MENU_ACT_NONE);
    CHECK_EQ(st.device, 1);

    st.cartPresent = true;
    CHECK_EQ(menuStateStep(&st, &right), MENU_ACT_RESCAN_SLOTS);
    CHECK_EQ(st.device, 2);
    CHECK_EQ(menuStateStep(&st, &right), MENU_ACT_RESCAN_SLOTS);
    CHECK_EQ(st.device, 1);
}

static void test_slot_cancel_goes_back_to_the_opening_screen(void)
{
    MenuState st;
    freshTitle(&st);
    MenuInput down = press("down");
    menuStateStep(&st, &down);
    MenuInput confirm = press("confirm");
    menuStateStep(&st, &confirm);
    CHECK_EQ(st.screen, MENU_SLOTS);
    MenuInput cancel = press("cancel");
    menuStateStep(&st, &cancel);
    CHECK_EQ(st.screen, MENU_TITLE);

    MenuState p;
    memset(&p, 0, sizeof(p));
    menuStateEnterPause(&p);
    menuStateStep(&p, &down);
    menuStateStep(&p, &confirm);
    CHECK_EQ(p.screen, MENU_SLOTS);
    menuStateStep(&p, &cancel);
    CHECK_EQ(p.screen, MENU_PAUSE);
}

static void test_save_over_used_slot_decline_stays_on_slots(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    st.screen = MENU_SLOTS;
    st.saving = true;
    st.slots[0].state = SLOT_OK;
    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_NONE);
    CHECK_EQ(st.screen, MENU_CONFIRM);
    CHECK(!st.confirmYes);
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_NONE);
    CHECK_EQ(st.screen, MENU_SLOTS);
}

static void test_confirm_cancel_declines_return_to_title(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    MenuInput down = press("down");
    for (int i = 0; i < 4; ++i) {
        menuStateStep(&st, &down);
    }
    MenuInput confirm = press("confirm");
    menuStateStep(&st, &confirm);
    CHECK_EQ(st.screen, MENU_CONFIRM);
    MenuInput cancel = press("cancel");
    CHECK_EQ(menuStateStep(&st, &cancel), MENU_ACT_NONE);
    CHECK_EQ(st.screen, MENU_PAUSE);
}

static void test_confirm_cancel_declines_save_overwrite(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    st.screen = MENU_SLOTS;
    st.saving = true;
    st.slots[0].state = SLOT_DAMAGED;
    MenuInput confirm = press("confirm");
    menuStateStep(&st, &confirm);
    CHECK_EQ(st.screen, MENU_CONFIRM);
    MenuInput cancel = press("cancel");
    CHECK_EQ(menuStateStep(&st, &cancel), MENU_ACT_NONE);
    CHECK_EQ(st.screen, MENU_SLOTS);
}

static void test_empty_input_does_nothing(void)
{
    MenuState st;
    freshTitle(&st);
    memset(&NONE, 0, sizeof(NONE));
    CHECK_EQ(menuStateStep(&st, &NONE), MENU_ACT_NONE);
    CHECK_EQ(st.cursor, 0);
    CHECK_EQ(st.screen, MENU_TITLE);
}

static void test_pause_cursor_wraps_across_five_rows(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);

    MenuInput up = press("up");
    menuStateStep(&st, &up);
    CHECK_EQ(st.cursor, 4);

    MenuInput down = press("down");
    menuStateStep(&st, &down);
    CHECK_EQ(st.cursor, 0);
}

static void test_buttons_row_toggles_on_confirm(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    st.cursor = 3;

    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_TOGGLE_BUTTONS);
    CHECK(st.swapButtons);
    CHECK_EQ(st.screen, MENU_PAUSE);
    CHECK_EQ(st.cursor, 3);

    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_TOGGLE_BUTTONS);
    CHECK(!st.swapButtons);
}

static void test_left_and_right_do_not_toggle_the_buttons_row(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    st.cursor = 3;

    MenuInput left = press("left");
    CHECK_EQ(menuStateStep(&st, &left), MENU_ACT_NONE);
    CHECK(!st.swapButtons);
    CHECK_EQ(st.cursor, 3);

    MenuInput right = press("right");
    CHECK_EQ(menuStateStep(&st, &right), MENU_ACT_NONE);
    CHECK(!st.swapButtons);
    CHECK_EQ(st.cursor, 3);
}

static void test_left_and_right_do_nothing_off_the_buttons_row(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);

    MenuInput left = press("left");
    CHECK_EQ(menuStateStep(&st, &left), MENU_ACT_NONE);
    CHECK(!st.swapButtons);
    CHECK_EQ(st.cursor, 0);
}

static void test_entering_a_screen_preserves_the_button_layout(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    st.swapButtons = true;

    menuStateEnterPause(&st);
    CHECK(st.swapButtons);

    menuStateEnterTitle(&st);
    CHECK(st.swapButtons);

    menuStateEnterLoad(&st, MENU_TITLE);
    CHECK(st.swapButtons);
}

static void freshDeath(MenuState *st)
{
    memset(st, 0, sizeof(*st));
    menuStateEnterDeath(st);
}

static void test_death_starts_on_resume(void)
{
    MenuState st;
    freshDeath(&st);
    CHECK_EQ(st.screen, MENU_DEATH);
    CHECK_EQ(st.cursor, 0);
}

static void test_death_resume_row(void)
{
    MenuState st;
    freshDeath(&st);
    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_RETRY);
}

static void test_death_save_and_resume_row(void)
{
    MenuState st;
    freshDeath(&st);
    st.cursor = 1;
    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_SAVE_RETRY);
}

static void test_death_save_and_quit_row(void)
{
    MenuState st;
    freshDeath(&st);
    st.cursor = 3;
    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_SAVE_AND_QUIT);
}

static void test_death_quit_row(void)
{
    MenuState st;
    freshDeath(&st);
    st.cursor = 4;
    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_RETURN_TO_TITLE);
}

static void test_death_cancel_resumes_from_any_row(void)
{
    for (int row = 0; row < 5; ++row) {
        MenuState st;
        freshDeath(&st);
        st.cursor = row;
        MenuInput cancel = press("cancel");
        CHECK_EQ(menuStateStep(&st, &cancel), MENU_ACT_RETRY);
    }
}

static void test_death_cursor_wraps_across_five_rows(void)
{
    MenuState st;
    freshDeath(&st);

    MenuInput up = press("up");
    menuStateStep(&st, &up);
    CHECK_EQ(st.cursor, 4);

    MenuInput down = press("down");
    menuStateStep(&st, &down);
    CHECK_EQ(st.cursor, 0);
}

static void test_death_down_reaches_every_row(void)
{
    MenuState st;
    freshDeath(&st);
    MenuInput down = press("down");

    for (int expected = 1; expected <= 4; ++expected) {
        menuStateStep(&st, &down);
        CHECK_EQ(st.cursor, expected);
    }

    menuStateStep(&st, &down);
    CHECK_EQ(st.cursor, 0);
}

static void test_death_load_opens_the_slot_list_in_load_mode(void)
{
    MenuState st;
    freshDeath(&st);
    st.cursor = 2;

    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_RESCAN_SLOTS);
    CHECK_EQ(st.screen, MENU_SLOTS);
    CHECK(!st.saving);
    CHECK_EQ(st.slotCursor, 0);
}

static void test_slot_cancel_returns_to_the_death_menu(void)
{
    MenuState st;
    freshDeath(&st);
    st.cursor = 2;

    MenuInput confirm = press("confirm");
    menuStateStep(&st, &confirm);
    CHECK_EQ(st.screen, MENU_SLOTS);

    MenuInput cancel = press("cancel");
    CHECK_EQ(menuStateStep(&st, &cancel), MENU_ACT_NONE);
    CHECK_EQ(st.screen, MENU_DEATH);
}

int main(void)
{
    test_title_starts_on_start_game();
    test_title_confirm_starts_game();
    test_title_cursor_wraps();
    test_title_load_opens_slots_in_load_mode();
    test_pause_starts_on_resume();
    test_pause_button_resumes();
    test_pause_cancel_resumes();
    test_pause_cursor_wraps_across_five_rows();
    test_buttons_row_toggles_on_confirm();
    test_left_and_right_do_not_toggle_the_buttons_row();
    test_left_and_right_do_nothing_off_the_buttons_row();
    test_entering_a_screen_preserves_the_button_layout();
    test_death_starts_on_resume();
    test_death_resume_row();
    test_death_save_and_resume_row();
    test_death_save_and_quit_row();
    test_death_quit_row();
    test_death_cancel_resumes_from_any_row();
    test_death_cursor_wraps_across_five_rows();
    test_death_down_reaches_every_row();
    test_death_load_opens_the_slot_list_in_load_mode();
    test_slot_cancel_returns_to_the_death_menu();
    test_return_to_menu_asks_for_confirmation();
    test_confirm_defaults_to_no();
    test_confirm_yes_returns_to_title();
    test_save_over_empty_slot_needs_no_confirmation();
    test_save_over_used_slot_asks_first();
    test_save_over_used_slot_decline_stays_on_slots();
    test_confirm_cancel_declines_return_to_title();
    test_confirm_cancel_declines_save_overwrite();
    test_confirmed_overwrite_stays_on_the_slot_list();
    test_declined_overwrite_also_stays_on_the_slot_list();
    test_load_refuses_empty_and_damaged_slots();
    test_save_over_damaged_slot_is_allowed();
    test_device_toggle_only_when_cart_present();
    test_slot_cancel_goes_back_to_the_opening_screen();
    test_empty_input_does_nothing();

    if (g_fail == 0) {
        printf("all tests passed\n");
        return 0;
    }
    printf("%d check(s) failed\n", g_fail);
    return 1;
}
