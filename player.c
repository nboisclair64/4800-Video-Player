#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <gtk/gtk.h>
#include <cairo.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#define MAX_BUFFER 90

GtkWidget * darea;
pthread_t readThread;
pthread_t writeThread;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
pthread_cond_t condition2 = PTHREAD_COND_INITIALIZER;
typedef struct
{
    int width;
    int height;
    int maxval;
    unsigned char *pixels;
    int isEmpty;
} Frame;
static AVFormatContext *fmt_ctx = NULL;
static AVCodecContext *video_dec_ctx = NULL, *audio_dec_ctx;
static int width, height;
static enum AVPixelFormat pix_fmt;
static AVStream *video_stream = NULL, *audio_stream = NULL;
char *src_filename = NULL;
static const char *video_dst_filename = NULL;
static const char video_dst_filename_backup[100];
static const char *audio_dst_filename = NULL;
static FILE *video_dst_file = NULL;
static FILE *video_dst_file_grey_1 = NULL;
static FILE *video_dst_file_grey_2 = NULL;
static FILE *video_dst_file_grey_3 = NULL;
static FILE *video_dst_file_grey_4 = NULL;
static FILE *audio_dst_file = NULL;

static uint8_t *video_dst_data[4] = {NULL};
static int video_dst_linesize[4];
static int video_dst_bufsize;

static int video_stream_idx = -1, audio_stream_idx = -1;
static AVFrame *frame = NULL;
static AVPacket *pkt = NULL;
static int video_frame_count = 0;
static int audio_frame_count = 0;
static int frameRate;

static int state = 1;

GtkWidget *button;
GtkWidget *image;

Frame arrayOfFrames[MAX_BUFFER]; //Optimize array, potentially clear already shown frames
int lastFrameRead = 0;
int readIndex=1;
int readLap = 1;
int writeIndex = 0;
int writeLap=1;
GtkWidget *button4;
int videoStatus = 0;

//Function to recieve the width and height of the current video for the purpose of dynamically setting the window size for the video passed to the program
int get_video_dimensions(const char *filename, int *width, int *height) {
    AVFormatContext *format_ctx = NULL;
    AVCodecParameters *codec_params = NULL;
    const AVCodec *codec = NULL;

    // Open Video File 
    if (avformat_open_input(&format_ctx, filename, NULL, NULL) != 0) {
        fprintf(stderr, "Failed to open video file\n");
        return -1;
    }

    // Retrieve stream information
    if (avformat_find_stream_info(format_ctx, NULL) < 0) {
        fprintf(stderr, "Failed to retrieve stream information\n");
        avformat_close_input(&format_ctx);
        return -1;
    }

    // Find the first video stream
    int video_stream_index = av_find_best_stream(format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (video_stream_index < 0) {
        fprintf(stderr, "Failed to find video stream\n");
        avformat_close_input(&format_ctx);
        return -1;
    }

    // Get codec parameters for the video stream
    codec_params = format_ctx->streams[video_stream_index]->codecpar;

    // Retrieve width and height from codec parameters
    *width = codec_params->width;
    *height = codec_params->height;

    // Clean up
    avformat_close_input(&format_ctx);

    return 0;
}
//Function for capturing the current frame as a png using
//pixel buffer from the current frame
static void captureFrame(){
    //Retrieves the current frame
    Frame currentFrame = arrayOfFrames[writeIndex];
    //Creates pixel buffer from current frame
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_data(currentFrame.pixels,GDK_COLORSPACE_RGB,FALSE,8,currentFrame.width,currentFrame.height,3 * currentFrame.width,NULL,NULL);
    //Saves the pixel buffer from the current frame to captured_frame.png
    gdk_pixbuf_save(pixbuf, "captured_frame.png", "png", NULL, NULL);
}
//Function for playing the video
//It sets the playing flag to 0 which indiciates for the threads
//to continue their operations
static void playVideo()
{
   videoStatus = 0;
   //Sets Capture Frame button to inactive
   gtk_widget_set_sensitive(button4, FALSE);
}
//Function for pausing the video
//It sets the playing flag to 1 which indiciates for the threads
//to stop their operations
static void pauseVideo()
{
    videoStatus = 1;
    //Sets Capture Frame button to active
    gtk_widget_set_sensitive(button4, TRUE);
}
//Toggles between the play and pause functions, and their images
static void togglePlayPause(){
    if (state == 0){
        GtkWidget *image2 = gtk_image_new_from_file("pause.png");
        gtk_button_set_child(GTK_BUTTON(button), image2);
        gtk_image_set_pixel_size(GTK_IMAGE(image2), 32); // Set the pixel size of the image (e.g., 64 pixels)

        playVideo();
        state = 1;
    } else {

        GtkWidget *image3 = gtk_image_new_from_file("play.png");
        gtk_button_set_child(GTK_BUTTON(button), image3);
        gtk_image_set_pixel_size(GTK_IMAGE(image3), 32); // Set the pixel size of the image (e.g., 64 pixels)

        pauseVideo();
        state =0;
    }
}
//Iterates through a frame pixel by pixel saving each of its R,G,B
//Components into its pixels array to be stored in its struct
//This array is then added to its Frame struct and put into the array of frames
static int output_video_frame(AVFrame *frame)
{
    if (frame->width != width || frame->height != height ||
        frame->format != pix_fmt)
    {
        /* To handle this change, one could call av_image_alloc again and
         * decode the following frames into another rawvideo file. */
        fprintf(stderr, "Error: Width, height and pixel format have to be "
                        "constant in a rawvideo file, but the width, height or "
                        "pixel format of the input video changed:\n"
                        "old: width = %d, height = %d, format = %s\n"
                        "new: width = %d, height = %d, format = %s\n",
                width, height, av_get_pix_fmt_name(pix_fmt),
                frame->width, frame->height,
                av_get_pix_fmt_name(frame->format));
        return -1;
    }

    /* copy decoded frame to destination buffer:
     * this is required since rawvideo expects non aligned data */
    av_image_copy2(video_dst_data, video_dst_linesize,
                   frame->data, frame->linesize,
                   pix_fmt, width, height);
    unsigned char *pixels = (unsigned char *)malloc(width * height * 3 * sizeof(unsigned char));
    //Loops through each of the individual pixels
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            //Stream #0 tells us it has color format YUV420p which must be converted to RGB
            //For YUV color format U and V are often subsampled to save data
            //Common ratio for YUV is 4:2:0
            int y_index = y * video_dst_linesize[0] + x;
            //Subsampling ratio describes why we must do y/2 and x/2 since the data would be in half
            //the bytes as the Y data
            int u_index = (y / 2) * video_dst_linesize[1] + (x / 2);
            int v_index = (y / 2) * video_dst_linesize[2] + (x / 2);
            //Since video_dst contains Y,U,V data planes in index 0,1,2 respectively
            //We can apply the index we found above to the respective plane to find the Y,U,V actual pixel
            //values
            uint8_t Y = video_dst_data[0][y_index];
            uint8_t U = video_dst_data[1][u_index];
            uint8_t V = video_dst_data[2][v_index];

            // Apply YUV to RGB conversion formulas
            int R = Y + 1.402 * (V - 128);
            int G = Y - 0.344136 * (U - 128) - 0.714136 * (V - 128);
            int B = Y + 1.772 * (U - 128);
            //Create position to insert these 3 values
            int offset = (y * width + x) * 3;
            //Insert the values at the appropriate posistions
            pixels[offset] = R;
            pixels[offset+1] = G;
            pixels[offset + 2] = B;

        }
    }
    //Create Frame struct object based on width,height,max value for color, the pixel array and if its active
    Frame currentFrame = {frame->width,frame->height,255,pixels,1};
    //Put frame into the array
    //Puts it at readIndex-1 to allow circular buffer to ensure it is always ahead of write
    arrayOfFrames[readIndex-1] = currentFrame;
    return 0;
}
//Function for drawing the current frame the write thread has
//to the drawing area using a pixel buffer since it is more efficient
//than drawing each individual pixel as a rectangle
void draw_images(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data)
{
    //Retrieve current frame
    Frame currentFrame = arrayOfFrames[writeIndex];
    //Create the pixel buffer for current frame
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_data(currentFrame.pixels,GDK_COLORSPACE_RGB,FALSE,8,currentFrame.width,currentFrame.height,3*currentFrame.width,NULL,NULL);
    //set cairo source as the pixel buffer
    gdk_cairo_set_source_pixbuf(cr,pixbuf,0,0);
    //paint the pixel buffer on the drawing area
    cairo_paint(cr);
    //Release the pixel buffer from memory to save resources
    g_object_unref(pixbuf);
    //Set frame to empty to alert the code its index is "available"
    arrayOfFrames[writeIndex].isEmpty = 0;
    //Increment index for writing thread
    writeIndex++;
    //If writing thread has reached end of array then increment
    // Its lap and also reset write index to start of array
    if (writeIndex == MAX_BUFFER)
    {
        writeLap++;
        writeIndex = 0;
    }
}
static int decode_packet(AVCodecContext *dec, const AVPacket *pkt)
{

    int ret = 0;
    // submit the packet to the decoder
    ret = avcodec_send_packet(dec, pkt);
    if (ret < 0)
    {
        fprintf(stderr, "Error submitting a packet for decoding (%s)\n", av_err2str(ret));
        return ret;
    }

    // get the next available frames from the decoder
    if (ret >= 0)
    {
    
        ret = avcodec_receive_frame(dec, frame);
        if (ret < 0)
        {
            // those two return values are special and mean there is no output
            // frame available, but there were no errors during decoding
            if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
                return 0;

            fprintf(stderr, "Error during decoding (%s)\n", av_err2str(ret));
            return ret;
        }

        // write the frame data to output file only if its the one specified
            ret = output_video_frame(frame);
            //After reading a frame increment the reading threads
            //Position in the array
            readIndex++;
            //If read Index has reached end of array then fill in the last space since
            //Code is designed to put frame in readIndex-1
            //Output that frame and then reset Index to 1 and increment lap
            if (readIndex == MAX_BUFFER)
            {
                readIndex = 90;
                ret = output_video_frame(frame);
                readIndex = 1;
                readLap++;
                return 1;
            }
        av_frame_unref(frame);
        if (ret < 0)
            return ret;
    }
    return 0;
}
static int open_codec_context(int *stream_idx,
                              AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type)
{
    int ret, stream_index;
    AVStream *st;
    const AVCodec *dec = NULL;

    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0)
    {
        fprintf(stderr, "Could not find %s stream in input file '%s'\n",
                av_get_media_type_string(type), src_filename);
        return ret;
    }
    else
    {
        stream_index = ret;
        st = fmt_ctx->streams[stream_index];

        /* find decoder for the stream */
        dec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!dec)
        {
            fprintf(stderr, "Failed to find %s codec\n",
                    av_get_media_type_string(type));
            return AVERROR(EINVAL);
        }

        /* Allocate a codec context for the decoder */
        *dec_ctx = avcodec_alloc_context3(dec);
        if (!*dec_ctx)
        {
            fprintf(stderr, "Failed to allocate the %s codec context\n",
                    av_get_media_type_string(type));
            return AVERROR(ENOMEM);
        }

        /* Copy codec parameters from input stream to output codec context */
        if ((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0)
        {
            fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
                    av_get_media_type_string(type));
            return ret;
        }

        /* Init the decoders */
        if ((ret = avcodec_open2(*dec_ctx, dec, NULL)) < 0)
        {
            fprintf(stderr, "Failed to open %s codec\n",
                    av_get_media_type_string(type));
            return ret;
        }
        *stream_idx = stream_index;
    }

    return 0;
}
int initializeStreamVariables()
{
    int ret = 0;
    // Open input file and allocate format context
    if (avformat_open_input(&fmt_ctx, src_filename, NULL, NULL) < 0)
    {
        fprintf(stderr, "Could not open source file %s\n", src_filename);
        return -1;
    }

    // Retrieve stream information
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0)
    {
        fprintf(stderr, "Could not find stream information\n");
        return -1;
    }

    // Find the video stream
    video_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_stream_idx < 0)
    {
        fprintf(stderr, "Could not find video stream in the input\n");
        return -1;
    }
    video_stream = fmt_ctx->streams[video_stream_idx];

    // Open codec context
    if (open_codec_context(&video_stream_idx, &video_dec_ctx, fmt_ctx, AVMEDIA_TYPE_VIDEO) <0)
    {
        fprintf(stderr, "Could not open codec context\n");
        return -1;
    }
    else{
        video_stream = fmt_ctx->streams[video_stream_idx];

        //Retrieve the nessecary information regarding the video
        width = video_dec_ctx->width;
        height = video_dec_ctx->height;
        pix_fmt = video_dec_ctx->pix_fmt;
        ret = av_image_alloc(video_dst_data, video_dst_linesize,
                             width, height, pix_fmt, 1);
        if (ret < 0)
        {
            fprintf(stderr, "Could not allocate raw video buffer\n");
        }
        video_dst_bufsize = ret;
    }

    // Allocate frame
    frame = av_frame_alloc();
    if (!frame)
    {
        fprintf(stderr, "Could not allocate frame\n");
        return -1;
    }

    // Allocate packet
    pkt = av_packet_alloc();
    if (!pkt)
    {
        fprintf(stderr, "Could not allocate packet\n");
        return -1;
    }

    return 0;
}
int decodeFrame()
{
    int ret;

    // Read frame from input
    ret = av_read_frame(fmt_ctx, pkt);
    if (ret < 0)
    {
        // End of file or error
        return ret;
    }

    // Check if the packet belongs to the video stream
    if (pkt->stream_index == video_stream_idx)
    {
        // Decode the packet
        ret = decode_packet(video_dec_ctx, pkt);
        if (ret < 0)
        {
            fprintf(stderr, "Error decoding packet\n");
            return ret;
        }
    }

    // Free the packet
    av_packet_unref(pkt);

    return 0;
}
static bool isFrameBufferFull()
{
    //Loop through every frame and check if every one is "unavailable"
    for (int i = 0; i < MAX_BUFFER; i++)
    {
        if (arrayOfFrames[i].isEmpty == 0)
        {
            return FALSE;
        }
    }
    return TRUE;
}
static bool isFrameBufferEmpty()
{
    //Loop through every frame and check if every one is "available"
    for (int i = 0; i < MAX_BUFFER; i++)
    {
        if (arrayOfFrames[i].isEmpty == 1)
        {
            return FALSE;
        }
    }
    return TRUE;
}
//Thread for reading frames
static void *readFunction()
{
    while (1)
    {   
        //If video is not paused then perform
        //reading threads actions
        if(videoStatus==0){
            int status;
            pthread_mutex_lock(&mutex);
            //If buffer is full then read should wait for write to write some of the frames
            //to the screen
            while (isFrameBufferFull())
            {
                pthread_cond_wait(&condition2, &mutex); // Wait for condition to be empty
            }
            //If read is on the same lap as the write thread then only read a frame
            //if it is ahead of the write index
            if (readLap == writeLap && readIndex > writeIndex)
            {
                status = decodeFrame();
            }
            //if read is ahead of write in the circular buffer then it must make sure that its index is less than
            //write to avoid overwriting frames that havent been displayed
            else if (readLap > writeLap && readIndex < writeIndex)
            {
                status = decodeFrame(); 
            }

            pthread_cond_signal(&condition2); // Signal write function to start writing to screen

            pthread_mutex_unlock(&mutex); //Unlock

            //Indiciating video has completed
            if (status < 0)
                pthread_exit(NULL);
        }
        

    }


    return NULL;
}
static void *writeFunction()
{
    //Taking advantage of the nano sleep function
    //Allows you to indicate the seconds and nanoseconds to sleep
    struct timespec timeToSleep;
    //Sleep for 0 seconds
    timeToSleep.tv_sec = 0;
    //Sleep for 1000000000/Frame rate since 1000000000 is how many nanoseconds are in a second
    //And by dividing it by frame rate u will achieve the number that
    //this function must be run every second to achieve the desired framerate
    timeToSleep.tv_nsec = 1000000000/frameRate;
    while (1)
    {
        //If video is in playing state
        //run the code
        if(videoStatus == 0){
            //Sleep for the time alloted
            nanosleep(&timeToSleep, &timeToSleep);
            //Lock the mutex
            pthread_mutex_lock(&mutex);
            //If full wait
            if (isFrameBufferEmpty())
            {
                pthread_cond_wait(&condition2, &mutex);
            }
            //Write Frame
            gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(darea), draw_images, NULL, NULL);

            //Signal Read
            pthread_cond_signal(&condition2);
            //Unlock
            pthread_mutex_unlock(&mutex);
        }
        
    }
    return NULL;
}

static void
activate(GtkApplication *app,
         gpointer user_data)
{
    darea = gtk_drawing_area_new();


    int width, height;
    get_video_dimensions(src_filename, &width, &height);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *menuBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(menuBox, GTK_ALIGN_CENTER);
        //gtk_widget_set_size_request(menuBox, width, 32);


    button = gtk_button_new();
    image = gtk_image_new_from_file("pause.png");
    gtk_image_set_pixel_size(GTK_IMAGE(image), 32); 
    gtk_button_set_child(GTK_BUTTON(button), image);
    g_signal_connect_swapped(button, "clicked", G_CALLBACK(togglePlayPause), NULL);

    button4 = gtk_button_new_with_label("Capture Frame");
    g_signal_connect_swapped(button4, "clicked", G_CALLBACK(captureFrame), NULL);
    gtk_widget_set_sensitive(button4, FALSE);

    gtk_box_append(GTK_BOX(menuBox), button);
    gtk_box_append(GTK_BOX(menuBox), button4);

    //Window Settings
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Media Player");

    gtk_widget_set_size_request(darea, width, height);
    gtk_box_append(GTK_BOX(box), darea);
    gtk_box_append(GTK_BOX(box), menuBox);
    gtk_window_set_child(GTK_WINDOW(window), box);

    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(darea), draw_images, NULL, NULL);
    if (pthread_create(&writeThread, NULL, writeFunction, NULL) != 0)
    {
        printf("Error creating write thread\n");
    }
    if (pthread_create(&readThread, NULL, readFunction, NULL) != 0)
    {
        printf("Error creating write thread\n");
    }

        gtk_window_present(GTK_WINDOW(window));
}
int main(int argc, char *argv[])
{
    //Ensures the correct arguments are passed into command line
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s  input_file video_output_file(without file extension) frame_num\n"
                        "API example program to show how to read frames from an input file.\n"
                        "This program reads frames from a file, decodes them, and writes decoded\n"
                        "video frames to a rawvideo file named video_output_file, and decoded\n"
                        "audio frames to a rawaudio file named audio_output_file.\n",
                argv[0]);
        exit(1);
    }
    src_filename = argv[1];
    frameRate = atoi(argv[2]);
    //Resets argc to ensure GTK does not throw any errors
    argc = 1;
    // Initialize the nessecary stream variables 
    if (initializeStreamVariables() < 0)
    {
        fprintf(stderr, "Initialization failed\n");
        return 1;
    }
    // Create a new application
    GtkApplication *app = gtk_application_new("com.example.GtkApplication",
                                              G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    return status;
}