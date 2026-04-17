// SPDX-License-Identifier: GPL-2.0-or-later

#include "radio-output.h"

static const char *radio_output_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("RadioOutput.Name");
}

static void radio_output_update(void *data, obs_data_t *settings)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(settings);
}

static void *radio_output_create(obs_data_t *settings, obs_output_t *output)
{
	struct radio_output *context = bzalloc(sizeof(struct radio_output));
	context->output = output;
	context->state = RADIO_STATE_DISCONNECTED;
	pthread_mutex_init(&context->state_mutex, NULL);

#ifdef HAVE_LIBSHOUT
	shout_init();
#endif

	radio_output_update(context, settings);
	return context;
}

static void radio_output_destroy(void *data)
{
	struct radio_output *context = data;
	if (!context)
		return;

#ifdef HAVE_LIBSHOUT
	if (context->shout) {
		shout_close(context->shout);
		shout_free(context->shout);
	}
	shout_shutdown();
#endif

	pthread_mutex_destroy(&context->state_mutex);
	bfree(context->host);
	bfree(context->mount);
	bfree(context->password);
	bfree(context);
}

static bool radio_output_start(void *data)
{
	UNUSED_PARAMETER(data);
	return false;
}

static void radio_output_stop(void *data, uint64_t ts)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(ts);
}

static void radio_output_encoded_packet(void *data, struct encoder_packet *packet)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(packet);
}

static obs_properties_t *radio_output_get_properties(void *data)
{
	UNUSED_PARAMETER(data);
	return NULL;
}

static void radio_output_get_defaults(obs_data_t *settings)
{
	UNUSED_PARAMETER(settings);
}

struct obs_output_info radio_output_info = {
	.id = "radio_output",
	.flags = OBS_OUTPUT_AUDIO | OBS_OUTPUT_ENCODED,
	.get_name = radio_output_get_name,
	.create = radio_output_create,
	.destroy = radio_output_destroy,
	.start = radio_output_start,
	.stop = radio_output_stop,
	.encoded_packet = radio_output_encoded_packet,
	.get_properties = radio_output_get_properties,
	.get_defaults = radio_output_get_defaults,
	.update = radio_output_update,
};
