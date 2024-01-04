import esphome.codegen as cg
from esphome import config_validation as cv, automation
from esphome.const import (
    CONF_ID,
    CONF_STATE,
)
from esphome.components import binary_sensor, switch

# Typing imports
import esphome.util
from esphome.core import ID

energy_management_ns = cg.esphome_ns.namespace("energy_management")

CONF_LOAD_SHED_SWITCH = "load_shed_switch"
CONF_ENERGY_SAVING_SWITCH = "energy_saving_switch"
CONF_TURN_ON_AFTER_SHEDDING_SWITCH = "turn_on_after_shedding_switch"
CONF_ENERGY_SAVING_OVERWRITTEN = "energy_saving_overwritten_sensor"
CONF_REQUESTED_SHEDDING_STOP = "requested_shedding_stop_sensor"
CONF_SET_DEVICE_STATE = "set_device_state"

EnergyManagementComponent = energy_management_ns.class_(
    "EnergyManagementComponent", cg.Component
)

OptimisticSwitch = energy_management_ns.class_(
    "OptimisticSwitch", switch.Switch, cg.Component
)

EnergyManagementSetDeviceStateCondition = energy_management_ns.class_(
    "EnergyManagementSetDeviceStateCondition", automation.Condition
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EnergyManagementComponent),
        cv.Required(CONF_LOAD_SHED_SWITCH): switch.switch_schema(OptimisticSwitch),
        cv.Required(CONF_ENERGY_SAVING_SWITCH): switch.switch_schema(OptimisticSwitch),
        cv.Required(CONF_TURN_ON_AFTER_SHEDDING_SWITCH): switch.switch_schema(
            OptimisticSwitch
        ),
        cv.Required(
            CONF_ENERGY_SAVING_OVERWRITTEN
        ): binary_sensor.binary_sensor_schema(),
        cv.Required(CONF_REQUESTED_SHEDDING_STOP): binary_sensor.binary_sensor_schema(),
        cv.Required(CONF_SET_DEVICE_STATE): {
            cv.Required("lambda"): cv.returning_lambda
        },
    }
).extend(cv.COMPONENT_SCHEMA)

AUTO_LOAD = ["switch", "binary_sensor"]


async def to_code(config: esphome.util.OrderedDict):
    component = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(component, config)
    switches_and_sensors = [
        await switch.new_switch(config[entry])
        for entry in [
            CONF_TURN_ON_AFTER_SHEDDING_SWITCH,
            CONF_LOAD_SHED_SWITCH,
            CONF_ENERGY_SAVING_SWITCH,
        ]
    ] + [
        await binary_sensor.new_binary_sensor(config[entry])
        for entry in [CONF_REQUESTED_SHEDDING_STOP, CONF_ENERGY_SAVING_OVERWRITTEN]
    ]
    cg.add(component.set_switches_and_sensors(*switches_and_sensors))
    set_device_state_lambda = await cg.process_lambda(
        config[CONF_SET_DEVICE_STATE]["lambda"],
        [(cg.bool_, "switch_to")],
        return_type=cg.bool_,
    )
    cg.add(component.set_device_state_lambda(set_device_state_lambda))


@automation.register_condition(
    "energy_management.set_device_state",
    EnergyManagementSetDeviceStateCondition,
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.use_id(EnergyManagementComponent),
            cv.Required(CONF_STATE): cv.templatable(cv.boolean),
        }
    ),
)
async def energy_management_set_to_code(
    config: esphome.util.OrderedDict, action_id: ID, template_arg, args
):
    parent_id = await cg.get_variable(config[CONF_ID])
    component = cg.new_Pvariable(action_id, template_arg, parent_id)
    template_ = await cg.templatable(config[CONF_STATE], args, bool)
    cg.add(component.set_state(template_))
    return component
