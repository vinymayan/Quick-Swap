#include "logger.h"
#include "Hooks.h"
#include "Manager.h"
#include "Settings.h"

void OnMessage(SKSE::MessagingInterface::Message* message)
{
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        Manager::GetSingleton()->PopulateAllLists();
        QuickSwapMenu::Register();
    }
    if (message->type == SKSE::MessagingInterface::kNewGame || message->type == SKSE::MessagingInterface::kPostLoadGame) {
        // Post-load
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
