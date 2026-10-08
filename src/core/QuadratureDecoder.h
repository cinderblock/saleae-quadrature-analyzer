#ifndef QUADRATURE_DECODER_H
#define QUADRATURE_DECODER_H

#include <cstdint>
#include <vector>

namespace quadrature
{
    enum class Decoding
    {
        X1 = 1, // one count per cycle: A edges while B is low
        X2 = 2, // two counts per cycle: every A edge
        X4 = 4, // four counts per cycle: every A and B edge
    };

    enum class IndexMode
    {
        Ignore, // Z is not used
        Mark,   // report each index pulse and the travel since the previous one
        Reset,  // as Mark, and the count restarts at 0 on each index pulse
    };

    struct DecoderConfig
    {
        Decoding decoding = Decoding::X4;
        IndexMode index_mode = IndexMode::Mark;
        bool invert = false; // count B-leads-A as forward
    };

    enum class SegmentType
    {
        Count, // the count held one value over the span
        Index, // an index pulse (one sample)
        Error, // A and B changed on the same sample: a state was skipped, direction unknown (one sample)
        Move,  // a run of short count spans grouped into one move (MoveGrouper only)
    };

    // A span of samples [start, end).
    struct Segment
    {
        SegmentType type = SegmentType::Count;
        uint64_t start = 0;
        uint64_t end = 0;
        int64_t count = 0;  // count over the span; for a move, where it ended
        int64_t travel = 0; // cumulative signed counts since the start, never reset by an index; for a move, at its end
        int64_t delta = 0;  // Count: the change that ended the span. Move: travel over the move
        int direction = 0;  // sign of `delta`

        // Index only: travel since the previous index pulse, when there was one.
        bool has_since_index = false;
        int64_t since_index = 0;

        // Move only.
        int64_t from_count = 0;
        int64_t min_count = 0;
        int64_t max_count = 0;
        uint32_t index_pulses = 0;
        uint32_t errors = 0;
    };

    // Turns the A/B (and optional Z) levels into count segments.
    //
    // (A, B) walks the Gray sequence 00 -> 10 -> 11 -> 01 -> 00 when A leads B (forward). Each step
    // between neighbors is one X4 count; X2 counts only A's edges and X1 only A's edges while B is
    // low, which keeps the count exact when the shaft dithers on an edge.
    class QuadratureDecoder
    {
      public:
        explicit QuadratureDecoder( const DecoderConfig& config );

        void Start( uint64_t sample, bool a, bool b, bool z );

        // The channel levels from `sample` on, after every edge at that sample. Call once per sample
        // on which any channel changed, in increasing sample order. Completed segments are appended
        // to `out`.
        void Update( uint64_t sample, bool a, bool b, bool z, std::vector<Segment>& out );

        // Close the open count span at `end` (end of data).
        void Flush( uint64_t end, std::vector<Segment>& out );

        int64_t Count() const
        {
            return mCount;
        }

        // Gray state index of (A, B): 00 -> 0, 10 -> 1, 11 -> 2, 01 -> 3.
        static int State( bool a, bool b );

      private:
        void CloseOpen( uint64_t end, int64_t delta, std::vector<Segment>& out );
        void EmitInstant( SegmentType type, uint64_t sample, std::vector<Segment>& out, Segment instant );

        DecoderConfig mConfig;
        bool mA = false;
        bool mB = false;
        bool mZ = false;

        int64_t mCount = 0;
        int64_t mTravel = 0;
        uint64_t mOpenStart = 0;

        bool mHaveIndex = false;
        int64_t mIndexTravel = 0;
    };
}

#endif // QUADRATURE_DECODER_H
