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

#include <mpe.h>

#define MAX_NPROPS (4*0x10)

typedef struct _targetI_t targetI_t;
typedef struct _plugstate_t plugstate_t;
typedef struct _plughandle_t plughandle_t;

typedef enum _pressure_mode_t
{
	MODE_CONTROLLER       = 0,
	MODE_NOTE_PRESSURE    = 1,
	MODE_CHANNEL_PRESSURE = 2,
	MODE_NOTE_VELOCITY    = 3
} pressure_mode_t;

struct _targetI_t {
	uint8_t chan;
	uint8_t key;

	float range; //FIXME use this
	uint8_t pressure; //FIXME use this
	uint8_t timbre; //FIXME use this
	pressure_mode_t mode; //FIXME use this
};

struct _plugstate_t {
	float range [0x10];
	int32_t pressure [0x10];
	int32_t timbre [0x10];
	int32_t mode [0x10];
};

struct _plughandle_t {
	struct {
		LV2_URID midi_MidiEvent;
	} uris;

	LV2_URID_Map *map;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Ref ref;

	XPRESS_T(xpressI, MAX_NVOICES);
	targetI_t targetI [MAX_NVOICES];

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *event_out;

	PROPS_T(props, MAX_NPROPS);

	plugstate_t state;
	plugstate_t stash;
};

static inline LV2_Atom_Forge_Ref
_midi_event(plughandle_t *handle, int64_t frames, const uint8_t *m, size_t len)
{
	LV2_Atom_Forge *forge = &handle->forge;
	LV2_Atom_Forge_Ref ref;

	ref = lv2_atom_forge_frame_time(forge, frames);
	if(ref)
		ref = lv2_atom_forge_atom(forge, len, handle->uris.midi_MidiEvent);
	if(ref)
		ref = lv2_atom_forge_write(forge, m, len);

	return ref;
}

static void
_intercept_midi_range(void *data, int64_t frames, props_impl_t *impl)
{
	plughandle_t *handle = data;

	const size_t chan = (float *)impl->value.body - handle->state.range;

	if( (chan >= 0x0) && (chan < 0xf) )
	{
		const uint8_t rpn_lsb [3] = {
			LV2_MIDI_MSG_CONTROLLER | chan,
			LV2_MIDI_CTL_RPN_LSB,
			0x0
		};

		const uint8_t rpn_msb [3] = {
			LV2_MIDI_MSG_CONTROLLER | chan,
			LV2_MIDI_CTL_RPN_MSB,
			0x0
		};

		const float range = handle->state.range[chan];
		const uint8_t semis = floorf(range);
		const uint8_t cents = floorf( (range - semis) * 100.f);

		const uint8_t data_lsb [3] = {
			LV2_MIDI_MSG_CONTROLLER | chan,
			LV2_MIDI_CTL_LSB_DATA_ENTRY,
			cents
		};

		const uint8_t data_msb [3] = {
			LV2_MIDI_MSG_CONTROLLER | chan,
			LV2_MIDI_CTL_MSB_DATA_ENTRY,
			semis
		};

		if(handle->ref)
			handle->ref = _midi_event(handle, frames, rpn_lsb, 3);
		if(handle->ref)
			handle->ref = _midi_event(handle, frames, rpn_msb, 3);
		if(handle->ref)
			handle->ref = _midi_event(handle, frames, data_lsb, 3);
		if(handle->ref)
			handle->ref = _midi_event(handle, frames, data_msb, 3);
	}
}

#define RANGE(NUM) \
{ \
	.property = ESPRESSIVO_URI"#midi_range_"#NUM, \
	.offset = offsetof(plugstate_t, range) + (NUM-1)*sizeof(float), \
	.type = LV2_ATOM__Float, \
	.event_cb = _intercept_midi_range, \
}

#define PRESSURE(NUM) \
{ \
	.property = ESPRESSIVO_URI"#midi_pressure_"#NUM, \
	.offset = offsetof(plugstate_t, pressure) + (NUM-1)*sizeof(int32_t), \
	.type = LV2_ATOM__Int, \
}

#define TIMBRE(NUM) \
{ \
	.property = ESPRESSIVO_URI"#midi_timbre_"#NUM, \
	.offset = offsetof(plugstate_t, timbre) + (NUM-1)*sizeof(int32_t), \
	.type = LV2_ATOM__Int, \
}

#define MODE(NUM) \
{ \
	.property = ESPRESSIVO_URI"#midi_pressure_mode_"#NUM, \
	.offset = offsetof(plugstate_t, mode) + (NUM-1)*sizeof(int32_t), \
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
	TIMBRE(16),

	MODE(1),
	MODE(2),
	MODE(3),
	MODE(4),
	MODE(5),
	MODE(6),
	MODE(7),
	MODE(8),
	MODE(9),
	MODE(10),
	MODE(11),
	MODE(12),
	MODE(13),
	MODE(14),
	MODE(15),
	MODE(16)
};

static inline void
_upd(plughandle_t *handle, int64_t frames, const xpress_state_t *state,
	float val, targetI_t *src)
{
	// bender
	{
		const uint16_t bnd = (val - src->key) * src->range * 0x1fff + 0x2000;
		const uint8_t bnd_msb = bnd >> 7;
		const uint8_t bnd_lsb = bnd & 0x7f;

		const uint8_t bend [3] = {
			LV2_MIDI_MSG_BENDER | src->chan,
			bnd_lsb,
			bnd_msb
		};

		if(handle->ref)
			handle->ref = _midi_event(handle, frames, bend, 3);
	}

	// pressure
	{
		const uint16_t z = state->pressure * 0x3fff;
		const uint8_t z_msb = z >> 7;
		const uint8_t z_lsb = z & 0x7f;

		switch(src->mode)
		{
			case MODE_NOTE_PRESSURE:
			{
				const uint8_t note_pressure [3] = {
					LV2_MIDI_MSG_NOTE_PRESSURE | src->chan,
					src->key,
					z_msb
				};

				if(handle->ref)
					handle->ref = _midi_event(handle, frames, note_pressure, 3);
			} break;
			case MODE_CHANNEL_PRESSURE:
			{
				const uint8_t channel_pressure [2] = {
					LV2_MIDI_MSG_CHANNEL_PRESSURE | src->chan,
					z_msb
				};

				if(handle->ref)
					handle->ref = _midi_event(handle, frames, channel_pressure, 2);
			} break;
			case MODE_CONTROLLER:
			{
				const uint8_t pressure_lsb [3] = {
					LV2_MIDI_MSG_CONTROLLER | src->chan,
					src->pressure | 0x20,
					z_lsb
				};

				const uint8_t pressure_msb [3] = {
					LV2_MIDI_MSG_CONTROLLER | src->chan,
					src->pressure,
					z_msb
				};

				if(handle->ref)
					handle->ref = _midi_event(handle, frames, pressure_lsb, 3);
				if(handle->ref)
					handle->ref = _midi_event(handle, frames, pressure_msb, 3);
			} break;
			case MODE_NOTE_VELOCITY:
			{
				// nothing to do
			} break;
		}
	}

	// timbre
	{
		const uint16_t z = state->timbre * 0x3fff;
		const uint8_t z_msb = z >> 7;
		const uint8_t z_lsb = z & 0x7f;

		const uint8_t timbre_lsb [3] = {
			LV2_MIDI_MSG_CONTROLLER | src->chan,
			src->timbre | 0x20,
			z_lsb
		};

		const uint8_t timbre_msb [3] = {
			LV2_MIDI_MSG_CONTROLLER | src->chan,
			src->timbre,
			z_msb
		};

		if(handle->ref)
			handle->ref = _midi_event(handle, frames, timbre_lsb, 3);
		if(handle->ref)
			handle->ref = _midi_event(handle, frames, timbre_msb, 3);
	}

	// FIXME dPitch, dPressure, dTimbre
}

static void
_add(void *data, int64_t frames, const xpress_state_t *state,
	xpress_uuid_t uuid, void *target)
{
	plughandle_t *handle = data;
	targetI_t *src = target;

	const float val = state->pitch * 0x7f;

	// these will remain fixed per note
	src->chan = state->zone;
	src->key = floorf(val);
	src->mode = handle->state.mode[state->zone];
	src->range = handle->state.range[state->zone];
	src->pressure = handle->state.pressure[state->zone];
	src->timbre = handle->state.timbre[state->zone];

	const uint8_t vel = (src->mode == MODE_NOTE_VELOCITY)
		? state->pressure * 0x7f
		: 0x7f; //FIXME make this configurable

	const uint8_t note_on [3] = {
		LV2_MIDI_MSG_NOTE_ON | src->chan,
		src->key,
		vel
	};

	if(handle->ref)
		handle->ref = _midi_event(handle, frames, note_on, 3);

	_upd(handle, frames, state, val, src);
}

static void
_set(void *data, int64_t frames, const xpress_state_t *state,
	xpress_uuid_t uuid, void *target)
{
	plughandle_t *handle = data;
	targetI_t *src = target;

	const float val = state->pitch * 0x7f;

	_upd(handle, frames, state, val, src);
}

static void
_del(void *data, int64_t frames,
	xpress_uuid_t uuid, void *target)
{
	plughandle_t *handle = data;
	targetI_t *src = target;

	const uint8_t vel = 0x0; //FIXME maybe we want src->pressure here ?

	const uint8_t note_off [3] = {
		LV2_MIDI_MSG_NOTE_OFF | src->chan,
		src->key,
		vel
	};

	if(handle->ref)
		handle->ref = _midi_event(handle, frames, note_off, 3);
}

static const xpress_iface_t ifaceI = {
	.size = sizeof(targetI_t),

	.add = _add,
	.set = _set,
	.del = _del
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
		else if(!strcmp(features[i]->URI, XPRESS__voiceMap))
			voice_map = features[i]->data;
	}

	if(!handle->map)
	{
		fprintf(stderr, "%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	handle->uris.midi_MidiEvent = handle->map->map(handle->map->handle, LV2_MIDI__MidiEvent);

	lv2_atom_forge_init(&handle->forge, handle->map);

	if(!xpress_init(&handle->xpressI, MAX_NVOICES, handle->map, voice_map,
			XPRESS_EVENT_ALL, &ifaceI, handle->targetI, handle))
	{
		free(handle);
		return NULL;
	}

	if(!props_init(&handle->props, descriptor->URI,
		defs, MAX_NPROPS, &handle->state, &handle->stash,
		handle->map, handle))
	{
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
			handle->event_out = (LV2_Atom_Sequence *)data;
			break;
		default:
			break;
	}
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;

	// prepare midi atom forge
	const uint32_t capacity = handle->event_out->atom.size;
	LV2_Atom_Forge *forge = &handle->forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->event_out, capacity);
	LV2_Atom_Forge_Frame frame;
	handle->ref = lv2_atom_forge_sequence_head(forge, &frame, 0);

	props_idle(&handle->props, forge, 0, &handle->ref);
	xpress_pre(&handle->xpressI);

	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		const int64_t frames = ev->time.frames;

		if(!props_advance(&handle->props, forge, frames, obj, &handle->ref))
		{
			xpress_advance(&handle->xpressI, forge, frames, obj, &handle->ref);
		}
	}

	xpress_post(&handle->xpressI, nsamples-1);

	if(handle->ref)
		lv2_atom_forge_pop(forge, &frame);
	else
		lv2_atom_sequence_clear(handle->event_out);
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	if(handle)
	{
		xpress_deinit(&handle->xpressI);
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

const LV2_Descriptor midi_out = {
	.URI						= ESPRESSIVO_MIDI_OUT_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
