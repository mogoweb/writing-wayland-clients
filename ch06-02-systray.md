## 系统托盘

在 Wayland 开发中，系统托盘其实并不属于 Wayland 协议范畴，我们必须依赖基于 D-Bus 的 SNI (Status Notifier Item) 协议。

本章将梳理如何在 Wayland 客户端中实现一个完整的系统托盘功能，涵盖图标显示、自定义图像、右键菜单以及窗口隐藏/恢复逻辑。

为了更好的理解基于 D-Bus 的 SNI 协议，示例代码尽量不借助第三方封装库，只引入 ibdbus-1-dev 开发库处理 D-Bus 消息的收发处理，引入 Cairo 库 处理 pixmap 图像。所以在编译示例代码之前，请使用如下命令安装开发库：

```bash
sudo apt install libcairo2-dev libdbus-1-dev
```

### 一、 核心概念与职责划分

1. SNI 协议 (org.kde.StatusNotifierItem): 负责定义托盘图标的属性（ID、图标、状态）和动作（左键点击、激活）。
2. DBusMenu 协议 (com.canonical.dbusmenu): 负责定义右键菜单的内容和点击回调。
3. Wayland 角色管理: 创建窗口，隐藏窗口时销毁 xdg_toplevel 角色，恢复窗口时重建并重新进行配置（Configure）握手。

### 二、 系统托盘 (SNI) 的dbus实现

#### 2.1. 理解 D-Bus 通信体系

在进行 D-Bus 通信之前，一般要定义 Service、Path 和 Interfaces。比如我们为 SNI 协议通信定义了：
```c
#define SNI_SERVICE   "org.wayland.demo.tray"
#define SNI_PATH      "/StatusNotifierItem"
#define SNI_INTERFACE "org.kde.StatusNotifierItem"
```
* 服务名 (Well-known Name)

  这是你的应用程序在 D-Bus 总线上的唯一标识符。它让系统托盘管理器（Watcher）能在成百上千个进程中找到你。在整个 Session Bus（用户会话总线）中，这个名字必须是唯一的。通常使用反向域名格式（如 com.baidu.music）。当你的程序启动并成功声明这个名字后，总线就会把所有发往这个地址的请求路由到你的进程。
  
* 对象路径(Object Path)

  标识程序内部提供的具体功能的路径。一个程序可以提供多个功能（比如一个音乐软件既有托盘图标，又有远程播放控制）。SNI 协议约定俗成通常使用 /StatusNotifierItem。托盘管理器会去这个特定的路径下查找图标属性。

* 接口 (Interface)

  这是双方约定的通讯协议/方法集合。它定义了你可以被调用的方法（如 Activate）和可以被读取的属性（如 IconName, IconPixmap）。接口名通常与协议规范绑定。虽然我们是在 Wayland 下开发，但因为 SNI 协议最早由 KDE 定义，所以接口名统一叫 org.kde.StatusNotifierItem。

#### 2.2. 注册与握手

程序启动时，首先向 D-Bus 注册应用的服务名和对象路径。

```c
    dbus_conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    dbus_bus_request_name(dbus_conn,
                          SNI_SERVICE,
                          DBUS_NAME_FLAG_REPLACE_EXISTING,
                          &err);

    DBusObjectPathVTable vtable = {
        .message_function = sni_message_handler
    };

    dbus_connection_register_object_path(dbus_conn,
                                         SNI_PATH,
                                         &vtable,
                                         sni);
```
message_function 指定收到 D-Bus 消息后的处理函数。一般形式为：
```c
DBusHandlerResult sni_message_handler(DBusConnection *conn,
                                      DBusMessage *msg,
                                      void *data)
{
    if (dbus_message_is_method_call(msg, "xxx", "xxx"))
    {
        ...
    }
    // 比如处理属性获取请求，与托盘获取图标相关)
    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Properties", "Get"))
    {
        const char *iface, *prop;
        dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID);

        DBusMessage *reply = dbus_message_new_method_return(msg);
        DBusMessageIter iter, variant;
        dbus_message_iter_init_append(reply, &iter);
        if (strcmp(prop, "IconPixmap") == 0) {
        } else if (strcmp(prop, "IconName") == 0) {
        } else if (...) {
            ...
        }
    }
}
```

接下来还要需要向 `org.kde.StatusNotifierWatcher` 注册。方法为向 `/StatusNotifierWatcher` 发送 `RegisterStatusNotifierItem`，参数为你的 D-Bus 服务名。
```c
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
```

#### 2.3. 图标显示
SNI 协议中有两种方式指定托盘图标，一种是在上面的 message_function 函数中处理 IconName ，另一种方式是处理 IconPixmap。如果已经处理了 IconName，系统就不会再发送 IconPixmap。所以如果希望使用自定义的 Pixmap 图像，在处理 IconName 时给一个空的字符串。

* IconName: 这里并不是给一个具体的图像文件的名称，而是用户主题中的某个图像名，如 `apps-system`。优点是图标可以自动适配用户主题颜色和风格，缺点是不够灵活，图标必须先加入用户主题。
* IconPixmap: 当你需要显示应用自己的图标，甚至是自定义图形（如 Cairo 画的进度圆圈）时，你需要自己处理图标文件的读取（或者自行绘制），然后以数据流的形式返回给 D-Bus，参数中的
`a(iiay)`代表一个包含 `(宽, 高, 数据)` 的数组。
```c
DBusMessage *reply = dbus_message_new_method_return(msg);
DBusMessageIter iter, variant;
dbus_message_iter_init_append(reply, &iter);
dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "a(iiay)", &variant);
append_pixmap(&variant, sni->icon_surface); // 调用转换函数
dbus_message_iter_close_container(&iter, &variant);
```

Cairo `ARGB32` 是宿主字节序。SNI 协议强制要求 Big-Endian ARGB。
```c
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
```

### 三、 托盘菜单 (DBusMenu) 的布局与交互

SNI 菜单遵循 `com.canonical.dbusmenu` 协议。它不传输像素，只传输“菜单树”。

#### 3.1. `GetLayout` 树形结构
这是最复杂的部分。你需要返回一个嵌套的 Variant 结构：
*   **根节点 (ID 0)**: 必须设置 `children-display: submenu`。
*   **子节点 (ID 1+)**: 
    *   `label`: 文本。
    *   `enabled`: 布尔值（必须提供，否则可能导致渲染为黑色或不可用）。
    *   `visible`: 布尔值。
    *   `type`: `standard` 或 `separator`。

```c
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
```
#### 3.2. 菜单事件路由
当用户点击右键菜单项时，管理器会调用 `Event` 方法：
*   **参数**: `(i s v u)` -> `(ID, 事件类型, 数据, 时间戳)`。

```c
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
```

### 四、 主循环：高性能 Poll 模式

在前面的示例代码中，我们通常使用这样的主循环：
``` c
    // 主事件循环
    while (state.running && wl_display_dispatch(state.display) != -1) {
        // 事件处理都在监听器回调中完成
    }
```
但在本示例中却行不通，因为 `wl_display_dispatch()`，会阻塞 D-Bus 处理。这种情况下建议使用标准的 `poll` 或 `epoll`：

```c
    struct pollfd fds[1];
    fds[0].fd = wl_display_get_fd(app.display);
    fds[0].events = POLLIN;

    // 主循环
    while (app.running) {
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

        // --- STEP 4: 处理 D-Bus 消息 ---
        dbus_connection_read_write_dispatch(sni->dbus_conn, 0);
    }
```
上面的代码还有改进的空间，比如 poll 可以同时监听 wayland 和 dbus 的文件句柄，这样有消息到达，都可以得到处理，而不会阻塞。

### 五、调试小技巧

在 SNI（Status Notifier Item）开发过程中，D-Bus 通信往往是一个黑盒。使用调试工具观察消息流向，是定位图标不显示、菜单没反应等问题的核心手段。

以下是针对 SNI 开发的详细调试建议和常用指令。

#### 5.1、 验证服务是否注册 (`busctl` & `dbus-send`)

在你的程序启动后，首先确认 D-Bus 总线上是否真的存在你申请的服务名。

* 列出所有服务名：
```bash
    # 推荐：使用 busctl 查看（支持模糊匹配）
    busctl --user list | grep wayland

    # 备选：使用 dbus-send
    dbus-send --session --dest=org.freedesktop.DBus --type=method_call --print-reply /org/freedesktop/DBus org.freedesktop.DBus.ListNames
```

* 查看对象树（确认 Path 是否注册成功）：
```bash
    # 查看该服务下有哪些路径，确认 /StatusNotifierItem 和 /MenuBar 是否存在
    busctl --user tree org.wayland.demo.tray
```

#### 5.2、 使用 `dbus-monitor` 监听实时消息

监听发往或来自你的应用的所有消息：

```bash
    # 替换为你定义的 SNI_SERVICE
    dbus-monitor "sender='org.wayland.demo.tray'" "destination='org.wayland.demo.tray'"
```

重点关注的信号与方法：

1.  看注册请求：看你的程序是否向 `org.kde.StatusNotifierWatcher` 发送了 `RegisterStatusNotifierItem`。
2.  看属性拉取：观察是否有来自管理器的 `org.freedesktop.DBus.Properties.Get` 调用。
3.  看交互响应：点击托盘图标时，是否有 `Activate` 方法调用进入你的程序。

#### 5.3、 手动发送消息 (`dbus-send`)

不确定是否代码的问题，你还可以尝试向 D-Bus 手动发送消息，比如模拟 SNI 的激活方法。SNI 的 `Activate` 方法接受两个整数参数（x, y 坐标，通常传 0, 0 即可）。

```bash
    # 使用 dbus-send (注意参数类型 ii 代表两个 int32)
    dbus-send --session --type=method_call --dest=org.wayland.demo.tray \
    /StatusNotifierItem org.kde.StatusNotifierItem.Activate int32:0 int32:0
```
