/*
 *  gstfs - a gstreamer filesystem
 */
#include <sys/types.h>
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fuse.h>
#include <errno.h>
#include <glib.h>
#include <gst/gst.h>
#include "xcode.h"

#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

/* per-mount options and data structures */
struct gstfs_mount_info
{
    pthread_mutex_t cache_mutex; /* protects file_cache, cache_lru accesses */
    GHashTable *file_cache;      /* cache of transcoded audio */
    GQueue *cache_lru;           /* queue of items in LRU order */
    int max_cache_entries;       /* max # of entries in the cache */
    char *source_mount;          /* directory we are mirroring */
    char *src_ext;               /* extension of files we transcode */
    char *dest_ext;              /* extension of target files */
};

/* This stuff is stored into file_cache by filename */
struct gstfs_file_info
{
    char *filename;           /* hash key */
    char *source_filename;    /* filename in other mount */
    pthread_mutex_t mutex;    /* protects this file info */
    size_t len;               /* size of file */
    size_t alloc_len;         /* allocated size of buf */
    char *buf;                /* completely converted file */
    GList *list_node;         /* pointer for cache_lru */
};


static struct gstfs_mount_info mount_info;

static char *get_source_path(const char *filename);


struct gstfs_file_info *get_file_info(const char *filename)
{
    struct gstfs_file_info *fi;

    fi = calloc(1, sizeof(struct gstfs_file_info));
    fi->filename = g_strdup(filename);
    fi->source_filename = get_source_path(filename);
    pthread_mutex_init(&fi->mutex, NULL);
    return fi;
}

void put_file_info(struct gstfs_file_info *fi)
{
    g_free(fi->filename);
    g_free(fi->source_filename);
    free(fi);
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
    return (ext && strcmp(ext+1, mount_info.dest_ext) == 0);
}

/*  
 *  Remove items from the file cache until below the maximum.
 *  This is relatively quick since we can find elements by looking at the
 *  head of the lru list and then do a single hash lookup to remove from 
 *  the hash table.
 *
 *  Called with cache_mutex held.
 */
static void expire_cache()
{
    struct gstfs_file_info *fi;

    while (g_queue_get_length(mount_info.cache_lru) > 
           mount_info.max_cache_entries)
    {
        fi = (struct gstfs_file_info *) g_queue_pop_head(mount_info.cache_lru);
        g_hash_table_remove(mount_info.file_cache, fi);
        put_file_info(fi);
    }
}

/*
 *  If the path represents a file in the mirror filesystem, then
 *  look for it in the cache.  If not, create a new file info.
 *
 *  If it isn't a mirror file, return NULL.
 */
static struct gstfs_file_info *gstfs_lookup(const char *path)
{
    struct gstfs_file_info *ret;

    if (!is_target_type(path))
        return NULL;

    pthread_mutex_lock(&mount_info.cache_mutex);
    ret = g_hash_table_lookup(mount_info.file_cache, path);
    if (!ret)
    {
        ret = get_file_info(path);
        if (!ret)
            goto out;

        g_hash_table_replace(mount_info.file_cache, ret->filename, ret);
    }

    // move to end of LRU
    if (ret->list_node)
        g_queue_unlink(mount_info.cache_lru, ret->list_node);

    g_queue_push_tail(mount_info.cache_lru, ret);
    ret->list_node = mount_info.cache_lru->tail;

    expire_cache();

out:
    pthread_mutex_unlock(&mount_info.cache_mutex);
    return ret;
}

static char *get_source_path(const char *filename)
{
    char *source;

    source = g_strdup_printf("%s%s", mount_info.source_mount, filename);
    source = replace_ext(source, mount_info.dest_ext, mount_info.src_ext);
    return source;
}

int gstfs_statfs(const char *path, struct statvfs *buf)
{
    char *source_path;

    source_path = get_source_path(path);
    if (statvfs(source_path, buf))
        return -errno;

    g_free(source_path);
    return 0;
}

int gstfs_getattr(const char *path, struct stat *stbuf)
{
    int ret = 0;
    char *source_path;
    struct gstfs_file_info *converted;

    source_path = get_source_path(path);

    if (stat(source_path, stbuf))
        ret = -errno;
    else if ((converted = gstfs_lookup(path)))
        stbuf->st_size = converted->len;

    g_free(source_path);
    return ret;
}

static int read_cb(char *buf, size_t size, void *data)
{
    struct gstfs_file_info *info = (struct gstfs_file_info *) data;

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
    struct gstfs_file_info *info = gstfs_lookup(path);
    size_t count = 0;

    if (!info)
        return -ENOENT;

    pthread_mutex_lock(&info->mutex);

    if (!info->buf)
        transcode(info->source_filename, read_cb, info);
    
    if (info->len <= offset)
        goto out;

    count = min(info->len - offset, size);

    memcpy(buf, &info->buf[offset], count);

out:
    pthread_mutex_unlock(&info->mutex);
    return count;
}

int gstfs_open(const char *path, struct fuse_file_info *fi)
{
    struct gstfs_file_info *info = gstfs_lookup(path);
    if (!info)
        return -ENOENT;

    return 0;
}

/*
 *  copy all entries from source mount
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
        s = replace_ext(s, mount_info.src_ext, mount_info.dest_ext);
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
    pthread_mutex_init(&mount_info.cache_mutex, NULL);
    mount_info.file_cache = g_hash_table_new(g_str_hash, g_str_equal);
    mount_info.cache_lru = g_queue_new();
    mount_info.max_cache_entries = 50;
    mount_info.source_mount = "ogg";
    mount_info.src_ext = "ogg";
    mount_info.dest_ext = "mp3";

    gst_init(&argc, &argv);
    return fuse_main(argc, argv, &gstfs_ops, NULL);
}
