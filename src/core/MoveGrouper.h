#ifndef QUADRATURE_MOVE_GROUPER_H
#define QUADRATURE_MOVE_GROUPER_H

#include "QuadratureDecoder.h"

#include <cstdint>
#include <vector>

namespace quadrature
{
    // Collapses runs of short count spans into single moves.
    //
    // A count held at least `hold_samples` is a hold and passes through unchanged. Consecutive
    // shorter spans form one Move from the held count before them to the one after. Index pulses
    // and errors inside a move are absorbed into it (counted); outside a move they pass through.
    // A move that ends where it started is jitter, e.g. an encoder dithering on an edge.
    class MoveGrouper
    {
      public:
        explicit MoveGrouper( uint64_t hold_samples );

        // Feed segments in order; finished segments are appended to `out`.
        void Add( const Segment& segment, std::vector<Segment>& out );

        // Decoding has reached `now` and the current count began at `open_start` (it has not been
        // emitted yet). Once that count has held for the hold time, a move in progress has ended:
        // it is emitted now rather than when the hold eventually ends.
        void Tick( uint64_t now, uint64_t open_start, int64_t open_count, int64_t open_travel, std::vector<Segment>& out );

        // Emit whatever is still buffered (end of data).
        void Flush( std::vector<Segment>& out );

      private:
        void EndMove( int64_t to_count, int64_t to_travel, std::vector<Segment>& out );

        uint64_t mHoldSamples;

        bool mInMove = false;
        Segment mMove;
        Segment mFirst; // the move's first span, as it arrived
        int mSpans = 0; // count spans in the move so far
        int64_t mFromTravel = 0;
        int64_t mLastCount = 0;
        int64_t mLastTravel = 0;

        bool mHaveHold = false;
        int64_t mHoldCount = 0;
        int64_t mHoldTravel = 0;
    };
}

#endif // QUADRATURE_MOVE_GROUPER_H
