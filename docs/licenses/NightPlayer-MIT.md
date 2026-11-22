# NightPlayer (FakePod Nano firmware) - MIT

Register values, power sequences and calibration constants taken over as facts (CO5300 initialisation, CST820 x scale 29/30, CS43131 power-down timing, AXP2101 power-on source and soft power-off,
battery open-circuit table and the gauge fusion approach, low-battery thresholds) come from the working firmware of the same board: NightPlayer (c) 2026 ZYFDroid, MIT
(gitee ZYFDroid/night-player-fake-pod-nano, and the GitHub fork leosongyyy/night-player-fake-pod-nano for the same files). Our code is written fresh from those facts; the places are marked
"NightPlayer (c) 2026 ZYFDroid, MIT" in comments (bsp_co5300.c, bsp_touch.c, pc_cs43131.c, pc_axp2101.c, bsp_pmu.h, pc_pwrpol.h).

The Helix AAC component of that fork is RPSL-licensed and was NOT used.

MIT License

Copyright (c) 2026 ZYFDroid

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
