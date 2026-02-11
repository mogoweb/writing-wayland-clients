#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <wayland-client.h>
#include <cairo/cairo.h>
#include <dbus/dbus.h>
#include <poll.h>

#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-client-protocol.h"

// SNI 相关的属性定义
#define SNI_SERVICE   "org.wayland.demo.tray"
#define SNI_PATH      "/StatusNotifierItem"
#define SNI_INTERFACE "org.kde.StatusNotifierItem"

// 状态结构体
struct app_state {
    struct wl_display *display;
    struct wl_shm *shm;
    struct wl_compositor *compositor;
    struct xdg_wm_base *wm_base;
    struct zxdg_decoration_manager_v1 *deco_manager;
    struct zxdg_toplevel_decoration_v1 *deco;

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    
    int width, height;
    int running;
    int visible;

    DBusConnection *dbus_conn;
};

static const char introspection_xml[] =
"<node>"
"  <interface name='org.freedesktop.DBus.Introspectable'>"
"    <method name='Introspect'>"
"      <arg name='xml' type='s' direction='out'/>"
"    </method>"
"  </interface>"
"  <interface name='org.freedesktop.DBus.Properties'>"
"    <method name='Get'>"
"      <arg direction='in' type='s'/>"
"      <arg direction='in' type='s'/>"
"      <arg direction='out' type='v'/>"
"    </method>"
"    <method name='GetAll'>"
"      <arg direction='in' type='s'/>"
"      <arg direction='out' type='a{sv}'/>"
"    </method>"
"  </interface>"
"  <interface name='org.kde.StatusNotifierItem'>"
"    <method name='Activate'>"
"      <arg type='i' direction='in'/>"
"      <arg type='i' direction='in'/>"
"    </method>"
"    <property name='Category' type='s' access='read'/>"
"    <property name='Id' type='s' access='read'/>"
"    <property name='Status' type='s' access='read'/>"
"    <property name='IconName' type='s' access='read'/>"
"  </interface>"
"</node>";

static void append_string_variant(DBusMessageIter *iter, const char *value) {
    DBusMessageIter variant;
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "s", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &value);
    dbus_message_iter_close_container(iter, &variant);
}

// --- D-Bus 属性回调：这里定义图标和状态 ---
DBusHandlerResult sni_message_handler(DBusConnection *conn, DBusMessage *msg, void *data) {
    printf("D-Bus 消息到达\n");
    if (dbus_message_is_method_call(msg,
        "org.freedesktop.DBus.Introspectable",
        "Introspect")) {

        DBusMessage *reply = dbus_message_new_method_return(msg);
        dbus_message_append_args(reply,
                                 DBUS_TYPE_STRING,
                                 &introspection_xml,
                                 DBUS_TYPE_INVALID);

        dbus_connection_send(conn, reply, NULL);
        dbus_connection_flush(conn);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    // 1. 处理属性获取请求 (这是托盘获取图标的关键)
    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Properties", "Get")) {
        const char *iface, *prop;
        dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID);

        DBusMessage *reply = dbus_message_new_method_return(msg);
        DBusMessageIter iter, variant;

        if (strcmp(prop, "IconName") == 0) {
            printf("获取图标属性\n");
            // 【在这里指定图标】可以使用系统图标名，如 "utilities-terminal", "favorite", "applications-games"
            const char *icon_name = "sunlogin_client.png"; 
            dbus_message_iter_init_append(reply, &iter);
            dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &icon_name);
            dbus_message_iter_close_container(&iter, &variant);
        } else if (strcmp(prop, "Status") == 0) {
            // 状态可以是 Active, Passive, NeedsAttention
            const char *status = "Active";
            dbus_message_iter_init_append(reply, &iter);
            dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &status);
            dbus_message_iter_close_container(&iter, &variant);
        } else if (strcmp(prop, "Id") == 0) {
            const char *id = "MyWaylandApp";
            dbus_message_iter_init_append(reply, &iter);
            dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &id);
            dbus_message_iter_close_container(&iter, &variant);
        } else if (strcmp(prop, "Category") == 0) {
            const char *cat = "ApplicationStatus";
            dbus_message_iter_init_append(reply, &iter);
            dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &cat);
            dbus_message_iter_close_container(&iter, &variant);
        }

        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    // 2. 处理点击托盘动作 (Activate 方法)
    if (dbus_message_is_method_call(msg, SNI_INTERFACE, "Activate")) {
        printf("托盘被点击，尝试恢复窗口...\n");
        // 这里应触发 Wayland 重新显示窗口的逻辑
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

// --- 初始化 SNI ---
static void setup_sni(struct app_state *app) {
    DBusError err;
    dbus_error_init(&err);

    app->dbus_conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!app->dbus_conn ||dbus_error_is_set(&err)) {
        fprintf(stderr, "DBus error: %s\n", err.message);
        dbus_error_free(&err);
        return;
    }

    int ret = dbus_bus_request_name(app->dbus_conn,
                                    SNI_SERVICE,
                                    DBUS_NAME_FLAG_REPLACE_EXISTING,
                                    &err);

    if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        fprintf(stderr, "Failed to acquire name\n");
        return;
    }
    printf("成功注册 D-Bus 服务名: %s\n", SNI_SERVICE);

    DBusObjectPathVTable vtable = {
        .message_function = sni_message_handler
    };

    if (!dbus_connection_register_object_path(app->dbus_conn,
                                              SNI_PATH,
                                              &vtable,
                                              app)) {
        fprintf(stderr, "Failed to register object path\n");
        return;
    }
    printf("成功注册 D-Bus 对象路径: %s\n", SNI_PATH);
    dbus_connection_read_write_dispatch(app->dbus_conn, 100);

    /* 注册到 watcher */
    DBusMessage *msg = dbus_message_new_method_call(
        "org.kde.StatusNotifierWatcher",
        "/StatusNotifierWatcher",
        "org.kde.StatusNotifierWatcher",
        "RegisterStatusNotifierItem");

    const char *full = SNI_SERVICE;
    dbus_message_append_args(msg,
                             DBUS_TYPE_STRING,
                             &full,
                             DBUS_TYPE_INVALID);

    dbus_connection_send(app->dbus_conn, msg, NULL);
    dbus_connection_flush(app->dbus_conn);
    printf("已向 StatusNotifierWatcher 注册\n");
    dbus_message_unref(msg);
}

// --- Cairo 绘图辅助 ---
static struct wl_buffer* create_shm_buffer(struct app_state *app) {
    char name[] = "/tmp/wayland-shm-XXXXXX";
    int fd = mkstemp(name);
    if (fd < 0) {
        fprintf(stderr, "mkstemp failed\n");
        return NULL;
    }

    unlink(name);
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, app->width);
    int size = stride * app->height;
    ftruncate(fd, size);

    unsigned char *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    struct wl_shm_pool *pool = wl_shm_create_pool(app->shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, app->width, app->height, stride, WL_SHM_FORMAT_ARGB8888);
    
    // 使用 Cairo 绘制纯色背景 (例如：天蓝色)
    cairo_surface_t *surface = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32, app->width, app->height, stride);
    cairo_t *cr = cairo_create(surface);
    cairo_set_source_rgb(cr, 0.53, 0.81, 0.98); // SkyBlue
    cairo_paint(cr);
    
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    munmap(data, size);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buffer;
}

// --- Wayland 回调 ---
static void xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *xdg_toplevel,
                                          int32_t width, int32_t height, struct wl_array *states) {
    // TODO:
}

static void xdg_toplevel_handle_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    struct app_state *app = data;
    printf("点击了关闭按钮，隐藏窗口并驻留托盘...\n");
    
    // 隐藏窗口逻辑：解绑角色
    if (app->deco) zxdg_toplevel_decoration_v1_destroy(app->deco);
    if (app->deco_manager) zxdg_decoration_manager_v1_destroy(app->deco_manager);
    xdg_toplevel_destroy(app->xdg_toplevel);
    xdg_surface_destroy(app->xdg_surface);
    app->xdg_toplevel = NULL;
    app->xdg_surface = NULL;
    app->visible = 0;
    wl_surface_attach(app->surface, NULL, 0, 0);
    wl_surface_commit(app->surface);
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = xdg_toplevel_handle_configure,
    .close = xdg_toplevel_handle_close,
};

static void xdg_surface_handle_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    struct app_state *app = data;
    // 必须确认配置事件
    xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_handle_configure,
};

// --- 全局注册 ---
static void registry_handle_global(void *data, struct wl_registry *reg, uint32_t id, const char *interface, uint32_t version) {
    struct app_state *app = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0)
        app->compositor = wl_registry_bind(reg, id, &wl_compositor_interface, 4);
    else if (strcmp(interface, wl_shm_interface.name) == 0)
        app->shm = wl_registry_bind(reg, id, &wl_shm_interface, 1);
    else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
        app->wm_base = wl_registry_bind(reg, id, &xdg_wm_base_interface, 1);
    else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0)
        app->deco_manager = wl_registry_bind(reg, id, &zxdg_decoration_manager_v1_interface, 1);
}

int main() {
    struct app_state app = { .width = 400, .height = 300, .running = 1, .visible = 1 };
    
    app.display = wl_display_connect(NULL);
    struct wl_registry *registry = wl_display_get_registry(app.display);
    static const struct wl_registry_listener reg_listener = { .global = registry_handle_global };
    wl_registry_add_listener(registry, &reg_listener, &app);
    wl_display_roundtrip(app.display);

    // 创建 Surface
    app.surface = wl_compositor_create_surface(app.compositor);
    app.xdg_surface = xdg_wm_base_get_xdg_surface(app.wm_base, app.surface);
    xdg_surface_add_listener(app.xdg_surface, &xdg_surface_listener, &app);
    app.xdg_toplevel = xdg_surface_get_toplevel(app.xdg_surface);
    xdg_toplevel_add_listener(app.xdg_toplevel, &toplevel_listener, &app);

    // 请求 SSD (服务端装饰)
    if (app.deco_manager) {
        app.deco = zxdg_decoration_manager_v1_get_toplevel_decoration(app.deco_manager, app.xdg_toplevel);
        zxdg_toplevel_decoration_v1_set_mode(app.deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    wl_surface_commit(app.surface);
    wl_display_roundtrip(app.display);

    // 渲染第一帧
    struct wl_buffer *buffer = create_shm_buffer(&app);
    wl_surface_attach(app.surface, buffer, 0, 0);
    wl_surface_commit(app.surface);

    // 设置托盘
    setup_sni(&app);

    struct pollfd fds[1];
    fds[0].fd = wl_display_get_fd(app.display);
    fds[0].events = POLLIN;

    // 主循环
    while (app.running/* && wl_display_dispatch(app.display) != -1*/) {
        // --- STEP 1: 准备读取 Wayland 事件 ---
        // 这步非常关键，它告诉 Wayland 库我们要开始处理事件了
        while (wl_display_prepare_read(app.display) != 0) {
            wl_display_dispatch_pending(app.display);
        }

        // --- STEP 2: 发送所有挂起的 Wayland 请求 ---
        wl_display_flush(app.display);

        // --- STEP 3: 使用 poll 同时监控事件 ---
        // 设置超时时间（例如 10 毫秒），这样 D-Bus 才有机会被处理
        int ret = poll(fds, 1, 10);

        if (ret > 0 && (fds[0].revents & POLLIN)) {
            // 有 Wayland 数据可读
            wl_display_read_events(app.display);
        } else {
            // 没有数据或超时，取消读取准备
            wl_display_cancel_read(app.display);
        }

        // 分发处理 Wayland 事件
        wl_display_dispatch_pending(app.display);

        // --- STEP 4: 处理 D-Bus 消息 (这是你之前的痛点) ---
        // read_write_dispatch 会在内部调用你的 sni_message_handler
        // 参数 0 表示非阻塞，立刻返回
        dbus_connection_read_write_dispatch(app.dbus_conn, 0);
    }

    return 0;
}
