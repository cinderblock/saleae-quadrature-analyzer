#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "EncoderWaveform.h"
#include "MoveGrouper.h"
#include "QuadratureDecoder.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <vector>

using namespace quadrature;

namespace
{
    int64_t FloorDiv( int64_t a, int64_t b )
    {
        return a / b - ( ( a % b != 0 ) && ( ( a < 0 ) != ( b < 0 ) ) );
    }

    // Decode `loops` of the synthetic program, feeding every distinct edge sample like the analyzer does.
    std::vector<Segment> DecodeProgram( double sample_rate, const DecoderConfig& config, double loops, uint64_t* tick = nullptr )
    {
        EncoderWaveform wave( sample_rate );
        QuadratureDecoder decoder( config );
        bool level[ CHANNEL_COUNT ];
        for( int c = 0; c < CHANNEL_COUNT; ++c )
            level[ c ] = wave.InitialState( c );
        decoder.Start( 0, level[ CHANNEL_A ], level[ CHANNEL_B ], level[ CHANNEL_Z ] );

        const uint64_t total = uint64_t( loops * EncoderWaveform::ProgramDuration() * sample_rate );
        std::vector<Transition> transitions;
        std::vector<Segment> segments;
        uint64_t generated = 0;
        uint64_t first_end = 0;
        while( generated < total )
        {
            transitions.clear();
            generated = wave.GenerateNext( transitions );
            if( first_end == 0 )
                first_end = generated;
            for( size_t i = 0; i < transitions.size(); )
            {
                const uint64_t sample = transitions[ i ].sample;
                for( ; i < transitions.size() && transitions[ i ].sample == sample; ++i )
                    level[ transitions[ i ].channel ] = !level[ transitions[ i ].channel ];
                decoder.Update( sample, level[ CHANNEL_A ], level[ CHANNEL_B ], level[ CHANNEL_Z ], segments );
            }
        }
        decoder.Flush( generated, segments );
        if( tick )
            *tick = first_end;
        return segments;
    }

    const Segment* CountAt( const std::vector<Segment>& segments, uint64_t sample )
    {
        auto it = std::upper_bound( segments.begin(), segments.end(), sample,
                                    []( uint64_t s, const Segment& segment ) { return s < segment.start; } );
        if( it == segments.begin() )
            return nullptr;
        --it;
        return sample < it->end ? &*it : nullptr;
    }
}

TEST_CASE( "Gray states" )
{
    CHECK( QuadratureDecoder::State( false, false ) == 0 );
    CHECK( QuadratureDecoder::State( true, false ) == 1 );
    CHECK( QuadratureDecoder::State( true, true ) == 2 );
    CHECK( QuadratureDecoder::State( false, true ) == 3 );
}

TEST_CASE( "Decoder counts forward and back, flags skipped states, and inverts" )
{
    std::vector<Segment> out;

    SUBCASE( "X4 forward then back" )
    {
        QuadratureDecoder decoder( DecoderConfig{} );
        decoder.Start( 0, false, false, false );
        decoder.Update( 10, true, false, false, out ); // +1
        decoder.Update( 20, true, true, false, out );  // +2
        decoder.Update( 30, false, true, false, out ); // +3
        decoder.Update( 40, true, true, false, out );  // back to +2
        decoder.Flush( 50, out );
        REQUIRE( out.size() == 5 );
        const int64_t counts[] = { 0, 1, 2, 3, 2 };
        for( int i = 0; i < 5; ++i )
            CHECK( out[ i ].count == counts[ i ] );
        CHECK( out[ 0 ].start == 0 );
        CHECK( out[ 0 ].end == 10 );
        CHECK( out[ 0 ].direction == 1 );
        CHECK( out[ 3 ].direction == -1 );
        CHECK( out[ 4 ].travel == 2 );
    }

    SUBCASE( "A and B changing together is an error and does not count" )
    {
        QuadratureDecoder decoder( DecoderConfig{} );
        decoder.Start( 0, false, false, false );
        decoder.Update( 10, true, true, false, out );  // 00 -> 11 skips a state
        decoder.Update( 20, false, true, false, out ); // 11 -> 01 is +1 from the skipped position
        decoder.Flush( 30, out );
        REQUIRE( out.size() == 4 );
        CHECK( out[ 0 ].type == SegmentType::Count );
        CHECK( out[ 0 ].end == 10 );
        CHECK( out[ 1 ].type == SegmentType::Error );
        CHECK( out[ 1 ].start == 10 );
        CHECK( out[ 1 ].end == 11 );
        CHECK( out[ 2 ].count == 0 );
        CHECK( out[ 2 ].start == 11 );
        CHECK( out[ 3 ].count == 1 );
    }

    SUBCASE( "invert" )
    {
        DecoderConfig config;
        config.invert = true;
        QuadratureDecoder decoder( config );
        decoder.Start( 0, false, false, false );
        decoder.Update( 10, true, false, false, out );
        decoder.Flush( 20, out );
        REQUIRE( out.size() == 2 );
        CHECK( out[ 1 ].count == -1 );
    }

    SUBCASE( "index pulses report travel since the previous one, and can reset the count" )
    {
        DecoderConfig config;
        config.index_mode = IndexMode::Reset;
        QuadratureDecoder decoder( config );
        decoder.Start( 0, false, false, false );
        bool a = false, b = false;
        uint64_t t = 0;
        auto step = [ & ]( bool z )
        {
            // One forward X4 step.
            const int state = QuadratureDecoder::State( a, b );
            static const bool kA[ 4 ] = { false, true, true, false };
            static const bool kB[ 4 ] = { false, false, true, true };
            a = kA[ ( state + 1 ) & 3 ];
            b = kB[ ( state + 1 ) & 3 ];
            t += 10;
            decoder.Update( t, a, b, z, out );
        };
        for( int i = 0; i < 7; ++i )
            step( false );
        step( true ); // index at count 8
        for( int i = 0; i < 3; ++i )
            step( false );
        decoder.Update( t + 5, a, b, false, out ); // index ends
        for( int i = 0; i < 4; ++i )
            step( false );
        step( true ); // next index 8 counts later
        decoder.Flush( t + 10, out );

        std::vector<Segment> indexes;
        for( const Segment& segment : out )
            if( segment.type == SegmentType::Index )
                indexes.push_back( segment );
        REQUIRE( indexes.size() == 2 );
        CHECK_FALSE( indexes[ 0 ].has_since_index );
        CHECK( indexes[ 0 ].count == 0 ); // reset
        CHECK( indexes[ 1 ].has_since_index );
        CHECK( indexes[ 1 ].since_index == 8 );
        CHECK( out.back().count == 0 );
        CHECK( out.back().travel == 16 );
    }
}

TEST_CASE( "Decoding the synthetic program matches the commanded count exactly" )
{
    const double rates[] = { 1e6, 4e6, 6.25e6, 250e6 };
    for( double rate : rates )
    {
        CAPTURE( rate );
        uint64_t tick = 0;
        DecoderConfig config;
        config.index_mode = IndexMode::Mark;
        const std::vector<Segment> segments = DecodeProgram( rate, config, 2.0, &tick );

        // The segments tile time without gaps or overlaps.
        for( size_t i = 1; i < segments.size(); ++i )
            CHECK( segments[ i ].start == segments[ i - 1 ].end );

        size_t errors = 0;
        size_t indexes = 0;
        for( const Segment& segment : segments )
        {
            errors += segment.type == SegmentType::Error;
            indexes += segment.type == SegmentType::Index;
        }
        CHECK( errors == 0 );
        // Each loop enters count 400 twice and count 0 twice; it starts already on the index at 0.
        CHECK( indexes == 8 );

        // At random times, the count is the program's count at the start of that tick.
        std::mt19937 random( 1234 );
        const uint64_t total = segments.back().end;
        std::uniform_int_distribution<uint64_t> pick( 0, total - 1 );
        for( int i = 0; i < 400; ++i )
        {
            const uint64_t sample = pick( random );
            const Segment* segment = CountAt( segments, sample );
            REQUIRE( segment != nullptr );
            if( segment->type != SegmentType::Count )
                continue;
            const uint64_t tick_start = ( sample / tick ) * tick;
            CAPTURE( sample );
            CHECK( segment->count == EncoderWaveform::CountAt( double( tick_start ) / rate ) );
        }
    }
}

TEST_CASE( "X2 and X1 decoding follow the X4 count" )
{
    const double rate = 4e6;
    DecoderConfig x4;
    DecoderConfig x2;
    x2.decoding = Decoding::X2;
    DecoderConfig x1;
    x1.decoding = Decoding::X1;
    const std::vector<Segment> s4 = DecodeProgram( rate, x4, 1.0 );
    const std::vector<Segment> s2 = DecodeProgram( rate, x2, 1.0 );
    const std::vector<Segment> s1 = DecodeProgram( rate, x1, 1.0 );

    std::mt19937 random( 99 );
    std::uniform_int_distribution<uint64_t> pick( 0, s4.back().end - 2 );
    for( int i = 0; i < 400; ++i )
    {
        const uint64_t sample = pick( random );
        const Segment* a = CountAt( s4, sample );
        const Segment* b = CountAt( s2, sample );
        const Segment* c = CountAt( s1, sample );
        REQUIRE( a );
        REQUIRE( b );
        REQUIRE( c );
        if( a->type != SegmentType::Count || b->type != SegmentType::Count || c->type != SegmentType::Count )
            continue;
        CAPTURE( sample );
        CHECK( b->count == FloorDiv( a->count + 1, 2 ) );
        CHECK( c->count == FloorDiv( a->count + 3, 4 ) );
    }
}

TEST_CASE( "Index reset restarts the count at each index pulse" )
{
    DecoderConfig config;
    config.index_mode = IndexMode::Reset;
    const double rate = 4e6;
    const std::vector<Segment> segments = DecodeProgram( rate, config, 1.0 );

    // Hold at 600 after passing the index at 400: the count reads 200, travel still 600.
    const Segment* hold = CountAt( segments, uint64_t( 0.0135 * rate ) );
    REQUIRE( hold );
    CHECK( hold->count == 200 );
    CHECK( hold->travel == 600 );

    // Every index pulse reads 0.
    for( const Segment& segment : segments )
        if( segment.type == SegmentType::Index )
            CHECK( segment.count == 0 );
}

TEST_CASE( "A move is emitted once the count has held for the hold time, not when the hold ends" )
{
    auto count = []( uint64_t start, uint64_t end, int64_t value )
    {
        Segment segment;
        segment.start = start;
        segment.end = end;
        segment.count = value;
        segment.travel = value;
        return segment;
    };

    MoveGrouper grouper( 100 );
    std::vector<Segment> out;
    grouper.Add( count( 0, 1000, 0 ), out ); // hold
    grouper.Add( count( 1000, 1010, 1 ), out );
    grouper.Add( count( 1010, 1020, 2 ), out );
    REQUIRE( out.size() == 1 );

    // Count 3 began at 1020 and is still open.
    grouper.Tick( 1100, 1020, 3, 3, out );
    CHECK( out.size() == 1 ); // only held 80 samples so far
    grouper.Tick( 1120, 1020, 3, 3, out );
    REQUIRE( out.size() == 2 );
    CHECK( out[ 1 ].type == SegmentType::Move );
    CHECK( out[ 1 ].start == 1000 );
    CHECK( out[ 1 ].end == 1020 );
    CHECK( out[ 1 ].from_count == 0 );
    CHECK( out[ 1 ].count == 3 );

    // When the hold finally ends, it passes through as a hold.
    grouper.Add( count( 1020, 5000, 3 ), out );
    REQUIRE( out.size() == 3 );
    CHECK( out[ 2 ].type == SegmentType::Count );
    CHECK( out[ 2 ].count == 3 );
}

TEST_CASE( "After a flush, the continuing span stays a hold however short it is" )
{
    QuadratureDecoder decoder( DecoderConfig{} );
    decoder.Start( 0, false, false, false );
    std::vector<Segment> segments;
    decoder.Update( 10, true, false, false, segments );  // count 1 from 10
    decoder.Flush( 5000, segments );                     // waiting for data: show count 1 up to 5000
    decoder.Update( 5005, true, true, false, segments ); // more data: count 2 at 5005
    decoder.Flush( 9000, segments );

    REQUIRE( segments.size() == 4 );
    CHECK( segments[ 1 ].end == 5000 );
    CHECK_FALSE( segments[ 1 ].continuation );
    CHECK( segments[ 2 ].start == 5000 );
    CHECK( segments[ 2 ].end == 5005 );
    CHECK( segments[ 2 ].count == 1 );
    CHECK( segments[ 2 ].continuation );
    CHECK_FALSE( segments[ 3 ].continuation );

    MoveGrouper grouper( 100 );
    std::vector<Segment> out;
    for( const Segment& segment : segments )
        grouper.Add( segment, out );
    grouper.Flush( out );
    // Move 0 -> 1 (the short start), hold 1, its 5-sample continuation (still a hold, not a move), hold 2.
    REQUIRE( out.size() == 4 );
    CHECK( out[ 0 ].type == SegmentType::Move );
    for( size_t i = 1; i < out.size(); ++i )
        CHECK( out[ i ].type == SegmentType::Count );
    CHECK( out[ 2 ].continuation );
}

TEST_CASE( "Moves and holds" )
{
    const double rate = 4e6;
    DecoderConfig config;
    const std::vector<Segment> segments = DecodeProgram( rate, config, 2.0 );

    MoveGrouper grouper( uint64_t( 1e-3 * rate ) );
    std::vector<Segment> grouped;
    for( const Segment& segment : segments )
        grouper.Add( segment, grouped );
    grouper.Flush( grouped );

    // The second loop, fully preceded by a hold: hold 0, move to 600 (absorbing the index at 400),
    // hold 600, jitter 600..601, hold 600, move to -200 (two indexes), hold -200, move to 0 (one index).
    const double loop = EncoderWaveform::ProgramDuration();
    auto at = [ & ]( double seconds ) -> const Segment*
    {
        const uint64_t sample = uint64_t( ( loop + seconds ) * rate );
        for( const Segment& segment : grouped )
            if( segment.start <= sample && sample < segment.end )
                return &segment;
        return nullptr;
    };

    const Segment* hold0 = at( 0.001 );
    REQUIRE( hold0 );
    CHECK( hold0->type == SegmentType::Count );
    CHECK( hold0->count == 0 );

    const Segment* out = at( 0.007 );
    REQUIRE( out );
    CHECK( out->type == SegmentType::Move );
    CHECK( out->from_count == 0 );
    CHECK( out->count == 600 );
    CHECK( out->delta == 600 );
    CHECK( out->index_pulses == 1 );

    const Segment* jitter = at( 0.0165 );
    REQUIRE( jitter );
    CHECK( jitter->type == SegmentType::Move );
    CHECK( jitter->delta == 0 );
    CHECK( jitter->min_count == 600 );
    CHECK( jitter->max_count == 601 );

    const Segment* back = at( 0.025 );
    REQUIRE( back );
    CHECK( back->type == SegmentType::Move );
    CHECK( back->from_count == 600 );
    CHECK( back->count == -200 );
    CHECK( back->delta == -800 );
    CHECK( back->index_pulses == 2 );
    CHECK( back->min_count == -200 );
    CHECK( back->max_count == 600 );

    const Segment* hold = at( 0.0315 );
    REQUIRE( hold );
    CHECK( hold->type == SegmentType::Count );
    CHECK( hold->count == -200 );
}
