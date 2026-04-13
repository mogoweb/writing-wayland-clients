## 窗口状态监控：foreign-toplevel-manager 协议

在某些场景下，应用程序需要了解系统中其他窗口的状态，例如：

- 任务栏/ Dock 应用：需要显示所有窗口的列表
- 窗口切换器：显示当前打开的窗口供用户选择
- 窗口管理工具：监控和管理窗口状态

Wayland 默认不允许客户端获取其他窗口的信息，但通过扩展协议，合成器可以向可信客户端暴露窗口状态信息。`foreign-toplevel-manager` 协议（如 treeland 合成器中的 `treeland_foreign_toplevel_manager_v1`）就是这样一个例子。

### 协议概述

foreign-toplevel-manager 协议提供以下功能：

1. 枚举系统中所有顶层窗口（toplevel）
2. 接收窗口状态变化通知（标题、App ID、最大化、最小化等）
3. 接收窗口创建和销毁通知

```
<interface name="treeland_foreign_toplevel_manager_v1" version="1">
  <event name="toplevel">
    <arg name="toplevel" type="new_id" interface="treeland_foreign_toplevel_handle_v1"/>
  </event>
  <event name="finished"/>
</interface>

<interface name="treeland_foreign_toplevel_handle_v1" version="1">
  <event name="title"/>
  <event name="app_id"/>
  <event name="pid"/>
  <event name="state"/>
  <event name="closed"/>
  <event name="done"/>
  <!-- ... 更多事件 -->
</interface>
```

### 获取窗口管理器

首先从 registry 绑定窗口管理器全局对象：

```c
static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t name, const char *interface, uint32_t version) {
    // ... 其他绑定 ...
    if (strcmp(interface, treeland_foreign_toplevel_manager_v1_interface.name) == 0) {
        state->foreign_toplevel_manager = wl_registry_bind(
            registry, name, &treeland_foreign_toplevel_manager_v1_interface, 1);

        treeland_foreign_toplevel_manager_v1_add_listener(
            state->foreign_toplevel_manager,
            &foreign_toplevel_manager_listener,
            state
        );
    }
}
```

### 监听窗口事件

注册监听器以接收窗口通知：

```c
// 当发现新窗口时触发
static void foreign_toplevel_manager_handle_toplevel(void *data,
             struct treeland_foreign_toplevel_manager_v1 *manager,
             struct treeland_foreign_toplevel_handle_v1 *handle) {
    struct state *state = data;
    printf("发现新窗口！句柄: %p\n", (void *)handle);

    // 创建窗口信息结构
    struct foreign_window *win = calloc(1, sizeof(struct foreign_window));
    win->handle = handle;
    win->app_state = state;

    // 添加到窗口列表
    wl_list_insert(&state->toplevel_list, &win->link);

    // 为窗口句柄添加监听器
    treeland_foreign_toplevel_handle_v1_add_listener(handle, &handle_listener, win);
}

static const struct treeland_foreign_toplevel_manager_v1_listener foreign_toplevel_manager_listener = {
    .toplevel = foreign_toplevel_manager_handle_toplevel,
    .finished = foreign_toplevel_manager_handle_finished,
};
```

### 处理窗口状态变化

每个窗口句柄会产生多个事件，用于同步窗口状态：

```c
// 窗口标题
static void handle_title(void *data, struct treeland_foreign_toplevel_handle_v1 *handle,
                         const char *title) {
    struct foreign_window *win = data;
    free(win->title);
    win->title = strdup(title);
    printf("[Window %p] Title: %s\n", (void*)handle, title);
}

// 应用 ID
static void handle_app_id(void *data, struct treeland_foreign_toplevel_handle_v1 *handle,
                          const char *app_id) {
    struct foreign_window *win = data;
    free(win->app_id);
    win->app_id = strdup(app_id);
    printf("[Window %p] App ID: %s\n", (void*)handle, app_id);
}

// 进程 ID
static void handle_pid(void *data, struct treeland_foreign_toplevel_handle_v1 *handle,
                       uint32_t pid) {
    struct foreign_window *win = data;
    win->pid = pid;
    printf("[Window %p] PID: %u\n", (void*)handle, pid);

    // 可以通过 PID 识别自己创建的窗口
    if (pid == getpid()) {
        printf("这是我自己创建的窗口！\n");
        win->app_state->my_toplevel_handle = handle;
    }
}

// 窗口状态（最大化、最小化、激活等）
static void handle_state(void *data, struct treeland_foreign_toplevel_handle_v1 *handle,
                         struct wl_array *states) {
    struct foreign_window *win = data;
    win->is_maximized = 0;
    win->is_minimized = 0;
    win->is_activated = 0;
    win->is_fullscreen = 0;

    uint32_t *state;
    wl_array_for_each(state, states) {
        switch (*state) {
            case TREELAND_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED:
                win->is_maximized = 1;
                break;
            case TREELAND_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED:
                win->is_minimized = 1;
                break;
            case TREELAND_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED:
                win->is_activated = 1;
                break;
            case TREELAND_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN:
                win->is_fullscreen = 1;
                break;
        }
    }
}

// 状态同步完成
static void handle_done(void *data, struct treeland_foreign_toplevel_handle_v1 *handle) {
    printf("[Window %p] 所有属性已同步。\n", (void*)handle);
    // 此时可以更新 UI
}

// 窗口关闭
static void handle_closed(void *data, struct treeland_foreign_toplevel_handle_v1 *handle) {
    struct foreign_window *win = data;
    printf("[Window %p] 已关闭。\n", (void*)handle);

    wl_list_remove(&win->link);
    free(win->title);
    free(win->app_id);
    treeland_foreign_toplevel_handle_v1_destroy(win->handle);
    free(win);
}
```

### 监听器结构体组装

```c
static const struct treeland_foreign_toplevel_handle_v1_listener handle_listener = {
    .pid = handle_pid,
    .title = handle_title,
    .app_id = handle_app_id,
    .identifier = handle_identifier,
    .output_enter = handle_output_enter,
    .output_leave = handle_output_leave,
    .state = handle_state,
    .done = handle_done,
    .closed = handle_closed,
    .parent = handle_parent,
};
```

### 使用场景

1. **任务栏/Dock 应用**
   - 监听所有窗口的标题和图标
   - 显示窗口列表
   - 点击时激活对应窗口

2. **窗口切换器**
   - 获取所有窗口及其缩略图
   - 提供窗口切换界面

3. **窗口管理工具**
   - 监控窗口状态变化
   - 实现自定义窗口管理逻辑

### 注意事项

1. **协议特异性**：`treeland_foreign_toplevel_manager_v1` 是 treeland 合成器特有的协议，其他合成器可能使用不同的协议（如 wlr-foreign-toplevel-management）

2. **权限要求**：合成器可能限制只有特定的可信客户端才能使用该协议

3. **性能考虑**：频繁的窗口状态更新可能带来性能开销，应合理处理

4. **资源管理**：及时销毁不再需要的窗口句柄，避免内存泄漏

### 与标准协议的关系

foreign-toplevel-manager 是合成器扩展协议，不是 Wayland 核心协议的一部分。不同合成器可能提供不同的实现：

| 合成器 | 协议名称 |
|--------|---------|
| treeland | treeland_foreign_toplevel_manager_v1 |
| sway/wlroots | wlr-foreign-toplevel-management-unstable-v1 |
| KDE | kde-plasma-window-management |

完整代码请参考 `code/ch06/sample6-3` 目录。
