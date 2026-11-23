/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef STELLAR_EFFECTS_H
#define STELLAR_EFFECTS_H

#include <stdbool.h>
#include <stdint.h>

struct view;

/*
 * Pure animation state. Kept independent from the scene graph so the timing
 * rules can be unit-tested without a compositor.
 */
struct effects_animation {
	uint32_t start_ms;
	uint32_t duration_ms;
	float from;
	float to;
	bool active;
};

/* Pure helpers implemented in effects-state.c */
float effects_clamp_unit(float value);
float effects_ease_out(float t);
bool effects_animation_start(struct effects_animation *anim, uint32_t now_ms,
	float from, float to, uint32_t duration_ms);
/* Returns true while running. Always writes the current value to *value. */
bool effects_animation_tick(struct effects_animation *anim, uint32_t now_ms,
	float *value);
void effects_animation_cancel(struct effects_animation *anim);
bool effects_animation_active(const struct effects_animation *anim);

/* Runtime integration, implemented in effects.c */
void effects_init(void);
void effects_finish(void);
void effects_reconcile(void);
void effects_on_view_map(struct view *view);
/* Additional transitions for minimize/tab visibility changes. */
void effects_on_view_show(struct view *view);
bool effects_on_view_hide(struct view *view);
/*
 * Defer a compositor-requested close until a fade-out finishes. The client
 * surface is still alive during the animation, so no destroyed resource is
 * ever touched. Returns true when the close was deferred.
 */
bool effects_on_view_close(struct view *view);
bool effects_cancel_hide(struct view *view);
void effects_on_view_unmap(struct view *view);
void effects_on_view_destroy(struct view *view);
void effects_on_view_commit(struct view *view);

/* Instrumentation used by EFFECTS STATUS and the idle-cost regression test */
void effects_note_frame_commit(void);
uint64_t effects_frame_commits(void);
uint64_t effects_animations_started(void);
int effects_active_animations(void);
bool effects_timer_armed(void);
float effects_target_opacity(void);

#endif /* STELLAR_EFFECTS_H */
