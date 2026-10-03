/* grid_test — beat↔sample mapping, phase-continuous tempo change, nudge. */
#include "check.h"
#include "grid.h"

int main(void) {
	fg_grid g;
	fg_grid_init(&g, 120.0, 48000.0);
	fg_grid_restart(&g, 1000.0);
	CHECK(g.running);
	/* 120 bpm: one beat = 0.5 s = 24000 samples */
	CHECK_NEAR(fg_grid_sample_at_beat(&g, 0.0), 1000.0, 1e-9);
	CHECK_NEAR(fg_grid_sample_at_beat(&g, 4.0), 1000.0 + 4 * 24000.0, 1e-9);
	CHECK_NEAR(fg_grid_beat_at_sample(&g, 1000.0 + 24000.0), 1.0, 1e-12);

	/* tempo change at beat 2 keeps beat 2 where it was and stretches after it */
	const double atB2 = fg_grid_sample_at_beat(&g, 2.0);
	fg_grid_set_tempo(&g, 60.0, atB2);
	CHECK_NEAR(fg_grid_beat_at_sample(&g, atB2), 2.0, 1e-9);
	CHECK_NEAR(fg_grid_sample_at_beat(&g, 3.0), atB2 + 48000.0, 1e-9);
	/* an invalid tempo is ignored */
	fg_grid_set_tempo(&g, -5.0, atB2);
	CHECK_NEAR(g.bpm, 60.0, 0.0);

	/* nudge slides the origin only */
	const double before = fg_grid_sample_at_beat(&g, 5.0);
	fg_grid_nudge(&g, -100.0);
	CHECK_NEAR(fg_grid_sample_at_beat(&g, 5.0), before - 100.0, 1e-9);
	CHECK_NEAR(g.bpm, 60.0, 0.0);

	/* sync_phase: beat 0 at a given sample, new tempo */
	fg_grid_sync_phase(&g, 0.0, 5000.0, 100.0);
	CHECK_NEAR(fg_grid_sample_at_beat(&g, 1.0), 5000.0 + 48000.0 * 0.6, 1e-9);
	fg_grid_stop(&g);
	CHECK(!g.running);

	return check_report("grid_test");
}
