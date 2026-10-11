#include "ConfigManager.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <format>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>

#include <toml++/toml.hpp>

class ConfigManager::Impl {
public:
    toml::table table;
};

namespace {

    // Navigates to the table addressed by all but the last key of configPath, creating
    // any missing tables on the fly, and stores the value at the final key.
    template<typename T>
    void storeSetting(toml::table& rootTable, const ConfigPath& configPath, T&& value) {
        const auto& keys = configPath.getKeys();
        if (keys.empty()) return;

        toml::table* currentTable = &rootTable;

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
        currentTable->insert_or_assign(keys.back(), std::forward<T>(value));
    }

    // Returns the node addressed by configPath, or nullptr if any key along the way is missing.
    toml::node* findNode(toml::table& rootTable, const ConfigPath& configPath) {
        const auto& keys = configPath.getKeys();
        if (keys.empty()) return nullptr;

        toml::node* currentNode = &rootTable;
        for (const auto& key : keys) {
            if (!currentNode->is_table()) return nullptr;
            currentNode = currentNode->as_table()->get(key);
            if (currentNode == nullptr) return nullptr;
        }
        return currentNode;
    }

    std::string lastKeyString(const ConfigPath& configPath) {
        const auto& keys = configPath.getKeys();
        return keys.empty() ? "unknown" : keys.back();
    }

}

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
    storeSetting(_impl->table, configPath, value);
    save();
}

void ConfigManager::storeBoolSetting(const ConfigPath& configPath, bool value) {
    storeSetting(_impl->table, configPath, value);
    save();
}

void ConfigManager::storeIntSetting(const ConfigPath& configPath, int value) {
    storeSetting(_impl->table, configPath, static_cast<std::int64_t>(value));
    save();
}

void ConfigManager::storeDoubleSetting(const ConfigPath& configPath, double value) {
    storeSetting(_impl->table, configPath, value);
    save();
}

void ConfigManager::storePathSetting(const ConfigPath& configPath, const std::filesystem::path& value) {
    storeSetting(_impl->table, configPath, value.string());
    save();
}

ConfigManager::ConfigSetting<std::string> ConfigManager::getStringSettingOrDefault(const ConfigPath& configPath, const std::string& defaultValue) {
    toml::node* node = findNode(_impl->table, configPath);
    if (node != nullptr) {
        if (node->is_string()) {
            return {node->as_string()->get(), true};
        }
        throw std::invalid_argument(std::format("Expected string value for config path key '{}', but found a value of a different type", lastKeyString(configPath)));
    }

    // The key was not found. Store the default value so it gets saved.
    storeStringSetting(configPath, defaultValue);

    return {defaultValue, false};
}

ConfigManager::ConfigSetting<bool> ConfigManager::getBoolSettingOrDefault(const ConfigPath& configPath, bool defaultValue) {
    toml::node* node = findNode(_impl->table, configPath);
    if (node != nullptr) {
        if (node->is_boolean()) {
            return {*node->value<bool>(), true};
        }
        if (node->is_string()) {
            // Fallback for string values, e.g. from config files written by older versions.
            const std::string str = node->as_string()->get();
            std::string val = str;
            // convert to lowercase for easier comparison
            std::transform(val.begin(), val.end(), val.begin(), [](unsigned char c) { return std::tolower(c); });

            if (val == "true" || val == "1") {
                return {true, true};
            } else if (val == "false" || val == "0") {
                return {false, true};
            }
            throw std::invalid_argument(std::format("Invalid boolean value '{}' found for config path key '{}'. Use a TOML boolean (true/false) or a string 'True'/'False'.", str, lastKeyString(configPath)));
        }
        throw std::invalid_argument(std::format("Expected boolean value for config path key '{}', but found a value of a different type", lastKeyString(configPath)));
    }

    // The key was not found. Store the default value so it gets saved.
    storeBoolSetting(configPath, defaultValue);

    return {defaultValue, false};
}

ConfigManager::ConfigSetting<int> ConfigManager::getIntSettingOrDefault(const ConfigPath& configPath, int defaultValue) {
    toml::node* node = findNode(_impl->table, configPath);
    if (node != nullptr) {
        if (node->is_integer()) {
            // value<int>() returns an empty optional if the TOML integer does not fit into an int.
            if (auto val = node->value<int>()) {
                return {*val, true};
            }
            throw std::out_of_range(std::format("Value '{}' is out of range for integer number at config path key '{}'", *node->value<std::int64_t>(), lastKeyString(configPath)));
        }
        if (node->is_string()) {
            // Fallback for string values, e.g. from config files written by older versions.
            const std::string str = node->as_string()->get();
            try {
                return {std::stoi(str), true};
            } catch (const std::invalid_argument&) {
                throw std::invalid_argument(std::format("Expected integer number for config path key '{}', but found '{}'", lastKeyString(configPath), str));
            } catch (const std::out_of_range&) {
                throw std::out_of_range(std::format("Value '{}' is out of range for integer number at config path key '{}'", str, lastKeyString(configPath)));
            }
        }
        throw std::invalid_argument(std::format("Expected integer number for config path key '{}', but found a value of a different type", lastKeyString(configPath)));
    }

    // The key was not found. Store the default value so it gets saved.
    storeIntSetting(configPath, defaultValue);

    return {defaultValue, false};
}

ConfigManager::ConfigSetting<double> ConfigManager::getDoubleSettingOrDefault(const ConfigPath& configPath, double defaultValue) {
    toml::node* node = findNode(_impl->table, configPath);
    if (node != nullptr) {
        if (node->is_floating_point()) {
            return {*node->value<double>(), true};
        }
        if (node->is_integer()) {
            // Accept TOML integers as well, so that e.g. "exposure = 1" is a valid double.
            // value<double>() returns an empty optional if the integer is too large to represent exactly.
            if (auto val = node->value<double>()) {
                return {*val, true};
            }
            throw std::out_of_range(std::format("Value '{}' is out of range for number at config path key '{}'", *node->value<std::int64_t>(), lastKeyString(configPath)));
        }
        if (node->is_string()) {
            // Fallback for string values, e.g. from config files written by older versions.
            const std::string str = node->as_string()->get();
            try {
                return {std::stod(str), true};
            } catch (const std::invalid_argument&) {
                throw std::invalid_argument(std::format("Expected number for config path key '{}', but found '{}'", lastKeyString(configPath), str));
            } catch (const std::out_of_range&) {
                throw std::out_of_range(std::format("Value '{}' is out of range for number at config path key '{}'", str, lastKeyString(configPath)));
            }
        }
        throw std::invalid_argument(std::format("Expected number for config path key '{}', but found a value of a different type", lastKeyString(configPath)));
    }

    // The key was not found. Store the default value so it gets saved.
    storeDoubleSetting(configPath, defaultValue);

    return {defaultValue, false};
}

ConfigManager::ConfigSetting<std::filesystem::path> ConfigManager::getPathSettingOrDefault(const ConfigPath& configPath, const std::filesystem::path& defaultValue) {
    auto stringSetting = getStringSettingOrDefault(configPath, defaultValue.string());
    if (!stringSetting.wasFoundInConfig) {
        return {defaultValue, false};
    }

    return {std::filesystem::path(stringSetting.value), true};
}
