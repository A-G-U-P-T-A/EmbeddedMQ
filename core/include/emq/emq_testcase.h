#ifndef EMQ_TESTCASE_H
#define EMQ_TESTCASE_H

/*
 * SQLite-style branch anchor for coverage builds.
 *
 * Release / normal debug: no-op.
 * EMQ_COVERAGE_BUILD: evaluates (x) and records a side effect when true so
 * gcov/llvm-cov attributes the branch to this statement site.
 */

#if defined(EMQ_COVERAGE_BUILD)
extern void emq_testcase_branch(int truth, const char *expr, const char *file,
                                int line);
#define EMQ_TESTCASE(x) emq_testcase_branch((int)(!!(x)), #x, __FILE__, __LINE__)
#else
#define EMQ_TESTCASE(x) ((void)0)
#endif

#endif /* EMQ_TESTCASE_H */
