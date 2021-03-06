/* 
 * Copyright (C) 2005,2006,2007 MaNGOS <http://www.mangosproject.org/>
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

#include "GridNotifiers.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "UpdateData.h"
#include "Item.h"
#include "Map.h"
#include "MapManager.h"
#include "Transports.h"
#include "ObjectAccessor.h"

using namespace MaNGOS;

void
MaNGOS::PlayerNotifier::Visit(PlayerMapType &m)
{
    for(PlayerMapType::iterator iter=m.begin(); iter != m.end(); ++iter)
    {
        if( iter->getSource() == &i_player )
            continue;

        iter->getSource()->UpdateVisibilityOf(&i_player);
        i_player.UpdateVisibilityOf(iter->getSource());
    }
}

void
VisibleChangesNotifier::Visit(PlayerMapType &m)
{
    for(PlayerMapType::iterator iter=m.begin(); iter != m.end(); ++iter)
    {
        if(iter->getSource() == &i_object)
            continue;

        iter->getSource()->UpdateVisibilityOf(&i_object);
    }
}

void
VisibleNotifier::Visit(PlayerMapType &m)
{
    for(PlayerMapType::iterator iter=m.begin(); iter != m.end(); ++iter)
    {
        if( iter->getSource() == &i_player )
            continue;

        iter->getSource()->UpdateVisibilityOf(&i_player);
        i_player.UpdateVisibilityOf(iter->getSource(),i_data,i_data_updates,i_visibleNow);
        i_clientGUIDs.erase(iter->getSource()->GetGUID());
    }
}

void
VisibleNotifier::Notify()
{
    // at this moment i_clientGUIDs have guids that not iterate at grid level checks
    // but exist one case when this possible and object not out of range: transports
    if(Transport* transport = i_player.GetTransport())
    {
        for(Transport::PlayerSet::const_iterator itr = transport->GetPassengers().begin();itr!=transport->GetPassengers().end();++itr)
        {
            if(i_clientGUIDs.find((*itr)->GetGUID())!=i_clientGUIDs.end())
            {
                (*itr)->UpdateVisibilityOf(&i_player);
                i_player.UpdateVisibilityOf((*itr),i_data,i_data_updates,i_visibleNow);
                i_clientGUIDs.erase((*itr)->GetGUID());
            }
        }
    }

    // generate outOfRange for not iterate objects
    i_data.AddOutOfRangeGUID(i_clientGUIDs);
    for(Player::ClientGUIDs::iterator itr = i_clientGUIDs.begin();itr!=i_clientGUIDs.end();++itr)
    {
        i_player.m_clientGUIDs.erase(*itr);

        #ifdef MANGOS_DEBUG
        if((sLog.getLogFilter() & LOG_FILTER_VISIBILITY_CHANGES)==0)
            sLog.outDebug("Object %u (Type: %u) is out of range (no in active cells set) now for player %u",GUID_LOPART(*itr),GuidHigh2TypeId(GUID_HIPART(*itr)),i_player.GetGUIDLow());
        #endif
    }

    // send update to other players (except player updates that already sent using SendUpdateToPlayer)
    for(UpdateDataMapType::iterator iter = i_data_updates.begin(); iter != i_data_updates.end(); ++iter)
    {
        if(iter->first==&i_player)
            continue;

        WorldPacket packet;
        iter->second.BuildPacket(&packet);
        iter->first->GetSession()->SendPacket(&packet);
    }

    if( i_data.HasData() )
    {
        // send create/outofrange packet to player (except player create updates that already sent using SendUpdateToPlayer)
        WorldPacket packet;
        i_data.BuildPacket(&packet);
        i_player.GetSession()->SendPacket(&packet);

        // send out of range to other players if need
        std::set<uint64> const& oor = i_data.GetOutOfRangeGUIDs();
        for(std::set<uint64>::const_iterator iter = oor.begin(); iter != oor.end(); ++iter)
        {
            if(GUID_HIPART(*iter)!=HIGHGUID_PLAYER)
                continue;

            Player* plr = ObjectAccessor::GetPlayer(i_player,*iter);
            if(plr)
                plr->UpdateVisibilityOf(&i_player);
        }
    }

    // Now do operations that required done at object visibility change to visible

    // target aura duration for caster show only if target exist at caster client 
    // send data at target visibility change (adding to client)
    for(std::set<WorldObject*>::const_iterator vItr = i_visibleNow.begin(); vItr != i_visibleNow.end(); ++vItr)
        if((*vItr)!=&i_player && (*vItr)->isType(TYPE_UNIT))
            i_player.SendAuraDurationsForTarget((Unit*)(*vItr));
}

void
MessageDeliverer::Visit(PlayerMapType &m)
{
    for(PlayerMapType::iterator iter=m.begin(); iter != m.end(); ++iter)
    {
        if( (iter->getSource() != &i_player || i_toSelf)
            && (!i_ownTeamOnly || iter->getSource()->GetTeam() == i_player.GetTeam()) )
        {
            if(WorldSession* session = iter->getSource()->GetSession())
                session->SendPacket(i_message);
        }
    }
}

void
ObjectMessageDeliverer::Visit(PlayerMapType &m)
{
    for(PlayerMapType::iterator iter=m.begin(); iter != m.end(); ++iter)
    {
        if(WorldSession* session = iter->getSource()->GetSession())
            session->SendPacket(i_message);
    }
}

template<class T> void
ObjectUpdater::Visit(GridRefManager<T> &m)
{
    for(typename GridRefManager<T>::iterator iter = m.begin(); iter != m.end(); ++iter)
    {
        iter->getSource()->Update(i_timeDiff);
    }
}

template void ObjectUpdater::Visit<GameObject>(GameObjectMapType &);
template void ObjectUpdater::Visit<DynamicObject>(DynamicObjectMapType &);
