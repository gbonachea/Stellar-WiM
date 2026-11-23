// SPDX-License-Identifier: GPL-2.0-only
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include "effects.h"

static void
test_clamp_unit(void **state)
{
	(void)state;
	assert_float_equal(effects_clamp_unit(-2.5f), 0.0f, 0.0001f);
	assert_float_equal(effects_clamp_unit(0.25f), 0.25f, 0.0001f);
	assert_float_equal(effects_clamp_unit(4.0f), 1.0f, 0.0001f);
}

static void
test_ease_out(void **state)
{
	(void)state;
	assert_float_equal(effects_ease_out(-1.0f), 0.0f, 0.0001f);
	assert_float_equal(effects_ease_out(0.0f), 0.0f, 0.0001f);
	assert_float_equal(effects_ease_out(1.0f), 1.0f, 0.0001f);
	assert_float_equal(effects_ease_out(2.0f), 1.0f, 0.0001f);
	/* Ease-out is front-loaded: half way through we are already at 87.5%. */
	assert_float_equal(effects_ease_out(0.5f), 0.875f, 0.0001f);
	assert_true(effects_ease_out(0.25f) < effects_ease_out(0.75f));
}

static void
test_animation_lifecycle(void **state)
{
	(void)state;
	struct effects_animation anim = {0};
	float value = -1.0f;

	assert_false(effects_animation_active(&anim));
	assert_false(effects_animation_tick(&anim, 100, &value));
	assert_float_equal(value, -1.0f, 0.0001f);

	assert_true(effects_animation_start(&anim, 100, 0.0f, 1.0f, 200));
	assert_true(effects_animation_active(&anim));

	assert_true(effects_animation_tick(&anim, 100, &value));
	assert_float_equal(value, 0.0f, 0.0001f);

	assert_true(effects_animation_tick(&anim, 150, &value));
	assert_true(value > 0.0f && value < 1.0f);

	/* Completion writes the exact target and deactivates the animation. */
	assert_false(effects_animation_tick(&anim, 300, &value));
	assert_float_equal(value, 1.0f, 0.0001f);
	assert_false(effects_animation_active(&anim));
	assert_false(effects_animation_tick(&anim, 400, &value));
	assert_float_equal(value, 1.0f, 0.0001f);
}

static void
test_animation_clamps_and_cancel(void **state)
{
	(void)state;
	struct effects_animation anim = {0};
	float value = -1.0f;

	assert_true(effects_animation_start(&anim, 0, -5.0f, 9.0f, 100));
	assert_float_equal(anim.from, 0.0f, 0.0001f);
	assert_float_equal(anim.to, 1.0f, 0.0001f);

	effects_animation_cancel(&anim);
	assert_false(effects_animation_active(&anim));
	assert_false(effects_animation_tick(&anim, 10, &value));
	assert_float_equal(value, -1.0f, 0.0001f);
}

static void
test_animation_rejects_zero_duration(void **state)
{
	(void)state;
	struct effects_animation anim = {0};
	assert_false(effects_animation_start(&anim, 0, 0.0f, 1.0f, 0));
	assert_false(effects_animation_active(&anim));
	assert_false(effects_animation_start(NULL, 0, 0.0f, 1.0f, 10));
	effects_animation_cancel(NULL);
	assert_false(effects_animation_active(NULL));
}

int
main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_clamp_unit),
		cmocka_unit_test(test_ease_out),
		cmocka_unit_test(test_animation_lifecycle),
		cmocka_unit_test(test_animation_clamps_and_cancel),
		cmocka_unit_test(test_animation_rejects_zero_duration),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
