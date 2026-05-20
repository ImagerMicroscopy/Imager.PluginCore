#include "ConfigManager.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include <toml++/toml.hpp>

class ConfigManager::Impl {
public:
    toml::table table;
};

ConfigManager::ConfigManager(const std::filesystem::path& configFilePath)
    : _configFilePath(configFilePath),
      _impl(std::make_unique<Impl>()) {

    if (std::filesystem::exists(_configFilePath)) {
        try {
            _impl->table = toml::parse_file(_configFilePath.string());
        } catch (const toml::parse_error& err) {
            std::cerr << "Parsing failed: " << err << "\n";
            // Handled as empty config, or you could throw an exception
        }
    }
}

ConfigManager::~ConfigManager() {
}

void ConfigManager::save() {
    if (_configFilePath.empty()) return;

    std::ofstream outStream(_configFilePath);
    
    // toml++ knows how to serialize itself to std::ostream naturally
    outStream << _impl->table;
}

void ConfigManager::storeStringSetting(const ConfigPath& configPath, const std::string& value) {
    const auto& keys = configPath.getKeys();
    if (keys.empty()) return;

    toml::table* currentTable = &(_impl->table);

    // Navigate to the correct nested table, creating any missing sections on the fly
    for (size_t i = 0; i < keys.size() - 1; ++i) {
        const auto& key = keys[i];
        
        // If the table doesn't exist at this key, insert a new one
        if (!currentTable->contains(key) || !currentTable->at(key).is_table()) {
            currentTable->insert_or_assign(key, toml::table{});
        }
        
        currentTable = currentTable->at(key).as_table();
    }

    // Now insert the value at the final key location
    const std::string& finalKey = keys.back();
    
    // Insert or replace the value
    currentTable->insert_or_assign(finalKey, value);

    save();
}

void ConfigManager::storeBoolSetting(const ConfigPath& configPath, bool value) {
    std::string setting;
    if (value) {
        setting = "True";
    } else {
        setting = "False";
    }
    storeStringSetting(configPath, setting);
}

void ConfigManager::storeIntSetting(const ConfigPath& configPath, int value) {
    storeStringSetting(configPath, std::to_string(value));
}

void ConfigManager::storeDoubleSetting(const ConfigPath& configPath, double value) {
    storeStringSetting(configPath, std::to_string(value));
}

void ConfigManager::storePathSetting(const ConfigPath& configPath, const std::filesystem::path& value) {
    storeStringSetting(configPath, value.string());
}

ConfigManager::ConfigSetting<std::string> ConfigManager::getStringSettingOrDefault(const ConfigPath& configPath, const std::string& defaultValue) {
    const auto& keys = configPath.getKeys();
    if (keys.empty()) {
        return {defaultValue, false};
    }

    toml::table* root = &(_impl->table);
    toml::node* currentNode = root;

    for (const auto& key : keys) {
        if (!currentNode || !currentNode->is_table()) {
            currentNode = nullptr;
            break;
        }
        currentNode = currentNode->as_table()->get(key);
    }

    if (currentNode) {
        if (currentNode->is_string()) {
            return {currentNode->as_string()->get(), true};
        } 
        
        // Attempt to extract the value as a std::string if it's not directly a string (e.g. conversion)
        if (auto val = currentNode->value<std::string>()) {
            return {*val, true};
        }
    }

    // The key was not found or was of an incompatible type.
    // Store the default value so it gets saved.
    storeStringSetting(configPath, defaultValue);

    return {defaultValue, false};
}

ConfigManager::ConfigSetting<bool> ConfigManager::getBoolSettingOrDefault(const ConfigPath& configPath, bool defaultValue) {
    auto stringSetting = getStringSettingOrDefault(configPath, defaultValue ? "True" : "False");
    if (!stringSetting.wasFoundInConfig) {
        return {defaultValue, false};
    }

    std::string val = stringSetting.value;
    // convert to lowercase for easier comparison
    std::transform(val.begin(), val.end(), val.begin(), [](unsigned char c) { return std::tolower(c); });

    if (val == "true" || val == "1") {
        return {true, true};
    } else if (val == "false" || val == "0") {
        return {false, true};
    } else {
        std::string pathStr = configPath.getKeys().empty() ? "unknown" : configPath.getKeys().back();
        throw std::invalid_argument(std::format("Invalid boolean value '{}' found for config path key '{}'. Only 'True', or 'False' are allowed.", stringSetting.value, pathStr));
    }
}

ConfigManager::ConfigSetting<int> ConfigManager::getIntSettingOrDefault(const ConfigPath& configPath, int defaultValue) {
    auto stringSetting = getStringSettingOrDefault(configPath, std::to_string(defaultValue));
    if (!stringSetting.wasFoundInConfig) {
        return {defaultValue, false};
    }

    try {
        int parsedValue = std::stoi(stringSetting.value);
        return {parsedValue, true};
    } catch (const std::invalid_argument&) {
        std::string pathStr = configPath.getKeys().empty() ? "unknown" : configPath.getKeys().back();
        throw std::invalid_argument(std::format("Expected integer number for config path key '{}', but found '{}'", pathStr, stringSetting.value));
    } catch (const std::out_of_range&) {
        std::string pathStr = configPath.getKeys().empty() ? "unknown" : configPath.getKeys().back();
        throw std::out_of_range(std::format("Value '{}' is out of range for integer number at config path key '{}'", stringSetting.value, pathStr));
    }
}

ConfigManager::ConfigSetting<double> ConfigManager::getDoubleSettingOrDefault(const ConfigPath& configPath, double defaultValue) {
    auto stringSetting = getStringSettingOrDefault(configPath, std::to_string(defaultValue));
    if (!stringSetting.wasFoundInConfig) {
        return {defaultValue, false};
    }

    try {
        double parsedValue = std::stod(stringSetting.value);
        return {parsedValue, true};
    } catch (const std::invalid_argument&) {
        std::string pathStr = configPath.getKeys().empty() ? "unknown" : configPath.getKeys().back();
        throw std::invalid_argument(std::format("Expected number for config path key '{}', but found '{}'", pathStr, stringSetting.value));
    } catch (const std::out_of_range&) {
        std::string pathStr = configPath.getKeys().empty() ? "unknown" : configPath.getKeys().back();
        throw std::out_of_range(std::format("Value '{}' is out of range for number at config path key '{}'", stringSetting.value, pathStr));
    }
}

ConfigManager::ConfigSetting<std::filesystem::path> ConfigManager::getPathSettingOrDefault(const ConfigPath& configPath, const std::filesystem::path& defaultValue) {
    auto stringSetting = getStringSettingOrDefault(configPath, defaultValue.string());
    if (!stringSetting.wasFoundInConfig) {
        return {defaultValue, false};
    }

    return {std::filesystem::path(stringSetting.value), true};
}
