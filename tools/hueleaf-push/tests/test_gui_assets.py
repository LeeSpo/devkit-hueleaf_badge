from html.parser import HTMLParser
from http.server import ThreadingHTTPServer
import json
from pathlib import Path
import sys
import threading
import urllib.error
import urllib.request

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import hueleaf_gui as G


class PageAssets(HTMLParser):
    def __init__(self):
        super().__init__()
        self.assets = []
        self.inline_assets = []
        self.ids = []

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if "id" in attrs:
            self.ids.append(attrs["id"])
        if tag == "script":
            if "src" in attrs:
                self.assets.append(attrs["src"])
            else:
                self.inline_assets.append(tag)
        if tag == "style":
            self.inline_assets.append(tag)
        if tag == "link" and attrs.get("rel") == "stylesheet":
            self.assets.append(attrs["href"])


@pytest.fixture
def assets(monkeypatch, tmp_path):
    # Asset paths must not depend on where a launcher was started.
    monkeypatch.chdir(tmp_path)
    monkeypatch.setattr(G, "device", lambda port: pytest.fail("static routes must not open USB"))
    server = ThreadingHTTPServer(("127.0.0.1", 0), G.H)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()

    def fetch(path, method="GET"):
        request = urllib.request.Request(
            f"http://127.0.0.1:{server.server_port}{path}", method=method)
        try:
            with urllib.request.urlopen(request, timeout=5) as response:
                return response.status, response.headers, response.read()
        except urllib.error.HTTPError as error:
            return error.code, error.headers, error.read()

    yield fetch
    server.shutdown()
    server.server_close()
    worker.join(timeout=5)


def test_page_loads_external_assets_from_any_working_directory(assets):
    status, headers, body = assets("/")
    assert status == 200
    assert headers.get_content_type() == "text/html"
    assert headers.get_content_charset() == "utf-8"
    assert headers["Cache-Control"] == "no-store"
    page = PageAssets()
    page.feed(body.decode("utf-8"))
    assert not page.inline_assets
    assert len(page.ids) == len(set(page.ids)), "IDs must be unique across the three views"
    assert set(page.assets) == {"/web/styles.css", "/web/app.js"}
    for path in page.assets:
        status, headers, body = assets(path + "?v=layout")
        filename, content_type = G.WEB_ASSETS[path]
        assert status == 200
        assert headers["Content-Type"] == content_type
        assert headers["Cache-Control"] == "no-store"
        assert body == (G.WEB_DIR / filename).read_bytes()


@pytest.mark.parametrize("path", [
    "/hueleaf_gui.py", "/web/../hueleaf_gui.py", "/web/%2e%2e/hueleaf_gui.py",
    "/web/index.html", "/web/", "/.hueleaf/settings.json",
])
def test_static_routes_only_expose_allowlisted_assets(assets, path):
    status, _, body = assets(path)
    assert status == 404
    assert json.loads(body) == {"error": "not found"}


def test_static_assets_are_read_only(assets):
    for path in G.WEB_ASSETS:
        assert assets(path, method="POST")[0] == 404


def test_missing_asset_is_reported(assets, monkeypatch, tmp_path):
    monkeypatch.setattr(G, "WEB_DIR", tmp_path)
    status, _, body = assets("/web/app.js")
    assert status == 404
    assert "web/" in json.loads(body)["error"]


def test_startup_reports_missing_web_directory(monkeypatch, tmp_path, capsys):
    monkeypatch.setattr(G, "WEB_DIR", tmp_path)
    assert G.main(open_browser=False) == 1
    assert "web/" in capsys.readouterr().err
