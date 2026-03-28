# IAP Programmer

IAP Programmer 是一个跨平台的 In-Application Programming 工具，用于向嵌入式设备烧录固件。它支持 GUI 和命令行两种操作模式，可在 Windows 和 Linux 平台上运行。

## 功能特点

- **跨平台支持**：Windows、Linux
- **双模式操作**：图形界面 (GUI) 和命令行界面 (CLI)
- **多文件格式**：支持 .bin 和 .hex 固件文件
- **灵活配置**：可设置烧录地址、设备 VID/PID
- **国际化支持**：英文和中文界面
- **用户友好**：直观的图形界面，操作简单

## 系统要求

- **Windows**：Windows 10 或更高版本
- **Linux**：支持 Qt6 的 Linux 发行版

## 依赖项

- Qt6 Widgets
- hidapi 库

## 使用指南

### GUI 模式

1. 启动 IAP Programmer 应用程序
2. 选择固件文件（.bin 或 .hex）
3. 对于 .bin 文件，设置烧录地址
4. 可选：设置设备 VID/PID
5. 点击 "Program" 按钮开始烧录

### 命令行模式

```bash
# 基本用法
iap-programmer -i <固件文件>

# 带地址的烧录（.bin 文件必需）
iap-programmer -i <固件文件> -d <地址>

# 指定设备 VID/PID
iap-programmer -i <固件文件> -vid <VID> -pid <PID>

# 显示帮助信息
iap-programmer --help

# 显示版本信息
iap-programmer --version
```

**参数说明**：

- `-i <file>`：指定固件文件（.bin 或 .hex）
- `-d <address>`：烧录地址（十六进制，如 0x08005000），.bin 文件必需
- `-vid <id>`：设备厂商 ID（十六进制，如 0x2E3C）
- `-pid <id>`：设备产品 ID（十六进制，如 0xAF01）
- `-v, --version`：显示版本信息
- `-h, --help`：显示帮助信息

## 示例

```bash
# 烧录 hex 文件
iap-programmer -i firmware.hex

# 烧录 bin 文件到指定地址
iap-programmer -i firmware.bin -d 0x08005000

# 烧录到指定设备
iap-programmer -i firmware.hex -vid 0x2E3C -pid 0xAF01
```

## 设备权限（Linux）

在 Linux 系统上，可能需要添加 udev 规则以获取设备访问权限：

```bash
sudo cp 99-at32-iap.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

## 构建说明

### 从源码构建

1. **安装依赖**：
   - Qt6 开发库
   - CMake 3.16 或更高版本
   - hidapi 库
2. **构建步骤**：
   ```bash
   mkdir build && cd build
   cmake ..
   make
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
