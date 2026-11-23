/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef STELLAR_PROFILE_H
#define STELLAR_PROFILE_H

#include <stdbool.h>

/*
 * Profiles are presets over features that can be toggled safely at runtime.
 * CUSTOM means "no preset": the individual <stellar-wim> flags decide. It is
 * the default so that configurations written for 0.3.x keep their behaviour.
 */
enum stellar_profile {
	STELLAR_PROFILE_CUSTOM = 0,
	STELLAR_PROFILE_MINIMAL,
	STELLAR_PROFILE_ENHANCED,
};

const char *stellar_profile_name(enum stellar_profile profile);
bool stellar_profile_parse(const char *name, enum stellar_profile *out);
enum stellar_profile stellar_profile_current(void);

/* Apply the configured preset and reconcile safe runtime state. */
void stellar_profile_reconcile(void);

/* Runtime switch. CUSTOM cannot be selected here; returns false for it. */
bool stellar_profile_set(enum stellar_profile profile);

#endif /* STELLAR_PROFILE_H */
