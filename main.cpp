#define MOZ_WAYLAND 1
#define MOZ_ENABLE_DBUS 1
#define MOZ_ASAN 1

#include <gdk/gdk.h>

#ifdef MOZ_WAYLAND
#include <gdk/gdkwayland.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#if defined(MOZ_ENABLE_DBUS)
#  include <glib.h>
#  include <glib/gstdio.h>
#  include <dlfcn.h>
#endif

        gboolean save_to_stdout(const gchar* buf, gsize count, GError** error,
                                gpointer data) {
    size_t written = fwrite(buf, 1, count, stdout);
    if (written != count) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "Write to stdout failed: %s", g_strerror(errno));
        return FALSE;
    }

    return TRUE;
}

#if defined(MOZ_ENABLE_DBUS) && defined(MOZ_WAYLAND)
static GdkPixbuf* get_screenshot_dbus() {
    char *path = nullptr, *filename = nullptr, *tmpname = nullptr;
    GdkPixbuf* screenshot = nullptr;
    const char* method_name;
    GVariant* method_params;
    GDBusConnection* connection;

    auto sGApplicationGetDbusConnection = (GDBusConnection * (*)(GApplication*))
            dlsym(RTLD_DEFAULT, "g_application_get_dbus_connection");
    if (!sGApplicationGetDbusConnection) {
        return nullptr;
    }

    path =
            g_build_filename(g_get_user_cache_dir(), "mozilla-screenshot", nullptr);
    g_mkdir_with_parents(path, 0700);

    tmpname = g_strdup_printf("mozilla-screen-%d.png", g_random_int());
    filename = g_build_filename(path, tmpname, nullptr);

    method_name = "Screenshot";
    method_params = g_variant_new("(bbs)", FALSE, /* include pointer */
                                  FALSE,          /* flash */
                                  filename);

    GApplication* app = g_application_new(nullptr, G_APPLICATION_NON_UNIQUE);
    g_application_register(app, nullptr, nullptr);
    connection = sGApplicationGetDbusConnection(app);
    if (g_dbus_connection_call_sync(
            connection, "org.gnome.Shell.Screenshot",
            "/org/gnome/Shell/Screenshot", "org.gnome.Shell.Screenshot",
            method_name, method_params, nullptr, G_DBUS_CALL_FLAGS_NONE, -1,
            nullptr, nullptr)) {
        screenshot = gdk_pixbuf_new_from_file(filename, nullptr);
        unlink(filename);
    }

    g_free(path);
    g_free(tmpname);
    g_free(filename);

    return screenshot;
}
#endif

int main(int argc, char** argv) {
    gdk_init(&argc, &argv);

    GdkPixbuf* screenshot = nullptr;

#if defined(MOZ_ENABLE_DBUS) && defined(MOZ_WAYLAND)
    GdkDisplay* display = gdk_display_get_default();
    if (display && GDK_IS_WAYLAND_DISPLAY(display)) {
        screenshot = get_screenshot_dbus();
    }
#endif
    if (!screenshot) {
        GdkWindow* window = gdk_get_default_root_window();
        screenshot =
                gdk_pixbuf_get_from_window(window, 0, 0, gdk_window_get_width(window),
                                           gdk_window_get_height(window));
    }

    if (!screenshot) {
        fprintf(stderr, "%s: failed to create screenshot GdkPixbuf\n", argv[0]);
        return 1;
    }

    GError* error = nullptr;
    if (argc > 1) {
        gdk_pixbuf_save(screenshot, argv[1], "png", &error, nullptr);
    } else {
        gdk_pixbuf_save_to_callback(screenshot, save_to_stdout, nullptr, "png",
                                    &error, nullptr);
    }
    if (error) {
        fprintf(stderr, "%s: failed to write screenshot as png: %s\n", argv[0],
                error->message);
        return error->code;
    }

    return 0;
}

// These options are copied from mozglue/build/AsanOptions.cpp
#ifdef MOZ_ASAN
extern "C" const char* __asan_default_options() {
    return "allow_user_segv_handler=1:alloc_dealloc_mismatch=0:detect_leaks=0";
}
#endif