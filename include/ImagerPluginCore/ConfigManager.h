#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <filesystem>
#include <string>
#include <vector>
#include <utility>

class ConfigPath {
public:
    // Allow implicit conversion from single strings
    ConfigPath(std::string key) : _keys{std::move(key)} {}
    ConfigPath(const char* key) : _keys{key} {}
    ConfigPath() = default;

    // Append operator
    ConfigPath& operator/=(const ConfigPath& rhs) {
        _keys.insert(_keys.end(), rhs._keys.begin(), rhs._keys.end());
        return *this;
    }

    const std::vector<std::string>& getKeys() const { return _keys; }

private:
    std::vector<std::string> _keys;
};

// Global operator/ allows joining paths safely
inline ConfigPath operator/(ConfigPath lhs, const ConfigPath& rhs) {
    lhs /= rhs;
    return lhs;
}

class ConfigManager {
public:
    template<typename T>
    class ConfigSetting {
        public:
            ConfigSetting(T value, bool wasFoundInConfig) : value(std::move(value)), wasFoundInConfig(wasFoundInConfig) {}
            T value;
            bool wasFoundInConfig;
    };

    ConfigManager(const std::filesystem::path& configFilePath);
    ~ConfigManager(); // Will save the config on destruction, or manually
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    void storeStringSetting(const ConfigPath& configPath, const std::string& value);
    void storeBoolSetting(const ConfigPath& configPath, bool value);
    void storeIntSetting(const ConfigPath& configPath, int value);
    void storeDoubleSetting(const ConfigPath& configPath, double value);
    void storePathSetting(const ConfigPath& configPath, const std::filesystem::path& value);

    ConfigSetting<std::string> getStringSettingOrDefault(const ConfigPath& configPath, const std::string& defaultValue);
    ConfigSetting<bool> getBoolSettingOrDefault(const ConfigPath& configPath, bool defaultValue);
    ConfigSetting<int> getIntSettingOrDefault(const ConfigPath& configPath, int defaultValue);
    ConfigSetting<double> getDoubleSettingOrDefault(const ConfigPath& configPath, double defaultValue);
    ConfigSetting<std::filesystem::path> getPathSettingOrDefault(const ConfigPath& configPath, const std::filesystem::path& defaultValue);

    void save(); // Trigger a manual save

private:
    std::filesystem::path _configFilePath;

    class Impl;    // forward declaration of a custom implementation so we don't need to include toml headers.
    std::unique_ptr<Impl> _impl;
};

#endif // CONFIGMANAGER_H
