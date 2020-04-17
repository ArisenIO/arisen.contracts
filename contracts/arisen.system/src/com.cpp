#include <arisen.system/arisen.system.hpp>
#include <arisen.token/arisen.token.hpp>
#include <arisen.system/com.results.hpp>

namespace arisensystem {

   using arisen::current_time_point;
   using arisen::token;

   void system_contract::deposit( const name& owner, const asset& amount )
   {
      require_auth( owner );

      check( amount.symbol == core_symbol(), "must deposit core token" );
      check( 0 < amount.amount, "must deposit a positive amount" );
      // inline transfer from owner's token balance
      {
         token::transfer_action transfer_act{ token_account, { owner, active_permission } };
         transfer_act.send( owner, com_account, amount, "deposit to COM fund" );
      }
      transfer_to_fund( owner, amount );
   }

   void system_contract::withdraw( const name& owner, const asset& amount )
   {
      require_auth( owner );

      check( amount.symbol == core_symbol(), "must withdraw core token" );
      check( 0 < amount.amount, "must withdraw a positive amount" );
      update_com_account( owner, asset( 0, core_symbol() ), asset( 0, core_symbol() ) );
      transfer_from_fund( owner, amount );
      // inline transfer to owner's token balance
      {
         token::transfer_action transfer_act{ token_account, { com_account, active_permission } };
         transfer_act.send( com_account, owner, amount, "withdraw from COM fund" );
      }
   }

   void system_contract::buycom( const name& from, const asset& amount )
   {
      require_auth( from );

      check( amount.symbol == core_symbol(), "asset must be core token" );
      check( 0 < amount.amount, "must use positive amount" );
      check_voting_requirement( from );
      transfer_from_fund( from, amount );
      const asset com_received    = add_to_com_pool( amount );
      const asset delta_com_stake = add_to_com_balance( from, amount, com_received );
      runcom(2);
      update_com_account( from, asset( 0, core_symbol() ), delta_com_stake );
      // dummy action added so that amount of COM tokens purchased shows up in action trace
      com_results::buyresult_action buycom_act( com_account, std::vector<arisen::permission_level>{ } );
      buycom_act.send( com_received );
   }

   void system_contract::unstaketocom( const name& owner, const name& receiver, const asset& from_net, const asset& from_cpu )
   {
      require_auth( owner );

      check( from_net.symbol == core_symbol() && from_cpu.symbol == core_symbol(), "asset must be core token" );
      check( (0 <= from_net.amount) && (0 <= from_cpu.amount) && (0 < from_net.amount || 0 < from_cpu.amount),
             "must unstake a positive amount to buy com" );
      check_voting_requirement( owner );

      {
         del_bandwidth_table dbw_table( get_self(), owner.value );
         auto del_itr = dbw_table.require_find( receiver.value, "delegated bandwidth record does not exist" );
         check( from_net.amount <= del_itr->net_weight.amount, "amount exceeds tokens staked for net");
         check( from_cpu.amount <= del_itr->cpu_weight.amount, "amount exceeds tokens staked for cpu");
         dbw_table.modify( del_itr, same_payer, [&]( delegated_bandwidth& dbw ) {
            dbw.net_weight.amount -= from_net.amount;
            dbw.cpu_weight.amount -= from_cpu.amount;
         });
         if ( del_itr->is_empty() ) {
            dbw_table.erase( del_itr );
         }
      }

      update_resource_limits( name(0), receiver, -from_net.amount, -from_cpu.amount );

      const asset payment = from_net + from_cpu;
      // inline transfer from stake_account to com_account
      {
         token::transfer_action transfer_act{ token_account, { stake_account, active_permission } };
         transfer_act.send( stake_account, com_account, payment, "buy COM with staked tokens" );
      }
      const asset com_received = add_to_com_pool( payment );
      add_to_com_balance( owner, payment, com_received );
      runcom(2);
      update_com_account( owner, asset( 0, core_symbol() ), asset( 0, core_symbol() ), true );
      // dummy action added so that amount of COM tokens purchased shows up in action trace
      com_results::buyresult_action buycom_act( com_account, std::vector<arisen::permission_level>{ } );
      buycom_act.send( com_received );
   }

   void system_contract::sellcom( const name& from, const asset& com )
   {
      require_auth( from );

      runcom(2);

      auto bitr = _combalance.require_find( from.value, "user must first buycom" );
      check( com.amount > 0 && com.symbol == bitr->com_balance.symbol,
             "asset must be a positive amount of (COM, 4)" );
      process_com_maturities( bitr );
      check( com.amount <= bitr->matured_com, "insufficient available com" );

      const auto current_order = fill_com_order( bitr, com );
      if ( current_order.success && current_order.proceeds.amount == 0 ) {
         check( false, "proceeds are negligible" );
      }
      asset pending_sell_order = update_com_account( from, current_order.proceeds, current_order.stake_change );
      if ( !current_order.success ) {
         if ( from == "b1"_n ) {
            check( false, "b1 sellcom orders should not be queued" );
         }
         /**
          * COM order couldn't be filled and is added to queue.
          * If account already has an open order, requested com is added to existing order.
          */
         auto oitr = _comorders.find( from.value );
         if ( oitr == _comorders.end() ) {
            oitr = _comorders.emplace( from, [&]( auto& order ) {
               order.owner         = from;
               order.com_requested = com;
               order.is_open       = true;
               order.proceeds      = asset( 0, core_symbol() );
               order.stake_change  = asset( 0, core_symbol() );
               order.order_time    = current_time_point();
            });
         } else {
            _comorders.modify( oitr, same_payer, [&]( auto& order ) {
               order.com_requested.amount += com.amount;
            });
         }
         pending_sell_order.amount = oitr->com_requested.amount;
      }
      check( pending_sell_order.amount <= bitr->matured_com, "insufficient funds for current and scheduled orders" );
      // dummy action added so that sell order proceeds show up in action trace
      if ( current_order.success ) {
         com_results::sellresult_action sellcom_act( com_account, std::vector<arisen::permission_level>{ } );
         sellcom_act.send( current_order.proceeds );
      }
   }

   void system_contract::cnclcomorder( const name& owner )
   {
      require_auth( owner );

      auto itr = _comorders.require_find( owner.value, "no sellcom order is scheduled" );
      check( itr->is_open, "sellcom order has been filled and cannot be canceled" );
      _comorders.erase( itr );
   }

   void system_contract::rentcpu( const name& from, const name& receiver, const asset& loan_payment, const asset& loan_fund )
   {
      require_auth( from );

      com_cpu_loan_table cpu_loans( get_self(), get_self().value );
      int64_t rented_tokens = rent_com( cpu_loans, from, receiver, loan_payment, loan_fund );
      update_resource_limits( from, receiver, 0, rented_tokens );
   }

   void system_contract::rentnet( const name& from, const name& receiver, const asset& loan_payment, const asset& loan_fund )
   {
      require_auth( from );

      com_net_loan_table net_loans( get_self(), get_self().value );
      int64_t rented_tokens = rent_com( net_loans, from, receiver, loan_payment, loan_fund );
      update_resource_limits( from, receiver, rented_tokens, 0 );
   }

   void system_contract::fundcpuloan( const name& from, uint64_t loan_num, const asset& payment )
   {
      require_auth( from );

      com_cpu_loan_table cpu_loans( get_self(), get_self().value );
      fund_com_loan( cpu_loans, from, loan_num, payment  );
   }

   void system_contract::fundnetloan( const name& from, uint64_t loan_num, const asset& payment )
   {
      require_auth( from );

      com_net_loan_table net_loans( get_self(), get_self().value );
      fund_com_loan( net_loans, from, loan_num, payment );
   }

   void system_contract::defcpuloan( const name& from, uint64_t loan_num, const asset& amount )
   {
      require_auth( from );

      com_cpu_loan_table cpu_loans( get_self(), get_self().value );
      defund_com_loan( cpu_loans, from, loan_num, amount );
   }

   void system_contract::defnetloan( const name& from, uint64_t loan_num, const asset& amount )
   {
      require_auth( from );

      com_net_loan_table net_loans( get_self(), get_self().value );
      defund_com_loan( net_loans, from, loan_num, amount );
   }

   void system_contract::updatecom( const name& owner )
   {
      require_auth( owner );

      runcom(2);

      auto itr = _combalance.require_find( owner.value, "account has no COM balance" );
      const asset init_stake = itr->vote_stake;

      auto comp_itr = _compool.begin();
      const int64_t total_com      = comp_itr->total_com.amount;
      const int64_t total_lendable = comp_itr->total_lendable.amount;
      const int64_t com_balance    = itr->com_balance.amount;

      asset current_stake( 0, core_symbol() );
      if ( total_com > 0 ) {
         current_stake.amount = ( uint128_t(com_balance) * total_lendable ) / total_com;
      }
      _combalance.modify( itr, same_payer, [&]( auto& rb ) {
         rb.vote_stake = current_stake;
      });

      update_com_account( owner, asset( 0, core_symbol() ), current_stake - init_stake, true );
      process_com_maturities( itr );
   }

   void system_contract::setcom( const asset& balance )
   {
      require_auth( "arisen"_n );

      check( balance.amount > 0, "balance must be set to have a positive amount" );
      check( balance.symbol == core_symbol(), "balance symbol must be core symbol" );
      check( com_system_initialized(), "com system is not initialized" );
      _compool.modify( _compool.begin(), same_payer, [&]( auto& pool ) {
         pool.total_rent = balance;
      });
   }

   void system_contract::comexec( const name& user, uint16_t max )
   {
      require_auth( user );

      runcom( max );
   }

   void system_contract::consolidate( const name& owner )
   {
      require_auth( owner );

      runcom(2);

      auto bitr = _combalance.require_find( owner.value, "account has no COM balance" );
      asset com_in_sell_order = update_com_account( owner, asset( 0, core_symbol() ), asset( 0, core_symbol() ) );
      consolidate_com_balance( bitr, com_in_sell_order );
   }

   void system_contract::mvtosavings( const name& owner, const asset& com )
   {
      require_auth( owner );

      runcom(2);

      auto bitr = _combalance.require_find( owner.value, "account has no COM balance" );
      check( com.amount > 0 && com.symbol == bitr->com_balance.symbol, "asset must be a positive amount of (COM, 4)" );
      const asset   com_in_sell_order = update_com_account( owner, asset( 0, core_symbol() ), asset( 0, core_symbol() ) );
      const int64_t com_in_savings    = read_com_savings( bitr );
      check( com.amount + com_in_sell_order.amount + com_in_savings <= bitr->com_balance.amount,
             "insufficient COM balance" );
      process_com_maturities( bitr );
      _combalance.modify( bitr, same_payer, [&]( auto& rb ) {
         int64_t moved_com = 0;
         while ( !rb.com_maturities.empty() && moved_com < com.amount) {
            const int64_t dcom = std::min( com.amount - moved_com, rb.com_maturities.back().second );
            rb.com_maturities.back().second -= dcom;
            moved_com                       += dcom;
            if ( rb.com_maturities.back().second == 0 ) {
               rb.com_maturities.pop_back();
            }
         }
         if ( moved_com < com.amount ) {
            const int64_t dcom = com.amount - moved_com;
            rb.matured_com    -= dcom;
            moved_com         += dcom;
            check( com_in_sell_order.amount <= rb.matured_com, "logic error in mvtosavings" );
         }
         check( moved_com == com.amount, "programmer error in mvtosavings" );
      });
      put_com_savings( bitr, com_in_savings + com.amount );
   }

   void system_contract::mvfrsavings( const name& owner, const asset& com )
   {
      require_auth( owner );

      runcom(2);

      auto bitr = _combalance.require_find( owner.value, "account has no COM balance" );
      check( com.amount > 0 && com.symbol == bitr->com_balance.symbol, "asset must be a positive amount of (COM, 4)" );
      const int64_t com_in_savings = read_com_savings( bitr );
      check( com.amount <= com_in_savings, "insufficient COM in savings" );
      process_com_maturities( bitr );
      _combalance.modify( bitr, same_payer, [&]( auto& rb ) {
         const time_point_sec maturity = get_com_maturity();
         if ( !rb.com_maturities.empty() && rb.com_maturities.back().first == maturity ) {
            rb.com_maturities.back().second += com.amount;
         } else {
            rb.com_maturities.emplace_back( maturity, com.amount );
         }
      });
      put_com_savings( bitr, com_in_savings - com.amount );
      update_com_account( owner, asset( 0, core_symbol() ), asset( 0, core_symbol() ) );
   }

   void system_contract::closecom( const name& owner )
   {
      require_auth( owner );

      if ( com_system_initialized() )
         runcom(2);

      update_com_account( owner, asset( 0, core_symbol() ), asset( 0, core_symbol() ) );

      /// check for any outstanding loans or com fund
      {
         com_cpu_loan_table cpu_loans( get_self(), get_self().value );
         auto cpu_idx = cpu_loans.get_index<"byowner"_n>();
         bool no_outstanding_cpu_loans = ( cpu_idx.find( owner.value ) == cpu_idx.end() );

         com_net_loan_table net_loans( get_self(), get_self().value );
         auto net_idx = net_loans.get_index<"byowner"_n>();
         bool no_outstanding_net_loans = ( net_idx.find( owner.value ) == net_idx.end() );

         auto fund_itr = _comfunds.find( owner.value );
         bool no_outstanding_com_fund = ( fund_itr != _comfunds.end() ) && ( fund_itr->balance.amount == 0 );

         if ( no_outstanding_cpu_loans && no_outstanding_net_loans && no_outstanding_com_fund ) {
            _comfunds.erase( fund_itr );
         }
      }

      /// check for remaining com balance
      {
         auto com_itr = _combalance.find( owner.value );
         if ( com_itr != _combalance.end() ) {
            check( com_itr->com_balance.amount == 0, "account has remaining COM balance, must sell first");
            _combalance.erase( com_itr );
         }
      }
   }

   /**
    * @brief Updates account NET and CPU resource limits
    *
    * @param from - account charged for RAM if there is a need
    * @param receiver - account whose resource limits are updated
    * @param delta_net - change in NET bandwidth limit
    * @param delta_cpu - change in CPU bandwidth limit
    */
   void system_contract::update_resource_limits( const name& from, const name& receiver, int64_t delta_net, int64_t delta_cpu )
   {
      if ( delta_cpu == 0 && delta_net == 0 ) { // nothing to update
         return;
      }

      user_resources_table totals_tbl( get_self(), receiver.value );
      auto tot_itr = totals_tbl.find( receiver.value );
      if ( tot_itr == totals_tbl.end() ) {
         check( 0 <= delta_net && 0 <= delta_cpu, "logic error, should not occur");
         tot_itr = totals_tbl.emplace( from, [&]( auto& tot ) {
            tot.owner      = receiver;
            tot.net_weight = asset( delta_net, core_symbol() );
            tot.cpu_weight = asset( delta_cpu, core_symbol() );
         });
      } else {
         totals_tbl.modify( tot_itr, same_payer, [&]( auto& tot ) {
            tot.net_weight.amount += delta_net;
            tot.cpu_weight.amount += delta_cpu;
         });
      }
      check( 0 <= tot_itr->net_weight.amount, "insufficient staked total net bandwidth" );
      check( 0 <= tot_itr->cpu_weight.amount, "insufficient staked total cpu bandwidth" );

      {
         bool net_managed = false;
         bool cpu_managed = false;

         auto voter_itr = _voters.find( receiver.value );
         if( voter_itr != _voters.end() ) {
            net_managed = has_field( voter_itr->flags1, voter_info::flags1_fields::net_managed );
            cpu_managed = has_field( voter_itr->flags1, voter_info::flags1_fields::cpu_managed );
         }

         if( !(net_managed && cpu_managed) ) {
            int64_t ram_bytes = 0, net = 0, cpu = 0;
            get_resource_limits( receiver, ram_bytes, net, cpu );

            set_resource_limits( receiver,
                                 ram_bytes,
                                 net_managed ? net : tot_itr->net_weight.amount,
                                 cpu_managed ? cpu : tot_itr->cpu_weight.amount );
         }
      }

      if ( tot_itr->is_empty() ) {
         totals_tbl.erase( tot_itr );
      }
   }

   /**
    * @brief Checks if account satisfies voting requirement (voting for a proxy or 21 producers)
    * for buying COM
    *
    * @param owner - account buying or already holding COM tokens
    * @err_msg - error message
    */
   void system_contract::check_voting_requirement( const name& owner, const char* error_msg )const
   {
      auto vitr = _voters.find( owner.value );
      check( vitr != _voters.end() && ( vitr->proxy || 21 <= vitr->producers.size() ), error_msg );
   }

   /**
    * @brief Checks if CPU and Network loans are available
    *
    * Loans are available if 1) COM pool lendable balance is nonempty, and 2) there are no
    * unfilled sellcom orders.
    */
   bool system_contract::com_loans_available()const
   {
      if ( !com_available() ) {
         return false;
      } else {
         if ( _comorders.begin() == _comorders.end() ) {
            return true; // no outstanding sellcom orders
         } else {
            auto idx = _comorders.get_index<"bytime"_n>();
            return !idx.begin()->is_open; // no outstanding unfilled sellcom orders
         }
      }
   }

   /**
    * @brief Updates com_pool balances upon creating a new loan or renewing an existing one
    *
    * @param payment - loan fee paid
    * @param rented_tokens - amount of tokens to be staked to loan receiver
    * @param new_loan - flag indicating whether the loan is new or being renewed
    */
   void system_contract::add_loan_to_com_pool( const asset& payment, int64_t rented_tokens, bool new_loan )
   {
      _compool.modify( _compool.begin(), same_payer, [&]( auto& rt ) {
         // add payment to total_rent
         rt.total_rent.amount    += payment.amount;
         // move rented_tokens from total_unlent to total_lent
         rt.total_unlent.amount  -= rented_tokens;
         rt.total_lent.amount    += rented_tokens;
         // add payment to total_unlent
         rt.total_unlent.amount  += payment.amount;
         rt.total_lendable.amount = rt.total_unlent.amount + rt.total_lent.amount;
         // increment loan_num if a new loan is being created
         if ( new_loan ) {
            rt.loan_num++;
         }
      });
   }

   /**
    * @brief Updates com_pool balances upon closing an expired loan
    *
    * @param loan - loan to be closed
    */
   void system_contract::remove_loan_from_com_pool( const com_loan& loan )
   {
      const auto& pool = _compool.begin();
      const int64_t delta_total_rent = exchange_state::get_bancor_output( pool->total_unlent.amount,
                                                                          pool->total_rent.amount,
                                                                          loan.total_staked.amount );
      _compool.modify( pool, same_payer, [&]( auto& rt ) {
         // deduct calculated delta_total_rent from total_rent
         rt.total_rent.amount    -= delta_total_rent;
         // move rented tokens from total_lent to total_unlent
         rt.total_unlent.amount  += loan.total_staked.amount;
         rt.total_lent.amount    -= loan.total_staked.amount;
         rt.total_lendable.amount = rt.total_unlent.amount + rt.total_lent.amount;
      });
   }

   /**
    * @brief Updates the fields of an existing loan that is being renewed
    */
   template <typename Index, typename Iterator>
   int64_t system_contract::update_renewed_loan( Index& idx, const Iterator& itr, int64_t rented_tokens )
   {
      int64_t delta_stake = rented_tokens - itr->total_staked.amount;
      idx.modify ( itr, same_payer, [&]( auto& loan ) {
         loan.total_staked.amount = rented_tokens;
         loan.expiration         += arisen::days(30);
         loan.balance.amount     -= loan.payment.amount;
      });
      return delta_stake;
   }

   /**
    * @brief Performs maintenance operations on expired NET and CPU loans and sellcom orders
    *
    * @param max - maximum number of each of the three categories to be processed
    */
   void system_contract::runcom( uint16_t max )
   {
      check( com_system_initialized(), "com system not initialized yet" );

      const auto& pool = _compool.begin();

      auto process_expired_loan = [&]( auto& idx, const auto& itr ) -> std::pair<bool, int64_t> {
         /// update com_pool in order to delete existing loan
         remove_loan_from_com_pool( *itr );
         bool    delete_loan   = false;
         int64_t delta_stake   = 0;
         /// calculate rented tokens at current price
         int64_t rented_tokens = exchange_state::get_bancor_output( pool->total_rent.amount,
                                                                    pool->total_unlent.amount,
                                                                    itr->payment.amount );
         /// conditions for loan renewal
         bool renew_loan = itr->payment <= itr->balance        /// loan has sufficient balance
                        && itr->payment.amount < rented_tokens /// loan has favorable return
                        && com_loans_available();              /// no pending sell orders
         if ( renew_loan ) {
            /// update com_pool in order to account for renewed loan
            add_loan_to_com_pool( itr->payment, rented_tokens, false );
            /// update renewed loan fields
            delta_stake = update_renewed_loan( idx, itr, rented_tokens );
         } else {
            delete_loan = true;
            delta_stake = -( itr->total_staked.amount );
            /// refund "from" account if the closed loan balance is positive
            if ( itr->balance.amount > 0 ) {
               transfer_to_fund( itr->from, itr->balance );
            }
         }

         return { delete_loan, delta_stake };
      };

      /// transfer from arisen.names to arisen.com
      if ( pool->namebid_proceeds.amount > 0 ) {
         channel_to_com( names_account, pool->namebid_proceeds );
         _compool.modify( pool, same_payer, [&]( auto& rt ) {
            rt.namebid_proceeds.amount = 0;
         });
      }

      /// process cpu loans
      {
         com_cpu_loan_table cpu_loans( get_self(), get_self().value );
         auto cpu_idx = cpu_loans.get_index<"byexpr"_n>();
         for ( uint16_t i = 0; i < max; ++i ) {
            auto itr = cpu_idx.begin();
            if ( itr == cpu_idx.end() || itr->expiration > current_time_point() ) break;

            auto result = process_expired_loan( cpu_idx, itr );
            if ( result.second != 0 )
               update_resource_limits( itr->from, itr->receiver, 0, result.second );

            if ( result.first )
               cpu_idx.erase( itr );
         }
      }

      /// process net loans
      {
         com_net_loan_table net_loans( get_self(), get_self().value );
         auto net_idx = net_loans.get_index<"byexpr"_n>();
         for ( uint16_t i = 0; i < max; ++i ) {
            auto itr = net_idx.begin();
            if ( itr == net_idx.end() || itr->expiration > current_time_point() ) break;

            auto result = process_expired_loan( net_idx, itr );
            if ( result.second != 0 )
               update_resource_limits( itr->from, itr->receiver, result.second, 0 );

            if ( result.first )
               net_idx.erase( itr );
         }
      }

      /// process sellcom orders
      if ( _comorders.begin() != _comorders.end() ) {
         auto idx  = _comorders.get_index<"bytime"_n>();
         auto oitr = idx.begin();
         for ( uint16_t i = 0; i < max; ++i ) {
            if ( oitr == idx.end() || !oitr->is_open ) break;
            auto next = oitr;
            ++next;
            auto bitr = _combalance.find( oitr->owner.value );
            if ( bitr != _combalance.end() ) { // should always be true
               auto result = fill_com_order( bitr, oitr->com_requested );
               if ( result.success ) {
                  const name order_owner = oitr->owner;
                  idx.modify( oitr, same_payer, [&]( auto& order ) {
                     order.proceeds.amount     = result.proceeds.amount;
                     order.stake_change.amount = result.stake_change.amount;
                     order.close();
                  });
                  /// send dummy action to show owner and proceeds of filled sellcom order
                  com_results::orderresult_action order_act( com_account, std::vector<arisen::permission_level>{ } );
                  order_act.send( order_owner, result.proceeds );
               }
            }
            oitr = next;
         }
      }

   }

   template <typename T>
   int64_t system_contract::rent_com( T& table, const name& from, const name& receiver, const asset& payment, const asset& fund )
   {
      runcom(2);

      check( com_loans_available(), "com loans are currently not available" );
      check( payment.symbol == core_symbol() && fund.symbol == core_symbol(), "must use core token" );
      check( 0 < payment.amount && 0 <= fund.amount, "must use positive asset amount" );

      transfer_from_fund( from, payment + fund );

      const auto& pool = _compool.begin(); /// already checked that _compool.begin() != _compool.end() in com_loans_available()

      int64_t rented_tokens = exchange_state::get_bancor_output( pool->total_rent.amount,
                                                                 pool->total_unlent.amount,
                                                                 payment.amount );
      check( payment.amount < rented_tokens, "loan price does not favor renting" );
      add_loan_to_com_pool( payment, rented_tokens, true );

      table.emplace( from, [&]( auto& c ) {
         c.from         = from;
         c.receiver     = receiver;
         c.payment      = payment;
         c.balance      = fund;
         c.total_staked = asset( rented_tokens, core_symbol() );
         c.expiration   = current_time_point() + arisen::days(30);
         c.loan_num     = pool->loan_num;
      });

      com_results::rentresult_action rentresult_act{ com_account, std::vector<arisen::permission_level>{ } };
      rentresult_act.send( asset{ rented_tokens, core_symbol() } );
      return rented_tokens;
   }

   /**
    * @brief Processes a sellcom order and returns object containing the results
    *
    * Processes an incoming or already scheduled sellcom order. If COM pool has enough core
    * tokens not frozen in loans, order is filled. In this case, COM pool totals, user com_balance
    * and user vote_stake are updated. However, this function does not update user voting power. The
    * function returns success flag, order proceeds, and vote stake delta. These are used later in a
    * different function to complete order processing, i.e. transfer proceeds to user COM fund and
    * update user vote weight.
    *
    * @param bitr - iterator pointing to com_balance database record
    * @param com - amount of com to be sold
    *
    * @return com_order_outcome - a struct containing success flag, order proceeds, and resultant
    * vote stake change
    */
   com_order_outcome system_contract::fill_com_order( const com_balance_table::const_iterator& bitr, const asset& com )
   {
      auto comitr = _compool.begin();
      const int64_t S0 = comitr->total_lendable.amount;
      const int64_t R0 = comitr->total_com.amount;
      const int64_t p  = (uint128_t(com.amount) * S0) / R0;
      const int64_t R1 = R0 - com.amount;
      const int64_t S1 = S0 - p;
      asset proceeds( p, core_symbol() );
      asset stake_change( 0, core_symbol() );
      bool  success = false;

      const int64_t unlent_lower_bound = ( uint128_t(2) * comitr->total_lent.amount ) / 10;
      const int64_t available_unlent   = comitr->total_unlent.amount - unlent_lower_bound; // available_unlent <= 0 is possible
      if ( proceeds.amount <= available_unlent ) {
         const int64_t init_vote_stake_amount = bitr->vote_stake.amount;
         const int64_t current_stake_value    = ( uint128_t(bitr->com_balance.amount) * S0 ) / R0;
         _compool.modify( comitr, same_payer, [&]( auto& rt ) {
            rt.total_com.amount      = R1;
            rt.total_lendable.amount = S1;
            rt.total_unlent.amount   = rt.total_lendable.amount - rt.total_lent.amount;
         });
         _combalance.modify( bitr, same_payer, [&]( auto& rb ) {
            rb.vote_stake.amount   = current_stake_value - proceeds.amount;
            rb.com_balance.amount -= com.amount;
            rb.matured_com        -= com.amount;
         });
         stake_change.amount = bitr->vote_stake.amount - init_vote_stake_amount;
         success = true;
      } else {
         proceeds.amount = 0;
      }

      return { success, proceeds, stake_change };
   }

   template <typename T>
   void system_contract::fund_com_loan( T& table, const name& from, uint64_t loan_num, const asset& payment  )
   {
      check( payment.symbol == core_symbol(), "must use core token" );
      transfer_from_fund( from, payment );
      auto itr = table.require_find( loan_num, "loan not found" );
      check( itr->from == from, "user must be loan creator" );
      check( itr->expiration > current_time_point(), "loan has already expired" );
      table.modify( itr, same_payer, [&]( auto& loan ) {
         loan.balance.amount += payment.amount;
      });
   }

   template <typename T>
   void system_contract::defund_com_loan( T& table, const name& from, uint64_t loan_num, const asset& amount  )
   {
      check( amount.symbol == core_symbol(), "must use core token" );
      auto itr = table.require_find( loan_num, "loan not found" );
      check( itr->from == from, "user must be loan creator" );
      check( itr->expiration > current_time_point(), "loan has already expired" );
      check( itr->balance >= amount, "insufficent loan balance" );
      table.modify( itr, same_payer, [&]( auto& loan ) {
         loan.balance.amount -= amount.amount;
      });
      transfer_to_fund( from, amount );
   }

   /**
    * @brief Transfers tokens from owner COM fund
    *
    * @pre - owner COM fund has sufficient balance
    *
    * @param owner - owner account name
    * @param amount - tokens to be transfered out of COM fund
    */
   void system_contract::transfer_from_fund( const name& owner, const asset& amount )
   {
      check( 0 < amount.amount && amount.symbol == core_symbol(), "must transfer positive amount from COM fund" );
      auto itr = _comfunds.require_find( owner.value, "must deposit to COM fund first" );
      check( amount <= itr->balance, "insufficient funds" );
      _comfunds.modify( itr, same_payer, [&]( auto& fund ) {
         fund.balance.amount -= amount.amount;
      });
   }

   /**
    * @brief Transfers tokens to owner COM fund
    *
    * @param owner - owner account name
    * @param amount - tokens to be transfered to COM fund
    */
   void system_contract::transfer_to_fund( const name& owner, const asset& amount )
   {
      check( 0 < amount.amount && amount.symbol == core_symbol(), "must transfer positive amount to COM fund" );
      auto itr = _comfunds.find( owner.value );
      if ( itr == _comfunds.end() ) {
         _comfunds.emplace( owner, [&]( auto& fund ) {
            fund.owner   = owner;
            fund.balance = amount;
         });
      } else {
         _comfunds.modify( itr, same_payer, [&]( auto& fund ) {
            fund.balance.amount += amount.amount;
         });
      }
   }

   /**
    * @brief Processes owner filled sellcom order and updates vote weight
    *
    * Checks if user has a scheduled sellcom order that has been filled, completes its processing,
    * and deletes it. Processing entails transfering proceeds to user COM fund and updating user
    * vote weight. Additional proceeds and stake change can be passed as arguments. This function
    * is called only by actions pushed by owner.
    *
    * @param owner - owner account name
    * @param proceeds - additional proceeds to be transfered to owner COM fund
    * @param delta_stake - additional stake to be added to owner vote weight
    * @param force_vote_update - if true, vote weight is updated even if vote stake didn't change
    *
    * @return asset - COM amount of owner unfilled sell order if one exists
    */
   asset system_contract::update_com_account( const name& owner, const asset& proceeds, const asset& delta_stake, bool force_vote_update )
   {
      asset to_fund( proceeds );
      asset to_stake( delta_stake );
      asset com_in_sell_order( 0, com_symbol );
      auto itr = _comorders.find( owner.value );
      if ( itr != _comorders.end() ) {
         if ( itr->is_open ) {
            com_in_sell_order.amount = itr->com_requested.amount;
         } else {
            to_fund.amount  += itr->proceeds.amount;
            to_stake.amount += itr->stake_change.amount;
            _comorders.erase( itr );
         }
      }

      if ( to_fund.amount > 0 )
         transfer_to_fund( owner, to_fund );
      if ( force_vote_update || to_stake.amount != 0 )
         update_voting_power( owner, to_stake );

      return com_in_sell_order;
   }

   /**
    * @brief Channels system fees to COM pool
    *
    * @param from - account from which asset is transfered to COM pool
    * @param amount - amount of tokens to be transfered
    */
   void system_contract::channel_to_com( const name& from, const asset& amount )
   {
#if CHANNEL_RAM_AND_NAMEBID_FEES_TO_COM
      if ( com_available() ) {
         _compool.modify( _compool.begin(), same_payer, [&]( auto& rp ) {
            rp.total_unlent.amount   += amount.amount;
            rp.total_lendable.amount += amount.amount;
         });
         // inline transfer to com_account
         token::transfer_action transfer_act{ token_account, { from, active_permission } };
         transfer_act.send( from, com_account, amount,
                            std::string("transfer from ") + from.to_string() + " to arisen.com" );
      }
#endif
   }

   /**
    * @brief Updates namebid proceeds to be transfered to COM pool
    *
    * @param highest_bid - highest bidding amount of closed namebid
    */
   void system_contract::channel_namebid_to_com( const int64_t highest_bid )
   {
#if CHANNEL_RAM_AND_NAMEBID_FEES_TO_COM
      if ( com_available() ) {
         _compool.modify( _compool.begin(), same_payer, [&]( auto& rp ) {
            rp.namebid_proceeds.amount += highest_bid;
         });
      }
#endif
   }

   /**
    * @brief Calculates maturity time of purchased COM tokens which is 4 days from end
    * of the day UTC
    *
    * @return time_point_sec
    */
   time_point_sec system_contract::get_com_maturity()
   {
      const uint32_t num_of_maturity_buckets = 5;
      static const uint32_t now = current_time_point().sec_since_epoch();
      static const uint32_t r   = now % seconds_per_day;
      static const time_point_sec rms{ now - r + num_of_maturity_buckets * seconds_per_day };
      return rms;
   }

   /**
    * @brief Updates COM owner maturity buckets
    *
    * @param bitr - iterator pointing to com_balance object
    */
   void system_contract::process_com_maturities( const com_balance_table::const_iterator& bitr )
   {
      const time_point_sec now = current_time_point();
      _combalance.modify( bitr, same_payer, [&]( auto& rb ) {
         while ( !rb.com_maturities.empty() && rb.com_maturities.front().first <= now ) {
            rb.matured_com += rb.com_maturities.front().second;
            rb.com_maturities.pop_front();
         }
      });
   }

   /**
    * @brief Consolidates COM maturity buckets into one
    *
    * @param bitr - iterator pointing to com_balance object
    * @param com_in_sell_order - COM tokens in owner unfilled sell order, if one exists
    */
   void system_contract::consolidate_com_balance( const com_balance_table::const_iterator& bitr,
                                                  const asset& com_in_sell_order )
   {
      const int64_t com_in_savings = read_com_savings( bitr );
      _combalance.modify( bitr, same_payer, [&]( auto& rb ) {
         int64_t total  = rb.matured_com - com_in_sell_order.amount;
         rb.matured_com = com_in_sell_order.amount;
         while ( !rb.com_maturities.empty() ) {
            total += rb.com_maturities.front().second;
            rb.com_maturities.pop_front();
         }
         if ( total > 0 ) {
            rb.com_maturities.emplace_back( get_com_maturity(), total );
         }
      });
      put_com_savings( bitr, com_in_savings );
   }

   /**
    * @brief Updates COM pool balances upon COM purchase
    *
    * @param payment - amount of core tokens paid
    *
    * @return asset - calculated amount of COM tokens purchased
    */
   asset system_contract::add_to_com_pool( const asset& payment )
   {
      /**
       * If CORE_SYMBOL is (RIX,4), maximum supply is 10^10 tokens (10 billion tokens), i.e., maximum amount
       * of indivisible units is 10^14. com_ratio = 10^4 sets the upper bound on (COM,4) indivisible units to
       * 10^18 and that is within the maximum allowable amount field of asset type which is set to 2^62
       * (approximately 4.6 * 10^18). For a different CORE_SYMBOL, and in order for maximum (COM,4) amount not
       * to exceed that limit, maximum amount of indivisible units cannot be set to a value larger than 4 * 10^14.
       * If precision of CORE_SYMBOL is 4, that corresponds to a maximum supply of 40 billion tokens.
       */
      const int64_t com_ratio = 10000;
      const asset   init_total_rent( 20'000'0000, core_symbol() ); /// base balance prevents renting profitably until at least a minimum number of core_symbol() is made available
      asset com_received( 0, com_symbol );
      auto itr = _compool.begin();
      if ( !com_system_initialized() ) {
         /// initialize COM pool
         _compool.emplace( get_self(), [&]( auto& rp ) {
            com_received.amount = payment.amount * com_ratio;
            rp.total_lendable   = payment;
            rp.total_lent       = asset( 0, core_symbol() );
            rp.total_unlent     = rp.total_lendable - rp.total_lent;
            rp.total_rent       = init_total_rent;
            rp.total_com        = com_received;
            rp.namebid_proceeds = asset( 0, core_symbol() );
         });
      } else if ( !com_available() ) { /// should be a rare corner case, COM pool is initialized but empty
         _compool.modify( itr, same_payer, [&]( auto& rp ) {
            com_received.amount      = payment.amount * com_ratio;
            rp.total_lendable.amount = payment.amount;
            rp.total_lent.amount     = 0;
            rp.total_unlent.amount   = rp.total_lendable.amount - rp.total_lent.amount;
            rp.total_rent.amount     = init_total_rent.amount;
            rp.total_com.amount      = com_received.amount;
         });
      } else {
         /// total_lendable > 0 if total_com > 0 except in a rare case and due to rounding errors
         check( itr->total_lendable.amount > 0, "lendable COM pool is empty" );
         const int64_t S0 = itr->total_lendable.amount;
         const int64_t S1 = S0 + payment.amount;
         const int64_t R0 = itr->total_com.amount;
         const int64_t R1 = (uint128_t(S1) * R0) / S0;
         com_received.amount = R1 - R0;
         _compool.modify( itr, same_payer, [&]( auto& rp ) {
            rp.total_lendable.amount = S1;
            rp.total_com.amount      = R1;
            rp.total_unlent.amount   = rp.total_lendable.amount - rp.total_lent.amount;
            check( rp.total_unlent.amount >= 0, "programmer error, this should never go negative" );
         });
      }

      return com_received;
   }

   /**
    * @brief Updates owner COM balance upon buying COM tokens
    *
    * @param owner - account name of COM owner
    * @param payment - amount core tokens paid to buy COM
    * @param com_received - amount of purchased COM tokens
    *
    * @return asset - change in owner COM vote stake
    */
   asset system_contract::add_to_com_balance( const name& owner, const asset& payment, const asset& com_received )
   {
      asset init_com_stake( 0, core_symbol() );
      asset current_com_stake( 0, core_symbol() );
      auto bitr = _combalance.find( owner.value );
      if ( bitr == _combalance.end() ) {
         bitr = _combalance.emplace( owner, [&]( auto& rb ) {
            rb.owner       = owner;
            rb.vote_stake  = payment;
            rb.com_balance = com_received;
         });
         current_com_stake.amount = payment.amount;
      } else {
         init_com_stake.amount = bitr->vote_stake.amount;
         _combalance.modify( bitr, same_payer, [&]( auto& rb ) {
            rb.com_balance.amount += com_received.amount;
            rb.vote_stake.amount   = ( uint128_t(rb.com_balance.amount) * _compool.begin()->total_lendable.amount )
                                     / _compool.begin()->total_com.amount;
         });
         current_com_stake.amount = bitr->vote_stake.amount;
      }

      const int64_t com_in_savings = read_com_savings( bitr );
      process_com_maturities( bitr );
      _combalance.modify( bitr, same_payer, [&]( auto& rb ) {
         const time_point_sec maturity = get_com_maturity();
         if ( !rb.com_maturities.empty() && rb.com_maturities.back().first == maturity ) {
            rb.com_maturities.back().second += com_received.amount;
         } else {
            rb.com_maturities.emplace_back( maturity, com_received.amount );
         }
      });
      put_com_savings( bitr, com_in_savings );
      return current_com_stake - init_com_stake;
   }

   /**
    * @brief Reads amount of COM in savings bucket and removes the bucket from maturities
    *
    * Reads and (temporarily) removes COM savings bucket from COM maturities in order to
    * allow uniform processing of remaining buckets as savings is a special case. This
    * function is used in conjunction with put_com_savings.
    *
    * @param bitr - iterator pointing to com_balance object
    *
    * @return int64_t - amount of COM in savings bucket
    */
   int64_t system_contract::read_com_savings( const com_balance_table::const_iterator& bitr )
   {
      int64_t com_in_savings = 0;
      static const time_point_sec end_of_days = time_point_sec::maximum();
      if ( !bitr->com_maturities.empty() && bitr->com_maturities.back().first == end_of_days ) {
         _combalance.modify( bitr, same_payer, [&]( auto& rb ) {
            com_in_savings = rb.com_maturities.back().second;
            rb.com_maturities.pop_back();
         });
      }
      return com_in_savings;
   }

   /**
    * @brief Adds a specified COM amount to savings bucket
    *
    * @param bitr - iterator pointing to com_balance object
    * @param com - amount of COM to be added
    */
   void system_contract::put_com_savings( const com_balance_table::const_iterator& bitr, int64_t com )
   {
      if ( com == 0 ) return;
      static const time_point_sec end_of_days = time_point_sec::maximum();
      _combalance.modify( bitr, same_payer, [&]( auto& rb ) {
         if ( !rb.com_maturities.empty() && rb.com_maturities.back().first == end_of_days ) {
            rb.com_maturities.back().second += com;
         } else {
            rb.com_maturities.emplace_back( end_of_days, com );
         }
      });
   }

   /**
    * @brief Updates voter COM vote stake to the current value of COM tokens held
    *
    * @param voter - account name of voter
    */
   void system_contract::update_com_stake( const name& voter )
   {
      int64_t delta_stake = 0;
      auto bitr = _combalance.find( voter.value );
      if ( bitr != _combalance.end() && com_available() ) {
         asset init_vote_stake = bitr->vote_stake;
         asset current_vote_stake( 0, core_symbol() );
         current_vote_stake.amount = ( uint128_t(bitr->com_balance.amount) * _compool.begin()->total_lendable.amount )
                                     / _compool.begin()->total_com.amount;
         _combalance.modify( bitr, same_payer, [&]( auto& rb ) {
            rb.vote_stake.amount = current_vote_stake.amount;
         });
         delta_stake = current_vote_stake.amount - init_vote_stake.amount;
      }

      if ( delta_stake != 0 ) {
         auto vitr = _voters.find( voter.value );
         if ( vitr != _voters.end() ) {
            _voters.modify( vitr, same_payer, [&]( auto& vinfo ) {
               vinfo.staked += delta_stake;
            });
         }
      }
   }

}; /// namespace arisensystem
