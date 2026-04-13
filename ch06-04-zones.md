## 区域管理：xx-zones 协议

在多显示器环境或需要精确控制窗口位置的场景中，应用程序可能需要：

- 获取屏幕区域信息
- 跨进程共享窗口位置
- 实现窗口的精确定位

`xx-zones` 协议（treeland 合成器特有）提供了一种机制，允许客户端创建"区域"（zone），并通过区域句柄在进程间共享窗口位置信息。

### 协议概述

xx-zones 协议引入了以下接口：

1. **xx_zone_manager_v1** - 区域管理器，用于创建区域
2. **xx_zone_v1** - 区域对象，包含尺寸和句柄信息
3. **xx_zone_item_v1** - 区域项，用于管理窗口位置

```
<interface name="xx_zone_manager_v1" version="1">
  <request name="destroy" type="destructor"/>
  <request name="get_zone">
    <arg name="id" type="new_id" interface="xx_zone_v1"/>
    <arg name="output" type="object" interface="wl_output"/>
  </request>
</interface>

<interface name="xx_zone_v1" version="1">
  <event name="size"/>
  <event name="handle"/>
  <event name="done"/>
  <request name="destroy" type="destructor"/>
</interface>

<interface name="xx_zone_item_v1" version="1">
  <request name="set_position"/>
  <event name="position"/>
  <event name="position_failed"/>
</interface>
```

### 获取区域管理器

首先从 registry 绑定区域管理器：

```c
static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t name, const char *interface, uint32_t version) {
    // ... 其他绑定 ...
    if (strcmp(interface, "xx_zone_manager_v1") == 0) {
        state->xx_zone_manager = wl_registry_bind(
            registry, name, &xx_zone_manager_v1_interface, 1);
    }
}
```

### 创建区域

为指定的输出设备创建区域：

```c
if (state.xx_zone_manager) {
    printf("Zone manager found.\n");

    // 为 wl_output 创建区域
    state.xx_zone = xx_zone_manager_v1_get_zone(state.xx_zone_manager, state.output);
    xx_zone_v1_add_listener(state.xx_zone, &zone_listener, NULL);
}
```

### 监听区域事件

区域对象会产生以下事件：

```c
// 区域尺寸
static void zone_size(void *data, struct xx_zone_v1 *xx_zone, int32_t width, int32_t height) {
    printf("Zone size: %dx%d\n", width, height);
}

// 区域句柄（用于跨进程共享）
static void zone_handle(void *data, struct xx_zone_v1 *xx_zone, const char *handle) {
    printf("Zone handle: %s\n", handle);

    // 将句柄保存到文件，供其他进程使用
    FILE *fp = fopen("./handle.id", "w");
    if (fp) {
        fwrite(handle, 1, strlen(handle), fp);
        fclose(fp);
    }
}

// 区域信息同步完成
static void zone_done(void *data, struct xx_zone_v1 *xx_zone) {
    printf("Zone info synced.\n");
}

// 区域项事件
static void zone_item_entered(void *data, struct xx_zone_v1 *xx_zone,
                              struct xx_zone_item_v1 *item) {
    printf("Zone item entered.\n");
}

static void zone_item_left(void *data, struct xx_zone_v1 *xx_zone,
                           struct xx_zone_item_v1 *item) {
    printf("Zone item left.\n");
}

static const struct xx_zone_v1_listener zone_listener = {
    .size = zone_size,
    .handle = zone_handle,
    .done = zone_done,
    .item_blocked = zone_item_blocked,
    .item_entered = zone_item_entered,
    .item_left = zone_item_left,
};
```

### 使用场景

#### 1. 跨进程窗口定位

一个进程创建区域并导出句柄，另一个进程使用句柄将窗口定位到指定位置：

**客户端 A（创建区域）：**

```c
// 创建区域
state.xx_zone = xx_zone_manager_v1_get_zone(state.xx_zone_manager, state.output);
xx_zone_v1_add_listener(state.xx_zone, &zone_listener, NULL);

// 在 zone_handle 回调中保存句柄到文件
```

**客户端 B（使用区域）：**

```c
// 读取区域句柄
char handle[256];
FILE *fp = fopen("./handle.id", "r");
fgets(handle, sizeof(handle), fp);
fclose(fp);

// 使用句柄创建区域项并设置位置
// (具体 API 取决于协议实现)
```

#### 2. 多显示器窗口管理

在多显示器环境中，区域可用于：

- 确定窗口显示在哪个显示器
- 实现窗口在不同显示器间的移动
- 获取显示器的工作区域（排除任务栏等）

### 区域项（Zone Item）

区域项是与区域关联的窗口或 surface：

```c
// 区域项事件监听
static void zone_item_position(void *data, struct xx_zone_item_v1 *item,
                               int32_t x, int32_t y) {
    printf("Zone item position: (%d, %d)\n", x, y);
}

static void zone_item_position_failed(void *data, struct xx_zone_item_v1 *item) {
    printf("Zone item position request failed.\n");
}

static void zone_item_closed(void *data, struct xx_zone_item_v1 *item) {
    printf("Zone item closed.\n");
}

static const struct xx_zone_item_v1_listener zone_item_listener = {
    .frame_extents = zone_item_frame_extents,
    .position = zone_item_position,
    .position_failed = zone_item_position_failed,
    .closed = zone_item_closed,
};
```

### 完整示例

本节示例包含两个客户端程序：

- **client_a** - 创建区域并导出句柄
- **client_b** - 使用区域句柄定位窗口

运行方式：

```bash
# 终端1：运行 client_a
cd code/ch06/sample6-4
make
./client_a

# client_a 会输出区域句柄并保存到 handle.id 文件

# 终端2：运行 client_b
./client_b
```

### 注意事项

1. **协议特异性**：`xx-zones` 是 treeland 合成器特有的协议，其他合成器不支持

2. **安全性**：区域句柄应妥善保管，避免被恶意程序利用

3. **生命周期**：区域在创建它的客户端断开连接后可能失效

4. **输出设备**：区域与 `wl_output` 关联，显示器配置变化时区域信息可能更新

### 与其他协议的对比

| 特性 | xx-zones | xdg-foreign | xdg_output |
|------|----------|-------------|------------|
| 主要用途 | 窗口定位 | 跨进程窗口关系 | 输出设备信息 |
| 信息共享 | 区域句柄 | 窗口句柄 | 输出设备属性 |
| 跨进程 | 支持 | 支持 | 不需要 |

完整代码请参考 `code/ch06/sample6-4` 目录。
