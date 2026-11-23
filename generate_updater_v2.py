#!/usr/bin/env python3
"""生成并校验 updater v2 服务端文档。

Updater v2 读取 <site>/updater/ 目录下的文档树：

    keys/<version>.json (+ .sig)   根密钥/在线密钥文档，通过签名逐版链接
    mirrors.json (+ .sig)          文件站镜像分组
    tags.json (+ .sig)             latest/testing/stable 构建号
    builds/<build>.json (+ .sig)   构建清单，包含各变体的文件列表
    files/<xx>/<sha256>            按内容寻址的文件内容

每个文档都是严格 JSON（键和类型必须精确匹配、时间使用 UTC、十六进制小写、
路径已排序），并使用 Ed25519 签名。签名文件每个签名者一行
"<64 位十六进制密钥> <128 位十六进制签名>"，且必须以换行结尾。

本脚本与 launcher/updater/v2/Formats.cpp 中的检查保持一致，因此它写出的
文档树就是启动器可以接受的。脚本不依赖第三方库，可在 Python 3.7 及以上运行。

典型发布流程（私钥始终保留在发布机器上）：

    python tools/generate_updater_v2.py keygen --out keys --name root1 --name root2 --name online1

    python tools/generate_updater_v2.py keys --tree qiniu/updater --version 1 --expires 2027-01-01T00:00:00Z --root-pub <root1 public hex> --root-pub <root2 public hex> --online-pub <online1 public hex> --sign-key keys/root1.pem --sign-key keys/root2.pem

    python tools/generate_updater_v2.py publish --tree qiniu/updater --install install --build 12 --commit <40 hex digits> --built 2026-11-07T00:00:00Z --variant windows-x86-xp --requires-file requires.json --sign-key keys/online1.pem

    python tools/generate_updater_v2.py mirrors --tree qiniu/updater --group https://files.example.com/ --sign-key keys/online1.pem

    python tools/generate_updater_v2.py tags --tree qiniu/updater --serial 7 --latest 12 --testing 12 --stable 12 --sign-key keys/online1.pem

    python tools/generate_updater_v2.py validate --tree qiniu/updater --root-pub <root1 public hex> --root-pub <root2 public hex> --check-content
"""

import argparse
import base64
import datetime
import hashlib
import json
import os
import re
import secrets
import shutil
import sys
from pathlib import Path


# ---------------------------------------------------------------------------
# Limits, patterns and paths, matching launcher/updater/v2/Formats.cpp.
# ---------------------------------------------------------------------------

MAX_DOCUMENT_SIZE = 64 * 1024
MAX_MANIFEST_SIZE = 8 * 1024 * 1024
MAX_SIGNATURE_FILE_SIZE = 8 * 194
MAX_EXACT_INTEGER = 9007199254740991
MAX_KEYS = 4
MAX_MIRRORS = 16
MAX_SIGNATURE_LINES = 8
MAX_PATH = 512
MAX_VARIANT_ID = 64
MAX_VARIANT_NAME = 64
MAX_LINK_DEPTH = 40

COMMIT_RE = re.compile(r"[0-9a-f]{40}")
VERSION_RE = re.compile(r"(0|[1-9][0-9]*)(\.(0|[1-9][0-9]*))*")
VARIANT_ID_RE = re.compile(r"(windows|macos|linux)-(x86_64|x86|arm64)-[a-z0-9.]+(-[a-z0-9.]+)*")
VARIANT_NAME_RE = re.compile(r"[\x21-\x7e]([\x20-\x7e]*[\x21-\x7e])?")
BASE_URL_RE = re.compile(r"https://[\x21-\x7e]+/")
HEX_RE = re.compile(r"[0-9a-f]+")
UTC_RE = re.compile(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z")
FORBIDDEN_PATH_CHARS = set('\\:*?"<>|')


def fail(message):
    raise ValueError(message)


def _at(where, key):
    return key if not where else "%s.%s" % (where, key)


def _at_index(where, index):
    return "%s[%d]" % (where, index)


def _is_integer(value):
    return isinstance(value, int) and not isinstance(value, bool)


def _require_integer(value, minimum, maximum, where):
    if not _is_integer(value):
        fail("%s: 不是整数" % where)
    if value < minimum or value > maximum:
        fail("%s: 不在 %d 到 %d 之间" % (where, minimum, maximum))
    return value


def _require_hex(value, size, where):
    if not isinstance(value, str):
        fail("%s: 不是字符串" % where)
    if len(value) != size * 2 or not HEX_RE.fullmatch(value):
        fail("%s: 不是 %d 位小写十六进制" % (where, size * 2))
    return value


def _require_matching(value, pattern, where):
    if not isinstance(value, str):
        fail("%s: 不是字符串" % where)
    if not pattern.fullmatch(value):
        fail("%s: 格式不正确" % where)
    return value


def _require_utc(value, where):
    if not isinstance(value, str):
        fail("%s: 不是字符串" % where)
    if not UTC_RE.fullmatch(value):
        fail("%s: 不是 YYYY-MM-DDTHH:MM:SSZ 形式的 UTC 时间" % where)
    try:
        parsed = datetime.datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ")
    except ValueError:
        fail("%s: 不是有效时间" % where)
    if parsed.strftime("%Y-%m-%dT%H:%M:%SZ") != value:
        fail("%s: 不是 YYYY-MM-DDTHH:MM:SSZ 形式的 UTC 时间" % where)
    return value


def _require_exact_keys(obj, keys, where):
    if not isinstance(obj, dict):
        fail("%s: 不是对象" % where)
    for key in keys:
        if key not in obj:
            fail("%s: 缺少 '%s'" % (where, key))
    for key in obj:
        if key not in keys:
            fail("%s: 含有未知键 '%s'" % (where, key))


def _require_object(value, where):
    if not isinstance(value, dict):
        fail("%s: 不是对象" % where)
    return value


def _require_list(value, where, minimum, maximum):
    if not isinstance(value, list):
        fail("%s: 不是数组" % where)
    if len(value) < minimum or len(value) > maximum:
        fail("%s: 元素数为 %d，允许 %d 到 %d" % (where, len(value), minimum, maximum))
    return value


def _pairs_without_duplicates(pairs):
    obj = {}
    for key, value in pairs:
        if key in obj:
            fail("重复键 '%s'" % key)
        obj[key] = value
    return obj


def _reject_constant(value):
    fail("不支持的 JSON 数值 '%s'" % value)


def _read_json(data, where, limit):
    if len(data) > limit:
        fail("%s: %d 字节，超过 %d" % (where, len(data), limit))
    if data.startswith(b"\xef\xbb\xbf"):
        fail("%s: 不能带字节序标记" % where)
    try:
        text = data.decode("utf-8")
        obj = json.loads(text, object_pairs_hook=_pairs_without_duplicates, parse_constant=_reject_constant)
    except ValueError as e:
        fail("%s: JSON 解析失败: %s" % (where, e))
    return _require_object(obj, where)


def _dump_json(obj):
    return json.dumps(obj, ensure_ascii=True, sort_keys=True, indent=2).encode("ascii") + b"\n"


def _header(doc, doc_type, names):
    if doc.get("type") != doc_type:
        fail("type 不是 '%s'" % doc_type)
    if not _is_integer(doc.get("format")) or doc["format"] != 1:
        fail("format 不受支持（只能是 1）")
    _require_exact_keys(doc, names + ["type", "format"], doc_type)


def is_valid_path(path, dotdot=False):
    if not isinstance(path, str) or not path or len(path) > MAX_PATH:
        return False
    for char in path:
        code = ord(char)
        if code < 0x20 or code > 0x7e or char in FORBIDDEN_PATH_CHARS:
            return False
    for part in path.split("/"):
        if not part or part == "." or (part == ".." and not dotdot):
            return False
    return True


def _require_path(value, where, dotdot=False):
    if not isinstance(value, str):
        fail("%s: 不是字符串" % where)
    if not is_valid_path(value, dotdot):
        fail("%s: '%s' 不是有效路径" % (where, value))
    return value


def content_path(sha256):
    return "files/%s/%s" % (sha256[:2], sha256)


def _require_version(value, where):
    return _require_matching(value, VERSION_RE, where)


def _validate_requirements(value, where):
    req = _require_object(value, where)
    os_name = req.get("os")
    if os_name in ("windows", "macos"):
        _require_exact_keys(req, ["os", "arch", "os_version"], where)
    elif os_name == "linux":
        _require_exact_keys(req, ["os", "arch", "glibc", "glibcxx", "qt"], where)
    else:
        fail("%s: os 必须是 linux、macos 或 windows" % where)
    _require_matching(req["arch"], re.compile("x86|x86_64|arm64"), _at(where, "arch"))
    if os_name == "linux":
        _require_version(req["glibc"], _at(where, "glibc"))
        _require_version(req["glibcxx"], _at(where, "glibcxx"))
        _require_version(req["qt"], _at(where, "qt"))
    else:
        _require_version(req["os_version"], _at(where, "os_version"))
    return req


def _require_variant_id(value, where):
    _require_matching(value, VARIANT_ID_RE, where)
    if len(value) > MAX_VARIANT_ID:
        fail("%s: 超过 %d 个字符" % (where, MAX_VARIANT_ID))
    return value


def _resolve_link(link_path, links, where):
    current = link_path.split("/")[:-1]
    pending = links[link_path].split("/")
    depth = 0
    while pending:
        part = pending.pop(0)
        if part == "..":
            if not current:
                fail("%s: 指向安装目录之外" % where)
            current.pop()
            continue
        current.append(part)
        here = "/".join(current)
        if here in links:
            depth += 1
            if depth > MAX_LINK_DEPTH:
                fail("%s: 链接成环" % where)
            current.pop()
            pending = links[here].split("/") + pending
    return "/".join(current)


def parse_keys(data, expected, where="keys"):
    doc = _read_json(data, where, MAX_DOCUMENT_SIZE)
    _header(doc, "keys", ["version", "expires", "root_keys", "online_keys"])
    version = _require_integer(doc["version"], 1, MAX_EXACT_INTEGER, _at(where, "version"))
    if version != expected:
        fail("%s: version 是 %d，但文件名要求 %d" % (where, version, expected))
    expires = _require_utc(doc["expires"], _at(where, "expires"))
    root_keys = _key_list(doc["root_keys"], _at(where, "root_keys"))
    online_keys = _key_list(doc["online_keys"], _at(where, "online_keys"))
    for key in root_keys:
        if key in online_keys:
            fail("%s: 一个密钥同时出现在 root_keys 和 online_keys" % where)
    return {
        "version": version,
        "expires": expires,
        "root_keys": root_keys,
        "online_keys": online_keys,
    }


def _key_list(value, where):
    items = _require_list(value, where, 1, MAX_KEYS)
    out = []
    for index, item in enumerate(items):
        key = _require_hex(item, 32, _at_index(where, index))
        if key in out:
            fail("%s: 密钥重复" % where)
        out.append(key)
    return out


def parse_tags(data, where="tags"):
    doc = _read_json(data, where, MAX_DOCUMENT_SIZE)
    _header(doc, "tags", ["serial", "issued", "tags", "withdrawn"])
    serial = _require_integer(doc["serial"], 1, MAX_EXACT_INTEGER, _at(where, "serial"))
    issued = _require_utc(doc["issued"], _at(where, "issued"))
    tags = _require_object(doc["tags"], _at(where, "tags"))
    _require_exact_keys(tags, ["latest", "testing", "stable"], _at(where, "tags"))
    latest = _require_integer(tags["latest"], 1, MAX_EXACT_INTEGER, _at(_at(where, "tags"), "latest"))
    testing = _require_integer(tags["testing"], 1, MAX_EXACT_INTEGER, _at(_at(where, "tags"), "testing"))
    stable = _require_integer(tags["stable"], 1, MAX_EXACT_INTEGER, _at(_at(where, "tags"), "stable"))
    if not (stable <= testing <= latest):
        fail("%s.tags: 需要 stable <= testing <= latest，当前为 %d, %d, %d" % (where, stable, testing, latest))
    withdrawn = []
    items = _require_list(doc["withdrawn"], _at(where, "withdrawn"), 0, MAX_EXACT_INTEGER)
    for index, item in enumerate(items):
        build = _require_integer(item, 1, MAX_EXACT_INTEGER, _at_index(_at(where, "withdrawn"), index))
        if withdrawn and withdrawn[-1] >= build:
            fail("%s.withdrawn: 不是递增的，或构建号重复" % where)
        if build in (latest, testing, stable):
            fail("%s.withdrawn: 构建 %d 仍在 tags 中" % (where, build))
        withdrawn.append(build)
    return {
        "serial": serial,
        "issued": issued,
        "tags": {"latest": latest, "testing": testing, "stable": stable},
        "withdrawn": withdrawn,
    }


def parse_mirrors(data, where="mirrors"):
    doc = _read_json(data, where, MAX_DOCUMENT_SIZE)
    _header(doc, "mirrors", ["issued", "mirrors"])
    issued = _require_utc(doc["issued"], _at(where, "issued"))
    groups_in = _require_list(doc["mirrors"], _at(where, "mirrors"), 1, MAX_MIRRORS)
    seen = set()
    groups = []
    for group_index, group_in in enumerate(groups_in):
        group_where = _at_index(_at(where, "mirrors"), group_index)
        items = _require_list(group_in, group_where, 1, MAX_MIRRORS)
        group = []
        for index, item in enumerate(items):
            url = _require_url(item, _at_index(group_where, index))
            if url in seen:
                fail("%s: '%s' 重复出现" % (group_where, url))
            seen.add(url)
            group.append(url)
        groups.append(group)
    if len(seen) > MAX_MIRRORS:
        fail("%s: 共 %d 个 URL，最多 %d 个" % (where, len(seen), MAX_MIRRORS))
    return {"issued": issued, "mirrors": groups}


def _require_url(value, where):
    if not isinstance(value, str):
        fail("%s: 不是字符串" % where)
    if not BASE_URL_RE.fullmatch(value):
        fail("%s: '%s' 不是以 / 结尾、不带用户信息/查询/片段的 https:// 基地址" % (where, value))
    host = value[8:].split("/")[0]
    if not host or "@" in value or "?" in value or "#" in value or "\\" in value:
        fail("%s: '%s' 不是以 / 结尾、不带用户信息/查询/片段的 https:// 基地址" % (where, value))
    return value


def parse_build(data, expected, where="build"):
    doc = _read_json(data, where, MAX_MANIFEST_SIZE)
    _header(doc, "build", ["build", "commit", "built", "variants"])
    build = _require_integer(doc["build"], 1, MAX_EXACT_INTEGER, _at(where, "build"))
    if build != expected:
        fail("%s: build 是 %d，但文件名要求 %d" % (where, build, expected))
    commit = _require_matching(doc["commit"], COMMIT_RE, _at(where, "commit"))
    built = _require_utc(doc["built"], _at(where, "built"))
    variants_in = _require_object(doc["variants"], _at(where, "variants"))
    if not variants_in:
        fail("%s: variants 至少需要一个变体" % where)
    variants = {}
    for variant_id, variant_value in variants_in.items():
        variant_where = _at(_at(where, "variants"), variant_id)
        _require_variant_id(variant_id, variant_where)
        variants[variant_id] = _parse_variant(variant_id, variant_value, variant_where)

    successor = {}
    for variant in variants.values():
        for other in variant["supersedes"]:
            if other in successor:
                fail("%s: %s 同时被 %s 和 %s 取代" % (where, other, successor[other], variant["id"]))
            successor[other] = variant["id"]
    for start in successor:
        seen = {start}
        current = start
        while current in successor:
            current = successor[current]
            if current in seen:
                fail("%s: 从 %s 开始的 supersedes 链成环" % (where, start))
            seen.add(current)
    return {"build": build, "commit": commit, "built": built, "variants": variants}


def _parse_variant(variant_id, value, where):
    obj = _require_object(value, where)
    _require_exact_keys(obj, ["name", "supersedes", "requires", "files", "links"], where)
    name = _require_matching(obj["name"], VARIANT_NAME_RE, _at(where, "name"))
    if len(name) > MAX_VARIANT_NAME:
        fail("%s.name: 超过 %d 个字符" % (where, MAX_VARIANT_NAME))
    requires = _validate_requirements(obj["requires"], _at(where, "requires"))
    match = VARIANT_ID_RE.fullmatch(variant_id)
    if match.group(1) != requires["os"] or match.group(2) != requires["arch"]:
        fail("%s: 变体 ID 与 requires 不匹配" % where)

    supersedes_in = _require_list(obj["supersedes"], _at(where, "supersedes"), 0, MAX_EXACT_INTEGER)
    supersedes = []
    for index, item in enumerate(supersedes_in):
        item_where = _at_index(_at(where, "supersedes"), index)
        other = _require_variant_id(item, item_where)
        if other == variant_id:
            fail("%s: 变体取代了它自己" % where)
        if other in supersedes:
            fail("%s.supersedes: 变体重复出现" % where)
        supersedes.append(other)

    files_in = _require_list(obj["files"], _at(where, "files"), 1, MAX_EXACT_INTEGER)
    files = []
    paths = []
    for index, item in enumerate(files_in):
        item_where = _at_index(_at(where, "files"), index)
        entry = _require_object(item, item_where)
        _require_exact_keys(entry, ["path", "sha256", "size", "executable"], item_where)
        path = _require_path(entry["path"], _at(item_where, "path"))
        sha256 = _require_hex(entry["sha256"], 32, _at(item_where, "sha256"))
        size = _require_integer(entry["size"], 0, MAX_EXACT_INTEGER, _at(item_where, "size"))
        if not isinstance(entry["executable"], bool):
            fail("%s: 不是布尔值" % _at(item_where, "executable"))
        file_entry = {"path": path, "sha256": sha256, "size": size, "executable": entry["executable"]}
        files.append(file_entry)
        paths.append(path)

    links_in = _require_list(obj["links"], _at(where, "links"), 0, MAX_EXACT_INTEGER)
    if links_in and requires["os"] == "windows":
        fail("%s: Windows 变体不能有 links" % where)
    links = []
    link_paths = []
    link_map = {}
    for index, item in enumerate(links_in):
        item_where = _at_index(_at(where, "links"), index)
        entry = _require_object(item, item_where)
        _require_exact_keys(entry, ["path", "target"], item_where)
        path = _require_path(entry["path"], _at(item_where, "path"))
        target = _require_path(entry["target"], _at(item_where, "target"), dotdot=True)
        links.append({"path": path, "target": target})
        link_paths.append(path)
        link_map[path] = target

    for listed in (paths, link_paths):
        if listed != sorted(listed):
            fail("%s: 路径没有按字节序排序" % where)

    entries = paths + link_paths
    spelling = {}
    directories = set()
    for entry_path in entries:
        parts = entry_path.split("/")
        for count in range(1, len(parts) + 1):
            prefix = "/".join(parts[:count])
            if count < len(parts):
                directories.add(prefix)
            lower = prefix.lower()
            if lower not in spelling:
                spelling[lower] = prefix
            elif spelling[lower] != prefix:
                fail("%s: '%s' 与 '%s' 只有大小写不同" % (where, spelling[lower], prefix))
    if len(set(entries)) != len(entries):
        fail("%s: 路径重复" % where)
    if directories & set(entries):
        fail("%s: 列出的路径同时是其他路径的目录" % where)

    file_set = set(paths)
    for link_path in link_paths:
        target = _resolve_link(link_path, link_map, "%s: 链接 '%s'" % (where, link_path))
        if target not in file_set and target not in directories:
            fail("%s: 链接 '%s' 指向 '%s'，不在已列出的文件或目录中" % (where, link_path, target))

    return {
        "id": variant_id,
        "name": name,
        "supersedes": supersedes,
        "requires": requires,
        "files": files,
        "links": links,
    }


# ---------------------------------------------------------------------------
# Ed25519 (RFC 8032), pure Python so the tool needs no external packages.
# ---------------------------------------------------------------------------

P = 2 ** 255 - 19
L = 2 ** 252 + 27742317777372353535851937790883648493
D = (-121665 * pow(121666, P - 2, P)) % P
I = pow(2, (P - 1) // 4, P)


def _inv(x):
    return pow(x, P - 2, P)


def _x_recover(y):
    xx = (y * y - 1) * _inv(D * y * y + 1)
    x = pow(xx, (P + 3) // 8, P)
    if (x * x - xx) % P != 0:
        x = (x * I) % P
    if x % 2 != 0:
        x = P - x
    return x


BY = 4 * _inv(5) % P
BX = _x_recover(BY)
BASE = (BX, BY, 1, (BX * BY) % P)


def _point_add(left, right):
    x1, y1, z1, t1 = left
    x2, y2, z2, t2 = right
    a = (y1 - x1) * (y2 - x2) % P
    b = (y1 + x1) * (y2 + x2) % P
    c = t1 * 2 * D * t2 % P
    d = z1 * 2 * z2 % P
    e = b - a
    f = d - c
    g = d + c
    h = b + a
    return (e * f % P, g * h % P, f * g % P, e * h % P)


def _point_multiply(point, scalar):
    result = (0, 1, 1, 0)
    addend = point
    while scalar:
        if scalar & 1:
            result = _point_add(result, addend)
        addend = _point_add(addend, addend)
        scalar >>= 1
    return result


def _point_encode(point):
    x, y, z, _ = point
    z_inv = _inv(z)
    x = x * z_inv % P
    y = y * z_inv % P
    return (y | ((x & 1) << 255)).to_bytes(32, "little")


def _point_decode(data):
    if len(data) != 32:
        fail("Ed25519 点不是 32 字节")
    y = int.from_bytes(data, "little") & ((1 << 255) - 1)
    if y >= P:
        fail("Ed25519 点无效")
    x = _x_recover(y)
    if (x & 1) != (data[31] >> 7):
        x = P - x
    if (y * y - x * x - 1 - D * x * x * y * y) % P != 0:
        fail("Ed25519 点不在曲线上")
    return (x, y, 1, (x * y) % P)


def _clamp(h):
    scalar = int.from_bytes(h[:32], "little")
    scalar &= (1 << 254) - 8
    scalar |= 1 << 254
    return scalar


def ed25519_public_key(seed):
    if len(seed) != 32:
        fail("Ed25519 私钥种子必须是 32 字节")
    hashed = hashlib.sha512(seed).digest()
    return _point_encode(_point_multiply(BASE, _clamp(hashed)))


def ed25519_sign(seed, message):
    if len(seed) != 32:
        fail("Ed25519 私钥种子必须是 32 字节")
    hashed = hashlib.sha512(seed).digest()
    secret = _clamp(hashed)
    public = _point_encode(_point_multiply(BASE, secret))
    nonce = int.from_bytes(hashlib.sha512(hashed[32:] + message).digest(), "little") % L
    r_point = _point_encode(_point_multiply(BASE, nonce))
    challenge = int.from_bytes(hashlib.sha512(r_point + public + message).digest(), "little") % L
    s = (nonce + challenge * secret) % L
    return r_point + s.to_bytes(32, "little")


def ed25519_verify(public, message, signature):
    if len(public) != 32 or len(signature) != 64:
        return False
    try:
        a_point = _point_decode(public)
        r_point = _point_decode(signature[:32])
    except ValueError:
        return False
    s = int.from_bytes(signature[32:], "little")
    if s >= L:
        return False
    challenge = int.from_bytes(hashlib.sha512(signature[:32] + public + message).digest(), "little") % L
    left = _point_multiply(BASE, s)
    right = _point_add(r_point, _point_multiply(a_point, challenge))
    return _point_encode(left) == _point_encode(right)


def _ed25519_self_test():
    seed = bytes.fromhex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60")
    public = bytes.fromhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a")
    expected = bytes.fromhex(
        "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"
    )
    signature = ed25519_sign(seed, b"")
    if ed25519_public_key(seed) != public or signature != expected or not ed25519_verify(public, b"", signature):
        fail("Ed25519 自检失败，脚本内置实现不可用")


# ---------------------------------------------------------------------------
# Key files.
# ---------------------------------------------------------------------------

def _pem_der(data):
    text = data.decode("ascii", "ignore")
    match = re.search(r"-----BEGIN [^-]+-----(.*?)-----END [^-]+-----", text, re.S)
    if not match:
        fail("不是 PEM 文件")
    return base64.b64decode("".join(match.group(1).split()))


def _seed_from_pem(data):
    der = _pem_der(data)
    marker = b"\x06\x03\x2b\x65\x70"
    index = der.find(marker)
    if index < 0:
        fail("PEM 不是 Ed25519 私钥")
    pos = index + len(marker)
    if der[pos:pos + 2] == b"\x04\x20":
        seed = der[pos + 2:pos + 34]
    elif der[pos:pos + 2] == b"\x04\x22" and der[pos + 2:pos + 4] == b"\x04\x20":
        seed = der[pos + 4:pos + 36]
    else:
        fail("PEM 不是 Ed25519 私钥")
    if len(seed) != 32:
        fail("PEM 私钥长度不正确")
    return seed


def load_private_key(path):
    file_path = Path(path)
    if not file_path.is_file():
        fail("私钥文件不存在: %s" % path)
    data = file_path.read_bytes()
    if len(data) == 32:
        seed = data
    else:
        stripped = data.strip()
        if re.fullmatch(rb"[0-9a-fA-F]{64}", stripped):
            seed = bytes.fromhex(stripped.decode("ascii"))
        elif b"-----BEGIN" in data:
            seed = _seed_from_pem(data)
        else:
            fail("无法识别的私钥格式: %s（支持 PKCS#8 PEM、64 位十六进制种子或 32 字节原始种子）" % path)
    return seed, ed25519_public_key(seed)


def load_public_key(value):
    text = value.strip()
    if re.fullmatch(r"[0-9a-fA-F]{64}", text):
        return text.lower()
    if text.startswith("ssh-ed25519 "):
        parts = text.split()
        if len(parts) != 2:
            fail("ssh-ed25519 公钥格式不正确")
        blob = base64.b64decode(parts[1])
        if len(blob) < 32:
            fail("ssh-ed25519 公钥长度不正确")
        return blob[-32:].hex()
    file_path = Path(value)
    if not file_path.is_file():
        fail("公钥既不是 64 位十六进制，也不是存在的文件: %s" % value)
    data = file_path.read_bytes()
    if b"BEGIN PRIVATE KEY" in data:
        return ed25519_public_key(_seed_from_pem(data)).hex()
    if b"-----BEGIN" in data:
        der = _pem_der(data)
        marker = b"\x03\x21\x00"
        index = der.find(marker)
        if index >= 0:
            key = der[index + len(marker):index + len(marker) + 32]
            if len(key) == 32:
                return key.hex()
        if len(der) >= 32 and der[-32:].hex() != "":
            return der[-32:].hex()
        fail("PEM 公钥不是 Ed25519 公钥")
    fail("无法识别的公钥格式: %s" % value)


def write_key_pem(path, seed):
    der = bytes.fromhex("302e020100300506032b657004220420") + seed
    encoded = base64.b64encode(der).decode("ascii")
    lines = [encoded[index:index + 64] for index in range(0, len(encoded), 64)]
    text = "-----BEGIN PRIVATE KEY-----\n%s\n-----END PRIVATE KEY-----\n" % "\n".join(lines)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="ascii")
    try:
        os.chmod(str(path), 0o600)
    except OSError:
        pass


# ---------------------------------------------------------------------------
# Signature files.
# ---------------------------------------------------------------------------

def check_signature_file(document, signature_file, trusted_keys, where):
    if len(signature_file) > MAX_SIGNATURE_FILE_SIZE:
        fail("%s: 签名文件过大" % where)
    if not signature_file or not signature_file.endswith(b"\n"):
        fail("%s: 签名文件为空或末尾没有换行" % where)
    lines = signature_file[:-1].split(b"\n")
    if len(lines) > MAX_SIGNATURE_LINES:
        fail("%s: 签名文件有 %d 行，最多 %d 行" % (where, len(lines), MAX_SIGNATURE_LINES))
    seen = []
    trusted = 0
    for index, raw_line in enumerate(lines):
        line = raw_line.decode("ascii", "replace")
        match = re.fullmatch(r"([0-9a-f]{64}) ([0-9a-f]{128})", line)
        if not match:
            fail("%s: 第 %d 行不是 <64 位十六进制密钥> <128 位十六进制签名>" % (where, index + 1))
        key = match.group(1)
        if key in seen:
            fail("%s: 密钥重复出现" % where)
        seen.append(key)
        if key not in trusted_keys:
            continue
        trusted += 1
        if not ed25519_verify(bytes.fromhex(key), document, bytes.fromhex(match.group(2))):
            fail("%s: 密钥 %s 的签名无法验证" % (where, key))
    if not trusted:
        fail("%s: 没有受信任密钥的签名" % where)


def sign_document(document, private_keys, where):
    lines = []
    for seed, public in private_keys:
        signature = ed25519_sign(seed, document)
        if not ed25519_verify(public, document, signature):
            fail("%s: 内部签名自检失败" % where)
        lines.append("%s %s" % (public.hex(), signature.hex()))
    return ("\n".join(lines) + "\n").encode("ascii")


# ---------------------------------------------------------------------------
# Tree helpers.
# ---------------------------------------------------------------------------

def _atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(data)
    os.replace(str(temporary), str(path))


def _write_signed(tree, name, document, private_keys, validator):
    data = _dump_json(document)
    validator(data)
    path = tree / name
    signature = sign_document(data, private_keys, name)
    check_signature_file(data, signature, [public.hex() for _, public in private_keys], name)
    _atomic_write(path, data)
    _atomic_write(tree / (name + ".sig"), signature)
    return path, data


def _now_utc():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _parse_int_list(value):
    if value is None:
        return []
    items = []
    for part in value.split(","):
        part = part.strip()
        if not part:
            continue
        try:
            items.append(int(part))
        except ValueError:
            fail("不是有效的构建号: %s" % part)
    return items


def _sha256_file(path):
    digest = hashlib.sha256()
    with open(str(path), "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _is_executable(path):
    suffix = path.suffix.lower()
    if suffix in (".exe", ".bat", ".cmd", ".com", ".sh"):
        return True
    if os.name != "nt":
        try:
            if os.stat(str(path)).st_mode & 0o111:
                return True
        except OSError:
            pass
    try:
        with open(str(path), "rb") as handle:
            head = handle.read(4)
    except OSError:
        fail("无法读取文件: %s" % path)
    if head[:2] == b"MZ" or head == b"\x7fELF":
        return True
    if head in (b"\xfe\xed\xfa\xce", b"\xfe\xed\xfa\xcf", b"\xce\xfa\xed\xfe", b"\xcf\xfa\xed\xfe"):
        return True
    if head[:2] == b"#!":
        return True
    return False


def _collect_install(install, tree):
    install = install.resolve()
    tree = tree.resolve()
    if install == tree or install in tree.parents or tree in install.parents:
        fail("install 目录与输出树不能互相包含")
    files = []
    links = []
    for dirpath, dirnames, filenames in os.walk(str(install), followlinks=False):
        for name in sorted(filenames) + sorted(dirnames):
            full = Path(dirpath) / name
            relative = full.relative_to(install).as_posix()
            if full.is_symlink():
                target = os.readlink(str(full))
                if os.path.isabs(target):
                    target = os.path.relpath(target, str(full.parent))
                target = Path(target).as_posix()
                links.append({"path": relative, "target": target})
                if name in dirnames:
                    dirnames.remove(name)
                continue
            if name in dirnames:
                continue
            if not is_valid_path(relative):
                fail("install 中的路径不是合法清单路径: %s" % relative)
            files.append({"path": relative, "full": full})
    return files, links


def _copy_content(tree, full, sha256, size):
    target = tree / content_path(sha256)
    if target.exists():
        if target.stat().st_size != size or _sha256_file(target) != sha256:
            fail("内容寻址文件已存在但内容不符: %s" % target)
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(str(full), str(target))


# ---------------------------------------------------------------------------
# Commands.
# ---------------------------------------------------------------------------

def cmd_keygen(args):
    out = Path(args.out)
    names = args.name or ["online1"]
    seen = set()
    for name in names:
        if name in seen:
            fail("密钥名重复: %s" % name)
        seen.add(name)
        path = out / (name + ".pem")
        if path.exists() and not args.force:
            fail("密钥已存在，使用 --force 覆盖: %s" % path)
        seed = secrets.token_bytes(32)
        write_key_pem(path, seed)
        print("%s" % path)
        print("  public: %s" % ed25519_public_key(seed).hex())
    print("请妥善保管私钥；生产环境的根密钥应离线保存。")


def cmd_keys(args):
    tree = Path(args.tree)
    version = args.version
    if version < 1:
        fail("keys 版本必须大于等于 1")
    root_keys = [load_public_key(value) for value in args.root_pub]
    online_keys = [load_public_key(value) for value in args.online_pub]
    if not root_keys:
        fail("至少需要一个 --root-pub")
    if not online_keys:
        fail("至少需要一个 --online-pub")
    if len(root_keys) > MAX_KEYS or len(online_keys) > MAX_KEYS:
        fail("root_keys 和 online_keys 各自最多 %d 个" % MAX_KEYS)
    if len(set(root_keys)) != len(root_keys):
        fail("root_keys 有重复")
    if len(set(online_keys)) != len(online_keys):
        fail("online_keys 有重复")
    if set(root_keys) & set(online_keys):
        fail("同一个密钥不能同时是 root key 和 online key")

    document = {
        "type": "keys",
        "format": 1,
        "version": version,
        "expires": _require_utc(args.expires, "expires"),
        "root_keys": root_keys,
        "online_keys": online_keys,
    }
    private_keys = [load_private_key(value) for value in args.sign_key]
    if not private_keys:
        fail("至少需要一个 --sign-key")
    if len(private_keys) > MAX_SIGNATURE_LINES:
        fail("签名密钥最多 %d 个" % MAX_SIGNATURE_LINES)
    if args.trusted_pub:
        trusted = {load_public_key(value) for value in args.trusted_pub}
        for _, public in private_keys:
            if public.hex() not in trusted:
                fail("签名密钥 %s 不在 --trusted-pub 中" % public.hex())
    else:
        print("提示: 未提供 --trusted-pub，无法自动确认签名密钥属于预期的上一版根密钥。")

    path = tree / ("keys/%d.json" % version)
    if path.exists() and not args.force:
        fail("密钥文档已存在，使用 --force 覆盖: %s" % path)
    _write_signed(tree, "keys/%d.json" % version, document, private_keys,
                  lambda data: parse_keys(data, version, "keys/%d.json" % version))
    print("已生成: %s" % path)
    print("  root_keys:   %s" % ", ".join(root_keys))
    print("  online_keys: %s" % ", ".join(online_keys))


def cmd_publish(args):
    tree = Path(args.tree)
    install = Path(args.install)
    if not install.is_dir():
        fail("install 目录不存在: %s" % install)
    build = args.build
    if build < 1:
        fail("--build 必须大于等于 1")
    _require_matching(args.commit, COMMIT_RE, "--commit")
    built = _require_utc(args.built or _now_utc(), "--built")
    variant_id = _require_variant_id(args.variant, "--variant")
    if args.requires_file and args.requires:
        fail("--requires 与 --requires-file 只能提供一个")
    if not args.requires_file and not args.requires:
        fail("需要 --requires 或 --requires-file")
    if args.requires_file:
        try:
            requires = json.loads(Path(args.requires_file).read_text(encoding="utf-8-sig"))
        except ValueError as e:
            fail("--requires-file 不是有效 JSON: %s" % e)
        except OSError as e:
            fail("无法读取 --requires-file: %s" % e)
    else:
        try:
            requires = json.loads(args.requires)
        except ValueError as e:
            fail("--requires 不是有效 JSON: %s" % e)
    _validate_requirements(requires, "requires")
    match = VARIANT_ID_RE.fullmatch(variant_id)
    if match.group(1) != requires["os"] or match.group(2) != requires["arch"]:
        fail("--variant 与 --requires 的 os/arch 不匹配")

    supersedes = []
    for value in args.supersedes:
        for item in value.split(","):
            item = item.strip()
            if not item:
                continue
            _require_variant_id(item, "--supersedes")
            if item == variant_id:
                fail("变体不能取代它自己")
            if item in supersedes:
                fail("--supersedes 有重复: %s" % item)
            supersedes.append(item)

    private_keys = [load_private_key(value) for value in args.sign_key]
    if not private_keys:
        fail("至少需要一个 --sign-key")

    files, links = _collect_install(install, tree)
    entries = []
    for item in files:
        relative = item["path"]
        full = item["full"]
        sha256 = _sha256_file(full)
        size = full.stat().st_size
        _copy_content(tree, full, sha256, size)
        entries.append({
            "path": relative,
            "sha256": sha256,
            "size": size,
            "executable": _is_executable(full),
        })
    entries.sort(key=lambda item: item["path"])
    if not entries:
        fail("install 目录中没有可发布的普通文件")

    variant = {
        "name": args.name or variant_id,
        "supersedes": supersedes,
        "requires": requires,
        "files": entries,
        "links": sorted(links, key=lambda item: item["path"]),
    }

    manifest_path = tree / ("builds/%d.json" % build)
    variants = {}
    if args.merge and manifest_path.exists():
        old = parse_build(manifest_path.read_bytes(), build, "builds/%d.json" % build)
        for key, value in old["variants"].items():
            variants[key] = {
                "name": value["name"],
                "supersedes": value["supersedes"],
                "requires": value["requires"],
                "files": value["files"],
                "links": value["links"],
            }
        print("合并已有清单中的 %d 个变体" % len(variants))
    variants[variant_id] = variant

    document = {
        "type": "build",
        "format": 1,
        "build": build,
        "commit": args.commit,
        "built": built,
        "variants": variants,
    }
    _write_signed(tree, "builds/%d.json" % build, document, private_keys,
                  lambda data: parse_build(data, build, "builds/%d.json" % build))
    print("已生成: %s" % manifest_path)
    print("  变体: %s" % variant_id)
    print("  文件: %d 个，链接: %d 个" % (len(entries), len(links)))


def cmd_tags(args):
    tree = Path(args.tree)
    serial = args.serial
    if serial < 1:
        fail("--serial 必须大于等于 1")
    latest = args.latest
    testing = args.testing
    stable = args.stable
    if min(latest, testing, stable) < 1:
        fail("latest/testing/stable 必须大于等于 1")
    if not (stable <= testing <= latest):
        fail("需要 stable <= testing <= latest")
    withdrawn = sorted(_parse_int_list(args.withdrawn))
    for index in range(1, len(withdrawn)):
        if withdrawn[index - 1] == withdrawn[index]:
            fail("--withdrawn 有重复: %d" % withdrawn[index])

    existing_path = tree / "tags.json"
    if existing_path.exists():
        old = parse_tags(existing_path.read_bytes(), "tags.json")
        if serial <= old["serial"]:
            fail("新 serial (%d) 必须大于已有 tags.json 的 serial (%d)" % (serial, old["serial"]))

    document = {
        "type": "tags",
        "format": 1,
        "serial": serial,
        "issued": _require_utc(args.issued or _now_utc(), "--issued"),
        "tags": {"latest": latest, "testing": testing, "stable": stable},
        "withdrawn": withdrawn,
    }
    private_keys = [load_private_key(value) for value in args.sign_key]
    if not private_keys:
        fail("至少需要一个 --sign-key")
    _write_signed(tree, "tags.json", document, private_keys, lambda data: parse_tags(data, "tags.json"))
    print("已生成: %s" % existing_path)


def cmd_mirrors(args):
    tree = Path(args.tree)
    if args.group:
        groups = []
        for group in args.group:
            urls = [item.strip() for item in group.split(",") if item.strip()]
            if not urls:
                fail("--group 不能为空")
            groups.append(urls)
    elif args.mirror:
        groups = [[item.strip() for item in args.mirror if item.strip()]]
    else:
        fail("至少需要一个 --group 或 --mirror")
    document = {
        "type": "mirrors",
        "format": 1,
        "issued": _require_utc(args.issued or _now_utc(), "--issued"),
        "mirrors": groups,
    }
    private_keys = [load_private_key(value) for value in args.sign_key]
    if not private_keys:
        fail("至少需要一个 --sign-key")
    _write_signed(tree, "mirrors.json", document, private_keys, lambda data: parse_mirrors(data, "mirrors.json"))
    print("已生成: %s" % (tree / "mirrors.json"))


def cmd_validate(args):
    tree = Path(args.tree)
    root_keys = [load_public_key(value) for value in args.root_pub] if args.root_pub else []
    key_paths = sorted((tree / "keys").glob("*.json"), key=lambda item: int(item.stem) if item.stem.isdigit() else -1)
    if not key_paths:
        fail("没有找到 keys/*.json")
    documents = []
    previous_version = None
    for path in key_paths:
        if not path.stem.isdigit():
            fail("keys 目录中有不是数字文件名的文件: %s" % path)
        version = int(path.stem)
        if previous_version is not None and version != previous_version + 1:
            fail("keys 版本不连续: %d 之后是 %d" % (previous_version, version))
        data = path.read_bytes()
        parsed = parse_keys(data, version, str(path))
        documents.append((path, data, parsed))
        signature_path = tree / (str(path.relative_to(tree)) + ".sig")
        if previous_version is not None:
            if not signature_path.exists():
                fail("缺少签名文件: %s" % signature_path)
            previous = documents[-2][2]
            check_signature_file(data, signature_path.read_bytes(), previous["root_keys"], str(signature_path))
        elif root_keys:
            if parsed["root_keys"] != root_keys:
                fail("keys/%d.json 的 root_keys 与 --root-pub 不一致" % version)
            if signature_path.exists():
                check_signature_file(data, signature_path.read_bytes(), root_keys, str(signature_path))
        previous_version = version
    newest = documents[-1][2]
    if not root_keys:
        print("警告: 未提供 --root-pub，keys 文档的固定根信任未被校验")
    now = datetime.datetime.now(datetime.timezone.utc)
    expires = datetime.datetime.strptime(newest["expires"], "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=datetime.timezone.utc)
    if expires <= now:
        print("警告: 最新的 keys 文档已在 %s 过期" % newest["expires"])

    online_keys = newest["online_keys"]
    mirrors_path = tree / "mirrors.json"
    if not mirrors_path.exists():
        fail("缺少 mirrors.json")
    mirrors = parse_mirrors(mirrors_path.read_bytes(), str(mirrors_path))
    mirrors_signature = tree / "mirrors.json.sig"
    if not mirrors_signature.exists():
        fail("缺少签名文件: %s" % mirrors_signature)
    check_signature_file(mirrors_path.read_bytes(), mirrors_signature.read_bytes(), online_keys, "mirrors.json")

    tags_path = tree / "tags.json"
    if not tags_path.exists():
        fail("缺少 tags.json")
    tags = parse_tags(tags_path.read_bytes(), str(tags_path))
    tags_signature = tree / "tags.json.sig"
    if not tags_signature.exists():
        fail("缺少签名文件: %s" % tags_signature)
    check_signature_file(tags_path.read_bytes(), tags_signature.read_bytes(), online_keys, "tags.json")
    tagged = set(tags["tags"].values())
    for build in tags["withdrawn"]:
        if build in tagged:
            fail("withdrawn 中的构建号 %d 仍在 tags 中" % build)

    builds = []
    for path in sorted((tree / "builds").glob("*.json"), key=lambda item: int(item.stem) if item.stem.isdigit() else -1):
        if not path.stem.isdigit():
            fail("builds 目录中有不是数字文件名的文件: %s" % path)
        build = int(path.stem)
        data = path.read_bytes()
        parsed = parse_build(data, build, str(path))
        signature_path = tree / (str(path.relative_to(tree)) + ".sig")
        if not signature_path.exists():
            fail("缺少签名文件: %s" % signature_path)
        check_signature_file(data, signature_path.read_bytes(), online_keys, str(signature_path))
        builds.append(parsed)
        if args.check_content:
            for variant in parsed["variants"].values():
                for entry in variant["files"]:
                    content = tree / content_path(entry["sha256"])
                    if not content.exists():
                        fail("%s: 缺少内容文件 %s" % (path, content))
                    if content.stat().st_size != entry["size"] or _sha256_file(content) != entry["sha256"]:
                        fail("%s: 内容文件与清单不符: %s" % (path, content))
    missing = [build for build in tagged if not (tree / ("builds/%d.json" % build)).exists()]
    if missing:
        fail("tags 引用了不存在的清单: %s" % ", ".join(str(build) for build in sorted(missing)))

    print("校验通过: %d 个 keys 文档、%d 个构建清单、%d 个镜像组" % (
        len(documents), len(builds), len(mirrors["mirrors"]) if mirrors else 0))


# ---------------------------------------------------------------------------
# Command line.
# ---------------------------------------------------------------------------

def build_parser():
    parser = argparse.ArgumentParser(
        description="生成并校验 updater v2 服务端 JSON 文档树。",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    keygen = subparsers.add_parser("keygen", help="生成 Ed25519 私钥 PEM（测试或初始化用）")
    keygen.add_argument("--out", required=True, help="私钥输出目录")
    keygen.add_argument("--name", action="append", help="密钥名，可重复；默认 online1")
    keygen.add_argument("--force", action="store_true", help="覆盖已存在的私钥")
    keygen.set_defaults(func=cmd_keygen)

    keys = subparsers.add_parser("keys", help="生成 keys/<version>.json 及其签名")
    keys.add_argument("--tree", required=True, help="updater 树根目录，例如 qiniu/updater")
    keys.add_argument("--version", required=True, type=int, help="keys 版本号")
    keys.add_argument("--expires", required=True, help="过期时间，YYYY-MM-DDTHH:MM:SSZ")
    keys.add_argument("--root-pub", action="append", default=[], help="根公钥：64 位十六进制、PEM/ssh-ed25519 公钥文件，或 Ed25519 私钥 PEM（自动推导）")
    keys.add_argument("--online-pub", action="append", default=[], help="在线公钥，格式同 --root-pub")
    keys.add_argument("--sign-key", action="append", default=[], help="签名私钥，可重复")
    keys.add_argument("--trusted-pub", action="append", default=[], help="允许签名此文档的上一版根公钥，格式同 --root-pub")
    keys.add_argument("--force", action="store_true", help="覆盖已存在的 keys 文档")
    keys.set_defaults(func=cmd_keys)

    publish = subparsers.add_parser("publish", help="扫描 install 目录并生成 builds/<build>.json 及内容文件")
    publish.add_argument("--tree", required=True, help="updater 树根目录")
    publish.add_argument("--install", required=True, help="要发布的安装目录")
    publish.add_argument("--build", required=True, type=int, help="构建号")
    publish.add_argument("--commit", required=True, help="40 位小写十六进制提交号")
    publish.add_argument("--built", help="构建时间，YYYY-MM-DDTHH:MM:SSZ；默认当前 UTC 时间")
    publish.add_argument("--variant", required=True, help="变体 ID，例如 windows-x86-xp")
    publish.add_argument("--name", help="变体显示名称；默认使用变体 ID")
    publish.add_argument("--requires", help="requires JSON，例如 '{\"os\":\"windows\",\"arch\":\"x86\",\"os_version\":\"5.1\"}'")
    publish.add_argument("--requires-file", help="从文件读取 requires JSON，避免命令行引号问题")
    publish.add_argument("--supersedes", action="append", default=[], help="被此变体取代的旧变体 ID，逗号分隔或重复")
    publish.add_argument("--sign-key", action="append", default=[], help="在线私钥，可重复")
    publish.add_argument("--merge", action="store_true", help="与已有的 builds/<build>.json 合并变体")
    publish.set_defaults(func=cmd_publish)

    tags = subparsers.add_parser("tags", help="生成 tags.json 及其签名")
    tags.add_argument("--tree", required=True, help="updater 树根目录")
    tags.add_argument("--serial", required=True, type=int, help="单调递增的 serial")
    tags.add_argument("--issued", help="签发时间，默认当前 UTC 时间")
    tags.add_argument("--latest", required=True, type=int, help="latest 构建号")
    tags.add_argument("--testing", required=True, type=int, help="testing 构建号")
    tags.add_argument("--stable", required=True, type=int, help="stable 构建号")
    tags.add_argument("--withdrawn", help="已撤回构建号，逗号分隔")
    tags.add_argument("--sign-key", action="append", default=[], help="在线私钥，可重复")
    tags.set_defaults(func=cmd_tags)

    mirrors = subparsers.add_parser("mirrors", help="生成 mirrors.json 及其签名")
    mirrors.add_argument("--tree", required=True, help="updater 树根目录")
    mirrors.add_argument("--issued", help="签发时间，默认当前 UTC 时间")
    mirrors.add_argument("--group", action="append", default=[], help="镜像组，逗号分隔的 https 基地址；可重复")
    mirrors.add_argument("--mirror", action="append", default=[], help="单个镜像基地址；全部放入一个组")
    mirrors.add_argument("--sign-key", action="append", default=[], help="在线私钥，可重复")
    mirrors.set_defaults(func=cmd_mirrors)

    validate = subparsers.add_parser("validate", help="校验整棵 updater 树")
    validate.add_argument("--tree", required=True, help="updater 树根目录")
    validate.add_argument("--root-pub", action="append", default=[], help="固定的根公钥，用于校验 keys 文档；格式同 keys 命令")
    validate.add_argument("--check-content", action="store_true", help="校验内容文件的大小与 SHA-256")
    validate.set_defaults(func=cmd_validate)

    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        _ed25519_self_test()
        args.func(args)
    except ValueError as e:
        print("错误: %s" % e, file=sys.stderr)
        return 1
    except OSError as e:
        print("错误: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
