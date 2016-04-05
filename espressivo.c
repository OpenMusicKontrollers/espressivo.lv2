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

static _Atomic xpress_uuid_t voice_uuid = ATOMIC_VAR_INIT(INT64_MAX / UINT16_MAX * 1LL);

static xpress_uuid_t
_voice_map_new_uuid(void *handle)
{
	_Atomic xpress_uuid_t *uuid = handle;
	return atomic_fetch_add_explicit(uuid, 1, memory_order_relaxed);
}

xpress_map_t voice_map_fallback = {
	.handle = &voice_uuid,
	.new_uuid = _voice_map_new_uuid
};

#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
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
		case 5:
			return &through;
		case 6:
			return &reducto;
		case 7:
			return &discreto;
		case 8:
			return &mpe_in;
		case 9:
			return &tuio2_out;
		default:
			return NULL;
	}
}
