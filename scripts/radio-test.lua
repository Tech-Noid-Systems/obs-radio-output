-- radio-test.lua
--
-- DEVELOPMENT AID ONLY — not the supported way to use this plugin.
--
-- The supported configuration path is Tools → Radio Output… (config dialog).
-- The supported runtime-control path is the "Radio Output" dock
-- (View → Docks → Radio Output, with Start / Stop buttons + connection
-- status).  Both were added in Phase 2 Section B (PRs #22 and #24).
--
-- This script is retained as a minimal headless driver for quick
-- regression testing from the command line / Lua console — it bypasses
-- the config.json pipeline and hardcodes localhost:8000/stream settings.
-- Use it when you want to sanity-check the output code path without
-- exercising the UI.

local obs = obslua

local output = nil

function script_description()
	return "Radio Output Test — start/stop radio_output"
end

function start_output()
	if output then
		obs.obs_output_stop(output)
		obs.obs_output_release(output)
		output = nil
	end

	local settings = obs.obs_data_create()
	obs.obs_data_set_string(settings, "host",             "localhost")
	obs.obs_data_set_int(settings,    "port",             8000)
	obs.obs_data_set_string(settings, "mount",            "/stream")
	obs.obs_data_set_string(settings, "password",         "hackme")
	obs.obs_data_set_int(settings,    "codec",            1)   -- 1 = MP3
	obs.obs_data_set_int(settings,    "bitrate",          128)
	obs.obs_data_set_bool(settings,   "reconnect_enabled", true)
	obs.obs_data_set_int(settings,    "reconnect_delay",  5)
	obs.obs_data_set_int(settings,    "reconnect_max",    10)

	output = obs.obs_output_create("radio_output", "radio_output_inst", settings, nil)
	obs.obs_data_release(settings)

	local ok = obs.obs_output_start(output)
	print("obs_output_start returned: " .. tostring(ok))
end

function stop_output()
	if output then
		obs.obs_output_stop(output)
		obs.obs_output_release(output)
		output = nil
	end
end

function script_properties()
	local props = obs.obs_properties_create()
	obs.obs_properties_add_button(props, "start", "Start Radio Output",
		function() start_output() return true end)
	obs.obs_properties_add_button(props, "stop",  "Stop Radio Output",
		function() stop_output()  return true end)
	return props
end
