from test_slideshow import slideshow  # shared HTTP/device fixture
import hueleaf_push as P


def test_tap_wake_save_toggle_and_legacy_preservation(slideshow):
    fake, call = slideshow
    body = {"port": "FAKE", "enabled": True, "interval_min": 30, "tap_wake": True}
    assert call(body) == (200, {"ok": True})
    assert fake.tap_wake and len(fake.payloads[-1]) == 6
    # Saving with an older client must not turn off the independent option.
    assert call({k: v for k, v in body.items() if k != "tap_wake"})[0] == 200
    assert fake.tap_wake and len(fake.payloads[-1]) == 5
    fake.write_error = -7
    assert call({**body, "tap_wake": False})[0] == 500
    assert fake.tap_wake
    fake.write_error = 0
    assert call({**body, "tap_wake": False})[0] == 200
    assert not fake.tap_wake
    assert P.CMD["SHOW"] not in fake.commands and P.CMD["REBOOT"] not in fake.commands


def test_no_six_byte_payload_to_older_slideshow_firmware(slideshow):
    fake, call = slideshow
    fake.tap_supported = False
    body = {"port": "FAKE", "enabled": True, "interval_min": 30}
    assert call({**body, "tap_wake": True})[0] == 500
    assert not fake.payloads
    assert call(body)[0] == 200
    assert len(fake.payloads[-1]) == 5
