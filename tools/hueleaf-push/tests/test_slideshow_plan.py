"""Combined saves use current snapshots and never hide partial/uncertain writes."""
import json
import sys
from pathlib import Path

import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import hueleaf_gui as G


class PlanDevice:
    def __init__(self, fail=None, ignore=None, lose_readback=False):
        self.order = ['a.bin', 'b.bin', 'c.bin']
        self.names = set(self.order)
        self.token = 1
        self.custom = False
        self.info = dict(slideshow_supported=True, slideshow_selection_supported=True,
                         tap_wake_supported=True, slideshow_enabled=False,
                         slideshow_interval_min=20, tap_wake=False)
        self.writes = []
        self.fail, self.ignore, self.lose_readback = fail, ignore, lose_readback

    def ok(self, cmd):
        assert cmd == 'INFO'
        if self.lose_readback and self.writes:
            raise TimeoutError('disconnected')
        return json.dumps(self.info).encode()

    def list(self):
        return self.order[:]

    def slideshow_selection(self):
        return dict(token=f'{self.token:016x}', custom=self.custom, fault=False,
                    items=[dict(id=n, selected=n in self.names) for n in self.order])

    def write(self, stage):
        self.writes.append(stage)
        if self.fail == stage:
            raise OSError('disk full')
        return self.ignore != stage

    def set_order(self, order):
        if self.write('order'):
            self.order = order[:]
            self.token += 1

    def set_slideshow_selection(self, snapshot, names):
        assert snapshot['token'] == f'{self.token:016x}'
        if self.write('selection'):
            self.names, self.custom = set(names), True
            self.token += 1

    def set_slideshow(self, enabled, minutes, tap=None):
        if self.write('settings'):
            self.info.update(slideshow_enabled=enabled, slideshow_interval_min=minutes)
            if tap is not None:
                self.info['tap_wake'] = tap


def plan():
    return dict(token='0000000000000001', order=['c.bin', 'b.bin', 'a.bin'],
                names=['c.bin', 'a.bin'], settings=dict(enabled=True, interval_min=30))


def test_combined_order_selection_timer_readback():
    d = PlanDevice()
    result = G.save_slideshow_plan(d, plan())
    assert result['ok'] and result['confirmed'] == ['order', 'selection', 'settings']
    assert d.writes == ['order', 'selection', 'settings']
    assert result['order'] == plan()['order']
    assert result['info']['slideshow_interval_min'] == 30


@pytest.mark.parametrize('failure,confirmed', [
    ('order', []), ('selection', ['order']), ('settings', ['order', 'selection'])])
def test_partial_save_stops_and_reports_actual_state(failure, confirmed):
    d = PlanDevice(fail=failure)
    result = G.save_slideshow_plan(d, plan())
    assert not result['ok'] and result['failed'] == failure
    assert result['confirmed'] == confirmed
    assert d.writes == ['order', 'selection', 'settings'][:len(confirmed)+1]
    assert result['info']['slideshow_enabled'] is False
    assert result['order'] == (plan()['order'] if confirmed else ['a.bin', 'b.bin', 'c.bin'])


def test_timer_only_preserves_default_future_image_membership():
    d = PlanDevice()
    result = G.save_slideshow_plan(d, dict(settings=dict(enabled=True, interval_min=30)))
    assert result['ok'] and d.writes == ['settings']
    assert result['selection']['custom'] is False


def test_stale_token_rejected_before_any_write():
    d = PlanDevice()
    body = plan(); body['token'] = '0000000000000000'
    result = G.save_slideshow_plan(d, body)
    assert not result['ok'] and result['failed'] == 'read' and not d.writes


@pytest.mark.parametrize('stage', ['order', 'selection', 'settings'])
def test_success_ack_without_matching_readback_is_not_confirmed(stage):
    d = PlanDevice(ignore=stage)
    result = G.save_slideshow_plan(d, plan())
    assert not result['ok'] and result['failed'] == stage
    assert stage not in result['confirmed']


def test_disconnect_never_returns_stale_prewrite_info_as_current():
    d = PlanDevice(lose_readback=True)
    result = G.save_slideshow_plan(d, dict(settings=dict(enabled=True, interval_min=30)))
    assert result['state_unknown'] and not result['ok']
    assert 'info' not in result and 'selection' not in result


@pytest.fixture
def plan_http(monkeypatch):
    import threading
    import urllib.request
    import urllib.error
    from http.server import ThreadingHTTPServer
    d = PlanDevice()
    monkeypatch.setattr(G, 'device', lambda port: d)
    monkeypatch.setattr(G, '_operations', {})
    server = ThreadingHTTPServer(('127.0.0.1', 0), G.H)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()

    def post(body):
        request = urllib.request.Request(f'http://127.0.0.1:{server.server_port}/api/slideshow-plan',
                                        json.dumps(body).encode(), {'Content-Type': 'application/json'})
        try:
            with urllib.request.urlopen(request) as response:
                return response.status, json.load(response)
        except urllib.error.HTTPError as e:
            return e.code, json.load(e)
    yield d, post
    server.shutdown(); worker.join(); server.server_close()


def test_http_composition_and_validation(plan_http):
    d, post = plan_http
    for invalid in [None, {}, {'port':'TEST','settings':{'enabled':True,'interval_min':False}},
                    {'port':'TEST','names':['../a.bin'],'token':'0000000000000001'},
                    {'port':'TEST','names':['a.bin','a.bin'],'token':'0000000000000001'}]:
        assert post(invalid)[0] == 400
        assert not d.writes
    status, result = post(dict(port='TEST', **plan()))
    assert status == 200 and result['ok']
    assert not G._operations
