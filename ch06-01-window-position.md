## 设置窗口位置

在 Wayland 的世界里，如何设置窗口位置是一个令无数 Windows 和 X11 迁移过来的开发者感到困惑的问题。

在传统的 X11 中，应用可以随意调用 `XMoveWindow`。这导致了许多安全和体验问题：应用可以相互覆盖、恶意抢占焦点，或者将自己定位在屏幕之外。

Wayland 的 `xdg-shell` 协议取消了绝对坐标的概念。应用只能请求最大化、全屏或最小化，而具体的屏幕坐标由合成器根据策略（如平铺或堆叠逻辑）自动分配。

注意：`xdg-surface` 提供了一个非常有迷惑性的接口 `set_window_geometry`，从接口名称上看似乎可以设置窗口的 (x, y) 坐标，但这个 x 和 y 设置的是窗口内容相对于 wl_surface 坐标系的偏移，而不是窗口相对于屏幕坐标系的偏移。它只是告诉合成器，“在我的这个 wl_surface 内部，从坐标 (x, y) 开始，宽度为 w，高度为 h 的这一块区域，才是我的实际窗口主体”。这个接口设计的初衷是为了解决 CSD（客户端装饰） 带来的阴影和边框问题。

所以，Wayland 的核心设计哲学之一是：窗口布局权属于合成器（Compositor），而非客户端（Client）。

然而，在开发某些特定应用（如系统挂件、弹出式工具、或者需要恢复用户上次关闭位置的软件）时，固定位置的需求是真实存在的。为了解决这一矛盾，不同的合成器会提供私有协议。在深度（deepin） 系统的 Treeland 合成器中，`treeland-dde-shell-v1` 协议便是解开这一束缚的关键。

### 认识 Treeland 私有协议：`treeland-dde-shell-v1`

Treeland 是深度操作系统（Deepin）基于 wlroots 开发的新一代合成器。为了支持 DDE（Deepin Desktop Environment）特有的桌面交互，它引入了 `treeland-dde-shell-v1` 协议。

该协议扩展了标准窗口的功能，允许客户端与合成器进行更深度的沟通，其中最重要的功能之一就是 **手动指定坐标**。

#### 核心接口：
1.  **`treeland_dde_shell_manager_v1`**: 全局管理器，用于获取 surface 的扩展句柄。
2.  **`treeland_dde_shell_surface_v1`**: 针对特定 Surface 的扩展接口，提供了 `set_surface_position(x, y)` 请求。

### 使用 `treeland-dde-shell-v1` 协议

要使用该协议，你需要按照“生成接口代码 -> 绑定全局对象 -> 关联 Surface -> 发送请求”的流程进行。

#### 1. 生成接口代码
首先，你需要从 Treeland 的源代码或系统开发包中找到 `treeland-dde-shell-v1.xml`。使用 `wayland-scanner` 工具生成 C 语言头文件和胶水代码：

```bash
wayland-scanner client-header treeland-dde-shell-v1.xml treeland-dde-shell-protocol.h
wayland-scanner private-code treeland-dde-shell-v1.xml treeland-dde-shell-protocol.c
```

#### 2. 绑定全局管理器
在 Wayland 的 `registry_handle_global` 回调中，寻找该协议接口：

```c
struct treeland_dde_shell_manager_v1 *dde_shell_manager = NULL;

static void registry_handle_global(void *data, struct wl_registry *registry, 
                                   uint32_t id, const char *interface, uint32_t version) {
    if (strcmp(interface, treeland_dde_shell_manager_v1_interface.name) == 0) {
        dde_shell_manager = wl_registry_bind(registry, id, 
                            &treeland_dde_shell_manager_v1_interface, 1);
    }
}
```

#### 3. 关联窗口并设置位置
你不能直接对 `wl_surface` 设置位置，而是需要通过管理器创建一个“扩展对象”，将其与你的 `xdg_surface` 绑定。

```c
// 创建Wayland表面
state.surface = wl_compositor_create_surface(state.compositor);

// treeland 扩展对象
state.dde_shell_surface = treeland_dde_shell_manager_v1_get_shell_surface(state.dde_shell_manager, state.surface);
// 指定窗口的初始位置
printf("call set_surface_position to %d,%d\n", 10, 20);
treeland_dde_shell_surface_v1_set_surface_position(state.dde_shell_surface, 10, 20);

// 通过xdg-shell将表面设置为toplevel窗口
state.xdg_surface = xdg_wm_base_get_xdg_surface(state.xdg_wm_base, state.surface);
xdg_surface_add_listener(state.xdg_surface, &xdg_surface_listener, &state);
state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);
xdg_toplevel_add_listener(state.xdg_toplevel, &xdg_toplevel_listener, &state);
// 提交表面，让xdg-shell知道我们已经配置好了
wl_surface_commit(state.surface);
```

### 注意事项

1.  **兼容性陷阱**：私有协议顾名思义是私有的。如果你的应用运行在 GNOME (Mutter) 或 KDE (KWin) 上，`dde_shell_manager` 将会是空指针。因此，代码中必须做好 **Null Check**，在协议不支持时退回到默认的 Wayland 行为。
2.  **坐标系基准**：Treeland 的坐标系通常是以逻辑像素为单位的全局输出坐标。在多显示器环境下，坐标可能会跨越不同的屏幕区域。
3.  **生效时机**：`set_surface_position` 需要在设置为 toplevel 窗口之前调用才能生效，这个接口无法做到在任意地方调用都可以生效。
