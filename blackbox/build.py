#!/usr/bin/env python3
"""
ESP32 Blackbox 构建脚本（跨平台版本）

跨平台构建脚本，支持 Windows 和 Linux，自动检测 ESP-IDF 安装位置。
无需硬编码路径，无需手动设置环境变量。

用法: python build.py <target> [action] [port]
  target: esp32c3 | esp32c6
  action: build | flash | monitor | clean  (默认: build)
  port:   串口端口号 (例如 COM3 或 /dev/ttyUSB0)
"""

import glob
import os
import shutil
import subprocess
import sys

# ============================================================================
# 常量定义
# ============================================================================

PROJECT_DIR = os.path.dirname(os.path.abspath(__file__))
LOG_FILE = os.path.join(PROJECT_DIR, "build_log.txt")

TARGETS = ["esp32c3", "esp32c6"]
ACTIONS = ["build", "flash", "monitor", "clean"]

IS_WINDOWS = os.name == "nt"


# ============================================================================
# 工具函数
# ============================================================================


def log(msg):
    """输出日志到终端和日志文件"""
    print(msg)
    with open(LOG_FILE, "a", encoding="utf-8") as f:
        f.write(msg + "\n")


def find_idf_py():
    """
    自动检测 ESP-IDF 安装位置，返回 idf.py 的完整路径。

    检测优先级：
    1. idf.py 已在 PATH 中（用户已运行 export.sh / export.bat）
    2. IDF_PATH 环境变量指向的安装目录
    3. 标准安装位置自动扫描

    返回: idf.py 路径字符串，未找到则返回 None
    """
    # 优先级 1: 检查 PATH 中是否已有 idf.py
    idf_in_path = shutil.which("idf.py")
    if idf_in_path:
        return idf_in_path

    # 优先级 2: 检查 IDF_PATH 环境变量
    idf_path = os.environ.get("IDF_PATH", "").strip()
    if idf_path:
        candidate = os.path.join(idf_path, "tools", "idf.py")
        if os.path.isfile(candidate):
            return candidate

    # 优先级 3: 扫描标准安装位置
    candidates = _get_standard_idf_locations()
    for loc in candidates:
        idf_py = os.path.join(loc, "tools", "idf.py")
        if os.path.isfile(idf_py):
            return idf_py

    return None


def _get_standard_idf_locations():
    """
    返回当前平台上 ESP-IDF 的标准安装位置列表。

    按可能性从高到低排列。
    """
    home = os.path.expanduser("~")
    locations = []

    if IS_WINDOWS:
        # Windows 标准安装位置
        locations.extend([
            os.path.join(home, "esp", "esp-idf"),
            os.path.join(home, ".espressif", "frameworks", "esp-idf-v6.0"),
            os.path.join(home, ".espressif", "frameworks", "esp-idf"),
        ])
        # IDF_TOOLS_PATH 下的框架目录（通配符匹配版本号）
        idf_tools = os.environ.get("IDF_TOOLS_PATH", "")
        if idf_tools:
            frameworks_dir = os.path.join(idf_tools, "frameworks")
            if os.path.isdir(frameworks_dir):
                # 匹配 esp-idf* 目录（含版本号）
                for d in sorted(glob.glob(os.path.join(frameworks_dir, "esp-idf*")), reverse=True):
                    locations.append(d)
        # 检查 C:\Espressif 等常见位置（非硬编码，仅作为搜索路径）
        for drive in _get_available_drives():
            locations.extend([
                os.path.join(drive, "Espressif", "frameworks", "esp-idf"),
                os.path.join(drive, "Espressif", "esp-idf"),
            ])
    else:
        # Linux/macOS 标准安装位置
        locations.extend([
            os.path.join(home, "esp", "esp-idf"),
            os.path.join(home, ".espressif", "frameworks", "esp-idf-v6.0"),
            os.path.join(home, ".espressif", "frameworks", "esp-idf"),
        ])
        # 全局安装位置
        locations.extend([
            "/opt/esp-idf",
            "/usr/local/esp-idf",
        ])
        # IDF_TOOLS_PATH 下的框架目录
        idf_tools = os.environ.get("IDF_TOOLS_PATH", "")
        if idf_tools:
            frameworks_dir = os.path.join(idf_tools, "frameworks")
            if os.path.isdir(frameworks_dir):
                for d in sorted(glob.glob(os.path.join(frameworks_dir, "esp-idf*")), reverse=True):
                    locations.append(d)

    return locations


def _get_available_drives():
    """获取 Windows 上可用的驱动器盘符列表"""
    if not IS_WINDOWS:
        return []
    drives = []
    for letter in "CDEFGH":
        drive = f"{letter}:\\"
        if os.path.isdir(drive):
            drives.append(drive)
    return drives


def print_install_help():
    """打印 ESP-IDF 安装指南"""
    log("")
    log("=" * 60)
    log(" ERROR: 无法找到 ESP-IDF 安装")
    log("=" * 60)
    log("")
    log("请选择以下任一方式安装 ESP-IDF:")
    log("")
    log("  方式一: 官方安装器（推荐 Windows 用户）")
    log("    https://dl.espressif.com/dl/esp-idf/")
    log("")
    log("  方式二: 手动安装")
    log("    git clone -b v6.0 --recursive https://github.com/espressif/esp-idf.git")
    log("    cd esp-idf && ./install.sh")
    log("")
    log("安装后，执行以下任一操作使此脚本可找到 idf.py:")
    log("")
    log("  1. 设置环境变量:")
    if IS_WINDOWS:
        log("     set IDF_PATH=C:\\path\\to\\esp-idf")
    else:
        log("     export IDF_PATH=~/esp/esp-idf")
    log("")
    log("  2. 或者先运行 ESP-IDF 环境脚本:")
    if IS_WINDOWS:
        log("     C:\\path\\to\\esp-idf\\export.bat")
    else:
        log("     source ~/esp/esp-idf/export.sh")
    log("")
    log("  3. 或者克隆到标准位置:")
    log("     ~/esp/esp-idf")
    log("")
    log("=" * 60)


def run_cmd(cmd_list, cwd=None, timeout=600):
    """
    运行命令并返回 (returncode, stdout, stderr)

    直接调用 idf.py，由 ESP-IDF 内部处理所有工具路径。
    不手动构造 PATH 或设置 IDF_TOOLS_PATH 等环境变量。
    """
    result = subprocess.run(
        cmd_list,
        cwd=cwd or PROJECT_DIR,
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    return result.returncode, result.stdout, result.stderr


def build_idf_cmd(idf_py_path, extra_args):
    """
    构造调用 idf.py 的完整命令列表。

    跨平台处理：直接调用 idf.py（通过 python 解释器）。
    """
    if IS_WINDOWS:
        # Windows: 使用当前 python 解释器调用 idf.py 脚本
        return [sys.executable, idf_py_path] + extra_args
    else:
        # Linux/macOS: idf.py 有 shebang，可直接执行；但通过 python 调用更可靠
        return [sys.executable, idf_py_path] + extra_args


def get_current_target():
    """从 sdkconfig 读取当前配置的目标芯片"""
    sdkconfig_path = os.path.join(PROJECT_DIR, "sdkconfig")
    if not os.path.isfile(sdkconfig_path):
        return None
    with open(sdkconfig_path, "r", encoding="utf-8") as f:
        for line in f:
            if line.startswith("CONFIG_IDF_TARGET="):
                return line.strip().split('"')[1]
    return None


def print_firmware_info():
    """构建成功后打印固件信息"""
    firmware = os.path.join(PROJECT_DIR, "build", "esp32-blackbox.bin")
    if os.path.isfile(firmware):
        size = os.path.getsize(firmware)
        log(f"  固件:   {firmware}")
        log(f"  大小:   {size:,} 字节 ({size / 1024:.1f} KB)")


# ============================================================================
# 核心构建逻辑
# ============================================================================


def do_clean(idf_py, target):
    """全量清理后重新构建"""
    build_dir = os.path.join(PROJECT_DIR, "build")
    sdkconfig = os.path.join(PROJECT_DIR, "sdkconfig")
    sdkconfig_old = sdkconfig + ".old"

    # 第一步: 清理构建产物和配置文件
    removed = False
    if os.path.isdir(build_dir):
        shutil.rmtree(build_dir)
        log("[1/3] 已清理 build 目录")
        removed = True
    for f in [sdkconfig, sdkconfig_old]:
        if os.path.isfile(f):
            os.remove(f)
            log(f"[1/3] 已删除 {os.path.basename(f)}")
            removed = True
    if not removed:
        log("[1/3] 无需清理（构建目录和配置文件不存在）")

    # 第二步: 设置目标芯片
    log(f"[2/3] 设置目标芯片: {target}...")
    cmd = build_idf_cmd(idf_py, ["set-target", target])
    rc, out, err = run_cmd(cmd)
    log(out)
    if err.strip():
        log(err)
    if rc != 0:
        log(f"ERROR: set-target 失败 (rc={rc})")
        sys.exit(rc)

    # 第三步: 构建
    log("[3/3] 构建中...")
    cmd = build_idf_cmd(idf_py, ["build"])
    rc, out, err = run_cmd(cmd)
    log(out)
    if err.strip():
        log(err)
    if rc == 0:
        log("=" * 50)
        log(" 构建成功 (CLEAN BUILD)")
        log("=" * 50)
        print_firmware_info()
    else:
        log(f"ERROR: 构建失败 (rc={rc})")
    sys.exit(rc)


def do_build(idf_py, target):
    """增量构建（需要时自动切换目标芯片）"""
    current = get_current_target()
    need_set_target = False

    if current is None:
        log(f"[1/3] 首次构建 - 设置目标芯片: {target}...")
        need_set_target = True
    elif current != target:
        log(f"[1/3] 目标芯片变更: {current} -> {target}，重新配置...")
        need_set_target = True
    else:
        log(f"[1/3] 目标芯片: {target} (已配置)")

    if need_set_target:
        cmd = build_idf_cmd(idf_py, ["set-target", target])
        rc, out, err = run_cmd(cmd)
        log(out)
        if err.strip() and "deprecated" not in err.lower():
            log(err)
        if rc != 0:
            log(f"ERROR: set-target 失败 (rc={rc})")
            sys.exit(rc)

    # 构建
    log("[2/3] 构建中...")
    cmd = build_idf_cmd(idf_py, ["build"])
    rc, out, err = run_cmd(cmd)
    log(out)
    if err.strip() and ("warning" in err.lower() or "error" in err.lower()):
        log(err)
    if rc != 0:
        log(f"ERROR: 构建失败 (rc={rc})")
        sys.exit(rc)

    return rc


def do_flash(idf_py, target, port=None):
    """构建并烧录固件"""
    do_build(idf_py, target)

    flash_args = []
    if port:
        flash_args.extend(["-p", port])

    log("[3/3] 烧录中...")
    cmd = build_idf_cmd(idf_py, flash_args + ["flash"])
    rc, out, err = run_cmd(cmd, timeout=300)
    log(out)
    if err.strip():
        log(err)
    if rc != 0:
        log(f"ERROR: 烧录失败 (rc={rc})")
        sys.exit(rc)

    log(" 烧录成功")


def do_monitor(idf_py, target, port=None):
    """构建、烧录并进入串口监控"""
    do_flash(idf_py, target, port)

    log("[INFO] 烧录完成。串口监控需要交互式终端。")
    log(f"[INFO] 请手动运行: idf.py {'-p ' + port + ' ' if port else ''}monitor")
    log("=" * 50)
    log(" 构建并烧录成功")
    log("=" * 50)
    print_firmware_info()


# ============================================================================
# 入口
# ============================================================================


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    target = sys.argv[1].lower()
    action = (sys.argv[2] if len(sys.argv) > 2 else "build").lower()
    port = sys.argv[3] if len(sys.argv) > 3 else None

    if target not in TARGETS:
        log(f"ERROR: 无效目标 '{target}'，可选: {TARGETS}")
        sys.exit(1)
    if action not in ACTIONS:
        log(f"ERROR: 无效操作 '{action}'，可选: {ACTIONS}")
        sys.exit(1)

    # 清理旧日志
    if os.path.isfile(LOG_FILE):
        os.remove(LOG_FILE)

    log("=" * 50)
    log(" ESP32 Blackbox 构建脚本")
    log(f" 目标:   {target}")
    log(f" 操作:   {action}")
    if port:
        log(f" 端口:   {port}")
    log(f" 项目:   {PROJECT_DIR}")
    log("=" * 50)

    # 检测 ESP-IDF
    idf_py = find_idf_py()
    if idf_py is None:
        print_install_help()
        sys.exit(1)

    # 推导 IDF_PATH（用于信息展示）
    idf_path = os.path.dirname(os.path.dirname(idf_py))
    log(f"[INFO] ESP-IDF: {idf_path}")
    log(f"[INFO] idf.py:  {idf_py}")
    log("")

    # 执行操作
    if action == "clean":
        do_clean(idf_py, target)
    elif action == "build":
        do_build(idf_py, target)
        log("=" * 50)
        log(" 构建成功")
        log("=" * 50)
        print_firmware_info()
    elif action == "flash":
        do_flash(idf_py, target, port)
    elif action == "monitor":
        do_monitor(idf_py, target, port)

    sys.exit(0)


if __name__ == "__main__":
    main()
