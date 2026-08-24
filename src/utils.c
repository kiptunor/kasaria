/*

Kasaria -- A powerful and High efficiency MIDI Synth based on TiMidity
Copyright (C) 1995 Tuukka Toivonen <toivonen@clinet.fi>
Copyright (C) 2026 Kiptunor

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

common.c

*/













#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>

#ifndef _WIN32_WCE
    #include <string.h>
#endif



#include "ksr_internal.h"
// #include "ext_deps/log_c/log.h"

// I guess "rb" should be right for any libc
#define OPEN_MODE "rb"










int fp_equals(float a, float b, float tolerance)
{
    return ((a + tolerance > b) && (b > a - tolerance)) ? 1 : 0;
}

/* Try to open a file for reading. If the filename ends in one of the
defined compressor extensions, pipe the file through the decompressor */
static FILE *try_to_open(char *name, int decompress, int noise_mode)
{
    FILE *fp;

    fp = fopen(name, OPEN_MODE); // First just check that the file exists

    if(!fp)
        return 0;

#ifdef DECOMPRESSOR_LIST
    if(decompress)
    {
        int          l, el;
        static char *decompressor_list[] = DECOMPRESSOR_LIST, **dec;
        char         tmp[1024], tmp2[1024], *cp, *cp2;

        // Check if it's a compressed file
        l = strlen(name);

        for(dec = decompressor_list; *dec; dec += 2)
        {
            el = strlen(*dec);
            if((el >= l) || (strcmp(name + l - el, *dec)))
                continue;

            // Yes. Close the file, open a pipe instead.
            fclose(fp);

            // Quote some special characters in the file name
            cp  = name;
            cp2 = tmp2;
            while(*cp)
            {
                switch(*cp)
                {
                case '\'':
                case '\\':
                case ' ':
                case '`':
                case '!':
                case '"':
                case '&':
                case ';':
                    *cp2++ = '\\';
                }
                *cp2++ = *cp++;
            }
            *cp2 = 0;

            sprintf(tmp, *(dec + 1), tmp2);
            fp = popen(tmp, "r");
            break;
        }
    }
#endif

    return fp;
}

// This is meant to find and open files for reading, possibly piping them through a decompressor.
FILE *open_file(Kasaria *ksr, const char *name, int decompress, int noise_mode)
{
    FILE     *fp;
    PathList *plp = ksr->pathlist;
    int       l;

    if(!name || !(*name))
        return 0;


    // First try the given name

    strncpy(ksr->current_filename, name, 1023);
    ksr->current_filename[1023] = '\0';

    if((fp = try_to_open(ksr->current_filename, decompress, noise_mode)))
        return fp;
    

    if(name[0] != PATH_SEP)
    {
        while(plp) // Try along the path then
        {
            *ksr->current_filename = 0;
            l                      = strlen(plp->path);
            if(l)
            {
                strcpy(ksr->current_filename, plp->path);
                if(ksr->current_filename[l - 1] != PATH_SEP)
                    strcat(ksr->current_filename, PATH_STRING);
            }
            strcat(ksr->current_filename, name);
            if((fp = try_to_open(ksr->current_filename, decompress, noise_mode)))
                return fp;

            plp = (PathList *)plp->next;
        }
    }

    // Nothing could be opened.

    *ksr->current_filename = 0;

    return 0;
}

// This closes files opened with open_file
void close_file(FILE *fp)
{
#ifdef DECOMPRESSOR_LIST
    if(pclose(fp)) // Any better ideas?
#endif
        fclose(fp);
}

// This is meant for skipping a few bytes in a file or fifo.
void skip(FILE *fp, size_t len)
{
    size_t c;
    char   tmp[1024];
    while(len > 0)
    {
        c = len;
        if(c > 1024)
            c = 1024;
        len -= c;
        fread(tmp, 1, c, fp);
    }
}

// This'll allocate memory or die.
void *safe_malloc(size_t count)
{
    void *p;
    if((p = malloc(count)))
        return p;

    return NULL;
}

void *safe_large_malloc(size_t count)
{
    void *p;
    static int errflag = 0;

    if(errflag)
	    exit(10);
	
    if(count == 0)
      /* Some malloc routine return NULL if count is zero, such as
       * malloc routine from libmalloc.a of Solaris.
       * But TiMidity doesn't want to return NULL even if count is zero.
       */
        count = 1;
    
    if((p = (void*) malloc(count)) != NULL)
        return p;
    
    errflag = 1;

#ifdef ABORT_AT_FATAL
    abort();
#endif // ABORT_AT_FATAL
    exit(10);
    // NOTREACHED
	return 0;
}

// This adds a directory to the path list
void add_to_pathlist(Kasaria *ksr, char *s)
{
    PathList *plp = (PathList *)safe_malloc(sizeof(PathList));
    strcpy((plp->path = (char *)safe_malloc(strlen(s) + 1)), s);
    plp->next     = ksr->pathlist;
    ksr->pathlist = plp;
}

void free_pathlist(Kasaria *ksr)
{
    PathList *plp = ksr->pathlist;
    PathList *next;
    while(plp)
    {
        next = (PathList *)plp->next;
        free(plp->path);
        free(plp);
        plp = next;
    }
    ksr->pathlist = 0;
}

const char *url_unexpand_home_dir(const char *fname)
{
    static char path[BUFSIZ];
    const char *dir, *p;
    int dirlen;

    if(!IS_PATH_SEP(fname[0]))
	    return fname;

    if((dir = getenv("HOME")) == NULL)
	    if((dir = getenv("home")) == NULL)
	        return fname;
    
    dirlen = strlen(dir);
    
    if(dirlen == 0 || dirlen >= sizeof(path) - 2)
	    return fname;
    
    memcpy(path, dir, dirlen);
    
    if(!IS_PATH_SEP(path[dirlen - 1]))
	    path[dirlen++] = PATH_SEP;

#ifndef __W32__
    if(strncmp(path, fname, dirlen))
#else
    if(strncasecmp(path, fname, dirlen))
#endif /* __W32__ */
	    return fname;

    path[0] = '~';
    path[1] = '/';
    p = fname + dirlen;
    
    if(strlen(p) >= sizeof(path) - 3)
	    return fname;
    
    path[2] = '\0';
    strcat(path, p);
    
    return path;
}

static MBlockNode *free_mblock_list = NULL;
#define ADDRALIGN 8
// #define DEBUG

void init_mblock(MBlockList *mblock)
{
    mblock->first = NULL;
    mblock->allocated = 0;
}

static MBlockNode *new_mblock_node(size_t n)
{
    MBlockNode *p;

    if(n > MIN_MBLOCK_SIZE)
    {
	if((p = (MBlockNode *)safe_malloc(n + sizeof(MBlockNode))) == NULL)
	    return NULL;
	p->block_size = n;
    }
    else if(free_mblock_list == NULL)
    {
	if((p = (MBlockNode *)safe_malloc(sizeof(MBlockNode) + MIN_MBLOCK_SIZE)) == NULL)
	    return NULL;
	p->block_size = MIN_MBLOCK_SIZE;
    }
    else
    {
	p = free_mblock_list;
	free_mblock_list = free_mblock_list->next;
    }

    p->offset = 0;
    p->next = NULL;

    return p;
}

static int enough_block_memory(MBlockList *mblock, size_t n)
{
    size_t newoffset;

    if(mblock->first == NULL)
	return 0;

    newoffset = mblock->first->offset + n;

    if(newoffset < mblock->first->offset) // exceed representable in size_t
	return 0;

    if(newoffset > mblock->first->block_size)
	return 0;

    return 1;
}

void *new_segment(MBlockList *mblock, size_t nbytes)
{
    MBlockNode *p;
    void *addr;

    // round up to ADDRALIGN
    nbytes = ((nbytes + ADDRALIGN - 1) & ~(ADDRALIGN - 1));
    if(!enough_block_memory(mblock, nbytes))
    {
	p = new_mblock_node(nbytes);
	p->next = mblock->first;
	mblock->first = p;
	mblock->allocated += p->block_size;
    }
    else
	p = mblock->first;

    addr = (void *)(p->buffer + p->offset);
    p->offset += nbytes;

#ifdef DEBUG
    if(((unsigned long)addr) & (ADDRALIGN-1))
    {
	fprintf(stderr, "Bad address: 0x%x\n", addr);
	exit(1);
    }
#endif // DEBUG

    return addr;
}

static void reuse_mblock1(MBlockNode *p)
{
    if(p->block_size > MIN_MBLOCK_SIZE)
	safe_free(p);
    else // p->block_size <= MIN_MBLOCK_SIZE
    {
	p->next = free_mblock_list;
	free_mblock_list = p;
    }
}

void reuse_mblock(MBlockList *mblock)
{
    MBlockNode *p;

    if((p = mblock->first) == NULL)
	return;			// There is nothing to collect memory

    while(p)
    {
	MBlockNode *tmp;

	tmp = p;
	p = p->next;
	reuse_mblock1(tmp);
    }
    init_mblock(mblock);
}

char *strdup_mblock(MBlockList *mblock, const char *str)
{
    int len;
    char *p;

    len = strlen(str);
    p = (char *)new_segment(mblock, len + 1); // for '\0'
    memcpy(p, str, len + 1);
    return p;
}

int free_global_mblock(void)
{
    int cnt;

    cnt = 0;
    while(free_mblock_list)
    {
	MBlockNode *tmp;

	tmp = free_mblock_list;
	free_mblock_list = free_mblock_list->next;
	safe_free(tmp);
	cnt++;
    }
    return cnt;
}