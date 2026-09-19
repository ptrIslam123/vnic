#include "include/rss/hash.h"

//#include <rte_ethdev.h>

#include <cstring>
#include <cassert>

#include <netinet/ip.h>
#include <linux/types.h>

namespace
{

void key320_turnkey( uint8_t* Key, int len )
{
     int i, r;
     uint8_t tmp;

     for( i = 0; i < ( len >> 1 ); i++ )
     {
          r = len - i - 1;
          tmp = Key[ i ];
          Key[ i ] = Key[ r ];
          Key[ r ] = tmp;
     }
}

void key320_rotate( uint32_t* K )
{
     uint32_t tmp = K[ 9 ];

     K[ 9 ] = ( K[ 8 ] >> 31 ) | ( K[ 9 ] << 1 );
     K[ 8 ] = ( K[ 7 ] >> 31 ) | ( K[ 8 ] << 1 );
     K[ 7 ] = ( K[ 6 ] >> 31 ) | ( K[ 7 ] << 1 );
     K[ 6 ] = ( K[ 5 ] >> 31 ) | ( K[ 6 ] << 1 );
     K[ 5 ] = ( K[ 4 ] >> 31 ) | ( K[ 5 ] << 1 );
     K[ 4 ] = ( K[ 3 ] >> 31 ) | ( K[ 4 ] << 1 );
     K[ 3 ] = ( K[ 2 ] >> 31 ) | ( K[ 3 ] << 1 );
     K[ 2 ] = ( K[ 1 ] >> 31 ) | ( K[ 2 ] << 1 );
     K[ 1 ] = ( K[ 0 ] >> 31 ) | ( K[ 1 ] << 1 );
     K[ 0 ] = ( tmp >> 31 ) | ( K[ 0 ] << 1 );
}

uint32_t key320_hash_calc( const std::uint32_t* key, std::uint32_t hash, const uint8_t* data, int bits )
{
     int i;
     uint32_t d = 0;
     uint32_t K[ 10 ];

     memcpy( K, key, sizeof( K ) );
     key320_turnkey( ( uint8_t* )K, sizeof( K ) );

     for( i = 0; i < bits; i++ )
     {
          if( ( i & 7 ) == 0 )
          {
               d = data[ i >> 3 ]; // Load next word
          }
          if( ( d & 128 ) != 0 )
               hash ^= K[ 9 ];
          d <<= 1;
          key320_rotate( K );
     }

     return ( hash );
}

} // namespace

namespace vnic::rss::soft {

std::uint32_t calc_hash(const FiveTuple& tuple, std::span<const std::uint8_t> key, HashFunc hf, Protocol protocol) {
     if( key.size() < 40 )
     {
          assert( false && "RSS key is too short, expected at least 40 bytes" );
          //LOG_DEBUG_ERROR( "RSS key size is %u bytes, expected at least 40 bytes", keySize );
          return 0;
     }

     auto len{ 64 };
     // // Если включена RSS HashFunc для портов, изменим длину последовательности.
     // if( ( !!( hashFuncMask & ETH_RSS_UDP ) && tuple.iproto == IPPROTO_UDP ) ||
     //     ( !!( hashFuncMask & ETH_RSS_TCP ) && tuple.iproto == IPPROTO_TCP ) )
     // {
     //      len += 32;
     // }
     return key320_hash_calc( reinterpret_cast< const std::uint32_t* >( key.data() ), 0,
                              reinterpret_cast< const std::uint8_t* >( &tuple ), len );
}

} // namespace vnic::rss::soft
