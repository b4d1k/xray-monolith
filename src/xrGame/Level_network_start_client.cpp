#include "stdafx.h"
//#include "PHdynamicdata.h"
//#include "Physics.h"
#include "level.h"
#include "../xrEngine/x_ray.h"
#include "../xrEngine/igame_persistent.h"

#include "ai_space.h"
#include "game_cl_base.h"
#include "NET_Queue.h"
#include "file_transfer.h"
#include "hudmanager.h"
#include "alife_simulator.h"

#include "../xrphysics/iphworld.h"


#include "phcommander.h"
#include "physics_game.h"
extern pureFrame* g_pNetProcessor;

bool CLevel::net_Start_client(const char* options)
{
	return false;
}

#include "string_table.h"

bool CLevel::net_start_client1()
{
    Msg("net_start_client1 start");
	pApp->LoadBegin();
	// name_of_server
	string64 name_of_server = "";
	//	xr_strcpy						(name_of_server,*m_caClientOptions);
	if (strchr(*m_caClientOptions, '/'))
		strncpy_s(name_of_server, *m_caClientOptions, strchr(*m_caClientOptions, '/') - *m_caClientOptions);

	if (strchr(name_of_server, '/')) *strchr(name_of_server, '/') = 0;

	// Startup client
	/*
		string256					temp;
		xr_sprintf						(temp,"%s %s",
									CStringTable().translate("st_client_connecting_to").c_str(), name_of_server);
	
		g_pGamePersistent->LoadTitle				(temp);
	*/
	g_pGamePersistent->LoadTitle();

    Msg("net_start_client1 end");
	return true;
}

#include "xrServer.h"

bool CLevel::net_start_client2()
{
    Msg("net_start_client2 start");
	if (psNET_direct_connect)
	{
		Server->create_direct_client();
		//offline account creation
		m_bConnectResultReceived = false;
		while (!m_bConnectResultReceived)
		{
			ClientReceive();
			Server->Update();
		}
	}

	m_host_object_id_map.clear();
	m_host_object_id_map_sync_received = false;
	reset_local_alife_replica();
	m_profile_data_sent = false;
	connected_to_server = Connect2Server(*m_caClientOptions);

    Msg("net_start_client2 end");
	return true;
}

void rescan_mp_archives()
{
	FS_Path* mp_archs_path = FS.get_path("$game_arch_mp$");
	FS.rescan_path(mp_archs_path->m_Path,
	               mp_archs_path->m_Flags.is(FS_Path::flRecurse)
	);
}

bool CLevel::net_start_client3()
{
    Msg("net_start_client3 start");
	if (connected_to_server)
	{
		LPCSTR level_name = NULL;
		LPCSTR level_ver = NULL;
		LPCSTR download_url = NULL;

		const bool local_single_host = Server && !!strstr(m_caServerOptions.c_str(), "/single");
		if (psNET_direct_connect || local_single_host) // single/direct or local single listen-host
		{
			shared_str const& server_options = Server->GetConnectOptions();
			level_name = name().c_str(); //Server->level_name		(server_options).c_str();
			level_ver = Server->level_version(server_options).c_str(); //1.0
		}
		else //multiplayer
		{
			level_name = get_net_DescriptionData().map_name;
			level_ver = get_net_DescriptionData().map_version;
			download_url = get_net_DescriptionData().download_url;
			rescan_mp_archives(); //because if we are using psNET_direct_connect, we not download map...
		}
		// Determine internal level-ID.
		// First try MP lookup, then fallback to local single-level lookup for co-op campaign maps.
		int level_id = pApp->Level_ID(level_name, level_ver, true);
		if (level_id == -1)
			level_id = pApp->Level_ID(level_name, level_ver, false);
		if (level_id == -1)
		{
			Disconnect();

			connected_to_server = FALSE;
			Msg("! Level (name:%s), (version:%s), not found, try to download from:%s",
			    level_name, level_ver, download_url);
			map_data.m_name = level_name;
			map_data.m_map_version = level_ver;
			map_data.m_map_download_url = download_url;
			map_data.m_map_loaded = false;
			return false;
		}
#ifdef DEBUG
		Msg("--- net_start_client3: level_id [%d], level_name[%s], level_version[%s]", level_id, level_name, level_ver);
#endif // #ifdef DEBUG
		map_data.m_name = level_name;
		map_data.m_map_version = level_ver;
		map_data.m_map_download_url = download_url;
		map_data.m_map_loaded = true;

		deny_m_spawn = FALSE;
		// Load level
		R_ASSERT2(Load(level_id), "Loading failed.");
		map_data.m_level_geom_crc32 = 0;
		if (!IsGameTypeSingle())
			CalculateLevelCrc32();
	}

    Msg("net_start_client3 end");
	return true;
}

bool CLevel::net_start_client4()
{
    Msg("net_start_client4 start");
	if (connected_to_server)
	{
		// Begin spawn
		//		g_pGamePersistent->LoadTitle		("st_client_spawning");
		g_pGamePersistent->LoadTitle();

		// Send physics to single or multithreaded mode

		create_physics_world(!!psDeviceFlags.test(mtPhysics), &ObjectSpace, &Objects, &Device);


		R_ASSERT(physics_world());

		m_ph_commander_physics_worldstep = xr_new<CPHCommander>();
		physics_world()->set_update_callback(m_ph_commander_physics_worldstep);

		physics_world()->set_default_contact_shotmark(ContactShotMark);
		physics_world()->set_default_character_contact_shotmark(CharacterContactShotMark);

		VERIFY(physics_world());
		physics_world()->set_step_time_callback((PhysicsStepTimeCallback*)&PhisStepsCallback);


		// Send network to single or multithreaded mode
		// *note: release version always has "mt_*" enabled
		Device.seqFrameMT.Remove(g_pNetProcessor);
		Device.seqFrame.Remove(g_pNetProcessor);
		if (psDeviceFlags.test(mtNetwork)) Device.seqFrameMT.Add(g_pNetProcessor,REG_PRIORITY_HIGH + 2);
		else Device.seqFrame.Add(g_pNetProcessor,REG_PRIORITY_LOW - 2);

		if (!psNET_direct_connect)
		{
			// Waiting for connection/configuration completition
			CTimer timer_sync;
			timer_sync.Start();
			while (!net_isCompleted_Connect()) Sleep(5);
			Msg("* connection sync: %d ms", timer_sync.GetElapsed_ms());
			while (!net_isCompleted_Sync())
			{
				ClientReceive();
				Sleep(5);
			}
		}
		/*
				if(psNET_direct_connect)
				{
					ClientReceive(); 
					if(Server)
							Server->Update()	;
					Sleep(5);
				}else
		
					while(!game_configured)			
					{ 
						ClientReceive(); 
						if(Server)
							Server->Update()	;
						Sleep(5); 
					}
		*/
	}

    Msg("net_start_client4 end");
	return true;
}

void CLevel::ClientSendProfileData()
{
	if (m_profile_data_sent)
		return;
	m_profile_data_sent = true;
#ifdef DEBUG
	Msg("* Sending profile data");
#endif
	NET_Packet NP;
	NP.w_begin(M_CREATE_PLAYER_STATE);
	game_PlayerState tmp_player_state(NULL);
	tmp_player_state.net_Export(NP, TRUE);
	SecureSend(NP, net_flags(TRUE, TRUE, TRUE, TRUE));
}


bool CLevel::net_start_client5()
{
    Msg("net_start_client5 start");
	if (connected_to_server)
	{
		// HUD

		// Textures
		if (!g_dedicated_server)
		{
			//			g_pGamePersistent->LoadTitle		("st_loading_textures");
			g_pGamePersistent->LoadTitle();
			//Device.Resources->DeferredLoad	(FALSE);
			Device.m_pRender->DeferredLoad(FALSE);
			//Device.Resources->DeferredUpload	();
			Device.m_pRender->ResourcesDeferredUpload();
			LL_CheckTextures();
		}
		sended_request_connection_data = FALSE;
		deny_m_spawn = TRUE;
	}
    Msg("net_start_client5 end");
	return true;
}

bool CLevel::net_start_client6()
{
    Msg("net_start_client6 start");

    static u32 call = 0;
    Msg("## client6 enter call=%u connected=%d game_configured=%d map_sync=%d",
        ++call, connected_to_server, game_configured, (int)map_data.m_map_sync_received);

	if (connected_to_server)
	{
		if (OnClient() && !OnServer() && !m_client_alife_simulator && !ai().get_alife())
		{
			m_client_alife_simulator = xr_new<CALifeSimulator>((xrServer*)nullptr);
			Msg("* client alife snapshot: empty simulator created");
		}

		// Sync
		const bool local_single_host = Server && !!strstr(m_caServerOptions.c_str(), "/single");
		const bool single_game_client = game && (game->Type() == eGameIDSingle);
		const bool force_single_coop_sync = !!strstr(Core.Params, "-coop_force_single_listen");
		if (local_single_host || single_game_client || force_single_coop_sync)
		{
			// For single/co-op startup, bypass MP map-sync branch and perform direct client sync.
			deny_m_spawn = FALSE;
			map_data.m_map_sync_received = true;
            if (!synchronize_client())
            {
                Msg("!! client6 fail: sync_client/map_data returned false");
                return false;
            }
		}
		else if (!synchronize_map_data())
		{
            Msg("!! client6 fail: sync_client/map_data returned false");
			return false;
		}

		if (!game_configured)
		{
			// Startup may still complete with delayed game configuration in co-op single/listen path.
			// Mark net start successful to avoid false failure in CLevel::net_start6().
			net_start_result_total = TRUE;
			pApp->LoadEnd();
			return true;
		}
		if (!g_dedicated_server)
		{
			g_hud->Load();
			g_hud->OnConnected();
		}

#ifdef DEBUG
		Msg("--- net_start_client6");
#endif // #ifdef DEBUG

		if (game)
		{
			game->OnConnected();
			if (game->Type() != eGameIDSingle)
			{
				m_file_transfer = xr_new<file_transfer::client_site>();
			}
		}

		//		g_pGamePersistent->LoadTitle		("st_client_synchronising");
		g_pGamePersistent->LoadTitle();
		Device.PreCache(60, true, true);
		net_start_result_total = TRUE;
	}
	else
	{
		net_start_result_total = FALSE;
	}

    Msg("## client6 after sync ok, game_configured=%d", game_configured);

	pApp->LoadEnd();
    Msg("net_start_client6 end");
	return true;
}
