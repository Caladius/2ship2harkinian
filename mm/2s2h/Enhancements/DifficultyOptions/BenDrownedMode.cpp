#include <libultraship/bridge.h>
#include "Enhancements/GameInteractor/GameInteractor.h"

#include "global.h"
#include "overlays/actors/ovl_En_Torch2/z_en_torch2.h"

#include "SaveManager/SaveManager.h"
#include <fstream>
#include <filesystem>
#include "z64save.h"
#include "overlays/gamestates/ovl_file_choose/z_file_select.h"
#include "Enhancements/Saving/SavingEnhancements.h"

static uint32_t shellSpawn = 0;
static uint32_t soundSpawn = 5000;
static uint32_t screamEffect = 0;
static uint32_t spawnCount = 0;
static uint32_t resetTimer = 0;
bool benClean = false;
bool benInit = false;
bool resetTrigger = false;

void BenCleanUp() {
    if (benClean == false) {
        Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_ITEMACTION].first;

        while (actor != NULL) {
            if (actor->id == ACTOR_EN_TORCH2) {
                Actor_Kill(actor);
            }

            actor = actor->next;
        }
        spawnCount = 0;
        benClean = true;
        benInit = false;
    }
}

void ShellSelector() {
    Player* player = GET_PLAYER(gPlayState);
    int randShell = rand() % 4;

    if (spawnCount < 4) {
        Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_TORCH2, player->actor.world.pos.x,
                    player->actor.world.pos.y, player->actor.world.pos.z, player->actor.world.rot.x,
                    player->actor.world.rot.y, player->actor.world.rot.z, randShell);
        spawnCount++;
    } else {
        Actor* actor = &GET_PLAYER(gPlayState)->actor;
        Actor* nearbyShell = Actor_FindNearby(gPlayState, actor, ACTOR_EN_TORCH2, ACTORCAT_ITEMACTION, 15000);
        if (!nearbyShell) {
            spawnCount = 0;
        } else {
            Actor_Kill(nearbyShell);
            spawnCount--;
        }
        ShellSelector();
    }
}

void SoundSelector() {
    int randSound = rand() % 4;
    switch (randSound) {
        case 0:
            Audio_PlaySfx(NA_SE_EN_STAL09_SCREAM);
            CVarSetInteger("gEnhancements.Graphics.MotionBlur.Mode", 2);
            CVarSetInteger("gEnhancements.Graphics.MotionBlur.Strength", 200);
            screamEffect = 1000;
            break;
        case 1:
            Audio_PlaySfx(NA_SE_EN_STALKIDS_LAUGH);
            gSaveContext.save.saveInfo.playerData.health = 0x8;
            break;
        default:
            break;
    }
}

void SaveModifier() {
    const std::string appShortName = "2ship";
    const std::filesystem::path savesFolderPath(Ship::Context::GetPathRelativeToAppDirectory("saves", appShortName));
    static std::string fileNo = "file" + std::to_string(gSaveContext.fileNum + 1) + ".json";

    if (gSaveContext.fileNum == 0) {
        std::string fromFileNo = "file" + std::to_string(gSaveContext.fileNum + 1) + ".json";
        const std::filesystem::path fromFilePath = savesFolderPath / fromFileNo;
        std::string toFileNo = "file" + std::to_string(gSaveContext.fileNum + 2) + ".json";
        const std::filesystem::path toFilePath = savesFolderPath / toFileNo;
        if (std::filesystem::exists(toFilePath)) {
            std::filesystem::remove(toFilePath);
        }
        std::filesystem::copy(fromFilePath, toFilePath);
        std::filesystem::remove(fromFilePath);
    } else {
        std::string fromFileNo = "file" + std::to_string(gSaveContext.fileNum + 2) + ".json";
        const std::filesystem::path fromFilePath = savesFolderPath / fromFileNo;
        std::string toFileNo = "file" + std::to_string(gSaveContext.fileNum + 1) + ".json";
        const std::filesystem::path toFilePath = savesFolderPath / toFileNo;
        if (std::filesystem::exists(toFilePath)) {
            std::filesystem::remove(toFilePath);
        }
        std::filesystem::copy(fromFilePath, toFilePath);
        std::filesystem::remove(fromFilePath);
    }
}

void BenAutoSave() {
    // Create owl save
    gSaveContext.save.isOwlSave = true;
    gSaveContext.save.shipSaveInfo.pauseSaveEntrance = SavingEnhancements_GetSaveEntrance();
    Play_SaveCycleSceneFlags(&gPlayState->state);
    gSaveContext.save.saveInfo.playerData.savedSceneId = gPlayState->sceneId;
    func_8014546C(&gPlayState->sramCtx);
    Sram_SetFlashPagesOwlSave(&gPlayState->sramCtx, gFlashOwlSaveStartPages[gSaveContext.fileNum * 2],
                              gFlashOwlSaveNumPages[gSaveContext.fileNum * 2]);
    Sram_StartWriteToFlashOwlSave(&gPlayState->sramCtx);
    gSaveContext.save.isOwlSave = false;
    gSaveContext.save.shipSaveInfo.pauseSaveEntrance = -1;

    //SaveModifier();
}

void NameChanger(uint32_t event) {
    if (event == 1) {
        char nameValues[8] = { '\x10', '2', '2', '\'', '\x15', '8', '&', '.' };
        for (int i = 0; i <= 7; i++) {
            gSaveContext.save.saveInfo.playerData.playerName[i] = nameValues[i];
        }
        resetTimer = 100;
        resetTrigger = true;
    } else {
        char nameValues[8] = { '\v', '\xe', '\x17', '>', '>', '>', '>', '>' };
        for (int i = 0; i <= 7; i++) {
            gSaveContext.save.saveInfo.playerData.playerName[i] = nameValues[i];
        }
    }
    BenAutoSave();
}

void DrownedTimerManager() {
    if (shellSpawn > 0) {
        shellSpawn--;
    }
    if (soundSpawn > 0) {
        soundSpawn--;
    }
    if (screamEffect > 0) {
        screamEffect--;
    }
    if (screamEffect <= 0) {
        CVarSetInteger("gEnhancements.Graphics.MotionBlur.Mode", 0);
        CVarSetInteger("gEnhancements.Graphics.MotionBlur.Strength", 180);
    }
    if (resetTimer > 0) {
        resetTimer--;
    } else if (resetTimer <= 0 && resetTrigger == true){
        resetTrigger = false;
        std::reinterpret_pointer_cast<Ship::ConsoleWindow>(
            Ship::Context::GetInstance()->GetWindow()->GetGui()->GetGuiWindow("Console"))
            ->Dispatch("reset");
    }
}

void RegisterBenDrownedMode() {
	GameInteractor::Instance->RegisterGameHookForID<GameInteractor::OnActorInit>(ACTOR_PLAYER, [](Actor* outerActor) {
        
        static HOOK_ID playerUpdateHook = 0;
        static HOOK_ID sceneUpdateHook = 0;
        static HOOK_ID playerKillHook = 0;
        GameInteractor::Instance->UnregisterGameHookForPtr<GameInteractor::OnActorUpdate>(playerUpdateHook);
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnSceneInit>(playerKillHook);
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnSceneInit>(sceneUpdateHook);
        playerUpdateHook = 0;
        playerKillHook = 0;

        

        playerUpdateHook = GameInteractor::Instance->RegisterGameHookForPtr<GameInteractor::OnActorUpdate>(
            (uintptr_t)outerActor, [](Actor* actor) {
                if (CVarGetInteger("gEnhancements.Difficulty.BenDrowned", 0)) {
                    if (benInit == false) {
                        benInit = true;
                        benClean = false;
                        NameChanger(0);
                    }
                    if (shellSpawn <= 0) {
                        shellSpawn = 120;
                        ShellSelector();
                    }
                    if (soundSpawn <= 0) {
                        int randTimer = rand() % 8001;
                        soundSpawn = 5000 + randTimer;
                        SoundSelector();
                    }
                    DrownedTimerManager();
                } else {
                    playerKillHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>(
                        [](s8 sceneId, s8 spawnNum) {
                            GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnActorUpdate>(
                                playerUpdateHook);
                            GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnSceneInit>(playerKillHook);
                        });
                    BenCleanUp();
                }
        });
	});
}
