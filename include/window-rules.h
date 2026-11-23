/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_WINDOW_RULES_H
#define LABWC_WINDOW_RULES_H

#include <stdbool.h>
#include <wayland-util.h>
#include "config/types.h"
#include "tiling.h"

enum window_rule_event {
	LAB_WINDOW_RULE_EVENT_ON_FIRST_MAP = 0,
};

enum property {
	LAB_PROP_UNSPECIFIED = 0,
	LAB_PROP_UNSET,
	LAB_PROP_FALSE,
	LAB_PROP_TRUE,
};

/*
 * 'identifier' represents:
 *   - 'app_id' for native Wayland windows
 *   - 'WM_CLASS' for XWayland clients
 */
struct window_rule {
	char *identifier;
	char *title;
	enum lab_window_type window_type;
	char *sandbox_engine;
	char *sandbox_app_id;
	bool match_once;

	enum window_rule_event event;
	struct wl_list actions;

	enum property server_decoration;
	enum property skip_taskbar;
	enum property skip_window_switcher;
	enum property ignore_focus_request;
	enum property ignore_configure_request;
	enum property fixed_position;
	enum property icon_prefer_client;
	enum property allow_always_on_top;

	/* Stellar WiM tiling overrides, matched by the inherited criteria. */
	enum stellar_tiling_mode tiling_mode;
	bool has_tiling_mode;
	/* LAB_PROP_FALSE keeps the window floating, TRUE forces it managed. */
	enum property stellar_tiling;

	struct wl_list link; /* struct rcxml.window_rules */
};

struct view;

void window_rules_apply(struct view *view, enum window_rule_event event);
enum property window_rules_get_property(struct view *view, const char *property);
bool window_rules_get_tiling_mode(struct view *view,
	enum stellar_tiling_mode *mode);
/* LAB_PROP_FALSE = floating exception, LAB_PROP_TRUE = force managed. */
enum property window_rules_get_stellar_tiling(struct view *view);

#endif /* LABWC_WINDOW_RULES_H */
