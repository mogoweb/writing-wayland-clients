#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include "xdg-shell.h" // 替换 wl_shell 为现代的 xdg-shell

struct wl_display *display = NULL;
struct wl_compositor *compositor = NULL;
struct wl_surface *surface;
struct wl_egl_window *egl_window;
struct wl_region *region;

// 替换 wl_shell 相关的全局变量
struct xdg_wm_base *wm_base = NULL;
struct xdg_surface *xdg_surface;
struct xdg_toplevel *xdg_toplevel;

EGLDisplay egl_display;
EGLConfig egl_conf;
EGLSurface egl_surface;
EGLContext egl_context;

/* XDG Shell 的 Ping-Pong 机制：混成器用来检查程序是否卡死 */
static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

/* 响应 XDG Surface 配置事件（必须回应 Ack） */
static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

/* 响应窗口大小变化及关闭请求 */
static void xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel,
                                   int32_t width, int32_t height, struct wl_array *states) {
    // 可以在这里改变 egl_window 的大小：wl_egl_window_resize(...)
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    exit(0);
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
};

/* 注册表监听器 */
static void
global_registry_handler(void *data, struct wl_registry *registry, uint32_t id,
                        const char *interface, uint32_t version)
{
    printf("Got a registry event for %s id %d\n", interface, id);
    if (strcmp(interface, "wl_compositor") == 0) {
        compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 1);
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        // 绑定现代的 xdg_wm_base
        wm_base = wl_registry_bind(registry, id, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(wm_base, &xdg_wm_base_listener, NULL);
    }
}

static void
global_registry_remover(void *data, struct wl_registry *registry, uint32_t id)
{
    printf("Got a registry losing event for %d\n", id);
}

static const struct wl_registry_listener registry_listener = {
    global_registry_handler,
    global_registry_remover
};

static void
create_opaque_region() {
    region = wl_compositor_create_region(compositor);
    wl_region_add(region, 0, 0, 480, 360);
    wl_surface_set_opaque_region(surface, region);
    
    // 设置完不透明区域后，应当释放这个 region 句柄，避免内存泄漏
    wl_region_destroy(region);
}

static void
create_window() {
    egl_window = wl_egl_window_create(surface, 480, 360);
    if (egl_window == NULL) { // 修正: wl_egl_window 失败应判断 NULL，而不是 EGL_NO_SURFACE
        fprintf(stderr, "Can't create egl window\n");
        exit(1);
    } else {
        fprintf(stderr, "Created egl window\n");
    }

    egl_surface = eglCreateWindowSurface(egl_display, egl_conf, egl_window, NULL);

    if (eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
        fprintf(stderr, "Made current\n");
    } else {
        fprintf(stderr, "Made current failed\n");
    }

    // 取消了注释：在 Wayland 下如果不渲染内容直接交换缓冲区，可能导致窗口不更新或者花屏
    glClearColor(1.0, 1.0, 0.0, 1.0); // 黄色背景
    glClear(GL_COLOR_BUFFER_BIT);
    glFlush();

    if (eglSwapBuffers(egl_display, egl_surface)) {
        fprintf(stderr, "Swapped buffers\n");
    } else {
        fprintf(stderr, "Swapped buffers failed\n");
    }
}

static void
init_egl() {
    EGLint major, minor, count, n, size;
    EGLConfig *configs;
    int i;
    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    static const EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    egl_display = eglGetDisplay((EGLNativeDisplayType) display);
    if (egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "Can't create egl display\n");
        exit(1);
    } else {
        fprintf(stderr, "Created egl display\n");
    }

    if (eglInitialize(egl_display, &major, &minor) != EGL_TRUE) {
        fprintf(stderr, "Can't initialise egl display\n");
        exit(1);
    }
    printf("EGL major: %d, minor %d\n", major, minor);

    eglGetConfigs(egl_display, NULL, 0, &count);
    printf("EGL has %d configs\n", count);

    configs = calloc(count, sizeof *configs);
    
    eglChooseConfig(egl_display, config_attribs, configs, count, &n);
    
    for (i = 0; i < n; i++) {
        eglGetConfigAttrib(egl_display, configs[i], EGL_BUFFER_SIZE, &size);
        printf("Buffer size for config %d is %d\n", i, size);
        eglGetConfigAttrib(egl_display, configs[i], EGL_RED_SIZE, &size);
        printf("Red size for config %d is %d\n", i, size);
        
        egl_conf = configs[i];
        break;
    }

    egl_context = eglCreateContext(egl_display, egl_conf, EGL_NO_CONTEXT, context_attribs);
    free(configs); // 释放之前申请的内存
}

static void
get_server_references(void) {
    display = wl_display_connect(NULL);
    if (display == NULL) {
        fprintf(stderr, "Can't connect to display\n");
        exit(1);
    }
    printf("connected to display\n");

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);

    wl_display_dispatch(display);
    wl_display_roundtrip(display);

    if (compositor == NULL || wm_base == NULL) {
        fprintf(stderr, "Can't find compositor or xdg_wm_base\n");
        exit(1);
    } else {
        fprintf(stderr, "Found compositor and xdg_wm_base\n");
    }
}

int main(int argc, char **argv) {
    get_server_references();

    surface = wl_compositor_create_surface(compositor);
    if (surface == NULL) {
        fprintf(stderr, "Can't create surface\n");
        exit(1);
    } else {
        fprintf(stderr, "Created surface\n");
    }

    // --- XDG Shell 核心逻辑 ---
    xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface);
    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);

    xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
    xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);
    xdg_toplevel_set_title(xdg_toplevel, "Wayland EGL App");

    // 注意：xdg-shell 规定必须先发出一次 commit 以触发服务器发回 configure 事件
    // 只有收到并 Ack 了 configure 后，才能通过 EGL 渲染。
    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    create_opaque_region();
    init_egl();
    create_window();

    while (wl_display_dispatch(display) != -1) {
        // 主事件循环
    }

    wl_display_disconnect(display);
    printf("disconnected from display\n");
    
    exit(0);
}
