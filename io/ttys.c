/*  
    This file is part of VK.
    Copyright (C) 2018 Valentin Haudiquet

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

#include "io.h"
#include "video/video.h"
#include "tasking/task.h"
#include "error/error.h"
#include "filesystem/devfs.h"

#define TTY_DEFAULT_BUFFER_SIZE 1024

tty_t* tty1 = 0;
tty_t* tty2 = 0;
tty_t* tty3 = 0;

tty_t* current_tty = 0;

void tty_init()
{
    kprintf("Initializing ttys...");

    tty1 = kmalloc(sizeof(tty_t)); tty1->buffer = kmalloc(TTY_DEFAULT_BUFFER_SIZE);
    tty1->count = 0; tty1->buffer_size = TTY_DEFAULT_BUFFER_SIZE;
    tty1->keyboard_stream = iostream_alloc();
    tty_write((u8*) "VK 0.0-indev (tty1)\n", 20, tty1);
    devfs_register_device(devfs->root_dir, "tty1", tty1, DEVFS_TYPE_TTY, 0);
    fd_t* tty1f = open_file("/dev/tty1", OPEN_MODE_RP);
    if(!tty1f) {vga_text_failmsg(); fatal_kernel_error("Failed to initialize TTY 1 (file can't be opened)", "TTY_INIT");}
    tty1->pointer = tty1f;

    tty2 = kmalloc(sizeof(tty_t)); tty2->buffer = kmalloc(TTY_DEFAULT_BUFFER_SIZE);
    tty2->count = 0; tty2->buffer_size = TTY_DEFAULT_BUFFER_SIZE;
    tty2->keyboard_stream = iostream_alloc();
    tty_write((u8*) "VK 0.0-indev (tty2)\n", 20, tty2);
    devfs_register_device(devfs->root_dir, "tty2", tty2, DEVFS_TYPE_TTY, 0);
    fd_t* tty2f = open_file("/dev/tty2", OPEN_MODE_RP);
    if(!tty2f) {vga_text_failmsg(); fatal_kernel_error("Failed to initialize TTY 2 (file can't be opened)", "TTY_INIT");}
    tty2->pointer = tty2f;

    tty3 = kmalloc(sizeof(tty_t)); tty3->buffer = kmalloc(TTY_DEFAULT_BUFFER_SIZE);
    tty3->count = 0; tty3->buffer_size = TTY_DEFAULT_BUFFER_SIZE;
    tty3->keyboard_stream = iostream_alloc();
    tty_write((u8*) "VK 0.0-indev (tty3)\n", 20, tty3);
    devfs_register_device(devfs->root_dir, "tty3", tty3, DEVFS_TYPE_TTY, 0);
    fd_t* tty3f = open_file("/dev/tty3", OPEN_MODE_RP);
    if(!tty3f) {vga_text_failmsg(); fatal_kernel_error("Failed to initialize TTY 3 (file can't be opened)", "TTY_INIT");}
    tty3->pointer = tty3f;

    current_tty = tty1;

    vga_text_okmsg();
}

/*
* This function writes to a virtual terminal
*/
u8 tty_write(u8* buffer, u32 count, tty_t* tty)
{
    if(tty->count+count > tty->buffer_size)
    {
        tty->buffer_size*=2;
        tty->buffer = krealloc(tty->buffer, tty->buffer_size);
    }

    memcpy(tty->buffer+tty->count, buffer, count);
    tty->count += count;

    if(tty == current_tty) 
    {
        u32 t = 0;
        while(count)
        {
            vga_text_putc(*(buffer+t), 0b00000111);
            count--;
            t++;
        }
    }

    return 0;
}

/*
* This function reads from a virtual terminal
* CARE : it actually reads the keyboard stream associated to this virtual terminal
* CARE : this function can block (wait keyboard IRQ)
*/
u8 tty_getch(tty_t* tty)
{
    if(!tty->keyboard_stream->count)
    {
        scheduler_wait_process(current_process, SLEEP_WAIT_IRQ, 1, 0);
        return tty_getch(tty);
    }

    u8 c = iostream_getch(tty->keyboard_stream);
    tty_write(&c, 1, tty);
    return c;
}

void tty_switch(tty_t* tty)
{
    if(tty != current_tty)
    {
        current_tty = tty;
        vga_text_tty_switch(current_tty);
    }
}
