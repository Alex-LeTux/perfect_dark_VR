#include "vrtest.h"
int vrtest_failures = 0;
int vrtest_checks = 0;
void run_reload_core_tests(void);
void run_holster_core_tests(void);
void run_holster_layout_tests(void);
void run_reload_loadsoninsert_tests(void);
void run_reload_rackslap_tests(void);
void run_reload_shotgun_tests(void);
void run_reload_crossbow_tests(void);
int main(void)
{
	run_reload_core_tests();
	run_holster_core_tests();
	run_holster_layout_tests();
	run_reload_loadsoninsert_tests();
	run_reload_rackslap_tests();
	run_reload_shotgun_tests();
	run_reload_crossbow_tests();
	printf("%d checks, %d failures\n", vrtest_checks, vrtest_failures);
	return vrtest_failures ? 1 : 0;
}
