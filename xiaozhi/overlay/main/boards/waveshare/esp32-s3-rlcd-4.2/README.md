# 产品链接

[微雪电子 ESP32-S3-RLCD-4.2](https://www.waveshare.net/shop/ESP32-S3-RLCD-4.2.htm)

# 局域网管理页（无需连热点）

设备联网后，在电脑浏览器直接打开 `http://板子IP:8080`（IP 可在屏幕“关于”页查看，地址旁已标注端口），即可进入与热点配置模式相同的设置页，修改 Wi-Fi、Reporter 令牌、API 余额、天气、管理密码等所有配置。

- **管理密码**：局域网页面需要密码登录。首次打开时会提示设置；后续可在设置页“管理密码”栏修改，也可在热点配置页面设置。密码只存 SHA-256+盐，不会回显。
- **改 Wi-Fi 热切换**：在局域网页面填写新 Wi-Fi 保存后，设备自动切换网络（无需重启）；若 IP 变化，用新地址重新打开。
- **忘记密码**：按住 BOOT，再同时长按 KEY 约 3 秒进入热点配置模式（物理按键门槛，此入口不锁密码），在“管理密码”栏重新设置。
- 热点配置模式（`Syna-0721`，192.168.4.1）行为不变，依旧免密码。
- 局域网访问为家庭内网信任模型下的 HTTP 明文；连续输错 5 次密码会锁定 1 分钟。

# 编译配置命令

**克隆工程**

```bash
git clone https://github.com/78/xiaozhi-esp32.git
```

**进入工程**

```bash
cd xiaozhi-esp32
```

**配置编译目标为 ESP32S3**

```bash
idf.py set-target esp32s3
```

**打开 menuconfig**

```bash
idf.py menuconfig
```

**选择板子**

```bash
Xiaozhi Assistant -> Board Type -> Waveshare ESP32-S3-RLCD-4.2
```

**编译**

```ba
idf.py build
```

**下载并打开串口终端**

```bash
idf.py build flash monitor
```

