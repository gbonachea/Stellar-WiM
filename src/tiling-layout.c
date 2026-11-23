// SPDX-License-Identifier: GPL-2.0-only
/*
 * Pure integer layouts used by Stellar WiM tiling. Kept separate from the
 * compositor integration so they can be unit-tested without a scene graph.
 */
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wlr/util/box.h>
#include "tiling.h"

bool
stellar_tiling_parse_mode(const char *name, enum stellar_tiling_mode *out)
{
	if (!name || !out) {
		return false;
	}
	while (isspace((unsigned char)*name)) {
		name++;
	}
	size_t len = strlen(name);
	while (len > 0 && isspace((unsigned char)name[len - 1])) {
		len--;
	}
	if (len == 0) {
		return false;
	}

	if (!strncasecmp(name, "master-stack", len) && len == strlen("master-stack")) {
		*out = STELLAR_TILING_MASTER_STACK;
	} else if (!strncasecmp(name, "master", len) && len == strlen("master")) {
		*out = STELLAR_TILING_MASTER_STACK;
	} else if (!strncasecmp(name, "horizontal", len) && len == strlen("horizontal")) {
		*out = STELLAR_TILING_HORIZONTAL;
	} else if (!strncasecmp(name, "vertical", len) && len == strlen("vertical")) {
		*out = STELLAR_TILING_VERTICAL;
	} else if (!strncasecmp(name, "grid", len) && len == strlen("grid")) {
		*out = STELLAR_TILING_GRID;
	} else if (!strncasecmp(name, "hybrid", len) && len == strlen("hybrid")) {
		*out = STELLAR_TILING_HYBRID;
	} else {
		return false;
	}
	return true;
}

const char *
stellar_tiling_mode_name(enum stellar_tiling_mode mode)
{
	switch (mode) {
	case STELLAR_TILING_HORIZONTAL:
		return "horizontal";
	case STELLAR_TILING_VERTICAL:
		return "vertical";
	case STELLAR_TILING_GRID:
		return "grid";
	case STELLAR_TILING_HYBRID:
		return "hybrid";
	case STELLAR_TILING_MASTER_STACK:
	default:
		return "master-stack";
	}
}

static int
effective_gap(const struct wlr_box *area, int gap, size_t spokes)
{
	int width = area->width > 0 ? area->width : 0;
	int height = area->height > 0 ? area->height : 0;
	int g = gap > 0 ? gap : 0;
	if (width <= 2 || height <= 2) {
		return 0;
	}
	int limit = (width - 2) / (int)(spokes > 1 ? spokes : 2);
	if (height - 2 < width - 2) {
		limit = (height - 2) / (int)(spokes > 1 ? spokes : 2);
	}
	if (limit < 0) {
		limit = 0;
	}
	return g > limit ? limit : g;
}

static void
split_row(const struct wlr_box *area, int gap, int y, int height,
	int x, int width, size_t count, struct wlr_box *out)
{
	if (count == 0) {
		return;
	}
	int available = width - (int)(count - 1) * gap;
	if (available < (int)count) {
		available = width;
		gap = 0;
	}
	int column = available / (int)count;
	int cursor = x;
	for (size_t i = 0; i < count; i++) {
		int w = column;
		if (i == count - 1) {
			w = x + width - cursor;
		}
		if (w < 1) {
			w = 1;
		}
		out[i] = (struct wlr_box){
			.x = cursor, .y = y, .width = w, .height = height,
		};
		cursor += w + gap;
	}
}

static void
split_column(const struct wlr_box *area, int gap, int x, int width,
	int y, int height, size_t count, struct wlr_box *out)
{
	if (count == 0) {
		return;
	}
	int available = height - (int)(count - 1) * gap;
	if (available < (int)count) {
		available = height;
		gap = 0;
	}
	int row = available / (int)count;
	int cursor = y;
	for (size_t i = 0; i < count; i++) {
		int h = row;
		if (i == count - 1) {
			h = y + height - cursor;
		}
		if (h < 1) {
			h = 1;
		}
		out[i] = (struct wlr_box){
			.x = x, .y = cursor, .width = width, .height = h,
		};
		cursor += h + gap;
	}
}

static void
master_stack(const struct wlr_box *area, int gap, size_t n, float ratio,
	struct wlr_box *out)
{
	if (ratio < 0.2f) {
		ratio = 0.2f;
	} else if (ratio > 0.8f) {
		ratio = 0.8f;
	}
	int master_width = (int)((area->width - gap) * ratio + 0.5f);
	if (master_width < 1) {
		master_width = 1;
	}
	int stack_width = area->width - gap - master_width;
	if (stack_width < 1) {
		stack_width = 1;
		master_width = area->width - gap - 1;
		if (master_width < 1) {
			master_width = 1;
		}
	}
	out[0] = (struct wlr_box){
		.x = area->x, .y = area->y,
		.width = master_width, .height = area->height,
	};
	split_column(area, gap, area->x + master_width + gap, stack_width,
		area->y, area->height, n - 1, &out[1]);
}

void
stellar_tiling_layout_boxes_mode(const struct wlr_box *area, int gap,
		size_t n, enum stellar_tiling_mode mode, float master_ratio,
		int columns, struct wlr_box *out)
{
	if (!area || !out || n == 0) {
		return;
	}
	if (n == 1 || area->width <= 0 || area->height <= 0) {
		out[0] = (struct wlr_box){
			.x = area->x, .y = area->y,
			.width = area->width > 0 ? area->width : 0,
			.height = area->height > 0 ? area->height : 0,
		};
		return;
	}

	switch (mode) {
	case STELLAR_TILING_HORIZONTAL:
		gap = effective_gap(area, gap, n);
		split_row(area, gap, area->y, area->height,
			area->x, area->width, n, out);
		break;
	case STELLAR_TILING_VERTICAL:
		gap = effective_gap(area, gap, n);
		split_column(area, gap, area->x, area->width,
			area->y, area->height, n, out);
		break;
	case STELLAR_TILING_GRID:
	case STELLAR_TILING_HYBRID: {
		size_t remaining = n;
		int x = area->x;
		int y = area->y;
		int width = area->width;
		int height = area->height;
		if (mode == STELLAR_TILING_HYBRID) {
			gap = effective_gap(area, gap, 2);
			master_stack(area, gap, n, master_ratio, out);
			remaining = n - 1;
			x = out[0].x + out[0].width + gap;
			width = area->x + area->width - x;
			if (width < 1) {
				width = 1;
			}
			out++;
			if (remaining == 0) {
				return;
			}
		} else {
			gap = effective_gap(area, gap, 2);
		}
		if (columns < 1) {
			columns = 1;
		}
		if ((size_t)columns > remaining) {
			columns = (int)remaining;
		}
		int rows = (int)((remaining + (size_t)columns - 1) /
			(size_t)columns);
		int row_gap = effective_gap(&(struct wlr_box){
			.x = x, .y = y, .width = width, .height = height,
		}, gap, rows);
		int available = height - (rows - 1) * row_gap;
		if (available < rows) {
			available = height;
			row_gap = 0;
		}
		int row_height = available / rows;
		int cursor_y = y;
		size_t index = 0;
		for (int row = 0; row < rows; row++) {
			size_t in_row = remaining - index;
			if (in_row > (size_t)columns) {
				in_row = (size_t)columns;
			}
			int h = row_height;
			if (row == rows - 1) {
				h = y + height - cursor_y;
			}
			if (h < 1) {
				h = 1;
			}
			split_row(&(struct wlr_box){
				.x = x, .y = cursor_y, .width = width, .height = h,
			}, gap, cursor_y, h, x, width, in_row, &out[index]);
			index += in_row;
			cursor_y += h + row_gap;
		}
		break;
	}
	case STELLAR_TILING_MASTER_STACK:
	default:
		gap = effective_gap(area, gap, 2);
		master_stack(area, gap, n, master_ratio, out);
		break;
	}
}

void
stellar_tiling_layout_boxes(const struct wlr_box *area, int gap,
		size_t n, struct wlr_box *out)
{
	stellar_tiling_layout_boxes_mode(area, gap, n,
		STELLAR_TILING_MASTER_STACK, 0.6f, 1, out);
}
