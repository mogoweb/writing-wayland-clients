到目前为止我们展示的 surface 接口基础功能，已经足以向用户呈现内容，但 surface 接口还提供了许多额外的 request 与 event，以便更高效地使用。很多——如果不是绝大多数——应用程序并不需要在每一帧都重绘整个 surface。甚至“何时绘制下一帧”这一决策，也最好在合成器（compositor）的协助下完成。本章我们将深入探讨 wl_surface 的特性。

## Surface 生命周期

前面提到，Wayland 被设计为原子化更新（atomic updates）：任何一帧都不会以无效或中间状态呈现。对于应用窗口以及其他 surface 的诸多可配置属性而言，实现这种原子性的核心机制就是 wl_surface 本身。

每个 surface 都包含：

* pending state（待定状态）
* applied state（已应用状态）
* 没有任何状态（在首次创建时）

pending state 通过客户端的 request 和服务器的 event 在一段时间内协商形成。当双方都认为该状态是一致且完整的 surface 状态时，surface 会被 commit —— 此时 pending state 会原子地应用为当前状态。

在 commit 之前，合成器会继续渲染最后一个一致的状态；一旦 commit 完成，从下一帧开始将使用新的状态。

以下状态是原子更新的：

* 附加的 wl_buffer（即 surface 的像素内容）
* 自上一帧以来被“损坏”（需要重绘）的区域
* 接收输入事件的区域
* 被视为不透明的区域（用于合成优化）
* 应用于 wl_buffer 的变换（旋转或裁剪显示子区域）
* buffer 的缩放因子（用于 HiDPI 显示）

除了上述 surface 自身的属性外，其“角色（role）”也可能包含类似的双缓冲状态。所有这些状态，以及角色相关的状态，都会在发送 wl_surface.commit 时统一生效。

你可以多次修改这些属性；只有在最终 commit 时，每个属性的“最新值”才会被应用。

刚创建 surface 时，其初始状态是无效的。为了使其有效（即被映射显示），你必须：

* 指定一个角色（例如 xdg_toplevel）
* 分配并 attach 一个 buffer
* 设置角色相关的必要状态

当你发送一次正确配置的 wl_surface.commit 后，该 surface 便成为有效（mapped）状态，并由合成器呈现。

接下来一个关键问题是：什么时候应该准备下一帧？

## Frame 回调机制

更新绘图表面（surface）最简单的方式是：当它需要变更时，直接渲染并附加新的帧（frame）即可。这种方式在事件驱动型应用中表现尤为良好 —— 例如，用户按下某个按键后，文本框需要重新渲染，此时你只需立即重新渲染该文本框，标记出需要更新的区域（damage the appropriate area），并附加一个新的缓冲区（buffer），使其在下一帧中显示。

不过，有些应用可能需要持续不断地渲染帧。比如你在渲染电子游戏画面、播放视频，或是渲染动画时就属于这种情况。显示器本身存在一个固有刷新率（refresh rate），即它能够显示画面更新的最快频率（通常为 60 赫兹、144 赫兹等数值）。渲染帧的速度超过这个频率毫无意义，反而会造成资源浪费 —— 包括 CPU、GPU 资源，甚至会消耗用户设备的电量。如果在显示器每一次刷新间隔内你发送了多个帧，那么除了最后一个帧之外，其余所有帧都会被丢弃，此前的渲染工作也就白费了。

此外，合成器（compositor）甚至可能并不希望为你展示新帧。你的应用可能处于屏幕外、最小化状态，或是被其他窗口遮挡；又或者仅展示应用的小尺寸缩略图 —— 这种情况下，合成器可能会要求你以更低的帧率渲染，以节省资源。正因如此，在 Wayland 客户端中实现持续渲染帧的最佳方式，是让合成器通过帧回调（frame callbacks） 机制告知你它何时准备好接收新帧。

```
<interface name="wl_surface" version="4">
  <!-- ... -->

  <request name="frame">
    <arg name="callback" type="new_id" interface="wl_callback" />
  </request>

  <!-- ... -->
</interface>
```

该请求会分配一个 wl_callback 对象，该对象拥有一套相当简洁的接口：

```
<interface name="wl_callback" version="1">
  <event name="done">
    <arg name="callback_data" type="uint" />
  </event>
</interface>
```

当你在某个绘图表面（surface）上请求帧回调（frame callback）后，一旦合成器（compositor）准备好接收该绘图表面的新帧，就会向这个回调对象发送一个 done 事件。对于帧事件而言，callback_data 会被设置为当前时间（以毫秒为单位），其时间起点（epoch）未做指定。你可以将该时间与上一帧的时间做对比，以此计算动画的进度，或是对输入事件进行比例缩放。

既然我们已经掌握了帧回调这一工具，何不基于第 7.3 节的应用程序进行改造，让它每一帧都滚动一小段距离呢？首先，我们给 client_state 结构体添加一小段状态变量：

```
--- a/client.c
+++ b/client.c
@@ -71,6 +71,8 @@ struct client_state {
 	struct xdg_surface *xdg_surface;
 	struct xdg_toplevel *xdg_toplevel;
+	/* State */
+	float offset;
+	uint32_t last_frame;
 };
 
 static void wl_buffer_release(void *data, struct wl_buffer *wl_buffer) {
```

接下来，我们要更新 draw_frame 函数，让它把偏移量（offset）纳入计算考量：

```
@@ -107,9 +109,10 @@ draw_frame(struct client_state *state)
 	close(fd);
 
 	/* Draw checkerboxed background */
+	int offset = (int)state->offset % 8;
 	for (int y = 0; y < height; ++y) {
 		for (int x = 0; x < width; ++x) {
-			if ((x + y / 8 * 8) % 16 < 8)
+			if (((x + offset) + (y + offset) / 8 * 8) % 16 < 8)
 				data[y * width + x] = 0xFF666666;
 			else
 				data[y * width + x] = 0xFFEEEEEE;
```

在 main 函数中，我们来为第一个新帧注册一个回调函数：

```
@@ -195,6 +230,9 @@ main(int argc, char *argv[])
 	xdg_toplevel_set_title(state.xdg_toplevel, "Example client");
 	wl_surface_commit(state.wl_surface);
 
+	struct wl_callback *cb = wl_surface_frame(state.wl_surface);
+	wl_callback_add_listener(cb, &wl_surface_frame_listener, &state);
+
 	while (wl_display_dispatch(state.wl_display)) {
 		/* This space deliberately left blank */
 	}
```

随后按如下方式实现该回调：

```
@@ -147,6 +150,38 @@ static const struct xdg_wm_base_listener xdg_wm_base_listener = {
 	.ping = xdg_wm_base_ping,
 };
 
+static const struct wl_callback_listener wl_surface_frame_listener;
+
+static void
+wl_surface_frame_done(void *data, struct wl_callback *cb, uint32_t time)
+{
+	/* Destroy this callback */
+	wl_callback_destroy(cb);
+
+	/* Request another frame */
+	struct client_state *state = data;
+	cb = wl_surface_frame(state->wl_surface);
+	wl_callback_add_listener(cb, &wl_surface_frame_listener, state);
+
+	/* Update scroll amount at 24 pixels per second */
+	if (state->last_frame != 0) {
+		int elapsed = time - state->last_frame;
+		state->offset += elapsed / 1000.0 * 24;
+	}
+
+	/* Submit a frame for this event */
+	struct wl_buffer *buffer = draw_frame(state);
+	wl_surface_attach(state->wl_surface, buffer, 0, 0);
+	wl_surface_damage_buffer(state->wl_surface, 0, 0, INT32_MAX, INT32_MAX);
+	wl_surface_commit(state->wl_surface);
+
+	state->last_frame = time;
+}
+
+static const struct wl_callback_listener wl_surface_frame_listener = {
+	.done = wl_surface_frame_done,
+};
+
 static void
 registry_global(void *data, struct wl_registry *wl_registry,
 		uint32_t name, const char *interface, uint32_t version)
```

现在，针对每一帧，我们需要执行以下操作：

1. 销毁已使用完毕的帧回调（frame callback）对象。
2. 为下一帧请求一个新的回调。
3. 渲染并提交新帧。

其中第三步可拆解为以下子步骤：
1. 结合自上一帧以来的时间差更新状态（设置新的偏移量），以保证滚动速率的一致性。
2. 创建一个新的 wl_buffer 对象，并为其渲染一帧画面。
3. 将这个新的 wl_buffer 附加到绘图表面（surface）上。
4. 标记整个绘图表面为 “脏区”（damage）。
5. 提交（commit）该绘图表面。

步骤 3 和步骤 4 会更新绘图表面的 “待处理状态（pending state）”—— 为其绑定新的缓冲区，并标记整个绘图表面已发生变更。步骤 5 则提交这份待处理状态，将其应用到绘图表面的 “当前状态（current state）”，并在下一帧中生效。以原子操作的方式应用这个新缓冲区，意味着我们绝不会出现 “仅显示上一帧一半内容” 的情况，从而实现无撕裂（tear-free）的流畅显示效果。编译并运行更新后的客户端程序，亲自体验一下吧！

## 标记绘图表面为脏区

你可能已经注意到，在上一个示例中，我们提交绘图表面的新帧时添加了这行代码：

```
wl_surface_damage_buffer(state->wl_surface, 0, 0, INT32_MAX, INT32_MAX);
```

如果你注意到了，说明观察得很仔细！这行代码会将我们的绘图表面标记为脏区，告知合成器（compositor）该区域需要重新绘制。在这个例子中，我们标记了整个绘图表面（甚至超出其边界的区域），但实际上我们也可以只标记其中一部分区域为脏区。

举个例子：假设你开发了一套 GUI 工具库，用户正在向文本框中输入内容。这个文本框通常只占窗口的一小部分，而每输入一个新字符，需要更新的区域会更小。当用户按下按键时，你可以只渲染新增的那个字符，然后仅将绘图表面中对应这个字符的区域标记为脏区。这样一来，合成器只需复制绘图表面中这一小部分内容即可，能显著提升渲染效率 —— 对于嵌入式设备而言，效果尤为明显。当字符间的光标闪烁时，你只需为光标更新的区域提交脏区标记；而当用户切换视图时，你可能需要将整个绘图表面标记为脏区。通过这种方式，所有环节的运算量都会减少，用户也会因为设备续航的提升而受益。

注意：Wayland 协议提供了两个用于标记脏区的请求（request）：damage 和 damage_buffer。前者已实质上被废弃（deprecated），你应当只使用后者。二者的核心区别在于：damage 会考虑所有作用于绘图表面的变换操作（例如旋转、缩放比例、缓冲区位置和裁剪等）；而 damage_buffer 则基于缓冲区（buffer）相对坐标标记脏区，这种方式通常更易于理解和处理。

## 绘图表面区域（Surface regions）

我们已经通过 `wl_compositor.create_surface` 接口调用，利用 `wl_compositor` 接口创建过 `wl_surface` 对象。但需要注意的是，该接口还有第二个请求（request）：`create_region`。

```
<interface name="wl_compositor" version="4">
  <request name="create_surface">
    <arg name="id" type="new_id" interface="wl_surface" />
  </request>

  <request name="create_region">
    <arg name="id" type="new_id" interface="wl_region" />
  </request>
</interface>
```

`wl_region` 接口用于定义一组矩形区域，这些矩形共同构成一个任意形状的几何区域。通过该接口提供的请求，你可以对其定义的几何区域执行**位运算**——具体方式是向其中添加（add）或移除（subtract）矩形区域。

```
<interface name="wl_region" version="1">
  <request name="destroy" type="destructor" />

  <request name="add">
    <arg name="x" type="int" />
    <arg name="y" type="int" />
    <arg name="width" type="int" />
    <arg name="height" type="int" />
  </request>

  <request name="subtract">
    <arg name="x" type="int" />
    <arg name="y" type="int" />
    <arg name="width" type="int" />
    <arg name="height" type="int" />
  </request>
</interface>
```

举例来说，若要创建一个中间带孔洞的矩形区域，你可以按以下步骤操作：

1. 发送 `wl_compositor.create_region` 请求，分配一个 `wl_region` 对象；
2. 发送 `wl_region.add(0, 0, 512, 512)` 请求，创建一个 512×512 像素的矩形区域；
3. 发送 `wl_region.subtract(128, 128, 256, 256)` 请求，从该区域的中间移除一个 256×256 像素的矩形（形成孔洞）。

这些区域也可以是**不连续的**，并非必须是单个连续的多边形。创建好这类区域后，你可将其传入 `wl_surface` 接口的两个请求中，即 `set_opaque_region` 和 `set_input_region`。

```
<interface name="wl_surface" version="4">
  <request name="set_opaque_region">
    <arg name="region" type="object" interface="wl_region" allow-null="true" />
  </request>

  <request name="set_input_region">
    <arg name="region" type="object" interface="wl_region" allow-null="true" />
  </request>
</interface>
```

不透明区域（opaque region）是向合成器（compositor）传递的一个“提示”，用于告知合成器：绘图表面的哪些部分被认定为不透明。合成器可基于此信息优化渲染流程。例如，若你的绘图表面完全不透明，且遮挡了其下方的另一个窗口，合成器就无需浪费资源重绘被遮挡的窗口。该区域默认为空，意味着合成器会假定绘图表面的任意部分都可能是透明的——这种默认行为效率最低，但兼容性最好（最不易出错）。

输入区域（input region）用于指定绘图表面中哪些部分接收指针（pointer）和触摸（touch）输入事件。例如，你可能在绘图表面下方绘制了投影阴影，但该阴影区域的输入事件应传递给下方的客户端窗口；或者，若你的窗口是不规则形状，可将输入区域设置为对应形状。对于大多数类型的绘图表面，默认情况下整个表面都会接收输入事件。

向这两个请求传入 `null`（而非 `wl_region` 对象），即可将对应区域设置为空。这两个请求均采用**双缓冲（double-buffered）** 机制——需发送 `wl_surface.commit` 请求，才能让你的修改生效。一旦通过这两个请求传入了 `wl_region` 对象，即可立即销毁该对象以释放资源。传入对象后再修改 `wl_region` 的内容，不会同步更新绘图表面的对应状态。

## 子表面（Subsurfaces）
在 Wayland 核心协议 `wayland.xml` 中，只定义了一种表面角色：**子表面（subsurfaces）**。
子表面拥有相对于父表面的 X、Y 坐标（该坐标不受父表面边界限制），以及相对于同级子表面和父表面的**Z 序（层级）**。

该特性的一些典型用途包括：
- 使用原生像素格式播放视频表面，并在上方叠加 RGBA 格式的用户界面或字幕；
- 主应用界面使用 OpenGL 表面，同时用子表面以软件渲染方式绘制窗口装饰；
- 在客户端无需重新绘制的情况下移动 UI 的某一部分。

在硬件叠加层（hardware planes）的支持下，**合成器甚至不需要为更新子表面进行任何重绘**。
这一点在嵌入式系统中尤其有用。设计精巧的应用可以利用子表面实现极高的渲染效率。

管理子表面的接口是 `wl_subcompositor`。
`get_subsurface` 请求是子合成器的主要入口：

```
<request name="get_subsurface">
  <arg name="id" type="new_id" interface="wl_subsurface" />
  <arg name="surface" type="object" interface="wl_surface" />
  <arg name="parent" type="object" interface="wl_surface" />
</request>
```

当一个 `wl_surface` 与 `wl_subsurface` 对象关联后，它就成为了对应父表面的子表面。
子表面本身还可以拥有自己的子表面，从而在任意顶层表面之下形成一棵**有序的表面树**。
对这些子表面的操作通过 `wl_subsurface` 接口完成：

```
<request name="set_position">
  <arg name="x" type="int" summary="x coordinate in the parent surface"/>
  <arg name="y" type="int" summary="y coordinate in the parent surface"/>
</request>

<request name="place_above">
  <arg name="sibling" type="object" interface="wl_surface" />
</request>

<request name="place_below">
  <arg name="sibling" type="object" interface="wl_surface" />
</request>

<request name="set_sync" />
<request name="set_desync" />
```

通过将子表面置于**共享同一父表面的同级表面之上或之下**，或是置于父表面本身之上或之下，就可以修改它的 Z 序。

`wl_subsurface` 的各类属性同步机制需要一些说明。
位置与 Z 序属性会与**父表面的生命周期同步**。当对主表面发送 `wl_surface.commit` 请求时，其所有子表面的位置与 Z 序变更会随之一同生效。

但是，与子表面关联的 `wl_surface` 状态（例如缓冲区附着、脏区累积）**不必与父表面生命周期绑定**。
这正是 `set_sync` 和 `set_desync` 请求的作用：
- 与父表面同步（synced）的子表面，会在父表面提交时一并提交自身所有状态；
- 不同步（desynced）的子表面，则像普通表面一样管理自己的提交生命周期。

简单来说：
- `sync` 和 `desync` 请求**非缓冲、立即生效**；
- 位置与 Z 序请求是**缓冲状态**，不受子表面同步/不同步属性影响——它们**总是随父表面一起提交**；
- 关联 `wl_surface` 上的其余表面状态，则根据子表面的同步/不同步状态来提交。

## 高分辨率表面（HiDPI）

过去几年里，高端显示器的像素密度实现了巨大飞跃，新屏幕能在相同物理尺寸内塞进两倍于以往的像素。我们称这类屏幕为 **HiDPI**（高每英寸点数）。

但这类屏幕远超前代“低 DPI”设备，必须在应用层面做适配才能正常使用。

如果在相同空间内把屏幕分辨率翻倍，却不对界面做特殊处理，所有 UI 元素的尺寸都会**减半**，导致文字无法阅读、交互元素过小。

作为代价换来的，则是矢量图形（尤其是文字渲染）的画质大幅提升。

Wayland 的解决方案是：为每个输出（output）添加**缩放系数（scale factor）**，并要求客户端将该缩放系数应用到界面上。

此外，不支持 HiDPI 的客户端可以不做任何处理，由合成器通过放大其缓冲区来兼容显示。

合成器通过对应事件向客户端通知每个输出的缩放系数：

```
<interface name="wl_output" version="3">
  <!-- ... -->
  <event name="scale" since="2">
    <arg name="factor" type="int" />
  </event>
</interface>
```

注意：该事件在版本 2 中加入，因此绑定 `wl_output` 全局对象时，必须将版本至少设为 2 才能收到该事件。

但仅靠这一点还不足以在客户端中使用 HiDPI。

要正确实现 HiDPI，合成器还必须向你的 `wl_surface` 发送 **enter 事件**，表明它已“进入”（显示在）某个或多个输出上：

```
<interface name="wl_surface" version="4">
  <!-- ... -->
  <event name="enter">
    <arg name="output" type="object" interface="wl_output" />
  </event>
</interface>
```

一旦客户端知道表面显示在哪些输出上，就应当**取这些输出缩放系数的最大值**，
将缓冲区的尺寸（以像素为单位）乘以该值，然后以 2 倍、3 倍或 N 倍缩放渲染 UI。

随后，像这样声明缓冲区所使用的缩放比例：

```
<interface name="wl_surface" version="4">
  <!-- ... -->
  <request name="set_buffer_scale" since="3">
    <arg name="scale" type="int" />
  </request>
</interface>
```

注意：这需要 `wl_surface` 版本至少为 3。
该版本号应在通过 `wl_registry` 绑定 `wl_compositor` 时指定。

在下一次 `wl_surface.commit` 时，你的表面就会应用这个缩放系数。
- 如果缓冲区缩放系数大于表面所在输出的缩放系数，合成器会缩小显示；
- 如果更小，则合成器会放大显示。
