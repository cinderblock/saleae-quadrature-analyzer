// Exercises the analyzer's simulation data generator against the real Analyzer SDK library, the
// way Logic's demo device calls it, and checks it reproduces the EncoderWaveform program.
//
// Logic's demo device only plays an analyzer's simulation when the analyzer is added in the UI
// before capturing, which the automation API cannot do, so this is how it gets tested.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "QuadratureEncoderAnalyzerSettings.h"
#include "QuadratureEncoderSimulationDataGenerator.h"
#include "EncoderWaveform.h"

#include <vector>

namespace
{
    void Check( bool with_z )
    {
        const U32 sample_rate = 4000000;

        QuadratureEncoderAnalyzerSettings settings;
        settings.mA = Channel( 0x1234, 5, DIGITAL_CHANNEL );
        settings.mB = Channel( 0x1234, 6, DIGITAL_CHANNEL );
        settings.mZ = with_z ? Channel( 0x1234, 7, DIGITAL_CHANNEL ) : UNDEFINED_CHANNEL;

        QuadratureEncoderSimulationDataGenerator generator;
        generator.Initialize( sample_rate, &settings );

        // Logic asks for data in increasing chunks.
        SimulationChannelDescriptor* channels = nullptr;
        U32 count = 0;
        const U64 target = U64( 2.5 * quadrature::EncoderWaveform::ProgramDuration() * sample_rate );
        for( U64 requested = 1000; requested < target; requested += 77777 )
            count = generator.GenerateSimulationData( requested, sample_rate, &channels );
        count = generator.GenerateSimulationData( target, sample_rate, &channels );

        REQUIRE( count == ( with_z ? 3u : 2u ) );
        const Channel expected[] = { settings.mA, settings.mB, settings.mZ };

        // Every channel is generated to the same point, at least as far as requested.
        const U64 reached = channels[ 0 ].GetCurrentSampleNumber();
        CHECK( reached >= target );
        for( U32 i = 0; i < count; ++i )
        {
            CAPTURE( i );
            CHECK( channels[ i ].GetChannel() == expected[ i ] );
            CHECK( channels[ i ].GetCurrentSampleNumber() == reached );
        }

        // Replay the same program independently: each channel must end at the same level.
        quadrature::EncoderWaveform reference( sample_rate, 400 );
        std::vector<quadrature::Transition> transitions;
        bool level[ quadrature::CHANNEL_COUNT ];
        for( int i = 0; i < quadrature::CHANNEL_COUNT; ++i )
            level[ i ] = reference.InitialState( i );
        U64 generated = 0;
        U64 toggles = 0;
        while( generated < reached )
        {
            transitions.clear();
            generated = reference.GenerateNext( transitions );
            for( const quadrature::Transition& transition : transitions )
                level[ transition.channel ] = !level[ transition.channel ];
            toggles += transitions.size();
        }
        REQUIRE( generated == reached );
        CHECK( toggles > 1000 );

        for( U32 i = 0; i < count; ++i )
        {
            CAPTURE( i );
            CHECK( ( channels[ i ].GetCurrentBitState() == BIT_HIGH ) == level[ i ] );
        }
    }
}

TEST_CASE( "Simulation generator plays the demo program on A, B and Z" )
{
    Check( true );
}

TEST_CASE( "Simulation generator works without Z" )
{
    Check( false );
}
