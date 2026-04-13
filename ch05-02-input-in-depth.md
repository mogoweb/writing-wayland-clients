## 席位（Seat）：输入处理

向用户展示应用只是 I/O 流程的一半——大多数应用还需要处理输入。为此，Wayland 中的 **seat（席位）** 提供了对输入事件的抽象。从设计理念上讲，一个 Wayland 席位代表用户操作计算机的一个“座位”，最多关联一个键盘和一个指针设备（如鼠标或触控板）。触摸屏、数位板等设备也有类似的关联定义。

需要明确的是，这只是一层抽象，Wayland 显示中表示的席位未必与现实物理设备一一对应。实际使用中，一个 Wayland 会话里通常只会有一个席位。如果你插入第二个键盘，它一般会和第一个键盘分配到同一个席位，并且在你分别输入时动态切换键盘布局等配置。这些实现细节由 Wayland 合成器负责处理。

从客户端视角来看，这一机制相当直观。只要绑定 `wl_seat` 全局对象，你就能使用以下接口：

```
<interface name="wl_seat" version="7">
  <enum name="capability" bitfield="true">
    <entry name="pointer" value="1" />
    <entry name="keyboard" value="2" />
    <entry name="touch" value="4" />
  </enum>

  <event name="capabilities">
    <arg name="capabilities" type="uint" enum="capability" />
  </event>

  <event name="name" since="2">
    <arg name="name" type="string" />
  </event>

  <request name="get_pointer">
    <arg name="id" type="new_id" interface="wl_pointer" />
  </request>

  <request name="get_keyboard">
    <arg name="id" type="new_id" interface="wl_keyboard" />
  </request>

  <request name="get_touch">
    <arg name="id" type="new_id" interface="wl_touch" />
  </request>

  <request name="release" type="destructor" since="5" />
</interface>
```

注意：该接口已多次更新——绑定全局对象时请注意版本。本书假设你绑定的是最新版本，撰写时为版本 7。

该接口的用法相当直观。服务端会向客户端发送 `capabilities` 事件，通过**能力位域**告知此席位支持哪些输入设备，客户端可据此获取想要使用的输入设备对象。例如，如果服务端发送的能力满足
`(caps & WL_SEAT_CAPABILITY_KEYBOARD) > 0`，
客户端就可以使用 `get_keyboard` 请求为该席位获取一个 `wl_keyboard` 对象。每种具体输入设备的语义将在后续章节介绍。

在进入这些细节之前，我们先说明一些通用语义。

### 事件序列号（Event serials）

Wayland 客户端的某些操作需要一种简单的验证方式：**输入事件序列号（serial）**。例如，当客户端打开弹出窗口（右键菜单就是一种弹出窗口）时，可能需要在服务端“抓取”该席位的所有输入事件，直到弹出窗口关闭。为防止滥用此功能，服务端会为每个输入事件分配序列号，并要求客户端在请求中带上该序列号。

服务端收到这类请求时，会查找与序列号对应的输入事件并进行判断：如果事件已过期、对应错误的表面，或是事件类型不匹配（例如仅在点击时允许抓取，鼠标移动时拒绝），就可以拒绝该请求。

从服务端角度看，只需为每个输入事件发送一个递增整数，并记录哪些序列号对特定场景有效，用于后续校验即可。客户端从输入事件处理函数中获取这些序列号，直接回传即可执行所需操作。

我们将在后续章节详细讨论需要序列号验证的具体请求。

### 输入帧（Input frames）

出于实际实现的考虑，来自一个输入设备的单次操作可能会被拆分成多个 Wayland 事件。例如，`wl_pointer` 在你滚动滚轮时会发出 `axis` 事件，同时还会单独发送事件说明轴的类型：滚轮、触控板手指、侧倾滚轮等。如果用户同时快速进行了鼠标移动或按键，来自同一物理输入的事件还可能包含移动或点击信息。

这些相关事件的语义分组在不同输入类型间略有差异，但 **frame 事件** 通常是通用的。简单来说：你可以缓存从设备收到的所有输入事件，等到 `frame` 事件到来时，说明单次输入“帧”的所有事件已收齐，此时可将这批缓存事件当作**一次完整输入**处理，然后清空缓存，开始收集下一帧事件。

如果这听起来有点复杂，也不用太在意。很多应用并不需要关心输入帧，只有在处理更复杂的输入逻辑时才需要考虑。

### 释放设备

当你不再使用某个设备时，每个输入接口都提供了 `release` 请求用于清理资源，形式如下：

```
<request name="release" type="destructor" />
```

非常简单。

