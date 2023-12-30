#include "switches_select.h"
#include "esphome/core/log.h"
#include <map>

namespace esphome
{
    namespace switches_select
    {

        static const char *const TAG = "switches_select.select";

        void SwitchesSelect::setup()
        {
            this->modifying_ = std::vector<bool>(this->switches_.size(), false);
            std::vector<std::string> options(this->switches_.size());
            for (int i = 0; i < this->switches_.size(); i++)
            {
                const auto switch_ = this->switches_[i];
                const auto name = switch_->get_name();
                options[i] = name;
                switch_->add_on_state_callback(
                    [this, i](float value)
                    {
                        if (this->modifying_[i])
                        {
                            this->modifying_[i] = false;
                            return;
                        }
                        if (value != 0.)
                            this->publish_state(this->switches_[i]->get_name());
                    });
            }
            this->traits.set_options(options);
        }
        void SwitchesSelect::dump_config() { LOG_SELECT("", "Switches Select", this); }

        void SwitchesSelect::control(const std::string &value)
        {
            for (int i = 0; i < this->switches_.size(); i++)
            {
                auto *switch_ = this->switches_[i];
                bool switch_to = switch_->get_name() == value;
                if (switch_to != switch_->state)
                {
                    this->modifying_[i] = true;
                    switch_to ? switch_->turn_on() : switch_->turn_off();
                }
            }
            this->publish_state(value);
        }
    }
}
