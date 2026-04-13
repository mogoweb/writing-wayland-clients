## 多窗口与父子关系

在实际应用中，经常会遇到需要同时管理多个窗口的场景。例如：

- 主窗口与工具窗口（如 GIMP 的工具箱）
- 对话框与父窗口
- 设置窗口与主窗口

Wayland 的 `xdg-shell` 协议提供了 `xdg_toplevel_set_parent` 请求，用于声明窗口之间的父子关系。

### xdg_toplevel_set_parent

当两个窗口存在父子关系时，合成器通常会：

1. 将子窗口置于父窗口之上
2. 在窗口切换（如 Alt+Tab）时，父子窗口会一起切换
3. 当父窗口最小化时，子窗口也会最小化
4. 子窗口的位置可以相对于父窗口

```c
/**
 * 设置父窗口
 * @param xdg_toplevel 要设置父窗口的 toplevel
 * @param parent       父窗口的 toplevel，如果为 NULL 则清除父窗口关系
 */
void xdg_toplevel_set_parent(struct xdg_toplevel *xdg_toplevel,
                              struct xdg_toplevel *parent);
```

### 示例：创建两个窗口并建立父子关系

下面的示例创建两个窗口：主窗口（蓝色）和弹出窗口（红色），并将弹出窗口设置为主窗口的子窗口。

```c
// 创建窗口的辅助函数
struct Window* create_window(struct ClientState *state, int width, int height, uint32_t color, const char *title) {
    struct Window *win = calloc(1, sizeof(struct Window));
    win->state = state;
    win->width = width;
    win->height = height;
    win->color = color;

    // 1. 创建 wl_surface
    win->surface = wl_compositor_create_surface(state->compositor);

    // 2. 获取 xdg_surface 角色
    win->xdg_surface = xdg_wm_base_get_xdg_surface(state->xdg_wm_base, win->surface);
    xdg_surface_add_listener(win->xdg_surface, &xdg_surface_listener, win);

    // 3. 获取 xdg_toplevel 角色
    win->xdg_toplevel = xdg_surface_get_toplevel(win->xdg_surface);
    xdg_toplevel_add_listener(win->xdg_toplevel, &xdg_toplevel_listener, win);
    xdg_toplevel_set_title(win->xdg_toplevel, title);

    // 4. 提交初始状态，触发 configure
    wl_surface_commit(win->surface);
    return win;
}

int main() {
    // ... 初始化 Wayland 连接 ...

    // 创建主窗口
    struct Window *main_window = create_window(&state, 800, 600, 0xFF0000FF, "Main Window");

    // 创建子窗口
    struct Window *popup_window = create_window(&state, 400, 300, 0xFFFF0000, "Popup Window");

    // ★ 设置父子关系 ★
    xdg_toplevel_set_parent(popup_window->xdg_toplevel, main_window->xdg_toplevel);

    // 主事件循环同时服务于两个窗口
    while (wl_display_dispatch(state.display) != -1) {
        // 处理事件
    }
}
```

### 父子关系的效果

建立父子关系后，可以观察到以下行为：

1. **窗口堆叠**：子窗口始终显示在父窗口之上
2. **任务栏/窗口列表**：父子窗口通常作为一个组显示
3. **最小化行为**：最小化父窗口时，子窗口也会被最小化
4. **焦点管理**：点击父窗口时，焦点可能在父窗口和子窗口之间转移

### 注意事项

1. **父窗口可以为空**：调用 `xdg_toplevel_set_parent(toplevel, NULL)` 可以清除已有的父子关系

2. **避免循环引用**：不要让窗口 A 的父窗口是 B，同时 B 的父窗口又是 A

3. **生命周期管理**：销毁父窗口前应先销毁子窗口，或先清除父子关系

4. **合成器差异**：不同合成器对父子窗口的处理方式可能略有不同

### 与 xdg-foreign 的区别

`xdg_toplevel_set_parent` 与 `xdg-foreign` 协议都可以建立窗口父子关系，但适用场景不同：

| 特性 | xdg_toplevel_set_parent | xdg-foreign |
|------|-------------------------|-------------|
| 适用范围 | 同一进程内的窗口 | 跨进程窗口 |
| 需要额外协议 | 否（xdg-shell 内置） | 是（xdg-foreign-unstable-v2） |
| 句柄传递 | 不需要 | 需要通过进程间通信传递 |
| 典型场景 | 工具窗口、对话框 | 嵌入式插件、辅助工具 |

完整代码请参考 `code/ch05/sample5-4` 目录。
