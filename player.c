#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <gtk/gtk.h>
#include <cairo.h>
#include <stdio.h>
#include <string.h>

static void playVideo()
{
   
}
static void pauseVideo()
{
    
}
static void slowDown()
{
  
}
static void speedUp()
{
   
}
static void
activate(GtkApplication *app,
         gpointer user_data)
{
    GtkWidget *darea = gtk_drawing_area_new();

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *menuBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    GtkWidget *button = gtk_button_new_with_label("Slow Down");
    g_signal_connect_swapped(button, "clicked", G_CALLBACK(slowDown), (gpointer)0);
    GtkWidget *button2 = gtk_button_new_with_label("Pause");
    g_signal_connect_swapped(button2, "clicked", G_CALLBACK(pauseVideo), NULL);
    GtkWidget *button3 = gtk_button_new_with_label("Play");
    g_signal_connect_swapped(button3, "clicked", G_CALLBACK(playVideo), NULL);
    GtkWidget *button4 = gtk_button_new_with_label("Speed Up");
    g_signal_connect_swapped(button4, "clicked", G_CALLBACK(speedUp), (gpointer)0);

    gtk_box_append(GTK_BOX(menuBox), button);
    gtk_box_append(GTK_BOX(menuBox), button2);
    gtk_box_append(GTK_BOX(menuBox), button3);
    gtk_box_append(GTK_BOX(menuBox), button4);

    //Window Settings
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Picture Editor");

    gtk_widget_set_size_request(darea, 400, 400);
    gtk_box_append(GTK_BOX(box), darea);
    gtk_box_append(GTK_BOX(box), menuBox);
    gtk_window_set_child(GTK_WINDOW(window), box);

    //gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(darea), draw_image, NULL, NULL);
    gtk_window_present(GTK_WINDOW(window));
}
int main(int argc, char *argv[])
{
    //Ensures the correct arguments are passed into command line
    // if (argc != 4)
    // {
    //     fprintf(stderr, "usage: %s  input_file video_output_file(without file extension) frame_num\n"
    //                     "API example program to show how to read frames from an input file.\n"
    //                     "This program reads frames from a file, decodes them, and writes decoded\n"
    //                     "video frames to a rawvideo file named video_output_file, and decoded\n"
    //                     "audio frames to a rawaudio file named audio_output_file.\n",
    //             argv[0]);
    //     exit(1);
    // }
    
    // Create a new application
    GtkApplication *app = gtk_application_new("com.example.GtkApplication",
                                              G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    return status;
}