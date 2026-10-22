"""Validate device-ID keywords without creating a ROS node or connecting to hardware."""

import unittest

from light_curtain_nodes.gc_modbus_driver import _slave_kwarg


class CurrentClient:
    def read_holding_registers(self, address, *, count=1, device_id=1):
        raise AssertionError('The test must not issue reads')


class PreviousClient:
    def read_holding_registers(self, address, *, count=1, slave=1):
        raise AssertionError('The test must not issue reads')


class LegacyClient:
    def read_holding_registers(self, address, count=1, unit=1):
        raise AssertionError('The test must not issue reads')


class DeviceIdCompatibility(unittest.TestCase):
    def test_current_device_id(self):
        self.assertEqual(_slave_kwarg(CurrentClient(), 42), {'device_id': 42})

    def test_previous_slave(self):
        self.assertEqual(_slave_kwarg(PreviousClient(), 42), {'slave': 42})

    def test_legacy_unit(self):
        self.assertEqual(_slave_kwarg(LegacyClient(), 42), {'unit': 42})


if __name__ == '__main__':
    unittest.main()
