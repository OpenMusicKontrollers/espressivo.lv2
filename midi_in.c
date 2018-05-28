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

#define MAX_NPROPS (3*0x10)

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
	float range [0x10];
	int32_t pressure [0x10];
	int32_t timbre [0x10];
};

struct _plughandle_t {
	LV2_URID_Map *map;
	struct {
		LV2_URID midi_MidiEvent;
		LV2_URID range [0x10];
	} uris;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Ref ref;

	const LV2_Atom_Sequence *control;
	LV2_Atom_Sequence *notify;

	PROPS_T(props, MAX_NPROPS);

	XPRESS_T(xpressO, MAX_NVOICES);
	targetO_t targetO [MAX_NVOICES];

	plugstate_t state;
	plugstate_t stash;

	uint16_t midi_rpn [0x10];
	uint16_t midi_data [0x10];
	int16_t midi_bender [0x10];
	float midi_offset [0x10];
};

static const targetO_t targetO_vanilla;

static inline void
_update_offset(plughandle_t *handle, uint8_t chan)
{
	handle->midi_offset[chan] = handle->midi_bender[chan] / 0x1fff * handle->state.range[chan];
}

static inline float
_get_pitch(plughandle_t *handle, uint8_t chan, uint8_t key)
{
	return ((float)key + handle->midi_offset[chan]) / 0x7f;
}

static void
_handle_midi_bender_internal(plughandle_t *handle, LV2_Atom_Forge *forge, int64_t frames,
	uint8_t chan)
{
	XPRESS_VOICE_FOREACH(&handle->xpressO, voice)
	{
		targetO_t *target = voice->target;

		if(target->chan != chan)
			continue; // channel not matching

		target->state.pitch = _get_pitch(handle, target->chan, target->key);

		if(handle->ref)
			handle->ref = xpress_token(&handle->xpressO, forge, frames, target->uuid, &target->state);
	}
}

static void
_intercept_midi_range(void *data, int64_t frames, props_impl_t *impl)
{
	plughandle_t *handle = data;

	const size_t chan = (float *)impl->value.body - handle->state.range;

	if( (chan >= 0x0) && (chan < 0xf) )
	{
		_update_offset(handle, chan);
		_handle_midi_bender_internal(handle, &handle->forge, frames, chan);
	}
}

#define RANGE(NUM) \
{ \
	.property = ESPRESSIVO_URI"#midi_range_"#NUM, \
	.offset = offsetof(plugstate_t, range) + (NUM-1)*sizeof(float), \
	.type = LV2_ATOM__Float, \
	.event_cb = _intercept_midi_range \
}

#define PRESSURE(NUM) \
{ \
	.property = ESPRESSIVO_URI"#midi_pressure_controller_"#NUM, \
	.offset = offsetof(plugstate_t, pressure) + (NUM-1)*sizeof(int32_t), \
	.type = LV2_ATOM__Int, \
}

#define TIMBRE(NUM) \
{ \
	.property = ESPRESSIVO_URI"#midi_timbre_controller_"#NUM, \
	.offset = offsetof(plugstate_t, timbre) + (NUM-1)*sizeof(int32_t), \
	.type = LV2_ATOM__Int, \
}

static const props_def_t defs [MAX_NPROPS] = {
	RANGE(1),
	RANGE(2),
	RANGE(3),
	RANGE(4),
	RANGE(5),
	RANGE(6),
	RANGE(7),
	RANGE(8),
	RANGE(9),
	RANGE(10),
	RANGE(11),
	RANGE(12),
	RANGE(13),
	RANGE(14),
	RANGE(15),
	RANGE(16),

	PRESSURE(1),
	PRESSURE(2),
	PRESSURE(3),
	PRESSURE(4),
	PRESSURE(5),
	PRESSURE(6),
	PRESSURE(7),
	PRESSURE(8),
	PRESSURE(9),
	PRESSURE(10),
	PRESSURE(11),
	PRESSURE(12),
	PRESSURE(13),
	PRESSURE(14),
	PRESSURE(15),
	PRESSURE(16),

	TIMBRE(1),
	TIMBRE(2),
	TIMBRE(3),
	TIMBRE(4),
	TIMBRE(5),
	TIMBRE(6),
	TIMBRE(7),
	TIMBRE(8),
	TIMBRE(9),
	TIMBRE(10),
	TIMBRE(11),
	TIMBRE(12),
	TIMBRE(13),
	TIMBRE(14),
	TIMBRE(15),
	TIMBRE(16)
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

	if(!handle->map)
	{
		fprintf(stderr, "%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	handle->uris.midi_MidiEvent = handle->map->map(handle->map->handle, LV2_MIDI__MidiEvent);
	handle->uris.range[0x0] = handle->map->map(handle->map->handle, ESPRESSIVO_URI"#midi_range_1");
	handle->uris.range[0x1] = handle->map->map(handle->map->handle, ESPRESSIVO_URI"#midi_range_2");
	handle->uris.range[0x2] = handle->map->map(handle->map->handle, ESPRESSIVO_URI"#midi_range_3");
	handle->uris.range[0x3] = handle->map->map(handle->map->handle, ESPRESSIVO_URI"#midi_range_4");
	handle->uris.range[0x4] = handle->map->map(handle->map->handle, ESPRESSIVO_URI"#midi_range_5");
	handle->uris.range[0x5] = handle->map->map(handle->map->handle, ESPRESSIVO_URI"#midi_range_6");
	handle->uris.range[0x6] = handle->map->map(handle->map->handle, ESPRESSIVO_URI"#midi_range_7");
	handle->uris.range[0x7] = handle->map->map(handle->map->handle, ESPRESSIVO_URI"#midi_range_8");
	handle->uris.range[0x8] = handle->map->map(handle->map->handle, ESPRESSIVO_URI"#midi_range_9");
	handle->uris.range[0x9] = handle->map->map(handle->map->handle, ESPRESSIVO_URI"#midi_range_10");
	handle->uris.range[0xa] = handle->map->map(handle->map->handle, ESPRESSIVO_URI"#midi_range_11");
	handle->uris.range[0xb] = handle->map->map(handle->map->handle, ESPRESSIVO_URI"#midi_range_12");
	handle->uris.range[0xc] = handle->map->map(handle->map->handle, ESPRESSIVO_URI"#midi_range_13");
	handle->uris.range[0xd] = handle->map->map(handle->map->handle, ESPRESSIVO_URI"#midi_range_14");
	handle->uris.range[0xe] = handle->map->map(handle->map->handle, ESPRESSIVO_URI"#midi_range_15");
	handle->uris.range[0xf] = handle->map->map(handle->map->handle, ESPRESSIVO_URI"#midi_range_16");

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
			handle->control = (const LV2_Atom_Sequence *)data;
			break;
		case 1:
			handle->notify = (LV2_Atom_Sequence *)data;
			break;
		default:
			break;
	}
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
_handle_midi_note_on(plughandle_t *handle, LV2_Atom_Forge *forge, int64_t frames,
	const uint8_t *m)
{
	const uint8_t chan = m[0] & 0x0f;
	const uint8_t key = m[1];

	xpress_uuid_t uuid;
	targetO_t *target = xpress_create(&handle->xpressO, &uuid);
	if(target)
	{
		const float pressure = (float)m[2] / 0x7f;

		*target = targetO_vanilla;
		target->chan = chan;
		target->key = key;
		target->uuid = uuid;
		target->state.zone = chan;
		target->state.pitch = _get_pitch(handle, target->chan, target->key);
		target->state.pressure = pressure;

		if(handle->ref)
			handle->ref = xpress_token(&handle->xpressO, forge, frames, target->uuid, &target->state);
	}
}

static void
_handle_midi_note_off(plughandle_t *handle, LV2_Atom_Forge *forge, int64_t frames,
	const uint8_t *m)
{
	const uint8_t chan = m[0] & 0x0f;
	const uint8_t key = m[1];

	xpress_uuid_t uuid;
	targetO_t *target = _midi_get(handle, chan, key, &uuid);
	if(target)
	{
		xpress_free(&handle->xpressO, uuid);

		if(handle->ref)
			handle->ref = xpress_alive(&handle->xpressO, forge, frames);
	}
}

static void
_handle_midi_note_pressure(plughandle_t *handle, LV2_Atom_Forge *forge, int64_t frames,
	const uint8_t *m)
{
	const uint8_t chan = m[0] & 0x0f;
	const uint8_t key = m[1];

	xpress_uuid_t uuid;
	targetO_t *target = _midi_get(handle, chan, key, &uuid);
	if(target)
	{
		const float pressure = (float)m[2] / 0x7f;
		target->state.pressure = pressure;

		if(handle->ref)
			handle->ref = xpress_token(&handle->xpressO, forge, frames, target->uuid, &target->state);
	}
}

static void
_handle_midi_bender(plughandle_t *handle, LV2_Atom_Forge *forge, int64_t frames,
	const uint8_t *m)
{
	const uint8_t chan = m[0] & 0x0f;

	handle->midi_bender[chan] = (((int16_t)m[2] << 7) | m[1]) - 0x2000;

	_update_offset(handle, chan);
	_handle_midi_bender_internal(handle, forge, frames, chan);
}

static void
_handle_midi_controller(plughandle_t *handle, LV2_Atom_Forge *forge, int64_t frames,
	const uint8_t *m)
{
	const uint8_t chan = m[0] & 0x0f;
	const uint8_t controller = m[1];
	const uint8_t value = m[2];

	switch(controller)
	{
		case LV2_MIDI_CTL_RPN_LSB:
		{
			handle->midi_rpn[chan] &= 0x3f80; // clear LSB
			handle->midi_rpn[chan] |= m[2];
		} break;
		case LV2_MIDI_CTL_RPN_MSB:
		{
			handle->midi_rpn[chan] &= 0x7f; // clear MSB
			handle->midi_rpn[chan] |= m[2] << 7;
		} break;

		case LV2_MIDI_CTL_LSB_DATA_ENTRY:
		{
			handle->midi_data[chan] &= 0x3f80; // clear LSB
			handle->midi_data[chan] |= m[2];
		} break;
		case LV2_MIDI_CTL_MSB_DATA_ENTRY:
		{
			handle->midi_data[chan] &= 0x7f; // clear MSB
			handle->midi_data[chan] |= m[2] << 7;

			switch(handle->midi_rpn[chan])
			{
				case 0x0: // MIDI pitch bend range
				{
					const uint8_t semi = handle->midi_data[chan] >> 7; // MSB
					const uint8_t cent = handle->midi_data[chan] & 0x7f; // LSB

					handle->state.range[chan] = (float)semi + cent*0.01f;

					_update_offset(handle, chan);
					_handle_midi_bender_internal(handle, forge, frames, chan);
					props_set(&handle->props, &handle->forge, frames,
						handle->uris.range[chan], &handle->ref);
				} break;
			}
		} break;

		default:
		{
			if(  (controller == handle->state.pressure[chan])
				|| (controller == (handle->state.pressure[chan] | 0x20))
				|| (controller == handle->state.timbre[chan])
				|| (controller == (handle->state.timbre[chan] | 0x20)) )
			{
				XPRESS_VOICE_FOREACH(&handle->xpressO, voice)
				{
					targetO_t *target = voice->target;
					bool put = false;

					if(target->chan != chan)
						continue; // channel not matching

					if(controller == (handle->state.pressure[chan] | 0x20))
					{
						target->pressure_lsb = value;
					}
					else if(controller == handle->state.pressure[chan])
					{
						target->state.pressure = (float)( (value << 7) | target->pressure_lsb) / 0x3fff;
						put = true;
					}
					else if(controller == (handle->state.timbre[chan] | 0x20))
					{
						target->timbre_lsb = value;
					}
					else if(controller == handle->state.timbre[chan])
					{
						target->state.timbre = (float)( (value << 7) | target->timbre_lsb) / 0x3fff;
						put = true;
					}

					if(put)
					{
						if(handle->ref)
							handle->ref = xpress_token(&handle->xpressO, forge, frames, target->uuid, &target->state);
					}
				}
			}
		} break;
	}
}

static void
_handle_midi(plughandle_t *handle, LV2_Atom_Forge *forge, int64_t frames,
	const uint8_t *m)
{
	const uint8_t comm = m[0] & 0xf0;

	switch(comm)
	{
		case LV2_MIDI_MSG_NOTE_ON:
		{
			_handle_midi_note_on(handle, forge, frames, m);
		} break;
		case LV2_MIDI_MSG_NOTE_OFF:
		{
			_handle_midi_note_off(handle, forge, frames, m);
		} break;
		case LV2_MIDI_MSG_NOTE_PRESSURE: //FIXME make this configurable
		{
			_handle_midi_note_pressure(handle, forge, frames, m);
		} break;
		case LV2_MIDI_MSG_BENDER:
		{
			_handle_midi_bender(handle, forge, frames, m);
		} break;
		case LV2_MIDI_MSG_CONTROLLER:
		{
			_handle_midi_controller(handle, forge, frames, m);
		} break;
	}
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;

	// prepare midi atom forge
	const uint32_t capacity = handle->notify->atom.size;
	LV2_Atom_Forge *forge = &handle->forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->notify, capacity);
	LV2_Atom_Forge_Frame frame;
	handle->ref = lv2_atom_forge_sequence_head(forge, &frame, 0);

	props_idle(&handle->props, forge, 0, &handle->ref);
	xpress_rst(&handle->xpressO);

	LV2_ATOM_SEQUENCE_FOREACH(handle->control, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		const int64_t frames = ev->time.frames;

		if(obj->atom.type == handle->uris.midi_MidiEvent)
		{
			const uint8_t *m = LV2_ATOM_BODY_CONST(&obj->atom);

			_handle_midi(handle, forge, frames, m);
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
		lv2_atom_sequence_clear(handle->notify);
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	if(handle)
	{
		xpress_deinit(&handle->xpressO);
		free(handle);
	}
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
