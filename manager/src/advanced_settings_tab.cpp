/*
    sys-clk manager, a sys-clk frontend homebrew
    Copyright (C) 2019  natinusala
    Copyright (C) 2019  p-sam
    Copyright (C) 2019  m4xw

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "advanced_settings_tab.h"

#include <sysclk.h>
#include "utils.h"
#include "fan_settings_frame.h"

AdvancedSettingsTab::AdvancedSettingsTab()
{
    // Get config values
    sysclkIpcGetConfigValues(&this->configValues);

    // Uncapped Mod Settings
    this->addView(new brls::Header("Uncapped Mod Settings"));

    // 1. Only on Charging
    bool onlyCharge = this->configValues.values[SysClkConfigValue_OnlyOnCharging] != 0;
    brls::ToggleListItem* chargeItem = new brls::ToggleListItem("Limit to Charging (Recommended)", onlyCharge);
    chargeItem->getClickEvent()->subscribe([this, chargeItem](brls::View* v) {
        this->configValues.values[SysClkConfigValue_OnlyOnCharging] = chargeItem->getToggleState() ? 1 : 0;
        sysclkIpcSetConfigValues(&this->configValues);
    });
    this->addView(chargeItem);

    // 2. Unlock GPU for current SoC type
    if (g_socType == SysClkSocType_Mariko) {
        bool mEnabled = this->configValues.values[SysClkConfigValue_UnlockGpuMariko] != 0;
        brls::ToggleListItem* marikoItem = new brls::ToggleListItem("Unlock GPU (Mariko/OLED/Lite)", mEnabled);
        marikoItem->getClickEvent()->subscribe([this, marikoItem](brls::View* v) {
            bool newState = marikoItem->getToggleState();
            if (newState) {
                brls::Dialog* diag = new brls::Dialog("WARNING: Higher GPU clocks can cause overheating on Mariko. Proceed?");
                diag->addButton("Cancel", [marikoItem, diag](brls::View* v) { 
                    marikoItem->setChecked(false); 
                    diag->close(); 
                });
                diag->addButton("Proceed", [this, marikoItem, diag](brls::View* v) {
                    this->configValues.values[SysClkConfigValue_UnlockGpuMariko] = 1;
                    sysclkIpcSetConfigValues(&this->configValues);
                    brls::Application::notify("Mariko limits removed!");
                    diag->close();
                });
                diag->open();
            } else {
                this->configValues.values[SysClkConfigValue_UnlockGpuMariko] = 0;
                sysclkIpcSetConfigValues(&this->configValues);
            }
        });
        this->addView(marikoItem);
    } else {
        bool eEnabled = this->configValues.values[SysClkConfigValue_UnlockGpuErista] != 0;
        brls::ToggleListItem* eristaItem = new brls::ToggleListItem("Unlock GPU (Erista/V1)", eEnabled);
        eristaItem->getClickEvent()->subscribe([this, eristaItem](brls::View* v) {
            bool newState = eristaItem->getToggleState();
            if (newState) {
                brls::Dialog* diag = new brls::Dialog("WARNING: Erista (V1) has lower thermal limits. Unlock higher clocks?");
                diag->addButton("Cancel", [eristaItem, diag](brls::View* v) { 
                    eristaItem->setChecked(false); 
                    diag->close(); 
                });
                diag->addButton("Proceed", [this, eristaItem, diag](brls::View* v) {
                    this->configValues.values[SysClkConfigValue_UnlockGpuErista] = 1;
                    sysclkIpcSetConfigValues(&this->configValues);
                    brls::Application::notify("Erista limits removed!");
                    diag->close();
                });
                diag->open();
            } else {
                this->configValues.values[SysClkConfigValue_UnlockGpuErista] = 0;
                sysclkIpcSetConfigValues(&this->configValues);
            }
        });
        this->addView(eristaItem);
    }

    // Fan Settings
    this->addView(new brls::Header("Fan Settings"));
    brls::ListItem* fanItem = new brls::ListItem("Fan Curve Configuration");
    fanItem->setValue("Edit");
    fanItem->getClickEvent()->subscribe([](brls::View* view) {
        brls::Application::pushView(new FanSettingsFrame());
    });
    this->addView(fanItem);
}