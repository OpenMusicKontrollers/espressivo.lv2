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

#include <espressivo.h>

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
		case 10:
			return &chord;
		case 11:
			return &sqew;
		case 12:
			return &monitor_out;
		case 13:
			return &modulator;
		case 14:
			return &redirector;
		case 15:
			return &midi_out;
		default:
			return NULL;
	}
}
