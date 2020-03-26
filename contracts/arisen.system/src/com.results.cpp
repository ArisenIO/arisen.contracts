#include <arisen.system/com.results.hpp>

void com_results::buyresult( const asset& com_received ) { }

void com_results::sellresult( const asset& proceeds ) { }

void com_results::orderresult( const name& owner, const asset& proceeds ) { }

void com_results::rentresult( const asset& rented_tokens ) { }

extern "C" void apply( uint64_t, uint64_t, uint64_t ) { }
