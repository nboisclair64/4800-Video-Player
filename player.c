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

Frame arrayOfFrames[MAX_BUFFER]; //Optimize array, potentially clear already shown frames
int lastFrameRead = 0;
int readIndex=1;
int readLap = 1;
int writeIndex = 0;
int writeLap=1;
static void playVideo()
{
   
}
static void pauseVideo()
{
    
}
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
            //Common ratio for YUV is 4:2:2
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

            int offset = (y * width + x) * 3;
            pixels[offset] = R;
            pixels[offset+1] = G;
            pixels[offset + 2] = B;

        }
    }
    Frame currentFrame = {frame->width,frame->height,255,pixels,1};
    printf("Read Index: %d\n",readIndex-1);
    arrayOfFrames[readIndex-1] = currentFrame;
    return 0;
}
void draw_images(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data)
{
    Frame currentFrame = arrayOfFrames[writeIndex];
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_data(
        currentFrame.pixels,
        GDK_COLORSPACE_RGB,
        FALSE,
        8,
        currentFrame.width,
        currentFrame.height,
        3*currentFrame.width,
        NULL,
        NULL
    );
    gdk_cairo_set_source_pixbuf(cr,pixbuf,0,0);
    cairo_paint(cr);
    g_object_unref(pixbuf);
    //Iterates through every pixel in the images array
    // for (int y = 0; y < currentFrame.height; y++)
    // {
    //     for (int x = 0; x < currentFrame.width; x++)
    //     {
    //         //Offset for finding the colored pixels in the array based on row and column
    //         int offset = (y * currentFrame.width + x) * 3;
    //         unsigned char red = currentFrame.pixels[offset];       //Offset will give u red
    //         unsigned char green = currentFrame.pixels[offset + 1]; //+1 for green
    //         unsigned char blue = currentFrame.pixels[offset + 2];  //+2 from offset gives you blue


    //         //Set the pixel color for ppm tile
    //         cairo_set_source_rgb(cr, red / 255.0, green / 255.0, blue / 255.0);
    //         // Draw a rectangle representing the pixel for ppm image
    //         cairo_rectangle(cr, x, y, 1, 1);
    //         cairo_fill(cr);
    //     }
    // }
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
            readIndex++;
            if (readIndex == MAX_BUFFER)
            {
                //lastFrameRead = dec->frame_num;
                printf("Done Reading\n");
                readIndex = 1;
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
int initialize()
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

        /* allocate image where the decoded image will be put */
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
        printf("Decoding Pkt\n");
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
    for (int i = 0; i < MAX_BUFFER; i++)
    {
        if (arrayOfFrames[i].isEmpty == 1)
        {
            return FALSE;
        }
    }
    return TRUE;
}
static void *readFunction()
{
    printf("Read Started\n");
    while (1)
    {   
        
        pthread_mutex_lock(&mutex);
        printf("Locked In Read\n");
        while(isFrameBufferFull())
        {
            printf("Buffer full\n");
            pthread_cond_wait(&condition2, &mutex); // Wait for condition to be empty
        }
        if(readLap==writeLap && readIndex>writeIndex){
            printf("Decoding Evem\n");
            int status = decodeFrame(); //Need to get this to read one frame
            printf("Done Decoding\n");
        }
        else if(readLap>writeLap && readIndex<writeIndex){
            printf("Decoding Odd\n");
            int status = decodeFrame(); //Need to get this to read one frame
            printf("Done Decoding\n");
        }
        
        pthread_cond_signal(&condition2); // Signal write function
        //printf("Unlocked in Read\n");
        pthread_mutex_unlock(&mutex); //Unlock
        
    }
    return NULL;
}
static void *writeFunction()
{
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 1000000000/frameRate;
    printf("Write Started\n");
    while (1)
    {
        printf("Writing Loop\n");
        //sleep(1);
        nanosleep(&ts, &ts);
        pthread_mutex_lock(&mutex);
        //If full wait
        if (isFrameBufferEmpty())
        {
            printf("Buffer Empty\n");
            pthread_cond_wait(&condition2, &mutex);
        }
        //pthread_cond_wait(&condition, &mutex); // Wait for signal from read function
        //Write Frame
        printf("Drawing frame: %d\n",writeIndex);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(darea), draw_images, NULL, NULL);
        arrayOfFrames[writeIndex].isEmpty = 0;
        writeIndex++;
        if(writeIndex==MAX_BUFFER){
            writeLap++;
            writeIndex=0;
        }
        //Signal Read
        pthread_cond_signal(&condition2); //Signal Read
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}

static void
activate(GtkApplication *app,
         gpointer user_data)
{
    darea = gtk_drawing_area_new();

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *menuBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    GtkWidget *button2 = gtk_button_new_with_label("Pause");
    g_signal_connect_swapped(button2, "clicked", G_CALLBACK(pauseVideo), NULL);
    GtkWidget *button3 = gtk_button_new_with_label("Play");
    g_signal_connect_swapped(button3, "clicked", G_CALLBACK(playVideo), NULL);

    gtk_box_append(GTK_BOX(menuBox), button2);
    gtk_box_append(GTK_BOX(menuBox), button3);

    //Window Settings
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Picture Editor");

    gtk_widget_set_size_request(darea, 400, 400);
    gtk_box_append(GTK_BOX(box), darea);
    gtk_box_append(GTK_BOX(box), menuBox);
    gtk_window_set_child(GTK_WINDOW(window), box);

    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(darea), draw_images, NULL, NULL);
    if (pthread_create(&writeThread, NULL, writeFunction, NULL) != 0)
    {
        printf("Error creating write thread\n");
    }
    pthread_create(&readThread, NULL, readFunction, NULL);

    
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
    // Initialize format context, open input file, etc.
    if (initialize() < 0)
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