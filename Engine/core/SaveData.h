#pragma once
// セーブデータ（「名前=値」の文字だけのファイルで、ハイスコアや設定を保存する）。ヘッダーだけで使える
//
//   SaveData save;
//   save.Load("save/save.txt");                  // ファイルがなくても大丈夫（false が返るだけ）
//   int best = save.GetInt("highScore", 0);      // 無いときは 0（2つ目の引数）が返る
//   save.SetInt("highScore", 1200);
//   save.SetFloat("volume", 0.8f);
//   save.SetString("playerName", "Taro");
//   save.Save("save/save.txt");                  // フォルダがなければ作る
//
//   ※名前に「=」や改行は使えない。値に改行は使えない
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <string>

class SaveData
{
public:
    // 読み込む。ファイルがない・開けないときは false（中身は変わらない）
    bool Load(const std::string& path)
    {
        std::ifstream file(path);
        if (!file) {
            return false;
        }
        values_.clear();
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back(); // Windowsの改行に対応
            }
            const size_t eq = line.find('=');
            if (eq == std::string::npos || eq == 0) {
                continue; // 「名前=値」の形でない行は読み飛ばす
            }
            values_[line.substr(0, eq)] = line.substr(eq + 1);
        }
        return true;
    }

    // 書き出す。フォルダがなければ作る。失敗したら false
    bool Save(const std::string& path) const
    {
        const std::filesystem::path p(path);
        if (p.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(p.parent_path(), ec);
        }
        std::ofstream file(path, std::ios::trunc);
        if (!file) {
            return false;
        }
        for (const auto& [key, value] : values_) {
            file << key << '=' << value << '\n';
        }
        return static_cast<bool>(file);
    }

    bool Has(const std::string& key) const { return values_.find(key) != values_.end(); }
    void Remove(const std::string& key) { values_.erase(key); }
    void Clear() { values_.clear(); }

    void SetInt(const std::string& key, int value) { values_[key] = std::to_string(value); }
    void SetFloat(const std::string& key, float value) { values_[key] = std::format("{}", value); }
    void SetBool(const std::string& key, bool value) { values_[key] = value ? "1" : "0"; }
    void SetString(const std::string& key, const std::string& value) { values_[key] = value; }

    int GetInt(const std::string& key, int defaultValue = 0) const
    {
        const auto it = values_.find(key);
        if (it == values_.end()) { return defaultValue; }
        try { return std::stoi(it->second); }
        catch (...) { return defaultValue; }
    }

    float GetFloat(const std::string& key, float defaultValue = 0.0f) const
    {
        const auto it = values_.find(key);
        if (it == values_.end()) { return defaultValue; }
        try { return std::stof(it->second); }
        catch (...) { return defaultValue; }
    }

    bool GetBool(const std::string& key, bool defaultValue = false) const
    {
        const auto it = values_.find(key);
        if (it == values_.end()) { return defaultValue; }
        return it->second == "1" || it->second == "true";
    }

    std::string GetString(const std::string& key, const std::string& defaultValue = "") const
    {
        const auto it = values_.find(key);
        return it == values_.end() ? defaultValue : it->second;
    }

private:
    std::map<std::string, std::string> values_;
};
