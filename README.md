## 构建

1. 下载 [Ninja](https://github.com/ninja-build/ninja/releases/latest) 并放置在/添加到 Path，
2. 下载 [FlatBuffers 编译器 (flatc)](https://github.com/google/flatbuffers/releases) 并解压放置在/添加到 Path，
3. [安装 Rust](https://rust-lang.org/zh-CN/tools/install/) 或更新至 1.75.0：`rustup update`，
4. 打开一个新 Shell，
5. 按以下参数进行 cmake 配置（可能需要在 IDE 的 CMake 设置中修改）：
   - `-G Ninja`：Ninja 生成器可提高混合语言工程的构建速度
   - `CMAKE_BUILD_TYPE`：根据需要设置为 `Release` 或 `Debug`
   - `CMAKE_INSTALL_PREFIX`：无需设置，将固定为 cmake 构建目录的 `installed/`
   - `ZENOHC_BUILD_WITH_SHARED_MEMORY` 开关（默认关闭）：若要进行本机进程间通讯，则打开
   - `ZENOHC_BUILD_WITH_UNSTABLE_API` 开关（默认关闭）：若要使用实验性 API，则打开
   - `BUILD_SHARED_LIBS` 开关（默认打开）：若要静态链接，则关闭
6. 构建工程：`cmake --build 构建目录 --target LearnZenohCpp`
7. （可选）install，将复制各类依赖文件到构建目录的 `installed/` 下：`cmake --build 构建目录 --target install`

## 配置

程序启动时会尝试读取工作目录下的 `config.json5`, 若不存在，则按 [默认配置](https://github.com/eclipse-zenoh/zenoh/blob/1.9.0/DEFAULT_CONFIG.json5) 运行。

如下配置将令其工作在 Client 模式下，并连接位于 `192.168.0.50:7447` 上的 Zenoh 路由，若 5000ms 内连不上，则程序退出：

```json5
{
  mode: "client",
  connect: {
    timeout_ms: { client: 5000 },
    endpoints: [ "tcp/192.168.0.50:7447" ],
  },
}
```
