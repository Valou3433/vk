/*  
    This file is part of VK.

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
#include "storage/storage.h"
#include "memory/mem.h"
#include "filesystem/fs.h"

typedef struct ISO9660_TIME_REC
{
    u8 year;
    u8 month;
    u8 day;
    u8 hour;
    u8 minute;
    u8 second;
    int8_t timezone;
} __attribute__((packed)) iso9660_time_rec_t;

#define ISO9660_FLAG_HIDDEN 1
#define ISO9660_FLAG_DIR 2
#define ISO9660_FLAG_ASSOCIATED 4
#define ISO9660_FLAG_EXTENDED_INF 8
#define ISO9660_FLAG_EXTENDED_OWN 16
#define ISO9660_FLAG_FINAL 128

typedef struct ISO9660_DIR_ENTRY
{
    u8 length;
    u8 ext_length;
    u32 extent_start_lsb; //little endian
    u32 extent_start_msb; //big endian
    u32 extent_size_lsb;
    u32 extent_size_msb;
    struct ISO9660_TIME_REC record_time;
    u8 flags;
    u8 interleave_units;
	u8 interleave_gap;

	u16 volume_sequence_lsb;
	u16 volume_sequence_msb;

	u8 name_len;
    char name;
} __attribute__((packed)) iso9660_dir_entry_t;

typedef struct ISO_9660_DATE_TIME
{
	u8 year[4];
	u8 month[2];
	u8 day[2];
	u8 hour[2];
	u8 minute[2];
	u8 second[2];
	u8 hundredths[2];
	int8_t timezone;
} __attribute__((packed)) iso9660_date_time_t;

typedef struct ISO9660_PRIMARY_VOLUME_DESCRIPTOR
{
    u8 type; //if primary volume descriptor, 0x01;
    u8 identifier[5]; //CD001
    u8 version; //0x1
    u8 unused_0; //0x0
    u8 system_id[32]; //ex: LINUX, ...
    u8 volume_id[32];
    u8 unused_1[8]; //0x0
    u32 volume_space_lsb;
    u32 volume_space_msb;
    u8 unused_2[32]; //0x0
    u16 volume_set_lsb;
    u16 volume_set_msb;
    u16 volume_sequence_lsb;
    u16 volume_sequence_msb;
    u16 logical_block_size_lsb;
    u16 logical_block_size_msb;
    u32 path_table_size_lsb;
    u32 path_table_size_msb;
    u32 l_path_table_lba;
    u32 opt_l_path_table_lba; //optional, 0x0 = don't exist
    u32 m_path_table_lba;
    u32 opt_m_path_table_lba; //optional, 0x0 = don't exist
    u8 root_directory[34];//struct ISO9660_DIR_ENTRY root_directory;
    u8 volume_set_id[128];
    u8 volume_publisher[128];
    u8 data_preparer[128];
    u8 application_id[128];
    u8 copyright_file_id[38];
    u8 abstract_file_id[36];
    u8 bibliographic_file_id[37];
    struct ISO_9660_DATE_TIME creation_date_time;
    struct ISO_9660_DATE_TIME modification_date_time;
    struct ISO_9660_DATE_TIME expiration_date_time;
    struct ISO_9660_DATE_TIME effective_date_time;
    u8 file_structure_version; //0x1
    u8 unused_3; //0x0
    u8 application_used[512];
    u8 reserved[653];
} __attribute__((packed)) iso9660_primary_volume_descriptor_t;

typedef struct ISO9660_PATH_TABLE_ENTRY
{
    u8 name_len;
    u8 extended_attribute_rec_len;
    u32 extent_loc; //lba
    u16 parent_dir; //path table index
    u8 name[];
} __attribute__((packed)) iso9660_path_table_entry_t;

static void iso9660_get_fd(file_descriptor_t* dest, iso9660_dir_entry_t* dirent, iso9660fs_t* fs);

iso9660fs_t* iso9660fs_init(block_device_t* drive)
{
    //allocating data struct
    iso9660fs_t* tr = kmalloc(sizeof(iso9660fs_t));
    tr->drive = drive;

    //reading primary volume descriptor
    iso9660_primary_volume_descriptor_t pvd;
    block_read_flexible(0x10, 0, (u8*) &pvd, sizeof(pvd), drive);

    //making root_dir file descriptor
    iso9660_dir_entry_t* rd = (iso9660_dir_entry_t*) pvd.root_directory;
    iso9660_get_fd(&tr->root_dir, rd, tr);

    return tr;
}

list_entry_t* iso9660fs_read_dir(file_descriptor_t* dir, u32* size)
{
    iso9660fs_t* fs = dir->file_system;
    u64 lba = dir->fsdisk_loc;
    u32 length = (u32) dir->length;
    kprintf("length: %u B ; loc= lba %u\n", length, lba);
    iso9660_dir_entry_t* dirent = kmalloc(length);

    //TEMP : this can't work, because on multiple sector using, sectors are padded with 0s
    if(block_read_flexible(lba, 0, (u8*) dirent, length, fs->drive) != DISK_SUCCESS)
        return 0;
    
    u32 i = 0; u32 saved = 0;
    while(length){kprintf("%l%u ", i == saved ? 3:0, *((u8*)dirent+i));if(i==saved){saved += *((u8*)dirent+i);}i++;length-=4;}
    *size = 0;
    iso9660_dir_entry_t* db = dirent;
    while(length)
    {
        u32 cl = (u32) db->length + db->ext_length;
        if(!cl) {db++; length--; kprintf("%u ", *((u8*)db)); kprintf("(0x%X) %u\n", db, db->length); continue;}
        kprintf("db->length = %u , ext:%u ==> ", db->length, db->ext_length);
        kprintf("%lFile %s ; lba %u ; size %u B ; flags 0x%X\n", 3, &db->name, db->extent_start_lsb, db->extent_size_lsb, db->flags);
        db+=cl;
        length -= cl;
        (*size)++;
        if(!cl) break;
    }

    return 0;
}

static void iso9660_get_fd(file_descriptor_t* dest, iso9660_dir_entry_t* dirent, iso9660fs_t* fs)
{
    //copy name
    dest->name = kmalloc(dirent->name_len);
    strncpy(dest->name, &dirent->name, dirent->name_len);

    //set infos
    dest->file_system = fs;
    dest->parent_directory = 0;
    dest->fs_type = FS_TYPE_ISO9660;
    dest->fsdisk_loc = dirent->extent_start_lsb;
    dest->length = dirent->extent_size_lsb;

    //set attributes
    dest->attributes = 0;
    if(dirent->flags & ISO9660_FLAG_HIDDEN) dest->attributes |= FILE_ATTR_HIDDEN;
    if(dirent->flags & ISO9660_FLAG_DIR) dest->attributes |= FILE_ATTR_DIR;
}