// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
#define CHECK_EQ_INT(a, b) do { long long _a = (long long)(a), _b = (long long)(b); if (_a != _b) { fprintf(stderr, "%s:%d: %s == %lld, expected %lld\n", __FILE__, __LINE__, #a, _a, _b); exit(1); } } while (0)
#define CHECK_STR(a, b) do { const char *_a = (a), *_b = (b); if (!_a || strcmp(_a, _b)) { fprintf(stderr, "%s:%d: %s == \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, _a ? _a : "(null)", _b); exit(1); } } while (0)
#define CHECK_NEAR(a, b, eps) do { double _a = (a), _b = (b); if (fabs(_a - _b) > (eps)) { fprintf(stderr, "%s:%d: %s == %g, expected %g +- %g\n", __FILE__, __LINE__, #a, _a, _b, (double)(eps)); exit(1); } } while (0)
#define RUN(t) do { t(); printf("ok %s\n", #t); } while (0)
