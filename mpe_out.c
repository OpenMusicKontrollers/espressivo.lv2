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

#define MAX_NPROPS (2 + MPE_ZONE_MAX*4)

typedef struct _targetI_t targetI_t;
typedef struct _plugstate_t plugstate_t;
typedef struct _plughandle_t plughandle_t;

struct _targetI_t {
	uint8_t chan;
	uint8_t zone;
	uint8_t key;
};

struct _plugstate_t {
	int32_t zones;
	int32_t velocity;
	int32_t master_range [MPE_ZONE_MAX];
	int32_t voice_range [MPE_ZONE_MAX];
	int32_t pressure_controller [MPE_ZONE_MAX];
	int32_t timbre_controller [MPE_ZONE_MAX];
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
	LV2_Atom_Sequence *midi_out;

	mpe_t mpe;
	PROPS_T(props, MAX_NPROPS);

	plugstate_t state;
	plugstate_t stash;
};

static inline void
mpe_populate(mpe_t *mpe, uint8_t n_zones)
{
	plughandle_t *handle = (void *)mpe - offsetof(plughandle_t, mpe);

	assert(n_zones > 0);
	n_zones %= MPE_ZONE_MAX + 1; // wrap around if n_zones > MPE_ZONE_MAX
	int8_t rem = MPE_CHAN_MAX % n_zones;
	const uint8_t span = (MPE_CHAN_MAX - rem) / n_zones - 1;
	uint8_t ptr = 0;

	mpe->n_zones = n_zones;
	zone_t *zones = mpe->zones;
	int8_t *channels = mpe->channels;

	for(uint8_t i=0;
		i<n_zones;
		rem--, ptr += 1 + zones[i++].span)
	{
		zones[i].base = ptr;
		zones[i].ref = 0;
		zones[i].span = span;
		if(rem > 0)
			zones[i].span += 1;
		zones[i].master_range = handle->state.master_range[i];
		zones[i].voice_range = handle->state.voice_range[i];
	}

	for(uint8_t i=0; i<MPE_CHAN_MAX; i++)
		channels[i] = 0;
}

static inline uint8_t
mpe_acquire(mpe_t *mpe, uint8_t zone_idx)
{
	zone_idx %= mpe->n_zones; // wrap around if zone_idx > n_zones
	zone_t *zone = &mpe->zones[zone_idx];
	int8_t *channels = mpe->channels;

	int8_t min = INT8_MAX;
	uint8_t pos = zone->ref; // start search at current channel
	const uint8_t base_1 = zone->base + 1;
	for(uint8_t i = zone->ref; i < zone->ref + zone->span; i++)
	{
		const uint8_t ch = base_1 + (i % zone->span); // wrap to [0..span]
		if(channels[ch] < min) // check for less occupation
		{
			min = channels[ch]; // lower minimum
			pos = i; // set new minimally occupied channel
		}
	}

	const uint8_t ch = base_1 + (pos % zone->span); // wrap to [0..span]
	if(channels[ch] <= 0) // off since long
		channels[ch] = 1;
	else
		channels[ch] += 1; // increase occupation
	zone->ref = (pos + 1) % zone->span; // start next search from next channel

	return ch;
}

static inline float
mpe_range_1(mpe_t *mpe, uint8_t zone_idx)
{
	zone_idx %= mpe->n_zones; // wrap around if zone_idx > n_zones
	zone_t *zone = &mpe->zones[zone_idx];
	return 1.f / (float)zone->voice_range;
}

static inline void
mpe_release(mpe_t *mpe, uint8_t zone_idx, uint8_t ch)
{
	zone_idx %= mpe->n_zones; // wrap around if zone_idx > n_zones
	ch %= MPE_CHAN_MAX; // wrap around if ch > MPE_CHAN_MAX
	zone_t *zone = &mpe->zones[zone_idx];
	int8_t *channels = mpe->channels;

	const uint8_t base_1 = zone->base + 1;
	for(uint8_t i = base_1; i < base_1 + zone->span; i++)
	{
		if( (i == ch) || (channels[i] <= 0) )
			channels[i] -= 1;
		// do not decrease occupied channels
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
		ref = lv2_atom_forge_write(forge, m, len);

	return ref;
}

static inline LV2_Atom_Forge_Ref
_zone_span_update(plughandle_t *handle, int64_t frames, unsigned zone_idx)
{
	LV2_Atom_Forge_Ref fref;
	mpe_t *mpe = &handle->mpe;
	zone_t *zone = &mpe->zones[zone_idx];
	const uint8_t zone_ch = zone->base;

	const uint8_t lsb [3] = {
		LV2_MIDI_MSG_CONTROLLER | zone_ch,
		LV2_MIDI_CTL_RPN_LSB,
		0x6 // zone
	};

	const uint8_t msb [3] = {
		LV2_MIDI_MSG_CONTROLLER | zone_ch,
		LV2_MIDI_CTL_RPN_MSB,
		0x0
	};

	const uint8_t dat [3] = {
		LV2_MIDI_MSG_CONTROLLER | zone_ch,
		LV2_MIDI_CTL_MSB_DATA_ENTRY,
		zone->span
	};

	fref = _midi_event(handle, frames, lsb, 3);
	if(fref)
		fref = _midi_event(handle, frames, msb, 3);
	if(fref)
		fref = _midi_event(handle, frames, dat, 3);

	return fref;
}

static inline LV2_Atom_Forge_Ref
_master_range_update(plughandle_t *handle, int64_t frames, unsigned zone_idx)
{
	LV2_Atom_Forge_Ref fref;
	mpe_t *mpe = &handle->mpe;
	zone_t *zone = &mpe->zones[zone_idx];
	const uint8_t zone_ch = zone->base;

	const uint8_t lsb [3] = {
		LV2_MIDI_MSG_CONTROLLER | zone_ch,
		LV2_MIDI_CTL_RPN_LSB,
		0x0, // bend range
	};

	const uint8_t msb [3] = {
		LV2_MIDI_MSG_CONTROLLER | zone_ch,
		LV2_MIDI_CTL_RPN_MSB,
		0x0,
	};
	const uint8_t dat [3] = {
		LV2_MIDI_MSG_CONTROLLER | zone_ch,
		LV2_MIDI_CTL_MSB_DATA_ENTRY,
		zone->master_range
	};

	fref = _midi_event(handle, frames, lsb, 3);
	if(fref)
		fref = _midi_event(handle, frames, msb, 3);
	if(fref)
		fref = _midi_event(handle, frames, dat, 3);

	return fref;
}

static inline LV2_Atom_Forge_Ref
_voice_range_update(plughandle_t *handle, int64_t frames, unsigned zone_idx)
{
	LV2_Atom_Forge_Ref fref;
	mpe_t *mpe = &handle->mpe;
	zone_t *zone = &mpe->zones[zone_idx];
	const uint8_t voice_ch = zone->base + 1;

	const uint8_t lsb [3] = {
		LV2_MIDI_MSG_CONTROLLER | voice_ch,
		LV2_MIDI_CTL_RPN_LSB,
		0x0, // bend range
	};

	const uint8_t msb [3] = {
		LV2_MIDI_MSG_CONTROLLER | voice_ch,
		LV2_MIDI_CTL_RPN_MSB,
		0x0,
	};
	const uint8_t dat [3] = {
		LV2_MIDI_MSG_CONTROLLER | voice_ch,
		LV2_MIDI_CTL_MSB_DATA_ENTRY,
		zone->voice_range
	};

	fref = _midi_event(handle, frames, lsb, 3);
	if(fref)
		fref = _midi_event(handle, frames, msb, 3);
	if(fref)
		fref = _midi_event(handle, frames, dat, 3);

	return fref;
}

static inline LV2_Atom_Forge_Ref
_full_update(plughandle_t *handle, int64_t frames)
{
	LV2_Atom_Forge_Ref fref = 1;
	mpe_t *mpe = &handle->mpe;

	for(unsigned z=0; z<mpe->n_zones; z++)
	{
		if(fref)
			fref = _zone_span_update(handle, frames, z);
		if(fref)
			fref = _master_range_update(handle, frames, z);
		if(fref)
			fref = _voice_range_update(handle, frames, z);
	}

	return fref;
}

static void
_intercept_zones(void *data, int64_t frames, props_impl_t *impl)
{
	plughandle_t *handle = data;

	mpe_populate(&handle->mpe, handle->state.zones);
	if(handle->ref)
		handle->ref = _full_update(handle, frames);
}

static void
_intercept_master(void *data, int64_t frames, props_impl_t *impl)
{
	plughandle_t *handle = data;

	const int zone_idx = (int32_t *)impl->value.body - handle->state.master_range; //TODO check
	handle->mpe.zones[zone_idx].master_range = handle->state.master_range[zone_idx];

	if( (zone_idx < handle->state.zones) && handle->ref) // update active zones only
		handle->ref = _master_range_update(handle, frames, zone_idx);
}

static void
_intercept_voice(void *data, int64_t frames, props_impl_t *impl)
{
	plughandle_t *handle = data;

	const int zone_idx = (int32_t *)impl->value.body - handle->state.voice_range; //TODO check
	handle->mpe.zones[zone_idx].voice_range = handle->state.voice_range[zone_idx];

	if( (zone_idx < handle->state.zones) && handle->ref) // update active zones only
		handle->ref = _voice_range_update(handle, frames, zone_idx);
}

#define MASTER_RANGE(NUM) \
{ \
	.property = ESPRESSIVO_URI"#mpe_master_range_"#NUM, \
	.offset = offsetof(plugstate_t, master_range) + (NUM-1)*sizeof(int32_t), \
	.type = LV2_ATOM__Int, \
	.event_cb = _intercept_master \
}

#define VOICE_RANGE(NUM) \
{ \
	.property = ESPRESSIVO_URI"#mpe_voice_range_"#NUM, \
	.offset = offsetof(plugstate_t, voice_range) + (NUM-1)*sizeof(int32_t), \
	.type = LV2_ATOM__Int, \
	.event_cb = _intercept_voice \
}

#define PRESSURE_CONTROLLER(NUM) \
{ \
	.property = ESPRESSIVO_URI"#mpe_pressure_controller_"#NUM, \
	.offset = offsetof(plugstate_t, pressure_controller) + (NUM-1)*sizeof(int32_t), \
	.type = LV2_ATOM__Int, \
}

#define TIMBRE_CONTROLLER(NUM) \
{ \
	.property = ESPRESSIVO_URI"#mpe_timbre_controller_"#NUM, \
	.offset = offsetof(plugstate_t, timbre_controller) + (NUM-1)*sizeof(int32_t), \
	.type = LV2_ATOM__Int, \
}

static const props_def_t defs [MAX_NPROPS] = {
	{
		.property = ESPRESSIVO_URI"#mpe_zones",
		.offset = offsetof(plugstate_t, zones),
		.type = LV2_ATOM__Int,
		.event_cb = _intercept_zones
	},
	{
		.property = ESPRESSIVO_URI"#mpe_velocity",
		.offset = offsetof(plugstate_t, velocity),
		.type = LV2_ATOM__Int
	},

	MASTER_RANGE(1),
	MASTER_RANGE(2),
	MASTER_RANGE(3),
	MASTER_RANGE(4),
	MASTER_RANGE(5),
	MASTER_RANGE(6),
	MASTER_RANGE(7),
	MASTER_RANGE(8),

	VOICE_RANGE(1),
	VOICE_RANGE(2),
	VOICE_RANGE(3),
	VOICE_RANGE(4),
	VOICE_RANGE(5),
	VOICE_RANGE(6),
	VOICE_RANGE(7),
	VOICE_RANGE(8),

	PRESSURE_CONTROLLER(1),
	PRESSURE_CONTROLLER(2),
	PRESSURE_CONTROLLER(3),
	PRESSURE_CONTROLLER(4),
	PRESSURE_CONTROLLER(5),
	PRESSURE_CONTROLLER(6),
	PRESSURE_CONTROLLER(7),
	PRESSURE_CONTROLLER(8),

	TIMBRE_CONTROLLER(1),
	TIMBRE_CONTROLLER(2),
	TIMBRE_CONTROLLER(3),
	TIMBRE_CONTROLLER(4),
	TIMBRE_CONTROLLER(5),
	TIMBRE_CONTROLLER(6),
	TIMBRE_CONTROLLER(7),
	TIMBRE_CONTROLLER(8)
};

static inline void
_upd(plughandle_t *handle, int64_t frames, const xpress_state_t *state,
	float val, targetI_t *src)
{
	// bender
	const uint16_t bnd = (val - src->key) * mpe_range_1(&handle->mpe, state->zone) * 0x2000 + 0x1fff;
	const uint8_t bnd_msb = bnd >> 7;
	const uint8_t bnd_lsb = bnd & 0x7f;

	const uint8_t bend [3] = {
		LV2_MIDI_MSG_BENDER | src->chan,
		bnd_lsb,
		bnd_msb
	};

	if(handle->ref)
		handle->ref = _midi_event(handle, frames, bend, 3);

	// pressure
	const uint16_t z = state->pressure * 0x3fff;
	const uint8_t z_msb = z >> 7;
	const uint8_t z_lsb = z & 0x7f;

	const uint8_t pressure_lsb [3] = {
		LV2_MIDI_MSG_CONTROLLER | src->chan,
		handle->state.pressure_controller[state->zone] | 0x20,
		z_lsb
	};

	const uint8_t pressure_msb [3] = {
		LV2_MIDI_MSG_CONTROLLER | src->chan,
		handle->state.pressure_controller[state->zone],
		z_msb
	};

	if(handle->ref)
		handle->ref = _midi_event(handle, frames, pressure_lsb, 3);
	if(handle->ref)
		handle->ref = _midi_event(handle, frames, pressure_msb, 3);

	// timbre
	float pos2 = state->timbre;
	if(pos2 < -1.f) pos2 = -1.f;
	else if(pos2 > 1.f) pos2 = 1.f;
	const uint16_t vx = (pos2 * 0x2000) + 0x1fff;
	const uint8_t vx_msb = vx >> 7;
	const uint8_t vx_lsb = vx & 0x7f;

	const uint8_t timbre_lsb [3] = {
		LV2_MIDI_MSG_CONTROLLER | src->chan,
		handle->state.timbre_controller[state->zone] | 0x20,
		vx_lsb
	};

	const uint8_t timbre_msb [3] = {
		LV2_MIDI_MSG_CONTROLLER | src->chan,
		handle->state.timbre_controller[state->zone],
		vx_msb
	};

	if(handle->ref)
		handle->ref = _midi_event(handle, frames, timbre_lsb, 3);
	if(handle->ref)
		handle->ref = _midi_event(handle, frames, timbre_msb, 3);

	// timbre 2
	/* FIXME
	float vel0 = state->dPitch;
	if(vel0 < -1.f) vel0 = -1.f;
	if(vel0 > 1.f) vel0 = 1.f;
	const uint16_t vz = (vel0 * 0x2000) + 0x1fff;
	const uint8_t vz_msb = vz >> 7;
	const uint8_t vz_lsb = vz & 0x7f;

	const uint8_t mod_lsb [3] = {
		LV2_MIDI_MSG_CONTROLLER | src->chan,
		LV2_MIDI_CTL_LSB_MODWHEEL,
		vz_lsb
	};

	const uint8_t mod_msb [3] = {
		LV2_MIDI_MSG_CONTROLLER | src->chan,
		LV2_MIDI_CTL_MSB_MODWHEEL,
		vz_msb
	};

	if(handle->ref)
		handle->ref = _midi_event(handle, frames, mod_lsb, 3);
	if(handle->ref)
		handle->ref = _midi_event(handle, frames, mod_msb, 3);
	*/
}

static void
_add(void *data, int64_t frames, const xpress_state_t *state,
	xpress_uuid_t uuid, void *target)
{
	plughandle_t *handle = data;
	targetI_t *src = target;

	const float val = state->pitch;

	src->chan = mpe_acquire(&handle->mpe, state->zone);
	src->zone = state->zone;
	src->key = floor(val);
	const uint8_t vel = handle->state.velocity;

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

	const float val = state->pitch;

	_upd(handle, frames, state, val, src);
}

static void
_del(void *data, int64_t frames,
	xpress_uuid_t uuid, void *target)
{
	plughandle_t *handle = data;
	targetI_t *src = target;

	const uint8_t vel = 0x0;

	const uint8_t note_off [3] = {
		LV2_MIDI_MSG_NOTE_OFF | src->chan,
		src->key,
		vel
	};

	if(handle->ref)
		handle->ref = _midi_event(handle, frames, note_off, 3);

	mpe_release(&handle->mpe, src->zone, src->chan);
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
			handle->midi_out = (LV2_Atom_Sequence *)data;
			break;
		default:
			break;
	}
}

static void
activate(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	const uint8_t n_zones = 1;
	mpe_populate(&handle->mpe, n_zones);
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

const LV2_Descriptor mpe_out = {
	.URI						= ESPRESSIVO_MPE_OUT_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
