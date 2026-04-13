## 窗口激活：xdg_activation_v1 协议

在多窗口应用中，经常需要从一个窗口"激活"另一个窗口，使其获得焦点并显示在最前面。例如：

- 点击工具窗口的按钮后，激活主窗口
- 点击通知后，激活对应的应用窗口
- 从托盘图标激活应用主窗口

在传统的 X11 中，应用程序可以自由地激活任意窗口。但这种自由带来了安全问题：恶意程序可以不断抢夺焦点，干扰用户操作。

Wayland 出于安全考虑，默认不允许客户端随意激活窗口。`xdg_activation_v1` 协议提供了一种安全机制，允许在特定条件下请求窗口激活。

### 协议概述

`xdg_activation_v1` 协议的核心概念是 **激活令牌（activation token）**：

1. 客户端先请求一个令牌
2. 合成器将令牌与当前的"用户意图"绑定（如鼠标点击事件）
3. 客户端使用令牌请求激活目标窗口
4. 合成器验证令牌有效性后执行激活

```
<interface name="xdg_activation_v1" version="1">
  <request name="destroy" type="destructor"/>

  <request name="get_activation_token">
    <arg name="id" type="new_id" interface="xdg_activation_token_v1"/>
  </request>

  <request name="activate">
    <arg name="token" type="string"/>
    <arg name="surface" type="object" interface="wl_surface"/>
  </request>
</interface>
```

### 激活令牌

激活令牌通过 `xdg_activation_token_v1` 接口创建和配置：

```c
// 创建令牌对象
struct xdg_activation_token_v1 *token = xdg_activation_v1_get_activation_token(activation);

// 设置令牌属性
xdg_activation_token_v1_set_surface(token, source_surface);  // 激活来源的 surface
xdg_activation_token_v1_set_serial(token, serial, seat);     // 关联的输入事件序列号

// 请求合成器生成令牌字符串
xdg_activation_token_v1_commit(token);
```

令牌请求后，合成器会通过 `done` 事件返回令牌字符串：

```c
static void token_done(void *data, struct xdg_activation_token_v1 *token, const char *token_str) {
    char **out = data;
    *out = strdup(token_str);
    xdg_activation_token_v1_destroy(token);
}

static const struct xdg_activation_token_v1_listener token_listener = {
    .done = token_done,
};
```

### 使用令牌激活窗口

获得令牌后，可以请求激活目标窗口：

```c
// 使用令牌激活目标窗口
xdg_activation_v1_activate(activation, token_string, target_surface);
```

### 完整示例

以下示例创建两个窗口：主窗口（绿色）和工具窗口（灰色）。点击工具窗口中的红色按钮后，激活主窗口。

```c
// 鼠标点击处理
static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial,
                           uint32_t time, uint32_t button, uint32_t state_action) {
    struct AppState *app = data;

    if (state_action == WL_POINTER_BUTTON_STATE_PRESSED && button == BTN_LEFT) {
        // 检查是否点击了工具窗口中的按钮区域
        if (app->pointer_surface == app->tool_window->surface &&
            app->pointer_x >= 175 && app->pointer_x <= 225 &&
            app->pointer_y >= 125 && app->pointer_y <= 175) {

            printf("Button clicked! Requesting activation of main window...\n");

            // 1. 创建激活令牌
            struct xdg_activation_token_v1 *token =
                xdg_activation_v1_get_activation_token(app->activation);

            // 2. 设置令牌属性
            xdg_activation_token_v1_set_surface(token, app->tool_window->surface);
            xdg_activation_token_v1_set_serial(token, serial, app->seat);

            // 3. 添加监听器并提交请求
            char *token_str = NULL;
            xdg_activation_token_v1_add_listener(token, &token_listener, &token_str);
            xdg_activation_token_v1_commit(token);

            // 4. 等待令牌生成
            wl_display_roundtrip(app->display);

            // 5. 使用令牌激活主窗口
            if (token_str) {
                xdg_activation_v1_activate(app->activation, token_str, app->main_window->surface);
                free(token_str);
            }
        }
    }
}
```

### 安全机制

`xdg_activation_v1` 的安全机制体现在：

1. **令牌与用户意图绑定**：令牌必须与实际的输入事件关联，防止程序"伪造"用户行为

2. **令牌有时效性**：令牌在生成后一段时间内有效，过期后无法使用

3. **令牌与来源窗口绑定**：合成器可以验证令牌是否来自合法的窗口

4. **合成器有权拒绝**：即使令牌有效，合成器也可以根据当前策略拒绝激活请求

### 典型使用场景

| 场景 | 令牌来源 | 激活目标 |
|------|---------|---------|
| 工具窗口按钮 | 工具窗口 | 主窗口 |
| 通知点击 | 通知 surface | 应用窗口 |
| D-Bus 激活 | 无（需要特殊处理） | 应用窗口 |
| 拖放操作 | 拖放源窗口 | 拖放目标窗口 |

### 注意事项

1. **检查协议支持**：使用前检查 `xdg_activation_v1` 全局对象是否存在

2. **序列号必须有效**：`set_serial` 使用的序列号必须来自真实的输入事件

3. **令牌是一次性的**：每个令牌只能使用一次

4. **合成器差异**：不同合成器对激活请求的处理可能不同

完整代码请参考 `code/ch05/sample5-6` 目录。
