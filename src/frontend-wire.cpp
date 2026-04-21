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

#include "frontend-wire.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>
#include <util/bmem.h>
#include <util/platform.h>

#include <QMainWindow>
#include <QString>

#include <string>

#include "radio-output-config-dialog.hpp"
#include "radio-output-dock.hpp"
#include "radio-output.h"

namespace {

constexpr const char *kConfigFileName = "config.json";

/* Module-global config.  Owned by this translation unit. */
obs_data_t *g_config = nullptr;

/* Module-global active obs_output_t handle.  Populated by
 * frontend_wire_start_output, released by frontend_wire_stop_output.
 * nullptr when nothing is running. */
obs_output_t *g_active_output_handle = nullptr;

std::string config_file_path()
{
	char *raw = obs_module_config_path(kConfigFileName);
	if (!raw)
		return {};
	std::string path(raw);
	bfree(raw);
	return path;
}

std::string config_dir_path()
{
	char *raw = obs_module_config_path("");
	if (!raw)
		return {};
	std::string path(raw);
	bfree(raw);
	/* Trim trailing slash if present so os_mkdirs treats the dir correctly. */
	while (!path.empty() && (path.back() == '/' || path.back() == '\\'))
		path.pop_back();
	return path;
}

void apply_defaults(obs_data_t *data)
{
	obs_data_set_default_string(data, SETTING_HOST, "localhost");
	obs_data_set_default_int(data, SETTING_PORT, 8000);
	obs_data_set_default_string(data, SETTING_MOUNT, "/stream");
	obs_data_set_default_string(data, SETTING_PASSWORD, "");
	obs_data_set_default_int(data, SETTING_CODEC, RADIO_CODEC_MP3);
	obs_data_set_default_int(data, SETTING_BITRATE, 128);
	obs_data_set_default_bool(data, SETTING_RECONNECT, true);
	obs_data_set_default_int(data, SETTING_RECONNECT_DELAY, 5);
	obs_data_set_default_int(data, SETTING_RECONNECT_MAX, 10);
	obs_data_set_default_bool(data, SETTING_START_WITH_STREAMING, false);
}

void load_config_from_disk()
{
	const std::string path = config_file_path();
	if (!path.empty() && os_file_exists(path.c_str())) {
		g_config = obs_data_create_from_json_file(path.c_str());
		if (!g_config)
			obs_log(LOG_WARNING, "[frontend-wire] failed to parse %s; using defaults", path.c_str());
	}
	if (!g_config)
		g_config = obs_data_create();
	apply_defaults(g_config);
}

void save_config_to_disk()
{
	if (!g_config)
		return;

	const std::string dir = config_dir_path();
	if (!dir.empty())
		os_mkdirs(dir.c_str());

	const std::string path = config_file_path();
	if (path.empty())
		return;

	if (!obs_data_save_json_safe(g_config, path.c_str(), ".tmp", ".bak"))
		obs_log(LOG_WARNING, "[frontend-wire] failed to save config to %s", path.c_str());
}

void on_tools_menu_clicked(void * /*priv*/)
{
	if (!g_config)
		return;

	auto *parent = reinterpret_cast<QMainWindow *>(obs_frontend_get_main_window());
	RadioOutputConfigDialog dialog(g_config, parent);
	if (dialog.exec() == QDialog::Accepted)
		save_config_to_disk();
}

} // namespace

extern "C" void frontend_wire_load(void)
{
	load_config_from_disk();

	obs_frontend_add_tools_menu_item(obs_module_text("RadioOutput.Menu.OpenConfig"), on_tools_menu_clicked,
					 nullptr);

	/* Register the persistent dock.  OBS takes ownership of the widget
	 * and keeps it alive for the lifetime of the frontend. */
	auto *dock = new RadioOutputDock();
	obs_frontend_add_dock_by_id("obs-radio-output-dock", obs_module_text("RadioOutput.Dock.Name"), dock);
}

extern "C" void frontend_wire_unload(void)
{
	frontend_wire_stop_output();
	save_config_to_disk();

	if (g_config) {
		obs_data_release(g_config);
		g_config = nullptr;
	}
}

extern "C" obs_data_t *frontend_wire_get_config(void)
{
	return g_config;
}

extern "C" bool frontend_wire_start_output(void)
{
	if (g_active_output_handle) {
		obs_log(LOG_INFO, "[frontend-wire] output already running; start request ignored");
		return true;
	}
	if (!g_config) {
		obs_log(LOG_ERROR, "[frontend-wire] cannot start output: config not loaded");
		return false;
	}

	g_active_output_handle = obs_output_create("radio_output", "radio_output_inst", g_config, nullptr);
	if (!g_active_output_handle) {
		obs_log(LOG_ERROR, "[frontend-wire] obs_output_create failed");
		return false;
	}

	if (!obs_output_start(g_active_output_handle)) {
		obs_log(LOG_ERROR, "[frontend-wire] obs_output_start failed");
		obs_output_release(g_active_output_handle);
		g_active_output_handle = nullptr;
		return false;
	}

	return true;
}

extern "C" void frontend_wire_stop_output(void)
{
	if (!g_active_output_handle)
		return;

	obs_output_stop(g_active_output_handle);
	obs_output_release(g_active_output_handle);
	g_active_output_handle = nullptr;
}
