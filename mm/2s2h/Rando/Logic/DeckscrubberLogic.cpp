#include "Logic.h"
#include "2s2h/Rando/Types.h"

#include <numeric>
#include <iterator>
#include <spdlog/spdlog.h>

extern "C" {
#include "variables.h"
#include "ShipUtils.h"
uint64_t GetUnixTimestamp();
}

namespace Rando {

namespace Logic {

void ApplyDeckscrubberLogicToSaveContext(std::vector<RandoCheckId>& checkPool, std::vector<RandoItemId>& itemPool) {

    std::map<RandoItemId, std::vector<SceneId>> itemToSceneBlacklist = {
        { RI_MASK_DEKU,
          { SCENE_MITURIN, SCENE_MITURIN_BS, SCENE_LAST_DEKU, SCENE_LAST_GORON, SCENE_LAST_ZORA, SCENE_LAST_LINK,
            SCENE_SOUGEN, SCENE_LAST_BS } },
        { RI_SONG_SONATA,
          { SCENE_MITURIN, SCENE_MITURIN_BS, SCENE_LAST_DEKU, SCENE_LAST_GORON, SCENE_LAST_ZORA, SCENE_LAST_LINK,
            SCENE_SOUGEN, SCENE_LAST_BS } },
        { RI_MASK_ZORA,
          { SCENE_SEA, SCENE_SEA_BS, SCENE_LAST_DEKU, SCENE_LAST_GORON, SCENE_LAST_ZORA, SCENE_LAST_LINK, SCENE_SOUGEN,
            SCENE_LAST_BS } },
        { RI_SONG_NOVA,
          { SCENE_SEA, SCENE_SEA_BS, SCENE_LAST_DEKU, SCENE_LAST_GORON, SCENE_LAST_ZORA, SCENE_LAST_LINK, SCENE_SOUGEN,
            SCENE_LAST_BS } },
        { RI_MASK_GORON,
          { SCENE_HAKUGIN, SCENE_HAKUGIN_BS, SCENE_LAST_DEKU, SCENE_LAST_GORON, SCENE_LAST_ZORA, SCENE_LAST_LINK,
            SCENE_SOUGEN, SCENE_LAST_BS } },
        { RI_PROGRESSIVE_LULLABY,
          { SCENE_HAKUGIN, SCENE_HAKUGIN_BS, SCENE_LAST_DEKU, SCENE_LAST_GORON, SCENE_LAST_ZORA, SCENE_LAST_LINK,
            SCENE_SOUGEN, SCENE_LAST_BS } },
        { RI_REMAINS_ODOLWA,
          { SCENE_LAST_DEKU, SCENE_LAST_GORON, SCENE_LAST_ZORA, SCENE_LAST_LINK, SCENE_SOUGEN, SCENE_LAST_BS } },
        { RI_REMAINS_GOHT,
          { SCENE_LAST_DEKU, SCENE_LAST_GORON, SCENE_LAST_ZORA, SCENE_LAST_LINK, SCENE_SOUGEN, SCENE_LAST_BS } },
        { RI_REMAINS_GYORG,
          { SCENE_LAST_DEKU, SCENE_LAST_GORON, SCENE_LAST_ZORA, SCENE_LAST_LINK, SCENE_SOUGEN, SCENE_LAST_BS } },
        { RI_REMAINS_TWINMOLD,
          { SCENE_LAST_DEKU, SCENE_LAST_GORON, SCENE_LAST_ZORA, SCENE_LAST_LINK, SCENE_SOUGEN, SCENE_LAST_BS } },
        { RI_SONG_OATH,
          { SCENE_LAST_DEKU, SCENE_LAST_GORON, SCENE_LAST_ZORA, SCENE_LAST_LINK, SCENE_SOUGEN, SCENE_LAST_BS } },
    };

    uint64_t tick = GetUnixTimestamp();

    SaveContext copiedSaveContext;
    memcpy(&copiedSaveContext, &gSaveContext, sizeof(SaveContext));

    std::set<RandoRegionId> regionsInLogic = { RR_MAX };
    std::map<RandoCheckId, bool> checksInLogic;
    std::set<std::pair<RandoEvent, std::function<bool()>>*> eventsInLogic;

    RandoCheckId checkWithJunk = RC_UNKNOWN;
    std::set<RandoItemId> nonJunkItemsThatWeHaveTried;
    std::vector<RandoCheckId> checksWithJunk;
    std::vector<int> checksWithJunkWeights;
    int weight = 1;

    // Initial shuffle
    if (itemPool.size() > 1) {
        for (size_t i = 0; i < itemPool.size(); i++) {
            size_t j = Ship_Random(0, itemPool.size() - 1);
            std::swap(itemPool[i], itemPool[j]);
        }
    }

    auto handleError = [&](std::string message) {
        SPDLOG_ERROR("Items/Checks: {}/{}", itemPool.size(), checkPool.size());

        for (auto& randoCheckId : checkPool) {
            SPDLOG_ERROR("Check still in pool: {}", Rando::StaticData::Checks[randoCheckId].name);
        }
        for (RandoItemId randoItemId : itemPool) {
            SPDLOG_ERROR("Item still in pool: {}", Rando::StaticData::Items[randoItemId].spoilerName);
        }

        memcpy(&gSaveContext, &copiedSaveContext, sizeof(SaveContext));
        throw std::runtime_error(message);
    };

    // Helper: pick valid item respecting the scene blacklist
    RandoItemId currentRandoItemId = RI_UNKNOWN;
    auto pickValidItemForScene = [&](std::vector<RandoItemId>& pool, SceneId scene) -> RandoItemId {
        for (auto it = pool.rbegin(); it != pool.rend(); ++it) {
            RandoItemId candidate = *it;
            currentRandoItemId = candidate;

            auto blIt = itemToSceneBlacklist.find(candidate);
            if (blIt != itemToSceneBlacklist.end()) {
                const auto& blacklistScenes = blIt->second;

                if (std::find(blacklistScenes.begin(), blacklistScenes.end(), scene) != blacklistScenes.end()) {
                    // Item forbidden for this scene, skip it
                    continue;
                }
            }

            // Valid — remove and return
            RandoItemId chosen = candidate;
            pool.erase(std::next(it).base());
            return chosen;
        }

        handleError("No valid item found for check due to blacklist.");
        return RI_NONE; // unreachable
    };

    while (true) {
        if (GetUnixTimestamp() - tick > 10000) {
            handleError("Logic Generation Timeout");
        }

        bool regionsInLogicChanged = false;
        bool eventsInLogicChanged = false;
        bool checksInLogicChanged = false;

        // Discover reachable regions
        auto prevRegionsInLogicSize = regionsInLogic.size();
        for (RandoRegionId regionId : regionsInLogic) {
            FindReachableRegions(regionId, regionsInLogic);
        }
        if (regionsInLogic.size() != prevRegionsInLogicSize) {
            regionsInLogicChanged = true;
        }

        for (RandoRegionId regionId : regionsInLogic) {
            auto& randoRegion = Regions[regionId];

            for (auto& randoEvent : randoRegion.events) {
                if (!eventsInLogic.contains(&randoEvent) && randoEvent.second()) {
                    RANDO_EVENTS[randoEvent.first]++;
                    eventsInLogic.insert(&randoEvent);
                    eventsInLogicChanged = true;
                }
            }

            for (auto& [randoCheckId, checkLogic] : randoRegion.checks) {
                if (checksInLogic.find(randoCheckId) == checksInLogic.end() && checkLogic.first()) {

                    auto it = std::find(checkPool.begin(), checkPool.end(), randoCheckId);
                    bool isShuffled = it != checkPool.end();
                    checksInLogic.insert({ randoCheckId, isShuffled });

                    if (isShuffled) {
                        checkPool.erase(it);
                    }

                    RandoItemId randoItemId;

                    // Determine scene for blacklist check
                    SceneId checkSceneId = Rando::StaticData::Checks[randoCheckId].sceneId;

                    if (RANDO_SAVE_CHECKS[randoCheckId].skipped) {
                        uint32_t index = 0;
                        for (auto& item : itemPool) {
                            if (Rando::StaticData::Items[item].randoItemType == RITYPE_JUNK) {
                                randoItemId = item;
                                itemPool.erase(itemPool.begin() + index);
                                break;
                            }
                            index++;
                        }
                    } else if (isShuffled) {
                        // NEW: pick item that is NOT blacklisted for this check's scene
                        randoItemId = pickValidItemForScene(itemPool, checkSceneId);

                        if (Rando::StaticData::Items[randoItemId].randoItemType == RITYPE_JUNK ||
                            Rando::StaticData::Items[randoItemId].randoItemType == RITYPE_HEALTH) {

                            checksWithJunk.push_back(randoCheckId);
                            checksWithJunkWeights.push_back(weight);
                        }

                        SPDLOG_TRACE("Check: {}:{}", Rando::StaticData::Checks[randoCheckId].name,
                                     Rando::StaticData::Items[randoItemId].spoilerName);
                    } else {
                        randoItemId = Rando::StaticData::Checks[randoCheckId].randoItemId;
                    }

                    RANDO_SAVE_CHECKS[randoCheckId].randoItemId = randoItemId;
                    RANDO_SAVE_CHECKS[randoCheckId].shuffled = isShuffled;
                    GiveItem(ConvertItem(randoItemId));
                    checksInLogicChanged = true;
                }
            }
        }

        if (itemPool.empty()) {
            break;
        }

        // Junk replacement logic — unchanged
        if (!regionsInLogicChanged && !checksInLogicChanged && !eventsInLogicChanged) {

            if (checkWithJunk == RC_UNKNOWN) {
                if (checksWithJunk.empty()) {
                    handleError("No checks with junk, not sure what to do");
                }

                if (checksWithJunk.size() == 1) {
                    checkWithJunk = checksWithJunk[0];
                } else {
                    std::vector<double> cumulativeWeights(checksWithJunkWeights.size());
                    std::partial_sum(checksWithJunkWeights.begin(), checksWithJunkWeights.end(),
                                     cumulativeWeights.begin());
                    double random = Ship_Random(0, cumulativeWeights.back());
                    auto it = std::lower_bound(cumulativeWeights.begin(), cumulativeWeights.end(), random);
                    size_t index = std::distance(cumulativeWeights.begin(), it);

                    checkWithJunk = checksWithJunk[index];

                    checksWithJunk.erase(checksWithJunk.begin() + index);
                    checksWithJunkWeights.erase(checksWithJunkWeights.begin() + index);
                }
            }

            std::vector<std::pair<RandoItemId, int>> nonJunkItemsThatWeHaveNotTried;
            bool anyNonJunkItemsLeft = false;

            for (size_t i = 0; i < itemPool.size(); i++) {
                if (Rando::StaticData::Items[itemPool[i]].randoItemType != RITYPE_JUNK &&
                    Rando::StaticData::Items[itemPool[i]].randoItemType != RITYPE_HEALTH) {

                    anyNonJunkItemsLeft = true;

                    if (nonJunkItemsThatWeHaveTried.find(itemPool[i]) == nonJunkItemsThatWeHaveTried.end()) {
                        nonJunkItemsThatWeHaveNotTried.push_back({ itemPool[i], i });
                    }
                }
            }

            if (!anyNonJunkItemsLeft) {
                handleError("No non-junk items left");
            }

            if (nonJunkItemsThatWeHaveNotTried.empty()) {
                SPDLOG_TRACE("Already tried all non-junk items, leaving the last non-junk item in place: {}: {}",
                             Rando::StaticData::Checks[checkWithJunk].name,
                             Rando::StaticData::Items[RANDO_SAVE_CHECKS[checkWithJunk].randoItemId].spoilerName);
                checkWithJunk = RC_UNKNOWN;
                nonJunkItemsThatWeHaveTried.clear();
                continue;
            }

            RandoItemId oldRandoItemId = RANDO_SAVE_CHECKS[checkWithJunk].randoItemId;
            auto& [newRandoItemId, indexInPool] = nonJunkItemsThatWeHaveNotTried[0];

            RANDO_SAVE_CHECKS[checkWithJunk].randoItemId = newRandoItemId;

            RemoveItem(oldRandoItemId);
            GiveItem(ConvertItem(newRandoItemId));

            itemPool.erase(itemPool.begin() + indexInPool);
            itemPool.push_back(oldRandoItemId);

            nonJunkItemsThatWeHaveTried.insert(newRandoItemId);
            SPDLOG_TRACE("Attempting to replaced junk item: {}:{}", Rando::StaticData::Checks[checkWithJunk].name,
                         Rando::StaticData::Items[newRandoItemId].spoilerName);
        } else {
            weight++;
            if (checkWithJunk != RC_UNKNOWN) {
                SPDLOG_TRACE("Successfully Replaced junk item with: {}:{}",
                             Rando::StaticData::Checks[checkWithJunk].name,
                             Rando::StaticData::Items[RANDO_SAVE_CHECKS[checkWithJunk].randoItemId].spoilerName);
            }
            checkWithJunk = RC_UNKNOWN;
            nonJunkItemsThatWeHaveTried.clear();

            if (itemPool.size() > 1) {
                for (size_t i = 0; i < itemPool.size(); i++) {
                    size_t j = Ship_Random(0, itemPool.size() - 1);
                    std::swap(itemPool[i], itemPool[j]);
                }
            }
        }
    }

    for (auto& [randoCheckId, isShuffled] : checksInLogic) {
        copiedSaveContext.save.shipSaveInfo.rando.randoSaveChecks[randoCheckId].randoItemId =
            RANDO_SAVE_CHECKS[randoCheckId].randoItemId;
        copiedSaveContext.save.shipSaveInfo.rando.randoSaveChecks[randoCheckId].shuffled = isShuffled;
    }

    memcpy(&gSaveContext, &copiedSaveContext, sizeof(SaveContext));

    SPDLOG_INFO("Successfully placed all items with Glitchless logic");
}

} // namespace Logic

} // namespace Rando
