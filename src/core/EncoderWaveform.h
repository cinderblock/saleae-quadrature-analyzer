#ifndef QUADRATURE_ENCODER_WAVEFORM_H
#define QUADRATURE_ENCODER_WAVEFORM_H

#include <cstdint>
#include <vector>

namespace quadrature
{
    enum Channel
    {
        CHANNEL_A = 0,
        CHANNEL_B = 1,
        CHANNEL_Z = 2,
        CHANNEL_COUNT = 3
    };

    struct Transition
    {
        uint64_t sample;
        int channel; // Channel; it toggles at `sample`
    };

    // Synthesizes A/B/Z signals of an incremental encoder following a looping demo program with a
    // known position at every instant: holds, smooth moves both ways, and dithering on an edge.
    // Z pulses high for the quarter cycle at every multiple of `counts_per_index` X4 counts.
    //
    // Used for Logic's simulation device and for the unit tests.
    class EncoderWaveform
    {
      public:
        EncoderWaveform( double sample_rate, int64_t counts_per_index = 400 );

        bool InitialState( int channel ) const;

        // Generate the next slice of the program. Toggles are appended in sample order.
        // Returns the sample at which the generated span ends.
        uint64_t GenerateNext( std::vector<Transition>& out );

        // Position in X4 counts (continuous) at a time within the program; the X4 count is its floor.
        static double PositionAt( double seconds );
        static int64_t CountAt( double seconds );
        static double ProgramDuration();

      private:
        void Levels( int64_t count, bool levels[ CHANNEL_COUNT ] ) const;

        double mSampleRate;
        int64_t mCountsPerIndex;
        uint64_t mTickSamples;
        uint64_t mNextSample = 0;
        int64_t mCount = 0;
        bool mLevel[ CHANNEL_COUNT ] = {};
    };
}

#endif // QUADRATURE_ENCODER_WAVEFORM_H
