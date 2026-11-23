/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef STELLAR_TILING_H
#define STELLAR_TILING_H

#include <stdbool.h>
#include <stddef.h>

struct output;
struct view;
struct wlr_box;

enum stellar_tiling_mode {
	STELLAR_TILING_MASTER_STACK = 0,
	STELLAR_TILING_HORIZONTAL,
	STELLAR_TILING_VERTICAL,
	STELLAR_TILING_GRID,
	STELLAR_TILING_HYBRID,
};

/* Pure layout helpers; neither touches compositor state. */
bool stellar_tiling_parse_mode(const char *name, enum stellar_tiling_mode *out);
const char *stellar_tiling_mode_name(enum stellar_tiling_mode mode);
void stellar_tiling_layout_boxes(const struct wlr_box *area, int gap,
	size_t n, struct wlr_box *out);
void stellar_tiling_layout_boxes_mode(const struct wlr_box *area, int gap,
	size_t n, enum stellar_tiling_mode mode, float master_ratio,
	int columns, struct wlr_box *out);

/* Integration; all of these are no-ops while tiling is disabled. */
bool stellar_tiling_is_enabled(void);
bool stellar_tiling_is_managed(const struct view *view);
void stellar_tiling_reapply(void);
void stellar_tiling_toggle(void);
/* Effective mode for one output, including active-view window-rule overrides. */
enum stellar_tiling_mode stellar_tiling_output_mode(struct output *output);
void stellar_tiling_on_map(struct view *view);
void stellar_tiling_on_minimize(struct view *view);
void stellar_tiling_on_unmap(struct view *view);
void stellar_tiling_on_destroy(struct view *view);

#endif /* STELLAR_TILING_H */
