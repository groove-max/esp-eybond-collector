"""Select platform: runtime inverter-UART baud rate (PI30 2400 <-> SMG 9600)."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import CONF_RESTORE_VALUE

from . import EybondCollector, eybond_collector_ns

CONF_EYBOND_COLLECTOR_ID = "eybond_collector_id"

BAUD_OPTIONS = ["1200", "2400", "4800", "9600", "19200", "38400", "57600", "115200"]

EybondBaudSelect = eybond_collector_ns.class_(
    "EybondBaudSelect", select.Select, cg.Component
)

CONFIG_SCHEMA = (
    select.select_schema(EybondBaudSelect)
    .extend(
        {
            cv.GenerateID(CONF_EYBOND_COLLECTOR_ID): cv.use_id(EybondCollector),
            cv.Optional(CONF_RESTORE_VALUE, default=False): cv.boolean,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await select.new_select(config, options=BAUD_OPTIONS)
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_EYBOND_COLLECTOR_ID])
    cg.add(var.set_restore_value(config[CONF_RESTORE_VALUE]))
