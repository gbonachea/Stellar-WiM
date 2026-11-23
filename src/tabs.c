// SPDX-License-Identifier: GPL-2.0-only
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-util.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include "config/rcxml.h"
#include "ipc/server.h"
#include "labwc.h"
#include "node.h"
#include "tabs.h"
#include "tiling.h"
#include "view.h"

#define TAB_BAR_HEIGHT 18

static struct wl_list groups = { &groups, &groups };
static uint64_t next_group_id = 1;

static const float bar_bg_color[4] = {0.12f, 0.14f, 0.18f, 0.90f};
static const float active_color[4] = {0.35f, 0.55f, 0.90f, 1.00f};
static const float inactive_color[4] = {0.22f, 0.25f, 0.30f, 1.00f};

void
tabs_init(void)
{
	/* The group list is statically initialized; keep the hook for symmetry. */
}

size_t
tabs_count_groups(void)
{
	size_t count = 0;
	struct tab_group *group;
	wl_list_for_each(group, &groups, link) {
		count++;
	}
	return count;
}

const struct wl_list *
tabs_groups(void)
{
	return &groups;
}

size_t
tabs_group_size(const struct tab_group *group)
{
	size_t count = 0;
	if (!group) {
		return 0;
	}
	struct view *view;
	wl_list_for_each(view, &group->members, stellar_tab_link) {
		count++;
	}
	return count;
}

struct view *
tabs_group_active_view(const struct tab_group *group)
{
	return group ? group->active : NULL;
}

struct tab_group *
tabs_view_group(const struct view *view)
{
	return view ? view->stellar_tab_group : NULL;
}

bool
tabs_view_is_hidden(const struct view *view)
{
	struct tab_group *group = tabs_view_group(view);
	return group && group->active && group->active != view;
}

bool
tabs_group_contains(const struct tab_group *group, const struct view *view)
{
	return group && view && view->stellar_tab_group == group;
}

static struct view *
nth_member(const struct tab_group *group, size_t index)
{
	size_t i = 0;
	struct view *view;
	wl_list_for_each(view, &group->members, stellar_tab_link) {
		if (i++ == index) {
			return view;
		}
	}
	return NULL;
}

struct view *
tabs_group_next_view(const struct tab_group *group, struct view *current)
{
	if (!group || wl_list_empty(&group->members)) {
		return NULL;
	}
	if (!current || !tabs_group_contains(group, current)) {
		current = group->active;
	}
	if (!current) {
		return nth_member(group, 0);
	}
	struct wl_list *link = current->stellar_tab_link.next;
	if (link == &group->members) {
		link = group->members.next;
	}
	return wl_container_of(link, current, stellar_tab_link);
}

struct view *
tabs_group_prev_view(const struct tab_group *group, struct view *current)
{
	if (!group || wl_list_empty(&group->members)) {
		return NULL;
	}
	if (!current || !tabs_group_contains(group, current)) {
		current = group->active;
	}
	if (!current) {
		return nth_member(group, tabs_group_size(group) - 1);
	}
	struct wl_list *link = current->stellar_tab_link.prev;
	if (link == &group->members) {
		link = group->members.prev;
	}
	return wl_container_of(link, current, stellar_tab_link);
}

struct tab_group *
tabs_group_create(void)
{
	struct tab_group *group = calloc(1, sizeof(*group));
	if (!group) {
		wlr_log(WLR_ERROR, "stellar tabs: out of memory");
		return NULL;
	}
	group->id = next_group_id++;
	wl_list_init(&group->members);
	wl_list_init(&group->bar_segments);
	wl_list_insert(&groups, &group->link);
	return group;
}

bool
tabs_group_add(struct tab_group *group, struct view *view)
{
	if (!group || !view) {
		return false;
	}
	if (view->stellar_tab_group == group) {
		return true;
	}
	if (view->stellar_tab_group) {
		return false;
	}
	view->stellar_tab_group = group;
	/* Preserve insertion order so tab cycling follows grouping order. */
	wl_list_insert(group->members.prev, &view->stellar_tab_link);
	if (!group->active) {
		group->active = view;
	}
	return true;
}

void
tabs_group_remove(struct tab_group *group, struct view *view)
{
	if (!group || !view || view->stellar_tab_group != group) {
		return;
	}
	bool was_active = group->active == view;
	wl_list_remove(&view->stellar_tab_link);
	wl_list_init(&view->stellar_tab_link);
	view->stellar_tab_group = NULL;

	if (was_active) {
		group->active = wl_list_empty(&group->members)
			? NULL : nth_member(group, 0);
	}
}

/*
 * Members can advertise incompatible size constraints (a terminal locked to
 * character cells, an editor with a large minimum, a client maximum). Find a
 * size that satisfies as many of them as possible instead of repeatedly
 * configuring hidden members with a size they will reject.
 */
static void
group_adjust_size(struct tab_group *group, int *width, int *height)
{
	/*
	 * Combine the hints of all members. Mandatory minimum sizes win over
	 * optional maximums when a client advertises an impossible range.
	 */
	int min_w = 1, min_h = 1;
	int max_w = INT_MAX, max_h = INT_MAX;
	struct view *member;
	wl_list_for_each(member, &group->members, stellar_tab_link) {
		struct view_size_hints hints = view_get_size_hints(member);
		if (hints.min_width > 0) {
			min_w = MAX(min_w, hints.min_width);
		}
		if (hints.min_height > 0) {
			min_h = MAX(min_h, hints.min_height);
		}
		if (hints.max_width > 0) {
			max_w = MIN(max_w, hints.max_width);
		}
		if (hints.max_height > 0) {
			max_h = MIN(max_h, hints.max_height);
		}
	}

	int w = MAX(*width, min_w);
	int h = MAX(*height, min_h);
	if (max_w >= min_w) {
		w = MIN(w, max_w);
	}
	if (max_h >= min_h) {
		h = MIN(h, max_h);
	}

	/* Snapping (terminal cell size, X11 increments) can only grow a size. */
	wl_list_for_each(member, &group->members, stellar_tab_link) {
		int mw = w, mh = h;
		view_adjust_size(member, &mw, &mh);
		w = MAX(w, mw);
		h = MAX(h, mh);
		if (max_w >= min_w) {
			w = MIN(w, max_w);
		}
		if (max_h >= min_h) {
			h = MIN(h, max_h);
		}
	}
	*width = w;
	*height = h;
}

void
tabs_group_apply_geometry(struct tab_group *group, struct wlr_box geo)
{
	if (!group || group->applying_geometry) {
		return;
	}
	group_adjust_size(group, &geo.width, &geo.height);
	group->geometry = geo;
	group->geometry_valid = true;
	group->applying_geometry = true;

	struct view *member;
	wl_list_for_each(member, &group->members, stellar_tab_link) {
		bool same_position = member->current.x == geo.x
			&& member->current.y == geo.y;
		bool same_size = member->current.width == geo.width
			&& member->current.height == geo.height
			&& member->pending.width == geo.width
			&& member->pending.height == geo.height;
		if (same_position && same_size) {
			continue;
		}
		if (same_size) {
			view_move(member, geo.x, geo.y);
		} else {
			view_move_resize(member, geo);
		}
	}
	group->applying_geometry = false;
	tabs_update_bar(group);
}

void
tabs_group_on_view_moved(struct view *view)
{
	struct tab_group *group = tabs_view_group(view);
	if (!group || group->active != view || group->applying_geometry) {
		return;
	}
	tabs_group_apply_geometry(group, view->current);
}

void
tabs_group_destroy(struct tab_group *group)
{
	if (!group) {
		return;
	}
	wl_list_remove(&group->link);

	struct view *view, *tmp;
	wl_list_for_each_safe(view, tmp, &group->members, stellar_tab_link) {
		wl_list_remove(&view->stellar_tab_link);
		wl_list_init(&view->stellar_tab_link);
		view->stellar_tab_group = NULL;
	}

	struct tab_segment *seg, *seg_tmp;
	wl_list_for_each_safe(seg, seg_tmp, &group->bar_segments, link) {
		wl_list_remove(&seg->link);
		wlr_scene_node_destroy(&seg->rect->node);
		free(seg);
	}
	if (group->bar_tree) {
		wlr_scene_node_destroy(&group->bar_tree->node);
	}
	free(group);
}

void
tabs_destroy_all(void)
{
	struct tab_group *group, *tmp;
	wl_list_for_each_safe(group, tmp, &groups, link) {
		tabs_group_destroy(group);
	}
}

void
tabs_finish(void)
{
	tabs_destroy_all();
}

void
tabs_update_bar(struct tab_group *group)
{
	if (!group || !rc.stellar.tabs_enabled) {
		return;
	}
	struct view *active = group->active;
	if (!active || !active->scene_tree || !active->scene_tree->node.parent) {
		return;
	}

	struct wlr_scene_tree *parent = active->scene_tree->node.parent;
	if (!parent) {
		return;
	}
	if (!group->bar_tree) {
		group->bar_tree = wlr_scene_tree_create(parent);
		if (!group->bar_tree) {
			return;
		}
		group->bar_bg = wlr_scene_rect_create(group->bar_tree, 1,
			TAB_BAR_HEIGHT, bar_bg_color);
	}
	if (group->bar_tree->node.parent != parent) {
		wlr_scene_node_reparent(&group->bar_tree->node, parent);
	}
	wlr_scene_node_raise_to_top(&group->bar_tree->node);

	struct wlr_box geo = group->geometry_valid
		? group->geometry : active->current;
	int width = geo.width > 0 ? geo.width : 1;
	int y = geo.y - TAB_BAR_HEIGHT;
	if (y < 0) {
		y = geo.y;
	}
	wlr_scene_node_set_position(&group->bar_tree->node, geo.x, y);
	if (group->bar_bg) {
		wlr_scene_rect_set_size(group->bar_bg, width, TAB_BAR_HEIGHT);
	}

	size_t count = tabs_group_size(group);
	if (count == 0) {
		return;
	}

	int segment_width = width / (int)count;
	if (segment_width < 1) {
		segment_width = 1;
	}

	size_t index = 0;
	struct tab_segment *seg, *seg_tmp;
	wl_list_for_each_safe(seg, seg_tmp, &group->bar_segments, link) {
		if (index >= count) {
			wl_list_remove(&seg->link);
			wlr_scene_node_destroy(&seg->rect->node);
			free(seg);
			continue;
		}
		struct view *member = nth_member(group, index);
		seg->view = member;
		seg->group = group;
		int x = (int)index * segment_width;
		int w = (index == count - 1)
			? width - x : segment_width;
		if (w < 1) {
			w = 1;
		}
		wlr_scene_node_set_position(&seg->rect->node, x, 0);
		wlr_scene_rect_set_size(seg->rect, w, TAB_BAR_HEIGHT);
		wlr_scene_rect_set_color(seg->rect,
			member == active ? active_color : inactive_color);
		index++;
	}

	while (index < count) {
		seg = calloc(1, sizeof(*seg));
		if (!seg) {
			break;
		}
		seg->rect = wlr_scene_rect_create(group->bar_tree, 1,
			TAB_BAR_HEIGHT, inactive_color);
		if (!seg->rect) {
			free(seg);
			break;
		}
		node_descriptor_create(&seg->rect->node, LAB_NODE_TAB, NULL, seg);
		wl_list_insert(&group->bar_segments, &seg->link);
		struct view *member = nth_member(group, index);
		seg->view = member;
		seg->group = group;
		int x = (int)index * segment_width;
		int w = (index == count - 1) ? width - x : segment_width;
		if (w < 1) {
			w = 1;
		}
		wlr_scene_node_set_position(&seg->rect->node, x, 0);
		wlr_scene_rect_set_size(seg->rect, w, TAB_BAR_HEIGHT);
		wlr_scene_rect_set_color(seg->rect,
			member == active ? active_color : inactive_color);
		index++;
	}
}

void
tabs_set_active_view(struct view *view)
{
	struct tab_group *group = tabs_view_group(view);
	if (!group || group->active == view) {
		return;
	}
	group->active = view;

	struct view *member;
	wl_list_for_each(member, &group->members, stellar_tab_link) {
		if (member->scene_tree) {
			view_update_visibility(member);
		}
	}
	tabs_update_bar(group);
	ipc_notify_tabs("activate", group->id, view->creation_id);
}

void
tabs_on_view_minimize(struct view *view)
{
	struct tab_group *group = tabs_view_group(view);
	if (!group || group->active != view || !view->minimized) {
		return;
	}
	/* Keep the group usable: another visible member becomes the active tab. */
	struct view *next = tabs_group_next_view(group, view);
	for (size_t i = 0; next && next != view && next->minimized
			&& i < tabs_group_size(group); i++) {
		next = tabs_group_next_view(group, next);
	}
	if (next && next != view) {
		tabs_set_active_view(next);
	}
}

bool
tabs_group_views(struct view **views, size_t count)
{
	if (!rc.stellar.tabs_enabled || !views || count < 2) {
		return false;
	}
	struct tab_group *group = NULL;
	for (size_t i = 0; i < count; i++) {
		struct view *view = views[i];
		if (!view || !view->mapped || !view->scene_tree ||
				view_is_modal_dialog(view)) {
			return false;
		}
		if (i > 0 && view->workspace != views[0]->workspace) {
			return false;
		}
		if (view->stellar_tab_group) {
			if (group && group != view->stellar_tab_group) {
				return false;
			}
			group = view->stellar_tab_group;
		}
		for (size_t j = 0; j < i; j++) {
			if (view == views[j]) {
				return false;
			}
		}
	}
	if (!group) {
		group = tabs_group_create();
		if (!group) {
			return false;
		}
	}
	for (size_t i = 0; i < count; i++) {
		/* Extending an existing group is the common incremental path. */
		if (views[i]->stellar_tab_group == group) {
			continue;
		}
		if (!tabs_group_add(group, views[i])) {
			return false;
		}
	}
	if (!group->active || group->active == views[0]) {
		group->active = views[0];
	}

	struct view *member;
	wl_list_for_each(member, &group->members, stellar_tab_link) {
		view_update_visibility(member);
	}
	if (group->geometry_valid) {
		tabs_group_apply_geometry(group, group->geometry);
	} else {
		tabs_group_apply_geometry(group, views[0]->current);
	}
	ipc_notify_tabs("create", group->id, views[0]->creation_id);
	stellar_tiling_reapply();
	return true;
}

bool
tabs_create_group(struct view *a, struct view *b)
{
	struct view *views[2] = { a, b };
	return tabs_group_views(views, 2);
}

void
tabs_switch(struct view *view, bool next)
{
	if (!rc.stellar.tabs_enabled) {
		return;
	}
	struct tab_group *group = tabs_view_group(view);
	if (!group) {
		return;
	}
	struct view *target = next
		? tabs_group_next_view(group, view)
		: tabs_group_prev_view(group, view);
	if (!target || target == view) {
		return;
	}
	tabs_set_active_view(target);
	desktop_focus_view(target, true);
	stellar_tiling_reapply();
}

void
tabs_detach(struct view *view)
{
	if (!rc.stellar.tabs_enabled) {
		return;
	}
	struct tab_group *group = tabs_view_group(view);
	if (!group) {
		return;
	}
	uint64_t group_id = group->id;
	tabs_group_remove(group, view);
	if (tabs_group_size(group) <= 1) {
		struct view *last = tabs_group_active_view(group);
		tabs_group_destroy(group);
		if (last && last->scene_tree) {
			view_update_visibility(last);
		}
	} else {
		struct view *member;
		wl_list_for_each(member, &group->members, stellar_tab_link) {
			if (member->scene_tree) {
				view_update_visibility(member);
			}
		}
		tabs_update_bar(group);
	}
	if (view->scene_tree) {
		view_update_visibility(view);
	}
	ipc_notify_tabs("detach", group_id, view->creation_id);
	stellar_tiling_reapply();
}

void
tabs_on_cursor_release(struct wlr_scene_node *node)
{
	struct tab_segment *segment = node_tab_segment_from_node(node);
	if (!segment || !segment->view || !rc.stellar.tabs_enabled) {
		return;
	}
	tabs_set_active_view(segment->view);
	desktop_focus_view(segment->view, /* raise */ true);
}

void
tabs_on_view_destroy(struct view *view)
{
	if (!view) {
		return;
	}
	struct tab_group *group = tabs_view_group(view);
	if (!group) {
		return;
	}
	tabs_group_remove(group, view);
	if (tabs_group_size(group) <= 1) {
		/* A one-member "group" has no tabs; leave the survivor floating. */
		struct view *last = tabs_group_active_view(group);
		tabs_group_destroy(group);
		if (last && last->scene_tree) {
			view_update_visibility(last);
		}
		return;
	}

	struct view *member;
	wl_list_for_each(member, &group->members, stellar_tab_link) {
		if (member->scene_tree) {
			view_update_visibility(member);
		}
	}
	tabs_update_bar(group);
}
