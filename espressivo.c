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

#include <stdatomic.h>

#include <espressivo.h>

static _Atomic uint32_t voice_id;

static uint32_t
_voice_map_new_id(void *handle)
{
	(void) handle;
	return atomic_fetch_sub_explicit(&voice_id, 1, memory_order_relaxed);
}

static voice_map_t voice_map = {
	.handle = NULL,
	.new_id = _voice_map_new_id
};

voice_map_t *voice_map_fallback = NULL;

#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
	if(!voice_map_fallback)
	{
		voice_map_fallback = &voice_map;
		atomic_init(&voice_id, UINT32_MAX);
	}

	switch(index)
	{
		case 0:
			return &tuio2_in;
		case 1:
			return &midi_in;
		case 2:
			return &mpe_out;
		case 3:
			return &snh;
		case 4:
			return &sc_out;
		default:
			return NULL;
	}
}
