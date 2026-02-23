#include "stdafx.h"
#include "xrserver.h"
#include "xrmessages.h"
#include "xrserver_objects.h"
#include "xrServer_Objects_Alife_Monsters.h"
#include "Level.h"
#include <algorithm>


void xrServer::Perform_connect_spawn(CSE_Abstract* E, xrClientData* CL, NET_Packet& P)
{
	P.B.count = 0;
	xr_vector<u16>::iterator it = std::find(conn_spawned_ids.begin(), conn_spawned_ids.end(), E->ID);
	if (it != conn_spawned_ids.end())
	{
		//.		Msg("Rejecting redundant SPAWN data [%d]", E->ID);
		return;
	}

	conn_spawned_ids.push_back(E->ID);

	if (E->net_Processed) return;
	if (E->s_flags.is(M_SPAWN_OBJECT_PHANTOM)) return;

	//.	Msg("Perform connect spawn [%d][%s]", E->ID, E->s_name.c_str());

	// Connectivity order
	CSE_Abstract* Parent = ID_to_entity(E->ID_Parent);
	if (Parent) Perform_connect_spawn(Parent, CL, P);

	// Process
	Flags16 save = E->s_flags;
	//-------------------------------------------------
	E->s_flags.set(M_SPAWN_UPDATE,TRUE);
	if (0 == E->owner)
	{
		// PROCESS NAME; Name this entity
		if (E->s_flags.is(M_SPAWN_OBJECT_ASPLAYER))
		{
			CL->owner = E;
			if (CL->ps)
			{
				E->set_name_replace(CL->ps->getName());
			}
			else
			{
				// In single/co-op listen flow player-state can be created slightly later.
				// Do not crash on connect spawn; use client name as a temporary fallback.
				E->set_name_replace(*CL->name ? CL->name.c_str() : "mp_actor");
				Msg("! Perform_connect_spawn: missing player state for 0x%08x, using fallback actor name", CL->ID.value());
			}
		}

		// Associate
		E->owner = CL;
		E->Spawn_Write(P,TRUE);
		E->UPDATE_Write(P);

		CSE_ALifeObject* object = smart_cast<CSE_ALifeObject*>(E);
		VERIFY(object);
		if (!object->keep_saved_data_anyway())
			object->client_data.clear();
	}
	else
	{
		E->Spawn_Write(P, FALSE);
		E->UPDATE_Write(P);
		//		CSE_ALifeObject*	object = smart_cast<CSE_ALifeObject*>(E);
		//		VERIFY				(object);
		//		VERIFY				(object->client_data.empty());
	}
	//-----------------------------------------------------
	E->s_flags = save;
	SendTo(CL->ID, P, net_flags(TRUE,TRUE));
	E->net_Processed = TRUE;
}


void xrServer::SendLevelObjectsIdMap(IClient* _CL)
{
	xrClientData* CL = (xrClientData*)_CL;
	if (!CL)
		return;

	xr_vector<u16> ids;
	ids.reserve(entities.size());
	for (xrS_entities::const_iterator it = entities.begin(); it != entities.end(); ++it)
		ids.push_back(it->first);

	std::sort(ids.begin(), ids.end());

	const u16 total = (u16)ids.size();
	const u16 chunk_size = 256;

	for (u16 chunk_start = 0; chunk_start < total; chunk_start = u16(chunk_start + chunk_size))
	{
		const u16 chunk_count = std::min<u16>(chunk_size, u16(total - chunk_start));

		NET_Packet P;
		P.w_begin(M_H2C_SYNC_STATE);
		P.w_u8(COOP_SYNC_OBJECT_ID_MAP); // sync payload type: full object id map (chunked)
		P.w_u16(total);
		P.w_u16(chunk_start);
		P.w_u16(chunk_count);

		for (u16 i = 0; i < chunk_count; ++i)
		{
			CSE_Abstract* E = ID_to_entity(ids[chunk_start + i]);
			VERIFY(E);
			P.w_u16(E->ID);
			P.w_u16(E->ID_Parent);
		}

		SendTo(CL->ID, P, net_flags(TRUE, TRUE));
	}
}



void xrServer::OnSyncRequest(IClient* _CL, u8 sync_type)
{
	xrClientData* CL = static_cast<xrClientData*>(_CL);
	if (!CL || !CL->net_Accepted)
		return;

	switch (sync_type)
	{
	case COOP_SYNC_REQUEST_OBJECT_ID_MAP:
		SendLevelObjectsIdMap(CL);
		break;
	default:
		Msg("* unsupported C2H_SYNC_REQUEST type=%u from 0x%08x", sync_type, CL->ID.value());
		break;
	}
}

void xrServer::BroadcastLevelObjectsIdMap()
{
	struct Sender
	{
		xrServer* self;
		void operator()(IClient* client)
		{
			xrClientData* cl = static_cast<xrClientData*>(client);
			if (!cl || !cl->net_Accepted)
				return;
			self->SendLevelObjectsIdMap(cl);
		}
	};

	Sender sender = { this };
	ForEachClientDoSender(sender);
}

void xrServer::SendConfigFinished(ClientID const& clientId)
{
	NET_Packet P;
	P.w_begin(M_SV_CONFIG_FINISHED);
	SendTo(clientId, P, net_flags(TRUE,TRUE));
}

void xrServer::SendConnectionData(IClient* _CL)
{
	conn_spawned_ids.clear();
	xrClientData* CL = (xrClientData*)_CL;
	NET_Packet P;
	// Replicate current entities on to this client
	xrS_entities::iterator I = entities.begin(), E = entities.end();
	for (; I != E; ++I) I->second->net_Processed = FALSE;
	for (I = entities.begin(); I != E; ++I) Perform_connect_spawn(I->second, CL, P);

	// Start to send server logo and rules
	SendServerInfoToClient(CL->ID);
	SendLevelObjectsIdMap(CL);

	/*
		Msg("--- Our sended SPAWN IDs:");
		xr_vector<u16>::iterator it = conn_spawned_ids.begin();
		for (; it != conn_spawned_ids.end(); ++it)
		{
			Msg("%d", *it);
		}
		Msg("---- Our sended SPAWN END");
	*/
};

void xrServer::OnCL_Connected(IClient* _CL)
{
	xrClientData* CL = (xrClientData*)_CL;
	CL->net_Accepted = TRUE;
	/*if (Level().IsDemoPlay())
	{
		Level().StartPlayDemo();
		return;
	};*/
	///	Server_Client_Check(CL);
	//csPlayers.Enter					();	//sychronized by a parent call
	Export_game_type(CL);
	Perform_game_export();
	SendConnectionData(CL);

	VERIFY2(CL->ps, "Player state not created");
	if (!CL->ps)
	{
		Msg("! ERROR: Player state not created - incorect message sequence!");
		return;
	}

	game->OnPlayerConnect(CL->ID);
	BroadcastLevelObjectsIdMap();
}

void xrServer::SendConnectResult(IClient* CL, u8 res, u8 res1, char* ResultStr)
{
	NET_Packet P;
	P.w_begin(M_CLIENT_CONNECT_RESULT);
	P.w_u8(res);
	P.w_u8(res1);
	P.w_stringZ(ResultStr);
	P.w_clientID(CL->ID);

	if (SV_Client && SV_Client == CL)
		P.w_u8(1);
	else
		P.w_u8(0);
	P.w_stringZ(Level().m_caServerOptions);

	SendTo(CL->ID, P);

	if (!res) //need disconnect 
	{
#ifdef MP_LOGGING
		Msg("* Server disconnecting client, resaon: %s", ResultStr);
#endif
		Flush_Clients_Buffers();
		DisconnectClient(CL, ResultStr);
	}

	if (Level().IsDemoPlay())
	{
		Level().StartPlayDemo();

		return;
	}
};

void xrServer::SendProfileCreationError(IClient* CL, char const* reason)
{
	VERIFY(CL);

	NET_Packet P;
	P.w_begin(M_CLIENT_CONNECT_RESULT);
	P.w_u8(0);
	P.w_u8(ecr_profile_error);
	P.w_stringZ(reason);
	P.w_clientID(CL->ID);
	SendTo(CL->ID, P);
	if (CL != GetServerClient())
	{
		Flush_Clients_Buffers();
		DisconnectClient(CL, reason);
	}
}

//this method response for client validation on connect state (CLevel::net_start_client2)
//the first validation is CDKEY, then gamedata checksum (NeedToCheckClient_BuildVersion), then 
//banned or not...
//WARNING ! if you will change this method see M_AUTH_CHALLENGE event handler
void xrServer::Check_GameSpy_CDKey_Success(IClient* CL)
{
	if (NeedToCheckClient_BuildVersion(CL))
		return;
	//-------------------------------------------------------------
	RequestClientDigest(CL);
};

BOOL g_SV_Disable_Auth_Check = FALSE;

bool xrServer::NeedToCheckClient_BuildVersion(IClient* CL)
{
	/*#ifdef DEBUG
	
		return false; 
	
	#endif*/
	xrClientData* tmp_client = smart_cast<xrClientData*>(CL);
	VERIFY(tmp_client);
	PerformSecretKeysSync(tmp_client);

	// Co-op/single listen server should never enter MP auth challenge flow.
	// In some startup windows IsGameTypeSingle() can still be unreliable,
	// therefore we gate by both global game type and server game id.
	if (IsGameTypeSingle() || (GameID() == eGameIDSingle))
	{
		Msg("* NeedToCheckClient_BuildVersion: skipped for single/co-op client 0x%08x", CL->ID.value());
		return false;
	}

	if (g_SV_Disable_Auth_Check) return false;
	CL->flags.bVerified = FALSE;
	NET_Packet P;
	P.w_begin(M_AUTH_CHALLENGE);
	SendTo(CL->ID, P);
	return true;
};

void xrServer::OnBuildVersionRespond(IClient* CL, NET_Packet& P)
{
	u16 Type;
	P.r_begin(Type);
	u64 _our = FS.auth_get();
	u64 _him = P.r_u64();

#ifdef USE_DEBUG_AUTH
	Msg("_our = %d", _our);
	Msg("_him = %d", _him);
	_our = MP_DEBUG_AUTH;
#endif // USE_DEBUG_AUTH

	// Co-op/single listen server may still receive stale M_CL_AUTH packets.
	// Never reject by MP build-auth hash in this mode.
	if (IsGameTypeSingle() || (GameID() == eGameIDSingle))
	{
		Msg("* OnBuildVersionRespond: bypassed build-auth check for single/co-op client 0x%08x", CL->ID.value());
		RequestClientDigest(CL);
		return;
	}

	if (_our != _him)
	{
		SendConnectResult(CL, 0, ecr_data_verification_failed, "Data verification failed. Cheater?");
	}
	else
	{
		bool bAccessUser = false;
		string512 res_check;

		if (!CL->flags.bLocal)
		{
			bAccessUser = Check_ServerAccess(CL, res_check);
		}

		if (CL->flags.bLocal || bAccessUser)
		{
			//Check_BuildVersion_Success( CL );
			RequestClientDigest(CL);
		}
		else
		{
			Msg("* Client 0x%08x has an incorrect password", CL->ID.value());
			xr_strcat(res_check, "Invalid password.");
			SendConnectResult(CL, 0, ecr_password_verification_failed, res_check);
		}
	}
};

void xrServer::Check_BuildVersion_Success(IClient* CL)
{
	CL->flags.bVerified = TRUE;
	SendConnectResult(CL, 1, 0, "All Ok");
};
