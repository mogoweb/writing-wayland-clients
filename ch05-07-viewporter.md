## Surface 裁剪与缩放：viewporter 协议

默认情况下，Wayland surface 的尺寸与附加的 buffer 尺寸相同。但在某些场景下，我们可能需要：

- **裁剪（cropping）**：只显示 buffer 的一部分区域
- **缩放（scaling）**：将 buffer 缩放到不同的尺寸显示

`viewporter` 协议（全名 `wp_viewporter`）提供了这种能力，使得 buffer 尺寸和 surface 尺寸可以解耦。

### 协议概述

viewporter 协议引入了两个核心接口：

1. **wp_viewporter** - 全局对象，用于创建 viewport
2. **wp_viewport** - 与 surface 关联的对象，用于设置裁剪和缩放参数

```
<interface name="wp_viewporter" version="1">
  <request name="destroy" type="destructor"/>
  <request name="get_viewport">
    <arg name="id" type="new_id" interface="wp_viewport"/>
    <arg name="surface" type="object" interface="wl_surface"/>
  </request>
</interface>

<interface name="wp_viewport" version="1">
  <request name="destroy" type="destructor"/>
  <request name="set_source">
    <arg name="x" type="fixed"/>
    <arg name="y" type="fixed"/>
    <arg name="width" type="fixed"/>
    <arg name="height" type="fixed"/>
  </request>
  <request name="set_destination">
    <arg name="width" type="int"/>
    <arg name="height" type="int"/>
  </request>
</interface>
```

### 源矩形与目标尺寸

viewport 的工作原理基于两个概念：

1. **源矩形（source rectangle）**：从 buffer 中裁剪的区域
   - 通过 `set_source(x, y, width, height)` 设置
   - 坐标单位是像素，支持浮点数

2. **目标尺寸（destination size）**：surface 最终显示的尺寸
   - 通过 `set_destination(width, height)` 设置
   - 单位是 surface 坐标

当两者都设置后，合成器会将源矩形的内容缩放到目标尺寸进行显示。

### 使用方法

首先，从 registry 获取 `wp_viewporter` 全局对象：

```c
static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface, uint32_t version) {
    // ... 其他绑定 ...
    if (strcmp(interface, wp_viewporter_interface.name) == 0) {
        state->viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
    }
}
```

然后为需要操作的 surface 创建 viewport：

```c
// 为 surface 创建 viewport
struct wp_viewport *viewport = wp_viewporter_get_viewport(viewporter, surface);
```

设置裁剪区域和目标尺寸：

```c
// 设置源矩形：从 (100, 100) 开始，取 200x200 的区域
wp_viewport_set_source(viewport,
    wl_fixed_from_double(100.0),  // x
    wl_fixed_from_double(100.0),  // y
    wl_fixed_from_double(200.0),  // width
    wl_fixed_from_double(200.0)   // height
);

// 设置目标尺寸：缩放到 300x300
wp_viewport_set_destination(viewport, 300, 300);
```

### 坐标变换顺序

当同时使用 buffer transform、buffer scale 和 viewport 时，坐标变换的顺序为：

1. **buffer_transform** - `wl_surface.set_buffer_transform`
2. **buffer_scale** - `wl_surface.set_buffer_scale`
3. **crop and scale** - `wp_viewport.set_source/set_destination`

这意味着 viewport 的源矩形坐标是在应用了 transform 和 scale 之后的坐标系中。

### 使用场景

#### 1. 裁剪显示

只显示 buffer 的中心区域：

```c
// buffer 尺寸: 400x400
// 只显示中心 200x200
wp_viewport_set_source(viewport,
    wl_fixed_from_double(100.0),
    wl_fixed_from_double(100.0),
    wl_fixed_from_double(200.0),
    wl_fixed_from_double(200.0)
);
// 不设置 destination，surface 尺寸就是 200x200
wp_viewport_set_destination(viewport, -1, -1);  // -1 表示取消设置
```

#### 2. 缩放显示

将 buffer 放大或缩小：

```c
// buffer 尺寸: 200x200
// 放大到 400x400 显示
wp_viewport_set_source(viewport,
    wl_fixed_from_double(0.0),
    wl_fixed_from_double(0.0),
    wl_fixed_from_double(200.0),
    wl_fixed_from_double(200.0)
);
wp_viewport_set_destination(viewport, 400, 400);
```

#### 3. 裁剪并缩放

```c
// buffer 尺寸: 400x400
// 裁剪中心 200x200，然后放大到 300x300
wp_viewport_set_source(viewport,
    wl_fixed_from_double(100.0),
    wl_fixed_from_double(100.0),
    wl_fixed_from_double(200.0),
    wl_fixed_from_double(200.0)
);
wp_viewport_set_destination(viewport, 300, 300);
```

### 清除设置

要取消 viewport 设置，可以传入特殊值：

```c
// 取消源矩形设置（使用完整的 buffer）
wp_viewport_set_source(viewport,
    wl_fixed_from_double(-1.0),  // 所有值都为 -1 表示取消
    wl_fixed_from_double(-1.0),
    wl_fixed_from_double(-1.0),
    wl_fixed_from_double(-1.0)
);

// 取消目标尺寸设置
wp_viewport_set_destination(viewport, -1, -1);
```

### 错误处理

viewport 可能产生以下协议错误：

| 错误 | 原因 |
|------|------|
| `bad_value` | 源矩形坐标为负，或宽高为零/负 |
| `bad_size` | 仅设置源矩形但未设置目标尺寸，且源矩形宽高不是整数 |
| `out_of_buffer` | 源矩形超出 buffer 边界 |
| `no_surface` | 关联的 surface 已被销毁 |

### 注意事项

1. **双缓冲状态**：viewport 的设置是双缓冲的，需要在 `wl_surface.commit` 后才生效

2. **销毁 viewport**：销毁 viewport 对象会移除 surface 上的裁剪和缩放设置

3. **协议支持**：使用前应检查合成器是否支持 `wp_viewporter`

4. **性能考虑**：频繁更改 viewport 设置可能导致额外的渲染开销

### 完整示例

本节示例创建一个 400x400 的棋盘格 buffer，然后裁剪中心 200x200 区域，缩放到 300x300 显示。

```c
// buffer 尺寸: 400x400 (棋盘格)
struct wl_buffer *buffer = create_checkerboard_buffer(shm, 400, 400);
wl_surface_attach(surface, buffer, 0, 0);

// 使用 viewporter 裁剪和缩放
if (viewporter) {
    struct wp_viewport *viewport = wp_viewporter_get_viewport(viewporter, surface);

    // 源矩形: 从 (100,100) 开始，200x200
    wp_viewport_set_source(viewport,
        wl_fixed_from_double(100.0),
        wl_fixed_from_double(100.0),
        wl_fixed_from_double(200.0),
        wl_fixed_from_double(200.0)
    );

    // 目标尺寸: 300x300
    wp_viewport_set_destination(viewport, 300, 300);
}

wl_surface_commit(surface);
```

完整代码请参考 `code/ch05/sample5-7` 目录。
