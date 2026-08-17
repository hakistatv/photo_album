#pragma once

/*
 * Firmware provenance. Lets a running device (via the undocumented /origin
 * route -- see web_server.c) or a raw flash dump (`strings` on a .bin will
 * turn up FW_ORIGIN_MARK) be traced back to this project, even if a fork's
 * web UI branding or README get changed downstream. Not meant to survive a
 * determined line-by-line rewrite -- just a low-effort, hard-to-miss check
 * against someone reflashing/redistributing this as their own.
 */

#define FW_ORIGIN_AUTHOR "Hakista TV"
#define FW_ORIGIN_REPO   "https://github.com/hakistatv/photo_album"
#define FW_ORIGIN_YT     "https://youtube.com/HakistaTV"

/* Stable token -- grep a strings dump of a suspect .bin for this exact
 * string to confirm it was built from this codebase. Don't rename it. */
#define FW_ORIGIN_MARK   "hakistatv-photo-album-fw"
