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

#define SNI_MENU_PATH "/MenuBar"
// 菜单项结构
struct menu_item {
    int id;
    const char *label;
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

struct sni_manager {
    DBusConnection *dbus_conn;
    char *id;
    char *icon_name;
    cairo_surface_t *icon_surface;
    char *status;
    
    // 回调相关
    sni_activate_callback on_activate;
    sni_menu_click_callback on_menu_click;
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
        dbus_message_iter_init_append(reply, &iter);

        printf("获取属性: %s\n", prop);
        if (strcmp(prop, "IconPixmap") == 0) {
            printf("获取IconPixmap属性\n");
            dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "a(iiay)", &variant);
            append_pixmap(&variant, sni->icon_surface); // 调用转换函数
            dbus_message_iter_close_container(&iter, &variant);
        } else if (strcmp(prop, "IconName") == 0) {
            printf("获取图标属性\n");
            const char *icon_name = sni->icon_name;
            dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &icon_name);
            dbus_message_iter_close_container(&iter, &variant);
        } else if (strcmp(prop, "Status") == 0) {
            // 状态可以是 Active, Passive, NeedsAttention
            const char *status = "Active";
            dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &status);
            dbus_message_iter_close_container(&iter, &variant);
        } else if (strcmp(prop, "Id") == 0) {
            const char *id = "MyWaylandApp";
            dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &id);
            dbus_message_iter_close_container(&iter, &variant);
        } else if (strcmp(prop, "Category") == 0) {
            const char *cat = "ApplicationStatus";
            dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &cat);
            dbus_message_iter_close_container(&iter, &variant);
        } else if (strcmp(prop, "Menu") == 0) {
            const char *menu_path = SNI_MENU_PATH;
            dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "o", &variant);
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_OBJECT_PATH, &menu_path);
            dbus_message_iter_close_container(&iter, &variant);
        } if (strcmp(prop, "ToolTip") == 0) {
            dbus_message_iter_open_container(
                &iter,
                DBUS_TYPE_VARIANT,
                "(sa(iiay)ss)",
                &variant);

            DBusMessageIter struct_iter;
            dbus_message_iter_open_container(
                &variant,
                DBUS_TYPE_STRUCT,
                NULL,
                &struct_iter);

            const char *icon_name = "applications-system";
            const char *title = "Wayland Tray Demo";
            const char *text = "Hello from pure C SNI demo";

            dbus_message_iter_append_basic(
                &struct_iter,
                DBUS_TYPE_STRING,
                &icon_name);
            
            /* empty icon pixmap array */
            DBusMessageIter array_iter;
            dbus_message_iter_open_container(
                &struct_iter,
                DBUS_TYPE_ARRAY,
                "(iiay)",
                &array_iter);
            dbus_message_iter_close_container(&struct_iter, &array_iter);

            dbus_message_iter_append_basic(
                &struct_iter,
                DBUS_TYPE_STRING,
                &title);

            dbus_message_iter_append_basic(
                &struct_iter,
                DBUS_TYPE_STRING,
                &text);

            dbus_message_iter_close_container(
                &variant,
                &struct_iter);

            dbus_message_iter_close_container(
                &iter,
                &variant);
        }

        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    // 2. 处理点击托盘动作 (Activate 方法)
    if (dbus_message_is_method_call(msg, SNI_INTERFACE, "Activate")) {
        printf("托盘被点击，尝试恢复窗口...\n");
        if (sni->on_activate) {
            sni->on_activate(sni->user_data);
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult menu_handler(DBusConnection *conn, DBusMessage *msg, void *data) {
    struct sni_manager *sni = (struct sni_manager *)data;

    // 1. 获取菜单布局 (GetLayout)
    if (dbus_message_is_method_call(msg, "com.canonical.dbusmenu", "GetLayout")) {
        DBusMessage *reply = dbus_message_new_method_return(msg);
        DBusMessageIter iter, root_struct, props_array, dict_entry, variant, children_array;

        uint32_t revision = 1;
        dbus_message_iter_init_append(reply, &iter);
        dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &revision);

        // 返回一个嵌套结构：(ia{sv}av) -> (ID, 属性, 子项)
        dbus_message_iter_open_container(&iter, DBUS_TYPE_STRUCT, NULL, &root_struct);

        int id = 0; // 根节点 ID
        dbus_message_iter_append_basic(&root_struct, DBUS_TYPE_INT32, &id);

        // 根节点属性 (留空)
        dbus_message_iter_open_container(&root_struct, DBUS_TYPE_ARRAY, "{sv}", &props_array);
        dbus_message_iter_close_container(&root_struct, &props_array);

        // 子节点数组 (放我们的退出项)
        dbus_message_iter_open_container(&root_struct, DBUS_TYPE_ARRAY, "v", &children_array);

        // --- 退出项开始 ---
        DBusMessageIter child_variant, child_struct, child_props;
        dbus_message_iter_open_container(&children_array, DBUS_TYPE_VARIANT, "(ia{sv}av)", &child_variant);
        dbus_message_iter_open_container(&child_variant, DBUS_TYPE_STRUCT, NULL, &child_struct);

        int exit_id = MENU_ID_EXIT;
        dbus_message_iter_append_basic(&child_struct, DBUS_TYPE_INT32, &exit_id);

        // 退出项属性 (Label = "退出")
        dbus_message_iter_open_container(&child_struct, DBUS_TYPE_ARRAY, "{sv}", &child_props);

        dbus_message_iter_open_container(&child_props, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
        const char *label_key = "label";
        dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &label_key);
        const char *label_val = "退出程序";
        dbus_message_iter_open_container(&dict_entry, DBUS_TYPE_VARIANT, "s", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &label_val);
        dbus_message_iter_close_container(&dict_entry, &variant);
        dbus_message_iter_close_container(&child_props, &dict_entry);

        dbus_message_iter_close_container(&child_struct, &child_props);

        // 退出项没有子项
        DBusMessageIter grandchild_array;
        dbus_message_iter_open_container(&child_struct, DBUS_TYPE_ARRAY, "v", &grandchild_array);
        dbus_message_iter_close_container(&child_struct, &grandchild_array);

        dbus_message_iter_close_container(&child_variant, &child_struct);
        dbus_message_iter_close_container(&children_array, &child_variant);
        // --- 退出项结束 ---

        dbus_message_iter_close_container(&root_struct, &children_array);
        dbus_message_iter_close_container(&iter, &root_struct);

        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    // 2. 处理点击事件 (Event)
    if (dbus_message_is_method_call(msg, "com.canonical.dbusmenu", "Event")) {
        int id;
        const char *event_type;
        dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &id, DBUS_TYPE_STRING, &event_type, DBUS_TYPE_INVALID);

        if (sni->on_menu_click && strcmp(event_type, "clicked") == 0) {
            sni->on_menu_click(id, sni->user_data);
        }

        DBusMessage *reply = dbus_message_new_method_return(msg);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
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

    // 注册菜单处理器
    DBusObjectPathVTable menu_vtable = { .message_function = menu_handler };
    if (!dbus_connection_try_register_object_path(sni->dbus_conn, SNI_MENU_PATH, &menu_vtable, sni, NULL)) {
        fprintf(stderr, "Failed to register menu object path\n");
    }
    printf("成功注册 D-Bus 菜单路径: %s\n", SNI_MENU_PATH);

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

void sni_manager_set_on_activate(sni_manager_t *sni, sni_activate_callback cb, void *user_data)
{
    sni->on_activate = cb;
    sni->user_data = user_data;
}

void sni_manager_set_on_menu_click(sni_manager_t *sni, sni_menu_click_callback cb, void *user_data)
{
    sni->on_menu_click = cb;
    sni->user_data = user_data;
}
