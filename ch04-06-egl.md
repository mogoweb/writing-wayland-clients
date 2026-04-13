## EGL 渲染

前面的示例都是使用共享内存（SHM）作为渲染后端，这种方式简单易用，但对于需要高性能图形渲染的应用（如游戏、视频播放器、3D 应用）来说，使用 GPU 加速是更好的选择。

EGL（Embedded-System Graphics Library）是 Khronos 制定的接口，用于管理图形上下文、surface 和渲染 API（如 OpenGL ES）。在 Wayland 中，EGL 是连接 GPU 渲染与 Wayland surface 的桥梁。

### Wayland EGL 架构

```
┌─────────────────────────────────────────────┐
│              Application                     │
├─────────────────────────────────────────────┤
│         OpenGL ES / OpenGL API               │
├─────────────────────────────────────────────┤
│              EGL API                         │
├─────────────────────────────────────────────┤
│     Wayland EGL Platform (wayland-egl)       │
├─────────────────────────────────────────────┤
│    wl_surface / wl_display (Wayland)         │
└─────────────────────────────────────────────┘
```

### 关键组件

1. **wl_egl_window** - Wayland EGL 窗口，将 `wl_surface` 包装为 EGL 可用的原生窗口
2. **EGLDisplay** - EGL 显示连接，对应 Wayland 的 `wl_display`
3. **EGLSurface** - EGL 绘图表面，对应 Wayland surface
4. **EGLContext** - OpenGL ES 渲染上下文

### 初始化 EGL

首先需要安装 EGL 和 OpenGL ES 开发包：

```bash
sudo apt install libegl-dev libgles2-dev
```

初始化 EGL 的步骤：

```c
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

EGLDisplay egl_display;
EGLSurface egl_surface;
EGLContext egl_context;
struct wl_egl_window *egl_window;

static void init_egl(struct wl_display *display, struct wl_surface *surface) {
    // 1. 获取 EGL Display
    egl_display = eglGetDisplay((EGLNativeDisplayType)display);
    if (egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "Can't get EGL display\n");
        exit(1);
    }

    // 2. 初始化 EGL
    EGLint major, minor;
    if (eglInitialize(egl_display, &major, &minor) != EGL_TRUE) {
        fprintf(stderr, "Can't initialize EGL\n");
        exit(1);
    }
    printf("EGL version: %d.%d\n", major, minor);

    // 3. 选择 EGL 配置
    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    EGLConfig config;
    EGLint num_configs;
    eglChooseConfig(egl_display, config_attribs, &config, 1, &num_configs);

    // 4. 创建 EGL Context
    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,  // OpenGL ES 2.0
        EGL_NONE
    };
    egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT, context_attribs);

    // 5. 创建 wl_egl_window
    egl_window = wl_egl_window_create(surface, 480, 360);

    // 6. 创建 EGL Surface
    egl_surface = eglCreateWindowSurface(egl_display, config, egl_window, NULL);

    // 7. 绑定上下文
    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
}
```

### 渲染循环

使用 OpenGL ES 渲染并交换缓冲区：

```c
static void render_frame(void) {
    // 设置视口
    glViewport(0, 0, 480, 360);

    // 清除颜色缓冲区
    glClearColor(1.0f, 1.0f, 0.0f, 1.0f);  // 黄色背景
    glClear(GL_COLOR_BUFFER_BIT);

    // 绘制 OpenGL 内容
    // ... 你的绘制代码 ...

    // 交换缓冲区
    eglSwapBuffers(egl_display, egl_surface);
}
```

### 处理窗口大小变化

当窗口大小改变时，需要调整 EGL window：

```c
static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                                   int32_t width, int32_t height, struct wl_array *states) {
    if (width > 0 && height > 0 && egl_window) {
        wl_egl_window_resize(egl_window, width, height, 0, 0);
        glViewport(0, 0, width, height);
    }
}
```

### 完整示例

以下是一个使用 EGL 和 OpenGL ES 渲染黄色背景的完整示例：

```c
#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include "xdg-shell.h"

// ... 全局变量和监听器定义 ...

int main(int argc, char **argv) {
    // 1. 连接 Wayland 显示服务器
    display = wl_display_connect(NULL);

    // 2. 获取全局对象
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);

    // 3. 创建 surface
    surface = wl_compositor_create_surface(compositor);

    // 4. 设置 XDG Shell 角色
    xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface);
    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
    xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
    xdg_toplevel_set_title(xdg_toplevel, "Wayland EGL App");
    wl_surface_commit(surface);
    wl_display_roundtrip(display);  // 等待 configure

    // 5. 初始化 EGL
    init_egl(display, surface);

    // 6. 渲染第一帧
    render_frame();

    // 7. 主事件循环
    while (wl_display_dispatch(display) != -1) {
        // 可以在这里添加帧回调以实现动画
    }

    // 8. 清理资源
    eglDestroyContext(egl_display, egl_context);
    eglDestroySurface(egl_display, egl_surface);
    eglTerminate(egl_display);
    wl_egl_window_destroy(egl_window);

    wl_display_disconnect(display);
    return 0;
}
```

### 与 SHM 的对比

| 特性 | SHM（共享内存） | EGL |
|------|----------------|-----|
| 性能 | 较低（CPU 渲染） | 高（GPU 渲染） |
| 适用场景 | 简单 2D 应用 | 游戏、视频、3D 应用 |
| 实现复杂度 | 简单 | 较复杂 |
| 依赖 | libwayland-client | libwayland-client, libEGL, libGLES |
| 合成器优化 | 需要 CPU 拷贝 | 可直接使用 GPU 纹理 |

### 编译说明

编译 EGL 应用需要链接额外的库：

```makefile
runme: main.c xdg-shell.h xdg-shell.c
	gcc main.c xdg-shell.c -lwayland-client -lwayland-egl -lEGL -lGLESv2 -o runme
```

### 注意事项

1. **先 commit 后渲染**：XDG Shell 要求先发送初始 `wl_surface_commit`，收到 `configure` 事件后才能开始渲染

2. **帧回调**：对于动画应用，应使用 `wl_surface.frame` 回调来同步渲染与显示器刷新

3. **EGL 扩展**：检查 `EGL_WL_bind_wayland_display` 等扩展以支持高级特性

4. **缓冲区年龄**：使用 `eglQuerySurface` 查询 `EGL_BUFFER_AGE` 以实现高效的局部更新

完整代码请参考 `code/ch04/sample4-6` 目录。
