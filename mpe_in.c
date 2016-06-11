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
#include <inttypes.h>
#include <math.h>

#include <espressivo.h>
#include <props.h>

#define MAX_NPROPS 1
#define MAX_NVOICES 64
#define MAX_ZONES 8
#define MAX_CHANNELS 16

typedef struct _slot_t slot_t;
typedef struct _target_t target_t;
typedef struct _state_t state_t;
typedef struct _plughandle_t plughandle_t;

struct _slot_t {
	uint8_t master_bend_range;
	uint8_t voice_bend_range;

	int16_t master_bender;
	int16_t voice_bender;

	int16_t voice_pressure;
	int16_t voice_timbre;
};

struct _target_t {
	uint8_t key;
	xpress_uuid_t uuid;

	/*FIXME
	xpress_state_t state;

	uint8_t pressure_lsb;
	uint8_t timbre_lsb;
	*/
};

struct _state_t {
	int32_t num_zones;
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

	XPRESS_T(xpress, MAX_NVOICES);
	target_t target [MAX_NVOICES];

	uint16_t data;
	uint16_t rpn;

	slot_t slots [MAX_CHANNELS];
	int index [MAX_CHANNELS];

	state_t state;
	state_t stash;
};

static const props_def_t stat_mpe_zones = {
	.property = ESPRESSIVO_URI"#mpe_range",
	.access = LV2_PATCH__readable,
	.type = LV2_ATOM__Int,
	.mode = PROP_MODE_STATIC
};

static inline void
_slot_init(slot_t *slot)
{
	slot->master_bend_range = 2;
	slot->voice_bend_range = 48;

	slot->master_bender = 0;
	slot->voice_bender = 0;
}

static inline void
_slots_init(plughandle_t *handle)
{
	for(unsigned i=0; i<MAX_CHANNELS; i++)
		_slot_init(&handle->slots[i]);
}

static inline void
_index_init(int *index)
{
	for(unsigned i=0; i<MAX_CHANNELS; i++)
		index[i] = 0;
}

static inline void
_index_dump(int *slots)
{
	for(unsigned i=0; i<MAX_CHANNELS; i++)
		printf("%3i ", slots[i]);
	printf("\n");
}

static inline void
_index_update(plughandle_t *handle, unsigned chan, unsigned width)
{
	const unsigned from = chan;
	const unsigned to = chan + 1 + width;
	int idx = -1;

	// find pre zone index 
	for(unsigned i=0; i<from; i++)
	{
		if(handle->index[i] > idx)
			idx = handle->index[i];
	}

	// invalidate overwritten master/first voice
	for(unsigned i=from; i<to; i++)
	{
		if(handle->index[i] > idx) // beginning of new zone
		{
			const int slot_i= handle->index[i];

			for(unsigned j=i; j<MAX_CHANNELS; j++)
			{
				if(handle->index[j] == slot_i)
					handle->index[j] = -1; // invalidate
				else
					handle->index[j] -= 1; // decrease
			}
		}
	}

	// set own zone index, init slot
	idx += 1;
	for(unsigned i=from; i<to; i++)
	{
		handle->index[i] = idx;
		_slot_init(&handle->slots[i]);
	}

	// invalidate/increase post zone indeces
	for(unsigned i=to; i<MAX_CHANNELS; i++)
	{
		if(handle->index[i] >= idx)
			handle->index[i] += 1; // increase
		else
			handle->index[i] = -1; // invalidate
	}
}

static inline bool
_channel_is_master(plughandle_t *handle, unsigned chan)
{
	if(chan == 0)
		return true;

	if(handle->index[chan - 1] != handle->index[chan])
		return true;

	return false;
}

static inline bool
_channel_is_first(plughandle_t *handle, unsigned chan)
{
	if(chan == 0)
		return false;

	if(handle->index[chan - 1] == handle->index[chan])
	{
		if( (chan == 1) || (handle->index[chan - 2] != handle->index[chan]) )
			return true;
	}

	return false;
}

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

	if(!props_register(&handle->props, &stat_mpe_zones, &handle->state.num_zones, &handle->stash.num_zones))
	{
		free(handle);
		return NULL;
	}

	_index_init(handle->index);
	_slots_init(handle);

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
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;

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
				const int idx = handle->index[chan];

				if(idx != -1)
				{
					//FIXME
				}
			}
			else if(comm == LV2_MIDI_MSG_NOTE_OFF)
			{
				const int idx = handle->index[chan];

				if(idx != -1)
				{
					//FIXME
				}
			}
			else if(comm == LV2_MIDI_MSG_CHANNEL_PRESSURE)
			{
				const int idx = handle->index[chan];

				if(idx != -1)
				{
					//FIXME
				}
			}
			else if(comm == LV2_MIDI_MSG_BENDER)
			{
				const int idx = handle->index[chan];

				if(idx != -1)
				{
					//FIXME
				}
			}
			else if(comm == LV2_MIDI_MSG_CONTROLLER)
			{
				const uint8_t controller = m[1];
				const uint8_t value = m[2];

				switch(controller)
				{
					case LV2_MIDI_CTL_LSB_DATA_ENTRY:
						handle->data = (handle->data & 0x3f80) | value;
						break;
					case LV2_MIDI_CTL_MSB_DATA_ENTRY:
						handle->data = (handle->data & 0x7f) | (value << 7);

						if(handle->rpn == 6)
						{
							const uint8_t zone_width = handle->data >> 7;
							//printf("setting zone width: %"PRIu8" %"PRIu8"\n", chan, zone_width);

							_index_update(handle, chan, zone_width);
						}
						else if(handle->rpn == 0)
						{
							const uint8_t bend_range = handle->data >> 7;
							const int idx = handle->index[chan];

							if(idx != -1)
							{
								if(_channel_is_master(handle, chan))
								{
									for(unsigned i=chan; i<MAX_CHANNELS; i++)
									{
										if(handle->index[i] == idx)
											handle->slots[i].master_bend_range = bend_range; //TODO check
									}
								}
								else if(_channel_is_first(handle, chan))
								{
									for(unsigned i=chan-1; i<MAX_CHANNELS; i++)
									{
										if(handle->index[i] == idx)
											handle->slots[i].voice_bend_range = bend_range; //TODO check
									}
								}
							}

							/* FIXME
							for(unsigned i=0; i<MAX_CHANNELS; i++)
								printf("%u %i %i\n", i, handle->slots[i].master_bend_range, handle->slots[i].voice_bend_range);
							printf("\n");
							*/
						}
						break;
					case LV2_MIDI_CTL_RPN_LSB:
						handle->rpn = (handle->rpn & 0x3f80) | value;
						break;
					case LV2_MIDI_CTL_RPN_MSB:
						handle->rpn = (handle->rpn & 0x7f) | ((uint16_t)value << 7);
						break;
					//FIXME SC5
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

	return props_save(&handle->props, &handle->forge, store, state, flags, features);
}

static LV2_State_Status
_state_restore(LV2_Handle instance, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = instance;

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
