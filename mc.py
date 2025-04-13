#!/usr/bin/env python3
import os
import sys
import json
import hashlib
import requests
import shutil
from pathlib import Path

def calculate_md5(file_path):
    """计算文件的MD5哈希值"""
    hash_md5 = hashlib.md5()
    with open(file_path, "rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            hash_md5.update(chunk)
    return hash_md5.hexdigest()

def is_executable(file_path):
    """检查文件是否可执行"""
    # 检查文件扩展名是否为.exe
    return Path(file_path).suffix.lower() == '.exe'

def get_file_permissions(file_path):
    """获取文件权限"""
    # exe文件使用511权限，其他文件使用438权限
    if is_executable(file_path):
        return 511
    else:
        return 438

def parse_args(args):
    """解析命令行参数为字典"""
    params = {}
    for arg in args:
        if "=" in arg:
            key, value = arg.split("=", 1)
            params[key.lower()] = value
    return params

def main():
    # 解析命令行参数
    params = parse_args(sys.argv[1:])

    # 检查必需参数
    if "id" not in params:
        print("错误：未提供版本ID")
        print("用法：python 脚本.py id=版本ID [url=URL前缀]")
        return 1

    version_id = params["id"]

    # 获取URL前缀，如果提供的话
    url_prefix = params.get("url", "https://mmc.miniskins.top")

    # 如果输出目录不存在则创建
    output_dir = Path(os.getcwd()) / "qiniu" / "win32"
    output_dir.mkdir(parents=True, exist_ok=True)
    miniskins_dir = Path(os.getcwd()) / "qiniu" / "MiniSkins"
    miniskins_dir.mkdir(parents=True, exist_ok=True)

    # 打印环境信息，便于调试
    print(f"当前工作目录: {os.getcwd()}")
    print(f"版本ID: {version_id}")
    print(f"URL前缀: {url_prefix}")

    # 功能1：更新index.json
    try:
        # 获取JSON数据
        response = requests.get(f"{url_prefix}/win32/index.json")
        response.raise_for_status()  # 如果请求失败则抛出异常
        data = response.json()

        # 获取版本数组
        versions = data.get("Versions", [])

        # 检查当前版本是否存在
        version_exists = False
        for version in versions:
            if version["Id"] == version_id:
                version_exists = True
                break

        # 如果当前版本不存在，则添加新版本
        if not version_exists:
            versions.append({
                "Id": version_id,
                "Name": "0.0.0"
            })

        # 保存更新后的数据
        index_json_path = output_dir / "index.json"
        with open(index_json_path, 'w', encoding='utf-8') as f:
            json.dump(data, f, indent=4)

        print(f"已成功更新index.json文件：{index_json_path}")

    except requests.exceptions.RequestException as e:
        print(f"错误：获取JSON数据失败 - {e}")
        return 1
    except Exception as e:
        print(f"错误：处理index.json时发生错误 - {e}")
        return 1

    # 功能2：处理install目录中的文件
    try:
        install_dir = Path(os.getcwd()) / "install"
        if not install_dir.exists():
            print(f"警告：install目录不存在 - {install_dir}")
            return 0

        # 创建文件列表
        files_list = []
        file_paths = list(install_dir.glob('**/*'))

        # 首先收集所有文件信息
        files_info = []
        for file_path in file_paths:
            if file_path.is_file():
                # 计算相对路径
                relative_path = file_path.relative_to(install_dir)

                # 计算MD5值
                md5_hash = calculate_md5(file_path)

                # 判断是否可执行
                executable = is_executable(file_path)

                # 获取统一的权限设置
                permissions = get_file_permissions(file_path)
                # 构建URL
                # 获取相对目录路径和文件名
                relative_dir = str(relative_path.parent).replace("\\", "/") if relative_path.parent != Path(".") else ""
                filename = relative_path.name
                # 构建正确的URL格式
                if relative_dir:
                    url = f"{url_prefix}/MiniSkins/{relative_dir}/{md5_hash}-{filename}"
                else:
                    url = f"{url_prefix}/MiniSkins/{md5_hash}-{filename}"
                # 新文件名
                new_filename = f"{md5_hash}-{file_path.name}"

                # 收集文件信息
                files_info.append({
                    "old_path": file_path,
                    "relative_path": relative_path,
                    "md5": md5_hash,
                    "executable": executable,
                    "permissions": permissions,
                    "url": url,
                    "new_filename": new_filename
                })

        # 然后处理文件重命名和JSON生成
        for info in files_info:
            # 创建与源文件相同的目录结构
            relative_dir = info["relative_path"].parent
            target_dir = miniskins_dir / relative_dir
            target_dir.mkdir(parents=True, exist_ok=True)

            # 新路径包含完整的目录结构
            new_path = target_dir / info["new_filename"]

            # 复制文件（如果新文件名与旧文件名不同）
            if info["old_path"].name != info["new_filename"]:
                # 如果目标文件已存在，先删除它
                if new_path.exists():
                    os.remove(new_path)
                # 复制文件而不是重命名，保留原文件
                shutil.copy2(info["old_path"], new_path)
                print(f"已复制文件：{info['old_path'].name} -> {str(relative_dir / info['new_filename'])}")

            # 添加到JSON文件列表
            file_info = {
                "Executable": info["executable"],
                "MD5": info["md5"],
                "Path": str(info["relative_path"]).replace("\\", "/"),  # 确保使用正斜杠
                "Perms": info["permissions"],
                "Sources": [
                    {
                        "SourceType": "http",
                        "Url": info["url"]
                    }
                ]
            }

            files_list.append(file_info)

        # 创建完整的JSON数据
        version_data = {
            "ApiVersion": 0,
            "Id": version_id,
            "Name": "0.0.0",
            "Files": files_list
        }

        # 保存JSON数据
        version_json_path = output_dir / f"{version_id}.json"
        with open(version_json_path, 'w', encoding='utf-8') as f:
            json.dump(version_data, f, indent=4)

        print(f"已成功生成版本文件：{version_json_path}")
        return 0

    except Exception as e:
        print(f"错误：处理install目录文件时发生错误 - {e}")
        return 1

if __name__ == "__main__":
    sys.exit(main())