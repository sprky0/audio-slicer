/* envelope_test — fade shapes, scaling of overlong fades, the declick
 * clearance, and the gain curve a voice sees. */
#include "check.h"
#include "envelope.h"
#include "frog_types.h"

int main(void) {
	/* shapes */
	CHECK_NEAR(fg_curve_shape(FG_CURVE_LINEAR, 0.25), 0.25, 1e-12);
	CHECK_NEAR(fg_curve_shape(FG_CURVE_EXP, 0.5), 0.25, 1e-12);
	CHECK_NEAR(fg_curve_shape(FG_CURVE_LOG, 0.25), 0.5, 1e-12);
	CHECK_NEAR(fg_curve_shape(FG_CURVE_S, 0.5), 0.5, 1e-12);
	CHECK_NEAR(fg_curve_shape(FG_CURVE_S, 0.25), 0.1464466, 1e-6);
	CHECK_NEAR(fg_curve_shape(FG_CURVE_EXP, -1.0), 0.0, 0.0);
	CHECK_NEAR(fg_curve_shape(FG_CURVE_EXP, 2.0), 1.0, 0.0);

	fg_env_state st;
	/* no env: unity */
	CHECK(!fg_env_prepare(&st, NULL, 1.0));
	CHECK_NEAR(fg_env_gain(&st, 0.5), 1.0, 0.0);

	/* plain fades, linear */
	fg_env e = {0.1, 0.2, FG_CURVE_LINEAR, FG_CURVE_LINEAR, 1.0};
	CHECK(fg_env_prepare(&st, &e, 1.0));
	CHECK_NEAR(fg_env_gain(&st, 0.0), 0.0, 0.0);
	CHECK_NEAR(fg_env_gain(&st, 0.05), 0.5, 1e-12);
	CHECK_NEAR(fg_env_gain(&st, 0.5), 1.0, 0.0);
	CHECK_NEAR(fg_env_gain(&st, 0.9), 0.5, 1e-12);
	CHECK_NEAR(fg_env_gain(&st, 1.0), 0.0, 1e-12);

	/* gain ceiling + curves: in = exp (slow start), out = s */
	e = (fg_env){0.4, 0.4, FG_CURVE_EXP, FG_CURVE_S, 1.5};
	fg_env_prepare(&st, &e, 1.0);
	CHECK_NEAR(fg_env_gain(&st, 0.2), 1.5 * 0.25, 1e-12);
	CHECK_NEAR(fg_env_gain(&st, 0.5), 1.5, 0.0);
	CHECK_NEAR(fg_env_gain(&st, 0.8), 1.5 * 0.5, 1e-12);

	/* overlong fades scale proportionally to meet at the crossover */
	e = (fg_env){2.0, 2.0, FG_CURVE_LINEAR, FG_CURVE_LINEAR, 1.0};
	fg_env_prepare(&st, &e, 1.0);
	CHECK_NEAR(st.fadeIn, 0.5, 1e-12);
	CHECK_NEAR(st.fadeOut, 0.5, 1e-12);
	CHECK_NEAR(fg_env_gain(&st, 0.5), 1.0, 1e-12);
	CHECK_NEAR(fg_env_gain(&st, 0.25), 0.5, 1e-12);

	/* no fade-out: the fade-in leaves 5 ms clear for the declick ramp */
	e = (fg_env){1.0, 0.0, FG_CURVE_LINEAR, FG_CURVE_LINEAR, 1.0};
	CHECK(!fg_env_prepare(&st, &e, 1.0));
	CHECK_NEAR(st.fadeIn, 0.995, 1e-12);
	CHECK(!st.fadesOut);

	/* level only, no fades: not "fading out", gain flat */
	e = (fg_env){0.0, 0.0, FG_CURVE_LINEAR, FG_CURVE_LINEAR, 0.5};
	CHECK(!fg_env_prepare(&st, &e, 1.0));
	CHECK_NEAR(fg_env_gain(&st, 0.3), 0.5, 0.0);
	/* a negative gain is treated as unity */
	e.gain = -1.0;
	fg_env_prepare(&st, &e, 1.0);
	CHECK_NEAR(st.level, 1.0, 0.0);

	return check_report("envelope_test");
}
