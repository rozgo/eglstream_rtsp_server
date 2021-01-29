//usr/bin/g++ $0 -o a.out -lEGL -lGL `pkg-config --cflags gstreamer-1.0` `pkg-config --libs gstreamer-1.0` && ./a.out; exit
// This is a self-running .cpp file.  chmod +x this file.  Then you can run it like a shell script.

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

using namespace std;

int main(int argc, char **argv)
{
    gst_init(&argc, &argv);
    GMainLoop *main_loop = g_main_loop_new(NULL, FALSE);

    int width = 960, height = 540;
    ostringstream launch_stream;
    launch_stream << "nveglstreamsrc name=egl_src ! ";
    launch_stream << "video/x-raw(memory:NVMM), format=RGBA, width=" << width << ", height=" << height << ", framerate=15/1 ! ";
    launch_stream << "nvvidconv ! video/x-raw(memory:NVMM),format=NV12 ! ";
    launch_stream << "fakesink ";

    string launch_string = launch_stream.str();
    g_print("Using launch string: %s\n", launch_string.c_str());

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

    for (int loopCnt = 0; loopCnt < 10; loopCnt++)
    {
        EGLDisplay eglDisplay;
        EGLSurface eglSurface;
        EGLConfig eglConfig;
        EGLContext eglContext;
        EGLStreamKHR eglStream;
        EGLint streamState = 0;

        GError *error;
        GstPipeline *gst_pipeline;

        printf("    **************************************\n");
        printf("    **************  LOOP %d  *************\n", loopCnt);
        printf("    **************************************\n");

        error = nullptr;
        gst_pipeline = (GstPipeline *)gst_parse_launch(launch_string.c_str(), &error);
        if (gst_pipeline == nullptr)
        {
            g_print("Failed to parse launch: %s\n", error->message);
            return -1;
        }
        if (error)
            g_error_free(error);

        {
            printf("EGLStreamInit() begin...\n");
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
            printf("EGLStreamInit() success!\n");
            EglQueryStreamKHR(eglDisplay, eglStream, EGL_STREAM_STATE_KHR, &streamState);
            printf("EGL Stream state: [%i]\n", streamState);
        }
        {
            printf("GST_STATE_PLAYING begin\n");
            GstElement *videoSource = gst_bin_get_by_name(GST_BIN(gst_pipeline), "egl_src");
            if (!GST_IS_ELEMENT(videoSource))
                return 1;
            g_object_set(G_OBJECT(videoSource), "display", eglDisplay, NULL);
            g_object_set(G_OBJECT(videoSource), "eglstream", eglStream, NULL);
            gst_element_set_state((GstElement *)gst_pipeline, GST_STATE_PLAYING);
            printf(" GST_STATE_PLAYING success!\n");
            EglQueryStreamKHR(eglDisplay, eglStream, EGL_STREAM_STATE_KHR, &streamState);
            printf("EGL Stream state: [%i]\n", streamState);
        }

        {
            printf("EGLStreamCreateProducer() begin...\n");
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
            EGLint srf_attr[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
            if ((eglSurface = EglCreateStreamProducerSurfaceKHR(eglDisplay, eglConfig, eglStream, srf_attr)) == EGL_NO_SURFACE)
                return 203;
            if (!eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext))
                return 204;
            printf("EGLStreamCreateProducer() success!\n");
            EglQueryStreamKHR(eglDisplay, eglStream, EGL_STREAM_STATE_KHR, &streamState);
            printf("EGL Stream state: [%i]\n", streamState);
        }
        {
            printf("Render loop begin\n");
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
                usleep(32000);
            }
            printf("Render loop success!\n");
        }
        {
            printf("GST_STATE_NULL begin\n");
            gst_element_set_state((GstElement *)gst_pipeline, GST_STATE_NULL);
            gst_object_unref(GST_OBJECT(gst_pipeline));
            //        g_main_loop_unref(main_loop);
            printf("GST_STATE_NULL success!\n");
        }
        {
            printf("EGLStreamFini begin\n");
            EGLint streamState = 0;
            if (!EglQueryStreamKHR(eglDisplay, eglStream, EGL_STREAM_STATE_KHR, &streamState))
                return 301;
            eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(eglDisplay, eglContext);
            eglDestroySurface(eglDisplay, eglSurface);
            EglDestroyStreamKHR(eglDisplay, eglStream);
            printf("EGLStreamFini success!\n");
        }
        printf("    **************  LOOP %d DONE  *************\n", loopCnt);

        sleep(1);
    }
    g_main_loop_unref(main_loop);
    return 0;
}
