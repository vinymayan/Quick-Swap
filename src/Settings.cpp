#include "Settings.h"

#include "Manager.h"
#include "SKSEMCP/SKSEMenuFramework.hpp"
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace ImGui = ImGuiMCP;

namespace
{
    constexpr const char* MOD_DIR = "Data/Viny Mods/Quick Swap";
    constexpr const char* SETTINGS_PATH = "Data/Viny Mods/Quick Swap/Settings.json";
    constexpr const char* LANG_PATH = "Data/Viny Mods/Quick Swap/Language.json";

    std::map<std::string, std::string> g_language;

    std::string ReadTextFile(const char* a_path)
    {
        std::ifstream file(a_path, std::ios::binary);
        if (!file.is_open()) {
            return {};
        }

        std::stringstream buffer;
        buffer << file.rdbuf();

        auto text = buffer.str();
        if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
            static_cast<unsigned char>(text[1]) == 0xBB &&
            static_cast<unsigned char>(text[2]) == 0xBF) {
            text.erase(0, 3);
        }

        return text;
    }

    void FlattenLanguageNode(const rapidjson::Value& a_value, const std::string& a_prefix)
    {
        if (!a_value.IsObject()) {
            return;
        }

        for (auto itr = a_value.MemberBegin(); itr != a_value.MemberEnd(); ++itr) {
            const std::string key = a_prefix.empty() ? itr->name.GetString() : a_prefix + "." + itr->name.GetString();
            if (itr->value.IsString()) {
                g_language[key] = itr->value.GetString();
            } else if (itr->value.IsObject()) {
                FlattenLanguageNode(itr->value, key);
            }
        }
    }

    bool ReadBool(const rapidjson::Value& a_doc, const char* a_key, bool a_default)
    {
        return a_doc.HasMember(a_key) && a_doc[a_key].IsBool() ? a_doc[a_key].GetBool() : a_default;
    }

    RE::FormID ReadFormID(const rapidjson::Value& a_doc, const char* a_key)
    {
        if (!a_doc.HasMember(a_key)) {
            return 0;
        }

        const auto& value = a_doc[a_key];
        if (value.IsString()) {
            try {
                return FormUtil::FormIDFromString(value.GetString());
            } catch (...) {
                SKSE::log::warn("[QuickSwap] Invalid form string in setting '{}'", a_key);
                return 0;
            }
        }

        if (value.IsUint()) {
            return value.GetUint();
        }

        return 0;
    }

    std::string FormIDToString(RE::FormID a_formID)
    {
        if (a_formID == 0) {
            return "";
        }

        if (auto* form = RE::TESForm::LookupByID(a_formID)) {
            return FormUtil::NormalizeFormID(form);
        }

        return std::format("{:X}", a_formID);
    }

    void AddStringMember(
        rapidjson::Document& a_doc,
        rapidjson::Document::AllocatorType& a_allocator,
        const char* a_key,
        const std::string& a_value)
    {
        rapidjson::Value key;
        key.SetString(a_key, a_allocator);

        rapidjson::Value value;
        value.SetString(a_value.c_str(), static_cast<rapidjson::SizeType>(a_value.size()), a_allocator);
        a_doc.AddMember(key, value, a_allocator);
    }

    bool PlayerHasPerk(RE::Actor* a_actor, RE::FormID a_perkID)
    {
        if (a_perkID == 0) {
            return true;
        }

        auto* perk = RE::TESForm::LookupByID<RE::BGSPerk>(a_perkID);
        return perk && a_actor && a_actor->HasPerk(perk);
    }

    bool DrawPerkDropdown(const char* a_label, RE::FormID& a_currentPerk)
    {
        const auto& perks = Manager::GetSingleton()->GetList("Perk");
        bool changed = false;
        int currentIndex = 0;

        std::vector<const char*> items;
        items.reserve(perks.size() + 1);
        items.push_back(Settings::GetLoc("common.none", "None"));

        for (std::size_t i = 0; i < perks.size(); ++i) {
            items.push_back(perks[i].cachedDisplayName.c_str());
            if (perks[i].formID == a_currentPerk) {
                currentIndex = static_cast<int>(i + 1);
            }
        }

        if (ImGui::Combo(a_label, &currentIndex, items.data(), static_cast<int>(items.size()))) {
            a_currentPerk = currentIndex == 0 ? 0 : perks[static_cast<std::size_t>(currentIndex - 1)].formID;
            changed = true;
        }

        return changed;
    }

    void EnsureMenuListsPopulated()
    {
        auto* manager = Manager::GetSingleton();
        if (!manager->IsPopulated()) {
            manager->PopulateAllLists();
        }
    }
}

namespace Settings
{
    void LoadSettings()
    {
        auto json = ReadTextFile(SETTINGS_PATH);
        if (json.empty()) {
            SaveSettings();
            return;
        }

        rapidjson::Document doc;
        doc.Parse(json.c_str());
        if (doc.HasParseError() || !doc.IsObject()) {
            SKSE::log::warn("[QuickSwap] Failed to parse settings. Using defaults.");
            return;
        }

        EnableQuickSwap = ReadBool(doc, "enableQuickSwap", EnableQuickSwap);
        EnableNonCancelAttack = ReadBool(doc, "enableNonCancelAttack", EnableNonCancelAttack);
        QuickSwapPerk = ReadFormID(doc, "quickSwapPerk");
        NonCancelAttackPerk = ReadFormID(doc, "nonCancelAttackPerk");
    }

    void SaveSettings()
    {
        std::filesystem::create_directories(MOD_DIR);

        rapidjson::Document doc;
        doc.SetObject();
        auto& allocator = doc.GetAllocator();

        doc.AddMember("enableQuickSwap", EnableQuickSwap, allocator);
        doc.AddMember("enableNonCancelAttack", EnableNonCancelAttack, allocator);
        AddStringMember(doc, allocator, "quickSwapPerk", FormIDToString(QuickSwapPerk));
        AddStringMember(doc, allocator, "nonCancelAttackPerk", FormIDToString(NonCancelAttackPerk));

        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);

        std::ofstream file(SETTINGS_PATH, std::ios::binary | std::ios::trunc);
        if (file.is_open()) {
            file << buffer.GetString();
        }
    }

    void LoadLanguage()
    {
        g_language.clear();

        auto json = ReadTextFile(LANG_PATH);
        if (json.empty()) {
            return;
        }

        rapidjson::Document doc;
        doc.Parse(json.c_str());
        if (doc.HasParseError() || !doc.IsObject()) {
            SKSE::log::warn("[QuickSwap] Failed to parse language file. Using fallback text.");
            return;
        }

        FlattenLanguageNode(doc, {});
    }

    const char* GetLoc(const char* a_key, const char* a_fallback)
    {
        const auto it = g_language.find(a_key);
        if (it != g_language.end()) {
            return it->second.c_str();
        }

        return a_fallback;
    }

    bool IsQuickSwapAllowed(RE::Actor* a_actor)
    {
        return EnableQuickSwap && PlayerHasPerk(a_actor, QuickSwapPerk);
    }

    bool IsNonCancelAttackAllowed(RE::Actor* a_actor)
    {
        return EnableNonCancelAttack && PlayerHasPerk(a_actor, NonCancelAttackPerk);
    }
}

namespace QuickSwapMenu
{
    void Render()
    {
        EnsureMenuListsPopulated();

        bool changed = false;

        ImGui::Text("%s", Settings::GetLoc("menu.quick_swap_header", "Quick Swap"));
        changed |= ImGui::Checkbox(Settings::GetLoc("menu.enable_quick_swap", "Enable Quick Swap"), &Settings::EnableQuickSwap);
        changed |= DrawPerkDropdown(
            Settings::GetLoc("menu.quick_swap_perk", "Quick Swap locked behind perk"),
            Settings::QuickSwapPerk);

        ImGui::Separator();
        ImGui::Text("%s", Settings::GetLoc("menu.non_cancel_attack_header", "Non Cancel Attack"));
        changed |= ImGui::Checkbox(
            Settings::GetLoc("menu.enable_non_cancel_attack", "Enable Non Cancel Attack"),
            &Settings::EnableNonCancelAttack);
        changed |= DrawPerkDropdown(
            Settings::GetLoc("menu.non_cancel_attack_perk", "Non Cancel Attack locked behind perk"),
            Settings::NonCancelAttackPerk);

        if (changed) {
            Settings::SaveSettings();
        }
    }

    void Register()
    {
        Settings::LoadLanguage();
        Settings::LoadSettings();

        if (SKSEMenuFramework::IsInstalled()) {
            SKSEMenuFramework::SetSection("Quick Swap");
            SKSEMenuFramework::AddSectionItem("Settings", Render);
        }
    }
}
