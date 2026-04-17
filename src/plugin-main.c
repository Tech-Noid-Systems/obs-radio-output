// SPDX-License-Identifier: GPL-2.0-or-later
/*
obs-radio-output
Copyright (C) 2026 Aaron Cupp <mrcupp@mrcupp.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <plugin-support.h>
#include "radio-output.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

/* OQ #3 — MP3 encoder availability diagnostic (temporary, remove after result recorded) */
static void mp3_encoder_diagnostic(void)
{
	static const char *candidates[] = {"ffmpeg_mp3", "mp3_audio_encoder", "libmp3lame"};
	for (size_t idx = 0; idx < sizeof(candidates) / sizeof(candidates[0]); idx++) {
		obs_encoder_t *enc = obs_audio_encoder_create(candidates[idx], "mp3_diag", NULL, 0, NULL);
		if (enc) {
			obs_log(LOG_INFO, "[mp3-diag] %s: AVAILABLE", candidates[idx]);
			obs_encoder_release(enc);
		} else {
			obs_log(LOG_INFO, "[mp3-diag] %s: NOT AVAILABLE", candidates[idx]);
		}
	}
}

bool obs_module_load(void)
{
	obs_register_output(&radio_output_info);
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	mp3_encoder_diagnostic();
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "plugin unloaded");
}
