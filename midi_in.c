/*
 * Copyright (c) 2015 Hanspeter Portner (dev@open-music-kontrollers.ch)
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the Artistic License 2.0 as published by
 * The Perl Foundation.
 *
 * This source is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Artistic License 2.0 for more details.
 *
 * You should have received a copy of the Artistic License 2.0
 * along the source as a COPYING file. If not, obtain it from
 * http://www.perlfoundation.org/artistic_license_2_0.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <espressivo.h>
#include <props.h>

#define MAX_NPROPS (1 + 12)
#define MAX_NVOICES 64

typedef struct _target_t target_t;
typedef struct _handle_t handle_t;

struct _target_t {
	uint8_t key;
	xpress_uuid_t uuid;

	xpress_state_t state;

	uint8_t pressure_lsb;
	uint8_t timbre_lsb;
};

struct _handle_t {
	LV2_URID_Map *map;
	struct {
		LV2_URID midi_MidiEvent;
	} uris;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Ref ref;

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *midi_out;

	int32_t range;
	int32_t cents [MAX_NPROPS];

	PROPS_T(props, MAX_NPROPS);

	XPRESS_T(xpress, MAX_NVOICES);
	target_t target [MAX_NVOICES];
};

static const props_def_t stat_midi_range = {
	.property = ESPRESSIVO_URI"#midi_range",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Int,
	.mode = PROP_MODE_STATIC
};

static const props_def_t stat_midi_cents [MAX_NPROPS] = {
	[0] = {
		.property = ESPRESSIVO_URI"#midi_cents_1",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Int,
		.mode = PROP_MODE_STATIC
	},
	[1] = {
		.property = ESPRESSIVO_URI"#midi_cents_2",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Int,
		.mode = PROP_MODE_STATIC
	},
	[2] = {
		.property = ESPRESSIVO_URI"#midi_cents_3",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Int,
		.mode = PROP_MODE_STATIC
	},
	[3] = {
		.property = ESPRESSIVO_URI"#midi_cents_4",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Int,
		.mode = PROP_MODE_STATIC
	},
	[4] = {
		.property = ESPRESSIVO_URI"#midi_cents_5",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Int,
		.mode = PROP_MODE_STATIC
	},
	[5] = {
		.property = ESPRESSIVO_URI"#midi_cents_6",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Int,
		.mode = PROP_MODE_STATIC
	},
	[6] = {
		.property = ESPRESSIVO_URI"#midi_cents_7",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Int,
		.mode = PROP_MODE_STATIC
	},
	[7] = {
		.property = ESPRESSIVO_URI"#midi_cents_8",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Int,
		.mode = PROP_MODE_STATIC
	},
	[8] = {
		.property = ESPRESSIVO_URI"#midi_cents_9",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Int,
		.mode = PROP_MODE_STATIC
	},
	[9] = {
		.property = ESPRESSIVO_URI"#midi_cents_10",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Int,
		.mode = PROP_MODE_STATIC
	},
	[10] = {
		.property = ESPRESSIVO_URI"#midi_cents_11",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Int,
		.mode = PROP_MODE_STATIC
	},
	[11] = {
		.property = ESPRESSIVO_URI"#midi_cents_12",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Int,
		.mode = PROP_MODE_STATIC
	},
};

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	handle_t *handle = calloc(1, sizeof(handle_t));
	if(!handle)
		return NULL;

	xpress_map_t *voice_map = NULL;

	for(unsigned i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = features[i]->data;
		else if(!strcmp(features[i]->URI, ESPRESSIVO_URI"#voiceMap"))
			voice_map = features[i]->data;
	}

	if(!voice_map)
		voice_map = &voice_map_fallback;

	if(!handle->map)
	{
		fprintf(stderr, "%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	handle->uris.midi_MidiEvent = handle->map->map(handle->map->handle, LV2_MIDI__MidiEvent);

	lv2_atom_forge_init(&handle->forge, handle->map);
	
	if(!xpress_init(&handle->xpress, MAX_NVOICES, handle->map, voice_map,
			XPRESS_EVENT_NONE, NULL, NULL, NULL) )
	{
		free(handle);
		return NULL;
	}

	if(!props_init(&handle->props, MAX_NPROPS, descriptor->URI, handle->map, handle))
	{
		fprintf(stderr, "failed to allocate property structure\n");
		free(handle);
		return NULL;
	}

	LV2_URID urid = props_register(&handle->props, &stat_midi_range, PROP_EVENT_NONE, NULL, &handle->range);
	for(unsigned z=0; (z<12) && urid; z++)
	{
		urid = props_register(&handle->props, &stat_midi_cents[z], PROP_EVENT_NONE, NULL, &handle->cents[z]);
	}
	if(urid)
	{
		props_sort(&handle->props);
	}
	else
	{
		free(handle);
		return NULL;
	}

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	handle_t *handle = instance;

	switch(port)
	{
		case 0:
			handle->event_in = (const LV2_Atom_Sequence *)data;
			break;
		case 1:
			handle->midi_out = (LV2_Atom_Sequence *)data;
			break;
		default:
			break;
	}
}

static inline LV2_Atom_Forge_Ref
_midi_event(handle_t *handle, int64_t frames, const uint8_t *m, size_t len)
{
	LV2_Atom_Forge *forge = &handle->forge;
	LV2_Atom_Forge_Ref ref;
		
	ref = lv2_atom_forge_frame_time(forge, frames);
	if(ref)
		ref = lv2_atom_forge_atom(forge, len, handle->uris.midi_MidiEvent);
	if(ref)
		ref = lv2_atom_forge_raw(forge, m, len);
	if(ref)
		lv2_atom_forge_pad(forge, len);

	return ref;
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	handle_t *handle = instance;

	// prepare midi atom forge
	const uint32_t capacity = handle->midi_out->atom.size;
	LV2_Atom_Forge *forge = &handle->forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->midi_out, capacity);
	LV2_Atom_Forge_Frame frame;
	handle->ref = lv2_atom_forge_sequence_head(forge, &frame, 0);

	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		const int64_t frames = ev->time.frames;

		if(obj->atom.type == handle->uris.midi_MidiEvent)
		{
			const uint8_t *m = LV2_ATOM_BODY_CONST(&obj->atom);
			const uint8_t comm = m[0] & 0xf0;
			const uint8_t chan = m[0] & 0x0f;

			if(comm == LV2_MIDI_MSG_NOTE_ON)
			{
				const uint8_t key = m[1];
				const xpress_uuid_t uuid = ((int32_t)chan << 8) | key;

				target_t *target = xpress_add(&handle->xpress, uuid);
				if(target)
				{
					memset(target, 0x0, sizeof(target_t));
					target->key = key;
					target->uuid = xpress_map(&handle->xpress);
					target->state.zone = chan;
					target->state.position[0] = _midi2cps(target->key);

					if(handle->ref)
						handle->ref = xpress_put(&handle->xpress, forge, frames, target->uuid, &target->state);
				}
			}
			else if(comm == LV2_MIDI_MSG_NOTE_OFF)
			{
				const uint8_t key = m[1];
				const xpress_uuid_t uuid  = ((int32_t)chan << 8) | key;

				target_t *target = xpress_get(&handle->xpress, uuid);
				if(target)
				{
					if(handle->ref)
						handle->ref = xpress_del(&handle->xpress, forge, frames, target->uuid);
				}

				xpress_free(&handle->xpress, uuid);
			}
			else if(comm == LV2_MIDI_MSG_NOTE_PRESSURE)
			{
				const uint8_t key = m[1];
				const xpress_uuid_t uuid = ((int32_t)chan << 8) | key;

				target_t *target = xpress_get(&handle->xpress, uuid);
				if(target)
				{
					const float pressure = m[2] * 0x1p-7;
					target->state.position[1] = pressure;

					if(handle->ref)
						handle->ref = xpress_del(&handle->xpress, forge, frames, target->uuid);
				}
			}
			else if(comm == LV2_MIDI_MSG_BENDER)
			{
				const int16_t bender = (((int16_t)m[2] << 7) | m[1]) - 0x2000;
				const float offset = bender * 0x1p-13 * handle->range * 0.01f;

				XPRESS_VOICE_FOREACH(&handle->xpress, voice)
				{
					target_t *target = voice->target;

					if(target->state.zone != chan)
						continue; // channel not matching

					target->state.position[0] = (float)target->key + offset;

					if(handle->ref)
						handle->ref = xpress_del(&handle->xpress, forge, frames, target->uuid);
				}
			}
			else if(comm == LV2_MIDI_MSG_CONTROLLER)
			{
				const uint8_t controller = m[1];
				const uint8_t value = m[2];

				if(  (controller == LV2_MIDI_CTL_SC5_BRIGHTNESS)
					|| (controller == (LV2_MIDI_CTL_SC5_BRIGHTNESS | 0x20) )
					|| (controller == LV2_MIDI_CTL_LSB_MODWHEEL)
					|| (controller == LV2_MIDI_CTL_MSB_MODWHEEL) )
				{
					XPRESS_VOICE_FOREACH(&handle->xpress, voice)
					{
						target_t *target = voice->target;
						bool put = false;

						if(target->state.zone != chan)
							continue; // channel not matching

						switch(controller)
						{
							case LV2_MIDI_CTL_SC5_BRIGHTNESS:
								target->pressure_lsb = value;
								break;
							case LV2_MIDI_CTL_SC5_BRIGHTNESS | 0x20:
								target->state.position[1] = ( (value << 7) | target->pressure_lsb) * 0x1p-14;
								put = true;
								break;
							case LV2_MIDI_CTL_LSB_MODWHEEL:
								target->timbre_lsb = value;
								break;
							case LV2_MIDI_CTL_MSB_MODWHEEL:
								target->state.position[2] = ( (value << 7) | target->timbre_lsb) * 0x1p-14;
								put = true;
								break;
						}

						if(put)
						{
							if(handle->ref)
								handle->ref = xpress_del(&handle->xpress, forge, frames, target->uuid);
						}
					}
				}
			}
		}
		else
		{
			props_advance(&handle->props, forge, frames, obj, &handle->ref);
		}
	}

	if(handle->ref)
		lv2_atom_forge_pop(forge, &frame);
	else
		lv2_atom_sequence_clear(handle->midi_out);
}

static void
cleanup(LV2_Handle instance)
{
	handle_t *handle = instance;

	if(handle)
		free(handle);
}

static LV2_State_Status
_state_save(LV2_Handle instance, LV2_State_Store_Function store,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	handle_t *handle = instance;

	return props_save(&handle->props, &handle->forge, store, state, flags, features);
}

static LV2_State_Status
_state_restore(LV2_Handle instance, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	handle_t *handle = instance;

	return props_restore(&handle->props, &handle->forge, retrieve, state, flags, features);
}

static const LV2_State_Interface state_iface = {
	.save = _state_save,
	.restore = _state_restore
};

static const void *
extension_data(const char *uri)
{
	if(!strcmp(uri, LV2_STATE__interface))
		return &state_iface;
	return NULL;
}

const LV2_Descriptor midi_in = {
	.URI						= ESPRESSIVO_MIDI_IN_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
