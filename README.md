## 何意味

一个基于 [Zenoh](https://zenoh.io/) 的简易命令行聊天室，展示其发布/订阅模式及传输开销。

技术：

- 语言：C++17
- 构建系统：CMake
- 消息中间件：[Eclipse Zenoh](https://github.com/eclipse-zenoh/zenoh-cpp)
- 元数据封装：[Eclipse Cyclone DDS](https://cyclonedds.io)
- 内存映射：[Mio](https://github.com/vimpunk/mio)
- 命令行界面：手写

## 构建

准备工具：

1. 下载 [Ninja](https://github.com/ninja-build/ninja/releases/latest) 并放置在/添加到 Path
2. 下载 [FlatBuffers 编译器 (flatc)](https://github.com/google/flatbuffers/releases) 并解压放置在/添加到 Path
    > 不建议使用 Linux 包管理器（APT 等）安装，因为其版本可能相当陈旧；Linux 下，若因 LIBC 版本等问题不便使用官方发布版时，建议从源码编译安装。
3. 从源码构建 CycloneDDS：
    1. 克隆仓库：`git clone https://github.com/eclipse-cyclonedds/cyclonedds.git` 并 cd 到 `cyclonedds` 目录下
    2. 按操作系统：
        - Windows（CMD）：将下面的 BUILD_TYPE 赋值为需要使用的构建类型（`Debug` 或 `Release`），然后执行：
            ```bat
            set GIT_TAG=e54e991f
            set BUILD_TYPE=Release
            set INSTALL_DIR=C:/lib/cyclonedds/%GIT_TAG%/%BUILD_TYPE%

            git checkout %GIT_TAG% -- * && git clean -fd && git clean -fdX
            (
            echo diff --git a/src/core/CMakeLists.txt b/src/core/CMakeLists.txt
            echo index d4d23a49..08884943 100644
            echo --- a/src/core/CMakeLists.txt
            echo +++ b/src/core/CMakeLists.txt
            echo @@ -165,5 +165,6 @@ if ^(INSTALL_PDB^)
            echo      OPTIONAL
            echo    ^)
            echo  endif^(^)
            echo -
            echo +if^(BUILD_IDLC_XTESTS^)
            echo  add_subdirectory^(xtests^)
            echo +endif^(^)
            ) > fix_xtests.patch
            git apply fix_xtests.patch
            cmake -S . -B build -DBUILD_EXAMPLES=FALSE -DBUILD_TESTING=FALSE -DBUILD_IDLC=TRUE -DBUILD_DDSPERF=FALSE -DENABLE_SSL=FALSE -DENABLE_ICEORYX=FALSE -DENABLE_SECURITY=FALSE -DENABLE_LIFESPAN=FALSE -DENABLE_DEADLINE_MISSED=FALSE -DENABLE_TYPELIB=FALSE -DENABLE_TYPE_DISCOVERY=FALSE -DENABLE_TOPIC_DISCOVERY=FALSE -DENABLE_SOURCE_SPECIFIC_MULTICAST=FALSE -DENABLE_IPV6=FALSE -DBUILD_IDLC_XTESTS=FALSE -DENABLE_QOS_PROVIDER=FALSE -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR%"
            cmake --build build --config %BUILD_TYPE% && cmake --install build --config %BUILD_TYPE%
            ```
        - Linux：Bash 脚本待编写 <!-- TODO -->
4. 从源码构建 CycloneDDS C++ Bindings：
    1. 克隆仓库：`git clone https://github.com/eclipse-cyclonedds/cyclonedds-cxx.git` 并 cd 到 `cyclonedds-cxx` 目录下
    2. 按操作系统：
        - Windows（CMD）：将下面的 BUILD_TYPE 赋值为需要使用的构建类型（`Debug` 或 `Release`），然后执行：
            ```bat
            set GIT_TAG=20ccaa51
            set BUILD_TYPE=Release
            set INSTALL_DIR=C:/lib/cyclonedds-cxx/%GIT_TAG%/%BUILD_TYPE%

            set DDS_GIT_TAG=e54e991f
            set DDS_DIR=C:/lib/cyclonedds/%DDS_GIT_TAG%/%BUILD_TYPE%

            git checkout %GIT_TAG% && git clean -fdX
            cmake -S . -B build -DCMAKE_PREFIX_PATH="%DDS_DIR%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR%"
            cmake --build build --config %BUILD_TYPE% && cmake --install build --config %BUILD_TYPE%
            ```
        - Linux：Bash 脚本待编写 <!-- TODO -->
5. [安装 Rust](https://rust-lang.org/zh-CN/tools/install/) 或更新至 1.75.0：`rustup update`，安装后需要打开一个新 shell（或重启当前 IDE）

构建：

1. 按以下参数进行 cmake 配置（可能需要在 IDE 的 CMake 设置中修改）：
    - `-G Ninja`：Ninja 生成器可提高混合语言工程的构建速度
    - `CMAKE_BUILD_TYPE`：根据需要设置为 `Release` 或 `Debug`
    - `CMAKE_PREFIX_PATH`：须设置/增加上文构建的 CycloneDDS 库的 *2 个* 安装目录（用 `;` 分隔，若带空格须加引号）
    - `CMAKE_INSTALL_PREFIX`：无需设置，将固定为 cmake 构建目录的 `installed/`
    - `ZENOHC_BUILD_WITH_SHARED_MEMORY` 开关（默认关闭）：若要进行本机进程间通讯，则打开
    - `ZENOHC_BUILD_WITH_UNSTABLE_API` 开关（默认关闭）：若要使用实验性 API，则打开
    - `BUILD_SHARED_LIBS` 开关（默认打开）：若要静态链接，则关闭
    > 配置例（Windows）：`cmake -S . -B cmake-build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=FALSE -DCMAKE_PREFIX_PATH="C:/lib/cyclonedds/e54e991f/Release;C:/lib/cyclonedds-cxx/20ccaa51/Release"`
2. 构建工程：`cmake --build 构建目录 --target LearnZenohCpp`
3. （可选）install，将复制各类依赖文件到构建目录的 `installed/` 下：`cmake --build 构建目录 --target install`

## 配置

程序启动时会尝试读取工作目录下的 `config.json5`, 若不存在，则按 [默认配置](https://github.com/eclipse-zenoh/zenoh/blob/1.9.0/DEFAULT_CONFIG.json5) 运行。

如下配置将令其工作在 [Client 模式](https://zenoh.io/docs/getting-started/deployment/#brokered) 下，并连接位于 `192.168.0.50:7447` 上的 [Zenoh 路由](https://zenoh.io/docs/getting-started/deployment/#routed)，若 5000ms 内连不上，则程序退出：

```json5
{
  mode: "client",
  connect: {
    timeout_ms: { client: 5000 },
    endpoints: [ "tcp/192.168.0.50:7447" ],
  },
}
```

如下配置将令其工作在 [Peer 模式](https://zenoh.io/docs/getting-started/deployment/#peer-to-peer) 下，并连接到上述路由以增强网络发现性：

```json5
{
  mode: "peer",
  connect: {
    endpoints: [ "tcp/192.168.0.50:7447" ],
  },
  scouting: {
    gossip: {
      enabled: true,
      multihop: false,
      autoconnect: { router: [], peer: ["router", "peer"] },
    }
  },
}
```

## 使用

- 启动 chat 程序：命令行执行 `./chat KEY_EXPR`，这里的 KEY_EXPR 是用户自己 Zenoh 节点的 **键表达式**，别人通过此键表达式订阅其消息
    > Zenoh 键表达式规格：不能含空格，不能含双斜线，开头和结尾不能是斜线。
- 订阅其他节点：`/sub KEY1 KEY2...`，这里的 KEY 是要订阅的人的键表达式，支持一行订阅多个（用空格分隔）
    > 所有命令都是在一行开头，以斜线开始，区分大小写。
- 发布消息：直接输入文字，回车发布，一行开头的双斜线 `//` 会被转义为单斜线（出现在其他位置时不处理）
- 发布文件：`/send PATH`，这里的 PATH 是要发布的文件的绝对路径，或相对于工作目录的相对路径
    > 如果使用 Windows 终端（或类似物），输入 PATH 时可将目标文件拖拽到终端窗口上，代替手动输入。
- 接收文件：订阅的节点发布文件时，会自动保存到 `~`（Linux）或用户的“下载”文件夹（Windows）下，*覆盖* 已有文件
- 退出：`/quit` 或 Ctrl+C，出现“Graceful.”字样说明正确退出了会话（不建议强杀进程，可能会导致终端失灵）
