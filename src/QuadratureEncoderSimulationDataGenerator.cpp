#include "QuadratureEncoderSimulationDataGenerator.h"

#include "QuadratureEncoderAnalyzerSettings.h"

#include <AnalyzerHelpers.h>

QuadratureEncoderSimulationDataGenerator::QuadratureEncoderSimulationDataGenerator()
    : mSettings( nullptr ), mSimulationSampleRateHz( 0 ), mGenerated( 0 ), mChannels()
{
}

QuadratureEncoderSimulationDataGenerator::~QuadratureEncoderSimulationDataGenerator()
{
}

void QuadratureEncoderSimulationDataGenerator::Initialize( U32 simulation_sample_rate, QuadratureEncoderAnalyzerSettings* settings )
{
    mSimulationSampleRateHz = simulation_sample_rate;
    mSettings = settings;

    // One index pulse per 400 counts (a 100-line encoder at X4), whatever the decoding setting.
    mWaveform.reset( new quadrature::EncoderWaveform( simulation_sample_rate, 400 ) );
    mGenerated = 0;

    Channel* channels[ quadrature::CHANNEL_COUNT ] = { &settings->mA, &settings->mB, &settings->mZ };
    for( int i = 0; i < quadrature::CHANNEL_COUNT; ++i )
    {
        mChannels[ i ] = nullptr;
        if( *channels[ i ] == UNDEFINED_CHANNEL )
            continue;
        const BitState initial = mWaveform->InitialState( i ) ? BIT_HIGH : BIT_LOW;
        mChannels[ i ] = mGroup.Add( *channels[ i ], simulation_sample_rate, initial );
    }
}

U32 QuadratureEncoderSimulationDataGenerator::GenerateSimulationData( U64 largest_sample_requested, U32 sample_rate,
                                                                      SimulationChannelDescriptor** simulation_channels )
{
    const U64 adjusted_largest_sample_requested =
        AnalyzerHelpers::AdjustSimulationTargetSample( largest_sample_requested, sample_rate, mSimulationSampleRateHz );

    while( mGenerated < adjusted_largest_sample_requested )
    {
        mTransitions.clear();
        mGenerated = mWaveform->GenerateNext( mTransitions );

        for( const quadrature::Transition& transition : mTransitions )
        {
            SimulationChannelDescriptor* channel = mChannels[ transition.channel ];
            if( channel == nullptr )
                continue;
            const U64 current = channel->GetCurrentSampleNumber();
            if( transition.sample > current )
                channel->Advance( U32( transition.sample - current ) );
            channel->Transition();
        }

        // Every channel must reach the end of the generated span, including ones that did not change.
        for( SimulationChannelDescriptor* channel : mChannels )
        {
            if( channel == nullptr )
                continue;
            const U64 current = channel->GetCurrentSampleNumber();
            if( mGenerated > current )
                channel->Advance( U32( mGenerated - current ) );
        }
    }

    *simulation_channels = mGroup.GetArray();
    return mGroup.GetCount();
}
