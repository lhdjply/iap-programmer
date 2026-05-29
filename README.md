# IAP Programmer

IAP Programmer 是一个跨平台的 In-Application Programming 工具，用于向嵌入式设备烧录固件。支持 GUI 图形界面，可在 Windows 和 Linux 平台上运行。

## 功能特点

- **匠芯创 (ArtInChip) 芯片烧录**：支持通过 USB 直接烧录 .img 镜像文件到 ArtInChip 系列芯片
- **通用 IAP HID 升级**：支持通过 HID 协议烧录 .bin / .hex 固件到通用 MCU
- **跨平台支持**：Windows、Linux
- **多文件格式**：支持 .img、.bin 和 .hex 固件文件
- **灵活配置**：可设置烧录地址、设备 VID/PID
- **国际化支持**：中文和英文界面
- **用户友好**：直观的图形界面，实时日志和进度显示

## 系统要求

- **Windows**：Windows 10 或更高版本
- **Linux**：支持 Qt6 的 Linux 发行版

## 依赖项

- Qt6 Widgets
- hidapi 库
- libusb-1.0（匠芯创下载功能需要）

## 使用指南

启动 IAP Programmer 应用程序后，主页面提供两种下载模式：

### 匠芯创 (ArtInChip) 下载

1. 点击 "ArtInChip Download" 进入匠芯创下载页面
2. 点击 "Browse" 选择 .img 固件文件
3. 点击 "Start Download" 开始烧录
4. 固件烧录完成后会自动复位设备
5. 日志区实时显示烧录进度和状态

### IAP HID 升级

1. 点击 "IAP Upgrade" 进入升级页面
2. 选择固件文件（.bin 或 .hex）
3. 对于 .bin 文件，设置烧录地址
4. 可选：设置设备 VID/PID
5. 点击 "Program" 按钮开始烧录

## 设备权限（Linux）

在 Linux 系统上，需要添加 udev 规则以获取设备访问权限：

```bash
# 通用 IAP 设备
sudo cp 99-iap.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

## 构建说明

### 从源码构建

1. **安装依赖**：
   - Qt6 开发库
   - CMake 3.16 或更高版本
   - hidapi 库
   - libusb-1.0 开发库

2. **构建步骤**：
   ```bash
   mkdir build && cd build
   cmake ..
   make -j$(nproc)
   ```

3. **安装**：
   ```bash
   make install
   ```

## 许可证

本项目使用 MIT 许可证。详见 [LICENSE](LICENSE) 文件。

## 贡献

欢迎提交问题和拉取请求！

## 联系方式

如有问题或建议，请通过 GitHub Issues 提交。
