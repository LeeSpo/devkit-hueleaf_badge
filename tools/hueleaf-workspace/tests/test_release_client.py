"""Actual HTTP downloads and interactive input, with no GitHub/network dependency."""
from contextlib import contextmanager
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import threading
from urllib.parse import parse_qs, unquote, urlsplit

import pytest

from release_client import ReleaseClient, local_input, resolve_source


@contextmanager
def github_server(pages=None, status=200, truncate=False):
    class Requests(list):
        headers = None
    seen = Requests()
    seen.headers = []
    pages = pages or [[{"tag_name": "v2.0.0", "assets": []}]]
    binary = b"plain user firmware, deliberately without a checksum"
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass
        def do_GET(self):
            seen.append(self.path)
            seen.headers.append(self.headers.get("Authorization"))
            parsed = urlsplit(self.path)
            if parsed.path == "/asset.bin":
                data = binary
                self.send_response(200)
                self.send_header("Content-Length", str(len(data) + (10 if truncate else 0)))
                self.end_headers()
                self.wfile.write(data)
                return
            if status != 200:
                self.send_error(status)
                return
            if "/tags/" in parsed.path:
                tag = unquote(parsed.path.split("/tags/", 1)[1])
                release = next((r for page in pages for r in page if r["tag_name"] == tag), None)
                if release is None:
                    self.send_error(404)
                    return
                data = json.dumps(release).encode()
            else:
                page = int(parse_qs(parsed.query).get("page", ["1"])[0])
                data = json.dumps(pages[page - 1] if page <= len(pages) else []).encode()
            self.send_response(200)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    base = f"http://127.0.0.1:{server.server_port}"
    for page in pages:
        for release in page:
            for asset in release.get("assets", []):
                asset["browser_download_url"] = base + "/asset.bin"
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield ReleaseClient(api_root=base), seen, binary
    finally:
        server.shutdown()
        thread.join()
        server.server_close()


def release(tag="v2.0.0", prerelease=False):
    return {"tag_name": tag, "prerelease": prerelease,
            "assets": [{"id": 42, "name": "hueleaf-firmware-2.0.0.bin"},
                       {"id": 43, "name": "hueleaf-installer.zip"}]}


def test_lists_new_releases_and_prereleases_without_separate_channels():
    old = {"tag_name": "v1.4.0", "assets": [{"name": "hueleaf-gallery-1.4.0.zip"}]}
    draft = {**release("draft"), "draft": True}
    with github_server([[release(), release("v2.1.0-rc1", True), old, draft]]) as (client, seen, _):
        assert [r["tag_name"] for r in client.releases()] == ["v2.0.0", "v2.1.0-rc1"]


def test_list_pagination():
    first = [{"tag_name": f"old-{n}", "assets": []} for n in range(100)]
    with github_server([first, [release()]]) as (client, seen, _):
        assert [r["tag_name"] for r in client.releases()] == ["v2.0.0"]
        assert any("page=2" in path for path in seen)


def test_exact_tag_downloads_binary_without_checksum_requests(tmp_path):
    with github_server([[release("preview/2.0")]]) as (client, seen, binary):
        path = resolve_source("preview/2.0", tmp_path, client)
        assert path.read_bytes() == binary
        assert any("preview%2F2.0" in url for url in seen)
        assert len(seen) == 2
        assert not list(tmp_path.rglob("*.part"))


def test_optional_token_is_sent_only_to_api_not_asset_download(tmp_path):
    with github_server([[release()]]) as (client, seen, binary):
        client.token = "example-token"
        assert resolve_source("v2.0.0", tmp_path, client).read_bytes() == binary
        assert seen.headers == ["Bearer example-token", None]


def test_numeric_interactive_selection(tmp_path, monkeypatch):
    monkeypatch.setattr("builtins.input", lambda _: "1")
    with github_server([[release()]]) as (client, seen, binary):
        assert resolve_source(None, tmp_path, client).read_bytes() == binary
        assert not any("/tags/" in path for path in seen)


def test_numeric_tag_on_command_line_is_not_an_index(tmp_path):
    with github_server([[release("123")]]) as (client, seen, binary):
        assert resolve_source("123", tmp_path, client).read_bytes() == binary
        assert any("/tags/123" in path for path in seen)


def test_offline_local_file_never_queries_github(tmp_path):
    path = tmp_path / "firmware with spaces.bin"
    path.write_bytes(b"anything")
    with github_server(status=500) as (client, seen, _):
        assert resolve_source(f'"{path}"', tmp_path, client) == path.resolve()
        assert seen == []


def test_list_failure_still_accepts_local_file(tmp_path, monkeypatch):
    path = tmp_path / "firmware.bin"
    path.write_bytes(b"app")
    monkeypatch.setattr("builtins.input", lambda _: str(path))
    with github_server(status=403) as (client, seen, _):
        assert resolve_source(None, tmp_path, client) == path


@pytest.mark.parametrize("value", ["./missing.bin", "C:\\Downloads\\missing.bin", "/does/not/exist.bin"])
def test_missing_local_path_is_not_queried_as_tag(value):
    with pytest.raises(ValueError, match="文件不存在"):
        local_input(value)


def test_tag_without_bin_has_clear_error():
    old = {"tag_name": "v1.4.0", "assets": [{"name": "old.zip"}]}
    with github_server([[old]]) as (client, _, _):
        with pytest.raises(RuntimeError, match="新格式"):
            client.release("v1.4.0")
        with pytest.raises(RuntimeError, match="没有 Release"):
            client.release("git-tag-only")


def test_multiple_binary_assets_are_selected_by_filename(tmp_path, monkeypatch):
    item = release()
    item["assets"].append({"id": 99, "name": "another.bin"})
    monkeypatch.setattr("builtins.input", lambda _: "2")
    with github_server([[item]]) as (client, _, binary):
        path = resolve_source("v2.0.0", tmp_path, client)
        assert path.name == "another.bin"
        assert path.read_bytes() == binary


def test_incomplete_download_never_leaves_a_final_file(tmp_path):
    with github_server([[release()]], truncate=True) as (client, _, _):
        with pytest.raises(RuntimeError, match="下载未完成"):
            resolve_source("v2.0.0", tmp_path, client)
        assert not list(tmp_path.rglob("*.part"))
        assert not list(tmp_path.rglob("*.bin"))
