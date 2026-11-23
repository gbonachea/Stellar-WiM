// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include "common/mem.h"
#include "config/rcxml.h"
#include "effects.h"
#include "labwc.h"
#include "tabs.h"
#include "view.h"

/*
 * Stellar visual effects are strictly opt-in. When both flags are false this
 * module allocates nothing, arms no timer and installs no extra render loop.
 * There is exactly one transient timer, and only while a fade is running.
 */
static const uint32_t FADE_DURATION_MS = 180;
static const int FADE_TICK_MS = 16;

static int handle_fade_tick(void *data);

struct effects_fade {
	struct wl_list link;
	struct view *view;
	struct effects_animation anim;
	float current;
	bool fading_out;
	bool closing; /* deferred protocol close, sent when the fade ends */
};

static struct wl_list fades = { &fades, &fades };
static struct wl_event_source *fade_timer;
static uint64_t frame_commits;
static uint64_t animations_started;

static uint32_t
now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

float
effects_target_opacity(void)
{
	return rc.stellar.opacity_enabled
		? effects_clamp_unit(rc.stellar.opacity_level) : 1.0f;
}

static void
set_buffer_opacity(struct wlr_scene_buffer *buffer, int sx, int sy, void *data)
{
	float *opacity = data;
	wlr_scene_buffer_set_opacity(buffer, effects_clamp_unit(*opacity));
}

/*
 * wlroots resets a scene buffer's opacity to 1.0 on every surface commit, so
 * the value has to be re-applied from our commit handlers. Rects (including
 * decorations and the tab bar) are intentionally left untouched.
 */
static void
apply_opacity(struct view *view, float opacity)
{
	if (!view || !view->scene_tree) {
		return;
	}
	wlr_scene_node_for_each_buffer(&view->scene_tree->node,
		set_buffer_opacity, &opacity);
}

static struct effects_fade *
find_fade(struct view *view)
{
	struct effects_fade *fade;
	wl_list_for_each(fade, &fades, link) {
		if (fade->view == view) {
			return fade;
		}
	}
	return NULL;
}

static void
cancel_view_fade(struct view *view)
{
	struct effects_fade *fade = find_fade(view);
	if (!fade) {
		return;
	}
	wl_list_remove(&fade->link);
	free(fade);
	if (wl_list_empty(&fades) && fade_timer) {
		wl_event_source_remove(fade_timer);
		fade_timer = NULL;
	}
}

/* All fade entry points share this timer/accounting path. */
static bool
start_fade(struct view *view, float from, float to, bool fading_out,
	bool closing)
{
	if (!view || !rc.stellar.animations_enabled || find_fade(view)) {
		return false;
	}
	struct effects_fade *fade = znew(struct effects_fade);
	fade->view = view;
	fade->current = from;
	fade->fading_out = fading_out;
	fade->closing = closing;
	if (!effects_animation_start(&fade->anim, now_ms(), from, to,
			FADE_DURATION_MS)) {
		free(fade);
		return false;
	}
	wl_list_insert(&fades, &fade->link);
	animations_started++;
	apply_opacity(view, from);

	if (!fade_timer) {
		fade_timer = wl_event_loop_add_timer(server.wl_event_loop,
			handle_fade_tick, NULL);
	}
	if (!fade_timer) {
		wlr_log(WLR_ERROR, "Unable to arm Stellar fade timer");
		cancel_view_fade(view);
		/* Never leave a view transparent when no fade can run. */
		apply_opacity(view, effects_target_opacity());
		return false;
	}
	wl_event_source_timer_update(fade_timer, FADE_TICK_MS);
	return true;
}

static int
handle_fade_tick(void *data)
{
	(void)data;
	uint32_t time = now_ms();
	struct effects_fade *fade, *tmp;
	bool running_any = false;

	wl_list_for_each_safe(fade, tmp, &fades, link) {
		float value = fade->current;
		bool running = effects_animation_tick(&fade->anim, time, &value);
		fade->current = value;
		apply_opacity(fade->view, value);
		if (running) {
			running_any = true;
		} else {
			struct view *view = fade->view;
			bool fading_out = fade->fading_out;
			bool closing = fade->closing;
			wl_list_remove(&fade->link);
			free(fade);
			if (closing && view && view->mapped) {
				/*
				 * The client surface is still mapped here: only now do
				 * we send the protocol close. No destroyed resource is
				 * referenced at any point of the animation.
				 */
				view_close_now(view);
				/*
				 * If the client ignores the close request, fade it
				 * back in instead of leaving an invisible window.
				 * A client that honours the request unmaps first and
				 * cancels this fade.
				 */
				if (view->mapped) {
					effects_on_view_show(view);
				}
			} else if (fading_out && view) {
				/* Hide only now that the content is invisible. */
				view_update_visibility_now(view);
				apply_opacity(view, effects_target_opacity());
			}
		}
	}

	if (!running_any && fade_timer) {
		wl_event_source_remove(fade_timer);
		fade_timer = NULL;
	} else if (running_any && fade_timer) {
		/* wl_event_source_timer_update() is one-shot: re-arm each tick. */
		wl_event_source_timer_update(fade_timer, FADE_TICK_MS);
	}
	return 0;
}

static void
cancel_all_fades(void)
{
	struct effects_fade *fade, *tmp;
	wl_list_for_each_safe(fade, tmp, &fades, link) {
		wl_list_remove(&fade->link);
		free(fade);
	}
	if (fade_timer) {
		wl_event_source_remove(fade_timer);
		fade_timer = NULL;
	}
}

void
effects_init(void)
{
	frame_commits = 0;
	animations_started = 0;
}

void
effects_finish(void)
{
	cancel_all_fades();
}

/*
 * Called after rc.xml is (re)read. Reapplying 1.0 is a no-op in wlroots when
 * the buffer is already opaque, so turning effects off leaves no residue.
 */
void
effects_reconcile(void)
{
	if (!rc.stellar.animations_enabled) {
		/*
		 * A fade-out owns the delayed visibility change. Finish it
		 * immediately before dropping the fade, otherwise disabling
		 * effects mid-transition would leave a transparent view enabled.
		 */
		struct effects_fade *fade, *tmp;
		wl_list_for_each_safe(fade, tmp, &fades, link) {
			struct view *view = fade->view;
			bool closing = fade->closing;
			bool fading_out = fade->fading_out;
			wl_list_remove(&fade->link);
			free(fade);
			if (!view) {
				continue;
			}
			if (closing && view->mapped) {
				/* Do not lose a close request when effects are turned off. */
				view_close_now(view);
			} else if (fading_out) {
				view_update_visibility_now(view);
			}
		}
		cancel_all_fades();
	}

	struct view *view;
	wl_list_for_each(view, &server.views, link) {
		struct effects_fade *fade = find_fade(view);
		if (fade && !fade->fading_out) {
			fade->anim.to = effects_target_opacity();
		}
		apply_opacity(view, fade ? fade->current
			: effects_target_opacity());
	}
}

void
effects_on_view_map(struct view *view)
{
	if (!view || !view->mapped || view->minimized ||
			tabs_view_is_hidden(view)) {
		return;
	}
	struct effects_fade *fade = find_fade(view);
	if (fade && !fade->fading_out) {
		/* The visibility wrapper already started this fade-in. */
		return;
	}
	if (fade) {
		cancel_view_fade(view);
	}
	start_fade(view, 0.0f, effects_target_opacity(), false, false);
}

void
effects_on_view_show(struct view *view)
{
	if (!view || find_fade(view)) {
		return;
	}
	start_fade(view, 0.0f, effects_target_opacity(), false, false);
}

bool
effects_on_view_hide(struct view *view)
{
	if (!view || !rc.stellar.animations_enabled) {
		return false;
	}
	struct effects_fade *fade = find_fade(view);
	float from = effects_target_opacity();
	if (fade) {
		if (fade->fading_out) {
			return true;
		}
		/* Continue from the current opacity if map-in was interrupted. */
		from = fade->current;
		cancel_view_fade(view);
	}
	return start_fade(view, from, 0.0f, true, false);
}

bool
effects_on_view_close(struct view *view)
{
	if (!view || !rc.stellar.animations_enabled || !view->mapped
			|| view->minimized || tabs_view_is_hidden(view)) {
		return false;
	}
	struct effects_fade *fade = find_fade(view);
	if (fade && fade->closing) {
		/* A close is already pending; never schedule a second one. */
		return true;
	}
	float from = fade ? fade->current : effects_target_opacity();
	if (fade) {
		cancel_view_fade(view);
	}
	return start_fade(view, from, 0.0f, true, true);
}

bool
effects_cancel_hide(struct view *view)
{
	struct effects_fade *fade = find_fade(view);
	if (!fade || !fade->fading_out || fade->closing) {
		return false;
	}
	float from = fade->current;
	cancel_view_fade(view);
	if (!start_fade(view, from, effects_target_opacity(), false, false)) {
		apply_opacity(view, effects_target_opacity());
	}
	return true;
}

void
effects_on_view_unmap(struct view *view)
{
	cancel_view_fade(view);
}

void
effects_on_view_destroy(struct view *view)
{
	/* Never keep a pointer to a view whose scene tree is about to die. */
	cancel_view_fade(view);
}

void
effects_on_view_commit(struct view *view)
{
	struct effects_fade *fade = find_fade(view);
	if (!rc.stellar.opacity_enabled && !fade) {
		return;
	}
	apply_opacity(view, fade ? fade->current : effects_target_opacity());
}

void
effects_note_frame_commit(void)
{
	frame_commits++;
}

uint64_t
effects_frame_commits(void)
{
	return frame_commits;
}

uint64_t
effects_animations_started(void)
{
	return animations_started;
}

int
effects_active_animations(void)
{
	return wl_list_length(&fades);
}

bool
effects_timer_armed(void)
{
	return fade_timer != NULL;
}
