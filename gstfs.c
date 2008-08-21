/*
 *  gstfs - a gstreamer filesystem
 */
#include <sys/types.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fuse.h>
#include <errno.h>
#include <glib.h>
#include <printf.h>
#include <gst/gst.h>
#include "xcode.h"

#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

static GHashTable *file_cache;
static char *get_source_path(const char *filename);

char *source_mount = "ogg";
char *src_ext = "ogg", *dest_ext = "mp3";

// This stuff is stored into a hash table blah blah
struct file_info
{
    char *filename;           /* hash key */
    char *source_filename;    /* filename in other mount */
    size_t len;               /* size of file */
    size_t alloc_len;         /* allocated size of buf */
    char *buf;                /* completely converted file */
};

struct file_info *new_file_info(const char *filename)
{
    struct file_info *fi;

    fi = calloc(1, sizeof(struct file_info));
    fi->filename = strdup(filename);
    fi->source_filename = get_source_path(filename);
    return fi;
}

char *replace_ext(char *filename, char *search, char *replace)
{
    char *ext = strrchr(filename, '.');
    if (ext && strcmp(ext+1, search) == 0) 
    {
        *(ext+1) = 0;
        filename = g_strconcat(filename, replace, NULL);
    }
    return filename;
}

int is_target_type(const char *filename)
{
    char *ext = strrchr(filename, '.');
    return (ext && strcmp(ext+1, dest_ext) == 0);
}

/*
 *  If the path represents a file in the mirror filesystem, then
 *  look for it in the cache.  If not, create a new file info.
 *
 *  If it isn't a mirror file, return NULL.
 */
static struct file_info *gstfs_lookup(const char *path)
{
    struct file_info *ret;

    if (!is_target_type(path))
        return NULL;

    ret = g_hash_table_lookup(file_cache, path);
    if (!ret)
    {
	ret = new_file_info(path);
	if (!ret)
	    goto out;

        g_hash_table_replace(file_cache, ret->filename, ret);
    }
out:
    return ret;
}

static char *get_source_path(const char *filename)
{
    char *source;

    printf("filename: %s\n", filename);

    source = g_strdup_printf("%s%s", source_mount, filename);
    source = replace_ext(source, dest_ext, src_ext);
    printf("-> source filename: %s\n", source);
    return source;
}

int gstfs_statfs(const char *path, struct statvfs *buf)
{
    char *source_path;

    source_path = get_source_path(path);
    if (statvfs(path, buf))
        return -errno;

    g_free(source_path);
    return 0;
}

int gstfs_getattr(const char *path, struct stat *stbuf)
{
    int ret = 0;
    char *source_path;
    struct file_info *converted;

    source_path = get_source_path(path);

    printf("in gettattr, foo\n");
    if (stat(source_path, stbuf))
       ret = -errno;
    else if ((converted = gstfs_lookup(path)))
    {
       stbuf->st_size = converted->len;
    }

    g_free(source_path);
    return ret;
}

static int read_cb(char *buf, size_t size, void *data)
{
    struct file_info *info = (struct file_info *) data;

    size_t newsz = info->len + size;
   
    if (info->alloc_len < newsz)
    {
        info->alloc_len = max(info->alloc_len * 2, newsz);
        info->buf = realloc(info->buf, info->alloc_len);
        if (!info->buf)
            return -ENOMEM;
    }

    memcpy(&info->buf[info->len], buf, size);
    info->len += size;
    return 0;
}

int gstfs_read(const char *path, char *buf, size_t size, off_t offset, 
    struct fuse_file_info *fi)
{
    struct file_info *info = gstfs_lookup(path);
    size_t count;

    if (!info)
        return -ENOENT;

    if (!info->buf)
        transcode(info->source_filename, read_cb, info);
    
    if (info->len <= offset)
        return 0;

    count = min(info->len - offset, size);

    memcpy(buf, &info->buf[offset], count);
    return count;
}

int gstfs_open(const char *path, struct fuse_file_info *fi)
{
    struct file_info *info = gstfs_lookup(path);
    if (!info)
        return -ENOENT;

    return 0;
}

/**
 *  readdir - copy all entries from source mount
 */
int gstfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
    off_t offset, struct fuse_file_info *fi)
{
    struct dirent *dirent;
    DIR *dir;
    char *source_path;

    source_path = get_source_path(path);
    dir = opendir(source_path);
 
    if (!dir)
        return -ENOENT;

    while ((dirent = readdir(dir)))
    {
        char *s = g_strdup(dirent->d_name);
        s = replace_ext(s, src_ext, dest_ext);
        filler(buf, s, NULL, 0);

        g_free(s);
    }
    closedir(dir);

    return 0;
}

static struct fuse_operations gstfs_ops = {
    .readdir = gstfs_readdir,
    .statfs = gstfs_statfs,
    .getattr = gstfs_getattr,
    .open = gstfs_open,
    .read = gstfs_read
};

int main(int argc, char *argv[])
{
    gst_init (&argc, &argv);
    file_cache = g_hash_table_new(g_str_hash, g_str_equal);

    return fuse_main(argc, argv, &gstfs_ops, NULL);
}
