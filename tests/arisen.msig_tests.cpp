#include <boost/test/unit_test.hpp>
#include <arisen/testing/tester.hpp>
#include <arisen/chain/abi_serializer.hpp>
#include <arisen/chain/wast_to_wasm.hpp>

#include <Runtime/Runtime.h>

#include <fc/variant_object.hpp>
#include "contracts.hpp"
#include "test_symbol.hpp"

using namespace arisen::testing;
using namespace arisen;
using namespace arisen::chain;
using namespace arisen::testing;
using namespace fc;

using mvo = fc::mutable_variant_object;

class arisen_msig_tester : public tester {
public:
   arisen_msig_tester() {
      create_accounts( { N(arisen.msig), N(arisen.stake), N(arisen.ram), N(arisen.rfee), N(alice), N(bob), N(carol) } );
      produce_block();

      auto trace = base_tester::push_action(config::system_account_name, N(setpriv),
                                            config::system_account_name,  mutable_variant_object()
                                            ("account", "arisen.msig")
                                            ("is_priv", 1)
      );

      set_code( N(arisen.msig), contracts::msig_wasm() );
      set_abi( N(arisen.msig), contracts::msig_abi().data() );

      produce_blocks();
      const auto& accnt = control->db().get<account_object,by_name>( N(arisen.msig) );
      abi_def abi;
      BOOST_REQUIRE_EQUAL(abi_serializer::to_abi(accnt.abi, abi), true);
      abi_ser.set_abi(abi, abi_serializer_max_time);
   }

   transaction_trace_ptr create_account_with_resources( account_name a, account_name creator, asset ramfunds, bool multisig,
                                                        asset net = core_sym::from_string("10.0000"), asset cpu = core_sym::from_string("10.0000") ) {
      signed_transaction trx;
      set_transaction_headers(trx);

      authority owner_auth;
      if (multisig) {
         // multisig between account's owner key and creators active permission
         owner_auth = authority(2, {key_weight{get_public_key( a, "owner" ), 1}}, {permission_level_weight{{creator, config::active_name}, 1}});
      } else {
         owner_auth =  authority( get_public_key( a, "owner" ) );
      }

      trx.actions.emplace_back( vector<permission_level>{{creator,config::active_name}},
                                newaccount{
                                   .creator  = creator,
                                   .name     = a,
                                   .owner    = owner_auth,
                                   .active   = authority( get_public_key( a, "active" ) )
                                });

      trx.actions.emplace_back( get_action( N(arisen), N(buyram), vector<permission_level>{{creator,config::active_name}},
                                            mvo()
                                            ("payer", creator)
                                            ("receiver", a)
                                            ("quant", ramfunds) )
                              );

      trx.actions.emplace_back( get_action( N(arisen), N(delegatebw), vector<permission_level>{{creator,config::active_name}},
                                            mvo()
                                            ("from", creator)
                                            ("receiver", a)
                                            ("stake_net_quantity", net )
                                            ("stake_cpu_quantity", cpu )
                                            ("transfer", 0 )
                                          )
                                );

      set_transaction_headers(trx);
      trx.sign( get_private_key( creator, "active" ), control->get_chain_id()  );
      return push_transaction( trx );
   }
   void create_currency( name contract, name manager, asset maxsupply ) {
      auto act =  mutable_variant_object()
         ("issuer",       manager )
         ("maximum_supply", maxsupply );

      base_tester::push_action(contract, N(create), contract, act );
   }
   void issue( name to, const asset& amount, name manager = config::system_account_name ) {
      base_tester::push_action( N(arisen.token), N(issue), manager, mutable_variant_object()
                                ("to",      to )
                                ("quantity", amount )
                                ("memo", "")
                                );
   }
   void transfer( name from, name to, const string& amount, name manager = config::system_account_name ) {
      base_tester::push_action( N(arisen.token), N(transfer), manager, mutable_variant_object()
                                ("from",    from)
                                ("to",      to )
                                ("quantity", asset::from_string(amount) )
                                ("memo", "")
                                );
   }
   asset get_balance( const account_name& act ) {
      //return get_currency_balance( config::system_account_name, symbol(CORE_SYMBOL), act );
      //temporary code. current get_currency_balancy uses table name N(accounts) from currency.h
      //generic_currency table name is N(account).
      const auto& db  = control->db();
      const auto* tbl = db.find<table_id_object, by_code_scope_table>(boost::make_tuple(N(arisen.token), act, N(accounts)));
      share_type result = 0;

      // the balance is implied to be 0 if either the table or row does not exist
      if (tbl) {
         const auto *obj = db.find<key_value_object, by_scope_primary>(boost::make_tuple(tbl->id, symbol(CORE_SYM).to_symbol_code()));
         if (obj) {
            // balance is the first field in the serialization
            fc::datastream<const char *> ds(obj->value.data(), obj->value.size());
            fc::raw::unpack(ds, result);
         }
      }
      return asset( result, symbol(CORE_SYM) );
   }

   transaction_trace_ptr push_action( const account_name& signer, const action_name& name, const variant_object& data, bool auth = true ) {
      vector<account_name> accounts;
      if( auth )
         accounts.push_back( signer );
      auto trace = base_tester::push_action( N(arisen.msig), name, accounts, data );
      produce_block();
      BOOST_REQUIRE_EQUAL( true, chain_has_transaction(trace->id) );
      return trace;

      /*
         string action_type_name = abi_ser.get_action_type(name);

         action act;
         act.account = N(arisen.msig);
         act.name = name;
         act.data = abi_ser.variant_to_binary( action_type_name, data, abi_serializer_max_time );
         //std::cout << "test:\n" << fc::to_hex(act.data.data(), act.data.size()) << " size = " << act.data.size() << std::endl;

         return base_tester::push_action( std::move(act), auth ? uint64_t(signer) : 0 );
      */
   }

   transaction reqauth( account_name from, const vector<permission_level>& auths, const fc::microseconds& max_serialization_time );

   abi_serializer abi_ser;
};

transaction arisen_msig_tester::reqauth( account_name from, const vector<permission_level>& auths, const fc::microseconds& max_serialization_time ) {
   fc::variants v;
   for ( auto& level : auths ) {
      v.push_back(fc::mutable_variant_object()
                  ("actor", level.actor)
                  ("permission", level.permission)
      );
   }
   variant pretty_trx = fc::mutable_variant_object()
      ("expiration", "2020-01-01T00:30")
      ("ref_block_num", 2)
      ("ref_block_prefix", 3)
      ("max_net_usage_words", 0)
      ("max_cpu_usage_ms", 0)
      ("delay_sec", 0)
      ("actions", fc::variants({
            fc::mutable_variant_object()
               ("account", name(config::system_account_name))
               ("name", "reqauth")
               ("authorization", v)
               ("data", fc::mutable_variant_object() ("from", from) )
               })
      );
   transaction trx;
   abi_serializer::from_variant(pretty_trx, trx, get_resolver(), max_serialization_time);
   return trx;
}

BOOST_AUTO_TEST_SUITE(arisen_msig_tests)

BOOST_FIXTURE_TEST_CASE( propose_approve_execute, arisen_msig_tester ) try {
   auto trx = reqauth("alice", {permission_level{N(alice), config::active_name}}, abi_serializer_max_time );

   push_action( N(alice), N(propose), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", vector<permission_level>{{ N(alice), config::active_name }})
   );

   //fail to execute before approval
   BOOST_REQUIRE_EXCEPTION( push_action( N(alice), N(exec), mvo()
                                          ("proposer",      "alice")
                                          ("proposal_name", "first")
                                          ("executer",      "alice")
                            ),
                            arisen_assert_message_exception,
                            arisen_assert_message_is("transaction authorization failed")
   );

   //approve and execute
   push_action( N(alice), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(alice), config::active_name })
   );

   transaction_trace_ptr trace;
   control->applied_transaction.connect(
   [&]( std::tuple<const transaction_trace_ptr&, const signed_transaction&> p ) {
      const auto& t = std::get<0>(p);
      if( t->scheduled ) { trace = t; }
   } );
   push_action( N(alice), N(exec), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("executer",      "alice")
   );

   BOOST_REQUIRE( bool(trace) );
   BOOST_REQUIRE_EQUAL( 1, trace->action_traces.size() );
   BOOST_REQUIRE_EQUAL( transaction_receipt::executed, trace->receipt->status );
} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( propose_approve_unapprove, arisen_msig_tester ) try {
   auto trx = reqauth("alice", {permission_level{N(alice), config::active_name}}, abi_serializer_max_time );

   push_action( N(alice), N(propose), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", vector<permission_level>{{ N(alice), config::active_name }})
   );

   push_action( N(alice), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(alice), config::active_name })
   );

   push_action( N(alice), N(unapprove), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(alice), config::active_name })
   );

   BOOST_REQUIRE_EXCEPTION( push_action( N(alice), N(exec), mvo()
                                          ("proposer",      "alice")
                                          ("proposal_name", "first")
                                          ("executer",      "alice")
                            ),
                            arisen_assert_message_exception,
                            arisen_assert_message_is("transaction authorization failed")
   );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( propose_approve_by_two, arisen_msig_tester ) try {
   auto trx = reqauth("alice", vector<permission_level>{ { N(alice), config::active_name }, { N(bob), config::active_name } }, abi_serializer_max_time );
   push_action( N(alice), N(propose), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", vector<permission_level>{ { N(alice), config::active_name }, { N(bob), config::active_name } })
   );

   //approve by alice
   push_action( N(alice), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(alice), config::active_name })
   );

   //fail because approval by bob is missing

   BOOST_REQUIRE_EXCEPTION( push_action( N(alice), N(exec), mvo()
                                          ("proposer",      "alice")
                                          ("proposal_name", "first")
                                          ("executer",      "alice")
                            ),
                            arisen_assert_message_exception,
                            arisen_assert_message_is("transaction authorization failed")
   );

   //approve by bob and execute
   push_action( N(bob), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(bob), config::active_name })
   );

   transaction_trace_ptr trace;
   control->applied_transaction.connect(
   [&]( std::tuple<const transaction_trace_ptr&, const signed_transaction&> p ) {
      const auto& t = std::get<0>(p);
      if( t->scheduled ) { trace = t; }
   } );

   push_action( N(alice), N(exec), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("executer",      "alice")
   );

   BOOST_REQUIRE( bool(trace) );
   BOOST_REQUIRE_EQUAL( 1, trace->action_traces.size() );
   BOOST_REQUIRE_EQUAL( transaction_receipt::executed, trace->receipt->status );
} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( propose_with_wrong_requested_auth, arisen_msig_tester ) try {
   auto trx = reqauth("alice", vector<permission_level>{ { N(alice), config::active_name },  { N(bob), config::active_name } }, abi_serializer_max_time );
   //try with not enough requested auth
   BOOST_REQUIRE_EXCEPTION( push_action( N(alice), N(propose), mvo()
                                             ("proposer",      "alice")
                                             ("proposal_name", "third")
                                             ("trx",           trx)
                                             ("requested", vector<permission_level>{ { N(alice), config::active_name } } )
                            ),
                            arisen_assert_message_exception,
                            arisen_assert_message_is("transaction authorization failed")
   );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( big_transaction, arisen_msig_tester ) try {
   vector<permission_level> perm = { { N(alice), config::active_name }, { N(bob), config::active_name } };
   auto wasm = contracts::util::exchange_wasm();

   variant pretty_trx = fc::mutable_variant_object()
      ("expiration", "2020-01-01T00:30")
      ("ref_block_num", 2)
      ("ref_block_prefix", 3)
      ("max_net_usage_words", 0)
      ("max_cpu_usage_ms", 0)
      ("delay_sec", 0)
      ("actions", fc::variants({
            fc::mutable_variant_object()
               ("account", name(config::system_account_name))
               ("name", "setcode")
               ("authorization", perm)
               ("data", fc::mutable_variant_object()
                ("account", "alice")
                ("vmtype", 0)
                ("vmversion", 0)
                ("code", bytes( wasm.begin(), wasm.end() ))
               )
               })
      );

   transaction trx;
   abi_serializer::from_variant(pretty_trx, trx, get_resolver(), abi_serializer_max_time);

   push_action( N(alice), N(propose), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", perm)
   );

   //approve by alice
   push_action( N(alice), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(alice), config::active_name })
   );
   //approve by bob and execute
   push_action( N(bob), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(bob), config::active_name })
   );

   transaction_trace_ptr trace;
   control->applied_transaction.connect(
   [&]( std::tuple<const transaction_trace_ptr&, const signed_transaction&> p ) {
      const auto& t = std::get<0>(p);
      if( t->scheduled ) { trace = t; }
   } );

   push_action( N(alice), N(exec), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("executer",      "alice")
   );

   BOOST_REQUIRE( bool(trace) );
   BOOST_REQUIRE_EQUAL( 1, trace->action_traces.size() );
   BOOST_REQUIRE_EQUAL( transaction_receipt::executed, trace->receipt->status );
} FC_LOG_AND_RETHROW()



BOOST_FIXTURE_TEST_CASE( update_system_contract_all_approve, arisen_msig_tester ) try {

   // required to set up the link between (arisen active) and (arisen.prods active)
   //
   //                  arisen active
   //                       |
   //             arisen.prods active (2/3 threshold)
   //             /         |        \             <--- implicitly updated in onblock action
   // alice active     bob active   carol active

   set_authority(N(arisen), "active", authority(1,
      vector<key_weight>{{get_private_key("arisen", "active").get_public_key(), 1}},
      vector<permission_level_weight>{{{N(arisen.prods), config::active_name}, 1}}), "owner",
      { { N(arisen), "active" } }, { get_private_key( N(arisen), "active" ) });

   set_producers( {N(alice),N(bob),N(carol)} );
   produce_blocks(50);

   create_accounts( { N(arisen.token), N(arisen.com) } );
   set_code( N(arisen.token), contracts::token_wasm() );
   set_abi( N(arisen.token), contracts::token_abi().data() );

   create_currency( N(arisen.token), config::system_account_name, core_sym::from_string("10000000000.0000") );
   issue(config::system_account_name, core_sym::from_string("1000000000.0000"));
   BOOST_REQUIRE_EQUAL( core_sym::from_string("1000000000.0000"),
                        get_balance("arisen") + get_balance("arisen.rfee") + get_balance("arisen.stake") + get_balance("arisen.ram") );

   set_code( config::system_account_name, contracts::system_wasm() );
   set_abi( config::system_account_name, contracts::system_abi().data() );
   base_tester::push_action( config::system_account_name, N(init),
                             config::system_account_name,  mutable_variant_object()
                              ("version", 0)
                              ("core", CORE_SYM_STR)
   );
   produce_blocks();
   create_account_with_resources( N(alice1111111), N(arisen), core_sym::from_string("1.0000"), false );
   create_account_with_resources( N(bob111111111), N(arisen), core_sym::from_string("0.4500"), false );
   create_account_with_resources( N(carol1111111), N(arisen), core_sym::from_string("1.0000"), false );

   BOOST_REQUIRE_EQUAL( core_sym::from_string("1000000000.0000"),
                        get_balance("arisen") + get_balance("arisen.rfee") + get_balance("arisen.stake") + get_balance("arisen.ram") );

   vector<permission_level> perm = { { N(alice), config::active_name }, { N(bob), config::active_name },
      {N(carol), config::active_name} };

   vector<permission_level> action_perm = {{N(arisen), config::active_name}};

   auto wasm = contracts::util::reject_all_wasm();

   variant pretty_trx = fc::mutable_variant_object()
      ("expiration", "2020-01-01T00:30")
      ("ref_block_num", 2)
      ("ref_block_prefix", 3)
      ("max_net_usage_words", 0)
      ("max_cpu_usage_ms", 0)
      ("delay_sec", 0)
      ("actions", fc::variants({
            fc::mutable_variant_object()
               ("account", name(config::system_account_name))
               ("name", "setcode")
               ("authorization", action_perm)
               ("data", fc::mutable_variant_object()
                ("account", name(config::system_account_name))
                ("vmtype", 0)
                ("vmversion", 0)
                ("code", bytes( wasm.begin(), wasm.end() ))
               )
               })
      );

   transaction trx;
   abi_serializer::from_variant(pretty_trx, trx, get_resolver(), abi_serializer_max_time);

   // propose action
   push_action( N(alice), N(propose), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", perm)
   );

   //approve by alice
   push_action( N(alice), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(alice), config::active_name })
   );
   //approve by bob
   push_action( N(bob), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(bob), config::active_name })
   );
   //approve by carol
   push_action( N(carol), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(carol), config::active_name })
   );
   // execute by alice to replace the arisen system contract
   transaction_trace_ptr trace;
   control->applied_transaction.connect(
   [&]( std::tuple<const transaction_trace_ptr&, const signed_transaction&> p ) {
      const auto& t = std::get<0>(p);
      if( t->scheduled ) { trace = t; }
   } );

   push_action( N(alice), N(exec), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("executer",      "alice")
   );

   BOOST_REQUIRE( bool(trace) );
   BOOST_REQUIRE_EQUAL( 1, trace->action_traces.size() );
   BOOST_REQUIRE_EQUAL( transaction_receipt::executed, trace->receipt->status );

   // can't create account because system contract was replaced by the reject_all contract

   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( N(alice1111112), N(arisen), core_sym::from_string("1.0000"), false ),
                            arisen_assert_message_exception, arisen_assert_message_is("rejecting all actions")

   );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( update_system_contract_major_approve, arisen_msig_tester ) try {

   // set up the link between (arisen active) and (arisen.prods active)
   set_authority(N(arisen), "active", authority(1,
      vector<key_weight>{{get_private_key("arisen", "active").get_public_key(), 1}},
      vector<permission_level_weight>{{{N(arisen.prods), config::active_name}, 1}}), "owner",
      { { N(arisen), "active" } }, { get_private_key( N(arisen), "active" ) });

   create_accounts( { N(apple) } );
   set_producers( {N(alice),N(bob),N(carol), N(apple)} );
   produce_blocks(50);

   create_accounts( { N(arisen.token), N(arisen.com) } );
   set_code( N(arisen.token), contracts::token_wasm() );
   set_abi( N(arisen.token), contracts::token_abi().data() );

   create_currency( N(arisen.token), config::system_account_name, core_sym::from_string("10000000000.0000") );
   issue(config::system_account_name, core_sym::from_string("1000000000.0000"));
   BOOST_REQUIRE_EQUAL( core_sym::from_string("1000000000.0000"), get_balance( "arisen" ) );

   set_code( config::system_account_name, contracts::system_wasm() );
   set_abi( config::system_account_name, contracts::system_abi().data() );
   base_tester::push_action( config::system_account_name, N(init),
                             config::system_account_name,  mutable_variant_object()
                                 ("version", 0)
                                 ("core", CORE_SYM_STR)
   );
   produce_blocks();

   create_account_with_resources( N(alice1111111), N(arisen), core_sym::from_string("1.0000"), false );
   create_account_with_resources( N(bob111111111), N(arisen), core_sym::from_string("0.4500"), false );
   create_account_with_resources( N(carol1111111), N(arisen), core_sym::from_string("1.0000"), false );

   BOOST_REQUIRE_EQUAL( core_sym::from_string("1000000000.0000"),
                        get_balance("arisen") + get_balance("arisen.rfee") + get_balance("arisen.stake") + get_balance("arisen.ram") );

   vector<permission_level> perm = { { N(alice), config::active_name }, { N(bob), config::active_name },
      {N(carol), config::active_name}, {N(apple), config::active_name}};

   vector<permission_level> action_perm = {{N(arisen), config::active_name}};

   auto wasm = contracts::util::reject_all_wasm();

   variant pretty_trx = fc::mutable_variant_object()
      ("expiration", "2020-01-01T00:30")
      ("ref_block_num", 2)
      ("ref_block_prefix", 3)
      ("max_net_usage_words", 0)
      ("max_cpu_usage_ms", 0)
      ("delay_sec", 0)
      ("actions", fc::variants({
            fc::mutable_variant_object()
               ("account", name(config::system_account_name))
               ("name", "setcode")
               ("authorization", action_perm)
               ("data", fc::mutable_variant_object()
                ("account", name(config::system_account_name))
                ("vmtype", 0)
                ("vmversion", 0)
                ("code", bytes( wasm.begin(), wasm.end() ))
               )
               })
      );

   transaction trx;
   abi_serializer::from_variant(pretty_trx, trx, get_resolver(), abi_serializer_max_time);

   // propose action
   push_action( N(alice), N(propose), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", perm)
   );

   //approve by alice
   push_action( N(alice), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(alice), config::active_name })
   );
   //approve by bob
   push_action( N(bob), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(bob), config::active_name })
   );

   // not enough approvers
   BOOST_REQUIRE_EXCEPTION(
      push_action( N(alice), N(exec), mvo()
                     ("proposer",      "alice")
                     ("proposal_name", "first")
                     ("executer",      "alice")
      ),
      arisen_assert_message_exception, arisen_assert_message_is("transaction authorization failed")
   );

   //approve by apple
   push_action( N(apple), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(apple), config::active_name })
   );
   // execute by alice to replace the arisen system contract
   transaction_trace_ptr trace;
   control->applied_transaction.connect(
   [&]( std::tuple<const transaction_trace_ptr&, const signed_transaction&> p ) {
      const auto& t = std::get<0>(p);
      if( t->scheduled ) { trace = t; }
   } );

   // execute by another producer different from proposer
   push_action( N(apple), N(exec), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("executer",      "apple")
   );

   BOOST_REQUIRE( bool(trace) );
   BOOST_REQUIRE_EQUAL( 1, trace->action_traces.size() );
   BOOST_REQUIRE_EQUAL( transaction_receipt::executed, trace->receipt->status );

   // can't create account because system contract was replaced by the reject_all contract

   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( N(alice1111112), N(arisen), core_sym::from_string("1.0000"), false ),
                            arisen_assert_message_exception, arisen_assert_message_is("rejecting all actions")

   );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( propose_approve_invalidate, arisen_msig_tester ) try {
   auto trx = reqauth("alice", {permission_level{N(alice), config::active_name}}, abi_serializer_max_time );

   push_action( N(alice), N(propose), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", vector<permission_level>{{ N(alice), config::active_name }})
   );

   //fail to execute before approval
   BOOST_REQUIRE_EXCEPTION( push_action( N(alice), N(exec), mvo()
                                          ("proposer",      "alice")
                                          ("proposal_name", "first")
                                          ("executer",      "alice")
                            ),
                            arisen_assert_message_exception,
                            arisen_assert_message_is("transaction authorization failed")
   );

   //approve
   push_action( N(alice), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(alice), config::active_name })
   );

   //invalidate
   push_action( N(alice), N(invalidate), mvo()
                  ("account",      "alice")
   );

   //fail to execute after invalidation
   BOOST_REQUIRE_EXCEPTION( push_action( N(alice), N(exec), mvo()
                                          ("proposer",      "alice")
                                          ("proposal_name", "first")
                                          ("executer",      "alice")
                            ),
                            arisen_assert_message_exception,
                            arisen_assert_message_is("transaction authorization failed")
   );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( propose_invalidate_approve, arisen_msig_tester ) try {
   auto trx = reqauth("alice", {permission_level{N(alice), config::active_name}}, abi_serializer_max_time );

   push_action( N(alice), N(propose), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", vector<permission_level>{{ N(alice), config::active_name }})
   );

   //fail to execute before approval
   BOOST_REQUIRE_EXCEPTION( push_action( N(alice), N(exec), mvo()
                                          ("proposer",      "alice")
                                          ("proposal_name", "first")
                                          ("executer",      "alice")
                            ),
                            arisen_assert_message_exception,
                            arisen_assert_message_is("transaction authorization failed")
   );

   //invalidate
   push_action( N(alice), N(invalidate), mvo()
                  ("account",      "alice")
   );

   //approve
   push_action( N(alice), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(alice), config::active_name })
   );

   //successfully execute
   transaction_trace_ptr trace;
   control->applied_transaction.connect(
   [&]( std::tuple<const transaction_trace_ptr&, const signed_transaction&> p ) {
      const auto& t = std::get<0>(p);
      if( t->scheduled ) { trace = t; }
   } );

   push_action( N(bob), N(exec), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("executer",      "bob")
   );

   BOOST_REQUIRE( bool(trace) );
   BOOST_REQUIRE_EQUAL( 1, trace->action_traces.size() );
   BOOST_REQUIRE_EQUAL( transaction_receipt::executed, trace->receipt->status );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( approve_execute_old, arisen_msig_tester ) try {
   set_code( N(arisen.msig), contracts::util::msig_wasm_old() );
   set_abi( N(arisen.msig), contracts::util::msig_abi_old().data() );
   produce_blocks();

   //propose with old version of arisen.msig
   auto trx = reqauth("alice", {permission_level{N(alice), config::active_name}}, abi_serializer_max_time );
   push_action( N(alice), N(propose), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", vector<permission_level>{{ N(alice), config::active_name }})
   );

   set_code( N(arisen.msig), contracts::msig_wasm() );
   set_abi( N(arisen.msig), contracts::msig_abi().data() );
   produce_blocks();

   //approve and execute with new version
   push_action( N(alice), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(alice), config::active_name })
   );

   transaction_trace_ptr trace;
   control->applied_transaction.connect(
   [&]( std::tuple<const transaction_trace_ptr&, const signed_transaction&> p ) {
      const auto& t = std::get<0>(p);
      if( t->scheduled ) { trace = t; }
   } );

   push_action( N(alice), N(exec), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("executer",      "alice")
   );

   BOOST_REQUIRE( bool(trace) );
   BOOST_REQUIRE_EQUAL( 1, trace->action_traces.size() );
   BOOST_REQUIRE_EQUAL( transaction_receipt::executed, trace->receipt->status );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( approve_unapprove_old, arisen_msig_tester ) try {
   set_code( N(arisen.msig), contracts::util::msig_wasm_old() );
   set_abi( N(arisen.msig), contracts::util::msig_abi_old().data() );
   produce_blocks();

   //propose with old version of arisen.msig
   auto trx = reqauth("alice", {permission_level{N(alice), config::active_name}}, abi_serializer_max_time );
   push_action( N(alice), N(propose), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", vector<permission_level>{{ N(alice), config::active_name }})
   );

   //approve with old version
   push_action( N(alice), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(alice), config::active_name })
   );

   set_code( N(arisen.msig), contracts::msig_wasm() );
   set_abi( N(arisen.msig), contracts::msig_abi().data() );
   produce_blocks();

   //unapprove with old version
   push_action( N(alice), N(unapprove), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(alice), config::active_name })
   );

   BOOST_REQUIRE_EXCEPTION( push_action( N(alice), N(exec), mvo()
                                          ("proposer",      "alice")
                                          ("proposal_name", "first")
                                          ("executer",      "alice")
                            ),
                            arisen_assert_message_exception,
                            arisen_assert_message_is("transaction authorization failed")
   );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( approve_by_two_old, arisen_msig_tester ) try {
   set_code( N(arisen.msig), contracts::util::msig_wasm_old() );
   set_abi( N(arisen.msig), contracts::util::msig_abi_old().data() );
   produce_blocks();

   auto trx = reqauth("alice", vector<permission_level>{ { N(alice), config::active_name }, { N(bob), config::active_name } }, abi_serializer_max_time );
   push_action( N(alice), N(propose), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", vector<permission_level>{ { N(alice), config::active_name }, { N(bob), config::active_name } })
   );

   //approve by alice
   push_action( N(alice), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(alice), config::active_name })
   );

   set_code( N(arisen.msig), contracts::msig_wasm() );
   set_abi( N(arisen.msig), contracts::msig_abi().data() );
   produce_blocks();

   //fail because approval by bob is missing
   BOOST_REQUIRE_EXCEPTION( push_action( N(alice), N(exec), mvo()
                                          ("proposer",      "alice")
                                          ("proposal_name", "first")
                                          ("executer",      "alice")
                            ),
                            arisen_assert_message_exception,
                            arisen_assert_message_is("transaction authorization failed")
   );

   //approve and execute with new version
   push_action( N(bob), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(bob), config::active_name })
   );

   transaction_trace_ptr trace;
   control->applied_transaction.connect(
   [&]( std::tuple<const transaction_trace_ptr&, const signed_transaction&> p ) {
      const auto& t = std::get<0>(p);
      if( t->scheduled ) { trace = t; }
   } );

   push_action( N(alice), N(exec), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("executer",      "alice")
   );

   BOOST_REQUIRE( bool(trace) );
   BOOST_REQUIRE_EQUAL( 1, trace->action_traces.size() );
   BOOST_REQUIRE_EQUAL( transaction_receipt::executed, trace->receipt->status );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( approve_with_hash, arisen_msig_tester ) try {
   auto trx = reqauth("alice", {permission_level{N(alice), config::active_name}}, abi_serializer_max_time );
   auto trx_hash = fc::sha256::hash( trx );
   auto not_trx_hash = fc::sha256::hash( trx_hash );

   push_action( N(alice), N(propose), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", vector<permission_level>{{ N(alice), config::active_name }})
   );

   //fail to approve with incorrect hash
   BOOST_REQUIRE_EXCEPTION( push_action( N(alice), N(approve), mvo()
                                          ("proposer",      "alice")
                                          ("proposal_name", "first")
                                          ("level",         permission_level{ N(alice), config::active_name })
                                          ("proposal_hash", not_trx_hash)
                            ),
                            arisen::chain::crypto_api_exception,
                            fc_exception_message_is("hash mismatch")
   );

   //approve and execute
   push_action( N(alice), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(alice), config::active_name })
                  ("proposal_hash", trx_hash)
   );

   transaction_trace_ptr trace;
   control->applied_transaction.connect(
   [&]( std::tuple<const transaction_trace_ptr&, const signed_transaction&> p ) {
      const auto& t = std::get<0>(p);
      if( t->scheduled ) { trace = t; }
   } );

   push_action( N(alice), N(exec), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("executer",      "alice")
   );

   BOOST_REQUIRE( bool(trace) );
   BOOST_REQUIRE_EQUAL( 1, trace->action_traces.size() );
   BOOST_REQUIRE_EQUAL( transaction_receipt::executed, trace->receipt->status );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( switch_proposal_and_fail_approve_with_hash, arisen_msig_tester ) try {
   auto trx1 = reqauth("alice", {permission_level{N(alice), config::active_name}}, abi_serializer_max_time );
   auto trx1_hash = fc::sha256::hash( trx1 );

   push_action( N(alice), N(propose), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx1)
                  ("requested", vector<permission_level>{{ N(alice), config::active_name }})
   );

   auto trx2 = reqauth("alice",
                       { permission_level{N(alice), config::active_name},
                         permission_level{N(alice), config::owner_name}  },
                       abi_serializer_max_time );

   push_action( N(alice), N(cancel), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("canceler",       "alice")
   );

   push_action( N(alice), N(propose), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx2)
                  ("requested", vector<permission_level>{ { N(alice), config::active_name },
                                                          { N(alice), config::owner_name } })
   );

   //fail to approve with hash meant for old proposal
   BOOST_REQUIRE_EXCEPTION( push_action( N(alice), N(approve), mvo()
                                          ("proposer",      "alice")
                                          ("proposal_name", "first")
                                          ("level",         permission_level{ N(alice), config::active_name })
                                          ("proposal_hash", trx1_hash)
                            ),
                            arisen::chain::crypto_api_exception,
                            fc_exception_message_is("hash mismatch")
   );
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
