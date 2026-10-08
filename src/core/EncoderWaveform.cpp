#include "EncoderWaveform.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace quadrature
{
    namespace
    {
        const double kPi = 3.14159265358979323846;

        struct Phase
        {
            double start; // seconds into the program
            double duration;
            double from; // X4 counts; holds sit mid-state (x.5) so they never touch an edge
            double to;
            bool jitter; // dither across the edge at `from` instead of moving
        };

        // Moves use a raised-cosine profile (peak about 94k counts/s); holds are moves with from == to.
        const Phase kProgram[] = {
            { 0.000, 0.002, 0.5, 0.5, false },       { 0.002, 0.010, 0.5, 600.5, false },   { 0.012, 0.003, 600.5, 600.5, false },
            { 0.015, 0.003, 601.0, 601.0, true },    { 0.018, 0.002, 600.5, 600.5, false }, { 0.020, 0.010, 600.5, -199.5, false },
            { 0.030, 0.003, -199.5, -199.5, false }, { 0.033, 0.003, -199.5, 0.5, false },
        };
        const double kProgramDuration = 0.036;

        int64_t FloorMod( int64_t value, int64_t modulus )
        {
            const int64_t r = value % modulus;
            return r < 0 ? r + modulus : r;
        }
    }

    EncoderWaveform::EncoderWaveform( double sample_rate, int64_t counts_per_index )
        : mSampleRate( sample_rate ), mCountsPerIndex( counts_per_index )
    {
        // Evaluate the program every half microsecond (or every sample, if slower): the fastest move
        // takes over 10 us per count, so no tick ever skips a state.
        mTickSamples = std::max<uint64_t>( 1, uint64_t( std::llround( 0.5e-6 * sample_rate ) ) );
        mCount = CountAt( 0 );
        Levels( mCount, mLevel );
    }

    double EncoderWaveform::ProgramDuration()
    {
        return kProgramDuration;
    }

    double EncoderWaveform::PositionAt( double seconds )
    {
        double t = std::fmod( seconds, kProgramDuration );
        if( t < 0 )
            t += kProgramDuration;

        const Phase* phase = &kProgram[ 0 ];
        for( const Phase& candidate : kProgram )
            if( t >= candidate.start )
                phase = &candidate;

        const double u = std::min( 1.0, std::max( 0.0, ( t - phase->start ) / phase->duration ) );
        if( phase->jitter )
            return phase->from + 0.6 * std::sin( 2.0 * kPi * 3.0 * u ); // three dithers across the edge
        return phase->from + ( phase->to - phase->from ) * ( 1.0 - std::cos( kPi * u ) ) / 2.0;
    }

    int64_t EncoderWaveform::CountAt( double seconds )
    {
        return int64_t( std::floor( PositionAt( seconds ) ) );
    }

    bool EncoderWaveform::InitialState( int channel ) const
    {
        bool levels[ CHANNEL_COUNT ];
        Levels( CountAt( 0 ), levels );
        return levels[ channel ];
    }

    void EncoderWaveform::Levels( int64_t count, bool levels[ CHANNEL_COUNT ] ) const
    {
        // Forward Gray sequence: 00 -> 10 -> 11 -> 01 (A leads B).
        static const bool kA[ 4 ] = { false, true, true, false };
        static const bool kB[ 4 ] = { false, false, true, true };
        const int64_t state = FloorMod( count, 4 );
        levels[ CHANNEL_A ] = kA[ state ];
        levels[ CHANNEL_B ] = kB[ state ];
        levels[ CHANNEL_Z ] = FloorMod( count, mCountsPerIndex ) == 0;
    }

    uint64_t EncoderWaveform::GenerateNext( std::vector<Transition>& out )
    {
        const uint64_t sample = mNextSample;
        mNextSample += mTickSamples;

        const int64_t target = CountAt( double( sample ) / mSampleRate );

        // Step one count at a time so every intermediate state appears; if a tick ever had to cover
        // several counts, they land on successive samples.
        uint64_t at = sample;
        while( mCount != target )
        {
            mCount += target > mCount ? 1 : -1;
            bool levels[ CHANNEL_COUNT ];
            Levels( mCount, levels );
            for( int channel = 0; channel < CHANNEL_COUNT; ++channel )
            {
                if( levels[ channel ] != mLevel[ channel ] )
                {
                    out.push_back( Transition{ at, channel } );
                    mLevel[ channel ] = levels[ channel ];
                }
            }
            ++at;
        }
        mNextSample = std::max( mNextSample, at );
        return mNextSample;
    }
}
