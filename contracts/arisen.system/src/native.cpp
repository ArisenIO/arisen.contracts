#include <arisen.system/native.hpp>

#include <arisen/check.hpp>

namespace arisen {
   bool is_feature_activated( const arisen::checksum256& feature_digest ) {
      auto feature_digest_data = feature_digest.extract_as_byte_array();
      return internal_use_do_not_use::is_feature_activated(
         reinterpret_cast<const ::capi_checksum256*>( feature_digest_data.data() )
      );
   }

   void preactivate_feature( const arisen::checksum256& feature_digest ) {
      auto feature_digest_data = feature_digest.extract_as_byte_array();
      internal_use_do_not_use::preactivate_feature(
         reinterpret_cast<const ::capi_checksum256*>( feature_digest_data.data() )
      );
   }
}

namespace arisensystem {

   void native::onerror( ignore<uint128_t>, ignore<std::vector<char>> ) {
      arisen::check( false, "the onerror action cannot be called directly" );
   }

}
