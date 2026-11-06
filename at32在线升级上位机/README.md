# AT32 固件升级助手

双击 `dist/AT32_Firmware_Updater.exe` 即可运行，无需安装 Python。

1. 板子先运行带升级任务的 APP，USB 转串口连接 USART1。
2. 选择串口，使用 `115200 / 8N1`，点击“连接设备”。
3. 选择链接到 `0x08006000` 的 APP `.bin`，点击“开始下发”。
4. 最后一块收到 OK 后显示完成，板子自动复位，由 Bootloader 搬运固件。

固件最大 360 KB（368640 字节），超过时弹出警告、清空固件选择并禁止下发；每次发送 1024 字节，收到 OK 后继续。界面会自动计算文件长度和 CRC，运行日志逐条显示 AT32 的 READY / OK / ERROR 回应。

源代码运行：`python -m pip install -r requirements.txt`，然后 `python app.py`。

协议头为 `UPFW + uint32_le(长度) + uint32_le(CRC)`。先等待 READY，再发送 BIN；CRC32 初值为 `0x3AA3`、反射多项式为 `0xEDB88320`、结果不取反，计算范围为原始 BIN 字节。设备返回 ERROR 或超时后停止，不自动重发单个数据块。

界面包含“原始数据 RAW”页：按运行日志的“时间 + TX/RX + 十六进制数据”样式显示实际串口收发字节，包括 UPFW 帧头、完整 BIN 数据以及 CR/LF。时间为串口接口读写返回时的本机时间。RAW 内容在后台增量追加到缓存视图，切换页签和筛选不会清空或重新播放历史数据。“清空当前页”分别清空运行日志或 RAW 记录。

RAW 页支持“全部 / 仅 TX / 仅 RX”显示筛选，切换筛选不会删除原始记录，TX/RX 字节统计保持累计总量。两页签使用等宽的无边框文字，标题字号与“固件文件”一致，选中状态通过蓝色下划线区分。

RAW 页“格式”支持十六进制与 ASCII。可打印 ASCII 字节直接显示，CR/LF/TAB/NUL 分别显示为 \r、\n、\t、\0，其他不可打印字节显示为 \xHH，反斜杠显示为 \\。两种视图持续缓存，切换格式或 TX/RX 筛选不重新播放历史数据。TX/RX 字节统计位于底部状态栏，保持原始字节总量。

重新打包（Windows）：

```powershell
python -m pip install -r requirements.txt
python -m pip install pyinstaller
python -m PyInstaller --noconfirm --clean --onefile --windowed --name AT32_Firmware_Updater --icon icon.ico --add-data "icon.ico;." app.py
```

打包结果位于 `dist/AT32_Firmware_Updater.exe`。构建目录、缓存和本地虚拟环境已清理；使用 EXE 无需安装依赖。
