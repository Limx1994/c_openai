#!/usr/bin/env python3
"""
C-OpenAI 一键编译脚本
支持 lwIP 嵌入式后端（ARM GCC）和 libcurl 后端（系统 GCC）
"""

import os
import sys
import glob
import shutil
import subprocess
import argparse
import shlex
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

# ============================================================
# 配置
# ============================================================
SCRIPT_DIR = Path(__file__).parent.resolve()
SRC_DIR = SCRIPT_DIR / "src"
INC_DIR = SCRIPT_DIR / "include"
BUILD_DIR = SCRIPT_DIR / "build_output"

# 默认 STM32CubeIDE 工具链搜索路径
STM32IDE_SEARCH_PATHS = [
    r"D:\APPS\st32ide\STM32CubeIDE_2.1.1\STM32CubeIDE\plugins",
    r"C:\ST\STM32CubeIDE_2.1.1\STM32CubeIDE\plugins",
    os.path.expanduser(r"~\STM32CubeIDE\plugins"),
]

# 源文件
CORE_SRCS = [
    SRC_DIR / "openai_client.c",
    SRC_DIR / "openai_json.c",
]
LWIP_SRC = SRC_DIR / "openai_http_lwip.c"
CURL_SRC = SRC_DIR / "openai_http_curl.c"


# ============================================================
# 工具链发现
# ============================================================
def find_arm_toolchain():
    """在 STM32CubeIDE 插件目录中查找 ARM GCC 工具链"""
    compiler = shutil.which("arm-none-eabi-gcc")
    if compiler:
        return Path(compiler).parent
    for base in STM32IDE_SEARCH_PATHS:
        if not os.path.exists(base):
            continue
        pattern = os.path.join(base, "com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*", "tools", "bin", "arm-none-eabi-gcc.exe")
        matches = glob.glob(pattern)
        if matches:
            # 取最新版本
            matches.sort(reverse=True)
            return Path(matches[0]).parent
    return None


def find_lwip_include():
    """查找 lwIP 头文件目录"""
    candidates = [
        SCRIPT_DIR / "third_party" / "lwip" / "src" / "include",
    ]
    for p in candidates:
        if p.exists() and (p / "lwip" / "sockets.h").exists():
            return p
    return None


def find_lwipopts():
    """查找 lwipopts.h"""
    candidates = [
        SCRIPT_DIR / "third_party" / "lwip" / "contrib" / "examples" / "example_app",
        SCRIPT_DIR / "third_party" / "lwip" / "contrib" / "ports" / "unix" / "lib",
    ]
    for p in candidates:
        if (p / "lwipopts.h").exists():
            return p
    return None


def find_lwipopts_default():
    """查找默认 lwipopts.h（项目自带的）"""
    default = SCRIPT_DIR / "third_party" / "lwip_port" / "lwipopts.h"
    if default.exists():
        return default.parent
    return None


def find_mbedtls_include():
    """查找 mbedTLS 头文件目录"""
    candidates = [
        SCRIPT_DIR / "third_party" / "mbedtls",
    ]
    for p in candidates:
        if p.exists() and (p / "include").exists():
            return p
    return None


def find_lwip_arch_dir():
    """查找 lwIP arch 目录（提供 arch/cc.h）"""
    candidates = [
        SCRIPT_DIR / "third_party" / "lwip_port",
        SCRIPT_DIR / "third_party" / "lwip" / "contrib" / "ports" / "win32" / "include",
        SCRIPT_DIR / "third_party" / "lwip" / "contrib" / "ports" / "unix" / "port" / "include",
    ]
    for p in candidates:
        if p.exists() and (p / "arch" / "cc.h").exists():
            return p
    return None


# ============================================================
# 编译
# ============================================================
def run_cmd(cmd, verbose=False):
    """执行命令并返回成功/失败"""
    if verbose:
        print(f"  > {' '.join(str(c) for c in cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        output = "\n".join(part.strip() for part in (result.stdout, result.stderr)
                           if part.strip())
        print(f"  [ERROR] {output}")
        return False
    return True


def compile_file(compiler, src, obj, includes, defines, cflags, verbose=False):
    """编译单个 .c 文件为 .o"""
    cmd = [
        str(compiler),
        "-c", "-w", "-std=gnu11", *cflags,
        "-o", str(obj),
        str(src),
    ]
    for inc in includes:
        cmd.extend(["-I", str(inc)])
    for d, v in defines.items():
        cmd.extend([f"-D{d}={v}"])
    return run_cmd(cmd, verbose)


def compile_sources(compiler, sources, obj_dir, includes, defines,
                    cflags, jobs, verbose=False):
    """并行编译源文件，返回按输入顺序排列的目标文件列表。"""
    work = [(src, obj_dir / (src.stem + ".o")) for src in sources]
    results = {}
    with ThreadPoolExecutor(max_workers=jobs) as executor:
        futures = {
            executor.submit(compile_file, compiler, src, obj, includes,
                            defines, cflags, verbose): (src, obj)
            for src, obj in work
        }
        for future in as_completed(futures):
            src, obj = futures[future]
            try:
                ok = future.result()
            except Exception as exc:
                print(f"  [ERROR] 编译 {src.name} 时发生异常: {exc}")
                ok = False
            print(f"  编译 {src.name} ... {'OK' if ok else 'FAIL'}")
            results[src] = (ok, obj)

    failed = [src for src, _ in work if not results.get(src, (False, None))[0]]
    if failed:
        print(f"  [ERROR] {len(failed)} 个源文件编译失败: "
              + ", ".join(src.name for src in failed))
        return None
    return [results[src][1] for src, _ in work]


def create_static_lib(archiver, obj_files, output, verbose=False):
    """打包为静态库 .a"""
    # 清理旧文件
    if output.exists():
        output.unlink()
    cmd = [str(archiver), "rcs", str(output)] + [str(o) for o in obj_files]
    return run_cmd(cmd, verbose)


def find_tool(toolchain_dir, name):
    """在工具链目录中查找带或不带 .exe 后缀的程序。"""
    for candidate in (toolchain_dir / name, toolchain_dir / f"{name}.exe"):
        if candidate.exists():
            return candidate
    return None


def build_lwip(toolchain_dir, lwip_inc, lwipopts_dir, mbedtls_inc,
                defines, cflags, jobs, arch_dir=None, verbose=False):
    """编译 lwIP 后端库"""
    print("\n[1/3] 编译 lwIP 后端 (libopenai_lwip.a)")

    compiler = find_tool(toolchain_dir, "arm-none-eabi-gcc")
    archiver = find_tool(toolchain_dir, "arm-none-eabi-ar")
    if not compiler or not archiver:
        print(f"  [ERROR] ARM 工具链不完整: {toolchain_dir}")
        return False

    includes = [INC_DIR, lwip_inc, lwipopts_dir]
    if arch_dir:
        includes.append(arch_dir)
    if mbedtls_inc:
        includes.append(mbedtls_inc / "include")
        # mbedTLS 3.x+ has headers in tf-psa-crypto subdirectory
        tf_psa = mbedtls_inc / "tf-psa-crypto" / "include"
        if tf_psa.exists():
            includes.append(tf_psa)
        builtin = mbedtls_inc / "tf-psa-crypto" / "drivers" / "builtin" / "include"
        if builtin.exists():
            includes.append(builtin)
        core = mbedtls_inc / "tf-psa-crypto" / "core"
        if core.exists():
            includes.append(core)
        lib = mbedtls_inc / "library"
        if lib.exists():
            includes.append(lib)

    obj_dir = BUILD_DIR / "lwip"
    obj_dir.mkdir(parents=True, exist_ok=True)

    all_srcs = CORE_SRCS + [LWIP_SRC]
    obj_files = compile_sources(compiler, all_srcs, obj_dir, includes, defines,
                                cflags, jobs, verbose)
    if obj_files is None:
        return False

    output = BUILD_DIR / "libopenai_lwip.a"
    print(f"  打包 {output.name} ...", end=" ")
    if create_static_lib(archiver, obj_files, output, verbose):
        print("OK")
        return True
    else:
        print("FAIL")
        return False


def find_curl_include():
    """查找 curl 头文件目录"""
    candidates = [
        SCRIPT_DIR / "third_party" / "libcurl" / "include",
    ]
    for p in candidates:
        if p.exists() and (p / "curl" / "curl.h").exists():
            return p
    return None


def build_curl(defines, cflags, jobs, curl_inc_dir=None, verbose=False):
    """编译 libcurl 后端库"""
    print("\n[2/3] 编译 libcurl 后端 (libopenai_curl.a)")

    gcc_path = shutil.which("gcc") or shutil.which("cc")
    ar_path = shutil.which("ar")
    if not gcc_path or not ar_path:
        print("  [ERROR] curl 后端需要 host GCC/cc 和 ar")
        return False
    compiler = Path(gcc_path)
    archiver = Path(ar_path)

    includes = [INC_DIR]
    if curl_inc_dir:
        includes.append(curl_inc_dir)

    obj_dir = BUILD_DIR / "curl"
    obj_dir.mkdir(parents=True, exist_ok=True)

    all_srcs = CORE_SRCS + [CURL_SRC]
    obj_files = compile_sources(compiler, all_srcs, obj_dir, includes, defines,
                                cflags, jobs, verbose)
    if obj_files is None:
        return False

    output = BUILD_DIR / "libopenai_curl.a"
    print(f"  打包 {output.name} ...", end=" ")
    if create_static_lib(archiver, obj_files, output, verbose):
        print("OK")
        return True
    else:
        print("FAIL")
        return False


# ============================================================
# 主流程
# ============================================================
def main():
    parser = argparse.ArgumentParser(description="C-OpenAI 一键编译脚本")
    parser.add_argument("--backend", choices=["lwip", "curl", "all"], default="all",
                        help="选择编译后端 (默认: all)")
    parser.add_argument("--toolchain", type=str, default=None,
                        help="ARM GCC 工具链 bin 目录路径")
    parser.add_argument("--lwip-include", type=str, default=None,
                        help="lwIP 头文件目录路径")
    parser.add_argument("--lwipopts", type=str, default=None,
                        help="lwipopts.h 所在目录路径")
    parser.add_argument("--mbedtls", type=str, default=None,
                        help="mbedTLS 根目录路径")
    parser.add_argument("--timeout", type=int, default=30,
                        help="HTTP 超时秒数 (默认: 30)")
    parser.add_argument("--no-tls", action="store_true",
                        help="禁用 lwIP TLS 支持")
    parser.add_argument("--log-level", type=int, default=0, choices=[0, 1, 2, 3],
                        help="日志级别 (0=error 1=warn 2=info 3=debug)")
    parser.add_argument("--build-type", choices=["Debug", "Release", "MinSizeRel"],
                        default="MinSizeRel",
                        help="优化配置 (默认: MinSizeRel/-Os)")
    parser.add_argument("--jobs", type=int, default=10,
                        help="并行编译任务数，最小 10 (默认: 10)")
    parser.add_argument("--mcu-flags", type=str,
                        default="-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16",
                        help="lwIP ARM 编译器的 MCU/ABI 参数")
    parser.add_argument("--clean", action="store_true",
                        help="清理构建目录")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="显示详细编译命令")
    args = parser.parse_args()
    if args.jobs < 10:
        parser.error("--jobs 必须大于或等于 10")

    # 清理
    if args.clean:
        if BUILD_DIR.exists():
            shutil.rmtree(BUILD_DIR)
            print("已清理构建目录")
        return 0

    print("=" * 50)
    print("  C-OpenAI 一键编译")
    print("=" * 50)

    # 查找工具链
    toolchain_dir = None
    if args.toolchain:
        toolchain_dir = Path(args.toolchain)
    else:
        toolchain_dir = find_arm_toolchain()

    if toolchain_dir:
        print(f"\nARM 工具链: {toolchain_dir}")
    else:
        print("\n未找到 ARM 工具链")
    host_gcc = shutil.which("gcc") or shutil.which("cc")
    print(f"Host C 编译器: {host_gcc or '未找到'}")
    print(f"并行任务数: {args.jobs}")

    # 查找 lwIP
    lwip_inc = Path(args.lwip_include) if args.lwip_include else find_lwip_include()
    lwipopts_dir = Path(args.lwipopts) if args.lwipopts else (find_lwipopts_default() or find_lwipopts())
    mbedtls_inc = Path(args.mbedtls) if args.mbedtls else find_mbedtls_include()
    arch_dir = find_lwip_arch_dir()

    if lwip_inc:
        print(f"lwIP 头文件: {lwip_inc}")
    if lwipopts_dir:
        print(f"lwipopts.h:  {lwipopts_dir}")
    if arch_dir:
        print(f"arch 目录:   {arch_dir}")
    if mbedtls_inc:
        print(f"mbedTLS:     {mbedtls_inc}")

    # 编译定义
    common_defs = {
        "OPENAI_LOG_ENABLED": 1 if args.log_level > 0 else 0,
        "OPENAI_LOG_LEVEL": args.log_level,
        "OPENAI_TIMEOUT": args.timeout,
    }

    lwip_defs = {
        **common_defs,
        "OPENAI_USE_LWIP": 1,
        "OPENAI_USE_TLS": 0 if args.no_tls else 1,
    }
    if not args.no_tls:
        lwip_defs.update({
            "LWIP_ALTCP": 1,
            "LWIP_ALTCP_TLS": 1,
            "LWIP_ALTCP_TLS_MBEDTLS": 1,
        })

    curl_defs = {
        **common_defs,
        "OPENAI_USE_TLS": 0 if args.no_tls else 1,
    }

    optimization_flags = {
        "Debug": ["-O0", "-g"],
        "Release": ["-O2", "-DNDEBUG"],
        "MinSizeRel": ["-Os", "-DNDEBUG"],
    }[args.build_type]
    lwip_cflags = shlex.split(args.mcu_flags) + optimization_flags

    BUILD_DIR.mkdir(parents=True, exist_ok=True)

    results = {}

    # 编译 lwIP
    if args.backend in ("lwip", "all"):
        if not toolchain_dir:
            print("\n[ERROR] lwIP 后端需要 ARM 工具链 (--toolchain)")
            results["lwip"] = False
        elif not lwip_inc:
            print("\n[ERROR] lwIP 后端需要 lwIP 头文件 (--lwip-include)")
            results["lwip"] = False
        elif not lwipopts_dir:
            print("\n[ERROR] lwIP 后端需要 lwipopts.h (--lwipopts)")
            results["lwip"] = False
        elif not args.no_tls and not mbedtls_inc:
            print("\n[ERROR] 启用 TLS 时 lwIP 后端需要 mbedTLS (--mbedtls)")
            results["lwip"] = False
        else:
            results["lwip"] = build_lwip(
                toolchain_dir, lwip_inc, lwipopts_dir, mbedtls_inc,
                lwip_defs, lwip_cflags, args.jobs, arch_dir, args.verbose)

    # 查找 curl 头文件
    curl_inc_dir = find_curl_include()
    if curl_inc_dir:
        print(f"curl 头文件: {curl_inc_dir}")

    # 编译 curl（仅支持系统 GCC，ARM 工具链缺少系统头文件）
    if args.backend in ("curl", "all"):
        if not curl_inc_dir:
            print("\n[ERROR] libcurl 后端需要 curl 头文件 (third_party/libcurl/include)")
            results["curl"] = False
        else:
            results["curl"] = build_curl(
                curl_defs, optimization_flags, args.jobs, curl_inc_dir, args.verbose)

    # 结果
    success_count = sum(1 for ok in results.values() if ok)
    failure_count = sum(1 for ok in results.values() if not ok)
    success = bool(results) and failure_count == 0
    print("\n" + "=" * 50)
    if success:
        print("  编译成功!")
        print(f"  输出目录: {BUILD_DIR}")
        for f in BUILD_DIR.glob("*.a"):
            size_kb = f.stat().st_size / 1024
            print(f"    {f.name}  ({size_kb:.1f} KB)")
    else:
        print("  编译失败!")
    print(f"  后端结果: 成功 {success_count}, 失败 {failure_count}")
    print("=" * 50)

    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
