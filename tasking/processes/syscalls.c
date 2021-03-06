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
#include "tasking/task.h"
#include "video/video.h"
#include "memory/mem.h"
#include "syscalls.h"
#include "external_structures.h"
#include "filesystem/ext2.h"
#include "filesystem/iso9660.h"

static bool ptr_validate(u32 ptr, u32* page_directory);

void* system_calls[] = {0, syscall_open, syscall_close, syscall_read, syscall_write, 
syscall_link, syscall_unlink, syscall_seek, syscall_stat, syscall_rename, syscall_finfo, 
syscall_mount, syscall_umount, syscall_mkdir, syscall_readdir, syscall_openio, syscall_dup, syscall_fsinfo,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
syscall_fork, syscall_exit, syscall_exec, syscall_wait, syscall_getpinfo, syscall_setpinfo, 
syscall_sig, syscall_sigaction, syscall_sigret, syscall_sbrk,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
syscall_ioctl};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

void syscall_open(u32 ebx, u32 ecx, u32 edx)
{
    if(!ptr_validate(ebx, current_process->page_directory)) 
    {asm("mov $0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR):"%eax", "%ecx"); return;}

    char* path = (char*) ebx;
    u8 is_path_freeable = 0;
    if(*path != '/')
    {
        char* opath = path;
        u32 len = strlen(opath);
        u32 dirlen = strlen(current_process->current_dir);
        path = kmalloc(len+dirlen+2);
        strncpy(path, current_process->current_dir, dirlen);
        strncat(path, "/", 1);
        strncat(path, opath, len);
        *(path+len+dirlen+1) = 0;
        is_path_freeable = 1;
    }

    fd_t* file = open_file(path, (u8) ecx);
    if(is_path_freeable) kfree(path);
    if(!file) {asm("mov $0, %%eax ; mov %0, %%ecx"::"N"(ERROR_FILE_NOT_FOUND):"%eax", "%ecx"); return;}
    
    if(current_process->files_count == current_process->files_size)
    {
        current_process->files_size*=2; 
        current_process->files = krealloc(current_process->files, current_process->files_size*sizeof(fd_t));
        memset((void*)(((u32)current_process->files)+(current_process->files_size/2)*sizeof(fd_t)), 0, (current_process->files_size/2)*sizeof(fd_t));
    }

    u32 i = 0;
    for(i = 3;i<current_process->files_size;i++)
    {
        if(!current_process->files[i])
        {
            current_process->files[i] = file;
            break;
        }
    }
    current_process->files_count++;

    kprintf("%lSYS_OPEN(%s, %u) = 0x%X (%u)\n", 3, path, ecx, file, i);
    asm("mov %0, %%eax ; mov %1, %%ecx"::"g"(i), "N"(ERROR_NONE):"%eax", "%ecx");
}

void syscall_close(u32 ebx, u32 ecx, u32 edx)
{
    if((ebx >= 3) && (current_process->files_size >= ebx) && (current_process->files[ebx]))
    {
        fd_t* file = current_process->files[ebx]; 
        current_process->files[ebx] = 0;
        current_process->files_count--;
        close_file(file);
        
        kprintf("%lSYS_CLOSE : closed file %u\n", 3, ebx);
    }
}

void syscall_read(u32 ebx, u32 ecx, u32 edx)
{
    if((current_process->files_size < ebx) | (!current_process->files[ebx])) {asm("mov $0, %%eax ; mov %0, %%ecx"::"N"(ERROR_FILE_NOT_FOUND)); return;}
    if(!ptr_validate(ecx, current_process->page_directory)) {asm("mov $0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR)); return;}
    
    if(ebx >= 3) kprintf("%lSYSCALL_READ(%u, count %u)\n", 3, ebx, edx);

    u32 counttr = (u32) current_process->files[ebx]->offset;
    error_t tr = read_file(current_process->files[ebx], (void*) ecx, edx);
    counttr = (u32) (current_process->files[ebx]->offset - counttr);
    asm("mov %0, %%eax ; mov %1, %%ecx"::"g"(counttr), "g"(tr):"%eax", "%ecx");
}

void syscall_write(u32 ebx, u32 ecx, u32 edx)
{
    if((current_process->files_size < ebx) | (!current_process->files[ebx])) {asm("mov $0, %%eax ; mov %0, %%ecx"::"N"(ERROR_FILE_NOT_FOUND):"%eax", "%ecx"); return;}
    if(!ptr_validate(ecx, current_process->page_directory)) {asm("mov $0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR):"%eax", "%ecx"); return;}
    
    if(ebx >= 3) kprintf("%lSYS_WRITE(%u, count %u): ", 3, ebx, edx);

    u32 counttr = (u32) current_process->files[ebx]->offset;
    error_t tr = write_file(current_process->files[ebx], (u8*) ecx, edx);
    counttr = (u32) (current_process->files[ebx]->offset - counttr);
    asm("mov %0, %%eax ; mov %1, %%ecx"::"g"(counttr), "g"(tr):"%eax", "%ecx");
}

void syscall_link(u32 ebx, u32 ecx, u32 edx)
{
    if(!ptr_validate(ebx, current_process->page_directory)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR):"%eax", "%ecx"); return;}
    if(!ptr_validate(ecx, current_process->page_directory)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR):"%eax", "%ecx"); return;}
    char* oldpath = (char*) ebx;
    char* newpath = (char*) ecx;

    error_t tr = link(oldpath, newpath);
    asm("mov %0, %%eax ; mov %0, %%ecx"::"g"(tr):"%eax", "%ecx");
}

void syscall_unlink(u32 ebx, u32 ecx, u32 edx)
{
    if(!ptr_validate(ebx, current_process->page_directory)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR):"%eax", "%ecx"); return;}
    //kprintf("%lSYS_UNLINK(0x%X = %s)\n", 3, ebx, ebx);
    char* path = (char*) ebx;

    if(*path != '/')
    {
        char* opath = path;
        u32 len = strlen(opath);
        u32 dirlen = strlen(current_process->current_dir);
        path = kmalloc(len+dirlen+2);
        strncpy(path, current_process->current_dir, dirlen);
        strncat(path, "/", 1);
        strncat(path, opath, len);
        *(path+len+dirlen+1) = 0;    
    }

    error_t tr = unlink(path);
    asm("mov %0, %%eax ; mov %0, %%ecx"::"g"(tr):"%eax", "%ecx");
}

void syscall_seek(u32 ebx, u32 ecx, u32 edx)
{
    if((current_process->files_size < ebx) | (!current_process->files[ebx])) {asm("mov $0, %%eax ; mov %0, %%ecx"::"N"(ERROR_FILE_NOT_FOUND):"%eax", "%ecx"); return;}
    u32 offset = ecx;
    u32 whence = edx;
    //TODO : check file size and offset movement...
    if(whence == 0) current_process->files[ebx]->offset = offset; //SEEK_SET
    else if(whence == 1) current_process->files[ebx]->offset += offset; //SEEK_CUR
    else if(whence == 2) current_process->files[ebx]->offset = flength(current_process->files[ebx])+offset; //SEEK_END
    
    asm("mov %0, %%eax ; mov %1, %%ecx"::"g"((u32) current_process->files[ebx]->offset), "N"(ERROR_NONE):"%eax", "%ecx");
}

void syscall_stat(u32 ebx, u32 ecx, u32 edx)
{
    if((current_process->files_size < ebx) | (!current_process->files[ebx])) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_FILE_NOT_FOUND):"%eax", "%ecx"); return;}
    if(!ptr_validate(edx, current_process->page_directory)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR):"%eax", "%ecx"); return;}
    fsnode_t* file = current_process->files[ebx]->file;

    kprintf("%lSYS_STAT(%u, 0x%X)\n", 3, ebx, edx);

    stat_t* ptr = (stat_t*) edx;
    ptr->st_dev = 0; //todo : device ids //(u16) file->file_system->drive;
    ptr->st_ino = 0x20;
    ptr->st_mode = (file->attributes & FILE_ATTR_DIR) ? 0040000 : 0100000; //TODO: ptr[2] = current_process->files[ebx]->mode;
    ptr->st_nlink = file->hard_links;
    ptr->st_uid = 0; // user id
    ptr->st_gid = 0; // group id
    ptr->st_rdev = 0; // device id
    ptr->st_size = (u32) file->length;
    ptr->st_atime = file->last_access_time;
    ptr->st_mtime = file->last_modification_time;
    ptr->st_ctime = file->last_modification_time;
    ptr->st_blksize = 512; //todo: cluster size or ext2 blocksize
    ptr->st_blocks = (u32) (file->length/512); //todo: clusters or blocks

    //file-system dependant stats
    switch(file->file_system->fs_type)
    {
        case FS_TYPE_EXT2:
        {
            ptr->st_ino = ((ext2_node_specific_t*) file->specific)->inode_nbr;
            break;
        }
        case FS_TYPE_ISO9660:
        {
            ptr->st_ino = ((iso9660_node_specific_t*) file->specific)->extent_start;
            break;
        }
    }
    
    asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_NONE):"%eax", "%ecx");
}

void syscall_rename(u32 ebx, u32 ecx, u32 edx)
{
    if(!ptr_validate(ebx, current_process->page_directory)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR):"%eax", "%ecx"); return;}
    if(!ptr_validate(ecx, current_process->page_directory)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR):"%eax", "%ecx"); return;}
    char* oldpath = (char*) ebx;
    char* newname = (char*) ecx;

    error_t tr = rename(oldpath, newname);
    asm("mov %0, %%eax ; mov %0, %%ecx"::"g"(tr):"%eax", "%ecx");
}

void syscall_finfo(u32 ebx, u32 ecx, u32 edx)
{
    if((current_process->files_size < ebx) | (!current_process->files[ebx])) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_FILE_NOT_FOUND):"%eax", "%ecx"); return;}
    if(!ptr_validate(edx, current_process->page_directory)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR):"%eax", "%ecx"); return;}

    kprintf("%lSYS_FINFO(%u, 0x%X)\n", 3, ebx, edx);

    switch(ecx)
    {
        case VK_FINFO_DEVICE_TYPE: 
        {
            fsnode_t* node = current_process->files[ebx]->file;
            if(node->file_system->fs_type == FS_TYPE_DEVFS)
            {
                devfs_node_specific_t* spe = node->specific;
                *((u32*)edx) = spe->device_type;
            }
            else *((u32*)edx) = VK_NOT_A_DEVICE;
            
            break;
        }
        case VK_FINFO_PATH:
        {
            fd_t* file = current_process->files[ebx];
            size_t path_len = strlen(file->path);
            strncpy((char*) edx, file->path, path_len);
            *(((char*) edx)+path_len) = 0;

            break;
        }
        default:{asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(UNKNOWN_ERROR):"%eax", "%ecx"); return;}
    }
    
    asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_NONE):"%eax", "%ecx");
}

void syscall_mount(u32 ebx, u32 ecx, u32 edx)
{

}

void syscall_umount(u32 ebx, u32 ecx, u32 edx)
{

}

void syscall_mkdir(u32 ebx, u32 ecx, u32 edx)
{
    if(!ptr_validate(ebx, current_process->page_directory)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR):"%eax", "%ecx"); return;}

    char* path = (char*) ebx;
    if(*path != '/')
    {
        char* opath = path;
        u32 len = strlen(opath);
        u32 dirlen = strlen(current_process->current_dir);
        path = kmalloc(len+dirlen+2);
        strncpy(path, current_process->current_dir, dirlen);
        strncat(path, "/", 1);
        strncat(path, opath, len);
        *(path+len+dirlen+1) = 0;    
    }

    fsnode_t* node = create_file(path, FILE_ATTR_DIR);
    if(!node) asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(UNKNOWN_ERROR):"%eax", "%ecx");
    else asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_NONE):"%eax", "%ecx");
}

void syscall_readdir(u32 ebx, u32 ecx, u32 edx)
{
    if((current_process->files_size < ebx) | (!current_process->files[ebx])) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_FILE_NOT_FOUND):"%eax", "%ecx"); return;}
    if(!ptr_validate(edx, current_process->page_directory)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR):"%eax", "%ecx"); return;}
    u8* buffer = (u8*) edx;

    list_entry_t* list = kmalloc(sizeof(list_entry_t));
    u32 size = 0;
    error_t tr = read_directory(current_process->files[ebx], list, &size);
    if((tr != ERROR_NONE) | (ecx >= size))
    {
        if(tr == ERROR_NONE) tr = ERROR_FILE_OUT;
        list_free(list, size);
        asm("mov %0, %%eax ; mov %0, %%ecx"::"g"(tr):"%eax", "%ecx");
        return;
    }

    list_entry_t* ptr = list;
    u32 i = 0;
    for(i = 0; i < ecx; i++)
    {
        ptr = ptr->next;
    }
    dirent_t* dirent = ptr->element;

    /* assuming POSIX dirent with char name[256] */
    *((u32*) buffer) = dirent->inode;
    u32 minsize = dirent->name_len < 255 ? dirent->name_len : 255;
    memcpy(buffer+sizeof(u32), dirent->name, minsize);
    *(buffer+sizeof(u32)+minsize) = 0;

    list_free(list, size);

    asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_NONE):"%eax", "%ecx");
}

void syscall_openio(u32 ebx, u32 ecx, u32 edx)
{
    io_stream_t* iostream = iostream_alloc();
    fd_t* file = kmalloc(sizeof(fd_t));
    file->file = iostream->file; file->offset = 0;

    if(current_process->files_count == current_process->files_size)
    {current_process->files_size*=2; current_process->files = krealloc(current_process->files, current_process->files_size*sizeof(fd_t));}

    u32 i = 0;
    for(i = 3;i<current_process->files_size;i++)
    {
        if(!current_process->files[i])
        {
            current_process->files[i] = file;
            break;
        }
    }
    current_process->files_count++;

    asm("mov %0, %%eax ; mov %1, %%ecx"::"g"(i), "N"(ERROR_NONE):"%eax", "%ecx");
}

void syscall_dup(u32 ebx, u32 ecx, u32 edx)
{
    if((current_process->files_size < ebx) | (!current_process->files[ebx])) {asm("mov $0, %%eax ; mov %0, %%ecx"::"N"(ERROR_FILE_NOT_FOUND)); return;}

    fd_t* oldf = current_process->files[ebx];
    int new = 0;
    if(ecx)
    {
        if(ecx < 3) {asm("mov $0, %%eax ; mov %0, %%ecx"::"N"(UNKNOWN_ERROR):"%eax", "%ecx"); return;}

        while(current_process->files_size < ecx)
        {
            current_process->files_size*=2; 
            current_process->files = krealloc(current_process->files, current_process->files_size*sizeof(fd_t)); 
            memset(current_process->files+current_process->files_size/2, 0, current_process->files_size/2);
        }

        oldf->instances++;
        if(current_process->files[ecx]) close_file(current_process->files[ecx]);
        current_process->files[ecx] = oldf;
        new = (int) ecx;
    }
    else
    {
        if(current_process->files_count == current_process->files_size)
        {
            current_process->files_size*=2; 
            current_process->files = krealloc(current_process->files, current_process->files_size*sizeof(fd_t));
            memset(current_process->files+current_process->files_size/2, 0, current_process->files_size/2);
        }

        u32 i = 0;
        for(i = 3;i<current_process->files_size;i++)
        {
            if(!current_process->files[i])
            {
                current_process->files[i] = oldf;
                break;
            }
        }
        oldf->instances++;
        current_process->files_count++;
        new = (int) i;
    }

    kprintf("%lSYS_DUP: duplicated file %d (0x%X) to %d %s\n", 3, ebx, oldf, new, ecx ? "(by choice)" : "(by default)");
    asm("mov %0, %%eax ; mov %1, %%ecx"::"g"(new), "N"(ERROR_NONE):"%eax", "%ecx");
}

void syscall_fsinfo(u32 ebx, u32 ecx, u32 edx)
{
    if(!ptr_validate(ecx, current_process->page_directory)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR):"%eax", "%ecx"); return;}

    switch(ebx)
    {
        case VK_FSINFO_MOUNTED_FS_NUMBER:
        {
            *((u32*) ecx) = current_mount_points;
            asm("mov %0, %%eax ; mov %1, %%ecx"::"g"(current_mount_points), "N"(ERROR_NONE):"%eax", "%ecx"); return;
        }
        case VK_FSINFO_MOUNTED_FS_ALL:
        {
            mount_point_t* ptr = root_point;
            while(ptr)
            {
                file_system_t* fs = ptr->fs;
                statfs_t* dest = (statfs_t*) ecx;
                dest->f_type = fs->fs_type;
                dest->f_flags = fs->flags;
                dest->f_bsize = 512; //TODO
                //TODO
                if(fs->partition) dest->f_blocks = fs->drive->partitions[fs->partition-1]->length;
                else dest->f_blocks = fs->drive->device_size;
                dest->f_bfree = dest->f_blocks;
                dest->f_bavail = dest->f_bfree;
                dest->f_files = U32_MAX-1;
                dest->f_ffree = dest->f_files;
                dest->f_fsid = (u32) fs;
                u32 len = strlen(ptr->path); if(len > 99) len = 99; //TODO assert
                memcpy(dest->mount_path, ptr->path, len);
                *(dest->mount_path+len) = 0;

                dest++;
                ptr = ptr->next;
            }
            asm("mov %0, %%eax ; mov %1, %%ecx"::"g"(current_mount_points), "N"(ERROR_NONE):"%eax", "%ecx"); return;
        }
    }

    asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(UNKNOWN_ERROR):"%eax", "%ecx"); 
}

void syscall_exit(u32 ebx, u32 ecx, u32 edx)
{
    exit_process(current_process, EXIT_CONDITION_USER | ((u8) ebx));
}

void syscall_exec(u32 ebx, u32 ecx, u32 edx)
{
    if((current_process->files_size < ebx) | (!current_process->files[ebx])) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_FILE_NOT_FOUND):"%eax", "%ecx"); return;}
    
    error_t is_elf = elf_check(current_process->files[ebx]);
    if(is_elf != ERROR_NONE) {asm("mov %0, %%eax ; mov %0, %%ecx"::"g"(is_elf):"%eax", "%ecx"); return;}

    //copy/counts arguments before process memory freeing
    int argc = 0;
    u32 argv_size = 5;
    char** argv = kmalloc(sizeof(char*)*argv_size);
    while(true)
    {
        char* oldarg = ((char**) edx)[argc];
        if(oldarg == 0) {argv[argc] = 0; break;}

        size_t len = strlen(oldarg);
        char* newarg = kmalloc(len+1);
        strcpy(newarg, oldarg);
        argv[argc] = newarg;
        argc++;

        if(((u32)argc) >= argv_size)
        {
            argv_size *= 2;
            argv = krealloc(argv, sizeof(char*)*argv_size);
        }
    }

    //copy environment before process memory freeing
    int envc = 0;
    u32 env_size = 5;
    char** env = kmalloc(sizeof(char*)*env_size);
    while(true)
    {
        char* oldenv = ((char**) ecx)[envc];
        if(oldenv == 0) {env[envc] = 0; break;}

        size_t len = strlen(oldenv);
        char* newenv = kmalloc(len+1);
        strcpy(newenv, oldenv);
        env[envc] = newenv;
        envc++;

        if(((u32)envc) >= env_size)
        {
            env_size *= 2;
            env = krealloc(env, sizeof(char*)*env_size);
        }
    }

    //free old process memory
    free_process_memory(current_process);

    //kprintf("%lSYS_EXEC : loading executable...\n", 3);
    error_t load = load_executable(current_process, current_process->files[ebx], argc, argv, env, envc);
    if(load != ERROR_NONE) 
    {
        kprintf("%lLOAD = %u\n", 3, load); 
        exit_process(current_process, (3<<8)); 
        //fatal_kernel_error("LOAD", "SYSCALL_EXEC");
    }
    //kprintf("%lSYS_EXEC : executable loaded.\n", 3);

    //schedule to force reload eip/esp + registers that are in memory
    __asm__ __volatile__("jmp schedule_switch"::"a"(current_process->active_thread), "d"(current_process));

    //asm("mov %0, %%eax ; mov %0, %%ecx"::"g"(load));
}

void syscall_wait(u32 ebx, u32 ecx, u32 edx)
{
    //kprintf("%lSYS_WAIT(%d, 0x%X, 0x%X)\n", 3, ebx, ecx, edx);
    int pid = (int) ebx;
    
    int* wstatus = (int*) ecx;
    if(wstatus) if(!ptr_validate((uintptr_t) wstatus, current_process->page_directory)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR):"%eax", "%ecx"); return;}

    if((!current_process->children) || (!current_process->children->element)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_HAS_NO_CHILD)); return;}

    wait_start:
    if(pid < -1)
    {
        list_entry_t* ptr = current_process->children;
        list_entry_t* prev = 0;
        while(ptr)
        {
            process_t* element = ptr->element;
            if((element->status == PROCESS_STATUS_ZOMBIE) && (element->group->gid == -pid))
            {
                int trpid = element->pid;
                int retcode = (int) element->active_thread->gregs.eax;
                
                processes[element->pid] = 0;
                kfree(element->active_thread);
                kfree(element);

                //remove zombie process from parent's children list
                if(prev) prev->next = ptr->next;
                else current_process->children = 0;
                kfree(ptr);

                if(wstatus) *wstatus = retcode;
                asm("mov %0, %%eax ; mov %1, %%ecx"::"g"(trpid), "N"(ERROR_NONE));
                return;
            }
            prev = ptr;
            ptr = ptr->next;
        }
    }
    else if(pid == -1)
    {
        //waiting for any child processes
        list_entry_t* prev = 0;
        list_entry_t* ptr = current_process->children;
        while(ptr)
        {
            process_t* element = ptr->element;
            if((element->status == PROCESS_STATUS_ZOMBIE))
            {
                //get zombie process pid and return code
                int trpid = element->pid;
                int retcode = (int) element->active_thread->gregs.eax;
                
                //completely remove zombie process from process list
                processes[element->pid] = 0;
                kfree(element->active_thread);
                kfree(element);

                //remove zombie process from parent's children list
                if(prev) prev->next = ptr->next;
                else current_process->children = 0;
                kfree(ptr);

                if(wstatus) *wstatus = retcode;
                asm("mov %0, %%eax ; mov %1, %%ecx"::"g"(trpid), "N"(ERROR_NONE));
                return;
            }
            prev = ptr;
            ptr = ptr->next;
        }
    }
    else if(pid == 0)
    {
        list_entry_t* ptr = current_process->children;
        list_entry_t* prev = 0;
        while(ptr)
        {
            process_t* element = ptr->element;
            if((element->status == PROCESS_STATUS_ZOMBIE) && (element->group->gid == current_process->group->gid))
            {
                int trpid = element->pid;
                int retcode = (int) element->active_thread->gregs.eax;
                
                processes[element->pid] = 0;
                kfree(element->active_thread);
                kfree(element);

                //remove zombie process from parent's children list
                if(prev) prev->next = ptr->next;
                else current_process->children = 0;
                kfree(ptr);

                if(wstatus) *wstatus = retcode;
                asm("mov %0, %%eax ; mov %1, %%ecx"::"g"(trpid), "N"(ERROR_NONE));
                return;
            }
            prev = ptr;
            ptr = ptr->next;
        }
    }
    else if(pid > 0)
    {
        list_entry_t* ptr = current_process->children;
        list_entry_t* prev = 0;
        bool element_found = false;
        while(ptr)
        {
            process_t* element = ptr->element;
            if(element->pid == pid)
            {
                element_found = true;
                if(element->status == PROCESS_STATUS_ZOMBIE)
                {
                    int trpid = element->pid;
                    int retcode = (int) element->active_thread->gregs.eax;
                    
                    processes[element->pid] = 0;
                    kfree(element->active_thread);
                    kfree(element);

                    //remove zombie process from parent's children list
                    if(prev) prev->next = ptr->next;
                    else current_process->children = 0;
                    kfree(ptr);

                    if(wstatus) *wstatus = retcode;
                    asm("mov %0, %%eax ; mov %1, %%ecx"::"g"(trpid), "N"(ERROR_NONE));
                    return;
                }
            }
            prev = ptr;
            ptr = ptr->next;
        }
        if(!element_found) asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_PERMISSION));
    }

    current_process->active_thread->status = THREAD_STATUS_ASLEEP_CHILD;
    scheduler_remove_thread(current_process, current_process->active_thread);
    goto wait_start;
}

void syscall_getpinfo(u32 ebx, u32 ecx, u32 edx)
{
    if(!ptr_validate(edx, current_process->page_directory)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR):"%eax", "%ecx"); return;}
    
    int pid = (int) ebx;
    if((pid < 0) | (pid > (int) processes_size)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PID):"%eax", "%ecx"); return;}

    process_t* process = 0;
    if(!ebx) process = current_process;
    else process = processes[ebx];

    //check if we are trying to access current or child process. if not, no permission
    if((!process) | ((process != current_process) && (process->parent != current_process)))
    {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_PERMISSION):"%eax", "%ecx"); return;}

    switch(ecx)
    {
        case VK_PINFO_PID: {*((int*)edx) = process->pid; break;}
        case VK_PINFO_PPID: {if(process->parent) *((int*)edx) = process->parent->pid; else *((int*)edx) = -1; break;}
        case VK_PINFO_WORKING_DIRECTORY: {strcpy((char*) edx, process->current_dir); break;}
        case VK_PINFO_GID: {*((int*)edx) = process->group->gid; break;}
        default: {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(UNKNOWN_ERROR):"%eax", "%ecx"); return;}
    }

    asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_NONE):"%eax", "%ecx");
}

void syscall_setpinfo(u32 ebx, u32 ecx, u32 edx)
{
    int pid = (int) ebx;
    if((pid < 0) | (pid > (int) processes_size)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PID):"%eax", "%ecx"); return;}

    process_t* process = 0;
    if(!ebx) process = current_process;
    else process = processes[ebx];

    //check if we are trying to access current or child process. if not, no permission
    if((!process) | ((process != current_process) && (process->parent != current_process)))
    {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_PERMISSION):"%eax", "%ecx"); return;}

    switch(ecx)
    {
        case VK_PINFO_WORKING_DIRECTORY:
        {
            if(!ptr_validate(edx, current_process->page_directory)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR):"%eax", "%ecx"); return;}

            char* newdir = (char*) edx;
            u32 len = strlen(newdir); if(len >= 99) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_FILE_OUT):"%eax", "%ecx"); return;}
            fd_t* t = open_file(newdir, 0);
            if(!t) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_FILE_NOT_FOUND):"%eax", "%ecx"); return;}
            close_file(t);
            strncpy(process->current_dir, newdir, len);
            *(process->current_dir+len) = 0;
            break;
        }
        case VK_PINFO_GID:
        {
            int gid = (int) edx;
            //TODO : if child process, check for exec()
            error_t tr = process_setgroup(gid, process);
            asm("mov %0, %%eax ; mov %0, %%ecx"::"g"(tr):"%eax", "%ecx"); 
            return;
        }
        default: {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(UNKNOWN_ERROR):"%eax", "%ecx"); return;}
    }
    asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_NONE):"%eax", "%ecx");
}

void syscall_sig(u32 ebx, u32 ecx, u32 edx)
{
    int pid = (int) ebx;
    if((pid == 0) | (pid > (int) processes_size)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PID):"%eax", "%ecx"); return;}
    if(pid > 0) if(!processes[pid]) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PID):"%eax", "%ecx"); return;}

    int sig = (int) ecx;
    if((sig <= 0) | (sig >= NSIG)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_SIGNAL):"%eax", "%ecx"); return;}

    if(pid < 0) send_signal_to_group(-pid, sig);
    else send_signal(pid, sig);
    
    asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_NONE):"%eax", "%ecx");
}

void syscall_sigaction(u32 ebx, u32 ecx, u32 edx)
{
    int sig = (int) ebx;

    if((sig >= NSIG) | (sig <= 0)) {asm("mov %0, %%ecx"::"N"(ERROR_INVALID_SIGNAL):"%eax", "%ecx"); return;}
    if((sig == SIGKILL) | (sig == SIGSTOP)) {asm("mov %0, %%ecx"::"N"(ERROR_INVALID_SIGNAL):"%eax", "%ecx"); return;}
    
    u32 old_handler = (uintptr_t) current_process->signal_handlers[sig];
    current_process->signal_handlers[sig] = (void*) ecx;
    asm("mov %0, %%eax ; mov %1, %%ecx"::"g"(old_handler), "N"(ERROR_NONE):"%eax", "%ecx");
}

void syscall_sigret(u32 ebx, u32 ecx, u32 edx)
{
    
}

void syscall_sbrk(u32 ebx, u32 ecx, u32 edx)
{
    u32 tr = sbrk(current_process, ebx);
    asm("mov %0, %%eax ; mov %1, %%ecx"::"g"(tr), "N"(ERROR_NONE):"%eax", "%ecx");
}

void syscall_ioctl(u32 ebx, u32 ecx, u32 edx)
{
    //kprintf("%lSYS_IOCTL(%u, 0x%X, 0x%X)\n", 3, ebx, ecx, edx);
    if((current_process->files_size < ebx) || (!current_process->files[ebx])) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_FILE_NOT_FOUND):"%eax", "%ecx"); return;}
    fd_t* file = current_process->files[ebx];

    /* check if the file is a device (if file.fs == DEVFS) */
    if(file->file->file_system->fs_type != FS_TYPE_DEVFS) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_NO_DEVICE):"%eax", "%ecx"); return;}

    devfs_node_specific_t* spe = file->file->specific;
    switch(spe->device_type)
    {
        case DEVFS_TYPE_TTY:
        {
            error_t err = tty_ioctl(spe->device_struct, ecx, edx);
            asm("mov %0, %%eax ; mov %0, %%ecx"::"g"(err):"%eax", "%ecx");
            return;
        }
    }

    asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(UNKNOWN_ERROR):"%eax", "%ecx");
}

#pragma GCC diagnostic pop

__asm__(".global syscall_fork \n \
syscall_fork: \n \
pushl %esp /* push esp to be restored later on the forked process */ \n \
pushl current_process /* push current process as fork() argument */ \n \
call fork /* call fork() */ \n \
addl $0x8, %esp /* clean esp after fork() call */ \n \
movl 0x34(%eax), %eax /* return new process pid */ \n \
ret");

int fork_ret()
{
    return 0;
}

static bool ptr_validate(u32 ptr, u32* page_directory)
{
    if(ptr >= 0xC0000000) return false;
    if(!is_mapped(ptr, page_directory)) return false;
    return true;
}
