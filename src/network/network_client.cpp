/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_client.cpp Client part of the network protocol. */

#ifdef ENABLE_NETWORK

#include "../stdafx.h"
#include "../debug.h"
#include "network_internal.h"
#include "network_gui.h"
#include "../saveload/saveload.h"
#include "../command_func.h"
#include "../console_func.h"
#include "../fileio_func.h"
#include "../3rdparty/md5/md5.h"
#include "../strings_func.h"
#include "../window_func.h"
#include "../company_func.h"
#include "../company_base.h"
#include "../company_gui.h"
#include "../core/random_func.hpp"
#include "../date_func.h"
#include "../rev.h"
#include "network.h"
#include "network_base.h"
#include "network_client.h"

#include "table/strings.h"

/* This file handles all the client-commands */

/**
 * Create a new socket for the client side of the game connection.
 * @param s The socket to connect with.
 */
ClientNetworkGameSocketHandler::ClientNetworkGameSocketHandler(SOCKET s) : NetworkGameSocketHandler(s), download_file(NULL), status(STATUS_INACTIVE)
{
	assert(ClientNetworkGameSocketHandler::my_client == NULL);
	ClientNetworkGameSocketHandler::my_client = this;
}

/** Clear whatever we assigned. */
ClientNetworkGameSocketHandler::~ClientNetworkGameSocketHandler()
{
	assert(ClientNetworkGameSocketHandler::my_client == this);
	ClientNetworkGameSocketHandler::my_client = NULL;

	/* If for whatever reason the handle isn't closed, do it now. */
	if (this->download_file != NULL) fclose(this->download_file);
}

NetworkRecvStatus ClientNetworkGameSocketHandler::CloseConnection(NetworkRecvStatus status)
{
	assert(status != NETWORK_RECV_STATUS_OKAY);
	/*
	 * Sending a message just before leaving the game calls cs->Send_Packets.
	 * This might invoke this function, which means that when we close the
	 * connection after cs->Send_Packets we will close an already closed
	 * connection. This handles that case gracefully without having to make
	 * that code any more complex or more aware of the validity of the socket.
	 */
	if (this->sock == INVALID_SOCKET) return status;

	DEBUG(net, 1, "Closed client connection %d", this->client_id);

	this->Send_Packets(true);

	delete this->GetInfo();
	delete this;

	return status;
}

/**
 * Handle an error coming from the client side.
 * @param res The "error" that happened.
 */
void ClientNetworkGameSocketHandler::ClientError(NetworkRecvStatus res)
{
	/* First, send a CLIENT_ERROR to the server, so he knows we are
	 *  disconnection (and why!) */
	NetworkErrorCode errorno;

	/* We just want to close the connection.. */
	if (res == NETWORK_RECV_STATUS_CLOSE_QUERY) {
		this->NetworkSocketHandler::CloseConnection();
		this->CloseConnection(res);
		_networking = false;

		DeleteWindowById(WC_NETWORK_STATUS_WINDOW, 0);
		return;
	}

	switch (res) {
		case NETWORK_RECV_STATUS_DESYNC:          errorno = NETWORK_ERROR_DESYNC; break;
		case NETWORK_RECV_STATUS_SAVEGAME:        errorno = NETWORK_ERROR_SAVEGAME_FAILED; break;
		case NETWORK_RECV_STATUS_NEWGRF_MISMATCH: errorno = NETWORK_ERROR_NEWGRF_MISMATCH; break;
		default:                                  errorno = NETWORK_ERROR_GENERAL; break;
	}

	/* This means we fucked up and the server closed the connection */
	if (res != NETWORK_RECV_STATUS_SERVER_ERROR && res != NETWORK_RECV_STATUS_SERVER_FULL &&
			res != NETWORK_RECV_STATUS_SERVER_BANNED) {
		SendError(errorno);
	}

	_switch_mode = SM_MENU;
	this->CloseConnection(res);
	_networking = false;
}


/**
 * Check whether we received/can send some data from/to the server and
 * when that's the case handle it appropriately.
 * @return true when everything went okay.
 */
/*static */ bool ClientNetworkGameSocketHandler::Receive()
{
	if (my_client->CanSendReceive()) {
		NetworkRecvStatus res = my_client->Recv_Packets();
		if (res != NETWORK_RECV_STATUS_OKAY) {
			/* The client made an error of which we can not recover
				*   close the client and drop back to main menu */
			my_client->ClientError(res);
			return false;
		}
	}
	return _networking;
}

/** Send the packets of this socket handler. */
/*static */ void ClientNetworkGameSocketHandler::Send()
{
	my_client->Send_Packets();
}

/**
 * Actual game loop for the client.
 * @return Whether everything went okay, or not.
 */
/* static */ bool ClientNetworkGameSocketHandler::GameLoop()
{
	_frame_counter++;

	NetworkExecuteLocalCommandQueue();

	extern void StateGameLoop();
	StateGameLoop();

	/* Check if we are in sync! */
	if (_sync_frame != 0) {
		if (_sync_frame == _frame_counter) {
#ifdef NETWORK_SEND_DOUBLE_SEED
			if (_sync_seed_1 != _random.state[0] || _sync_seed_2 != _random.state[1]) {
#else
			if (_sync_seed_1 != _random.state[0]) {
#endif
				NetworkError(STR_NETWORK_ERROR_DESYNC);
				DEBUG(desync, 1, "sync_err: %08x; %02x", _date, _date_fract);
				DEBUG(net, 0, "Sync error detected!");
				my_client->ClientError(NETWORK_RECV_STATUS_DESYNC);
				return false;
			}

			/* If this is the first time we have a sync-frame, we
			 *   need to let the server know that we are ready and at the same
			 *   frame as he is.. so we can start playing! */
			if (_network_first_time) {
				_network_first_time = false;
				SendAck();
			}

			_sync_frame = 0;
		} else if (_sync_frame < _frame_counter) {
			DEBUG(net, 1, "Missed frame for sync-test (%d / %d)", _sync_frame, _frame_counter);
			_sync_frame = 0;
		}
	}

	return true;
}


/** Our client's connection. */
ClientNetworkGameSocketHandler * ClientNetworkGameSocketHandler::my_client = NULL;

static uint32 last_ack_frame;

/** One bit of 'entropy' used to generate a salt for the company passwords. */
static uint32 _password_game_seed;
/** The other bit of 'entropy' used to generate a salt for the company passwords. */
static char _password_server_id[NETWORK_SERVER_ID_LENGTH];

/** Maximum number of companies of the currently joined server. */
static uint8 _network_server_max_companies;
/** Maximum number of spectators of the currently joined server. */
static uint8 _network_server_max_spectators;

/** Who would we like to join as. */
CompanyID _network_join_as;

/** Login password from -p argument */
const char *_network_join_server_password = NULL;
/** Company password from -P argument */
const char *_network_join_company_password = NULL;

/** Make sure the server ID length is the same as a md5 hash. */
assert_compile(NETWORK_SERVER_ID_LENGTH == 16 * 2 + 1);

/**
 * Generates a hashed password for the company name.
 * @param password the password to 'encrypt'.
 * @return the hashed password.
 */
static const char *GenerateCompanyPasswordHash(const char *password)
{
	if (StrEmpty(password)) return password;

	char salted_password[NETWORK_SERVER_ID_LENGTH];

	memset(salted_password, 0, sizeof(salted_password));
	snprintf(salted_password, sizeof(salted_password), "%s", password);
	/* Add the game seed and the server's ID as the salt. */
	for (uint i = 0; i < NETWORK_SERVER_ID_LENGTH - 1; i++) salted_password[i] ^= _password_server_id[i] ^ (_password_game_seed >> i);

	Md5 checksum;
	uint8 digest[16];
	static char hashed_password[NETWORK_SERVER_ID_LENGTH];

	/* Generate the MD5 hash */
	checksum.Append((const uint8*)salted_password, sizeof(salted_password) - 1);
	checksum.Finish(digest);

	for (int di = 0; di < 16; di++) sprintf(hashed_password + di * 2, "%02x", digest[di]);
	hashed_password[lengthof(hashed_password) - 1] = '\0';

	return hashed_password;
}

/**
 * Hash the current company password; used when the server 'company' sets his/her password.
 */
void HashCurrentCompanyPassword(const char *password)
{
	_password_game_seed = _settings_game.game_creation.generation_seed;
	strecpy(_password_server_id, _settings_client.network.network_id, lastof(_password_server_id));

	const char *new_pw = GenerateCompanyPasswordHash(password);
	strecpy(_network_company_states[_local_company].password, new_pw, lastof(_network_company_states[_local_company].password));

	if (_network_server) {
		NetworkServerUpdateCompanyPassworded(_local_company, !StrEmpty(_network_company_states[_local_company].password));
	}
}


/***********
 * Sending functions
 *   DEF_CLIENT_SEND_COMMAND has no parameters
 ************/

NetworkRecvStatus ClientNetworkGameSocketHandler::SendCompanyInformationQuery()
{
	my_client->status = STATUS_COMPANY_INFO;
	_network_join_status = NETWORK_JOIN_STATUS_GETTING_COMPANY_INFO;
	SetWindowDirty(WC_NETWORK_STATUS_WINDOW, 0);

	Packet *p = new Packet(PACKET_CLIENT_COMPANY_INFO);
	my_client->Send_Packet(p);
	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::SendJoin()
{
	my_client->status = STATUS_JOIN;
	_network_join_status = NETWORK_JOIN_STATUS_AUTHORIZING;
	SetWindowDirty(WC_NETWORK_STATUS_WINDOW, 0);

	Packet *p = new Packet(PACKET_CLIENT_JOIN);
	p->Send_string(_openttd_revision);
	p->Send_string(_settings_client.network.client_name); // Client name
	p->Send_uint8 (_network_join_as);     // PlayAs
	p->Send_uint8 (NETLANG_ANY);          // Language
	my_client->Send_Packet(p);
	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::SendNewGRFsOk()
{
	Packet *p = new Packet(PACKET_CLIENT_NEWGRFS_CHECKED);
	my_client->Send_Packet(p);
	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::SendGamePassword(const char *password)
{
	Packet *p = new Packet(PACKET_CLIENT_GAME_PASSWORD);
	p->Send_string(password);
	my_client->Send_Packet(p);
	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::SendCompanyPassword(const char *password)
{
	Packet *p = new Packet(PACKET_CLIENT_COMPANY_PASSWORD);
	p->Send_string(GenerateCompanyPasswordHash(password));
	my_client->Send_Packet(p);
	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::SendGetMap()
{
	my_client->status = STATUS_MAP_WAIT;

	Packet *p = new Packet(PACKET_CLIENT_GETMAP);
	/* Send the OpenTTD version to the server, let it validate it too.
	 * But only do it for stable releases because of those we are sure
	 * that everybody has the same NewGRF version. For trunk and the
	 * branches we make tarballs of the OpenTTDs compiled from tarball
	 * will have the lower bits set to 0. As such they would become
	 * incompatible, which we would like to prevent by this. */
	if (HasBit(_openttd_newgrf_version, 19)) p->Send_uint32(_openttd_newgrf_version);
	my_client->Send_Packet(p);
	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::SendMapOk()
{
	my_client->status = STATUS_ACTIVE;

	Packet *p = new Packet(PACKET_CLIENT_MAP_OK);
	my_client->Send_Packet(p);
	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::SendAck()
{
	Packet *p = new Packet(PACKET_CLIENT_ACK);

	p->Send_uint32(_frame_counter);
	my_client->Send_Packet(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/* Send a command packet to the server */
NetworkRecvStatus ClientNetworkGameSocketHandler::SendCommand(const CommandPacket *cp)
{
	Packet *p = new Packet(PACKET_CLIENT_COMMAND);
	my_client->Send_Command(p, cp);

	my_client->Send_Packet(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/* Send a chat-packet over the network */
NetworkRecvStatus ClientNetworkGameSocketHandler::SendChat(NetworkAction action, DestType type, int dest, const char *msg, int64 data)
{
	Packet *p = new Packet(PACKET_CLIENT_CHAT);

	p->Send_uint8 (action);
	p->Send_uint8 (type);
	p->Send_uint32(dest);
	p->Send_string(msg);
	p->Send_uint64(data);

	my_client->Send_Packet(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/* Send an error-packet over the network */
NetworkRecvStatus ClientNetworkGameSocketHandler::SendError(NetworkErrorCode errorno)
{
	Packet *p = new Packet(PACKET_CLIENT_ERROR);

	p->Send_uint8(errorno);
	my_client->Send_Packet(p);
	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::SendSetPassword(const char *password)
{
	Packet *p = new Packet(PACKET_CLIENT_SET_PASSWORD);

	p->Send_string(GenerateCompanyPasswordHash(password));
	my_client->Send_Packet(p);
	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::SendSetName(const char *name)
{
	Packet *p = new Packet(PACKET_CLIENT_SET_NAME);

	p->Send_string(name);
	my_client->Send_Packet(p);
	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::SendQuit()
{
	Packet *p = new Packet(PACKET_CLIENT_QUIT);

	my_client->Send_Packet(p);
	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::SendRCon(const char *pass, const char *command)
{
	Packet *p = new Packet(PACKET_CLIENT_RCON);
	p->Send_string(pass);
	p->Send_string(command);
	my_client->Send_Packet(p);
	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::SendMove(CompanyID company, const char *pass)
{
	Packet *p = new Packet(PACKET_CLIENT_MOVE);
	p->Send_uint8(company);
	p->Send_string(GenerateCompanyPasswordHash(pass));
	my_client->Send_Packet(p);
	return NETWORK_RECV_STATUS_OKAY;
}


/***********
 * Receiving functions
 *   DEF_CLIENT_RECEIVE_COMMAND has parameter: Packet *p
 ************/

extern bool SafeSaveOrLoad(const char *filename, int mode, GameMode newgm, Subdirectory subdir);
extern StringID _switch_mode_errorstr;

DEF_GAME_RECEIVE_COMMAND(Client, PACKET_SERVER_FULL)
{
	/* We try to join a server which is full */
	_switch_mode_errorstr = STR_NETWORK_ERROR_SERVER_FULL;
	DeleteWindowById(WC_NETWORK_STATUS_WINDOW, 0);

	return NETWORK_RECV_STATUS_SERVER_FULL;
}

DEF_GAME_RECEIVE_COMMAND(Client, PACKET_SERVER_BANNED)
{
	/* We try to join a server where we are banned */
	_switch_mode_errorstr = STR_NETWORK_ERROR_SERVER_BANNED;
	DeleteWindowById(WC_NETWORK_STATUS_WINDOW, 0);

	return NETWORK_RECV_STATUS_SERVER_BANNED;
}

DEF_GAME_RECEIVE_COMMAND(Client, PACKET_SERVER_COMPANY_INFO)
{
	if (this->status != STATUS_COMPANY_INFO) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	byte company_info_version = p->Recv_uint8();

	if (!this->HasClientQuit() && company_info_version == NETWORK_COMPANY_INFO_VERSION) {
		/* We have received all data... (there are no more packets coming) */
		if (!p->Recv_bool()) return NETWORK_RECV_STATUS_CLOSE_QUERY;

		CompanyID current = (Owner)p->Recv_uint8();
		if (current >= MAX_COMPANIES) return NETWORK_RECV_STATUS_CLOSE_QUERY;

		NetworkCompanyInfo *company_info = GetLobbyCompanyInfo(current);
		if (company_info == NULL) return NETWORK_RECV_STATUS_CLOSE_QUERY;

		p->Recv_string(company_info->company_name, sizeof(company_info->company_name));
		company_info->inaugurated_year = p->Recv_uint32();
		company_info->company_value    = p->Recv_uint64();
		company_info->money            = p->Recv_uint64();
		company_info->income           = p->Recv_uint64();
		company_info->performance      = p->Recv_uint16();
		company_info->use_password     = p->Recv_bool();
		for (uint i = 0; i < NETWORK_VEH_END; i++) {
			company_info->num_vehicle[i] = p->Recv_uint16();
		}
		for (uint i = 0; i < NETWORK_VEH_END; i++) {
			company_info->num_station[i] = p->Recv_uint16();
		}
		company_info->ai               = p->Recv_bool();

		p->Recv_string(company_info->clients, sizeof(company_info->clients));

		SetWindowDirty(WC_NETWORK_WINDOW, 0);

		return NETWORK_RECV_STATUS_OKAY;
	}

	return NETWORK_RECV_STATUS_CLOSE_QUERY;
}

/* This packet contains info about the client (playas and name)
 *  as client we save this in NetworkClientInfo, linked via 'client_id'
 *  which is always an unique number on a server. */
DEF_GAME_RECEIVE_COMMAND(Client, PACKET_SERVER_CLIENT_INFO)
{
	NetworkClientInfo *ci;
	ClientID client_id = (ClientID)p->Recv_uint32();
	CompanyID playas = (CompanyID)p->Recv_uint8();
	char name[NETWORK_NAME_LENGTH];

	p->Recv_string(name, sizeof(name));

	if (this->status < STATUS_AUTHORIZED) return NETWORK_RECV_STATUS_MALFORMED_PACKET;
	if (this->HasClientQuit()) return NETWORK_RECV_STATUS_CONN_LOST;

	ci = NetworkFindClientInfoFromClientID(client_id);
	if (ci != NULL) {
		if (playas == ci->client_playas && strcmp(name, ci->client_name) != 0) {
			/* Client name changed, display the change */
			NetworkTextMessage(NETWORK_ACTION_NAME_CHANGE, CC_DEFAULT, false, ci->client_name, name);
		} else if (playas != ci->client_playas) {
			/* The client changed from client-player..
			 * Do not display that for now */
		}

		/* Make sure we're in the company the server tells us to be in,
		 * for the rare case that we get moved while joining. */
		if (client_id == _network_own_client_id) SetLocalCompany(!Company::IsValidID(playas) ? COMPANY_SPECTATOR : playas);

		ci->client_playas = playas;
		strecpy(ci->client_name, name, lastof(ci->client_name));

		SetWindowDirty(WC_CLIENT_LIST, 0);

		return NETWORK_RECV_STATUS_OKAY;
	}

	/* We don't have this client_id yet, find an empty client_id, and put the data there */
	ci = new NetworkClientInfo(client_id);
	ci->client_playas = playas;
	if (client_id == _network_own_client_id) this->SetInfo(ci);

	strecpy(ci->client_name, name, lastof(ci->client_name));

	SetWindowDirty(WC_CLIENT_LIST, 0);

	return NETWORK_RECV_STATUS_OKAY;
}

DEF_GAME_RECEIVE_COMMAND(Client, PACKET_SERVER_ERROR)
{
	NetworkErrorCode error = (NetworkErrorCode)p->Recv_uint8();

	switch (error) {
		/* We made an error in the protocol, and our connection is closed.... */
		case NETWORK_ERROR_NOT_AUTHORIZED:
		case NETWORK_ERROR_NOT_EXPECTED:
		case NETWORK_ERROR_COMPANY_MISMATCH:
			_switch_mode_errorstr = STR_NETWORK_ERROR_SERVER_ERROR;
			break;
		case NETWORK_ERROR_FULL:
			_switch_mode_errorstr = STR_NETWORK_ERROR_SERVER_FULL;
			break;
		case NETWORK_ERROR_WRONG_REVISION:
			_switch_mode_errorstr = STR_NETWORK_ERROR_WRONG_REVISION;
			break;
		case NETWORK_ERROR_WRONG_PASSWORD:
			_switch_mode_errorstr = STR_NETWORK_ERROR_WRONG_PASSWORD;
			break;
		case NETWORK_ERROR_KICKED:
			_switch_mode_errorstr = STR_NETWORK_ERROR_KICKED;
			break;
		case NETWORK_ERROR_CHEATER:
			_switch_mode_errorstr = STR_NETWORK_ERROR_CHEATER;
			break;
		case NETWORK_ERROR_TOO_MANY_COMMANDS:
			_switch_mode_errorstr = STR_NETWORK_ERROR_TOO_MANY_COMMANDS;
			break;
		default:
			_switch_mode_errorstr = STR_NETWORK_ERROR_LOSTCONNECTION;
	}

	DeleteWindowById(WC_NETWORK_STATUS_WINDOW, 0);

	return NETWORK_RECV_STATUS_SERVER_ERROR;
}

DEF_GAME_RECEIVE_COMMAND(Client, PACKET_SERVER_CHECK_NEWGRFS)
{
	if (this->status != STATUS_JOIN) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	uint grf_count = p->Recv_uint8();
	NetworkRecvStatus ret = NETWORK_RECV_STATUS_OKAY;

	/* Check all GRFs */
	for (; grf_count > 0; grf_count--) {
		GRFIdentifier c;
		this->Recv_GRFIdentifier(p, &c);

		/* Check whether we know this GRF */
		const GRFConfig *f = FindGRFConfig(c.grfid, FGCM_EXACT, c.md5sum);
		if (f == NULL) {
			/* We do not know this GRF, bail out of initialization */
			char buf[sizeof(c.md5sum) * 2 + 1];
			md5sumToString(buf, lastof(buf), c.md5sum);
			DEBUG(grf, 0, "NewGRF %08X not found; checksum %s", BSWAP32(c.grfid), buf);
			ret = NETWORK_RECV_STATUS_NEWGRF_MISMATCH;
		}
	}

	if (ret == NETWORK_RECV_STATUS_OKAY) {
		/* Start receiving the map */
		return SendNewGRFsOk();
	}

	/* NewGRF mismatch, bail out */
	_switch_mode_errorstr = STR_NETWORK_ERROR_NEWGRF_MISMATCH;
	return ret;
}

DEF_GAME_RECEIVE_COMMAND(Client, PACKET_SERVER_NEED_GAME_PASSWORD)
{
	if (this->status < STATUS_JOIN || this->status >= STATUS_AUTH_GAME) return NETWORK_RECV_STATUS_MALFORMED_PACKET;
	this->status = STATUS_AUTH_GAME;

	const char *password = _network_join_server_password;
	if (!StrEmpty(password)) {
		return SendGamePassword(password);
	}

	ShowNetworkNeedPassword(NETWORK_GAME_PASSWORD);

	return NETWORK_RECV_STATUS_OKAY;
}

DEF_GAME_RECEIVE_COMMAND(Client, PACKET_SERVER_NEED_COMPANY_PASSWORD)
{
	if (this->status < STATUS_JOIN || this->status >= STATUS_AUTH_COMPANY) return NETWORK_RECV_STATUS_MALFORMED_PACKET;
	this->status = STATUS_AUTH_COMPANY;

	_password_game_seed = p->Recv_uint32();
	p->Recv_string(_password_server_id, sizeof(_password_server_id));
	if (this->HasClientQuit()) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	const char *password = _network_join_company_password;
	if (!StrEmpty(password)) {
		return SendCompanyPassword(password);
	}

	ShowNetworkNeedPassword(NETWORK_COMPANY_PASSWORD);

	return NETWORK_RECV_STATUS_OKAY;
}

DEF_GAME_RECEIVE_COMMAND(Client, PACKET_SERVER_WELCOME)
{
	if (this->status < STATUS_JOIN || this->status >= STATUS_AUTHORIZED) return NETWORK_RECV_STATUS_MALFORMED_PACKET;
	this->status = STATUS_AUTHORIZED;

	_network_own_client_id = (ClientID)p->Recv_uint32();

	/* Initialize the password hash salting variables, even if they were previously. */
	_password_game_seed = p->Recv_uint32();
	p->Recv_string(_password_server_id, sizeof(_password_server_id));

	/* Start receiving the map */
	return SendGetMap();
}

DEF_GAME_RECEIVE_COMMAND(Client, PACKET_SERVER_WAIT)
{
	if (this->status != STATUS_AUTHORIZED) return NETWORK_RECV_STATUS_MALFORMED_PACKET;
	this->status = STATUS_MAP_WAIT;

	_network_join_status = NETWORK_JOIN_STATUS_WAITING;
	_network_join_waiting = p->Recv_uint8();
	SetWindowDirty(WC_NETWORK_STATUS_WINDOW, 0);

	/* We are put on hold for receiving the map.. we need GUI for this ;) */
	DEBUG(net, 1, "The server is currently busy sending the map to someone else, please wait..." );
	DEBUG(net, 1, "There are %d clients in front of you", _network_join_waiting);

	return NETWORK_RECV_STATUS_OKAY;
}

DEF_GAME_RECEIVE_COMMAND(Client, PACKET_SERVER_MAP_BEGIN)
{
	if (this->status < STATUS_AUTHORIZED || this->status >= STATUS_MAP) return NETWORK_RECV_STATUS_MALFORMED_PACKET;
	this->status = STATUS_MAP;

	if (this->HasClientQuit()) return NETWORK_RECV_STATUS_CONN_LOST;
	if (this->download_file != NULL) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	this->download_file = FioFOpenFile("network_client.tmp", "wb", AUTOSAVE_DIR);
	if (this->download_file == NULL) {
		_switch_mode_errorstr = STR_NETWORK_ERROR_SAVEGAMEERROR;
		return NETWORK_RECV_STATUS_SAVEGAME;
	}

	_frame_counter = _frame_counter_server = _frame_counter_max = p->Recv_uint32();

	_network_join_bytes = 0;
	_network_join_bytes_total = p->Recv_uint32();

	/* If the network connection has been closed due to loss of connection
	 * or when _network_join_kbytes_total is 0, the join status window will
	 * do a division by zero. When the connection is lost, we just return
	 * that. If kbytes_total is 0, the packet must be malformed as a
	 * savegame less than 1 kilobyte is practically impossible. */
	if (this->HasClientQuit()) return NETWORK_RECV_STATUS_CONN_LOST;
	if (_network_join_bytes_total == 0) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	_network_join_status = NETWORK_JOIN_STATUS_DOWNLOADING;
	SetWindowDirty(WC_NETWORK_STATUS_WINDOW, 0);

	return NETWORK_RECV_STATUS_OKAY;
}

DEF_GAME_RECEIVE_COMMAND(Client, PACKET_SERVER_MAP_DATA)
{
	if (this->status != STATUS_MAP) return NETWORK_RECV_STATUS_MALFORMED_PACKET;
	if (this->download_file == NULL) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	/* We are still receiving data, put it to the file */
	if (fwrite(p->buffer + p->pos, 1, p->size - p->pos, this->download_file) != (size_t)(p->size - p->pos)) {
		_switch_mode_errorstr = STR_NETWORK_ERROR_SAVEGAMEERROR;
		fclose(this->download_file);
		this->download_file = NULL;
		return NETWORK_RECV_STATUS_SAVEGAME;
	}

	_network_join_bytes = ftell(this->download_file);
	SetWindowDirty(WC_NETWORK_STATUS_WINDOW, 0);

	return NETWORK_RECV_STATUS_OKAY;
}

DEF_GAME_RECEIVE_COMMAND(Client, PACKET_SERVER_MAP_DONE)
{
	if (this->status != STATUS_MAP) return NETWORK_RECV_STATUS_MALFORMED_PACKET;
	if (this->download_file == NULL) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	fclose(this->download_file);
	this->download_file = NULL;

	_network_join_status = NETWORK_JOIN_STATUS_PROCESSING;
	SetWindowDirty(WC_NETWORK_STATUS_WINDOW, 0);

	/* The map is done downloading, load it */
	if (!SafeSaveOrLoad("network_client.tmp", SL_LOAD, GM_NORMAL, AUTOSAVE_DIR)) {
		DeleteWindowById(WC_NETWORK_STATUS_WINDOW, 0);
		_switch_mode_errorstr = STR_NETWORK_ERROR_SAVEGAMEERROR;
		return NETWORK_RECV_STATUS_SAVEGAME;
	}
	/* If the savegame has successfully loaded, ALL windows have been removed,
	 * only toolbar/statusbar and gamefield are visible */

	/* Say we received the map and loaded it correctly! */
	SendMapOk();

	/* New company/spectator (invalid company) or company we want to join is not active
	 * Switch local company to spectator and await the server's judgement */
	if (_network_join_as == COMPANY_NEW_COMPANY || !Company::IsValidID(_network_join_as)) {
		SetLocalCompany(COMPANY_SPECTATOR);

		if (_network_join_as != COMPANY_SPECTATOR) {
			/* We have arrived and ready to start playing; send a command to make a new company;
			 * the server will give us a client-id and let us in */
			_network_join_status = NETWORK_JOIN_STATUS_REGISTERING;
			ShowJoinStatusWindow();
			NetworkSend_Command(0, 0, 0, CMD_COMPANY_CTRL, NULL, NULL, _local_company);
		}
	} else {
		/* take control over an existing company */
		SetLocalCompany(_network_join_as);
	}

	return NETWORK_RECV_STATUS_OKAY;
}

DEF_GAME_RECEIVE_COMMAND(Client, PACKET_SERVER_FRAME)
{
	if (this->status != STATUS_ACTIVE) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	_frame_counter_server = p->Recv_uint32();
	_frame_counter_max = p->Recv_uint32();
#ifdef ENABLE_NETWORK_SYNC_EVERY_FRAME
	/* Test if the server supports this option
	 *  and if we are at the frame the server is */
	if (p->pos < p->size) {
		_sync_frame = _frame_counter_server;
		_sync_seed_1 = p->Recv_uint32();
#ifdef NETWORK_SEND_DOUBLE_SEED
		_sync_seed_2 = p->Recv_uint32();
#endif
	}
#endif
	DEBUG(net, 5, "Received FRAME %d", _frame_counter_server);

	/* Let the server know that we received this frame correctly
	 *  We do this only once per day, to save some bandwidth ;) */
	if (!_network_first_time && last_ack_frame < _frame_counter) {
		last_ack_frame = _frame_counter + DAY_TICKS;
		DEBUG(net, 4, "Sent ACK at %d", _frame_counter);
		SendAck();
	}

	return NETWORK_RECV_STATUS_OKAY;
}

DEF_GAME_RECEIVE_COMMAND(Client, PACKET_SERVER_SYNC)
{
	if (this->status != STATUS_ACTIVE) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	_sync_frame = p->Recv_uint32();
	_sync_seed_1 = p->Recv_uint32();
#ifdef NETWORK_SEND_DOUBLE_SEED
	_sync_seed_2 = p->Recv_uint32();
#endif

	return NETWORK_RECV_STATUS_OKAY;
}

DEF_GAME_RECEIVE_COMMAND(Client, PACKET_SERVER_COMMAND)
{
	if (this->status != STATUS_ACTIVE) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	CommandPacket cp;
	const char *err = this->Recv_Command(p, &cp);
	cp.frame    = p->Recv_uint32();
	cp.my_cmd   = p->Recv_bool();

	if (err != NULL) {
		IConsolePrintF(CC_ERROR, "WARNING: %s from server, dropping...", err);
		return NETWORK_RECV_STATUS_MALFORMED_PACKET;
	}

	this->incoming_queue.Append(&cp);

	return NETWORK_RECV_STATUS_OKAY;
}

DEF_GAME_RECEIVE_COMMAND(Client, PACKET_SERVER_CHAT)
{
	if (this->status != STATUS_ACTIVE) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	char name[NETWORK_NAME_LENGTH], msg[NETWORK_CHAT_LENGTH];
	const NetworkClientInfo *ci = NULL, *ci_to;

	NetworkAction action = (NetworkAction)p->Recv_uint8();
	ClientID client_id = (ClientID)p->Recv_uint32();
	bool self_send = p->Recv_bool();
	p->Recv_string(msg, NETWORK_CHAT_LENGTH);
	int64 data = p->Recv_uint64();

	ci_to = NetworkFindClientInfoFromClientID(client_id);
	if (ci_to == NULL) return NETWORK_RECV_STATUS_OKAY;

	/* Did we initiate the action locally? */
	if (self_send) {
		switch (action) {
			case NETWORK_ACTION_CHAT_CLIENT:
				/* For speaking to client we need the client-name */
				snprintf(name, sizeof(name), "%s", ci_to->client_name);
				ci = NetworkFindClientInfoFromClientID(_network_own_client_id);
				break;

			/* For speaking to company or giving money, we need the company-name */
			case NETWORK_ACTION_GIVE_MONEY:
				if (!Company::IsValidID(ci_to->client_playas)) return NETWORK_RECV_STATUS_OKAY;
				/* FALL THROUGH */
			case NETWORK_ACTION_CHAT_COMPANY: {
				StringID str = Company::IsValidID(ci_to->client_playas) ? STR_COMPANY_NAME : STR_NETWORK_SPECTATORS;
				SetDParam(0, ci_to->client_playas);

				GetString(name, str, lastof(name));
				ci = NetworkFindClientInfoFromClientID(_network_own_client_id);
				break;
			}

			default: return NETWORK_RECV_STATUS_MALFORMED_PACKET;
		}
	} else {
		/* Display message from somebody else */
		snprintf(name, sizeof(name), "%s", ci_to->client_name);
		ci = ci_to;
	}

	if (ci != NULL) {
		NetworkTextMessage(action, (ConsoleColour)GetDrawStringCompanyColour(ci->client_playas), self_send, name, msg, data);
	}
	return NETWORK_RECV_STATUS_OKAY;
}

DEF_GAME_RECEIVE_COMMAND(Client, PACKET_SERVER_ERROR_QUIT)
{
	if (this->status < STATUS_AUTHORIZED) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	ClientID client_id = (ClientID)p->Recv_uint32();

	NetworkClientInfo *ci = NetworkFindClientInfoFromClientID(client_id);
	if (ci != NULL) {
		NetworkTextMessage(NETWORK_ACTION_LEAVE, CC_DEFAULT, false, ci->client_name, NULL, GetNetworkErrorMsg((NetworkErrorCode)p->Recv_uint8()));
		delete ci;
	}

	SetWindowDirty(WC_CLIENT_LIST, 0);

	return NETWORK_RECV_STATUS_OKAY;
}

DEF_GAME_RECEIVE_COMMAND(Client, PACKET_SERVER_QUIT)
{
	if (this->status < STATUS_AUTHORIZED) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	ClientID client_id = (ClientID)p->Recv_uint32();

	NetworkClientInfo *ci = NetworkFindClientInfoFromClientID(client_id);
	if (ci != NULL) {
		NetworkTextMessage(NETWORK_ACTION_LEAVE, CC_DEFAULT, false, ci->client_name, NULL, STR_NETWORK_MESSAGE_CLIENT_LEAVING);
		delete ci;
	} else {
		DEBUG(net, 0, "Unknown client (%d) is leaving the game", client_id);
	}

	SetWindowDirty(WC_CLIENT_LIST, 0);

	/* If we come here it means we could not locate the client.. strange :s */
	return NETWORK_RECV_STATUS_OKAY;
}

DEF_GAME_RECEIVE_COMMAND(Client, PACKET_SERVER_JOIN)
{
	if (this->status < STATUS_AUTHORIZED) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	ClientID client_id = (ClientID)p->Recv_uint32();

	NetworkClientInfo *ci = NetworkFindClientInfoFromClientID(client_id);
	if (ci != NULL) {
		NetworkTextMessage(NETWORK_ACTION_JOIN, CC_DEFAULT, false, ci->client_name);
	}

	SetWindowDirty(WC_CLIENT_LIST, 0);

	return NETWORK_RECV_STATUS_OKAY;
}

DEF_GAME_RECEIVE_COMMAND(Client, PACKET_SERVER_SHUTDOWN)
{
	/* Only when we're trying to join we really
	 * care about the server shutting down. */
	if (this->status >= STATUS_JOIN) {
		_switch_mode_errorstr = STR_NETWORK_MESSAGE_SERVER_SHUTDOWN;
	}

	return NETWORK_RECV_STATUS_SERVER_ERROR;
}

DEF_GAME_RECEIVE_COMMAND(Client, PACKET_SERVER_NEWGAME)
{
	/* Only when we're trying to join we really
	 * care about the server shutting down. */
	if (this->status >= STATUS_JOIN) {
		/* To trottle the reconnects a bit, every clients waits its
		 * Client ID modulo 16. This way reconnects should be spread
		 * out a bit. */
		_network_reconnect = _network_own_client_id % 16;
		_switch_mode_errorstr = STR_NETWORK_MESSAGE_SERVER_REBOOT;
	}

	return NETWORK_RECV_STATUS_SERVER_ERROR;
}

DEF_GAME_RECEIVE_COMMAND(Client, PACKET_SERVER_RCON)
{
	if (this->status < STATUS_AUTHORIZED) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	char rcon_out[NETWORK_RCONCOMMAND_LENGTH];

	ConsoleColour colour_code = (ConsoleColour)p->Recv_uint16();
	p->Recv_string(rcon_out, sizeof(rcon_out));

	IConsolePrint(colour_code, rcon_out);

	return NETWORK_RECV_STATUS_OKAY;
}

DEF_GAME_RECEIVE_COMMAND(Client, PACKET_SERVER_MOVE)
{
	if (this->status < STATUS_AUTHORIZED) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	/* Nothing more in this packet... */
	ClientID client_id   = (ClientID)p->Recv_uint32();
	CompanyID company_id = (CompanyID)p->Recv_uint8();

	if (client_id == 0) {
		/* definitely an invalid client id, debug message and do nothing. */
		DEBUG(net, 0, "[move] received invalid client index = 0");
		return NETWORK_RECV_STATUS_MALFORMED_PACKET;
	}

	const NetworkClientInfo *ci = NetworkFindClientInfoFromClientID(client_id);
	/* Just make sure we do not try to use a client_index that does not exist */
	if (ci == NULL) return NETWORK_RECV_STATUS_OKAY;

	/* if not valid player, force spectator, else check player exists */
	if (!Company::IsValidID(company_id)) company_id = COMPANY_SPECTATOR;

	if (client_id == _network_own_client_id) {
		SetLocalCompany(company_id);
	}

	return NETWORK_RECV_STATUS_OKAY;
}

DEF_GAME_RECEIVE_COMMAND(Client, PACKET_SERVER_CONFIG_UPDATE)
{
	if (this->status < STATUS_ACTIVE) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	_network_server_max_companies = p->Recv_uint8();
	_network_server_max_spectators = p->Recv_uint8();

	return NETWORK_RECV_STATUS_OKAY;
}

DEF_GAME_RECEIVE_COMMAND(Client, PACKET_SERVER_COMPANY_UPDATE)
{
	if (this->status < STATUS_ACTIVE) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	_network_company_passworded = p->Recv_uint16();
	SetWindowClassesDirty(WC_COMPANY);

	return NETWORK_RECV_STATUS_OKAY;
}

/* Is called after a client is connected to the server */
void NetworkClient_Connected()
{
	/* Set the frame-counter to 0 so nothing happens till we are ready */
	_frame_counter = 0;
	_frame_counter_server = 0;
	last_ack_frame = 0;
	/* Request the game-info */
	MyClient::SendJoin();
}

void NetworkClientSendRcon(const char *password, const char *command)
{
	MyClient::SendRCon(password, command);
}

/**
 * Notify the server of this client wanting to be moved to another company.
 * @param company_id id of the company the client wishes to be moved to.
 * @param pass the password, is only checked on the server end if a password is needed.
 * @return void
 */
void NetworkClientRequestMove(CompanyID company_id, const char *pass)
{
	MyClient::SendMove(company_id, pass);
}

void NetworkClientsToSpectators(CompanyID cid)
{
	/* If our company is changing owner, go to spectators */
	if (cid == _local_company) SetLocalCompany(COMPANY_SPECTATOR);

	NetworkClientInfo *ci;
	FOR_ALL_CLIENT_INFOS(ci) {
		if (ci->client_playas != cid) continue;
		NetworkTextMessage(NETWORK_ACTION_COMPANY_SPECTATOR, CC_DEFAULT, false, ci->client_name);
		ci->client_playas = COMPANY_SPECTATOR;
	}
}

void NetworkUpdateClientName()
{
	NetworkClientInfo *ci = NetworkFindClientInfoFromClientID(_network_own_client_id);

	if (ci == NULL) return;

	/* Don't change the name if it is the same as the old name */
	if (strcmp(ci->client_name, _settings_client.network.client_name) != 0) {
		if (!_network_server) {
			MyClient::SendSetName(_settings_client.network.client_name);
		} else {
			if (NetworkFindName(_settings_client.network.client_name)) {
				NetworkTextMessage(NETWORK_ACTION_NAME_CHANGE, CC_DEFAULT, false, ci->client_name, _settings_client.network.client_name);
				strecpy(ci->client_name, _settings_client.network.client_name, lastof(ci->client_name));
				NetworkUpdateClientInfo(CLIENT_ID_SERVER);
			}
		}
	}
}

void NetworkClientSendChat(NetworkAction action, DestType type, int dest, const char *msg, int64 data)
{
	MyClient::SendChat(action, type, dest, msg, data);
}

static void NetworkClientSetPassword(const char *password)
{
	MyClient::SendSetPassword(password);
}

/**
 * Tell whether the client has team members where he/she can chat to.
 * @param cio client to check members of.
 * @return true if there is at least one team member.
 */
bool NetworkClientPreferTeamChat(const NetworkClientInfo *cio)
{
	/* Only companies actually playing can speak to team. Eg spectators cannot */
	if (!_settings_client.gui.prefer_teamchat || !Company::IsValidID(cio->client_playas)) return false;

	const NetworkClientInfo *ci;
	FOR_ALL_CLIENT_INFOS(ci) {
		if (ci->client_playas == cio->client_playas && ci != cio) return true;
	}

	return false;
}

/**
 * Sets/resets company password
 * @param password new password, "" or "*" resets password
 * @return new password
 */
const char *NetworkChangeCompanyPassword(const char *password)
{
	if (strcmp(password, "*") == 0) password = "";

	if (!_network_server) {
		NetworkClientSetPassword(password);
	} else {
		HashCurrentCompanyPassword(password);
	}

	return password;
}

/**
 * Check if max_companies has been reached on the server (local check only).
 * @return true if the max value has been reached or exceeded, false otherwise.
 */
bool NetworkMaxCompaniesReached()
{
	return Company::GetNumItems() >= (_network_server ? _settings_client.network.max_companies : _network_server_max_companies);
}

/**
 * Check if max_spectatos has been reached on the server (local check only).
 * @return true if the max value has been reached or exceeded, false otherwise.
 */
bool NetworkMaxSpectatorsReached()
{
	return NetworkSpectatorCount() >= (_network_server ? _settings_client.network.max_spectators : _network_server_max_spectators);
}

/**
 * Print all the clients to the console
 */
void NetworkPrintClients()
{
	NetworkClientInfo *ci;
	FOR_ALL_CLIENT_INFOS(ci) {
		IConsolePrintF(CC_INFO, "Client #%1d  name: '%s'  company: %1d  IP: %s",
				ci->client_id,
				ci->client_name,
				ci->client_playas + (Company::IsValidID(ci->client_playas) ? 1 : 0),
				GetClientIP(ci));
	}
}

#endif /* ENABLE_NETWORK */
