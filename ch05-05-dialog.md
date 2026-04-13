## 模态对话框：xdg_dialog_v1 协议

对话框是应用程序中常见的交互方式，用于向用户询问问题、显示信息或收集输入。在传统窗口系统中，对话框可以分为模态和非模态两种：

- **模态对话框**：阻止用户与父窗口交互，直到对话框关闭
- **非模态对话框**：允许用户同时操作对话框和父窗口

Wayland 的 `xdg_dialog_v1` 协议提供了标准的对话框支持，允许客户端向合成器声明对话框属性。

### 协议概述

`xdg_dialog_v1` 协议提供以下接口：

```
<interface name="xdg_dialog_v1" version="1">
  <request name="destroy" type="destructor"/>

  <request name="set_modal">
    <description summary="mark dialog as modal">
      此请求声明对话框为模态。合成器可能会阻止用户与父窗口交互。
    </description>
  </request>

  <request name="set_unmodal">
    <description summary="mark dialog as non-modal">
      此请求声明对话框为非模态。
    </description>
  </request>
</interface>
```

### 使用 xdg_wm_dialog_v1 创建对话框

要使用对话框功能，需要先获取 `xdg_wm_dialog_v1` 全局对象：

```c
static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface, uint32_t version) {
    // ... 其他全局对象绑定 ...
    if (strcmp(interface, xdg_wm_dialog_v1_interface.name) == 0) {
        state->wm_dialog = wl_registry_bind(registry, name, &xdg_wm_dialog_v1_interface, 1);
    }
}
```

然后为对话框窗口创建 `xdg_dialog_v1` 对象：

```c
// 创建对话框窗口
dialog_surface = wl_compositor_create_surface(compositor);
dialog_xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, dialog_surface);
dialog_toplevel = xdg_surface_get_toplevel(dialog_xdg_surface);

// 设置父窗口
xdg_toplevel_set_parent(dialog_toplevel, parent_toplevel);

// 获取对话框对象并设置为模态
struct xdg_dialog_v1 *dialog = xdg_wm_dialog_v1_get_xdg_dialog(wm_dialog, dialog_toplevel);
xdg_dialog_v1_set_modal(dialog);
```

### 完整示例

以下示例演示如何创建一个模态对话框：

```c
// 创建父窗口
parent_surface = wl_compositor_create_surface(compositor);
parent_xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, parent_surface);
parent_toplevel = xdg_surface_get_toplevel(parent_xdg_surface);
xdg_toplevel_set_title(parent_toplevel, "Parent Window");
wl_surface_commit(parent_surface);

// 等待用户按键后创建对话框
printf("Press Enter to create dialog...\n");
getchar();

// 创建对话框窗口
dialog_surface = wl_compositor_create_surface(compositor);
dialog_xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, dialog_surface);
dialog_toplevel = xdg_surface_get_toplevel(dialog_xdg_surface);
xdg_toplevel_set_title(dialog_toplevel, "Dialog Window");

// 设置父窗口关系
xdg_toplevel_set_parent(dialog_toplevel, parent_toplevel);

// 设置为模态对话框
if (wm_dialog) {
    struct xdg_dialog_v1 *dialog = xdg_wm_dialog_v1_get_xdg_dialog(wm_dialog, dialog_toplevel);
    xdg_dialog_v1_set_modal(dialog);
    printf("Dialog set as modal.\n");
}

wl_surface_commit(dialog_surface);
```

### 模态对话框的行为

设置对话框为模态后，合成器可能会：

1. **阻止父窗口交互**：用户无法点击或操作父窗口
2. **居中显示**：对话框可能自动居中于父窗口
3. **视觉提示**：可能对父窗口进行变暗或其他视觉处理
4. **关闭行为**：关闭父窗口时可能自动关闭对话框

### 注意事项

1. **协议支持检查**：并非所有合成器都支持 `xdg_dialog_v1`，使用前应检查 `xdg_wm_dialog_v1` 是否存在

2. **模态是提示而非强制**：`set_modal` 是向合成器的提示，合成器可以选择如何处理

3. **与 xdg_toplevel_set_parent 配合使用**：对话框应该先设置父窗口，再设置模态属性

4. **资源清理**：销毁对话框窗口时，应同时销毁 `xdg_dialog_v1` 对象

```c
xdg_dialog_v1_destroy(dialog);
xdg_toplevel_destroy(dialog_toplevel);
xdg_surface_destroy(dialog_xdg_surface);
wl_surface_destroy(dialog_surface);
```

### 与传统对话框的对比

| 特性 | 传统窗口系统 | Wayland + xdg_dialog_v1 |
|------|-------------|------------------------|
| 模态实现 | 客户端自行阻止父窗口输入 | 合成器协助管理 |
| 窗口管理 | 客户端控制位置和层级 | 合成器决定显示方式 |
| 安全性 | 客户端可抓取所有输入 | 合成器控制输入路由 |

完整代码请参考 `code/ch05/sample5-5` 目录。
