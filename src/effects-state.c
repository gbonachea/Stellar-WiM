// SPDX-License-Identifier: GPL-2.0-only
#include "effects.h"

float
effects_clamp_unit(float value)
{
	if (value < 0.0f) {
		return 0.0f;
	}
	if (value > 1.0f) {
		return 1.0f;
	}
	return value;
}

/* Cubic ease-out, polynomial only so no libm dependency is introduced. */
float
effects_ease_out(float t)
{
	t = effects_clamp_unit(t);
	float remaining = 1.0f - t;
	return 1.0f - remaining * remaining * remaining;
}

bool
effects_animation_start(struct effects_animation *anim, uint32_t now_ms,
		float from, float to, uint32_t duration_ms)
{
	if (!anim || duration_ms == 0) {
		return false;
	}
	anim->start_ms = now_ms;
	anim->duration_ms = duration_ms;
	anim->from = effects_clamp_unit(from);
	anim->to = effects_clamp_unit(to);
	anim->active = true;
	return true;
}

bool
effects_animation_tick(struct effects_animation *anim, uint32_t now_ms,
		float *value)
{
	if (!anim || !anim->active) {
		return false;
	}

	uint32_t elapsed = now_ms >= anim->start_ms
		? now_ms - anim->start_ms : 0;
	if (elapsed >= anim->duration_ms) {
		anim->active = false;
		if (value) {
			*value = anim->to;
		}
		return false;
	}

	if (value) {
		float t = (float)elapsed / (float)anim->duration_ms;
		*value = anim->from
			+ (anim->to - anim->from) * effects_ease_out(t);
	}
	return true;
}

void
effects_animation_cancel(struct effects_animation *anim)
{
	if (anim) {
		anim->active = false;
	}
}

bool
effects_animation_active(const struct effects_animation *anim)
{
	return anim && anim->active;
}
