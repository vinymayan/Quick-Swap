#include "logger.h"
#include "Events.h"
#include "Hooks.h"
#include "Manager.h"
#include "Settings.h"

void OnMessage(SKSE::MessagingInterface::Message* message)
{
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        PlayerMagicEquipTracker::Install();
        QuickSwapMenu::Register();
    }
    if (message->type == SKSE::MessagingInterface::kNewGame || message->type == SKSE::MessagingInterface::kPostLoadGame) {
        PlayerMagicEquipTracker::Refresh();
        Manager::GetSingleton()->PopulateAllLists();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SetupLog();
    logger::info("Plugin loaded");
    SKSE::Init(skse);
    NotifyAnimationGraphHook::Install();
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    return true;
}
