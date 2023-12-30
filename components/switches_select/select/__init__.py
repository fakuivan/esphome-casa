import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select, switch
from esphome.const import CONF_ID

from .. import switches_select_ns

SwitchesSelect = switches_select_ns.class_(
    "SwitchesSelect", select.Select, cg.Component
)

CONF_SWITCHES = "switches"

CONFIG_SCHEMA = (
    select.select_schema(SwitchesSelect)
    .extend(
        {
            cv.Required(CONF_SWITCHES): cv.All(
                cv.ensure_list(cv.use_id(switch.Switch)), cv.Length(min=1)
            )
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    select_id = cg.new_Pvariable(config[CONF_ID])
    await select.register_select(select_id, config, options=[])
    await cg.register_component(select_id, config)

    switches = [await cg.get_variable(switch) for switch in config[CONF_SWITCHES]]
    cg.add(select_id.set_switches(switches))
