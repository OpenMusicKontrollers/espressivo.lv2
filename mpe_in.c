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
#include <inttypes.h>
#include <math.h>

#include <espressivo.h>
#include <props.h>

#include <mpe.h>

#define MAX_NPROPS (1 + MPE_ZONE_MAX*2)
#define MAX_ZONES 8
#define MAX_CHANNELS 16

typedef struct _slot_t slot_t;
typedef struct _targetO_t targetO_t;
typedef struct _plugstate_t plugstate_t;
typedef struct _plughandle_t plughandle_t;

struct _slot_t {
	uint8_t zone;
	uint8_t master_channel;
	uint8_t num_voices;

	uint8_t master_bend_range;
	uint8_t voice_bend_range;

	int16_t master_bender;

	int16_t voice_bender [MAX_CHANNELS - 1];
	int16_t voice_pressure [MAX_CHANNELS - 1];
	int16_t voice_timbre [ MAX_CHANNELS - 1];
};

struct _targetO_t {
	uint8_t key;
	unsigned voice;
	xpress_uuid_t uuid;
	xpress_state_t state;
};

struct _plugstate_t {
	int32_t num_zones;
	int32_t master_range [MPE_ZONE_MAX];
	int32_t voice_range [MPE_ZONE_MAX];
};

struct _plughandle_t {
	LV2_URID_Map *map;
	struct {
		LV2_URID midi_MidiEvent;
	} uris;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Ref ref;

	const LV2_Atom_Sequence *midi_in;
	LV2_Atom_Sequence *event_out;

	PROPS_T(props, MAX_NPROPS);

	XPRESS_T(xpressO, MAX_NVOICES);
	targetO_t targetO [MAX_NVOICES];

	uint16_t data;
	uint16_t rpn;

	slot_t slots [MAX_ZONES];
	slot_t *index [MAX_CHANNELS];

	plugstate_t state;
	plugstate_t stash;
	struct {
		LV2_URID num_zones;
		LV2_URID master_range [MPE_ZONE_MAX];
		LV2_URID voice_range [MPE_ZONE_MAX];
	} urid;
};

static const targetO_t targetO_vanilla;

#define MASTER_RANGE(NUM) \
{ \
	.property = ESPRESSIVO_URI"#mpe_master_range_"#NUM, \
	.offset = offsetof(plugstate_t, master_range) + (NUM-1)*sizeof(int32_t), \
	.access = LV2_PATCH__readable, \
	.type = LV2_ATOM__Int, \
}

#define VOICE_RANGE(NUM) \
{ \
	.property = ESPRESSIVO_URI"#mpe_voice_range_"#NUM, \
	.offset = offsetof(plugstate_t, voice_range) + (NUM-1)*sizeof(int32_t), \
	.access = LV2_PATCH__readable, \
	.type = LV2_ATOM__Int, \
}

static const props_def_t defs [MAX_NPROPS] = {
	{
		.property = ESPRESSIVO_URI"#mpe_zones",
		.offset = offsetof(plugstate_t, num_zones),
		.access = LV2_PATCH__readable,
		.type = LV2_ATOM__Int,
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
	VOICE_RANGE(8)
};


static inline void
_slot_init(slot_t *slot, uint8_t zone, uint8_t master_channel, uint8_t num_voices)
{
	slot->zone = zone;
	slot->master_channel = master_channel;
	slot->num_voices = num_voices;

	slot->master_bend_range = 2;
	slot->voice_bend_range = 48;

	slot->master_bender = 0;

	const int16_t empty [MAX_CHANNELS - 1] = {
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};
	memcpy(slot->voice_bender, empty, sizeof(empty));
	memcpy(slot->voice_pressure, empty, sizeof(empty));
	memcpy(slot->voice_timbre, empty, sizeof(empty));
}

static inline void
_slots_init(plughandle_t *handle)
{
	_slot_init(&handle->slots[0], 0, 0, 15);
	for(unsigned i=1; i<MAX_ZONES; i++)
		_slot_init(&handle->slots[i], i, UINT8_MAX, 0);
}

static inline void
_index_update(plughandle_t *handle)
{
	memset(handle->index, 0x0, sizeof(handle->index));	

	for(unsigned i=0; i<MAX_ZONES; i++)
	{
		slot_t *slot = &handle->slots[i];

		if(slot->num_voices == 0)
			continue; // invalid

		for(unsigned j = slot->master_channel;
			j <= slot->master_channel + slot->num_voices;
			j++)
		{
			handle->index[j] = slot;
		}
	}
}

static inline bool
_slot_is_master(slot_t *slot, uint8_t chan)
{
	return slot->master_channel == chan;
}

static inline bool
_slot_is_first(slot_t *slot, uint8_t chan)
{
	return slot->master_channel + 1 == chan;	
}

static inline bool
_slot_is_voice(slot_t *slot, uint8_t chan)
{
	return (chan >= slot->master_channel + 1)
		&& (chan <= slot->master_channel + slot->num_voices);
}

static inline unsigned 
_slot_voice(slot_t *slot, uint8_t chan)
{
	return chan - slot->master_channel - 1;
}

static inline void
_zone_dump(plughandle_t *handle)
{
	for(unsigned i=0; i<MAX_ZONES; i++)
	{
		slot_t *slot = &handle->slots[i];

		if(slot->num_voices == 0)
			continue;

		printf("%u: %"PRIu8" %"PRIu8" %"PRIu8"\n", i, slot->zone, slot->master_channel, slot->num_voices);
	}
	printf("\n");
}

static inline void
_zone_notify(plughandle_t *handle, int64_t frames)
{
	handle->state.num_zones = 0;

	for(unsigned i=0; i<MAX_ZONES; i++)
	{
		slot_t *slot = &handle->slots[i];

		//printf("%u: %u %i %i\n", i, slot->num_voices, slot->master_bend_range, slot->voice_bend_range);

		handle->state.master_range[i] = slot->master_bend_range;
		props_set(&handle->props, &handle->forge, frames, handle->urid.master_range[i], &handle->ref);

		handle->state.voice_range[i] = slot->voice_bend_range;
		props_set(&handle->props, &handle->forge, frames, handle->urid.voice_range[i], &handle->ref);

		if(slot->num_voices == 0)
			continue; // invalid

		handle->state.num_zones += 1;
	}

	props_set(&handle->props, &handle->forge, frames, handle->urid.num_zones, &handle->ref);
}

static inline void
_zone_register(plughandle_t *handle, int64_t frames, uint8_t master_channel, uint8_t num_voices)
{
	if( (num_voices < 1) || (master_channel + num_voices > MAX_CHANNELS) )
		return; // invalid

	// make copy of current zone layout
	slot_t stash [MAX_ZONES];
	memcpy(stash, handle->slots, sizeof(stash));	

	const uint8_t master_end = master_channel + num_voices;

	// pass 1
	for(unsigned i=0; i<MAX_ZONES; i++)
	{
		slot_t *slot = &stash[i];

		// invalid?
		if(slot->num_voices == 0)
			continue;

		// shorten zone?
		if( (slot->master_channel < master_channel) && (slot->master_channel + slot->num_voices >= master_channel) )
		{
			slot->num_voices = master_channel - 1 - slot->master_channel;
			continue;
		}

		// overwrite master?
		if( (slot->master_channel >= master_channel) && (slot->master_channel <= master_end) )
		{
			slot->num_voices = 0; // invalidate
			continue;
		}

		// overwrite first voice?
		if( (slot->master_channel + 1 >= master_channel) && (slot->master_channel + 1 <= master_end) )
		{
			slot->num_voices = 0; // invalidate
			continue;
		}
	}

	// clear slots
	_slots_init(handle);

	// pass 2
	bool inserted = false;
	slot_t *dst = handle->slots;
	unsigned I = 0;
	for(unsigned i=0; i<MAX_ZONES; i++)
	{
		slot_t *slot = &stash[i];

		if(slot->num_voices == 0)
		{
			continue;
		}

		if(!inserted && (slot->master_channel > master_end) )
		{
			// insert new zone
			_slot_init(dst, i, master_channel, num_voices);
			dst++;

			inserted = true;
		}

		// clone zone
		memcpy(dst, slot, sizeof(slot_t));
		dst->zone = i;
		dst++;

		I = i + 1;
	}

	if(!inserted)
	{
		// insert new zone at end
		_slot_init(dst, I++, master_channel, num_voices);
		dst++;
	}

	_index_update(handle);
}

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

	unsigned p = 0;
	handle->urid.num_zones = props_map(&handle->props, defs[p++].property);
	handle->state.num_zones = 1;

	for(unsigned z=0; z<MPE_ZONE_MAX; z++)
	{
		handle->urid.master_range[z] = props_map(&handle->props, defs[p++].property);
		handle->state.master_range[z] = 2;
	}
	for(unsigned z=0; z<MPE_ZONE_MAX; z++)
	{
		handle->urid.voice_range[z] = props_map(&handle->props, defs[p++].property);
		handle->state.voice_range[z] = 48;
	}

	_slots_init(handle);
	_index_update(handle);

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	plughandle_t *handle = instance;

	switch(port)
	{
		case 0:
			handle->midi_in = (const LV2_Atom_Sequence *)data;
			break;
		case 1:
			handle->event_out = (LV2_Atom_Sequence *)data;
			break;
		default:
			break;
	}
}

static inline xpress_uuid_t
_uuid_create(slot_t *slot, uint8_t chan)
{
	return ((int32_t)slot->zone << 8) | chan;
}

static inline bool 
_mpe_in(plughandle_t *handle, int64_t frames, const LV2_Atom *atom)
{
	LV2_Atom_Forge *forge = &handle->forge;
	const uint8_t *m = LV2_ATOM_BODY_CONST(atom);
	const uint8_t comm = m[0] & 0xf0;
	const uint8_t chan = m[0] & 0x0f;

	bool zone_notify = false;

	switch(comm)
	{
		case LV2_MIDI_MSG_NOTE_ON:
		{
			slot_t *slot = handle->index[chan];

			if(slot && _slot_is_voice(slot, chan))
			{
				const unsigned voice = _slot_voice(slot, chan);
				const uint8_t key = m[1];
				const xpress_uuid_t uuid = _uuid_create(slot, chan);

				targetO_t *target = xpress_add(&handle->xpressO, uuid);
				if(target)
				{
					*target = targetO_vanilla;
					target->key = key;
					target->voice = voice;
					target->uuid = xpress_map(&handle->xpressO);

					const float offset_master = slot->master_bender * 0x1p-13 * slot->master_bend_range;
					const float offset_voice = slot->voice_bender[voice] * 0x1p-13 * slot->voice_bend_range;
					target->state.zone = slot->zone;
					target->state.pitch = ((float)target->key + offset_master + offset_voice) / 0x7f;

					if(handle->ref)
						handle->ref = xpress_token(&handle->xpressO, forge, frames, target->uuid, &target->state);
				}
			}

			break;
		}
		case LV2_MIDI_MSG_NOTE_OFF:
		{
			slot_t *slot = handle->index[chan];

			if(slot && _slot_is_voice(slot, chan))
			{
				const xpress_uuid_t uuid = _uuid_create(slot, chan);

				targetO_t *target = xpress_get(&handle->xpressO, uuid);
				if(target)
				{
#if 0
					if(handle->ref)
						handle->ref = xpress_del(&handle->xpressO, forge, frames, target->uuid);
#endif
				}

				xpress_free(&handle->xpressO, uuid);
			}

			break;
		}
		case LV2_MIDI_MSG_CHANNEL_PRESSURE:
		{
			slot_t *slot = handle->index[chan];

			if(slot && _slot_is_voice(slot, chan))
			{
				const xpress_uuid_t uuid = _uuid_create(slot, chan);
				const unsigned voice = _slot_voice(slot, chan);

				slot->voice_pressure[voice] = m[1] << 7;

				targetO_t *target = xpress_get(&handle->xpressO, uuid);
				if(target)
				{
					const float pressure = slot->voice_pressure[voice] / 0x3fff;
					target->state.pressure = pressure;

					if(handle->ref)
						handle->ref = xpress_token(&handle->xpressO, forge, frames, target->uuid, &target->state);
				}
			}

			break;
		}
		case LV2_MIDI_MSG_BENDER:
		{
			slot_t *slot = handle->index[chan];

			if(slot)
			{
				const int16_t bender = (((int16_t)m[2] << 7) | m[1]) - 0x2000;

				if(_slot_is_master(slot, chan))
				{
					slot->master_bender = bender;

					XPRESS_VOICE_FOREACH(&handle->xpressO, voice)
					{
						targetO_t *target = voice->target;

						if(target->state.zone != slot->zone)
							continue;

						const float offset_master = slot->master_bender * 0x1p-13 * slot->master_bend_range;
						const float offset_voice = slot->voice_bender[target->voice] * 0x1p-13 * slot->voice_bend_range;
						target->state.pitch = ((float)target->key + offset_master + offset_voice) / 0x7f;

						if(handle->ref)
							handle->ref = xpress_token(&handle->xpressO, forge, frames, target->uuid, &target->state);
					}
				}
				else if(_slot_is_voice(slot, chan))
				{
					const xpress_uuid_t uuid = _uuid_create(slot, chan);
					const unsigned voice = _slot_voice(slot, chan);

					slot->voice_bender[voice] = bender;

					targetO_t *target = xpress_get(&handle->xpressO, uuid);
					if(target)
					{
						const float offset_master = slot->master_bender * 0x1p-13 * slot->master_bend_range;
						const float offset_voice = slot->voice_bender[voice] * 0x1p-13 * slot->voice_bend_range;
						target->state.pitch = ((float)target->key + offset_master + offset_voice) / 0x7f;

						if(handle->ref)
							handle->ref = xpress_token(&handle->xpressO, forge, frames, target->uuid, &target->state);
					}
				}
			}

			break;
		}
		case LV2_MIDI_MSG_CONTROLLER:
		{
			const uint8_t controller = m[1];
			const uint8_t value = m[2];

			switch(controller)
			{
				case LV2_MIDI_CTL_LSB_DATA_ENTRY:
				{
					handle->data = (handle->data & 0x3f80) | value;

					break;
				}
				case LV2_MIDI_CTL_MSB_DATA_ENTRY:
				{
					handle->data = (handle->data & 0x7f) | (value << 7);

					if(handle->rpn == 6) // new MPE zone registered
					{
						const uint8_t zone_width = handle->data >> 7;

						_zone_register(handle, frames, chan, zone_width);
						zone_notify = true;
					}
					else if(handle->rpn == 0) // pitch-bend range registered
					{
						const uint8_t bend_range = handle->data >> 7;
						slot_t *slot = handle->index[chan];

						if(slot)
						{
							if(_slot_is_master(slot, chan))
							{
								slot->master_bend_range = bend_range;

								handle->state.master_range[slot->zone] = slot->master_bend_range;
								props_set(&handle->props, &handle->forge, frames, handle->urid.master_range[slot->zone], &handle->ref);
							}
							else if(_slot_is_first(slot, chan))
							{
								slot->voice_bend_range = bend_range;

								handle->state.voice_range[slot->zone] = slot->voice_bend_range;
								props_set(&handle->props, &handle->forge, frames, handle->urid.voice_range[slot->zone], &handle->ref);
							}
						}
					}

					break;
				}

				case LV2_MIDI_CTL_RPN_LSB:
				{
					handle->rpn = (handle->rpn & 0x3f80) | value;

					break;
				}
				case LV2_MIDI_CTL_RPN_MSB:
				{
					handle->rpn = (handle->rpn & 0x7f) | ((uint16_t)value << 7);

					break;
				}

				case LV2_MIDI_CTL_SC1_SOUND_VARIATION | 0x20:
				{
					slot_t *slot = handle->index[chan];

					if(slot && _slot_is_voice(slot, chan))
					{
						const unsigned voice = _slot_voice(slot, chan);
						slot->voice_pressure[voice] = (slot->voice_pressure[voice] & 0x3f80) | value;
					}

					break;
				}
				case LV2_MIDI_CTL_SC1_SOUND_VARIATION:
				{
					slot_t *slot = handle->index[chan];

					if(slot && _slot_is_voice(slot, chan))
					{
						const unsigned voice = _slot_voice(slot, chan);
						const xpress_uuid_t uuid = _uuid_create(slot, chan);

						slot->voice_pressure[voice] = (slot->voice_pressure[voice] & 0x7f) | ((uint16_t)value << 7);

						targetO_t *target = xpress_get(&handle->xpressO, uuid);
						if(target)
						{
							const float pressure = slot->voice_pressure[voice] / 0x3fff;
							target->state.pressure = pressure;

							if(handle->ref)
								handle->ref = xpress_token(&handle->xpressO, forge, frames, target->uuid, &target->state);
						}
					}

					break;
				}

				case LV2_MIDI_CTL_SC5_BRIGHTNESS | 0x20:
				{
					slot_t *slot = handle->index[chan];

					if(slot && _slot_is_voice(slot, chan))
					{
						const unsigned voice = _slot_voice(slot, chan);
						slot->voice_timbre[voice] = (slot->voice_timbre[voice] & 0x3f80) | value;
					}

					break;
				}
				case LV2_MIDI_CTL_SC5_BRIGHTNESS:
				{
					slot_t *slot = handle->index[chan];

					if(slot && _slot_is_voice(slot, chan))
					{
						const unsigned voice = _slot_voice(slot, chan);
						const xpress_uuid_t uuid = _uuid_create(slot, chan);

						slot->voice_timbre[voice] = (slot->voice_timbre[voice] & 0x7f) | ((uint16_t)value << 7);

						targetO_t *target = xpress_get(&handle->xpressO, uuid);
						if(target)
						{
							const float timbre = slot->voice_timbre[voice] / 0x3fff;
							target->state.timbre = timbre;

							if(handle->ref)
								handle->ref = xpress_token(&handle->xpressO, forge, frames, target->uuid, &target->state);
						}
					}

					break;
				}
			}

			break;
		}
	}

	return zone_notify;
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
	xpress_rst(&handle->xpressO);

	bool zone_notify = false;

	LV2_ATOM_SEQUENCE_FOREACH(handle->midi_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		const int64_t frames = ev->time.frames;

		if(obj->atom.type == handle->uris.midi_MidiEvent)
		{
			if(_mpe_in(handle, frames, &obj->atom))
				zone_notify = true;
		}
		else
		{
			props_advance(&handle->props, forge, frames, obj, &handle->ref);
		}
	}

	if(zone_notify)
		_zone_notify(handle, nsamples - 1);

	if(handle->ref && !xpress_synced(&handle->xpressO))
		handle->ref = xpress_alive(&handle->xpressO, forge, nsamples-1);

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

const LV2_Descriptor mpe_in = {
	.URI						= ESPRESSIVO_MPE_IN_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
