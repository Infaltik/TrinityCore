/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Common.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "GameTime.h"
#include "Log.h"
#include "Player.h"

#include "ScriptMgr.h"
#include "Pet.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "World.h"


static void ResetSpellCooldowns(Player* player, bool onStartDuel);

static void ResetSpellCooldowns(Player* player, bool onStartDuel)
{
    // remove cooldowns on spells that have < 10 min CD > 30 sec and has no onHold
    player->GetSpellHistory()->ResetCooldowns([player, onStartDuel](SpellHistory::CooldownStorageType::iterator itr) -> bool
    {
        SpellInfo const* spellInfo = sSpellMgr->AssertSpellInfo(itr->first);
        uint32 remainingCooldown = player->GetSpellHistory()->GetRemainingCooldown(spellInfo);
        int32 totalCooldown = spellInfo->RecoveryTime;
        int32 categoryCooldown = spellInfo->CategoryRecoveryTime;

        player->ApplySpellMod(spellInfo->Id, SPELLMOD_COOLDOWN, totalCooldown, nullptr);

        if (int32 cooldownMod = player->GetTotalAuraModifier(SPELL_AURA_MOD_COOLDOWN))
            totalCooldown += cooldownMod * IN_MILLISECONDS;

        if (!spellInfo->HasAttribute(SPELL_ATTR6_IGNORE_CATEGORY_COOLDOWN_MODS))
            player->ApplySpellMod(spellInfo->Id, SPELLMOD_COOLDOWN, categoryCooldown, nullptr);

        return remainingCooldown > 0;
    }, true);
    
    player->GetSpellHistory()->ResetAllCooldowns();

    // pet cooldowns
    if (Pet* pet = player->GetPet())
        pet->GetSpellHistory()->ResetAllCooldowns();
}

void WorldSession::HandleDuelAcceptedOpcode(WorldPacket& recvPacket)
{
    Player* player = GetPlayer();
    if (!player->duel || player == player->duel->Initiator || player->duel->State != DUEL_STATE_CHALLENGED)
        return;

    ObjectGuid guid;
    recvPacket >> guid;

    Player* target = player->duel->Opponent;
    if (target->GetGuidValue(PLAYER_DUEL_ARBITER) != guid)
        return;

    //TC_LOG_DEBUG("network", "WORLD: Received CMSG_DUEL_ACCEPTED");
    TC_LOG_DEBUG("network", "Player 1 is: %s (%s)", player->GetGUID().ToString().c_str(), player->GetName().c_str());
    TC_LOG_DEBUG("network", "Player 2 is: %s (%s)", target->GetGUID().ToString().c_str(), target->GetName().c_str());

    time_t now = GameTime::GetGameTime();
    player->duel->StartTime = now + 3;
    target->duel->StartTime = now + 3;

    player->duel->State = DUEL_STATE_COUNTDOWN;
    target->duel->State = DUEL_STATE_COUNTDOWN;
	
	// Reset cds, hp and mana before duel
	ResetSpellCooldowns(player, true);
	ResetSpellCooldowns(target, true);

    player->SendDuelCountdown(3000);
    target->SendDuelCountdown(3000);
}

void WorldSession::HandleDuelCancelledOpcode(WorldPacket& recvPacket)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_DUEL_CANCELLED");
    Player* player = GetPlayer();

    ObjectGuid guid;
    recvPacket >> guid;

    // no duel requested
    if (!player->duel || player->duel->State == DUEL_STATE_COMPLETED)
        return;

    // player surrendered in a duel using /forfeit
    if (GetPlayer()->duel->State == DUEL_STATE_IN_PROGRESS)
    {
        GetPlayer()->CombatStopWithPets(true);
        GetPlayer()->duel->Opponent->CombatStopWithPets(true);

        GetPlayer()->CastSpell(GetPlayer(), 7267, true);    // beg
        GetPlayer()->DuelComplete(DUEL_WON);
        return;
    }

    GetPlayer()->DuelComplete(DUEL_INTERRUPTED);
}
