-- radio-test.lua
-- Manual test harness for obs-radio-output.
-- Load via OBS Tools → Scripts, then use the Start / Stop buttons.

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
