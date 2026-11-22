# SPDX-License-Identifier: LGPL-2.1-or-later
# LDAC_PROF build only (idf.py -DLDAC_PROF=1): stands in for components/libldac under the same name — same sources
# and flags, compiled through prof_ldaclib.c with stage timers. The shared component is not touched.
set(L ${CMAKE_CURRENT_LIST_DIR}/../../../../../components/libldac)
idf_component_register(SRCS "prof_ldaclib.c" "${L}/src/ldacBT.c" INCLUDE_DIRS "${L}/inc" PRIV_INCLUDE_DIRS "${L}/src"
                       REQUIRES esp_hw_support)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-implicit-fallthrough -Wno-type-limits -Wno-error)
target_compile_definitions(${COMPONENT_LIB} PUBLIC _32BIT_FIXED_POINT LDAC_PROF)
