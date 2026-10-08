#include "QuadratureEncoderAnalyzer.h"

#include <AnalyzerChannelData.h>

#include "MoveGrouper.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace
{
    const char* const kAnalyzerName = "Quadrature Encoder";

    const char* FrameTypeName( const quadrature::Segment& segment )
    {
        switch( segment.type )
        {
        case quadrature::SegmentType::Count:
            return "count";
        case quadrature::SegmentType::Index:
            return "index";
        case quadrature::SegmentType::Error:
            return "error";
        case quadrature::SegmentType::Move:
            return segment.delta == 0 ? "jitter" : "move";
        }
        return "count";
    }
}

QuadratureEncoderAnalyzer::QuadratureEncoderAnalyzer()
    : Analyzer2(), mSettings(), mSimulationInitialized( false ), mSampleRateHz( 0 ), mUseZ( false ), mFilled( 0 )
{
    SetAnalyzerSettings( &mSettings );
    UseFrameV2();
}

QuadratureEncoderAnalyzer::~QuadratureEncoderAnalyzer()
{
    KillThread();
    mFlusher.Stop();
}

void QuadratureEncoderAnalyzer::SetupResults()
{
    // A previous run's flusher must not touch the results being replaced.
    mFlusher.Stop();

    // SetupResults is called each time the analyzer is run. Because the same instance can be used for multiple runs, we need to clear the
    // results each time.
    mResults.reset( new QuadratureEncoderAnalyzerResults( this, &mSettings ) );
    SetAnalyzerResults( mResults.get() );
    mResults->AddChannelBubblesWillAppearOn( mSettings.mA );
}

void QuadratureEncoderAnalyzer::WorkerThread()
{
    mSampleRateHz = GetSampleRate();

    // A, B and (optionally) Z, read together so edges that land on the same sample are seen together.
    mUseZ = mSettings.mZ != UNDEFINED_CHANNEL;
    std::vector<AnalyzerChannelData*> channels = { GetAnalyzerChannelData( mSettings.mA ), GetAnalyzerChannelData( mSettings.mB ) };
    if( mUseZ )
        channels.push_back( GetAnalyzerChannelData( mSettings.mZ ) );

    U64 start = 0;
    for( AnalyzerChannelData* data : channels )
        start = std::max( start, data->GetSampleNumber() );
    bool level[ 3 ] = {};
    for( size_t i = 0; i < channels.size(); ++i )
    {
        channels[ i ]->AdvanceToAbsPosition( start );
        level[ i ] = channels[ i ]->GetBitState() == BIT_HIGH;
    }

    {
        std::lock_guard<std::mutex> lock( mFlusher.Lock() );
        mDecoder.reset( new quadrature::QuadratureDecoder( mSettings.MakeDecoderConfig() ) );
        mDecoder->Start( start, level[ 0 ], level[ 1 ], level[ 2 ] );
        mGrouper.reset();
        if( ResultMode( mSettings.mResultMode ) == ResultMode::MovesAndHolds )
            mGrouper.reset( new quadrature::MoveGrouper( U64( double( mSettings.mHoldTime ) * 1e-3 * double( mSampleRateHz ) ) ) );
        mFilled = start;
    }
    mFlusher.Start( [ this ]() { FlushOpen(); }, std::chrono::milliseconds( 100 ), std::chrono::milliseconds( 2000 ) );

    // Edges are gathered a chunk at a time from every channel and then replayed in sample order.
    const U64 chunk = std::max<U64>( 1, U64( 1e-3 * double( mSampleRateHz ) ) );
    std::vector<std::pair<U64, int>> edges;
    U64 filled = start;

    for( ;; )
    {
        // Advancing blocks until the capture has data that far, which is how a live capture is
        // followed, and also how the end of a capture looks: the flusher shows the open span then.
        mFlusher.Waiting();
        const U64 target = filled + chunk;
        edges.clear();
        for( size_t i = 0; i < channels.size(); ++i )
        {
            AnalyzerChannelData* data = channels[ i ];
            while( data->WouldAdvancingToAbsPositionCauseTransition( target ) )
            {
                data->AdvanceToNextEdge();
                edges.emplace_back( data->GetSampleNumber(), int( i ) );
            }
            data->AdvanceToAbsPosition( target );
        }
        filled = target;
        mFlusher.Progressed();
        std::sort( edges.begin(), edges.end() );

        std::lock_guard<std::mutex> lock( mFlusher.Lock() );
        mSegments.clear();
        for( size_t i = 0; i < edges.size(); )
        {
            const U64 sample = edges[ i ].first;
            for( ; i < edges.size() && edges[ i ].first == sample; ++i )
                level[ edges[ i ].second ] = !level[ edges[ i ].second ];
            mDecoder->Update( sample, level[ 0 ], level[ 1 ], level[ 2 ], mSegments );
        }
        mFilled = filled;
        for( const quadrature::Segment& segment : mSegments )
        {
            if( !segment.continuation )
            {
                mFlusher.Activity();
                break;
            }
        }
        EmitResults( false );
        ReportProgress( filled );
    }
}

void QuadratureEncoderAnalyzer::FlushOpen()
{
    // Runs on the flusher thread, holding its lock, while the worker waits for data.
    if( !mDecoder )
        return;
    mSegments.clear();
    mDecoder->Flush( mFilled, mSegments );
    EmitResults( true );
}

void QuadratureEncoderAnalyzer::EmitResults( bool flush )
{
    // Markers go on the channels for every index and error, even those a move absorbs.
    for( const quadrature::Segment& segment : mSegments )
    {
        if( segment.type == quadrature::SegmentType::Error )
            mResults->AddMarker( segment.start, AnalyzerResults::ErrorX, mSettings.mA );
        else if( segment.type == quadrature::SegmentType::Index && mUseZ )
            mResults->AddMarker( segment.start, AnalyzerResults::Dot, mSettings.mZ );
    }

    const std::vector<quadrature::Segment>* results = &mSegments;
    if( mGrouper )
    {
        mGrouped.clear();
        for( const quadrature::Segment& segment : mSegments )
            mGrouper->Add( segment, mGrouped );
        // Results that wait in the grouper are emitted as soon as they are known: Logic only takes
        // late results into its data table while the analyzer is still running.
        if( flush )
            mGrouper->Flush( mGrouped );
        else
            mGrouper->Tick( mFilled, mDecoder->OpenStart(), mDecoder->Count(), mDecoder->Travel(), mGrouped );
        results = &mGrouped;
    }

    for( const quadrature::Segment& segment : *results )
        EmitSegment( segment );

    if( !results->empty() )
        mResults->CommitResults();
}

void QuadratureEncoderAnalyzer::EmitSegment( const quadrature::Segment& segment )
{
    if( segment.end <= segment.start )
        return;

    mResults->AddFrame( QuadratureEncoderAnalyzerResults::EncodeFrame( segment ) );

    const double duration = double( segment.end - segment.start ) / double( mSampleRateHz );
    char position[ 80 ];
    mResults->FormatPosition( double( segment.count ), true, position, sizeof( position ) );

    FrameV2 frame_v2;
    frame_v2.AddInteger( "count", segment.count );
    frame_v2.AddString( "position", position );
    frame_v2.AddInteger( "travel", segment.travel );

    switch( segment.type )
    {
    case quadrature::SegmentType::Count:
        frame_v2.AddString( "direction", segment.direction > 0 ? "+" : ( segment.direction < 0 ? "-" : "" ) );
        // The speed implied by the count that ended this span: one count over the time spent here.
        frame_v2.AddDouble( "rate", duration > 0 ? double( segment.delta ) / duration : 0 );
        break;
    case quadrature::SegmentType::Index:
        if( segment.has_since_index )
            frame_v2.AddInteger( "since_index", segment.since_index );
        break;
    case quadrature::SegmentType::Error:
        break;
    case quadrature::SegmentType::Move:
        frame_v2.AddInteger( "from", segment.from_count );
        frame_v2.AddInteger( "delta", segment.delta );
        frame_v2.AddString( "direction", segment.direction > 0 ? "+" : ( segment.direction < 0 ? "-" : "" ) );
        frame_v2.AddDouble( "rate", duration > 0 ? double( segment.delta ) / duration : 0 );
        frame_v2.AddInteger( "min", segment.min_count );
        frame_v2.AddInteger( "max", segment.max_count );
        frame_v2.AddInteger( "index_pulses", segment.index_pulses );
        frame_v2.AddInteger( "errors", segment.errors );
        break;
    }
    mResults->AddFrameV2( frame_v2, FrameTypeName( segment ), segment.start, segment.end - 1 );
}

bool QuadratureEncoderAnalyzer::NeedsRerun()
{
    return false;
}

U32 QuadratureEncoderAnalyzer::GenerateSimulationData( U64 minimum_sample_index, U32 device_sample_rate,
                                                       SimulationChannelDescriptor** simulation_channels )
{
    if( mSimulationInitialized == false )
    {
        mSimulationDataGenerator.Initialize( GetSimulationSampleRate(), &mSettings );
        mSimulationInitialized = true;
    }

    return mSimulationDataGenerator.GenerateSimulationData( minimum_sample_index, device_sample_rate, simulation_channels );
}

U32 QuadratureEncoderAnalyzer::GetMinimumSampleRateHz()
{
    // The simulated encoder peaks near 100k counts/s: a count every 10 us.
    return 1000000;
}

const char* QuadratureEncoderAnalyzer::GetAnalyzerName() const
{
    return kAnalyzerName;
}

const char* GetAnalyzerName()
{
    return kAnalyzerName;
}

Analyzer* CreateAnalyzer()
{
    return new QuadratureEncoderAnalyzer();
}

void DestroyAnalyzer( Analyzer* analyzer )
{
    delete analyzer;
}
