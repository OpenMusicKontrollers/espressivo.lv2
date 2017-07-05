/*
 * Copyright (c) 2015-2017 Hanspeter Portner (dev@open-music-kontrollers.ch)
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

typedef struct _targetO_t targetO_t;
typedef struct _plugstate_t plugstate_t;
typedef struct _plughandle_t plughandle_t;

struct _targetO_t {
	uint8_t chan;
	uint8_t key;
	xpress_uuid_t uuid;

	xpress_state_t state;

	uint8_t pressure_lsb;
	uint8_t timbre_lsb;
};

struct _plugstate_t {
	int32_t range;
	int32_t cents [12];
};

struct _plughandle_t {
	LV2_URID_Map *map;
	struct {
		LV2_URID midi_MidiEvent;
	} uris;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Ref ref;

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *midi_out;

	PROPS_T(props, MAX_NPROPS);

	XPRESS_T(xpressO, MAX_NVOICES);
	targetO_t targetO [MAX_NVOICES];

	plugstate_t state;
	plugstate_t stash;
};

static const targetO_t targetO_vanilla;

#define CENTS(NUM) \
{ \
	.property = ESPRESSIVO_URI"#midi_cents_"#NUM, \
	.offset = offsetof(plugstate_t, cents) + (NUM-1)*sizeof(int32_t), \
	.type = LV2_ATOM__Int, \
}

static const props_def_t defs [MAX_NPROPS] = {
	{
		.property = ESPRESSIVO_URI"#midi_range",
		.offset = offsetof(plugstate_t, range),
		.type = LV2_ATOM__Int,
	},

	CENTS(1),
	CENTS(2),
	CENTS(3),
	CENTS(4),
	CENTS(5),
	CENTS(6),
	CENTS(7),
	CENTS(8),
	CENTS(9),
	CENTS(10),
	CENTS(11),
	CENTS(12)
};

static const xpress_iface_t ifaceO = {
	.size = sizeof(targetO_t)
};

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
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
	
	if(  !xpress_init(&handle->xpressO, MAX_NVOICES, handle->map, voice_map,
			XPRESS_EVENT_NONE, &ifaceO, handle->targetO, handle) )
	{
		free(handle);
		return NULL;
	}

	if(!props_init(&handle->props, descriptor->URI,
		defs, MAX_NPROPS, &handle->state, &handle->stash,
		handle->map, handle))
	{
		fprintf(stderr, "failed to allocate property structure\n");
		free(handle);
		return NULL;
	}

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	plughandle_t *handle = instance;

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
_midi_event(plughandle_t *handle, int64_t frames, const uint8_t *m, size_t len)
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

static targetO_t *
_midi_get(plughandle_t *handle, uint8_t chan, uint8_t key, xpress_uuid_t *uuid)
{
	XPRESS_VOICE_FOREACH(&handle->xpressO, voice)
	{
		targetO_t *dst = voice->target;

		if( (dst->chan == chan) && (dst->key == key) )
		{
			*uuid = voice->uuid;
			return dst;
		}
	}

	*uuid = 0;
	return NULL;
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;

	// prepare midi atom forge
	const uint32_t capacity = handle->midi_out->atom.size;
	LV2_Atom_Forge *forge = &handle->forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->midi_out, capacity);
	LV2_Atom_Forge_Frame frame;
	handle->ref = lv2_atom_forge_sequence_head(forge, &frame, 0);

	props_idle(&handle->props, forge, 0, &handle->ref);
	xpress_rst(&handle->xpressO);

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

				xpress_uuid_t uuid;
				targetO_t *target = xpress_create(&handle->xpressO, &uuid);
				if(target)
				{
					*target = targetO_vanilla;
					target->chan = chan;
					target->key = key;
					target->uuid = uuid;
					target->state.zone = chan;
					target->state.pitch = target->key;

					if(handle->ref)
						handle->ref = xpress_token(&handle->xpressO, forge, frames, target->uuid, &target->state);
				}
			}
			else if(comm == LV2_MIDI_MSG_NOTE_OFF)
			{
				const uint8_t key = m[1];

				xpress_uuid_t uuid;
				targetO_t *target = _midi_get(handle, chan, key, &uuid);
				if(target)
				{
					xpress_free(&handle->xpressO, uuid);

					if(handle->ref)
						handle->ref = xpress_alive(&handle->xpressO, forge, frames);
				}

				xpress_free(&handle->xpressO, uuid);
			}
			else if(comm == LV2_MIDI_MSG_NOTE_PRESSURE)
			{
				const uint8_t key = m[1];

				xpress_uuid_t uuid;
				targetO_t *target = _midi_get(handle, chan, key, &uuid);
				if(target)
				{
					const float pressure = m[2] * 0x1p-7;
					target->state.pressure = pressure;

					if(handle->ref)
						handle->ref = xpress_token(&handle->xpressO, forge, frames, target->uuid, &target->state);
				}
			}
			else if(comm == LV2_MIDI_MSG_BENDER)
			{
				const int16_t bender = (((int16_t)m[2] << 7) | m[1]) - 0x2000;
				const float offset = bender * 0x1p-13 * handle->state.range * 0.01f;

				XPRESS_VOICE_FOREACH(&handle->xpressO, voice)
				{
					targetO_t *target = voice->target;

					if(target->chan != chan)
						continue; // channel not matching

					target->state.pitch = (float)target->key + offset;

					if(handle->ref)
						handle->ref = xpress_token(&handle->xpressO, forge, frames, target->uuid, &target->state);
				}
			}
			else if(comm == LV2_MIDI_MSG_CONTROLLER)
			{
				const uint8_t controller = m[1];
				const uint8_t value = m[2];

				if(  (controller == (LV2_MIDI_CTL_SC5_BRIGHTNESS | 0x20) )
					|| (controller == LV2_MIDI_CTL_SC5_BRIGHTNESS)
					|| (controller == LV2_MIDI_CTL_LSB_MODWHEEL)
					|| (controller == LV2_MIDI_CTL_MSB_MODWHEEL) )
				{
					XPRESS_VOICE_FOREACH(&handle->xpressO, voice)
					{
						targetO_t *target = voice->target;
						bool put = false;

						if(target->chan  != chan)
							continue; // channel not matching

						switch(controller)
						{
							case LV2_MIDI_CTL_SC5_BRIGHTNESS | 0x20:
								target->pressure_lsb = value;
								break;
							case LV2_MIDI_CTL_SC5_BRIGHTNESS:
								target->state.pressure = ( (value << 7) | target->pressure_lsb) * 0x1p-14;
								put = true;
								break;
							case LV2_MIDI_CTL_LSB_MODWHEEL:
								target->timbre_lsb = value;
								break;
							case LV2_MIDI_CTL_MSB_MODWHEEL:
								target->state.timbre = ( (value << 7) | target->timbre_lsb) * 0x1p-14;
								put = true;
								break;
						}

						if(put)
						{
							if(handle->ref)
								handle->ref = xpress_token(&handle->xpressO, forge, frames, target->uuid, &target->state);
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

	if(handle->ref && !xpress_synced(&handle->xpressO))
		handle->ref = xpress_alive(&handle->xpressO, forge, nsamples-1);

	if(handle->ref)
		lv2_atom_forge_pop(forge, &frame);
	else
		lv2_atom_sequence_clear(handle->midi_out);
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	if(handle)
		free(handle);
}

static LV2_State_Status
_state_save(LV2_Handle instance, LV2_State_Store_Function store,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = instance;

	return props_save(&handle->props, store, state, flags, features);
}

static LV2_State_Status
_state_restore(LV2_Handle instance, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = instance;

	return props_restore(&handle->props, retrieve, state, flags, features);
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
