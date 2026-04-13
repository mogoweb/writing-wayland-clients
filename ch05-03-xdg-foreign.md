## 跨进程窗口关系：xdg-foreign 协议

在传统的窗口系统中（如 X11），窗口句柄（如 X11 的 Window ID）是全局唯一的，不同进程之间可以通过窗口句柄来建立窗口关系。例如，一个进程可以将自己的窗口设置为另一个进程窗口的子窗口。

Wayland 出于安全考虑，默认不允许客户端获取其他客户端窗口的信息。但某些场景下确实需要跨进程建立窗口关系，例如：

- 嵌入式播放器：视频播放器作为独立进程运行，但其窗口需要嵌入到主应用窗口中
- 插件系统：插件进程的窗口需要作为主程序窗口的子窗口
- 辅助工具窗口：某个工具窗口需要始终跟随主窗口

为此，Wayland 提供了 `xdg-foreign-unstable-v2` 协议，允许一个进程"导出"自己的窗口句柄，另一个进程"导入"该句柄并建立父子关系。

### 协议概述

xdg-foreign 协议提供两个核心接口：

1. **zxdg_exporter_v2** - 用于导出窗口句柄
2. **zxdg_importer_v2** - 用于导入窗口句柄

```
<interface name="zxdg_exporter_v2" version="1">
  <request name="destroy" type="destructor"/>
  <request name="export_toplevel">
    <arg name="id" type="new_id" interface="zxdg_exported_v2"/>
    <arg name="surface" type="object" interface="wl_surface"/>
  </request>
</interface>

<interface name="zxdg_importer_v2" version="1">
  <request name="destroy" type="destructor"/>
  <request name="import_toplevel">
    <arg name="id" type="new_id" interface="zxdg_imported_v2"/>
    <arg name="handle" type="string"/>
  </request>
</interface>
```

### 导出窗口句柄

父进程需要完成以下步骤：

1. 从 `wl_registry` 获取 `zxdg_exporter_v2` 全局对象
2. 创建并映射窗口
3. 调用 `zxdg_exporter_v2_export_toplevel` 导出窗口
4. 监听 `zxdg_exported_v2` 的 `handle` 事件获取导出句柄
5. 通过进程间通信（如命令行参数、管道、文件等）将句柄传递给子进程

```c
// 监听导出句柄
static void handle_exported_handle(void *data, struct zxdg_exported_v2 *exported, const char *handle) {
    struct app_state *state = data;
    state->exported_handle = strdup(handle);
    printf("窗口导出成功！句柄: %s\n", handle);
    printf("请运行子进程: ./wayland_child %s\n", handle);
}

static const struct zxdg_exported_v2_listener exported_listener = {
    .handle = handle_exported_handle,
};

// 导出窗口
struct zxdg_exported_v2 *exported = zxdg_exporter_v2_export_toplevel(state.exporter, state.surface);
zxdg_exported_v2_add_listener(exported, &exported_listener, &state);
```

### 导入窗口句柄

子进程需要完成以下步骤：

1. 从 `wl_registry` 获取 `zxdg_importer_v2` 全局对象
2. 接收父进程传递的句柄
3. 调用 `zxdg_importer_v2_import_toplevel` 导入句柄
4. 调用 `zxdg_imported_v2_set_parent_of` 设置父子关系
5. 监听 `zxdg_imported_v2` 的 `destroyed` 事件，处理父窗口被关闭的情况

```c
// 监听父窗口销毁事件
static void handle_imported_destroyed(void *data, struct zxdg_imported_v2 *imported) {
    printf("父窗口已被销毁，当前的导入句柄已失效。\n");
    zxdg_imported_v2_destroy(imported);
    exit(0);
}

static const struct zxdg_imported_v2_listener imported_listener = {
    .destroyed = handle_imported_destroyed,
};

// 导入句柄并设置父子关系
struct zxdg_imported_v2 *imported = zxdg_importer_v2_import_toplevel(state.importer, parent_handle);
zxdg_imported_v2_add_listener(imported, &imported_listener, &state);
zxdg_imported_v2_set_parent_of(imported, state.surface);
```

### 完整示例

本节示例包含两个程序：

- **wayland_parent** - 父进程，导出窗口句柄并等待子进程连接
- **wayland_child** - 子进程，接收句柄并设置窗口父子关系

运行方式：

```bash
# 终端1：运行父进程
cd code/ch05/sample5-3
make
./wayland_parent

# 程序会输出类似以下内容：
# >>> 窗口导出成功！
# 请在另一个终端运行子进程:
# ./wayland_child <handle_string>

# 终端2：运行子进程（使用父进程输出的句柄）
./wayland_child <handle_string>
```

运行后，子窗口会作为父窗口的子窗口显示，父窗口关闭时子窗口也会收到 `destroyed` 事件。

### 安全机制

xdg-foreign 协议设计时考虑了安全性：

1. **句柄不透明** - 导出的句柄是一个不透明的字符串，客户端无法从中推断出其他信息
2. **句柄有效期** - 句柄仅在当前 Wayland 会话有效，无法跨会话使用
3. **销毁通知** - 当父窗口被销毁时，合成器会通知所有导入该窗口的客户端

### 注意事项

1. xdg-foreign 协议目前仍然是 unstable 版本（v2），接口可能在未来版本中变化
2. 并非所有合成器都支持该协议，使用前应检查 `zxdg_exporter_v2` 和 `zxdg_importer_v2` 是否存在
3. 父子关系的语义由合成器决定，不同合成器可能有不同的行为

完整代码请参考 `code/ch05/sample5-3` 目录。
