# STM32F103 + ESP32-S3 串口 OTA

一条能上板跑通的 OTA 链路：**ESP32-S3 从 HTTP 服务器下载固件 → USART2 传给 STM32F103 → 写入 W25Q128 → 置 24C02 标志 → 软复位 → bootloader 安装到应用区**。

带断点续传的可靠性设计：CRC16 帧校验 + 停等重传、整包 CRC32 校验、写入后回读校验、bootloader 侧向量表校验。任一步失败都不会破坏正在运行的程序。

---

## 1. 为什么 app 不能自己刷自己

这是整个设计的地基，也是最容易踩错的地方。

应用固件运行在 `0x08008000`，编译器把它的**代码、中断向量表、常量**全部放在这段区域。要装新固件，第一步就必须擦除这一段；而擦除的正是此刻正在取指的代码。擦完第一条指令就取不到了，1ms 后 SysTick 中断从已擦除的向量表取地址 → 跳 `0xFFFFFFFF` → HardFault。

**所以搬运工作必须由"不在被擦区域内"的程序完成** —— 也就是 bootloader（`0x08000000` / `0x08004000`）。

app 能做的只有三件事：**校验镜像 → 置标志位 → 软复位**。搬运交给 bootloader。这条约束在 `app_update.c` 的注释里有对应说明。

### 由此带来的一次性限制

安装完成后，应用区里跑的是新固件。**如果新固件里没有包含 OTA 接收代码，OTA 能力就消失了**，只能重新用 ST-Link 烧写。这是本仓库的形状决定的：`bootloadertest-LED`（带 OTA）与 `P01_led2`（不带）是两个独立工程，后者仅作为"被升级的目标"演示。

---

## 2. 存储布局

### STM32F103ZET6 内部 Flash（512 KB）

| 地址范围 | 内容 | 工程 |
|---|---|---|
| `0x08000000` – `0x08003FFF` | bootloader：读元数据、搬 W25Q128 → Flash | `P02_bootloader_hal` |
| `0x08004000` – `0x08007FFF` | 复位/防砖层 | `P03_bootloader_reset_hal` |
| `0x08008000` – `0x0807FFFF` | 应用区（480 KB） | `bootloadertest-LED` / `P01_led2` |

### W25Q128（16 MB，SPI1）

| 地址 | 内容 |
|---|---|
| `0x000000` | 元数据：`start(4,LE) + size(4,LE)` |
| `0x001000` | 固件镜像 |

元数据在镜像**通过 CRC32 校验之后**才写入，因此"元数据合法"隐含"镜像当时是完整的"。

### 24C02（I2C1）

| 地址 | 值 | 含义 |
|---|---|---|
| `0x10` | `0x01` 升级 / `0x02` 不升级 | 状态 |
| `0x11`–`0x12` | `0x5A` `0x6B` | 密钥（防误判）|

**三个字节必须一起写。** 只写 `0x10` 而不写密钥，bootloader 会把状态重置为 `0x02` 并且永不安装 —— 这是最难定位的一个坑。

---

## 3. 串口帧协议（USART2 / 115200 8N1）

```
AA 55 | type | rsv | len(2, LE) | payload | crc16(2, LE)
```

| type | 名称 | payload | 应答 |
|---|---|---|---|
| `0x01` | START | `size(4,LE) + crc32(4,LE)`，`crc32==0` 表示 CRC 改由 END 帧携带 | `'K'` 接受 / `'E'` 拒绝 |
| `0x02` | DATA | ≤ 2048 字节，长度必须为偶数 | `'A'` 收到 / `'E'` 重发 |
| `0x03` | END | `crc32(4,LE)` | `'A'` 校验并写入完成 / `'E'` 失败 |

- `crc16` = CCITT（poly `0x1021`，init `0xFFFF`），**只覆盖 `len + payload`**，不含帧头。
- 停等协议：一帧一个应答，收不到应答就重发。
- END 的 `'A'` 在**写盘完成之后**才发出，ESP32 侧等 8 秒。

### CRC32 用的是 zlib 参数

poly `0xEDB88320`（反射）、init `0xFFFFFFFF`、结果异或 `0xFFFFFFFF`。

**不能用 STM32F1 的硬件 CRC 单元** —— 它是 poly `0x04C11DB7`、不反射、不异或，与 zlib 不兼容。两侧都用软件逐位实现，可与 `python -c "import zlib; ..."` 对齐验证。

长度补奇：镜像为奇数长度时补一个 `0xFF`，**补的那个字节计入 CRC**。

---

## 4. 硬件接线

| STM32F103 | 方向 | 对端 | 说明 |
|---|---|---|---|
| PA9 / PA10 | TX / RX | USB-TTL | **USART1 @ 9600**，日志口 |
| PA2 / PA3 | TX / RX | ESP32-S3 GPIO17 / GPIO18 | **USART2 @ 115200**，OTA 数据口，必须共地 |
| PA5 / PA6 / PA7 | SCK / MISO / MOSI | W25Q128 | SPI1，9 MHz |
| PC13 | CS | W25Q128 /CS | 软件片选 |
| PB6 / PB7 | SCL / SDA | 24C02 | I2C1，100 kHz |
| PB5 | — | LED0 | **低电平点亮** |
| PE5 | — | LED1 | **低电平点亮** |
| PE3 | — | KEY1 | 上拉输入，下降沿触发 |

---

## 5. 构建

### STM32 侧（Keil MDK 5 + ARMCC 5.06）

分别打开各工程的 `MDK-ARM/*.uvprojx` 编译，产物 `MDK-ARM/<target>/<target>.bin`。

命令行构建：

```powershell
UV4.exe -b path\to\project.uvprojx -j0 -o build.log
```

单工程 RAM 占用约 37 KB（`s_image` 32 KB 镜像缓冲 + 环形缓冲 2.3 KB + 校验缓冲）。

### 配置（烧 ESP32 前必填）

编辑 `Uart_wifi_project/main/app_config.h`：

```c
#define APP_WIFI_SSID "YOUR_WIFI_SSID"      /* 你的 2.4GHz WiFi 名 */
#define APP_WIFI_PASS "YOUR_WIFI_PASSWORD"  /* 密码 */
#define APP_FW_URL    "http://192.168.1.100:8000/P01_led1.bin"  /* 固件直链 */
```

仓库里的这三个值是占位符 —— 凭据不提交到公开仓库。

固件地址的限制见 §9：**必须 `http://`（当前不支持 HTTPS）、必须返回 `Content-Length`、最终状态码 200**。

### ESP32 侧（ESP-IDF）

```bash
cd Uart_wifi_project
idf.py set-target esp32s3
idf.py build flash monitor
```

固件地址在 `main/app_config.h` 的 `APP_FW_URL` 里改。

---

## 6. 烧写顺序

1. `P03_bootloader_reset_hal` → `0x08004000`
2. `P02_bootloader_hal` → `0x08000000`
3. 应用（`bootloadertest-LED`）→ `0x08008000`

先 P03 后 P02 的顺序不能反，否则上电会跳到空区域。

---

## 7. 使用

上电后应用进入**流水灯**：LED0（PB5）与 LED1（PE5）每 200 ms 交替。

### 方式一：从 ESP32 升级（需要网络）

ESP32 连上 WiFi → HTTP 下载 → 通过 USART2 推给 STM32。传输开始时**两灯同时熄灭**，说明已进入接收状态。

传输结束、CRC32 校验通过后，镜像**只是写进 W25Q128，不会安装**：两灯恢复流水，板子继续跑原来的程序，断电也不影响。安装动作是独立的第二步 —— 见方式二。

### 方式二：按键安装（固件已在 W25Q128 里）

这是**独立的一步**：下载与安装故意分开，避免传完就自动覆盖正在运行的程序。

**按一下 PE3** → 校验 W25Q128 中的元数据与向量表 → 置 24C02 标志 → 复位 → bootloader 搬运。

串口会打印：

```
[app] key: image ok (start=0x001000 size=8412), set flag and reset
[app] 24C02 flag ok: 01 5A 6B
bootloader start
check update
update
write app flash success:8412,0
```

校验不通过时**不会置标志**，只打印原因并回到流水灯 —— 这是防止把无效镜像搬进应用区的唯一防线。

---

## 8. 日志

`app_update.c` 顶部：

```c
#define APP_OTA_DEBUG 0   /* 1 = 逐帧协议追踪 */
```

默认只输出里程碑（约 27 行，含所有失败原因）。改成 `1` 会恢复逐帧细节：每帧长度检查、CRC16 不匹配、越界、超时、多余字节等；关闭时这些字符串不占用 Flash（实测差 860 字节）。

---

## 9. 已知限制与后续可改进项

| 项 | 说明 |
|---|---|
| 镜像缓冲 32 KB | `APP_OTA_IMAGE_BUF` 是硬上限（F103ZE 只有 64 KB RAM）。超过 32 KB 的固件会被 START 拒绝，需改为"边收边写 W25Q128" |
| bootloader 不校验内容 | `P02` 的 `App_bootloader_check_meta_data()` 返回值被忽略（`App_bootloader.c:128`），元数据非法时仍会按 `app_size` 擦页。曾规划改为"校验失败即跳过"，尚未实施 |
| 升级中无进度指示 | 只有"两灯全灭" |
| 单次安装 | 见 §1 |
| 无回滚 | 新固件启动后若异常，只能 ST-Link 救砖 |

---

## 10. 仓库结构

```
.
├── bootloadertest-LED/         应用：OTA 接收器 + 流水灯 + 按键安装
│   ├── Core/                   CubeMX 生成（main.c / gpio.c / usart.c / i2c.c / spi.c）
│   └── MDK-ARM/application/    app_update.c/.h  ← OTA 核心逻辑
├── P02_bootloader_hal/         bootloader：W25Q128 → Flash 搬运
├── P03_bootloader_reset_hal/   复位/防砖层
├── P01_led2/                   被升级的目标工程（演示用，不含 OTA 代码）
├── Uart_wifi_project/          ESP32-S3：WiFi + HTTP 下载 + 串口推包
│   └── main/                   ota_proto.c（协议）/ ota_client.c（流程）/ fw_http.c（下载）
└── docs/
    ├── OTA_上板实测清单.md      上板验收步骤
    └── 按键触发升级-改造方案.md  按键升级的设计取舍记录
```

预处理产物（`*.o` `*.crf` `*.map` `*.axf` `*.htm` `build/` 等）已在 `.gitignore` 中排除。四个 Keil 工程都使用 "Target Dialog" 生成 scatter 文件（`umfTarg=1`、`ScatterFile` 为空），因此 `.sct` 可安全忽略。
