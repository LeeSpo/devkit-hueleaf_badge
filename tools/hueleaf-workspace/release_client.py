"""Public GitHub Releases, starting with the new firmware-BIN attachments."""
from http.client import HTTPException
import json
import os
from pathlib import Path
import re
import tempfile
from urllib.error import HTTPError, URLError
from urllib.parse import quote
from urllib.request import Request, urlopen

REPOSITORY = "LeeSpo/devkit-hueleaf_badge"


def firmware_assets(release):
    return [asset for asset in release.get("assets", []) if asset.get("name", "").lower().endswith(".bin")]


class ReleaseClient:
    def __init__(self, repository=REPOSITORY, api_root="https://api.github.com", token=None):
        self.base = f"{api_root.rstrip('/')}/repos/{repository}/releases"
        self.token = token or os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")

    def json(self, url):
        request = Request(url, headers={"Accept": "application/vnd.github+json", "User-Agent": "HueLeaf-installer"})
        if self.token:
            # API only: neither cross-host redirects nor asset downloads get it.
            request.add_unredirected_header("Authorization", f"Bearer {self.token}")
        try:
            with urlopen(request, timeout=30) as response:
                return json.load(response)
        except HTTPError as exc:
            if exc.code == 404:
                raise RuntimeError("该标签没有 Release 或可下载固件") from exc
            if exc.code in (403, 429):
                raise RuntimeError("GitHub 请求受限，请稍后重试、设置 GH_TOKEN，或直接输入本地 BIN 路径") from exc
            raise RuntimeError(f"GitHub 请求失败：HTTP {exc.code}") from exc
        except (URLError, TimeoutError, HTTPException) as exc:
            raise RuntimeError(f"无法连接 GitHub：{exc}；可直接输入本地 BIN 路径") from exc

    def releases(self):
        result = []
        page = 1
        while True:
            batch = self.json(f"{self.base}?per_page=100&page={page}")
            result.extend(release for release in batch if not release.get("draft") and firmware_assets(release))
            if len(batch) < 100:
                return result
            page += 1

    def release(self, tag):
        release = self.json(f"{self.base}/tags/{quote(tag, safe='')}")
        if release.get("draft") or not firmware_assets(release):
            raise RuntimeError(f"{tag} 没有新格式的固件 BIN；此工具从新的发布格式开始支持")
        return release

    def download(self, release, asset, directory):
        tag = re.sub(r"[^A-Za-z0-9._-]", "_", release["tag_name"]).strip(".") or "release"
        directory = Path(directory) / tag / str(asset["id"])
        directory.mkdir(parents=True, exist_ok=True)
        name = asset["name"]
        if Path(name).name != name or "\\" in name:
            raise ValueError("固件附件名无效")
        target = directory / name
        temporary = None
        request = Request(asset["browser_download_url"], headers={"User-Agent": "HueLeaf-installer"})
        print(f"下载 {release['tag_name']} / {name}")
        try:
            with urlopen(request, timeout=60) as response, tempfile.NamedTemporaryFile(dir=directory, suffix=".part", delete=False) as output:
                temporary = Path(output.name)
                total = int(response.headers.get("Content-Length", 0))
                received = 0
                while chunk := response.read(256 * 1024):
                    output.write(chunk)
                    received += len(chunk)
                    print(f"\r已下载 {received:,}" + (f" / {total:,} 字节" if total else " 字节"), end="", flush=True)
            print()
            if not received or (total and received != total):
                raise RuntimeError("下载未完成，请重试")
            temporary.replace(target)
            return target
        except (HTTPError, URLError, TimeoutError, HTTPException, ConnectionError) as exc:
            raise RuntimeError(f"固件下载失败：{exc}；也可以手动下载后输入本地路径") from exc
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)


def show_releases(releases):
    if not releases:
        print("暂无新格式固件 Release，可输入本地 BIN 路径。")
    for index, release in enumerate(releases, 1):
        suffix = "（预发布）" if release.get("prerelease") else ""
        print(f"  {index}. {release['tag_name']}{suffix}")


def local_input(value):
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
        value = value[1:-1]
    path = Path(value).expanduser()
    if path.exists():
        if not path.is_file() or path.suffix.lower() != ".bin":
            raise ValueError("请选择固件 BIN 文件")
        return value, path.resolve()
    if path.suffix.lower() == ".bin" or value.startswith(("/", "~", ".", "\\")) or re.match(r"^[A-Za-z]:", value):
        raise ValueError(f"文件不存在：{path}")
    return value, None


def resolve_source(value, directory, client=None):
    client = client or ReleaseClient()
    releases = []
    if value is None:
        try:
            releases = client.releases()
            show_releases(releases)
        except RuntimeError as exc:
            print(exc)
        value = input("输入序号、Release 标签或本地 BIN 路径（留空返回）：").strip()
        if not value:
            return None
        if value.isdigit() and 1 <= int(value) <= len(releases):
            value = releases[int(value) - 1]["tag_name"]
    value, local = local_input(value)
    if local:
        return local
    release = next((item for item in releases if item["tag_name"] == value), None) or client.release(value)
    assets = firmware_assets(release)
    if len(assets) == 1:
        asset = assets[0]
    else:
        for index, candidate in enumerate(assets, 1):
            print(f"  {index}. {candidate['name']}")
        choice = input("选择固件附件序号：").strip()
        if not choice.isdigit() or not 1 <= int(choice) <= len(assets):
            raise ValueError("无效的附件序号")
        asset = assets[int(choice) - 1]
    return client.download(release, asset, directory)
