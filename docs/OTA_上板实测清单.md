# OTA 上板实测清单

按顺序做，每步都有可判定的通过标准。失败时先看最后一节的对照表。

## 0. 这一版的行为

- 上电：**流水灯**（PB5 / PE5 各 200 ms 交替）
- ESP32 推包：**只写 W25Q128，不安装**，传完两灯恢复流水，板子继续跑老程序
- 按 PE3：校验通过 → 置 24C02 标志 → 复位 → P02 搬运 → 新程序运行
- 日志：默认约 27 行里程碑；`app_update.c:9` 的 `APP_OTA_DEBUG` 改 `1` 可开逐帧追踪

## 1. 烧录与接线

| 项 | 值 |
|---|---|
| 应用 bin | `bootloadertest-LED\MDK-ARM\bootloadertest-LED\bootloadertest-LED.bin` |
| 应用地址 | `0x08008000` |
| 日志口 | USART1 PA9/PA10 **9600 8N1** |
| OTA 口 | USART2 PA2/PA3 **115200 8N1**，与 ESP32 必须共地 |
| 按键 | PE3 → GND，上拉输入，下降沿触发 |
| 灯 | PB5 = LED0，PE5 = LED1，**低电平点亮** |

## 2. 上电自检（预期就这两行）

```
[app] flash ready (W25Q128 16MB), image base 0x001000, buffer 32768 bytes
[app] ota comm ready: USART2 115200, waiting for S3
```

同时 PB5 / PE5 交替闪。

| 异常输出 | 原因 |
|---|---|
| `flash NOT usable (mf=0x00 id=0x0000)` | SPI1 接线 / CS(PC13) / 供电 |
| `flash ready (unknown part)` | ID 能读但不认识，检查是不是 W25Q 系列 |
| 只有第一行，没有第二行 | USART2 初始化没跑到 |
| 两行都有但灯不闪 | `App_led_poll()` 没进主循环（`main.c:130`）|

## 3. 不触发验证（先排除误动作）

上电后放着不动 2 分钟：

- 灯一直流水
- 串口**不再有任何输出**（不应出现 `start:` / `stored` / `key:`）

出现 `[app] key: ...` 说明 PE3 被误触发（浮空、干扰、或按键常闭）。

## 4. 第一步：ESP32 下载（只存不装）

ESP32 上电 → 连 WiFi → HTTP 下载 → 经 USART2 推包。传输开始时**两灯同时熄灭**。

STM32 侧完整预期：

```
[app] start: size=8412 crc32=from END frame
[app] end: crc32=0x98AC1316
[app] write W25Q128: 8412 bytes @0x001000, 4 sector(s)
[app] verify: crc32=0x98AC1316 expect=0x98AC1316 OK
[app] meta ok: start=0x001000 size=8412
[app] stored in W25Q128: start=0x001000 size=8412, push KEY1 to install
```

**通过标准：最后一行出现，且两灯恢复流水，板子仍在跑老程序（不是新程序）。**

ESP32 侧：`STM32 校验通过，镜像已存入 W25Q128；按 STM32 的 KEY1(PE3) 才会安装`

## 5. 第二步：按 PE3 安装

```
[app] key: image ok (start=0x001000 size=8412), set flag and reset
[app] 24C02 flag ok: 01 5A 6B
```

复位后 P02 侧：

```
bootloader start
check update
update
write app flash success:8412,0
```

**通过标准：最后一行出现，且新程序（P01_led2，1 Hz 闪灯）开始运行。**

## 6. 失败对照表

| 输出 | 原因 | 处理 |
|---|---|---|
| `start: size N > buffer 32768, rejected` | 固件超过 RAM 缓冲 | 换小于 32 KB 的 bin |
| `start: bad size N` | START 帧里的尺寸非法 | 检查 ESP32 侧 `Content-Length` |
| `start: W25Q128 not ready` | 自检时 flash 就没通过 | 回到第 2 节 |
| `crc32 mismatch: got .. expect ..` | 传输丢字节 | 查共地 / 波特率 / 线长；开 `APP_OTA_DEBUG 1` 看 `crc16 mismatch` |
| `end: only N/M bytes received` | 数据帧少了 | 同上 |
| `bad vector: sp=.. reset=..` | 传的 bin 不是给 `0x08008000` 的 | Keil 的 `IROM1` 必须 0x08008000，长度 0x78000 |
| `save failed` | W25Q128 写入或回读失败 | 换扇区 / 查供电 |
| `meta failed` | 元数据回读不一致 | 同上 |
| `key: no valid image in W25Q128` | 元数据非法或从没成功下载过 | 先做第 4 节 |
| `key: bad vector` | W25Q128 里的镜像不适合本应用区 | 重新下载正确的 bin |
| `key: 24C02 flag failed` | I2C1 / 24C02 通信失败 | 查 PB6/PB7、上拉、供电 |
| 完全无 `[app] key:` | EXTI 没进中断 | PE3 是否真接地；`gpio.c:61-63` 的 `GPIO_MODE_IT_FALLING` |
| P02 打印 `no update` | 24C02 里标志是 `0x02` | 没按键，或按键路径失败 |

## 7. 断电测试（防变砖，必做）

1. 完成第 4 节后**不给按键**，直接断电再上电
2. 预期：P02 打印 `no update`，老程序正常运行，W25Q128 里的镜像仍在
3. 再按 PE3 → 应能正常安装

这一步验证"下载"与"安装"确实解耦，是两段式设计的核心收益。

## 8. 回滚手段

| 场景 | 手段 |
|---|---|
| 新程序不正常 | ST-Link 重烧 `bootloadertest-LED.bin` 到 `0x08008000` |
| bootloader 被破坏 | ST-Link 重烧 P03（`0x08004000`）后再烧 P02（`0x08000000`）|
| 只想清掉待装镜像 | 让 STM32 跑一次失败的 START（或直接覆盖 W25Q128 元数据区）|

## 9. 尚未验证的项（如实记录）

- 镜像接近 32 KB 时的边界行为
- W25Q128 坏块下的重试
- 断电恰好落在 P02 擦除瞬间的表现（P03 的设计目标，未做注入测试）
