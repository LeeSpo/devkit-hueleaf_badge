import ast
import pathlib
import subprocess
import sys


def test_mono_optin(tmp_path):
    root = pathlib.Path(__file__).resolve().parents[2]
    # Reuse the startup test's platform declarations; main.c itself is not copied.
    startup = ast.parse((root / 'tools/tests/test_startup.py').read_text())
    mock = next(node.value for node in ast.walk(startup)
                if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)
                and node.func.attr == 'write_text' and node.args
                for node in [node.args[0]]
                if isinstance(node, ast.Constant) and isinstance(node.value, str)
                and '#ifndef MOCK_H' in node.value)
    mock = mock.replace('#define CONFIG_HL_MONO_FAST 0', '#define CONFIG_HL_MONO_FAST 1')
    mock = mock.replace('#endif\n', '''
typedef int nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
int nvs_open(const char *, int, nvs_handle_t *);
int nvs_get_u8(nvs_handle_t, const char *, uint8_t *);
int nvs_set_u8(nvs_handle_t, const char *, uint8_t);
int nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
void screen_driver_set_mono_fast(bool);
#endif
''')
    (tmp_path / 'mock.h').write_text(mock)
    headers = ['axp2101.h', 'board.h', 'bootloader_common.h', 'esp_log.h',
               'esp_ota_ops.h', 'esp_system.h', 'freertos/FreeRTOS.h',
               'freertos/task.h', 'nvs_flash.h', 'pcf8563.h', 'screen_driver.h',
               'tf_card.h', 'sdkconfig.h', 'nvs.h', 'esp_err.h']
    for header in headers:
        path = tmp_path / header
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include "mock.h"\n')
    binary = tmp_path / 'mono_optin'
    dead_strip = '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections'
    subprocess.run(['cc', '-std=c11', '-ffunction-sections', '-fdata-sections',
                    '-I', str(tmp_path), '-I', str(root / 'main'),
                    str(root / 'test_host/mono_optin_test.c'),
                    str(root / 'main/hl_cfg.c'), dead_strip, '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
