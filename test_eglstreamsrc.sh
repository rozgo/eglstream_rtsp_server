g++ test_eglstreamsrc.cpp -o test_eglstreamsrc -lEGL -lGL \
    -ldl \
    `pkg-config --cflags gstreamer-1.0` \
    `pkg-config --libs gstreamer-1.0` \
    `pkg-config --cflags --libs glib-2.0` && \
./test_eglstreamsrc

