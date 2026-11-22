// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* test_rsq_order only (forced in with -include, pc_rsq.c and the test): every hand-off load and store of pc_rsq.c's swap state goes through
 * rsq_hook first, with the field's source text, its address, the value stored and the memory order, so the test sees their order and kind. */
void rsq_hook(const char *field, const volatile void *addr, unsigned long v, int mo, int is_store);
#define PC_RSQ_LOAD(x, mo) (rsq_hook(#x, &(x), 0, (mo), 0), __atomic_load_n(&(x), (mo)))
#define PC_RSQ_STORE(x, v, mo) do { rsq_hook(#x, &(x), (unsigned long)(v), (mo), 1); __atomic_store_n(&(x), (v), (mo)); } while (0)
