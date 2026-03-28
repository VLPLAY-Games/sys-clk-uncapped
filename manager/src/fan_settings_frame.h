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

#pragma once

#include <borealis.hpp>
#include <string>
#include <vector>

struct FanTableEntry {
    int minTemp;
    int maxTemp;
    int minPwm;
    int maxPwm;
};

class FanSettingsFrame : public brls::ThumbnailFrame
{
public:
    FanSettingsFrame();
    ~FanSettingsFrame() override;

    bool onCancel() override;

private:
    std::vector<FanTableEntry> handheldTable;
    std::vector<FanTableEntry> dockedTable;

    brls::List* list = nullptr;
    brls::Button* saveButton = nullptr;

    void loadFromIni();
    void saveToIni();
    void buildUI();

    static std::vector<FanTableEntry> parseTableString(const std::string& str);
    static std::string serializeTable(const std::vector<FanTableEntry>& table);
    static std::string getDefaultHandheldTable();
    static std::string getDefaultDockedTable();
};
