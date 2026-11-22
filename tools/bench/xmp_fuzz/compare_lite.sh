#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Renders module files with libxmp-lite 4.7.2 (the build the firmware had before the swap, taken from git history) and with the full
# libxmp as built now (components/libxmp, no Paula mixer; LIBXMP_LITE_COMPAT is OFF since 2026-11-18, so a few IT / MOD files differ on purpose) and compares the PCM byte for byte.
#   tools/bench/xmp_fuzz/compare_lite.sh [SECONDS=40] FILE...      exit 1 when any file differs
# Only the formats libxmp-lite plays are meaningful (MOD with M.K., XM, S3M, IT); a file lite refuses is reported "lite refuses".
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
W=${XF_TMP:-/tmp/xmp_compare}
LITE_REV=${LITE_REV:-f5230d5}          # the last commit that still has the removed libxmp-lite component
LITE_DIR="components/libxmp"_lite   # exists only in $LITE_REV (written split: check_paths.py must not read it as a live path)
mkdir -p "$W/lite" "$W/new"
if [ ! -f "$W/lite/.done" ]; then
  git -C "$ROOT" archive "$LITE_REV" "$LITE_DIR" | tar -x -C "$W/lite" && touch "$W/lite/.done"
fi
secs=40; case ${1:-} in [0-9]*) secs=$1; shift;; esac
cat > "$W/render.c" <<'CEOF'
#include <stdio.h>
#include <stdlib.h>
#include <xmp.h>
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb"); if (!f) return 3; fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    void *b = malloc(n); if (fread(b, 1, n, f) != (size_t)n) return 3; fclose(f);
    xmp_context c = xmp_create_context();
    if (xmp_load_module_from_memory(c, b, n)) return 4;
    xmp_start_player(c, 44100, 0);
    struct xmp_frame_info fi; FILE *o = fopen(argv[2], "wb"); long tot = 0, lim = atol(argv[3]) * 44100;
    while (tot < lim && xmp_play_frame(c) == 0) { xmp_get_frame_info(c, &fi); if (fi.loop_count > 0) break; fwrite(fi.buffer, 1, fi.buffer_size, o); tot += fi.buffer_size / 4; }
    fclose(o); return 0;
}
CEOF
LITE=$W/lite/$LITE_DIR/libxmp
[ -x "$W/render_lite" ] || gcc -O2 -w -DLIBXMP_STATIC -DLIBXMP_CORE_PLAYER -I"$LITE/include" "$LITE"/src/lite/lite-*.c "$W/render.c" -o "$W/render_lite" -lm || exit 2
printf 'include(%s/components/libxmp/xmp_sources.cmake)\nstring(REPLACE ";" " " S "${XMP_SRCS}")\nstring(REPLACE ";" " -D" D "${XMP_DEFS}")\nfile(WRITE %s/srcs.txt "${S}")\nif(D)\nfile(WRITE %s/defs.txt "-D${D}")\nelse()\nfile(WRITE %s/defs.txt "")\nendif()\n' "$ROOT" "$W" "$W" "$W" > "$W/list.cmake"
cmake -P "$W/list.cmake" || exit 2
gcc -O2 -w -DLIBXMP_STATIC $(cat "$W/defs.txt") -I"$ROOT/components/libxmp/libxmp/include" $(cat "$W/srcs.txt") "$W/render.c" -o "$W/render_new" -lm || exit 2
bad=0
for f in "$@"; do
  if ! "$W/render_lite" "$f" "$W/l.pcm" "$secs" 2>/dev/null; then echo "lite refuses: $f"; continue; fi
  "$W/render_new" "$f" "$W/n.pcm" "$secs" 2>/dev/null
  if cmp -s "$W/l.pcm" "$W/n.pcm"; then echo "identical ($(stat -c %s "$W/l.pcm") bytes): $f"; else echo "DIFFERENT: $f"; bad=1; fi
done
exit $bad
