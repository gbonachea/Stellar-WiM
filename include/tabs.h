/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef STELLAR_TABS_H
#define STELLAR_TABS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wayland-util.h>
#include <wlr/util/box.h>

struct view;
struct wlr_scene_node;
struct wlr_scene_tree;
struct wlr_scene_rect;

/*
 * A tab group is an optional set of views sharing one visual slot. Only the
 * active member has an enabled scene node; the others stay mapped but hidden.
 * Groups are created and destroyed explicitly and never own the views.
 */
struct tab_group {
	struct wl_list link; /* static list of all groups */
	struct wl_list members; /* struct view::stellar_tab_link */
	struct view *active;
	uint64_t id;

	/* Shared geometry: hidden members are configured to match the slot. */
	struct wlr_box geometry;
	bool geometry_valid;
	bool applying_geometry;

	/* Optional, damage-driven indicator. NULL when tabs are disabled. */
	struct wlr_scene_tree *bar_tree;
	struct wlr_scene_rect *bar_bg;
	struct wl_list bar_segments; /* struct tab_segment.link */
};

struct tab_segment {
	struct wl_list link;
	struct wlr_scene_rect *rect;
	struct view *view; /* member selected by this segment */
	struct tab_group *group;
};

/* Core lifecycle (safe with synthetic views in unit tests) */
void tabs_init(void);
void tabs_finish(void);
struct tab_group *tabs_group_create(void);
bool tabs_group_add(struct tab_group *group, struct view *view);
void tabs_group_remove(struct tab_group *group, struct view *view);
void tabs_group_destroy(struct tab_group *group);
void tabs_destroy_all(void);

size_t tabs_count_groups(void);
const struct wl_list *tabs_groups(void);
size_t tabs_group_size(const struct tab_group *group);
struct view *tabs_group_active_view(const struct tab_group *group);
struct view *tabs_group_next_view(const struct tab_group *group,
	struct view *current);
struct view *tabs_group_prev_view(const struct tab_group *group,
	struct view *current);
struct tab_group *tabs_view_group(const struct view *view);
bool tabs_view_is_hidden(const struct view *view);
bool tabs_group_contains(const struct tab_group *group, const struct view *view);

/* Integration with the compositor (no-ops when tabs are disabled) */
void tabs_set_active_view(struct view *view);
bool tabs_create_group(struct view *a, struct view *b);
/* Group an arbitrary number of views; existing groups are extended. */
bool tabs_group_views(struct view **views, size_t count);
void tabs_switch(struct view *view, bool next);
void tabs_detach(struct view *view);
void tabs_on_view_destroy(struct view *view);
/* Move group focus to another visible member when the active tab is minimized. */
void tabs_on_view_minimize(struct view *view);
void tabs_update_bar(struct tab_group *group);
void tabs_group_apply_geometry(struct tab_group *group, struct wlr_box geo);
void tabs_group_on_view_moved(struct view *view);
void tabs_on_cursor_release(struct wlr_scene_node *node);

#endif /* STELLAR_TABS_H */
