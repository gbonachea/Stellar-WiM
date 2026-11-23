// SPDX-License-Identifier: GPL-2.0-only
#include <stdint.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include "config/rcxml.h"
#include "ipc/server.h"
#include "labwc.h"
#include "output.h"
#include "profile.h"
#include "tabs.h"
#include "tiling.h"
#include "view.h"
#include "window-rules.h"

static bool reapplying;
static uint64_t manage_gen;

bool
stellar_tiling_is_enabled(void)
{
	return rc.stellar.tiling_enabled;
}

bool
stellar_tiling_is_managed(const struct view *view)
{
	return view && rc.stellar.tiling_enabled && view->stellar_tiled;
}

static enum stellar_tiling_mode
layout_mode_for_views(struct view **views, size_t count)
{
	/*
	 * A window rule overrides the global mode for the whole output. The
	 * active view has priority; otherwise the first eligible match in view
	 * order wins, so the result is deterministic.
	 */
	for (int pass = 0; pass < 2; pass++) {
		for (size_t i = 0; i < count; i++) {
			if (pass == 0 && views[i] != server.active_view) {
				continue;
			}
			enum stellar_tiling_mode mode;
			if (window_rules_get_tiling_mode(views[i], &mode)) {
				return mode;
			}
		}
	}
	return rc.stellar.tiling_mode;
}

static void
clamp_box(struct wlr_box *box, const struct wlr_box *area)
{
	int64_t width = box->width;
	int64_t height = box->height;
	int64_t area_width = area->width;
	int64_t area_height = area->height;
	if (area_width < 1 || area_height < 1) {
		return;
	}

	if (rc.stellar.tiling_min_width > 0 &&
			width < rc.stellar.tiling_min_width) {
		width = MIN(rc.stellar.tiling_min_width, area_width);
	}
	if (rc.stellar.tiling_min_height > 0 &&
			height < rc.stellar.tiling_min_height) {
		height = MIN(rc.stellar.tiling_min_height, area_height);
	}
	if (rc.stellar.tiling_max_width > 0 &&
			width > rc.stellar.tiling_max_width) {
		width = MAX(rc.stellar.tiling_max_width, 1);
	}
	if (rc.stellar.tiling_max_height > 0 &&
			height > rc.stellar.tiling_max_height) {
		height = MAX(rc.stellar.tiling_max_height, 1);
	}
	width = MIN(width, area_width);
	height = MIN(height, area_height);

	int64_t x = box->x;
	int64_t y = box->y;
	x = MAX(x, area->x);
	y = MAX(y, area->y);
	x = MIN(x, (int64_t)area->x + area_width - width);
	y = MIN(y, (int64_t)area->y + area_height - height);

	box->x = (int)x;
	box->y = (int)y;
	box->width = (int)width;
	box->height = (int)height;
}

static bool
view_is_eligible(struct view *view)
{
	if (!view || !view->surface || !view->mapped || view->minimized) {
		return false;
	}
	if (view->fullscreen || view->maximized != VIEW_AXIS_NONE ||
			view_is_tiled(view)) {
		return false;
	}
	if (tabs_view_is_hidden(view) || view_is_modal_dialog(view)) {
		return false;
	}
	if (!view->output) {
		return false;
	}
	if (!view->visible_on_all_workspaces &&
			view->workspace != server.workspaces.current) {
		return false;
	}
	return true;
}

static bool
view_in_special_state(struct view *view)
{
	return view->fullscreen || view->maximized != VIEW_AXIS_NONE ||
		view_is_tiled(view);
}

static bool
view_is_candidate(struct view *view)
{
	/* Configurable per-window floating exception. */
	if (window_rules_get_stellar_tiling(view) == LAB_PROP_FALSE) {
		return false;
	}
	return view_is_eligible(view) && view->output &&
		view->output->wlr_output && view->output->wlr_output->enabled;
}

static void
save_floating_geometry(struct view *view)
{
	if (!view || view->stellar_floating_saved || view_in_special_state(view)) {
		return;
	}
	view->stellar_floating_geometry = view->pending;
	if (wlr_box_empty(&view->stellar_floating_geometry)) {
		view->stellar_floating_geometry = view->current;
	}
	view->stellar_floating_saved = true;
}

static struct wlr_box
clamp_floating_geometry(struct view *view)
{
	struct wlr_box geo = view->stellar_floating_geometry;
	if (wlr_box_empty(&geo)) {
		geo = view_get_fallback_natural_geometry(view);
	}
	if (view->output && output_is_usable(view->output)) {
		struct wlr_box area = output_usable_area_in_layout_coords(view->output);
		if (area.width > 0 && area.height > 0) {
			geo.width = MIN(geo.width, area.width);
			geo.height = MIN(geo.height, area.height);
			geo.x = MAX(area.x,
				MIN(geo.x, area.x + area.width - geo.width));
			geo.y = MAX(area.y,
				MIN(geo.y, area.y + area.height - geo.height));
		}
	}
	return geo;
}

static void
restore_floating_geometry(struct view *view)
{
	if (!view || !view->stellar_floating_saved) {
		return;
	}
	struct wlr_box geo = clamp_floating_geometry(view);
	view_move_resize(view, geo);
	view->stellar_floating_saved = false;
}

static size_t
collect_output_views(struct output *output, struct view **views)
{
	size_t count = 0;
	struct view *view;
	wl_list_for_each(view, &server.views, link) {
		if (view_is_candidate(view) && view->output == output) {
			views[count++] = view;
		}
	}
	return count;
}

static void
layout_output_views(struct output *output, struct view **views, size_t count,
		struct wlr_box *boxes)
{
	if (count == 0) {
		return;
	}
	struct wlr_box area = output_usable_area_in_layout_coords(output);
	enum stellar_tiling_mode mode = layout_mode_for_views(views, count);
	float ratio = rc.stellar.tiling_master_ratio > 0.0f
		? rc.stellar.tiling_master_ratio : 0.6f;
	stellar_tiling_layout_boxes_mode(&area, rc.gap, count, mode,
		ratio, rc.stellar.tiling_columns, boxes);
	for (size_t i = 0; i < count; i++) {
		clamp_box(&boxes[i], &area);
	}
}

void
stellar_tiling_reapply(void)
{
	if (reapplying || server.input_mode == LAB_INPUT_STATE_MOVE ||
			server.input_mode == LAB_INPUT_STATE_RESIZE) {
		return;
	}
	reapplying = true;
	manage_gen++;

	if (!rc.stellar.tiling_enabled) {
		struct view *view;
		wl_list_for_each(view, &server.views, link) {
			if (view->stellar_tiled && view->stellar_floating_saved &&
					!view_in_special_state(view)) {
				restore_floating_geometry(view);
			}
			view->stellar_tiled = false;
			view->stellar_manage_gen = 0;
			if (view_in_special_state(view) || !view->stellar_tiled) {
				view->stellar_floating_saved = false;
			}
		}
		reapplying = false;
		return;
	}

	size_t total = 0;
	struct view *view;
	wl_list_for_each(view, &server.views, link) {
		if (view_is_candidate(view)) {
			total++;
		}
	}

	struct view **views = total ? calloc(total, sizeof(*views)) : NULL;
	struct wlr_box *boxes = total ? calloc(total, sizeof(*boxes)) : NULL;
	if (total && (!views || !boxes)) {
		wlr_log(WLR_ERROR, "stellar tiling: out of memory");
		free(views);
		free(boxes);
		reapplying = false;
		return;
	}

	size_t used = 0;
	struct output *output;
	wl_list_for_each(output, &server.outputs, link) {
		size_t count = collect_output_views(output, views + used);
		if (count == 0) {
			continue;
		}
		/*
		 * Hybrid is the true mixed mode: one managed master and the
		 * remaining windows floating unless a rule explicitly forces
		 * them managed. Other modes manage every eligible view.
		 */
		if (count > 1 && layout_mode_for_views(views + used, count)
				== STELLAR_TILING_HYBRID) {
			size_t keep = 0;
			for (size_t i = 0; i < count; i++) {
				if (i == 0 || window_rules_get_stellar_tiling(
						views[used + i]) == LAB_PROP_TRUE) {
					views[used + keep++] = views[used + i];
				}
			}
			count = keep;
		}
		layout_output_views(output, views + used, count, boxes + used);
		for (size_t i = 0; i < count; i++) {
			views[used + i]->stellar_manage_gen = manage_gen;
		}
		used += count;
	}

	/* Views that are no longer managed get their pre-tiling geometry back. */
	wl_list_for_each(view, &server.views, link) {
		if (view->stellar_manage_gen == manage_gen) {
			continue;
		}
		if (view->stellar_tiled && view->stellar_floating_saved &&
				!view_in_special_state(view)) {
			restore_floating_geometry(view);
		}
		view->stellar_tiled = false;
		view->stellar_manage_gen = 0;
		/*
		 * Maximized/fullscreen state owns the geometry for now; any
		 * other stale snapshot has either been restored above or is no
		 * longer relevant.
		 */
		view->stellar_floating_saved = false;
	}

	for (size_t i = 0; i < used; i++) {
		view = views[i];
		struct wlr_box *box = &boxes[i];
		if (!view->stellar_tiled) {
			save_floating_geometry(view);
			view->stellar_tiled = true;
		}

		struct tab_group *group = view->stellar_tab_group;
		if (group) {
			struct view *member;
			wl_list_for_each(member, &group->members, stellar_tab_link) {
				if (!member->stellar_tiled) {
					save_floating_geometry(member);
				}
				member->stellar_tiled = true;
				member->stellar_manage_gen = manage_gen;
			}
			tabs_group_apply_geometry(group, *box);
			continue;
		}

		struct wlr_box current = view->pending;
		if (current.x != box->x || current.y != box->y ||
				current.width != box->width ||
				current.height != box->height) {
			view_move_resize(view, *box);
		}
	}

	free(views);
	free(boxes);

	/* Keep optional tab bars attached to their new active-window geometry. */
	if (rc.stellar.tabs_enabled) {
		wl_list_for_each(view, &server.views, link) {
			if (view->stellar_tab_group) {
				tabs_update_bar(view->stellar_tab_group);
			}
		}
	}
	reapplying = false;
}

void
stellar_tiling_toggle(void)
{
	int managed = 0;
	struct view *view;
	wl_list_for_each(view, &server.views, link) {
		if (stellar_tiling_is_managed(view)) {
			managed++;
		}
	}
	rc.stellar.tiling_enabled = !rc.stellar.tiling_enabled;
	/* An individual override means the session no longer matches a preset. */
	rc.stellar.profile = STELLAR_PROFILE_CUSTOM;
	wlr_log(WLR_INFO, "Stellar tiling %s",
		rc.stellar.tiling_enabled ? "enabled" : "disabled");
	stellar_tiling_reapply();
	ipc_notify_tiling("toggle", managed);
}

enum stellar_tiling_mode
stellar_tiling_output_mode(struct output *output)
{
	struct view *views[256] = {0};
	size_t count = 0;
	if (output) {
		struct view *view;
		wl_list_for_each(view, &server.views, link) {
			if (count == sizeof(views) / sizeof(views[0])) {
				break;
			}
			if (view_is_candidate(view) && view->output == output) {
				views[count++] = view;
			}
		}
	}
	if (!count) {
		return rc.stellar.tiling_mode;
	}
	return layout_mode_for_views(views, count);
}

void
stellar_tiling_on_map(struct view *view)
{
	if (rc.stellar.tiling_enabled && view_is_eligible(view)) {
		stellar_tiling_reapply();
	}
}

void
stellar_tiling_on_minimize(struct view *view)
{
	(void)view;
	if (rc.stellar.tiling_enabled) {
		stellar_tiling_reapply();
	}
}

void
stellar_tiling_on_unmap(struct view *view)
{
	if (rc.stellar.tiling_enabled && view->stellar_tiled) {
		stellar_tiling_reapply();
	}
}

void
stellar_tiling_on_destroy(struct view *view)
{
	if (!view) {
		return;
	}
	view->stellar_tiled = false;
	if (rc.stellar.tiling_enabled) {
		stellar_tiling_reapply();
	}
}
