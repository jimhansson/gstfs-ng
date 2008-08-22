/*
 * gstfs - gstreamer glue routines for transcoding
 */

#include <gst/gst.h>
#include <glib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>

struct pipe_params
{
    int fd;
    void (*add_data_cb)(char *, size_t, void *);
    void *user_data;
};


/*                
static gboolean bus_call (GstBus *bus, GstMessage *msg, gpointer data)
{
    gchar  *debug;
    GError *error;
    done_cb_t callback = (done_cb_t) data;

    switch (GST_MESSAGE_TYPE (msg)) {

        case GST_MESSAGE_EOS:
            g_main_loop_quit (loop);
            break;

        case GST_MESSAGE_ERROR:
            gst_message_parse_error (msg, &error, &debug);
            g_free (debug);

            g_printerr ("Error: %s\n", error->message);
            g_error_free (error);

            g_main_loop_quit (loop);
            break;

        default:
            break;
    }
    return TRUE;
}
*/

void close_pipe(void *data)
{
    int fd = (int) data;
    printf("close pipe! %d\n", fd);
    close(fd);
}

void *send_pipe(void *data)
{
    struct pipe_params *param = (struct pipe_params *) data;
    char buf[PIPE_BUF];
    size_t sizeread;
    
    while ((sizeread = read(param->fd, buf, sizeof(buf))) > 0)
    {
        param->add_data_cb(buf, sizeread, param->user_data);
    }
    return NULL;
}

/*
 *  Transcodes a file into a buffer, blocking until done.
 */
int transcode(char *filename, void (*add_data_cb)(char *, size_t, void *),
    void *user_data)
{
    GstElement *pipeline, *source, *dest;
    GError *error = NULL;
    GstBus *bus;
    char *pipeline_str = "filesrc name=\"_source\" ! oggdemux ! " 
        "vorbisdec ! audioconvert ! lame bitrate=160 ! " 
        "fdsink name=\"_dest\" sync=false";

    int pipefds[2];

    struct pipe_params thread_params;
    pthread_t thread;
    void *thread_status;

    pipeline = gst_parse_launch(pipeline_str, &error);
    if (error)
    {
        fprintf(stderr, "Error parsing pipeline: %s\n", error->message);
        return -1;
    }

    source = gst_bin_get_by_name(GST_BIN(pipeline), "_source");
    dest = gst_bin_get_by_name(GST_BIN(pipeline), "_dest");

    if (!pipeline || !source || !dest) 
    {
        fprintf(stderr, "Could not initialize pipeline\n");
        return -2;
    }

    if (pipe(pipefds))
    {
        perror("gstfs");
        return -1;
    }

    thread_params.fd = pipefds[0];
    thread_params.add_data_cb = add_data_cb;
    thread_params.user_data = user_data;

    pthread_create(&thread, NULL, send_pipe, (void *) &thread_params); 

    g_object_set(G_OBJECT(source), "location", filename, NULL);
    g_object_set(G_OBJECT(dest), "fd", pipefds[1], NULL);

    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    gst_bus_add_signal_watch(bus);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    GstMessage *message = gst_bus_poll(bus, GST_MESSAGE_EOS | 
	    GST_MESSAGE_ERROR, -1);
    gst_message_unref(message); 

    // close read-side so pipe will terminate
    close(pipefds[1]);
    pthread_join(thread, thread_status);

    return 0;
}
