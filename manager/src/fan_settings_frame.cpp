#include "fan_settings_frame.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include <switch.h>
#include <borealis.hpp>

#define SYSTEM_SETTINGS_PATH "/atmosphere/config/system_settings.ini"
#define TC_SECTION "[tc]"
#define HANDHELD_KEY "tskin_rate_table_handheld"
#define DOCKED_KEY "tskin_rate_table_console"

namespace {
    static inline void trimInPlace(std::string& s) {
        auto start = std::find_if_not(s.begin(), s.end(), ::isspace);
        auto end   = std::find_if_not(s.rbegin(), s.rend(), ::isspace).base();
        s = (start < end) ? std::string(start, end) : "";
    }

    static inline bool startsWith(const std::string& s, const char* prefix) {
        return s.rfind(prefix, 0) == 0;
    }
}

// --- Парсинг и сериализация таблиц ---
std::vector<FanTableEntry> FanSettingsFrame::parseTableString(const std::string& str) {
    std::vector<FanTableEntry> result;

    std::string cleaned = str;
    cleaned.erase(std::remove_if(cleaned.begin(), cleaned.end(),
        [](unsigned char ch) { return std::isspace(ch) || ch == '"'; }),
        cleaned.end());

    if (cleaned.rfind("str!", 0) == 0) cleaned.erase(0, 4);
    if (cleaned.empty() || cleaned.front() != '[' || cleaned.back() != ']') return result;

    cleaned = cleaned.substr(1, cleaned.length() - 2);

    std::vector<std::string> entries;
    size_t start = 0, end;
    while ((end = cleaned.find("],[", start)) != std::string::npos) {
        entries.push_back(cleaned.substr(start, end - start));
        start = end + 3;
    }
    entries.push_back(cleaned.substr(start));

    for (const auto& entry : entries) {
        std::stringstream ss(entry);
        std::string token;
        std::vector<int> values;
        while (std::getline(ss, token, ',')) {
            if (!token.empty()) {
                try { values.push_back(std::stoi(token)); } catch (...) { continue; }
            }
        }
        if (values.size() == 4)
            result.push_back({values[0], values[1], values[2], values[3]});
    }
    return result;
}

std::string FanSettingsFrame::serializeTable(const std::vector<FanTableEntry>& table) {
    std::string result = "[";
    for (size_t i = 0; i < table.size(); ++i) {
        const auto& e = table[i];
        char buf[128];
        snprintf(buf, sizeof(buf), "[%d, %d, %d, %d]", e.minTemp, e.maxTemp, e.minPwm, e.maxPwm);
        result += buf;
        if (i != table.size() - 1) result += ", ";
    }
    result += "]";
    return result;
}

std::string FanSettingsFrame::getDefaultHandheldTable() {
    return "[[-1000000, 40000, 0, 0], [36000, 43000, 51, 51], [43000, 48000, 51, 102], [48000, 53000, 102, 153], [53000, 1000000, 153, 153], [48000, 1000000, 153, 153]]";
}

std::string FanSettingsFrame::getDefaultDockedTable() {
    return "[[-1000000, 40000, 0, 0], [36000, 43000, 51, 51], [43000, 53000, 51, 153], [53000, 58000, 153, 255], [58000, 1000000, 255, 255]]";
}

// --- Загрузка и сохранение INI ---
void FanSettingsFrame::loadFromIni() {
    FILE* f = fopen(SYSTEM_SETTINGS_PATH, "r");
    if (!f) {
        handheldTable = parseTableString(getDefaultHandheldTable());
        dockedTable   = parseTableString(getDefaultDockedTable());
        return;
    }

    char line[1024];
    bool inTcSection = false;
    std::string handheldStr, dockedStr;

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        std::string s(line);
        trimInPlace(s);
        if (s.empty()) continue;

        if (s == TC_SECTION) {
            inTcSection = true;
            continue;
        }
        if (inTcSection && !s.empty() && s.front() == '[' && s != TC_SECTION)
            inTcSection = false;

        if (inTcSection) {
            if (startsWith(s, HANDHELD_KEY)) {
                size_t eq = s.find('=');
                if (eq != std::string::npos) {
                    handheldStr = s.substr(eq + 1);
                    trimInPlace(handheldStr);
                }
            } else if (startsWith(s, DOCKED_KEY)) {
                size_t eq = s.find('=');
                if (eq != std::string::npos) {
                    dockedStr = s.substr(eq + 1);
                    trimInPlace(dockedStr);
                }
            }
        }
    }
    fclose(f);

    handheldTable = parseTableString(handheldStr.empty() ? getDefaultHandheldTable() : handheldStr);
    if (handheldTable.empty()) handheldTable = parseTableString(getDefaultHandheldTable());

    dockedTable = parseTableString(dockedStr.empty() ? getDefaultDockedTable() : dockedStr);
    if (dockedTable.empty()) dockedTable = parseTableString(getDefaultDockedTable());
}

void FanSettingsFrame::saveToIni() {
    std::vector<std::string> lines;
    FILE* f = fopen(SYSTEM_SETTINGS_PATH, "r");
    if (f) {
        char line[1024];
        while (fgets(line, sizeof(line), f)) lines.emplace_back(line);
        fclose(f);
    }

    f = fopen(SYSTEM_SETTINGS_PATH, "w");
    if (!f) {
        brls::Application::notify("Failed to open system_settings.ini for writing");
        return;
    }

    bool tcSectionFound = false;
    bool inTcSection = false;
    bool handheldWritten = false, dockedWritten = false;

    auto writeMissing = [&]() {
        if (!handheldWritten) {
            fprintf(f, "%s = str!\"%s\"\n", HANDHELD_KEY, serializeTable(handheldTable).c_str());
            handheldWritten = true;
        }
        if (!dockedWritten) {
            fprintf(f, "%s = str!\"%s\"\n", DOCKED_KEY, serializeTable(dockedTable).c_str());
            dockedWritten = true;
        }
    };

    for (const auto& line : lines) {
        std::string trimmed = line;
        trimInPlace(trimmed);

        if (trimmed == TC_SECTION) {
            if (inTcSection) writeMissing();
            tcSectionFound = true;
            inTcSection = true;
            handheldWritten = dockedWritten = false;
            fprintf(f, "%s", line.c_str());
            continue;
        }

        if (inTcSection && !trimmed.empty() && trimmed.front() == '[' && trimmed != TC_SECTION) {
            writeMissing();
            inTcSection = false;
        }

        if (inTcSection && startsWith(trimmed, HANDHELD_KEY)) {
            if (!handheldWritten) {
                fprintf(f, "%s = str!\"%s\"\n", HANDHELD_KEY, serializeTable(handheldTable).c_str());
                handheldWritten = true;
            }
            continue;
        }
        if (inTcSection && startsWith(trimmed, DOCKED_KEY)) {
            if (!dockedWritten) {
                fprintf(f, "%s = str!\"%s\"\n", DOCKED_KEY, serializeTable(dockedTable).c_str());
                dockedWritten = true;
            }
            continue;
        }

        fprintf(f, "%s", line.c_str());
    }

    if (inTcSection) writeMissing();
    if (!tcSectionFound) {
        fprintf(f, "\n%s\n", TC_SECTION);
        fprintf(f, "%s = str!\"%s\"\n", HANDHELD_KEY, serializeTable(handheldTable).c_str());
        fprintf(f, "%s = str!\"%s\"\n", DOCKED_KEY, serializeTable(dockedTable).c_str());
    }

    fclose(f);
    brls::Application::notify("Fan settings saved. Reboot for changes to take effect.");
}

// --- Строка таблицы с двумя полями PWM ---
class FanTableRow : public brls::ListItem {
public:
    FanTableRow(const std::string& label, FanTableEntry* entry)
        : brls::ListItem(label), entry(entry), activeField(0) {
        updateValue();
    }

    void onFocusGained() override {
        brls::ListItem::onFocusGained();

        // Стрелки вверх/вниз переключают между строками (по умолчанию List делает это)
        // Стрелки влево/вправо переключают активное поле
        registerAction("Switch Field Left", brls::Key::DLEFT, [this]() { activeField = 0; updateValue(); return true; });
        registerAction("Switch Field Right", brls::Key::DRIGHT, [this]() { activeField = 1; updateValue(); return true; });

        // Увеличение/уменьшение значения активного поля
        registerAction("Increase", brls::Key::DUP, [this]() { changeValue(1); return true; });
        registerAction("Decrease", brls::Key::DDOWN, [this]() { changeValue(-1); return true; });
        registerAction("Increase10", brls::Key::R, [this]() { changeValue(10); return true; });
        registerAction("Decrease10", brls::Key::L, [this]() { changeValue(-10); return true; });

        // Прямой ввод через A
        registerAction("Edit", brls::Key::A, [this]() {
            SwkbdConfig kbd;
            char tmp[16] = {0};
            swkbdCreate(&kbd, 0);
            swkbdConfigMakePresetDefault(&kbd);
            swkbdConfigSetGuideText(&kbd, "Enter min,max PWM");
            swkbdConfigSetInitialText(&kbd, (std::to_string(entry->minPwm)+","+std::to_string(entry->maxPwm)).c_str());
            swkbdConfigSetType(&kbd, SwkbdType_NumPad);

            if (swkbdShow(&kbd, tmp, sizeof(tmp)) == 0) {
                std::string s(tmp);
                auto comma = s.find(',');
                if (comma != std::string::npos) {
                    try {
                        int minv = std::stoi(s.substr(0, comma));
                        int maxv = std::stoi(s.substr(comma+1));
                        setValues(minv, maxv);
                    } catch (...) {}
                }
            }
            swkbdClose(&kbd);
            return true;
        });
    }

private:
    FanTableEntry* entry;
    int activeField; // 0 = min, 1 = max

    void updateValue() {
        std::string display = (activeField == 0 ? "▶ " : "   ") + std::to_string(entry->minPwm) +
                              " / " +
                              (activeField == 1 ? "▶ " : "   ") + std::to_string(entry->maxPwm);
        setValue(display);
    }

    void setValues(int minv, int maxv) {
        if (minv > maxv) std::swap(minv, maxv);
        entry->minPwm = std::clamp(minv, 0, 255);
        entry->maxPwm = std::clamp(maxv, 0, 255);
        updateValue();
    }

    void changeValue(int delta) {
        if (activeField == 0) setValues(entry->minPwm + delta, entry->maxPwm);
        else setValues(entry->minPwm, entry->maxPwm + delta);
    }
};

// --- Основное окно ---
void FanSettingsFrame::buildUI() {
    auto* list = new brls::List();

    list->addView(new brls::Header("Handheld Mode"));
    for (auto& entry : handheldTable) {
        char label[64];
        snprintf(label, sizeof(label), "%d°C - %d°C", entry.minTemp / 1000, entry.maxTemp / 1000);
        list->addView(new FanTableRow(label, &entry));
    }

    list->addView(new brls::Header("Docked Mode"));
    for (auto& entry : dockedTable) {
        char label[64];
        snprintf(label, sizeof(label), "%d°C - %d°C", entry.minTemp / 1000, entry.maxTemp / 1000);
        list->addView(new FanTableRow(label, &entry));
    }

    auto* saveButton = new brls::Button(brls::ButtonStyle::BORDERLESS);
    saveButton->setLabel("Save");
    saveButton->getClickEvent()->subscribe([this](brls::View*) {
        saveToIni();
        brls::Application::popView();
        return true;
    });
    list->addView(saveButton);

    this->setContentView(list);
}

FanSettingsFrame::FanSettingsFrame() {
    this->setTitle("Fan Curve Configuration");
    this->setIcon(new brls::MaterialIcon("\uE3E9"));
    loadFromIni();
    buildUI();
}

FanSettingsFrame::~FanSettingsFrame() {}

bool FanSettingsFrame::onCancel() {
    brls::Application::popView();
    return true;
}