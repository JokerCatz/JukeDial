# SPDX-License-Identifier: LGPL-2.1-or-later
# (prepared in 00_helpers.sh: mk_t10root)
mk_t10root
# games G0: one file per game and the hub in sim/test_games/*.sh, sourced here in name order (they use nowrun, tp, us, stack_is and fail from above)
for gt in sim/test_games/*.sh; do [ -f "$gt" ] && . "$gt"; done

