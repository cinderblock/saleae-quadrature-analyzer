#include "QuadratureEncoderAnalyzerResults.h"

#include <AnalyzerHelpers.h>

#include "QuadratureEncoderAnalyzer.h"
#include "QuadratureEncoderAnalyzerSettings.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace
{
    const char* const kArrow = "\xE2\x86\x92"; // U+2192

    // Trailing zeros (and a trailing decimal point) after printf("%.Nf").
    void TrimZeros( char* text )
    {
        if( std::strchr( text, '.' ) == nullptr )
            return;
        size_t length = std::strlen( text );
        while( length > 0 && text[ length - 1 ] == '0' )
            text[ --length ] = '\0';
        if( length > 0 && text[ length - 1 ] == '.' )
            text[ --length ] = '\0';
    }

    S64 Signed( U64 value )
    {
        S64 result;
        std::memcpy( &result, &value, sizeof( result ) );
        return result;
    }

    U64 Unsigned( S64 value )
    {
        U64 result;
        std::memcpy( &result, &value, sizeof( result ) );
        return result;
    }
}

QuadratureEncoderAnalyzerResults::QuadratureEncoderAnalyzerResults( QuadratureEncoderAnalyzer* analyzer,
                                                                    QuadratureEncoderAnalyzerSettings* settings )
    : AnalyzerResults(), mSettings( settings ), mAnalyzer( analyzer )
{
}

QuadratureEncoderAnalyzerResults::~QuadratureEncoderAnalyzerResults()
{
}

Frame QuadratureEncoderAnalyzerResults::EncodeFrame( const quadrature::Segment& segment )
{
    Frame frame;
    frame.mStartingSampleInclusive = S64( segment.start );
    frame.mEndingSampleInclusive = S64( segment.end ) - 1;
    frame.mType = U8( segment.type );
    frame.mData1 = Unsigned( segment.count );
    frame.mData2 = 0;
    frame.mFlags = 0;
    if( segment.direction > 0 )
        frame.mFlags |= FRAME_POSITIVE;
    if( segment.direction < 0 )
        frame.mFlags |= FRAME_NEGATIVE;

    switch( segment.type )
    {
    case quadrature::SegmentType::Count:
        frame.mData2 = Unsigned( segment.delta );
        break;
    case quadrature::SegmentType::Index:
        frame.mData2 = Unsigned( segment.since_index );
        if( segment.has_since_index )
            frame.mFlags |= FRAME_HAS_SINCE;
        break;
    case quadrature::SegmentType::Error:
        frame.mFlags |= DISPLAY_AS_ERROR_FLAG;
        break;
    case quadrature::SegmentType::Move:
        frame.mData2 = Unsigned( segment.from_count );
        if( segment.index_pulses > 0 )
            frame.mFlags |= FRAME_MOVE_INDEX;
        if( segment.errors > 0 )
            frame.mFlags |= FRAME_MOVE_ERRORS | DISPLAY_AS_WARNING_FLAG;
        if( segment.delta == 0 )
            frame.mFlags |= FRAME_MOVE_JITTER;
        break;
    }
    return frame;
}

double QuadratureEncoderAnalyzerResults::FrameSeconds( const Frame& frame ) const
{
    return double( frame.mEndingSampleInclusive - frame.mStartingSampleInclusive + 1 ) / double( mAnalyzer->GetSampleRate() );
}

void QuadratureEncoderAnalyzerResults::FormatPosition( double counts, bool with_unit, char* out, size_t size ) const
{
    char number[ 64 ];
    if( PositionUnits( mSettings->mUnits ) == PositionUnits::Counts )
    {
        std::snprintf( number, sizeof( number ), "%.0f", counts );
    }
    else
    {
        // Enough decimals to resolve one count in the chosen unit.
        const double step = std::fabs( mSettings->ToUnits( 1.0 ) );
        int decimals = 0;
        while( decimals < 9 && step * std::pow( 10.0, decimals ) < 1.0 )
            ++decimals;
        std::snprintf( number, sizeof( number ), "%.*f", decimals, mSettings->ToUnits( counts ) );
        TrimZeros( number );
    }
    if( std::strcmp( number, "-0" ) == 0 )
        std::snprintf( number, sizeof( number ), "0" );
    std::snprintf( out, size, "%s%s", number, with_unit ? mSettings->UnitSuffix() : "" );
}

void QuadratureEncoderAnalyzerResults::GenerateBubbleText( U64 frame_index, Channel& channel, DisplayBase display_base )
{
    ClearResultStrings();
    const Frame frame = GetFrame( frame_index );
    const S64 count = Signed( frame.mData1 );
    const S64 data2 = Signed( frame.mData2 );

    char position[ 64 ];
    char with_unit[ 80 ];
    char text[ 256 ];
    FormatPosition( double( count ), false, position, sizeof( position ) );
    FormatPosition( double( count ), true, with_unit, sizeof( with_unit ) );

    switch( quadrature::SegmentType( frame.mType ) )
    {
    case quadrature::SegmentType::Count:
        AddResultString( position );
        AddResultString( with_unit );
        std::snprintf( text, sizeof( text ), "Count %s", with_unit );
        AddResultString( text );
        break;

    case quadrature::SegmentType::Index:
        AddResultString( "Z" );
        AddResultString( "Index" );
        if( frame.mFlags & FRAME_HAS_SINCE )
        {
            std::snprintf( text, sizeof( text ), "Index (%+lld counts since last)", ( long long )data2 );
            AddResultString( text );
        }
        break;

    case quadrature::SegmentType::Error:
        AddResultString( "!" );
        AddResultString( "Skipped state" );
        AddResultString( "Skipped state: A and B changed together" );
        break;

    case quadrature::SegmentType::Move:
    {
        char from[ 64 ];
        FormatPosition( double( data2 ), false, from, sizeof( from ) );
        const char* warning = ( frame.mFlags & FRAME_MOVE_ERRORS ) ? " (skipped states)" : "";

        if( frame.mFlags & FRAME_MOVE_JITTER )
        {
            AddResultString( "~", position );
            std::snprintf( text, sizeof( text ), "Jitter at %s", with_unit );
            AddResultString( text );
            std::snprintf( text, sizeof( text ), "Jitter at %s for %.3g s%s", with_unit, FrameSeconds( frame ), warning );
            AddResultString( text );
            break;
        }

        AddResultString( position );
        std::snprintf( text, sizeof( text ), "%s %s", kArrow, with_unit );
        AddResultString( text );
        std::snprintf( text, sizeof( text ), "Move %s %s %s", from, kArrow, with_unit );
        AddResultString( text );

        // With index resets inside the move, from and to are on different count origins, so the
        // difference is not the distance moved; the data table has the travel.
        const bool reset_inside = ( frame.mFlags & FRAME_MOVE_INDEX ) &&
                                  quadrature::IndexMode( mSettings->mIndexMode ) == quadrature::IndexMode::Reset && mSettings->HasIndex();
        if( !reset_inside )
        {
            const double seconds = FrameSeconds( frame );
            const S64 delta = count - data2;
            char delta_text[ 80 ];
            FormatPosition( double( delta ), true, delta_text, sizeof( delta_text ) );
            std::snprintf( text, sizeof( text ), "Move %s %s %s (%s%s in %.3g s, %.4g counts/s)%s", from, kArrow, with_unit,
                           delta > 0 ? "+" : "", delta_text, seconds, double( delta ) / seconds, warning );
            AddResultString( text );
        }
        break;
    }
    }
}

void QuadratureEncoderAnalyzerResults::GenerateExportFile( const char* file, DisplayBase display_base, U32 export_type_user_id )
{
    std::ofstream file_stream( file, std::ios::out );

    const U64 trigger_sample = mAnalyzer->GetTriggerSample();
    const U32 sample_rate = mAnalyzer->GetSampleRate();

    file_stream << "Time [s],Duration [s],Type,Count,Position,From,Since index,Direction" << std::endl;

    const U64 num_frames = GetNumFrames();
    for( U64 i = 0; i < num_frames; i++ )
    {
        const Frame frame = GetFrame( i );
        const S64 count = Signed( frame.mData1 );
        const S64 data2 = Signed( frame.mData2 );

        char time_str[ 128 ];
        AnalyzerHelpers::GetTimeString( frame.mStartingSampleInclusive, trigger_sample, sample_rate, time_str, 128 );
        char position[ 64 ];
        FormatPosition( double( count ), false, position, sizeof( position ) );
        const char* direction = ( frame.mFlags & FRAME_POSITIVE ) ? "+" : ( ( frame.mFlags & FRAME_NEGATIVE ) ? "-" : "" );

        file_stream << time_str << "," << FrameSeconds( frame ) << ",";
        switch( quadrature::SegmentType( frame.mType ) )
        {
        case quadrature::SegmentType::Count:
            file_stream << "count," << count << "," << position << ",,," << direction;
            break;
        case quadrature::SegmentType::Index:
            file_stream << "index," << count << "," << position << ",,";
            if( frame.mFlags & FRAME_HAS_SINCE )
                file_stream << data2;
            file_stream << ",";
            break;
        case quadrature::SegmentType::Error:
            file_stream << "error," << count << "," << position << ",,,";
            break;
        case quadrature::SegmentType::Move:
            file_stream << ( ( frame.mFlags & FRAME_MOVE_JITTER ) ? "jitter," : "move," ) << count << "," << position << "," << data2
                        << ",," << direction;
            break;
        }
        file_stream << std::endl;

        if( UpdateExportProgressAndCheckForCancel( i, num_frames ) == true )
        {
            file_stream.close();
            return;
        }
    }

    file_stream.close();
}

void QuadratureEncoderAnalyzerResults::GenerateFrameTabularText( U64 frame_index, DisplayBase display_base )
{
#ifdef SUPPORTS_PROTOCOL_SEARCH
    ClearTabularText();
    const Frame frame = GetFrame( frame_index );
    char with_unit[ 80 ];
    FormatPosition( double( Signed( frame.mData1 ) ), true, with_unit, sizeof( with_unit ) );

    switch( quadrature::SegmentType( frame.mType ) )
    {
    case quadrature::SegmentType::Count:
        AddTabularText( with_unit );
        break;
    case quadrature::SegmentType::Index:
        AddTabularText( "Index at ", with_unit );
        break;
    case quadrature::SegmentType::Error:
        AddTabularText( "Skipped state at ", with_unit );
        break;
    case quadrature::SegmentType::Move:
    {
        char from[ 64 ];
        FormatPosition( double( Signed( frame.mData2 ) ), false, from, sizeof( from ) );
        if( frame.mFlags & FRAME_MOVE_JITTER )
            AddTabularText( "Jitter at ", with_unit );
        else
            AddTabularText( "Move ", from, " ", kArrow, " ", with_unit );
        break;
    }
    }
#endif
}

void QuadratureEncoderAnalyzerResults::GeneratePacketTabularText( U64 packet_id, DisplayBase display_base )
{
    // not supported
}

void QuadratureEncoderAnalyzerResults::GenerateTransactionTabularText( U64 transaction_id, DisplayBase display_base )
{
    // not supported
}
