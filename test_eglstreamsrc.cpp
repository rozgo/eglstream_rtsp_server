#include <cstdlib>
#include <gst/gst.h>
#include <gst/gstinfo.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <thread>
#include "readerwriterqueue.h"

using namespace std;
using namespace moodycamel;
typedef BlockingReaderWriterQueue<EGLint> Channel;

int egl_loop(int _loop_count, Channel &_gst_to_egl, Channel &_egl_to_gst, int _width, int _height, EGLDisplay *_egl_display, EGLStreamKHR *_egl_stream)
{
    // eglSetupExtensions() See bug 200161837 on EGL pointer functions should renamed. Not renaming egl pointer function causes 64 bit app to crash.
    PFNEGLCREATESTREAMKHRPROC EglCreateStreamKHR = (PFNEGLCREATESTREAMKHRPROC)eglGetProcAddress("eglCreateStreamKHR");
    PFNEGLDESTROYSTREAMKHRPROC EglDestroyStreamKHR = (PFNEGLDESTROYSTREAMKHRPROC)eglGetProcAddress("eglDestroyStreamKHR");
    PFNEGLQUERYSTREAMKHRPROC EglQueryStreamKHR = (PFNEGLQUERYSTREAMKHRPROC)eglGetProcAddress("eglQueryStreamKHR");
    PFNEGLQUERYSTREAMU64KHRPROC EglQueryStreamu64KHR = (PFNEGLQUERYSTREAMU64KHRPROC)eglGetProcAddress("eglQueryStreamu64KHR");
    PFNEGLQUERYSTREAMTIMEKHRPROC EglQueryStreamTimeKHR = (PFNEGLQUERYSTREAMTIMEKHRPROC)eglGetProcAddress("eglQueryStreamTimeKHR");
    PFNEGLSTREAMATTRIBKHRPROC EglStreamAttribKHR = (PFNEGLSTREAMATTRIBKHRPROC)eglGetProcAddress("eglStreamAttribKHR");
    PFNEGLSTREAMCONSUMERACQUIREKHRPROC EglStreamConsumerAcquireKHR = (PFNEGLSTREAMCONSUMERACQUIREKHRPROC)eglGetProcAddress("eglStreamConsumerAcquireKHR");
    PFNEGLSTREAMCONSUMERRELEASEKHRPROC EglStreamConsumerReleaseKHR = (PFNEGLSTREAMCONSUMERRELEASEKHRPROC)eglGetProcAddress("eglStreamConsumerReleaseKHR");
    PFNEGLSTREAMCONSUMERGLTEXTUREEXTERNALKHRPROC EglStreamConsumerGLTextureExternalKHR = (PFNEGLSTREAMCONSUMERGLTEXTUREEXTERNALKHRPROC)eglGetProcAddress("eglStreamConsumerGLTextureExternalKHR");
    PFNEGLGETSTREAMFILEDESCRIPTORKHRPROC EglGetStreamFileDescriptorKHR = (PFNEGLGETSTREAMFILEDESCRIPTORKHRPROC)eglGetProcAddress("eglGetStreamFileDescriptorKHR");
    PFNEGLCREATESTREAMFROMFILEDESCRIPTORKHRPROC EglCreateStreamFromFileDescriptorKHR = (PFNEGLCREATESTREAMFROMFILEDESCRIPTORKHRPROC)eglGetProcAddress("eglCreateStreamFromFileDescriptorKHR");
    PFNEGLCREATESTREAMPRODUCERSURFACEKHRPROC EglCreateStreamProducerSurfaceKHR = (PFNEGLCREATESTREAMPRODUCERSURFACEKHRPROC)eglGetProcAddress("eglCreateStreamProducerSurfaceKHR");

    EGLDisplay eglDisplay;
    EGLSurface eglSurface;
    EGLConfig eglConfig;
    EGLContext eglContext;
    EGLStreamKHR eglStream;
    EGLint streamState = 0;

    // Create an egl display and stream
    {
        printf("EGL_LOOP %d creating stream...\n", _loop_count);
        if ((eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY)) == EGL_NO_DISPLAY)
            return 101;
        if (!eglInitialize(eglDisplay, 0, 0))
            return 102;
        EGLint streamAttrMailboxMode[] = {EGL_NONE};
        if ((eglStream = EglCreateStreamKHR(eglDisplay, streamAttrMailboxMode)) == EGL_NO_STREAM_KHR)
            return 103;
        if (!EglStreamAttribKHR(eglDisplay, eglStream, EGL_CONSUMER_LATENCY_USEC_KHR, 16000))
            return 104;
        if (!EglStreamAttribKHR(eglDisplay, eglStream, EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR, 16000))
            return 105;
        printf("EGL_LOOP %d stream created.\n", _loop_count);
    }

    *_egl_display = eglDisplay;
    *_egl_stream = eglStream;

    // Done creating egl_display and stream, notify gst!
    EglQueryStreamKHR(eglDisplay, eglStream, EGL_STREAM_STATE_KHR, &streamState);
    printf("EGL_LOOP %d egl stream state: (%p)[%i]\n", _loop_count, &eglStream, streamState);
    _egl_to_gst.enqueue(streamState);

    // Wait for gst to connect to a consumer
    printf("EGL_LOOP %d waiting for GST_LOOP to connect to consumer...\n", _loop_count);
    _gst_to_egl.wait_dequeue(streamState);
    if (streamState != EGL_STREAM_STATE_CONNECTING_KHR)
    {
        printf("EGL_LOOP %d STREAM ERROR: %s:%i [%i]\n", _loop_count, __FILE__, __LINE__, streamState);
        return -1;
    }
    printf("EGL_LOOP %d stream connected to consumer.\n", _loop_count);

    // Connect egl stream to producer, only after connected to consumer
    {
        printf("EGL_LOOP %d creating producer...\n", _loop_count);
        EGLint cfg_attr[] = {
            EGL_SURFACE_TYPE, EGL_STREAM_BIT_KHR,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_ALPHA_SIZE, 1,
            EGL_NONE};
        EGLint n = 0;
        if (!eglChooseConfig(eglDisplay, cfg_attr, &eglConfig, 1, &n) || !n)
            return 201;
        EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
        eglBindAPI(EGL_OPENGL_ES_API);
        if ((eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, ctx_attr)) == EGL_NO_CONTEXT)
            return 202;
        EGLint srf_attr[] = {EGL_WIDTH, _width, EGL_HEIGHT, _height, EGL_NONE};
        if ((eglSurface = EglCreateStreamProducerSurfaceKHR(eglDisplay, eglConfig, eglStream, srf_attr)) == EGL_NO_SURFACE)
            return 203;
        if (!eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext))
            return 204;
        printf("EGL_LOOP %d producer created.\n", _loop_count);
    }

    printf("EGL_LOOP %d render loop...\n", _loop_count);
    while (streamState != EGL_STREAM_STATE_DISCONNECTED_KHR)
    {
        float r = 0, g = 0.5, b = 1;
        glClearColor(r, g, b, 1); // blue-ish background
        glClear(GL_COLOR_BUFFER_BIT);
        eglSwapBuffers(eglDisplay, eglSurface);
        for (int frame = 0; frame < 20; frame++)
        {
            if (frame % 15 == 0)
            {
                r = (r > 0) ? 0 : 0.5;
                g = (g > 0) ? 0 : 0.5;
                b = (b > 0.5) ? 0.5 : 1;
            }
            glClearColor(r, g, b, 1); // blue-ish background
            glClear(GL_COLOR_BUFFER_BIT);
            eglSwapBuffers(eglDisplay, eglSurface);
        }

        usleep(60000);

        EglQueryStreamKHR(eglDisplay, eglStream, EGL_STREAM_STATE_KHR, &streamState);
    }
    printf("EGL_LOOP %d render loop ended.\n", _loop_count);

    {
        printf("EGL_LOOP %d exiting...\n", _loop_count);
        EGLint streamState = 0;
        if (!EglQueryStreamKHR(eglDisplay, eglStream, EGL_STREAM_STATE_KHR, &streamState))
            return 301;
        eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(eglDisplay, eglContext);
        eglDestroySurface(eglDisplay, eglSurface);
        EglDestroyStreamKHR(eglDisplay, eglStream);
        printf("EGL_LOOP %d exited. DONE.\n", _loop_count);
    }

    return 0;
}

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data)
{
    GMainLoop *loop = (GMainLoop *)data;

    switch (GST_MESSAGE_TYPE(msg))
    {

    case GST_MESSAGE_EOS:
        g_print("GST_LOOP EOS\n");
        g_main_loop_quit(loop);
        break;

    case GST_MESSAGE_ERROR:
    {
        gchar *debug;
        GError *error;

        gst_message_parse_error(msg, &error, &debug);
        g_free(debug);

        g_printerr("Error: %s\n", error->message);
        g_error_free(error);

        g_main_loop_quit(loop);
        break;
    }
    default:
        break;
    }

    return TRUE;
}

int main(int argc, char **argv)
{
    PFNEGLQUERYSTREAMKHRPROC EglQueryStreamKHR = (PFNEGLQUERYSTREAMKHRPROC)eglGetProcAddress("eglQueryStreamKHR");

    gst_init(&argc, &argv);

    int width = 960, height = 540;
    ostringstream launch_stream;
    launch_stream << "nveglstreamsrc name=egl_src num-buffers=500 ! ";
    launch_stream << "video/x-raw(memory:NVMM), format=RGBA, width=" << width << ", height=" << height << ", framerate=15/1 ! ";
    launch_stream << "nvvidconv ! video/x-raw(memory:NVMM),format=NV12 ! ";
    launch_stream << "fakesink ";

    string launch_string = launch_stream.str();
    g_print("GST_LOOP using launch string: %s\n", launch_string.c_str());

    for (int loop_count = 0; loop_count < 5; loop_count++)
    {
        GMainLoop *main_loop = g_main_loop_new(NULL, FALSE);

        EGLint stream_state = 0;

        Channel gst_to_egl;
        Channel egl_to_gst;

        EGLDisplay egl_display;
        EGLStreamKHR egl_stream;

        GError *error;
        GstPipeline *pipeline;

        g_print("**************************************\n");

        error = nullptr;
        pipeline = (GstPipeline *)gst_parse_launch(launch_string.c_str(), &error);
        if (pipeline == nullptr)
        {
            g_print("Failed to parse launch: %s\n", error->message);
            return -1;
        }
        if (error)
            g_error_free(error);

        // Add message handler to bus
        GstBus *bus;
        guint bus_watch_id;
        bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
        bus_watch_id = gst_bus_add_watch(bus, bus_call, main_loop);
        gst_object_unref(bus);

        std::thread egl_thread([&] {
            int err_code = egl_loop(loop_count, gst_to_egl, egl_to_gst, width, height, &egl_display, &egl_stream);
            g_print("EGL_LOOP %d ERROR: %i.\n", loop_count, err_code);
        });

        g_print("GST_LOOP %d waiting for EGL_LOOP to create stream...\n", loop_count);
        egl_to_gst.wait_dequeue(stream_state);
        EglQueryStreamKHR(egl_display, egl_stream, EGL_STREAM_STATE_KHR, &stream_state);
        g_print("GST_LOOP %d egl stream state: (%p)[%i]\n", loop_count, egl_stream, stream_state);
        if (stream_state != EGL_STREAM_STATE_CREATED_KHR)
        {
            g_print("EGL_STREAM ERROR: %s:%i [%i]\n", __FILE__, __LINE__, stream_state);
            return -1;
        }

        {
            g_print("GST_LOOP %d GST_STATE_PLAYING starting...\n", loop_count);
            GstElement *videoSource = gst_bin_get_by_name(GST_BIN(pipeline), "egl_src");
            if (!GST_IS_ELEMENT(videoSource))
                return 1;
            g_object_set(G_OBJECT(videoSource), "display", egl_display, NULL);
            g_object_set(G_OBJECT(videoSource), "eglstream", egl_stream, NULL);
            gst_element_set_state((GstElement *)pipeline, GST_STATE_PLAYING);
            g_print("GST_LOOP %d GST_STATE_PLAYING succeded.\n", loop_count);
            EglQueryStreamKHR(egl_display, egl_stream, EGL_STREAM_STATE_KHR, &stream_state);
            g_print("GST_LOOP %d egl stream state: (%p)[%i]\n", loop_count, egl_stream, stream_state);
            gst_to_egl.enqueue(stream_state);
        }

        g_print("GST_LOOP %d looping...\n", loop_count);

        g_main_loop_run(main_loop);

        g_print("GST_LOOP %d stopping...\n", loop_count);

        g_print("GST_LOOP %d GST_STATE_NULL setting...\n", loop_count);
        gst_element_set_state((GstElement *)pipeline, GST_STATE_NULL);

        egl_thread.join();

        gst_object_unref(GST_OBJECT(pipeline));
        g_source_remove(bus_watch_id);

        g_main_loop_unref(main_loop);

        g_print("GST_LOOP %d DONE.\n", loop_count);
    }

    return 0;
}
