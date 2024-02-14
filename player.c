#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <gtk/gtk.h>
#include <cairo.h>
#include <stdio.h>
#include <string.h>

pthread_t readThread;
pthread_t writeThread;
typedef struct
{
    int width;
    int height;
    int maxval;
    unsigned char *pixels;
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

Frame arrayOfFrames[100]; //Optimize array, potentially clear already shown frames
int readOccurence = 1;
int readIndex=0;
int writeIndex = 0;
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
    Frame currentFrame = {frame->width,frame->height,255,pixels};
    arrayOfFrames[readIndex] = currentFrame;
    return 0;
}
void draw_images(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data)
{
    Frame currentFrame = arrayOfFrames[writeIndex];
    //Iterates through every pixel in the images array
    for (int y = 0; y < currentFrame.height; y++)
    {
        for (int x = 0; x < currentFrame.width; x++)
        {
            //Offset for finding the colored pixels in the array based on row and column
            int offset = (y * currentFrame.width + x) * 3;
            unsigned char red = currentFrame.pixels[offset];       //Offset will give u red
            unsigned char green = currentFrame.pixels[offset + 1]; //+1 for green
            unsigned char blue = currentFrame.pixels[offset + 2];  //+2 from offset gives you blue


            //Set the pixel color for ppm tile
            cairo_set_source_rgb(cr, red / 255.0, green / 255.0, blue / 255.0);
            // Draw a rectangle representing the pixel for ppm image
            cairo_rectangle(cr, x, y, 1, 1);
            cairo_fill(cr);
        }
    }
    writeIndex++;
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

    // get all the available frames from the decoder
    while (ret >= 0)
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
        if (frameRate*(readIndex-1)<=dec->frame_num && dec->frame_num<frameRate*readIndex)
        {
            printf("Frame Num: %lld\n", dec->frame_num);
            ret = output_video_frame(frame);
            readIndex++;
        }

        av_frame_unref(frame);
        if (ret < 0)
            return ret;
    }
    readOccurence++;
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
int decodeFrame()
{
    int ret = 0;

    /* open input file, and allocate format context */
    if (avformat_open_input(&fmt_ctx, src_filename, NULL, NULL) < 0)
    {
        fprintf(stderr, "Could not open source file %s\n", src_filename);
        exit(1);
    }

    /* retrieve stream information */
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0)
    {
        fprintf(stderr, "Could not find stream information\n");
        exit(1);
    }

    if (open_codec_context(&video_stream_idx, &video_dec_ctx, fmt_ctx, AVMEDIA_TYPE_VIDEO) >= 0)
    {

        video_stream = fmt_ctx->streams[video_stream_idx];
        snprintf(src_filename, sizeof(src_filename) + 4, "%s.ppm", video_dst_filename);
        //snprintf(video_dst_filename, 100, "%s.ppm", video_dst_filename);
        //Create the ppm file
        video_dst_file = fopen(src_filename, "wb");
        //Error upon creating file
        if (!video_dst_file)
        {
            fprintf(stderr, "Could not open destination file %s\n", video_dst_filename);
            ret = 1;
            goto end;
        }
        
        /* allocate image where the decoded image will be put */
        width = video_dec_ctx->width;
        height = video_dec_ctx->height;
        pix_fmt = video_dec_ctx->pix_fmt;
        ret = av_image_alloc(video_dst_data, video_dst_linesize,
                             width, height, pix_fmt, 1);
        if (ret < 0)
        {
            fprintf(stderr, "Could not allocate raw video buffer\n");
            goto end;
        }
        video_dst_bufsize = ret;
    }

    /* dump input information to stderr */
    av_dump_format(fmt_ctx, 0, src_filename, 0);

    if (!video_stream)
    {
        fprintf(stderr, "Could not find video stream in the input, aborting\n");
        ret = 1;
        goto end;
    }
    //Allocates memory for the frame
    frame = av_frame_alloc();
    if (!frame)
    {
        fprintf(stderr, "Could not allocate frame\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    pkt = av_packet_alloc();
    if (!pkt)
    {
        fprintf(stderr, "Could not allocate packet\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* read frames from the file */
    while (av_read_frame(fmt_ctx, pkt) >= 0)
    {
        // check if the packet belongs to a stream we are interested in, otherwise
        // skip it
        if (pkt->stream_index == video_stream_idx)
        {
            ret = decode_packet(video_dec_ctx, pkt);
        }
        av_packet_unref(pkt);
        if (ret < 0)
            break;
    }

    /* flush the decoders */
    if (video_dec_ctx)
        decode_packet(video_dec_ctx, NULL);

end:
    avcodec_free_context(&video_dec_ctx);
    avcodec_free_context(&audio_dec_ctx);
    avformat_close_input(&fmt_ctx);
    if (video_dst_file)
        fclose(video_dst_file);
    if (audio_dst_file)
        fclose(audio_dst_file);
    av_packet_free(&pkt);
    av_frame_free(&frame);
    av_free(video_dst_data[0]);

    return ret < 0;
}
static void *readFunction(){
    //Lock
    //Read in the next 'x' frames, x is fps
    //unlock
    //Signal write
}
static void *writeFunction()
{
    //Lock
    //for loop 'x' iterations, x is fps
    //draw current writeIndex frame
    //sleep for a second
    //Increment writeIndex
    //unlock
    //Signal read
}

static void
activate(GtkApplication *app,
         gpointer user_data)
{
    GtkWidget *darea = gtk_drawing_area_new();

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
    pthread_create(&readThread, NULL, readFunction, NULL);
    gtk_window_present(GTK_WINDOW(window));
}
int main(int argc, char *argv[])
{
    //Ensures the correct arguments are passed into command line
    if (argc != 4)
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
    //Calls function to decode and save the frame
    decodeFrame();
    //Resets argc to ensure GTK does not throw any errors
    argc = 1;
    // Create a new application
    GtkApplication *app = gtk_application_new("com.example.GtkApplication",
                                              G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    return status;
}