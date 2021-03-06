/*  
    This file is part of VK.
    Copyright (C) 2017 Valentin Haudiquet

    VK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 2.

    VK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with VK.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "system.h"

bool alive = false;
u8 aboot_hint_present = 0;
bool asilent = false;

void args_parse(char* cmdline)
{
    if(!*cmdline) return;

    char* ndash = strchr(cmdline, '-');
    while(ndash)
    {
        if(strcfirst("-live", ndash) == 5)
        {alive = true; aboot_hint_present = KERNEL_MODE_LIVE;}
        if(strcfirst("-silent", ndash) == 7)
        {asilent = true;}
        
        ndash = strchr(ndash+1, '-');
    }
}