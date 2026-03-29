#include "fan_settings_frame.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
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
        auto isSpace = [](unsigned char c) { return std::isspace(c); };

        auto start = std::find_if_not(s.begin(), s.end(), isSpace);
        auto end   = std::find_if_not(s.rbegin(), s.rend(), isSpace).base();

        s = (start < end) ? std::string(start, end) : "";
    }

    static inline bool startsWith(const std::string& s, const char* prefix) {
        return s.rfind(prefix, 0) == 0;
    }

    static inline int pwmToPercent(int pwm) {
        pwm = std::clamp(pwm, 0, 255);
        return (pwm * 100 + 127) / 255;
    }

    static inline int percentToPwm(int percent) {
        percent = std::clamp(percent, 0, 100);
        return (percent * 255 + 50) / 100;
    }
}

// 10 фиксированных точек: 35..80 по 5 градусов
static std::vector<FanTableEntry> buildFixedTable(const std::vector<FanTableEntry>& old) {
    std::vector<FanTableEntry> result;
    result.reserve(10);

    int temp = 35000;
    int prevPwm = 0;

    for (int i = 0; i < 10; ++i, temp += 5000) {
        int currentPwm = (i < static_cast<int>(old.size())) ? old[i].maxPwm : prevPwm;

        FanTableEntry e{};
        e.minTemp = temp;
        e.maxTemp = temp;

        if (i == 0) {
            e.minPwm = currentPwm;
            e.maxPwm = currentPwm;
        } else {
            e.minPwm = prevPwm;
            e.maxPwm = currentPwm;
        }

        prevPwm = currentPwm;
        result.push_back(e);
    }

    return result;
}

// --- Парсинг ---
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
                try {
                    values.push_back(std::stoi(token));
                } catch (...) {
                    continue;
                }
            }
        }
        if (values.size() == 4)
            result.push_back({values[0], values[1], values[2], values[3]});
    }

    return result;
}

// --- Сериализация ---
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

// --- Дефолты ---
std::string FanSettingsFrame::getDefaultHandheldTable() {
    return "[[35000,35000,10,10],[40000,40000,10,30],[45000,45000,30,60],[50000,50000,60,90],[55000,55000,90,120],[60000,60000,120,150],[65000,65000,150,180],[70000,70000,180,200],[75000,75000,200,220],[80000,80000,220,255]]";
}

std::string FanSettingsFrame::getDefaultDockedTable() {
    return getDefaultHandheldTable();
}

// --- Загрузка из INI ---
void FanSettingsFrame::loadFromIni() {
    FILE* f = fopen(SYSTEM_SETTINGS_PATH, "r");
    if (!f) {
        handheldTable = parseTableString(getDefaultHandheldTable());
        dockedTable   = parseTableString(getDefaultDockedTable());
    } else {
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
        dockedTable   = parseTableString(dockedStr.empty() ? getDefaultDockedTable() : dockedStr);
    }

    handheldTable = buildFixedTable(handheldTable);
    dockedTable   = buildFixedTable(dockedTable);
}

// --- Сохранение в INI ---
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
    bool handheldWritten = false;
    bool dockedWritten = false;

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

// --- Строка таблицы с одним PWM в процентах ---
class FanTableRow : public brls::ListItem {
public:
    FanTableRow(const std::string& label, FanTableEntry* tableEntry, FanTableEntry* nextEntry = nullptr)
        : brls::ListItem(label), entry(tableEntry), next(nextEntry), editing(false) {
        updateValue();
    }

    void onFocusGained() override {
        brls::ListItem::onFocusGained();

        registerAction("+", brls::Key::DUP, [this]() {
            if (!editing) return false;
            changeValue(1);
            return true;
        });

        registerAction("-", brls::Key::DDOWN, [this]() {
            if (!editing) return false;
            changeValue(-1);
            return true;
        });

        registerAction("+5", brls::Key::R, [this]() {
            if (!editing) return false;
            changeValue(5);
            return true;
        });

        registerAction("-5", brls::Key::L, [this]() {
            if (!editing) return false;
            changeValue(-5);
            return true;
        });

        registerAction("Edit", brls::Key::A, [this]() {
            editing = true;
            updateValue();
            return true;
        });

        registerAction("CancelEdit", brls::Key::B, [this]() {
            if (editing) {
                editing = false;
                updateValue();
                return true;
            }
            return false;
        });
    }

private:
    FanTableEntry* entry;
    FanTableEntry* next;
    bool editing;

    void updateValue() {
        int percent = pwmToPercent(entry->maxPwm);
        std::string display = std::to_string(percent) + "%";
        if (editing) display = "* " + display;
        setValue(display);
    }

    void setPercentValue(int percent) {
        percent = std::clamp(percent, 0, 100);
        int pwm = percentToPwm(percent);

        int lowerBound = entry->minPwm;
        int upperBound = next ? next->maxPwm : 255;

        if (lowerBound > upperBound) {
            upperBound = lowerBound;
        }

        pwm = std::clamp(pwm, lowerBound, upperBound);

        entry->maxPwm = pwm;
        if (next) {
            next->minPwm = pwm;
        }

        updateValue();
    }

    void changeValue(int deltaPercent) {
        int currentPercent = pwmToPercent(entry->maxPwm);
        setPercentValue(currentPercent + deltaPercent);
    }
};

// --- Основное окно ---
void FanSettingsFrame::buildUI() {
    list = new brls::List();

    list->addView(new brls::Header("Handheld Mode"));
    for (size_t i = 0; i < handheldTable.size(); ++i) {
        char label[64];
        snprintf(label, sizeof(label), "%d°C", handheldTable[i].minTemp / 1000);
        FanTableEntry* next = (i + 1 < handheldTable.size()) ? &handheldTable[i + 1] : nullptr;
        list->addView(new FanTableRow(label, &handheldTable[i], next));
    }

    list->addView(new brls::Header("Docked Mode"));
    for (size_t i = 0; i < dockedTable.size(); ++i) {
        char label[64];
        snprintf(label, sizeof(label), "%d°C", dockedTable[i].minTemp / 1000);
        FanTableEntry* next = (i + 1 < dockedTable.size()) ? &dockedTable[i + 1] : nullptr;
        list->addView(new FanTableRow(label, &dockedTable[i], next));
    }

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