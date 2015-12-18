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

#define CHAN_MAX 16
#define ZONE_MAX (CHAN_MAX / 2)

typedef struct _zone_t zone_t;
typedef struct _mpe_t mpe_t;
typedef struct _ref_t ref_t;
typedef struct _handle_t handle_t;

struct _ref_t {
	uint8_t chan;
	uint8_t key;
};

struct _zone_t {
	uint8_t base;
	uint8_t span;
	uint8_t ref;
	uint8_t range;
};

struct _mpe_t {
	uint8_t n_zones;
	zone_t zones [ZONE_MAX];
	int8_t channels [CHAN_MAX];
};

struct _handle_t {
	LV2_URID_Map *map;
	struct {
		LV2_URID midi_MidiEvent;
	} uris;
	espressivo_forge_t cforge;

	espressivo_dict_t dict [ESPRESSIVO_DICT_SIZE];
	ref_t ref [ESPRESSIVO_DICT_SIZE];

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *midi_out;

	mpe_t mpe;
};

static void
mpe_populate(mpe_t *mpe, uint8_t n_zones)
{
	assert(n_zones > 0);
	n_zones %= ZONE_MAX + 1; // wrap around if n_zones > ZONE_MAX
	int8_t rem = CHAN_MAX % n_zones;
	const uint8_t span = (CHAN_MAX - rem) / n_zones - 1;
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
		zones[i].range = 48;
	}

	for(uint8_t i=0; i<CHAN_MAX; i++)
		channels[i] = 0;
}

static uint8_t
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

static float
mpe_range_1(mpe_t *mpe, uint8_t zone_idx)
{
	zone_idx %= mpe->n_zones; // wrap around if zone_idx > n_zones
	zone_t *zone = &mpe->zones[zone_idx];
	return 1.f / (float)zone->range;
}

static void
mpe_release(mpe_t *mpe, uint8_t zone_idx, uint8_t ch)
{
	zone_idx %= mpe->n_zones; // wrap around if zone_idx > n_zones
	ch %= CHAN_MAX; // wrap around if ch > CHAN_MAX
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

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	handle_t *handle = calloc(1, sizeof(handle_t));
	if(!handle)
		return NULL;

	for(unsigned i=0; features[i]; i++)
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = (LV2_URID_Map *)features[i]->data;

	if(!handle->map)
	{
		fprintf(stderr, "%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	handle->uris.midi_MidiEvent = handle->map->map(handle->map->handle, LV2_MIDI__MidiEvent);
	espressivo_forge_init(&handle->cforge, handle->map);
	ESPRESSIVO_DICT_INIT(handle->dict, handle->ref);

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	handle_t *handle = (handle_t *)instance;

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
	handle_t *handle = (handle_t *)instance;

	const uint8_t n_zones = 1;
	mpe_populate(&handle->mpe, n_zones);
}

static inline LV2_Atom_Forge_Ref
_midi_event(handle_t *handle, int64_t frames, const uint8_t *m, size_t len)
{
	LV2_Atom_Forge *forge = &handle->cforge.forge;
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

static inline LV2_Atom_Forge_Ref
_midi_on(handle_t *handle, int64_t frames, const espressivo_event_t *cev)
{
	ref_t *ref = espressivo_dict_add(handle->dict, cev->sid);
	if(!ref)
		return 1;

	LV2_Atom_Forge_Ref fref;	

	const float val = _cps2midi(cev->dim[0]);

	const uint8_t chan = mpe_acquire(&handle->mpe, cev->gid);
	const uint8_t key = floor(val);
	const uint8_t vel = 0x7f;

	const uint8_t note_on [3] = {
		LV2_MIDI_MSG_NOTE_ON | chan,
		key,
		vel
	};
	fref = _midi_event(handle, frames, note_on, 3);
	
	ref->key = key;
	ref->chan = chan;

	return fref;
}

static inline LV2_Atom_Forge_Ref
_midi_off(handle_t *handle, int64_t frames, const espressivo_event_t *cev)
{
	ref_t *ref = espressivo_dict_del(handle->dict, cev->sid);
	if(!ref)
		return 1;

	LV2_Atom_Forge_Ref fref;

	const uint8_t chan = ref->chan;
	const uint8_t key = ref->key;
	const uint8_t vel = 0x7f;

	const uint8_t note_off [3] = {
		LV2_MIDI_MSG_NOTE_OFF | chan,
		key,
		vel
	};
	fref = _midi_event(handle, frames, note_off, 3);

	mpe_release(&handle->mpe, cev->gid, ref->chan);

	return fref;
}

static inline LV2_Atom_Forge_Ref
_midi_set(handle_t *handle, int64_t frames, const espressivo_event_t *cev)
{
	ref_t *ref = espressivo_dict_ref(handle->dict, cev->sid);
	if(!ref)
		return 1;

	LV2_Atom_Forge_Ref fref;

	const float val = _cps2midi(cev->dim[0]);
	
	const uint8_t chan = ref->chan;
	const uint8_t key = ref->key;

	// bender
	const uint16_t bnd = (val-key) * mpe_range_1(&handle->mpe, cev->gid) * 0x2000 + 0x1fff;
	const uint8_t bnd_msb = bnd >> 7;
	const uint8_t bnd_lsb = bnd & 0x7f;

	const uint8_t bend [3] = {
		LV2_MIDI_MSG_BENDER | chan,
		bnd_lsb,
		bnd_msb
	};
	fref = _midi_event(handle, frames, bend, 3);

	// pressure
	const uint16_t z = cev->dim[1] * 0x3fff;
	const uint8_t z_msb = z >> 7;
	const uint8_t z_lsb = z & 0x7f;

	const uint8_t pressure_lsb [3] = {
		LV2_MIDI_MSG_CONTROLLER | chan,
		LV2_MIDI_CTL_SC1_SOUND_VARIATION | 0x20,
		z_lsb
	};

	const uint8_t pressure_msb [3] = {
		LV2_MIDI_MSG_CONTROLLER | chan,
		LV2_MIDI_CTL_SC1_SOUND_VARIATION,
		z_msb
	};

	if(fref)
		fref = _midi_event(handle, frames, pressure_lsb, 3);
	if(fref)
		fref = _midi_event(handle, frames, pressure_msb, 3);

	// timbre
	const uint16_t vx = (cev->dim[2] * 0x2000) + 0x1fff; //TODO limit
	const uint8_t vx_msb = vx >> 7;
	const uint8_t vx_lsb = vx & 0x7f;

	const uint8_t timbre_lsb [3] = {
		LV2_MIDI_MSG_CONTROLLER | chan,
		LV2_MIDI_CTL_SC5_BRIGHTNESS | 0x20,
		vx_lsb
	};

	const uint8_t timbre_msb [3] = {
		LV2_MIDI_MSG_CONTROLLER | chan,
		LV2_MIDI_CTL_SC5_BRIGHTNESS,
		vx_msb
	};

	if(fref)
		fref = _midi_event(handle, frames, timbre_lsb, 3);
	if(fref)
		fref = _midi_event(handle, frames, timbre_msb, 3);

	// timbre
	const uint16_t vz = (cev->dim[3] * 0x2000) + 0x1fff; //TODO limit
	const uint8_t vz_msb = vz >> 7;
	const uint8_t vz_lsb = vz & 0x7f;

	const uint8_t mod_lsb [3] = {
		LV2_MIDI_MSG_CONTROLLER | chan,
		LV2_MIDI_CTL_LSB_MODWHEEL,
		vz_lsb
	};

	const uint8_t mod_msb [3] = {
		LV2_MIDI_MSG_CONTROLLER | chan,
		LV2_MIDI_CTL_MSB_MODWHEEL,
		vz_msb
	};

	if(fref)
		fref = _midi_event(handle, frames, mod_lsb, 3);
	if(fref)
		fref = _midi_event(handle, frames, mod_msb, 3);

	return fref;
}

static inline LV2_Atom_Forge_Ref
_midi_idle(handle_t *handle, int64_t frames, const espressivo_event_t *cev)
{
	LV2_Atom_Forge_Ref fref = 1;

	espressivo_dict_clear(handle->dict);

	return fref;
}

static inline LV2_Atom_Forge_Ref
_midi_init(handle_t *handle, int64_t frames)
{
	LV2_Atom_Forge_Ref fref = 1;
	mpe_t *mpe = &handle->mpe;

	for(unsigned z=0; z<mpe->n_zones; z++)
	{
		zone_t *zone = &mpe->zones[z];
		const uint8_t zone_ch = zone->base;
		const uint8_t voice_ch = zone->base + 1;

		// define zone span
		{
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

			if(fref)
				fref = _midi_event(handle, frames, lsb, 3);
			if(fref)
				fref = _midi_event(handle, frames, msb, 3);
			if(fref)
				fref = _midi_event(handle, frames, dat, 3);
		}

		// define zone bend range
		{
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
				2 //TODO make configurable
			};

			if(fref)
				fref = _midi_event(handle, frames, lsb, 3);
			if(fref)
				fref = _midi_event(handle, frames, msb, 3);
			if(fref)
				fref = _midi_event(handle, frames, dat, 3);
		}

		// define voice bend range
		{
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
				zone->range
			};

			if(fref)
				fref = _midi_event(handle, frames, lsb, 3);
			if(fref)
				fref = _midi_event(handle, frames, msb, 3);
			if(fref)
				fref = _midi_event(handle, frames, dat, 3);
		}
	}

	return fref;
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	handle_t *handle = (handle_t *)instance;

	// prepare midi atom forge
	const uint32_t capacity = handle->midi_out->atom.size;
	LV2_Atom_Forge *forge = &handle->cforge.forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->midi_out, capacity);
	LV2_Atom_Forge_Frame frame;
	LV2_Atom_Forge_Ref ref;
	ref = lv2_atom_forge_sequence_head(forge, &frame, 0);

	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		if(espressivo_event_check_type(&handle->cforge, &ev->body) && ref)
		{
			const int64_t frames = ev->time.frames;
			espressivo_event_t cev;

			espressivo_event_deforge(&handle->cforge, &ev->body, &cev);

			switch(cev.state)
			{
				case ESPRESSIVO_STATE_ON:
					ref = _midi_on(handle, frames, &cev);
					// fall-through
				case ESPRESSIVO_STATE_SET:
					if(ref)
						ref = _midi_set(handle, frames, &cev);
					break;
				case ESPRESSIVO_STATE_OFF:
					ref = _midi_off(handle, frames, &cev);
					break;
				case ESPRESSIVO_STATE_IDLE:
					ref = _midi_idle(handle, frames, &cev);
					break;
			}
		}
	}

	if(ref)
		lv2_atom_forge_pop(forge, &frame);
	else
		lv2_atom_sequence_clear(handle->midi_out);
}

static void
cleanup(LV2_Handle instance)
{
	handle_t *handle = (handle_t *)instance;

	free(handle);
}

const LV2_Descriptor mpe_out = {
	.URI						= ESPRESSIVO_MPE_OUT_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= NULL
};
