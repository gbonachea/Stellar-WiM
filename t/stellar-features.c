// SPDX-License-Identifier: GPL-2.0-only
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <wayland-util.h>
#include <wlr/util/box.h>
#include "config/rcxml.h"
#include "ipc/server.h"
#include "labwc.h"
#include "node.h"
#include "profile.h"
#include "tabs.h"
#include "tiling.h"
#include "view.h"

/* Minimal stubs: these unit tests exercise pure state, not a scene graph. */
struct rcxml rc = {0};
struct server server = {0};
struct ssd;
void effects_reconcile(void);
void ssd_shadow_create(struct ssd *ssd);
void ssd_shadow_update(struct ssd *ssd);
void ssd_shadow_destroy(struct ssd *ssd);

void
desktop_focus_view(struct view *view, bool raise)
{
	(void)view;
	(void)raise;
}

void
view_update_visibility(struct view *view)
{
	(void)view;
}

void
view_move(struct view *view, int x, int y)
{
	(void)view;
	(void)x;
	(void)y;
}

void
view_move_resize(struct view *view, struct wlr_box geo)
{
	(void)view;
	(void)geo;
}

void
view_adjust_size(struct view *view, int *width, int *height)
{
	(void)view;
	(void)width;
	(void)height;
}

/* Encode size hints in creation_id so pure-state tests can exercise hints. */
struct view_size_hints
view_get_size_hints(struct view *view)
{
	switch (view->creation_id) {
	case 101:
		return (struct view_size_hints){ .min_width = 500 };
	case 102:
		return (struct view_size_hints){ .max_width = 300 };
	case 103:
		return (struct view_size_hints){ .min_height = 400 };
	default:
		return (struct view_size_hints){0};
	}
}

bool
view_is_modal_dialog(struct view *view)
{
	(void)view;
	return false;
}

void
stellar_tiling_reapply(void)
{
}

void
effects_reconcile(void)
{
}

void
ssd_shadow_create(struct ssd *ssd)
{
	(void)ssd;
}

void
ssd_shadow_update(struct ssd *ssd)
{
	(void)ssd;
}

void
ssd_shadow_destroy(struct ssd *ssd)
{
	(void)ssd;
}

void
node_descriptor_create(struct wlr_scene_node *node, enum lab_node_type type,
	struct view *view, void *data)
{
	(void)node;
	(void)type;
	(void)view;
	(void)data;
}

struct tab_segment *
node_tab_segment_from_node(struct wlr_scene_node *node)
{
	(void)node;
	return NULL;
}

void
ipc_notify_tabs(const char *event, uint64_t group_id, uint64_t view_id)
{
	(void)event;
	(void)group_id;
	(void)view_id;
}

void
ipc_notify_profile(const char *event, const char *profile)
{
	(void)event;
	(void)profile;
}

static void
box_is_inside(const struct wlr_box *outer, const struct wlr_box *inner)
{
	assert_true(inner->x >= outer->x);
	assert_true(inner->y >= outer->y);
	assert_true(inner->x + inner->width <= outer->x + outer->width);
	assert_true(inner->y + inner->height <= outer->y + outer->height);
	assert_true(inner->width > 0);
	assert_true(inner->height > 0);
}

static void
test_tiling_single_view(void **state)
{
	(void)state;
	struct wlr_box area = { .x = 100, .y = 200, .width = 600, .height = 400 };
	struct wlr_box out[4] = {0};

	stellar_tiling_layout_boxes(&area, 10, 1, out);
	assert_int_equal(out[0].x, 100);
	assert_int_equal(out[0].y, 200);
	assert_int_equal(out[0].width, 600);
	assert_int_equal(out[0].height, 400);
}

static void
test_tiling_two_views(void **state)
{
	(void)state;
	struct wlr_box area = { .x = 100, .y = 200, .width = 600, .height = 400 };
	struct wlr_box out[2] = {0};

	stellar_tiling_layout_boxes(&area, 10, 2, out);
	for (size_t i = 0; i < 2; i++) {
		box_is_inside(&area, &out[i]);
	}
	assert_int_equal(out[0].width, 354);
	assert_int_equal(out[1].width, 236);
	assert_true(out[1].x >= out[0].x + out[0].width);
	assert_int_equal(out[0].y, out[1].y);
	assert_int_equal(out[0].height, out[1].height);
}

static void
test_tiling_three_views(void **state)
{
	(void)state;
	struct wlr_box area = { .x = 0, .y = 0, .width = 800, .height = 600 };
	struct wlr_box out[3] = {0};

	stellar_tiling_layout_boxes(&area, 20, 3, out);
	for (size_t i = 0; i < 3; i++) {
		box_is_inside(&area, &out[i]);
	}
	assert_int_equal(out[0].width, 468);
	assert_int_equal(out[1].width, 312);
	assert_int_equal(out[2].width, 312);
	assert_int_equal(out[1].x, out[2].x);
	assert_true(out[1].y + out[1].height <= out[2].y);
	assert_int_equal(out[0].height, 600);
}

static void
test_tiling_never_overlaps(void **state)
{
	(void)state;
	struct wlr_box area = { .x = 0, .y = 0, .width = 640, .height = 480 };
	struct wlr_box out[8] = {0};

	for (size_t n = 2; n <= 8; n++) {
		stellar_tiling_layout_boxes(&area, 8, n, out);
		for (size_t i = 0; i < n; i++) {
			box_is_inside(&area, &out[i]);
			for (size_t j = i + 1; j < n; j++) {
				bool separated = out[i].x + out[i].width <= out[j].x ||
					out[j].x + out[j].width <= out[i].x ||
					out[i].y + out[i].height <= out[j].y ||
					out[j].y + out[j].height <= out[i].y;
				assert_true(separated);
			}
		}
	}
}

static void
test_tab_group_lifecycle(void **state)
{
	(void)state;
	tabs_init();
	tabs_destroy_all();

	struct view a = {0}, b = {0}, outsider = {0};
	wl_list_init(&a.stellar_tab_link);
	wl_list_init(&b.stellar_tab_link);
	wl_list_init(&outsider.stellar_tab_link);
	a.creation_id = 1;
	b.creation_id = 2;
	outsider.creation_id = 3;

	struct tab_group *group = tabs_group_create();
	assert_non_null(group);
	assert_int_equal(tabs_count_groups(), 1);
	assert_true(tabs_group_add(group, &a));
	assert_true(tabs_group_add(group, &b));
	assert_true(tabs_group_add(group, &a)); /* idempotent */

	assert_int_equal(tabs_group_size(group), 2);
	assert_true(tabs_group_contains(group, &a));
	assert_false(tabs_group_contains(group, &outsider));
	assert_ptr_equal(tabs_group_active_view(group), &a);
	assert_false(tabs_view_is_hidden(&a));
	assert_true(tabs_view_is_hidden(&b));
	assert_ptr_equal(tabs_group_next_view(group, &a), &b);
	assert_ptr_equal(tabs_group_prev_view(group, &a), &b);

	tabs_set_active_view(&b);
	assert_ptr_equal(tabs_group_active_view(group), &b);
	assert_true(tabs_view_is_hidden(&a));
	assert_false(tabs_view_is_hidden(&b));

	tabs_set_active_view(&outsider);
	assert_ptr_equal(tabs_group_active_view(group), &b);
	assert_null(tabs_view_group(&outsider));

	tabs_group_remove(group, &b);
	assert_int_equal(tabs_group_size(group), 1);
	assert_ptr_equal(tabs_group_active_view(group), &a);
	assert_null(b.stellar_tab_group);
	assert_false(tabs_view_is_hidden(&a));

	tabs_group_destroy(group);
	assert_null(a.stellar_tab_group);
	assert_int_equal(tabs_count_groups(), 0);

	tabs_finish();
}

static void
test_tab_group_destroy_clears_members(void **state)
{
	(void)state;
	struct view a = {0}, b = {0};
	wl_list_init(&a.stellar_tab_link);
	wl_list_init(&b.stellar_tab_link);

	struct tab_group *group = tabs_group_create();
	assert_non_null(group);
	tabs_group_add(group, &a);
	tabs_group_add(group, &b);
	tabs_group_destroy(group);

	assert_null(a.stellar_tab_group);
	assert_null(b.stellar_tab_group);
	assert_int_equal(tabs_count_groups(), 0);
}

static void
test_tab_group_three_members(void **state)
{
	(void)state;
	struct view a = {0}, b = {0}, c = {0};
	wl_list_init(&a.stellar_tab_link);
	wl_list_init(&b.stellar_tab_link);
	wl_list_init(&c.stellar_tab_link);

	struct tab_group *group = tabs_group_create();
	assert_non_null(group);
	assert_true(tabs_group_add(group, &a));
	assert_true(tabs_group_add(group, &b));
	assert_true(tabs_group_add(group, &c));
	assert_int_equal(tabs_group_size(group), 3);
	assert_true(tabs_group_add(group, &a)); /* idempotent */
	assert_int_equal(tabs_group_size(group), 3);
	assert_true(tabs_group_contains(group, &c));
	assert_ptr_equal(tabs_group_next_view(group, &a), &b);
	assert_ptr_equal(tabs_group_next_view(group, &c), &a);
	assert_ptr_equal(tabs_group_prev_view(group, &a), &c);
	tabs_group_destroy(group);
	assert_int_equal(tabs_count_groups(), 0);
}

static void
test_tab_group_size_constraints(void **state)
{
	(void)state;
	struct view a = { .creation_id = 101 }, b = { .creation_id = 102 };
	struct view c = { .creation_id = 103 };
	wl_list_init(&a.stellar_tab_link);
	wl_list_init(&b.stellar_tab_link);
	wl_list_init(&c.stellar_tab_link);

	struct tab_group *group = tabs_group_create();
	assert_non_null(group);
	assert_true(tabs_group_add(group, &a));
	assert_true(tabs_group_add(group, &b));
	assert_true(tabs_group_add(group, &c));

	/* Incompatible min/max: the mandatory minimum must win. */
	tabs_group_apply_geometry(group,
		(struct wlr_box){ .x = 10, .y = 20, .width = 400, .height = 300 });
	assert_int_equal(group->geometry.x, 10);
	assert_int_equal(group->geometry.y, 20);
	assert_int_equal(group->geometry.width, 500);
	assert_int_equal(group->geometry.height, 400);

	tabs_group_destroy(group);
}

static void
test_tab_group_minimize_active(void **state)
{
	(void)state;
	struct view a = { .creation_id = 1 }, b = { .creation_id = 2 };
	wl_list_init(&a.stellar_tab_link);
	wl_list_init(&b.stellar_tab_link);

	struct tab_group *group = tabs_group_create();
	assert_non_null(group);
	assert_true(tabs_group_add(group, &a));
	assert_true(tabs_group_add(group, &b));
	rc.stellar.tabs_enabled = true;

	a.minimized = true;
	tabs_on_view_minimize(&a);
	assert_ptr_equal(group->active, &b);
	assert_false(tabs_view_is_hidden(&b));

	a.minimized = false;
	rc.stellar.tabs_enabled = false;
	tabs_group_destroy(group);
}

static void
test_tab_group_destroy_on_last_removal(void **state)
{
	(void)state;
	struct view a = {0}, b = {0};
	wl_list_init(&a.stellar_tab_link);
	wl_list_init(&b.stellar_tab_link);

	struct tab_group *group = tabs_group_create();
	tabs_group_add(group, &a);
	tabs_group_add(group, &b);
	tabs_on_view_destroy(&a);
	assert_int_equal(tabs_count_groups(), 0);
	assert_null(a.stellar_tab_group);
	assert_null(b.stellar_tab_group); /* one-member groups are dissolved */

	tabs_destroy_all();
}

static void
test_profile_parse_and_names(void **state)
{
	(void)state;
	enum stellar_profile profile = STELLAR_PROFILE_CUSTOM;

	assert_true(stellar_profile_parse("minimal", &profile));
	assert_int_equal(profile, STELLAR_PROFILE_MINIMAL);
	assert_true(stellar_profile_parse("  ENHANCED  ", &profile));
	assert_int_equal(profile, STELLAR_PROFILE_ENHANCED);
	assert_true(stellar_profile_parse("Custom", &profile));
	assert_int_equal(profile, STELLAR_PROFILE_CUSTOM);
	assert_false(stellar_profile_parse("bogus", &profile));
	assert_false(stellar_profile_parse("", &profile));
	assert_false(stellar_profile_parse(NULL, &profile));

	assert_string_equal(stellar_profile_name(STELLAR_PROFILE_MINIMAL), "minimal");
	assert_string_equal(stellar_profile_name(STELLAR_PROFILE_ENHANCED), "enhanced");
	assert_string_equal(stellar_profile_name(STELLAR_PROFILE_CUSTOM), "custom");
}

static void
test_profile_runtime_switch(void **state)
{
	(void)state;
	/* reconcile_safe_state() walks the view list; make the stub a valid list. */
	wl_list_init(&server.views);
	rc.stellar.profile = STELLAR_PROFILE_CUSTOM;
	rc.stellar.tabs_enabled = false;
	rc.stellar.tiling_enabled = false;

	assert_false(stellar_profile_set(STELLAR_PROFILE_CUSTOM));
	assert_int_equal(stellar_profile_current(), STELLAR_PROFILE_CUSTOM);

	assert_true(stellar_profile_set(STELLAR_PROFILE_ENHANCED));
	assert_int_equal(stellar_profile_current(), STELLAR_PROFILE_ENHANCED);
	assert_true(rc.stellar.tabs_enabled);
	assert_true(rc.stellar.tiling_enabled);
	assert_true(rc.stellar.animations_enabled);
	assert_true(rc.stellar.opacity_enabled);
	assert_true(rc.shadows_enabled);

	assert_true(stellar_profile_set(STELLAR_PROFILE_MINIMAL));
	assert_int_equal(stellar_profile_current(), STELLAR_PROFILE_MINIMAL);
	assert_false(rc.stellar.tabs_enabled);
	assert_false(rc.stellar.tiling_enabled);
	assert_false(rc.stellar.animations_enabled);
	assert_false(rc.stellar.opacity_enabled);
	assert_false(rc.shadows_enabled);

	rc.stellar.profile = STELLAR_PROFILE_ENHANCED;
	rc.stellar.tabs_enabled = true;
	rc.stellar.tiling_enabled = true;
	stellar_profile_reconcile();
	assert_true(rc.stellar.tabs_enabled);
	assert_true(rc.stellar.tiling_enabled);

	rc.stellar.profile = STELLAR_PROFILE_CUSTOM;
	rc.stellar.tabs_enabled = false;
	rc.stellar.tiling_enabled = false;
}

static void
test_tiling_layout_modes(void **state)
{
	(void)state;
	struct wlr_box area = { .x = 0, .y = 0, .width = 600, .height = 400 };
	struct wlr_box out[4] = {0};

	stellar_tiling_layout_boxes_mode(&area, 10, 3,
		STELLAR_TILING_HORIZONTAL, 0.6f, 2, out);
	assert_int_equal(out[0].x, 0);
	assert_int_equal(out[1].x, 203);
	assert_int_equal(out[2].x, 406);
	assert_int_equal(out[2].width, 194);

	stellar_tiling_layout_boxes_mode(&area, 10, 2,
		STELLAR_TILING_VERTICAL, 0.6f, 2, out);
	assert_int_equal(out[0].y, 0);
	assert_int_equal(out[0].height, 195);
	assert_int_equal(out[1].y, 205);
	assert_int_equal(out[1].height, 195);

	stellar_tiling_layout_boxes_mode(&area, 10, 4,
		STELLAR_TILING_GRID, 0.6f, 2, out);
	assert_int_equal(out[0].width, 295);
	assert_int_equal(out[0].height, 195);
	assert_int_equal(out[1].x, 305);
	assert_int_equal(out[2].y, 205);
	assert_int_equal(out[3].x, 305);
	assert_int_equal(out[3].y, 205);

	stellar_tiling_layout_boxes_mode(&area, 10, 3,
		STELLAR_TILING_HYBRID, 0.6f, 1, out);
	assert_int_equal(out[0].width, 354);
	assert_int_equal(out[1].x, 364);
	assert_int_equal(out[1].height, 195);
	assert_int_equal(out[2].x, 364);
	assert_int_equal(out[2].y, 205);

	enum stellar_tiling_mode mode = STELLAR_TILING_MASTER_STACK;
	assert_true(stellar_tiling_parse_mode("grid", &mode));
	assert_int_equal(mode, STELLAR_TILING_GRID);
	assert_true(stellar_tiling_parse_mode("master-stack", &mode));
	assert_int_equal(mode, STELLAR_TILING_MASTER_STACK);
	assert_false(stellar_tiling_parse_mode("bogus", &mode));
	assert_string_equal(stellar_tiling_mode_name(STELLAR_TILING_HYBRID),
		"hybrid");
}

int
main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_tiling_single_view),
		cmocka_unit_test(test_tiling_two_views),
		cmocka_unit_test(test_tiling_three_views),
		cmocka_unit_test(test_tiling_never_overlaps),
		cmocka_unit_test(test_tiling_layout_modes),
		cmocka_unit_test(test_tab_group_lifecycle),
		cmocka_unit_test(test_tab_group_destroy_clears_members),
		cmocka_unit_test(test_tab_group_three_members),
		cmocka_unit_test(test_tab_group_size_constraints),
		cmocka_unit_test(test_tab_group_minimize_active),
		cmocka_unit_test(test_tab_group_destroy_on_last_removal),
		cmocka_unit_test(test_profile_parse_and_names),
		cmocka_unit_test(test_profile_runtime_switch),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
