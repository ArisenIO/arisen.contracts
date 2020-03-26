#pragma once

#include <arisen/asset.hpp>
#include <arisen/arisen.hpp>
#include <arisen/name.hpp>

using arisen::action_wrapper;
using arisen::asset;
using arisen::name;

class [[arisen::contract("com.results")]] com_results : arisen::contract {
   public:

      using arisen::contract::contract;

      [[arisen::action]]
      void buyresult( const asset& com_received );

      [[arisen::action]]
      void sellresult( const asset& proceeds );

      [[arisen::action]]
      void orderresult( const name& owner, const asset& proceeds );

      [[arisen::action]]
      void rentresult( const asset& rented_tokens );

      using buyresult_action   = action_wrapper<"buyresult"_n,   &com_results::buyresult>;
      using sellresult_action  = action_wrapper<"sellresult"_n,  &com_results::sellresult>;
      using orderresult_action = action_wrapper<"orderresult"_n, &com_results::orderresult>;
      using rentresult_action  = action_wrapper<"rentresult"_n,  &com_results::rentresult>;
};
