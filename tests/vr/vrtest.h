#ifndef VRTEST_H
#define VRTEST_H
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include "game/vr_reload_core.h"
extern int vrtest_failures;
extern int vrtest_checks;
#define CHECK(cond) do { vrtest_checks++; if (!(cond)) { vrtest_failures++; \
	printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { long _a = (long)(a), _b = (long)(b); vrtest_checks++; if (_a != _b) { \
	vrtest_failures++; printf("FAIL %s:%d: %s == %ld, expected %ld\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)
#define CHECK_NEAR(a, b, eps) do { double _a = (a), _b = (b); vrtest_checks++; if (fabs(_a - _b) > (eps)) { \
	vrtest_failures++; printf("FAIL %s:%d: %s == %f, expected %f\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)

/* A reload-core gun with the clip out and the chamber as vrrInit(loaded) left it: a restored or
 * settled state only, as every eject empties the chamber since Task F4a */
static inline void vrtest_restored_clip_out(struct vrrgun *g, int32_t loaded)
{
	vrrInit(g, loaded);
	g->magin = 0;
	g->clip = 0;
	vrrSettle(g, 0);
}
#endif
