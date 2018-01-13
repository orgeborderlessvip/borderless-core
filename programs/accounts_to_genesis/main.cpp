/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <iterator>

#include <fc/io/json.hpp>
#include <fc/io/stdio.hpp>
#include <fc/network/http/server.hpp>
#include <fc/network/http/websocket.hpp>
#include <fc/rpc/cli.hpp>
#include <fc/rpc/http_api.hpp>
#include <fc/rpc/websocket_api.hpp>
#include <fc/smart_ref_impl.hpp>

#include <graphene/app/api.hpp>
#include <graphene/chain/protocol/protocol.hpp>
#include <graphene/egenesis/egenesis.hpp>
#include <graphene/utilities/key_conversion.hpp>
#include <graphene/wallet/wallet.hpp>

#include <fc/interprocess/signals.hpp>
#include <boost/program_options.hpp>

#include <fc/log/console_appender.hpp>
#include <fc/log/file_appender.hpp>
#include <fc/log/logger.hpp>
#include <fc/log/logger_config.hpp>

#ifdef WIN32
# include <signal.h>
#else
# include <csignal>
#endif

using namespace graphene::app;
using namespace graphene::chain;
using namespace graphene::utilities;
using namespace graphene::wallet;
using namespace std;
namespace bpo = boost::program_options;


bool getGenesisData(genesis_state_type &genesisData,const bpo::variables_map & options)
{
	//const string genesisPath = options.count("genesis-json-path") ? options.at("genesis-json-path").as<string>() : "genesis.json";
	const string genesisPath = options.at("genesis-json-path").as<string>();
	fc::path genesisi_file(genesisPath);
	if (fc::exists(genesisi_file))
	{
		genesisData = fc::json::from_file(genesisi_file).as<genesis_state_type>();
		if (!options.count("append"))
		{
			genesisData.initial_accounts.clear();
			genesisData.initial_balances.clear();
		}
		return true;
	}
	cout << genesisPath << " is not exists" << endl;
	return false;
}

void updateGenesisData(genesis_state_type &genesisData,const fc::api<database_api>& database_api,const bpo::variables_map & options)
{
	uint64_t account_count = database_api->get_account_count();
	auto nameMap = database_api->lookup_accounts("", account_count);
	vector<string> nameArray;
	for (auto iter : nameMap)
	{
		nameArray.push_back(iter.first);
	}

	auto userInfoArray = database_api->get_full_accounts(nameArray, false);
	const bool isDebug = options.count("debug") ? true : false;
	for (auto iter : userInfoArray)
	{
		if (iter.second.account.get_id().instance.value < 6)/*to do */
		{
			continue;
		}
		genesis_state_type::initial_account_type init_account;
		init_account.name = iter.first;
		auto ownerPublicKey = iter.second.account.owner.key_auths.begin()->first;
		init_account.owner_key = ownerPublicKey;
		auto activePublicKey = iter.second.account.active.key_auths.begin()->first;
		init_account.active_key = activePublicKey;
		init_account.is_lifetime_member = iter.second.account.is_lifetime_member();
		genesisData.initial_accounts.push_back(init_account);
		if (isDebug)
		{
			cout << iter.first << endl;
			cout << "owner_key: " << (string)ownerPublicKey << endl;
			cout << "active_key: " << (string)activePublicKey << endl;
			cout << "is life member: " << init_account.is_lifetime_member << endl;
		}
		for (auto iterBalances : iter.second.balances)
		{
			if (iterBalances.balance == 0)
			{
				continue;
			}
			genesis_state_type::initial_balance_type init_balance;
			init_balance.owner = graphene::chain::address(ownerPublicKey);
			vector<asset_id_type>  asset_ids;
			asset_ids.push_back(iterBalances.asset_type);
			auto assertObjVector = database_api->get_assets(asset_ids);
			init_balance.asset_symbol = assertObjVector.front()->symbol;
			init_balance.amount = iterBalances.balance;
			genesisData.initial_balances.push_back(init_balance);
			if (isDebug)
			{
				cout << "address :" << graphene::chain::address(ownerPublicKey).operator fc::string() << endl;
				cout << "asset_symbol: " << assertObjVector.front()->symbol << endl;
				cout << "amount: " << iterBalances.balance.value << endl;
			}
		}
		if (isDebug)
		{
			cout << endl << endl << endl;
		}
	}
}

int main( int argc, char** argv )
{
   try {
	  bpo::variables_map options;
	  boost::program_options::options_description opts;
	  opts.add_options()
			("help,h", "Print this help message and exit.")
			("server-rpc-endpoint,s", bpo::value<string>()->implicit_value("ws://127.0.0.1:8090"), "Server websocket RPC endpoint")
			("genesis-json-path,g", bpo::value<string>(), "genesis json path")
			("output-json-path,o", bpo::value<string>(), "output json path")
			("server-rpc-user,u", bpo::value<string>(), "Server Username")
			("server-rpc-password,p", bpo::value<string>(), "Server Password")
			("append,a","output with append mode")
		    ("debug,d","print all account info");
	  bpo::store(bpo::parse_command_line(argc, argv, opts), options);

	  if (options.count("help") || options.empty())
	  {
		  cout << opts << endl;
		  return 0;
	  }
	 
	  /*获取原始数据*/
	  genesis_state_type genesisData;
	  if(!getGenesisData(genesisData, options)) 
	  {
		  return -1;
	  }

	  /*获取database api*/
      fc::http::websocket_client client;
	  const string serverUrl = options.at("server-rpc-endpoint").as<string>();
      auto con  = client.connect(serverUrl);
      auto apic = std::make_shared<fc::rpc::websocket_api_connection>(*con);

      auto remote_api = apic->get_remote_api< login_api >(1);
	  const string user = options.count("server-rpc-user") ? options.at("server-rpc-user").as<string>() : "";
	  const string password = options.count("server-rpc-password") ? options.at("server-rpc-password").as<string>() : "";
      remote_api->login(user,password);
	  fc::api<database_api> database_api = remote_api->database();

	  /*更新genesis json*/
	  updateGenesisData(genesisData, database_api, options);
	 
	  /*输出*/
	  //const string outputPath = options.count("output-json-path") ? options.at("output-json-path").as<string>() : "output.json";
	  const string outputPath =  options.at("output-json-path").as<string>();
	  fc::json::save_to_file(genesisData, outputPath);
   }
   catch ( const fc::exception& e )
   {
      std::cout << e.to_detail_string() << "\n";
      return -1;
   }
   return 0;
}
