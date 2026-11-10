"""Regression checks for external-component library selection (no radio IO)."""

import asyncio
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock, MagicMock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from components import eybond_collector as component


class LibrarySelectionTests(unittest.TestCase):
    def run_codegen(self, *, esp32, arduino):
        interval = SimpleNamespace(total_milliseconds=1000)
        config = {
            "id": "bridge", "uart_id": "uart", "udp_port": 58899,
            "heartbeat_interval": interval, "response_gap": interval,
            "response_timeout": interval, "command_spacing": interval,
            "devcode": 0, "collector_addr": 1,
            "ble_provisioning": False, "web_configuration": False,
        }
        core = SimpleNamespace(is_esp32=esp32, using_arduino=arduino, config={})
        codegen = MagicMock()
        codegen.register_component = AsyncMock()
        with patch.object(component, "CORE", core), patch.object(component, "cg", codegen), \
                patch.object(component.uart, "register_uart_device", new_callable=AsyncMock):
            asyncio.run(component.to_code(config))
        return codegen

    def test_esp32_arduino_explicitly_enables_wifi(self):
        self.run_codegen(esp32=True, arduino=True).add_library.assert_called_once_with("WiFi", None)

    def test_other_arduino_platforms_keep_native_libraries(self):
        self.run_codegen(esp32=False, arduino=True).add_library.assert_not_called()

    def test_no_arduino_library_for_idf(self):
        # IDF is currently rejected by CONFIG_SCHEMA; keep this guard explicit.
        self.run_codegen(esp32=True, arduino=False).add_library.assert_not_called()


if __name__ == "__main__":
    unittest.main()
