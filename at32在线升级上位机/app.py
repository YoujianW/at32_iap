"""AT32 固件升级助手。串口读写在工作线程中完成，界面保持响应。"""

import argparse
import ctypes
import queue
import threading
import time
import tkinter as tk
from collections import deque
from datetime import datetime
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

import serial
from serial.tools import list_ports

from protocol import TransferCancelled, read_firmware, send_firmware

BG = "#F4F6FA"
WHITE = "#FFFFFF"
TEXT = "#17243B"
MUTED = "#64748B"
LINE = "#E2E8F0"
ACCENT = "#2563EB"
LIGHT = "#EFF5FF"
RED = "#D74855"
FONT = ("Microsoft YaHei UI", 13)
SMALL = ("Microsoft YaHei UI", 11)
MONO = ("Consolas", 12)
TITLE_FONT = ("Microsoft YaHei UI", 16, "bold")


class TracedSerial:
    """记录串口接口实际写入、读取的字节，不改变传输协议。"""

    def __init__(self, port, on_raw):
        self.port = port
        self.on_raw = on_raw

    def __getattr__(self, name):
        return getattr(self.port, name)

    def write(self, data):
        count = self.port.write(data)
        if count:
            self.on_raw("TX", bytes(data[:count]))
        return count

    def read(self, *args, **kwargs):
        data = self.port.read(*args, **kwargs)
        if data:
            self.on_raw("RX", bytes(data))
        return data

    def read_until(self, *args, **kwargs):
        data = self.port.read_until(*args, **kwargs)
        if data:
            self.on_raw("RX", bytes(data))
        return data


def raw_hex_lines(data):
    for index in range(0, len(data), 16):
        yield data[index:index + 16].hex(" ").upper()


def raw_ascii_lines(data):
    escapes = {0: r"\0", 9: r"\t", 10: r"\n", 13: r"\r", 92: r"\\"}
    for index in range(0, len(data), 16):
        chunk = data[index:index + 16]
        yield "".join(escapes.get(byte, chr(byte) if 32 <= byte < 127 else f"\\x{byte:02X}")
                      for byte in chunk)


class SerialWorker(threading.Thread):
    def __init__(self, events):
        super().__init__(daemon=True)
        self.events = events
        self.commands = queue.Queue()
        self.cancel = threading.Event()
        self.stopping = threading.Event()
        self.port = None

    def emit(self, name, *args):
        self.events.put((name, args))

    def close_port(self):
        if self.port is not None:
            self.port.close()
            self.port = None

    def connect(self, name, baud):
        self.close_port()
        connection = serial.Serial()
        connection.port = name
        connection.baudrate = baud
        connection.timeout = 0.08
        connection.write_timeout = 3
        connection.dtr = False
        connection.rts = False
        try:
            connection.open()
        except Exception:
            connection.close()
            raise
        self.port = TracedSerial(
            connection,
            lambda direction, data: self.emit(
                "raw", datetime.now().strftime("%H:%M:%S.%f")[:-3], direction, data),
        )
        self.emit("connected", name, baud)

    def transfer(self, data):
        try:
            send_firmware(
                self.port, data, self.cancel,
                lambda *args: self.emit("progress", *args),
                lambda *args: self.emit("log", *args),
            )
            self.emit("completed")
        except TransferCancelled:
            self.emit("log", "已停止发送，等待设备退出接收状态。", "warning")
            self.recover()
        except Exception as error:
            self.emit("log", str(error), "error")
            self.recover()

    def recover(self):
        # 板端没有取消指令，等待其 5 秒接收超时后才能重新发起。
        for remaining in range(6, 0, -1):
            self.emit("recovering", remaining)
            if self.stopping.wait(1):
                return
        self.emit("recovered")

    def run(self):
        try:
            while not self.stopping.is_set():
                try:
                    command, args = self.commands.get_nowait()
                except queue.Empty:
                    command, args = None, ()
                try:
                    if command == "connect":
                        self.connect(*args)
                    elif command == "disconnect":
                        self.close_port()
                        self.emit("disconnected")
                    elif command == "send":
                        self.transfer(*args)
                    elif self.port is not None:
                        data = self.port.read(min(self.port.in_waiting, 4096) or 1)
                        if data:
                            self.emit("rx", data)
                    else:
                        self.stopping.wait(0.08)
                except Exception as error:
                    self.close_port()
                    self.emit("connection_error", str(error))
        finally:
            self.close_port()


class ProgressBar(tk.Canvas):
    def __init__(self, parent):
        self.scale = parent.winfo_fpixels("1i") / 96
        self.thickness = max(10, round(12 * self.scale))
        super().__init__(parent, height=self.thickness + 4, bg=WHITE, highlightthickness=0)
        self.value = 0
        self._redraw_job = None
        self._track = self.create_line(0, 0, 0, 0, width=self.thickness,
                                       fill="#E9EEF6", capstyle="round")
        self._fill = self.create_line(0, 0, 0, 0, width=self.thickness,
                                      fill=ACCENT, capstyle="round", state="hidden")
        self.bind("<Configure>", self.schedule_redraw)
        self.bind("<Destroy>", self.cancel_redraw)

    def schedule_redraw(self, event=None):
        if self._redraw_job is None:
            self._redraw_job = self.after(33, self.redraw)

    def cancel_redraw(self, event=None):
        if self._redraw_job is not None:
            self.after_cancel(self._redraw_job)
            self._redraw_job = None

    def redraw(self):
        self._redraw_job = None
        padding = self.thickness / 2 + 1
        width = max(padding * 2, self.winfo_width())
        middle = (self.thickness + 4) / 2
        self.coords(self._track, padding, middle, width - padding, middle)
        end = padding + (width - padding * 2) * self.value / 100
        self.coords(self._fill, padding, middle, end, middle)
        self.itemconfigure(self._fill, state="normal" if self.value else "hidden")

    def set(self, value):
        value = max(0, min(100, value))
        if value != self.value:
            self.value = value
            self.schedule_redraw()


class UpdateApp:
    def __init__(self, root, firmware=None):
        self.root = root
        self.root.title("AT32 固件升级助手")
        self.scale = self.root.winfo_fpixels("1i") / 96
        screen_width = self.root.winfo_screenwidth()
        screen_height = self.root.winfo_screenheight()
        width = min(self.px(1220), screen_width - self.px(60))
        height = min(self.px(900), screen_height - self.px(90))
        self.root.geometry(f"{width}x{height}")
        self.root.minsize(min(self.px(1060), width), min(self.px(790), height))
        self.root.configure(bg=BG)
        icon = Path(__file__).with_name("icon.ico")
        if icon.exists():
            self.root.iconbitmap(str(icon))
        self.root.option_add("*Font", FONT)
        self.root.option_add("*Listbox.font", FONT)

        self.events = queue.Queue()
        self.worker = SerialWorker(self.events)
        self.connected = False
        self.connecting = False
        self.busy = False
        self.data = None
        self.path = None
        self.port_map = {}
        self.rx_text = bytearray()
        self.pending_logs = deque()
        self.raw_records = []
        self.raw_render_index = 0
        self.raw_render_offset = 0
        self.raw_tx_bytes = 0
        self.raw_rx_bytes = 0
        self._raw_counts_dirty = True
        self.raw_filter = "ALL"
        self.raw_mode = "HEX"
        self._wrap_widths = {}
        self._applied_wrap_widths = {}
        self._wrap_jobs = {}
        self._poll_job = None
        self._style()
        self._build()
        self.refresh_ports()
        self.worker.start()
        self.log("升级助手已就绪，请连接设备并选择 APP 固件。", "info")
        if firmware:
            self.load_firmware(firmware)
        self._poll_job = self.root.after(33, self.poll_events)
        self.root.protocol("WM_DELETE_WINDOW", self.close)

    def px(self, value):
        return max(1, round(value * self.scale))

    def _style(self):
        style = ttk.Style(self.root)
        style.theme_use("clam")
        style.configure("TCombobox", padding=self.px(9), font=FONT, foreground=TEXT,
                        fieldbackground=WHITE, background=WHITE, bordercolor=LINE,
                        lightcolor=LINE, darkcolor=LINE, arrowcolor=MUTED,
                        arrowsize=self.px(17))
        style.map("TCombobox", fieldbackground=[("readonly", WHITE)],
                  selectbackground=[("readonly", WHITE)], selectforeground=[("readonly", TEXT)])
        style.configure("Vertical.TScrollbar", background="#CBD5E1", troughcolor=WHITE,
                        bordercolor=WHITE, arrowcolor=MUTED, arrowsize=self.px(15))
        style.configure("Logs.TNotebook", background=WHITE, borderwidth=0,
                        tabmargins=(0, 0, 0, 0))
        # 内容仍由 Notebook 管理，页签使用无边框的文字与下划线。
        style.layout("Logs.TNotebook.Tab", [])
        style.configure("RawFilter.TCombobox", font=("Microsoft YaHei UI", 12),
                        padding=(self.px(6), self.px(2)), arrowsize=self.px(14))

    def label(self, parent, text, **kwargs):
        options = dict(text=text, bg=parent.cget("bg"), fg=TEXT, font=FONT, anchor="w")
        options.update(kwargs)
        return tk.Label(parent, **options)

    def button(self, parent, text, command, primary=False, **kwargs):
        options = dict(text=text, command=command, font=("Microsoft YaHei UI", 13, "bold"),
                       bg=ACCENT if primary else WHITE, fg=WHITE if primary else TEXT,
                       activebackground="#1D4ED8" if primary else LIGHT,
                       activeforeground=WHITE if primary else ACCENT, relief="flat", bd=0,
                       cursor="hand2", padx=self.px(18), pady=self.px(10),
                       disabledforeground="#94A3B8", highlightthickness=1,
                       highlightbackground=ACCENT if primary else LINE,
                       highlightcolor=ACCENT, takefocus=True)
        options.update(kwargs)
        return tk.Button(parent, **options)

    def card(self, parent):
        return tk.Frame(parent, bg=WHITE, highlightbackground=LINE, highlightthickness=1)

    def _build(self):
        p = self.px
        header = tk.Frame(self.root, bg=WHITE)
        header.pack(fill="x")
        header_inner = tk.Frame(header, bg=WHITE)
        header_inner.pack(fill="x", padx=p(28), pady=p(20))
        self.label(header_inner, "AT32", fg=ACCENT,
                   font=("Segoe UI", 22, "bold")).pack(side="left", padx=(0, p(18)))
        title = tk.Frame(header_inner, bg=WHITE)
        title.pack(side="left")
        self.label(title, "固件升级", font=("Microsoft YaHei UI", 20, "bold")).pack(anchor="w")
        self.label(title, "连接设备，选择固件，一键完成下发", fg=MUTED, font=SMALL).pack(anchor="w", pady=(p(3), 0))
        self.connection_badge = self.label(header_inner, "●  未连接", fg=MUTED, bg=BG,
                                            padx=p(16), pady=p(9))
        self.connection_badge.pack(side="right")
        tk.Frame(header, height=1, bg=LINE).pack(fill="x")

        footer = tk.Frame(self.root, bg=BG)
        footer.pack(side="bottom", fill="x", padx=p(28), pady=(p(8), p(14)))
        self.label(footer, "固件升级助手", fg=MUTED, font=SMALL).pack(side="left")
        self.label(footer, "最大固件 360 KB", fg=MUTED, font=SMALL).pack(side="right")

        main = tk.Frame(self.root, bg=BG)
        main.pack(fill="both", expand=True, padx=p(24), pady=(p(20), 0))
        main.grid_columnconfigure(0, weight=3, uniform="body")
        main.grid_columnconfigure(1, weight=2, uniform="body")
        main.grid_rowconfigure(2, weight=1)

        connection = self.card(main)
        connection.grid(row=0, column=0, columnspan=2, sticky="ew", pady=(0, p(18)))
        form = tk.Frame(connection, bg=WHITE)
        form.pack(fill="x", padx=p(22), pady=p(18))
        form.grid_columnconfigure(0, weight=1)
        self.label(form, "设备串口", fg=MUTED, font=SMALL).grid(row=0, column=0, sticky="w", pady=(0, p(7)))
        self.label(form, "波特率", fg=MUTED, font=SMALL).grid(row=0, column=1, sticky="w", padx=(p(20), 0), pady=(0, p(7)))
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(form, textvariable=self.port_var, state="readonly", width=14)
        self.port_combo.grid(row=1, column=0, sticky="ew")
        self.port_combo.bind("<<ComboboxSelected>>", lambda event: self.show_port_description())
        self.baud_var = tk.StringVar(value="115200")
        self.baud_combo = ttk.Combobox(form, textvariable=self.baud_var, state="readonly",
                                      values=("115200", "57600", "38400", "19200", "9600"), width=10)
        self.baud_combo.grid(row=1, column=1, padx=(p(20), 0))
        self.refresh_button = self.button(form, "刷新", self.refresh_ports, fg=ACCENT)
        self.refresh_button.grid(row=1, column=2, padx=(p(14), 0))
        self.connect_button = self.button(form, "连接设备", self.toggle_connection, primary=True)
        self.connect_button.grid(row=1, column=3, padx=(p(14), 0))
        self.port_description = self.label(form, "", fg=MUTED, font=SMALL, wraplength=p(500), justify="left")
        self.port_description.grid(row=2, column=0, columnspan=2, sticky="w", pady=(p(9), 0))
        self.label(form, "8N1 · 无流控", fg=MUTED, font=SMALL).grid(row=2, column=2, columnspan=2,
                                                                   sticky="e", pady=(p(9), 0))

        firmware_card = self.card(main)
        firmware_card.grid(row=1, column=0, sticky="nsew", padx=(0, p(16)), pady=(0, p(18)))
        firmware = tk.Frame(firmware_card, bg=WHITE)
        firmware.pack(fill="both", expand=True, padx=p(22), pady=p(20))
        top = tk.Frame(firmware, bg=WHITE)
        top.pack(fill="x")
        self.label(top, "固件文件", font=TITLE_FONT).pack(side="left")
        self.file_button = self.button(top, "选择 BIN", self.choose_file, fg=ACCENT,
                                        padx=p(13), pady=p(7))
        self.file_button.pack(side="right")
        file_panel = tk.Frame(firmware, bg="#F7F9FC", highlightbackground=LINE, highlightthickness=1)
        file_panel.pack(fill="both", expand=True, pady=(p(18), p(20)))
        file_panel.grid_columnconfigure(1, weight=1)
        file_panel.grid_rowconfigure(0, weight=1)
        document = tk.Canvas(file_panel, width=p(48), height=p(58), highlightthickness=0, bg="#F7F9FC")
        document.grid(row=0, column=0, padx=(p(18), p(14)), pady=p(20))
        document.create_rectangle(p(6), p(2), p(40), p(53), outline="#90AFE8", width=2)
        document.create_rectangle(p(1), p(24), p(47), p(45), outline=LIGHT, fill=LIGHT)
        document.create_text(p(24), p(34), text="BIN", font=("Consolas", 12, "bold"), fill=ACCENT)
        details = tk.Frame(file_panel, bg="#F7F9FC")
        details.grid(row=0, column=1, sticky="ew", padx=(0, p(18)), pady=p(18))
        self.filename_label = self.label(details, "请选择固件文件", font=("Microsoft YaHei UI", 14, "bold"),
                                         wraplength=p(290), justify="left")
        self.filename_label.pack(fill="x")
        self.filepath_label = self.label(details, "点击右上角“选择 BIN”", fg=MUTED, font=SMALL,
                                         wraplength=p(290), justify="left")
        self.filepath_label.pack(fill="x", pady=(p(7), 0))
        details.bind("<Configure>", lambda event: self.fit_file_labels(event.width))
        metrics = tk.Frame(firmware, bg=WHITE)
        metrics.pack(fill="x")
        self.size_value = self.metric(metrics, "文件大小", "—", 0)
        self.crc_value = self.metric(metrics, "CRC32", "—", 1)

        transfer_card = self.card(main)
        transfer_card.grid(row=1, column=1, sticky="nsew", pady=(0, p(18)))
        transfer = tk.Frame(transfer_card, bg=WHITE)
        transfer.pack(fill="both", expand=True, padx=p(22), pady=p(16))
        status_top = tk.Frame(transfer, bg=WHITE)
        status_top.pack(fill="x")
        self.label(status_top, "升级进度", font=("Microsoft YaHei UI", 16, "bold")).pack(side="left", anchor="n")
        self.percent_label = self.label(status_top, "0%", fg=ACCENT, font=("Segoe UI", 30, "bold"))
        self.percent_label.pack(side="right")
        self.status_label = self.label(transfer, "连接设备并选择固件后开始", fg=MUTED, wraplength=p(330), justify="left")
        self.status_label.pack(fill="x", pady=(p(4), p(10)))
        self.status_label.bind("<Configure>", lambda event: self.schedule_wrap("status", event.width))
        self.progress = ProgressBar(transfer)
        self.progress.pack(fill="x", pady=(0, p(9)))
        self.sent_label = self.label(transfer, "已确认  0 / 0 KB", fg=MUTED, font=SMALL)
        self.sent_label.pack(fill="x")
        self.timing_label = self.label(transfer, "速度 —    用时 —", fg=MUTED, font=SMALL)
        self.timing_label.pack(fill="x", pady=(p(4), p(12)))
        actions = tk.Frame(transfer, bg=WHITE)
        actions.pack(side="bottom", fill="x")
        self.start_button = self.button(actions, "开始下发", self.start_transfer, primary=True, state="disabled")
        self.start_button.pack(side="left", fill="x", expand=True)
        self.cancel_button = self.button(actions, "停止", self.cancel_transfer, state="disabled", padx=p(18))
        self.cancel_button.pack(side="right", padx=(p(12), 0))

        log_card = self.card(main)
        log_card.grid(row=2, column=0, columnspan=2, sticky="nsew")
        log_bar = tk.Frame(log_card, bg=WHITE)
        log_bar.pack(fill="x", padx=p(16), pady=(p(12), 0))
        tab_bar = tk.Frame(log_bar, bg=WHITE)
        tab_bar.pack(side="left")
        log_controls = tk.Frame(log_bar, bg=WHITE)
        log_controls.pack(side="right")

        self.log_tabs = ttk.Notebook(log_card, style="Logs.TNotebook")
        self.log_tabs.pack(fill="both", expand=True, padx=p(16), pady=(0, p(12)))
        self.runtime_tab = tk.Frame(self.log_tabs, bg=WHITE)
        self.raw_tab = tk.Frame(self.log_tabs, bg=WHITE)
        self.log_tabs.add(self.runtime_tab, text="运行日志")
        self.log_tabs.add(self.raw_tab, text="原始数据 RAW")
        self.tab_buttons = {}
        self.tab_underlines = {}
        for page, title in [(self.runtime_tab, "运行日志"), (self.raw_tab, "原始数据 RAW")]:
            item = tk.Frame(tab_bar, bg=WHITE)
            item.pack(side="left")
            button = tk.Button(
                item, text=title, command=lambda page=page: self.log_tabs.select(page),
                font=TITLE_FONT, width=12,
                bg=WHITE, fg=TEXT, activebackground=WHITE, activeforeground=TEXT,
                relief="flat", borderwidth=0, highlightthickness=0,
                padx=p(18), pady=p(8), cursor="hand2", takefocus=True)
            button.pack(fill="x")
            underline = tk.Frame(item, height=p(2), bg=WHITE)
            underline.pack(fill="x")
            self.tab_buttons[str(page)] = button
            self.tab_underlines[str(page)] = underline

        self.log_text = self.create_log_text(self.runtime_tab)
        self.raw_tab.grid_rowconfigure(0, weight=1)
        self.raw_tab.grid_columnconfigure(0, weight=1)
        self.raw_views = {}
        self.raw_view_frames = {}
        for mode in ("HEX", "ASCII"):
            panel = tk.Frame(self.raw_tab, bg=WHITE)
            panel.grid(row=0, column=0, sticky="nsew")
            self.raw_view_frames[mode] = panel
            self.raw_views[mode] = self.create_log_text(panel)
        self.raw_text = self.raw_views[self.raw_mode]
        self.raw_view_frames[self.raw_mode].tkraise()
        for tag, color in [("time", "#94A3B8"), ("info", TEXT), ("muted", MUTED),
                           ("success", "#16845A"), ("warning", "#AF7B1D"), ("error", RED), ("reply", ACCENT)]:
            self.log_text.tag_configure(tag, foreground=color)
        for text in self.raw_views.values():
            text.tag_configure("time", foreground="#94A3B8")
            text.tag_configure("TX", foreground=TEXT)
            text.tag_configure("RX", foreground=ACCENT)
            text.tag_configure("raw_TX", elide=False)
            text.tag_configure("raw_RX", elide=False)
        self.button(log_controls, "清空当前页", self.clear_active_log, fg=MUTED,
                    pady=p(3), padx=p(10)).pack(side="right")
        self.raw_stats_label = self.label(footer, "TX 0 B    RX 0 B", fg=MUTED,
                                           font=("Consolas", 11))
        self.raw_filter_frame = tk.Frame(log_controls, bg=WHITE)
        self.label(self.raw_filter_frame, "显示", fg=MUTED, font=SMALL).pack(side="left", padx=(0, p(7)))
        self.raw_filter_var = tk.StringVar(master=self.root, value="全部")
        self.raw_filter_combo = ttk.Combobox(
            self.raw_filter_frame, textvariable=self.raw_filter_var,
            values=("全部", "仅 TX", "仅 RX"), state="readonly", width=7,
            style="RawFilter.TCombobox", font=("Microsoft YaHei UI", 12))
        self.raw_filter_combo.pack(side="left")
        self.raw_filter_combo.bind("<<ComboboxSelected>>", self.on_raw_filter_changed)
        self.raw_format_frame = tk.Frame(log_controls, bg=WHITE)
        self.label(self.raw_format_frame, "格式", fg=MUTED, font=SMALL).pack(side="left", padx=(0, p(7)))
        self.raw_format_var = tk.StringVar(master=self.root, value="十六进制")
        self.raw_format_combo = ttk.Combobox(
            self.raw_format_frame, textvariable=self.raw_format_var,
            values=("十六进制", "ASCII"), state="readonly", width=8,
            style="RawFilter.TCombobox", font=("Microsoft YaHei UI", 12))
        self.raw_format_combo.pack(side="left")
        self.raw_format_combo.bind("<<ComboboxSelected>>", self.on_raw_format_changed)
        self.log_tabs.bind("<<NotebookTabChanged>>", self.on_log_tab_changed)
        self.on_log_tab_changed()

    def create_log_text(self, parent):
        parent.grid_rowconfigure(0, weight=1)
        parent.grid_columnconfigure(0, weight=1)
        text = tk.Text(parent, width=1, height=3, font=MONO, relief="flat", bg=WHITE,
                       fg=TEXT, bd=0, wrap="word", state="disabled",
                       padx=self.px(5), pady=self.px(6), spacing3=self.px(2))
        text.grid(row=0, column=0, sticky="nsew")
        vertical = ttk.Scrollbar(parent, command=text.yview)
        vertical.grid(row=0, column=1, sticky="ns")
        text.configure(yscrollcommand=vertical.set)
        return text

    def on_log_tab_changed(self, event=None):
        selected = self.log_tabs.select()
        for page, button in self.tab_buttons.items():
            active = page == selected
            button.configure(fg=TEXT)
            self.tab_underlines[page].configure(bg=ACCENT if active else WHITE)
        if selected == str(self.raw_tab):
            self.raw_stats_label.pack(side="left", padx=self.px(24))
            self.raw_filter_frame.pack(side="right", padx=(0, self.px(8)))
            self.raw_format_frame.pack(side="right", padx=(0, self.px(8)))
            self._raw_counts_dirty = True
        else:
            self.raw_stats_label.pack_forget()
            self.raw_filter_frame.pack_forget()
            self.raw_format_frame.pack_forget()

    def on_raw_filter_changed(self, event=None):
        selected = {"全部": "ALL", "仅 TX": "TX", "仅 RX": "RX"}[self.raw_filter_var.get()]
        if selected != self.raw_filter:
            self.raw_filter = selected
            for text in self.raw_views.values():
                follow_end = text.yview()[1] >= 0.995
                text.tag_configure("raw_TX", elide=selected == "RX")
                text.tag_configure("raw_RX", elide=selected == "TX")
                if follow_end:
                    text.see("end")

    def on_raw_format_changed(self, event=None):
        self.raw_mode = {"十六进制": "HEX", "ASCII": "ASCII"}[self.raw_format_var.get()]
        self.raw_text = self.raw_views[self.raw_mode]
        self.raw_view_frames[self.raw_mode].tkraise()

    def reset_raw_view(self):
        self.raw_render_index = 0
        self.raw_render_offset = 0
        for text in self.raw_views.values():
            text.configure(state="normal")
            text.delete("1.0", "end")
            text.configure(state="disabled")

    def clear_active_log(self):
        if self.log_tabs.select() == str(self.raw_tab):
            self.clear_raw()
        else:
            self.clear_log()

    def fit_file_labels(self, width):
        self.schedule_wrap("file", width)

    def schedule_wrap(self, area, width):
        width = max(self.px(180), width - self.px(4))
        self._wrap_widths[area] = width
        if width == self._applied_wrap_widths.get(area) or area in self._wrap_jobs:
            return
        # 连续尺寸事件合并处理，避免每移动一个像素都重新排版。
        self._wrap_jobs[area] = self.root.after(60, self.apply_wrap, area)

    def apply_wrap(self, area):
        self._wrap_jobs.pop(area, None)
        width = self._wrap_widths[area]
        if width == self._applied_wrap_widths.get(area):
            return
        self._applied_wrap_widths[area] = width
        if area == "file":
            self.filename_label.configure(wraplength=width)
            self.filepath_label.configure(wraplength=width)
        else:
            self.status_label.configure(wraplength=width)

    def metric(self, parent, title, value, column):
        parent.grid_columnconfigure(column, weight=1, uniform="metric")
        cell = tk.Frame(parent, bg=WHITE)
        cell.grid(row=0, column=column, sticky="ew", padx=(0, self.px(12)))
        self.label(cell, title, fg=MUTED, font=SMALL).pack(fill="x")
        result = self.label(cell, value, font=("Consolas", 16, "bold"))
        result.pack(fill="x", pady=(self.px(6), 0))
        return result

    def refresh_ports(self):
        previous = self.port_var.get()
        ports = sorted(list_ports.comports(), key=lambda item: item.device)
        self.port_map = {item.device: item.description for item in ports}
        self.port_combo["values"] = tuple(self.port_map)
        if previous in self.port_map:
            self.port_var.set(previous)
        else:
            self.port_var.set(next(iter(self.port_map), ""))
        self.show_port_description()

    def show_port_description(self):
        description = self.port_map.get(self.port_var.get(), "未检测到串口，请接入 USB 转串口后刷新。")
        self.port_description.configure(text=description)

    def toggle_connection(self):
        if self.connected:
            self.connecting = True
            self.worker.commands.put(("disconnect", ()))
        else:
            port = self.port_var.get()
            if not port:
                messagebox.showinfo("连接设备", "请先接入设备并选择串口。", parent=self.root)
                return
            self.connecting = True
            self.worker.commands.put(("connect", (port, int(self.baud_var.get()))))
        self.sync_controls()

    def choose_file(self):
        path = filedialog.askopenfilename(title="选择 APP 固件", parent=self.root,
                                          filetypes=[("BIN 固件", "*.bin")])
        if path:
            self.load_firmware(path)

    def load_firmware(self, path):
        try:
            data, crc = read_firmware(path)
        except (OSError, ValueError) as error:
            self.clear_firmware()
            self.log(str(error), "error")
            messagebox.showwarning("固件无法下发", str(error), parent=self.root)
            return
        self.path = Path(path)
        self.data = data
        name = self.path.name
        self.filename_label.configure(text=name if len(name) <= 52 else name[:49] + "…")
        self.filepath_label.configure(text=str(self.path.parent))
        self.size_value.configure(text=f"{len(data) / 1024:.2f} KB")
        self.crc_value.configure(text=f"0x{crc:08X}")
        self.progress.set(0)
        self.percent_label.configure(text="0%")
        self.sent_label.configure(text=f"已确认  0 / {len(data) / 1024:.2f} KB")
        self.timing_label.configure(text="速度 —    用时 —")
        self.log(f"已加载 {name} · {len(data):,} 字节 · CRC32 0x{crc:08X}", "info")
        self.idle_status()
        self.sync_controls()

    def clear_firmware(self):
        self.path = None
        self.data = None
        self.filename_label.configure(text="未选择有效固件")
        self.filepath_label.configure(text="请选择不超过 360 KB 的 BIN 文件")
        self.size_value.configure(text="—")
        self.crc_value.configure(text="—")
        self.progress.set(0)
        self.percent_label.configure(text="0%")
        self.sent_label.configure(text="已确认  0 / 0 KB")
        self.timing_label.configure(text="速度 —    用时 —")
        self.idle_status()
        self.sync_controls()

    def start_transfer(self):
        if self.busy or not self.connected or self.data is None:
            return
        # 在 UI 线程清除取消标志，避免工作线程稍后启动时覆盖用户的停止操作。
        self.worker.cancel.clear()
        self.busy = True
        self.progress.set(0)
        self.percent_label.configure(text="0%")
        self.sent_label.configure(text=f"已确认  0 / {len(self.data) / 1024:.2f} KB")
        self.timing_label.configure(text="速度 —    用时 —")
        self.status_label.configure(text="正在等待设备准备就绪…", fg=ACCENT)
        self.worker.commands.put(("send", (self.data,)))
        self.sync_controls()

    def cancel_transfer(self):
        self.worker.cancel.set()
        self.cancel_button.configure(state="disabled")
        self.status_label.configure(text="正在停止发送…", fg=MUTED)

    def sync_controls(self):
        locked = self.busy or self.connecting
        self.port_combo.configure(state="disabled" if self.connected or locked else "readonly")
        self.baud_combo.configure(state="disabled" if self.connected or locked else "readonly")
        self.refresh_button.configure(state="disabled" if self.connected or locked else "normal")
        self.connect_button.configure(text="连接中…" if self.connecting else ("断开连接" if self.connected else "连接设备"),
                                      state="disabled" if locked else "normal")
        self.file_button.configure(state="disabled" if self.busy else "normal")
        can_send = self.connected and self.data is not None and not locked
        self.start_button.configure(state="normal" if can_send else "disabled",
                                    bg=ACCENT if can_send else "#E8EDF5",
                                    highlightbackground=ACCENT if can_send else "#E8EDF5")
        self.cancel_button.configure(state="normal" if self.busy and not self.worker.cancel.is_set() else "disabled")

    def idle_status(self):
        if self.connected and self.data is not None:
            text = "固件与设备已就绪，可以开始下发"
        elif self.data is not None:
            text = "固件已就绪，连接设备后即可下发"
        elif self.connected:
            text = "设备已连接，请选择 APP 固件"
        else:
            text = "等待连接设备并选择固件"
        self.status_label.configure(text=text, fg=MUTED)

    def log(self, message, kind="info"):
        self.pending_logs.append((datetime.now().strftime("%H:%M:%S  "), message.rstrip(), kind))

    def flush_logs(self):
        if not self.pending_logs:
            return
        entries = []
        for _ in range(min(100, len(self.pending_logs))):
            stamp, message, kind = self.pending_logs.popleft()
            entries.extend((stamp, ("time",), message + "\n", (kind,)))
        follow_end = self.log_text.yview()[1] >= 0.995
        self.log_text.configure(state="normal")
        self.log_text.insert("end", *entries)
        lines = int(self.log_text.index("end-1c").split(".")[0])
        if lines > 5000:
            self.log_text.delete("1.0", f"{lines - 5000 + 1}.0")
        if follow_end:
            self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def clear_log(self):
        self.pending_logs.clear()
        self.log_text.configure(state="normal")
        self.log_text.delete("1.0", "end")
        self.log_text.configure(state="disabled")

    def record_raw(self, stamp, direction, data):
        offset = self.raw_tx_bytes if direction == "TX" else self.raw_rx_bytes
        self.raw_records.append((stamp, direction, offset, data))
        if direction == "TX":
            self.raw_tx_bytes += len(data)
        else:
            self.raw_rx_bytes += len(data)
        self._raw_counts_dirty = True

    def flush_raw(self):
        # 持续增量维护缓存视图，切换页签或筛选时不重放历史数据。
        if self._raw_counts_dirty:
            self.raw_stats_label.configure(text=f"TX {self.raw_tx_bytes:,} B    RX {self.raw_rx_bytes:,} B")
            self._raw_counts_dirty = False
        entries = {"HEX": [], "ASCII": []}
        budget = 4096
        records = 0
        while self.raw_render_index < len(self.raw_records) and budget >= 16 and records < 64:
            records += 1
            stamp, direction, _offset, data = self.raw_records[self.raw_render_index]
            scope = "raw_" + direction
            start = self.raw_render_offset
            display_stamp = stamp.split(".")[0] + "  "
            size = min(len(data) - start, budget)
            if size < len(data) - start:
                size -= size % 16
            chunk = data[start:start + size]
            for mode, formatter in (("HEX", raw_hex_lines), ("ASCII", raw_ascii_lines)):
                for line in formatter(chunk):
                    entries[mode].extend((display_stamp, ("time", scope),
                                          f"{direction}  {line}\n", (direction, scope)))
            budget -= size
            self.raw_render_offset += size
            if self.raw_render_offset == len(data):
                self.raw_render_index += 1
                self.raw_render_offset = 0
        for mode, segments in entries.items():
            if segments:
                text = self.raw_views[mode]
                follow_end = text.yview()[1] >= 0.995
                text.configure(state="normal")
                text.insert("end", *segments)
                if follow_end:
                    text.see("end")
                text.configure(state="disabled")

    def clear_raw(self):
        self.raw_records.clear()
        self.raw_tx_bytes = 0
        self.raw_rx_bytes = 0
        self._raw_counts_dirty = True
        self.reset_raw_view()
        self.flush_raw()

    def update_progress(self, confirmed, total, elapsed):
        percent = confirmed * 100 / total
        self.progress.set(percent)
        self.percent_label.configure(text=f"{int(percent)}%")
        self.status_label.configure(text="正在下发固件，等待设备逐块确认…", fg=ACCENT)
        self.sent_label.configure(text=f"已确认  {confirmed / 1024:.2f} / {total / 1024:.2f} KB")
        speed = confirmed / 1024 / max(elapsed, 0.001)
        self.timing_label.configure(text=f"{speed:.1f} KB/s    {elapsed:.1f} 秒")

    def poll_events(self):
        progress_update = None
        deadline = time.perf_counter() + 0.008
        for _ in range(200):
            if time.perf_counter() >= deadline:
                break
            try:
                name, args = self.events.get_nowait()
            except queue.Empty:
                break
            if name in ("completed", "recovering", "recovered", "disconnected", "connection_error") and progress_update is not None:
                self.update_progress(*progress_update)
                progress_update = None
            if name == "connected":
                self.connected, self.connecting = True, False
                self.connection_badge.configure(text=f"●  {args[0]} 已连接", fg=ACCENT, bg=LIGHT)
                self.log(f"已连接 {args[0]} · {args[1]} · 8N1", "success")
                self.idle_status()
                self.sync_controls()
            elif name in ("disconnected", "connection_error"):
                self.connected, self.connecting, self.busy = False, False, False
                self.connection_badge.configure(text="●  未连接设备", fg=MUTED, bg=BG)
                self.log("设备已断开。" if name == "disconnected" else "串口连接失败：" + args[0],
                         "muted" if name == "disconnected" else "error")
                self.idle_status()
                self.sync_controls()
            elif name == "log":
                self.log(*args)
            elif name == "raw":
                self.record_raw(*args)
            elif name == "rx":
                self.rx_text.extend(args[0])
                while b"\n" in self.rx_text:
                    line, _, rest = self.rx_text.partition(b"\n")
                    self.rx_text[:] = rest
                    self.log("RX  " + line.decode("utf-8", errors="replace").rstrip(), "muted")
                if len(self.rx_text) > 2048:
                    self.log("RX  " + self.rx_text.decode("utf-8", errors="replace"), "muted")
                    self.rx_text.clear()
            elif name == "progress":
                progress_update = args
            elif name == "completed":
                self.busy = False
                self.progress.set(100)
                self.percent_label.configure(text="100%")
                self.status_label.configure(text="下发完成，设备已提交升级并将自动重启", fg=ACCENT)
                self.sync_controls()
            elif name == "recovering":
                self.worker.cancel.set()
                self.status_label.configure(text=f"发送已停止，等待设备恢复接收（{args[0]} 秒）", fg=RED)
                self.cancel_button.configure(state="disabled")
            elif name == "recovered":
                self.busy = False
                self.status_label.configure(text="本次下发未完成，可以重新开始", fg=RED)
                self.sync_controls()
        if progress_update is not None:
            self.update_progress(*progress_update)
        self.flush_logs()
        self.flush_raw()
        self._poll_job = self.root.after(33, self.poll_events)

    def close(self):
        if self._poll_job is not None:
            self.root.after_cancel(self._poll_job)
        for job in self._wrap_jobs.values():
            self.root.after_cancel(job)
        self._wrap_jobs.clear()
        self.worker.cancel.set()
        self.worker.stopping.set()
        self.root.destroy()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--firmware", type=Path, help="打开时预先加载一个 BIN 文件")
    args = parser.parse_args()
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(1)
    except (AttributeError, OSError):
        pass
    root = tk.Tk()
    UpdateApp(root, args.firmware)
    root.mainloop()


if __name__ == "__main__":
    main()
