#include "sni.h"
#include <dbus/dbus.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h> // 用于 htonl (将主机字节序转为网络大端字节序)
#include <cairo/cairo.h>

#define SNI_SERVICE   "org.wayland.demo.tray"
#define SNI_PATH      "/StatusNotifierItem"
#define SNI_INTERFACE "org.kde.StatusNotifierItem"

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

struct sni_manager {
    DBusConnection *dbus_conn;
    char *id;
    char *icon_name;
    cairo_surface_t *icon_surface;
    char *status;
    
    // 回调相关
    sni_activate_callback on_activate;
    void *user_data;
};

// 简单的错误检查宏
#define CHECK_DBUS_ERROR(err) \
    if (dbus_error_is_set(&err)) { \
        fprintf(stderr, "DBus Error: %s\n", err.message); \
        dbus_error_free(&err); \
    }

// 将 Cairo 像素数据转换为 SNI 要求的 Big-Endian ARGB
static void append_pixmap(DBusMessageIter *iter, cairo_surface_t *surface) {
    int width = cairo_image_surface_get_width(surface);
    int height = cairo_image_surface_get_height(surface);
    unsigned char *data = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);

    DBusMessageIter array_iter, struct_iter, byte_iter;
    printf("append_pixmap: %d x %d\n", width, height);

    // 1. 最外层数组 a(iiay)
    dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "(iiay)", &array_iter);
    
    // 2. 结构体 (iiay)
    dbus_message_iter_open_container(&array_iter, DBUS_TYPE_STRUCT, NULL, &struct_iter);
    
    // 写入宽和高
    dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_INT32, &width);
    dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_INT32, &height);

    // 3. 像素字节数组 ay
    dbus_message_iter_open_container(&struct_iter, DBUS_TYPE_ARRAY, "y", &byte_iter);
    
    // 转换像素：Cairo (Native Endian) -> SNI (Big Endian ARGB)
    for (int y = 0; y < height; y++) {
        uint32_t *row = (uint32_t *)(data + y * stride);
        for (int x = 0; x < width; x++) {
            uint32_t pixel = row[x];
            uint8_t a, r, g, b;

            // Cairo ARGB32 提取 (处理宿主字节序)
            // 注意：这里假设输入是 CAIRO_FORMAT_ARGB32
            a = (pixel >> 24) & 0xFF;
            r = (pixel >> 16) & 0xFF;
            g = (pixel >> 8) & 0xFF;
            b = (pixel >> 0) & 0xFF;

            // 按 A, R, G, B 顺序写入字节流
            dbus_message_iter_append_basic(&byte_iter, DBUS_TYPE_BYTE, &a);
            dbus_message_iter_append_basic(&byte_iter, DBUS_TYPE_BYTE, &r);
            dbus_message_iter_append_basic(&byte_iter, DBUS_TYPE_BYTE, &g);
            dbus_message_iter_append_basic(&byte_iter, DBUS_TYPE_BYTE, &b);
        }
    }
    
    dbus_message_iter_close_container(&struct_iter, &byte_iter);
    dbus_message_iter_close_container(&array_iter, &struct_iter);
    dbus_message_iter_close_container(iter, &array_iter);
}

// --- D-Bus 属性回调：这里定义图标和状态 ---
static DBusHandlerResult sni_message_handler(DBusConnection *conn, DBusMessage *msg, void *data) {
    printf("D-Bus 消息到达\n");
    sni_manager_t *sni = (sni_manager_t *)data;

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

        printf("获取属性: %s\n", prop);
        if (strcmp(prop, "IconPixmap") == 0) {
            printf("获取IconPixmap属性\n");
            dbus_message_iter_init_append(reply, &iter);
            dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "a(iiay)", &variant);
            append_pixmap(&variant, sni->icon_surface); // 调用转换函数
            dbus_message_iter_close_container(&iter, &variant);
        } else if (strcmp(prop, "IconName") == 0) {
            printf("获取图标属性\n");
            const char *icon_name = sni->icon_name;
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

sni_manager_t* sni_manager_create(const char *appid, const char *icon_name)
{
    DBusError err;
    dbus_error_init(&err);

    sni_manager_t *sni = calloc(1, sizeof(sni_manager_t));
    sni->id = strdup(appid);
    sni->icon_name = strdup(icon_name);
    sni->status = strdup("Active");

    sni->dbus_conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!sni->dbus_conn ||dbus_error_is_set(&err)) {
        fprintf(stderr, "DBus error: %s\n", err.message);
        dbus_error_free(&err);
        return sni;
    }

    int ret = dbus_bus_request_name(sni->dbus_conn,
                                    SNI_SERVICE,
                                    DBUS_NAME_FLAG_REPLACE_EXISTING,
                                    &err);

    if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        fprintf(stderr, "Failed to acquire name\n");
        return sni;
    }
    printf("成功注册 D-Bus 服务名: %s\n", SNI_SERVICE);

    DBusObjectPathVTable vtable = {
        .message_function = sni_message_handler
    };

    if (!dbus_connection_register_object_path(sni->dbus_conn,
                                              SNI_PATH,
                                              &vtable,
                                              sni)) {
        fprintf(stderr, "Failed to register object path\n");
        return sni;
    }
    printf("成功注册 D-Bus 对象路径: %s\n", SNI_PATH);
    dbus_connection_read_write_dispatch(sni->dbus_conn, 100);

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

    dbus_connection_send(sni->dbus_conn, msg, NULL);
    dbus_connection_flush(sni->dbus_conn);
    printf("已向 StatusNotifierWatcher 注册\n");
    dbus_message_unref(msg);

    return sni;
}

void sni_manager_dispatch(sni_manager_t *sni)
{
    // 参数 0 表示非阻塞，立刻返回
    dbus_connection_read_write_dispatch(sni->dbus_conn, 0);
}

void sni_manager_set_icon_pixmap(sni_manager_t *sni, cairo_surface_t *surface)
{
    sni->icon_surface = surface;
}