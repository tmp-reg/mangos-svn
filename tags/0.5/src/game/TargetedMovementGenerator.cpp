/* 
 * Copyright (C) 2005,2006 MaNGOS <http://www.mangosproject.org/>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ByteBuffer.h"
#include "TargetedMovementGenerator.h"
#include "Errors.h"
#include "Creature.h"
#include "MapManager.h"
#include "Spell.h"
#include "DestinationHolderImp.h"

#define SMALL_ALPHA 0.05

#include <cmath>

struct StackCleaner
{
    Creature &i_creature;
    StackCleaner(Creature &creature) : i_creature(creature) {}
    void Done(void) { i_creature.StopMoving(); }
    ~StackCleaner()
    {
        i_creature->Clear();
    }
};

void
TargetedMovementGenerator::_setTargetLocation(Creature &owner, float offset)
{
    if( !&i_target || !&owner )
        return;

    if( owner.hasUnitState(UNIT_STAT_ROOT) || owner.hasUnitState(UNIT_STAT_STUNDED) )
        return;

    float x, y, z;
    i_target.GetContactPoint( &owner, x, y, z );
    Traveller<Creature> traveller(owner);
    i_destinationHolder.SetDestination(traveller, x, y, z, offset);
    owner.addUnitState(UNIT_STAT_CHASE);
}

void
TargetedMovementGenerator::_setAttackRadius(Creature &owner)
{
    if(!&owner)
        return;
    float combat_reach = owner.GetFloatValue(UNIT_FIELD_COMBATREACH);
    if( combat_reach <= 0.0f )
        combat_reach = 1.0f;
    //float bounding_radius = owner.GetFloatValue(UNIT_FIELD_BOUNDINGRADIUS);
    i_attackRadius = combat_reach;                          // - SMALL_ALPHA);
}

void
TargetedMovementGenerator::Initialize(Creature &owner)
{
    if(!&owner)
        return;
    owner.setMoveRunFlag(true);
    _setAttackRadius(owner);
    _setTargetLocation(owner, 0);
}

void
TargetedMovementGenerator::Reset(Creature &owner)
{
    Initialize(owner);
}

void
TargetedMovementGenerator::TargetedHome(Creature &owner)
{
    if(!&owner)
        return;
    if(owner.hasUnitState(UNIT_STAT_FLEEING))
        return;
    DEBUG_LOG("Target home location %u", owner.GetGUIDLow());
    float x, y, z;
    owner.GetRespawnCoord(x, y, z);
    Traveller<Creature> traveller(owner);
    i_destinationHolder.SetDestination(traveller, x, y, z);
    traveller.Relocation(x,y,z);
    i_targetedHome = true;
    owner.clearUnitState(UNIT_STAT_ALL_STATE);
}

void
TargetedMovementGenerator::Update(Creature &owner, const uint32 & time_diff)
{
    if( !&owner || !owner.isAlive() || !&i_target || i_targetedHome )
        return;
    if( owner.hasUnitState(UNIT_STAT_ROOT) || owner.hasUnitState(UNIT_STAT_STUNDED) || owner.hasUnitState(UNIT_STAT_FLEEING))
        return;
    if( !owner.isInCombat() && !owner.hasUnitState(UNIT_STAT_FOLLOW) )
    {
        owner.AIM_Initialize();
        return;
    }

    // prevent crash after creature killed pet
    if (!owner.hasUnitState(UNIT_STAT_FOLLOW) && owner.getVictim() != &i_target)
        return;

    Traveller<Creature> traveller(owner);
    if (i_destinationHolder.UpdateTraveller(traveller, time_diff, false))
    {
        // put targeted movement generators on a higher priority
        i_destinationHolder.ResetUpdate(50);
        float dist = i_target.GetObjectSize() + owner.GetObjectSize() + OBJECT_CONTACT_DISTANCE;
        // try to counter precision differences
        if( i_destinationHolder.GetDistanceFromDestSq(i_target) > dist * dist + 0.1)
            _setTargetLocation(owner, 0);
        else if ( !owner.HasInArc( 0.1f, &i_target ) )
        {
            owner.SetInFront(&i_target);
            if( i_target.GetTypeId() == TYPEID_PLAYER )
                owner.SendUpdateToPlayer( (Player*)&i_target );
        }
        if( !owner.IsStopped() && i_destinationHolder.HasArrived())
        {
            owner.StopMoving();
            if(owner.canReachWithAttack(&i_target) && !owner.hasUnitState(UNIT_STAT_FOLLOW))
                owner.Attack(&i_target);
        }
    }
    /*

         //SpellEntry* spellInfo;
        if( reach )
        {
            if( owner.GetDistance2dSq( &i_target ) > 0.0f )
            {
                DEBUG_LOG("MOVEMENT : Distance = %f",owner.GetDistance2dSq( &i_target ));
                owner.addUnitState(UNIT_STAT_CHASE);
                _setTargetLocation(owner, 0);
            }
            else if ( !owner.HasInArc( 0.0f, &i_target ) )
            {
                DEBUG_LOG("MOVEMENT : Orientation = %f",owner.GetAngle(&i_target));
                owner.SetInFront(&i_target);
                if( i_target.GetTypeId() == TYPEID_PLAYER )
                    owner.SendUpdateToPlayer( (Player*)&i_target );
            }
            if( !owner.isInCombat() )
            {
                owner.AIM_Initialize();
                return;
            }
        }
        else if( i_target.isAlive() )
        {
            if( !owner.hasUnitState(UNIT_STAT_FOLLOW) && owner.isInCombat() )
            {
                if( spellInfo = owner.reachWithSpellAttack( &i_target ) )
                {
                    _spellAtack(owner, spellInfo);
                    return;
                }
            }
            if( owner.GetDistance2dSq( &i_target ) > 0.0f )
            {
                DEBUG_LOG("MOVEMENT : Distance = %f",owner.GetDistance2dSq( &i_target ));
                owner.addUnitState(UNIT_STAT_CHASE);
                _setTargetLocation(owner, 0);
            }
        }
        else if( !i_targetedHome )
        {
            if( !owner.hasUnitState(UNIT_STAT_FOLLOW) && owner.isInCombat() && (spellInfo = owner.reachWithSpellAttack(&i_target)) )
            {
                _spellAtack(owner, spellInfo);
                return;
            }
            _setTargetLocation(owner, 0);
            if(reach)
            {
                if( reach && owner.canReachWithAttack(&i_target) )
                {
                    owner.StopMoving();
                    if(!owner.hasUnitState(UNIT_STAT_FOLLOW))
                        owner.Attack(&i_target);
                    owner.clearUnitState(UNIT_STAT_CHASE);
                    DEBUG_LOG("UNIT IS THERE");
                }
                else
                {
                    _setTargetLocation(owner, 0);
                    DEBUG_LOG("Continue to chase");
                }
            }
        }*/
}

void TargetedMovementGenerator::_spellAtack(Creature &owner, SpellEntry* spellInfo)
{
    if(!spellInfo)
        return;
    owner.StopMoving();
    owner->Idle();
    if(owner.m_currentSpell)
    {
        if(owner.m_currentSpell->m_spellInfo->Id == spellInfo->Id )
            return;
        else
        {
            owner.m_currentSpell->cancel();
        }
    }
    Spell *spell = new Spell(&owner, spellInfo, false, 0);
    spell->SetAutoRepeat(true);
    //owner.addUnitState(UNIT_STAT_ATTACKING);
    owner.Attack(&owner);                                   //??
    owner.clearUnitState(UNIT_STAT_CHASE);
    SpellCastTargets targets;
    targets.setUnitTarget( &i_target );
    spell->prepare(&targets);
    owner.m_canMove = false;
    DEBUG_LOG("Spell Attack.");
}
