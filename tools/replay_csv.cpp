// Replays a Logic 2 digital CSV export ("Time [s],<ch>,<ch>,...") through the quadrature decoder
// and prints the resulting segments. Used to check the decoder against real captures without
// loading it into Logic.
//
//   quadrature_replay <digital.csv> --rate <Hz> [--columns A,B[,Z]] [--decoding 4|2|1]
//                     [--index mark|reset|ignore] [--invert] [--min-ms <ms>] [--group-ms <ms>]
//
// --columns picks the CSV data columns (0-based, after the time column); Z is optional.
// --min-ms hides count segments shorter than the given duration in the listing (the summary still
// counts everything). --group-ms groups quick successive counts into moves, as the analyzer's
// "Moves and holds" results do, with the given hold time.

#include "MoveGrouper.h"
#include "QuadratureDecoder.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace quadrature;

namespace
{
    const char* TypeName( const Segment& segment )
    {
        switch( segment.type )
        {
        case SegmentType::Count:
            return "count";
        case SegmentType::Index:
            return "index";
        case SegmentType::Error:
            return "error";
        case SegmentType::Move:
            return segment.delta == 0 ? "jitter" : "move";
        }
        return "?";
    }

    int Usage()
    {
        std::cerr << "usage: quadrature_replay <digital.csv> --rate <Hz> [--columns 0,1[,2]] [--decoding 4|2|1]"
                     " [--index mark|reset|ignore] [--invert] [--min-ms 0] [--group-ms 20]\n";
        return 2;
    }
}

int main( int argc, char** argv )
{
    if( argc < 2 )
        return Usage();

    std::string path = argv[ 1 ];
    double rate = 0;
    std::vector<int> columns = { 0, 1 };
    DecoderConfig config;
    double min_ms = 0;
    double group_ms = 0;

    for( int i = 2; i < argc; ++i )
    {
        std::string arg = argv[ i ];
        auto next = [ & ]() -> std::string { return i + 1 < argc ? argv[ ++i ] : ""; };
        if( arg == "--rate" )
            rate = std::atof( next().c_str() );
        else if( arg == "--columns" )
        {
            columns.clear();
            std::stringstream list( next() );
            std::string item;
            while( std::getline( list, item, ',' ) )
                columns.push_back( std::atoi( item.c_str() ) );
        }
        else if( arg == "--decoding" )
            config.decoding = Decoding( std::atoi( next().c_str() ) );
        else if( arg == "--index" )
        {
            const std::string mode = next();
            config.index_mode = mode == "reset" ? IndexMode::Reset : ( mode == "ignore" ? IndexMode::Ignore : IndexMode::Mark );
        }
        else if( arg == "--invert" )
            config.invert = true;
        else if( arg == "--min-ms" )
            min_ms = std::atof( next().c_str() );
        else if( arg == "--group-ms" )
            group_ms = std::atof( next().c_str() );
        else
            return Usage();
    }
    if( rate <= 0 || columns.size() < 2 )
        return Usage();
    const bool use_z = columns.size() >= 3;
    if( !use_z )
        config.index_mode = IndexMode::Ignore;

    std::ifstream file( path );
    if( !file )
    {
        std::cerr << "cannot open " << path << "\n";
        return 1;
    }

    std::string line;
    std::getline( file, line ); // header

    QuadratureDecoder decoder( config );
    std::vector<Segment> segments;
    bool started = false;
    double t0 = 0;
    uint64_t last_sample = 0;
    std::vector<std::string> fields;

    while( std::getline( file, line ) )
    {
        fields.clear();
        std::stringstream row( line );
        std::string field;
        while( std::getline( row, field, ',' ) )
            fields.push_back( field );

        const double t = std::atof( fields[ 0 ].c_str() );
        const bool a = std::atoi( fields[ 1 + columns[ 0 ] ].c_str() ) != 0;
        const bool b = std::atoi( fields[ 1 + columns[ 1 ] ].c_str() ) != 0;
        const bool z = use_z && std::atoi( fields[ 1 + columns[ 2 ] ].c_str() ) != 0;

        if( !started )
        {
            t0 = t;
            decoder.Start( 0, a, b, z );
            started = true;
            continue;
        }

        // Logic writes one row per sample at which any channel changed.
        last_sample = uint64_t( std::llround( ( t - t0 ) * rate ) );
        decoder.Update( last_sample, a, b, z, segments );
    }
    decoder.Flush( last_sample + 1, segments );

    if( group_ms > 0 )
    {
        MoveGrouper grouper( uint64_t( group_ms * 1e-3 * rate ) );
        std::vector<Segment> grouped;
        for( const Segment& segment : segments )
            grouper.Add( segment, grouped );
        grouper.Flush( grouped );
        segments.swap( grouped );
    }

    size_t counts[ 4 ] = {};
    std::printf( "start_s,end_s,type,count,travel,delta,from,min,max,since_index,index_pulses,errors\n" );
    for( const Segment& segment : segments )
    {
        counts[ int( segment.type ) ]++;
        const double start = t0 + double( segment.start ) / rate;
        const double end = t0 + double( segment.end ) / rate;
        if( segment.type == SegmentType::Count && ( end - start ) * 1000.0 < min_ms )
            continue;
        std::printf( "%.9f,%.9f,%s,%lld,%lld,%lld,%lld,%lld,%lld,%s,%u,%u\n", start, end, TypeName( segment ), ( long long )segment.count,
                     ( long long )segment.travel, ( long long )segment.delta, ( long long )segment.from_count,
                     ( long long )segment.min_count, ( long long )segment.max_count,
                     segment.has_since_index ? std::to_string( segment.since_index ).c_str() : "", segment.index_pulses, segment.errors );
    }

    std::fprintf( stderr, "segments: %zu count, %zu move, %zu index, %zu error; final count %lld\n", counts[ int( SegmentType::Count ) ],
                  counts[ int( SegmentType::Move ) ], counts[ int( SegmentType::Index ) ], counts[ int( SegmentType::Error ) ],
                  ( long long )decoder.Count() );
    return 0;
}
