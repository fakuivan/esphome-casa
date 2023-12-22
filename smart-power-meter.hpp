#pragma once
#include <tuple>
#include <unordered_map>

namespace power_meter {

using config_field = uint8_t[4];
template<typename SetTrip, typename SetThr>
using id_map = std::unordered_map<uint8_t, std::tuple<SetTrip, SetThr>>;

config_field* find_config(uint8_t* configs, size_t configs_n, uint8_t id) {
    for (size_t i = 0; i<configs_n; i++) {
        if (configs[i*4] != id) {
            continue;
        }
        return (config_field*)(&configs[i*4]);
    }
    return nullptr;
}

template<size_t N>
config_field* find_config(uint8_t (&configs)[N], uint8_t id) {
    return find_config(&configs[0], N/4, id);
}

// Do not hang onto the return value of this if the configs container has been modified
template<typename Container>
config_field* find_config(Container &configs, uint8_t id) {
    return find_config(configs.data(), configs.size()/4, id);
}

uint8_t get_id(const config_field &field) {
    return field[0];
}

bool get_trip(const config_field &field) {
    return field[1];
}

void set_trip(config_field &field, bool trip) {
    field[1] = trip ? 1 : 0;
}

uint16_t get_threshold(const config_field &field) {
    return field[2] << 8 | field[3];
}

void set_threshold(config_field &field, uint16_t threshold) {
    field[2] = (threshold >> 8) & 0xff;
    field[3] = threshold & 0xff;
}

template<typename Container>
void update_values(
    const std::unordered_map<uint8_t, std::tuple<esphome::template_::TemplateSwitch*, esphome::template_::TemplateNumber*>> &map,
    const Container &configs
) {  
    const auto data = configs.data();
    const auto size = configs.size();
    for (size_t i = 0; i<size/4; i++) {
        const config_field& field = *(config_field*)(&data[i*4]);
        const auto field_id = get_id(field);
        const auto kv = map.find(field_id);
        if (kv != map.end()) {
            const auto funcs = kv->second;
            std::get<0>(funcs)->publish_state(get_trip(field));
            std::get<1>(funcs)->publish_state(get_threshold(field));
        }
    }

}

}
