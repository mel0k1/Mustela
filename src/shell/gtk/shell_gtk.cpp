#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <cstring>

namespace {

void on_destroy(GtkWidget*, gpointer) {
    gtk_main_quit();
}

WebKitWebView* create_view(WebKitWebContext* context) {
    WebKitWebView* view = WEBKIT_WEB_VIEW(webkit_web_view_new_with_context(context));
    WebKitSettings* settings = webkit_web_view_get_settings(view);
    webkit_settings_set_enable_developer_extras(settings, TRUE);
    return view;
}

}

int mustela_main(int argc, char** argv) {
    gtk_init(&argc, &argv);

    bool ephemeral = false;
    const char* url = "https://example.org";

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--private") == 0) {
            ephemeral = true;
        } else {
            url = argv[i];
        }
    }

    WebKitWebContext* context = ephemeral ? webkit_web_context_new_ephemeral()
                                          : webkit_web_context_new();
    WebKitWebView* view = create_view(context);

    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Mustela");
    gtk_window_set_default_size(GTK_WINDOW(window), 1280, 800);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), nullptr);
    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(view));
    gtk_widget_show_all(window);

    webkit_web_view_load_uri(view, url);
    gtk_main();
    return 0;
}
