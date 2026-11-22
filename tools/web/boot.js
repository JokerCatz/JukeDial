// SPDX-License-Identifier: LGPL-2.1-or-later
/* Last script of the page: the test hooks. A pretend device (pagemock.js) is loaded only when the address says so (webcheck: ?mockserial or ?demo); a published site does not have the file. */
if (/[?&](mockserial|demo)\b/.test(location.search)) { const s = document.createElement('script'); s.src = 'pagemock.js'; document.head.append(s) }
