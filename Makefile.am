
bin_PROGRAMS = gstfs
gstfs_SOURCES = xcode.c gstfs.c

gstfs_CPPFLAGS = \
	$(fuse_CFLAGS) \
	$(glib_CFLAGS) \
	$(gstreamer_CFLAGS) 

AM_CFLAGS = -DFUSE_USE_VERSION=26

gstfs_LDFLAGS= \
	$(fuse_LIBS) \
	$(glib_LIBS) \
	$(gstreamer_LIBS) \
	-lpthread

# old targets 

